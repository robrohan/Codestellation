#ifndef CODEMAP_GRAPHSTATS_H
#define CODEMAP_GRAPHSTATS_H

#include "../graph/graph.h"

/* Fills fan_in, fan_out, cycle_id, cycle_size and blast_radius on every
 * node from the graph's edges. Edges of different kinds between the same
 * two files count once; self-edges are ignored. */
void graphstats_compute(Graph *g);

/* Ranks files by git_commits x complexity (1 = highest) into
 * hotspot_rank; files missing either number, or with no commits, stay 0.
 * Run after git stats and complexity are in. */
void graphstats_rank_hotspots(Graph *g);

#endif
