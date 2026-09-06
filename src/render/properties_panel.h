#ifndef CODEMAP_PROPERTIES_PANEL_H
#define CODEMAP_PROPERTIES_PANEL_H

#include "panel_rect.h"

/* Forward-declared rather than including nuklear.h -- see ui_panel.h. */
struct nk_context;

/* Shared with main.c's double-click-header-to-shade handling. */
#define PROPERTIES_PANEL_TITLE "Properties"

/* Renders the floating, resizable, collapsible Properties pane -- always
 * drawn (unlike the Note pane, which only appears while composing), since
 * it's the primary way to get started when no project is loaded yet.
 *
 * *show_origin is the "Show origin" checkbox's own state, owned by the
 * caller (gates the axis gizmo draw call in main.c) and just flipped
 * here. Plain `int` (0/1), not C99 `bool` -- this is a Nuklear `nk_bool`,
 * which in this codebase's build (no NK_INCLUDE_STANDARD_BOOL) is `int`,
 * not `_Bool`; the two aren't the same size, so `nk_checkbox_label` needs
 * an actual `int*` here, not a `bool*`.
 *
 * Returns a newly malloc'd path (caller frees) the one frame the Open
 * button was clicked and the native folder picker returned a real
 * selection -- NULL every other frame, including when the picker was
 * canceled. *out_bounds receives this frame's live window rect, same
 * convention as ui_panel_draw/note_compose_draw. */
char *properties_panel_draw(struct nk_context *ctx, int *show_origin, PanelRect *out_bounds);

#endif
