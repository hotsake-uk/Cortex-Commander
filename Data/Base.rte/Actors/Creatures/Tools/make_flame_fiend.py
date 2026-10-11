#!/usr/bin/env python3
"""The Flame Fiend's placeholder body (CR-7): a civilian's rig half as big again, in charcoal and embers.

Re-run from anywhere:
    python3 Data/Base.rte/Actors/Creatures/Tools/make_flame_fiend.py

Writes Data/Base.rte/Actors/Creatures/FlameFiend/FlameFiendBody.ini and its sprites. The Fiend is about one and a half times a soldier
(plan step C7), and an actor can't be drawn bigger, so its body is the Civilian's (Civilians.ini, itself the Green Dummy's rig) with every
preset flattened out of its CopyOf chain, its sprites scaled up 1.5x and recoloured, and every offset, limb path and reach scaled to match.
What makes it a Fiend (its fire, its flames, its weakness to water) is in FlameFiend.ini, which uses these parts.

Sprites stay 8-bit paletted PNGs in the game palette, index 0 the transparent background (as make_placeholders.py).
"""

import copy
import glob
import os
import re

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
CREATURES_DIR = os.path.normpath(os.path.join(HERE, ".."))
BASE_DIR = os.path.normpath(os.path.join(CREATURES_DIR, "..", ".."))
DATA_DIR = os.path.dirname(BASE_DIR)
OUT_DIR = os.path.join(CREATURES_DIR, "FlameFiend")
OUT_INI = os.path.join(OUT_DIR, "FlameFiendBody.ini")
SCALE = 1.5
PREFIX = "Flame Fiend"

SOURCES = [
	os.path.join(BASE_DIR, "Actors", "Shared.ini"),
	os.path.join(BASE_DIR, "Actors", "Infantry", "GreenDummy", "GreenDummy.ini"),
	os.path.join(BASE_DIR, "Actors", "Civilians", "Civilians.ini"),
]

# Offsets and reaches that grow with the body.
VECTOR_KEYS = {"SpriteOffset", "JointOffset", "ParentOffset", "ExtendedOffset", "ContractedOffset", "IdleOffset", "StanceOffset",
	"SharpStanceOffset", "SupportOffset", "HolsterOffset", "ReloadOffset", "StartOffset", "AddSegment"}
NUMBER_KEYS = {"MaxLength", "CharHeight", "AimDistance"}
# Nested parts flattened and scaled along with what holds them; anything else nested (wounds, materials, sounds, gibs) is a reference.
PART_KEYS = {"Head", "FGArm", "BGArm", "FGLeg", "BGLeg", "Foot", "StandLimbPath", "StandLimbPathBG", "WalkLimbPath", "RunLimbPath",
	"CrouchLimbPath", "CrouchLimbPathBG", "CrawlLimbPath", "ArmCrawlLimbPath", "ClimbLimbPath", "JumpLimbPath", "DislodgeLimbPath"}
# Kept out of the flattened parts: they're the Fiend's own (FlameFiend.ini), or not a Fiend's at all.
DROPPED_KEYS = {"AddToGroup", "Temperament", "NonCombatant", "Buyable", "GoldValue", "Description", "Jetpack", "PainSound", "DeathSound",
	"DeviceSwitchSound", "BodyHitSound", "StrideSound", "ScriptPath"}
# And the body's own gibs (the Fiend bursts into flame instead, FlameFiend.ini); its limbs keep theirs, flesh for the dummy's plastic.
DROPPED_TOP_KEYS = {"AddGib", "GibSound"}

# Charcoal skin cracked with embers: each colour goes by its brightness to a step of this ramp.
RAMP = [(18, 12, 12), (40, 20, 16), (70, 26, 18), (110, 34, 18), (160, 50, 20), (210, 90, 24), (245, 150, 40), (255, 210, 90)]


class Node:
	def __init__(self, key, value, depth):
		self.key = key
		self.value = value
		self.depth = depth
		self.children = []

	def child(self, key):
		for each in self.children:
			if each.key == key:
				return each
		return None


