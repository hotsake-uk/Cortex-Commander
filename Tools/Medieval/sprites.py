#!/usr/bin/env python3
"""Generates Medieval.rte's sprites in the game palette.

Hand-drawn parts (torsos, heads, weapons, shields) are ASCII grids below; limbs are Ronin's limbs recoloured by brightness
onto each unit's colours, so they keep their frames and joints.
"""
import math
import os
import sys

from PIL import Image

DATA = sys.argv[1]  # .../Data
OUT = os.path.join(DATA, "Medieval.rte")
RONIN = os.path.join(DATA, "Ronin.rte/Actors/Infantry/RoninLight")

PAL_SRC = Image.open(os.path.join(RONIN, "Torso000.png"))
PAL_FLAT = PAL_SRC.getpalette()[:768]
PAL = [tuple(PAL_FLAT[i * 3:i * 3 + 3]) for i in range(256)]

_cache = {}


def nearest(rgb):
	if rgb in _cache:
		return _cache[rgb]
	best, bestD = 1, 1e9
	for i in range(1, 255):
		p = PAL[i]
		# Weighted towards how the eye sees it.
		d = 2 * (p[0] - rgb[0]) ** 2 + 4 * (p[1] - rgb[1]) ** 2 + 3 * (p[2] - rgb[2]) ** 2
		if d < bestD:
			best, bestD = i, d
	_cache[rgb] = best
	return best


def save(img_rgb, path):
	"""Saves an RGBA image as a palette PNG, magenta (index 0) where transparent."""
	w, h = img_rgb.size
	out = Image.new("P", (w, h), 0)
	out.putpalette(PAL_FLAT)
	src = img_rgb.load()
	dst = out.load()
	for y in range(h):
		for x in range(w):
			r, g, b, a = src[x, y]
			dst[x, y] = 0 if a < 128 else nearest((r, g, b))
	full = os.path.join(OUT, path)
	os.makedirs(os.path.dirname(full), exist_ok=True)
	out.save(full, transparency=0)


COLORS = {
	# Steel
	"k": (28, 30, 36), "d": (60, 64, 74), "s": (96, 102, 114), "m": (138, 144, 154), "l": (180, 186, 194), "H": (228, 232, 238),
	# Red cloth
	"r": (104, 20, 24), "R": (164, 32, 36), "o": (212, 64, 56),
	# Blue cloth
	"v": (28, 40, 96), "V": (48, 72, 150), "i": (88, 120, 196),
	# Gold
	"y": (126, 92, 28), "g": (196, 156, 44), "G": (244, 214, 104),
	# Leather / wood
	"b": (56, 36, 22), "n": (96, 64, 38), "N": (140, 98, 58), "j": (180, 136, 84),
	# Green cloth
	"f": (32, 54, 28), "F": (58, 92, 44), "e": (94, 134, 62),
	# Undyed cloth
	"W": (160, 150, 124), "w": (214, 206, 180),
	# Mail
	"c": (64, 68, 74), "C": (124, 128, 134),
	# Skin
	"t": (146, 92, 68), "T": (204, 148, 112), "u": (232, 186, 150),
	# Hair, eyes
	"h": (84, 56, 36), "x": (14, 14, 16), "a": (200, 200, 196),
	# Purple
	"p": (70, 30, 96), "P": (120, 60, 156), "q": (168, 104, 200),
	# Flame: ember, orange, yellow
	"E": (130, 40, 16), "O": (232, 120, 32), "Y": (250, 204, 64),
	# Ice
	"K": (60, 104, 168), "I": (140, 190, 236), "J": (218, 238, 255),
	# White and grey cloth
	"D": (104, 106, 116), "B": (164, 166, 176), "A": (228, 228, 234),
	# Storm teal
	"S": (36, 104, 124), "U": (90, 176, 196),
	# Bone
	"X": (112, 100, 80), "z": (176, 166, 136), "Z": (228, 220, 192),
	# Fur
	"L": (104, 78, 52), "M": (164, 134, 96),
}


def grid(rows, colors=COLORS):
	h = len(rows)
	w = max(len(r) for r in rows)
	img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
	px = img.load()
	for y, row in enumerate(rows):
		for x, ch in enumerate(row):
			if ch in ". ":
				continue
			px[x, y] = colors[ch] + (255,)
	return img


def check(name, rows, w, h):
	assert len(rows) == h, f"{name}: {len(rows)} rows, want {h}"
	for i, r in enumerate(rows):
		assert len(r) == w, f"{name} row {i}: '{r}' is {len(r)} wide, want {w}"


