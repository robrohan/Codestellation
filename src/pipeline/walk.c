#include "walk.h"
#include "../common/pathutil.h"
#include "../lang/registry.h"
#include <stdlib.h>
#include <string.h>

void filelist_init(FileList *list) {
    list->paths = NULL;
    list->count = 0;
    list->cap = 0;
}

void filelist_free(FileList *list) {
    for (size_t i = 0; i < list->count; i++) free(list->paths[i]);
    free(list->paths);
}

/* Files with a supported extension that are generated noise rather than
 * source: lock files (often megabytes, no dependencies worth drawing) and
 * minified bundles. */
static bool is_ignored_file(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *base = slash && (!bslash || slash > bslash) ? slash + 1 : bslash ? bslash + 1 : path;
    if (strcmp(base, "package-lock.json") == 0 || strcmp(base, "npm-shrinkwrap.json") == 0) return true;
    size_t n = strlen(base);
    return n > 7 && strcmp(base + n - 7, ".min.js") == 0;
}

static void collect_fn(const char *path, void *ctx) {
    FileList *list = (FileList *)ctx;
    if (adapter_for_extension(path_extension(path)) == NULL) return;
    if (is_ignored_file(path)) return;
    if (list->count == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 64;
        list->paths = (char **)realloc(list->paths, list->cap * sizeof(char *));
    }
    list->paths[list->count++] = xstrdup(path);
}

void walk_project(const char *root, FileList *out) {
    filelist_init(out);
    walk_project_append(root, out);
}

void walk_project_append(const char *root, FileList *list) {
    walk_directory(root, collect_fn, list);
}
