#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "ui_panel.h"
#include "../common/pathutil.h"
#include "../notes/filehash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cache the last-loaded file so it isn't re-read from disk every frame. */
static char *g_cached_path = NULL;
static char *g_cached_content = NULL;

/* Notes compose-box state. Only one compose box is ever visible at a time
 * (single-file mode or group mode, never both), so one set of statics
 * covers either. Reset whenever the selection context changes -- see
 * reset_compose_if_context_changed -- so a half-written note or an
 * "editing note n3" session never bleeds into a different file/cluster. */
static char g_compose_body[4096] = "";
static nk_bool g_compose_has_line = nk_false;
static int g_compose_line = 1;
static bool g_compose_editing = false;
static char g_compose_editing_id[16] = "";
static char g_pending_delete_id[16] = "";

/* Identifies "what the compose box is currently about" so a selection
 * change can reset it. Single-file mode: the path. Group mode: nothing
 * sane to key on but the path list itself, so just the count plus first
 * path is good enough to notice "the cluster changed". */
static char g_compose_context[512] = "";

static void reset_compose(void) {
    g_compose_body[0] = '\0';
    g_compose_has_line = nk_false;
    g_compose_line = 1;
    g_compose_editing = false;
    g_compose_editing_id[0] = '\0';
    g_pending_delete_id[0] = '\0';
}

static void reset_compose_if_context_changed(const char *context) {
    if (strcmp(g_compose_context, context) != 0) {
        reset_compose();
        strncpy(g_compose_context, context, sizeof(g_compose_context) - 1);
        g_compose_context[sizeof(g_compose_context) - 1] = '\0';
    }
}

