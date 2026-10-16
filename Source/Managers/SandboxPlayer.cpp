// The player's own character and taking control of a unit.

#include "SandboxInternal.h"
#include "AVehicle.h"
#include "ACShip.h"
#include "BuyMenuGUI.h"

namespace SandboxDetail {
	void TakeControl(const Vector& position) {
		Actor* actor = dynamic_cast<Actor*>(ObjectUnder(position, true));
		GameActivity* game = CurrentGame();
		if (!actor || !game || !actor->IsPlayerControllable()) {
			return;
		}
		// (In commander mode, only your own side's units.)
		if (s_Commander && actor->GetTeam() != s_CommanderTeam) {
			g_ConsoleMan.PrintString("SANDBOX: In command mode you can only take control of your own team's units.");
			return;
		}
		if (game->SwitchToActor(actor, Players::PlayerOne, actor->GetTeam())) {
			game->SetViewState(Activity::ViewState::Normal, Players::PlayerOne);
			SetPossessed(actor);
			s_PlayHintSeconds = 9.0F;
			// Every tool window goes away, not only the sandbox's: any left open would keep the mouse from the unit.
			g_DebugMan.CloseTools();
			g_ConsoleMan.PrintString("SANDBOX: You're controlling " + actor->GetPresetName() + (s_Commander ? ". Press Tab to go back to the command view." : ". Press Tab to go back to the god view."));
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

	/// The unit carrying your character in its inventory (a rocket or dropship it climbed into), and where in it. Out of the world there,
	/// it is the same unit, back in the world when it gets out.
	Actor* CarrierOfPlayer(int* index) {
		if (s_PlayerUnit.ID == 0 || GetRef(s_PlayerUnit)) {
			return nullptr;
		}
		for (Actor* carrier: SandboxAccess::Actors()) {
			const std::deque<MovableObject*>* inventory = carrier->GetInventory();
			for (size_t i = 0; i < inventory->size(); ++i) {
				if ((*inventory)[i] && (*inventory)[i]->GetUniqueID() == s_PlayerUnit.ID) {
					if (index) {
						*index = static_cast<int>(i);
					}
					return carrier;
				}
			}
		}
		return nullptr;
	}

	/// What has your character while it isn't standing in the world: the vehicle it sits in or the craft carrying it. Null when it is in the world, or there is none.
	Actor* PlayerHolder() {
		if (GetRef(s_PlayerUnit)) {
			return nullptr;
		}
		if (Actor* ridden = GetRef(s_PlayerVehicle)) {
			return ridden;
		}
		return CarrierOfPlayer(nullptr);
	}

	/// Your character, out of the vehicle or craft that has it if one does, so there is a unit in the world to move or delete. Null if there is none.
	Actor* TakeOutPlayer() {
		if (Actor* actor = GetRef(s_PlayerUnit)) {
			return actor;
		}
		if (Actor* ridden = GetRef(s_PlayerVehicle)) {
			// (Let out thrown, which always gets it out: a blocked way out would otherwise keep it in its seat. Getting out makes it your character again.)
			if (AVehicle* vehicle = dynamic_cast<AVehicle*>(ridden)) {
				vehicle->EjectSeat(vehicle->GetControlSeat(), true);
			} else if (ACShip* ship = dynamic_cast<ACShip*>(ridden)) {
				ship->EjectSeat(ship->GetControlSeat(), true);
			}
			return GetRef(s_PlayerUnit);
		}
		int index = -1;
		if (Actor* carrier = CarrierOfPlayer(&index)) {
			if (Actor* unit = dynamic_cast<Actor*>(carrier->RemoveInventoryItemAtIndex(index))) {
				unit->SetPos(OpenAirAt(carrier->GetPos()));
				unit->SetVel(Vector());
				g_MovableMan.AddActor(unit);
				s_PlayerUnit = MakeRef(unit);
				return unit;
			}
		}
		return nullptr;
	}

	/// Takes your character out of the world, or out of the vehicle or craft that has it (which stays).
	void RemovePlayer() {
		StopFlying();
		if (Actor* actor = TakeOutPlayer()) {
			actor->SetToDelete(true);
		}
		s_PlayerUnit = UnitRef();
		s_PlayerVehicle = UnitRef();
		s_PlayerEnterPending = 0;
		ClearRespawn();
	}

	/// Takes away every character made as yours except the current one, wherever it has got to: only ever one of you.
	void RemoveStrayPlayers() {
		if (s_PlayerMade.empty()) {
			return;
		}
		const Actor* current = GetRef(s_PlayerUnit);
		if (!current) {
			return;
		}
		for (Actor* actor: SandboxAccess::Actors()) {
			if (actor != current && !actor->IsSetToDelete() && std::find(s_PlayerMade.begin(), s_PlayerMade.end(), static_cast<long>(actor->GetUniqueID())) != s_PlayerMade.end()) {
				actor->SetToDelete(true);
			}
		}
	}

	/// Forgets any death in progress: the character was taken away on purpose, or is back, so nothing is to be respawned.
	void ClearRespawn() {
		s_PlayerWasPlaying = false;
		s_PlayerGoneUpdates = 0;
		s_RespawnWait = -1.0F;
	}

	/// Makes your unit's whole body unbreakable, or puts its parts back as they were. (Limbs come off at a joint's impulse limit or by wounds of their own,
	/// which keeping health and wounds right on the whole unit doesn't stop.)
	void SetBodyUnbreakable(MOSRotating* part, bool unbreakable) {
		if (!part) {
			return;
		}
		for (Attachable* attachable: part->GetAttachables()) {
			long id = static_cast<long>(attachable->GetUniqueID());
			if (unbreakable) {
				if (!s_PlayerPartLimits.count(id)) {
					s_PlayerPartLimits[id] = {attachable->GetJointStrength(), attachable->GetGibImpulseLimit(), static_cast<float>(attachable->GetGibWoundLimit(false, false, false))};
				}
				attachable->SetJointStrength(0.0F);
				attachable->SetGibImpulseLimit(0.0F);
				attachable->SetGibWoundLimit(0);
			} else if (auto saved = s_PlayerPartLimits.find(id); saved != s_PlayerPartLimits.end()) {
				attachable->SetJointStrength(saved->second[0]);
				attachable->SetGibImpulseLimit(saved->second[1]);
				attachable->SetGibWoundLimit(static_cast<int>(saved->second[2]));
				s_PlayerPartLimits.erase(saved);
			}
			SetBodyUnbreakable(attachable, unbreakable);
		}
	}

	void ReleaseControl(bool leavingPlay) {
		StopFlying();
		if (GameActivity* game = CurrentGame()) {
			game->LoseControlOfActor(Players::PlayerOne);
			game->SetViewState(Activity::ViewState::Observe, Players::PlayerOne);
		}
		s_Possessed = nullptr;
		// Leaving play mode takes your character away with it, if that is ticked in the You tab: P makes a new one. A unit of the scene's you took over stays.
		// (Whatever the game says you were controlling: by the time the release came round it could already have let go of the character, and it stayed.)
		if (leavingPlay) {
			RemovePlayerOutOfPlay();
		}
	}

	void RemovePlayerOutOfPlay() {
		if (!s_Player.RemoveOnLeave || s_Possessed || s_PlayerEnterPending > 0 || Sandbox::IsBattleCommand()) {
			return;
		}
		if (GetRef(s_PlayerUnit) || PlayerHolder()) {
			RemovePlayer();
		}
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
		if (s_Player.InheritKit) {
			// What a unit of that kind carries when spawned: its own inventory and its faction's guns, the kit going on top.
			GiveLoadout(actor, *body, 0);
		} else {
			while (MovableObject* item = actor->RemoveInventoryItemAtIndex(0)) {
				delete item;
			}
		}
		for (const std::string& itemName: s_Player.Kit) {
			if (const Preset* item = FindPreset(s_Items, itemName)) {
				if (MovableObject* object = CreateObject(item->ClassName, item->PresetName, item->ModuleID)) {
					actor->AddInventoryItem(object);
				}
			}
		}
		PutPlayerInWorld(actor, place);
		return actor;
	}

	void PutPlayerInWorld(Actor* actor, const Vector& place) {
		int team = std::clamp(s_Player.Team, 0, c_Sides - 1);
		ActivateSide(team);
		actor->SetTeam(team);
		actor->SetControllerMode(Controller::CIM_AI);
		actor->SetAIMode(Actor::AIMODE_SENTRY);
		actor->SetPos(place);
		g_MovableMan.AddActor(actor);
		s_PlayerUnit = {actor, static_cast<long>(actor->GetUniqueID())};
		s_PlayerMade.push_back(s_PlayerUnit.ID);
		RemoveStrayPlayers();
	}

	void PlayAsUnit(Actor* unit) {
		// Where the old character stands (got out of any vehicle or craft first), or the middle of the view if there is none.
		Vector place;
		if (Actor* old = TakeOutPlayer()) {
			place = old->GetPos();
			if (s_Possessed == old) {
				ReleaseControl(false);
			}
			StopFlying();
			old->SetToDelete(true);
			s_PlayerUnit = UnitRef();
		} else {
			Vector viewMiddle = g_CameraMan.GetOffset(0) + Vector(static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F, static_cast<float>(g_FrameMan.GetPlayerScreenHeight()) * 0.4F);
			g_SceneMan.WrapPosition(viewMiddle);
			place = StandingPlaceBelow(viewMiddle);
		}
		ClearRespawn();
		PutPlayerInWorld(unit, OpenAirAt(place));
		s_PlayerEnterPending = 20;
	}

	/// Steps into your character, making it first if there isn't one. The stepping in itself happens in an update soon after, once the character is in the world.
	/// @param atPlace Whether to put it down at a place first. @param place The place.
	void EnterPlayer(bool atPlace, const Vector& place) {
		if (!CurrentGame() || !Sandbox::IsGodMode()) {
			return;
		}
		Actor* actor = atPlace ? TakeOutPlayer() : GetRef(s_PlayerUnit);
		if (!actor) {
			// Still sat in a vehicle or carried in a craft: back into that, rather than a second character made beside it.
			if (Actor* holder = PlayerHolder()) {
				GameActivity* game = CurrentGame();
				if (holder->IsPlayerControllable() && game->SwitchToActor(holder, Players::PlayerOne, holder->GetTeam())) {
					game->SetViewState(Activity::ViewState::Normal, Players::PlayerOne);
					SetPossessed(holder);
					s_PlayHintSeconds = 9.0F;
					g_DebugMan.CloseTools();
					return;
				}
				// (One that can't be taken over lets it out instead.)
				actor = TakeOutPlayer();
			}
		}
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
				if (s_Possessed && s_Possessed != actor) {
					ReleaseControl(false);
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
			// The character was being played and is gone, and nothing has it (a vehicle or craft it is in): it died. With a respawn point set it comes back there.
			if (s_PlayerWasPlaying && !PlayerHolder() && s_PlayerEnterPending == 0) {
				if (!HasCharacter()) {
					ClearRespawn();
				} else if (s_RespawnWait < 0.0F && ++s_PlayerGoneUpdates >= 3) {
					if (s_RespawnSet) {
						s_RespawnWait = s_Player.RespawnInstant ? 0.0F : c_RespawnWaitSeconds;
					} else {
						ClearRespawn();
					}
				}
				if (s_RespawnWait >= 0.0F) {
					s_RespawnWait -= g_TimerMan.GetDeltaTimeSecs();
					if (s_RespawnWait <= 0.0F) {
						ClearRespawn();
						// (EnterPlayer makes nothing if there already is a character, in a vehicle or not, so there is never a second one.)
						EnterPlayer(true, s_RespawnPoint);
					}
				}
			} else if (s_PlayerWasPlaying && PlayerHolder()) {
				s_PlayerGoneUpdates = 0;
			}
			// Sat in a vehicle's open seat, where shots can reach it: still kept from harm, if it is to be.
			const Actor* ridden = GetRef(s_PlayerVehicle);
			const AVehicle* vehicle = dynamic_cast<const AVehicle*>(ridden);
			const ACShip* ship = dynamic_cast<const ACShip*>(ridden);
			if ((vehicle || ship) && s_Player.Unkillable) {
				int seat = vehicle ? vehicle->GetControlSeat() : ship->GetControlSeat();
				if (Actor* rider = seat >= 0 ? (vehicle ? vehicle->GetSeatOccupant(seat) : ship->GetSeatOccupant(seat)) : nullptr) {
					rider->SetHealth(rider->GetMaxHealth());
					if (int wounds = rider->GetWoundCount(); wounds > 0) {
						rider->RemoveWounds(wounds);
					}
				}
			}
			return;
		}
		RemoveStrayPlayers();
		bool playing = s_Possessed == actor;
		if (playing) {
			s_PlayerWasPlaying = true;
			s_PlayerGoneUpdates = 0;
			s_RespawnWait = -1.0F;
		}
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
			// Nor losing limbs, which health that never runs out doesn't stop on its own.
			SetBodyUnbreakable(actor, true);
		} else if (s_PlayerGibLimits.ID == static_cast<long>(actor->GetUniqueID())) {
			SetBodyUnbreakable(actor, false);
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
			// (Not being played any more, so if it dies now it is not a death to come back from.)
			ClearRespawn();
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
			if (!Sandbox::IsGodMode()) {
				s_Possessed = nullptr;
			}
			return;
		}
		// A unit taken over with the Control tool is played, not let go of, until Tab (or F9 or the toolbar key) brings you back up. If it
		// dies, back to the command view rather than into whatever unit the game hands you next. A switch to another of your units with the
		// game's own next/previous keys is followed.
		if (s_Possessed) {
			Actor* controlled = game->GetControlledActor(Players::PlayerOne);
			const bool alive = g_MovableMan.IsActor(s_Possessed) && static_cast<long>(s_Possessed->GetUniqueID()) == s_PossessedID;
			if (alive && controlled == s_Possessed) {
				return;
			}
			if (alive && controlled && g_MovableMan.IsActor(controlled) && controlled->GetTeam() == s_CommanderTeam) {
				SetPossessed(controlled);
				return;
			}
			s_Possessed = nullptr;
			Sandbox::SetOpen(!s_CommanderToolbarView);
			if (!alive) {
				g_ConsoleMan.PrintString("SANDBOX: The unit you were controlling is gone; back to the command view.");
			}
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

	void SavePlayer(std::ostream& out) {
		out << ' ' << s_PlayerUnit.ID << ' ' << s_PlayerVehicle.ID << ' ' << s_RespawnSet << ' ' << s_RespawnPoint.m_X << ' ' << s_RespawnPoint.m_Y;
	}

	void LoadPlayer(std::istream& in, const LoadedIDs& ids) {
		long unit = 0;
		long vehicle = 0;
		in >> unit >> vehicle >> s_RespawnSet >> s_RespawnPoint.m_X >> s_RespawnPoint.m_Y;
		s_PlayerUnit = ids.Ref(unit);
		s_PlayerVehicle = ids.Ref(vehicle);
		s_PlayerMade.clear();
		if (Actor* character = s_PlayerUnit.Unit) {
			s_PlayerMade.push_back(s_PlayerUnit.ID);
			// Its parts were saved unbreakable (joints that never give), as it is kept while it can't be hurt; what they had before is
			// their presets', taken from those, so turning Unkillable off (or the unit being yours no more) puts them back.
			std::function<void(MOSRotating*)> remember = [&remember](MOSRotating* part) {
				for (Attachable* attachable: part->GetAttachables()) {
					if (const Attachable* preset = dynamic_cast<const Attachable*>(g_PresetMan.GetEntityPreset(attachable->GetClassName(), attachable->GetPresetName(), attachable->GetModuleID()))) {
						s_PlayerPartLimits[static_cast<long>(attachable->GetUniqueID())] = {preset->GetJointStrength(), preset->GetGibImpulseLimit(), static_cast<float>(preset->GetGibWoundLimit(false, false, false))};
					}
					remember(attachable);
				}
			};
			remember(character);
		}
	}
} // namespace SandboxDetail

void Sandbox::OnUnitBoarded(const Actor* unit, Actor* vehicle) {
	using namespace SandboxDetail;
	if (!unit || !vehicle) {
		return;
	}
	bool character = GetRef(s_PlayerUnit) == unit;
	if (character) {
		StopFlying();
		s_PlayerVehicle = MakeRef(vehicle);
		s_PlayerUnit = UnitRef();
	}
	// Into the vehicle, not back to the god view: the unit you were in is deleted this update, and the next look would have found it gone.
	if (s_Possessed == unit) {
		SetPossessed(vehicle);
	}
}

void Sandbox::OnUnitLeftVehicle(const Actor* vehicle, Actor* unit) {
	using namespace SandboxDetail;
	if (!unit || !vehicle) {
		return;
	}
	if (s_PlayerVehicle.Unit == vehicle && s_PlayerVehicle.ID == static_cast<long>(vehicle->GetUniqueID())) {
		s_PlayerUnit = MakeRef(unit);
		s_PlayerVehicle = UnitRef();
		// (The copy that got out is a new unit, with the gib limits taken off while it couldn't be hurt: the saved ones are put back on it in time.)
		s_PlayerGibLimits.ID = static_cast<long>(unit->GetUniqueID());
	}
	if (s_Possessed == vehicle) {
		SetPossessed(unit);
	}
}
