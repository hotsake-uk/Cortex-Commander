#!/usr/bin/env python3
"""portdata: which edges of each bunker module are open, from its material bitmap.

For every TerrainObject in BunkerModules.ini with a MaterialFile: the bitmap size, the x ranges of open pixels (cavity 1, or mask 0 which
leaves whatever is there -- air in the sky) on the top and bottom edge rows, the y ranges on the left and right edge columns, and the
floor surfaces inside (for each column the y of the first solid pixel under an air run, grouped into x ranges). A 4x4-block ASCII
picture follows each module ('.' mask, ':' cavity, '#' concrete, 'M' metal, '?' other).
Usage: python3 -I portdata.py [REPO] > portdata.txt
"""

import os
import re
import sys

import numpy as np
from PIL import Image

REPO = sys.argv[1] if len(sys.argv) > 1 else "/home/user/Cortex-Commander"
INI = os.path.join(REPO, "Data/Base.rte/Scenes/Objects/Bunkers/BunkerModules/BunkerModules.ini")


def read_presets():
    presets = []
    current = None
    with open(INI, encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.split("//", 1)[0].rstrip()
            s = line.strip()
            if s.startswith("AddTerrainObject"):
                current = {"mat": None, "offset": None}
                presets.append(current)
            elif current is not None:
                m = re.match(r"^(\w+)\s*=\s*(.*)$", s)
                indent = len(line) - len(line.lstrip("\t "))
                if m and m.group(1) == "PresetName" and indent == 1:
                    current["name"] = m.group(2)
                if "Mat" in s and s.startswith("FilePath") and current["mat"] is None:
                    current["mat"] = m.group(2)
                if m and m.group(1) == "BitmapOffset" and indent == 1:
                    current["offset"] = []
                if m and m.group(1) in ("X", "Y") and indent == 2 and current.get("offset") is not None and len(current["offset"]) < 2:
                    current["offset"].append(float(m.group(2)))
    return [p for p in presets if p.get("mat")]


def ranges(mask):
    """[(start, end)] inclusive index ranges where mask is True."""
    out = []
    start = None
    for i, v in enumerate(mask):
        if v and start is None:
            start = i
        if not v and start is not None:
            out.append((start, i - 1))
            start = None
    if start is not None:
        out.append((start, len(mask) - 1))
    return out


def fmt_ranges(rs):
    return ", ".join("%d-%d" % r for r in rs) if rs else "closed"


def floors(a):
    """Floor surfaces: per column, y of the first solid pixel below each air run (air = 0 or 1); grouped as (y, x-range)."""
    h, w = a.shape
    per_col = {}
    for x in range(w):
        col = a[:, x]
        ys = []
        prev_air = False
        for y in range(h):
            air = col[y] <= 1
            if not air and prev_air:
                ys.append(y)
            prev_air = air
        per_col[x] = tuple(ys)
    groups = []
    start = 0
    for x in range(1, w + 1):
        if x == w or per_col[x] != per_col[start]:
            groups.append((start, x - 1, per_col[start]))
            start = x
    return groups


def main():
    chars = {0: ".", 1: ":", 177: "#", 178: "M"}
    for p in read_presets():
        path = os.path.join(REPO, "Data", p["mat"])  # a ContentFile path is relative to Data/
        try:
            a = np.array(Image.open(path))
        except Exception as e:  # noqa: BLE001
            print("== %s: cannot read %s (%s)" % (p["name"], p["mat"], e))
            continue
        h, w = a.shape[:2]
        if a.ndim != 2:
            print("== %s: %s is not a palette image" % (p["name"], p["mat"]))
            continue
        offset = p["offset"] or [-(w // 2) if w > 24 else 0, -(h // 2) if h > 24 else 0]
        print("== %s  %s  %dx%d  BitmapOffset %s%s" % (p["name"], os.path.basename(p["mat"]), w, h, tuple(int(v) for v in offset), "" if p["offset"] is None else " (explicit)"))
        top, bottom = a[0, :], a[h - 1, :]
        left, right = a[:, 0], a[:, w - 1]
        print("   top    row 0      cavity x: %-18s mask x: %s" % (fmt_ranges(ranges(top == 1)), fmt_ranges(ranges(top == 0))))
        print("   bottom row %-3d    cavity x: %-18s mask x: %s" % (h - 1, fmt_ranges(ranges(bottom == 1)), fmt_ranges(ranges(bottom == 0))))
        print("   left   col 0      cavity y: %-18s mask y: %s" % (fmt_ranges(ranges(left == 1)), fmt_ranges(ranges(left == 0))))
        print("   right  col %-3d    cavity y: %-18s mask y: %s" % (w - 1, fmt_ranges(ranges(right == 1)), fmt_ranges(ranges(right == 0))))
        fl = [(x0, x1, ys) for x0, x1, ys in floors(a) if ys]
        print("   floors (y of first solid under air, by x range): " + "; ".join("x%d-%d: y%s" % (x0, x1, ",".join(str(y) for y in ys)) for x0, x1, ys in fl))
        for y in range(0, h, 4):
            print("   %3d %s" % (y, "".join(chars.get(int(a[y, x]), "?") for x in range(0, w, 4))))


if __name__ == "__main__":
    main()
