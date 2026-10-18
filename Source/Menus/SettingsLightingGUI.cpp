#include "SettingsLightingGUI.h"
#include "PostProcessMan.h"
#include "LightingOptions.h"
#include "Weather.h"

#include "GUI.h"
#include "GUICollectionBox.h"
#include "GUIButton.h"
#include "GUICheckbox.h"
#include "GUIComboBox.h"
#include "GUILabel.h"
#include "GUISlider.h"

#include <cmath>
#include <cstdio>

using namespace RTE;

SettingsLightingGUI::SettingsLightingGUI(GUIControlManager* parentControlManager) :
    m_GUIControlManager(parentControlManager), m_Page(0) {
	m_LightingSettingsBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("CollectionBoxLightingSettings"));
	m_PageComboBox = dynamic_cast<GUIComboBox*>(m_GUIControlManager->GetControl("ComboLightingPage"));
	m_PreviousPageButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonLightingPreviousPage"));
	m_NextPageButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonLightingNextPage"));
	m_ResetButton = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonLightingReset"));

	for (const std::string& pageName: LightingOptions::GetPageNames()) {
		m_PageComboBox->AddItem(pageName);
	}
	for (int row = 0; row < c_RowCount; ++row) {
		std::string rowNumber = std::to_string(row + 1);
		m_RowOptions[row] = nullptr;
		m_RowLabels[row] = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelLightingRow" + rowNumber));
		m_RowSliders[row] = dynamic_cast<GUISlider*>(m_GUIControlManager->GetControl("SliderLightingRow" + rowNumber));
		m_RowCheckboxes[row] = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxLightingRow" + rowNumber));
		m_RowValueLabels[row] = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelLightingRowValue" + rowNumber));
	}
	ShowPage(0);
}

void SettingsLightingGUI::SetEnabled(bool enable) {
	m_LightingSettingsBox->SetVisible(enable);
	m_LightingSettingsBox->SetEnabled(enable);
	if (enable) {
		// The Video menu's quality preset, a map or the scenario setup may have changed them since.
		UpdateRows();
	}
}

void SettingsLightingGUI::ShowPage(int page) {
	int pageCount = static_cast<int>(LightingOptions::GetPageNames().size());
	m_Page = ((page % pageCount) + pageCount) % pageCount;
	m_PageComboBox->SetSelectedIndex(m_Page);

	m_RowOptions.fill(nullptr);
	int row = 0;
	for (const LightingOption& option: LightingOptions::GetAll()) {
		if (option.Page == m_Page && row < c_RowCount) {
			m_RowOptions[row++] = &option;
		}
	}
	UpdateRows();
}

void SettingsLightingGUI::UpdateRows() {
	// What's saved: the player's own time of day and weather, not a map's or a game's showing now.
	const LightingSettings settings = g_PostProcessMan.GetLightingSettingsToSave();
	for (int row = 0; row < c_RowCount; ++row) {
		const LightingOption* option = m_RowOptions[row];
		m_RowLabels[row]->SetVisible(option != nullptr);
		m_RowSliders[row]->SetVisible(option && !option->BoolField);
		m_RowCheckboxes[row]->SetVisible(option && option->BoolField);
		m_RowValueLabels[row]->SetVisible(option && !option->BoolField);
		if (!option) {
			continue;
		}
		m_RowLabels[row]->SetText(option->Label);
		float value = LightingOptions::GetValue(*option, settings);
		if (option->BoolField) {
			m_RowCheckboxes[row]->SetCheck(value >= 0.5F);
		} else {
			int sliderMin = 0;
			int sliderMax = 0;
			int position = GetSliderPosition(*option, value, sliderMin, sliderMax);
			m_RowSliders[row]->SetMinimum(sliderMin);
			m_RowSliders[row]->SetMaximum(sliderMax);
			m_RowSliders[row]->SetValue(position);
			m_RowValueLabels[row]->SetText(GetValueText(*option, value));
		}
	}
}

int SettingsLightingGUI::GetSliderPosition(const LightingOption& option, float value, int& sliderMin, int& sliderMax) const {
	if (option.IntField == &LightingSettings::WeatherType) {
		const LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
		sliderMin = 0;
		sliderMax = settings.CustomWeather ? Weather::GetSlotCount() - 1 : Weather::c_BuiltInCount;
		return std::clamp(static_cast<int>(value), sliderMin, sliderMax);
	} else if (option.IntField) {
		sliderMin = static_cast<int>(option.Min);
		sliderMax = static_cast<int>(option.Max);
		return std::clamp(static_cast<int>(value), sliderMin, sliderMax);
	}
	sliderMin = 0;
	sliderMax = c_SliderSteps;
	float range = option.Max - option.Min;
	return range > 0.0F ? static_cast<int>(std::lround(std::clamp((value - option.Min) / range, 0.0F, 1.0F) * static_cast<float>(c_SliderSteps))) : 0;
}

float SettingsLightingGUI::GetSliderValue(const LightingOption& option, int sliderPosition) const {
	if (option.IntField) {
		return static_cast<float>(sliderPosition);
	}
	return option.Min + (option.Max - option.Min) * static_cast<float>(sliderPosition) / static_cast<float>(c_SliderSteps);
}

