/* JavaScript / TypeScript / TSX dependency adapters.
 *
 * Three adapters (one per grammar -- tree-sitter-javascript covers JSX,
 * tree-sitter-typescript ships separate typescript and tsx grammars) that
 * share all of their logic: the three grammars use the same node shapes
 * for everything this looks at.
 *
 *   - Declarations: each file declares its own absolute path. Module
 *     identity in JS/TS is a file path, so that's all a relative import
 *     needs to resolve to.
 *
 *   - References (the module specifier string of each):
 *       import x from "./a"        import_statement      source: string
 *       import "./side-effect"     import_statement      source: string
 *       export { y } from "./b"    export_statement      source: string
 *       export * from "./c"        export_statement      source: string
 *       import fs = require("./d") import_require_clause source: string   (TS)
 *       require("./e")             call_expression  function: identifier "require"
 *       import("./f")              call_expression  function: (import)
 *     Only a literal string argument counts; template strings and
 *     computed specifiers are skipped.
 *
 *   - Resolution: relative ("./", "../") and absolute specifiers are joined
 *     to the importing file's directory and cleaned lexically, then tried
 *     the way Node/TypeScript/bundlers do: as written; with each source
 *     extension appended; TypeScript's ESM convention of importing
 *     "./x.js" for a file that's really x.ts; and as a directory with an
 *     index file. A resolved .json import links to the JSON adapter's node
 *     for that file (both declare plain absolute paths).
 *
 *     Bare specifiers go through the nearest tsconfig.json / jsconfig.json
 *     (walking up from the importing file) first, since most real TS
 *     projects use them for local modules:
 *       "paths" aliases, e.g. @components/<name> mapped to
 *         src/components/<name> -- longest matching prefix wins, as in
 *         TypeScript;
 *       "baseUrl": "src"  -- `import x from "interfaces"` is src/interfaces.
 *     Configs are parsed leniently (comments, trailing commas) and follow
 *     relative "extends" chains, inheriting baseUrl/paths the way tsc
 *     does. Anything still unmatched is a package ("react", "@scope/pkg")
 *     -- unresolved, like the C adapter's system includes.
 *
 * Node shapes were confirmed by parsing samples with the pinned grammars
 * (javascript v0.25.0, typescript/tsx v0.23.2), not guessed.
 */

#include "js_adapter.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_javascript(void);
extern const TSLanguage *tree_sitter_typescript(void);
extern const TSLanguage *tree_sitter_tsx(void);

static const char *JS_EXTENSIONS[] = { ".js", ".mjs", ".cjs", ".jsx", NULL };
static const char *TS_EXTENSIONS[] = { ".ts", ".mts", ".cts", NULL };
static const char *TSX_EXTENSIONS[] = { ".tsx", NULL };

/* ---- declarations ---- */

static void js_extract_declarations(const ParsedFile *file, TSQuery *decl_query, DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    DeclFact fact;
    fact.kind = DECL_PACKAGE;
    fact.qualified_name = file->path;
    sink(ctx, &fact);
}

/* ---- references ---- */

/* A string node's contents (its string_fragment child), or NULL for an
 * empty string / anything that isn't a plain string literal. Caller frees. */
static char *string_contents(const ParsedFile *file, TSNode str) {
    if (ts_node_is_null(str) || strcmp(ts_node_type(str), "string") != 0) return NULL;
    uint32_t n = ts_node_named_child_count(str);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_named_child(str, i);
        if (strcmp(ts_node_type(c), "string_fragment") == 0) {
            uint32_t s = ts_node_start_byte(c), e = ts_node_end_byte(c);
            char *out = (char *)malloc(e - s + 1);
            memcpy(out, file->source + s, e - s);
            out[e - s] = '\0';
            return out;
        }
    }
    return NULL;
}

static void emit(RefSinkFn sink, void *ctx, char *spec, RefKind kind) {
    if (!spec) return;
    RefFact fact;
    fact.kind = kind;
    fact.raw_text = spec;
    fact.scope_namespace = NULL;
    sink(ctx, &fact);
    free(spec);
}

