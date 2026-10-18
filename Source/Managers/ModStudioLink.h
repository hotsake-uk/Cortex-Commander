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
	///   ModStudio/Watch.ini     The values scripts show with StudioWatch(name, value), as "name = value" lines.
	///   ModStudio/Profile.ini   Twice a second in an activity: frame, update and draw times, object counts, the performance counters
	///                           ("Stage = <name>\t<ms>") and each script's time and calls in a sim update ("Script = <path>\t<ms>\t<calls>").
	///
	///   ModStudio/Inspect.ini   From the tool, rewritten every second or two while its inspector is open: "Selected = <unique ID>" (outlined
	///                           in the world), "Follow = <unique ID>" (the camera follows it), "Pause = 1" (holds the sim still) and
	///                           "Step = <count>" (each increase lets that many sim updates through while held). Once it's more than five
	///                           seconds old, the inspector lets go: a tool that closes doesn't leave the game held.
	///   ModStudio/World.ini     Four times a second while the inspector is open: whether the sim is paused, the actors (one line each), and
	///                           the selected actor's values, scripts, inventory and parts. It ends with "End = 1".
	///
	/// The files the game writes are written to a temporary file first and swapped in, so the tool never reads half of one.
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

		/// Called after each sim update's MovableMan update: adds up the scripts' time for Profile.ini (written twice a second), and outlines
		/// the actor selected in the inspector.
		static void UpdateAfterSim();

		/// StudioWatch(name, value) in Lua: shows a value in the tool's Watch panel. Any thread can call it.
		/// @param name The value's name.
		/// @param value The value, as text.
		static void Watch(const std::string& name, const std::string& value);

	private:
		/// Carries out ModStudio/Scripts.ini when it was written since it was last read.
		static void HandleScriptRequests();

		/// Reads ModStudio/Inspect.ini, holds or steps the sim as it asks, and writes ModStudio/World.ini.
		static void HandleInspector();

		static bool s_Enabled; //!< Whether the game was started with -modstudio.
		static std::chrono::steady_clock::time_point s_LastUpdate; //!< When Update last did its work.
		static bool s_ScriptsLookedAt; //!< Whether Scripts.ini has been looked at yet. A request left from before the game started is skipped.
		static std::filesystem::file_time_type s_ScriptsWriteTime; //!< When the last Scripts.ini handled was written.
		static std::chrono::steady_clock::time_point s_LastProfileWrite; //!< When Profile.ini was last written.
		static bool s_Inspecting; //!< Whether the tool's inspector is open (Inspect.ini is fresh).
		static long s_InspectSelected; //!< The unique ID of the actor selected in the inspector, 0 for none.
		static long s_InspectFollow; //!< The unique ID of the actor the camera follows, 0 for none.
		static long s_InspectSteps; //!< The last step count read from Inspect.ini, -1 before one is read.
	};
} // namespace RTE