std::string SettingsLightingGUI::GetValueText(const LightingOption& option, float value) const {
	char text[32];
	if (option.FloatField == &LightingSettings::TimeOfDay) {
		int minutes = static_cast<int>(std::lround(value * 60.0F)) % (24 * 60);
		std::snprintf(text, sizeof(text), "%02d:%02d", minutes / 60, minutes % 60);
	} else if (option.FloatField == &LightingSettings::DayLengthMinutes) {
		if (value <= 0.0F) {
			return "Stopped";
		}
		std::snprintf(text, sizeof(text), "%.1f min", value);
	} else if (option.IntField == &LightingSettings::WeatherType) {
		std::vector<std::string> slotNames = Weather::GetSlotNames();
		int slot = static_cast<int>(value);
		return slot >= 0 && slot < static_cast<int>(slotNames.size()) ? slotNames[slot] : "Clear";
	} else if (option.IntField) {
		std::snprintf(text, sizeof(text), "%d", static_cast<int>(value));
	} else {
		float range = option.Max - option.Min;
		std::snprintf(text, sizeof(text), range >= 50.0F ? "%.0f" : (range >= 5.0F ? "%.1f" : "%.2f"), value);
	}
	return text;
}

void SettingsLightingGUI::ChangeSetting(const LightingOption& option, float value) {
	LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
	LightingSettings& playerAtmosphere = g_PostProcessMan.GetPlayerAtmosphere();
	for (LightingSettings* changing: {&settings, &playerAtmosphere}) {
		LightingOptions::SetValue(option, *changing, value);
		changing->GraphicsQuality = LightingSettings::QualityCustom;
	}
	if (option.IntField == &LightingSettings::WeatherType) {
		// A weather picked here goes by its slot, not a name asked for before the Weather presets were loaded.
		settings.WeatherName.clear();
		playerAtmosphere.WeatherName.clear();
	}
	if (option.IntField == &LightingSettings::WeatherType || option.FloatField == &LightingSettings::WeatherIntensity || option.BoolField == &LightingSettings::OverrideMapWeather || option.BoolField == &LightingSettings::CustomWeather) {
		// Puts the weather it now means up at once: the player's own, or the map's if it has one and the player's isn't on every map.
		g_PostProcessMan.SetOverrideMapWeather(settings.OverrideMapWeather);
	}
}

void SettingsLightingGUI::ResetToDefaults() {
	const LightingSettings defaults;
	LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
	LightingSettings& playerAtmosphere = g_PostProcessMan.GetPlayerAtmosphere();
	int defaultWeather = Weather::FindSlot(defaults.WeatherName);
	for (LightingSettings* changing: {&settings, &playerAtmosphere}) {
		for (const LightingOption& option: LightingOptions::GetAll()) {
			LightingOptions::CopyValue(option, *changing, defaults);
		}
		changing->GraphicsQuality = defaults.GraphicsQuality;
		if (defaultWeather >= 0) {
			changing->WeatherType = defaultWeather;
			changing->WeatherName.clear();
		} else {
			changing->WeatherName = defaults.WeatherName;
		}
	}
	g_PostProcessMan.SetOverrideMapWeather(settings.OverrideMapWeather);
	UpdateRows();
}

void SettingsLightingGUI::HandleInputEvents(GUIEvent& guiEvent) {
	if (guiEvent.GetType() == GUIEvent::Command) {
		if (guiEvent.GetControl() == m_PreviousPageButton) {
			g_GUISound.ButtonPressSound()->Play();
			ShowPage(m_Page - 1);
		} else if (guiEvent.GetControl() == m_NextPageButton) {
			g_GUISound.ButtonPressSound()->Play();
			ShowPage(m_Page + 1);
		} else if (guiEvent.GetControl() == m_ResetButton) {
			g_GUISound.ButtonPressSound()->Play();
			ResetToDefaults();
		}
	} else if (guiEvent.GetType() == GUIEvent::Notification) {
		if (guiEvent.GetControl() == m_PageComboBox) {
			if (guiEvent.GetMsg() == GUIComboBox::Closed && m_PageComboBox->GetSelectedIndex() >= 0 && m_PageComboBox->GetSelectedIndex() != m_Page) {
				ShowPage(m_PageComboBox->GetSelectedIndex());
			}
			return;
		}
		for (int row = 0; row < c_RowCount; ++row) {
			const LightingOption* option = m_RowOptions[row];
			if (!option) {
				continue;
			}
			if (guiEvent.GetControl() == m_RowSliders[row] && guiEvent.GetMsg() == GUISlider::Changed) {
				float value = GetSliderValue(*option, m_RowSliders[row]->GetValue());
				ChangeSetting(*option, value);
				m_RowValueLabels[row]->SetText(GetValueText(*option, LightingOptions::GetValue(*option, g_PostProcessMan.GetLightingSettingsToSave())));
				break;
			} else if (guiEvent.GetControl() == m_RowCheckboxes[row] && guiEvent.GetMsg() == GUICheckbox::Changed) {
				ChangeSetting(*option, m_RowCheckboxes[row]->GetCheck() ? 1.0F : 0.0F);
				if (option->BoolField == &LightingSettings::CustomWeather) {
					UpdateRows();
				}
				break;
			}
		}
	}
}
