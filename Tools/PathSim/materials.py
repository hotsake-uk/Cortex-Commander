"""Materials: the game's material palette, read from Data/Base.rte/Materials.ini.

A material bitmap's palette index IS the material id (SceneMan::GetTerrMatter returns the pixel value; GetMaterialFromID looks it
up). Each Material carries Index, PresetName, StructuralIntegrity (what PathFinder.cpp calls GetIntegrity()) and a Color.

Material ids that PathFinder.cpp names (Source/System/Constants.h, enum MaterialColorKeys):
    g_MaterialAir = 0, g_MaterialOutOfBounds = 1 (= g_MaterialCavity, the "Default" material, integrity -1), g_MaterialDoor = 181.
"""

import os
import re

MATERIAL_AIR = 0
MATERIAL_OUT_OF_BOUNDS = 1
MATERIAL_CAVITY = 1
MATERIAL_DOOR = 181


class Material:
    __slots__ = ("index", "name", "integrity", "color")

    def __init__(self, index, name, integrity, color):
        self.index = index
        self.name = name
        self.integrity = float(integrity)
        self.color = color

    # Names as in Material.h, so grid_rules.py reads like PathFinder.cpp.
    def GetIntegrity(self):
        return self.integrity

    def GetIndex(self):
        return self.index

    def __repr__(self):
        return "Material(%d %s %g)" % (self.index, self.name, self.integrity)


def parse_materials_ini(path):
    """Returns {index: Material} from a Materials.ini. Only Index, PresetName, StructuralIntegrity and Color are read."""
    materials = {}
    current = None
    color = None
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            line = raw.split("//", 1)[0].rstrip()
            stripped = line.strip()
            if not stripped:
                continue
            if stripped.startswith("AddMaterial"):
                if current is not None:
                    _finish(materials, current, color)
                current = {}
                color = None
                continue
            if current is None:
                continue
            match = re.match(r"^(\w+)\s*=\s*(.*)$", stripped)
            if not match:
                continue
            key, value = match.group(1), match.group(2).strip()
            indent = len(line) - len(line.lstrip("\t "))
            if key == "Index" and indent == 1:
                current["index"] = int(value)
            elif key == "PresetName" and indent == 1:
                current["name"] = value
            elif key == "StructuralIntegrity" and indent == 1:
                current["integrity"] = float(value)
            elif key == "Color" and indent == 1:
                color = {}
            elif key in ("R", "G", "B") and indent == 2 and color is not None:
                color[key] = int(value)
    if current is not None:
        _finish(materials, current, color)
    return materials


def _finish(materials, current, color):
    if "index" not in current:
        return
    rgb = (color.get("R", 128), color.get("G", 128), color.get("B", 128)) if color else (128, 128, 128)
    materials[current["index"]] = Material(current["index"], current.get("name", "?"), current.get("integrity", 0.0), rgb)


class MaterialTable:
    """GetMaterialFromID with the game's fallback: an unknown id reads as the Default (index 1) material."""

    def __init__(self, materials):
        self.materials = dict(materials)
        if MATERIAL_AIR not in self.materials:
            self.materials[MATERIAL_AIR] = Material(0, "Air", 0.0, (0, 0, 0))
        if MATERIAL_OUT_OF_BOUNDS not in self.materials:
            self.materials[MATERIAL_OUT_OF_BOUNDS] = Material(1, "Default", -1.0, (128, 0, 128))
        self.air = self.materials[MATERIAL_AIR]
        self.out_of_bounds = self.materials[MATERIAL_OUT_OF_BOUNDS]
        # A dense lookup list for speed.
        self.by_id = [self.materials.get(i, self.out_of_bounds) for i in range(256)]

    def GetMaterialFromID(self, material_id):
        return self.by_id[material_id & 255]


def load_materials(repo_root):
    return MaterialTable(parse_materials_ini(os.path.join(repo_root, "Data", "Base.rte", "Materials.ini")))
