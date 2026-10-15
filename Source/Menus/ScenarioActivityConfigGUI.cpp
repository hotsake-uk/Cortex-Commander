#include "ScenarioActivityConfigGUI.h"
#include "GUISound.h"
#include "PostProcessMan.h"

#include "PresetMan.h"
#include "SettingsMan.h"
#include "UInputMan.h"
#include "FrameMan.h"
#include "SceneMan.h"
#include "LuaMan.h"

#include "DataModule.h"
#include "GameActivity.h"
#include "Scene.h"

#include "GUI.h"
#include "AllegroBitmap.h"
#include "GUICollectionBox.h"
#include "GUIComboBox.h"
#include "GUICheckbox.h"
#include "GUIButton.h"
#include "GUILabel.h"
#include "GUISlider.h"
#include "GUIListBox.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace RTE;

namespace {
	constexpr int c_PickSeveralIndex = -3; //!< The ExtraIndex of the "-Pick several-" item in a team's Tech ComboBox.
} // namespace

ScenarioActivityConfigGUI::ScenarioActivityConfigGUI(GUIControlManager* parentControlManager) :
    m_GUIControlManager(parentControlManager) {
	m_ActivityConfigBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("CollectionBoxActivityConfig"));
	m_ActivityConfigBox->CenterInParent(true, true);
	m_ActivityConfigBox->SetVisible(false);

	m_CancelConfigButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonCancelConfig"));

	m_ActivityDifficultyLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelActivityDifficultyValue"));
	m_ActivityDifficultySlider = dynamic_cast<GUISlider*>(m_GUIControlManager->GetControl("SliderActivityDifficulty"));
	m_StartingGoldLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelStartingGoldValue"));
	m_StartingGoldSlider = dynamic_cast<GUISlider*>(m_GUIControlManager->GetControl("SliderStartingGold"));

	m_RequireClearPathToOrbitCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxRequireClearPathToOrbit"));
	m_FogOfWarCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxFogOfWar"));
	m_DeployUnitsCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxDeployUnits"));
	// Optional in custom layouts.
	m_TimeOfDayButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonTimeOfDay"));
	m_WeatherButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonWeather"));
	UpdateAtmosphereButtons();

	m_PlayersAndTeamsConfigBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("CollectionBoxPlayersAndTeamsConfig"));
	m_TeamIconBoxes[TeamRows::DisabledTeam] = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("CollectionBoxDisabledTeamIcon"));
	m_TeamNameLabels[TeamRows::DisabledTeam] = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelDisabledTeam"));

	GUILabel* teamTechLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelTeamTech"));
	for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
		std::string teamNumber = std::to_string(team + 1);
		m_TeamIconBoxes[team] = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("CollectionBoxTeam" + teamNumber + "Icon"));
		m_TeamNameLabels[team] = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelTeam" + teamNumber));

		m_TeamTechComboBoxes[team] = dynamic_cast<GUIComboBox*>(m_GUIControlManager->GetControl("ComboBoxTeam" + teamNumber + "Tech"));
		m_TeamTechComboBoxes[team]->Move(teamTechLabel->GetXPos(), teamTechLabel->GetYPos() + teamTechLabel->GetHeight() + 5 + (25 * (team + 1)));
		m_TeamTechComboBoxes[team]->SetVisible(false);

		m_TeamAISkillSliders[team] = dynamic_cast<GUISlider*>(m_GUIControlManager->GetControl("SliderTeam" + teamNumber + "AISkill"));
		m_TeamAISkillSliders[team]->SetValue(Activity::AISkillSetting::DefaultSkill);
		m_TeamAISkillLabels[team] = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelTeam" + teamNumber + "AISkill"));
		m_TeamAISkillLabels[team]->SetText(Activity::GetAISkillString(m_TeamAISkillSliders[team]->GetValue()));
	}
	for (int player = Players::PlayerOne; player < PlayerColumns::PlayerColumnCount; ++player) {
		for (int team = Activity::Teams::TeamOne; team < TeamRows::TeamRowCount; ++team) {
			m_PlayerBoxes[player][team] = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("P" + std::to_string(player + 1) + "T" + std::to_string(team + 1) + "Box"));
		}
	}
	m_CPULockLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelCPUTeamLock"));
	m_StartErrorLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelStartError"));
	m_StartGameButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonStartGame"));

	// Optional in custom layouts: without it, a team picks one faction as before.
	m_FactionPicksBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("CollectionBoxFactionPicks"));
	m_FactionPicksLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelFactionPicks"));
	m_FactionPicksList = dynamic_cast<GUIListBox*>(m_GUIControlManager->GetControl("ListBoxFactionPicks"));
	m_FactionPicksDoneButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonFactionPicksDone"));
	if (!m_FactionPicksBox || !m_FactionPicksLabel || !m_FactionPicksList || !m_FactionPicksDoneButton) {
		m_FactionPicksBox = nullptr;
	} else {
		m_FactionPicksBox->SetVisible(false);
	}

	// Optional in custom layouts: without it, scenarios run on their defaults and the AI may use flying craft.
	m_ScenarioSettingsButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonScenarioSettings"));
	m_ScenarioSettingsBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("CollectionBoxScenarioSettings"));
	m_ScenarioSettingsLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelScenarioSettings"));
	m_ScenarioSettingsList = dynamic_cast<GUIListBox*>(m_GUIControlManager->GetControl("ListBoxScenarioSettings"));
	m_ScenarioSettingValueLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelScenarioSettingValue"));
	m_ScenarioSettingSlider = dynamic_cast<GUISlider*>(m_GUIControlManager->GetControl("SliderScenarioSetting"));
	m_ScenarioSettingsResetButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonScenarioSettingsReset"));
	m_ScenarioSettingsDoneButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonScenarioSettingsDone"));
	if (!m_ScenarioSettingsButton || !m_ScenarioSettingsBox || !m_ScenarioSettingsLabel || !m_ScenarioSettingsList || !m_ScenarioSettingValueLabel || !m_ScenarioSettingSlider || !m_ScenarioSettingsResetButton || !m_ScenarioSettingsDoneButton) {
		if (m_ScenarioSettingsButton) {
			m_ScenarioSettingsButton->SetVisible(false);
		}
		if (m_ScenarioSettingsBox) {
			m_ScenarioSettingsBox->SetVisible(false);
		}
		m_ScenarioSettingsBox = nullptr;
	} else {
		m_ScenarioSettingsBox->SetVisible(false);
	}
}

namespace {
	constexpr int c_FlyingCraftItemIndex = -1; //!< The ExtraIndex of the flying craft switch in the scenario settings list (the scenario's settings are 0 and up).
} // namespace

void ScenarioActivityConfigGUI::LoadScenarioSettingValues() {
	if (m_ScenarioSettingValuesLoaded) {
		return;
	}
	m_ScenarioSettingValuesLoaded = true;
	m_ScenarioFlyingCraft = g_SettingsMan.ScenarioFlyingCraft();
	// "Maginot Defence|AttackersPerCraft=3|SpawnInterval=0.5;Wave Defense|WaveSize=2"
	std::istringstream scenarios(g_SettingsMan.ScenarioSettingValues());
	std::string scenario;
	while (std::getline(scenarios, scenario, ';')) {
		std::istringstream fields(scenario);
		std::string scenarioName;
		if (!std::getline(fields, scenarioName, '|') || scenarioName.empty()) {
			continue;
		}
		std::string field;
		while (std::getline(fields, field, '|')) {
			if (size_t equals = field.find('='); equals != std::string::npos && equals > 0) {
				try {
					m_ScenarioSettingValues[scenarioName][field.substr(0, equals)] = std::stof(field.substr(equals + 1));
				} catch (const std::exception&) {
				}
			}
		}
	}
}

