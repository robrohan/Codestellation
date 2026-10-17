#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "labels.h"
#include "theme.h"
#include "picking.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *base = path;
    if (slash && (!bslash || slash > bslash)) base = slash + 1;
    else if (bslash) base = bslash + 1;
    return base;
}

/* One on-screen node that's a label candidate. `forced` labels (selected
 * or noted) always draw; the rest compete for LABELS_MAX_VISIBLE slots by
 * `depth` (clip-space w -- smaller is nearer the camera). */
typedef struct {
    int   idx;
    float sx, sy;
    float depth;
    int   forced;
    int   has_note;
} LabelCand;

/* Indices of the (up to) LABELS_TOP_MARKED nodes with the highest
 * positive score, highest first; returns how many. */
static int top_nodes(const double *score, size_t n, int *out) {
    int count = 0;
    for (size_t i = 0; i < n; i++) {
        if (score[i] <= 0.0) continue;
        int at = count < LABELS_TOP_MARKED ? count : LABELS_TOP_MARKED;
        while (at > 0 && score[out[at - 1]] < score[i]) at--;
        if (at >= LABELS_TOP_MARKED) continue;
        int last = count < LABELS_TOP_MARKED ? count : LABELS_TOP_MARKED - 1;
        for (int k = last; k > at; k--) out[k] = out[k - 1];
        out[at] = (int)i;
        if (count < LABELS_TOP_MARKED) count++;
    }
    return count;
}

/* Screen position of node i, or false if it's off screen or under a pane. */
static bool marker_pos(const float *view_proj, Vec3 p, int w, int h, PanelRect a, PanelRect b, PanelRect c,
                       float *sx, float *sy) {
    if (!project_to_screen(view_proj, p, w, h, sx, sy)) return false;
    if (*sx < 0 || *sx > (float)w || *sy < 0 || *sy > (float)h) return false;
    return !panel_rect_contains(a, *sx, *sy) && !panel_rect_contains(b, *sx, *sy) &&
           !panel_rect_contains(c, *sx, *sy);
}

static void draw_markers(struct nk_command_buffer *canvas, const struct nk_user_font *font, int window_width,
                         int window_height, PanelRect inspector_bounds, PanelRect note_bounds,
                         PanelRect properties_bounds, const float *view_proj, const Vec3 *positions,
                         const float *sizes, float point_scale, const Graph *g, const CostParams *cost) {
    size_t n = g->node_count;
    if (n == 0) return;
    double *score = (double *)malloc(n * sizeof(double));
    int top[LABELS_TOP_MARKED];
    float sx, sy;

    /* Most complex: an outline just outside the node's square. */
    for (size_t i = 0; i < n; i++) score[i] = g->nodes[i].stats.has_stats ? g->nodes[i].stats.complexity : 0;
    int count = top_nodes(score, n, top);
    for (int k = 0; k < count; k++) {
        int i = top[k];
        if (!marker_pos(view_proj, positions[i], window_width, window_height, inspector_bounds, note_bounds,
                        properties_bounds, &sx, &sy)) continue;
        float half = sizes[i] / (point_scale > 0.0f ? point_scale : 1.0f) * 0.5f + 3.0f;
        nk_stroke_rect(canvas, nk_rect(sx - half, sy - half, half * 2.0f, half * 2.0f), 0.0f, 2.0f,
                       theme_nk(g_theme.complexity_marker));
    }

    /* Most valuable: the currency sign to the node's right. */
    for (size_t i = 0; i < n; i++) {
        CodeTotals t = { 0 };
        code_totals_add(&t, &g->nodes[i].stats, g->nodes[i].language);
        score[i] = t.sloc > 0 ? cost_value(&t, cost) : 0.0;
    }
    count = top_nodes(score, n, top);
    const char *sign = cost->currency[0] ? cost->currency : "$";
    int len = (int)strlen(sign);
    while (len > 1 && sign[len - 1] == ' ') len--;
    float text_w = font ? font->width(font->userdata, font->height, sign, len) : (float)(len * 7);
    float text_h = font ? font->height : 14.0f;
    for (int k = 0; k < count; k++) {
        int i = top[k];
        if (!marker_pos(view_proj, positions[i], window_width, window_height, inspector_bounds, note_bounds,
                        properties_bounds, &sx, &sy)) continue;
        float half = sizes[i] / (point_scale > 0.0f ? point_scale : 1.0f) * 0.5f;
        struct nk_rect r = nk_rect(sx + half + 6.0f, sy - text_h * 0.5f, text_w + 4.0f, text_h);
        nk_draw_text(canvas, nk_rect(r.x + 1.0f, r.y + 1.0f, r.w, r.h), sign, len, font, nk_rgba(0, 0, 0, 0),
                     theme_nk(g_theme.label_shadow));
        nk_draw_text(canvas, r, sign, len, font, nk_rgba(0, 0, 0, 0), theme_nk(g_theme.cost_marker));
    }
    free(score);
}

