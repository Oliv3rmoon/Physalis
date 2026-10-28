#!/usr/bin/env python3
# Nightshade TV - Draw the home-screen pictures: the icon, the background and the launch picture.
# Copyright (C) 2026 Oliv3rmoon
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw Nightshade TV's home-screen pictures in Nightshade's style.

usage: python3 tools/render-home-art.py   (needs Pillow and NumPy)

Nightshade's tools/render-home-art.py (Oliv3rmoon/Nightshade), with a television added: the same
nightshade flower (five red petals round a pale cone), black berries and dark leaves, on near
black with a red glow and faint stars, and Nightshade's palette (its "nightly" theme).

Writes, in sce_sys/ and in opengl-ui/ps5/sce_sys/ (the copies the released app is built with):
  icon0.png                     512x512: a black set lit red, the flower on its screen and two
                                berries on its rods
  background-source.png         3840x2160: an aerial at the right, the nightshade climbing it
  launch-background-source.png  3840x2160: a screen-shaped ring of red light round the flower
  pic0.dds, pic1.dds            those two as BC7 (the format, size and header the console's
                                pictures were in, tools/validate-assets.sh)
Everything is procedural; the same seed gives the same files.
"""
import math
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parent.parent
targets = [root / 'sce_sys', root / 'opengl-ui/ps5/sce_sys']

# Nightshade's palette: base, accent_deep, accent, accent_pale; the leaves' near black.
BASE = (0x09, 0x09, 0x0b)
DEEP = (0x5a, 0x14, 0x19)
RED = (0xe5, 0x48, 0x4d)
PALE = (0xf3, 0xa9, 0xad)
LEAF = (0x17, 0x08, 0x0b)
# The set and the aerial: Nightshade's glass and panel edge.
SHELL = (0x14, 0x14, 0x17)
EDGE = (0x2b, 0x2b, 0x31)


def rgb(color):
    return np.array(color, np.float32) / 255.0


def canvas(width, height):
    image = np.empty((height, width, 3), np.float32)
    image[:] = rgb(BASE)
    return image


def glow(image, cx, cy, radius, color, strength, ring=None, width=None):
    """Adds a soft round light; with ring, a ring of light of that radius instead."""
    height, width_px = image.shape[:2]
    y, x = np.ogrid[:height, :width_px]
    d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2, dtype=np.float32)
    if ring is None:
        falloff = np.exp(-(d / radius) ** 2)
    else:
        falloff = np.exp(-((d - ring) / width) ** 2)
    image += falloff[..., None] * rgb(color) * strength


def vignette(image, amount):
    height, width = image.shape[:2]
    y, x = np.ogrid[:height, :width]
    d = np.sqrt(((x - width / 2) / (width / 2)) ** 2 + ((y - height / 2) / (height / 2)) ** 2)
    image *= (1.0 - amount * np.clip(d - 0.55, 0, None) ** 1.5)[..., None]


def stars(image, count, seed, avoid=None):
    rng = np.random.default_rng(seed)
    height, width = image.shape[:2]
    for _ in range(count):
        x, y = rng.uniform(0, width), rng.uniform(0, height * 0.8)
        if avoid and math.hypot(x - avoid[0], y - avoid[1]) < avoid[2]:
            continue
        size = rng.uniform(0.6, 1.6) * width / 3840
        light = rng.uniform(0.15, 0.6) ** 2
        tint = PALE if rng.random() < 0.3 else (0xff, 0xf4, 0xf4)
        r = int(size * 4) + 2
        x0, y0 = int(x) - r, int(y) - r
        if x0 < 0 or y0 < 0 or x0 + 2 * r >= width or y0 + 2 * r >= height:
            continue
        yy, xx = np.mgrid[y0:y0 + 2 * r, x0:x0 + 2 * r]
        dot = np.exp(-(((xx - x) ** 2 + (yy - y) ** 2) / (size * size)))
        image[y0:y0 + 2 * r, x0:x0 + 2 * r] += dot[..., None].astype(np.float32) * rgb(tint) * light


def leaf_points(cx, cy, length, wide, angle, steps=64, bend=0.0):
    """A pointed leaf or petal from (cx, cy) along angle (radians): its outline."""
    left, right = [], []
    for i in range(steps + 1):
        t = i / steps
        w = wide * (math.sin(math.pi * t) ** 0.85) * (1 - t) ** 0.25
        off = bend * length * t * t
        left.append((t * length, w + off))
        right.append((t * length, -w + off))
    outline = left + right[::-1]
    c, s = math.cos(angle), math.sin(angle)
    return [(cx + px * c - py * s, cy + px * s + py * c) for px, py in outline]


def shape_mask(points, box, scale=4):
    """The anti-aliased coverage of a polygon inside box (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    big = Image.new('L', (w * scale, h * scale), 0)
    ImageDraw.Draw(big).polygon([((px - x0) * scale, (py - y0) * scale) for px, py in points], fill=255)
    return np.asarray(big.resize((w, h), Image.LANCZOS), np.float32) / 255.0


def stroke_mask(points, thickness, box, scale=4):
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    big = Image.new('L', (w * scale, h * scale), 0)
    ImageDraw.Draw(big).line([((px - x0) * scale, (py - y0) * scale) for px, py in points], fill=255,
                             width=max(1, int(thickness * scale)), joint='curve')
    return np.asarray(big.resize((w, h), Image.LANCZOS), np.float32) / 255.0


def bounds(image, points, pad=4):
    height, width = image.shape[:2]
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    return (max(0, int(min(xs)) - pad), max(0, int(min(ys)) - pad),
            min(width, int(max(xs)) + pad), min(height, int(max(ys)) + pad))


def put(image, box, mask, color):
    """Covers box with color (an RGB triple or an array the size of box) by mask."""
    x0, y0, x1, y1 = box
    region = image[y0:y1, x0:x1]
    fill = color if isinstance(color, np.ndarray) and color.ndim == 3 else rgb(color)
    region[:] = region * (1 - mask[..., None]) + fill * mask[..., None]


def leaf(image, cx, cy, length, wide, angle, bend=0.0, rim=0.35):
    points = leaf_points(cx, cy, length, wide, angle, bend=bend)
    box = bounds(image, points, pad=int(length * 0.05) + 4)
    mask = shape_mask(points, box)
    # Red light along one edge: the leaf less a copy shifted away from the light.
    shift = max(2, int(length * 0.012))
    moved = np.zeros_like(mask)
    moved[shift:, shift:] = mask[:-shift, :-shift]
    edge = np.clip(mask - moved, 0, 1)
    put(image, box, mask, LEAF)
    image[box[1]:box[3], box[0]:box[2]] += edge[..., None] * rgb(RED) * rim
    rib = [(cx + math.cos(angle) * length * t, cy + math.sin(angle) * length * t + bend * length * t * t * math.cos(angle))
           for t in np.linspace(0.02, 0.85, 24)]
    put(image, box, stroke_mask(rib, max(1.0, length * 0.006), box) * 0.5, DEEP)


def flower(image, cx, cy, size, rotation):
    """Five red petals round a pale cone, with a red light behind."""
    glow(image, cx, cy, size * 1.2, RED, 0.22)
    for k in range(5):
        angle = rotation + k * 2 * math.pi / 5
        points = leaf_points(cx, cy, size, size * 0.30, angle)
        box = bounds(image, points)
        mask = shape_mask(points, box)
        yy, xx = np.mgrid[box[1]:box[3], box[0]:box[2]]
        t = np.clip(np.hypot(xx - cx, yy - cy) / size, 0, 1)[..., None].astype(np.float32)
        color = rgb(DEEP) * (1 - t) + rgb(RED) * t
        color = color + (rgb(PALE) - color) * np.clip((t - 0.78) * 2.5, 0, 1) * 0.35
        put(image, box, mask, color)
        rib = [(cx + math.cos(angle) * size * s, cy + math.sin(angle) * size * s) for s in np.linspace(0.18, 0.8, 16)]
        put(image, box, stroke_mask(rib, max(1.0, size * 0.018), box) * 0.55, DEEP)
    # The cone: a pale star of short points, and its tip.
    cone = []
    for k in range(10):
        a = rotation + math.pi / 5 + k * math.pi / 5
        r = size * (0.26 if k % 2 == 0 else 0.11)
        cone.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    box = bounds(image, cone)
    yy, xx = np.mgrid[box[1]:box[3], box[0]:box[2]]
    t = np.clip(np.hypot(xx - cx, yy - cy) / (size * 0.26), 0, 1)[..., None].astype(np.float32)
    put(image, box, shape_mask(cone, box), rgb((0xff, 0xf1, 0xe6)) * (1 - t) + rgb(PALE) * t)
    glow(image, cx, cy, size * 0.08, (0xff, 0xff, 0xff), 0.5)


def berry(image, cx, cy, radius):
    """A glossy black berry lit red from the upper left, with a white highlight."""
    x0, y0 = int(cx - radius) - 2, int(cy - radius) - 2
    x1, y1 = int(cx + radius) + 3, int(cy + radius) + 3
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    nx, ny = (xx - cx) / radius, (yy - cy) / radius
    d = np.sqrt(nx * nx + ny * ny)
    mask = np.clip((1 - d) * radius * 0.7, 0, 1)
    nz = np.sqrt(np.clip(1 - d * d, 0, 1))
    light = np.clip(-0.55 * nx - 0.55 * ny + 0.62 * nz, 0, 1)
    rim = np.clip(d - 0.72, 0, 1) / 0.28 * np.clip(nx + ny, 0, 1)
    color = (rgb((0x0c, 0x0b, 0x0e))[None, None] + light[..., None] ** 3 * rgb(DEEP) * 0.9
             + rim[..., None] * rgb(RED) * 0.55)
    spec = np.exp(-(((nx + 0.38) ** 2 + (ny + 0.4) ** 2) / 0.012))
    color = color + spec[..., None] * 0.85
    put(image, (x0, y0, x1, y1), mask, color)


def stem(image, points, thickness):
    box = bounds(image, points, pad=int(thickness) + 4)
    put(image, box, stroke_mask(points, thickness, box), (0x2a, 0x0b, 0x10))


def curve(a, b, c, steps=40):
    return [((1 - t) ** 2 * a[0] + 2 * (1 - t) * t * b[0] + t * t * c[0],
             (1 - t) ** 2 * a[1] + 2 * (1 - t) * t * b[1] + t * t * c[1]) for t in np.linspace(0, 1, steps)]


def berries(image, x, y, r):
    for dx, dy, s in ((0, 0, 1.0), (1.55, 0.55, 0.86), (0.55, 1.6, 0.8)):
        berry(image, x + dx * r, y + dy * r, r * s)



# ---- Nightshade TV's own: the set, the aerial, the screen's light -----------------------------


def fill(box, top, bottom):
    """A colour for every pixel of box, from top to bottom."""
    x0, y0, x1, y1 = box
    t = np.linspace(0, 1, y1 - y0, dtype=np.float32)[:, None, None]
    return np.broadcast_to(rgb(top) * (1 - t) + rgb(bottom) * t, (y1 - y0, x1 - x0, 3)).copy()


def rounded_mask(box, radius, scale=4):
    """The anti-aliased coverage of a rounded rectangle filling box (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    big = Image.new('L', (w * scale, h * scale), 0)
    ImageDraw.Draw(big).rounded_rectangle((0, 0, w * scale - 1, h * scale - 1), int(radius * scale), fill=255)
    return np.asarray(big.resize((w, h), Image.LANCZOS), np.float32) / 255.0


def rim(image, box, mask, light, strength, shift):
    """Light along a shape's upper-left edges: the shape less a copy shifted away from the light."""
    moved = np.zeros_like(mask)
    moved[shift:, shift:] = mask[:-shift, :-shift]
    edge = np.clip(mask - moved, 0, 1)
    image[box[1]:box[3], box[0]:box[2]] += edge[..., None] * rgb(light) * strength


def rod(image, points, thickness, light=0.55):
    """A dark metal rod lit red along its upper-left side: a red copy a little up and to the left,
    and the rod over it."""
    off = max(1.0, thickness * 0.3)
    lit = [(x - off, y - off) for x, y in points]
    box = bounds(image, points + lit, pad=int(thickness) + 4)
    put(image, box, stroke_mask(lit, thickness, box) * light, RED)
    put(image, box, stroke_mask(points, thickness, box), SHELL)


def line(a, b, steps=24):
    return [(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t) for t in np.linspace(0, 1, steps)]


def screen_light(image, cx, cy, half_w, half_h, radius, strength=1.0):
    """A ring of red light the shape of a screen round a dark inside, as Nightshade's eclipse."""
    height, width = image.shape[:2]
    y, x = np.ogrid[:height, :width]
    qx = np.abs(x - cx) - (half_w - radius)
    qy = np.abs(y - cy) - (half_h - radius)
    d = (np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - radius)
    d = d.astype(np.float32)
    image += np.exp(-(d / (height * 0.012)) ** 2)[..., None] * rgb(RED) * 0.75 * strength
    image += np.exp(-(d / (height * 0.06)) ** 2)[..., None] * rgb(RED) * 0.25 * strength
    inside = np.clip(-d / 3, 0, 1)[..., None]
    image[:] = image * (1 - inside * 0.92) + rgb(BASE) * inside * 0.92


def tv_set(image, cx, cy, width, height):
    """A black set lit red from its screen, on two feet, with a berry on each of its two rods.
    Returns its screen's box and corner radius."""
    x0, y0, x1, y1 = int(cx - width / 2), int(cy - height / 2), int(cx + width / 2), int(cy + height / 2)
    top = (cx, y0 + height * 0.05)
    for end in ((cx - width * 0.30, y0 - height * 0.40), (cx + width * 0.25, y0 - height * 0.47)):
        rod(image, line(top, end), width * 0.016)
    for side in (-1, 1):
        foot = (cx + side * width * 0.30, y1 - height * 0.02)
        rod(image, line(foot, (foot[0] + side * width * 0.05, y1 + height * 0.11)), width * 0.024)
    for end in ((cx - width * 0.30, y0 - height * 0.40), (cx + width * 0.25, y0 - height * 0.47)):
        berry(image, end[0], end[1], width * 0.042)
    box = (x0, y0, x1, y1)
    shell = rounded_mask(box, height * 0.16)
    put(image, box, shell, fill(box, (0x22, 0x22, 0x28), SHELL))
    rim(image, box, shell, RED, 0.6, max(2, int(width * 0.006)))
    bezel = height * 0.11
    sx0, sy0, sx1, sy1 = int(x0 + bezel), int(y0 + bezel), int(x1 - bezel), int(y1 - bezel)
    screen_box = (sx0, sy0, sx1, sy1)
    corner = (sy1 - sy0) * 0.13
    screen = rounded_mask(screen_box, corner)
    # The screen: a deep red light from its middle, as the flower on it gives.
    yy, xx = np.mgrid[sy0:sy1, sx0:sx1].astype(np.float32)
    t = np.clip(np.hypot((xx - cx) / ((sx1 - sx0) / 2), (yy - (sy0 + sy1) / 2) / ((sy1 - sy0) / 2)) / 1.25, 0, 1)
    glass = rgb(DEEP)[None, None] * (1 - t[..., None]) * 0.9 + rgb((0x05, 0x05, 0x06))[None, None] * t[..., None]
    put(image, screen_box, screen, glass)
    return screen_box, corner


def glass_light(image, box, corner):
    """Light from above on a screen's glass, fading before its middle, and its hairline."""
    x0, y0, x1, y1 = box
    mask = rounded_mask(box, corner)
    t = np.linspace(1, 0, y1 - y0, dtype=np.float32)[:, None]
    light = np.clip((t - 0.54) / 0.46, 0, 1) * 0.10
    image[y0:y1, x0:x1] += (mask * light)[..., None]
    inner = rounded_mask((x0 + 2, y0 + 2, x1 - 2, y1 - 2), max(1, corner - 2))
    hair = np.zeros_like(mask)
    hair[:] = mask
    hair[2:-2, 2:-2] -= inner
    image[y0:y1, x0:x1] += np.clip(hair, 0, 1)[..., None] * 0.16


def aerial(image, base_x, base_y, top_y, boom_from, boom_to, elements):
    """A television aerial: a mast on a foot, a boom across its top, and elements along the boom
    shorter towards its front, all dark metal lit red."""
    w = image.shape[1]
    foot = [(base_x - w * 0.016, base_y), (base_x + w * 0.016, base_y)]
    rod(image, foot, w * 0.012)
    rod(image, line((base_x, base_y), (base_x, top_y)), w * 0.0055)
    rod(image, line(boom_from, boom_to), w * 0.0042)
    for i in range(elements):
        t = 0.06 + 0.88 * i / (elements - 1)
        px = boom_from[0] + (boom_to[0] - boom_from[0]) * t
        py = boom_from[1] + (boom_to[1] - boom_from[1]) * t
        half = w * (0.034 - 0.016 * t)
        rod(image, line((px - half * 0.42, py - half), (px + half * 0.42, py + half)), w * 0.0028, light=0.7)


def vine(image, x, y0, y1, reach, turns, thickness):
    """The nightshade's stem climbing a mast from y0 up to y1, wound round it."""
    points = []
    for t in np.linspace(0, 1, 160):
        points.append((x + reach * math.sin(t * turns * 2 * math.pi), y0 + (y1 - y0) * t))
    stem(image, points, thickness)
    return points


def icon(n=512):
    image = canvas(n, n)
    glow(image, n * 0.5, n * 0.55, n * 0.46, DEEP, 0.9)
    glow(image, n * 0.5, n * 0.58, n * 0.22, RED, 0.14)
    screen, corner = tv_set(image, n * 0.5, n * 0.585, n * 0.77, n * 0.53)
    cx, cy = (screen[0] + screen[2]) / 2, (screen[1] + screen[3]) / 2
    stem(image, curve((cx, cy), (cx + n * 0.05, cy + n * 0.1), (cx + n * 0.12, cy + n * 0.115)), n * 0.008)
    flower(image, cx - n * 0.012, cy - n * 0.008, n * 0.165, -math.pi / 2)
    berries(image, cx + n * 0.12, cy + n * 0.1, n * 0.026)
    glass_light(image, screen, corner)
    vignette(image, 0.6)
    return image


def background(w=3840, h=2160):
    image = canvas(w, h)
    glow(image, w * 0.74, h * 0.5, w * 0.34, DEEP, 0.85)
    glow(image, w * 0.74, h * 0.5, w * 0.12, RED, 0.12)
    glow(image, w * 0.05, h * 1.05, w * 0.3, DEEP, 0.45)
    stars(image, 900, 1009, avoid=(w * 0.74, h * 0.48, w * 0.12))
    for x, y, length, angle, bend in ((w * 1.02, h * 0.08, w * 0.24, 2.55, 0.10), (w * 1.03, h * 0.62, w * 0.2, 3.35, -0.12),
                                      (w * 0.94, h * 1.06, w * 0.22, 3.9, 0.08), (w * 0.6, h * 1.08, w * 0.17, 4.5, -0.1),
                                      (-w * 0.02, h * 1.04, w * 0.2, 5.6, 0.1)):
        leaf(image, x, y, length, length * 0.24, angle, bend=bend)
    # The aerial at the right, and the nightshade climbing it to flower beside its boom.
    mast_x, foot_y, top_y = w * 0.80, h * 0.86, h * 0.34
    aerial(image, mast_x, foot_y, top_y, (w * 0.665, h * 0.385), (w * 0.93, h * 0.305), 8)
    points = vine(image, mast_x, foot_y - h * 0.02, h * 0.42, w * 0.009, 3.2, w * 0.0032)
    for i, (t, angle, side) in enumerate(((0.18, 3.6, -1), (0.42, 5.9, 1), (0.66, 3.3, -1))):
        px, py = points[int(t * (len(points) - 1))]
        leaf(image, px, py, w * 0.05, w * 0.05 * 0.32, angle, bend=0.15 * side, rim=0.45)
    stem(image, curve((mast_x, h * 0.44), (w * 0.77, h * 0.47), (w * 0.735, h * 0.47)), w * 0.003)
    flower(image, w * 0.725, h * 0.47, w * 0.085, -math.pi / 2 - 0.25)
    stem(image, curve((w * 0.86, h * 0.325), (w * 0.875, h * 0.36), (w * 0.872, h * 0.405)), w * 0.0025)
    berries(image, w * 0.865, h * 0.405, w * 0.017)
    flower(image, w * 0.905, h * 0.235, w * 0.04, -math.pi / 2 + 0.4)
    vignette(image, 0.55)
    return image


def launch(w=3840, h=2160):
    image = canvas(w, h)
    cx, cy = w * 0.5, h * 0.53
    glow(image, cx, cy, h * 0.55, DEEP, 0.75)
    stars(image, 1100, 1010, avoid=(cx, cy, h * 0.46))
    # A screen in the dark: its light a ring the shape of a set's glass, round the flower, as
    # Nightshade's eclipse is round its flower. Two rods on top, a berry on each.
    half_w, half_h = h * 0.36, h * 0.26
    top = (cx, cy - half_h - h * 0.01)
    ends = ((cx - h * 0.17, cy - half_h - h * 0.14), (cx + h * 0.13, cy - half_h - h * 0.17))
    for end in ends:
        rod(image, line(top, end), h * 0.007)
    screen_light(image, cx, cy, half_w, half_h, h * 0.07)
    for end in ends:
        berry(image, end[0], end[1], h * 0.022)
    for x, y, length, angle, bend in ((-w * 0.02, h * 0.02, w * 0.26, 0.55, 0.08), (-w * 0.03, h * 0.7, w * 0.23, -0.2, -0.1),
                                      (w * 0.1, h * 1.08, w * 0.2, -1.0, 0.1), (w * 1.02, h * 0.04, w * 0.26, 2.6, -0.08),
                                      (w * 1.03, h * 0.72, w * 0.23, 3.35, 0.1), (w * 0.9, h * 1.08, w * 0.2, 4.15, -0.1)):
        leaf(image, x, y, length, length * 0.24, angle, bend=bend)
    stem(image, curve((cx, cy), (cx + w * 0.03, cy + h * 0.15), (cx + w * 0.065, cy + h * 0.17)), w * 0.004)
    flower(image, cx, cy - h * 0.01, h * 0.17, -math.pi / 2)
    berries(image, cx + w * 0.065, cy + h * 0.165, h * 0.033)
    vignette(image, 0.6)
    return image


def to_image(image, mode='RGB'):
    # A little noise keeps the dark glows from showing steps (the same noise every time).
    noise = np.random.default_rng(7).uniform(-0.75, 0.75, image.shape[:2])[..., None]
    data = np.clip(np.clip(image, 0, 1) * 255 + 0.5 + noise, 0, 255).astype(np.uint8)
    picture = Image.fromarray(data)
    return picture.convert(mode)


def bc7(picture):
    """BC7 blocks of an RGBA picture, every block in mode 6 (one pair of endpoints, 16 steps)."""
    pixels = np.asarray(picture.convert('RGBA'), np.float32)
    h, w = pixels.shape[:2]
    blocks = pixels.reshape(h // 4, 4, w // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4)
    weights = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64], np.int32)
    out = np.empty((blocks.shape[0], 16), np.uint8)
    for start in range(0, blocks.shape[0], 32768):
        b = blocks[start:start + 32768]
        mean = b.mean(1, keepdims=True)
        centred = b - mean
        cov = np.einsum('npi,npj->nij', centred, centred)
        axis = np.ones((b.shape[0], 4), np.float32)
        for _ in range(8):
            axis = np.einsum('nij,nj->ni', cov, axis)
            axis /= np.maximum(np.linalg.norm(axis, axis=1, keepdims=True), 1e-9)
        proj = np.einsum('npi,ni->np', centred, axis)
        e0 = np.clip(mean[:, 0] + proj.min(1, keepdims=True) * axis, 0, 255)
        e1 = np.clip(mean[:, 0] + proj.max(1, keepdims=True) * axis, 0, 255)
        # 7 bits and a p-bit of 1 per endpoint: an endpoint is 2q + 1, so opaque stays 255.
        q0 = np.clip(np.round((e0 - 1) / 2), 0, 127).astype(np.int32)
        q1 = np.clip(np.round((e1 - 1) / 2), 0, 127).astype(np.int32)
        v0, v1 = q0 * 2 + 1, q1 * 2 + 1
        palette = ((64 - weights)[None, :, None] * v0[:, None, :] + weights[None, :, None] * v1[:, None, :] + 32) >> 6
        dist = ((b[:, :, None, :] - palette[:, None, :, :].astype(np.float32)) ** 2).sum(-1)
        index = dist.argmin(-1).astype(np.int32)
        # The first pixel's index has 3 bits: when it would need 4, the endpoints change places.
        flip = index[:, 0] >= 8
        q0[flip], q1[flip] = q1[flip].copy(), q0[flip].copy()
        index[flip] = 15 - index[flip]
        lo = np.zeros(b.shape[0], np.uint64)
        hi = np.zeros(b.shape[0], np.uint64)
        position = 0

        def field(value, bits):
            nonlocal position
            value = value.astype(np.uint64) & np.uint64((1 << bits) - 1)
            if position >= 64:
                hi[:] |= value << np.uint64(position - 64)
            elif position + bits <= 64:
                lo[:] |= value << np.uint64(position)
            else:
                low = 64 - position
                lo[:] |= (value & np.uint64((1 << low) - 1)) << np.uint64(position)
                hi[:] |= value >> np.uint64(low)
            position += bits

        field(np.full(b.shape[0], 1 << 6), 7)
        for channel in range(4):
            field(q0[:, channel], 7)
            field(q1[:, channel], 7)
        field(np.ones(b.shape[0]), 1)
        field(np.ones(b.shape[0]), 1)
        field(index[:, 0], 3)
        for pixel in range(1, 16):
            field(index[:, pixel], 4)
        assert position == 128
        out[start:start + 32768, :8] = lo.view(np.uint8).reshape(-1, 8)
        out[start:start + 32768, 8:] = hi.view(np.uint8).reshape(-1, 8)
    return out.tobytes()


