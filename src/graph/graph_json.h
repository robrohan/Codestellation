#ifndef CODEMAP_GRAPH_JSON_H
#define CODEMAP_GRAPH_JSON_H

#include <stdbool.h>
#include "graph.h"

/* Writes the graph as networkx "node-link" JSON:
 *   {"directed": true, "graph": {}, "nodes": [{"id", "path", "language"}],
 *    "links": [{"source", "target", "kind"}]}
 * Load in Python with:
 *   nx.node_link_graph(json.load(f), edges="links")  # networkx >= 3.x
 */
bool graph_write_json(const Graph *g, const char *out_path);

#endif
