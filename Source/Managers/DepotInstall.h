#pragma once

#include <functional>
#include <string>
#include <vector>

namespace RTE {

	/// Installs, updates and removes what's got from Cortex Depot (DepotInstall.cpp). Modules can't be swapped while the game runs, so
	/// nothing changes at once: a download is checked and unpacked into Userdata/Depot/Staging, and what to do with it is written to
	/// Userdata/Depot/Pending.json, which the next start carries out before any module loads. What the Depot installed is kept in
	/// Userdata/Depot/Installed.json, so updates can be found and removing takes away only what came with the item.
	class DepotInstall {

	public:
		/// An item from the Depot, as installed.
		struct Entry {
			std::string Slug;
			std::string Kind;
			std::string Title;
			std::string Version;
			std::string InstallTo; //!< "mods" for modules (.rte folders in the mods folder), "battlePresets" for battle setups.
			std::vector<std::string> Modules; //!< The .rte folders it put in the mods folder.
			std::string Map; //!< For battle setups: the map they're for.
			std::vector<std::string> Setups; //!< For battle setups: the names they were added to the map's setups under.
			bool Adopted = false; //!< Installed by hand and taken on later, so its version is the Depot's guess.
		};

		/// What an install or update the player asked for is doing, or why it stopped.
		struct Task {
			std::string Slug;
			std::string Title;
			bool Running = false;
			std::string Doing; //!< What it's on now, in words for the player.
			float Progress = -1.0F; //!< How far the download in hand has got, 0 to 1, or -1 when there's none or it can't tell.
			std::string Error; //!< Why it stopped, when it did.
			std::string Warning; //!< Something the player should know though it went ahead, e.g. a module it needs that nothing provides.
		};

		/// A newer version of something installed.
		struct Update {
			std::string Slug;
			std::string Title;
			std::string Installed;
			std::string Latest;
			std::string Changelog;
			long long FileSize = 0;
		};

		/// A module installed by hand that's on the Depot, so the Depot could look after it.
		struct Match {
			std::string ModuleID;
			std::string Slug;
			std::string Title;
			std::string Version;
		};

#pragma region Start-up
		/// Carries out the installs and removals waiting from the last time the game ran. Call before the modules load.
		/// @param report Told about each change, for the loading screen.
		static void ApplyPending(const std::function<void(const std::string&)>& report);

		/// Starts the game again if the player asked to restart to apply changes. Call last thing before the game exits.
		/// @param argv0 The game's own argv[0].
		static void RelaunchIfWanted(const char* argv0);
#pragma endregion

#pragma region Installed
		/// The item installed from the Depot under a slug, or nullptr.
		static const Entry* Installed(const std::string& slug);

		/// Everything installed from the Depot.
		static const std::vector<Entry>& AllInstalled();

		/// The Depot item that installed a module, or nullptr when it came some other way.
		static const Entry* InstalledModule(const std::string& moduleID);

		/// What's waiting for a restart for an item: "install" (or update), "remove", or empty for nothing.
		/// @param version Set to the version an install waiting will put in.
		static std::string PendingFor(const std::string& slug, std::string* version = nullptr);

		/// How many changes are waiting for a restart.
		static int PendingCount();
#pragma endregion

#pragma region Changes
		/// Downloads an item, and what it needs that isn't here, to be installed at the next start. Replaces an older version installed.
		/// @param version The version to install, or empty for the latest.
		static void Install(const std::string& slug, const std::string& version = "");

		/// Has an item taken away at the next start: its modules, or the battle setups it added.
		static void Remove(const std::string& slug);

		/// Drops what's waiting for an item, so the next start leaves it as it is.
		static void Undo(const std::string& slug);

		/// Takes on a module installed by hand as the Depot item it matches, so updates are found for it.
		static void Adopt(const Match& match);

		/// The install for an item running or last stopped with an error, or nullptr.
		static const Task* TaskFor(const std::string& slug);

		/// Forgets a stopped install's error.
		static void Dismiss(const std::string& slug);

		/// Asks for a restart: the game quits, and starts again (applying what's waiting) as it exits.
		static void RestartToApply();
#pragma endregion

#pragma region Updates
		/// Asks the Depot what's newer than what's installed, and which hand-installed modules it knows.
		static void CheckForUpdates();

		/// Whether the check is under way.
		static bool CheckingForUpdates();

		/// What the last check found newer, without what's already waiting to be installed at that version.
		static std::vector<Update> Updates();

		/// The hand-installed modules the last check found on the Depot.
		static const std::vector<Match>& Matches();

		/// Why the last check failed, or empty.
		static const std::string& UpdateError();
#pragma endregion
	};
} // namespace RTE
