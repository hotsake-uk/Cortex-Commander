"""Makes the Medieval catapult's pictures in the look of the vehicles' art (Tools/MakeVehicleSprites.py, whose helpers it uses): 8-bit in the
game's palette, index 0 the see-through mask colour, lit from the upper left, with a little dithering.

The catapult: its frame (two long beams on the cart's spoked wheels, an A-frame with a padded crossbar the arm strikes, and the winch at the
back), the throwing arm with its bucket (lying back at rest, turning about its right-hand end), and the icon for the buy menu. What it throws:
a boulder and the stone chunks it breaks into, and a clay pot of burning pitch, stopped with a rag, and the shards it breaks into.

Run from the repository's root: python Tools/MakeCatapultSprites.py
"""

import math
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
from MakeVehicleSprites import IRON, PALETTE, WOOD, Canvas, cart_wheel, dither, line, shade  # noqa: E402

OUT = Path("Data/Medieval.rte/Actors/Vehicles/Catapult")

STONE = [247, 248, 249, 250, 251, 94, 124, 173, 183]
CLAY = [6, 7, 26, 28, 31, 33, 34, 36, 37]
RAG = [58, 61, 67, 72, 50]

# The catapult's size (keep in step with Catapult.ini): the frame's picture and its middle in it, where the wheels hang from the middle, where
# the arm turns from the middle, and the arm's picture and how far its bucket is from where it turns.
FRAME_W, FRAME_H = 124, 60
FRAME_MID = (62, 36)
AXLE_X = 36
AXLE_Y = 18
WHEEL_SIZE = 31
ARM_PIVOT = (18, -1)  # From the frame's middle.
ARM_W, ARM_H = 66, 14
ARM_TURN = (62, 7)  # Where it turns, in its picture.
ARM_REACH = 54  # From where it turns to the bucket's middle.


def frame():
    """The catapult's frame, facing right: two long beams with iron straps where the wheels hang, the arm's axle block, an A-frame up to a
    padded crossbar, and the winch drum at the back with its spokes."""
    c = Canvas(FRAME_W, FRAME_H, 21)
    mx, my = FRAME_MID
    # The long beams, the upper one a little shorter.
    c.plank(4, my + 8, FRAME_W - 5, my + 13, base=0.5, seed_row=1)
    c.plank(10, my + 3, FRAME_W - 12, my + 7, base=0.58, seed_row=2)
    # Iron straps at the axles.
    for axle in (mx - AXLE_X, mx + AXLE_X):
        for x in range(axle - 3, axle + 4):
            for y in range(my + 6, my + 16):
                c.put(x, y, shade(IRON, dither(x, y, 0.6 - (y - my - 6) * 0.05 - abs(x - axle) * 0.06, 0.04)))
        c.nail(axle - 2, my + 8)
        c.nail(axle + 1, my + 8)
    # The A-frame: two slanted posts up to the crossbar, the padded crossbar over them.
    top = my - 30
    px = mx + ARM_PIVOT[0]
    line(c, px - 6, my + 3, px + 4, top + 4, WOOD, 0.45, width=4)
    line(c, px + 24, my + 3, px + 10, top + 4, WOOD, 0.4, width=4)
    c.plank(px - 2, top, px + 16, top + 5, base=0.55, seed_row=3)
    # The pad the arm strikes: a stuffed leather roll on the back of the crossbar.
    for y in range(top - 1, top + 7):
        for x in range(px - 6, px - 1):
            d = math.hypot((x - (px - 3.5)) / 2.6, (y - (top + 3)) / 4.2)
            if d <= 1.0:
                c.put(x, y, shade(RAG, dither(x, y, 0.55 - d * 0.35 - (y - top) * 0.03, 0.04)))
    # A brace across the A-frame.
    line(c, px - 2, my - 8, px + 19, my - 8, WOOD, 0.35, width=2)
    # The arm's axle block on the beams, with its iron pin.
    py = my + ARM_PIVOT[1]
    c.post(px - 5, py - 3, px + 5, my + 3, base=0.4)
    for y in range(py - 1, py + 2):
        for x in range(px - 1, px + 2):
            c.put(x, y, shade(IRON, dither(x, y, 0.7)))
    # The winch at the back: a post either side and the drum with its handle spokes.
    wx, wy = mx - 50, my - 5
    c.post(wx - 6, wy - 2, wx - 4, my + 3, base=0.5)
    c.post(wx + 4, wy - 2, wx + 6, my + 3, base=0.42)
    for y in range(wy - 4, wy + 5):
        for x in range(wx - 4, wx + 5):
            d = math.hypot(x - wx, y - wy)
            if d <= 4.4:
                light = 0.55 - (y - wy) * 0.05 - (x - wx) * 0.02
                if d > 3.5:
                    light -= 0.25
                c.put(x, y, shade(WOOD, dither(x, y, light)))
    for angle in (0.3, 0.3 + math.pi / 2, 0.3 + math.pi, 0.3 + 3 * math.pi / 2):
        line(c, wx, wy, round(wx + math.cos(angle) * 8), round(wy + math.sin(angle) * 8), WOOD, 0.5)
    for y in range(wy - 1, wy + 2):
        for x in range(wx - 1, wx + 2):
            c.put(x, y, shade(IRON, 0.6))
    # A dark line under it, as the base game's props have, to sit it on the ground.
    for (x, y), colour in list(c.px.items()):
        if (x, y + 1) not in c.px and y + 1 < c.h:
            c.put(x, y + 1, WOOD[0])
    return c.image()


