/* SQL dependency adapter.
 *
 * SQL files depend on each other through the objects they name: a view
 * selecting from a table, a migration altering a table another one
 * created, a foreign key, a function call. So this works on object names
 * rather than paths, like the C# adapter works on type names:
 *
 *   - Declarations: the object a CREATE statement makes -- TABLE, VIEW,
 *     MATERIALIZED VIEW, FUNCTION, PROCEDURE, TYPE, SEQUENCE -- declared
 *     as "sql:<name>" and, when schema-qualified, "sql:<schema>.<name>"
 *     too. Several files creating the same name (migrations recreating a
 *     table) collide; last one wins.
 *   - References: every other object name -- FROM / JOIN, INSERT INTO,
 *     UPDATE, DELETE FROM, ALTER / DROP, REFERENCES (foreign keys),
 *     CREATE INDEX ... ON, function invocations, casts to user types.
 *     Column qualifiers ("i" in i.id, i.*) are aliases, not objects, and
 *     are skipped; so are SET / variable names. A name nothing declares
 *     (a CTE, a built-in, a table from outside the project) just stays
 *     unresolved.
 *   - Resolution: "sql:<schema>.<name>" first, then "sql:<name>".
 *
 * Names are unquoted ("x", `x`, [x]) and lowercased: SQL folds unquoted
 * identifiers, and matching quoted ones case-insensitively too is the
 * right trade-off for a dependency map.
 *
 * Node shapes (DerekStride/tree-sitter-sql, gh-pages 39fdb00, confirmed by
 * parsing samples and node-types.json): object names are object_reference
 * nodes with fields database / schema / name; the object a create_* makes
 * is its first direct object_reference child.
 */

#include "sql_adapter.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_sql(void);

static const char *SQL_EXTENSIONS[] = { ".sql", NULL };

static const char *DECLARING[] = {
    "create_table", "create_view", "create_materialized_view", "create_function",
    "create_procedure", "create_type", "create_sequence", NULL,
};

/* Parents whose object_reference isn't a database object. */
static const char *NOT_OBJECTS[] = {
    "field", "all_fields", "set_statement", "reset_statement", "var_declaration", NULL,
};

static bool in_list(const char *type, const char **list) {
    for (int i = 0; list[i]; i++) {
        if (strcmp(type, list[i]) == 0) return true;
    }
    return false;
}

/* An identifier's text, unquoted and lowercased. Caller frees. */
static char *ident(const ParsedFile *file, TSNode node) {
    uint32_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
    char *out = (char *)malloc(e - s + 1);
    size_t o = 0;
    for (uint32_t i = s; i < e; i++) {
        char c = file->source[i];
        if (c == '"' || c == '`' || c == '[' || c == ']') continue;
        out[o++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[o] = '\0';
    return out;
}

/* "schema.name" or "name" for an object_reference. Caller frees; NULL if
 * it has no name. */
static char *qualified(const ParsedFile *file, TSNode ref) {
    TSNode name = ts_node_child_by_field_name(ref, "name", 4);
    if (ts_node_is_null(name)) return NULL;
    char *n = ident(file, name);
    TSNode schema = ts_node_child_by_field_name(ref, "schema", 6);
    if (ts_node_is_null(schema)) return n;
    char *sc = ident(file, schema);
    size_t sl = strlen(sc), nl = strlen(n);
    char *out = (char *)malloc(sl + nl + 2);
    memcpy(out, sc, sl);
    out[sl] = '.';
    memcpy(out + sl + 1, n, nl + 1);
    free(sc);
    free(n);
    return out;
}

static void declare(DeclSinkFn sink, void *ctx, const char *name) {
    size_t n = strlen(name);
    char *key = (char *)malloc(n + 5);
    memcpy(key, "sql:", 4);
    memcpy(key + 4, name, n + 1);
    DeclFact fact;
    fact.kind = DECL_TYPE;
    fact.qualified_name = key;
    sink(ctx, &fact);
    free(key);
}

static void collect_decls(TSNode node, const ParsedFile *file, DeclSinkFn sink, void *ctx) {
    if (in_list(ts_node_type(node), DECLARING)) {
        uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++) {
            TSNode c = ts_node_named_child(node, i);
            if (strcmp(ts_node_type(c), "object_reference") != 0) continue;
            char *q = qualified(file, c);
            if (q) {
                declare(sink, ctx, q);
                const char *dot = strrchr(q, '.');
                if (dot) declare(sink, ctx, dot + 1);
                free(q);
            }
            break; /* only the object being created */
        }
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_decls(ts_node_named_child(node, i), file, sink, ctx);
}

static void sql_extract_declarations(const ParsedFile *file, TSQuery *decl_query, DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    collect_decls(ts_tree_root_node(file->tree), file, sink, ctx);
}

static void collect_refs(TSNode node, TSNode parent, bool first_under_declaring, const ParsedFile *file,
                         RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);
    if (strcmp(type, "object_reference") == 0) {
        const char *pt = ts_node_is_null(parent) ? "" : ts_node_type(parent);
        if (!first_under_declaring && !in_list(pt, NOT_OBJECTS) && strcmp(pt, "object_reference") != 0) {
            char *q = qualified(file, node);
            if (q) {
                RefFact fact;
                fact.kind = REF_TYPE_NAME;
                fact.raw_text = q;
                fact.scope_namespace = NULL;
                sink(ctx, &fact);
                free(q);
            }
        }
        return;
    }
    bool declaring = in_list(type, DECLARING);
    bool seen_first = false;
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_named_child(node, i);
        bool first = false;
        if (declaring && !seen_first && strcmp(ts_node_type(c), "object_reference") == 0) {
            first = true; /* the name being created, not a use */
            seen_first = true;
        }
        collect_refs(c, node, first, file, sink, ctx);
    }
}

static void sql_extract_references(const ParsedFile *file, TSQuery *ref_query, RefSinkFn sink, void *ctx) {
    (void)ref_query;
    TSNode none = { 0 };
    collect_refs(ts_tree_root_node(file->tree), none, false, file, sink, ctx);
}

static char *key_for(const char *name) {
    size_t n = strlen(name);
    char *key = (char *)malloc(n + 5);
    memcpy(key, "sql:", 4);
    memcpy(key + 4, name, n + 1);
    return key;
}

static char *sql_resolve_reference(const char *raw_text, const char *referencing_file_path,
                                   const char *scope_namespace, const char **imported_namespaces,
                                   int imported_count, void *symbol_table_handle) {
    (void)referencing_file_path;
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text || !raw_text[0]) return NULL;
    char *key = key_for(raw_text);
    if (symtab_get(table, key) >= 0) return key;
    free(key);
    const char *dot = strrchr(raw_text, '.');
    if (dot && dot[1]) {
        key = key_for(dot + 1);
        if (symtab_get(table, key) >= 0) return key;
        free(key);
    }
    return NULL;
}

static LanguageAdapter g_sql_adapter;
static int g_initialized = 0;

const LanguageAdapter *sql_adapter_get(void) {
    if (!g_initialized) {
        g_sql_adapter.name = "sql";
        g_sql_adapter.extensions = SQL_EXTENSIONS;
        g_sql_adapter.extension_count = 1;
        g_sql_adapter.ts_language = tree_sitter_sql;
        g_sql_adapter.decl_query = NULL;
        g_sql_adapter.ref_query = NULL;
        g_sql_adapter.extract_declarations = sql_extract_declarations;
        g_sql_adapter.extract_references = sql_extract_references;
        g_sql_adapter.resolve_reference = sql_resolve_reference;
        g_initialized = 1;
    }
    return &g_sql_adapter;
}
