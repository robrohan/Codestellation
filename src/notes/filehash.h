#ifndef CODEMAP_FILEHASH_H
#define CODEMAP_FILEHASH_H

#include <stdbool.h>
#include <stdint.h>

/* 64-bit FNV-1a over a file's raw bytes -- a cheap, dependency-free content
 * fingerprint used only to flag "this file changed since a note/position
 * was recorded" (see notes/overlay.h). Not cryptographic, doesn't need to
 * be: staleness detection, not integrity verification. Unrelated to
 * layout3d.c's own fnv1a, which hashes a node's *path string* as a layout
 * PRNG seed rather than file content. */
bool file_content_hash(const char *path, uint64_t *out_hash);

/* Lowercase 16-hex-digit encoding used in graph.overlay.json / graph.notes.md.
 * out_hex must have room for 17 bytes (16 digits + NUL). */
void file_hash_to_hex(uint64_t hash, char out_hex[17]);

/* Parses exactly 16 hex digits back into a hash. Returns false (leaving
 * *out_hash untouched) on anything else -- malformed input from a
 * hand-edited sidecar file should be ignored, not crash the loader. */
bool file_hash_from_hex(const char *hex, uint64_t *out_hash);

#endif
