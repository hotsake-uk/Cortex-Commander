# The AI overhaul

Branch `ai-overhaul`. The movement AI is the part of the game players have complained about longest: units walk into terrain, can't judge
jetpack jumps, fly past the ledge they were going for and come down hard, stand at the foot of a wall until the tank is full, or give up
on a route a human would take in ten seconds. This is the review of how it all fits together, what is wrong with it, what has been done
about it so far, and the plan for the rest. Progress is measured on the AI gym (below) rather than by eye.

## How a unit gets about

Three layers, each written by someone else, in a different language, at a different time.

**1. The path grid** (`Source/System/PathFinder.cpp`, MicroPather). The scene is covered by nodes 24 px apart, one grid per team (doors).
Each node knows the strongest material along the lines to its eight neighbours (`UpdateNodeCosts`, two rays 3 px apart per edge).
`AdjacentCost` turns that into A* edge costs: 1 for a step, 1.4 diagonal, plus the material's integrity, times a thousand when the
material is harder than the searcher can dig. Sideways moves are only offered from a node that stands on something (`NodeIsOnSolidGround`),
otherwise the only way is down: the grid knows about gravity. Going up is a "jump chain": from a grounded node, each node straight up
(or diagonally up) for as many nodes as the jump height allows, at a cost that grows with the square of the height. Terrain changes are
queued as boxes and the nodes inside re-sampled in `Scene::UpdatePathFinding`. Paths are solved on the thread pool; `Actor::UpdateMovePath`
asks, `Actor::PreControllerUpdate` collects the answer into `m_MovePath`.

**2. The movement script** (`Data/Base.rte/AI/SharedBehaviors.lua`, `GoToWpt`). A coroutine resumed every AI tick (`AIUpdateInterval` = 2
sim frames) by `NativeHumanAI.lua`. It takes the first point of `MovePath` as the waypoint, decides a lateral move, whether to jet, where
to aim the nozzle, whether to crawl or dig, and notices arrival. The jet decision was a planner that predicted the position after 0.4 s
of thrust in four facings and compared it with falling; "stuck" is a timer on the average speed with random remedies. `NativeHumanAI.lua`
turns the decisions into controller states (MOVE_LEFT, BODY_JUMP, ...) and runs the higher behaviours (Sentry, Patrol, AttackTarget,
BrainSearch, GoldDig, WeaponSearch, ShootArea, FaceAlarm, PickupHD) that give it waypoints. Crabs have their own copy of most of this.

**3. The body** (`AHuman.cpp`, `AEJetpack.cpp`, `Leg`/`Arm` limb paths). Walking is a limb-path state machine with its own climbing arms;
the jetpack is an emitter whose nozzle follows the aim angle within ±14° (`JetAngleRange` 0.16 of a right angle), always leaning a
little forward unless the unit strafes, with a burst (ten times the fuel) on the first press. `Actor::GetHeight` (`CharHeight`) is about
twice the sprite's height; the feet are a fifth of it under `Pos`.

## What was wrong (the review)

1. **Air waypoints had no path.** A target a little above the ground, or past the edge of what it was over, is an air node: unreachable
   except by a jump from directly below, so the unit got no route and flew at it blind. *Fixed:* targets are moved to the ground.
2. **The grid lagged the world by seconds.** 100 node updates every 100 ms; a built wall or a dug tunnel wasn't in the grid for a long
   time, so units were routed through walls and round holes that no longer existed. *Fixed:* 2000 per call every 33 ms, and if requests
   are in flight for 300 ms the update blocks on them.
3. **Ledges were unreachable.** A jump chain only lands where the chain ends, and a chain ends on a node in the air beside the ledge
   unless the geometry lines a diagonal up exactly. A 90 px platform was "impossible" from almost everywhere. *Fixed:* from every rung a
   step sideways onto a grounded node is offered too, which is what a jetpack really does.
