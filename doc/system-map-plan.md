# System map: nested canvases above the 3D explorer

Design plan agreed on 2026-09-27. Nothing here is implemented yet. It is
written so a fresh session can pick up the work cold.

## Goal

Turn Codestellation from a single-codebase explorer into a top-to-tail
mapping tool for a whole system. Today the user maps servers, databases and
networks in Obsidian Canvas, but Obsidian can't "zoom in", so everything
ends up organised outside the canvas and gets messy.

What we want:

- An Obsidian-Canvas-style 2D board of hand-drawn boxes with lines between
  them (servers, databases, regions, anything).
- Double-clicking a box **goes into it**: the view is replaced by another
  canvas describing that box. Nesting depth is unlimited (e.g. region →
  network → server → services).
- When a box refers to source directories, going into it opens the
  **existing 3D explorer** on those folders. One box can map to many
  folders.
- Markdown notes everywhere: on canvas boxes, and (as today) on files and
  lines in the 3D view.
- Finding things matters more than exporting them: search across all
  canvases.
- Later, not v1: generate an overall manual or an LLM briefing by walking
  the whole hierarchy. Keep the data export-friendly so this stays cheap.

Kinopio and Obsidian Canvas are the nearest references for the feel. IcePanel,
Structurizr, LikeC4, Ilograph and Heptabase were looked at and judged too
specific.

## Decisions

### Stay in this project (native C / OpenGL / Nuklear)

We considered a web rewrite (Tauri/Electron + canvas lib + three.js) and
rejected it. The 3D explorer is the payoff and the user likes it as is.
Most canvas needs are easy here (pan/zoom, drag, edges, breadcrumbs, search
panel). The known weak spots, and how v1 handles each:

| Weak spot | v1 approach |
|---|---|
| Nuklear has no rich text | Render a small markdown subset on box faces: headings, bold/italic, bullets, links. |
| Inline text editing on a zoomable canvas is painful | Edit the selected box's markdown in the right-hand panel (reuse the existing note editor, `src/render/note_compose.c`), not inline. |
| Nuklear bakes fonts at a fixed size, so text blurs when zoomed | 2–3 baked font sizes chosen by zoom level; below a threshold show only the box's first line/title. |

The zoom text is the real risk. That's why step 3 is a spike to check it.

### One binary, no CLI

`codemap-build` is being folded into the viewer. The viewer already has
native file dialogs on macOS, Linux and Windows, so a separate builder isn't
needed. **No headless mode**: the CLI goes away entirely. The pipeline
(`src/pipeline/`: walk → parse → resolve → write JSON) is about 50 lines of
driver in `build_graph_cli.c`, frees everything it allocates and never calls
`exit()`, so it can be called in-process.

`scripts/sanity_check.py` stays; point it at a cached `graph.json` from
Application Support.

### Canvas file format: JSON Canvas

Use the open JSON Canvas spec (https://jsoncanvas.org, from the Obsidian
team) for every non-directory level: one `.canvas` file per level.

- Nodes: `text` (markdown), `file` (path + optional `subpath`), `link`
  (URL), `group`. Each has `id`, `x`, `y`, `width`, `height` and optional
  `color` (preset `"1"`–`"6"` or hex). `color` gives border colours.
- Edges: `id`, `fromNode`, `toNode`, optional `fromSide`/`toSide`,
  `fromEnd`/`toEnd`, `color`, `label`.

Boxes don't change shape, have no types and no structured fields in v1.
A box is a markdown text node, optionally coloured.

### One box type; links decide behaviour

There is no separate "link box". Every box is a markdown `text` node, and
`[[...]]` links in its text determine what it can do:

- `[[eu-west.canvas]]`: a link to another canvas file. Clicking the link
  (or double-clicking the box) goes into that canvas.
- `[[../services/api]]`: a link to a directory. Clicking it opens the 3D
  explorer on that folder.
- Several directory links in one box: double-clicking the box opens **all**
  of them as one merged 3D graph (one box → many folders). The existing
  per-directory colouring (`src/render/dircolor.c`) keeps them visually
  distinct.
- Box has both canvas and directory links: double-click goes into the
  canvas; a small "code" affordance on the box opens the folders.
- Paths are relative to the `.canvas` file containing them, so a project
  survives being cloned elsewhere. (Absolute paths also accepted.)

The one convention added on top of the spec: "a link that resolves to a
directory means open the 3D explorer."

### Project file

There's a project file at the top (`project.json` or simply the root
`.canvas`; decide during step 4). Code lives in many folders spread across
the disk, but the canvas hierarchy lives in one place.

### Breadcrumb bar

Always visible: `World › EU-West › api-01 › [code]`. Click any crumb to jump
up; Backspace/Esc goes up one level. It works across the canvas → 3D
boundary, so leaving the 3D view returns to the canvas you came from.

### Cross-canvas references: search + weak links

Boxes on different canvases can't be connected directly (e.g. a server in
EU-West talking to a database in US-East). Solution:

- A **search panel** that searches the text of every box on every canvas
  (and later, code notes too).
- From a search result you can drop a **weak link** on the current canvas:
  a read-only, dashed box showing the target's current text. Edges can
  connect to it like any box. Clicking it jumps to the original box on its
  own canvas.
- Encoding: a JSON Canvas `file` node with `file: "eu-west.canvas"` and
  `subpath: "#<node-id>"`. This is standard JSON Canvas, no extension needed.

### Colours

Pull all colours into one shared palette (a theme struct) before building
the canvas, so the canvas uses it from day one. The current dark theme is
hard to read. Tuning it is a later polish pass, which becomes palette-only
changes once colours live in one place.

## Implementation order

