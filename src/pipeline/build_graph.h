#ifndef CODEMAP_BUILD_GRAPH_H
#define CODEMAP_BUILD_GRAPH_H

#include <stdbool.h>

/* The whole pipeline in one call: walk `root` recursively, parse every
 * file a language adapter claims, resolve cross-file references, and
 * write the resulting dependency graph to `out_path` (networkx node-link
 * JSON, see graph/graph_json.h). Returns false if parsing or writing
 * failed. Progress goes to stdout.
 *
 * Safe to run on a worker thread (render/project_build.c does), but not
 * concurrently with itself: adapter_registry_init() and the language
 * adapters keep file-scope state. */
bool build_graph_json(const char *root, const char *out_path);

#endif
