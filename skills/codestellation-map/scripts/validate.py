#!/usr/bin/env python3
"""Validate a Codestellation project folder (project.json + JSON Canvas files).

Usage: validate.py <project-dir> [--code-root DIR]

Walks every canvas reachable from project.json's root, mirroring the app's
rules (src/canvas/canvas_doc.c, src/render/canvas_view.c):
  - [[x.canvas]] in a text node, or a non-weak file node, nests a canvas
  - any other [[link]] that resolves to a directory opens the 3D view;
    links are relative to the canvas, '|alias' and '#frag' are stripped
  - a file node with subpath '#id' is a weak link to a box on another canvas

Exit status 1 if there are errors. Warnings don't fail.
"""
import json
import os
import re
import subprocess
import sys

LINK_RE = re.compile(r"\[\[(.*?)\]\]", re.S)
COLOR_RE = re.compile(r"^([1-6]|#[0-9a-fA-F]{6})$")
SIDES = {"top", "right", "bottom", "left"}
SKIP_DIRS = {"build", "dist", "out", "node_modules", "vendor", "runs", "state",
             "coverage", ".next", "target", "bin", "obj", "__pycache__"}

errors, warnings = [], []


def err(where, msg):
    errors.append(f"{where}: {msg}")


def warn(where, msg):
    warnings.append(f"{where}: {msg}")


def link_target(raw):
    return re.split(r"[|#]", raw, maxsplit=1)[0].strip()


def text_links(node):
    if node.get("type") != "text":
        return []
    return [link_target(m) for m in LINK_RE.findall(node.get("text") or "") if link_target(m)]


def is_weak(node):
    return node.get("type") == "file" and str(node.get("subpath") or "").startswith("#")


def load_canvas(path, where):
    try:
        with open(path) as f:
            doc = json.load(f)
    except FileNotFoundError:
        err(where, "canvas file does not exist")
        return None
    except json.JSONDecodeError as e:
        err(where, f"invalid JSON: {e}")
        return None
    if not isinstance(doc.get("nodes", []), list) or not isinstance(doc.get("edges", []), list):
        err(where, "'nodes' and 'edges' must be arrays")
        return None
    return doc


def rect(n):
    return (n["x"], n["y"], n["x"] + n["width"], n["y"] + n["height"])