4. **Going up cost too much.** `extraUpCost` 3 made a hill worth a long walk round, through the cave under it. *Fixed:* 1.5.
5. **The jet planner thrust for its own sake.** It compared "jet" with "fall" by distance to the waypoint, so it jetted along flat ground,
   ignored ceilings, and ran the tank dry; the burst was charged every relight though the wish flickers every tick. *Fixed:* a climb
   controller for anything more than a quarter height up, gates for walkable/too high/already fast, no burst charge when none can be had,
   a 150 ms hold after the planner lets go.
6. **The nozzle lean.** With the aim level the jet pushes 14° forward, so every climb was also an acceleration the way the unit faced.
   This is what sent units sailing past hill tops at 25 m/s and killed them on landing. *Fixed:* vertical climbs aim straight up, drift
   is flown by speed, and a governor leans the nozzle against any sideways speed over 8 m/s and brakes a fast fall onto ground, even
   while the tank is refilling (which used to skip all movement control).
7. **Refuelling froze the unit.** Out of fuel at the foot of a ledge, it stood until the tank was 98% full. *Fixed:* walking carries on.
8. **Stuck handling made things worse.** The remedy probabilities were per tick (so a new direction every few frames), the jet was lit
   whenever stuck regardless of what was overhead. *Fixed:* per-second chances with a held direction, one jet try with a head-room check.
9. **A route that needs digging was followed for ever.** Cost over 100000 means a wall the unit can't dig; it pushed at it. *Fixed:* three
   such answers in a row and the unit stands down (one can be down to where it happens to be wedged).
10. **The start of a path was always on the ground**, however far below; a unit part way up a climb was routed from the bottom and headed
    down for it. *Fixed:* only when the ground is within a body length.

Still open, in rough order of value:

11. **The grid has no idea of body size.** *Done:* each node now carries the head room above the floor it stands on and the room to
    either side of it; walking needs the searcher's crawl height (and costs 1.4x under its standing height), falling through and jumping up
    a column need its width, and the searcher's sizes go in with every search (`PathAgent`: jump, dig and breach strengths, stand and
    crawl heights, half width; `Actor::GetPathAgent`, humans with a crawl height). A jet column too close to a cliff face is refused
    and the next one out used.
12. **Jump edges aren't annotated.** *Done:* every step of a path comes back tagged walk / crawl / jump / fall / dig / door
    (`PathStepKind`, `Actor::GetMovePathStepKind`, `MovePathStepKind` in Lua), and the movement script acts on it: a jump step is
    jetted whatever the slope looks like, a fall step is walked off (no hop, no wall-jet), a crawl step is gone prone for, a dig step is
    never jetted.
13. **Grid updates aren't double-buffered.** *Done, without the copy:* the request count that the cost rebuild waits on is now taken
    from the moment a search is queued, not from when a worker picks it up, so a search still in the queue can't start on a grid being
    written (new searches are only queued from the main thread, which is the one doing the rebuild). The rebuild runs with nothing
    queued or running, so there is nothing to double-buffer.
14. **No path simplification.** *Done:* walks that keep heading the same way on much the same level are run together in the pather's
    answer, up to eight nodes a waypoint; crawls, jumps, falls, digs and doors keep their own points. With it, a jump that is up a lot
    and over a little (a jet column with a landing on the ledge beside it) gets the top of the column as a point of its own, a node and a
    half over the landing: flown as the one straight line it was, the line went into the face under the ledge's lip.
15. **The jet model in the script is a guess.** *Done:* `SharedBehaviors.JetNumbers` works the net climb acceleration, the distance a fall
    at a given speed takes to arrest, and the height the fuel left buys, from the body (`EstimateImpulse`, mass, `JumpHeight`, the tank).
    The landing brake fires on the real stopping distance instead of "half a second's fall", and a tall climb waits for the fuel the
    height actually wants instead of 85% of a tank. Found on the way, in the grid: a node whose centre sat a couple of pixels under a
    slope's surface was a dead end upwards (its own surface was in the way of every line out of it), and the thick bushes on a hillside
    (integrity 20, "diggable") weren't ground at all, so a 55-degree slope was climbed by a 264 px jet from a column further along it and
    a glide back down. Edges are now sampled between where a body stands at each node, anything that isn't air is stood on, and the
    walking band sits a few pixels up off the bumps. A climb's lip memory clears only once the feet are past the lip.