void ScenarioActivityConfigGUI::SaveScenarioSettingValues() const {
	std::string values;
	for (const auto& [scenarioName, settings]: m_ScenarioSettingValues) {
		if (settings.empty()) {
			continue;
		}
		values += (values.empty() ? "" : ";") + scenarioName;
		for (const auto& [key, value]: settings) {
			std::ostringstream number;
			number << value;
			values += "|" + key + "=" + number.str();
		}
	}
	g_SettingsMan.SetScenarioSettingValues(values);
	g_SettingsMan.SetScenarioFlyingCraft(m_ScenarioFlyingCraft);
}

float ScenarioActivityConfigGUI::GetScenarioSettingValue(const GameActivity::ScenarioSetting& setting) const {
	if (m_SelectedActivity) {
		if (auto scenario = m_ScenarioSettingValues.find(m_SelectedActivity->GetPresetName()); scenario != m_ScenarioSettingValues.end()) {
			if (auto value = scenario->second.find(setting.Key); value != scenario->second.end()) {
				if (setting.AllowAuto && value->second == GameActivity::c_ScenarioSettingAuto) {
					return value->second;
				}
				return std::clamp(value->second, setting.Minimum, setting.Maximum);
			}
		}
	}
	return setting.Default;
}

void ScenarioActivityConfigGUI::SetScenarioSettingValue(const GameActivity::ScenarioSetting& setting, float value) {
	if (!m_SelectedActivity) {
		return;
	}
	std::map<std::string, float>& settings = m_ScenarioSettingValues[m_SelectedActivity->GetPresetName()];
	if (std::abs(value - setting.Default) < 0.0001F) {
		settings.erase(setting.Key);
	} else {
		settings[setting.Key] = value;
	}
}

std::string ScenarioActivityConfigGUI::ScenarioSettingValueText(const GameActivity::ScenarioSetting& setting, float value) {
	if (setting.AllowAuto && value == GameActivity::c_ScenarioSettingAuto) {
		return "Auto";
	}
	std::ostringstream text;
	text << std::fixed;
	if (setting.Unit == "Multiplier") {
		text << std::setprecision(2) << "x" << value;
	} else if (setting.Unit == "Toggle") {
		return value > 0.5F ? "On" : "Off";
	} else {
		// Whole numbers without decimals, others with one.
		const bool whole = std::abs(value - std::round(value)) < 0.001F;
		text << std::setprecision(whole ? 0 : 1) << value;
		if (setting.Unit == "Seconds") {
			text << " s";
		} else if (setting.Unit == "Minutes") {
			text << " min";
		} else if (setting.Unit == "Percent") {
			text << "%";
		}
	}
	return text.str();
}

void ScenarioActivityConfigGUI::FillScenarioSettingsList() {
	if (!m_ScenarioSettingsBox || !m_SelectedActivity) {
		return;
	}
	int scroll = m_ScenarioSettingsList->GetScrollVerticalValue();
	m_ScenarioSettingsList->ClearList();
	m_ScenarioSettingsList->AddItem("Flying craft for the AI", m_ScenarioFlyingCraft ? "On *" : "Off", nullptr, nullptr, c_FlyingCraftItemIndex);
	const std::vector<GameActivity::ScenarioSetting>& settings = m_SelectedActivity->GetScenarioSettings();
	for (int i = 0; i < static_cast<int>(settings.size()); ++i) {
		float value = GetScenarioSettingValue(settings[i]);
		bool changed = std::abs(value - settings[i].Default) >= 0.0001F;
		m_ScenarioSettingsList->AddItem(settings[i].Label, ScenarioSettingValueText(settings[i], value) + (changed ? " *" : ""), nullptr, nullptr, i);
	}
	if (m_ScenarioSettingSelected >= 0) {
		m_ScenarioSettingsList->SetSelectedIndex(m_ScenarioSettingSelected + 1);
	}
	m_ScenarioSettingsList->ScrollTo(scroll);
}

void ScenarioActivityConfigGUI::UpdateScenarioSettingSlider() {
	if (!m_ScenarioSettingsBox || !m_SelectedActivity) {
		return;
	}
	const std::vector<GameActivity::ScenarioSetting>& settings = m_SelectedActivity->GetScenarioSettings();
	if (m_ScenarioSettingSelected < 0 || m_ScenarioSettingSelected >= static_cast<int>(settings.size())) {
		m_ScenarioSettingSelected = -1;
		m_ScenarioSettingSlider->SetVisible(false);
		m_ScenarioSettingValueLabel->SetText(settings.empty() ? "This scenario has no settings of its own." : "Click a setting above to change it.");
		return;
	}
	const GameActivity::ScenarioSetting& setting = settings[m_ScenarioSettingSelected];
	const int steps = std::max(1, static_cast<int>(std::round((setting.Maximum - setting.Minimum) / setting.Step)));
	const int autoNotch = setting.AllowAuto ? 1 : 0;
	float value = GetScenarioSettingValue(setting);
	int position = (setting.AllowAuto && value == GameActivity::c_ScenarioSettingAuto) ? 0 : autoNotch + std::clamp(static_cast<int>(std::round((value - setting.Minimum) / setting.Step)), 0, steps);
	m_ScenarioSettingSlider->SetMinimum(0);
	m_ScenarioSettingSlider->SetMaximum(steps + autoNotch);
	m_ScenarioSettingSlider->SetValue(position);
	m_ScenarioSettingSlider->SetVisible(true);
	m_ScenarioSettingValueLabel->SetText(setting.Label + ": " + ScenarioSettingValueText(setting, value) + "   (default " + ScenarioSettingValueText(setting, setting.Default) + ")");
}

void ScenarioActivityConfigGUI::UpdateScenarioSettingsButton() {
	if (!m_ScenarioSettingsBox || !m_SelectedActivity) {
		return;
	}
	bool changed = m_ScenarioFlyingCraft;
	if (auto scenario = m_ScenarioSettingValues.find(m_SelectedActivity->GetPresetName()); scenario != m_ScenarioSettingValues.end()) {
		for (const GameActivity::ScenarioSetting& setting: m_SelectedActivity->GetScenarioSettings()) {
			changed = changed || scenario->second.count(setting.Key) > 0;
		}
	}
	m_ScenarioSettingsButton->SetText(changed ? "Scenario settings (changed)" : "Scenario settings...");
}

void ScenarioActivityConfigGUI::OpenScenarioSettings() {
	if (!m_ScenarioSettingsBox || !m_SelectedActivity) {
		return;
	}
	if (m_FactionPicksBox && m_FactionPicksTeam >= 0) {
		CloseFactionPicks();
	}
	m_ScenarioSettingsLabel->SetText("Settings: " + m_SelectedActivity->GetPresetName());
	// Over the middle of the setup box.
	m_ScenarioSettingsBox->Move(m_ActivityConfigBox->GetXPos() + (m_ActivityConfigBox->GetWidth() - m_ScenarioSettingsBox->GetWidth()) / 2, m_ActivityConfigBox->GetYPos() + (m_ActivityConfigBox->GetHeight() - m_ScenarioSettingsBox->GetHeight()) / 2);
	m_ScenarioSettingsBox->SetVisible(true);
	m_ScenarioSettingSelected = -1;
	m_ScenarioSettingsList->ScrollToTop();
	FillScenarioSettingsList();
	UpdateScenarioSettingSlider();
}

