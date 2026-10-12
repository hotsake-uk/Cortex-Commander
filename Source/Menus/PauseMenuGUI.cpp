#include "PauseMenuGUI.h"

#include "WindowMan.h"
#include "FrameMan.h"
#include "ConsoleMan.h"
#include "ActivityMan.h"
#include "UInputMan.h"
#include "SettingsMan.h"
#include "Sandbox.h"
#include "DebugMan.h"
#include "MetaMan.h"
#include "Notices.h"

#include "SaveLoadMenuGUI.h"
#include "SettingsGUI.h"
#include "ModManagerGUI.h"

#include "GUI.h"
#include "AllegroScreen.h"
#include "GUIInputWrapper.h"
#include "GUICollectionBox.h"
#include "GUIButton.h"

#include "AllegroTools.h"

using namespace RTE;

void PauseMenuGUI::Clear() {
	m_GUIControlManager = nullptr;
	m_ActiveDialogBox = nullptr;

	m_BackdropBitmap = nullptr;

	m_ActiveMenuScreen = PauseMenuScreen::MainScreen;
	m_UpdateResult = PauseMenuUpdateResult::NoEvent;
	m_ResumeButtonBlinkTimer.Reset();

	m_SaveLoadMenu = nullptr;
	m_SettingsMenu = nullptr;
	m_ModManagerMenu = nullptr;

	m_ButtonHoveredText.fill(std::string());
	m_ButtonUnhoveredText.fill(std::string());
	m_HoveredButton = nullptr;
	m_PrevHoveredButtonIndex = 0;

	m_SavingButtonsDisabled = false;
	m_ModManagerButtonDisabled = false;
	m_BattleButtonsShown = true;
	m_RestartAsked = false;

	m_PauseMenuBox = nullptr;
	m_PauseMenuButtons.fill(nullptr);
}

