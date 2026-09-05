/* C# dependency adapter.
 *
 * Unlike the C adapter, this one needs real scoped symbol resolution:
 * namespaces nest, types can be qualified several ways, and a bare type
 * reference has to be disambiguated against the file's `using` imports.
 * Declaration/reference extraction is done by walking the parse tree
 * directly (rather than through a flat tree-sitter query) so the walker
 * can track the current enclosing namespace/type as it recurses -- a
 * flat query match list has no notion of "which namespace was this
 * match found inside." The queries/ dir documents the node shapes
 * involved but isn't compiled/used at runtime.
 *
 * Node shapes below were confirmed against tree-sitter-c-sharp v0.23.5's
 * src/node-types.json, not guessed:
 *   - namespace_declaration / file_scoped_namespace_declaration: field
 *     "name" (identifier | qualified_name | generic_name | alias_qualified_name),
 *     field "body": declaration_list (block form only).
 *   - class/interface/struct/enum_declaration: field "name": identifier,
 *     field "body": declaration_list (enum: enum_member_declaration_list,
 *     not walked further -- enums don't nest types).
 *   - base_list is NOT a field -- it's an unnamed child of the above,
 *     found by node type. Its own children are "type" (a tree-sitter
 *     *supertype* -- never appears as an actual node; identifier /
 *     qualified_name / generic_name / etc. appear directly in its place),
 *     plus argument_list/primary_constructor_base_type (skipped, not
 *     type references).
 *   - using_directive: field "name" only holds an alias
 *     (`using Alias = X;`); the actual imported name is always the
 *     directive's last child.
 */

#include "csharp_adapter.h"
#include "../../common/symtab.h"
#include <tree_sitter/api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_c_sharp(void);

static const char *CSHARP_EXTENSIONS[] = { ".cs", NULL };

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    memcpy(p, s, n);
    return p;
}

/* strndup isn't universally available (no MSVC), so a local copy. */
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

/* For `generic_name` (e.g. `List<Foo>` used as a base type), only the
 * base identifier is interesting as a dependency target -- the type
 * arguments aren't walked in this MVP. Everything else is used verbatim. */
static char *reference_name_from_node(const ParsedFile *file, TSNode node) {
    if (strcmp(ts_node_type(node), "generic_name") == 0 && ts_node_named_child_count(node) > 0) {
        return node_text(file, ts_node_named_child(node, 0));
    }
    return node_text(file, node);
}

/* ---- declarations ---- */

static void collect_decls(TSNode node, const ParsedFile *file, const char *ns,
                           DeclSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);

    if (strcmp(type, "namespace_declaration") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        char *seg = ts_node_is_null(name_node) ? NULL : node_text(file, name_node);
        char *child_ns = join_dot(ns, seg);
        free(seg);
        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body)) {
            uint32_t n = ts_node_named_child_count(body);
            for (uint32_t i = 0; i < n; i++) collect_decls(ts_node_named_child(body, i), file, child_ns, sink, ctx);
        }
        free(child_ns);
        return;
    }

    if (strcmp(type, "class_declaration") == 0 || strcmp(type, "interface_declaration") == 0 ||
        strcmp(type, "struct_declaration") == 0 || strcmp(type, "enum_declaration") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        if (!ts_node_is_null(name_node)) {
            char *seg = node_text(file, name_node);
            char *qualified = join_dot(ns, seg);
            free(seg);

            DeclFact fact;
            fact.kind = DECL_TYPE;
            fact.qualified_name = qualified;
            sink(ctx, &fact);

            TSNode body = ts_node_child_by_field_name(node, "body", 4);
            if (!ts_node_is_null(body) && strcmp(ts_node_type(body), "declaration_list") == 0) {
                uint32_t n = ts_node_named_child_count(body);
                for (uint32_t i = 0; i < n; i++) collect_decls(ts_node_named_child(body, i), file, qualified, sink, ctx);
            }
            free(qualified);
        }
        return;
    }

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_decls(ts_node_named_child(node, i), file, ns, sink, ctx);
}

static void csharp_extract_declarations(const ParsedFile *file, TSQuery *decl_query,
                                         DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    TSNode root = ts_tree_root_node(file->tree);
    uint32_t n = ts_node_named_child_count(root);
    char *file_ns = NULL;
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_named_child(root, i);
        if (strcmp(ts_node_type(child), "file_scoped_namespace_declaration") == 0) {
            TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
            if (!ts_node_is_null(name_node)) {
                free(file_ns);
                file_ns = node_text(file, name_node);
            }
            continue;
        }
        collect_decls(child, file, file_ns ? file_ns : "", sink, ctx);
    }
    free(file_ns);
}

