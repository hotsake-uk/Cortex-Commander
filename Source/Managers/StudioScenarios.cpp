// The main menu's Studio Scenarios screen: every scenario (game activity) the game knows in a list, with the map each is played on and the
// mod it comes from, filtered by a search and (by default) to the mods' own. The one picked shows its description, map and mod beside the
// list, with a warning when the map it names isn't there; Play (or a double-click) goes to the scenario setup with it selected, where its
// own map is picked for it, so the difficulty, teams and gold are set as for any scenario. Drawn with ImGui, like the map browser. See
// StudioScenarios.h.

#include "StudioScenarios.h"

#include "Activity.h"
#include "DataModule.h"
#include "GameActivity.h"
#include "MenuMan.h"
#include "PresetMan.h"
#include "Scene.h"
#include "ToolWidgets.h"

#include <algorithm>
#include <cctype>
#include <list>
#include <vector>

using namespace RTE;

namespace {
	/// A scenario in the list.
	struct ScenarioEntry {
		std::string Name; //!< Its preset name.
		std::string Kind; //!< Its class (GAScripted, a built-in game mode...).
		std::string Description;
		std::string Map; //!< The map it names (SceneName), or empty for any compatible map.
		bool MapFound = false; //!< Whether a map of that name is loaded.
		std::string Mod; //!< The mod it comes from, by its own name.
		std::string ModFile; //!< That mod's folder (Mod.rte).
		bool Official = false; //!< From the game's own data, not a mod.
	};

	struct Screen {
		bool Open = false;
		std::vector<ScenarioEntry> Scenarios;
		std::vector<int> Shown;
		char Search[64] = "";
		bool OnlyMods = true;
		bool FiltersChanged = true;
		int Picked = -1;
		bool Back = false;
		std::string Play;
		std::string Pending;
	};
	Screen s_Screen;