void PauseMenuGUI::Create(AllegroScreen* guiScreen, GUIInputWrapper* guiInput) {
	m_GUIControlManager = std::make_unique<GUIControlManager>();
	RTEAssert(m_GUIControlManager->Create(guiScreen, guiInput, "Base.rte/GUIs/Skins/Menus", "MainMenuScreenSkin.ini"), "Failed to create GUI Control Manager and load it from Base.rte/GUIs/Skins/Menus/MainMenuScreenSkin.ini");
	m_GUIControlManager->Load("Base.rte/GUIs/PauseMenuGUI.ini");

	int rootBoxMaxWidth = g_WindowMan.FullyCoversAllDisplays() ? g_WindowMan.GetPrimaryWindowDisplayWidth() / g_WindowMan.GetResMultiplier() : g_WindowMan.GetResX();

	dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("root"))->Resize(rootBoxMaxWidth, g_WindowMan.GetResY());

	m_PauseMenuBox = dynamic_cast<GUICollectionBox*>(m_GUIControlManager->GetControl("PauseScreen"));
	m_PauseMenuBox->CenterInParent(true, true);

	m_PauseMenuButtons[PauseMenuButton::BackToMainButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonBackToMain"));
	m_PauseMenuButtons[PauseMenuButton::QuickSaveButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonQuickSave"));
	m_PauseMenuButtons[PauseMenuButton::SettingsPanelButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonSettingsPanel"));
	m_PauseMenuButtons[PauseMenuButton::RestartButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonRestart"));
	m_PauseMenuButtons[PauseMenuButton::SaveOrLoadGameButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonSaveOrLoadGame"));
	m_PauseMenuButtons[PauseMenuButton::SettingsButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonSettings"));
	m_PauseMenuButtons[PauseMenuButton::ModManagerButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonModManager"));
	m_PauseMenuButtons[PauseMenuButton::RestartBattleButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonRestartBattle"));
	m_PauseMenuButtons[PauseMenuButton::SurrenderButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonSurrender"));
	m_PauseMenuButtons[PauseMenuButton::KeysButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonKeys"));
	m_PauseMenuButtons[PauseMenuButton::ResumeButton] = dynamic_cast<GUIButton*>(m_GUIControlManager->GetControl("ButtonResume"));

	for (size_t pauseMenuButton = 0; pauseMenuButton < m_PauseMenuButtons.size(); ++pauseMenuButton) {
		std::string buttonText = m_PauseMenuButtons[pauseMenuButton]->GetText();

		std::transform(buttonText.begin(), buttonText.end(), buttonText.begin(), ::toupper);
		m_ButtonHoveredText[pauseMenuButton] = buttonText;
		std::transform(buttonText.begin(), buttonText.end(), buttonText.begin(), ::tolower);
		m_ButtonUnhoveredText[pauseMenuButton] = buttonText;

		m_PauseMenuButtons[pauseMenuButton]->SetText(m_ButtonUnhoveredText[pauseMenuButton]);
		m_PauseMenuButtons[pauseMenuButton]->CenterInParent(true, false);
	}

	if (m_BackdropBitmap) {
		destroy_bitmap(m_BackdropBitmap);
	}
	const BITMAP* backbuffer = g_FrameMan.GetBackBuffer32();
	m_BackdropBitmap = create_bitmap_ex(FrameMan::c_BPP, backbuffer->w, backbuffer->h);
	unsigned int halfTransBlack = makeacol32(0, 0, 0, 96);
	clear_to_color(m_BackdropBitmap, halfTransBlack);

	m_SaveLoadMenu = std::make_unique<SaveLoadMenuGUI>(guiScreen, guiInput, true);
	m_SettingsMenu = std::make_unique<SettingsGUI>(guiScreen, guiInput, true);
	m_ModManagerMenu = std::make_unique<ModManagerGUI>(guiScreen, guiInput, true);
}

void PauseMenuGUI::SetBackButtonTargetName(const std::string& menuName) {
	std::string newButtonText = "Back to " + menuName + " Menu";

	std::transform(newButtonText.begin(), newButtonText.end(), newButtonText.begin(), ::toupper);
	m_ButtonHoveredText[PauseMenuButton::BackToMainButton] = newButtonText;
	std::transform(newButtonText.begin(), newButtonText.end(), newButtonText.begin(), ::tolower);
	m_ButtonUnhoveredText[PauseMenuButton::BackToMainButton] = newButtonText;

	int newButtonWidth = m_GUIControlManager->GetSkin()->GetFont("FontMainMenu.png")->CalculateWidth(newButtonText) + 50;

	m_PauseMenuButtons[PauseMenuButton::BackToMainButton]->SetSize(newButtonWidth, m_PauseMenuButtons[PauseMenuButton::BackToMainButton]->GetHeight());
	m_PauseMenuButtons[PauseMenuButton::BackToMainButton]->SetText(m_ButtonUnhoveredText[PauseMenuButton::BackToMainButton]);
	m_PauseMenuButtons[PauseMenuButton::BackToMainButton]->CenterInParent(true, false);
}

void PauseMenuGUI::EnableOrDisablePauseMenuFeatures() {
	// The quick actions (U-4). Quick Save as F5, only where the game can be saved; the menu's font has no other way to show it greyed, so
	// it says so. Restart as Ctrl+R, not in a campaign (where Ctrl+R doesn't either), and asking again each time the menu opens.
	const Activity* currentActivity = g_ActivityMan.GetActivity();
	const bool canQuickSave = currentActivity && currentActivity->CanBeUserSaved();
	m_PauseMenuButtons[PauseMenuButton::QuickSaveButton]->SetEnabled(canQuickSave);
	SetButtonText(PauseMenuButton::QuickSaveButton, canQuickSave ? "Quick Save" : "No Quick Save Here");
	m_PauseMenuButtons[PauseMenuButton::RestartButton]->SetEnabled(currentActivity && !g_MetaMan.GameInProgress());
	m_RestartAsked = false;
	SetButtonText(PauseMenuButton::RestartButton, "Restart");

	bool disableModManager = true;

	if (const Activity* activity = g_ActivityMan.GetActivity(); activity) {
		disableModManager = activity->GetClassName() != "GAScripted";
	}

	if (m_ModManagerButtonDisabled != disableModManager) {
		GUIButton* modManagerButton = m_PauseMenuButtons[PauseMenuButton::ModManagerButton];

		modManagerButton->SetEnabled(!disableModManager);
		modManagerButton->SetVisible(!disableModManager);

		int yOffset = m_PauseMenuButtons[PauseMenuButton::ModManagerButton]->GetHeight();

		m_PauseMenuButtons[PauseMenuButton::ResumeButton]->MoveRelative(0, yOffset * (disableModManager ? -1 : 1));
		// (The battle entries sit under the mod manager's button: up or down with it.)
		for (int battleButton = PauseMenuButton::RestartBattleButton; battleButton <= PauseMenuButton::KeysButton; ++battleButton) {
			m_PauseMenuButtons[battleButton]->MoveRelative(0, yOffset * (disableModManager ? -1 : 1));
		}
		m_PauseMenuBox->MoveRelative(0, yOffset / 2 * -1);

		m_ModManagerButtonDisabled = disableModManager;
	}

	// The battle entries (Restart battle, Surrender, Keys): only while a battle mode's game is on, in the Sandbox game mode or Battle Command.
	// Surrender only while the battle can still be lost by a team you command.
	const bool showBattleButtons = Sandbox::IsBattleModeRunning();
	if (m_BattleButtonsShown != showBattleButtons) {
		int yOffset = 0;
		for (int battleButton = PauseMenuButton::RestartBattleButton; battleButton <= PauseMenuButton::KeysButton; ++battleButton) {
			m_PauseMenuButtons[battleButton]->SetVisible(showBattleButtons);
			m_PauseMenuButtons[battleButton]->SetEnabled(showBattleButtons);
			yOffset += m_PauseMenuButtons[battleButton]->GetHeight();
		}
		m_PauseMenuButtons[PauseMenuButton::ResumeButton]->MoveRelative(0, yOffset * (showBattleButtons ? 1 : -1));
		m_PauseMenuBox->MoveRelative(0, yOffset / 2 * (showBattleButtons ? -1 : 1));

		m_BattleButtonsShown = showBattleButtons;
	}
	if (showBattleButtons) {
		m_PauseMenuButtons[PauseMenuButton::SurrenderButton]->SetEnabled(Sandbox::CanSurrenderBattle());
	}
}

void PauseMenuGUI::SetActiveMenuScreen(PauseMenuScreen screenToShow, bool playButtonPressSound) {
	if (screenToShow != m_ActiveMenuScreen) {
		m_ActiveMenuScreen = screenToShow;
		if (screenToShow == PauseMenuScreen::SaveOrLoadGameScreen) {
			m_SaveLoadMenu->Refresh();
		}
		if (playButtonPressSound) {
			g_GUISound.ButtonPressSound()->Play();
		}
	}
}

PauseMenuGUI::PauseMenuUpdateResult PauseMenuGUI::Update() {
	m_UpdateResult = PauseMenuUpdateResult::NoEvent;

	if (g_ConsoleMan.IsEnabled() && !g_ConsoleMan.IsReadOnly()) {
		return m_UpdateResult;
	}

	bool backToMainScreen = false;

	switch (m_ActiveMenuScreen) {
		case PauseMenuScreen::MainScreen:
			backToMainScreen = HandleInputEvents();
			BlinkResumeButton();
			break;
		case PauseMenuScreen::SaveOrLoadGameScreen:
			backToMainScreen = m_SaveLoadMenu->HandleInputEvents(this);
			break;
		case PauseMenuScreen::SettingsScreen:
			backToMainScreen = m_SettingsMenu->HandleInputEvents();
			m_ActiveDialogBox = m_SettingsMenu->GetActiveDialogBox();
			break;
		case PauseMenuScreen::ModManagerScreen:
			backToMainScreen = m_ModManagerMenu->HandleInputEvents();
			break;
		default:
			break;
	}
	HandleBackNavigation(backToMainScreen);

	return m_UpdateResult;
}

void PauseMenuGUI::HandleBackNavigation(bool backButtonPressed) {
	if (!m_ActiveDialogBox && (backButtonPressed || g_UInputMan.KeyPressed(SDLK_ESCAPE))) {
		if (m_ActiveMenuScreen != PauseMenuScreen::MainScreen) {
			if (m_ActiveMenuScreen == PauseMenuScreen::SettingsScreen || m_ActiveMenuScreen == PauseMenuScreen::ModManagerScreen) {
				if (m_ActiveMenuScreen == PauseMenuScreen::SettingsScreen) {
					m_SettingsMenu->RefreshActiveSettingsMenuScreen();
				}
				g_SettingsMan.UpdateSettingsFile();
			}
			m_ActiveDialogBox = nullptr;
			SetActiveMenuScreen(PauseMenuScreen::MainScreen, false);
		} else {
			m_UpdateResult = PauseMenuUpdateResult::ActivityResumed;
		}
		g_GUISound.BackButtonPressSound()->Play();
	} else if (m_ActiveMenuScreen == PauseMenuScreen::SettingsScreen && m_ActiveDialogBox && g_UInputMan.KeyPressed(SDLK_ESCAPE)) {
		m_SettingsMenu->CloseActiveDialogBox();
	}
}

bool PauseMenuGUI::HandleInputEvents() {
	if (m_ActiveMenuScreen == PauseMenuScreen::MainScreen) {
		int mousePosX;
		int mousePosY;
		m_GUIControlManager->GetManager()->GetInputController()->GetMousePosition(&mousePosX, &mousePosY);
		UpdateHoveredButton(dynamic_cast<GUIButton*>(m_GUIControlManager->GetControlUnderPoint(mousePosX, mousePosY, m_PauseMenuBox, 1)));
	}
	m_GUIControlManager->Update();

	GUIEvent guiEvent;
	while (m_GUIControlManager->GetEvent(&guiEvent)) {
		if (guiEvent.GetType() == GUIEvent::Command) {
			// Any other button pressed puts Restart back from asking again.
			if (m_RestartAsked && guiEvent.GetControl() != m_PauseMenuButtons[PauseMenuButton::RestartButton]) {
				m_RestartAsked = false;
				SetButtonText(PauseMenuButton::RestartButton, "Restart");
			}
			if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::ResumeButton]) {
				return true;
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::QuickSaveButton]) {
				// As F5: saved over the quick-save, with a notice over the menu, which stays open.
				if (const Activity* activity = g_ActivityMan.GetActivity(); activity && activity->CanBeUserSaved()) {
					g_GUISound.ButtonPressSound()->Play();
					Notices::Post(g_ActivityMan.SaveCurrentGame("QuickSave") ? "Game saved" : "Could not save the game");
				}
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::RestartButton]) {
				// Pressed twice: the first asks, the second restarts the game as Ctrl+R does (the same map and setup, from the start), once
				// the game is back.
				if (!m_RestartAsked) {
					g_GUISound.ButtonPressSound()->Play();
					m_RestartAsked = true;
					SetButtonText(PauseMenuButton::RestartButton, "Confirm Restart");
				} else {
					m_RestartAsked = false;
					SetButtonText(PauseMenuButton::RestartButton, "Restart");
					g_ActivityMan.SetRestartActivity();
					return true;
				}
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::SettingsPanelButton]) {
				// The settings panel (F6) over the game, rather than the old Settings menu.
				g_DebugMan.ShowSettingsPanel();
				return true;
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::SaveOrLoadGameButton]) {
				SetActiveMenuScreen(PauseMenuScreen::SaveOrLoadGameScreen);
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::SettingsButton]) {
				SetActiveMenuScreen(PauseMenuScreen::SettingsScreen);
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::ModManagerButton]) {
				SetActiveMenuScreen(PauseMenuScreen::ModManagerScreen);
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::RestartBattleButton]) {
				// (These three go back to the game as Resume does, with its sound.) The battle mode's game from the start, as it's set up, once the game is back.
				Sandbox::RestartBattleMode();
				return true;
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::SurrenderButton]) {
				// Lost by the team you command; in Battle Command the after-battle report opens.
				Sandbox::SurrenderBattleMode();
				return true;
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::KeysButton]) {
				// The sandbox window's Keys page, over the game.
				Sandbox::ShowKeys();
				return true;
			} else if (guiEvent.GetControl() == m_PauseMenuButtons[PauseMenuButton::BackToMainButton]) {
				g_GUISound.BackButtonPressSound()->Play();
				m_UpdateResult = PauseMenuUpdateResult::BackToMain;
			}
		}
		if (guiEvent.GetType() == GUIEvent::Notification && (guiEvent.GetMsg() == GUIButton::Focused && dynamic_cast<GUIButton*>(guiEvent.GetControl()))) {
			g_GUISound.SelectionChangeSound()->Play();
		}
	}
	return false;
}

