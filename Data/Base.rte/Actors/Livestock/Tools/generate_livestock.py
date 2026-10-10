#!/usr/bin/env python3
"""Generates the Base.rte livestock and civilian sprites (and Livestock.ini).

Re-run from anywhere:
    python3 Data/Base.rte/Actors/Livestock/Tools/generate_livestock.py [--preview out.png]

Everything is written as 8-bit paletted PNGs that use the exact game palette
(taken from the crab body sprite). Index 0 (magenta) is the transparent
background. Every colour used below is an RGB wish that is mapped to its
nearest palette index once, up front (see COLOURS), so the choice of index is
explicit and printed with --list-colours.

Bodies are hand-placed ASCII art (facing right, as the engine expects).
Legs are drawn procedurally per extension frame so that the foot of frame k
sits at the ankle distance the engine uses to pick that frame:
    frame k  <->  ankle distance ~ C + (E - C) * (k + 0.5) / 4
with ContractedOffset = (0, C) and ExtendedOffset = (0, E) (straight down).
The same numbers are written into Livestock.ini, so sprites and INI agree.
"""

import argparse
import colorsys
import math
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
BASE_RTE = os.path.normpath(os.path.join(HERE, "..", "..", ".."))  # Data/Base.rte
DATA_DIR = os.path.dirname(BASE_RTE)
LIVESTOCK_DIR = os.path.join(BASE_RTE, "Actors", "Livestock")
CIVILIAN_DIR = os.path.join(BASE_RTE, "Actors", "Civilians")
DUMMY_DIR = os.path.join(BASE_RTE, "Actors", "Infantry", "GreenDummy")
PALETTE_SOURCE = os.path.join(BASE_RTE, "Actors", "Wildlife", "Crabs", "CrabBodyA000.png")

_src = Image.open(PALETTE_SOURCE)
PALETTE = _src.getpalette()[:768]
BACKGROUND = _src.getpixel((0, 0))  # 0, magenta (255, 0, 255)
assert BACKGROUND == 0 and tuple(PALETTE[0:3]) == (255, 0, 255)


def nearest_index(rgb):
	best, best_d = None, None
	for i in range(1, 256):
		r, g, b = PALETTE[i * 3:i * 3 + 3]
		if (r, g, b) == (255, 0, 255):
			continue
		# Weighted distance (roughly perceptual).
		d = 2 * (r - rgb[0]) ** 2 + 4 * (g - rgb[1]) ** 2 + 3 * (b - rgb[2]) ** 2
		if best_d is None or d < best_d:
			best, best_d = i, d
	return best


# Named colour wishes -> palette index (resolved once).
COLOUR_WISHES = {
	# chicken
	"chick_white": (238, 236, 228), "chick_shade": (196, 192, 180), "chick_line": (122, 116, 104),
	"comb": (204, 36, 36), "comb_dark": (150, 24, 28), "beak": (236, 184, 40), "chick_leg": (222, 160, 48),
	"chick_leg_bg": (176, 120, 40),
	# pig
	"pig_light": (242, 178, 172), "pig_mid": (222, 140, 140), "pig_dark": (184, 100, 106),
	"pig_line": (123, 40, 36), "snout": (171, 93, 100), "nostril": (90, 40, 48),
	"pig_leg_bg": (176, 104, 110), "pig_hoof": (96, 64, 56),
	# cow
	"cow_white": (232, 230, 222), "cow_shade": (184, 180, 170), "cow_black": (34, 30, 30),
	"cow_black_hi": (70, 64, 62), "cow_line": (88, 84, 80), "udder": (171, 93, 100),
	"cow_nose": (171, 93, 100), "horn": (226, 214, 180), "horn_dark": (162, 146, 112),
	"hoof": (58, 46, 40),
	# sheep
	"wool": (232, 226, 204), "wool_shade": (196, 188, 164), "wool_line": (140, 132, 112),
	"sheep_face": (52, 46, 46), "sheep_face_hi": (88, 80, 76), "sheep_leg": (60, 52, 50),
	"sheep_leg_bg": (40, 34, 34),
	# goat
	"goat_light": (188, 142, 96), "goat_mid": (152, 104, 64), "goat_dark": (108, 70, 42),
	"goat_line": (72, 46, 30), "goat_horn": (156, 150, 136), "goat_beard": (226, 220, 200),
	# bull
	"bull_light": (120, 78, 52), "bull_mid": (88, 54, 36), "bull_dark": (60, 36, 26),
	"bull_line": (36, 22, 18), "ring": (212, 176, 64), "bull_nose": (150, 104, 96),
	# civilians
	"skin": (234, 182, 142), "skin_shade": (196, 140, 104), "skin_line": (120, 80, 60), "hair": (88, 58, 38),
	"hair_dark": (56, 38, 28), "straw": (232, 202, 112), "straw_dark": (184, 148, 68), "hood": (112, 80, 50), "hood_dark": (72, 50, 34),
	"mouth": (150, 70, 60),
	# shared
	"eye": (16, 16, 16), "eye_hi": (250, 250, 250), "tail_hair": (40, 34, 30),
}
COLOURS = {name: nearest_index(rgb) for name, rgb in COLOUR_WISHES.items()}


def rgb_of(index):
	return tuple(PALETTE[index * 3:index * 3 + 3])


def new_image(w, h):
	im = Image.new("P", (w, h), BACKGROUND)
	im.putpalette(PALETTE)
	return im


def save(im, path):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	im.putpalette(PALETTE)
	im.save(path, optimize=False)


def from_ascii(rows, legend):
	h, w = len(rows), len(rows[0])
	im = new_image(w, h)
	for y, row in enumerate(rows):
		assert len(row) == w, "row %d is %d wide, expected %d: %r" % (y, len(row), w, row)
		for x, ch in enumerate(row):
			if ch == ".":
				continue
			im.putpixel((x, y), COLOURS[legend[ch]])
	return im


def bob(im, box):
	"""Second animation frame: shift the pixels inside box down by one (head bob)."""
	x0, y0, x1, y1 = box
	out = im.copy()
	for x in range(x0, x1 + 1):
		for y in range(y1, y0 - 1, -1):
			src = im.getpixel((x, y - 1)) if y > y0 else BACKGROUND
			if y == y1 and src == BACKGROUND:
				continue
			out.putpixel((x, y), src)
	return out


###############################################################################
# Bodies (facing right). '.' = transparent.

CHICKEN = dict(
	name="Chicken", prefix="Chicken",
	legend={"W": "chick_white", "w": "chick_shade", "o": "chick_line", "R": "comb", "r": "comb_dark", "Y": "beak", "k": "eye"},
	art=[
		"....RR..",
		"...RWWo.",
		"o..WWkWY",
		"Wo.oWWr.",
		"WWowWWw.",
		"wWWWWWw.",
		".wwWWwo.",
		"..oooo..",
	],
	bob_box=(3, 0, 7, 4),
	hips={"rear": (2, 6), "front": (5, 6)},
	bg_nudge=0,  # BG legs hidden exactly behind the FG ones: reads as a two-legged bird
	leg=dict(E=5, C=2.5, thick=1, colour="chick_leg", colour_bg="chick_leg_bg", hoof=None, toes=True),
)

