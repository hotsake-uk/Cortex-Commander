#pragma once

#include "glm/glm.hpp"

#include <array>
#include <cmath>
#include <string>

namespace RTE {

	/// Tunable parameters of the scene lighting and post-processing. Colors are linear.
	/// Persisted in Settings.ini by SettingsMan and editable live in the Graphics Lab (DebugMan).
	struct LightingSettings {
		/// Graphics quality presets, from the classic look up to every effect at full quality.
		enum Quality {
			QualityPotato,
			QualityLow,
			QualityMedium,
			QualityHigh,
			QualityUltra,
			QualityCustom,
			QualityCount
		};
		int GraphicsQuality = QualityUltra; //!< The preset these settings came from, or QualityCustom once individual effects were changed.

		/// Sets the effects that cost performance to one of the presets, leaving the art direction (colors, grading, time of day) alone.
		void ApplyQualityPreset(int quality) {
			GraphicsQuality = quality;
			if (quality < QualityPotato || quality >= QualityCustom) {
				return;
			}
			struct Preset {
				bool Lighting, Bloom, Distortion, Scorch;
				float Embers, GodRays, Indirect;
				int Propagation;
				bool RadianceCascades;
				float Particles;
				float Smoke;
			};
			static constexpr Preset presets[] = {
			    {false, false, false, false, 0.0F, 0.0F, 0.0F, 6, false, 0.0F, 0.0F}, // Potato: the classic look.
			    {true, true, false, true, 0.5F, 0.0F, 0.0F, 3, false, 0.5F, 0.0F}, // Low
			    {true, true, true, true, 1.0F, 0.7F, 0.0F, 4, false, 1.0F, 0.7F}, // Medium
			    {true, true, true, true, 1.0F, 0.7F, 0.35F, 6, false, 1.0F, 1.0F}, // High
			    {true, true, true, true, 1.3F, 0.8F, 0.45F, 12, true, 1.5F, 1.2F}, // Ultra
			};
			const Preset& preset = presets[quality];
			Enabled = preset.Lighting;
			BloomEnabled = preset.Bloom;
			DistortionEnabled = preset.Distortion;
			ScorchMarks = preset.Scorch;
			Stains = preset.Scorch;
			UnitBloodStains = preset.Scorch;
			LivingWorld = preset.Distortion;
			Embers = preset.Embers;
			GodRays = preset.GodRays;
			IndirectLight = preset.Indirect;
			PropagationIterationsPerFrame = preset.Propagation;
			RadianceCascades = preset.RadianceCascades;
			EffectsSparks = preset.Particles;
			EffectsDust = preset.Particles;
			EffectsDebris = preset.Particles;
			SmokeScattering = preset.Smoke;
			ApplyShadowPreset(quality);
			// Water reflections: a few extra texture reads per water pixel, from Medium up.
			WaterReflections = quality >= QualityMedium;
			// Lights traced through the terrain distance field from Medium up; Low keeps the cheaper fixed march.
			LightShadowField = quality >= QualityMedium;
			// The fog volume: one pass over the light grid a frame, from Medium up.
			FogVolume = quality >= QualityMedium ? 0.6F : 0.0F;
			// Low lights its steady scenery lamps once into a map instead of every frame.
			LampCache = quality == QualityLow;
			// The sun's shadow map: remade only when the ground changes or the sun moves, from Low up (Potato has no sun shadows).
			SunShadowMap = quality >= QualityLow;
			// The weather's shelter map, the same kind of strip, from Low up too, so Potato keeps the classic weather.
			ShelterMask = quality >= QualityLow;
			// The wetness map: one small pass over the light grid a frame, from Medium up.
			WetnessMap = quality >= QualityMedium;
			// The cloud layer: a few noise reads per sky pixel, from Medium up.
			CloudLayer = quality >= QualityMedium;
		}

		/// The ready-made looks: grading, grain, vignette and bloom together.
		enum Look {
			LookNatural,
			LookGritty,
			LookVivid,
			LookNoir,
			LookCount,
			// Looks that events push the grade towards for a moment (LightingSettings::EventLooks); not offered as a look of their own.
			LookHurt = LookCount, //!< Drained, dark at the edges, the shadows reddened: badly wounded.
			LookFlash, //!< Washed out, warm and glowing: the moment after a huge blast.
			LookWarm, //!< Warm and a little richer: standing by a fire.
			LookAllCount
		};

		/// The grade a look sets: the fields ApplyLook changes, on their own.
		struct GradeLook {
			float Saturation = 1.05F;
			float Contrast = 1.0F;
			float Temperature = 0.0F;
			float Tint = 0.0F;
			float Vignette = 0.15F;
			float FilmGrain = 0.0F;
			float BloomIntensity = 0.5F;
			glm::vec3 ShadowTint = {1.0F, 1.0F, 1.0F};
			glm::vec3 HighlightTint = {1.0F, 1.0F, 1.0F};
		};

		/// Gets the grade of one of the looks, ready-made or event (Natural for anything else).
		static GradeLook LookGrade(int look) {
			GradeLook grade;
			switch (look) {
				case LookGritty:
					// Drained and hard, with cold shadows and a little grain.
					grade.Saturation = 0.78F;
					grade.Contrast = 1.16F;
					grade.Temperature = -0.08F;
					grade.Vignette = 0.32F;
					grade.FilmGrain = 0.22F;
					grade.ShadowTint = {0.9F, 0.97F, 1.08F};
					grade.HighlightTint = {1.05F, 1.0F, 0.94F};
					break;
				case LookVivid:
					// Rich colour and glowing lights.
					grade.Saturation = 1.32F;
					grade.Contrast = 1.07F;
					grade.Temperature = 0.06F;
					grade.Vignette = 0.1F;
					grade.BloomIntensity = 0.75F;
					break;
				case LookNoir:
					// Black and white, deep contrast, heavy vignette and grain.
					grade.Saturation = 0.0F;
					grade.Contrast = 1.28F;
					grade.Vignette = 0.42F;
					grade.FilmGrain = 0.3F;
					grade.BloomIntensity = 0.6F;
					break;
				case LookHurt:
					grade.Saturation = 0.35F;
					grade.Contrast = 1.1F;
					grade.Temperature = -0.03F;
					grade.Vignette = 0.6F;
					grade.FilmGrain = 0.15F;
					grade.ShadowTint = {1.1F, 0.9F, 0.9F};
					break;
				case LookFlash:
					grade.Saturation = 0.55F;
					grade.Contrast = 0.85F;
					grade.Temperature = 0.12F;
					grade.Vignette = 0.0F;
					grade.FilmGrain = 0.1F;
					grade.BloomIntensity = 1.4F;
					grade.ShadowTint = {1.1F, 1.05F, 0.95F};
					grade.HighlightTint = {1.1F, 1.08F, 1.0F};
					break;
				case LookWarm:
					grade.Saturation = 1.12F;
					grade.Contrast = 1.02F;
					grade.Temperature = 0.22F;
					grade.Tint = 0.02F;
					grade.Vignette = 0.18F;
					grade.BloomIntensity = 0.62F;
					grade.ShadowTint = {1.05F, 0.98F, 0.9F};
					grade.HighlightTint = {1.06F, 1.0F, 0.92F};
					break;
				default:
					break;
			}
			return grade;
		}

		/// Gets the grade these settings set now.
		GradeLook CurrentGrade() const {
			return {Saturation, Contrast, Temperature, Tint, Vignette, FilmGrain, BloomIntensity, ShadowTint, HighlightTint};
		}

		/// Sets the grading, grain, vignette and bloom to one of the ready-made looks. Natural is the defaults.
		void ApplyLook(int look) {
			GradeLook grade = LookGrade(look < LookCount ? look : LookNatural);
			Saturation = grade.Saturation;
			Contrast = grade.Contrast;
			Temperature = grade.Temperature;
			Tint = grade.Tint;
			Vignette = grade.Vignette;
			FilmGrain = grade.FilmGrain;
			BloomIntensity = grade.BloomIntensity;
			ShadowTint = grade.ShadowTint;
			HighlightTint = grade.HighlightTint;
		}

