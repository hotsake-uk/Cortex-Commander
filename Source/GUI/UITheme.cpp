#include "UITheme.h"

#include "PresetMan.h"
#include "System.h"
#include "ToolWidgets.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>

using namespace RTE;

namespace {
	/// One theme, as read from its Theme.ini.
	struct Theme {
		std::string Name;
		int Order = 0;
		std::unordered_map<std::string, std::string> Images; //!< Swapped images: the original's module path, lower case, to the replacement's module path.
		std::unordered_map<std::string, std::string> FoundImages; //!< Images already looked up: the original's module path, lower case, to the full path to load, empty if the replacement is missing.
		std::unordered_map<unsigned long, unsigned long> Colors; //!< Swapped GUI colours.
		std::unordered_map<std::string, ImU32> HUD; //!< Tool window and HUD colours by role.
	};

	std::vector<Theme> s_Themes;
	bool s_Loaded = false;
	Theme* s_Current = nullptr;
	std::string s_CurrentName = UITheme::ClassicName;
	int s_Generation = 0;

	std::string Trim(const std::string& text) {
		size_t from = text.find_first_not_of(" \t\r\n");
		if (from == std::string::npos) {
			return "";
		}
		return text.substr(from, text.find_last_not_of(" \t\r\n") - from + 1);
	}