PIG = dict(
	name="Pig", prefix="Pig",
	legend={"L": "pig_light", "M": "pig_mid", "D": "pig_dark", "p": "pig_line", "S": "snout", "n": "nostril", "k": "eye", "t": "pig_line"},
	art=[
		"..............pp....",
		".t..ppppppppppDDp...",
		"t.tpLLLLLLLLLLLDDp..",
		".tpLLLLLLLLLLLLLDp..",
		"..pLLLLLLLLLLLLLLkp.",
		"..pLLLLLLLLLLLLLLLpp",
		"..pMLLLLLLLLLLLLLMSn",
		"..pMMLLLLLLLLLLLMMSS",
		"..pMMMMMMMMMMMMMMMpp",
		"..pDMMMMMMMMMMMMMDp.",
		"..pDDDMMMMMMMMMDDDp.",
		"...pppppppppppppppp.",
	],
	bob_box=(14, 0, 19, 8),
	hips={"rear": (4, 10), "front": (16, 10)},
	leg=dict(E=6, C=3, thick=2, colour="pig_mid", colour_bg="pig_leg_bg", hoof="pig_hoof"),
)

SHEEP = dict(
	name="Sheep", prefix="Sheep",
	legend={"W": "wool", "w": "wool_shade", "o": "wool_line", "F": "sheep_face", "f": "sheep_face_hi", "k": "eye", "e": "eye_hi"},
	art=[
		"...o.o.o.o.o........",
		"..oWoWoWoWoWo.......",
		".oWWWWWWWWWWWo..ff..",
		"oWWWWWWWWWWWWWofFFf.",
		"oWWWWWWWWWWWWWfFFeF.",
		"oWWWWWWWWWWWWWWoFFFF",
		"owWWWWWWWWWWWWWoFFFF",
		"owWWWWWWWWWWWWWo.FFf",
		"owwWWWWWWWWWWWwo....",
		"owwwwWWWWWWwwwwo....",
		".owwwwwwwwwwwwo.....",
		".owwwwwwwwwwwwo.....",
		"..o.o.o.o.o.o.......",
	],
	bob_box=(14, 2, 19, 8),
	hips={"rear": (2, 11), "front": (13, 11)},
	leg=dict(E=7, C=3.5, thick=2, colour="sheep_leg", colour_bg="sheep_leg_bg", hoof="sheep_leg_bg"),
)

GOAT = dict(
	name="Goat", prefix="Goat",
	legend={"L": "goat_light", "M": "goat_mid", "D": "goat_dark", "o": "goat_line", "h": "goat_horn", "b": "goat_beard", "k": "eye", "n": "goat_line"},
	art=[
		"............hh....",
		"...........h..h...",
		"...........hoMMo..",
		"............oMkMo.",
		"............oMMMLo",
		"..o.........oMMMMn",
		"..oo.......oLMMbo.",
		"..oLoooooooLLMMb..",
		"..oLLLLLLLLLLMMo..",
		"..oLLLLLLLLLLMo...",
		"..oMLLLLLLLLLMo...",
		"..oMMMMMMMMMMMo...",
		"..oDMMMMMMMMMDo...",
		"...ooooooooooo....",
	],
	bob_box=(11, 0, 17, 7),
	hips={"rear": (3, 12), "front": (13, 12)},
	leg=dict(E=7, C=3.5, thick=2, colour="goat_mid", colour_bg="goat_dark", hoof="goat_line"),
)

COW = dict(
	name="Cow", prefix="Cow",
	legend={"W": "cow_white", "w": "cow_shade", "B": "cow_black", "b": "cow_black_hi", "o": "cow_line", "u": "udder",
	        "n": "cow_nose", "h": "horn", "H": "horn_dark", "k": "eye", "t": "tail_hair"},
	art=[
		"........................h..h..",
		"........................HooH..",
		"..oooooooooooooooooooo..oBBWo.",
		".obBBBWWWWWWWWBBBWWWWWo.oBkWWo",
		"ooBBBBBWWWWWWBBBBBWWWWWooWWWWW",
		"o.oBBBWWWWWWWWBBBWWWWWWoBWWWWn",
		"o.oWWWWWWWWWWWWWWWWWWWWBBWWWnn",
		"o.oWWWWWBBBWWWWWWWWWWWBBo.onn.",
		"o.oWWWWBBBBBWWWWWWWWWWBo......",
		"t.oWWWWWBBBWWWWWWWWWWWBo......",
		"t.owWWWWWWWWWWWWWWWWWWwo......",
		"t.owwWWWWWWWWWWWBBWWWwwo......",
		"..owwwwWWWWWWWWBBBWWwwwo......",
		"..owwwwwwwwwwwwwwwwwwwwo......",
		"...oooouuuuooooooooooo........",
		"......ouuuuo..................",
		".......u..u...................",
		"..............................",
	],
	bob_box=(23, 0, 29, 9),
	hips={"rear": (5, 13), "front": (20, 13)},
	leg=dict(E=9, C=4.5, thick=3, colour="cow_white", colour_bg="cow_shade", hoof="hoof"),
)

BULL = dict(
	name="Bull", prefix="Bull",
	legend={"L": "bull_light", "M": "bull_mid", "D": "bull_dark", "o": "bull_line", "h": "horn", "H": "horn_dark",
	        "r": "ring", "n": "bull_nose", "k": "eye", "t": "tail_hair"},
	art=[
		"................................",
		"..................ooo...........",
		"................ooLLLooo..hh.hh.",
		"..ooooooooooooooLLLLLLLLo.Hh.Hh.",
		".oMLLLLLLLLLLLLLLLLLLLLLLoooHo..",
		"ooMMLLLLLLLLLLLLLLLLLLLLLoMMMMo.",
		"o.oMMMLLLLLLLLLLLLLLLLLLLoMkMMMo",
		"o.oMMMMMMMLLLLLLLLLLLLLLMoMMMMMn",
		"o.oMMMMMMMMMMMMMMMMMMMMMMMMMMMnn",
		"o.oMMMMMMMMMMMMMMMMMMMMMMMoMMnno",
		"t.oMMMMMMMMMMMMMMMMMMMMMMMo.oror",
		"t.oDMMMMMMMMMMMMMMMMMMMMMMo..rr.",
		"t.oDDMMMMMMMMMMMMMMMMMMMMDo.....",
		"..oDDDMMMMMMMMMMMMMMMMMMDDo.....",
		"..oDDDDDDMMMMMMMMMMMMMDDDDo.....",
		"..oDDDDDDDDDDDDDDDDDDDDDDDo.....",
		"...ooooooooooooooooooooooo......",
		"................................",
		"................................",
	],
	bob_box=(25, 1, 31, 12),
	hips={"rear": (6, 15), "front": (22, 15)},
	leg=dict(E=9, C=4.5, thick=3, colour="bull_mid", colour_bg="bull_dark", hoof="bull_line"),
)

ANIMALS = [CHICKEN, PIG, SHEEP, GOAT, COW, BULL]


###############################################################################
# Legs


def leg_lengths(E, C):
	return [C + (E - C) * (k + 0.5) / 4.0 for k in range(4)]


