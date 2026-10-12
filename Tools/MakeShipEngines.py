"""Makes the flying ships' engines, so they no longer fly on the drop ship's rusty orange ones: a left and a right engine for each look,
in the base game's style (8-bit in the game's palette, index 0 the see-through mask colour, lit from the upper left, a little dithering),
the pieces each one breaks into, cut from its own picture so they come off in its colours, and their presets.

The looks:
- Gunship: a slim olive pod with the hull's red-orange stripe, a steel collar like the roof mounts it hangs from, and a steel nozzle.
- Frigate: a bigger pod in the Frigate's steel armour, riveted bands and the orange stripe, on a heavier pylon.
- Steel: the Gunship's shape in plain gunmetal, with no paint, for craft still to come.

The engines are the drop ship's (Dropship Engine A Left and Right in DropshipMK1.ini), read from there each run: the same mass, thrust,
sounds, flame, toughness and blast, with only the picture, where it joins the craft and where the thrust comes out changed, and the
rusty pieces it breaks into swapped for its own. The right engine is the left one's shape mirrored, still lit from the upper left.

Writes Data/Base.rte/Craft/Ships/Engines/: the engines' pictures, Gibs/ with their pieces, and ShipEngines.ini (included from Craft.ini
before the ships). Rerun it rather than edit those.

Run from the repository's root: python Tools/MakeShipEngines.py
"""

import random
from pathlib import Path

from MakeShipSprites import OLIVE, STEEL
from MakeVehicleSprites import CHROME, IRON, LAMP, PAINT, Canvas, dither, shade
from MakeWreckSprites import CHAR, RIM, neighbours8, partition, save_piece

OUT = Path("Data/Base.rte/Craft/Ships/Engines")
REL = "Base.rte/Craft/Ships/Engines"
TEMPLATE = Path("Data/Base.rte/Craft/Dropships/DropshipMK1.ini")

# Each look: its picture's size, the engine's middle in it (where it turns about), where it joins the craft and where the thrust comes out
# (both from the middle, for the left engine), and the ramps it's drawn in.
LOOKS = {
    "Gunship": {"size": (34, 52), "mid": (12, 21), "joint": (17, 0), "nozzle": (0, 28), "body": OLIVE, "trim": STEEL,
        "stripe": True, "bands": False, "light": 0.62, "seed": 301},
    "Frigate": {"size": (44, 64), "mid": (16, 26), "joint": (21, 0), "nozzle": (0, 34), "body": STEEL, "trim": STEEL,
        "stripe": True, "bands": True, "light": 0.82, "seed": 302},
    "Steel": {"size": (34, 52), "mid": (12, 21), "joint": (17, 0), "nozzle": (0, 28), "body": IRON, "trim": IRON,
        "stripe": False, "bands": False, "light": 0.74, "seed": 303},
}

# The drop ship's own pieces and the rusty and orange bits, taken out of the engines' gib lists for their own pieces.
SWAPPED_GIBS = ("Dropship Engine Gib", "Gib Panel Orange", "Gib Metal Rust")
PIECES_MASS = 0.6  # Of the engine's mass, shared among its pieces by size.


class Side:
    """Draws the left engine's shape on the left or the right engine's picture: x is mirrored on the right, but each run of pixels is
    still shaded from its own left end, so both engines are lit from the upper left."""

    def __init__(self, canvas, right):
        self.c = canvas
        self.right = right

    def span(self, x0, x1):
        """The left engine's x0..x1 as this side's left-to-right run."""
        if not self.right:
            return x0, x1
        return self.c.w - 1 - x1, self.c.w - 1 - x0

    def put(self, x, y, colour):
        self.c.put(self.c.w - 1 - x if self.right else x, y, colour)

    def tube(self, x0, x1, y, ramp, base, edge=0.25):
        """One row across a round body: light a third of the way in from the left, dark at both rims."""
        a, b = self.span(x0, x1)
        for x in range(a, b + 1):
            t = (x - a) / max(b - a, 1)
            light = base + 0.25 - abs(t - 0.3) * 0.9
            if x in (a, b):
                light -= edge
            self.c.put(x, y, shade(ramp, dither(x, y, light, 0.04)))

    def plate(self, x0, y0, x1, y1, ramp, base):
        """A flat block lit from the upper left: a light top and left edge, a dark bottom and right."""
        a, b = self.span(x0, x1)
        for y in range(y0, y1 + 1):
            for x in range(a, b + 1):
                light = base + 0.1 - (y - y0) / max(y1 - y0, 1) * 0.2 - (x - a) / max(b - a, 1) * 0.08
                if y == y0 or x == a:
                    light += 0.18
                if y == y1 or x == b:
                    light -= 0.3
                self.c.put(x, y, shade(ramp, dither(x, y, light, 0.04)))

    def bolt(self, x, y):
        self.put(x, y, CHROME[6])
        self.put(x + 1, y + 1, CHROME[1])


