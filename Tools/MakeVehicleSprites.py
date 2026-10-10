"""Makes the vehicles' pictures (VH-1) in the look of the base game's art: 8-bit in the game's palette, index 0 the see-through mask colour,
lit from the upper left, with a little dithering.

The wooden cart: its body (the bed, facing right), a spoked wheel with an iron tyre, the planks and splinters it breaks into, and the icon
for the buy menu (the body with its wheels on).

The Moonhopper: a six-wheeled rock hopper on tall telescoping legs, made to show off the sprung wheels. Its body (an open roll-cage
cab, an engine with a stack, shock towers and coil springs over the shocks), the leg each wheel hangs on, a fat knobbly tyre, and the icon.

Run from the repository's root: python Tools/MakeVehicleSprites.py
"""

import math
import random
from pathlib import Path

from PIL import Image

OUT = Path("Data/Base.rte/Actors/Vehicles/WoodenCart")
HOPPER_OUT = Path("Data/Base.rte/Actors/Vehicles/Moonhopper")
PALETTE = Image.open("Data/Base.rte/palette.bmp").getpalette()[:768]

# Palette ramps, dark to light.
WOOD = [21, 22, 54, 25, 58, 61, 62, 66, 67, 72]
IRON = [245, 246, 247, 248, 249, 250, 251, 94, 124]
CHROME = [247, 249, 251, 94, 124, 173, 183, 50, 174, 97]
PAINT = [54, 24, 30, 36, 71, 73, 77]  # The orange of the base game's orange panels (and the gibs it breaks into).
RUBBER = [245, 246, 247, 248, 249, 250]
GLASS = [192, 207, 208, 216, 210, 213]
LAMP = [116, 120]


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
AXLE_X = 24  # Either side of the middle: the bed reaches well past the wheels at both ends, for balance.
AXLE_Y = 19  # Below the middle, with the spring let all the way out: the body rides well clear of the ground, the wheels reaching up its side.
WHEEL_SIZE = 31


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
    rest = 2  # How far the springs are pushed in at rest, about.
    drop = AXLE_Y - rest + WHEEL_SIZE // 2 + 1
    img = Image.new("P", (body.width, BODY_MID[1] + drop), 0)
    img.putpalette(PALETTE)
    img.paste(body, (0, 0))
    mask = wheel.point(lambda i: 255 if i else 0, mode="1")
    for axle in (BODY_MID[0] - AXLE_X, BODY_MID[0] + AXLE_X):
        img.paste(wheel, (axle - WHEEL_SIZE // 2, BODY_MID[1] + AXLE_Y - rest - WHEEL_SIZE // 2), mask)
    return img


# The Moonhopper's size (keep in step with Moonhopper.ini): the body's picture and its middle in it, where the three legs come down from
# the middle, how low each leg's wheel hangs with the spring let all the way out, and how far the springs travel.
HOP_W, HOP_H = 100, 38
HOP_MID = (50, 24)
HOP_LEGS = (-36, 0, 36)
HOP_HANG = 48
HOP_TRAVEL = 26
HOP_WHEEL = 30
HOP_STRUT_LEN = 38
HOP_STRUT_HUB = 35  # Where the hub's eye is, down the leg's picture.


def line(c, x0, y0, x1, y1, ramp, light, width=1):
    """A tube from one point to another, lit from above: a light top, a dark underside."""
    steps = max(abs(x1 - x0), abs(y1 - y0), 1)
    for i in range(steps + 1):
        x = round(x0 + (x1 - x0) * i / steps)
        y = round(y0 + (y1 - y0) * i / steps)
        for w in range(width):
            c.put(x, y + w, shade(ramp, dither(x, y + w, light + 0.2 - w * 0.25 / max(width - 1, 1), 0.03)))


def panel(c, x0, y0, x1, y1, ramp, base, slope=None):
    """A painted plate lit from the upper left, with a light top edge and a dark rim. slope(y) gives how far the right edge comes in."""
    for y in range(y0, y1 + 1):
        right = x1 - (slope(y) if slope else 0)
        for x in range(x0, right + 1):
            light = base + 0.12 - (y - y0) / max(y1 - y0, 1) * 0.25 - (x - x0) / max(x1 - x0, 1) * 0.08
            if y == y0 or x == right:
                light += 0.18
            if y == y1 or x == x0:
                light -= 0.3
            c.put(x, y, shade(ramp, dither(x, y, light, 0.04)))


def bolt(c, x, y):
    c.put(x, y, CHROME[6])
    c.put(x + 1, y + 1, CHROME[1])


def hopper_body():
    """The Moonhopper's body, facing right: a tube chassis with a shock tower over each end leg and a shock and coil spring hanging under
    each leg (the legs slide up into them), an open roll-cage tub in the middle, the engine and its stack at the back, a nose, a bull bar
    and a lamp at the front, and a whip aerial with a pennant."""
    c = Canvas(HOP_W, HOP_H, 21)
    mx, my = HOP_MID
    legs = [mx + leg for leg in HOP_LEGS]
    # The chassis: a lower rail, an upper rail, and a truss between them.
    line(c, 8, 30, 92, 30, IRON, 0.45, 3)
    line(c, 14, 24, 86, 24, IRON, 0.45, 2)
    for x in range(18, 84, 12):
        line(c, x, 25, x + 6, 30, IRON, 0.35, 2)
        line(c, x + 6, 25, x + 12, 30, IRON, 0.35, 2)
    # The shocks over each leg: a dark body, a bright cap, and an orange coil spring round it.
    for x in legs:
        top = 26 if x != mx else 29
        for y in range(top, 37):
            for dx in range(-2, 3):
                light = 0.55 - abs(dx + 0.7) * 0.15 - (y - top) * 0.01
                c.put(x + dx, y, shade(IRON, dither(x + dx, y, light, 0.03)))
        for dx in range(-3, 4):
            c.put(x + dx, 36, IRON[1])
            c.put(x + dx, top, CHROME[7] if abs(dx) < 2 else CHROME[4])
        for y in range(top + 2, 35, 3):
            for dx in range(-3, 4):
                c.put(x + dx, y + (1 if dx > 0 else 0), shade(PAINT, 0.85 - abs(dx) * 0.08) if dx < 1 else shade(PAINT, 0.55))
    # The shock towers over the end legs.
    for x in (legs[0], legs[2]):
        panel(c, x - 5, 8, x + 5, 27, PAINT, 0.6)
        for y in (10, 25):
            bolt(c, x - 3, y)
            bolt(c, x + 2, y)
        for y in range(13, 23):
            c.put(x, y, PAINT[1])
    # The engine at the back, between the rear tower and the tub: a dark block with fins, and a tall chrome stack with a soot-black tip.
    for y in range(13, 24):
        for x in range(20, 35):
            light = 0.4 - (y - 13) * 0.02 + (0.15 if y % 3 == 0 else 0.0) - (x - 20) * 0.006
            c.put(x, y, shade(IRON, dither(x, y, light, 0.03)))
    for x in range(21, 34, 3):
        c.put(x, 12, CHROME[5])
    for y in range(0, 14):
        for dx in range(3):
            c.put(25 + dx, y, shade(CHROME, 0.75 - dx * 0.25) if y > 2 else RUBBER[1])
    # The tub the driver sits in, its front sloping down to the nose.
    panel(c, 36, 14, 72, 29, PAINT, 0.62)
    panel(c, 72, 18, 93, 29, PAINT, 0.55, slope=lambda y: max(0, 24 - y))
    for x in range(37, 92):
        c.put(x, 22, PAINT[1] if x % 2 else PAINT[0])
    for x in (40, 52, 64, 76, 88):
        bolt(c, x, 26)
    # The roll cage over the tub.
    line(c, 38, 13, 41, 0, CHROME, 0.55, 2)
    line(c, 70, 13, 66, 0, CHROME, 0.55, 2)
    line(c, 41, 0, 66, 0, CHROME, 0.6, 2)
    line(c, 41, 1, 70, 13, CHROME, 0.4, 1)
    # A windscreen at the front of the cage, leaning back.
    for y in range(4, 14):
        lean = (13 - y) // 3
        for x in range(70, 72):
            c.put(x - lean, y, shade(GLASS, 0.75 - (y - 4) * 0.04 - (x - 70) * 0.2))
    # The bull bar and the lamp.
    line(c, 94, 15, 94, 31, CHROME, 0.6, 1)
    line(c, 95, 15, 95, 31, CHROME, 0.35, 1)
    line(c, 89, 15, 95, 15, CHROME, 0.65, 1)
    for x, y in ((90, 19), (91, 19), (90, 20), (91, 20)):
        c.put(x, y, LAMP[1] if (x, y) == (90, 19) else LAMP[0])
    # A whip aerial off the back tower, with a little red pennant at the top.
    line(c, 10, 9, 4, 0, RUBBER, 0.3, 1)
    for y in range(0, 4):
        for x in range(5, 11 - y * 2):
            c.put(x, y, (12, 13, 38)[(x + y) % 3])
    return c.image()


def hopper_strut():
    """A leg: a polished piston rod with an eye at the bottom for the wheel's axle. Drawn behind the body, it slides up into the shock."""
    c = Canvas(7, HOP_STRUT_LEN, 22)
    for y in range(0, HOP_STRUT_HUB - 2):
        for x, light in ((2, 0.9), (3, 0.65), (4, 0.35)):
            c.put(x, y, shade(CHROME, dither(x, y, light, 0.03)))
    for y in range(HOP_STRUT_HUB - 3, HOP_STRUT_LEN):
        for x in range(7):
            d = abs(x - 3) + abs(y - HOP_STRUT_HUB)
            if d <= 3:
                c.put(x, y, shade(IRON, 0.65 - d * 0.1 - (y - HOP_STRUT_HUB) * 0.05))
    c.put(3, HOP_STRUT_HUB, IRON[0])
    return c.image()


def hopper_wheel():
    """A fat knobbly tyre: square tread blocks round the outside, an orange rim with bolts, and a chrome hub. Lit from the upper left."""
    size = HOP_WHEEL
    c = Canvas(size, size, 23)
    mid = (size - 1) / 2
    outer = mid + 0.4
    for y in range(size):
        for x in range(size):
            dx, dy = x - mid, y - mid
            d = math.hypot(dx, dy)
            angle = math.atan2(dy, dx)
            lit = (-dx - dy) / max(d, 0.001) * 0.15
            knob = (angle * 8 / math.pi) % 2 < 1.0  # Sixteen tread blocks round it.
            if d > outer or (d > outer - 1.6 and not knob):
                continue
            if d > outer - 6.0:
                light = 0.45 + lit - (0.2 if d > outer - 1.6 else 0.0) - (0.15 if d < outer - 5.0 else 0.0)
                c.put(x, y, shade(RUBBER, dither(x, y, light, 0.04)))
            elif d > outer - 9.5:
                light = 0.6 + lit * 1.5 - (0.25 if d > outer - 6.8 else 0.0)
                c.put(x, y, shade(PAINT, dither(x, y, light, 0.03)))
            elif d < 2.5:
                c.put(x, y, shade(CHROME, 0.85 if d < 1.2 else 0.5))
            else:
                c.put(x, y, shade(IRON, dither(x, y, 0.35 + lit, 0.03)))
    for k in range(5):
        a = k * 2 * math.pi / 5
        c.put(round(mid + math.cos(a) * 4.2), round(mid + math.sin(a) * 4.2), CHROME[7])
    return c.image()


def hopper_icon(body, strut, wheel):
    """The buy menu's picture: the body on its legs, its springs pushed half in as it stands."""
    hub = HOP_MID[1] + HOP_HANG - HOP_TRAVEL // 2
    img = Image.new("P", (HOP_W, hub + HOP_WHEEL // 2 + 1), 0)
    img.putpalette(PALETTE)

    def paste(picture, x, y):
        img.paste(picture, (x, y), picture.point(lambda i: 255 if i else 0, mode="1"))

    for leg in HOP_LEGS:
        paste(strut, HOP_MID[0] + leg - 3, hub - HOP_STRUT_HUB)
    paste(body, 0, 0)
    for leg in HOP_LEGS:
        paste(wheel, HOP_MID[0] + leg - HOP_WHEEL // 2, hub - HOP_WHEEL // 2)
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

    HOPPER_OUT.mkdir(parents=True, exist_ok=True)
    body = hopper_body()
    strut = hopper_strut()
    wheel = hopper_wheel()
    body.save(HOPPER_OUT / "HopperBody.png")
    strut.save(HOPPER_OUT / "HopperLeg.png")
    wheel.save(HOPPER_OUT / "HopperWheel.png")
    hopper_icon(body, strut, wheel).save(HOPPER_OUT / "HopperIcon.png")


if __name__ == "__main__":
    main()
