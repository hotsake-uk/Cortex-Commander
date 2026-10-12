#!/usr/bin/env python3
"""Synthesises the Medieval arsenal's sounds (Sounds/Arsenal) as FLAC: pots shattering, greek fire's whoosh and roar, the stinkpot's gassy
burst, the bolas' whirr and wrap, caltrops scattering and biting, heavy blunt hits and swings, and the Holy Hand Grenade's choir
("Hal-le-lu-jah") and its blast. Needs numpy and ffmpeg. Seeded, so it can be re-run.

Run from the repository root: python3 Tools/Medieval/arsenalsounds.py Data
"""
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

OUT = os.path.join(sys.argv[1] if len(sys.argv) > 1 else "Data", "Medieval.rte/Sounds/Arsenal")
SR = 44100
rng = np.random.default_rng(1191)


def env(n, attack, decay):
	t = np.arange(n) / SR
	a = np.clip(t / max(attack, 1e-4), 0, 1)
	return a * np.exp(-t / decay)


def band(x, lo, hi):
	spec = np.fft.rfft(x)
	f = np.fft.rfftfreq(len(x), 1 / SR)
	spec[(f < lo) | (f > hi)] *= 0.03
	return np.fft.irfft(spec, len(x))


def at(n, start, x):
	"""x placed into n samples from start (seconds)."""
	out = np.zeros(n)
	s = int(start * SR)
	if s >= n:
		return out
	m = min(len(x), n - s)
	out[s:s + m] = x[:m]
	return out


def write(name, x, gain=0.8):
	x = x - np.mean(x)
	x = x / (np.max(np.abs(x)) + 1e-9) * gain
	# A few ms of fade at each end so nothing clicks.
	f = int(SR * 0.004)
	x[:f] *= np.linspace(0, 1, f)
	x[-f:] *= np.linspace(1, 0, f)
	os.makedirs(OUT, exist_ok=True)
	with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
		path = tmp.name
	with wave.open(path, "wb") as w:
		w.setnchannels(1)
		w.setsampwidth(2)
		w.setframerate(SR)
		w.writeframes((x * 32767).astype(np.int16).tobytes())
	subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", path, "-fflags", "+bitexact", "-flags:a", "+bitexact", os.path.join(OUT, name + ".flac")], check=True)
	os.unlink(path)


def shatter(dur=0.6, seed=0):
	"""Fired clay breaking: a dull knock, a burst of cracks, and shards ringing and tinkling as they scatter."""
	n = int(SR * dur)
	t = np.arange(n) / SR
	out = np.sin(2 * np.pi * (230 - 80 * t / dur) * t) * env(n, 0.001, 0.035) * 1.2
	out += band(rng.standard_normal(n), 1500, 9000) * env(n, 0.0005, 0.03) * 1.4
	for _ in range(14):
		start = rng.uniform(0.0, 0.08) + rng.exponential(0.06)
		m = int(SR * 0.05)
		crack = band(rng.standard_normal(m), 2500, 10000) * env(m, 0.0003, rng.uniform(0.004, 0.012))
		out += at(n, start, crack) * rng.uniform(0.3, 0.9)
	for _ in range(10):
		start = rng.uniform(0.03, dur * 0.7)
		m = int(SR * 0.08)
		tt = np.arange(m) / SR
		ping = np.sin(2 * np.pi * rng.uniform(2200, 5200) * tt) * env(m, 0.0005, rng.uniform(0.01, 0.03))
		out += at(n, start, ping) * rng.uniform(0.1, 0.35) * np.exp(-start * 3)
	return out


def roar(dur=1.6):
	"""Greek fire catching: a rising whoosh into a low roar that crackles and dies away."""
	n = int(SR * dur)
	t = np.arange(n) / SR
	noise = rng.standard_normal(n)
	low = band(noise, 60, 700)
	whoosh = np.zeros(n)
	for k in range(0, n, 1024):
		seg = rng.standard_normal(2048)
		c = 300 + 2500 * min(t[k] / 0.18, 1.0) * np.exp(-max(t[k] - 0.18, 0) * 4)
		whoosh = whoosh + at(n, k / SR, band(seg, c * 0.5, c * 1.5) * np.hanning(2048))
	out = low * env(n, 0.06, 0.55) * 1.6 + whoosh * env(n, 0.03, 0.3) * 0.7
	rumble = np.sin(2 * np.pi * 48 * t + 3 * np.sin(2 * np.pi * 3 * t)) * env(n, 0.02, 0.4)
	out += rumble * 0.8
	crackle = np.zeros(n)
	for _ in range(90):
		s = rng.uniform(0.05, dur * 0.95)
		m = int(SR * 0.006)
		crackle += at(n, s, rng.standard_normal(m) * env(m, 0.0001, 0.0015)) * np.exp(-s * 1.2)
	return out + band(crackle, 1500, 9000) * 1.5


