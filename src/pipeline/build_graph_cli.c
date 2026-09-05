#include "walk.h"
#include "parse.h"
#include "resolve.h"
#include "../lang/registry.h"
#include "../graph/graph.h"
#include "../graph/graph_json.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3 || strcmp(argv[1], "--root") != 0) {
        fprintf(stderr, "usage: %s --root <dir> [--out <graph.json>]\n", argv[0]);
        return 1;
    }
    const char *root = argv[2];
    const char *out_path = "graph.json";
    if (argc >= 5 && strcmp(argv[3], "--out") == 0) out_path = argv[4];

    adapter_registry_init();

    FileList files;
    walk_project(root, &files);
    printf("found %zu source files\n", files.count);

    ParsedFileList parsed;
    if (!parse_all(&files, &parsed)) {
        fprintf(stderr, "error: parse_all failed\n");
        return 1;
    }
    printf("parsed %zu files\n", parsed.count);

    Graph graph;
    graph_init(&graph);
    int unresolved = 0;
    resolve_build_graph(&parsed, &graph, &unresolved);
    printf("graph: %zu nodes, %zu edges (%d unresolved references)\n",
           graph.node_count, graph.edge_count, unresolved);

    if (!graph_write_json(&graph, out_path)) {
        fprintf(stderr, "error: failed to write %s\n", out_path);
        return 1;
    }
    printf("wrote %s\n", out_path);

    graph_free(&graph);
    parsed_file_list_free(&parsed);
    filelist_free(&files);
    return 0;
}
