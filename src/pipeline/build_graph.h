#ifndef CODEMAP_BUILD_GRAPH_H
#define CODEMAP_BUILD_GRAPH_H

#include <stdbool.h>
#include <stddef.h>

/* The whole pipeline in one call: walk every root recursively, parse every
 * file a language adapter claims, resolve cross-file references across
 * all of them, and write the resulting dependency graph to `out_path`
 * (networkx node-link JSON, see graph/graph_json.h). Overlapping roots
 * (one inside another) don't produce duplicate nodes. Returns false if
 * parsing or writing failed. Progress goes to stdout.
 *
 * Safe to run on a worker thread (render/project_build.c does), but not
 * concurrently with itself: adapter_registry_init() and the language
 * adapters keep file-scope state. */
bool build_graph_json(const char *const *roots, size_t root_count, const char *out_path);

#endif
