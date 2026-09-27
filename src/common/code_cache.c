#include "code_cache.h"
#include "pathutil.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fnv1a(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *base = path;
    if (slash && (!bslash || slash > bslash)) base = slash + 1;
    else if (bslash) base = bslash + 1;
    return base;
}

static int cmp_str(const void *x, const void *y) {
    return strcmp(*(char *const *)x, *(char *const *)y);
}

char **code_cache_normalize_dirs(const char *const *dirs, size_t count) {
    char **out = (char **)calloc(count ? count : 1, sizeof(char *));
    for (size_t i = 0; i < count; i++) {
        /* tinyfd's AppleScript backend sometimes appends a trailing slash
         * to a chosen folder. */
        char *root = xstrdup(dirs[i]);
        size_t len = strlen(root);
        while (len > 1 && (root[len - 1] == '/' || root[len - 1] == '\\')) root[--len] = '\0';
        char *norm = path_normalize(root);
        if (norm) {
            free(root);
            root = norm;
        }
        out[i] = root;
    }
    qsort(out, count, sizeof(char *), cmp_str);
    return out;
}

void code_cache_free_dirs(char **dirs, size_t count) {
    for (size_t i = 0; i < count; i++) free(dirs[i]);
    free(dirs);
}

char *code_cache_dir(char *const *dirs, size_t count) {
    if (count == 0) return NULL;
    const char *home = getenv("HOME");
#if defined(_WIN32)
    if (!home) home = getenv("USERPROFILE"); /* Windows rarely sets HOME */
#endif
    if (!home) return NULL;

    /* One folder hashes exactly as it always has (its own path), so caches
     * from before multi-folder builds still line up. */
    size_t key_len = 0;
    for (size_t i = 0; i < count; i++) key_len += strlen(dirs[i]) + 1;
    char *key = (char *)malloc(key_len + 1);
    key[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        if (i) strcat(key, "\n");
        strcat(key, dirs[i]);
    }
    uint64_t h = fnv1a(key);
    free(key);

    char out[4096];
    unsigned long long h8 = (unsigned long long)(h & 0xFFFFFFFFULL);
    if (count == 1) {
        snprintf(out, sizeof(out), "%s/Library/Application Support/Codestellation/%s-%08llx", home,
                 basename_of(dirs[0]), h8);
    } else {
        snprintf(out, sizeof(out), "%s/Library/Application Support/Codestellation/%s-and-%zu-%08llx", home,
                 basename_of(dirs[0]), count - 1, h8);
    }
    return xstrdup(out);
}
