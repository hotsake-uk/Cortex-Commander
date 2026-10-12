#!/usr/bin/env python3
"""Synthesises the Medieval spells' sounds (Data/Medieval.rte/Sounds/Spells) as FLAC: casting whooshes, the fireball's roar and blast,
the dragon's breath, thunder, shattering ice, arcane zings, the eerie summoning, wind, rumbling earth, the vortex hum, the poison burst
and the time warp's ticking warble. Needs numpy and ffmpeg.

Run from the repository root: python3 Tools/Medieval/spellsounds.py Data
"""
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

OUT = os.path.join(sys.argv[1] if len(sys.argv) > 1 else "Data", "Medieval.rte/Sounds/Spells")
SR = 44100
rng = np.random.default_rng(1215)


def t_of(seconds):
	return np.arange(int(SR * seconds)) / SR


def env(n, attack, decay):
	t = np.arange(n) / SR
	a = np.clip(t / max(attack, 1e-4), 0, 1)
	return a * np.exp(-t / decay)


def bandpass(x, lo, hi):
	spec = np.fft.rfft(x)
	f = np.fft.rfftfreq(len(x), 1 / SR)
	spec[(f < lo) | (f > hi)] *= 0.03
	return np.fft.irfft(spec, len(x))


def lowpass(x, hi):
	return bandpass(x, 0, hi)


