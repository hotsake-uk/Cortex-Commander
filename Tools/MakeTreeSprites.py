"""Makes the sandbox's tree pictures (Paint > Plants > Trees) in the look of the base game's own plants.

Each tree is two pictures of the same size, drawn one over the other where it is put down: TreeTrunkNNN.png, the trunk, branches and
roots (painted in the tree trunk material, or wood without it), and TreeLeavesNNN.png, the leaves (vegetation). Both are 8-bit in the
game's palette, taken from a base game plant, with index 0 the see-through mask colour. The bottom c_RootDepth rows are roots, set into
the ground.

Run from the repository's root: python Tools/MakeTreeSprites.py
"""

import math
import random
from pathlib import Path

from PIL import Image

PLANTS = Path("Data/Base.rte/Scenes/Objects/Plants")
OUT = PLANTS / "Trees"
ROOT_DEPTH = 12  # Keep in step with c_TreeRootDepth in SandboxInternal.h.

# Palette ramps, dark to light, from the colours the base game's plants use.
LEAVES = [158, 150, 137, 140, 126, 127, 129, 148, 149, 131, 133, 135]
LEAVES_COOL = [158, 167, 139, 141, 125, 161, 130, 132, 134, 156]
LEAVES_AUTUMN = [22, 54, 55, 62, 65, 69, 70, 71, 131, 133]
BARK = [22, 54, 24, 25, 57, 58, 61, 64, 66, 67]


def shade(ramp, value):
    """The ramp's colour for a lightness from 0 to 1."""
    return ramp[max(0, min(len(ramp) - 1, int(value * len(ramp))))]


class Tree:
    def __init__(self, width, height, seed):
        self.w = width
        self.h = height
        self.seed = seed
        self.rng = random.Random(seed)
        self.cells = {}
        self.trunk = {}  # (x, y): lightness
        self.leaves = {}

    def noise(self, x, y, scale):
        # Cheap value noise from a hash of the cell, smoothly mixed: the clumps in the bark and the leaves.
        def cell(cx, cy):
            key = (cx, cy, scale)
            if key not in self.cells:
                self.cells[key] = random.Random(f"{self.seed}:{cx}:{cy}:{scale}").random()
            return self.cells[key]

        fx, fy = x / scale, y / scale
        x0, y0 = math.floor(fx), math.floor(fy)
        tx, ty = fx - x0, fy - y0
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a, b, c, d = cell(x0, y0), cell(x0 + 1, y0), cell(x0, y0 + 1), cell(x0 + 1, y0 + 1)
        return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty

    def limb(self, x0, y0, x1, y1, w0, w1, bend=0.0):
        """A tapering stroke of bark from one point to another, lit from the upper left."""
        steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(steps + 1):
            t = i / steps
            x = x0 + (x1 - x0) * t + math.sin(t * math.pi) * bend
            y = y0 + (y1 - y0) * t
            half = (w0 + (w1 - w0) * t) / 2
            for dx in range(-int(half) - 1, int(half) + 2):
                for dy in range(-1, 2):
                    px, py = int(round(x + dx)), int(round(y + dy * 0.5))
                    across = dx / max(half, 0.5)
                    if abs(across) > 1.0 or py >= self.h:
                        continue
                    light = 0.62 - across * 0.32 + (self.noise(px * 3, py * 0.6, 3) - 0.5) * 0.35
                    if abs(across) > 0.8:
                        light -= 0.25
                    self.trunk[(px, py)] = max(self.trunk.get((px, py), -1.0), light)

    def roots(self, base_x, ground, base_width):
        for _ in range(self.rng.randint(4, 7)):
            ex = base_x + self.rng.uniform(-base_width * 1.8, base_width * 1.8)
            ey = ground + self.rng.uniform(ROOT_DEPTH * 0.5, ROOT_DEPTH - 1)
            self.limb(base_x + self.rng.uniform(-base_width / 3, base_width / 3), ground - 2, ex, ey, self.rng.uniform(2, 3.5), 1, self.rng.uniform(-2, 2))

    def blob(self, cx, cy, rx, ry, ramp_light=0.0, holes=0.12):
        """A clump of leaves: an oval, ragged at the edge, lit from the upper left, darker underneath and at the rim."""
        for py in range(int(cy - ry - 2), int(cy + ry + 3)):
            for px in range(int(cx - rx - 2), int(cx + rx + 3)):
                nx, ny = (px - cx) / rx, (py - cy) / ry
                d = math.hypot(nx, ny)
                rag = (self.noise(px, py, 2.2) - 0.5) * 0.55
                if d > 1.0 + rag:
                    continue
                n = self.noise(px, py, 2.6)
                if d > 0.55 and n < holes:
                    continue
                light = 0.44 - nx * 0.18 - ny * 0.32 + (n - 0.5) * 0.55 + ramp_light
                if d > 0.85 + rag:
                    light -= 0.22
                # A highlight on each little clump, as the base plants' leaves have.
                if self.noise(px + 0.5, py + 0.5, 1.4) > 0.78 and ny < 0.3:
                    light += 0.25
                old = self.leaves.get((px, py))
                self.leaves[(px, py)] = light if old is None else max(old, light) * 0.6 + light * 0.4

    def pictures(self, leaf_ramp):
        """The trunk and leaf pictures, as wide as the tree reaches either way of the middle (so the trunk stays in the middle) and from
        its top down to the bottom of the roots."""
        points = list(self.trunk) + list(self.leaves)
        half = int(math.ceil(max(abs(x + 0.5 - self.w / 2) for x, _ in points))) + 1
        top = max(0, min(y for _, y in points) - 1) if min(y for _, y in points) >= 0 else min(y for _, y in points) - 1
        width = half * 2
        height = self.h - top
        left = int(self.w / 2) - half
        trunk = Image.new("P", (width, height), 0)
        leaves = Image.new("P", (width, height), 0)
        for (x, y), light in self.trunk.items():
            if (x, y) not in self.leaves:
                # A little dithering, as the base game's art has.
                light += 0.06 if (x + y) % 2 else -0.06
                trunk.putpixel((x - left, y - top), shade(BARK, light))
        for (x, y), light in self.leaves.items():
            light += 0.05 if (x + y) % 2 else -0.05
            leaves.putpixel((x - left, y - top), shade(leaf_ramp, light))
        return trunk, leaves


