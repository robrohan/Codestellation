/* graph.notes.md format -- real Markdown, meant to be opened and read like
 * a normal document, but parseable by the small hand-rolled scanner below
 * (no markdown library, matching this codebase's hand-rolled-JSON-writer
 * convention in graph_json.c).
 *
 * Layout: free-form preamble text, then a sequence of notes each bounded
 * by a line that is exactly "---" on its own. Adjacent notes share their
 * boundary separator, so N notes need exactly N+1 separator lines (one
 * before the first, one after each note). Each note looks like:
 *
 *   ## src/render/main.c:214
 *
 *   <!-- codestellation-note id=n1 path="src/render/main.c" hash=... line=214 created=... updated=... -->
 *
 *   free-text body, as many lines as you like
 *
 * or, for a whole-file note, the same shape without `line=`. A group note
 * (several files at once) uses a `codestellation-group` marker instead:
 *
 *   ## Group: 3 files
 *
 *   <!-- codestellation-group id=g1 paths="a.c|b.c|c.c" created=... updated=... -->
 *
 *   body
 *
 * The HTML-comment marker line carries all the structured data (id, path
 * or pipe-separated paths, optional line/hash, timestamps) and is invisible
 * in any markdown renderer; the "## " heading above it is purely for human
 * readability and is never parsed back -- it's regenerated fresh from the
 * marker's fields on every write, so it doesn't matter that this parser
 * ignores its exact text.
 *
 * Add/update/delete all work by splicing byte ranges of the raw file text
 * rather than fully re-serializing the document from a parsed model, so
 * that any hand-added content outside a recognized note block (extra
 * paragraphs, headings) survives untouched across app saves -- the whole
 * point of this being real Markdown instead of a JSON dump with an .md
 * extension. */

#include "notes.h"
#include "filehash.h"
#include "../common/pathutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

/* -- tiny growable string buffer, used only for building output text -- */

typedef struct { char *data; size_t len, cap; } strbuf;

static void strbuf_init(strbuf *sb) { sb->data = NULL; sb->len = 0; sb->cap = 0; }

static void strbuf_free(strbuf *sb) { free(sb->data); sb->data = NULL; sb->len = sb->cap = 0; }

static void strbuf_ensure(strbuf *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return;
    size_t new_cap = sb->cap ? sb->cap * 2 : 256;
    while (new_cap < sb->len + extra + 1) new_cap *= 2;
    sb->data = (char *)realloc(sb->data, new_cap);
    sb->cap = new_cap;
}

static void strbuf_append_n(strbuf *sb, const char *s, size_t n) {
    strbuf_ensure(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static void strbuf_append(strbuf *sb, const char *s) { strbuf_append_n(sb, s, strlen(s)); }

static void strbuf_appendf(strbuf *sb, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) { va_end(ap2); return; }
    strbuf_ensure(sb, (size_t)needed);
    vsnprintf(sb->data + sb->len, (size_t)needed + 1, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)needed;
}

/* Takes ownership of the buffer; sb is left empty. Never returns NULL. */
static char *strbuf_take(strbuf *sb) {
    char *d = sb->data ? sb->data : xstrdup("");
    sb->data = NULL;
    sb->len = sb->cap = 0;
    return d;
}

/* -- file I/O: never hard-fails on a missing file, matching the rest of
 * this feature's "sidecar files are optional until you save one" stance -- */

static void slurp_file(const char *path, char **out_buf, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { *out_buf = xstrdup(""); *out_len = 0; return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); *out_buf = xstrdup(""); *out_len = 0; return; }
    char *buf = (char *)malloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    *out_buf = buf;
    *out_len = got;
}

static bool write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite(data, 1, len, f);
    fclose(f);
    return true;
}