static void collect_refs(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);

    if (strcmp(type, "import_statement") == 0 || strcmp(type, "export_statement") == 0 ||
        strcmp(type, "import_require_clause") == 0) {
        TSNode src = ts_node_child_by_field_name(node, "source", 6);
        emit(sink, ctx, string_contents(file, src), REF_IMPORT_PATH);
        /* An import_statement can hold an import_require_clause; keep
         * walking so it's found too. */
    } else if (strcmp(type, "call_expression") == 0) {
        TSNode fn = ts_node_child_by_field_name(node, "function", 8);
        bool is_require = false, is_import = false;
        if (!ts_node_is_null(fn)) {
            const char *ft = ts_node_type(fn);
            if (strcmp(ft, "import") == 0) {
                is_import = true;
            } else if (strcmp(ft, "identifier") == 0) {
                uint32_t s = ts_node_start_byte(fn), e = ts_node_end_byte(fn);
                is_require = e - s == 7 && strncmp(file->source + s, "require", 7) == 0;
            }
        }
        if (is_require || is_import) {
            TSNode args = ts_node_child_by_field_name(node, "arguments", 9);
            if (!ts_node_is_null(args) && ts_node_named_child_count(args) > 0) {
                emit(sink, ctx, string_contents(file, ts_node_named_child(args, 0)),
                     is_require ? REF_MODULE_REQUIRE : REF_IMPORT_PATH);
            }
        }
    }

    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, sink, ctx);
}

static void js_extract_references(const ParsedFile *file, TSQuery *ref_query, RefSinkFn sink, void *ctx) {
    (void)ref_query;
    collect_refs(ts_tree_root_node(file->tree), file, sink, ctx);
}

/* ---- resolution ---- */

static bool ends_with(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

/* base + suffix, returned (malloc'd) if it's a known file, else NULL. */
static char *try_path(SymbolTable *table, const char *base, const char *suffix) {
    size_t bl = strlen(base), sl = strlen(suffix);
    char *cand = (char *)malloc(bl + sl + 1);
    memcpy(cand, base, bl);
    memcpy(cand + bl, suffix, sl + 1);
    if (symtab_get(table, cand) >= 0) return cand;
    free(cand);
    return NULL;
}

/* The file an import of `joined` (an absolute path, not yet cleaned)
 * lands on, trying extensions and index files like Node/tsc/bundlers.
 * malloc'd; NULL if none. */
static char *try_candidates(SymbolTable *table, const char *joined) {
    char *base = path_clean(joined);
    static const char *EXT[] = { ".ts", ".tsx", ".d.ts", ".js", ".jsx", ".mjs", ".cjs", ".mts", ".cts", ".json" };
    static const char *INDEX[] = { "/index.ts", "/index.tsx", "/index.d.ts", "/index.js", "/index.jsx", "/index.mjs" };
    char *hit = try_path(table, base, "");
    for (size_t i = 0; !hit && i < sizeof(EXT) / sizeof(EXT[0]); i++) hit = try_path(table, base, EXT[i]);

    /* TypeScript ESM: `import "./x.js"` names the compiled output of x.ts. */
    if (!hit) {
        static const char *JS_TO_TS[][2] = {
            { ".js", ".ts" }, { ".js", ".tsx" }, { ".jsx", ".tsx" }, { ".mjs", ".mts" }, { ".cjs", ".cts" },
        };
        for (size_t i = 0; !hit && i < sizeof(JS_TO_TS) / sizeof(JS_TO_TS[0]); i++) {
            if (!ends_with(base, JS_TO_TS[i][0])) continue;
            size_t stem = strlen(base) - strlen(JS_TO_TS[i][0]);
            char *stripped = (char *)malloc(stem + 1);
            memcpy(stripped, base, stem);
            stripped[stem] = '\0';
            hit = try_path(table, stripped, JS_TO_TS[i][1]);
            free(stripped);
        }
    }
    for (size_t i = 0; !hit && i < sizeof(INDEX) / sizeof(INDEX[0]); i++) hit = try_path(table, base, INDEX[i]);
    free(base);
    return hit;
}

/* ---- tsconfig.json / jsconfig.json: baseUrl and paths ---- */

typedef struct {
    char *prefix, *suffix;  /* the key split at its '*' (suffix usually "") */
    bool wildcard;          /* the key had a '*' */
    char **targets;         /* as written, each with at most one '*' */
    size_t target_count;
} PathAlias;

typedef struct {
    char *base_url;         /* absolute, or NULL */
    char *paths_base;       /* what "paths" targets are relative to */
    PathAlias *aliases;
    size_t alias_count;
} TsConfig;

/* Loaded configs, and a directory -> config memo (value = index + 1, or 0
 * for "no config above here"). Both live for one build: js_adapter_reset
 * clears them, so edits to a tsconfig show up on the next build. */
static TsConfig *g_configs = NULL;
static size_t g_config_count = 0, g_config_cap = 0;
static SymbolTable *g_dir_memo = NULL;

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    *out_len = fread(buf, 1, (size_t)size, f);
    buf[*out_len] = '\0';
    fclose(f);
    return buf;
}