void ScenarioActivityConfigGUI::CloseScenarioSettings() {
	if (!m_ScenarioSettingsBox || !m_ScenarioSettingsBox->GetVisible()) {
		return;
	}
	m_ScenarioSettingsBox->SetVisible(false);
	m_ScenarioSettingSelected = -1;
	SaveScenarioSettingValues();
	UpdateScenarioSettingsButton();
}

void ScenarioActivityConfigGUI::LoadFactionPicks() {
	if (m_FactionPicksLoaded) {
		return;
	}
	m_FactionPicksLoaded = true;
	// "1:Coalition.rte|Ronin.rte;0:Imperatus.rte;0:;0:"
	std::istringstream teams(g_SettingsMan.ScenarioFactionPicks());
	std::string entry;
	for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount && std::getline(teams, entry, ';'); ++team) {
		m_TeamUsedFactionPicks[team] = entry.size() >= 2 && entry[0] == '1' && entry[1] == ':';
		std::istringstream names(entry.size() >= 2 ? entry.substr(2) : "");
		std::string name;
		while (std::getline(names, name, '|')) {
			if (!name.empty()) {
				m_TeamFactionPicks[team].push_back(name);
			}
		}
	}
}

void ScenarioActivityConfigGUI::SaveFactionPicks() const {
	std::string picks;
	for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
		picks += (team > 0 ? ";" : "") + std::string(m_TeamUsedFactionPicks[team] ? "1:" : "0:");
		for (size_t i = 0; i < m_TeamFactionPicks[team].size(); ++i) {
			picks += (i > 0 ? "|" : "") + m_TeamFactionPicks[team][i];
		}
	}
	g_SettingsMan.SetScenarioFactionPicks(picks);
}

void ScenarioActivityConfigGUI::UpdateSeveralItemText(int team) {
	GUIListPanel* list = m_TeamTechComboBoxes.at(team)->GetListPanel();
	for (int i = 0; i < static_cast<int>(list->GetItemList()->size()); ++i) {
		GUIListPanel::Item* item = list->GetItem(i);
		if (item->m_ExtraIndex == c_PickSeveralIndex) {
			// (Only factions still loaded count.)
			int ticked = 0;
			for (const std::string& name: m_TeamFactionPicks[team]) {
				const DataModule* dataModule = g_PresetMan.GetDataModule(g_PresetMan.GetModuleID(name));
				ticked += dataModule && dataModule->IsFaction() ? 1 : 0;
			}
			GUIListPanel::Item changed = *item;
			changed.m_Name = ticked == 0 ? "-Pick several-" : "-" + std::to_string(ticked) + (ticked == 1 ? " faction-" : " factions-");
			list->SetItemValues(i, changed);
			if (m_TeamTechComboBoxes.at(team)->GetSelectedIndex() == i) {
				m_TeamTechComboBoxes.at(team)->SetSelectedIndex(i);
			}
			return;
		}
	}
}

void ScenarioActivityConfigGUI::FillFactionPicksList() {
	if (!m_FactionPicksBox || m_FactionPicksTeam < 0) {
		return;
	}
	int scroll = m_FactionPicksList->GetScrollVerticalValue();
	m_FactionPicksList->ClearList();
	const std::vector<std::string>& picks = m_TeamFactionPicks[m_FactionPicksTeam];
	for (int moduleID = 0; moduleID < g_PresetMan.GetTotalModuleCount(); ++moduleID) {
		if (const DataModule* dataModule = g_PresetMan.GetDataModule(moduleID); dataModule && dataModule->IsFaction()) {
			bool ticked = std::find_if(picks.begin(), picks.end(), [moduleID](const std::string& name) { return g_PresetMan.GetModuleID(name) == moduleID; }) != picks.end();
			m_FactionPicksList->AddItem(std::string(ticked ? "[X] " : "[  ] ") + dataModule->GetFriendlyName(), "", nullptr, nullptr, moduleID);
		}
	}
	m_FactionPicksList->ScrollTo(scroll);
}

void ScenarioActivityConfigGUI::OpenFactionPicks(int team) {
	if (!m_FactionPicksBox) {
		return;
	}
	m_FactionPicksTeam = team;
	m_FactionPicksLabel->SetText("Factions: " + m_SelectedActivity->GetTeamName(team));
	// Beside the team's Tech ComboBox, to its left, kept on the screen.
	const GUIComboBox* comboBox = m_TeamTechComboBoxes.at(team);
	int x = std::max(0, comboBox->GetXPos() - m_FactionPicksBox->GetWidth() - 4);
	int y = std::max(0, std::min(comboBox->GetYPos() - 20, m_ActivityConfigBox->GetYPos() + m_ActivityConfigBox->GetHeight() - m_FactionPicksBox->GetHeight()));
	m_FactionPicksBox->Move(x, y);
	m_FactionPicksBox->SetVisible(true);
	m_FactionPicksList->ScrollToTop();
	FillFactionPicksList();
}

void ScenarioActivityConfigGUI::CloseFactionPicks() {
	if (!m_FactionPicksBox || m_FactionPicksTeam < 0) {
		return;
	}
	int team = m_FactionPicksTeam;
	m_FactionPicksTeam = -1;
	m_FactionPicksBox->SetVisible(false);
	UpdateSeveralItemText(team);
	SaveFactionPicks();
}

void ScenarioActivityConfigGUI::PopulateTechComboBoxes() {
	for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
		m_TeamTechComboBoxes[team]->GetListPanel()->ClearList();
		m_TeamTechComboBoxes[team]->GetListPanel()->AddItem("-All-", "", nullptr, nullptr, -2);
		m_TeamTechComboBoxes[team]->GetListPanel()->AddItem("-Random-", "", nullptr, nullptr, -1);
		if (m_FactionPicksBox) {
			m_TeamTechComboBoxes[team]->GetListPanel()->AddItem("-Pick several-", "", nullptr, nullptr, c_PickSeveralIndex);
		}
		m_TeamTechComboBoxes[team]->SetSelectedIndex(0);
	}
	for (int moduleID = 0; moduleID < g_PresetMan.GetTotalModuleCount(); ++moduleID) {
		if (const DataModule* dataModule = g_PresetMan.GetDataModule(moduleID)) {
			if (dataModule->IsFaction()) {
				for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
					m_TeamTechComboBoxes[team]->GetListPanel()->AddItem(dataModule->GetFriendlyName(), "", nullptr, nullptr, moduleID);
				}
			}
		}
	}
	for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
		m_TeamTechComboBoxes[team]->GetListPanel()->ScrollToTop();
		if (m_FactionPicksBox) {
			UpdateSeveralItemText(team);
		}
	}
	m_TechListMedievalUnitsDisabled = g_SettingsMan.MedievalUnitsDisabled();
	m_TechListFetched = true;
}

bool ScenarioActivityConfigGUI::IsEnabled() const {
	return m_ActivityConfigBox->GetEnabled() && m_ActivityConfigBox->GetVisible();
}

