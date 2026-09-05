#ifndef CODEMAP_GL_COMPAT_H
#define CODEMAP_GL_COMPAT_H

#ifdef __APPLE__
/* macOS ships GL 3.2+ core profile entry points as regular linked symbols
 * in OpenGL.framework -- no runtime loader needed. Apple deprecated the
 * API in 10.14 in favor of Metal but it still works on every version
 * since; silence the resulting warning noise. */
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#define CODEMAP_GL_NEEDS_LOADER 0
#else
/* opengl32.dll (Windows) and libGL (Linux) only export legacy (pre-1.2)
 * entry points -- everything else has to be resolved at runtime via a
 * loader. gl_load() wraps that; call it once, right after making the GL
 * context current, before any other GL call. Written but NOT run on an
 * actual Windows machine -- there is none in this environment -- so
 * treat this path as "should work," not "verified." */
#include <glad/gl.h>
#define CODEMAP_GL_NEEDS_LOADER 1
#endif

#if CODEMAP_GL_NEEDS_LOADER
#define gl_load(get_proc_address) gladLoadGL((GLADloadfunc)(get_proc_address))
#else
#define gl_load(get_proc_address) ((void)(get_proc_address), 1)
#endif

#endif
