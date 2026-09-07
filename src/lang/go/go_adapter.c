/* Go dependency adapter.
 *
 * Go's dependency signal is `import "import/path"`, a string naming a
 * *package* (a directory), where the path is `<module path>/<subdir>` --
 * and the module path lives in a go.mod file this adapter never sees. So,
 * like the Python adapter, package identity here is path-derived and
 * matched by suffix rather than resolved for real:
 *
 *   - Declarations: a .go file at .../a/b/pkg/f.go emits every trailing
 *     slash-joined suffix of its *directory* -- `pkg`, `b/pkg`, `a/b/pkg`,
 *     ... (capped at DIR_SUFFIX_MAX segments). Every file in the same
 *     directory emits the same set, so an import of that package resolves
 *     to whichever of its files registered last in the symbol table --
 *     one edge to one file, not one per file in the package. A known,
 *     accepted simplification (the graph is file-grained; Go deps are
 *     package-grained).
 *
 *   - References: each `import` spec's path string (single or grouped),
 *     quotes/backticks stripped. `import _ "x"` and `import . "x"` are
 *     included -- the blank/dot import name doesn't change the dependency.
 *
 *   - Resolution: try the whole import path against the symbol table,
 *     then drop leading path segments one at a time (`ex.com/m/a/b` ->
 *     `m/a/b` -> `a/b` -> `b`) until one matches a declared directory
 *     suffix. Longest match wins. Standard-library and external-module
 *     imports (`fmt`, `github.com/...`) simply don't match anything local
 *     and are left unresolved, same as Python's `import os`.
 *
 * Node shapes confirmed against tree-sitter-go v0.23.4's
 * src/node-types.json:
 *   - import_declaration -> import_spec | import_spec_list
 *   - import_spec: field "path" -> interpreted_string_literal | raw_string_literal
 *   - import_spec_list: import_spec children
 * A flat query handles this fine -- imports are always top-level, no
 * scope tracking needed (unlike C#).
 */

#include "go_adapter.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_go(void);

static const char *GO_EXTENSIONS[] = { ".go", NULL };

/* Trailing directory segments a package suffix may span -- generous for
 * real nesting, bounded so a deep checkout path doesn't spray decls. */
#define DIR_SUFFIX_MAX 8

static const char *REF_QUERY_SRC =
    "(import_spec path: (interpreted_string_literal) @path)\n"
    "(import_spec path: (raw_string_literal) @path)\n";

/* `"a/b"` or `` `a/b` `` -> a/b. Caller frees. */
static char *strip_string_literal(const char *text, uint32_t len) {
    if (len >= 2 && (text[0] == '"' || text[0] == '`') &&
        (text[len - 1] == '"' || text[len - 1] == '`')) {
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

/* Split path on '/' or '\\' into [start,len) spans, empty segments
 * dropped. Fills seg_start/seg_len (caller-sized to cap), returns count. */
static int path_segments(const char *path, const char **seg_start, size_t *seg_len, int cap) {
    int count = 0;
    const char *p = path;
    while (*p && count < cap) {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/' && *p != '\\') p++;
        seg_start[count] = s;
        seg_len[count] = (size_t)(p - s);
        count++;
    }
    return count;
}

static void go_extract_declarations(const ParsedFile *file, TSQuery *decl_query,
                                     DeclSinkFn sink, void *ctx) {
    (void)decl_query;

    const char *seg_start[64];
    size_t seg_len[64];
    int n = path_segments(file->path, seg_start, seg_len, 64);
    if (n < 2) return; /* need at least one directory segment + the filename */
    n--;               /* drop the filename -- a Go import names the directory */

    /* Cumulative trailing suffixes, innermost first: "pkg", "b/pkg", ... */
    char acc[1024];
    size_t acc_len = 0;
    acc[0] = '\0';
    int lo = n - DIR_SUFFIX_MAX;
    if (lo < 0) lo = 0;
    for (int i = n - 1; i >= lo; i--) {
        size_t sl = seg_len[i];
        size_t need = sl + (acc_len ? 1 : 0);
        if (acc_len + need + 1 > sizeof(acc)) break;
        memmove(acc + sl + (acc_len ? 1 : 0), acc, acc_len + 1);
        memcpy(acc, seg_start[i], sl);
        if (acc_len) acc[sl] = '/';
        acc_len += need;

        DeclFact fact;
        fact.kind = DECL_PACKAGE;
        fact.qualified_name = acc;
        sink(ctx, &fact);
    }
}

static void go_extract_references(const ParsedFile *file, TSQuery *ref_query,
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
            char *raw = strip_string_literal(file->source + start, end - start);

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

/* Try the full import path, then progressively drop leading segments
 * until one matches a declared directory suffix. Longest match wins. */
static char *go_resolve_reference(const char *raw_text,
                                   const char *referencing_file_path,
                                   const char *scope_namespace,
                                   const char **imported_namespaces,
                                   int imported_count,
                                   void *symbol_table_handle) {
    (void)referencing_file_path;
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text) return NULL;

    for (const char *p = raw_text; p && *p;) {
        if (symtab_get(table, p) >= 0) return xstrdup(p);
        const char *slash = strchr(p, '/');
        if (!slash) break;
        p = slash + 1;
    }
    return NULL;
}

static LanguageAdapter g_go_adapter;
static TSQuery *g_ref_query = NULL;

const LanguageAdapter *go_adapter_get(void) {
    if (g_ref_query == NULL) {
        uint32_t error_offset = 0;
        TSQueryError error_type = TSQueryErrorNone;
        g_ref_query = ts_query_new(tree_sitter_go(), REF_QUERY_SRC,
                                    (uint32_t)strlen(REF_QUERY_SRC),
                                    &error_offset, &error_type);
        if (!g_ref_query) {
            fprintf(stderr,
                    "go_adapter: failed to compile query at offset %u (error %d)\n",
                    error_offset, (int)error_type);
        }

        g_go_adapter.name = "go";
        g_go_adapter.extensions = GO_EXTENSIONS;
        g_go_adapter.extension_count = 1;
        g_go_adapter.ts_language = tree_sitter_go;
        g_go_adapter.decl_query = NULL;
        g_go_adapter.ref_query = g_ref_query;
        g_go_adapter.extract_declarations = go_extract_declarations;
        g_go_adapter.extract_references = go_extract_references;
        g_go_adapter.resolve_reference = go_resolve_reference;
    }
    return &g_go_adapter;
}