def parse(path):
	"""The top-level blocks of an INI file, as trees of Key = Value nodes by their tab indentation (comments dropped)."""
	roots = []
	stack = []
	in_comment = False
	with open(path, encoding="utf-8", errors="replace") as handle:
		for raw in handle:
			line = raw.rstrip("\r\n")
			if in_comment:
				if "*/" in line:
					in_comment = False
				continue
			if line.strip().startswith("/*"):
				in_comment = "*/" not in line
				continue
			line = re.sub(r"//.*", "", line)
			if not line.strip() or "=" not in line:
				continue
			depth = len(line) - len(line.lstrip("\t"))
			key, value = [part.strip() for part in line.split("=", 1)]
			node = Node(key, value, depth)
			while stack and stack[-1].depth >= depth:
				stack.pop()
			if stack:
				stack[-1].children.append(node)
			else:
				roots.append(node)
			stack.append(node)
	return roots


PRESETS = {}


def register(node):
	"""Every preset, the ones defined inside another's block too (a body's own limb paths), by name."""
	name = node.child("PresetName")
	if name and name.value not in PRESETS:
		PRESETS[name.value] = node
	for each in node.children:
		register(each)


for source in SOURCES:
	for root in parse(source):
		register(root)


def flatten(node):
	"""The node with its CopyOf chain folded in: later keys replace earlier ones of the same name, Add... keys are added to them."""
	copied = node.child("CopyOf")
	if copied and copied.value in PRESETS:
		result = copy.deepcopy(flatten(PRESETS[copied.value]))
		result.key, result.value = node.key, node.value
	else:
		result = Node(node.key, node.value, node.depth)
		if copied:
			result.children.append(copy.deepcopy(copied))
	for each in node.children:
		if each.key == "CopyOf" and copied and copied.value in PRESETS:
			continue
		if each.key.startswith("Add"):
			result.children.append(copy.deepcopy(each))
			continue
		existing = result.child(each.key)
		if existing:
			result.children[result.children.index(existing)] = copy.deepcopy(each)
		else:
			result.children.append(copy.deepcopy(each))
	return result


def scaled_number(value):
	number = float(value) * SCALE
	return str(int(round(number))) if abs(number - round(number)) < 0.05 else ("%.1f" % number)


def nearest_index(palette, rgb):
	best, best_d = None, None
	for i in range(1, 256):
		r, g, b = palette[i * 3:i * 3 + 3]
		if (r, g, b) == (255, 0, 255):
			continue
		d = 2 * (r - rgb[0]) ** 2 + 4 * (g - rgb[1]) ** 2 + 3 * (b - rgb[2]) ** 2
		if best_d is None or d < best_d:
			best, best_d = i, d
	return best


WRITTEN = {}


def fiend_sprite(file_path):
	"""Writes the Fiend's copy of a sprite (every frame of an animated one): scaled up and recoloured. @return Its new FilePath."""
	if file_path in WRITTEN:
		return WRITTEN[file_path]
	source = os.path.join(DATA_DIR, file_path)
	stem, ext = os.path.splitext(os.path.basename(file_path))
	frames = [source] if os.path.exists(source) else sorted(glob.glob(os.path.join(os.path.dirname(source), stem + "[0-9][0-9][0-9]" + ext)))
	if not frames:
		raise SystemExit("No sprite for " + file_path)
	new_stem = "Fiend" + stem
	for frame in frames:
		image = Image.open(frame)
		palette = image.getpalette()[:768]
		remap = {0: 0}
		for index in set(image.get_flattened_data()):
			if index == 0 or tuple(palette[index * 3:index * 3 + 3]) == (255, 0, 255):
				remap[index] = 0
				continue
			r, g, b = palette[index * 3:index * 3 + 3]
			light = (0.3 * r + 0.55 * g + 0.15 * b) / 255.0
			remap[index] = nearest_index(palette, RAMP[min(int(light * len(RAMP)), len(RAMP) - 1)])
		out = image.copy()
		out.putdata([remap[index] for index in image.get_flattened_data()])
		out = out.resize((round(image.width * SCALE), round(image.height * SCALE)), Image.NEAREST)
		out.putpalette(palette)
		name = os.path.basename(frame).replace(stem, new_stem, 1)
		out.save(os.path.join(OUT_DIR, name))
	new_path = "Base.rte/Actors/Creatures/FlameFiend/" + new_stem + ext
	WRITTEN[file_path] = new_path
	return new_path


