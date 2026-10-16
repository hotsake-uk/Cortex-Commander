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

		/// In a running game started with -modstudio, makes each new unit from Studio your sandbox character (or the player's unit). Called every frame.
		static void UpdateInGame();

	private:
		static bool s_InGame; //!< Whether games swap in Studio's units.
		static std::chrono::steady_clock::time_point s_LastCheck; //!< When the request file was last looked at. (Not a Timer: a static one would need TimerMan before it exists.)
		static std::filesystem::file_time_type s_LastWriteTime; //!< When the last request handled in game was written.
		static long s_LastUnitID; //!< The unique ID of the last unit put in a game outside the sandbox, which the next one replaces.
	};
} // namespace RTE
