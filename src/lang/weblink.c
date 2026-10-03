#include "weblink.h"
#include "../common/symtab.h"
#include "../common/pathutil.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* "scheme:" -- letters, then letters/digits/+.-, then ':'. Two or more
 * characters, so a Windows drive ("C:\x") isn't mistaken for one. */
static bool has_scheme(const char *s) {
    size_t i = 0;
    if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'))) return false;
    while (s[i] && s[i] != ':') {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '+' || c == '.' || c == '-';
        if (!ok) return false;
        i++;
    }
    return s[i] == ':' && i >= 2;
}

static bool is_asset(const char *path) {
    static const char *ASSET_EXTS[] = {
        ".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".avif", ".ico", ".bmp",
        ".woff", ".woff2", ".ttf", ".otf", ".eot",
        ".mp4", ".webm", ".ogg", ".mp3", ".wav", ".pdf", NULL };
    const char *ext = path_extension(path);
    for (int i = 0; ASSET_EXTS[i]; i++) {
        const char *a = ASSET_EXTS[i];
        size_t n = strlen(a);
        if (strlen(ext) != n) continue;
        bool match = true;
        for (size_t k = 0; k < n && match; k++) {
            char c = ext[k];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            match = c == a[k];
        }
        if (match) return true;
    }
    return false;
}

char *weblink_clean(const char *text, size_t len) {
    while (len && is_space(*text)) { text++; len--; }
    while (len && is_space(text[len - 1])) len--;
    while (len >= 2 && ((text[0] == '"' && text[len - 1] == '"') ||
                        (text[0] == '\'' && text[len - 1] == '\'') ||
                        (text[0] == '<' && text[len - 1] == '>'))) {
        text++;
        len -= 2;
    }

    char *out = (char *)malloc(len + 1);
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == '?' || c == '#') break;
        if (c == '%' && i + 2 < len && hex_value(text[i + 1]) >= 0 && hex_value(text[i + 2]) >= 0) {
            c = (char)(hex_value(text[i + 1]) * 16 + hex_value(text[i + 2]));
            i += 2;
        }
        out[n++] = c;
    }
    out[n] = '\0';

    if (n == 0 || has_scheme(out) || strncmp(out, "//", 2) == 0 || is_asset(out)) {
        free(out);
        return NULL;
    }
    return out;
}

/* The key for `base`, or `base` + one of the suffixes, if declared. */
static char *try_candidate(const char *base, const char *const *suffixes, SymbolTable *table) {
    if (symtab_get(table, base) >= 0) return xstrdup(base);
    for (int i = 0; suffixes && suffixes[i]; i++) {
        size_t a = strlen(base), b = strlen(suffixes[i]);
        char *cand = (char *)malloc(a + b + 1);
        memcpy(cand, base, a);
        memcpy(cand + a, suffixes[i], b + 1);
        if (symtab_get(table, cand) >= 0) return cand;
        free(cand);
    }
    return NULL;
}

static char *try_in_dir(const char *dir, const char *rel, const char *const *suffixes, SymbolTable *table) {
    char *joined = path_join(dir, rel);
    char *clean = path_clean(joined);
    free(joined);
    char *hit = try_candidate(clean, suffixes, table);
    free(clean);
    return hit;
}

char *weblink_resolve(const char *link, const char *referencing_file_path,
                      const char *const *try_suffixes, void *symbol_table_handle) {
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !link || !link[0]) return NULL;

    char *dir = path_dirname(referencing_file_path);
    if (link[0] != '/') {
        char *hit = try_in_dir(dir, link, try_suffixes, table);
        free(dir);
        return hit;
    }

    /* Site-root relative: as an absolute path first, then against each
     * ancestor of the page, nearest first. */
    char *clean = path_clean(link);
    char *hit = try_candidate(clean, try_suffixes, table);
    free(clean);
    const char *rel = link + 1;
    while (!hit && dir) {
        hit = try_in_dir(dir, rel, try_suffixes, table);
        char *parent = path_dirname(dir);
        if (!parent || strlen(parent) >= strlen(dir)) {
            free(parent);
            break;
        }
        free(dir);
        dir = parent;
    }
    free(dir);
    return hit;
}
