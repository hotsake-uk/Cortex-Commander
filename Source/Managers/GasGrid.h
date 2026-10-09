#pragma once

#include <functional>

namespace RTE {
	class Vector;

	/// Gas as a grid element (SB-6): smoke, toxic gas, methane and steam held in a coarse grid over the scene. Each spreads through open air
	/// and is stopped by ground and liquid, so it fills a sealed room and leaks out of an open one; heavy gas (toxic) sinks and pools low,
	/// light gas (methane, steam) rises and escapes to the sky (gas that drifts past any edge of the scene that doesn't wrap is gone), smoke drifts up and slowly settles out, steam condenses away. Toxic gas hurts
	/// whoever breathes it, steam scalds, and methane goes up in a chain of blasts where it meets fire. Smoke, toxic gas and steam block sight.
	/// Part of the simulation and deterministic: fixed sim steps, cells and objects in a fixed order, its own seeded random numbers.
	class GasGrid {

	public:
		/// The kinds of gas.
		enum Kind {
			Smoke,
			Toxic,
			Methane,
			Steam,
			KindCount
		};

		/// Gets whether gas is simulated (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether gas is simulated.
		static void SetEnabled(bool enabled);

		/// Gets how much of the gas is shown, 0 for none (the gas still does what it does), 1 as made, up to 2 (a visual setting).
		static float GetShown() { return s_Shown; }

		/// Sets how much of the gas is shown.
		static void SetShown(float shown);

		/// Lets gas out into the air at a point. Thread safe; applied on the next sim step.
		/// @param position Where, in scene coordinates.
		/// @param kind Which gas.
		/// @param amount How much: 1 fills a cell (8 pixels a side) thick.
		static void Add(const Vector& position, Kind kind, float amount);

		/// Gets how thick a gas is at a point, 0 for none: 1 is a cell full.
		static float Get(const Vector& position, Kind kind);

		/// Visits each cell with gas thick enough to hide things, for the smoke map (SmokeGrid). Call from the main thread.
		/// @param visit Called with the cell's middle, in scene pixels, and how much it hides, in the smoke map's units.
		static void VisitObscuring(const std::function<void(int x, int y, float amount)>& visit);

		/// Gets how many cells the gas is worked out over, for statistics.
		static int GetActiveCells();

		/// Advances the gas one simulation step. Call once per sim update, from the main thread.
		static void Update();

		/// Forgets all gas, e.g. when the scene changes.
		static void Clear();

	private:
		/// Smoke puffs leave some of themselves in the air as gas, so smoke builds up where it can't get out.
		static void TakeInSmoke();

		/// Toxic gas hurts whoever breathes it; steam scalds flesh.
		static void HurtUnits();

		/// Methane where it meets fire goes up, and the blast lights the methane around it on the next update.
		static void BurnMethane();

		static bool s_Enabled; //!< Whether gas is simulated.
		static float s_Shown; //!< How much of the gas is shown.
	};
} // namespace RTE
