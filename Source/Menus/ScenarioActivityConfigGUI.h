#pragma once

#include "Activity.h"
#include "GameActivity.h"

#include <array>
#include <map>
#include <string>

namespace RTE {

	class GameActivity;
	class AllegroBitmap;
	class GUIControlManager;
	class GUICollectionBox;
	class GUIComboBox;
	class GUICheckbox;
	class GUIButton;
	class GUILabel;
	class GUISlider;
	class GUIListBox;
	class GUIEvent;

	/// Handling for the scenario Activity configuration screen composition and interaction.
	class ScenarioActivityConfigGUI {

	public:
#pragma region Creation
		/// Constructor method used to instantiate a ScenarioActivityConfigGUI object in system memory and make it ready for use.
		/// @param parentControlManager Pointer to the parent GUIControlManager which owns all the GUIControls of this ScenarioActivityConfigGUI. Ownership is NOT transferred!
		explicit ScenarioActivityConfigGUI(GUIControlManager* parentControlManager);
#pragma endregion

#pragma region Getters and Setters
		/// Gets whether this ScenarioActivityConfigGUI is currently visible and enabled.
		/// @return Whether this ScenarioActivityConfigGUI is currently visible and enabled.
		bool IsEnabled() const;

		/// Enables or disables the ScenarioActivityConfigGUI.
		/// @param enable Show and enable or hide and disable the ScenarioActivityConfigGUI.
		/// @param selectedActivity Pointer to the Activity this ScenarioActivityConfigGUI will be configuring for.
		/// @param selectedScene Pointer to the Scene the passed in Activity will be using.
		void SetEnabled(bool enable, const Activity* selectedActivity = nullptr, Scene* selectedScene = nullptr);
#pragma endregion

#pragma region Concrete Methods
		/// Updates the ScenarioActivityConfigGUI state.
		/// @param mouseX Mouse X position.
		/// @param mouseY Mouse Y position.
		/// @return Whether the player started a new game through the ScenarioActivityConfigGUI.
		bool Update(int mouseX, int mouseY);

		/// Draws the ScenarioActivityConfigGUI to the screen.
		void Draw();
#pragma endregion

	private:
		/// Enumeration for all the player columns in the player setup box. "Extends" the Players enumeration by adding an entry for the CPU player.
		enum PlayerColumns {
			PlayerCPU = Players::MaxPlayerCount,
			PlayerColumnCount
		};

		/// Enumeration for all the team rows in the player setup box. "Extends" the Teams enumeration by adding an entry for unused (disabled) Team.
		enum TeamRows {
			DisabledTeam = Activity::Teams::MaxTeamCount,
			TeamRowCount
		};

		GUIControlManager* m_GUIControlManager; //!< The GUIControlManager which holds all the GUIControls of this menu. Not owned by this.

		const GameActivity* m_SelectedActivity {}; //!< The Activity this ScenarioActivityConfigGUI is configuring.
		const GameActivity* m_PreviouslySelectedActivity {}; //!< The Activity this ScenarioActivityConfigGUI was configuring last, before it got was disabled.
		Scene* m_SelectedScene {}; //!< The Scene the selected Activity will be using.
		int m_LockedCPUTeam { Activity::Teams::NoTeam }; //!< Which team the CPU is locked to, if any.

		bool m_StartingGoldAdjustedManually {}; //!< Whether the player adjusted the starting gold, meaning it should stop automatically adjusting to the difficulty setting default starting gold where applicable.

		Timer m_StartGameButtonBlinkTimer; //!< Timer for blinking the start game button.

		bool m_TechListMedievalUnitsDisabled {}; //!< The mod manager's Medieval units setting when the tech list was fetched, so it's fetched again when that changes.
		bool m_TechListFetched {}; //!< Whether the tech list was fetched and each team's ComboBox was populated with it, even if no valid tech modules were added.

