# Requests to the test machine

Written by the cloud session working on the AI; read by the local session that builds and runs the gyms. Newest at the top. A
request is struck through (or removed) once its answer is in a `Results/<sha>/SUMMARY.md`.

## What each push expects (newest first)

The commit message of each push says the same at more length; this is the checklist.

- **(this push), the crab's lean on purpose** (GoToWpt publishes jetLeanX: towards the waypoint in a climb when the way is clear at
  chest height, against the speed when braking, else 0; NativeCrabAI sets the move stick to (0.27 x lean, -1), which is a nozzle lean
  in screen terms whatever the crab faces; the four-facing planner predicts a crab's thrust vertical and no longer flips its facing).
  Expect: "crab over the hill" makes progress up the near slope instead of rising and falling in its own column, and arrives; "crab
  flat run" back to ~15 s (no jets on the flat: a vertical jet never beats a walk in the planner's sums). If the crab is thrown
  BACKWARDS again, the stick's lean is facing-relative after all and I'll mirror it by HFlipped.

- **ba4f53b1, the climb's ending, from the review** (GoToWpt): a top-of-column point always hands the drift and the step off to the
  landing after it; its height rule applies only in the air; a wall is told from a slope by the two rays hitting at the same distance,
  so 45 degree ground is walked again; a room up to 200 px wide is no longer taken for a shaft; the crawl probe never drops below the
  standing head; tall climbs fly at up to 8 m/s and the tank check wants the climb's real fuel plus a reserve for the top (a 192 px
  shaft left 10% of the tank at the top before). Expect: sky "bottom corridor to the top room" and "bottom to the top gallery" well
  under 30 s with fuel to spare at the top; no `jet: wall ahead` on the hill's 45 degree steps; "steps up" and "onto the ledge" as
  before or faster.

- **7d8b18a7, the climb's ending, five faults from Results/af7836b** (GoToWpt): only an apex over a drop waits for its height, a
  jump point on the floor pops as before (fixes outdoor "through the room" back to ~14 s); a jump landing beside the unit at its height
  is not popped from 30 px away (Bywater course 5's pit, course 1's hatch); the lip probe reaches no higher than where the head will be
  at the waypoint, so a corridor's ceiling is no longer a lip pushing the unit onto the far lip (sky "up the hatch", "bottom corridor
  to the top room"); a climb that leaves the unit on the ground for 1.5 s has failed and asks for a new route at once, and the 12 s
  re-path is held only while the climb is in the air (no more one-point routes held for 45 s); a crab's climb cuts at the height and
  its legs drive towards the waypoint (crab over the hill: no 100 px overshoot, tank not burned on a 23 px climb). Expect sky 21/21
  or close, Bywater courses 1 and 5 improving, crab arriving, "through the room" 14 s.

- **c553485a, head room for a step off an edge** (PathFinder HeadRoomFactor / StepKindBetween: a node with no floor under it has
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

1. **The Bywater middle-room deaths** (af7836b fault 4): yes please, a capture of "middle to the mid left room" around 1812,432 in its
   first five seconds, to see whether a door closes on the unit. If it is a door, the movement script needs door manners (wait for
   it to open; don't jet through a doorway), which I'll write once confirmed; tell me the door's preset and which way it moves.
2. The `AIGYM unit` line for any other unit you run (height, radius, aim range), so the body fractions can be checked against it.
