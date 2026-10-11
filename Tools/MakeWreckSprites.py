"""Makes the wrecks the vehicles and flying ships leave when they blow up: a charred wreck of each one's body, which drops and stays where
it died, and the chunks blown off it, all cut from the body's own picture rather than the generic debris.

For each body picture (8-bit in the game's palette, index 0 the see-through mask colour) it:
- splits the opaque pixels with a jittered Voronoi partition (a seeded random, so a rerun makes the same pieces): the 60-70% of the pixels
  nearest the middle stay as the wreck, and the outer cells (3 to 6 of them, by size) are the chunks; a scrap under MIN_CHUNK pixels joins
  the wreck;
- chars it all: each palette colour goes to the nearest palette colour that is darker and a little greyer (never index 0), with random
  scorch blotches darker still and a black rim one or two pixels deep along the cuts;
- saves <Body>Wreck.png and <Body>ChunkA.png, ChunkB... cropped tight, next to the body picture;
- writes their presets to <Vehicle>Wreck.ini next to the vehicle's ini (each piece's sprite centred on its position);
- and in the vehicle's ini: includes that file before the vehicle, takes the generic big debris out of the body's gib list, and puts the
  wreck and the chunks in its place (between the two "Tools/MakeWreckSprites.py" comments, which a rerun replaces), each at the place
  on the body it was cut from. Only the body's own gib list is touched, not those of its wheels, turrets or arms.

Run from the repository's root: python Tools/MakeWreckSprites.py
"""

import colorsys
import math
import random
import re
from pathlib import Path

from PIL import Image

DATA = Path("Data")
PALETTE = Image.open("Data/Base.rte/palette.bmp").getpalette()[:768]

# (the vehicle's ini, its body picture, the seed for its pieces), under Data/.
VEHICLES = [
    ("Medieval.rte/Actors/Vehicles/Catapult/Catapult.ini", "CatapultFrame.png", 101),
    ("Base.rte/Actors/Vehicles/MotorBoat/MotorBoat.ini", "MotorBoatHull.png", 102),
    ("Base.rte/Actors/Vehicles/Moonhopper/Moonhopper.ini", "HopperBody.png", 103),
    ("Base.rte/Actors/Vehicles/Tank/Tank.ini", "TankHull.png", 104),
    ("Base.rte/Actors/Vehicles/MotorBuggy/MotorBuggy.ini", "BuggyBody.png", 105),
    ("Base.rte/Actors/Vehicles/WoodenCart/WoodenCart.ini", "CartBody.png", 106),
    ("Base.rte/Actors/Vehicles/RowingBoat/RowingBoat.ini", "RowingBoatHull.png", 107),
    ("Base.rte/Craft/Ships/Gunship/Gunship.ini", "GunshipHull.png", 108),
    ("Base.rte/Craft/Ships/Frigate/Frigate.ini", "FrigateHull.png", 109),
]

# The generic debris the wreck and its chunks stand in for, taken out of the body's gib list. The small and tiny bits, the explosions,
# fire and smoke stay.
GENERIC_DEBRIS = {
    "Dropship Hull Large Gib A", "Dropship Hull Gib G", "Gib Panel White Medium A", "Gib Device Big B", "Gib Panel Orange Large A",
    "Gib Panel Orange Medium B", "Gib Panel Dark Medium A", "Gib Pipe Medium A", "Gib Metal Grey Small A",
    "Gib Cart Plank A", "Gib Cart Plank B", "Gib Cart Plank C",
}

WRECK_SHARE = 0.65  # Of the body's pixels, how many stay as the wreck (before the scraps join it).
MIN_CHUNK = 15  # A piece with fewer pixels than this joins the wreck.
GOOD_CHUNK = 50  # The cut is tried again (with the next seed) while a chunk is smaller than this.
WRECK_MASS = 0.5  # Of the vehicle's mass.
CHUNKS_MASS = 0.1  # Of the vehicle's mass, shared among the chunks by size.
CHAR_LIGHT = 0.6  # How much of its lightness a colour keeps when charred.
CHAR_GREY = 0.35  # How much of its colour it loses.
SCORCH_LIGHT = 0.35  # The same, in a scorch blotch.
BEGIN_MARK = "\t// The wreck and the chunks blown off it, cut from the body's picture by Tools/MakeWreckSprites.py."
END_MARK = "\t// (End of the pieces made by Tools/MakeWreckSprites.py.)"


def rgb(index):
    return PALETTE[3 * index:3 * index + 3]


