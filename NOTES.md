# Handoff notes

Written by Claude at the end of the session that built this from scratch, for
whichever fresh session picks it up next in this directory. That session will
have zero memory of any of this — everything worth knowing is here or in
`git log` / the code itself.

## What this is

A native C tool for visually exploring a codebase's cross-file dependencies as
an interactive 3D graph — built because the user needed to dig through a large
legacy multi-language codebase and wanted something more useful than reading
files cold. Two binaries:

- `codemap-build` — headless CLI. Walks a directory (recursively, mixed
  languages in one tree work fine), parses each file with tree-sitter via a
  per-language adapter, resolves cross-file references into a dependency
  graph, writes it to `graph.json` (networkx node-link format).
- `codemap-view` — loads a `graph.json`, lays it out in 3D once at startup
  (force-directed, not live physics), and renders it in native OpenGL with a
  Nuklear UI for inspecting files.

Stack: pure C11, CMake + FetchContent for deps (tree-sitter + per-language
grammars, GLFW), Nuklear for UI, vendored GLAD for the Windows/Linux GL loader
(macOS links GL directly from the framework, no loader needed there).

## Current state (as of the last commit before this note)

All of this is built, working, and verified on macOS:

- **Languages**: C (`#include`-based, no symbol table needed), C# (real
  namespace/type-scoped resolution, `using`-qualified), Lisp (a **Common Lisp
  stand-in** — the actual dialect in the target codebase was never confirmed;
  see the big comment at the top of `src/lang/lisp/lisp_adapter.c` before
  trusting it on anything real).
- **Viewer**: 3D force-directed layout (centered at origin, with a gravity
  term so sparse graphs don't expand unboundedly — see `layout3d.c`), orbit
  camera with scroll-zoom, pan, an XYZ axis gizmo, floating filename labels
  above nodes, click-to-inspect (shows real file content in a side panel),
  drag-to-reposition nodes, and edge highlighting (selecting a node brightens
  its edges and dims everything else).
- **Controls** (current scheme, changed once already — the user explicitly
  wanted select and reposition decoupled from each other):
  - Left-click + drag on a node → reposition it. On empty space → orbit.
  - Right-click (no real movement) on a node → select it, highlighting its
    edges. On empty space → deselect. Right-click **+ drag** → pan instead
    (distinguished from a click by a small pixel-movement threshold).
  - Middle-click + drag → also pan.
  - Scroll wheel → zoom (has both a proportional and a minimum-absolute step
    so it doesn't stall near the floor — see the comment in `main.c`'s scroll
    handler if this ever feels wrong again).
- **CI**: `.github/workflows/windows-build.yml` builds on `windows-latest`
  with MSVC and smoke-tests `codemap-build` for real against all fixture
  sets. **This has never actually been run on a real GitHub Actions runner —
  there is no way to verify that from a local dev environment.** First thing
  to check in this new session: did it pass? (GitHub Actions tab on the
  repo.) If not, that's likely priority one.
- Pushed to `git@github.com:robrohan/Codestellation.git`.

## Naming is half-done — flag this, don't just silently fix it

The **repo/folder** is named `Codestellation` (chosen for the "code +
constellation" pun, and because it's meant to grow from a single-codebase
star map into a multi-service "galaxy" view later). The **internals still say
`codemap3d`**: the CMake project name, both binary names (`codemap-build`,
`codemap-view`), the GLFW window title, and the README all predate the
rename. Ask the user whether they want a real rename pass (binaries, window
title, `project()` name, README) or whether "Codestellation" is just the
repo-level brand and the tool keeps calling itself codemap3d internally —
don't assume either way.

## What the user said they want to do next, in order

1. **Confirm the Windows CI actually passes.** Unverified as of this note.
2. **Design a "notes/annotations" feature** — the user wants to attach
   comments to files at particular lines, and see a dot/indicator in the UI
   (probably near the label or node) marking which files have notes. They
   were explicit: **talk through the design before building anything.** Don't
   jump straight to implementation on this one. Open questions worth raising
   when that conversation happens: where do annotations persist (a sidecar
   JSON next to `graph.json`, keyed by file path + line number, seems like
   the natural fit given everything else here is file-path-keyed), how does
   the UI show/edit them (a new Nuklear panel? inline in the existing
   inspector?), and whether they attach to a specific commit/version of a
   file or just float on the current on-disk content.
3. Longer-term, unprompted-so-far idea from earlier design discussion: a
   "supergraph" linking multiple projects' graphs together (microservices
   scenario). Not started. If it comes up, `graph_json.c`'s node ids are
   currently NOT namespaced by project, which would need addressing before
   merging multiple graphs into one scene.

