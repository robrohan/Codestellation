/* CSS dependency adapter.
 *
 * A stylesheet depends on what it @imports -- `@import "base.css";`,
 * `@import url(theme.css) screen;` -- and on any url(...) that names
 * another stylesheet. Most url()s are images and fonts, which
 * weblink_clean drops (see lang/weblink.h), so they don't count as
 * unresolved.
 *
 *   - Declarations: each file declares its own absolute path.
 *   - Resolution: relative to the stylesheet; a leading "/" is site-root
 *     relative and tried against each ancestor folder (weblink_resolve).
 *
 * Node shapes (tree-sitter-css v0.25.0, confirmed by parsing samples):
 * import_statement holds a string_value (whose text is its string_content
 * child) or a call_expression; a call_expression has a function_name and
 * arguments holding a string_value or a bare plain_value.
 */

#include "css_adapter.h"
#include "../weblink.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_css(void);

static const char *CSS_EXTENSIONS[] = { ".css", NULL };

static void css_extract_declarations(const ParsedFile *file, TSQuery *decl_query, DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    DeclFact fact;
    fact.kind = DECL_PACKAGE;
    fact.qualified_name = file->path;
    sink(ctx, &fact);
}

static void emit(const ParsedFile *file, TSNode value, RefSinkFn sink, void *ctx) {
    /* A string_value's text is its string_content child (absent for ""). */
    if (strcmp(ts_node_type(value), "string_value") == 0) {
        value = ts_node_named_child(value, 0);
        if (ts_node_is_null(value) || strcmp(ts_node_type(value), "string_content") != 0) return;
    } else if (strcmp(ts_node_type(value), "plain_value") != 0) {
        return;
    }
    uint32_t s = ts_node_start_byte(value), e = ts_node_end_byte(value);
    char *link = weblink_clean(file->source + s, e - s);
    if (!link) return;
    RefFact fact;
    fact.kind = REF_IMPORT_PATH;
    fact.raw_text = link;
    fact.scope_namespace = NULL;
    sink(ctx, &fact);
    free(link);
}

static void emit_url_call(const ParsedFile *file, TSNode call, RefSinkFn sink, void *ctx) {
    TSNode fn = ts_node_named_child(call, 0);
    if (ts_node_is_null(fn) || strcmp(ts_node_type(fn), "function_name") != 0) return;
    uint32_t s = ts_node_start_byte(fn), e = ts_node_end_byte(fn);
    if (e - s != 3 || strncmp(file->source + s, "url", 3) != 0) return;
    TSNode args = ts_node_named_child(call, 1);
    if (ts_node_is_null(args) || strcmp(ts_node_type(args), "arguments") != 0) return;
    TSNode arg = ts_node_named_child(args, 0);
    if (!ts_node_is_null(arg)) emit(file, arg, sink, ctx);
}

static void collect_refs(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);
    if (strcmp(type, "import_statement") == 0) {
        TSNode target = ts_node_named_child(node, 0);
        if (ts_node_is_null(target)) return;
        if (strcmp(ts_node_type(target), "call_expression") == 0) emit_url_call(file, target, sink, ctx);
        else emit(file, target, sink, ctx);
        return;
    }
    if (strcmp(type, "call_expression") == 0) {
        emit_url_call(file, node, sink, ctx);
        return;
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, sink, ctx);
}

static void css_extract_references(const ParsedFile *file, TSQuery *ref_query, RefSinkFn sink, void *ctx) {
    (void)ref_query;
    collect_refs(ts_tree_root_node(file->tree), file, sink, ctx);
}

static char *css_resolve_reference(const char *raw_text, const char *referencing_file_path,
                                   const char *scope_namespace, const char **imported_namespaces,
                                   int imported_count, void *symbol_table_handle) {
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    static const char *const SUFFIXES[] = { ".css", NULL };
    return weblink_resolve(raw_text, referencing_file_path, SUFFIXES, symbol_table_handle);
}

static LanguageAdapter g_css_adapter;
static int g_initialized = 0;

const LanguageAdapter *css_adapter_get(void) {
    if (!g_initialized) {
        g_css_adapter.name = "css";
        g_css_adapter.extensions = CSS_EXTENSIONS;
        g_css_adapter.extension_count = 1;
        g_css_adapter.ts_language = tree_sitter_css;
        g_css_adapter.decl_query = NULL;
        g_css_adapter.ref_query = NULL;
        g_css_adapter.extract_declarations = css_extract_declarations;
        g_css_adapter.extract_references = css_extract_references;
        g_css_adapter.resolve_reference = css_resolve_reference;
        g_initialized = 1;
    }
    return &g_css_adapter;
}
