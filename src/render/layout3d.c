#include "layout3d.h"
#include "dirgroup.h"
#include "../common/pathutil.h"
#include <stdint.h>
#include <stdio.h>
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

/* dir_target[i] receives each node's "home" point -- reused by
 * layout3d_compute as a standing clustering attractor, see there, rather
 * than just a one-time starting nudge. This is two nested levels, not
 * one flat hash:
 *
 *   coarse anchor (dirgroup.c's grouping, same one dircolor.c colors by)
 *     -> exact-directory offset from that anchor
 *       -> per-file jitter from that
 *
 * Nesting the exact directory's point *inside* its coarse group's anchor
 * (rather than hashing it independently across the whole world) is what
 * actually keeps e.g. a vendored library's several real subdirectories
 * near each other -- coloring them the same was never enough on its own;
 * a flat per-exact-directory hash scatters same-colored-but-differently-
 * pathed directories anywhere, since nothing tied their random points
 * together. */
static void seed_positions(const Graph *g, Vec3 *pos, Vec3 *dir_target, size_t prefix_len) {
    const float coarse_spread = 6.0f;  /* separates different coarse groups (e.g. distinct vendored libs) */
    const float fine_spread = 1.8f;    /* separates exact directories within the same coarse group */
    const float file_jitter = 0.6f;    /* keeps files in the same dir from starting stacked exactly */

    for (size_t i = 0; i < g->node_count; i++) {
        char coarse_key[512];
        dirgroup_coarse_key(g->nodes[i].path, prefix_len, coarse_key, sizeof(coarse_key));
        uint64_t ch = fnv1a(coarse_key);

        char *dir = path_dirname(g->nodes[i].path);
        uint64_t dh = fnv1a(dir);
        uint64_t fh = fnv1a(g->nodes[i].path);
        free(dir);

        Vec3 coarse_anchor = {
            hash_to_unit(ch, 0) * coarse_spread,
            hash_to_unit(ch, 16) * coarse_spread,
            hash_to_unit(ch, 32) * coarse_spread,
        };
        Vec3 home = {
            coarse_anchor.x + hash_to_unit(dh, 0) * fine_spread,
            coarse_anchor.y + hash_to_unit(dh, 16) * fine_spread,
            coarse_anchor.z + hash_to_unit(dh, 32) * fine_spread,
        };
        dir_target[i] = home;

        pos[i].x = home.x + hash_to_unit(fh, 0) * file_jitter;
        pos[i].y = home.y + hash_to_unit(fh, 16) * file_jitter;
        pos[i].z = home.z + hash_to_unit(fh, 32) * file_jitter;
    }
}

void layout3d_compute(const Graph *g, Vec3 *out_positions, int iterations) {
    size_t n = g->node_count;
    if (n == 0) return;

    size_t prefix_len = dirgroup_common_prefix_len(g);
    Vec3 *dir_target = (Vec3 *)malloc(n * sizeof(Vec3));
    seed_positions(g, out_positions, dir_target, prefix_len);
    if (n == 1) {
        free(dir_target);
        return;
    }

    /* Repulsion is O(n^2) per iteration. Below ~1500 nodes the full 300
     * iterations are cheap; past that, scale the count down (never below
     * 60, which is enough for the seeded layout to settle) so a
     * few-thousand-node graph loads in seconds instead of a minute. The
     * proper fix is a Barnes-Hut approximation of the repulsion pass --
     * this is the stopgap until then. */
    int effective_iters = iterations;
    if (n > 1500) {
        effective_iters = (int)((long)iterations * 1500 / (long)n);
        if (effective_iters < 60) effective_iters = 60;
    }

    Vec3 *disp = (Vec3 *)malloc(n * sizeof(Vec3));
    float k = 2.2f; /* ideal edge length */
    float temperature = 3.0f;
    const float cooling = 0.96f;
    /* Standing pull toward this node's hash-derived "home" point (the
     * same point seed_positions started it near) -- without this,
     * unconnected same-directory files (a vendored library rarely
     * #includes every sibling, fixture/test-data files reference
     * nothing) drift apart over these iterations even though they
     * started out clustered.
     *
     * Tuning this fought the *gravity* term below, not edge attraction --
     * gravity pulls every node toward one shared global origin every
     * iteration regardless of grouping, so a weak clustering pull (tried
     * down to 0.02) just gets outcompeted and directory groups blur
     * together near the origin as gravity dominates. Verified empirically
     * (a standalone test measuring distances between several of a
     * vendored library's real subdirectories vs. an unrelated one):
     * separation improves steadily up to here and then plateaus, since
     * gravity's uniform pull is the actual ceiling, not insufficient
     * clustering strength -- pushing much higher than this starts
     * overpowering real edge attraction instead (files that genuinely
     * reference something in another directory should still visibly
     * pull toward it). */
    const float dir_cluster_strength = 4.0f;

    int progress_step = effective_iters > 10 ? effective_iters / 10 : 1;
    for (int iter = 0; iter < effective_iters; iter++) {
        if (n > 1500 && iter % progress_step == 0) {
            fprintf(stderr, "layout: %d/%d iterations (%zu nodes)\n", iter, effective_iters, n);
        }
        for (size_t i = 0; i < n; i++) disp[i] = (Vec3){ 0, 0, 0 };

        /* Repulsion, all pairs. The displacement contribution is
         * dir * force = (delta/dist) * (k*k/dist) = delta * (k*k / dist^2),
         * so this works entirely in squared distance -- no sqrt in the
         * hottest loop in the program. */
        for (size_t i = 0; i < n; i++) {
            for (size_t j = i + 1; j < n; j++) {
                Vec3 delta = vec3_sub(out_positions[i], out_positions[j]);
                float d2 = vec3_dot(delta, delta);
                if (d2 < 0.0001f) d2 = 0.0001f;
                Vec3 f = vec3_scale(delta, (k * k) / d2);
                disp[i] = vec3_add(disp[i], f);
                disp[j] = vec3_sub(disp[j], f);
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

        /* Directory clustering -- see dir_cluster_strength's comment above. */
        for (size_t i = 0; i < n; i++) {
            Vec3 toward_home = vec3_sub(dir_target[i], out_positions[i]);
            disp[i] = vec3_add(disp[i], vec3_scale(toward_home, dir_cluster_strength));
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
    free(dir_target);

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
