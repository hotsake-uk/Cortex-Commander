"""Makes the sandbox's candle pictures (Paint > Plants > Candles) in the look of the base game's own art.

Each candle is two pictures of the same size, drawn one over the other where it is put down: CandleWaxNNN.png, the wax (painted in the
candle wax material), and CandleWickNNN.png, the wick standing out of its top (candle wick, which TerrainCandle lights and burns down).
Both are 8-bit in the game's palette, taken from a base game plant, with index 0 the see-through mask colour. The bottom row is set into
the ground. Some candles are a few candles fused at the foot, each with its own wick.

Run from the repository's root: python Tools/MakeCandleSprites.py
"""

import random
from pathlib import Path

from PIL import Image

OUT = Path("Data/Base.rte/Scenes/Objects/Candles")
PALETTE_FROM = Path("Data/Base.rte/Scenes/Objects/Plants/Plant010.png")

# Palette ramps, dark to light.
WHITE = [92, 95, 49, 51, 53, 97, 99]
IVORY = [64, 49, 78, 52, 97, 99]
RED = [6, 11, 12, 13, 38]
HONEY = [82, 65, 71, 73, 79, 115, 120]
WICK = [100, 88]  # Charred tip, then the cotton below it.


def shade(ramp, value):
    """The ramp's colour for a lightness from 0 to 1."""
    return ramp[max(0, min(len(ramp) - 1, int(value * len(ramp))))]


class Candle:
    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.wax = {}  # (x, y): (ramp, lightness)
        self.wick = set()

    def body(self, left, width, top, bottom, ramp, rounded=False, ribbed=False):
        """A column of wax from top to bottom (inclusive), lit from the upper left: lighter on the left, darker at the right and the edges,
        a highlight along the top."""
        half = (width - 1) / 2
        for x in range(left, left + width):
            across = (x - left - half) / max(half, 0.5)
            for y in range(top, bottom + 1):
                if rounded and width >= 5 and y == top and x in (left, left + width - 1):
                    continue
                light = 0.62 - across * 0.3
                if abs(across) > 0.75:
                    light -= 0.18
                if y == top or (rounded and y == top + 1 and x in (left, left + width - 1)):
                    light += 0.2
                if ribbed and (y - top) % 3 == 2:
                    light -= 0.14
                light += (self.rng.random() - 0.5) * 0.08
                self.wax[(x, y)] = (ramp, light)

    def drip(self, x, top, length, ramp):
        """An old drip of wax run down the outside of the candle and set, with a bead at its foot."""
        for y in range(top, top + length):
            self.wax.setdefault((x, y), (ramp, 0.5 + (0.12 if x % 2 else -0.05)))
        self.wax.setdefault((x, top + length), (ramp, 0.35))

    def dish(self, left, width, top, ramp):
        """A candle burnt down a way: its top melted into a dish, the rim standing up round it."""
        for x in range(left + 1, left + width - 1):
            self.wax.pop((x, top), None)
            self.wax[(x, top + 1)] = (ramp, 0.85)

    def add_wick(self, x, top, length):
        for y in range(top - length, top):
            self.wick.add((x, y))

    def pictures(self):
        points = list(self.wax) + list(self.wick)
        left = min(x for x, _ in points)
        right = max(x for x, _ in points)
        top = min(y for _, y in points)
        bottom = max(y for _, y in points)
        width = right - left + 1
        height = bottom - top + 1
        wax = Image.new("P", (width, height), 0)
        wick = Image.new("P", (width, height), 0)
        for (x, y), (ramp, light) in self.wax.items():
            # A little dithering, as the base game's art has.
            light += 0.03 if (x + y) % 2 else -0.03
            wax.putpixel((x - left, y - top), shade(ramp, light))
        tips = {}
        for x, y in self.wick:
            tips[x] = min(tips.get(x, y), y)
        for x, y in self.wick:
            wick.putpixel((x - left, y - top), WICK[0] if y == tips[x] else WICK[1])
        return wax, wick


def taper(seed, width, height, ramp, ribbed=False):
    candle = Candle(seed)
    candle.body(0, width, 0, height - 1, ramp, ribbed=ribbed)
    candle.add_wick(width // 2, 0, 3)
    return candle.pictures()


def pillar(seed, width, height, ramp, drips=0, burnt=False):
    candle = Candle(seed)
    rng = candle.rng
    candle.body(0, width, 0, height - 1, ramp, rounded=True)
    top = 0
    if burnt:
        candle.dish(0, width, 0, ramp)
        top = 1
    for _ in range(drips):
        side = rng.choice([-1, width])
        candle.drip(side, rng.randint(1, 2), rng.randint(2, height // 2), ramp)
    candle.add_wick(width // 2, top, 2)
    return candle.pictures()


def cluster(seed, ramp):
    """Three candles of different heights, their feet fused in the wax that ran down them."""
    candle = Candle(seed)
    rng = candle.rng
    parts = [(0, 3, 9), (4, 4, 18), (9, 3, 13)]
    base = 19
    for left, width, height in parts:
        top = base - height
        candle.body(left, width, top, base, ramp)
        candle.add_wick(left + width // 2, top, 2)
    # The pool of wax they stand in, two rows deep, filling the gaps between them.
    candle.body(-1, 14, base - 1, base, ramp)
    for x in (3, 8):
        candle.drip(x, base - rng.randint(3, 5), 2, ramp)
    return candle.pictures()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    palette = Image.open(PALETTE_FROM).getpalette()
    candles = [
        taper(1, 3, 22, WHITE),
        taper(2, 3, 20, RED),
        taper(3, 4, 26, IVORY),
        pillar(4, 8, 16, IVORY),
        pillar(5, 7, 12, RED, drips=2),
        pillar(6, 5, 8, HONEY, drips=1, burnt=True),
        pillar(7, 10, 20, WHITE, drips=3),
        cluster(8, IVORY),
        taper(9, 3, 18, HONEY, ribbed=True),
        pillar(10, 6, 10, WHITE, burnt=True),
    ]
    for index, (wax, wick) in enumerate(candles):
        for picture, name in ((wax, "CandleWax"), (wick, "CandleWick")):
            picture.putpalette(palette)
            picture.save(OUT / f"{name}{index:03d}.png")
    print(f"{len(candles)} candles written to {OUT}")


if __name__ == "__main__":
    main()
