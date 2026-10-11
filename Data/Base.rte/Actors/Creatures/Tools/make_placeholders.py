#!/usr/bin/env python3
"""Placeholder sprites for the creatures (CR-1), recoloured from the livestock's.

Re-run from anywhere:
    python3 Data/Base.rte/Actors/Creatures/Tools/make_placeholders.py

The creatures are being proven before their real art is drawn (plan decision D4): each one borrows an animal's
body and legs, recoloured, with parts it shouldn't have (a goat's horns and beard) made transparent. The sprites
stay 8-bit paletted PNGs in the game palette: every colour is mapped to its nearest palette index, and index 0
(magenta) is the transparent background.
"""

import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
CREATURES_DIR = os.path.normpath(os.path.join(HERE, ".."))
ACTORS_DIR = os.path.dirname(CREATURES_DIR)
LIVESTOCK_DIR = os.path.join(ACTORS_DIR, "Livestock")


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


def recolour(source, target, wishes):
	"""Writes the sprite at source to target with its colours swapped: wishes maps a source RGB to a new RGB, or to None
	for transparent. Colours not named are kept."""
	image = Image.open(source)
	palette = image.getpalette()[:768]
	remap = {}
	for rgb, wish in wishes.items():
		for i in range(256):
			if tuple(palette[i * 3:i * 3 + 3]) == rgb:
				remap[i] = 0 if wish is None else nearest_index(palette, wish)
	out = image.copy()
	out.putdata([remap.get(index, index) for index in image.getdata()])
	out.save(target)


# The Hound: a goat with no horns or beard, in charcoal, with red eyes.
HOUND = {
	(72, 40, 32): (30, 28, 28),  # outline
	(104, 72, 40): (48, 44, 44),  # dark
	(151, 105, 57): (70, 66, 64),  # mid
	(172, 140, 85): (92, 86, 82),  # light
	(215, 180, 142): (120, 112, 106),  # highlight
	(104, 104, 88): None,  # horn
	(171, 150, 138): None,  # horn tip
	(218, 214, 197): None,  # beard
	(12, 12, 12): (220, 30, 30),  # eye
}


def main():
	goat = os.path.join(LIVESTOCK_DIR, "Goat")
	hound = os.path.join(CREATURES_DIR, "Hound")
	os.makedirs(hound, exist_ok=True)
	for name in sorted(os.listdir(goat)):
		if name.endswith(".png"):
			recolour(os.path.join(goat, name), os.path.join(hound, name.replace("Goat", "Hound")), HOUND)
			print("wrote", name.replace("Goat", "Hound"))


if __name__ == "__main__":
	main()
