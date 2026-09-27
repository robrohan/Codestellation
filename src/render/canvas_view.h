#ifndef CODEMAP_CANVAS_VIEW_H
#define CODEMAP_CANVAS_VIEW_H

#include <stdbool.h>

/* SPIKE (step 3 of doc/system-map-plan.md): a 2D box canvas with
 * hard-coded sample content, to judge whether markdown text on a
 * pannable/zoomable canvas is readable enough in this renderer before
 * building the real JSON Canvas feature. Not wired to any file format.
 *
 * Controls: scroll zooms around the cursor; left-drag a box to move it;
 * left-drag empty space, or right/middle-drag anywhere, pans; left-click
 * selects a box. */

struct nk_context;

typedef struct {
    float mx, my;            /* cursor, logical pixels */
    bool left, right, middle;
    float scroll;            /* this frame's wheel delta */
    bool over_panel;         /* cursor is over a floating panel: ignore presses/scroll */
} CanvasInput;

void canvas_view_update(const CanvasInput *in, int width, int height);

/* Draws into a full-window background layer (behind every panel). */
void canvas_view_draw(struct nk_context *ctx, int width, int height);

#endif
