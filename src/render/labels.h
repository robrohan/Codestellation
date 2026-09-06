#ifndef CODEMAP_LABELS_H
#define CODEMAP_LABELS_H

/* Forward-declared, not included -- see the same note in ui_panel.h
 * about Nuklear's NK_INCLUDE_* macros needing to match per-TU. */
struct nk_context;

#include "mat4.h"
#include "panel_rect.h"
#include "../graph/graph.h"
#include "../notes/notes.h"

/* Draws each node's filename (not full path) floating just above its
 * projected screen position -- a transparent full-window Nuklear overlay
 * drawn directly via the canvas API rather than through normal widgets,
 * since labels need to sit at arbitrary points, not in a layout flow.
 * Labels that would land under the Inspector or Note pane (wherever the
 * user has dragged/resized either to) are skipped rather than drawn
 * underneath -- pass a rect's w as 0 for "that pane isn't open, nothing to
 * avoid". A node with at least one note (file or group) gets a small
 * amber dot next to its label. */
void labels_draw(struct nk_context *ctx, int window_width, int window_height,
                  PanelRect inspector_bounds, PanelRect note_bounds,
                  const float *view_proj, const Vec3 *positions, const Graph *g, const NoteSet *notes);

#endif
