#pragma once

namespace RTE {
	class Vector;
	class Actor;

	/// A coarse temperature field over the scene (SB-4): heat from fire and lava and cold from snow, rain and cryogenic fluid spread through it and settle
	/// towards the weather's own temperature. Where it is cold enough water freezes, where it is warm ice and snow melt, water boils by lava, lava crusts
	/// over in the cold and what burns catches fire by the heat; units take burns in great heat, frostbite in great cold, and walk slower in the cold.
	/// Part of the simulation and deterministic: fixed sim steps, cells in a fixed order, pixels picked by a hash of where and when.
	class Temperature {

	public:
		/// Gets whether the temperature field is on (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether the temperature field is on.
		static void SetEnabled(bool enabled);

		/// Gets whether great heat and cold hurt units (a gameplay setting).
		static bool HurtsUnits() { return s_HurtsUnits; }

		/// Sets whether great heat and cold hurt units.
		static void SetHurtsUnits(bool hurts) { s_HurtsUnits = hurts; }

		/// Gets the temperature at a point, in degrees Celsius: the weather's own where the field is off or has not started.
		/// @param position Where, in scene coordinates.
		static float GetTemperature(const Vector& position);

		/// Gets the weather's own temperature, which the field settles towards, in degrees Celsius.
		static float GetAmbient();

		/// Warms (or with a negative amount cools) the field around a point. Thread safe; applied on the next sim step.
		/// @param position Centre, in scene coordinates.
		/// @param degrees How much warmer the cell there gets, in degrees Celsius; the cells around get half.
		static void AddHeat(const Vector& position, float degrees);

		/// Gets how much the cold where a unit stands slows its walk: 1 for not at all, down to a half in hard frost.
		static float GetWalkSpeedMultiplier(const Actor* actor);

		/// Advances the field one simulation step. Call once per sim update, from the main thread, after the liquids' update.
		static void Update();

		/// Forgets the field, e.g. when the scene changes.
		static void Clear();

	private:
		static bool s_Enabled; //!< Whether the temperature field is on.
		static bool s_HurtsUnits; //!< Whether great heat and cold hurt units.
	};
} // namespace RTE
