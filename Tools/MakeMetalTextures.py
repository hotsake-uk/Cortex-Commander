"""Makes the textures of the metals the sandbox paints (Paint > Metals), in the look of the bunkers' own plating.

Each is a tile, 8-bit in the game's palette, that the painted metal takes its colours from (the scene's pixel x, y read at x % width,
y % height, so strokes painted side by side line up). Plates with seams and bolts, lit from the upper left like the base game's art;
the polished metals have a faint brushed grain and a broad sheen baked in, and the lighting puts the moving shine on top (their
Metalness and Gloss in Materials.ini).

Run from the repository's root: python Tools/MakeMetalTextures.py
"""

import math
import random
from pathlib import Path

from PIL import Image

OUT = Path("Data/Base.rte/Scenes/Textures/Metals")
PALETTE_FROM = Path("Data/Base.rte/Scenes/Textures/Concrete.png")

# Colour ramps, dark to light (matched to the nearest colour of the palette).
RAMPS = {
    "Steel": [(32, 47, 47), (62, 62, 62), (113, 133, 134), (147, 155, 154), (166, 171, 172), (175, 189, 199), (187, 210, 220), (204, 217, 217)],
    "Tread": [(47, 47, 47), (79, 79, 79), (117, 117, 115), (147, 155, 154), (166, 171, 172), (175, 189, 199), (204, 217, 217), (220, 234, 234)],
    "Rust": [(47, 19, 13), (79, 32, 6), (96, 47, 13), (115, 62, 32), (133, 79, 39), (152, 90, 32), (170, 104, 50), (201, 130, 57)],
    "Gold": [(62, 48, 13), (115, 96, 39), (161, 109, 20), (210, 163, 72), (241, 201, 23), (246, 205, 51), (249, 234, 115), (249, 249, 185)],
    "Brass": [(71, 67, 19), (103, 103, 18), (123, 127, 38), (145, 140, 70), (181, 176, 70), (213, 193, 73), (248, 227, 90), (249, 248, 145)],
    "Bronze": [(62, 32, 6), (104, 67, 15), (137, 71, 16), (152, 90, 32), (201, 130, 57), (175, 138, 75), (210, 163, 72), (235, 190, 109)],
    "Copper": [(64, 19, 12), (96, 32, 13), (115, 47, 13), (133, 61, 39), (181, 86, 39), (210, 146, 104), (250, 155, 133), (251, 196, 179)],
    "Silver": [(62, 62, 62), (96, 96, 96), (141, 144, 139), (147, 155, 154), (166, 171, 172), (204, 217, 217), (220, 220, 228), (234, 234, 234)],
    "Chrome": [(32, 32, 32), (79, 79, 79), (147, 155, 154), (204, 217, 217), (220, 234, 249), (234, 249, 249), (250, 250, 250), (255, 255, 255)],
}


def load_palette():
    image = Image.open(PALETTE_FROM)
    flat = image.getpalette()[: 256 * 3]
    return image, [tuple(flat[i * 3 : i * 3 + 3]) for i in range(256)]


def nearest(palette, rgb):
    # Index 0 is the see-through mask colour; never pick it.
    return min(range(1, 256), key=lambda i: sum((a - b) ** 2 for a, b in zip(palette[i], rgb)))