def overlaps(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    proj_dir = os.path.abspath(args[0])
    code_root = os.path.dirname(proj_dir)
    if "--code-root" in argv:
        code_root = os.path.abspath(argv[argv.index("--code-root") + 1])

    pj_path = os.path.join(proj_dir, "project.json")
    try:
        with open(pj_path) as f:
            pj = json.load(f)
    except Exception as e:
        err("project.json", f"unreadable: {e}")
        return report({}, set(), proj_dir, code_root)
    for key in ("title", "root", "created"):
        if not pj.get(key):
            warn("project.json", f"missing '{key}'")
    root = os.path.normpath(os.path.join(proj_dir, pj.get("root") or "root.canvas"))

    # Breadth-first over canvas links, like canvas_index.c.
    queue, seen, docs, linked_dirs = [root], set(), {}, set()
    weak_checks = []
    while queue:
        cpath = queue.pop(0)
        if cpath in seen:
            continue
        seen.add(cpath)
        where = os.path.relpath(cpath, proj_dir)
        doc = load_canvas(cpath, where)
        if doc is None:
            continue
        docs[cpath] = doc
        cdir = os.path.dirname(cpath)
        nodes = doc.get("nodes", [])
        ids = {}
        for i, n in enumerate(nodes):
            nid = n.get("id")
            nw = f"{where} node {nid or '#' + str(i)}"
            if not nid:
                err(nw, "missing id")
            elif nid in ids:
                err(nw, "duplicate id")
            ids[nid] = n
            t = n.get("type")
            if t not in ("text", "file", "link", "group"):
                err(nw, f"unknown type {t!r}")
            for k in ("x", "y", "width", "height"):
                if not isinstance(n.get(k), (int, float)):
                    err(nw, f"'{k}' must be a number")
            if "color" in n and not COLOR_RE.match(str(n["color"])):
                err(nw, f"bad color {n['color']!r} (use '1'-'6' or #rrggbb)")
            if t == "text" and not (n.get("text") or "").strip():
                warn(nw, "empty text")

            canvas_links, dir_links = [], []
            for tgt in text_links(n):
                full = os.path.normpath(os.path.join(cdir, tgt))
                if tgt.lower().endswith(".canvas"):
                    canvas_links.append(full)
                elif os.path.isdir(full):
                    dir_links.append(full)
                elif os.path.exists(full):
                    err(nw, f"[[{tgt}]] is a file; only folders open in the 3D view")
                else:
                    err(nw, f"[[{tgt}]] does not exist ({full})")
            if t == "file" and n.get("file"):
                full = os.path.normpath(os.path.join(cdir, n["file"]))
                if is_weak(n):
                    weak_checks.append((nw, full, n["subpath"][1:]))
                elif n["file"].lower().endswith(".canvas"):
                    canvas_links.append(full)
            for c in canvas_links:
                if not os.path.exists(c):
                    err(nw, f"links to missing canvas {os.path.relpath(c, proj_dir)}")
                queue.append(c)
            linked_dirs.update(dir_links)
            if canvas_links and dir_links:
                warn(nw, "has both canvas and folder links (canvas wins on shift+click)")

        for i, e in enumerate(doc.get("edges", [])):
            ew = f"{where} edge {e.get('id') or '#' + str(i)}"
            if not e.get("id"):
                err(ew, "missing id")
            for end in ("fromNode", "toNode"):
                if e.get(end) not in ids:
                    err(ew, f"{end} {e.get(end)!r} is not a node on this canvas")
            for side in ("fromSide", "toSide"):
                if side in e and e[side] not in SIDES:
                    err(ew, f"bad {side} {e[side]!r}")
            if not e.get("label"):
                warn(ew, "no label (name the mechanism/protocol)")
        if len({e.get("id") for e in doc.get("edges", [])}) != len(doc.get("edges", [])):
            err(where, "duplicate edge ids")

        # Geometry: non-group boxes shouldn't overlap; groups shouldn't half-overlap.
        geo = [n for n in nodes if all(isinstance(n.get(k), (int, float)) for k in ("x", "y", "width", "height"))]
        boxes = [n for n in geo if n.get("type") != "group"]
        for a in range(len(boxes)):
            for b in range(a + 1, len(boxes)):
                if overlaps(rect(boxes[a]), rect(boxes[b])):
                    warn(where, f"boxes {boxes[a].get('id')} and {boxes[b].get('id')} overlap")
        groups = [n for n in geo if n.get("type") == "group"]
        for g in groups:
            gr = rect(g)
            for n in boxes:
                r = rect(n)
                inside = gr[0] <= r[0] and gr[1] <= r[1] and r[2] <= gr[2] and r[3] <= gr[3]
                if overlaps(gr, r) and not inside:
                    warn(where, f"box {n.get('id')} straddles group {g.get('id')} edge")

    for nw, target, tid in weak_checks:
        doc = docs.get(target) or load_canvas(target, nw)
        if doc is not None and tid not in {n.get("id") for n in doc.get("nodes", [])}:
            err(nw, f"weak link target #{tid} not found in {os.path.basename(target)}")

    for f in sorted(os.listdir(proj_dir)):
        full = os.path.join(proj_dir, f)
        if f.endswith(".canvas") and full not in seen:
            warn(f, "not reachable from the root canvas")

    return report(docs, linked_dirs, proj_dir, code_root)


def source_dirs(code_root):
    """Top-level folders holding tracked files (git if available)."""
    try:
        out = subprocess.run(["git", "-C", code_root, "ls-files"], capture_output=True,
                             text=True, check=True).stdout.split("\n")
        tops = {p.split("/")[0] for p in out if "/" in p}
    except Exception:
        tops = {d for d in os.listdir(code_root) if os.path.isdir(os.path.join(code_root, d))}
    return sorted(t for t in tops if not t.startswith(".") and t not in SKIP_DIRS)


def report(docs, linked_dirs, proj_dir, code_root):
    proj_top = os.path.relpath(proj_dir, code_root).split(os.sep)[0]
    for top in source_dirs(code_root):
        if top == proj_top:
            continue
        full = os.path.join(code_root, top)
        if not any(d == full or d.startswith(full + os.sep) or full.startswith(d + os.sep)
                   for d in linked_dirs):
            warn("coverage", f"no box links to {top}/ (or anything inside it)")

    print(f"Canvases reachable: {len(docs)}")
    for path, doc in docs.items():
        print(f"  {os.path.relpath(path, proj_dir)}: {len(doc.get('nodes', []))} nodes, "
              f"{len(doc.get('edges', []))} edges")
    print(f"Folders linked: {len(linked_dirs)}")
    for w in warnings:
        print(f"WARN  {w}")
    for e in errors:
        print(f"ERROR {e}")
    print("OK" if not errors else f"{len(errors)} error(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
