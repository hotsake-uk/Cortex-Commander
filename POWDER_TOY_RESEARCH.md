# The Powder Toy: what we can take from it

Research done 2026-10-05 against The Powder Toy's `master` (commit `99a1882`), read from source. Nothing in this repository has been changed by it.

## The short answer

- **Its air system is the prize.** A 575-line, self-contained simulation of air pressure, air velocity and air heat on a coarse grid. We have nothing like it, and most of what would make explosions, smoke, fire and weather feel physical comes from it. It can be taken nearly as it is.
- **Its liquid and powder movement is not better than what we now have**, and its design (every particle updated every frame, in a world 612 by 384) does not fit a world of millions of mostly still pixels. Two of its ideas are already in our code under other names. There are parameters and one technique worth copying.
- **Its heat and phase-change model is a good pattern to copy at a coarse scale**, not per pixel.
- **The licences allow reuse.** The Powder Toy is GPL-3.0; this project is AGPL-3.0, and GPL-3 code may be combined into an AGPL-3 work (section 13 of both). Files taken from it keep their copyright notices and stay GPL-3; the whole stays AGPL-3.

## What I read, and what I didn't

- **Read closely:** `src/simulation/Air.cpp` (all of it), and in `Simulation.cpp` the particle update's velocity, movement, liquid spreading and water-equalising code, `PlanMove`, `flood_water`, and the heat conduction block; the element definitions for water, oil, lava, sand and smoke; where fire and bombs add pressure.
- **Skimmed or not read:** the 200-odd other elements, Newtonian gravity, electronics, photons, the renderer, saves, the Lua API. None of those look relevant, but I can't vouch for them.

## How The Powder Toy works

**The world.** 612 by 384 pixels. Every non-empty pixel is a particle in one big array (position, velocity, temperature, type, a few spare fields), with a map from pixel to particle. There is no sleeping: every particle is visited every frame, on one thread. A "stagnant" flag only shortens how far a stuck liquid looks sideways. This is affordable at 235,000 cells and is the reason it cannot be scaled to our scenes directly (ours are around 4.5 million pixels).

**Air** (`Air.cpp`). Four grids at one cell per 4 by 4 pixels: pressure, velocity x, velocity y, and air temperature, plus a map of cells that block air (walls, and solid particles that insulate). Each frame:
1. Edges are pulled towards the ambient pressure and velocity; velocity next to walls is zeroed.
2. Pressure changes by the difference of velocity across each cell; velocity changes by the difference of pressure. That pair is what makes a pressure spike travel outwards as a wave and reflect off walls.
3. Everything is smoothed with a 3 by 3 kernel and carried along its own velocity (it samples "upwind", stepping cell by cell so it never reaches through a wall).
4. A little vorticity is added back so swirls don't smear away.
5. Hot air rises: velocity gets a push against gravity in proportion to how much warmer the cell is than ambient (the Boussinesq approximation).

**Particles and air talk both ways.** Each particle adds a fraction of its own velocity to its cell's air (`AirDrag`), damps the air it sits in (`AirLoss`), and takes a fraction of the air's velocity (`Advection`). Some add pressure all the time (`HotAir`: fire, smoke). Explosions are just a large pressure number written into a cell: burning explosives add 50 at once. That is the whole of its blast model, and it is why its explosions push things round corners and are stopped by walls.

**Movement.** A particle's velocity is gravity plus air plus, for gases, a random kick (`Diffusion`). `PlanMove` then walks along the velocity in steps of one pixel to find the last free pixel before an obstacle, so nothing tunnels. If the full move is blocked, it tries the move on one axis only, then the two diagonals.

**Liquids.** A liquid that still can't move looks sideways along its row, up to 30 pixels (10 if it was stuck last frame), for a pixel that isn't the same liquid; it moves there, then drops as far as 30 pixels down. Heavier particles swap with lighter ones (`Weight`).

**Water equalising** (`flood_water`, off by default in the game). One time in 200, a liquid particle flood-fills the body of liquid it is part of, looking for a free pixel lower than itself, and moves there. It allocates a bitmap the size of the world on every call.

**Heat.** Every particle has a temperature. One particle at a time averages its temperature with its eight neighbours, weighted by heat capacity, with a chance set by `HeatConduct`. The air's temperature grid exchanges heat with particles too. Each element names what it turns into above and below a temperature and a pressure (water to ice at 273 K and to steam at 373 K; lava to stone when it cools), so phase changes need no special code.

**Elements are data.** About 200 files, each a table of some 30 numbers and flags plus an optional update function.

## Against what we have

