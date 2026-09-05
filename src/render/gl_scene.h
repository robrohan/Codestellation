#ifndef CODEMAP_GL_SCENE_H
#define CODEMAP_GL_SCENE_H

#include "gl_compat.h"
#include <stddef.h>

typedef struct {
    GLuint vao, vbo, ebo;
    GLuint prog;
    GLint u_mvp, u_color;
    GLsizei edge_index_count;
    GLsizei point_count;
} GLScene;

void gl_scene_init(GLScene *scene);

/* positions: point_count * 3 floats (x,y,z per node, node id == array
 * index). edge_indices: edge_count * 2 uints (source, target per edge). */
void gl_scene_upload(GLScene *scene, const float *positions, size_t point_count,
                      const unsigned int *edge_indices, size_t edge_count);

/* Cheap re-upload of just the vertex buffer -- used after a drag moves a
 * node. Point/edge counts must match what gl_scene_upload was last
 * called with. */
void gl_scene_update_positions(GLScene *scene, const float *positions, size_t point_count);

/* mvp: 16 floats, column-major. highlight_index: node to draw in the
 * highlight color, or -1 for none. */
void gl_scene_draw(const GLScene *scene, const float *mvp, int highlight_index);

void gl_scene_destroy(GLScene *scene);

#endif
