#ifndef CODEMAP_FILESTATS_H
#define CODEMAP_FILESTATS_H

#include "../lang/adapter.h"
#include "../graph/graph.h"

/* Fills the text- and tree-derived parts of *out (lines, blank and comment
 * lines, indentation depth, parse errors, and -- when the adapter lists
 * branch/function node types -- complexity) from one parsed file. Doesn't
 * touch the graph or git fields. */
void filestats_compute(const ParsedFile *file, const LanguageAdapter *adapter, NodeStats *out);

#endif
