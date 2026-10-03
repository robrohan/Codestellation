/* Bash / shell script dependency adapter.
 *
 * A shell script depends on another when it pulls it in or runs it:
 *
 *   source lib/common.sh          . lib/common.sh
 *   ./deploy.sh --prod            scripts/build.sh
 *   bash scripts/build.sh         (also sh, zsh, exec)
 *
 * Scripts usually locate their siblings relative to themselves, not the
 * working directory, with an expansion in front of the path:
 *
 *   . "$(dirname "$0")/env.sh"
 *   source "$(dirname "${BASH_SOURCE[0]}")/helpers.sh"
 *   source "$SCRIPT_DIR/lib.sh"
 *
 * so any leading expansion is taken to mean "this script's directory" and
 * only the literal tail ("/env.sh") is kept. That's a heuristic -- a
 * variable could point anywhere -- but it's the overwhelmingly common
 * idiom. Arguments that are expansions all the way through are skipped.
 *
 *   - Declarations: the file's absolute path, plus "sh:<file name>" as a
 *     fallback key for scripts invoked by bare name from elsewhere
 *     (collisions: last one wins, as with Python's module suffixes).
 *   - Resolution: joined to the script's directory and cleaned lexically;
 *     failing that, matched by file name.
 *
 * Only .sh / .bash files are walked; extension-less scripts with a shebang
 * aren't picked up (the walker is extension-based).
 *
 * Node shapes (tree-sitter-bash v0.25.1, confirmed by parsing samples):
 * command has field name (command_name wrapping a word) and repeated field
 * argument (word | string | raw_string | concatenation | expansion...); a
 * double-quoted string's children are string_content pieces interleaved
 * with simple_expansion / expansion / command_substitution.
 */

#include "bash_adapter.h"
#include "../../common/symtab.h"
#include "../../common/pathutil.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_bash(void);

static const char *BASH_EXTENSIONS[] = { ".sh", ".bash", NULL };

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (slash && (!bslash || slash > bslash)) return slash + 1;
    return bslash ? bslash + 1 : path;
}

static void bash_extract_declarations(const ParsedFile *file, TSQuery *decl_query, DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    DeclFact fact;
    fact.kind = DECL_PACKAGE;
    fact.qualified_name = file->path;
    sink(ctx, &fact);

    const char *base = base_name(file->path);
    size_t n = strlen(base);
    char *key = (char *)malloc(n + 4);
    memcpy(key, "sh:", 3);
    memcpy(key + 3, base, n + 1);
    fact.qualified_name = key;
    sink(ctx, &fact);
    free(key);
}

static char *slice(const ParsedFile *file, TSNode node) {
    uint32_t s = ts_node_start_byte(node), e = ts_node_end_byte(node);
    char *out = (char *)malloc(e - s + 1);
    memcpy(out, file->source + s, e - s);
    out[e - s] = '\0';
    return out;
}

static bool is_expansion(const char *type) {
    return strcmp(type, "simple_expansion") == 0 || strcmp(type, "expansion") == 0 ||
           strcmp(type, "command_substitution") == 0;
}

/* The literal path an argument names. A leading expansion is replaced by
 * "." (the script's own directory) -- "$(dirname "$0")/env.sh" becomes
 * "./env.sh". NULL when there's no literal part. Caller frees. */
static char *argument_path(const ParsedFile *file, TSNode arg) {
    const char *type = ts_node_type(arg);
    if (strcmp(type, "word") == 0) return slice(file, arg);
    if (strcmp(type, "raw_string") == 0) {
        char *s = slice(file, arg); /* 'text' -- drop the quotes */
        size_t n = strlen(s);
        if (n >= 2) {
            memmove(s, s + 1, n - 2);
            s[n - 2] = '\0';
        }
        return s;
    }
    if (strcmp(type, "string") != 0 && strcmp(type, "concatenation") != 0) return NULL;

    /* Keep only what follows the last expansion. */
    uint32_t n = ts_node_named_child_count(arg);
    int last_exp = -1;
    for (uint32_t i = 0; i < n; i++) {
        if (is_expansion(ts_node_type(ts_node_named_child(arg, i)))) last_exp = (int)i;
    }
    size_t cap = 1, len = 0;
    char *out = (char *)malloc(cap);
    out[0] = '\0';
    if (last_exp >= 0) {
        /* The expansion stands for the script's directory. */
        free(out);
        out = xstrdup(".");
        len = 1;
        cap = 2;
    }
    for (uint32_t i = (uint32_t)(last_exp + 1); i < n; i++) {
        TSNode c = ts_node_named_child(arg, i);
        const char *ct = ts_node_type(c);
        if (strcmp(ct, "string_content") != 0 && strcmp(ct, "word") != 0) continue;
        char *piece = slice(file, c);
        size_t pl = strlen(piece);
        if (len + pl + 1 > cap) {
            cap = (len + pl + 1) * 2;
            out = (char *)realloc(out, cap);
        }
        memcpy(out + len, piece, pl + 1);
        len += pl;
        free(piece);
    }
    /* Just the expansion, nothing literal at all, or an expansion that's
     * part of the file name ("$NAME.sh") rather than a directory. */
    if (len == 0 || (last_exp >= 0 && (len == 1 || out[1] != '/'))) {
        free(out);
        return NULL;
    }
    return out;
}

