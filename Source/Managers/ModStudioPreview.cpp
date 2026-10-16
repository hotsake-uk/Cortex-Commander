#include "ModStudioPreview.h"

#include "ActivityMan.h"
#include "AHuman.h"
#include "CameraMan.h"
#include "ConsoleMan.h"
#include "FrameMan.h"
#include "GameActivity.h"
#include "GAScripted.h"
#include "HeldDevice.h"
#include "LuaMan.h"
#include "MovableMan.h"
#include "PresetMan.h"
#include "Sandbox.h"
#include "SceneMan.h"
#include "System.h"

#include <fstream>
#include <map>
#include <mutex>

using namespace RTE;

bool ModStudioPreview::s_InGame = false;
std::chrono::steady_clock::time_point ModStudioPreview::s_LastCheck = {};
long ModStudioPreview::s_LastUnitID = 0;
std::filesystem::file_time_type ModStudioPreview::s_LastWriteTime = {};
std::chrono::steady_clock::time_point ModStudioPreview::s_LastScriptCheck = {};
std::filesystem::file_time_type ModStudioPreview::s_ScriptsWriteTime = {};
bool ModStudioPreview::s_ScriptsLookedAt = false;

namespace {
	std::string TrimSpaces(const std::string& text) {
		const size_t first = text.find_first_not_of(" \t\r\n");
		return first == std::string::npos ? std::string() : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
	}

	std::filesystem::path StudioFolder() {
		return std::filesystem::path(System::GetWorkingDirectory()) / "ModStudio";
	}

	// The console log and the watched values. Any thread can print or watch, so each has a lock. They are made on first use and never freed:
	// the console can still print while the program shuts down, after ordinary statics are gone.
	struct StudioLog {
		std::mutex Mutex;
		std::ofstream File; //!< ModStudio/Console.log, opened (emptied) on the first line printed.
		bool Unflushed = false; //!< Whether lines were added since it was last flushed.
	};
	StudioLog& Log() {
		static StudioLog* log = new StudioLog();
		return *log;
	}
	struct StudioWatched {
		std::mutex Mutex;
		std::map<std::string, std::string> Values; //!< The values scripts watch, by name.
		bool Changed = false; //!< Whether one changed since Watch.ini was last written.
	};
	StudioWatched& Watched() {
		static StudioWatched* watched = new StudioWatched();
		return *watched;
	}
} // namespace

bool ModStudioPreview::Poll(std::filesystem::file_time_type& lastWriteTime, Request& request) {
	const std::filesystem::path requestPath = std::filesystem::path(System::GetWorkingDirectory()) / "ModStudio" / "Preview.ini";
	std::error_code error;
	const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(requestPath, error);
	if (error || writeTime == lastWriteTime) {
		return false;
	}
	std::ifstream requestFile(requestPath);
	if (!requestFile.good()) {
		return false; // Still being written, most likely; try again next time.
	}
	lastWriteTime = writeTime;

	request = Request();
	std::string line;
	while (std::getline(requestFile, line)) {
		if (const size_t commentStart = line.find("//"); commentStart != std::string::npos) {
			line.erase(commentStart);
		}
		const size_t equalsPos = line.find('=');
		if (equalsPos == std::string::npos) {
			continue;
		}
		const std::string key = TrimSpaces(line.substr(0, equalsPos));
		const std::string value = TrimSpaces(line.substr(equalsPos + 1));
		if (key == "Class") {
			request.ClassName = value;
		} else if (key == "Preset") {
			request.PresetName = value;
		} else if (key == "Module") {
			request.ModuleName = value;
		} else if (key == "ReloadFile" && !value.empty()) {
			request.FilesToReload.emplace_back(value);
		} else if (key == "DeviceClass") {
			request.DeviceClass = value;
		} else if (key == "DevicePreset") {
			request.DevicePreset = value;
		} else if (key == "DeviceModule") {
			request.DeviceModule = value;
		}
	}
	return !request.ClassName.empty() && !request.PresetName.empty();
}

