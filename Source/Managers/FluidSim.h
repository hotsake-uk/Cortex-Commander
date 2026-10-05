#pragma once

#include <string>

namespace RTE {
	class Vector;
	class MovableObject;

	/// Flowing liquids in the terrain: water, lava, acid and oil pixels fall, spread sideways and pool.
	/// Water puts out fire; lava sets things alight, glows, hurts and turns to stone where it meets water; acid eats through soft terrain; oil burns.
	/// Liquids at rest cost nothing: only pixels that moved recently, or whose surroundings changed, are simulated.
	/// Part of the simulation and deterministic: fixed sim steps, sorted order, its own seeded random numbers.
	class FluidSim {

	public:
		/// Gets whether flowing liquids are on (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether flowing liquids are on.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Gets whether loose powders (sand, snow, rubble, ash) slide and pile when disturbed (a gameplay setting).
		static bool PowdersEnabled() { return s_Powders; }

		/// Sets whether loose powders slide and pile.
		static void SetPowdersEnabled(bool enabled);

		/// Gets whether a material is one of the flowing liquids. Powders aren't.
		static bool IsLiquid(int materialID);

		/// Fills air in a circle with a liquid. Thread safe; applied on the next sim step.
		/// @param position Centre, in scene coordinates.
		/// @param radius Radius in pixels.
		/// @param liquidName "Water", "Lava", "Acid" or "Oil", or a powder: "Sand", "Snow", "Earth Rubble" or "Ashes".
		static void Pour(const Vector& position, float radius, const char* liquidName);

		/// Wakes liquid around a disturbance (explosion, collapse) so it starts flowing again. Thread safe.
		static void Disturb(const Vector& position, float radius);

		/// Lets a particle that just settled into the terrain join in: a drop of liquid in that liquid's own colour starts flowing (so blood, drawn in water, stays put),
		/// and a burning particle sets the flammable pixel it became alight. Call after the particle is drawn into the terrain.
		/// @param particle The settled particle.
		static void OnParticleSettled(const MovableObject* particle);

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

		/// Gets how long the last update took, in milliseconds, for statistics.
		static float GetLastUpdateMS();

	private:
		static bool s_Enabled; //!< Whether flowing liquids are on.
		static bool s_Powders; //!< Whether loose powders slide and pile.
	};
} // namespace RTE
