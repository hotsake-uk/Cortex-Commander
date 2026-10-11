"""Makes the sandbox maps: four sizes of open air map (no floor, no walls, no wrap) and the themes they come in, with their backdrop art.
Run from anywhere: python Tools/MakeSandboxMaps.py. Needs Pillow and numpy.

Writes Data/Base.rte/Scenes/Sandbox/: an empty terrain picture per size, the backdrop pictures of every theme (full colour, so the skies are
smooth gradients rather than the palette's steps), and SandboxMaps.ini with the terrains, the backdrop layers and a scene per size and theme,
named "Sandbox <size> - <theme>". The main menu's Sandbox screen finds the themes from those names, in the order they're written here.

The backdrop layers use ParallaxFactor (see SLBackground): a sky or star layer spread over the whole height of the map (ParallaxFitHeight),
and scenery whose bottom sits at the bottom of the screen with the camera at the bottom of the map, sinking out of sight as the camera rises.
Every picture tiles left to right."""
import math
import os

import numpy as np
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Data'))
FOLDER = 'Base.rte/Scenes/Sandbox'
OUT = os.path.join(ROOT, FOLDER.replace('/', os.sep))

SCENE_HEIGHT = 12288
SIZES = [('Small', 3072), ('Medium', 6144), ('Large', 9216), ('Extra Large', 12288)]

SKY_WIDTH = 64
SKY_HEIGHT = 2400  # Taller than any screen, so a sky spread over the map always covers it.


