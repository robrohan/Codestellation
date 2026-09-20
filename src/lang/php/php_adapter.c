/* PHP dependency adapter.
 *
 * PHP mixes two independent dependency signals that the other adapters
 * only ever have one of:
 *   - `require`/`require_once`/`include`/`include_once` name a *file path*,
 *     the same signal as C's #include (c_adapter.c).
 *   - `namespace`/`use`/`extends`/`implements` name a *backslash-qualified
 *     class*, the same signal as C#'s namespace/using (csharp_adapter.c).
 * Both are extracted here, but the LanguageAdapter interface's
 * resolve_reference doesn't receive the RefFact's kind -- only raw_text,
 * scope, and imports -- so which of the two a given raw_text is has to be
 * inferred syntactically at resolve time (see resolve_reference below).
 * This is a deliberate best-guess, same spirit as the Python/Go adapters'
 * documented simplifications, not a full PHP resolver.
 *
 *   - Declarations: every file emits its own path (so require/include
 *     targets can resolve), plus one DECL_TYPE per class/interface/trait/
 *     enum, qualified by its enclosing namespace with '\' as separator
 *     (namespace_name's own source text already includes the backslashes
 *     for a multi-segment name, so no manual joining beyond one level is
 *     needed). Namespace declarations are only honored where PHP itself
 *     allows them: as direct statements at the top of the file (see the
 *     root-scanning loop in both extract_declarations and
 *     extract_references, mirroring csharp_adapter.c's handling of C#'s
 *     file-scoped namespace form) -- a `namespace_definition` found
 *     anywhere else in the tree is simply walked through generically and
 *     has no effect, since that's not legal PHP.
 *
 *   - References:
 *       `use Foo\Bar;`                 -> ref "Foo\Bar" (REF_IMPORT_PATH)
 *       `use Foo\Bar as Baz;`          -> ref "Foo\Bar" (alias ignored)
 *       `use Foo\Bar\{Baz, Qux};`      -> refs "Foo\Bar\Baz", "Foo\Bar\Qux"
 *       `require`/`include` (+ _once)  -> ref of the literal string/
 *         no-interpolation double-quoted argument (REF_MODULE_REQUIRE);
 *         a computed argument (`__DIR__ . '/x.php'`, heredoc, interpolated
 *         string) can't be resolved statically and is skipped, same
 *         limitation as C's #include only handling string_literal.
 *       `class Foo extends Bar implements Baz` -> refs "Bar", "Baz"
 *         (REF_TYPE_NAME, scope = enclosing namespace)
 *
 *   - Resolution: raw_text containing '/' is treated as a require/include
 *     path (resolved relative to the referencing file's directory, like
 *     c_adapter.c). Otherwise it's treated as a class reference: a
 *     `namespace\Foo` prefix is relative to the current scope; a leading
 *     '\' is a fully-qualified name (root namespace); otherwise try raw
 *     as-is, then scope-qualified. If still unresolved, try each
 *     `use`-imported name -- unlike C#/VB.NET's `using`/`Imports` (which
 *     import a *namespace*, so a bare reference is resolved by prepending
 *     it), PHP's `use` imports one fully-qualified *class name*, aliased by
 *     its last segment by default, so a bare reference is matched against
 *     each import's own last segment rather than qualified by it (an
 *     explicit `use X as Y;` rename isn't tracked, since the alias name
 *     itself is discarded during extraction -- a known limitation). Finally
 *     -- since a same-directory `require 'x.php'` has no separator either
 *     -- fall back to a same-directory path lookup. PHP's own class-name
 *     case-insensitivity isn't emulated (matching this codebase's
 *     case-sensitive resolution everywhere else).
 *
 * Node shapes below were confirmed against tree-sitter-php v0.24.2's
 * php_only/src/node-types.json, not guessed:
 *   - program (root): children are php_tag/php_end_tag/statement*.
 *     `statement` is a supertype (hidden -- never a real node); its
 *     alternatives (namespace_definition, class_declaration,
 *     expression_statement, ...) appear directly as the actual nodes.
 *   - namespace_definition: field "name" -> namespace_name (optional --
 *     absent means the global namespace); field "body" ->
 *     compound_statement, present only for the block form
 *     (`namespace X { ... }`); absent for the statement form
 *     (`namespace X;`), which applies to the rest of the file.
 *   - class/interface/trait/enum_declaration: field "name" -> name (a
 *     single, unqualified segment); field "body" -> declaration_list.
 *     base_clause ("extends") and class_interface_clause ("implements")
 *     are NOT fields -- unnamed children of class_declaration, found by
 *     node type, each holding name | qualified_name | relative_name
 *     children directly (no wrapper/supertype).
 *   - namespace_use_declaration: children (not fields) are
 *     namespace_use_clause (flat form, possibly several: `use A, B;`) or a
 *     namespace_name prefix + field "body": namespace_use_group holding
 *     namespace_use_clause children (grouped form).
 *   - namespace_use_clause: fields "alias" and "type" (const|function);
 *     the imported name itself is the one non-field child (name |
 *     qualified_name).
 *   - require_expression / require_once_expression / include_expression /
 *     include_once_expression: single child, the argument expression
 *     (unwrapped through parenthesized_expression if present).
 *   - string: single-quoted literal, e.g. 'foo.php'. encapsed_string:
 *     double-quoted; only resolved here when every child is
 *     string_content/escape_sequence (no `"$var"`-style interpolation).
 */

