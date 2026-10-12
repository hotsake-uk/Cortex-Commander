#!/usr/bin/env python3
"""Generates FellLegion.rte's sprites in the game palette, on top of Tools/Medieval/sprites.py.

The Fell Legion are the Medieval kingdom's dark mirror, so most of their units are the Medieval drawings in black iron, crimson, purple
and sickly green (sprites.py's swap of the same ASCII grids), with a few heads of their own: the Dread Lord's horned helm, the
Executioner's hood, the Lich's skull. Their weapons are the Medieval weapons' grids in black iron with green steel, plus the Legion's
own: the Executioner's axe, the barbed flail and the bone javelin. Run from the repository root: python3 Tools/FellLegion/sprites.py Data
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Medieval"))
sys.argv = [sys.argv[0], sys.argv[1] if len(sys.argv) > 1 else "Data"]

from PIL import Image

import sprites as S
import arsenal as A
import spells as P

DATA = sys.argv[1]
S.OUT = os.path.join(DATA, "FellLegion.rte")
A.OUT = os.path.join(DATA, "FellLegion.rte/Devices/Arsenal")
P.OUT = os.path.join(DATA, "FellLegion.rte/Devices/Spells")

# The Legion's own colours, added to the Medieval letters: black iron (1 darkest to 5, 9 the highlight) and sickly green (6 to 8).
DARK = {
	"1": (16, 16, 20), "2": (36, 36, 44), "3": (62, 62, 72), "4": (92, 92, 104), "5": (128, 128, 140), "9": (164, 164, 176),
	"6": (26, 70, 34), "7": (56, 140, 60), "8": (120, 220, 100),
}
S.COLORS.update(DARK)
A.COLORS.update(DARK)
A.COLORS.update({"p": S.COLORS["p"], "P": S.COLORS["P"], "q": S.COLORS["q"], "1": DARK["1"], "6": DARK["6"], "7": DARK["7"], "8": DARK["8"]})
S.RAMPS["iron"] = [(10, 10, 12), DARK["1"], DARK["2"], DARK["3"], DARK["4"], DARK["5"]]
S.RAMPS["sickly"] = [(14, 40, 20), DARK["6"], (40, 100, 46), DARK["7"], DARK["8"]]

swap = S.swap
IRON = {"k": "1", "d": "2", "s": "3", "m": "4", "l": "5", "H": "9"}  # Steel to black iron.
GREEN_TRIM = {"y": "6", "g": "7", "G": "8"}  # Gold to green steel.
BLACK_CLOTH = {"r": "1", "R": "2", "o": "3"}  # The kingdom's red to black.
PURPLE_CLOTH = {"r": "p", "R": "P", "o": "q"}
GREEN_PLUME = {"r": "6", "R": "7", "o": "8"}

T, H = S.TORSOS, S.HEADS

# Dread Lord: black plate, a crimson tabard with a green device, and a horned helm with a burning slit.
T["DreadLord"] = swap(T["Knight"], {**IRON, **GREEN_TRIM})
H["DreadLord"] = [
	".Z.........Z.",
	".zZ.......Zz.",
	"..zZ11111Zz..",
	"..123455991..",
	"..1234455991.",
	"..1234455991.",
	"..123ooooooo1",
	"..1233455591.",
	"..1233457891.",
	"..12233447441",
	"...122434x3x1",
	"...112233441.",
	"....1111111..",
	".....123.....",
]
# Black Knight: black plate, black tabard with a green device, a green plume.
T["BlackKnight"] = swap(T["Knight"], {**IRON, **GREEN_TRIM, **BLACK_CLOTH})
H["BlackKnight"] = swap(H["Knight"], {**IRON, **GREEN_PLUME})
H["BlackKnight B"] = swap(H["Knight B"], IRON)
# Death Knight: black plate under a purple tabard, a purple plume.
T["DeathKnight"] = swap(T["Knight"], {**IRON, **GREEN_TRIM, **PURPLE_CLOTH})
H["DeathKnight"] = swap(H["Knight"], {**IRON, **PURPLE_CLOTH})
# Executioner: bare-chested like the Berserker, a black fur mantle, and a black hood with eye slits.
T["Executioner"] = swap(T["Berserker"], {"L": "1", "M": "2"})
H["Executioner"] = [
	".............",
	"......111....",
	".....12221...",
	"....1222221..",
	"...122222221.",
	"..12222222221",
	"..122222a2a21",
	"..12222222221",
	"..12222222221",
	"..12222222221",
	"...1222222221",
	"...1122222211",
	"....11111111.",
	".....tTt.....",
]
# Cultist: black robes with a crimson sash, a hood lined in crimson, red eyes in a pale face.
T["Cultist"] = swap(T["Witch"], {"d": "1", "s": "2", "e": "R", "f": "r", "F": "R"})
H["Cultist"] = swap(H["Necromancer"], {"d": "1", "p": "r", "e": "o"})
# Dark Archer: black hooded cloth, the belt still leather.
T["DarkArcher"] = swap(T["Archer"], {"f": "1", "F": "2", "e": "3"})
H["DarkArcher"] = swap(H["Archer"], {"f": "1", "F": "2", "e": "3"})
H["DarkArcher B"] = swap(H["Archer B"], {"f": "1", "F": "2", "e": "3"})
# Marauder: dark mail under a crimson surcoat.
T["Marauder"] = swap(T["Footman"], {"c": "2", "C": "3", "v": "r", "V": "R", "i": "o", "k": "1"})
H["Marauder"] = swap(H["Footman"], {"c": "2", "C": "3", "k": "1"})
H["Marauder B"] = swap(H["Footman B"], {"c": "2", "C": "3", "k": "1"})
# Shadow: black leathers head to foot.
T["Shadow"] = swap(T["Skirmisher"], {"b": "1", "n": "2", "N": "3", "j": "4"})
H["Shadow"] = swap(H["Skirmisher"], {"b": "1", "n": "2", "N": "3", "h": "k"})
H["Shadow B"] = swap(H["Skirmisher B"], {"b": "1", "n": "2", "N": "3", "h": "k"})
# Warlock: purple robes with green trim.
T["Warlock"] = swap(T["Mage"], {"v": "p", "V": "P", "i": "q", "g": "7", "G": "8"})
H["Warlock"] = swap(H["Mage"], {"v": "p", "V": "P", "i": "q", "g": "7", "G": "8"})
# Lich: a skull with green fire in its sockets over black robes with purple.
T["Lich"] = swap(T["Necromancer"], {"d": "1", "s": "2"})
H["Lich"] = swap(H["Skeleton"], {"x": "8"})
# Plaguebearer: a gambeson stained sickly green, black cap.
T["Plaguebearer"] = swap(T["Crossbowman"], {"W": "6", "w": "7", "r": "1", "R": "2", "o": "3"})
H["Plaguebearer"] = swap(H["Crossbowman"], {"W": "6", "w": "7", **IRON})
# Hellfire Mage: black robes with a green flame trim.
T["HellfireMage"] = swap(T["Mage"], {"v": "1", "V": "2", "i": "3", "g": "7", "G": "8"})
H["HellfireMage"] = swap(H["Mage"], {"v": "1", "V": "2", "i": "3", "g": "7", "G": "8", "a": "h"})

UNITS = {
	# name: torso, heads, arm ramp, leg ramp, foot ramp, hand ramp
	"DreadLord": ("DreadLord", ["DreadLord"], "iron", "iron", "iron", "iron"),
	"BlackKnight": ("BlackKnight", ["BlackKnight", "BlackKnight B"], "iron", "iron", "iron", "iron"),
	"DeathKnight": ("DeathKnight", ["DeathKnight", "BlackKnight B"], "iron", "iron", "iron", "iron"),
	"Executioner": ("Executioner", ["Executioner"], "skin", "black", "black", None),
	"Cultist": ("Cultist", ["Cultist"], "black", "black", "black", None),
	"DarkArcher": ("DarkArcher", ["DarkArcher", "DarkArcher B"], "black", "brown", "leather", None),
	"Marauder": ("Marauder", ["Marauder", "Marauder B"], "iron", "brown", "leather", "leather"),
	"Shadow": ("Shadow", ["Shadow", "Shadow B"], "black", "black", "black", "black"),
	"Warlock": ("Warlock", ["Warlock"], "purple", "purple", "black", None),
	"Lich": ("Lich", ["Lich"], "black", "black", "black", "bone"),
	"Plaguebearer": ("Plaguebearer", ["Plaguebearer"], "sickly", "brown", "leather", "leather"),
	"HellfireMage": ("HellfireMage", ["HellfireMage"], "black", "black", "black", None),
}

# ---------------------------------------------------------------------------
# Weapons

BLACK_STEEL = {**IRON, **GREEN_TRIM}
BLACK_BLADE = swap(S.SWORD, BLACK_STEEL)
WICKED_DAGGER = swap(S.DAGGER, BLACK_STEEL)
CURSED_GREATSWORD = swap(A.GREATSWORD, {**BLACK_STEEL, "W": "8"})
DARK_BOW = swap(S.BOW, {"b": "1", "n": "2", "N": "3", "j": "4", "w": "4"})
BLACK_ARROW_NOCKED = swap(S.ARROW_NOCKED, {"w": "2", "R": "1", "n": "2", "m": "4", "H": "9"})
SPIKED_SHIELD = swap(S.KITE, {**IRON, "R": "1", "g": "7", "G": "8"})
PLAGUE_POT = swap(A.STINKPOT, {"f": "p", "F": "P", "e": "q", "E": "q"})
HELLFIRE_POT = swap(A.FIREPOT, {"R": "6", "o": "7", "O": "8"})

# A headsman's axe: a long dark haft and one great crescent blade.
EXECUTIONER_AXE = [
	"." * 23 + "155" + "." * 4,
	"." * 22 + "1455" + "." * 4,
	"." * 22 + "13455" + "." * 3,
	"." * 22 + "123455" + "." * 2,
	"." * 22 + "1234559" + ".",
	"bnbn" * 5 + "bn" + "12344559",
	"nbnb" * 5 + "nb" + "12344559",
	"." * 22 + "1234559" + ".",
	"." * 22 + "123455" + "." * 2,
	"." * 22 + "13455" + "." * 3,
	"." * 23 + "155" + "." * 4,
]

# A barbed flail: a short haft, three links of chain and a spiked iron ball.
BARBED_FLAIL = [
	"." * 16 + "1" + "." * 3,
	"." * 15 + "121" + "..",
	"." * 14 + "12221" + ".",
	"." * 13 + "1223321",
	"jnNnNnNn" + "1.1.1." + "22332" + "1",
	"bnbnbnbn" + "....." + "1223321",
	"." * 14 + "12221" + ".",
	"." * 15 + "121" + "..",
	"." * 16 + "1" + "." * 3,
]

# A javelin of sharpened bone.
BONE_JAVELIN = [
	"." * 25 + "zZ" + "...",
	"zZ" * 13 + "ZZZZ",
	"." * 25 + "zZ" + "...",
]

# The module icon: the kingdom's shield in black with a green cross.
ICON = swap(S.ICON, {**IRON, "R": "1", "g": "7", "G": "8"})

# ---------------------------------------------------------------------------
# Spells: books and glows, with Tools/Medieval/spells.py's drawing.

P.EMBLEMS["bone"] = ["c...c", ".c.c.", "..b..", ".c.c.", "c...c"]
P.EMBLEMS["rain"] = ["b.b.b", ".b.b.", "b.b.b", ".b.b.", "..a.."]
P.EMBLEMS["chain"] = ["b...b", "bc.cb", ".bab.", "bc.cb", "b...b"]
P.EMBLEMS["doom"] = [".bbb.", "bcccb", "bcacb", "bcccb", ".bbb."]
P.EMBLEMS["shadow"] = ["..b..", ".bcb.", "bcacb", ".bcb.", "..b.."]

BLACK_COVER = [(14, 12, 16), (30, 28, 34), (56, 52, 62)]
GREEN_IRON = [(26, 70, 34), (56, 140, 60), (120, 220, 100)]


def make_spells():
	os.makedirs(P.OUT, exist_ok=True)
	# Shadow Bolt: a black book with a violet star.
	P.book("ShadowBolt.png", BLACK_COVER, P.IRON, "shadow", [(120, 60, 180), (180, 100, 240), (240, 200, 255)])
	# Bone Spear: a bone-bound grimoire with crossed bones.
	P.book("BoneSpear.png", [(40, 38, 32), (70, 66, 56), (100, 94, 80)], P.BONE, "bone", [(150, 140, 110), (206, 198, 168), (240, 236, 214)])
	# Hellfire Rain: a big black tome bound in green iron, fire falling on its cover.
	P.book("HellfireRain.png", BLACK_COVER, GREEN_IRON, "rain", [(200, 250, 140), (56, 140, 60), (120, 220, 100)], w=14, h=10)
	# Shadowflame: a scroll with black ends and a green seal.
	P.scroll("Shadowflame.png", [(90, 80, 70), (130, 120, 100), (160, 150, 130)], [(20, 20, 24), (50, 50, 60)], [(26, 70, 34), (56, 140, 60), (120, 220, 100)], ribbon=[(40, 40, 48), (20, 20, 24)])
	# Doom: a crimson book with a black eye.
	P.book("Doom.png", [(70, 10, 14), (120, 20, 24), (170, 40, 40)], P.IRON, "doom", [(230, 60, 40), (20, 16, 20), (40, 30, 36)])
	# Chains of the Damned: an iron-bound black tome with chain links.
	P.book("ChainsOfTheDamned.png", BLACK_COVER, P.IRON, "chain", [(60, 62, 70), (110, 114, 124), (170, 174, 184)], w=13, h=10)

	# The shadow bolt: a dark orb with a violet heart.
	def shadow_color(r, ang, f, n, dx, dy):
		import math
		edge = r + 0.2 * (n - 0.5) + 0.08 * math.sin(ang * 4 + f * 1.9)
		if edge > 1.0:
			return None
		if edge < 0.3:
			return (230, 190, 255)
		if edge < 0.6:
			return (120, 50, 190)
		return (40, 16, 60)
	P.orb_frames("ShadowOrb", 7, 4, shadow_color, 1313)

	# Hellfire: a green-white fireball.
	def hellfire_color(r, ang, f, n, dx, dy):
		import math
		edge = r + 0.18 * (n - 0.5) + 0.08 * math.sin(ang * 5 + f * 1.7)
		if edge > 1.0:
			return None
		if edge < 0.35:
			return (230, 255, 200)
		if edge < 0.6:
			return (120, 230, 90)
		if edge < 0.85:
			return (40, 140, 50)
		return (20, 60, 30)
	P.orb_frames("HellfireOrb", 9, 4, hellfire_color, 666)

	# Glows: green hellfire, violet shadow, the doom rune's red, and the green rune circle.
	P.glow("GlowHellfireSmall.png", 15, (230, 255, 200), (60, 220, 70))
	P.glow("GlowHellfireBig.png", 41, (230, 255, 200), (60, 220, 70))
	P.glow("GlowHellfireHuge.png", 121, (230, 255, 200), (60, 220, 70), 1.6)
	P.glow("GlowShadowSmall.png", 15, (200, 150, 255), (70, 20, 120))
	P.glow("GlowShadowBig.png", 41, (200, 150, 255), (70, 20, 120))
	P.glow("GlowDoomHuge.png", 101, (255, 200, 180), (230, 30, 20), 1.6)
	P.ring_glow("GlowRuneHell.png", 61, 17, (90, 255, 90), 0.14)
	P.rune_circle("RuneCircleHell.png", (20, 60, 30), (56, 140, 60), (160, 255, 140))


def main():
	for name, rows in T.items():
		S.check("torso " + name, rows, 13, 17)
	for name, rows in H.items():
		S.check("head " + name, rows, 13, 14)
	S.check("executioner axe", EXECUTIONER_AXE, 30, 11)
	S.check("barbed flail", BARBED_FLAIL, 20, 9)
	S.check("bone javelin", BONE_JAVELIN, 30, 3)

	for unit, (torso, heads, armRamp, legRamp, footRamp, handRamp) in UNITS.items():
		base = f"Actors/{unit}/"
		S.save(S.grid(T[torso]), base + "Torso.png")
		for i, h in enumerate(heads):
			S.save(S.grid(H[h]), base + f"Head{i:03d}.png")
		for part, frames in (("ArmFGA", 5), ("ArmBGA", 5), ("LegFGA", 5), ("LegBGA", 5), ("FootFGA", 4), ("FootBGA", 4)):
			ramp = S.RAMPS[armRamp if part.startswith("Arm") else legRamp if part.startswith("Leg") else footRamp]
			for f in range(frames):
				src = os.path.join(S.RONIN, f"{part}{f:03d}.png")
				S.save(S.recolor(src, ramp), base + f"{part}{f:03d}.png")
		for hand in ("HandFGA", "HandFGB"):
			src = os.path.join(S.RONIN, hand + ".png")
			if handRamp:
				S.save(S.recolor(src, S.RAMPS[handRamp]), base + hand + ".png")
			else:
				S.save(Image.open(src).convert("RGBA"), base + hand + ".png")

	S.save(S.grid(BLACK_BLADE), "Devices/BlackBlade/BlackBlade.png")
	S.save(S.grid(WICKED_DAGGER), "Devices/WickedDagger/WickedDagger.png")
	S.save(S.grid(CURSED_GREATSWORD, A.COLORS), "Devices/CursedGreatsword/CursedGreatsword.png")
	S.save(S.grid(EXECUTIONER_AXE), "Devices/ExecutionersAxe/ExecutionersAxe.png")
	S.save(S.grid(BARBED_FLAIL), "Devices/BarbedFlail/BarbedFlail.png")
	S.save(S.grid(DARK_BOW), "Devices/DarkLongbow/DarkLongbow.png")
	S.save(S.grid(BLACK_ARROW_NOCKED), "Devices/DarkLongbow/BlackArrowNocked.png")
	for i, img in enumerate(S.arrow_frames(11, 2, 2, DARK["2"])):
		S.save(img, f"Devices/DarkLongbow/BlackArrow{i:03d}.png")
	S.save(S.grid(SPIKED_SHIELD), "Devices/SpikedShield/SpikedShield.png")
	# The Bone Spear's flight frames: an arrow of bone, longer and headed, in the arrow's 15 pixel frame.
	for i, img in enumerate(S.arrow_frames(13, 3, 0, S.COLORS["z"])):
		S.save(img, f"Devices/Spells/BoneSpear{i:03d}.png")
	S.save(S.grid(BONE_JAVELIN), "Devices/BoneJavelin/BoneJavelin.png")
	S.save(S.grid(PLAGUE_POT, A.COLORS), "Devices/Arsenal/PlaguePot.png")
	S.save(S.grid(HELLFIRE_POT, A.COLORS), "Devices/Arsenal/HellfirePot.png")
	S.save(S.grid(ICON), "ModuleIcon.png")
	make_spells()
	print("fell legion sprites done")


if __name__ == "__main__":
	main()
