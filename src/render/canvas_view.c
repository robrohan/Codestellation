#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "canvas_view.h"
#include "md_render.h"
#include "fonts.h"
#include "theme.h"
#include <math.h>
#include <stdio.h>

#define MIN_ZOOM 0.1f
#define MAX_ZOOM 4.0f
#define BOX_PAD 12.0f
#define GRID_MINOR 20.0f
#define GRID_MAJOR 100.0f
/* Below this on-screen body size, full markdown is unreadable anyway --
 * show just the title. */
#define MIN_BODY_PX 7.0f

typedef struct {
    float x, y, w, h;   /* world units */
    int color;          /* 0 = none, 1..6 = JSON Canvas preset */
    const char *text;
} SpikeBox;

typedef struct {
    int from, to;
    const char *label;
} SpikeEdge;

static SpikeBox g_boxes[] = {
    { 0, 0, 260, 170, 5,
      "# EU-West\n"
      "Primary production region.\n"
      "\n"
      "- 2 API servers\n"
      "- 1 database, [[db-main]]\n"
      "- VPC `10.0.0.0/16`" },
    { 360, -90, 290, 180, 4,
      "## api-01\n"
      "**host** `api-01.eu.internal`\n"
      "**ip** `10.0.3.12`\n"
      "\n"
      "Serves the public REST API. Talks to [[db-main]] over TLS." },
    { 360, 140, 290, 180, 4,
      "## api-02\n"
      "**host** `api-02.eu.internal`\n"
      "**ip** `10.0.3.13`\n"
      "\n"
      "Hot standby for *api-01*; takes traffic when the health check fails." },
    { 760, 40, 270, 170, 6,
      "## db-main\n"
      "Postgres 15, primary.\n"
      "\n"
      "- `10.0.9.4:5432`\n"
      "- nightly backup to *S3*\n"
      "- replica in [[US-East]]" },
    { 760, -200, 270, 150, 2,
      "## Source\n"
      "[[../services/api]]\n"
      "[[../services/worker]]\n"
      "Double-clicking this box would open both folders in the 3D view." },
    { 0, 240, 300, 300, 0,
      "# Notes\n"
      "This box has a longer paragraph so wrapping, line spacing and small "
      "text can be judged at different zoom levels. Zoom out until only the "
      "title remains, then back in.\n"
      "\n"
      "### Checklist\n"
      "- **bold**, *italic* and `code` inline\n"
      "- a snake_case_name stays intact\n"
      "- curly \xE2\x80\x9Cquotes\xE2\x80\x9D and an em dash \xE2\x80\x94 like this\n"
      "- arrows \xE2\x86\x92 and an ellipsis\xE2\x80\xA6" },
};
#define BOX_COUNT ((int)(sizeof(g_boxes) / sizeof(g_boxes[0])))

static const SpikeEdge g_edges[] = {
    { 0, 1, NULL },
    { 0, 2, NULL },
    { 1, 3, "Postgres 5432" },
    { 2, 3, "Postgres 5432" },
    { 4, 1, "deploys" },
};
#define EDGE_COUNT ((int)(sizeof(g_edges) / sizeof(g_edges[0])))

typedef enum { MODE_NONE, MODE_PAN, MODE_DRAG } Mode;

static bool g_initialized = false;
static float g_zoom = 1.0f;
static float g_ox = 0.0f, g_oy = 0.0f;   /* screen position of world origin */
static int g_selected = -1;
static int g_drag_box = -1;
static Mode g_mode = MODE_NONE;
static float g_last_x, g_last_y;
static bool g_prev_left, g_prev_right, g_prev_middle;

static float to_sx(float wx) { return wx * g_zoom + g_ox; }
static float to_sy(float wy) { return wy * g_zoom + g_oy; }

static int hit_box(float sx, float sy) {
    /* Topmost first: later boxes draw over earlier ones. */
    for (int i = BOX_COUNT - 1; i >= 0; i--) {
        const SpikeBox *b = &g_boxes[i];
        float x0 = to_sx(b->x), y0 = to_sy(b->y);
        if (sx >= x0 && sx <= x0 + b->w * g_zoom && sy >= y0 && sy <= y0 + b->h * g_zoom) return i;
    }
    return -1;
}

