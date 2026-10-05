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
#include "textarea.h"
#include "fonts.h"
#include "theme.h"
#include "winstate.h"
#include "../canvas/canvas_doc.h"
#include "../canvas/project.h"
#include "../canvas/canvas_index.h"
#include "../common/pathutil.h"
#include "tinyfiledialogs.h"
#include <math.h>
#include <stdint.h>
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
#define GROUP_MIN_DRAG 12.0f   /* screen px each way before a drag draws a group */
#define EDITOR_TITLE "Edit"
#define CRUMBS_TITLE "##breadcrumbs"
#define EDIT_BUF_SIZE 32768
#define SEARCH_TITLE "Search"
#define SEARCH_MAX 200
#define REF_CACHE 16

typedef enum { SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM, SIDE_LEFT, SIDE_AUTO } Side;
typedef enum { MODE_NONE, MODE_PAN, MODE_DRAG, MODE_RESIZE, MODE_CONNECT, MODE_GROUP } Mode;
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
static float g_group_x, g_group_y;   /* MODE_GROUP: where the drag started, screen px */
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
static char g_edit_buf[EDIT_BUF_SIZE];     /* single-line fields: labels, paths, URLs */
static TextArea g_edit_md;                 /* a text box's markdown: wrapped, editable */
static bool g_edit_md_ready = false;

/* The editor's markdown area, created on first use. */
static TextArea *edit_md(void) {
    if (!g_edit_md_ready) {
        textarea_init(&g_edit_md, TEXTAREA_EDITABLE);
        g_edit_md_ready = true;
    }
    return &g_edit_md;
}

/* Actions requested from the editor/breadcrumb widgets during draw, run at
 * the start of the next update so the doc never changes mid-draw. */
static int g_pending_crumb = -1;
static int g_pending_go_into = -1;
static int g_pending_open_code = -1;
static bool g_pending_delete = false;

/* Code mode: a box's folders are showing in the 3D explorer (main.c owns
 * that); the canvas is hidden but its trail stays, with one extra crumb. */
static bool g_in_code = false;
static char *g_code_title = NULL;

/* Folders queued by shift+click / "Open code", waiting for main.c to take
 * them (canvas_view_take_code_request). */
static char **g_code_req_dirs = NULL;
static size_t g_code_req_count = 0;
static char *g_code_req_title = NULL;

/* Last window size seen by update -- for centring on a jumped-to box. */
static int g_view_w = 1280, g_view_h = 800;

/* Every reachable canvas and box (canvas_index.h), built on demand and
 * rebuilt each time search opens. */
static CanvasIndex g_index;
static bool g_index_built = false;

static bool g_search_open = false;
static bool g_search_focus_pending = false;
static bool g_search_active = false;   /* query field has focus */
static char g_search_buf[256];
static char g_search_last[256];
static size_t g_results[SEARCH_MAX];
static size_t g_result_count = 0;
static long g_pending_jump_entry = -1;
static long g_pending_link_entry = -1;
static int g_pending_jump_original = -1;

/* Other canvases that weak links on the current one point into, loaded
 * lazily and dropped whenever a canvas loads. Keyed on the link's file
 * string as written, which is unambiguous per canvas. */
typedef struct {
    char *raw;
    char *path;
    bool ok;
    CanvasDoc doc;
} RefDoc;
static RefDoc g_refs[REF_CACHE];
static int g_ref_count = 0;

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

/* The canvas shift+click goes into: the node's first canvas link (see
 * canvas_node_canvas_links -- weak links don't count). malloc'd; NULL if
 * there isn't one. */
static char *canvas_link_of(const CanvasNode *n) {
    char **links;
    size_t count = canvas_node_canvas_links(n, &links);
    char *first = count ? links[0] : NULL;
    for (size_t i = 1; i < count; i++) free(links[i]);
    free(links);
    return first;
}

/* The existing folders this node links to -- [[targets]] in a text node
 * (or a file node's path) that resolve, relative to the current canvas, to
 * a directory. Returns the count; *out is a malloc'd array of malloc'd
 * absolute paths (NULL when 0). */
static size_t dir_links_of(const CanvasNode *n, char ***out) {
    *out = NULL;
    size_t count = 0, cap = 0;
    const char *single = NULL;
    if (n->type == CNODE_FILE) single = n->file;
    else if (n->type != CNODE_TEXT || !n->text) return 0;

    const char *p = single ? NULL : n->text;
    for (;;) {
        const char *target;
        size_t len;
        if (single) {
            target = single;
            len = strlen(single);
        } else {
            p = strstr(p, "[[");
            if (!p) break;
            p += 2;
            const char *end = strstr(p, "]]");
            if (!end) break;
            len = 0;
            while (p + len < end && p[len] != '|' && p[len] != '#') len++;
            target = p;
            p = end + 2;
        }
        if (len > 0 && !ends_with_ci(target, len, ".canvas")) {
            char *raw = (char *)malloc(len + 1);
            memcpy(raw, target, len);
            raw[len] = '\0';
            char *path = project_resolve_link(cur_path(), raw);
            free(raw);
            bool dup = false;
            for (size_t i = 0; i < count; i++) dup = dup || strcmp((*out)[i], path) == 0;
            if (!dup && path_is_dir(path)) {
                if (count == cap) {
                    cap = cap ? cap * 2 : 4;
                    *out = (char **)realloc(*out, cap * sizeof(char *));
                }
                (*out)[count++] = path;
            } else {
                free(path);
            }
        }
        if (single) break;
    }
    return count;
}

