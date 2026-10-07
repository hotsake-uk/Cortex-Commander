# AI benchmark: our AI against the original

Each course was run three times on each build with `Tools/RenderTest/Bench.ps1`, and the tables were made by `Bench.py`.
The original AI is the community build at bc93d5a5e (worktree `../cccp-ai-baseline`), with gyms patched for its older pathfinder.
Our AI is `ai-overhaul` at 30e038a1a. A course counts when the unit arrives within 60 seconds.

## Totals

| Suite | Original | Ours |
|---|---|---|
| Gym courses | 40/48 | 47/48 |
| Sky bunker | 21/21 | 21/21 |
| Ladder tower | 23/24 | 23/24 |
| Bywater Barracks | 5/15 | 6/15 |
| Hemslock Hold | 14/18 | 17/18 |
| **All** | **103/126** | **114/126** |

## How we got here in this round

The first benchmark of this round (bench1) had ours at 100/126, behind the original. Traces of the failures found these faults, each fixed:

- A crawl step laid the unit down from 100 px away, so units at the tower top lay at their start and never reached the hatch.
- Flight control jetted for sideways speed at the foot of a shaft and held units against the corridor's lip until the tank was empty.
- Under a ceiling, the climber walked to the nearest opening, which could lead away from the ladder the route went up.
- A drop whose node sat on the edge of a slab left the unit standing on the lip all minute (Hemslock roof, Bywater west hatch).
- A shaft climb planned mid-hop, far from its column, flew there at 11 m/s, overshot and ran dry.
- In flight the unit now steers for the furthest route point in clear view, and take-off direction comes from the flight controller.

## Where ours is still behind

- Bywater "top room to the bottom corridor": original 2/3, ours 0/3.
- Some times are slower: tower descents and the Hemslock east wing climb.

Full tables: `bench1/` (first run, both builds), `bench2/` and `bench3/` (ours after fixes). Compare with:

    python Tools/RenderTest/Bench.py Tools/RenderTest/Results/bench1/base/results.csv Tools/RenderTest/Results/bench3/ours/results.csv
