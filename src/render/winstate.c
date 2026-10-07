#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"

#include "winstate.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#define WS_MAX 16

typedef struct {
    char title[64];
    struct nk_rect saved;    /* where it reopens */
    bool saved_shaded;
    bool known;              /* saved is real: moved, sized or shaded at some point */
    struct nk_rect seen;     /* last frame's bounds, to spot a change */
    bool seen_shaded;
} WinState;

static WinState g_states[WS_MAX];
static int g_count = 0;
static bool g_loaded = false;
static bool g_dirty = false;
static int g_screen_w = 0, g_screen_h = 0;

/* Alongside the per-project caches (code_cache.c), same home lookup. */
static const char *state_path(void) {
    static char path[4096];
    const char *home = getenv("HOME");
#if defined(_WIN32)
    if (!home) home = getenv("USERPROFILE");
#endif
    if (!home) return NULL;
    snprintf(path, sizeof(path), "%s/Library/Application Support/Codestellation/windows.txt", home);
    return path;
}

static WinState *find(const char *title, bool add) {
    for (int i = 0; i < g_count; i++) {
        if (strcmp(g_states[i].title, title) == 0) return &g_states[i];
    }
    if (!add || g_count == WS_MAX) return NULL;
    WinState *s = &g_states[g_count++];
    memset(s, 0, sizeof(*s));
    snprintf(s->title, sizeof(s->title), "%s", title);
    return s;
}

/* One window per line: "x y w h shaded title" -- the title last, since it
 * may hold spaces. */
static void load(void) {
    g_loaded = true;
    const char *path = state_path();
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        float x, y, w, h;
        int shaded;
        char title[64];
        if (sscanf(line, "%f %f %f %f %d %63[^\r\n]", &x, &y, &w, &h, &shaded, title) != 6) continue;
        WinState *s = find(title, true);
        if (!s) break;
        s->saved = nk_rect(x, y, w, h);
        s->saved_shaded = shaded != 0;
        s->known = true;
    }
    fclose(f);
}

static void save(void) {
    const char *path = state_path();
    if (!path) return;
    /* The folder is normally there already (project caches live in it). */
    char dir[4096];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
#if defined(_WIN32)
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "warning: could not save window positions to %s\n", path);
        return;
    }
    for (int i = 0; i < g_count; i++) {
        const WinState *s = &g_states[i];
        if (!s->known) continue;
        fprintf(f, "%.0f %.0f %.0f %.0f %d %s\n", s->saved.x, s->saved.y, s->saved.w, s->saved.h,
                s->saved_shaded ? 1 : 0, s->title);
    }
    fclose(f);
}

void winstate_set_screen(int width, int height) {
    g_screen_w = width;
    g_screen_h = height;
}

/* Enough of the title bar on screen to grab it again. */
static bool reachable(struct nk_rect r) {
    if (r.w < 40.0f || r.h < 20.0f) return false;
    if (g_screen_w <= 0 || g_screen_h <= 0) return true;
    return r.x < (float)g_screen_w - 40.0f && r.x + r.w > 40.0f && r.y >= -5.0f && r.y < (float)g_screen_h - 20.0f;
}

static bool moved(struct nk_rect a, struct nk_rect b) {
    return fabsf(a.x - b.x) > 0.5f || fabsf(a.y - b.y) > 0.5f || fabsf(a.w - b.w) > 0.5f || fabsf(a.h - b.h) > 0.5f;
}

bool winstate_begin(struct nk_context *ctx, const char *title, float x, float y, float w, float h,
                    unsigned flags) {
    if (!g_loaded) load();
    WinState *s = find(title, true);
    struct nk_rect r = nk_rect(x, y, w, h);
    nk_flags fl = (nk_flags)flags;
    bool fresh = nk_window_find(ctx, title) == NULL;
    if (fresh && s && s->known && reachable(s->saved)) {
        r = s->saved;
        /* Only on creation: from then on nuklear keeps the bit itself and
         * the user's shade/unshade owns it. */
        if (s->saved_shaded && (fl & NK_WINDOW_MINIMIZABLE)) fl |= NK_WINDOW_MINIMIZED;
    }

    bool open = nk_begin(ctx, title, r, fl) != 0;
    if (!s) return open;

    /* Bounds stay the restored size while shaded (see panel_rect.h). */
    struct nk_rect b = nk_window_get_bounds(ctx);
    bool shaded = nk_window_is_collapsed(ctx, title) != 0;
    if (fresh) {
        s->seen = b;
        s->seen_shaded = shaded;
    } else if (moved(b, s->seen) || shaded != s->seen_shaded) {
        s->seen = b;
        s->seen_shaded = shaded;
        s->saved = b;
        s->saved_shaded = shaded;
        s->known = true;
        g_dirty = true;
    }
    /* Not mid-drag: one write when the move/resize lands. */
    if (g_dirty && !ctx->input.mouse.buttons[NK_BUTTON_LEFT].down) {
        save();
        g_dirty = false;
    }
    return open;
}
