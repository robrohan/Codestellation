/* codemap-view: loads a graph.json (see graph/graph_json.h), lays it out
 * in 3D, and renders it as a navigable point/line scene with a Nuklear
 * side panel for inspecting a selected file's contents. Properties > Open
 * builds a new project's graph in-process (see project_build.h) and swaps
 * it in without restarting. */

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "gl_compat.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_GLFW_GL3_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_glfw_gl3.h"

#include "mat4.h"
#include "camera.h"
#include "layout3d.h"
#include "gl_scene.h"
#include "dircolor.h"
#include "picking.h"
#include "ui_panel.h"
#include "note_compose.h"
#include "properties_panel.h"
#include "project_build.h"
#include "tinyfiledialogs.h"
#include "labels.h"
#include "theme.h"
#include "fonts.h"
#include "canvas_view.h"
#include "panel_rect.h"
#include "../common/pathutil.h"
#include "../graph/graph.h"
#include "../graph/graph_json.h"
#include "../notes/filehash.h"
#include "../notes/overlay.h"
#include "../notes/notes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* Sized for the label overlay's worst case: LABELS_MAX_VISIBLE text
 * labels plus the panels, all converted into this one buffer per frame.
 * Nuklear silently truncates the *whole* draw (panels included) when
 * nk_convert overflows, which looked like garbled/half-drawn panels. */
#define MAX_VERTEX_BUFFER (4 * 1024 * 1024)
#define MAX_ELEMENT_BUFFER (1024 * 1024)
#define PICK_RADIUS_PX 10.0f
#define AXIS_LENGTH 4.0f

typedef enum { INTERACT_NONE, INTERACT_ORBIT, INTERACT_DRAG, INTERACT_PAN } InteractMode;

/* Rebuilds the GPU's "highlighted edges" buffer to just the edges
 * touching `node_id` (or clears it if node_id < 0). Only called when
 * the selection actually changes, not every frame -- the edge count is
 * small enough that per-frame rebuilding would be fine too, but there's
 * no reason to. */
static void rebuild_highlighted_edges(GLScene *scene, const Graph *graph, int node_id) {
    if (node_id < 0) {
        gl_scene_set_highlighted_edges(scene, NULL, 0);
        return;
    }
    unsigned int *hi = (unsigned int *)malloc(graph->edge_count * 2 * sizeof(unsigned int));
    size_t count = 0;
    for (size_t e = 0; e < graph->edge_count; e++) {
        if (graph->edges[e].source == node_id || graph->edges[e].target == node_id) {
            hi[count * 2 + 0] = (unsigned int)graph->edges[e].source;
            hi[count * 2 + 1] = (unsigned int)graph->edges[e].target;
            count++;
        }
    }
    gl_scene_set_highlighted_edges(scene, hi, count);
    free(hi);
}

/* Adds node_id to cluster_ids if absent, removes it if present. Returns
 * the new count. Only ever called with node_id >= 0 (a real pick hit), so
 * unsigned int storage matches what gl_scene_set_cluster_points wants
 * with no cast. */
static size_t cluster_toggle(unsigned int **cluster_ids, size_t *cluster_count, size_t *cluster_cap,
                              unsigned int node_id) {
    for (size_t i = 0; i < *cluster_count; i++) {
        if ((*cluster_ids)[i] == node_id) {
            memmove(*cluster_ids + i, *cluster_ids + i + 1, (*cluster_count - i - 1) * sizeof(unsigned int));
            (*cluster_count)--;
            return *cluster_count;
        }
    }
    if (*cluster_count == *cluster_cap) {
        *cluster_cap = *cluster_cap ? *cluster_cap * 2 : 8;
        *cluster_ids = (unsigned int *)realloc(*cluster_ids, *cluster_cap * sizeof(unsigned int));
    }
    (*cluster_ids)[(*cluster_count)++] = node_id;
    return *cluster_count;
}

/* graph.json -> graph.overlay.json / graph.notes.md: sidecar files live
 * next to whatever graph.json was loaded, found by convention rather than
 * a CLI flag. Strips a trailing ".json" if present, then appends suffix. */
