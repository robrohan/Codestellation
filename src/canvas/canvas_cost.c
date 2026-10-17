#include "canvas_cost.h"
#include "project.h"
#include "../common/code_cache.h"
#include "../common/cost.h"
#include "../common/pathutil.h"
#include "../common/symtab.h"
#include "../graph/graph_json.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Canvas nesting deeper than this is treated as a mistake, not followed. */
#define MAX_NESTING 64

/* One graph.json boiled down to what costing needs. */
typedef struct {
    char *path;
    long long mtime;
    char **files;
    CodeTotals *totals; /* per file */
    size_t count;
} GraphLines;

static GraphLines *g_graphs = NULL;
static size_t g_graph_count = 0, g_graph_cap = 0;

static long long mtime_of(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long long)st.st_mtime : -1;
}

static void free_lines(GraphLines *gl) {
    for (size_t i = 0; i < gl->count; i++) free(gl->files[i]);
    free(gl->files);
    free(gl->totals);
    free(gl->path);
}

/* The cached lines for graph_path, (re)reading it if it changed. NULL if
 * it doesn't exist or can't be read. */
static const GraphLines *graph_lines(const char *graph_path) {
    long long mtime = mtime_of(graph_path);
    if (mtime < 0) return NULL;
    GraphLines *slot = NULL;
    for (size_t i = 0; i < g_graph_count; i++) {
        if (strcmp(g_graphs[i].path, graph_path) == 0) {
            if (g_graphs[i].mtime == mtime) return &g_graphs[i];
            slot = &g_graphs[i];
            break;
        }
    }

    Graph g;
    if (!graph_read_json(graph_path, &g)) return NULL;
    if (slot) {
        free_lines(slot);
    } else {
        if (g_graph_count == g_graph_cap) {
            g_graph_cap = g_graph_cap ? g_graph_cap * 2 : 8;
            g_graphs = (GraphLines *)realloc(g_graphs, g_graph_cap * sizeof(GraphLines));
        }
        slot = &g_graphs[g_graph_count++];
    }
    memset(slot, 0, sizeof(*slot));
    slot->path = xstrdup(graph_path);
    slot->mtime = mtime;
    slot->files = (char **)malloc((g.node_count ? g.node_count : 1) * sizeof(char *));
    slot->totals = (CodeTotals *)calloc(g.node_count ? g.node_count : 1, sizeof(CodeTotals));
    for (size_t i = 0; i < g.node_count; i++) {
        CodeTotals *t = &slot->totals[slot->count];
        code_totals_add(t, &g.nodes[i].stats, g.nodes[i].language);
        if (t->files == 0) continue; /* not code */
        slot->files[slot->count++] = xstrdup(g.nodes[i].path);
    }
    graph_free(&g);
    return slot;
}

typedef struct {
    CodeSize size;
    SymbolTable *seen_files;
    SymbolTable *seen_canvases;
    int depth;
} Walk;

static void add_dirs(Walk *w, char **dirs, size_t count) {
    char **norm = code_cache_normalize_dirs((const char *const *)dirs, count);
    char *cache = code_cache_dir(norm, count);
    char *graph_path = cache ? path_join(cache, "graph.json") : NULL;
    const GraphLines *gl = graph_path ? graph_lines(graph_path) : NULL;
    if (!gl) {
        w->size.unbuilt++;
    } else {
        for (size_t i = 0; i < gl->count; i++) {
            if (symtab_get(w->seen_files, gl->files[i]) >= 0) continue;
            symtab_put(w->seen_files, gl->files[i], 1);
            code_totals_merge(&w->size.code, &gl->totals[i]);
        }
    }
    free(graph_path);
    free(cache);
    code_cache_free_dirs(norm, count);
}

static void add_canvas(Walk *w, const char *canvas_path);

static void add_node(Walk *w, const CanvasNode *n, const char *canvas_path) {
    if (canvas_node_is_weak_link(n)) return; /* a reference, not part of this box */

    char **dirs;
    size_t ndirs = canvas_node_dir_links(n, canvas_path, &dirs);
    if (ndirs) add_dirs(w, dirs, ndirs);
    for (size_t i = 0; i < ndirs; i++) free(dirs[i]);
    free(dirs);

    char **links;
    size_t nlinks = canvas_node_canvas_links(n, &links);
    for (size_t i = 0; i < nlinks; i++) {
        char *path = project_resolve_link(canvas_path, links[i]);
        add_canvas(w, path);
        free(path);
        free(links[i]);
    }
    free(links);
}

static void add_canvas(Walk *w, const char *canvas_path) {
    if (w->depth >= MAX_NESTING || symtab_get(w->seen_canvases, canvas_path) >= 0) return;
    symtab_put(w->seen_canvases, canvas_path, 1);
    CanvasDoc doc;
    if (canvas_doc_read(canvas_path, &doc)) {
        w->depth++;
        for (size_t i = 0; i < doc.node_count; i++) add_node(w, &doc.nodes[i], canvas_path);
        w->depth--;
    }
    canvas_doc_free(&doc);
}

static void walk_begin(Walk *w) {
    memset(w, 0, sizeof(*w));
    w->seen_files = symtab_create(1024);
    w->seen_canvases = symtab_create(32);
}

static CodeSize walk_end(Walk *w) {
    symtab_destroy(w->seen_files);
    symtab_destroy(w->seen_canvases);
    return w->size;
}

CodeSize canvas_cost_node(const CanvasNode *n, const char *canvas_path) {
    Walk w;
    walk_begin(&w);
    /* The box's own canvas counts as visited, so a child linking back up
     * doesn't pull the parent's other boxes into this one. */
    symtab_put(w.seen_canvases, canvas_path, 1);
    add_node(&w, n, canvas_path);
    return walk_end(&w);
}

CodeSize canvas_cost_canvas(const char *canvas_path) {
    Walk w;
    walk_begin(&w);
    add_canvas(&w, canvas_path);
    return walk_end(&w);
}

void canvas_cost_shutdown(void) {
    for (size_t i = 0; i < g_graph_count; i++) free_lines(&g_graphs[i]);
    free(g_graphs);
    g_graphs = NULL;
    g_graph_count = g_graph_cap = 0;
}
