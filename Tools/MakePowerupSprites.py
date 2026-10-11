#!/usr/bin/env python3
"""Draws the powerups' pictures: what each looks like lying on the ground or carried.

- Base.rte/Devices/Powerups: a small canister for each powerup (Energy Shield, Stim, Overcharge, Cloak, Medkit, Juggernaut), a grey
  body with a band and an emblem in the powerup's own colour, so one can be told from another across the field.
- Medieval.rte/Devices/Scrolls: a rolled scroll for the spell scrolls, its wax seal and ribbon in the colour of the spell's school.

8-bit in the game palette (index 0 the see-through mask colour), lit from the upper left, like Tools/Medieval/spells.py.

Run from the repository root: python3 Tools/MakePowerupSprites.py Data
"""
import os
import sys

from PIL import Image

DATA = sys.argv[1] if len(sys.argv) > 1 else "Data"
PAL_SRC = Image.open(os.path.join(DATA, "Ronin.rte/Actors/Infantry/RoninLight/Torso000.png"))
PAL_FLAT = PAL_SRC.getpalette()[:768]
PAL = [tuple(PAL_FLAT[i * 3:i * 3 + 3]) for i in range(256)]

_cache = {}


def nearest(rgb):
	rgb = tuple(int(max(0, min(255, c))) for c in rgb)
	if rgb not in _cache:
		_cache[rgb] = min(range(1, 255), key=lambda i: 2 * (PAL[i][0] - rgb[0]) ** 2 + 4 * (PAL[i][1] - rgb[1]) ** 2 + 3 * (PAL[i][2] - rgb[2]) ** 2)
	return _cache[rgb]


def shade(rgb, f):
	return tuple(c * f for c in rgb)


def save_grid(rows, colors, path):
	w = len(rows[0])
	assert all(len(r) == w for r in rows), path
	out = Image.new("P", (w, len(rows)), 0)
	out.putpalette(PAL_FLAT)
	dst = out.load()
	for y, row in enumerate(rows):
		for x, ch in enumerate(row):
			if ch != ".":
				dst[x, y] = nearest(colors[ch])
	os.makedirs(os.path.dirname(path), exist_ok=True)
	out.save(path, transparency=0)


# A canister lying on its side: cap, body with a coloured band, emblem (E) on the band. Light from the upper left.
CANISTER = [
	"..kkkkkkkk..",
	".kLLhhhhhhk.",
	"kcLbbEEbbhdk",
	"kcbbEEEEbbdk",
	"kcBBEEEEBBdk",
	"kcBBBEEBBBdk",
	"kcbbbbbbbbdk",
	".kddddddddk.",
	"..kkkkkkkk..",
]

# Emblems drawn over the band (E cells): each powerup's own shape, by which cells of the 4x4 emblem box are lit.
EMBLEMS = {
	"EnergyShield": [".##.", "#..#", "#..#", ".##."],
	"Stim": [".#..", "####", ".#..", ".#.."],
	"Overcharge": ["..#.", ".##.", "##..", ".#.."],
	"Cloak": ["#..#", ".##.", ".##.", "#..#"],
	"Medkit": [".#..", "###.", ".#..", "...."],
	"Juggernaut": ["####", "#..#", "####", ".##."],
}

POWERUPS = {
	"EnergyShield": (90, 170, 255),
	"Stim": (90, 220, 110),
	"Overcharge": (255, 150, 50),
	"Cloak": (170, 110, 240),
	"Medkit": (230, 60, 60),
	"Juggernaut": (240, 200, 70),
}


def canister(name, color):
	rows = [list(r) for r in CANISTER]
	emblem = EMBLEMS[name]
	# The emblem box sits over columns 4..7, rows 2..5; its lit cells are E, the rest band.
	for ey in range(4):
		for ex in range(4):
			x, y = 4 + ex, 2 + ey
			if rows[y][x] in "EbB":
				rows[y][x] = "E" if emblem[ey][ex] == "#" else ("b" if y < 4 else "B")
	colors = {
		"k": (30, 30, 34),
		"L": (200, 200, 205),
		"h": (150, 150, 158),
		"c": (110, 110, 118),
		"d": (70, 70, 78),
		"b": shade(color, 0.85),
		"B": shade(color, 0.6),
		"E": (245, 245, 250) if name != "Juggernaut" else (255, 250, 220),
	}
	save_grid(["".join(r) for r in rows], colors, os.path.join(DATA, "Base.rte/Devices/Powerups", name + ".png"))


# A rolled scroll: parchment, the curl at each end, a ribbon (r) and wax seal (s) in the school's colour.
SCROLL = [
	".kkk.......kk.",
	"kpPkkkkkkkkpPk",
	"kPppppprrppPpk",
	"kpppppsrsspppk",
	"kPpppprssrppPk",
	"kppppppppppppk",
	".kkkkkkkkkkkk.",
]

SCHOOLS = {
	"Fire": (230, 70, 30),
	"Storm": (255, 230, 90),
	"Frost": (130, 210, 255),
	"Arcane": (180, 90, 240),
	"Nature": (90, 200, 70),
	"Earth": (160, 110, 60),
	"Death": (60, 160, 90),
	"Wind": (210, 220, 230),
}


def scroll(school, color):
	colors = {
		"k": (60, 40, 25),
		"p": (225, 205, 160),
		"P": (185, 160, 115),
		"r": shade(color, 0.7),
		"s": color,
	}
	save_grid(SCROLL, colors, os.path.join(DATA, "Medieval.rte/Devices/Scrolls", "Scroll" + school + ".png"))


for name, color in POWERUPS.items():
	canister(name, color)
for school, color in SCHOOLS.items():
	scroll(school, color)