static char *derive_sibling_path(const char *graph_path, const char *suffix) {
    size_t len = strlen(graph_path);
    static const char ext[] = ".json";
    size_t ext_len = sizeof(ext) - 1;
    size_t base_len = (len >= ext_len && strcmp(graph_path + len - ext_len, ext) == 0)
                           ? len - ext_len : len;
    size_t suffix_len = strlen(suffix);
    char *out = (char *)malloc(base_len + suffix_len + 1);
    memcpy(out, graph_path, base_len);
    memcpy(out + base_len, suffix, suffix_len + 1); /* + NUL */
    return out;
}

/* Everything derived from one loaded graph.json, torn down and rebuilt as
 * a unit when Properties > Open swaps in a different project. graph_path
 * NULL (zero nodes, no sidecar paths) is the "nothing opened yet" state. */
typedef struct {
    char *graph_path;
    char *overlay_path;
    char *notes_path;
    Graph graph;
    Overlay overlay;
    NoteSet notes;
    Vec3 *positions;
    float *colors;
    unsigned int *edge_indices;
} LoadedGraph;

/* CPU side only -- needs no GL context, so main can call it before the
 * window exists and fail out cleanly. graph_path NULL loads the empty
 * state. Returns false (leaving *lg untouched) only if graph_path was
 * given and couldn't be read. */
static bool loaded_graph_load(LoadedGraph *lg, const char *graph_path) {
    Graph graph;
    if (graph_path) {
        if (!graph_read_json(graph_path, &graph)) return false;
        printf("loaded %zu nodes, %zu edges from %s\n", graph.node_count, graph.edge_count, graph_path);
    } else {
        graph_init(&graph);
    }

    memset(lg, 0, sizeof(*lg));
    lg->graph = graph;
    lg->graph_path = graph_path ? xstrdup(graph_path) : NULL;
    lg->overlay_path = graph_path ? derive_sibling_path(graph_path, ".overlay.json") : NULL;
    lg->notes_path = graph_path ? derive_sibling_path(graph_path, ".notes.md") : NULL;

    if (lg->overlay_path) {
        if (!overlay_read_json(lg->overlay_path, &lg->overlay)) {
            fprintf(stderr, "warning: could not parse %s, ignoring\n", lg->overlay_path);
        }
    } else {
        overlay_init(&lg->overlay);
    }

    if (lg->notes_path) {
        notes_read_md(lg->notes_path, &lg->notes);
    } else {
        notes_init(&lg->notes);
    }

    lg->positions = (Vec3 *)malloc(graph.node_count * sizeof(Vec3));
    layout3d_compute(&lg->graph, lg->positions, 300);

    /* Static per-node color (directory-derived) -- computed once here,
     * never touched again (unlike positions, no per-drag update path). */
    lg->colors = (float *)malloc(graph.node_count * 3 * sizeof(float));
    dircolor_compute(&lg->graph, lg->colors);

    /* Overlay any manually-dragged positions on top of the fresh layout.
     * A hash mismatch (the file changed since the position was saved) is
     * advisory, not blocking -- the position still applies. */
    for (size_t i = 0; i < graph.node_count; i++) {
        const OverlayEntry *e = overlay_find(&lg->overlay, lg->graph.nodes[i].path);
        if (!e) continue;
        lg->positions[i].x = e->x;
        lg->positions[i].y = e->y;
        lg->positions[i].z = e->z;
        uint64_t current_hash;
        if (file_content_hash(lg->graph.nodes[i].path, &current_hash) && current_hash != e->hash) {
            printf("note: %s changed since its position was saved (possibly stale)\n", lg->graph.nodes[i].path);
        }
    }

    lg->edge_indices = (unsigned int *)malloc(graph.edge_count * 2 * sizeof(unsigned int));
    for (size_t i = 0; i < graph.edge_count; i++) {
        lg->edge_indices[i * 2 + 0] = (unsigned int)graph.edges[i].source;
        lg->edge_indices[i * 2 + 1] = (unsigned int)graph.edges[i].target;
    }
    return true;
}

static void loaded_graph_free(LoadedGraph *lg) {
    free(lg->positions);
    free(lg->colors);
    free(lg->edge_indices);
    graph_free(&lg->graph);
    overlay_free(&lg->overlay);
    notes_free(&lg->notes);
    free(lg->graph_path);
    free(lg->overlay_path);
    free(lg->notes_path);
    memset(lg, 0, sizeof(*lg));
}

/* GPU side of switching to lg: replaces the scene's buffers, clears the
 * per-selection overlays (their node ids belonged to the old graph), and
 * re-frames the camera on the new data. */
