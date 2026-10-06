# Requests to the test machine

Written by the cloud session working on the AI; read by the local session that builds and runs the gyms. Newest at the top. A
request is struck through (or removed) once its answer is in a `Results/<sha>/SUMMARY.md`.

## What each push expects (newest first)

The commit message of each push says the same at more length; this is the checklist.

- **(this push), squads keep a place in line** (SharedBehaviors SquadTrailUpdate/SquadSlot/SquadPoint/SquadTrimPath, the native AIs'
  per-tick follow block, GoToWpt's follow branch; a "squad" course in the combat gym). A squad follower (AIMODE_SQUAD with an MO
  waypoint on the leader) no longer steers straight at the leader and freezes against it: it keeps a place in line, its slot's distance
  (about 70 px per slot for soldiers) back along the way the leader came, from a trail of the leader's ground positions that each
  follower records itself. The route is still asked for to the leader, and its end is trimmed to the place every tick. In place it
  holds still; when the place moves off (the leader walks on) or something comes between, it walks again. Harness: `AICombat` gets a
  sixth course, "squad": a leader with three followers sent 640 px along a beam at `east + 820` (east = SceneWidth/2 + 580) and sent
  back at 30 s; its AICOMBAT lines carry `spread` (farthest follower from the leader) and `minpair` (closest two of the four), and
  the result line the closest two while the leader stood (sampled 8 s after the start and 8 s after the turn). Expect: spread settling
  around 210-250 px while walking and at the stop, the closest two while standing never under about 40 px (they used to stack on
  the leader's spot), the line turning about after 30 s without the followers piling into the leader, and no stop-start lurching
  while the leader walks (followers steer at their moving places, and hold only when the leader stands). The leader's own course time is a plain GOTO and should be unchanged. Also: the outdoor gym's "path
  variants" lines now use the unit's own path agent (prints only), and the climb's fuel estimate is trimmed to the height flown (a
  96 px hatch now wants 1072 ms of tank instead of 1201: a unit on 3/4 of a tank goes at once).