## Architecture, if you need to navigate the code fast

- `src/lang/adapter.h` — the `LanguageAdapter` interface every language
  plugs into. The pipeline (`src/pipeline/*.c`) never references a specific
  language by name, only through this struct.
- `src/lang/{c,csharp,lisp}/` — the three adapters. C is the simplest
  (no symbol table). C# walks the parse tree directly (not a flat
  tree-sitter query) to track enclosing namespace/type scope while
  extracting declarations/references. Lisp pattern-matches on head-symbol
  text since its grammar has no dialect-specific node types at all.
- `src/pipeline/{walk,parse,resolve}.c` — directory walk → per-file parse →
  two-pass resolution (declarations build a symbol table, then references
  get resolved against it via each adapter's `resolve_reference`).
- `src/graph/graph_json.c` — hand-rolled JSON writer, vendored
  `sheredom/json.h` for reading.
- `src/render/` — `main.c` (window/input loop), `layout3d.c` (force-directed
  layout + centering + gravity), `camera.c` (orbit camera + pan + zoom),
  `gl_scene.c` (VBO/VAO/shader plumbing, including the highlighted-edges
  second index buffer), `picking.c` (screen-space node picking + ray/plane
  drag), `labels.c` and `ui_panel.c` (Nuklear overlays).

## Gotchas actually hit during development — worth knowing before you rediscover them

- **EBO binding is part of VAO state**, unlike `GL_ARRAY_BUFFER` bindings.
  Binding the element buffer *after* unbinding the VAO attaches it to VAO 0
  instead — silently draws zero lines with no error. Fixed in `gl_scene.c`;
  if a *new* VAO/EBO pair ever gets added, bind the EBO while that VAO is
  still current, not later.
- **HiDPI/Retina**: `glViewport` needs `glfwGetFramebufferSize` (physical
  pixels), not `glfwGetWindowSize` (screen-coordinate units) — they differ by
  the DPI scale factor. Mouse picking math stays in window/screen coordinates
  though, since that's what `glfwGetCursorPos` reports.
- **Force-directed layout drift**: pure repulsion + attraction has no
  centering force, so the whole cluster's barycenter can wander arbitrarily
  far from the origin over many iterations, and — separately — a sparse
  graph (most node pairs aren't edge-connected) expands roughly unbounded
  without a mild gravity-toward-centroid term. Both are handled in
  `layout3d_compute` now; if layouts ever look "wrong" again (off-origin, or
  way more spread out than the node count would suggest), check those two
  things first.
- **Nuklear's `NK_INCLUDE_*` feature macros must match across every `.c` file
  that includes `nuklear.h`**, since they affect real struct layout, not just
  declarations. `main.c` is the one TU with `NK_IMPLEMENTATION` +
  `NK_GLFW_GL3_IMPLEMENTATION`; `ui_panel.c` and `labels.c` mirror the same
  `NK_INCLUDE_*` set without those two.
- **PowerShell doesn't auto-fail a step on a non-zero exit from an external
  `.exe`** called via `&` — it just sets `$LASTEXITCODE`. The Windows CI
  workflow checks this explicitly after every `codemap-build` invocation;
  don't remove those checks or the smoke test can never actually go red.
- Grammar versions are pinned in the top-level `CMakeLists.txt`
  (`FetchContent_Declare` `GIT_TAG`s) — bumping any of them is fine but
  should be a deliberate choice, not incidental.

## Testing conventions used throughout

Every non-trivial change in this project was verified concretely, not just
asserted — usually via a temporary debug `fprintf`/`glReadPixels` pixel-probe
added right before rebuilding, checked, then removed before committing. If
you add a rendering feature, that's the pattern: don't just claim it draws
correctly, sample the actual framebuffer for the expected colors. Screenshots
from the user were also used heavily to catch real regressions (the HiDPI bug
and the missing-edges bug were both things that looked fine in isolated
reasoning but were visibly wrong on screen).
