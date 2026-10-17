#pragma once

#include "Vector.h"

namespace RTE {

	class Actor;

	/// Units moved the way soft tyres move (AVehicle::UpdateSoftTyres): a unit's upper body is a weight of its own on a spring above its legs,
	/// stepped in short sub-steps, so it dips when it lands and with each footfall, leans when it starts and stops, and rides out rough ground
	/// steadily while the body under it jolts; the head and the gun lag it a little on springs of their own. Feet feel the ground across their
	/// whole width, as tyres do, and are drawn on it, turned to its slope. Limbs torn off squash where they land and flop at the knee and elbow.
	/// All of that is drawing only (MOSRotating::SetDrawNudge): units are hit and collide where they really are. Sprung legs (a setting) also
	/// hold the body up through a spring, rather than with each foot's bump on the ground.
	namespace UnitSprings {

		/// A weight on a spring in two directions, at rest at 0, stepped in sub-steps so a stiff one stays steady.
		struct Spring {
			Vector Value; //!< How far it is from rest, in pixels.
			Vector Speed; //!< How fast it is moving, in pixels a second.

			/// Moves it on.
			/// @param deltaTime The update's length, in seconds.
			/// @param frequency How fast it swings back, in swings a second.
			/// @param dampingRatio How quickly a swing dies away: 1 none past rest, lower bouncier.
			/// @param acceleration What pushes it this update, in pixels a second squared (a unit's body taking the opposite of its own push).
			/// @param limit How far it may go from rest, in pixels.
			void Step(float deltaTime, float frequency, float dampingRatio, const Vector& acceleration, float limit);

			/// Sets it at rest.
			void Reset() {
				Value.Reset();
				Speed.Reset();
			}
		};

		/// What a unit's springs are doing, kept on it (Actor).
		struct BodySprings {
			Spring Body; //!< The upper body against the legs.
			Spring Head; //!< The head against the body.
			Spring Gun; //!< The held gun against the arms.
			Vector LastVel; //!< The unit's speed at the last update, in m/s, for how hard it was pushed.
			bool Started = false; //!< Whether LastVel has been set.
			float SupportForce = 0.0F; //!< Sprung legs: the push up being passed on to the body, in newtons (see SupportFilter).
			float SupportForceSpeed = 0.0F; //!< How fast SupportForce is changing.
			float SupportOwed = 0.0F; //!< Sprung legs: what the feet have pushed up that hasn't reached the body yet, in N s (negative is up).
		float HeldPush = 0.0F; //!< Sprung legs: the feet's push up this update, held back to be passed on through the spring, in N s.
			float Softness = 1.0F; //!< How soft it moves, from its kind (see Actor::GetSoftness), at the last update.
			float Height = 24.0F; //!< How tall it is, in pixels, for how far its springs may go.
		};

		/// Gets whether units' springs are on (a setting).
		bool Enabled();

		/// Gets how strongly units' springs move them, 1 normal (a setting).
		float Strength();

		/// Gets whether units' legs hold them up through a spring (sprung legs, a physics setting).
		bool SprungLegs();

		/// Gets whether big fleshy bodies gib into wobbling chunks of meat (a setting).
		bool FleshChunks();

		/// Whether the ground at a point holds a foot up: anything but air, liquid and what units walk through (trees while they pass them).
		bool HoldsFoot(float x, float y);

		/// The ground under a foot, felt across its whole width.
		struct GroundFeel {
			bool Found = false; //!< Whether there is ground within reach.
			float Y = 0.0F; //!< How high the highest ground under it is, to a fraction of a pixel, in scene coordinates.
			float Slope = 0.0F; //!< How steeply it rises or falls under it, as pixels down per pixel to the right.
		};

		/// Feels for the ground under a span, in up to seven columns across it, each to a fraction of a pixel: the highest found, and its slope from
		/// a line through the columns' heights (so a single pebble doesn't tip a foot over).
		/// @param left The span's left edge, in scene coordinates. @param right Its right edge. @param top Where to start looking down from.
		/// @param reach How far down to look, in pixels.
		GroundFeel FeelGround(float left, float right, float top, float reach);

		/// Moves a unit's springs on one update and sets where its parts are drawn: its body, head and gun nudged, its body squashed if soft.
		/// @param actor The unit. @param springs Its springs. @param onGround Whether it stands on its feet. @param footfall Whether a foot came down this update.
		void UpdateActor(Actor& actor, BodySprings& springs, bool onGround, bool footfall);

		/// Sprung legs: passes the feet's push up, held back this update (BodySprings::HeldPush), on to the body through a spring, as a soft tyre's
		/// push reaches its vehicle, rather than in a bump at each footfall. Over time all of it arrives.
		/// @param body The unit. @param springs Its springs. @param active Whether it stands on sprung legs this update; off, nothing is held.
		void PassOnSupport(MOSRotating& body, BodySprings& springs, bool active);

		/// Kicks a unit's springs: a limb torn off, a heavy hit.
		/// @param springs Its springs. @param velocityChange The jolt, as a change of the body's speed, in m/s.
		void Jolt(BodySprings& springs, const Vector& velocityChange);
	} // namespace UnitSprings
} // namespace RTE