def leg_canvas(E, thick):
	maxd = math.ceil(E / 2.0) + thick
	w = 2 * maxd + 3
	if w % 2 == 0:
		w += 1
	h = int(math.ceil(E)) + 3
	return w, h, w // 2, 1  # width, height, joint x, joint y


def draw_leg_frame(spec, k, front, background):
	E, C, t = spec["E"], spec["C"], spec["thick"]
	w, h, jx, jy = leg_canvas(E, t)
	im = new_image(w, h)
	L = round(leg_lengths(E, C)[k])
	direction = 1 if front else -1
	a = E / 2.0
	d = 0 if k == 3 else 0.75 * math.sqrt(max(0.0, a * a - (L / 2.0) ** 2))
	knee_x, knee_y = jx + direction * d, jy + L / 2.0
	foot_y = jy + L
	main = COLOURS[spec["colour_bg"] if background else spec["colour"]]
	hoof = COLOURS[spec["hoof"]] if spec.get("hoof") else main

	def x_at(y):
		if y <= knee_y:
			f = (y - jy) / max(0.001, knee_y - jy)
			return jx + (knee_x - jx) * f
		f = (y - knee_y) / max(0.001, foot_y - knee_y)
		return knee_x + (jx - knee_x) * f

	for y in range(jy, foot_y + 1):
		cx = x_at(y)
		width = t + (1 if (y - jy) < L * 0.35 and t > 1 else 0)  # thicker thigh
		x0 = int(round(cx - (width - 1) / 2.0))
		is_hoof = spec.get("hoof") and y >= foot_y - (1 if t >= 3 else 0)
		for x in range(x0, x0 + width):
			im.putpixel((x, y), hoof if is_hoof else main)
	if spec.get("toes"):
		im.putpixel((jx + 1, foot_y), main)  # forward toe
	return im


def leg_files(animal, front, background):
	part = ("Front" if front else "Hind") + ("BG" if background else "FG")
	return "%sLeg%sA" % (animal["prefix"], part)


###############################################################################
# Body helpers


def body_frames(animal):
	f0 = from_ascii(animal["art"], animal["legend"])
	f1 = bob(f0, animal["bob_box"])
	return [f0, f1]


def body_origin(animal):
	"""The body's origin (its position and centre of rotation): horizontally centred, vertically halfway between the
	sprite's middle and the hips, which keeps the pivot low over the legs so walking doesn't tip the animal over."""
	w, h = len(animal["art"][0]), len(animal["art"])
	hip_y = animal["hips"]["rear"][1]
	return w // 2, (h // 2 + hip_y) // 2


def write_animal(animal):
	frames = body_frames(animal)
	for i, f in enumerate(frames):
		save(f, os.path.join(LIVESTOCK_DIR, animal["prefix"], "%sBodyA%03d.png" % (animal["prefix"], i)))
	for front in (True, False):
		for bg in (False, True):
			for k in range(4):
				im = draw_leg_frame(animal["leg"], k, front, bg)
				save(im, os.path.join(LIVESTOCK_DIR, animal["prefix"], "%s%03d.png" % (leg_files(animal, front, bg), k)))


###############################################################################
# Civilians: palette-shifted copies of the Green Dummy sprites.


def recolour(src_path, dst_path, hue_deg, sat, val_scale=1.0):
	im = Image.open(src_path)
	assert im.mode == "P"
	out = im.copy()
	cache = {}
	w, h = im.size
	for y in range(h):
		for x in range(w):
			i = im.getpixel((x, y))
			if i == BACKGROUND:
				continue
			if i not in cache:
				r, g, b = [c / 255.0 for c in PALETTE[i * 3:i * 3 + 3]]
				hh, ss, vv = colorsys.rgb_to_hsv(r, g, b)
				if 0.17 < hh < 0.45 and ss > 0.12:  # greenish dummy plastic
					nr, ng, nb = colorsys.hsv_to_rgb(hue_deg / 360.0, sat, min(1.0, vv * val_scale))
					cache[i] = nearest_index((int(nr * 255), int(ng * 255), int(nb * 255)))
				else:
					cache[i] = i
			out.putpixel((x, y), cache[i])
	save(out, dst_path)


CIVILIANS = {
	# name: (shirt hue, sat, value scale), (trouser hue, sat, value), (skin hue, sat, value)
	"Civilian": ((210, 0.45, 1.0), (30, 0.25, 0.75), (25, 0.42, 1.15)),
	"Farmhand": ((0, 0.55, 1.0), (215, 0.45, 0.85), (22, 0.48, 1.05)),
	"Monk": ((30, 0.50, 0.85), (30, 0.50, 0.75), (25, 0.38, 1.15)),
}


HEAD_LEGEND = {"S": "skin", "s": "skin_shade", "o": "skin_line", "h": "hair", "H": "hair_dark", "k": "eye", "m": "mouth",
               "y": "straw", "Y": "straw_dark", "c": "hood", "C": "hood_dark"}

# 11x11 heads, facing right, same footprint as the Green Dummy head (origin (5, 5), neck at the bottom middle).
HEADS = {
	"Civilian": [
		"...........",
		"...hhhhh...",
		"..hhhhhhhh.",
		"..hhhhhhhh.",
		"..hhhSSSkS.",
		"..hhsSSSSSo",
		"..hHSSSSSs.",
		"...hSSSSmS.",
		"....sSSSs..",
		".....sSs...",
		".....sSs...",
	],
	"Farmhand": [
		"....yyyy...",
		"...yyyyyy..",
		"...YYYYYY..",
		"yyyyyyyyyyy",
		"..hhhSSSkS.",
		"..hhsSSSSSo",
		"..hHSSSSSs.",
		"...hSSSSmS.",
		"....sSSSs..",
		".....sSs...",
		".....sSs...",
	],
	"Monk": [
		"...........",
		"...cccc....",
		"..ccSSSSs..",
		".cccSSSSSS.",
		".cccSSSSkS.",
		".CccsSSSSSo",
		".CccSSSSSs.",
		".CCcsSSSmS.",
		"..CCcsSSs..",
		"...CCcSs...",
		"....CcSs...",
	],
}


def write_civilians():
	for name, (shirt, trousers, skin) in CIVILIANS.items():
		d = os.path.join(CIVILIAN_DIR, name)
		save(from_ascii(HEADS[name], HEAD_LEGEND), os.path.join(d, name + "HeadA.png"))
		recolour(os.path.join(DUMMY_DIR, "TorsoA.png"), os.path.join(d, name + "TorsoA.png"), *shirt)
		for k in range(5):
			for layer in ("FG", "BG"):
				recolour(os.path.join(DUMMY_DIR, "Arm%sA%03d.png" % (layer, k)), os.path.join(d, "%sArm%sA%03d.png" % (name, layer, k)), *shirt)
				recolour(os.path.join(DUMMY_DIR, "Leg%sA%03d.png" % (layer, k)), os.path.join(d, "%sLeg%sA%03d.png" % (name, layer, k)), *trousers)


###############################################################################
# Civilians INI (AHumans built from the Green Dummy rig, with flesh wounds/gibs and no combat groups).

