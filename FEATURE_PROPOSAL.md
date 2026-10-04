# Cortex Command: Next Phase Proposal (features, visuals, physics)

The visual modernisation roadmap is done (see `MODERNISATION_PLAN.md`). This is the proposal for what came next. It was approved and is now mostly implemented; see **Status** at the end.

**Ground rules (unchanged):**
- Keep the pixel-art identity.
- Never break determinism, the 8-bit material bitmaps or palette semantics.
- Keep Lua and INI mods working.
- Anything that changes the simulation (not just how it looks) is marked **[sim]**. Those items need extra care for saved games and multiplayer.

Effort: S = a day or two, M = about a week, L = several weeks.

---

## 1. Physics and simulation

| # | Feature | What it adds | Effort | Notes |
|---|---|---|---|---|
| 1.1 | **Fire that spreads and burns terrain** [sim] | Wood, vegetation and fuel materials catch fire, spread, burn out to ash and cave in. Napalm and incendiaries become tactical. | M | Builds on the existing scorch, ember and hot-spot visuals. Runs as a sparse "burning cells" list on the material bitmap, so it stays deterministic. |
| 1.2 | **Collapsing terrain** [sim] | Terrain cut off from support breaks away and falls as debris, so tunnels and bunkers can be undermined. | M | Connected-component check only around craters, not the whole scene. Detached chunks become falling MOSRotating debris. |
| 1.3 | **Fluids: water, lava, acid** [sim] | New flowing materials (falling-sand style) settle, pool and flow through tunnels. Actors swim, lava ignites things, water puts out fire. | L | The biggest and most exciting item. Needs new material properties, a flow pass and buoyancy for MOs. |
| 1.4 | **Smoke and gas volumes** [sim, optional] | A coarse smoke/gas grid that drifts with the wind, blocks sight lines and AI vision, and allows toxic gas grenades. | M | The visual half (lit volumetric smoke, 2.1) can ship first without touching the sim. |
| 1.5 | **Wind** [sim, optional] | The weather wind also pushes light particles, smoke and (optionally) projectiles. | S | Visual-only by default; a gameplay toggle for the sim effect. |

## 2. Visual upgrades

| # | Feature | What it adds | Effort |
|---|---|---|---|
| 2.1 | **Lit volumetric smoke** | Smoke and dust write into a density buffer that the lights and radiance cascades scatter through. Muzzle flashes light up their own smoke, and fires glow through it. | M |
| 2.2 | **GPU effects particles** | Thousands of purely visual sparks, dust puffs, shell casings and debris with no simulation cost. Makes explosions and impacts much meatier. | M |
| 2.3 | **Water rendering** (with 1.3) | Refraction, surface highlights, light caustics, murk with depth. | M |
| 2.4 | **Metal and wet surfaces** | Per-material shininess: bunker walls and metal glint under lights; wet terrain in rain. | M |
| 2.5 | **Living world** | Grass and vegetation sway in the wind and flatten in blast waves; snow builds up on ledges during snowfall; puddles in rain. | M |
| 2.6 | **Blood and impact decals** | Blood and oil splatter stain terrain and fade over time (decal map like the scorch marks). Optional and gore-toggleable. | S |
| 2.7 | **Camera zoom** | Smooth zoom out for a tactical view, and in for drama. The engine already has camera zoom plumbing; the renderer and lighting need to support it. | L |

## 3. Gameplay and UX features

| # | Feature | What it adds | Effort |
|---|---|---|---|
| 3.1 | **Modern HUD option** | Minimap, crisp health and ammo bars, objective markers, damage numbers and a kill feed, using the new high-resolution text layer. The classic HUD stays the default. | M |
| 3.2 | **Night gameplay** [sim] | Darkness limits AI sight. Actors carry flashlights and headlamps that cast real light cones. Flares light up the battlefield. | M |
| 3.3 | **Photo mode** | Pause, free camera, effect sliders (time of day, weather, grading, depth of field), screenshots at any resolution. Builds on the F6 window. | S |
| 3.4 | **Scenario atmosphere picker** | Choose time of day, weather and day length when starting a scenario, saved per activity. | S |
| 3.5 | **Controller and Steam Deck polish** | Radial menus sized for gamepads, UI scale setting, Deck-friendly defaults. | M |
| 3.6 | **Mod tooling** | Lighting, atmosphere and fluid properties in the scene editor; Lua access to fluids and fire; a "lighting preview" toggle in editors. | M |