		/// Sets only the shadow effects to a preset's values: directional daylight and terrain shadows on the background from Low up, shadows from solid objects and contact shading from Medium up.
		/// Also used when reading settings saved before these existed, so they follow the saved preset.
		void ApplyShadowPreset(int quality) {
			if (quality < QualityPotato || quality >= QualityCustom) {
				return;
			}
			SunShadows = quality >= QualityLow ? 0.55F : 0.0F;
			BackgroundShadows = quality >= QualityLow ? 0.6F : 0.0F;
			UnitShadows = quality >= QualityMedium ? 0.85F : 0.0F;
			ContactShading = quality >= QualityMedium ? 0.4F : 0.0F;
		}

		bool Enabled = true; //!< Whether scene lighting is applied at all. Glows and bloom still apply when disabled.
		glm::vec3 Ambient = {0.6F, 0.59F, 0.63F}; //!< Light where no sky light reaches: bunker interiors and caves.
		glm::vec3 SkyColor = {1.0F, 0.98F, 0.95F}; //!< Light under open sky.
		glm::vec3 ForegroundAmbient = {0.654423F, 0.641335F, 0.6806F}; //!< Minimum light on the foreground terrain and objects, so the playfield stays readable deep underground. Caves behind stay darker.
		float AirFalloff = 0.96F; //!< How much sky light is kept per grid cell travelled through air.
		float SolidFalloff = 0.6F; //!< How much sky light is kept per grid cell travelled into terrain.
		int PropagationIterationsPerFrame = 12; //!< Sky light propagation iterations per frame. Light settles into new terrain over a few frames.

		float AtmosphereHaze = 0.0F; //!< How much the furthest background layers fade into the atmosphere, 0 to 1.
		glm::vec3 AtmosphereColor = {0.62F, 0.74F, 0.95F}; //!< Color of the atmosphere at noon, linear. Tinted by the time of day.

		int WeatherType = 0; //!< The weather's slot (Weather.h): 0 clear, 1 rain, 2 snow, 3 ash fall, 4 dust storm, 5 on the other Weather presets, mods' included.
		std::string WeatherName; //!< A custom weather (slot 5 on) asked for by name before the presets were loaded (saved settings); picked up into WeatherType once they are.
		bool OverrideMapWeather = false; //!< The player's weather (WeatherType and WeatherIntensity) is used on every map, over the weather a map sets for itself. A game's own pick on the scenario start screen and a saved game's weather still win. Off: a map with its own weather has it, as before.
		bool CustomWeather = true; //!< Weather types past the built-in four (data and mods' Weather presets) can be chosen. Off: they fall clear and the menu lists the built-in four.
		float WeatherGlow = 1.0F; //!< How much light glowing weather (acid rain, embers) gives off, 0 to 2. Built-in weather doesn't glow.
		float WeatherIntensity = 0.6F; //!< How heavy the rain or snow is, 0 to 1.
		float WeatherSpeed = 1.0F; //!< How fast the weather's drops move against their preset's own speeds: fall, wind drift, sway, swirl and flicker. 1 is as the preset has it.
		bool WeatherEverywhere = false; //!< The weather's drops show everywhere but over the terrain and its back walls: under roofs and overhangs and in cave mouths too, wherever the sky or the backdrop shows. Ground wetness, snow cover and splashes still keep to shelter. Off: drops keep out of shelter, as before.
		bool WeatherOverBackground = true; //!< The weather's drops are drawn over the background: the sky and the distant scenery. Off: they only show in front of the terrain's back walls, units and the rest of the scene.
		float Wind = 60.0F; //!< Horizontal wind speed for precipitation, pixels per second. Negative blows left.

		float TimeOfDay = 6.323529F; //!< Hours, 0 to 24. Tints and dims the sky light through dawn, day, dusk and night. Noon reproduces the classic look.
		float DayLengthMinutes = 25.4F; //!< Real minutes for a full day/night cycle (in sim time, so it pauses with the game). 0 keeps the time of day fixed.

		float GlowLightIntensity = 2.5F; //!< Brightness of the lights cast by glow effects.
		float GlowLightRadiusScale = 8.0F; //!< Radius of glow lights relative to the glow sprite's size.
		int MaxScreenLights = 1024; //!< Most lights (glow lights and dynamic lights together) drawn on one player screen; past it the faintest dynamic lights are left out. Each costs a full-radius quad with a shadow march, twice for cone lights.
		float ShadowStrength = 0.85F; //!< How much terrain blocks dynamic lights, 0 to 1.
		bool LightShadowField = true; //!< Lights' terrain shadows are traced through a distance field of the terrain: thin walls stop light instead of leaking it, and shadows soften with distance from what casts them. Off: eleven evenly spaced samples of the light grid, as before.
		bool SoftWallLight = true; //!< Where a lamp, fire or flash touches a wall, the lit edge on the wall is feathered by tracing its shadow from three points across the light instead of one. Off: the hard edge as before. Needs LightShadowField.
		float LightShadowSoftness = 1.0F; //!< How soft those shadows' edges are, 0 (sharp) to 2. Bigger lights soften more.
		float UnitShadows = 0.85F; //!< How dark the shadows are that solid objects (units, devices, doors, wreckage) cast from lights and from the sun, 0 to 1. 0 turns them off.
		bool SunShadowMap = true; //!< Sun (and moon) shadows from a shadow map of the scene: pixel-sharp next to what casts them and softer further off, and they follow the sun at once. Off: from the light grid, 4 px cells that catch up with the sun over a few frames, as before.
		float SunShadowSoftness = 1.0F; //!< How soft those shadows grow with distance from what casts them, 0 (sharp) to 2.
		float SunShadows = 1.0F; //!< Directional daylight: how much dimmer and cooler ground, walls and units are where the sun (or the moon at night) can't be seen, 0 to 1. 0 turns it off.
		float BackgroundShadows = 0.6F; //!< How dark the drop shadow is that the terrain casts on the background scenery behind it, offset away from the sun (or the moon at night), so the ground stands out from the backdrop instead of looking pasted flat on it, 0 to 1. 0 turns them off, as before. Never on the sky itself.
		float BackgroundShadowLength = 1.0F; //!< How far that shadow is offset, 0.25 to 3: 1 is about 10 pixels on the nearest scenery up to about 26 on the furthest, whose edge is also softer.
		float ContactShading = 0.4F; //!< How much background walls darken right next to solid objects and terrain, 0 to 1. 0 turns it off.
		float EmissiveIntensity = 1.4F; //!< Brightness of glow sprites drawn as emitted light. Above 1 lets the brightest glows feed the bloom.
		float IndirectLight = 0.45F; //!< One bounce of light: lit surfaces bleed their color onto their surroundings. 0 to disable.
		bool RadianceCascades = true; //!< Global illumination by radiance cascades: glows light their surroundings with soft occlusion, and light bounces off surfaces. Replaces the simpler indirect light.
		float GIStrength = 2.53F; //!< Brightness of the radiance cascades light.
		float GIBounce = 0.49F; //!< How much of the light reaching surfaces they pass on.
		int CRTStyle = 0; //!< What the CRT effect (Scanlines sets how strong) looks like: 0 scanlines, as before; 1 an aperture grille; 2 a shadow mask; 3 scanlines that bright pixels bloom across.
		float UpscaleSharpness = 1.0F; //!< How crisp the picture is when scaled up to the window: 1 even, sharp pixels as before, down to 0 plainly smoothed.
		float Scanlines = 0.39F; //!< CRT style scanlines on the final image, 0 (off) to 1.