void PauseMenuGUI::UpdateHoveredButton(const GUIButton* hoveredButton) {
	int hoveredButtonIndex = -1;
	if (hoveredButton) {
		hoveredButtonIndex = std::distance(m_PauseMenuButtons.begin(), std::find(m_PauseMenuButtons.begin(), m_PauseMenuButtons.end(), hoveredButton));
		if (hoveredButton != m_HoveredButton) {
			m_PauseMenuButtons[hoveredButtonIndex]->SetText(m_ButtonHoveredText[hoveredButtonIndex]);
		}
		m_HoveredButton = m_PauseMenuButtons[hoveredButtonIndex];
	}
	if (!hoveredButton || hoveredButtonIndex != m_PrevHoveredButtonIndex) {
		m_PauseMenuButtons[m_PrevHoveredButtonIndex]->SetText(m_ButtonUnhoveredText[m_PrevHoveredButtonIndex]);
	}

	if (hoveredButtonIndex >= 0) {
		m_PrevHoveredButtonIndex = hoveredButtonIndex;
	} else {
		m_HoveredButton = nullptr;
	}
}

void PauseMenuGUI::SetButtonText(PauseMenuButton button, const std::string& text) {
	std::string buttonText = text;
	std::transform(buttonText.begin(), buttonText.end(), buttonText.begin(), ::toupper);
	m_ButtonHoveredText[button] = buttonText;
	std::transform(buttonText.begin(), buttonText.end(), buttonText.begin(), ::tolower);
	m_ButtonUnhoveredText[button] = buttonText;

	GUIButton* guiButton = m_PauseMenuButtons[button];
	int wantedWidth = m_GUIControlManager->GetSkin()->GetFont("FontMainMenu.png")->CalculateWidth(m_ButtonHoveredText[button]) + 20;
	if (wantedWidth > guiButton->GetWidth()) {
		guiButton->SetSize(wantedWidth, guiButton->GetHeight());
	}
	guiButton->SetText(guiButton == m_HoveredButton ? m_ButtonHoveredText[button] : m_ButtonUnhoveredText[button]);
	guiButton->CenterInParent(true, false);
}

