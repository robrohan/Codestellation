#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "labels.h"
#include "picking.h"
#include <string.h>

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *base = path;
    if (slash && (!bslash || slash > bslash)) base = slash + 1;
    else if (bslash) base = bslash + 1;
    return base;
}

void labels_draw(struct nk_context *ctx, int window_width, int window_height,
                  PanelRect inspector_bounds, PanelRect note_bounds,
                  const float *view_proj, const Vec3 *positions, const Graph *g, const NoteSet *notes) {
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background,
                              nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));

    /* NK_WINDOW_BACKGROUND matters here beyond just draw order: without
     * it, this full-screen window (created every frame, after the
     * Inspector) claims ctx->end (nuklear's "topmost/focused" window)
     * and never releases it -- being NK_WINDOW_NO_INPUT, it's skipped
     * from the focus-stealing logic that would otherwise let Inspector
     * reclaim the top slot on click. Any window that isn't ctx->end gets
     * flagged NK_WINDOW_ROM, which is what silently killed all input
     * (scroll, click-to-position, drag-select) to the Inspector's text
     * box. NK_WINDOW_BACKGROUND exempts this window from that fight
     * entirely and draws it behind other windows instead of in front. */
    if (nk_begin(ctx, "##labels", nk_rect(0, 0, (float)window_width, (float)window_height),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND)) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        const struct nk_user_font *font = ctx->style.font;

        for (size_t i = 0; i < g->node_count; i++) {
            float sx, sy;
            if (!project_to_screen(view_proj, positions[i], window_width, window_height, &sx, &sy)) continue;
            if (sx < 0 || sy < 0 || sy > (float)window_height) continue;
            if (panel_rect_contains(inspector_bounds, sx, sy) || panel_rect_contains(note_bounds, sx, sy)) continue;

            const char *name = basename_of(g->nodes[i].path);
            int len = (int)strlen(name);
            float text_w = font ? font->width(font->userdata, font->height, name, len) : (float)(len * 6);

            struct nk_rect r = nk_rect(sx - text_w * 0.5f, sy - 20.0f, text_w + 4.0f, 16.0f);
            nk_draw_text(canvas, r, name, len, font, nk_rgba(0, 0, 0, 0), nk_rgb(225, 225, 225));

            const Note *found[1];
            if (notes && notes_find_for_path(notes, g->nodes[i].path, found, 1) > 0) {
                float dot_x = sx + text_w * 0.5f + 6.0f;
                nk_fill_circle(canvas, nk_rect(dot_x - 3.0f, sy - 20.0f + 2.0f, 6.0f, 6.0f), nk_rgb(230, 165, 40));
            }
        }
    }
    nk_end(ctx);

    nk_style_pop_vec2(ctx);
    nk_style_pop_style_item(ctx);
}
