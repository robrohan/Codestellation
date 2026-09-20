/* Visual Basic .NET dependency adapter.
 *
 * Structurally the same problem as the C# adapter (csharp_adapter.c):
 * namespaces nest, types can be referenced several ways, and a bare
 * reference has to be disambiguated against the file's `Imports`. Extraction
 * walks the parse tree directly (not a flat query) so it can track the
 * current enclosing namespace as it recurses.
 *
 * There is no long-established, widely-adopted tree-sitter grammar for
 * VB.NET (unlike C#'s tree-sitter-c-sharp) -- this adapter is built against
 * CodeAnt-AI/tree-sitter-vb-dotnet, a young/unofficial grammar with no
 * tagged releases, pinned to a specific commit in the top-level
 * CMakeLists.txt. Its node shapes may shift under future updates more than
 * the other adapters' grammars, and it has at least one known real-world
 * gap: an `Inherits X` / `Implements X` line as its own statement inside a
 * class/interface/structure body -- the standard, and only, way VB.NET
 * expresses either -- is misparsed as a field declaration followed by an
 * ERROR node rather than populating the "inherits"/"implements" fields
 * documented in node-types.json (confirmed against this grammar's actual
 * output, not assumed). The extraction code below is written correctly
 * against that documented schema and will pick up inherits_clause/
 * implements_clause nodes if a future grammar version produces them, but
 * as of the pinned commit, base/interface type_reference edges essentially
 * never materialize -- namespace declarations, type declarations, and
 * `Imports`-based import edges are unaffected and do work.
 *
 * Node shapes below were confirmed against that grammar's
 * src/node-types.json, not guessed. Notably, unlike tree-sitter-c-sharp,
 * this grammar has no supertype nodes at all -- `type_declaration` and
 * `type` are real wrapper nodes that each hold exactly one meaningful
 * child, not hidden aliases:
 *   - namespace_block: field "name" -> namespace_name (its own source text
 *     is already the full dotted segment, e.g. "Foo.Bar"); no separate
 *     "body" field -- statements are direct children of the block itself.
 *     Unlike C#, there's no file-scoped shorthand -- every namespace is a
 *     `Namespace ... End Namespace` block.
 *   - type_declaration: a transparent wrapper with one child, one of
 *     class_block | interface_block | module_block | structure_block |
 *     enum_block | delegate_declaration.
 *   - class_block / interface_block / structure_block: field "name" ->
 *     identifier (single segment, never qualified); optional fields
 *     "inherits" -> inherits_clause, "implements" -> implements_clause
 *     (class_block only has both; interface_block only "inherits";
 *     structure_block only "implements"). module_block/enum_block have
 *     neither. None of these nest further type declarations in this
 *     grammar (their children are members: fields/methods/properties/etc,
 *     not type_declaration), so no recursion into their bodies is needed.
 *   - inherits_clause / implements_clause: children (multiple) are `type`
 *     wrapper nodes, one per listed base/interface.
 *   - type: a transparent wrapper, one child from generic_type |
 *     namespace_name | primitive_type | array_type | array_rank_specifier.
 *   - generic_type: children include a namespace_name (the base type) plus
 *     a type_argument_list -- like C#'s generic_name handling, only the
 *     base name is used as the dependency target.
 *   - imports_statement: field "namespace" (multiple) -> namespace_name,
 *     one per comma-separated name in `Imports A.B, C.D`. Aliased imports
 *     (`Imports X = A.B`) aren't represented as a distinct node shape in
 *     this grammar's node-types.json and aren't special-cased here.
 */

#include "vbnet_adapter.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_vb_dotnet(void);

static const char *VBNET_EXTENSIONS[] = { ".vb", NULL };

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

/* base.name, or a copy of name if base is NULL/empty (top-level scope). */
static char *join_dot(const char *base, const char *name) {
    if (!base || base[0] == '\0') return xstrdup(name ? name : "");
    if (!name || name[0] == '\0') return xstrdup(base);
    size_t bl = strlen(base), nl = strlen(name);
    char *out = (char *)malloc(bl + 1 + nl + 1);
    memcpy(out, base, bl);
    out[bl] = '.';
    memcpy(out + bl + 1, name, nl);
    out[bl + 1 + nl] = '\0';
    return out;
}

/* `type` wraps exactly one meaningful child; for `generic_type` only the
 * base name (its namespace_name child) is the dependency target, matching
 * the C# adapter's generic_name handling. */
static char *type_ref_text(const ParsedFile *file, TSNode type_node) {
    if (ts_node_named_child_count(type_node) == 0) return node_text(file, type_node);
    TSNode inner = ts_node_named_child(type_node, 0);
    if (strcmp(ts_node_type(inner), "generic_type") == 0) {
        uint32_t n = ts_node_named_child_count(inner);
        for (uint32_t i = 0; i < n; i++) {
            TSNode c = ts_node_named_child(inner, i);
            if (strcmp(ts_node_type(c), "namespace_name") == 0) return node_text(file, c);
        }
        return node_text(file, inner);
    }
    return node_text(file, inner);
}

static int is_type_block(const char *type) {
    return strcmp(type, "class_block") == 0 || strcmp(type, "interface_block") == 0 ||
           strcmp(type, "module_block") == 0 || strcmp(type, "structure_block") == 0 ||
           strcmp(type, "enum_block") == 0;
}