void canvas_view_update(const CanvasInput *in, int width, int height) {
    if (!g_initialized) {
        g_ox = (float)width * 0.5f - 515.0f;
        g_oy = (float)height * 0.5f - 170.0f;
        g_initialized = true;
    }

    if (in->scroll != 0.0f && !in->over_panel) {
        float z = g_zoom * powf(1.1f, in->scroll);
        if (z < MIN_ZOOM) z = MIN_ZOOM;
        if (z > MAX_ZOOM) z = MAX_ZOOM;
        /* Keep the world point under the cursor fixed. */
        float wx = (in->mx - g_ox) / g_zoom, wy = (in->my - g_oy) / g_zoom;
        g_zoom = z;
        g_ox = in->mx - wx * g_zoom;
        g_oy = in->my - wy * g_zoom;
    }

    bool left_edge = in->left && !g_prev_left;
    bool pan_edge = (in->right && !g_prev_right) || (in->middle && !g_prev_middle);

    if (g_mode == MODE_NONE && !in->over_panel) {
        if (left_edge) {
            int hit = hit_box(in->mx, in->my);
            g_selected = hit;
            if (hit >= 0) {
                g_mode = MODE_DRAG;
                g_drag_box = hit;
            } else {
                g_mode = MODE_PAN;
            }
        } else if (pan_edge) {
            g_mode = MODE_PAN;
        }
        g_last_x = in->mx;
        g_last_y = in->my;
    } else if (g_mode != MODE_NONE) {
        if (!in->left && !in->right && !in->middle) {
            g_mode = MODE_NONE;
            g_drag_box = -1;
        } else {
            float dx = in->mx - g_last_x, dy = in->my - g_last_y;
            if (g_mode == MODE_PAN) {
                g_ox += dx;
                g_oy += dy;
            } else if (g_drag_box >= 0) {
                g_boxes[g_drag_box].x += dx / g_zoom;
                g_boxes[g_drag_box].y += dy / g_zoom;
            }
            g_last_x = in->mx;
            g_last_y = in->my;
        }
    }

    g_prev_left = in->left;
    g_prev_right = in->right;
    g_prev_middle = in->middle;
}

static void draw_grid(struct nk_command_buffer *c, int width, int height) {
    const float spacing[2] = { GRID_MINOR, GRID_MAJOR };
    const ThemeColor color[2] = { g_theme.grid_minor, g_theme.grid_major };
    for (int g = 0; g < 2; g++) {
        float step = spacing[g] * g_zoom;
        if (step < 8.0f) continue; /* too dense to be anything but noise */
        struct nk_color col = theme_nk(color[g]);
        float x = fmodf(g_ox, step);
        if (x < 0) x += step;
        for (; x < (float)width; x += step) nk_stroke_line(c, x, 0, x, (float)height, 1.0f, col);
        float y = fmodf(g_oy, step);
        if (y < 0) y += step;
        for (; y < (float)height; y += step) nk_stroke_line(c, 0, y, (float)width, y, 1.0f, col);
    }
}

/* Anchor on the side of box b facing (tx, ty), plus that side's outward
 * normal. */
static void edge_anchor(const SpikeBox *b, float tx, float ty, float *ax, float *ay, float *nx, float *ny) {
    float cx = to_sx(b->x + b->w * 0.5f), cy = to_sy(b->y + b->h * 0.5f);
    float hw = b->w * 0.5f * g_zoom, hh = b->h * 0.5f * g_zoom;
    float dx = tx - cx, dy = ty - cy;
    /* Compare against the box's own aspect so wide boxes prefer their
     * left/right sides. */
    if (fabsf(dx) * hh > fabsf(dy) * hw) {
        *nx = dx > 0 ? 1.0f : -1.0f;
        *ny = 0.0f;
        *ax = cx + *nx * hw;
        *ay = cy;
    } else {
        *nx = 0.0f;
        *ny = dy > 0 ? 1.0f : -1.0f;
        *ax = cx;
        *ay = cy + *ny * hh;
    }
}

static void draw_edge(struct nk_command_buffer *c, const SpikeEdge *e) {
    const SpikeBox *a = &g_boxes[e->from], *b = &g_boxes[e->to];
    float acx = to_sx(a->x + a->w * 0.5f), acy = to_sy(a->y + a->h * 0.5f);
    float bcx = to_sx(b->x + b->w * 0.5f), bcy = to_sy(b->y + b->h * 0.5f);
    float ax, ay, anx, any, bx, by, bnx, bny;
    edge_anchor(a, bcx, bcy, &ax, &ay, &anx, &any);
    edge_anchor(b, acx, acy, &bx, &by, &bnx, &bny);

    float dist = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    float off = fmaxf(40.0f * g_zoom, dist * 0.35f);
    float c1x = ax + anx * off, c1y = ay + any * off;
    float c2x = bx + bnx * off, c2y = by + bny * off;

    struct nk_color col = theme_nk(g_theme.canvas_edge);
    float thick = fmaxf(1.0f, 1.5f * fminf(g_zoom, 1.5f));
    nk_stroke_curve(c, ax, ay, c1x, c1y, c2x, c2y, bx, by, thick, col);

    /* Arrowhead at the target, pointing along the curve's final tangent. */
    float dx = bx - c2x, dy = by - c2y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.001f) {
        dx /= len;
        dy /= len;
        float s = fminf(fmaxf(10.0f * g_zoom, 5.0f), 16.0f);
        nk_fill_triangle(c, bx, by,
                         bx - dx * s - dy * s * 0.5f, by - dy * s + dx * s * 0.5f,
                         bx - dx * s + dy * s * 0.5f, by - dy * s - dx * s * 0.5f, col);
    }

    if (e->label) {
        float px = 12.5f * g_zoom;
        if (px < MIN_BODY_PX) return;
        /* Cubic bezier midpoint (t = 0.5). */
        float mx = 0.125f * ax + 0.375f * c1x + 0.375f * c2x + 0.125f * bx;
        float my = 0.125f * ay + 0.375f * c1y + 0.375f * c2y + 0.125f * by;
        const struct nk_user_font *f = fonts_sized(FONT_REGULAR, px);
        int n = 0;
        while (e->label[n]) n++;
        float w = f->width(f->userdata, f->height, e->label, n);
        float pad = 4.0f * g_zoom;
        struct nk_rect r = nk_rect(mx - w * 0.5f - pad, my - f->height * 0.5f - pad * 0.5f, w + pad * 2.0f,
                                   f->height + pad);
        nk_fill_rect(c, r, 3.0f * g_zoom, theme_nk(g_theme.canvas_background));
        nk_draw_text(c, nk_rect(mx - w * 0.5f, my - f->height * 0.5f, w + 2.0f, f->height), e->label, n, f,
                     nk_rgba(0, 0, 0, 0), theme_nk(g_theme.canvas_edge_label));
    }
}