static int label_cmp(const void *a, const void *b) {
    const LabelCand *la = (const LabelCand *)a, *lb = (const LabelCand *)b;
    if (la->forced != lb->forced) return lb->forced - la->forced; /* forced first */
    if (la->depth < lb->depth) return -1;
    if (la->depth > lb->depth) return 1;
    return 0;
}

void labels_draw(struct nk_context *ctx, int window_width, int window_height,
                  PanelRect inspector_bounds, PanelRect note_bounds, PanelRect properties_bounds,
                  const float *view_proj, const Vec3 *positions, const float *sizes, float point_scale,
                  const Graph *g, const NoteSet *notes, const CostParams *cost, int selected) {
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
        size_t n = g->node_count;

        LabelCand *cand = (LabelCand *)malloc(n * sizeof(LabelCand));
        size_t cc = 0;
        for (size_t i = 0; i < n; i++) {
            float sx, sy;
            if (!project_to_screen(view_proj, positions[i], window_width, window_height, &sx, &sy)) continue;
            if (sx < 0 || sx > (float)window_width || sy < 0 || sy > (float)window_height) continue;
            if (panel_rect_contains(inspector_bounds, sx, sy) || panel_rect_contains(note_bounds, sx, sy) ||
                panel_rect_contains(properties_bounds, sx, sy)) continue;

            const Note *found[1];
            int has_note = notes && notes_find_for_path(notes, g->nodes[i].path, found, 1) > 0;

            Vec3 p = positions[i];
            float w = view_proj[3] * p.x + view_proj[7] * p.y + view_proj[11] * p.z + view_proj[15];

            cand[cc].idx = (int)i;
            cand[cc].sx = sx;
            cand[cc].sy = sy;
            cand[cc].depth = w;
            cand[cc].has_note = has_note;
            cand[cc].forced = ((int)i == selected) || has_note;
            cc++;
        }

        qsort(cand, cc, sizeof(LabelCand), label_cmp);

        int drawn_unforced = 0;
        for (size_t c = 0; c < cc; c++) {
            const LabelCand *lc = &cand[c];
            if (!lc->forced) {
                if (drawn_unforced >= LABELS_MAX_VISIBLE) break; /* rest are farther still */
                drawn_unforced++;
            }

            const char *name = basename_of(g->nodes[lc->idx].path);
            int len = (int)strlen(name);
            float text_w = font ? font->width(font->userdata, font->height, name, len) : (float)(len * 6);

            struct nk_rect r = nk_rect(lc->sx - text_w * 0.5f, lc->sy - 20.0f, text_w + 4.0f, 16.0f);
            /* A 1px drop shadow keeps labels legible where they cross
             * edges and bright nodes. */
            struct nk_rect shadow = nk_rect(r.x + 1.0f, r.y + 1.0f, r.w, r.h);
            nk_draw_text(canvas, shadow, name, len, font, nk_rgba(0, 0, 0, 0), theme_nk(g_theme.label_shadow));
            nk_draw_text(canvas, r, name, len, font, nk_rgba(0, 0, 0, 0), theme_nk(g_theme.label_text));

            if (lc->has_note) {
                float dot_x = lc->sx + text_w * 0.5f + 6.0f;
                nk_fill_circle(canvas, nk_rect(dot_x - 3.0f, lc->sy - 20.0f + 2.0f, 6.0f, 6.0f), theme_nk(g_theme.note_dot));
            }
        }
        free(cand);

        draw_markers(canvas, font, window_width, window_height, inspector_bounds, note_bounds, properties_bounds,
                     view_proj, positions, sizes, point_scale, g, cost);
    }
    nk_end(ctx);

    nk_style_pop_vec2(ctx);
    nk_style_pop_style_item(ctx);
}
