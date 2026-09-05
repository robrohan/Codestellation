#ifndef CODEMAP_CAMERA_H
#define CODEMAP_CAMERA_H

#include "mat4.h"

/* Simple target-centered orbit camera -- yaw/pitch/distance around a
 * fixed target point, which is all a "fly around and inspect the graph"
 * viewer needs. */
typedef struct {
    Vec3  target;
    float yaw;    /* radians, around world up */
    float pitch;  /* radians, clamped away from the poles */
    float distance;
    float fovy;   /* radians */
} Camera;

void camera_init(Camera *cam);
void camera_orbit(Camera *cam, float dyaw, float dpitch);
void camera_zoom(Camera *cam, float delta);
Vec3 camera_eye(const Camera *cam);
/* forward points from eye toward target. */
void camera_basis(const Camera *cam, Vec3 *out_forward, Vec3 *out_right, Vec3 *out_up);
void camera_view_matrix(const Camera *cam, float out_view[16]);

/* World-space ray through a normalized device coordinate (each in
 * [-1, 1]) on the near plane, for picking/dragging. */
void camera_ray(const Camera *cam, float ndc_x, float ndc_y, float aspect,
                 Vec3 *out_origin, Vec3 *out_dir);

#endif