static void draw_box(struct nk_command_buffer *c, int i, struct nk_rect window_clip) {
    const SpikeBox *b = &g_boxes[i];
    struct nk_rect r = nk_rect(to_sx(b->x), to_sy(b->y), b->w * g_zoom, b->h * g_zoom);
    if (r.x > window_clip.x + window_clip.w || r.y > window_clip.y + window_clip.h ||
        r.x + r.w < window_clip.x || r.y + r.h < window_clip.y) return;

    float rounding = 6.0f * fminf(g_zoom, 1.5f);
    ThemeColor border = b->color >= 1 && b->color <= 6 ? g_theme.box_preset[b->color - 1] : g_theme.box_border;
    nk_fill_rect(c, r, rounding, theme_nk(g_theme.box_fill));
    nk_stroke_rect(c, r, rounding, 2.0f, theme_nk(border));
    if (i == g_selected) {
        struct nk_rect o = nk_rect(r.x - 3.0f, r.y - 3.0f, r.w + 6.0f, r.h + 6.0f);
        nk_stroke_rect(c, o, rounding + 3.0f, 1.5f, theme_nk(g_theme.box_border_selected));
    }

    float pad = BOX_PAD * g_zoom;
    struct nk_rect inner = nk_rect(r.x + pad, r.y + pad, r.w - pad * 2.0f, r.h - pad * 2.0f);
    if (inner.w <= 4.0f || inner.h <= 4.0f) return;

    /* Scissor to the box (intersected with the window) so wrapped text
     * never spills out, then restore. */
    float x0 = fmaxf(r.x, window_clip.x), y0 = fmaxf(r.y, window_clip.y);
    float x1 = fminf(r.x + r.w, window_clip.x + window_clip.w);
    float y1 = fminf(r.y + r.h, window_clip.y + window_clip.h);
    nk_push_scissor(c, nk_rect(x0, y0, x1 - x0, y1 - y0));

    if (14.0f * g_zoom >= MIN_BODY_PX) {
        md_render_draw(c, inner, b->text, g_zoom);
    } else {
        /* Zoomed far out: title only, kept at a readable size as long as
         * the box is tall enough to hold it. */
        float px = fmaxf(22.0f * g_zoom, 11.0f);
        float tpad = fminf(pad, 4.0f);
        struct nk_rect t = nk_rect(r.x + tpad, r.y + tpad, r.w - tpad * 2.0f, r.h - tpad * 2.0f);
        if (t.h >= px && t.w >= 24.0f) md_render_title(c, t, b->text, px);
    }

    nk_push_scissor(c, window_clip);
}

void canvas_view_draw(struct nk_context *ctx, int width, int height) {
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    nk_style_push_vec2(ctx, &ctx->style.window.padding, nk_vec2(0, 0));

    /* Same NK_WINDOW_BACKGROUND reasoning as labels.c: a full-window
     * NO_INPUT layer must not claim Nuklear's topmost slot, or it steals
     * input from every real panel. */
    if (nk_begin(ctx, "##canvas", nk_rect(0, 0, (float)width, (float)height),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND)) {
        struct nk_command_buffer *c = nk_window_get_canvas(ctx);
        struct nk_rect clip = c->clip;

        draw_grid(c, width, height);
        for (int e = 0; e < EDGE_COUNT; e++) draw_edge(c, &g_edges[e]);
        for (int i = 0; i < BOX_COUNT; i++) draw_box(c, i, clip);

        char hud[64];
        snprintf(hud, sizeof(hud), "Canvas spike  \xC2\xB7  %.0f%%", g_zoom * 100.0f);
        const struct nk_user_font *f = fonts_ui();
        int n = 0;
        while (hud[n]) n++;
        float w = f->width(f->userdata, f->height, hud, n);
        nk_draw_text(c, nk_rect(12.0f, (float)height - f->height - 12.0f, w + 2.0f, f->height), hud, n, f,
                     nk_rgba(0, 0, 0, 0), theme_nk(g_theme.canvas_edge_label));
    }
    nk_end(ctx);

    nk_style_pop_vec2(ctx);
    nk_style_pop_style_item(ctx);
}
