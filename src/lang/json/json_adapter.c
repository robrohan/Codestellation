/* JSON dependency adapter.
 *
 * Most JSON is data with no dependencies, so most .json files simply show
 * up as nodes. Two conventions do point at other files, and those become
 * edges:
 *
 *   - "$ref": "./common.json#/defs/x"   JSON Schema / OpenAPI references.
 *     The part before '#' is the file; "#/..." alone is a reference
 *     within the same document and is skipped, as are URLs.
 *   - "extends": "./tsconfig.base.json"  tsconfig / eslint / similar.
 *     Only relative paths -- "extends": "@tsconfig/node18" names a package.
 *
 *   - Declarations: each file declares its own absolute path (the same
 *     key the JS/TS adapters resolve `import data from "./x.json"` to).
 *   - Resolution: joined to the file's directory, cleaned lexically, tried
 *     as written and with ".json" appended.
 *
 * Node shapes (tree-sitter-json v0.24.8, confirmed by parsing samples):
 * pair has fields key and value; a string's text is its string_content
 * child (absent for "").
 */

#include "json_adapter.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_json(void);

static const char *JSON_EXTENSIONS[] = { ".json", NULL };

static void json_extract_declarations(const ParsedFile *file, TSQuery *decl_query, DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    DeclFact fact;
    fact.kind = DECL_PACKAGE;
    fact.qualified_name = file->path;
    sink(ctx, &fact);
}

/* A string node's text (its string_content child), or NULL. Caller frees. */
static char *string_contents(const ParsedFile *file, TSNode str) {
    if (ts_node_is_null(str) || strcmp(ts_node_type(str), "string") != 0) return NULL;
    uint32_t n = ts_node_named_child_count(str);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_named_child(str, i);
        if (strcmp(ts_node_type(c), "string_content") == 0) {
            uint32_t s = ts_node_start_byte(c), e = ts_node_end_byte(c);
            char *out = (char *)malloc(e - s + 1);
            memcpy(out, file->source + s, e - s);
            out[e - s] = '\0';
            return out;
        }
    }
    return NULL;
}

static void collect_refs(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    if (strcmp(ts_node_type(node), "pair") == 0) {
        char *key = string_contents(file, ts_node_child_by_field_name(node, "key", 3));
        char *value = key ? string_contents(file, ts_node_child_by_field_name(node, "value", 5)) : NULL;
        if (value) {
            bool is_ref = strcmp(key, "$ref") == 0;
            bool is_extends = strcmp(key, "extends") == 0;
            /* Strip a "#/pointer" fragment; what's left is the file. */
            value[strcspn(value, "#")] = '\0';
            bool is_url = strstr(value, "://") != NULL;
            if (value[0] && !is_url && (is_ref || (is_extends && value[0] == '.'))) {
                RefFact fact;
                fact.kind = REF_IMPORT_PATH;
                fact.raw_text = value;
                fact.scope_namespace = NULL;
                sink(ctx, &fact);
            }
        }
        free(key);
        free(value);
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, sink, ctx);
}

static void json_extract_references(const ParsedFile *file, TSQuery *ref_query, RefSinkFn sink, void *ctx) {
    (void)ref_query;
    collect_refs(ts_tree_root_node(file->tree), file, sink, ctx);
}

static char *json_resolve_reference(const char *raw_text, const char *referencing_file_path,
                                    const char *scope_namespace, const char **imported_namespaces,
                                    int imported_count, void *symbol_table_handle) {
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text || !raw_text[0]) return NULL;

    char *joined;
    if (raw_text[0] == '/') {
        joined = xstrdup(raw_text);
    } else {
        char *dir = path_dirname(referencing_file_path);
        joined = path_join(dir, raw_text);
        free(dir);
    }
    char *base = path_clean(joined);
    free(joined);
    if (symtab_get(table, base) >= 0) return base;

    size_t n = strlen(base);
    char *with_ext = (char *)malloc(n + 6);
    memcpy(with_ext, base, n);
    memcpy(with_ext + n, ".json", 6);
    free(base);
    if (symtab_get(table, with_ext) >= 0) return with_ext;
    free(with_ext);
    return NULL;
}

static LanguageAdapter g_json_adapter;
static int g_initialized = 0;

const LanguageAdapter *json_adapter_get(void) {
    if (!g_initialized) {
        g_json_adapter.name = "json";
        g_json_adapter.extensions = JSON_EXTENSIONS;
        g_json_adapter.extension_count = 1;
        g_json_adapter.ts_language = tree_sitter_json;
        g_json_adapter.decl_query = NULL;
        g_json_adapter.ref_query = NULL;
        g_json_adapter.extract_declarations = json_extract_declarations;
        g_json_adapter.extract_references = json_extract_references;
        g_json_adapter.resolve_reference = json_resolve_reference;
        g_initialized = 1;
    }
    return &g_json_adapter;
}
