#!/usr/bin/env python3
"""Generate the Amrvis2 application logo.

The emblem is a heatmap: a curved Gaussian ridge mapped through a rainbow
palette (blue -> cyan -> green -> yellow -> red) with a white AMR-style grid
overlay -- a stylized Amrvis2 scalar-field render. The design is produced
procedurally so it stays crisp and reproducible.

Usage:
    generate_logo.py [--shape square|rounded|circle] [--size N] [-o OUT]

The square canvas is the source of truth; `rounded` and `circle` apply an
alpha/background mask so the emblem also reads well under the squircle or
circular masks that macOS/iOS/Android apply to app icons.
"""
import argparse

import numpy as np
from PIL import Image


def rainbow_lut(n: int = 256) -> np.ndarray:
    """Blue -> cyan -> green -> yellow -> red, as an (n, 3) float table."""
    t = np.linspace(0.0, 1.0, n)
    lut = np.zeros((n, 3), dtype=np.float64)
    for i, v in enumerate(t):
        if v < 0.25:
            f = v / 0.25
            r, g, b = 0.0, f, 1.0
        elif v < 0.5:
            f = (v - 0.25) / 0.25
            r, g, b = 0.0, 1.0, 1.0 - f
        elif v < 0.75:
            f = (v - 0.5) / 0.25
            r, g, b = f, 1.0, 0.0
        else:
            f = (v - 0.75) / 0.25
            r, g, b = 1.0, 1.0 - f, 0.0
        lut[i] = (r, g, b)
    return lut


def heat_field(coords: np.ndarray) -> np.ndarray:
    """A curved Gaussian ridge along a C-shaped interior arc, in [0, 1]."""
    x = coords[..., 0]
    y = coords[..., 1]
    cx, cy, radius, sigma = 0.40, 0.40, 0.42, 0.15
    dx = x - cx
    dy = y - cy
    d_arc = np.abs(np.sqrt(dx * dx + dy * dy) - radius)
    ridge = np.exp(-(d_arc / sigma) ** 2)
    # Restrict the band to a C-shaped arc segment that stays inside the canvas,
    # so the bright region is a smooth curve the AMR boxes can approximate with
    # a few rectangles.
    theta = np.arctan2(dy, dx)
    angular = np.exp(-(((theta - 0.6) / 1.1) ** 2))
    return np.clip(ridge * angular, 0.0, 1.0)