static void now_iso8601(char out[21]) {
    time_t t = time(NULL);
    struct tm tm_utc;
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    strftime(out, 21, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

/* -- NoteSet plumbing -- */

void notes_init(NoteSet *ns) { ns->notes = NULL; ns->count = ns->cap = 0; }

void notes_free(NoteSet *ns) {
    for (size_t i = 0; i < ns->count; i++) {
        Note *n = &ns->notes[i];
        free(n->id);
        free(n->path);
        free(n->body);
        for (size_t g = 0; g < n->group_path_count; g++) free(n->group_paths[g]);
        free(n->group_paths);
    }
    free(ns->notes);
    ns->notes = NULL;
    ns->count = ns->cap = 0;
}

static Note *notes_push(NoteSet *ns) {
    if (ns->count == ns->cap) {
        ns->cap = ns->cap ? ns->cap * 2 : 8;
        ns->notes = (Note *)realloc(ns->notes, ns->cap * sizeof(Note));
    }
    return &ns->notes[ns->count++];
}

static Note *find_note_by_id(NoteSet *ns, const char *id) {
    for (size_t i = 0; i < ns->count; i++) {
        if (strcmp(ns->notes[i].id, id) == 0) return &ns->notes[i];
    }
    return NULL;
}

static int max_id_number(const NoteSet *ns) {
    int max_n = 0;
    for (size_t i = 0; i < ns->count; i++) {
        const char *id = ns->notes[i].id;
        if (!id || !id[0]) continue;
        int n = atoi(id + 1); /* skip the 'n'/'g' prefix */
        if (n > max_n) max_n = n;
    }
    return max_n;
}

/* -- attribute tokenizer for a marker's "<!-- codestellation-... KEY=VAL ... -->"
 * interior text -- values are bare (no spaces) or double-quoted -- */

typedef struct { char key[16]; char val[512]; } Attr;

static size_t tokenize_attrs(const char *s, const char *end, Attr *out, size_t max_out) {
    size_t n = 0;
    const char *p = s;
    while (p < end && n < max_out) {
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end) break;
        const char *key_start = p;
        while (p < end && *p != '=' && !isspace((unsigned char)*p)) p++;
        size_t key_len = (size_t)(p - key_start);
        if (p >= end || *p != '=') {
            while (p < end && !isspace((unsigned char)*p)) p++; /* skip malformed token */
            continue;
        }
        p++; /* skip '=' */
        char valbuf[512];
        size_t vlen = 0;
        if (p < end && *p == '"') {
            p++;
            while (p < end && *p != '"' && vlen < sizeof(valbuf) - 1) valbuf[vlen++] = *p++;
            if (p < end && *p == '"') p++;
        } else {
            while (p < end && !isspace((unsigned char)*p) && vlen < sizeof(valbuf) - 1) valbuf[vlen++] = *p++;
        }
        valbuf[vlen] = '\0';
        if (key_len < sizeof(out[n].key)) {
            memcpy(out[n].key, key_start, key_len);
            out[n].key[key_len] = '\0';
            strncpy(out[n].val, valbuf, sizeof(out[n].val) - 1);
            out[n].val[sizeof(out[n].val) - 1] = '\0';
            n++;
        }
    }
    return n;
}

static const char *attr_find(Attr *attrs, size_t n, const char *key) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(attrs[i].key, key) == 0) return attrs[i].val;
    }
    return NULL;
}

/* -- parsing a single note out of the raw text between two separators -- */