static void free_dirs(char **dirs, size_t count) {
    for (size_t i = 0; i < count; i++) free(dirs[i]);
    free(dirs);
}

/* dir_links_of resolves and stats every link -- too much to redo for each
 * box every frame just to draw a marker -- so the count is cached per node
 * index, keyed on a hash of what it was computed from. Cleared whenever a
 * canvas loads (links are relative to it). */
typedef struct {
    uint64_t key;
    int count;
    bool valid;
} DirCountCache;
static DirCountCache *g_dir_cache = NULL;
static size_t g_dir_cache_cap = 0;

static uint64_t node_link_key(const CanvasNode *n) {
    const char *s = n->type == CNODE_FILE ? n->file : n->text;
    uint64_t h = 1469598103934665603ULL ^ (uint64_t)n->type;
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    return h;
}

static int cached_dir_count(int i) {
    if (g_dir_cache_cap < g_doc.node_count) {
        size_t cap = g_doc.node_count * 2;
        g_dir_cache = (DirCountCache *)realloc(g_dir_cache, cap * sizeof(DirCountCache));
        memset(g_dir_cache + g_dir_cache_cap, 0, (cap - g_dir_cache_cap) * sizeof(DirCountCache));
        g_dir_cache_cap = cap;
    }
    const CanvasNode *n = &g_doc.nodes[i];
    uint64_t key = node_link_key(n);
    DirCountCache *c = &g_dir_cache[i];
    if (!c->valid || c->key != key) {
        char **dirs;
        size_t count = dir_links_of(n, &dirs);
        free_dirs(dirs, count);
        c->key = key;
        c->count = (int)count;
        c->valid = true;
    }
    return c->count;
}

