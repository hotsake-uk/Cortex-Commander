#!/usr/bin/env python3
"""Writes Medieval.rte/Actors.ini: each unit's head, limbs and body, built on the Ronin soldier's frame and joints."""
import os
import sys

OUT = os.path.join(sys.argv[1], "Medieval.rte/Actors.ini")

UNITS = [
	dict(
		key="Knight", name="Knight", groups=["Actors - Heavy"], gold=140, mass=60, heads=2,
		desc="A knight in full plate with sword and kite shield. Slow, but arrows and blades glance off the armour; send him at the enemy's front.",
		torsoMat="Military Stuff", headMat="Military Stuff", limbMat="Kevlared Flesh", health=120, walk=3.8, run=4.6,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Metal Generic",
		torsoWounds=22, inventory=[("HDFirearm", "Longsword"), ("HeldDevice", "Kite Shield")], metal=True,
	),
	dict(
		key="Footman", name="Spearman", groups=["Actors - Light"], gold=70, mass=50, heads=2,
		desc="A levied footman in mail and a kettle hat, with a long spear. Cheap, and his reach keeps swords at bay.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=100, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("HDFirearm", "Spear")], metal=False,
	),
	dict(
		key="Footman", name="Axeman", groups=["Actors - Heavy"], gold=90, mass=52, heads=2,
		desc="A footman with a two-handed battle axe. Hits hard enough to take limbs off; keep him out of arrow range until he's close.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=100, walk=4.0, run=5.0,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("HDFirearm", "Battle Axe")], metal=False,
	),
	dict(
		key="Archer", name="Archer", groups=["Actors - Light", "Actors - Snipers"], gold=70, mass=45, heads=2,
		desc="A hooded longbowman with a dagger at his belt. Unarmoured: keep him behind the line and let him shoot over it.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=90, walk=4.4, run=5.6,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=14, inventory=[("HDFirearm", "Longbow"), ("HDFirearm", "Dagger")], metal=False,
	),
	dict(
		key="Crossbowman", name="Crossbowman", groups=["Actors - Light"], gold=85, mass=48, heads=1,
		desc="A crossbowman in a padded gambeson and steel cap. Slow to reload, but the bolts fly flat and hit hard.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=100, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("HDFirearm", "Crossbow"), ("HDFirearm", "Dagger")], metal=False,
	),
	dict(
		key="King", name="King", groups=["Brains"], gold=400, mass=55, heads=1,
		desc="The king himself, crowned and robed over his mail. He commands the army like any brain unit; lose him and the battle is lost.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Kevlared Flesh", health=150, walk=4.0, run=5.0,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Generic",
		torsoWounds=24, inventory=[("HDFirearm", "Longsword"), ("HeldDevice", "Kite Shield")], metal=True,
	),
]


def vec(x, y, indent):
	t = "\t" * indent
	return f"Vector\n{t}\tX = {x}\n{t}\tY = {y}"


def gibs(indent, *items):
	t = "\t" * indent
	out = []
	for cls, preset, count, extra in items:
		s = f"{t}AddGib = Gib\n{t}\tGibParticle = {cls}\n{t}\t\tCopyOf = {preset}\n{t}\tCount = {count}\n{t}\tSpread = 2.25\n{t}\tMaxVelocity = 10\n{t}\tMinVelocity = 1"
		if extra:
			s += "\n" + extra
		out.append(s)
	return "\n".join(out)