| Area | The Powder Toy | Ours now | Verdict |
|---|---|---|---|
| Air pressure and wind | Full grid simulation | None. Wind is one number; blasts push things by distance | **Take it** |
| Explosions | A pressure spike in the air | Separate rules for craters, splashes, thrown pieces, shockwave visuals | **Unify on pressure** once air exists |
| Smoke and gas | Particles carried by the air | A density grid that drifts and blocks sight | **Carry ours on the air field** |
| Liquid sideways flow | Looks 30 px along the row | Looks 720 px along the row, through liquid | Ours is the same idea, further |
| Pools coming level | Flood fill, 1 in 200 per particle, per frame | Budgeted search for a lower place through the body | Same idea; ours is bounded and cheaper |
| Resting liquid | Still visited every frame | Costs nothing | Ours fits a big world |
| Falling without tunnelling | Steps along the velocity | Liquid steps pixel by pixel already | Nothing to take |
| Liquids pushed by wind and blasts | Yes, through `Advection` | No | **Take the parameter** |
| Heat | Per particle, conducted | None. Fire is its own system; freezing is tied to the weather | **Copy the pattern, coarsely** |
| Phase changes | A table per element | Hand-written cases | **Copy the pattern** |
| Element properties | Data tables | Constants in `FluidSim.cpp` | **Copy the pattern** into `Materials.ini` |
| Rigid pieces of terrain | None | Ours | Nothing to take |

## What I recommend, in order

### 1. Port the air system (the big one)

**What it gives us**
- Blast waves that travel, bend round corners, funnel down corridors and stop at walls, from one number written at the blast.
- Smoke, dust, embers, rain and snow that swirl and get blown about by explosions, rotors and fire.
- Loose pieces of terrain, liquid spray, gibs and units pushed by the same pressure, replacing the by-distance push added today.
- Hot air rising from fires, drawing smoke up shafts.
- Real wind that is blocked by walls, which the weather shelter and wet-ground tests could read too.

**How**
- Take `Air.cpp` close to verbatim into a new `Source/Managers/AirSim`. Its arrays are fixed-size globals; they become vectors sized to the scene.
- The blocking map comes from the grid of solid ground the lighting already keeps at the same 4-pixel cell size.
- Everything that should feel the air reads the velocity at its position; everything that should push the air writes to it.

**Cost: the main risk.** Its grid is 153 by 96 cells. A scene of ours at the same cell size is about 750 by 375, nineteen times as many, and the update is several full passes with a 3 by 3 kernel. My estimate is 3 to 6 ms per update on one thread for a whole scene, which is too much. Ways to bring it under 1 ms, to be measured before committing to one:
- 8-pixel cells (a quarter of the cells);
- only simulate the cells where the air is disturbed, with a margin (still air stays still);
- run it every other update;
- several threads, since each pass reads one grid and writes another.

**Determinism.** It is plain float arithmetic in a fixed order on one grid, so it is deterministic on one build, like the rest of the simulation. With threads it stays so as long as each pass writes to a separate output grid, which is how it is written.

### 2. Carry smoke on the air

Our smoke grid keeps its sight-blocking and its look; its drift is replaced by the air's velocity. Small once the air exists.

### 3. Explosions and pushes through pressure

Each blast writes pressure in proportion to its energy. The thrown pieces, the liquid splash and the visual shockwave then read the air instead of each having its own rule. This removes code. The "how hard explosions throw loose pieces" slider becomes "how much pieces feel the air".

### 4. Material properties as data

Move the liquid and powder constants (how fast it falls and runs, weight, how much it feels the air) into `Materials.ini`, named after The Powder Toy's where they mean the same (`Advection`, `AirDrag`, `Weight`). Mods could then add liquids and powders. This is a refactor with no visible change by itself.

### 5. Coarse heat, with phase changes from a table

A temperature per air cell, not per pixel: fire and lava heat it, hot air rises and carries it, and materials name what they become when hot or cold (water to ice, ice and snow to water, water to steam, lava to stone). Freezing would then follow where it is actually cold instead of the weather type. Worth doing after the air is in and measured.

## What I would not take

- **Its particle model.** Every particle every frame cannot cover our scenes, and our terrain bitmaps, sleeping liquid and rigid pieces are built on a different footing.
- **Its liquid spreading and water equalising.** We have the same ideas with longer reach and bounded cost. If water still feels slow somewhere, that is tuning ours, not swapping it.
- **Per-pixel temperature.** Four bytes for each of 4.5 million pixels is affordable in memory, but conducting heat between them every frame is not, and nothing in the game needs that resolution.
- **Newtonian gravity, electronics, photons, Game of Life.** Not our game.

## A sensible first step

A spike, on a branch: port `Air.cpp`, drive it from explosions only, draw the pressure as a debug overlay, and measure the cost on a big scene with each of the four ways of cutting it. That answers the one open question, cost, in a day or two of work, before anything depends on it.
