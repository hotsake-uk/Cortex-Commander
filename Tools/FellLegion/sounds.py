#!/usr/bin/env python3
"""Synthesises FellLegion.rte/Sounds as FLAC (needs numpy and ffmpeg), with Tools/Medieval/spellsounds.py's helpers: the Doom's toll and
burst, the chains' rattle, the shadow bolt, the life drain, the bone spear's throw and the hellfire's fall.
Run from the repository root: python3 Tools/FellLegion/sounds.py Data
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Medieval"))
sys.argv = [sys.argv[0], sys.argv[1] if len(sys.argv) > 1 else "Data"]

import numpy as np

import spellsounds as P

P.OUT = os.path.join(sys.argv[1], "FellLegion.rte/Sounds")
SR = P.SR
rng = np.random.default_rng(1666)


def doom_toll():
	# A low cracked bell, beating against itself, with a breath of wind under it.
	t = P.t_of(1.6)
	n = len(t)
	bell = np.zeros(n)
	for freq, amp, decay in ((98, 1.0, 1.2), (147, 0.5, 0.9), (196, 0.35, 0.6), (261, 0.25, 0.5), (392, 0.15, 0.3)):
		bell += amp * np.sin(2 * np.pi * freq * t + 0.3 * np.sin(2 * np.pi * 3.1 * t)) * np.exp(-t / decay)
	beat = 1 + 0.25 * np.sin(2 * np.pi * 4.5 * t)
	wind = P.bandpass(rng.standard_normal(n), 120, 500) * P.env(n, 0.3, 0.9) * 0.3
	return bell * beat * P.env(n, 0.005, 1.5) + wind


def doom_burst():
	# The mark bursting: a deep thud, a tearing crack, and a dark rush fading.
	t = P.t_of(1.2)
	n = len(t)
	thud = np.sin(2 * np.pi * (60 + 40 * np.exp(-t * 12)) * t) * np.exp(-t * 6)
	crack = P.bandpass(rng.standard_normal(n), 800, 5000) * np.exp(-t * 25)
	rush = P.swept_noise(n, np.interp(t, [0, 0.4, 1.2], [1800, 400, 120]), 0.5) * P.env(n, 0.02, 0.8)
	return thud * 1.2 + crack * 0.8 + rush * 0.6


def chain_rattle():
	# Chains dragged out of the ground: a run of metallic clinks over a grinding rumble.
	t = P.t_of(1.1)
	n = len(t)
	out = P.bandpass(rng.standard_normal(n), 60, 250) * P.env(n, 0.05, 0.7) * 0.5
	for i in range(14):
		start = int((0.03 + i * 0.065 + rng.uniform(0, 0.02)) * SR)
		length = int(0.08 * SR)
		if start + length > n:
			break
		tt = np.arange(length) / SR
		freq = rng.uniform(1800, 3400)
		clink = (np.sin(2 * np.pi * freq * tt) + 0.5 * np.sin(2 * np.pi * freq * 2.7 * tt)) * np.exp(-tt * 60)
		out[start:start + length] += clink * rng.uniform(0.4, 0.8)
	return out


def shadow_bolt(i):
	# A dark zing: a falling whistle with a hollow low under it.
	t = P.t_of(0.55)
	n = len(t)
	f = 1400 - 900 * (t / t[-1]) ** 0.7 * (1 + 0.1 * i)
	phase = 2 * np.pi * np.cumsum(f) / SR
	tone = np.sin(phase) + 0.5 * np.sin(0.5 * phase)
	low = np.sin(2 * np.pi * 70 * t) * P.env(n, 0.02, 0.3) * 0.6
	air = P.bandpass(rng.standard_normal(n), 300, 2000) * P.env(n, 0.01, 0.2) * 0.4
	return tone * P.env(n, 0.01, 0.25) + low + air


def life_drain():
	# Life drawn up a thread: a rising, warbling slurp.
	t = P.t_of(0.5)
	n = len(t)
	f = 200 + 700 * (t / t[-1]) ** 1.5
	phase = 2 * np.pi * np.cumsum(f) / SR
	tone = np.sin(phase + 0.4 * np.sin(2 * np.pi * 18 * t)) * P.env(n, 0.05, 0.15)
	return tone + P.bandpass(rng.standard_normal(n), 2000, 6000) * P.env(n, 0.2, 0.1) * 0.25


def bone_throw():
	# A short hard whoosh with a dry click at the start.
	t = P.t_of(0.4)
	n = len(t)
	click = P.bandpass(rng.standard_normal(n), 2500, 7000) * np.exp(-t * 90) * 0.8
	whoosh = P.swept_noise(n, np.interp(t, [0, 0.15, 0.4], [600, 2400, 900]), 0.4) * P.env(n, 0.03, 0.2)
	return click + whoosh


def hellfire_fall():
	# Fire falling out of the sky: a descending whistle over a growing crackling roar.
	t = P.t_of(1.3)
	n = len(t)
	f = 1800 - 1300 * (t / t[-1])
	phase = 2 * np.pi * np.cumsum(f) / SR
	whistle = np.sin(phase) * P.env(n, 0.1, 0.5) * 0.4
	roar = P.bandpass(rng.standard_normal(n), 150, 1200) * (t / t[-1]) ** 1.5
	crackle = (rng.random(n) < 0.004).astype(float) * rng.uniform(0.3, 1, n)
	crackle = P.lowpass(crackle, 6000) * (t / t[-1])
	return whistle + roar * 0.8 + crackle * 0.5


def main():
	P.write("DoomToll1", doom_toll(), 0.8)
	P.write("DoomBurst1", doom_burst(), 0.9)
	P.write("ChainRattle1", chain_rattle(), 0.7)
	for i in range(2):
		P.write(f"ShadowBolt{i + 1}", shadow_bolt(i), 0.5)
	P.write("LifeDrain1", life_drain(), 0.45)
	P.write("BoneThrow1", bone_throw(), 0.6)
	P.write("HellfireFall1", hellfire_fall(), 0.7)
	print("fell legion sounds done")


if __name__ == "__main__":
	main()
