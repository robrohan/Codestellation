/* Git history per file, for churn and hotspot stats.
 *
 * For each work tree (roots inside the same repo share one run):
 *
 *   git -c core.quotepath=off -C <top> log --no-merges --no-renames
 *       --format=%x01%at%x09%ae --name-only -- <root>...
 *
 * prints, per commit, "\x01<unix time>\t<author email>" then the paths it
 * touched, relative to the work tree top. Each path is joined to the top
 * and looked up among the graph's (already normalized) node paths.
 *
 * On macOS /usr/bin/git is a stub that pops up a "install developer
 * tools" dialog when they're missing, so git is only run there when
 * `xcode-select -p` says the tools are installed.
 */

#include "gitstats.h"
#include "../common/pathutil.h"
#include "../common/symtab.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#define NULL_DEVICE "NUL"
#else
#define NULL_DEVICE "/dev/null"
#endif

/* Appends `s` to the command, quoted for the platform's shell. */
static void append_quoted(char **cmd, size_t *len, const char *s) {
    size_t extra = strlen(s) * 4 + 4;
    *cmd = (char *)realloc(*cmd, *len + extra);
    char *p = *cmd + *len;
#ifdef _WIN32
    *p++ = '"';
    for (const char *c = s; *c; c++) *p++ = *c; /* '"' can't appear in a Windows path */
    *p++ = '"';
#else
    *p++ = '\'';
    for (const char *c = s; *c; c++) {
        if (*c == '\'') { memcpy(p, "'\\''", 4); p += 4; }
        else *p++ = *c;
    }
    *p++ = '\'';
#endif
    *p = '\0';
    *len = (size_t)(p - *cmd);
}

static void append(char **cmd, size_t *len, const char *s) {
    size_t n = strlen(s);
    *cmd = (char *)realloc(*cmd, *len + n + 1);
    memcpy(*cmd + *len, s, n + 1);
    *len += n;
}

static bool git_available(void) {
#ifdef __APPLE__
    return system("xcode-select -p >/dev/null 2>&1") == 0;
#else
    return true;
#endif
}

/* The normalized work-tree top containing `dir`, or NULL. Caller frees. */
static char *work_tree_top(const char *dir) {
    char *cmd = NULL;
    size_t len = 0;
    append(&cmd, &len, "git -C ");
    append_quoted(&cmd, &len, dir);
    append(&cmd, &len, " rev-parse --show-toplevel 2>" NULL_DEVICE);
    FILE *p = popen(cmd, "r");
    free(cmd);
    if (!p) return NULL;
    char line[4096];
    char *top = NULL;
    if (fgets(line, sizeof(line), p)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0]) top = path_normalize(line);
    }
    pclose(p);
    return top;
}

static bool has_prefix_dir(const char *path, const char *dir) {
    size_t n = strlen(dir);
    if (strncmp(path, dir, n) != 0) return false;
    return path[n] == '/' || path[n] == '\\' || (n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\'));
}

static uint64_t fnv1a(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

typedef struct {
    uint64_t *items;
    size_t count, cap;
} AuthorSet;

static void author_add(AuthorSet *set, uint64_t h) {
    for (size_t i = 0; i < set->count; i++) if (set->items[i] == h) return;
    if (set->count == set->cap) {
        set->cap = set->cap ? set->cap * 2 : 4;
        set->items = (uint64_t *)realloc(set->items, set->cap * sizeof(uint64_t));
    }
    set->items[set->count++] = h;
}

static void read_log(Graph *g, SymbolTable *by_path, AuthorSet *authors, const char *top,
                     const char *const *roots, size_t root_count) {
    char *cmd = NULL;
    size_t len = 0;
    append(&cmd, &len, "git -c core.quotepath=off -C ");
    append_quoted(&cmd, &len, top);
    append(&cmd, &len, " log --no-merges --no-renames --format=%x01%at%x09%ae --name-only --");
    for (size_t i = 0; i < root_count; i++) {
        append(&cmd, &len, " ");
        append_quoted(&cmd, &len, roots[i]);
    }
    append(&cmd, &len, " 2>" NULL_DEVICE);
    FILE *p = popen(cmd, "r");
    free(cmd);
    if (!p) return;

    char line[8192];
    long long when = 0;
    uint64_t who = 0;
    while (fgets(line, sizeof(line), p)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\x01') {
            when = atoll(line + 1);
            char *tab = strchr(line, '\t');
            who = fnv1a(tab ? tab + 1 : "");
            continue;
        }
        if (!line[0]) continue;
        char *joined = path_join(top, line);
        char *full = path_clean(joined);
        free(joined);
        int id = symtab_get(by_path, full);
        free(full);
        if (id < 0) continue;
        NodeStats *st = &g->nodes[id].stats;
        st->git_commits++;
        if (when > st->git_last_commit) st->git_last_commit = when;
        author_add(&authors[id], who);
    }
    pclose(p);
}

void gitstats_compute(Graph *g, const char *const *roots, size_t root_count) {
    if (g->node_count == 0 || root_count == 0 || !git_available()) return;

    char **tops = (char **)calloc(root_count, sizeof(char *));
    for (size_t i = 0; i < root_count; i++) {
        char *norm = path_normalize(roots[i]);
        tops[i] = work_tree_top(norm ? norm : roots[i]);
        free(norm);
    }

    SymbolTable *by_path = symtab_create(g->node_count * 2 + 16);
    for (size_t i = 0; i < g->node_count; i++) symtab_put(by_path, g->nodes[i].path, (int)i);
    AuthorSet *authors = (AuthorSet *)calloc(g->node_count, sizeof(AuthorSet));

    bool *done = (bool *)calloc(root_count, sizeof(bool));
    for (size_t i = 0; i < root_count; i++) {
        if (!tops[i] || done[i]) continue;
        /* Every root in this work tree goes into one `git log`. */
        const char **group = (const char **)malloc(sizeof(char *) * root_count);
        char **norms = (char **)calloc(root_count, sizeof(char *));
        size_t n = 0;
        for (size_t j = i; j < root_count; j++) {
            if (!tops[j] || done[j] || strcmp(tops[j], tops[i]) != 0) continue;
            done[j] = true;
            norms[n] = path_normalize(roots[j]);
            group[n] = norms[n] ? norms[n] : roots[j];
            n++;
        }
        /* Files in this work tree start at 0: in the repo, maybe never committed. */
        for (size_t v = 0; v < g->node_count; v++) {
            if (g->nodes[v].stats.git_commits < 0 && has_prefix_dir(g->nodes[v].path, tops[i])) {
                g->nodes[v].stats.git_commits = 0;
            }
        }
        read_log(g, by_path, authors, tops[i], group, n);
        for (size_t k = 0; k < n; k++) free(norms[k]);
        free(norms);
        free(group);
    }

    for (size_t v = 0; v < g->node_count; v++) {
        if (g->nodes[v].stats.git_commits >= 0) g->nodes[v].stats.git_authors = (int)authors[v].count;
        free(authors[v].items);
    }
    free(authors);
    free(done);
    symtab_destroy(by_path);
    for (size_t i = 0; i < root_count; i++) free(tops[i]);
    free(tops);
}
