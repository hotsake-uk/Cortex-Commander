#pragma once

#include "Controller.h"

#include "SaveLoadMenuGUI.h"
#include "SettingsGUI.h"
#include "ModManagerGUI.h"

#include <array>
#include <vector>

namespace RTE {

	class AllegroScreen;
	class GUIInputWrapper;
	class GUIControlManager;
	class GUICollectionBox;
	class GUIButton;
	class GUILabel;
	class GUIControl;

	/// Handling for the main menu screen composition and sub-menu interaction.
	class MainMenuGUI {

	public:
		/// Enumeration for the results of the MainMenuGUI input and event update.
		enum class MainMenuUpdateResult {
			NoEvent,
			MetaGameStarted,
			ScenarioStarted,
			EnterCreditsScreen,
			BackToMainFromCredits,
			ActivityStarted,
			ActivityResumed,
			Quit
		};

#pragma region Creation
		/// Constructor method used to instantiate a MainMenuGUI object in system memory and makes it ready for use.
		/// @param guiScreen Pointer to a GUIScreen interface that will be used by this MainMenuGUI's GUIControlManager. Ownership is NOT transferred!
		/// @param guiInput Pointer to a GUIInput interface that will be used by this MainMenuGUI's GUIControlManager. Ownership is NOT transferred!
		MainMenuGUI(AllegroScreen* guiScreen, GUIInputWrapper* guiInput) {
			Clear();
			Create(guiScreen, guiInput);
		}

		/// Makes the MainMenuGUI object ready for use.
		/// @param guiScreen Pointer to a GUIScreen interface that will be used by this MainMenuGUI's GUIControlManager. Ownership is NOT transferred!
		/// @param guiInput Pointer to a GUIInput interface that will be used by this MainMenuGUI's GUIControlManager. Ownership is NOT transferred!
		void Create(AllegroScreen* guiScreen, GUIInputWrapper* guiInput);
#pragma endregion

#pragma region Concrete Methods
		/// Updates the MainMenuGUI state.
		/// @return The result of the MainMenuGUI input and event update. See MainMenuUpdateResult enumeration.
		MainMenuUpdateResult Update();

		/// Draws the MainMenuGUI to the screen.
		void Draw();
#pragma endregion

	private:
		/// Enumeration for the different sub-menu screens of the main menu.
		enum MenuScreen {
			MainScreen,
			MetaGameNoticeScreen,
			SaveOrLoadGameScreen,
			SettingsScreen,
			ModManagerScreen,
			EditorScreen,
			SandboxScreen,
			MapBrowserScreen,
			CampaignScreen,
			StudioScenariosScreen,
			CreditsScreen,
			QuitScreen,
			ScreenCount
		};

		/// Enumeration for all the different buttons of the main menu and sub-menus.
		enum MenuButton {
			MetaGameButton,
			ScenarioButton,
			SandboxButton,
			StudioScenariosButton,
			BattleCommandButton,
			QuickBattleButton,
			SaveOrLoadGameButton,
			SettingsButton,
			ModManagerButton,
			EditorsButton,
			CreditsButton,
			QuitButton,
			ResumeButton,
			BackToMainButton,
			PlayTutorialButton,
			MetaGameContinueButton,
			QuitConfirmButton,
			QuitCancelButton,
			SceneEditorButton,
			AreaEditorButton,
			AssemblyEditorButton,
			GibEditorButton,
			ActorEditorButton,
			SandboxEmptyButton,
			SandboxCurrentMapButton,
			SandboxMapSelectButton,
			SandboxSmallButton,
			SandboxMediumButton,
			SandboxLargeButton,
			SandboxExtraLargeButton,
			ButtonCount
		};

		int m_RootBoxMaxWidth; //!< The maximum width the root CollectionBox that holds all this menu's GUI elements. This is to constrain this menu to the primary window's display (left-most) while in multi-display fullscreen, otherwise positioning can get stupid.

		std::unique_ptr<GUIControlManager> m_MainMenuScreenGUIControlManager; //!< The GUIControlManager which owns all the GUIControls of the MainMenuGUI main screen. Alternative to changing skins at runtime which is expensive, since the main screen now has a unique skin.
		std::unique_ptr<GUIControlManager> m_SubMenuScreenGUIControlManager; //!< The GUIControlManager which owns all the GUIControls of the MainMenuGUI sub-menus.
		GUIControlManager* m_ActiveGUIControlManager; //!< The GUIControlManager that is currently being updated and drawn to the screen.
		GUICollectionBox* m_ActiveDialogBox; // The currently active GUICollectionBox in any of the main or sub-menu screens that acts as a dialog box and requires drawing an overlay.

