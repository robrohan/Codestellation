#ifndef CODEMAP_DUPES_H
#define CODEMAP_DUPES_H

#include "parse.h"
#include "../graph/graph.h"

/* Smallest copy worth reporting: at least this many tokens, spanning at
 * least this many lines in both places. */
#define DUPES_MIN_TOKENS 50
#define DUPES_MIN_LINES 5

/* Finds stretches of code that appear more than once anywhere in the
 * project -- across files and folders, or twice in one file -- and adds
 * them to g->clones. Node i must be parsed->entries[i] (as
 * resolve_build_graph adds them). Only code is compared: files whose
 * adapter lists branch_types (adapter.h), so data and markup (JSON,
 * Markdown, HTML...) are skipped, as are files over 1 MB (generated), and
 * only files of the same language match each other. */
void dupes_compute(const ParsedFileList *parsed, Graph *g);

#endif
