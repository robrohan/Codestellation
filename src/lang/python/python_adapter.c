/* Python dependency adapter.
 *
 * Python's dependency signal is `import` / `from ... import ...`, which
 * name *modules* by dotted path (`pkg.sub.mod`) rather than files. There's
 * no in-source declaration of "I am module pkg.sub.mod" -- that identity
 * comes entirely from where the file sits on disk relative to some
 * (unknown to this adapter) source root. So, like the Lisp adapter, this
 * is a deliberate best-guess stand-in, not a full import resolver:
 *
 *   - Declarations: for a file at .../a/b/c.py this emits every trailing
 *     dotted suffix of its path -- `c`, `b.c`, `a.b.c`, ... (capped at
 *     MODULE_SUFFIX_MAX segments) -- plus its own absolute path. An
 *     `__init__.py` contributes suffixes of its *directory* instead
 *     (`pkg`, `parent.pkg`, ...), matching how a package is imported by
 *     its folder name. No `__init__.py` / PEP-420 check is done: any
 *     directory level can act as a package here. Two files whose paths
 *     share a trailing suffix (`foo/util.py` and `bar/util.py` both
 *     emitting `util`) collide in the symbol table -- last one wins. A
 *     known, accepted limitation, same as the Lisp adapter's package
 *     collisions.
 *
 *   - References:
 *       `import a.b.c`              -> ref "a.b.c"
 *       `import a.b.c as x`         -> ref "a.b.c"
 *       `from a.b import c, d`      -> refs "a.b", "a.b.c", "a.b.d"
 *                                     (c/d may be submodules or just names;
 *                                     the ones that aren't modules simply
 *                                     don't resolve and are counted
 *                                     unresolved, which is harmless)
 *       `from a.b import *`         -> ref "a.b"
 *       `from . import x`           -> ref ".x"      (relative)
 *       `from ..pkg import y`       -> refs "..pkg", "..pkg.y"
 *     `from __future__ import ...` is skipped.
 *
 *   - Resolution: a dotted ref is matched straight against the symbol
 *     table (i.e. against some other file's path-derived suffix). A
 *     relative ref (leading dots) is resolved the way the C adapter
 *     resolves `#include "..."`: walk up from the referencing file's
 *     directory one level per leading dot beyond the first, append the
 *     remaining segments, and look for `<that>.py` or
 *     `<that>/__init__.py` on disk.
 *
 * Node shapes were confirmed against tree-sitter-python v0.23.6's
 * src/node-types.json, not guessed:
 *   - import_statement: field "name" (multiple) -> dotted_name | aliased_import
 *   - import_from_statement: field "module_name" -> dotted_name | relative_import;
 *     field "name" (multiple) -> dotted_name | aliased_import; an unnamed
 *     wildcard_import child for `import *`.
 *   - aliased_import: field "name" -> dotted_name, field "alias" -> identifier
 *   - relative_import: import_prefix (the dots) + optional dotted_name;
 *     its own source text is the whole `..pkg` string.
 *   - dotted_name: identifier children joined by '.'.
 */

#include "python_adapter.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_python(void);

static const char *PYTHON_EXTENSIONS[] = { ".py", ".pyi", NULL };

/* How many trailing path segments a module suffix is allowed to span --
 * enough for realistically nested packages, bounded so a deep checkout
 * path doesn't emit dozens of pointless decls. */
#define MODULE_SUFFIX_MAX 8

static char *strndup_local(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *node_text(const ParsedFile *file, TSNode node) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    return strndup_local(file->source + start, end - start);
}

/* ---- declarations ---- */

/* Split path on '/' or '\\' into segment [start,len) spans (empty
 * segments, e.g. from a leading '/', are dropped). Returns the count and
 * fills seg_start/seg_len (caller-sized to cap). */
static int path_segments(const char *path, const char **seg_start, size_t *seg_len, int cap) {
    int count = 0;
    const char *p = path;
    while (*p && count < cap) {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/' && *p != '\\') p++;
        seg_start[count] = s;
        seg_len[count] = (size_t)(p - s);
        count++;
    }
    return count;
}

