#ifndef CODEMAP_CANVAS_VIEW_H
#define CODEMAP_CANVAS_VIEW_H

#include <stdbool.h>
#include "panel_rect.h"

/* The system-map canvas: a stack of JSON Canvas files (see
 * canvas/canvas_doc.h), rooted at a project's root canvas, drawn as boxes
 * and edges on a zoomable grid with a breadcrumb bar and a side editor.
 *
 * Mouse:
 *   scroll                     zoom around the cursor
 *   left-drag box              move it (bottom-right corner of a selected box: resize)
 *   left-drag a side dot       draw an edge to another box
 *   left-drag empty / right / middle-drag   pan
 *   click                      select a box or edge
 *   double-click box / edge    edit it in the side panel
 *   double-click empty         new box
 *   shift+click box            go into the canvas it links to ([[x.canvas]])
 * Keys (handled by main.c, routed here):
 *   Delete/Backspace           delete the selection (not while typing)
 *   Esc                        close the editor, else deselect, else go up a level
 *
 * Every change is saved back to the current .canvas file automatically
 * (debounced while typing). */

struct nk_context;

typedef struct {
    float mx, my;            /* cursor, logical pixels */
    bool left, right, middle;
    bool shift;
    float scroll;            /* this frame's wheel delta */
    double time;             /* glfwGetTime(), for double-clicks and save debounce */
    bool over_panel;         /* cursor is over a floating panel: ignore presses/scroll */
    bool key_delete;         /* Delete/Backspace pressed this frame */
} CanvasInput;

/* Opens a project's root canvas as the first breadcrumb, closing any
 * canvas already open. root_title labels that crumb. */
void canvas_view_open(const char *root_canvas_path, const char *root_title);

/* Saves anything pending and closes. Safe to call when nothing is open. */
void canvas_view_close(void);

bool canvas_view_is_open(void);

void canvas_view_update(const CanvasInput *in, int width, int height);

/* Esc: close the editor, else clear the selection, else go up one level. */
void canvas_view_escape(void);

/* Draws the canvas into a full-window background layer, plus the
 * breadcrumb bar and (when open) the editor panel, reporting both
 * panels' bounds so main.c can keep canvas input off them. */
void canvas_view_draw(struct nk_context *ctx, int width, int height, PanelRect *out_crumbs,
                      PanelRect *out_editor);

#endif
