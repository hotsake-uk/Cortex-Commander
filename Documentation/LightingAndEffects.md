# Lighting and Visual Effects

This branch adds scene lighting and post-processing on top of the GPU renderer. Everything works automatically with existing content. These are the knobs for players, modders and activity scripters.

## How it works, briefly

Each player screen is drawn as before. Before the HUD goes on top, it is then:

1. **Lit.** Light comes from four sources:
   - **Sky light.** It spreads through a low-resolution copy of the terrain, so caves get darker the deeper they go, and digging lets light in.
   - **Ambient light.** It applies where no sky light reaches.
   - **Glow lights.** Every glow effect (`ScreenEffect`) casts light in its own colour, with soft shadows from terrain.
   - **Lights you add.** These come from INI or Lua (see below).

   All of it is computed in HDR. At full daylight the original art is reproduced unchanged.
2. **Edge shading.** Sprites and terrain get automatic edge normals from their outlines, so edges facing a light catch it.
3. **Emissive pixels.** Glows, the palette's glow yellows (gold sparkle, tracers) and freshly blasted terrain shine regardless of the surrounding light.
4. **Post-processing**, in this order:
   - Heat haze and explosion shockwaves
   - God rays into caves
   - Rain or snow
   - Bloom
   - Highlight compression, colour grading and vignette

## Settings (Settings.ini)

All colours are linear `R G B` (1 = neutral). Most of these can be tuned live in the **Graphics Lab**. To open it, set `ShowGraphicsLab = 1`, use *Debug Options → Show Graphics Lab*, or call `DebugMan:ShowGraphicsLab()` from Lua. It has a *Save to Settings.ini* button. The in-game *Video* settings have toggles for Lighting, Bloom and Extra Effects (heat haze, shockwaves, scorch marks, embers). Turning all three off gives the classic look.

Press **F6** in game or in the menus for the **World Debug** window: time of day (with Midnight/Dawn/Noon/Dusk/Night presets and an option to let time pass), weather, the lighting toggles, interior/cave light, the playfield light floor, sky light, exposure, god rays, haze, debug views and game speed. It has buttons for the Graphics Lab, performance stats and saving. While any debug window is open the mouse is released from aiming so the window can be used, and clicks on a window don't reach the game.

**Effects particles** (`EffectsParticles`, a multiplier where 0 turns them off) add sparks, dust and debris chips from explosions and from fast hits on terrain. Hard materials (integrity 100 or more) throw sparks; soft ones throw dust. They are purely visual: the simulation spawns them, but they move in simulation time with their own random numbers and only read the terrain. Sparks glow (emissive), chips are lit in the scene, and dust is lit over the scene. They're part of Extra Effects and the quality presets.

**Smoke scattering** (`SmokeScattering`, where 0 turns it off): smoke particles, meaning Air-material MOSParticles with negative gravity, are splatted into a half-resolution density buffer. The light passing through them, from dynamic lights and radiance cascades, is added where the smoke is, so fire, muzzle flashes and lamps glow through smoke. The smoke sprites themselves are unchanged.

**Living world** (`LivingWorld`):
- Vegetation in the terrain (green-dominant palette colours) sways with the wind and gusts, more towards the tips, and is pushed outwards as blast waves pass.
- Snow builds up a few pixels deep on ground under open sky while it snows, over about a minute, and melts afterwards.
- Rain darkens exposed surfaces and they dry slowly.

All of it happens in the terrain shader; the terrain bitmaps are never changed.

**Radiance cascades GI** (`RadianceCascades = 1`, which the Ultra preset turns on) is 2D global illumination at half resolution. Glows such as fire, explosions and lamps light their surroundings with soft occlusion from terrain and objects. Lit surfaces pass on part of their light (`GIBounce`), building up to several bounces over a few frames. It replaces the screen-space indirect light, costs about 0.3 ms, and `GIStrength` scales it. The Graphics Lab has a "GI only" view.

