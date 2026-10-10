#pragma once

namespace RTE {
	class Vector;

	/// Air pressure and wind (SB-5). A blast is a wave of pressure in a coarse grid over the scene: it spreads through air and liquid, stops
	/// at and bounces off solid ground, and so carries far down a corridor and fades quickly in the open. The air it moves pushes what is
	/// in it (smoke hardest, units least), and where it runs through liquid to the surface it throws the liquid up. Apart from blasts, the
	/// weather's wind carries smoke and fine spray along, swirling in the lee of what shelters it.
	/// Part of the simulation and deterministic: fixed sim steps, cells and objects in a fixed order.
	class AirPressure {

	public:
		/// How strongly the air does each thing it does. Every value is a multiplier on how it comes, 1.
		struct Tuning {
			float Overall = 1.0F; //!< Over all the rest: how strong the air is and how fast its waves travel. 2 twice as strong and fast, 0.5 half; 0 the air does nothing.
			float BlastStrength = 1.0F; //!< How much pressure a blast puts into the air: 2 twice as much, 0.5 half.
			float BlastReach = 1.0F; //!< How far a wave carries before it dies away: 2 about twice as far, 0.5 half.
			float PushStrength = 1.0F; //!< How hard moving air pushes smoke, loose things and gibs.
			float UnitPush = 1.0F; //!< How hard moving air pushes units, on top of PushStrength. 0: units are never pushed.
			float LiquidThrow = 1.0F; //!< How readily a wave running up through liquid throws it into the air. 0: never.
			float WindStrength = 5.0F; //!< How hard the weather's wind carries smoke and spray.
			float WindGas = 1.71F; //!< How fast the weather's wind carries gas (SB-6) along. 0: the wind leaves gas be.
			// Natural wind: the weather's wind as it really blows, rather than one steady speed. These change the wind itself, so everything that
			// follows it does too (rain and snow, plants, fog, clouds, fire, smoke), whether or not the air does anything (IsOn).
			float Gusts = 1.0F; //!< How much the wind gusts and lulls every few seconds: 1 gusts of about half again, 2 nearly twice. 0: steady.
			float Shifts = 1.0F; //!< How much the wind's strength wanders over a minute or so, and the breeze's way with it. 0: it stays as set.
			float Breeze = 12.0F; //!< A light breeze that blows even with no wind set, in pixels a second, wandering in strength and now and then turning about. 0: still air is still.
		};

		/// Gets whether the air does anything at all: blast waves, wind on smoke and wind on gas (a gameplay setting, over the ones below).
		static bool IsOn() { return s_On; }

		/// Sets whether the air does anything at all.
		static void SetOn(bool on);

		/// Gets how strongly the air does each thing it does, to read or change.
		static Tuning& GetTuning() { return s_Tuning; }

		/// Gets Tuning::Overall, never below 0.
		static float GetOverall() { return s_Tuning.Overall > 0.0F ? s_Tuning.Overall : 0.0F; }

		/// Gets whether blasts travel as waves of pressure through the air (a gameplay setting, while IsOn).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether blasts travel as waves of pressure through the air.
		static void SetEnabled(bool enabled);

		/// Gets whether the weather's wind carries smoke and spray along (a gameplay setting, while IsOn).
		static bool WindMovesSmoke() { return s_Wind; }

		/// Gets the wind as it blows now, in pixels a second, rightwards positive: the weather's wind (Time & weather, Wind) with the natural wind's
		/// gusts, shifts and breeze (Tuning::Gusts, Shifts, Breeze). Deterministic: worked out from the sim time, the same on every machine.
		static float GetNaturalWind();

		/// Gets how far, in pixels, the natural wind has carried things beyond what the weather's steady wind would have, since the scene began:
		/// for what is drawn from a time (falling rain and snow, cloud shadows), so that gusts move it along without making it jump.
		static float GetNaturalWindDrift() { return s_NaturalDrift; }

		/// Gets the wind the air carries things with, from -1 (a gale blowing left) to 1 (a gale blowing right): the weather's wind times
		/// Tuning::WindStrength, 0 while the air is off or the wind doesn't carry smoke.
		static float GetWind();

		/// Gets the wind the air carries drawn effects with (smoke and fire puffs, embers, spray, dust), in pixels a second, rightwards positive:
		/// the weather's wind times Tuning::WindStrength, 0 while the air is off or the wind doesn't carry smoke.
		static float GetWindSpeed();

		/// Gets how much a passing blast wave changes a weightless thing's speed this update at a point, in metres a second, as PushObjects
		/// pushes smoke: zero in still air or while blast waves are off.
		/// @param position Where, in scene coordinates.
		static Vector GetPush(const Vector& position);

		/// Gets whether a point is in the lee of ground upwind of it, where the wind eddies instead of blowing through.
		/// @param position Where, in scene coordinates.
		/// @param wind The wind, as GetWind gives it: only its sign (which way is upwind) counts.
		static bool IsSheltered(const Vector& position, float wind);

		/// Sets whether the weather's wind carries smoke and spray along.
		static void SetWindMovesSmoke(bool enabled) { s_Wind = enabled; }

		/// Starts a wave of pressure at a point. Thread safe; applied on the next sim step.
		/// @param position Where, in scene coordinates.
		/// @param energy How big a blast: the energy of the explosion that made it, as MOSRotating's gib energy.
		static void Blast(const Vector& position, float energy);

		/// Gets how the air moves at a point because of blasts, in metres a second per update of push, 0 where it is still.
		/// @param position Where, in scene coordinates.
		static Vector GetFlow(const Vector& position);

		/// Gets the pressure at a point above still air, 0 where no blast is passing.
		static float GetPressure(const Vector& position);

		/// Gets how many cells the waves are being worked out over, for statistics.
		static int GetActiveCells();

		/// Gets the part of the scene the waves are being worked out over, in scene pixels, for the debug overlay.
		/// @return Whether any wave is going; if not, the rest are left as they were.
		static bool GetActiveArea(int& left, int& top, int& right, int& bottom);

		/// Gets how many pixels a side each cell of the waves' grid covers.
		static int GetCellSize();

		/// Advances the waves and the wind one simulation step. Call once per sim update, from the main thread, before the liquids' update.
		static void Update();

		/// Forgets every wave, e.g. when the scene changes.
		static void Clear();

	private:
		/// The moving air pushes what is in it: a weightless thing (smoke) as fast as the air, a heavy one (a unit) hardly at all.
		static void PushObjects();

		/// The weather's wind carries smoke, and fine spray more weakly, along; in the lee of ground upwind of it, it eddies instead.
		static void BlowSmoke(long long update);

		static bool s_On; //!< Whether the air does anything at all.
		static Tuning s_Tuning; //!< How strongly the air does each thing it does.
		static float s_NaturalDrift; //!< See GetNaturalWindDrift.
		static bool s_Enabled; //!< Whether blasts travel as waves of pressure.
		static bool s_Wind; //!< Whether the wind carries smoke and spray.
	};
} // namespace RTE
