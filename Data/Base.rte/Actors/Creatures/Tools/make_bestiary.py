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


if __name__ == "__main__":
	main()
