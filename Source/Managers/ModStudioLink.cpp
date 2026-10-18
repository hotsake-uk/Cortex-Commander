#include "ModStudioLink.h"

#include "ActivityMan.h"
#include "ACrab.h"
#include "AEJetpack.h"
#include "AHuman.h"
#include "AudioMan.h"
#include "CameraMan.h"
#include "ConsoleMan.h"
#include "ContentFile.h"
#include "GAScripted.h"
#include "HeldDevice.h"
#include "LuaMan.h"
#include "MovableMan.h"
#include "PerformanceMan.h"
#include "PresetMan.h"
#include "PrimitiveMan.h"
#include "System.h"
#include "TimerMan.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace RTE;

bool ModStudioLink::s_Enabled = false;
std::chrono::steady_clock::time_point ModStudioLink::s_LastUpdate = {};
bool ModStudioLink::s_ScriptsLookedAt = false;
std::filesystem::file_time_type ModStudioLink::s_ScriptsWriteTime = {};
std::chrono::steady_clock::time_point ModStudioLink::s_LastProfileWrite = {};
bool ModStudioLink::s_Inspecting = false;
long ModStudioLink::s_InspectSelected = 0;
long ModStudioLink::s_InspectFollow = 0;
long ModStudioLink::s_InspectSteps = -1;

namespace {
	std::filesystem::path LinkFolder() {
		return std::filesystem::path(System::GetWorkingDirectory()) / "ModStudio";
	}

	/// The console log. Any thread can print, so it has a lock. It is made on first use and never freed: the console can still print while
	/// the program shuts down, after ordinary statics are gone.
	struct ConsoleLog {
		std::mutex Mutex;
		std::ofstream File; //!< ModStudio/Console.log, opened (emptied) on the first line printed.
		bool Unflushed = false; //!< Whether lines were added since it was last flushed.
	};

	ConsoleLog& Log() {
		static ConsoleLog* log = new ConsoleLog();
		return *log;
	}

	/// A file the tool reads while the game runs, written to a temporary file and swapped in when it's done so the tool never reads half of one.
	/// Where the swap can't be made (the tool has the file open just then, on Windows) it is written in place. With endMarker, the file ends
	/// with "End = 1", so the tool can tell a whole one.
	class LinkFileOut : public std::ostringstream {
	public:
		explicit LinkFileOut(std::filesystem::path path, bool endMarker = false) :
		    m_Path(std::move(path)), m_EndMarker(endMarker) {}

		~LinkFileOut() override {
			if (m_EndMarker) {
				*this << "End = 1\n";
			}
			const std::string text = str();
			std::error_code error;
			std::filesystem::create_directories(m_Path.parent_path(), error);
			std::filesystem::path temp = m_Path;
			temp += ".tmp";
			{
				std::ofstream out(temp, std::ios::binary | std::ios::trunc);
				out << text;
			}
			std::filesystem::rename(temp, m_Path, error);
			if (error) {
				std::ofstream out(m_Path, std::ios::binary | std::ios::trunc);
				out << text;
				std::filesystem::remove(temp, error);
			}
		}

	private:
		std::filesystem::path m_Path;
		bool m_EndMarker;
	};

	/// The values scripts watch. Scripts can run on other threads, so it has a lock; like the log, it is never freed.
	struct WatchedValues {
		std::mutex Mutex;
		std::map<std::string, std::string> Values; //!< The values, by name.
		bool Changed = false; //!< Whether one changed since Watch.ini was last written.
	};

	WatchedValues& Watched() {
		static WatchedValues* watched = new WatchedValues();
		return *watched;
	}