def gas(dur=1.8):
	"""A pot of something foul bursting: a wet hiss and slow, glooping bubbles."""
	n = int(SR * dur)
	t = np.arange(n) / SR
	hiss = band(rng.standard_normal(n), 1800, 7000) * env(n, 0.02, 0.5) * 0.7
	out = hiss
	for _ in range(26):
		s = rng.uniform(0.05, dur * 0.85)
		m = int(SR * rng.uniform(0.03, 0.07))
		tt = np.arange(m) / SR
		f0 = rng.uniform(180, 420)
		blip = np.sin(2 * np.pi * (f0 * tt + f0 * 3 * tt * tt / (m / SR))) * np.sin(np.pi * tt / tt[-1]) ** 2
		out += at(n, s, blip) * rng.uniform(0.25, 0.6) * np.exp(-s * 0.6)
	return out


def whirr(dur=1.0):
	"""Weighted cords whirling: air chopped at the spin rate, pitch climbing a little."""
	n = int(SR * dur)
	t = np.arange(n) / SR
	air = band(rng.standard_normal(n), 350, 1800)
	rate = 8 + 3 * t / dur
	chop = np.sin(np.pi * np.cumsum(rate) / SR) ** 4
	hum = np.sin(2 * np.pi * np.cumsum(140 + 40 * t / dur) / SR) * chop * 0.3
	return (air * chop + hum) * np.sin(np.pi * t / t[-1]) ** 0.5


def wrap():
	"""The cords snapping round legs: three quick leather slaps and the weights thudding together."""
	n = int(SR * 0.45)
	out = np.zeros(n)
	for k, s in enumerate((0.0, 0.07, 0.13)):
		m = int(SR * 0.05)
		slap = band(rng.standard_normal(m), 900, 5000) * env(m, 0.0005, 0.012)
		out += at(n, s, slap) * (1 - 0.2 * k)
	m = int(SR * 0.25)
	tt = np.arange(m) / SR
	thud = np.sin(2 * np.pi * 130 * tt) * env(m, 0.001, 0.05)
	knock = np.sin(2 * np.pi * 1350 * tt) * env(m, 0.0005, 0.02)
	return out + at(n, 0.16, thud + knock * 0.4)


def scatter(dur=0.9):
	"""A pouch of caltrops bursting: a soft thump and iron points pinging and skittering about."""
	n = int(SR * dur)
	t = np.arange(n) / SR
	out = np.sin(2 * np.pi * 110 * t) * env(n, 0.002, 0.04) * 0.8
	out += band(rng.standard_normal(n), 400, 2500) * env(n, 0.001, 0.02) * 0.5
	for _ in range(34):
		s = rng.exponential(0.16)
		m = int(SR * 0.09)
		tt = np.arange(m) / SR
		f = rng.uniform(2600, 6800)
		ping = (np.sin(2 * np.pi * f * tt) + 0.5 * np.sin(2 * np.pi * f * 2.76 * tt)) * env(m, 0.0003, rng.uniform(0.008, 0.03))
		out += at(n, s, ping) * rng.uniform(0.15, 0.5)
	return out


def step():
	"""Treading on a caltrop: a sharp prick and a small iron ping."""
	n = int(SR * 0.3)
	t = np.arange(n) / SR
	stab = band(rng.standard_normal(n), 1200, 6000) * env(n, 0.0005, 0.015)
	thud = np.sin(2 * np.pi * 150 * t) * env(n, 0.001, 0.04)
	ping = np.sin(2 * np.pi * 4300 * t) * env(n, 0.0005, 0.05) * 0.4
	return stab + thud + ping


def blunt(i):
	"""A heavy blunt blow: a deep thud through armour and a ringing clang."""
	n = int(SR * 0.7)
	t = np.arange(n) / SR
	f0 = [78, 92, 66][i]
	thud = np.sin(2 * np.pi * (f0 + 60 * np.exp(-t / 0.02)) * t) * env(n, 0.001, 0.11) * 1.4
	body = band(rng.standard_normal(n), 80, 900) * env(n, 0.001, 0.05)
	base = [640, 720, 560][i]
	clang = sum(np.sin(2 * np.pi * base * r * t + rng.uniform(0, 6)) * np.exp(-t / (0.28 / (1 + 0.5 * j))) / (1 + j * 0.7)
		for j, r in enumerate([1.0, 2.31, 3.89, 5.12]))
	click = band(rng.standard_normal(n), 2000, 8000) * env(n, 0.0003, 0.006)
	return thud + body + clang * 0.35 + click * 0.8


