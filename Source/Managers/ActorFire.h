#pragma once

namespace RTE {
	class Material;
	class MovableObject;
	class Vector;

	/// Units on fire: flames, napalm, burning ground and lava set them alight. A burning unit takes damage, panics and runs, and sets grass and nearby units alight as it goes, until it burns out or water puts it out.
	/// Anything else with a "Flammable" number value gets an "Ignited" number value instead, for its own script to act on (fuel barrels explode).
	/// Part of the simulation and deterministic: fixed sim steps, units handled in a fixed order, its own seeded random numbers.
	class ActorFire {

	public:
		/// Gets whether units can catch fire (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether units can catch fire.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Reports one thing hitting another: fire sets what it hits alight, and water puts it out. Thread safe; call from collision code.
		/// @param hitter What did the hitting.
		/// @param hitRoot The root parent of what was hit.
		/// @param hitterMaterial The hitter's material.
		static void OnHit(const MovableObject* hitter, MovableObject* hitRoot, const Material* hitterMaterial);

		/// Queues fire put straight onto an area (the sandbox's fire brush, a lightning strike): every unit and fuel barrel it touches catches. Thread safe.
		/// @param position The centre.
		/// @param radius How far it reaches, in pixels.
		static void QueueIgniteArea(const Vector& position, float radius);

		/// Burns, spreads and puts out fire on units. Call once per sim update, from the main thread, after the terrain fire update.
		static void Update();

		/// Gets whether something is on fire.
		static bool IsBurning(const MovableObject* object);

		/// Gets how many units are on fire.
		static int GetCount();

		/// Puts out every burning unit and forgets them.
		static void Clear();

	private:
		static bool s_Enabled; //!< Whether units can catch fire.
	};
} // namespace RTE
