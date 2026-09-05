#ifndef CODEMAP_LABELS_H
#define CODEMAP_LABELS_H

/* Forward-declared, not included -- see the same note in ui_panel.h
 * about Nuklear's NK_INCLUDE_* macros needing to match per-TU. */
struct nk_context;

#include "mat4.h"
#include "../graph/graph.h"

/* Draws each node's filename (not full path) floating just above its
 * projected screen position -- a transparent full-window Nuklear overlay
 * drawn directly via the canvas API rather than through normal widgets,
 * since labels need to sit at arbitrary points, not in a layout flow.
 * Labels that would land under the inspector panel (x >= panel_x) are
 * skipped rather than drawn underneath it. */
void labels_draw(struct nk_context *ctx, int window_width, int window_height, int panel_x,
                  const float *view_proj, const Vec3 *positions, const Graph *g);

#endif