def heavy_swing(i):
	"""A big weapon going round: a slow, low whoosh."""
	n = int(SR * 0.5)
	t = np.arange(n) / SR
	out = np.zeros(n)
	centre = 260 + 520 * np.sin(np.pi * t / t[-1]) * (1 + 0.12 * i)
	noise = rng.standard_normal(n + 2048)
	for k in range(0, n, 512):
		seg = noise[k:k + 2048]
		c = centre[min(k, n - 1)]
		out += at(n, k / SR, band(seg, c * 0.6, c * 1.5) * np.hanning(2048))
	return out * np.sin(np.pi * t / t[-1]) ** 2


# ---------------------------------------------------------------------------
# The choir: detuned sawtooth voices singing a chord, shaped frame by frame into vowels by formant peaks.

VOWELS = {
	"a": [(800, 1.0, 90), (1200, 0.55, 110), (2600, 0.25, 160)],
	"e": [(450, 1.0, 80), (1900, 0.5, 120), (2600, 0.3, 160)],
	"u": [(350, 1.0, 70), (750, 0.5, 90), (2400, 0.15, 160)],
	"o": [(500, 1.0, 80), (900, 0.55, 100), (2500, 0.2, 160)],
}


def note(freq):
	return 440.0 * 2 ** ((freq - 69) / 12.0)


def voices(n, chords):
	"""chords: list of (start, end, [midi notes]). Each note sung by three slightly detuned voices with vibrato."""
	t = np.arange(n) / SR
	out = np.zeros(n)
	for start, end, notes in chords:
		mask = np.clip((t - start) / 0.06, 0, 1) * np.clip((end - t) / 0.08, 0, 1)
		for m in notes:
			for d in (-0.08, 0.0, 0.09):
				f = note(m + d) * (1 + 0.006 * np.sin(2 * np.pi * rng.uniform(4.6, 5.8) * t + rng.uniform(0, 6)))
				phase = np.cumsum(f) / SR
				saw = 2 * (phase % 1.0) - 1
				out += saw * mask / (1 + 0.15 * (m - 50) / 12)
	return out


def sing(src, vowelAt, frame=2048, hop=512):
	"""Shapes src into vowels: vowelAt(seconds) gives the vowel blend as [(vowel, weight)]."""
	n = len(src)
	out = np.zeros(n + frame)
	win = np.hanning(frame)
	f = np.fft.rfftfreq(frame, 1 / SR)
	padded = np.concatenate([src, np.zeros(frame)])
	for k in range(0, n, hop):
		seg = padded[k:k + frame] * win
		spec = np.fft.rfft(seg)
		shape = np.zeros_like(f)
		for vowel, weight in vowelAt(k / SR):
			for fc, amp, bw in VOWELS[vowel]:
				shape += weight * amp * np.exp(-0.5 * ((f - fc) / bw) ** 2)
		shape += 0.02
		out[k:k + frame] += np.fft.irfft(spec * shape, frame) * win
	return out[:n]


def hallelujah():
	"""Hal-le-lu-jah: four syllables on I, IV, I, then a long, swelling I. D major."""
	dur = 2.8
	n = int(SR * dur)
	syll = [(0.0, 0.32, "a", [50, 57, 62, 66, 69]), (0.32, 0.62, "e", [55, 59, 62, 67, 71]), (0.62, 0.95, "u", [50, 57, 62, 66, 69]),
		(0.95, 2.75, "a", [45, 52, 61, 64, 69])]
	src = voices(n, [(s, e + 0.04, notes) for s, e, _, notes in syll])

	def vowelAt(sec):
		for s, e, v, _ in syll:
			if s <= sec < e:
				return [(v, 1.0)]
		return [("a", 1.0)]
	choir = sing(src, vowelAt)
	t = np.arange(n) / SR
	# "l" and "j" consonants: brief dips between syllables.
	dips = np.ones(n)
	for s in (0.32, 0.62, 0.95):
		dips *= 1 - 0.7 * np.exp(-0.5 * ((t - s) / 0.018) ** 2)
	swell = 0.75 + 0.25 * np.clip((t - 0.95) / 0.8, 0, 1)
	breath = band(rng.standard_normal(n), 3000, 9000) * 0.02
	return choir * dips * swell * np.clip((dur - t) / 0.35, 0, 1) + breath


