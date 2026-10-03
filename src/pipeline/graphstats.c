/* Dependency-graph stats.
 *
 *   - fan_in / fan_out: distinct files depending on / depended on.
 *   - cycles: strongly connected components (Tarjan, iterative -- a deep
 *     dependency chain would overflow the stack recursively). Files in a
 *     component of two or more share its cycle_id.
 *   - blast_radius: how many files depend on this one directly or through
 *     others. Worked out per component in topological order, each keeping
 *     a bitset of the files that reach it; a file's radius is its
 *     component's set plus the rest of its own cycle. The bitsets cost
 *     components x files bits, so very large graphs skip it (-1).
 */

#include "graphstats.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* 50 MB of bitsets. */
#define BLAST_BIT_BUDGET ((uint64_t)400 * 1000 * 1000)

typedef struct {
    int a, b;
} Pair;

static int cmp_pair(const void *x, const void *y) {
    const Pair *p = (const Pair *)x, *q = (const Pair *)y;
    if (p->a != q->a) return p->a < q->a ? -1 : 1;
    if (p->b != q->b) return p->b < q->b ? -1 : 1;
    return 0;
}

/* Compressed adjacency: neighbours of v are adj[off[v] .. off[v+1]). */
typedef struct {
    int *off, *adj;
} Csr;

static Csr csr_build(const Pair *pairs, size_t m, int n, bool reverse) {
    Csr c;
    c.off = (int *)calloc((size_t)n + 1, sizeof(int));
    c.adj = (int *)malloc(sizeof(int) * (m ? m : 1));
    for (size_t i = 0; i < m; i++) c.off[(reverse ? pairs[i].b : pairs[i].a) + 1]++;
    for (int v = 0; v < n; v++) c.off[v + 1] += c.off[v];
    int *fill = (int *)malloc(sizeof(int) * ((size_t)n + 1));
    memcpy(fill, c.off, sizeof(int) * ((size_t)n + 1));
    for (size_t i = 0; i < m; i++) {
        int from = reverse ? pairs[i].b : pairs[i].a, to = reverse ? pairs[i].a : pairs[i].b;
        c.adj[fill[from]++] = to;
    }
    free(fill);
    return c;
}

static void csr_free(Csr *c) {
    free(c->off);
    free(c->adj);
}

/* comp[v] = component index, in Tarjan's emission order (a component is
 * emitted after every component it can reach). Returns the count. */
static int tarjan(const Csr *out, int n, int *comp) {
    int *index = (int *)malloc(sizeof(int) * (size_t)n);
    int *low = (int *)malloc(sizeof(int) * (size_t)n);
    bool *on_stack = (bool *)calloc((size_t)n, sizeof(bool));
    int *stack = (int *)malloc(sizeof(int) * (size_t)n);
    int *call_v = (int *)malloc(sizeof(int) * (size_t)n);
    int *call_e = (int *)malloc(sizeof(int) * (size_t)n);
    for (int v = 0; v < n; v++) index[v] = -1;

    int next_index = 0, sp = 0, comps = 0;
    for (int root = 0; root < n; root++) {
        if (index[root] >= 0) continue;
        int depth = 0;
        call_v[0] = root;
        call_e[0] = out->off[root];
        index[root] = low[root] = next_index++;
        stack[sp++] = root;
        on_stack[root] = true;
        while (depth >= 0) {
            int v = call_v[depth];
            if (call_e[depth] < out->off[v + 1]) {
                int w = out->adj[call_e[depth]++];
                if (index[w] < 0) {
                    index[w] = low[w] = next_index++;
                    stack[sp++] = w;
                    on_stack[w] = true;
                    depth++;
                    call_v[depth] = w;
                    call_e[depth] = out->off[w];
                } else if (on_stack[w] && index[w] < low[v]) {
                    low[v] = index[w];
                }
                continue;
            }
            if (low[v] == index[v]) {
                int w;
                do {
                    w = stack[--sp];
                    on_stack[w] = false;
                    comp[w] = comps;
                } while (w != v);
                comps++;
            }
            depth--;
            if (depth >= 0) {
                int parent = call_v[depth];
                if (low[v] < low[parent]) low[parent] = low[v];
            }
        }
    }
    free(index);
    free(low);
    free(on_stack);
    free(stack);
    free(call_v);
    free(call_e);
    return comps;
}

static int popcount64(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (int)((x * 0x0101010101010101ULL) >> 56);
}