### 1. In-process graph switching (removes the relaunch hack)

Currently opening a directory (`src/render/project_launcher.c`) blocks on a
spawned `codemap-build`, then `posix_spawn`s (Win: `_spawnv`) a fresh
`codemap-view` and exits the current process. That would destroy canvas
state and breadcrumbs, so it has to go first.

- Link the pipeline into the viewer; delete the `codemap-build` target, the
  sibling-binary lookup and the Xcode bundle embedding of it
  (`src/CMakeLists.txt`). Delete `build_graph_cli.c`'s `main`, keeping a
  `build_graph(root, out_path)`-style function.
- Run the build on a background thread so the window keeps drawing; show a
  "Building…" overlay. Needs a tiny thread wrapper: pthreads on Mac/Linux,
  Win32 threads on Windows. macOS has no C11 `<threads.h>`.
- When the build finishes, the main thread tears down the current graph (GL
  buffers, layout, labels, notes, overlay, picking state) and loads the new
  one.
- Keep the Application Support per-project cache (`graph.json`, notes,
  overlay) so reopening is instant.
- Remove the relaunch-specific workarounds (the `DYLD_*`/debugger notes
  in README and code comments).
- Update README (build/run sections, two-binaries description) and NOTES.md.

It's useful on its own: opening a project stops relaunching the window.

### 2. Palette extraction

Move every hard-coded colour into one theme struct. No visual change is
required. Readability fixes can happen here if convenient, otherwise later.

### 3. Canvas spike (1–2 days, risk check)

In the existing app: a pannable, zoomable 2D grid background with a few
hard-coded markdown boxes and edges. Judge text quality across zoom levels
with the baked-sizes approach. If text can't be made acceptable, revisit the
native-vs-web decision before investing further.

**Result (2026-09-27): passed, so we stay native.** The user judged the
canvas "looks great". What the spike established, reusable in step 4:

- Fonts: Inter (regular/bold/italic) + JetBrains Mono, vendored in
  `vendor/fonts/` (OFL) and compiled into the binary
  (`cmake/embed_file.cmake`). `src/render/fonts.c` bakes each at 10/14/20/28/40
  logical px × framebuffer scale, for crisp text on Retina. Any other size is
  the next baked size scaled via a per-frame pool (`fonts_sized`). The atlas
  is 1024×4096 at 2×. Panels now use Inter 14, the file preview uses Mono 13
  (the user finds the preview a bit small; bump `MONO_UI_SIZE` sometime).
- `src/render/md_render.c`: markdown subset with word wrap, drawn straight
  into a Nuklear command buffer.
- `src/render/canvas_view.c`: grid, zoom around the cursor, pan, drag,
  bezier edges with arrowheads and labels, title-only below 7px body text.
  Hard-coded sample boxes. Toggled by Properties > "Canvas view (spike)".
  Step 4 replaces the sample data and the toggle.

### 4. Canvas v1

- Load/save JSON Canvas `.canvas` files.
- Create, move and resize text boxes; set border colour; delete.
- Draw edges between boxes, with optional labels.
- Select a box → edit its markdown in the right-hand panel.
- Render the markdown subset on box faces.
- `[[links]]`: canvas links go in, directory links open the 3D explorer
  (merged for multiple), relative path resolution.
- Breadcrumb bar across canvas and 3D levels.
- Grid background.

**Status (2026-09-27): canvas parts done, directory links (4c) remaining.**
Decisions made along the way:

- There is a `project.json` after all (`src/canvas/project.c`): title,
  description, root canvas, created date. It's there for export titles and
  metadata later.
- Gestures: **shift+click** goes into a box (Obsidian uses double-click for
  editing, so double-click edits here too). Double-click on empty space adds
  a box. Side dots draw edges, the bottom-right corner resizes,
  Delete/Backspace removes the selection. Esc closes the editor, then clears
  the selection, then goes up a level. Esc no longer quits while a project
  is open.
- Only boxes with a `[[x.canvas]]` link (or a file node pointing at a
  `.canvas`) can be gone into; a missing target is created on first visit.
  Auto-creating a canvas for a box without a link was offered and declined.
- Canvas I/O is in `src/canvas/canvas_doc.c`: keys the spec doesn't define
  are dropped on save, writes are atomic (temp file + rename), and a file
  that fails to parse is shown empty and never overwritten.
- `vendor/nuklear/nuklear_glfw_gl3.h` is patched so Cmd works as the
  shortcut key on macOS (copy/paste in the editor).
- Known gaps: no undo; the editor doesn't word-wrap (a Nuklear limitation).

**4c, next:** directory `[[links]]` open the 3D explorer, with several
folders merged into one graph (`walk_project` over each root into one
FileList, cached under a hash of the sorted folder set), and a `[code]`
breadcrumb to get back to the canvas.

### 5. Search + weak links

Global search panel over all canvases; drop weak-link nodes from results;
click a weak link to jump to its original box.

### 6. Later

- Colour/readability polish (dark mode is hard to see now).
- Manual export: depth-first walk producing a document (canvas = section,
  box = subsection with its note, edges as "A → B (label)", code leaves with
  file notes + generated summary: languages, file count, most-depended-on
  files).
- LLM briefing: same walk, terser template.
- Include code notes in global search.

## Open questions

- Root of a project: a `project.json` pointing at the root `.canvas`, or the
  root `.canvas` itself? (Decide in step 4.)
- Where the per-box code graph caches live when a box maps to several
  folders (Application Support keyed by the sorted folder set is the likely
  answer).
- Whether to also allow existing JSON Canvas `file` nodes (not just `[[links]]`
  in text) to point at directories or canvases. It's cheap to support and
  improves Obsidian compatibility.
