#!/usr/bin/env python3
"""The bestiary's placeholder bodies (the medieval factions expansion, Bestiary I): six creatures built from the rigs already here, so
their frames, joints, limb paths and AI all still fit, until their own art is drawn.

Re-run from anywhere:
    python3 Data/Base.rte/Actors/Creatures/Tools/make_bestiary.py

On four legs (the Hound's rig, Hound/Hound.ini, scaled and recoloured): the Dire Wolf (1.3x, black), the Boar (1.1x, bristly brown),
the Bear (1.8x, brown) and the Giant Rat (0.5x, grey-brown). For each this writes <Creature>/<Creature>Body.ini (the legs and an ACrab
"<Creature> Body" with every offset, limb path and reach scaled) and its sprites; what makes it the creature (its jaws, how it hunts, its
health) is in <Creature>/<Creature>.ini, by hand, on that body.

On two legs (the Civilian's rig through make_flame_fiend.py, as the Ogre): the Cave Troll (2x, blue-grey hide) and the Stone Golem (1.7x,
grey stone, its flesh and wounds swapped for stone and dents). Also the Golem's and the Troll's rock sprites (the stone gibs and the
boulder).

Sprites stay 8-bit paletted PNGs in the game palette, index 0 the transparent background (as make_placeholders.py).
"""

import glob
import os
import re

from PIL import Image, ImageDraw

import make_flame_fiend as rig

HERE = os.path.dirname(os.path.abspath(__file__))
CREATURES_DIR = os.path.normpath(os.path.join(HERE, ".."))
HOUND_DIR = os.path.join(CREATURES_DIR, "Hound")
DATA_DIR = rig.DATA_DIR


def nearest_index(palette, rgb):
	return rig.nearest_index(palette, rgb)


def ramp_colour(ramp, shade):
	"""The ramp's colour for a shade from 0 (darkest) to 1 (lightest), blended between its steps."""
	pos = shade * (len(ramp) - 1)
	i = min(int(pos), len(ramp) - 2)
	f = pos - i
	return tuple(int(ramp[i][k] * (1 - f) + ramp[i + 1][k] * f) for k in range(3))


def reshade(source, target, ramp, scale, eyes=None, outline=None):
	"""Writes the sprite at source to target with every colour's shade mapped onto ramp and the image scaled (nearest neighbour, so it
	stays crisp and paletted). Reddish pixels (the Hound's eyes) become eyes, if given; the darkest become outline, if given."""
	image = Image.open(source)
	palette = image.getpalette()[:768]
	data = list(image.get_flattened_data())
	shades = {}
	for index in set(data):
		if index == 0:
			continue
		r, g, b = palette[index * 3:index * 3 + 3]
		if (r, g, b) == (255, 0, 255):
			continue
		shades[index] = (0.299 * r + 0.587 * g + 0.114 * b) / 255, (r, g, b)
	remap = {}
	if shades:
		lo = min(s for s, _ in shades.values())
		hi = max(s for s, _ in shades.values())
		for index, (shade, (r, g, b)) in shades.items():
			if eyes and r > 120 and r > g * 2 and r > b * 2:
				remap[index] = nearest_index(palette, eyes)
				continue
			norm = (shade - lo) / max(hi - lo, 1e-6)
			if outline and norm < 0.08:
				remap[index] = nearest_index(palette, outline)
			else:
				remap[index] = nearest_index(palette, ramp_colour(ramp, norm))
	out = image.copy()
	out.putdata([remap.get(index, index) for index in data])
	if scale != 1:
		out = out.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))), Image.NEAREST)
		out.putpalette(palette)
	out.save(target, transparency=0)


def num(value, scale, places=1):
	"""A number scaled, written short."""
	number = value * scale
	return str(int(round(number))) if abs(number - round(number)) < 0.05 else ("%.*f" % (places, number))


def vec(x, y, scale, depth):
	tabs = "\t" * depth
	return "%sX = %s\n%sY = %s" % (tabs, num(x, scale), tabs, num(y, scale))


