/* Markdown dependency adapter.
 *
 * A note depends on the local files it links to:
 *
 *   - [text](docs/guide.md#setup) and ![alt](diagram.html) inline links;
 *   - [ref]: ./other.md reference definitions (whether or not anything
 *     uses the label -- a definition is a link either way);
 *   - Obsidian-style [[Wiki Note]] / [[folder/Note|alias]] / [[Note#Heading]]
 *     and ![[embeds]], which name a note rather than a path.
 *
 * Links to other sites, "#anchors" and images/fonts/media are skipped (see
 * lang/weblink.h). Links inside code spans and fenced code don't count.
 *
 *   - Declarations: each file declares its own absolute path, plus
 *     "mdnote:<file name without .md, lowercased>" for wiki links.
 *   - Resolution: paths relative to the note, tried as written and with
 *     ".md" appended; a leading "/" is tried against each ancestor folder
 *     (weblink_resolve). A wiki link is tried as a path from the note's
 *     folder and then each ancestor folder (".md" appended), so the
 *     nearest match wins, as in Obsidian; failing that, by note name
 *     anywhere, where the first note parsed wins a tie.
 *
 * tree-sitter-markdown is two grammars (v0.5.3): the block grammar that
 * the pipeline parses with, whose `inline` and `pipe_table_cell` nodes are
 * left unparsed, and an inline grammar for their contents, run here over
 * exactly those byte ranges (ts_parser_set_included_ranges) -- the same
 * split editors use. Node shapes (confirmed by parsing samples): block
 * link_reference_definition has a link_destination; inline inline_link and
 * image have a link_destination. The inline grammar has no wiki-link
 * node: [[x]] parses as "[" + shortcut_link "[x]" + "]", and ![[x]] as an
 * image whose image_description holds that shortcut_link, so the outer
 * brackets are checked in the source.
 */

#include "markdown_adapter.h"
#include "../weblink.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_markdown(void);
extern const TSLanguage *tree_sitter_markdown_inline(void);

static const char *MD_EXTENSIONS[] = { ".md", ".markdown", NULL };

#define WIKI_PREFIX "wiki:"
#define NOTE_PREFIX "mdnote:"

/* NOTE_PREFIX + name lowercased, minus a trailing ".md"/".markdown". */
static char *note_key(const char *name, size_t n) {
    static const char *const EXTS[] = { ".markdown", ".md", NULL };
    for (int i = 0; EXTS[i]; i++) {
        size_t m = strlen(EXTS[i]);
        if (n > m) {
            bool match = true;
            for (size_t k = 0; k < m && match; k++) {
                char c = name[n - m + k];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                match = c == EXTS[i][k];
            }
            if (match) { n -= m; break; }
        }
    }
    size_t pl = strlen(NOTE_PREFIX);
    char *out = (char *)malloc(pl + n + 1);
    memcpy(out, NOTE_PREFIX, pl);
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        out[pl + i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[pl + n] = '\0';
    return out;
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (slash && (!bslash || slash > bslash)) return slash + 1;
    return bslash ? bslash + 1 : path;
}

static void md_extract_declarations(const ParsedFile *file, TSQuery *decl_query, DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    DeclFact fact;
    fact.kind = DECL_PACKAGE;
    fact.qualified_name = file->path;
    sink(ctx, &fact);

    const char *base = base_name(file->path);
    char *key = note_key(base, strlen(base));
    fact.qualified_name = key;
    sink(ctx, &fact);
    free(key);
}

static void emit_raw(const char *raw, RefSinkFn sink, void *ctx) {
    RefFact fact;
    fact.kind = REF_IMPORT_PATH;
    fact.raw_text = raw;
    fact.scope_namespace = NULL;
    sink(ctx, &fact);
}

static void emit_destination(const ParsedFile *file, TSNode dest, RefSinkFn sink, void *ctx) {
    uint32_t s = ts_node_start_byte(dest), e = ts_node_end_byte(dest);
    char *link = weblink_clean(file->source + s, e - s);
    if (!link) return;
    emit_raw(link, sink, ctx);
    free(link);
}

static TSNode child_of_type(TSNode node, const char *type) {
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_named_child(node, i);
        if (strcmp(ts_node_type(c), type) == 0) return c;
    }
    TSNode none;
    memset(&none, 0, sizeof(none));
    return none;
}

/* [[target|alias]] / [[target#heading]] -- emitted as WIKI_PREFIX + target.
 * The shortcut_link is the inner "[target]"; the outer brackets are its
 * neighbours in the source. */
static void emit_wiki(const ParsedFile *file, TSNode link, RefSinkFn sink, void *ctx) {
    uint32_t s = ts_node_start_byte(link), e = ts_node_end_byte(link);
    const char *src = file->source;
    if (s == 0 || e >= file->source_len || src[s - 1] != '[' || src[e] != ']') return;
    if (e - s < 3 || src[s] != '[' || src[e - 1] != ']') return;

    const char *t = src + s + 1;
    size_t n = (e - 1) - (s + 1);
    for (size_t i = 0; i < n; i++) {
        if (t[i] == '|' || t[i] == '#' || t[i] == '^' || t[i] == '\n') { n = i; break; }
    }
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\t')) n--;
    while (n && (*t == ' ' || *t == '\t')) { t++; n--; }
    if (!n) return;

    size_t pl = strlen(WIKI_PREFIX);
    char *raw = (char *)malloc(pl + n + 1);
    memcpy(raw, WIKI_PREFIX, pl);
    memcpy(raw + pl, t, n);
    raw[pl + n] = '\0';
    emit_raw(raw, sink, ctx);
    free(raw);
}

