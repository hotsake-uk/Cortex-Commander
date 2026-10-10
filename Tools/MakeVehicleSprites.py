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


# The cart's size: the body's picture, its middle in it, and where the wheels hang from the middle (keep in step with WoodenCart.ini).
BODY_W, BODY_H = 100, 36
BODY_MID = (50, 20)
AXLE_X = 30  # Either side of the middle: the bed reaches well past the wheels at both ends, for balance.
AXLE_Y = 16  # Below the middle, with the spring let all the way out.
WHEEL_SIZE = 27


def cart_body():
    """The cart's bed, facing right: three side boards between corner posts, a raised board at the front for the driver to brace against,
    and the frame under it with iron straps where the wheels hang."""
    c = Canvas(BODY_W, BODY_H, 11)
    left, right = 4, BODY_W - 5
    # The frame under the bed: a dark beam, with iron straps over it at the axles.
    for x in range(left + 4, right - 3):
        for y in (30, 31, 32):
            light = 0.3 - (y - 30) * 0.1 + c.grain_at(700 + y, x) * 0.5
            c.put(x, y, shade(WOOD, dither(x, y, light, 0.04)))
    for axle in (BODY_MID[0] - AXLE_X, BODY_MID[0] + AXLE_X):
        for x in range(axle - 3, axle + 4):
            for y in range(25, 34):
                light = 0.6 - (y - 25) * 0.06 - abs(x - axle) * 0.06
                c.put(x, y, shade(IRON, dither(x, y, light, 0.04)))
        c.nail(axle - 2, 26)
        c.nail(axle + 1, 26)
    # The side boards.
    c.plank(left, 10, right, 16, base=0.62, seed_row=1)
    c.plank(left, 17, right, 23, base=0.55, seed_row=2)
    c.plank(left, 24, right, 29, base=0.48, seed_row=3)
    # The front board, raised and leaning forward a little, and the corner posts and a middle post.
    for y in range(0, 11):
        lean = (10 - y) // 4
        c.post(right - 9 + lean, y, right + lean, y, base=0.55)
    c.post(left, 6, left + 3, 30, base=0.5)
    c.post(right - 4, 7, right - 1, 30, base=0.45)
    c.post(BODY_MID[0] - 1, 10, BODY_MID[0] + 1, 29, base=0.42)
    # Nails where the boards meet the posts.
    for x in (left + 1, right - 3, BODY_MID[0] - 1):
        for y in (13, 20, 26):
            c.nail(x, y)
    # A dark line under it, as the base game's props have, to sit it on the ground.
    for (x, y), colour in list(c.px.items()):
        if (x, y + 1) not in c.px and y + 1 < c.h:
            c.put(x, y + 1, WOOD[0])
    return c.image()


def cart_wheel():
    """A wooden wheel: an iron tyre, the wooden rim, eight spokes and an iron hub. Lit from the upper left; it turns in the game, so the light
    turns with it, as it does on every turning thing in the base game."""
    size = WHEEL_SIZE
    c = Canvas(size, size, 5)
    mid = (size - 1) / 2
    outer = mid + 0.4
    for y in range(size):
        for x in range(size):
            dx, dy = x - mid, y - mid
            d = math.hypot(dx, dy)
            lit = (-dx - dy) / max(d, 0.001) * 0.15
            if d > outer:
                continue
            if d > outer - 1.8:
                c.put(x, y, shade(IRON, dither(x, y, 0.5 + lit - (0.15 if d > outer - 0.6 else 0.0))))
            elif d > outer - 4.2:
                c.put(x, y, shade(WOOD, dither(x, y, 0.55 + lit + c.grain_at(int(math.atan2(dy, dx) * 4), 0) * 0.6)))
            elif d < 2.0:
                c.put(x, y, shade(IRON, dither(x, y, 0.75 if d < 1.0 else 0.4)))
            elif d < 3.4:
                c.put(x, y, shade(WOOD, dither(x, y, 0.4 + lit)))
            else:
                angle = math.atan2(dy, dx)
                off = min(abs(((angle - k * math.pi / 4) + math.pi) % (2 * math.pi) - math.pi) for k in range(8))
                if off * d < 1.0:
                    c.put(x, y, shade(WOOD, dither(x, y, 0.52 + lit)))
    return c.image()


def planks():
    """The pieces it breaks into: three lengths of board and a splinter."""
    pieces = []
    for n, (length, height) in enumerate(((26, 6), (17, 6), (10, 5))):
        c = Canvas(length, height, 30 + n)
        c.plank(0, 0, length - 1, height - 1, base=0.55, seed_row=n)
        # A broken end: a jagged edge on one side.
        for y in range(height):
            for x in range(length - 1 - c.rng.randint(0, 3), length):
                c.px.pop((x, y), None)
        if length > 12:
            c.nail(1, 2)
        pieces.append(c.image())
    c = Canvas(6, 2, 40)
    c.plank(0, 0, 5, 1, base=0.6, seed_row=4)
    c.px.pop((5, 1), None)
    pieces.append(c.image())
    return pieces


def icon(body, wheel):
    """The buy menu's picture: the body with its wheels on, as it sits on its springs."""
    rest = 4  # How far the springs are pushed in at rest, about.
    drop = AXLE_Y - rest + WHEEL_SIZE // 2 + 1
    img = Image.new("P", (body.width, BODY_MID[1] + drop), 0)
    img.putpalette(PALETTE)
    img.paste(body, (0, 0))
    mask = wheel.point(lambda i: 255 if i else 0, mode="1")
    for axle in (BODY_MID[0] - AXLE_X, BODY_MID[0] + AXLE_X):
        img.paste(wheel, (axle - WHEEL_SIZE // 2, BODY_MID[1] + AXLE_Y - rest - WHEEL_SIZE // 2), mask)
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
