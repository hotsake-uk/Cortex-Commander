// The player's own character and taking control of a unit.

#include "SandboxInternal.h"
#include "BuyMenuGUI.h"

namespace SandboxDetail {
	void TakeControl(const Vector& position) {
		Actor* actor = dynamic_cast<Actor*>(ObjectUnder(position, true));
		GameActivity* game = CurrentGame();
		if (!actor || !game || !actor->IsPlayerControllable()) {
			return;
		}
		if (game->SwitchToActor(actor, Players::PlayerOne, actor->GetTeam())) {
			game->SetViewState(Activity::ViewState::Normal, Players::PlayerOne);
			SetPossessed(actor);
			s_PlayHintSeconds = 9.0F;
			// Every tool window goes away, not only the sandbox's: any left open would keep the mouse from the unit.
			g_DebugMan.CloseTools();
			g_ConsoleMan.PrintString("SANDBOX: You're controlling " + actor->GetPresetName() + ". Press Tab to go back to the god view.");
		} else {
			// (The game refuses a unit another player controls, or another player's brain, with only its error sound.)
			g_ConsoleMan.PrintString("SANDBOX: " + actor->GetPresetName() + " can't be taken over: another player has it, or it is their brain.");
		}
	}

	void StopFlying() {
		if (s_Flying) {
			if (Actor* actor = GetRef(s_PlayerUnit)) {
				actor->SetPinStrength(0.0F);
			}
			s_Flying = false;
		}
	}

	void ReleaseControl() {
		StopFlying();
		if (GameActivity* game = CurrentGame()) {
			game->LoseControlOfActor(Players::PlayerOne);
			game->SetViewState(Activity::ViewState::Observe, Players::PlayerOne);
		}
		SetPossessed(nullptr);
	}

	const Preset* FindPreset(const std::vector<Preset>& list, const std::string& presetName) {
		auto found = std::find_if(list.begin(), list.end(), [&presetName](const Preset& preset) { return preset.PresetName == presetName; });
		return found != list.end() ? &*found : nullptr;
	}

	/// The nearest open air at or above a place, for putting a unit down: a place inside the ground is moved up out of it.
	Vector OpenAirAt(Vector place) {
		for (int tries = 0; tries < 400 && g_SceneMan.GetTerrMatter(place.GetFloorIntX(), place.GetFloorIntY()) != g_MaterialAir; ++tries) {
			place.m_Y -= 2.0F;
		}
		return place;
	}

	/// Standing height over the ground straight below a place.
	Vector StandingPlaceBelow(Vector place) {
		place = OpenAirAt(place);
		int sceneHeight = g_SceneMan.GetSceneHeight();
		while (place.m_Y < static_cast<float>(sceneHeight - 2) && g_SceneMan.GetTerrMatter(place.GetFloorIntX(), place.GetFloorIntY() + 1) == g_MaterialAir) {
			place.m_Y += 1.0F;
		}
		place.m_Y -= 26.0F;
		return place;
	}

	/// Makes your character and puts it in the world. It can be stepped into from the next update.
	Actor* MakePlayer(const Vector& place) {
		const Preset* body = FindPreset(s_Units, s_Player.Body);
		for (const char* fallback: {"Soldier Heavy", "Soldier Light", "Whitebot", "Dummy", "Green Dummy"}) {
			if (!body) {
				body = FindPreset(s_Units, fallback);
			}
		}
		if (!body && !s_Units.empty()) {
			body = &s_Units.front();
		}
		Actor* actor = body ? dynamic_cast<Actor*>(CreateObject(body->ClassName, body->PresetName, body->ModuleID)) : nullptr;
		if (!actor) {
			return nullptr;
		}
		for (const std::string& itemName: s_Player.Kit) {
			if (const Preset* item = FindPreset(s_Items, itemName)) {
				if (MovableObject* object = CreateObject(item->ClassName, item->PresetName, item->ModuleID)) {
					actor->AddInventoryItem(object);
				}
			}
		}
		int team = std::clamp(s_Player.Team, 0, c_Sides - 1);
		ActivateSide(team);
		actor->SetTeam(team);
		actor->SetControllerMode(Controller::CIM_AI);
		actor->SetAIMode(Actor::AIMODE_SENTRY);
		actor->SetPos(place);
		g_MovableMan.AddActor(actor);
		s_PlayerUnit = {actor, static_cast<long>(actor->GetUniqueID())};
		return actor;
	}

