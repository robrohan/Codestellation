/* C dependency adapter — also the pipeline's smoke-test language.
 *
 * C's only dependency signal is a local #include "foo.h", a direct file
 * path rather than a symbol reference, so there's no real "declaration" to
 * extract other than "this file exists" (used so resolve.c's generic
 * symbol table can look files up by their own normalized path). */

#include "c_adapter.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_c(void);

static const char *C_EXTENSIONS[] = { ".c", ".h", NULL };

static const char *REF_QUERY_SRC =
    "(preproc_include path: (string_literal) @path.local)\n";

/* Strips the surrounding quotes tree-sitter includes in the string_literal
 * node text (e.g. `"foo.h"` -> `foo.h`). */
static char *strip_quotes(const char *text, uint32_t len) {
    if (len >= 2 && text[0] == '"' && text[len - 1] == '"') {
        char *out = (char *)malloc(len - 1);
        memcpy(out, text + 1, len - 2);
        out[len - 2] = '\0';
        return out;
    }
    char *out = (char *)malloc(len + 1);
    memcpy(out, text, len);
    out[len] = '\0';
    return out;
}

static void c_extract_declarations(const ParsedFile *file, TSQuery *decl_query,
                                    DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    DeclFact fact;
    fact.kind = DECL_TYPE;
    fact.qualified_name = file->path;
    sink(ctx, &fact);
}

static void c_extract_references(const ParsedFile *file, TSQuery *ref_query,
                                  RefSinkFn sink, void *ctx) {
    if (!ref_query) return;
    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, ref_query, ts_tree_root_node(file->tree));
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        for (uint32_t i = 0; i < match.capture_count; i++) {
            TSNode node = match.captures[i].node;
            uint32_t start = ts_node_start_byte(node);
            uint32_t end = ts_node_end_byte(node);
            char *raw = strip_quotes(file->source + start, end - start);

            RefFact fact;
            fact.kind = REF_IMPORT_PATH;
            fact.raw_text = raw;
            fact.scope_namespace = NULL;
            sink(ctx, &fact);

            free(raw);
        }
    }
    ts_query_cursor_delete(cursor);
}

static char *c_resolve_reference(const char *raw_text,
                                  const char *referencing_file_path,
                                  const char *scope_namespace,
                                  const char **imported_namespaces,
                                  int imported_count,
                                  void *symbol_table_handle) {
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    (void)symbol_table_handle;

    char *dir = path_dirname(referencing_file_path);
    char *joined = path_join(dir, raw_text);
    char *normalized = path_normalize(joined);
    free(dir);
    free(joined);
    return normalized; /* NULL if the included file doesn't exist on disk */
}

static LanguageAdapter g_c_adapter;
static TSQuery *g_ref_query = NULL;

const LanguageAdapter *c_adapter_get(void) {
    if (g_ref_query == NULL) {
        uint32_t error_offset = 0;
        TSQueryError error_type = TSQueryErrorNone;
        g_ref_query = ts_query_new(tree_sitter_c(), REF_QUERY_SRC,
                                    (uint32_t)strlen(REF_QUERY_SRC),
                                    &error_offset, &error_type);
        if (!g_ref_query) {
            fprintf(stderr,
                    "c_adapter: failed to compile query at offset %u (error %d)\n",
                    error_offset, (int)error_type);
        }

        g_c_adapter.name = "c";
        g_c_adapter.extensions = C_EXTENSIONS;
        g_c_adapter.extension_count = 2;
        g_c_adapter.ts_language = tree_sitter_c;
        g_c_adapter.decl_query = NULL;
        g_c_adapter.ref_query = g_ref_query;
        g_c_adapter.extract_declarations = c_extract_declarations;
        g_c_adapter.extract_references = c_extract_references;
        g_c_adapter.resolve_reference = c_resolve_reference;
    }
    return &g_c_adapter;
}