static bool parse_block(const char *raw, size_t block_start, size_t block_end, Note *out_note) {
    const char *chunk = raw + block_start;
    size_t chunk_len = block_end - block_start;
    const char *chunk_end = chunk + chunk_len;

    static const char note_kw[] = "<!-- codestellation-note ";
    static const char group_kw[] = "<!-- codestellation-group ";
    size_t note_kw_len = sizeof(note_kw) - 1;
    size_t group_kw_len = sizeof(group_kw) - 1;

    const char *marker = NULL;
    bool is_group = false;
    size_t kw_len = 0;
    for (size_t i = 0; i + note_kw_len <= chunk_len; i++) {
        if (memcmp(chunk + i, note_kw, note_kw_len) == 0) { marker = chunk + i; kw_len = note_kw_len; break; }
    }
    if (!marker) {
        for (size_t i = 0; i + group_kw_len <= chunk_len; i++) {
            if (memcmp(chunk + i, group_kw, group_kw_len) == 0) {
                marker = chunk + i;
                kw_len = group_kw_len;
                is_group = true;
                break;
            }
        }
    }
    if (!marker) return false; /* not a recognized note -- leave it alone */

    const char *attr_start = marker + kw_len;
    const char *close = NULL;
    for (const char *p = attr_start; p + 3 <= chunk_end; p++) {
        if (memcmp(p, "-->", 3) == 0) { close = p; break; }
    }
    if (!close) return false;

    Attr attrs[8];
    size_t nattrs = tokenize_attrs(attr_start, close, attrs, 8);
    const char *id_v = attr_find(attrs, nattrs, "id");
    if (!id_v) return false;

    memset(out_note, 0, sizeof(*out_note));
    out_note->id = xstrdup(id_v);
    const char *created_v = attr_find(attrs, nattrs, "created");
    const char *updated_v = attr_find(attrs, nattrs, "updated");
    if (created_v) strncpy(out_note->created, created_v, sizeof(out_note->created) - 1);
    if (updated_v) strncpy(out_note->updated, updated_v, sizeof(out_note->updated) - 1);

    if (is_group) {
        const char *paths_v = attr_find(attrs, nattrs, "paths");
        if (paths_v) {
            size_t cap = 4, cnt = 0;
            char **arr = (char **)malloc(cap * sizeof(char *));
            const char *p = paths_v;
            while (*p) {
                const char *seg_start = p;
                while (*p && *p != '|') p++;
                size_t seg_len = (size_t)(p - seg_start);
                if (cnt == cap) { cap *= 2; arr = (char **)realloc(arr, cap * sizeof(char *)); }
                char *seg = (char *)malloc(seg_len + 1);
                memcpy(seg, seg_start, seg_len);
                seg[seg_len] = '\0';
                arr[cnt++] = seg;
                if (*p == '|') p++;
            }
            out_note->group_paths = arr;
            out_note->group_path_count = cnt;
        }
    } else {
        const char *path_v = attr_find(attrs, nattrs, "path");
        const char *hash_v = attr_find(attrs, nattrs, "hash");
        const char *line_v = attr_find(attrs, nattrs, "line");
        if (path_v) out_note->path = xstrdup(path_v);
        if (hash_v) {
            uint64_t h;
            if (file_hash_from_hex(hash_v, &h)) { out_note->has_hash = true; out_note->hash = h; }
        }
        if (line_v) { out_note->has_line = true; out_note->line = atoi(line_v); }
    }

    /* Body: everything after the marker line's newline, minus leading/
     * trailing blank lines, up to the block's own end. */
    const char *body_line_end = close + 3;
    while (body_line_end < chunk_end && *body_line_end != '\n') body_line_end++;
    if (body_line_end < chunk_end) body_line_end++;
    const char *body_start = body_line_end;
    while (body_start < chunk_end && (*body_start == '\n' || *body_start == '\r')) body_start++;
    const char *body_end = chunk_end;
    while (body_end > body_start &&
           (body_end[-1] == '\n' || body_end[-1] == '\r' || body_end[-1] == ' ' || body_end[-1] == '\t')) {
        body_end--;
    }
    size_t body_len = (size_t)(body_end - body_start);
    char *body = (char *)malloc(body_len + 1);
    memcpy(body, body_start, body_len);
    body[body_len] = '\0';
    out_note->body = body;

    out_note->_block_start = block_start;
    out_note->_block_end = block_end;
    return true;
}

