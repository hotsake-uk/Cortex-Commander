#!/usr/bin/env python3
"""Makes the UI themes in Base.rte/GUIs/Themes: Classic (the game as it was), Green, Grey and Brown.

Each theme is a folder holding Theme.ini and the skin images it swaps in. Theme.ini names the theme, swaps skin images
("Image = <original> => <replacement>"), swaps GUI colours ("Color = <original> => <replacement>", palette indices or #RRGGBB)
and sets the HUD and tool window colours ("HUD.<role> = #RRGGBB"). See Source/GUI/UITheme.h.

How each theme's images are made:
- Green takes the build menu and inventory's greens (Skins/SkinGreen.png) exactly. The menu skin (Menus/SkinBlue.png) and its
  tabs are recoloured: their blues go to the green of the same lightness, with the build menu's main green for the menu's main
  blue, and their browns go to the greens the build menu used in their place. The console and loading screen's brown skin is
  recoloured the same way the build menu's green skin was made from the brown one.
- Grey and Brown take the game's own grey and brown skins (Skins/SkinGray.png, Skins/SkinBrown.png). The build menu, inventory
  and object picker skins are copied from those, pixel for pixel, where the green skin has a pixel. The menu skin's blues go to
  the grey or brown of the same lightness.

Run from the repository root: python3 Tools/MakeUIThemes.py
"""

import colorsys
import os
from collections import Counter, defaultdict

from PIL import Image

GUIS = "Data/Base.rte/GUIs"
SKINS = GUIS + "/Skins"
MENUS = SKINS + "/Menus"
THEMES = GUIS + "/Themes"
MODULE_GUIS = "Base.rte/GUIs"


def hexc(rgb):
	return "#%02x%02x%02x" % tuple(rgb[:3])


def parse(hex_string):
	hex_string = hex_string.lstrip("#")
	return tuple(int(hex_string[i:i + 2], 16) for i in (0, 2, 4))


def hsv(rgb):
	return colorsys.rgb_to_hsv(*(c / 255.0 for c in rgb[:3]))


def rgb_of(h, s, v):
	return tuple(int(round(c * 255.0)) for c in colorsys.hsv_to_rgb(h, max(0.0, min(1.0, s)), max(0.0, min(1.0, v))))


def shift(rgb, anchor_from, anchor_to):
	"""The colour of the same standing next to anchor_to as rgb has next to anchor_from: anchor_to's hue, saturation scaled by
	the anchors', darker shades scaled down with the anchors' value and lighter ones keeping their share of the headroom."""
	_, s, v = hsv(rgb)
	_, sf, vf = hsv(anchor_from)
	ht, st, vt = hsv(anchor_to)
	s2 = s * (st / sf if sf > 0 else 1.0)
	if v <= vf:
		v2 = v * vt / vf
	else:
		v2 = 1.0 - (1.0 - v) * (1.0 - vt) / (1.0 - vf)
	return rgb_of(ht, s2, v2)


def distance(a, b):
	return sum((x - y) ** 2 for x, y in zip(a[:3], b[:3])) ** 0.5


def snap(rgb, palette, reach=18.0):
	"""The nearest of the theme's own colours, if one is close, so near misses land on the skin's exact shades."""
	best = min(palette, key=lambda p: distance(p, rgb))
	return best if distance(best, rgb) <= reach else rgb


def is_blue(rgb):
	h, s, _ = hsv(rgb)
	return 0.58 <= h <= 0.84 and s >= 0.1


def is_green(rgb):
	h, s, _ = hsv(rgb)
	return 0.22 <= h <= 0.37 and s >= 0.2


def correspondence(source_path, target_path):
	"""For each colour in the source image, the colour the target image most often has at the same pixels."""
	source = Image.open(source_path).convert("RGBA")
	target = Image.open(target_path).convert("RGBA")
	seen = defaultdict(Counter)
	for y in range(source.height):
		for x in range(source.width):
			seen[source.getpixel((x, y))[:3]][target.getpixel((x, y))[:3]] += 1
	return {colour: counts.most_common(1)[0][0] for colour, counts in seen.items()}