At night, stars and a moon appear on the sky layers. These are the background layers with little or no parallax, and both fade out toward the horizon. Heavy rain (intensity above 0.5) brings occasional lightning that briefly lights the sky. Auto exposure (`AutoExposure`, `AutoExposureLow`, `AutoExposureHigh`) only kicks in when the average scene brightness leaves the normal range, such as a screen-filling flash or near-total darkness.

**Spreading fire** (`TerrainFire` in the gameplay settings, also in F6):
- **What lights it:** explosions and fire, flame and napalm particles light flammable terrain.
- **How it burns:** grass and vegetation flare up and burn away; wood, cloth and rubber burn slowly from the surface in, leaving ash; oil burns fast.
- **How it spreads:** fire needs air, so only exposed pixels catch, and it climbs upward more readily than sideways.
- **Weather:** rain (and less so snow) makes it spread less and burn out sooner. Wind makes it spread downwind, and in dry weather carries embers a few pixels downwind to start new fires.
- **Water** splashing on it puts it out.
- **What it gives off:** burning areas give light, smoke, embers and flames that hurt whoever stands in them.
- **Determinism:** it is part of the simulation and stays deterministic. It ticks in fixed sim steps with its own seeded random numbers, and ignitions are sorted before they're applied.
- **Saves:** burning terrain is saved in saved games and keeps burning when loaded.

**Collapsing terrain** (`TerrainCollapse`, a gameplay setting, also in F6):
- **When it checks:** half a second and again a second and a half after a big explosion, it looks around the crater for pieces of terrain no longer touching anything.
- **What falls:** pieces up to 2500 pixels drop as rigid chunks, accelerating until they land with a puff of dust.
- **What stays up:** built structures (concrete, metal, military materials) and anything touching the edge of the world.
- **Determinism:** it is part of the simulation and deterministic, with sorted checks and no random numbers.

**Flowing liquids** (`FlowingLiquids`, a gameplay setting, also in F6):
- **What flows:** Water (material 160), Lava (165), Acid (167) and Oil in the terrain fall, run sideways and pool.
- **Drops join in:** a liquid particle that settles into the terrain flows too, if it's drawn in its liquid's own colour (so blood, which uses the Water material in red, stays put). A burning particle that settles as something flammable, such as napalm fuel, sets it alight.
- **Water** puts out fire. It is drawn see-through and shimmering, with a bright surface line.
- **Lava** flows slowly, glows, sets flammable terrain alight, hurts whatever touches its surface, and sets to stone in a puff of steam where it meets water.
- **Acid** slowly eats soft terrain and is used up doing it.
- **Oil** flows more slowly and burns where it pools. It isn't drawn shimmering, because its colour is shared with many sprites.
- **Saves:** moving liquid is saved in saved games.
- **Cost:** liquid at rest costs nothing. Only pixels that moved recently, or were disturbed by explosions or collapses, are simulated.
- **Determinism:** it is part of the simulation and deterministic.
- **Lua:** `SceneMan:PourLiquid(Vector, radius, "Water"|"Lava"|"Acid"|"Oil")`, `SceneMan:GetFlowingLiquidPixelCount()`, `SceneMan:GetBurningPixelCount()`.
- **Liquid weapons:** the **Napalm Flamer** sprays burning fuel that pools and burns, the **Water Cannon** knocks people back and puts fires out, and the **Acid Sprayer** lobs globs that sting and eat soft ground.

**Swimming and drowning** (`SwimmingAndDrowning`, a gameplay setting, also in F6):
- Bodies (units, limbs, weapons, wreckage) move through liquid instead of hitting and destroying it, so pools and floods stay put when units walk in. Single particles, such as drops and bullets, still strike the surface.
- **Wading** slows walking, more the deeper the unit is.
- **Buoyancy:** light units float (a lightly armed soldier, about 145 kg all in, just floats; robots like the Dummy bob high), and heavy armour and big guns sink.
- **Air:** flesh and blood units hold their breath for 12 seconds with their heads under, then lose health until they surface. Robots and drones don't breathe. The Modern HUD shows an air bar for the unit you control.
- **Acid** hurts anything standing in it. Lava sets units alight (see Burning units).
- Deterministic, with no random numbers.

