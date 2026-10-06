# Test results: 7f0949674 (gym stall reports; no AI change)

Tested on the Windows machine, debug build of 5355fe156 (no C++ change since). One full pass, three traced reruns, then two more
runs of each indoor suite. Raw logs are in this folder; `results.txt` / `results_repeat.txt` have the result lines.
The baseline before your commit is in `../baseline-5355fe1/`.

## Read this first: the indoor results are noisy

Your commit only adds logging, yet the sky bunker went from 2/5 (baseline) to 0/5, and the combat "move" fight from arrived to
669 px short. Over four runs per suite:

| Suite | Passes per run | Notes |
|---|---|---|
| Outdoor (16 courses) | 15, 15, (16 in the crab trace run) | crab over the hill passed in the traced run (39 s) |
| Combat (5 fights) | all resolve | winners vary run to run |
| Sky bunker (5) | 2, 0, 1, 0 | "across the gap" fails every time at the same spot |
| Bywater (5) | 2, 1, 1, 1 | "mid left room to the right column" fails every time, ~200 px short |

From now on I run each indoor suite at least 3 times per push (`TestRun.ps1 -Repeat 3`) and report pass counts out of 15.
Judge a change on those totals, not one run.

## Root causes, most important first

### 1. The grid and the movement script disagree about standing room (affects both bunkers)

- The grid treats `FreeHeight >= s_StandHeight` as standing room, with StandHeight = 0.42 x Height = **42 px** for the Soldier Light
  (height 100, radius 28). Corridors in both bunkers measure **48 px** (`free 48`), so the grid routes walking through them.
- `GoToWpt`'s crawl check (SharedBehaviors.lua around line 843) casts from `Owner.Pos` to `topHeadPos = Pos - (0, 0.3H + 5)` = 35 px
  above the centre, then forward H/2. Standing on the sky bunker's top corridor floor (surface 264) the head point is about y 209; the
  ceiling is about y 215. So it goes prone in a corridor the grid calls walkable.
- Evidence: sky bunker stalls `top left to low right` / `low right to mid left` are all `prone 2`, creeping ~5 px/s along a flat
  corridor (`AIBunker.log`, stall blocks at 1548,258 / 1586,259 / 1603,258 = 55 px in 12 s). The traced unit toggles
  `crawl: going prone` / `crawl: standing up` every second along a flat corridor (`AIBunker_trace1.log`, 2s-6s).
- Worse at hatches: in Bywater the unit jets up the first hatch (1716,1188 -> 1716,1083), lands, immediately goes prone
  (`crawl: going prone, wpt dx 44 dy -24`), a prone unit may not jet (line ~1743), and it slides back down the hatch. It loops like
  this for the whole minute (`AIBywater_trace1.log`, 2s-30s).
- The always-failing Bywater course is the same thing: it has to jet up a hatch about 45 px wide (node 1956,852 `clear 24/21`), then go
  right into a 48 px corridor (1980..2028,852 `free 48`). It never gets in (stall at 1977,897, route left 1956,843 1980,833 ...).

Suggested fix: make one definition of standing room and use it on both sides. Measure the real standing body of a Soldier Light
(feet to top of head, relative to Pos) and either (a) raise s_StandHeight to match, which makes 48 px corridors Crawl steps the
script knows about and costs them properly, or (b) if a soldier can crouch-walk through 48 px, make the script crouch
instead of going prone when the grid says Walk with FreeHeight under the full standing height. Also, never go prone within a body
width of the top of a Jump step, so a unit that has just come up a hatch steps clear of it before lying down.
If you want, I'll measure the body extents. Add the request and say which unit.

### 2. Jet columns start at the edge of an opening, and the climb then refuses (sky bunker)

- Course "low left to top right": the route goes through the open floor of the bottom-storey hub at 1696, falls to the hill top
  under the bunker (~y 564), then asks for a 312 px climb `1908,564 -> 1932,252` through the stacked hubs at x 1888 (opening about
  1840-1936).
- 1908/1932 are at the right edge of that opening. The unit drifts to 1943, and `climb: under a ceiling at 476, keeping out from under it`
  fires. It never gets back under the opening and loops below the bunker for the rest of the minute (`AIBunker_trace1.log`, 13s on).
- Suggested fix: place the vertical run of a jump chain on the middle of the opening (the grid knows ClearLeft/ClearRight per node),
  and/or have the climb controller centre on `ShaftMiddle` of the opening above, not just "keep out from under" the ceiling.

### 3. No way back in from under the sky bunker ("across the gap", 4/4 failures)

- The unit falls through the same open hub floor into a pocket on the hill at 1903,575. From there the pather finds no path
  (`path 0` for 30 s).
- The grid at the pocket: nodes 1884,588 / 1908,588 / 1932,588 are ground with `free 96` but have **no up edges**. Clearance is
  `18/48`, `42/48`, `12/48`, so the three-node-wide jet column rule (RoomToPass 3) rejects every column, although the hub opening above is 96 px wide
  and the unit is 28 px wide (HalfWidth 14).
- Suggested fix: base the jet column width test on the unit's HalfWidth plus margin, measured on the column's own nodes, not on three
  full nodes. Also reconsider routing *down* out of a bunker through the floor of a bottom-storey hub when the goal is inside. Falling out is
  only acceptable if climbing back is reliable.
- Caveat: the sky bunker's open-bottom hubs are a quirk of how it is built. A real bunker's bottom storey sits on terrain. Fix 1 and 2 first. This one matters most for
  the sky bunker's score.

### 4. Odd step kinds (minor, may be harmless)

- Sky bunker routes label short corridor steps `Fall` (kind 3) as the first step while standing on flat floor (stall lines
  `first step kind 3`). Bywater has `1716,1083 -> 1740,1092(2)`, a Jump labelled step that goes 9 px *down*.

## Your numbered requests

1. Sky bunker is in every run (above).
2. Stall blocks: in `AIBunker.log`, `AIBywater.log` and the repeat logs. Traced: sky bunker course 1 (`AIBunker_trace1.log`) and
   Bywater course 1 (`AIBywater_trace1.log`), the worst in both.
3. Crab: traced with `CCCP_GYM_TRACE=15` (`AIGym_trace15.log`). In that run it **arrived in 39.2 s**. It made slow progress up the
   first slope (1536 -> 1594 took 12 s), facing flipped left four times while heading right, and aim sat at the 0.5 limit much of
   the time. No throw-back this run. It failed in the other two plain runs (stall blocks in `AIGym.log`).
4. Unit lines: `Soldier Light height 100 radius 28 jump height 440 px aim range 1.57`, `Dreadnought height 110 radius 42 jump
   height 169 px aim range 0.5`.
5. Hemslock Hold has no saved gym courses (only Bywater has a file, and it is switched off), so there are no Hemslock results.

## Tooling added on the test side

- `TestRun.ps1 [-Repeat n] [-Suites ...]` runs the whole pass into `Results/<label>/`.
- `TraceRuns.ps1 -Label x -Runs "AIGym:15,AIBywater:1"` runs traced reruns.
- `CCCP_GYM_TRACE=n` picks the outdoor gym's traced course (AIGym.lua).
