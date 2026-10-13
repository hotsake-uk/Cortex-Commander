#pragma once

#include "glm/glm.hpp"
#include <string>
#include <vector>

namespace RTE {
	class Vector;

	/// Soft bodies: blobs of goo, jelly and bouncy gel. Each is a ring of light points held round by springs along its edge, an air pressure
	/// inside that keeps its area, and a pull back towards its own shape: it falls, squashes when it lands, wobbles, rolls down slopes and
	/// squeezes through gaps narrower than itself.
	/// While the world goes on around it a blob is drawn into the terrain as its own material, so units walk and stand on it like any ground,
	/// and it dents under them and throws them back up as it springs back (a trampoline). Bullets and blasts knock pixels out of it as out of
	/// ground: a shot that goes right through, or a blast that takes a big bite, splits it in two; what is knocked out drips as slime.
	/// Part of the simulation and deterministic: updated once per sim update in a fixed order, with its own seeded random numbers. Everything but
	/// the Queue functions is for the main thread.
	class SoftBodySim {

	public:
		/// What a blob is made of, as offered in the sandbox and taken by name from scripts.
		struct TypeInfo {
			const char* Name;
			const char* About; //!< What it does, for the tooltip.
			unsigned char R, G, B; //!< Its main colour, for buttons and markers.
		};

		/// A blob as the debug overlay draws it.
		struct DebugBlob {
			std::vector<glm::vec2> Ring;
			glm::vec2 Center;
			float Squash; //!< Its area now against the area it keeps, 1 at rest.
			int Contacts; //!< How many of its points a unit is pressing on.
		};

		/// Gets how many kinds of blob there are.
		static int GetTypeCount();

		/// Gets a kind of blob's name, look and description; the first for an index out of range.
		static const TypeInfo& GetType(int type);

		/// Gets the kind of blob with a name (any case), or -1.
		static int FindType(const std::string& name);

		/// Makes a round blob. Call from the simulation.
		/// @param type The kind (GetType).
		/// @param center Where its middle is, in scene coordinates.
		/// @param radius How big it is, in pixels (3 to 60).
		/// @param velocity How fast it is moving, in pixels per sim update.
		/// @return Its id, or 0 if there's no scene or too many blobs.
		static int Create(int type, const Vector& center, float radius, const Vector& velocity);

		/// Takes every blob away, and out of the terrain. Call from the simulation.
		static void Clear();

		/// Gets how many blobs there are. Safe from any thread.
		static int GetCount();

		/// Queues a blob from a script or another thread, made on the next sim update. Thread safe.
		static void QueueBlob(int type, const Vector& center, float radius, const Vector& velocity);

		/// Queues taking every blob away. Thread safe.
		static void QueueClear();

		/// Queues an explosion's push on blobs: their points are thrown out from it. Thread safe.
		/// @param reach How far the blast reaches, in pixels.
		/// @param energy The gib energy, as MOSRotating's.
		static void QueueBlast(const Vector& position, float reach, float energy);

		/// Lifts the blobs out of the terrain, noting the pixels of them shot, blasted or dug away since they were drawn in. Call once per sim
		/// update before the falling terrain's update, so loose pieces of ground don't take blobs' pixels for ground of their own.
		static void Lift();

		/// Moves the blobs on one sim update and draws them back into the terrain. Call once per sim update, after Lift and the falling
		/// terrain's update and before the liquids' and the objects' updates (its pushes on units are for their next move).
		static void Update();

		/// Gets the blobs, for the debug overlay.
		static void GetDebug(std::vector<DebugBlob>& blobs);

		/// Gets the blobs as text, for saved games.
		static std::string GetSaveState();

		/// Sets the blobs from a saved game, put back when the loaded scene starts.
		static void SetPendingLoadState(const std::string& state);
	};
} // namespace RTE
