#pragma once

#include "PostProcessMan.h"

#include <algorithm>

namespace RTE {

	/// How the scene's weather affects the simulation: rain and snow damp fire, snow slows walkers, wind pushes fire downwind.
	/// The weather comes from the scene, the scenario setup or the player's settings, and stays the same through a game.
	namespace WeatherEffects {

		/// Gets how hard it's raining, 0 for not at all to 1 for a downpour.
		inline float GetRain() {
			const LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
			return settings.WeatherType == 1 ? std::clamp(settings.WeatherIntensity, 0.0F, 1.0F) : 0.0F;
		}

		/// Gets how hard it's snowing, 0 for not at all to 1 for a blizzard.
		inline float GetSnow() {
			const LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
			return settings.WeatherType == 2 ? std::clamp(settings.WeatherIntensity, 0.0F, 1.0F) : 0.0F;
		}

		/// Gets the wind, from -1 (a gale blowing left) to 1 (a gale blowing right).
		inline float GetWind() {
			return std::clamp(g_PostProcessMan.GetLightingSettings().Wind / 150.0F, -1.0F, 1.0F);
		}

		/// Gets how thick a dust storm is, 0 for none to 1.
		inline float GetDust() {
			const LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
			return settings.WeatherType == 4 ? std::clamp(settings.WeatherIntensity, 0.0F, 1.0F) : 0.0F;
		}

		/// Gets how far units see compared to normal: a dust storm cuts it to as little as half.
		inline float GetSightMultiplier() {
			return 1.0F - 0.5F * GetDust();
		}

		/// Gets how fast units walk compared to normal: snow slows them down a little.
		inline float GetWalkSpeedMultiplier() {
			return 1.0F - 0.15F * GetSnow();
		}
	} // namespace WeatherEffects
} // namespace RTE