static void collect_inline(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);
    if (strcmp(type, "inline_link") == 0 || strcmp(type, "image") == 0) {
        TSNode dest = child_of_type(node, "link_destination");
        if (!ts_node_is_null(dest)) {
            emit_destination(file, dest, sink, ctx);
            return;
        }
        /* No destination: ![[embed]], whose description holds the
         * shortcut_link -- fall through and look inside. */
    }
    if (strcmp(type, "shortcut_link") == 0) {
        emit_wiki(file, node, sink, ctx);
        return;
    }
    if (strcmp(type, "code_span") == 0) return;
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_inline(ts_node_named_child(node, i), file, sink, ctx);
}

typedef struct {
    TSRange *items;
    uint32_t count, cap;
} RangeList;

/* Block-level pass: reference definitions emit directly; inline content
 * is gathered (in document order, as included ranges must be) for the
 * inline grammar. */
static void collect_block(TSNode node, const ParsedFile *file, RangeList *ranges, RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);
    if (strcmp(type, "link_reference_definition") == 0) {
        TSNode dest = child_of_type(node, "link_destination");
        if (!ts_node_is_null(dest)) emit_destination(file, dest, sink, ctx);
        return;
    }
    if (strcmp(type, "inline") == 0 || strcmp(type, "pipe_table_cell") == 0) {
        if (ranges->count == ranges->cap) {
            ranges->cap = ranges->cap ? ranges->cap * 2 : 64;
            ranges->items = (TSRange *)realloc(ranges->items, ranges->cap * sizeof(TSRange));
        }
        TSRange *r = &ranges->items[ranges->count++];
        r->start_point = ts_node_start_point(node);
        r->end_point = ts_node_end_point(node);
        r->start_byte = ts_node_start_byte(node);
        r->end_byte = ts_node_end_byte(node);
        return;
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_block(ts_node_named_child(node, i), file, ranges, sink, ctx);
}

static void md_extract_references(const ParsedFile *file, TSQuery *ref_query, RefSinkFn sink, void *ctx) {
    (void)ref_query;
    RangeList ranges;
    memset(&ranges, 0, sizeof(ranges));
    collect_block(ts_tree_root_node(file->tree), file, &ranges, sink, ctx);
    if (ranges.count == 0) return;

    /* One parse per range, not all ranges combined: combined, a stray
     * backtick in one paragraph pairs with one in a later paragraph and
     * code spans come out inside-out. */
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_markdown_inline());
    for (uint32_t i = 0; i < ranges.count; i++) {
        if (!ts_parser_set_included_ranges(parser, &ranges.items[i], 1)) continue;
        TSTree *tree = ts_parser_parse_string(parser, NULL, file->source, file->source_len);
        if (!tree) continue;
        collect_inline(ts_tree_root_node(tree), file, sink, ctx);
        ts_tree_delete(tree);
    }
    ts_parser_delete(parser);
    free(ranges.items);
}

static char *md_resolve_reference(const char *raw_text, const char *referencing_file_path,
                                  const char *scope_namespace, const char **imported_namespaces,
                                  int imported_count, void *symbol_table_handle) {
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    static const char *const SUFFIXES[] = { ".md", ".markdown", NULL };
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text) return NULL;

    size_t wl = strlen(WIKI_PREFIX);
    if (strncmp(raw_text, WIKI_PREFIX, wl) != 0) {
        return weblink_resolve(raw_text, referencing_file_path, SUFFIXES, table);
    }

    /* As a path from the note's own folder, then from each ancestor
     * (weblink_resolve's site-root walk), so the nearest note wins. */
    const char *target = raw_text + wl;
    size_t tn = strlen(target);
    char *rooted = (char *)malloc(tn + 2);
    rooted[0] = '/';
    memcpy(rooted + 1, target, tn + 1);
    char *hit = weblink_resolve(rooted, referencing_file_path, SUFFIXES, table);
    free(rooted);
    if (hit) return hit;
    const char *base = base_name(target);
    char *key = note_key(base, strlen(base));
    if (symtab_get(table, key) >= 0) return key;
    free(key);
    return NULL;
}

static LanguageAdapter g_md_adapter;
static int g_initialized = 0;

const LanguageAdapter *markdown_adapter_get(void) {
    if (!g_initialized) {
        g_md_adapter.name = "markdown";
        g_md_adapter.extensions = MD_EXTENSIONS;
        g_md_adapter.extension_count = 2;
        g_md_adapter.ts_language = tree_sitter_markdown;
        g_md_adapter.decl_query = NULL;
        g_md_adapter.ref_query = NULL;
        g_md_adapter.extract_declarations = md_extract_declarations;
        g_md_adapter.extract_references = md_extract_references;
        g_md_adapter.resolve_reference = md_resolve_reference;
        g_initialized = 1;
    }
    return &g_md_adapter;
}
