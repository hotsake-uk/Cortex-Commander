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
#include "Attachable.h"
#include "ACrab.h"
#include "AEJetpack.h"
#include "AudioMan.h"
#include "PrimitiveMan.h"
#include "TimerMan.h"
#include "MovableMan.h"
#include "PerformanceMan.h"
#include "PresetMan.h"
#include "RTEError.h"
#include "Sandbox.h"
#include "SceneMan.h"
#include "System.h"

#include <algorithm>
#include <fstream>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>

using namespace RTE;

bool ModStudioPreview::s_InGame = false;
std::chrono::steady_clock::time_point ModStudioPreview::s_LastCheck = {};
long ModStudioPreview::s_LastUnitID = 0;
std::filesystem::file_time_type ModStudioPreview::s_LastWriteTime = {};
std::chrono::steady_clock::time_point ModStudioPreview::s_LastScriptCheck = {};
std::filesystem::file_time_type ModStudioPreview::s_ScriptsWriteTime = {};
bool ModStudioPreview::s_ScriptsLookedAt = false;
std::chrono::steady_clock::time_point ModStudioPreview::s_LastProfileWrite = {};
std::chrono::steady_clock::time_point ModStudioPreview::s_LastInspect = {};
bool ModStudioPreview::s_Inspecting = false;
long ModStudioPreview::s_InspectSelected = 0;
bool ModStudioPreview::s_InspectPaused = false;
long ModStudioPreview::s_InspectSteps = -1;
ModStudioPreview::Request ModStudioPreview::s_LastRequest = {};

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
	struct StudioLiveValues {
		struct Probe {
			unsigned long long Count = 0; //!< How many times the probe ran.
			std::deque<std::string> Recent; //!< Its last values, newest last.
		};
		std::mutex Mutex;
		std::map<std::string, Probe> Probes; //!< By key, "<script path>|<line>:<column>".
		bool Changed = false; //!< Whether a probe ran since Live.ini was last written.
	};
	StudioLiveValues& LiveValues() {
		static StudioLiveValues* live = new StudioLiveValues();
		return *live;
	}

	std::string OneLine(std::string text) {
		std::replace(text.begin(), text.end(), '\n', ' ');
		return text;
	}

	/// Reloads a file Studio changed. Something wrong in it is caught the way a broken mod is at start-up: one ERROR line on the console (which Studio
	/// shows), and the game carries on with what it had, rather than stopping on an assert dialog. Returns whether it was read again.
	bool ReloadFileFromStudio(const std::string& filePath) {
		const std::string moduleName = g_PresetMan.GetModuleNameFromPath(filePath);
		for (const auto& [failedName, failure]: g_PresetMan.GetFailedMods()) {
			if (StringsEqualCaseInsensitive(failedName, moduleName)) {
				g_ConsoleMan.PrintString("ERROR: Mod Studio couldn't reload \"" + filePath + "\": the mod " + failedName + " wasn't loaded when the game started, because: " + OneLine(failure) + " Fix that and restart the game.");
				return false;
			}
		}
		bool reloaded = false;
		std::string failure;
		RTEError::s_LoadingMod = true;
		try {
			reloaded = g_PresetMan.ReloadPresetFile(filePath);
		} catch (const ModLoadFailure& modLoadFailure) {
			failure = modLoadFailure.Description;
		}
		RTEError::s_LoadingMod = false;
		if (!failure.empty()) {
			g_ConsoleMan.PrintString("ERROR: Mod Studio couldn't reload \"" + filePath + "\", so the game keeps what it had before: " + OneLine(failure));
		} else if (!reloaded) {
			g_ConsoleMan.PrintString("WARNING: Mod Studio couldn't reload \"" + filePath + "\". Its mod may not be loaded yet.");
		}
		return reloaded && failure.empty();
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
		ReloadFileFromStudio(fileToReload);
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
	if (!GameRunning()) {
		return;
	}
	Request request;
	if (!Poll(s_LastWriteTime, request)) {
		return;
	}
	Show(request);
}

bool ModStudioPreview::GameRunning() {
	const GameActivity* game = dynamic_cast<GameActivity*>(g_ActivityMan.GetActivity());
	return g_ActivityMan.IsInActivity() && game && game->GetActivityState() == Activity::ActivityState::Running && g_SceneMan.GetScene();
}

