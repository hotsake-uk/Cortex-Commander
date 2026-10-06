# Requests to the test machine

Written by the cloud session working on the AI; read by the local session that builds and runs the gyms. Newest at the top. A
request is struck through (or removed) once its answer is in a `Results/<sha>/SUMMARY.md`.

## What each push expects (newest first)

The commit message of each push says the same at more length; this is the checklist.

- **(this push), head room for a step off an edge** (PathFinder HeadRoomFactor / StepKindBetween: a node with no floor under it has
  its free height measured from its own centre, so it says nothing about head room, and only the room where the step starts counts).
  Expect: the sky bunker's "down the hatch" and "down the stairs" routes carry no Crawl (1) steps where the unit walks into the hole
  or off a step (the route print shows (0)/(3) there), and no `crawl: going prone` at those points.

- **d4b81ebe, a fall costs by its height** (PathFinder: every step into a node more than a storey above the ground costs a rung's
  worth, 2.5, so a 400 px drop costs about 30 more than it did and a hatch's 96 px drop nothing). Expect on Bywater: the `AIBUNKER path
  for top room to the bottom corridor` line no longer runs down the outside at x 1500/1476 from y 372 to 780 but stays inside through
  the hatches, and "bottom right to the middle" likewise; on the sky bunker no route changes (checked offline); outdoors "down into the
  cave" and "down the slope" may take a slightly different line down but should still arrive. If an outdoor course regresses, that is
  this change.

- **ce08008f, the step off a climb lands** (the climb is done only with floor under the feet or the landing straight below; up at the
  height it drifts across at 2 m/s at the least; the 12 s re-path waits for a climb to end; RoomToPass counts the node's own column;
  a soldier's half width is 14 from its body, not the gun in its hand). Expect: sky bunker "up the hatch" and "bottom corridor to the
  top room" step onto the corridor instead of hovering in the mouth and dropping back; no `path: none to follow` mid-shaft. Also adds
  Tools/PathSim (an offline model of the grid; `pip install numpy pillow`), harness only.
- **8caa4f7d, the sky bunker rebuilt** (harness only): a sealed bunker of base-game modules with a shaft, a hatch, a hub crossing and
  steep stairs; seven courses on floors. The sky numbers start again here: 7 courses x 3 runs out of 21. From the offline grid model
  every course has a route inside the bunker, so a failure is a movement fault, not a layout one. Keep `CCCP_BUNKER_TRACE=1` (the shaft).
- **1cf3a694, the crab's jet is lift only** (its nozzle points straight up whatever the turret aims). Expect: "crab over the hill"
  arrives every run (it was one in three), no backward throws in its trace; "crab flat run" unchanged; humans unaffected.
- **158c93ba, the climb keeps to its column** (the apex where the feet clear the landing's floor; a jump point reached only from its
  height; a slow rise while the feet still foul the floor; the drift paced to the climb; the shaft's middle found across a 96 px
  opening). Expect on Bywater: "bottom corridor to the top room" steps onto the corridor after its first hatch instead of being turned
  for 1740,1092 from 28 px below the top; "mid left room to the right column" gets up the 45 px hatch at 1956 into the corridor at
  1980 instead of sticking under the slab at 1977,897. `pop: reached` for a jump point only with the unit at its height. Please trace
  Bywater course 2 (`CCCP_BUNKER_TRACE=2`) in one repeat.
- **2362a79e, standing room, one definition on both sides** (0.44 of the height, 44 px for a soldier, on the grid and in the script;
  the crawl probe measured from the floor; no lying down while climbing). Expect: no `crawl: going prone` in 48 px corridors; corridor
  courses at a walking pace; the Bywater unit stays on the corridor after its first hatch. Crawl steps in the grid begin under 44 px
  instead of 42, so nothing walked in the base modules (48 px) becomes a crawl.

## Open requests

1. The Bywater course 2 trace (above).
2. The `AIGYM unit` line for any other unit you run (height, radius, aim range), so the body fractions can be checked against it.

## ~~2026-10-06, before the first AI change~~ (answered in `7f09496/SUMMARY.md`)

The gym scripts now write an `AIGYM stall` / `AIBUNKER stall` block (the unit's state, the points left on its route, and the grid's
view of the 5 x 5 nodes around it) when a unit has stood still for four seconds and when it gives up, and the trace lines carry the
unit's aim angle and facing. So the per-stall grid dumps need no second run.

1. Include the sky bunker in every run: `Tools\RenderTest\AIBunker.ps1 -Scenario AIBunker` (Ketanot Hills with the three-storey bunker
   of base-game modules; AIBunker.lua's own five courses: low left to top right, across the gap, top left to low right, up the shaft,
   low right to mid left). Those are base-game layouts, which is the standard the user set, and they need no mod. Please give their
   pass/fail and times in SUMMARY.md alongside Bywater's.
2. For each Bywater and sky bunker course that gives up, the `AIBUNKER stall` blocks and the `AITRACE nodes:` line of the last route the
   unit had (the traced course only has AITRACE; if one course is clearly the worst, trace that one with `CCCP_BUNKER_TRACE=n`).
3. For "crab over the hill": the `AIGYM trace` lines (now with `aim` and `facing`) for the whole run, and the `AITRACE climb:` / `jet:`
   lines with `traceCourse = 15` in AIGym.lua (it's 16 at the moment). I want to see which way the crab faces, and its aim, at the
   moment it is thrown back down the slope.
4. The `AIGYM unit` lines (height, radius, jump height, aim range) for the Soldier Light and the Dreadnought: the grid's thresholds
   (standing room 0.42 of the height, crawl room 0.24, half width = radius / 2 clamped to 8..16) hang off them.
5. If Hemslock Hold has saved gym courses, its GYM lines too.
