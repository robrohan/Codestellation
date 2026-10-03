/* Per-file stats from the text and its syntax tree.
 *
 *   - lines / blank_lines: from the text. comment_lines: lines that any
 *     comment node touches (grammars name them "comment", "line_comment",
 *     "block_comment"... -- any named node type containing "comment").
 *   - parse_errors: ERROR and MISSING nodes. A file with many is one whose
 *     dependency edges may be incomplete.
 *   - max_indent: deepest indentation in the file's own indent steps --
 *     a language-agnostic stand-in for nesting depth. The step is the most
 *     common increase in indentation between consecutive lines (tabs count
 *     as 4 columns); lines that start inside a comment are ignored, so a
 *     block comment's " * " doesn't make the step look like 1.
 *   - complexity: McCabe-style decision points + 1, counting the adapter's
 *     branch_types (adapter.h). Each function gets its own decisions + 1,
 *     with a nested function's decisions counted in it, not its parent.
 *
 * Node types are matched by symbol id, not by comparing names per node:
 * the adapter's names are turned into a per-symbol flag table once per
 * file, which matters on large files. */

#include "filestats.h"
#include <tree_sitter/api.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum { F_BRANCH = 1, F_FUNCTION = 2, F_COMMENT = 4 };

static bool in_list(const char *name, const char *const *list) {
    for (int i = 0; list && list[i]; i++) {
        if (strcmp(name, list[i]) == 0) return true;
    }
    return false;
}

/* flags[symbol] for every symbol in the language. Caller frees. */
static unsigned char *symbol_flags(const TSLanguage *lang, const LanguageAdapter *adapter) {
    uint32_t n = ts_language_symbol_count(lang);
    unsigned char *flags = (unsigned char *)calloc(n ? n : 1, 1);
    for (uint32_t sym = 0; sym < n; sym++) {
        const char *name = ts_language_symbol_name(lang, (TSSymbol)sym);
        if (!name) continue;
        if (in_list(name, adapter->branch_types)) flags[sym] |= F_BRANCH;
        if (in_list(name, adapter->function_types)) flags[sym] |= F_FUNCTION;
        if (ts_language_symbol_type(lang, (TSSymbol)sym) == TSSymbolTypeRegular && strstr(name, "comment")) {
            flags[sym] |= F_COMMENT;
        }
    }
    return flags;
}

/* Line i is source[line_start[i] .. line_start[i+1]). */
static uint32_t *line_starts(const char *src, uint32_t len, uint32_t *out_count) {
    uint32_t count = (len > 0 && src[len - 1] != '\n') ? 1 : 0;
    for (uint32_t i = 0; i < len; i++) if (src[i] == '\n') count++;
    uint32_t *starts = (uint32_t *)malloc(sizeof(uint32_t) * (count + 1));
    uint32_t k = 0;
    if (count) starts[k++] = 0;
    for (uint32_t i = 0; i < len && k < count; i++) if (src[i] == '\n') starts[k++] = i + 1;
    starts[count] = len;
    *out_count = count;
    return starts;
}

static bool is_blank_until(const char *src, uint32_t from, uint32_t to) {
    for (uint32_t i = from; i < to; i++) {
        if (src[i] != ' ' && src[i] != '\t' && src[i] != '\r' && src[i] != '\n' && src[i] != '\f') return false;
    }
    return true;
}

typedef struct {
    uint32_t depth;
    int decisions;
} FnFrame;