static struct json_value_s *obj_get(struct json_object_s *obj, const char *key) {
    if (!obj) return NULL;
    size_t key_len = strlen(key);
    for (struct json_object_element_s *e = obj->start; e; e = e->next) {
        if (e->name->string_size == key_len && memcmp(e->name->string, key, key_len) == 0) return e->value;
    }
    return NULL;
}

static char *json_str(struct json_value_s *v) {
    struct json_string_s *s = v ? json_value_as_string(v) : NULL;
    if (!s) return NULL;
    char *out = (char *)malloc(s->string_size + 1);
    memcpy(out, s->string, s->string_size);
    out[s->string_size] = '\0';
    return out;
}

static char *join_clean(const char *dir, const char *rel) {
    char *j = rel[0] == '/' ? xstrdup(rel) : path_join(dir, rel);
    char *c = path_clean(j);
    free(j);
    return c;
}

static void free_config_parts(TsConfig *c) {
    for (size_t i = 0; i < c->alias_count; i++) {
        free(c->aliases[i].prefix);
        free(c->aliases[i].suffix);
        for (size_t t = 0; t < c->aliases[i].target_count; t++) free(c->aliases[i].targets[t]);
        free(c->aliases[i].targets);
    }
    free(c->aliases);
    free(c->base_url);
    free(c->paths_base);
    memset(c, 0, sizeof(*c));
}

static void parse_paths(struct json_object_s *paths, TsConfig *out) {
    size_t n = 0;
    for (struct json_object_element_s *e = paths->start; e; e = e->next) n++;
    out->aliases = (PathAlias *)calloc(n ? n : 1, sizeof(PathAlias));
    for (struct json_object_element_s *e = paths->start; e; e = e->next) {
        struct json_array_s *arr = json_value_as_array(e->value);
        if (!arr) continue;
        PathAlias *a = &out->aliases[out->alias_count++];
        const char *key = e->name->string;
        size_t klen = e->name->string_size;
        const char *star = memchr(key, '*', klen);
        a->wildcard = star != NULL;
        size_t plen = star ? (size_t)(star - key) : klen;
        a->prefix = (char *)malloc(plen + 1);
        memcpy(a->prefix, key, plen);
        a->prefix[plen] = '\0';
        size_t slen = star ? klen - plen - 1 : 0;
        a->suffix = (char *)malloc(slen + 1);
        if (slen) memcpy(a->suffix, star + 1, slen);
        a->suffix[slen] = '\0';
        a->targets = (char **)calloc(arr->length ? arr->length : 1, sizeof(char *));
        for (struct json_array_element_s *t = arr->start; t; t = t->next) {
            char *target = json_str(t->value);
            if (target) a->targets[a->target_count++] = target;
        }
    }
}

/* Reads one config into *out, following a relative "extends" (depth-capped)
 * for whatever this file doesn't set itself. false if unreadable. */
