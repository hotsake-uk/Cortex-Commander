#pragma once

#include <array>
#include <string>

namespace RTE {

	class GUIControlManager;
	class GUICollectionBox;
	class GUIButton;
	class GUICheckbox;
	class GUIComboBox;
	class GUILabel;
	class GUISlider;
	class GUIEvent;
	struct LightingOption;

	/// Handling for the lighting settings through the game settings user interface: every lighting and effects setting (LightingOptions), a page at a time.
	/// Drawing only: none of these change what happens in a game.
	class SettingsLightingGUI {

	public:
#pragma region Creation
		/// Constructor method used to instantiate a SettingsLightingGUI object in system memory and make it ready for use.
		/// @param parentControlManager Pointer to the parent GUIControlManager which owns all the GUIControls of this SettingsLightingGUI. Ownership is NOT transferred!
		explicit SettingsLightingGUI(GUIControlManager* parentControlManager);
#pragma endregion

#pragma region Getters and Setters
		/// Enables or disables the SettingsLightingGUI, showing the settings as they are now when enabled.
		/// @param enable Show and enable or hide and disable the SettingsLightingGUI.
		void SetEnabled(bool enable = true);
#pragma endregion

#pragma region Concrete Methods
		/// Handles the player interaction with the SettingsLightingGUI GUI elements.
		/// @param guiEvent The GUIEvent containing information about the player interaction with an element.
		void HandleInputEvents(GUIEvent& guiEvent);
#pragma endregion

	private:
		static constexpr int c_RowCount = 9; //!< How many settings a page shows.
		static constexpr int c_SliderSteps = 200; //!< How many steps a slider has across a number setting's range.

		GUIControlManager* m_GUIControlManager; //!< The GUIControlManager which holds all the GUIControls of this menu. Not owned by this.

		GUICollectionBox* m_LightingSettingsBox; //!< The box all of this menu's controls are in.
		GUIComboBox* m_PageComboBox; //!< Picks the page.
		GUIButton* m_PreviousPageButton; //!< Goes to the page before.
		GUIButton* m_NextPageButton; //!< Goes to the page after.
		GUIButton* m_ResetButton; //!< Puts every setting back to its default.

		int m_Page; //!< The page showing.
		std::array<const LightingOption*, c_RowCount> m_RowOptions; //!< The setting on each row, nullptr for an empty row.
		std::array<GUILabel*, c_RowCount> m_RowLabels; //!< Each row's setting name.
		std::array<GUISlider*, c_RowCount> m_RowSliders; //!< Each row's slider, for a number setting.
		std::array<GUICheckbox*, c_RowCount> m_RowCheckboxes; //!< Each row's checkbox, for a setting that's on or off.
		std::array<GUILabel*, c_RowCount> m_RowValueLabels; //!< Each row's value, for a number setting.

#pragma region Lighting Settings Handling
		/// Shows a page of settings.
		/// @param page The page to show, wrapped around the pages there are.
		void ShowPage(int page);

		/// Updates every row's controls to the settings as they are now.
		void UpdateRows();

		/// Gets the slider range and position for a number setting.
		/// @param option The setting.
		/// @param value Its value.
		/// @param sliderMin Set to the slider's lowest value.
		/// @param sliderMax Set to the slider's highest value.
		/// @return The slider's position for the value.
		int GetSliderPosition(const LightingOption& option, float value, int& sliderMin, int& sliderMax) const;

		/// Gets a setting's value from its slider's position.
		/// @param option The setting.
		/// @param sliderPosition The slider's position.
		/// @return The value.
		float GetSliderValue(const LightingOption& option, int sliderPosition) const;

		/// Gets a setting's value as the menu shows it.
		/// @param option The setting.
		/// @param value Its value.
		/// @return The text to show.
		std::string GetValueText(const LightingOption& option, float value) const;

		/// Sets a setting, both now and as the player's own (saved and kept from map to map), and marks the quality preset as custom.
		/// @param option The setting.
		/// @param value The new value.
		void ChangeSetting(const LightingOption& option, float value);

		/// Puts every setting back to its default: the settings the game starts with.
		void ResetToDefaults();
#pragma endregion

		// Disallow the use of some implicit methods.
		SettingsLightingGUI(const SettingsLightingGUI& reference) = delete;
		SettingsLightingGUI& operator=(const SettingsLightingGUI& rhs) = delete;
	};
} // namespace RTE