def holy_blast():
	"""The blast: a huge boom with a bright crack, and over it the choir's held "Aaah" and a shimmer of bells."""
	dur = 3.8
	n = int(SR * dur)
	t = np.arange(n) / SR
	boom = np.sin(2 * np.pi * (34 + 70 * np.exp(-t / 0.05)) * t) * env(n, 0.002, 0.5) * 2.2
	crack = band(rng.standard_normal(n), 600, 9000) * env(n, 0.0005, 0.06) * 1.6
	rumble = band(rng.standard_normal(n), 30, 250) * env(n, 0.01, 0.9) * 1.6
	src = voices(n, [(0.05, dur, [50, 57, 62, 66, 69, 74])])
	choir = sing(src, lambda sec: [("a", 1.0 - min(sec / 3.0, 0.6)), ("o", min(sec / 3.0, 0.6))])
	choir *= np.clip((t - 0.05) / 0.25, 0, 1) * np.clip((dur - t) / 1.4, 0, 1)
	bells = np.zeros(n)
	for k, m in enumerate([74, 78, 81, 86, 90]):
		s = 0.1 + 0.12 * k
		bells += at(n, s, np.sin(2 * np.pi * note(m) * t) * env(n, 0.001, 0.7) + 0.4 * np.sin(2 * np.pi * note(m) * 2.76 * t) * env(n, 0.001, 0.25))
	return boom + crack + rumble + choir * 0.9 + bells * 0.12


# ---------------------------------------------------------------------------
# The second armoury (Arsenal2.ini).


def pluck(freq, dur, damp=0.996):
	n = int(SR * dur)
	period = int(SR / freq)
	buf = rng.uniform(-1, 1, period)
	out = np.zeros(n)
	for k in range(n):
		out[k] = buf[k % period]
		buf[k % period] = damp * 0.5 * (buf[k % period] + buf[(k + 1) % period])
	return out


def warbow(i):
	"""A heavy bow loosed: a deep, long string note, a thump of the stave, and the arrow's hiss away."""
	n = int(SR * 0.6)
	t = np.arange(n) / SR
	string = pluck([88, 98][i], 0.6, 0.997) * env(n, 0.001, 0.25)
	thump = np.sin(2 * np.pi * 55 * t) * env(n, 0.001, 0.06) * 1.2
	hiss = band(rng.standard_normal(n), 2500, 9000) * env(n, 0.005, 0.12) * 0.5
	return string + thump + hiss


def arbalest(n_dur=0.5):
	"""An arbalest's steel bow snapping straight: a hard clack, a short deep twang and a whip of air."""
	n = int(SR * n_dur)
	t = np.arange(n) / SR
	clack = band(rng.standard_normal(n), 1200, 6000) * env(n, 0.0005, 0.015) * 3
	twang = pluck(140, n_dur, 0.99) * env(n, 0.001, 0.07) * 1.5
	thump = np.sin(2 * np.pi * 70 * t) * env(n, 0.001, 0.04) * 1.5
	whip = band(rng.standard_normal(n), 3000, 10000) * env(n, 0.002, 0.08) * 0.6
	return clack + twang + thump + whip


def sling(i):
	"""A sling whirled and let go: a rising whirr, then a snap of the cords."""
	n = int(SR * 0.7)
	t = np.arange(n) / SR
	rate = 6 + 10 * t / t[-1]
	whirr = band(rng.standard_normal(n), 300, 1800) * (0.5 + 0.5 * np.sin(2 * np.pi * np.cumsum(rate) / SR)) * np.clip(t / 0.1, 0, 1) * np.clip((0.55 - t) / 0.1, 0, 1)
	snap = at(n, 0.55, band(rng.standard_normal(int(SR * 0.08)), 2000, 8000) * env(int(SR * 0.08), 0.0005, 0.01) * 3)
	return whirr * (0.6 + 0.1 * i) + snap


def stone_hit(i):
	"""A sling stone striking: a sharp knock."""
	n = int(SR * 0.15)
	t = np.arange(n) / SR
	knock = np.sin(2 * np.pi * [420, 360][i] * t) * env(n, 0.0005, 0.02)
	crack = band(rng.standard_normal(n), 1500, 7000) * env(n, 0.0002, 0.008) * 2
	return knock + crack


