#pragma once

#include "glm/glm.hpp"

namespace RTE {

	/// Tunable parameters of the scene lighting and post-processing. Colors are linear.
	/// Persisted in Settings.ini by SettingsMan and editable live in the Graphics Lab (DebugMan).
	struct LightingSettings {
		bool Enabled = true; //!< Whether scene lighting is applied at all. Glows and bloom still apply when disabled.
		glm::vec3 Ambient = {0.13F, 0.13F, 0.16F}; //!< Light where no sky light reaches.
		glm::vec3 SkyColor = {1.0F, 0.98F, 0.95F}; //!< Light under open sky.
		float AirFalloff = 0.96F; //!< How much sky light is kept per grid cell travelled through air.
		float SolidFalloff = 0.6F; //!< How much sky light is kept per grid cell travelled into terrain.
		int PropagationIterationsPerFrame = 6; //!< Sky light propagation iterations per frame. Light settles into new terrain over a few frames.

		float TimeOfDay = 12.0F; //!< Hours, 0 to 24. Tints and dims the sky light through dawn, day, dusk and night. Noon reproduces the classic look.
		float DayLengthMinutes = 0.0F; //!< Real minutes for a full day/night cycle (in sim time, so it pauses with the game). 0 keeps the time of day fixed.

		float GlowLightIntensity = 2.5F; //!< Brightness of the lights cast by glow effects.
		float GlowLightRadiusScale = 8.0F; //!< Radius of glow lights relative to the glow sprite's size.
		float ShadowStrength = 0.85F; //!< How much terrain blocks dynamic lights, 0 to 1.
		float EmissiveIntensity = 1.4F; //!< Brightness of glow sprites drawn as emitted light. Above 1 lets the brightest glows feed the bloom.

		bool BloomEnabled = true;
		float BloomThreshold = 0.9F;
		float BloomKnee = 0.4F;
		float BloomIntensity = 0.6F;

		float Exposure = 1.0F;
		float ShoulderStart = 0.75F; //!< Linear brightness above which highlights are softly compressed.
		float Vignette = 0.15F;
		float Saturation = 1.05F;

		int DebugView = 0; //!< Not persisted. 0 final image, 1 lighting on grey, 2 sky light only, 3 dynamic light only.
	};
} // namespace RTE
