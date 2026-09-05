#!/usr/bin/env python3
"""Loads a codemap3d graph.json via networkx as a quick sanity check.
Not part of the tool itself -- codemap3d's core pipeline is C; this is
just a convenience for offline analysis of its output.
"""
import json
import sys


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <graph.json>", file=sys.stderr)
        sys.exit(1)

    import networkx as nx

    with open(sys.argv[1]) as f:
        data = json.load(f)

    try:
        g = nx.node_link_graph(data, edges="links")  # networkx >= 3.x
    except TypeError:
        g = nx.node_link_graph(data)  # older networkx

    print(f"{g.number_of_nodes()} nodes, {g.number_of_edges()} edges")
    for n, d in g.nodes(data=True):
        print(f"  node {n}: {d.get('path')} ({d.get('language')})")
    for u, v, d in g.edges(data=True):
        print(f"  edge {u} -> {v} ({d.get('kind')})")


if __name__ == "__main__":
    main()