def fiendify(node, top=True):
	"""Scales a flattened part (and the parts it holds) up to the Fiend's size, with the Fiend's sprites, flesh and names."""
	kept = []
	for each in node.children:
		if each.key in DROPPED_KEYS or (top and each.key in DROPPED_TOP_KEYS):
			continue
		if each.key == "AddGib":
			particle = each.child("GibParticle")
			source = particle.child("CopyOf") if particle else None
			if source and "Military" in source.value:
				source.value = source.value.replace("Military", "Flesh")
			offset = each.child("Offset")
			if offset:
				for axis in offset.children:
					axis.value = scaled_number(axis.value)
			kept.append(each)
			continue
		if each.key == "PresetName":
			each.value = PREFIX + " " + re.sub(r"^(Civilian|Green Dummy|Human) ", "", each.value)
		elif each.key in VECTOR_KEYS:
			for axis in each.children:
				axis.value = scaled_number(axis.value)
		elif each.key in NUMBER_KEYS:
			each.value = scaled_number(each.value)
		elif each.key in ("SpriteFile", "Hand"):
			path = each.child("FilePath")
			path.value = fiend_sprite(path.value)
		elif each.key == "Material":
			each.children = [Node("CopyOf", "Flesh", each.depth + 1)]
		elif each.key in ("EntryWound", "ExitWound"):
			each.children = [Node("CopyOf", "Wound Flesh " + ("Entry" if each.key == "EntryWound" else "Exit"), each.depth + 1)]
		elif each.key in ("BreakWound", "ParentBreakWound"):
			each.children = [Node("CopyOf", "Wound Bone Break", each.depth + 1)]
		elif each.key in PART_KEYS:
			flat = flatten(each)
			fiendify(flat, False)
			if flat.child("PresetName") is None and each.child("CopyOf"):
				flat.children.insert(0, Node("PresetName", PREFIX + " " + re.sub(r"^(Civilian|Green Dummy|Human) ", "", each.child("CopyOf").value), 0))
			each = flat
		elif each.key in ("AtomGroup", "DeepGroup"):
			fiendify(each, False)
		kept.append(each)
	node.children = kept


def write(node, depth, lines):
	lines.append("\t" * depth + node.key + " = " + node.value)
	for each in node.children:
		write(each, depth + 1, lines)


def main():
	os.makedirs(OUT_DIR, exist_ok=True)
	for old in glob.glob(os.path.join(OUT_DIR, "Fiend*.png")):
		os.remove(old)
	body = flatten(PRESETS["Civilian"])
	fiendify(body)
	body.key, body.value = "AddActor", "AHuman"
	body.child("PresetName").value = PREFIX + " Body"
	# (Only a body to build the Fiend on: not in the catalogue itself.)
	body.children.insert(1, Node("Buyable", "0", 1))
	lines = [
		"///////////////////////////////////////////////////////////////////////",
		"// The Flame Fiend's body (CR-7): the Civilian's rig half as big again, in charcoal and embers.",
		"// GENERATED by Base.rte/Actors/Creatures/Tools/make_flame_fiend.py - edit that and re-run. The Fiend itself is FlameFiend.ini.",
		"",
		"",
	]
	write(body, 0, lines)
	with open(OUT_INI, "w", encoding="utf-8", newline="\n") as handle:
		handle.write("\n".join(lines) + "\n")
	print("wrote", os.path.relpath(OUT_INI, DATA_DIR), "and", len(WRITTEN), "sprites")


if __name__ == "__main__":
	main()