CIVILIAN_STATS = {
	"Civilian": dict(temperament="Skittish", gold=15, health=80, mass=30, inventory=None,
	                 description="An ordinary settler. Unarmed and in no mood for a fight: runs for cover when the shooting starts."),
	"Farmhand": dict(temperament="Defensive", gold=25, health=100, mass=32, inventory="Pistol",
	                 description="A hardy farm worker with a pistol for varmints. Won't start trouble, but will shoot back if attacked."),
	"Monk": dict(temperament="Pacifist", gold=10, health=90, mass=28, inventory=None, preset="Pacifist Monk",
	             description="A serene monk who has sworn never to raise a hand in violence, and does not flinch from danger either."),
}


def civilian_ini():
	t = []
	t.append("""///////////////////////////////////////////////////////////////////////
// Civilians: non-combatant humans.
// GENERATED by Base.rte/Actors/Livestock/Tools/generate_livestock.py - edit that and re-run.
// Rig, limb offsets and limb paths follow Base.rte/Actors/Infantry/GreenDummy (the only AHuman in Base.rte),
// but the body is defined from scratch so it does not inherit the Green Dummy's combat groups.


""")
	for name in CIVILIANS:
		d = "Base.rte/Actors/Civilians/%s/%s" % (name, name)
		if name == "Civilian":
			t.append("""AddEffect = Attachable
	PresetName = Civilian Head
	Mass = 6
	HitsMOs = 1
	GetsHitByMOs = 1
	SpriteFile = ContentFile
		FilePath = %(d)sHeadA.png
	FrameCount = 1
	SpriteOffset = Vector
		X = -5
		Y = -5
	AngularVel = 6
	EntryWound = AEmitter
		CopyOf = Wound Flesh Entry
	ExitWound = AEmitter
		CopyOf = Wound Flesh Exit
	AtomGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = Flesh
		Resolution = 4
		Depth = 0
	DeepCheck = 0
	JointStrength = 150
	JointStiffness = 0.1
	BreakWound = AEmitter
		CopyOf = Wound Bone Break
	ParentBreakWound = AEmitter
		CopyOf = Wound Bone Break
	JointOffset = Vector
		X = 0
		Y = 6
	DrawAfterParent = 1
	AddGib = Gib
		GibParticle = MOPixel
			CopyOf = Drop Blood
		Count = 6
		MinVelocity = 1
		MaxVelocity = 8
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Flesh Small A
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Bone Small C
		Offset = Vector
			X = 1
			Y = -2
	AddGib = Gib
		GibParticle = MOSParticle
			CopyOf = Gib Flesh Tiny A
		Offset = Vector
			X = -2
			Y = 2
		Count = 2
	GibImpulseLimit = 300
	GibWoundLimit = 3
	GibSound = SoundContainer
		CopyOf = Flesh Head Gib


""" % dict(d=d))
		else:
			t.append("""AddEffect = Attachable
	CopyOf = Civilian Head
	PresetName = %(n)s Head
	SpriteFile = ContentFile
		FilePath = %(d)sHeadA.png
	FrameCount = 1


""" % dict(n=name, d=d))
		t.append("""AddActor = Arm
	CopyOf = Green Dummy Arm FG
	PresetName = %(n)s Arm FG
	SpriteFile = ContentFile
		FilePath = %(d)sArmFGA.png
	FrameCount = 5
	EntryWound = AEmitter
		CopyOf = Wound Flesh Entry
	ExitWound = AEmitter
		CopyOf = Wound Flesh Exit
	BreakWound = AEmitter
		CopyOf = Wound Bone Break
	ParentBreakWound = AEmitter
		CopyOf = Wound Bone Break
	GibSound = SoundContainer
		CopyOf = Flesh Limb Gib
	AddGib = Gib
		GibParticle = MOPixel
			CopyOf = Drop Blood
		Count = 3
		MinVelocity = 1
		MaxVelocity = 6


AddActor = Arm
	CopyOf = %(n)s Arm FG
	PresetName = %(n)s Arm BG
	SpriteFile = ContentFile
		FilePath = %(d)sArmBGA.png
	FrameCount = 5
	Hand = ContentFile
		FilePath = Base.rte/Actors/Infantry/GreenDummy/HandBGA.png


AddActor = Leg
	CopyOf = Green Dummy Leg FG
	PresetName = %(n)s Leg FG
	SpriteFile = ContentFile
		FilePath = %(d)sLegFGA.png
	FrameCount = 5
	EntryWound = AEmitter
		CopyOf = Wound Flesh Entry
	ExitWound = AEmitter
		CopyOf = Wound Flesh Exit
	BreakWound = AEmitter
		CopyOf = Wound Bone Break
	ParentBreakWound = AEmitter
		CopyOf = Wound Bone Break
	GibSound = SoundContainer
		CopyOf = Flesh Limb Gib
	AddGib = Gib
		GibParticle = MOPixel
			CopyOf = Drop Blood
		Count = 3
		MinVelocity = 1
		MaxVelocity = 6


AddActor = Leg
	CopyOf = %(n)s Leg FG
	PresetName = %(n)s Leg BG
	SpriteFile = ContentFile
		FilePath = %(d)sLegBGA.png
	FrameCount = 5
	Foot = Attachable
		CopyOf = Green Dummy Foot BG
		ParentOffset = Vector
			X = -11
			Y = -10
	DrawAfterParent = 0


""" % dict(n=name, d=d))

	# Base civilian body.
	st = CIVILIAN_STATS["Civilian"]
	t.append("""AddActor = AHuman
	PresetName = Civilian
	Description = "%(desc)s"
	AddToGroup = Actors - Civilians
	AddToGroup = Non-combatants
	Temperament = %(temp)s
	NonCombatant = 1
	Buyable = 1
	Mass = %(mass)d
	GoldValue = %(gold)d
	HitsMOs = 1
	GetsHitByMOs = 1
	ScriptPath = Base.rte/AI/HumanAI.lua
	SpriteFile = ContentFile
		FilePath = Base.rte/Actors/Civilians/Civilian/CivilianTorsoA.png
	FrameCount = 1
	SpriteOffset = Vector
		X = -4
		Y = -16
	EntryWound = AEmitter
		CopyOf = Wound Flesh Entry
	ExitWound = AEmitter
		CopyOf = Wound Flesh Exit
	AtomGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = Flesh
		Resolution = 4
		Depth = 0
	DeepGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = Flesh
		Resolution = 6
		Depth = 3
	DeepCheck = 0
	BodyHitSound = SoundContainer
		CopyOf = Flesh Body Blunt Hit
	PainSound = SoundContainer
		CopyOf = Human Pain
	DeathSound = SoundContainer
		CopyOf = Human Death
	DeviceSwitchSound = SoundContainer
		CopyOf = Foley Light Cloth Light
	MaxHealth = %(health)d
	Health = %(health)d
	Organic = 1
	ImpulseDamageThreshold = 2500
	AimDistance = 30
	Perceptiveness = 1
	CharHeight = 100
	HolsterOffset = Vector
		X = -6
		Y = -8
	ReloadOffset = Vector
		X = 2
		Y = 4
	Head = Attachable
		CopyOf = Civilian Head
		ParentOffset = Vector
			X = -1
			Y = -13
	Jetpack = AEJetpack
		CopyOf = Jetpack
		ParentOffset = Vector
			X = -6
			Y = -1
		ParticlesPerMinute = 8400
		JumpTime = 1.5
		JumpReplenishRate = 1.5
		JumpAngleRange = 0.16
	FGArm = Arm
		CopyOf = Civilian Arm FG
		ParentOffset = Vector
			X = 0
			Y = -8
	BGArm = Arm
		CopyOf = Civilian Arm BG
		ParentOffset = Vector
			X = 4
			Y = -9
	FGLeg = Leg
		CopyOf = Civilian Leg FG
		ParentOffset = Vector
			X = 0
			Y = 1
	BGLeg = Leg
		CopyOf = Civilian Leg BG
		ParentOffset = Vector
			X = 2
			Y = 1
	HandGroup = AtomGroup
		CopyOf = Human Hand
	FGFootGroup = AtomGroup
		CopyOf = Human Foot
	BGFootGroup = AtomGroup
		CopyOf = Human Foot
	StrideSound = SoundContainer
		CopyOf = Footstep Light Generic
	StandLimbPath = LimbPath
		PresetName = Civilian Stand Path
		StartOffset = Vector
			X = 0
			Y = 17
		StartSegCount = 0
		TravelSpeed = 0.5
		PushForce = 1800
	StandLimbPathBG = LimbPath
		CopyOf = Civilian Stand Path
		PresetName = Civilian Stand Path BG
		StartOffset = Vector
			X = 5
			Y = 17
	WalkLimbPath = LimbPath
		CopyOf = Human Walk Path
		PresetName = Civilian Walk Path
		StartOffset = Vector
			X = 10
			Y = -3
		TravelSpeed = 2.6
	WalkRotAngleTarget = -0.05
	RunLimbPath = LimbPath
		CopyOf = Human Run Path
		TravelSpeed = 4.0
	RunRotAngleTarget = -0.07
	CrouchLimbPath = LimbPath
		CopyOf = Human Crouch Path
	CrouchLimbPathBG = LimbPath
		CopyOf = Human Crouch Path BG
	CrouchRotAngleTarget = -0.6
	CrawlLimbPath = LimbPath
		CopyOf = Human Crawl Path
	ArmCrawlLimbPath = LimbPath
		CopyOf = Human Arm Crawl Path
	ClimbLimbPath = LimbPath
		CopyOf = Human Climb Path
	JumpLimbPath = LimbPath
		CopyOf = Human Jump Path
	DislodgeLimbPath = LimbPath
		CopyOf = Human Dislodge Path
	AddGib = Gib
		GibParticle = MOPixel
			CopyOf = Drop Blood
		Count = 12
		MinVelocity = 2
		MaxVelocity = 10
	AddGib = Gib
		GibParticle = MOSParticle
			CopyOf = Blood Spray Particle
		Count = 2
		MinVelocity = 1
		MaxVelocity = 5
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Flesh Small B
		Offset = Vector
			X = -2
			Y = -3
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Flesh Small D
		Offset = Vector
			X = 1
			Y = -8
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Bone Small B
		Offset = Vector
			X = 0
			Y = -5
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Bone Small D
		Offset = Vector
			X = -1
			Y = -10
	AddGib = Gib
		GibParticle = MOSParticle
			CopyOf = Gib Flesh Tiny A
		Offset = Vector
			X = -3
			Y = -9
		Count = 3
	AddGib = Gib
		GibParticle = MOSParticle
			CopyOf = Gib Flesh Micro A
		Offset = Vector
			X = -1
			Y = -1
		Count = 3
	GibImpulseLimit = 3000
	GibWoundLimit = 6
	GibSound = SoundContainer
		CopyOf = Flesh Torso Gib


""" % dict(desc=st["description"], temp=st["temperament"], mass=st["mass"], gold=st["gold"], health=st["health"]))

	for name in ("Farmhand", "Monk"):
		st = CIVILIAN_STATS[name]
		d = "Base.rte/Actors/Civilians/%s/%s" % (name, name)
		block = """AddActor = AHuman
	CopyOf = Civilian
	PresetName = %(preset)s
	Description = "%(desc)s"
	AddToGroup = Actors - Civilians
	AddToGroup = Non-combatants
	Temperament = %(temp)s
	NonCombatant = 1
	Mass = %(mass)d
	GoldValue = %(gold)d
	SpriteFile = ContentFile
		FilePath = %(d)sTorsoA.png
	FrameCount = 1
	MaxHealth = %(health)d
	Health = %(health)d
	Head = Attachable
		CopyOf = %(n)s Head
		ParentOffset = Vector
			X = -1
			Y = -13
	FGArm = Arm
		CopyOf = %(n)s Arm FG
		ParentOffset = Vector
			X = 0
			Y = -8
	BGArm = Arm
		CopyOf = %(n)s Arm BG
		ParentOffset = Vector
			X = 4
			Y = -9
	FGLeg = Leg
		CopyOf = %(n)s Leg FG
		ParentOffset = Vector
			X = 0
			Y = 1
	BGLeg = Leg
		CopyOf = %(n)s Leg BG
		ParentOffset = Vector
			X = 2
			Y = 1
""" % dict(preset=st.get("preset", name), desc=st["description"], temp=st["temperament"], mass=st["mass"], gold=st["gold"],
		           health=st["health"], n=name, d=d)
		if st["inventory"]:
			block += "\tAddInventory = HDFirearm\n\t\tCopyOf = %s\n" % st["inventory"]
		t.append(block + "\n\n")
	return "".join(t)


