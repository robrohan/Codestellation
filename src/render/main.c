/* codemap-view: loads a graph.json (see graph/graph_json.h), lays it out
 * in 3D once at startup, and renders it as a navigable point/line scene
 * with a Nuklear side panel for inspecting a selected file's contents. */

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
#include "project_launcher.h"
#include "tinyfiledialogs.h"
#include "labels.h"
#include "panel_rect.h"
#include "../common/pathutil.h"
#include "../graph/graph.h"
#include "../graph/graph_json.h"
#include "../notes/filehash.h"
#include "../notes/overlay.h"
#include "../notes/notes.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define MAX_VERTEX_BUFFER (512 * 1024)
#define MAX_ELEMENT_BUFFER (128 * 1024)
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

/* Resolved (symlink-free, absolute) path to this running executable --
 * used only if the Properties pane's Open button is ever clicked (see
 * project_launcher.h), to find the sibling codemap-build binary and to
 * relaunch. argv[0] alone isn't reliable (can be a bare name found via
 * PATH, or relative to a cwd that's since changed), so macOS gets the
 * real thing via _NSGetExecutablePath; elsewhere this falls back to
 * resolving argv[0] itself, "should work, not verified" like this
 * project's other non-macOS-specific paths. */
static char *resolve_self_exe_path(const char *argv0) {
#if defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        char *resolved = path_normalize(buf);
        return resolved ? resolved : xstrdup(buf);
    }
#endif
    char *resolved = path_normalize(argv0);
    return resolved ? resolved : xstrdup(argv0);
}

