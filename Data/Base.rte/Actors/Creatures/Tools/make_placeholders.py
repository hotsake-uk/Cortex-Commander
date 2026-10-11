#!/usr/bin/env python3
"""Placeholder sprites for the creatures (CR-1, CR-6), recoloured from the livestock's and the wildlife's.

Re-run from anywhere:
    python3 Data/Base.rte/Actors/Creatures/Tools/make_placeholders.py

The creatures are being proven before their real art is drawn (plan decision D4): each one borrows an animal's
body and legs, recoloured, with parts it shouldn't have (a goat's horns and beard) made transparent. The sprites
stay 8-bit paletted PNGs in the game palette: every colour is mapped to its nearest palette index, and index 0
(magenta) is the transparent background.
"""

import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
CREATURES_DIR = os.path.normpath(os.path.join(HERE, ".."))
ACTORS_DIR = os.path.dirname(CREATURES_DIR)
LIVESTOCK_DIR = os.path.join(ACTORS_DIR, "Livestock")
MEGACRAB_DIR = os.path.join(ACTORS_DIR, "Wildlife", "MegaCrab")


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
	out.putdata([remap.get(index, index) for index in image.get_flattened_data()])
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


def recolour_by_light(source, target, ramp):
	"""Writes the sprite at source to target with each colour swapped by its brightness for a step of ramp (darkest first)."""
	image = Image.open(source)
	palette = image.getpalette()[:768]
	remap = {}
	for index in set(image.get_flattened_data()):
		rgb = tuple(palette[index * 3:index * 3 + 3])
		if index == 0 or rgb == (255, 0, 255):
			remap[index] = 0
			continue
		light = (0.3 * rgb[0] + 0.55 * rgb[1] + 0.15 * rgb[2]) / 255.0
		remap[index] = nearest_index(palette, ramp[min(int(light * len(ramp)), len(ramp) - 1)])
	out = image.copy()
	out.putdata([remap[index] for index in image.get_flattened_data()])
	out.save(target)


# The Giant Spider (CR-6): the Mega Crab's legs in glossy black with a purple sheen, on a body of its own (spider_body).
SPIDER_RAMP = [(10, 8, 12), (22, 16, 28), (34, 24, 44), (48, 34, 62), (66, 48, 84), (92, 70, 112)]


def spider_body(target, frame):
	"""Draws the spider's body, 48 x 28, facing right: its head and thorax in the middle, where the legs join, with a cluster of red eyes
	and fangs in front, and a great round abdomen hanging behind with a red hourglass on it. The second frame breathes, the abdomen a
	pixel bigger."""
	palette = Image.open(os.path.join(MEGACRAB_DIR, "BigCrabBodyA000.png")).getpalette()[:768]
	image = Image.new("P", (48, 28), 0)
	image.putpalette(palette)
	draw = ImageDraw.Draw(image)
	ink = lambda rgb: nearest_index(palette, rgb)
	swell = frame
	# Abdomen, with a sheen along its top and the hourglass.
	draw.ellipse((1 - swell, 2 - swell, 23 + swell, 24 + swell), fill=ink((14, 10, 18)), outline=ink((4, 4, 6)))
	draw.ellipse((5, 5, 17, 11), fill=ink((40, 28, 52)))
	draw.ellipse((8, 6, 14, 8), fill=ink((70, 52, 90)))
	draw.polygon([(9, 12), (15, 12), (12, 15)], fill=ink((190, 20, 20)))
	draw.polygon([(9, 19), (15, 19), (12, 16)], fill=ink((190, 20, 20)))
	# Head and thorax, where the legs join.
	draw.ellipse((17, 9, 37, 23), fill=ink((20, 14, 26)), outline=ink((4, 4, 6)))
	draw.ellipse((21, 11, 31, 15), fill=ink((50, 36, 64)))
	# Eyes, and the fangs below them.
	for x, y in ((33, 12), (35, 13), (34, 15), (36, 15)):
		draw.point((x, y), fill=ink((230, 30, 30)))
	draw.line((35, 20, 38, 25), fill=ink((60, 50, 40)))
	draw.line((33, 21, 34, 26), fill=ink((60, 50, 40)))
	image.save(target)


def main():
	goat = os.path.join(LIVESTOCK_DIR, "Goat")
	hound = os.path.join(CREATURES_DIR, "Hound")
	os.makedirs(hound, exist_ok=True)
	for name in sorted(os.listdir(goat)):
		if name.endswith(".png"):
			recolour(os.path.join(goat, name), os.path.join(hound, name.replace("Goat", "Hound")), HOUND)
			print("wrote", name.replace("Goat", "Hound"))

	spider = os.path.join(CREATURES_DIR, "GiantSpider")
	os.makedirs(spider, exist_ok=True)
	for name in sorted(os.listdir(MEGACRAB_DIR)):
		if name.startswith("BigCrabLeg") and name.endswith(".png"):
			recolour_by_light(os.path.join(MEGACRAB_DIR, name), os.path.join(spider, name.replace("BigCrab", "Spider")), SPIDER_RAMP)
			print("wrote", name.replace("BigCrab", "Spider"))
	for frame in range(2):
		spider_body(os.path.join(spider, "SpiderBodyA%03d.png" % frame), frame)
		print("wrote", "SpiderBodyA%03d.png" % frame)


if __name__ == "__main__":
	main()
