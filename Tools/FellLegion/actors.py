#!/usr/bin/env python3
"""Writes FellLegion.rte/Actors.ini with Tools/Medieval/actors.py's writer: the Legion's units are built on the same Ronin frame and
joints as the Medieval ones, with their own sprites, kit and speech. Run from the repository root: python3 Tools/FellLegion/actors.py Data
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Medieval"))
sys.argv = [sys.argv[0], sys.argv[1] if len(sys.argv) > 1 else "Data"]

import actors as M

M.MODULE = "FellLegion"
M.OUT = os.path.join(sys.argv[1], "FellLegion.rte/Actors.ini")

# Every Legion unit is in "Unholy": what Holy Smite and the Paladin's blessings strike harder, and what the Legion's own dark magic spares.
UNHOLY = ["Unholy"]

M.UNITS = [
	dict(
		key="DreadLord", name="Dread Lord", groups=["Brains"] + UNHOLY, gold=450, mass=64, heads=1,
		desc="The Legion's master, in black plate under a crimson tabard, his horned helm burning from within. He commands the host like any brain unit, and carries a black blade, a spiked shield, the Doom and a Dark Shield for his guard: lose him and the Legion scatters.",
		torsoMat="Military Stuff", headMat="Military Stuff", limbMat="Kevlared Flesh", health=170, walk=3.8, run=4.6,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Metal Generic",
		torsoWounds=26, inventory=[("HDFirearm", "Black Blade"), ("HeldDevice", "Spiked Shield"), ("HDFirearm", "Doom"), ("HDFirearm", "Dark Shield")], metal=True, energy=(120, 12),
	),
	dict(
		key="BlackKnight", name="Black Knight", groups=["Actors - Heavy"] + UNHOLY, gold=150, mass=62, heads=2,
		desc="A knight sworn to the Dread Lord, in black plate with a green plume, a black blade and a spiked shield. Slow and hard to kill; he holds the Legion's front.",
		torsoMat="Military Stuff", headMat="Military Stuff", limbMat="Kevlared Flesh", health=130, walk=3.8, run=4.6,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Metal Generic",
		torsoWounds=24, inventory=[("HDFirearm", "Black Blade"), ("HeldDevice", "Spiked Shield")], metal=True,
	),
	dict(
		key="DeathKnight", name="Death Knight", groups=["Actors - Heavy"] + UNHOLY, gold=220, mass=62, heads=2,
		desc="A knight who died and was not let rest, in black plate under a purple tabard. He swings a cursed greatsword that feeds him the blood it spills, calls Chains of the Damned out of the ground to hold the enemy still for it, and drives his comrades into an Unholy Frenzy.",
		torsoMat="Military Stuff", headMat="Military Stuff", limbMat="Kevlared Flesh", health=140, walk=3.8, run=4.6,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Metal Generic",
		torsoWounds=24, inventory=[("HDFirearm", "Cursed Greatsword"), ("HDFirearm", "Chains of the Damned"), ("HDFirearm", "Unholy Frenzy")], metal=True, energy=(100, 10),
	),
	dict(
		key="Executioner", name="Executioner", groups=["Actors - Heavy"] + UNHOLY, gold=120, mass=58, heads=1,
		desc="A hooded headsman, bare-chested under a black fur mantle, with a great crescent axe that takes heads and limbs in one sweep and a throwing hammer for those who run. No armour: close in fast or not at all.",
		torsoMat="Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=115, walk=4.0, run=5.0,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Heavy Generic",
		torsoWounds=18, inventory=[("HDFirearm", "Executioner's Axe"), ("ThrownDevice", "Throwing Hammer"), ("ThrownDevice", "Throwing Hammer")], metal=False,
	),
	dict(
		key="Cultist", name="Cultist", groups=["Actors - Light"] + UNHOLY, gold=90, mass=42, heads=1,
		desc="A hooded cultist in black robes with a crimson sash and a wicked dagger. Cheap, and he casts Shadow Bolts that seek the enemy out and give him back what they take, and a Blood Pact that trades his own blood for the energy to cast more.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=85, walk=4.4, run=5.4,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Shadow Bolt"), ("HDFirearm", "Blood Pact"), ("HDFirearm", "Wicked Dagger")], metal=False, energy=(90, 12),
	),
	dict(
		key="DarkArcher", name="Dark Archer", groups=["Actors - Light", "Actors - Snipers"] + UNHOLY, gold=75, mass=45, heads=2,
		desc="A black-hooded archer with a dark longbow and a wicked dagger. His black arrows fly flat and bite deep; keep him behind the knights.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=90, walk=4.4, run=5.6,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=14, inventory=[("HDFirearm", "Dark Longbow"), ("HDFirearm", "Wicked Dagger")], metal=False,
	),
	dict(
		key="Marauder", name="Marauder", groups=["Actors - Light"] + UNHOLY, gold=80, mass=52, heads=2,
		desc="A raider in dark mail under a crimson surcoat, with a barbed flail that knocks men flat and a brace of bone javelins. The Legion's line infantry.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=105, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("HDFirearm", "Barbed Flail"), ("ThrownDevice", "Bone Javelin"), ("ThrownDevice", "Bone Javelin")], metal=False,
	),
	dict(
		key="Shadow", name="Shadow", groups=["Actors - Light"] + UNHOLY, gold=85, mass=44, heads=2,
		desc="An assassin in black leathers head to foot, quick on his feet, with a wicked dagger and a sheaf of cursed throwing knives. Sent round the flank while the knights hold the front; a Shadow Step takes him through a wall or a line, and a Shroud hides him from the enemy's eyes.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=85, walk=4.8, run=6.0,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Wicked Dagger"), ("ThrownDevice", "Cursed Throwing Knife"), ("ThrownDevice", "Cursed Throwing Knife"), ("ThrownDevice", "Cursed Throwing Knife"), ("ThrownDevice", "Cursed Throwing Knife"), ("HDFirearm", "Shadow Step"), ("HDFirearm", "Shroud")], metal=False, energy=(70, 10),
	),
	dict(
		key="Warlock", name="Warlock", groups=["Actors - Light"] + UNHOLY, gold=150, mass=42, heads=1,
		desc="A warlock in purple robes trimmed with green. A curse caster: Hex Bolts that weaken the enemy's arm, a Curse of Weakness over a whole knot of them, Fear that sends them running, and Life Drain, a held tether that pulls their life into him.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=80, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Hex Bolt"), ("HDFirearm", "Curse of Weakness"), ("HDFirearm", "Life Drain"), ("HDFirearm", "Fear"), ("HDFirearm", "Wicked Dagger")], metal=False, energy=(130, 14),
	),
	dict(
		key="Lich", name="Lich", groups=["Actors - Heavy"] + UNHOLY, gold=260, mass=36, heads=1,
		desc="A sorcerer long dead and still walking, green fire in his empty sockets. He hurls spears of bone, claws Ghouls up out of the ground, binds the enemy in Chains of the Damned, lays the Doom on them and harvests the souls of the fallen for his own strength. No flesh to bleed: he falls to dust instead.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=110, walk=4.0, run=5.0,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=14, inventory=[("HDFirearm", "Bone Spear"), ("HDFirearm", "Raise Ghoul"), ("HDFirearm", "Chains of the Damned"), ("HDFirearm", "Doom"), ("HDFirearm", "Soul Harvest")], metal=False, energy=(180, 15), blood="Bone Particle",
	),
	dict(
		key="Plaguebearer", name="Plaguebearer", groups=["Actors - Light"] + UNHOLY, gold=100, mass=48, heads=1,
		desc="A grenadier in a gambeson stained sickly green, who carries plague pots that burst into a choking purple miasma and hellfire pots that burn where they break. Keep him behind the line and let him throw over it.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=100, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("TDExplosive", "Plague Pot"), ("TDExplosive", "Plague Pot"), ("TDExplosive", "Plague Pot"), ("TDExplosive", "Hellfire Pot"), ("TDExplosive", "Hellfire Pot"), ("HDFirearm", "Wicked Dagger")], metal=False,
	),
	dict(
		key="HellfireMage", name="Hellfire Mage", groups=["Actors - Light"] + UNHOLY, gold=160, mass=42, heads=1,
		desc="A mage in black robes trimmed with green flame. Calls Hellfire Rain, a sigil that draws green fire down out of the sky on a patch of ground, breathes Shadowflame, a held cone of black fire that burns whoever stands in it, and calls down Darkness over the field.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=80, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Hellfire Rain"), ("HDFirearm", "Shadowflame"), ("HDFirearm", "Darkness"), ("HDFirearm", "Wicked Dagger")], metal=False, energy=(140, 14),
	),
]

M.UNITS += [
	dict(
		key="SkeletonWarrior", name="Skeleton Warrior", groups=["Actors - Light", "Actors - Undead"] + UNHOLY, gold=45, mass=32, heads=1,
		desc="Bones the Legion's liches keep on their feet, green fire in the sockets, with a rusted sword and a bone shield. Cheap, slow, fearless, and nothing to bleed: it falls to pieces when it's hit hard enough.",
		torsoMat="Bone", headMat="Bone", limbMat="Bone", health=65, walk=3.8, run=4.8,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Rusted Sword"), ("HeldDevice", "Bone Shield")], metal=False, blood="Bone Particle",
	),
	dict(
		key="Ghoul", name="Ghoul", groups=["Actors - Light", "Actors - Undead", "Actors - Creatures"] + UNHOLY, gold=70, mass=48, heads=1,
		desc="A corpse that won't lie down: rotten green flesh on a runner's frame, a skull for a face. It runs at the enemy and tears at them with its claws, and its wounds knit themselves closed (Ghoul.lua). Raise Ghoul claws one out of the ground.",
		torsoMat="Flesh", headMat="Bone", limbMat="Flesh", health=90, walk=5.0, run=6.4,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=14, inventory=[("HDFirearm", "Ghoul Claws")], metal=False, blood="Bone Particle", script="FellLegion.rte/Actors/Ghoul/Ghoul.lua",
	),
	dict(
		key="Imp", name="Imp", groups=["Actors - Light", "Actors - Creatures"] + UNHOLY, gold=40, mass=28, heads=1,
		desc="A small red fiend with horns and a grin. Fire doesn't catch on it, it cackles, and it throws gouts of fire at whatever it hunts. Weak alone; the Legion buys them in threes.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=60, walk=4.8, run=6.0,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=10, inventory=[("HDFirearm", "Imp Fire Bolt")], metal=False, fireproof=True,
	),
]

M.HEADER = """///////////////////////////////////////////////////////////////////////
// Fell Legion Actors
//
// Written by Tools/FellLegion/actors.py with Tools/Medieval/actors.py's writer: each unit's head, limbs and body share the Ronin soldier's
// frame and joints, with the Legion's own sprites and armour, and the Medieval leap (Medieval.rte/Actors.ini) instead of a jetpack.
// Every unit is in the "Unholy" group.
"""

if __name__ == "__main__":
	M.main()
