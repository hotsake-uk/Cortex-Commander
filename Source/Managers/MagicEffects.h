#pragma once

namespace RTE {
	class Vector;
	class MovableObject;

	/// Magic and tool effects (MG-1): what a spell, a tool or a script does to the world when it is cast, built on the systems already there. A push
	/// shoves what is in a cone straight away and sets the air blowing (AirPressure::Gust) so smoke, spray and loose bits go with it; a pull is a push
	/// the other way. Liquids that fade after a while (a spell's acid) are their material's (MaterialBehaviour::FadeAfter, FluidSim).
	/// Part of the simulation and deterministic: objects in the scene's own order, no random numbers.
	class MagicEffects {

	public:
		/// How strongly the effects work. Every value is a multiplier on how it comes, 1.
		struct Tuning {
			float PushStrength = 1.0F; //!< How hard a push or pull shoves what it reaches.
			float UnitPush = 1.0F; //!< How hard it shoves units, on top of PushStrength. 0: units are never pushed.
			float AirGust = 1.0F; //!< How hard the gust a push sets blowing is. 0: no gust.
		};

		/// Gets whether magic and tool effects do anything at all (a gameplay setting). Off, a push does nothing; what a spell sprays still flies.
		static bool IsOn() { return s_On; }

		/// Sets whether magic and tool effects do anything at all.
		static void SetOn(bool on) { s_On = on; }

		/// Gets how strongly the effects work, to read or change.
		static Tuning& GetTuning() { return s_Tuning; }

		/// Shoves everything in a cone away from a point: units, items, gibs and particles, harder the closer and the lighter they are, and not
		/// through solid ground. Also starts a gust of air the same way (AirPressure::Gust). A negative power pulls toward the point instead.
		/// @param origin Where it comes from, in scene coordinates.
		/// @param direction Which way it pushes; only its direction counts. Ignored with a spread of 360.
		/// @param power The speed it gives a weightless thing right at the origin, in metres a second. Heavier and further things get less.
		/// @param range How far it reaches, in pixels.
		/// @param spread How wide the cone is, in degrees, 0 to 360 (360 every way, a shockwave).
		/// @param caster What cast it, whose own body (and what it holds) is left alone. May be null.
		/// Thread safe; applied at the next sim update, before the air's (so its gust starts the same update).
		static void Push(const Vector& origin, const Vector& direction, float power, float range, float spread, const MovableObject* caster);

		/// Applies the effects cast since the last update. Call once per sim update, from the main thread, before the air's update.
		static void Update();

	private:
		/// Shoves what is in the cone: Push, applied.
		static void ApplyPush(const Vector& origin, const Vector& direction, float power, float range, float spread, long casterID);

		static bool s_On; //!< Whether the effects do anything at all.
		static Tuning s_Tuning; //!< How strongly they work.
	};
} // namespace RTE
