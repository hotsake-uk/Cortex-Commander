"""The sky bunker of Tools/RenderTest/RenderTest.rte/AIBunker.lua: which modules go where, and the five courses.

Modules are placed in the order the Lua places them (it matters: a later module's cavity carves air through an earlier one, and a
later module's walls overwrite an earlier one's air). Mat bitmaps come from Data/Base.rte/Scenes/Objects/Bunkers/BunkerModules/
(the MaterialFile of each TerrainObject preset in BunkerModules.ini).
"""

import os

import numpy as np
from PIL import Image

# PresetName -> MaterialFile (BunkerModules.ini lines 327-418).
MODULE_MAT_FILES = {
    "Tunnel A": "Data/Base.rte/Scenes/Objects/Bunkers/BunkerModules/ConcreteTunnelAMat.png",
    "Hub A": "Data/Base.rte/Scenes/Objects/Bunkers/BunkerModules/ConcreteHubAMat.png",
    "Shaft A": "Data/Base.rte/Scenes/Objects/Bunkers/BunkerModules/ConcreteShaftAMat.png",
    "Stairs A": "Data/Base.rte/Scenes/Objects/Bunkers/BunkerModules/ConcreteStairsAMat.png",
}

TOP, MID, LOW = 228, 324, 420

# (preset, centre x, centre y), in the Lua's order (AIBunker.lua, UpdateScript, "builtModules").
SKY_BUNKER_LAYOUT = (
    [("Tunnel A", x, TOP) for x in (1504, 1600, 1792, 1984, 2080)]
    + [("Hub A", 1696, TOP), ("Hub A", 1888, TOP)]
    + [("Hub A", 1504, MID), ("Hub A", 1696, MID), ("Shaft A", 1792, MID), ("Hub A", 1888, MID), ("Stairs A", 1984, MID + 24), ("Tunnel A", 2080, MID)]
    + [("Tunnel A", x, LOW) for x in (1504, 1600, 1984, 2080)]
    + [("Hub A", 1696, LOW), ("Hub A", 1792, LOW), ("Hub A", 1888, LOW)]
)

# The five courses (AIBunker.lua, "local courses = {...}").
SKY_COURSES = [
    ((1520, 444), (2070, 252), "low left to top right"),
    ((1510, 348), (1900, 348), "across the gap"),
    ((1520, 252), (2070, 444), "top left to low right"),
    ((1792, 444), (1792, 252), "up the shaft"),
    ((2080, 444), (1510, 348), "low right to mid left"),
]

KETANOT_TERRAIN = "Data/Base.rte/Scenes/Terrains/KetanotHills.png"  # the SLTerrain's BitmapFile: an 8-bit material bitmap, WrapX = 1

# ---- The redesigned sky bunker (portdata.txt has every module's open edges) -------------------------------------------------------
# Module grid: corners x = 1464 + 96k (k = 0..7), storey corners y = 192 (top), 288 (mid), 384 (bottom); placed by centre = corner + 48
# (+ 72 for the 96x192 Steep Stairs, + 48 for 96x96), so the sandbox's 24 px snap is a no-op. Corridors are 48 px tall with floors at
# corner + 72: 264 / 360 / 456. Openings are 48 px wide at offset 24-71 of a module edge.
#
#   top    k0 End B   k1 T-Junction D   k2 End D   k3 (sky)   k4 End B   k5 L-Junction D   k6 Steep Stairs D (upper)  k7 End D
#   mid    k0 (sky)   k1 Shaft A        k2 End B   k3 T-Junction D   k4 Tunnel A   k5 Hub A   k6 Steep Stairs D (lower)  k7 (sky)
#   bottom k0 End B   k1 T-Junction B   k2 Tunnel A   k3 T-Junction B   k4 Tunnel A   k5 T-Junction B   k6 Tunnel A   k7 End D
#
# Features: the SHAFT (b) k1: T-Junction B foot, Shaft A, T-Junction D mouth into the top room (End B + T-Junction D + End D; reachable
# only by the shaft). The HATCH (a) k3: T-Junction D (mid, open down) directly over T-Junction B (bottom, open up): a 48 px passage
# through both slabs. The HUB crossing (c) k5: Hub A on the mid corridor with T-Junction B under it (its floor hole leads to the bottom
# corridor) and L-Junction D over it (its roof hole leads up into a two-module top gallery, End B + L-Junction D: the L-junction corner
# (e)). STAIRS (d) k6: Steep Stairs D, entered from the mid corridor on its left (rows 120-167 of its 192 px bitmap = the mid storey)
# and exiting right at the top storey into End D (k7). Dead ends (e): End B / End D at the ends of every corridor.
# Every module edge faces either a matching open edge, a closed edge, or the sky from behind a closed edge; the bottom row's floors
# are all closed (End B, T-Junction B, Tunnel A, End D are closed below). Nothing opens to the sky.
NEW_TOP, NEW_MID, NEW_LOW = 192, 288, 384


