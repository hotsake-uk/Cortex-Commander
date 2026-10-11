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
	# The pig leans on the deeper pinks: the game's lighting blows the palest ones out to white in daylight.
	"pig_hi": (251, 196, 179), "pig_light": (250, 155, 133), "pig_mid": (173, 84, 92), "pig_dark": (133, 61, 45),
	"pig_line": (123, 40, 36), "snout": (171, 93, 100), "nostril": (90, 40, 48),
	"pig_leg_bg": (133, 61, 45), "pig_hoof": (96, 64, 56),
	# cow
	"cow_hi": (250, 250, 246), "cow_shade_dark": (140, 136, 128), "udder_hi": (226, 150, 150), "udder_dark": (120, 60, 66),
	"cow_white": (232, 230, 222), "cow_shade": (184, 180, 170), "cow_black": (34, 30, 30),
	"cow_black_hi": (70, 64, 62), "cow_line": (88, 84, 80), "udder": (171, 93, 100),
	"cow_nose": (171, 93, 100), "horn_tip": (60, 52, 44), "horn": (226, 214, 180), "horn_dark": (162, 146, 112),
	"hoof": (58, 46, 40),
	# sheep
	"wool_hi": (250, 248, 236), "wool_dark": (160, 150, 124), "wool": (232, 226, 204), "wool_shade": (196, 188, 164), "wool_line": (140, 132, 112),
	"sheep_face": (52, 46, 46), "sheep_face_hi": (88, 80, 76), "sheep_leg": (60, 52, 50),
	"sheep_leg_bg": (40, 34, 34),
	# goat
	"goat_hi": (214, 176, 128), "goat_horn_dark": (104, 98, 88), "goat_light": (188, 142, 96), "goat_mid": (152, 104, 64), "goat_dark": (108, 70, 42),
	"goat_line": (72, 46, 30), "goat_horn": (156, 150, 136), "goat_beard": (226, 220, 200),
	# bull
	"bull_hi": (150, 104, 72), "bull_nose_dark": (110, 74, 68), "bull_light": (120, 78, 52), "bull_mid": (88, 54, 36), "bull_dark": (60, 36, 26),
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
# Scale: everything is drawn against the Coalition "Soldier Light" (Coalition.rte/Actors/Infantry/CoalitionLight), the
# game's "human": composited per its INI (helmet, head, torso, legs and feet at their joint offsets, standing on its stand
# path) it is SOLDIER_HEIGHT px tall, which we take as 1.8 m. See soldier_parts() and the preview sheet.
SOLDIER_HEIGHT = 41
PX_PER_M = SOLDIER_HEIGHT / 1.8  # ~23 px per metre


###############################################################################
# Bodies (facing right).
#
# The chicken is small enough to be plain ASCII art. The bigger animals are sketched from hand-placed shapes (ellipses,
# polygons, strokes and small ASCII stamps) onto a material map, which is then shaded: every material has a ramp
# (highlight, light, mid, dark) and an outline colour. The silhouette edge gets the outline colour; inside, the ramp step
# comes from where the pixel sits in its shading group (top lit, belly dark, the front a touch lighter than the back),
# like the banded shading of the stock sprites. Eyes, nostrils, rings and so on are flat colours placed last.