def write_civilians_ini():
	with open(os.path.join(CIVILIAN_DIR, "Civilians.ini"), "w", newline="\n") as f:
		f.write(civilian_ini().rstrip("\n") + "\n")


###############################################################################
# INI


def vec(indent, name, x, y):
	t = "\t" * indent
	return "%s%s = Vector\n%s\tX = %s\n%s\tY = %s\n" % (t, name, t, fmt(x), t, fmt(y))


def fmt(v):
	v = round(float(v), 2)
	return str(int(v)) if v == int(v) else str(v)


def gib(indent, kind, preset, x=0, y=0, count=None, vmin=None, vmax=None):
	t = "\t" * indent
	s = "%sAddGib = Gib\n%s\tGibParticle = %s\n%s\t\tCopyOf = %s\n" % (t, t, kind, t, preset)
	if count:
		s += "%s\tCount = %d\n" % (t, count)
	if vmin is not None:
		s += "%s\tMinVelocity = %s\n%s\tMaxVelocity = %s\n" % (t, fmt(vmin), t, fmt(vmax))
	if x or y:
		s += vec(indent + 1, "Offset", x, y)
	return s


STATS = {
	# name: mass, leg mass, health, gold, temperament, walk speed, gib impulse, gib wounds, joint strength,
	#       char height, perceptiveness, description, extra gib flavour
	"Chicken": dict(mass=4, leg_mass=0.4, health=25, gold=5, temperament="Skittish", speed=2.6, gib_impulse=400, gib_wounds=2,
	                joint=40, char_height=15, perceptiveness=0.6, flavour="Gib Panel White Micro A",
	                description="A nervous little bird. Lays eggs, flaps about and runs from anything louder than a cluck."),
	"Pig": dict(mass=38, leg_mass=4, health=80, gold=20, temperament="Skittish", speed=2.4, gib_impulse=3000, gib_wounds=6,
	            joint=200, char_height=40, perceptiveness=0.4, flavour=None,
	            description="A plump pink pig. Squeals and bolts at the first sign of trouble."),
	"Sheep": dict(mass=34, leg_mass=4, health=70, gold=20, temperament="Skittish", speed=2.4, gib_impulse=2800, gib_wounds=6,
	              joint=200, char_height=40, perceptiveness=0.4, flavour="Gib Panel White Tiny A",
	              description="A woolly sheep. Follows the flock and flees from danger."),
	"Goat": dict(mass=30, leg_mass=4, health=90, gold=25, temperament="Defensive", speed=2.5, gib_impulse=3000, gib_wounds=7,
	             joint=220, char_height=45, perceptiveness=0.5, flavour=None,
	             description="A stubborn goat. Minds its own business, but will butt back if you start something."),
	"Cow": dict(mass=120, leg_mass=10, health=150, gold=40, temperament="Skittish", speed=2.0, gib_impulse=7000, gib_wounds=12,
	            joint=500, char_height=70, perceptiveness=0.3, flavour=None,
	            description="A big black-and-white dairy cow. Gentle, heavy, and quick to stampede away from gunfire."),
	"Bull": dict(mass=140, leg_mass=12, health=200, gold=50, temperament="Defensive", speed=2.3, gib_impulse=9000, gib_wounds=15,
	             joint=600, char_height=75, perceptiveness=0.5, flavour=None,
	             description="A dark, horned bull with a brass nose ring. Leave it alone and it leaves you alone."),
}


