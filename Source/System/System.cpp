#include "System.h"

#include "RTETools.h"
#ifdef SYSTEM_MINIZIP
#include <minizip/unzip.h>
#else
#include "unzip.h"
#endif

#include "RTEError.h"

// Convenience macro to not have to write this out.
#define _LINUX_OR_MACOSX_ (__unix__ || (__APPLE__ && __MACH__))

#ifdef _WIN32
#include "Windows.h"
#elif defined _LINUX_OR_MACOSX_
#include <unistd.h>
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <string>
#include <utility>
#include <vector>

using namespace RTE;

bool System::s_Quit = false;
bool System::s_LogToCLI = false;
bool System::s_ExternalModuleValidation = false;
std::string System::s_ThisExePathAndName = "";
std::string System::s_WorkingDirectory = ".";
std::unordered_set<uint64_t> System::s_WorkingTree;
std::vector<std::string> System::s_UnindexedDirectories;
bool System::s_WorkingTreeBuilt = false;
int System::s_WorkingTreeIndexMS = -1;
std::filesystem::file_time_type System::s_ProgramStartTime = std::filesystem::file_time_type::clock::now();
bool System::s_CaseSensitive = true;
const std::string System::s_DataDirectory = "Data/";
const std::string System::s_ScreenshotDirectory = "ScreenShots/";
std::string System::InstanceFile(const std::string& fileName) {
	const char* instance = std::getenv("CCCP_INSTANCE");
	if (!instance || !*instance) {
		return fileName;
	}
	std::string folder = std::string("Instances/") + instance;
	std::error_code ignored;
	std::filesystem::create_directories(folder, ignored);
	return folder + "/" + fileName;
}

bool System::IsUnattendedInstance() {
	const char* unattended = std::getenv("CCCP_UNATTENDED");
	return unattended && *unattended;
}

// CCCP_MODS_DIR names another folder to load mods from (next to the game, without a slash), so tests can try mods out without touching the player's own Mods folder.
const std::string System::s_ModDirectory = std::getenv("CCCP_MODS_DIR") ? std::string(std::getenv("CCCP_MODS_DIR")) + "/" : "Mods/";
const std::string System::s_UserdataDirectory = "Userdata/";
const std::string System::s_ModulePackageExtension = ".rte";
const std::string System::s_ZippedModulePackageExtension = ".zip";
const std::unordered_set<std::string> System::s_SupportedExtensions = {".ini", ".txt", ".lua", ".cfg", ".bmp", ".png", ".jpg", ".jpeg", ".wav", ".ogg", ".mp3", ".flac"};