def draw_engine(look, right):
    w, h = look["size"]
    mx, my = look["mid"]
    jx = mx + look["joint"][0]
    ny = my + look["nozzle"][1]
    body, trim = look["body"], look["trim"]
    c = Canvas(w, h, look["seed"] + right)
    s = Side(c, right)
    big = look["bands"]
    half = mx - 2  # The pod's half width.
    pod_top, pod_bottom = 3, ny - (12 if big else 10)
    # The mount: a block reaching out from the pod to where it joins the craft, the end a steel collar like the craft's own mounts.
    reach = (my - (6 if big else 5), my + (6 if big else 4))
    s.plate(mx + half - 1, reach[0] + 1, w - 6, reach[1] - 1, body, look["light"] - 0.1)
    s.plate(w - 7, reach[0], w - 1, reach[1], trim, 0.62)
    s.bolt(jx - 1, reach[0] + 2)
    s.bolt(jx - 1, reach[1] - 3)
    # The pod: rounded over the top and narrowing a little to the nozzle.
    for y in range(pod_top, pod_bottom + 1):
        cap = max(0, pod_top + 4 - y)
        taper = max(0, y - (pod_bottom - 5)) // 2
        inset = cap * cap // 2 + taper
        s.tube(mx - half + inset, mx + half - inset, y, body, look["light"] - (y - pod_top) * 0.004)
    # The intake on top: a dark mouth in a steel lip.
    for x in range(mx - half + 4, mx + half - 3):
        s.put(x, pod_top, trim[0])
        s.put(x, pod_top + 1, shade(trim, 0.7 if x < mx else 0.5))
    # A collar where the mount meets the pod.
    for y in (reach[0] - 1, reach[1] + 1):
        s.tube(mx - half, mx + half, y, trim, 0.62)
    # Armour bands with rivets (the Frigate), or a seam and bolts.
    if big:
        for y in (pod_top + 8, my + 10, pod_bottom - 4):
            s.tube(mx - half, mx + half, y, trim, 0.78)
            s.tube(mx - half, mx + half, y + 1, trim, 0.3)
            for x in range(mx - half + 3, mx + half - 1, 5):
                s.bolt(x, y + 3)
    else:
        for x in range(mx - half + 1, mx + half):
            s.put(x, my + 9, body[1])
        for x in range(mx - half + 3, mx + half - 1, 5):
            s.bolt(x, pod_top + 6)
            s.bolt(x, my + 12)
    # The stripe round the pod, under the mount.
    if look["stripe"]:
        y = reach[1] + 4
        a, b = mx - half + 1, mx + half - 1
        for x in range(a, b + 1):
            s.put(x, y, PAINT[3] if x % 2 else PAINT[2])
            s.put(x, y + 1, PAINT[1])
    else:
        s.put(mx + half - 3, pod_top + 3, LAMP[1])
    # The nozzle: a steel bell flaring to the bottom with dark rings round it, black inside its mouth.
    for y in range(pod_bottom + 1, h):
        t = (y - pod_bottom) / max(h - 1 - pod_bottom, 1)
        nhalf = round((half - 3) * (0.75 + t * 0.35))
        ring = (y - pod_bottom) % 3 == 0
        s.tube(mx - nhalf, mx + nhalf, y, trim, 0.72 - t * 0.2 - (0.3 if ring else 0))
    for x in range(mx - half + 5, mx + half - 4):
        s.put(x, h - 1, RIM[0])
    # A dark line round it all, as the base game's craft have.
    for (x, y), colour in list(c.px.items()):
        for dx, dy in ((0, 1), (1, 0), (-1, 0)):
            if (x + dx, y + dy) not in c.px and 0 <= x + dx < w and 0 <= y + dy < h:
                c.put(x + dx, y + dy, body[0])
    return c.image()


def cut_pieces(img, seed):
    """The engine cut into its pieces, each in the engine's own colours with a dark rim along the cuts. Returns (pixels, colours) each."""
    piece, count = partition(img, seed)
    px = img.load()
    colours = {}
    rng = random.Random(seed)
    for p, number in piece.items():
        colour = px[p]
        if any(piece.get(q, number) != number for q in neighbours8(*p)):
            colour = RIM[rng.randrange(2)] if rng.random() < 0.6 else CHAR[colour]
        colours[p] = colour
    return [([p for p, n in piece.items() if n == k], colours) for k in range(count + 1)]


def properties(block):
    """A preset's lines as its top-level properties, each with the lines under it."""
    props = []
    for line in block:
        if line.startswith("\t") and not line.startswith("\t\t"):
            props.append([line])
        elif props:
            props[-1].append(line)
    return props


def set_vector(prop, x, y):
    return [prop[0], f"\t\tX = {x}", f"\t\tY = {y}"]


def template(name):
    lines = TEMPLATE.read_text().split("\n")
    start = lines.index(f"\tPresetName = {name}")
    end = start + 1
    while end < len(lines) and not lines[end].startswith("AddEffect"):
        end += 1
    while not lines[end - 1].strip():
        end -= 1
    return lines[start:end]


