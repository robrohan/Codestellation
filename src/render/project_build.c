/* The whole "Open a project" flow: hash+name a per-project folder under
 * Application Support and build graph.json into it on a worker thread.
 * See project_build.h for the contract. */

#include "project_build.h"
#include "../common/pathutil.h"
#include "../common/thread.h"
#include "../pipeline/build_graph.h"
#include "tinyfiledialogs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#endif

struct ProjectBuild {
    Thread *thread;
    char **roots;
    size_t root_count;
    char *label;
    char *graph_json_path;
    bool ok; /* written by the worker, read only after it has finished */
};

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

/* mkdir -p, one path component at a time -- EEXIST at any level is fine,
 * anything else is a real failure (permissions, not-a-directory, ...). */
static bool mkdir_p(const char *path) {
    char *copy = xstrdup(path);
    size_t len = strlen(copy);
    bool ok = true;
    for (size_t i = 1; i <= len && ok; i++) {
        if (i != len && copy[i] != '/' && copy[i] != '\\') continue;
        char saved = copy[i];
        copy[i] = '\0';
        if (copy[0] != '\0') {
#if defined(_WIN32)
            if (_mkdir(copy) != 0 && errno != EEXIST) ok = false;
#else
            if (mkdir(copy, 0755) != 0 && errno != EEXIST) ok = false;
#endif
        }
        copy[i] = saved;
    }
    free(copy);
    return ok;
}

static void show_error(const char *message) {
    tinyfd_messageBox("Codestellation", message, "ok", "error", 1);
}

static void build_worker(void *arg) {
    ProjectBuild *b = (ProjectBuild *)arg;
    b->ok = build_graph_json((const char *const *)b->roots, b->root_count, b->graph_json_path);
}

static int cmp_str(const void *x, const void *y) {
    return strcmp(*(char *const *)x, *(char *const *)y);
}

static void free_build(ProjectBuild *b) {
    for (size_t i = 0; i < b->root_count; i++) free(b->roots[i]);
    free(b->roots);
    free(b->label);
    free(b->graph_json_path);
    free(b);
}

ProjectBuild *project_build_start(const char *const *dirs, size_t count) {
    if (count == 0) return NULL;
    ProjectBuild *b = (ProjectBuild *)calloc(1, sizeof(ProjectBuild));
    b->roots = (char **)calloc(count, sizeof(char *));
    b->root_count = count;

    /* tinyfd's AppleScript backend sometimes appends a trailing slash to a
     * chosen folder; strip it so names and walk roots are clean. Resolved
     * (symlink-free, absolute) where possible, so the same real directory
     * reached by a different-looking route lands in the same cache. */
    for (size_t i = 0; i < count; i++) {
        char *root = xstrdup(dirs[i]);
        size_t len = strlen(root);
        while (len > 1 && (root[len - 1] == '/' || root[len - 1] == '\\')) root[--len] = '\0';
        char *norm = path_normalize(root);
        if (norm) {
            free(root);
            root = norm;
        }
        b->roots[i] = root;
    }
    qsort(b->roots, count, sizeof(char *), cmp_str);

    /* One folder hashes exactly as it always has (its own path), so caches
     * from before multi-folder builds still line up. */
    size_t key_len = 0;
    for (size_t i = 0; i < count; i++) key_len += strlen(b->roots[i]) + 1;
    char *key = (char *)malloc(key_len + 1);
    key[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        if (i) strcat(key, "\n");
        strcat(key, b->roots[i]);
    }
    uint64_t h = fnv1a(key);
    free(key);

    char hash8[9];
    snprintf(hash8, sizeof(hash8), "%08llx", (unsigned long long)(h & 0xFFFFFFFFULL));

    char label[4200];
    if (count == 1) snprintf(label, sizeof(label), "%s", b->roots[0]);
    else snprintf(label, sizeof(label), "%s (+%zu more)", b->roots[0], count - 1);
    b->label = xstrdup(label);

    const char *home = getenv("HOME");
#if defined(_WIN32)
    if (!home) home = getenv("USERPROFILE"); /* Windows rarely sets HOME */
#endif
    if (!home) {
        show_error("Could not determine your home directory "
                   "(no HOME or USERPROFILE environment variable).");
        free_build(b);
        return NULL;
    }

    char project_dir[4096];
    if (count == 1) {
        snprintf(project_dir, sizeof(project_dir), "%s/Library/Application Support/Codestellation/%s-%s",
                 home, basename_of(b->roots[0]), hash8);
    } else {
        snprintf(project_dir, sizeof(project_dir), "%s/Library/Application Support/Codestellation/%s-and-%zu-%s",
                 home, basename_of(b->roots[0]), count - 1, hash8);
    }

    if (!mkdir_p(project_dir)) {
        char msg[4200];
        snprintf(msg, sizeof(msg), "Could not create:\n%s", project_dir);
        show_error(msg);
        free_build(b);
        return NULL;
    }

    b->graph_json_path = path_join(project_dir, "graph.json");
    b->thread = thread_start(build_worker, b);
    if (!b->thread) {
        show_error("Could not start a background thread to build the project.");
        free_build(b);
        return NULL;
    }
    return b;
}

bool project_build_is_done(ProjectBuild *b) {
    return thread_is_done(b->thread);
}

const char *project_build_label(const ProjectBuild *b) {
    return b->label;
}

char *project_build_finish(ProjectBuild *b) {
    thread_join(b->thread);
    char *result = NULL;
    if (b->ok) {
        result = b->graph_json_path;
        b->graph_json_path = NULL;
    } else {
        char msg[4400];
        snprintf(msg, sizeof(msg), "Building the graph failed for:\n%s\n\nSee the terminal (if any) for details.",
                 b->label);
        show_error(msg);
    }
    free_build(b);
    return result;
}