static void clear_dir_cache(void) {
    if (g_dir_cache) memset(g_dir_cache, 0, g_dir_cache_cap * sizeof(DirCountCache));
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

static void clear_refs(void) {
    for (int i = 0; i < g_ref_count; i++) {
        free(g_refs[i].raw);
        free(g_refs[i].path);
        canvas_doc_free(&g_refs[i].doc);
    }
    g_ref_count = 0;
}

static void invalidate_index(void) {
    if (g_index_built) canvas_index_free(&g_index);
    g_index_built = false;
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
    clear_dir_cache();
    clear_refs();
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
    /* zoom 0: a level that was never viewed (pushed by a jump) -- fit it. */
    if (g_nav[index].zoom > 0.0f) {
        g_zoom = g_nav[index].zoom;
        g_ox = g_nav[index].ox;
        g_oy = g_nav[index].oy;
    } else {
        g_fit_pending = true;
    }
}

static void ensure_index(void) {
    if (g_index_built) return;
    flush(); /* the index reads from disk */
    canvas_index_build(&g_index, g_nav[0].path, g_nav[0].title);
    g_index_built = true;
}

static void push_level(const char *path, const char *title) {
    if (g_depth >= MAX_DEPTH) return;
    g_nav[g_depth].path = xstrdup(path);
    g_nav[g_depth].title = xstrdup(title);
    g_nav[g_depth].zoom = 0.0f;
    g_depth++;
}

static void center_on_node(int i) {
    const CanvasNode *n = &g_doc.nodes[i];
    g_ox = (float)g_view_w * 0.5f - (n->x + n->w * 0.5f) * g_zoom;
    g_oy = (float)g_view_h * 0.5f - (n->y + n->h * 0.5f) * g_zoom;
    g_sel = SEL_NODE;
    g_sel_index = i;
    g_editor_open = false;
}

/* Shows node_id on the canvas at canvas_path, rebuilding the breadcrumb
 * trail to it from the index (its shortest route from the root). */
static void jump_to(const char *canvas_path, const char *node_id) {
    if (strcmp(canvas_path, cur_path()) != 0) {
        ensure_index();
        int chain[MAX_DEPTH];
        int len = 0;
        int ci = canvas_index_find_canvas(&g_index, canvas_path);
        for (int c = ci; c >= 0 && len < MAX_DEPTH; c = g_index.canvases[c].parent) chain[len++] = c;

        flush();
        save_view();
        for (int i = 1; i < g_depth; i++) {
            free(g_nav[i].path);
            free(g_nav[i].title);
        }
        g_depth = 1;
        if (ci >= 0) {
            /* chain runs target -> root; the root is already g_nav[0]. */
            for (int k = len - 2; k >= 0; k--) push_level(g_index.canvases[chain[k]].path, g_index.canvases[chain[k]].title);
        } else if (strcmp(canvas_path, g_nav[0].path) != 0) {
            /* Not reachable from the root (a weak link into some other
             * canvas): one hop, named after the file. */
            const char *base = strrchr(canvas_path, '/');
            push_level(canvas_path, base ? base + 1 : canvas_path);
        }
        load_current();
        g_zoom = 1.0f;
    }
    int idx = canvas_doc_find_node(&g_doc, node_id);
    if (idx >= 0) center_on_node(idx);
    else g_fit_pending = true;
}

/* The box a weak link references, or NULL if it's gone. The pointer is
 * only good until the next canvas load. */
static const CanvasNode *weak_target(const CanvasNode *n) {
    const CanvasDoc *doc = NULL;
    RefDoc *ref = NULL;
    for (int i = 0; i < g_ref_count && !ref; i++) {
        if (strcmp(g_refs[i].raw, n->file) == 0) ref = &g_refs[i];
    }
    if (!ref) {
        if (g_ref_count == REF_CACHE) clear_refs();
        ref = &g_refs[g_ref_count++];
        ref->raw = xstrdup(n->file);
        ref->path = project_resolve_link(cur_path(), n->file);
        /* A link back into this same canvas reads the live doc instead. */
        ref->ok = strcmp(ref->path, cur_path()) != 0 && canvas_doc_read(ref->path, &ref->doc);
        if (!ref->ok) canvas_doc_init(&ref->doc);
    }
    if (strcmp(ref->path, cur_path()) == 0) doc = &g_doc;
    else if (ref->ok) doc = &ref->doc;
    int idx = doc ? canvas_doc_find_node(doc, n->subpath + 1) : -1;
    return idx >= 0 ? &doc->nodes[idx] : NULL;
}

/* Label for the canvas a weak link points into: its crumb title when the
 * index knows it, else the file name without ".canvas". */
static void weak_canvas_label(const CanvasNode *n, char *buf, size_t cap) {
    const char *base = strrchr(n->file, '/');
    base = base ? base + 1 : n->file;
    snprintf(buf, cap, "%s", base);
    size_t len = strlen(buf);
    if (len > 7 && strcmp(buf + len - 7, ".canvas") == 0) buf[len - 7] = '\0';
    if (!g_index_built) return;
    char *path = project_resolve_link(cur_path(), n->file);
    int ci = canvas_index_find_canvas(&g_index, path);
    free(path);
    if (ci >= 0) snprintf(buf, cap, "%s", g_index.canvases[ci].title);
}

static void jump_to_original(int node_index) {
    const CanvasNode *n = &g_doc.nodes[node_index];
    char *path = project_resolve_link(cur_path(), n->file);
    char *id = xstrdup(n->subpath + 1);
    if (path_exists(path)) {
        jump_to(path, id);
    } else {
        char msg[4200];
        snprintf(msg, sizeof(msg), "The referenced canvas no longer exists:\n%s", path);
        tinyfd_messageBox("Codestellation", msg, "ok", "warning", 1);
    }
    free(path);
    free(id);
}

/* Drops a weak link to search result `entry` in the middle of the view. */
static void add_weak_link(size_t entry) {
    ensure_index();
    if (entry >= g_index.entry_count) return;
    const IndexEntry *e = &g_index.entries[entry];
    char *dir = path_dirname(cur_path());
    char *rel = path_relative(dir, g_index.canvases[e->canvas].path);
    free(dir);
    size_t id_len = strlen(e->node_id);
    char *sub = (char *)malloc(id_len + 2);
    sub[0] = '#';
    memcpy(sub + 1, e->node_id, id_len + 1);

    CanvasNode *n = canvas_doc_add_node(&g_doc, CNODE_FILE);
    n->file = rel;
    n->subpath = sub;
    n->w = 280.0f;
    n->h = 160.0f;
    n->x = roundf(((float)g_view_w * 0.5f - g_ox) / g_zoom - n->w * 0.5f);
    n->y = roundf(((float)g_view_h * 0.5f - g_oy) / g_zoom - n->h * 0.5f);
    mark_dirty();
    center_on_node((int)g_doc.node_count - 1);
}

/* Queues node n's folders for main.c to build and show in 3D. */
static void request_code(int node_index) {
    if (node_index < 0 || node_index >= (int)g_doc.node_count) return;
    const CanvasNode *n = &g_doc.nodes[node_index];
    char **dirs;
    size_t count = dir_links_of(n, &dirs);
    if (count == 0) return;
    free_dirs(g_code_req_dirs, g_code_req_count);
    free(g_code_req_title);
    g_code_req_dirs = dirs;
    g_code_req_count = count;
    char buf[64];
    if (n->type == CNODE_TEXT && n->text && md_title_text(n->text, buf, (int)sizeof(buf)) > 0) {
        g_code_req_title = xstrdup(buf);
    } else {
        const char *base = strrchr(dirs[0], '/');
        g_code_req_title = xstrdup(base ? base + 1 : dirs[0]);
    }
}

/* Shift+click: a canvas link wins; otherwise the box's folders open in 3D. */
static void go_into(int node_index) {
    if (node_index < 0 || node_index >= (int)g_doc.node_count) return;
    const CanvasNode *n = &g_doc.nodes[node_index];
    if (canvas_node_is_weak_link(n)) {
        jump_to_original(node_index);
        return;
    }
    char *link = canvas_link_of(n);
    if (!link) {
        request_code(node_index);
        return;
    }

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
    g_in_code = false;
    free(g_code_title);
    g_code_title = NULL;
    free_dirs(g_code_req_dirs, g_code_req_count);
    g_code_req_dirs = NULL;
    g_code_req_count = 0;
    free(g_code_req_title);
    g_code_req_title = NULL;
    invalidate_index();
    clear_refs();
    g_search_open = false;
    g_search_active = false;
}

bool canvas_view_is_open(void) { return g_open; }

void canvas_view_flush(void) {
    if (g_open) flush();
}

bool canvas_view_in_code(void) { return g_open && g_in_code; }

void canvas_view_enter_code(const char *title) {
    if (!g_open) return;
    free(g_code_title);
    g_code_title = xstrdup(title && *title ? title : "code");
    g_in_code = true;
    clear_selection();
}

bool canvas_view_take_code_request(char ***dirs, size_t *count, char **title) {
    if (!g_code_req_dirs) return false;
    *dirs = g_code_req_dirs;
    *count = g_code_req_count;
    *title = g_code_req_title;
    g_code_req_dirs = NULL;
    g_code_req_count = 0;
    g_code_req_title = NULL;
    return true;
}

static void leave_code(void) {
    g_in_code = false;
    free(g_code_title);
    g_code_title = NULL;
}

void canvas_view_open_search(void) {
    if (!g_open || g_in_code) return;
    /* Fresh index each time, so edits since the last search show up. */
    invalidate_index();
    ensure_index();
    g_search_last[0] = '\x01'; /* force a re-run of the current query */
    g_search_open = true;
    g_search_focus_pending = true;
}

void canvas_view_escape(void) {
    if (!g_open) return;
    if (g_in_code) {
        leave_code();
        return;
    }
    if (g_search_open) {
        g_search_open = false;
        g_search_active = false;
        return;
    }
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

/* The sides an edge leaves and arrives on: its written ones, else the
 * ones facing the other box. False if either end's box is missing. */
static bool edge_sides(const CanvasEdge *e, int *ia, int *ib, Side *sa, Side *sb) {
    *ia = canvas_doc_find_node(&g_doc, e->from_node);
    *ib = canvas_doc_find_node(&g_doc, e->to_node);
    if (*ia < 0 || *ib < 0) return false;
    const CanvasNode *a = &g_doc.nodes[*ia], *b = &g_doc.nodes[*ib];
    float acx, acy, bcx, bcy;
    center_of(a, &acx, &acy);
    center_of(b, &bcx, &bcy);
    *sa = side_from(e->from_side);
    *sb = side_from(e->to_side);
    if (*sa == SIDE_AUTO) *sa = facing_side(a, bcx, bcy);
    if (*sb == SIDE_AUTO) *sb = facing_side(b, acx, acy);
    return true;
}

static bool edge_bez(const CanvasEdge *e, Bez *out) {
    int ia, ib;
    Side sa, sb;
    if (!edge_sides(e, &ia, &ib, &sa, &sb)) return false;
    const CanvasNode *a = &g_doc.nodes[ia], *b = &g_doc.nodes[ib];
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
    textarea_set_text(edit_md(), src);
    if (!focus) textarea_blur(edit_md());
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

/* Arrowheads on an edge, as the editor offers them. */
enum { ARROWS_TO, ARROWS_FROM, ARROWS_BOTH, ARROWS_NONE };

static int edge_arrows(const CanvasEdge *e) {
    /* Spec defaults: no arrow at the start, one at the end. */
    bool to = !e->to_end || strcmp(e->to_end, "none") != 0;
    bool from = e->from_end && strcmp(e->from_end, "arrow") == 0;
    return to && from ? ARROWS_BOTH : to ? ARROWS_TO : from ? ARROWS_FROM : ARROWS_NONE;
}

/* Spec defaults are left unwritten, so a plain "->" edge stays as terse
 * in the file as one drawn by hand. */
static void set_edge_arrows(CanvasEdge *e, int arrows) {
    bool to = arrows == ARROWS_TO || arrows == ARROWS_BOTH;
    bool from = arrows == ARROWS_FROM || arrows == ARROWS_BOTH;
    free(e->from_end);
    free(e->to_end);
    e->from_end = from ? xstrdup("arrow") : NULL;
    e->to_end = to ? NULL : xstrdup("none");
    mark_dirty();
}

/* Locked: the edge's sides are written, so it stays put when its boxes
 * move. Either side written counts (other editors may write just one). */
static bool edge_locked(const CanvasEdge *e) { return e->from_side || e->to_side; }

/* Locking pins the sides the edge is drawn on right now; unlocking
 * clears them so it reflows again. */
static void set_edge_locked(CanvasEdge *e, bool lock) {
    int ia, ib;
    Side sa, sb;
    bool resolved = lock && edge_sides(e, &ia, &ib, &sa, &sb);
    free(e->from_side);
    free(e->to_side);
    e->from_side = resolved ? xstrdup(side_name(sa)) : NULL;
    e->to_side = resolved ? xstrdup(side_name(sb)) : NULL;
    mark_dirty();
}

/* ---- update ------------------------------------------------------------- */

void canvas_view_update(const CanvasInput *in, int width, int height) {
    if (!g_open) return;
    g_now = in->time;
    g_cursor_x = in->mx;
    g_cursor_y = in->my;

    if (g_pending_crumb >= 0) {
        if (g_in_code) leave_code();
        pop_to(g_pending_crumb);
        g_pending_crumb = -1;
    }
    if (g_in_code) {
        /* The 3D explorer owns all input while code is showing. */
        g_prev_left = in->left;
        g_prev_right = in->right;
        g_prev_middle = in->middle;
        return;
    }
    g_view_w = width;
    g_view_h = height;
    if (g_pending_go_into >= 0) {
        go_into(g_pending_go_into);
        g_pending_go_into = -1;
    }
    if (g_pending_jump_original >= 0) {
        if (g_pending_jump_original < (int)g_doc.node_count) jump_to_original(g_pending_jump_original);
        g_pending_jump_original = -1;
    }
    if (g_pending_jump_entry >= 0) {
        if ((size_t)g_pending_jump_entry < g_index.entry_count) {
            const IndexEntry *e = &g_index.entries[g_pending_jump_entry];
            char *path = xstrdup(g_index.canvases[e->canvas].path);
            char *id = xstrdup(e->node_id);
            jump_to(path, id);
            free(path);
            free(id);
        }
        g_pending_jump_entry = -1;
    }
    if (g_pending_link_entry >= 0) {
        add_weak_link((size_t)g_pending_link_entry);
        g_pending_link_entry = -1;
    }
    if (g_pending_open_code >= 0) {
        request_code(g_pending_open_code);
        g_pending_open_code = -1;
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

    if (in->key_delete && !g_edit_active && !g_search_active && g_sel != SEL_NONE) delete_selection();

    bool left_edge = in->left && !g_prev_left;
    bool right_edge = in->right && !g_prev_right;
    bool pan_edge = right_edge || (in->middle && !g_prev_middle);

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
    } else if (g_mode == MODE_NONE && !in->over_panel && right_edge && in->ctrl) {
        /* Same chord as grouping in the 3D view. */
        g_mode = MODE_GROUP;
        g_group_x = in->mx;
        g_group_y = in->my;
    } else if (g_mode == MODE_NONE && !in->over_panel && pan_edge) {
        g_mode = MODE_PAN;
        g_last_x = in->mx;
        g_last_y = in->my;
    } else if (g_mode != MODE_NONE) {
        bool held = (g_mode == MODE_PAN) ? (in->left || in->right || in->middle)
                  : (g_mode == MODE_GROUP) ? in->right : in->left;
        float dx = in->mx - g_last_x, dy = in->my - g_last_y;
        if (!held) {
            if (g_mode == MODE_GROUP) {
                /* A click or a twitch isn't a group -- nothing is made. */
                float x0 = fminf(g_group_x, in->mx), y0 = fminf(g_group_y, in->my);
                float w = fabsf(in->mx - g_group_x), h = fabsf(in->my - g_group_y);
                if (w >= GROUP_MIN_DRAG && h >= GROUP_MIN_DRAG) {
                    CanvasNode *n = canvas_doc_add_node(&g_doc, CNODE_GROUP);
                    n->x = roundf((x0 - g_ox) / g_zoom);
                    n->y = roundf((y0 - g_oy) / g_zoom);
                    n->w = roundf(fmaxf(MIN_W, w / g_zoom));
                    n->h = roundf(fmaxf(MIN_H, h / g_zoom));
                    mark_dirty();
                    select_node((int)g_doc.node_count - 1);
                    open_editor(true);
                }
            } else if (g_mode == MODE_CONNECT) {
                int target = hit_node(in->mx, in->my);
                if (target >= 0 && target != g_mode_index && g_doc.nodes[target].type != CNODE_GROUP) {
                    const char *from_id = g_doc.nodes[g_mode_index].id;
                    const char *to_id = g_doc.nodes[target].id;
                    /* No sides written: the edge reflows as its boxes
                     * move, until it's locked in the editor. */
                    CanvasEdge *e = canvas_doc_add_edge(&g_doc);
                    e->from_node = xstrdup(from_id);
                    e->to_node = xstrdup(to_id);
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

/* Nuklear has no dashed strokes; weak links draw their outline in segments. */
static void stroke_dashed_rect(struct nk_command_buffer *c, struct nk_rect r, float thick, struct nk_color col) {
    const float dash = 7.0f, gap = 5.0f;
    const float xs[4][4] = {
        { r.x, r.y, r.x + r.w, r.y },
        { r.x + r.w, r.y, r.x + r.w, r.y + r.h },
        { r.x + r.w, r.y + r.h, r.x, r.y + r.h },
        { r.x, r.y + r.h, r.x, r.y },
    };
    for (int s = 0; s < 4; s++) {
        float x0 = xs[s][0], y0 = xs[s][1], x1 = xs[s][2], y1 = xs[s][3];
        float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        if (len < 1.0f) continue;
        float ux = (x1 - x0) / len, uy = (y1 - y0) / len;
        for (float t = 0.0f; t < len; t += dash + gap) {
            float t1 = fminf(t + dash, len);
            nk_stroke_line(c, x0 + ux * t, y0 + uy * t, x0 + ux * t1, y0 + uy * t1, thick, col);
        }
    }
}

/* A weak link: dashed outline, a "→ canvas" header, and the referenced
 * box's live text (read-only). */
static void draw_weak_link(struct nk_command_buffer *c, int i, struct nk_rect r, struct nk_rect window_clip) {
    const CanvasNode *n = &g_doc.nodes[i];
    const CanvasNode *t = weak_target(n);
    ThemeColor border = g_theme.box_border;
    if (!node_color(n->color, &border) && t) node_color(t->color, &border);
    nk_fill_rect(c, r, 0.0f, theme_nk(g_theme.box_fill));
    stroke_dashed_rect(c, r, 2.0f, theme_nk(border));
    if (g_sel == SEL_NODE && g_sel_index == i) {
        struct nk_rect o = nk_rect(r.x - 3.0f, r.y - 3.0f, r.w + 6.0f, r.h + 6.0f);
        nk_stroke_rect(c, o, 3.0f, 1.5f, theme_nk(g_theme.box_border_selected));
    }

    float pad = BOX_PAD * g_zoom;
    struct nk_rect inner = nk_rect(r.x + pad, r.y + pad, r.w - pad * 2.0f, r.h - pad * 2.0f);
    if (inner.w <= 4.0f || inner.h <= 4.0f) return;
    push_clip(c, r, window_clip);

    char label[96], header[128];
    weak_canvas_label(n, label, sizeof(label));
    snprintf(header, sizeof(header), "\xE2\x86\x92 %s", label);
    const char *body = t ? (t->type == CNODE_TEXT ? t->text : NULL) : NULL;
    char fallback[160];
    if (t && !body) {
        canvas_node_title(t, fallback, (int)sizeof(fallback));
        body = fallback;
    } else if (!t) {
        body = "*The referenced box no longer exists.*";
    }

    if (14.0f * g_zoom >= MIN_BODY_PX) {
        float hpx = 11.0f * g_zoom;
        const struct nk_user_font *hf = fonts_sized(FONT_REGULAR, hpx);
        nk_draw_text(c, nk_rect(inner.x, inner.y, inner.w, hf->height), header, (int)strlen(header), hf,
                     nk_rgba(0, 0, 0, 0), theme_nk(g_theme.box_link));
        float dy = hf->height * 1.6f;
        md_render_draw(c, nk_rect(inner.x, inner.y + dy, inner.w, inner.h - dy), body, g_zoom);
    } else {
        float px = fmaxf(22.0f * g_zoom, 11.0f);
        float tpad = fminf(pad, 4.0f);
        struct nk_rect tr = nk_rect(r.x + tpad, r.y + tpad, r.w - tpad * 2.0f, r.h - tpad * 2.0f);
        if (tr.h >= px && tr.w >= 24.0f) md_render_title(c, tr, body, px);
    }
    nk_push_scissor(c, window_clip);
}

static void draw_box(struct nk_command_buffer *c, int i, struct nk_rect window_clip) {
    const CanvasNode *n = &g_doc.nodes[i];
    struct nk_rect r = rect_of(n);
    if (r.x > window_clip.x + window_clip.w || r.y > window_clip.y + window_clip.h ||
        r.x + r.w < window_clip.x || r.y + r.h < window_clip.y) return;

    if (canvas_node_is_weak_link(n)) {
        draw_weak_link(c, i, r, window_clip);
        return;
    }

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

    /* Corner markers so it's obvious shift+click goes somewhere: a
     * triangle for a canvas link, "</>" for folders of code. */
    char *link = canvas_link_of(n);
    float marker_x = r.x + r.w - 4.0f;
    if (link) {
        free(link);
        float s = fminf(fmaxf(9.0f * g_zoom, 5.0f), 12.0f);
        nk_fill_triangle(c, r.x + r.w - s - 4.0f, r.y + 4.0f, r.x + r.w - 4.0f, r.y + 4.0f, r.x + r.w - 4.0f,
                         r.y + 4.0f + s, theme_nk(border));
        marker_x -= s + 4.0f;
    }
    if (cached_dir_count(i) > 0 && g_zoom >= 0.3f) {
        const struct nk_user_font *f = fonts_sized(FONT_MONO, fminf(fmaxf(11.0f * g_zoom, 8.0f), 14.0f));
        float w = f->width(f->userdata, f->height, "</>", 3);
        push_clip(c, r, window_clip);
        nk_draw_text(c, nk_rect(marker_x - w, r.y + 3.0f, w + 2.0f, f->height), "</>", 3, f, nk_rgba(0, 0, 0, 0),
                     theme_nk(border));
        nk_push_scissor(c, window_clip);
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
        if (g_mode == MODE_GROUP) {
            struct nk_rect r = nk_rect(fminf(g_group_x, g_cursor_x), fminf(g_group_y, g_cursor_y),
                                       fabsf(g_cursor_x - g_group_x), fabsf(g_cursor_y - g_group_y));
            float rounding = 8.0f * fminf(g_zoom, 1.5f);
            nk_fill_rect(c, r, rounding, with_alpha(g_theme.box_border_selected, 28));
            nk_stroke_rect(c, r, rounding, 1.5f, theme_nk(g_theme.box_border_selected));
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

#ifdef __APPLE__
        const char *mod = "Cmd";
#else
        const char *mod = "Ctrl";
#endif
        char hud[256];
        snprintf(hud, sizeof(hud),
                 "%.0f%%   \xC2\xB7   double-click: add / edit   \xC2\xB7   shift+click: go in / open code   "
                 "\xC2\xB7   %s+right-drag: group   \xC2\xB7   %s+F: search   \xC2\xB7   Esc: up",
                 g_zoom * 100.0f, mod, mod);
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

    /* The canvas trail, plus a final "code" crumb while 3D is showing. */
    int crumbs = g_depth + (g_in_code ? 1 : 0);
    char labels[MAX_DEPTH + 1][64];
    float widths[MAX_DEPTH + 1];
    float total = 0.0f;
    for (int i = 0; i < crumbs; i++) {
        if (i < g_depth) snprintf(labels[i], sizeof(labels[i]), "%s", g_nav[i].title);
        else snprintf(labels[i], sizeof(labels[i]), "</> %s", g_code_title ? g_code_title : "code");
        widths[i] = f->width(f->userdata, f->height, labels[i], (int)strlen(labels[i])) + 18.0f;
        total += widths[i];
        if (i) total += sep_w + spacing * 2.0f;
    }
    const float search_w = 64.0f;
    bool show_search = !g_in_code;
    if (show_search) total += search_w + 10.0f + spacing;
    float bw = total + pad.x * 2.0f + 6.0f;
    float bh = row_h + pad.y * 2.0f + 4.0f;
    struct nk_rect bounds = nk_rect(((float)width - bw) * 0.5f, 10.0f, bw, bh);
    /* Recentred every frame as the trail changes length. */
    nk_window_set_bounds(ctx, CRUMBS_TITLE, bounds);
    if (nk_begin(ctx, CRUMBS_TITLE, bounds, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        struct nk_rect b = nk_window_get_bounds(ctx);
        *out = (PanelRect){ b.x, b.y, b.w, b.h };
        nk_layout_row_begin(ctx, NK_STATIC, row_h, crumbs * 2 - 1 + (show_search ? 2 : 0));
        for (int i = 0; i < crumbs; i++) {
            if (i) {
                nk_layout_row_push(ctx, sep_w);
                nk_label(ctx, "\xE2\x80\xBA", NK_TEXT_CENTERED);
            }
            nk_layout_row_push(ctx, widths[i]);
            if (i == crumbs - 1) {
                nk_label(ctx, labels[i], NK_TEXT_CENTERED);
            } else if (nk_button_label(ctx, labels[i])) {
                g_pending_crumb = i;
            }
        }
        if (show_search) {
            nk_layout_row_push(ctx, 10.0f);
            nk_spacing(ctx, 1);
            nk_layout_row_push(ctx, search_w);
            if (nk_button_label(ctx, "Search")) canvas_view_open_search();
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
        if (g_edit_md_ready) textarea_blur(&g_edit_md);
        *out = (PanelRect){ 0, 0, 0, 0 };
        return;
    }
    bool is_edge = g_sel == SEL_EDGE;
    CanvasNode *node = is_edge ? NULL : &g_doc.nodes[g_sel_index];
    bool weak = node && canvas_node_is_weak_link(node);

    if (winstate_begin(ctx, EDITOR_TITLE, (float)width - 460.0f, 60.0f, 440.0f, fmaxf((float)height - 120.0f, 300.0f),
                       NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE)) {
        struct nk_rect b = nk_window_get_bounds(ctx);
        *out = (PanelRect){ b.x, b.y, b.w, b.h };

        if (weak) {
            /* Weak links aren't edited -- the text belongs to the original. */
            char label[96], msg[160];
            weak_canvas_label(node, label, sizeof(label));
            snprintf(msg, sizeof(msg), "Reference to a box on \xE2\x80\x9C%s\xE2\x80\x9D", label);
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label(ctx, msg, NK_TEXT_LEFT);
            nk_label(ctx, weak_target(node) ? "Edit the original to change its text."
                                            : "The referenced box no longer exists.", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 26, 3);
            if (nk_button_label(ctx, "Jump to original")) g_pending_jump_original = g_sel_index;
            if (nk_button_label(ctx, "Delete")) g_pending_delete = true;
            if (nk_button_label(ctx, "Close")) g_editor_open = false;
            g_edit_active = false;
            nk_end(ctx);
            return;
        }

        const char *what = is_edge ? "Edge label"
                         : node->type == CNODE_TEXT ? "Markdown"
                         : node->type == CNODE_FILE ? "File path"
                         : node->type == CNODE_LINK ? "URL" : "Group label";
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label(ctx, what, NK_TEXT_LEFT);

        bool multiline = !is_edge && node->type == CNODE_TEXT;
        /* Single-line fields: longer than the buffer means show it, but
         * don't let the edit silently truncate the real text. */
        bool too_long = !multiline && *target && strlen(*target) >= sizeof(g_edit_buf) - 1;
        struct nk_rect content = nk_window_get_content_region(ctx);
        float edit_h = multiline ? fmaxf(content.h - 130.0f, 120.0f) : 30.0f;
        nk_layout_row_dynamic(ctx, edit_h, 1);
        if (multiline) {
            TextArea *md = edit_md();
            /* Changed from elsewhere while not being typed in: follow it. */
            if (!md->focused && strcmp(textarea_text(md), *target ? *target : "") != 0) {
                textarea_set_text(md, *target ? *target : "");
            }
            if (g_edit_focus_pending) {
                textarea_focus(md);
                g_edit_focus_pending = false;
            }
            TextAreaResult res;
            textarea_draw(ctx, md, NULL, 0, &res);
            g_edit_active = res.focused;
            if (res.changed && strcmp(textarea_text(md), *target ? *target : "") != 0) {
                free(*target);
                *target = xstrdup(textarea_text(md));
                mark_dirty();
            }
        } else {
            if (g_edit_focus_pending) {
                nk_edit_focus(ctx, 0);
                g_edit_focus_pending = false;
            }
            nk_flags state = nk_edit_string_zero_terminated(ctx, (too_long ? NK_EDIT_READ_ONLY : 0) | NK_EDIT_FIELD,
                                                            g_edit_buf, (int)sizeof(g_edit_buf), nk_filter_default);
            g_edit_active = (state & NK_EDIT_ACTIVE) != 0;
            if (!too_long && strcmp(g_edit_buf, *target ? *target : "") != 0) {
                free(*target);
                /* An emptied edge/group label is removed rather than kept as "". */
                *target = g_edit_buf[0] ? xstrdup(g_edit_buf) : NULL;
                mark_dirty();
            }
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

        if (is_edge) {
            CanvasEdge *e = &g_doc.edges[g_sel_index];
            static const char *const names[] = { "\xE2\x86\x92", "\xE2\x86\x90", "\xE2\x86\x90 \xE2\x86\x92", "none" };
            int cur_arrows = edge_arrows(e);
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label(ctx, "Arrows", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 24, 4);
            for (int k = 0; k < 4; k++) {
                nk_bool on = k == cur_arrows;
                if (nk_selectable_label(ctx, names[k], NK_TEXT_CENTERED, &on) && k != cur_arrows) {
                    set_edge_arrows(e, k);
                }
            }
            nk_layout_row_dynamic(ctx, 24, 1);
            nk_bool locked = edge_locked(e);
            if (nk_checkbox_label(ctx, "Lock sides (don't reflow when boxes move)", &locked)) {
                set_edge_locked(e, locked);
            }
        }

        char *link = node ? canvas_link_of(node) : NULL;
        int ndirs = node ? cached_dir_count(g_sel_index) : 0;
        nk_layout_row_dynamic(ctx, 26, 2 + (link ? 1 : 0) + (ndirs ? 1 : 0));
        if (link && nk_button_label(ctx, "Go into")) g_pending_go_into = g_sel_index;
        if (ndirs && nk_button_label(ctx, "Open code")) g_pending_open_code = g_sel_index;
        free(link);
        if (nk_button_label(ctx, "Delete")) g_pending_delete = true;
        if (nk_button_label(ctx, "Close")) {
            g_editor_open = false;
            g_edit_active = false;
        }
    }
    nk_end(ctx);
}

/* "in Root › A › B" for a result's canvas. */
static void result_trail(int canvas, char *buf, size_t cap) {
    int chain[MAX_DEPTH];
    int len = 0;
    for (int c = canvas; c >= 0 && len < MAX_DEPTH; c = g_index.canvases[c].parent) chain[len++] = c;
    size_t o = (size_t)snprintf(buf, cap, "in ");
    for (int k = len - 1; k >= 0 && o < cap; k--) {
        o += (size_t)snprintf(buf + o, cap - o, "%s%s", g_index.canvases[chain[k]].title, k ? " \xE2\x80\xBA " : "");
    }
}

static void draw_search(struct nk_context *ctx, int width, int height, PanelRect *out) {
    if (!g_search_open) {
        g_search_active = false;
        *out = (PanelRect){ 0, 0, 0, 0 };
        return;
    }
    if (winstate_begin(ctx, SEARCH_TITLE, (float)width * 0.5f - 280.0f, 64.0f, 560.0f, fminf((float)height - 120.0f, 480.0f),
                       NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE)) {
        struct nk_rect b = nk_window_get_bounds(ctx);
        *out = (PanelRect){ b.x, b.y, b.w, b.h };

        nk_layout_row_dynamic(ctx, 28, 1);
        if (g_search_focus_pending) {
            nk_edit_focus(ctx, 0);
            g_search_focus_pending = false;
        }
        nk_flags state = nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER, g_search_buf,
                                                        (int)sizeof(g_search_buf), nk_filter_default);
        g_search_active = (state & NK_EDIT_ACTIVE) != 0;
        if (strcmp(g_search_buf, g_search_last) != 0) {
            ensure_index();
            g_result_count = canvas_index_search(&g_index, g_search_buf, g_results, SEARCH_MAX);
            snprintf(g_search_last, sizeof(g_search_last), "%s", g_search_buf);
        }
        if ((state & NK_EDIT_COMMITED) && g_result_count > 0) g_pending_jump_entry = (long)g_results[0];

        nk_layout_row_dynamic(ctx, 18, 1);
        char count[64];
        if (!g_search_buf[0]) snprintf(count, sizeof(count), "Type to search every canvas (Enter jumps to the first)");
        else snprintf(count, sizeof(count), "%zu result%s%s", g_result_count, g_result_count == 1 ? "" : "s",
                      g_result_count == SEARCH_MAX ? " (showing the first 200)" : "");
        nk_label_colored(ctx, count, NK_TEXT_LEFT, theme_nk(g_theme.canvas_edge_label));

        for (size_t k = 0; k < g_result_count; k++) {
            const IndexEntry *e = &g_index.entries[g_results[k]];
            nk_layout_row_begin(ctx, NK_DYNAMIC, 24, 3);
            nk_layout_row_push(ctx, 0.62f);
            nk_label(ctx, e->title[0] ? e->title : "(untitled)", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 0.14f);
            if (nk_button_label(ctx, "Go")) g_pending_jump_entry = (long)g_results[k];
            nk_layout_row_push(ctx, 0.22f);
            if (nk_button_label(ctx, "Link here")) g_pending_link_entry = (long)g_results[k];
            nk_layout_row_end(ctx);

            char trail[256];
            result_trail(e->canvas, trail, sizeof(trail));
            nk_layout_row_dynamic(ctx, 16, 1);
            nk_label_colored(ctx, trail, NK_TEXT_LEFT, theme_nk(g_theme.canvas_edge_label));
        }
    }
    nk_end(ctx);
}

void canvas_view_draw(struct nk_context *ctx, int width, int height, CanvasPanels *out) {
    out->crumbs = out->editor = out->search = (PanelRect){ 0, 0, 0, 0 };
    if (!g_open) return;
    if (!g_in_code) draw_canvas_layer(ctx, width, height);
    draw_breadcrumbs(ctx, width, &out->crumbs);
    if (!g_in_code) {
        draw_editor(ctx, width, height, &out->editor);
        draw_search(ctx, width, height, &out->search);
    } else {
        g_search_active = false;
    }
}
