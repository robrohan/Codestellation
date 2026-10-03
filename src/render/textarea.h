#ifndef CODEMAP_TEXTAREA_H
#define CODEMAP_TEXTAREA_H

#include <stdbool.h>
#include <stddef.h>

/* The app's one multi-line text box: word-wrapped, scrollable, optionally
 * editable, optionally with a gutter of line numbers and clickable markers.
 * Used for the Inspector's file view and note bodies, the note composer
 * and the canvas box editor. Nuklear's own edit widget can't wrap, which is
 * why this exists. Single-line fields (search, edge labels) still use
 * Nuklear's nk_edit field, which has nothing to wrap.
 *
 * Drawn into the next Nuklear layout slot, like any widget: call
 * nk_layout_row_* for the height you want, then textarea_draw.
 *
 * Text is UTF-8 and only '\n' breaks lines. Tabs draw as four spaces and
 * '\r' draws as nothing, so CRLF files and tab-indented code look right
 * without rewriting the text.
 *
 * Keys, when focused: arrows (Shift extends), word left/right, Home/End
 * (line start/end), PageUp/PageDown, Ctrl/Cmd+A/C/X/V/Z (Shift+Z or R to
 * redo), Enter, Tab, Backspace, Delete, with key repeat. Mouse: click,
 * drag-select, double-click a word, wheel, scrollbar. Read-only areas keep
 * selection, copy and navigation. */

struct nk_context;
struct nk_user_font;

enum {
    TEXTAREA_EDITABLE = 1 << 0,
    TEXTAREA_GUTTER   = 1 << 1, /* line numbers + a marker column */
    TEXTAREA_MONO     = 1 << 2, /* monospace font (source code) */
};

/* A gutter icon on a (1-based) line; `id` comes back when it's clicked. */
typedef struct {
    int line;
    int id;
} TextAreaMarker;

typedef struct {
    bool changed;          /* the text was edited this frame */
    bool focused;          /* has keyboard focus -- gate app shortcuts on this */
    int  gutter_click_line; /* line number clicked in the gutter, 0 if none */
    int  marker_click_id;   /* id of a clicked marker, -1 if none */
} TextAreaResult;

typedef struct {
    size_t start, end; /* byte range of this visual row (no '\n') */
    int    line;       /* 1-based logical line */
} TextAreaRow;

typedef struct {
    char  *text;
    size_t cursor;
} TextAreaSnapshot;

typedef struct {
    unsigned flags;

    char  *text; /* NUL-terminated */
    size_t len, cap;
    int    line_count;

    size_t cursor, anchor; /* byte offsets; the selection is between them */
    bool   cursor_placed;  /* the user has clicked/moved since set_text */
    float  scroll_y;
    float  pref_x;         /* column kept while moving up/down */
    bool   pref_x_valid;
    bool   focused, want_focus;
    bool   dragging_text, dragging_bar;
    float  bar_grab;
    bool   scroll_to_cursor;

    /* Wrapped rows, rebuilt when the text, width or font changes. */
    TextAreaRow *rows;
    size_t row_count, row_cap;
    float  layout_width;
    const struct nk_user_font *layout_font;
    bool   layout_dirty;

    /* Undo: snapshots of the whole text (these texts are small; a large
     * read-only file never records any). */
    TextAreaSnapshot *undo, *redo;
    size_t undo_count, undo_cap, redo_count, redo_cap;
    int    last_edit_kind;

    int   repeat_key;
    float repeat_wait;
} TextArea;

void textarea_init(TextArea *ta, unsigned flags);
void textarea_free(TextArea *ta);

/* Replaces the text and resets cursor, scroll and undo. */
void textarea_set_text(TextArea *ta, const char *text);
const char *textarea_text(const TextArea *ta);

/* Gives it keyboard focus on its next draw (e.g. when an editor opens);
 * blur takes it away now (e.g. when its editor closes). */
void textarea_focus(TextArea *ta);
void textarea_blur(TextArea *ta);

/* 1-based line the cursor is on, or 0 if the user hasn't placed it since
 * the last set_text. */
int textarea_cursor_line(const TextArea *ta);

void textarea_draw(struct nk_context *ctx, TextArea *ta, const TextAreaMarker *markers, int marker_count,
                   TextAreaResult *out);

/* Stateless, read-only, unscrolled wrapped text: for short text in lists
 * (note bodies, paths). textarea_label_height says how tall a layout row
 * the text needs at `width`; textarea_label draws it into the next slot. */
float textarea_label_height(struct nk_context *ctx, const char *text, float width);
void  textarea_label(struct nk_context *ctx, const char *text);

#endif