int main(int argc, char **argv) {
    /* No graph.json is a valid way to launch now -- the Properties pane's
     * Open button (see project_launcher.h) picks a directory, builds it,
     * and execv()s a fresh copy of this same process with the result as
     * argv[1], so the "real" startup path below only ever needs to
     * handle "a graph.json was given" vs "nothing was given yet". */
    const char *graph_path = (argc >= 2) ? argv[1] : NULL;
    char *self_exe_path = resolve_self_exe_path(argv[0]);

    Graph graph;
    if (graph_path) {
        if (!graph_read_json(graph_path, &graph)) {
            fprintf(stderr, "error: could not read %s\n", graph_path);
            return 1;
        }
        printf("loaded %zu nodes, %zu edges from %s\n", graph.node_count, graph.edge_count, graph_path);
    } else {
        graph_init(&graph);
        printf("no project loaded -- use Properties > Open to pick a directory\n");
    }

    char *overlay_path = graph_path ? derive_sibling_path(graph_path, ".overlay.json") : NULL;
    char *notes_path = graph_path ? derive_sibling_path(graph_path, ".notes.md") : NULL;

    Overlay overlay;
    if (overlay_path) {
        if (!overlay_read_json(overlay_path, &overlay)) {
            fprintf(stderr, "warning: could not parse %s, ignoring\n", overlay_path);
        }
    } else {
        overlay_init(&overlay);
    }

    NoteSet notes;
    if (notes_path) {
        notes_read_md(notes_path, &notes);
    } else {
        notes_init(&notes);
    }

    Vec3 *positions = (Vec3 *)malloc(graph.node_count * sizeof(Vec3));
    layout3d_compute(&graph, positions, 300);

    /* Static per-node color (directory-derived) -- computed once here,
     * never touched again (unlike positions, no per-drag update path). */
    float *colors = (float *)malloc(graph.node_count * 3 * sizeof(float));
    dircolor_compute(&graph, colors);

    /* Overlay any manually-dragged positions on top of the fresh layout.
     * A hash mismatch (the file changed since the position was saved) is
     * advisory, not blocking -- the position still applies. */
    for (size_t i = 0; i < graph.node_count; i++) {
        const OverlayEntry *e = overlay_find(&overlay, graph.nodes[i].path);
        if (!e) continue;
        positions[i].x = e->x;
        positions[i].y = e->y;
        positions[i].z = e->z;
        uint64_t current_hash;
        if (file_content_hash(graph.nodes[i].path, &current_hash) && current_hash != e->hash) {
            printf("note: %s changed since its position was saved (possibly stale)\n", graph.nodes[i].path);
        }
    }

    unsigned int *edge_indices = (unsigned int *)malloc(graph.edge_count * 2 * sizeof(unsigned int));
    for (size_t i = 0; i < graph.edge_count; i++) {
        edge_indices[i * 2 + 0] = (unsigned int)graph.edges[i].source;
        edge_indices[i * 2 + 1] = (unsigned int)graph.edges[i].target;
    }

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
    {
        struct nk_font_atlas *atlas;
        nk_glfw3_font_stash_begin(&glfw_nk, &atlas);
        nk_glfw3_font_stash_end(&glfw_nk);
    }

    GLScene scene;
    gl_scene_init(&scene);
    gl_scene_upload(&scene, (const float *)positions, colors, graph.node_count, edge_indices, graph.edge_count);

    Camera camera;
    camera_init(&camera);
    {
        /* Fixed-distance default framed empty graphs badly and huge ones
         * worse -- frame the actual data instead. A sphere of `radius`
         * exactly fills the vertical field of view at
         * distance = radius / sin(fovy/2); back off another 20% so
         * boundary nodes aren't clipped right at the frustum edge. */
        float radius = layout3d_bounding_radius(positions, graph.node_count);
        if (radius > 0.1f) camera.distance = (radius / sinf(camera.fovy * 0.5f)) * 1.2f;
    }

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

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

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
                insp_hdr.h = note_hdr.h = props_hdr.h = PANEL_HEADER_HEIGHT;
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
            if (left_pressed_edge && !over_panel) {
                int hit = pick_nearest_node(view_proj, positions, graph.node_count, width, height,
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
            } else if (pan_button_down && !over_panel) {
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
                    int hit = pick_nearest_node(view_proj, positions, graph.node_count, width, height,
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
                        rebuild_highlighted_edges(&scene, &graph, selected);
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
                    if (file_content_hash(graph.nodes[drag_node].path, &h)) {
                        overlay_set(&overlay, graph.nodes[drag_node].path, h,
                                    positions[drag_node].x, positions[drag_node].y, positions[drag_node].z);
                        overlay_write_json(&overlay, overlay_path);
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
                    if (ray_plane_intersect(ray_origin, ray_dir, positions[drag_node], drag_plane_normal, &new_pos)) {
                        positions[drag_node] = new_pos;
                        gl_scene_update_positions(&scene, (const float *)positions, graph.node_count);
                    }
                }
                last_mouse_x = mx;
                last_mouse_y = my;
            }
        }
        left_was_down = (left_state == GLFW_PRESS);

        float scroll_y = ctx->input.mouse.scroll_delta.y;
        if (scroll_y != 0.0f && !over_panel) {
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
            const char *sel_path = selected >= 0 ? graph.nodes[selected].path : NULL;
            const char *sel_lang = selected >= 0 ? graph.nodes[selected].language : NULL;

            if (cluster_count > cluster_paths_cap) {
                cluster_paths_cap = cluster_count;
                cluster_paths = (const char **)realloc((void *)cluster_paths, cluster_paths_cap * sizeof(char *));
            }
            for (size_t i = 0; i < cluster_count; i++) cluster_paths[i] = graph.nodes[cluster_ids[i]].path;

            ui_panel_draw(ctx, width, height, sel_path, sel_lang, cluster_paths, cluster_count,
                          &notes, notes_path, &inspector_bounds);
            note_compose_draw(ctx, &notes, notes_path, &note_bounds);
            picked_dir = properties_panel_draw(ctx, &show_origin, notes_path != NULL,
                                                &export_notes_clicked, &properties_bounds);
            labels_draw(ctx, width, height, inspector_bounds, note_bounds, properties_bounds,
                        view_proj, positions, &graph, &notes);
        }

        if (export_notes_clicked && notes_path) {
            /* graph.notes.md lives under Application Support, easy to
             * lose track of -- tinyfd_saveFileDialog + a plain copy gets
             * a copy somewhere the user will actually find it. */
            const char *dest = tinyfd_saveFileDialog("Export Notes", "notes.md", 0, NULL, NULL);
            if (dest) {
                FILE *in = fopen(notes_path, "rb");
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
        glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gl_scene_draw(&scene, view_proj, selected);
        if (show_origin) gl_scene_draw_axis(&scene, view_proj, AXIS_LENGTH);

        nk_glfw3_render(&glfw_nk, NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
        glfwSwapBuffers(win);

        if (picked_dir) {
            /* One extra, ad-hoc frame -- not part of the persistent
             * window set above, just a status screen shown once before
             * blocking on the (synchronous) build -- so it doesn't need
             * any of the focus/z-order care those windows do. */
            nk_glfw3_new_frame(&glfw_nk);
            if (nk_begin(ctx, "##building", nk_rect(0, 0, (float)width, (float)height),
                         NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_labelf(ctx, NK_TEXT_CENTERED, "Building %s ...", picked_dir);
            }
            nk_end(ctx);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            nk_glfw3_render(&glfw_nk, NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
            glfwSwapBuffers(win);

            /* Blocks; on success this execv()s a fresh process and never
             * returns. On failure it's already shown a native error
             * dialog, so just fall back into the normal loop. */
            project_launcher_build_and_relaunch(self_exe_path, picked_dir);
            free(picked_dir);
        }
    }

    gl_scene_destroy(&scene);
    ui_panel_shutdown();
    nk_glfw3_shutdown(&glfw_nk);
    glfwTerminate();

    free(positions);
    free(colors);
    free(edge_indices);
    graph_free(&graph);
    overlay_free(&overlay);
    notes_free(&notes);
    free(cluster_ids);
    free((void *)cluster_paths);
    free(overlay_path);
    free(notes_path);
    free(self_exe_path);
    return 0;
}