		/// Screen styles: a whole retro look laid over everything (the battlefield and the HUD in game), each very unlike the others. A style is a set of
		/// RetroLayers of its own; Off uses the player's own RetroLayers (all off unless they turned some on). The player's saved variants (filter presets,
		/// ScreenVariant) are RetroLayers too, each based on one of these.
		enum ScreenStyles {
			StyleOff,
			StyleSynthwave, //!< Neon: an Outrun sunset sky with a striped sun and a scrolling grid, pink and cyan glow, glowing edges, VHS tape and a curved CRT.
			StyleHackerman, //!< Phosphor green on black: code rain for a sky, ASCII hills, green vector edges, afterglow trails, a terminal readout and a flickering monitor.
			StyleComic, //!< The game's own art as an inked, printed comic: cel-shaded light, black ink lines, halftone shadows, flat bold backgrounds and BOOM! on big blasts.
			StyleGameBoy, //!< Played on a Game Boy: four shades of green over everything, chunky dithered pixels, an LCD's dot grid and smear, light in four steps, dark outlines and a flash on big blasts.
			StyleCount
		};
		/// What a palette filter maps the picture's brightness onto.
		enum RetroPalettes {
			PaletteOff,
			PaletteSynthwave, //!< Indigo, violet, hot magenta, cyan and white.
			PaletteVaporwave, //!< Pastel lavender, pink and mint.
			PaletteMiami, //!< Teal and coral.
			PaletteGameBoy, //!< The four greens.
			PaletteCGA, //!< Black, cyan, magenta and white.
			PaletteSepia,
			PaletteThermal, //!< A false-colour heat camera: black, blue, purple, red, orange, yellow, white.
			PaletteNightVision, //!< Green and noisy.
			PaletteTerminal, //!< A phosphor green monitor: black, deep green, bright green, near-white green.
			PaletteComic, //!< The picture's own colours, printed punchier: richer, clean paper-white highlights, solid blacks, cool shadows.
			PaletteCount
		};
		/// The four shades a Game Boy screen (RetroLayers::LCD) and the Game Boy palette show, darkest first: the original's greens, the Pocket's and Light's
		/// screens, and the colourings the Game Boy Color gave old black and white games.
		enum LCDPalettes {
			LCDClassic, //!< The original's four greens.
			LCDPocket, //!< The Pocket's greys with a hint of olive.
			LCDLight, //!< The Light's backlit blue-green.
			LCDColorBrown, //!< Game Boy Color: brown (up on the boot screen).
			LCDColorRed, //!< Game Boy Color: red (up and A).
			LCDColorDarkBrown, //!< Game Boy Color: dark brown and cream (up and B).
			LCDColorBlue, //!< Game Boy Color: blue (left).
			LCDColorGrey, //!< Game Boy Color: greyscale (left and B).
			LCDColorPastel, //!< Game Boy Color: pastel (down).
			LCDColorYellow, //!< Game Boy Color: yellow (down and B).
			LCDColorGreen, //!< Game Boy Color: green and red (right).
			LCDColorInverted, //!< Game Boy Color: inverted (right and B).
			LCDPaletteCount
		};
		/// Gets a Game Boy screen palette's four shades in display colours, darkest first (for the inverted one, the shade the darkest things take first).
		static std::array<glm::vec3, 4> LCDShades(int palette) {
			auto rgb = [](int hex) { return glm::vec3(static_cast<float>((hex >> 16) & 0xFF), static_cast<float>((hex >> 8) & 0xFF), static_cast<float>(hex & 0xFF)) / 255.0F; };
			switch (palette) {
				case LCDPocket: return {rgb(0x1F1F1F), rgb(0x4D533C), rgb(0x8B956D), rgb(0xC4CFA1)};
				case LCDLight: return {rgb(0x00303A), rgb(0x00626B), rgb(0x00A69B), rgb(0x31E1C0)};
				case LCDColorBrown: return {rgb(0x000000), rgb(0x843100), rgb(0xFFAD63), rgb(0xFFFFFF)};
				case LCDColorRed: return {rgb(0x000000), rgb(0x943A3A), rgb(0xFF8484), rgb(0xFFFFFF)};
				case LCDColorDarkBrown: return {rgb(0x5A3108), rgb(0x846B29), rgb(0xCE9C84), rgb(0xFFE6C5)};
				case LCDColorBlue: return {rgb(0x000000), rgb(0x0000FE), rgb(0x65A49B), rgb(0xFFFFFF)};
				case LCDColorGrey: return {rgb(0x000000), rgb(0x525252), rgb(0xA5A5A5), rgb(0xFFFFFF)};
				case LCDColorPastel: return {rgb(0x000000), rgb(0x9494FE), rgb(0xFE9494), rgb(0xFFFFA5)};
				case LCDColorYellow: return {rgb(0x000000), rgb(0x7B4A00), rgb(0xFFFF00), rgb(0xFFFFFF)};
				case LCDColorGreen: return {rgb(0x000000), rgb(0xFF4200), rgb(0x51FF00), rgb(0xFFFFFF)};
				case LCDColorInverted: return {rgb(0xFFFFFF), rgb(0xFFDE00), rgb(0x008484), rgb(0x000000)};
				default: return {rgb(0x0F380F), rgb(0x306230), rgb(0x8BAC0F), rgb(0x9BBC0F)};
			}
		}
		/// Gets the battlefield's pixel size in game pixels for a ChunkyPixels setting: 1 (as is) at 0, up to 6 at 1.
		static float ChunkyBlock(float chunkyPixels) { return 1.0F + std::round((chunkyPixels < 0.0F ? 0.0F : (chunkyPixels > 1.0F ? 1.0F : chunkyPixels)) * 5.0F); }
		/// The retro filters one screen style (or the player's own, with no style, or a saved variant) sets. All 0 is the plain picture.
		struct RetroLayers {
			int Look = StyleSynthwave; //!< ScreenStyles: whose sky, edge and glow colours and kind of glitch these use (Synthwave's sunset, Hackerman's code rain, or Comic's flat inked backgrounds).
			int Palette = PaletteOff; //!< RetroPalettes.
			float PaletteStrength = 0.0F; //!< How far the picture is remapped onto the palette, 0 to 1.
			float NeonGlow = 0.0F; //!< Lower, stronger, wider bloom tinted in the look's colours, so lights, shots and blasts glow, 0 to 1.
			float EdgeLines = 0.0F; //!< Lines drawn along the edges of terrain, units and buildings, 0 to 1.
			bool InkEdges = false; //!< The lines are black ink (a comic look). Off: glowing, in the look's colours.
			float RetroSky = 0.0F; //!< How fully the sky behind the terrain is replaced by the look's own sky, 0 to 1.
			float GridSpeed = 1.0F; //!< How fast the look's sky moves (Synthwave's grid, Hackerman's code rain), 0 to 3.
			float VHS = 0.0F; //!< Video tape: colour bleeding sideways, wobble and tape noise, 0 to 1 (the tracking band is TrackingBand).
			float Glitch = 0.0F; //!< Now and then a short burst of glitching (blocks torn sideways, or in Hackerman's look scrambled into glyphs), 0 to 1.
			bool GlitchOnBlasts = false; //!< Big explosions set off a glitch burst too.
			float Curvature = 0.0F; //!< The picture bends like a CRT tube's glass, with darkened rounded corners, 0 to 1.
			float CRT = 0.0F; //!< CRT scanlines at least this strong (in the CRT style chosen), 0 to 1.
			float Flicker = 0.0F; //!< An old monitor's faint flicker and a brighter refresh band rolling down the screen, 0 to 1.
			float Posterize = 0.0F; //!< Fewer colour levels, down to four per channel at 1.
			float Dither = 0.0F; //!< Ordered dithering across the colour levels, 0 to 1.
			float Pixelate = 0.0F; //!< Chunkier pixels, up to 8 game pixels a block at 1.
			float ASCII = 0.0F; //!< The whole picture redrawn as text characters picked by brightness, 0 to 1.
			float ASCIIBackground = 0.0F; //!< The same for the distant background only (the hills behind the battlefield), 0 to 1.
			float Afterglow = 0.0F; //!< Bright moving things leave a fading trail, like an old monochrome monitor's phosphor, 0 to 1.
			float TerminalOverlay = 0.0F; //!< Faint scrolling hex and log lines down the screen's edges and a blinking cursor, under the HUD, 0 to 1.
			float CelShading = 0.0F; //!< The light falling on things stepped into flat bands, as in a cel-shaded cartoon, leaving the art's own colours alone, 0 to 1.
			float CelBands = 3.0F; //!< How many bands of light, 2 to 5.
			float Halftone = 0.0F; //!< Ben-Day dots in the shadows, bigger the darker it gets, 0 to 1.
			float ColorHalftone = 0.0F; //!< The whole picture printed in cyan, magenta and yellow dot screens, 0 to 1.
			float PaperGrain = 0.0F; //!< Newsprint: a faint paper grain and off-white highlights, 0 to 1.
			bool PaperBackgroundOnly = false; //!< The paper grain only on the backdrop behind the terrain (background walls, distant hills, sky), not on terrain, units or effects.
			float Misregister = 0.0F; //!< The colour plates printed a little out of line, up to 2 pixels, 0 to 1.
			float ComicFX = 0.0F; //!< Big blasts get radial speed lines and a hand-lettered sound effect word (BOOM!, KRAK!, BLAM!, WHAM!), 0 to 1.
			float PanelFrame = 0.0F; //!< A comic panel's frame round the screen: a paper gutter and a black ink line, 0 to 1.
			float TrackingBand = 0.0F; //!< Video tape's tracking band: a torn, snowy strip rolling slowly up the screen, 0 to 1. Apart from VHS so the rest of the tape look can be had without it.
			float LCD = 0.0F; //!< A Game Boy screen: the whole frame, HUD included, in the four shades of LCDPalette, 0 to 1.
			int LCDPalette = LCDClassic; //!< LCDPalettes: which four shades the Game Boy screen (and the Game Boy palette) show.
			float LCDGrid = 0.0F; //!< The LCD's dot matrix: thin gaps of the lightest shade between its pixels, 0 to 1.
			float LCDGhosting = 0.0F; //!< The LCD's slow pixels: everything that moves leaves a short smear, and the whole picture smears when the view scrolls, 0 to 1.
			float ChunkyPixels = 0.0F; //!< The battlefield drawn in bigger pixels, up to 6 game pixels a block at 1, leaving the HUD sharp.
			float BlastFlash = 0.0F; //!< A big blast in view flashes the screen's shades inverted for a moment, as Game Boy games do on a big hit, 0 to 1.
			float Bezel = 0.0F; //!< The handheld round the screen: grey plastic, the dark glass surround with its stripes and red power light, 0 to 1.
			float ChipSound = 0.0F; //!< The sound comes out of a handheld's tiny mono speaker: no bass, crushed to fewer bits and a lower sample rate, 0 to 1.
		};
		/// Gets the layers a screen style comes with.
		static RetroLayers StyleLayers(int style) {
			RetroLayers layers;
			switch (style) {
				case StyleSynthwave:
					layers.Palette = PaletteSynthwave;
					layers.PaletteStrength = 0.55F;
					layers.NeonGlow = 0.8F;
					layers.EdgeLines = 0.6F;
					layers.RetroSky = 1.0F;
					layers.GridSpeed = 1.0F;
					layers.VHS = 0.35F;
					layers.Glitch = 0.25F;
					layers.GlitchOnBlasts = true;
					layers.Curvature = 0.4F;
					layers.CRT = 0.45F;
					break;
				case StyleHackerman:
					layers.Look = StyleHackerman;
					layers.Palette = PaletteTerminal;
					layers.PaletteStrength = 0.85F;
					layers.NeonGlow = 0.5F;
					layers.EdgeLines = 0.5F;
					layers.RetroSky = 1.0F;
					layers.GridSpeed = 1.0F;
					layers.Glitch = 0.3F;
					layers.GlitchOnBlasts = true;
					layers.Curvature = 0.3F;
					layers.CRT = 0.6F;
					layers.Flicker = 0.45F;
					layers.ASCIIBackground = 1.0F;
					layers.Afterglow = 0.6F;
					layers.TerminalOverlay = 0.6F;
					break;
				case StyleComic:
					layers.Look = StyleComic;
					layers.Palette = PaletteComic;
					layers.PaletteStrength = 0.8F;
					layers.EdgeLines = 0.8F;
					layers.InkEdges = true;
					layers.RetroSky = 1.0F;
					layers.CelShading = 0.85F;
					layers.CelBands = 3.0F;
					layers.Halftone = 0.7F;
					layers.PaperGrain = 0.5F;
					layers.ComicFX = 1.0F;
					break;
				case StyleGameBoy:
					layers.Look = StyleGameBoy;
					layers.Palette = PaletteGameBoy;
					layers.PaletteStrength = 1.0F;
					layers.Dither = 0.7F;
					layers.EdgeLines = 0.45F;
					layers.InkEdges = true;
					layers.RetroSky = 1.0F;
					layers.CelShading = 0.8F;
					layers.CelBands = 4.0F;
					layers.LCD = 1.0F;
					layers.LCDGrid = 0.5F;
					layers.LCDGhosting = 0.45F;
					layers.ChunkyPixels = 0.4F;
					layers.BlastFlash = 1.0F;
					break;
				default:
					break;
			}
			return layers;
		}
		/// What colours ASCII mode draws its characters in.
		enum AsciiColours {
			AsciiStyleColours, //!< The screen style's look: Synthwave's pink to cyan, Hackerman's phosphor green, Comic's black ink on paper.
			AsciiSceneColours, //!< Each character in the colour of what it stands for, like a colour terminal.
			AsciiGreen, //!< A green phosphor monitor.
			AsciiAmber, //!< An amber phosphor monitor.
			AsciiWhite, //!< A white phosphor monitor.
			AsciiPaper, //!< Black typewriter ink on paper: the darker it is, the denser the characters.
			AsciiColourCount
		};
		/// Ready-made ASCII mode looks. They set how it's drawn, not how much of the picture it covers.
		enum AsciiPresets {
			AsciiPresetTerminal, //!< Green phosphor text with clean outlines.
			AsciiPresetMatrix, //!< Glowing green with the bright parts flickering through characters.
			AsciiPresetColour, //!< The scene's own colours, on a tinted cell for each character.
			AsciiPresetTypewriter, //!< Black ink on paper.
			AsciiPresetClassic, //!< The first ASCII filter: shading only, fixed to the screen.
			AsciiPresetCount
		};
		/// ASCII mode: the picture redrawn as text characters, on a settings page of its own and the same whatever screen style is on. A screen style's
		/// own ASCII layers (RetroLayers::ASCII and ASCIIBackground) add to it and are drawn the same way.
		struct AsciiModeSettings {
			float Everywhere = 0.0F; //!< How fully the whole battlefield is redrawn as characters, 0 to 1.
			float Background = 0.0F; //!< How fully the distant background (not a sky a screen style replaced) is, 0 to 1.
			int Size = 1; //!< How big the characters are: 1 is 6 by 9 game pixels a character, up to 4.
			float EdgeGlyphs = 0.6F; //!< How readily outlines are drawn in line characters (| - / \) along their direction, 0 (shading only) to 1.
			float Contrast = 1.1F; //!< How far the shading is spread between sparse and dense characters, 0.5 to 2.
			float Exposure = 1.0F; //!< How bright the picture is read as before picking characters, 0.25 to 3.
			int Colours = AsciiStyleColours; //!< AsciiColours.
			float CellFill = 0.2F; //!< A faint tint behind each character in its own colour (on paper, shading of the paper), 0 to 1.
			float Glow = 0.3F; //!< Bright characters drawn brighter, so they bloom, 0 to 1.
			float Churn = 0.0F; //!< Bright parts flicker through changing characters, 0 to 1.
			bool WorldLocked = true; //!< The characters' grid moves with the battlefield. Off: fixed to the screen, so the picture slides under it.
		};
		/// Gets an ASCII mode preset's look, keeping how much of the picture the given settings cover.
		static AsciiModeSettings AsciiPreset(int preset, const AsciiModeSettings& current) {
			AsciiModeSettings ascii;
			ascii.Everywhere = current.Everywhere;
			ascii.Background = current.Background;
			switch (preset) {
				case AsciiPresetTerminal:
					ascii.Colours = AsciiGreen;
					ascii.EdgeGlyphs = 0.6F;
					ascii.CellFill = 0.15F;
					ascii.Glow = 0.4F;
					break;
				case AsciiPresetMatrix:
					ascii.Colours = AsciiGreen;
					ascii.EdgeGlyphs = 0.4F;
					ascii.Contrast = 1.3F;
					ascii.CellFill = 0.1F;
					ascii.Glow = 0.75F;
					ascii.Churn = 0.6F;
					break;
				case AsciiPresetColour:
					ascii.Colours = AsciiSceneColours;
					ascii.EdgeGlyphs = 0.7F;
					ascii.CellFill = 0.35F;
					ascii.Glow = 0.3F;
					break;
				case AsciiPresetTypewriter:
					ascii.Colours = AsciiPaper;
					ascii.EdgeGlyphs = 0.8F;
					ascii.Contrast = 1.2F;
					ascii.CellFill = 0.3F;
					ascii.Glow = 0.0F;
					break;
				case AsciiPresetClassic:
					ascii.Colours = AsciiStyleColours;
					ascii.EdgeGlyphs = 0.0F;
					ascii.Contrast = 1.0F;
					ascii.CellFill = 0.17F;
					ascii.Glow = 0.0F;
					ascii.WorldLocked = false;
					break;
				default:
					break;
			}
			return ascii;
		}
		AsciiModeSettings Ascii; //!< ASCII mode's own settings.