def render(size: int, shape: str, ss: int = 4) -> Image.Image:
    s = size * ss
    ys, xs = np.mgrid[0:s, 0:s].astype(np.float64)
    x = xs / (s - 1)
    y = 1.0 - ys / (s - 1)
    coords = np.stack([x, y], axis=-1)

    field = heat_field(coords)
    lut = rainbow_lut(256)
    idx = np.clip(field * 255.0, 0, 255).astype(np.int32)
    rgb = lut[idx]  # (s, s, 3)

    # AMR-style hierarchical grid: a coarse level-0 grid over the whole
    # canvas, with finer sub-grids inside coarse-aligned "refined" patches
    # (level 1, and a nested level 2) placed where the field is hot -- like
    # Amrvis2 refining cells around an interesting region. White lines, with
    # the refined-box outlines emphasized.
    def add_lines(mask, x0, y0, x1, y1, spacing, thickness):
        inside = (x >= x0) & (x <= x1) & (y >= y0) & (y <= y1)
        on = np.zeros_like(x, dtype=bool)
        kx = int(np.floor(x0 / spacing)) + 1
        while kx * spacing < x1 - 1e-9:
            on |= np.abs(x - kx * spacing) < thickness
            kx += 1
        ky = int(np.floor(y0 / spacing)) + 1
        while ky * spacing < y1 - 1e-9:
            on |= np.abs(y - ky * spacing) < thickness
            ky += 1
        mask |= inside & on

    def add_outline(mask, x0, y0, x1, y1, thickness):
        inside = (x >= x0) & (x <= x1) & (y >= y0) & (y <= y1)
        edge = (np.abs(x - x0) < thickness) | (np.abs(x - x1) < thickness) \
            | (np.abs(y - y0) < thickness) | (np.abs(y - y1) < thickness)
        mask |= inside & edge

    c = 1.0 / 6.0

    def snap(v, step, up):
        v = min(max(v, 0.0), 1.0)
        return float((np.ceil if up else np.floor)(v / step) * step)

    def cover_slabs(mask, n_slabs, step, pad):
        # Cover the masked region (image coords, row 0 = top) with up to
        # n_slabs horizontal slabs, each a grid-aligned rectangle -- a curved
        # feature becomes a small set of AMR boxes whose union need not be
        # rectangular, each box holding an integer number of cells.
        rows = np.where(np.any(mask, axis=1))[0]
        if rows.size == 0:
            return []
        top, bot = int(rows.min()), int(rows.max())
        rects = []
        for k in range(n_slabs):
            r0 = top + round((bot - top) * k / n_slabs)
            r1 = top + round((bot - top) * (k + 1) / n_slabs)
            cols = np.where(np.any(mask[r0:r1 + 1, :], axis=0))[0]
            if cols.size == 0:
                continue
            x0 = snap(cols.min() / (s - 1) - pad, step, False)
            x1 = snap(cols.max() / (s - 1) + pad, step, True)
            yhi = snap(1.0 - r0 / (s - 1) + pad, step, True)
            ylo = snap(1.0 - r1 / (s - 1) - pad, step, False)
            if x1 > x0 and yhi > ylo:
                rects.append((x0, ylo, x1, yhi))
        return rects

    # Level 2 (finest) covers the very red region; level 1 wraps the broader
    # warm region. Same slab breaks so each level-2 box nests in its level-1
    # box; the union of boxes follows the curve and need not be rectangular.
    step1 = c / 2.0
    step2 = c / 4.0
    l2 = cover_slabs(field > 0.85, 3, step2, 0.5 * step2)
    l1 = cover_slabs(field > 0.60, 3, step1, 0.5 * step1)
    for i in range(min(len(l1), len(l2))):
        x0, y0, x1, y1 = l1[i]
        l1[i] = (min(x0, l2[i][0]), min(y0, l2[i][1]),
                 max(x1, l2[i][2]), max(y1, l2[i][3]))

    grid = np.zeros_like(x, dtype=bool)
    t = 1.1 / size
    add_lines(grid, 0.0, 0.0, 1.0, 1.0, c, t)            # level 0 (whole canvas)
    for (x0, y0, x1, y1) in l1:                          # level 1 boxes (2x finer)
        add_lines(grid, x0, y0, x1, y1, step1, t)
        add_outline(grid, x0, y0, x1, y1, 1.7 / size)
    for (x0, y0, x1, y1) in l2:                          # level 2 boxes (4x finer, on red)
        add_lines(grid, x0, y0, x1, y1, step2, t * 0.9)
        add_outline(grid, x0, y0, x1, y1, 1.7 / size)

    white = np.array([1.0, 1.0, 1.0])
    blend = np.where(grid, 0.62, 0.0)[..., None]
    rgb = rgb * (1.0 - blend) + white * blend

    rgba = np.empty((s, s, 4), dtype=np.float64)
    rgba[..., :3] = rgb
    rgba[..., 3] = 1.0

    if shape == "rounded":
        mask = rounded_rect_mask(s, radius_frac=0.20)
        rgba[..., 3] *= mask
    elif shape == "circle":
        # Solid dark background behind a circular emblem, plus a subtle rim.
        bg = np.array([0.04, 0.09, 0.16])  # deep navy
        circ = circle_mask(s)
        inside = circ > 0.5
        for c in range(3):
            rgba[..., c] = np.where(inside, rgba[..., c], bg[c])
        rim = (circ > 0.0) & (circ < 1.0)
        rgba[rim, :3] = 0.85

    out = (np.clip(rgba, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    img = Image.fromarray(out, "RGBA")
    if ss != 1:
        img = img.resize((size, size), Image.LANCZOS)
    return img


def rounded_rect_mask(s: int, radius_frac: float) -> np.ndarray:
    ys, xs = np.mgrid[0:s, 0:s].astype(np.float64)
    r = radius_frac * s
    dx = np.clip(np.abs(xs - (s - 1) / 2.0) - (s / 2.0 - r), 0, None)
    dy = np.clip(np.abs(ys - (s - 1) / 2.0) - (s / 2.0 - r), 0, None)
    d = np.sqrt(dx * dx + dy * dy)
    return np.clip(1.0 - (d - (r - 1.5)) / 2.0, 0.0, 1.0)


def circle_mask(s: int, shrink: float = 0.0) -> np.ndarray:
    ys, xs = np.mgrid[0:s, 0:s].astype(np.float64)
    cx = cy = (s - 1) / 2.0
    r = s / 2.0 - shrink * s
    d = np.sqrt((xs - cx) ** 2 + (ys - cy) ** 2)
    return np.clip(1.0 - (d - (r - 1.5)) / 2.0, 0.0, 1.0)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--shape", choices=["square", "rounded", "circle"],
                   default="rounded")
    p.add_argument("--size", type=int, default=256)
    p.add_argument("-o", "--output", default="amrvis2.png")
    args = p.parse_args()
    img = render(args.size, args.shape)
    img.save(args.output)
    print(f"wrote {args.output} ({args.shape}, {args.size}x{args.size})")


if __name__ == "__main__":
    main()
