#pragma once

#include <functional>

namespace RTE {
	class Vector;

	/// Lightning that hits the ground: a bolt from the open sky to the first solid thing under a point, a flash, fire where it lands, harm to units
	/// close by, and thunder. The sandbox's lightning tool and storm cells strike with it, and a heavy rainstorm strikes on its own.
	/// Part of the simulation and deterministic: the weather picks when and where it strikes from the sim update count, never from the camera,
	/// so a replay strikes the same places; the bolt's look comes from where and when it struck, not from the simulation's random numbers.
	class WeatherLightning {

	public:
		/// What a heavy rainstorm's lightning does (a gameplay setting).
		enum class Strikes {
			SkyOnly = 0, //!< Flashes in the sky only, as before.
			Fires = 1, //!< Bolts hit the ground and set fires, but never hurt units.
			FiresAndUnits = 2 //!< Bolts hit the ground, set fires and hurt units close to where they land.
		};

		/// Gets what a heavy rainstorm's lightning does.
		static Strikes GetStrikes() { return s_Strikes; }

		/// Sets what a heavy rainstorm's lightning does.
		static void SetStrikes(Strikes strikes) { s_Strikes = strikes; }

		/// Strikes lightning at a point: a bolt from the open sky above it, at most 480 px up, to the first solid thing below it.
		/// @param target Where to strike, in scene coordinates. The bolt lands on the first non-air pixel at or below it.
		/// @param random01 The caller's random numbers, 0 to 1, used for the bolt's particle path and where charges land, in a fixed order.
		/// @param harmUnits Whether units within 30 px are hit.
		static void Strike(const Vector& target, const std::function<float()>& random01, bool harmUnits);

		/// Strikes now and then in a heavy rainstorm (rain over half intensity), harder storms more often. Call once per sim update, from the main thread.
		static void Update();

	private:
		static Strikes s_Strikes; //!< What a rainstorm's lightning does.
	};
} // namespace RTE
