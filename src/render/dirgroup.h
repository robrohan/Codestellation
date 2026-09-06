#ifndef CODEMAP_DIRGROUP_H
#define CODEMAP_DIRGROUP_H

#include "../graph/graph.h"
#include <stddef.h>

/* Longest common directory-prefix (byte length, snapped to the last path
 * separator) shared by every node's path in the graph -- the effective
 * "root" for coarse grouping, since paths aren't normalized and may be
 * absolute or relative depending on how `codemap-build --root ...` was
 * invoked (an absolute path's first couple of segments are just
 * "/Users/you", not anything meaningful). Shared by dircolor.c (coarse
 * color) and layout3d.c (coarse cluster anchor) so both use the exact
 * same grouping -- see dirgroup.c's top comment for exactly how the key
 * itself is derived from this. */
size_t dirgroup_common_prefix_len(const Graph *g);

/* Writes the NUL-terminated coarse grouping key for `path` into out
 * (out_cap bytes): normally just the first directory segment after
 * prefix_len, or that segment plus the next one if the first is a known
 * "wrapper" directory (vendor, node_modules, third_party, ...) that
 * isn't a meaningful grouping by itself. */
void dirgroup_coarse_key(const char *path, size_t prefix_len, char *out, size_t out_cap);

#endif
