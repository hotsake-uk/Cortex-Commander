# Cortex Commander: Visuals and Performance Plan

The earlier plans are done (`MODERNISATION_PLAN.md`, `FEATURE_PROPOSAL.md`). This one covers the next round: shadows, materials that look like what they are, better effects, and making the engine cope with bigger fights.

**Ground rules (unchanged):**
- Keep the pixel-art identity. Every look change gets a strength setting, and the Classic preset stays the original look.
- Never break determinism, the 8-bit material bitmaps or palette semantics.
- Keep Lua and INI mods working.
- Measure before and after. Performance claims come from the performance log (see below), not from guesses.

## Where we started (measured 2026-10-04)

Final build, 960x540 internal resolution, VSync off, the user's own settings (High preset).

| Situation | Result |
|---|---|
| Constant bombardment of a bunker, about a dozen units | 340 to 430 fps, worst frame 8 ms |
| 500 units spawned (about 330 alive at once) | about 10 fps, 88 ms per frame |

Where the 88 ms of the big battle goes:

| Part | Time |
|---|---|
| Moving and updating particles (15 to 25 thousand of them) | 28 ms |
| Handing objects to the GPU one at a time | 16 ms |
| Units: movement and update | 14 ms |
| A leftover software drawing of every object | 8 ms |
| Lighting | 1 ms |

- Half of the particles in that battle are single pixels of terrain knocked loose (4,900 of 9,300 at the 30 second mark).
- Lighting costs under 1 ms a frame on the development machine, and 2.7 ms at worst with 280 lights on screen (test build, with GPU waits).
- **Already fixed:** the check for floating terrain after each blast took 20 to 38 ms in one sim update, a hitch after every explosion. It now takes about 1 ms.

**How to measure:** set the `CCCP_PERF_LOG` environment variable to a file name and run the `Blast` or `SandboxStress` test scenario. `Tools/RenderTest/README.md` has the details.

## The list

Status: **done**, **in progress**, **next**, or **later** (not asked for yet).

### Lighting

| # | Item | What you see | Status |
|---|---|---|---|
| 1 | **Unit shadows** | Soldiers, crates, doors and wreckage cast shadows from muzzle flashes, flares, lamps and explosions. Before, only terrain blocked light. | done |
| 2 | **Sun and moon shadows** | Daylight gets a direction. Hills, bunkers and units cast shadows that turn with the time of day; sunlight falls through openings onto bunker walls. | done |
| 3 | **Contact shading** | A soft dark edge where things stand against walls and in corners, so they look anchored instead of pasted on. | done |
| 4 | Tighter light shadows | Lights sample the terrain coarsely, so thin walls can let light through. A distance map fixes that and costs less per light. | later |
| 5 | Water reflections | The scene mirrored in rippling water, with shimmer on pool floors. | later |
| 6 | Sky | A visible sun, and drifting clouds that shade the ground. | later |

### Materials

| # | Item | What you see | Status |
|---|---|---|---|
| 7 | **Material per object** | Every object already has a physical material (metal, flesh, cloth). It drives the look too: a robot is metal all over, fatigues stay matte. Today shine is guessed from grey palette colours. | next |
| 8 | **Surface detail from the art** | A relief worked out from each sprite's own pixels, so plates, rivets and folds catch light. Today only the outline does. | next |
| 9 | **Metal shading** | Metal reflects the sky from above and the ground from below, with tinted highlights and a bright rim. This is what makes a grey robot read as steel. | next |
| 10 | **Terrain by real material** | Steel plating, gold, ice and mud shaded from the material map, not guessed from colour. | next |
| 11 | Surface states | Wet after wading or rain, sooty near blasts, snow settling, barrels and struck metal glowing hot. | later |

### Other visuals

| # | Item | What you see | Status |
|---|---|---|---|
| 12 | **Better explosions** | Fireballs that roll into smoke, smoke columns lit from within, dust rings along the ground. | next |
| 13 | **Energy effects** | Shimmer for shields and cloaks, tracers that light what they pass. | next |
| 14 | **Depth** | Slight blur and haze on far backgrounds. | next |
| 15 | **Look presets** | Gritty, Vivid, Noir. | next |

### Performance

