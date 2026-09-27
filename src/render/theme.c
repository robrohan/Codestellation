#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "theme.h"

/* Tuned for readability on a dark background: panel text is ~13:1
 * against the window (Nuklear's stock grey was ~6:1), edges that fade
 * while a node is selected stay faintly visible instead of vanishing
 * into the background, and the selection is white so it can't be
 * mistaken for one of the hashed per-directory hues. */
const Theme g_theme = {
    .background     = 0x14161B,
    .edge           = 0x5B8DB8,
    .edge_faded     = 0x334050,
    .edge_highlight = 0x7CD0FF,
    .node_selected  = 0xFFFFFF,
    .node_cluster   = 0x59F273,
    .axis_x         = 0xD94040,
    .axis_y         = 0x40D940,
    .axis_z         = 0x4073F2,
    .node_saturation = 0.65f,
    .node_lightness  = 0.62f,

    .label_text   = 0xECEEF2,
    .label_shadow = 0x000000,
    .note_dot     = 0xFFB347,
};

/* Nuklear's style table, indexed by enum nk_style_colors. Every entry is
 * spelled out (nk_style_from_table takes the whole table) -- entries for
 * widgets this app doesn't use (sliders, charts, knobs) just follow the
 * rest of the palette. Hover/active states get *lighter* (Nuklear's
 * stock table went darker, which read as backwards), text inputs sit
 * darker than the window so they look inset, and checkbox ticks use the
 * accent blue. */
static const ThemeColor g_panel_colors[NK_COLOR_COUNT] = {
    [NK_COLOR_TEXT]                    = 0xE4E6EB,
    [NK_COLOR_WINDOW]                  = 0x22262D,
    [NK_COLOR_HEADER]                  = 0x2E343E,
    [NK_COLOR_BORDER]                  = 0x444B57,
    [NK_COLOR_BUTTON]                  = 0x323945,
    [NK_COLOR_BUTTON_HOVER]            = 0x3E4755,
    [NK_COLOR_BUTTON_ACTIVE]           = 0x4A5568,
    [NK_COLOR_TOGGLE]                  = 0x323945,
    [NK_COLOR_TOGGLE_HOVER]            = 0x3E4755,
    [NK_COLOR_TOGGLE_CURSOR]           = 0x5AB0FF,
    [NK_COLOR_SELECT]                  = 0x323945,
    [NK_COLOR_SELECT_ACTIVE]           = 0x3D6FA8,
    [NK_COLOR_SLIDER]                  = 0x2E343E,
    [NK_COLOR_SLIDER_CURSOR]           = 0x5A6475,
    [NK_COLOR_SLIDER_CURSOR_HOVER]     = 0x6E7A8E,
    [NK_COLOR_SLIDER_CURSOR_ACTIVE]    = 0x5AB0FF,
    [NK_COLOR_PROPERTY]                = 0x13151A,
    [NK_COLOR_EDIT]                    = 0x13151A,
    [NK_COLOR_EDIT_CURSOR]             = 0xE4E6EB,
    [NK_COLOR_COMBO]                   = 0x323945,
    [NK_COLOR_CHART]                   = 0x323945,
    [NK_COLOR_CHART_COLOR]             = 0x5AB0FF,
    [NK_COLOR_CHART_COLOR_HIGHLIGHT]   = 0xFF6B6B,
    [NK_COLOR_SCROLLBAR]               = 0x1B1E24,
    [NK_COLOR_SCROLLBAR_CURSOR]        = 0x4A5260,
    [NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = 0x5E6878,
    [NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = 0x7A8496,
    [NK_COLOR_TAB_HEADER]              = 0x2E343E,
    [NK_COLOR_KNOB]                    = 0x2E343E,
    [NK_COLOR_KNOB_CURSOR]             = 0x5A6475,
    [NK_COLOR_KNOB_CURSOR_HOVER]       = 0x6E7A8E,
    [NK_COLOR_KNOB_CURSOR_ACTIVE]      = 0x5AB0FF,
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
