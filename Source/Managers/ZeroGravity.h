#pragma once

namespace RTE {
	class Vector;

	/// Zero gravity: a map with next to no gravity (its GlobalAcceleration, which scripts can change, such as the ships of Void
	/// Wanderers in space), and the scene's NoGravityArea. The Zero G Settings say what follows it: falling ground and sand, liquids, the
	/// jetpack's push, and how AI units get about. Saved with the settings.
	class ZeroGravity {

	public:
		/// Under this much gravity (m/s/s, a twentieth of the stock 20) a map counts as weightless.
		static constexpr float c_Weightless = 1.0F;

		/// Whether the map has next to no gravity now.
		static bool SceneWeightless();

		/// Whether ground and liquid at a place are weightless: the map has next to no gravity, or (with CountZones on) the place is in its
		/// NoGravityArea.
		/// @param point The place.
		static bool GroundWeightlessAt(const Vector& point);

		/// The gravity on ground and liquid at a place, in m/s/s: the map's, or none in its NoGravityArea while CountZones is on.
		/// @param point The place.
		static Vector GroundGravityAt(const Vector& point);

		/// Whether units are weightless: the map has next to no gravity. (A NoGravityArea doesn't take the gravity off units and objects;
		/// the lifts that make one move what's in it themselves.)
		static bool UnitsWeightless();

#pragma region Zero G Settings
		/// Puts the Zero G Settings back to how the game comes.
		static void ResetSettings();

		static bool TerrainFollowsGravity() { return s_TerrainFollowsGravity; } //!< Falling ground and sand go by the map's gravity: none, and nothing comes down.
		static void SetTerrainFollowsGravity(bool on) { s_TerrainFollowsGravity = on; }
		static bool LiquidsFloat() { return s_LiquidsFloat; } //!< Liquids hang where they are where there's no gravity.
		static void SetLiquidsFloat(bool on) { s_LiquidsFloat = on; }
		static bool CountZones() { return s_CountZones; } //!< A map's no gravity zones (NoGravityArea) count for ground and liquid as well as no gravity everywhere.
		static void SetCountZones(bool on) { s_CountZones = on; }
		static bool JetpackAnyDirection() { return s_JetpackAnyDirection; } //!< With no gravity a jetpack pushes whichever way the unit wants to go.
		static void SetJetpackAnyDirection(bool on) { s_JetpackAnyDirection = on; }
		static bool AIFlies() { return s_AIFlies; } //!< With no gravity AI units fly their routes on the jetpack, braking at each point, instead of walking and landing.
		static void SetAIFlies(bool on) { s_AIFlies = on; }
		static bool PushOff() { return s_PushOff; } //!< With no gravity AI units with no jetpack (or an empty one) push off what they touch towards where they're going.
		static void SetPushOff(bool on) { s_PushOff = on; }
		static float FlySpeed() { return s_FlySpeed; } //!< How fast AI units fly in zero gravity, m/s.
		static void SetFlySpeed(float speed) { s_FlySpeed = speed; }
		static float PushSpeed() { return s_PushSpeed; } //!< How hard a push-off sends a unit, m/s.
		static void SetPushSpeed(float speed) { s_PushSpeed = speed; }
#pragma endregion

	private:
		static bool s_TerrainFollowsGravity;
		static bool s_LiquidsFloat;
		static bool s_CountZones;
		static bool s_JetpackAnyDirection;
		static bool s_AIFlies;
		static bool s_PushOff;
		static float s_FlySpeed;
		static float s_PushSpeed;
	};
} // namespace RTE