		/// GUI elements that compose the Activity setup box.
		GUICollectionBox* m_ActivityConfigBox;
		GUILabel* m_StartErrorLabel;
		GUIButton* m_StartGameButton;
		GUIButton* m_CancelConfigButton;
		GUILabel* m_ActivityDifficultyLabel;
		GUISlider* m_ActivityDifficultySlider;
		GUILabel* m_StartingGoldLabel;
		GUISlider* m_StartingGoldSlider;
		GUICheckbox* m_RequireClearPathToOrbitCheckbox;
		GUICheckbox* m_FogOfWarCheckbox;
		GUICheckbox* m_DeployUnitsCheckbox;
		GUIButton* m_TimeOfDayButton; //!< Cycles the time of day for the game.
		GUIButton* m_WeatherButton; //!< Cycles the weather for the game.
		int m_TimeOfDayChoice = 0; //!< Index into the time of day choices, 0 for the scene's default.
		int m_WeatherChoice = 0; //!< Index into the weather choices, 0 for the scene's default.

		/// Updates the time of day and weather buttons' text.
		void UpdateAtmosphereButtons();
		GUILabel* m_CPULockLabel;
		GUICollectionBox* m_PlayersAndTeamsConfigBox;
		std::array<GUICollectionBox*, TeamRows::TeamRowCount> m_TeamIconBoxes;
		std::array<GUILabel*, TeamRows::TeamRowCount> m_TeamNameLabels;
		std::array<std::array<GUICollectionBox*, TeamRows::TeamRowCount>, PlayerColumns::PlayerColumnCount> m_PlayerBoxes;
		std::array<GUIComboBox*, Activity::Teams::MaxTeamCount> m_TeamTechComboBoxes;
		std::array<GUILabel*, Activity::Teams::MaxTeamCount> m_TeamAISkillLabels;
		std::array<GUISlider*, Activity::Teams::MaxTeamCount> m_TeamAISkillSliders;

		/// The tick-list for a team's "-Pick several-" factions (optional in custom layouts).
		GUICollectionBox* m_FactionPicksBox {};
		GUILabel* m_FactionPicksLabel {};
		GUIListBox* m_FactionPicksList {};
		GUIButton* m_FactionPicksDoneButton {};
		int m_FactionPicksTeam { -1 }; //!< The team whose factions the tick-list is picking, or -1 when it's shut.
		std::array<std::vector<std::string>, Activity::Teams::MaxTeamCount> m_TeamFactionPicks; //!< The factions ticked for each team (module file names).
		std::array<bool, Activity::Teams::MaxTeamCount> m_TeamUsedFactionPicks {}; //!< Whether each team last started with "-Pick several-".
		bool m_FactionPicksLoaded {}; //!< Whether the remembered picks were read from the settings.

		/// The "Scenario settings" panel: the selected scenario's own settings and the flying craft switch (optional in custom layouts).
		GUIButton* m_ScenarioSettingsButton {};
		GUICollectionBox* m_ScenarioSettingsBox {};
		GUILabel* m_ScenarioSettingsLabel {};
		GUIListBox* m_ScenarioSettingsList {};
		GUILabel* m_ScenarioSettingValueLabel {};
		GUISlider* m_ScenarioSettingSlider {};
		GUIButton* m_ScenarioSettingsResetButton {};
		GUIButton* m_ScenarioSettingsDoneButton {};
		int m_ScenarioSettingSelected { -1 }; //!< Which of the scenario's settings the slider is changing, or -1 for none.
		std::map<std::string, std::map<std::string, float>> m_ScenarioSettingValues; //!< The values set for each scenario's settings (by scenario, then key). Defaults aren't kept.
		bool m_ScenarioFlyingCraft {}; //!< Whether the AI may use the flying ships in scenarios.
		bool m_ScenarioSettingValuesLoaded {}; //!< Whether the remembered values were read from the settings.

#pragma region Activity Configuration Screen Handling
		/// Fills each team's Tech ComboBox with all valid Tech DataModules.
		void PopulateTechComboBoxes();

		/// Reads the factions each team last had ticked from the settings, once.
		void LoadFactionPicks();

