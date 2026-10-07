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
