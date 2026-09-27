#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "md_render.h"
#include "fonts.h"
#include "theme.h"
#include <stdbool.h>
#include <string.h>

/* Sizes at zoom 1.0, in logical pixels. */
#define BODY_PX 14.0f
#define LINE_FACTOR 1.4f
#define BULLET_INDENT 16.0f

enum { SEG_BOLD = 1, SEG_ITALIC = 2, SEG_CODE = 4, SEG_LINK = 8 };

typedef struct {
    struct nk_command_buffer *canvas;
    struct nk_rect r;
    float zoom;
    float x, y;          /* pen position */
    float line_left;     /* where wrapped lines restart (after any bullet indent) */
    float px, lh;        /* current block's font size and line height */
    bool heading;
    bool stop;           /* ran past the bottom of r */
} Layout;

static const struct nk_user_font *font_for(int flags, float px) {
    if (flags & SEG_CODE) return fonts_sized(FONT_MONO, px * 0.92f);
    if (flags & SEG_BOLD) return fonts_sized(FONT_BOLD, px);
    if (flags & SEG_ITALIC) return fonts_sized(FONT_ITALIC, px);
    return fonts_sized(FONT_REGULAR, px);
}

static void newline(Layout *L) {
    L->x = L->line_left;
    L->y += L->lh;
    if (L->y > L->r.y + L->r.h) L->stop = true;
}

/* Draws one word-ish segment (no spaces inside, one style), wrapping
 * first if it doesn't fit on the current line. */
static void emit(Layout *L, const char *text, int len, int flags, bool space_before) {
    if (len <= 0 || L->stop) return;
    int f = flags | (L->heading ? SEG_BOLD : 0);
    const struct nk_user_font *font = font_for(f, L->px);
    float w = font->width(font->userdata, font->height, text, len);

    if (space_before && L->x > L->line_left) {
        const struct nk_user_font *sf = font_for(L->heading ? SEG_BOLD : 0, L->px);
        L->x += sf->width(sf->userdata, sf->height, " ", 1);
    }
    if (L->x + w > L->r.x + L->r.w && L->x > L->line_left) {
        newline(L);
        if (L->stop) return;
    }

    float ty = L->y + (L->lh - font->height) * 0.5f;
    struct nk_color fg = theme_nk(L->heading ? g_theme.box_heading : g_theme.box_text);
    if (f & SEG_LINK) fg = theme_nk(g_theme.box_link);

    if (f & SEG_CODE) {
        float pad = 2.0f * L->zoom;
        nk_fill_rect(L->canvas, nk_rect(L->x - pad, ty - pad * 0.5f, w + pad * 2.0f, font->height + pad),
                     2.0f * L->zoom, theme_nk(g_theme.box_code_fill));
    }
    /* nk_draw_text truncates to the rect's width, so leave a little slack
     * for float rounding or the last glyph can vanish. */
    nk_draw_text(L->canvas, nk_rect(L->x, ty, w + 2.0f, font->height), text, len, font, nk_rgba(0, 0, 0, 0), fg);
    if (f & SEG_LINK) {
        float uy = ty + font->height + 1.0f;
        nk_stroke_line(L->canvas, L->x, uy, L->x + w, uy, 1.0f, fg);
    }
    L->x += w;
}

/* Splits one line's inline markup into styled segments and emits them. */
static void layout_inline(Layout *L, const char *s, int n) {
    int flags = 0;
    int start = 0;       /* start of the pending segment */
    bool space_before = false;
    int i = 0;

#define FLUSH()                                                           \
    do {                                                                  \
        if (i > start) {                                                  \
            emit(L, s + start, i - start, flags, space_before);           \
            space_before = false;                                         \
        }                                                                 \
    } while (0)

    while (i < n && !L->stop) {
        char c = s[i];
        if (flags & SEG_CODE) {
            if (c == '`') {
                FLUSH();
                flags &= ~SEG_CODE;
                start = ++i;
            } else if (c == ' ') {
                FLUSH();
                space_before = true;
                start = ++i;
            } else {
                i++;
            }
            continue;
        }
        if (c == '*' && i + 1 < n && s[i + 1] == '*') {
            FLUSH();
            flags ^= SEG_BOLD;
            i += 2;
            start = i;
        } else if (c == '*') {
            FLUSH();
            flags ^= SEG_ITALIC;
            start = ++i;
        } else if (c == '`') {
            FLUSH();
            flags |= SEG_CODE;
            start = ++i;
        } else if (c == '[' && i + 1 < n && s[i + 1] == '[') {
            FLUSH();
            flags |= SEG_LINK;
            i += 2;
            start = i;
        } else if (c == ']' && i + 1 < n && s[i + 1] == ']' && (flags & SEG_LINK)) {
            FLUSH();
            flags &= ~SEG_LINK;
            i += 2;
            start = i;
        } else if (c == ' ' || c == '\t') {
            FLUSH();
            space_before = true;
            start = ++i;
        } else {
            i++;
        }
    }
    FLUSH();
#undef FLUSH
}

