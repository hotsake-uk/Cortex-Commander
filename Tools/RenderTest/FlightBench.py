"""Summarise flight gym runs (FLIGHT lines in Results/<label>/<build>/Flight_<n>.log) for one or more builds.

    python Tools/RenderTest/FlightBench.py Tools/RenderTest/Results/flight1/base Tools/RenderTest/Results/flight1/ours

Per course and in total: landings, mean time of the landings, and means over every flight of the fuel burned, the overshoot past
the pad sideways, the height above the pad, the sideways reversals in the air, and how many fell.
"""
import glob
import os
import re
import sys
from collections import defaultdict

LINE = re.compile(r"FLIGHT (.+?): (landed in ([\d.]+) s|GAVE UP after 30 s.*?|died)(?:, fuel (\d+) ms, over (\d+) px, above (\d+) px, reversals (\d+)(, fell)?)?$")


def load(folder):
    runs = defaultdict(list)
    for path in sorted(glob.glob(os.path.join(folder, "Flight_*.log"))):
        for raw in open(path, encoding="utf-8", errors="replace"):
            m = LINE.search(raw.strip())
            if not m:
                continue
            name = m.group(1)
            runs[name].append({
                "landed": m.group(2).startswith("landed"),
                "time": float(m.group(3)) if m.group(3) else None,
                "fuel": int(m.group(4)) if m.group(4) else 0,
                "over": int(m.group(5)) if m.group(5) else 0,
                "above": int(m.group(6)) if m.group(6) else 0,
                "rev": int(m.group(7)) if m.group(7) else 0,
                "fell": bool(m.group(8)),
            })
    return runs


def summary(flights):
    n = len(flights)
    if n == 0:
        return None
    landed = [f for f in flights if f["landed"]]
    mean = lambda key: sum(f[key] for f in flights) / n
    return {
        "landed": f"{len(landed)}/{n}",
        "time": f"{sum(f['time'] for f in landed) / len(landed):.1f}s" if landed else "-",
        "fuel": f"{mean('fuel') / 1000:.1f}s",
        "over": f"{mean('over'):.0f}",
        "above": f"{mean('above'):.0f}",
        "rev": f"{mean('rev'):.1f}",
        "fell": f"{sum(f['fell'] for f in flights)}",
    }


def main(folders):
    builds = [(os.path.basename(os.path.normpath(f)), load(f)) for f in folders]
    names = sorted({name for _, runs in builds for name in runs})
    cols = ["landed", "time", "fuel", "over", "above", "rev", "fell"]
    print("| Course | Build | Landed | Time | Fuel | Over px | Above px | Reversals | Fell |")
    print("|---|---|---|---|---|---|---|---|---|")
    for name in names:
        for build, runs in builds:
            s = summary(runs.get(name, []))
            if s:
                print(f"| {name} | {build} | " + " | ".join(s[c] for c in cols) + " |")
    print()
    print("| Build | Landed | Time | Fuel | Over px | Above px | Reversals | Fell |")
    print("|---|---|---|---|---|---|---|---|")
    for build, runs in builds:
        s = summary([f for flights in runs.values() for f in flights])
        if s:
            print(f"| **{build}** | " + " | ".join(s[c] for c in cols) + " |")


if __name__ == "__main__":
    main(sys.argv[1:])
