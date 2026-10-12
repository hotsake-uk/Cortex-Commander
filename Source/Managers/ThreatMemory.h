#pragma once

#include "Vector.h"

namespace RTE {
	class Actor;

	/// What each team knows of its enemies (AC-2): where each enemy was last seen, by whom and when. A unit that notices an enemy reports it;
	/// the report reaches the team's other AI units close by, who turn to face it, and stays with the team, so units that lost sight of an
	/// enemy, or never saw it, know where to look, and the AI team knows where the player's units were last seen.
	/// Part of the simulation and deterministic: kept per team in order of the enemies' unique IDs, updated on the main thread. The AI scripts
	/// that report sightings run on worker threads, so a report is queued and applied, in a fixed order, at the next Update; the reads only
	/// happen while the AI runs, when nothing changes the memories.
	class ThreatMemory {

	public:
		/// Gets whether units share and remember sightings (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether units share and remember sightings.
		static void SetEnabled(bool enabled);

		/// Reports that a unit noticed an enemy: the team remembers where, and the team's other AI units close by (within 500 px) are alerted to it,
		/// unless the team already heard of that enemy within the last second. Safe from any thread: applied at the next Update.
		/// @param reporter The unit that noticed it.
		/// @param enemy The enemy noticed.
		static void Report(const Actor* reporter, const Actor* enemy);

		/// Notes that a unit has just got out of a craft or vehicle (its seats or its hold): for a few seconds after, its AI takes the enemies
		/// its team saw lately as already noticed, and turns to face the nearest (SharedBehaviors.KnowsOf). Kept in its number value
		/// "AILeftCarrierMS", the sim time it got out.
		/// @param unit The unit that got out.
		static void NoteLeftCarrier(Actor* unit);

		/// Gets where the closest enemy a team remembers was last seen, if it was seen recently enough and is still about.
		/// @param team The team that remembers.
		/// @param near The point to measure from.
		/// @param maxAgeMS How long ago at most it was last seen, in sim milliseconds.
		/// @param maxDistance How far from near at most, in pixels.
		/// @return Where, or a zero vector for none.
		static Vector GetNearestRemembered(int team, const Vector& near, float maxAgeMS, float maxDistance);

		/// Gets where a team last saw any unit a player controls, if it did within a time.
		/// @param team The team that remembers.
		/// @param maxAgeMS How long ago at most, in sim milliseconds.
		/// @return Where, or a zero vector for none.
		static Vector GetPlayerLastSeen(int team, float maxAgeMS);

		/// Gets how long ago a team last saw an enemy, in sim milliseconds, or -1 if it has no memory of it.
		static float GetAgeOf(int team, const Actor* enemy);

		/// Gets how long ago a team last saw an enemy, in sim milliseconds, or -1 if it has no memory of it, by the enemy's unique ID.
		static float GetAgeOfID(int team, long enemyID);

		/// Forgets enemies that are gone and memories over a minute old, and has every craft and vehicle with anyone inside look out for its
		/// riders (LookOutFromCarriers). Call once per sim update, from the main thread.
		static void Update();

		/// Forgets everything, e.g. when the scene changes.
		static void Clear();

	private:
		static bool s_Enabled; //!< Whether units share and remember sightings.

		/// Applies the reports queued since the last Update, on the main thread.
		static void ApplyReports();

		/// Those riding in a craft or vehicle are out of the scene, so see nothing for themselves: twice a second each craft and vehicle with
		/// anyone inside reports the enemies in clear sight of it, out to the furthest any of them can see, as if they had seen them. So the team
		/// knows what they would have seen, and they get out knowing it (see SharedBehaviors.KnowsOf).
		static void LookOutFromCarriers();
	};
} // namespace RTE