		MenuScreen m_ActiveMenuScreen; //!< The currently active menu screen that is being updated and drawn to the screen. See MenuScreen enumeration.
		MainMenuUpdateResult m_UpdateResult; //!< The result of the MainMenuGUI update. See MainMenuUpdateResult enumeration.
		bool m_MenuScreenChange; //!< Whether the active menu screen was changed and a different one needs to be shown.
		bool m_MetaGameNoticeShown; //!< Whether the MetaGame notice and tutorial offer have been shown to the player.
		std::string m_SandboxCurrentMapName; //!< The map the sandbox screen's "Current map" button starts on: the map of the game in progress, else the Sandbox game mode's own.
		std::string m_SandboxPickedSize; //!< The size of new sandbox map picked on the sandbox screen ("Small" to "Extra Large"), while its theme is being picked; empty otherwise.
		GUICollectionBox* m_SandboxMapsPanel; //!< The sandbox screen's panel of existing maps.
		GUICollectionBox* m_SandboxSizePanel; //!< The sandbox screen's panel of new sandbox map sizes.
		GUICollectionBox* m_SandboxThemePanel; //!< The panel that replaces those two once a size is picked, with a button per theme.
		GUILabel* m_SandboxThemeLabel; //!< The theme panel's title, naming the size picked.
		std::vector<std::pair<GUIButton*, std::string>> m_SandboxThemeButtons; //!< The theme panel's buttons and the theme each starts.
		GUIButton* m_SandboxThemeBackButton; //!< The theme panel's button back to the sizes.
		MenuScreen m_MapBrowserBackScreen; //!< The screen the map browser goes back to: the sandbox screen it was opened from, or the main screen.

		Timer m_ResumeButtonBlinkTimer; //!< Activity resume button blink timer.
		Timer m_CreditsScrollTimer; //!< Credits scrolling timer.

		std::unique_ptr<SaveLoadMenuGUI> m_SaveLoadMenu; //!< The save/load menu screen.
		std::unique_ptr<SettingsGUI> m_SettingsMenu; //!< The settings menu screen.
		std::unique_ptr<ModManagerGUI> m_ModManagerMenu; //!< The mod manager menu screen.

		// TODO: Rework this hacky garbage implementation when setting button font at runtime without loading a different skin is fixed. Would eliminate the need for a second GUIControlManager as well.
		// Right now the way this works is the font graphic has different character visuals for uppercase and lowercase and the visual change happens by applying the appropriate case string when hovering/unhovering.
		std::array<std::string, MenuButton::ResumeButton + 1> m_MainScreenButtonHoveredText; //!< Array containing uppercase strings of the main screen buttons text that are used to display the larger font when a button is hovered over.
		std::array<std::string, MenuButton::ResumeButton + 1> m_MainScreenButtonUnhoveredText; //!< Array containing lowercase strings of the main menu screen buttons text that are used to display the smaller font when a button is not hovered over.
		GUIButton* m_MainScreenHoveredButton; //!< The currently hovered main screen button.
		int m_MainScreenPrevHoveredButtonIndex; //!< The index of the previously hovered main screen button in the main menu button array.

		/// GUI elements that compose the main menu screen.
		GUILabel* m_VersionLabel;
		GUILabel* m_CreditsTextLabel;
		GUICollectionBox* m_CreditsScrollPanel;
		std::array<GUICollectionBox*, MenuScreen::ScreenCount> m_MainMenuScreens;
		std::array<GUIButton*, MenuButton::ButtonCount> m_MainMenuButtons;

#pragma region Create Breakdown
		/// Creates all the elements that compose the main menu screen.
		void CreateMainScreen();

		/// Creates all the elements that compose the MetaGame notice menu screen.
		void CreateMetaGameNoticeScreen();

		/// Creates all the elements that compose the editor selection menu screen.
		void CreateEditorsScreen();

		/// Creates all the elements that compose the sandbox start menu screen.
		void CreateSandboxScreen();

		/// Adds a button to the sandbox theme panel for every theme the sandbox maps come in, taken from the Small maps ("Sandbox Small - <theme>").
		void CreateSandboxThemeButtons();

		/// Shows the sandbox screen's maps and sizes, or its themes for a picked size.
		/// @param pickedSize The size picked ("Small" to "Extra Large"), or empty for the maps and sizes.
		void ShowSandboxPanels(const std::string& pickedSize);

