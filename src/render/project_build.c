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
    char *root;
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
    b->ok = build_graph_json(b->root, b->graph_json_path);
}

ProjectBuild *project_build_start(const char *picked_dir) {
    /* tinyfd's AppleScript backend sometimes appends a trailing slash to
     * a chosen folder; strip it so basename_of and the walk root are clean. */
    char *root = xstrdup(picked_dir);
    size_t root_len = strlen(root);
    while (root_len > 1 && (root[root_len - 1] == '/' || root[root_len - 1] == '\\')) {
        root[--root_len] = '\0';
    }

    /* Hash the resolved (symlink-free, absolute) path when possible, so
     * re-opening the same real directory by a different-looking route
     * still lands in the same Application Support folder -- fall back to
     * the as-picked path if normalization fails for some reason rather
     * than aborting the whole flow over it. */
    char *normalized = path_normalize(root);
    uint64_t h = fnv1a(normalized ? normalized : root);
    free(normalized);

    char hash8[9];
    snprintf(hash8, sizeof(hash8), "%08llx", (unsigned long long)(h & 0xFFFFFFFFULL));

    const char *home = getenv("HOME");
#if defined(_WIN32)
    if (!home) home = getenv("USERPROFILE"); /* Windows rarely sets HOME */
#endif
    if (!home) {
        show_error("Could not determine your home directory "
                   "(no HOME or USERPROFILE environment variable).");
        free(root);
        return NULL;
    }

    char project_dir[4096];
    snprintf(project_dir, sizeof(project_dir), "%s/Library/Application Support/Codestellation/%s-%s",
             home, basename_of(root), hash8);

    if (!mkdir_p(project_dir)) {
        char msg[4200];
        snprintf(msg, sizeof(msg), "Could not create:\n%s", project_dir);
        show_error(msg);
        free(root);
        return NULL;
    }

    ProjectBuild *b = (ProjectBuild *)calloc(1, sizeof(ProjectBuild));
    b->root = root;
    b->graph_json_path = path_join(project_dir, "graph.json");
    b->thread = thread_start(build_worker, b);
    if (!b->thread) {
        show_error("Could not start a background thread to build the project.");
        free(b->root);
        free(b->graph_json_path);
        free(b);
        return NULL;
    }
    return b;
}

bool project_build_is_done(ProjectBuild *b) {
    return thread_is_done(b->thread);
}

const char *project_build_root(const ProjectBuild *b) {
    return b->root;
}

char *project_build_finish(ProjectBuild *b) {
    thread_join(b->thread);
    char *result = NULL;
    if (b->ok) {
        result = b->graph_json_path;
    } else {
        char msg[4300];
        snprintf(msg, sizeof(msg), "Building the graph failed for:\n%s\n\nSee the terminal (if any) for details.",
                 b->root);
        show_error(msg);
        free(b->graph_json_path);
    }
    free(b->root);
    free(b);
    return result;
}