def arm():
    """The throwing arm, lying back from where it turns (the right-hand end, an iron-bound eye) to the bucket at the left: a tapering beam
    with rope lashings, the bucket a hollowed wooden cup."""
    c = Canvas(ARM_W, ARM_H, 23)
    tx, ty = ARM_TURN
    bucket_x = tx - ARM_REACH
    # The beam, thicker at the eye.
    for x in range(bucket_x + 4, tx + 1):
        t = (x - bucket_x) / (tx - bucket_x)
        half = 1 + round(t * 1.4)
        for y in range(ty - half, ty + half + 1):
            light = 0.62 - (y - (ty - half)) / max(2 * half, 1) * 0.35 + c.grain_at(y, x) * 0.6
            c.put(x, y, shade(WOOD, dither(x, y, light)))
    # Rope lashings along it.
    for x in (bucket_x + 14, bucket_x + 30, tx - 10):
        for y in range(ty - 3, ty + 4):
            if (x, y) in c.px:
                c.put(x, y, shade(RAG, dither(x, y, 0.45 - (y - ty) * 0.06)))
                c.put(x + 1, y, shade(RAG, dither(x + 1, y, 0.3)))
    # The iron-bound eye where it turns.
    for y in range(ty - 3, ty + 4):
        for x in range(tx - 3, tx + 4):
            d = math.hypot(x - tx, y - ty)
            if d <= 3.4:
                c.put(x, y, shade(IRON, dither(x, y, 0.75 if d < 1.2 else 0.45 - (y - ty) * 0.05)))
    # The bucket: a cup open at the top, its rim lit.
    for y in range(ty - 6, ty + 6):
        for x in range(bucket_x - 6, bucket_x + 7):
            d = math.hypot((x - bucket_x) / 6.4, (y - ty + 1) / 5.6)
            if d > 1.0 or y < ty - 4:
                continue
            inside = math.hypot((x - bucket_x) / 4.4, (y - ty + 2) / 3.8) < 1.0 and y < ty + 1
            light = 0.2 if inside else 0.6 - (y - ty) * 0.05 - (x - bucket_x) * 0.015
            c.put(x, y, shade(WOOD, dither(x, y, light)))
    for x in range(bucket_x - 5, bucket_x + 6):
        c.put(x, ty - 4, shade(WOOD, 0.75))
    c.nail(bucket_x - 4, ty + 1)
    c.nail(bucket_x + 3, ty + 1)
    return c.image()


