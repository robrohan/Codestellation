#include "camera.h"
#include <math.h>

#define PITCH_LIMIT 1.5f /* radians, just short of +/- pi/2 */

void camera_init(Camera *cam) {
    cam->target = (Vec3){ 0, 0, 0 };
    cam->yaw = 0.0f;
    cam->pitch = 0.3f;
    cam->distance = 12.0f;
    cam->fovy = 60.0f * (float)M_PI / 180.0f;
}

void camera_orbit(Camera *cam, float dyaw, float dpitch) {
    cam->yaw += dyaw;
    cam->pitch += dpitch;
    if (cam->pitch > PITCH_LIMIT) cam->pitch = PITCH_LIMIT;
    if (cam->pitch < -PITCH_LIMIT) cam->pitch = -PITCH_LIMIT;
}

void camera_zoom(Camera *cam, float delta) {
    cam->distance -= delta;
    if (cam->distance < 0.5f) cam->distance = 0.5f;
    if (cam->distance > 500.0f) cam->distance = 500.0f;
}

Vec3 camera_eye(const Camera *cam) {
    float cp = cosf(cam->pitch);
    Vec3 offset = {
        cam->distance * cp * sinf(cam->yaw),
        cam->distance * sinf(cam->pitch),
        cam->distance * cp * cosf(cam->yaw),
    };
    return vec3_add(cam->target, offset);
}

void camera_basis(const Camera *cam, Vec3 *out_forward, Vec3 *out_right, Vec3 *out_up) {
    Vec3 eye = camera_eye(cam);
    Vec3 forward = vec3_normalize(vec3_sub(cam->target, eye));
    Vec3 world_up = { 0, 1, 0 };
    Vec3 right = vec3_normalize(vec3_cross(forward, world_up));
    Vec3 up = vec3_cross(right, forward);
    *out_forward = forward;
    *out_right = right;
    *out_up = up;
}

void camera_view_matrix(const Camera *cam, float out_view[16]) {
    mat4_look_at(out_view, camera_eye(cam), cam->target, (Vec3){ 0, 1, 0 });
}

void camera_ray(const Camera *cam, float ndc_x, float ndc_y, float aspect,
                 Vec3 *out_origin, Vec3 *out_dir) {
    Vec3 forward, right, up;
    camera_basis(cam, &forward, &right, &up);

    float half_h = tanf(cam->fovy * 0.5f);
    float half_w = half_h * aspect;

    Vec3 dir = vec3_add(forward,
                vec3_add(vec3_scale(right, ndc_x * half_w),
                         vec3_scale(up, ndc_y * half_h)));

    *out_origin = camera_eye(cam);
    *out_dir = vec3_normalize(dir);
}
