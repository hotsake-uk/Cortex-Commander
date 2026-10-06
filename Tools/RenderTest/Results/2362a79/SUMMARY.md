# Test results: 2362a79e (standing room, one definition)

Debug build of 2362a79e. Every suite run 3 times, plus traced reruns of sky bunker course 1 and Bywater course 1. Then a second
set of 3 runs of both bunkers with two test-side course fixes (`courses_*.log`, `results_courses.txt`). Your later pushes
(158c93ba to c553485a) landed while this ran, so they are tested in `../af7836b/`.

## Totals

| Suite | 7f09496 (4 runs) | 2362a79 (3 runs) | 2362a79, corrected courses (3 runs) |
|---|---|---|---|
| Outdoor | 15/16 each | 15/16 each (crab fails 3/3) | - |
| Combat | all resolve | all resolve | - |
| Old sky bunker | 3/20 | 6/15 | **11/15** (with a floor under it) |
| Bywater | 5/20 | 7/15 | 5/15 |

## What the standing-room change did

- Prone crawling in corridors is mostly gone. `crawl: going prone` in the traced runs went from 21 to 2 (Bywater) and 6 to 3 (sky
  bunker). Stall lines with `prone 2` went from 42 to 29.
- Bywater "mid left room to the right column" went from 0/4 to 3/3 (20-30 s). It now gets up the 45 px hatch into the 48 px corridor.
- The Bywater course 1 unit gets past the prone loop but still loses ~15 s at two places (`AIBywater_trace1.log`):
  1. At the top of the first hatch it is popped at 1692,1105 for 1716,1085, then drifts *left* to 1676 (its next point is 1740, to
     the right), burns the tank to 44 and drops back down the hatch, twice (2-8 s). Your ce08008f ("the step off a climb lands")
     looks aimed at this.
  2. It walks past the foot of the second climb (waypoint 1788,1073, unit at 1838), then `climb: under a ceiling at 1043, keeping out from
     under it` until it walks back (9-14 s).

## Test-side course fixes (my changes, in AIBunker.lua)

- **Bywater "bottom right to the middle" started inside a pillar.** (2070,1040) settles into a solid column about 24 px wide (nodes
  2076,1020..1092 all solid). Before your change the unit was pushed out sideways. With it, it lay prone inside the pillar, built
  speed to 31 without moving, and died, in 3/3 runs. The start is now (2130,1070), on the 1092 floor beside it. This course's
  numbers start again here.
- **The old sky bunker got a floor** (concrete beams under the bottom storey) as a check: 6/15 to 11/15, which confirms its open
  hub floors were most of its failures. Your rebuild (8caa4f7d) replaces it, so I dropped the floor change.

## New problem: Bywater "middle to the mid left room" dies, 3/3 in the corrected-course runs

It dies about 3-5 s after the start (1823,531) while jetting up its first step (1812,432), every run. In the first set it died once,
arrived once and gave up once. The gym logged nothing about why. The gym now writes `AIBUNKER hurt ...` on every health loss (with the speed
before and after) and the death line gives the last seen position, speed and health. The af7836b runs carry those lines.