def recolour_rgba(source_path, out_path, colour_map):
	image = Image.open(source_path).convert("RGBA")
	pixels = image.load()
	for y in range(image.height):
		for x in range(image.width):
			r, g, b, a = pixels[x, y]
			if a and (r, g, b) in colour_map:
				pixels[x, y] = colour_map[(r, g, b)] + (a,)
	image.save(out_path)


def masked_copy(shape_path, art_path, out_path):
	"""The art image's palette pixels wherever the shape image isn't its colour key (magenta), and the key elsewhere."""
	shape = Image.open(shape_path)
	art = Image.open(art_path)
	assert shape.mode == "P" and art.mode == "P" and shape.size == art.size
	key = shape.getpixel((shape.width - 1, 0))
	out = art.copy()
	pixels = out.load()
	shape_pixels = shape.load()
	for y in range(out.height):
		for x in range(out.width):
			if shape_pixels[x, y] == key:
				pixels[x, y] = key
	out.save(out_path)


def colours_of(path):
	return [rgb[:3] for _, rgb in Image.open(path).convert("RGBA").getcolors(1 << 16) if rgb[3]]


def game_palette():
	palette = Image.open("Data/Base.rte/palette.bmp").getpalette()
	return [tuple(palette[i * 3:i * 3 + 3]) for i in range(256)]


MENU_MAIN = parse("6c76a8")  # The menu skin's main blue.
BUILD_MAIN = parse("4c6944")  # The build menu's main green.
GREY_MAIN = parse("797961")
BROWN_MAIN = parse("7d6130")

# The tool windows' and HUD's colours on the Classic theme, taken from the menu skin (see ToolTheme in Source/Managers/ToolWidgets.h).
HUD_CLASSIC = {
	"Panel": parse("3b4153"),
	"Well": parse("0c1427"),
	"WellHover": parse("1b253a"),
	"WellPressed": parse("0c0c1c"),
	"Edge": parse("6c76a8"),
	"EdgeLight": parse("c1acc6"),
	"EdgeDark": parse("181c37"),
}

# The palette colours GUI .ini files fill boxes with: the build menu, inventory and object picker's green (144), the area picker's (161)
# and the console's brown (56).
FILL_COLOURS = [144, 161, 56]


def menu_map(theme_main, theme_colours, brown_map=None):
	"""Recolours the menu skin's blues (and, given brown_map, its browns) into the theme's."""
	colours = set(colours_of(MENUS + "/SkinBlue.png")) | set(colours_of(MENUS + "/TabBlue.png")) | set(HUD_CLASSIC.values())
	mapping = {}
	for colour in colours:
		if colour == MENU_MAIN:
			mapping[colour] = theme_main
		elif is_blue(colour) and colour not in (parse("2c3038"), parse("141820")):  # Those two are in every skin, not only the blue.
			mapping[colour] = snap(shift(colour, MENU_MAIN, theme_main), theme_colours)
		elif brown_map and colour in brown_map:
			mapping[colour] = brown_map[colour]
	return mapping


def write_theme(folder, name, order, description, images, colours, hud):
	os.makedirs(folder, exist_ok=True)
	lines = [
		"// " + description,
		"// Made by Tools/MakeUIThemes.py. See Source/GUI/UITheme.h for what each line does.",
		"Name = " + name,
		"Order = %d" % order,
	]
	for original, replacement in images:
		lines.append("Image = %s => %s" % (original, replacement))
	for original, replacement in colours:
		lines.append("Color = %s => %s" % (original, replacement))
	for role, colour in hud.items():
		lines.append("HUD.%s = %s" % (role, hexc(colour)))
	with open(folder + "/Theme.ini", "w", newline="\r\n") as file:
		file.write("\n".join(lines) + "\n")