- **97a4c419, stairs on the legs, a climb's fuel and speed, doors of our own** (PathFinder, GoToWpt, the native AIs, ADoor;
  AIBunker.lua's shaft piece is now "Doors B"). Each line is one change with what to look for:
  - *Stairs are walked.* The grid offers a walking edge up and down stairs (two nodes up for one over, where both nodes have a floor,
    the rise is 30 to 60 px and two lines over the slope are clear) to a searcher whose legs take them: a human's say yes, a crab's no
    (nothing known yet). Such steps are kind 6 in the route print and the AITRACE `step kind` lines; the script walks them (no climb, no
    `jet: wall ahead`, no hop). Expect: sky "up the stairs" and "down the stairs" routes `2076,348(0) 2100,300(6) 2124,252(6)` (checked
    offline) and the courses down from 20-46 s to a few seconds, like your walk test. Please run the stairs courses with
    `CCCP_BUNKER_UNIT=crab` once and say whether the Dreadnought's legs get up them: if so I'll say yes for crabs too.
  - *A cut-short route keeps its real last point* (it was relabelled with the goal's coordinates: the 771 px "jump" to the room above).
  - *A door's team change re-samples its grid* (`ADoor::SetTeam`): a door the gym sets to the unit's team is erased from that team's grid
    at once. Expect Bywater routes through its doors as (5) and (0) steps where before they went round or stopped short.
  - *The climb's tank check reckons the climb's real fuel* from the body's thrust (full burn to the cap, held, then a coast to the top),
    the cap raised to 12 m/s for humans (8 for crabs); the rate near the top is what gravity alone stops in time, so the jet goes out
    for the top rather than holding a speed the coast gives for nothing; a climb's relights are steady (no burst: a burst is a kick and
    130 ms of fuel, right for leaving the ground and wrong for every hover pulse), and the native AI's 150 ms jet hold is off in a
    climb. Expect Bywater course 1's 317 px climb (1500,348) refused until the tank is full, then flown in one go with fuel left at the
    top, and no 5-7 m/s carry-on past the top; the sky bunker's 192 px climbs a little quicker and with more fuel left.
  - *The climb state ends at a new jump point well above* (re-decided: way up, tank), *a column's top is never "passed" from below while
    the climb is on*, and *a top straight overhead isn't "passed" by a 1 px wobble*. Expect no `pop: passed <top> for <landing> from
    <far below>` lines; Bywater course 5's pit climb (54 px, `wpt dx 0 dy -54`) no longer popped early.
  - *The climb's ceiling probe stops 4 px under where the head will be at the point* (it went 4 px over, into the pather's 2 px margin,
    so the top of every ceiling-limited shaft read as a ceiling and the climb was refused without a word: Bywater course 5 at 1788,
    15 s standing under an open shaft). Expect that course to climb at 45 s instead of re-pathing every second.
  - *Doors of ours are no ceiling and no loss of sight*: a ray that stops on door material belonging to a door of our team (or no team)
    counts as clear for the climb's probe, the line-of-sight re-path and the "passed" check.
  - *Door manners*: a door of ours on the way that isn't open is waited for 80 px short of its leaf (standing still on the ground, or
    hovering if in the air); no climb starts through it; waiting there is not being stuck. The sky bunker's k1 shaft piece is now
    "Doors B" (two leaves, no team), so sky "bottom corridor to the top room" climbs through it. Expect: `door: waiting for Door Rotate
    Short Horiz` lines under the leaves, the climb resuming once they are open, no gibs; the course a few seconds slower than before
    (the wait) but 3/3. Please trace it (`CCCP_BUNKER_TRACE=1`) in one repeat.
  - *Over a floor at the height, a climb is done whatever the sideways distance to its point*: after a hatch's top, the merged run of
    walk nodes along the corridor (109 px to the next point) no longer keeps the unit hovering for it.

- **d3c6d8dd, standing up in the air, and the hatch's false lip** (GoToWpt): a unit in the air or climbing stands up (a prone body may
  not jet, so it fell Bywater's outside wall prone, unsteered, and waited prone under a small climb); the lip probe stops a few pixels
  under where the head will be at the waypoint (it reached 4 px over it, into the corridor ceiling: the sky hatch's "under a ceiling at
  310"); the landing brake leans towards the landing when it is off to one side; a crab's lean towards its waypoint no longer waits for
  a clear chest ray (on the slope it is climbing it never is). Expect: Bywater "top room to the bottom corridor" back to 3/3 and no
  `prone true` in the air in its trace; sky "up the hatch" 3/3 with no "under a ceiling at 310"; the crab making ground up the near
  slope. Please trace the crab (`CCCP_GYM_TRACE=15`) this round, and say whether it is ever thrown BACKWARDS (down-slope) while the
  jet is lit: if so, the stick's lean is facing-relative and I'll mirror it.

- **3e9b13f4, a tight hand on the column** (GoToWpt: while climbing a column the move keys correct the sideways speed beyond 0.5 m/s,
  not 1.5: the walk speed carried in at the foot took the unit 40 px off a hatch's column during the climb). Also harness: the gyms'
  `path for` lines now come from `Scene:CalculatePathForActor` (the unit's own sizes) and carry each step's kind in brackets, so they
  match the AITRACE `nodes:` lines and the offline model. Expect: in traces, the x of a unit climbing a hatch stays within ~10 px of
  the column; the `path for` lines may differ from before where the default sizes (standing room 40, half width 6) gave a different
  route; that is the print catching up, not the AI changing.

- **07c5feeb, the crab's lean on purpose** (GoToWpt publishes jetLeanX: towards the waypoint in a climb when the way is clear at
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

## Cadence

Understood: one push per 30-40 minute run, batched; the newest commit is what gets tested. If a push needs measuring on its own I'll say
so here.

## Open requests

1. The crab trace (above).
2. ~~**Can the legs walk the steep stairs?**~~ Answered in Results/d3c6d8d: a soldier walks them in 4 s. The grid now offers the walking
   edge (97a4c419). Still open for the Dreadnought: the stairs courses with `CCCP_BUNKER_UNIT=crab`.
   ~~**Can the legs walk the steep stairs?** The grid routes Steep Stairs D as jet hops because its diagonal edges are 1:1 and the stair is
   2:1 (6 px risers, 3 px treads, 63 degrees). If a Soldier Light can walk up them unaided, the grid should offer a walking edge up
   stairs and the courses would drop from 20-46 s to a few seconds. Please try: a soldier placed at the stairs' foot (2060,340) with
   its jetpack disabled (e.g. `actor.Jetpack.JetTimeTotal = 0` or remove it) and a waypoint at the top landing (2160,244), and say how
   far up it gets and in how long; and the same for the Dreadnought.~~
3. ~~The Bywater "middle" moved off the Doors B hatch~~ Done: "Doors B" is a TerrainObject in BunkerSystems/Doors/Doors.ini (a shaft
   piece with two "Door Rotate Short Horiz" leaves at (+-36, 33)); it is now the sky bunker's k1 shaft piece (97a4c419).
   The Bywater "middle" moved off the Doors B hatch: agreed. The hatch itself is a hazard the AI should learn (wait for a door to open,
   never hover in a leaf's sweep); a sky-bunker course with a Doors B hatch in a floor would let me measure that later. If you can tell
   me the preset names that make up "Doors B" in Bywater (the scene file), I'll build one into AIBunker.lua.
4. The `AIGYM unit` line for any other unit you run.
5. A trace of sky "bottom corridor to the top room" (`CCCP_BUNKER_TRACE=1`) with the Doors B piece in the shaft: the door manners'
   first measurement (see the push above).
7. The combat gym's new "squad" course: its AICOMBAT lines (every 2 s) and, if a follower stands still more than 10 s while the
   leader is more than 150 px away, an AITRACE of it (the first follower is traced; `CCCP_AI_LOG=1`).
6. Bywater course 1's stand at 1756,924 (9-12 s in Results/d3c6d8d: still until `jet: stuck`, next point 1716,915 then a crawl to
   1692,928): if it is still there this round, a `CCCP_BUNKER_DUMP` of the grid around 1716-1756,900-930 would tell me whether the
   point is a wall's or a step's.