void System::Initialize(const char* thisExePathAndName) {
	s_ThisExePathAndName = std::filesystem::path(thisExePathAndName).generic_string();

	s_WorkingDirectory = std::filesystem::current_path().generic_string();

#ifdef __APPLE__
	// Get a reference to the main bundle
	CFBundleRef mainBundle = CFBundleGetMainBundle();

	if (!mainBundle) {
		RTEAbort("Could not get a reference to the main App Bundle! This may be due to a missing argv[0] path.")
	}
	// Get the URL of the application bundle
	CFURLRef bundleURL = CFBundleCopyBundleURL(mainBundle);

	if (!bundleURL) {
		RTEAbort("Could not copy App Bundle URL, the bundle does not exist!")
	}

	// Convert the URL to a C string
	char pathBuffer[PATH_MAX];
	if (CFURLGetFileSystemRepresentation(bundleURL, true, (UInt8*)pathBuffer, sizeof(pathBuffer))) {
		// bundlePath now contains the path to the application bundle as a C string
		auto bundlePath = std::filesystem::path(pathBuffer);

		if (std::filesystem::exists(bundlePath) && bundlePath.extension() == ".app") {
			auto workingDirPath = bundlePath.parent_path();
			std::filesystem::current_path(workingDirPath);
			s_WorkingDirectory = workingDirPath.generic_string();
		}

	} else {
		CFRelease(bundleURL);
		RTEAbort("Could not write App Bundle URL to a readable representation! The bundle path may exceed the local PATH_MAX.")
	}
	// Release the CFURL object
	CFRelease(bundleURL);

#endif
	if (s_WorkingDirectory.back() != '/') {
		s_WorkingDirectory.append("/");
	}

	if (!PathExistsCaseSensitive(s_WorkingDirectory + s_ScreenshotDirectory)) {
		MakeDirectory(s_WorkingDirectory + s_ScreenshotDirectory);
	}
	if (!PathExistsCaseSensitive(s_WorkingDirectory + s_ModDirectory)) {
		MakeDirectory(s_WorkingDirectory + s_ModDirectory);
	}
	if (!PathExistsCaseSensitive(s_WorkingDirectory + s_UserdataDirectory)) {
		MakeDirectory(s_WorkingDirectory + s_UserdataDirectory);
	}

#ifdef _WIN32
	// Consider Settings.ini not existing as first time boot, then create quick launch files if they are missing.
	if (!std::filesystem::exists(s_WorkingDirectory + s_UserdataDirectory + "Settings.ini")) {
		std::array<std::pair<const std::string, const std::string>, 7> quickLaunchFiles = {{
		    {"Launch Actor Editor.bat", R"(start "" "Cortex Command.exe" -editor "ActorEditor")"},
		    {"Launch Area Editor.bat", R"(start "" "Cortex Command.exe" -editor "AreaEditor")"},
		    {"Launch Assembly Editor.bat", R"(start "" "Cortex Command.exe" -editor "AssemblyEditor")"},
		    {"Launch Gib Editor.bat", R"(start "" "Cortex Command.exe" -editor "GibEditor")"},
		    {"Launch Scene Editor.bat", R"(start "" "Cortex Command.exe" -editor "SceneEditor")"},
#ifdef TARGET_MACHINE_X86
		    {"Start Dedicated Server x86.bat", R"(start "" "Cortex Command x86.exe" -server 8000)"},
#else
		    {"Start Dedicated Server.bat", R"(start "" "Cortex Command.exe" -server 8000)"},
#endif
		}};
		for (const auto& [fileName, fileContent]: quickLaunchFiles) {
			if (std::filesystem::path filePath = s_WorkingDirectory + fileName; !std::filesystem::exists(filePath)) {
				std::ofstream fileStream(filePath);
				fileStream << fileContent;
				fileStream.close();
			}
		}
	}
#endif
}

bool System::MakeDirectory(const std::string& pathToMake) {
	bool createResult = std::filesystem::create_directory(pathToMake);
	if (createResult) {
		std::filesystem::permissions(pathToMake, std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec | std::filesystem::perms::others_read | std::filesystem::perms::others_exec, std::filesystem::perm_options::add);
	}
	return createResult;
}

namespace {
	/// Whether a directory under the working directory is left out of the case check's index (see System::PathExistsCaseSensitive): what the
	/// game reads its data from never is, but development and user clutter is. Hidden folders (.git and the like), the user's own folders
	/// and the launcher's builds at the top, and anywhere another checkout, worktree or build tree sits.
	bool LeftOutOfCaseIndex(const std::filesystem::path& directory, bool topLevel) {
		std::string name = directory.filename().generic_string();
		if (name.empty() || name.ends_with(".rte")) {
			return false;
		}
		if (name.front() == '.') {
			return true;
		}
		// At the top, only the data and the mods are indexed: they are what the game loads by path, and so what the case check is for.
		// Anything else (userdata, saves, a build tree, a vcpkg or external folder, a copy of the game, whatever a working folder gathers)
		// is asked of the file system as it is. (A list of what to leave out missed whatever wasn't on it: Liam's start was still slow
		// with the fix in.)
		if (topLevel) {
			auto sameName = [](std::string_view a, std::string_view b) {
				return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) { return std::tolower(x) == std::tolower(y); });
			};
			std::string_view data = System::GetDataDirectory();
			std::string_view mods = System::GetModDirectory();
			data = data.substr(0, data.find_last_not_of('/') + 1);
			mods = mods.substr(0, mods.find_last_not_of('/') + 1);
			if (!sameName(name, data) && !sameName(name, mods)) {
				return true;
			}
		}
		std::error_code error;
		return std::filesystem::exists(directory / ".git", error) || std::filesystem::exists(directory / "meson-private", error) || std::filesystem::exists(directory / "CMakeCache.txt", error);
	}
} // namespace

