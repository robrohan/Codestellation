/* Shaders are kept as inline string constants rather than external
 * .vert/.frag files -- two tiny shaders don't justify a runtime file-load
 * path, and this keeps codemap-view runnable from any working directory. */

#include "gl_scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static const char *VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "uniform mat4 u_mvp;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "    gl_PointSize = 9.0;\n"
    "}\n";

static const char *FRAG_SRC =
    "#version 330 core\n"
    "uniform vec4 u_color;\n"
    "out vec4 frag_color;\n"
    "void main() { frag_color = u_color; }\n";

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "gl_scene: shader compile error: %s\n", log);
    }
    return shader;
}

static GLuint link_program(void) {
    GLuint vert = compile_shader(GL_VERTEX_SHADER, VERT_SRC);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, FRAG_SRC);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "gl_scene: program link error: %s\n", log);
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

void gl_scene_init(GLScene *scene) {
    scene->prog = link_program();
    scene->u_mvp = glGetUniformLocation(scene->prog, "u_mvp");
    scene->u_color = glGetUniformLocation(scene->prog, "u_color");

    glGenVertexArrays(1, &scene->vao);
    glGenBuffers(1, &scene->vbo);
    glGenBuffers(1, &scene->ebo);
    glGenBuffers(1, &scene->highlight_ebo);

    glBindVertexArray(scene->vao);
    glBindBuffer(GL_ARRAY_BUFFER, scene->vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    /* The element-buffer binding is part of VAO state (unlike GL_ARRAY_BUFFER),
     * so it has to be bound here, while this VAO is current -- binding it later
     * in gl_scene_upload (with no VAO bound) would attach it to VAO 0 instead,
     * leaving this VAO with no index buffer and silently drawing zero lines.
     * gl_scene_draw explicitly rebinds whichever of ebo/highlight_ebo it
     * needs before each glDrawElements call, so the binding made here is
     * just the initial default. */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene->ebo);
    glBindVertexArray(0);

    scene->edge_index_count = 0;
    scene->point_count = 0;
    scene->highlight_index_count = 0;

    /* Axis gizmo: 3 segments (origin -> X/Y/Z). Re-uploaded (6 floats*3,
     * trivially cheap) each gl_scene_draw_axis call rather than cached,
     * since `length` is caller-supplied and rarely if ever changes. */
    glGenVertexArrays(1, &scene->axis_vao);
    glGenBuffers(1, &scene->axis_vbo);
    glBindVertexArray(scene->axis_vao);
    glBindBuffer(GL_ARRAY_BUFFER, scene->axis_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void gl_scene_upload(GLScene *scene, const float *positions, size_t point_count,
                      const unsigned int *edge_indices, size_t edge_count) {
    scene->point_count = (GLsizei)point_count;
    scene->edge_index_count = (GLsizei)(edge_count * 2);

    glBindBuffer(GL_ARRAY_BUFFER, scene->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(point_count * 3 * sizeof(float)), positions, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(edge_count * 2 * sizeof(unsigned int)), edge_indices, GL_STATIC_DRAW);
}

void gl_scene_update_positions(GLScene *scene, const float *positions, size_t point_count) {
    glBindBuffer(GL_ARRAY_BUFFER, scene->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(point_count * 3 * sizeof(float)), positions);
}

void gl_scene_set_highlighted_edges(GLScene *scene, const unsigned int *edge_indices, size_t edge_count) {
    scene->highlight_index_count = (GLsizei)(edge_count * 2);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene->highlight_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(edge_count * 2 * sizeof(unsigned int)), edge_indices, GL_DYNAMIC_DRAW);
}

void gl_scene_draw(const GLScene *scene, const float *mvp, int highlight_index) {
    glUseProgram(scene->prog);
    glUniformMatrix4fv(scene->u_mvp, 1, GL_FALSE, mvp);
    glBindVertexArray(scene->vao);

    /* No blending is enabled anywhere in this renderer, so "fade" is done
     * by dimming the RGB itself rather than via alpha -- simpler than
     * introducing blend-state management for one effect. */
    bool has_highlight = highlight_index >= 0;
    if (has_highlight) {
        glUniform4f(scene->u_color, 0.30f * 0.25f, 0.55f * 0.25f, 0.75f * 0.25f, 1.0f);
    } else {
        glUniform4f(scene->u_color, 0.30f, 0.55f, 0.75f, 1.0f);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene->ebo);
    glDrawElements(GL_LINES, scene->edge_index_count, GL_UNSIGNED_INT, 0);

    if (has_highlight && scene->highlight_index_count > 0) {
        glUniform4f(scene->u_color, 0.35f, 0.75f, 1.0f, 1.0f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene->highlight_ebo);
        glDrawElements(GL_LINES, scene->highlight_index_count, GL_UNSIGNED_INT, 0);
    }

    glUniform4f(scene->u_color, 1.0f, 0.82f, 0.25f, 1.0f);
    glDrawArrays(GL_POINTS, 0, scene->point_count);

    if (highlight_index >= 0 && highlight_index < scene->point_count) {
        glUniform4f(scene->u_color, 1.0f, 0.35f, 0.35f, 1.0f);
        glDrawArrays(GL_POINTS, highlight_index, 1);
    }

    glBindVertexArray(0);
}

void gl_scene_draw_axis(const GLScene *scene, const float *mvp, float length) {
    const float verts[18] = {
        0, 0, 0,  length, 0, 0,   /* X */
        0, 0, 0,  0, length, 0,   /* Y */
        0, 0, 0,  0, 0, length,   /* Z */
    };

    glUseProgram(scene->prog);
    glUniformMatrix4fv(scene->u_mvp, 1, GL_FALSE, mvp);
    glBindVertexArray(scene->axis_vao);
    glBindBuffer(GL_ARRAY_BUFFER, scene->axis_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);

    glUniform4f(scene->u_color, 0.85f, 0.25f, 0.25f, 1.0f); /* X = red */
    glDrawArrays(GL_LINES, 0, 2);
    glUniform4f(scene->u_color, 0.25f, 0.85f, 0.25f, 1.0f); /* Y = green */
    glDrawArrays(GL_LINES, 2, 2);
    glUniform4f(scene->u_color, 0.25f, 0.45f, 0.95f, 1.0f); /* Z = blue */
    glDrawArrays(GL_LINES, 4, 2);

    glBindVertexArray(0);
}

void gl_scene_destroy(GLScene *scene) {
    glDeleteProgram(scene->prog);
    glDeleteBuffers(1, &scene->vbo);
    glDeleteBuffers(1, &scene->ebo);
    glDeleteBuffers(1, &scene->highlight_ebo);
    glDeleteVertexArrays(1, &scene->vao);
    glDeleteBuffers(1, &scene->axis_vbo);
    glDeleteVertexArrays(1, &scene->axis_vao);
}