static void loaded_graph_show(const LoadedGraph *lg, GLScene *scene, Camera *camera) {
    gl_scene_upload(scene, (const float *)lg->positions, lg->colors, lg->graph.node_count,
                    lg->edge_indices, lg->graph.edge_count);
    gl_scene_set_highlighted_edges(scene, NULL, 0);
    gl_scene_set_cluster_points(scene, NULL, 0);

    camera_init(camera);
    /* Fixed-distance default framed empty graphs badly and huge ones
     * worse -- frame the actual data instead. A sphere of `radius`
     * exactly fills the vertical field of view at
     * distance = radius / sin(fovy/2); back off another 20% so
     * boundary nodes aren't clipped right at the frustum edge. */
    float radius = layout3d_bounding_radius(lg->positions, lg->graph.node_count);
    if (radius > 0.1f) camera->distance = (radius / sinf(camera->fovy * 0.5f)) * 1.2f;
}

int main(int argc, char **argv) {
    /* No graph.json is a valid way to launch -- the Properties pane's
     * Open button builds a picked directory and swaps it in at runtime
     * (see project_build.h), so startup only needs to handle "a
     * graph.json was given" vs "nothing was given yet".
     *
     * Xcode's default scheme silently injects "-NSDocumentRevisionsDebugMode
     * YES" into every launched process's argv (a long-standing default for
     * any auto-generated scheme, document-based app or not) -- without
     * this skip, that pair was taken as argv[1] and codemap-view tried to
     * open a "graph.json" literally named "-NSDocumentRevisionsDebugMode"
     * every time it was run from Xcode's Run button, whether or not the
     * scheme's own Arguments tab had anything configured. */
    const char *graph_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-NSDocumentRevisionsDebugMode") == 0) {
            i++; /* also skip its YES/NO value */
            continue;
        }
        graph_path = argv[i];
        break;
    }

    LoadedGraph lg;
    if (!loaded_graph_load(&lg, graph_path)) {
        fprintf(stderr, "error: could not read %s\n", graph_path);
        return 1;
    }
    if (!graph_path) printf("no project loaded -- use Properties > Open to pick a directory\n");

    if (!glfwInit()) {
        fprintf(stderr, "error: glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow *win = glfwCreateWindow(1280, 800, "Codestellation", NULL, NULL);
    if (!win) {
        fprintf(stderr, "error: glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    if (!gl_load(glfwGetProcAddress)) {
        fprintf(stderr, "error: failed to load GL functions\n");
        glfwTerminate();
        return 1;
    }

    struct nk_glfw glfw_nk = { 0 };
    struct nk_context *ctx = nk_glfw3_init(&glfw_nk, win, NK_GLFW3_INSTALL_CALLBACKS);
    theme_apply_panels(ctx);
    {
        /* Bake at the framebuffer's real resolution (see fonts.h). Fixed
         * at startup: dragging the window to a display with a different
         * scale keeps the original bake. */
        int fbw, fbh, ww, wh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        glfwGetWindowSize(win, &ww, &wh);
        struct nk_font_atlas *atlas;
        nk_glfw3_font_stash_begin(&glfw_nk, &atlas);
        fonts_add(atlas, ww > 0 ? (float)fbw / (float)ww : 1.0f);
        nk_glfw3_font_stash_end(&glfw_nk);
        fonts_finish();
        nk_style_set_font(ctx, fonts_ui());
    }

    GLScene scene;
    gl_scene_init(&scene);

    Camera camera;
    loaded_graph_show(&lg, &scene, &camera);

    /* In-flight Properties > Open build, if any -- polled once per frame
     * below; the old graph stays fully usable until the new one is ready. */
    ProjectBuild *build = NULL;

    int selected = -1;
    int drag_node = -1;
    InteractMode interact = INTERACT_NONE;
    double last_mouse_x = 0, last_mouse_y = 0;
    Vec3 drag_plane_normal = { 0, 0, 1 };

    /* Ctrl/Cmd+right-click toggles a node in/out of this set instead of
     * replacing `selected` -- see the group-mode branch below. Rebuilt
     * into `cluster_paths` each frame the count changes (cheap at the
     * node counts this app targets) so ui_panel_draw/gl_scene can take
     * plain path/id arrays without knowing about node-selection internals. */
    unsigned int *cluster_ids = NULL;
    size_t cluster_count = 0, cluster_cap = 0;
    const char **cluster_paths = NULL;
    size_t cluster_paths_cap = 0;

    /* Right-click doubles as both "pan" (drag) and "select" (click with
     * no real movement) -- these track which button started the current
     * pan and whether it has moved enough to count as a drag rather
     * than a click. */
    bool pan_via_right = false;
    bool cluster_modifier_at_press = false;
    double press_x = 0, press_y = 0;
    bool moved_since_press = false;
    bool left_was_down = false;

    /* Double-click-a-panel's-header-to-shade: nuklear's own
     * NK_WINDOW_MINIMIZABLE only adds a click-to-collapse icon in the
     * header, not a double-click-the-header-itself gesture, so this is
     * bolted on ourselves via nk_window_collapse/nk_window_is_collapsed
     * (both public API, no vendor patch needed). */
    double last_click_time = -1.0;
    double last_click_x = 0.0, last_click_y = 0.0;

    /* Populated at the end of each frame from ui_panel_draw/note_compose_draw's
     * own out-params; read at the *start* of the next frame (one-frame
     * latent, same as any immediate-mode UI querying its own last layout)
     * to gate 3D camera/pick interaction -- see over_panel below. Zero-init
     * so the very first frame (before either panel has ever been drawn)
     * correctly treats nothing as covered. */
    PanelRect inspector_bounds = { 0, 0, 0, 0 };
    PanelRect note_bounds = { 0, 0, 0, 0 };
    PanelRect properties_bounds = { 0, 0, 0, 0 };

    /* Properties' "Show origin" checkbox state -- plain int (Nuklear's
     * nk_bool, an int in this build), see properties_panel.h. */
    int show_origin = 1;

    /* Properties' "Canvas view (spike)" checkbox: swaps the 3D graph for
     * the canvas spike (canvas_view.h). Same int convention. */
    int canvas_view = 0;

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

        /* Swap in a finished Open build at the top of the frame, before
         * anything below reads node ids or positions, so no frame ever
         * mixes old and new graph state. Every piece of per-graph
         * interaction state is reset: its node ids belonged to the old
         * graph. */
        if (build && project_build_is_done(build)) {
            char *new_graph_path = project_build_finish(build);
            build = NULL;
            if (new_graph_path) {
                LoadedGraph next;
                if (loaded_graph_load(&next, new_graph_path)) {
                    loaded_graph_free(&lg);
                    lg = next;
                    loaded_graph_show(&lg, &scene, &camera);
                    selected = -1;
                    drag_node = -1;
                    interact = INTERACT_NONE;
                    cluster_count = 0;
                    note_compose_close();
                } else {
                    tinyfd_messageBox("Codestellation", "The project built, but its graph.json could not be read.",
                                      "ok", "error", 1);
                }
                free(new_graph_path);
            }
        }

        fonts_frame_reset();
        nk_glfw3_new_frame(&glfw_nk);

        int width, height;
        glfwGetWindowSize(win, &width, &height);
        float aspect = height > 0 ? (float)width / (float)height : 1.0f;

        float view[16], proj[16], view_proj[16];
        camera_view_matrix(&camera, view);
        mat4_perspective(proj, camera.fovy, aspect, 0.002f, 1000.0f);
        mat4_multiply(view_proj, proj, view);

        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        /* Inspector/Note are now independently movable/resizable floating
         * panels, not a fixed strip pinned to the right edge -- checked
         * against their own tracked bounds from last frame (one-frame
         * latent, same as any immediate-mode UI querying its own last
         * layout), not nuklear's nk_window_is_any_hovered: that considers
         * every non-hidden window, including the always-present,
         * fullscreen NK_WINDOW_NO_INPUT label overlay in labels.c, and so
         * reports "hovered" everywhere, all the time -- which is exactly
         * what broke all 3D-view mouse input the first time this was
         * wired up. */
        bool over_panel = panel_rect_contains(inspector_bounds, (float)mx, (float)my) ||
                           panel_rect_contains(note_bounds, (float)mx, (float)my) ||
                           panel_rect_contains(properties_bounds, (float)mx, (float)my);
        /* The canvas spike takes all non-panel mouse input; the 3D view
         * treats it exactly like the cursor being over a panel. */
        bool block_3d = over_panel || canvas_view;
        int left_state = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT);
        int middle_state = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE);
        int right_state = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT);
        bool pan_button_down = (middle_state == GLFW_PRESS || right_state == GLFW_PRESS);

        /* Gated on the actual press *edge* (left_state == GLFW_PRESS &&
         * !left_was_down), not just "is currently pressed": while
         * over_panel was true, interact stays INTERACT_NONE and the
         * block below re-runs every frame the button is still held.
         * Inspector/Note are movable/resizable now, so their bounds (and
         * over_panel, one-frame-latent by nature) can shift *during* an
         * ongoing drag on the panel itself -- without the edge check, a
         * frame or two into dragging the panel's title bar or resize
         * handle, over_panel could briefly read false against the
         * panel's just-moved bounds and wrongly start an orbit mid-drag,
         * even though the mouse button was never released. Latching the
         * decision to the press edge means it's made once, using
         * over_panel as of that exact frame, and never revisited for the
         * rest of the hold no matter how the panel moves under it
         * afterward. */
        bool left_pressed_edge = (left_state == GLFW_PRESS && !left_was_down);

        if (left_pressed_edge) {
            /* Double-click a panel's header (title bar) to shade/unshade
             * it -- checked against last frame's tracked bounds clipped
             * to just the header strip, same latency as over_panel. Not
             * gated on over_panel itself since that's whole-panel, and
             * this only ever fires within the header sliver anyway. */
            double now = glfwGetTime();
            bool is_double_click = (now - last_click_time) < 0.4 &&
                                    fabs(mx - last_click_x) < 6.0 && fabs(my - last_click_y) < 6.0;
            if (is_double_click) {
                PanelRect insp_hdr = inspector_bounds, note_hdr = note_bounds, props_hdr = properties_bounds;
                insp_hdr.h = note_hdr.h = props_hdr.h = panel_header_height(ctx);
                if (panel_rect_contains(insp_hdr, (float)mx, (float)my)) {
                    nk_window_collapse(ctx, UI_PANEL_TITLE,
                        nk_window_is_collapsed(ctx, UI_PANEL_TITLE) ? NK_MAXIMIZED : NK_MINIMIZED);
                } else if (panel_rect_contains(note_hdr, (float)mx, (float)my)) {
                    nk_window_collapse(ctx, NOTE_COMPOSE_TITLE,
                        nk_window_is_collapsed(ctx, NOTE_COMPOSE_TITLE) ? NK_MAXIMIZED : NK_MINIMIZED);
                } else if (panel_rect_contains(props_hdr, (float)mx, (float)my)) {
                    nk_window_collapse(ctx, PROPERTIES_PANEL_TITLE,
                        nk_window_is_collapsed(ctx, PROPERTIES_PANEL_TITLE) ? NK_MAXIMIZED : NK_MINIMIZED);
                }
                last_click_time = -1.0; /* consumed -- a third click starts a fresh pair, not another toggle */
            } else {
                last_click_time = now;
                last_click_x = mx;
                last_click_y = my;
            }
        }

        if (interact == INTERACT_NONE) {
            /* Left button: always reposition-drag on a node, or orbit on
             * empty space -- never touches selection. */
            if (left_pressed_edge && !block_3d) {
                int hit = pick_nearest_node(view_proj, lg.positions, lg.graph.node_count, width, height,
                                             (float)mx, (float)my, PICK_RADIUS_PX);
                if (hit >= 0) {
                    drag_node = hit;
                    interact = INTERACT_DRAG;
                    Vec3 forward, right, up;
                    camera_basis(&camera, &forward, &right, &up);
                    drag_plane_normal = forward;
                } else {
                    interact = INTERACT_ORBIT;
                }
                last_mouse_x = mx;
                last_mouse_y = my;
            } else if (pan_button_down && !block_3d) {
                interact = INTERACT_PAN;
                pan_via_right = (right_state == GLFW_PRESS);
                cluster_modifier_at_press =
                    glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(win, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                    glfwGetKey(win, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
                press_x = mx;
                press_y = my;
                moved_since_press = false;
                last_mouse_x = mx;
                last_mouse_y = my;
            }
        } else {
            bool should_end = (interact == INTERACT_PAN) ? !pan_button_down : (left_state == GLFW_RELEASE);
            if (should_end) {
                /* A right-click that never moved past the threshold is a
                 * select/deselect click, not a pan -- act on it now,
                 * at release, using the current cursor position. */
                if (interact == INTERACT_PAN && pan_via_right && !moved_since_press) {
                    int hit = pick_nearest_node(view_proj, lg.positions, lg.graph.node_count, width, height,
                                                 (float)mx, (float)my, PICK_RADIUS_PX);
                    if (cluster_modifier_at_press) {
                        /* Building/editing a cluster -- independent of
                         * `selected` and its edge highlight entirely. */
                        if (hit >= 0) {
                            cluster_toggle(&cluster_ids, &cluster_count, &cluster_cap, (unsigned int)hit);
                            gl_scene_set_cluster_points(&scene, cluster_ids, cluster_count);
                        }
                    } else {
                        selected = hit;
                        rebuild_highlighted_edges(&scene, &lg.graph, selected);
                        /* Plain select is the obvious way back out of group
                         * mode -- clear the cluster rather than leaving it
                         * active alongside a new single selection. */
                        cluster_count = 0;
                        gl_scene_set_cluster_points(&scene, NULL, 0);
                    }
                } else if (interact == INTERACT_DRAG && drag_node >= 0) {
                    /* Drag just ended -- persist the new position immediately.
                     * The file is tiny and this is the only save trigger, so
                     * writing on every release (rather than debouncing) keeps
                     * "if I move something it stays there" true with no
                     * separate save step. */
                    uint64_t h;
                    if (file_content_hash(lg.graph.nodes[drag_node].path, &h)) {
                        overlay_set(&lg.overlay, lg.graph.nodes[drag_node].path, h,
                                    lg.positions[drag_node].x, lg.positions[drag_node].y, lg.positions[drag_node].z);
                        overlay_write_json(&lg.overlay, lg.overlay_path);
                    }
                }
                interact = INTERACT_NONE;
                drag_node = -1;
            } else {
                double dx = mx - last_mouse_x;
                double dy = my - last_mouse_y;

                if (interact == INTERACT_ORBIT) {
                    camera_orbit(&camera, (float)dx * -0.005f, (float)dy * -0.005f);
                } else if (interact == INTERACT_PAN) {
                    if (fabs(mx - press_x) > 4.0 || fabs(my - press_y) > 4.0) moved_since_press = true;
                    camera_pan(&camera, (float)dx, (float)dy);
                } else if (interact == INTERACT_DRAG && drag_node >= 0) {
                    float ndc_x = (2.0f * (float)mx / (float)width) - 1.0f;
                    float ndc_y = 1.0f - (2.0f * (float)my / (float)height);
                    Vec3 ray_origin, ray_dir;
                    camera_ray(&camera, ndc_x, ndc_y, aspect, &ray_origin, &ray_dir);
                    Vec3 new_pos;
                    if (ray_plane_intersect(ray_origin, ray_dir, lg.positions[drag_node], drag_plane_normal, &new_pos)) {
                        lg.positions[drag_node] = new_pos;
                        gl_scene_update_positions(&scene, (const float *)lg.positions, lg.graph.node_count);
                    }
                }
                last_mouse_x = mx;
                last_mouse_y = my;
            }
        }
        left_was_down = (left_state == GLFW_PRESS);

        float scroll_y = ctx->input.mouse.scroll_delta.y;
        if (canvas_view) {
            CanvasInput cin = {
                .mx = (float)mx, .my = (float)my,
                .left = left_state == GLFW_PRESS,
                .right = right_state == GLFW_PRESS,
                .middle = middle_state == GLFW_PRESS,
                .scroll = scroll_y,
                .over_panel = over_panel,
            };
            canvas_view_update(&cin, width, height);
        }
        if (scroll_y != 0.0f && !block_3d) {
            /* Proportional-to-distance step feels natural zoomed out, but
             * decays asymptotically and stalls well short of the actual
             * floor once close in -- a minimum absolute step keeps every
             * scroll tick doing something perceptible. */
            float step = camera.distance * 0.1f;
            if (step < 0.01f) step = 0.01f;
            camera_zoom(&camera, scroll_y * step);
        }

        char *picked_dir = NULL;
        bool export_notes_clicked = false;
        {
            const char *sel_path = selected >= 0 ? lg.graph.nodes[selected].path : NULL;
            const char *sel_lang = selected >= 0 ? lg.graph.nodes[selected].language : NULL;

            if (cluster_count > cluster_paths_cap) {
                cluster_paths_cap = cluster_count;
                cluster_paths = (const char **)realloc((void *)cluster_paths, cluster_paths_cap * sizeof(char *));
            }
            for (size_t i = 0; i < cluster_count; i++) cluster_paths[i] = lg.graph.nodes[cluster_ids[i]].path;

            if (canvas_view) {
                /* Inspector/Note describe 3D selections -- hidden here,
                 * with their bounds cleared so they don't block canvas
                 * input from where they used to be. */
                inspector_bounds = (PanelRect){ 0, 0, 0, 0 };
                note_bounds = (PanelRect){ 0, 0, 0, 0 };
            } else {
                ui_panel_draw(ctx, width, height, sel_path, sel_lang, cluster_paths, cluster_count,
                              &lg.notes, lg.notes_path, &inspector_bounds);
                note_compose_draw(ctx, &lg.notes, lg.notes_path, &note_bounds);
            }
            picked_dir = properties_panel_draw(ctx, &show_origin, &canvas_view, lg.notes_path != NULL,
                                                &export_notes_clicked, &properties_bounds);
            if (canvas_view) {
                canvas_view_draw(ctx, width, height);
            } else {
                labels_draw(ctx, width, height, inspector_bounds, note_bounds, properties_bounds,
                            view_proj, lg.positions, &lg.graph, &lg.notes, selected);
            }

            if (build) {
                /* Small status strip, bottom-center. nk_begin only honours
                 * the rect when the window is first created, which is
                 * each time a build starts (it's dropped once not drawn). */
                const float bw = 480.0f, bh = 44.0f;
                if (nk_begin(ctx, "##building", nk_rect(((float)width - bw) * 0.5f, (float)height - bh - 16.0f, bw, bh),
                             NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
                    nk_layout_row_dynamic(ctx, 24, 1);
                    nk_labelf(ctx, NK_TEXT_CENTERED, "Building %s ...", project_build_root(build));
                }
                nk_end(ctx);
            }
        }

        if (picked_dir) {
            /* One build at a time -- the pipeline isn't reentrant. A
             * second Open while one is running is dropped; the status
             * strip already shows what's in progress. */
            if (!build) build = project_build_start(picked_dir);
            free(picked_dir);
        }

        if (export_notes_clicked && lg.notes_path) {
            /* graph.notes.md lives under Application Support, easy to
             * lose track of -- tinyfd_saveFileDialog + a plain copy gets
             * a copy somewhere the user will actually find it. */
            const char *dest = tinyfd_saveFileDialog("Export Notes", "notes.md", 0, NULL, NULL);
            if (dest) {
                FILE *in = fopen(lg.notes_path, "rb");
                FILE *out = in ? fopen(dest, "wb") : NULL;
                bool ok = false;
                if (in && out) {
                    char buf[8192];
                    size_t n;
                    ok = true;
                    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
                    }
                }
                if (in) fclose(in);
                if (out) fclose(out);
                if (!ok) tinyfd_messageBox("Codestellation", "Could not export notes.", "ok", "error", 1);
            }
        }

        int fb_width, fb_height;
        glfwGetFramebufferSize(win, &fb_width, &fb_height);
        glViewport(0, 0, fb_width, fb_height);
        {
            float r, g, b;
            theme_rgb(canvas_view ? g_theme.canvas_background : g_theme.background, &r, &g, &b);
            glClearColor(r, g, b, 1.0f);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (!canvas_view) {
            gl_scene_draw(&scene, view_proj, selected);
            if (show_origin) gl_scene_draw_axis(&scene, view_proj, AXIS_LENGTH);
        }

        nk_glfw3_render(&glfw_nk, NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
        glfwSwapBuffers(win);

    }

    /* A build still running at quit is abandoned rather than joined --
     * a big codebase could take a long while, and process exit ends the
     * worker anyway. Its half-written graph.json only matters if someone
     * later passes that exact file on the command line. */
    gl_scene_destroy(&scene);
    ui_panel_shutdown();
    nk_glfw3_shutdown(&glfw_nk);
    glfwTerminate();

    loaded_graph_free(&lg);
    free(cluster_ids);
    free((void *)cluster_paths);
    return 0;
}
