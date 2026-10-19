---
name: codestellation-map
description: Generate a starter Codestellation system map (project.json + nested JSON Canvas files) for a codebase, drawn with the C4 model - Level 1 system context, Level 2 containers, Level 3 components per container (whose boxes open the real code folders, which serve as Level 4), plus a deployment view when the same units run in several places. Invoke when asked to create, start, scaffold or bootstrap a Codestellation project / architecture map / system map / C4 diagram for a repo, or to fill an empty architecture/ folder that holds a project.json and root.canvas.
---

# Codestellation starter map (C4 model)

You are producing a **starter** system map that a human will open in
Codestellation, rearrange, and annotate. The map is a tree of JSON Canvas
files drawn with the [C4 model](https://c4model.com): each C4 level is its
own canvas, and you go one level deeper by shift+clicking a box. The top shows
who uses the system and what it deals with; the leaves are component boxes
that open the real source folders in Codestellation's 3D code view, which
serves as C4 Level 4 (code). Every box you write must be something you checked
in the code. Do not draw boxes for things you guess at.

`$ARGUMENTS` may name the target repo and/or the output folder. Defaults:
the repo is the current working directory, and the output is `<repo>/architecture/`.

## How Codestellation reads the files

Get these rules exactly right. The app silently ignores anything else.

- **`project.json`** (in the output folder):
  `{"title", "description", "root": "root.canvas", "created": "<ISO-8601 UTC>"}`.
  Keep any other keys already in it.
- **`.canvas` files** follow [JSON Canvas](https://jsoncanvas.org):
  `{"nodes": [...], "edges": [...]}`. Keys outside the spec are dropped
  when the app saves.
  - text node: `{"id","type":"text","text":"<markdown>","x","y","width","height","color"?}`
  - group node: `{"id","type":"group","label","x","y","width","height","color"?}`.
    A group contains the boxes whose **centre** lies inside it. Groups are
    drawn behind boxes, so list them first. Groups may nest (a group whose
    centre lies inside another group).
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
  Point it at the box's **defining** canvas, never at another weak link.
  Several weak links may point at the same box (useful for container
  instances in the deployment view).
- Markdown on box faces supports headings, **bold**, *italic*, `code`,
  bullets and links. When the box is zoomed out, only the **first line** shows,
  so make the first line `## Name`.
- *Export Manual* turns every box into a section of a Markdown manual,
  and *Export LLM Brief* does the same as context for an LLM. Box text is
  therefore documentation: write it for a reader who has never seen the repo.

## Workflow

### 1. Check the output folder

- If `project.json` exists, keep its `title`, `created` and any extra keys.
  Fill in an empty `description`.
- If `root.canvas` (or any canvas reachable from it) already has nodes,
  **stop and ask** before overwriting. Offer to write the C4 set alongside
  it instead: prefix the new files `c4-` (`c4-context.canvas`,
  `c4-containers.canvas`, …) and link `c4-context.canvas` from one new
  "C4 view" box on the existing root.
- Otherwise create the folder and files. Put all canvases **flat in the
  output folder** so every folder link has the same prefix
  (`../<path>` when the output is `<repo>/architecture/`).

**Never move a box that already exists.** Once a canvas exists, assume the
human has arranged it, and a readable layout is the most expensive part to
get back. When changing an existing map:

- Patch the JSON in place (load, change, save). Never regenerate a canvas
  that already exists from a script, because that resets every position.
- Change text, colour, links and edges freely. Keep every existing node's
  `x`, `y` and `width`; leave `height` alone too unless the new text no
  longer fits, and then grow it downward only (keep `y`).
- If growing a box or adding one would overlap something, don't shuffle
  the neighbours. Put new boxes in clear space at the edge of the canvas
  (to the right, or below everything) and tell the user where they went so
  they can place them.
- Groups follow the same rule: never resize or move a group to fit
  something; place the new box outside and say so.
- A validator overlap warning caused by the human's own arrangement is
  theirs to resolve. Report it, don't fix it by moving boxes.

The layout rules in step 5 are only for boxes you create.

### 2. Survey the repo (evidence first)

Read, in roughly this order, and note file:line evidence as you go:

1. README, CLAUDE.md, PLAN/ARCHITECTURE/design docs, `doc(s)/`.
2. Build and run entry points: Makefile, CMakeLists, package.json scripts,
   go.mod, Cargo.toml, pyproject, `*.sln`/`*.csproj`, `main.*`, `cmd/`, `bin/`.
3. Deployment and runtime: Dockerfile, docker-compose, k8s/helm,
   terraform, Procfile, CI workflows, `.env*` / config files (ports, hosts,
   DSNs, API base URLs, buckets, queues), flash/run/serve targets, emulators.
4. Directory tree: `git ls-files | sed 's|/[^/]*$||' | sort | uniq -c`
   shows where the code mass is.
5. Boundaries in code: network listeners and clients (HTTP routes, gRPC,
   sockets, WebSockets), DB drivers and migrations, file stores, message
   buses, IPC, FFI, WASM, subprocesses, vendored or copied-in code.

Classify what you find. The C4 kind goes on the box in brackets:

| C4 element | What counts | Colour |
|---|---|---|
| `[Person]` | a human role that drives the system (user, operator, researcher, admin) | `"6"` purple |
| `[Software System]` | *this* repo as a whole: one box at Level 1, the boundary **group** at Level 2 | `"5"` cyan (box), group uncoloured |
| `[Container]` | a separately runnable or deployable unit: a binary, firmware image, service, web app, CLI, worker, browser bundle, notebook set | `"5"` cyan |
| `[Component]` | a cohesive module inside a container, usually one or a few folders | `"4"` green |
| data store | database, file-backed index, model weights, state directory, bucket, queue, removable media | `"2"` orange |
| tooling | tests, benchmarks, scripts, docs or notebooks that matter to how the system is built or studied | `"1"` red |
| note / legend | the legend and title boxes, and any explanatory note that isn't part of the model | `"3"` yellow |
| external | a person-facing or software system outside the repo, or a deployment node | no colour |

Sort external things by **when** the system deals with them, because C4
puts them on different canvases:

- **Run time** (it calls them, they call it, users reach it through them):
  Level 1 external systems.
- **Build time** (code copied in, vendored libraries, toolchains, code
  generators): not on Level 1. Name the copied-in code in the Level 3
  component that holds it, and the toolchain in a tooling box.
- **Where it runs** (hardware boards, emulators, browsers, hosts, clusters):
  deployment nodes in the deployment view, not Level 1 systems.

### 3. One canvas per C4 level

| Canvas | C4 level | Holds |
|---|---|---|
| `root.canvas` | **1, System context** | legend; people; the system as **one** cyan box (`Inside: [[containers.canvas]]`); run-time external systems around it; a box linking the deployment view if there is one |
| `containers.canvas` | **2, Containers** | title/legend; the system boundary **group** holding containers, data stores and tooling; Level 1 neighbours as weak links outside the group |
| `components-<container>.canvas` | **3, Components** | title/legend; the container as a boundary group holding its components; Level 2 neighbours as weak links outside it |
| *(folder links)* | **4, Code** | not a canvas: component boxes open their folders in the 3D view |
| `deployment.canvas` | **Deployment** (supplementary) | deployment nodes as groups, container instances inside them as weak links |

Decide the depth per box:

- Give a container a Level 3 canvas when it has about **3 or more**
  distinct components (separate folders, separate responsibilities). If it
  is a single folder or one cohesive module, it is a leaf: put its folder
  links on the Level 2 box.
- Draw **one component canvas per container**, even when containers share
  code. Two containers built from one source tree usually have different
  main data paths, and that difference is what the diagram should show.
  C4 has no notation for shared code, so say "shares `kernel/` + `lib/`
  with …" in the box text.
- A big component that is one flat folder stays a leaf; describe its parts
  in `Key:`. Never go below Level 3.
- Add `deployment.canvas` when the same container runs in **more than one
  place** (boards and emulators, browser and server, several instances wired
  together, staging and production). Skip it for a single, obvious runtime.
- Nested canvases may reuse an existing canvas instead of duplicating one.

At every level, the boxes with no canvas link (the leaves) that hold **code**
must carry folder links. Aim for every substantial source folder to be
reachable from some leaf. People, external systems, deployment nodes and
data-only stores (weights, text packs, runtime state) get no link, because
the 3D view would show nothing for them. Name their paths in backticks
instead. Generated, output and build folders (`build/`, `dist/`, `runs/`,
`node_modules/`, `tools/` and similar) are treated the same way: mention
them, don't link them.

### 4. Write the boxes

Box text template (keep it to about 6 lines; the canvas is a map, not a
manual):

```markdown
## <Name>
*[<C4 kind>: <technology>]*
<One or two sentences: what it does and why it exists.>
Key: `entry.c`, `thing.h`
Code: [[../dir]] [[../other/dir]]          <- leaves only
Inside: [[name.canvas]]                    <- non-leaves only
```

- Use the repo's own names for things, from its README, directory names
  and identifiers.
- `Key:` lists the most important files by name, since files can't be linked.
- **Edges read caller → callee** (initiator → provider) and are labelled
  ***what [how]***: the action as a verb phrase, then the mechanism or
  protocol in brackets. Examples: `Uses the shell [UART0 serial, 115200]`,
  `Reads/writes files [SDMMC, FAT16]`, `Hosts the files [HTTPS]`,
  `Starts tasks [sched_add]`. In the deployment view, short mechanism-only
  labels (`SDMMC`, `serves [HTTPS]`) are fine.
- On each nested canvas, add the neighbours from the level above as
  **weak links** and draw the edges to them, so the sub-diagram shows its
  context.
- Every canvas starts with a **legend/title box** (`id: "legend"` on the
  root, `id: "title"` elsewhere, colour `"3"` yellow) at the top-left: `# C4 Level N: <name>`,
  one or two sentences on what this level shows, then the notation key
  exactly as below. On the root, also say "shift+click to go in / open
  code" and that the map was generated as a starter on `<date>` and should
  be checked.

```markdown
**C4 notation:**
- `[Person]` purple
- `[Software System]` / `[Container]` cyan
- `[Component]` green
- data orange
- tooling red
- notes + legends yellow
- plain = external
- Arrows read *caller → callee*, labelled *what [how]*
```

Deployment view specifics:

- Each **deployment node** is a group labelled `<Name> [<technology>]`
  (a board, a host OS, a browser, an emulator, a cluster). Nest execution
  environments inside their host (an emulator group inside the
  "Developer Mac" group).
- Each **container instance** is a weak link to the container's box on
  `containers.canvas`, placed inside the node it runs on. Draw two instances
  when two copies run and talk to each other.
- Infrastructure that is not a container (boot partitions, host tools,
  static hosts) is an uncoloured box marked `*[Infrastructure]*` or
  `*[Deployment node: …]*`.
- Mark anything planned but not built as `[planned]` in its label.

### 5. Lay it out

For boxes you create (never existing ones, see step 1), use a grid so the
first view is readable without dragging:

- Box: width `300`, height `160`. Add 40 to the height for each extra two
  lines of text. Gap: `80` horizontal, `80` vertical. Legend/title boxes are
  about `380`–`420` wide and `240`–`280` tall (the notation key takes room).
  Weak links: `300 × 120`.
- **Level 1:** legend top-left at about `(-460, -460)`; people in a row at
  `y = -420`; the system box below them (wider, about `680 × 220`); run-time
  external systems to the right and left; the deployment link box under the
  legend.
- **Levels 2 and 3:** title top-left; weak-linked neighbours along the top or
  outer edges; the boundary group in the middle with its boxes in a grid
  (40 px padding, plus 40 px at the top for the label), ordered so the main
  flow reads left to right or top to bottom.
- **Deployment:** node groups side by side, nested groups inset by 40 px,
  instances in a column inside each node.
- Size each group to its children plus padding, and don't let sibling
  groups partly overlap.
- Keep edges short. Put tightly-coupled boxes next to each other, and set
  `fromSide`/`toSide` when the default route would cross a box.
- IDs: readable kebab-case with a prefix by kind (`person-`, `system-`,
  `ext-`, `ctr-`, `store-`, `tool-`, `c-` for components, `w-` for weak
  links, `g-` for groups, `d-` for deployment instances). Edge IDs are
  `e-<from>-<to>`. IDs must be unique within each canvas.

Generating the canvases from a small script (in a scratch directory, not the
repo) is less error-prone than hand-writing JSON: helpers for `box`, `group`,
`weak` and `edge`, then one `save()` per canvas.

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

- the canvas tree, as an indented list with box counts per canvas, naming
  each canvas's C4 level
- what each Level 1 and Level 2 element is based on (one line each, citing
  a file)
- what the C4 levels made visible (for example: which dependencies turned
  out to be build-time only, where containers share code, what the
  deployment view shows)
- anything you were **unsure** about, merged or left out, so they can check
  it first
- that they should open `<output-dir>/project.json` in Codestellation
  (`codemap-view <output-dir>/project.json`), or use Properties > Open Project

Don't launch the app or a browser yourself.
