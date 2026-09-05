#include "graph.h"
#include "../common/pathutil.h"
#include <stdlib.h>
#include <string.h>

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
}