void PauseMenuGUI::BlinkResumeButton() {
	if (m_HoveredButton && m_HoveredButton == m_PauseMenuButtons[PauseMenuButton::ResumeButton]) {
		m_PauseMenuButtons[PauseMenuButton::ResumeButton]->SetText(m_ResumeButtonBlinkTimer.AlternateReal(500) ? m_ButtonHoveredText[PauseMenuButton::ResumeButton] : "]" + m_ButtonHoveredText[PauseMenuButton::ResumeButton] + "[");
	} else {
		m_PauseMenuButtons[PauseMenuButton::ResumeButton]->SetText(m_ResumeButtonBlinkTimer.AlternateReal(500) ? m_ButtonUnhoveredText[PauseMenuButton::ResumeButton] : ">" + m_ButtonUnhoveredText[PauseMenuButton::ResumeButton] + "<");
	}
}

void PauseMenuGUI::Draw() {
	g_WindowMan.DrawPostProcessBuffer();
	blit(m_BackdropBitmap, g_FrameMan.GetBackBuffer32(), 0, 0, 0, 0, m_BackdropBitmap->w, m_BackdropBitmap->h);

	switch (m_ActiveMenuScreen) {
		case PauseMenuScreen::SaveOrLoadGameScreen:
			m_SaveLoadMenu->Draw();
			break;
		case PauseMenuScreen::SettingsScreen:
			m_SettingsMenu->Draw();
			break;
		case PauseMenuScreen::ModManagerScreen:
			m_ModManagerMenu->Draw();
			break;
		default:
			m_GUIControlManager->Draw();
			break;
	}
	if (m_ActiveDialogBox) {
		SetTrueAlphaBlender();
		// Whatever this box may be at this point it's already been drawn by the owning GUIControlManager, but we need to draw it again on top of the overlay so it's not affected by it.
		m_ActiveDialogBox->Draw(m_GUIControlManager->GetScreen());
	}
	m_GUIControlManager->DrawMouse();
}
