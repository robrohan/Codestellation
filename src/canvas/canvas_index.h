#ifndef CODEMAP_CANVAS_INDEX_H
#define CODEMAP_CANVAS_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include "canvas_doc.h"

/* Every canvas reachable from a project's root by following canvas links
 * (breadth-first, so each canvas gets its shortest breadcrumb trail), and
 * every box on them -- for project-wide search and for jumping straight to
 * a box on another canvas. Built from what's on disk, so save first. */

typedef struct {
    char *path;     /* absolute, normalized */
    char *title;    /* crumb label: the linking box's title (root: project title) */
    int parent;     /* index of the canvas it was reached from; -1 for the root */
} IndexCanvas;

typedef struct {
    int canvas;             /* into canvases[] */
    char *node_id;
    CanvasNodeType type;
    bool weak_link;         /* references a box elsewhere -- skipped by search */
    char *title;
    char *text;             /* everything searchable, lowercased */
} IndexEntry;

typedef struct {
    IndexCanvas *canvases;
    size_t canvas_count;
    IndexEntry *entries;
    size_t entry_count;
} CanvasIndex;

void canvas_index_build(CanvasIndex *idx, const char *root_path, const char *root_title);
void canvas_index_free(CanvasIndex *idx);

/* Index of the canvas at this (normalized) path, or -1. */
int canvas_index_find_canvas(const CanvasIndex *idx, const char *path);

/* Case-insensitive substring search over every box's text (weak links
 * excluded). Writes up to max entry indices into out, returns how many. An
 * empty query matches nothing. */
size_t canvas_index_search(const CanvasIndex *idx, const char *query, size_t *out, size_t max);

#endif
