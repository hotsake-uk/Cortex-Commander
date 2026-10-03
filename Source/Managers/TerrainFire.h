#pragma once

#include "glm/glm.hpp"
#include <vector>

namespace RTE {
	class MovableObject;
	class Vector;

	/// Fire that spreads through flammable terrain (grass, vegetation, wood, oil) and burns it away or to ash.
	/// This is part of the simulation and deterministic: it updates in fixed sim steps with its own seeded random numbers,
	/// and ignitions queued from (possibly parallel) collision code are sorted before they're applied.
	class TerrainFire {

	public:
		/// Gets whether terrain fire is on (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether terrain fire is on.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Gets whether a terrain material can burn. Cheap; safe to call from collision code.
		static bool IsFlammable(int materialID);

		/// Gets whether a moving object sets flammable terrain alight when it touches it (fire, flame and napalm particles). Thread safe.
		static bool IsFireSource(const MovableObject* object);

		/// Queues an ignition of the terrain pixel at a position. Thread safe.
		static void QueueIgnite(int x, int y);

		/// Queues an explosion's chance of setting flammable terrain around it alight. Thread safe.
		static void QueueIgniteArea(const Vector& position, float radius);

		/// Puts out the fire at a pixel, if it's burning. Call from the simulation (main thread).
		static void Extinguish(int x, int y);

		/// Advances the fire one simulation step. Call once per sim update, from the main thread.
		static void Update();

		/// Gets the burning pixels visible in a screen area, relative to it, with a 0..1 heat each (in z).
		static void GetBurning(const glm::vec2& screenOrigin, int width, int height, std::vector<glm::vec3>& burning);

		/// Puts out all fire, e.g. when the scene changes.
		static void Clear();

		/// Gets how many pixels are burning, for statistics.
		static int GetCount();

	private:
		static bool s_Enabled; //!< Whether terrain fire is on.
	};
} // namespace RTE