#include "php_adapter.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_php_only(void);

static const char *PHP_EXTENSIONS[] = { ".php", NULL };

static char *strndup_local(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *node_text(const ParsedFile *file, TSNode node) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    return strndup_local(file->source + start, end - start);
}

/* base\name, or a copy of name if base is NULL/empty (global namespace). */
static char *join_bs(const char *base, const char *name) {
    if (!base || base[0] == '\0') return xstrdup(name ? name : "");
    if (!name || name[0] == '\0') return xstrdup(base);
    size_t bl = strlen(base), nl = strlen(name);
    char *out = (char *)malloc(bl + 1 + nl + 1);
    memcpy(out, base, bl);
    out[bl] = '\\';
    memcpy(out + bl + 1, name, nl);
    out[bl + 1 + nl] = '\0';
    return out;
}

static int is_type_decl(const char *type) {
    return strcmp(type, "class_declaration") == 0 || strcmp(type, "interface_declaration") == 0 ||
           strcmp(type, "trait_declaration") == 0 || strcmp(type, "enum_declaration") == 0;
}

/* ---- declarations ---- */

static void collect_decls(TSNode node, const ParsedFile *file, const char *ns,
                           DeclSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);

    if (is_type_decl(type)) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char *seg = node_text(file, name_node);
            char *qualified = join_bs(ns, seg);
            free(seg);

            DeclFact fact;
            fact.kind = DECL_TYPE;
            fact.qualified_name = qualified;
            sink(ctx, &fact);

            free(qualified);
        }
        return;
    }

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_decls(ts_node_named_child(node, i), file, ns, sink, ctx);
}

static void php_extract_declarations(const ParsedFile *file, TSQuery *decl_query,
                                      DeclSinkFn sink, void *ctx) {
    (void)decl_query;

    /* The file's own path -- lets require/include (which name a path, not
     * a class) resolve to it. */
    DeclFact path_fact;
    path_fact.kind = DECL_TYPE;
    path_fact.qualified_name = file->path;
    sink(ctx, &path_fact);

    TSNode root = ts_tree_root_node(file->tree);
    uint32_t n = ts_node_named_child_count(root);
    char *file_ns = NULL;
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_named_child(root, i);
        if (strcmp(ts_node_type(child), "namespace_definition") == 0) {
            TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
            char *seg = ts_node_is_null(name_node) ? NULL : node_text(file, name_node);
            TSNode body = ts_node_child_by_field_name(child, "body", 4);
            if (ts_node_is_null(body)) {
                /* Statement form: applies to the rest of the file. */
                free(file_ns);
                file_ns = seg;
            } else {
                /* Block form: scoped to its own body only. */
                uint32_t bn = ts_node_named_child_count(body);
                for (uint32_t bi = 0; bi < bn; bi++) {
                    collect_decls(ts_node_named_child(body, bi), file, seg ? seg : "", sink, ctx);
                }
                free(seg);
            }
            continue;
        }
        collect_decls(child, file, file_ns ? file_ns : "", sink, ctx);
    }
    free(file_ns);
}

/* ---- references ---- */

static void emit_ref(RefSinkFn sink, void *ctx, RefKind kind, const char *raw, const char *scope_ns) {
    RefFact fact;
    fact.kind = kind;
    fact.raw_text = raw;
    fact.scope_namespace = scope_ns;
    sink(ctx, &fact);
}

static TSNode unwrap_paren(TSNode node) {
    while (strcmp(ts_node_type(node), "parenthesized_expression") == 0 &&
           ts_node_named_child_count(node) == 1) {
        node = ts_node_named_child(node, 0);
    }
    return node;
}

/* Extracts the literal text of a plain string/encapsed_string node (no
 * interpolation), stripping the surrounding quotes. Returns NULL if the
 * node isn't a statically-resolvable string. Caller frees. */
