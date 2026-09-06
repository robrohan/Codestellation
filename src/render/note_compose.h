#ifndef CODEMAP_NOTE_COMPOSE_H
#define CODEMAP_NOTE_COMPOSE_H

#include <stdbool.h>
#include <stddef.h>
#include "panel_rect.h"
#include "../notes/notes.h"

/* Shared with main.c's double-click-header-to-shade handling. */
#define NOTE_COMPOSE_TITLE "Note"

/* Forward-declared rather than including nuklear.h -- see ui_panel.h. */
struct nk_context;

/* Opens the floating "Note" pane in add mode, targeting a single file
 * (path_count == 1) or a fixed set of cluster paths (path_count > 1) --
 * pass the current selection/cluster as-is; the paths are copied in, so
 * the caller's own array doesn't need to stay valid. has_line/line just
 * seed the pane's own "Anchor to line" checkbox/property (usually
 * false/1; the user can still toggle it before saving). */
void note_compose_open_add(const char **paths, size_t path_count, bool has_line, int line);

/* Opens the pane pre-filled to edit an existing note. */
void note_compose_open_edit(const Note *note);

bool note_compose_is_open(void);

/* Draws the pane only while open (no-op, *out_bounds left w==0, otherwise).
 * notes/notes_md_path: the same store the Inspector reads -- mutated in
 * place on Save, see notes.h. */
void note_compose_draw(struct nk_context *ctx, NoteSet *notes, const char *notes_md_path,
                        PanelRect *out_bounds);

#endif
