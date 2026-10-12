#!/usr/bin/env python3
"""Draws the Medieval spells' pictures (Data/Medieval.rte/Devices/Spells).

- Each spell's book, tome, scroll or tablet: what it looks like lying on the ground (a spell isn't drawn while held, DrawnWhenHeld = 0).
  Every one has its own cover colour and emblem, so a dropped spell can be told from across the field.
- The projectiles: the fireball, the meteor, the poison and gravity orbs (four frames each, they flicker), and the rune circles the
  meteor storm and the raising of the dead glow on the ground.
  All of these are 8-bit in the game palette (index 0 the see-through mask colour), lit from the upper left.
- Coloured glows: true-colour RGB images, black for nothing, added onto the screen where an object with ScreenEffect is (and they light the
  scene in their average colour).

Run from the repository root: python3 Tools/Medieval/spells.py Data
"""
import math
import os
import random
import sys

from PIL import Image

DATA = sys.argv[1] if len(sys.argv) > 1 else "Data"
OUT = os.path.join(DATA, "Medieval.rte/Devices/Spells")
PAL_SRC = Image.open(os.path.join(DATA, "Ronin.rte/Actors/Infantry/RoninLight/Torso000.png"))
PAL_FLAT = PAL_SRC.getpalette()[:768]
PAL = [tuple(PAL_FLAT[i * 3:i * 3 + 3]) for i in range(256)]

_cache = {}


def nearest(rgb):
	rgb = tuple(int(max(0, min(255, c))) for c in rgb)
	if rgb not in _cache:
		_cache[rgb] = min(range(1, 255), key=lambda i: 2 * (PAL[i][0] - rgb[0]) ** 2 + 4 * (PAL[i][1] - rgb[1]) ** 2 + 3 * (PAL[i][2] - rgb[2]) ** 2)
	return _cache[rgb]


def save_indexed(pixels, w, h, name):
	"""pixels: dict (x, y) -> rgb. Saves a palette PNG, index 0 elsewhere."""
	out = Image.new("P", (w, h), 0)
	out.putpalette(PAL_FLAT)
	dst = out.load()
	for (x, y), rgb in pixels.items():
		if 0 <= x < w and 0 <= y < h:
			dst[x, y] = nearest(rgb)
	out.save(os.path.join(OUT, name), transparency=0)


def save_grid(rows, colors, name):
	w = len(rows[0])
	assert all(len(r) == w for r in rows), name
	px = {}
	for y, row in enumerate(rows):
		for x, ch in enumerate(row):
			if ch != ".":
				px[(x, y)] = colors[ch]
	save_indexed(px, w, len(rows), name)


def save_rgb(img, name):
	img.save(os.path.join(OUT, name))


# ---------------------------------------------------------------------------------------------------------------------------------
# Books, tomes, scrolls and the tablet

OUTLINE = (22, 20, 26)
GOLD = [(126, 92, 28), (196, 156, 44), (244, 214, 104)]
SILVER = [(96, 102, 114), (160, 166, 176), (228, 232, 238)]
BONE = [(150, 140, 110), (206, 198, 168), (240, 236, 214)]
IRON = [(50, 52, 58), (90, 94, 102), (140, 144, 152)]
PAGES = [(170, 160, 132), (214, 206, 180)]

# Emblems, 5 wide and 5 high, drawn in the middle of the cover. Letters: a, b, c the emblem's three shades (dark to light), k outline.
EMBLEMS = {
	"flame": ["..b..", ".bc..", ".bcb.", "bcacb", ".bab."],
	"meteor": ["...bb", "..bb.", ".ac..", "acca.", ".aa.."],
	"bolt": ["...c.", "..c..", ".ccc.", "..c..", ".c..."],
	"star": ["..b..", ".bcb.", "bcccb", ".bcb.", "..b.."],
	"crystal": ["c.b.c", ".bcb.", "bcccb", ".bcb.", "c.b.c"],
	"skull": [".ccc.", "ccccc", "cacac", ".ccc.", ".b.b."],
	"spiral": [".bbb.", "b...b", "b.c.b", "b..bb", ".bb.."],
	"hourglass": ["ccccc", ".bab.", "..b..", ".bcb.", "ccccc"],
	"fang": ["c...c", "cc.cc", ".bcb.", "..b..", "....."],
	"eye": [".bbb.", "bcacb", "bcacb", ".bbb.", "....."],
}