def parts(u):
	k = u["key"]
	d = f"Medieval.rte/Actors/{k}"
	p = f"Medieval {k}"
	metalGib = ("MOSParticle", "Gib Metal Grey Tiny A", 3, "") if u["metal"] else ("MOSParticle", "Gib Flesh Tiny A", 2, "")
	return f"""
///////////////////////////////////////////////////////////////////////
// {k} parts


AddEffect = Attachable
	PresetName = {p} Head
	Mass = 5
	HitsMOs = 1
	GetsHitByMOs = 1
	ScriptPath = Base.rte/Scripts/Shared/RandomFrame.lua
	SpriteFile = ContentFile
		FilePath = {d}/Head.png
	FrameCount = {u["heads"]}
	SpriteOffset = Vector
		X = -7
		Y = -8
	AngularVel = 6
	EntryWound = AEmitter
		CopyOf = Wound Flesh Entry Deadly
		PresetName = {p} Wound Entry Head
		BurstSound = SoundContainer
			CopyOf = {u["headImpact"]}
	ExitWound = AEmitter
		CopyOf = Wound Flesh Exit Deadly
	AtomGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = {u["headMat"]}
		Resolution = 4
		Depth = 1
	DeepCheck = 0
	JointStrength = 200
	JointStiffness = 0.1
	BreakWound = AEmitter
		CopyOf = Wound Flesh Body
	ParentBreakWound = AEmitter
		CopyOf = Wound Flesh Body
	JointOffset = Vector
		X = -2
		Y = 5
	GibImpulseLimit = 480
	GibWoundLimit = {10 if u["metal"] else 8}
	GibSound = SoundContainer
		CopyOf = Flesh Head Gib
{gibs(1, ("MOPixel", "Drop Blood", 60, ""), ("MOSRotating", "Gib Flesh Small A", 2, ""), ("MOSRotating", "Gib Bone Small D", 2, ""), metalGib)}


AddActor = Arm
	PresetName = {p} Arm FG
	Mass = 6
	HitsMOs = 1
	GetsHitByMOs = 1
	SpriteFile = ContentFile
		FilePath = {d}/ArmFGA.png
	FrameCount = 5
	SpriteOffset = Vector
		X = -6
		Y = -3
	AngularVel = 6
	EntryWound = AEmitter
		CopyOf = Wound Flesh Entry
		BurstSound = SoundContainer
			CopyOf = Flesh Limb Impact
	ExitWound = AEmitter
		CopyOf = Wound Flesh Exit
	AtomGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = {u["limbMat"]}
		Resolution = 4
		Depth = 0
	DeepCheck = 0
	ParentOffset = Vector
		X = -1
		Y = -4
	JointStrength = 160
	JointStiffness = 0.5
	BreakWound = AEmitter
		CopyOf = Wound Flesh Body
	ParentBreakWound = AEmitter
		CopyOf = Wound Flesh Body
	JointOffset = Vector
		X = -3.5
		Y = -1
	DrawAfterParent = 1
	Hand = ContentFile
		FilePath = {d}/HandFGA.png
	GripStrength = 130
	ThrowStrength = 31
	MaxLength = 14
	IdleOffset = Vector
		X = 4
		Y = 8
	MoveSpeed = 0.2
	GibImpulseLimit = 240
	GibWoundLimit = 7
	GibSound = SoundContainer
		CopyOf = Flesh Limb Gib
{gibs(1, ("MOPixel", "Drop Blood", 25, ""), ("MOSRotating", "Gib Bone Small A", 1, ""), ("MOSParticle", "Gib Flesh Micro A", 2, ""), metalGib)}


AddActor = Arm
	CopyOf = {p} Arm FG
	PresetName = {p} Arm BG
	SpriteFile = ContentFile
		FilePath = {d}/ArmBGA.png
	Hand = ContentFile
		FilePath = {d}/HandFGB.png


AddActor = Attachable
	PresetName = {p} Foot FG
	Mass = 4
	HitsMOs = 1
	GetsHitByMOs = 0
	SpriteFile = ContentFile
		FilePath = {d}/FootFGA.png
	FrameCount = 4
	SpriteOffset = Vector
		X = -6
		Y = -3
	AngularVel = 6
	EntryWound = AEmitter
		CopyOf = Wound Bone Entry
	ExitWound = AEmitter
		CopyOf = Wound Bone Exit
	AtomGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = Flesh
		Resolution = 1
		Depth = 0
	DeepGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = Civilian Stuff
		Resolution = 4
		Depth = 2
	DeepCheck = 0
	JointStrength = 110
	JointStiffness = 0.5
	BreakWound = AEmitter
		CopyOf = Wound Flesh Body
	ParentBreakWound = AEmitter
		CopyOf = Wound Flesh Body
	JointOffset = Vector
		X = -2
		Y = -1
	DrawAfterParent = 0


AddActor = Attachable
	CopyOf = {p} Foot FG
	PresetName = {p} Foot BG
	SpriteFile = ContentFile
		FilePath = {d}/FootBGA.png


AddActor = Leg
	PresetName = {p} Leg FG
	Mass = 10
	HitsMOs = 1
	GetsHitByMOs = 1
	SpriteFile = ContentFile
		FilePath = {d}/LegFGA.png
	FrameCount = 5
	SpriteOffset = Vector
		X = -6
		Y = -7
	AngularVel = 6
	EntryWound = AEmitter
		CopyOf = Wound Flesh Entry
		BurstSound = SoundContainer
			CopyOf = Flesh Limb Impact
	ExitWound = AEmitter
		CopyOf = Wound Flesh Exit
	AtomGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = {u["limbMat"]}
		Resolution = 4
		Depth = 0
	DeepCheck = 0
	JointStrength = 360
	JointStiffness = 0.5
	BreakWound = AEmitter
		CopyOf = Wound Flesh Body
	ParentBreakWound = AEmitter
		CopyOf = Wound Flesh Body
	JointOffset = Vector
		X = -5
		Y = 2
	DrawAfterParent = 1
	Foot = Attachable
		CopyOf = {p} Foot FG
		ParentOffset = Vector
			X = -11
			Y = -10
	ExtendedOffset = Vector
		X = 15
		Y = 0
	ContractedOffset = Vector
		X = 7.5
		Y = 0
	IdleOffset = Vector
		X = 1
		Y = 3
	MoveSpeed = 0.4
	GibImpulseLimit = 480
	GibWoundLimit = 9
	GibSound = SoundContainer
		CopyOf = Flesh Limb Gib
{gibs(1, ("MOPixel", "Drop Blood", 35, ""), ("MOSRotating", "Gib Bone Small E", 1, ""), ("MOSParticle", "Gib Flesh Tiny A", 2, ""), metalGib)}


AddActor = Leg
	CopyOf = {p} Leg FG
	PresetName = {p} Leg BG
	SpriteFile = ContentFile
		FilePath = {d}/LegBGA.png
	Foot = Attachable
		CopyOf = {p} Foot BG
		ParentOffset = Vector
			X = -11
			Y = -10
"""


