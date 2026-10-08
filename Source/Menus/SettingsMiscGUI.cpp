#include "SettingsMiscGUI.h"
#include "SettingsMan.h"
#include "ConsoleMan.h"
#include "PerformanceMan.h"
#include "ActorFire.h"
#include "ActorWater.h"
#include "FluidSim.h"
#include "SmokeGrid.h"
#include "TerrainCollapse.h"
#include "TerrainFire.h"

#include "GUI.h"
#include "GUICollectionBox.h"
#include "GUICheckbox.h"
#include "GUILabel.h"
#include "GUISlider.h"

#include <algorithm>
#include <string>

using namespace RTE;

SettingsMiscGUI::SettingsMiscGUI(GUIControlManager* parentControlManager) :
    m_GUIControlManager(parentControlManager) {
	m_MiscSettingsBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("CollectionBoxMiscSettings"));

	m_SkipIntroCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxSkipIntro"));
	m_SkipIntroCheckbox->SetCheck(g_SettingsMan.SkipIntro());

	m_ShowToolTipsCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxShowToolTips"));
	m_ShowToolTipsCheckbox->SetCheck(g_SettingsMan.ShowToolTips());

	m_ShowLoadingScreenProgressReportCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxShowLoadingScreenProgressReport"));
	m_ShowLoadingScreenProgressReportCheckbox->SetCheck(!g_SettingsMan.GetLoadingScreenProgressReportDisabled());

	m_ShowAdvancedPerfStatsCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxShowAdvancedPerfStats"));
	m_ShowAdvancedPerfStatsCheckbox->SetCheck(g_PerformanceMan.AdvancedPerformanceStatsEnabled());

	m_MeasureLoadTimeCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxMeasureLoadingTime"));
	m_MeasureLoadTimeCheckbox->SetCheck(g_SettingsMan.IsMeasuringModuleLoadTime());

	m_UseMonospaceConsoleFontCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxUseMonospaceConsoleFont"));
	m_UseMonospaceConsoleFontCheckbox->SetCheck(g_ConsoleMan.GetConsoleUseMonospaceFont());

	m_DisableFactionBuyMenuThemesCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxDisableFactionBuyMenuThemes"));
	m_DisableFactionBuyMenuThemesCheckbox->SetCheck(g_SettingsMan.FactionBuyMenuThemesDisabled());

	m_DisableFactionBuyMenuThemeCursorsCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxDisableFactionBuyMenuThemeCursors"));
	m_DisableFactionBuyMenuThemeCursorsCheckbox->SetCheck(g_SettingsMan.FactionBuyMenuThemeCursorsDisabled());

	m_SceneBackgroundAutoScaleLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelSceneBackgroundAutoScaleSetting"));
	UpdateSceneBackgroundAutoScaleLabel();

	m_SceneBackgroundAutoScaleSlider = dynamic_cast<GUISlider*>(m_GUIControlManager->GetControl("SliderSceneBackgroundAutoScale"));
	m_SceneBackgroundAutoScaleSlider->SetValue(g_SettingsMan.GetSceneBackgroundAutoScaleMode());

	m_SpreadingFireCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxSpreadingFire"));
	m_TerrainCollapseCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxTerrainCollapse"));
	m_FlowingLiquidsCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxFlowingLiquids"));
	m_LoosePowdersCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxLoosePowders"));
	m_SwimmingCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxSwimming"));
	m_UnitsBurnCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxUnitsBurn"));
	m_SmokeSightCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxSmokeSight"));
	ShowWorldSimulationSettings();

	m_ConsoleLogToFileCheckbox = dynamic_cast<GUICheckbox*>(m_GUIControlManager->GetControl("CheckboxConsoleLogToFile"));
	if (m_ConsoleLogToFileCheckbox) {
		m_ConsoleLogToFileCheckbox->SetCheck(g_SettingsMan.ConsoleLogToFile());
	}

	m_ScriptThreadsSlider = dynamic_cast<GUISlider*>(m_GUIControlManager->GetControl("SliderScriptThreads"));
	m_ScriptThreadsLabel = dynamic_cast<GUILabel*>(m_GUIControlManager->GetControl("LabelScriptThreadsValue"));
	if (m_ScriptThreadsSlider) {
		m_ScriptThreadsSlider->SetValue(std::max(0, g_SettingsMan.GetNumberOfLuaStatesOverride()));
	}
	UpdateScriptThreadsLabel();
}