static void load_file_if_needed(const char *path) {
    if (path && g_cached_path && strcmp(g_cached_path, path) == 0) return;

    free(g_cached_path);
    free(g_cached_content);
    g_cached_path = NULL;
    g_cached_content = NULL;
    if (!path) return;

    g_cached_path = xstrdup(path);

    FILE *f = fopen(path, "rb");
    if (!f) {
        g_cached_content = xstrdup("(could not read file)");
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        g_cached_content = xstrdup("(could not read file)");
        return;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    g_cached_content = buf;
}

/* One note's summary line + body preview + Edit/Delete. Sets *edit_target
 * / *delete_target (both assumed pre-cleared by the caller) if their
 * button was clicked this frame -- deferred rather than acted on
 * immediately since acting mid-list would invalidate `notes`. */
static void draw_note_row(struct nk_context *ctx, const Note *note,
                           const Note **edit_target, const Note **delete_target) {
    nk_layout_row_dynamic(ctx, 16, 1);
    if (note->path) {
        if (note->has_line) nk_labelf(ctx, NK_TEXT_LEFT, "[%s] line %d \xc2\xb7 %s", note->id, note->line, note->updated);
        else nk_labelf(ctx, NK_TEXT_LEFT, "[%s] whole file \xc2\xb7 %s", note->id, note->updated);
    } else {
        nk_labelf(ctx, NK_TEXT_LEFT, "[%s] group, %zu files \xc2\xb7 %s", note->id, note->group_path_count, note->updated);
    }
    nk_layout_row_dynamic(ctx, 32, 1);
    nk_label_wrap(ctx, note->body);

    nk_layout_row_dynamic(ctx, 20, 2);
    if (nk_button_label(ctx, "Edit")) *edit_target = note;
    bool pending = strcmp(g_pending_delete_id, note->id) == 0;
    if (nk_button_label(ctx, pending ? "Confirm?" : "Delete")) {
        if (pending) {
            *delete_target = note;
        } else {
            /* First click arms this row; clicking a different row's
             * Delete overwrites the pending id, re-arming there instead --
             * effectively "clicking anything else clears/moves it". */
            strncpy(g_pending_delete_id, note->id, sizeof(g_pending_delete_id) - 1);
            g_pending_delete_id[sizeof(g_pending_delete_id) - 1] = '\0';
        }
    }
}

static void begin_edit(const Note *note) {
    strncpy(g_compose_body, note->body ? note->body : "", sizeof(g_compose_body) - 1);
    g_compose_body[sizeof(g_compose_body) - 1] = '\0';
    g_compose_has_line = note->has_line ? nk_true : nk_false;
    g_compose_line = note->has_line ? note->line : 1;
    g_compose_editing = true;
    strncpy(g_compose_editing_id, note->id, sizeof(g_compose_editing_id) - 1);
    g_compose_editing_id[sizeof(g_compose_editing_id) - 1] = '\0';
    g_pending_delete_id[0] = '\0';
}

/* Line-anchor checkbox + line number property + editable body + Save/Add
 * and (only while editing) Cancel. `on_save` does the actual
 * append/update call and is given the current compose fields. */
static void draw_compose_box(struct nk_context *ctx, bool allow_line_toggle,
                              bool *out_save_clicked, bool *out_cancel_clicked) {
    *out_save_clicked = false;
    *out_cancel_clicked = false;

    if (allow_line_toggle) {
        nk_layout_row_dynamic(ctx, 24, 2);
        nk_checkbox_label(ctx, "Anchor to line", &g_compose_has_line);
        if (g_compose_has_line) {
            nk_property_int(ctx, "Line", 1, &g_compose_line, 1000000, 1, 1);
        } else {
            nk_spacing(ctx, 1);
        }
    }

    nk_layout_row_dynamic(ctx, 80, 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_BOX, g_compose_body, sizeof(g_compose_body), nk_filter_default);

    if (g_compose_editing) {
        nk_layout_row_dynamic(ctx, 24, 2);
        *out_save_clicked = nk_button_label(ctx, "Save");
        *out_cancel_clicked = nk_button_label(ctx, "Cancel");
    } else {
        nk_layout_row_dynamic(ctx, 24, 1);
        *out_save_clicked = nk_button_label(ctx, "Add note");
    }
}

static void draw_group_mode(struct nk_context *ctx, int window_height,
                             const char **cluster_paths, size_t cluster_count,
                             NoteSet *notes, const char *notes_md_path) {
    char context[512];
    snprintf(context, sizeof(context), "group:%zu:%s", cluster_count,
             cluster_count > 0 ? cluster_paths[0] : "");
    reset_compose_if_context_changed(context);

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_labelf(ctx, NK_TEXT_LEFT, "%zu files selected", cluster_count);

    nk_layout_row_dynamic(ctx, 100, 1);
    if (nk_group_begin(ctx, "cluster_paths", NK_WINDOW_BORDER)) {
        for (size_t i = 0; i < cluster_count; i++) {
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, cluster_paths[i], NK_TEXT_LEFT);
        }
        nk_group_end(ctx);
    }

    (void)window_height;

    const Note *found[64];
    size_t found_n = notes_find_for_group(notes, cluster_paths, cluster_count, found, 64);

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, "Notes", NK_TEXT_LEFT);

    const Note *edit_target = NULL, *delete_target = NULL;
    nk_layout_row_dynamic(ctx, 130, 1);
    if (nk_group_begin(ctx, "group_notes", NK_WINDOW_BORDER)) {
        for (size_t i = 0; i < found_n; i++) draw_note_row(ctx, found[i], &edit_target, &delete_target);
        nk_group_end(ctx);
    }
    if (edit_target) begin_edit(edit_target);
    if (delete_target) notes_delete_note(notes_md_path, notes, delete_target->id);

    bool save_clicked, cancel_clicked;
    draw_compose_box(ctx, false, &save_clicked, &cancel_clicked);
    if (cancel_clicked) {
        reset_compose();
    } else if (save_clicked) {
        if (g_compose_editing) {
            notes_update_note(notes_md_path, notes, g_compose_editing_id, false, 0, g_compose_body);
        } else {
            notes_append_group_note(notes_md_path, notes, cluster_paths, cluster_count, g_compose_body);
        }
        reset_compose();
    }
}

