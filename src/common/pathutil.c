#include "pathutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    memcpy(p, s, n);
    return p;
}

char *path_join(const char *dir, const char *rel) {
    size_t dlen = strlen(dir);
    size_t rlen = strlen(rel);
    bool need_sep = dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\';
    size_t total = dlen + (need_sep ? 1 : 0) + rlen + 1;
    char *out = (char *)malloc(total);
    memcpy(out, dir, dlen);
    size_t pos = dlen;
    if (need_sep) out[pos++] = '/';
    memcpy(out + pos, rel, rlen);
    pos += rlen;
    out[pos] = '\0';
    return out;
}

char *path_dirname(const char *path) {
    const char *last_slash = strrchr(path, '/');
    const char *last_bslash = strrchr(path, '\\');
    const char *last = last_slash;
    if (last_bslash && (!last || last_bslash > last)) last = last_bslash;
    if (!last) return xstrdup(".");
    size_t len = (size_t)(last - path);
    if (len == 0) len = 1; /* root "/" */
    char *out = (char *)malloc(len + 1);
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

char *path_normalize(const char *path) {
#ifdef _WIN32
    /* Not MAX_PATH (260) -- real source trees, especially vendored C/C++
     * header forests, blow past that and _fullpath would just fail. */
    char buf[4096];
    if (_fullpath(buf, path, sizeof(buf)) == NULL) return NULL;
    struct stat st;
    if (stat(buf, &st) != 0) return NULL;
    return xstrdup(buf);
#else
    char buf[PATH_MAX];
    if (realpath(path, buf) == NULL) return NULL;
    return xstrdup(buf);
#endif
}

bool path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

const char *path_extension(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *base = path;
    if (slash && (!bslash || slash > bslash)) base = slash + 1;
    else if (bslash) base = bslash + 1;
    const char *dot = strrchr(base, '.');
    return dot ? dot : "";
}

static const char *SKIP_DIRS[] = { ".git", "bin", "obj", "build", "node_modules",
                                   "__pycache__", ".venv", "venv", ".tox", ".mypy_cache",
                                   "vendor", NULL };

static bool should_skip_dir(const char *name) {
    for (int i = 0; SKIP_DIRS[i]; i++) {
        if (strcmp(name, SKIP_DIRS[i]) == 0) return true;
    }
    return false;
}

#ifdef _WIN32

void walk_directory(const char *root, WalkFileFn fn, void *ctx) {
    char pattern[4096]; /* not MAX_PATH -- deep trees exceed 260 */
    snprintf(pattern, sizeof(pattern), "%s\\*", root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char *full = path_join(root, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!should_skip_dir(fd.cFileName)) walk_directory(full, fn, ctx);
        } else {
            fn(full, ctx);
        }
        free(full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

#else

void walk_directory(const char *root, WalkFileFn fn, void *ctx) {
    DIR *d = opendir(root);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char *full = path_join(root, entry->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (!should_skip_dir(entry->d_name)) walk_directory(full, fn, ctx);
            } else if (S_ISREG(st.st_mode)) {
                fn(full, ctx);
            }
        }
        free(full);
    }
    closedir(d);
}

#endif
