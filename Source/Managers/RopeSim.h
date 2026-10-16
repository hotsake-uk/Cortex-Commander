#pragma once

#include "glm/glm.hpp"
#include <string>
#include <vector>

namespace RTE {
	class Camera;
	class Vector;

	/// Ropes, threads, chains and cables strung through the world: each a line of light points held a fixed length apart that swings, sags,
	/// drapes over the ground and blows in the wind, tied where it was put down to the ground, to a loose piece of falling terrain, to a unit or to a thing. Tied to the ground it
	/// holds fast there until the ground under it is dug, burnt or blown away; tied to a unit or a thing it follows it, and pulls on it once it
	/// is taut (as the grapple gun's line pulls its user: what moves away along the rope is stopped, and what hangs from it hangs). Each kind is
	/// its own material: how heavy it is, how much it holds before it snaps, how far it stretches, whether it burns and how hard it is to cut.
	/// Bullets and blasts cut it, fire burns through the kinds that burn, and a pull past what it holds snaps it.
	/// Part of the simulation and deterministic: updated once per sim update in a fixed order, with its own seeded random numbers. Everything but the
	/// Queue functions is for the main thread.
	class RopeSim {

	public:
		/// What a rope is made of, as offered in the sandbox and taken by name from scripts.
		struct TypeInfo {
			const char* Name;
			const char* About; //!< What it does, for the tooltip.
			unsigned char R, G, B; //!< Its main colour, for buttons and markers.
		};

		/// A rope's link as the debug overlay draws it.
		struct DebugLink {
			glm::vec2 A;
			glm::vec2 B;
			float Load; //!< How hard it is pulled, 0 slack to 1 at the point of snapping.
			bool Burning;
		};

		/// Where a rope is tied, as the debug overlay draws it.
		struct DebugAnchor {
			glm::vec2 Pos;
			int Kind; //!< 0 the ground, 1 a unit or a thing, 2 a unit's hands holding it.
		};

		/// A unit's hold on a rope (AHuman's rope hold), as it is now.
		struct HoldInfo {
			glm::vec2 Grip{0.0F, 0.0F}; //!< Where the held point of the rope is (the unit's hands).
			glm::vec2 Up{0.0F, -1.0F}; //!< The way up the rope from the hands, a unit length: towards the tie it hangs from.
			bool Supported = false; //!< Whether a tie up the rope holds it (the unit hangs from something), else it holds a loose rope.
			bool CanClimbUp = false; //!< More rope above the hands before the tie.
			bool CanClimbDown = false; //!< More rope below the hands before its end or a cut.
			float Tension = 0.0F; //!< How hard the hold is pulled, in kg.
		};

		/// Takes hold of the nearest rope point within reach of a place for a unit: one strong enough for it, uncut on at least one side, not
		/// itself tied, and hanging from a tie further along. The point is then tied to the unit at its hands (a point fixed on its body), so
		/// the rope's own pull hangs and swings the unit (see Pull), and the unit's weight drags the rope. Call from the simulation.
		/// @param unit The unit taking hold.
		/// @param center Where its hands can reach about, and how far.
		/// @param minKg How much the rope has to hold for the unit to trust it (Thread holds next to nothing).
		/// @param gripLocal Where on the unit the hands hold, from its middle, as it is upright and unflipped.
		/// @return The rope's id, or 0 for none in reach.
		static int TakeHold(const MovableObject& unit, const Vector& center, float reach, float minKg, const Vector& gripLocal);

		/// Gets how a unit's hold on a rope is.
		/// @return Whether it still holds it: false once the rope is gone, burnt through at the hands, or the hold let go of.
		static bool GetHold(int rope, long unitID, HoldInfo& info);

		/// Moves a unit's hold a point along the rope, hand over hand: up (-1) towards the tie it hangs from, or down (1). Climbing up shortens
		/// the rope above the hands, which the rope's pull then takes up, lifting the unit; climbing down lets it drop a point.
		/// @return Whether it moved (there was rope that way).
		static bool ClimbHold(int rope, long unitID, int way);

		/// Lets go of a unit's hold on a rope.
		static void LetGo(int rope, long unitID);

