/* HTML dependency adapter.
 *
 * A page depends on whatever its src / href attributes point at:
 * <script src>, <link href> (stylesheets, preloads), <a href>, <iframe
 * src>, <source src> and so on -- every element, since the attribute name
 * is what matters. Links to other sites, "#anchors", "mailto:" and the
 * like are skipped, and so are images, fonts and media, which no adapter
 * claims (see lang/weblink.h for the cleaning rules).
 *
 *   - Declarations: each file declares its own absolute path.
 *   - Resolution: relative to the page; a leading "/" is site-root
 *     relative and tried against each ancestor folder (weblink_resolve).
 *     A link to a folder ("docs/") finds its index.html, and an
 *     extensionless "/about" finds about.html.
 *
 * Inline <script> and <style> bodies aren't parsed, so an `import` or
 * `@import` written inside the page itself doesn't produce an edge.
 *
 * Node shapes (tree-sitter-html v0.23.2, confirmed by parsing samples):
 * attribute has an attribute_name child and either a bare attribute_value
 * or a quoted_attribute_value wrapping one (absent for "").
 */

#include "html_adapter.h"
#include "../weblink.h"
#include <tree_sitter/api.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_html(void);

static const char *HTML_EXTENSIONS[] = { ".html", ".htm", NULL };

static void html_extract_declarations(const ParsedFile *file, TSQuery *decl_query, DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    DeclFact fact;
    fact.kind = DECL_PACKAGE;
    fact.qualified_name = file->path;
    sink(ctx, &fact);
}

static bool name_is(const ParsedFile *file, TSNode node, const char *want) {
    uint32_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
    size_t n = strlen(want);
    if (e - s != n) return false;
    for (size_t i = 0; i < n; i++) {
        char c = file->source[s + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != want[i]) return false;
    }
    return true;
}

static void emit_attribute(TSNode attr, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    TSNode name = ts_node_named_child(attr, 0);
    if (ts_node_is_null(name) || strcmp(ts_node_type(name), "attribute_name") != 0) return;
    if (!name_is(file, name, "src") && !name_is(file, name, "href")) return;

    TSNode value = ts_node_named_child(attr, 1);
    if (!ts_node_is_null(value) && strcmp(ts_node_type(value), "quoted_attribute_value") == 0) {
        value = ts_node_named_child(value, 0);
    }
    if (ts_node_is_null(value) || strcmp(ts_node_type(value), "attribute_value") != 0) return;

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

static void collect_refs(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    if (strcmp(ts_node_type(node), "attribute") == 0) {
        emit_attribute(node, file, sink, ctx);
        return;
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, sink, ctx);
}

static void html_extract_references(const ParsedFile *file, TSQuery *ref_query, RefSinkFn sink, void *ctx) {
    (void)ref_query;
    collect_refs(ts_tree_root_node(file->tree), file, sink, ctx);
}

static char *html_resolve_reference(const char *raw_text, const char *referencing_file_path,
                                    const char *scope_namespace, const char **imported_namespaces,
                                    int imported_count, void *symbol_table_handle) {
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    static const char *const SUFFIXES[] = { "/index.html", "/index.htm", ".html", NULL };
    return weblink_resolve(raw_text, referencing_file_path, SUFFIXES, symbol_table_handle);
}

static LanguageAdapter g_html_adapter;
static int g_initialized = 0;

const LanguageAdapter *html_adapter_get(void) {
    if (!g_initialized) {
        g_html_adapter.name = "html";
        g_html_adapter.extensions = HTML_EXTENSIONS;
        g_html_adapter.extension_count = 2;
        g_html_adapter.ts_language = tree_sitter_html;
        g_html_adapter.decl_query = NULL;
        g_html_adapter.ref_query = NULL;
        g_html_adapter.extract_declarations = html_extract_declarations;
        g_html_adapter.extract_references = html_extract_references;
        g_html_adapter.resolve_reference = html_resolve_reference;
        g_initialized = 1;
    }
    return &g_html_adapter;
}
