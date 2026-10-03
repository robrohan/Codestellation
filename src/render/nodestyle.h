#ifndef CODEMAP_NODESTYLE_H
#define CODEMAP_NODESTYLE_H

#include "../graph/graph.h"

/* Point size for a node with no stats (a graph.json from before stats). */
#define NODESTYLE_DEFAULT_SIZE 9.0f

/* Per-node point size in pixels for the 3D view, one float per node into
 * out (node_count floats): longer files are bigger, on a log scale of line
 * count so a 5,000-line file isn't 500x a 10-line one -- NODESTYLE_MIN_SIZE
 * at 1 line up to NODESTYLE_MAX_SIZE at NODESTYLE_MAX_LINES and beyond. */
#define NODESTYLE_MIN_SIZE 5.0f
#define NODESTYLE_MAX_SIZE 16.0f
#define NODESTYLE_MAX_LINES 5000.0f

void nodestyle_compute(const Graph *g, float *out);

#endif
