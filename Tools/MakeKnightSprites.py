"""Makes the Imperial Knight's pictures (the first mech) from Liam's sprite sheet, Tools/Mechs/ImperialKnightSheet.png, and the assembled
front view, Tools/Mechs/ImperialKnightFront.png (the catalogue icon).

The sheet is drawn at twice the game's size on a magenta background. Each part is cut out of it by its place on the sheet, shrunk to half
(each 2x2 block is see-through when two or more of its pixels are, else the average of its solid pixels) and put into the game's palette
(8-bit, index 0 the see-through mask colour). The knight faces right, the way the game's pictures do, with the cockpit at the front.

- KnightHull.png: the carapace and cockpit, its groin plate and both pauldrons (the far one behind, the near one in front), as the side view
  on the sheet has them.
- KnightThighFG/BG.png: the thigh bone with its armour plate over it, from the hip down to the knee. The far leg (BG) is shaded darker.
- KnightShinFG/BG.png: the shin, from the knee down to the ankle.
- KnightFootFG/BG.png: the armoured foot, the ankle at its top.
- KnightBattleCannon.png: a stand-in for the battle cannon until its art arrives (a blue and gold housing and a long grey barrel).
- KnightHeavyStubber.png: a stand-in for the carapace gun until its art arrives (a small dark gun).
- KnightChainsword.png: a stand-in for the reaper chainsword until its art arrives (a grey housing and a long toothed blade).

The sheet has no weapon art: the red block on it (twice, by the side view and under the front views) isn't part of the knight, so it isn't used.
- KnightIcon.png: the front view, shrunk to catalogue size.

It prints the joints each part's INI offsets are worked out from (hip, knee, ankle, pivots and muzzles), in the shrunk pictures' pixels.

Run from the repository's root: python Tools/MakeKnightSprites.py
"""

from pathlib import Path

from PIL import Image

SHEET = Image.open("Tools/Mechs/ImperialKnightSheet.png").convert("RGB")
FRONT = Image.open("Tools/Mechs/ImperialKnightFront.png").convert("RGB")
OUT = Path("Data/Base.rte/Actors/Mechs/ImperialKnight")
PALETTE = Image.open("Data/Base.rte/palette.bmp").getpalette()[:768]
COLOURS = [tuple(PALETTE[i * 3:i * 3 + 3]) for i in range(256)]
FAR_SHADE = 0.72  # How much of its lightness the far leg keeps.

# Parts on the sheet: (x, y, width, height), in the sheet's pixels.
HULL = (1198, 129, 168, 187)
PAULDRON_FAR = (256, 4, 78, 51)
PAULDRON_NEAR = (334, 4, 66, 51)
GROIN = (1045, 82, 57, 69)
THIGH_BONE = (931, 1, 51, 99)
THIGH_ARMOUR = (985, 22, 45, 90)
SHIN_AND_FOOT = (805, 67, 81, 153)
# Where the side view on the sheet has the pauldrons and the groin plate, against the hull's corner.
PAULDRON_FAR_AT = (1162 - HULL[0], 136 - HULL[1])
PAULDRON_NEAR_AT = (1345 - HULL[0], 136 - HULL[1])
GROIN_AT = (1285 - HULL[0], 268 - HULL[1])
THIGH_ARMOUR_AT = (7, 18)  # On the thigh bone, as the whole leg on the sheet has it.
SHIN_ANKLE_ROW = 106  # Where the shin ends and the foot begins, in the shin-and-foot part.


def is_mask(colour):
    r, g, b = colour[:3]
    return abs(r - 255) < 40 and g < 60 and abs(b - 255) < 40


def cut(box, source=SHEET):
    x, y, w, h = box
    return source.crop((x, y, x + w, y + h))


def only_largest(img):
    """Keeps the largest solid piece of a cut, so a neighbour poking into its box isn't taken with it."""
    w, h = img.size
    px = img.load()
    seen = [[False] * w for _ in range(h)]
    best = []
    for sy in range(h):
        for sx in range(w):
            if seen[sy][sx] or is_mask(px[sx, sy]):
                continue
            stack = [(sx, sy)]
            seen[sy][sx] = True
            piece = []
            while stack:
                x, y = stack.pop()
                piece.append((x, y))
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx] and not is_mask(px[nx, ny]):
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if len(piece) > len(best):
                best = piece
    out = Image.new("RGB", (w, h), (255, 0, 255))
    opx = out.load()
    for x, y in best:
        opx[x, y] = px[x, y]
    return out