		int ScreenStyle = StyleOff; //!< ScreenStyles: the retro look over everything, or Off for the player's own Retro layers. Ignored while a ScreenVariant is chosen.
		std::string ScreenVariant; //!< The player's saved variant (a filter preset in Userdata/FilterPresets) chosen as the screen style, empty for none. Its layers are VariantRetro.
		float ScreenStyleStrength = 1.0F; //!< How strongly the screen style applies, 0 to 1.
		RetroLayers Retro; //!< The player's own retro filters, used with no screen style.
		std::array<RetroLayers, StyleCount> StyleRetro = {StyleLayers(StyleOff), StyleLayers(StyleSynthwave), StyleLayers(StyleHackerman), StyleLayers(StyleComic), StyleLayers(StyleGameBoy)}; //!< Each screen style's layers, as the player tuned them.
		RetroLayers VariantRetro; //!< The chosen variant's layers, as the player tuned them since loading or saving it.

		/// Gets whether a screen style (built-in or a variant) is on, rather than the player's own filters.
		bool ScreenStyleOn() const { return !ScreenVariant.empty() || (ScreenStyle > StyleOff && ScreenStyle < StyleCount); }

		/// Gets the retro layers in effect now: the screen style's (or variant's) faded by its strength, or the player's own.
		RetroLayers ActiveRetro() const {
			if (!ScreenStyleOn()) {
				return Retro;
			}
			RetroLayers layers = ScreenVariant.empty() ? StyleRetro[ScreenStyle] : VariantRetro;
			float strength = ScreenStyleStrength < 0.0F ? 0.0F : (ScreenStyleStrength > 1.0F ? 1.0F : ScreenStyleStrength);
			for (float* value: {&layers.PaletteStrength, &layers.NeonGlow, &layers.EdgeLines, &layers.RetroSky, &layers.VHS, &layers.Glitch, &layers.Curvature, &layers.CRT, &layers.Flicker,
			                    &layers.Posterize, &layers.Dither, &layers.Pixelate, &layers.ASCII, &layers.ASCIIBackground, &layers.Afterglow, &layers.TerminalOverlay, &layers.CelShading,
			                    &layers.Halftone, &layers.ColorHalftone, &layers.PaperGrain, &layers.Misregister, &layers.ComicFX, &layers.PanelFrame, &layers.TrackingBand,
			                    &layers.LCD, &layers.LCDGrid, &layers.LCDGhosting, &layers.ChunkyPixels, &layers.BlastFlash, &layers.Bezel, &layers.ChipSound}) {
				*value *= strength;
			}
			return layers;
		}
		/// Gets the retro layers the settings edit now: the chosen variant's, the chosen screen style's, or the player's own with none.
		RetroLayers& EditedRetro() { return !ScreenVariant.empty() ? VariantRetro : (ScreenStyle > StyleOff && ScreenStyle < StyleCount ? StyleRetro[ScreenStyle] : Retro); }
		float Specular = 1.0F; //!< Strength of the highlights lights throw on shiny surfaces (metal, concrete, wet ground, water), 0 for none.
		bool UnitShineLights = false; //!< Lights (headlamps, fire, muzzle flashes, any light that moves) throw highlights on units and other solid objects and brighten their edges facing them. Off by default: a unit's own headlamp washed it out white.
		bool UnitShineLamps = true; //!< Steady scenery lamps do the same.
		bool UnitShineSun = true; //!< The sun (or moon) glints on units and other solid objects.
		float Metals = 1.0F; //!< How strongly metallic surfaces mirror their surroundings (sky from above, ground from below) and glint in the sun, 0 for none.
		float Relief = 1.5F; //!< How much the lighting reads sprites' and terrain's own shading as relief (plates, rivets, folds catch the light), 0 for the outline only.
		float SunDisc = 1.0F; //!< Brightness of the sun drawn in the sky by day, 0 for none.
		float CloudShadows = 0.54F; //!< How much drifting clouds shade the ground under open sky, 0 for none. Needs SunShadows.
		bool CloudLayer = true; //!< Clouds are drawn in the sky, as thick and as stormy as the shadows crossing the ground; they gather in bad weather and break up after it. Off: no clouds in the sky and the shadows keep their fixed spread, as before.
		float CloudCover = 0.47F; //!< How much of the sky is cloud in clear weather, 0 to 1 (0.5 is the spread the shadows always had). Rain, snow and ash fall add to it.
		float CloudOpacity = 0.68F; //!< How solid the clouds in the sky are drawn, 0 to 1.
		float CloudSize = 1.0F; //!< How big the clouds are, 0.4 to 2.5: the width of the patches (in the sky and their shadows alike), their puffs, and how deep the band they sit in is (with the square of the size, so small clouds stay long and shallow).
		float CloudHeight = 0.81F; //!< How high in the sky the cloud band sits, 0 to 1: 1 along the top of the view as before, 0 starting halfway down it.
		bool SurfaceStates = true; //!< Units and objects show what has happened to them: wet, sooty, snowed on, glowing hot.
		bool TracerLights = true; //!< Fast projectiles with a trail (tracers) light what they pass.
		float WaterFoam = 1.0F; //!< How much thin, broken water (a stream off a ledge, spray, the lip of a pour) is drawn as froth, with froth filling the air beside it. 0 for none. Visual only.
		float WaterFoamStray = 0.25F; //!< How much of that froth a stray pixel or two of water thrown clear gets, against a stream: 0 none (bare pixels), 1 as much as a stream.
		float WaterFoamBrightness = 1.0F; //!< How bright froth is drawn.
		float WaterFoamGlow = 0.45F; //!< How much light of its own froth carries, so it shows at night. 0: lit only by what lights the scene.
		float SoftSmoke = 0.39F; //!< How much soft, billowing smoke the game's smoke sprites trail, so smoke hangs and rolls instead of being a cluster of sprites. 0 for none. Visual only.
		/// The visual-only effects that can be drawn in the effects layer (c_EffectsDepth) instead of in front of everything.
		enum EffectLayer {
			LayerSmoke, //!< The game's smoke sprites, and the light smoke scatters.
			LayerSoftSmoke, //!< The soft smoke they trail, and the smoke explosions leave.
			LayerMist, //!< Spray mist off falling and splashing water.
			LayerSplash, //!< The drops of splashes.
			LayerFroth, //!< Froth on water.
			LayerDust, //!< Dust puffs from blasts and hits on the ground.
			LayerDebris, //!< Little chips thrown from blasts and hits.
			LayerSparks, //!< Sparks from blasts and hits on hard ground.
			LayerEmbers, //!< Embers off fires.
			LayerFire, //!< The balls of fire of explosions.
			EffectLayerCount
		};
		/// Where all of them go: each where its own setting says, all in front, or all behind.
		enum EffectLayersMode { EffectLayersEach = 0, EffectLayersAllFront = 1, EffectLayersAllBehind = 2 };
		/// The settings keys of the effects' own layer settings, in EffectLayer order.
		static constexpr std::array<const char*, EffectLayerCount> EffectLayerKeys = {"LayerSmoke", "LayerSoftSmoke", "LayerMist", "LayerSplash", "LayerFroth", "LayerDust", "LayerDebris", "LayerSparks", "LayerEmbers", "LayerFire"};
		int EffectLayers = EffectLayersEach; //!< EffectLayersMode: every effect in the layer its own setting picks, or all of them in front (as before) or all behind.
		std::array<bool, EffectLayerCount> EffectBehind = {true, true, true, true, true, true, false, false, false, false}; //!< Per effect: drawn in the effects layer, behind units and the ground in front (over the back walls of caves and bunkers and the background beyond). Off: in front of everything.