def broadleaf(seed, width, height, canopy_share=0.6, ramp=LEAVES):
    tree = Tree(width, height, seed)
    rng = tree.rng
    ground = height - ROOT_DEPTH
    base_x = width / 2 + rng.uniform(-3, 3)
    base_w = rng.uniform(7, 11) * width / 70
    top_y = height * (1 - canopy_share) * 0.55
    top_x = width / 2 + rng.uniform(-6, 6)
    tree.limb(base_x, ground + 2, top_x, top_y + height * 0.1, base_w, base_w * 0.35, rng.uniform(-4, 4))
    tree.roots(base_x, ground, base_w)
    canopy_cy = top_y + (ground - top_y) * 0.32
    ends = []
    for _ in range(rng.randint(4, 7)):
        t = rng.uniform(0.25, 0.65)
        sx = base_x + (top_x - base_x) * t
        sy = ground + (top_y - ground) * t
        side = rng.choice([-1, 1])
        ex = width / 2 + side * rng.uniform(width * 0.15, width * 0.38)
        ey = canopy_cy + rng.uniform(-height * 0.12, height * 0.08)
        tree.limb(sx, sy, ex, ey, base_w * 0.45, 1.2, rng.uniform(-3, 3))
        ends.append((ex, ey))
    ends.append((top_x, top_y + height * 0.08))
    rx_all, ry_all = width * 0.46, (ground - top_y) * canopy_share * 0.6
    tree.blob(width / 2, canopy_cy, rx_all * 0.8, ry_all * 0.75)
    for ex, ey in ends:
        tree.blob(ex, ey - rng.uniform(2, 6), rng.uniform(width * 0.13, width * 0.22), rng.uniform(height * 0.08, height * 0.13))
    for _ in range(rng.randint(3, 6)):
        tree.blob(width / 2 + rng.uniform(-rx_all * 0.6, rx_all * 0.6), canopy_cy + rng.uniform(-ry_all * 0.8, ry_all * 0.3), rng.uniform(width * 0.1, width * 0.18), rng.uniform(height * 0.06, height * 0.1), 0.05)
    return tree.pictures(ramp)


def conifer(seed, width, height, ramp=LEAVES_COOL):
    tree = Tree(width, height, seed)
    rng = tree.rng
    ground = height - ROOT_DEPTH
    base_x = width / 2
    base_w = rng.uniform(5, 8) * width / 40
    tiers = rng.randint(5, 8)
    tree.limb(base_x, ground + 2, base_x + rng.uniform(-1, 1), 8, base_w, 1.5)
    tree.roots(base_x, ground, base_w)
    bottom = ground - height * rng.uniform(0.12, 0.2)
    for i in range(tiers):
        t = i / (tiers - 1)
        y = bottom + (8 - bottom) * t
        half = (width * 0.48) * (1 - t * 0.8) + 3
        # Each tier: a drooping skirt of needles, wide at the bottom of the tier.
        tree.blob(base_x, y - half * 0.2, half, max(half * 0.45, 3), -0.05 + t * 0.12, holes=0.08)
        for side in (-1, 1):
            tree.blob(base_x + side * half * 0.6, y + half * 0.05, half * 0.45, max(half * 0.25, 2.5), -0.08, holes=0.1)
    return tree.pictures(ramp)


def poplar(seed, width, height, ramp=LEAVES):
    tree = Tree(width, height, seed)
    rng = tree.rng
    ground = height - ROOT_DEPTH
    base_x = width / 2
    base_w = rng.uniform(4, 6)
    tree.limb(base_x, ground + 2, base_x + rng.uniform(-2, 2), height * 0.12, base_w, 1.5, rng.uniform(-1.5, 1.5))
    tree.roots(base_x, ground, base_w)
    cy = height * 0.42
    tree.blob(base_x, cy, width * 0.42, height * 0.32)
    for _ in range(rng.randint(5, 8)):
        tree.blob(base_x + rng.uniform(-width * 0.2, width * 0.2), cy + rng.uniform(-height * 0.25, height * 0.2), width * rng.uniform(0.2, 0.3), height * rng.uniform(0.08, 0.13), 0.04)
    return tree.pictures(ramp)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    palette = Image.open(PLANTS / "Plant010.png").getpalette()
    trees = [
        broadleaf(1, 64, 110),
        broadleaf(2, 78, 130),
        broadleaf(3, 92, 150, 0.65),
        broadleaf(4, 56, 92),
        broadleaf(5, 104, 168, 0.62),
        broadleaf(6, 70, 118, ramp=LEAVES_AUTUMN),
        conifer(7, 44, 120),
        conifer(8, 56, 150),
        conifer(9, 36, 96),
        poplar(10, 30, 120),
        poplar(11, 36, 140),
        broadleaf(12, 84, 136, ramp=LEAVES_COOL),
    ]
    for index, (trunk, leaves) in enumerate(trees):
        for picture, name in ((trunk, "TreeTrunk"), (leaves, "TreeLeaves")):
            picture.putpalette(palette)
            picture.save(OUT / f"{name}{index:03d}.png")
    print(f"{len(trees)} trees written to {OUT}")


if __name__ == "__main__":
    main()
