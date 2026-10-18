#pragma once

#include <chrono>
#include <filesystem>
#include <string>

namespace RTE {

	/// The game's side of the link to Cortex Mod Studio (or any tool that speaks the same files), on only when the game is started with
	/// -modstudio. The link is a few plain files in the ModStudio folder next to the game: the tool writes a request and the game reads it
	/// once a frame (or every few hundred milliseconds), and the game writes what the tool shows. Nothing here changes the simulation unless
	/// the tool asks for it, and with -modstudio left off none of it runs.
	///
	///   ModStudio/Console.log   Every line the console prints, as it prints it (the game's own LogConsole.txt is only written at exit).
	///   ModStudio/Scripts.ini   From the tool, after it saves files: "ReloadFile = <module path>" reads an INI file's presets again (or an
	///                           image's pixels), "ReloadScript = <module path>" reloads one script in the presets and MOs using it,
	///                           "ReloadScripts = 1" reloads every script, and "Run = <path>" runs a Lua file (the tool's console).
	///                           Handled each time the file is written again.
	class ModStudioLink {

	public:
		/// Turns the link on (the -modstudio launch argument).
		static void Enable() { s_Enabled = true; }

		/// Gets whether the link is on.
		/// @return Whether the game was started with -modstudio.
		static bool IsEnabled() { return s_Enabled; }

		/// Called for every line the console prints: with the link on, the line also goes to ModStudio/Console.log.
		/// @param line The line printed.
		static void OnConsoleLine(const std::string& line);

		/// Called once a frame: flushes the console log and carries out the tool's requests. Does nothing with the link off.
		static void Update();

	private:
		/// Carries out ModStudio/Scripts.ini when it was written since it was last read.
		static void HandleScriptRequests();

		static bool s_Enabled; //!< Whether the game was started with -modstudio.
		static std::chrono::steady_clock::time_point s_LastUpdate; //!< When Update last did its work.
		static bool s_ScriptsLookedAt; //!< Whether Scripts.ini has been looked at yet. A request left from before the game started is skipped.
		static std::filesystem::file_time_type s_ScriptsWriteTime; //!< When the last Scripts.ini handled was written.
	};
} // namespace RTE
