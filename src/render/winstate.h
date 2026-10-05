#ifndef CODEMAP_WINSTATE_H
#define CODEMAP_WINSTATE_H

#include <stdbool.h>

/* Remembered panel positions: each floating window's position, size and
 * shaded state, keyed on its title and kept in
 * ~/Library/Application Support/Codestellation/windows.txt across runs.
 *
 * Nuklear forgets a window that isn't drawn for a frame (the Inspector
 * while the canvas is up, the canvas editor once closed) and recreates it
 * from the rect nk_begin is handed -- so that rect has to come from here,
 * not a hard-coded default, for a moved panel to stay moved.
 *
 * Forward-declared, not included -- see the NK_INCLUDE_* consistency note
 * in ui_panel.h. */
struct nk_context;

/* The app window's size this frame, so a remembered rect from a bigger
 * screen that would land off this one falls back to the default. */
void winstate_set_screen(int width, int height);

/* nk_begin, but a window being (re)created opens where the user last left
 * it -- shaded too, if it's minimizable and was shaded -- and the given
 * rect is only the default for a window never moved. Changes are saved
 * once the mouse is released. `flags` is nk_flags. */
bool winstate_begin(struct nk_context *ctx, const char *title, float x, float y, float w, float h,
                    unsigned flags);

#endif
