#ifndef CODEMAP_CODE_CACHE_H
#define CODEMAP_CODE_CACHE_H

#include <stddef.h>

/* Where a set of code folders keeps its built graph.json, notes and
 * dragged positions:
 *   ~/Library/Application Support/Codestellation/<name>-<hash8>
 * (<name>-and-<N>-<hash8> for several folders). Shared by the build
 * (render/project_build.c) and the export (canvas/export.c) so they
 * always agree. */

/* Cleans up a folder set into the canonical form the cache is keyed on:
 * trailing separators stripped, resolved to absolute symlink-free paths
 * where they exist, sorted. Returns a malloc'd array of malloc'd strings
 * (count entries); free with code_cache_free_dirs. */
char **code_cache_normalize_dirs(const char *const *dirs, size_t count);
void code_cache_free_dirs(char **dirs, size_t count);

/* The cache folder for an already-normalized set. NULL if the home
 * directory can't be determined (no HOME / USERPROFILE). Doesn't create
 * it. Caller frees. */
char *code_cache_dir(char *const *normalized_dirs, size_t count);

#endif
