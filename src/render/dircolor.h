#ifndef CODEMAP_DIRCOLOR_H
#define CODEMAP_DIRCOLOR_H

#include "../graph/graph.h"

/* Fills out_colors[i*3 .. i*3+2] (RGB, each in [0,1]) for every node in
 * g, grouping by a shared-ancestor-relative directory prefix -- see this
 * file's top comment for exactly how the grouping key is derived --
 * hashed to a hue at fixed saturation/lightness. out_colors must have
 * room for g->node_count * 3 floats. */
void dircolor_compute(const Graph *g, float *out_colors);

#endif