static bool load_config(const char *path, TsConfig *out, int depth) {
    memset(out, 0, sizeof(*out));
    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf) return false;
    struct json_value_s *root = json_parse_ex(buf, len,
                                              json_parse_flags_allow_trailing_comma |
                                                  json_parse_flags_allow_c_style_comments,
                                              NULL, NULL, NULL);
    struct json_object_s *obj = root ? json_value_as_object(root) : NULL;
    if (!obj) {
        free(root);
        free(buf);
        return false;
    }
    char *dir = path_dirname(path);

    TsConfig parent;
    memset(&parent, 0, sizeof(parent));
    char *ext = json_str(obj_get(obj, "extends"));
    if (ext && ext[0] == '.' && depth < 5) {
        char *pp = join_clean(dir, ext);
        if (!ends_with(pp, ".json")) {
            char *withext = (char *)malloc(strlen(pp) + 6);
            strcpy(withext, pp);
            strcat(withext, ".json");
            free(pp);
            pp = withext;
        }
        load_config(pp, &parent, depth + 1);
        free(pp);
    }
    free(ext);

    struct json_value_s *opts_v = obj_get(obj, "compilerOptions");
    struct json_object_s *opts = opts_v ? json_value_as_object(opts_v) : NULL;
    char *base_url = json_str(obj_get(opts, "baseUrl"));
    struct json_value_s *paths_v = obj_get(opts, "paths");
    struct json_object_s *paths = paths_v ? json_value_as_object(paths_v) : NULL;

    /* baseUrl: this file's (relative to this file) or the inherited one. */
    if (base_url) {
        out->base_url = join_clean(dir, base_url);
        free(base_url);
    } else if (parent.base_url) {
        out->base_url = xstrdup(parent.base_url);
    }
    if (paths) {
        parse_paths(paths, out);
        out->paths_base = xstrdup(out->base_url ? out->base_url : dir);
    } else if (parent.alias_count) {
        /* Inherit the parent's aliases wholesale (already absolute). */
        out->aliases = parent.aliases;
        out->alias_count = parent.alias_count;
        out->paths_base = parent.paths_base;
        parent.aliases = NULL;
        parent.alias_count = 0;
        parent.paths_base = NULL;
    }
    free_config_parts(&parent);
    free(dir);
    free(root);
    free(buf);
    return true;
}

/* The config governing files in `dir`, or NULL. Memoized per directory. */
static TsConfig *config_for_dir(const char *dir) {
    if (!g_dir_memo) g_dir_memo = symtab_create(64);
    int memo = symtab_get(g_dir_memo, dir);
    if (memo >= 0) return memo ? &g_configs[memo - 1] : NULL;

    TsConfig *found = NULL;
    static const char *NAMES[] = { "tsconfig.json", "jsconfig.json" };
    for (size_t i = 0; i < 2 && !found; i++) {
        char *cand = path_join(dir, NAMES[i]);
        if (path_exists(cand)) {
            TsConfig cfg;
            if (load_config(cand, &cfg, 0)) {
                if (g_config_count == g_config_cap) {
                    g_config_cap = g_config_cap ? g_config_cap * 2 : 4;
                    g_configs = (TsConfig *)realloc(g_configs, g_config_cap * sizeof(TsConfig));
                }
                g_configs[g_config_count++] = cfg;
                found = &g_configs[g_config_count - 1];
            }
        }
        free(cand);
    }
    if (!found) {
        char *parent = path_dirname(dir);
        if (strcmp(parent, dir) != 0) found = config_for_dir(parent);
        free(parent);
    }
    /* g_configs may have moved (realloc) while recursing; memo by index. */
    symtab_put(g_dir_memo, dir, found ? (int)(found - g_configs) + 1 : 0);
    return found;
}