	std::string Lower(std::string text) {
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	/// Every game activity the game knows, with its map and mod.
	void ReadScenarios() {
		s_Screen.Scenarios.clear();
		std::list<Entity*> scenes;
		g_PresetMan.GetAllOfType(scenes, "Scene");
		std::list<Entity*> activities;
		g_PresetMan.GetAllOfType(activities, "Activity");
		for (const Entity* entity: activities) {
			const GameActivity* activity = dynamic_cast<const GameActivity*>(entity);
			if (!activity || activity->GetPresetName() == "Mod Studio Test" || activity->GetPresetName().empty()) {
				continue;
			}
			ScenarioEntry entry;
			entry.Name = activity->GetPresetName();
			entry.Kind = activity->GetClassName();
			entry.Description = activity->GetDescription();
			entry.Map = activity->GetSceneName();
			if (!entry.Map.empty()) {
				entry.MapFound = std::any_of(scenes.begin(), scenes.end(), [&entry](const Entity* scene) { return scene->GetPresetName() == entry.Map; });
			}
			const int module = activity->GetModuleID();
			if (const DataModule* dataModule = g_PresetMan.GetDataModule(module)) {
				entry.Mod = dataModule->GetFriendlyName().empty() ? dataModule->GetFileName() : dataModule->GetFriendlyName();
				entry.ModFile = dataModule->GetFileName();
			}
			entry.Official = module >= 0 && module < g_PresetMan.GetOfficialModuleCount();
			s_Screen.Scenarios.push_back(std::move(entry));
		}
		// The mods' first, then by mod, then by name.
		std::stable_sort(s_Screen.Scenarios.begin(), s_Screen.Scenarios.end(), [](const ScenarioEntry& a, const ScenarioEntry& b) {
			if (a.Official != b.Official) {
				return !a.Official;
			}
			if (a.Mod != b.Mod) {
				return Lower(a.Mod) < Lower(b.Mod);
			}
			return Lower(a.Name) < Lower(b.Name);
		});
	}

	void Filter() {
		s_Screen.FiltersChanged = false;
		s_Screen.Shown.clear();
		const std::string search = Lower(s_Screen.Search);
		for (int i = 0; i < static_cast<int>(s_Screen.Scenarios.size()); ++i) {
			const ScenarioEntry& entry = s_Screen.Scenarios[i];
			if (s_Screen.OnlyMods && entry.Official) {
				continue;
			}
			if (!search.empty() && Lower(entry.Name + " " + entry.Map + " " + entry.Mod + " " + entry.ModFile).find(search) == std::string::npos) {
				continue;
			}
			s_Screen.Shown.push_back(i);
		}
		if (std::find(s_Screen.Shown.begin(), s_Screen.Shown.end(), s_Screen.Picked) == s_Screen.Shown.end()) {
			s_Screen.Picked = s_Screen.Shown.empty() ? -1 : s_Screen.Shown.front();
		}
	}

	void PlayPicked() {
		if (s_Screen.Picked >= 0 && s_Screen.Picked < static_cast<int>(s_Screen.Scenarios.size())) {
			s_Screen.Play = s_Screen.Scenarios[s_Screen.Picked].Name;
		}
	}

	/// The list: a row per scenario (its name, map and mod), picked by a click, played by a double-click.
	void List(float width, float height) {
		if (!ImGui::BeginChild("##scenarioList", ImVec2(width, height), ImGuiChildFlags_Borders)) {
			ImGui::EndChild();
			return;
		}
		if (ImGui::BeginTable("##scenarios", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Scenario", ImGuiTableColumnFlags_WidthStretch, 1.4F);
			ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch, 1.2F);
			ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch, 0.8F);
			ImGui::TableHeadersRow();
			for (int index: s_Screen.Shown) {
				const ScenarioEntry& entry = s_Screen.Scenarios[index];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushID(index);
				if (ImGui::Selectable(entry.Name.c_str(), s_Screen.Picked == index, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
					s_Screen.Picked = index;
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						PlayPicked();
					}
				}
				ImGui::PopID();
				ImGui::TableSetColumnIndex(1);
				if (entry.Map.empty()) {
					ImGui::TextDisabled("any map it fits");
				} else if (!entry.MapFound) {
					ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.4F, 1.0F), "%s (not found)", entry.Map.c_str());
				} else {
					ImGui::TextUnformatted(entry.Map.c_str());
				}
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(entry.Mod.c_str());
			}
			ImGui::EndTable();
		}
		ImGui::EndChild();
	}

	/// The one picked: its name, mod, map, kind and description, and Play.
	void Details(float width, float height) {
		if (!ImGui::BeginChild("##scenarioDetails", ImVec2(width, height), ImGuiChildFlags_Borders)) {
			ImGui::EndChild();
			return;
		}
		if (s_Screen.Picked < 0 || s_Screen.Picked >= static_cast<int>(s_Screen.Scenarios.size())) {
			ImGui::TextDisabled(s_Screen.Scenarios.empty() ? "No scenarios loaded." : "Pick a scenario.");
			ImGui::EndChild();
			return;
		}
		const ScenarioEntry& entry = s_Screen.Scenarios[s_Screen.Picked];
		const float playHeight = ImGui::GetFrameHeight() * 1.6F;
		ImGui::BeginChild("##scenarioInfo", ImVec2(0.0F, -(playHeight + ImGui::GetStyle().ItemSpacing.y)), ImGuiChildFlags_None);
		ImGui::TextUnformatted(entry.Name.c_str());
		ImGui::Separator();
		ImGui::TextDisabled("Mod");
		ImGui::SameLine(ImGui::GetFontSize() * 4.0F);
		ImGui::Text("%s%s", entry.Mod.c_str(), entry.ModFile.empty() || entry.ModFile == entry.Mod ? "" : (" (" + entry.ModFile + ")").c_str());
		ImGui::TextDisabled("Map");
		ImGui::SameLine(ImGui::GetFontSize() * 4.0F);
		if (entry.Map.empty()) {
			ImGui::TextWrapped("Any map it fits: you pick one on the planet.");
		} else if (!entry.MapFound) {
			ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.4F, 1.0F), "%s", entry.Map.c_str());
			ImGui::TextWrapped("No map of that name is loaded (its mod may be off, or the map renamed), so it's offered on any map it fits.");
		} else {
			ImGui::TextUnformatted(entry.Map.c_str());
		}
		ImGui::TextDisabled("Kind");
		ImGui::SameLine(ImGui::GetFontSize() * 4.0F);
		ImGui::TextUnformatted(entry.Kind.c_str());
		ImGui::Spacing();
		if (!entry.Description.empty()) {
			ImGui::TextWrapped("%s", entry.Description.c_str());
		}
		ImGui::EndChild();
		if (ToolUI::Button("Set up and play", ImVec2(-1.0F, playHeight))) {
			PlayPicked();
		}
		ImGui::EndChild();
	}
} // namespace