def book(name, cover, trim, emblem, emblem_colors, w=12, h=9, clasp=True):
	"""A closed book seen from the front: outline, cover in three shades (shadowed lower right), trimmed corners, pages showing on the
	right and a clasp, and the emblem in the middle."""
	d, m, l = cover
	px = {}
	for y in range(h):
		for x in range(w - 1):
			edge = x == 0 or y == 0 or y == h - 1 or x == w - 2
			if edge:
				if (x, y) in ((0, 0), (0, h - 1)):
					continue
				px[(x, y)] = OUTLINE
			else:
				shade = m
				if x == 1 or y == 1:
					shade = l if (x + y) % 3 else m
				if x == w - 3 or y == h - 2:
					shade = d
				px[(x, y)] = shade
	# The spine of pages on the right.
	for y in range(1, h - 1):
		px[(w - 1, y)] = PAGES[(y % 2)]
	px[(w - 1, h - 1)] = PAGES[0]
	# Trimmed corners.
	for (x, y) in ((1, 1), (w - 3, 1), (1, h - 2), (w - 3, h - 2)):
		px[(x, y)] = trim[2] if y == 1 and x == 1 else trim[1]
	if clasp:
		cy = h // 2
		px[(w - 3, cy)] = trim[1]
		px[(w - 2, cy)] = trim[2]
		px[(w - 2, cy + 1)] = trim[0]
	# Emblem.
	grid = EMBLEMS[emblem]
	ox = (w - 1 - 5) // 2
	oy = (h - 5) // 2
	shades = {"a": emblem_colors[0], "b": emblem_colors[1], "c": emblem_colors[2]}
	for ey, row in enumerate(grid):
		for ex, ch in enumerate(row):
			if ch in shades:
				px[(ox + ex, oy + ey)] = shades[ch]
	save_indexed(px, w, h, name)


