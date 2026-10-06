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
