#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "note_compose.h"
#include "../common/pathutil.h"
#include "../notes/filehash.h"
#include <stdlib.h>
#include <string.h>

#define MAX_TARGET_PATHS 64

static bool g_open = false;
static char g_body[4096] = "";
static nk_bool g_has_line = nk_false;
static int g_line = 1;
static bool g_editing = false;
static char g_editing_id[16] = "";

/* Copied in on open (via xstrdup) rather than referencing the caller's
 * arrays directly -- main.c's cluster_paths/selected_path aren't
 * guaranteed to stay valid or unchanged for as long as this pane stays
 * open (e.g. the user can change the 3D selection while composing). */
static char *g_target_paths[MAX_TARGET_PATHS];
static size_t g_target_count = 0;

static void free_targets(void) {
    for (size_t i = 0; i < g_target_count; i++) free(g_target_paths[i]);
    g_target_count = 0;
}

static void reset(void) {
    g_open = false;
    g_body[0] = '\0';
    g_has_line = nk_false;
    g_line = 1;
    g_editing = false;
    g_editing_id[0] = '\0';
    free_targets();
}

void note_compose_open_add(const char **paths, size_t path_count, bool has_line, int line) {
    reset();
    if (path_count > MAX_TARGET_PATHS) path_count = MAX_TARGET_PATHS;
    for (size_t i = 0; i < path_count; i++) g_target_paths[i] = xstrdup(paths[i]);
    g_target_count = path_count;
    g_has_line = has_line ? nk_true : nk_false;
    g_line = line > 0 ? line : 1;
    g_open = true;
}

void note_compose_open_edit(const Note *note) {
    reset();
    strncpy(g_body, note->body ? note->body : "", sizeof(g_body) - 1);
    g_body[sizeof(g_body) - 1] = '\0';
    g_has_line = note->has_line ? nk_true : nk_false;
    g_line = note->has_line ? note->line : 1;
    g_editing = true;
    strncpy(g_editing_id, note->id, sizeof(g_editing_id) - 1);
    g_editing_id[sizeof(g_editing_id) - 1] = '\0';

    if (note->path) {
        g_target_paths[0] = xstrdup(note->path);
        g_target_count = 1;
    } else {
        size_t n = note->group_path_count;
        if (n > MAX_TARGET_PATHS) n = MAX_TARGET_PATHS;
        for (size_t i = 0; i < n; i++) g_target_paths[i] = xstrdup(note->group_paths[i]);
        g_target_count = n;
    }
    g_open = true;
}

bool note_compose_is_open(void) { return g_open; }

/* No word-wrap exists anywhere in Nuklear's edit widget (confirmed by
 * reading its row-layout code directly -- it only ever breaks a row on a
 * literal '\n'). Rather than patching nuklear.h a fourth time to add a
 * true reflowing soft-wrap (the approach an upstream PR attempted and
 * never got working cleanly), this hard-wraps as you type: once per
 * frame, on the *settled* buffer from the previous frame (never
 * mid-keystroke), swap the last space before an overflowing line for a
 * real newline. Same-length substitution + strictly between-frames means
 * it can't desync nuklear's own internal cursor/selection byte offsets --
 * the same "safe to swap content between frames" pattern ui_panel.c
 * already relies on when reloading file content on selection change.
 *
 * Trade-off, deliberate: this writes real newlines into the saved body,
 * so resizing the pane after text is already wrapped won't retroactively
 * reflow it (like a manually-wrapped plain-text email). A single token
 * wider than the pane (a long URL) is left alone rather than force-broken
 * mid-word -- it just overflows, no worse than before this existed. */
static void hard_wrap(const struct nk_user_font *font, char *body, float avail_width) {
    if (!font || avail_width <= 0.0f) return;
    size_t len = strlen(body);
    size_t line_start = 0;
    size_t last_space = (size_t)-1;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || body[i] == '\n') {
            line_start = i + 1;
            last_space = (size_t)-1;
            continue;
        }
        if (body[i] == ' ') last_space = i;
        float w = font->width(font->userdata, font->height, body + line_start, (int)(i - line_start + 1));
        if (w > avail_width && last_space != (size_t)-1 && last_space > line_start) {
            body[last_space] = '\n';
            line_start = last_space + 1;
            last_space = (size_t)-1;
        }
    }
}

void note_compose_draw(struct nk_context *ctx, NoteSet *notes, const char *notes_md_path,
                        PanelRect *out_bounds) {
    out_bounds->x = out_bounds->y = out_bounds->w = out_bounds->h = 0.0f;
    if (!g_open) return;

    if (nk_begin(ctx, NOTE_COMPOSE_TITLE, nk_rect(320, 140, 380, 320),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                 NK_WINDOW_MINIMIZABLE)) {
        struct nk_vec2 size = nk_window_get_size(ctx);
        bool single = (g_target_count == 1);

        if (single) {
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label(ctx, g_target_paths[0], NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 24, 2);
            nk_checkbox_label(ctx, "Anchor to line", &g_has_line);
            if (g_has_line) nk_property_int(ctx, "Line", 1, &g_line, 1000000, 1, 1);
            else nk_spacing(ctx, 1);
        } else {
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_labelf(ctx, NK_TEXT_LEFT, "Group note \xc2\xb7 %zu files", g_target_count);
        }

        hard_wrap(ctx->style.font, g_body, size.x - 24.0f);

        /* 150 (not the raw ~90 the label/checkbox/button rows above and
         * below sum to) is deliberate slack for nuklear's per-row gaps --
         * too little here was pushing total content a few px past the
         * window, triggering this window's own scrollbar for a two-line
         * note with nothing that actually needed to scroll. */
        float body_h = size.y - 150.0f;
        if (body_h < 60.0f) body_h = 60.0f;
        nk_layout_row_dynamic(ctx, body_h, 1);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_BOX, g_body, sizeof(g_body), nk_filter_default);

        nk_layout_row_dynamic(ctx, 26, 2);
        bool save_clicked = nk_button_label(ctx, "Save");
        bool cancel_clicked = nk_button_label(ctx, "Cancel");

        if (save_clicked) {
            if (g_editing) {
                notes_update_note(notes_md_path, notes, g_editing_id, g_has_line, g_line, g_body);
            } else if (single) {
                uint64_t h;
                bool has_hash = file_content_hash(g_target_paths[0], &h);
                notes_append_file_note(notes_md_path, notes, g_target_paths[0], has_hash, h,
                                        g_has_line, g_line, g_body);
            } else if (g_target_count > 1) {
                notes_append_group_note(notes_md_path, notes, (const char **)g_target_paths,
                                         g_target_count, g_body);
            }
            reset();
        } else if (cancel_clicked) {
            reset();
        }

        if (g_open) {
            struct nk_rect b = nk_window_get_bounds(ctx);
            if (nk_window_is_collapsed(ctx, NOTE_COMPOSE_TITLE)) b.h = PANEL_HEADER_HEIGHT;
            out_bounds->x = b.x;
            out_bounds->y = b.y;
            out_bounds->w = b.w;
            out_bounds->h = b.h;
        }
    }
    nk_end(ctx);
}
