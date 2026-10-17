#ifndef CODEMAP_CANVAS_COST_H
#define CODEMAP_CANVAS_COST_H

#include <stddef.h>
#include "canvas_doc.h"
#include "../common/cost.h"

/* How much code sits behind a canvas box or a whole canvas: the box's
 * folder links, plus -- through its canvas links -- every box on the
 * canvases it nests, all the way down. Lines come from each folder set's
 * built graph.json in the code cache (common/code_cache.h), so code that
 * hasn't been opened in 3D yet can't be counted; `unbuilt` says how many
 * folder sets that was. Each file is counted once, however many boxes
 * reach it. Price `code` with common/cost.h.
 *
 * Main-thread only. Parsed graph.json files are kept in memory and reread
 * only when their modification time changes; canvases are reread every
 * call, so call these when something has changed, not every frame. */
typedef struct {
    CodeTotals code;
    int unbuilt; /* folder sets with no graph.json yet */
} CodeSize;

/* canvas_path: the canvas n is on (its links are relative to it). */
CodeSize canvas_cost_node(const CanvasNode *n, const char *canvas_path);

/* Everything reachable from every box on the canvas at canvas_path. */
CodeSize canvas_cost_canvas(const char *canvas_path);

/* Drops the in-memory graph.json cache. */
void canvas_cost_shutdown(void);

#endif
