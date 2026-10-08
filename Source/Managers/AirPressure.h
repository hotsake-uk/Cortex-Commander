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
		/// Gets whether blasts travel as waves of pressure through the air (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether blasts travel as waves of pressure through the air.
		static void SetEnabled(bool enabled);

		/// Gets whether the weather's wind carries smoke and spray along (a gameplay setting).
		static bool WindMovesSmoke() { return s_Wind; }

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

		/// Advances the waves and the wind one simulation step. Call once per sim update, from the main thread, before the liquids' update.
		static void Update();

		/// Forgets every wave, e.g. when the scene changes.
		static void Clear();

	private:
		/// The moving air pushes what is in it: a weightless thing (smoke) as fast as the air, a heavy one (a unit) hardly at all.
		static void PushObjects();

		/// The weather's wind carries smoke, and fine spray more weakly, along; in the lee of ground upwind of it, it eddies instead.
		static void BlowSmoke(long long update);

		static bool s_Enabled; //!< Whether blasts travel as waves of pressure.
		static bool s_Wind; //!< Whether the wind carries smoke and spray.
	};
} // namespace RTE
