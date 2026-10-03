#include "nodestyle.h"
#include <math.h>

void nodestyle_compute(const Graph *g, float *out) {
    for (size_t i = 0; i < g->node_count; i++) {
        const NodeStats *st = &g->nodes[i].stats;
        float size = NODESTYLE_DEFAULT_SIZE;
        if (st->has_stats) {
            float lines = st->lines > 1 ? (float)st->lines : 1.0f;
            float t = log10f(lines) / log10f(NODESTYLE_MAX_LINES);
            if (t > 1.0f) t = 1.0f;
            size = NODESTYLE_MIN_SIZE + (NODESTYLE_MAX_SIZE - NODESTYLE_MIN_SIZE) * t;
        }
        out[i] = size;
    }
}