static void draw_single_mode(struct nk_context *ctx, int window_width, int window_height,
                              const char *selected_path, const char *selected_language,
                              NoteSet *notes, const char *notes_md_path) {
    load_file_if_needed(selected_path);
    reset_compose_if_context_changed(selected_path ? selected_path : "");

    if (!selected_path) {
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label(ctx, "Codestellation", NK_TEXT_LEFT);
        nk_label(ctx, "Right-click a node to select it.", NK_TEXT_LEFT);
        nk_label(ctx, "Left-drag a node to reposition it.", NK_TEXT_LEFT);
        nk_label(ctx, "Ctrl/Cmd+right-click to add to a group.", NK_TEXT_LEFT);
        return;
    }

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, selected_language ? selected_language : "", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 40, 1);
    nk_label_wrap(ctx, selected_path);

    /* Reserve fixed heights for everything below the content viewer (notes
     * label, notes list, compose controls, compose body, compose buttons),
     * same "whatever's left" approach the original single-section layout
     * used (window_height - 110). */
    const float notes_label_h = 20.0f, notes_list_h = 120.0f;
    const float compose_controls_h = 26.0f, compose_body_h = 80.0f, compose_buttons_h = 26.0f;
    const float reserved = 110.0f + notes_label_h + notes_list_h + compose_controls_h +
                            compose_body_h + compose_buttons_h;
    float content_h = (float)window_height - reserved;
    if (content_h < 60.0f) content_h = 60.0f;

    nk_layout_row_dynamic(ctx, content_h, 1);
    size_t len = g_cached_content ? strlen(g_cached_content) : 0;
    nk_edit_string_zero_terminated(ctx, NK_EDIT_BOX | NK_EDIT_READ_ONLY,
                                    g_cached_content ? g_cached_content : "",
                                    (int)(len + 1), nk_filter_default);

    const Note *found[64];
    size_t found_n = notes_find_for_path(notes, selected_path, found, 64);

    nk_layout_row_dynamic(ctx, notes_label_h, 1);
    nk_labelf(ctx, NK_TEXT_LEFT, "Notes (%zu)", found_n);

    const Note *edit_target = NULL, *delete_target = NULL;
    nk_layout_row_dynamic(ctx, notes_list_h, 1);
    if (nk_group_begin(ctx, "file_notes", NK_WINDOW_BORDER)) {
        for (size_t i = 0; i < found_n; i++) draw_note_row(ctx, found[i], &edit_target, &delete_target);
        nk_group_end(ctx);
    }
    if (edit_target) begin_edit(edit_target);
    if (delete_target) notes_delete_note(notes_md_path, notes, delete_target->id);

    (void)window_width;

    bool save_clicked, cancel_clicked;
    draw_compose_box(ctx, true, &save_clicked, &cancel_clicked);
    if (cancel_clicked) {
        reset_compose();
    } else if (save_clicked) {
        if (g_compose_editing) {
            notes_update_note(notes_md_path, notes, g_compose_editing_id,
                               g_compose_has_line, g_compose_line, g_compose_body);
        } else {
            uint64_t h;
            bool has_hash = file_content_hash(selected_path, &h);
            notes_append_file_note(notes_md_path, notes, selected_path, has_hash, h,
                                    g_compose_has_line, g_compose_line, g_compose_body);
        }
        reset_compose();
    }
}

void ui_panel_draw(struct nk_context *ctx, int window_width, int window_height,
                    const char *selected_path, const char *selected_language,
                    const char **cluster_paths, size_t cluster_count,
                    NoteSet *notes, const char *notes_md_path) {
    float w = (float)UI_PANEL_WIDTH;
    /* This window's own scrollbar was previously suppressed
     * (NK_WINDOW_NO_SCROLLBAR) because the content viewer's height was
     * sized to exactly fill the window and nuklear's item spacing/padding
     * pushed the laid-out content a few px past the window's rect, adding
     * a redundant outer scrollbar alongside the edit box's own internal
     * one for the *same* content. Now that the Notes section (list +
     * compose box) sits below the viewer too, the window's total content
     * can genuinely exceed its height for real (more notes than fit, a
     * long note body, a short window) -- that's not redundant, it's the
     * only way to reach controls below the fold, so the scrollbar stays
     * on. */
    if (nk_begin(ctx, "Inspector", nk_rect((float)window_width - w, 0, w, (float)window_height),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        if (cluster_count > 1) {
            draw_group_mode(ctx, window_height, cluster_paths, cluster_count, notes, notes_md_path);
        } else {
            draw_single_mode(ctx, window_width, window_height, selected_path, selected_language,
                              notes, notes_md_path);
        }
    }
    nk_end(ctx);
}

void ui_panel_shutdown(void) {
    free(g_cached_path);
    free(g_cached_content);
    g_cached_path = NULL;
    g_cached_content = NULL;
}
