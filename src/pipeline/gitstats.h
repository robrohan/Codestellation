#ifndef CODEMAP_GITSTATS_H
#define CODEMAP_GITSTATS_H

#include "../graph/graph.h"
#include <stddef.h>

/* Fills git_commits, git_authors and git_last_commit for every node that
 * lives in a git work tree under one of `roots`, from `git log` (merges
 * and renames not followed). Files in a repo that git has never committed
 * get 0 commits; files outside any repo keep -1. Does nothing when git
 * isn't available. Runs git as a subprocess, so it's slow on very long
 * histories -- call it from the build worker, not the UI thread. */
void gitstats_compute(Graph *g, const char *const *roots, size_t root_count);

#endif