# The four-legged creatures: the Hound's rig at a scale, in a hide. walk: the walk path's TravelSpeed (the Hound's is 4.4).
CRABS = [
	dict(name="Dire Wolf", folder="DireWolf", prefix="DireWolf", scale=1.3, walk=5.0, jump=44, jumpSpeed=7.5,
		ramp=[(14, 12, 14), (34, 30, 34), (58, 52, 56), (84, 76, 78), (112, 104, 104), (150, 142, 140)], eyes=(240, 150, 40), outline=(8, 6, 8),
		header="The Dire Wolf's body: the Hound's rig a third bigger again, in black fur."),
	dict(name="Boar", folder="Boar", prefix="Boar", scale=1.1, walk=4.6, jump=24, jumpSpeed=6,
		ramp=[(26, 18, 12), (54, 38, 24), (86, 62, 38), (118, 88, 54), (150, 116, 74), (180, 146, 100)], eyes=(40, 30, 24), outline=(14, 10, 6),
		header="The Boar's body: the Hound's rig a tenth bigger, in bristly brown."),
	dict(name="Bear", folder="Bear", prefix="Bear", scale=1.8, walk=3.4, jump=30, jumpSpeed=6.5,
		ramp=[(24, 16, 10), (52, 36, 22), (84, 58, 34), (116, 82, 48), (146, 108, 66), (176, 138, 92)], eyes=(30, 22, 18), outline=(12, 8, 4),
		header="The Bear's body: the Hound's rig near twice the size, in brown fur."),
	dict(name="Giant Rat", folder="GiantRat", prefix="GiantRat", scale=0.5, walk=5.6, jump=20, jumpSpeed=6,
		ramp=[(30, 26, 24), (58, 50, 44), (90, 80, 70), (122, 110, 96), (150, 138, 122), (178, 166, 150)], eyes=(220, 40, 40), outline=(16, 14, 12),
		header="The Giant Rat's body: the Hound's rig at half size, in grey-brown fur."),
]

HOUND_SPRITES = ["BodyA", "LegHindFGA", "LegHindBGA", "LegFrontFGA", "LegFrontBGA"]


def crab_sprites(c):
	out = os.path.join(CREATURES_DIR, c["folder"])
	os.makedirs(out, exist_ok=True)
	for old in glob.glob(os.path.join(out, c["prefix"] + "*.png")):
		os.remove(old)
	count = 0
	for path in sorted(glob.glob(os.path.join(HOUND_DIR, "Hound*.png"))):
		name = os.path.basename(path).replace("Hound", c["prefix"], 1)
		reshade(path, os.path.join(out, name), c["ramp"], c["scale"], eyes=c["eyes"], outline=c["outline"])
		count += 1
	return count


def leg_block(c, which, fg):
	"""A Leg preset of the Hound's, scaled: which is "Hind" or "Front"."""
	s = c["scale"]
	name = c["name"]
	sprite = "Base.rte/Actors/Creatures/%s/%sLeg%s%sA.png" % (c["folder"], c["prefix"], which, "FG" if fg else "BG")
	if not fg:
		return """

AddEffect = Leg
	CopyOf = %s Leg %s FG
	PresetName = %s Leg %s BG
	SpriteFile = ContentFile
		FilePath = %s
	FrameCount = 4
	DrawAfterParent = 0""" % (name, which, name, which, sprite)
	return """

AddEffect = Leg
	PresetName = %s Leg %s FG
	Mass = %s
	HitsMOs = 1
	GetsHitByMOs = 1
	SpriteFile = ContentFile
		FilePath = %s
	FrameCount = 4
	SpriteOffset = Vector
%s
	AngularVel = 6
	EntryWound = AEmitter
		CopyOf = Wound Flesh Entry
	ExitWound = AEmitter
		CopyOf = Wound Flesh Exit
	AtomGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = Flesh
		Resolution = 2
		Depth = 0
	DeepCheck = 0
	JointStrength = %s
	JointStiffness = 0.5
	BreakWound = AEmitter
		CopyOf = Wound Bone Break
	ParentBreakWound = AEmitter
		CopyOf = Wound Bone Break
	JointOffset = Vector
%s
	DrawAfterParent = 1
	ExtendedOffset = Vector
%s
	ContractedOffset = Vector
%s
	IdleOffset = Vector
%s
	MoveSpeed = 0.4
	GibImpulseLimit = %s
	GibWoundLimit = %s
	GibSound = SoundContainer
		CopyOf = Flesh Limb Gib
	AddGib = Gib
		GibParticle = MOPixel
			CopyOf = Drop Blood
		Count = 4
		MinVelocity = 1
		MaxVelocity = 6
	AddGib = Gib
		GibParticle = MOSParticle
			CopyOf = Gib Flesh Tiny A
		Offset = Vector
			X = 0
			Y = 1
	AddGib = Gib
		GibParticle = MOSParticle
			CopyOf = Gib Bone Tiny A
		Offset = Vector
			X = 0
			Y = -1""" % (name, which, num(4, s * s), sprite, vec(-9, -8, s, 2), num(400, s * s), vec(0, -7, s, 2), vec(0, 12.5, s, 2),
		vec(0, 6, s, 2), vec(0, 10.6, s, 2), num(1125, s * s), max(2, int(round(4 * s))))


