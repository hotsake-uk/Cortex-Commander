# Physics and polish plan

Asked for on 2026-10-05: make liquids work properly and expand them, rework how detached terrain falls apart, and keep polishing the sandbox, graphics and shaders.

## References

- **Noita analysis** (a separate session's notes on how Noita updates its liquid and sand cells). Used for ideas only: each cell carries a velocity, falling accelerates and takes extra steps, a blocked cell builds sideways speed toward its open side and keeps sliding with damping, fast liquid that lands turns into flying drops, sand only slides diagonally, and cells that stop moving go to sleep. No code is taken from it: the notes come from a decompiled commercial game, so everything here is written from scratch against this engine's own terrain bitmaps.
- **Open source rigid body engines** (Box2D's sequential impulses): the impulse formulas for a body hitting a surface with bounce and friction, and the habit of moving in sub-steps no longer than a pixel so nothing tunnels.

## 1. Liquids (FluidSim)

| Step | What | Status |
|---|---|---|
| 1.1 | Each moving pixel carries a velocity: falls accelerate, sideways flow builds up and dies down, liquid sloshes back off walls | built |
| 1.2 | Layering by weight: oil floats on water, water on acid, everything on lava; heavier liquid sinks through lighter | built |
| 1.3 | Splashes: liquid that lands fast throws drops that fly and rejoin the pool | built |
| 1.4 | Loose powders: sand, snow, rubble and ash slide and pile at a slope when disturbed, and sink in liquid | built |
| 1.5 | Sandbox: pour sand; Lua `SceneMan:PourLiquid` takes powders too | built |
| 1.6 | Water look: surface line, depth shading | see section 4 |

Kept from before: finding a common level through connected bodies, reactions (lava and water, acid, fire), resting liquid costs nothing, saved games.

## 2. Terrain chunks (TerrainCollapse)

Before: a detached piece moved straight down as a frozen shape and stopped at first touch. Pieces holding any concrete or metal never fell.

| Step | What | Status |
|---|---|---|
| 2.1 | Detached pieces are rigid bodies: position, angle, speed, spin, mass and inertia from their materials | built |
| 2.2 | Collision with terrain by the piece's outline, in sub-steps of a pixel; impulses with bounce and friction, so pieces tip, roll and slide to rest | built |
| 2.3 | Breaking: a piece that lands faster than its material can take cracks into smaller pieces and loose debris | built |
| 2.4 | Pieces of buildings fall too once nothing holds them (doors and tiny fittings excepted) | built |
| 2.5 | Falling pieces can be shot apart, hit and push units, and push liquid out of the way instead of deleting it | built |
| 2.6 | Sandbox: drop a boulder | built |

## 3. Sandbox

| Step | What | Status |
|---|---|---|
| 3.1 | Sand and Boulder tools | built |
| 3.2 | Time controls (pause, slow motion, single step) | see log |

## 4. Graphics

| Step | What | Status |
|---|---|---|
| 4.1 | Water: bright surface line, darker with depth, foam where it moves | see log |

## Log

See the bottom of this file for what was checked and what was not.
