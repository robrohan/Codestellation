/* Protocol Buffers dependency adapter.
 *
 * A .proto file's dependencies are its imports:
 *
 *   import "acme/common/money.proto";
 *   import public "acme/common/ids.proto";
 *   import weak "legacy/old.proto";
 *
 * Proto uses its imports for every cross-file type, so they're the whole
 * dependency picture -- no need to chase field types.
 *
 * Import paths are relative to an include root (protoc's -I / --proto_path),
 * which this adapter never sees. So, like the Python and Go adapters, it's
 * a path-suffix match:
 *
 *   - Declarations: for .../src/acme/common/money.proto, every trailing
 *     slice of the path -- "money.proto", "common/money.proto",
 *     "acme/common/money.proto", ... (capped at SUFFIX_MAX segments) --
 *     prefixed "proto:" so they can't collide with other adapters' keys.
 *     Two files ending the same way collide; last one wins.
 *   - Resolution: the import path, looked up as-is. Imports from outside
 *     the project (google/protobuf/timestamp.proto and the rest of the
 *     well-known types, googleapis) stay unresolved.
 *
 * Node shapes (coder3101/tree-sitter-proto 0.6.0, confirmed by parsing
 * samples): import has field path -> string, whose text includes the
 * quotes.
 */

#include "proto_adapter.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_proto(void);

static const char *PROTO_EXTENSIONS[] = { ".proto", NULL };

#define SUFFIX_MAX 8
#define PREFIX "proto:"

static void emit_decl(DeclSinkFn sink, void *ctx, const char *suffix) {
    size_t n = strlen(suffix);
    char *key = (char *)malloc(sizeof(PREFIX) + n);
    memcpy(key, PREFIX, sizeof(PREFIX) - 1);
    memcpy(key + sizeof(PREFIX) - 1, suffix, n + 1);
    DeclFact fact;
    fact.kind = DECL_PACKAGE;
    fact.qualified_name = key;
    sink(ctx, &fact);
    free(key);
}

static void proto_extract_declarations(const ParsedFile *file, TSQuery *decl_query, DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    /* Walk back from the end of the path, emitting each trailing slice
     * ("money.proto", "common/money.proto", ...), normalized to '/'. */
    size_t len = strlen(file->path);
    char *norm = xstrdup(file->path);
    for (size_t i = 0; i < len; i++) {
        if (norm[i] == '\\') norm[i] = '/';
    }
    int segments = 0;
    for (size_t i = len; i > 0 && segments < SUFFIX_MAX; i--) {
        if (norm[i - 1] == '/') {
            if (i < len) emit_decl(sink, ctx, norm + i);
            segments++;
        }
    }
    free(norm);
}

static void collect_refs(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    if (strcmp(ts_node_type(node), "import") == 0) {
        TSNode path = ts_node_child_by_field_name(node, "path", 4);
        if (!ts_node_is_null(path)) {
            uint32_t s = ts_node_start_byte(path), e = ts_node_end_byte(path);
            /* Drop the surrounding quotes. */
            if (e - s >= 2) {
                s++;
                e--;
            }
            char *raw = (char *)malloc(e - s + 1);
            memcpy(raw, file->source + s, e - s);
            raw[e - s] = '\0';
            if (raw[0]) {
                RefFact fact;
                fact.kind = REF_IMPORT_PATH;
                fact.raw_text = raw;
                fact.scope_namespace = NULL;
                sink(ctx, &fact);
            }
            free(raw);
        }
        return;
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, sink, ctx);
}

static void proto_extract_references(const ParsedFile *file, TSQuery *ref_query, RefSinkFn sink, void *ctx) {
    (void)ref_query;
    collect_refs(ts_tree_root_node(file->tree), file, sink, ctx);
}

static char *proto_resolve_reference(const char *raw_text, const char *referencing_file_path,
                                     const char *scope_namespace, const char **imported_namespaces,
                                     int imported_count, void *symbol_table_handle) {
    (void)referencing_file_path;
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text || !raw_text[0]) return NULL;
    size_t n = strlen(raw_text);
    char *key = (char *)malloc(sizeof(PREFIX) + n);
    memcpy(key, PREFIX, sizeof(PREFIX) - 1);
    memcpy(key + sizeof(PREFIX) - 1, raw_text, n + 1);
    for (char *p = key; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    if (symtab_get(table, key) >= 0) return key;
    free(key);
    return NULL;
}

static LanguageAdapter g_proto_adapter;
static int g_initialized = 0;

const LanguageAdapter *proto_adapter_get(void) {
    if (!g_initialized) {
        g_proto_adapter.name = "proto";
        g_proto_adapter.extensions = PROTO_EXTENSIONS;
        g_proto_adapter.extension_count = 1;
        g_proto_adapter.ts_language = tree_sitter_proto;
        g_proto_adapter.decl_query = NULL;
        g_proto_adapter.ref_query = NULL;
        g_proto_adapter.extract_declarations = proto_extract_declarations;
        g_proto_adapter.extract_references = proto_extract_references;
        g_proto_adapter.resolve_reference = proto_resolve_reference;
        g_initialized = 1;
    }
    return &g_proto_adapter;
}