		/// Whether an effect is drawn behind units and the ground in front, going by EffectLayers and its own setting.
		bool Behind(EffectLayer layer) const { return EffectLayers == EffectLayersAllBehind || (EffectLayers == EffectLayersEach && EffectBehind[layer]); }
		/// Whether any effect is drawn behind.
		bool AnyBehind() const {
			for (int layer = 0; layer < EffectLayerCount; ++layer) {
				if (Behind(static_cast<EffectLayer>(layer))) {
					return true;
				}
			}
			return false;
		}
		float WaterLightGlow = 0.01F; //!< How much the light of lamps, fires and blasts shows as a glow in water it passes through, in the light's own colour. 0: water is only lit like a surface.
		bool WaterReflections = true; //!< Water mirrors what's above it, shows what's behind it bent by its ripples, and its rippled surface catches lamps and the sun. Off: water as it was, flat and tinted.
		float WaterReflectionStrength = 0.5F; //!< How strongly water mirrors the scene above it, 0 for none.
		float WaterRefraction = 0.72F; //!< How much water's ripples bend what's seen through it, and how much it darkens with depth, 0 for none.
		float WaterRipples = 1.0F; //!< How much the surface ripples tilt the light and the reflection, 0 for a flat mirror.
		bool WaterMirrorSurface = true; //!< The reflection ripples with the surface above it, each column as one, so it reads as a mirror image on moving water and goes clean on still water. Off: each pixel's own ripple shifts it, as before. Strength is WaterRipples.
		bool WaterSoftReflection = true; //!< The reflection is blurred more the deeper it is, fades out with depth and where the open air above the pool ends, and isn't clipped hard at the edge of the screen or other water. Off: sharp and cut off, as before.
		bool DistinctLiquidLooks = true; //!< Liquids past water, lava and acid (oil, mud, slime, mercury and new ones) each have their own look. Off: they are all drawn as water. Water, lava and acid look the same either way.
		bool WaterFlowSurface = true; //!< Water's surface follows how the water moves: still water goes glassy, a stream's ripples run downstream, the surface rings out where a pour lands and fast churn froths. Off: the same slow waves everywhere, as before.
		bool WaterCaustics = false; //!< The thin bright wavy lines of light that wander through water. Off: water is drawn without them.
		float WaterFlowStrength = 1.0F; //!< How strongly, 0 (as off) to 1.
		float WaterFoamBubbles = 0.5F; //!< How much froth bubbles (flickers lighter and darker): 0 smooth like still water, 1 lively.
		float WaterMistSize = 0.45F; //!< How big each puff of spray is: 1 is about 3 to 6 pixels across at first.
		float WaterMistLife = 1.0F; //!< How long each puff lasts: 1 is about half a second to a second.
		float WaterMistOpacity = 0.42F; //!< How solid each puff is at its start.
		float WaterMistSpread = 1.0F; //!< How much each puff swells as it thins.
		float WaterMist = 0.4F; //!< How much soft spray falling and landing water throws off. 0 for none. Visual only.
		float WaterThinFlow = 1.0F; //!< How much water running over the ground only a pixel or two deep is shown up: paler, with spray skipping along it, so a thin stream can be seen. 0 for not at all, up to 2. Visual only.
		float SplashFroth = 1.0F; //!< How much froth a splash leaves on the surface, and the surface throws up where the level rises from something falling in. 0 for none, up to 3. Visual only.
		float SplashFrothDensity = 1.0F; //!< How many puffs that froth is made of for the same splash: 1 as it is, up to 6 for a thick bank. Visual only.
		float SplashFrothSpecks = 1.58F; //!< How many bright pixel-sized specks sit in front of the puffs: 0 for none, up to 3.
		float SplashFrothSize = 1.0F; //!< How big each bubble of that froth is: 1 is about 4 to 9 pixels across.
		float SplashFrothLife = 0.73F; //!< How long it lasts: 1 is about one and a half to three and a half seconds.
		float SplashFrothOpacity = 0.6F; //!< How solid it is at first.
		bool PuffVariety = true; //!< Each puff of spray, froth mist, dust and smoke is turned and mirrored its own way, so they don't all show the same shape. Off: all the same way up, as before.
		float WaterSplash = 2.65F; //!< How big the splash is when something heavy falls into a liquid (falling ground, a broken-off piece): drops and spray, 1 the plain size, up to 4. 0 for none. Visual only: the liquid a body pushes aside goes into the level.
		float SplashDrops = 1.0F; //!< How many drops a splash throws, for the same size: 1 as it is, 0 for none (spray and froth only), up to 4.
		float SplashHeight = 1.0F; //!< How high a splash's drops are thrown: 1 as it is, from 0.2 (a low skirt) up to 3.
		float SplashWidth = 1.0F; //!< How far out to the sides a splash's drops are thrown: 1 as it is, from 0.2 (straight up) up to 3.
		float SplashDropSize = 1.0F; //!< How big each drop of a splash is drawn, in pixels across: 1 to 4.
		float SplashSpray = 1.0F; //!< How much soft spray a splash throws up with its drops: 1 as it is, 0 for none, up to 3.
		float SplashUnder = 1.6F; //!< A second layer of drops a splash throws, drawn under the first in a colour of its own: how many, 1 about as many as the first layer, 0 for none, up to 3. Visual only.
		glm::vec3 SplashUnderColor = {0.603395F, 0.73991F, 0.958333F}; //!< The colour of that under-layer.
		float SplashUnderLiquidColor = 0.51F; //!< How much the under-layer takes the liquid's own colour instead: 0 its own colour only, 1 the liquid's.
		float SplashUnderHeight = 0.75F; //!< How high the under-layer is thrown, as the main layer's Splash height is: 1 as high as the plain splash.
		float SplashUnderWidth = 0.37F; //!< How far out to the sides the under-layer is thrown: 1 as far as the plain splash.
		float SplashUnderDropSize = 1.0F; //!< How big each drop of the under-layer is drawn, in pixels across: 1 to 4.
		float SplashUnderOpacity = 0.16F; //!< How solid the under-layer's drops are.
		float SplashUnderScatter = 0.8F; //!< How much each under-layer drop strays from the others in direction and speed: 0 a tidy crown, 1 every which way.
		float WaterMistBrightness = 1.0F; //!< How bright the spray is drawn.
		float WaterMistGlow = 0.4F; //!< The least light the spray is drawn with, so it shows at night. 0: lit only by what lights the scene.
		bool ShelterMask = true; //!< Where rain, snow and ash can't reach, from a shelter map of the scene made the way the weather falls: overhangs, roofs and caves keep drops, splashes, wetness and snow out right to the edge, however far up the shelter is. Off: each drop and each patch of ground marches up the light grid to look for shelter, as before, which gives up a few hundred pixels up. On from the Low preset up.
		float ShelterSoftness = 1.0F; //!< How soft the edge of a shelter is for drops and splashes, 0 (ruled) to 2 (spread up to eight pixels either way).
		float RainSplashes = 1.0F; //!< How many little splashes rain makes where it lands on ground, water, roofs and units. 0 for none. Visual only.
		float WeatherLight = 0.35F; //!< The least light rain, snow, ash and dust are drawn with, so weather shows on a dark night and not only where a lamp catches it. 0 leaves it to the sky and lamps.
		float TracerGlow = 0.8F; //!< How strongly tracers and their trails shine in their own color (and so bloom), 0 for none.
		float TracerLightBrightness = 0.55F; //!< Brightness of the light a tracer throws on what it passes.
		float TracerLightRandomness = 0.35F; //!< How much tracers' lights differ from one another in size and brightness, and waver as they fly: 0 all alike and steady, 1 anything from a quarter to nearly twice the size.
		float TracerLightReach = 28.0F; //!< How far a tracer's light reaches, in pixels.