def leg_ini(animal, front, bg, stats):
	spec = animal["leg"]
	E, C, t = spec["E"], spec["C"], spec["thick"]
	w, h, jx, jy = leg_canvas(E, t)
	cx, cy = w // 2, h // 2
	base = "%s Leg %s" % (animal["name"], "Front" if front else "Hind")
	name = base + (" BG" if bg else " FG")
	path = "Base.rte/Actors/Livestock/%s/%s.png" % (animal["prefix"], leg_files(animal, front, bg))
	S = stand_depth(spec)
	s = "AddEffect = Leg\n"
	if bg:
		s += "\tCopyOf = %s FG\n\tPresetName = %s\n" % (base, name)
		s += "\tSpriteFile = ContentFile\n\t\tFilePath = %s\n\tFrameCount = 4\n\tDrawAfterParent = 0\n\n\n" % path
		return s
	s += "\tPresetName = %s\n" % name
	s += "\tMass = %s\n\tHitsMOs = 1\n\tGetsHitByMOs = 1\n" % fmt(stats["leg_mass"])
	s += "\tSpriteFile = ContentFile\n\t\tFilePath = %s\n\tFrameCount = 4\n" % path
	s += vec(1, "SpriteOffset", -cx, -cy)
	s += "\tAngularVel = 6\n"
	s += "\tEntryWound = AEmitter\n\t\tCopyOf = Wound Flesh Entry\n\tExitWound = AEmitter\n\t\tCopyOf = Wound Flesh Exit\n"
	s += "\tAtomGroup = AtomGroup\n\t\tAutoGenerate = 1\n\t\tMaterial = Material\n\t\t\tCopyOf = Flesh\n\t\tResolution = %d\n\t\tDepth = 0\n" % (1 if t == 1 else 2)
	s += "\tDeepCheck = 0\n"
	s += "\tJointStrength = %d\n\tJointStiffness = 0.5\n" % stats["joint"]
	s += "\tBreakWound = AEmitter\n\t\tCopyOf = Wound Bone Break\n\tParentBreakWound = AEmitter\n\t\tCopyOf = Wound Bone Break\n"
	s += vec(1, "JointOffset", jx - cx, jy - cy)
	s += "\tDrawAfterParent = 1\n"
	s += vec(1, "ExtendedOffset", 0, E)
	s += vec(1, "ContractedOffset", 0, C)
	s += vec(1, "IdleOffset", 0, S)
	s += "\tMoveSpeed = 0.4\n"
	s += "\tGibImpulseLimit = %d\n\tGibWoundLimit = %d\n" % (max(300, stats["gib_impulse"] // 4), max(2, stats["gib_wounds"] // 2))
	s += "\tGibSound = SoundContainer\n\t\tCopyOf = Flesh Limb Gib\n"
	s += gib(1, "MOPixel", "Drop Blood", count=2 if t == 1 else 4, vmin=1, vmax=6)
	s += gib(1, "MOSParticle", "Gib Flesh Tiny A", 0, 1)
	if t > 1:
		s += gib(1, "MOSParticle", "Gib Bone Tiny A", 0, -1)
		s += gib(1, "MOSParticle", "Gib Flesh Micro A", 1, 2)
	else:
		s += gib(1, "MOSParticle", "Gib Bone Micro A", 0, -1)
	s += "\n\n"
	return s


def stand_depth(spec):
	return round(spec["E"] * 0.85, 1)


def animal_ini(animal):
	stats = STATS[animal["name"]]
	spec = animal["leg"]
	E = spec["E"]
	S = stand_depth(spec)
	splay = max(1.0, round(E * 0.15, 1))  # stand with the hind feet a little back and the front feet a little forward
	half_stride = round(E * 0.4, 1)
	lift = max(1.0, round(E * 0.3, 1))
	total_mass = stats["mass"] + 4 * stats["leg_mass"]
	stand_push = int(round(total_mass * 30, -1))
	walk_push = int(round(total_mass * 30, -1))
	dislodge_push = int(round(total_mass * 110, -1))
	w, h = len(animal["art"][0]), len(animal["art"])
	ox, oy = body_origin(animal)
	small = spec["thick"] == 1
	foot = "Livestock Foot Small" if small else "Livestock Foot Large"

	s = "///////////////////////////////////////////////////////////////////////\n// %s\n\n\n" % animal["name"]
	for front in (False, True):
		s += leg_ini(animal, front, False, stats)
		s += leg_ini(animal, front, True, stats)

	s += "AddActor = ACrab\n"
	s += "\tPresetName = %s\n" % animal["name"]
	s += "\tDescription = \"%s\"\n" % stats["description"]
	s += "\tAddToGroup = Actors - Livestock\n\tAddToGroup = Non-combatants\n"
	s += "\tTemperament = %s\n\tNonCombatant = 1\n" % stats["temperament"]
	s += "\tGoldValue = %d\n\tBuyable = 1\n" % stats["gold"]
	s += "\tMass = %s\n\tHitsMOs = 1\n\tGetsHitByMOs = 1\n\tIgnoresTeamHits = 0\n" % fmt(stats["mass"])
	s += "\tScriptPath = Base.rte/AI/CrabAI.lua\n"
	s += "\tSpriteFile = ContentFile\n\t\tFilePath = Base.rte/Actors/Livestock/%s/%sBodyA.png\n" % (animal["prefix"], animal["prefix"])
	s += "\tFrameCount = 2\n\tSpriteAnimMode = 4\n\tSpriteAnimDuration = %d\n" % (300 if small else 700)
	s += vec(1, "SpriteOffset", -ox, -oy)
	s += "\tEntryWound = AEmitter\n\t\tCopyOf = Wound Flesh Entry\n\tExitWound = AEmitter\n\t\tCopyOf = Wound Flesh Exit\n"
	s += "\tAtomGroup = AtomGroup\n\t\tAutoGenerate = 1\n\t\tMaterial = Material\n\t\t\tCopyOf = Flesh\n\t\tResolution = %d\n\t\tDepth = 0\n" % (2 if small else 4)
	s += "\tDeepCheck = 0\n"
	s += "\tBodyHitSound = SoundContainer\n\t\tCopyOf = Flesh Body Blunt Hit\n"
	s += "\tPainSound = SoundContainer\n\t\tCopyOf = Flesh Limb Impact\n"
	s += "\tDeathSound = SoundContainer\n\t\tCopyOf = Flesh Torso Impact\n"
	s += "\tPassengerSlots = %d\n" % (1 if total_mass > 100 else 0)
	s += "\tMaxHealth = %d\n\tHealth = %d\n" % (stats["health"], stats["health"])
	s += "\tOrganic = 1\n"
	s += "\tImpulseDamageThreshold = %d\n" % max(300, int(total_mass * 18))
	s += "\tPerceptiveness = %s\n\tCharHeight = %d\n" % (fmt(stats["perceptiveness"]), stats["char_height"])

	hind, front = animal["hips"]["rear"], animal["hips"]["front"]
	# L legs = hind pair, R legs = front pair. BG legs sit one pixel further in.
	slots = [
		("LFGLeg", "%s Leg Hind FG" % animal["name"], hind, 0),
		("LBGLeg", "%s Leg Hind BG" % animal["name"], hind, animal.get("bg_nudge", 1)),
		("RFGLeg", "%s Leg Front FG" % animal["name"], front, 0),
		("RBGLeg", "%s Leg Front BG" % animal["name"], front, -animal.get("bg_nudge", 1)),
	]
	for slot, preset, (hx, hy), nudge in slots:
		s += "\t%s = Leg\n\t\tCopyOf = %s\n" % (slot, preset)
		s += vec(2, "ParentOffset", hx + nudge - ox, hy - oy)
	s += "\tLFootGroup = AtomGroup\n\t\tCopyOf = %s\n" % foot
	s += "\tRFootGroup = AtomGroup\n\t\tCopyOf = %s\n" % foot

	p = animal["name"]
	s += "\tLStandLimbPath = LimbPath\n\t\tPresetName = %s Stand Path Left\n" % p
	s += vec(2, "StartOffset", -splay, S)
	s += "\t\tStartSegCount = 0\n\t\tTravelSpeed = 0.5\n\t\tPushForce = %d\n" % stand_push
	s += "\tLWalkLimbPath = LimbPath\n\t\tPresetName = %s Walk Path\n" % p
	# Like the crab, the stride (re)starts above the hip joint: LimbPath::RestartFree gives up if the first point is inside
	# terrain, so a low start point leaves a sunk animal unable to ever take another step. The two start segments swing
	# the foot forward and down to the ground; then it pushes back along the ground and lifts.
	s += vec(2, "StartOffset", -half_stride / 2.0, -1)
	s += "\t\tStartSegCount = 2\n"
	for dx, dy in [(half_stride * 1.5, S - lift + 1), (0, lift), (-half_stride, 0), (-half_stride, 0)]:
		s += vec(2, "AddSegment", dx, dy)
	s += vec(2, "AddSegment", half_stride / 2.0, -lift)
	s += "\t\tEndSegCount = 1\n\t\tTravelSpeed = %s\n\t\tPushForce = %d\n" % (fmt(stats["speed"]), walk_push)
	s += "\tLDislodgeLimbPath = LimbPath\n\t\tPresetName = %s Dislodge Path Left\n" % p
	s += vec(2, "StartOffset", -0.5, -round(E * 0.6, 1))
	s += "\t\tStartSegCount = 0\n"
	s += vec(2, "AddSegment", 0, round(E * 0.5, 1))
	s += "\t\tTravelSpeed = %s\n\t\tPushForce = %d\n" % (fmt(stats["speed"]), dislodge_push)
	s += "\tRStandLimbPath = LimbPath\n\t\tCopyOf = %s Stand Path Left\n\t\tPresetName = %s Stand Path Right\n" % (p, p)
	s += vec(2, "StartOffset", splay, S)
	s += "\tRWalkLimbPath = LimbPath\n\t\tCopyOf = %s Walk Path\n" % p
	s += "\tRDislodgeLimbPath = LimbPath\n\t\tCopyOf = %s Dislodge Path Left\n\t\tPresetName = %s Dislodge Path Right\n" % (p, p)
	s += vec(2, "StartOffset", 0.5, -round(E * 0.6, 1))

	s += "\tGibImpulseLimit = %d\n\tGibWoundLimit = %d\n" % (stats["gib_impulse"], stats["gib_wounds"])
	s += "\tGibSound = SoundContainer\n\t\tCopyOf = Flesh Torso Gib\n"
	scale = max(1, int(round(math.sqrt(total_mass / 20.0))))
	s += gib(1, "MOPixel", "Drop Blood", count=4 * scale + 2, vmin=2, vmax=10)
	s += gib(1, "MOSParticle", "Blood Spray Particle", count=scale, vmin=1, vmax=5)
	hw, hh = (w - 2) // 2, (h - 2) // 2
	if small:
		s += gib(1, "MOSParticle", "Gib Flesh Tiny A", -1, 0)
		s += gib(1, "MOSParticle", "Gib Bone Micro A", 1, -1)
		s += gib(1, "MOSParticle", "Gib Flesh Micro A", 2, 1)
	else:
		flesh = ["Gib Flesh Small A", "Gib Flesh Small B", "Gib Flesh Small C", "Gib Flesh Small D"]
		bones = ["Gib Bone Small A", "Gib Bone Small B", "Gib Bone Small C", "Gib Bone Small D", "Gib Bone Small E"]
		n = min(4, scale + 1)
		for i in range(n):
			s += gib(1, "MOSRotating", flesh[i % 4], round(-hw + (2 * hw) * i / max(1, n - 1)), (-1) ** i)
		for i in range(n):
			s += gib(1, "MOSRotating", bones[i % 5], round(-hw / 2 + hw * i / max(1, n - 1)), -(-1) ** i)
		s += gib(1, "MOSParticle", "Gib Flesh Tiny A", -2, -hh // 2, count=scale + 1)
		s += gib(1, "MOSParticle", "Gib Flesh Micro A", 2, hh // 2, count=scale + 2)
		s += gib(1, "MOSParticle", "Gib Bone Tiny A", 0, 0, count=scale)
	if stats["flavour"]:
		s += gib(1, "MOSParticle", stats["flavour"], 0, -hh // 2, count=6 if small else 8, vmin=1, vmax=6)
	s += "\n\n"
	return s


INI_HEADER = """///////////////////////////////////////////////////////////////////////
// Livestock: non-combatant farm animals.
// GENERATED by Base.rte/Actors/Livestock/Tools/generate_livestock.py - edit that and re-run.
//
// Each animal is an ACrab with no turret: L legs are the hind pair, R legs the front pair.
// Leg sprites point straight down; ContractedOffset/ExtendedOffset are (0, C)/(0, E) and
// sprite frame k shows the foot at C + (E - C) * (k + 0.5) / 4 below the hip joint.


AddActor = AtomGroup
	PresetName = Livestock Foot Small
	AutoGenerate = 0
	AddAtom = Atom
		Offset = Vector
			X = -1
			Y = 0
		Material = Material
			CopyOf = Rubber
	AddAtom = Atom
		Material = Material
			CopyOf = Rubber
	AddAtom = Atom
		Offset = Vector
			X = 1
			Y = 0
		Material = Material
			CopyOf = Rubber
	JointOffset = Vector
		X = 0
		Y = 0


AddActor = AtomGroup
	PresetName = Livestock Foot Large
	AutoGenerate = 0
	AddAtom = Atom
		Offset = Vector
			X = -2
			Y = 0
		Material = Material
			CopyOf = Rubber
	AddAtom = Atom
		Offset = Vector
			X = -1
			Y = 0
		Material = Material
			CopyOf = Rubber
	AddAtom = Atom
		Material = Material
			CopyOf = Rubber
	AddAtom = Atom
		Offset = Vector
			X = 1
			Y = 0
		Material = Material
			CopyOf = Rubber
	AddAtom = Atom
		Offset = Vector
			X = 2
			Y = 0
		Material = Material
			CopyOf = Rubber
	JointOffset = Vector
		X = 0
		Y = 0


"""


def write_ini():
	text = INI_HEADER + "".join(animal_ini(a) for a in ANIMALS)
	with open(os.path.join(LIVESTOCK_DIR, "Livestock.ini"), "w", newline="\n") as f:
		f.write(text.rstrip("\n") + "\n")


###############################################################################
# Preview


def paste_p(canvas, im, x, y):
	w, h = im.size
	for yy in range(h):
		for xx in range(w):
			i = im.getpixel((xx, yy))
			if i != BACKGROUND:
				cx, cy = x + xx, y + yy
				if 0 <= cx < canvas.width and 0 <= cy < canvas.height:
					canvas.putpixel((cx, cy), rgb_of(i))


def compose_animal(animal, frame, leg_frame=2):
	"""Returns an RGB-ready list of (image, x, y) placements relative to the body origin."""
	ox, oy = body_origin(animal)
	spec = animal["leg"]
	lw, lh, jx, jy = leg_canvas(spec["E"], spec["thick"])
	hind, front = animal["hips"]["rear"], animal["hips"]["front"]
	parts = []
	for (hx, hy), is_front, bg, nudge in [(hind, False, True, animal.get("bg_nudge", 1)), (front, True, True, -animal.get("bg_nudge", 1))]:
		parts.append((draw_leg_frame(spec, leg_frame, is_front, bg), hx + nudge - jx, hy - jy))
	parts.append((body_frames(animal)[frame], 0, 0))
	for (hx, hy), is_front in [(hind, False), (front, True)]:
		parts.append((draw_leg_frame(spec, leg_frame, is_front, False), hx - jx, hy - jy))
	ground = animal["hips"]["rear"][1] + stand_depth(spec)
	return parts, ground


def write_preview(path, scale=6):
	pad = 5
	columns = []
	for a in ANIMALS:
		rows = []
		for frame, leg_frame in ((0, 2), (1, 0)):
			parts, ground = compose_animal(a, frame, leg_frame)
			x0 = min(x for p, x, y in parts)
			x1 = max(x + p.width for p, x, y in parts)
			rows.append((parts, ground, x0))
		columns.append((rows, x1 - x0))
	civ = []
	for name in CIVILIANS:
		d = os.path.join(CIVILIAN_DIR, name)
		civ.append([Image.open(os.path.join(d, f)) for f in (name + "LegBGA004.png", name + "ArmBGA000.png", name + "TorsoA.png",
		                                                     name + "HeadA.png", name + "LegFGA004.png", name + "ArmFGA000.png")])
	row_h = 30
	width = sum(w + pad for rows, w in columns) + pad + 6
	canvas = Image.new("RGB", (width, row_h * 3 + 24), (128, 128, 128))
	x = pad
	for rows, w in columns:
		for r, (parts, ground, x0) in enumerate(rows):
			gy = r * row_h + 27
			top = gy - int(round(ground))
			for p, px, py in parts:
				paste_p(canvas, p, x - x0 + px, top + py)
			for gx in range(x - 1, x + w + 1):
				canvas.putpixel((gx, gy + 1), (96, 76, 56))
		x += w + pad
	# Soldier-height ruler (24 px) at the right edge of the first row.
	for yy in range(24):
		canvas.putpixel((width - 3, 27 - yy), (255, 255, 255) if yy % 2 == 0 else (40, 40, 40))
	# Civilians: torso/head/limbs at roughly their Green Dummy offsets (origin = torso origin), legs rotated to stand.
	cx, gy = pad + 8, row_h * 2 + 44
	for leg_bg, arm_bg, torso, head, leg_fg, arm_fg in civ:
		oy = gy - 18
		# Leg frame 0 is horizontal (joint at pixel (2, 9)); rotate it to point down: joint -> (h - 1 - 9, 2).
		lbg = leg_bg.rotate(-90, expand=True)
		lfg = leg_fg.rotate(-90, expand=True)
		jx, jy = leg_fg.height - 1 - 9, 2
		paste_p(canvas, lbg, cx + 2 - jx, oy + 1 - jy)
		paste_p(canvas, arm_bg, cx + 4 - 6, oy - 9 - 3)
		paste_p(canvas, torso, cx - 4, oy - 16)
		paste_p(canvas, head, cx - 1 - 5, oy - 13 - 5)
		paste_p(canvas, lfg, cx - jx, oy + 1 - jy)
		paste_p(canvas, arm_fg, cx - 6, oy - 8 - 3)
		for gx in range(cx - 8, cx + 10):
			canvas.putpixel((gx, gy + 1), (96, 76, 56))
		cx += 26
	canvas = canvas.resize((canvas.width * scale, canvas.height * scale), Image.NEAREST)
	os.makedirs(os.path.dirname(path), exist_ok=True)
	canvas.save(path)


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--preview", help="write a scaled RGB preview sheet to this path")
	ap.add_argument("--list-colours", action="store_true")
	args = ap.parse_args()
	if args.list_colours:
		for name, idx in COLOURS.items():
			print("%-16s wish %-16s -> index %3d %s" % (name, COLOUR_WISHES[name], idx, rgb_of(idx)))
	for a in ANIMALS:
		write_animal(a)
	write_civilians()
	write_civilians_ini()
	write_ini()
	if args.preview:
		write_preview(args.preview)
	print("done")


if __name__ == "__main__":
	main()