def recolor(path, ramp, keepSkin=False, skinRamp=None):
	"""A Ronin limb, its brightness mapped onto ramp (dark to light)."""
	src = Image.open(path).convert("RGB")
	w, h = src.size
	img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
	sp = src.load()
	px = img.load()
	for y in range(h):
		for x in range(w):
			r, g, b = sp[x, y]
			if (r, g, b) == (255, 0, 255):
				continue
			lum = 0.3 * r + 0.59 * g + 0.11 * b
			isSkin = r > 150 and r > g + 25 and g > b
			use = ramp
			if isSkin and keepSkin:
				continue_rgb = (r, g, b)
				px[x, y] = continue_rgb + (255,)
				continue
			if isSkin and skinRamp:
				use = skinRamp
			t = min(max(lum / 190.0, 0.0), 1.0)
			idx = min(int(round(t * (len(use) - 1))), len(use) - 1)
			px[x, y] = use[idx] + (255,)
	return img


RAMPS = {
	"steel": [COLORS[c] for c in "kdsmlH"],
	"mail": [COLORS["k"], COLORS["c"], (92, 96, 102), COLORS["C"], (160, 164, 170)],
	"green": [(20, 34, 18), COLORS["f"], COLORS["F"], COLORS["e"], (130, 168, 90)],
	"leather": [(34, 22, 14), COLORS["b"], COLORS["n"], COLORS["N"], COLORS["j"]],
	"brown": [(30, 24, 18), (58, 44, 32), (88, 68, 48), (120, 96, 68), (156, 128, 92)],
	"red": [(60, 10, 14), COLORS["r"], COLORS["R"], COLORS["o"], (236, 110, 96)],
	"purple": [(40, 16, 56), COLORS["p"], COLORS["P"], COLORS["q"], (200, 150, 224)],
	"blue": [(16, 22, 56), COLORS["v"], COLORS["V"], COLORS["i"], (140, 170, 230)],
	"gambeson": [(52, 46, 34), (96, 88, 66), COLORS["W"], (190, 180, 152), COLORS["w"]],
	"black": [(12, 12, 16), (26, 26, 32), (40, 42, 50), (58, 60, 70), (84, 88, 98)],
	"grey": [(40, 42, 48), COLORS["D"], (136, 138, 148), COLORS["B"], (204, 206, 214)],
	"ice": [(30, 56, 104), COLORS["K"], (100, 150, 210), COLORS["I"], COLORS["J"]],
	"white": [(120, 120, 130), (170, 170, 180), (200, 200, 208), COLORS["A"], (246, 246, 250)],
	"orange": [(70, 20, 8), COLORS["E"], (190, 80, 24), COLORS["O"], COLORS["Y"]],
	"skin": [(90, 54, 38), COLORS["t"], (176, 120, 90), COLORS["T"], COLORS["u"]],
	"bone": [(60, 54, 42), COLORS["X"], COLORS["z"], (204, 196, 166), COLORS["Z"]],
}


def swap(rows, mapping):
	"""The same drawing in other colours: each character in mapping becomes its value."""
	return ["".join(mapping.get(ch, ch) for ch in row) for row in rows]

# ---------------------------------------------------------------------------
# Torsos, 13 x 17, front to the right (same frame as Ronin's, so its joint offsets fit).