def crab_body(c):
	"""The <Creature>Body.ini: the legs and the body, the Hound's numbers scaled."""
	s = c["scale"]
	name = c["name"]
	lines = ["///////////////////////////////////////////////////////////////////////",
		"// " + c["header"],
		"// GENERATED by Base.rte/Actors/Creatures/Tools/make_bestiary.py - edit that and re-run. The creature itself is %s.ini." % c["prefix"]]
	text = "\n".join(lines)
	for which in ("Hind", "Front"):
		text += leg_block(c, which, True)
		text += leg_block(c, which, False)
	body = """

AddActor = ACrab
	PresetName = %(name)s Body
	Buyable = 0
	Mass = %(mass)s
	HitsMOs = 1
	GetsHitByMOs = 1
	IgnoresTeamHits = 0
	SpriteFile = ContentFile
		FilePath = Base.rte/Actors/Creatures/%(folder)s/%(prefix)sBodyA.png
	FrameCount = 2
	SpriteAnimMode = 4
	SpriteAnimDuration = 700
	SpriteOffset = Vector
%(spriteOffset)s
	EntryWound = AEmitter
		CopyOf = Wound Flesh Entry
	ExitWound = AEmitter
		CopyOf = Wound Flesh Exit
	AtomGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = Flesh
		Resolution = 4
		Depth = 0
	DeepCheck = 0
	BodyHitSound = SoundContainer
		CopyOf = Flesh Body Blunt Hit
	PainSound = SoundContainer
		CopyOf = Flesh Limb Impact
	DeathSound = SoundContainer
		CopyOf = Flesh Torso Impact
	PassengerSlots = 0
	Swims = Surface
	SafeFallHeight = %(fall)s
	LegJumpHeight = %(jump)s
	LegJumpSpeed = %(jumpSpeed)s
	AimRangeUpperLimit = 1
	AimRangeLowerLimit = 1
	MaxHealth = 100
	Health = 100
	Organic = 1
	ImpulseDamageThreshold = %(impulse)s
	Perceptiveness = 0.9
	CharHeight = %(charHeight)s
	LFGLeg = Leg
		CopyOf = %(name)s Leg Hind FG
		ParentOffset = Vector
%(lfg)s
	LBGLeg = Leg
		CopyOf = %(name)s Leg Hind BG
		ParentOffset = Vector
%(lbg)s
	RFGLeg = Leg
		CopyOf = %(name)s Leg Front FG
		ParentOffset = Vector
%(rfg)s
	RBGLeg = Leg
		CopyOf = %(name)s Leg Front BG
		ParentOffset = Vector
%(rbg)s
	LFootGroup = AtomGroup
		CopyOf = %(foot)s
	RFootGroup = AtomGroup
		CopyOf = %(foot)s
	LStandLimbPath = LimbPath
		PresetName = %(name)s Stand Path Left
		StartOffset = Vector
%(standL)s
		StartSegCount = 0
		TravelSpeed = 0.5
		PushForce = %(push)s
	LWalkLimbPath = LimbPath
		PresetName = %(name)s Walk Path
		StartOffset = Vector
%(walkStart)s
		StartSegCount = 2
		AddSegment = Vector
%(seg1)s
		AddSegment = Vector
%(seg2)s
		AddSegment = Vector
%(seg3)s
		AddSegment = Vector
%(seg4)s
		AddSegment = Vector
%(seg5)s
		EndSegCount = 1
		TravelSpeed = %(walk)s
		PushForce = %(push)s
	LDislodgeLimbPath = LimbPath
		PresetName = %(name)s Dislodge Path Left
		StartOffset = Vector
%(dislodgeL)s
		StartSegCount = 0
		AddSegment = Vector
%(dislodgeSeg)s
		TravelSpeed = 2.8
		PushForce = %(dislodgePush)s
	RStandLimbPath = LimbPath
		CopyOf = %(name)s Stand Path Left
		PresetName = %(name)s Stand Path Right
		StartOffset = Vector
%(standR)s
	RWalkLimbPath = LimbPath
		CopyOf = %(name)s Walk Path
	RDislodgeLimbPath = LimbPath
		CopyOf = %(name)s Dislodge Path Left
		PresetName = %(name)s Dislodge Path Right
		StartOffset = Vector
%(dislodgeR)s
	GibImpulseLimit = %(gibImpulse)s
	GibWoundLimit = %(gibWounds)s
	GibSound = SoundContainer
		CopyOf = Flesh Torso Gib
	AddGib = Gib
		GibParticle = MOPixel
			CopyOf = Drop Blood
		Count = %(blood)s
		MinVelocity = 2
		MaxVelocity = 10
	AddGib = Gib
		GibParticle = MOSParticle
			CopyOf = Blood Spray Particle
		Count = 2
		MinVelocity = 1
		MaxVelocity = 5
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Flesh Small A
		Offset = Vector
%(gibA)s
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Flesh Small B
		Offset = Vector
			X = 0
			Y = -1
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Flesh Small C
		Offset = Vector
%(gibC)s
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Bone Small A
		Offset = Vector
%(gibD)s
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Bone Small B
		Offset = Vector
			X = 0
			Y = 1
	AddGib = Gib
		GibParticle = MOSRotating
			CopyOf = Gib Bone Small C
		Offset = Vector
%(gibF)s
	AddGib = Gib
		GibParticle = MOSParticle
			CopyOf = Gib Flesh Tiny A
		Count = 3
		Offset = Vector
			X = -2
			Y = -4
	AddGib = Gib
		GibParticle = MOSParticle
			CopyOf = Gib Flesh Micro A
		Count = 4
		Offset = Vector
			X = 2
			Y = 4
	AddGib = Gib
		GibParticle = MOSParticle
			CopyOf = Gib Bone Tiny A
		Count = 2
""" % dict(name=name, folder=c["folder"], prefix=c["prefix"], mass=num(40, s * s), spriteOffset=vec(-12, -12, s, 2),
		fall=num(150, s), jump=c["jump"], jumpSpeed=c["jumpSpeed"], impulse=num(1278, s * s), charHeight=num(55, s),
		lfg=vec(-7, 2, s, 3), lbg=vec(-6, 2, s, 3), rfg=vec(7, 2, s, 3), rbg=vec(6, 2, s, 3),
		foot="Livestock Foot Small" if s < 0.8 else "Livestock Foot Large",
		standL=vec(-2.5, 10.6, s, 3), standR=vec(2.5, 10.6, s, 3), push=num(2130, s * s), walkStart=vec(-2.5, -1, s, 3),
		seg1=vec(7.5, 7.8, s, 3), seg2=vec(0, 3.8, s, 3), seg3=vec(-5, 0, s, 3), seg4=vec(-5, 0, s, 3), seg5=vec(2.5, -3.8, s, 3),
		walk=c["walk"], dislodgeL=vec(-0.5, -7.5, s, 3), dislodgeR=vec(0.5, -7.5, s, 3), dislodgeSeg=vec(0, 6.2, s, 3),
		dislodgePush=num(7810, s * s), gibImpulse=num(4500, s * s), gibWounds=max(3, int(round(8 * s))), blood=max(3, int(round(10 * s))),
		gibA=vec(-14, 1, s, 3), gibC=vec(14, 1, s, 3), gibD=vec(-7, -1, s, 3), gibF=vec(7, -1, s, 3))
	text += body
	out = os.path.join(CREATURES_DIR, c["folder"], c["prefix"] + "Body.ini")
	with open(out, "w", encoding="utf-8", newline="\n") as handle:
		handle.write(text)
	return out


