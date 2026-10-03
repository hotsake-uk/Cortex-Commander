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
- **What it gives off:** burning areas give light, smoke, embers and flames that hurt whoever stands in them.
- **Determinism:** it is part of the simulation and stays deterministic. It ticks in fixed sim steps with its own seeded random numbers, and ignitions are sorted before they're applied.
- **Saves:** fire isn't saved in saved games.

**Collapsing terrain** (`TerrainCollapse`, a gameplay setting, also in F6):
- **When it checks:** half a second and again a second and a half after a big explosion, it looks around the crater for pieces of terrain no longer touching anything.
- **What falls:** pieces up to 2500 pixels drop as rigid chunks, accelerating until they land with a puff of dust.
- **What stays up:** built structures (concrete, metal, military materials) and anything touching the edge of the world.
- **Determinism:** it is part of the simulation and deterministic, with sorted checks and no random numbers.

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
| `WeatherType` | 0 | 0 clear, 1 rain, 2 snow. Precipitation doesn't fall under overhangs or in caves. |
| `WeatherIntensity` / `Wind` | 0.6 / 60 | How heavy the rain or snow is, and its horizontal speed in px/s. |
| `LightingGlowIntensity` / `LightingGlowRadiusScale` | 2.5 / 8 | Brightness and reach of the light that glow effects cast. |
| `LightingShadowStrength` | 0.85 | How much terrain blocks dynamic lights. |
| `LightingEmissiveIntensity` | 1.4 | Brightness of glow sprites. |
| `LightingEdgeLighting` | 1 | Strength of the automatic edge normals. |
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
	WeatherType = 2         // 0 clear, 1 rain, 2 snow
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
	LightOffset = Vector   // Offset from the object's position, rotated and flipped with it.
		X = 10
		Y = -2
```

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
shield.RenderBlendMode = 1   -- 0 normal, 1 additive, 2 screen
shield.RenderOpacity = 0.5
print(actor.LightRed, actor.LightGreen, actor.LightBlue)

-- A light for this frame only. Call every update to keep it on:
PostProcessMan:AddLight(position, radius, r, g, b, intensity)

-- Atmosphere for scripted scenes:
PostProcessMan.TimeOfDay = 6.8          -- dawn
PostProcessMan.DayLengthMinutes = 20    -- 0 to freeze the time
PostProcessMan.WeatherType = 1          -- 0 clear, 1 rain, 2 snow
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

The lighting shaders live in `Data/Base.rte/Shaders/Lighting/`. The sprite shader `Base.rte/Shaders/Blit8.frag` and the terrain shader `Base.rte/Shaders/Terrain.frag` write a second output: a screen-space normal in RGB, and in alpha 0 for "nothing drawn" or 0.5–1 for "drawn, with emissive strength 0–1". Custom shaders used for scene drawing should write it too.