class Tile:
    def __init__(self, width, height, ramp, seed):
        self.width, self.height = width, height
        self.ramp = ramp
        self.rng = random.Random(seed)
        self.light = [[0.5] * width for _ in range(height)]

    def add(self, x, y, amount):
        self.light[y % self.height][x % self.width] += amount

    def set(self, x, y, value):
        self.light[y % self.height][x % self.width] = value

    def grain(self, strength, length):
        """Brushed grain: streaks along x, each row a little lighter or darker in runs."""
        for y in range(self.height):
            x = 0
            while x < self.width:
                run = self.rng.randint(length // 2, length)
                shift = (self.rng.random() - 0.5) * strength
                for dx in range(run):
                    self.add(x + dx, y, shift)
                x += run

    def speckle(self, strength):
        for y in range(self.height):
            for x in range(self.width):
                self.add(x, y, (self.rng.random() - 0.5) * strength)

    def sheen(self, strength, period):
        """Broad diagonal bands of shine, as a polished plate catches the light. Whole periods across the tile, so it wraps."""
        for y in range(self.height):
            for x in range(self.width):
                phase = (x / self.width + y / self.height) * 2 * math.pi * period
                self.add(x, y, strength * max(0.0, math.cos(phase)) ** 3)

    def plates(self, plate_width, plate_height, stagger, bolts, bevel=1):
        """Plates in rows, each a little lighter at the top: a dark seam along the bottom and right of each, a light edge along its top
        and left, and bolts in its corners."""
        for row in range(self.height // plate_height):
            offset = (plate_width // 2) if stagger and row % 2 else 0
            top = row * plate_height
            for column in range(self.width // plate_width):
                left = column * plate_width + offset
                tone = (self.rng.random() - 0.5) * 0.08
                for dy in range(plate_height):
                    for dx in range(plate_width):
                        self.add(left + dx, top + dy, tone + 0.08 * (1 - dy / plate_height))
                for dx in range(plate_width):
                    for b in range(bevel):
                        self.add(left + dx, top + b, 0.22)
                    self.set(left + dx, top + plate_height - 1, 0.04)
                for dy in range(plate_height):
                    for b in range(bevel):
                        self.add(left + b, top + dy, 0.14)
                    self.set(left + plate_width - 1, top + dy, 0.08)
                if bolts:
                    for bx, by in ((3, 3), (plate_width - 5, 3), (3, plate_height - 5), (plate_width - 5, plate_height - 5)):
                        self.set(left + bx, top + by, 0.95)
                        self.set(left + bx + 1, top + by, 0.6)
                        self.set(left + bx, top + by + 1, 0.6)
                        self.set(left + bx + 1, top + by + 1, 0.12)

    def tread(self, spacing):
        """Diamond tread plate: little raised bars, alternately leaning each way, lit on top and shadowed under."""
        for row, y in enumerate(range(0, self.height, spacing)):
            for column, x in enumerate(range(0, self.width, spacing)):
                lean = 1 if (row + column) % 2 else -1
                cx, cy = x + spacing // 2, y + spacing // 2
                for t in range(-2, 3):
                    px, py = cx + t, cy + t * lean
                    self.set(px, py, 0.95)
                    self.set(px, py + 1, 0.15)

    def rust(self, coverage, rust_tile):
        """Patches where the plate has rusted through to the rust ramp's colours (taken from rust_tile)."""
        self.rusted = [[False] * self.width for _ in range(self.height)]
        for _ in range(int(self.width * self.height * coverage / 40)):
            cx, cy = self.rng.randrange(self.width), self.rng.randrange(self.height)
            radius = self.rng.uniform(1.5, 5.0)
            for dy in range(-6, 7):
                for dx in range(-6, 7):
                    if dx * dx + dy * dy <= radius * radius * self.rng.uniform(0.6, 1.2):
                        self.rusted[(cy + dy) % self.height][(cx + dx) % self.width] = True
        self.rust_tile = rust_tile

    def image(self, base, palette):
        out = Image.new("P", (self.width, self.height), 0)
        out.putpalette(base.getpalette())
        indices = [nearest(palette, rgb) for rgb in self.ramp]
        rust = [nearest(palette, rgb) for rgb in self.rust_tile.ramp] if getattr(self, "rust_tile", None) else None
        for y in range(self.height):
            for x in range(self.width):
                value = max(0.0, min(0.999, self.light[y][x]))
                if rust and self.rusted[y][x]:
                    rv = max(0.0, min(0.999, self.rust_tile.light[y][x]))
                    out.putpixel((x, y), rust[int(rv * len(rust))])
                else:
                    out.putpixel((x, y), indices[int(value * len(indices))])
        return out


def polished(name, seed, plate_width, plate_height, sheen=0.1, base=0.5):
    tile = Tile(96, 96, RAMPS[name], seed)
    for row in tile.light:
        row[:] = [base] * tile.width
    tile.grain(0.07, 10)
    tile.sheen(sheen, 1)
    tile.plates(plate_width, plate_height, stagger=False, bolts=False)
    return tile


def main():
    base, palette = load_palette()
    OUT.mkdir(parents=True, exist_ok=True)
    tiles = {}

    # The bunkers' plating: light blue-grey plates, staggered like brickwork, bolted at the corners.
    steel = Tile(96, 48, RAMPS["Steel"], 1)
    steel.speckle(0.06)
    steel.plates(48, 24, stagger=True, bolts=True)
    tiles["SteelPlate"] = steel

    # Heavy plate (Mega Metal): big tread plate panels.
    tread = Tile(96, 96, RAMPS["Tread"], 2)
    tread.speckle(0.05)
    tread.tread(6)
    tread.plates(48, 48, stagger=False, bolts=True)
    tiles["TreadPlate"] = tread

    # Scrap metal: battered plates rusted through in patches.
    scrap = Tile(96, 48, RAMPS["Steel"], 3)
    scrap.speckle(0.18)
    scrap.plates(32, 16, stagger=True, bolts=False)
    rust = Tile(96, 48, RAMPS["Rust"], 4)
    rust.speckle(0.5)
    scrap.rust(0.5, rust)
    tiles["ScrapPlate"] = scrap

    tiles["GoldPlate"] = polished("Gold", 10, 48, 24, 0.12, 0.38)
    tiles["SilverPlate"] = polished("Silver", 11, 48, 48, 0.1, 0.42)
    tiles["BronzePlate"] = polished("Bronze", 12, 32, 32)
    tiles["BrassPlate"] = polished("Brass", 13, 48, 24, 0.1, 0.4)
    tiles["CopperPlate"] = polished("Copper", 14, 32, 16)
    tiles["ChromePlate"] = polished("Chrome", 15, 96, 48, 0.3, 0.3)

    for name, tile in tiles.items():
        tile.image(base, palette).save(OUT / f"{name}.png")
        print("Wrote", OUT / f"{name}.png")


if __name__ == "__main__":
    main()