bool System::PathExistsCaseSensitive(const std::string& pathToCheck) {
	// Use Hash for compiler independent hashing.
	// The working directory's file paths, hashed, are walked once and looked up after. (Kept in a vector and searched end to end for every file
	// the game loaded, and walked through everything under the game's folder, a checkout's history, build trees and other clones beside it
	// included, start-up slowed with every file that piled up there: minutes, on a working copy that had been cloned into a few times.)
	if (s_CaseSensitive) {
		// (Data is loaded from more than one thread: the index is built and grown under a lock.)
		static std::mutex workingTreeMutex;
		std::lock_guard<std::mutex> lock(workingTreeMutex);
		if (!s_WorkingTreeBuilt) {
			s_WorkingTreeBuilt = true;
			const auto indexStart = std::chrono::steady_clock::now();
			std::error_code error;
			std::filesystem::recursive_directory_iterator entry(s_WorkingDirectory, std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied, error);
			// Linked folders are followed (a mod can be a link to its own checkout), but not into one already on the way down: a link to a
			// folder above it walked round the loop until the iterator gave up on the path's length, and the index ended there.
			auto isUnder = [](const std::filesystem::path& path, const std::filesystem::path& folder) {
				auto mismatch = std::mismatch(folder.begin(), folder.end(), path.begin(), path.end());
				return mismatch.first == folder.end();
			};
			std::vector<std::pair<std::filesystem::path, std::filesystem::path>> linksDown; // Each link walked into, and where it goes.
			std::error_code rootError;
			linksDown.emplace_back(std::filesystem::path(s_WorkingDirectory), std::filesystem::weakly_canonical(s_WorkingDirectory, rootError));
			for (; !error && entry != std::filesystem::recursive_directory_iterator(); entry.increment(error)) {
				std::string relative = entry->path().generic_string().substr(s_WorkingDirectory.length());
				std::error_code typeError;
				if (entry->is_directory(typeError) && LeftOutOfCaseIndex(entry->path(), entry.depth() == 0)) {
					s_UnindexedDirectories.emplace_back(relative + "/");
					entry.disable_recursion_pending();
					continue;
				}
				if (entry->is_symlink(typeError) && entry->is_directory(typeError)) {
					while (linksDown.size() > 1 && !isUnder(entry->path(), linksDown.back().first)) {
						linksDown.pop_back();
					}
					std::error_code linkError;
					std::filesystem::path target = std::filesystem::weakly_canonical(entry->path(), linkError);
					std::filesystem::path above = std::filesystem::weakly_canonical(entry->path().parent_path(), linkError);
					bool loops = linkError || isUnder(above, target);
					for (const auto& [link, down]: linksDown) {
						loops = loops || target == down;
					}
					if (loops) {
						s_WorkingTree.insert(Hash(relative));
						entry.disable_recursion_pending();
						continue;
					}
					linksDown.emplace_back(entry->path(), target);
				}
				s_WorkingTree.insert(Hash(relative));
			}
			s_WorkingTreeIndexMS = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - indexStart).count());
		}
		// (Asked about by its full path too, from the working directory's set-up; the index is of paths relative to it.)
		std::string relative = pathToCheck.starts_with(s_WorkingDirectory) ? pathToCheck.substr(s_WorkingDirectory.length()) : pathToCheck;
		if (s_WorkingTree.contains(Hash(relative))) {
			return true;
		}
		// In a folder left out of the index: asked of the file system as it is, case checked or not.
		for (const std::string& directory: s_UnindexedDirectories) {
			if (relative.starts_with(directory) || relative + "/" == directory) {
				return std::filesystem::exists(pathToCheck);
			}
		}
		// (One stat, not two: the write time errors for a file that isn't there.)
		if (std::error_code missing; std::filesystem::last_write_time(pathToCheck, missing) > s_ProgramStartTime && !missing) {
			s_WorkingTree.insert(Hash(relative));
			return true;
		}
		return false;
	}
	return std::filesystem::exists(pathToCheck);
}

