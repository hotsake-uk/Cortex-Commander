#!/usr/bin/env python3
"""Synthesises Medieval.rte's sounds (swings, clashes, cuts, bows) as FLAC."""
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

OUT = os.path.join(sys.argv[1], "Medieval.rte/Sounds")
SR = 44100
rng = np.random.default_rng(1066)


def env(n, attack, decay):
	t = np.arange(n) / SR
	a = np.clip(t / max(attack, 1e-4), 0, 1)
	return a * np.exp(-t / decay)


def bandpass(x, lo, hi):
	spec = np.fft.rfft(x)
	f = np.fft.rfftfreq(len(x), 1 / SR)
	spec[(f < lo) | (f > hi)] *= 0.05
	return np.fft.irfft(spec, len(x))


def write(name, x, gain=0.8):
	x = x / (np.max(np.abs(x)) + 1e-9) * gain
	os.makedirs(OUT, exist_ok=True)
	with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
		path = tmp.name
	with wave.open(path, "wb") as w:
		w.setnchannels(1)
		w.setsampwidth(2)
		w.setframerate(SR)
		w.writeframes((x * 32767).astype(np.int16).tobytes())
	subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", path, os.path.join(OUT, name + ".flac")], check=True)
	os.unlink(path)


def swing(i):
	n = int(SR * 0.3)
	t = np.arange(n) / SR
	noise = rng.standard_normal(n)
	# A whoosh: noise swept up then down in pitch as the blade passes.
	out = np.zeros(n)
	centre = 500 + 900 * np.sin(np.pi * t / t[-1]) * (1 + 0.15 * i)
	for k in range(0, n, 512):
		seg = noise[k:k + 1024]
		c = centre[min(k, n - 1)]
		out[k:k + len(seg)] += bandpass(seg, c * 0.6, c * 1.6)[:len(seg)] * np.hanning(len(seg))
	shape = np.sin(np.pi * t / t[-1]) ** 2
	return out * shape


def clash(i):
	n = int(SR * 0.9)
	t = np.arange(n) / SR
	base = [1180, 1720, 2650][i]
	ratios = [1.0, 2.76, 5.40, 8.93, 1.51]
	x = sum(np.sin(2 * np.pi * base * r * t + rng.uniform(0, 6)) * np.exp(-t / (0.35 / (1 + 0.4 * j))) / (1 + j * 0.6) for j, r in enumerate(ratios))
	click = rng.standard_normal(n) * env(n, 0.0005, 0.012)
	return x * env(n, 0.001, 0.4) + click * 1.5


def cut(i):
	n = int(SR * 0.25)
	t = np.arange(n) / SR
	thud = np.sin(2 * np.pi * (140 - 60 * t / t[-1]) * t) * env(n, 0.002, 0.06)
	slash = bandpass(rng.standard_normal(n), 900, 5000) * env(n, 0.001, 0.035 + 0.01 * i)
	return thud * 1.2 + slash


def pluck(freq, dur, damp=0.996):
	n = int(SR * dur)
	period = int(SR / freq)
	buf = rng.uniform(-1, 1, period)
	out = np.zeros(n)
	for k in range(n):
		out[k] = buf[k % period]
		buf[k % period] = damp * 0.5 * (buf[k % period] + buf[(k + 1) % period])
	return out


def bow(i):
	n = int(SR * 0.45)
	t = np.arange(n) / SR
	string = pluck([118, 132][i], 0.45)
	thump = np.sin(2 * np.pi * 70 * t) * env(n, 0.001, 0.04)
	return string * env(n, 0.001, 0.18) + thump * 0.8


def crossbow():
	n = int(SR * 0.4)
	t = np.arange(n) / SR
	clack = bandpass(rng.standard_normal(n), 1500, 7000) * env(n, 0.0005, 0.01)
	string = pluck(210, 0.4, 0.993) * env(n, 0.001, 0.09)
	thump = np.sin(2 * np.pi * 90 * t) * env(n, 0.001, 0.03)
	return clack * 2 + string + thump


def draw():
	# A bowstring drawn back: a creak of wood and cord.
	n = int(SR * 0.45)
	t = np.arange(n) / SR
	creak = bandpass(rng.standard_normal(n), 300, 1400) * (0.5 + 0.5 * np.sign(np.sin(2 * np.pi * 38 * t)))
	return creak * np.sin(np.pi * t / t[-1]) * 0.6


def thunk(i):
	n = int(SR * 0.15)
	t = np.arange(n) / SR
	knock = np.sin(2 * np.pi * [380, 330][i] * t) * env(n, 0.0005, 0.03)
	tick = bandpass(rng.standard_normal(n), 2000, 6000) * env(n, 0.0002, 0.006)
	return knock + tick


for i in range(3):
	write(f"Swing{i + 1}", swing(i), 0.55)
	write(f"Clash{i + 1}", clash(i), 0.7)
for i in range(2):
	write(f"Cut{i + 1}", cut(i), 0.8)
	write(f"Bow{i + 1}", bow(i), 0.7)
	write(f"ArrowHit{i + 1}", thunk(i), 0.6)
write("Crossbow1", crossbow(), 0.75)
write("BowDraw1", draw(), 0.5)
print("sounds done")