def rgb(hex_color):
	hex_color = hex_color.lstrip('#')
	return np.array([int(hex_color[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float32)


def mix(a, b, t):
	return a + (b - a) * t


class Canvas:
	"""An RGBA picture that tiles left to right: everything drawn past an edge comes in at the other."""

	def __init__(self, width, height):
		self.width = width
		self.height = height
		self.color = np.zeros((height, width, 3), dtype=np.float32)
		self.alpha = np.zeros((height, width), dtype=bool)

	def _wrapped(self, x0, x1):
		"""Column ranges (canvas start, canvas end) covering [x0, x1) round the seam."""
		spans = []
		for shift in (-self.width, 0, self.width):
			a, b = max(0, x0 + shift), min(self.width, x1 + shift)
			if a < b:
				spans.append((a, b, shift))
		return spans

	def put(self, x0, y0, mask, color):
		"""Paints the boolean mask (rows, columns) with its top left at x0, y0, in one colour or a per pixel (rows, columns, 3) array."""
		rows, cols = mask.shape
		y_a, y_b = max(0, y0), min(self.height, y0 + rows)
		if y_a >= y_b:
			return
		for a, b, shift in self._wrapped(x0, x0 + cols):
			sub = mask[y_a - y0:y_b - y0, a - shift - x0:b - shift - x0]
			region = self.color[y_a:y_b, a:b]
			if isinstance(color, np.ndarray) and color.ndim == 3:
				region[sub] = color[y_a - y0:y_b - y0, a - shift - x0:b - shift - x0][sub]
			else:
				region[sub] = color
			self.alpha[y_a:y_b, a:b] |= sub

	def save(self, name):
		rgba = np.zeros((self.height, self.width, 4), dtype=np.uint8)
		rgba[..., :3] = np.clip(self.color + 0.5, 0, 255).astype(np.uint8)
		rgba[..., 3] = np.where(self.alpha, 255, 0)
		rgba[~self.alpha, :3] = 0
		Image.fromarray(rgba, 'RGBA').save(os.path.join(OUT, name), optimize=True)


def periodic_noise(width, rng, octaves, roughness=0.5, base_frequency=2):
	"""A smooth random curve over width columns, -1 to 1 roughly, that joins up at the seam."""
	x = np.arange(width) / width * 2 * math.pi
	total = np.zeros(width, dtype=np.float32)
	amplitude = 1.0
	norm = 0.0
	for octave in range(octaves):
		frequency = base_frequency * 2 ** octave
		for _ in range(2):
			k = frequency + rng.integers(0, frequency + 1)
			total += amplitude * np.sin(k * x + rng.uniform(0, 2 * math.pi))
			norm += amplitude
		amplitude *= roughness
	return total / norm * 1.6


def ridge_mask(height, ridge_top):
	"""Columns filled from ridge_top[x] down."""
	rows = np.arange(height)[:, None]
	return rows >= np.round(ridge_top)[None, :].astype(int)


def shaded(height, width, top_color, bottom_color, ridge_top=None, rim=None, rim_width=3):
	"""A per pixel colour: top_color fading to bottom_color down the picture, with a lighter rim along the ridge if asked."""
	t = (np.arange(height, dtype=np.float32) / max(1, height - 1))[:, None, None]
	color = np.broadcast_to(mix(top_color, bottom_color, t), (height, width, 3)).copy()
	if ridge_top is not None and rim is not None:
		rows = np.arange(height)[:, None]
		depth = rows - np.round(ridge_top)[None, :]
		edge = (depth >= 0) & (depth < rim_width)
		color[edge] = mix(color[edge], rim, 0.55)
	return color


def dither(height, width, rng, strength=1.2):
	return rng.uniform(-strength, strength, (height, width, 1)).astype(np.float32)


def sky(name, stops, rng):
	"""A vertical gradient through (position 0 to 1, colour) stops, top to bottom."""
	canvas = Canvas(SKY_WIDTH, SKY_HEIGHT)
	t = np.arange(SKY_HEIGHT, dtype=np.float32) / (SKY_HEIGHT - 1)
	positions = [p for p, _ in stops]
	colors = np.stack([c for _, c in stops])
	column = np.stack([np.interp(t, positions, colors[:, channel]) for channel in range(3)], axis=1)
	canvas.color[:] = column[:, None, :] + dither(SKY_HEIGHT, SKY_WIDTH, rng)
	canvas.alpha[:] = True
	canvas.save(name)


def disc(radius):
	size = radius * 2 + 1
	yy, xx = np.mgrid[-radius:radius + 1, -radius:radius + 1]
	return (xx * xx + yy * yy) <= radius * radius, size


def ellipse(rx, ry):
	yy, xx = np.mgrid[-ry:ry + 1, -rx:rx + 1]
	return (xx * xx) / max(1, rx * rx) + (yy * yy) / max(1, ry * ry) <= 1.0


def hills(name, width, height, rng, color_top, color_bottom, rim, base, amplitude, octaves=4, roughness=0.5, base_frequency=2, decorate=None):
	"""Rolling ground: a ridge base + amplitude * noise pixels above the bottom, shaded down."""
	canvas = Canvas(width, height)
	ridge = height - (base + amplitude * periodic_noise(width, rng, octaves, roughness, base_frequency))
	ridge = np.clip(ridge, 2, height - 1)
	canvas.put(0, 0, ridge_mask(height, ridge), shaded(height, width, color_top, color_bottom, ridge, rim) + dither(height, width, rng))
	if decorate:
		decorate(canvas, ridge, rng)
	canvas.save(name)
	return ridge


def jagged(name, width, height, rng, color_top, color_bottom, base, amplitude, snow=None, snow_depth=0.0, peaks=9, rim=None):
	"""Mountains: sharp peaks with snow caps above snow_depth of their height if asked."""
	canvas = Canvas(width, height)
	x = np.arange(width, dtype=np.float32)
	ridge_height = np.zeros(width, dtype=np.float32)
	spacing = width / peaks
	for index in range(peaks):
		center = (index + rng.uniform(0.15, 0.85)) * spacing
		peak = amplitude * rng.uniform(0.55, 1.0)
		slope = peak / (spacing * rng.uniform(0.45, 0.8))
		distance = np.abs(((x - center + width / 2) % width) - width / 2)
		ridge_height = np.maximum(ridge_height, peak - slope * distance)
	ridge_height += amplitude * 0.08 * periodic_noise(width, rng, 6, 0.6, 16)
	ridge = np.clip(height - (base + ridge_height), 2, height - 1)
	color = shaded(height, width, color_top, color_bottom, ridge, rim) + dither(height, width, rng)
	if snow is not None:
		rows = np.arange(height)[:, None]
		snow_line = height - base - amplitude * snow_depth + amplitude * 0.05 * periodic_noise(width, rng, 5, 0.6, 12)[None, :]
		snowy = rows < snow_line
		color[snowy] = mix(color[snowy], snow, 0.85)
	canvas.put(0, 0, ridge_mask(height, ridge), color)
	canvas.save(name)


def pine(height):
	width = max(3, int(height * 0.42)) | 1
	mask = np.zeros((height, width), dtype=bool)
	trunk = max(2, height // 30)
	for row in range(height - trunk):
		half = int((row / (height - trunk)) * (width / 2))
		# Tiers: every few rows the outline steps back in.
		half = int(half * (0.75 + 0.25 * ((row % max(3, height // 6)) / max(3, height // 6))))
		mask[row, width // 2 - half:width // 2 + half + 1] = True
	mask[height - trunk:, width // 2 - max(0, width // 14):width // 2 + max(1, width // 14) + 1] = True
	return mask


def round_tree(height):
	width = max(5, int(height * 0.8))
	mask = np.zeros((height, width), dtype=bool)
	crown = ellipse(width // 2, int(height * 0.38))
	crown_rows = min(crown.shape[0], height)
	mask[:crown_rows, :crown.shape[1]][crown[:crown_rows, :mask.shape[1]]] = True
	trunk_width = max(1, width // 8)
	mask[int(height * 0.6):, width // 2 - trunk_width // 2:width // 2 + trunk_width // 2 + 1] = True
	return mask


def trees_on(canvas, ridge, rng, count, size_range, color, shape):
	for _ in range(count):
		x = int(rng.uniform(0, canvas.width))
		tree_height = int(rng.uniform(*size_range))
		mask = shape(tree_height)
		# Planted at the lowest ground under the tree, so no sky shows between the trunk and a slope.
		left = x - mask.shape[1] // 2
		ground = max(int(ridge[(left + column) % canvas.width]) for column in range(mask.shape[1]))
		canvas.put(left, ground - tree_height + 2, mask, color)


def forest(name, width, height, rng, color, base, amplitude, tree_size, density):
	"""A tree line: a low ridge thick with pines."""
	canvas = Canvas(width, height)
	ridge = np.clip(height - (base + amplitude * periodic_noise(width, rng, 3, 0.5, 2)), 2, height - 1)
	canvas.put(0, 0, ridge_mask(height, ridge), color)
	x = 0.0
	while x < width:
		tree_height = int(rng.uniform(*tree_size))
		mask = pine(tree_height)
		left = int(x) - mask.shape[1] // 2
		ground = max(int(ridge[(left + column) % width]) for column in range(mask.shape[1]))
		canvas.put(left, ground - tree_height + 2, mask, color)
		x += mask.shape[1] * density * rng.uniform(0.5, 1.1)
	# Close the slivers of sky between neighbouring trees' lowest branches and the ground.
	band = 40
	for column in range(width):
		top = int(ridge[column])
		if top - band - 1 >= 0 and canvas.alpha[top - band - 1, column]:
			canvas.alpha[top - band:top, column] = True
			canvas.color[top - band:top, column] = color
	canvas.save(name)


def clouds(name, width, height, rng, count, light, shadow, band=(0.08, 0.7), size=(60, 170)):
	"""Puffy clouds scattered through a band of a tall picture spread over the map."""
	canvas = Canvas(width, height)
	for _ in range(count):
		cx = int(rng.uniform(0, width))
		cy = int(rng.uniform(*band) * height)
		cloud_width = int(rng.uniform(*size))
		puffs = []
		for _ in range(int(rng.uniform(4, 8))):
			rx = int(cloud_width * rng.uniform(0.18, 0.32))
			ry = int(rx * rng.uniform(0.55, 0.8))
			ox = int(rng.uniform(-0.5, 0.5) * cloud_width)
			oy = int(rng.uniform(-0.4, 0.1) * ry)
			puffs.append((ox, oy, rx, ry))
		flat = max(oy + ry for ox, oy, rx, ry in puffs) - 2
		for ox, oy, rx, ry in puffs:
			mask = ellipse(rx, ry)
			# Flat bottoms, darker underneath.
			rows = np.arange(mask.shape[0])[:, None] + oy - ry
			mask &= rows <= flat
			t = np.clip((rows - (oy - ry)) / (2 * ry + 1), 0, 1)
			color = np.broadcast_to(mix(light, shadow, t[..., None] ** 1.5), mask.shape + (3,)).copy()
			canvas.put(cx + ox - rx, cy + oy - ry, mask, color)
	canvas.save(name)


def stars(name, width, height, rng, count, colors, band=(0.0, 0.85)):
	canvas = Canvas(width, height)
	for _ in range(count):
		x = int(rng.uniform(0, width))
		y = int(rng.uniform(*band) * height)
		color = colors[int(rng.integers(0, len(colors)))] * rng.uniform(0.55, 1.0)
		if rng.uniform() < 0.07:
			mask = np.array([[0, 1, 0], [1, 1, 1], [0, 1, 0]], dtype=bool)
			canvas.put(x - 1, y - 1, mask, color)
		else:
			canvas.put(x, y, np.ones((1, 1), dtype=bool), color)
	canvas.save(name)


def body(name, width, height, rng, x, y, radius, color_light, color_dark, rings=None, craters=0, glow=None):
	"""A sun, moon or planet, once per picture width."""
	canvas = Canvas(width, height)
	if glow is not None:
		for step in range(4, 0, -1):
			r = int(radius * (1 + 0.18 * step))
			mask, _ = disc(r)
			canvas.put(x - r, y - r, mask, mix(glow[1], glow[0], step / 4))
	mask, size = disc(radius)
	yy, xx = np.mgrid[-radius:radius + 1, -radius:radius + 1]
	light = np.clip(0.5 + 0.5 * (-(xx + yy) / (radius * 1.6)), 0, 1)[..., None]
	color = mix(color_dark, color_light, light)
	for _ in range(craters):
		cr = int(rng.uniform(0.06, 0.16) * radius)
		cx, cy = rng.uniform(-0.6, 0.6, 2) * radius
		crater = ((xx - cx) ** 2 + (yy - cy) ** 2) <= cr * cr
		color[crater] *= 0.86
	if rings is not None:
		ring_color, tilt = rings
		canvas.put(x - radius, y - radius, mask, color)
		ring_rx = int(radius * 2.1)
		ring_ry = int(ring_rx * tilt)
		ryy, rxx = np.mgrid[-ring_ry:ring_ry + 1, -ring_rx:ring_rx + 1]
		outer = (rxx / ring_rx) ** 2 + (ryy / max(1, ring_ry)) ** 2 <= 1
		inner = (rxx / (ring_rx * 0.72)) ** 2 + (ryy / max(1, ring_ry * 0.72)) ** 2 <= 1
		band = outer & ~inner
		# Behind the planet in its top half, in front below.
		in_planet = np.zeros_like(band)
		py0, px0 = ring_ry - radius, ring_rx - radius
		for row in range(band.shape[0]):
			planet_row = row - py0
			if 0 <= planet_row < size:
				segment = slice(max(0, px0), min(band.shape[1], px0 + size))
				in_planet[row, segment] = mask[planet_row, segment.start - px0:segment.stop - px0]
		front = band & ~(in_planet & (ryy < 0))
		canvas.put(x - ring_rx, y - ring_ry, front, ring_color)
	else:
		canvas.put(x - radius, y - radius, mask, color)
	canvas.save(name)


def skyline(name, width, height, rng, color, base, tall, count, windows=None, spires=False):
	canvas = Canvas(width, height)
	canvas.put(0, height - base, np.ones((base, width), dtype=bool), color)
	x = 0
	while x < width:
		building_width = int(rng.uniform(26, 70))
		building_height = int(rng.uniform(0.25, 1.0) ** 1.6 * tall) + 20
		top = height - base - building_height
		mask = np.ones((building_height + base, building_width), dtype=bool)
		if rng.uniform() < 0.35:
			step = int(building_width * rng.uniform(0.15, 0.3))
			mask[:int(building_height * 0.15), :step] = False
			mask[:int(building_height * 0.15), -step:] = False
		canvas.put(x, top, mask, color)
		if spires and rng.uniform() < 0.3:
			spire_height = int(rng.uniform(20, 60))
			canvas.put(x + building_width // 2 - 1, top - spire_height, np.ones((spire_height, 3), dtype=bool), color)
		if windows is not None:
			for wy in range(top + 8, height - base - 4, 9):
				for wx in range(x + 5, x + building_width - 6, 8):
					if rng.uniform() < 0.33:
						canvas.put(wx, wy, np.ones((4, 3), dtype=bool), windows * rng.uniform(0.75, 1.0))
		x += building_width + int(rng.uniform(-6, 10))
		if count and x > width:
			break
	canvas.save(name)


def crystals(name, width, height, rng, color, glow, base, tall, count):
	canvas = Canvas(width, height)
	ridge = np.clip(height - (base + base * 0.5 * periodic_noise(width, rng, 4, 0.5, 3)), 2, height - 1)
	canvas.put(0, 0, ridge_mask(height, ridge), color)
	for _ in range(count):
		x = int(rng.uniform(0, width))
		spire_height = int(rng.uniform(0.3, 1.0) * tall)
		spire_width = max(6, int(spire_height * rng.uniform(0.12, 0.22)))
		mask = np.zeros((spire_height, spire_width), dtype=bool)
		for row in range(spire_height):
			half = int((row / spire_height) * spire_width / 2) + 1
			mask[row, spire_width // 2 - half:spire_width // 2 + half] = True
		top = int(ridge[x % width]) - spire_height + 6
		shade = np.broadcast_to(mix(glow, color, np.linspace(0, 1, spire_height)[:, None, None] ** 0.6), (spire_height, spire_width, 3)).copy()
		canvas.put(x - spire_width // 2, top, mask, shade)
	canvas.save(name)


def write_empty_terrains():
	for size_name, width in SIZES:
		image = Image.new('P', (width, SCENE_HEIGHT), 0)
		image.putpalette([0] * 768)
		image.save(os.path.join(OUT, f'Sandbox{size_name.replace(" ", "")}.png'), optimize=True)


# Each theme: name, description, and its layers far to near. A layer: (file, kind, parallax factor (x, y)), kind 'fit' for one spread
# over the map's height (skies, stars, clouds, sun and moon), 'ground' for scenery standing at the bottom.
THEMES = []


def theme(name, description, layers):
	THEMES.append((name, description, layers))


def make_art():
	rng = np.random.default_rng(20261011)
	blue_top, blue_horizon = rgb('2f63b8'), rgb('b9dcf5')

	# 1. Clear Sky: the gradient and nothing else.
	sky('ClearSky.png', [(0.0, rgb('2a5aad')), (0.55, rgb('5d97d8')), (1.0, rgb('bfe0f7'))], rng)
	theme('Clear Sky', 'Open blue sky and nothing else.', [('ClearSky.png', 'fit', 0.0)])

	# 2. Rolling Hills.
	sky('HillsSky.png', [(0.0, blue_top), (0.6, rgb('6aa5df')), (1.0, rgb('cfe8f6'))], rng)
	clouds('HillsClouds.png', 1600, SKY_HEIGHT, rng, 14, rgb('ffffff'), rgb('c5d3e3'))
	hills('HillsFar.png', 2048, 520, rng, rgb('8db8a6'), rgb('9cc3b5'), rgb('a9cdbd'), 280, 75, base_frequency=2)
	hills('HillsMid.png', 2048, 460, rng, rgb('5f9a52'), rgb('4c7f43'), rgb('86bb6a'), 230, 110, base_frequency=2,
	      decorate=lambda c, r, g: trees_on(c, r, g, 40, (14, 26), rgb('3e6b37'), round_tree))
	hills('HillsNear.png', 2560, 380, rng, rgb('4a8a3c'), rgb('2f5e28'), rgb('77b257'), 160, 80, base_frequency=1,
	      decorate=lambda c, r, g: trees_on(c, r, g, 18, (34, 60), rgb('2c5426'), round_tree))
	theme('Rolling Hills', 'Green hills under a summer sky.', [
		('HillsSky.png', 'fit', 0.0), ('HillsClouds.png', 'fit', 0.03), ('HillsFar.png', 'ground', 0.08), ('HillsMid.png', 'ground', 0.18), ('HillsNear.png', 'ground', 0.32)])

	# 3. Mountains.
	sky('MountainsSky.png', [(0.0, rgb('244f9e')), (0.6, rgb('5e93d2')), (1.0, rgb('d3e6f3'))], rng)
	clouds('MountainsClouds.png', 1800, SKY_HEIGHT, rng, 9, rgb('f4f8fb'), rgb('b8c7d8'), band=(0.15, 0.6))
	jagged('MountainsFar.png', 2560, 900, rng, rgb('9fb4cc'), rgb('aec2d6'), 220, 640, snow=rgb('f2f7fb'), snow_depth=0.62, peaks=7)
	jagged('MountainsMid.png', 2560, 700, rng, rgb('5f7189'), rgb('6e8197'), 160, 480, snow=rgb('e3ecf4'), snow_depth=0.78, peaks=8, rim=rgb('8a9bb0'))
	forest('MountainsNear.png', 2560, 300, rng, rgb('23402f'), 110, 40, (30, 70), 0.55)
	theme('Mountains', 'Snowy peaks in the haze, pines below.', [
		('MountainsSky.png', 'fit', 0.0), ('MountainsClouds.png', 'fit', 0.02), ('MountainsFar.png', 'ground', 0.05), ('MountainsMid.png', 'ground', 0.14), ('MountainsNear.png', 'ground', 0.3)])

	# 4. Desert.
	sky('DesertSky.png', [(0.0, rgb('3a72c0')), (0.55, rgb('86b4dc')), (0.85, rgb('e8d9b8')), (1.0, rgb('f3e2bd'))], rng)
	body('DesertSun.png', 3000, SKY_HEIGHT, rng, 900, int(SKY_HEIGHT * 0.22), 46, rgb('fffbe8'), rgb('fff2c2'), glow=(rgb('f8eccb'), rgb('fff6dc')))
	mesas = Canvas(2560, 620)
	x = 0
	while x < mesas.width:
		mesa_width = int(rng.uniform(140, 420))
		mesa_height = int(rng.uniform(150, 380))
		mask = np.zeros((mesa_height, mesa_width), dtype=bool)
		for row in range(mesa_height):
			inset = int(max(0, 1 - row / (mesa_height * 0.25)) * mesa_width * 0.12) + int((row / mesa_height) * -mesa_width * 0.05)
			mask[row, max(0, inset):mesa_width - max(0, inset)] = True
		color = shaded(mesa_height, mesa_width, rgb('c98a5a'), rgb('d9a578'))
		color[:, :mesa_width // 3] *= 0.92
		mesas.put(x, mesas.height - mesa_height - 60, mask, color)
		x += mesa_width + int(rng.uniform(80, 400))
	mesas.put(0, mesas.height - 60, np.ones((60, mesas.width), dtype=bool), rgb('d9a578'))
	mesas.save('DesertFar.png')
	hills('DesertMid.png', 2560, 340, rng, rgb('e3bd85'), rgb('d6a96e'), rgb('f1d3a2'), 170, 70, octaves=2, base_frequency=3)
	hills('DesertNear.png', 3072, 280, rng, rgb('d8a565'), rgb('b8834a'), rgb('ebc68c'), 110, 60, octaves=2, base_frequency=2,
	      decorate=lambda c, r, g: trees_on(c, r, g, 6, (40, 70), rgb('6f7d3c'), lambda h: cactus(h)))
	theme('Desert', 'Mesas and dunes under a hot sky.', [
		('DesertSky.png', 'fit', 0.0), ('DesertSun.png', 'fit', 0.01), ('DesertFar.png', 'ground', 0.07), ('DesertMid.png', 'ground', 0.17), ('DesertNear.png', 'ground', 0.3)])

	# 5. Pine Forest.
	sky('ForestSky.png', [(0.0, rgb('2b5ea8')), (0.6, rgb('6b9fd4')), (1.0, rgb('d6ebf2'))], rng)
	forest('ForestFar.png', 2048, 420, rng, rgb('7f9fa0'), 200, 60, (40, 80), 0.5)
	forest('ForestMid.png', 2048, 380, rng, rgb('3f6b55'), 150, 50, (60, 120), 0.5)
	forest('ForestNear.png', 2560, 380, rng, rgb('1d3a29'), 90, 30, (120, 220), 0.6)
	theme('Pine Forest', 'Layer on layer of pine trees.', [
		('ForestSky.png', 'fit', 0.0), ('ForestFar.png', 'ground', 0.08), ('ForestMid.png', 'ground', 0.18), ('ForestNear.png', 'ground', 0.34)])

	# 6. Coast.
	sky('CoastSky.png', [(0.0, rgb('2d64b4')), (0.6, rgb('6fa8de')), (1.0, rgb('dbeef8'))], rng)
	clouds('CoastClouds.png', 1800, SKY_HEIGHT, rng, 10, rgb('ffffff'), rgb('c9d6e4'), band=(0.25, 0.8), size=(80, 220))
	sea = Canvas(2048, 420)
	sea_height = 240
	sea_color = shaded(sea_height, sea.width, rgb('3f86b8'), rgb('1f5d8e')) + dither(sea_height, sea.width, rng, 2.0)
	for row in range(0, sea_height, 7):
		for _ in range(int(sea.width / 60)):
			wx = int(rng.uniform(0, sea.width))
			sea_color[row, wx:wx + int(rng.uniform(6, 24))] = rgb('8cc1df')
	sea.put(0, sea.height - sea_height, np.ones((sea_height, sea.width), dtype=bool), sea_color)
	for _ in range(3):
		island_width = int(rng.uniform(120, 360))
		island = ellipse(island_width // 2, int(island_width * rng.uniform(0.12, 0.22)))
		half = island[:island.shape[0] // 2 + 1]
		sea.put(int(rng.uniform(0, sea.width)), sea.height - sea_height - half.shape[0] + 1, half, rgb('6b8f8a'))
	sea.save('CoastSea.png')
	hills('CoastNear.png', 2560, 220, rng, rgb('ead7a6'), rgb('d1b981'), rgb('f6e8c4'), 70, 30, octaves=2, base_frequency=2,
	      decorate=lambda c, r, g: trees_on(c, r, g, 8, (60, 110), rgb('3f6a3a'), palm))
	theme('Coast', 'A calm sea with islands on the horizon, and a beach.', [
		('CoastSky.png', 'fit', 0.0), ('CoastClouds.png', 'fit', 0.03), ('CoastSea.png', 'ground', 0.1), ('CoastNear.png', 'ground', 0.34)])

	# 7. Arctic.
	sky('ArcticSky.png', [(0.0, rgb('4b78b5')), (0.6, rgb('9cc4e6')), (1.0, rgb('eef6fb'))], rng)
	jagged('ArcticFar.png', 2560, 600, rng, rgb('c5d8ea'), rgb('d6e5f1'), 200, 300, peaks=5)
	glacier = Canvas(2560, 460)
	x = 0
	while x < glacier.width:
		block_width = int(rng.uniform(60, 220))
		block_height = int(rng.uniform(80, 260))
		color = shaded(block_height + 120, block_width, rgb('e9f3fa'), rgb('a6c8e2'))
		color[:, :block_width // 4] *= 0.93
		# Broken, sloping tops rather than flat ones.
		tilt = rng.uniform(-0.35, 0.35)
		tops = np.clip(np.cumsum(rng.integers(-1, 2, block_width)) + tilt * np.arange(block_width) + block_height * 0.15, 0, block_height)
		mask = np.arange(block_height + 120)[:, None] >= tops[None, :]
		glacier.put(x, glacier.height - 120 - block_height, mask, color)
		x += block_width + int(rng.uniform(-20, 10))
	glacier.save('ArcticMid.png')
	hills('ArcticNear.png', 2560, 240, rng, rgb('f6fbff'), rgb('d3e4f1'), rgb('ffffff'), 90, 45, octaves=3, base_frequency=2)
	theme('Arctic', 'Ice shelves and snow drifts under a pale sky.', [
		('ArcticSky.png', 'fit', 0.0), ('ArcticFar.png', 'ground', 0.06), ('ArcticMid.png', 'ground', 0.16), ('ArcticNear.png', 'ground', 0.32)])

	# 8. City Skyline.
	sky('CitySky.png', [(0.0, rgb('3462a8')), (0.6, rgb('7aa5d4')), (1.0, rgb('dfe7ec'))], rng)
	skyline('CityFar.png', 2560, 720, rng, rgb('a9b8c8'), 40, 620, 0, spires=True)
	skyline('CityMid.png', 2560, 520, rng, rgb('6c7b8f'), 40, 420, 0, windows=rgb('9fb2c6'))
	skyline('CityNear.png', 3072, 320, rng, rgb('39424f'), 40, 200, 0, windows=rgb('7f8ea0'))
	theme('City Skyline', 'Towers of a far city.', [
		('CitySky.png', 'fit', 0.0), ('CityFar.png', 'ground', 0.06), ('CityMid.png', 'ground', 0.15), ('CityNear.png', 'ground', 0.3)])

	# 9. Sunset.
	sky('SunsetSky.png', [(0.0, rgb('241a4d')), (0.35, rgb('5b2f73')), (0.65, rgb('c2546a')), (0.85, rgb('f19a55')), (1.0, rgb('fbd27a'))], rng)
	body('SunsetSun.png', 3200, SKY_HEIGHT, rng, 1200, int(SKY_HEIGHT * 0.93), 110, rgb('fff1b8'), rgb('ffcf6e'), glow=(rgb('f7a95c'), rgb('fcd58a')))
	clouds('SunsetClouds.png', 1800, SKY_HEIGHT, rng, 12, rgb('f6a882'), rgb('7b3f6e'), band=(0.45, 0.85), size=(120, 300))
	hills('SunsetFar.png', 2048, 480, rng, rgb('7b3c6a'), rgb('6d3561'), rgb('a2537a'), 260, 110)
	hills('SunsetMid.png', 2048, 400, rng, rgb('46224c'), rgb('3a1d42'), rgb('6e3462'), 180, 90,
	      decorate=lambda c, r, g: trees_on(c, r, g, 30, (16, 30), rgb('3a1d42'), round_tree))
	hills('SunsetNear.png', 2560, 320, rng, rgb('1f1028'), rgb('140a1b'), rgb('3a1d42'), 110, 60,
	      decorate=lambda c, r, g: trees_on(c, r, g, 10, (60, 120), rgb('140a1b'), pine))
	theme('Sunset', 'An orange and purple evening with dark hills.', [
		('SunsetSky.png', 'fit', 0.0), ('SunsetSun.png', 'fit', 0.01), ('SunsetClouds.png', 'fit', 0.03), ('SunsetFar.png', 'ground', 0.08), ('SunsetMid.png', 'ground', 0.18), ('SunsetNear.png', 'ground', 0.32)])

	# 10. Night.
	sky('NightSky.png', [(0.0, rgb('04061a')), (0.5, rgb('0d1840')), (1.0, rgb('27386b'))], rng)
	stars('NightStars.png', 1400, SKY_HEIGHT, rng, 700, [rgb('ffffff'), rgb('cfe0ff'), rgb('fff1cf')])
	body('NightMoon.png', 3000, SKY_HEIGHT, rng, 2100, int(SKY_HEIGHT * 0.2), 60, rgb('f4f1e1'), rgb('a9a698'), craters=8, glow=(rgb('1a2a5a'), rgb('2b3c70')))
	hills('NightFar.png', 2048, 460, rng, rgb('1d2a52'), rgb('19244a'), rgb('34497d'), 240, 110)
	hills('NightNear.png', 2560, 340, rng, rgb('0b1128'), rgb('070b1c'), rgb('22325f'), 120, 70,
	      decorate=lambda c, r, g: trees_on(c, r, g, 14, (50, 110), rgb('070b1c'), pine))
	theme('Night', 'Stars, a full moon and dark hills.', [
		('NightSky.png', 'fit', 0.0), ('NightStars.png', 'fit', 0.01), ('NightMoon.png', 'fit', 0.015), ('NightFar.png', 'ground', 0.08), ('NightNear.png', 'ground', 0.3)])

	# 11. Alien World.
	sky('AlienSky.png', [(0.0, rgb('1e0e3d')), (0.4, rgb('4f2384')), (0.75, rgb('b24f9a')), (1.0, rgb('f2a3b8'))], rng)
	stars('AlienStars.png', 1400, SKY_HEIGHT, rng, 350, [rgb('ffffff'), rgb('d6c2ff')], band=(0.0, 0.5))
	body('AlienPlanet.png', 3600, SKY_HEIGHT, rng, 1300, int(SKY_HEIGHT * 0.3), 150, rgb('8ee0d0'), rgb('2d6f7a'), rings=(rgb('e9c9f2'), 0.22))
	crystals('AlienFar.png', 2560, 640, rng, rgb('6b3f8f'), rgb('b88ae0'), 160, 420, 30)
	crystals('AlienNear.png', 2560, 420, rng, rgb('2a1640'), rgb('46e0c8'), 90, 300, 16)
	theme('Alien World', 'A violet sky, a ringed planet and crystal spires.', [
		('AlienSky.png', 'fit', 0.0), ('AlienStars.png', 'fit', 0.01), ('AlienPlanet.png', 'fit', 0.012), ('AlienFar.png', 'ground', 0.08), ('AlienNear.png', 'ground', 0.3)])


def cactus(height):
	width = max(9, int(height * 0.5)) | 1
	mask = np.zeros((height, width), dtype=bool)
	trunk = max(3, width // 4)
	mid = width // 2
	mask[:, mid - trunk // 2:mid + trunk // 2 + 1] = True
	arm = max(2, trunk - 1)
	left_y = int(height * 0.45)
	mask[left_y:left_y + arm, 1:mid] = True
	mask[int(height * 0.2):left_y + arm, 1:1 + arm] = True
	right_y = int(height * 0.35)
	mask[right_y:right_y + arm, mid:width - 1] = True
	mask[int(height * 0.12):right_y + arm, width - 1 - arm:width - 1] = True
	return mask


def palm(height):
	width = int(height * 0.9) | 1
	mask = np.zeros((height, width), dtype=bool)
	mid = width // 2
	for row in range(int(height * 0.15), height):
		lean = int(((height - row) / height) ** 2 * width * 0.15)
		mask[row, mid + lean - 1:mid + lean + 2] = True
	top = int(height * 0.15)
	lean_top = int(((height - top) / height) ** 2 * width * 0.15)
	for angle in (-2.6, -2.0, -1.2, -0.5, 0.2, 0.9):
		for step in range(int(width * 0.45)):
			x = int(mid + lean_top + math.sin(angle) * step)
			y = int(top + (step / (width * 0.45)) ** 2 * height * 0.18 - math.cos(angle) * step * 0.35)
			if 0 <= x < width and 0 <= y < height:
				mask[max(0, y - 1):y + 1, x] = True
	return mask


def write_ini():
	lines = [
		'///////////////////////////////////////////////////////////////////////',
		'// Sandbox maps: open air of four sizes (no floor, no walls, no wrap), each in every theme. Made by Tools/MakeSandboxMaps.py: change that',
		'// and run it again rather than editing this by hand. Started from Sandbox on the main menu: a size, then a theme.',
		'',
	]
	for size_name, width in SIZES:
		lines += [
			'Terrain = SLTerrain',
			f'\tPresetName = Sandbox {size_name} Terrain',
			'\tBitmapFile = ContentFile',
			f'\t\tFilePath = {FOLDER}/Sandbox{size_name.replace(" ", "")}.png',
			'\tWrapX = 0',
			'\tWrapY = 0',
			'\tBackgroundTexture = ContentFile',
			'\t\tFilePath = Base.rte/Scenes/Textures/Default.png',
			'',
		]
	for theme_name, _, layers in THEMES:
		for file_name, kind, factor in layers:
			lines += [
				'AddBackgroundLayer = SLBackground',
				f'\tPresetName = Sandbox {theme_name} {file_name[:-4]}',
				'\tWrapX = 1',
				'\tWrapY = 0',
				'\tIgnoreAutoScaling = 1',
				'\tParallaxFactor = Vector',
				f'\t\tX = {factor}',
				f'\t\tY = {factor}',
				f'\tParallaxFitHeight = {1 if kind == "fit" else 0}',
				'\tBitmapFile = ContentFile',
				f'\t\tFilePath = {FOLDER}/{file_name}',
				'',
			]
	lines.append('')
	for size_name, width in SIZES:
		for theme_name, description, layers in THEMES:
			lines += [
				'AddScene = Scene',
				f'\tPresetName = Sandbox {size_name} - {theme_name}',
				f'\tDescription = {size_name} sandbox map, {width} by {SCENE_HEIGHT}: open air with no floor or walls. {description}',
				'\tTerrain = SLTerrain',
				f'\t\tCopyOf = Sandbox {size_name} Terrain',
				'\tGlobalAcceleration = Vector',
				'\t\tX = 0',
				'\t\tY = 20',
			]
			# Nearest first, as in every scene.
			for file_name, _, _ in reversed(layers):
				lines += ['\tAddBackgroundLayer = SLBackground', f'\t\tCopyOf = Sandbox {theme_name} {file_name[:-4]}']
			lines.append('')
	with open(os.path.join(OUT, 'SandboxMaps.ini'), 'w', newline='\n') as ini:
		ini.write('\n'.join(lines))


if __name__ == '__main__':
	os.makedirs(OUT, exist_ok=True)
	make_art()
	write_empty_terrains()
	write_ini()
	print(f'{len(THEMES)} themes, {len(SIZES)} sizes, written to {OUT}')