void ScenarioActivityConfigGUI::SetEnabled(bool enable, const Activity* selectedActivity, Scene* selectedScene) {
	m_ActivityConfigBox->SetEnabled(enable);
	m_ActivityConfigBox->SetVisible(enable);

	bool selectingPreviousActivityWithManuallyAdjustedGold = m_StartingGoldAdjustedManually && m_PreviouslySelectedActivity == selectedActivity;
	if (enable) {
		m_SelectedActivity = dynamic_cast<const GameActivity*>(selectedActivity);
		m_SelectedScene = selectedScene;
		RTEAssert(m_SelectedActivity && m_SelectedScene, "Trying to start a scenario game without an Activity or a Scene!");
	} else {
		m_PreviouslySelectedActivity = m_SelectedActivity;
		m_SelectedActivity = nullptr;
		m_SelectedScene = nullptr;
	}
	// The tech select ComboBoxes aren't children of the config box (dirty hack to allow the drop-down list to extend beyond the parent box bounds without clipping) so we need to set their visibility separately.
	for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
		m_TeamTechComboBoxes[team]->SetVisible(enable);
	}
	if (m_FactionPicksBox) {
		m_FactionPicksTeam = -1;
		m_FactionPicksBox->SetVisible(false);
	}
	if (m_ScenarioSettingsBox) {
		m_ScenarioSettingsBox->SetVisible(false);
		m_ScenarioSettingSelected = -1;
	}
	if (enable && m_SelectedActivity && m_SelectedScene) {
		LoadFactionPicks();
		if (m_ScenarioSettingsBox) {
			LoadScenarioSettingValues();
			UpdateScenarioSettingsButton();
		}
		if (!m_TechListFetched || m_TechListMedievalUnitsDisabled != g_SettingsMan.MedievalUnitsDisabled()) {
			PopulateTechComboBoxes();
		}

		int startingGoldOverride = selectingPreviousActivityWithManuallyAdjustedGold ? m_StartingGoldSlider->GetValue() : -1;
		ResetActivityConfigBox();

		if (startingGoldOverride >= 0) {
			m_StartingGoldSlider->SetValue(startingGoldOverride);
			m_StartingGoldAdjustedManually = true;
			UpdateStartingGoldSliderAndLabel();
		}
	}
}

void ScenarioActivityConfigGUI::ResetActivityConfigBox() {
	m_ActivityDifficultyLabel->SetText(" " + Activity::GetDifficultyString(m_ActivityDifficultySlider->GetValue()));

	m_StartingGoldAdjustedManually = false;
	UpdateStartingGoldSliderAndLabel();
	m_StartingGoldSlider->SetEnabled(m_SelectedActivity->GetGoldSwitchEnabled());

	m_FogOfWarCheckbox->SetCheck(m_SelectedActivity->GetDefaultFogOfWar() > 0);
	m_FogOfWarCheckbox->SetEnabled(m_SelectedActivity->GetFogOfWarSwitchEnabled());

	m_RequireClearPathToOrbitCheckbox->SetCheck(m_SelectedActivity->GetDefaultRequireClearPathToOrbit() > 0);
	m_RequireClearPathToOrbitCheckbox->SetEnabled(m_SelectedActivity->GetRequireClearPathToOrbitSwitchEnabled());

	m_DeployUnitsCheckbox->SetCheck(m_SelectedActivity->GetDefaultDeployUnits() > 0);
	m_DeployUnitsCheckbox->SetEnabled(m_SelectedActivity->GetDeployUnitsSwitchEnabled());

	m_StartGameButton->SetVisible(false);
	m_StartErrorLabel->SetVisible(true);

	for (int player = Players::PlayerOne; player < PlayerColumns::PlayerColumnCount; ++player) {
		for (int team = Activity::Teams::TeamOne; team < TeamRows::TeamRowCount; ++team) {
			m_PlayerBoxes[player][team]->SetDrawType(GUICollectionBox::Color);
			m_PlayerBoxes[player][team]->SetDrawColor(c_GUIColorBlue);
		}
		if (player < Players::MaxPlayerCount) {
			if (const Icon* playerDeviceIcon = g_UInputMan.GetSchemeIcon(player)) {
				m_PlayerBoxes[player][TeamRows::DisabledTeam]->SetDrawImage(new AllegroBitmap(playerDeviceIcon->GetBitmaps32()[0]));
			}
			m_PlayerBoxes[player][TeamRows::DisabledTeam]->SetDrawType(GUICollectionBox::Image);
		} else {
			int cpuInitialTeam = TeamRows::DisabledTeam;
			m_LockedCPUTeam = m_SelectedActivity->GetCPUTeam();
			if (m_LockedCPUTeam != Activity::Teams::NoTeam) {
				cpuInitialTeam = m_LockedCPUTeam;
				m_CPULockLabel->SetPositionAbs(m_CPULockLabel->GetXPos(), m_TeamNameLabels.at(m_LockedCPUTeam)->GetYPos());
				m_CPULockLabel->SetVisible(true);
			} else {
				m_CPULockLabel->SetVisible(false);
			}
			m_PlayerBoxes.at(player).at(cpuInitialTeam)->SetDrawType(GUICollectionBox::Image);
			if (const Icon* cpuIcon = dynamic_cast<const Icon*>(g_PresetMan.GetEntityPreset("Icon", "Device CPU"))) {
				m_PlayerBoxes.at(PlayerColumns::PlayerCPU).at(cpuInitialTeam)->SetDrawImage(new AllegroBitmap(cpuIcon->GetBitmaps32()[0]));
			}
		}
	}

	for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
		const Icon* teamIcon = nullptr;
		if (m_SelectedActivity->TeamActive(team)) {
			teamIcon = m_SelectedActivity->GetTeamIcon(team);
			if (!teamIcon) {
				teamIcon = dynamic_cast<const Icon*>(g_PresetMan.GetEntityPreset("Icon", "Team " + std::to_string(team + 1) + " Default"));
			}
			m_TeamNameLabels.at(team)->SetText(m_SelectedActivity->GetTeamName(team));
		} else {
			teamIcon = dynamic_cast<const Icon*>(g_PresetMan.GetEntityPreset("Icon", "Locked Team"));
			m_TeamNameLabels.at(team)->SetText("Unavailable");
		}
		if (teamIcon) {
			m_TeamIconBoxes.at(team)->SetDrawImage(new AllegroBitmap(teamIcon->GetBitmaps32()[0]));
		}

		m_TeamTechComboBoxes.at(team)->SetVisible(m_SelectedActivity->TeamActive(team));

		std::string teamModule = m_SelectedActivity->GetTeamTech(team);
		int teamModuleID = g_PresetMan.GetModuleID(teamModule);

		if (teamModuleID != -1) {
			auto items = m_TeamTechComboBoxes.at(team)->GetListPanel()->GetItemList();
			for (int i = 0; i < items->size(); i++) {
				if (teamModuleID == items->at(i)->m_ExtraIndex) {
					teamModuleID = i;
				}
			}
			m_TeamTechComboBoxes.at(team)->SetSelectedIndex(teamModuleID);
		} else {
			m_TeamTechComboBoxes.at(team)->SetSelectedIndex(0);
		}
		// A team that last started on several factions is set to them again, where the activity lets its tech be changed.
		if (m_FactionPicksBox && m_TeamUsedFactionPicks[team] && !m_TeamFactionPicks[team].empty() && m_SelectedActivity->GetTeamTechSwitchEnabled(team)) {
			auto items = m_TeamTechComboBoxes.at(team)->GetListPanel()->GetItemList();
			for (int i = 0; i < static_cast<int>(items->size()); i++) {
				if (items->at(i)->m_ExtraIndex == c_PickSeveralIndex) {
					m_TeamTechComboBoxes.at(team)->SetSelectedIndex(i);
				}
			}
		}

		m_TeamTechComboBoxes.at(team)->SetEnabled(m_SelectedActivity->GetTeamTechSwitchEnabled(team));

		m_TeamAISkillSliders.at(team)->SetVisible(m_SelectedActivity->TeamActive(team));
		m_TeamAISkillLabels.at(team)->SetVisible(m_SelectedActivity->TeamActive(team));
	}
	if (const Icon* disabledTeamIcon = dynamic_cast<const Icon*>(g_PresetMan.GetEntityPreset("Icon", "Disabled Team"))) {
		m_TeamIconBoxes.at(TeamRows::DisabledTeam)->SetDrawImage(new AllegroBitmap(disabledTeamIcon->GetBitmaps32()[0]));
	}
}