class Sketch:
	def __init__(self, w, h):
		self.w, self.h = w, h
		self.m = [[None] * w for _ in range(h)]

	def get(self, x, y):
		return self.m[y][x] if 0 <= x < self.w and 0 <= y < self.h else None

	def put(self, x, y, mat, only=None):
		x, y = int(round(x)), int(round(y))
		if 0 <= x < self.w and 0 <= y < self.h and (only is None or self.m[y][x] in only):
			self.m[y][x] = mat

	def ellipse(self, cx, cy, rx, ry, mat, only=None):
		for y in range(self.h):
			for x in range(self.w):
				if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
					self.put(x, y, mat, only)

	def poly(self, pts, mat, only=None):
		for y in range(self.h):
			for x in range(self.w):
				if point_in_poly(x + 0.5, y + 0.5, pts):
					self.put(x, y, mat, only)

	def stroke(self, pts, mat, width=1, only=None):
		"""A polyline of the given width through pixel centres."""
		lo, hi = -(width // 2), (width - 1) // 2
		for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
			n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
			for i in range(n + 1):
				x, y = x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n
				for dy in range(lo, hi + 1):
					for dx in range(lo, hi + 1):
						if dx * dx + dy * dy <= (width / 2.0) ** 2:
							self.put(x + dx, y + dy, mat, only)

	def stamp(self, rows, x0, y0, legend):
		for y, row in enumerate(rows):
			for x, ch in enumerate(row):
				if ch != ".":
					self.put(x0 + x, y0 + y, legend[ch])


def point_in_poly(x, y, pts):
	inside = False
	j = len(pts) - 1
	for i in range(len(pts)):
		xi, yi = pts[i]
		xj, yj = pts[j]
		if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
			inside = not inside
		j = i
	return inside


def mat(ramp, line, group="body", bands=(0.16, 0.45, 0.74), texture=None):
	"""A shaded material: ramp is 1-4 colour names from highlight to dark."""
	return dict(ramp=ramp, line=line, group=group, bands=bands, texture=texture)


def flat(colour):
	return dict(flat=colour)


def render(sk, mats):
	im = new_image(sk.w, sk.h)
	filled = lambda x, y: sk.get(x, y) is not None
	group = lambda x, y: mats[sk.get(x, y)].get("group") if filled(x, y) else None
	for y in range(sk.h):
		for x in range(sk.w):
			m = sk.get(x, y)
			if m is None:
				continue
			spec = mats[m]
			if "flat" in spec:
				im.putpixel((x, y), COLOURS[spec["flat"]])
				continue
			if not all(filled(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
				im.putpixel((x, y), COLOURS[spec["line"]])
				continue
			g = spec["group"]
			top, bottom, left, right = y, y, x, x
			while group(x, top - 1) == g:
				top -= 1
			while group(x, bottom + 1) == g:
				bottom += 1
			while group(left - 1, y) == g:
				left -= 1
			while group(right + 1, y) == g:
				right += 1
			t = (y - top) / max(1.0, bottom - top)
			u = (x - left) / max(1.0, right - left)
			v = 0.82 * t + 0.18 * (1.0 - u)
			step = sum(1 for b in spec["bands"] if v >= b)
			ramp = spec["ramp"]
			if len(ramp) < 4:
				step = max(0, step - (4 - len(ramp)))
			if spec["texture"]:
				step += spec["texture"](x, y)
			step = max(0, min(len(ramp) - 1, step))
			im.putpixel((x, y), COLOURS[ramp[step]])
	return im


def wool_texture(x, y):
	"""Little curls: a darker pixel on a staggered grid."""
	return 1 if (x + 3 * (y // 2)) % 4 == 0 and y % 2 == 0 else 0


###############################################################################
# Chicken (ASCII).

CHICKEN_ART = [
	"........RR...",
	"o......RRRR..",
	"Wo....oWWWkYY",
	"WWo..oWWWWWY.",
	"WWWooWWWWWrr.",
	"wWWWWwwWWWr..",
	".wwWwwwwWWo..",
	"..wwwwwwwo...",
	"...oooooo....",
]
CHICKEN_LEGEND = {"W": "chick_white", "w": "chick_shade", "o": "chick_line", "R": "comb", "r": "comb_dark", "Y": "beak", "k": "eye"}


def draw_chicken():
	return from_ascii(CHICKEN_ART, CHICKEN_LEGEND)


###############################################################################
# Pig: pink barrel body, big head with a flat snout, floppy ear, curly tail.

PIG_MATS = {
	"body": mat(("pig_hi", "pig_light", "pig_mid", "pig_dark"), "pig_line", bands=(0.1, 0.38, 0.85)),
	"head": mat(("pig_hi", "pig_light", "pig_mid", "pig_dark"), "pig_line", group="head", bands=(0.1, 0.38, 0.85)),
	"ear": mat(("pig_mid", "pig_dark"), "pig_line", group="ear"),
	"snout": mat(("pig_light", "snout"), "pig_line", group="snout"),
	"tail": flat("pig_line"),
	"nostril": flat("nostril"),
	"eye": flat("eye"),
	"cheek": flat("pig_dark"),
}


def draw_pig():
	sk = Sketch(36, 16)
	sk.ellipse(16.5, 9.0, 14.5, 6.6, "body")
	sk.ellipse(10, 7.5, 8, 5.5, "body")  # rump
	sk.ellipse(28.5, 8.5, 5.2, 5.0, "head")
	sk.poly([(31, 5.5), (35.4, 6), (35.4, 11.5), (31, 12)], "snout")
	sk.poly([(25, 4), (27, 1), (30.6, 2.6), (29, 5.4)], "ear")  # floppy ear tipping forward
	sk.stroke([(2.5, 8), (1, 6.5), (1, 5), (2.5, 4.5), (3, 6)], "tail")  # curly tail
	sk.put(30, 6, "eye")
	sk.put(34, 8, "nostril")
	sk.put(34, 10, "nostril")
	sk.put(29, 11, "cheek")
	sk.put(30, 11, "cheek")
	return render(sk, PIG_MATS)


###############################################################################
# Sheep: a bumpy cloud of wool, black face and ears, pale eye.

SHEEP_MATS = {
	"wool": mat(("wool_hi", "wool", "wool_shade", "wool_dark"), "wool_line", texture=wool_texture),
	"face": mat(("sheep_face_hi", "sheep_face", "sheep_face", "sheep_leg_bg"), "sheep_leg_bg", group="face"),
	"ear": flat("sheep_face"),
	"eye": flat("eye_hi"),
	"pupil": flat("eye"),
	"nose": flat("sheep_leg_bg"),
}


def draw_sheep():
	sk = Sketch(33, 17)
	sk.ellipse(14.5, 9.5, 13.8, 6.8, "wool")
	for bx, by in ((3, 5.5), (6.5, 3.4), (10.5, 2.6), (14.5, 2.4), (18.5, 2.6), (22.5, 3.6), (25, 6), (1.8, 9.5), (2.5, 13)):
		sk.ellipse(bx, by, 2.6, 2.3, "wool")
	for bx in (5, 9, 13, 17, 21, 24.5):  # lumpy belly fringe
		sk.ellipse(bx, 15.3, 2.1, 1.6, "wool")
	sk.poly([(24.5, 5.5), (28.5, 4.5), (32.5, 9), (32.5, 11.5), (30, 12.5), (26, 10)], "face")
	sk.ellipse(26.5, 4.5, 2.5, 2.2, "wool")  # woolly top-knot
	sk.stroke([(26, 6.5), (23.5, 8)], "ear", 1)
	sk.put(29, 7, "eye")
	sk.put(30, 7, "pupil")
	sk.put(32, 10, "nose")
	return render(sk, SHEEP_MATS)


###############################################################################
# Goat: lean brown body, neck up, swept-back horns, white beard, flicked-up tail.

GOAT_MATS = {
	"body": mat(("goat_hi", "goat_light", "goat_mid", "goat_dark"), "goat_line"),
	"head": mat(("goat_hi", "goat_light", "goat_mid", "goat_dark"), "goat_line", group="head"),
	"horn": mat(("goat_horn", "goat_horn_dark"), "goat_horn_dark", group="horn"),
	"beard": flat("goat_beard"),
	"eye": flat("eye"),
	"nostril": flat("goat_line"),
	"ear": flat("goat_dark"),
	"tail": flat("goat_dark"),
}


def draw_goat():
	sk = Sketch(30, 19)
	sk.ellipse(12.5, 12.0, 11.0, 4.9, "body")
	sk.poly([(18, 9), (22, 4.5), (25.5, 6), (24.5, 10.5), (21, 14)], "body")  # neck
	sk.stroke([(1.5, 9), (1, 7.5)], "tail", 1)
	sk.ellipse(24.5, 5.8, 3.2, 3.0, "head")
	sk.poly([(25, 4.5), (29.5, 7.5), (29.5, 9.5), (26.5, 9.5), (23.5, 7.5)], "head")  # muzzle
	sk.stroke([(23, 3.4), (21.5, 1.2), (19.5, 0.4)], "horn", 1)
	sk.stroke([(24.5, 3.0), (23.5, 0.8), (22.0, 0.0)], "horn", 1)
	sk.stroke([(22.5, 5.5), (20.5, 6.5)], "ear", 1)
	sk.stroke([(26.5, 9.5), (26, 13)], "beard", 1)
	sk.stroke([(27.5, 9.5), (27, 12)], "beard", 1)
	sk.put(25, 5, "eye")
	sk.put(29, 8, "nostril")
	return render(sk, GOAT_MATS)


###############################################################################
# Cow: boxy black-and-white Friesian with a pink udder, muzzle and horn stubs.

COW_MATS = {
	"white": mat(("cow_hi", "cow_white", "cow_shade", "cow_shade_dark"), "cow_line"),
	"black": mat(("cow_black_hi", "cow_black_hi", "cow_black", "cow_black"), "cow_black"),
	"hwhite": mat(("cow_hi", "cow_white", "cow_shade", "cow_shade_dark"), "cow_line", group="head"),
	"hblack": mat(("cow_black_hi", "cow_black_hi", "cow_black", "cow_black"), "cow_black", group="head"),
	"muzzle": mat(("udder_hi", "udder", "udder", "udder_dark"), "udder_dark", group="muzzle"),
	"udder": mat(("udder_hi", "udder", "udder", "udder_dark"), "udder_dark", group="udder"),
	"horn": mat(("horn", "horn_dark"), "horn_dark", group="horn"),
	"tail": flat("cow_line"),
	"tuft": flat("tail_hair"),
	"eye": flat("eye"),
	"nostril": flat("nostril"),
	"ear": flat("cow_black"),
}


def draw_cow():
	sk = Sketch(56, 26)
	body = [(3, 8), (6, 5.5), (12, 4.6), (38, 5), (42, 6.5), (45, 9), (45, 17), (42, 20.5), (36, 21.5), (10, 21.5), (4.5, 19), (3, 15)]
	sk.poly(body, "white")
	sk.ellipse(7.5, 5.6, 3, 1.6, "white")  # hip bones
	sk.poly([(39, 6), (45, 3.5), (48, 5), (49.5, 12), (46, 18), (41, 16)], "white")  # neck and dewlap
	sk.ellipse(13, 11.5, 7, 5.5, "black", only={"white"})
	sk.ellipse(28.5, 8.5, 6, 4.2, "black", only={"white"})
	sk.ellipse(37, 16.5, 4.2, 3.4, "black", only={"white"})
	sk.ellipse(22, 19, 3, 2, "black", only={"white"})
	sk.ellipse(43, 8.5, 3, 3, "black", only={"white"})
	sk.poly([(45.5, 3), (50.5, 2.5), (54.5, 10.5), (55.5, 14.5), (52.5, 16), (49, 14), (46, 7)], "hwhite")
	sk.ellipse(48.5, 6, 2.6, 3.2, "hblack", only={"hwhite"})
	sk.poly([(50.5, 12), (54, 10.5), (55.8, 13), (55.8, 15.5), (52.5, 16.4), (50, 15)], "muzzle")
	sk.stroke([(47.5, 2), (47, 0.5)], "horn", 1)
	sk.stroke([(50, 2), (50.5, 0.5)], "horn", 1)
	sk.stroke([(46, 4.5), (43.5, 5.5)], "ear", 2)
	sk.ellipse(14.5, 22, 4, 2.6, "udder")
	for tx in (12, 14, 16):
		sk.put(tx, 24.5, "udder")
		sk.put(tx, 25, "udder")
	sk.stroke([(3, 8.5), (2, 12), (1.5, 18)], "tail", 1)
	sk.ellipse(1.5, 20, 1.2, 2, "tuft")
	sk.put(50, 7, "eye")
	sk.put(54, 13, "nostril")
	return render(sk, COW_MATS)


###############################################################################
# Bull: bigger and darker, shoulder hump, deep chest, horns forward, brass nose ring.

BULL_MATS = {
	"body": mat(("bull_hi", "bull_light", "bull_mid", "bull_dark"), "bull_line"),
	"head": mat(("bull_light", "bull_mid", "bull_dark", "bull_dark"), "bull_line", group="head"),
	"poll": flat("bull_light"),
	"muscle": flat("bull_dark"),
	"muzzle": mat(("bull_nose", "bull_nose_dark"), "bull_line", group="muzzle"),
	"horn": mat(("horn", "horn", "horn_dark", "horn_dark"), "horn_dark", group="horn"),
	"horntip": flat("horn_tip"),
	"hornbg": flat("horn_dark"),
	"ring": flat("ring"),
	"tail": flat("bull_line"),
	"tuft": flat("tail_hair"),
	"eye": flat("eye"),
	"eye_red": flat("comb_dark"),
	"nostril": flat("bull_line"),
}


def draw_bull():
	sk = Sketch(61, 28)
	body = [(3, 10), (7, 7.5), (14, 7), (30, 7), (36, 4), (42, 2.5), (46, 5), (49, 10), (50, 19), (46, 24), (38, 24),
	        (11, 23.5), (5, 21), (3, 16)]
	sk.poly(body, "body")
	sk.ellipse(8.5, 8, 3.5, 1.8, "body")  # hip bones
	sk.stroke([(13, 10), (11, 14), (11, 19)], "muscle", 1, only={"body"})  # haunch
	sk.stroke([(39, 9), (42, 14), (41, 20)], "muscle", 1, only={"body"})  # shoulder
	sk.stroke([(48.5, 8), (47, 4.5), (46, 3)], "hornbg", 2)  # far horn, behind the head
	sk.poly([(46, 7), (52.5, 5.5), (57.5, 12), (59.5, 18), (55, 21), (50, 18.5), (47, 13)], "head")
	sk.poly([(53.5, 15.5), (58, 14), (60.5, 18), (59.5, 21), (55, 21.5), (53, 19)], "muzzle")
	for x, y in ((50, 6), (51, 6), (52, 6), (51, 7)):
		sk.put(x, y, "poll")  # curly forelock
	sk.stroke([(51, 6.5), (54, 5), (55.5, 3.5)], "horn", 2)
	sk.stroke([(55.5, 3.5), (56, 1.5)], "horn", 1)
	sk.put(56, 1, "horntip")
	sk.stroke([(3, 10.5), (2, 14), (1.5, 21)], "tail", 1)
	sk.ellipse(1.5, 23, 1.2, 2, "tuft")
	sk.put(53, 10, "eye")
	sk.put(52, 10, "eye_red")
	sk.put(59, 17, "nostril")
	for x, y in ((57, 21), (57, 22), (58, 23), (59, 23), (60, 22), (60, 21)):
		sk.put(x, y, "ring")
	return render(sk, BULL_MATS)


###############################################################################
# Animal rigs. hips are the leg joints on the body sprite (rear = L legs, front = R legs).
# leg: E/C = extended/contracted length (px, joint to foot), thigh/shank = widths, hoof = rows of hoof at the bottom,
# blend = rows at the top drawn without outline so the thigh melts into the body.

CHICKEN = dict(
	name="Chicken", prefix="Chicken", draw=draw_chicken,
	bob_box=(6, 0, 12, 4),
	hips={"rear": (3, 7), "front": (7, 7)},
	bg_nudge=0,  # BG legs hidden exactly behind the FG ones: reads as a two-legged bird
	leg=dict(E=4.6, C=2.4, thick=1, colour="chick_leg", colour_bg="chick_leg_bg", hoof=None, toes=True),
)

PIG = dict(
	name="Pig", prefix="Pig", draw=draw_pig,
	bob_box=(23, 0, 35, 14),
	hips={"rear": (8, 12), "front": (25, 12)},
	leg=dict(E=10.5, C=5.5, thigh=5, shank=3, hoof=1, blend=3, ramp=("pig_light", "pig_mid", "pig_mid"), line="pig_line",
	         ramp_bg=("pig_mid", "pig_dark", "pig_dark"), hoof_colour="pig_hoof"),
)

SHEEP = dict(
	name="Sheep", prefix="Sheep", draw=draw_sheep,
	bob_box=(23, 1, 32, 13),
	hips={"rear": (6, 14), "front": (22, 14)},
	leg=dict(E=11, C=5.5, thigh=3, shank=2, hoof=1, blend=2, ramp=("sheep_face_hi", "sheep_leg", "sheep_leg"), line="sheep_leg_bg",
	         ramp_bg=("sheep_leg", "sheep_leg_bg", "sheep_leg_bg"), hoof_colour="sheep_leg_bg"),
)

GOAT = dict(
	name="Goat", prefix="Goat", draw=draw_goat,
	bob_box=(19, 0, 29, 12),
	hips={"rear": (5, 14), "front": (19, 14)},
	leg=dict(E=12.5, C=6, thigh=3, shank=2, hoof=1, blend=2, ramp=("goat_light", "goat_mid", "goat_dark"), line="goat_line",
	         ramp_bg=("goat_mid", "goat_dark", "goat_dark"), hoof_colour="goat_line"),
)

COW = dict(
	name="Cow", prefix="Cow", draw=draw_cow,
	bob_box=(45, 0, 55, 17),
	hips={"rear": (10, 18), "front": (39, 18)},
	leg=dict(E=20.5, C=10, thigh=6, shank=4, hoof=2, blend=4, ramp=("cow_white", "cow_shade", "cow_shade_dark"), line="cow_line",
	         ramp_bg=("cow_shade", "cow_shade_dark", "cow_line"), hoof_colour="hoof"),
)

BULL = dict(
	name="Bull", prefix="Bull", draw=draw_bull,
	bob_box=(46, 0, 60, 24),
	hips={"rear": (11, 20), "front": (42, 20)},
	leg=dict(E=22, C=11, thigh=7, shank=4, hoof=2, blend=4, ramp=("bull_light", "bull_mid", "bull_dark"), line="bull_line",
	         ramp_bg=("bull_mid", "bull_dark", "bull_line"), hoof_colour="bull_line"),
)

ANIMALS = [CHICKEN, PIG, SHEEP, GOAT, COW, BULL]


###############################################################################
# Legs


def leg_lengths(E, C):
	return [C + (E - C) * (k + 0.5) / 4.0 for k in range(4)]


def leg_widest(spec):
	return spec.get("thigh", spec.get("thick", 1))


def leg_canvas(spec):
	E = spec["E"]
	maxd = math.ceil(E / 2.0 * 0.75) + leg_widest(spec)
	w = 2 * maxd + 3
	if w % 2 == 0:
		w += 1
	h = int(math.ceil(E)) + 3
	return w, h, w // 2, 1  # width, height, joint x, joint y


def leg_knee(spec, k, front):
	"""Knee position (relative to the joint) and foot depth for extension frame k. Front knees bend forward, hind hocks back."""
	E, C = spec["E"], spec["C"]
	L = round(leg_lengths(E, C)[k])
	a = E / 2.0
	d = 0 if k == 3 else 0.75 * math.sqrt(max(0.0, a * a - (L / 2.0) ** 2))
	return (1 if front else -1) * d, L / 2.0, L


def draw_leg_frame(spec, k, front, background):
	w, h, jx, jy = leg_canvas(spec)
	im = new_image(w, h)
	kdx, kdy, L = leg_knee(spec, k, front)
	knee_x, knee_y, foot_y = jx + kdx, jy + kdy, jy + L

	def x_at(y):
		if y <= knee_y:
			f = (y - jy) / max(0.001, knee_y - jy)
			return jx + (knee_x - jx) * f
		f = (y - knee_y) / max(0.001, foot_y - knee_y)
		return knee_x + (jx - knee_x) * f

	if spec.get("thick") == 1:  # bird legs: one pixel wide, with a forward toe
		main = COLOURS[spec["colour_bg"] if background else spec["colour"]]
		for y in range(jy, foot_y + 1):
			im.putpixel((int(round(x_at(y))), y), main)
		if spec.get("toes"):
			im.putpixel((jx + 1, foot_y), main)
			im.putpixel((jx - 1, foot_y), main)
		return im

	ramp = [COLOURS[c] for c in (spec["ramp_bg"] if background else spec["ramp"])]
	line = COLOURS[spec["line"]]
	hoof = COLOURS[spec["hoof_colour"]]
	thigh, shank = spec["thigh"], spec["shank"]
	rows = {}
	for y in range(jy, foot_y + 1):
		# Width: thigh at the top, tapering to the shank by the knee, a knobbly joint, then the shank down to the hoof.
		if y <= knee_y:
			f = (y - jy) / max(0.001, knee_y - jy)
			width = thigh + (shank + 0.6 - thigh) * f
		else:
			width = shank + (0.6 if y - knee_y < 1.5 else 0)
		width = max(1, int(round(width)))
		cx = x_at(y)
		x0 = int(math.floor(cx - (width - 1) / 2.0 + 0.5))
		rows[y] = (x0, x0 + width - 1)
	for y, (x0, x1) in rows.items():
		is_hoof = y > foot_y - spec["hoof"]
		for x in range(x0, x1 + 1):
			if is_hoof:
				c = line if y == foot_y and spec["hoof"] > 1 else hoof
			elif y - jy < spec["blend"]:
				c = ramp[1] if x < x1 else ramp[0]
			elif x == x0:
				c = line  # back edge outline
			elif x == x1 and x1 - x0 >= 3:
				c = ramp[0]  # lit front edge
			else:
				u = (x - x0) / max(1, x1 - x0)
				c = ramp[2] if u < 0.4 else ramp[1]
			im.putpixel((x, y), c)
	return im


def leg_files(animal, front, background):
	part = ("Front" if front else "Hind") + ("BG" if background else "FG")
	return "%sLeg%sA" % (animal["prefix"], part)


###############################################################################
# Body helpers


def body_frames(animal):
	f0 = animal["draw"]()
	f1 = bob(f0, animal["bob_box"])
	return [f0, f1]


def body_size(animal):
	return animal["draw"]().size


def body_origin(animal):
	"""The body's origin (its position and centre of rotation): midway between the hind and front hips, and low, a little
	above them, so the pivot sits over the legs and walking doesn't tip the animal over."""
	hind, front = animal["hips"]["rear"], animal["hips"]["front"]
	return (hind[0] + front[0]) // 2, hind[1] - max(1, int(round(animal["leg"]["E"] * 0.15)))


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
	# Masses are real-world-ish (kg) at the Soldier Light scale (whose total is ~100 with its gear), trimmed where needed.
	"Chicken": dict(mass=3, leg_mass=0.2, health=30, gold=5, temperament="Skittish", speed=2.6, gib_impulse=400, gib_wounds=2,
	                joint=40, char_height=26, perceptiveness=0.6, flavour="Gib Panel White Micro A",
	                description="A nervous little bird. Lays eggs, flaps about and runs from anything louder than a cluck."),
	"Pig": dict(mass=90, leg_mass=5, health=100, gold=20, temperament="Skittish", speed=2.6, gib_impulse=6000, gib_wounds=8,
	            joint=500, char_height=46, perceptiveness=0.4, flavour=None,
	            description="A plump pink pig. Squeals and bolts at the first sign of trouble."),
	"Sheep": dict(mass=70, leg_mass=4, health=90, gold=20, temperament="Skittish", speed=2.6, gib_impulse=5000, gib_wounds=8,
	              joint=400, char_height=50, perceptiveness=0.4, flavour="Gib Panel White Tiny A",
	              description="A woolly sheep. Follows the flock and flees from danger."),
	"Goat": dict(mass=55, leg_mass=4, health=90, gold=25, temperament="Defensive", speed=2.8, gib_impulse=4500, gib_wounds=8,
	             joint=400, char_height=55, perceptiveness=0.5, flavour=None,
	             description="A stubborn goat. Minds its own business, but will butt back if you start something."),
	"Cow": dict(mass=400, leg_mass=20, health=220, gold=40, temperament="Skittish", speed=2.6, gib_impulse=24000, gib_wounds=16,
	            joint=2500, char_height=80, perceptiveness=0.3, flavour=None,
	            description="A big black-and-white dairy cow. Gentle, heavy, and quick to stampede away from gunfire."),
	"Bull": dict(mass=600, leg_mass=28, health=300, gold=50, temperament="Defensive", speed=2.8, gib_impulse=34000, gib_wounds=20,
	             joint=3500, char_height=86, perceptiveness=0.5, flavour=None,
	             description="A dark, horned bull with a brass nose ring. Leave it alone and it leaves you alone."),
}


def leg_ini(animal, front, bg, stats):
	spec = animal["leg"]
	E, C = spec["E"], spec["C"]
	t = leg_widest(spec)
	w, h, jx, jy = leg_canvas(spec)
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
	splay = max(1.0, round(E * 0.2, 1))  # wide stance: hind feet a little back, front feet a little forward
	half_stride = round(E * 0.4, 1)
	lift = max(1.0, round(E * 0.3, 1))
	total_mass = stats["mass"] + 4 * stats["leg_mass"]
	stand_push = int(round(total_mass * 30, -1))
	walk_push = int(round(total_mass * 30, -1))
	dislodge_push = int(round(total_mass * 110, -1))
	w, h = body_size(animal)
	ox, oy = body_origin(animal)
	small = spec.get("thick") == 1
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
	lw, lh, jx, jy = leg_canvas(spec)
	hind, front = animal["hips"]["rear"], animal["hips"]["front"]
	parts = []
	for (hx, hy), is_front, bg, nudge in [(hind, False, True, animal.get("bg_nudge", 1)), (front, True, True, -animal.get("bg_nudge", 1))]:
		parts.append((draw_leg_frame(spec, leg_frame, is_front, bg), hx + nudge - jx, hy - jy))
	parts.append((body_frames(animal)[frame], 0, 0))
	for (hx, hy), is_front in [(hind, False), (front, True)]:
		parts.append((draw_leg_frame(spec, leg_frame, is_front, False), hx - jx, hy - jy))
	ground = animal["hips"]["rear"][1] + round(leg_lengths(spec["E"], spec["C"])[leg_frame])
	return parts, ground


SOLDIER_DIR = os.path.join(DATA_DIR, "Coalition.rte", "Actors", "Infantry", "CoalitionLight")


def paste_rotated(canvas_pixels, im, pivot, at, angle):
	"""Nearest-neighbour paste of paletted im rotated by angle (radians) about its pixel pivot, which lands on at.
	canvas_pixels is a dict (x, y) -> palette index (later pastes win)."""
	w, h = im.size
	ca, sa = math.cos(angle), math.sin(angle)
	r = int(math.hypot(w, h)) + 2
	for dy in range(-r, r + 1):
		for dx in range(-r, r + 1):
			sx = ca * dx + sa * dy + pivot[0]
			sy = -sa * dx + ca * dy + pivot[1]
			ix, iy = int(math.floor(sx + 0.5)), int(math.floor(sy + 0.5))
			if 0 <= ix < w and 0 <= iy < h:
				i = im.getpixel((ix, iy))
				if i != BACKGROUND:
					canvas_pixels[(int(round(at[0])) + dx, int(round(at[1])) + dy)] = i


def soldier_parts():
	"""The Coalition Soldier Light standing, composited from its sprites at the offsets in CoalitionLight.ini:
	torso origin at (0, 0); head at ParentOffset (0, -10) less its JointOffset (-2, 5); helmet at (-1, -3) on the head;
	legs (extended frame, 15 px) from the hips (1, 1) and (3.3, 1) towards the stand path feet (hip + (-2, 17) and
	hip + (8, 17)), feet hung from the ankles by their JointOffset; arms hanging towards their IdleOffset (4, 9).
	Returns {(x, y): palette index} with y = 0 on the ground."""
	img = lambda n: Image.open(os.path.join(SOLDIER_DIR, n))
	px = {}

	def put(im, sprite_offset, origin):
		w, h = im.size
		for yy in range(h):
			for xx in range(w):
				i = im.getpixel((xx, yy))
				if i != BACKGROUND:
					px[(int(round(origin[0] + sprite_offset[0])) + xx, int(round(origin[1] + sprite_offset[1])) + yy)] = i

	def limb(sprite, sprite_offset, joint_offset, joint_at, target, reach):
		d = (target[0] - joint_at[0], target[1] - joint_at[1])
		angle = math.atan2(d[1], d[0])
		pivot = (joint_offset[0] - sprite_offset[0], joint_offset[1] - sprite_offset[1])
		paste_rotated(px, sprite, pivot, joint_at, angle)
		n = math.hypot(*d)
		k = min(1.0, reach / n)
		return joint_at[0] + d[0] * k, joint_at[1] + d[1] * k

	def foot(name, ankle):
		put(img(name), (-6, -3), (ankle[0] + 2, ankle[1] + 1))  # Foot JointOffset (-2, -1)

	# Back to front: BG arm, BG leg + foot, torso, head, helmet, FG leg + foot, FG arm.
	limb(img("ArmBGA000.png"), (-6, -3), (-3.5, -1), (3, -7), (3 + 4, -7 + 9), 11)
	ankle = limb(img("LegBGA004.png"), (-6, -7), (-5, 2), (3.3, 1), (3.3 + 8, 18), 15)
	foot("FootBGA000.png", ankle)
	put(img("TorsoA.png"), (-7, -12), (0, 0))
	head = (0 + 2, -10 - 5)
	put(img("HeadA.png"), (-7, -6), head)
	put(img("HelmetA000.png"), (-7, -4), (head[0] - 1, head[1] - 3))
	ankle = limb(img("LegFGA004.png"), (-6, -7), (-5, 2), (1, 1), (1 - 2, 18), 15)
	foot("FootFGA000.png", ankle)
	limb(img("ArmFGA000.png"), (-6, -3), (-3.5, -1), (-2, -6), (-2 + 4, -6 + 9), 11)
	bottom = max(y for x, y in px)
	return {(x, y - bottom): i for (x, y), i in px.items()}


def soldier_height():
	px = soldier_parts()
	return -min(y for x, y in px) + 1


def write_preview(path, scale=5):
	"""Every animal at game scale beside the Soldier Light: standing (frame 0, leg frame 2) on the top row, mid-stride
	(frame 1, legs contracted) below, then the civilians with the soldier again."""
	pad = 6
	soldier = soldier_parts()
	sx0, sx1 = min(x for x, y in soldier), max(x for x, y in soldier)
	sh = soldier_height()
	placed = []  # rows of (kind, payload, width)
	for frame, leg_frame in ((0, 2), (1, 0)):
		row = [("soldier", None, sx1 - sx0 + 1)]
		for a in ANIMALS:
			parts, ground = compose_animal(a, frame, leg_frame)
			x0 = min(x for p, x, y in parts)
			x1 = max(x + p.width for p, x, y in parts)
			row.append(("animal", (parts, ground, x0), x1 - x0))
		placed.append(row)
	row_h = max(sh, 44) + 8
	width = max(sum(w + pad for k, p, w in row) for row in placed) + pad
	civ_h = 48
	canvas = Image.new("RGB", (width, row_h * 2 + civ_h + 12), (128, 128, 128))
	for r, row in enumerate(placed):
		gy = (r + 1) * row_h
		x = pad
		for kind, payload, w in row:
			if kind == "soldier":
				for (px_, py_), i in soldier.items():
					canvas.putpixel((x - sx0 + px_, gy + py_), rgb_of(i))
			else:
				parts, ground, x0 = payload
				top = gy - int(round(ground))
				for p, px_, py_ in parts:
					paste_p(canvas, p, x - x0 + px_, top + py_)
			for gx in range(x - 1, x + w + 1):
				canvas.putpixel((gx, gy + 1), (96, 76, 56))
			x += w + pad
		if r == 0:  # 1 m ruler at the right edge
			for yy in range(int(round(PX_PER_M))):
				canvas.putpixel((width - 3, gy - yy), (255, 255, 255) if yy % 2 == 0 else (40, 40, 40))
	# Civilians: torso/head/limbs at roughly their Green Dummy offsets (origin = torso origin), legs rotated to stand.
	civ = []
	for name in CIVILIANS:
		d = os.path.join(CIVILIAN_DIR, name)
		civ.append([Image.open(os.path.join(d, f)) for f in (name + "LegBGA004.png", name + "ArmBGA000.png", name + "TorsoA.png",
		                                                     name + "HeadA.png", name + "LegFGA004.png", name + "ArmFGA000.png")])
	cx, gy = pad + 8, row_h * 2 + civ_h
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
	for (px_, py_), i in soldier.items():
		canvas.putpixel((cx - sx0 + px_ - 4, gy + py_), rgb_of(i))
	canvas = canvas.resize((canvas.width * scale, canvas.height * scale), Image.NEAREST)
	os.makedirs(os.path.dirname(path), exist_ok=True)
	canvas.save(path)


def report_sizes():
	"""Standing size of each animal in px (legs included / nose to tail) and in metres at the soldier's scale."""
	print("Soldier Light: %d px tall (= 1.8 m, %.1f px/m)" % (soldier_height(), PX_PER_M))
	for a in ANIMALS:
		parts, ground = compose_animal(a, 0, 2)
		ys = [y + yy for p, x, y in parts for yy in range(p.height) for xx in range(p.width) if p.getpixel((xx, yy)) != BACKGROUND]
		xs = [x + xx for p, x, y in parts for yy in range(p.height) for xx in range(p.width) if p.getpixel((xx, yy)) != BACKGROUND]
		h = int(round(ground)) - min(ys) + 1
		l = max(xs) - min(xs) + 1
		print("%-8s %3d x %3d px  (%.2f m tall, %.2f m long)" % (a["name"], h, l, h / PX_PER_M, l / PX_PER_M))


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
	report_sizes()
	print("done")


if __name__ == "__main__":
	main()
