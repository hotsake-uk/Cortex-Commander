// Installing from Cortex Depot. An install asks the Depot for its plan (GET /api/v1/items/{slug}/install: the item and what it needs,
// in order, with checksums), downloads each file that isn't here yet on the downloads' thread, checks it against its SHA-256 and unpacks
// it into Userdata/Depot/Staging. Only the modules the plan names are taken from a zip, wherever in it they sit, and only files of the
// kinds a zipped module may hold (System::IsExtractableExtension), each by a path that stays inside its module. Nothing in the mods folder
// changes until the next start: ApplyPending moves what was staged into place, or takes away what was asked to be removed, before any
// module loads. Battle setups are merged into Userdata/BattlePresets/<map>.txt, renamed where a setup of that name is there already.

#include "DepotInstall.h"
#include "DataModule.h"
#include "DepotClient.h"
#include "PresetMan.h"
#include "System.h"

#ifdef SYSTEM_MINIZIP
#include <minizip/unzip.h>
#else
#include "unzip.h"
#endif

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

using namespace RTE;
using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
	constexpr const char* c_DepotFolder = "Userdata/Depot/";
	constexpr const char* c_InstalledFile = "Userdata/Depot/Installed.json";
	constexpr const char* c_PendingFile = "Userdata/Depot/Pending.json";
	constexpr const char* c_StagingFolder = "Userdata/Depot/Staging/";
	constexpr const char* c_DownloadFolder = "Userdata/Depot/Downloads/";
	constexpr const char* c_PresetsFolder = "Userdata/BattlePresets/";

