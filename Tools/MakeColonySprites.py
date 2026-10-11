#!/usr/bin/env python3
"""Draws Base.rte's colony art in the game palette: the sci-fi colony buildings, the forester (a sci-fi woodcutter's staff), his laser saw,
and the log every woodcutter cuts.

The buildings are the Sci-fi style of the colony's buildings (Source/Managers/Colony.cpp), each drawn to its kind's plot so it works as the
Fantasy one does: a floor to stand on, a wall at each end, the doorway at the right on the floor, room inside for units to come out, and
the brain's chamber at the back (20 x 20 from 10 px in, on the floor) in an armoured alcove. Each is a foreground picture (where it is
solid), a material picture (what it is made of: Metal, Concrete, Glass) and a background picture (what is seen through it, inside).

Run from the repository root: python3 Tools/MakeColonySprites.py Data
The forester's limbs are the civilian's, recoloured by brightness onto his work clothes, so their frames and joints still fit.
"""
import math
import os
import sys

from PIL import Image

DATA = sys.argv[1]
BASE = os.path.join(DATA, "Base.rte")
PAL_SRC = Image.open(os.path.join(DATA, "Ronin.rte/Actors/Infantry/RoninLight/Torso000.png"))
PAL_FLAT = PAL_SRC.getpalette()[:768]
PAL = [tuple(PAL_FLAT[i * 3:i * 3 + 3]) for i in range(256)]

AIR, CONCRETE, METAL, GLASS = 0, 177, 178, 184

_cache = {}


def nearest(rgb):
	if rgb in _cache:
		return _cache[rgb]
	best, bestD = 1, 1e9
	for i in range(1, 255):
		p = PAL[i]
		d = 2 * (p[0] - rgb[0]) ** 2 + 4 * (p[1] - rgb[1]) ** 2 + 3 * (p[2] - rgb[2]) ** 2
		if d < bestD:
			best, bestD = i, d
	_cache[rgb] = best
	return best


def save(img, path):
	"""Saves an RGBA image as a palette PNG, index 0 (magenta) where transparent."""
	w, h = img.size
	out = Image.new("P", (w, h), 0)
	out.putpalette(PAL_FLAT)
	src = img.load()
	dst = out.load()
	for y in range(h):
		for x in range(w):
			r, g, b, a = src[x, y]
			dst[x, y] = 0 if a < 128 else nearest((r, g, b))
	full = os.path.join(BASE, path)
	os.makedirs(os.path.dirname(full), exist_ok=True)
	out.save(full, transparency=0)


def save_materials(mat, w, h, path):
	"""Saves material indices as a palette PNG: each pixel's index is its material."""
	out = Image.new("P", (w, h), 0)
	out.putpalette(PAL_FLAT)
	dst = out.load()
	for y in range(h):
		for x in range(w):
			dst[x, y] = mat[y][x]
	full = os.path.join(BASE, path)
	os.makedirs(os.path.dirname(full), exist_ok=True)
	out.save(full)


def mix(a, b, t):
	return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def shade(c, f):
	return tuple(max(0, min(255, int(c[i] * f))) for i in range(3))


# Colours.
STEEL = (118, 126, 138)
STEEL_DARK = (66, 72, 84)
STEEL_LIGHT = (176, 184, 194)
PANEL = (48, 54, 66)
PANEL_DARK = (30, 34, 44)
CONCRETE_C = (128, 124, 116)
HAZARD_Y = (232, 188, 40)
HAZARD_K = (34, 32, 30)
ORANGE = (220, 110, 30)
CYAN = (90, 220, 240)
CYAN_DIM = (40, 120, 150)
GREEN = (90, 230, 120)
GREEN_DIM = (30, 110, 60)
RED = (230, 50, 40)
WHITE = (240, 244, 248)
GLASS_C = (120, 170, 190)
WOOD = (120, 78, 44)
WOOD_DARK = (78, 48, 26)
WOOD_LIGHT = (176, 128, 80)