		float LightSaturation = 1.0F; //!< How colorful the light of lamps, glows, flashes and fire is: 0 makes all of it white, above 1 deepens the colors.
		glm::vec3 LightTint = {1.0F, 1.0F, 1.0F}; //!< A color every lamp, glow, flash and fire light is multiplied by.

		float LampBrightness = 1.0F; //!< Multiplier for the lamps of bunker pieces and other scenery.
		float LampReach = 1.0F; //!< Multiplier for how far scenery lamps reach.
		glm::vec3 LampTint = {1.0F, 1.0F, 1.0F}; //!< A color scenery lamps are multiplied by.
		bool LampsOffByDay = true; //!< Scenery lamps that daylight reaches (by a doorway, a window, a hole in the roof) are switched off between LampsOffFrom and LampsOffUntil. Deep lamps no daylight gets to, and warning lights that flicker or pulse, stay on. A switched off lamp can still be shot out.
		float LampsOffFrom = 9.0F; //!< Hours, 0 to 24: when daylit scenery lamps go off, each a little early or late.
		float LampsOffUntil = 20.0F; //!< Hours, 0 to 24: when they come back on.
		bool LampCache = false; //!< Steady scenery lamps are lit once into a map of the world and only relit where the ground around them changes, instead of every frame: lamp-filled bases cost about the same as one lamp, and lamps no longer count against MaxScreenLights. Units cast no shadows from them, and their shadows are as coarse as the map. Off: every lamp is drawn every frame, as before.
		int LampCacheDetail = 1; //!< How fine that map is: 0 coarse (8 px a texel), 1 (4 px), 2 fine (2 px). Large scenes get coarser so the map stays under about 64 MB.