		/// Gets how long a rope that is tied to nothing has to lie still before it settles into the terrain, in seconds; 0 when ropes never settle.
		static float GetSettleSeconds();

		/// Sets how long a rope tied to nothing lies still before it settles (0 never).
		static void SetSettleSeconds(float seconds);

		/// Gets how strongly ropes react to the wind, blast waves and explosions (1 as it is, 0 not at all).
		static float GetReaction();

		/// Sets how strongly ropes react to the wind, blast waves and explosions.
		static void SetReaction(float reaction);

		/// Gets how many kinds of rope there are.
		static int GetTypeCount();

		/// Gets a kind of rope's name, look and description; the first for an index out of range.
		static const TypeInfo& GetType(int type);

		/// Gets the kind of rope with a name (any case), or -1.
		static int FindType(const std::string& name);

		/// Starts a rope at a point, tied to what is there: a unit or a thing under it, else the ground under or right beside it, else nothing
		/// (a loose end). Call from the simulation.
		/// @param type The kind (GetType).
		/// @param slack How much longer than the straight line between its points each stretch is, 0 to 1 (0.1 hangs a little).
		/// @param strength A multiplier on what the kind holds before it snaps (1 as it is); it burns, cuts and weighs the same.
		/// @param anchorKg How hard, in kg, a tie can be pulled before it lets go of the rope; 0 for ties that never fail.
		/// @return Its id, or 0 if there's no scene or too many ropes.
		static int Create(int type, float slack, const Vector& position, float strength = 1.0F, float anchorKg = 0.0F);

		/// Carries a rope on to another point, tied to what is there as Create ties its first. Call from the simulation.
		/// @return Whether there was such a rope to carry on (one burnt or cut through is carried on all the same).
		static bool AddPoint(int rope, const Vector& position);

		/// Gets how many points a rope was put down with (1 for one only started), 0 for no such rope.
		static int GetPointCount(int rope);

		/// Takes a rope away. Call from the simulation.
		static void Remove(int rope);

		/// Takes every rope away.
		static void Clear();

		/// Gets how many ropes there are. Safe from any thread.
		static int GetCount();

		/// Queues a rope from a script: put down through the points in order, as Create and AddPoint would. Thread safe; made on the next sim update.
		/// @return The id it will have.
		static int QueueRope(int type, float slack, const std::vector<Vector>& points, float strength = 1.0F, float anchorKg = 0.0F);

		/// Queues carrying a rope on to another point, as AddPoint. Thread safe.
		static void QueueAddPoint(int rope, const Vector& position);

		/// Queues a rope's removal. Thread safe.
		static void QueueRemove(int rope);

		/// Queues cutting every rope within a circle. Thread safe.
		static void QueueCut(const Vector& position, float radius);

		/// Queues fire put on ropes within a circle: those that burn catch, as flammable ground does. Thread safe.
		static void QueueIgniteArea(const Vector& position, float radius);

		/// Queues an explosion's effect on ropes: they are thrown out from it, and cut close to it (a chain only very close). Thread safe.
		/// @param reach How far the blast reaches, in pixels.
		/// @param energy The gib energy, as MOSRotating's.
		static void QueueBlast(const Vector& position, float reach, float energy);

		/// Advances the ropes one simulation update. Call once per sim update, from the main thread, before the objects' update (the pulls on
		/// them are forces for their next move).
		static void Update();

		/// Draws the ropes into the scene, as pixel art. Call while drawing a camera's view.
		static void Draw(const Camera& camera);

		/// Gets the ropes' links and ties in a part of the scene, for the debug overlay.
		/// @param nodes Set to how many points the ropes have, all told.
		/// @param burning Set to how many of them are burning.
		static void GetDebug(std::vector<DebugLink>& links, std::vector<DebugAnchor>& anchors, int& nodes, int& burning);

		/// Gets the ropes as text, for saved games. Those tied to units or things are saved loose at that end.
		static std::string GetSaveState();

		/// Sets the ropes from a saved game, put back when the loaded scene starts.
		static void SetPendingLoadState(const std::string& state);
	};
} // namespace RTE