namespace {
	struct AtmosphereChoice {
		const char* Name;
		float Value;
	};
	constexpr AtmosphereChoice c_TimeChoices[] = {{"Scene default", -1.0F}, {"Dawn", 6.5F}, {"Noon", 12.0F}, {"Dusk", 18.8F}, {"Night", 23.0F}};
	constexpr AtmosphereChoice c_WeatherChoices[] = {{"Scene default", -1.0F}, {"Clear", 0.0F}, {"Rain", 1.0F}, {"Snow", 2.0F}, {"Ash fall", 3.0F}, {"Dust storm", 4.0F}};
} // namespace

void ScenarioActivityConfigGUI::UpdateAtmosphereButtons() {
	if (m_TimeOfDayButton) {
		m_TimeOfDayButton->SetText(std::string("Time: ") + c_TimeChoices[m_TimeOfDayChoice].Name);
	}
	if (m_WeatherButton) {
		m_WeatherButton->SetText(std::string("Weather: ") + c_WeatherChoices[m_WeatherChoice].Name);
	}
}

void ScenarioActivityConfigGUI::StartGame() {
	g_PostProcessMan.SetActivityAtmosphere(c_TimeChoices[m_TimeOfDayChoice].Value, static_cast<int>(c_WeatherChoices[m_WeatherChoice].Value));
	GameActivity* gameActivity = dynamic_cast<GameActivity*>(m_SelectedActivity->Clone());

	gameActivity->SetDifficulty(m_ActivityDifficultySlider->GetValue());
	gameActivity->SetStartingGold((m_StartingGoldSlider->GetValue() == m_StartingGoldSlider->GetMaximum()) ? 1000000000 : static_cast<int>(std::floor(m_StartingGoldSlider->GetValue())));

	gameActivity->SetRequireClearPathToOrbit(m_RequireClearPathToOrbitCheckbox->GetCheck());
	gameActivity->SetFogOfWarEnabled(m_FogOfWarCheckbox->GetCheck());
	g_SceneMan.SetSceneToLoad(m_SelectedScene, true, m_DeployUnitsCheckbox->GetCheck());

	gameActivity->ClearPlayers(false);
	for (int player = Players::PlayerOne; player < PlayerColumns::PlayerColumnCount; ++player) {
		for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
			if (m_PlayerBoxes.at(player).at(team)->GetDrawType() == GUICollectionBox::Image) {
				if (player == PlayerColumns::PlayerCPU) {
					gameActivity->SetCPUTeam(team);
				} else {
					gameActivity->AddPlayer(player, true, team, 0);
					break;
				}
			}
		}
	}

	for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
		if (const GUIListPanel::Item* techItem = m_TeamTechComboBoxes.at(team)->GetSelectedItem()) {
			if (techItem->m_ExtraIndex == -2) {
				gameActivity->SetTeamTech(team, "-All-");
			} else if (techItem->m_ExtraIndex == -1) {
				// (The factions follow the -All-, -Random- and -Pick several- items.)
				std::vector<int> factions;
				for (const GUIListPanel::Item* item: *m_TeamTechComboBoxes.at(team)->GetListPanel()->GetItemList()) {
					if (item->m_ExtraIndex >= 0) {
						factions.push_back(item->m_ExtraIndex);
					}
				}
				if (!factions.empty()) {
					gameActivity->SetTeamTech(team, g_PresetMan.GetDataModuleName(factions[RandomNum<int>(0, static_cast<int>(factions.size()) - 1)]));
				}
			} else if (techItem->m_ExtraIndex == c_PickSeveralIndex) {
				// Several factions: each time the team's tech is looked up (a delivery, a unit, the buy menu), one of them.
				std::string several;
				for (const std::string& name: m_TeamFactionPicks[team]) {
					if (const DataModule* dataModule = g_PresetMan.GetDataModule(g_PresetMan.GetModuleID(name)); dataModule && dataModule->IsFaction()) {
						several += (several.empty() ? "" : "|") + name;
					}
				}
				if (several.empty()) {
					gameActivity->SetTeamTech(team, "-All-");
				} else {
					gameActivity->SetTeamTech(team, several);
				}
			} else {
				gameActivity->SetTeamTech(team, g_PresetMan.GetDataModuleName(techItem->m_ExtraIndex));
			}
		}
		gameActivity->SetTeamAISkill(team, (m_TeamAISkillSliders.at(team)->IsEnabled()) ? m_TeamAISkillSliders.at(team)->GetValue() : Activity::AISkillSetting::DefaultSkill);
		if (m_FactionPicksBox && m_SelectedActivity->TeamActive(team) && m_SelectedActivity->GetTeamTechSwitchEnabled(team)) {
			const GUIListPanel::Item* techItem = m_TeamTechComboBoxes.at(team)->GetSelectedItem();
			m_TeamUsedFactionPicks[team] = techItem && techItem->m_ExtraIndex == c_PickSeveralIndex;
		}
	}
	if (m_FactionPicksBox) {
		SaveFactionPicks();
	}
	if (m_ScenarioSettingsBox) {
		gameActivity->SetFlyingCraftAllowed(m_ScenarioFlyingCraft);
		for (const GameActivity::ScenarioSetting& setting: m_SelectedActivity->GetScenarioSettings()) {
			gameActivity->SetScenarioSetting(setting.Key, GetScenarioSettingValue(setting));
		}
		SaveScenarioSettingValues();
	}
	g_LuaMan.FileCloseAll();
	g_ActivityMan.SetStartActivity(gameActivity);
}

