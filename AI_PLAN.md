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

11. **The grid has no idea of body size.** A 34 px tunnel is "open" to a standing unit; a one-node gap is a corridor; a jump chain's
    column can run up a cliff face the unit's body overlaps. The fix is a clearance-aware grid: nodes carry the free height above the floor
    and the free width, sideways edges require the unit's (standing or prone) height, jump columns keep half a body from walls. *Partly
    done:* walking edges are now sampled along the ground surface at each column, and the goal node is the one the unit stands at, which
    took out the zigzag along flat surfaces and made gentle slopes walkable.
12. **Jump edges aren't annotated.** The path is a list of points; the script re-derives "this is a jump" from the geometry, which is where
    most of the judgement errors come from. The pather should return the edge kind (walk, fall, jump of N nodes, dig, door) with each point,
    and the script should act on it directly: hold the jet for a known height, step off at a known column.
13. **Grid updates aren't double-buffered.** `RecalculateAreaCosts` writes nodes while solves read them on other threads; the starvation
    guard papers over it. Costs should be rebuilt into a copy and swapped.
14. **No path simplification.** Forty waypoints along a straight beam; each is "arrived at" with a 100 ms timer. Collinear runs should
    collapse, so arrival checks and the passed-waypoint popping have less to do.
15. **The jet model in the script is a guess.** `jetImpulseFactor`, `jetBurstFactor` and the 0.4 s horizon are tuned constants. The body
    knows its thrust, mass and fuel; expose a "height reachable with the fuel left" and "time to arrest this fall" and use those.
16. **Crabs.** `NativeCrabAI.lua` has its own, older `GoToWpt`; none of the above reaches it.
17. **Combat movement.** AttackTarget just walks at the target; no use of cover, no flanking, no retreat to refuel or heal; the "Guard"
    command mode in the sandbox is a hold with a wider look. The command tool's attack focus (`c_AttackTag`) is sandbox-side only.
18. **Squads** move as individuals that block each other (`teamBlockState` is a 20 s timeout). Formation offsets along the leader's path
    would do most of it.

## The gym

`Tools/RenderTest/RenderTest.rte/AIGym.lua`, scenario `AIGym` in `Tools/RenderTest/Setup.ps1`. It builds six courses out of concrete beams
(flat run, steps up with a 36 px hop and a 90 px jet, a 70 px gap, a 34 px tunnel with a pillar so there's no way over, over the scene's
own hill, and down into a cave that can't be reached without digging), sends a Soldier Light down each and writes `AIGYM` lines: the
pathfinder's answer for each course, a ground profile, a trace of each unit every two seconds, and when it arrived, died or gave up.
With `CCCP_AI_LOG=1` the movement script says why it jets (`AITRACE`), for actors given the `AITrace` number value; `CCCP_PATH_LOG=1`
prints every solve with its per-step costs.

Run it (the debug-release build, from `Tools\RenderTest\Build.ps1`, must not already be running):

```
powershell -ExecutionPolicy Bypass -File Tools\RenderTest\AIGym.ps1 -Runs 4
```

`-Trace` adds the `AITRACE` lines; the full console log of each run is left in `Tools\RenderTest\Output\aigym_<n>.txt`.

Where things stand (four runs, seconds to arrive):

| course             | before        | now                      |
|--------------------|---------------|--------------------------|
| flat run           | 12            | 12                       |
| gap                | 8–9           | 10–13                    |
| steps up           | impossible    | 12–15                    |
| low tunnel         | 24–35*        | 16–19                    |
| over the hill      | 20–60, deaths | 13–18, a rare give-up    |
| down into the cave | loops         | 15–17                    |
| down the slope     | –             | 10–11                    |
| up the slope       | –             | 12–41 (a 166 px cliff)   |

\* the tunnel used to be built into the hill, so its "detour" was the pathfinder being right.

The cliff is the course to watch: it takes most of a tank, the unit presses itself to the face (the push-off helps), and a unit that
arrives at the foot with half a tank now waits for it to fill rather than failing part way up.

## The plan

Each step is measured on the gym before and after, and committed on its own.

1. **Edge kinds in the path** (#12), since everything in the script that guesses can then stop guessing. `PathFinder` returns
   `{pos, kind, height}` per point; `Actor` keeps the kinds alongside `m_MovePath`; the Lua gets them through `MovePath` or a new accessor.
2. **Clearance-aware grid** (#11): free height per node, body-size-dependent sideways edges, standing nodes on the surface. The sampling
   fudge goes. Crawl passages and flat walking come out of this.
3. **Double-buffered cost updates and path simplification** (#13, #14): correctness and less waypoint churn.
4. **Jet numbers from the body** (#15): `AEJetpack` exposes reachable height and arrest time; the climb and the governor use them, the
   tuned constants go.
5. **Crabs** (#16): move `NativeCrabAI` onto the shared `GoToWpt`.
6. **Combat movement** (#17), **squads** (#18): new behaviours, each with a gym course (a wall to take cover behind, a target to flank,
   a squad to move as one).
7. More gym courses as problems are found in play: a cliff taller than one tank of fuel, a door, water, a narrow shaft to climb.
