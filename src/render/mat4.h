#ifndef CODEMAP_MAT4_H
#define CODEMAP_MAT4_H

/* Minimal column-major (OpenGL-convention) 4x4 matrix + vec3 helpers.
 * Small enough, and used in few enough places, that a header-only
 * static-inline module is simpler than a .c/.h pair. */

#include <math.h>
#include <string.h>

typedef struct { float x, y, z; } Vec3;

static inline Vec3 vec3_add(Vec3 a, Vec3 b) { return (Vec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline Vec3 vec3_sub(Vec3 a, Vec3 b) { return (Vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline Vec3 vec3_scale(Vec3 a, float s) { return (Vec3){ a.x * s, a.y * s, a.z * s }; }
static inline float vec3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return (Vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static inline float vec3_length(Vec3 a) { return sqrtf(vec3_dot(a, a)); }
static inline Vec3 vec3_normalize(Vec3 a) {
    float len = vec3_length(a);
    if (len < 1e-6f) return (Vec3){ 0, 0, 0 };
    return vec3_scale(a, 1.0f / len);
}

static inline void mat4_identity(float *m) {
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static inline void mat4_multiply(float *out, const float *a, const float *b) {
    float r[16];
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            r[col * 4 + row] = a[0 * 4 + row] * b[col * 4 + 0] +
                                a[1 * 4 + row] * b[col * 4 + 1] +
                                a[2 * 4 + row] * b[col * 4 + 2] +
                                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

static inline void mat4_perspective(float *m, float fovy_radians, float aspect, float znear, float zfar) {
    float f = 1.0f / tanf(fovy_radians * 0.5f);
    memset(m, 0, sizeof(float) * 16);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
}

static inline void mat4_look_at(float *m, Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = vec3_normalize(vec3_sub(center, eye));
    Vec3 s = vec3_normalize(vec3_cross(f, up));
    Vec3 u = vec3_cross(s, f);

    mat4_identity(m);
    m[0] = s.x;  m[4] = s.y;  m[8] = s.z;
    m[1] = u.x;  m[5] = u.y;  m[9] = u.z;
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z;
    m[12] = -vec3_dot(s, eye);
    m[13] = -vec3_dot(u, eye);
    m[14] = vec3_dot(f, eye);
}

#endif
