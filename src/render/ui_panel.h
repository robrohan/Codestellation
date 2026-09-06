#ifndef CODEMAP_UI_PANEL_H
#define CODEMAP_UI_PANEL_H

#include <stddef.h>
#include "panel_rect.h"
#include "../notes/notes.h"

/* Forward-declared rather than including nuklear.h here: Nuklear's
 * NK_INCLUDE_* feature macros affect struct layout, so every TU that
 * sees the real definition must define the same set consistently --
 * simplest to confine that to ui_panel.c and main.c and keep this header
 * opaque. */
struct nk_context;

/* Initial width only -- the window is NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE,
 * so after its first frame this constant only shapes the first-launch
 * default placement (see ui_panel_draw); the user's own resize sticks for
 * the rest of that run. */
#define UI_PANEL_WIDTH 420
/* Shared with main.c's double-click-header-to-shade handling. */
#define UI_PANEL_TITLE "Inspector"

/* Renders the floating, resizable Inspector pane.
 *
 * Single-selection mode (cluster_count <= 1): selected node's path and
 * language (if any), a read-only scrollable view of that file's actual
 * text, a Notes list for it, and an "+ Add note" button (opens the
 * separate Note compose pane, see note_compose.h).
 *
 * Group mode (cluster_count > 1): cluster_paths/cluster_count instead
 * describe a multi-file selection; the panel shows that path list and a
 * Notes list for group notes covering exactly that set of files, plus
 * "+ Add group note". selected_path/selected_language are ignored here.
 *
 * notes is reloaded in place (see notes.h) by any edit/delete the user
 * performs here (adding happens in the Note pane instead); notes_md_path
 * is where those saves go. *out_bounds receives this frame's live window
 * rect (for main.c/labels.c to avoid drawing under it). */
void ui_panel_draw(struct nk_context *ctx, int window_width, int window_height,
                    const char *selected_path, const char *selected_language,
                    const char **cluster_paths, size_t cluster_count,
                    NoteSet *notes, const char *notes_md_path,
                    PanelRect *out_bounds);

void ui_panel_shutdown(void);

#endif