void filestats_compute(const ParsedFile *file, const LanguageAdapter *adapter, NodeStats *out) {
    const char *src = file->source;
    uint32_t len = file->source_len;
    out->has_stats = true;

    uint32_t line_count = 0;
    uint32_t *starts = line_starts(src, len, &line_count);
    out->lines = (int)line_count;
    out->blank_lines = 0;
    for (uint32_t i = 0; i < line_count; i++) {
        if (is_blank_until(src, starts[i], starts[i + 1])) out->blank_lines++;
    }

    /* Per line: 1 = a comment touches it, 2 = it starts inside a comment. */
    unsigned char *comment_row = (unsigned char *)calloc(line_count + 1, 1);

    const TSLanguage *lang = ts_tree_language(file->tree);
    unsigned char *flags = symbol_flags(lang, adapter);
    uint32_t symbol_count = ts_language_symbol_count(lang);
    bool want_complexity = adapter->branch_types != NULL;
    bool want_functions = want_complexity && adapter->function_types != NULL;

    int parse_errors = 0, decisions = 0, functions = 0, max_fn = 0;
    FnFrame *fns = NULL;
    size_t fn_count = 0, fn_cap = 0;

    TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(file->tree));
    uint32_t depth = 0;
    for (;;) {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        TSSymbol sym = ts_node_symbol(node);
        unsigned char f = sym < symbol_count ? flags[sym] : 0;
        bool descend = true;

        /* Functions at this depth or deeper have been fully walked. */
        while (fn_count && fns[fn_count - 1].depth >= depth) {
            int c = fns[--fn_count].decisions + 1;
            if (c > max_fn) max_fn = c;
        }

        if (ts_node_is_error(node) || ts_node_is_missing(node)) parse_errors++;

        if (f & F_COMMENT) {
            TSPoint a = ts_node_start_point(node), b = ts_node_end_point(node);
            /* A comment ending at column 0 ended with the previous line's newline. */
            uint32_t last = (b.column == 0 && b.row > a.row) ? b.row - 1 : b.row;
            for (uint32_t r = a.row; r <= last && r < line_count; r++) {
                comment_row[r] |= 1;
                if (r > a.row || is_blank_until(src, starts[r], ts_node_start_byte(node))) comment_row[r] |= 2;
            }
            descend = false;
        }

        if (want_functions && (f & F_FUNCTION)) {
            functions++;
            if (fn_count == fn_cap) {
                fn_cap = fn_cap ? fn_cap * 2 : 16;
                fns = (FnFrame *)realloc(fns, fn_cap * sizeof(FnFrame));
            }
            fns[fn_count].depth = depth;
            fns[fn_count].decisions = 0;
            fn_count++;
        }

        if (want_complexity && (f & F_BRANCH)) {
            bool is_default = false;
            if (ts_node_child_count(node) > 0) {
                is_default = strcmp(ts_node_type(ts_node_child(node, 0)), "default") == 0;
            }
            if (!is_default) {
                decisions++;
                if (fn_count) fns[fn_count - 1].decisions++;
            }
        }

        if (descend && ts_tree_cursor_goto_first_child(&cursor)) {
            depth++;
            continue;
        }
        bool done = false;
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) { done = true; break; }
            depth--;
        }
        if (done) break;
    }
    ts_tree_cursor_delete(&cursor);
    while (fn_count) {
        int c = fns[--fn_count].decisions + 1;
        if (c > max_fn) max_fn = c;
    }
    free(fns);
    free(flags);

    out->parse_errors = parse_errors;
    out->comment_lines = 0;
    for (uint32_t i = 0; i < line_count; i++) if (comment_row[i] & 1) out->comment_lines++;
    if (want_complexity) out->complexity = decisions + 1;
    if (want_functions) {
        out->functions = functions;
        out->max_function_complexity = functions ? max_fn : 0;
    }

    /* Indentation: leading width of each non-blank, non-comment line. */
    int increases[9] = { 0 };
    int prev = -1, widest = 0;
    for (uint32_t i = 0; i < line_count; i++) {
        if (comment_row[i] & 2) continue;
        if (is_blank_until(src, starts[i], starts[i + 1])) continue;
        int w = 0;
        for (uint32_t k = starts[i]; k < starts[i + 1]; k++) {
            if (src[k] == ' ') w++;
            else if (src[k] == '\t') w += 4;
            else break;
        }
        if (w > widest) widest = w;
        if (prev >= 0 && w > prev && w - prev <= 8) increases[w - prev]++;
        prev = w;
    }
    int step = 0;
    for (int d = 1; d <= 8; d++) {
        if (increases[d] > (step ? increases[step] : 0)) step = d;
    }
    out->max_indent = step ? widest / step : 0;

    free(comment_row);
    free(starts);
}
