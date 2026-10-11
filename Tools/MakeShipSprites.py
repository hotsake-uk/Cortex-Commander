"""Makes the flying ships' pictures (FS-1) in the look of the base game's art, as MakeVehicleSprites does: 8-bit in the game's palette,
index 0 the see-through mask colour, lit from the upper left, with a little dithering. The ships fly on the drop ship's engines, which
keep their own pictures.

The Gunship: a side-on hull, facing right, with an open cabin under its roof where the crew show from the waist up (the pilot at the
front behind a framed windscreen, a gunner at each end, passengers between), a tail boom with a fin, and the buy menu's icon.

The Frigate: a long armoured hull, facing right, with a raised bridge whose open window shows whoever stands at its controls, a deck
with a rail over its two gunners' posts, a hold under it, and the icon.

Run from the repository's root: python Tools/MakeShipSprites.py
"""

from pathlib import Path

from PIL import Image

from MakeVehicleSprites import CHROME, GLASS, IRON, LAMP, PAINT, PALETTE, Canvas, bolt, dither, line, panel, shade

GUNSHIP_OUT = Path("Data/Base.rte/Craft/Ships/Gunship")
FRIGATE_OUT = Path("Data/Base.rte/Craft/Ships/Frigate")

STEEL = [245, 246, 247, 248, 249, 250, 251, 94, 124]  # Dark to light greys.
OLIVE = [139, 141, 125, 123, 126, 128, 129, 130]

# The Gunship's size (keep in step with Gunship.ini): its picture and its middle in it, where the cabin's floor rail is (the crew show
# above it), and the roof over the cabin.
GUN_W, GUN_H = 168, 64
GUN_MID = (84, 36)
GUN_RAIL = 34  # The top of the hull's side under the cabin.
GUN_ROOF = (36, 140, 8, 13)  # Left, right, top, bottom.
GUN_POSTS = (36, 84, 120)

# The Frigate's size (keep in step with Frigate.ini).
FRI_W, FRI_H = 300, 112
FRI_MID = (150, 62)
FRI_DECK = 40  # The deck's top: its gunners stand on it, showing above the rail.
FRI_BRIDGE = (196, 246, 4, 40)  # Left, right, top, bottom of the bridge's block.


def outline(c, ramp):
    """A dark line under and round the hull, as the base game's craft have."""
    for (x, y), colour in list(c.px.items()):
        for dx, dy in ((0, 1), (1, 0), (-1, 0)):
            if (x + dx, y + dy) not in c.px and 0 <= x + dx < c.w and 0 <= y + dy < c.h:
                c.put(x + dx, y + dy, ramp[0])


