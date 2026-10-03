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
			Embers = preset.Embers;
			GodRays = preset.GodRays;
			IndirectLight = preset.Indirect;
			PropagationIterationsPerFrame = preset.Propagation;
			RadianceCascades = preset.RadianceCascades;
			EffectsParticles = preset.Particles;
			SmokeScattering = preset.Smoke;
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

		int WeatherType = 0; //!< 0 clear, 1 rain, 2 snow.
		float WeatherIntensity = 0.6F; //!< How heavy the rain or snow is, 0 to 1.
		float Wind = 60.0F; //!< Horizontal wind speed for precipitation, pixels per second. Negative blows left.

		float TimeOfDay = 12.0F; //!< Hours, 0 to 24. Tints and dims the sky light through dawn, day, dusk and night. Noon reproduces the classic look.
		float DayLengthMinutes = 0.0F; //!< Real minutes for a full day/night cycle (in sim time, so it pauses with the game). 0 keeps the time of day fixed.

		float GlowLightIntensity = 2.5F; //!< Brightness of the lights cast by glow effects.
		float GlowLightRadiusScale = 8.0F; //!< Radius of glow lights relative to the glow sprite's size.
		float ShadowStrength = 0.85F; //!< How much terrain blocks dynamic lights, 0 to 1.
		float EmissiveIntensity = 1.4F; //!< Brightness of glow sprites drawn as emitted light. Above 1 lets the brightest glows feed the bloom.
		float IndirectLight = 0.35F; //!< One bounce of light: lit surfaces bleed their color onto their surroundings. 0 to disable.
		bool RadianceCascades = false; //!< Global illumination by radiance cascades: glows light their surroundings with soft occlusion, and light bounces off surfaces. Replaces the simpler indirect light.
		float GIStrength = 1.0F; //!< Brightness of the radiance cascades light.
		float GIBounce = 0.5F; //!< How much of the light reaching surfaces they pass on.
		float EdgeLighting = 1.0F; //!< How strongly sprite and terrain edges (from automatic normals) catch and turn away from light, 0 to 1.

		bool DistortionEnabled = true; //!< Heat haze above hot things and shockwaves from explosions.
		float HeatHaze = 1.5F; //!< Heat haze shimmer, in pixels at full heat.
		float ShockwaveStrength = 1.0F; //!< Multiplier for explosion shockwave refraction.

		float GodRays = 0.7F; //!< Strength of light shafts streaming from the sky through gaps in terrain, 0 to disable.
		float GodRayDecay = 0.965F; //!< How quickly shafts fade along their length.

		float Embers = 1.0F; //!< Amount of embers rising from fire and other warm glows, 0 to disable.
		float EffectsParticles = 1.0F; //!< Amount of visual sparks, dust and debris from explosions and impacts, 0 to disable.
		float SmokeScattering = 1.0F; //!< How brightly smoke catches the light passing through it (fire, muzzle flashes, lamps), 0 to disable.

		bool ScorchMarks = true; //!< Explosions leave soot on the terrain and glow while it cools.
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

		int DebugView = 0; //!< Not persisted. 0 final image, 1 lighting on grey, 2 sky light only, 3 dynamic light only.
	};
} // namespace RTE