def paste_over(base, part, at):
    """Pastes a part's solid pixels over a picture (magenta is see-through), growing the picture to fit."""
    left = min(0, at[0])
    top = min(0, at[1])
    right = max(base.width, at[0] + part.width)
    bottom = max(base.height, at[1] + part.height)
    out = Image.new("RGB", (right - left, bottom - top), (255, 0, 255))
    out.paste(base, (-left, -top))
    opx = out.load()
    ppx = part.load()
    for y in range(part.height):
        for x in range(part.width):
            if not is_mask(ppx[x, y]):
                opx[x + at[0] - left, y + at[1] - top] = ppx[x, y]
    return out, (-left, -top)


def nearest(colour):
    best = 1
    best_d = 1 << 30
    for i in range(1, 256):
        c = COLOURS[i]
        if is_mask(c):
            continue
        d = (c[0] - colour[0]) ** 2 * 3 + (c[1] - colour[1]) ** 2 * 4 + (c[2] - colour[2]) ** 2 * 2
        if d < best_d:
            best_d = d
            best = i
    return best


_nearest_cache = {}


def to_palette(img, shade=1.0, scale=2):
    """Shrinks a picture by the scale and puts it in the game's palette."""
    w, h = (img.width + scale - 1) // scale, (img.height + scale - 1) // scale
    out = Image.new("P", (w, h), 0)
    out.putpalette(PALETTE)
    px = img.load()
    opx = out.load()
    for by in range(h):
        for bx in range(w):
            solid = []
            for dy in range(scale):
                for dx in range(scale):
                    x, y = bx * scale + dx, by * scale + dy
                    if x < img.width and y < img.height and not is_mask(px[x, y]):
                        solid.append(px[x, y])
            if len(solid) * 2 < scale * scale:
                continue
            colour = tuple(min(255, int(sum(c[i] for c in solid) / len(solid) * shade)) for i in range(3))
            key = colour
            if key not in _nearest_cache:
                _nearest_cache[key] = nearest(colour)
            opx[bx, by] = _nearest_cache[key]
    return out


def mirror(img):
    return img.transpose(Image.FLIP_LEFT_RIGHT)


