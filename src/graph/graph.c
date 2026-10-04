#include "graph.h"
#include "../common/pathutil.h"
#include <stdlib.h>
#include <string.h>

void node_stats_init(NodeStats *s) {
    memset(s, 0, sizeof(*s));
    s->complexity = s->functions = s->max_function_complexity = -1;
    s->blast_radius = -1;
    s->cycle_id = -1;
    s->git_commits = s->git_authors = -1;
}

void graph_init(Graph *g) {
    memset(g, 0, sizeof(*g));
}

int graph_add_node(Graph *g, const char *path, const char *language) {
    if (g->node_count == g->node_cap) {
        g->node_cap = g->node_cap ? g->node_cap * 2 : 64;
        g->nodes = (GraphNode *)realloc(g->nodes, g->node_cap * sizeof(GraphNode));
    }
    int id = (int)g->node_count;
    g->nodes[g->node_count].id = id;
    g->nodes[g->node_count].path = xstrdup(path);
    g->nodes[g->node_count].language = xstrdup(language);
    node_stats_init(&g->nodes[g->node_count].stats);
    g->node_count++;
    return id;
}

void graph_add_edge(Graph *g, int source, int target, const char *kind) {
    if (g->edge_count == g->edge_cap) {
        g->edge_cap = g->edge_cap ? g->edge_cap * 2 : 64;
        g->edges = (GraphEdge *)realloc(g->edges, g->edge_cap * sizeof(GraphEdge));
    }
    g->edges[g->edge_count].source = source;
    g->edges[g->edge_count].target = target;
    g->edges[g->edge_count].kind = xstrdup(kind);
    g->edge_count++;
}

void graph_add_clone(Graph *g, const GraphClone *c) {
    if (g->clone_count == g->clone_cap) {
        g->clone_cap = g->clone_cap ? g->clone_cap * 2 : 64;
        g->clones = (GraphClone *)realloc(g->clones, g->clone_cap * sizeof(GraphClone));
    }
    g->clones[g->clone_count++] = *c;
}

void graph_free(Graph *g) {
    for (size_t i = 0; i < g->node_count; i++) {
        free(g->nodes[i].path);
        free(g->nodes[i].language);
    }
    for (size_t i = 0; i < g->edge_count; i++) {
        free(g->edges[i].kind);
    }
    free(g->nodes);
    free(g->edges);
    free(g->clones);
}
