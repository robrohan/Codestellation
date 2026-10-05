/* Duplicate code across the whole project, token-based (the approach of
 * PMD's CPD and jscpd):
 *
 *   1. Each code file becomes a stream of tokens: the syntax tree's
 *      leaves, comments skipped, so whitespace, layout and comments don't
 *      matter. A token is its exact text, hashed and seeded with the
 *      language, so a renamed copy doesn't match and C never matches Go.
 *   2. Every run of DUPES_MIN_TOKENS tokens gets a rolling hash, and all
 *      runs from all files are sorted by it -- equal hashes are candidate
 *      copies, wherever they live.
 *   3. A pair of runs is only taken where it starts (the tokens just
 *      before differ), then grown forward token by token, so one long
 *      copy is reported once, not once per window. Growing compares the
 *      tokens, so a hash collision comes out short and is dropped.
 *
 * Two runs in the same file can't overlap: a loop of a few repeated
 * statements isn't a copy of itself. */

#include "dupes.h"
#include <tree_sitter/api.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runs with the same hash beyond this many are ignored: boilerplate that
 * appears hundreds of times would otherwise cost n^2 pairs. */
#define BUCKET_MAX 64
/* graph.json stays a sensible size even on a copy-paste-heavy project. */
#define CLONES_MAX 20000
/* Bigger files are generated (parser tables, minified bundles): their
 * copies aren't worth flagging, and they can hold most of a project's
 * tokens -- each costs ~32 bytes of memory here. */
#define FILE_BYTES_MAX (1024u * 1024u)

#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL
#define ROLL_BASE 0x9e3779b97f4a7c15ULL

typedef struct {
    uint64_t *hash;
    uint32_t *first_row, *last_row; /* 0-based rows the token starts/ends on */
    uint32_t  count, cap;
} Tokens;

typedef struct {
    uint64_t hash;
    uint32_t file, pos;
} Window;

static uint64_t fnv(uint64_t h, const char *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)p[i];
        h *= FNV_PRIME;
    }
    return h;
}

static void push_token(Tokens *t, uint64_t h, uint32_t first_row, uint32_t last_row) {
    if (t->count == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 1024;
        t->hash = (uint64_t *)realloc(t->hash, t->cap * sizeof(uint64_t));
        t->first_row = (uint32_t *)realloc(t->first_row, t->cap * sizeof(uint32_t));
        t->last_row = (uint32_t *)realloc(t->last_row, t->cap * sizeof(uint32_t));
    }
    t->hash[t->count] = h;
    t->first_row[t->count] = first_row;
    t->last_row[t->count] = last_row;
    t->count++;
}

/* is_comment[symbol] for every symbol in the language -- same rule as
 * filestats.c. Caller frees. */
static unsigned char *comment_symbols(const TSLanguage *lang) {
    uint32_t n = ts_language_symbol_count(lang);
    unsigned char *flags = (unsigned char *)calloc(n ? n : 1, 1);
    for (uint32_t sym = 0; sym < n; sym++) {
        const char *name = ts_language_symbol_name(lang, (TSSymbol)sym);
        if (name && ts_language_symbol_type(lang, (TSSymbol)sym) == TSSymbolTypeRegular && strstr(name, "comment")) {
            flags[sym] = 1;
        }
    }
    return flags;
}

static void tokenize(const ParsedFileEntry *e, Tokens *out) {
    const ParsedFile *file = &e->parsed;
    uint64_t seed = fnv(FNV_OFFSET, e->adapter->name, strlen(e->adapter->name));
    const TSLanguage *lang = ts_tree_language(file->tree);
    unsigned char *is_comment = comment_symbols(lang);
    uint32_t symbol_count = ts_language_symbol_count(lang);

    TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(file->tree));
    for (;;) {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        TSSymbol sym = ts_node_symbol(node);
        bool comment = sym < symbol_count && is_comment[sym];
        if (!comment && ts_tree_cursor_goto_first_child(&cursor)) continue;
        if (!comment) {
            uint32_t a = ts_node_start_byte(node), b = ts_node_end_byte(node);
            if (b > a && b <= file->source_len) {
                TSPoint pa = ts_node_start_point(node), pb = ts_node_end_point(node);
                push_token(out, fnv(seed, file->source + a, b - a), pa.row, pb.row);
            }
        }
        bool done = false;
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) { done = true; break; }
        }
        if (done) break;
    }
    ts_tree_cursor_delete(&cursor);
    free(is_comment);
}