def half(point):
    return (point[0] // 2, point[1] // 2)


def battle_cannon():
    """The stand-in battle cannon: a blue housing trimmed in gold at the shoulder end and a long grey barrel with a muzzle brake, pointing
    right. Drawn at the game's size (the shoulder joint at (13, 13))."""
    w, h = 72, 26
    img = Image.new("RGB", (w, h), (255, 0, 255))
    px = img.load()
    for y in range(1, 25):
        for x in range(0, 27):
            edge = y in (1, 24) or x in (0, 26)
            trim = y in (2, 23) or x in (1, 25)
            px[x, y] = (30, 30, 40) if edge else (200, 150, 40) if trim else (40, 80, 190) if (x + y) % 11 else (30, 60, 150)
    for y in range(8, 18):
        for x in range(27, 64):
            px[x, y] = (30, 30, 30) if y in (8, 17) else (130, 132, 138) if y < 13 else (96, 98, 104)
    for y in range(5, 21):
        for x in range(64, 72):
            px[x, y] = (30, 30, 30) if y in (5, 20) or x in (64, 71) else (80, 82, 88)
    for y in range(11, 15):
        px[71, y] = (10, 10, 10)
    return img


def heavy_stubber():
    """The stand-in heavy stubber: a small dark gun body and a thin barrel, pointing right. Drawn at the game's size (its mount at (6, 6))."""
    w, h = 34, 12
    img = Image.new("RGB", (w, h), (255, 0, 255))
    px = img.load()
    for y in range(1, 11):
        for x in range(0, 18):
            px[x, y] = (20, 20, 22) if y in (1, 10) or x in (0, 17) else (60, 62, 68) if y < 6 else (45, 46, 52)
    for y in range(4, 8):
        for x in range(18, 34):
            px[x, y] = (20, 20, 22) if y in (4, 7) else (110, 112, 118)
    return img


def chainsword():
    """The stand-in reaper chainsword: a grey housing at the shoulder end and a long blade with yellow and black teeth, pointing right."""
    w, h = 136, 30
    img = Image.new("RGB", (w, h), (255, 0, 255))
    px = img.load()
    for y in range(4, 26):
        for x in range(0, 30):
            edge = y in (4, 25) or x in (0, 29)
            px[x, y] = (40, 40, 44) if edge else (96, 98, 104) if (x + y) % 9 else (70, 72, 78)
    for y in range(9, 21):
        for x in range(30, 126):
            if y in (9, 20):
                px[x, y] = (30, 30, 30)
            else:
                px[x, y] = (150, 152, 158) if y < 15 else (118, 120, 126)
    for x in range(30, 126, 6):
        for k in range(3):
            for y in (6 + k, 23 - k):
                if x + k < 128:
                    px[x + k, y] = (220, 180, 30) if (x // 6) % 2 else (25, 25, 25)
    for y in range(11, 19):
        for x in range(126, 126 + (8 - abs(y - 15))):
            px[x, y] = (150, 152, 158)
    return img


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    joints = {}

    # The hull, with the far pauldron behind it and the groin plate and near pauldron in front.
    hull = Image.new("RGB", (1, 1), (255, 0, 255))
    hull, shift = paste_over(hull, cut(PAULDRON_FAR), PAULDRON_FAR_AT)
    origin = shift
    hull, shift = paste_over(hull, only_largest(cut(HULL)), (origin[0], origin[1]))
    origin = (origin[0] + shift[0], origin[1] + shift[1])
    hull, shift = paste_over(hull, cut(GROIN), (GROIN_AT[0] + origin[0], GROIN_AT[1] + origin[1]))
    origin = (origin[0] + shift[0], origin[1] + shift[1])
    hull, shift = paste_over(hull, cut(PAULDRON_NEAR), (PAULDRON_NEAR_AT[0] + origin[0], PAULDRON_NEAR_AT[1] + origin[1]))
    origin = (origin[0] + shift[0], origin[1] + shift[1])
    to_palette(hull).save(OUT / "KnightHull.png")
    joints["hull size"] = half(hull.size)
    joints["hull: hull part corner"] = half(origin)
    joints["hull: hip (bottom middle of the hull part)"] = half((origin[0] + 84, origin[1] + 172))
    joints["hull: near shoulder (under the near pauldron)"] = half((origin[0] + PAULDRON_NEAR_AT[0] + 20, origin[1] + PAULDRON_NEAR_AT[1] + 40))
    joints["hull: far shoulder (under the far pauldron)"] = half((origin[0] + PAULDRON_FAR_AT[0] + 50, origin[1] + PAULDRON_FAR_AT[1] + 40))
    joints["hull: carapace top front"] = half((origin[0] + 120, origin[1] + 6))

    # The thigh: bone with its armour plate over it. Hip and knee are the bone's two ball joints.
    thigh, shift = paste_over(cut(THIGH_BONE), cut(THIGH_ARMOUR), THIGH_ARMOUR_AT)
    hip = (26 + shift[0], 16 + shift[1])
    knee = (24 + shift[0], 85 + shift[1])
    shin = cut(SHIN_AND_FOOT).crop((0, 0, SHIN_AND_FOOT[2], SHIN_ANKLE_ROW + 2))
    foot = cut(SHIN_AND_FOOT).crop((0, SHIN_ANKLE_ROW - 4, SHIN_AND_FOOT[2], SHIN_AND_FOOT[3]))
    for layer, shade in (("FG", 1.0), ("BG", FAR_SHADE)):
        to_palette(thigh, shade).save(OUT / f"KnightThigh{layer}.png")
        to_palette(shin, shade).save(OUT / f"KnightShin{layer}.png")
        to_palette(foot, shade).save(OUT / f"KnightFoot{layer}.png")
    joints["thigh size"] = half(thigh.size)
    joints["thigh: hip"] = half(hip)
    joints["thigh: knee"] = half(knee)
    joints["shin size"] = half(shin.size)
    joints["shin: knee"] = half((42, 15))
    joints["shin: ankle"] = half((40, SHIN_ANKLE_ROW))
    joints["foot size"] = half(foot.size)
    joints["foot: ankle"] = half((40, 4))

    cannon = battle_cannon()
    to_palette(cannon, scale=1).save(OUT / "KnightBattleCannon.png")
    joints["cannon size"] = cannon.size

    stubber = heavy_stubber()
    to_palette(stubber, scale=1).save(OUT / "KnightHeavyStubber.png")
    joints["stubber size"] = stubber.size

    sword = chainsword()
    to_palette(sword, scale=1).save(OUT / "KnightChainsword.png")
    joints["chainsword size"] = sword.size

    icon = to_palette(FRONT, scale=6)
    icon.save(OUT / "KnightIcon.png")
    joints["icon size"] = icon.size

    for name, value in joints.items():
        print(f"{name}: {value}")


if __name__ == "__main__":
    main()