# The two-legged creatures, on make_flame_fiend's rig as the Ogre is.
TROLL_RAMP = [(16, 18, 26), (32, 36, 50), (50, 56, 74), (72, 80, 98), (96, 106, 122), (122, 132, 146), (150, 158, 170), (182, 188, 196)]
GOLEM_RAMP = [(28, 26, 24), (46, 44, 42), (64, 62, 58), (82, 80, 76), (100, 98, 92), (120, 118, 110), (142, 140, 130), (166, 164, 152)]


def two_legged(folder, ini, scale, prefix, sprite_prefix, header, ramp):
	rig.OUT_DIR = os.path.join(CREATURES_DIR, folder)
	rig.OUT_INI = os.path.join(rig.OUT_DIR, ini)
	rig.SCALE = scale
	rig.PREFIX = prefix
	rig.SPRITE_PREFIX = sprite_prefix
	rig.HEADER = header
	rig.RAMP = ramp
	rig.WRITTEN.clear()
	rig.main()
	return rig.OUT_INI


def stonify(path):
	"""The Golem's body made of stone: flesh to stone, wounds to dents (no blood), gibs to rock, and no longer a living thing."""
	with open(path, encoding="utf-8") as handle:
		text = handle.read()
	text = text.replace("CopyOf = Flesh\n", "CopyOf = Stone\n")
	text = re.sub(r"CopyOf = Wound (Flesh|Bone) \w+( \w+)?", "CopyOf = Dent Metal No Spark", text)
	text = text.replace("Organic = 1", "Organic = 0")
	text = re.sub(r"CopyOf = Gib (?:Flesh|Bone) Small ([A-Z])", lambda m: "CopyOf = Gib Stone Small " + "ABC"[(ord(m.group(1)) - 65) % 3], text)
	# (Blood drops are MOPixels; the stone dust is an MOSParticle, so the gib's class goes with it.)
	text = re.sub(r"GibParticle = MOPixel(\n\s*)CopyOf = Drop Blood", r"GibParticle = MOSParticle\1CopyOf = Gib Stone Tiny A", text)
	text = re.sub(r"CopyOf = (Gib (?:Flesh|Bone) (?:Tiny|Micro) A|Blood Spray Particle)", "CopyOf = Gib Stone Tiny A", text)
	text = text.replace("CopyOf = Flesh Limb Gib", "CopyOf = Metal Door Gib")
	with open(path, "w", encoding="utf-8", newline="\n") as handle:
		handle.write(text)


