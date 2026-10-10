#!/usr/bin/env python3
"""Draws the Medieval fire and castle buildings: a bonfire, a campfire and a castle gatehouse.

Usage (from the repository root): python3 Tools/Medieval/fires.py Data

Each is written as FG / BG / Mat PNGs (in the game palette, taken from the existing Medieval buildings) into
Data/Medieval.rte/Scenes/Buildings, and its TerrainObject entry is added to Buildings.ini if it isn't there yet.
The Mat layer uses the base game's Wood (129) and Stone (12), so the logs and the gate burn and the stone doesn't.
"""

import random
import sys
from pathlib import Path

from PIL import Image

WOOD = 129
STONE = 12


class Sprite:
    def __init__(self, width, height):
        self.w = width
        self.h = height
        self.fg = [[None] * width for _ in range(height)]
        self.bg = [[None] * width for _ in range(height)]
        self.mat = [[0] * width for _ in range(height)]

    def inside(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h

    def put(self, x, y, rgb, material):
        if self.inside(x, y):
            self.fg[y][x] = rgb
            self.mat[y][x] = material

    def clear(self, x, y):
        """Hollows a pixel out: air, with nothing drawn in front."""
        if self.inside(x, y):
            self.fg[y][x] = None
            self.mat[y][x] = 0

    def back(self, x, y, rgb):
        if self.inside(x, y):
            self.bg[y][x] = rgb


def shade(rgb, amount):
    return tuple(max(0, min(255, int(c + amount))) for c in rgb)


def log(sprite, rng, x0, y0, length, thick, material=WOOD):
    """A horizontal log: bark with a darker underside, the cut ends pale with a ring."""
    bark = (98, 64, 36)
    for dy in range(thick):
        light = 22 - 44 * dy / max(thick - 1, 1)
        for dx in range(length):
            colour = shade(bark, light + rng.randint(-10, 10))
            if rng.random() < 0.07:
                colour = shade(bark, -34)
            sprite.put(x0 + dx, y0 + dy, colour, material)
    for end in (0, length - 1):
        for dy in range(thick):
            for step in range(2):
                x = x0 + end + (step if end == 0 else -step)
                ring = dy in (0, thick - 1) or (step == 1 and dy % 2 == 0)
                sprite.put(x, y0 + dy, (150, 108, 64) if ring else (190, 146, 92), material)


def stick(sprite, rng, a, b, thick, material=WOOD):
    """A branch, drawn as a run of little squares along the line."""
    (xa, ya), (xb, yb) = a, b
    steps = max(abs(xb - xa), abs(yb - ya), 1)
    for i in range(steps + 1):
        x = round(xa + (xb - xa) * i / steps)
        y = round(ya + (yb - ya) * i / steps)
        for dy in range(thick):
            for dx in range(thick):
                sprite.put(x + dx, y + dy, shade((84, 56, 32), rng.randint(-12, 12) + (8 if dx == 0 else -8)), material)


def stone_block(sprite, rng, x0, y0, width, height, brick_w=9, brick_h=5):
    """Dressed stone in courses, each block its own grey, with dark mortar between."""
    for row in range((height + brick_h - 1) // brick_h):
        offset = (brick_w // 2) if row % 2 else 0
        for col in range(-1, width // brick_w + 2):
            tone = rng.randint(-14, 14)
            for dy in range(brick_h):
                y = y0 + row * brick_h + dy
                if y >= y0 + height:
                    continue
                for dx in range(brick_w):
                    x = x0 + col * brick_w - offset + dx
                    if not (x0 <= x < x0 + width):
                        continue
                    if dy == brick_h - 1 or dx == brick_w - 1:
                        colour = (80, 78, 76)
                    elif dy == 0 or dx == 0:
                        colour = shade((150, 148, 142), tone)
                    else:
                        colour = shade((126, 124, 120), tone + rng.randint(-5, 5))
                    sprite.put(x, y, colour, STONE)


def crenellations(sprite, rng, x0, x1, y_top, rise, merlon=7, gap=5):
    x = x0
    while x + merlon <= x1:
        stone_block(sprite, rng, x, y_top, merlon, rise, 7, 5)
        x += merlon + gap


def bonfire(rng):
    s = Sprite(72, 60)
    # Branches leaning in to a point, behind the stacked logs.
    stick(s, rng, (18, 52), (35, 6), 3)
    stick(s, rng, (54, 52), (36, 8), 3)
    stick(s, rng, (36, 52), (35, 4), 3)
    # The pile: each course narrower than the one below it, the odd ones in two logs with a gap for the flames to climb.
    courses = [(53, 4, 64, False), (46, 10, 52, True), (39, 15, 42, False), (32, 20, 32, True), (25, 26, 20, False)]
    for y, x, length, split in courses:
        if split:
            half = (length - 4) // 2
            log(s, rng, x, y, half, 6)
            log(s, rng, x + half + 4, y, length - half - 4, 6)
        else:
            log(s, rng, x, y, length, 6)
    return s


def campfire(rng):
    s = Sprite(60, 36)
    # A teepee of sticks over two logs, the embers glowing where they meet.
    for foot in (13, 21, 39, 47):
        stick(s, rng, (foot, 31), (28 + (foot - 30) // 6, 8), 3)
    stick(s, rng, (29, 31), (29, 6), 3)
    log(s, rng, 13, 30, 34, 5)
    log(s, rng, 19, 25, 22, 5)
    for _ in range(46):
        x = rng.randint(14, 46)
        y = rng.randint(14, 34)
        if s.mat[y][x] == WOOD:
            s.fg[y][x] = rng.choice([(255, 120, 30), (255, 190, 70), (210, 60, 20), (255, 150, 40)])
    # A ring of stones round it: big ones at the sides, small ones in front, the middle left for the fire.
    for x, top, width in ((0, 24, 12), (9, 29, 9), (42, 29, 9), (49, 24, 11), (22, 32, 7), (31, 32, 7)):
        height = 36 - top
        for dy in range(height):
            for dx in range(width):
                if (dy == 0 or dy == height - 1) and dx in (0, width - 1):
                    continue
                tone = rng.randint(-10, 10) + (14 if dy < 2 else (-18 if dy > height - 3 else 0))
                s.put(x + dx, top + dy, shade((132, 130, 125), tone), STONE)
    return s


def gatehouse(rng):
    s = Sprite(168, 156)
    ox, oy = 4, 6
    tower_w, tower_h = 36, 130
    wall_top = oy + 56
    # The towers, with their battlements.
    for x in (ox, ox + 124):
        stone_block(s, rng, x, oy + 14, tower_w, tower_h)
        crenellations(s, rng, x, x + tower_w, oy + 4, 10, 8, 6)
        # a floor of timber showing at the top of each, under the merlons
        for ly in (46, 90):
            for lx in range(x + 8, x + tower_w - 8):
                s.put(lx, oy + ly, (74, 52, 32), WOOD)
        # arrow slits: hollowed right through
        for sy in (30, 70, 104):
            for dy in range(10):
                for dx in range(2):
                    s.clear(x + tower_w // 2 - 1 + dx, oy + sy + dy)
                    s.back(x + tower_w // 2 - 1 + dx, oy + sy + dy, (24, 22, 26))
    # The wall between them, and its battlements.
    stone_block(s, rng, ox + tower_w, wall_top, 88, 144 - (wall_top - oy) + 0)
    crenellations(s, rng, ox + tower_w + 3, ox + tower_w + 88 - 3, wall_top - 10, 10, 8, 6)
    # The archway: a rectangle with a half-round head, filled by the gate.
    cx = ox + tower_w + 44
    arch_top = oy + 98
    radius = 16
    gate_bottom = oy + 143
    for y in range(arch_top, gate_bottom + 1):
        for x in range(cx - radius, cx + radius):
            if y < arch_top + radius:
                dy = arch_top + radius - y
                if (x - cx + 0.5) ** 2 + dy ** 2 > radius ** 2:
                    continue
            s.clear(x, y)
    for y in range(arch_top, gate_bottom + 1):
        for x in range(cx - radius, cx + radius):
            if y < arch_top + radius and ((x - cx + 0.5) ** 2 + (arch_top + radius - y) ** 2 > radius ** 2):
                continue
            plank = (x - (cx - radius)) // 4
            edge = (x - (cx - radius)) % 4 == 3
            colour = (46, 32, 20) if edge else shade((112, 78, 46), (plank % 3 - 1) * 8 + rng.randint(-8, 8))
            band = (y - arch_top) % 22 in (8, 9)
            if band:
                colour = shade((70, 72, 80), rng.randint(-6, 6))
            if band and (x - cx) % 6 == 0:
                colour = (150, 152, 160)
            s.put(x, y, colour, WOOD)
    # Sconces: a niche cut in the wall each side of the gate, with a bracket and a flame at the back.
    for nx in (cx - radius - 12, cx + radius + 6):
        for dy in range(11):
            for dx in range(6):
                s.clear(nx + dx, oy + 104 + dy)
                s.back(nx + dx, oy + 104 + dy, (34, 28, 26))
        for dy in range(4, 11):
            s.back(nx + 2, oy + 104 + dy, (84, 56, 32))
            s.back(nx + 3, oy + 104 + dy, (84, 56, 32))
        for (fx, fy, colour) in [(2, 1, (255, 224, 120)), (3, 1, (255, 224, 120)), (2, 2, (255, 170, 50)), (3, 2, (255, 170, 50)), (2, 3, (255, 120, 30)), (3, 3, (255, 120, 30)), (1, 3, (255, 120, 30)), (4, 3, (255, 120, 30))]:
            s.back(nx + fx, oy + 104 + fy, colour)
    # A banner over the arch, in the background of the wall (a niche again, so it shows).
    for dy in range(22):
        for dx in range(14):
            s.clear(cx - 7 + dx, oy + 66 + dy)
            colour = (150, 36, 36) if dy < 18 or abs(dx - 7) > dy - 17 + 1 else (24, 22, 26)
            s.back(cx - 7 + dx, oy + 66 + dy, colour)
    for dx in range(14):
        s.back(cx - 7 + dx, oy + 66, (214, 176, 70))
    return s


def palette_of(folder):
    im = Image.open(folder / "WatchtowerFG.png")
    raw = im.getpalette()
    return raw, [(raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]) for i in range(256)]


def save(sprite, folder, name, palette_raw, palette):
    cache = {}

    def index_of(rgb):
        if rgb not in cache:
            best, best_distance = 1, 1 << 30
            for i in range(1, 256):
                r, g, b = palette[i]
                distance = (r - rgb[0]) ** 2 + (g - rgb[1]) ** 2 + (b - rgb[2]) ** 2
                if distance < best_distance:
                    best, best_distance = i, distance
            cache[rgb] = best
        return cache[rgb]

    for suffix, grid in (("FG", sprite.fg), ("BG", sprite.bg)):
        im = Image.new("P", (sprite.w, sprite.h), 0)
        im.putpalette(palette_raw)
        px = im.load()
        for y in range(sprite.h):
            for x in range(sprite.w):
                if grid[y][x] is not None:
                    px[x, y] = index_of(grid[y][x])
        im.save(folder / f"{name}{suffix}.png", transparency=0)
    im = Image.new("P", (sprite.w, sprite.h), 0)
    im.putpalette(palette_raw)
    px = im.load()
    for y in range(sprite.h):
        for x in range(sprite.w):
            px[x, y] = sprite.mat[y][x]
    im.save(folder / f"{name}Mat.png", transparency=0)


def light(x, y, colour, radius, intensity, flicker):
    return f"""	AddLight = TerrainLight
		Offset = Vector
			X = {x}
			Y = {y}
		Color = Color
			R = {colour[0]}
			G = {colour[1]}
			B = {colour[2]}
		Radius = {radius}
		Intensity = {intensity}
		Flicker = {flicker}
"""


def entry(name, file, description, gold, sprite, lights):
    return f"""

AddTerrainObject = TerrainObject
	PresetName = {name}
	Description = {description}
	AddToGroup = Bunker Modules
	AddToGroup = Medieval Buildings
	GoldValue = {gold}
	FGColorFile = ContentFile
		FilePath = Medieval.rte/Scenes/Buildings/{file}FG.png
	MaterialFile = ContentFile
		FilePath = Medieval.rte/Scenes/Buildings/{file}Mat.png
	BGColorFile = ContentFile
		FilePath = Medieval.rte/Scenes/Buildings/{file}BG.png
	BitmapOffset = Vector
		X = {-(sprite.w // 2)}
		Y = {-sprite.h}
{lights}"""


def main():
    data = Path(sys.argv[1] if len(sys.argv) > 1 else "Data")
    folder = data / "Medieval.rte" / "Scenes" / "Buildings"
    raw, palette = palette_of(folder)
    rng = random.Random(1214)
    ini = folder / "Buildings.ini"
    text = ini.read_text(encoding="utf-8")
    additions = ""
    glow = (255, 160, 70)
    things = [
        ("Bonfire", "Bonfire", bonfire(rng), "A tall pile of logs and branches, stacked to be burned. It is wood all through: set it alight and it burns down to ash and charcoal.", 8, ""),
        ("Campfire", "Campfire", campfire(rng), "Crossed logs in a ring of stones, the embers still glowing under them. It lights up the ground round it, and burns up if the fire is fed.", 6, light(29, 24, glow, 95, 0.9, 0.4)),
        ("Castle Gatehouse", "CastleGatehouse", gatehouse(rng), "Two stone towers and a wall between them, with a barred wooden gate under the arch and a torch each side of it. The gate burns.", 70, light(59, 114, glow, 110, 1.3, 0.4) + light(109, 114, glow, 110, 1.3, 0.4)),
    ]
    for name, file, sprite, description, gold, lights in things:
        save(sprite, folder, file, raw, palette)
        if f"PresetName = {name}\n" not in text.replace("\r\n", "\n"):
            additions += entry(name, file, description, gold, sprite, lights)
    if additions:
        ini.write_text(text.rstrip("\n") + "\n" + additions.rstrip("\n") + "\n", encoding="utf-8")
    print("wrote", ", ".join(t[0] for t in things))


if __name__ == "__main__":
    main()
