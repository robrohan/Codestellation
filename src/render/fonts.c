#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "fonts.h"
#include <stddef.h>

/* Generated at build time from the TTFs in vendor/fonts (src/CMakeLists.txt). */
extern const unsigned char font_inter_regular[];
extern const size_t font_inter_regular_size;
extern const unsigned char font_inter_bold[];
extern const size_t font_inter_bold_size;
extern const unsigned char font_inter_italic[];
extern const size_t font_inter_italic_size;
extern const unsigned char font_jetbrains_mono[];
extern const size_t font_jetbrains_mono_size;

/* Logical pixel sizes baked for every style. Canvas text at any other
 * size is the next size up, scaled down (linear filtering keeps that
 * clean); past the largest it's scaled up and softens slightly. */
static const float k_sizes[] = { 10.0f, 14.0f, 20.0f, 28.0f, 40.0f };
#define SIZE_COUNT (sizeof(k_sizes) / sizeof(k_sizes[0]))

/* Both are in k_sizes, so panels and the source preview reuse the canvas
 * bake rather than adding fonts to the atlas. */
#define UI_SIZE 14.0f
#define MONO_UI_SIZE 14.0f

/* Latin-1 plus the typographic punctuation people paste into notes
 * (dashes, curly quotes, bullet, ellipsis, arrows). Nuklear keeps this
 * pointer, so it must outlive the atlas. */
static const nk_rune k_ranges[] = {
    0x0020, 0x00FF,
    0x2013, 0x2014,
    0x2018, 0x201D,
    0x2022, 0x2022,
    0x2026, 0x2026,
    0x2039, 0x203A,
    0x2190, 0x2193,
    0
};

static struct nk_font *g_fonts[FONT_STYLE_COUNT][SIZE_COUNT];
static float g_scale = 1.0f;

#define POOL_SIZE 256
static struct nk_user_font g_pool[POOL_SIZE];
static int g_pool_used = 0;

static struct nk_font *add_one(struct nk_font_atlas *atlas, const unsigned char *data, size_t size, float px) {
    struct nk_font_config cfg = nk_font_config(px * g_scale);
    cfg.range = k_ranges;
    /* Horizontal oversampling buys sub-pixel glyph placement, which only
     * matters when a glyph is barely a few physical pixels wide -- at 2x
     * the atlas cost isn't worth it. */
    cfg.oversample_h = g_scale >= 2.0f ? 1 : 2;
    cfg.oversample_v = 1;
    return nk_font_atlas_add_from_memory(atlas, (void *)data, (nk_size)size, px * g_scale, &cfg);
}

void fonts_add(struct nk_font_atlas *atlas, float fb_scale) {
    g_scale = fb_scale >= 1.0f ? fb_scale : 1.0f;
    const unsigned char *data[FONT_STYLE_COUNT] = {
        font_inter_regular, font_inter_bold, font_inter_italic, font_jetbrains_mono,
    };
    const size_t sizes[FONT_STYLE_COUNT] = {
        font_inter_regular_size, font_inter_bold_size, font_inter_italic_size, font_jetbrains_mono_size,
    };
    for (int s = 0; s < FONT_STYLE_COUNT; s++) {
        for (size_t i = 0; i < SIZE_COUNT; i++) {
            g_fonts[s][i] = add_one(atlas, data[s], sizes[s], k_sizes[i]);
        }
    }
}

void fonts_finish(void) {
    for (int s = 0; s < FONT_STYLE_COUNT; s++) {
        for (size_t i = 0; i < SIZE_COUNT; i++) g_fonts[s][i]->handle.height /= g_scale;
    }
}

static const struct nk_user_font *baked(FontStyle style, float px) {
    for (size_t i = 0; i < SIZE_COUNT; i++) {
        if (k_sizes[i] == px) return &g_fonts[style][i]->handle;
    }
    return &g_fonts[style][0]->handle;
}

const struct nk_user_font *fonts_ui(void) { return baked(FONT_REGULAR, UI_SIZE); }
const struct nk_user_font *fonts_mono(void) { return baked(FONT_MONO, MONO_UI_SIZE); }

const struct nk_user_font *fonts_sized(FontStyle style, float px) {
    size_t i = 0;
    while (i + 1 < SIZE_COUNT && k_sizes[i] < px) i++;
    const struct nk_user_font *base = &g_fonts[style][i]->handle;
    if (px == base->height) return base;
    /* Everything on screen shares one zoom, so a frame only ever asks for
     * a handful of distinct sizes -- reuse an existing entry first. */
    for (int k = 0; k < g_pool_used; k++) {
        if (g_pool[k].userdata.ptr == base->userdata.ptr && g_pool[k].height == px) return &g_pool[k];
    }
    /* Pool exhausted (a pathological frame): fall back to the unscaled
     * base rather than overwrite a font an earlier command points at. */
    if (g_pool_used >= POOL_SIZE) return base;
    struct nk_user_font *f = &g_pool[g_pool_used++];
    *f = *base;
    f->height = px; /* nk_font measures and places glyphs relative to this */
    return f;
}

void fonts_frame_reset(void) {
    g_pool_used = 0;
}
