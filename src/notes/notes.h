#ifndef CODEMAP_NOTES_H
#define CODEMAP_NOTES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* A single note, either:
 *  - a file note: `path` set, optionally `has_line`/`line` (absent line =
 *    whole-file note), or
 *  - a group note: `group_paths`/`group_path_count` set, `path` NULL.
 * Read from / written to graph.notes.md -- see notes.c's top comment for
 * the on-disk format. */
typedef struct {
    char *id;                                     /* "n3" (file) / "g2" (group) */
    char *path;                                    /* file notes only */
    char **group_paths;
    size_t group_path_count;                       /* group notes only */
    bool has_line;
    int line;
    bool has_hash;
    uint64_t hash;                                  /* file_content_hash() at save time */
    char created[21];                               /* ISO-8601 UTC, e.g. 2026-09-06T14:22:10Z */
    char updated[21];
    char *body;

    /* Internal: this note's exact byte span in the raw text of whatever
     * on-disk file it was just parsed from (see notes.c's splice-based
     * writer). Only meaningful immediately after a fresh read against a
     * specific buffer -- notes_update_note/notes_delete_note recompute it
     * themselves from a fresh re-read rather than trusting a caller's
     * stale copy. Not for use outside notes.c. */
    size_t _leading_sep_start, _block_start, _block_end;
} Note;

typedef struct {
    Note *notes;
    size_t count, cap;
} NoteSet;

void notes_init(NoteSet *ns);
void notes_free(NoteSet *ns);

/* Missing file is not an error -- `out` is just left empty. */
bool notes_read_md(const char *path, NoteSet *out);

/* Each of these re-reads `path` fresh from disk (picking up any hand-edits
 * made since `existing` was last loaded), splices the change into the raw
 * text, writes it back, then reloads `existing` from the result -- so the
 * in-app list always reflects what's actually on disk. Returns false only
 * on a real I/O failure, or (update/delete) if `id` no longer exists. */
bool notes_append_file_note(const char *path, NoteSet *existing,
                             const char *file_path, bool has_hash, uint64_t file_hash,
                             bool has_line, int line, const char *body);
bool notes_append_group_note(const char *path, NoteSet *existing,
                              const char **paths, size_t path_count, const char *body);
bool notes_update_note(const char *path, NoteSet *existing, const char *id,
                        bool has_line, int line, const char *body);
bool notes_delete_note(const char *path, NoteSet *existing, const char *id);

/* Writes up to max_out matching notes into `out`; returns the number
 * found. A file note matches on exact path; a group note matches if
 * file_path is one of its members. */
size_t notes_find_for_path(const NoteSet *ns, const char *file_path,
                            const Note **out, size_t max_out);

/* A group note matches only if its path set is exactly (order-independent)
 * the given set -- "notes about this exact selection". */
size_t notes_find_for_group(const NoteSet *ns, const char **paths, size_t path_count,
                             const Note **out, size_t max_out);

#endif
