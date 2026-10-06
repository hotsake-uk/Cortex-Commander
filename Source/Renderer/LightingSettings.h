#pragma once

#include "glm/glm.hpp"

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
		int GraphicsQuality = QualityHigh; //!< The preset these settings came from, or QualityCustom once individual effects were changed.

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
			LivingWorld = preset.Distortion;
			Embers = preset.Embers;
			GodRays = preset.GodRays;
			IndirectLight = preset.Indirect;
			PropagationIterationsPerFrame = preset.Propagation;
			RadianceCascades = preset.RadianceCascades;
			EffectsParticles = preset.Particles;
			SmokeScattering = preset.Smoke;
			ApplyShadowPreset(quality);
		}

		/// The ready-made looks: grading, grain, vignette and bloom together.
		enum Look {
			LookNatural,
			LookGritty,
			LookVivid,
			LookNoir,
			LookCount
		};

		/// Sets the grading, grain, vignette and bloom to one of the ready-made looks. Natural is the defaults.
		void ApplyLook(int look) {
			Saturation = 1.05F;
			Contrast = 1.0F;
			Temperature = 0.0F;
			Tint = 0.0F;
			Vignette = 0.15F;
			FilmGrain = 0.0F;
			BloomIntensity = 0.5F;
			ShadowTint = {1.0F, 1.0F, 1.0F};
			HighlightTint = {1.0F, 1.0F, 1.0F};
			switch (look) {
				case LookGritty:
					// Drained and hard, with cold shadows and a little grain.
					Saturation = 0.78F;
					Contrast = 1.16F;
					Temperature = -0.08F;
					Vignette = 0.32F;
					FilmGrain = 0.22F;
					ShadowTint = {0.9F, 0.97F, 1.08F};
					HighlightTint = {1.05F, 1.0F, 0.94F};
					break;
				case LookVivid:
					// Rich colour and glowing lights.
					Saturation = 1.32F;
					Contrast = 1.07F;
					Temperature = 0.06F;
					Vignette = 0.1F;
					BloomIntensity = 0.75F;
					break;
				case LookNoir:
					// Black and white, deep contrast, heavy vignette and grain.
					Saturation = 0.0F;
					Contrast = 1.28F;
					Vignette = 0.42F;
					FilmGrain = 0.3F;
					BloomIntensity = 0.6F;
					break;
				default:
					break;
			}
		}

		/// Sets only the shadow effects to a preset's values: directional daylight from Low up, shadows from solid objects and contact shading from Medium up.
		/// Also used when reading settings saved before these existed, so they follow the saved preset.
		void ApplyShadowPreset(int quality) {
			if (quality < QualityPotato || quality >= QualityCustom) {
				return;
			}
			SunShadows = quality >= QualityLow ? 0.55F : 0.0F;
			UnitShadows = quality >= QualityMedium ? 0.85F : 0.0F;
			ContactShading = quality >= QualityMedium ? 0.4F : 0.0F;
		}

		bool Enabled = true; //!< Whether scene lighting is applied at all. Glows and bloom still apply when disabled.
		glm::vec3 Ambient = {0.6F, 0.59F, 0.63F}; //!< Light where no sky light reaches: bunker interiors and caves.
		glm::vec3 SkyColor = {1.0F, 0.98F, 0.95F}; //!< Light under open sky.
		glm::vec3 ForegroundAmbient = {0.5F, 0.49F, 0.52F}; //!< Minimum light on the foreground terrain and objects, so the playfield stays readable deep underground. Caves behind stay darker.
		float AirFalloff = 0.96F; //!< How much sky light is kept per grid cell travelled through air.
		float SolidFalloff = 0.6F; //!< How much sky light is kept per grid cell travelled into terrain.
		int PropagationIterationsPerFrame = 6; //!< Sky light propagation iterations per frame. Light settles into new terrain over a few frames.

		float AtmosphereHaze = 0.18F; //!< How much the furthest background layers fade into the atmosphere, 0 to 1.
		glm::vec3 AtmosphereColor = {0.62F, 0.74F, 0.95F}; //!< Color of the atmosphere at noon, linear. Tinted by the time of day.

		int WeatherType = 0; //!< 0 clear, 1 rain, 2 snow, 3 ash fall, 4 dust storm.
		float WeatherIntensity = 0.6F; //!< How heavy the rain or snow is, 0 to 1.
		float Wind = 60.0F; //!< Horizontal wind speed for precipitation, pixels per second. Negative blows left.

		float TimeOfDay = 12.0F; //!< Hours, 0 to 24. Tints and dims the sky light through dawn, day, dusk and night. Noon reproduces the classic look.
		float DayLengthMinutes = 0.0F; //!< Real minutes for a full day/night cycle (in sim time, so it pauses with the game). 0 keeps the time of day fixed.

		float GlowLightIntensity = 2.5F; //!< Brightness of the lights cast by glow effects.
		float GlowLightRadiusScale = 8.0F; //!< Radius of glow lights relative to the glow sprite's size.
		float ShadowStrength = 0.85F; //!< How much terrain blocks dynamic lights, 0 to 1.
		float UnitShadows = 0.85F; //!< How dark the shadows are that solid objects (units, devices, doors, wreckage) cast from lights and from the sun, 0 to 1. 0 turns them off.
		float SunShadows = 0.55F; //!< Directional daylight: how much dimmer and cooler ground, walls and units are where the sun (or the moon at night) can't be seen, 0 to 1. 0 turns it off.
		float ContactShading = 0.4F; //!< How much background walls darken right next to solid objects and terrain, 0 to 1. 0 turns it off.
		float EmissiveIntensity = 1.4F; //!< Brightness of glow sprites drawn as emitted light. Above 1 lets the brightest glows feed the bloom.
		float IndirectLight = 0.35F; //!< One bounce of light: lit surfaces bleed their color onto their surroundings. 0 to disable.
		bool RadianceCascades = false; //!< Global illumination by radiance cascades: glows light their surroundings with soft occlusion, and light bounces off surfaces. Replaces the simpler indirect light.
		float GIStrength = 1.0F; //!< Brightness of the radiance cascades light.
		float GIBounce = 0.5F; //!< How much of the light reaching surfaces they pass on.
		float Scanlines = 0.0F; //!< CRT style scanlines on the final image, 0 (off) to 1.
		float Specular = 1.0F; //!< Strength of the highlights lights throw on shiny surfaces (metal, concrete, wet ground, water), 0 for none.
		float Metals = 1.0F; //!< How strongly metallic surfaces mirror their surroundings (sky from above, ground from below) and glint in the sun, 0 for none.
		float Relief = 0.6F; //!< How much the lighting reads sprites' and terrain's own shading as relief (plates, rivets, folds catch the light), 0 for the outline only.
		float SunDisc = 1.0F; //!< Brightness of the sun drawn in the sky by day, 0 for none.
		float CloudShadows = 0.5F; //!< How much drifting clouds shade the ground under open sky, 0 for none. Needs SunShadows.
		bool SurfaceStates = true; //!< Units and objects show what has happened to them: wet, sooty, snowed on, glowing hot.
		bool TracerLights = true; //!< Fast projectiles with a trail (tracers) light what they pass.
		float WaterFoam = 1.0F; //!< How much thin, broken water (a stream off a ledge, spray, the lip of a pour) is drawn as froth, with froth filling the air beside it. 0 for none. Visual only.
		float WaterFoamStray = 0.25F; //!< How much of that froth a stray pixel or two of water thrown clear gets, against a stream: 0 none (bare pixels), 1 as much as a stream.
		float WaterFoamBrightness = 1.0F; //!< How bright froth is drawn.
		float WaterFoamGlow = 0.45F; //!< How much light of its own froth carries, so it shows at night. 0: lit only by what lights the scene.
		float SoftSmoke = 1.0F; //!< How much soft, billowing smoke the game's smoke sprites trail, so smoke hangs and rolls instead of being a cluster of sprites. 0 for none. Visual only.
		float WaterLightGlow = 0.22F; //!< How much the light of lamps, fires and blasts shows as a glow in water it passes through, in the light's own colour. 0: water is only lit like a surface.
		float WaterFoamBubbles = 0.5F; //!< How much froth bubbles (flickers lighter and darker): 0 smooth like still water, 1 lively.
		float WaterMistSize = 0.45F; //!< How big each puff of spray is: 1 is about 3 to 6 pixels across at first.
		float WaterMistLife = 1.0F; //!< How long each puff lasts: 1 is about half a second to a second.
		float WaterMistOpacity = 0.42F; //!< How solid each puff is at its start.
		float WaterMistSpread = 1.0F; //!< How much each puff swells as it thins.
		float WaterMist = 0.4F; //!< How much soft spray falling and landing water throws off. 0 for none. Visual only.
		float WaterMistBrightness = 1.0F; //!< How bright the spray is drawn.
		float WaterMistGlow = 0.4F; //!< The least light the spray is drawn with, so it shows at night. 0: lit only by what lights the scene.
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

		float HeadlampBrightness = 1.4F; //!< Brightness of soldiers' headlamp beams.
		float HeadlampReach = 210.0F; //!< How far the beams reach, in pixels.
		float HeadlampWidth = 26.0F; //!< Half-angle of the beams, in degrees.
		glm::vec3 HeadlampColor = {1.0F, 0.875F, 0.687F}; //!< Color of the beams (linear).
		float HeadlampGlow = 0.35F; //!< Brightness of the small glow around the lamp itself.
		float HeadlampTeamTint = 0.0F; //!< How much each side's headlamps take its team color, 0 (none) to 1 (fully).
		bool AimDotsLight = false; //!< The dots that show where a weapon is aimed light the scene around them. Off, they still glow but cast no light.
		bool HeadlampsByDay = false; //!< Headlamps are on in daylight too, not only after dark.
		float BackgroundBlur = 0.6F; //!< How much the far background layers are softened, for depth. 0 leaves them sharp.
		float EdgeLighting = 1.0F; //!< How strongly sprite and terrain edges (from automatic normals) catch and turn away from light, 0 to 1.

		bool DistortionEnabled = true; //!< Heat haze above hot things and shockwaves from explosions.
		float HeatHaze = 1.5F; //!< Heat haze shimmer, in pixels at full heat.
		float ShockwaveStrength = 1.0F; //!< Multiplier for explosion shockwave refraction.

		float DeepNightDarkness = 0.5F; //!< How much darker the scene is in the dead of night (eleven to two) than at nightfall: 0 not at all, 0.5 half the light, 0.9 a tenth. Lamps, fires and headlamps are not dimmed.
		float SkyFollowsTime = 1.0F; //!< How far the sky art (painted as a blue day) takes the colours of the hour away from midday: a dark night sky, a red dawn and dusk, grey in bad weather. 0 only darkens the art, as before.
		float GodRays = 0.7F; //!< Strength of the light shafts in the air of caves and bunkers where the sun (or moon) gets in, 0 to disable.

		float Embers = 1.0F; //!< Amount of embers rising from fire and other warm glows, 0 to disable.
		float EffectsParticles = 1.0F; //!< Amount of visual sparks, dust and debris from explosions and impacts, 0 to disable.
		float SmokeScattering = 1.0F; //!< How brightly smoke catches the light passing through it (fire, muzzle flashes, lamps), 0 to disable.

		bool ScorchMarks = true; //!< Explosions leave soot on the terrain and glow while it cools.
		bool Stains = true; //!< Blood, oil and water splashes stain the terrain.
		bool Headlamps = true; //!< At night, soldiers switch on headlamps that light the way they're looking.
		bool NightAffectsAI = false; //!< At night, AI sees less far unless it has a headlamp on. Changes gameplay; off by default so the AI isn't handicapped.
		bool LivingWorld = true; //!< Vegetation sways in the wind and bends in blasts; snow settles and rain wets exposed ground.
		float HotSpotSeconds = 3.5F; //!< How long freshly blasted terrain glows.

		bool BloomEnabled = true;
		float BloomThreshold = 0.9F;
		float BloomKnee = 0.4F;
		float BloomIntensity = 0.5F;

		float Exposure = 1.0F;
		float AutoExposure = 0.6F; //!< How strongly exposure adapts when the scene is much brighter or darker than usual (flashes, pitch black caves), 0 to disable.
		float AutoExposureLow = 0.01F; //!< Average scene luminance below which exposure starts to brighten. Ordinary scenes, night included, stay above it.
		float AutoExposureHigh = 0.3F; //!< Average scene luminance above which exposure starts to darken, e.g. a big explosion filling the screen.
		float ShoulderStart = 0.75F; //!< Linear brightness above which highlights are softly compressed.
		float Vignette = 0.15F;
		float Saturation = 1.05F;

		float Temperature = 0.0F; //!< Color grading white balance, -1 cool to 1 warm.
		float Tint = 0.0F; //!< Color grading tint, -1 green to 1 magenta.
		float Contrast = 1.0F; //!< Color grading contrast around mid grey.
		glm::vec3 ShadowTint = {1.0F, 1.0F, 1.0F}; //!< Color multiplier for the shadows.
		glm::vec3 HighlightTint = {1.0F, 1.0F, 1.0F}; //!< Color multiplier for the highlights.
		float FilmGrain = 0.0F; //!< Film grain strength, 0 to 1.
		float ChromaticAberration = 0.0F; //!< Lens color fringing towards the screen edges, in pixels.

		int DebugView = 0; //!< Not persisted. 0 final image, 1 lighting on grey, 2 sky light only, 3 dynamic light only, 4 normals, 5 distortion, 6 GI only, 7 solid objects and the distance to them, 8 where the sun is visible.
	};
} // namespace RTE