		float HeadlampBrightness = 0.39F; //!< Brightness of soldiers' headlamp beams.
		float HeadlampReach = 64.0F; //!< How far the beams reach, in pixels.
		float HeadlampWidth = 19.0F; //!< Half-angle of the beams, in degrees.
		glm::vec3 HeadlampColor = {1.0F, 0.875F, 0.687F}; //!< Color of the beams (linear).
		float HeadlampGlow = 0.99F; //!< Brightness of the small glow around the lamp itself.
		float HeadlampTeamTint = 0.0F; //!< How much each side's headlamps take its team color, 0 (none) to 1 (fully).
		float SaberLightBrightness = 1.0F; //!< Brightness of the light lightsaber blades (energy blades) throw on what's around them.
		float SaberLightReach = 0.8F; //!< Multiplier for how far that light reaches.
		float JetpackLightBrightness = 1.0F; //!< Brightness of all the light jetpacks give off: the glow at the nozzle while they fire, and the light of their flames, flashes and exhaust glows. 0 for none.
		float JetpackLightReach = 1.0F; //!< Multiplier for how far that light reaches.
		float SaberAirGlow = 1.0F; //!< How strongly blades glow in the air around them, 0 for none.
		bool AimDotsLight = false; //!< The dots that show where a weapon is aimed light the scene around them. Off, they still glow but cast no light.
		bool HeadlampsByDay = false; //!< Headlamps are on in daylight too, not only after dark.
		bool HeadlampsOnlyInDark = true; //!< Headlamps follow the light where each unit stands: on in the dark (at night, in caves, under roofs), off in the light, by HeadlampDarkThreshold. Off: every headlamp comes on at night by the clock, wherever its unit is, as before.
		float HeadlampDarkThreshold = 0.5F; //!< How dark it must be around a unit for its headlamp to come on, as the light there (sky, lamps, fires; 1 is open daylight): on below this, off again a little above it. So a lamp comes on in a cave by day and goes off next to a lit lamp at night.
		float BackgroundBlur = 0.6F; //!< How much the far background layers are softened, for depth. 0 leaves them sharp.
		bool EventLooks = true; //!< The grade answers what happens: it flashes with a huge blast, drains and darkens at the edges when your unit is badly hurt, and warms by a fire; scripts can pulse it and crossfade between looks. Off: the grade stays as set, as before.
		float EventLookStrength = 1.0F; //!< How strongly events push the grade, 0 to 2.
		bool EventBlastFlash = true; //!< The washed-out flash after a huge blast (LookFlash, scripts' pulses of it too). Off for players bothered by flashing.
		bool EventHurtLook = true; //!< The drain, dark edges and faint heartbeat when your unit is badly hurt (LookHurt).
		bool EventFireWarmth = true; //!< The warmer grade standing by a fire (LookWarm).
		bool DepthOfField = true; //!< Blur what's nearer or further than the focus by how far it is from it, like a camera lens. Off: everything is sharp as before.
		float DepthOfFieldFocus = 0.0F; //!< Where the focus is: 0 the battlefield (units and terrain), 1 the furthest background.
		float DepthOfFieldStrength = 0.08F; //!< How strong the blur gets, 0 to 2 (2 is about 16 px at its widest).
		bool TiltShift = false; //!< Blur the top and bottom of the screen and keep a band sharp, so the battlefield reads as a model diorama. Off: as before.
		float TiltShiftLine = 0.54F; //!< Where the sharp band is, 0 the top of the screen to 1 the bottom.
		float TiltShiftStrength = 0.0F; //!< How strong its blur gets, 0 to 2.
		bool FocusEffectsInPhotoModeOnly = false; //!< Depth of field and tilt-shift only while photo mode (F8) is open. Scripts' own focus effects apply regardless.
		float EdgeLighting = 1.0F; //!< How strongly sprite and terrain edges (from automatic normals) catch and turn away from light, 0 to 1.

		bool DistortionEnabled = true; //!< Heat haze above hot things and shockwaves from explosions.
		float HeatHaze = 2.2F; //!< Heat haze shimmer, in pixels at full heat.
		bool HazeFromHeat = true; //!< Heat haze rises from hot things (fire, burning ground, blasts, warm glows) only. Off: from anything bright, lamps included, as before.
		enum FireStyles { FireBoth = 0, FirePixel = 1, FireShaderOnly = 2 };
		int FireStyle = FireBoth; //!< How fire is drawn (FireStyles): the pixel fire (a flickering dot and tongue per burning pixel, the flame sprites of flame particles), the shader's flames (shape and motion, a dark core, embers off the tips) or both, the shader's over the pixels.
		float FireFlameSize = 0.2F; //!< How tall the flames of burning ground stand.
		float FireFlameBrightness = 0.54F; //!< How bright those flames are.
		float ShockwaveStrength = 1.3F; //!< Multiplier for explosion shockwave refraction.

		bool UnitTeamColorParts = false; //!< The parts of units' sprites that mods mark (TeamColorIndex, TeamColorMapFile) are drawn in their side's colour (red, green, blue, yellow). Off: in the mod's default colour, or as painted.
		bool UnitOutline = true; //!< A stroke round each unit and what it holds, over the sky, the background and other objects but never over terrain. Off: no outline, as before.
		bool UnitOutlineOverEverything = false; //!< The stroke is drawn over terrain and water too (everything but the post-processing), so a unit hidden behind them still shows its outline. Off: never over foreground terrain.
		float UnitOutlineWidth = 1.0F; //!< How thick the stroke is, in the game's pixels (1 to 4). Zoomed out it is thickened to keep its size on screen.
		bool UnitOutlineTeamColor = true; //!< Each unit's stroke is its side's colour (red, green, blue, yellow; white for no side). Off: all are UnitOutlineColor.
		glm::vec3 UnitOutlineColor = {1.0F, 1.0F, 1.0F}; //!< The stroke's colour when not by side, as shown on screen.
		float UnitOutlineOpacity = 0.33F; //!< How solid the stroke is, 0 (unseen) to 1.
		float UnitOutlineGlow = 0.0F; //!< How brightly each outlined unit lights what's round it, in its outline's colour (its side's, UnitOutlineColor, or a highlighted unit's pink), 0 to 2. Needs lighting on. 0: no light, as before.
		bool HighlightUnits = false; //!< Set by the game, not saved: some unit is highlighted (Actor::SetHighlighted), so the outline pass runs for its bright, pulsing glow even with UnitOutline off.