**Smoke and gas** (`SmokeBlocksSight`, a gameplay setting, also in F6):
- Thick smoke blocks sight: units can't spot enemies through it, and the AI loses track of targets hidden by it. Thin wisps don't.
- It's worked out each sim update from the smoke particles on a coarse grid, so it is deterministic. Lua: `SceneMan:SmokeBlocksSight(from, to)`.
- **Smoke Grenade:** a thick screen lasting about ten seconds. The AI doesn't throw it.
- **Toxic Gas Grenade:** a green cloud that hurts anyone inside it for about ten seconds, and blocks sight too.

**Burning units** (`BurningUnits`, a gameplay setting, also in F6):
- Flames, napalm, burning ground and lava set units alight. A burning unit takes damage, panics and runs, and sets grass and anyone it bumps into alight.
- It burns out after a few seconds. Water puts it out sooner (a pool, the Water Cannon or heavy rain).
- Water putting out fire, and lava meeting water, throws up **steam** that rises and blocks sight like smoke. Only real water drops douse fire: blood is made of water too, but doesn't.
- **Fuel Barrel:** leaks oil that flows where it's shot, and explodes in burning fuel when fire reaches it or it's shot to pieces. It's in the build menu and the item list. Lua: `SceneMan:IsBurningNear(pos, radius)`, `SceneMan:GetBurningUnitCount()`.

**Weather** also changes gameplay: rain and snow damp fire (see above), snow slows walking by up to 15%, and wind drives fire downwind. The weather stays fixed through a game. It comes from the scene, the scenario setup's **Time** and **Weather** buttons, or the player's settings.

**Night gameplay**:
- **Headlamps** (`Headlamps`): after dark, soldiers wear headlamps that throw a cone of light where they aim, with a faint visible beam.
- **AI sight** (`NightAffectsAI`): AI sees only about half as far at night without a headlamp, or 80% with one. This changes gameplay.
- **Flare:** a buyable grenade that burns red for about 45 seconds.
- **Cone lights:** available to code via `PostProcessMan::RegisterConeLight`.
- **Lights near terrain:** a light no longer shadows itself on the terrain right around it, so a lamp lying in grass still lights up its surroundings.

**Sandbox** (game mode and F7 tools):
- **The game mode:** pick **Sandbox** in the scenario menu. You play as a god with a free camera (right-drag or WASD), every unit is run by the AI, including your own side's, and the game never ends.
- **Spawning:** units from every faction (with squad size, loadout and orders), brains and items. Each spawn list has a search box. The default loadout uses each faction's guns and plain grenades.
- **Building:** "Build bunkers with the build menu" opens the game's own build menu in the middle of play. It places straight into the world, and the money never runs out. Choose Done in its pie menu, or press F7, to go back. There are four sides, in the game's own team colours: Red, Green, Blue and Yellow.
- **Brains are optional:** place one for any side to give the others something to hunt and that side something to defend.
- **Orders:**
  - Hold position, attack nearest enemy, hunt brains, patrol, go to the side's rally point, or do nothing.
  - Given when units spawn, or to a whole side from the Orders tab. "Everyone attack!" starts a free-for-all.
  - Units told to attack pick a new target when theirs dies.
