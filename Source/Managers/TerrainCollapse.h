#pragma once

namespace RTE {
	class Vector;

	/// Terrain left floating after explosions breaks loose: a piece no longer touching any other ground becomes a rigid body
	/// that falls, tips, rolls and slides to rest, cracks into smaller pieces if it lands hard, hits units in its way and pushes liquid aside.
	/// Pieces of buildings fall too once nothing holds them; doors and small fittings stay.
	/// While it moves a piece is drawn into the terrain each update, so everything treats it as ground.
	/// Part of the simulation and deterministic: fixed sim steps, sorted order, its own seeded random numbers.
	class TerrainCollapse {

	public:
		/// Gets whether collapsing terrain is on (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether collapsing terrain is on.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Queues a check for floating terrain around a crater. Thread safe.
		/// @param position Centre of the crater, in scene coordinates.
		/// @param radius How far around it to look.
		static void QueueCheck(const Vector& position, float radius);

		/// Makes a boulder of a material in the air at a point, which falls like any loose piece. Thread safe; applied on the next sim step.
		/// @param position Its centre, in scene coordinates.
		/// @param radius Its rough radius in pixels (3 to 60).
		/// @param materialName The material's name, e.g. "Stone".
		static void SpawnChunk(const Vector& position, float radius, const char* materialName);

		/// Runs due checks. Call once per sim update, from the main thread.
		static void Update();

		/// Forgets pending checks, e.g. when the scene changes.
		static void Clear();

		/// Gets how many pixels have collapsed in this scene, for statistics.
		static int GetCollapsedCount();

		/// Gets how many loose pieces are moving right now, for statistics.
		static int GetFallingCount();

	private:
		static bool s_Enabled; //!< Whether collapsing terrain is on.
	};
} // namespace RTE
