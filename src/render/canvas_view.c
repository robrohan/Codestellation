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
#include "../canvas/canvas_doc.h"
#include "../canvas/project.h"
#include "../common/pathutil.h"
#include "tinyfiledialogs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_ZOOM 0.1f
#define MAX_ZOOM 4.0f
#define BOX_PAD 12.0f
#define GRID_MINOR 20.0f
#define GRID_MAJOR 100.0f
/* Below this on-screen body size, full markdown is unreadable anyway --
 * show just the title. */
#define MIN_BODY_PX 7.0f
#define MAX_DEPTH 32
#define HANDLE_R 5.0f          /* connection dot radius, screen px */
#define HANDLE_HIT 9.0f
#define RESIZE_HIT 14.0f
#define EDGE_HIT 7.0f
#define DOUBLE_CLICK_S 0.35
#define SAVE_DEBOUNCE_S 0.6
#define MIN_W 60.0f
#define MIN_H 40.0f
#define EDITOR_TITLE "Edit"
#define CRUMBS_TITLE "##breadcrumbs"
#define EDIT_BUF_SIZE 32768

typedef enum { SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM, SIDE_LEFT, SIDE_AUTO } Side;
typedef enum { MODE_NONE, MODE_PAN, MODE_DRAG, MODE_RESIZE, MODE_CONNECT } Mode;
typedef enum { SEL_NONE, SEL_NODE, SEL_EDGE } SelKind;

typedef struct {
    char *path;     /* absolute path of this level's .canvas */
    char *title;    /* crumb label */
    float zoom, ox, oy;
} NavEntry;

typedef struct {
    float ax, ay, c1x, c1y, c2x, c2y, bx, by;
} Bez;

/* ---- state -------------------------------------------------------------- */

static bool g_open = false;
static NavEntry g_nav[MAX_DEPTH];
static int g_depth = 0;              /* g_nav[g_depth - 1] is the current level */
static CanvasDoc g_doc;
static bool g_load_failed = false;   /* current file didn't parse: never overwrite it */

static float g_zoom = 1.0f, g_ox = 0.0f, g_oy = 0.0f;
static bool g_fit_pending = false;

static SelKind g_sel = SEL_NONE;
static int g_sel_index = -1;
static int g_hover = -1;

static Mode g_mode = MODE_NONE;
static int g_mode_index = -1;        /* node being dragged/resized/connected from */
static Side g_conn_side = SIDE_AUTO;
static bool g_moved = false;
static float g_last_x, g_last_y;
static float g_cursor_x, g_cursor_y;
static bool g_prev_left, g_prev_right, g_prev_middle;
static double g_last_click_time = -1.0;
static float g_last_click_x, g_last_click_y;

static bool g_dirty = false;
static double g_dirty_since = 0.0;
static double g_now = 0.0;

static bool g_editor_open = false;
static bool g_edit_focus_pending = false;
static bool g_edit_active = false;
static char g_edit_buf[EDIT_BUF_SIZE];

/* Actions requested from the editor/breadcrumb widgets during draw, run at
 * the start of the next update so the doc never changes mid-draw. */
static int g_pending_crumb = -1;
static int g_pending_go_into = -1;
static bool g_pending_delete = false;

/* ---- small helpers ------------------------------------------------------ */

static const char *cur_path(void) { return g_nav[g_depth - 1].path; }

static float to_sx(float wx) { return wx * g_zoom + g_ox; }
static float to_sy(float wy) { return wy * g_zoom + g_oy; }

static struct nk_rect rect_of(const CanvasNode *n) {
    return nk_rect(to_sx(n->x), to_sy(n->y), n->w * g_zoom, n->h * g_zoom);
}

