# Codestellation

![screen shot](./doc/screen.png)

Walks a codebase, extracts cross-file dependencies with tree-sitter, and
renders them as an interactive 3D graph in native OpenGL + Nuklear.
Built for digging through unfamiliar/legacy multi-language codebases.

One app, `codemap-view`: Properties > Open picks a directory, walks it
(recursively, mixed languages in one tree are fine), parses each file with
the matching language adapter, builds a dependency graph in the background,
and loads it into the 3D view -- no restart. The graph is cached as
`graph.json` in a per-project folder under
`~/Library/Application Support/Codestellation/`.

Currently supported languages: **C** (`.c`/`.h`, `#include`-based),
**C#** (`.cs`, namespace/type declarations + `using`-qualified
references), **Python** (`.py`/`.pyi`, `import` / `from ... import`,
including relative imports), **Go** (`.go`, `import` paths resolved to
packages by directory), **Lisp** (`.lisp`/`.lsp`/`.cl` -- a Common Lisp
stand-in; see the header comment in `src/lang/lisp/lisp_adapter.c`
before trusting it on a real codebase, the actual dialect wasn't
confirmed when this was written).

Python and Go have no in-source module identity -- `pkg.sub.mod` /
`example.com/m/pkg` come from where a file sits relative to a source
root (or `go.mod`) the adapter never sees -- so their dependency
resolution is a documented best-guess (path-suffix matching, plus
on-disk lookup for Python relative imports). See the header comments in
`src/lang/python/python_adapter.c` and `src/lang/go/go_adapter.c`.

## Build

```
cmake -S . -B build
cmake --build build
```

Fetches tree-sitter + per-language grammars and GLFW via CMake
FetchContent on first configure (the C# grammar's generated parser is
large, so that step takes a bit). Verified on macOS; Windows/Linux
should work (a GL loader and the directory-walk code both have
non-Apple branches) but hasn't been run on real hardware in this
environment -- see the Phase 7 commit message for specifics.

### Xcode project

```
cmake -G Xcode -B build-xcode -S .
open build-xcode/Codestellation.xcodeproj
```

`build-xcode/` is gitignored. Xcode is a multi-config generator, so
don't pass `-DCMAKE_BUILD_TYPE` -- pick the configuration in Xcode, or
build from the CLI with `cmake --build build-xcode --config Debug`.
Editing `CMakeLists.txt` re-runs CMake automatically on the next build
via the generated `ZERO_CHECK` target.

This produces an ad-hoc-signed `Codestellation.app` that runs straight
from Xcode's Run button. Hardened runtime is deliberately off here, since
it refuses the `DYLD_*` injection Xcode's debugger relies on.

For a Developer-ID-signed, hardened-runtime `.app` to hand to another
Mac:

```
cmake -G Xcode -B build-xcode -S . -DCODESTELLATION_DIST=ON
cmake --build build-xcode --config Release
```

Don't Run that build from Xcode -- launch the built `.app` directly
(Finder or `open`), where no `DYLD_*` injection happens.

## Run

```
./build/src/codemap-view              # then Properties > Open...
./build/src/codemap-view graph.json   # or open a previously built graph
```

In the viewer: left-drag empty space to orbit, scroll to zoom, click a
node to inspect its file in the right-hand panel, drag a node to
reposition it (it stays put -- layout is computed once at load, not a
continuous simulation). Esc to quit.

Optional sanity check of a built graph (needs networkx), pointed at the
cached `graph.json` in the project's Application Support folder:
```
python3 scripts/sanity_check.py graph.json
```

## graph.json schema

networkx "node-link" format:

```json
{
  "directed": true,
  "graph": {},
  "nodes": [{"id": 0, "path": "/abs/path/foo.c", "language": "c"}],
  "links": [{"source": 0, "target": 1, "kind": "import"}]
}
```

Load in Python: `nx.node_link_graph(json.load(f), edges="links")`
(older networkx: drop the `edges` kwarg).

## Adding a language

Every language plugs into `src/lang/adapter.h`'s `LanguageAdapter`
struct -- the pipeline (`src/pipeline/*.c`) never references a specific
language by name, only through that interface. To add one: pick a
tree-sitter grammar, fetch it in the top-level `CMakeLists.txt`
(mirroring the existing `ts_c`/`ts_csharp`/`ts_lisp`/`ts_python`/`ts_go`
blocks), add the adapter `.c` and its `tree_sitter_*` library to
`src/CMakeLists.txt`'s `Codestellation` target, register it in
`src/lang/registry.c`, and write an adapter implementing
`extract_declarations`/`extract_references`/`resolve_reference`.
`src/lang/c/c_adapter.c` is the simplest example (no symbol table,
path-based deps, flat query); `src/lang/csharp/csharp_adapter.c` shows
scoped namespace/type resolution by walking the parse tree directly
rather than a flat query; `src/lang/python/python_adapter.c` and
`src/lang/go/go_adapter.c` show path-derived module identity matched by
suffix (Python also resolves relative imports against the filesystem);
`src/lang/lisp/lisp_adapter.c` shows adapting to a grammar with no
dialect-specific node types at all.