Actor* ModStudioPreview::MakeUnit(const Request& request, std::string& problem) {
	if (!request.ModuleName.empty() && g_PresetMan.GetModuleID(request.ModuleName) < 0) {
		// The game only learns about mod folders when it starts.
		problem = request.ModuleName + " is a new mod. Restart the game to load it, then press Preview in game again.";
		return nullptr;
	}
	for (const std::string& fileToReload: request.FilesToReload) {
		if (!g_PresetMan.ReloadPresetFile(fileToReload)) {
			g_ConsoleMan.PrintString("WARNING: Mod Studio preview couldn't reload \"" + fileToReload + "\". Its mod may not be loaded yet.");
		}
	}
	const Entity* preset = g_PresetMan.GetEntityPreset(request.ClassName, request.PresetName, request.ModuleName.empty() ? -1 : g_PresetMan.GetModuleID(request.ModuleName));
	Actor* unit = preset ? dynamic_cast<Actor*>(preset->Clone()) : nullptr;
	if (!unit) {
		problem = "Mod Studio asked for \"" + request.PresetName + "\", but the game can't find it. Check the console for errors.";
		return nullptr;
	}

	// The item picked in Studio goes in the unit's hands; what the unit carries already stays in its inventory.
	if (AHuman* human = dynamic_cast<AHuman*>(unit); human && !request.DevicePreset.empty()) {
		const int deviceModuleID = request.DeviceModule.empty() ? -1 : g_PresetMan.GetModuleID(request.DeviceModule);
		const Entity* devicePreset = g_PresetMan.GetEntityPreset(request.DeviceClass.empty() ? "HDFirearm" : request.DeviceClass, request.DevicePreset, deviceModuleID);
		if (HeldDevice* device = devicePreset ? dynamic_cast<HeldDevice*>(devicePreset->Clone()) : nullptr) {
			human->AddInventoryItem(device);
			human->EquipNamedDevice(request.DevicePreset, true);
		} else {
			g_ConsoleMan.PrintString("WARNING: Mod Studio preview couldn't find the held item \"" + request.DevicePreset + "\".");
		}
	}
	g_ConsoleMan.PrintString("SYSTEM: Showing \"" + request.PresetName + "\" from Mod Studio.");
	return unit;
}

void ModStudioPreview::UpdateInGame() {
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (!s_InGame || now - s_LastCheck < std::chrono::milliseconds(400)) {
		return;
	}
	s_LastCheck = now;
	GameActivity* game = dynamic_cast<GameActivity*>(g_ActivityMan.GetActivity());
	if (!g_ActivityMan.IsInActivity() || !game || game->GetActivityState() != Activity::ActivityState::Running || !g_SceneMan.GetScene()) {
		return;
	}
	Request request;
	if (!Poll(s_LastWriteTime, request)) {
		return;
	}
	std::string problem;
	Actor* unit = MakeUnit(request, problem);
	if (!unit) {
		g_FrameMan.SetScreenText(problem, 0, 0, 8000);
		return;
	}

	const std::string shown = "Showing " + request.PresetName + " from Mod Studio. Press Preview in game again after a change.";
	// In the sandbox it becomes your own character, so you play it as yourself, with the tools put away.
	if (Sandbox::PlayAs(unit)) {
		g_FrameMan.SetScreenText(shown, 0, 0, 4000);
		return;
	}

	// In other games (Test In Game.bat's plain map, or any other) the new unit takes the place of the last one from Studio (or the unit the player
	// is in), or comes down where the player is looking. The old one goes, so there is only ever one.
	const int player = Players::PlayerOne;
	const int team = game->GetTeamOfPlayer(player);
	Actor* previous = s_LastUnitID != 0 ? dynamic_cast<Actor*>(g_MovableMan.FindObjectByUniqueID(s_LastUnitID)) : nullptr;
	if (!previous) {
		Actor* controlled = game->GetControlledActor(player);
		previous = controlled && g_MovableMan.IsActor(controlled) ? controlled : nullptr;
	}
	const Vector position = previous ? previous->GetPos() : g_SceneMan.MovePointToGround(g_CameraMan.GetScrollTarget(player), 30, 2);
	if (previous) {
		if (game->GetControlledActor(player) == previous) {
			game->LoseControlOfActor(player);
		}
		previous->SetToDelete(true);
	}
	unit->SetPos(position);
	unit->SetTeam(team);
	// (Left to the AI until the switch: the game won't switch to a unit that's already player-controlled.)
	unit->SetControllerMode(Controller::CIM_AI);
	g_MovableMan.AddActor(unit);
	s_LastUnitID = unit->GetUniqueID();
	if (game->SwitchToActor(unit, player, team)) {
		game->SetViewState(Activity::ViewState::Normal, player);
	}
	g_FrameMan.SetScreenText(shown, 0, 0, 4000);
}

void ModStudioPreview::OnConsoleLine(const std::string& line) {
	if (!s_InGame) {
		return;
	}
	StudioLog& log = Log();
	std::scoped_lock<std::mutex> logLock(log.Mutex);
	if (!log.File.is_open()) {
		std::error_code error;
		std::filesystem::create_directories(StudioFolder(), error);
		log.File.open(StudioFolder() / "Console.log", std::ios::trunc);
	}
	if (log.File.is_open()) {
		log.File << line << '\n';
		log.Unflushed = true;
	}
}