void System::EnableLoggingToCLI() {
#ifdef _WIN32
	// Create a console instance for the current process
	if (AllocConsole()) {
		CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
		GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &consoleInfo);
		consoleInfo.dwSize.X = 192;
		SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), consoleInfo.dwSize);

		static std::ofstream consoleOutStream("CONOUT$", std::ios::out);
		// Set std::cout stream buffer to consoleOut's buffer to redirect the output
		std::cout.rdbuf(consoleOutStream.rdbuf());
	} else {
		MessageBox(nullptr, "Failed to allocate a console instance for this process, game console output will not be printed to CLI!", "RTE Warning! (>_<)", MB_OK);
		return;
	}
#endif
	s_LogToCLI = true;
}

void System::PrintLoadingToCLI(const std::string& reportString, bool newItem) {
	if (newItem) {
		std::cout << std::endl;
	}
	// Overwrite current line
	std::cout << "\r";
	size_t startPos = 0;
	// Just make sure to really overwrite all old output, " - done! ✓" is shorter than "reading line 700"
	std::string unicodedOutput = reportString + "            ";

#if _LINUX_OR_MACOSX_
	// Colorize output with ANSI escape code
	std::string greenTick = "\033[1;32m✓\033[0;0m";
	std::string yellowDot = "\033[1;33m•\033[0;0m";
#elif _WIN32
	// Fancy colors don't work for the Windows console so just replace with blanks
	std::string greenTick = "";
	std::string yellowDot = "";
	// Also replace tab with 4 spaces because tab is super wide in the Windows console
	size_t tabPos = 0;
	while ((tabPos = unicodedOutput.find("\t")) != std::string::npos) {
		unicodedOutput.replace(tabPos, 1, "    ");
	}
#endif
	// Convert all ✓ characters to unicode, it's the 42th from last character in CC's custom font
	while ((startPos = unicodedOutput.find(-42, startPos)) != std::string::npos) {
		unicodedOutput.replace(startPos, 1, greenTick);
		// We don't have to check indices we just overwrote
		startPos += greenTick.length();
	}
	startPos = 0;

	// Convert all • characters to unicode
	while ((startPos = unicodedOutput.find(-43, startPos)) != std::string::npos) {
		unicodedOutput.replace(startPos, 1, yellowDot);
		startPos += yellowDot.length();
	}
	std::cout << unicodedOutput << std::flush;
}

void System::PrintToCLI(const std::string& stringToPrint) {
#if _LINUX_OR_MACOSX_
	std::string outputString = stringToPrint;
	// Color the words ERROR: and SYSTEM: red
	std::regex regexError("(ERROR|SYSTEM):");
	outputString = std::regex_replace(outputString, regexError, "\033[1;31m$&\033[0;0m");

	// Color .rte-paths green
	std::regex regexPath("\\w*\\.rte\\/(\\w| |\\.|\\/)*(\\/|\\.bmp|\\.png|\\.wav|\\.ogg|\\.flac||\\.lua|\\.ini)");
	outputString = std::regex_replace(outputString, regexPath, "\033[1;32m$&\033[0;0m");

	// Color names in quotes yellow, they have to start with an upper case letter to sort out apostrophes
	std::regex regexName("(\"[A-Z].*\"|\'[A-Z].*\')");
	outputString = std::regex_replace(outputString, regexName, "\033[1;33m$&\033[0;0m");

	std::cout << "\r" << outputString << std::endl;
#elif _WIN32
	// All the fancy formatting doesn't work with the Windows console so just print the string as it is
	std::cout << "\r" << stringToPrint << std::endl;
#endif
}

