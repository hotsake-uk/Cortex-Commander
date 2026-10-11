#pragma once

#include <string>

namespace RTE {

	/// The map browser (SandboxMapBrowser.cpp): the main menu's screen for picking a map to play the Sandbox game mode or Battle Command on,
	/// in place of the planet. Every playable map as a card with a big picture of the map, filtered by name, mod, mod type, size and whether it
	/// has saved battles; the map picked shows bigger, with its saved battles to pick from, the one picked drawn over it (its spawn zones,
	/// bases, flags, goals and objectives), and Play starts the game on it with that battle set up.
	class MapBrowser {

	public:
		/// What the browser is picking a map for.
		enum class Purpose {
			Sandbox,
			Battle
		};

		/// What was done in the browser since it was last asked.
		struct Choice {
			bool Back = false; //!< Closed without a pick: back to where it was opened from.
			bool Play = false; //!< A map picked to play.
			Purpose For = Purpose::Sandbox;
			std::string Scene; //!< The map.
			std::string Battle; //!< The saved battle picked to set up on it. Empty with NoBattle for none, or for the one marked to load with the map.
			bool NoBattle = false; //!< No saved battle: the map as it is.
		};

		/// Opens the browser, every filter as it was left except the saved battles one, which is on for Battle and off for Sandbox.
		static void Open(Purpose purpose);

		/// Closes the browser and lets go of the map pictures it holds.
		static void Close();

		/// Whether the browser is open.
		static bool IsOpen();

		/// Draws the browser while it's open. Call from the ImGui frame.
		static void DrawGUI();

		/// What was done in the browser since the last call: nothing, Back or Play.
		static Choice TakeChoice();

		/// Sets the game a Play choice asks for as the one to start: its game mode on its map, its saved battle (or none) to go in once it's there.
		/// @return Whether the game mode and the map exist to be started.
		static bool SetStartChoice(const Choice& choice);
	};
} // namespace RTE
