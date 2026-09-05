/* Lisp dependency adapter -- CLEARLY A COMMON LISP STAND-IN.
 *
 * The actual Lisp dialect in the target codebase wasn't known when this
 * was written. tree-sitter-grammars/tree-sitter-commonlisp is WIP,
 * extends the tree-sitter-clojure grammar, and its node-types.json
 * (confirmed by inspection) has NO Common-Lisp-specific node types --
 * `in-package`, `defpackage`, `require`, `load`, and `:use` clauses are
 * all just generic `list_lit`/`sym_lit`/`kwd_lit`/`str_lit` shapes with
 * no semantic node of their own. So unlike the C# adapter (which walks
 * real grammar fields), this one pattern-matches on head-symbol text.
 *
 * Scope is deliberately narrow -- this exists to prove the adapter
 * interface holds for a dependency model that's nothing like C#'s
 * (module/package names as direct string references, not nested
 * namespace-qualified type names), not to be a production-quality
 * extractor:
 *   - `(in-package X)` / `(defpackage X ...)` -> this file declares
 *     package X (whole-file granularity, matching how Lisp actually
 *     namespaces -- one package per file/module, not per form). Two
 *     files declaring the same package will collide in the symbol
 *     table (last one wins); a known, accepted limitation here.
 *   - `(require X)` / `(load X)` -> a direct reference to package/file X.
 *   - `(defpackage X (:use Y Z))` -> also references to packages Y, Z.
 *   - ASDF `:depends-on` (typically in a separate .asd file) isn't
 *     handled at all yet.
 *
 * If the real dialect turns out to be Emacs Lisp, Wilfred/tree-sitter-
 * elisp is the recommended swap-in -- more mature, actively maintained.
 * Swapping it in only touches this file and the CMake fetch, per the
 * adapter interface's whole point.
 */

#include "lisp_adapter.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_commonlisp(void);

static const char *LISP_EXTENSIONS[] = { ".lisp", ".lsp", ".cl", NULL };

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

/* Common Lisp symbols are traditionally case-insensitive. */
static int ci_equal(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* Strips a leading ':' (kwd_lit) or surrounding '"..."' (str_lit) so
 * `:foo`, `"foo"`, and bare `foo` (sym_lit) all normalize to "foo". */
static char *lisp_name_text(const ParsedFile *file, TSNode node) {
    char *raw = node_text(file, node);
    size_t len = strlen(raw);
    size_t start = (len > 0 && raw[0] == ':') ? 1 : 0;
    size_t end = len;
    if (end > start + 1 && raw[start] == '"' && raw[end - 1] == '"') {
        start++;
        end--;
    }
    char *out = strndup_local(raw + start, end - start);
    free(raw);
    return out;
}

static void lisp_extract_declarations(const ParsedFile *file, TSQuery *decl_query,
                                       DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    TSNode root = ts_tree_root_node(file->tree);
    uint32_t n = ts_node_named_child_count(root);

    for (uint32_t i = 0; i < n; i++) {
        TSNode form = ts_node_named_child(root, i);
        if (strcmp(ts_node_type(form), "list_lit") != 0) continue;
        uint32_t fn = ts_node_named_child_count(form);
        if (fn < 2) continue;

        TSNode head = ts_node_named_child(form, 0);
        if (strcmp(ts_node_type(head), "sym_lit") != 0) continue;
        char *head_text = node_text(file, head);
        int is_pkg_decl = ci_equal(head_text, "in-package") || ci_equal(head_text, "defpackage");
        free(head_text);
        if (!is_pkg_decl) continue;

        char *pkg_name = lisp_name_text(file, ts_node_named_child(form, 1));
        DeclFact fact;
        fact.kind = DECL_PACKAGE;
        fact.qualified_name = pkg_name;
        sink(ctx, &fact);
        free(pkg_name);
    }
}

static void lisp_extract_references(const ParsedFile *file, TSQuery *ref_query,
                                     RefSinkFn sink, void *ctx) {
    (void)ref_query;
    TSNode root = ts_tree_root_node(file->tree);
    uint32_t n = ts_node_named_child_count(root);

    for (uint32_t i = 0; i < n; i++) {
        TSNode form = ts_node_named_child(root, i);
        if (strcmp(ts_node_type(form), "list_lit") != 0) continue;
        uint32_t fn = ts_node_named_child_count(form);
        if (fn < 1) continue;

        TSNode head = ts_node_named_child(form, 0);
        if (strcmp(ts_node_type(head), "sym_lit") != 0) continue;
        char *head_text = node_text(file, head);

        if ((ci_equal(head_text, "require") || ci_equal(head_text, "load")) && fn >= 2) {
            char *name = lisp_name_text(file, ts_node_named_child(form, 1));
            RefFact fact;
            fact.kind = REF_MODULE_REQUIRE;
            fact.raw_text = name;
            fact.scope_namespace = NULL;
            sink(ctx, &fact);
            free(name);
        } else if (ci_equal(head_text, "defpackage")) {
            for (uint32_t c = 2; c < fn; c++) {
                TSNode clause = ts_node_named_child(form, c);
                if (strcmp(ts_node_type(clause), "list_lit") != 0) continue;
                uint32_t cn = ts_node_named_child_count(clause);
                if (cn < 1) continue;

                char *clause_head = lisp_name_text(file, ts_node_named_child(clause, 0));
                if (ci_equal(clause_head, "use")) {
                    for (uint32_t u = 1; u < cn; u++) {
                        char *used = lisp_name_text(file, ts_node_named_child(clause, u));
                        RefFact fact;
                        fact.kind = REF_IMPORT_PATH;
                        fact.raw_text = used;
                        fact.scope_namespace = NULL;
                        sink(ctx, &fact);
                        free(used);
                    }
                }
                free(clause_head);
            }
        }

        free(head_text);
    }
}

/* No namespace qualification needed -- a require/load/:use argument is
 * already the target package's own name. */
static char *lisp_resolve_reference(const char *raw_text,
                                     const char *referencing_file_path,
                                     const char *scope_namespace,
                                     const char **imported_namespaces,
                                     int imported_count,
                                     void *symbol_table_handle) {
    (void)referencing_file_path;
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text) return NULL;
    if (symtab_get(table, raw_text) >= 0) return xstrdup(raw_text);
    return NULL;
}

static LanguageAdapter g_lisp_adapter;
static int g_initialized = 0;

const LanguageAdapter *lisp_adapter_get(void) {
    if (!g_initialized) {
        g_lisp_adapter.name = "lisp";
        g_lisp_adapter.extensions = LISP_EXTENSIONS;
        g_lisp_adapter.extension_count = 3;
        g_lisp_adapter.ts_language = tree_sitter_commonlisp;
        g_lisp_adapter.decl_query = NULL;
        g_lisp_adapter.ref_query = NULL;
        g_lisp_adapter.extract_declarations = lisp_extract_declarations;
        g_lisp_adapter.extract_references = lisp_extract_references;
        g_lisp_adapter.resolve_reference = lisp_resolve_reference;
        g_initialized = 1;
    }
    return &g_lisp_adapter;
}
