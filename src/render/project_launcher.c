/* The whole "Open a project" flow: hash+name a per-project folder under
 * Application Support, run codemap-build into it, and hand off to a
 * fresh codemap-view process pointed at the result. See
 * project_launcher.h for the entry point's contract. */

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "project_launcher.h"
#include "../common/pathutil.h"
#include "tinyfiledialogs.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#define execv _execv
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

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

/* Wraps s in single quotes for a POSIX shell command line, escaping any
 * embedded ' as the standard '\'' trick (end quote, literal escaped
 * quote, resume quoting). Not attempting general Windows cmd.exe
 * quoting here -- unverified on Windows, matching this project's
 * existing stance on other platform-specific paths. */
static char *shell_quote(const char *s) {
    size_t len = strlen(s);
    char *out = (char *)malloc(len * 4 + 3);
    size_t o = 0;
    out[o++] = '\'';
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\'') {
            out[o++] = '\'';
            out[o++] = '\\';
            out[o++] = '\'';
            out[o++] = '\'';
        } else {
            out[o++] = s[i];
        }
    }
    out[o++] = '\'';
    out[o] = '\0';
    return out;
}

static void show_error(const char *message) {
    tinyfd_messageBox("Codestellation", message, "ok", "error", 1);
}

void project_launcher_build_and_relaunch(const char *self_exe_path, const char *picked_dir) {
    /* tinyfd's AppleScript backend sometimes appends a trailing slash to
     * a chosen folder; strip it so basename_of and --root are clean. */
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
    if (!home) {
        show_error("Could not determine your home directory (no HOME environment variable).");
        free(root);
        return;
    }

    char project_dir[4096];
    snprintf(project_dir, sizeof(project_dir), "%s/Library/Application Support/Codestellation/%s-%s",
             home, basename_of(root), hash8);

    if (!mkdir_p(project_dir)) {
        char msg[4200];
        snprintf(msg, sizeof(msg), "Could not create:\n%s", project_dir);
        show_error(msg);
        free(root);
        return;
    }

    char graph_json_path[4200];
    snprintf(graph_json_path, sizeof(graph_json_path), "%s/graph.json", project_dir);

    char *exe_dir = path_dirname(self_exe_path);
    char *codemap_build_path = path_join(exe_dir, "codemap-build");
    free(exe_dir);

    char *q_build = shell_quote(codemap_build_path);
    char *q_root = shell_quote(root);
    char *q_out = shell_quote(graph_json_path);
    size_t cmd_cap = strlen(q_build) + strlen(q_root) + strlen(q_out) + 64;
    char *cmd = (char *)malloc(cmd_cap);
    snprintf(cmd, cmd_cap, "%s --root %s --out %s", q_build, q_root, q_out);

    int rc = system(cmd);

    free(q_build);
    free(q_root);
    free(q_out);
    free(cmd);
    free(codemap_build_path);

#if defined(_WIN32)
    bool build_ok = (rc == 0);
#else
    bool build_ok = WIFEXITED(rc) && WEXITSTATUS(rc) == 0;
#endif

    if (!build_ok) {
        char msg[4300];
        snprintf(msg, sizeof(msg), "codemap-build failed for:\n%s\n\nSee the terminal (if any) for details.", root);
        show_error(msg);
        free(root);
        return;
    }

    free(root);

    /* Process-replace rather than a full quit/relaunch cycle -- see
     * project_launcher.h. glfwTerminate first for a clean handoff rather
     * than leaving GL/window state to be reclaimed mid-transition. */
    glfwTerminate();
    char *argv_new[3] = { (char *)self_exe_path, graph_json_path, NULL };
    execv(self_exe_path, argv_new);

    /* Only reached if execv itself failed to launch at all. */
    fprintf(stderr, "error: failed to relaunch %s\n", self_exe_path);
}
