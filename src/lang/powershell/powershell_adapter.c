/* PowerShell dependency adapter (.ps1 scripts, .psm1 modules, .psd1
 * module manifests).
 *
 * References:
 *   . .\lib\common.ps1                  dot-sourcing
 *   . "$PSScriptRoot\lib\helpers.ps1"   ...relative to this script
 *   & .\scripts\build.ps1               call operator
 *   .\scripts\test.ps1                  running a script directly
 *   Import-Module .\modules\Deploy.psm1 / Import-Module Deploy (by name)
 *   using module ./modules/Types.psm1
 *   cmd /c build.bat, Start-Process x.bat, powershell -File x.ps1
 *                                        (any argument naming a script)
 *   RootModule / NestedModules / RequiredModules / ScriptsToProcess
 *                                        entries in a .psd1 manifest
 *
 * Paths: $PSScriptRoot (or any leading variable followed by a separator)
 * means this script's directory; anything more dynamic -- Join-Path, a
 * variable in the middle -- is skipped. Resolution, shared with the batch
 * adapter (winscript.h): relative path, script extensions, module folders,
 * file name, then module name. `Import-Module Az.Accounts` names an
 * installed module and stays unresolved.
 *
 * Node shapes (airbus-cert/tree-sitter-powershell v0.26.5, confirmed by
 * parsing samples): command has an optional command_invokation_operator
 * child ('.' or '&'), field command_name (command_name, or
 * command_name_expr wrapping a command_name / string_literal), and field
 * command_elements holding generic_token / command_parameter /
 * expression arguments (a quoted string is nested inside
 * array_literal_expression > unary_expression > string_literal). Manifest
 * entries are hash_entry nodes: key_expression > simple_name, then the
 * value.
 */

#include "powershell_adapter.h"
#include "../winscript.h"
#include <tree_sitter/api.h>
#include <stdlib.h>
#include <string.h>

extern const TSLanguage *tree_sitter_powershell(void);

static const char *PS_EXTENSIONS[] = { ".ps1", ".psm1", ".psd1", NULL };
static const char *SCRIPT_EXTS[] = { ".ps1", ".psm1", ".psd1", ".bat", ".cmd", NULL };

static const char *MANIFEST_KEYS[] = {
    "rootmodule", "moduletoprocess", "nestedmodules", "requiredmodules", "scriptstoprocess", NULL,
};

static void ps_extract_declarations(const ParsedFile *file, TSQuery *decl_query, DeclSinkFn sink, void *ctx) {
    (void)decl_query;
    winscript_declare(file, sink, ctx);
}

static bool text_ieq(const ParsedFile *file, TSNode node, const char *s) {
    uint32_t a = ts_node_start_byte(node), b = ts_node_end_byte(node);
    size_t n = strlen(s);
    if (b - a != n) return false;
    for (size_t i = 0; i < n; i++) {
        char c = file->source[a + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != s[i]) return false;
    }
    return true;
}

/* The first string_literal / command_name / generic_token at or under
 * node -- where an argument's actual text lives. */
static TSNode text_node(TSNode node) {
    const char *t = ts_node_type(node);
    if (strcmp(t, "string_literal") == 0 || strcmp(t, "command_name") == 0 || strcmp(t, "generic_token") == 0) {
        return node;
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) {
        TSNode found = text_node(ts_node_named_child(node, i));
        if (!ts_node_is_null(found)) return found;
    }
    TSNode none = { 0 };
    return none;
}

/* A node's path text, cleaned (see winscript_clean_path). Uses the node's
 * whole source text rather than a particular child: an unquoted
 * `$PSScriptRoot/lib.ps1` parses as a member access (with an error node),
 * not a string, but its text is still exactly the path. Caller frees. */
