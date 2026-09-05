#include "layout3d.h"
#include "../common/pathutil.h"
#include <stdint.h>
#include <stdlib.h>

static uint64_t fnv1a(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

/* Maps bits [shift, shift+16) of h to [-1, 1]. */
static float hash_to_unit(uint64_t h, int shift) {
    unsigned int bits = (unsigned int)((h >> shift) & 0xFFFFu);
    return ((float)bits / 65535.0f) * 2.0f - 1.0f;
}

static void seed_positions(const Graph *g, Vec3 *pos) {
    const float dir_spread = 6.0f;  /* separates different directories */
    const float file_jitter = 0.6f; /* keeps files in the same dir from starting stacked exactly */

    for (size_t i = 0; i < g->node_count; i++) {
        char *dir = path_dirname(g->nodes[i].path);
        uint64_t dh = fnv1a(dir);
        uint64_t fh = fnv1a(g->nodes[i].path);
        free(dir);

        pos[i].x = hash_to_unit(dh, 0) * dir_spread + hash_to_unit(fh, 0) * file_jitter;
        pos[i].y = hash_to_unit(dh, 16) * dir_spread + hash_to_unit(fh, 16) * file_jitter;
        pos[i].z = hash_to_unit(dh, 32) * dir_spread + hash_to_unit(fh, 32) * file_jitter;
    }
}

void layout3d_compute(const Graph *g, Vec3 *out_positions, int iterations) {
    size_t n = g->node_count;
    if (n == 0) return;

    seed_positions(g, out_positions);
    if (n == 1) return;

    Vec3 *disp = (Vec3 *)malloc(n * sizeof(Vec3));
    float k = 2.2f; /* ideal edge length */
    float temperature = 3.0f;
    const float cooling = 0.96f;

    for (int iter = 0; iter < iterations; iter++) {
        for (size_t i = 0; i < n; i++) disp[i] = (Vec3){ 0, 0, 0 };

        /* Repulsion, all pairs. */
        for (size_t i = 0; i < n; i++) {
            for (size_t j = i + 1; j < n; j++) {
                Vec3 delta = vec3_sub(out_positions[i], out_positions[j]);
                float dist = vec3_length(delta);
                if (dist < 0.01f) dist = 0.01f;
                float force = (k * k) / dist;
                Vec3 dir = vec3_scale(delta, 1.0f / dist);
                disp[i] = vec3_add(disp[i], vec3_scale(dir, force));
                disp[j] = vec3_sub(disp[j], vec3_scale(dir, force));
            }
        }

        /* Attraction along edges. */
        for (size_t e = 0; e < g->edge_count; e++) {
            int s = g->edges[e].source, t = g->edges[e].target;
            Vec3 delta = vec3_sub(out_positions[s], out_positions[t]);
            float dist = vec3_length(delta);
            if (dist < 0.01f) dist = 0.01f;
            float force = (dist * dist) / k;
            Vec3 dir = vec3_scale(delta, 1.0f / dist);
            disp[s] = vec3_sub(disp[s], vec3_scale(dir, force));
            disp[t] = vec3_add(disp[t], vec3_scale(dir, force));
        }

        /* Mild pull toward the centroid. Most node pairs in a real
         * dependency graph aren't directly connected, so repulsion
         * (summed over all O(n) other nodes) heavily outweighs
         * attraction (summed over just each node's own edges) --
         * without this, a sparse graph's whole cluster expands roughly
         * unbounded instead of settling at the equilibrium size the
         * repulsion/attraction balance is supposed to produce. */
        const float gravity = 1.0f;
        for (size_t i = 0; i < n; i++) {
            disp[i] = vec3_sub(disp[i], vec3_scale(out_positions[i], gravity));
        }

        for (size_t i = 0; i < n; i++) {
            float len = vec3_length(disp[i]);
            if (len > 0.01f) {
                float capped = len < temperature ? len : temperature;
                out_positions[i] = vec3_add(out_positions[i], vec3_scale(disp[i], capped / len));
            }
        }

        temperature *= cooling;
    }

    free(disp);

    /* Repulsion + attraction alone don't hold the barycenter still --
     * asymmetric edge distribution lets the whole cluster drift away
     * from the origin over many iterations. Recenter so the graph ends
     * up where the camera/axis gizmo actually expect it: at (0,0,0). */
    Vec3 centroid = { 0, 0, 0 };
    for (size_t i = 0; i < n; i++) centroid = vec3_add(centroid, out_positions[i]);
    centroid = vec3_scale(centroid, 1.0f / (float)n);
    for (size_t i = 0; i < n; i++) out_positions[i] = vec3_sub(out_positions[i], centroid);
}

float layout3d_bounding_radius(const Vec3 *positions, size_t count) {
    float max_dist = 0.0f;
    for (size_t i = 0; i < count; i++) {
        float d = vec3_length(positions[i]);
        if (d > max_dist) max_dist = d;
    }
    return max_dist;
}