def net_wrap():
	"""A net dropping over someone: a rustle of cord and the thuds of its weights."""
	n = int(SR * 0.6)
	rustle = band(rng.standard_normal(n), 800, 4000) * env(n, 0.01, 0.2)
	thuds = np.zeros(n)
	for k in range(4):
		seg = int(SR * 0.08)
		thuds += at(n, 0.05 + 0.09 * k + rng.uniform(0, 0.03), np.sin(2 * np.pi * rng.uniform(150, 260) * np.arange(seg) / SR) * env(seg, 0.001, 0.02))
	return rustle * 0.6 + thuds


def trap_snap():
	"""A snare trap closing: a spring's twang and the clang of iron jaws."""
	n = int(SR * 0.4)
	t = np.arange(n) / SR
	spring = pluck(320, 0.4, 0.99) * env(n, 0.001, 0.04)
	clang = sum(np.sin(2 * np.pi * f * t) * env(n, 0.001, 0.12 / (k + 1)) for k, f in enumerate((1900, 2700, 3900)))
	knock = np.sin(2 * np.pi * 110 * t) * env(n, 0.001, 0.03) * 1.5
	return spring + clang * 0.5 + knock


def flail_whirl():
	"""A flail's ball whirling through a swing: chain rattle and a rising whoosh."""
	n = int(SR * 0.45)
	t = np.arange(n) / SR
	noise = rng.standard_normal(n)
	rise = np.sin(np.pi * t / t[-1]) ** 2
	whoosh = (band(noise, 200, 1200) * (1 - t / t[-1]) + band(noise, 900, 2500) * (t / t[-1])) * rise
	rattle = band((rng.random(n) > 0.992) * rng.standard_normal(n) * 3, 2000, 7000) * env(n, 0.01, 0.3)
	return whoosh + rattle * 0.5


def frost_burst():
	"""A frost flask breaking: glass, then a rushing crackle of ice forming and a cold ring."""
	n = int(SR * 1.2)
	t = np.arange(n) / SR
	glass = band(rng.standard_normal(n), 3000, 10000) * env(n, 0.0005, 0.03) * 2
	crackle = band((rng.random(n) > 0.994) * rng.standard_normal(n) * 4, 1500, 9000) * env(n, 0.02, 0.5)
	ring = sum(np.sin(2 * np.pi * f * t) for f in (1760, 2217, 2637)) * env(n, 0.01, 0.6) * 0.25
	rush = band(rng.standard_normal(n), 400, 2000) * env(n, 0.005, 0.25)
	return glass + crackle + ring + rush * 0.6


def draught_burst():
	"""A healing draught breaking: glass, a soft bubbling fizz and a warm chime."""
	n = int(SR * 1.0)
	t = np.arange(n) / SR
	glass = band(rng.standard_normal(n), 3000, 10000) * env(n, 0.0005, 0.03) * 1.5
	fizz = band((rng.random(n) > 0.99) * rng.standard_normal(n) * 2, 2000, 8000) * env(n, 0.05, 0.4)
	chime = sum(np.sin(2 * np.pi * f * t) * env(n, 0.02, 0.5) for f in (1046.5, 1318.5, 1568.0)) * 0.3
	return glass + fizz + chime


def main():
	for i in range(2):
		write(f"PotShatter{i + 1}", shatter(0.6), 0.75)
		write(f"FirepotBurst{i + 1}", shatter(1.6) + roar(1.6) * 1.1, 0.85)
	write("StinkBurst1", shatter(1.8) * 0.7 + gas(1.8), 0.75)
	write("BolasWhirr1", whirr(1.0), 0.5)
	write("BolasWrap1", wrap(), 0.75)
	write("CaltropScatter1", scatter(), 0.6)
	write("CaltropStep1", step(), 0.7)
	for i in range(3):
		write(f"BluntHit{i + 1}", blunt(i), 0.85)
	for i in range(2):
		write(f"HeavySwing{i + 1}", heavy_swing(i), 0.6)
	# The second armoury's.
	for i in range(2):
		write(f"Warbow{i + 1}", warbow(i), 0.75)
		write(f"Sling{i + 1}", sling(i), 0.6)
		write(f"StoneHit{i + 1}", stone_hit(i), 0.6)
	write("Arbalest1", arbalest(), 0.85)
	write("NetWrap1", net_wrap(), 0.6)
	write("TrapSnap1", trap_snap(), 0.8)
	write("FlailWhirl1", flail_whirl(), 0.5)
	write("FrostBurst1", frost_burst(), 0.7)
	write("DraughtBurst1", draught_burst(), 0.6)
	write("Hallelujah1", hallelujah(), 0.7)
	write("HolyBlast1", holy_blast(), 0.95)
	print("arsenal sounds done")


main()
