# Test results: d3c6d8dd (stand up in the air; the hatch's false lip; the brake's lean; the crab's lean ungated)

Debug build of 3e9b13f (no C++ change since). Every suite run 3 times (`*_1..3.log`, `results.txt`), a crab trace
(`AIGym_trace15.log`), and the stairs experiment (`stairs_*.log`). Bywater uses the new "middle" from here on (baseline 5/15, in
`../3e9b13f/`).

## Totals

| Suite | 3e9b13f | d3c6d8d | Notes |
|---|---|---|---|
| Outdoor | 15/16 x3 | **16, 15, 16** | crab over the hill **2/3** (28.4 s, 23 s; once 471 px short at 1593,651) |
| Combat | all resolve | all resolve | |
| Sky bunker | 17/21 | **21/21** | every course every run. Slowest: gallery 11.5-27.5 s, down the stairs 15.5 s |
| Bywater | 5/15 (new middle) | **6/15** | courses 2, 3 and 4 pass 2/3 each; courses 1 and 5 0/3 |

Sky bunker times (3 runs): down the hatch 6.5-7.5, up the hatch 7.5-16.5, across the hub 6.5-7.5, up the stairs 6.5, down the stairs
15.5, bottom corridor to the top room 8.5-12.5, bottom to the top gallery 11.5-27.5.

## Checklist

- **Stand up in the air** (Bywater course 3 outside drop): course 3 is 2/3 again (42.5, 43.5 s; once 61 px short at 1701,1106).
- **The hatch's false lip** (sky "up the hatch"): met, 3/3, 7.5-16.5 s.
- **The crab's lean ungated**: met in part, over the hill 2/3 (was 0/3). Trace in `AIGym_trace15.log`.

## The stairs experiment (your request 2)

- **AI, jetpack emptied** (`CCCP_BUNKER_NOJET=1`, "up the stairs" only): neither the soldier nor the Dreadnought moves at all. The route is
  one Jump step (`2060,341 -> 2160,244(2)`), and with no fuel the AI stands at the foot for the whole minute. That is a result in
  itself: a unit whose jetpack is empty or missing won't try a Jump step, even one it could walk.
- **Legs only** (`CCCP_BUNKER_WALK=1`: no AI, the walk key held towards the goal every frame, jetpack empty): **the Soldier Light
  walks up Steep Stairs D unaided and arrives in 5.5 s** (4 s after the key is first held; at 3 s it is at 2096,285, halfway up).
  `stairs_walk_soldier.log`.
- The Dreadnought didn't move at all with the key held (`stairs_walk_crab.log`, vel 0). I think the forced input doesn't reach a crab's
  controller this way, so that result is inconclusive, not a "can't". If you know how a crab's legs are driven from Lua, say so and
  I'll rerun it.
- So for humans, a walking edge up these stairs looks right.

## Faults

### 1. Bywater "bottom corridor to the top room" (0/3)

Run 3 stood still 49 s at 1819,1051, `fuel 174`, `first step kind 2`, next point 1812,908 (a 143 px climb). The grid: the hatch above
is the column at 1788/1812 (`free 96`, clear `10/35` and `34/11` at y 1020, so about 45 px wide between x ~1778 and ~1823). The unit
stands at its right edge. Two possibilities: it waits at the foot for a tank it never refills (fuel 174 four seconds into the
stall), or it tries and fails without moving. No AITRACE for this course this round. I'll trace it next round (course 1 is the
default trace course).
The other runs gave up at 1758,905 and 1225,1068: the second is outside the bunker at the bottom left.

### 2. Bywater "bottom right to the middle" (0/3)

It gives up in three different places (1560,544 / 1431,705 / 1685,1163). The route is 53 nodes and goes far right and back first.
No trace yet. I'll trace course 5 next round as well.

## Your other requests

- **Doors B in Bywater** (request 3): the scene places `PlaceSceneObject = TerrainObject` / `CopyOf = Base.rte/Doors B` at
  (1800,540) and (1800,780), and `Base.rte/Doors D` at (1800,324), all `Team = 1` (Mods/BB+.rte/Scenes/Bywater Barracks.ini, from
  line 2939). `Doors B` (Base.rte/Scenes/Objects/Bunkers/BunkerSystems/Doors/Doors.ini, line 1372) is a TerrainObject
  (DoorsBFG/Mat/BG.png) with two child ADoors, `CopyOf = Door Rotate Short Horiz`, at offsets (36,33) rotated -90 and (-36,33)
  HFlipped rotated 90. In the sky bunker, `SandboxDo("Structure", pos, 0, 0, 0, "Doors B")` should place it like the other modules.
- **Unit lines**: still only the Soldier Light and the Dreadnought.

## Test-side switches added (AIBunker.lua)

`CCCP_BUNKER_UNIT=crab` sends the Dreadnought instead of the soldier. `CCCP_BUNKER_NOJET=1` empties every jetpack. `CCCP_BUNKER_WALK=1`
replaces the AI with the walk key held towards the goal.

## Addendum: traces of Bywater courses 1 and 5 (same build)

### Course 1, `AIBywater_trace1.log`: now close, it reaches the top room's height at 55 s

The first hatch (2-5 s) and the second climb (5-8 s) work. Time is lost in three places:
1. **9-12 s**: stands still at 1756,924 (fuel full) until `jet: stuck`. Its next point is 1716,915, then a crawl step to 1692,928.
2. **22-44 s, a 317 px climb started on too little fuel**: `step kind 2 to 1500,348 from 1461,667`. Fuel was 782 at 21 s and 117 at
   22 s. At up to 8 m/s (ba4f53b) the tank went in about a second, the unit ran dry at 1539,470, flailed for 6 s, fell all the way
   down and out to 1248,1008 at the bottom left, and took until 44 s to get back to the foot. The tank check let it start: at 8 m/s
   the climb needs more than the check reckons, or the burn rate at that speed is higher than modelled.
3. **54-60 s, overshoot at the top of the same climb** (second attempt, full tank): it reaches 1500,348, then carries on up at 5-7
   m/s to 1547,196, 1689,78 and 1768,33, passing its next points (1596,353 / 1644,329) as "passed" from 150 px above them, then
   falls back to 1511,562. Nothing brakes the climb near its top at the higher speed.

### Course 5, `AIBywater_trace5.log`

1. **3-33 s, trapped in the small pit** at 2220-2244, 1070-1140. Each try at the 54 px climb out (`wpt dx 0 dy -54`) burns most of a
   tank with almost no rise (fuel 1500 to 25 in 2 s at 5 s, then 247, 300, 100). That looks like jetting into the lip, then
   `climb: down again, asking for a new route`, and again. It gets out walking left at 33 s.
2. **45-60 s, a route whose first point is at the unit's feet**: standing at 1785-1791,1075 with a full tank, it re-paths **every
   second** (14 `path for` lines), and each route begins `1784,1072 -> 1788,1092(3)`, a Fall point 17-20 px under Pos, at its own feet,
   then the 293 px climb `1788,799(2)`. The Fall point is never popped, so the climb never starts, until `jet: stuck` at 55 s.
   Suggest: drop a first point that is within a few pixels of the unit's feet, or pop a Fall point when the unit is standing on the
   floor at its column.
