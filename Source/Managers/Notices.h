#pragma once

#include <string>

namespace RTE {

	/// On-screen notices (U-1): short lines such as "Game saved" or "Nothing to undo", drawn at the top centre of the game's picture with ImGui in
	/// every game mode (and over the menus), up to four at once with the newest at the bottom, each fading out after a few seconds. For things
	/// the player did that would otherwise only write a console line, or nothing at all. Turned off with ShowNotices in the settings.
	class Notices {

	public:
		/// Shows a notice. Can be called from any thread: the notice waits under a lock and is drawn on the main thread. The same text as the
		/// newest notice still shown starts that one's time again rather than showing twice.
		/// @param text What the notice says: a few words, a short line at most.
		static void Post(const std::string& text);

		/// Draws the notices and lets go of those that have faded. Call once per frame while building the ImGui frame, on the main thread.
		static void Draw();
	};
} // namespace RTE
