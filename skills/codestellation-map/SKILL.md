---
name: codestellation-map
description: Generate a starter Codestellation system map (project.json + nested JSON Canvas files) for a codebase - C4-style boxes for people, the system, its containers, external systems and data stores, drilled down into components whose boxes link to the real code folders. Invoke when asked to create, start, scaffold or bootstrap a Codestellation project / architecture map / system map / C4 diagram for a repo, or to fill an empty architecture/ folder that holds a project.json and root.canvas.
---

# Codestellation starter map

You are producing a **starter** system map that a human will open in
Codestellation, rearrange, and annotate. The map is a tree of JSON Canvas
files. Its top shows who uses the system and what it talks to; the leaves are
boxes that open the real source folders in Codestellation's 3D code view.
Every box you write must be something you checked in the code. Do not draw
boxes for things you guess at.

`$ARGUMENTS` may name the target repo and/or the output folder. Defaults:
the repo is the current working directory, and the output is `<repo>/architecture/`.

## How Codestellation reads the files

Get these rules exactly right. The app silently ignores anything else.

- **`project.json`** (in the output folder):
  `{"title", "description", "root": "root.canvas", "created": "<ISO-8601 UTC>"}`.
- **`.canvas` files** follow [JSON Canvas](https://jsoncanvas.org):
  `{"nodes": [...], "edges": [...]}`. Keys outside the spec are dropped
  when the app saves.
  - text node: `{"id","type":"text","text":"<markdown>","x","y","width","height","color"?}`
  - group node: `{"id","type":"group","label","x","y","width","height","color"?}`.
    A group contains the boxes whose **centre** lies inside it. Groups are
    drawn behind boxes, so list them first.
  - edge: `{"id","fromNode","toNode","label"?,"fromSide"?,"toSide"?}`, with sides `top|right|bottom|left`.
  - colours: `"1"` red, `"2"` orange, `"3"` yellow, `"4"` green, `"5"` cyan, `"6"` purple, or `"#rrggbb"`.
- **`[[links]]` in a box's markdown decide what shift+click does.**
  - `[[name.canvas]]` goes *into* that canvas (one level down).
  - `[[../some/dir]]` opens that folder in the 3D view. A box with several
    folder links opens all of them as one merged graph. Only
    **directories** work: a link to a file does nothing.
  - Paths are relative to the `.canvas` file. `[[path|Label]]` and
    `[[path#x]]` are allowed, and the app strips the `|`/`#` part.
  - A box should have **either** a canvas link **or** folder links, never
    both. If a box has both, the canvas link wins.
- **Weak link** (refers to a box on another canvas, drawn dashed, can take
  edges): `{"id","type":"file","file":"other.canvas","subpath":"#<node-id>","x","y","width","height"}`.
  Use one when a component on one canvas talks to something defined on another.
- Markdown on box faces supports headings, **bold**, *italic*, `code`,
  bullets and links. When the box is zoomed out, only the **first line** shows,
  so make the first line `## Name`.
- *Export Manual* turns every box into a section of a Markdown manual,
  and *Export LLM Brief* does the same as context for an LLM. Box text is
  therefore documentation: write it for a reader who has never seen the repo.

## Workflow

### 1. Check the output folder

- If `project.json` exists, keep its `title` and `created`. Fill in an
  empty `description`.
- If `root.canvas` (or any canvas reachable from it) already has nodes,
  **stop and ask** before overwriting. Offer to write alongside it instead,
  for example into a fresh `generated.canvas` linked from a new box on the root.
- Otherwise create the folder and files. Put all canvases **flat in the
  output folder** so every folder link has the same prefix
  (`../<path>` when the output is `<repo>/architecture/`).

### 2. Survey the repo (evidence first)

Read, in roughly this order, and note file:line evidence as you go:

1. README, CLAUDE.md, PLAN/ARCHITECTURE/design docs, `doc(s)/`.
2. Build and run entry points: Makefile, CMakeLists, package.json scripts,
   go.mod, Cargo.toml, pyproject, `*.sln`/`*.csproj`, `main.*`, `cmd/`, `bin/`.
3. Deployment and runtime: Dockerfile, docker-compose, k8s/helm,
   terraform, Procfile, CI workflows, `.env*` / config files (ports, hosts,
   DSNs, API base URLs, buckets, queues).
4. Directory tree: `git ls-files | sed 's|/[^/]*$||' | sort | uniq -c`
   shows where the code mass is.
5. Boundaries in code: network listeners and clients (HTTP routes, gRPC,
   sockets, WebSockets), DB drivers and migrations, file stores, message
   buses, IPC, FFI, WASM, subprocesses, vendored or copied-in code.

Classify what you find:

| C4 element | What counts | Colour |
|---|---|---|
| Person | a human role that drives the system (user, operator, researcher, admin) | `"6"` purple |
| Software system | *this* repo as a whole. It becomes the boundary **group** | group, no colour |
| Container | a separately runnable or deployable unit: a binary, service, web app, CLI, worker, browser bundle, notebook set | `"5"` cyan |
| Data store | database, file-backed index, model weights, state directory, bucket, queue | `"2"` orange |
| External system | something outside the repo that it calls, is called by, or **copies code from** | no colour |
| Component | a cohesive module inside a container, usually one or a few folders | `"4"` green |
| Tooling | tests, benchmarks, scripts, docs or notebooks that matter to how the system is built or studied | `"3"` yellow |

Red (`"1"`) is left for the human to mark problems with. Don't use it.

### 3. Decide the depth, per box

Go **as deep as the code warrants**, up to 3 levels, and decide separately
for each box:

- **Level 0, `root.canvas` (context + containers):** a legend/intro box,
  people along the top, the system boundary group in the middle holding
  its containers and data stores, and external systems around the outside.
- **Level 1, `<container>.canvas` (components):** give a container its
  own canvas when it has about **3 or more** distinct components (separate
  folders, separate responsibilities). If it is a single folder or one
  cohesive module, put the folder link straight on the root box instead.
- **Level 2, `<container>-<component>.canvas`:** go one level deeper only
  when a component has several **subfolders** with clear roles (such as a
  vendored engine with `collision/`, `solver/` and so on). A big component
  that is one flat folder stays a leaf: folders are the smallest unit a box
  can open, so a sub-canvas would repeat the same link. Describe its
  internal parts in the box text (`Key:`) instead. Never go deeper than this.

At every level, the boxes with no canvas link (the leaves) that hold **code**
must carry folder links. Aim for every substantial source folder to be
reachable from some leaf. People, external systems and data-only stores
(weights, text packs, runtime state) get no link, because the 3D view would
show nothing for them. Name their paths in backticks instead. Generated,
output and build folders (`build/`, `dist/`, `runs/`, `node_modules/` and
similar) are treated the same way: mention them, don't link them.

### 4. Write the boxes

Box text template (keep it to about 6 lines; the canvas is a map, not a
manual):

```markdown
## <Name>
*<C4 kind>: <technology>*
<One or two sentences: what it does and why it exists.>
Key: `entry.c`, `thing.h`
Code: [[../dir]] [[../other/dir]]          <- leaves only
Inside: [[name.canvas]]                    <- non-leaves only
```

- Use the repo's own names for things, from its README, directory names
  and identifiers.
- `Key:` lists the most important files by name, since files can't be linked.
- Every edge gets a **label** naming the mechanism, with the protocol or
  data format where it matters: `bus messages (s-expr)`, `HTTPS JSON`,
  `reads/writes`, `#include`, `copied by make sync-alan`, `WASM build`. An
  arrow points from the caller or initiator to the callee or provider.
- On each nested canvas, add the neighbours the container talks to as
  **weak links** to their boxes on the parent canvas, and draw the edges to
  them. That way the sub-diagram shows its context.
- Put one small **legend/intro box** (`id: "legend"`) at the top-left of
  `root.canvas`. It gives the project's one-sentence purpose, the colour key,
  "shift+click to go in / open code", and states that the map was generated
  as a starter on `<date>` and should be checked.

### 5. Lay it out

Use a grid so the first view is readable without dragging:

- Box: width `300`, height `160`. Add 40 to the height for each extra two
  lines of text. Gap: `80` horizontal, `80` vertical.
- Root canvas rows, top to bottom: legend + people at `y = -400`; the system
  group starting at `y = -160` with containers in rows inside it (40 px
  padding, plus 40 px at the top for the label); external systems in a row
  below the group or a column to its right.
- Size each group to its children plus padding, and don't let groups
  partly overlap.
- Nested canvases: a title box at the top-left saying what this level is
  (`# <Container>, components`), weak-linked neighbours on the left or
  right edge, and components in a grid in the middle, ordered so the main
  data flow reads left to right or top to bottom.
- Keep edges short. Put tightly-coupled boxes next to each other, and set
  `fromSide`/`toSide` when the default route would cross a box.
- IDs: readable kebab-case with a prefix by kind (`person-`, `ctr-`,
  `store-`, `ext-`, `cmp-`, `tool-`). Edge IDs are `e-<from>-<to>`. IDs must
  be unique within each canvas.

### 6. Validate, then report

Run the validator. It is next to this file, so resolve its real path:

```bash
python3 "$(dirname "$(readlink -f ~/.claude/skills/codestellation-map/SKILL.md)")/scripts/validate.py" <output-dir>
```

It parses every canvas reachable from the root and fails on bad JSON,
duplicate IDs, dangling edges, links to canvases or folders that don't
exist, file links that the app would ignore, and broken weak links. It warns
about overlapping boxes, mixed canvas+folder boxes, unreachable canvases and
top-level source folders that no box links to. Fix every error. Fix the
warnings or explain why you left them.

Then tell the user:

- the canvas tree, as an indented list with box counts per canvas
- what each top-level element is based on (one line each, citing a file)
- anything you were **unsure** about or left out, so they can check it first
- that they should open `<output-dir>/project.json` in Codestellation
  (`codemap-view <output-dir>/project.json`), or use Properties > Open Project

Don't launch the app or a browser yourself.