static char *path_of(const ParsedFile *file, TSNode node) {
    uint32_t a = ts_node_start_byte(node), b = ts_node_end_byte(node);
    return winscript_clean_path(file->source + a, b - a);
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

static void handle_command(TSNode cmd, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    TSNode name = ts_node_child_by_field_name(cmd, "command_name", 12);
    TSNode elements = ts_node_child_by_field_name(cmd, "command_elements", 16);
    if (ts_node_is_null(name)) return;

    /* `. path` / `& path`: the command name is the script. */
    TSNode op = { 0 };
    uint32_t n = ts_node_named_child_count(cmd);
    for (uint32_t i = 0; i < n; i++) {
        TSNode c = ts_node_named_child(cmd, i);
        if (strcmp(ts_node_type(c), "command_invokation_operator") == 0) op = c;
    }
    if (!ts_node_is_null(op)) {
        bool dot = text_ieq(file, op, ".");
        emit(sink, ctx, path_of(file, name), dot ? REF_IMPORT_PATH : REF_MODULE_REQUIRE);
        return;
    }

    /* Collect the arguments that aren't -Parameters. */
    TSNode args[16];
    int nargs = 0;
    if (!ts_node_is_null(elements)) {
        uint32_t en = ts_node_named_child_count(elements);
        for (uint32_t i = 0; i < en && nargs < 16; i++) {
            TSNode c = ts_node_named_child(elements, i);
            const char *ct = ts_node_type(c);
            if (strcmp(ct, "command_argument_sep") == 0 || strcmp(ct, "command_parameter") == 0) continue;
            args[nargs++] = c;
        }
    }

    if (text_ieq(file, name, "import-module") || text_ieq(file, name, "ipmo")) {
        if (nargs > 0) emit(sink, ctx, path_of(file, args[0]), REF_IMPORT_PATH);
        return;
    }
    if (text_ieq(file, name, "using")) {
        /* using module <path>; `using namespace` / `using assembly` aren't files. */
        if (nargs >= 2 && text_ieq(file, text_node(args[0]), "module")) {
            emit(sink, ctx, path_of(file, args[1]), REF_IMPORT_PATH);
        }
        return;
    }

    /* Running a script directly: `.\scripts\test.ps1`. */
    char *self = path_of(file, name);
    if (self && winscript_has_ext(self, SCRIPT_EXTS)) {
        emit(sink, ctx, self, REF_MODULE_REQUIRE);
        return;
    }
    free(self);

    /* Any other command handed a script: cmd /c x.bat, Start-Process x.bat,
     * powershell -File x.ps1. */
    for (int i = 0; i < nargs; i++) {
        char *p = path_of(file, args[i]);
        if (p && winscript_has_ext(p, SCRIPT_EXTS)) emit(sink, ctx, p, REF_MODULE_REQUIRE);
        else free(p);
    }
}

/* Every string in a manifest value (a string, or an @('a', 'b') array). */
static void emit_strings(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    if (strcmp(ts_node_type(node), "string_literal") == 0) {
        uint32_t a = ts_node_start_byte(node), b = ts_node_end_byte(node);
        emit(sink, ctx, winscript_clean_path(file->source + a, b - a), REF_IMPORT_PATH);
        return;
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) emit_strings(ts_node_named_child(node, i), file, sink, ctx);
}

static void collect_refs(TSNode node, const ParsedFile *file, RefSinkFn sink, void *ctx) {
    const char *type = ts_node_type(node);
    if (strcmp(type, "command") == 0) {
        handle_command(node, file, sink, ctx);
    } else if (strcmp(type, "hash_entry") == 0) {
        TSNode key = ts_node_named_child(node, 0);
        TSNode name = ts_node_is_null(key) ? key : text_node(key);
        if (ts_node_is_null(name) && !ts_node_is_null(key)) name = ts_node_named_child(key, 0);
        bool manifest_key = false;
        for (int i = 0; !ts_node_is_null(name) && MANIFEST_KEYS[i] && !manifest_key; i++) {
            manifest_key = text_ieq(file, name, MANIFEST_KEYS[i]);
        }
        if (manifest_key) {
            uint32_t n = ts_node_named_child_count(node);
            for (uint32_t i = 1; i < n; i++) emit_strings(ts_node_named_child(node, i), file, sink, ctx);
            return;
        }
    }
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n; i++) collect_refs(ts_node_named_child(node, i), file, sink, ctx);
}

static void ps_extract_references(const ParsedFile *file, TSQuery *ref_query, RefSinkFn sink, void *ctx) {
    (void)ref_query;
    collect_refs(ts_tree_root_node(file->tree), file, sink, ctx);
}

static char *ps_resolve_reference(const char *raw_text, const char *referencing_file_path,
                                  const char *scope_namespace, const char **imported_namespaces,
                                  int imported_count, void *symbol_table_handle) {
    (void)scope_namespace;
    (void)imported_namespaces;
    (void)imported_count;
    return winscript_resolve(raw_text, referencing_file_path, symbol_table_handle);
}

static LanguageAdapter g_ps_adapter;
static int g_initialized = 0;

const LanguageAdapter *powershell_adapter_get(void) {
    if (!g_initialized) {
        g_ps_adapter.name = "powershell";
        g_ps_adapter.extensions = PS_EXTENSIONS;
        g_ps_adapter.extension_count = 3;
        g_ps_adapter.ts_language = tree_sitter_powershell;
        g_ps_adapter.decl_query = NULL;
        g_ps_adapter.ref_query = NULL;
        g_ps_adapter.extract_declarations = ps_extract_declarations;
        g_ps_adapter.extract_references = ps_extract_references;
        g_ps_adapter.resolve_reference = ps_resolve_reference;
        g_initialized = 1;
    }
    return &g_ps_adapter;
}
