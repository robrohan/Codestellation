/* Phase 2 skeleton: confirms GLFW + GL 3.3 core (+ macOS forward-compat) +
 * Nuklear all work together before any real graph data enters the picture.
 * Draws a handful of hardcoded points/lines and a static Nuklear side
 * panel. Real layout (layout3d.c), scene rendering (gl_scene.c), and the
 * file-content panel (ui_panel.c) come in later phases. */

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

#include <stdio.h>
#include <stdlib.h>

#define MAX_VERTEX_BUFFER (512 * 1024)
#define MAX_ELEMENT_BUFFER (128 * 1024)

/* --- minimal hardcoded point/line scene, just to prove our own GL calls
 * (not only Nuklear's) work under this context --- */

static const char *POINT_VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 1.0);\n"
    "    gl_PointSize = 10.0;\n"
    "}\n";

static const char *POINT_FRAG_SRC =
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
        fprintf(stderr, "shader compile error: %s\n", log);
    }
    return shader;
}

static GLuint link_program(const char *vert_src, const char *frag_src) {
    GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "program link error: %s\n", log);
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

/* 5 dummy points in NDC space, roughly forming a small cluster + one
 * outlier, connected by a couple of lines -- stand-ins for real graph
 * nodes/edges until layout3d.c exists. */
static const float DUMMY_POINTS[] = {
    -0.3f,  0.2f, 0.0f,
     0.1f,  0.4f, 0.0f,
     0.2f, -0.1f, 0.0f,
    -0.2f, -0.3f, 0.0f,
     0.6f,  0.5f, 0.0f,
};
static const unsigned int DUMMY_LINE_INDICES[] = {
    0, 1,  1, 2,  2, 3,  3, 0,  1, 4,
};

int main(void) {
    if (!glfwInit()) {
        fprintf(stderr, "error: glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow *win = glfwCreateWindow(1280, 800, "codemap3d (phase 2 skeleton)", NULL, NULL);
    if (!win) {
        fprintf(stderr, "error: glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    struct nk_glfw glfw = {0};
    struct nk_context *ctx = nk_glfw3_init(&glfw, win, NK_GLFW3_INSTALL_CALLBACKS);
    {
        struct nk_font_atlas *atlas;
        nk_glfw3_font_stash_begin(&glfw, &atlas);
        nk_glfw3_font_stash_end(&glfw);
    }

    GLuint prog = link_program(POINT_VERT_SRC, POINT_FRAG_SRC);
    GLint u_color = glGetUniformLocation(prog, "u_color");

    GLuint vao, vbo, ebo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(DUMMY_POINTS), DUMMY_POINTS, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(DUMMY_LINE_INDICES), DUMMY_LINE_INDICES, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glEnable(GL_PROGRAM_POINT_SIZE);

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }

        int width, height;
        glfwGetWindowSize(win, &width, &height);

        nk_glfw3_new_frame(&glfw);
        if (nk_begin(ctx, "Inspector", nk_rect((float)width - 320, 0, 320, (float)height),
                     NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label(ctx, "codemap3d", NK_TEXT_LEFT);
            nk_label(ctx, "Phase 2: window + Nuklear skeleton", NK_TEXT_LEFT);
            nk_label(ctx, "Click a node to inspect its file here.", NK_TEXT_LEFT);
        }
        nk_end(ctx);

        glViewport(0, 0, width, height);
        glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(prog);
        glBindVertexArray(vao);
        glUniform4f(u_color, 0.35f, 0.75f, 0.95f, 1.0f);
        glDrawElements(GL_LINES, sizeof(DUMMY_LINE_INDICES) / sizeof(unsigned int), GL_UNSIGNED_INT, 0);
        glUniform4f(u_color, 1.0f, 0.85f, 0.3f, 1.0f);
        glDrawArrays(GL_POINTS, 0, sizeof(DUMMY_POINTS) / (3 * sizeof(float)));
        glBindVertexArray(0);

        nk_glfw3_render(&glfw, NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
        glfwSwapBuffers(win);
    }

    glDeleteProgram(prog);
    glDeleteBuffers(1, &vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteVertexArrays(1, &vao);

    nk_glfw3_shutdown(&glfw);
    glfwTerminate();
    return 0;
}
