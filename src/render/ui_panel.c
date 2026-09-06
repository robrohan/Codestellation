#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "ui_panel.h"
#include "note_compose.h"
#include "../common/pathutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cache the last-loaded file so it isn't re-read from disk every frame. */
static char *g_cached_path = NULL;
static char *g_cached_content = NULL;

/* Two-step delete confirmation for the notes list -- see draw_note_row.
 * Reset whenever the selection context changes so an armed "Confirm?" on
 * one file's note doesn't silently carry over and land on the wrong note
 * if the user switches selection and clicks Delete again in the same
 * screen position. */
static char g_pending_delete_id[16] = "";
static char g_delete_arm_context[512] = "";

static void reset_delete_arm_if_context_changed(const char *context) {
    if (strcmp(g_delete_arm_context, context) != 0) {
        g_pending_delete_id[0] = '\0';
        strncpy(g_delete_arm_context, context, sizeof(g_delete_arm_context) - 1);
        g_delete_arm_context[sizeof(g_delete_arm_context) - 1] = '\0';
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

/* One note's summary line + body preview + Edit/Delete. Edit opens the
 * separate Note compose pane directly (safe mid-list, it only copies the
 * note's fields into that pane's own state). Delete is deferred via
 * *delete_target instead -- acting on it immediately would call
 * notes_delete_note, which frees/reloads the very `notes->notes` array
 * this loop is iterating pointers into. */
static void draw_note_row(struct nk_context *ctx, const Note *note, const Note **delete_target) {
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
    if (nk_button_label(ctx, "Edit")) note_compose_open_edit(note);
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

static void draw_group_mode(struct nk_context *ctx, const char **cluster_paths, size_t cluster_count,
                             NoteSet *notes, const char *notes_md_path) {
    char context[512];
    snprintf(context, sizeof(context), "group:%zu:%s", cluster_count,
             cluster_count > 0 ? cluster_paths[0] : "");
    reset_delete_arm_if_context_changed(context);

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

    const Note *found[64];
    size_t found_n = notes_find_for_group(notes, cluster_paths, cluster_count, found, 64);

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_labelf(ctx, NK_TEXT_LEFT, "Notes (%zu)", found_n);

    const Note *delete_target = NULL;
    nk_layout_row_dynamic(ctx, 150, 1);
    if (nk_group_begin(ctx, "group_notes", NK_WINDOW_BORDER)) {
        for (size_t i = 0; i < found_n; i++) draw_note_row(ctx, found[i], &delete_target);
        nk_group_end(ctx);
    }
    if (delete_target) {
        notes_delete_note(notes_md_path, notes, delete_target->id);
        g_pending_delete_id[0] = '\0';
    }

    nk_layout_row_dynamic(ctx, 26, 1);
    if (nk_button_label(ctx, "+ Add group note")) {
        note_compose_open_add(cluster_paths, cluster_count, false, 1);
    }
}

static void draw_single_mode(struct nk_context *ctx, float panel_h,
                              const char *selected_path, const char *selected_language,
                              NoteSet *notes, const char *notes_md_path) {
    load_file_if_needed(selected_path);
    reset_delete_arm_if_context_changed(selected_path ? selected_path : "");

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

    /* Reserve fixed heights for everything below the content viewer
     * (notes label, notes list, the add-note button), same "whatever's
     * left" approach as before -- just a much smaller reservation now
     * that the compose box lives in its own pane instead of here. The
     * 140 base (rather than the raw ~110 the fixed rows above actually
     * sum to) is deliberate slack: nuklear adds a small gap between each
     * stacked row, and with this many rows that adds up to more than it
     * looks like -- too little slack here was pushing total content a
     * few px past the window, triggering this window's own scrollbar for
     * completely ordinary-sized content (nothing actually needing to
     * scroll). Better to keep a bit of daylight than fight that again. */
    const float notes_label_h = 20.0f, notes_list_h = 150.0f, add_button_h = 30.0f;
    const float reserved = 140.0f + notes_label_h + notes_list_h + add_button_h;
    float content_h = panel_h - reserved;
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

    const Note *delete_target = NULL;
    nk_layout_row_dynamic(ctx, notes_list_h, 1);
    if (nk_group_begin(ctx, "file_notes", NK_WINDOW_BORDER)) {
        for (size_t i = 0; i < found_n; i++) draw_note_row(ctx, found[i], &delete_target);
        nk_group_end(ctx);
    }
    if (delete_target) {
        notes_delete_note(notes_md_path, notes, delete_target->id);
        g_pending_delete_id[0] = '\0';
    }

    nk_layout_row_dynamic(ctx, add_button_h, 1);
    if (nk_button_label(ctx, "+ Add note")) {
        const char *p[1] = { selected_path };
        note_compose_open_add(p, 1, false, 1);
    }
}

void ui_panel_draw(struct nk_context *ctx, int window_width, int window_height,
                    const char *selected_path, const char *selected_language,
                    const char **cluster_paths, size_t cluster_count,
                    NoteSet *notes, const char *notes_md_path,
                    PanelRect *out_bounds) {
    float w = (float)UI_PANEL_WIDTH;
    /* NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE: this rect is only honored the
     * frame the window is first created (nuklear owns its position/size
     * from then on, updated by the user's own drag/resize) -- see the
     * plan notes on nk_begin_titled's behavior. So this is just a sane
     * first-launch default, not a per-frame pin anymore. */
    if (nk_begin(ctx, UI_PANEL_TITLE, nk_rect((float)window_width - w, 0, w, (float)window_height),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                 NK_WINDOW_MINIMIZABLE)) {
        struct nk_rect b = nk_window_get_bounds(ctx);
        /* nk_window_get_bounds keeps reporting the *restored* size even
         * while shaded/collapsed (double-click the header -- see main.c) --
         * report just the header strip instead, so 3D interaction and
         * label drawing correctly get the rest of the screen back. */
        if (nk_window_is_collapsed(ctx, UI_PANEL_TITLE)) b.h = PANEL_HEADER_HEIGHT;
        out_bounds->x = b.x;
        out_bounds->y = b.y;
        out_bounds->w = b.w;
        out_bounds->h = b.h;

        if (cluster_count > 1) {
            draw_group_mode(ctx, cluster_paths, cluster_count, notes, notes_md_path);
        } else {
            struct nk_vec2 size = nk_window_get_size(ctx);
            draw_single_mode(ctx, size.y, selected_path, selected_language, notes, notes_md_path);
        }
    } else {
        out_bounds->x = out_bounds->y = out_bounds->w = out_bounds->h = 0.0f;
    }
    nk_end(ctx);
}

void ui_panel_shutdown(void) {
    free(g_cached_path);
    free(g_cached_content);
    g_cached_path = NULL;
    g_cached_content = NULL;
}