| # | Item | What it does | Status |
|---|---|---|---|
| 16 | **Batch particle drawing, skip off-screen ones** | Each pixel particle was sent to the GPU separately, visible or not. | done |
| 17 | **Drop the leftover software drawing** | Every object was still drawn in software each update, into a layer the GPU renderer doesn't use. | done |
| 18 | Debris budget | Past a limit, loose terrain pixels become cheap visual-only chips. Changes how rubble piles up in very large fights. | later |
| 19 | Merge and cap lights | The big battle peaked at 2,863 lights on screen. | later |
| 20 | Refresh the light grid only where terrain changed | Saves a little every frame, and digging lights up at once. | later |

## Order of work

Agreed with the user on 2026-10-04:

1. Shadows: items 1, 2 and 3.
2. Materials: items 7, 8, 9 and 10, with the two invisible performance items 16 and 17.
3. Other visuals: items 12, 13, 14 and 15.

## How each piece is meant to work

Short design notes, updated as things are built.

### Shadows (1 to 3)

- **A mask of solid objects.** The scene's draw pass marks which pixels belong to solid objects (units, items, doors, wreckage). Particles, smoke and glows are left out, so a puff of smoke or a bullet never throws a hard shadow.
- **A distance map from the mask.** Each frame, every pixel learns how far away the nearest solid object is. Rays can then cross empty space in a few big steps and still hit thin things like rifles.
- **Unit shadows from lights (1).** Each light's rays are traced through the distance map as well as through the terrain grid they already use. The skin of the object the light sits on is skipped, so a headlamp or muzzle flash isn't blocked by its own carrier.
- **Sun and moon shadows (2).** The grid that already spreads sky light through the terrain carries a second value: whether the sun (or the moon at night) can be seen from each cell. It spreads the same way, along the sun's direction, so it costs almost nothing and softens with distance. Units add their own crisp shadows through the distance map. Under open sky in full sun the picture is exactly as before; shade is darker and a little cooler. Shadows fade out under heavy weather and around sunrise and sunset, when the sun and moon swap.
  - Solid ground keeps what reached its surface, so the band under the surface of a hill's far side or an overhang is shaded too.
  - Where the sun reaches inside a cave or bunker it lights the walls, and the light shafts now come from the same data, so a beam stops where a roof cuts it off.
- **Contact shading (3).** Background walls darken slightly close to solid objects and close to terrain.
- **Found on the way:** the terrain's background layer lost its depth whenever a scene was copied from its preset, so it was drawn at the foreground's depth and the lighting couldn't tell walls from solid ground. Light shafts were never shown over walls because of it. Fixed; walls are now treated as walls everywhere.
- **Measured cost** (test build, 960x540, GPU waits on): the distance map 0.2 ms a frame; lights 0.16 to 0.18 ms with shadows, 0.12 ms before.

### Materials (7 to 10)

- **Material per object (7).** Each draw carries its object's surface values (how metallic, how glossy), taken from its physical material. `Material` presets get optional keys so mods can set them.
- **Surface detail (8).** The sprite shader works out a relief from brightness differences between neighbouring pixels and adds it to the outline bevel that exists already.
- **Metal shading (9).** Metallic pixels reflect a simple sky-over-ground environment according to their relief, get a bright rim, and take sharper highlights from lights.
- **Terrain by material (10).** The terrain shader reads the material layer, so its surface values come from the real material of each pixel.

### Performance (16, 17)

- **16:** particles off screen are skipped. Pixel particles and their trails that are drawn one after another share a draw call, each pixel still at its own place in the draw order.
- **17:** the software drawing is gone from the update. Only world dumps look at that layer, and they now draw it when one is taken.
  - **Correction:** this plan first said glow effects were registered inside that drawing and had to be moved out. That was wrong: they are registered in the objects' updates, and none of the software draw methods has a side effect, so the drawing could simply go.
- **Measured** (Final build, the 500 unit battle, at about 15,000 particles): a frame took 88.8 ms before and 78.3 ms after. The software drawing was 7.9 ms of that; handing objects to the GPU went from 16.1 to 13.5 ms and rendering the batch from 6.6 to 5.1 ms, with half as many draw calls. The battle in that test is all on screen, so skipping off-screen particles doesn't show in these numbers.
- **What's left:** the rest of the time is the physics of the particles themselves (item 18).

## Progress log

- **2026-10-04:** performance log added; terrain collapse hitch fixed; this plan written.
- **2026-10-04:** shadows (items 1 to 3) built, with a regression scene (`GoldenShadows`) and test scenarios (`Shadows*`). Background wall depth fixed. Light shafts now follow the sun.
- **2026-10-04:** a crash in the Modern HUD fixed (it read the controlled unit a frame after the unit was deleted).
- **2026-10-04:** performance items 16 and 17 done.
