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
#else
/* TODO(Phase 7 - Windows/Linux verification): opengl32.dll / libGL only
 * export legacy (pre-1.2) entry points; everything else needs a runtime
 * loader (e.g. GLAD) here. Not implemented yet -- only macOS has been
 * exercised so far. */
#include <GL/gl.h>
#endif

#endif