def gunship_hull():
    c = Canvas(GUN_W, GUN_H, 71)
    left, right, top, bottom = GUN_ROOF
    # The hull under the cabin: a deep olive tub, rounded underneath, its nose sloping up to the windscreen.
    for y in range(GUN_RAIL, 56):
        sag = max(0, y - 50)
        x0 = 22 + sag * 3
        x1 = 150 - max(0, (y - GUN_RAIL) // 2) - sag * 3
        for x in range(x0, x1 + 1):
            light = 0.66 - (y - GUN_RAIL) * 0.02 - (x - x0) * 0.0008
            if y == GUN_RAIL:
                light += 0.2
            if y == 55 - 0 or x in (x0, x1):
                light -= 0.25
            c.put(x, y, shade(OLIVE, dither(x, y, light, 0.04)))
    # A stripe along it, panel lines and bolts.
    for x in range(26, 146):
        c.put(x, 41, PAINT[3] if x % 2 else PAINT[2])
        c.put(x, 42, PAINT[1])
    for x in (44, 70, 96, 122):
        for y in range(GUN_RAIL + 2, 54):
            c.put(x, y, OLIVE[1])
    for x in range(30, 146, 12):
        bolt(c, x, 46)
    # The nose: a sloped front that the windscreen sits on, and a lamp.
    for y in range(18, GUN_RAIL):
        reach = 140 + (y - 18)
        for x in range(right, min(reach, 160) + 1):
            if y > 26:
                light = 0.55 - (y - 18) * 0.015
                c.put(x, y, shade(OLIVE, dither(x, y, light, 0.04)))
    for y in range(GUN_RAIL, 50):
        for x in range(150 - (y - GUN_RAIL) // 2, min(162 - (y - GUN_RAIL), 160) + 1):
            c.put(x, y, shade(OLIVE, dither(x, y, 0.5 - (y - GUN_RAIL) * 0.02, 0.04)))
    for x, y in ((157, 36), (158, 36), (157, 37), (158, 37)):
        c.put(x, y, LAMP[1] if (x, y) == (157, 36) else LAMP[0])
    # The windscreen's frame over the pilot: a slanted bar from the roof to the nose, and a thin middle bar, see-through between.
    line(c, right, top, 152, 26, CHROME, 0.55, 2)
    line(c, right - 2, bottom, 146, 26, CHROME, 0.4, 1)
    for i in range(0, 12, 3):
        c.put(right + 2 + i, top + 2 + i, shade(GLASS, 0.85))
    # The roof over the cabin, with the engines' mounts at each end and a stub of mast on top.
    panel(c, left, top, right, bottom, OLIVE, 0.62)
    for x in range(left + 2, right - 1, 10):
        bolt(c, x, top + 3)
    for x0 in (left - 6, right - 2):
        panel(c, x0, top - 2, x0 + 10, bottom + 2, STEEL, 0.55)
    line(c, 88, top - 6, 88, top, STEEL, 0.6, 1)
    # The posts holding the roof up: the cabin is open between them.
    for x in GUN_POSTS:
        line(c, x, bottom, x, GUN_RAIL, STEEL, 0.5, 2)
    # The tail boom and its fin.
    for y in range(30, 42):
        x0 = 2 + max(0, 36 - y) // 2
        for x in range(x0, 24):
            light = 0.6 - (y - 30) * 0.03
            c.put(x, y, shade(OLIVE, dither(x, y, light, 0.04)))
    for y in range(10, 31):
        for x in range(2 + (y - 10) // 3, 14 + (y - 10) // 4):
            c.put(x, y, shade(OLIVE, dither(x, y, 0.58 - (x - 2) * 0.01, 0.04)))
    for y in range(14, 22):
        c.put(5 + (y - 10) // 3, y, PAINT[4])
    outline(c, OLIVE)
    return c.image()


def frigate_hull():
    c = Canvas(FRI_W, FRI_H, 81)
    # The long armoured hull: a deck at FRI_DECK, a keel that swells under the middle, a raked bow and a squared stern.
    for y in range(FRI_DECK, 98):
        swell = max(0, y - 84)
        x0 = 12 + swell * 2 + max(0, 50 - y)
        x1 = 290 - max(0, (y - FRI_DECK) // 3) - swell * 3
        for x in range(x0, x1 + 1):
            light = 0.86 - (y - FRI_DECK) * 0.008 - (x - x0) * 0.0005
            if y == FRI_DECK:
                light += 0.2
            if x in (x0, x1):
                light -= 0.2
            c.put(x, y, shade(STEEL, dither(x, y, light, 0.04)))
    # Armour belts and their rivets, a red-orange stripe, and portholes along it.
    for y in (56, 76):
        for x in range(16, 286):
            c.put(x, y, STEEL[2])
            c.put(x, y + 1, STEEL[6])
    for x in range(18, 286):
        c.put(x, 64, PAINT[3] if x % 2 else PAINT[2])
        c.put(x, 65, PAINT[1])
    for x in range(24, 280, 14):
        bolt(c, x, 59)
        bolt(c, x + 7, 80)
    for x in range(40, 260, 24):
        for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
            c.put(x + dx, 69 + dy, GLASS[3] if dx == dy == 0 else GLASS[1])
    # The hold's doors underneath, in the middle.
    for x in range(126, 176):
        c.put(x, 92, STEEL[1])
    c.put(150, 93, STEEL[1])
    # The deck's rail: a thin bar on posts, the gunners standing behind it.
    for x in range(20, 280):
        c.put(x, FRI_DECK - 14, CHROME[5] if x % 3 else CHROME[3])
    for x in range(20, 281, 10):
        line(c, x, FRI_DECK - 13, x, FRI_DECK - 1, CHROME, 0.45, 1)
    # The bridge: a raised block near the bow with an open window over its controls, and a mast with a lamp.
    left, right, top, bottom = FRI_BRIDGE
    panel(c, left, top, right, bottom, STEEL, 0.8)
    for y in range(top + 8, top + 22):
        for x in range(left + 8, right - 6):
            c.px.pop((x, y), None)
    line(c, left + 8, top + 7, right - 7, top + 7, CHROME, 0.6, 1)
    line(c, left + 7, top + 8, left + 7, top + 22, CHROME, 0.5, 1)
    line(c, right - 6, top + 8, right - 6, top + 22, CHROME, 0.5, 1)
    line(c, (left + right) // 2, top + 8, (left + right) // 2, top + 22, CHROME, 0.4, 1)
    line(c, right - 10, top, right - 10, 0, STEEL, 0.6, 1)
    c.put(right - 10, 0, LAMP[1])
    # Gun platforms on the deck, fore and aft.
    for gx in (90, 266):
        panel(c, gx - 8, FRI_DECK - 6, gx + 8, FRI_DECK - 1, STEEL, 0.7)
    outline(c, STEEL)
    return c.image()


def icon(hull, scale):
    """The buy menu's picture: the hull scaled down."""
    width = max(1, round(hull.width * scale))
    height = max(1, round(hull.height * scale))
    small = hull.resize((width, height), Image.NEAREST)
    small.putpalette(PALETTE)
    return small


def main():
    GUNSHIP_OUT.mkdir(parents=True, exist_ok=True)
    FRIGATE_OUT.mkdir(parents=True, exist_ok=True)
    gunship = gunship_hull()
    gunship.save(GUNSHIP_OUT / "GunshipHull.png")
    icon(gunship, 0.5).save(GUNSHIP_OUT / "GunshipIcon.png")
    frigate = frigate_hull()
    frigate.save(FRIGATE_OUT / "FrigateHull.png")
    icon(frigate, 0.3).save(FRIGATE_OUT / "FrigateIcon.png")


if __name__ == "__main__":
    main()