def icon(body, wheel, beam):
    """The buy menu's picture: the frame with its wheels on and the arm lying back, as it sits on its springs."""
    rest = 3
    img = Image.new("P", (body.width, FRAME_MID[1] + AXLE_Y - rest + WHEEL_SIZE // 2 + 1), 0)
    img.putpalette(PALETTE)
    img.paste(body, (0, 0))

    def paste(picture, x, y):
        img.paste(picture, (x, y), picture.point(lambda i: 255 if i else 0, mode="1"))

    paste(beam, FRAME_MID[0] + ARM_PIVOT[0] - ARM_TURN[0], FRAME_MID[1] + ARM_PIVOT[1] - ARM_TURN[1])
    for axle in (FRAME_MID[0] - AXLE_X, FRAME_MID[0] + AXLE_X):
        paste(wheel, axle - WHEEL_SIZE // 2, FRAME_MID[1] + AXLE_Y - rest - WHEEL_SIZE // 2)
    return img


def rock(size, seed, roundness=0.8):
    """A rough stone, lit from the upper left: lumpy round, darker underneath, with a few cracks."""
    c = Canvas(size, size, seed)
    mid = (size - 1) / 2
    bumps = [c.rng.uniform(roundness, 1.0) for _ in range(8)]
    for y in range(size):
        for x in range(size):
            dx, dy = x - mid, y - mid
            angle = math.atan2(dy, dx)
            k = (angle + math.pi) / (2 * math.pi) * 8
            i = int(k) % 8
            f = k - int(k)
            radius = (bumps[i] * (1 - f) + bumps[(i + 1) % 8] * f) * (mid + 0.5)
            d = math.hypot(dx, dy)
            if d > radius:
                continue
            light = 0.55 - (dx + dy) / max(size, 1) * 0.6 - (d / max(radius, 0.01)) ** 3 * 0.25 + c.rng.uniform(-0.06, 0.06)
            c.put(x, y, shade(STONE, dither(x, y, light, 0.04)))
    for _ in range(max(1, size // 5)):
        x, y = c.rng.randint(1, size - 2), c.rng.randint(1, size - 2)
        for step in range(size // 3):
            if (x, y) in c.px:
                c.put(x, y, STONE[1])
            x += c.rng.choice((-1, 0, 1))
            y += 1
    return c.image()


def pot():
    """A clay pot of burning pitch, round-bellied with a short neck, stopped with a rag."""
    w, h = 11, 13
    c = Canvas(w, h, 31)
    mid = (w - 1) / 2
    for y in range(3, h):
        for x in range(w):
            dx = x - mid
            belly = 5.2 if y >= 6 else 2.6 + (y - 3) * 0.35
            if abs(dx) > belly - (0.0 if y < h - 2 else (y - (h - 3)) * 1.5):
                continue
            light = 0.62 - dx / w * 0.8 - (y - 3) / h * 0.4
            if y == 6:
                light += 0.12
            c.put(x, y, shade(CLAY, dither(x, y, light, 0.04)))
    # A band painted round the belly.
    for x in range(w):
        if (x, 8) in c.px:
            c.put(x, 8, shade(CLAY, 0.15))
    # The rag stuffed in the neck, its end hanging over.
    for y in range(0, 4):
        for x in range(int(mid) - 1, int(mid) + 2 + (1 if y < 2 else 0)):
            c.put(x, y, shade(RAG, dither(x, y, 0.75 - y * 0.12)))
    c.put(int(mid) + 3, 2, shade(RAG, 0.4))
    c.put(int(mid) + 3, 3, shade(RAG, 0.3))
    return c.image()


def shard(seed):
    """A broken piece of the pot."""
    c = Canvas(5, 4, seed)
    for y in range(4):
        for x in range(5):
            if c.rng.random() < 0.85 and not (y == 3 and x in (0, 4)) and not (y == 0 and x == 4):
                c.put(x, y, shade(CLAY, dither(x, y, 0.55 - y * 0.1)))
    return c.image()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    body = frame()
    beam = arm()
    body.save(OUT / "CatapultFrame.png")
    beam.save(OUT / "CatapultArm.png")
    icon(body, cart_wheel(), beam).save(OUT / "CatapultIcon.png")
    rock(15, 41).save(OUT / "Boulder.png")
    for name, size, seed in (("RockChunkA", 8, 42), ("RockChunkB", 6, 43), ("RockChunkC", 4, 44)):
        rock(size, seed, roundness=0.6).save(OUT / f"{name}.png")
    pot().save(OUT / "FirePot.png")
    shard(45).save(OUT / "PotShardA.png")
    shard(46).save(OUT / "PotShardB.png")


if __name__ == "__main__":
    main()
