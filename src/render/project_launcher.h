#ifndef CODEMAP_PROJECT_LAUNCHER_H
#define CODEMAP_PROJECT_LAUNCHER_H

/* Resolves `picked_dir` (an absolute, existing directory -- e.g. straight
 * from tinyfd_selectFolderDialog) to its Application Support project
 * folder, runs `codemap-build --root picked_dir --out <that>/graph.json`
 * (blocking), and on success execv()s a fresh codemap-view process
 * pointed at the result -- this call never returns on success. On
 * failure, shows a native error dialog and returns normally so the
 * caller keeps running exactly as it was.
 *
 * self_exe_path: this process's own resolved executable path (see
 * main.c) -- used both to find the sibling codemap-build binary (built
 * to the same output directory in a normal dev build; embedded into
 * Contents/MacOS/ alongside codemap-view in the signed Xcode bundle
 * build, see src/CMakeLists.txt) and to relaunch. */
void project_launcher_build_and_relaunch(const char *self_exe_path, const char *picked_dir);

#endif