def main():
	palette = game_palette()
	build_greens = [c for c in set(colours_of(SKINS + "/SkinGreen.png")) | set(colours_of(SKINS + "/TabGreen.png")) if is_green(c)]
	grey_colours = [parse("797961"), parse("484838")]
	brown_colours = [parse("7d6130"), parse("6d4c28"), parse("483828"), parse("aa8d50")]

	write_theme(THEMES + "/Classic", "Classic", 0, "Classic: the game's own colours, blue menus and a green build menu and inventory.", [], [], {})

	# Green.
	brown_to_green = {k: v for k, v in correspondence(SKINS + "/SkinBrown.png", SKINS + "/SkinGreen.png").items() if k != v and is_green(v)}
	folder = THEMES + "/Green"
	os.makedirs(folder, exist_ok=True)
	green_map = menu_map(BUILD_MAIN, build_greens, brown_to_green)
	recolour_rgba(MENUS + "/SkinBlue.png", folder + "/MenuSkin.png", green_map)
	recolour_rgba(MENUS + "/TabBlue.png", folder + "/MenuTab.png", green_map)
	console_map = {c: brown_to_green.get(c, snap(shift(c, BROWN_MAIN, BUILD_MAIN), build_greens)) for c in colours_of(MENUS + "/SkinBrown.png") if c == BROWN_MAIN or c in brown_to_green}
	recolour_rgba(MENUS + "/SkinBrown.png", folder + "/ConsoleSkin.png", console_map)
	write_theme(folder, "Green", 1, "Green: the build menu and inventory's greens on every menu, the console, the loading screen, the tool windows and the HUD.", [
		(MODULE_GUIS + "/Skins/Menus/SkinBlue.png", MODULE_GUIS + "/Themes/Green/MenuSkin.png"),
		(MODULE_GUIS + "/Skins/Menus/TabBlue.png", MODULE_GUIS + "/Themes/Green/MenuTab.png"),
		(MODULE_GUIS + "/Skins/Menus/SkinBrown.png", MODULE_GUIS + "/Themes/Green/ConsoleSkin.png"),
	], [
		("56", hexc(shift(palette[56], BROWN_MAIN, BUILD_MAIN))),
	], {role: green_map.get(colour, colour) for role, colour in HUD_CLASSIC.items()})

	# Grey and Brown.
	for name, order, main, own, skin, tab in (("Grey", 2, GREY_MAIN, grey_colours, "SkinGray", "TabGray"), ("Brown", 3, BROWN_MAIN, brown_colours, "SkinBrown", "TabBrown")):
		folder = THEMES + "/" + name
		os.makedirs(folder, exist_ok=True)
		theme_map = menu_map(main, own)
		recolour_rgba(MENUS + "/SkinBlue.png", folder + "/MenuSkin.png", theme_map)
		recolour_rgba(MENUS + "/TabBlue.png", folder + "/MenuTab.png", theme_map)
		masked_copy(SKINS + "/SkinGreen.png", SKINS + "/" + skin + ".png", folder + "/BuildSkin.png")
		masked_copy(SKINS + "/TabGreen.png", SKINS + "/" + tab + ".png", folder + "/BuildTab.png")
		images = [
			(MODULE_GUIS + "/Skins/Menus/SkinBlue.png", MODULE_GUIS + "/Themes/%s/MenuSkin.png" % name),
			(MODULE_GUIS + "/Skins/Menus/TabBlue.png", MODULE_GUIS + "/Themes/%s/MenuTab.png" % name),
			(MODULE_GUIS + "/Skins/SkinGreen.png", MODULE_GUIS + "/Themes/%s/BuildSkin.png" % name),
			(MODULE_GUIS + "/Skins/TabGreen.png", MODULE_GUIS + "/Themes/%s/BuildTab.png" % name),
		]
		if name == "Grey":
			console_map = {BROWN_MAIN: GREY_MAIN}
			recolour_rgba(MENUS + "/SkinBrown.png", folder + "/ConsoleSkin.png", console_map)
			images.append((MODULE_GUIS + "/Skins/Menus/SkinBrown.png", MODULE_GUIS + "/Themes/Grey/ConsoleSkin.png"))
		colours = [(str(index), hexc(shift(palette[index], BUILD_MAIN, main))) for index in FILL_COLOURS if index != 56]
		if name == "Grey":
			colours.append(("56", hexc(shift(palette[56], BROWN_MAIN, GREY_MAIN))))
		description = "%s: the game's own %s skin on every menu, the build menu, the inventory, the console, the tool windows and the HUD." % (name, name.lower())
		write_theme(folder, name, order, description, images, colours, {role: theme_map.get(colour, colour) for role, colour in HUD_CLASSIC.items()})


if __name__ == "__main__":
	main()