std::string System::ExtractZippedDataModule(const std::string& zippedModulePath) {
	std::string zippedModuleName = System::GetModDirectory() + std::filesystem::path(zippedModulePath).filename().generic_string();

	unzFile zippedModule = unzOpen(zippedModuleName.c_str());
	std::stringstream extractionProgressReport;
	bool abortExtract = false;

	if (!zippedModule) {
		bool makeDirResult = false;
		if (!std::filesystem::exists(s_WorkingDirectory + "_FailedExtract")) {
			makeDirResult = MakeDirectory(s_WorkingDirectory + "_FailedExtract");
		}
		if (makeDirResult) {
			extractionProgressReport << "Failed to extract Data module from: " + zippedModuleName + " - Moving zip file to failed extract directory!\n";
			std::filesystem::rename(s_WorkingDirectory + zippedModuleName, s_WorkingDirectory + "_FailedExtract/" + zippedModuleName);
		} else {
			extractionProgressReport << "Failed to extract Data module from: " + zippedModuleName + " - Failed to create directory to move zip file into, deleting zip file!\n";
			std::remove((s_WorkingDirectory + zippedModuleName).c_str());
		}
		return extractionProgressReport.str();
	}

	unz_global_info zippedModuleInfo;
	if (unzGetGlobalInfo(zippedModule, &zippedModuleInfo) != UNZ_OK) {
		extractionProgressReport << "\tSkipped: " + zippedModuleName + " - Could not read global file info!\n";
		abortExtract = true;
	}
	std::array<char, s_FileBufferSize> fileBuffer;

	// Go through and extract every file inside this zip, overwriting every colliding file that already exists in the install directory.
	for (size_t i = 0; i < zippedModuleInfo.number_entry && !abortExtract; ++i) {
		unz_file_info currentFileInfo;
		std::array<char, s_MaxFileName> outputFileInfoData;
		if (unzGetCurrentFileInfo(zippedModule, &currentFileInfo, outputFileInfoData.data(), s_MaxFileName, nullptr, 0, nullptr, 0) != UNZ_OK) {
			extractionProgressReport << "\tSkipped: " + std::string(outputFileInfoData.data()) + " - Could not read file info!\n";
			continue;
		}
		std::string outputFileName = System::GetModDirectory() + outputFileInfoData.data();
#ifdef _WIN32
		// TODO: Windows 10 adds support for paths over 260 characters so investigate how to get Windows version and whether the setting is enabled at runtime.
		// Windows doesn't support paths over 260 characters long.
		if ((s_WorkingDirectory + outputFileName).length() >= MAX_PATH) {
			extractionProgressReport << "\tSkipped file: " + outputFileName + " - Full path to file exceeds 260 characters!\n";
			continue;
		}
#endif
		// Check if the directory we are trying to extract into exists, and if not, create it.
		std::string outputFileDirectory = outputFileName.substr(0, outputFileName.find_last_of("/\\") + 1);
		if (!std::filesystem::exists(outputFileDirectory)) {
			if (!MakeDirectory(s_WorkingDirectory + outputFileDirectory)) {
				extractionProgressReport << "\tFailed to create directory: " + outputFileName + " - Extraction aborted!\n";
				abortExtract = true;
				continue;
			} else {
				extractionProgressReport << "\tCreated directory: " + outputFileName + "\n";
			}
		}
		// If the output file is a directly, go the next entry listed in the zip file.
		if (std::filesystem::is_directory(outputFileName)) {
			unzCloseCurrentFile(zippedModule);
			if ((i + 1) < zippedModuleInfo.number_entry && unzGoToNextFile(zippedModule) != UNZ_OK) {
				extractionProgressReport << "\tCould not read next file inside zip - Extraction aborted!\n";
				abortExtract = true;
			}
			continue;
		}

		// Validate so only certain file types are extracted.
		std::string fileExtension = std::filesystem::path(outputFileName).extension().generic_string();
		std::transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), tolower);

		if (s_SupportedExtensions.find(fileExtension) == s_SupportedExtensions.end()) {
			extractionProgressReport << "\tSkipped file: " + outputFileName + " - Bad extension!\n";
			unzCloseCurrentFile(zippedModule);

			if ((i + 1) < zippedModuleInfo.number_entry && unzGoToNextFile(zippedModule) != UNZ_OK) {
				extractionProgressReport << "\tCould not read next file inside zip - Extraction aborted!\n";
				abortExtract = true;
			}
			continue;
		}

		if (unzOpenCurrentFile(zippedModule) != UNZ_OK) {
			extractionProgressReport << "\tSkipped file: " + zippedModuleName + " - Could not open file!\n";
		} else {
			FILE* outputFile = fopen(outputFileName.c_str(), "wb");
			if (outputFile == nullptr) {
				extractionProgressReport << "\tSkipped file: " + outputFileName + " - Could not open/create destination file!\n";
			} else {
				// Write the entire file out, reading in buffer size chunks and spitting them out to the output stream.
				bool abortWrite = false;
				int bytesRead = 0;
				int totalBytesRead = 0;
				do {
					bytesRead = unzReadCurrentFile(zippedModule, fileBuffer.data(), s_FileBufferSize);
					totalBytesRead += bytesRead;

					if (bytesRead < 0) {
						extractionProgressReport << "\tSkipped file: " + outputFileName + " - File is empty or corrupt!\n";
						abortWrite = true;
						// Sanity check how damn big this file we're writing is becoming. could prevent zip bomb exploits: http://en.wikipedia.org/wiki/Zip_bomb
					} else if (totalBytesRead >= s_MaxUnzippedFileSize) {
						extractionProgressReport << "\tSkipped file: " + outputFileName + " - File is too large, extract it manually!\n";
						abortWrite = true;
					}
					if (abortWrite) {
						break;
					}
					fwrite(fileBuffer.data(), bytesRead, 1, outputFile);
					// Keep going while bytes are still being read (0 means end of file).
				} while (bytesRead > 0 && outputFile);

				fclose(outputFile);
			}

			unzCloseCurrentFile(zippedModule);

			extractionProgressReport << "\tExtracted file: " + outputFileName + "\n";
		}

		if ((i + 1) < zippedModuleInfo.number_entry && unzGoToNextFile(zippedModule) != UNZ_OK) {
			extractionProgressReport << "\tCould not read next file inside zip - Extraction aborted!\n";
			abortExtract = true;
		}
	}
	unzClose(zippedModule);

	if (!abortExtract) {
		extractionProgressReport << "Successfully extracted Data Module from: " + zippedModuleName + " - Deleting zip file!\n";
		std::remove((s_WorkingDirectory + zippedModuleName).c_str());
	}

	return extractionProgressReport.str();
}

int System::ASCIIFileContainsString(const std::string& filePath, const std::string_view& findString) {
	std::ifstream inputStream(filePath, std::ios::binary);
	if (!inputStream.is_open()) {
		return -1;
	} else {
		size_t fileSize = static_cast<size_t>(std::filesystem::file_size(filePath));
		std::vector<unsigned char> rawData(fileSize);
		inputStream.read(reinterpret_cast<char*>(&rawData[0]), fileSize);
		inputStream.close();

		return (std::search(rawData.begin(), rawData.end(), findString.begin(), findString.end()) != rawData.end()) ? 0 : 1;
	}
}
