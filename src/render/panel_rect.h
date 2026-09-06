#ifndef CODEMAP_PANEL_RECT_H
#define CODEMAP_PANEL_RECT_H

#include <stdbool.h>

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

/* Approximate default Nuklear title-bar height -- there's no public
 * getter for the exact value. Used both to detect a double-click landing
 * on a panel's header (main.c) and to report a shrunk "just the header"
 * rect once a panel is shaded/collapsed (ui_panel.c/note_compose.c),
 * since nk_window_get_bounds keeps returning the *restored* size even
 * while collapsed -- without this, a shaded panel would still block 3D
 * interaction and label drawing over the space it visually gave back. */
#define PANEL_HEADER_HEIGHT 30.0f

#endif
