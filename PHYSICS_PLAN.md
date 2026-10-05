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
| 1.6 | Water look: foam, bending what's behind it | not done, see section 5 |

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
| 3.1 | Loose sand, loose snow, boulder, concrete lump and ice tools | built |
| 3.2 | Speed of time slider (0.05x to 3x) with Slow, Normal and Fast buttons | built; a full pause and single step are not |

## 4. More liquid behaviour (added while building)

| Step | What | Status |
|---|---|---|
| 4.1 | Explosions, units and boulders falling in throw liquid as drops that rejoin it | built |
| 4.2 | Drops that land inside liquid rise to its surface (before, most of a splash was lost) | built |
| 4.3 | Still water freezes over in snowy weather; lava melts ice and snow | built |

## 5. Not done

- **Water's look.** It already had a surface line, depth shading and ripples; nothing was changed. Foam where it moves and bending of what's behind it are still ideas.
- **Open pits look black.** A pit dug open to the sky shows the scene's dark back wall, not sky. Seen in the tests; not looked into.
- **Electricity through water, mud, a cryo weapon.** From the earlier agreed list; not built.
- **Saving falling pieces.** A saved game keeps them as ground where they were.

## Log

2026-10-05:
- **Checked by test captures:** boulders fall, turn and settle on a slope (Physics); a disc cut loose in a hill drops and settles (Collapse); a 19,000-pixel section of a bunker broke loose under bombing and tipped over (BlastDay); sand piles at a slope; oil and water run down a hill; a grenade and a boulder in a pool (Splash).
- **Checked by numbers:** two pits joined by a tunnel come to one level (Level, with collapse off: the block of earth between the pits now rightly falls into the tunnel). A grenade in a small pool: 2170 pixels of water before, 1600 back in the pool after, the rest thrown clear of the pit; before the fix 600 came back. A boulder dropped in loses about 1%.
- **Cost:** the heaviest flood (80,000 moving pixels) 7 to 10 ms an update, as before. Chunk physics 0.1 ms an update under bombing, 4 ms at worst.
- **The seven regression scenes pass.**
- **Not checked by eye:** freezing (a pale crust was starting after 50 s of snow, but I can't tell it from the surface line), lava melting ice, liquids layering in a deep pool, a unit's splash, breaking of a big boulder from a long fall, how a falling building piece treats units inside it, the Sandbox's new buttons and the time slider (built, compiled, not clicked).

2026-10-05, later (after the user's report that water sat as a heap, and that falling pieces froze at first touch):
- **Liquid:** surface pixels look along their own row, through liquid, for a lower place (up to 720 pixels for water) and go there; a pixel off an edge slides down the face in one step; no more aimless wandering along the top; rows are visited left to right and right to left on alternate updates; water falls and runs about a third faster; the sweep for missed liquid covers the map three times as often and wakes one surface pixel in 16 a pass to look for a lower place through the body (joined pools).
- **Measured (Heap test, 3,700 pixels poured into one end of a 570-pixel trough):** before, depth 4 to 11 pixels 50 seconds after pouring; now flat within the floor's own unevenness when pouring ends, and nothing moving two seconds later. Flood test: the hill drains fully; 4 ms an update typical, 10 ms worst. Level test: joined pits equal in about 15 seconds from a 75-pixel difference.
- **Falling pieces:** position correction after contact (they roll now: a 640-pixel rock tumbled 350 pixels down the test hill), a little more bounce, and two and a half seconds of lying still before a piece becomes ground.
- **Reference:** the second Noita note (LIQUID_SIM_HANDOFF.md) was read. Taken from it: alternating the scan direction, and waking on change rather than polling. Not taken: its liquids level only from their edges by per-cell momentum, which is the slow behaviour reported here; the row look above is this engine's own answer. Its 26% chance for a heavy liquid to sink into a lighter one (a ragged, bubbling front) is not used.