		/// Creates all the elements that compose the credits menu screen.
		void CreateCreditsScreen();

		/// Creates all the elements that compose the quit confirmation menu screen.
		void CreateQuitScreen();
#pragma endregion

#pragma region Menu Screen Handling
		/// Hides all main menu screens.
		void HideAllScreens();

		/// Sets the MainMenuGUI to display a menu screen.
		/// @param screenToShow Which menu screen to display. See MenuScreen enumeration.
		/// @param playButtonPressSound Whether to play a sound if the menu screen change is triggered by a button press.
		void SetActiveMenuScreen(MenuScreen screenToShow, bool playButtonPressSound = true);

		/// Makes the main menu screen visible to be interacted with by the player.
		void ShowMainScreen();

		/// Makes the MetaGame notice menu screen visible to be interacted with by the player.
		void ShowMetaGameNoticeScreen();

		/// Makes the editor selection menu screen visible to be interacted with by the player.
		void ShowEditorsScreen();

		/// Makes the sandbox start menu screen visible to be interacted with by the player, naming the map "Current map" would start on.
		void ShowSandboxScreen();

		/// Makes the credits menu screen visible to be interacted with by the player and resets the scrolling timer for the credits.
		void ShowCreditsScreen();

		/// Makes the quit confirmation menu screen visible to be interacted with by the player if a game is in progress, or immediately sets the UpdateResult to Quit if not.
		void ShowQuitScreenOrQuit();

		/// Makes the resume game button visible to be interacted with by the player if a game is in progress and animates it (blinking).
		void ShowAndBlinkResumeButton();

		/// Progresses the credits scrolling.
		/// @return Whether the credits finished scrolling.
		bool RollCredits();
#pragma endregion

#pragma region Update Breakdown
		/// Handles returning to the main menu from one of the sub-menus if the player requested to return via the back button or the esc key. Also handles closing active dialog boxes with the esc key.
		/// @param backButtonPressed Whether the player requested to return to the main menu from one of the sub-menus via back button.
		void HandleBackNavigation(bool backButtonPressed);

		/// Handles the player interaction with the MainMenuGUI GUI elements.
		/// @return Whether the player requested to return to the main menu from one of the sub-menus.
		bool HandleInputEvents();

		/// Handles the player interaction with the main screen GUI elements.
		/// @param guiEventControl Pointer to the GUI element that the player interacted with.
		void HandleMainScreenInputEvents(const GUIControl* guiEventControl);

		/// Handles the player interaction with the MetaGame notice screen GUI elements.
		/// @param guiEventControl Pointer to the GUI element that the player interacted with.
		void HandleMetaGameNoticeScreenInputEvents(const GUIControl* guiEventControl);

		/// Handles the player interaction with the editor selection screen GUI elements.
		/// @param guiEventControl Pointer to the GUI element that the player interacted with.
		void HandleEditorsScreenInputEvents(const GUIControl* guiEventControl);

		/// Handles the player interaction with the sandbox start menu screen GUI elements.
		/// @param guiEventControl Pointer to the GUI element that the player interacted with.
		void HandleSandboxScreenInputEvents(const GUIControl* guiEventControl);

		/// Opens the map browser in place of the menu, for a map to play the Sandbox or Battle Command on.
		/// @param battle Whether it's for Battle Command: it goes back to the main screen, rather than the sandbox screen.
		/// @param quickBattle Whether it's for the Quick Battle button: the saved battle picked is started as soon as its map is in.
		void OpenMapBrowser(bool battle, bool quickBattle = false);

		/// Acts on what was done in the map browser: back to where it was opened from, or the game picked started.
		void HandleMapBrowserChoice();

		/// Handles the player interaction with the quit screen GUI elements.
		/// @param guiEventControl Pointer to the GUI element that the player interacted with.
		void HandleQuitScreenInputEvents(const GUIControl* guiEventControl);

		/// Updates the currently hovered main screen button text to give the hovered visual and updates the previously hovered button to remove the hovered visual.
		/// @param hoveredButton Pointer to the currently hovered main screen button, if any. Acquired by GUIControlManager::GetControlUnderPoint.
		void UpdateMainScreenHoveredButton(const GUIButton* hoveredButton);
#pragma endregion

		/// Clears all the member variables of this MainMenuGUI, effectively resetting the members of this object.
		void Clear();

		// Disallow the use of some implicit methods.
		MainMenuGUI(const MainMenuGUI& reference) = delete;
		MainMenuGUI& operator=(const MainMenuGUI& rhs) = delete;
	};
} // namespace RTE