def engine_ini(look_name, look, right, file, pieces):
    side = "Right" if right else "Left"
    w, h = look["size"]
    mx, my = look["mid"]
    if right:
        mx = w - 1 - mx
    sign = -1 if right else 1
    out = []
    for prop in properties(template(f"Dropship Engine A {side}")):
        key = prop[0].strip().split(" =")[0]
        if key == "PresetName":
            prop = [f"\tPresetName = {look_name} Engine {side}"]
        elif key == "SpriteFile":
            prop = [prop[0], f"\t\tFilePath = {REL}/{file}"]
        elif key == "SpriteOffset":
            prop = set_vector(prop, -mx, -my)
        elif key == "JointOffset":
            prop = set_vector(prop, sign * look["joint"][0], look["joint"][1])
        elif key == "EmissionOffset":
            prop = set_vector(prop, sign * look["nozzle"][0], look["nozzle"][1])
        elif key == "AddGib" and any(f"CopyOf = {g}" in line for line in prop for g in SWAPPED_GIBS):
            continue
        out += prop
    for piece in pieces:
        out += ["\tAddGib = Gib",
            "\t\tGibParticle = MOSRotating",
            f"\t\t\tCopyOf = {piece['preset']}",
            "\t\tOffset = Vector",
            f"\t\t\tX = {piece['x']}",
            f"\t\t\tY = {piece['y']}"]
    for bit, x, y in (("Gib Metal Grey Tiny A", 6, -8), ("Gib Metal Dark Tiny A", -6, 4), ("Gib Metal Dark Micro A", 2, 20)):
        out += ["\tAddGib = Gib", "\t\tGibParticle = MOSParticle", f"\t\t\tCopyOf = {bit}", "\t\tOffset = Vector",
            f"\t\t\tX = {sign * x}", f"\t\t\tY = {y}"]
    return ["AddEffect = AEmitter"] + out


def piece_ini(preset, file, w, h, mass):
    return ["AddEffect = MOSRotating",
        f"\tPresetName = {preset}",
        f"\tMass = {mass:.2f}",
        "\tHitsMOs = 1",
        "\tGetsHitByMOs = 0",
        "\tSpriteFile = ContentFile",
        f"\t\tFilePath = {REL}/Gibs/{file}",
        "\tFrameCount = 1",
        "\tSpriteOffset = Vector",
        f"\t\tX = {-(w // 2)}",
        f"\t\tY = {-(h // 2)}",
        "\tAngularVel = 3",
        "\tAtomGroup = AtomGroup",
        "\t\tAutoGenerate = 1",
        "\t\tMaterial = Material",
        "\t\t\tCopyOf = Military Stuff",
        f"\t\tResolution = {8 if w * h > 300 else 4}",
        "\t\tDepth = 0",
        "\tDeepCheck = 0",
        "\tAddToGroup = Gibs - Base Set",
        "\tBuyable = 0",
        "\tGoldValue = 0"]


def main():
    (OUT / "Gibs").mkdir(parents=True, exist_ok=True)
    engine_mass = float(next(l for l in template("Dropship Engine A Left") if l.startswith("\tMass =")).split("=")[1])
    pieces_out = ["///////////////////////////////////////////////////////////////////////",
        "// The flying ships' engines and the pieces they break into. Made by Tools/MakeShipEngines.py: rerun it rather than edit this.",
        "", ""]
    engines_out = []
    for look_name, look in LOOKS.items():
        for right in (False, True):
            side = "Right" if right else "Left"
            img = draw_engine(look, right)
            file = f"{look_name}Engine{side}.png"
            img.save(OUT / file)
            w = img.width
            mx, my = look["mid"]
            if right:
                mx = w - 1 - mx
            cuts = cut_pieces(img, look["seed"] * 10 + right)
            total = sum(len(pixels) for pixels, _ in cuts)
            pieces = []
            for number, (pixels, colours) in enumerate(cuts):
                letter = chr(ord("A") + number)
                piece_file = f"{look_name}Engine{side}Gib{letter}.png"
                x0, y0, pw, ph = save_piece(pixels, colours, OUT / "Gibs" / piece_file)
                preset = f"{look_name} Engine {side} Gib {letter}"
                pieces_out += piece_ini(preset, piece_file, pw, ph, engine_mass * PIECES_MASS * len(pixels) / total) + ["", ""]
                pieces.append({"preset": preset, "x": x0 + pw // 2 - mx, "y": y0 + ph // 2 - my})
            for old in (OUT / "Gibs").glob(f"{look_name}Engine{side}Gib*.png"):
                if old.name not in {f"{look_name}Engine{side}Gib{chr(ord('A') + n)}.png" for n in range(len(cuts))}:
                    old.unlink()
            engines_out += engine_ini(look_name, look, right, file, pieces) + ["", ""]
            print(f"{look_name} Engine {side}: {img.width}x{img.height}, {len(cuts)} pieces")
    (OUT / "ShipEngines.ini").write_text("\n".join(pieces_out + engines_out).rstrip("\n") + "\n")


if __name__ == "__main__":
    main()