def _col(k):
    return 1464 + 96 * k


NEW_BUNKER_LAYOUT = [
    # bottom storey: a corridor the whole width, closed floor
    ("End B", _col(0) + 48, NEW_LOW + 48),  # k0: left dead end, open right
    ("T-Junction B", _col(1) + 48, NEW_LOW + 48),  # k1: shaft foot (open up)
    ("Tunnel A", _col(2) + 48, NEW_LOW + 48),  # k2
    ("T-Junction B", _col(3) + 48, NEW_LOW + 48),  # k3: hatch foot (open up)
    ("Tunnel A", _col(4) + 48, NEW_LOW + 48),  # k4
    ("T-Junction B", _col(5) + 48, NEW_LOW + 48),  # k5: under the hub (open up)
    ("Tunnel A", _col(6) + 48, NEW_LOW + 48),  # k6
    ("End D", _col(7) + 48, NEW_LOW + 48),  # k7: right dead end, open left
    # middle storey
    ("Shaft A", _col(1) + 48, NEW_MID + 48),  # k1: the shaft's middle
    ("End B", _col(2) + 48, NEW_MID + 48),  # k2: left dead end of the mid corridor (closed left, against the shaft wall)
    ("T-Junction D", _col(3) + 48, NEW_MID + 48),  # k3: hatch top (open down, left, right; closed roof)
    ("Tunnel A", _col(4) + 48, NEW_MID + 48),  # k4
    ("Hub A", _col(5) + 48, NEW_MID + 48),  # k5: the crossing: hole down to k5 bottom, hole up to the top gallery
    # top storey
    ("End B", _col(0) + 48, NEW_TOP + 48),  # k0: top room, left part
    ("T-Junction D", _col(1) + 48, NEW_TOP + 48),  # k1: shaft mouth (open down, left, right)
    ("End D", _col(2) + 48, NEW_TOP + 48),  # k2: top room, right part (closed right)
    ("End B", _col(4) + 48, NEW_TOP + 48),  # k4: top gallery, left dead end
    ("L-Junction D", _col(5) + 48, NEW_TOP + 48),  # k5: the corner: open down (onto the hub) and left (the gallery)
    ("Steep Stairs D", _col(6) + 48, NEW_TOP + 96),  # k6: 96x192, spans top and mid: mid corridor in at the left, out right at the top
    ("End D", _col(7) + 48, NEW_TOP + 48),  # k7: landing room at the top of the stairs (open left)
]