TORSOS = {
	"Knight": [
		".....kkk.....",
		"....kdmdk....",
		"....kslsk....",
		"...kkdmdkk...",
		"..kdsmlHlsk..",
		".kdsmlHlRRok.",
		"kdsmlHlRRgRok",
		"kdsmmlmRRgRrk",
		"kdssmmsRggggk",
		"kddsmmsRRgRrk",
		".kdssdsRRgRrk",
		".kkdssdRRgRk.",
		"..kyggggGggk.",
		"....krRRRrk..",
		"....krRRorrk.",
		".....krRRrk..",
		"......kkkk...",
	],
	"Footman": [
		".....kkk.....",
		"....kcCck....",
		"....cCcCc....",
		"...kcCcCck...",
		"..kcCcCcCck..",
		".kcCcVVVVVVk.",
		"kcCcCViVVVvk.",
		"kCcCcVViVVvk.",
		"kcCcCVVViVvk.",
		"kCcCcVVVVVvk.",
		".kcCcvVVVVvk.",
		".kkcCvvVVvk..",
		"..kbnnnNnnbk.",
		"....kVVVVvk..",
		"....kVViVvk..",
		".....kvVvk...",
		"......kkkk...",
	],
	"Archer": [
		".....bbb.....",
		"....bnNnb....",
		"....bnNnb....",
		"...fFFeFFf...",
		"..fFFeeFFFf..",
		".fFFeeFFFFFf.",
		"fFFeFFFbbFFff",
		"fFeFFFFFnbFff",
		"fFFFFFFFFnbff",
		"fFFFFFFFFFbnf",
		".fFFFFFFFFFf.",
		".ffFFFFFFFff.",
		"..fbnNjNnnbf.",
		"....fFFFFff..",
		"....fFFeFff..",
		".....fFFff...",
		"......ffff...",
	],
	"Crossbowman": [
		".....bbb.....",
		"....bWwWb....",
		"....bwWwb....",
		"...bWwWwWb...",
		"..bWwWwwWWb..",
		".bWwWwWRRRRb.",
		"bWwWwWwRoRRrb",
		"bWWwWwWRRoRrb",
		"bWwWwWWRRRorb",
		"bWWwWwWRRRRrb",
		".bWwWwWRRRRb.",
		".bbWWwrRRRrb.",
		"..bnnNjNnnbb.",
		"....bWwWWbb..",
		"....bWwwWWb..",
		".....bWWWb...",
		"......bbbb...",
	],
	"King": [
		".....yyy.....",
		"....ygGgy....",
		"....kslsk....",
		"...pPPqPPp...",
		"..pPqqPPPPp..",
		".pPqqPPgwwwk.",
		"pPqPPPPgwRwwk",
		"pPqPPPPgwwwwk",
		"pPPPPPPgwRwwk",
		"pPPPPPPgwwwwk",
		".pPPPPPgwRwk.",
		".ppPPPPgwwwk.",
		"..pyggGgggyk.",
		"....pPPqPpk..",
		"....pPPqPPp..",
		".....pPqPp...",
		"......pppp...",
	],
	# A robed mage: blue robe with a gold trim down the front, a rope belt.
	"Mage": [
		".....vvv.....",
		"....vVgVv....",
		"....kslsk....",
		"...vVVgVVv...",
		"..vVViGVVVv..",
		".vVViVgVVVVv.",
		"vVViVVgVVVVVv",
		"vVViVVgVVVVVv",
		"vVVVVVgVVVVVv",
		"vVVnNjjNNnVVv",
		".vVVVVgVVVVv.",
		".vvVVVgVVVVv.",
		"..vVVVgVVVv..",
		"....vVgVVv...",
		"....vVVgVVv..",
		".....vVgVv...",
		"......vvvv...",
	],
	# A witch: a black robe with green trim and a green sash.
	"Witch": [
		".....kkk.....",
		"....kdedk....",
		"....kslsk....",
		"...kddeddk...",
		"..kddsedddk..",
		".kddsdedddsk.",
		"kddsddeddddsk",
		"kddsddeddddsk",
		"kdddddeddddsk",
		"kddfFFeeFFfdk",
		".kdddddeddsk.",
		".kkddddeddsk.",
		"..kdddeddsk..",
		"....kdedddk..",
		"....kddeddsk.",
		".....kdedk...",
		"......kkkk...",
	],
}

# ---------------------------------------------------------------------------
# Heads, 13 x 14, face to the right (Ronin's head frame).

