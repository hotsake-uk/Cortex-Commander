"""Makes the vehicles' pictures (VH-1) in the look of the base game's art: 8-bit in the game's palette, index 0 the see-through mask colour,
lit from the upper left, with a little dithering.

The wooden cart: its body (the bed, facing right), a spoked wheel with an iron tyre, the planks and splinters it breaks into, and the icon
for the buy menu (the body with its wheels on).

Run from the repository's root: python Tools/MakeVehicleSprites.py
"""

import math
import random
from pathlib import Path

from PIL import Image

OUT = Path("Data/Base.rte/Actors/Vehicles/WoodenCart")
PALETTE = Image.open("Data/Base.rte/palette.bmp").getpalette()[:768]

# Palette ramps, dark to light.
WOOD = [21, 22, 54, 25, 58, 61, 62, 66, 67, 72]
IRON = [245, 246, 247, 248, 249, 250, 251, 94, 124]


def shade(ramp, value):
    """The ramp's colour for a lightness from 0 to 1."""
    return ramp[max(0, min(len(ramp) - 1, int(value * len(ramp))))]


def dither(x, y, light, amount=0.05):
    return light + (amount if (x + y) % 2 else -amount)


class Canvas:
    def __init__(self, width, height, seed):
        self.w = width
        self.h = height
        self.px = {}
        self.rng = random.Random(seed)
        self.grain = {}

    def put(self, x, y, colour):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[(x, y)] = colour

    def grain_at(self, row, x):
        """Streaks along a plank: a lightness offset that changes slowly along it and from row to row."""
        key = (row, x // 3)
        if key not in self.grain:
            self.grain[key] = self.rng.uniform(-0.12, 0.12)
        return self.grain[key]

    def plank(self, x0, y0, x1, y1, base=0.55, seed_row=0):
        """A board from (x0, y0) to (x1, y1) inclusive, lit from above: a light top edge, a dark bottom one, grain along it."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                t = (y - y0) / max(y1 - y0, 1)
                light = base + 0.18 - t * 0.3 + self.grain_at(seed_row * 100 + y, x + seed_row * 7)
                if y == y0:
                    light += 0.12
                if y == y1:
                    light -= 0.25
                if x in (x0, x1):
                    light -= 0.15
                self.put(x, y, shade(WOOD, dither(x, y, light)))

    def post(self, x0, y0, x1, y1, base=0.5):
        """An upright board, lit from the left."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                t = (x - x0) / max(x1 - x0, 1)
                light = base + 0.2 - t * 0.35 + self.grain_at(900 + x, y // 2) * 0.6
                if y == y0:
                    light += 0.1
                self.put(x, y, shade(WOOD, dither(x, y, light)))

    def nail(self, x, y):
        self.put(x, y, IRON[6])
        self.put(x + 1, y + 1, IRON[2])

    def image(self):
        img = Image.new("P", (self.w, self.h), 0)
        img.putpalette(PALETTE)
        for (x, y), colour in self.px.items():
            img.putpixel((x, y), colour)
        return img


def cart_body():
    """The cart's bed, 48 by 20, facing right: two side boards between corner posts, a raised board at the front for the driver to brace
    against, and the frame under it with iron straps where the wheels hang."""
    c = Canvas(48, 20, 11)
    # The frame under the bed: a dark beam, with iron straps over it at the axles (the wheels hang at 14 either side of the middle).
    for x in range(4, 44):
        for y in (16, 17):
            light = 0.28 - (y - 16) * 0.12 + c.grain_at(700 + y, x) * 0.5
            c.put(x, y, shade(WOOD, dither(x, y, light, 0.04)))
    for axle in (10, 38):
        for x in range(axle - 2, axle + 3):
            for y in range(14, 19):
                light = 0.55 - (y - 14) * 0.1 - abs(x - axle) * 0.08
                c.put(x, y, shade(IRON, dither(x, y, light, 0.04)))
    # The side boards.
    c.plank(3, 6, 44, 10, base=0.6, seed_row=1)
    c.plank(3, 11, 44, 15, base=0.5, seed_row=2)
    # The front board, raised and leaning forward a little, and the corner posts.
    for y in range(0, 7):
        lean = (6 - y) // 3
        c.post(40 + lean, y, 44 + lean, y, base=0.55)
    c.post(3, 3, 5, 16, base=0.5)
    c.post(41, 4, 43, 16, base=0.45)
    # A cross brace on each board, and nails where the boards meet the posts.
    for i in range(0, 14):
        x, y = 9 + i * 2, 14 - i * 8 // 13
        if 6 <= y <= 15 and x < 40:
            c.put(x, y, shade(WOOD, 0.25))
            c.put(x + 1, y, shade(WOOD, 0.3))
    for x in (4, 42):
        for y in (8, 13):
            c.nail(x, y)
    # A dark outline along the bottom and the ends, as the base game's props have, to sit it on the ground.
    for (x, y), colour in list(c.px.items()):
        for dx, dy in ((1, 0), (-1, 0), (0, 1)):
            if (x + dx, y + dy) not in c.px and 0 <= x + dx < c.w and 0 <= y + dy < c.h and dy == 1:
                c.put(x + dx, y + dy, WOOD[0])
    return c.image()


def cart_wheel():
    """A wooden wheel, 15 across: an iron tyre, the wooden rim, six spokes and an iron hub. Lit from the upper left; it turns in the game,
    so the light turns with it, as it does on every turning thing in the base game."""
    size = 15
    c = Canvas(size, size, 5)
    mid = (size - 1) / 2
    for y in range(size):
        for x in range(size):
            dx, dy = x - mid, y - mid
            d = math.hypot(dx, dy)
            lit = (-dx - dy) / max(d, 0.001) * 0.15
            if d > 7.4:
                continue
            if d > 6.2:
                c.put(x, y, shade(IRON, dither(x, y, 0.45 + lit)))
            elif d > 4.8:
                c.put(x, y, shade(WOOD, dither(x, y, 0.55 + lit + c.grain_at(int(math.atan2(dy, dx) * 3), 0) * 0.6)))
            elif d < 1.6:
                c.put(x, y, shade(IRON, dither(x, y, 0.7 if d < 0.8 else 0.35)))
            else:
                # Six spokes.
                angle = math.atan2(dy, dx)
                off = min(abs(((angle - k * math.pi / 3) + math.pi) % (2 * math.pi) - math.pi) for k in range(6))
                if off * d < 0.85:
                    c.put(x, y, shade(WOOD, dither(x, y, 0.5 + lit)))
    return c.image()


def planks():
    """The pieces it breaks into: three lengths of board and a splinter."""
    pieces = []
    for n, (length, height) in enumerate(((16, 4), (11, 4), (7, 3))):
        c = Canvas(length, height, 30 + n)
        c.plank(0, 0, length - 1, height - 1, base=0.55, seed_row=n)
        # A broken end: a jagged edge on one side.
        for y in range(height):
            for x in range(length - 1 - c.rng.randint(0, 2), length):
                c.px.pop((x, y), None)
        if length > 10:
            c.nail(1, 1)
        pieces.append(c.image())
    c = Canvas(4, 2, 40)
    c.plank(0, 0, 3, 1, base=0.6, seed_row=4)
    c.px.pop((3, 1), None)
    pieces.append(c.image())
    return pieces


def icon(body, wheel):
    """The buy menu's picture: the body with its wheels on, as it sits on its springs."""
    img = Image.new("P", (body.width, body.height + 6), 0)
    img.putpalette(PALETTE)
    img.paste(body, (0, 0))
    mask = wheel.point(lambda i: 255 if i else 0, mode="1")
    for x in (24 - 14 - 7, 24 + 14 - 7):
        img.paste(wheel, (x, 12 + 6 - 7), mask)
    return img


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    body = cart_body()
    wheel = cart_wheel()
    body.save(OUT / "CartBody.png")
    wheel.save(OUT / "CartWheel.png")
    for name, piece in zip(("CartPlankA", "CartPlankB", "CartPlankC", "CartSplinter"), planks()):
        piece.save(OUT / f"{name}.png")
    icon(body, wheel).save(OUT / "CartIcon.png")


if __name__ == "__main__":
    main()