- **Pause AI** (top of the sandbox window, also in F6): every AI-run unit stands still and holds fire, so you can set up armies, bunkers and traps, then untick it to let them loose. Physics, fire and liquids carry on. Craft keep flying, units you control still move, and a banner shows while it's paused. A new game starts with the AI running. Lua: `SandboxPauseAI(true/false)`.
- **Drop squad:** a squad arrives by dropship or rocket over the point you click, then the craft flies off.
- **Command and Follow:** with Command, drag a box to select units, then click the ground to send them there or an enemy to attack it. Follow locks the camera onto a unit; "Follow the action" (World tab) keeps the camera on the closest fighting. Moving the camera yourself stops following.
- **Auto battle (Orders tab):** give sides a faction and a budget. Each buys waves of its faction's units and drops them in to attack until one side is left, and the winner is announced. Lua: `SandboxAutoBattleSide(side, faction, budget)`, `SandboxStartAutoBattle()`.
- **Take control:** click a unit to play it yourself. F7 (or the unit dying) puts you back in the god view.
- **Paint and Boom:** fire, water, lava, acid, oil, smoke and toxic gas; dig, or add earth, sand, grass, wood or concrete; grenade blasts, big bombs, napalm and lightning strikes.
- **World tab:** weather, wind, time of day, slow motion, a free camera toggle, and putting out all fire.
- **In other games:** F7 opens the same tools as a debug panel.
- **Lua:**
  - `SandboxDo(tool, position, side, order, count, presetName)` uses any tool from a script. For example, `SandboxDo("Units", pos, 1, 1, 4, "Soldier Light")` spawns four Red soldiers told to attack.
  - `SandboxCountUnits(side)` counts a side's units, and `SandboxBuildMode(true/false)` opens or closes the build menu.

**Modern HUD** (`ModernHUD`, in Video settings and F6) is drawn crisply at window resolution on top of the classic HUD. It is off by default. It adds:
- A minimap of the terrain, your view and every unit, coloured by team.
- Health and ammo bars for the unit you control.
- A feed of units lost.

**Feel:**
- **Hit-stop** (`HitStopStrength`, 1 by default, 0 for off, also in F6): a big blast in view holds the game for 25 to 70 milliseconds and smears the lens for an instant. It happens at most every 0.6 seconds, so chains of explosions don't stutter. Only the timing changes, never what the simulation does.
- **Recoil kick:** the view kicks back with the gun when the unit you control fires, and springs back. It follows the Screen Shake Strength setting.
- **Frame cap** (`FrameCap`, 0 for none, also in F6): limits frames per second (30 to 1000), to save power and heat when VSync is off.
- **CRT scanlines** (`PostScanlines`, 0 to 1, in the Graphics Lab): optional scanlines on the final image.
- **Modern HUD extras:** numbers float up from units as they lose health (slow damage like fire is gathered up), and a red arc shows which way a hit on your unit came from.

**Camera zoom:**
- **Ctrl + mouse wheel** zooms the view out (down to 0.4x, about six times as much world on screen) or in (up to 2x). In the Sandbox god view the wheel alone zooms. There are sliders in F6 and the sandbox's World tab.
- The whole view (scene, lighting and HUD) is drawn at the size of the area it shows and scaled to the screen, so every effect works at any zoom. Far backgrounds don't zoom: the sky and backdrop stay put.
- HUD text stays readable when zoomed out (with Smooth HUD Text on); HUD icons shrink with the view.
- The view eases back to 1x while the buy menu or the build phase is open, because those menus are laid out for the normal screen. Menus and editors don't zoom. A new game starts at 1x.
- Split screens zoom together. Zooming far out costs GPU time in proportion to the area shown.
- Lua: `FrameMan.CameraZoom` (read and write). `FrameMan.PlayerScreenWidth` and `Height` give the size of the area in view, so they change with the zoom.

Press **F8** for **Photo Mode**:
- Freezes time and hides the HUD and screen text.
- Free camera: drag with the right mouse button, or use the arrow keys.
- Look sliders: time of day, weather, exposure, grading, bloom, haze, god rays, film grain, chromatic aberration.
- Screenshots go to the ScreenShots folder, as shown in the window or at 2x, 3x or 4x the internal resolution.
- Look changes are undone when you close it, unless "Keep look changes" is ticked.
- F6 and F8 work even while time is frozen.

`LightingSettingsVersion` records which defaults a settings file was written with. When the lighting defaults change a lot (version 2 made interiors and caves much brighter), saved values from older files are ignored so players get the new look.