void SettingsMiscGUI::ShowWorldSimulationSettings() const {
	auto show = [](GUICheckbox* checkbox, bool on) {
		if (checkbox) {
			checkbox->SetCheck(on);
		}
	};
	show(m_SpreadingFireCheckbox, TerrainFire::IsEnabled());
	show(m_TerrainCollapseCheckbox, TerrainCollapse::IsEnabled());
	show(m_FlowingLiquidsCheckbox, FluidSim::IsEnabled());
	show(m_LoosePowdersCheckbox, FluidSim::PowdersEnabled());
	show(m_SwimmingCheckbox, ActorWater::IsEnabled());
	show(m_UnitsBurnCheckbox, ActorFire::IsEnabled());
	show(m_SmokeSightCheckbox, SmokeGrid::IsEnabled());
}

void SettingsMiscGUI::UpdateScriptThreadsLabel() const {
	if (!m_ScriptThreadsLabel) {
		return;
	}
	int count = g_SettingsMan.GetNumberOfLuaStatesOverride();
	// The script states are made at start-up, so a change shows after a restart.
	m_ScriptThreadsLabel->SetText(count <= 0 ? "Per core (restart)" : std::to_string(count) + " (restart)");
}

void SettingsMiscGUI::SetEnabled(bool enable) const {
	m_MiscSettingsBox->SetVisible(enable);
	m_MiscSettingsBox->SetEnabled(enable);
	if (enable) {
		ShowWorldSimulationSettings();
	}
}

void SettingsMiscGUI::UpdateSceneBackgroundAutoScaleLabel() {
	switch (g_SettingsMan.GetSceneBackgroundAutoScaleMode()) {
		case 1:
			m_SceneBackgroundAutoScaleLabel->SetText("Stretch to fit screen");
			break;
		case 2:
			m_SceneBackgroundAutoScaleLabel->SetText("Always upscaled");
			break;
		default:
			m_SceneBackgroundAutoScaleLabel->SetText("Disabled");
			break;
	}
}

void SettingsMiscGUI::HandleInputEvents(GUIEvent& guiEvent) {
	if (guiEvent.GetType() == GUIEvent::Notification) {
		if (guiEvent.GetControl() == m_SkipIntroCheckbox) {
			g_SettingsMan.SetSkipIntro(m_SkipIntroCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_ShowToolTipsCheckbox) {
			g_SettingsMan.SetShowToolTips(m_ShowToolTipsCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_ShowLoadingScreenProgressReportCheckbox) {
			g_SettingsMan.SetLoadingScreenProgressReportDisabled(!m_ShowLoadingScreenProgressReportCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_ShowAdvancedPerfStatsCheckbox) {
			g_PerformanceMan.ShowAdvancedPerformanceStats(m_ShowAdvancedPerfStatsCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_MeasureLoadTimeCheckbox) {
			g_SettingsMan.MeasureModuleLoadTime(m_MeasureLoadTimeCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_UseMonospaceConsoleFontCheckbox) {
			g_ConsoleMan.SetConsoleUseMonospaceFont(m_UseMonospaceConsoleFontCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_DisableFactionBuyMenuThemesCheckbox) {
			g_SettingsMan.SetFactionBuyMenuThemesDisabled(m_DisableFactionBuyMenuThemesCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_DisableFactionBuyMenuThemeCursorsCheckbox) {
			g_SettingsMan.SetFactionBuyMenuThemeCursorsDisabled(m_DisableFactionBuyMenuThemeCursorsCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_SceneBackgroundAutoScaleSlider) {
			g_SettingsMan.SetSceneBackgroundAutoScaleMode(m_SceneBackgroundAutoScaleSlider->GetValue());
			UpdateSceneBackgroundAutoScaleLabel();
		} else if (guiEvent.GetControl() == m_SpreadingFireCheckbox) {
			TerrainFire::SetEnabled(m_SpreadingFireCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_TerrainCollapseCheckbox) {
			TerrainCollapse::SetEnabled(m_TerrainCollapseCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_FlowingLiquidsCheckbox) {
			FluidSim::SetEnabled(m_FlowingLiquidsCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_LoosePowdersCheckbox) {
			FluidSim::SetPowdersEnabled(m_LoosePowdersCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_SwimmingCheckbox) {
			ActorWater::SetEnabled(m_SwimmingCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_UnitsBurnCheckbox) {
			ActorFire::SetEnabled(m_UnitsBurnCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_SmokeSightCheckbox) {
			SmokeGrid::SetEnabled(m_SmokeSightCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_ConsoleLogToFileCheckbox) {
			g_SettingsMan.SetConsoleLogToFile(m_ConsoleLogToFileCheckbox->GetCheck());
		} else if (guiEvent.GetControl() == m_ScriptThreadsSlider) {
			int count = m_ScriptThreadsSlider->GetValue();
			g_SettingsMan.SetNumberOfLuaStatesOverride(count == 0 ? -1 : count);
			UpdateScriptThreadsLabel();
		}
	}
}