static bool inside(struct nk_rect r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

static bool node_color(const char *c, ThemeColor *out) {
    if (!c || !*c) return false;
    if (c[0] == '#' && strlen(c) == 7) {
        *out = (ThemeColor)strtoul(c + 1, NULL, 16);
        return true;
    }
    if (c[0] >= '1' && c[0] <= '6' && c[1] == '\0') {
        *out = g_theme.box_preset[c[0] - '1'];
        return true;
    }
    return false;
}

static struct nk_color with_alpha(ThemeColor c, int a) {
    struct nk_color k = theme_nk(c);
    k.a = (nk_byte)a;
    return k;
}

static bool ends_with_ci(const char *s, size_t n, const char *suffix) {
    size_t m = strlen(suffix);
    if (n < m) return false;
    for (size_t i = 0; i < m; i++) {
        char a = s[n - m + i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

/* The first link on this node to another canvas: a [[x.canvas]] in a text
 * node (alias after '|' and heading after '#' ignored), or a file node
 * pointing at a .canvas. malloc'd; NULL if there isn't one. */
static char *canvas_link_of(const CanvasNode *n) {
    if (n->type == CNODE_FILE && n->file && ends_with_ci(n->file, strlen(n->file), ".canvas")) {
        return xstrdup(n->file);
    }
    if (n->type != CNODE_TEXT || !n->text) return NULL;
    const char *p = n->text;
    while ((p = strstr(p, "[[")) != NULL) {
        p += 2;
        const char *end = strstr(p, "]]");
        if (!end) break;
        size_t len = 0;
        while (p + len < end && p[len] != '|' && p[len] != '#') len++;
        if (ends_with_ci(p, len, ".canvas")) {
            char *out = (char *)malloc(len + 1);
            memcpy(out, p, len);
            out[len] = '\0';
            return out;
        }
        p = end + 2;
    }
    return NULL;
}

/* Crumb label for going into node n: its title, else the link's file name. */
static char *crumb_title_for(const CanvasNode *n, const char *link) {
    char buf[64];
    if (n->type == CNODE_TEXT && n->text && md_title_text(n->text, buf, (int)sizeof(buf)) > 0) return xstrdup(buf);
    const char *base = strrchr(link, '/');
    base = base ? base + 1 : link;
    size_t len = strlen(base);
    if (ends_with_ci(base, len, ".canvas")) len -= 7;
    char *out = (char *)malloc(len + 1);
    memcpy(out, base, len);
    out[len] = '\0';
    return out;
}

/* ---- saving and navigation ---------------------------------------------- */

static void mark_dirty(void) {
    if (g_load_failed) return;
    g_dirty = true;
    g_dirty_since = g_now;
}

static void flush(void) {
    if (!g_dirty) return;
    if (!canvas_doc_write(&g_doc, cur_path())) fprintf(stderr, "error: could not save %s\n", cur_path());
    g_dirty = false;
}

static void clear_selection(void) {
    g_sel = SEL_NONE;
    g_sel_index = -1;
    g_editor_open = false;
    g_edit_active = false;
}

static void load_current(void) {
    canvas_doc_free(&g_doc);
    g_load_failed = !canvas_doc_read(cur_path(), &g_doc);
    if (g_load_failed) fprintf(stderr, "warning: could not read %s -- showing it empty, not saving\n", cur_path());
    clear_selection();
    g_mode = MODE_NONE;
    g_hover = -1;
}

static void fit_view(int width, int height) {
    if (g_doc.node_count == 0) {
        g_zoom = 1.0f;
        g_ox = (float)width * 0.5f;
        g_oy = (float)height * 0.5f;
        return;
    }
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (size_t i = 0; i < g_doc.node_count; i++) {
        const CanvasNode *n = &g_doc.nodes[i];
        x0 = fminf(x0, n->x);
        y0 = fminf(y0, n->y);
        x1 = fmaxf(x1, n->x + n->w);
        y1 = fmaxf(y1, n->y + n->h);
    }
    const float margin = 90.0f;
    float zx = ((float)width - margin * 2.0f) / fmaxf(x1 - x0, 1.0f);
    float zy = ((float)height - margin * 2.0f) / fmaxf(y1 - y0, 1.0f);
    g_zoom = fminf(fminf(zx, zy), 1.0f);
    if (g_zoom < MIN_ZOOM) g_zoom = MIN_ZOOM;
    g_ox = (float)width * 0.5f - (x0 + x1) * 0.5f * g_zoom;
    g_oy = (float)height * 0.5f - (y0 + y1) * 0.5f * g_zoom;
}

static void save_view(void) {
    NavEntry *e = &g_nav[g_depth - 1];
    e->zoom = g_zoom;
    e->ox = g_ox;
    e->oy = g_oy;
}

static void pop_to(int index) {
    if (index < 0 || index >= g_depth - 1) return;
    flush();
    for (int i = index + 1; i < g_depth; i++) {
        free(g_nav[i].path);
        free(g_nav[i].title);
    }
    g_depth = index + 1;
    load_current();
    g_zoom = g_nav[index].zoom;
    g_ox = g_nav[index].ox;
    g_oy = g_nav[index].oy;
}

static void go_into(int node_index) {
    if (node_index < 0 || node_index >= (int)g_doc.node_count) return;
    const CanvasNode *n = &g_doc.nodes[node_index];
    char *link = canvas_link_of(n);
    if (!link) return; /* directory links arrive in step 4c */

    char *path = project_resolve_link(cur_path(), link);
    if (!path_exists(path) && !canvas_doc_write_empty(path)) {
        char msg[4200];
        snprintf(msg, sizeof(msg), "Could not create the canvas:\n%s", path);
        tinyfd_messageBox("Codestellation", msg, "ok", "error", 1);
        free(path);
        free(link);
        return;
    }
    /* Normalize now that it exists, so the cycle check compares like
     * with like. */
    char *norm = path_normalize(path);
    if (norm) {
        free(path);
        path = norm;
    }

    /* Already on the trail (a canvas linking back to an ancestor): jump
     * back to it rather than nesting forever. */
    for (int i = 0; i < g_depth; i++) {
        if (strcmp(g_nav[i].path, path) == 0) {
            if (i < g_depth - 1) pop_to(i);
            free(path);
            free(link);
            return;
        }
    }
    if (g_depth >= MAX_DEPTH) {
        free(path);
        free(link);
        return;
    }

    char *title = crumb_title_for(n, link);
    free(link);
    flush();
    save_view();
    g_nav[g_depth].path = path;
    g_nav[g_depth].title = title;
    g_depth++;
    load_current();
    g_fit_pending = true;
}

void canvas_view_open(const char *root_canvas_path, const char *root_title) {
    canvas_view_close();
    char *norm = path_normalize(root_canvas_path);
    g_nav[0].path = norm ? norm : xstrdup(root_canvas_path);
    g_nav[0].title = xstrdup(root_title && *root_title ? root_title : "Project");
    g_depth = 1;
    g_open = true;
    canvas_doc_init(&g_doc);
    load_current();
    g_fit_pending = true;
}

void canvas_view_close(void) {
    if (!g_open) return;
    flush();
    canvas_doc_free(&g_doc);
    for (int i = 0; i < g_depth; i++) {
        free(g_nav[i].path);
        free(g_nav[i].title);
    }
    g_depth = 0;
    g_open = false;
    clear_selection();
}

bool canvas_view_is_open(void) { return g_open; }

void canvas_view_escape(void) {
    if (!g_open) return;
    if (g_editor_open || g_sel != SEL_NONE) {
        clear_selection();
        return;
    }
    if (g_depth > 1) pop_to(g_depth - 2);
}

/* ---- geometry ----------------------------------------------------------- */

static Side side_from(const char *s) {
    if (!s) return SIDE_AUTO;
    if (strcmp(s, "top") == 0) return SIDE_TOP;
    if (strcmp(s, "right") == 0) return SIDE_RIGHT;
    if (strcmp(s, "bottom") == 0) return SIDE_BOTTOM;
    if (strcmp(s, "left") == 0) return SIDE_LEFT;
    return SIDE_AUTO;
}

static const char *side_name(Side s) {
    static const char *names[] = { "top", "right", "bottom", "left" };
    return s < SIDE_AUTO ? names[s] : NULL;
}

/* Midpoint of a node's side in screen space, and its outward normal. */
static void side_point(const CanvasNode *n, Side s, float *x, float *y, float *nx, float *ny) {
    struct nk_rect r = rect_of(n);
    switch (s) {
        case SIDE_TOP:    *x = r.x + r.w * 0.5f; *y = r.y;           *nx = 0;  *ny = -1; break;
        case SIDE_BOTTOM: *x = r.x + r.w * 0.5f; *y = r.y + r.h;     *nx = 0;  *ny = 1;  break;
        case SIDE_LEFT:   *x = r.x;           *y = r.y + r.h * 0.5f; *nx = -1; *ny = 0;  break;
        default:          *x = r.x + r.w;     *y = r.y + r.h * 0.5f; *nx = 1;  *ny = 0;  break;
    }
}

/* The side of n facing screen point (tx, ty), comparing against the box's
 * own aspect so wide boxes prefer left/right. */
static Side facing_side(const CanvasNode *n, float tx, float ty) {
    struct nk_rect r = rect_of(n);
    float dx = tx - (r.x + r.w * 0.5f), dy = ty - (r.y + r.h * 0.5f);
    if (fabsf(dx) * (r.h * 0.5f) > fabsf(dy) * (r.w * 0.5f)) return dx > 0 ? SIDE_RIGHT : SIDE_LEFT;
    return dy > 0 ? SIDE_BOTTOM : SIDE_TOP;
}

static void center_of(const CanvasNode *n, float *x, float *y) {
    struct nk_rect r = rect_of(n);
    *x = r.x + r.w * 0.5f;
    *y = r.y + r.h * 0.5f;
}

static void make_bez(float ax, float ay, float anx, float any, float bx, float by, float bnx, float bny, Bez *b) {
    float dist = sqrtf((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
    float off = fmaxf(40.0f * g_zoom, dist * 0.35f);
    b->ax = ax; b->ay = ay;
    b->bx = bx; b->by = by;
    b->c1x = ax + anx * off; b->c1y = ay + any * off;
    b->c2x = bx + bnx * off; b->c2y = by + bny * off;
}

static bool edge_bez(const CanvasEdge *e, Bez *out) {
    int ia = canvas_doc_find_node(&g_doc, e->from_node), ib = canvas_doc_find_node(&g_doc, e->to_node);
    if (ia < 0 || ib < 0) return false;
    const CanvasNode *a = &g_doc.nodes[ia], *b = &g_doc.nodes[ib];
    float acx, acy, bcx, bcy;
    center_of(a, &acx, &acy);
    center_of(b, &bcx, &bcy);
    Side sa = side_from(e->from_side), sb = side_from(e->to_side);
    if (sa == SIDE_AUTO) sa = facing_side(a, bcx, bcy);
    if (sb == SIDE_AUTO) sb = facing_side(b, acx, acy);
    float ax, ay, anx, any, bx, by, bnx, bny;
    side_point(a, sa, &ax, &ay, &anx, &any);
    side_point(b, sb, &bx, &by, &bnx, &bny);
    make_bez(ax, ay, anx, any, bx, by, bnx, bny, out);
    return true;
}

static void bez_at(const Bez *b, float t, float *x, float *y) {
    float u = 1.0f - t;
    float w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
    *x = w0 * b->ax + w1 * b->c1x + w2 * b->c2x + w3 * b->bx;
    *y = w0 * b->ay + w1 * b->c1y + w2 * b->c2y + w3 * b->by;
}

static float bez_distance(const Bez *b, float px, float py) {
    float best = 1e30f;
    for (int i = 0; i <= 32; i++) {
        float x, y;
        bez_at(b, (float)i / 32.0f, &x, &y);
        float d = (x - px) * (x - px) + (y - py) * (y - py);
        if (d < best) best = d;
    }
    return sqrtf(best);
}

/* ---- hit testing -------------------------------------------------------- */

static struct nk_rect group_header(const CanvasNode *n) {
    struct nk_rect r = rect_of(n);
    r.h = fmaxf(26.0f * g_zoom, 14.0f);
    return r;
}

static int hit_node(float x, float y) {
    for (size_t i = g_doc.node_count; i-- > 0;) {
        const CanvasNode *n = &g_doc.nodes[i];
        if (n->type != CNODE_GROUP && inside(rect_of(n), x, y)) return (int)i;
    }
    /* Groups sit behind everything; only their header strip grabs them. */
    for (size_t i = g_doc.node_count; i-- > 0;) {
        const CanvasNode *n = &g_doc.nodes[i];
        if (n->type == CNODE_GROUP && inside(group_header(n), x, y)) return (int)i;
    }
    return -1;
}

static int hit_edge(float x, float y) {
    int best = -1;
    float best_d = EDGE_HIT;
    for (size_t i = 0; i < g_doc.edge_count; i++) {
        Bez b;
        if (!edge_bez(&g_doc.edges[i], &b)) continue;
        float d = bez_distance(&b, x, y);
        if (d < best_d) {
            best_d = d;
            best = (int)i;
        }
    }
    return best;
}

static bool handles_visible(int i) {
    return i >= 0 && i < (int)g_doc.node_count && g_doc.nodes[i].type != CNODE_GROUP && g_zoom >= 0.25f;
}

static bool hit_handle(int i, float x, float y, Side *out) {
    if (!handles_visible(i)) return false;
    for (int s = 0; s < 4; s++) {
        float hx, hy, nx, ny;
        side_point(&g_doc.nodes[i], (Side)s, &hx, &hy, &nx, &ny);
        if ((hx - x) * (hx - x) + (hy - y) * (hy - y) <= HANDLE_HIT * HANDLE_HIT) {
            *out = (Side)s;
            return true;
        }
    }
    return false;
}

static bool hit_resize(int i, float x, float y) {
    if (i < 0 || i >= (int)g_doc.node_count) return false;
    struct nk_rect r = rect_of(&g_doc.nodes[i]);
    return x >= r.x + r.w - RESIZE_HIT && x <= r.x + r.w + 2.0f && y >= r.y + r.h - RESIZE_HIT && y <= r.y + r.h + 2.0f;
}

/* ---- editing ------------------------------------------------------------ */

static char **editor_target_string(void) {
    if (g_sel == SEL_EDGE && g_sel_index >= 0 && g_sel_index < (int)g_doc.edge_count) {
        return &g_doc.edges[g_sel_index].label;
    }
    if (g_sel == SEL_NODE && g_sel_index >= 0 && g_sel_index < (int)g_doc.node_count) {
        CanvasNode *n = &g_doc.nodes[g_sel_index];
        switch (n->type) {
            case CNODE_TEXT: return &n->text;
            case CNODE_FILE: return &n->file;
            case CNODE_LINK: return &n->url;
            case CNODE_GROUP: return &n->label;
        }
    }
    return NULL;
}

static void open_editor(bool focus) {
    char **s = editor_target_string();
    if (!s) return;
    const char *src = *s ? *s : "";
    strncpy(g_edit_buf, src, sizeof(g_edit_buf) - 1);
    g_edit_buf[sizeof(g_edit_buf) - 1] = '\0';
    g_editor_open = true;
    g_edit_focus_pending = focus;
}

static void select_node(int i) {
    if (g_sel != SEL_NODE || g_sel_index != i) g_editor_open = false;
    g_sel = SEL_NODE;
    g_sel_index = i;
}

static void select_edge(int i) {
    if (g_sel != SEL_EDGE || g_sel_index != i) g_editor_open = false;
    g_sel = SEL_EDGE;
    g_sel_index = i;
}

static void delete_selection(void) {
    /* Indices shift after a removal, so no drag/resize/connect may
     * survive it. */
    g_mode = MODE_NONE;
    g_mode_index = -1;
    if (g_sel == SEL_NODE && g_sel_index >= 0 && g_sel_index < (int)g_doc.node_count) {
        canvas_doc_remove_node(&g_doc, (size_t)g_sel_index);
        mark_dirty();
    } else if (g_sel == SEL_EDGE && g_sel_index >= 0 && g_sel_index < (int)g_doc.edge_count) {
        canvas_doc_remove_edge(&g_doc, (size_t)g_sel_index);
        mark_dirty();
    }
    clear_selection();
}

static void create_box_at(float sx, float sy) {
    float wx = (sx - g_ox) / g_zoom, wy = (sy - g_oy) / g_zoom;
    CanvasNode *n = canvas_doc_add_node(&g_doc, CNODE_TEXT);
    n->text = xstrdup("");
    n->w = 260.0f;
    n->h = 140.0f;
    n->x = roundf(wx - n->w * 0.5f);
    n->y = roundf(wy - 20.0f);
    mark_dirty();
    select_node((int)g_doc.node_count - 1);
    open_editor(true);
}

static void set_selection_color(const char *c) {
    char **slot = NULL;
    if (g_sel == SEL_NODE && g_sel_index >= 0 && g_sel_index < (int)g_doc.node_count) {
        slot = &g_doc.nodes[g_sel_index].color;
    } else if (g_sel == SEL_EDGE && g_sel_index >= 0 && g_sel_index < (int)g_doc.edge_count) {
        slot = &g_doc.edges[g_sel_index].color;
    }
    if (!slot) return;
    free(*slot);
    *slot = c ? xstrdup(c) : NULL;
    mark_dirty();
}

/* ---- update ------------------------------------------------------------- */

void canvas_view_update(const CanvasInput *in, int width, int height) {
    if (!g_open) return;
    g_now = in->time;
    g_cursor_x = in->mx;
    g_cursor_y = in->my;

    if (g_pending_crumb >= 0) {
        pop_to(g_pending_crumb);
        g_pending_crumb = -1;
    }
    if (g_pending_go_into >= 0) {
        go_into(g_pending_go_into);
        g_pending_go_into = -1;
    }
    if (g_pending_delete) {
        delete_selection();
        g_pending_delete = false;
    }
    if (g_fit_pending) {
        fit_view(width, height);
        g_fit_pending = false;
    }
    if (g_dirty && g_now - g_dirty_since > SAVE_DEBOUNCE_S) flush();

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

    if (g_mode == MODE_NONE) {
        int prev = g_hover;
        Side unused;
        g_hover = in->over_panel ? -1 : hit_node(in->mx, in->my);
        /* Connection dots straddle the border, so keep the last hovered
         * box while the cursor is on one of its dots just outside it. */
        if (g_hover < 0 && !in->over_panel && hit_handle(prev, in->mx, in->my, &unused)) g_hover = prev;
    }

    if (in->key_delete && !g_edit_active && g_sel != SEL_NONE) delete_selection();

    bool left_edge = in->left && !g_prev_left;
    bool pan_edge = (in->right && !g_prev_right) || (in->middle && !g_prev_middle);

    if (g_mode == MODE_NONE && !in->over_panel && left_edge) {
        bool dbl = g_last_click_time >= 0.0 && in->time - g_last_click_time < DOUBLE_CLICK_S &&
                   fabsf(in->mx - g_last_click_x) < 6.0f && fabsf(in->my - g_last_click_y) < 6.0f;
        g_last_click_time = dbl ? -1.0 : in->time;
        g_last_click_x = in->mx;
        g_last_click_y = in->my;
        g_moved = false;
        g_last_x = in->mx;
        g_last_y = in->my;

        Side side;
        int handle_node = hit_handle(g_hover, in->mx, in->my, &side) ? g_hover
                        : (g_sel == SEL_NODE && hit_handle(g_sel_index, in->mx, in->my, &side)) ? g_sel_index : -1;
        int hit = hit_node(in->mx, in->my);

        if (handle_node >= 0) {
            g_mode = MODE_CONNECT;
            g_mode_index = handle_node;
            g_conn_side = side;
        } else if (g_sel == SEL_NODE && hit_resize(g_sel_index, in->mx, in->my)) {
            g_mode = MODE_RESIZE;
            g_mode_index = g_sel_index;
        } else if (hit >= 0) {
            if (in->shift) {
                go_into(hit);
                g_last_click_time = -1.0;
            } else {
                select_node(hit);
                if (dbl) open_editor(true);
                g_mode = MODE_DRAG;
                g_mode_index = hit;
            }
        } else {
            int e = hit_edge(in->mx, in->my);
            if (e >= 0) {
                select_edge(e);
                if (dbl) open_editor(true);
            } else if (dbl) {
                create_box_at(in->mx, in->my);
            } else {
                clear_selection();
                g_mode = MODE_PAN;
            }
        }
    } else if (g_mode == MODE_NONE && !in->over_panel && pan_edge) {
        g_mode = MODE_PAN;
        g_last_x = in->mx;
        g_last_y = in->my;
    } else if (g_mode != MODE_NONE) {
        bool held = (g_mode == MODE_PAN) ? (in->left || in->right || in->middle) : in->left;
        float dx = in->mx - g_last_x, dy = in->my - g_last_y;
        if (!held) {
            if (g_mode == MODE_CONNECT) {
                int target = hit_node(in->mx, in->my);
                if (target >= 0 && target != g_mode_index && g_doc.nodes[target].type != CNODE_GROUP) {
                    const char *from_id = g_doc.nodes[g_mode_index].id;
                    const char *to_id = g_doc.nodes[target].id;
                    /* Arrive on the target's side nearest where the drag ended. */
                    Side to_side = facing_side(&g_doc.nodes[target], in->mx, in->my);
                    CanvasEdge *e = canvas_doc_add_edge(&g_doc);
                    e->from_node = xstrdup(from_id);
                    e->to_node = xstrdup(to_id);
                    e->from_side = xstrdup(side_name(g_conn_side));
                    e->to_side = xstrdup(side_name(to_side));
                    mark_dirty();
                    select_edge((int)g_doc.edge_count - 1);
                }
            } else if ((g_mode == MODE_DRAG || g_mode == MODE_RESIZE) && g_moved) {
                mark_dirty();
                flush(); /* save as soon as a move/resize lands */
            }
            g_mode = MODE_NONE;
            g_mode_index = -1;
        } else {
            if (fabsf(dx) > 0.0f || fabsf(dy) > 0.0f) g_moved = true;
            if (g_mode == MODE_PAN) {
                g_ox += dx;
                g_oy += dy;
            } else if (g_mode == MODE_DRAG && g_mode_index >= 0) {
                g_doc.nodes[g_mode_index].x += dx / g_zoom;
                g_doc.nodes[g_mode_index].y += dy / g_zoom;
            } else if (g_mode == MODE_RESIZE && g_mode_index >= 0) {
                CanvasNode *n = &g_doc.nodes[g_mode_index];
                n->w = fmaxf(MIN_W, n->w + dx / g_zoom);
                n->h = fmaxf(MIN_H, n->h + dy / g_zoom);
            }
            g_last_x = in->mx;
            g_last_y = in->my;
        }
    }

    g_prev_left = in->left;
    g_prev_right = in->right;
    g_prev_middle = in->middle;
}

/* ---- drawing ------------------------------------------------------------ */

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

static void draw_arrow(struct nk_command_buffer *c, float tipx, float tipy, float fromx, float fromy,
                       struct nk_color col) {
    float dx = tipx - fromx, dy = tipy - fromy;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    dx /= len;
    dy /= len;
    float s = fminf(fmaxf(10.0f * g_zoom, 5.0f), 16.0f);
    nk_fill_triangle(c, tipx, tipy,
                     tipx - dx * s - dy * s * 0.5f, tipy - dy * s + dx * s * 0.5f,
                     tipx - dx * s + dy * s * 0.5f, tipy - dy * s - dx * s * 0.5f, col);
}

static void draw_edge(struct nk_command_buffer *c, int i) {
    const CanvasEdge *e = &g_doc.edges[i];
    Bez b;
    if (!edge_bez(e, &b)) return;

    ThemeColor tc = g_theme.canvas_edge;
    node_color(e->color, &tc);
    bool selected = g_sel == SEL_EDGE && g_sel_index == i;
    struct nk_color col = theme_nk(selected ? g_theme.box_border_selected : tc);
    float thick = fmaxf(1.0f, 1.5f * fminf(g_zoom, 1.5f)) + (selected ? 1.0f : 0.0f);
    nk_stroke_curve(c, b.ax, b.ay, b.c1x, b.c1y, b.c2x, b.c2y, b.bx, b.by, thick, col);

    /* Spec defaults: no arrow at the start, an arrow at the end. */
    if (!e->to_end || strcmp(e->to_end, "none") != 0) draw_arrow(c, b.bx, b.by, b.c2x, b.c2y, col);
    if (e->from_end && strcmp(e->from_end, "arrow") == 0) draw_arrow(c, b.ax, b.ay, b.c1x, b.c1y, col);

    if (e->label && *e->label) {
        float px = 12.5f * g_zoom;
        if (px < MIN_BODY_PX) return;
        float mx, my;
        bez_at(&b, 0.5f, &mx, &my);
        const struct nk_user_font *f = fonts_sized(FONT_REGULAR, px);
        int n = (int)strlen(e->label);
        float w = f->width(f->userdata, f->height, e->label, n);
        float pad = 4.0f * g_zoom;
        struct nk_rect r = nk_rect(mx - w * 0.5f - pad, my - f->height * 0.5f - pad * 0.5f, w + pad * 2.0f,
                                   f->height + pad);
        nk_fill_rect(c, r, 3.0f * g_zoom, theme_nk(g_theme.canvas_background));
        nk_draw_text(c, nk_rect(mx - w * 0.5f, my - f->height * 0.5f, w + 2.0f, f->height), e->label, n, f,
                     nk_rgba(0, 0, 0, 0), theme_nk(g_theme.canvas_edge_label));
    }
}

/* Scissor to r (intersected with the window) for the duration of a node's
 * text, so nothing spills outside its box. */
static void push_clip(struct nk_command_buffer *c, struct nk_rect r, struct nk_rect window_clip) {
    float x0 = fmaxf(r.x, window_clip.x), y0 = fmaxf(r.y, window_clip.y);
    float x1 = fminf(r.x + r.w, window_clip.x + window_clip.w);
    float y1 = fminf(r.y + r.h, window_clip.y + window_clip.h);
    nk_push_scissor(c, nk_rect(x0, y0, fmaxf(x1 - x0, 0.0f), fmaxf(y1 - y0, 0.0f)));
}

static void draw_group(struct nk_command_buffer *c, int i, struct nk_rect window_clip) {
    const CanvasNode *n = &g_doc.nodes[i];
    struct nk_rect r = rect_of(n);
    ThemeColor tc = g_theme.box_border;
    node_color(n->color, &tc);
    float rounding = 8.0f * fminf(g_zoom, 1.5f);
    nk_fill_rect(c, r, rounding, with_alpha(tc, 28));
    bool selected = g_sel == SEL_NODE && g_sel_index == i;
    nk_stroke_rect(c, r, rounding, selected ? 2.5f : 1.5f, theme_nk(selected ? g_theme.box_border_selected : tc));
    if (n->label && *n->label) {
        float px = fmaxf(15.0f * g_zoom, 11.0f);
        struct nk_rect h = group_header(n);
        push_clip(c, h, window_clip);
        const struct nk_user_font *f = fonts_sized(FONT_BOLD, px);
        float pad = 8.0f * fminf(g_zoom, 1.0f);
        nk_draw_text(c, nk_rect(r.x + pad, r.y + (h.h - f->height) * 0.5f, r.w - pad * 2.0f, f->height), n->label,
                     (int)strlen(n->label), f, nk_rgba(0, 0, 0, 0), theme_nk(g_theme.box_heading));
        nk_push_scissor(c, window_clip);
    }
}

static void draw_box(struct nk_command_buffer *c, int i, struct nk_rect window_clip) {
    const CanvasNode *n = &g_doc.nodes[i];
    struct nk_rect r = rect_of(n);
    if (r.x > window_clip.x + window_clip.w || r.y > window_clip.y + window_clip.h ||
        r.x + r.w < window_clip.x || r.y + r.h < window_clip.y) return;

    float rounding = 6.0f * fminf(g_zoom, 1.5f);
    ThemeColor border = g_theme.box_border;
    node_color(n->color, &border);
    nk_fill_rect(c, r, rounding, theme_nk(g_theme.box_fill));
    nk_stroke_rect(c, r, rounding, 2.0f, theme_nk(border));
    if (g_sel == SEL_NODE && g_sel_index == i) {
        struct nk_rect o = nk_rect(r.x - 3.0f, r.y - 3.0f, r.w + 6.0f, r.h + 6.0f);
        nk_stroke_rect(c, o, rounding + 3.0f, 1.5f, theme_nk(g_theme.box_border_selected));
    }

    /* Non-text nodes show what they point at, as markdown. */
    char tmp[1200];
    const char *md = n->text ? n->text : "";
    if (n->type == CNODE_FILE) {
        const char *f = n->file ? n->file : "";
        const char *base = strrchr(f, '/');
        snprintf(tmp, sizeof(tmp), "### %s\n`%s%s`", base ? base + 1 : f, f, n->subpath ? n->subpath : "");
        md = tmp;
    } else if (n->type == CNODE_LINK) {
        snprintf(tmp, sizeof(tmp), "### Link\n%s", n->url ? n->url : "");
        md = tmp;
    }

    float pad = BOX_PAD * g_zoom;
    struct nk_rect inner = nk_rect(r.x + pad, r.y + pad, r.w - pad * 2.0f, r.h - pad * 2.0f);
    if (inner.w > 4.0f && inner.h > 4.0f) {
        push_clip(c, r, window_clip);
        if (14.0f * g_zoom >= MIN_BODY_PX) {
            md_render_draw(c, inner, md, g_zoom);
        } else {
            /* Zoomed far out: title only, kept at a readable size as long
             * as the box is tall enough to hold it. */
            float px = fmaxf(22.0f * g_zoom, 11.0f);
            float tpad = fminf(pad, 4.0f);
            struct nk_rect t = nk_rect(r.x + tpad, r.y + tpad, r.w - tpad * 2.0f, r.h - tpad * 2.0f);
            if (t.h >= px && t.w >= 24.0f) md_render_title(c, t, md, px);
        }
        nk_push_scissor(c, window_clip);
    }

    /* A canvas link: small corner marker so it's obvious shift+click goes somewhere. */
    char *link = canvas_link_of(n);
    if (link) {
        free(link);
        float s = fminf(fmaxf(9.0f * g_zoom, 5.0f), 12.0f);
        nk_fill_triangle(c, r.x + r.w - s - 4.0f, r.y + 4.0f, r.x + r.w - 4.0f, r.y + 4.0f, r.x + r.w - 4.0f,
                         r.y + 4.0f + s, theme_nk(border));
    }
}

static void draw_handles(struct nk_command_buffer *c, int i) {
    if (!handles_visible(i)) return;
    for (int s = 0; s < 4; s++) {
        float hx, hy, nx, ny;
        side_point(&g_doc.nodes[i], (Side)s, &hx, &hy, &nx, &ny);
        struct nk_rect r = nk_rect(hx - HANDLE_R, hy - HANDLE_R, HANDLE_R * 2.0f, HANDLE_R * 2.0f);
        nk_fill_circle(c, r, theme_nk(g_theme.box_fill));
        nk_stroke_circle(c, r, 1.5f, theme_nk(g_theme.box_border_selected));
    }
}

static void draw_resize_grip(struct nk_command_buffer *c, int i) {
    struct nk_rect r = rect_of(&g_doc.nodes[i]);
    struct nk_color col = theme_nk(g_theme.box_border_selected);
    float x = r.x + r.w - 4.0f, y = r.y + r.h - 4.0f;
    nk_stroke_line(c, x - 10.0f, y, x, y - 10.0f, 1.5f, col);
    nk_stroke_line(c, x - 5.0f, y, x, y - 5.0f, 1.5f, col);
}

static void draw_text_at(struct nk_command_buffer *c, float x, float y, const char *s, ThemeColor color) {
    const struct nk_user_font *f = fonts_ui();
    int n = (int)strlen(s);
    float w = f->width(f->userdata, f->height, s, n);
    nk_draw_text(c, nk_rect(x, y, w + 2.0f, f->height), s, n, f, nk_rgba(0, 0, 0, 0), theme_nk(color));
}

static void draw_canvas_layer(struct nk_context *ctx, int width, int height) {
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
        for (size_t i = 0; i < g_doc.node_count; i++) {
            if (g_doc.nodes[i].type == CNODE_GROUP) draw_group(c, (int)i, clip);
        }
        for (size_t e = 0; e < g_doc.edge_count; e++) draw_edge(c, (int)e);
        for (size_t i = 0; i < g_doc.node_count; i++) {
            if (g_doc.nodes[i].type != CNODE_GROUP) draw_box(c, (int)i, clip);
        }

        if (g_mode == MODE_CONNECT && g_mode_index >= 0) {
            float ax, ay, nx, ny;
            side_point(&g_doc.nodes[g_mode_index], g_conn_side, &ax, &ay, &nx, &ny);
            Bez b;
            make_bez(ax, ay, nx, ny, g_cursor_x, g_cursor_y, 0, 0, &b);
            struct nk_color col = theme_nk(g_theme.box_border_selected);
            nk_stroke_curve(c, b.ax, b.ay, b.c1x, b.c1y, b.c2x, b.c2y, b.bx, b.by, 1.5f, col);
        }
        if (g_mode == MODE_NONE && g_hover >= 0) draw_handles(c, g_hover);
        if (g_sel == SEL_NODE && g_sel_index >= 0 && g_sel_index < (int)g_doc.node_count) {
            draw_handles(c, g_sel_index);
            draw_resize_grip(c, g_sel_index);
        }

        if (g_doc.node_count == 0) {
            const char *hint = "Empty canvas \xE2\x80\x94 double-click anywhere to add a box";
            const struct nk_user_font *f = fonts_ui();
            float w = f->width(f->userdata, f->height, hint, (int)strlen(hint));
            draw_text_at(c, ((float)width - w) * 0.5f, (float)height * 0.5f, hint, g_theme.canvas_edge_label);
        }

        char hud[160];
        snprintf(hud, sizeof(hud),
                 "%.0f%%   \xC2\xB7   double-click: add / edit   \xC2\xB7   shift+click: go in   \xC2\xB7   "
                 "Esc: up",
                 g_zoom * 100.0f);
        float fh = fonts_ui()->height;
        draw_text_at(c, 12.0f, (float)height - fh - 12.0f, hud, g_theme.canvas_edge_label);
        if (g_load_failed) {
            draw_text_at(c, 12.0f, (float)height - fh * 2.0f - 18.0f,
                         "This canvas file could not be read \xE2\x80\x94 changes here will not be saved.",
                         g_theme.box_preset[0]);
        }
    }
    nk_end(ctx);

    nk_style_pop_vec2(ctx);
    nk_style_pop_style_item(ctx);
}

static void draw_breadcrumbs(struct nk_context *ctx, int width, PanelRect *out) {
    const struct nk_user_font *f = fonts_ui();
    const float row_h = 22.0f, sep_w = 12.0f;
    struct nk_vec2 pad = ctx->style.window.padding;
    float spacing = ctx->style.window.spacing.x;

    char labels[MAX_DEPTH][48];
    float widths[MAX_DEPTH];
    float total = 0.0f;
    for (int i = 0; i < g_depth; i++) {
        snprintf(labels[i], sizeof(labels[i]), "%s", g_nav[i].title);
        widths[i] = f->width(f->userdata, f->height, labels[i], (int)strlen(labels[i])) + 18.0f;
        total += widths[i];
        if (i) total += sep_w + spacing * 2.0f;
    }
    float bw = total + pad.x * 2.0f + 6.0f;
    float bh = row_h + pad.y * 2.0f + 4.0f;
    struct nk_rect bounds = nk_rect(((float)width - bw) * 0.5f, 10.0f, bw, bh);
    /* Recentred every frame as the trail changes length. */
    nk_window_set_bounds(ctx, CRUMBS_TITLE, bounds);
    if (nk_begin(ctx, CRUMBS_TITLE, bounds, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        struct nk_rect b = nk_window_get_bounds(ctx);
        *out = (PanelRect){ b.x, b.y, b.w, b.h };
        nk_layout_row_begin(ctx, NK_STATIC, row_h, g_depth * 2 - 1);
        for (int i = 0; i < g_depth; i++) {
            if (i) {
                nk_layout_row_push(ctx, sep_w);
                nk_label(ctx, "\xE2\x80\xBA", NK_TEXT_CENTERED);
            }
            nk_layout_row_push(ctx, widths[i]);
            if (i == g_depth - 1) {
                nk_label(ctx, labels[i], NK_TEXT_CENTERED);
            } else if (nk_button_label(ctx, labels[i])) {
                g_pending_crumb = i;
            }
        }
        nk_layout_row_end(ctx);
    }
    nk_end(ctx);
}

static void draw_editor(struct nk_context *ctx, int width, int height, PanelRect *out) {
    char **target = editor_target_string();
    if (!g_editor_open || !target) {
        g_editor_open = false;
        g_edit_active = false;
        *out = (PanelRect){ 0, 0, 0, 0 };
        return;
    }
    bool is_edge = g_sel == SEL_EDGE;
    CanvasNode *node = is_edge ? NULL : &g_doc.nodes[g_sel_index];

    struct nk_rect initial = nk_rect((float)width - 460.0f, 60.0f, 440.0f, fmaxf((float)height - 120.0f, 300.0f));
    if (nk_begin(ctx, EDITOR_TITLE, initial,
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE)) {
        struct nk_rect b = nk_window_get_bounds(ctx);
        *out = (PanelRect){ b.x, b.y, b.w, b.h };

        const char *what = is_edge ? "Edge label"
                         : node->type == CNODE_TEXT ? "Markdown"
                         : node->type == CNODE_FILE ? "File path"
                         : node->type == CNODE_LINK ? "URL" : "Group label";
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label(ctx, what, NK_TEXT_LEFT);

        /* Longer than the buffer: show it, but don't let the edit
         * silently truncate the real text. */
        bool too_long = *target && strlen(*target) >= sizeof(g_edit_buf) - 1;
        nk_flags flags = (too_long ? NK_EDIT_READ_ONLY : 0);
        bool multiline = !is_edge && node->type == CNODE_TEXT;
        struct nk_rect content = nk_window_get_content_region(ctx);
        float edit_h = multiline ? fmaxf(content.h - 130.0f, 120.0f) : 30.0f;
        nk_layout_row_dynamic(ctx, edit_h, 1);
        if (g_edit_focus_pending) {
            nk_edit_focus(ctx, 0);
            g_edit_focus_pending = false;
        }
        nk_flags state = nk_edit_string_zero_terminated(ctx, flags | (multiline ? NK_EDIT_BOX : NK_EDIT_FIELD),
                                                        g_edit_buf, (int)sizeof(g_edit_buf), nk_filter_default);
        g_edit_active = (state & NK_EDIT_ACTIVE) != 0;
        if (!too_long && strcmp(g_edit_buf, *target ? *target : "") != 0) {
            free(*target);
            /* An emptied edge/group label is removed rather than kept as "". */
            *target = (g_edit_buf[0] || multiline) ? xstrdup(g_edit_buf) : NULL;
            mark_dirty();
        }
        if (too_long) {
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label(ctx, "Too long to edit here -- edit the .canvas file directly.", NK_TEXT_LEFT);
        }

        nk_layout_row_dynamic(ctx, 20, 1);
        const char *cur = is_edge ? g_doc.edges[g_sel_index].color : node->color;
        char color_label[48];
        snprintf(color_label, sizeof(color_label), "Colour: %s", cur ? cur : "none");
        nk_label(ctx, color_label, NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 24, 7);
        if (nk_button_label(ctx, "none")) set_selection_color(NULL);
        for (int k = 0; k < 6; k++) {
            if (nk_button_color(ctx, theme_nk(g_theme.box_preset[k]))) {
                char c[2] = { (char)('1' + k), '\0' };
                set_selection_color(c);
            }
        }

        char *link = node ? canvas_link_of(node) : NULL;
        nk_layout_row_dynamic(ctx, 26, link ? 3 : 2);
        if (link && nk_button_label(ctx, "Go into")) g_pending_go_into = g_sel_index;
        free(link);
        if (nk_button_label(ctx, "Delete")) g_pending_delete = true;
        if (nk_button_label(ctx, "Close")) {
            g_editor_open = false;
            g_edit_active = false;
        }
    }
    nk_end(ctx);
}

void canvas_view_draw(struct nk_context *ctx, int width, int height, PanelRect *out_crumbs, PanelRect *out_editor) {
    *out_crumbs = (PanelRect){ 0, 0, 0, 0 };
    *out_editor = (PanelRect){ 0, 0, 0, 0 };
    if (!g_open) return;
    draw_canvas_layer(ctx, width, height);
    draw_breadcrumbs(ctx, width, out_crumbs);
    draw_editor(ctx, width, height, out_editor);
}
