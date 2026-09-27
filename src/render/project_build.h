#ifndef CODEMAP_PROJECT_BUILD_H
#define CODEMAP_PROJECT_BUILD_H

#include <stdbool.h>

/* The "Open a project" flow, run in-process without blocking the window:
 * resolve a picked directory to its Application Support project folder,
 * then build its graph.json there on a worker thread (see
 * pipeline/build_graph.h). main.c polls for completion each frame and
 * swaps the new graph in when it's ready.
 *
 * All three calls are main-thread only; only one build may run at a time
 * (the pipeline isn't reentrant). */
typedef struct ProjectBuild ProjectBuild;

/* picked_dir: an absolute, existing directory (e.g. straight from
 * tinyfd_selectFolderDialog). Returns NULL -- having already shown a
 * native error dialog -- if the project folder can't be created or the
 * worker can't be started. */
ProjectBuild *project_build_start(const char *picked_dir);

/* Non-blocking: true once the worker has finished (successfully or not). */
bool project_build_is_done(ProjectBuild *b);

/* The directory being built, trailing slashes stripped -- for status text. */
const char *project_build_root(const ProjectBuild *b);

/* Waits for the worker if it hasn't finished, then frees b. On success
 * returns the newly malloc'd path to the built graph.json (caller frees);
 * on failure shows a native error dialog and returns NULL. */
char *project_build_finish(ProjectBuild *b);

#endif
