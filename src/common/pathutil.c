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

static bool is_sep(char c) { return c == '/' || c == '\\'; }

char *path_relative(const char *from_dir, const char *to_path) {
    /* Length of the common prefix, snapped back to a separator boundary. */
    size_t i = 0, common = 0;
    while (from_dir[i] && to_path[i] && (from_dir[i] == to_path[i] || (is_sep(from_dir[i]) && is_sep(to_path[i])))) {
        if (is_sep(from_dir[i])) common = i + 1;
        i++;
    }
    if (!from_dir[i] && (is_sep(to_path[i]) || !to_path[i])) common = to_path[i] ? i + 1 : i;
    if (common == 0) return xstrdup(to_path);

    /* One "../" per directory left in from_dir past the common prefix. */
    size_t ups = 0;
    /* common can sit one past the end of from_dir (when to_path is inside
     * it and from_dir has no trailing separator): nothing left there. */
    size_t from_len = strlen(from_dir);
    const char *rest = common < from_len ? from_dir + common : "";
    if (*rest) {
        ups = 1;
        for (const char *p = rest; *p; p++) {
            if (is_sep(*p) && p[1]) ups++;
        }
    }
    const char *tail = to_path + common;
    size_t tail_len = strlen(tail);
    char *out = (char *)malloc(ups * 3 + tail_len + 1);
    size_t o = 0;
    for (size_t k = 0; k < ups; k++) {
        memcpy(out + o, "../", 3);
        o += 3;
    }
    for (size_t k = 0; k < tail_len; k++) out[o++] = is_sep(tail[k]) ? '/' : tail[k];
    out[o] = '\0';
    return out;
}

char *path_clean(const char *path) {
    size_t len = strlen(path);
    /* Output separator: whichever style the path starts with -- the base
     * directory's. A Windows dir joined with a "./x" or "lib/x" specifier
     * ("C:\\proj\\src/./x.ts") must come out all-backslash to match the
     * key the file was declared under. */
    const char *first = strpbrk(path, "/\\");
    char sep = first ? *first : '/';

    /* Root prefix kept verbatim: "/" or a Windows drive "C:\\" / "C:/". */
    size_t root = 0;
    if (len >= 2 && path[1] == ':') root = 2;
    if (root < len && is_sep(path[root])) root++;

    /* Segment start/length pairs, with ".." popping the previous one. */
    size_t cap = len / 2 + 2, count = 0;
    size_t *starts = (size_t *)malloc(cap * sizeof(size_t));
    size_t *lens = (size_t *)malloc(cap * sizeof(size_t));
    size_t i = root;
    while (i < len) {
        while (i < len && is_sep(path[i])) i++;
        size_t s = i;
        while (i < len && !is_sep(path[i])) i++;
        size_t n = i - s;
        if (n == 0 || (n == 1 && path[s] == '.')) continue;
        if (n == 2 && path[s] == '.' && path[s + 1] == '.') {
            /* Pop, unless there's nothing to pop (or only ".." for a
             * relative path, which has to stay). */
            if (count > 0 && !(lens[count - 1] == 2 && path[starts[count - 1]] == '.' &&
                               path[starts[count - 1] + 1] == '.')) {
                count--;
                continue;
            }
            if (root > 0) continue; /* can't climb above the root */
        }
        starts[count] = s;
        lens[count] = n;
        count++;
    }

    char *out = (char *)malloc(len + 2);
    size_t o = 0;
    for (size_t k = 0; k < root; k++) out[o++] = is_sep(path[k]) ? sep : path[k];
    for (size_t k = 0; k < count; k++) {
        if (k > 0) out[o++] = sep;
        memcpy(out + o, path + starts[k], lens[k]);
        o += lens[k];
    }
    if (o == 0) out[o++] = '.';
    out[o] = '\0';
    free(starts);
    free(lens);
    return out;
}

bool path_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
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
                                   "vendor",
                                   /* JS/TS build output and test artifacts */
                                   "dist", ".next", "coverage", NULL };

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
