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
		torsoWounds=16, inventory=[("HDFirearm", "Spear"), ("ThrownDevice", "Javelin"), ("ThrownDevice", "Javelin")], metal=False,
	),
	dict(
		key="Footman", name="Axeman", groups=["Actors - Heavy"], gold=90, mass=52, heads=2,
		desc="A footman with a two-handed battle axe. Hits hard enough to take limbs off; keep him out of arrow range until he's close.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=100, walk=4.0, run=5.0,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("HDFirearm", "Battle Axe"), ("ThrownDevice", "Throwing Axe"), ("ThrownDevice", "Throwing Axe")], metal=False,
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
		torsoWounds=16, inventory=[("HDFirearm", "Crossbow"), ("HDFirearm", "Dagger"), ("ThrownDevice", "Throwing Knife"), ("ThrownDevice", "Throwing Knife"), ("ThrownDevice", "Throwing Knife")], metal=False,
	),
	dict(
		key="King", name="King", groups=["Brains"], gold=400, mass=55, heads=1,
		desc="The king himself, crowned and robed over his mail. He commands the army like any brain unit; lose him and the battle is lost.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Kevlared Flesh", health=150, walk=4.0, run=5.0,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Generic",
		torsoWounds=24, inventory=[("HDFirearm", "Longsword"), ("HeldDevice", "Kite Shield")], metal=True,
	),
	dict(
		key="Mage", name="Mage", groups=["Actors - Light"], gold=110, mass=42, heads=1,
		desc="A robed mage with a book of Telekinetic Push and a dagger. Throws back whoever closes in, grenades and all; keep him behind the line.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=80, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Telekinetic Push"), ("HDFirearm", "Dagger")], metal=False, energy=(100, 12),
	),
	dict(
		key="Witch", name="Witch", groups=["Actors - Light"], gold=110, mass=40, heads=1,
		desc="A witch with a scroll of Acid Spray and a dagger. Her acid stings and eats the ground for ten seconds, then is gone.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=80, walk=4.4, run=5.4,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Acid Spray"), ("HDFirearm", "Dagger")], metal=False, energy=(100, 15),
	),
	dict(
		key="Pyromancer", name="Pyromancer", groups=["Actors - Light"], gold=130, mass=42, heads=1,
		desc="A fire mage in a red robe. Hurls fireballs that burst and set the ground alight, raises a wall of flame across the enemy's path, and breathes flame on whoever gets close. Keep him away from your own wooden huts.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=80, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Fireball"), ("HDFirearm", "Flame Wall"), ("HDFirearm", "Dragon's Breath"), ("HDFirearm", "Dagger")], metal=False, energy=(130, 14),
	),
	dict(
		key="Stormcaller", name="Stormcaller", groups=["Actors - Light"], gold=140, mass=42, heads=1,
		desc="A storm mage in grey and teal. Calls lightning down out of a clear sky on whatever she points at, chains it from one man to the next, claps thunder that stuns whoever closes in, and looses homing arcane missiles between strikes.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=80, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Call Lightning"), ("HDFirearm", "Chain Lightning"), ("HDFirearm", "Thunderclap"), ("HDFirearm", "Arcane Missiles"), ("HDFirearm", "Dagger")], metal=False, energy=(130, 15),
	),
	dict(
		key="Cryomancer", name="Cryomancer", groups=["Actors - Light"], gold=120, mass=42, heads=1,
		desc="An ice mage. A blast of frost shards chills whoever it hits to a crawl, an ice lance skewers one man at range, a blizzard freezes a whole squad where it stands, and an arcane nova throws back anyone who closes in, grenades and all.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=80, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Frost Shards"), ("HDFirearm", "Ice Lance"), ("HDFirearm", "Blizzard"), ("HDFirearm", "Arcane Nova"), ("HDFirearm", "Dagger")], metal=False, energy=(130, 15),
	),
	dict(
		key="Geomancer", name="Geomancer", groups=["Actors - Light"], gold=150, mass=44, heads=1,
		desc="An earth mage in brown and ochre. Drives a line of stone spikes up through whoever stands in front of him, raises a wall of stone for his friends to shelter behind, and turns the man under his aim to a statue for five seconds.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=85, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Stone Spikes"), ("HDFirearm", "Wall of Stone"), ("HDFirearm", "Petrify"), ("HDFirearm", "Dagger")], metal=False, energy=(130, 14),
	),
	dict(
		key="Necromancer", name="Necromancer", groups=["Actors - Light"], gold=150, mass=40, heads=1,
		desc="A hooded necromancer. Raises skeletons out of the ground to fight for him and lobs clouds of poison into the enemy's trenches. Fragile: keep him at the back.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=80, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Raise Dead"), ("HDFirearm", "Poison Cloud"), ("HDFirearm", "Dagger")], metal=False, energy=(130, 12),
	),
	dict(
		key="Druid", name="Druid", groups=["Actors - Light"], gold=130, mass=44, heads=1,
		desc="A druid in a crown of leaves. Shatters the earth in a rolling wave that throws men off their feet, calls a pair of wolves out of the mist, douses fires and knocks men down with a jet of water, and sends a tornado tearing across the field.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=90, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Earth Shatter"), ("HDFirearm", "Summon Wolf"), ("HDFirearm", "Water Jet"), ("HDFirearm", "Tornado"), ("HDFirearm", "Dagger")], metal=False, energy=(140, 14),
	),
	dict(
		key="Archmage", name="Archmage", groups=["Actors - Heavy"], gold=280, mass=44, heads=1,
		desc="The court's archmage, in white and gold. Calls a storm of meteors down on the enemy, burns them with a beam of sunlight from his hands, opens gravity wells that swallow whole squads, and slows time itself. Expensive, and worth guarding.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=100, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Meteor Storm"), ("HDFirearm", "Sunbeam"), ("HDFirearm", "Gravity Well"), ("HDFirearm", "Time Warp"), ("HDFirearm", "Dagger")], metal=False, energy=(220, 17),
	),
	dict(
		key="Spellblade", name="Spellblade", groups=["Actors - Heavy"], gold=180, mass=58, heads=2,
		desc="A knight who studied at the tower. Plate, sword and shield like any knight, and a book of arcane missiles for whoever tries to keep out of reach.",
		torsoMat="Military Stuff", headMat="Military Stuff", limbMat="Kevlared Flesh", health=120, walk=3.8, run=4.6,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Metal Generic",
		torsoWounds=22, inventory=[("HDFirearm", "Longsword"), ("HeldDevice", "Kite Shield"), ("HDFirearm", "Arcane Missiles")], metal=True, energy=(80, 10),
	),
	dict(
		key="Paladin", name="Paladin", groups=["Actors - Heavy"], gold=200, mass=62, heads=2,
		desc="A holy knight in white and gold plate with a two-handed warhammer that knocks men flat, and one Holy Hand Grenade for when it really counts.",
		torsoMat="Military Stuff", headMat="Military Stuff", limbMat="Kevlared Flesh", health=140, walk=3.7, run=4.5,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Metal Generic",
		torsoWounds=22, inventory=[("HDFirearm", "Warhammer"), ("TDExplosive", "Holy Hand Grenade")], metal=True,
	),
	dict(
		key="Footman", name="Man-at-Arms", groups=["Actors - Light"], gold=80, mass=50, heads=2,
		desc="A footman with a flanged mace and a round shield. The mace doesn't care about armour.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=100, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("HDFirearm", "Mace"), ("HeldDevice", "Round Shield"), ("ThrownDevice", "Throwing Hammer"), ("ThrownDevice", "Throwing Hammer")], metal=False,
	),
	dict(
		key="Halberdier", name="Halberdier", groups=["Actors - Light"], gold=90, mass=52, heads=2,
		desc="A footman in the king's red with a halberd: an axe, a spike and a hook on a long pole. Holds a gate against anything.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=100, walk=4.0, run=5.0,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("HDFirearm", "Halberd")], metal=False,
	),
	dict(
		key="Berserker", name="Berserker", groups=["Actors - Heavy"], gold=110, mass=55, heads=1,
		desc="A bare-chested northman with a greatsword and two throwing axes. Fast and hard to put down; he doesn't hold a line, he breaks one.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=150, walk=4.6, run=6.0,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Greatsword"), ("ThrownDevice", "Throwing Axe"), ("ThrownDevice", "Throwing Axe")], metal=False,
	),
	dict(
		key="Bombardier", name="Bombardier", groups=["Actors - Light"], gold=100, mass=48, heads=1,
		desc="A sapper with a satchel of firepots and stinkpots. Throws greek fire and choking gas over walls; don't let him stand near anything that burns.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=90, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("TDExplosive", "Firepot"), ("TDExplosive", "Firepot"), ("TDExplosive", "Firepot"), ("TDExplosive", "Stinkpot"), ("TDExplosive", "Stinkpot"), ("HDFirearm", "Dagger")], metal=False,
	),
	dict(
		key="Skirmisher", name="Skirmisher", groups=["Actors - Light"], gold=75, mass=45, heads=2,
		desc="A light skirmisher in leathers. Javelins for the near ones, bolas to bring down runners, and a pouch of caltrops to cover a retreat.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=90, walk=4.6, run=5.8,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("ThrownDevice", "Javelin"), ("ThrownDevice", "Javelin"), ("ThrownDevice", "Javelin"), ("ThrownDevice", "Bolas"), ("ThrownDevice", "Bolas"), ("ThrownDevice", "Caltrops"), ("HDFirearm", "Dagger")], metal=False,
	),
	dict(
		key="Skeleton", name="Skeleton", groups=["Actors - Light"], gold=40, mass=30, heads=1,
		desc="Bones that a necromancer talked back onto their feet, with a sword they found on the way up. Cheap and fearless, and falls to pieces when it's hit hard enough.",
		torsoMat="Bone", headMat="Bone", limbMat="Bone", health=60, walk=4.0, run=5.0,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Longsword")], metal=False, speech="Undead", blood="Bone Particle",
	),
	dict(
		key="Woodcutter", name="Woodcutter", groups=["Actors - Light", "Non-combatants", "Colony Workers"], gold=40, mass=46, heads=1,
		desc="A woodcutter with a felling axe. A colony woodcutter's staff: he fells the trees round his hut, cuts them into logs and carries them home. Won't start a fight, but swings that axe at anyone who hits him.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=90, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=14, inventory=[("HDFirearm", "Woodcutter's Axe")], metal=False, nature="Defensive",
	),
	# The Kingdom's second levy (Devices/Arsenal/Arsenal2.ini).
	dict(
		key="Longbowman", name="Longbowman", groups=["Actors - Light", "Actors - Snipers"], gold=95, mass=48, heads=2,
		desc="A big man in a studded brigandine with a six-foot warbow and a quiver of bodkins that go through mail, a few fire arrows for the thatch, and a falchion for when they get close. Slow to draw, long to reach; put him on a hill.",
		torsoMat="Kevlared Flesh", headMat="Flesh", limbMat="Flesh", health=100, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact Armored", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("HDFirearm", "Warbow"), ("HDFirearm", "Fire Arrows"), ("HDFirearm", "Falchion")], metal=False,
	),
	dict(
		key="Hunter", name="Hunter", groups=["Actors - Light"], gold=85, mass=44, heads=2,
		desc="A huntsman in green leathers with a quick hunting bow and barbed arrows, a net for the one who runs, two snare traps for the path behind, and a hunting knife. Fast, quiet, and not for the shield wall.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=90, walk=4.6, run=5.8,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Hunting Bow"), ("ThrownDevice", "Net"), ("ThrownDevice", "Snare Trap"), ("ThrownDevice", "Snare Trap"), ("HDFirearm", "Hunting Knife")], metal=False,
	),
	dict(
		key="Arbalester", name="Arbalester", groups=["Actors - Light", "Actors - Snipers"], gold=110, mass=50, heads=1,
		desc="A crossbowman with a steel-bowed arbalest that takes an age to span and goes through a shield and the man behind it, a buckler on his arm and a falchion at his belt. One shot, then cover.",
		torsoMat="Kevlared Flesh", headMat="Kevlared Flesh", limbMat="Flesh", health=100, walk=4.0, run=5.0,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=16, inventory=[("HDFirearm", "Arbalest"), ("HDFirearm", "Falchion"), ("HeldDevice", "Buckler")], metal=False,
	),
	dict(
		key="Royal Guard", name="Royal Guard", groups=["Actors - Heavy"], gold=210, mass=68, heads=2,
		desc="The king's own, in full plate under purple and gold, with a poleaxe that cuts, crushes and knocks men flat, and a tower shield that covers all of him. The slowest man on the field, and the hardest to move.",
		torsoMat="Military Stuff", headMat="Military Stuff", limbMat="Kevlared Flesh", health=160, walk=3.4, run=4.2,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Metal Generic",
		torsoWounds=26, inventory=[("HDFirearm", "Poleaxe"), ("HeldDevice", "Tower Shield"), ("HDFirearm", "Morning Star")], metal=True,
	),
	dict(
		key="Alchemist", name="Alchemist", groups=["Actors - Light"], gold=120, mass=44, heads=1,
		desc="An alchemist in a leather apron with a satchel of flasks: oil to lay a slick for the fire arrows, frost to freeze a squad in its tracks, smoke to cross the open, and two healing draughts he throws among the wounded. A quarterstaff for the rest.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=90, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=14, inventory=[("HDFirearm", "Quarterstaff"), ("TDExplosive", "Oil Flask"), ("TDExplosive", "Oil Flask"), ("TDExplosive", "Frost Flask"), ("TDExplosive", "Smoke Flask"), ("TDExplosive", "Healing Draught"), ("TDExplosive", "Healing Draught")], metal=False,
	),
	# The Kingdom's support casters and champions (SE-1). The AI casts the support spells on friends who need them (HumanBehaviors.lua,
	# SupportSpellUpdate) and fights with the mace, dagger or sword between casts.
	dict(
		key="Cleric", name="Cleric", groups=["Actors - Light"], gold=130, mass=44, heads=1,
		desc="A hooded cleric in white with a mace, and the books of Heal, Blessing and Shield of Faith. Heals whoever is hurt near him, blesses the fighters and shields the one about to fall; keep him just behind the line.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=90, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=14, inventory=[("HDFirearm", "Mace"), ("HDFirearm", "Holy Smite"), ("HDFirearm", "Heal"), ("HDFirearm", "Blessing"), ("HDFirearm", "Shield of Faith")], metal=False, energy=(140, 14),
	),
	dict(
		key="Enchanter", name="Enchanter", groups=["Actors - Light"], gold=130, mass=42, heads=1,
		desc="A purple-robed enchanter with Haste, Enchant Weapon, Stoneskin and Dispel, and a dagger. Makes the fighters round him faster, harder-hitting and tougher, and lifts curses; worth nothing alone, worth a squad with one.",
		torsoMat="Flesh", headMat="Flesh", limbMat="Flesh", health=80, walk=4.2, run=5.2,
		headImpact="Flesh Head Impact", torsoImpact="Flesh Torso Impact", foley="Foley Light Cloth Light", stride="Footstep Light Generic",
		torsoWounds=12, inventory=[("HDFirearm", "Haste"), ("HDFirearm", "Enchant Weapon"), ("HDFirearm", "Stoneskin"), ("HDFirearm", "Dispel"), ("HDFirearm", "Bridge of Light"), ("HDFirearm", "Dagger")], metal=False, energy=(120, 14),
	),
	dict(
		key="Battlemage", name="Battlemage", groups=["Actors - Heavy"], gold=190, mass=60, heads=2,
		desc="A knight in blue plate who fights with a longsword and casts between blows: Fireball at range, Telekinetic Grab to haul a man out of the line, and Blink to cross a wall or escape one. Plate makes him a slow caster, but a hard one to stop.",
		torsoMat="Military Stuff", headMat="Military Stuff", limbMat="Kevlared Flesh", health=120, walk=3.8, run=4.6,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Metal Generic",
		torsoWounds=22, inventory=[("HDFirearm", "Longsword"), ("HDFirearm", "Fireball"), ("HDFirearm", "Wall of Stone"), ("HDFirearm", "Telekinetic Grab"), ("HDFirearm", "Blink")], metal=True, energy=(110, 10),
	),
	dict(
		key="Champion", name="Champion", groups=["Actors - Heavy"], gold=220, mass=64, heads=2,
		desc="The king's champion in gold-trimmed plate with a greatsword, a Blessing on himself before he closes and Mirror Image to draw the arrows meant for him. The best sword in the realm, and he knows it.",
		torsoMat="Military Stuff", headMat="Military Stuff", limbMat="Kevlared Flesh", health=150, walk=3.8, run=4.7,
		headImpact="Flesh Head Impact Armored", torsoImpact="Flesh Torso Impact Armored", foley="Foley Heavy Cloth Light", stride="Footstep Heavy Metal Generic",
		torsoWounds=24, inventory=[("HDFirearm", "Greatsword"), ("HDFirearm", "Blessing"), ("HDFirearm", "Mirror Image")], metal=True, energy=(80, 8),
	),
]