		/// Writes each team's ticked factions to the settings, so they're there next time.
		void SaveFactionPicks() const;

		/// Opens the tick-list of factions for a team, beside its Tech ComboBox.
		/// @param team The team to pick factions for.
		void OpenFactionPicks(int team);

		/// Shuts the tick-list of factions.
		void CloseFactionPicks();

		/// Fills the tick-list with every faction, ticked or not for the team it's open for.
		void FillFactionPicksList();

		/// Sets the "-Pick several-" item of a team's Tech ComboBox to read how many factions are ticked.
		/// @param team The team whose ComboBox to update.
		void UpdateSeveralItemText(int team);

		/// Reads the remembered scenario setting values and the flying craft switch from the settings, once.
		void LoadScenarioSettingValues();

		/// Writes the scenario setting values and the flying craft switch to the settings, so they're there next time.
		void SaveScenarioSettingValues() const;

		/// Gets the value one of the selected scenario's settings is set to: the one chosen, else its default.
		/// @param setting The setting.
		/// @return Its value, or GameActivity::c_ScenarioSettingAuto for Auto.
		float GetScenarioSettingValue(const GameActivity::ScenarioSetting& setting) const;

		/// Sets the value of one of the selected scenario's settings, forgetting it when it's the default.
		void SetScenarioSettingValue(const GameActivity::ScenarioSetting& setting, float value);

		/// Gets a setting's value as the panel shows it ("Auto", "x1.50", "3", "45 s", "5 min").
		static std::string ScenarioSettingValueText(const GameActivity::ScenarioSetting& setting, float value);

		/// Opens the "Scenario settings" panel over the setup box.
		void OpenScenarioSettings();

		/// Shuts the "Scenario settings" panel.
		void CloseScenarioSettings();

		/// Fills the panel's list with the flying craft switch and the selected scenario's settings, with their values.
		void FillScenarioSettingsList();

		/// Points the slider at the selected setting (or hides it), and shows that setting's value above it.
		void UpdateScenarioSettingSlider();

		/// Sets the "Scenario settings" button's text to say whether anything is changed from the defaults.
		void UpdateScenarioSettingsButton();

		/// Handles a GUI event while the scenario settings panel is open.
		/// @param guiEvent The event.
		void HandleScenarioSettingsEvent(GUIEvent& guiEvent);

		/// Helper function for multimouse support. Creates a list of human controlled players.
		std::vector<int> GetHumanPlayers();

		/// Resets the configuration screen to the selected Activity's default settings and enables/disables attribute settings accordingly, making the configuration screen ready for interaction.
		void ResetActivityConfigBox();

		/// Sets up and starts the currently selected Activity with the configured settings.
		void StartGame();

		/// Updates the starting gold slider to the Activity difficulty setting (when applicable) and updates the value in the label according to the value in the slider.
		/// @return
		void UpdateStartingGoldSliderAndLabel();

		/// Updates the currently hovered cell in the players and teams config box to apply the hovered visual and removes the hovered visual from any other cells. Also handles clicking on cells.
		/// @param mouseX Mouse X position.
		/// @param mouseY Mouse Y position.
		void UpdatePlayerTeamSetupCell(int mouseX, int mouseY);

		/// Handles the player interaction with a cell in the players and teams config box.
		/// @param clickedPlayer The player box that was clicked.
		/// @param clickedTeam The team box that was clicked.
		void HandleClickOnPlayerTeamSetupCell(int clickedPlayer, int clickedTeam);

		/// Handles the player interaction with the ScenarioActivityConfigGUI GUI elements.
		/// @return Whether the player started a new game through the ScenarioActivityConfigGUI.
		bool HandleInputEvents();
#pragma endregion

		// Disallow the use of some implicit methods.
		ScenarioActivityConfigGUI(const ScenarioActivityConfigGUI& reference) = delete;
		ScenarioActivityConfigGUI& operator=(const ScenarioActivityConfigGUI& rhs) = delete;
	};
} // namespace RTE
