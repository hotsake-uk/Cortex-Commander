#!/usr/bin/env python3
"""Draws the Medieval arsenal's sprites and glows (Devices/Arsenal): the firepot, stinkpot, Holy Hand Grenade, throwing hammer, bolas,
caltrops and their shards, and the warhammer, mace, halberd and greatsword.

Sprites are ASCII grids saved as 8-bit PNGs in the game palette (index 0 the see-through magenta), like sprites.py; glows are true-colour
RGB images with a soft radial falloff on black, for ScreenEffect. Deterministic, so it can be re-run.

Run from the repository root: python3 Tools/Medieval/arsenal.py Data
"""
import math
import os
import sys

from PIL import Image

DATA = sys.argv[1] if len(sys.argv) > 1 else "Data"
OUT = os.path.join(DATA, "Medieval.rte/Devices/Arsenal")
PAL_SRC = Image.open(os.path.join(DATA, "Ronin.rte/Actors/Infantry/RoninLight/Torso000.png"))
PAL_FLAT = PAL_SRC.getpalette()[:768]
PAL = [tuple(PAL_FLAT[i * 3:i * 3 + 3]) for i in range(256)]

_cache = {}


def nearest(rgb):
	if rgb in _cache:
		return _cache[rgb]
	best, bestD = 1, 1e9
	for i in range(1, 255):
		p = PAL[i]
		d = 2 * (p[0] - rgb[0]) ** 2 + 4 * (p[1] - rgb[1]) ** 2 + 3 * (p[2] - rgb[2]) ** 2
		if d < bestD:
			best, bestD = i, d
	_cache[rgb] = best
	return best


def save(img, name):
	"""Saves an RGBA image as a palette PNG, magenta (index 0) where transparent."""
	w, h = img.size
	out = Image.new("P", (w, h), 0)
	out.putpalette(PAL_FLAT)
	src = img.load()
	dst = out.load()
	for y in range(h):
		for x in range(w):
			r, g, b, a = src[x, y]
			dst[x, y] = 0 if a < 128 else nearest((r, g, b))
	os.makedirs(OUT, exist_ok=True)
	out.save(os.path.join(OUT, name), transparency=0)


COLORS = {
	# Steel
	"k": (28, 30, 36), "d": (60, 64, 74), "s": (96, 102, 114), "m": (138, 144, 154), "l": (180, 186, 194), "H": (228, 232, 238),
	# Gold
	"y": (126, 92, 28), "g": (196, 156, 44), "G": (244, 214, 104), "W": (255, 246, 196),
	# Leather / wood
	"b": (56, 36, 22), "n": (96, 64, 38), "N": (140, 98, 58), "j": (180, 136, 84),
	# Clay (terracotta)
	"c": (92, 40, 22), "z": (150, 70, 40), "Z": (196, 104, 60), "x": (228, 150, 100),
	# Red paint, rag
	"r": (104, 20, 24), "R": (164, 32, 36), "o": (230, 120, 40), "O": (255, 200, 80),
	# Stink: green-stained clay and slime
	"f": (40, 60, 26), "F": (84, 110, 40), "e": (150, 190, 50), "E": (210, 236, 90),
	# Cloth rag, cork
	"w": (214, 206, 180), "q": (160, 120, 80),
	# Gem
	"v": (48, 72, 150), "V": (120, 150, 230),
}


def grid(rows):
	h = len(rows)
	w = max(len(r) for r in rows)
	img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
	px = img.load()
	for y, row in enumerate(rows):
		for x, ch in enumerate(row):
			if ch in ". ":
				continue
			px[x, y] = COLORS[ch] + (255,)
	return img


def check(name, rows, w, h):
	assert len(rows) == h, f"{name}: {len(rows)} rows, want {h}"
	for i, r in enumerate(rows):
		assert len(r) == w, f"{name} row {i}: '{r}' is {len(r)} wide, want {w}"


def glow(name, size, rgb, power=1.6):
	"""A round additive glow: full colour at the centre falling off to black at the edge."""
	img = Image.new("RGB", (size, size), (0, 0, 0))
	px = img.load()
	c = (size - 1) / 2.0
	for y in range(size):
		for x in range(size):
			d = math.hypot(x - c, y - c) / (c + 0.5)
			t = max(0.0, 1.0 - d) ** power
			px[x, y] = tuple(int(round(v * t)) for v in rgb)
	os.makedirs(OUT, exist_ok=True)
	img.save(os.path.join(OUT, name))


# ---------------------------------------------------------------------------
# Thrown

# A clay jar of greek fire, a red band round its belly and an oily rag stuffed in its neck for a wick.
FIREPOT = [
	"...O....",
	"..oOo...",
	"...ww...",
	"..cqqc..",
	"..czZc..",
	".czZxZc.",
	"czZxxZzc",
	"cRRRRRRc",
	"czZZZzzc",
	"czZZzzzc",
	".czzzzc.",
	"..cccc..",
]

# A squat jar, green-stained and stoppered with a cork, slime dribbling down its side.
STINKPOT = [
	"...qq...",
	"..cqqc..",
	"..cFec..",
	".cFZxFc.",
	"cFZxxZFc",
	"czZeZZzc",
	"czZEzFzc",
	"cFzeFzzc",
	".cFezFc.",
	"..cecc..",
	"....e...",
]

# A golden orb with a jewelled band and a cross on top.
HOLY = [
	"...y...",
	"..yGy..",
	"...g...",
	"..kgk..",
	".yGWGg.",
	"yGWGGgy",
	"vVvVvVv",
	"ygGggyy",
	".yggyy.",
	"..yyy..",
]