def nearest(target, avoid=()):
    """The palette index nearest a colour, never index 0 (nor any colour equal to the mask)."""
    best, best_d = None, None
    mask = rgb(0)
    for i in range(1, 256):
        c = rgb(i)
        if c == mask or i in avoid:
            continue
        d = (c[0] - target[0]) ** 2 * 3 + (c[1] - target[1]) ** 2 * 4 + (c[2] - target[2]) ** 2 * 2
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best


def char_map(light, grey):
    """For each palette index, the nearest palette index darker and greyer by these amounts."""
    table = [0] * 256
    for i in range(1, 256):
        r, g, b = (v / 255 for v in rgb(i))
        h, l, s = colorsys.rgb_to_hls(r, g, b)
        r, g, b = colorsys.hls_to_rgb(h, l * light, s * (1 - grey))
        table[i] = nearest((r * 255, g * 255, b * 255))
    return table


CHAR = char_map(CHAR_LIGHT, CHAR_GREY)
SCORCH = char_map(SCORCH_LIGHT, 0.6)
RIM = [nearest((14, 12, 10)), nearest((26, 22, 20))]


def noise_field(rng, w, h, cell):
    """Smooth value noise from -1 to 1 over a w x h picture, varying over about `cell` pixels."""
    gw, gh = w // cell + 2, h // cell + 2
    grid = [[rng.uniform(-1, 1) for _ in range(gw + 1)] for _ in range(gh + 1)]

    def at(x, y):
        fx, fy = x / cell, y / cell
        ix, iy = int(fx), int(fy)
        tx, ty = fx - ix, fy - iy
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a = grid[iy][ix] * (1 - tx) + grid[iy][ix + 1] * tx
        b = grid[iy + 1][ix] * (1 - tx) + grid[iy + 1][ix + 1] * tx
        return a * (1 - ty) + b * ty

    return at


def neighbours8(x, y):
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx or dy:
                yield x + dx, y + dy


def components(pixels):
    """The 8-connected groups of a set of pixels, biggest first."""
    left = set(pixels)
    groups = []
    while left:
        start = left.pop()
        group, stack = {start}, [start]
        while stack:
            p = stack.pop()
            for q in neighbours8(*p):
                if q in left:
                    left.remove(q)
                    group.add(q)
                    stack.append(q)
        groups.append(group)
    return sorted(groups, key=len, reverse=True)