	std::string Lower(std::string text) {
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	/// The part of a path from its module's folder on (Base.rte/GUIs/...), with forward slashes, or the whole path if it isn't in a module.
	std::string ModulePath(const std::string& path) {
		std::string generic = path;
		std::replace(generic.begin(), generic.end(), '\\', '/');
		size_t module = Lower(generic).find(".rte/");
		if (module == std::string::npos) {
			return generic;
		}
		size_t start = generic.rfind('/', module);
		return start == std::string::npos ? generic : generic.substr(start + 1);
	}

	/// Reads a colour: #RRGGBB (packed RGB) or a palette index. False if it's neither.
	bool ReadColor(const std::string& text, unsigned long& color) {
		try {
			if (!text.empty() && text.front() == '#') {
				if (text.size() != 7) {
					return false;
				}
				color = std::stoul(text.substr(1), nullptr, 16);
				// Packed colours up to 255 would be taken for palette indices; the faintest touch of red keeps them RGB.
				if (color <= 255) {
					color |= 0x010000;
				}
			} else {
				color = std::stoul(text);
			}
			return true;
		} catch (const std::exception&) {
			return false;
		}
	}

	bool ReadHUDColor(const std::string& text, ImU32& color) {
		unsigned long packed = 0;
		if (text.empty() || text.front() != '#' || !ReadColor(text, packed)) {
			return false;
		}
		color = IM_COL32((packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF, 255);
		return true;
	}

	/// Reads a Theme.ini. False if it has no name.
	bool ReadTheme(const std::filesystem::path& file, Theme& theme) {
		std::ifstream stream(file);
		std::string line;
		while (std::getline(stream, line)) {
			if (size_t comment = line.find("//"); comment != std::string::npos) {
				line.erase(comment);
			}
			size_t equals = line.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			std::string key = Trim(line.substr(0, equals));
			std::string value = Trim(line.substr(equals + 1));
			std::string from;
			std::string to;
			if (size_t arrow = value.find("=>"); arrow != std::string::npos) {
				from = Trim(value.substr(0, arrow));
				to = Trim(value.substr(arrow + 2));
			}
			if (key == "Name") {
				theme.Name = value;
			} else if (key == "Order") {
				try {
					theme.Order = std::stoi(value);
				} catch (const std::exception&) {
				}
			} else if (key == "Image" && !from.empty() && !to.empty()) {
				theme.Images[Lower(ModulePath(from))] = ModulePath(to);
			} else if (key == "Color") {
				unsigned long original = 0;
				unsigned long replacement = 0;
				if (ReadColor(from, original) && ReadColor(to, replacement)) {
					theme.Colors[original] = replacement;
				}
			} else if (key.rfind("HUD.", 0) == 0) {
				if (ImU32 color = 0; ReadHUDColor(value, color)) {
					theme.HUD[key.substr(4)] = color;
				}
			}
		}
		return !theme.Name.empty();
	}

	/// Finds the themes in every module's GUIs/Themes, the game's own first. A later theme of a name already found is left out.
	void Load() {
		if (s_Loaded) {
			return;
		}
		s_Loaded = true;
		s_Themes.clear();
		std::error_code error;
		for (const std::string& top: {System::GetDataDirectory(), System::GetModDirectory(), System::GetUserdataDirectory()}) {
			std::vector<std::filesystem::path> modules;
			for (const auto& entry: std::filesystem::directory_iterator(top, error)) {
				if (entry.is_directory(error) && Lower(entry.path().extension().string()) == ".rte") {
					modules.push_back(entry.path());
				}
			}
			// Base.rte first, so its themes win over a mod's of the same name; then the rest in a steady order.
			std::sort(modules.begin(), modules.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
				bool aBase = Lower(a.filename().string()) == "base.rte";
				bool bBase = Lower(b.filename().string()) == "base.rte";
				return aBase != bBase ? aBase : a.filename().string() < b.filename().string();
			});
			for (const std::filesystem::path& module: modules) {
				std::vector<std::filesystem::path> folders;
				for (const auto& entry: std::filesystem::directory_iterator(module / "GUIs" / "Themes", error)) {
					if (entry.is_directory(error) && std::filesystem::exists(entry.path() / "Theme.ini", error)) {
						folders.push_back(entry.path());
					}
				}
				std::sort(folders.begin(), folders.end());
				for (const std::filesystem::path& folder: folders) {
					Theme theme;
					if (ReadTheme(folder / "Theme.ini", theme) && std::none_of(s_Themes.begin(), s_Themes.end(), [&theme](const Theme& known) { return Lower(known.Name) == Lower(theme.Name); })) {
						s_Themes.push_back(std::move(theme));
					}
				}
			}
		}
		std::stable_sort(s_Themes.begin(), s_Themes.end(), [](const Theme& a, const Theme& b) {
			bool aClassic = a.Name == UITheme::ClassicName;
			bool bClassic = b.Name == UITheme::ClassicName;
			if (aClassic != bClassic) {
				return aClassic;
			}
			return a.Order != b.Order ? a.Order < b.Order : a.Name < b.Name;
		});
	}

	/// Sets the tool window and HUD colours to the theme's, or Classic's where it leaves them be.
	void ApplyHUD(const Theme* theme) {
		auto role = [theme](const char* name, ImU32 classic) {
			if (theme) {
				if (auto found = theme->HUD.find(name); found != theme->HUD.end()) {
					return found->second;
				}
			}
			return classic;
		};
		ToolTheme::Panel = role("Panel", ToolTheme::ClassicTheme::Panel);
		ToolTheme::Well = role("Well", ToolTheme::ClassicTheme::Well);
		ToolTheme::WellHover = role("WellHover", ToolTheme::ClassicTheme::WellHover);
		ToolTheme::WellPressed = role("WellPressed", ToolTheme::ClassicTheme::WellPressed);
		ToolTheme::Edge = role("Edge", ToolTheme::ClassicTheme::Edge);
		ToolTheme::EdgeLight = role("EdgeLight", ToolTheme::ClassicTheme::EdgeLight);
		ToolTheme::EdgeDark = role("EdgeDark", ToolTheme::ClassicTheme::EdgeDark);
	}
} // namespace

std::vector<std::string> UITheme::Names() {
	Load();
	std::vector<std::string> names;
	for (const Theme& theme: s_Themes) {
		names.push_back(theme.Name);
	}
	if (std::find(names.begin(), names.end(), ClassicName) == names.end()) {
		names.insert(names.begin(), ClassicName);
	}
	return names;
}

const std::string& UITheme::Current() {
	return s_CurrentName;
}

void UITheme::Select(const std::string& name) {
	Load();
	Theme* chosen = nullptr;
	for (Theme& theme: s_Themes) {
		if (Lower(theme.Name) == Lower(name)) {
			chosen = &theme;
			break;
		}
	}
	std::string chosenName = chosen ? chosen->Name : ClassicName;
	if (chosenName == s_CurrentName && chosen == s_Current) {
		return;
	}
	s_Current = chosen;
	s_CurrentName = chosenName;
	ApplyHUD(chosen);
	++s_Generation;
}

int UITheme::Generation() {
	return s_Generation;
}

std::string UITheme::Image(const std::string& path) {
	if (!s_Current || s_Current->Images.empty()) {
		return path;
	}
	std::string key = Lower(ModulePath(path));
	auto found = s_Current->FoundImages.find(key);
	if (found == s_Current->FoundImages.end()) {
		std::string replacement;
		if (auto swap = s_Current->Images.find(key); swap != s_Current->Images.end()) {
			replacement = g_PresetMan.GetFullModulePath(swap->second);
			std::error_code error;
			if (!std::filesystem::exists(replacement, error)) {
				replacement.clear();
			}
		}
		found = s_Current->FoundImages.emplace(key, replacement).first;
	}
	return found->second.empty() ? path : found->second;
}

unsigned long UITheme::Color(unsigned long color) {
	if (!s_Current) {
		return color;
	}
	auto found = s_Current->Colors.find(color);
	return found == s_Current->Colors.end() ? color : found->second;
}
