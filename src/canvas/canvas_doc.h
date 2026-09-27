#ifndef CODEMAP_CANVAS_DOC_H
#define CODEMAP_CANVAS_DOC_H

#include <stdbool.h>
#include <stddef.h>

/* One JSON Canvas file (https://jsoncanvas.org, v1.0) in memory. Every
 * field the spec defines is kept and written back, so a .canvas made in
 * Obsidian survives a round trip -- except keys the spec doesn't define,
 * which are dropped on save.
 *
 * All strings are malloc'd and owned by the doc; optional ones are NULL
 * when absent. */

typedef enum { CNODE_TEXT, CNODE_FILE, CNODE_LINK, CNODE_GROUP } CanvasNodeType;

typedef struct {
    char *id;
    CanvasNodeType type;
    float x, y, w, h;    /* the spec says integers; written rounded */
    char *color;         /* "1".."6" preset, "#rrggbb", or NULL */
    char *text;          /* text: markdown */
    char *file;          /* file: path */
    char *subpath;       /* file: optional "#heading" / "#^block" / our "#node-id" */
    char *url;           /* link */
    char *label;         /* group: optional */
} CanvasNode;

typedef struct {
    char *id;
    char *from_node, *to_node;
    char *from_side, *to_side;  /* "top"/"right"/"bottom"/"left", or NULL */
    char *from_end, *to_end;    /* "none"/"arrow", or NULL (spec defaults: none / arrow) */
    char *color;
    char *label;
} CanvasEdge;

typedef struct {
    CanvasNode *nodes;
    size_t node_count, node_cap;
    CanvasEdge *edges;
    size_t edge_count, edge_cap;
} CanvasDoc;

void canvas_doc_init(CanvasDoc *doc);
void canvas_doc_free(CanvasDoc *doc);

/* false if the file can't be read or isn't a JSON object; *out is then
 * left empty (initialized) either way, so it's always safe to free. */
bool canvas_doc_read(const char *path, CanvasDoc *out);

/* One node/edge per line, like Obsidian, so diffs stay readable. */
bool canvas_doc_write(const CanvasDoc *doc, const char *path);

/* Writes an empty canvas ({"nodes":[],"edges":[]}) -- creating a child
 * canvas the first time a link to it is followed. */
bool canvas_doc_write_empty(const char *path);

/* Appends a zeroed node/edge (id already filled with a fresh random id)
 * and returns it. The pointer is invalidated by the next append. */
CanvasNode *canvas_doc_add_node(CanvasDoc *doc, CanvasNodeType type);
CanvasEdge *canvas_doc_add_edge(CanvasDoc *doc);

/* Removes a node and every edge touching it / removes one edge. Indices
 * after the removed one shift down by one. */
void canvas_doc_remove_node(CanvasDoc *doc, size_t index);
void canvas_doc_remove_edge(CanvasDoc *doc, size_t index);

/* Index of the node with this id, or -1. */
int canvas_doc_find_node(const CanvasDoc *doc, const char *id);

#endif
