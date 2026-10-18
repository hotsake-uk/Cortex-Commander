#pragma once

namespace RTE {

	class Vector;

	/// The wind as the visual effects see it: rain and snow, clouds, fog, plants, flames and the effect particles all follow it.
	/// It is worked out from the Wind setting and the smooth sim time alone, gusting and wandering on its own, and nothing in the simulation reads it.
	class VisualWind {

	public:
		/// Moves the wind on to the given smooth sim time. Called once a drawn frame, before anything reads the wind.
		/// @param smoothSimSeconds The smooth sim time now, in seconds (PostProcessMan::GetSmoothSimTimePrecise).
		/// @param baseWind The steady wind set by the settings or the scene, in pixels a second.
		static void Update(double smoothSimSeconds, float baseWind);

		/// Gets the wind as it blows right now: the steady wind with its gusts, lulls and wanderings, in pixels a second.
		static float GetWind() { return s_Wind; }

		/// Gets how far the gusts and wanderings have carried things beyond the steady wind, in pixels, wrapped far out so it stays precise.
		static float GetDrift() { return s_Drift; }

		/// Gets how hard the wind carries the light effect particles: the wind times a fixed strength.
		static float GetParticleWind() { return s_Wind * c_ParticleStrength; }

		/// Gets whether a point is out of a wind blowing this way: whether there is ground a short way upwind of it.
		/// @param position The point in the scene.
		/// @param wind The wind, for its direction.
		static bool IsSheltered(const Vector& position, float wind);

	private:
		static constexpr float c_ParticleStrength = 5.0F; //!< How hard the wind carries the effect particles, as a multiple of the wind.
		static constexpr float c_Breeze = 12.0F; //!< A light breeze that blows even with no wind set, in pixels a second.

		static float s_Wind; //!< See GetWind.
		static float s_Drift; //!< See GetDrift.
		static double s_LastSeconds; //!< The smooth sim time of the last update, or a negative value before the first.
	};
} // namespace RTE