# A short hammer with a heavy square steel head.
THROWING_HAMMER = [
	"........kkkk",
	"........kmlk",
	"........kmlk",
	"jnNnNnNnkmHk",
	"bnbnbnbnkdmk",
	"........kdmk",
	"........kkkk",
]

# Three iron weights on leather cords knotted together.
BOLAS = [
	"....kkk....",
	"....kmk....",
	"....kkk....",
	".....n.....",
	".....n.....",
	".....N.....",
	"....n.n....",
	"...n...n...",
	".kkk...kkk.",
	".kmk...kmk.",
	".kkk...kkk.",
]

# A leather pouch tied at the neck, full of caltrops.
CALTROP_POUCH = [
	".b..b.",
	"..bb..",
	".bNNb.",
	"bNjNNb",
	"bNNmNb",
	".bbbb.",
]

# One caltrop lying on the ground: three spikes down, one always pointing up.
CALTROP = [
	"..l..",
	"..m..",
	".dmd.",
	"m...m",
]

CLAY_SHARDS = [
	["zZ", "cz"],
	["Zxz", ".cz"],
	["z.", "Zz", ".c"],
]

STINK_SHARDS = [
	["Fz", "ce"],
	["zFe", ".cF"],
]

# ---------------------------------------------------------------------------
# Melee, pointing right, hand at the haft or grip.

# Two-handed: a long ash haft, a square steel head with a spike going on past it.
WARHAMMER = [
	"....................kkkkk...",
	"....................kmllk...",
	"....................kmllk...",
	"....................kmlHkk..",
	"bnNnNnNnNnNnNnNnNnNnkmlHmllH",
	"jnbnbnbnbnbnbnbnbnbnkdmskk..",
	"....................kdmsk...",
	"....................kdmsk...",
	"....................kkkkk...",
]

# One-handed: a leather-wrapped grip and a flanged steel head.
MACE = [
	"............k.k...",
	"...........kmkmk..",
	"..y.......kmllllk.",
	"jnynNnNnNnkllHHlmk",
	"bnybnbnbnbkmmlmmdk",
	"...........kdkdk..",
	"............k.k...",
]

# Ash shaft as long as a spear, an axe blade on top, a spike at the end and a hook behind.
HALBERD = [
	"." * 30 + "kHHk" + "." * 10,
	"." * 29 + "kllHk" + "." * 10,
	"." * 29 + "kmlHk" + "." * 10,
	"." * 29 + "kdmlk" + "." * 10,
	"jnNn" * 7 + "jn" + "kdsk" + "mmllllHHH.",
	"bnbn" * 7 + "bn" + "kdsk" + "ssmmmmlll.",
	"." * 30 + "kdk" + "." * 11,
	"." * 31 + "kd" + "." * 11,
	"." * 31 + "k" + "." * 12,
]

# A zweihander: a long two-hand grip, a wide guard with parrying lugs past it, and a long blade.
GREATSWORD = [
	"." * 8 + "y" + "." * 4 + "k" + "." * 21,
	"." * 8 + "g" + "." * 4 + "k" + "." * 21,
	"Gjnjnjnj" + "g" + "ll" + "H" * 21 + "lk" + ".",
	"yjnjnjnj" + "g" + "sm" + "m" * 21 + "ml" + "k",
	"." * 8 + "g" + "." * 4 + "k" + "." * 21,
	"." * 8 + "y" + "." * 26,
]


def main():
	check("firepot", FIREPOT, 8, 12)
	check("stinkpot", STINKPOT, 8, 11)
	check("holy", HOLY, 7, 10)
	check("throwing hammer", THROWING_HAMMER, 12, 7)
	check("bolas", BOLAS, 11, 11)
	check("caltrop pouch", CALTROP_POUCH, 6, 6)
	check("caltrop", CALTROP, 5, 4)
	check("warhammer", WARHAMMER, 28, 9)
	check("mace", MACE, 18, 7)
	check("halberd", HALBERD, 44, 9)
	check("greatsword", GREATSWORD, 35, 6)

	save(grid(FIREPOT), "Firepot.png")
	save(grid(STINKPOT), "Stinkpot.png")
	save(grid(HOLY), "HolyHandGrenade.png")
	save(grid(THROWING_HAMMER), "ThrowingHammer.png")
	save(grid(BOLAS), "Bolas.png")
	save(grid(CALTROP_POUCH), "CaltropPouch.png")
	save(grid(CALTROP), "Caltrop.png")
	for i, rows in enumerate(CLAY_SHARDS):
		save(grid(rows), f"ClayShard{chr(65 + i)}.png")
	for i, rows in enumerate(STINK_SHARDS):
		save(grid(rows), f"StinkShard{chr(65 + i)}.png")
	save(grid(WARHAMMER), "Warhammer.png")
	save(grid(MACE), "Mace.png")
	save(grid(HALBERD), "Halberd.png")
	save(grid(GREATSWORD), "Greatsword.png")
	# A transparent pixel, for the invisible scripted effects (the bursts, the tangle, the clouds).
	save(Image.new("RGBA", (1, 1), (0, 0, 0, 0)), "Null.png")

	# Glows.
	glow("WickGlow.png", 15, (255, 150, 40))
	glow("FireGlowOrange.png", 41, (255, 130, 30), 1.3)
	glow("StinkGlow.png", 27, (150, 210, 30), 1.4)
	glow("StinkGlowTiny.png", 7, (180, 240, 60))
	glow("HolyGlowSmall.png", 21, (255, 220, 110))
	glow("HolyGlowBig.png", 61, (255, 230, 140), 1.3)
	glow("HolyGlowHuge.png", 151, (255, 236, 160), 1.2)
	glow("HolySpark.png", 7, (255, 240, 170))
	print("arsenal sprites done")


if __name__ == "__main__":
	main()
