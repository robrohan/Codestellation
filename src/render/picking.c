#include "picking.h"
#include <math.h>

bool project_to_screen(const float *m, Vec3 p, int screen_w, int screen_h, float *out_x, float *out_y) {
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w <= 0.0001f) return false;

    float ndc_x = x / w;
    float ndc_y = y / w;
    *out_x = (ndc_x * 0.5f + 0.5f) * (float)screen_w;
    *out_y = (1.0f - (ndc_y * 0.5f + 0.5f)) * (float)screen_h;
    return true;
}

int pick_nearest_node(const float *view_proj, const Vec3 *positions, size_t count,
                       int screen_w, int screen_h, float mouse_x, float mouse_y, float max_dist_px) {
    int best = -1;
    float best_dist = max_dist_px;
    for (size_t i = 0; i < count; i++) {
        float sx, sy;
        if (!project_to_screen(view_proj, positions[i], screen_w, screen_h, &sx, &sy)) continue;
        float dx = sx - mouse_x, dy = sy - mouse_y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < best_dist) {
            best_dist = dist;
            best = (int)i;
        }
    }
    return best;
}

bool ray_plane_intersect(Vec3 ray_origin, Vec3 ray_dir, Vec3 plane_point, Vec3 plane_normal,
                          Vec3 *out_point) {
    float denom = vec3_dot(ray_dir, plane_normal);
    if (fabsf(denom) < 1e-6f) return false;
    float t = vec3_dot(vec3_sub(plane_point, ray_origin), plane_normal) / denom;
    if (t < 0.0f) return false;
    *out_point = vec3_add(ray_origin, vec3_scale(ray_dir, t));
    return true;
}