/* ---- references ---- */

static void collect_refs(TSNode node, const ParsedFile *file, const char *ns,
                          RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);

    if (strcmp(type, "using_directive") == 0) {
        uint32_t n = ts_node_named_child_count(node);
        if (n > 0) {
            char *raw = reference_name_from_node(file, ts_node_named_child(node, n - 1));
            RefFact fact;
            fact.kind = REF_IMPORT_PATH;
            fact.raw_text = raw;
            fact.scope_namespace = NULL;
            sink(ctx, &fact);
            free(raw);
        }
        return;
    }

    if (strcmp(type, "namespace_declaration") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        char *seg = ts_node_is_null(name_node) ? NULL : node_text(file, name_node);
        char *child_ns = join_dot(ns, seg);
        free(seg);
        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body)) {
            uint32_t n = ts_node_named_child_count(body);
            for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(body, i), file, child_ns, sink, ctx);
        }
        free(child_ns);
        return;
    }

    if (strcmp(type, "class_declaration") == 0 || strcmp(type, "interface_declaration") == 0 ||
        strcmp(type, "struct_declaration") == 0 || strcmp(type, "enum_declaration") == 0) {
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        char *seg = ts_node_is_null(name_node) ? NULL : node_text(file, name_node);
        char *qualified = join_dot(ns, seg);
        free(seg);

        uint32_t n = ts_node_child_count(node); /* base_list is unnamed-field, scan all children */
        for (uint32_t i = 0; i < n; i++) {
            TSNode c = ts_node_child(node, i);
            if (strcmp(ts_node_type(c), "base_list") != 0) continue;
            uint32_t bn = ts_node_named_child_count(c);
            for (uint32_t bi = 0; bi < bn; bi++) {
                TSNode base_item = ts_node_named_child(c, bi);
                const char *bt = ts_node_type(base_item);
                if (strcmp(bt, "argument_list") == 0 || strcmp(bt, "primary_constructor_base_type") == 0) continue;
                char *raw = reference_name_from_node(file, base_item);
                RefFact fact;
                fact.kind = REF_TYPE_NAME;
                fact.raw_text = raw;
                fact.scope_namespace = ns; /* enclosing namespace, not this type's own qualified name */
                sink(ctx, &fact);
                free(raw);
            }
        }

        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body) && strcmp(ts_node_type(body), "declaration_list") == 0) {
            uint32_t dn = ts_node_named_child_count(body);
            for (uint32_t i = 0; i < dn; i++) collect_refs(ts_node_named_child(body, i), file, qualified, sink, ctx);
        }
        free(qualified);
        return;
    }

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, ns, sink, ctx);
}

static void csharp_extract_references(const ParsedFile *file, TSQuery *ref_query,
                                       RefSinkFn sink, void *ctx) {
    (void)ref_query;
    TSNode root = ts_tree_root_node(file->tree);
    uint32_t n = ts_node_named_child_count(root);
    char *file_ns = NULL;
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_named_child(root, i);
        if (strcmp(ts_node_type(child), "file_scoped_namespace_declaration") == 0) {
            TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
            if (!ts_node_is_null(name_node)) {
                free(file_ns);
                file_ns = node_text(file, name_node);
            }
            continue;
        }
        collect_refs(child, file, file_ns ? file_ns : "", sink, ctx);
    }
    free(file_ns);
}

/* ---- resolution ----
 * Try the raw text as-is (it may already be a qualified_name's full
 * text), then qualified against the enclosing namespace (same-namespace
 * reference), then against each `using`-imported namespace. First hit
 * against the real symbol table wins; a miss (e.g. a BCL type like
 * System.String, or a genuinely external dependency) is left
 * unresolved rather than guessed at. */
static char *csharp_resolve_reference(const char *raw_text,
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

static LanguageAdapter g_csharp_adapter;
static int g_initialized = 0;

const LanguageAdapter *csharp_adapter_get(void) {
    if (!g_initialized) {
        g_csharp_adapter.name = "csharp";
        g_csharp_adapter.extensions = CSHARP_EXTENSIONS;
        g_csharp_adapter.extension_count = 1;
        g_csharp_adapter.ts_language = tree_sitter_c_sharp;
        g_csharp_adapter.decl_query = NULL; /* extraction walks the tree directly, see header comment */
        g_csharp_adapter.ref_query = NULL;
        g_csharp_adapter.extract_declarations = csharp_extract_declarations;
        g_csharp_adapter.extract_references = csharp_extract_references;
        g_csharp_adapter.resolve_reference = csharp_resolve_reference;
        g_initialized = 1;
    }
    return &g_csharp_adapter;
}