def scroll(name, paper, ends, seal, ribbon=None, w=15, h=8):
	"""A rolled scroll lying on its side: rolled ends, parchment between them, a wax seal (and a ribbon) in the middle."""
	pd, pm, pl = paper
	ed, el = ends
	px = {}
	for y in range(h):
		for x in range(w):
			end = x < 3 or x >= w - 3
			if end:
				if y in (0, h - 1):
					if x in (1, 2, w - 3, w - 2):
						px[(x, y)] = OUTLINE
					continue
				if x in (0, w - 1):
					px[(x, y)] = OUTLINE
				else:
					px[(x, y)] = el if (x in (1, w - 2) and y < h // 2) else ed
			else:
				if y in (1, h - 2):
					px[(x, y)] = OUTLINE
				elif 1 < y < h - 2:
					px[(x, y)] = pl if y == 2 else (pm if y < h - 3 else pd)
	cx = w // 2
	if ribbon:
		for y in range(2, h - 2):
			px[(cx - 1, y)] = ribbon[0]
		px[(cx - 2, h - 2)] = ribbon[1]
		px[(cx - 1, h - 1)] = ribbon[0]
	sd, sm, sl = seal
	for (x, y), c in {(cx, 3): sl, (cx + 1, 3): sm, (cx, 4): sm, (cx + 1, 4): sd, (cx + 2, 4): sd, (cx, 5): sd}.items():
		px[(x, y)] = c
	save_indexed(px, w, h, name)


def tablet(name):
	"""Earth Shatter: a cracked stone tablet with a brown rune of the earth cut into it."""
	rows = [
		"..kkkkkk..",
		".kGGGGGgk.",
		"kGgggggggk",
		"kGgnNNngsk",
		"kGgNgsNgsk",
		"kGgnNNngsk",
		"kGggNggxsk",
		"kGgnNnxgsk",
		"kGgggxgggk",
		"kGgnNNngsk",
		"kgsssssssk",
		".kkkkkkkk.",
	]
	colors = {"k": OUTLINE, "G": (170, 166, 154), "g": (128, 124, 114), "s": (88, 84, 78), "n": (84, 56, 32), "N": (140, 98, 58), "x": (40, 38, 36)}
	save_grid(rows, colors, name)


def make_items():
	# Fireball: a red leather book with a flame.
	book("Fireball.png", [(90, 20, 18), (150, 36, 28), (200, 70, 50)], GOLD, "flame", [(200, 70, 20), (244, 150, 40), (255, 236, 140)])
	# Dragon's Breath: a scroll with crimson-dyed ends and a fanged orange seal.
	scroll("DragonsBreath.png", [(150, 124, 82), (200, 176, 124), (234, 218, 170)], [(110, 24, 22), (170, 50, 40)], [(150, 60, 10), (230, 120, 30), (255, 200, 80)], ribbon=[(120, 24, 22), (80, 16, 14)])
	# Meteor Storm: a big black tome with a falling red star, iron bound.
	book("MeteorStorm.png", [(20, 18, 22), (44, 38, 44), (78, 66, 72)], IRON, "meteor", [(160, 30, 20), (230, 80, 30), (255, 200, 90)], w=14, h=10)
	# Call Lightning: a slate-blue book with a yellow lightning bolt, silver corners.
	book("CallLightning.png", [(34, 44, 66), (64, 82, 116), (110, 130, 166)], SILVER, "bolt", [(160, 140, 40), (230, 210, 70), (255, 250, 160)])
	# Arcane Missiles: a small violet book with a white star, gold.
	book("ArcaneMissiles.png", [(60, 24, 90), (110, 50, 150), (160, 100, 200)], GOLD, "star", [(170, 120, 220), (220, 190, 250), (255, 255, 255)], w=11, h=8)
	# Frost Shards: a pale ice-blue book with a white crystal, silver.
	book("FrostShards.png", [(70, 110, 150), (120, 170, 210), (190, 225, 245)], SILVER, "crystal", [(90, 150, 210), (200, 235, 255), (255, 255, 255)])
	# Poison Cloud: a sickly green book with a skull.
	book("PoisonCloud.png", [(30, 60, 24), (60, 110, 40), (110, 160, 60)], BONE, "skull", [(20, 30, 14), (170, 230, 90), (200, 250, 140)])
	# Gravity Well: a deep indigo book with a purple spiral, iron.
	book("GravityWell.png", [(18, 14, 40), (40, 30, 80), (70, 56, 120)], IRON, "spiral", [(90, 40, 160), (170, 90, 230), (230, 180, 255)])
	# Earth Shatter: a stone tablet.
	tablet("EarthShatter.png")
	# Raise Dead: a bone-cornered black grimoire with a green-eyed skull, big.
	book("RaiseDead.png", [(28, 30, 26), (52, 56, 48), (84, 90, 76)], BONE, "skull", [(60, 220, 90), (200, 196, 170), (236, 232, 210)], w=14, h=10)
	# Tornado: a scroll with sea-green ends, a teal seal and ribbon.
	scroll("Tornado.png", [(150, 140, 100), (196, 190, 150), (230, 226, 196)], [(30, 90, 90), (60, 150, 140)], [(20, 80, 90), (50, 160, 160), (150, 230, 220)], ribbon=[(40, 120, 120), (20, 80, 80)])
	# Time Warp: a gold-covered book with an hourglass.
	book("TimeWarp.png", [(110, 76, 22), (180, 136, 40), (236, 200, 96)], [(70, 46, 20), (110, 76, 34), (150, 110, 50)], "hourglass", [(250, 240, 200), (120, 80, 30), (70, 46, 20)])
	# Arcane Nova: a white scroll with violet ends and a bright violet seal.
	scroll("ArcaneNova.png", [(180, 176, 190), (220, 216, 230), (250, 248, 255)], [(70, 30, 110), (130, 70, 180)], [(90, 30, 140), (170, 90, 240), (240, 200, 255)], ribbon=[(110, 50, 170), (70, 30, 110)])


# ---------------------------------------------------------------------------------------------------------------------------------
# Projectiles


def orb_frames(prefix, size, frames, color_at, seed):
	rng = random.Random(seed)
	c = (size - 1) / 2
	for f in range(frames):
		noise = {(x, y): rng.random() for x in range(size) for y in range(size)}
		px = {}
		for y in range(size):
			for x in range(size):
				r = math.hypot(x - c, y - c) / (c + 0.5)
				ang = math.atan2(y - c, x - c)
				rgb = color_at(r, ang, f, noise[(x, y)], x - c, y - c)
				if rgb is not None:
					px[(x, y)] = rgb
		save_indexed(px, size, size, f"{prefix}{f:03d}.png")


def fire_color(r, ang, f, n, dx, dy):
	edge = r + 0.18 * (n - 0.5) + 0.08 * math.sin(ang * 5 + f * 1.7)
	if edge > 1.0:
		return None
	if edge < 0.35:
		return (255, 250, 200)
	if edge < 0.6:
		return (255, 200, 70)
	if edge < 0.82:
		return (240, 120, 30)
	return (170, 50, 20)


def meteor_color(r, ang, f, n, dx, dy):
	edge = r + 0.15 * (n - 0.5)
	if edge > 1.0:
		return None
	if edge > 0.82:
		# A rim of flame, flickering.
		return (255, 140, 40) if n > 0.45 else (200, 60, 20)
	# The rock: lit from the upper left, with glowing cracks.
	crack = abs(math.sin(dx * 1.3 + dy * 0.7 + f)) < 0.18 or abs(math.sin(dx * 0.6 - dy * 1.4)) < 0.14
	if crack:
		return (255, 170, 60)
	light = 0.5 - 0.06 * (dx + dy)
	base = 60 + 50 * light + 20 * n
	return (base * 1.05, base * 0.85, base * 0.7)


def poison_color(r, ang, f, n, dx, dy):
	if r > 1.0:
		return None
	bubble = n > 0.82 and r < 0.8
	if bubble:
		return (210, 255, 150)
	if r < 0.45 and dx + dy < 0:
		return (170, 240, 90)
	if r < 0.8:
		return (90, 190, 50)
	return (40, 100, 30)


def gravity_color(r, ang, f, n, dx, dy):
	if r > 1.0:
		return None
	swirl = math.sin(ang * 3 + r * 6 - f * 1.5)
	if r < 0.4:
		return (10, 6, 18)
	if r < 0.75:
		return (120, 60, 200) if swirl > 0.3 else (40, 20, 70)
	return (210, 160, 255) if swirl > 0 else (90, 40, 160)


def rune_circle(name, dark, mid, light, w=41, h=11):
	"""A ring of runes seen edge on, lying on the ground: an ellipse, notched with rune marks."""
	px = {}
	cx, cy = (w - 1) / 2, (h - 1) / 2
	for y in range(h):
		for x in range(w):
			ex = (x - cx) / (cx + 0.5)
			ey = (y - cy) / (cy + 0.5)
			r = math.hypot(ex, ey)
			if 0.78 < r <= 1.0:
				ang = math.atan2(ey, ex)
				tick = (int((ang + math.pi) / (2 * math.pi) * 16) % 2) == 0
				px[(x, y)] = light if (tick and r > 0.88) else mid
			elif 0.5 < r <= 0.58:
				px[(x, y)] = dark
	# A star of lines inside.
	for k in range(5):
		a = k * 2 * math.pi / 5 - math.pi / 2
		b = (k + 2) * 2 * math.pi / 5 - math.pi / 2
		for t in range(21):
			s = t / 20
			x = cx + (math.cos(a) * (1 - s) + math.cos(b) * s) * cx * 0.52
			y = cy + (math.sin(a) * (1 - s) + math.sin(b) * s) * cy * 0.52
			px[(int(round(x)), int(round(y)))] = mid
	save_indexed(px, w, h, name)


def make_projectiles():
	orb_frames("FireOrb", 9, 4, fire_color, 11)
	orb_frames("Meteor", 13, 4, meteor_color, 23)
	orb_frames("PoisonOrb", 7, 4, poison_color, 37)
	orb_frames("GravityOrb", 9, 4, gravity_color, 41)
	rune_circle("RuneCircleRed.png", (120, 20, 16), (220, 60, 30), (255, 190, 90))
	rune_circle("RuneCircleGreen.png", (20, 90, 30), (60, 200, 80), (190, 255, 170))


# ---------------------------------------------------------------------------------------------------------------------------------
# Glows (true colour, additive)


def glow(name, size, core, outer, falloff=2.0, core_size=0.25):
	"""A round glow: the core colour in the middle, fading through the outer colour to black at the edge."""
	img = Image.new("RGB", (size, size), (0, 0, 0))
	px = img.load()
	c = (size - 1) / 2
	for y in range(size):
		for x in range(size):
			r = math.hypot(x - c, y - c) / (c + 0.5)
			if r >= 1:
				continue
			i = (1 - r) ** falloff
			t = min(1.0, r / core_size) if core_size > 0 else 1
			col = [core[k] * (1 - t) + outer[k] * t for k in range(3)]
			px[x, y] = tuple(int(min(255, col[k] * i * 1.6)) for k in range(3))
	save_rgb(img, name)


def ring_glow(name, w, h, color, thickness=0.12, ticks=0, tick_color=None):
	"""An elliptical ring of light (rune circles, the time warp's clock), with tick marks if asked."""
	img = Image.new("RGB", (w, h), (0, 0, 0))
	px = img.load()
	cx, cy = (w - 1) / 2, (h - 1) / 2
	for y in range(h):
		for x in range(w):
			ex = (x - cx) / (cx + 0.5)
			ey = (y - cy) / (cy + 0.5)
			r = math.hypot(ex, ey)
			if r >= 1:
				continue
			ring = max(0.0, 1 - abs(r - 0.82) / thickness)
			inner = max(0.0, 1 - r) ** 3 * 0.35
			v = ring ** 1.5 + inner
			col = color
			if ticks:
				ang = math.atan2(ey, ex)
				frac = (ang + math.pi) / (2 * math.pi) * ticks
				near = abs(frac - round(frac))
				if near < 0.06 and 0.6 < r < 0.95:
					v = max(v, 1.0 - abs(r - 0.78) * 4)
					col = tick_color or color
			px[x, y] = tuple(int(min(255, col[k] * v)) for k in range(3))
	save_rgb(img, name)


def make_glows():
	fire_core, fire_out = (255, 250, 190), (255, 110, 20)
	glow("GlowFireSmall.png", 15, fire_core, fire_out)
	glow("GlowFireBig.png", 41, fire_core, fire_out)
	glow("GlowFireHuge.png", 121, fire_core, fire_out, 1.6)
	vio_core, vio_out = (255, 230, 255), (150, 60, 255)
	glow("GlowVioletSmall.png", 15, vio_core, vio_out)
	glow("GlowVioletBig.png", 41, vio_core, vio_out)
	glow("GlowVioletHuge.png", 101, vio_core, vio_out, 1.6)
	ice_core, ice_out = (255, 255, 255), (110, 190, 255)
	glow("GlowIceSmall.png", 11, ice_core, ice_out)
	glow("GlowIceBig.png", 41, ice_core, ice_out)
	gr_core, gr_out = (220, 255, 200), (60, 230, 70)
	glow("GlowGreenSmall.png", 15, gr_core, gr_out)
	glow("GlowGreenBig.png", 51, gr_core, gr_out)
	glow("GlowGreenHuge.png", 101, gr_core, gr_out, 1.6)
	red_core, red_out = (255, 210, 160), (230, 30, 20)
	glow("GlowRedSmall.png", 15, red_core, red_out)
	glow("GlowRedHuge.png", 101, red_core, red_out, 1.6)
	glow("GlowGoldBig.png", 41, (255, 250, 220), (230, 170, 40))
	glow("GlowWhiteHuge.png", 151, (255, 255, 255), (150, 190, 255), 1.4)
	glow("GlowPurpleHuge.png", 101, (200, 150, 255), (90, 20, 180), 1.3, 0.5)
	ring_glow("GlowRuneRed.png", 61, 17, (255, 70, 30), 0.14)
	ring_glow("GlowRuneGreen.png", 61, 17, (60, 255, 90), 0.14)
	ring_glow("GlowClock.png", 151, 151, (255, 190, 60), 0.05, 12, (255, 240, 180))


def main():
	os.makedirs(OUT, exist_ok=True)
	make_items()
	make_projectiles()
	make_glows()
	print("spell sprites done")


if __name__ == "__main__":
	main()