static char *resolve_bare(SymbolTable *table, const char *spec, const char *referencing_file_path) {
    char *dir = path_dirname(referencing_file_path);
    TsConfig *cfg = config_for_dir(dir);
    free(dir);
    if (!cfg) return NULL;

    /* paths: the alias with the longest matching prefix, like tsc. */
    const PathAlias *best = NULL;
    size_t spec_len = strlen(spec);
    for (size_t i = 0; i < cfg->alias_count; i++) {
        const PathAlias *a = &cfg->aliases[i];
        size_t pl = strlen(a->prefix), sl = strlen(a->suffix);
        bool match = a->wildcard ? (spec_len >= pl + sl && strncmp(spec, a->prefix, pl) == 0 &&
                                    strcmp(spec + spec_len - sl, a->suffix) == 0)
                                 : strcmp(spec, a->prefix) == 0;
        if (match && (!best || pl > strlen(best->prefix))) best = a;
    }
    if (best) {
        size_t pl = strlen(best->prefix), sl = strlen(best->suffix);
        size_t star_len = best->wildcard ? spec_len - pl - sl : 0;
        for (size_t t = 0; t < best->target_count; t++) {
            /* Substitute the matched part for the target's '*'. */
            const char *target = best->targets[t];
            const char *star = strchr(target, '*');
            size_t tl = strlen(target);
            char *sub = (char *)malloc(tl + star_len + 1);
            if (star && best->wildcard) {
                size_t before = (size_t)(star - target);
                memcpy(sub, target, before);
                memcpy(sub + before, spec + pl, star_len);
                strcpy(sub + before + star_len, star + 1);
            } else {
                strcpy(sub, target);
            }
            char *joined = sub[0] == '/' ? xstrdup(sub) : path_join(cfg->paths_base, sub);
            free(sub);
            char *hit = try_candidates(table, joined);
            free(joined);
            if (hit) return hit;
        }
    }

    /* baseUrl: bare names are relative to it. */
    if (cfg->base_url) {
        char *joined = path_join(cfg->base_url, spec);
        char *hit = try_candidates(table, joined);
        free(joined);
        if (hit) return hit;
    }
    return NULL;
}

void js_adapter_reset(void) {
    for (size_t i = 0; i < g_config_count; i++) free_config_parts(&g_configs[i]);
    free(g_configs);
    g_configs = NULL;
    g_config_count = g_config_cap = 0;
    if (g_dir_memo) symtab_destroy(g_dir_memo);
    g_dir_memo = NULL;
}

static char *js_resolve_reference(const char *raw_text, const char *referencing_file_path,
                                  const char *scope_namespace, const char **imported_namespaces,
                                  int imported_count, void *symbol_table_handle) {
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text || !raw_text[0]) return NULL;

    /* Drop a query/hash some bundlers allow ("./a.css?inline"). */
    char *spec = xstrdup(raw_text);
    spec[strcspn(spec, "?#")] = '\0';

    char *hit = NULL;
    if (spec[0] == '.' || spec[0] == '/') {
        char *joined;
        if (spec[0] == '/') {
            joined = xstrdup(spec);
        } else {
            char *dir = path_dirname(referencing_file_path);
            joined = path_join(dir, spec);
            free(dir);
        }
        hit = try_candidates(table, joined);
        free(joined);
    } else if (spec[0]) {
        hit = resolve_bare(table, spec, referencing_file_path);
    }
    free(spec);
    return hit;
}

/* ---- adapters ---- */

static LanguageAdapter g_js, g_ts, g_tsx;

static const LanguageAdapter *init(LanguageAdapter *a, const char *name, const char **exts,
                                   const TSLanguage *(*lang)(void)) {
    if (!a->name) {
        a->name = name;
        a->extensions = exts;
        int n = 0;
        while (exts[n]) n++;
        a->extension_count = n;
        a->ts_language = lang;
        a->decl_query = NULL;
        a->ref_query = NULL;
        a->extract_declarations = js_extract_declarations;
        a->extract_references = js_extract_references;
        a->resolve_reference = js_resolve_reference;
    }
    return a;
}

const LanguageAdapter *javascript_adapter_get(void) {
    return init(&g_js, "javascript", JS_EXTENSIONS, tree_sitter_javascript);
}

const LanguageAdapter *typescript_adapter_get(void) {
    return init(&g_ts, "typescript", TS_EXTENSIONS, tree_sitter_typescript);
}

const LanguageAdapter *tsx_adapter_get(void) {
    return init(&g_tsx, "tsx", TSX_EXTENSIONS, tree_sitter_tsx);
}
