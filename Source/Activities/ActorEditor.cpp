#include "ActorEditor.h"

#include "PresetMan.h"
#include "MovableMan.h"
#include "FrameMan.h"
#include "CameraMan.h"
#include "UInputMan.h"
#include "WindowMan.h"
// #include "AHuman.h"
// #include "MOPixel.h"
#include "SLTerrain.h"
#include "Controller.h"
// #include "AtomGroup.h"
#include "Actor.h"
#include "AHuman.h"
#include "ACRocket.h"
#include "HeldDevice.h"
#include "Scene.h"
#include "DataModule.h"

#include "ObjectPickerGUI.h"
#include "ModStudioPreview.h"

using namespace RTE;

ConcreteClassInfo(ActorEditor, EditorActivity, 0);

ActorEditor::ActorEditor() {
	Clear();
}

ActorEditor::~ActorEditor() {
	Destroy(true);
}

void ActorEditor::Clear() {
	m_pEditedActor = 0;
	m_pPicker = 0;
	m_PreviewCheckTimer.Reset();
	m_PreviewFileTime = {};
	m_ShowingModStudio = false;
	m_ZoomedIn = false;
}

int ActorEditor::Create() {
	if (EditorActivity::Create() < 0)
		return -1;

	m_PieMenu = std::unique_ptr<PieMenu>(dynamic_cast<PieMenu*>(g_PresetMan.GetEntityPreset("PieMenu", "Actor Editor Pie Menu")->Clone()));

	return 0;
}

int ActorEditor::Create(const ActorEditor& reference) {
	if (EditorActivity::Create(reference) < 0)
		return -1;

	if (m_Description.empty())
		m_Description = "Load and edit Actors.";

	return 0;
}

int ActorEditor::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return EditorActivity::ReadProperty(propName, reader));
	/*
	    MatchProperty("CPUTeam", { reader >> m_CPUTeam; });
	    MatchProperty("Difficulty", { reader >> m_Difficulty; });
	    MatchProperty("DeliveryDelay", { reader >> m_DeliveryDelay; });
	*/
	EndPropertyList;
}

int ActorEditor::Save(Writer& writer) const {
	EditorActivity::Save(writer);
	return 0;
}

void ActorEditor::Destroy(bool notInherited) {
	delete m_pEditedActor;
	delete m_pPicker;

	if (!notInherited)
		EditorActivity::Destroy();
	Clear();
}

int ActorEditor::Start() {
	int error = EditorActivity::Start();

	//////////////////////////////////////////////
	// Allocate and (re)create the picker GUI

	if (m_pPicker)
		m_pPicker->Reset();
	else
		m_pPicker = new ObjectPickerGUI;
	m_pPicker->Create(&(m_PlayerController[0]), -1, "Actor");

	m_PieMenu->SetMenuController(&m_PlayerController[0]);

	m_EditorMode = EditorActivity::EDITINGOBJECT;
	m_ModeChange = true;

	return error;
}

void ActorEditor::SetPaused(bool pause) {
	// Override the pause
	m_Paused = false;
}

void ActorEditor::End() {
	EditorActivity::End();

	m_ActivityState = ActivityState::Over;
}

void ActorEditor::Update() {
	if (m_PreviewCheckTimer.IsPastRealMS(400)) {
		m_PreviewCheckTimer.Reset();
		CheckModStudioPreview();
	}
	// Showing a unit from Mod Studio: Alt lets go of the mouse so it can leave the window for Studio, and a click on the game takes it back (as
	// coming back to the game from another window does by itself). While the game has the mouse, it aims and fires as ever.
	if (m_ShowingModStudio && m_EditorMode != EditorActivity::LOADDIALOG && !g_WindowMan.IsBackground()) {
		if (!g_UInputMan.IsMouseReleased()) {
			if (g_UInputMan.KeyPressed(SDL_SCANCODE_LALT) || g_UInputMan.KeyPressed(SDL_SCANCODE_RALT)) {
				g_UInputMan.DisableMouseMoving(true);
				SDL_ShowCursor();
				g_FrameMan.SetScreenText("Mouse free for Studio. Click the game to control the unit again.", 0, 0, 3000);
			}
		} else if (g_WindowMan.AnyWindowHasFocus() && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) {
			g_UInputMan.GiveMouseBackNow();
			SDL_HideCursor();
		}
	}

	// And object hasn't been loaded yet, so get the loading picker going
	if (!m_pEditedActor) {
		m_NeedSave = false;
		m_HasEverBeenSaved = false;
		// Show the picker dialog to select an object to load
		m_pPicker->SetEnabled(true);
		// Scroll to center of scene
		g_CameraMan.SetScrollTarget(g_SceneMan.GetSceneDim() * 0.5);
		m_EditorMode = m_PreviousMode = EditorActivity::LOADDIALOG;
		m_ModeChange = true;
	}

	// Mode switching etc - don't need for this yet
	//    EditorActivity::Update();
	// Update the player controller since we're not calling parent update
	m_PlayerController[0].Update();

	if (!g_SceneMan.GetScene())
		return;

	/////////////////////////////////////////////////////
	// Update the edited Actor

	if (m_pEditedActor) {
		m_pEditedActor->SetPos(g_SceneMan.GetSceneDim() * 0.5);
		m_pEditedActor->FullUpdate();
		g_CameraMan.SetScrollTarget(m_pEditedActor->GetPos());
	}

	/////////////////////////////////////////////////////
	// Picker logic

	// Enable or disable the picker
	m_pPicker->SetEnabled(m_EditorMode == EditorActivity::LOADDIALOG);

	// Update the picker GUI
	m_pPicker->Update();

	// Set the screen occlusion situation
	if (!m_pPicker->IsVisible())
		g_CameraMan.SetScreenOcclusion(Vector(), ScreenOfPlayer(Players::PlayerOne));

	// Picking something to load into the editor
	if (m_EditorMode == EditorActivity::LOADDIALOG) {
		g_FrameMan.ClearScreenText();
		g_FrameMan.SetScreenText("Select an Actor to LOAD into the editor ->", 0, 333);

		// Picked something!
		if (m_pPicker->ObjectPicked() && !m_pPicker->IsEnabled()) {
			// Load the picked Actor into the editor
			LoadActor(m_pPicker->ObjectPicked());
		}
	} else {
		g_FrameMan.ClearScreenText();
		g_FrameMan.SetScreenText("Control the actor to see how it moves. Ctrl + mouse wheel zooms. Reload data with the pie menu.", 0, 0, 5000);
	}

	//////////////////////////////////////////////
	// Pie menu logic

	if (m_pEditedActor) {
		PieMenu* editedActorPieMenu = m_pEditedActor->GetPieMenu();
		editedActorPieMenu->SetEnabled(m_PlayerController[0].IsState(ControlState::PIE_MENU_ACTIVE) && m_EditorMode != EditorActivity::LOADDIALOG);

		if (editedActorPieMenu->GetPieCommand() == PieSliceType::EditorLoad) {
			ReloadActorData();
		} else if (editedActorPieMenu->GetPieCommand() == PieSliceType::EditorPick) {
			m_EditorMode = EditorActivity::LOADDIALOG;
			m_ModeChange = true;
		}
	}
}

