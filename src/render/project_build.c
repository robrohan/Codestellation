/* The whole "Open a project" flow: hash+name a per-project folder under
 * Application Support and build graph.json into it on a worker thread.
 * See project_build.h for the contract. */

#include "project_build.h"
#include "../common/pathutil.h"
#include "../common/thread.h"
#include "../common/code_cache.h"
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

static void free_build(ProjectBuild *b) {
    code_cache_free_dirs(b->roots, b->root_count);
    free(b->label);
    free(b->graph_json_path);
    free(b);
}

ProjectBuild *project_build_start(const char *const *dirs, size_t count) {
    if (count == 0) return NULL;
    ProjectBuild *b = (ProjectBuild *)calloc(1, sizeof(ProjectBuild));
    b->roots = code_cache_normalize_dirs(dirs, count);
    b->root_count = count;

    char label[4200];
    if (count == 1) snprintf(label, sizeof(label), "%s", b->roots[0]);
    else snprintf(label, sizeof(label), "%s (+%zu more)", b->roots[0], count - 1);
    b->label = xstrdup(label);

    char *project_dir = code_cache_dir(b->roots, count);
    if (!project_dir) {
        show_error("Could not determine your home directory "
                   "(no HOME or USERPROFILE environment variable).");
        free_build(b);
        return NULL;
    }

    if (!mkdir_p(project_dir)) {
        char msg[4200];
        snprintf(msg, sizeof(msg), "Could not create:\n%s", project_dir);
        show_error(msg);
        free(project_dir);
        free_build(b);
        return NULL;
    }

    b->graph_json_path = path_join(project_dir, "graph.json");
    free(project_dir);
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
