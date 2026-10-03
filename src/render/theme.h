#ifndef CODEMAP_THEME_H
#define CODEMAP_THEME_H

#include <stdint.h>

/* The app's one colour palette. Every colour the renderer draws with
 * lives in theme.c -- the 3D scene, the label overlay, and Nuklear's
 * panel style table -- so retuning the look is an edit to that one file.
 *
 * Colours are 0xRRGGBB, easy to read and tweak by eye. */
typedef uint32_t ThemeColor;

typedef struct {
    /* 3D scene */
    ThemeColor background;
    ThemeColor edge;
    ThemeColor edge_faded;      /* every edge, while a node is selected */
    ThemeColor edge_highlight;  /* the selected node's own edges, drawn on top */
    ThemeColor node_selected;
    ThemeColor node_cluster;    /* multi-select accent */
    ThemeColor axis_x, axis_y, axis_z;
    /* Per-directory node colours are hashed hues (see dircolor.c) at this
     * fixed HSL saturation/lightness. */
    float node_saturation, node_lightness;

    /* Label overlay (labels.c) */
    ThemeColor label_text;
    ThemeColor label_shadow;    /* drawn 1px down-right, under the text */
    ThemeColor note_dot;
    ThemeColor complexity_marker; /* ring around a complex function's line (textarea.c) */

    /* 2D canvas (canvas_view.c, md_render.c) */
    ThemeColor canvas_background;
    ThemeColor grid_minor, grid_major;
    ThemeColor box_fill;
    ThemeColor box_border;          /* uncoloured box */
    ThemeColor box_border_selected;
    ThemeColor box_text;
    ThemeColor box_heading;
    ThemeColor box_link;
    ThemeColor box_code_fill;
    ThemeColor canvas_edge;
    ThemeColor canvas_edge_label;
    /* JSON Canvas preset colours "1".."6": red, orange, yellow, green,
     * cyan, purple. Index 0 is preset "1". */
    ThemeColor box_preset[6];
} Theme;

extern const Theme g_theme;

/* Unpacks to 0..1 floats for GL. */
void theme_rgb(ThemeColor c, float *r, float *g, float *b);

#ifdef NK_NUKLEAR_H_
/* For TUs that already include nuklear.h (with this codebase's usual
 * NK_INCLUDE_* set). */
struct nk_color theme_nk(ThemeColor c);
#endif

/* Installs the panel colours (kept in theme.c alongside everything else)
 * as Nuklear's style table. Call once after nk_glfw3_init. Forward-declared
 * rather than including nuklear.h -- see ui_panel.h. */
struct nk_context;
void theme_apply_panels(struct nk_context *ctx);

#endif