16. **Crabs.** `NativeCrabAI.lua` shares `GoToWpt`, so the movement work reaches them, but nothing has been measured on a crab: no head
    for the wall probe, legs on both sides, jets on some. They need a gym course of their own.
17. **Combat movement.** *Done, for humans (crabs get the engagement rule and the retreat):* the fighting rules read the unit's standing
    order (`SharedBehaviors.OrderKind`: move / attack / defend / guard, from the AI mode and the sandbox's tags, so a game waypoint order
    and a sandbox order fight the same way). A move order's unit fires on the way and doesn't stop; attack and guard units stop, fight and
    close in; a defender stands. In range they hold about half the weapon's reach (snipers most of it, explosives clear of the blast),
    closing in by the path when further off, backing off a step when much nearer, and shifting a step now and then (`HoldRange`).
    Reloads and a bad hit are taken behind cover a few steps away when there is any (`FindCover`, `TakeCover`, `LeaveCover`: in a side
    view that is under or back from a ledge, not behind a wall). A target in sight that the shots can't reach for 1.5 s is flanked: a
    spot above or beside it with a line of sight, within fifty path nodes (`FindFlank`, `StartFlank`, `FlankUpdate`; also from the last
    known spot of an enemy that was shooting at us). Under 30% health with no enemy in sight a unit falls back to the brain or a friend
    (never through the enemy; away from it if there's no one the right side), waits 25 s to be patched up, and takes its order up again
    (`RetreatUpdate`); not brains, defenders or player-posted sentries. Strafing and flanking scale with the team's AI skill. The sandbox
    leaves units that are falling back or flanking alone.
18. **Squads** move as individuals. (The `teamBlockState` machinery that looks like it handles team-mates in the way is dead: nothing
    ever sets BLOCKED.) Formation offsets along the leader's path would do most of what's wanted.

## The gym

`Tools/RenderTest/RenderTest.rte/AIGym.lua`, scenario `AIGym` in `Tools/RenderTest/Setup.ps1`. It builds courses out of concrete beams,
clear of the scene's own hill and of each other, sends a unit down each and writes `AIGYM` lines: the pathfinder's answer for each course,
a ground profile, a trace of each unit every two seconds, and when it arrived, died or gave up. With `-Trace` (`CCCP_AI_LOG=1`) the
movement script says why it jets (`AITRACE`), for the unit of the course `traceCourse` names (the `AITrace` number value on the actor);
`CCCP_PATH_LOG=1` prints every solve with its per-step costs, and the cost of the grid updates every five seconds.

Run it (the debug-release build, from `Tools\RenderTest\Build.ps1`, must not already be running):

```
powershell -ExecutionPolicy Bypass -File Tools\RenderTest\AIGym.ps1 -Runs 4
```

`-Trace` adds the `AITRACE` lines; the full console log of each run is left in `Tools\RenderTest\Outputigym_<n>.txt`.

Where things stand (seconds to arrive, over several runs):

| course              | what it asks                                              | before         | now                 |
|---------------------|-----------------------------------------------------------|----------------|---------------------|
| flat run            | 800 px of beam                                            | 12             | 10–12               |
| steps up            | a 36 px hop, then a 90 px jet onto a ledge                | impossible     | 12–16               |
| gap                 | 70 px to jump                                             | 8–9            | 10–14               |
| low tunnel          | 34 px of head room for 320 px, with a wall over the top   | 24–35*         | 16–20               |
| through the room    | two 30 px doorways                                        | –              | 11–14               |
| through the door    | a 56 px doorway with a team door shut across it           | –              | 5                   |
| over the hill       | the scene's hill, 60° up one side and down the other      | 20–60, deaths  | 13–20, a rare give-up |
| down into the cave  | under the hill                                            | loops          | 15–23               |
| down the slope      | 500 px down the far side                                  | –              | 9–12                |
| up the slope        | back up it, over a 166 px vertical cliff                  | –              | 12–40               |
| dig down            | a Heavy Digger, 140 px into the valley floor              | never          | 19–30               |
| crab flat run       | a Dummy Dreadnought on the beam                           | –              | 15                  |
| crab over the hill  | the Dreadnought over the hill                             | –              | 22–50, fails half the time |

\* the tunnel used to be built into the hill, so its "detour" was the pathfinder being right.

The crab is the one to watch: its jet is weak for the 60° slope, and when a re-path from part way up gives it the long way round
through the valley it goes, and falls into the far one. The cliff takes most of a human's tank; a unit that arrives at the foot with
half a tank waits for it to fill rather than failing part way up.

### The combat gym

`Tools\RenderTest\AICombat.ps1` (scenario `AICombat`, script `RenderTest.rte/AICombat.lua`): five small fights on beams in the sky, one per
rule, written up as AICOMBAT lines every two seconds (position, health, mode, whether each unit's eyes have a line to the other) and the
AI's own AITRACE lines (`range:`, `cover:`, `flank:`, `retreat:`). firefight: an attacker sent at a defender along a bare beam. cover: a
unit below a ledge the enemy stands on, its head showing over the lip; reloads go under the ledge and come out again. dug in: the same
with the body hidden too, which the attacker can't hit and should go round. move: a move-order unit walks past an enemy on a ledge and
should arrive. retreat: a unit on 20 health with its brain down the beam. As of 2026-10-06 all five do what they should; the dug-in
attacker usually dies first, as it would.

## On a real map

`Tools\RenderTest\RenderTest.rte\AIMap.lua` (scenario `AIMap`, same harness as the gym) sends a soldier, a soldier with a Heavy Digger and
a Dreadnought from the landing zone of Zekarra Mining Outpost to the brain room deep in its bunker, 2200 px away through corridors and
doors of the other team, with the garrison removed. The digger gets there in 50–90 s. The plain soldier and the crab get as far as the
bunker's doors (about 250 px short) and stop: nothing they carry can open a blast door, and that is the mission's design. On the way the
map found what the gym hadn't: a prone unit's jet drives it along the ground, the crawl decision flapped, a unit climbing round an
overhang kept drifting back under it, enemy doors were "open" to everyone in the path grid, and a route that needs what the unit hasn't
got is now cut short at the obstacle so the unit goes as far as it can.

## The plan

Each step is measured on the gym before and after, and committed on its own.

1. **Edge kinds in the path** (#12), since everything in the script that guesses can then stop guessing. `PathFinder` returns
   `{pos, kind, height}` per point; `Actor` keeps the kinds alongside `m_MovePath`; the Lua gets them through `MovePath` or a new accessor.
2. **Clearance-aware grid** (#11): free height per node, body-size-dependent sideways edges, standing nodes on the surface. The sampling
   fudge goes. Crawl passages and flat walking come out of this.
3. **Double-buffered cost updates and path simplification** (#13, #14 done): correctness and less waypoint churn.
4. **Jet numbers from the body** (#15, done): `AEJetpack` exposes reachable height and arrest time; the climb and the governor use them, the
   tuned constants go.
5. **Crabs** (#16): a crab on the gym courses, and whatever that shows.
6. **Combat movement** (#17, done; see the combat gym), **squads** (#18): new behaviours, each with a gym course (a wall to take cover
   behind, a target to flank, a squad to move as one).
7. More gym courses as problems are found in play: a cliff taller than one tank of fuel, a door, water, a narrow shaft to climb.

## Handover, 2026-10-06 evening

Branch `ai-overhaul` on hotsake-uk/Cortex-Commander (origin). Last commit 423329a92. Everything below is pushed.

### Where things stand

- Outdoor gym (`Tools\RenderTest\AIGym.ps1 -Runs N -Wait 70`, courses in `RenderTest.rte/AIGym.lua`): all 15 human courses
  pass, in simulation seconds. Open: the crab over the hill (#16): the crab's jet lean throws it backwards off the slope.
- Combat gym (`Tools\RenderTest\AICombat.ps1 -Trace`): five fights, all do what they should.
- Indoor gym: `Tools\RenderTest\Gym.ps1 -Run -Maps "Bywater Barracks,Hemslock Hold" -Exe "Cortex Command.debug.release.exe"`
  runs the maps' saved courses (`Userdata\Gyms\<map>.txt`, made in play with the sandbox's Gym tab, god mode) at the same time in
  their own windows. The maps come from the BB+ mod in `Mods\BB+.rte` (git-ignored: only the machine with the mod can run them).
  `Tools\RenderTest\AIBunker.ps1 -Scenario AIBywater -Trace` runs the five courses written into `RenderTest.rte/AIBunker.lua`
  instead when the map has no saved gym. On Bywater Barracks the units now climb shafts and hatches, but none of those five
  courses finishes inside 60 s: the routes are long and there are stalls still to find. That is the work in front of us.
- The user's standard: units should only get stuck in impossible spots, never in base-game bunker layouts, inside or out.

### How to look at a stall

1. Run one course, zoomed, traced, with the grid around a point dumped:
   `CCCP_BUNKER_VIEW=x,y,zoom CCCP_BUNKER_ONLY=n CCCP_BUNKER_TRACE=n CCCP_BUNKER_DUMP=x,y CCCP_AI_LOG=1
   CCCP_CONSOLE_LOG=<abs path> powershell -File Tools\RenderTest\Capture.ps1 -Scenario AIBywater -Name X -ExtraWait 4 -Burst 6 -BurstIntervalMs 8000`
   (run `Tools\RenderTest\Setup.ps1` first after editing anything under `Tools\RenderTest\RenderTest.rte`; it copies the module into Mods).
2. In the log: `AITRACE nodes:` is the route with each point's step kind (0 walk, 1 crawl, 2 jump, 3 fall, 4 dig, 5 door);
   `AITRACE pop:` says why a waypoint was dropped; `AITRACE climb:` / `jet:` say what the jet logic did; `AIBUNKER grid node ...`
   is the grid's view of a node (surface, ground, free head room, clearance either side, the material each way, 0 = air);
   `PATHLOG` (with `CCCP_PATH_LOG=1`) has every solve with its cost and time. Captures land in `Tools\RenderTest\Output`.
3. The pieces: `Source/System/PathFinder.cpp` (grid: `UpdateNodeCosts`, `AdjacentCost` with the jump chains and landings,
   `StepKindBetween`, the apex and the straight-run merging in `CalculatePath`), `Source/Entities/Actor.cpp` (`UpdateMovePath`,
   `PreControllerUpdate`, the path display), `Data/Base.rte/AI/SharedBehaviors.lua` (`GoToWpt`: the climb controller from
   "A climb:" on, waypoint popping, the stuck handling; the combat rules in `OrderKind`/`FightsOnTheMove`/`RetreatUpdate`/
   `FindFlank`), `HumanBehaviors.lua` (`ShootTarget`, `HoldRange`, `TakeCover`), `NativeHumanAI.lua`.

### Rules of the road

- Commit as `147993921+hotsake-uk@users.noreply.github.com`, push to origin only, never upstream; never push the
  `backup/modernisation-before-email-rewrite` branch. Mods are not committed.
- Debug build: `Tools\RenderTest\Build.ps1`; Final: `-Config Final`, only when `Cortex Command.exe` isn't running (the user plays it).
- Measure before and after on the gyms; sim-time numbers are the ones that count.

## The cloud session, 2026-10-06 evening on

Branch `ai-overhaul`, developed in a cloud session that cannot run the game, tested on the user's Windows machine by a local session
that watches the branch's tip: every push gets a build and the full gym run, three repeats of each indoor suite, and the results come
back as a commit under `Tools/RenderTest/Results/<sha>/` (a `SUMMARY.md` with a pass table and a diagnosis of every failure, and the
raw logs). Requests from the cloud side to the test side go in `Tools/RenderTest/Results/REQUESTS.md`, with a checklist of what each
push is expected to change. One change per push, so each run measures one thing.

### What the first test run found (Results/7f09496)

The indoor stalls came mostly from the grid and the movement script disagreeing about standing room: the grid called a 48 px corridor
walkable (standing room 0.42 of the height, 42 px) while the script probed for the head 55 px over the floor, went prone in every
corridor and crawled at five pixels a second, and a unit fresh out of a hatch lay down over the hole and slid back in. Second, climbs
drifted towards their landing while still far below it, into the side of the opening or under the floor slab. Third, the crab's turret
(aim range 0.5 rad) could never aim the nozzle straight up, so its every climb was a push the way it faced. And the sky bunker as
first built was not a fair test: Hub A is open on all four sides, so stacked hubs were chimneys open to the sky at both ends.

### What was changed, in push order

1. The gyms write a stall block (state, route left, the 5 x 5 grid around the unit) at four seconds still and at give-up; aim and
   facing in the trace lines.
2. One definition of standing room, 0.44 of the height (44 px for a soldier, head 24 px over Pos, feet 20 under): `AHuman::GetPathAgent`
   and `SharedBehaviors.StandingHeight`; the crawl probe measured from the floor under the unit; head-high rays start under the head's
   top; the wall-ahead chest ray at 0.1 of the height; no lying down while climbing, stepping off a climb or in the air.
3. The climb keeps to its column: the pather's apex point sits where the feet have just cleared the landing's floor (as low as the
   landing's ceiling allows); a jump point is reached only from its height; a slow rise while the feet still foul the floor; the drift
   paced to the climb; the shaft's middle looked for a body's height either way.
4. A crab's jet is lift only: `NativeCrabAI.lua` pushes the controller's move stick straight up whenever the jet is lit, which the
   jetpack turns into a vertical nozzle whatever the turret aims.
5. The sky bunker rebuilt as a sealed layout (shaft, hatch, hub crossing, steep stairs, dead ends, a corner), seven courses on floors.
6. The step off a climb lands: done only with floor under the feet (or the landing straight below), and at height the drift across is
   a walking pace at the least; the 12 s re-path waits for a climb to end; RoomToPass counts the node's own column (a 48 px hatch is
   48, not 47); a human's half width is 0.14 of the height, from the body, not the radius (which reaches to the gun in the hand).
7. A fall costs by its height: every step into a node more than a storey above the ground costs a rung's worth (2.5), charged per node
   so a search with no memory can't dodge it by hopping off a rung; hatch drops and slopes cost nothing extra.
8. A step off an edge is not a crawl: an airborne node's free height is measured from its centre and says nothing about head room.

### Tools/PathSim

An offline Python model of the path grid, `PathFinder.cpp` mirrored function by function, run on the sky bunker built from the
modules' material bitmaps. It gives the game's routes node for node (checked against the `AITRACE nodes:` lines) and is how the new
layout was designed and how the grid changes above were checked before a push: `python3 -I Tools/PathSim/pathsim.py sky --layout new`.
Keep `grid_rules.py` in step with the C++. It models the grid only; whether a unit can follow a route is the gym's business.

### Open, as of this writing

- Results for pushes 2 to 8 are pending on the test machine; the expectations are in `Results/REQUESTS.md`.
- Bywater's "mid left room to the right column" (the course that never passed) and the crab over the hill are the two to watch.
- The steep stairs are climbed by the grid as jet hops (the diagonal chains are 1:1, the stairs 2:1); whether the legs walk them is
  for the gym to say.
- Not yet looked at: squads (#18), the four-facing jet planner indoors (its rays start 40 px over Pos, inside a 48 px ceiling, so it
  never jets indoors; the climb controller does that work), doors on Bywater.
