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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cache the last-loaded file so it isn't re-read from disk every frame. */
static char *g_cached_path = NULL;
static char *g_cached_content = NULL;

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

void ui_panel_draw(struct nk_context *ctx, int window_width, int window_height,
                    const char *selected_path, const char *selected_language) {
    load_file_if_needed(selected_path);

    float w = (float)UI_PANEL_WIDTH;
    if (nk_begin(ctx, "Inspector", nk_rect((float)window_width - w, 0, w, (float)window_height),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        if (selected_path) {
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label(ctx, selected_language ? selected_language : "", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 40, 1);
            nk_label_wrap(ctx, selected_path);

            nk_layout_row_dynamic(ctx, (float)window_height - 110.0f, 1);
            size_t len = g_cached_content ? strlen(g_cached_content) : 0;
            nk_edit_string_zero_terminated(ctx, NK_EDIT_BOX | NK_EDIT_READ_ONLY,
                                            g_cached_content ? g_cached_content : "",
                                            (int)(len + 1), nk_filter_default);
        } else {
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label(ctx, "Codestellation", NK_TEXT_LEFT);
            nk_label(ctx, "Right-click a node to select it.", NK_TEXT_LEFT);
            nk_label(ctx, "Left-drag a node to reposition it.", NK_TEXT_LEFT);
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