void md_render_draw(struct nk_command_buffer *canvas, struct nk_rect r, const char *md, float zoom) {
    Layout L = { 0 };
    L.canvas = canvas;
    L.r = r;
    L.zoom = zoom;
    L.y = r.y;

    bool first_block = true;
    const char *p = md;
    while (*p && !L.stop) {
        const char *eol = strchr(p, '\n');
        int n = eol ? (int)(eol - p) : (int)strlen(p);
        const char *line = p;
        p = eol ? eol + 1 : p + n;

        /* Blank line: paragraph gap. */
        int k = 0;
        while (k < n && (line[k] == ' ' || line[k] == '\t')) k++;
        if (k == n) {
            if (!first_block) L.y += BODY_PX * zoom * 0.5f;
            continue;
        }

        L.heading = false;
        L.px = BODY_PX * zoom;
        L.line_left = r.x;
        int level = 0;
        while (k + level < n && line[k + level] == '#') level++;
        bool bullet = false;

        if (level >= 1 && level <= 3 && k + level < n && line[k + level] == ' ') {
            static const float scale[] = { 1.0f, 1.57f, 1.29f, 1.1f };
            L.heading = true;
            L.px = BODY_PX * scale[level] * zoom;
            k += level + 1;
            if (!first_block) L.y += L.px * 0.3f;
        } else if ((line[k] == '-' || line[k] == '*') && k + 1 < n && line[k + 1] == ' ') {
            bullet = true;
            k += 2;
        }

        L.lh = L.px * LINE_FACTOR;
        L.x = r.x;
        if (L.y + L.lh > r.y + r.h + L.lh) break;

        if (bullet) {
            const struct nk_user_font *bf = fonts_sized(FONT_REGULAR, L.px);
            float ty = L.y + (L.lh - bf->height) * 0.5f;
            nk_draw_text(canvas, nk_rect(r.x + 4.0f * zoom, ty, bf->height, bf->height), "\xE2\x80\xA2", 3, bf,
                         nk_rgba(0, 0, 0, 0), theme_nk(g_theme.box_text));
            L.line_left = r.x + BULLET_INDENT * zoom;
            L.x = L.line_left;
        }

        layout_inline(&L, line + k, n - k);
        if (!L.stop) newline(&L);
        first_block = false;
    }
}

void md_render_title(struct nk_command_buffer *canvas, struct nk_rect r, const char *md, float px) {
    /* First non-blank line. */
    const char *p = md;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    const char *eol = strchr(p, '\n');
    int n = eol ? (int)(eol - p) : (int)strlen(p);

    /* Strip heading/bullet prefix and inline markers into a small buffer. */
    char buf[256];
    int o = 0, i = 0;
    while (i < n && p[i] == '#') i++;
    if (i < n && (p[i] == '-' || p[i] == '*') && i + 1 < n && p[i + 1] == ' ') i += 2;
    while (i < n && p[i] == ' ') i++;
    for (; i < n && o < (int)sizeof(buf) - 1; i++) {
        char c = p[i];
        if (c == '*' || c == '`' || c == '[' || c == ']') continue;
        buf[o++] = c;
    }
    if (o == 0) return;

    const struct nk_user_font *font = fonts_sized(FONT_BOLD, px);
    nk_draw_text(canvas, nk_rect(r.x, r.y, r.w, font->height), buf, o, font, nk_rgba(0, 0, 0, 0),
                 theme_nk(g_theme.box_heading));
}