| Key | Default | What it does |
|---|---|---|
| `LightingEnabled` | 1 | Scene lighting on or off. |
| `LightingAmbient` | 0.13 0.13 0.16 | Light where no sky light reaches (cave backgrounds). |
| `LightingSkyColor` | 1 0.98 0.95 | Light under open sky at noon. |
| `LightingForegroundAmbient` | 0.4 0.39 0.42 | Minimum light on diggable terrain and objects, so the playfield stays readable underground. Dims at night. |
| `LightingAirFalloff` / `LightingSolidFalloff` | 0.96 / 0.6 | How far sky light reaches through air and into terrain. |
| `TimeOfDay` | 12 | Hours (0–24). Noon is the classic look; dawn, dusk and night tint the sky, haze and light. |
| `DayLengthMinutes` | 0 | Length of a full day/night cycle in minutes. 0 keeps the time fixed. |
| `AtmosphereHaze` / `AtmosphereColor` | 0.18 / 0.62 0.74 0.95 | Distant background layers fade into the atmosphere, based on their parallax. |
| `GodRays` / `GodRayDecay` | 0.7 / 0.965 | Light shafts from the sky through gaps in the terrain. |
| `WeatherType` | 0 | 0 clear, 1 rain, 2 snow, 3 ash fall (grey flakes and a grey haze), 4 dust storm (dust blown level, a tan haze, and units see up to half as far). Precipitation doesn't fall under overhangs or in caves. |
| `WeatherIntensity` / `Wind` | 0.6 / 60 | How heavy the rain or snow is, and its horizontal speed in px/s. |
| `LightingGlowIntensity` / `LightingGlowRadiusScale` | 2.5 / 8 | Brightness and reach of the light that glow effects cast. |
| `LightingShadowStrength` | 0.85 | How much terrain blocks dynamic lights. |
| `LightingEmissiveIntensity` | 1.4 | Brightness of glow sprites. |
| `LightingEdgeLighting` | 1 | Strength of the automatic edge normals. |
| `LightingSpecular` | 1 | Highlights that lights throw on shiny surfaces: grey metal and concrete, rain-wet ground, water and acid. 0 turns them off. |
| `LightingIndirect` | 0.35 | One bounce of light: lit surfaces bleed their colour onto their surroundings, mostly in shadowed areas and caves. |
| `Embers` | 1 | Embers rising from fire and other warm glows. |
| `DistortionEnabled` / `HeatHaze` / `ShockwaveStrength` | 1 / 1.5 / 1 | Heat haze above hot things, and refraction rings from explosions. |
| `ScorchMarks` | 1 | Explosions leave soot on terrain, and the crater rim glows while it cools. |
| `BloomEnabled` / `BloomThreshold` / `BloomIntensity` | 1 / 0.9 / 0.5 | Bloom. |
| `PostExposure` / `PostSaturation` / `PostVignette` | 1 / 1.05 / 0.15 | Final image. |
| `GradeTemperature` / `GradeTint` / `GradeContrast` | 0 / 0 / 1 | White balance and contrast. |
| `GradeShadowTint` / `GradeHighlightTint` | 1 1 1 / 1 1 1 | Split toning. |
| `FilmGrain` / `ChromaticAberration` | 0 / 0 | Film grain (0–1) and lens fringing (pixels). |

## Scene atmosphere (INI)

A Scene can set its own time of day and weather. These override the player's settings while the Scene is loaded, and every Scene load restores the player's own atmosphere first.

```ini
AddScene = Scene
	PresetName = My Frozen Valley
	TimeOfDay = 17.5        // Hours. Dusk.
	DayLengthMinutes = 0    // 0 freezes the time.
	WeatherType = 2         // 0 clear, 1 rain, 2 snow, 3 ash fall, 4 dust storm
	WeatherIntensity = 0.7
	Wind = -90
	...
```

## Lights on objects (INI)

Any `MovableObject` (actors, devices, particles, gibs…) can cast a light. The light moves, rotates and flips with the object, and terrain casts soft shadows from it.