def actor(u):
	k = u["key"]
	d = f"Medieval.rte/Actors/{k}"
	p = f"Medieval {k}"
	groups = "\n".join(f"\tAddToGroup = {g}" for g in ["Actors"] + u["groups"])
	inv = "\n".join(f"\tAddInventory = {cls}\n\t\tCopyOf = {name}" for cls, name in u["inventory"])
	metalGib = ("MOSParticle", "Gib Metal Grey Tiny A", 4, "") if u["metal"] else ("MOSParticle", "Gib Flesh Micro A", 3, "")
	return f"""
///////////////////////////////////////////////////////////////////////
// {u["name"]}


AddActor = AHuman
	PresetName = {u["name"]}
{groups}
	Description = {u["desc"]}
	Mass = {u["mass"]}
	GoldValue = {u["gold"]}
	HitsMOs = 1
	GetsHitByMOs = 1
	ScriptPath = Base.rte/AI/HumanAI.lua
	ScriptPath = Base.rte/Scripts/Shared/AlternateHumanPainSounds.lua
	SpriteFile = ContentFile
		FilePath = {d}/Torso.png
	FrameCount = 1
	SpriteOffset = Vector
		X = -7
		Y = -12
	EntryWound = AEmitter
		CopyOf = Wound Flesh Entry Deadly
		PresetName = {u["name"]} Wound Entry Torso
		BurstSound = SoundContainer
			CopyOf = {u["torsoImpact"]}
	ExitWound = AEmitter
		CopyOf = Wound Flesh Exit Deadly
	AtomGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = {u["torsoMat"]}
		Resolution = 4
		Depth = 0
	DeepGroup = AtomGroup
		AutoGenerate = 1
		Material = Material
			CopyOf = Civilian Stuff
		Resolution = 6
		Depth = 3
	DeepCheck = 0
	BodyHitSound = SoundContainer
		CopyOf = Foley Light Cloth Impact
	PainSound = SoundContainer
		CopyOf = Human Pain
	DeathSound = SoundContainer
		CopyOf = Human Death
	DeviceSwitchSound = SoundContainer
		CopyOf = {u["foley"]}
	Health = {u["health"]}
	MaxHealth = {u["health"]}
	Organic = 1
	ImpulseDamageThreshold = 1800
	AimDistance = 30
	Perceptiveness = 1
	SharpAimDelay = 100
	CharHeight = 100
	StableVelocityThreshold = Vector
		X = 12
		Y = 20
	HolsterOffset = Vector
		X = -6
		Y = -4
	ReloadOffset = Vector
		X = 2
		Y = 4
	Head = Attachable
		CopyOf = {p} Head
		ParentOffset = Vector
			X = 0
			Y = -9
	Jetpack = AEJetpack
		CopyOf = Medieval Leap
		ParentOffset = Vector
			X = -3
			Y = 4
	FGArm = Arm
		CopyOf = {p} Arm FG
		ParentOffset = Vector
			X = -1
			Y = -5
	FGArmFlailScalar = 0.35
	BGArm = Arm
		CopyOf = {p} Arm BG
		ParentOffset = Vector
			X = 4
			Y = -7
	BGArmFlailScalar = -0.7
	FGLeg = Leg
		CopyOf = {p} Leg FG
		ParentOffset = Vector
			X = 1
			Y = 1
	BGLeg = Leg
		CopyOf = {p} Leg BG
		ParentOffset = Vector
			X = 3
			Y = 1
	HandGroup = AtomGroup
		CopyOf = Human Hand
	FGFootGroup = AtomGroup
		CopyOf = Human Foot
	BGFootGroup = AtomGroup
		CopyOf = Human Foot
	StrideSound = SoundContainer
		CopyOf = {u["stride"]}
	StandLimbPath = LimbPath
		PresetName = {u["name"]} Stand Path
		StartOffset = Vector
			X = -2
			Y = 17
		StartSegCount = 0
		TravelSpeed = 0.5
		PushForce = 4750
	StandLimbPathBG = LimbPath
		CopyOf = {u["name"]} Stand Path
		PresetName = {u["name"]} Stand Path BG
		StartOffset = Vector
			X = 8
			Y = 17
	WalkLimbPath = LimbPath
		CopyOf = Human Walk Path
		TravelSpeed = {u["walk"]}
	WalkRotAngleTarget = -0.1
	RunLimbPath = LimbPath
		CopyOf = Human Run Path
		TravelSpeed = {u["run"]}
	RunRotAngleTarget = -0.12
	CrouchLimbPath = LimbPath
		CopyOf = Human Crouch Path
	CrouchLimbPathBG = LimbPath
		CopyOf = Human Crouch Path BG
	CrouchRotAngleTarget = -0.7
	CrawlLimbPath = LimbPath
		CopyOf = Human Crawl Path
	ArmCrawlLimbPath = LimbPath
		CopyOf = Human Arm Crawl Path
	ClimbLimbPath = LimbPath
		CopyOf = Human Climb Path
	JumpLimbPath = LimbPath
		CopyOf = Human Jump Path
	DislodgeLimbPath = LimbPath
		CopyOf = Human Dislodge Path
{inv}
	GibImpulseLimit = 5000
	GibWoundLimit = {u["torsoWounds"]}
	GibSound = SoundContainer
		CopyOf = Flesh Torso Gib
{gibs(1, ("MOPixel", "Drop Blood", 50, ""), ("MOSRotating", "Gib Flesh Small A", 4, ""), ("MOSRotating", "Gib Flesh Small D", 4, ""), ("MOSParticle", "Gib Bone Micro A", 3, ""), metalGib)}
"""