	/// Steps into your character, making it first if there isn't one. The stepping in itself happens in an update soon after, once the character is in the world.
	/// @param atPlace Whether to put it down at a place first. @param place The place.
	void EnterPlayer(bool atPlace, const Vector& place) {
		if (!CurrentGame() || !Sandbox::IsGodMode()) {
			return;
		}
		Actor* actor = GetRef(s_PlayerUnit);
		if (!actor && s_PlayerEnterPending == 0) {
			Vector viewMiddle = g_CameraMan.GetOffset(0) + Vector(static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F, static_cast<float>(g_FrameMan.GetPlayerScreenHeight()) * 0.4F);
			g_SceneMan.WrapPosition(viewMiddle);
			if (!MakePlayer(atPlace ? OpenAirAt(place) : StandingPlaceBelow(viewMiddle))) {
				g_ConsoleMan.PrintString("SANDBOX: There is no unit to make your character from.");
				return;
			}
		} else if (actor && atPlace) {
			StopFlying();
			actor->SetPos(OpenAirAt(place));
			actor->SetVel(Vector());
		}
		s_PlayerEnterPending = 20;
	}

	/// Called every update: steps into the character when it is ready, and keeps up what it has been given (no harm, a full jetpack, full magazines, the keys).
	void UpdatePlayer() {
		GameActivity* game = CurrentGame();
		Actor* actor = GetRef(s_PlayerUnit);
		if (s_PlayerEnterPending > 0 && game) {
			if (actor) {
				if (IsPossessing() && !IsPossessed(actor)) {
					ReleaseControl();
				}
				if (game->SwitchToActor(actor, Players::PlayerOne, actor->GetTeam())) {
					game->SetViewState(Activity::ViewState::Normal, Players::PlayerOne);
					SetPossessed(actor);
					s_PlayHintSeconds = 9.0F;
					if (AHuman* human = dynamic_cast<AHuman*>(actor); human && !human->GetEquippedItem()) {
						human->EquipFirearm(true);
					}
					g_DebugMan.CloseTools();
				}
				s_PlayerEnterPending = 0;
			} else if (--s_PlayerEnterPending == 0) {
				g_DebugMan.OpenTools();
			}
		}
		if (!actor) {
			s_Flying = false;
			return;
		}
		bool playing = IsPossessed(actor);
		actor->SetIgnoredByAI(s_Player.Neutral);
		if (s_Player.Unkillable) {
			actor->SetHealth(actor->GetMaxHealth());
			if (int wounds = actor->GetWoundCount(); wounds > 0) {
				actor->RemoveWounds(wounds);
			}
			// Nor blown apart: health and wounds put right each update don't stop a gib from one big hit or many wounds at once.
			if (s_PlayerGibLimits.ID != static_cast<long>(actor->GetUniqueID())) {
				s_PlayerGibLimits = {static_cast<long>(actor->GetUniqueID()), actor->GetGibImpulseLimit(), actor->GetGibWoundLimit()};
				actor->SetGibImpulseLimit(0.0F);
				actor->SetGibWoundLimit(0);
			}
		} else if (s_PlayerGibLimits.ID == static_cast<long>(actor->GetUniqueID())) {
			actor->SetGibImpulseLimit(s_PlayerGibLimits.Impulse);
			actor->SetGibWoundLimit(s_PlayerGibLimits.Wounds);
			s_PlayerGibLimits = SavedGibLimits();
		}
		AHuman* human = dynamic_cast<AHuman*>(actor);
		if (s_Player.EndlessJetpack) {
			AEJetpack* jetpack = human ? human->GetJetpack() : nullptr;
			if (ACrab* crab = dynamic_cast<ACrab*>(actor)) {
				jetpack = crab->GetJetpack();
			}
			if (jetpack) {
				jetpack->SetJetTimeLeft(jetpack->GetJetTimeTotal());
			}
		}
		if (s_Player.EndlessAmmo && human) {
			if (HDFirearm* gun = dynamic_cast<HDFirearm*>(human->GetEquippedItem()); gun && gun->GetMagazine() && gun->GetMagazine()->GetCapacity() > 0) {
				gun->GetMagazine()->SetRoundCount(gun->GetMagazine()->GetCapacity());
			}
		}
		if (!playing) {
			StopFlying();
			s_KitKeyPending = -1;
			return;
		}
		// 1 to 9: that item of the kit, taken out an update after the press so the game's own weapon keys (which share 1 and 2) don't undo it.
		if (s_KitKeyPending >= 0 && human) {
			if (static_cast<size_t>(s_KitKeyPending) < s_Player.Kit.size()) {
				human->EquipNamedDevice(s_Player.Kit[s_KitKeyPending], true);
			}
			s_KitKeyPending = -1;
		}
		if (s_Player.NumberKeys && !g_ConsoleMan.IsEnabled()) {
			for (int key = 0; key < 9; ++key) {
				if (g_UInputMan.KeyPressed(static_cast<SDL_Scancode>(SDL_SCANCODE_1 + key))) {
					s_KitKeyPending = key;
				}
			}
		}
		// N: fly through anything. The character is held out of the physics and moved by hand.
		if (s_Player.FlyKey && !g_ConsoleMan.IsEnabled() && g_UInputMan.KeyPressed(SDL_SCANCODE_N)) {
			s_Flying = !s_Flying;
			actor->SetPinStrength(s_Flying ? 100000.0F : 0.0F);
			s_PlayHintSeconds = 3.0F;
		} else if (!s_Player.FlyKey) {
			StopFlying();
		}
		if (s_Flying) {
			const Controller* controller = actor->GetController();
			Vector move;
			if (controller) {
				move.m_X = (controller->IsState(MOVE_RIGHT) ? 1.0F : 0.0F) - (controller->IsState(MOVE_LEFT) ? 1.0F : 0.0F);
				move.m_Y = ((controller->IsState(MOVE_DOWN) || controller->IsState(BODY_CROUCH)) ? 1.0F : 0.0F) - ((controller->IsState(MOVE_UP) || controller->IsState(BODY_JUMP)) ? 1.0F : 0.0F);
			}
			Vector place = actor->GetPos() + move * (420.0F * g_TimerMan.GetDeltaTimeSecs());
			g_SceneMan.WrapPosition(place);
			place.m_Y = std::clamp(place.m_Y, 10.0F, static_cast<float>(g_SceneMan.GetSceneHeight() - 10));
			actor->SetPos(place);
			actor->SetVel(Vector());
			actor->SetAngularVel(0.0F);
			actor->SetRotAngle(0.0F);
		}
	}

