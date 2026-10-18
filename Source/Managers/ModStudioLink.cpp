#include "ModStudioLink.h"

#include "System.h"

#include <filesystem>
#include <fstream>
#include <mutex>

using namespace RTE;

bool ModStudioLink::s_Enabled = false;
std::chrono::steady_clock::time_point ModStudioLink::s_LastUpdate = {};

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
	ConsoleLog& log = Log();
	std::scoped_lock<std::mutex> logLock(log.Mutex);
	if (log.Unflushed) {
		log.File.flush();
		log.Unflushed = false;
	}
}