static char *static_string_text(const ParsedFile *file, TSNode node) {
    const char *type = ts_node_type(node);
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);

    if (strcmp(type, "encapsed_string") == 0) {
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++) {
            const char *ct = ts_node_type(ts_node_named_child(node, i));
            if (strcmp(ct, "string_content") != 0 && strcmp(ct, "escape_sequence") != 0) return NULL;
        }
    } else if (strcmp(type, "string") != 0) {
        return NULL; /* heredoc/nowdoc/computed expression -- not handled */
    }

    uint32_t s = start;
    if (s < end && (file->source[s] == 'b' || file->source[s] == 'B')) s++;
    if (end >= s + 2) return strndup_local(file->source + s + 1, (end - 1) - (s + 1));
    return NULL;
}

/* The one non-field child of a namespace_use_clause -- the actual imported
 * name (a `type`/"alias" field child is skipped). Caller frees. */
static char *clause_inner_name(const ParsedFile *file, TSNode clause) {
    uint32_t n = ts_node_child_count(clause);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_child(clause, i);
        if (!ts_node_is_named(c)) continue;
        const char *field = ts_node_field_name_for_child(clause, i);
        if (field && (strcmp(field, "alias") == 0 || strcmp(field, "type") == 0)) continue;
        const char *ct = ts_node_type(c);
        if (strcmp(ct, "name") == 0 || strcmp(ct, "qualified_name") == 0) return node_text(file, c);
    }
    return NULL;
}

static void collect_use_declaration(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    TSNode body = ts_node_child_by_field_name(node, "body", 4);
    if (!ts_node_is_null(body)) {
        /* Grouped form: use Prefix\{A, B}; */
        char *prefix = NULL;
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++) {
            TSNode c = ts_node_named_child(node, i);
            if (strcmp(ts_node_type(c), "namespace_name") == 0) { prefix = node_text(file, c); break; }
        }
        uint32_t gn = ts_node_named_child_count(body);
        for (uint32_t i = 0; i < gn; i++) {
            TSNode clause = ts_node_named_child(body, i);
            if (strcmp(ts_node_type(clause), "namespace_use_clause") != 0) continue;
            char *inner = clause_inner_name(file, clause);
            if (inner) {
                char *combined = join_bs(prefix, inner);
                emit_ref(sink, ctx, REF_IMPORT_PATH, combined, NULL);
                free(combined);
                free(inner);
            }
        }
        free(prefix);
        return;
    }

    /* Flat form: use A\B, C\D; -- one or more direct clause children. */
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode clause = ts_node_named_child(node, i);
        if (strcmp(ts_node_type(clause), "namespace_use_clause") != 0) continue;
        char *inner = clause_inner_name(file, clause);
        if (inner) {
            emit_ref(sink, ctx, REF_IMPORT_PATH, inner, NULL);
            free(inner);
        }
    }
}

static void emit_type_refs_from_clause(TSNode clause, const ParsedFile *file, const char *ns,
                                        RefSinkFn sink, void *ctx) {
    uint32_t n = ts_node_named_child_count(clause);
    for (uint32_t i = 0; i < n; i++) {
        char *raw = node_text(file, ts_node_named_child(clause, i));
        emit_ref(sink, ctx, REF_TYPE_NAME, raw, ns);
        free(raw);
    }
}

static void collect_refs(TSNode node, const ParsedFile *file, const char *ns,
                          RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);

    if (strcmp(type, "namespace_use_declaration") == 0) {
        collect_use_declaration(node, file, sink, ctx);
        return;
    }

    if (strcmp(type, "require_expression") == 0 || strcmp(type, "require_once_expression") == 0 ||
        strcmp(type, "include_expression") == 0 || strcmp(type, "include_once_expression") == 0) {
        if (ts_node_named_child_count(node) >= 1) {
            TSNode target = unwrap_paren(ts_node_named_child(node, 0));
            char *raw = static_string_text(file, target);
            if (raw) {
                emit_ref(sink, ctx, REF_MODULE_REQUIRE, raw, NULL);
                free(raw);
            }
        }
        return;
    }

    if (is_type_decl(type)) {
        uint32_t n = ts_node_named_child_count(node); /* base_clause/class_interface_clause: unnamed-field children */
        for (uint32_t i = 0; i < n; i++) {
            TSNode c = ts_node_named_child(node, i);
            const char *ct = ts_node_type(c);
            if (strcmp(ct, "base_clause") == 0 || strcmp(ct, "class_interface_clause") == 0) {
                emit_type_refs_from_clause(c, file, ns, sink, ctx);
            }
        }
        return;
    }

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, ns, sink, ctx);
}