def partition_once(img, seed):
    """Splits the opaque pixels into the wreck (piece 0) and the chunks (1, 2...): a dict of pixel to piece."""
    rng = random.Random(seed)
    w, h = img.size
    px = img.load()
    opaque = [(x, y) for y in range(h) for x in range(w) if px[x, y] != 0]
    # Specks standing alone in the picture are cut along with whatever is nearest them, at the end.
    islands = {p for group in components(opaque)[1:] if len(group) < MIN_CHUNK for p in group}
    opaque = [p for p in opaque if p not in islands]
    n = len(opaque)
    cx = sum(p[0] for p in opaque) / n
    cy = sum(p[1] for p in opaque) / n
    half_w = max(abs(p[0] - cx) for p in opaque) + 1
    half_h = max(abs(p[1] - cy) for p in opaque) + 1
    # Long, flat bodies break at their ends more than along their tops, so the height counts for less than the width.
    reach_y = max(half_h, (half_w + half_h) / 2)
    # The jittered Voronoi cells: a seed in each square of a grid, moved about in it at random, and every pixel goes to the nearest seed.
    # The pixels are moved a little by fine noise before they are measured, so the cuts between the cells are ragged, not straight.
    size = max(5.0, math.sqrt(n) / 5)
    cells = []
    for gy in range(int(h / size) + 1):
        for gx in range(int(w / size) + 1):
            cells.append(((gx + rng.uniform(0.1, 0.9)) * size, (gy + rng.uniform(0.1, 0.9)) * size))
    fine_x, fine_y = noise_field(rng, w, h, 3), noise_field(rng, w, h, 3)
    rough = max(1.2, size * 0.2)

    def cell_of(p):
        x, y = p[0] + fine_x(*p) * rough, p[1] + fine_y(*p) * rough
        gx, gy = int(x / size), int(y / size)
        near = [i for i, c in enumerate(cells) if abs(c[0] / size - gx - 0.5) < 2.6 and abs(c[1] / size - gy - 0.5) < 2.6]
        return min(near or range(len(cells)), key=lambda i: math.dist((x, y), cells[i]))

    owner = {p: cell_of(p) for p in opaque}
    members = {}
    for p, i in owner.items():
        members.setdefault(i, []).append(p)

    def middle(i):
        group = members[i]
        return (sum(p[0] for p in group) / len(group), sum(p[1] for p in group) / len(group))

    def reach(i):
        mx, my = middle(i)
        return math.hypot((mx - cx) / half_w, (my - cy) / reach_y) + rng.uniform(-0.08, 0.08)

    # The wreck: the cells nearest the middle, until it has its share of the pixels.
    core, taken = set(), 0
    for i in sorted(members, key=reach):
        if taken >= n * WRECK_SHARE:
            break
        core.add(i)
        taken += len(members[i])
    outer_cells = [i for i in members if i not in core]

    # The chunks: the outer cells gathered round a few of them spread out over the rim (each as far as it can be from the others).
    count = max(3, min(6, 3 + n // 1500))
    seeds = [rng.choice(outer_cells)]
    while len(seeds) < min(count, len(outer_cells)):
        seeds.append(max(outer_cells, key=lambda i: min(math.dist(middle(i), middle(s)) for s in seeds) + rng.uniform(0, 2)))
    count = len(seeds)
    piece = {}
    for i in members:
        number = 0 if i in core else 1 + min(range(count), key=lambda k: math.dist(middle(i), middle(seeds[k])))
        for p in members[i]:
            piece[p] = number

    # Every piece keeps its biggest bit; its other bits (the wreck's only if small) go to the piece they touch most (or, touching none,
    # the nearest), and so does a chunk too small to be worth it. Done over, as a bit handed on can leave another piece in two.
    for _ in range(4):
        for number in range(count + 1):
            groups = components([p for p in opaque if piece[p] == number])
            if number == 0:
                loose = [g for g in groups[1:] if len(g) < MIN_CHUNK * 4]
            else:
                loose = [g for k, g in enumerate(groups) if k > 0 or len(g) < MIN_CHUNK]
            for group in loose:
                touching = {}
                for p in group:
                    for q in neighbours8(*p):
                        if q in piece and q not in group:
                            touching[piece[q]] = touching.get(piece[q], 0) + 1
                touching.pop(number, None)
                if touching:
                    to = max(touching, key=touching.get)
                else:
                    # An island in the picture itself: it goes with the piece nearest it.
                    start = next(iter(group))
                    near = min((q for q in opaque if piece[q] != number), key=lambda q: math.dist(q, start), default=None)
                    to = piece[near] if near else 0
                for p in group:
                    piece[p] = to
    chunks = [g for g in (components([p for p in opaque if piece[p] == k]) for k in range(1, count + 1)) if g]
    chunks = [g[0] for g in chunks if len(g[0]) >= MIN_CHUNK]
    for p in opaque:
        piece[p] = 0
    # Numbered left to right, top to bottom, so A is the leftmost.
    chunks.sort(key=lambda g: (min(p[0] for p in g), min(p[1] for p in g)))
    for number, group in enumerate(chunks, 1):
        for p in group:
            piece[p] = number
    for p in islands:
        piece[p] = piece[min(opaque, key=lambda q: math.dist(p, q))]
    return piece, len(chunks)


def partition(img, seed):
    """partition_once, tried with the next seeds until it gives at least three chunks, none of them a mere scrap (or, failing that, the
    first with three chunks at all)."""
    fallback = None
    for attempt in range(60):
        piece, count = partition_once(img, seed * 1000 + attempt)
        if count < 3:
            continue
        sizes = [sum(1 for v in piece.values() if v == k) for k in range(1, count + 1)]
        if min(sizes) >= GOOD_CHUNK:
            return piece, count
        fallback = fallback or (piece, count)
    if fallback:
        return fallback
    raise RuntimeError("never three chunks")


def char(img, piece, seed):
    """The body charred: darker, greyer, scorched in blotches, and black along the cuts. Returns a dict of pixel to colour."""
    rng = random.Random(seed * 7 + 1)
    w, h = img.size
    px = img.load()
    scorch = noise_field(rng, w, h, 5)
    blotches = [(rng.uniform(0, w), rng.uniform(0, h), rng.uniform(1.5, 3 + min(w, h) * 0.05)) for _ in range(max(3, w * h // 1400))]
    out = {}
    for p in piece:
        x, y = p
        colour = CHAR[px[x, y]]
        dark = scorch(x, y) > 0.62 or any(math.hypot(x - bx, y - by) < r for bx, by, r in blotches)
        if dark:
            colour = SCORCH[px[x, y]]
        out[p] = colour
    # The rim: the pixels beside a cut (a neighbour in another piece), and half of those one further in.
    edge = {p for p in piece if any(piece.get(q, piece[p]) != piece[p] for q in neighbours8(*p))}
    for p in edge:
        out[p] = RIM[0]
    for p in piece:
        if p in edge:
            continue
        if any(q in edge and piece[q] == piece[p] for q in neighbours8(*p)) and rng.random() < 0.5:
            out[p] = RIM[rng.randrange(2)]
    return out


def save_piece(pixels, colours, path):
    """Saves the pixels cropped tight; returns the crop's left, top, width and height."""
    x0 = min(p[0] for p in pixels)
    y0 = min(p[1] for p in pixels)
    w = max(p[0] for p in pixels) - x0 + 1
    h = max(p[1] for p in pixels) - y0 + 1
    img = Image.new("P", (w, h), 0)
    img.putpalette(PALETTE)
    for x, y in pixels:
        img.putpixel((x - x0, y - y0), colours[(x, y)])
    img.save(path)
    return x0, y0, w, h


def vehicle_block(lines):
    """The first and one-past-last line of the vehicle's own block (the AVehicle or ACShip)."""
    start = next(i for i, l in enumerate(lines) if re.match(r"AddActor = (AVehicle|ACShip)\s*$", l))
    end = next((i for i in range(start + 1, len(lines)) if lines[i] and not lines[i].startswith(("\t", "//"))), len(lines))
    while lines[end - 1].strip() == "":
        end -= 1
    return start, end


def block_value(block, name):
    m = next(re.match(r"\t" + name + r" = (.+)", l) for l in block if re.match(r"\t" + name + r" = ", l))
    return m.group(1).strip()


def vehicle_facts(lines):
    start, end = vehicle_block(lines)
    block = lines[start:end]
    facts = {"name": block_value(block, "PresetName"), "mass": float(block_value(block, "Mass")),
        "frames": int(block_value(block, "FrameCount"))}
    i = next(i for i, l in enumerate(block) if l.startswith("\tSpriteOffset = "))
    facts["offset"] = (int(block[i + 1].split("=")[1]), int(block[i + 2].split("=")[1]))
    i = next(i for i, l in enumerate(block) if l.startswith("\tAtomGroup = "))
    facts["material"] = next(l.split("=")[1].strip() for l in block[i:] if l.startswith("\t\t\tCopyOf = "))
    facts["resolution"] = next(int(l.split("=")[1]) for l in block[i:] if l.startswith("\t\tResolution = "))
    return facts


def mass_text(value):
    return f"{value:.0f}" if value >= 10 else f"{value:.1f}"


def preset_ini(facts, rel_dir, body_stem, pieces, group):
    """The presets of the wreck and its chunks."""
    name = facts["name"]
    chunk_pixels = sum(p["pixels"] for p in pieces[1:])
    out = ["///////////////////////////////////////////////////////////////////////",
        f"// The {name}'s wreck and the chunks blown off it when it blows up, cut from its body's picture and charred.",
        "// Made by Tools/MakeWreckSprites.py: rerun it rather than edit this.", "", ""]
    for piece in pieces:
        wreck = piece["index"] == 0
        mass = facts["mass"] * WRECK_MASS if wreck else max(1.0, facts["mass"] * CHUNKS_MASS * piece["pixels"] / chunk_pixels)
        out += ["AddEffect = MOSRotating",
            f"\tPresetName = {piece['preset']}",
            f"\tMass = {mass_text(mass)}",
            "\tHitsMOs = 1",
            f"\tGetsHitByMOs = {1 if wreck else 0}",
            "\tSpriteFile = ContentFile",
            f"\t\tFilePath = {rel_dir}/{piece['file']}",
            "\tFrameCount = 1",
            "\tSpriteOffset = Vector",
            f"\t\tX = {-(piece['w'] // 2)}",
            f"\t\tY = {-(piece['h'] // 2)}",
            f"\tAngularVel = {0 if wreck else 6}",
            "\tAtomGroup = AtomGroup",
            "\t\tAutoGenerate = 1",
            "\t\tMaterial = Material",
            f"\t\t\tCopyOf = {facts['material']}",
            f"\t\tResolution = {facts['resolution'] if wreck else 4}",
            "\t\tDepth = 0",
            "\tDeepCheck = 0",
            f"\tAddToGroup = {group}",
            "\tBuyable = 0",
            "\tGoldValue = 0",
            "", ""]
    return "\n".join(out[:-1])


def gib_lines(pieces):
    out = [BEGIN_MARK]
    for piece in pieces:
        wreck = piece["index"] == 0
        out += ["\tAddGib = Gib",
            "\t\tGibParticle = MOSRotating",
            f"\t\t\tCopyOf = {piece['preset']}",
            "\t\tOffset = Vector",
            f"\t\t\tX = {piece['gib_x']}",
            f"\t\t\tY = {piece['gib_y']}",
            "\t\tCount = 1",
            f"\t\tSpread = {0.3 if wreck else 0.6}",
            f"\t\tMaxVelocity = {3 if wreck else 14}",
            f"\t\tMinVelocity = {0 if wreck else 6}",
            # The chunks don't take the vehicle's speed: with it, they would all fly the way the blow that broke it went, not outward.
            f"\t\tInheritsVel = {1 if wreck else 0}"]
    out.append(END_MARK)
    return out


def update_vehicle_ini(path, include, pieces):
    lines = path.read_text().split("\n")
    # Include the presets before anything in the file.
    include_line = f"IncludeFile = {include}"
    if include_line not in lines:
        first = next(i for i, l in enumerate(lines) if l and not l.startswith("//"))
        lines[first:first] = [include_line, "", ""]
    start, end = vehicle_block(lines)
    block = lines[start:end]
    # Out with the last run's pieces.
    if BEGIN_MARK in block:
        a, b = block.index(BEGIN_MARK), block.index(END_MARK)
        del block[a:b + 1]
    # Out with the generic debris: an AddGib (at the block's own depth) whose particle is one of them.
    kept, i = [], 0
    while i < len(block):
        if block[i] == "\tAddGib = Gib":
            j = i + 1
            while j < len(block) and block[j].startswith("\t\t"):
                j += 1
            gib = block[i:j]
            copy = next((l.split("=", 1)[1].strip() for l in gib if l.startswith("\t\t\tCopyOf = ")), None)
            if "\t\tGibParticle = MOSRotating" in gib and copy in GENERIC_DEBRIS:
                i = j
                continue
            kept += gib
            i = j
        else:
            kept.append(block[i])
            i += 1
    block = kept
    # In with the new ones, after the last gib (or at the end).
    last = max((i for i, l in enumerate(block) if l == "\tAddGib = Gib"), default=None)
    if last is None:
        at = len(block)
    else:
        at = last + 1
        while at < len(block) and block[at].startswith("\t\t"):
            at += 1
    block[at:at] = gib_lines(pieces)
    lines[start:end] = block
    path.write_text("\n".join(lines))


def make(ini_rel, body_file, seed):
    ini = DATA / ini_rel
    folder = ini.parent
    rel_dir = ini.parent.relative_to(DATA).as_posix()
    facts = vehicle_facts(ini.read_text().split("\n"))
    body = Image.open(folder / body_file)
    assert body.mode == "P", body_file
    if facts["frames"] > 1:
        body = body.crop((0, 0, body.width // facts["frames"], body.height))
    piece, chunk_count = partition(body, seed)
    colours = char(body, piece, seed)
    stem = Path(body_file).stem
    module = rel_dir.split("/")[0]
    group = "Gibs - Base Set"  # The Medieval set's own gibs are in this group too.
    pieces = []
    for index in range(chunk_count + 1):
        pixels = [p for p, i in piece.items() if i == index]
        letter = "" if index == 0 else chr(ord("A") + index - 1)
        file = f"{stem}Wreck.png" if index == 0 else f"{stem}Chunk{letter}.png"
        x0, y0, w, h = save_piece(pixels, colours, folder / file)
        pieces.append({"index": index, "file": file, "pixels": len(pixels), "w": w, "h": h,
            "preset": f"{facts['name']} Wreck" + ("" if index == 0 else f" Chunk {letter}"),
            # The piece's middle from the body's: the crop's middle (as its SpriteOffset puts it) less the body's.
            "gib_x": x0 + w // 2 + facts["offset"][0], "gib_y": y0 + h // 2 + facts["offset"][1]})
    # Old chunk pictures from a run that made more of them.
    for old in folder.glob(f"{stem}Chunk*.png"):
        if old.name not in {p["file"] for p in pieces}:
            old.unlink()
    preset_path = folder / f"{ini.stem}Wreck.ini"
    preset_path.write_text(preset_ini(facts, rel_dir, stem, pieces, group) + "\n")
    update_vehicle_ini(ini, f"{rel_dir}/{preset_path.name}", pieces)
    total = sum(p["pixels"] for p in pieces)
    print(f"{facts['name']}: wreck {pieces[0]['pixels'] / total:.0%} of {total} px, {chunk_count} chunks "
        + ", ".join(f"{p['pixels']}" for p in pieces[1:]) + f" ({module})")


def main():
    for ini, body, seed in VEHICLES:
        make(ini, body, seed)


if __name__ == "__main__":
    main()
