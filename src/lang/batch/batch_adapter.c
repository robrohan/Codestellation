/* Windows batch / cmd dependency adapter (.bat, .cmd).
 *
 * References:
 *   call lib\env.bat                    call another script
 *   call "%~dp0scripts\build.cmd"       ...relative to this script
 *   call build                          no extension: build.bat / .cmd
 *   scripts\test.bat                    running a script directly
 *   powershell -File "%~dp0deploy.ps1"  handing a script to another
 *   start "" run.bat, cmd /c x.bat       program: any argument that names
 *                                        a .bat/.cmd/.ps1
 * `call :label` is a subroutine in the same file, not a dependency.
 *
 * Paths: %~dp0 (and a leading %VAR% followed by a separator) mean this
 * script's directory; anything with an expansion in the middle is
 * skipped. Resolution is shared with the PowerShell adapter (winscript.h),
 * so batch -> PowerShell edges come out naturally.
 *
 * The grammar (wharflab/tree-sitter-batch v0.11.1) finds the statements --
 * call_stmt for CALL, cmd for any other command -- but its node boundaries
 * can't be trusted for paths: an unquoted command name stops at the first
 * backslash (`lib\env.bat` becomes "lib" plus an argument "\env.bat") and
 * `call ..\lib\x.bat` is a parse error after "call". So each statement's
 * words are read straight from its source line instead, split the way
 * cmd.exe splits them: on whitespace, keeping "quoted strings" whole, and
 * stopping at & | < > or a closing ).
 */

#include "batch_adapter.h"
#include "../winscript.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_batch(void);

static const char *BATCH_EXTENSIONS[] = { ".bat", ".cmd", NULL };
static const char *SCRIPT_EXTS[] = { ".bat", ".cmd", ".ps1", ".psm1", NULL };

static void batch_extract_declarations(const ParsedFile *file, TSQuery *decl_query, DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    winscript_declare(file, sink, ctx);
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

typedef struct {
    const char *text;
    size_t len;
} Word;

#define MAX_WORDS 16

/* The words of the statement starting at `start`, up to the end of its
 * line or a command separator. Returns the count. */
static int line_words(const ParsedFile *file, uint32_t start, Word *out) {
    const char *p = file->source + start;
    const char *end = file->source + file->source_len;
    int n = 0;
    while (p < end && n < MAX_WORDS) {
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p >= end || *p == '\r' || *p == '\n' || *p == '&' || *p == '|' || *p == '<' || *p == '>' || *p == ')') {
            break;
        }
        const char *w = p;
        if (*p == '"') {
            p++;
            while (p < end && *p != '"' && *p != '\n') p++;
            if (p < end && *p == '"') p++;
        } else {
            while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '&' && *p != '|' &&
                   *p != '<' && *p != '>' && *p != ')') {
                p++;
            }
        }
        out[n].text = w;
        out[n].len = (size_t)(p - w);
        n++;
    }
    return n;
}

static bool word_ieq(Word w, const char *s) {
    size_t n = strlen(s);
    if (w.len != n) return false;
    for (size_t i = 0; i < n; i++) {
        char c = w.text[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != s[i]) return false;
    }
    return true;
}

static void emit_script_words(const Word *words, int from, int count, RefSinkFn sink, void *ctx) {
    for (int i = from; i < count; i++) {
        char *p = winscript_clean_path(words[i].text, words[i].len);
        if (p && winscript_has_ext(p, SCRIPT_EXTS)) emit(sink, ctx, p, REF_MODULE_REQUIRE);
        else free(p);
    }
}

static void collect_refs(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);
    bool is_call = strcmp(type, "call_stmt") == 0;
    if (is_call || strcmp(type, "cmd") == 0) {
        Word words[MAX_WORDS];
        int n = line_words(file, ts_node_start_byte(node), words);
        /* "@call x.bat" / "@x.bat": the @ only hides the echo. */
        if (n > 0 && words[0].len > 1 && words[0].text[0] == '@') {
            words[0].text++;
            words[0].len--;
        }
        int target = 0;
        if (is_call && n > 0 && word_ieq(words[0], "call")) target = 1;
        if (target < n) {
            char *p = winscript_clean_path(words[target].text, words[target].len);
            /* `call x` needn't name the extension; running a script
             * directly must (otherwise it's just some command). `call
             * :label` jumps within this file. */
            if (p && p[0] != ':' && (is_call || winscript_has_ext(p, SCRIPT_EXTS))) {
                emit(sink, ctx, p, REF_MODULE_REQUIRE);
            } else {
                free(p);
            }
            /* Scripts handed to another program: start, cmd /c,
             * powershell -File, ... */
            emit_script_words(words, target + 1, n, sink, ctx);
        }
        return;
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, sink, ctx);
}

static void batch_extract_references(const ParsedFile *file, TSQuery *ref_query, RefSinkFn sink, void *ctx) {
    (void)ref_query;
    collect_refs(ts_tree_root_node(file->tree), file, sink, ctx);
}

static char *batch_resolve_reference(const char *raw_text, const char *referencing_file_path,
                                     const char *scope_namespace, const char **imported_namespaces,
                                     int imported_count, void *symbol_table_handle) {
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    return winscript_resolve(raw_text, referencing_file_path, symbol_table_handle);
}

/* Complexity stats (see adapter.h). */
static const char *const BATCH_BRANCH_TYPES[] = { "if_stmt", "for_stmt", "&&", "||", NULL };

static LanguageAdapter g_batch_adapter;
static int g_initialized = 0;

const LanguageAdapter *batch_adapter_get(void) {
    if (!g_initialized) {
        g_batch_adapter.name = "batch";
        g_batch_adapter.extensions = BATCH_EXTENSIONS;
        g_batch_adapter.extension_count = 2;
        g_batch_adapter.ts_language = tree_sitter_batch;
        g_batch_adapter.decl_query = NULL;
        g_batch_adapter.ref_query = NULL;
        g_batch_adapter.extract_declarations = batch_extract_declarations;
        g_batch_adapter.extract_references = batch_extract_references;
        g_batch_adapter.resolve_reference = batch_resolve_reference;
        g_batch_adapter.branch_types = BATCH_BRANCH_TYPES;
        g_initialized = 1;
    }
    return &g_batch_adapter;
}
