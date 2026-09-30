#ifndef CODEMAP_WINSCRIPT_H
#define CODEMAP_WINSCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include "adapter.h"

/* Shared by the PowerShell and batch adapters, which reference each other
 * (a .bat running `powershell -File x.ps1`, a .ps1 running `cmd /c x.bat`)
 * and share Windows habits: backslash paths, case-insensitive names, and
 * "relative to this script" prefixes ($PSScriptRoot, %~dp0).
 *
 * Keys each file declares (winscript_declare):
 *   - its absolute path;
 *   - "winscript:<file name, lowercased>" -- for scripts named without a
 *     usable path, and case-insensitive matching;
 *   - for PowerShell modules (.psm1/.psd1), "psmod:<name, lowercased>" --
 *     so `Import-Module Deploy` finds Deploy.psm1 / Deploy.psd1.
 */

void winscript_declare(const ParsedFile *file, DeclSinkFn sink, void *ctx);

/* Resolves a script/module reference as written, relative to the
 * referencing script's directory: exact path, then with a script
 * extension appended (.ps1 .psm1 .psd1 .bat .cmd), then a module folder
 * (dir/Name/Name.psd1 or .psm1), then by file name, then as a module name.
 * Returns the symbol-table key (malloc'd) or NULL. */
char *winscript_resolve(const char *raw_text, const char *referencing_file_path, void *symbol_table_handle);

/* Normalizes a path as written in a script: strips surrounding quotes and
 * replaces a leading "this script's directory" prefix -- $PSScriptRoot or
 * any other leading $variable / %VAR% followed by a separator, or %~dp0 --
 * with ".". Returns NULL when nothing usable is left (e.g. a bare
 * variable). Caller frees. */
char *winscript_clean_path(const char *text, size_t len);

/* True if the name ends with one of the given extensions
 * (case-insensitive), e.g. {".ps1", ".bat", NULL}. */
bool winscript_has_ext(const char *name, const char *const *exts);

#endif