def swept_noise(n, centres, width=0.5, block=1024):
	"""Noise band-passed around a centre frequency that changes over time (centres: one per sample)."""
	noise = rng.standard_normal(n + block)
	out = np.zeros(n + block)
	win = np.hanning(block)
	for k in range(0, n, block // 2):
		c = centres[min(k, n - 1)]
		seg = noise[k:k + block]
		out[k:k + block] += bandpass(seg, c * (1 - width), c * (1 + width)) * win
	return out[:n]


def fade(x, fin=0.01, fout=0.05):
	n = len(x)
	a = np.ones(n)
	i = int(SR * fin)
	o = int(SR * fout)
	if i:
		a[:i] = np.linspace(0, 1, i)
	if o:
		a[-o:] *= np.linspace(1, 0, o)
	return x * a


def write(name, x, gain=0.8, faded=True):
	if faded:
		x = fade(x)
	x = x / (np.max(np.abs(x)) + 1e-9) * gain
	os.makedirs(OUT, exist_ok=True)
	with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
		path = tmp.name
	with wave.open(path, "wb") as w:
		w.setnchannels(1)
		w.setsampwidth(2)
		w.setframerate(SR)
		w.writeframes((x * 32767).astype(np.int16).tobytes())
	subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", path, "-sample_fmt", "s16", os.path.join(OUT, name + ".flac")], check=True)
	os.unlink(path)


def whoosh(i):
	# A rush of air as the hand is thrown out: noise swept up and down.
	t = t_of(0.45)
	n = len(t)
	centre = 400 + 1600 * np.sin(np.pi * t / t[-1]) ** 1.5 * (1 + 0.2 * i)
	return swept_noise(n, centre, 0.6) * np.sin(np.pi * t / t[-1]) ** 2


def fire_roar():
	# The fireball leaving the hand: a whoosh with a crackling, rumbling roar under it.
	t = t_of(0.9)
	n = len(t)
	roar = lowpass(rng.standard_normal(n), 500) * env(n, 0.03, 0.35) * 2.5
	crackle = (rng.random(n) > 0.997) * rng.standard_normal(n) * 3
	crackle = bandpass(crackle, 1500, 8000) * env(n, 0.01, 0.4)
	rush = swept_noise(n, 300 + 1500 * np.exp(-t * 4), 0.6) * env(n, 0.01, 0.3)
	return roar + crackle * 2 + rush


def fire_blast(i):
	# A fiery explosion: a deep thump, a roar of noise falling in pitch, and crackling embers.
	t = t_of(1.6)
	n = len(t)
	thump = np.sin(2 * np.pi * (70 - 30 * t) * t) * env(n, 0.002, 0.25) * 2.5
	roar = swept_noise(n, 1800 * np.exp(-t * 3) + 120, 0.7) * env(n, 0.002, 0.5) * 1.6
	crackle = bandpass((rng.random(n) > 0.995) * rng.standard_normal(n) * 4, 1200, 9000) * env(n, 0.05, 0.6)
	body = lowpass(rng.standard_normal(n), 300 + 100 * i) * env(n, 0.003, 0.6) * 2
	return thump + roar + crackle + body


def breath_loop():
	# Dragon's breath: a steady roaring jet of flame, made to loop (no fade in the middle).
	t = t_of(2.0)
	n = len(t)
	jet = bandpass(rng.standard_normal(n), 200, 3000)
	rumble = lowpass(rng.standard_normal(n), 220) * 2
	flutter = 0.75 + 0.25 * np.sin(2 * np.pi * 9 * t) * np.sin(2 * np.pi * 0.5 * t)
	crackle = bandpass((rng.random(n) > 0.996) * rng.standard_normal(n) * 3, 2000, 9000)
	x = (jet + rumble) * flutter + crackle
	# Crossfade the ends so it loops without a click.
	k = int(SR * 0.1)
	x[:k] = x[:k] * np.linspace(0, 1, k) + x[-k:] * np.linspace(1, 0, k)
	return x[: n - k]


def thunder(i):
	# Call Lightning: a sharp crack, then a long rolling rumble.
	t = t_of(2.6)
	n = len(t)
	crack = bandpass(rng.standard_normal(n), 1500, 12000) * env(n, 0.0005, 0.03) * 4
	snap = rng.standard_normal(n) * env(n, 0.0002, 0.008) * 5
	roll_env = env(n, 0.03, 0.9) * (0.6 + 0.4 * np.abs(np.sin(2 * np.pi * (1.3 + 0.4 * i) * t)))
	roll = lowpass(rng.standard_normal(n), 160) * roll_env * 6
	mid = bandpass(rng.standard_normal(n), 150, 900) * env(n, 0.01, 0.4) * 2
	return crack + snap + roll + mid


def ice_shatter(i):
	# Frost shards: a glassy crunch and a shower of bright tinkles.
	t = t_of(0.8)
	n = len(t)
	crunch = bandpass(rng.standard_normal(n), 2500, 11000) * env(n, 0.0005, 0.04) * 2
	tinkle = np.zeros(n)
	for _ in range(14):
		start = int(rng.uniform(0, 0.4) * SR)
		f = rng.uniform(2800, 7000) * (1 + 0.1 * i)
		seg = np.arange(n - start) / SR
		tinkle[start:] += np.sin(2 * np.pi * f * seg) * np.exp(-seg / rng.uniform(0.03, 0.12)) * rng.uniform(0.3, 1)
	return crunch + tinkle * 0.6


def arcane_zing(i):
	# Arcane missiles: a bright rising zing with a shimmer.
	t = t_of(0.5)
	n = len(t)
	f = 600 + 1800 * (t / t[-1]) ** 0.6 * (1 + 0.15 * i)
	phase = 2 * np.pi * np.cumsum(f) / SR
	tone = np.sin(phase) + 0.4 * np.sin(2 * phase + 1) + 0.25 * np.sin(3.01 * phase)
	shimmer = 0.6 + 0.4 * np.sin(2 * np.pi * 30 * t)
	air = bandpass(rng.standard_normal(n), 3000, 10000) * env(n, 0.005, 0.1) * 0.5
	return tone * shimmer * env(n, 0.01, 0.18) + air


def nova():
	# Arcane nova: a deep whump and a ringing, chorused chord blooming outwards.
	t = t_of(1.4)
	n = len(t)
	whump = np.sin(2 * np.pi * (90 - 40 * t) * t) * env(n, 0.003, 0.2) * 2
	chord = sum(np.sin(2 * np.pi * f * t + k) * (1 + 0.003 * np.sin(2 * np.pi * 5 * t)) for k, f in enumerate([330, 495, 660, 990, 1320]))
	chord *= env(n, 0.005, 0.5) * 0.5
	rush = swept_noise(n, 2500 * np.exp(-t * 2) + 300, 0.6) * env(n, 0.002, 0.35)
	return whump + chord + rush


def summon():
	# Raise dead: a low eerie drone, a wailing choir sliding down and a hollow knocking of bones.
	t = t_of(2.6)
	n = len(t)
	drone = sum(np.sin(2 * np.pi * f * t) for f in (55, 82.4, 110.5)) * env(n, 0.5, 2.0)
	wail_f = 520 * np.exp(-t * 0.3) * (1 + 0.02 * np.sin(2 * np.pi * 5.5 * t))
	wail = np.sin(2 * np.pi * np.cumsum(wail_f) / SR) * np.sin(np.pi * t / t[-1]) * 0.5
	wail2 = np.sin(2 * np.pi * np.cumsum(wail_f * 1.19) / SR) * np.sin(np.pi * t / t[-1]) * 0.3
	bones = np.zeros(n)
	for k in range(7):
		start = int((1.0 + 0.12 * k + rng.uniform(0, 0.05)) * SR)
		seg = np.arange(n - start) / SR
		bones[start:] += np.sin(2 * np.pi * rng.uniform(600, 900) * seg) * np.exp(-seg / 0.02)
	return drone * 0.8 + wail + wail2 + bones * 0.5


def wind_howl():
	# Tornado: a howling, whistling wind that swells and wavers, four seconds.
	t = t_of(4.2)
	n = len(t)
	centre = 500 + 300 * np.sin(2 * np.pi * 0.7 * t) + 200 * np.sin(2 * np.pi * 1.9 * t)
	howl = swept_noise(n, centre, 0.25)
	whistle_f = 900 + 250 * np.sin(2 * np.pi * 0.5 * t)
	whistle = np.sin(2 * np.pi * np.cumsum(whistle_f) / SR) * 0.15 * (0.5 + 0.5 * np.sin(2 * np.pi * 0.3 * t))
	rumble = lowpass(rng.standard_normal(n), 120) * 1.5
	shape = np.clip(t / 0.4, 0, 1) * np.clip((t[-1] - t) / 0.8, 0, 1)
	return (howl * 2 + whistle + rumble) * shape


def rumble(i):
	# Earth shatter: a deep grinding rumble with rocks cracking.
	t = t_of(1.8)
	n = len(t)
	low = lowpass(rng.standard_normal(n), 90 + 20 * i) * 6
	grind = bandpass(rng.standard_normal(n), 120, 600) * (0.5 + 0.5 * np.abs(np.sin(2 * np.pi * 7 * t)))
	cracks = np.zeros(n)
	for _ in range(8):
		s = int(rng.uniform(0, 1.2) * SR)
		seg = np.arange(n - s) / SR
		cracks[s:] += bandpass(rng.standard_normal(n - s), 800, 5000) * np.exp(-seg / 0.015) * rng.uniform(0.5, 1.5)
	return (low + grind) * env(n, 0.05, 0.8) + cracks


def meteor_fall():
	# A meteor's descending whistle and roar as it comes down.
	t = t_of(1.2)
	n = len(t)
	f = 1600 * np.exp(-t * 1.2) + 200
	whistle = np.sin(2 * np.pi * np.cumsum(f) / SR) * 0.3
	roar = swept_noise(n, f * 0.8, 0.5)
	shape = np.clip(t / 0.8, 0, 1)
	return (whistle + roar) * shape


def vortex_hum():
	# Gravity well: a deep pulsing hum that pulls in pitch, with a whine over it, three seconds.
	t = t_of(3.2)
	n = len(t)
	f = 60 + 25 * t
	hum = np.sin(2 * np.pi * np.cumsum(f) / SR) * (0.7 + 0.3 * np.sin(2 * np.pi * 6 * t))
	sub = np.sin(2 * np.pi * np.cumsum(f * 0.5) / SR)
	whine = np.sin(2 * np.pi * np.cumsum(400 + 300 * t) / SR) * 0.12
	suck = swept_noise(n, 300 + 200 * t, 0.5) * 0.5
	shape = np.clip(t / 0.2, 0, 1)
	return (hum + sub * 0.8 + whine + suck) * shape


def poison_burst():
	# Poison cloud: a wet splat and a long hiss of gas, with bubbles.
	t = t_of(1.6)
	n = len(t)
	splat = lowpass(rng.standard_normal(n), 900) * env(n, 0.001, 0.06) * 3
	hiss = bandpass(rng.standard_normal(n), 2500, 9000) * env(n, 0.02, 0.7)
	bubbles = np.zeros(n)
	for _ in range(16):
		s = int(rng.uniform(0.05, 1.2) * SR)
		seg = np.arange(min(int(0.06 * SR), n - s)) / SR
		f = rng.uniform(250, 600)
		bubbles[s:s + len(seg)] += np.sin(2 * np.pi * f * (1 + seg * 12) * seg) * np.exp(-seg / 0.02)
	return splat + hiss * 0.8 + bubbles * 0.6


def time_warp():
	# Time warp: a slow warbling, chorused tone that sinks and rises, and a clock ticking under it, five seconds.
	t = t_of(5.2)
	n = len(t)
	warble = 1 + 0.06 * np.sin(2 * np.pi * 0.6 * t)
	base = 220 * warble * (1 - 0.15 * np.sin(np.pi * t / t[-1]))
	tone = sum(np.sin(2 * np.pi * np.cumsum(base * r) / SR) / (1 + j) for j, r in enumerate((1, 1.5, 2.01, 3.0)))
	ticks = np.zeros(n)
	for k in range(int(t[-1] / 0.5)):
		s = int(k * 0.5 * SR)
		seg = np.arange(min(int(0.05 * SR), n - s)) / SR
		f = 2400 if k % 2 == 0 else 1800
		ticks[s:s + len(seg)] += np.sin(2 * np.pi * f * seg) * np.exp(-seg / 0.006) + bandpass(rng.standard_normal(len(seg)), 3000, 8000) * np.exp(-seg / 0.003) * 0.5
	shape = np.clip(t / 0.5, 0, 1) * np.clip((t[-1] - t) / 0.6, 0, 1)
	return tone * shape * 0.5 + ticks * 0.9


def holy_chime(i):
	# A heal or blessing: a soft bright chord of bells blooming, with a breath of air under it.
	t = t_of(1.3)
	n = len(t)
	notes = [523.25, 659.25, 783.99, 1046.5] if i == 0 else [587.33, 739.99, 880.0, 1174.7]
	chord = np.zeros(n)
	for k, f in enumerate(notes):
		start = int(0.06 * k * SR)
		seg = t[: n - start]
		chord[start:] += (np.sin(2 * np.pi * f * seg) + 0.3 * np.sin(2 * np.pi * f * 2.01 * seg)) * np.exp(-seg / 0.6) * (1 - 0.15 * k)
	breath = bandpass(rng.standard_normal(n), 2000, 7000) * env(n, 0.1, 0.4) * 0.25
	return chord * 0.6 + breath


def buff_shimmer(i):
	# An enchantment settling on someone: a rising glissando of sparkle, chorused.
	t = t_of(0.9)
	n = len(t)
	f = 500 + 1400 * (t / t[-1]) ** 0.8 * (1 + 0.1 * i)
	phase = 2 * np.pi * np.cumsum(f) / SR
	tone = np.sin(phase) + 0.5 * np.sin(phase * 1.5 + 0.3) + 0.3 * np.sin(phase * 2.0)
	sparkle = (rng.random(n) > 0.993) * rng.standard_normal(n) * 3
	sparkle = bandpass(sparkle, 3000, 9000) * env(n, 0.05, 0.4)
	return tone * env(n, 0.02, 0.3) * 0.5 + sparkle * 0.5


def blink_pop():
	# A blink or a mirror image: a quick suck of air inward and a pop outward.
	t = t_of(0.5)
	n = len(t)
	suck = swept_noise(n, 3000 * (1 - t / t[-1]) ** 2 + 200, 0.5) * np.clip(1 - t / 0.2, 0, 1)
	pop_start = int(0.2 * SR)
	seg = t[: n - pop_start]
	pop = np.zeros(n)
	pop[pop_start:] = np.sin(2 * np.pi * (300 - 200 * seg) * seg) * np.exp(-seg / 0.05) * 2 + bandpass(rng.standard_normal(len(seg)), 1500, 6000) * np.exp(-seg / 0.08)
	return suck * 0.6 + pop


def stone_grind():
	# Stoneskin: a grinding of rock with a deep thud as it sets.
	t = t_of(1.0)
	n = len(t)
	grind = bandpass(rng.standard_normal(n), 200, 1200) * (0.6 + 0.4 * np.sin(2 * np.pi * 23 * t)) * env(n, 0.05, 0.3)
	thud = np.sin(2 * np.pi * (60 - 20 * t) * t) * env(n, 0.002, 0.2) * 2
	return grind + thud


def dispel():
	# Dispel: a falling, dissolving chord with a glassy break.
	t = t_of(1.1)
	n = len(t)
	base = 880 * np.exp(-t * 1.2)
	tone = sum(np.sin(2 * np.pi * np.cumsum(base * r) / SR) / (1 + j) for j, r in enumerate((1, 1.26, 1.5, 2.0)))
	glass = bandpass((rng.random(n) > 0.996) * rng.standard_normal(n) * 4, 2500, 9000) * env(n, 0.01, 0.35)
	return tone * env(n, 0.01, 0.4) * 0.5 + glass * 0.6


def grab():
	# Telekinetic grab: a deep pulsing drone yanked downward, crackling.
	t = t_of(1.6)
	n = len(t)
	f = 160 * (1 - 0.4 * t / t[-1]) * (1 + 0.05 * np.sin(2 * np.pi * 9 * t))
	drone = np.sin(2 * np.pi * np.cumsum(f) / SR) + 0.5 * np.sin(2 * np.pi * np.cumsum(f * 2.02) / SR)
	crackle = bandpass((rng.random(n) > 0.995) * rng.standard_normal(n) * 3, 1000, 6000) * env(n, 0.02, 0.8)
	return drone * env(n, 0.03, 0.9) * 0.7 + crackle * 0.5


def main():
	for i in range(2):
		write(f"CastWhoosh{i + 1}", whoosh(i), 0.5)
		write(f"FireBlast{i + 1}", fire_blast(i), 0.9)
		write(f"Thunder{i + 1}", thunder(i), 0.95)
		write(f"IceShatter{i + 1}", ice_shatter(i), 0.6)
		write(f"Rumble{i + 1}", rumble(i), 0.85)
	for i in range(3):
		write(f"ArcaneZing{i + 1}", arcane_zing(i), 0.45)
	write("FireRoar1", fire_roar(), 0.7)
	write("BreathLoop1", breath_loop(), 0.55, faded=False)
	write("Nova1", nova(), 0.75)
	write("Summon1", summon(), 0.7)
	write("WindHowl1", wind_howl(), 0.7)
	write("MeteorFall1", meteor_fall(), 0.6)
	write("VortexHum1", vortex_hum(), 0.7)
	write("PoisonBurst1", poison_burst(), 0.65)
	write("TimeWarp1", time_warp(), 0.6)
	# The support spells'.
	for i in range(2):
		write(f"HolyChime{i + 1}", holy_chime(i), 0.6)
		write(f"BuffShimmer{i + 1}", buff_shimmer(i), 0.5)
	write("BlinkPop1", blink_pop(), 0.7)
	write("StoneGrind1", stone_grind(), 0.7)
	write("Dispel1", dispel(), 0.6)
	write("Grab1", grab(), 0.7)
	print("spell sounds done")


if __name__ == "__main__":
	main()
