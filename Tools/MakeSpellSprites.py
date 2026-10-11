"""Makes the spells' pictures (MG-1): what a spell looks like lying on the ground, a book or a scroll. A spell isn't drawn while a unit holds
it (DrawnWhenHeld = 0); the caster's empty hand takes the spell's pose instead. 8-bit in the game's palette, index 0 the see-through mask
colour, lit from the upper left.

The spell book (Telekinetic Push): a small leather-bound book with gold corners, a gold clasp and a rune on its blue cover.
The spell scroll (Acid Spray): a rolled parchment with wooden ends and a green wax seal.

Run from the repository's root: python Tools/MakeSpellSprites.py
"""

from pathlib import Path

from PIL import Image

OUT = Path("Data/Base.rte/Devices/Magic")
PALETTE_SOURCE = Path("Data/Ronin.rte/Actors/Infantry/RoninLight/Torso000.png")

COLORS = {
	"k": (22, 20, 26),  # outline
	# Blue leather cover
	"v": (24, 32, 80), "V": (44, 64, 140), "i": (92, 124, 200),
	# Gold
	"y": (126, 92, 28), "g": (196, 156, 44), "G": (244, 214, 104),
	# Pages
	"w": (214, 206, 180), "W": (170, 160, 132),
	# Parchment
	"p": (150, 124, 82), "P": (200, 176, 124), "q": (234, 218, 170),
	# Wood
	"n": (84, 56, 32), "N": (130, 92, 56),
	# Green wax
	"f": (30, 80, 30), "F": (60, 150, 50), "e": (130, 220, 90),
}

BOOK = [
	"..kkkkkkkkk.",
	".kGvvvvvvGkw",
	".kvVVVVVVvkw",
	".kvVViiVVvkw",
	".kvVVViVVgGk",
	".kvVViiVVgyk",
	".kvVVVVVVvkw",
	".kGvvvvvvGkw",
	"..kkkkkkkkWw",
]

SCROLL = [
	".kk.........kk.",
	"kNNkkkkkkkkkNNk",
	"knNqqqqqqqqqnNk",
	"knNPPPqqFfPPnNk",
	"knNPPPPeFfPPnNk",
	"knNpppppFppppNk",
	"kNNkkkkkkkkkNNk",
	".kk.........kk.",
]


def load_palette():
	flat = Image.open(PALETTE_SOURCE).getpalette()[:768]
	return flat, [tuple(flat[i * 3:i * 3 + 3]) for i in range(256)]


def nearest(rgb, palette, cache):
	if rgb not in cache:
		cache[rgb] = min(range(1, 255), key=lambda i: 2 * (palette[i][0] - rgb[0]) ** 2 + 4 * (palette[i][1] - rgb[1]) ** 2 + 3 * (palette[i][2] - rgb[2]) ** 2)
	return cache[rgb]


def save(rows, path, flat, palette, cache):
	width = max(len(row) for row in rows)
	assert all(len(row) == width for row in rows), path
	out = Image.new("P", (width, len(rows)), 0)
	out.putpalette(flat)
	pixels = out.load()
	for y, row in enumerate(rows):
		for x, ch in enumerate(row):
			if ch != ".":
				pixels[x, y] = nearest(COLORS[ch], palette, cache)
	out.save(OUT / path, transparency=0)


def main():
	OUT.mkdir(parents=True, exist_ok=True)
	flat, palette = load_palette()
	cache = {}
	save(BOOK, "SpellBook.png", flat, palette, cache)
	save(SCROLL, "SpellScroll.png", flat, palette, cache)
	print("spell sprites done")


main()