static bool is_script_name(const char *s) {
    size_t n = strlen(s);
    return (n > 3 && strcmp(s + n - 3, ".sh") == 0) || (n > 5 && strcmp(s + n - 5, ".bash") == 0);
}

static void emit(RefSinkFn sink, void *ctx, char *path, RefKind kind) {
    if (!path) return;
    RefFact fact;
    fact.kind = kind;
    fact.raw_text = path;
    fact.scope_namespace = NULL;
    sink(ctx, &fact);
    free(path);
}

static void collect_refs(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    if (strcmp(ts_node_type(node), "command") == 0) {
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        TSNode first_arg = { 0 };
        bool have_arg = false;
        uint32_t cc = ts_node_child_count(node);
        for (uint32_t i = 0; i < cc && !have_arg; i++) {
            const char *field = ts_node_field_name_for_child(node, i);
            if (field && strcmp(field, "argument") == 0) {
                first_arg = ts_node_child(node, i);
                have_arg = true;
            }
        }
        if (!ts_node_is_null(name)) {
            char *cmd = slice(file, name);
            if (strcmp(cmd, "source") == 0 || strcmp(cmd, ".") == 0) {
                if (have_arg) emit(sink, ctx, argument_path(file, first_arg), REF_IMPORT_PATH);
            } else if (strcmp(cmd, "bash") == 0 || strcmp(cmd, "sh") == 0 || strcmp(cmd, "zsh") == 0 ||
                       strcmp(cmd, "exec") == 0) {
                if (have_arg) {
                    char *p = argument_path(file, first_arg);
                    if (p && is_script_name(p)) emit(sink, ctx, p, REF_MODULE_REQUIRE);
                    else free(p);
                }
            } else {
                /* Running a script directly: the command name itself. */
                char *p = ts_node_named_child_count(name) > 0 ? argument_path(file, ts_node_named_child(name, 0))
                                                              : NULL;
                if (p && is_script_name(p)) emit(sink, ctx, p, REF_MODULE_REQUIRE);
                else free(p);
            }
            free(cmd);
        }
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, sink, ctx);
}

static void bash_extract_references(const ParsedFile *file, TSQuery *ref_query, RefSinkFn sink, void *ctx) {
    (void)ref_query;
    collect_refs(ts_tree_root_node(file->tree), file, sink, ctx);
}

static char *bash_resolve_reference(const char *raw_text, const char *referencing_file_path,
                                    const char *scope_namespace, const char **imported_namespaces,
                                    int imported_count, void *symbol_table_handle) {
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    SymbolTable *table = (SymbolTable *)symbol_table_handle;
    if (!table || !raw_text || !raw_text[0]) return NULL;

    char *joined;
    if (raw_text[0] == '/') {
        joined = xstrdup(raw_text);
    } else {
        char *dir = path_dirname(referencing_file_path);
        joined = path_join(dir, raw_text);
        free(dir);
    }
    char *path = path_clean(joined);
    free(joined);
    if (symtab_get(table, path) >= 0) return path;
    free(path);

    /* Fallback: some script with that file name, wherever it lives. */
    const char *base = base_name(raw_text);
    size_t n = strlen(base);
    char *key = (char *)malloc(n + 4);
    memcpy(key, "sh:", 3);
    memcpy(key + 3, base, n + 1);
    if (symtab_get(table, key) >= 0) return key;
    free(key);
    return NULL;
}

/* Complexity stats (see adapter.h). */
static const char *const BASH_BRANCH_TYPES[] = { "if_statement", "elif_clause", "for_statement", "c_style_for_statement", "while_statement", "case_item", "ternary_expression", "&&", "||", NULL };
static const char *const BASH_FUNCTION_TYPES[] = { "function_definition", NULL };

static LanguageAdapter g_bash_adapter;
static int g_initialized = 0;

const LanguageAdapter *bash_adapter_get(void) {
    if (!g_initialized) {
        g_bash_adapter.name = "bash";
        g_bash_adapter.extensions = BASH_EXTENSIONS;
        g_bash_adapter.extension_count = 2;
        g_bash_adapter.ts_language = tree_sitter_bash;
        g_bash_adapter.decl_query = NULL;
        g_bash_adapter.ref_query = NULL;
        g_bash_adapter.extract_declarations = bash_extract_declarations;
        g_bash_adapter.extract_references = bash_extract_references;
        g_bash_adapter.resolve_reference = bash_resolve_reference;
        g_bash_adapter.branch_types = BASH_BRANCH_TYPES;
        g_bash_adapter.function_types = BASH_FUNCTION_TYPES;
        g_initialized = 1;
    }
    return &g_bash_adapter;
}
