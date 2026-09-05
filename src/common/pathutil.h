#ifndef CODEMAP_PATHUTIL_H
#define CODEMAP_PATHUTIL_H

#include <stdbool.h>

/* All returned strings are malloc'd; caller frees. */
char *path_join(const char *dir, const char *rel);
char *path_dirname(const char *path);
/* Resolves to an absolute, symlink-free path. Returns NULL if the path
 * doesn't exist. */
char *path_normalize(const char *path);
bool  path_exists(const char *path);

/* Returns a pointer *into* path (not malloc'd) at the last "." in the
 * final path component, or "" if there is none. */
const char *path_extension(const char *path);

typedef void (*WalkFileFn)(const char *path, void *ctx);
/* Recursively visits every regular file under root, skipping common
 * non-source directories (.git, bin, obj, build, node_modules). */
void walk_directory(const char *root, WalkFileFn fn, void *ctx);

#endif