static int cmp_window(const void *pa, const void *pb) {
    const Window *a = (const Window *)pa, *b = (const Window *)pb;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    if (a->file != b->file) return a->file < b->file ? -1 : 1;
    return a->pos < b->pos ? -1 : a->pos > b->pos;
}

void dupes_compute(const ParsedFileList *parsed, Graph *g) {
    size_t file_count = parsed->count < g->node_count ? parsed->count : g->node_count;
    Tokens *toks = (Tokens *)calloc(file_count ? file_count : 1, sizeof(Tokens));

    size_t window_total = 0;
    for (size_t i = 0; i < file_count; i++) {
        const ParsedFileEntry *e = &parsed->entries[i];
        if (!e->adapter || !e->adapter->branch_types || !e->parsed.tree) continue;
        if (e->parsed.source_len > FILE_BYTES_MAX) continue;
        tokenize(e, &toks[i]);
        if (toks[i].count >= DUPES_MIN_TOKENS) window_total += toks[i].count - DUPES_MIN_TOKENS + 1;
    }

    uint64_t top_power = 1; /* ROLL_BASE^(DUPES_MIN_TOKENS-1), mod 2^64 */
    for (int k = 1; k < DUPES_MIN_TOKENS; k++) top_power *= ROLL_BASE;

    Window *win = (Window *)malloc((window_total ? window_total : 1) * sizeof(Window));
    size_t wn = 0;
    for (size_t i = 0; i < file_count; i++) {
        const Tokens *t = &toks[i];
        if (t->count < DUPES_MIN_TOKENS) continue;
        uint64_t h = 0;
        for (uint32_t k = 0; k < DUPES_MIN_TOKENS; k++) h = h * ROLL_BASE + t->hash[k];
        for (uint32_t p = 0;; p++) {
            win[wn++] = (Window){ h, (uint32_t)i, p };
            if (p + DUPES_MIN_TOKENS >= t->count) break;
            h = (h - t->hash[p] * top_power) * ROLL_BASE + t->hash[p + DUPES_MIN_TOKENS];
        }
    }
    qsort(win, wn, sizeof(Window), cmp_window);

    size_t found = 0;
    bool capped = false;
    for (size_t s = 0; s < wn && !capped;) {
        size_t e = s + 1;
        while (e < wn && win[e].hash == win[s].hash) e++;
        size_t end = e - s > BUCKET_MAX ? s + BUCKET_MAX : e;
        for (size_t x = s; x < end && !capped; x++) {
            for (size_t y = x + 1; y < end; y++) {
                const Window *wa = &win[x], *wb = &win[y];
                const Tokens *ta = &toks[wa->file], *tb = &toks[wb->file];
                uint32_t pa = wa->pos, pb = wb->pos;
                /* Not where the copy starts: the pair one token back covers it. */
                if (pa > 0 && pb > 0 && ta->hash[pa - 1] == tb->hash[pb - 1]) continue;
                uint32_t limit = UINT32_MAX;
                if (wa->file == wb->file) limit = pb - pa; /* sorted, so pa < pb */
                uint32_t len = 0;
                while (len < limit && pa + len < ta->count && pb + len < tb->count &&
                       ta->hash[pa + len] == tb->hash[pb + len]) {
                    len++;
                }
                if (len < DUPES_MIN_TOKENS) continue;
                GraphClone c = {
                    (int)wa->file, (int)ta->first_row[pa] + 1, (int)ta->last_row[pa + len - 1] + 1,
                    (int)wb->file, (int)tb->first_row[pb] + 1, (int)tb->last_row[pb + len - 1] + 1,
                };
                if (c.a_end - c.a_line + 1 < DUPES_MIN_LINES || c.b_end - c.b_line + 1 < DUPES_MIN_LINES) continue;
                graph_add_clone(g, &c);
                if (++found >= CLONES_MAX) { capped = true; break; }
            }
        }
        s = e;
    }
    printf("duplicates: %zu copied stretches%s\n", found, capped ? " (stopped at the limit)" : "");

    free(win);
    for (size_t i = 0; i < file_count; i++) {
        free(toks[i].hash);
        free(toks[i].first_row);
        free(toks[i].last_row);
    }
    free(toks);
}
