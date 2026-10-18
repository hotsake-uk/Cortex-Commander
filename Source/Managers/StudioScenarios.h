#pragma once

#include <string>

namespace RTE {

	/// The main menu's Studio Scenarios screen (StudioScenarios.cpp): every scenario (game activity) the game knows, with the map it's played
	/// on and the mod it comes from, to pick one and play it. Picking one goes to the scenario setup with it selected (and its own map, when
	/// it names one), so the difficulty, teams and gold are set as usual. Drawn in the main menu with ImGui, like the map browser.
	class StudioScenarios {

	public:
		/// Opens the screen, the list read again (a mod reloaded since shows its changes).
		static void Open();

		/// Closes the screen.
		static void Close();

		/// Whether the screen is open.
		static bool IsOpen();

		/// Draws the screen while it's open. Call from the ImGui frame.
		static void DrawGUI();

		/// Whether Back was pressed since the last call.
		static bool TakeBack();

		/// The scenario picked to play since the last call (its preset name), or empty.
		static std::string TakePlay();

		/// The scenario the scenario menu should select (and open the setup for) when it opens next, taken once; empty for none.
		static std::string TakePending();
	};
} // namespace RTE