	/// Whether, in commander mode (RC-9), you are looking down on it all: not in the game's buy menu or picking where a delivery lands, which
	/// take the mouse and keys for themselves.
	bool CommanderLooking() {
		GameActivity* game = CurrentGame();
		if (!s_Commander || !game) {
			return false;
		}
		const BuyMenuGUI* buy = game->GetBuyGUI(Players::PlayerOne);
		return game->GetViewState(Players::PlayerOne) == Activity::ViewState::Observe && !(buy && buy->IsVisible());
	}

	/// Keeps commander mode (RC-9) looking down: whatever hands you a unit (the game after a delivery lands, or the brain's) is let go and the
	/// view goes back over the field. The buy menu and picking a landing zone are left to run, as they are how you spend your funds. It ends
	/// with the game, or in the Sandbox game mode.
	void UpdateCommander() {
		if (!s_Commander) {
			return;
		}
		GameActivity* game = CurrentGame();
		if (!game || !InGame() || Sandbox::IsGodMode() || game->GetActivityState() == Activity::ActivityState::Over) {
			s_Commander = false;
			s_FreeCamera = false;
			return;
		}
		const BuyMenuGUI* buy = game->GetBuyGUI(Players::PlayerOne);
		Activity::ViewState view = game->GetViewState(Players::PlayerOne);
		if (view == Activity::ViewState::Observe || view == Activity::ViewState::LandingZoneSelect || (buy && buy->IsVisible())) {
			return;
		}
		if (game->GetControlledActor(Players::PlayerOne)) {
			game->LoseControlOfActor(Players::PlayerOne);
		}
		game->SetViewState(Activity::ViewState::Observe, Players::PlayerOne);
		s_FreeCameraStarted = false;
	}

	/// The commander mode's part of the sandbox window (RC-9): your side's funds, the game's buy menu, and the way back into your unit.
	void CommanderPanel() {
		GameActivity* game = CurrentGame();
		if (!game || Sandbox::IsGodMode()) {
			return;
		}
		if (!s_Commander) {
			if (ToolUI::Button("Commander view (F9)")) {
				Sandbox::ToggleCommander();
			}
			ImGui::SetItemTooltip("Leave your unit and command your side from above, under its own fog of war: select and order its units with\nthe command tool, the ring, the keys and the map, and spend its funds on deliveries with the game's buy menu.");
			return;
		}
		ImGui::TextColored(ImVec4(0.55F, 0.8F, 1.0F, 1.0F), "Commander: %s", game->GetTeamName(s_CommanderTeam).c_str());
		ImGui::SameLine();
		ImGui::Text("  Funds %.0f oz", game->GetTeamFunds(s_CommanderTeam));
		ImGui::SameLine();
		BuyMenuGUI* buy = game->GetBuyGUI(Players::PlayerOne);
		ImGui::BeginDisabled(!buy || buy->IsVisible() || game->GetViewState(Players::PlayerOne) == Activity::ViewState::LandingZoneSelect);
		if (ToolUI::SmallButton("Buy...")) {
			buy->SetEnabled(true);
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("The game's buy menu, on your side's funds: then pick where the delivery lands, as with the brain.");
		ImGui::SameLine();
		if (ToolUI::SmallButton("Back into my unit (F9)")) {
			Sandbox::ToggleCommander();
		}
	}
} // namespace SandboxDetail
