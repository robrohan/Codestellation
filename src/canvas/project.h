#ifndef CODEMAP_PROJECT_H
#define CODEMAP_PROJECT_H

#include <stdbool.h>
#include <stddef.h>
#include "canvas_doc.h"
#include "../common/cost.h"

/* A system-map project: a project.json holding project-wide metadata
 * (title for exports, description) and the root canvas that the
 * breadcrumb trail starts from. On disk:
 *
 *   {
 *     "title": "My System",
 *     "description": "",
 *     "root": "root.canvas",
 *     "created": "2026-09-27T14:22:10Z",
 *     "cost": {
 *       "currency": "$",
 *       "salary": 56286,
 *       "overhead": 2.4,
 *       "mode": "organic",
 *       "ai_share": 0,
 *       "token_price": 10,
 *       "reasoning_multiplier": 300,
 *       "chars_per_token": 4
 *     }
 *   }
 *
 * "root" is relative to the project.json's own directory. "cost" holds
 * the COCOMO settings (common/cost.h); it and each key in it are optional,
 * with missing ones taking cost_params_default's values. */
typedef struct {
    char *path;         /* absolute path to project.json */
    char *dir;          /* its directory */
    char *title;
    char *description;
    char *root;         /* as written in the file */
    char *root_path;    /* resolved absolute path of the root canvas */
    char *created;
    CostParams cost;
} Project;

void project_free(Project *p);

/* Reads project.json at `path`; also creates the root canvas if it's
 * missing. false if the file can't be read or parsed, or has no "root"
 * (so it isn't a project). */
bool project_open(const char *path, Project *out);

/* Creates dir/project.json (refusing to overwrite an existing one) and an
 * empty dir/root.canvas, then opens it. */
bool project_create(const char *dir, const char *title, Project *out);

/* Rewrites project.json from p, including the current cost settings.
 * Only the keys above are written; any others in the file are dropped. */
bool project_save(const Project *p);

/* Resolves a link target as written in a canvas (e.g. "eu-west.canvas" or
 * "../services/api") against the directory of the canvas containing it.
 * Absolute targets pass through. Normalized when the target exists.
 * Caller frees. */
char *project_resolve_link(const char *canvas_path, const char *target);

/* The existing folders node n links to -- [[targets]] in a text node (or
 * a file node's path) that aren't .canvas files and resolve, relative to
 * canvas_path, to a directory. Returns the count; *out is a malloc'd
 * array of malloc'd absolute paths (NULL when 0). */
size_t canvas_node_dir_links(const CanvasNode *n, const char *canvas_path, char ***out);

#endif
