#include "ModStudioLink.h"

#include "ActivityMan.h"
#include "AudioMan.h"
#include "ConsoleMan.h"
#include "ContentFile.h"
#include "GAScripted.h"
#include "LuaMan.h"
#include "MovableMan.h"
#include "PerformanceMan.h"
#include "PresetMan.h"
#include "System.h"

#include <algorithm>
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
	/// Where the swap can't be made (the tool has the file open just then, on Windows) it is written in place.
	class LinkFileOut : public std::ostringstream {
	public:
		explicit LinkFileOut(std::filesystem::path path) :
		    m_Path(std::move(path)) {}

		~LinkFileOut() override {
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
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (!s_Enabled || now - s_LastUpdate < std::chrono::milliseconds(250)) {
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

void ModStudioLink::UpdateProfile() {
	if (!s_Enabled || !g_ActivityMan.IsInActivity()) {
		return;
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