HEADS = {
	"Knight": [
		".............",
		"..rRo........",
		".rRoRkkkkkk..",
		".rRkdsmllHHk.",
		"..kdsmmllHHk.",
		"..kdsmmlllHk.",
		"..kdsmmxxxxxk",
		"..kdssmmlmlHk",
		"..kdssmmlgGHk",
		"..kddssmmgmmk",
		"...kddsmsxsxk",
		"...kkddssmmk.",
		"....kkkkkkk..",
		".....kdsk....",
	],
	"Knight B": [
		".............",
		".............",
		"....kkkkkk...",
		"...kdsmllHk..",
		"..kdsmmllHHk.",
		"..kdsmmlllHk.",
		"..kdsmmxxxxxk",
		"..kdssmmlmlHk",
		"..kdssmmlgGHk",
		"..kddssmmgmmk",
		"...kddsmsxsxk",
		"...kkddssmmk.",
		"....kkkkkkk..",
		".....kdsk....",
	],
	"Footman": [
		".............",
		".............",
		".....kkkk....",
		"....kdsmlk...",
		"...kdsmmlHk..",
		".kkdsmmmlHHkk",
		"kdssssmmmmmsk",
		"..cCcCtTTTTk.",
		"..CcCcTTxTTu.",
		"..cCcCTTTTTTT",
		"..CcCctTTTtk.",
		"..cCcCtThTk..",
		"...CcCcCck...",
		"....kcCck....",
	],
	"Footman B": [
		".............",
		".............",
		".....kkkk....",
		"....kdsmlk...",
		"...kdsmmlHk..",
		".kkdsmmmlHHkk",
		"kdssssmmmmmsk",
		"..cCcChhhhTk.",
		"..CcChhTxTTu.",
		"..cCcChTTTTTT",
		"..CcCchhTTtk.",
		"..cCcChhhhk..",
		"...CcCchhk...",
		"....kcCck....",
	],
	"Archer": [
		".............",
		"......fFf....",
		".....fFeFf...",
		"....fFeFFFf..",
		"...fFFFFFFFf.",
		"..fFFfFhhhTf.",
		"..fFFfhTTxTu.",
		".fFFFfhTTTTu.",
		".fFFffhTTTTTT",
		"..fFfftTTTTk.",
		"..ffFftTThTk.",
		"...fFFtTTtk..",
		"...ffFFffk...",
		"....ffff.....",
	],
	"Archer B": [
		".............",
		"......fFf....",
		".....fFeFf...",
		"....fFeFFFf..",
		"...fFFFFFFFf.",
		"..fFFfFhhhTf.",
		"..fFFfhTTxTu.",
		".fFFFfhTTTTu.",
		".fFFffhTTTTTT",
		"..fFffhhTThk.",
		"..ffFfhhhhhk.",
		"...fFFhhhhk..",
		"...ffFFffk...",
		"....ffff.....",
	],
	"Crossbowman": [
		".............",
		".............",
		"....kkkkkk...",
		"...kdsmmlHk..",
		"..kdsmmmlHHk.",
		"..kkkkkkkkkkk",
		"..WwWhTTTTTk.",
		"..wWhTTTxTTu.",
		"..WwhTTTTTTTT",
		"..wWhtTTTTtk.",
		"..WwWhTTThk..",
		"...wWWhhhk...",
		"....WwWWb....",
		"....bWwb.....",
	],
	"King": [
		"....g..g..g..",
		"....gG.gG.g..",
		"....ygGgGgy..",
		"...yggRgGggy.",
		"...aaaaaaaa..",
		"..aaaaTTTTTt.",
		"..aaaTTTxTTu.",
		"..aaaTTTTTTTT",
		"..aaatTTTTTk.",
		"..aaaaaaTTk..",
		"...aaaaaaak..",
		"...aaaaaaa...",
		"....aaaaa....",
		".....tTt.....",
	],
	# A tall pointed hat with a gold band, and a long white beard.
	"Mage": [
		"........v....",
		".......vV....",
		"......vVV....",
		".....vVVi....",
		"....vVViV....",
		"...vVVViVv...",
		"..gGgGgGgGg..",
		"...aaTTTxTTu.",
		"...aaTTTTTTTT",
		"...aaaTTTTTk.",
		"...aaaaaaTk..",
		"....aaaaaaa..",
		".....aaaaa...",
		".....tTt.....",
	],
	# A black pointed hat with a green band over long dark hair.
	"Witch": [
		".......k.....",
		"......kd.....",
		".....kdd.....",
		".....kdds....",
		"....kddds....",
		"...kddddsk...",
		".kkeeeeeeekk.",
		"..hhhTTTxTTu.",
		"..hhhTTTTTTTT",
		"..hhhtTTTTTk.",
		"..hhhhTTTTk..",
		"..hhhh.tTk...",
		"..hhh........",
		".....tTt.....",
	],
}

# ---------------------------------------------------------------------------
# The expansion's units: new drawings where nothing fits, the rest the drawings above in other colours.

