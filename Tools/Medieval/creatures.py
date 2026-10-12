#!/usr/bin/env python3
"""Draws the Medieval faction's creatures (Data/Medieval.rte/Actors/<Creature>/) from Base.rte's creature rigs, recoloured, so their
frames and joints still fit the rig's limb paths and AI.

- Wolf: the Hound's body and legs in wolf grey, pale below, with amber eyes (Summon Wolf's, Devices/Spells/Spells2.ini, and the Wolf in
  Actors/Wolf/Wolf.ini).

Every sprite stays an 8-bit paletted PNG in the game palette: a source pixel's shade (how light it is) picks a shade of the creature's own
ramp, so the rig's shading is kept; index 0 (magenta) is the see-through background.

Run from the repository root: python3 Tools/Medieval/creatures.py Data
"""
import glob
import os
import sys

from PIL import Image

DATA = sys.argv[1] if len(sys.argv) > 1 else "Data"
HOUND = os.path.join(DATA, "Base.rte/Actors/Creatures/Hound")

_cache = {}


def nearest(palette, rgb):
	if rgb not in _cache:
		best, best_d = 1, None
		for i in range(1, 256):
			r, g, b = palette[i * 3:i * 3 + 3]
			if (r, g, b) == (255, 0, 255):
				continue
			d = 2 * (r - rgb[0]) ** 2 + 4 * (g - rgb[1]) ** 2 + 3 * (b - rgb[2]) ** 2
			if best_d is None or d < best_d:
				best, best_d = i, d
		_cache[rgb] = best
	return _cache[rgb]


def ramp_colour(ramp, shade):
	"""The ramp's colour for a shade from 0 (darkest) to 1 (lightest), blended between its steps."""
	pos = shade * (len(ramp) - 1)
	i = min(int(pos), len(ramp) - 2)
	f = pos - i
	return tuple(int(ramp[i][k] * (1 - f) + ramp[i + 1][k] * f) for k in range(3))


def reshade(source, target, ramp, eyes=None, outline=None):
	"""Writes the sprite at source to target with every colour's shade mapped onto ramp. Reddish pixels (the Hound's eyes) become
	eyes, if given; the darkest pixels become outline, if given."""
	image = Image.open(source)
	palette = image.getpalette()[:768]
	data = list(image.get_flattened_data()) if hasattr(image, "get_flattened_data") else list(image.getdata())
	shades = {}
	for index in set(data):
		if index == 0:
			continue
		r, g, b = palette[index * 3:index * 3 + 3]
		if (r, g, b) == (255, 0, 255):
			continue
		shades[index] = (0.299 * r + 0.587 * g + 0.114 * b) / 255, (r, g, b)
	if not shades:
		image.save(target)
		return
	lo = min(s for s, _ in shades.values())
	hi = max(s for s, _ in shades.values())
	remap = {}
	for index, (shade, (r, g, b)) in shades.items():
		if eyes and r > 120 and r > g * 2 and r > b * 2:
			remap[index] = nearest(palette, eyes)
			continue
		norm = (shade - lo) / max(hi - lo, 1e-6)
		if outline and norm < 0.08:
			remap[index] = nearest(palette, outline)
		else:
			remap[index] = nearest(palette, ramp_colour(ramp, norm))
	out = image.copy()
	out.putdata([remap.get(index, index) for index in data])
	out.save(target, transparency=0)


WOLF_RAMP = [(28, 28, 30), (58, 58, 62), (96, 94, 92), (138, 134, 128), (182, 178, 170), (220, 216, 206)]


def make_wolf():
	out = os.path.join(DATA, "Medieval.rte/Actors/Wolf")
	os.makedirs(out, exist_ok=True)
	for path in sorted(glob.glob(os.path.join(HOUND, "Hound*.png"))):
		name = os.path.basename(path).replace("Hound", "Wolf")
		reshade(path, os.path.join(out, name), WOLF_RAMP, eyes=(240, 180, 60), outline=(22, 22, 24))
	print("wolf sprites done")


def main():
	make_wolf()


main()