void ActorEditor::DrawGUI(BITMAP* pTargetBitmap, const Vector& targetPos, int which) {
	// Draw the edited actor and pie menu
	if (m_pEditedActor) {
		m_pEditedActor->Draw(pTargetBitmap, targetPos, g_DrawColor);
		m_pEditedActor->GetPieMenu()->Draw(pTargetBitmap, targetPos);
	}

	// Clear out annoying blooms
	// TODO: Figure out if this needs a button or piemenu toggle for some edge case
	// g_PostProcessMan.ClearScenePostEffects();

	m_pPicker->Draw(pTargetBitmap);

	EditorActivity::DrawGUI(pTargetBitmap, targetPos, which);
}

void ActorEditor::Draw(BITMAP* pTargetBitmap, const Vector& targetPos) {
	EditorActivity::Draw(pTargetBitmap, targetPos);
}

bool ActorEditor::LoadActor(const Entity* pActorToLoad) {
	if (!pActorToLoad)
		return false;

	// Replace the old one
	if (MovableObject* asMo = dynamic_cast<MovableObject*>(m_pEditedActor)) {
		asMo->DestroyScriptState();
	}
	delete m_pEditedActor;
	// Make a copy of the picked object reference
	m_pEditedActor = dynamic_cast<Actor*>(pActorToLoad->Clone());

	if (m_pEditedActor) {
		// Set up the editor for the new actor
		m_pEditedActor->SetControllerMode(Controller::CIM_PLAYER, Players::PlayerOne);

		for (const PieSlice* pieSlice: m_PieMenu->GetPieSlices()) {
			m_pEditedActor->GetPieMenu()->AddPieSlice(dynamic_cast<PieSlice*>(pieSlice->Clone()), this);
		}

		m_EditorMode = EditorActivity::EDITINGOBJECT;
		m_ModeChange = true;
		// Units are small on a full screen; start close in. Ctrl + mouse wheel changes it.
		if (!m_ZoomedIn) {
			m_ZoomedIn = true;
			if (g_FrameMan.GetCameraZoom() <= 1.0F) {
				g_FrameMan.SetCameraZoom(3.0F);
			}
		}
	} else {
		g_FrameMan.ClearScreenText();
		g_FrameMan.SetScreenText("There's something wrong with that picked Actor!?", 0, 333, 2000);
		return false;
	}

	return true;
}

bool ActorEditor::ReloadActorData() {
	if (m_pEditedActor) {
		std::string presetName = m_pEditedActor->GetPresetName();
		std::string className = m_pEditedActor->GetClassName();
		std::string moduleName = g_PresetMan.GetDataModuleName(m_pEditedActor->GetModuleID());
		g_PresetMan.ReloadEntityPreset(presetName, className, moduleName, false);
		LoadActor(g_PresetMan.GetEntityPreset(className, presetName, moduleName));
		return m_pEditedActor != nullptr;
	}
	return false;
}

void ActorEditor::CheckModStudioPreview() {
	ModStudioPreview::Request request;
	if (!ModStudioPreview::Poll(m_PreviewFileTime, request)) {
		return;
	}
	g_FrameMan.ClearScreenText();
	std::string problem;
	Actor* unit = ModStudioPreview::MakeUnit(request, problem);
	if (!unit) {
		g_FrameMan.SetScreenText(problem, 0, 0, 8000);
		return;
	}
	const bool loaded = LoadActor(unit);
	delete unit; // LoadActor keeps a copy of its own.
	if (!loaded) {
		return;
	}
	m_pPicker->SetEnabled(false);
	m_ShowingModStudio = true;
	g_FrameMan.SetScreenText("Showing " + request.PresetName + " from Mod Studio. Press Alt to let go of the mouse for Studio, click the game to take it back. Ctrl + mouse wheel zooms.", 0, 0, 6000);
}
