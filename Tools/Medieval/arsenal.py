#!/usr/bin/env python3
"""Draws the Medieval arsenal's sprites and glows (Devices/Arsenal): the firepot, stinkpot, Holy Hand Grenade, throwing hammer, bolas,
caltrops and their shards, and the warhammer, mace, halberd and greatsword; and the Kingdom's second armoury (Arsenal2.ini): the warbow,
hunting bow and arbalest, the flamberge, poleaxe, flail, falchion, morning star, hunting knife, war scythe and quarterstaff, the tower
shield and buckler, the throwing spear, francisca, sling and its stones, the net, the snare trap, and the alchemist's four flasks.

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


# ---------------------------------------------------------------------------
# The second armoury (Arsenal2.ini).

# Bows, drawn strung, front to the right as the longbow is (sprites.py's BOW is 8 x 24).
# A warbow: taller and thicker than the longbow, horn nocks.
WARBOW = [
	"..H.....",
	"..nn....",
	".bNNn...",
	".w.NNn..",
	".w..NNn.",
	".w...NNn",
	".w...NNn",
	".w....NN",
	".w....NN",
	".w....NN",
	".w....NN",
	".w.....N",
	".w.....j",
	".w.....j",
	".w.....j",
	".w.....j",
	".w.....N",
	".w....NN",
	".w....NN",
	".w....NN",
	".w....NN",
	".w...NNn",
	".w...NNn",
	".w..NNn.",
	".w.NNn..",
	".bNNn...",
	"..nn....",
	"..H.....",
]

# A hunting bow: short, light, recurved tips.
HUNTING_BOW = [
	"....n...",
	"...Nn...",
	"..nN....",
	".w.Nn...",
	".w..Nn..",
	".w...Nn.",
	".w....Nn",
	".w....Nn",
	".w....Nj",
	".w....Nj",
	".w....Nn",
	".w....Nn",
	".w...Nn.",
	".w..Nn..",
	".w.Nn...",
	"..nN....",
	"...Nn...",
	"....n...",
]

# An arbalest: a heavy steel-bowed crossbow with a windlass at the butt.
ARBALEST = [
	"..............k....",
	"..............k....",
	"..............s....",
	"..........w...s....",
	"...........w..m....",
	"kdk.........w.l....",
	"kmkbnnNNjNNNNnsNnkk",
	"kdkbnnNNnNnnnnNsnmk",
	"..bb.bbn....w.l.kk.",
	"..b..bn....w..m....",
	"......b.......s....",
	"..............s....",
	"..............k....",
	"..............k....",
]

# Melee, pointing right, hand at the grip.
# A flamberge: a greatsword with a wave-edged blade.
FLAMBERGE = [
	"." * 8 + "y" + "." * 4 + "k" + "." * 20 + ".",
	"." * 8 + "g" + "." * 4 + "k" + ".HH..HH..HH..HH..HH." + ".",
	"Gjnjnjnj" + "g" + "ll" + "HHllHHllHHllHHllHHllk" + ".",
	"yjnjnjnj" + "g" + "sm" + "mmssmmssmmssmmssmmssmk",
	"." * 8 + "g" + "." * 4 + "k" + ".ss..ss..ss..ss..ss." + ".",
	"." * 8 + "y" + "." * 25 + ".",
]

# A poleaxe: a long haft, an axe blade one side, a hammer the other, a spike on top.
POLEAXE = [
	"." * 28 + "kHk" + "." * 7,
	"." * 28 + "klk" + "." * 7,
	"." * 23 + "kkkk.kdmk..kk..",
	"." * 23 + "kmlk.kdmk.kHk..",
	"jnNn" * 5 + "jnN" + "kmlkkkdmkkdlHk.",
	"bnbn" * 5 + "bnb" + "kdmkkkdmkksmlHk",
	"." * 23 + "kdmk.kdmk.kmk..",
	"." * 23 + "kkkk.kdmk..kk..",
	"." * 28 + "kdk" + "." * 7,
]

# A flail: a short haft, a chain, and a spiked iron ball.
FLAIL = [
	"................k.k...",
	"...............kkkkk..",
	"..........k.k.kkmlkkk.",
	"jnNnNnNnNnkkk.kklmlkk.",
	"bnbnbnbnbnk.kkkkmmdkk.",
	"...............kkkkk..",
	"................k.k...",
]

# A falchion: a broad, cleaver-like single-edged blade, one-handed.
FALCHION = [
	"...y................",
	"...g.........lHHHHk.",
	"jnNgllHHHHHHHHHHHHHk",
	"bnngsmmmmmmmmmmmmllk",
	"...g...........kkk..",
	"...y................",
]

# A morning star: a one-handed haft and a spiked ball.
MORNING_STAR = [
	"............k.k.k..",
	"...........kkkkkk..",
	"..y.......kkmlllkk.",
	"jnynNnNnNnkkllHHlkk",
	"bnybnbnbnbkkmmmmdkk",
	"...........kkkkkk..",
	"............k.k.k..",
]

# A hunting knife: a broad, short blade with a bone grip.
HUNTING_KNIFE = [
	"...k..........",
	"ZzzklHHHHHHlk.",
	"zZzksmmmmmmmlk",
	"...k..........",
]

# A war scythe: a long pole and a scythe blade set upright on it.
WAR_SCYTHE = [
	"." * 30 + "kk" + "." * 10,
	"." * 30 + "kHk" + "." * 9,
	"." * 30 + "kHlk" + "." * 8,
	"." * 30 + "kHlmk" + "." * 7,
	"jnNn" * 7 + "jn" + "kHlmsk" + "." * 6,
	"bnbn" * 7 + "bn" + "kHlmmsk" + "." * 5,
	"." * 30 + "kHllmmsk" + "." * 4,
	"." * 30 + "kkkkkkkkk" + "." * 3,
]

# A quarterstaff: a plain oak staff shod with iron.
QUARTERSTAFF = [
	"kk" + "jnNn" * 7 + "jn" + "kk",
	"kk" + "bnbn" * 7 + "bn" + "kk",
]

# Shields, seen face on, as the kite shield is (sprites.py's KITE is 9 x 17).
# A tower shield: tall, square-topped, iron-banded, the king's colours.
TOWER = [
	"kkkkkkkkkkk",
	"klmmmmmmmdk",
	"kmRRRRRRRdk",
	"kmRRRgRRRdk",
	"kmRRRgRRRdk",
	"kmmmmmmmmdk",
	"kmRRRgRRRdk",
	"kmRRggGRRdk",
	"kmRRRgRRRdk",
	"kmRRRgRRRdk",
	"kmmmmmmmmdk",
	"kmRRRgRRRdk",
	"kmRRRgRRRdk",
	"kmRRRRRRRdk",
	"kmRRRRRRRdk",
	".kmmmmmmdk.",
	".kmRRRRRdk.",
	"..kmRRRdk..",
	"...kmRdk...",
	"....kkk....",
]

# A buckler: a small round steel shield with a boss.
BUCKLER = [
	"..kkkkk..",
	".klmmmmk.",
	"klmmlmmdk",
	"kmmlHlmdk",
	"kmmlllmdk",
	"kmmmmmmdk",
	".kmmmmdk.",
	"..kkkkk..",
]

# Thrown.
# A throwing spear: heavier than the javelin, a broad leaf head.
THROWING_SPEAR = [
	"." * 24 + "kk" + "." * 4,
	"." * 22 + "kkllHk" + "." * 2,
	"jnNnNnNnNnNnNnNnNnNnNnNdsmmllHk",
	"bnbnbnbnbnbnbnbnbnbnbnbksmmmmlk",
	"." * 22 + "kksmk" + "." * 3,
	"." * 24 + "kk" + "." * 4,
]
THROWING_SPEAR = [r[:30] for r in THROWING_SPEAR]

# A francisca: a throwing axe with a long, curved, flared blade.
FRANCISCA = [
	".............kk..",
	"............kHlk.",
	"...........kdlHk.",
	"..........kkdmlHk",
	"jnNnNnNnNnkdsmlHk",
	"bnbnbnbnbnkdsmlk.",
	"..........kkdmk..",
	"...........kkk...",
]

# A sling: a leather pouch on two cords, held bunched.
SLING = [
	".....n......",
	"....n.n.....",
	"...n...n....",
	"..n.....n...",
	".n.......n..",
	"bNNjjNNb..n.",
	".bNNNNb...n.",
	"..bbbb....n.",
]

# A sling stone, three of them.
STONES = [
	["sms", "mlm", "sms"],
	[".ms", "mlm", "sm."],
	["sm", "ms"],
]

# A net, bunched to throw.
NET = [
	"...j.j.j...",
	"..jNjNjNj..",
	".jNjNjNjNj.",
	"jNjNjNjNjNj",
	".jNjNjNjNj.",
	"..jNjNjNj..",
	"...j.j.j...",
]

# A snare trap: an iron hoop with a sprung jaw, set.
SNARE_TRAP = [
	"k.........k",
	"kk.......kk",
	".kkkdddkkk.",
	"..kdsmmsdk.",
	"..kkmHmkk..",
	"...kkkkk...",
]

# Flasks: glass, with a stopper; what's in them shows.
def flask(liquid):
	a, b, c = liquid
	return [
		"...qq...",
		"..kqqk..",
		"..k" + a + a + "k..",
		".k" + a + b + b + a + "k.",
		"k" + a + b + c + c + b + a + "k",
		"k" + a + b + c + c + b + a + "k",
		"k" + a + b + b + b + b + a + "k",
		".k" + a + a + a + a + "k.",
		"..kkkk..",
	]

def pad(rows):
	"""Rows padded out to the longest with nothing, for the grids built from pieces."""
	w = max(len(r) for r in rows)
	return [r + "." * (w - len(r)) for r in rows]


FLAMBERGE = pad(FLAMBERGE)
POLEAXE = pad(POLEAXE)
WAR_SCYTHE = pad(WAR_SCYTHE)
THROWING_SPEAR = pad(THROWING_SPEAR)

OIL_FLASK = flask("kds")
FROST_FLASK = flask("vVw")
SMOKE_FLASK = flask("smH")
HEALING_DRAUGHT = flask("rRO")


def main():
	check("warbow", WARBOW, 8, 28)
	check("hunting bow", HUNTING_BOW, 8, 18)
	check("arbalest", ARBALEST, 19, 14)
	check("flamberge", FLAMBERGE, len(FLAMBERGE[0]), 6)
	check("poleaxe", POLEAXE, len(POLEAXE[0]), 9)
	check("flail", FLAIL, 22, 7)
	check("falchion", FALCHION, 20, 6)
	check("morning star", MORNING_STAR, 19, 7)
	check("hunting knife", HUNTING_KNIFE, 14, 4)
	check("war scythe", WAR_SCYTHE, len(WAR_SCYTHE[0]), 8)
	check("quarterstaff", QUARTERSTAFF, 34, 2)
	check("tower", TOWER, 11, 20)
	check("buckler", BUCKLER, 9, 8)
	check("throwing spear", THROWING_SPEAR, len(THROWING_SPEAR[0]), 6)
	check("francisca", FRANCISCA, 17, 8)
	check("sling", SLING, 12, 8)
	check("net", NET, 11, 7)
	check("snare trap", SNARE_TRAP, 11, 6)
	for name, rows in (("warbow", WARBOW), ("huntingbow", HUNTING_BOW), ("arbalest", ARBALEST), ("flamberge", FLAMBERGE), ("poleaxe", POLEAXE),
			("flail", FLAIL), ("falchion", FALCHION), ("morningstar", MORNING_STAR), ("huntingknife", HUNTING_KNIFE), ("warscythe", WAR_SCYTHE),
			("quarterstaff", QUARTERSTAFF), ("towershield", TOWER), ("buckler", BUCKLER), ("throwingspear", THROWING_SPEAR), ("francisca", FRANCISCA),
			("sling", SLING), ("net", NET), ("snaretrap", SNARE_TRAP), ("oilflask", OIL_FLASK), ("frostflask", FROST_FLASK), ("smokeflask", SMOKE_FLASK),
			("healingdraught", HEALING_DRAUGHT)):
		file = {"warbow": "Warbow", "huntingbow": "HuntingBow", "arbalest": "Arbalest", "flamberge": "Flamberge", "poleaxe": "Poleaxe", "flail": "Flail",
			"falchion": "Falchion", "morningstar": "MorningStar", "huntingknife": "HuntingKnife", "warscythe": "WarScythe", "quarterstaff": "Quarterstaff",
			"towershield": "TowerShield", "buckler": "Buckler", "throwingspear": "ThrowingSpear", "francisca": "Francisca", "sling": "Sling", "net": "Net",
			"snaretrap": "SnareTrap", "oilflask": "OilFlask", "frostflask": "FrostFlask", "smokeflask": "SmokeFlask", "healingdraught": "HealingDraught"}[name]
		save(grid(rows), file + ".png")
	for i, rows in enumerate(STONES):
		save(grid(rows), f"Stone{i:03d}.png")
	glow("FrostGlow.png", 41, (120, 200, 255), 1.4)
	glow("DraughtGlow.png", 31, (255, 120, 140), 1.4)

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


main()