static void blast_radius(Graph *g, const Csr *in, const int *comp, int comps, const int *comp_size) {
    int n = (int)g->node_count;
    if ((uint64_t)comps * (uint64_t)n > BLAST_BIT_BUDGET) return; /* stays -1 */
    size_t words = ((size_t)n + 63) / 64;
    uint64_t *bits = (uint64_t *)calloc((size_t)comps * words, sizeof(uint64_t));

    /* Members of each component, to walk components in order. */
    int *start = (int *)calloc((size_t)comps + 1, sizeof(int));
    for (int v = 0; v < n; v++) start[comp[v] + 1]++;
    for (int c = 0; c < comps; c++) start[c + 1] += start[c];
    int *members = (int *)malloc(sizeof(int) * ((size_t)n ? (size_t)n : 1));
    int *fill = (int *)malloc(sizeof(int) * ((size_t)comps + 1));
    memcpy(fill, start, sizeof(int) * ((size_t)comps + 1));
    for (int v = 0; v < n; v++) members[fill[comp[v]]++] = v;
    free(fill);

    /* Reverse emission order is topological: every component that depends
     * on c (has an edge into it) is finished before c. */
    for (int c = comps - 1; c >= 0; c--) {
        uint64_t *mine = bits + (size_t)c * words;
        for (int k = start[c]; k < start[c + 1]; k++) {
            int v = members[k];
            for (int e = in->off[v]; e < in->off[v + 1]; e++) {
                int u = in->adj[e];
                int cu = comp[u];
                if (cu == c) continue;
                const uint64_t *theirs = bits + (size_t)cu * words;
                for (size_t w = 0; w < words; w++) mine[w] |= theirs[w];
                for (int j = start[cu]; j < start[cu + 1]; j++) {
                    int x = members[j];
                    mine[x / 64] |= (uint64_t)1 << (x % 64);
                }
            }
        }
        int count = 0;
        for (size_t w = 0; w < words; w++) count += popcount64(mine[w]);
        for (int k = start[c]; k < start[c + 1]; k++) {
            g->nodes[members[k]].stats.blast_radius = count + comp_size[c] - 1;
        }
    }
    free(members);
    free(start);
    free(bits);
}

void graphstats_compute(Graph *g) {
    int n = (int)g->node_count;
    if (n == 0) return;

    Pair *pairs = (Pair *)malloc(sizeof(Pair) * (g->edge_count ? g->edge_count : 1));
    size_t m = 0;
    for (size_t i = 0; i < g->edge_count; i++) {
        int a = g->edges[i].source, b = g->edges[i].target;
        if (a == b || a < 0 || b < 0 || a >= n || b >= n) continue;
        pairs[m].a = a;
        pairs[m].b = b;
        m++;
    }
    qsort(pairs, m, sizeof(Pair), cmp_pair);
    size_t u = 0;
    for (size_t i = 0; i < m; i++) {
        if (u == 0 || cmp_pair(&pairs[i], &pairs[u - 1]) != 0) pairs[u++] = pairs[i];
    }
    m = u;

    for (int v = 0; v < n; v++) g->nodes[v].stats.fan_in = g->nodes[v].stats.fan_out = 0;
    for (size_t i = 0; i < m; i++) {
        g->nodes[pairs[i].a].stats.fan_out++;
        g->nodes[pairs[i].b].stats.fan_in++;
    }

    Csr out = csr_build(pairs, m, n, false);
    Csr in = csr_build(pairs, m, n, true);
    int *comp = (int *)malloc(sizeof(int) * (size_t)n);
    int comps = tarjan(&out, n, comp);

    int *comp_size = (int *)calloc((size_t)comps, sizeof(int));
    for (int v = 0; v < n; v++) comp_size[comp[v]]++;
    for (int v = 0; v < n; v++) {
        int size = comp_size[comp[v]];
        g->nodes[v].stats.cycle_id = size > 1 ? comp[v] : -1;
        g->nodes[v].stats.cycle_size = size > 1 ? size : 0;
    }

    blast_radius(g, &in, comp, comps, comp_size);

    free(comp_size);
    free(comp);
    csr_free(&out);
    csr_free(&in);
    free(pairs);
}

typedef struct {
    long long score;
    int node;
} Scored;

static int cmp_scored(const void *x, const void *y) {
    const Scored *p = (const Scored *)x, *q = (const Scored *)y;
    if (p->score != q->score) return p->score > q->score ? -1 : 1;
    return p->node - q->node;
}

void graphstats_rank_hotspots(Graph *g) {
    Scored *s = (Scored *)malloc(sizeof(Scored) * (g->node_count ? g->node_count : 1));
    size_t k = 0;
    for (size_t i = 0; i < g->node_count; i++) {
        NodeStats *st = &g->nodes[i].stats;
        st->hotspot_rank = 0;
        if (st->git_commits > 0 && st->complexity > 0) {
            s[k].score = (long long)st->git_commits * st->complexity;
            s[k].node = (int)i;
            k++;
        }
    }
    qsort(s, k, sizeof(Scored), cmp_scored);
    for (size_t i = 0; i < k; i++) g->nodes[s[i].node].stats.hotspot_rank = (int)i + 1;
    free(s);
}