void StudioScenarios::Open() {
	ReadScenarios();
	s_Screen.Open = true;
	s_Screen.Back = false;
	s_Screen.Play.clear();
	s_Screen.FiltersChanged = true;
}

void StudioScenarios::Close() {
	s_Screen.Open = false;
}

bool StudioScenarios::IsOpen() {
	return s_Screen.Open;
}

bool StudioScenarios::TakeBack() {
	const bool back = s_Screen.Back;
	s_Screen.Back = false;
	return back;
}

std::string StudioScenarios::TakePlay() {
	std::string play = std::move(s_Screen.Play);
	s_Screen.Play.clear();
	if (!play.empty()) {
		s_Screen.Pending = play;
	}
	return play;
}

std::string StudioScenarios::TakePending() {
	std::string pending = std::move(s_Screen.Pending);
	s_Screen.Pending.clear();
	return pending;
}

void StudioScenarios::DrawGUI() {
	if (!s_Screen.Open || !g_MenuMan.GetIsInMenuScreen()) {
		return;
	}
	// (The menu's own pointer is under the window: ImGui draws one over it.)
	ImGui::GetIO().MouseDrawCursor = true;
	if (s_Screen.FiltersChanged) {
		Filter();
	}
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float margin = std::floor(std::min(viewport->Size.x, viewport->Size.y) * 0.03F);
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + margin, viewport->Pos.y + margin));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - margin * 2.0F, viewport->Size.y - margin * 2.0F));
	if (ImGui::Begin("Studio Scenarios: every scenario, its map and its mod###StudioScenarios", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
		const ImGuiStyle& style = ImGui::GetStyle();
		const float backWidth = ImGui::CalcTextSize("Back").x + style.FramePadding.x * 4.0F;
		if (ToolUI::Button("Back", ImVec2(backWidth, 0.0F)) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput)) {
			s_Screen.Back = true;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0F);
		if (ImGui::InputTextWithHint("##scenarioSearch", "Search: scenario, map or mod", s_Screen.Search, sizeof(s_Screen.Search))) {
			s_Screen.FiltersChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Only mods' scenarios", &s_Screen.OnlyMods)) {
			s_Screen.FiltersChanged = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%d of %d scenarios", static_cast<int>(s_Screen.Shown.size()), static_cast<int>(s_Screen.Scenarios.size()));
		if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().WantTextInput) {
			PlayPicked();
		}
		ImVec2 room = ImGui::GetContentRegionAvail();
		const float detailsWidth = std::floor(std::clamp(room.x * 0.34F, std::min(room.x * 0.5F, ImGui::GetFontSize() * 18.0F), ImGui::GetFontSize() * 36.0F));
		List(room.x - detailsWidth - style.ItemSpacing.x, room.y);
		ImGui::SameLine();
		Details(detailsWidth, room.y);
	}
	ImGui::End();
}
