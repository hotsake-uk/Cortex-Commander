#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace RTE {

	class Actor;
	class Entity;

	/// Units sent from Cortex Mod Studio ("Preview in game"). Studio saves the unit's files, then writes ModStudio/Preview.ini in the game
	/// folder naming the unit, the files to reload and the item to hold. The Actor Editor shows each new one; a game started with
	/// -modstudio makes it your character in the sandbox (Test In Sandbox.bat), or puts you in it in other games (Test In Game.bat).
	class ModStudioPreview {

	public:
		/// One request from Studio.
		struct Request {
			std::string ClassName; //!< The unit's class, like AHuman.
			std::string PresetName; //!< The unit's preset name.
			std::string ModuleName; //!< The module the unit is in, like MyMod.rte.
			std::vector<std::string> FilesToReload; //!< Module paths of the files Studio saved since the last request.
			std::string DeviceClass; //!< The class of the item to put in the unit's hands, or empty for the unit's own.
			std::string DevicePreset; //!< The preset name of that item.
			std::string DeviceModule; //!< The module of that item.
		};

		/// Reads Studio's request if the file was written since the time given, and moves that time on.
		/// @param lastWriteTime When the last request handled was written. Updated when a new one is read.
		/// @param request Filled in with the new request.
		/// @return Whether there is a new request.
		static bool Poll(std::filesystem::file_time_type& lastWriteTime, Request& request);

		/// Reloads the request's files and finds the unit, with the item to hold in its hands.
		/// @param request The request.
		/// @param problem Set to a message for the screen when the unit can't be made.
		/// @return A new copy of the unit, owned by the caller, or nullptr.
		static Actor* MakeUnit(const Request& request, std::string& problem);

		/// Makes in-game swapping happen (the -modstudio launch argument).
		static void EnableInGame() { s_InGame = true; }

		/// Whether the game was started with -modstudio: then the camera stays on the unit you play rather than leaning towards where it aims.
		static bool IsInGame() { return s_InGame; }

		/// In a running game started with -modstudio, makes each new unit from Studio your sandbox character (or the player's unit). Called every frame.
		static void UpdateInGame();

		/// In a game started with -modstudio, carries out Studio's script requests (ModStudio/Scripts.ini): reloading scripts after one is saved,
		/// reloading INI files changed on disk (the last unit from Studio comes back with the change), and running Lua typed in Studio's console. Also writes the values scripts watch to ModStudio/Watch.ini. Called every frame.
		static void UpdateScripts();

		/// In a game started with -modstudio, adds up each sim update's Lua time per script and, twice a second, writes ModStudio/Profile.ini for
		/// Mod Studio's profiler: frame, update and draw milliseconds, how many actors, particles and MOIDs there are, the named timings
		/// (PerformanceMan's live timings) and each script's milliseconds and calls a sim update. Call once a sim update, before the script
		/// timings are cleared.
		static void UpdateProfile();

		/// Once a frame in a game started with -modstudio: Mod Studio's inspector. While Studio's Inspector is open it writes ModStudio/Inspect.ini
		/// every second or so (which actor is selected, whether to pause, a step count); this reads it, pauses or steps the simulation, and four
		/// times a second writes ModStudio/World.ini: every actor in a line, and the selected one's values in detail. While paused it also runs
		/// Studio's script requests (UpdateScripts), which otherwise wait for a sim update.
		static void UpdateFrame();

		/// Once a sim update: the inspector's highlight around the selected actor (drawn shapes are cleared each sim update, so it stays while paused).
		static void UpdateInspectorHighlight();

		/// Called when a unit is hit by another side (Actor::NoteHurtBy) while Studio's Inspector is open: remembers who hit whom, with what, for the Inspector's combat log.
		/// @param victim The unit hit.
		/// @param hitor What hit it (a shot, a blade, a body).
		static void RecordHit(const Actor* victim, const MovableObject* hitor);

		/// Called for every line the console prints. In a game started with -modstudio the line also goes to ModStudio/Console.log, where Studio shows
		/// it, and points at the line of any script error.
		/// @param line The line printed.
		static void OnConsoleLine(const std::string& line);

		/// StudioWatch(name, value) in Lua: shows a value in Studio's Watch panel, updated a few times a second.
		/// @param name What the value is called.
		/// @param value The value, as text.
		static void Watch(const std::string& name, const std::string& value);

		/// Studio's live values (its Quokka-style "--?" comments): Studio writes a copy of a script with probes in it to
		/// ModStudio/Live/<script path>, and a game started with -modstudio loads that copy in place of the script while it is newer
		/// than the script and not empty. Errors still name the script's own file and lines, as the copy keeps every line in place.
		/// @param filePath The script's path as the game names it, like MyMod.rte/Scripts/Gun.lua or Mods/MyMod.rte/Scripts/Gun.lua.
		/// @param fullScriptPath The script's full path on disk.
		/// @return The copy's full path, or empty to load the script itself.
		static std::string LiveScriptFor(const std::string& filePath, const std::string& fullScriptPath);

		/// _StudioLiveCode(path) in Lua: the text of the live copy of a script that require is about to load, so required libraries (like
		/// Base.rte/AI/HumanBehaviors.lua) show live values too.
		/// @param scriptPath The file require found, like Data/Base.rte/AI/HumanBehaviors.lua.
		/// @return The live copy's text, or empty to load the script itself.
		static std::string LiveCode(const std::string& scriptPath);

		/// _StudioLive(key, value) in Lua, called by the probes in a live copy: counts the probe's runs and keeps its last values, written
		/// to ModStudio/Live.ini a few times a second for Studio to show beside the line.
		/// @param key The probe: "<script path>|<line>:<column>".
		/// @param value The value, as text.
		static void Live(const std::string& key, const std::string& value);

		/// _StudioClock() in Lua: seconds on a steady clock, for the timing probes ("--?."). Wall time, unlike os.clock, which is the
		/// whole process's CPU time on some systems and so counts other threads' work.
		static double Clock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

	private:
		/// Puts a unit from Studio in the running game: your character in the sandbox, otherwise in place of the last one (or the player's unit).
		/// @param request What to show.
		/// @return Whether it was shown.
		static bool Show(const Request& request);

		/// Whether a game is running that units can be put in.
		static bool GameRunning();

		static Request s_LastRequest; //!< The last unit shown in a game, shown again when one of its files changes on disk (FilesToReload left empty).
		static bool s_InGame; //!< Whether games swap in Studio's units.
		static std::chrono::steady_clock::time_point s_LastCheck; //!< When the request file was last looked at. (Not a Timer: a static one would need TimerMan before it exists.)
		static std::filesystem::file_time_type s_LastWriteTime; //!< When the last request handled in game was written.
		static long s_LastUnitID; //!< The unique ID of the last unit put in a game outside the sandbox, which the next one replaces.

		static std::chrono::steady_clock::time_point s_LastScriptCheck; //!< When Scripts.ini was last looked at.
		static std::filesystem::file_time_type s_ScriptsWriteTime; //!< When the last script request handled was written.
		static bool s_ScriptsLookedAt; //!< Whether Scripts.ini has been looked at yet. A request left from before the game started is skipped.
		static std::chrono::steady_clock::time_point s_LastProfileWrite; //!< When Profile.ini was last written.
		static std::chrono::steady_clock::time_point s_LastInspect; //!< When Inspect.ini was last read and World.ini written.
		static bool s_Inspecting; //!< Whether Studio's Inspector is open (Inspect.ini written in the last few seconds).
		static long s_InspectSelected; //!< The unique ID of the actor the inspector shows in detail, 0 for none.
		static long s_InspectFollow; //!< The unique ID of the actor the camera follows because the inspector asked (a double-click on it), 0 for none.
		static bool s_InspectPaused; //!< Whether the inspector holds the simulation paused.
		static long s_InspectSteps; //!< The step count last seen in Inspect.ini; each one more lets one sim update through.
	};
} // namespace RTE