#pragma region SHA-256
	/// SHA-256 (FIPS 180-4), to check a download is the file the Depot has.
	class Sha256 {
	public:
		void Add(const unsigned char* data, size_t size) {
			m_Bits += static_cast<uint64_t>(size) * 8;
			while (size > 0) {
				size_t take = std::min(size, sizeof(m_Block) - m_Used);
				std::memcpy(m_Block + m_Used, data, take);
				m_Used += take;
				data += take;
				size -= take;
				if (m_Used == sizeof(m_Block)) {
					Compress();
					m_Used = 0;
				}
			}
		}

		std::string Hex() {
			uint64_t bits = m_Bits;
			unsigned char pad = 0x80;
			Add(&pad, 1);
			pad = 0;
			while (m_Used != 56) {
				Add(&pad, 1);
			}
			for (int i = 7; i >= 0; --i) {
				m_Block[m_Used++] = static_cast<unsigned char>(bits >> (i * 8));
			}
			Compress();
			static const char* digits = "0123456789abcdef";
			std::string hex;
			for (uint32_t word: m_State) {
				for (int i = 28; i >= 0; i -= 4) {
					hex += digits[(word >> i) & 15];
				}
			}
			return hex;
		}

	private:
		static uint32_t Rotate(uint32_t value, int by) { return (value >> by) | (value << (32 - by)); }

		void Compress() {
			static constexpr uint32_t k[64] = {
			    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
			uint32_t w[64];
			for (int i = 0; i < 16; ++i) {
				w[i] = (static_cast<uint32_t>(m_Block[i * 4]) << 24) | (static_cast<uint32_t>(m_Block[i * 4 + 1]) << 16) | (static_cast<uint32_t>(m_Block[i * 4 + 2]) << 8) | m_Block[i * 4 + 3];
			}
			for (int i = 16; i < 64; ++i) {
				uint32_t s0 = Rotate(w[i - 15], 7) ^ Rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
				uint32_t s1 = Rotate(w[i - 2], 17) ^ Rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}
			uint32_t a = m_State[0], b = m_State[1], c = m_State[2], d = m_State[3], e = m_State[4], f = m_State[5], g = m_State[6], h = m_State[7];
			for (int i = 0; i < 64; ++i) {
				uint32_t t1 = h + (Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
				uint32_t t2 = (Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
				h = g;
				g = f;
				f = e;
				e = d + t1;
				d = c;
				c = b;
				b = a;
				a = t1 + t2;
			}
			m_State[0] += a;
			m_State[1] += b;
			m_State[2] += c;
			m_State[3] += d;
			m_State[4] += e;
			m_State[5] += f;
			m_State[6] += g;
			m_State[7] += h;
		}

		uint32_t m_State[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
		unsigned char m_Block[64] = {};
		size_t m_Used = 0;
		uint64_t m_Bits = 0;
	};

	/// The SHA-256 of a file, in lower-case hex, or empty when it can't be read.
	std::string FileSha256(const fs::path& file) {
		std::ifstream stream(file, std::ios::binary);
		if (!stream) {
			return "";
		}
		Sha256 hash;
		std::vector<char> buffer(64 * 1024);
		while (stream) {
			stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			hash.Add(reinterpret_cast<const unsigned char*>(buffer.data()), static_cast<size_t>(stream.gcount()));
		}
		return hash.Hex();
	}
#pragma endregion

	std::string Lowered(std::string text) {
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	bool SameName(const std::string& a, const std::string& b) { return Lowered(a) == Lowered(b); }

	std::string Str(const Json& object, const char* key) {
		auto found = object.find(key);
		return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
	}

	std::vector<std::string> Strings(const Json& object, const char* key) {
		std::vector<std::string> strings;
		auto found = object.find(key);
		if (found != object.end() && found->is_array()) {
			for (const Json& value: *found) {
				if (value.is_string()) {
					strings.push_back(value.get<std::string>());
				}
			}
		}
		return strings;
	}

	/// A slug as a file name: the Depot's are already letters, digits and dashes, but a file name is no place to trust that.
	std::string FileNameOf(const std::string& slug) {
		std::string name;
		for (char c: slug) {
			name += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ? c : '_';
		}
		return name.empty() ? "item" : name.substr(0, 80);
	}

	/// Whether a name is one the Depot may put in the mods folder or take out of it: a single folder ending in .rte, and not one of the
	/// game's own.
	bool ModuleFolderAllowed(const std::string& name) {
		if (name.size() <= 4 || !Lowered(name).ends_with(".rte") || name.find_first_of("/\\:") != std::string::npos || name.front() == '.') {
			return false;
		}
		return !g_PresetMan.IsModuleOfficial(name) && !g_PresetMan.IsModuleUserdata(name);
	}

	fs::path ModsFolder() { return fs::path(System::GetWorkingDirectory()) / System::GetModDirectory(); }

	Json ToJson(const DepotInstall::Entry& entry) {
		Json json = {{"slug", entry.Slug}, {"kind", entry.Kind}, {"title", entry.Title}, {"version", entry.Version}, {"installTo", entry.InstallTo}, {"modules", entry.Modules}};
		if (!entry.Map.empty()) {
			json["map"] = entry.Map;
			json["setups"] = entry.Setups;
		}
		if (entry.Adopted) {
			json["adopted"] = true;
		}
		return json;
	}

	DepotInstall::Entry EntryFrom(const Json& json) {
		DepotInstall::Entry entry;
		entry.Slug = Str(json, "slug");
		entry.Kind = Str(json, "kind");
		entry.Title = Str(json, "title");
		entry.Version = Str(json, "version");
		entry.InstallTo = Str(json, "installTo");
		entry.Modules = Strings(json, "modules");
		entry.Map = Str(json, "map");
		entry.Setups = Strings(json, "setups");
		entry.Adopted = json.value("adopted", false);
		return entry;
	}

	/// A change waiting for the next start.
	struct Op {
		std::string Action; //!< "install" or "remove".
		DepotInstall::Entry Item;
		std::string Stage; //!< For an install: the folder (modules) or file (battle setups) it's staged in.
	};

	/// An install the player asked for, with what its download shares with the downloads' thread.
	struct TaskState {
		DepotInstall::Task Shown;
		std::shared_ptr<DepotClient::Progress> Progress = std::make_shared<DepotClient::Progress>();
		std::shared_ptr<std::atomic<int>> Step = std::make_shared<std::atomic<int>>(0);
		std::vector<std::string> StepTitles;
	};

	struct State {
		bool Loaded = false;
		std::vector<DepotInstall::Entry> Installed;
		std::vector<Op> Pending;
		std::map<std::string, TaskState> Tasks;
		std::vector<DepotInstall::Update> Updates;
		std::vector<DepotInstall::Match> Matches;
		bool Checking = false;
		std::string UpdateError;
		bool Restart = false;
	};
	State s_State;

	Json ReadJson(const char* file) {
		std::ifstream stream(file, std::ios::binary);
		if (!stream) {
			return Json();
		}
		return Json::parse(stream, nullptr, false);
	}

	/// Writes a file whole or not at all: to the side first, then over the old one.
	void WriteJson(const char* file, const Json& json) {
		std::error_code error;
		fs::create_directories(c_DepotFolder, error);
		std::string temporary = std::string(file) + ".new";
		{
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
			stream << json.dump(1, '\t') << '\n';
			if (!stream) {
				return;
			}
		}
		fs::rename(temporary, file, error);
		if (error) {
			fs::remove(file, error);
			fs::rename(temporary, file, error);
		}
	}

	void Load() {
		if (s_State.Loaded) {
			return;
		}
		s_State.Loaded = true;
		Json installed = ReadJson(c_InstalledFile);
		if (installed.is_object() && installed.contains("items") && installed["items"].is_array()) {
			for (const Json& item: installed["items"]) {
				if (item.is_object() && !Str(item, "slug").empty()) {
					s_State.Installed.push_back(EntryFrom(item));
				}
			}
		}
		Json pending = ReadJson(c_PendingFile);
		if (pending.is_object() && pending.contains("changes") && pending["changes"].is_array()) {
			for (const Json& change: pending["changes"]) {
				if (change.is_object() && change.contains("item") && change["item"].is_object()) {
					s_State.Pending.push_back({Str(change, "action"), EntryFrom(change["item"]), Str(change, "stage")});
				}
			}
		}
	}

	void SaveInstalled() {
		Json items = Json::array();
		for (const DepotInstall::Entry& entry: s_State.Installed) {
			items.push_back(ToJson(entry));
		}
		WriteJson(c_InstalledFile, {{"items", items}});
	}

	void SavePending() {
		if (s_State.Pending.empty()) {
			std::error_code error;
			fs::remove(c_PendingFile, error);
			return;
		}
		Json changes = Json::array();
		for (const Op& op: s_State.Pending) {
			Json change = {{"action", op.Action}, {"item", ToJson(op.Item)}};
			if (!op.Stage.empty()) {
				change["stage"] = op.Stage;
			}
			changes.push_back(change);
		}
		WriteJson(c_PendingFile, {{"changes", changes}});
	}

	DepotInstall::Entry* FindInstalled(const std::string& slug) {
		auto found = std::find_if(s_State.Installed.begin(), s_State.Installed.end(), [&](const DepotInstall::Entry& entry) { return entry.Slug == slug; });
		return found == s_State.Installed.end() ? nullptr : &*found;
	}

	/// Drops what's waiting for an item, and what was staged for it.
	void DropPending(const std::string& slug) {
		for (auto op = s_State.Pending.begin(); op != s_State.Pending.end();) {
			if (op->Item.Slug == slug) {
				if (!op->Stage.empty()) {
					std::error_code error;
					fs::remove_all(op->Stage, error);
				}
				op = s_State.Pending.erase(op);
			} else {
				++op;
			}
		}
	}

#pragma region Unpacking
	/// Unpacks the modules named from a zip into a folder, wherever in the zip they sit (at the top, or in a folder of their own).
	/// @param modules The .rte folders to take; set to their names as the zip has them.
	/// @return Why it failed, or empty.
	std::string UnpackModules(const fs::path& zip, const fs::path& stage, std::vector<std::string>& modules, const std::string& title) {
		unzFile file = unzOpen(zip.generic_string().c_str());
		if (!file) {
			return title + " isn't a zip the game can read.";
		}
		std::map<std::string, std::string> found; // Lowered name to the name in the zip.
		std::set<std::string> wanted;
		for (const std::string& module: modules) {
			wanted.insert(Lowered(module));
		}
		std::string problem;
		unz_global_info info{};
		if (unzGetGlobalInfo(file, &info) != UNZ_OK) {
			problem = title + "'s zip can't be read.";
		}
		std::vector<char> buffer(64 * 1024);
		for (unsigned long i = 0; problem.empty() && i < info.number_entry; ++i) {
			if (i > 0 && unzGoToNextFile(file) != UNZ_OK) {
				problem = title + "'s zip is damaged.";
				break;
			}
			unz_file_info fileInfo{};
			std::array<char, 1024> name{};
			if (unzGetCurrentFileInfo(file, &fileInfo, name.data(), static_cast<uLong>(name.size() - 1), nullptr, 0, nullptr, 0) != UNZ_OK) {
				continue;
			}
			std::string path = System::SafeZipEntryPath(name.data());
			if (path.empty() || path.back() == '/' || path.starts_with("__MACOSX/")) {
				continue;
			}
			// The part of the path from the module's folder on.
			std::vector<std::string> parts;
			for (size_t start = 0; start <= path.size();) {
				size_t end = path.find('/', start);
				parts.push_back(path.substr(start, end == std::string::npos ? std::string::npos : end - start));
				start = end == std::string::npos ? path.size() + 1 : end + 1;
			}
			auto module = std::find_if(parts.begin(), parts.end() - 1, [&](const std::string& part) { return wanted.contains(Lowered(part)); });
			if (module == parts.end() - 1) {
				continue;
			}
			fs::path relative;
			for (auto part = module; part != parts.end(); ++part) {
				relative /= *part;
			}
			if (!System::IsExtractableExtension(Lowered(relative.extension().generic_string())) || fileInfo.uncompressed_size > static_cast<uLong>(System::GetMaxUnzippedFileSize())) {
				continue;
			}
			found.emplace(Lowered(*module), *module);
			fs::path out = stage / relative;
			std::error_code error;
			fs::create_directories(out.parent_path(), error);
			if (unzOpenCurrentFile(file) != UNZ_OK) {
				problem = "Couldn't read " + relative.generic_string() + " from " + title + "'s zip.";
				break;
			}
			std::ofstream stream(out, std::ios::binary | std::ios::trunc);
			long long total = 0;
			int read = 0;
			while (stream && (read = unzReadCurrentFile(file, buffer.data(), static_cast<unsigned>(buffer.size()))) > 0) {
				total += read;
				if (total > System::GetMaxUnzippedFileSize()) {
					read = -1;
					break;
				}
				stream.write(buffer.data(), read);
			}
			unzCloseCurrentFile(file);
			if (read < 0 || !stream) {
				problem = !stream ? "Couldn't write " + out.generic_string() + " (is the disk full?)." : "Couldn't read " + relative.generic_string() + " from " + title + "'s zip.";
			}
		}
		unzClose(file);
		if (!problem.empty()) {
			return problem;
		}
		std::vector<std::string> names;
		for (const std::string& module: modules) {
			auto name = found.find(Lowered(module));
			if (name == found.end() || !fs::exists(stage / name->second / "Index.ini")) {
				return title + " doesn't have " + module + " in it as the Depot said.";
			}
			if (!ModuleFolderAllowed(name->second)) {
				return title + " has a module named " + name->second + ", which the Depot can't install.";
			}
			names.push_back(name->second);
		}
		modules = names;
		return "";
	}
#pragma endregion

#pragma region Battle setups
	std::string PresetsFile(const std::string& map) {
		std::string file = map;
		for (char& c: file) {
			if (std::strchr("<>:\"/\\|?*", c) || static_cast<unsigned char>(c) < 32) {
				c = '_';
			}
		}
		return c_PresetsFolder + file + ".txt";
	}

	std::string Trim(const std::string& text) {
		size_t from = text.find_first_not_of(" \t\r");
		size_t to = text.find_last_not_of(" \t\r");
		return from == std::string::npos ? std::string() : text.substr(from, to - from + 1);
	}

	/// A setups file: the lines before the first setup, and each setup's name and lines.
	struct SetupsFile {
		std::vector<std::string> Header;
		std::vector<std::pair<std::string, std::vector<std::string>>> Setups;
	};

	SetupsFile ReadSetups(std::istream& stream) {
		SetupsFile file;
		std::string line;
		while (std::getline(stream, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			std::string trimmed = Trim(line);
			if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
				file.Setups.emplace_back(trimmed.substr(1, trimmed.size() - 2), std::vector<std::string>());
			} else if (file.Setups.empty()) {
				file.Header.push_back(line);
			} else {
				file.Setups.back().second.push_back(line);
			}
		}
		return file;
	}

	SetupsFile ReadSetupsFile(const std::string& path) {
		std::ifstream stream(path, std::ios::binary);
		return ReadSetups(stream);
	}

	void WriteSetupsFile(const std::string& map, const SetupsFile& file) {
		std::error_code error;
		fs::create_directories(c_PresetsFolder, error);
		std::ofstream stream(PresetsFile(map), std::ios::binary | std::ios::trunc);
		if (file.Header.empty()) {
			stream << "// Battle Director setups for " << map << ", saved from the Battle tab.\n";
		}
		for (const std::string& line: file.Header) {
			stream << line << '\n';
		}
		for (const auto& [name, lines]: file.Setups) {
			size_t last = lines.size();
			while (last > 0 && Trim(lines[last - 1]).empty()) {
				--last;
			}
			stream << "\n[" << name << "]\n";
			for (size_t i = 0; i < last; ++i) {
				stream << lines[i] << '\n';
			}
		}
	}

	/// Takes setups out of a map's file by name.
	void RemoveSetups(const std::string& map, const std::vector<std::string>& names) {
		if (map.empty() || names.empty() || !fs::exists(PresetsFile(map))) {
			return;
		}
		SetupsFile file = ReadSetupsFile(PresetsFile(map));
		std::erase_if(file.Setups, [&](const auto& setup) { return std::find(names.begin(), names.end(), setup.first) != names.end(); });
		WriteSetupsFile(map, file);
	}

	/// Adds the setups in a downloaded file to a map's, each under a name not taken there already.
	/// @return The names they went in under.
	std::vector<std::string> AddSetups(const std::string& map, const std::string& downloaded) {
		SetupsFile file = ReadSetupsFile(PresetsFile(map));
		SetupsFile incoming = ReadSetupsFile(downloaded);
		std::vector<std::string> names;
		for (auto& [name, lines]: incoming.Setups) {
			std::string unique = name.empty() ? "Setup" : name;
			for (int n = 2; std::any_of(file.Setups.begin(), file.Setups.end(), [&](const auto& setup) { return setup.first == unique; }); ++n) {
				unique = (name.empty() ? "Setup" : name) + " (" + std::to_string(n) + ")";
			}
			file.Setups.emplace_back(unique, std::move(lines));
			names.push_back(unique);
		}
		WriteSetupsFile(map, file);
		return names;
	}
#pragma endregion

#pragma region Applying
	/// Takes a module folder out of the mods folder, under any casing of its name (the game would load either as the same module).
	void RemoveModuleFolder(const std::string& name) {
		if (!ModuleFolderAllowed(name)) {
			return;
		}
		std::error_code error;
		for (const fs::directory_entry& entry: fs::directory_iterator(ModsFolder(), error)) {
			if (entry.is_directory(error) && SameName(entry.path().filename().generic_string(), name)) {
				fs::remove_all(entry.path(), error);
			}
		}
	}

	/// Moves a folder, copying it when it can't simply be moved (another drive).
	bool MoveFolder(const fs::path& from, const fs::path& to) {
		std::error_code error;
		fs::rename(from, to, error);
		if (!error) {
			return true;
		}
		error.clear();
		fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
		return !error;
	}

	void ApplyInstall(const Op& op, const std::function<void(const std::string&)>& report) {
		DepotInstall::Entry entry = op.Item;
		DepotInstall::Entry* old = FindInstalled(entry.Slug);
		if (entry.InstallTo == "mods") {
			std::vector<std::string> moved;
			for (const std::string& module: entry.Modules) {
				fs::path staged = fs::path(op.Stage) / module;
				if (!ModuleFolderAllowed(module) || !fs::exists(staged)) {
					report("Depot: couldn't install " + module + " for " + entry.Title + ": it wasn't downloaded whole.");
					continue;
				}
				RemoveModuleFolder(module);
				if (MoveFolder(staged, ModsFolder() / module)) {
					moved.push_back(module);
				} else {
					report("Depot: couldn't move " + module + " into the mods folder.");
				}
			}
			if (old) {
				for (const std::string& module: old->Modules) {
					if (std::none_of(entry.Modules.begin(), entry.Modules.end(), [&](const std::string& now) { return SameName(now, module); })) {
						RemoveModuleFolder(module);
					}
				}
			}
			entry.Modules = moved;
		} else if (entry.InstallTo == "battlePresets") {
			if (old) {
				RemoveSetups(old->Map, old->Setups);
			}
			entry.Setups = AddSetups(entry.Map, op.Stage);
		}
		report("Depot: installed " + entry.Title + " " + entry.Version);
		if (old) {
			*old = entry;
		} else {
			s_State.Installed.push_back(entry);
		}
	}

	void ApplyRemove(const Op& op, const std::function<void(const std::string&)>& report) {
		DepotInstall::Entry* old = FindInstalled(op.Item.Slug);
		if (!old) {
			return;
		}
		for (const std::string& module: old->Modules) {
			RemoveModuleFolder(module);
		}
		RemoveSetups(old->Map, old->Setups);
		report("Depot: removed " + old->Title);
		std::erase_if(s_State.Installed, [&](const DepotInstall::Entry& entry) { return entry.Slug == op.Item.Slug; });
	}
#pragma endregion

#pragma region Installing
	/// A file of an install plan, to download.
	struct Download {
		DepotInstall::Entry Item;
		std::string Url;
		std::string Sha256;
		long long FileSize = 0;
		std::string Stage; //!< Set once it's unpacked.
	};

	TaskState& TaskOf(const std::string& slug) { return s_State.Tasks[slug]; }

	void Fail(const std::string& slug, const std::string& why) {
		TaskState& task = TaskOf(slug);
		task.Shown.Running = false;
		task.Shown.Error = why;
	}

	/// Whether something an item needs is here already: installed or waiting to be from the Depot, or its modules in the mods folder.
	bool AlreadyHere(const DepotInstall::Entry& item) {
		if (FindInstalled(item.Slug) || DepotInstall::PendingFor(item.Slug) == "install") {
			return true;
		}
		if (item.Modules.empty()) {
			return false;
		}
		return std::all_of(item.Modules.begin(), item.Modules.end(), [](const std::string& module) {
			std::error_code error;
			return g_PresetMan.GetModuleID(module) >= 0 || fs::exists(ModsFolder() / module, error);
		});
	}

	/// Downloads, checks and unpacks each file. Runs on the downloads' thread, touching nothing but what it's given.
	/// @return Why it stopped, or empty. The downloads before one that failed are still staged.
	std::string DownloadAll(std::vector<Download>& downloads, DepotClient::Progress& progress, std::atomic<int>& step) {
		std::error_code error;
		fs::create_directories(c_DownloadFolder, error);
		fs::create_directories(c_StagingFolder, error);
		for (size_t i = 0; i < downloads.size(); ++i) {
			Download& download = downloads[i];
			step = static_cast<int>(i);
			progress.Got = 0;
			progress.Total = download.FileSize;
			std::string name = FileNameOf(download.Item.Slug);
			fs::path temporary = fs::path(c_DownloadFolder) / (name + ".download");
			size_t most = download.FileSize > 0 ? static_cast<size_t>(download.FileSize) + 1024 * 1024 : static_cast<size_t>(2048) * 1024 * 1024;
			DepotClient::Response response = DepotClient::Fetch(download.Url, most, temporary.generic_string(), &progress);
			if (!response.Ok()) {
				fs::remove(temporary, error);
				return !response.Error.empty() ? response.Error : "The Depot wouldn't hand over " + download.Item.Title + " (" + std::to_string(response.Status) + ").";
			}
			if (!download.Sha256.empty() && FileSha256(temporary) != Lowered(download.Sha256)) {
				fs::remove(temporary, error);
				return download.Item.Title + " didn't arrive whole (its checksum doesn't match the Depot's). Try again.";
			}
			// A name of its own, so a change waiting for the same item keeps its files until this one replaces it.
			std::string stage = std::string(c_StagingFolder) + name + "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
			std::string problem;
			if (download.Item.InstallTo == "mods") {
				problem = UnpackModules(temporary, stage, download.Item.Modules, download.Item.Title);
			} else {
				stage += ".txt";
				fs::copy_file(temporary, stage, fs::copy_options::overwrite_existing, error);
				if (error) {
					problem = "Couldn't write " + stage + " (is the disk full?).";
				}
			}
			fs::remove(temporary, error);
			if (!problem.empty()) {
				fs::remove_all(stage, error);
				return problem;
			}
			download.Stage = stage;
		}
		return "";
	}

	/// What to do with an install plan from the Depot: download what isn't here.
	void FollowPlan(const std::string& slug, DepotClient::Response& response) {
		TaskState& task = TaskOf(slug);
		Json plan = Json::parse(response.Body, nullptr, false);
		if (!response.Ok() || !plan.is_object() || !plan.contains("steps") || !plan["steps"].is_array() || plan["steps"].empty()) {
			std::string error = !response.Error.empty() ? response.Error : plan.is_object() && plan.contains("error") && plan["error"].is_object() ? Str(plan["error"], "message") : "";
			Fail(slug, error.empty() ? "The Depot's install plan didn't make sense (" + std::to_string(response.Status) + ")." : error);
			return;
		}
		if (plan.contains("missing") && plan["missing"].is_array() && !plan["missing"].empty()) {
			std::string missing;
			for (const Json& each: plan["missing"]) {
				if (each.is_object()) {
					missing += (missing.empty() ? "" : ", ") + Str(each, "moduleId");
				}
			}
			task.Shown.Warning = "It needs " + missing + ", which nothing on the Depot provides, so it may not load until you get that some other way.";
		}
		std::vector<Download> downloads;
		const Json& steps = plan["steps"];
		for (size_t i = 0; i < steps.size(); ++i) {
			const Json& step = steps[i];
			if (!step.is_object()) {
				continue;
			}
			bool root = i + 1 == steps.size();
			Download download;
			download.Item = EntryFrom(step);
			download.Url = Str(step, "download");
			download.Sha256 = Str(step, "sha256");
			download.FileSize = step.value("fileSize", 0LL);
			if (root) {
				task.Shown.Title = download.Item.Title;
			}
			if (download.Item.InstallTo != "mods" && download.Item.InstallTo != "battlePresets") {
				if (root) {
					Fail(slug, "The game doesn't install this kind of thing itself. Open it on the Depot to get it.");
					return;
				}
				continue;
			}
			if (download.Item.InstallTo == "battlePresets" && download.Item.Map.empty()) {
				if (root) {
					Fail(slug, "These battle setups don't say which map they're for.");
					return;
				}
				continue;
			}
			std::string pendingVersion;
			if (root ? (DepotInstall::PendingFor(download.Item.Slug, &pendingVersion) == "install" && pendingVersion == download.Item.Version) || (DepotInstall::PendingFor(download.Item.Slug).empty() && FindInstalled(download.Item.Slug) && FindInstalled(download.Item.Slug)->Version == download.Item.Version) : AlreadyHere(download.Item)) {
				continue;
			}
			downloads.push_back(std::move(download));
		}
		if (downloads.empty()) {
			task.Shown.Running = false;
			task.Shown.Doing.clear();
			if (task.Shown.Warning.empty()) {
				s_State.Tasks.erase(slug);
			}
			return;
		}
		task.StepTitles.clear();
		for (const Download& download: downloads) {
			task.StepTitles.push_back(download.Item.Title + " " + download.Item.Version);
		}
		auto shared = std::make_shared<std::vector<Download>>(std::move(downloads));
		auto problem = std::make_shared<std::string>();
		auto progress = task.Progress;
		auto step = task.Step;
		DepotClient::Run([shared, problem, progress, step] { *problem = DownloadAll(*shared, *progress, *step); },
		                 [slug, shared, problem] {
			                 for (const Download& download: *shared) {
				                 if (download.Stage.empty()) {
					                 continue;
				                 }
				                 DropPending(download.Item.Slug);
				                 s_State.Pending.push_back({"install", download.Item, download.Stage});
			                 }
			                 SavePending();
			                 TaskState& task = TaskOf(slug);
			                 task.Shown.Running = false;
			                 task.Shown.Doing.clear();
			                 if (!problem->empty()) {
				                 task.Shown.Error = *problem;
			                 } else if (task.Shown.Warning.empty()) {
				                 s_State.Tasks.erase(slug);
			                 }
		                 },
		                 true);
	}
#pragma endregion
} // namespace

void DepotInstall::ApplyPending(const std::function<void(const std::string&)>& report) {
	Load();
	std::error_code error;
	fs::remove_all(c_DownloadFolder, error);
	if (s_State.Pending.empty()) {
		fs::remove_all(c_StagingFolder, error);
		return;
	}
	for (const Op& op: s_State.Pending) {
		if (op.Action == "install") {
			ApplyInstall(op, report);
		} else if (op.Action == "remove") {
			ApplyRemove(op, report);
		}
	}
	s_State.Pending.clear();
	SavePending();
	SaveInstalled();
	fs::remove_all(c_StagingFolder, error);
	// What was moved into the mods folder is older than this start, so the case check's index has to see it afresh.
	System::ForgetPathIndex();
}

void DepotInstall::RelaunchIfWanted(const char* argv0) {
	if (!s_State.Restart) {
		return;
	}
	std::string program = fs::path(argv0 ? argv0 : "").filename().generic_string();
	const char* base = SDL_GetBasePath();
	std::string path = base && !program.empty() ? std::string(base) + program : std::string(argv0 ? argv0 : "");
	const char* arguments[] = {path.c_str(), nullptr};
	if (SDL_Process* process = SDL_CreateProcess(arguments, false)) {
		SDL_DestroyProcess(process);
	}
}

const DepotInstall::Entry* DepotInstall::Installed(const std::string& slug) {
	Load();
	return FindInstalled(slug);
}

const std::vector<DepotInstall::Entry>& DepotInstall::AllInstalled() {
	Load();
	return s_State.Installed;
}

const DepotInstall::Entry* DepotInstall::InstalledModule(const std::string& moduleID) {
	Load();
	for (const Entry& entry: s_State.Installed) {
		if (std::any_of(entry.Modules.begin(), entry.Modules.end(), [&](const std::string& module) { return SameName(module, moduleID); })) {
			return &entry;
		}
	}
	return nullptr;
}

std::string DepotInstall::PendingFor(const std::string& slug, std::string* version) {
	Load();
	for (const Op& op: s_State.Pending) {
		if (op.Item.Slug == slug) {
			if (version) {
				*version = op.Item.Version;
			}
			return op.Action;
		}
	}
	return "";
}

int DepotInstall::PendingCount() {
	Load();
	return static_cast<int>(s_State.Pending.size());
}

void DepotInstall::Install(const std::string& slug, const std::string& version) {
	Load();
	if (slug.empty() || (s_State.Tasks.contains(slug) && s_State.Tasks[slug].Shown.Running)) {
		return;
	}
	TaskState& task = s_State.Tasks[slug];
	task = TaskState();
	task.Shown.Slug = slug;
	task.Shown.Title = slug;
	task.Shown.Running = true;
	std::string url = "/api/v1/items/" + DepotClient::Encode(slug) + "/install" + (version.empty() ? "" : "?version=" + DepotClient::Encode(version));
	auto response = std::make_shared<DepotClient::Response>();
	// (On the downloads' thread, so closing the Depot screen doesn't drop it.)
	DepotClient::Run([absolute = DepotClient::Absolute(url), response] { *response = DepotClient::Fetch(absolute, 4 * 1024 * 1024); }, [slug, response] { FollowPlan(slug, *response); }, true);
}

void DepotInstall::Remove(const std::string& slug) {
	Load();
	const Entry* installed = FindInstalled(slug);
	DropPending(slug);
	if (installed) {
		s_State.Pending.push_back({"remove", *installed, ""});
	}
	SavePending();
}

void DepotInstall::Undo(const std::string& slug) {
	Load();
	DropPending(slug);
	SavePending();
}

void DepotInstall::Adopt(const Match& match) {
	Load();
	if (FindInstalled(match.Slug)) {
		return;
	}
	Entry entry;
	entry.Slug = match.Slug;
	entry.Kind = "mod";
	entry.Title = match.Title;
	entry.Version = match.Version;
	entry.InstallTo = "mods";
	entry.Modules = {match.ModuleID};
	entry.Adopted = true;
	s_State.Installed.push_back(entry);
	SaveInstalled();
	std::erase_if(s_State.Matches, [&](const Match& each) { return each.Slug == match.Slug; });
}

const DepotInstall::Task* DepotInstall::TaskFor(const std::string& slug) {
	auto found = s_State.Tasks.find(slug);
	if (found == s_State.Tasks.end()) {
		return nullptr;
	}
	TaskState& task = found->second;
	if (task.Shown.Running) {
		int step = *task.Step;
		if (task.StepTitles.empty()) {
			task.Shown.Doing = "Asking the Depot what it needs";
			task.Shown.Progress = -1.0F;
		} else {
			std::string of = task.StepTitles.size() > 1 ? " (" + std::to_string(step + 1) + " of " + std::to_string(task.StepTitles.size()) + ")" : "";
			task.Shown.Doing = "Downloading " + task.StepTitles[std::clamp(step, 0, static_cast<int>(task.StepTitles.size()) - 1)] + of;
			long long total = task.Progress->Total;
			task.Shown.Progress = total > 0 ? std::clamp(static_cast<float>(task.Progress->Got) / static_cast<float>(total), 0.0F, 1.0F) : -1.0F;
		}
	}
	return &task.Shown;
}

void DepotInstall::Dismiss(const std::string& slug) {
	auto found = s_State.Tasks.find(slug);
	if (found != s_State.Tasks.end() && !found->second.Shown.Running) {
		s_State.Tasks.erase(found);
	}
}

void DepotInstall::RestartToApply() {
	s_State.Restart = true;
	System::SetQuit(true);
}

void DepotInstall::CheckForUpdates() {
	Load();
	if (s_State.Checking || DepotClient::s_Address.empty()) {
		return;
	}
	s_State.Checking = true;
	s_State.UpdateError.clear();
	Json installed = Json::array();
	for (const Entry& entry: s_State.Installed) {
		installed.push_back({{"slug", entry.Slug}, {"version", entry.Version}});
	}
	// The modules here that the Depot didn't install, so it can say which it has.
	Json modules = Json::array();
	for (int i = g_PresetMan.GetOfficialModuleCount(); i < g_PresetMan.GetTotalModuleCount(); ++i) {
		if (const DataModule* module = g_PresetMan.GetDataModule(i); module && !module->IsUserdata() && !InstalledModule(module->GetFileName())) {
			modules.push_back(module->GetFileName());
		}
	}
	std::string body = Json({{"installed", installed}, {"modules", modules}}).dump();
	auto response = std::make_shared<DepotClient::Response>();
	DepotClient::Run([absolute = DepotClient::Absolute("/api/v1/updates"), body, response] { *response = DepotClient::Fetch(absolute, 4 * 1024 * 1024, "", nullptr, body); },
	                 [response] {
		                 s_State.Checking = false;
		                 Json answer = Json::parse(response->Body, nullptr, false);
		                 if (!response->Ok() || !answer.is_object()) {
			                 s_State.UpdateError = !response->Error.empty() ? response->Error : "The Depot couldn't check for updates (" + std::to_string(response->Status) + ").";
			                 return;
		                 }
		                 s_State.Updates.clear();
		                 s_State.Matches.clear();
		                 if (answer.contains("updates") && answer["updates"].is_array()) {
			                 for (const Json& each: answer["updates"]) {
				                 if (each.is_object()) {
					                 s_State.Updates.push_back({Str(each, "slug"), Str(each, "title"), Str(each, "installed"), Str(each, "latest"), Str(each, "changelog"), each.value("fileSize", 0LL)});
				                 }
			                 }
		                 }
		                 if (answer.contains("matches") && answer["matches"].is_array()) {
			                 for (const Json& each: answer["matches"]) {
				                 if (each.is_object() && !FindInstalled(Str(each, "slug"))) {
					                 s_State.Matches.push_back({Str(each, "moduleId"), Str(each, "slug"), Str(each, "title"), Str(each, "version")});
				                 }
			                 }
		                 }
	                 },
	                 true);
}

bool DepotInstall::CheckingForUpdates() {
	return s_State.Checking;
}

std::vector<DepotInstall::Update> DepotInstall::Updates() {
	Load();
	std::vector<Update> updates;
	for (const Update& update: s_State.Updates) {
		std::string version;
		const Entry* installed = FindInstalled(update.Slug);
		bool waiting = PendingFor(update.Slug, &version) == "install" && version == update.Latest;
		if (!waiting && installed && installed->Version != update.Latest) {
			updates.push_back(update);
		}
	}
	return updates;
}

const std::vector<DepotInstall::Match>& DepotInstall::Matches() {
	return s_State.Matches;
}

const std::string& DepotInstall::UpdateError() {
	return s_State.UpdateError;
}
