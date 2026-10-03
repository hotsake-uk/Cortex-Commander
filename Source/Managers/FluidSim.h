#pragma once

#include <string>

namespace RTE {
	class Vector;

	/// Flowing liquids in the terrain: water, lava and acid pixels fall, spread sideways and pool.
	/// Water puts out fire; lava sets things alight, glows, hurts and turns to stone where it meets water; acid eats through soft terrain.
	/// Liquids at rest cost nothing: only pixels that moved recently, or whose surroundings changed, are simulated.
	/// Part of the simulation and deterministic: fixed sim steps, sorted order, its own seeded random numbers.
	class FluidSim {

	public:
		/// Gets whether flowing liquids are on (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether flowing liquids are on.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Gets whether a material is one of the flowing liquids.
		static bool IsLiquid(int materialID);

		/// Fills air in a circle with a liquid. Thread safe; applied on the next sim step.
		/// @param position Centre, in scene coordinates.
		/// @param radius Radius in pixels.
		/// @param liquidName "Water", "Lava" or "Acid".
		static void Pour(const Vector& position, float radius, const char* liquidName);

		/// Wakes liquid around a disturbance (explosion, collapse) so it starts flowing again. Thread safe.
		static void Disturb(const Vector& position, float radius);

		/// Advances the liquids one simulation step. Call once per sim update, from the main thread.
		static void Update();

		/// Gets the current state as text, for saved games.
		static std::string GetSaveState();

		/// Sets state from a saved game, applied when the loaded scene starts.
		static void SetPendingLoadState(const std::string& state);

		/// Forgets all moving liquid, e.g. when the scene changes. Liquid pixels stay where they are.
		static void Clear();

		/// Gets how many liquid pixels are moving, for statistics.
		static int GetActiveCount();

	private:
		static bool s_Enabled; //!< Whether flowing liquids are on.
	};
} // namespace RTE