		bool PaletteAnimation = true; //!< Animated palette flags: glowing liquids (lava) breathe, and colours set in Base.rte/PaletteAnimation.ini or by scripts pulse or cycle. Off: the palette stands still, as before.
		float PaletteAnimationStrength = 1.0F; //!< How far the pulses swing from each colour's own glow, 0 to 1.

		float DeepNightDarkness = 0.63F; //!< How much darker the scene is in the dead of night (eleven to two) than at nightfall: 0 not at all, 0.5 half the light, 0.9 a tenth. Lamps, fires and headlamps are not dimmed.
		float SkyFollowsTime = 1.0F; //!< How far the sky art (painted as a blue day) takes the colours of the hour away from midday: a dark night sky, a red dawn and dusk, grey in bad weather. 0 only darkens the art, as before.
		float FogVolume = 0.6F; //!< How thick mist and dust in the air are drawn: dawn mist in valleys, steam off water on lava, dust after a collapse, mist from scripts. It drifts with the wind, is lit by the sky and lamps and clears with time. 0: none, as before.
		float FogMorningMist = 0.5F; //!< How much mist gathers low in open valleys around dawn (and a little at night and in rain), 0 to 1.
		float FogOpacity = 0.85F; //!< How much of what's behind the thickest mist and dust is hidden, 0 to 1. Lower lets more of its colour through.
		float FogClearSeconds = 25.0F; //!< About how long mist and dust take to clear, in game seconds.
		bool LightningBolts = true; //!< Lightning is drawn as a jagged, forked bolt of light from the sky that lights up where it strikes. Off: the sandbox's bolt is a line of particles, as before.
		float LightningBrightness = 1.0F; //!< How bright lightning bolts, the light they cast and storms' sky flashes are, 0 to 2. 1: as first made.
		bool StormFlashes = true; //!< Storms (heavy rain, and weather with lightning in it) flash the whole sky now and then. Off for players bothered by flashing.
		float GodRays = 0.8F; //!< Strength of the light shafts in the air of caves and bunkers where the sun (or moon) gets in, 0 to disable.

		float Embers = 1.3F; //!< Amount of embers rising from fire and other warm glows, 0 to disable.
		float EffectsSparks = 1.5F; //!< Amount of visual sparks from explosions and hard impacts, 0 for none.
		float EffectsDust = 1.5F; //!< Amount of visual dust from explosions and soft impacts, 0 for none.
		float SparkLights = 1.0F; //!< Brightness of the glow and light of the game's own sparks (objects with SparkGlow), 0 for none. Sparks at 0 turns it off too.
		float EffectsDebris = 1.5F; //!< Amount of visual debris chips from explosions and impacts, 0 for none. Embers, smoke, fire and spray follow the largest of the three.
		bool SmokeShading = true; //!< Smoke takes its own colour, shadows itself (dark on the side away from a fire or the sun, lit on the near side) and has its top painted by the sun. Off: one pale tint lit evenly through, as before.
		float SmokeShadingStrength = 1.0F; //!< How strongly, 0 to 1.
		float SmokeScattering = 1.2F; //!< How brightly smoke catches the light passing through it (fire, muzzle flashes, lamps), 0 to disable.

		bool ScorchMarks = true; //!< Explosions leave soot on the terrain and glow while it cools.
		bool Stains = true; //!< Blood, oil and water splashes stain the terrain.
		bool UnitBloodStains = true; //!< Units get blood on them where they're wounded and where gore nearby splashes them, drying dark, and keep it as corpses. Healed away.
		float UnitBloodStainStrength = 1.0F; //!< How strongly it shows, 0 to 1.
		bool StainSurface = true; //!< Stains change how the ground shines: fresh blood a little, drying matte, oil glossy; soot dulls it. Off: stains and soot only tint, as before.
		float StainShine = 1.0F; //!< How much they change it, 0 to 1.
		bool DecalsFade = true; //!< Soot and stains weather away over time and wash off in the rain. Off: they stay until the scene is rebuilt, as before.
		float DecalFadeMinutes = 10.0F; //!< Game minutes for full soot to weather away (stains take half as long again), 0.5 to 60. Rain makes it much quicker.
		bool WetnessMap = true; //!< Rain wets the ground place by place and it dries after, hard rock and concrete slower than earth, with puddles in the dips after long rain. Off: all exposed ground is equally wet, as before.
		float WetDrySeconds = 120.0F; //!< Game seconds for wet earth to dry once the rain stops, 10 to 600; rock takes up to three times as long.
		float Puddles = 1.0F; //!< How much water standing in dips is drawn as reflecting puddles, 0 (none) to 1.
		bool Headlamps = false; //!< Where it's dark around them (at night, in caves, under roofs), soldiers switch on headlamps that light the way they're looking.
		bool NightAffectsAI = false; //!< Stealth (AC-11): at night the AI sees less far and less in the dark, under a roof or away from lights, a lit headlamp gives its wearer away, and footsteps are heard (Actor::HearFootsteps). Changes gameplay; off by default so the AI is not handicapped.
		bool LivingWorld = true; //!< Vegetation sways in the wind and bends in blasts; snow settles and rain wets exposed ground.
		float HotSpotSeconds = 3.5F; //!< How long freshly blasted terrain glows.

		bool BloomEnabled = true;
		float BloomThreshold = 3.67F;
		float BloomKnee = 0.4F;
		float BloomIntensity = 0.03F;

		float Exposure = 2.06F;
		float AutoExposure = 0.48F; //!< How strongly exposure adapts when the scene is much brighter or darker than usual (flashes, pitch black caves), 0 to disable.
		float AutoExposureLow = 0.021F; //!< Average scene luminance below which exposure starts to brighten. Ordinary scenes, night included, stay above it.
		bool AutoExposureGameTime = true; //!< Exposure adapts in game time, so it holds while paused and captures are repeatable. Off: in real time, as before, still adapting while paused.
		float AutoExposureHigh = 0.487F; //!< Average scene luminance above which exposure starts to darken, e.g. a big explosion filling the screen.
		float ShoulderStart = 0.75F; //!< Linear brightness above which highlights are softly compressed.
		float Vignette = 0.04F;
		float Saturation = 1.28F;

		float Temperature = 0.0F; //!< Color grading white balance, -1 cool to 1 warm.
		float Tint = 0.0F; //!< Color grading tint, -1 green to 1 magenta.
		float Contrast = 1.0F; //!< Color grading contrast around mid grey.
		glm::vec3 ShadowTint = {1.0F, 1.0F, 1.0F}; //!< Color multiplier for the shadows.
		glm::vec3 HighlightTint = {1.0F, 1.0F, 1.0F}; //!< Color multiplier for the highlights.
		float FilmGrain = 0.28F; //!< Film grain strength, 0 to 1.
		float ChromaticAberration = 0.18F; //!< Lens color fringing towards the screen edges, in pixels.
		bool ModShaders = true; //!< Mods' own shaders: objects drawn with a Shader of their own, and a scene's or activity's post pass. Off: everything is drawn with the game's shaders, as before.
		float ModShaderStrength = 1.0F; //!< How strongly mods' shaders apply, 0 to 1, handed to them as rteStrength (a mod's shader may ignore it).
		bool SpriteMaps = true; //!< Sprites that come with authored normal and emissive maps (NormalMapFile, EmissiveMapFile) are lit and glow as drawn. Off: every sprite gets the automatic bevel and palette glow, as before.
		float SpriteMapStrength = 1.0F; //!< How much the authored maps count over the automatic ones, 0 to 1.

		int DebugView = 0; //!< Not persisted. 0 final image, 1 lighting on grey, 2 sky light only, 3 dynamic light only, 4 normals, 5 distortion, 6 GI only, 7 solid objects and the distance to them, 8 where the sun is visible.
	};
} // namespace RTE
