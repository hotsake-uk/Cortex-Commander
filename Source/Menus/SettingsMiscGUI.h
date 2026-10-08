#pragma once

namespace RTE {

	class GUIControlManager;
	class GUICollectionBox;
	class GUICheckbox;
	class GUILabel;
	class GUISlider;
	class GUIEvent;

	/// Handling for misc settings through the game settings user interface.
	class SettingsMiscGUI {

	public:
#pragma region Creation
		/// Constructor method used to instantiate a SettingsMiscGUI object in system memory and make it ready for use.
		/// @param parentControlManager Pointer to the parent GUIControlManager which owns all the GUIControls of this SettingsMiscGUI. Ownership is NOT transferred!
		explicit SettingsMiscGUI(GUIControlManager* parentControlManager);
#pragma endregion

#pragma region Concrete Methods
		/// Enables or disables the SettingsMiscGUI.
		/// @param enable Show and enable or hide and disable the SettingsMiscGUI.
		void SetEnabled(bool enable = true) const;

		/// Handles the player interaction with the SettingsMiscGUI GUI elements.
		/// @param guiEvent The GUIEvent containing information about the player interaction with an element.
		void HandleInputEvents(GUIEvent& guiEvent);
#pragma endregion

	private:
		GUIControlManager* m_GUIControlManager; //!< The GUIControlManager which holds all the GUIControls of this menu. Not owned by this.

		/// GUI elements that compose the misc settings menu screen.
		GUICollectionBox* m_MiscSettingsBox;
		GUICheckbox* m_SkipIntroCheckbox;
		GUICheckbox* m_ShowToolTipsCheckbox;
		GUICheckbox* m_ShowLoadingScreenProgressReportCheckbox;
		GUICheckbox* m_ShowAdvancedPerfStatsCheckbox;
		GUICheckbox* m_MeasureLoadTimeCheckbox;
		GUICheckbox* m_UseMonospaceConsoleFontCheckbox;
		GUICheckbox* m_DisableFactionBuyMenuThemesCheckbox;
		GUICheckbox* m_DisableFactionBuyMenuThemeCursorsCheckbox;
		GUILabel* m_SceneBackgroundAutoScaleLabel;
		GUISlider* m_SceneBackgroundAutoScaleSlider;
		GUICheckbox* m_SpreadingFireCheckbox; //!< World simulation switches, the same ones as F6's World section (UI-31).
		GUICheckbox* m_TerrainCollapseCheckbox;
		GUICheckbox* m_FlowingLiquidsCheckbox;
		GUICheckbox* m_LoosePowdersCheckbox;
		GUICheckbox* m_SwimmingCheckbox;
		GUICheckbox* m_UnitsBurnCheckbox;
		GUICheckbox* m_SmokeSightCheckbox;
		GUICheckbox* m_ConsoleLogToFileCheckbox;
		GUISlider* m_ScriptThreadsSlider;
		GUILabel* m_ScriptThreadsLabel;

#pragma region Misc Settings Handling
		/// Updates the Scene background auto-scale label according to the setting.
		void UpdateSceneBackgroundAutoScaleLabel();

		/// Shows the world simulation switches as they are now, since F6 can change them while this page is hidden.
		void ShowWorldSimulationSettings() const;

		/// Updates the script threads label according to the setting.
		void UpdateScriptThreadsLabel() const;
#pragma endregion

		// Disallow the use of some implicit methods.
		SettingsMiscGUI(const SettingsMiscGUI& reference) = delete;
		SettingsMiscGUI& operator=(const SettingsMiscGUI& rhs) = delete;
	};
} // namespace RTE