	/// A value for World.ini, which is split on tabs and lines.
	std::string Clean(std::string text) {
		std::replace_if(text.begin(), text.end(), [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
		return text;
	}

	Actor* FindActor(long uniqueID) {
		return uniqueID == 0 ? nullptr : dynamic_cast<Actor*>(g_MovableMan.FindObjectByUniqueID(uniqueID));
	}

	const char* MovementName(int state) {
		static const char* names[] = {"Still", "Crouching", "Standing", "Walking", "Running", "Jumping", "Dislodged", "Prone", "Crawling", "Arm crawling", "Climbing"};
		return state >= 0 && state < static_cast<int>(std::size(names)) ? names[state] : "?";
	}

	const char* StatusName(int status) {
		switch (status) {
			case Actor::STABLE:
				return "Stable";
			case Actor::UNSTABLE:
				return "Unstable";
			case Actor::INACTIVE:
				return "Inactive";
			case Actor::DYING:
				return "Dying";
			case Actor::DEAD:
				return "Dead";
			default:
				return "?";
		}
	}

	std::string TrimSpaces(const std::string& text) {
		const size_t first = text.find_first_not_of(" \t\r\n");
		return first == std::string::npos ? std::string() : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
	}

	/// Reads a file the tool changed again: an image's pixels in place (every sprite using it shows the change), or an INI file's presets
	/// (new copies of them pick up the change, as with ReloadEntityPreset).
	/// @return Whether it was read again.
	bool ReloadChangedFile(const std::string& filePath) {
		const size_t dot = filePath.find_last_of('.');
		std::string extension = dot == std::string::npos ? std::string() : filePath.substr(dot);
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (extension == ".png" || extension == ".bmp") {
			ContentFile::ReloadBitmapFromDisk(filePath);
			return true;
		}
		if (!g_PresetMan.ReloadPresetFile(filePath)) {
			g_ConsoleMan.PrintString("WARNING: Couldn't reload \"" + filePath + "\" for Mod Studio. Its module may not be loaded (new modules load when the game starts).");
			return false;
		}
		return true;
	}
} // namespace

void ModStudioLink::OnConsoleLine(const std::string& line) {
	if (!s_Enabled) {
		return;
	}
	ConsoleLog& log = Log();
	std::scoped_lock<std::mutex> logLock(log.Mutex);
	if (!log.File.is_open()) {
		std::error_code error;
		std::filesystem::create_directories(LinkFolder(), error);
		log.File.open(LinkFolder() / "Console.log", std::ios::trunc);
	}
	if (log.File.is_open()) {
		log.File << line << '\n';
		log.Unflushed = true;
	}
}

void ModStudioLink::Update() {
	if (!s_Enabled) {
		return;
	}
	// The camera stays on the actor the inspector follows, every frame (the activity points it at the controlled actor each sim update, so a
	// one-off move would be undone at once). It lets go when the actor is gone.
	if (s_Inspecting && s_InspectFollow != 0) {
		if (const Actor* followed = FindActor(s_InspectFollow)) {
			g_CameraMan.SetScrollTarget(followed->GetPos(), 0.12F, 0);
		} else {
			s_InspectFollow = 0;
		}
	}
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (now - s_LastUpdate < std::chrono::milliseconds(250)) {
		return;
	}
	s_LastUpdate = now;

	// Flushed here rather than on every line, so a script that errors every frame doesn't write to the disk every frame.
	{
		ConsoleLog& log = Log();
		std::scoped_lock<std::mutex> logLock(log.Mutex);
		if (log.Unflushed) {
			log.File.flush();
			log.Unflushed = false;
		}
	}
	{
		WatchedValues& watched = Watched();
		std::scoped_lock<std::mutex> watchLock(watched.Mutex);
		if (watched.Changed) {
			watched.Changed = false;
			LinkFileOut watchFile(LinkFolder() / "Watch.ini");
			for (const auto& [name, value]: watched.Values) {
				watchFile << name << " = " << value << '\n';
			}
		}
	}
	HandleScriptRequests();
	HandleInspector();
}

Actor* ModStudioLink::TakePreviewRequest(std::filesystem::file_time_type& lastWriteTime) {
	if (!s_Enabled) {
		return nullptr;
	}
	const std::filesystem::path requestPath = LinkFolder() / "Preview.ini";
	std::error_code error;
	const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(requestPath, error);
	if (error || writeTime == lastWriteTime) {
		return nullptr;
	}
	std::ifstream requestFile(requestPath);
	if (!requestFile.good()) {
		return nullptr; // Still being written, most likely; try again next time.
	}
	lastWriteTime = writeTime;

	std::string className;
	std::string presetName;
	std::string moduleName;
	std::string deviceClass;
	std::string devicePreset;
	std::string deviceModule;
	std::vector<std::string> filesToReload;
	std::string line;
	while (std::getline(requestFile, line)) {
		const size_t equalsPos = line.find('=');
		if (equalsPos == std::string::npos) {
			continue;
		}
		const std::string key = TrimSpaces(line.substr(0, equalsPos));
		const std::string value = TrimSpaces(line.substr(equalsPos + 1));
		if (key == "Class") {
			className = value;
		} else if (key == "Preset") {
			presetName = value;
		} else if (key == "Module") {
			moduleName = value;
		} else if (key == "ReloadFile" && !value.empty()) {
			filesToReload.push_back(value);
		} else if (key == "DeviceClass") {
			deviceClass = value;
		} else if (key == "DevicePreset") {
			devicePreset = value;
		} else if (key == "DeviceModule") {
			deviceModule = value;
		}
	}
	if (className.empty() || presetName.empty()) {
		return nullptr;
	}
	if (!moduleName.empty() && g_PresetMan.GetModuleID(moduleName) < 0) {
		g_ConsoleMan.PrintString("WARNING: Mod Studio asked for \"" + presetName + "\" from " + moduleName + ", which isn't loaded. New modules load when the game starts.");
		return nullptr;
	}
	for (const std::string& file: filesToReload) {
		ReloadChangedFile(file);
	}
	const Entity* preset = g_PresetMan.GetEntityPreset(className, presetName, moduleName.empty() ? -1 : g_PresetMan.GetModuleID(moduleName));
	Actor* actor = preset ? dynamic_cast<Actor*>(preset->Clone()) : nullptr;
	if (!actor) {
		g_ConsoleMan.PrintString("WARNING: Mod Studio asked for the " + className + " \"" + presetName + "\", but it can't be found.");
		return nullptr;
	}
	// The item picked in the tool goes in the actor's hands; what it carries already stays in its inventory.
	if (AHuman* human = dynamic_cast<AHuman*>(actor); human && !devicePreset.empty()) {
		const Entity* deviceEntity = g_PresetMan.GetEntityPreset(deviceClass.empty() ? "HDFirearm" : deviceClass, devicePreset, deviceModule.empty() ? -1 : g_PresetMan.GetModuleID(deviceModule));
		if (HeldDevice* device = deviceEntity ? dynamic_cast<HeldDevice*>(deviceEntity->Clone()) : nullptr) {
			human->AddInventoryItem(device);
			human->EquipNamedDevice(devicePreset, true);
		} else {
			g_ConsoleMan.PrintString("WARNING: Mod Studio asked for \"" + devicePreset + "\" to be held, but it can't be found.");
		}
	}
	g_ConsoleMan.PrintString("SYSTEM: Showing \"" + presetName + "\" from Mod Studio.");
	return actor;
}

void ModStudioLink::Watch(const std::string& name, const std::string& value) {
	if (!s_Enabled) {
		return;
	}
	WatchedValues& watched = Watched();
	std::scoped_lock<std::mutex> watchLock(watched.Mutex);
	auto [entry, added] = watched.Values.try_emplace(name, value);
	if (added || entry->second != value) {
		entry->second = value;
		watched.Changed = true;
	}
}

void ModStudioLink::UpdateAfterSim() {
	if (!s_Enabled || !g_ActivityMan.IsInActivity()) {
		return;
	}
	// Primitives are cleared at the start of each sim update, so the outline is drawn again each one.
	if (s_Inspecting) {
		if (const Actor* selected = FindActor(s_InspectSelected)) {
			const float radius = std::max(8.0F, selected->GetRadius());
			g_PrimitiveMan.DrawCirclePrimitive(selected->GetPos(), static_cast<int>(radius) + 3, g_YellowGlowColor);
			g_PrimitiveMan.DrawCirclePrimitive(selected->GetPos(), static_cast<int>(radius) + 4, g_YellowGlowColor);
			g_PrimitiveMan.DrawTextPrimitive(selected->GetPos() + Vector(0, -radius - 18), selected->GetPresetName(), true, 1);
		}
	}
	struct ScriptTotal {
		long long Microseconds = 0;
		long long Calls = 0;
	};
	static std::unordered_map<std::string, ScriptTotal> s_Scripts;
	static int s_Updates = 0;

	// This sim update's script timings (LuaMan clears them before each MovableMan update).
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

	LinkFileOut profile(LinkFolder() / "Profile.ini");
	profile << "// Written by the game twice a second for Cortex Mod Studio's profiler. Times are in milliseconds; scripts' are per sim update.\n";
	profile << "Frame = " << g_PerformanceMan.GetMSPFAverage() << '\n';
	profile << "SimUpdate = " << g_PerformanceMan.GetMSPSUAverage() << '\n';
	profile << "Update = " << g_PerformanceMan.GetMSPUAverage() << '\n';
	profile << "Draw = " << g_PerformanceMan.GetMSPDAverage() << '\n';
	profile << "SimUpdates = " << s_Updates << '\n';
	profile << "Actors = " << g_MovableMan.GetActorCount() << '\n';
	profile << "Particles = " << g_MovableMan.GetParticleCount() << '\n';
	profile << "MOIDs = " << g_MovableMan.GetMOIDCount() << '\n';
	profile << "SoundChannels = " << realChannels << '\n';
	for (int counter = PerformanceMan::SimTotal; counter < PerformanceMan::PerfCounterCount; ++counter) {
		const PerformanceMan::PerformanceCounters performanceCounter = static_cast<PerformanceMan::PerformanceCounters>(counter);
		profile << "Stage = " << PerformanceMan::GetPerformanceCounterName(performanceCounter) << '\t' << g_PerformanceMan.GetPerformanceCounterAverageMS(performanceCounter) << '\n';
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

void ModStudioLink::HandleScriptRequests() {
	const std::filesystem::path requestPath = LinkFolder() / "Scripts.ini";
	std::error_code error;
	std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(requestPath, error);
	if (error) {
		writeTime = std::filesystem::file_time_type();
	}
	if (!s_ScriptsLookedAt) {
		// A request left over from before the game started: the scripts it reloads were just loaded, and the Lua it runs was for another game.
		s_ScriptsLookedAt = true;
		s_ScriptsWriteTime = writeTime;
		return;
	}
	if (error || writeTime == s_ScriptsWriteTime) {
		return;
	}
	std::ifstream requestFile(requestPath);
	if (!requestFile.good()) {
		return; // Still being written, most likely; try again next time.
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

	for (const std::string& file: filesToReload) {
		if (ReloadChangedFile(file)) {
			g_ConsoleMan.PrintString("SYSTEM: Reloaded " + file + " for Mod Studio.");
		}
	}
	if (!reloadScripts && !scriptsToReload.empty() && g_ActivityMan.IsInActivity()) {
		// Just the saved scripts: the presets and MOs using them reload, and every other MO keeps its state. A script nothing uses directly
		// (a library other scripts require, or a global script) needs everything reloaded.
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
			g_ConsoleMan.PrintString("SYSTEM: Reloaded " + scriptPath + " (" + std::to_string(objects) + (objects == 1 ? " MO" : " MOs") + " in the scene) for Mod Studio.");
		}
	}
	if (reloadScripts && g_ActivityMan.IsInActivity()) {
		// As F2 does. The running scripted activity is a copy of its preset, so it reloads on its own; its state stays.
		g_PresetMan.ReloadAllScripts();
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

void ModStudioLink::HandleInspector() {
	// Kept fresh by the tool while its inspector is open, so a tool that closes stops the inspector (and lets go of the hold).
	const std::filesystem::path requestPath = LinkFolder() / "Inspect.ini";
	std::error_code error;
	const std::filesystem::file_time_type written = std::filesystem::last_write_time(requestPath, error);
	const bool fresh = !error && std::filesystem::file_time_type::clock::now() - written < std::chrono::seconds(5);
	bool pause = false;
	long steps = s_InspectSteps;
	if (fresh) {
		std::ifstream requestFile(requestPath);
		std::string line;
		while (std::getline(requestFile, line)) {
			const size_t equalsPos = line.find('=');
			if (equalsPos == std::string::npos) {
				continue;
			}
			const std::string key = TrimSpaces(line.substr(0, equalsPos));
			const std::string value = TrimSpaces(line.substr(equalsPos + 1));
			try {
				if (key == "Selected") {
					s_InspectSelected = std::stol(value);
				} else if (key == "Follow") {
					s_InspectFollow = std::stol(value);
				} else if (key == "Pause") {
					pause = value == "1";
				} else if (key == "Step") {
					steps = std::stol(value);
				}
			} catch (const std::exception&) {
				// A line still being written; it's read again next time.
			}
		}
	}
	s_Inspecting = fresh;
	if (!fresh) {
		s_InspectFollow = 0;
	}
	// The hold is only kept in an activity, so it never stands in the way of the menus.
	const bool hold = fresh && pause && g_ActivityMan.IsInActivity();
	if (hold != g_TimerMan.IsSimHeld()) {
		g_TimerMan.HoldSim(hold);
	}
	if (s_InspectSteps >= 0 && steps > s_InspectSteps && g_TimerMan.IsSimHeld()) {
		g_TimerMan.StepSim(static_cast<int>(std::min(steps - s_InspectSteps, 60L)));
	}
	s_InspectSteps = steps;
	if (!s_Inspecting || !g_ActivityMan.IsInActivity()) {
		return;
	}

	LinkFileOut world(LinkFolder() / "World.ini", true);
	world << "// Written by the game four times a second for Cortex Mod Studio's inspector.\n";
	world << "Paused = " << (g_TimerMan.IsSimPaused() ? 1 : 0) << '\n';
	world << "PausedByStudio = " << (g_TimerMan.IsSimHeld() ? 1 : 0) << '\n';
	Activity* activity = g_ActivityMan.GetActivity();
	const Actor* controlled = activity ? activity->GetControlledActor(0) : nullptr;
	world << "Controlled = " << (controlled ? controlled->GetUniqueID() : 0) << '\n';
	world << "Following = " << s_InspectFollow << '\n';
	world << "Camera = " << g_CameraMan.GetScrollTarget(0).GetX() << '\t' << g_CameraMan.GetScrollTarget(0).GetY() << '\n';
	// One line an actor: unique ID, preset, class, team, x, y, health, max health, status.
	for (const Actor* actor: g_MovableMan.GetActors()) {
		if (actor) {
			world << "Actor = " << actor->GetUniqueID() << '\t' << Clean(actor->GetPresetName()) << '\t' << actor->GetClassName() << '\t' << actor->GetTeam() << '\t'
			      << actor->GetPos().GetX() << '\t' << actor->GetPos().GetY() << '\t' << actor->GetHealth() << '\t' << actor->GetMaxHealth() << '\t' << StatusName(actor->GetStatus()) << '\n';
		}
	}
	const Actor* actor = FindActor(s_InspectSelected);
	if (!actor) {
		return;
	}
	// The selected actor's values, "Value = name<TAB>value", in the order the tool shows them.
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
