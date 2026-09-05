# codemap3d

Walks a legacy codebase, extracts cross-file dependencies with tree-sitter,
and (eventually) renders them as an interactive 3D graph in native
OpenGL + Nuklear. See `/Users/robrohan/.claude/plans/splendid-skipping-cray.md`
for the full design.

Phase 1 (current): `codemap-build`, a headless CLI that walks a directory,
parses every file with the matching language adapter, and writes a
dependency graph to `graph.json`. No GL/window code yet.

## Build

```
cmake -S . -B build
cmake --build build
```

## Run

```
./build/src/codemap-build --root test/fixtures/c --out graph.json
python3 scripts/sanity_check.py graph.json   # optional, needs networkx
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

Every language plugs into `src/lang/adapter.h`'s `LanguageAdapter` struct —
the pipeline (`src/pipeline/*.c`) never references a specific language by
name. See `src/lang/c/c_adapter.c` for the simplest possible adapter
(path-based deps, no symbol table) and the plan doc for the C# adapter
(namespace/type declarations + `using`-qualified references) and the
Lisp adapter (package/require-based, grammar currently a Common-Lisp
stand-in pending confirmation of the actual dialect in use).