bool ScenarioActivityConfigGUI::Update(int mouseX, int mouseY) {
	// (The tick-list of factions lies over the team rows: clicks on it aren't for them.)
	if (m_FactionPicksTeam < 0 && !(m_ScenarioSettingsBox && m_ScenarioSettingsBox->GetVisible())) {
		UpdatePlayerTeamSetupCell(mouseX, mouseY);
	}

	int teamsWithPlayers = 0;
	bool teamWithHumans = false;
	int humansInTeams = 0;
	for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
		bool foundPlayer = false;
		if (m_SelectedActivity->TeamActive(team)) {
			for (int player = Players::PlayerOne; player < PlayerColumns::PlayerColumnCount; ++player) {
				if (m_PlayerBoxes.at(player).at(team)->GetDrawType() == GUICollectionBox::Image) {
					foundPlayer = true;
					if (player != PlayerColumns::PlayerCPU) {
						humansInTeams++;
						teamWithHumans = true;
					}
				}
			}
			if (foundPlayer) {
				teamsWithPlayers++;
			}
		}
	}

	int maxHumanPlayers = m_SelectedActivity->GetMaxPlayerSupport();
	int minTeamsRequired = m_SelectedActivity->GetMinTeamsRequired();

	std::vector humanPlayers = GetHumanPlayers();
	std::string errorMessage = "";
	if (humansInTeams > maxHumanPlayers) {
		errorMessage = "Too many players assigned! Max for this activity is " + std::to_string(maxHumanPlayers);
	} else if (minTeamsRequired > teamsWithPlayers) {
		errorMessage = "Assign players to at least " + std::to_string(minTeamsRequired) + " of the teams!";
	} else if (teamWithHumans == 0) {
		errorMessage = "Assign human players to at least one team!";
	} else if (g_UInputMan.CheckMultiMouseKeyboardEnabled(humanPlayers) && !g_UInputMan.AllPlayerInputDevicesKnown(humanPlayers)) {
		errorMessage = "Some players have not set keyboard or mouse devices. Please go to input settings to configure!";
	}

	m_StartErrorLabel->SetText(errorMessage);
	m_StartErrorLabel->SetVisible(!errorMessage.empty());
	m_StartGameButton->SetVisible(errorMessage.empty());

	if (m_StartGameButton->GetVisible()) {
		m_GUIControlManager->GetManager()->SetFocus(m_StartGameButtonBlinkTimer.AlternateReal(500) ? m_StartGameButton : nullptr);
	}

	return HandleInputEvents();
}

void ScenarioActivityConfigGUI::UpdateStartingGoldSliderAndLabel() {
	if (!m_StartingGoldAdjustedManually) {
		if (m_ActivityDifficultySlider->GetValue() <= Activity::DifficultySetting::CakeDifficulty && m_SelectedActivity->GetDefaultGoldCakeDifficulty() > -1) {
			m_StartingGoldSlider->SetValue(m_SelectedActivity->GetDefaultGoldCakeDifficulty());
		} else if (m_ActivityDifficultySlider->GetValue() <= Activity::DifficultySetting::EasyDifficulty && m_SelectedActivity->GetDefaultGoldEasyDifficulty() > -1) {
			m_StartingGoldSlider->SetValue(m_SelectedActivity->GetDefaultGoldEasyDifficulty());
		} else if (m_ActivityDifficultySlider->GetValue() <= Activity::DifficultySetting::MediumDifficulty && m_SelectedActivity->GetDefaultGoldMediumDifficulty() > -1) {
			m_StartingGoldSlider->SetValue(m_SelectedActivity->GetDefaultGoldMediumDifficulty());
		} else if (m_ActivityDifficultySlider->GetValue() <= Activity::DifficultySetting::HardDifficulty && m_SelectedActivity->GetDefaultGoldHardDifficulty() > -1) {
			m_StartingGoldSlider->SetValue(m_SelectedActivity->GetDefaultGoldHardDifficulty());
		} else if (m_ActivityDifficultySlider->GetValue() <= Activity::DifficultySetting::NutsDifficulty && m_SelectedActivity->GetDefaultGoldNutsDifficulty() > -1) {
			m_StartingGoldSlider->SetValue(m_SelectedActivity->GetDefaultGoldNutsDifficulty());
		} else if (m_ActivityDifficultySlider->GetValue() <= Activity::DifficultySetting::MaxDifficulty && m_SelectedActivity->GetDefaultGoldMaxDifficulty() > -1) {
			m_StartingGoldSlider->SetValue(m_SelectedActivity->GetDefaultGoldMaxDifficulty());
		} else if (m_SelectedActivity->GetDefaultGoldMaxDifficulty() > -1) {
			m_StartingGoldSlider->SetValue(m_SelectedActivity->GetDefaultGoldMaxDifficulty());
		} else {
			m_StartingGoldSlider->SetValue(2000);
		}
	}
	std::string goldString(16, '\0');
	if (m_StartingGoldSlider->GetValue() == m_StartingGoldSlider->GetMaximum()) {
		std::snprintf(goldString.data(), goldString.size(), " %c Infinite", -58);
	} else {
		int startGold = m_StartingGoldSlider->GetValue();
		std::snprintf(goldString.data(), goldString.size(), " %c %d oz", -58, startGold);
	}
	m_StartingGoldLabel->SetText(goldString);
}

void ScenarioActivityConfigGUI::UpdatePlayerTeamSetupCell(int mouseX, int mouseY) {
	if (const GUICollectionBox* hoveredCell = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControlUnderPoint(mouseX, mouseY, m_PlayersAndTeamsConfigBox, 1))) {
		int hoveredPlayer = PlayerColumns::PlayerColumnCount;
		int hoveredTeam = TeamRows::TeamRowCount;
		for (int player = Players::PlayerOne; player < PlayerColumns::PlayerColumnCount; ++player) {
			for (int team = Activity::Teams::TeamOne; team < TeamRows::TeamRowCount; ++team) {
				if (m_PlayerBoxes.at(player).at(team) == hoveredCell) {
					hoveredPlayer = player;
					hoveredTeam = team;
				} else if (m_PlayerBoxes.at(player).at(team)->GetDrawType() == GUICollectionBox::Color) {
					m_PlayerBoxes.at(player).at(team)->SetDrawColor(c_GUIColorBlue);
				}
			}
		}
		if ((m_SelectedActivity->TeamActive(hoveredTeam) || hoveredTeam == TeamRows::DisabledTeam) && hoveredTeam != m_LockedCPUTeam && (m_LockedCPUTeam == Activity::Teams::NoTeam || hoveredPlayer != PlayerColumns::PlayerCPU) && m_PlayerBoxes.at(hoveredPlayer).at(hoveredTeam)->GetDrawType() != GUICollectionBox::Image) {
			if (g_UInputMan.MenuButtonReleased(UInputMan::MenuCursorButtons::MENU_PRIMARY)) {
				HandleClickOnPlayerTeamSetupCell(hoveredPlayer, hoveredTeam);
			} else if (m_PlayerBoxes.at(hoveredPlayer).at(hoveredTeam)->GetDrawType() == GUICollectionBox::Color && m_PlayerBoxes.at(hoveredPlayer).at(hoveredTeam)->GetDrawColor() != c_GUIColorLightBlue) {
				m_PlayerBoxes.at(hoveredPlayer).at(hoveredTeam)->SetDrawColor(c_GUIColorLightBlue);
				g_GUISound.SelectionChangeSound()->Play();
			}
		}
	}
}