```ini
AddDevice = HDFirearm
	PresetName = Flashlight Rifle
	...
	LightColor = Color
		R = 255
		G = 235
		B = 200
	LightRadius = 180      // Pixels, where the light reaches zero. 0 = no light.
	LightIntensity = 1.2   // 0 = no light.
	LightFlicker = 0.1     // 0..1 random flicker, nice for fire and torches.
	LightConeAngle = 25    // Above 0, the light is a beam (a flashlight) with this half-angle in degrees. 0 shines all round.
	LightConeDirection = 0 // Which way the beam points, in degrees clockwise from the way the object faces.
	LightOffset = Vector   // Offset from the object's position, rotated and flipped with it.
		X = 10
		Y = -2
```

Any `MovableObject` can give off purely visual particles as it goes (they never affect the simulation, and thousands cost next to nothing):

```ini
	VisualEmission = Sparks        // Sparks, Dust, Debris or Embers
	VisualEmissionRate = 40        // Per second.
	VisualEmissionSpread = 0.6     // 0 (straight along the object's velocity) to 1 (every direction).
```

From Lua: `object:SetVisualEmission("Embers", 20, 0.5)`, or a one-off burst anywhere with `EmitVisualParticles(kind, position, velocity, spread, count, 0xRRGGBB)` (0 for the kind's own colour).

Any `MovableObject` can also be drawn translucent or glowing:

```ini
	RenderBlendMode = Additive   // Normal (default), Additive (energy, plasma, fire), or Screen (soft light)
	RenderOpacity = 0.6          // 0..1
```

Glow effects (`ScreenEffect`) already cast light automatically, in the average colour of their glow image. You don't need to add lights for muzzle flashes, explosions, thrusters or fire.

## Lua

```lua
-- Lights on any MovableObject:
actor.LightRadius = 140
actor.LightIntensity = 0.9
actor:SetLightColor(255, 230, 190)   -- 0-255
actor.LightOffset = Vector(0, -12)
actor.LightFlicker = 0.0
actor.LightConeAngle = 25      -- a beam; 0 for all round
actor.LightConeDirection = 0
shield.RenderBlendMode = 1   -- 0 normal, 1 additive, 2 screen
shield.RenderOpacity = 0.5
print(actor.LightRed, actor.LightGreen, actor.LightBlue)

-- A light for this frame only. Call every update to keep it on:
PostProcessMan:AddLight(position, radius, r, g, b, intensity)

-- Atmosphere for scripted scenes:
PostProcessMan.TimeOfDay = 6.8          -- dawn
PostProcessMan.DayLengthMinutes = 20    -- 0 to freeze the time
PostProcessMan.WeatherType = 1          -- 0 clear, 1 rain, 2 snow, 3 ash fall, 4 dust storm
PostProcessMan.WeatherIntensity = 0.9
PostProcessMan.Wind = -120
PostProcessMan.LightingEnabled = true
PostProcessMan:SetSkyColor(255, 250, 240)     -- 0-255, gamma space
PostProcessMan:SetAmbientColor(90, 90, 110)
PostProcessMan:SetColorGrade(-0.4, 0.0, 1.1)  -- temperature, tint, contrast
PostProcessMan:SetSplitToning(0.85, 0.95, 1.15, 1.1, 1.0, 0.9)  -- shadow rgb, highlight rgb
```

Atmosphere changed from Lua (time, weather, sky and ambient colours, grade) lasts until the next Scene loads. It is never saved over the player's own settings.

## Shaders

The lighting shaders live in `Data/Base.rte/Shaders/Lighting/`. The sprite shader `Base.rte/Shaders/Blit8.frag` and the terrain shader `Base.rte/Shaders/Terrain.frag` write a second output: the screen-space normal's x and y in RG, 1 minus the surface's shininess in B, and in alpha 0 for "nothing drawn" or 0.5–1 for "drawn, with emissive strength 0–1". Custom shaders used for scene drawing should write it too.
