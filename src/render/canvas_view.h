#ifndef CODEMAP_CANVAS_VIEW_H
#define CODEMAP_CANVAS_VIEW_H

#include <stdbool.h>
#include <stddef.h>
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
 *   shift+click box            go into the canvas it links to ([[x.canvas]]),
 *                              open the folders it links to in 3D, or -- on a
 *                              weak link -- jump to the box it references
 * Search (Cmd/Ctrl+F or the breadcrumb bar's button) covers every canvas
 * reachable from the root. Each result can be jumped to, or dropped onto
 * the current canvas as a weak link: a read-only, dashed box showing the
 * original's live text, stored as a JSON Canvas file node
 * (file "x.canvas", subpath "#<node id>").
 *
 * Keys (handled by main.c, routed here):
 *   Delete/Backspace           delete the selection (not while typing)
 *   Cmd/Ctrl+F                 search every canvas
 *   Esc                        leave code view, else close search, else close
 *                              the editor, else deselect, else go up a level
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

/* Folder links. Shift+click (or "Open code") on a box whose [[links]]
 * resolve to directories -- and that has no canvas link, which would win --
 * queues those folders. main.c takes the request, builds the folders into
 * one graph, and on success calls canvas_view_enter_code: the canvas hides,
 * the 3D explorer shows, and the breadcrumb bar gains a final "</> title"
 * crumb. Esc or any canvas crumb comes back. Taken strings (the array, each
 * path, the title) are the caller's to free. */
bool canvas_view_take_code_request(char ***dirs, size_t *count, char **title);
void canvas_view_enter_code(const char *title);
bool canvas_view_in_code(void);

/* Opens the search panel (canvas view only), with the query focused. */
void canvas_view_open_search(void);

/* Esc: leave code view, else close search, else close the editor, else
 * clear the selection, else go up one level. */
void canvas_view_escape(void);

/* Bounds of the canvas's floating panels this frame (w == 0 when not
 * shown), so main.c can keep canvas/3D input off them. */
typedef struct {
    PanelRect crumbs, editor, search;
} CanvasPanels;

/* Draws the canvas into a full-window background layer, plus the
 * breadcrumb bar and (when open) the editor and search panels. In code
 * mode only the breadcrumb bar is drawn. */
void canvas_view_draw(struct nk_context *ctx, int width, int height, CanvasPanels *out);

#endif
