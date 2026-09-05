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
#include "picking.h"
#include "ui_panel.h"
#include "labels.h"
#include "../graph/graph.h"
#include "../graph/graph_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <graph.json>\n", argv[0]);
        return 1;
    }

    Graph graph;
    if (!graph_read_json(argv[1], &graph)) {
        fprintf(stderr, "error: could not read %s\n", argv[1]);
        return 1;
    }
    printf("loaded %zu nodes, %zu edges from %s\n", graph.node_count, graph.edge_count, argv[1]);

    Vec3 *positions = (Vec3 *)malloc(graph.node_count * sizeof(Vec3));
    layout3d_compute(&graph, positions, 300);

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

    GLFWwindow *win = glfwCreateWindow(1280, 800, "codemap3d", NULL, NULL);
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
    gl_scene_upload(&scene, (const float *)positions, graph.node_count, edge_indices, graph.edge_count);

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

    /* Right-click doubles as both "pan" (drag) and "select" (click with
     * no real movement) -- these track which button started the current
     * pan and whether it has moved enough to count as a drag rather
     * than a click. */
    bool pan_via_right = false;
    double press_x = 0, press_y = 0;
    bool moved_since_press = false;

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

        int width, height;
        glfwGetWindowSize(win, &width, &height);
        float aspect = height > 0 ? (float)width / (float)height : 1.0f;

        float view[16], proj[16], view_proj[16];
        camera_view_matrix(&camera, view);
        mat4_perspective(proj, camera.fovy, aspect, 0.002f, 1000.0f);
        mat4_multiply(view_proj, proj, view);

        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        int panel_x = width - UI_PANEL_WIDTH;
        bool over_panel = mx >= (double)panel_x;
        int left_state = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT);
        int middle_state = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE);
        int right_state = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT);
        bool pan_button_down = (middle_state == GLFW_PRESS || right_state == GLFW_PRESS);

        if (interact == INTERACT_NONE) {
            /* Left button: always reposition-drag on a node, or orbit on
             * empty space -- never touches selection. */
            if (left_state == GLFW_PRESS && !over_panel) {
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
                    selected = pick_nearest_node(view_proj, positions, graph.node_count, width, height,
                                                  (float)mx, (float)my, PICK_RADIUS_PX);
                    rebuild_highlighted_edges(&scene, &graph, selected);
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

        nk_glfw3_new_frame(&glfw_nk);

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

        {
            const char *sel_path = selected >= 0 ? graph.nodes[selected].path : NULL;
            const char *sel_lang = selected >= 0 ? graph.nodes[selected].language : NULL;
            ui_panel_draw(ctx, width, height, sel_path, sel_lang);
            labels_draw(ctx, width, height, panel_x, view_proj, positions, &graph);
        }

        int fb_width, fb_height;
        glfwGetFramebufferSize(win, &fb_width, &fb_height);
        glViewport(0, 0, fb_width, fb_height);
        glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gl_scene_draw(&scene, view_proj, selected);
        gl_scene_draw_axis(&scene, view_proj, AXIS_LENGTH);

        nk_glfw3_render(&glfw_nk, NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
        glfwSwapBuffers(win);
    }

    gl_scene_destroy(&scene);
    ui_panel_shutdown();
    nk_glfw3_shutdown(&glfw_nk);
    glfwTerminate();

    free(positions);
    free(edge_indices);
    graph_free(&graph);
    return 0;
}