# Courses: points in the air 20 px over a solid floor strip with 48 px of head room (the Lua does not Settle on the sky bunker).
NEW_COURSES = [
    ((1700, 436), (1520, 244), "bottom corridor to the top room"),  # only the shaft at k1 leads there
    ((1700, 436), (1700, 340), "up the hatch"),  # bottom k2 -> mid k2 (End B), via the hatch at k3
    ((1700, 340), (1700, 436), "down the hatch"),
    ((1900, 340), (2060, 340), "across the hub"),  # mid k4 -> the stairs' lower landing (k6), over the hub's hole at k5
    ((2060, 340), (2160, 244), "up the stairs"),  # the stairs' lower landing -> End D at the top (k7)
    ((2160, 244), (2060, 340), "down the stairs"),
    ((1900, 436), (1900, 244), "bottom to the top gallery"),  # optional 7th: up through the hub's column (k5) and the L-junction
]

# The new layout's extent, for the "route stays inside" check: x over the module columns, y from the roof's top to the bottom floor.
NEW_BOUNDS = (_col(0), NEW_TOP, _col(8), NEW_LOW + 72)

LAYOUTS = {
    "sky": (SKY_BUNKER_LAYOUT, SKY_COURSES, None),
    "new": (NEW_BUNKER_LAYOUT, NEW_COURSES, NEW_BOUNDS),
}


def load_module_bitmaps_for(repo_root, layout):
    """Mat bitmaps for every preset a layout uses, found through BunkerModules.ini."""
    import re
    ini = os.path.join(repo_root, "Data/Base.rte/Scenes/Objects/Bunkers/BunkerModules/BunkerModules.ini")
    mats = {}
    name = None
    with open(ini, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.split("//", 1)[0].rstrip()
            s = line.strip()
            indent = len(line) - len(line.lstrip("\t "))
            if s.startswith("AddTerrainObject"):
                name = None
            m = re.match(r"^(\w+)\s*=\s*(.*)$", s)
            if m and m.group(1) == "PresetName" and indent == 1:
                name = m.group(2)
            if m and m.group(1) == "FilePath" and "Mat" in m.group(2) and name and name not in mats:
                mats[name] = m.group(2)
    bitmaps = {}
    for preset, _, _ in layout:
        if preset in bitmaps:
            continue
        if preset not in mats:
            raise RuntimeError("no MaterialFile for preset %r in BunkerModules.ini" % preset)
        image = Image.open(os.path.join(repo_root, "Data", mats[preset]))
        bitmaps[preset] = np.array(image, dtype=np.uint8)
    return bitmaps


def load_module_bitmaps(repo_root):
    bitmaps = {}
    for preset, rel in MODULE_MAT_FILES.items():
        image = Image.open(os.path.join(repo_root, rel))
        if image.mode != "P":
            raise RuntimeError("%s is not a palette image (mode %s)" % (rel, image.mode))
        bitmaps[preset] = np.array(image, dtype=np.uint8)
    return bitmaps


def load_ketanot_terrain(repo_root):
    image = Image.open(os.path.join(repo_root, KETANOT_TERRAIN))
    if image.mode != "P":
        raise RuntimeError("Ketanot terrain is not a palette image")
    return np.array(image, dtype=np.uint8)


def build_sky_bunker(scene, bitmaps, snap=True, layout=SKY_BUNKER_LAYOUT):
    """Places the modules on the scene (in order), returning the placements [(preset, m_Pos, corner)]."""
    placements = []
    for preset, cx, cy in layout:
        pos, corner = scene.place_structure(bitmaps[preset], float(cx), float(cy), snap=snap)
        placements.append((preset, pos, corner))
    return placements


def settle(scene, point, height):
    """AIBunkerScript:Settle: down out of any slab the point is in, down to the floor, then up a fifth of a body."""
    import math
    from materials import MATERIAL_AIR
    x, y = math.floor(point[0]), math.floor(point[1])
    limit = 0
    while scene.GetTerrMatter(x, y) != MATERIAL_AIR and limit < 200:
        y += 1
        limit += 1
    limit = 0
    while scene.GetTerrMatter(x, y) == MATERIAL_AIR and limit < 400:
        y += 1
        limit += 1
    return (float(x), float(y - math.floor(height * 0.2)))
