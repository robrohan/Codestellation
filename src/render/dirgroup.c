/* See dirgroup.h. The grouping key is normally just the *first* path
 * segment after the graph's common root -- "test_data/foo.c" and
 * "test_data/fixtures/bar.c" both key to "test_data", one group for the
 * whole subtree regardless of how deep it goes. The one wrinkle: a
 * handful of well-known *wrapper* directory names (vendor, node_modules,
 * third_party, ...) are never a meaningful grouping by themselves --
 * "vendor/raylib/..." and "vendor/otherlib/..." should clearly be
 * different groups, not one "vendor" blob -- so when the first segment is
 * one of those, the second segment is folded in too ("vendor/raylib"),
 * and that's what every file under the whole raylib subtree, however
 * deeply nested, collapses back down to. */

#include "dirgroup.h"
#include <string.h>
#include <stdbool.h>

static const char *WRAPPER_DIRS[] = {
    "vendor", "vendored", "third_party", "thirdparty", "node_modules",
    "deps", "external", "extern", "libs", ".deps",
};
#define WRAPPER_DIR_COUNT (sizeof(WRAPPER_DIRS) / sizeof(WRAPPER_DIRS[0]))

static bool ci_equal(const char *a, size_t a_len, const char *b) {
    size_t b_len = strlen(b);
    if (a_len != b_len) return false;
    for (size_t i = 0; i < a_len; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return false;
    }
    return true;
}

static bool is_wrapper_dir(const char *seg, size_t len) {
    for (size_t i = 0; i < WRAPPER_DIR_COUNT; i++) {
        if (ci_equal(seg, len, WRAPPER_DIRS[i])) return true;
    }
    return false;
}

size_t dirgroup_common_prefix_len(const Graph *g) {
    if (g->node_count == 0) return 0;
    const char *first = g->nodes[0].path;
    size_t prefix_len = strlen(first);

    for (size_t i = 1; i < g->node_count && prefix_len > 0; i++) {
        const char *p = g->nodes[i].path;
        size_t j = 0;
        while (j < prefix_len && p[j] != '\0' && p[j] == first[j]) j++;
        prefix_len = j;
    }

    while (prefix_len > 0 && first[prefix_len - 1] != '/' && first[prefix_len - 1] != '\\') {
        prefix_len--;
    }
    return prefix_len;
}

void dirgroup_coarse_key(const char *path, size_t prefix_len, char *out, size_t out_cap) {
    const char *last_slash = strrchr(path, '/');
    const char *last_bslash = strrchr(path, '\\');
    const char *dir_end = path; /* no directory component at all -- empty key */
    if (last_slash && (!last_bslash || last_slash > last_bslash)) dir_end = last_slash;
    else if (last_bslash) dir_end = last_bslash;

    const char *p = path + prefix_len;
    while (p < dir_end && (*p == '/' || *p == '\\')) p++;

    out[0] = '\0';
    if (p >= dir_end) return; /* file sits right at the effective root */

    const char *seg1_start = p;
    while (p < dir_end && *p != '/' && *p != '\\') p++;
    size_t seg1_len = (size_t)(p - seg1_start);

    size_t out_len = 0;
    for (size_t i = 0; i < seg1_len && out_len + 1 < out_cap; i++) out[out_len++] = seg1_start[i];

    if (is_wrapper_dir(seg1_start, seg1_len)) {
        while (p < dir_end && (*p == '/' || *p == '\\')) p++;
        const char *seg2_start = p;
        while (p < dir_end && *p != '/' && *p != '\\') p++;
        size_t seg2_len = (size_t)(p - seg2_start);
        if (seg2_len > 0) {
            if (out_len + 1 < out_cap) out[out_len++] = '/';
            for (size_t i = 0; i < seg2_len && out_len + 1 < out_cap; i++) out[out_len++] = seg2_start[i];
        }
    }
    out[out_len] = '\0';
}
