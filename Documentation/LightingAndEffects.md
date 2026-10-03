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

All colours are linear `R G B` (1 = neutral). Most of these can be tuned live in the **Graphics Lab**. To open it, set `ShowGraphicsLab = 1`, use *Debug Options → Show Graphics Lab*, or call `DebugMan:ShowGraphicsLab()` from Lua. It has a *Save to Settings.ini* button. The in-game *Video* settings have toggles for Lighting, Bloom and Heat/Shockwaves. Turning all three off gives the classic look.

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
| `DistortionEnabled` / `HeatHaze` / `ShockwaveStrength` | 1 / 1.5 / 1 | Heat haze above hot things, and refraction rings from explosions. |
| `ScorchMarks` | 1 | Explosions leave soot on terrain, and the crater rim glows while it cools. |
| `BloomEnabled` / `BloomThreshold` / `BloomIntensity` | 1 / 0.9 / 0.5 | Bloom. |
| `PostExposure` / `PostSaturation` / `PostVignette` | 1 / 1.05 / 0.15 | Final image. |
| `GradeTemperature` / `GradeTint` / `GradeContrast` | 0 / 0 / 1 | White balance and contrast. |
| `GradeShadowTint` / `GradeHighlightTint` | 1 1 1 / 1 1 1 | Split toning. |
| `FilmGrain` / `ChromaticAberration` | 0 / 0 | Film grain (0–1) and lens fringing (pixels). |

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

Glow effects (`ScreenEffect`) already cast light automatically, in the average colour of their glow image. You don't need to add lights for muzzle flashes, explosions, thrusters or fire.

## Lua

```lua
-- Lights on any MovableObject:
actor.LightRadius = 140
actor.LightIntensity = 0.9
actor:SetLightColor(255, 230, 190)   -- 0-255
actor.LightOffset = Vector(0, -12)
actor.LightFlicker = 0.0
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

Settings changed from Lua persist in `Settings.ini` when the player next saves their settings. If your activity sets a mood, restore the previous values when it ends.

## Shaders

The lighting shaders live in `Data/Base.rte/Shaders/Lighting/`. The sprite shader `Base.rte/Shaders/Blit8.frag` and the terrain shader `Base.rte/Shaders/Terrain.frag` write a second output: a screen-space normal in RGB, and in alpha 0 for "nothing drawn" or 0.5–1 for "drawn, with emissive strength 0–1". Custom shaders used for scene drawing should write it too.
