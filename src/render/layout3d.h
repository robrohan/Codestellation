#ifndef CODEMAP_LAYOUT3D_H
#define CODEMAP_LAYOUT3D_H

#include "mat4.h"
#include "../graph/graph.h"

/* Computes a once-at-load 3D layout into out_positions (caller-allocated,
 * g->node_count entries): seeds each node near others in the same
 * directory (so the initial state roughly tracks the file tree), then
 * relaxes with a 3D Fruchterman-Reingold spring embedder. Naive O(n^2)
 * repulsion -- fine at load time for hundreds-to-low-thousands of nodes;
 * a Barnes-Hut approximation would be the scale-up path, not needed yet. */
void layout3d_compute(const Graph *g, Vec3 *out_positions, int iterations);

/* Max distance from the origin across all positions -- used to pick an
 * initial camera distance that actually frames the graph, since layouts
 * range from a handful of nodes to thousands. */
float layout3d_bounding_radius(const Vec3 *positions, size_t count);

#endif
