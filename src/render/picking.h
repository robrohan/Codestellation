#ifndef CODEMAP_PICKING_H
#define CODEMAP_PICKING_H

#include "mat4.h"
#include <stdbool.h>
#include <stddef.h>

/* Projects a world position through a view*projection matrix (16 floats,
 * column-major) to screen pixel coordinates. Returns false if the point
 * is behind the camera. */
bool project_to_screen(const float *view_proj, Vec3 world_pos, int screen_w, int screen_h,
                        float *out_x, float *out_y);

/* Nearest node (by screen-space distance) to (mouse_x, mouse_y), within
 * max_dist_px, or -1 if none qualifies. This is the "click to select"
 * approach: forward-project every node rather than raycast into the
 * scene, since it's simpler and robust for a point-cloud-like graph. */
int pick_nearest_node(const float *view_proj, const Vec3 *positions, size_t count,
                       int screen_w, int screen_h, float mouse_x, float mouse_y, float max_dist_px);

/* Intersects a ray with the plane through plane_point facing
 * plane_normal -- used to drag a node across a camera-facing (billboard)
 * plane while the mouse moves. False if the ray is ~parallel to the
 * plane or points away from it. */
bool ray_plane_intersect(Vec3 ray_origin, Vec3 ray_dir, Vec3 plane_point, Vec3 plane_normal,
                          Vec3 *out_point);

#endif
