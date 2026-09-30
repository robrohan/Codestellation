#include "winscript.h"
#include "../common/symtab.h"
#include "../common/pathutil.h"
#include <stdlib.h>
#include <string.h>

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (slash && (!bslash || slash > bslash)) return slash + 1;
    return bslash ? bslash + 1 : path;
}

static char *lower_key(const char *prefix, const char *s, size_t n) {
    size_t pl = strlen(prefix);
    char *out = (char *)malloc(pl + n + 1);
    memcpy(out, prefix, pl);
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        out[pl + i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    out[pl + n] = '\0';
    return out;
}

bool winscript_has_ext(const char *name, const char *const *exts) {
    size_t n = strlen(name);
    for (int i = 0; exts[i]; i++) {
        size_t m = strlen(exts[i]);
        if (n < m) continue;
        bool match = true;
        for (size_t k = 0; k < m && match; k++) {
            char a = name[n - m + k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            match = a == exts[i][k];
        }
        if (match) return true;
    }
    return false;
}

static void declare(DeclSinkFn sink, void *ctx, const char *key) {
    DeclFact fact;
    fact.kind = DECL_PACKAGE;
    fact.qualified_name = key;
    sink(ctx, &fact);
}

void winscript_declare(const ParsedFile *file, DeclSinkFn sink, void *ctx) {
    declare(sink, ctx, file->path);
    const char *base = base_name(file->path);
    char *k = lower_key("winscript:", base, strlen(base));
    declare(sink, ctx, k);
    free(k);

    static const char *MODULE_EXTS[] = { ".psm1", ".psd1", NULL };
    if (winscript_has_ext(base, MODULE_EXTS)) {
        const char *dot = strrchr(base, '.');
        k = lower_key("psmod:", base, (size_t)(dot - base));
        declare(sink, ctx, k);
        free(k);
    }
}

char *winscript_clean_path(const char *text, size_t len) {
    /* Quotes: "..." or '...'. */
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) len--;
    while (len > 0 && (*text == ' ' || *text == '\t')) {
        text++;
        len--;
    }
    if (len >= 2 && (text[0] == '"' || text[0] == '\'') && text[len - 1] == text[0]) {
        text++;
        len -= 2;
    }
    if (len == 0) return NULL;

    size_t skip = 0;
    if (len >= 5 && strncmp(text, "%~dp0", 5) == 0) {
        /* %~dp0 already ends in a backslash: "%~dp0lib\x.bat". */
        skip = 5;
    } else if (text[0] == '%' || text[0] == '$') {
        /* $PSScriptRoot\..., ${Root}/..., %ROOT%\... -- the variable
         * stands for "this script's directory" when a separator follows. */
        size_t i = 1;
        if (text[0] == '%') {
            while (i < len && text[i] != '%') i++;
            i++;
        } else if (i < len && text[i] == '{') {
            while (i < len && text[i] != '}') i++;
            i++;
        } else {
            while (i < len && (text[i] == '_' || text[i] == ':' || (text[i] >= 'a' && text[i] <= 'z') ||
                               (text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= '0' && text[i] <= '9'))) {
                i++;
            }
        }
        if (i >= len || (text[i] != '\\' && text[i] != '/')) return NULL;
        skip = i;
    }
    /* Anything still containing an expansion is too dynamic to follow. */
    for (size_t i = skip; i < len; i++) {
        if (text[i] == '$' || text[i] == '%' || text[i] == '(') return NULL;
    }

    size_t rest = len - skip;
    char *out = (char *)malloc(rest + 3);
    size_t o = 0;
    if (skip) {
        out[o++] = '.';
        if (text[skip] != '\\' && text[skip] != '/') out[o++] = '/';
    }
    memcpy(out + o, text + skip, rest);
    out[o + rest] = '\0';
    return out;
}

/* base + suffix if it's a declared key. */
static char *try_key(SymbolTable *table, const char *base, const char *suffix) {
    size_t bl = strlen(base), sl = strlen(suffix);
    char *cand = (char *)malloc(bl + sl + 1);
    memcpy(cand, base, bl);
    memcpy(cand + bl, suffix, sl + 1);
    if (symtab_get(table, cand) >= 0) return cand;
    free(cand);
    return NULL;
}

char *winscript_resolve(const char *raw_text, const char *referencing_file_path, void *symbol_table_handle) {
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text || !raw_text[0]) return NULL;

    bool absolute = raw_text[0] == '/' || raw_text[0] == '\\' ||
                    (((raw_text[0] | 0x20) >= 'a' && (raw_text[0] | 0x20) <= 'z') && raw_text[1] == ':');
    char *joined;
    if (absolute) {
        joined = xstrdup(raw_text);
    } else {
        char *dir = path_dirname(referencing_file_path);
        joined = path_join(dir, raw_text);
        free(dir);
    }
    char *path = path_clean(joined);
    free(joined);

    static const char *EXTS[] = { "", ".ps1", ".psm1", ".psd1", ".bat", ".cmd" };
    char *hit = NULL;
    for (size_t i = 0; !hit && i < sizeof(EXTS) / sizeof(EXTS[0]); i++) hit = try_key(table, path, EXTS[i]);

    /* A module folder: Import-Module .\modules\Tools -> Tools\Tools.psd1. */
    if (!hit) {
        const char *name = base_name(path);
        char sep = strchr(path, '\\') ? '\\' : '/';
        size_t pl = strlen(path), nl = strlen(name);
        char *inner = (char *)malloc(pl + nl + 2);
        memcpy(inner, path, pl);
        inner[pl] = sep;
        memcpy(inner + pl + 1, name, nl + 1);
        hit = try_key(table, inner, ".psd1");
        if (!hit) hit = try_key(table, inner, ".psm1");
        free(inner);
    }
    free(path);
    if (hit) return hit;

    /* By file name (case-insensitive), with or without an extension. */
    const char *base = base_name(raw_text);
    char *k = lower_key("winscript:", base, strlen(base));
    for (size_t i = 0; !hit && i < sizeof(EXTS) / sizeof(EXTS[0]); i++) hit = try_key(table, k, EXTS[i]);
    free(k);
    if (hit) return hit;

    /* By module name: Import-Module Deploy. */
    const char *dot = strrchr(base, '.');
    static const char *MODULE_EXTS[] = { ".psm1", ".psd1", NULL };
    size_t stem = dot && winscript_has_ext(base, MODULE_EXTS) ? (size_t)(dot - base) : strlen(base);
    k = lower_key("psmod:", base, stem);
    if (symtab_get(table, k) >= 0) return k;
    free(k);
    return NULL;
}