def new_sprite(size):
	palette = Image.open(os.path.join(HOUND_DIR, "HoundBodyA000.png")).getpalette()[:768]
	image = Image.new("P", size, 0)
	image.putpalette(palette)
	return image, ImageDraw.Draw(image), lambda rgb: nearest_index(palette, rgb)


def rock(target, size, seed):
	"""A lump of grey stone, size x size: a rough polygon with a lit top-left and a dark underside."""
	image, draw, ink = new_sprite((size, size))
	import random
	rnd = random.Random(seed)
	points = []
	for i in range(7):
		angle = i * 6.283 / 7 + rnd.uniform(-0.2, 0.2)
		r = size * 0.5 * rnd.uniform(0.7, 0.98)
		points.append((size * 0.5 + r * __import__("math").cos(angle), size * 0.5 + r * __import__("math").sin(angle)))
	draw.polygon(points, fill=ink((96, 94, 88)), outline=ink((36, 34, 32)))
	for x in range(size):
		for y in range(size):
			if image.getpixel((x, y)) == ink((96, 94, 88)):
				if x + y < size * 0.8:
					image.putpixel((x, y), ink((130, 128, 120)))
				elif x + y > size * 1.3:
					image.putpixel((x, y), ink((62, 60, 56)))
	image.save(target, transparency=0)


# ---------------------------------------------------------------------------------------------------------------------------------------
# Bestiary II: the Basilisk on the Hound's rig; the Giant Beetle on the Scorpion's; the Giant Centipede on the Spider's legs with a long body
# of its own; the Myconid drawn whole; the Treant, Minotaur and Cyclops on the Civilian's rig.

CRABS.append(dict(name="Basilisk", folder="Basilisk", prefix="Basilisk", scale=1.4, walk=3.8, jump=30, jumpSpeed=6,
	ramp=[(14, 24, 12), (28, 48, 22), (46, 76, 34), (68, 104, 46), (96, 134, 60), (130, 164, 84)], eyes=(250, 230, 60), outline=(8, 14, 6),
	header="The Basilisk's body: the Hound's rig 1.4x, in dull green scales."))

BEETLE_RAMP = [(10, 10, 18), (22, 24, 40), (36, 40, 64), (52, 58, 88), (70, 80, 110), (96, 110, 136)]
CENTIPEDE_RAMP = [(30, 10, 6), (62, 22, 10), (96, 36, 14), (130, 54, 20), (160, 76, 30), (190, 104, 48)]
TREANT_RAMP = [(24, 18, 10), (44, 32, 16), (66, 48, 24), (88, 64, 32), (74, 92, 36), (98, 120, 48), (130, 150, 64), (170, 186, 96)]
MINOTAUR_RAMP = [(20, 12, 8), (44, 26, 16), (74, 44, 26), (104, 64, 36), (134, 86, 50), (160, 110, 68), (186, 140, 96), (210, 170, 128)]
CYCLOPS_RAMP = [(40, 26, 18), (74, 50, 34), (108, 76, 52), (140, 102, 70), (168, 128, 92), (192, 152, 114), (212, 176, 140), (228, 200, 168)]


def reshade_set(src_folder, src_prefix, folder, prefix, stems, ramp, scale=1.0, eyes=None, outline=None):
	"""Another creature's sprites (those whose names start src_prefix + a stem) reshaded into folder under prefix."""
	src = os.path.join(CREATURES_DIR, src_folder)
	out = os.path.join(CREATURES_DIR, folder)
	os.makedirs(out, exist_ok=True)
	count = 0
	for stem in stems:
		for path in sorted(glob.glob(os.path.join(src, src_prefix + stem + "*.png"))):
			name = os.path.basename(path).replace(src_prefix, prefix, 1)
			reshade(path, os.path.join(out, name), ramp, scale, eyes=eyes, outline=outline)
			count += 1
	return count


def centipede_body(target, frame):
	"""The Giant Centipede's body, 84 x 18: nine plates of red-brown chitin and a head with mandibles at the right. Frame 1 undulates."""
	width, height = 84, 18
	image, draw, ink = new_sprite((width, height))
	dark, mid, light, line = ink((40, 14, 8)), ink((110, 42, 16)), ink((160, 76, 30)), ink((16, 6, 4))
	for i in range(9):
		cx = 8 + i * 8
		dy = (1 if i % 2 == 0 else -1) if frame else 0
		draw.ellipse((cx - 5, 3 + dy, cx + 5, 14 + dy), fill=mid, outline=line)
		draw.line((cx - 3, 5 + dy, cx + 3, 5 + dy), fill=light)
		draw.line((cx - 3, 12 + dy, cx + 3, 12 + dy), fill=dark)
	draw.ellipse((73, 2, 83, 15), fill=dark, outline=line)
	draw.point((80, 6), fill=ink((250, 200, 40)))
	draw.line((82, 5, 83, 1), fill=line)
	draw.line((82, 12, 83, 16), fill=line)
	image.save(target, transparency=0)


