#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "theme.h"

const Theme g_theme = {
    .background     = 0x17171C,
    .edge           = 0x4D8CBF,
    .edge_faded     = 0x132330,
    .edge_highlight = 0x59BFFF,
    .node_selected  = 0xFF5959,
    .node_cluster   = 0x59F273,
    .axis_x         = 0xD94040,
    .axis_y         = 0x40D940,
    .axis_z         = 0x4073F2,
    .node_saturation = 0.55f,
    .node_lightness  = 0.55f,

    .label_text = 0xE1E1E1,
    .note_dot   = 0xE6A528,
};

/* Nuklear's style table, indexed by enum nk_style_colors. Every entry is
 * spelled out (nk_style_from_table takes the whole table) -- entries for
 * widgets this app doesn't use (sliders, charts, knobs) just follow the
 * rest of the palette. */
static const ThemeColor g_panel_colors[NK_COLOR_COUNT] = {
    [NK_COLOR_TEXT]                    = 0xAFAFAF,
    [NK_COLOR_WINDOW]                  = 0x2D2D2D,
    [NK_COLOR_HEADER]                  = 0x282828,
    [NK_COLOR_BORDER]                  = 0x414141,
    [NK_COLOR_BUTTON]                  = 0x323232,
    [NK_COLOR_BUTTON_HOVER]            = 0x282828,
    [NK_COLOR_BUTTON_ACTIVE]           = 0x232323,
    [NK_COLOR_TOGGLE]                  = 0x646464,
    [NK_COLOR_TOGGLE_HOVER]            = 0x787878,
    [NK_COLOR_TOGGLE_CURSOR]           = 0x2D2D2D,
    [NK_COLOR_SELECT]                  = 0x2D2D2D,
    [NK_COLOR_SELECT_ACTIVE]           = 0x232323,
    [NK_COLOR_SLIDER]                  = 0x262626,
    [NK_COLOR_SLIDER_CURSOR]           = 0x646464,
    [NK_COLOR_SLIDER_CURSOR_HOVER]     = 0x787878,
    [NK_COLOR_SLIDER_CURSOR_ACTIVE]    = 0x969696,
    [NK_COLOR_PROPERTY]                = 0x262626,
    [NK_COLOR_EDIT]                    = 0x262626,
    [NK_COLOR_EDIT_CURSOR]             = 0xAFAFAF,
    [NK_COLOR_COMBO]                   = 0x2D2D2D,
    [NK_COLOR_CHART]                   = 0x787878,
    [NK_COLOR_CHART_COLOR]             = 0x2D2D2D,
    [NK_COLOR_CHART_COLOR_HIGHLIGHT]   = 0xFF0000,
    [NK_COLOR_SCROLLBAR]               = 0x282828,
    [NK_COLOR_SCROLLBAR_CURSOR]        = 0x646464,
    [NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = 0x787878,
    [NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = 0x969696,
    [NK_COLOR_TAB_HEADER]              = 0x282828,
    [NK_COLOR_KNOB]                    = 0x262626,
    [NK_COLOR_KNOB_CURSOR]             = 0x646464,
    [NK_COLOR_KNOB_CURSOR_HOVER]       = 0x787878,
    [NK_COLOR_KNOB_CURSOR_ACTIVE]      = 0x969696,
};

void theme_rgb(ThemeColor c, float *r, float *g, float *b) {
    *r = (float)((c >> 16) & 0xFF) / 255.0f;
    *g = (float)((c >> 8) & 0xFF) / 255.0f;
    *b = (float)(c & 0xFF) / 255.0f;
}

struct nk_color theme_nk(ThemeColor c) {
    return nk_rgb((int)((c >> 16) & 0xFF), (int)((c >> 8) & 0xFF), (int)(c & 0xFF));
}

void theme_apply_panels(struct nk_context *ctx) {
    struct nk_color table[NK_COLOR_COUNT];
    for (int i = 0; i < NK_COLOR_COUNT; i++) table[i] = theme_nk(g_panel_colors[i]);
    nk_style_from_table(ctx, table);
}