## 4. Tech and platform

| # | Item | Why | Effort |
|---|---|---|---|
| 4.1 | **SDL_GPU backend** (staged plan in `MODERNISATION_PLAN.md`) | Vulkan, Metal and DX12 reach, macOS support. | L |
| 4.2 | **Multithreaded sim helpers** | Parallelise the per-cell passes that 1.1–1.4 add (fire, fluids, gas), so the new physics stays fast on big scenes. | M |
| 4.3 | **Linux/meson build check and packaging** | Make sure everything new builds on Linux; produce a release zip with Settings defaults. | S |

---

## Recommended order

1. **Phase A: "Battlefield feels alive" (visual, low risk):** 2.2 GPU effects particles, 2.1 lit volumetric smoke, 2.6 decals, 2.5 living world, 3.3 photo mode. Big look-and-feel jump, no sim changes, about 4–5 weeks.
2. **Phase B: "Fire and ruin" (first sim features):** 1.1 spreading fire, 1.2 collapsing terrain, 1.5 wind. These pair with the effects from A and change how battles play. About 3–4 weeks, with the multiplayer/save checks.
3. **Phase C: "Water and night":** 1.3 fluids plus 2.3 water rendering, 3.2 night gameplay with flashlights. The headline new mechanics. About 6–8 weeks.
4. **Phase D: "Reach":** 3.1 modern HUD, 3.5 controller and Deck polish, 2.7 camera zoom, 4.1 SDL_GPU. Whenever wider distribution becomes the goal.

Each phase ends with golden images, soak tests and a playtest like this round.

---

## Status (implemented)

Everything below is built, committed and verified: golden images pass, and a soak test with every system on (stormy night battle) holds memory flat. On the stress scene, everything on gives about 210 FPS (271 before Phase A, 230 before the atlas).

**Phase A: battlefield feels alive (visual only)**
- **2.2 Effects particles:** sparks, dust and debris chips from explosions and fast terrain hits.
  - Render only, with their own random numbers.
  - Sparks are emissive streaks; debris is lit; dust is lit over the scene.
- **2.1 Light scattering in smoke:** fire, flashes and lamps glow through smoke.
- **2.6 Stains:** blood and oil stain terrain, in a world-space decal map.
- **2.5 Living world:**
  - Vegetation sways with the wind and bends in blasts.
  - Snow settles on exposed ground while it snows, and melts after.
  - Rain darkens exposed ground.
- **3.3 Photo mode (F8):**
  - Freeze, free camera, look sliders.
  - Screenshots at window resolution or 2–4x internal resolution.

**Phase B: fire and ruin (changes the simulation, deterministic)**
- **1.1 Spreading fire:**
  - Grass and vegetation burn away; wood, cloth and rubber burn to ash; oil burns fast.
  - Explosions and fire particles light it.
  - Flames hurt; burning areas give light, smoke and embers; water puts it out.
- **1.2 Collapsing terrain:** detached pieces fall as rigid chunks after explosions. Concrete and metal hold.
- **1.5 Wind:** drives particles, embers, dust, vegetation and precipitation. It stays visual and doesn't push projectiles.

**Phase C: water and night**
- **1.3 + 2.3 Flowing liquids:**
  - Water, lava and acid flow and pool.
  - Lava glows, ignites, hurts and turns to stone with water.
  - Acid eats soft terrain.
  - Shimmering, see-through water; glowing lava; bubbling acid.
  - Lua: `SceneMan:PourLiquid`.
