#include "ModStudioPreview.h"

#include "ActivityMan.h"
#include "AHuman.h"
#include "CameraMan.h"
#include "ConsoleMan.h"
#include "FrameMan.h"
#include "GameActivity.h"
#include "HeldDevice.h"
#include "MovableMan.h"
#include "PresetMan.h"
#include "Sandbox.h"
#include "SceneMan.h"
#include "System.h"

#include <fstream>

using namespace RTE;

bool ModStudioPreview::s_InGame = false;
std::chrono::steady_clock::time_point ModStudioPreview::s_LastCheck = {};
std::filesystem::file_time_type ModStudioPreview::s_LastWriteTime = {};

namespace {
	std::string TrimSpaces(const std::string& text) {
		const size_t first = text.find_first_not_of(" \t\r\n");
		return first == std::string::npos ? std::string() : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
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

	// In other games (Test In Game.bat's plain map, or any other) the new unit takes the place of the one the player is in (the last one from Studio, usually), or drops in where the player is looking.
	const int player = Players::PlayerOne;
	const int team = game->GetTeamOfPlayer(player);
	Actor* current = game->GetControlledActor(player);
	const bool replacing = current && g_MovableMan.ValidMO(current);
	Vector position = replacing ? current->GetPos() : g_SceneMan.MovePointToGround(g_CameraMan.GetScrollTarget(player), 30, 2);
	unit->SetPos(position);
	unit->SetTeam(team);
	unit->SetControllerMode(Controller::CIM_PLAYER, player);
	if (replacing) {
		current->SetToDelete(true);
	}
	g_MovableMan.AddActor(unit);
	if (game->SwitchToActor(unit, player, team)) {
		game->SetViewState(Activity::ViewState::Normal, player);
	}
	g_FrameMan.SetScreenText(shown, 0, 0, 4000);
}
