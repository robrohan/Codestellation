#ifndef CODEMAP_RESOLVE_H
#define CODEMAP_RESOLVE_H

#include <stdbool.h>
#include "parse.h"
#include "../graph/graph.h"

/* Two-pass resolution: build a qualified-name -> file-id symbol table from
 * every adapter's extract_declarations, then run every extract_references
 * fact through adapter->resolve_reference + a symbol-table lookup to emit
 * GraphEdges. Unresolved references are counted, not fatal. */
bool resolve_build_graph(const ParsedFileList *files, Graph *out_graph, int *out_unresolved);

#endif
