#!/usr/bin/env python3
"""Placeholder sprites for the creatures (CR-1, CR-6, phase 3), drawn or recoloured from the livestock's and the wildlife's.

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


def new_sprite(size):
	"""A blank sprite in the game palette, and the palette index nearest a colour."""
	palette = Image.open(os.path.join(MEGACRAB_DIR, "BigCrabBodyA000.png")).getpalette()[:768]
	image = Image.new("P", size, 0)
	image.putpalette(palette)
	return image, ImageDraw.Draw(image), lambda rgb: nearest_index(palette, rgb)


# The Ooze (phase 3): a legless blob of green slime, drawn whole.

def ooze_body(target, frame, width, height):
	"""Draws an ooze, width x height, facing right: a low, lumpy mound of slime with a glossy top, a few bubbles in it and a pair of pale
	eyes at the front. The second frame wobbles, the mound a pixel lower and wider."""
	image, draw, ink = new_sprite((width, height))
	wobble = frame
	top = 2 + wobble
	draw.ellipse((1 - wobble, top, width - 2 + wobble, height + height // 2), fill=ink((70, 150, 40)), outline=ink((30, 70, 20)))
	draw.ellipse((width // 4, top + 2, width // 2 + 2, top + 2 + height // 5), fill=ink((140, 210, 80)))
	for bx, by in ((0.3, 0.6), (0.55, 0.75), (0.45, 0.5)):
		x, y = int(width * bx), int(height * by)
		draw.ellipse((x, y, x + 2, y + 2), outline=ink((160, 230, 110)))
	eye_y = top + height // 3
	for x in (width - 8, width - 5):
		draw.point((x, eye_y), fill=ink((240, 250, 200)))
	image.save(target)


# The Scorpion (phase 3): the Mega Crab's legs in sandy chitin, on a body of its own with the tail curled over it.
SCORPION_RAMP = [(40, 24, 10), (72, 44, 18), (108, 70, 30), (144, 100, 44), (180, 134, 64), (214, 172, 96)]


def scorpion_body(target, frame):
	"""Draws the scorpion's body, 48 x 30, facing right: a long segmented back, low down, with the tail rising from its rear and curling
	forward over it to the sting, and two black eyes up front. The second frame twitches the sting a pixel."""
	image, draw, ink = new_sprite((48, 30))
	shell, dark, light = ink((150, 104, 46)), ink((60, 36, 14)), ink((200, 156, 84))
	# The back, in segments.
	draw.ellipse((8, 15, 44, 29), fill=shell, outline=dark)
	for x in (16, 23, 30, 37):
		draw.line((x, 16, x, 28), fill=dark)
	draw.line((12, 18, 40, 18), fill=light)
	# The tail, from the rear up and over, and the sting.
	twitch = frame
	for i, (x, y) in enumerate(((8, 18), (4, 13), (5, 7), (10, 3), (16, 2), (21, 4 + twitch))):
		r = 3 if i < 4 else 2
		draw.ellipse((x - r, y - r, x + r, y + r), fill=shell, outline=dark)
	draw.polygon([(23, 5 + twitch), (27, 7 + twitch), (23, 9 + twitch)], fill=ink((30, 20, 10)))
	# Eyes.
	draw.point((41, 20), fill=ink((10, 10, 10)))
	draw.point((39, 20), fill=ink((10, 10, 10)))
	image.save(target)


def scorpion_plate(target):
	"""Draws the plate over the scorpion's front, 10 x 14: thick dark chitin with a ridge."""
	image, draw, ink = new_sprite((10, 14))
	draw.ellipse((0, 0, 9, 13), fill=ink((96, 62, 26)), outline=ink((40, 24, 10)))
	draw.line((5, 2, 5, 11), fill=ink((170, 124, 60)))
	image.save(target)


def scorpion_pincer(target):
	"""Draws a pincer, 14 x 8, pointing right: two curved jaws."""
	image, draw, ink = new_sprite((14, 8))
	shell, dark = ink((150, 104, 46)), ink((60, 36, 14))
	draw.ellipse((0, 1, 8, 7), fill=shell, outline=dark)
	draw.polygon([(7, 1), (13, 0), (9, 3)], fill=shell, outline=dark)
	draw.polygon([(7, 6), (13, 7), (9, 4)], fill=shell, outline=dark)
	image.save(target)


# The Ogre's club (phase 3): a gnarled log, thick at the far end.

def ogre_club(target):
	"""Draws the club, 32 x 10, held at its left end: a wooden haft swelling to a knotted head with an iron band."""
	image, draw, ink = new_sprite((32, 10))
	wood, dark, light = ink((110, 72, 36)), ink((50, 30, 14)), ink((150, 106, 60))
	draw.polygon([(0, 4), (16, 2), (31, 0), (31, 9), (16, 7), (0, 6)], fill=wood, outline=dark)
	draw.line((2, 4, 28, 2), fill=light)
	draw.line((20, 1, 20, 8), fill=ink((80, 80, 86)))
	for x, y in ((25, 3), (28, 6), (14, 5)):
		draw.point((x, y), fill=dark)
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

	ooze = os.path.join(CREATURES_DIR, "Ooze")
	os.makedirs(ooze, exist_ok=True)
	for frame in range(2):
		ooze_body(os.path.join(ooze, "OozeBodyA%03d.png" % frame), frame, 36, 20)
		ooze_body(os.path.join(ooze, "OozeSpawnBodyA%03d.png" % frame), frame, 24, 14)
	print("wrote the ooze's bodies")

	scorpion = os.path.join(CREATURES_DIR, "Scorpion")
	os.makedirs(scorpion, exist_ok=True)
	for name in sorted(os.listdir(MEGACRAB_DIR)):
		if name.startswith("BigCrabLeg") and name.endswith(".png"):
			recolour_by_light(os.path.join(MEGACRAB_DIR, name), os.path.join(scorpion, name.replace("BigCrab", "Scorpion")), SCORPION_RAMP)
	for frame in range(2):
		scorpion_body(os.path.join(scorpion, "ScorpionBodyA%03d.png" % frame), frame)
	scorpion_plate(os.path.join(scorpion, "ScorpionPlate.png"))
	scorpion_pincer(os.path.join(scorpion, "ScorpionPincer.png"))
	print("wrote the scorpion's legs, body, plate and pincer")

	ogre = os.path.join(CREATURES_DIR, "Ogre")
	os.makedirs(ogre, exist_ok=True)
	ogre_club(os.path.join(ogre, "OgreClub.png"))
	print("wrote the ogre's club")


if __name__ == "__main__":
	main()