def dds(picture, alpha_mode):
    """A DDS file with a DX10 header: BC7_UNORM, one level, as the console's pictures were."""
    w, h = picture.size
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000  # caps, size, format, levels, linear size
    header = struct.pack('<4s7I44x', b'DDS ', 124, flags, h, w, w * h, 1, 1)
    header += struct.pack('<2I4s5I', 32, 0x4, b'DX10', 0, 0, 0, 0, 0)  # pixel format: FourCC
    header += struct.pack('<5I', 0x1000, 0, 0, 0, 0)  # caps: texture
    header += struct.pack('<5I', 98, 3, 0, 1, alpha_mode)  # BC7_UNORM, 2D, one, alpha mode
    assert len(header) == 148
    return header + bc7(picture)


def main():
    preview = len(sys.argv) > 1 and sys.argv[1] == '--preview'
    if preview:
        # Small copies to look at, in the folder given (nothing in the repository changes).
        out = Path(sys.argv[2])
        out.mkdir(parents=True, exist_ok=True)
        to_image(icon()).save(out / 'icon0.png')
        to_image(background(960, 540)).save(out / 'background.png')
        to_image(launch(960, 540)).save(out / 'launch.png')
        return
    pictures = {'icon0.png': to_image(icon())}
    for source, target, draw in (('background-source.png', 'pic0.dds', background),
                                 ('launch-background-source.png', 'pic1.dds', launch)):
        picture = to_image(draw(), 'RGBA')
        pictures[source] = picture
        # Opaque, as ProsperoTV's pictures were (DX10 alpha mode 3).
        pictures[target] = dds(picture, 3)
        decoded = np.asarray(Image.open(__import__('io').BytesIO(pictures[target])).convert('RGB'), np.float32)
        error = ((decoded - np.asarray(picture.convert('RGB'), np.float32)) ** 2).mean()
        psnr = 10 * math.log10(255 * 255 / max(error, 1e-9))
        print(f'{target}: {psnr:.1f} dB against {source}')
        if psnr < 38:
            sys.exit(f'{target}: the BC7 blocks are too far from the picture')
    for folder in targets:
        for name, picture in pictures.items():
            if isinstance(picture, bytes):
                (folder / name).write_bytes(picture)
            else:
                picture.save(folder / name, optimize=True)
        print(folder.relative_to(root), ', '.join(pictures))


if __name__ == '__main__':
    main()