/* ---- declarations ---- */

static void collect_decls(TSNode node, const ParsedFile *file, const char *ns,
                           DeclSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);

    if (strcmp(type, "namespace_block") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        char *seg = ts_node_is_null(name_node) ? NULL : node_text(file, name_node);
        char *child_ns = join_dot(ns, seg);
        free(seg);
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++) collect_decls(ts_node_named_child(node, i), file, child_ns, sink, ctx);
        free(child_ns);
        return;
    }

    if (is_type_block(type)) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char *seg = node_text(file, name_node);
            char *qualified = join_dot(ns, seg);
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

static void vbnet_extract_declarations(const ParsedFile *file, TSQuery *decl_query,
                                        DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    TSNode root = ts_tree_root_node(file->tree);
    uint32_t n = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < n; i++) collect_decls(ts_node_named_child(root, i), file, "", sink, ctx);
}

/* ---- references ---- */

static void emit_type_refs_from_clause(TSNode clause, const ParsedFile *file, const char *ns,
                                        RefSinkFn sink, void *ctx) {
    uint32_t n = ts_node_named_child_count(clause);
    for (uint32_t i = 0; i < n; i++) {
        TSNode t = ts_node_named_child(clause, i);
        char *raw = type_ref_text(file, t);
        RefFact fact;
        fact.kind = REF_TYPE_NAME;
        fact.raw_text = raw;
        fact.scope_namespace = ns; /* enclosing namespace, not this type's own qualified name */
        sink(ctx, &fact);
        free(raw);
    }
}

static void collect_refs(TSNode node, const ParsedFile *file, const char *ns,
                          RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);

    if (strcmp(type, "imports_statement") == 0) {
        uint32_t n = ts_node_child_count(node);
        for (uint32_t i = 0; i < n; i++) {
            const char *field = ts_node_field_name_for_child(node, i);
            if (!field || strcmp(field, "namespace") != 0) continue;
            char *raw = node_text(file, ts_node_child(node, i));
            RefFact fact;
            fact.kind = REF_IMPORT_PATH;
            fact.raw_text = raw;
            fact.scope_namespace = NULL;
            sink(ctx, &fact);
            free(raw);
        }
        return;
    }

    if (strcmp(type, "namespace_block") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        char *seg = ts_node_is_null(name_node) ? NULL : node_text(file, name_node);
        char *child_ns = join_dot(ns, seg);
        free(seg);
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, child_ns, sink, ctx);
        free(child_ns);
        return;
    }

    if (is_type_block(type)) {
        TSNode inherits = ts_node_child_by_field_name(node, "inherits", 8);
        if (!ts_node_is_null(inherits)) emit_type_refs_from_clause(inherits, file, ns, sink, ctx);
        TSNode implements = ts_node_child_by_field_name(node, "implements", 10);
        if (!ts_node_is_null(implements)) emit_type_refs_from_clause(implements, file, ns, sink, ctx);
        return;
    }

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, ns, sink, ctx);
}

static void vbnet_extract_references(const ParsedFile *file, TSQuery *ref_query,
                                      RefSinkFn sink, void *ctx) {
    (void)ref_query;
    TSNode root = ts_tree_root_node(file->tree);
    uint32_t n = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(root, i), file, "", sink, ctx);
}

/* ---- resolution ----
 * Same strategy as the C# adapter: raw text as-is, then qualified against
 * the enclosing namespace, then against each `Imports`-ed namespace. First
 * hit against the real symbol table wins; a miss (BCL type, external
 * dependency) is left unresolved. */
static char *vbnet_resolve_reference(const char *raw_text,
                                      const char *referencing_file_path,
                                      const char *scope_namespace,
                                      const char **imported_namespaces,
                                      int imported_count,
                                      void *symbol_table_handle) {
    (void)referencing_file_path;
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text) return NULL;

    if (symtab_get(table, raw_text) >= 0) return xstrdup(raw_text);

    if (scope_namespace && scope_namespace[0] != '\0') {
        char *candidate = join_dot(scope_namespace, raw_text);
        if (symtab_get(table, candidate) >= 0) return candidate;
        free(candidate);
    }

    for (int i = 0; i < imported_count; i++) {
        if (!imported_namespaces[i]) continue;
        char *candidate = join_dot(imported_namespaces[i], raw_text);
        if (symtab_get(table, candidate) >= 0) return candidate;
        free(candidate);
    }

    return NULL;
}

static LanguageAdapter g_vbnet_adapter;
static int g_initialized = 0;

const LanguageAdapter *vbnet_adapter_get(void) {
    if (!g_initialized) {
        g_vbnet_adapter.name = "vbnet";
        g_vbnet_adapter.extensions = VBNET_EXTENSIONS;
        g_vbnet_adapter.extension_count = 1;
        g_vbnet_adapter.ts_language = tree_sitter_vb_dotnet;
        g_vbnet_adapter.decl_query = NULL; /* extraction walks the tree directly, see header comment */
        g_vbnet_adapter.ref_query = NULL;
        g_vbnet_adapter.extract_declarations = vbnet_extract_declarations;
        g_vbnet_adapter.extract_references = vbnet_extract_references;
        g_vbnet_adapter.resolve_reference = vbnet_resolve_reference;
        g_initialized = 1;
    }
    return &g_vbnet_adapter;
}