static void python_extract_declarations(const ParsedFile *file, TSQuery *decl_query,
                                         DeclSinkFn sink, void *ctx) {
    (void)decl_query;

    /* The file's own path -- lets relative-import resolution (which
     * produces a path, not a dotted name) find it. */
    DeclFact path_fact;
    path_fact.kind = DECL_PACKAGE;
    path_fact.qualified_name = file->path;
    sink(ctx, &path_fact);

    const char *seg_start[64];
    size_t seg_len[64];
    int n = path_segments(file->path, seg_start, seg_len, 64);
    if (n == 0) return;

    /* Trim ".py" / ".pyi" off the last segment. */
    size_t last = seg_len[n - 1];
    const char *dot = NULL;
    for (size_t i = last; i > 0; i--) {
        if (seg_start[n - 1][i - 1] == '.') { dot = seg_start[n - 1] + i - 1; break; }
    }
    if (dot) seg_len[n - 1] = (size_t)(dot - seg_start[n - 1]);

    /* `__init__.py` names its directory, not a module of its own. */
    if (seg_len[n - 1] == 8 && strncmp(seg_start[n - 1], "__init__", 8) == 0) {
        n--;
        if (n == 0) return;
    }

    /* Emit cumulative trailing suffixes, innermost first:
     * "mod", "pkg.mod", "parent.pkg.mod", ... */
    char acc[1024];
    size_t acc_len = 0;
    acc[0] = '\0';
    int lo = n - MODULE_SUFFIX_MAX;
    if (lo < 0) lo = 0;
    for (int i = n - 1; i >= lo; i--) {
        size_t sl = seg_len[i];
        size_t need = sl + (acc_len ? 1 : 0);
        if (acc_len + need + 1 > sizeof(acc)) break;
        memmove(acc + sl + (acc_len ? 1 : 0), acc, acc_len + 1);
        memcpy(acc, seg_start[i], sl);
        if (acc_len) acc[sl] = '.';
        acc_len += need;

        DeclFact fact;
        fact.kind = DECL_PACKAGE;
        fact.qualified_name = acc;
        sink(ctx, &fact);
    }
}

/* ---- references ---- */

static void emit_ref(RefSinkFn sink, void *ctx, const char *raw) {
    RefFact fact;
    fact.kind = REF_IMPORT_PATH;
    fact.raw_text = raw;
    fact.scope_namespace = NULL;
    sink(ctx, &fact);
}

/* "a.b" + "c" -> "a.b.c"; ".", ".." etc. already end in '.', so
 * "." + "x" -> ".x". Caller frees. */
static char *join_module(const char *mod, const char *name) {
    size_t ml = strlen(mod), nl = strlen(name);
    int need_dot = ml > 0 && mod[ml - 1] != '.';
    char *out = (char *)malloc(ml + (need_dot ? 1 : 0) + nl + 1);
    memcpy(out, mod, ml);
    size_t o = ml;
    if (need_dot) out[o++] = '.';
    memcpy(out + o, name, nl);
    out[o + nl] = '\0';
    return out;
}

/* The real module a `name`-field child names: a bare dotted_name is
 * itself; an aliased_import's module is its "name" field. Caller frees. */
static char *import_target_text(const ParsedFile *file, TSNode child) {
    if (strcmp(ts_node_type(child), "aliased_import") == 0) {
        TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
        if (!ts_node_is_null(name_node)) return node_text(file, name_node);
        return NULL;
    }
    return node_text(file, child);
}

