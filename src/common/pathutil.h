#ifndef CODEMAP_PATHUTIL_H
#define CODEMAP_PATHUTIL_H

#include <stdbool.h>

/* malloc'd copy of s; caller frees. Used throughout instead of libc
 * strdup, which isn't available without an underscore prefix on MSVC. */
char *xstrdup(const char *s);

/* All returned strings are malloc'd; caller frees. */
char *path_join(const char *dir, const char *rel);
char *path_dirname(const char *path);
/* Resolves to an absolute, symlink-free path. Returns NULL if the path
 * doesn't exist. */
char *path_normalize(const char *path);
bool  path_exists(const char *path);
bool  path_is_dir(const char *path);
/* to_path relative to from_dir (both absolute, e.g. from path_normalize),
 * using '/' separators: "../x/y.canvas". Falls back to a copy of to_path
 * when they share no root (different Windows drives). Caller frees. */
char *path_relative(const char *from_dir, const char *to_path);
/* Collapses "." and ".." segments and doubled separators without touching
 * the disk ("/a/b/../c/./d" -> "/a/c/d"), keeping a leading "/" or drive
 * prefix and the style of the path's first separator (a Windows base dir
 * joined with a "./x" specifier stays all-backslash). For resolving
 * relative imports against already-normalized file paths cheaply -- no
 * realpath() per candidate. Caller frees. */
char *path_clean(const char *path);

/* Returns a pointer *into* path (not malloc'd) at the last "." in the
 * final path component, or "" if there is none. */
const char *path_extension(const char *path);

typedef void (*WalkFileFn)(const char *path, void *ctx);
/* Recursively visits every regular file under root, skipping common
 * non-source directories (.git, bin, obj, build, node_modules). */
void walk_directory(const char *root, WalkFileFn fn, void *ctx);

#endif