void ScenarioActivityConfigGUI::HandleClickOnPlayerTeamSetupCell(int clickedPlayer, int clickedTeam) {
	m_PlayerBoxes.at(clickedPlayer).at(clickedTeam)->SetDrawType(GUICollectionBox::Image);
	const Icon* playerIcon = (clickedPlayer != PlayerColumns::PlayerCPU) ? g_UInputMan.GetSchemeIcon(clickedPlayer) : dynamic_cast<const Icon*>(g_PresetMan.GetEntityPreset("Icon", "Device CPU"));
	if (playerIcon) {
		m_PlayerBoxes.at(clickedPlayer).at(clickedTeam)->SetDrawImage(new AllegroBitmap(playerIcon->GetBitmaps32()[0]));
	}

	if (clickedPlayer == PlayerColumns::PlayerCPU) {
		if (clickedTeam == TeamRows::DisabledTeam) {
			for (int nonClickedTeam = Activity::Teams::TeamOne; nonClickedTeam < TeamRows::DisabledTeam; ++nonClickedTeam) {
				m_PlayerBoxes.at(clickedPlayer).at(nonClickedTeam)->SetDrawType(GUICollectionBox::Color);
				m_PlayerBoxes.at(clickedPlayer).at(nonClickedTeam)->SetDrawColor(c_GUIColorBlue);
			}
		} else {
			m_PlayerBoxes.at(clickedPlayer).at(TeamRows::DisabledTeam)->SetDrawType(GUICollectionBox::Color);
			m_PlayerBoxes.at(clickedPlayer).at(TeamRows::DisabledTeam)->SetDrawColor(c_GUIColorBlue);
		}
	} else {
		for (int nonClickedTeam = Activity::Teams::TeamOne; nonClickedTeam < TeamRows::TeamRowCount; ++nonClickedTeam) {
			if (nonClickedTeam != clickedTeam) {
				m_PlayerBoxes.at(clickedPlayer).at(nonClickedTeam)->SetDrawType(GUICollectionBox::Color);
				m_PlayerBoxes.at(clickedPlayer).at(nonClickedTeam)->SetDrawColor(c_GUIColorBlue);
			}
		}
	}

	// Remove human players from their team row if a CPU player is added to it, and vice - versa for CPU players.
	if (clickedPlayer == PlayerColumns::PlayerCPU && clickedTeam != TeamRows::DisabledTeam) {
		for (int humanPlayer = Players::PlayerOne; humanPlayer < Players::MaxPlayerCount; ++humanPlayer) {
			if (m_PlayerBoxes.at(humanPlayer).at(clickedTeam)->GetDrawType() == GUICollectionBox::Image) {
				m_PlayerBoxes.at(humanPlayer).at(clickedTeam)->SetDrawType(GUICollectionBox::Color);
				m_PlayerBoxes.at(humanPlayer).at(clickedTeam)->SetDrawColor(c_GUIColorBlue);
				m_PlayerBoxes.at(humanPlayer).at(TeamRows::DisabledTeam)->SetDrawType(GUICollectionBox::Image);
				playerIcon = g_UInputMan.GetSchemeIcon(humanPlayer);
				if (playerIcon) {
					m_PlayerBoxes.at(humanPlayer).at(TeamRows::DisabledTeam)->SetDrawImage(new AllegroBitmap(playerIcon->GetBitmaps32()[0]));
				}
			}
		}
	} else if (clickedPlayer != PlayerColumns::PlayerCPU && clickedTeam != TeamRows::DisabledTeam && m_PlayerBoxes.at(PlayerColumns::PlayerCPU).at(clickedTeam)->GetDrawType() == GUICollectionBox::Image) {
		m_PlayerBoxes.at(PlayerColumns::PlayerCPU).at(clickedTeam)->SetDrawType(GUICollectionBox::Color);
		m_PlayerBoxes.at(PlayerColumns::PlayerCPU).at(clickedTeam)->SetDrawColor(c_GUIColorBlue);

		int cpuTeamCount = 0;
		for (int teamToCount = Activity::Teams::TeamOne; teamToCount < TeamRows::DisabledTeam; ++teamToCount) {
			if (m_PlayerBoxes.at(PlayerColumns::PlayerCPU).at(teamToCount)->GetDrawType() == GUICollectionBox::Image) {
				cpuTeamCount++;
			}
		}
		if (cpuTeamCount == 0) {
			m_PlayerBoxes.at(PlayerColumns::PlayerCPU).at(TeamRows::DisabledTeam)->SetDrawType(GUICollectionBox::Image);
			playerIcon = dynamic_cast<const Icon*>(g_PresetMan.GetEntityPreset("Icon", "Device CPU"));
			if (playerIcon) {
				m_PlayerBoxes.at(PlayerColumns::PlayerCPU).at(TeamRows::DisabledTeam)->SetDrawImage(new AllegroBitmap(playerIcon->GetBitmaps32()[0]));
			}
		}
	}
	g_GUISound.FocusChangeSound()->Play();
}

bool ScenarioActivityConfigGUI::HandleInputEvents() {
	GUIEvent guiEvent;
	while (m_GUIControlManager->GetEvent(&guiEvent)) {
		// While the scenario settings panel is open, only its own controls answer.
		if (m_ScenarioSettingsBox && m_ScenarioSettingsBox->GetVisible()) {
			HandleScenarioSettingsEvent(guiEvent);
			continue;
		}
		if (guiEvent.GetType() == GUIEvent::Command) {
			if (guiEvent.GetControl() == m_CancelConfigButton) {
				g_GUISound.BackButtonPressSound()->Play();
				SetEnabled(false);
			} else if (m_TimeOfDayButton && guiEvent.GetControl() == m_TimeOfDayButton) {
				g_GUISound.ButtonPressSound()->Play();
				m_TimeOfDayChoice = (m_TimeOfDayChoice + 1) % static_cast<int>(std::size(c_TimeChoices));
				UpdateAtmosphereButtons();
			} else if (m_WeatherButton && guiEvent.GetControl() == m_WeatherButton) {
				g_GUISound.ButtonPressSound()->Play();
				m_WeatherChoice = (m_WeatherChoice + 1) % static_cast<int>(std::size(c_WeatherChoices));
				UpdateAtmosphereButtons();
			} else if (m_FactionPicksBox && guiEvent.GetControl() == m_FactionPicksDoneButton) {
				g_GUISound.ButtonPressSound()->Play();
				CloseFactionPicks();
			} else if (guiEvent.GetControl() == m_StartGameButton) {
				// Make sure all players have known input devices if multimouse is enabled.
				g_GUISound.ButtonPressSound()->Play();
				StartGame();
				SetEnabled(false);
				return true;
			}
		} else if (guiEvent.GetType() == GUIEvent::Notification) {
			if (guiEvent.GetControl() == m_ActivityDifficultySlider) {
				m_ActivityDifficultyLabel->SetText(" " + Activity::GetDifficultyString(m_ActivityDifficultySlider->GetValue()));
				if (!m_StartingGoldAdjustedManually) {
					UpdateStartingGoldSliderAndLabel();
				}
			} else if (guiEvent.GetControl() == m_StartingGoldSlider) {
				if (guiEvent.GetMsg() == GUISlider::Clicked) {
					m_StartingGoldAdjustedManually = true;
				}
				UpdateStartingGoldSliderAndLabel();
			} else if (m_FactionPicksBox && m_FactionPicksTeam >= 0 && guiEvent.GetControl() == m_FactionPicksList && guiEvent.GetMsg() == GUIListBox::Select) {
				// A click ticks a faction, or unticks it.
				if (const GUIListPanel::Item* item = m_FactionPicksList->GetSelected()) {
					std::vector<std::string>& picks = m_TeamFactionPicks[m_FactionPicksTeam];
					const int moduleID = item->m_ExtraIndex;
					auto ticked = std::find_if(picks.begin(), picks.end(), [moduleID](const std::string& name) { return g_PresetMan.GetModuleID(name) == moduleID; });
					if (ticked != picks.end()) {
						picks.erase(ticked);
					} else {
						picks.push_back(g_PresetMan.GetDataModuleName(moduleID));
					}
					g_GUISound.ItemChangeSound()->Play();
					FillFactionPicksList();
				}
			} else if (m_FactionPicksBox && guiEvent.GetMsg() == GUIComboBox::Closed) {
				for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; team++) {
					if (guiEvent.GetControl() == m_TeamTechComboBoxes.at(team)) {
						const GUIListPanel::Item* techItem = m_TeamTechComboBoxes.at(team)->GetSelectedItem();
						if (techItem && techItem->m_ExtraIndex == c_PickSeveralIndex) {
							OpenFactionPicks(team);
						} else if (m_FactionPicksTeam == team) {
							CloseFactionPicks();
						}
					}
				}
			} else if (guiEvent.GetMsg() == GUISlider::Changed) {
				for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; team++) {
					if (guiEvent.GetControl() == m_TeamAISkillSliders.at(team)) {
						m_TeamAISkillLabels.at(team)->SetText(Activity::GetAISkillString(m_TeamAISkillSliders.at(team)->GetValue()));
					}
				}
			}
		}
	}
	return false;
}

