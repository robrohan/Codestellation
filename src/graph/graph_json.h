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

/* Reads back a graph.json written by graph_write_json. Assumes node "id"
 * values are exactly 0..N-1 in file order (true for anything this tool
 * itself wrote) -- doesn't re-key off the "id" field. */
bool graph_read_json(const char *path, Graph *out);

#endif
