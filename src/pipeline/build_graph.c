#include "build_graph.h"
#include "walk.h"
#include "parse.h"
#include "resolve.h"
#include "filestats.h"
#include "graphstats.h"
#include "gitstats.h"
#include "../lang/registry.h"
#include "../graph/graph.h"
#include "../graph/graph_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Sorts and drops exact duplicates -- nested roots walk the same files. */
static void dedupe(FileList *files) {
    if (files->count < 2) return;
    qsort(files->paths, files->count, sizeof(char *), cmp_str);
    size_t out = 1;
    for (size_t i = 1; i < files->count; i++) {
        if (strcmp(files->paths[i], files->paths[out - 1]) == 0) {
            free(files->paths[i]);
        } else {
            files->paths[out++] = files->paths[i];
        }
    }
    files->count = out;
}

bool build_graph_json(const char *const *roots, size_t root_count, const char *out_path) {
    adapter_registry_init();

    FileList files;
    filelist_init(&files);
    for (size_t i = 0; i < root_count; i++) walk_project_append(roots[i], &files);
    if (root_count > 1) dedupe(&files);
    printf("found %zu source files\n", files.count);

    ParsedFileList parsed;
    if (!parse_all(&files, &parsed)) {
        fprintf(stderr, "error: parse_all failed\n");
        parsed_file_list_free(&parsed);
        filelist_free(&files);
        return false;
    }
    printf("parsed %zu files\n", parsed.count);

    Graph graph;
    graph_init(&graph);
    int unresolved = 0;
    resolve_build_graph(&parsed, &graph, &unresolved);
    printf("graph: %zu nodes, %zu edges (%d unresolved references)\n",
           graph.node_count, graph.edge_count, unresolved);

    /* Node i is parsed.entries[i] (resolve_build_graph adds them in order). */
    for (size_t i = 0; i < parsed.count && i < graph.node_count; i++) {
        filestats_compute(&parsed.entries[i].parsed, parsed.entries[i].adapter, &graph.nodes[i].stats);
    }
    graphstats_compute(&graph);
    gitstats_compute(&graph, roots, root_count);
    graphstats_rank_hotspots(&graph);
    printf("stats: done\n");

    bool ok = graph_write_json(&graph, out_path);
    if (ok) printf("wrote %s\n", out_path);
    else fprintf(stderr, "error: failed to write %s\n", out_path);

    graph_free(&graph);
    parsed_file_list_free(&parsed);
    filelist_free(&files);
    return ok;
}