# A berserker: bare chest under a fur mantle, a leather kilt.
TORSOS["Berserker"] = [
	".....LLL.....",
	"....LMMML....",
	"....tTTTt....",
	"...LMMLMMLL..",
	"..LMMLLMMMML.",
	".tTTuTTTTTTt.",
	"tTTuTTTTtTTTt",
	"tTuTTTTtTTTTt",
	"tTTTTTTTTTTTt",
	"tTTTtTTTTtTTt",
	".tTTTTTTTTTt.",
	".ttTTTTTTTtt.",
	"..bnnNjNnnbb.",
	"....bnNnnb...",
	"....bnnNnnb..",
	".....bnNnb...",
	"......bbbb...",
]
# A skeleton: ribs and spine, with the gaps between them.
TORSOS["Skeleton"] = [
	".....XXX.....",
	"....XzZzX....",
	"....XzZzX....",
	"...XZZZZZX...",
	"..XZz.Z.zZX..",
	".XZZZZZZZZZX.",
	"XZz.zZZz.zZZX",
	"XZZZZZZZZZZZX",
	"X.zZz.Z.zZz.X",
	"XZZZZZZZZZZZX",
	".X.zZ.Z.Zz.X.",
	"..XZZZZZZZX..",
	"....XzZzX....",
	".....XZX.....",
	"....XZZZX....",
	"...XZz.zZX...",
	"....XXXXX....",
]
TORSOS["Pyromancer"] = swap(TORSOS["Mage"], {"v": "r", "V": "R", "i": "o", "g": "O", "G": "Y"})
TORSOS["Stormcaller"] = swap(TORSOS["Mage"], {"v": "D", "V": "B", "i": "A", "g": "S", "G": "U"})
TORSOS["Cryomancer"] = swap(TORSOS["Mage"], {"v": "K", "V": "I", "i": "J", "g": "A", "G": "J"})
TORSOS["Necromancer"] = swap(TORSOS["Witch"], {"e": "P", "f": "p", "F": "P"})
TORSOS["Druid"] = swap(TORSOS["Mage"], {"v": "f", "V": "F", "i": "e", "g": "N", "G": "j"})
TORSOS["Archmage"] = swap(TORSOS["Mage"], {"v": "B", "V": "A", "i": "J"})
TORSOS["Spellblade"] = swap(TORSOS["Knight"], {"r": "p", "R": "P", "o": "q"})
TORSOS["Paladin"] = swap(TORSOS["Knight"], {"r": "B", "R": "A", "o": "J"})
TORSOS["Halberdier"] = swap(TORSOS["Footman"], {"v": "r", "V": "R", "i": "Y"})
TORSOS["Bombardier"] = swap(TORSOS["Crossbowman"], {"r": "E", "R": "O", "o": "Y"})
TORSOS["Skirmisher"] = swap(TORSOS["Archer"], {"f": "b", "F": "n", "e": "N"})

# A skull, eyes hollow.
HEADS["Skeleton"] = [
	".............",
	".............",
	"....XXXXX....",
	"...XZZZZZX...",
	"..XZZZZZZZX..",
	"..XZZZZZZZZX.",
	"..XZZZxxZZxX.",
	"..XZZZxxZZZX.",
	"..XzZZZZZZZZX",
	"...XzZZZxZZX.",
	"...XzZZZZZX..",
	"....XZxZxZX..",
	"....XZZZZZ...",
	".....XXXX....",
]
# A deep black hood with purple lining, a bone-white face and green eyes.
HEADS["Necromancer"] = [
	"....kkkk.....",
	"...kdddpk....",
	"..kddpppdk...",
	".kddpdddddk..",
	".kdpddddddk..",
	".kddkZZZZZk..",
	".kdkZZeZZeZ..",
	".kdkZZZZZZZ..",
	".kddkzZZZZZ..",
	".kddkzZxZxZ..",
	".kddkkzZZZk..",
	"..kdddkkkk...",
	"...kddddk....",
	".....tTt.....",
]
# A crown of leaves over long brown hair and beard.
HEADS["Druid"] = [
	".............",
	"..e...e..e...",
	"..eF.eFe.eF..",
	"...FnNnNnFe..",
	"...hhhhhhhh..",
	"..hhhTTTTTt..",
	"..hhhTTTxTTu.",
	"..hhhTTTTTTTT",
	"..hhhhTTTTTk.",
	"..hhhhhhhhk..",
	"...hhhhhhhk..",
	"...hhhhhhh...",
	"....hhhhh....",
	".....tTt.....",
]
# A horned helm and a wild red beard.
HEADS["Berserker"] = [
	".Z.........Z.",
	".zZ.......Zz.",
	"..zZkkkkkZz..",
	"...kdsmmlHk..",
	"..kdsmmmlHHk.",
	"..kkkkkkkkkkk",
	"..EEETTTxTTu.",
	"..EEETTTTTTTT",
	"..EEEETTTTTk.",
	"..EEEEEEEETk.",
	"...EEEEEEEk..",
	"...EEEEEEE...",
	"....EEEEE....",
	".....tTt.....",
]
HEADS["Pyromancer"] = swap(HEADS["Mage"], {"v": "r", "V": "R", "i": "o", "g": "O", "G": "Y", "a": "h"})
HEADS["Stormcaller"] = swap(HEADS["Mage"], {"v": "D", "V": "B", "i": "A", "g": "S", "G": "U"})
HEADS["Cryomancer"] = swap(HEADS["Mage"], {"v": "K", "V": "I", "i": "J", "g": "A", "G": "J"})
HEADS["Archmage"] = swap(HEADS["Mage"], {"v": "B", "V": "A", "i": "J"})
HEADS["Spellblade"] = swap(HEADS["Knight"], {"r": "p", "R": "P", "o": "q"})
HEADS["Paladin"] = swap(HEADS["Knight"], {"r": "g", "R": "G", "o": "Y"})
HEADS["Bombardier"] = swap(HEADS["Crossbowman"], {"W": "b", "w": "n"})
HEADS["Skirmisher"] = swap(HEADS["Archer"], {"f": "b", "F": "n", "e": "N"})
HEADS["Skirmisher B"] = swap(HEADS["Archer B"], {"f": "b", "F": "n", "e": "N"})

