#ifndef CODEMAP_GL_SCENE_H
#define CODEMAP_GL_SCENE_H

#include "gl_compat.h"
#include <stddef.h>

typedef struct {
    GLuint vao, vbo, color_vbo, ebo;
    GLuint prog;
    GLint u_mvp, u_color, u_override;
    GLsizei edge_index_count;
    GLsizei point_count;
    GLuint axis_vao, axis_vbo;
    GLuint highlight_ebo;
    GLsizei highlight_index_count;
    GLuint cluster_ebo;
    GLsizei cluster_point_count;
} GLScene;

void gl_scene_init(GLScene *scene);

/* positions: point_count * 3 floats (x,y,z per node, node id == array
 * index). colors: point_count * 3 floats (r,g,b per node, same order --
 * static per node, e.g. dircolor_compute's output; never touched by
 * dragging so there's no update path for it, unlike positions).
 * edge_indices: edge_count * 2 uints (source, target per edge). */
void gl_scene_upload(GLScene *scene, const float *positions, const float *colors, size_t point_count,
                      const unsigned int *edge_indices, size_t edge_count);

/* Cheap re-upload of just the vertex buffer -- used after a drag moves a
 * node. Point/edge counts must match what gl_scene_upload was last
 * called with. */
void gl_scene_update_positions(GLScene *scene, const float *positions, size_t point_count);

/* mvp: 16 floats, column-major. highlight_index: node to draw in the
 * highlight color, or -1 for none. Edges get the same treatment: with a
 * highlight set, all edges fade except the ones set via
 * gl_scene_set_highlighted_edges, which draw bright on top. */
void gl_scene_draw(const GLScene *scene, const float *mvp, int highlight_index);

/* Replaces the set of edges drawn in the bright "highlighted" color --
 * call whenever the selected node changes, passing just the edges that
 * touch it (or edge_count 0 to clear). Same index format as
 * gl_scene_upload's edge_indices. */
void gl_scene_set_highlighted_edges(GLScene *scene, const unsigned int *edge_indices, size_t edge_count);

/* Replaces the set of nodes drawn as a distinct-colored "cluster" point
 * pass on top of the normal points -- the multi-select accent, independent
 * of gl_scene_draw's own single `highlight_index`. node_ids are node
 * (== position array) indices, same as gl_scene_upload's edge_indices.
 * count 0 clears it. */
void gl_scene_set_cluster_points(GLScene *scene, const unsigned int *node_ids, size_t count);

/* Small RGB axis gizmo at the world origin (X=red, Y=green, Z=blue),
 * length in world units -- just an orientation anchor while flying
 * around the graph. */
void gl_scene_draw_axis(const GLScene *scene, const float *mvp, float length);

void gl_scene_destroy(GLScene *scene);

#endif