/* Finds every line that is exactly "---" (surrounding whitespace on that
 * line ignored) and parses the note sandwiched between each consecutive
 * pair. See the file-level comment for why adjacent notes share a
 * boundary separator. */
static void parse_notes_with_spans(const char *raw, size_t raw_len, NoteSet *out) {
    size_t *sep_start = NULL, *sep_end = NULL;
    size_t sep_n = 0, sep_cap = 0;

    size_t i = 0;
    while (i <= raw_len) {
        size_t line_start = i;
        size_t j = i;
        while (j < raw_len && raw[j] != '\n') j++;
        size_t line_end_excl = j;
        size_t line_end_incl = (j < raw_len) ? j + 1 : j;

        size_t ts = line_start, te = line_end_excl;
        while (ts < te && (raw[ts] == ' ' || raw[ts] == '\t' || raw[ts] == '\r')) ts++;
        while (te > ts && (raw[te - 1] == ' ' || raw[te - 1] == '\t' || raw[te - 1] == '\r')) te--;

        if (te - ts == 3 && memcmp(raw + ts, "---", 3) == 0) {
            if (sep_n == sep_cap) {
                sep_cap = sep_cap ? sep_cap * 2 : 8;
                sep_start = (size_t *)realloc(sep_start, sep_cap * sizeof(size_t));
                sep_end = (size_t *)realloc(sep_end, sep_cap * sizeof(size_t));
            }
            sep_start[sep_n] = line_start;
            sep_end[sep_n] = line_end_incl;
            sep_n++;
        }

        if (j >= raw_len) break;
        i = line_end_incl;
    }

    for (size_t k = 0; sep_n > 0 && k + 1 < sep_n; k++) {
        Note n;
        if (parse_block(raw, sep_end[k], sep_start[k + 1], &n)) {
            n._leading_sep_start = sep_start[k];
            *notes_push(out) = n;
        }
    }

    free(sep_start);
    free(sep_end);
}

bool notes_read_md(const char *path, NoteSet *out) {
    notes_init(out);
    char *raw;
    size_t raw_len;
    slurp_file(path, &raw, &raw_len);
    parse_notes_with_spans(raw, raw_len, out);
    free(raw);
    return true;
}

/* -- serializing one note's "core" text: everything between its two
 * bounding separators (leading "\n", heading, marker, body, trailing
 * "\n\n"). append() wraps this with a fresh trailing "---\n"; update()
 * drops it straight in between the two separators that were already
 * there, unchanged. -- */

static void trim_body(const char *body, const char **out_start, size_t *out_len) {
    const char *b = body ? body : "";
    size_t len = strlen(b);
    while (len > 0 && (b[len - 1] == '\n' || b[len - 1] == '\r' || b[len - 1] == ' ' || b[len - 1] == '\t')) len--;
    while (len > 0 && (*b == '\n' || *b == '\r')) { b++; len--; }
    *out_start = b;
    *out_len = len;
}

static char *serialize_file_core(const char *id, const char *path, bool has_hash, uint64_t hash,
                                  bool has_line, int line, const char *created, const char *updated,
                                  const char *body) {
    strbuf sb;
    strbuf_init(&sb);
    if (has_line) strbuf_appendf(&sb, "\n## %s:%d\n\n", path, line);
    else strbuf_appendf(&sb, "\n## %s (whole file)\n\n", path);

    strbuf_appendf(&sb, "<!-- codestellation-note id=%s path=\"%s\"", id, path);
    if (has_hash) {
        char hex[17];
        file_hash_to_hex(hash, hex);
        strbuf_appendf(&sb, " hash=%s", hex);
    }
    if (has_line) strbuf_appendf(&sb, " line=%d", line);
    strbuf_appendf(&sb, " created=%s updated=%s -->\n\n", created, updated);

    const char *b;
    size_t blen;
    trim_body(body, &b, &blen);
    strbuf_append_n(&sb, b, blen);
    strbuf_append(&sb, "\n\n");
    return strbuf_take(&sb);
}