static void collect_refs(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);

    if (strcmp(type, "import_statement") == 0) {
        uint32_t n = ts_node_child_count(node);
        for (uint32_t i = 0; i < n; i++) {
            const char *field = ts_node_field_name_for_child(node, i);
            if (!field || strcmp(field, "name") != 0) continue;
            char *t = import_target_text(file, ts_node_child(node, i));
            if (t) { emit_ref(sink, ctx, t); free(t); }
        }
        return;
    }

    if (strcmp(type, "import_from_statement") == 0) {
        TSNode mod = ts_node_child_by_field_name(node, "module_name", 11);
        char *modtext = ts_node_is_null(mod) ? NULL : node_text(file, mod);
        if (modtext && modtext[0] != '\0') emit_ref(sink, ctx, modtext);

        if (modtext) {
            uint32_t n = ts_node_child_count(node);
            for (uint32_t i = 0; i < n; i++) {
                const char *field = ts_node_field_name_for_child(node, i);
                if (!field || strcmp(field, "name") != 0) continue;
                char *nm = import_target_text(file, ts_node_child(node, i));
                if (nm) {
                    char *joined = join_module(modtext, nm);
                    emit_ref(sink, ctx, joined);
                    free(joined);
                    free(nm);
                }
            }
        }
        free(modtext);
        return;
    }

    /* `from __future__ import ...` is future_import_statement -- a
     * distinct node type, so it's ignored by falling through here. */

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        collect_refs(ts_node_named_child(node, i), file, sink, ctx);
    }
}

static void python_extract_references(const ParsedFile *file, TSQuery *ref_query,
                                       RefSinkFn sink, void *ctx) {
    (void)ref_query;
    collect_refs(ts_tree_root_node(file->tree), file, sink, ctx);
}

/* ---- resolution ---- */

static char *resolve_relative(const char *raw, const char *referencing_file_path,
                               SymbolTable *table) {
    int dots = 0;
    while (raw[dots] == '.') dots++;
    const char *rest = raw + dots; /* dotted remainder, possibly "" */

    /* One dot == the file's own package (its directory); each extra dot
     * climbs one more level. */
    char *dir = path_dirname(referencing_file_path);
    for (int i = 1; i < dots && dir; i++) {
        char *up = path_dirname(dir);
        free(dir);
        dir = up;
    }
    if (!dir) return NULL;

    /* Append the remaining dotted segments as path components. */
    char *base = dir;
    char *seg = (char *)malloc(strlen(rest) + 1);
    size_t si = 0;
    for (const char *p = rest;; p++) {
        if (*p == '.' || *p == '\0') {
            if (si > 0) {
                seg[si] = '\0';
                char *joined = path_join(base, seg);
                free(base);
                base = joined;
                si = 0;
            }
            if (*p == '\0') break;
        } else {
            seg[si++] = *p;
        }
    }
    free(seg);

    const char *suffixes[] = { ".py", ".pyi", "/__init__.py", "/__init__.pyi" };
    char *result = NULL;
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        char *cand = (char *)malloc(strlen(base) + strlen(suffixes[i]) + 1);
        strcpy(cand, base);
        strcat(cand, suffixes[i]);
        char *norm = path_normalize(cand);
        free(cand);
        if (norm) {
            if (symtab_get(table, norm) >= 0) { result = norm; break; }
            free(norm);
        }
    }
    free(base);
    return result;
}

static char *python_resolve_reference(const char *raw_text,
                                       const char *referencing_file_path,
                                       const char *scope_namespace,
                                       const char **imported_namespaces,
                                       int imported_count,
                                       void *symbol_table_handle) {
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text || !raw_text[0]) return NULL;

    if (raw_text[0] == '.') {
        return resolve_relative(raw_text, referencing_file_path, table);
    }
    if (symtab_get(table, raw_text) >= 0) return xstrdup(raw_text);
    return NULL;
}

static LanguageAdapter g_python_adapter;
static int g_initialized = 0;

const LanguageAdapter *python_adapter_get(void) {
    if (!g_initialized) {
        g_python_adapter.name = "python";
        g_python_adapter.extensions = PYTHON_EXTENSIONS;
        g_python_adapter.extension_count = 2;
        g_python_adapter.ts_language = tree_sitter_python;
        g_python_adapter.decl_query = NULL;
        g_python_adapter.ref_query = NULL;
        g_python_adapter.extract_declarations = python_extract_declarations;
        g_python_adapter.extract_references = python_extract_references;
        g_python_adapter.resolve_reference = python_resolve_reference;
        g_initialized = 1;
    }
    return &g_python_adapter;
}
