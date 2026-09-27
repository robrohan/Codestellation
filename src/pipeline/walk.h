#ifndef CODEMAP_WALK_H
#define CODEMAP_WALK_H

#include <stddef.h>

typedef struct {
    char **paths;
    size_t count, cap;
} FileList;

void filelist_init(FileList *list);
void filelist_free(FileList *list);

/* Recursively walks root, keeping only files whose extension has a
 * registered adapter (see lang/registry.h) — call adapter_registry_init()
 * first. */
void walk_project(const char *root, FileList *out);

/* Same, but appends to an already-initialized list -- for building one
 * graph from several roots. */
void walk_project_append(const char *root, FileList *list);

#endif