void ScenarioActivityConfigGUI::HandleScenarioSettingsEvent(GUIEvent& guiEvent) {
	const std::vector<GameActivity::ScenarioSetting>& settings = m_SelectedActivity->GetScenarioSettings();
	if (guiEvent.GetType() == GUIEvent::Command) {
		if (guiEvent.GetControl() == m_ScenarioSettingsDoneButton) {
			g_GUISound.ButtonPressSound()->Play();
			CloseScenarioSettings();
		} else if (guiEvent.GetControl() == m_ScenarioSettingsResetButton) {
			g_GUISound.ButtonPressSound()->Play();
			m_ScenarioSettingValues.erase(m_SelectedActivity->GetPresetName());
			m_ScenarioFlyingCraft = false;
			FillScenarioSettingsList();
			UpdateScenarioSettingSlider();
		}
	} else if (guiEvent.GetType() == GUIEvent::Notification) {
		if (guiEvent.GetControl() == m_ScenarioSettingsList && guiEvent.GetMsg() == GUIListBox::Select) {
			if (const GUIListPanel::Item* item = m_ScenarioSettingsList->GetSelected()) {
				if (item->m_ExtraIndex == c_FlyingCraftItemIndex) {
					// A click turns flying craft on or off.
					m_ScenarioFlyingCraft = !m_ScenarioFlyingCraft;
					m_ScenarioSettingSelected = -1;
					g_GUISound.ItemChangeSound()->Play();
					FillScenarioSettingsList();
				} else if (item->m_ExtraIndex != m_ScenarioSettingSelected) {
					m_ScenarioSettingSelected = item->m_ExtraIndex;
					g_GUISound.SelectionChangeSound()->Play();
				}
				UpdateScenarioSettingSlider();
			}
		} else if (guiEvent.GetControl() == m_ScenarioSettingSlider && m_ScenarioSettingSelected >= 0 && m_ScenarioSettingSelected < static_cast<int>(settings.size())) {
			const GameActivity::ScenarioSetting& setting = settings[m_ScenarioSettingSelected];
			const int position = m_ScenarioSettingSlider->GetValue();
			float value;
			if (setting.AllowAuto && position == 0) {
				value = GameActivity::c_ScenarioSettingAuto;
			} else {
				value = std::min(setting.Maximum, setting.Minimum + static_cast<float>(position - (setting.AllowAuto ? 1 : 0)) * setting.Step);
			}
			if (value != GetScenarioSettingValue(setting)) {
				SetScenarioSettingValue(setting, value);
				FillScenarioSettingsList();
			}
			UpdateScenarioSettingSlider();
		}
	}
}

std::vector<int> ScenarioActivityConfigGUI::GetHumanPlayers() {
	std::vector<int> humanPlayers;
	for (int player = Players::PlayerOne; player < PlayerColumns::PlayerColumnCount; ++player) {
		for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
			if (m_PlayerBoxes.at(player).at(team)->GetDrawType() == GUICollectionBox::Image) {
				if (player != PlayerColumns::PlayerCPU) {
					humanPlayers.push_back(player);
					break;
				}
			}
		}
	}
	return humanPlayers;
}

void ScenarioActivityConfigGUI::Draw() {
	m_GUIControlManager->Draw();

	int rectPosY = 50;
	// Apply a colored overlay on top of team rows that are locked or disabled by the Activity and are not modifiable.
	for (int team = Activity::Teams::TeamOne; team < Activity::Teams::MaxTeamCount; ++team) {
		if (!m_SelectedActivity->TeamActive(team) || m_LockedCPUTeam == team) {
			drawing_mode(DRAW_MODE_TRANS, nullptr, 0, 0);
			int blendAmount = 230;
			set_screen_blender(blendAmount, blendAmount, blendAmount, blendAmount);
			rectfill(g_FrameMan.GetBackBuffer32(), m_PlayersAndTeamsConfigBox->GetXPos() + 2, m_PlayersAndTeamsConfigBox->GetYPos() + rectPosY, m_PlayersAndTeamsConfigBox->GetXPos() + m_PlayersAndTeamsConfigBox->GetWidth() - 3, m_PlayersAndTeamsConfigBox->GetYPos() + rectPosY + 25, c_GUIColorDarkBlue);
			drawing_mode(DRAW_MODE_SOLID, nullptr, 0, 0);
		}
		rectPosY += 25;
	}

	int linePosY = 25;
	for (int i = 0; i < 6; ++i) {
		hline(g_FrameMan.GetBackBuffer32(), m_PlayersAndTeamsConfigBox->GetXPos() + 2, m_PlayersAndTeamsConfigBox->GetYPos() + linePosY, m_PlayersAndTeamsConfigBox->GetXPos() + m_PlayersAndTeamsConfigBox->GetWidth() - 2, c_GUIColorLightBlue);
		linePosY += 25;
	}

	// The colored overlay is semi-transparent and is drawn on top of GUI elements, so we need to redraw the relevant elements again so they aren't affected.
	for (int team = Activity::Teams::MaxTeamCount - 1; team >= Activity::Teams::TeamOne; --team) {
		if (m_TeamTechComboBoxes.at(team)->GetVisible()) {
			m_TeamTechComboBoxes.at(team)->Draw(m_GUIControlManager->GetScreen());
			if (m_TeamTechComboBoxes.at(team)->IsDropped()) {
				m_TeamTechComboBoxes.at(team)->GetListPanel()->Draw(m_GUIControlManager->GetScreen());
			}
		}
		if (m_TeamAISkillSliders.at(team)->GetVisible()) {
			m_TeamAISkillSliders.at(team)->Draw(m_GUIControlManager->GetScreen());
			m_TeamAISkillLabels.at(team)->Draw(m_GUIControlManager->GetScreen());
		}
	}
	// The tick-list of factions, over everything else.
	if (m_FactionPicksBox && m_FactionPicksBox->GetVisible()) {
		m_FactionPicksBox->Draw(m_GUIControlManager->GetScreen());
	}
	// The scenario settings panel, over everything else too.
	if (m_ScenarioSettingsBox && m_ScenarioSettingsBox->GetVisible()) {
		m_ScenarioSettingsBox->Draw(m_GUIControlManager->GetScreen());
	}
}