# The Kingdom's support casters and champions (SE-1).
# A cleric: a white robe with a gold stripe, hooded in white.
TORSOS["Cleric"] = swap(TORSOS["Mage"], {"v": "B", "V": "A", "i": "g", "G": "G"})
HEADS["Cleric"] = swap(HEADS["Archer"], {"f": "B", "F": "A", "e": "A"})
# An enchanter: a purple robe and hat with a gold stripe.
TORSOS["Enchanter"] = swap(TORSOS["Mage"], {"v": "p", "V": "P", "i": "q"})
HEADS["Enchanter"] = swap(HEADS["Mage"], {"v": "p", "V": "P", "i": "q"})
# A battlemage: a knight's plate with a blue surcoat, blue-plumed helm.
TORSOS["Battlemage"] = swap(TORSOS["Knight"], {"r": "v", "R": "V", "o": "i"})
HEADS["Battlemage"] = swap(HEADS["Knight"], {"r": "v", "R": "V", "o": "i"})
# A champion: a knight's plate with a gold surcoat, gold-plumed helm.
TORSOS["Champion"] = swap(TORSOS["Knight"], {"r": "y", "R": "g", "o": "G"})
HEADS["Champion"] = swap(HEADS["Knight"], {"r": "y", "R": "g", "o": "G"})

# The woodcutter (a colony woodcutter's staff): a green wool tunic with its sleeves rolled up, a leather belt and apron, and a red knitted cap
# over brown hair and a beard.
TORSOS["Woodcutter"] = swap(TORSOS["Skirmisher"], {"b": "f", "n": "F", "N": "e"})
TORSOS["Woodcutter"][12:] = TORSOS["Skirmisher"][12:]  # (Belt and breeches leather, as the skirmisher's.)
HEADS["Woodcutter"] = [
	".............",
	".............",
	".....RRRR....",
	"....RoRRRR...",
	"...RRRRRRRR..",
	"...hhhhhhhh..",
	"..hhhTTTxTTu.",
	"..hhTTTTTTTTT",
	"..hhhTTTTTTk.",
	"..hhhhhhhTTk.",
	"...hhhhhhhk..",
	"...hhhhhhh...",
	"....hhhhh....",
	".....tTt.....",
]

# ---------------------------------------------------------------------------
# Weapons, pointing right, hand at the hilt.

# The hilt's grip is 3 wide, the guard at x 3, the blade from 4 to 22.
SWORD = [
	"...y....................",
	"...g....................",
	"jnNgllHHHHHHHHHHHHHHHlk.",
	"bnngsmmmmmmmmmmmmmmmmmlk",
	"...g....................",
	"...y....................",
]

DAGGER = [
	"..y.........",
	"jnglHHHHHlk.",
	"bngsmmmmmmlk",
	"..y.........",
]

AXE = [
	"...................kk...",
	"...................kHk..",
	"..................kdlHk.",
	"................kkdmlHk.",
	"jnNnNnNnNnNnNnNnNdsmlHk.",
	"bnbnbnbnbnbnbnbnbdsmlHk.",
	"................kkdmlHk.",
	"..................kdlHk.",
	"...................kHk..",
	"...................kk...",
]

# A woodcutter's felling axe: a long haft and a narrow, deep bit, one-handed.
WOODAXE = [
	"..................kk.",
	"................kkdk.",
	"jnNnNnNnNnNnNnNkdmlk.",
	"bnbnbnbnbnbnbnbkdmlHk",
	"................kkdk.",
	"..................kk.",
]

SPEAR = [
	".....................................kk...",
	"jnNnNnNnNnNnNnNnNnNnNnNnNnNnNnNnNnNdslHHk.",
	"bnbnbnbnbnbnbnbnbnbnbnbnbnbnbnbnbnbksmmmlk",
	".....................................kk...",
]

BOW = [
	"..n.....",
	".bNn....",
	".w.Nn...",
	".w..Nn..",
	".w...Nn.",
	".w....Nn",
	".w....Nn",
	".w.....N",
	".w.....N",
	".w.....N",
	".w.....j",
	".w.....j",
	".w.....j",
	".w.....j",
	".w.....N",
	".w.....N",
	".w.....N",
	".w....Nn",
	".w....Nn",
	".w...Nn.",
	".w..Nn..",
	".w.Nn...",
	".bNn....",
	"..n.....",
]

