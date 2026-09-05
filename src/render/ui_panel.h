#ifndef CODEMAP_UI_PANEL_H
#define CODEMAP_UI_PANEL_H

/* Forward-declared rather than including nuklear.h here: Nuklear's
 * NK_INCLUDE_* feature macros affect struct layout, so every TU that
 * sees the real definition must define the same set consistently --
 * simplest to confine that to ui_panel.c and main.c and keep this header
 * opaque. */
struct nk_context;

#define UI_PANEL_WIDTH 420

/* Renders the fixed-width right-hand inspector: selected node's path and
 * language (if any) plus a read-only scrollable view of that file's
 * actual text, re-read from disk whenever the selection changes. */
void ui_panel_draw(struct nk_context *ctx, int window_width, int window_height,
                    const char *selected_path, const char *selected_language);

void ui_panel_shutdown(void);

#endif
