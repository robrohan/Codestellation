#ifndef CODEMAP_GRAPH_H
#define CODEMAP_GRAPH_H

#include <stdbool.h>
#include <stddef.h>

/* Per-file numbers worked out during the build (pipeline/filestats.h,
 * graphstats.h, gitstats.h) and shown in the Inspector. -1 means "not
 * applicable / not known" (complexity for JSON, git numbers outside a
 * repo); `has_stats` is false for a graph.json written before stats
 * existed, so the UI can say "rebuild" instead of showing zeros. */
typedef struct {
    bool has_stats;

    /* From the file text and its syntax tree -- every language. */
    int lines, blank_lines, comment_lines;
    int max_indent;   /* deepest indentation, in the file's own indent steps */
    int parse_errors; /* ERROR + MISSING nodes in the syntax tree */

    /* Cyclomatic complexity, for languages whose adapter lists branch
     * node types (-1 otherwise). complexity = decisions + 1 for the whole
     * file; functions/max_function_complexity need function node types. */
    int complexity, functions, max_function_complexity;

    /* From the dependency graph. */
    int fan_in, fan_out; /* distinct files depending on / depended on */
    int blast_radius;    /* files that depend on this one, directly or not (-1: graph too big) */
    int cycle_id;        /* files sharing a cycle_id form a dependency cycle; -1: none */
    int cycle_size;
    int unresolved;      /* references this file makes that matched no file */

    /* From `git log`, -1 when the file isn't in a git repo. */
    int git_commits, git_authors;
    long long git_last_commit; /* unix seconds, 0 if unknown */
    int hotspot_rank;          /* 1 = most commits x complexity; 0: unranked */
} NodeStats;

void node_stats_init(NodeStats *s);

typedef struct {
    int   id;
    char *path;
    char *language;
    NodeStats stats;
} GraphNode;

typedef struct {
    int   source;
    int   target;
    char *kind;
} GraphEdge;

typedef struct {
    GraphNode *nodes;
    size_t     node_count, node_cap;
    GraphEdge *edges;
    size_t     edge_count, edge_cap;
} Graph;

void graph_init(Graph *g);
int  graph_add_node(Graph *g, const char *path, const char *language);
void graph_add_edge(Graph *g, int source, int target, const char *kind);
void graph_free(Graph *g);

#endif