- **3.2 Night gameplay:**
  - Headlamp cone lights with visible beams.
  - AI sight shrinks at night (to half, or 80% with a lamp).
  - New buyable Flare.

**Phase D: reach**
- **3.1 Modern HUD (optional):** minimap, health and ammo bars, unit-loss feed.
- **3.5 Scaling (partly done):** debug UI and HUD scale with the window, with a crisp TTF font. Gamepad navigation is on in debug windows. Pixel-art menus were already scaled by the window multiplier and the sharp upscale.

**Still open, with reasons**
- **4.1 SDL_GPU backend:** not started. It's a port of every OpenGL path and about 25 shaders, and brings no visual change on Windows. The staged plan is in `MODERNISATION_PLAN.md`.
- **4.3 Linux/meson build check:** new sources are in the meson files, but there is no Linux machine or WSL here to build and test on.
- **3.5 Controller and Steam Deck polish:** debug UI and HUD scale with the window and gamepad navigation works in debug windows, but radial menus and Deck defaults haven't been tuned on a Deck.
- **3.6 Scene editor panels:** a scene's time and weather can now be set from F6 and saved by the scene editor (see the completion round), but there are no dedicated editor panels for lighting or liquids.

**Follow-up round**
- **Saved games:** loading a saved game works again; it had been failing because saved terrain images weren't found as textures. Burning terrain and moving liquid are now saved too.
- **3.4 Scenario atmosphere picker:** the scenario setup has **Time** (scene default, dawn, noon, dusk, night) and **Weather** (scene default, clear, rain, snow) buttons.
- **1.4 Gas volumes:** thick smoke blocks sight for spotting and for the AI's aim checks. Adds the Smoke Grenade and Toxic Gas Grenade.
- **Liquid weapons:** Napalm Flamer (burning fuel that pools and burns), Water Cannon (knockback, puts fires out) and Acid Sprayer. Liquid drops now join the flow when they settle, and oil is a flowing liquid.
- **Weather that matters:** rain and snow damp fire, snow slows walkers, and wind drives fire and embers downwind.
- **Renderer fix:** a preset that copies another and sets its own `SpriteFile` showed the original's frames on the GPU path. Fixed.

**Completion round (what the two plans still had open)**
- **2.7 Camera zoom:** 0.4x to 2x with Ctrl + wheel (wheel alone in the sandbox). The whole view is drawn at the size of the area shown and scaled, so lighting, HUD and split screens all work.
- **2.4 Metal and wet-surface shine:** lights throw highlights on grey metal and concrete, rain-wet ground and water.
- **1.3 Swimming:** bodies pass through liquid, light units float, heavy ones sink, and flesh units drown. Water was reworked to flow and find its level.
- **4.2 Faster liquid pass:** done as a faster single-threaded pass rather than threads, which would have cost determinism. A 30,000-pixel flood went from 17 ms to under 5 ms per update.
- **Feel (plan phase 6):** hit-stop, recoil kick, frame cap, optional CRT scanlines, damage numbers and hit-direction arcs on the Modern HUD.
- **Modding (plan phase 7):** cone lights and visual particle emission from INI and Lua.
- **Weather (plan phase 5):** ash fall and dust storms.
- **3.6:** a scene's own time and weather can be set from F6 for the scene editor to save.

**Left out on purpose from the modernisation plan's wish list**
- **UI drawn fully on the GPU:** the HUD text overlay and Modern HUD cover the visible gain. Porting the GUI library is a large job with no change the player would see.
- **3D LUT grading and a choice of tonemappers:** the parametric grading (temperature, tint, contrast, split toning) covers the same ground.
- **Authored normal and emissive map files, truecolour sprites, mod shader hooks:** automatic edge normals and the emissive palette serve existing content; these only matter once someone is making new art for them.
- **Bullet-pock decals:** bullets already chip real holes in the terrain.
- **Golden images in CI:** hosted runners have no GPU, so the check runs locally.
