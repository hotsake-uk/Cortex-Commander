#pragma once

#include <string>

namespace RTE {

	/// The Depot screen (DepotBrowser.cpp): the main menu's window onto Cortex Depot, the Commander Hub site, for browsing the mods, maps,
	/// game modes and the rest shared there. Cards filtered by kind, search text, tag and source, sorted as the site sorts them; the one
	/// picked shows bigger with its description, versions, what it needs and what's inside it.
	class DepotBrowser {

	public:
		/// Opens the screen.
		/// @param kind The kind of item to show at first ("mod", "map"…), or empty for what was showing last.
		static void Open(const std::string& kind = "");

		/// Closes the screen, drops requests still waiting and lets go of its pictures.
		static void Close();

		/// Whether the screen is open.
		static bool IsOpen();

		/// Whether Back was pressed since the last call.
		static bool TakeBack();

		/// Draws the screen while it's open. Call from the ImGui frame.
		static void DrawGUI();
	};
} // namespace RTE
