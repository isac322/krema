"""Regenerate roast-contours.png, 2560x1440.

Requires numpy and Pillow: python3 gen.py
"""
import os

import numpy as np
from PIL import Image

W, H = 2560, 1440
OUT = os.path.dirname(os.path.abspath(__file__))


def hexrgb(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float64)


# Palette A
NIGHT, INK, INK2 = hexrgb("110c09"), hexrgb("18110d"), hexrgb("231a14")
DEEP = hexrgb("a8682f")

yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
u, v = xx / W, yy / H
aspect = W / H


def smooth_noise(cells_x, seed, octaves=3):
    r = np.random.default_rng(seed)
    acc = np.zeros((H, W))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cx = cells_x * 2 ** o
        cy = max(2, int(cx * H / W))
        small = (r.random((cy, cx)) * 255).astype(np.uint8)
        big = np.asarray(Image.fromarray(small).resize((W, H), Image.BICUBIC), dtype=np.float64) / 255
        acc += amp * big
        total += amp
        amp *= 0.5
    return acc / total


def mix(a, b, t):
    t = np.clip(t, 0, 1)[..., None]
    return a * (1 - t) + b * t


def grain(img, amount=2.2, seed=1):
    g = np.random.default_rng(seed).normal(0, amount, (H, W, 1))
    return img + g


def gauss(cx, cy, sx, sy):
    return np.exp(-(((u - cx) * aspect) ** 2 / (2 * sx ** 2) + (v - cy) ** 2 / (2 * sy ** 2)))


# Roast Contours: faint topographic lines (bean-roast map) on a warm-dark gradient.
field = smooth_noise(4, 33, 3)
levels = field * 26.0
frac = np.abs(levels - np.round(levels))
gx, gy = np.gradient(levels)
width = np.sqrt(gx ** 2 + gy ** 2) * 1.2 + 1e-6
line = np.clip(1 - frac / width, 0, 1)
major = (np.round(levels) % 5 == 0).astype(np.float64)
base = mix(INK[None, None, :] * np.ones((H, W, 1)), NIGHT, v ** 0.8)
base = mix(base, INK2, gauss(0.75, 0.25, 0.8, 0.45) * 0.8)
img = mix(base, DEEP, line * (0.26 + 0.30 * major) * (1 - 0.55 * v))
img = grain(img, 2.0, 13)
img = np.clip(img + np.random.default_rng(3).random(img.shape) - 0.5, 0, 255).astype(np.uint8)
Image.fromarray(img, "RGB").save(os.path.join(OUT, "roast-contours.png"), optimize=True)
print("wrote roast-contours.png")