CROSSBOW = [
	"............k...",
	"............n...",
	"............n...",
	".........w..n...",
	"..........w.N...",
	"bbnnNNjNNNNnsNnk",
	"bnnNNnNnnnnnNsnb",
	"..b.bbn....w.N..",
	"..b..bn...w..n..",
	"......b......n..",
	".............n..",
	".............k..",
]

# The arrow lying nocked on the bow (the bow's magazine), and the bolt on the crossbow.
ARROW_NOCKED = [
	"wR..........",
	"RnnnnnnnnnmH",
	"wR..........",
]
BOLT_NOCKED = [
	"wnnnnnnmH",
]

KITE = [
	"kkkkkkkkk",
	"klmmmmmmk",
	"kmRRRRRdk",
	"kmRRgRRdk",
	"kmRRgRRdk",
	"kmggGggdk",
	"kmRRgRRdk",
	"kmRRgRRdk",
	"kmRRgRRdk",
	".kmRgRdk.",
	".kmRgRdk.",
	".kmRgRdk.",
	"..kmgdk..",
	"..kmRdk..",
	"...kdk...",
	"...kdk...",
	"....k....",
]

ROUND = [
	"...kkkkk...",
	"..knNnNnk..",
	".knNVVVnNk.",
	"knNVViVVNnk",
	"kNVVkkkVVNk",
	"knVikmkiVnk",
	"kNVVkkkVVNk",
	"knNVViVVNnk",
	".knNVVVnNk.",
	"..knNnNnk..",
	"...kkkkk...",
]

ICON = [
	".....................",
	"..kkkkkkkkkkkkkkkkk..",
	"..kmlllllllllllllmk..",
	"..klRRRRRRgRRRRRRdk..",
	"..klRRRRRRgRRRRRRdk..",
	"..klRRRRRRgRRRRRRdk..",
	"..klRRRRRRgRRRRRRdk..",
	"..klgggggGGGgggggdk..",
	"..klRRRRRRgRRRRRRdk..",
	"..klRRRRRRgRRRRRRdk..",
	"..klRRRRRRgRRRRRRdk..",
	"...klRRRRRgRRRRRdk...",
	"...klRRRRRgRRRRRdk...",
	"....klRRRRgRRRRdk....",
	"....klRRRRgRRRRdk....",
	".....klRRRgRRRdk.....",
	"......klRRgRRdk......",
	".......klRgRdk.......",
	"........klgdk........",
	".........kdk.........",
	"..........k..........",
	".....................",
]


def arrow_frames(length, head, fletch, colorsShaft, frames=16, size=15):
	"""A flying arrow drawn at each of frames angles, frame 0 pointing right, then counter-clockwise."""
	imgs = []
	c = (size - 1) / 2.0
	for f in range(frames):
		ang = 2 * math.pi * f / frames
		dx, dy = math.cos(ang), -math.sin(ang)
		img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
		px = img.load()
		steps = length * 3
		for i in range(steps + 1):
			t = i / steps  # 0 at the tail, 1 at the tip
			x = c + dx * (t - 0.5) * length
			y = c + dy * (t - 0.5) * length
			xi, yi = int(round(x)), int(round(y))
			if not (0 <= xi < size and 0 <= yi < size):
				continue
			along = t * length
			if along >= length - head:
				col = COLORS["l"] if along >= length - 1 else COLORS["m"]
			elif along <= fletch:
				col = COLORS["w"] if (int(along) % 2 == 0) else COLORS["R"]
			else:
				col = colorsShaft
			px[xi, yi] = col + (255,)
		imgs.append(img)
	return imgs


