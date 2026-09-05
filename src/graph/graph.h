#ifndef CODEMAP_GRAPH_H
#define CODEMAP_GRAPH_H

#include <stddef.h>

typedef struct {
    int   id;
    char *path;
    char *language;
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
