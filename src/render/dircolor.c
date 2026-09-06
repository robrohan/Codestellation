/* Colors each node by dirgroup.c's coarse directory grouping, so a whole
 * vendored subtree (however deeply nested, however many real
 * subdirectories it actually has) reads as one consistent color --
 * distinct from layout3d.c's *spatial* clustering, which nests each
 * exact subdirectory's own tight cluster near that same coarse group's
 * shared anchor point (finer-grained, but still kept visually together). */

#include "dircolor.h"
#include "dirgroup.h"
#include <stdint.h>
#include <math.h>

static uint64_t fnv1a(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

static void hsl_to_rgb(float h, float s, float l, float *r, float *g, float *b) {
    float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
    float hp = h / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float r1 = 0, g1 = 0, b1 = 0;
    if (hp < 1.0f) { r1 = c; g1 = x; b1 = 0; }
    else if (hp < 2.0f) { r1 = x; g1 = c; b1 = 0; }
    else if (hp < 3.0f) { r1 = 0; g1 = c; b1 = x; }
    else if (hp < 4.0f) { r1 = 0; g1 = x; b1 = c; }
    else if (hp < 5.0f) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }
    float m = l - c / 2.0f;
    *r = r1 + m;
    *g = g1 + m;
    *b = b1 + m;
}

void dircolor_compute(const Graph *g, float *out_colors) {
    size_t prefix_len = dirgroup_common_prefix_len(g);

    for (size_t i = 0; i < g->node_count; i++) {
        char key[512];
        dirgroup_coarse_key(g->nodes[i].path, prefix_len, key, sizeof(key));

        uint64_t h = fnv1a(key);
        /* Hue-only hash at fixed saturation/lightness, not 3
         * independently-hashed RGB channels -- the latter tends to
         * produce muddy or near-black colors; this guarantees every
         * group reads clearly against the dark 3D background. */
        float hue = (float)(h % 360ULL);
        hsl_to_rgb(hue, 0.55f, 0.55f, &out_colors[i * 3 + 0], &out_colors[i * 3 + 1], &out_colors[i * 3 + 2]);
    }
}