void ModStudioPreview::Watch(const std::string& name, const std::string& value) {
	StudioWatched& watched = Watched();
	std::scoped_lock<std::mutex> watchLock(watched.Mutex);
	std::string& current = watched.Values[name];
	if (current != value || current.empty()) {
		current = value;
		watched.Changed = true;
	}
}

void ModStudioPreview::UpdateScripts() {
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (!s_InGame || now - s_LastScriptCheck < std::chrono::milliseconds(250)) {
		return;
	}
	s_LastScriptCheck = now;

	// The log is flushed here rather than on every line, so a script that errors every frame doesn't write to the disk every frame.
	{
		StudioLog& log = Log();
		std::scoped_lock<std::mutex> logLock(log.Mutex);
		if (log.Unflushed) {
			log.File.flush();
			log.Unflushed = false;
		}
	}
	{
		StudioWatched& watched = Watched();
		std::scoped_lock<std::mutex> watchLock(watched.Mutex);
		if (watched.Changed) {
			watched.Changed = false;
			std::ofstream watchFile(StudioFolder() / "Watch.ini", std::ios::trunc);
			for (const auto& [name, value]: watched.Values) {
				watchFile << name << " = " << value << '\n';
			}
		}
	}

	const std::filesystem::path requestPath = StudioFolder() / "Scripts.ini";
	std::error_code error;
	std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(requestPath, error);
	if (error) {
		writeTime = std::filesystem::file_time_type();
	}
	if (!s_ScriptsLookedAt) {
		// A request left over from before the game started: the scripts it reloads were just loaded, and the Lua it runs was for that game.
		s_ScriptsLookedAt = true;
		s_ScriptsWriteTime = writeTime;
		return;
	}
	if (error || writeTime == s_ScriptsWriteTime) {
		return;
	}
	std::ifstream requestFile(requestPath);
	if (!requestFile.good()) {
		return;
	}
	s_ScriptsWriteTime = writeTime;

	bool reloadScripts = false;
	std::vector<std::string> scriptsToReload;
	std::string runFile;
	std::string line;
	while (std::getline(requestFile, line)) {
		const size_t equalsPos = line.find('=');
		if (equalsPos == std::string::npos) {
			continue;
		}
		const std::string key = TrimSpaces(line.substr(0, equalsPos));
		const std::string value = TrimSpaces(line.substr(equalsPos + 1));
		if (key == "ReloadScripts") {
			reloadScripts = value == "1";
		} else if (key == "ReloadScript" && !value.empty()) {
			scriptsToReload.push_back(value);
		} else if (key == "Run") {
			runFile = value;
		}
	}

	if (!reloadScripts && !scriptsToReload.empty() && g_ActivityMan.IsInActivity()) {
		// Just the saved scripts: the presets and objects using them reload (running their Create again), and every other object keeps its state.
		// A script no object uses (a library other scripts load, or a module's global script) needs everything reloaded.
		g_LuaMan.ClearUserModuleCache();
		std::list<Entity*> presets;
		g_PresetMan.GetAllOfType(presets, "MovableObject");
		for (const std::string& scriptPath: scriptsToReload) {
			const std::string fullPath = g_PresetMan.GetFullModulePath(scriptPath);
			int users = 0;
			for (Entity* preset: presets) {
				if (MovableObject* presetObject = dynamic_cast<MovableObject*>(preset); presetObject && presetObject->HasScript(fullPath)) {
					presetObject->ReloadScripts();
					++users;
				}
			}
			const int objects = g_MovableMan.ReloadLuaScriptsUsing(fullPath);
			users += objects;
			GAScripted* scriptedActivity = dynamic_cast<GAScripted*>(g_ActivityMan.GetActivity());
			if (scriptedActivity && scriptedActivity->GetScriptPath() == fullPath) {
				scriptedActivity->ReloadScripts();
				++users;
			}
			if (users == 0) {
				reloadScripts = true;
				break;
			}
			g_ConsoleMan.PrintString("SYSTEM: Reloaded " + scriptPath + " (" + std::to_string(objects) + (objects == 1 ? " object" : " objects") + " in the scene).");
		}
	}
	if (reloadScripts && g_ActivityMan.IsInActivity()) {
		g_PresetMan.ReloadAllScripts();
		// The running scripted activity is a copy of its preset, so it reloads on its own. Its state (its variables) stays.
		if (GAScripted* scriptedActivity = dynamic_cast<GAScripted*>(g_ActivityMan.GetActivity())) {
			scriptedActivity->ReloadScripts();
		}
	}
	if (!runFile.empty()) {
		std::ifstream codeFile(std::filesystem::path(System::GetWorkingDirectory()) / runFile);
		const std::string code((std::istreambuf_iterator<char>(codeFile)), std::istreambuf_iterator<char>());
		if (!code.empty()) {
			g_LuaMan.GetMasterScriptState().RunScriptString(code);
		}
	}
}
