#!/usr/bin/env python3
"""Renders the Codestellation app icon.

A dark rounded square (the macOS icon shape, with Apple's standard margin
and a soft shadow) holding a faint canvas grid and a small constellation:
stars in the canvas box preset colours, joined by edges, with one star
framed by a canvas box -- the "box you go into".

Writes resources/macos/Codestellation.png (1024x1024) and, on macOS,
resources/macos/Codestellation.icns via sips + iconutil. Needs numpy.

    python3 scripts/make_icon.py
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

import numpy as np

SIZE = 1024
SS = 2                      # supersampling factor for anti-aliasing
N = SIZE * SS

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "resources", "macos")


def hex_rgb(h):
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5)])


# Pixel-centre coordinates in 1024-space, sampled at SS x SS per pixel.
ys, xs = np.mgrid[0:N, 0:N].astype(np.float32)
X = (xs + 0.5) / SS
Y = (ys + 0.5) / SS

rgb = np.zeros((N, N, 3), np.float32)
alpha = np.zeros((N, N), np.float32)


def over(color, a):
    """Composites a solid colour with per-pixel alpha `a` over the canvas."""
    global rgb, alpha
    a = np.clip(a, 0.0, 1.0)
    c = color if np.ndim(color) == 3 else np.broadcast_to(color, rgb.shape)
    out_a = a + alpha * (1.0 - a)
    safe = np.where(out_a > 0, out_a, 1.0)
    rgb = (c * a[..., None] + rgb * (alpha * (1.0 - a))[..., None]) / safe[..., None]
    alpha = out_a


def coverage(d):
    """Signed distance (1024-space px, negative inside) -> AA coverage."""
    return np.clip(0.5 - d * SS, 0.0, 1.0)


def round_rect_sdf(cx, cy, hw, hh, r):
    qx = np.abs(X - cx) - (hw - r)
    qy = np.abs(Y - cy) - (hh - r)
    outside = np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qy, 0) ** 2)
    inside = np.minimum(np.maximum(qx, qy), 0)
    return outside + inside - r


def segment_dist(ax, ay, bx, by):
    px, py = X - ax, Y - ay
    dx, dy = bx - ax, by - ay
    t = np.clip((px * dx + py * dy) / (dx * dx + dy * dy), 0.0, 1.0)
    return np.sqrt((px - t * dx) ** 2 + (py - t * dy) ** 2)


# --- the icon tile: Apple's grid puts an 824px shape in a 1024px canvas --
C = SIZE / 2
HALF = 412.0
RADIUS = 185.0

# Soft drop shadow under the tile.
shadow_d = round_rect_sdf(C, C + 10, HALF - 6, HALF - 6, RADIUS)
over(np.zeros(3), 0.28 * np.clip(1.0 - shadow_d / 22.0, 0.0, 1.0) ** 2 * (shadow_d > -40))

tile_d = round_rect_sdf(C, C, HALF, HALF, RADIUS)
tile = coverage(tile_d)

# Background: vertical gradient plus a cool glow behind the constellation.
t = np.clip((Y - (C - HALF)) / (2 * HALF), 0, 1)[..., None]
bg = hex_rgb("#222A3A") * (1 - t) + hex_rgb("#0E1117") * t
glow = np.exp(-((X - 520) ** 2 + (Y - 470) ** 2) / (2 * 260.0 ** 2))[..., None]
bg = bg + hex_rgb("#2F4A72") * glow * 0.45
over(bg, tile)

# Faint canvas grid, clipped to the tile.
g = 64.0
gx = np.abs(((X - C) % g) - g / 2)
gy = np.abs(((Y - C) % g) - g / 2)
grid_line = np.maximum(coverage(np.abs(gx - g / 2) - 0.9), coverage(np.abs(gy - g / 2) - 0.9))
over(np.ones(3), grid_line * tile * 0.05)

# --- the constellation -----------------------------------------------------
stars = {                      # x, y, radius, colour (canvas box presets)
    "a": (300, 350, 34, "#4FC1C9"),
    "b": (530, 270, 40, "#F2F4F7"),
    "c": (735, 385, 32, "#A77BE0"),
    "d": (445, 560, 36, "#57C26A"),
    "e": (670, 640, 30, "#E8914A"),
    "f": (315, 735, 28, "#6CB8FF"),
    "g": (755, 790, 30, "#E3C454"),
}
edges = ["ab", "bc", "ad", "bd", "de", "ce", "df", "eg"]

edge_color = hex_rgb("#8FB4E0")
for e in edges:
    (ax, ay, _, _), (bx, by, _, _) = stars[e[0]], stars[e[1]]
    d = segment_dist(ax, ay, bx, by)
    over(edge_color, coverage(d - 5.0) * 0.55 * tile)

for x, y, r, col in stars.values():
    c = hex_rgb(col)
    dist = np.sqrt((X - x) ** 2 + (Y - y) ** 2)
    over(c, np.exp(-(dist / (r * 1.9)) ** 2) * 0.55 * tile)        # glow
    over(c, coverage(dist - r) * tile)                              # body
    over(np.ones(3), coverage(dist - r * 0.42) * 0.85 * tile)       # hot core

# The canvas box around the brightest star.
bx, by, _, _ = stars["b"]
box_d = round_rect_sdf(bx, by, 92, 78, 18)
over(np.ones(3), coverage(np.abs(box_d) - 4.5) * 0.9 * tile)

# --- write out ---------------------------------------------------------------
def downsample(a):
    return a.reshape(SIZE, SS, SIZE, SS, *a.shape[2:]).mean(axis=(1, 3))


# Premultiply before averaging so edges don't pick up dark fringes.
pm = downsample(rgb * alpha[..., None])
a = downsample(alpha)
color = np.where(a[..., None] > 0, pm / np.maximum(a[..., None], 1e-6), 0)
img = np.dstack([np.clip(color, 0, 1), np.clip(a, 0, 1)])
img8 = (img * 255.0 + 0.5).astype(np.uint8)


def write_png(path, pixels):
    h, w, _ = pixels.shape
    raw = b"".join(b"\x00" + pixels[row].tobytes() for row in range(h))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


os.makedirs(OUT_DIR, exist_ok=True)
png_path = os.path.join(OUT_DIR, "Codestellation.png")
write_png(png_path, img8)
print("wrote", os.path.normpath(png_path))

if sys.platform == "darwin" and shutil.which("iconutil") and shutil.which("sips"):
    with tempfile.TemporaryDirectory() as tmp:
        iconset = os.path.join(tmp, "Codestellation.iconset")
        os.mkdir(iconset)
        for base in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                px = base * scale
                name = f"icon_{base}x{base}{'@2x' if scale == 2 else ''}.png"
                subprocess.run(["sips", "-z", str(px), str(px), png_path, "--out", os.path.join(iconset, name)],
                               check=True, stdout=subprocess.DEVNULL)
        icns_path = os.path.join(OUT_DIR, "Codestellation.icns")
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o", icns_path], check=True)
        print("wrote", os.path.normpath(icns_path))