static void php_extract_references(const ParsedFile *file, TSQuery *ref_query,
                                    RefSinkFn sink, void *ctx) {
    (void)ref_query;
    TSNode root = ts_tree_root_node(file->tree);
    uint32_t n = ts_node_named_child_count(root);
    char *file_ns = NULL;
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_named_child(root, i);
        if (strcmp(ts_node_type(child), "namespace_definition") == 0) {
            TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
            char *seg = ts_node_is_null(name_node) ? NULL : node_text(file, name_node);
            TSNode body = ts_node_child_by_field_name(child, "body", 4);
            if (ts_node_is_null(body)) {
                free(file_ns);
                file_ns = seg;
            } else {
                uint32_t bn = ts_node_named_child_count(body);
                for (uint32_t bi = 0; bi < bn; bi++) {
                    collect_refs(ts_node_named_child(body, bi), file, seg ? seg : "", sink, ctx);
                }
                free(seg);
            }
            continue;
        }
        collect_refs(child, file, file_ns ? file_ns : "", sink, ctx);
    }
    free(file_ns);
}

/* ---- resolution ---- */

static char *resolve_as_path(const char *raw_text, const char *referencing_file_path, SymbolTable *table) {
    char *dir = path_dirname(referencing_file_path);
    char *joined = path_join(dir, raw_text);
    char *normalized = path_normalize(joined);
    free(dir);
    free(joined);
    if (normalized) {
        if (symtab_get(table, normalized) >= 0) return normalized;
        free(normalized);
    }
    return NULL;
}

static char *php_resolve_reference(const char *raw_text,
                                    const char *referencing_file_path,
                                    const char *scope_namespace,
                                    const char **imported_namespaces,
                                    int imported_count,
                                    void *symbol_table_handle) {
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text || !raw_text[0]) return NULL;

    /* A path separator can only appear in a require/include target here --
     * PHP namespaces use '\' exclusively, never '/'. */
    if (strchr(raw_text, '/')) return resolve_as_path(raw_text, referencing_file_path, table);

    static const char NS_RELATIVE_PREFIX[] = "namespace\\";
    if (strncmp(raw_text, NS_RELATIVE_PREFIX, sizeof(NS_RELATIVE_PREFIX) - 1) == 0) {
        char *candidate = join_bs(scope_namespace, raw_text + sizeof(NS_RELATIVE_PREFIX) - 1);
        if (symtab_get(table, candidate) >= 0) return candidate;
        free(candidate);
        return NULL;
    }

    const char *lookup = (raw_text[0] == '\\') ? raw_text + 1 : raw_text;

    if (symtab_get(table, lookup) >= 0) return xstrdup(lookup);

    if (scope_namespace && scope_namespace[0] != '\0') {
        char *candidate = join_bs(scope_namespace, lookup);
        if (symtab_get(table, candidate) >= 0) return candidate;
        free(candidate);
    }

    /* Unlike C#/VB.NET's `using`/`Imports` (which import a namespace, so a
     * bare reference is qualified by prepending it), PHP's `use` imports a
     * single fully-qualified class name -- effectively a short-name alias.
     * `use App\Models\User;` lets bare `User` resolve directly to
     * `App\Models\User`, not to some `User`-prefixed namespace. Approximate
     * that by matching `lookup` against each import's own last segment
     * (this misses an explicit `use X as Y;` rename, since the alias name
     * itself isn't tracked -- a known limitation, same spirit as the
     * relative-import edge cases elsewhere in this codebase). */
    for (int i = 0; i < imported_count; i++) {
        const char *imp = imported_namespaces[i];
        if (!imp) continue;
        const char *slash = strrchr(imp, '\\');
        const char *last_segment = slash ? slash + 1 : imp;
        if (strcmp(last_segment, lookup) == 0 && symtab_get(table, imp) >= 0) return xstrdup(imp);
    }

    /* No segment separator and no namespace hit -- may be a same-directory
     * require/include target named without a path, e.g. require 'x.php'. */
    return resolve_as_path(raw_text, referencing_file_path, table);
}

static LanguageAdapter g_php_adapter;
static int g_initialized = 0;

const LanguageAdapter *php_adapter_get(void) {
    if (!g_initialized) {
        g_php_adapter.name = "php";
        g_php_adapter.extensions = PHP_EXTENSIONS;
        g_php_adapter.extension_count = 1;
        g_php_adapter.ts_language = tree_sitter_php_only;
        g_php_adapter.decl_query = NULL; /* extraction walks the tree directly, see header comment */
        g_php_adapter.ref_query = NULL;
        g_php_adapter.extract_declarations = php_extract_declarations;
        g_php_adapter.extract_references = php_extract_references;
        g_php_adapter.resolve_reference = php_resolve_reference;
        g_initialized = 1;
    }
    return &g_php_adapter;
}
