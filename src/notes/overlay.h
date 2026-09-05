#ifndef CODEMAP_OVERLAY_H
#define CODEMAP_OVERLAY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* A manually-dragged node position, keyed by the exact path string as it
 * appears in graph.json (paths are never normalized anywhere in this
 * codebase -- see pathutil.h -- so this matches the existing precedent). */
typedef struct {
    char *path;
    uint64_t hash;   /* file_content_hash() at the time this was saved */
    float x, y, z;
} OverlayEntry;

typedef struct {
    OverlayEntry *entries;
    size_t count, cap;
} Overlay;

void overlay_init(Overlay *ov);
void overlay_free(Overlay *ov);

/* Adds a new entry or updates the position/hash of an existing one for
 * `path` (compared by exact string match). */
void overlay_set(Overlay *ov, const char *path, uint64_t hash, float x, float y, float z);

/* Returns the entry for `path`, or NULL if the file has never been
 * repositioned. */
const OverlayEntry *overlay_find(const Overlay *ov, const char *path);

/* Missing file is not an error -- `out` is left empty/initialized, since
 * an overlay simply doesn't exist yet for a graph nobody has repositioned
 * anything in. Returns false only on a real parse failure of an existing
 * file. */
bool overlay_read_json(const char *path, Overlay *out);
bool overlay_write_json(const Overlay *ov, const char *path);

#endif
