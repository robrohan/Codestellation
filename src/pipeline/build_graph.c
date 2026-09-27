#include "build_graph.h"
#include "walk.h"
#include "parse.h"
#include "resolve.h"
#include "../lang/registry.h"
#include "../graph/graph.h"
#include "../graph/graph_json.h"
#include <stdio.h>

bool build_graph_json(const char *root, const char *out_path) {
    adapter_registry_init();

    FileList files;
    walk_project(root, &files);
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

    bool ok = graph_write_json(&graph, out_path);
    if (ok) printf("wrote %s\n", out_path);
    else fprintf(stderr, "error: failed to write %s\n", out_path);

    graph_free(&graph);
    parsed_file_list_free(&parsed);
    filelist_free(&files);
    return ok;
}