def energy(u):
	"""A spellcaster's energy pool (MG-2): what its spells spend, and how much comes back a second."""
	if "energy" not in u:
		return ""
	most, regen = u["energy"]
	return f"\n\tMaxEnergy = {most}\n\tEnergyRegen = {regen}"


def blood(u):
	"""What a unit bleeds: blood, or bone dust for a skeleton."""
	return u.get("blood", "Drop Blood")


def gib(u, preset):
	"""A flesh gib, or the same in bone for a unit that has no flesh."""
	return preset.replace("Flesh", "Bone") if "blood" in u else preset


def nature(u):
	"""A unit that is no fighter by trade (NC-1): its temperament, and kept out of the fighting units' counts."""
	if "nature" not in u:
		return ""
	return f"\n\tTemperament = {u['nature']}\n\tNonCombatant = 1"


def speech(u):
	"""The unit's speech set (Medieval.rte/Speech): the faction's own lines, or the skeletons'."""
	return f"\n\tSpeechSet = {u.get('speech', 'Medieval')}"


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
	d = f"Medieval.rte/Actors/{k.replace(' ', '')}"
	p = f"Medieval {k}"
	metalGib = ("MOSParticle", "Gib Metal Grey Tiny A", 3, "") if u["metal"] else ("MOSParticle", gib(u, "Gib Flesh Tiny A"), 2, "")
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
{gibs(1, ("MOPixel", blood(u), 60, ""), ("MOSRotating", gib(u, "Gib Flesh Small A"), 2, ""), ("MOSRotating", "Gib Bone Small D", 2, ""), metalGib)}


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
{gibs(1, ("MOPixel", blood(u), 25, ""), ("MOSRotating", "Gib Bone Small A", 1, ""), ("MOSParticle", gib(u, "Gib Flesh Micro A"), 2, ""), metalGib)}


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
{gibs(1, ("MOPixel", blood(u), 35, ""), ("MOSRotating", "Gib Bone Small E", 1, ""), ("MOSParticle", gib(u, "Gib Flesh Tiny A"), 2, ""), metalGib)}


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
	d = f"Medieval.rte/Actors/{k.replace(' ', '')}"
	p = f"Medieval {k}"
	groups = "\n".join(f"\tAddToGroup = {g}" for g in ["Actors"] + u["groups"])
	inv = "\n".join(f"\tAddInventory = {cls}\n\t\tCopyOf = {name}" for cls, name in u["inventory"])
	metalGib = ("MOSParticle", "Gib Metal Grey Tiny A", 4, "") if u["metal"] else ("MOSParticle", gib(u, "Gib Flesh Micro A"), 3, "")
	return f"""
///////////////////////////////////////////////////////////////////////
// {u["name"]}


AddActor = AHuman
	PresetName = {u["name"]}
{groups}
	Description = {u["desc"]}{nature(u)}
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
		CopyOf = {"Bone Hit" if "blood" in u else "Human Pain"}
	DeathSound = SoundContainer
		CopyOf = {"Bone Crack" if "blood" in u else "Human Death"}
	DeviceSwitchSound = SoundContainer
		CopyOf = {u["foley"]}
	Health = {u["health"]}
	MaxHealth = {u["health"]}{energy(u)}{speech(u)}
	Organic = {0 if "blood" in u else 1}
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
{gibs(1, ("MOPixel", blood(u), 50, ""), ("MOSRotating", gib(u, "Gib Flesh Small A"), 4, ""), ("MOSRotating", gib(u, "Gib Flesh Small D"), 4, ""), ("MOSParticle", "Gib Bone Micro A", 3, ""), metalGib)}
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
