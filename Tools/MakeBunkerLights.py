"""Draws the small placeable light fixtures (Base.rte, group "Bunker Lights") and writes their INI.
Run from anywhere: python Tools/MakeBunkerLights.py. Needs Pillow. The pictures use the game's palette, taken from an existing sprite."""
import os
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Data'))
FOLDER = 'Base.rte/Scenes/Objects/Bunkers/BunkerLights'
OUT = os.path.join(ROOT, FOLDER.replace('/', os.sep))
os.makedirs(OUT, exist_ok=True)

reference = Image.open(os.path.join(ROOT, 'Base.rte', 'Scenes', 'Objects', 'Bunkers', 'BunkerBits', 'TutRoofLightBitFG.png'))
palette = reference.getpalette()
colors = [tuple(palette[i * 3:i * 3 + 3]) for i in range(256)]


def nearest(rgb):
	# Index 0 is the mask, so never that.
	return min(range(1, 256), key=lambda i: sum((colors[i][c] - rgb[c]) ** 2 for c in range(3)))


DARK, MID, LIGHT = (45, 48, 56), (95, 100, 110), (150, 155, 165)

# name: (bulb color, bulb core color, light color)
LAMPS = {
	'White': ((215, 235, 230), (245, 252, 250), (225, 240, 235)),
	'Warm': ((250, 215, 140), (255, 240, 200), (255, 215, 150)),
	'Blue': ((120, 185, 250), (190, 225, 255), (140, 195, 255)),
	'Red': ((230, 60, 50), (255, 140, 120), (255, 60, 50)),
	'Green': ((90, 220, 120), (180, 255, 190), (110, 255, 140)),
}


def picture(width, height, rows):
	"""rows: list of (x0, x1, y, rgb) spans, inclusive."""
	image = Image.new('P', (width, height), 0)
	image.putpalette(palette)
	for x0, x1, y, rgb in rows:
		index = nearest(rgb)
		for x in range(x0, x1 + 1):
			image.putpixel((x, y), index)
	return image


def ceiling(bulb, core):
	return 12, 12, [(3, 8, 0, DARK), (4, 7, 1, MID), (4, 7, 2, bulb), (5, 6, 2, core), (4, 7, 3, bulb), (5, 6, 3, core), (5, 6, 4, bulb)]


def wall(bulb, core):
	rows = [(4, 7, 2, DARK), (4, 7, 9, DARK)]
	for y in range(3, 9):
		rows += [(4, 4, y, MID), (7, 7, y, MID), (5, 6, y, bulb)]
	rows += [(5, 6, 5, core), (5, 6, 6, core)]
	return 12, 12, rows


def floor(bulb, core):
	return 12, 12, [(5, 6, 7, bulb), (4, 7, 8, bulb), (5, 6, 8, core), (4, 7, 9, bulb), (5, 6, 9, core), (4, 7, 10, MID), (3, 8, 11, DARK)]


def tiny(bulb, core):
	return 12, 12, [(5, 6, 4, DARK), (4, 4, 5, DARK), (7, 7, 5, DARK), (4, 4, 6, DARK), (7, 7, 6, DARK), (5, 6, 7, DARK), (5, 6, 5, bulb), (5, 6, 6, core)]


def strip(bulb, core):
	return 24, 12, [(2, 21, 0, DARK), (2, 2, 1, MID), (21, 21, 1, MID), (3, 20, 1, bulb), (3, 20, 2, core), (3, 20, 3, bulb)]


def floodDown(bulb, core):
	return 12, 12, [(5, 6, 0, DARK), (5, 6, 1, MID), (3, 8, 2, DARK), (3, 8, 3, MID), (2, 9, 4, MID), (3, 8, 5, bulb), (4, 7, 5, core)]


def floodSide(bulb, core, right):
	rows = [(0, 2, 5, DARK), (0, 2, 6, DARK), (3, 6, 3, DARK), (3, 7, 4, MID), (3, 8, 5, MID), (3, 8, 6, MID), (3, 7, 7, MID), (3, 6, 8, DARK), (8, 8, 4, bulb), (9, 9, 5, core), (9, 9, 6, core), (8, 8, 7, bulb)]
	if not right:
		rows = [(11 - x1, 11 - x0, y, rgb) for x0, x1, y, rgb in rows]
	return 12, 12, rows


def beacon(bulb, core):
	return 12, 12, [(3, 8, 0, DARK), (4, 7, 1, DARK), (4, 7, 2, bulb), (4, 7, 3, bulb), (5, 6, 3, core), (4, 7, 4, bulb), (5, 6, 4, core), (5, 6, 5, bulb)]


