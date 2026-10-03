#pragma once

namespace RTE {
	class Vector;

	/// Terrain left floating after explosions breaks loose: pieces of earth, rock and sand no longer touching any other terrain
	/// fall as loose pixels and pile up where they land. Built structures (concrete, metal) stay up.
	/// Part of the simulation and deterministic: checks happen in fixed sim steps, in sorted order, with no random numbers.
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

		/// Runs due checks. Call once per sim update, from the main thread.
		static void Update();

		/// Forgets pending checks, e.g. when the scene changes.
		static void Clear();

		/// Gets how many pixels have collapsed in this scene, for statistics.
		static int GetCollapsedCount();

	private:
		static bool s_Enabled; //!< Whether collapsing terrain is on.
	};
} // namespace RTE