bool ModStudioPreview::Show(const Request& request) {
	GameActivity* game = dynamic_cast<GameActivity*>(g_ActivityMan.GetActivity());
	std::string problem;
	Actor* unit = MakeUnit(request, problem);
	if (!unit) {
		g_FrameMan.SetScreenText(problem, 0, 0, 8000);
		return false;
	}
	s_LastRequest = request;
	s_LastRequest.FilesToReload.clear();

	const std::string shown = "Showing " + request.PresetName + " from Mod Studio. Press Preview in game again after a change.";
	// In the sandbox it becomes your own character, so you play it as yourself, with the tools put away.
	if (Sandbox::PlayAs(unit)) {
		g_FrameMan.SetScreenText(shown, 0, 0, 4000);
		return true;
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
	return true;
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

std::string ModStudioPreview::LiveScriptFor(const std::string& filePath, const std::string& fullScriptPath) {
	if (!s_InGame) {
		return "";
	}
	// Studio keeps copies by the module's path (MyMod.rte/...), and objects' scripts come here as full paths (Mods/MyMod.rte/...).
	std::string modulePath = std::filesystem::path(filePath).generic_string();
	if (modulePath.rfind("./", 0) == 0) {
		modulePath.erase(0, 2);
	}
	for (const std::string& top: {System::GetDataDirectory(), System::GetModDirectory(), System::GetUserdataDirectory()}) {
		if (modulePath.rfind(top, 0) == 0) {
			modulePath.erase(0, top.size());
			break;
		}
	}
	const std::filesystem::path copy = StudioFolder() / "Live" / std::filesystem::path(modulePath);
	std::error_code error;
	if (!std::filesystem::is_regular_file(copy, error) || std::filesystem::file_size(copy, error) == 0 || error) {
		return "";
	}
	// A copy older than the script is from before the script last changed (saved in another editor, say), so the script wins.
	const std::filesystem::file_time_type copyTime = std::filesystem::last_write_time(copy, error);
	const std::filesystem::file_time_type scriptTime = std::filesystem::last_write_time(std::filesystem::path(fullScriptPath), error);
	if (error || copyTime < scriptTime) {
		return "";
	}
	return copy.string();
}

std::string ModStudioPreview::LiveCode(const std::string& scriptPath) {
	const std::string copy = LiveScriptFor(scriptPath, scriptPath);
	if (copy.empty()) {
		return "";
	}
	std::ifstream copyFile(copy, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(copyFile)), std::istreambuf_iterator<char>());
}

void ModStudioPreview::Live(const std::string& key, const std::string& value) {
	StudioLiveValues& live = LiveValues();
	std::scoped_lock<std::mutex> liveLock(live.Mutex);
	StudioLiveValues::Probe& probe = live.Probes[key];
	probe.Count++;
	if (probe.Recent.empty() || probe.Recent.back() != value) {
		probe.Recent.push_back(value);
		if (probe.Recent.size() > 8) {
			probe.Recent.pop_front();
		}
	}
	live.Changed = true;
}

void ModStudioPreview::UpdateProfile() {
	if (!s_InGame) {
		return;
	}
	UpdateInspectorHighlight();
	struct ScriptTotal {
		long long Microseconds = 0;
		long long Calls = 0;
	};
	static std::unordered_map<std::string, ScriptTotal> s_Scripts;
	static int s_Updates = 0;
	if (!PerformanceMan::LiveTimingsOn()) {
		PerformanceMan::SetLiveTimings(true); // the named timings, for the profiler's list of where the time goes
	}
	// The last sim update's script timings (they're cleared just before the next MovableMan update).
	for (const auto& [path, timing]: g_LuaMan.GetScriptTimings()) {
		ScriptTotal& total = s_Scripts[path];
		total.Microseconds += timing.m_Time;
		total.Calls += timing.m_CallCount;
	}
	++s_Updates;

	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (now - s_LastProfileWrite < std::chrono::milliseconds(500)) {
		return;
	}
	s_LastProfileWrite = now;
	std::vector<std::pair<std::string, ScriptTotal>> scripts(s_Scripts.begin(), s_Scripts.end());
	std::sort(scripts.begin(), scripts.end(), [](const auto& left, const auto& right) { return left.second.Microseconds > right.second.Microseconds; });
	const double updates = static_cast<double>(std::max(s_Updates, 1));
	int virtualChannels = 0;
	int realChannels = 0;
	g_AudioMan.GetPlayingChannelCount(&virtualChannels, &realChannels);

	std::error_code folderError;
	std::filesystem::create_directories(StudioFolder(), folderError);
	std::ofstream profile(StudioFolder() / "Profile.ini", std::ios::trunc);
	profile << "// Written by the game for Cortex Mod Studio's profiler twice a second. Times are milliseconds; scripts' are a sim update.\n";
	profile << "Frame = " << g_PerformanceMan.GetMSPFAverage() << '\n';
	profile << "SimUpdate = " << g_PerformanceMan.GetMSPSUAverage() << '\n';
	profile << "Update = " << g_PerformanceMan.GetMSPUAverage() << '\n';
	profile << "Draw = " << g_PerformanceMan.GetMSPDAverage() << '\n';
	profile << "SimUpdates = " << s_Updates << '\n';
	profile << "Actors = " << g_MovableMan.GetActorCount() << '\n';
	profile << "Particles = " << g_MovableMan.GetParticleCount() << '\n';
	profile << "MOIDs = " << g_MovableMan.GetMOIDCount() << '\n';
	profile << "SoundChannels = " << realChannels << '\n';
	for (const auto& [name, ms]: PerformanceMan::GetLiveTimings()) {
		profile << "Stage = " << name << '\t' << ms << '\n';
	}
	int written = 0;
	for (const auto& [path, total]: scripts) {
		if (++written > 40) {
			break;
		}
		profile << "Script = " << path << '\t' << static_cast<double>(total.Microseconds) / 1000.0 / updates << '\t' << static_cast<double>(total.Calls) / updates << '\n';
	}
	s_Scripts.clear();
	s_Updates = 0;
}

namespace {
	// A value for World.ini: no tabs or line breaks.
	std::string Clean(std::string text) {
		std::replace_if(text.begin(), text.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
		return text;
	}

	Actor* FindActor(long uniqueID) {
		for (Actor* actor: g_MovableMan.GetActorList()) {
			if (actor && actor->GetUniqueID() == uniqueID) {
				return actor;
			}
		}
		return nullptr;
	}

	const char* MovementName(int state) {
		static const char* names[] = {"Still", "Crouching", "Standing", "Walking", "Running", "Jumping", "Dislodged", "Prone", "Crawling", "Arm crawling", "Climbing", "Walking crouched"};
		return state >= 0 && state < static_cast<int>(std::size(names)) ? names[state] : "?";
	}

	const char* StatusName(int status) {
		switch (status) {
			case Actor::STABLE: return "Stable";
			case Actor::UNSTABLE: return "Unstable";
			case Actor::INACTIVE: return "Inactive";
			case Actor::DYING: return "Dying";
			case Actor::DEAD: return "Dead";
			default: return "?";
		}
	}
} // namespace

void ModStudioPreview::UpdateInspectorHighlight() {
	if (!s_InGame || !s_Inspecting || s_InspectSelected == 0) {
		return;
	}
	if (const Actor* actor = FindActor(s_InspectSelected)) {
		const float radius = std::max(8.0F, actor->GetRadius());
		g_PrimitiveMan.DrawCirclePrimitive(actor->GetPos(), static_cast<int>(radius) + 3, g_YellowGlowColor);
		g_PrimitiveMan.DrawCirclePrimitive(actor->GetPos(), static_cast<int>(radius) + 4, g_YellowGlowColor);
		g_PrimitiveMan.DrawTextPrimitive(actor->GetPos() + Vector(0, -radius - 18), actor->GetPresetName(), true, 1);
	}
}

void ModStudioPreview::UpdateFrame() {
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (!s_InGame || now - s_LastInspect < std::chrono::milliseconds(250)) {
		return;
	}
	s_LastInspect = now;

	// The request: kept fresh by Studio while its Inspector is open, so a closed Studio stops the inspector (and lets go of the pause).
	const std::filesystem::path requestPath = StudioFolder() / "Inspect.ini";
	std::error_code error;
	const std::filesystem::file_time_type written = std::filesystem::last_write_time(requestPath, error);
	const bool fresh = !error && std::filesystem::file_time_type::clock::now() - written < std::chrono::seconds(5);
	bool pause = false;
	long steps = s_InspectSteps;
	if (fresh) {
		std::ifstream request(requestPath);
		std::string line;
		while (std::getline(request, line)) {
			const size_t equals = line.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			std::string key = line.substr(0, equals);
			std::string value = line.substr(equals + 1);
			key.erase(key.find_last_not_of(" \t\r") + 1);
			value.erase(0, value.find_first_not_of(" \t"));
			value.erase(value.find_last_not_of(" \t\r") + 1);
			try {
				if (key == "Selected") {
					s_InspectSelected = std::stol(value);
				} else if (key == "Pause") {
					pause = value == "1";
				} else if (key == "Step") {
					steps = std::stol(value);
				}
			} catch (const std::exception&) {
				// a half-written line: next time
			}
		}
	}
	s_Inspecting = fresh;
	if (pause != s_InspectPaused) {
		s_InspectPaused = pause;
		g_TimerMan.PauseSim(pause, TimerMan::SimPauseStudio);
	}
	if (s_InspectSteps >= 0 && steps > s_InspectSteps && g_TimerMan.IsSimPaused()) {
		g_TimerMan.StepSim(static_cast<int>(std::min(steps - s_InspectSteps, 60L)));
	}
	s_InspectSteps = steps;
	if (g_TimerMan.IsSimPaused()) {
		UpdateScripts(); // Studio's Lua (the inspector's changes, the console) runs while the world is held still, by Studio or the game
	}
	if (!s_Inspecting || !GameRunning()) {
		return;
	}

	std::ofstream world(StudioFolder() / "World.ini", std::ios::trunc);
	world << "// Written by the game for Cortex Mod Studio's inspector four times a second.\n";
	world << "Paused = " << (g_TimerMan.IsSimPaused() ? 1 : 0) << '\n';
	world << "PausedByStudio = " << (s_InspectPaused ? 1 : 0) << '\n';
	const Activity* activity = g_ActivityMan.GetActivity();
	Actor* controlled = activity ? const_cast<Activity*>(activity)->GetControlledActor(0) : nullptr;
	world << "Controlled = " << (controlled ? controlled->GetUniqueID() : 0) << '\n';
	// One line an actor: id, preset, class, team, x, y, health, max health, status.
	for (const Actor* actor: g_MovableMan.GetActorList()) {
		if (!actor) {
			continue;
		}
		world << "Actor = " << actor->GetUniqueID() << '\t' << Clean(actor->GetPresetName()) << '\t' << actor->GetClassName() << '\t' << actor->GetTeam() << '\t'
		      << actor->GetPos().GetX() << '\t' << actor->GetPos().GetY() << '\t' << actor->GetHealth() << '\t' << actor->GetMaxHealth() << '\t' << StatusName(actor->GetStatus()) << '\n';
	}
	const Actor* actor = s_InspectSelected ? FindActor(s_InspectSelected) : nullptr;
	if (!actor) {
		return;
	}
	// The selected actor's values: "Value = name<TAB>value", in the order Studio shows them.
	auto value = [&world](const std::string& name, const std::string& text) { world << "Value = " << name << '\t' << Clean(text) << '\n'; };
	auto number = [](float v) {
		std::ostringstream out;
		out << std::round(v * 100.0F) / 100.0F;
		return out.str();
	};
	auto vec = [&number](const Vector& v) { return "(" + number(v.GetX()) + ", " + number(v.GetY()) + ")"; };
	world << "Selected = " << actor->GetUniqueID() << '\n';
	value("Preset", actor->GetPresetName());
	value("Class", actor->GetClassName());
	value("Module", actor->GetModuleName());
	value("Team", std::to_string(actor->GetTeam()));
	value("Health", number(actor->GetHealth()) + " / " + number(actor->GetMaxHealth()));
	value("Status", StatusName(actor->GetStatus()));
	value("Position", vec(actor->GetPos()));
	value("Velocity", vec(actor->GetVel()) + "  speed " + number(actor->GetVel().GetMagnitude()));
	value("Rotation", number(actor->GetRotAngle() * 180.0F / c_PI) + "°  turning " + number(actor->GetAngularVel()));
	value("Mass", number(actor->GetMass()) + " kg");
	value("Wounds", std::to_string(actor->GetWoundCount()) + " of " + std::to_string(actor->GetGibWoundLimit()) + " to gib");
	value("Gold carried", number(actor->GetGoldCarried()));
	value("AI mode", std::to_string(actor->GetAIMode()));
	value("Player controlled", actor->IsPlayerControlled() ? "yes" : "no");
	value("Moving", MovementName(actor->GetMovementState()));
	value("Aim", number(actor->GetAimAngle(false) * 180.0F / c_PI) + "°");
	value("Age", number(static_cast<float>(actor->GetAge()) / 1000.0F) + " s");
	const AEJetpack* jetpack = nullptr;
	if (const AHuman* human = dynamic_cast<const AHuman*>(actor)) {
		value("Holding", human->GetEquippedItem() ? human->GetEquippedItem()->GetPresetName() : "nothing");
		jetpack = human->GetJetpack();
	} else if (const ACrab* crab = dynamic_cast<const ACrab*>(actor)) {
		jetpack = crab->GetJetpack();
	}
	if (jetpack && jetpack->GetJetTimeTotal() > 0) {
		value("Jetpack fuel", number(100.0F * jetpack->GetJetTimeLeft() / jetpack->GetJetTimeTotal()) + "%");
	}
	for (const std::string& script: actor->GetLoadedScripts()) {
		world << "Script = " << Clean(script) << '\n';
	}
	for (const MovableObject* item: *actor->GetInventory()) {
		if (item) {
			world << "Inventory = " << Clean(item->GetPresetName()) << '\n';
		}
	}
	// Its parts: preset, class, wounds and the wounds that gib it.
	for (const Attachable* part: actor->GetAttachableList()) {
		if (part) {
			world << "Part = " << Clean(part->GetPresetName()) << '\t' << part->GetClassName() << '\t' << part->GetWoundCount() << '\t' << part->GetGibWoundLimit() << '\n';
		}
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
	{
		// One line per probe: "<key> = <count>\t<oldest value>\t...\t<newest value>". The probes' text has no tabs or line breaks.
		StudioLiveValues& live = LiveValues();
		std::scoped_lock<std::mutex> liveLock(live.Mutex);
		if (live.Changed) {
			live.Changed = false;
			std::ofstream liveFile(StudioFolder() / "Live.ini", std::ios::trunc);
			for (const auto& [key, probe]: live.Probes) {
				liveFile << key << " = " << probe.Count;
				for (const std::string& value: probe.Recent) {
					liveFile << '\t' << value;
				}
				liveFile << '\n';
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
	std::vector<std::string> filesToReload;
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
		} else if (key == "ReloadFile" && !value.empty()) {
			filesToReload.push_back(value);
		} else if (key == "Run") {
			runFile = value;
		}
	}

	if (!filesToReload.empty() && g_ActivityMan.IsInActivity()) {
		// INI files saved outside Studio (Studio's file bridge watches the mods): their presets are read again, and the last unit from Studio
		// comes back with the change when the file is in its mod (or its held item's).
		bool lastUnitsMod = false;
		for (const std::string& file: filesToReload) {
			if (ReloadFileFromStudio(file)) {
				g_ConsoleMan.PrintString("SYSTEM: Reloaded " + file + " from disk.");
				const std::string module = file.substr(0, file.find('/'));
				lastUnitsMod = lastUnitsMod || module == s_LastRequest.ModuleName || module == s_LastRequest.DeviceModule;
			}
		}
		if (lastUnitsMod && !s_LastRequest.PresetName.empty() && GameRunning()) {
			Show(s_LastRequest);
		}
	}
	if (!scriptsToReload.empty()) {
		// A reloaded script's live values start afresh: its probes may have moved, and the old counts were for the old code.
		StudioLiveValues& live = LiveValues();
		std::scoped_lock<std::mutex> liveLock(live.Mutex);
		for (const std::string& scriptPath: scriptsToReload) {
			const std::string prefix = scriptPath + "|";
			for (auto probe = live.Probes.lower_bound(prefix); probe != live.Probes.end() && probe->first.compare(0, prefix.size(), prefix) == 0;) {
				probe = live.Probes.erase(probe);
				live.Changed = true;
			}
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
