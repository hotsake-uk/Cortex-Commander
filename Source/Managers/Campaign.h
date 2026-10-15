#pragma once

namespace RTE {

	/// Campaign mode (CampaignScreen.cpp): the main menu's grand strategy game, in place of Conquest. A galaxy of star systems joined by
	/// space lanes, with planets and moons; each has 3 or 5 points, and holding more than half of a planet's points controls it. Factions
	/// (mods' included) raise armies at their settlements, move them between systems and onto points, and fight auto-resolved battles,
	/// turn by turn. All of it is drawn in the menu with ImGui, like the map browser. The rules are in CampaignModel.
	class Campaign {

	public:
		/// Opens the campaign screen on its start page: a new campaign, or a saved one to carry on.
		static void Open();

		/// Closes the campaign screen, saving the campaign under way.
		static void Close();

		/// Whether the campaign screen is open.
		static bool IsOpen();

		/// Draws the campaign screen while it's open. Call from the ImGui frame.
		static void DrawGUI();

		/// Whether Back to the main menu was asked for since the last call.
		static bool TakeBack();

		/// Whether a battle to fight on the battlefield was set up since the last call (ActivityMan's start activity): the main menu
		/// starts it.
		static bool TakeBattleStart();

		/// Whether a battle is being fought, or has been and its result is waiting for the campaign screen. The menus go back to the
		/// campaign after it, which applies the result when it opens.
		static bool BattleWaiting();
	};
} // namespace RTE