def myconid_body(target, width, height, frame):
	"""A Myconid: a violet mushroom cap with pale spots over a pale stalk with two dark eyes. Frame 1 squashes the cap a pixel."""
	image, draw, ink = new_sprite((width, height))
	cap, cap_dark, spot, stalk, stalk_dark, line = ink((120, 60, 130)), ink((70, 30, 80)), ink((220, 190, 230)), ink((200, 190, 160)), ink((150, 140, 110)), ink((30, 14, 36))
	squash = 1 if frame else 0
	cap_bottom = int(height * 0.55)
	draw.rectangle((int(width * 0.32), int(cap_bottom * 0.6), int(width * 0.68), height - 1), fill=stalk, outline=line)
	draw.line((int(width * 0.34), height - 2, int(width * 0.66), height - 2), fill=stalk_dark)
	draw.ellipse((0, squash, width - 1, cap_bottom + squash), fill=cap, outline=line)
	draw.line((2, cap_bottom + squash - 1, width - 3, cap_bottom + squash - 1), fill=cap_dark)
	for sx, sy in ((width * 0.25, cap_bottom * 0.4), (width * 0.55, cap_bottom * 0.25), (width * 0.75, cap_bottom * 0.55)):
		draw.rectangle((int(sx), int(sy) + squash, int(sx) + max(1, width // 15), int(sy) + max(1, height // 13) + squash), fill=spot)
	eye_y = int(cap_bottom * 0.75)
	draw.point((int(width * 0.42), eye_y), fill=line)
	draw.point((int(width * 0.58), eye_y), fill=line)
	image.save(target, transparency=0)


def woodify(path):
	"""The Treant's body made of living wood: flesh to Wood, wounds to the dry kind (sap, not blood), gibs to planks and splinters (Shared/WoodGibs.ini)."""
	with open(path, encoding="utf-8") as handle:
		text = handle.read()
	text = text.replace("CopyOf = Flesh\n", "CopyOf = Wood\n")
	def wound(match):
		kind = match.group(2)
		return "CopyOf = Wound Dry Flesh " + ("Entry" if kind == "Entry" else "Exit" if kind == "Exit" else "Body")
	text = re.sub(r"CopyOf = Wound (Flesh|Bone) (\w+)( \w+)?", wound, text)
	text = re.sub(r"CopyOf = Gib (?:Flesh|Bone) Small ([A-Z])", lambda m: "CopyOf = Gib Wood Plank " + "ABC"[(ord(m.group(1)) - 65) % 3], text)
	text = re.sub(r"GibParticle = MOPixel(\n\s*)CopyOf = Drop Blood", r"GibParticle = MOSParticle\1CopyOf = Gib Wood Splinter", text)
	text = re.sub(r"CopyOf = (Gib (?:Flesh|Bone) (?:Tiny|Micro) A|Blood Spray Particle)", "CopyOf = Gib Wood Splinter", text)
	with open(path, "w", encoding="utf-8", newline="\n") as handle:
		handle.write(text)


def bestiary_two():
	count = reshade_set("Scorpion", "Scorpion", "GiantBeetle", "Beetle", ["BodyA", "LegFGA", "LegBGA"], BEETLE_RAMP, eyes=(200, 40, 20), outline=(4, 4, 10))
	count += reshade_set("GiantSpider", "Spider", "GiantCentipede", "Centipede", ["LegFGA", "LegBGA"], CENTIPEDE_RAMP, outline=(16, 6, 4))
	for frame in (0, 1):
		centipede_body(os.path.join(CREATURES_DIR, "GiantCentipede", "CentipedeBodyA%03d.png" % frame), frame)
		count += 1
	os.makedirs(os.path.join(CREATURES_DIR, "Myconid"), exist_ok=True)
	for frame in (0, 1):
		myconid_body(os.path.join(CREATURES_DIR, "Myconid", "MyconidBodyA%03d.png" % frame), 30, 26, frame)
		myconid_body(os.path.join(CREATURES_DIR, "Myconid", "MyconidSproutBodyA%03d.png" % frame), 18, 16, frame)
		count += 2
	print("wrote", count, "Bestiary II sprites")
	treant = two_legged("Treant", "TreantBody.ini", 2.0, "Treant", "Treant",
		["// The Treant's body: the Civilian's rig twice as big, in bark and moss; its flesh is Wood, its wounds dry, its gibs planks and splinters.",
		"// GENERATED by Base.rte/Actors/Creatures/Tools/make_bestiary.py - edit that and re-run. The Treant itself is Treant.ini."], TREANT_RAMP)
	woodify(treant)
	two_legged("Minotaur", "MinotaurBody.ini", 1.8, "Minotaur", "Minotaur",
		["// The Minotaur's body: the Civilian's rig 1.8x, in dark brown hide.",
		"// GENERATED by Base.rte/Actors/Creatures/Tools/make_bestiary.py - edit that and re-run. The Minotaur itself is Minotaur.ini."], MINOTAUR_RAMP)
	two_legged("Cyclops", "CyclopsBody.ini", 2.2, "Cyclops", "Cyclops",
		["// The Cyclops' body: the Civilian's rig 2.2x, in weathered tan skin.",
		"// GENERATED by Base.rte/Actors/Creatures/Tools/make_bestiary.py - edit that and re-run. The Cyclops itself is Cyclops.ini."], CYCLOPS_RAMP)
	rock(os.path.join(CREATURES_DIR, "Cyclops", "CyclopsBoulder.png"), 22, 6)
	print("wrote the Treant, Minotaur and Cyclops rigs and the Cyclops' boulder")


# ---------------------------------------------------------------------------------------------------------------------------------------
# Bestiary III, the fliers: winged bodies drawn whole (as make_placeholders.py draws the Wyvern), and the Shade, which has no wings.

def winged_body(target, frame, width, height, body, dark, wing, belly, eye=(250, 200, 40), horns=False, beak=False, tail=True):
	"""A winged creature facing right: a body with a lighter belly, a tail behind, a head on a short neck in front (horned or beaked,
	as asked) and two wings by the frame (0 up, 1 level, 2 down)."""
	image, draw, ink = new_sprite((width, height))
	B, D, W, L = ink(body), ink(dark), ink(wing), ink(belly)
	cx, cy = int(width * 0.45), int(height * 0.5)
	bw, bh = max(4, int(width * 0.3)), max(3, int(height * 0.26))
	spread = int(bw * 1.3)
	tips = [((cx - spread * 0.5, 0), (cx + spread * 0.4, 0)), ((cx - spread * 1.3, cy * 0.7), (cx + spread * 1.1, cy * 0.7)),
		((cx - spread * 0.6, height - 1), (cx + spread * 0.5, height - 1))][frame]
	for tip in tips:
		lean = max(3, width // 12) if tip[0] < cx else -max(3, width // 12)
		draw.polygon([(cx - 2, cy - 2), tip, (tip[0] + lean, tip[1] + max(2, height // 8)), (cx + 6, cy)], fill=W, outline=D)
	stroke = max(2, height // 10)
	if tail:
		draw.line((2, cy + bh * 0.6, cx - bw + 2, cy), fill=B, width=stroke)
		draw.polygon([(0, cy + bh * 0.6), (4, cy + bh * 0.2), (4, cy + bh)], fill=D)
	draw.ellipse((cx - bw, cy - bh, cx + bw, cy + bh), fill=B, outline=D)
	draw.ellipse((cx - bw * 0.7, cy, cx + bw * 0.7, cy + bh * 0.85), fill=L)
	hr = max(3, int(height * 0.14))
	hx, hy = cx + bw + hr, cy - int(height * 0.18)
	draw.line((cx + bw - 2, cy - 2, hx - hr, hy), fill=B, width=stroke)
	draw.ellipse((hx - hr, hy - hr, hx + hr, hy + hr), fill=B, outline=D)
	draw.polygon([(hx + hr - 1, hy - 1), (hx + hr + 4, hy + 1), (hx + hr - 1, hy + 2)], fill=ink((230, 190, 60)) if beak else B, outline=D)
	if horns:
		draw.polygon([(hx, hy - hr), (hx - 2, hy - hr - 5), (hx + 2, hy - hr + 1)], fill=L, outline=D)
	draw.point((hx + 1, hy - 1), fill=ink(eye))
	# Legs tucked under.
	draw.line((cx - bw * 0.4, cy + bh, cx - bw * 0.5, cy + bh + 4), fill=D, width=2)
	draw.line((cx + bw * 0.4, cy + bh, cx + bw * 0.5, cy + bh + 4), fill=D, width=2)
	image.save(target, transparency=0)


def shade_body(target, frame, width, height):
	"""A Shade: a hooded, tattered ghost of dark grey-blue with two pale eyes, its hem drifting by the frame."""
	image, draw, ink = new_sprite((width, height))
	body, dark, eye = ink((44, 48, 70)), ink((18, 20, 32)), ink((200, 220, 255))
	cx = width // 2
	draw.ellipse((cx - width * 0.35, 0, cx + width * 0.35, height * 0.5), fill=body, outline=dark)
	draw.polygon([(cx - width * 0.35, height * 0.3), (cx + width * 0.35, height * 0.3), (cx + width * 0.45, height * 0.8), (cx - width * 0.45, height * 0.8)], fill=body, outline=dark)
	hem = [(cx - width * 0.45, height * 0.8)]
	for i in range(5):
		x = cx - width * 0.45 + (width * 0.9) * (i + 0.5) / 5
		hem.append((x, height - 1 - ((i + frame) % 2) * 3))
		hem.append((x + width * 0.09, height * 0.8))
	draw.polygon(hem, fill=body, outline=dark)
	draw.point((cx - 2, int(height * 0.25)), fill=eye)
	draw.point((cx + 2, int(height * 0.25)), fill=eye)
	image.save(target, transparency=0)


FLIERS = [
	# folder, prefix, width, height, body, dark, wing, belly, eye, horns, beak, tail
	("Harpy", "Harpy", 44, 30, (150, 110, 80), (50, 30, 20), (110, 80, 50), (200, 170, 140), (230, 60, 40), False, False, True),
	("Gargoyle", "Gargoyle", 44, 30, (96, 94, 88), (36, 34, 32), (74, 72, 68), (130, 128, 120), (230, 60, 40), True, False, True),
	("FireDrake", "FireDrake", 64, 36, (150, 40, 30), (40, 10, 8), (100, 30, 20), (210, 140, 60), (250, 200, 40), True, False, True),
	("FrostDrake", "FrostDrake", 64, 36, (120, 170, 210), (30, 50, 80), (90, 130, 180), (220, 240, 255), (240, 250, 255), True, False, True),
	("GiantEagle", "GiantEagle", 48, 28, (110, 70, 40), (40, 24, 12), (80, 50, 30), (230, 225, 210), (250, 200, 40), False, True, True),
	("Stirge", "Stirge", 14, 10, (100, 60, 60), (30, 14, 14), (120, 80, 80), (150, 100, 100), (230, 30, 30), False, False, False),
]


def bestiary_three():
	count = 0
	for folder, prefix, width, height, body, dark, wing, belly, eye, horns, beak, tail in FLIERS:
		out = os.path.join(CREATURES_DIR, folder)
		os.makedirs(out, exist_ok=True)
		for frame in range(3):
			winged_body(os.path.join(out, "%sBodyA%03d.png" % (prefix, frame)), frame, width, height, body, dark, wing, belly, eye, horns, beak, tail)
			count += 1
	out = os.path.join(CREATURES_DIR, "Shade")
	os.makedirs(out, exist_ok=True)
	for frame in range(2):
		shade_body(os.path.join(out, "ShadeBodyA%03d.png" % frame), frame, 22, 28)
		count += 1
	print("wrote", count, "Bestiary III sprites")


def main():
	# Four legs.
	for c in CRABS:
		count = crab_sprites(c)
		out = crab_body(c)
		print("wrote", os.path.relpath(out, DATA_DIR), "and", count, "sprites")
	# Two legs.
	two_legged("CaveTroll", "CaveTrollBody.ini", 2.0, "Cave Troll", "Troll",
		["// The Cave Troll's body: the Civilian's rig twice as big, in a blue-grey hide.",
		"// GENERATED by Base.rte/Actors/Creatures/Tools/make_bestiary.py - edit that and re-run. The Troll itself is CaveTroll.ini."], TROLL_RAMP)
	golem = two_legged("StoneGolem", "StoneGolemBody.ini", 1.7, "Stone Golem", "Golem",
		["// The Stone Golem's body: the Civilian's rig 1.7x, in grey stone; its flesh is Stone, its wounds dents, its gibs rock.",
		"// GENERATED by Base.rte/Actors/Creatures/Tools/make_bestiary.py - edit that and re-run. The Golem itself is StoneGolem.ini."], GOLEM_RAMP)
	stonify(golem)
	# Rocks: the stone gibs (StoneGolem/) and the Troll's boulder (CaveTroll/).
	for letter, size, seed in (("A", 8, 1), ("B", 7, 2), ("C", 9, 3)):
		rock(os.path.join(CREATURES_DIR, "StoneGolem", "StoneSmall%s.png" % letter), size, seed)
	rock(os.path.join(CREATURES_DIR, "StoneGolem", "StoneTinyA.png"), 4, 4)
	rock(os.path.join(CREATURES_DIR, "CaveTroll", "TrollBoulder.png"), 16, 5)
	print("wrote the rocks")
	bestiary_two()
	bestiary_three()


if __name__ == "__main__":
	main()
