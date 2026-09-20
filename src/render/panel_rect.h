#ifndef CODEMAP_PANEL_RECT_H
#define CODEMAP_PANEL_RECT_H

#include <stdbool.h>

/* Forward-declared, not included -- see the NK_INCLUDE_* consistency note
 * in ui_panel.h. panel_header_height's actual definition (panel_rect.c)
 * is its own small TU that defines the same macro set as every other
 * Nuklear-touching .c file and includes nuklear.h for real. */
struct nk_context;

/* A window's on-screen rect in plain floats -- used to hand a floating
 * Nuklear panel's live bounds to code (main.c, labels.c) that
 * deliberately doesn't include nuklear.h itself (see the NK_INCLUDE_*
 * consistency note in ui_panel.h: those macros affect real struct layout,
 * so every TU that sees nuklear.h's actual definitions must define the
 * same set). w == 0 conventionally means "not currently open, nothing to
 * avoid drawing under". */
typedef struct {
    float x, y, w, h;
} PanelRect;

/* w <= 0 (the "not open" convention above) always reports false. Used by
 * both main.c (gate 3D camera/pick interaction) and labels.c (avoid
 * drawing a label under a panel) against the *previous* frame's tracked
 * bounds -- deliberately not nuklear's own nk_window_is_any_hovered,
 * which considers every non-hidden window including the always-present,
 * fullscreen NK_WINDOW_NO_INPUT label overlay (that flag exempts a window
 * from focus/move/scale, not from hover queries) and so reports "hovered"
 * everywhere, all the time. */
static inline bool panel_rect_contains(PanelRect r, float x, float y) {
    return r.w > 0.0f && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

/* The real, current title-bar height for `ctx`'s active style/font --
 * matches nuklear.h's own header-rendering formula exactly (see
 * nk_panel_begin's window-movement and header-drawing blocks), computed
 * from the same public style fields nuklear itself sums rather than a
 * guessed constant (there's no public getter for it). Used both to detect
 * a double-click landing on a panel's header (main.c) and to report a
 * shrunk "just the header" rect once a panel is shaded/collapsed
 * (ui_panel.c/note_compose.c/properties_panel.c), since nk_window_get_bounds
 * keeps returning the *restored* size even while collapsed -- without
 * this, a shaded panel would still block 3D interaction and label drawing
 * over the space it visually gave back. Getting this wrong in the
 * "shrunk header" case specifically used to under-report the real
 * clickable header height: a drag starting in the resulting gap was seen
 * by our own over_panel check as off-panel and started a 3D pan/orbit in
 * the same gesture nuklear was itself using to move the window. */
float panel_header_height(struct nk_context *ctx);

#endif
