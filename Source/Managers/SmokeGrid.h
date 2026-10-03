#pragma once

namespace RTE {
	class Vector;

	/// A coarse map of how thick smoke is across the scene, rebuilt every simulation update from the smoke particles, so units can't see through smoke.
	/// Part of the simulation and deterministic: built from simulated particles in a fixed order on the main thread, then only read.
	class SmokeGrid {

	public:
		/// Gets whether smoke blocks sight (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether smoke blocks sight.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Rebuilds the map from the current smoke particles. Call once per sim update, from the main thread, before units look around.
		static void Update();

		/// Gets whether thick enough smoke lies between two points to hide one from the other. Safe to call from any thread during the sim update.
		static bool BlocksSight(const Vector& from, const Vector& to);

		/// Gets how thick the smoke is at a point, 0 for none. Safe to call from any thread during the sim update.
		static float GetDensity(const Vector& position);

	private:
		static bool s_Enabled; //!< Whether smoke blocks sight.
	};
} // namespace RTE
