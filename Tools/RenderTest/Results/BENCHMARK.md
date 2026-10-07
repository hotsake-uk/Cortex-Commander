# AI benchmark: our AI against the original

Each round runs every course three times with `Tools/RenderTest/Bench.ps1`, and `Bench.py` makes the tables.
The original AI is the community build at bc93d5a5e (worktree `../cccp-ai-baseline`), with gyms patched for its older pathfinder.
A course counts when the unit arrives within 60 seconds. There are 126 runs per round.

## Every round

| Build | Gym | Sky | Tower | Bywater | Hemslock | All |
|---|---|---|---|---|---|---|
| Original AI | 40/48 | 21/21 | 23/24 | 5/15 | 14/18 | **103/126** |
| Ours, round 1 | 46/48 | 19/21 | 21/24 | 6/15 | 8/18 | **100/126** |
| Ours, round 3 | 47/48 | 21/21 | 23/24 | 6/15 | 17/18 | **114/126** |
| Ours, round 4 | 48/48 | 21/21 | 24/24 | 10/15 | 14/18 | **117/126** |
| Ours, round 5 | 43/48 | 21/21 | 24/24 | 9/15 | 15/18 | **112/126** |
| Ours, round 6 | 44/48 | 20/21 | 24/24 | 5/15 | 14/18 | **107/126** |
| Ours, round 7 | 46/48 | 21/21 | 24/24 | 7/15 | 13/18 | **111/126** |

Rounds swing by about five runs with no code change, mostly on the two real maps. Round 6 included two changes that were then
reverted or narrowed, because they cost the gym's gap course and Bywater. Round 7 is the current code (778cda1b9).

## What the original never does and ours does

- **Crab over the hill:** original 0/3, ours 3/3 in every round since round 1.
- **Hemslock, east hall to the far west upper floor:** original 0/3, ours 2/3 to 3/3.
- **Bywater, middle to the mid left room:** original 0/3, ours 1/3 to 3/3.
- **Dig down:** original 1/3, ours 3/3.

## Faults found and fixed from traces in this work

- A crawl step laid the unit down from 100 px away; tower units never reached the hatch.
- Flight control jetted for sideways speed at a shaft's foot and pinned units under the corridor's lip.
- Under a ceiling the climber walked to the nearest opening, away from the ladder the route used.
- A drop whose node sat on a slab's edge left the unit standing on the lip (Hemslock roof, Bywater west hatch).
- A shaft climb planned mid-hop flew to its column at 11 m/s and overshot.
- Mantling pulled units back up out of the hatch they were dropping through.
- At a tunnel's mouth units stood up into the slab over it, and counted the climb's points as reached from inside.
- In flight the unit steers for the furthest route point in clear view, and take-off direction comes from flight control.

## Still behind or unsteady

- Hemslock "east wing up a storey": the original passes 3/3, ours 0/3 to 3/3. The route crawls a 45 px tunnel and climbs
  the outside wall from its mouth, then makes a long diagonal jump onto the roof that sometimes overshoots.
- Bywater "bottom corridor to the top room" and "bottom right to the middle" stay at 0 to 2 of 3 for both builds.

## The 8.1 line: movement in the engine (2026-10-07 to 08)

From 8.1 the walking, climbing and flying are the engine's (the route-follower in `AHumanMovement.cpp`, the pilot in
`AHuman::PilotFlight`); the scripts say where to go. Successes per suite, from `benchmark_summary.csv`. Three repeats make a
fair comparison; the later rows are single quick checks (one repeat), so a course either way on Bywater is noise.

| Run | Version | Follower | Repeats | AIGym | Sky | Tower | AIBywater | AIHemslock | Flight |
|---|---|---|---|---|---|---|---|---|---|
| bench1 (original) | 7.0.0 | original AI | 3 | 40/48 | 21/21 | 23/24 | 5/15 | 14/18 | - |
| bench7 | 7.0.0 | Lua follower | 3 | 46/48 | 21/21 | 24/24 | 7/15 | 13/18 | - |
| flight13 (original) | 7.0.0 | original AI | 3 | - | - | - | - | - | 22/57 |
| flight13 | 7.0.x | Lua follower | 3 | - | - | - | - | - | 36/57 |
| mover2lua | 8.1.5 | Lua follower + engine pilot | 3 | 48/48 | 21/21 | 21/24 | 7/15 | 12/18 | 39/57 |
| mover2 | 8.1.2 | engine follower | 3 | 48/48 | 21/21 | 22/24 | 9/15 | 13/18 | 39/57 |
| mover4 | 8.1.13 | engine follower | 3 | 48/48 | 19/21 | 17/24 | 2/15 | 14/18 | 54/57 |
| foot1 | 8.1.19 | engine follower | 1 | - | 7/7 | 8/8 | 3/5 | - | 16/19 |
| anchor1 | 8.1.21 | engine follower | 1 | - | 7/7 | 8/8 | 3/5 | - | 17/19 |
| risk1 | 8.1.26 | engine follower | 1 | - | 7/7 | 8/8 | 4/5 | - | 17/19 |
| sense1 | 8.1.27 | engine follower | 1 | - | 7/7 | 8/8 | 3/5 | - | 17/19 |
| legs1 | 8.1.29 | engine follower | 1 | - | 7/7 | 8/8 | 2/5 | - | - |

- **Flight:** the original AI lands 22 of 57 gym flights, the Lua follower 36, the engine follower 54 (8.1.13): half the
  fuel and half the jet pulses of the Lua follower, and a third of its falls.
- **Indoors:** the engine follower started behind (Tower 17/24, Bywater 2/15 in 8.1.13) and is now clean on Tower and Sky,
  2 to 4 of 5 on Bywater, from walking down rather than flying it, take-offs only where the route leaves the floor, the walk's
  own sense of what is ahead, node anchors, and risk on jumps.
- **No jetpacks:** with ladders climbed hand over hand (8.1.25), jetless soldiers make 5 to 7 of 8 Tower courses (0 before).
- **Speed:** where both arrive, the engine follower is about a third faster (mean 10.8 to 12.6 s against 15 to 21 s).

### The data, for graphs

`python Tools/RenderTest/BenchHistory.py` rebuilds two files here from every run's `results.csv` (and git, for versions):

- `benchmark_history.csv`: one row per course attempt: date, run_label, build (`ours` or `base` for the original AI),
  version, commit, tested_before_commit, follower, condition (`standard` or `no jetpacks`), suite, course, repeat, arrived
  (1 or 0), result, seconds.
- `benchmark_summary.csv`: one row per run and suite (and `All`): version_order (the runs in time order, for an x axis),
  courses_in_suite, repeats, attempts, arrived, success_rate, mean_seconds (of the arrivals).

Graph success_rate (or mean_seconds) against date or version_order, one series per follower, faceted by suite. Compare like
with like: courses_in_suite changes when a suite's courses did, and the `fix*`, `ab*` and `flight1`-`17` runs are partial
runs from tuning. A run tested before its commit carries that commit's version (tested_before_commit = yes); where two
commits came minutes apart the version can be one out.