static char *serialize_group_core(const char *id, const char **paths, size_t path_count,
                                   const char *created, const char *updated, const char *body) {
    strbuf sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "\n## Group: %zu files\n\n", path_count);

    strbuf_appendf(&sb, "<!-- codestellation-group id=%s paths=\"", id);
    for (size_t i = 0; i < path_count; i++) {
        if (i > 0) strbuf_append(&sb, "|");
        strbuf_append(&sb, paths[i]);
    }
    strbuf_appendf(&sb, "\" created=%s updated=%s -->\n\n", created, updated);

    const char *b;
    size_t blen;
    trim_body(body, &b, &blen);
    strbuf_append_n(&sb, b, blen);
    strbuf_append(&sb, "\n\n");
    return strbuf_take(&sb);
}

static const char PREAMBLE[] =
    "# Codestellation Notes\n"
    "\n"
    "Notes on `graph.json`. Safe to edit by hand -- codemap-view re-reads this\n"
    "file fresh before every save, so hand edits won't be clobbered.\n"
    "\n"
    "---\n";

static bool ends_with_separator(const char *raw, size_t len) {
    size_t i = len;
    while (i > 0 && (raw[i - 1] == '\n' || raw[i - 1] == '\r')) i--;
    size_t line_end = i;
    while (i > 0 && raw[i - 1] != '\n') i--;
    size_t line_start = i;
    size_t ts = line_start, te = line_end;
    while (ts < te && (raw[ts] == ' ' || raw[ts] == '\t' || raw[ts] == '\r')) ts++;
    while (te > ts && (raw[te - 1] == ' ' || raw[te - 1] == '\t' || raw[te - 1] == '\r')) te--;
    return te - ts == 3 && memcmp(raw + ts, "---", 3) == 0;
}

static void reload(const char *path, NoteSet *existing) {
    notes_free(existing);
    notes_read_md(path, existing);
}

bool notes_append_file_note(const char *path, NoteSet *existing,
                             const char *file_path, bool has_hash, uint64_t file_hash,
                             bool has_line, int line, const char *body) {
    char *raw;
    size_t raw_len;
    slurp_file(path, &raw, &raw_len);

    NoteSet parsed;
    notes_init(&parsed);
    parse_notes_with_spans(raw, raw_len, &parsed);
    char id[16];
    snprintf(id, sizeof(id), "n%d", max_id_number(&parsed) + 1);
    notes_free(&parsed);

    char stamp[21];
    now_iso8601(stamp);

    strbuf out;
    strbuf_init(&out);
    if (raw_len == 0) {
        strbuf_append(&out, PREAMBLE);
    } else {
        strbuf_append_n(&out, raw, raw_len);
        if (!ends_with_separator(raw, raw_len)) strbuf_append(&out, "\n---\n");
    }
    char *core = serialize_file_core(id, file_path, has_hash, file_hash, has_line, line, stamp, stamp, body);
    strbuf_append(&out, core);
    strbuf_append(&out, "---\n");
    free(core);

    bool ok = write_file(path, out.data, out.len);
    strbuf_free(&out);
    free(raw);

    reload(path, existing);
    return ok;
}

bool notes_append_group_note(const char *path, NoteSet *existing,
                              const char **paths, size_t path_count, const char *body) {
    char *raw;
    size_t raw_len;
    slurp_file(path, &raw, &raw_len);

    NoteSet parsed;
    notes_init(&parsed);
    parse_notes_with_spans(raw, raw_len, &parsed);
    char id[16];
    snprintf(id, sizeof(id), "g%d", max_id_number(&parsed) + 1);
    notes_free(&parsed);

    char stamp[21];
    now_iso8601(stamp);

    strbuf out;
    strbuf_init(&out);
    if (raw_len == 0) {
        strbuf_append(&out, PREAMBLE);
    } else {
        strbuf_append_n(&out, raw, raw_len);
        if (!ends_with_separator(raw, raw_len)) strbuf_append(&out, "\n---\n");
    }
    char *core = serialize_group_core(id, paths, path_count, stamp, stamp, body);
    strbuf_append(&out, core);
    strbuf_append(&out, "---\n");
    free(core);

    bool ok = write_file(path, out.data, out.len);
    strbuf_free(&out);
    free(raw);

    reload(path, existing);
    return ok;
}

