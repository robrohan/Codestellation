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

static void collect_fn(const char *path, void *ctx) {
    FileList *list = (FileList *)ctx;
    if (adapter_for_extension(path_extension(path)) == NULL) return;
    if (list->count == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 64;
        list->paths = (char **)realloc(list->paths, list->cap * sizeof(char *));
    }
    list->paths[list->count++] = xstrdup(path);
}

void walk_project(const char *root, FileList *out) {
    filelist_init(out);
    walk_directory(root, collect_fn, out);
}