class Building:
	"""A building being drawn: its three pictures, and helpers to draw solid parts (picture and material at once) and what is seen inside."""

	def __init__(self, w, h):
		self.w, self.h = w, h
		self.fg = Image.new("RGBA", (w, h), (0, 0, 0, 0))
		self.bg = Image.new("RGBA", (w, h), (0, 0, 0, 0))
		self.mat = [[AIR] * w for _ in range(h)]
		self.f = self.fg.load()
		self.b = self.bg.load()
		self.floor = h - 6  # The top of the floor slab.

	def solid(self, x, y, color, material=METAL):
		if 0 <= x < self.w and 0 <= y < self.h:
			self.f[x, y] = color + (255,)
			self.mat[y][x] = material

	def clear(self, x, y):
		if 0 <= x < self.w and 0 <= y < self.h:
			self.f[x, y] = (0, 0, 0, 0)
			self.mat[y][x] = AIR

	def back(self, x, y, color):
		if 0 <= x < self.w and 0 <= y < self.h:
			self.b[x, y] = color + (255,)

	def box(self, x0, y0, x1, y1, color, material=METAL):
		for y in range(y0, y1 + 1):
			for x in range(x0, x1 + 1):
				self.solid(x, y, color, material)

	def back_box(self, x0, y0, x1, y1, color):
		for y in range(y0, y1 + 1):
			for x in range(x0, x1 + 1):
				self.back(x, y, color)

	def plate(self, x0, y0, x1, y1, base=STEEL, material=METAL, rivets=True, seams=12):
		"""A metal plate: lit along the top and left, shadowed along the bottom and right, with seams and rivets."""
		for y in range(y0, y1 + 1):
			for x in range(x0, x1 + 1):
				c = base
				if y == y0 or x == x0:
					c = shade(base, 1.3)
				elif y == y1 or x == x1:
					c = shade(base, 0.6)
				elif seams and (x - x0) % seams == 0:
					c = shade(base, 0.75)
				elif ((x * 7 + y * 13) % 23) == 0:
					c = shade(base, 0.92)
				self.solid(x, y, c, material)
		if rivets and x1 - x0 >= 4 and y1 - y0 >= 3:
			for x in range(x0 + 2, x1 - 1, 6):
				self.solid(x, y0 + 1, STEEL_LIGHT, material)
				if y1 - y0 >= 5:
					self.solid(x, y1 - 1, STEEL_DARK, material)

	def interior(self, x0, y0, x1, y1):
		"""The wall seen inside: dark panels in a grid, with a lit strip along the top."""
		for y in range(y0, y1 + 1):
			for x in range(x0, x1 + 1):
				c = PANEL
				if (x - x0) % 16 == 0 or (y - y0) % 14 == 0:
					c = PANEL_DARK
				elif (x - x0) % 16 == 1 or (y - y0) % 14 == 1:
					c = shade(PANEL, 1.25)
				self.back(x, y, c)
		for x in range(x0 + 2, x1 - 1):
			self.back(x, y0 + 2, mix(WHITE, CYAN, 0.3) if (x // 3) % 2 == 0 else CYAN_DIM)

	def floor_slab(self):
		for y in range(self.floor, self.h):
			for x in range(self.w):
				c = CONCRETE_C if y > self.floor else shade(CONCRETE_C, 1.25)
				if ((x * 5 + y * 11) % 17) == 0:
					c = shade(c, 0.85)
				self.solid(x, y, c, CONCRETE)
		# A hazard stripe at the doorway.
		for x in range(self.w - 18, self.w):
			self.solid(x, self.floor, HAZARD_Y if ((x // 3) % 2) == 0 else HAZARD_K, CONCRETE)

	def walls(self, top, door_height=46):
		"""A wall at each end from the roof down to the floor, the right one with its doorway."""
		self.plate(0, top, 5, self.floor - 1, STEEL, seams=0)
		self.plate(self.w - 6, top, self.w - 1, self.floor - 1, STEEL, seams=0)
		door_top = self.floor - door_height
		for y in range(door_top, self.floor):
			for x in range(self.w - 6, self.w):
				self.clear(x, y)
		# The door frame: lit edges, and a hazard-striped lintel.
		self.plate(self.w - 7, door_top - 4, self.w - 1, door_top - 1, STEEL_DARK, seams=0, rivets=False)
		for x in range(self.w - 7, self.w):
			self.solid(x, door_top - 2, HAZARD_Y if ((x + door_top) // 2) % 2 == 0 else HAZARD_K)
		for y in range(door_top, self.floor):
			self.back(self.w - 7, y, CYAN if y % 4 else CYAN_DIM)
		return door_top

	def brain_alcove(self):
		"""The armoured alcove round the brain's chamber (10 to 29 across, the 20 rows above the floor), cut open by the engine."""
		x0, x1 = 7, 32
		top = self.floor - 24
		self.plate(x0, top, x1, top + 3, STEEL_DARK, seams=0)
		self.plate(x0, top + 4, x0 + 2, self.floor - 1, STEEL_DARK, seams=0, rivets=False)
		self.plate(x1 - 2, top + 4, x1, self.floor - 1, STEEL_DARK, seams=0, rivets=False)
		# Inside: a lit cradle the brain sits in.
		self.back_box(x0 + 3, top + 4, x1 - 3, self.floor - 1, PANEL_DARK)
		for x in range(x0 + 4, x1 - 3):
			self.back(x, self.floor - 1, mix(CYAN, WHITE, 0.4) if x % 2 else CYAN_DIM)
			self.back(x, top + 5, CYAN_DIM)
		for y in range(top + 6, self.floor - 1, 3):
			self.back(x0 + 3, y, CYAN)
			self.back(x1 - 3, y, CYAN)
		# A warning light over it.
		self.solid((x0 + x1) // 2, top - 1, RED)

	def tank(self, cx, top, bottom, radius, liquid, liquid_dim, figure=False):
		"""A glass tank of glowing liquid, seen inside (background)."""
		for y in range(top, bottom + 1):
			for dx in range(-radius, radius + 1):
				x = cx + dx
				edge = abs(dx) == radius
				t = (y - top) / max(1, bottom - top)
				if edge:
					c = STEEL_LIGHT if dx < 0 else STEEL_DARK
				else:
					c = mix(liquid, liquid_dim, abs(dx) / radius * 0.8)
					if (y + dx * 3) % 9 == 0:
						c = mix(c, WHITE, 0.5)  # Bubbles.
					if dx == -radius + 2:
						c = mix(c, WHITE, 0.35)  # The shine on the glass.
				self.back(x, y, c)
		# A cap on top and a base below.
		for dx in range(-radius - 1, radius + 2):
			self.back(cx + dx, top - 1, STEEL)
			self.back(cx + dx, top - 2, STEEL_DARK)
			self.back(cx + dx, bottom + 1, STEEL)
			self.back(cx + dx, bottom + 2, STEEL_DARK)
		if figure:
			# A clone growing inside: a dark shape.
			mid = (top + bottom) // 2
			for y in range(mid - 9, mid + 10):
				half = 1 if y < mid - 5 else (2 if y < mid + 2 else 1)
				if y == mid - 9 or y == mid - 5:
					half = 1
				for dx in range(-half, half + 1):
					self.back(cx + dx, y, liquid_dim)
			for dx in (-2, 2):
				self.back(cx + dx, mid + 10, liquid_dim)

	def pipe_v(self, x, y0, y1, color=STEEL):
		for y in range(y0, y1 + 1):
			self.back(x, y, shade(color, 1.2))
			self.back(x + 1, y, color)
			self.back(x + 2, y, shade(color, 0.6))

	def pipe_h(self, x0, x1, y, color=STEEL):
		for x in range(x0, x1 + 1):
			self.back(x, y, shade(color, 1.2))
			self.back(x, y + 1, color)
			self.back(x, y + 2, shade(color, 0.6))

	def save(self, name):
		save(self.fg, f"Scenes/Objects/Colony/{name}FG.png")
		save(self.bg, f"Scenes/Objects/Colony/{name}BG.png")
		save_materials(self.mat, self.w, self.h, f"Scenes/Objects/Colony/{name}Mat.png")


def clone_barracks():
	"""A long prefab hangar under an arched roof: clone vats glowing green inside, a lit doorway at the right."""
	b = Building(240, 132)
	b.floor_slab()
	roof_base = 46
	# The arched roof: a half ellipse of plates, ribbed.
	cx, rx, ry = b.w / 2, b.w / 2, 40
	for x in range(b.w):
		dx = (x + 0.5 - cx) / rx
		if abs(dx) > 1:
			continue
		top = int(roof_base - ry * math.sqrt(1 - dx * dx))
		for y in range(top, roof_base + 1):
			if y - top < 5 or y > roof_base - 2:
				c = STEEL if y - top > 0 else STEEL_LIGHT
				if y - top == 4 or y == roof_base:
					c = STEEL_DARK
				if x % 20 == 0:
					c = shade(c, 0.7)  # A rib.
				b.solid(x, y, c)
	# Inside the arch: the wall seen through.
	for x in range(6, b.w - 6):
		dx = (x + 0.5 - cx) / rx
		top = int(roof_base - ry * math.sqrt(max(0.0, 1 - dx * dx))) + 5
		for y in range(top, b.floor):
			b.back(x, y, PANEL if (x % 16 and (y - top) % 14) else PANEL_DARK)
	b.interior(6, roof_base + 1, b.w - 7, b.floor - 1)
	door_top = b.walls(roof_base)
	# A catwalk along the back, and the vats under it.
	for x in range(40, b.w - 40):
		b.back(x, roof_base + 18, STEEL_LIGHT)
		b.back(x, roof_base + 19, STEEL_DARK)
		if x % 6 == 0:
			for y in range(roof_base + 20, roof_base + 25):
				b.back(x, y, STEEL_DARK)
	for i, cx_tank in enumerate((60, 92, 124, 156)):
		b.tank(cx_tank, roof_base + 30, b.floor - 6, 8, GREEN, GREEN_DIM, figure=True)
		b.pipe_v(cx_tank - 1, roof_base + 20, roof_base + 27)
	# A console by the door, lit.
	b.back_box(b.w - 50, b.floor - 16, b.w - 38, b.floor - 1, STEEL_DARK)
	for x in range(b.w - 48, b.w - 40):
		b.back(x, b.floor - 13, CYAN if x % 2 else GREEN)
		b.back(x, b.floor - 10, ORANGE if x % 3 == 0 else CYAN_DIM)
	b.brain_alcove()
	# On the roof: an antenna mast with a red light, and a stencilled number on the arch.
	for y in range(0, 10):
		b.solid(176, y + 4, STEEL_DARK)
	b.solid(176, 3, RED)
	for x in range(172, 181):
		b.solid(x, 13, STEEL)
	for x, y in ((100, 22), (101, 22), (102, 22), (100, 23), (100, 24), (101, 24), (102, 24), (102, 25), (100, 26), (101, 26), (102, 26)):
		b.solid(x, y, HAZARD_Y)
	b.save("CloneBarracks")
	return b, door_top


def ore_extractor():
	"""A squat pump house with a drilling derrick on its roof, the drill string going down through it into the ground."""
	b = Building(96, 112)
	b.floor_slab()
	roof = b.floor - 56
	b.interior(6, roof + 1, b.w - 7, b.floor - 1)
	b.plate(0, roof - 4, b.w - 1, roof + 1, STEEL, seams=16)
	b.walls(roof - 4)
	# Hazard paint along the roof's edge.
	for x in range(b.w):
		b.solid(x, roof - 4, HAZARD_Y if (x // 4) % 2 == 0 else HAZARD_K)
	# The derrick: a tapering lattice tower up to the crown block.
	base_l, base_r, top_y = 22, 74, 2
	top_l, top_r = 42, 54
	for y in range(top_y, roof - 4):
		t = (y - top_y) / (roof - 4 - top_y)
		l = int(top_l + (base_l - top_l) * t)
		r = int(top_r + (base_r - top_r) * t)
		b.solid(l, y, STEEL_DARK)
		b.solid(r, y, STEEL_DARK)
		b.solid(l + 1, y, STEEL)
		b.solid(r - 1, y, STEEL)
		if (y - top_y) % 10 == 0:
			for x in range(l, r + 1):
				b.solid(x, y, STEEL)
		else:
			# The cross bracing.
			k = (y - top_y) % 10
			span = r - l
			b.solid(l + int(span * k / 10), y, STEEL_DARK)
			b.solid(r - int(span * k / 10), y, STEEL_DARK)
	b.plate(top_l - 4, top_y - 2, top_r + 4, top_y + 2, ORANGE, seams=0, rivets=False)
	b.solid(48, 0, RED)
	# The drill string, seen inside: down from the roof into the floor, with a pump head working it.
	for y in range(roof + 2, b.h):
		b.back(47, y, STEEL_LIGHT)
		b.back(48, y, STEEL)
		b.back(49, y, STEEL_DARK)
	b.back_box(40, roof + 10, 57, roof + 22, ORANGE)
	b.back_box(41, roof + 11, 56, roof + 12, shade(ORANGE, 1.3))
	b.back_box(41, roof + 21, 56, roof + 22, shade(ORANGE, 0.6))
	for x in range(43, 55, 3):
		b.back(x, roof + 16, HAZARD_K)
	# Tanks of ore slurry, and the pipe to them.
	b.tank(70, roof + 18, b.floor - 4, 6, (200, 150, 60), (110, 70, 30))
	b.pipe_h(50, 64, roof + 26)
	b.brain_alcove()
	b.save("OreExtractor")
	return b


def fusion_generator():
	"""A tall reactor under a glass dome, its core glowing cyan, cooling fins up its sides."""
	b = Building(92, 120)
	b.floor_slab()
	dome_base = 44
	cx, rx, ry = b.w / 2, b.w / 2 - 4, 40
	# The dome: a shell of glass on metal ribs; inside, the core.
	for x in range(b.w):
		dx = (x + 0.5 - cx) / rx
		if abs(dx) > 1:
			continue
		top = int(dome_base - ry * math.sqrt(1 - dx * dx))
		for y in range(top, dome_base + 1):
			if y - top < 3:
				rib = int(x - cx) % 12 == 0
				b.solid(x, y, STEEL if rib else mix(GLASS_C, WHITE, 0.3 if y == top else 0.0), METAL if rib else GLASS)
	for x in range(b.w):
		dx = (x + 0.5 - cx) / rx
		if abs(dx) > 1:
			continue
		top = int(dome_base - ry * math.sqrt(1 - dx * dx)) + 3
		for y in range(top, dome_base + 1):
			d = math.hypot((x - cx) / 22.0, (y - (dome_base - 12)) / 16.0)
			c = mix(WHITE, CYAN, min(1, d * 1.2)) if d < 1 else mix(CYAN_DIM, PANEL_DARK, min(1, (d - 1) * 1.5))
			b.back(x, y, c)
	b.plate(0, dome_base, b.w - 1, dome_base + 5, STEEL, seams=10)
	b.interior(6, dome_base + 6, b.w - 7, b.floor - 1)
	b.walls(dome_base, door_height=44)
	# Cooling fins on the walls, lit cyan between.
	for y in range(dome_base + 8, b.floor - 50, 5):
		for x in range(0, 6):
			b.solid(x, y, STEEL_DARK)
			b.solid(b.w - 1 - x, y, STEEL_DARK)
		b.solid(2, y + 2, CYAN)
		b.solid(b.w - 3, y + 2, CYAN)
	# The core's column down into the floor, and the coolant pipes.
	b.back_box(40, dome_base + 6, 51, b.floor - 1, STEEL_DARK)
	for y in range(dome_base + 6, b.floor):
		b.back(45, y, CYAN if y % 3 else WHITE)
		b.back(46, y, CYAN)
	b.pipe_v(36, dome_base + 6, b.floor - 1, (90, 100, 120))
	b.pipe_v(53, dome_base + 6, b.floor - 1, (90, 100, 120))
	for y in range(dome_base + 14, b.floor - 4, 10):
		b.back_box(58, y, 70, y + 4, STEEL_DARK)
		for x in range(59, 70, 2):
			b.back(x, y + 2, CYAN_DIM)
	b.brain_alcove()
	b.save("FusionGenerator")
	return b


def lumber_mill():
	"""A prefab shed under a sloping roof: a log rack and a laser-saw bench inside, a vent on the roof."""
	b = Building(108, 108)
	b.floor_slab()
	high, low = 30, 44
	for x in range(b.w):
		top = int(high + (low - high) * x / (b.w - 1))
		for y in range(top, top + 6):
			c = STEEL if y > top else STEEL_LIGHT
			if y == top + 5:
				c = STEEL_DARK
			if x % 14 == 0:
				c = shade(c, 0.75)
			b.solid(x, y, c)
		for y in range(top + 6, b.floor):
			b.back(x, y, PANEL)
	b.interior(6, low + 6, b.w - 7, b.floor - 1)
	for x in range(6, b.w - 6):
		top = int(high + (low - high) * x / (b.w - 1)) + 6
		for y in range(top, low + 6):
			b.back(x, y, PANEL_DARK if x % 16 == 0 else PANEL)
	b.walls(high, door_height=46)
	# A green stripe along the eaves: forestry colours.
	for x in range(b.w):
		top = int(high + (low - high) * x / (b.w - 1))
		b.solid(x, top + 3, (60, 150, 70))
	# The vent on the roof.
	b.plate(70, 22, 80, 40, STEEL_DARK, seams=0, rivets=False)
	for y in range(24, 38, 3):
		for x in range(72, 79):
			b.solid(x, y, PANEL_DARK)
	# Inside: a rack of logs at the back...
	for row, y in enumerate(range(b.floor - 6, b.floor - 30, -6)):
		for x0 in range(38 + (row % 2) * 4, 70, 9):
			for dy in range(5):
				for dx in range(8):
					c = WOOD if 0 < dy < 4 else WOOD_DARK
					if dx == 0 or dx == 7:
						c = WOOD_LIGHT if dx == 7 else WOOD_DARK
					if dx == 7 and dy == 2:
						c = (200, 160, 110)
					b.back(x0 + dx, y + dy, c)
		for x in range(36, 72):
			b.back(x, y + 5, STEEL_DARK)
	for x in (36, 71):
		for y in range(b.floor - 30, b.floor):
			b.back(x, y, STEEL)
	# ...and the saw bench by the door: a laser emitter over a cutting table, a red line where it cuts.
	b.back_box(76, b.floor - 12, 96, b.floor - 9, STEEL)
	b.back_box(78, b.floor - 8, 79, b.floor - 1, STEEL_DARK)
	b.back_box(93, b.floor - 8, 94, b.floor - 1, STEEL_DARK)
	b.back_box(84, b.floor - 30, 90, b.floor - 26, STEEL_DARK)
	b.back(87, b.floor - 25, RED)
	for y in range(b.floor - 24, b.floor - 12):
		b.back(87, y, RED if y % 2 else (255, 130, 110))
	for x in range(80, 95):
		b.back(x, b.floor - 13, WOOD_LIGHT)
	b.brain_alcove()
	b.save("LumberMill")
	return b


# ---------------------------------------------------------------------------
# The forester: the civilian's body in hi-vis orange with a yellow hard hat and green work trousers.

CIVILIAN = os.path.join(BASE, "Actors/Civilians/Civilian")
HIVIS = [(70, 26, 8), (150, 60, 16), (214, 104, 28), (244, 146, 50), (252, 196, 110)]
TROUSERS = [(18, 30, 20), (34, 56, 36), (52, 82, 52), (74, 110, 70), (100, 140, 92)]
HAT = [(110, 80, 10), (190, 150, 20), (236, 200, 50), (250, 230, 120)]


def load(path):
	img = Image.open(path).convert("RGBA")
	px = img.load()
	for y in range(img.height):
		for x in range(img.width):
			if px[x, y][:3] == (255, 0, 255):
				px[x, y] = (0, 0, 0, 0)
	return img


def is_skin(rgb):
	r, g, b = rgb
	return r > 150 and r > g + 20 and g > b and r - b > 50


def recolour(img, ramp, keep_skin=True):
	"""The picture's brightness mapped onto a ramp, its skin kept."""
	out = img.copy()
	px = out.load()
	for y in range(out.height):
		for x in range(out.width):
			r, g, b, a = px[x, y]
			if a == 0 or (keep_skin and is_skin((r, g, b))):
				continue
			lum = 0.3 * r + 0.59 * g + 0.11 * b
			t = min(max(lum / 200.0, 0.0), 1.0)
			px[x, y] = ramp[min(int(round(t * (len(ramp) - 1))), len(ramp) - 1)] + (255,)
	return out


def forester():
	out = "Actors/Civilians/Forester/"
	torso = recolour(load(os.path.join(CIVILIAN, "CivilianTorsoA.png")), HIVIS)
	# A reflective band across the vest.
	px = torso.load()
	for x in range(torso.width):
		if px[x, 9][3]:
			px[x, 9] = (230, 236, 230, 255)
	save(torso, out + "ForesterTorsoA.png")
	head = load(os.path.join(CIVILIAN, "CivilianHeadA.png"))
	hp = head.load()
	# The hair under a hard hat, with a brim out over the face.
	top = None
	for y in range(head.height):
		for x in range(head.width):
			r, g, b, a = hp[x, y]
			if a and not is_skin((r, g, b)) and (r + g + b) > 60 and r >= g:
				lum = (r + g + b) / 3
				hp[x, y] = HAT[min(int(lum / 40), len(HAT) - 1)] + (255,)
				top = y if top is None else min(top, y)
	if top is not None:
		brim = top + 3
		for x in range(head.width):
			if any(hp[x, y][3] for y in range(top, brim + 1)):
				hp[x, brim] = HAT[1] + (255,)
		for x in range(head.width - 1, -1, -1):
			if hp[x, brim][3]:
				if x + 1 < head.width:
					hp[x + 1, brim] = HAT[1] + (255,)
				break
	save(head, out + "ForesterHeadA.png")
	for part, ramp in (("ArmFGA", HIVIS), ("ArmBGA", HIVIS), ("LegFGA", TROUSERS), ("LegBGA", TROUSERS)):
		for f in range(5):
			save(recolour(load(os.path.join(CIVILIAN, f"Civilian{part}{f:03d}.png")), ramp), out + f"Forester{part}{f:03d}.png")


# ---------------------------------------------------------------------------
# Small things: the laser saw and the log.

def grid(rows, colors):
	h = len(rows)
	w = max(len(r) for r in rows)
	img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
	px = img.load()
	for y, row in enumerate(rows):
		for x, ch in enumerate(row):
			if ch not in ". ":
				px[x, y] = colors[ch] + (255,)
	return img


SMALL = {
	"k": (24, 26, 30), "d": STEEL_DARK, "s": STEEL, "l": STEEL_LIGHT, "o": ORANGE, "y": HAZARD_Y,
	"r": RED, "R": (255, 140, 120), "c": CYAN_DIM,
	"b": WOOD_DARK, "n": WOOD, "N": WOOD_LIGHT, "j": (204, 164, 112), "g": (60, 40, 22),
}

# Held by the grip at the left; the emitter at the right, its lens red (lit when it runs).
LASER_SAW = [
	"....kkkkkkkkk.......",
	"...kooooooosskkk....",
	"..kdsssssssssddlkk..",
	"kkdsyysssssssddlrrk.",
	"kddsyysssssssddlrrk.",
	"..kdssssssssddddkk..",
	"..kdkk.kkkkkkkkk....",
	"..kdk...............",
	"..kkk...............",
]
LASER_SAW_ON = [row.replace("rr", "RR") for row in LASER_SAW]

# Its power cell, clipped under the body.
LASER_SAW_CELL = [
	"kkkk",
	"kcck",
	"kkkk",
]

# A length of trunk, its cut ends showing the rings.
LOG = [
	".bnnnnnnnnnnnb.",
	"bNnnnNnnnnNnnjb",
	"bnnNnnnnNnnnnNj",
	"bnnnnnnNnnnnnjb",
	".bbnbbbbbnbbbb.",
]


def small_things():
	save(grid(LASER_SAW, SMALL), "Devices/Tools/LaserSaw/LaserSaw000.png")
	save(grid(LASER_SAW_ON, SMALL), "Devices/Tools/LaserSaw/LaserSaw001.png")
	save(grid(LASER_SAW_CELL, SMALL), "Devices/Tools/LaserSaw/LaserSawCell.png")
	save(grid(LOG, SMALL), "Devices/Colony/Log.png")


clone_barracks()
ore_extractor()
fusion_generator()
lumber_mill()
forester()
small_things()
print("colony sprites done")