# Preset name, picture file, shape, lamp, light position, radius, intensity, extra light properties, description, price.
FIXTURES = []
for lamp in ('White', 'Warm', 'Blue', 'Red', 'Green'):
	FIXTURES.append(('Ceiling Lamp %s' % lamp, 'CeilingLamp%s' % lamp, ceiling, lamp, (6, 6), 120, 1.1, {}, 'A lamp that hangs from the ceiling. It goes out if the ceiling it hangs from is destroyed.', 5))
for lamp in ('White', 'Warm', 'Blue', 'Red', 'Green'):
	FIXTURES.append(('Wall Lamp %s' % lamp, 'WallLamp%s' % lamp, wall, lamp, (6, 6), 100, 1.0, {}, 'A lamp on the back wall.', 5))
for lamp in ('White', 'Warm'):
	FIXTURES.append(('Floor Lamp %s' % lamp, 'FloorLamp%s' % lamp, floor, lamp, (6, 6), 100, 1.0, {}, 'A lamp that stands on the floor, shining upwards.', 5))
for lamp in ('White', 'Warm', 'Blue', 'Red', 'Green'):
	FIXTURES.append(('Tiny Light %s' % lamp, 'TinyLight%s' % lamp, tiny, lamp, (6, 6), 55, 0.8, {}, 'A small indicator light.', 2))
for lamp in ('White', 'Warm'):
	FIXTURES.append(('Strip Light %s' % lamp, 'StripLight%s' % lamp, strip, lamp, (12, 5), 150, 1.2, {}, 'A long bright ceiling light for rooms and hangars.', 10))
FIXTURES.append(('Floodlight Down', 'FloodlightDown', floodDown, 'White', (6, 7), 260, 1.7, {'ConeAngle': 32, 'ConeDirection': 90}, 'A powerful ceiling floodlight with a beam straight down.', 15))
FIXTURES.append(('Floodlight Right', 'FloodlightRight', lambda b, c: floodSide(b, c, True), 'White', (10, 6), 280, 1.7, {'ConeAngle': 28, 'ConeDirection': 20}, 'A powerful floodlight with a beam to the right and a little down. Good over an entrance.', 15))
FIXTURES.append(('Floodlight Left', 'FloodlightLeft', lambda b, c: floodSide(b, c, False), 'White', (1, 6), 280, 1.7, {'ConeAngle': 28, 'ConeDirection': 160}, 'A powerful floodlight with a beam to the left and a little down. Good over an entrance.', 15))
FIXTURES.append(('Warning Beacon', 'WarningBeacon', beacon, 'Red', (6, 6), 110, 1.3, {'Pulse': 0.8}, 'A red warning light that swells and fades.', 5))
FIXTURES.append(('Flickering Lamp', 'FlickeringLamp', ceiling, 'White', (6, 6), 110, 1.1, {'Flicker': 0.7}, 'A ceiling lamp on its last legs.', 5))

lines = ['///////////////////////////////////////////////////////////////////////', '// Bunker Lights: small fixtures that can be put anywhere. Made by Tools/MakeBunkerLights.py.', '// They are drawn on the back wall, so they never get in the way. A light goes out when what it hangs on (anything solid within five pixels) is destroyed.', '']
for name, fileName, shape, lamp, position, radius, intensity, extra, description, price in FIXTURES:
	bulb, core, lightColor = LAMPS[lamp]
	width, height, rows = shape(bulb, core)
	path = os.path.join(OUT, fileName + 'BG.png')
	if not os.path.exists(path) or fileName != 'FlickeringLamp':
		picture(width, height, rows).save(path)
	lines += ['', 'AddTerrainObject = TerrainObject', '\tPresetName = ' + name, '\tDescription = ' + description, '\tAddToGroup = Bunker Lights', '\tGoldValue = %d' % price,
	          '\tBGColorFile = ContentFile', '\t\tFilePath = %s/%sBG.png' % (FOLDER, fileName), '\tBitmapOffset = Vector', '\t\tX = 0', '\t\tY = 0',
	          '\tAddLight = TerrainLight', '\t\tOffset = Vector', '\t\t\tX = %g' % position[0], '\t\t\tY = %g' % position[1], '\t\tColor = Color', '\t\t\tR = %d' % lightColor[0], '\t\t\tG = %d' % lightColor[1], '\t\t\tB = %d' % lightColor[2], '\t\tRadius = %g' % radius, '\t\tIntensity = %g' % intensity]
	for key, value in extra.items():
		lines.append('\t\t%s = %g' % (key, value))
	lines.append('')
open(os.path.join(OUT, 'BunkerLights.ini'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
print(len(FIXTURES), 'fixtures written to', OUT)