def main():
	for name, rows in TORSOS.items():
		check("torso " + name, rows, 13, 17)
	for name, rows in HEADS.items():
		check("head " + name, rows, 13, 14)

	units = {
		# name: torso, heads, arm ramp, leg ramp, foot ramp, hand ramp
		"Knight": ("Knight", ["Knight", "Knight B"], "steel", "steel", "steel", "steel"),
		"Footman": ("Footman", ["Footman", "Footman B"], "mail", "brown", "leather", "leather"),
		"Archer": ("Archer", ["Archer", "Archer B"], "green", "brown", "leather", None),
		"Crossbowman": ("Crossbowman", ["Crossbowman"], "gambeson", "red", "leather", None),
		"King": ("King", ["King"], "purple", "red", "leather", "steel"),
		"Mage": ("Mage", ["Mage"], "blue", "blue", "leather", None),
		"Witch": ("Witch", ["Witch"], "black", "black", "leather", None),
		"Pyromancer": ("Pyromancer", ["Pyromancer"], "red", "red", "leather", None),
		"Stormcaller": ("Stormcaller", ["Stormcaller"], "grey", "grey", "leather", None),
		"Cryomancer": ("Cryomancer", ["Cryomancer"], "ice", "ice", "leather", None),
		"Necromancer": ("Necromancer", ["Necromancer"], "black", "black", "black", "bone"),
		"Druid": ("Druid", ["Druid"], "green", "brown", "leather", None),
		"Archmage": ("Archmage", ["Archmage"], "white", "white", "leather", None),
		"Spellblade": ("Spellblade", ["Spellblade", "Knight B"], "steel", "steel", "steel", "steel"),
		"Paladin": ("Paladin", ["Paladin", "Knight B"], "steel", "steel", "steel", "steel"),
		"Halberdier": ("Halberdier", ["Footman", "Footman B"], "mail", "red", "leather", "leather"),
		"Berserker": ("Berserker", ["Berserker"], "skin", "brown", "leather", None),
		"Bombardier": ("Bombardier", ["Bombardier"], "gambeson", "brown", "leather", "leather"),
		"Skirmisher": ("Skirmisher", ["Skirmisher", "Skirmisher B"], "leather", "brown", "leather", None),
		"Skeleton": ("Skeleton", ["Skeleton"], "bone", "bone", "bone", "bone"),
		"Woodcutter": ("Woodcutter", ["Woodcutter"], "skin", "brown", "leather", None),
		"Cleric": ("Cleric", ["Cleric"], "white", "white", "leather", None),
		"Enchanter": ("Enchanter", ["Enchanter"], "purple", "purple", "leather", None),
		"Battlemage": ("Battlemage", ["Battlemage", "Knight B"], "steel", "steel", "steel", "steel"),
		"Champion": ("Champion", ["Champion", "Knight B"], "steel", "steel", "steel", "steel"),
	}
	for unit, (torso, heads, armRamp, legRamp, footRamp, handRamp) in units.items():
		base = f"Actors/{unit}/"
		save(grid(TORSOS[torso]), base + "Torso.png")
		for i, h in enumerate(heads):
			save(grid(HEADS[h]), base + f"Head{i:03d}.png")
		for part, frames in (("ArmFGA", 5), ("ArmBGA", 5), ("LegFGA", 5), ("LegBGA", 5), ("FootFGA", 4), ("FootBGA", 4)):
			ramp = RAMPS[armRamp if part.startswith("Arm") else legRamp if part.startswith("Leg") else footRamp]
			for f in range(frames):
				src = os.path.join(RONIN, f"{part}{f:03d}.png")
				save(recolor(src, ramp), base + f"{part}{f:03d}.png")
		for hand in ("HandFGA", "HandFGB"):
			src = os.path.join(RONIN, hand + ".png")
			if handRamp:
				save(recolor(src, RAMPS[handRamp]), base + hand + ".png")
			else:
				save(Image.open(src).convert("RGBA"), base + hand + ".png")

	save(grid(SWORD), "Devices/Longsword/Longsword.png")
	save(grid(DAGGER), "Devices/Dagger/Dagger.png")
	save(grid(AXE), "Devices/BattleAxe/BattleAxe.png")
	save(grid(WOODAXE), "Devices/WoodcutterAxe/WoodcutterAxe.png")
	save(grid(SPEAR), "Devices/Spear/Spear.png")
	save(grid(BOW), "Devices/Longbow/Longbow.png")
	save(grid(ARROW_NOCKED), "Devices/Longbow/ArrowNocked.png")
	save(grid(CROSSBOW), "Devices/Crossbow/Crossbow.png")
	save(grid(BOLT_NOCKED), "Devices/Crossbow/BoltNocked.png")
	save(grid(KITE), "Devices/KiteShield/KiteShield.png")
	save(grid(ROUND), "Devices/RoundShield/RoundShield.png")
	for i, img in enumerate(arrow_frames(11, 2, 2, COLORS["N"])):
		save(img, f"Devices/Longbow/Arrow{i:03d}.png")
	for i, img in enumerate(arrow_frames(7, 2, 1, COLORS["n"], size=11)):
		save(img, f"Devices/Crossbow/Bolt{i:03d}.png")
	save(grid(ICON), "ModuleIcon.png")
	# A transparent pixel, for the leap's invisible emitter.
	save(Image.new("RGBA", (1, 1), (0, 0, 0, 0)), "Actors/Shared/Null.png")
	print("sprites done")


main()
