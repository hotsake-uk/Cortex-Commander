"""Makes the vehicles' engine sounds (VH-2 on): short loops built from scratch, so there is nothing to license. The game plays each looped
and raises its pitch with the engine's load.

The outboard motor: a small two-stroke, a buzzy bark at each firing over a rattle and a little hiss, 50 firings a second at pitch 1.

Run from the repository's root: python Tools/MakeVehicleSounds.py
"""

import math
import random
import struct
import wave
from pathlib import Path

RATE = 22050


def write_wav(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    peak = max(abs(s) for s in samples) or 1.0
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", int(s / peak * 0.8 * 32767)) for s in samples))


def two_stroke(firings_per_second, seconds, bark, seed):
    """An engine loop: each firing a sharp pulse that rings at the exhaust's note (bark, Hz) and dies away, with a rattle that differs a
    little from firing to firing, and hiss under it all. A whole number of firings fits the loop, so it joins up without a click."""
    rng = random.Random(seed)
    count = round(firings_per_second * seconds)
    period = int(RATE / firings_per_second)
    samples = [0.0] * (period * count)
    for k in range(count):
        strength = 0.85 + rng.uniform(0.0, 0.3)
        rattle = [rng.uniform(-1.0, 1.0) for _ in range(period)]
        for i in range(period):
            t = i / RATE
            ring = math.sin(2 * math.pi * bark * t) * math.exp(-t * 90)
            thump = math.exp(-t * 300) * (1.0 if i < 8 else 0.4)
            samples[k * period + i] += strength * (ring * 0.8 + thump * 0.6 + rattle[i] * math.exp(-t * 160) * 0.35)
    # Hiss, and a low hum of the crank under it, both looping cleanly.
    length = len(samples)
    for i in range(length):
        samples[i] += rng.uniform(-1.0, 1.0) * 0.06 + math.sin(2 * math.pi * firings_per_second * 0.5 * i / RATE) * 0.12
    # A gentle low-pass, run round the loop so its start and end match.
    smoothed = samples[:]
    for _ in range(2):
        previous = smoothed[-1]
        for i in range(length):
            previous = previous + (smoothed[i] - previous) * 0.45
            smoothed[i] = previous
    return smoothed


def main():
    write_wav(Path("Data/Base.rte/Actors/Vehicles/MotorBoat/OutboardLoop.wav"), two_stroke(50, 1.0, 190, 7))


if __name__ == "__main__":
    main()
