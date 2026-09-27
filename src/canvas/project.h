#ifndef CODEMAP_PROJECT_H
#define CODEMAP_PROJECT_H

#include <stdbool.h>

/* A system-map project: a project.json holding project-wide metadata
 * (title for exports, description) and the root canvas that the
 * breadcrumb trail starts from. On disk:
 *
 *   {
 *     "title": "My System",
 *     "description": "",
 *     "root": "root.canvas",
 *     "created": "2026-09-27T14:22:10Z"
 *   }
 *
 * "root" is relative to the project.json's own directory. */
typedef struct {
    char *path;         /* absolute path to project.json */
    char *dir;          /* its directory */
    char *title;
    char *description;
    char *root;         /* as written in the file */
    char *root_path;    /* resolved absolute path of the root canvas */
    char *created;
} Project;

void project_free(Project *p);

/* Reads project.json at `path`; also creates the root canvas if it's
 * missing. false if the file can't be read or parsed. */
bool project_open(const char *path, Project *out);

/* Creates dir/project.json (refusing to overwrite an existing one) and an
 * empty dir/root.canvas, then opens it. */
bool project_create(const char *dir, const char *title, Project *out);

/* Resolves a link target as written in a canvas (e.g. "eu-west.canvas" or
 * "../services/api") against the directory of the canvas containing it.
 * Absolute targets pass through. Normalized when the target exists.
 * Caller frees. */
char *project_resolve_link(const char *canvas_path, const char *target);

#endif