HEADER = """///////////////////////////////////////////////////////////////////////
// Medieval Actors
//
// Written by Tools/Medieval/actors.py: each unit's head, limbs and body share the Ronin soldier's frame and joints, with their own
// sprites and armour. Instead of a jetpack they have a leap: a short, invisible jump push.


AddEffect = MOPixel
	PresetName = Medieval Leap Push
	Mass = 2
	LifeTime = 30
	HitsMOs = 0
	GetsHitByMOs = 0
	Color = Color
		R = 255
		G = 0
		B = 255
	Atom = Atom
		Material = Material
			CopyOf = Air


AddEffect = AEJetpack
	PresetName = Medieval Leap
	Mass = 0
	HitsMOs = 0
	GetsHitByMOs = 0
	SpriteFile = ContentFile
		FilePath = Medieval.rte/Actors/Shared/Null.png
	FrameCount = 1
	SpriteOffset = Vector
		X = 0
		Y = 0
	AtomGroup = AtomGroup
		CopyOf = Null AtomGroup
	JointStrength = 10000
	JointStiffness = 1
	DrawAfterParent = 0
	DeleteWhenRemovedFromParent = 1
	JetpackType = JumpPack
	AddEmission = Emission
		EmittedParticle = MOPixel
			CopyOf = Medieval Leap Push
		Spread = 0.1
		MaxVelocity = 20
		MinVelocity = 14
	AddEmission = Emission
		EmittedParticle = MOSParticle
			CopyOf = Tiny Smoke Ball 1
			PresetName = Medieval Leap Dust
			LifeTime = 300
			AirResistance = 0.2
			AirThreshold = 5
		ParticlesPerMinute = 600
		BurstSize = 3
		Spread = 1.2
		MaxVelocity = 6
		MinVelocity = 2
	ParticlesPerMinute = 12000
	BurstSize = 15
	JumpTime = 0.35
	JumpReplenishRate = 0.9
	JumpAngleRange = 0.35
	CanAdjustAngleWhileFiring = 0
"""

with open(OUT, "w") as f:
	f.write(HEADER)
	done = set()
	for u in UNITS:
		if u["key"] not in done:
			f.write(parts(u))
			done.add(u["key"])
	for u in UNITS:
		f.write(actor(u))
print("actors done")