bool notes_update_note(const char *path, NoteSet *existing, const char *id,
                        bool has_line, int line, const char *body) {
    char *raw;
    size_t raw_len;
    slurp_file(path, &raw, &raw_len);

    NoteSet parsed;
    notes_init(&parsed);
    parse_notes_with_spans(raw, raw_len, &parsed);
    Note *target = find_note_by_id(&parsed, id);
    if (!target) {
        notes_free(&parsed);
        free(raw);
        return false;
    }

    char stamp[21];
    now_iso8601(stamp);
    char *core = target->path
                     ? serialize_file_core(target->id, target->path, target->has_hash, target->hash,
                                            has_line, line, target->created, stamp, body)
                     : serialize_group_core(target->id, (const char **)target->group_paths,
                                             target->group_path_count, target->created, stamp, body);

    strbuf out;
    strbuf_init(&out);
    strbuf_append_n(&out, raw, target->_block_start);
    strbuf_append(&out, core);
    strbuf_append_n(&out, raw + target->_block_end, raw_len - target->_block_end);
    free(core);

    bool ok = write_file(path, out.data, out.len);
    strbuf_free(&out);
    notes_free(&parsed);
    free(raw);

    reload(path, existing);
    return ok;
}

bool notes_delete_note(const char *path, NoteSet *existing, const char *id) {
    char *raw;
    size_t raw_len;
    slurp_file(path, &raw, &raw_len);

    NoteSet parsed;
    notes_init(&parsed);
    parse_notes_with_spans(raw, raw_len, &parsed);
    Note *target = find_note_by_id(&parsed, id);
    if (!target) {
        notes_free(&parsed);
        free(raw);
        return false;
    }

    strbuf out;
    strbuf_init(&out);
    strbuf_append_n(&out, raw, target->_leading_sep_start);
    strbuf_append_n(&out, raw + target->_block_end, raw_len - target->_block_end);

    bool ok = write_file(path, out.data, out.len);
    strbuf_free(&out);
    notes_free(&parsed);
    free(raw);

    reload(path, existing);
    return ok;
}

size_t notes_find_for_path(const NoteSet *ns, const char *file_path, const Note **out, size_t max_out) {
    size_t n = 0;
    for (size_t i = 0; i < ns->count && n < max_out; i++) {
        const Note *note = &ns->notes[i];
        if (note->path && strcmp(note->path, file_path) == 0) {
            out[n++] = note;
            continue;
        }
        for (size_t g = 0; g < note->group_path_count; g++) {
            if (strcmp(note->group_paths[g], file_path) == 0) {
                out[n++] = note;
                break;
            }
        }
    }
    return n;
}

static bool path_sets_equal(char **a, size_t an, const char **b, size_t bn) {
    if (an != bn) return false;
    for (size_t i = 0; i < an; i++) {
        bool found = false;
        for (size_t j = 0; j < bn; j++) {
            if (strcmp(a[i], b[j]) == 0) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

size_t notes_find_for_group(const NoteSet *ns, const char **paths, size_t path_count,
                             const Note **out, size_t max_out) {
    size_t n = 0;
    for (size_t i = 0; i < ns->count && n < max_out; i++) {
        const Note *note = &ns->notes[i];
        if (note->group_path_count > 0 &&
            path_sets_equal(note->group_paths, note->group_path_count, paths, path_count)) {
            out[n++] = note;
        }
    }
    return n;
}
