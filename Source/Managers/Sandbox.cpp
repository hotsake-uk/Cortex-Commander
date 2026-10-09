#include "SandboxInternal.h"
#include "ActorWater.h"
#include "MenuMan.h"
#include "Weather.h"

bool Sandbox::s_Open = false;

std::deque<Actor*>& Sandbox::Actors() {
	return g_MovableMan.m_Actors;
}

std::deque<MovableObject*>& Sandbox::Items() {
	return g_MovableMan.m_Items;
}

bool Sandbox::Do(const std::string& toolName, const Vector& position, int team, int order, int count, const std::string& presetName) {
	if (!InGame()) {
		return false;
	}
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	if (toolName == "Remove water spawners") {
		s_WaterSpawners.clear();
		return true;
	}
	if (toolName == "Run gym") {
		return GymRunAll();
	}
	if (toolName == "Remove effects") {
		s_Effects.clear();
		return true;
	}
	if (toolName == "Effect") {
		// The preset name is the effect's name.
		for (int i = 0; i < static_cast<int>(EffectKind::Count); ++i) {
			if (presetName == c_Effects[i].Name) {
				Stroke placed;
				placed.Kind = Tool::Effect;
				placed.Position = position;
				placed.Choice = i;
				s_Queue.push_back(placed);
				return true;
			}
		}
		return false;
	}
	Stroke stroke;
	stroke.Position = position;
	stroke.Team = std::clamp(team, 0, c_Sides - 1);
	stroke.Orders = UnitOrder(order);
	stroke.Count = std::max(count, 1);
	stroke.Radius = std::max(count, 1);
	if (ContainsIgnoringCase(toolName, "Orders") && toolName.size() == 6) {
		stroke.Kind = Tool::OrderSide;
		s_Queue.push_back(stroke);
		return true;
	}
	if (ContainsIgnoringCase(toolName, "Select") && toolName.size() == 6) {
		// A box of half-size count around the point.
		stroke.Kind = Tool::Select;
		stroke.Position = position - Vector(static_cast<float>(count), static_cast<float>(count));
		stroke.Position2 = position + Vector(static_cast<float>(count), static_cast<float>(count));
		s_Queue.push_back(stroke);
		return true;
	}
	int toolIndex = -1;
	// (The spring tool's old name, still taken from scripts.)
	const std::string lookedFor = ContainsIgnoringCase("Water spawner", toolName.c_str()) && toolName.size() == 13 ? std::string("Spring") : toolName;
	for (int i = 0; i < c_ToolCount; ++i) {
		std::string name = c_Tools[i].Name;
		if (name.size() == lookedFor.size() && ContainsIgnoringCase(name, lookedFor.c_str())) {
			toolIndex = i;
		}
	}
	if (toolIndex < 0) {
		return false;
	}
	stroke.Kind = c_Tools[toolIndex].Kind;
	if (stroke.Kind == Tool::None) {
		// "Look around" from a script: put the free camera on the point, and stop following anything.
		s_FreeCamera = true;
		s_FreeCameraStarted = true;
		s_FollowTarget = UnitRef();
		s_FollowAction = false;
		s_CameraCenter = position;
		g_CameraMan.SetScroll(position, 0);
		return true;
	}
	int randomFaction = -1;
	if (presetName.rfind("Random ", 0) == 0) {
		// "Random <faction>" as the window's box gives: the faction by name, as SandboxAutoBattleSide takes it.
		std::string name = presetName.substr(7);
		for (size_t i = 0; i < s_FactionNames.size(); ++i) {
			if (s_FactionNames[i] == name || s_FactionNames[i] + ".rte" == name) {
				randomFaction = static_cast<int>(i);
			}
		}
	}
	if ((stroke.Kind == Tool::Drop || stroke.Kind == Tool::Unit) && (presetName == "Random units" || presetName == "Random favourites" || randomFaction >= 0)) {
		// Random units, as the window's "Random units" box: each one picked from every faction's units, from the favourites or from one faction.
		stroke.Random = true;
		stroke.FavouritesOnly = presetName == "Random favourites";
		stroke.RandomFaction = randomFaction;
	} else if (stroke.Kind == Tool::Unit || stroke.Kind == Tool::Drop || stroke.Kind == Tool::Brain || stroke.Kind == Tool::Item || stroke.Kind == Tool::Structure || stroke.Kind == Tool::Barracks) {
		const std::vector<Preset>& list = ListFor(stroke.Kind);
		auto found = std::find_if(list.begin(), list.end(), [&presetName](const Preset& preset) { return preset.PresetName == presetName; });
		if (found == list.end()) {
			return false;
		}
		stroke.Choice = static_cast<int>(found - list.begin());
		if (stroke.Kind == Tool::Structure) {
			stroke.Count = 1;
		}
		if (std::getenv("CCCP_TEST_POINTER")) {
			ChoiceFor(stroke.Kind) = stroke.Choice;
		}
	}
	if (std::getenv("CCCP_TEST_POINTER")) {
		// Test runs that show what the pointer does: what a script used stays in hand, for the side it used.
		s_ToolIndex = toolIndex;
		s_Team = std::clamp(team, 0, c_Sides - 1);
	}
	s_Queue.push_back(stroke);
	return true;
}

void Sandbox::SetAIPaused(bool paused) {
	Controller::SetAIPaused(paused);
}

bool Sandbox::SetBuildMode(bool build) {
	GameActivity* game = CurrentGame();
	if (!game || !InGame()) {
		return false;
	}
	if (build && s_Possessed) {
		// Building is done from the god view.
		ReleaseControl();
	}
	game->SetFreeBuildMode(build);
	if (build) {
		// The build menu needs the mouse, so every tool window goes away. Tab (or Done in its pie menu) comes back.
		g_DebugMan.CloseTools();
	}
	return game->IsFreeBuildMode();
}

int Sandbox::CountUnits(int team) {
	int count = 0;
	for (const Actor* actor: SandboxAccess::Actors()) {
		// (Not a brain: it doesn't fight, and a side down to its brain is out of the battle.)
		if (!IsCombatant(actor) || actor->GetTeam() != team || actor->IsInGroup("Brains")) {
			continue;
		}
		if (!dynamic_cast<const ACraft*>(actor)) {
			++count;
			continue;
		}
		// The passengers of a craft of the side's, still on the way in: inventory, not in the world. (Left out, a side whose last wave
		// was in the air was "gone", and the old auto battle was called for the other side.)
		for (const MovableObject* item: *actor->GetInventory()) {
			if (const Actor* passenger = dynamic_cast<const Actor*>(item); passenger && !passenger->IsDead() && passenger->GetHealth() > 0.0F) {
				++count;
			}
		}
	}
	return count;
}

Actor* Sandbox::SpawnUnit(const std::string& presetName, int team, const Vector& position, int order) {
	if (!InGame()) {
		return nullptr;
	}
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	const Preset* preset = FindPreset(s_Units, presetName);
	Actor* actor = preset ? CreateUnit(*preset, team, 0, UnitOrder(order)) : nullptr;
	if (!actor) {
		return nullptr;
	}
	ActivateSide(team);
	actor->SetPos(position);
	g_MovableMan.AddActor(actor);
	return actor;
}

float Sandbox::UnitCost(const std::string& presetName) {
	// (The catalogue is built when the window is first drawn; a barracks a script placed before that trained at the 20 supply floor.)
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	const Preset* preset = FindPreset(s_Units, presetName);
	const SceneObject* object = preset ? dynamic_cast<const SceneObject*>(g_PresetMan.GetEntityPreset(preset->ClassName, preset->PresetName, preset->ModuleID)) : nullptr;
	return object ? object->GetGoldValue(preset->ModuleID, 1.0F, 1.0F) : 0.0F;
}

void Sandbox::FillBox(const Vector& topLeft, int width, int height, const std::string& materialName) {
	if (!InGame() || width <= 0 || height <= 0) {
		return;
	}
	if (materialName.empty()) {
		ClearBox(topLeft, width, height);
	} else {
		PaintBox(topLeft, width, height, materialName.c_str());
	}
}

void Sandbox::OnToolsClosed(bool atPointer) {
	GameActivity* game = CurrentGame();
	if (!IsGodMode() || !game || game->IsFreeBuildMode() || s_Possessed) {
		return;
	}
	// With a tool in hand, or no character, the tools are only hidden: you stay above, the tool goes on working on the world, and P steps into the character.
	// With nothing in hand ("Look around") putting the tools away is stepping into the character, as is Shift+Tab whatever is in hand.
	if (!s_Player.EnterOnClose || (CurrentTool().Kind != Tool::None && !atPointer)) {
		s_PlayHintSeconds = 8.0F;
		return;
	}
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	Stroke stroke;
	stroke.Kind = Tool::PlayCharacter;
	stroke.Count = atPointer ? 1 : 0;
	stroke.Position = MouseScenePosition();
	s_Queue.push_back(stroke);
}

void Sandbox::TogglePlay(bool atPointer) {
	GameActivity* game = CurrentGame();
	if (!IsGodMode() || !game || game->IsFreeBuildMode()) {
		return;
	}
	if (s_Possessed) {
		// Back above, with the tools as they were (hidden) and whatever tool was in hand still in it.
		Stroke release;
		release.Kind = Tool::Release;
		s_Queue.push_back(release);
		s_Possessed = nullptr;
		s_FreeCameraStarted = false;
		s_PlayHintSeconds = 8.0F;
		return;
	}
	if (!s_Player.EnterOnClose) {
		g_ConsoleMan.PrintString("SANDBOX: There is no character to play. Tick \"Have a character of my own\" in the sandbox's You tab.");
		return;
	}
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	Stroke stroke;
	stroke.Kind = Tool::PlayCharacter;
	stroke.Count = atPointer ? 1 : 0;
	stroke.Position = MouseScenePosition();
	s_Queue.push_back(stroke);
}

std::string Sandbox::GetCharacterSetup() {
	std::string setup = s_Player.Body + "|" + std::to_string(s_Player.Team) + "|";
	for (bool flag: {s_Player.Unkillable, s_Player.EndlessJetpack, s_Player.EndlessAmmo, s_Player.NumberKeys, s_Player.FlyKey, s_Player.EnterOnClose, s_PauseInMenus, s_Player.Neutral, s_Player.InheritKit}) {
		setup += flag ? '1' : '0';
	}
	setup += "|";
	for (size_t i = 0; i < s_Player.Kit.size(); ++i) {
		setup += (i > 0 ? ";" : "") + s_Player.Kit[i];
	}
	return setup;
}

std::string Sandbox::GetFavourites() {
	std::string favourites;
	for (const Pin& favourite: s_Favourites) {
		favourites += (favourites.empty() ? "" : ";") + std::string(c_Tools[ToolIndex(favourite.Kind)].Name) + "=" + favourite.PresetName;
	}
	return favourites;
}

void Sandbox::SetFavourites(const std::string& favourites) {
	s_Favourites.clear();
	for (size_t at = 0; at < favourites.size();) {
		size_t end = favourites.find(';', at);
		end = end == std::string::npos ? favourites.size() : end;
		std::string one = favourites.substr(at, end - at);
		size_t equals = one.find('=');
		if (equals != std::string::npos) {
			std::string toolName = one.substr(0, equals);
			for (int i = 0; i < c_ToolCount; ++i) {
				if (toolName == c_Tools[i].Name) {
					s_Favourites.push_back({c_Tools[i].Kind, one.substr(equals + 1)});
					break;
				}
			}
		}
		at = end + 1;
	}
}

std::string Sandbox::GetPins() {
	std::string pins;
	for (const Pin& pin: s_Pins) {
		pins += (pins.empty() ? "" : ";") + std::string(c_Tools[ToolIndex(pin.Kind)].Name) + "=" + pin.PresetName;
	}
	return pins;
}

namespace {
	void ReadPins(const std::string& pins) {
		s_Pins.clear();
		for (size_t at = 0; at < pins.size();) {
			size_t end = pins.find(';', at);
			end = end == std::string::npos ? pins.size() : end;
			std::string one = pins.substr(at, end - at);
			size_t equals = one.find('=');
			if (equals != std::string::npos) {
				std::string toolName = one.substr(0, equals);
				for (int i = 0; i < c_ToolCount; ++i) {
					if (toolName == c_Tools[i].Name && s_Pins.size() < 24) {
						s_Pins.push_back({c_Tools[i].Kind, one.substr(equals + 1)});
						break;
					}
				}
			}
			at = end + 1;
		}
	}
} // namespace

void Sandbox::SetPins(const std::string& pins) {
	ReadPins(pins);
	SavePinsFile();
}

void Sandbox::LoadPins(const std::string& fromOldSave) {
	static bool loaded = false;
	if (loaded) {
		return;
	}
	loaded = true;
	if (std::ifstream file(c_PinsFile); file) {
		std::string line;
		std::getline(file, line);
		ReadPins(line);
	} else if (!fromOldSave.empty()) {
		SetPins(fromOldSave);
	}
}

void Sandbox::SetCharacterSetup(const std::string& setup) {
	std::vector<std::string> parts;
	size_t start = 0;
	for (size_t bar = setup.find('|'); parts.size() < 3 && bar != std::string::npos; bar = setup.find('|', start)) {
		parts.push_back(setup.substr(start, bar - start));
		start = bar + 1;
	}
	parts.push_back(setup.substr(start));
	if (parts.size() != 4 || parts[0].empty()) {
		return;
	}
	s_Player.Body = parts[0];
	s_Player.Team = std::clamp(std::atoi(parts[1].c_str()), 0, c_Sides - 1);
	bool* flags[] = {&s_Player.Unkillable, &s_Player.EndlessJetpack, &s_Player.EndlessAmmo, &s_Player.NumberKeys, &s_Player.FlyKey, &s_Player.EnterOnClose, &s_PauseInMenus, &s_Player.Neutral, &s_Player.InheritKit};
	for (size_t i = 0; i < std::size(flags) && i < parts[2].size(); ++i) {
		*flags[i] = parts[2][i] == '1';
	}
	s_Player.Kit.clear();
	for (size_t at = 0; at < parts[3].size();) {
		size_t end = parts[3].find(';', at);
		end = end == std::string::npos ? parts[3].size() : end;
		if (end > at) {
			s_Player.Kit.push_back(parts[3].substr(at, end - at));
		}
		at = end + 1;
	}
}

bool Sandbox::WantsWorldPaused() {
	return IsGodMode() && s_Open && s_PauseInMenus && g_TimerMan.GetSimUpdateCount() > s_GodStartUpdate + 90;
}

bool Sandbox::IsGodMode() {
	const Activity* activity = g_ActivityMan.GetActivity();
	return activity && InGame() && activity->GetPresetName() == "Sandbox";
}

bool Sandbox::WantsWheelZoom() {
	return IsLookingAround() && !ImGui::GetIO().WantCaptureMouse;
}

void Sandbox::ToggleCommander() {
	GameActivity* game = CurrentGame();
	if (!game || !InGame() || IsGodMode()) {
		s_Commander = false;
		return;
	}
	if (!s_Commander) {
		int team = game->GetTeamOfPlayer(Players::PlayerOne);
		if (team < 0 || team >= c_Sides) {
			return;
		}
		Actor* controlled = game->GetControlledActor(Players::PlayerOne);
		s_CommanderReturnTo = controlled && g_MovableMan.IsActor(controlled) ? MakeRef(controlled) : UnitRef();
		s_Commander = true;
		s_CommanderTeam = team;
		s_Team = team;
		s_Selected.erase(std::remove_if(s_Selected.begin(), s_Selected.end(), [team](const UnitRef& ref) { const Actor* unit = GetRef(ref); return !unit || unit->GetTeam() != team; }), s_Selected.end());
		if (controlled) {
			game->LoseControlOfActor(Players::PlayerOne);
		}
		game->SetViewState(Activity::ViewState::Observe, Players::PlayerOne);
		s_FreeCamera = true;
		s_FreeCameraStarted = false;
		s_FollowTarget = UnitRef();
		s_ToolIndex = ToolIndex(Tool::Command);
		s_Open = true;
		return;
	}
	s_Commander = false;
	s_FreeCamera = false;
	// Back into the unit you left, or else your brain, or else whatever the game gives you next.
	Actor* back = GetRef(s_CommanderReturnTo);
	if (!back || back->GetTeam() != s_CommanderTeam) {
		back = game->GetPlayerBrain(Players::PlayerOne);
	}
	if (back && g_MovableMan.IsActor(back) && game->SwitchToActor(back, Players::PlayerOne, s_CommanderTeam)) {
		game->SetViewState(Activity::ViewState::Normal, Players::PlayerOne);
	} else {
		game->SetViewState(Activity::ViewState::ActorSelect, Players::PlayerOne);
	}
}

bool Sandbox::IsCommander() {
	return s_Commander;
}

bool Sandbox::IsLookingAround() {
	if (s_Commander) {
		return CommanderLooking();
	}
	// Automated test runs (CCCP_HIDE_PANELS) place the camera themselves and want no pointer in their pictures.
	static const bool testRun = std::getenv("CCCP_HIDE_PANELS") != nullptr;
	const GameActivity* game = CurrentGame();
	return IsGodMode() && game && !s_Possessed && s_PlayerEnterPending == 0 && !game->IsFreeBuildMode() && (!testRun || s_Open);
}

bool Sandbox::CapturesWorldClicks() {
	// With the tools hidden in the Sandbox game mode, the tool in hand still works on the world.
	static const bool testPointer = std::getenv("CCCP_TEST_POINTER") != nullptr;
	return (s_Open || IsLookingAround() || testPointer) && CurrentTool().Kind != Tool::None && InGame() && !ImGui::GetIO().WantCaptureMouse;
}

void Sandbox::DrawGUI() {
	// A new Sandbox game opens the god view: the window and the free camera.
	if (IsGodMode()) {
		if (s_GodViewPending || !s_GodViewSetUp) {
			s_GodViewPending = false;
			s_GodViewSetUp = true;
			// Automated test runs set CCCP_HIDE_PANELS, so the window (wherever the player last left it) doesn't cover what they capture.
			s_Open = std::getenv("CCCP_HIDE_PANELS") == nullptr;
			s_FreeCamera = true;
			s_FreeCameraStarted = false;
			s_CameraWarmupFrames = 30;
			s_Possessed = nullptr;
			s_PlayerUnit = UnitRef();
			s_PlayerEnterPending = 0;
			s_Flying = false;
			s_StepsWanted = 0;
			s_GodStartUpdate = g_TimerMan.GetSimUpdateCount();
			s_RallySet.fill(false);
			s_Selected.clear();
			s_FollowTarget = UnitRef();
			s_ToolIndex = ToolIndex(Tool::Unit);
		}
	} else {
		if (s_GodViewSetUp) {
			// Left the sandbox: its pictures aren't needed until it's next opened.
			ForgetPictures();
		}
		s_GodViewSetUp = false;
	}
	// Nothing of the sandbox over the menus: the game's still loaded behind them, so the bar and the window went on being drawn over the
	// main menu (the menu loop draws the debug GUI too) after leaving a Sandbox game, until another game was started.
	if (g_MenuMan.GetIsInMenuScreen()) {
		UnmarkSelection();
		return;
	}
	if (GameActivity* game = CurrentGame(); s_Open && game && game->IsFreeBuildMode()) {
		game->SetFreeBuildMode(false);
	}
	if (s_Open && s_Possessed) {
		// Back to the god view.
		Stroke release;
		release.Kind = Tool::Release;
		s_Queue.push_back(release);
		s_Possessed = nullptr;
		s_FreeCameraStarted = false;
	}
	// In the Sandbox game mode the world stands still while the tools are open, so things can be set up and tuned. What is done with a tool still happens:
	// the world is let through one update for it, a sixtieth of a second.
	{
		bool wantPause = WantsWorldPaused() && !g_DebugMan.IsPhotoModeOpen();
		if (wantPause) {
			g_TimerMan.PauseSim(true, TimerMan::SimPauseSandbox);
			s_PausedByMenus = true;
			// Painting (the brushes held down: terrain, liquids, fire, smoke) is put in the world here, without a step: it is terrain and
			// liquid written in place, which needs no update to show. Let through an update each, as every stroke was, a held brush ran
			// the world at the frame rate under the "paused" banner, water flowing and fire spreading while it was held.
			if (InGame()) {
				auto paint = std::stable_partition(s_Queue.begin(), s_Queue.end(), [](const Stroke& stroke) { return c_Tools[ToolIndex(stroke.Kind)].Interval <= 0.0F; });
				std::vector<Stroke> painted(std::make_move_iterator(paint), std::make_move_iterator(s_Queue.end()));
				s_Queue.erase(paint, s_Queue.end());
				for (const Stroke& stroke: painted) {
					Apply(stroke);
				}
			}
			if (s_StepsWanted > 0 || !s_Queue.empty() || s_PlayerEnterPending > 0) {
				g_TimerMan.StepSim(1);
				s_StepsWanted = std::max(s_StepsWanted - 1, 0);
			}
		} else {
			// (Every frame, not only as the tools close: photo mode closing can set this pause for a frame; see DebugMan.)
			g_TimerMan.PauseSim(false, TimerMan::SimPauseSandbox);
			if (s_PausedByMenus) {
				s_PausedByMenus = false;
				s_StepsWanted = 0;
			}
		}
	}
	auto banner = [](const char* text, float fromTop, ImU32 color, float alpha) {
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		ImVec2 size = ImGui::CalcTextSize(text);
		float scale = g_DebugMan.UsingPixelFont() ? 1.0F : 1.3F;
		GameViewRect view = g_DebugMan.GetUncoveredView();
		ImVec2 at(view.x + (view.w - size.x * scale) * 0.5F, view.y + fromTop);
		drawList->AddRectFilled(ImVec2(at.x - 10.0F, at.y - 4.0F), ImVec2(at.x + size.x * scale + 10.0F, at.y + size.y * scale + 4.0F), IM_COL32(0, 0, 0, static_cast<int>(150.0F * alpha)), 4.0F);
		drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, at, (color & 0x00FFFFFF) | (static_cast<ImU32>(255.0F * alpha) << 24), text);
	};
	if (s_PausedByMenus && InGame()) {
		banner("WORLD PAUSED  -  Tab: play", 8.0F, IM_COL32(150, 210, 255, 255), 1.0F);
	}
	if (IsGodMode() && (s_Possessed || !s_Open) && s_PlayHintSeconds > 0.0F && !g_DebugMan.IsPhotoModeHidingHUD()) {
		// A reminder of the keys, for a few seconds after stepping in.
		s_PlayHintSeconds -= ImGui::GetIO().DeltaTime;
		std::string hint = "Tab: sandbox tools";
		if (!s_Possessed) {
			if (s_Player.EnterOnClose) {
				hint += "    P: play";
			}
			if (CurrentTool().Kind != Tool::None) {
				hint += std::string("    In hand: ") + CurrentTool().Name;
			}
			hint += IsPaintTool(CurrentTool().Kind) ? "    Right button: dig    Middle drag / WASD: move    Wheel: zoom" : "    Right drag / WASD: move    Wheel: zoom";
			hint += s_BarShown ? "    U: hide the bar" : "    U: show the bar";
		} else {
			hint += "    P: back above";
		}
		if (s_Possessed && s_Possessed == GetRef(s_PlayerUnit)) {
			if (s_Player.FlyKey) {
				hint += s_Flying ? "    N: stop flying" : "    N: fly";
			}
			if (s_Player.NumberKeys && !s_Player.Kit.empty()) {
				hint += "    1-" + std::to_string(std::min<size_t>(s_Player.Kit.size(), 9)) + ": kit";
			}
		}
		banner(hint.c_str(), 8.0F, IM_COL32(255, 255, 255, 255), std::clamp(s_PlayHintSeconds, 0.0F, 1.0F));
	}
	{
		// The swim keys (LM-4), the first time the unit you play is in liquid over its waist.
		static long swimHintedFor = 0;
		static float swimHintSeconds = 0.0F;
		if (s_Possessed && s_PossessedID != swimHintedFor && g_MovableMan.IsActor(s_Possessed) && static_cast<long>(s_Possessed->GetUniqueID()) == s_PossessedID && ActorWater::IsEnabled() && ActorWater::GetDepth(s_Possessed) >= 2) {
			swimHintedFor = s_PossessedID;
			swimHintSeconds = 7.0F;
		}
		if (swimHintSeconds > 0.0F && !g_DebugMan.IsPhotoModeHidingHUD()) {
			swimHintSeconds -= ImGui::GetIO().DeltaTime;
			banner("Swimming: Up or Jump strokes up, Down or Crouch dives    Air runs out with the head under: watch the Air gauge", 34.0F, IM_COL32(150, 210, 255, 255), std::clamp(swimHintSeconds, 0.0F, 1.0F));
		}
	}
	if (Controller::IsAIPaused() && InGame()) {
		// A reminder that nobody will move until it's resumed.
		const char* banner = "AI PAUSED";
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		ImVec2 size = ImGui::CalcTextSize(banner);
		float scale = g_DebugMan.UsingPixelFont() ? 2.0F : 1.6F;
		ImVec2 at(g_WindowMan.GetGameViewRect().x + (g_WindowMan.GetGameViewRect().w - size.x * scale) * 0.5F, g_WindowMan.GetGameViewRect().y + 36.0F);
		drawList->AddRectFilled(ImVec2(at.x - 10.0F, at.y - 4.0F), ImVec2(at.x + size.x * scale + 10.0F, at.y + size.y * scale + 4.0F), IM_COL32(0, 0, 0, 150), 4.0F);
		drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, at, IM_COL32(255, 210, 80, 255), banner);
	}
	if (InGame() && !Colony::Buildings().empty() && !g_DebugMan.IsPhotoModeHidingHUD()) {
		DrawColony();
	}
	if (InGame() && !g_DebugMan.IsPhotoModeHidingHUD()) {
		// A Battle Director mode's game: its flags and score, with the window open or not.
		DrawBattleMode();
	}
	// With the tools hidden in the Sandbox game mode you're still above it all: the view goes on moving with the mouse and keys, and the tool in hand goes on
	// working. Only the window itself is left out.
	bool hiddenButAbove = !s_Open && IsLookingAround();
	// The bar is there whenever you're above the world and not playing a unit, whatever else is open or hidden, unless put away with U.
	if (s_BarShown && IsGodMode() && InGame() && !s_Possessed && s_PlayerEnterPending == 0 && !g_DebugMan.IsPhotoModeHidingHUD()) {
		if (!s_CatalogueBuilt) {
			BuildCatalogue();
		}
		DrawBar();
	}
	if (!s_Open && !hiddenButAbove) {
		// The selection's arrows come off while the window is away (they stayed on the units of a game with the window shut, till it was
		// opened again); the selection itself is kept for when it is.
		UnmarkSelection();
		static const bool hidePanels = std::getenv("CCCP_HIDE_PANELS") != nullptr;
		if (s_FreeCameraStarted && IsGodMode() && hidePanels) {
			// Automated test runs keep the window shut, but the camera they've placed has to stay where they put it.
			UpdateFreeCamera();
			return;
		}
		s_FreeCameraStarted = false;
		return;
	}
	if (InGame() && !s_CatalogueBuilt) {
		BuildCatalogue();
	}
	UpdateFreeCamera();
	if (InGame()) {
		DrawRallyPoints();
		DrawBattleMarks();
	}

	ImGuiIO& io = ImGui::GetIO();
	// Test runs can't move the real pointer (someone may be using the computer), so they say where it is to be taken to be: CCCP_TEST_POINTER=x,y in window pixels.
	static const char* testPointer = std::getenv("CCCP_TEST_POINTER");
	if (testPointer) {
		float x = 0.0F;
		float y = 0.0F;
		if (std::sscanf(testPointer, "%f,%f", &x, &y) == 2) {
			io.MousePos = ImVec2(x, y);
			io.WantCaptureMouse = false;
			static const bool testRing = std::getenv("CCCP_TEST_RING") != nullptr;
			if (testRing && !s_RingOpen) {
				s_RingOpen = true;
				s_RingCenter = ImVec2(x - 40.0F, y - 10.0F);
			}
		}
	}
	// U: the bar along the bottom hidden or shown again, in the god view, so long as no text box has the keys.
	if (IsGodMode() && InGame() && !s_Possessed && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_U, false)) {
		s_BarShown = !s_BarShown;
	}
	// Enter: done with the Battle tab's defence point, drop line or spawn zone tool, which goes back to the one in hand before (PutDownBattleTool). Not
	// part way through a drag, so the line being drawn isn't lost.
	// With a spawn zone part drawn, Enter closes it instead (three corners or more; fewer are dropped), and Backspace takes back its last
	// corner.
	if (InGame() && !io.WantTextInput && !s_Dragging && IsBattleTool(CurrentTool().Kind) && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
		if (CurrentTool().Kind == Tool::BattleSpawnZone && !s_ZoneDraft.empty()) {
			if (CloseSpawnZone(s_ZoneDraft, s_BattleSetup[std::clamp(s_BattleEditTeam, 0, c_Sides - 1)])) {
				SendBattleSettings(s_BattleEditTeam);
			}
		} else if (IsModeZoneTool(CurrentTool().Kind) && !s_ZoneDraft.empty()) {
			CloseModeBase();
		} else {
			PutDownBattleTool();
		}
	}
	if (InGame() && !io.WantTextInput && (CurrentTool().Kind == Tool::BattleSpawnZone || IsModeZoneTool(CurrentTool().Kind)) && !s_ZoneDraft.empty() && ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
		s_ZoneDraft.pop_back();
	}
	// Ctrl+Z: the last terrain paint or build stroke undone (see UndoPaint), whichever tool is in hand, so long as no text box has the keys.
	// Not while you play a unit: in the WASD layouts Ctrl is crouch, so crouching with Z down took back the last stroke.
	if (InGame() && io.KeyCtrl && !io.WantTextInput && !s_Possessed && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !s_PaintUndo.empty()) {
		QueueSimChange(Tool::UndoTerrain);
	}
	// Control groups: Ctrl and a number keeps the selection under it, the number alone brings it back, and the number again straight after
	// looks at them (RC-6); Ctrl+A takes the whole side. With the command tool in hand, wherever the pointer is, so long as no text box has
	// the keys. (Only while the pointer was over the world, as these were, they did nothing with it resting on the window.) Not while you
	// play a unit: those keys are its own then (its weapons, and Ctrl+A as A).
	if (InGame() && CurrentTool().Kind == Tool::Command && !io.WantTextInput && !s_Possessed) {
		static int lastNumber = -1;
		static double lastNumberTime = -10.0;
		for (int number = 0; number < 10; ++number) {
			if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + number), false)) {
				if (io.KeyCtrl) {
					s_Groups[number] = s_Selected;
				} else if (!s_Groups[number].empty()) {
					s_Selected = s_Groups[number];
					if (number == lastNumber && ImGui::GetTime() - lastNumberTime < 0.4) {
						LookAtUnits(s_Selected);
					}
					lastNumber = number;
					lastNumberTime = ImGui::GetTime();
				}
			}
		}
		if (!io.KeyCtrl && !io.KeyAlt) {
			CommandHotkeys();
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
			// Everyone on the selection's side.
			int team = SelectionTeam();
			s_Selected.clear();
			for (Actor* actor: SandboxAccess::Actors()) {
				if (IsSelectable(actor) && actor->GetTeam() == team) {
					s_Selected.push_back(MakeRef(actor));
				}
			}
		}
	}
	// Paint or spawn with the left mouse button on the world.
	if (CapturesWorldClicks()) {
		const ToolInfo& tool = CurrentTool();
		Vector position = MouseScenePosition();
		if (tool.Kind == Tool::Command) {
			// Drag a box to select units; click the ground to send them there, or an enemy to attack it. A click on a "no route" marker
			// (RC-7) sends its units there again instead.
			if (const NoRoute* marker = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? NoRouteAt(io.MousePos) : nullptr) {
				Stroke stroke;
				stroke.Kind = Tool::Command;
				stroke.Position = marker->Destination;
				stroke.Count = 40;
				s_Queue.push_back(stroke);
			} else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s_Dragging = true;
				s_DragStart = io.MousePos;
				s_DoubleClick = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			}
		} else if (tool.Kind == Tool::BattleDropLine) {
			// Drag along where the team's ships are to come in.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s_Dragging = true;
				s_DragStart = io.MousePos;
				s_DoubleClick = false;
			}
		} else if (tool.Interval <= 0.0F) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				QueueStroke(tool.Kind, position);
			}
		} else if (IsPlantBrush(tool.Kind)) {
			// A plant where clicked, then another each Plant spacing the pointer goes across while held: a row along the ground.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || (ImGui::IsMouseDown(ImGuiMouseButton_Left) && std::abs(g_SceneMan.ShortestDistance(Vector(s_LastPlantX, position.m_Y), position, g_SceneMan.SceneWrapsX()).m_X) >= static_cast<float>(s_PlantSpacing))) {
				s_LastPlantX = position.m_X;
				QueueStroke(tool.Kind, position);
			}
		} else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			s_StrokeTimer -= io.DeltaTime;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || s_StrokeTimer <= 0.0F) {
				// (The Flow slider: the pouring brushes pour less often.)
				s_StrokeTimer = PoursLiquid(tool.Kind) ? tool.Interval / std::clamp(s_Flow, 0.1F, 1.0F) : tool.Interval;
				QueueStroke(tool.Kind, position);
			}
		}
		// With a Paint tool in hand the right button always digs, whatever material or brush is picked (the middle button and WASD move
		// the view). Not while you play a unit: the right button is its own then.
		if (IsPaintTool(tool.Kind) && !s_Possessed && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			s_DigTimer -= io.DeltaTime;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || s_DigTimer <= 0.0F) {
				s_DigTimer = c_Tools[ToolIndex(Tool::Dig)].Interval;
				QueueStroke(Tool::Dig, position);
			}
		}
		DrawSideRing();
		if (!s_RingOpen) {
			DrawCursor();
		}
	} else {
		s_RingOpen = false;
	}
	if (s_Dragging) {
		ImVec2 now = io.MousePos;
		// Defend at (RC-4), and a move or attack-move with Alt held (RC-5), drag the way to face, drawn as an arrow; anything else drags a box
		// to select.
		bool defendAt = s_CommandMode == CommandMode::DefendAt && CurrentTool().Kind == Tool::Command;
		bool facingMove = (s_CommandMode == CommandMode::Move || s_CommandMode == CommandMode::AttackMove) && io.KeyAlt && CurrentTool().Kind == Tool::Command;
		bool dropLine = CurrentTool().Kind == Tool::BattleDropLine;
		if (dropLine) {
			ImGui::GetForegroundDrawList()->AddLine(s_DragStart, now, c_SideColors[std::clamp(s_BattleEditTeam, 0, c_Sides - 1)], 3.0F);
		} else if (defendAt || facingMove) {
			ImU32 color = c_CommandModeColors[static_cast<int>(s_CommandMode)];
			ImGui::GetForegroundDrawList()->AddLine(s_DragStart, now, color, 2.0F);
			float side = now.x >= s_DragStart.x ? 1.0F : -1.0F;
			if (std::abs(now.x - s_DragStart.x) > 12.0F) {
				ImGui::GetForegroundDrawList()->AddTriangleFilled(ImVec2(now.x + side * 8.0F, now.y), ImVec2(now.x, now.y - 6.0F), ImVec2(now.x, now.y + 6.0F), color);
			}
		} else {
			ImGui::GetForegroundDrawList()->AddRect(ImVec2(std::min(s_DragStart.x, now.x), std::min(s_DragStart.y, now.y)), ImVec2(std::max(s_DragStart.x, now.x), std::max(s_DragStart.y, now.y)), IM_COL32(255, 255, 255, 200), 0.0F, 0, 1.5F);
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			s_Dragging = false;
			Stroke stroke;
			float scale = ScenePixelsPerWindowPixel();
			Vector start = g_CameraMan.GetOffset(0) + Vector(s_DragStart.x - ViewOrigin().x, s_DragStart.y - ViewOrigin().y) * scale;
			Vector end = g_CameraMan.GetOffset(0) + Vector(now.x - ViewOrigin().x, now.y - ViewOrigin().y) * scale;
			bool dragged = std::abs(now.x - s_DragStart.x) + std::abs(now.y - s_DragStart.y) > 8.0F;
			bool give = true;
			if (dropLine) {
				// The team being set up on the Battle tab has its ships come in over this line from now on.
				BattleSettings& setup = s_BattleSetup[std::clamp(s_BattleEditTeam, 0, c_Sides - 1)];
				setup.LineA = start;
				setup.LineB = dragged ? end : start;
				setup.HasLine = true;
				setup.DropOnLine = true;
				SendBattleSettings(s_BattleEditTeam);
				give = false;
			} else if (defendAt) {
				stroke.Kind = Tool::Command;
				stroke.Position = start;
				g_SceneMan.WrapPosition(stroke.Position);
				stroke.Position2 = end;
				stroke.Count = io.KeyShift ? 11 : 10;
			} else if (facingMove) {
				stroke.Kind = Tool::Command;
				stroke.Position = start;
				g_SceneMan.WrapPosition(stroke.Position);
				stroke.Position2 = end;
				stroke.Count = io.KeyShift ? 31 : 30;
			} else if (s_CommandMode == CommandMode::Patrol && CurrentTool().Kind == Tool::Command && !dragged) {
				// A point of the patrol route being clicked out; the command row starts it.
				g_SceneMan.WrapPosition(end);
				s_PatrolDraft.push_back(end);
				give = false;
			} else if (dragged) {
				stroke.Kind = Tool::Select;
				stroke.Position = start;
				stroke.Position2 = end;
			} else {
				stroke.Kind = Tool::Command;
				stroke.Position = end;
				g_SceneMan.WrapPosition(stroke.Position);
				stroke.Count = io.KeyShift ? 1 : (s_DoubleClick ? 2 : 0);
			}
			if (give) {
				s_Queue.push_back(stroke);
			}
		}
	}
	if (InGame()) {
		DrawSelection();
		DrawMinimap();
	}

	if (hiddenButAbove) {
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 445.0F, 40.0F), ImGuiCond_FirstUseEver);
	if (g_DebugMan.BeginPanel(IsGodMode() ? "Sandbox (F7)###Sandbox" : "Sandbox tools (F7)###Sandbox", &s_Open, DebugMan::PanelSide::Left, g_DebugMan.GetSandboxPlacement())) {
		if (!InGame()) {
			g_DebugMan.DrawToolWindowControls();
			ImGui::TextWrapped("Start a game to use the sandbox. Pick \"Sandbox\" on the main menu for the full god mode.");
			g_DebugMan.EndPanel();
			return;
		}
		if (!IsGodMode()) {
			// In other games there is no bar, so the window carries the main tools itself.
			g_DebugMan.DrawToolWindowControls();
			SideStatus();
			TimeControls();
			ImGui::TextDisabled("Left click: use tool.  Right drag / WASD: move camera.  Wheel: zoom.");
			ToolButtons({Tool::None, Tool::Command, Tool::Follow, Tool::Possess});
			ToolButtons({Tool::Remove, Tool::RallyPoint});
			CommanderPanel();
		}
		if (DrawTabRows()) {
			if (IsGodMode() && SandboxTab("You")) {
				s_CurrentTab = "You";
				bool exists = GetRef(s_PlayerUnit) != nullptr;
				if (ToolUI::Checkbox("Have a character of my own", &s_Player.EnterOnClose) && !s_Player.EnterOnClose) {
					Stroke stroke;
					stroke.Kind = Tool::PlayerRemove;
					s_Queue.push_back(stroke);
				}
				ImGui::SetItemTooltip("On: Tab puts the tools away and puts you in your character.\nOff: there is no character. Tab only hides and shows the tools, and you go on looking around from above.");
				if (!s_Player.EnterOnClose) {
					ImGui::TextWrapped("No character. Tab hides and shows these tools; with them hidden the right mouse button and WASD still move the view and the wheel zooms.");
					EndSandboxTab();
				} else {
				ImGui::TextWrapped("For walking about in what you've made. P puts you in it, and P again brings you back above; Shift+Tab puts it down where the mouse points and puts you in it. Tab hides and shows these tools: with a tool in hand you stay above and go on using it, with nothing in hand (Look around) Tab puts you in your character.");
				if (ToolUI::Button(exists ? "Play (Tab)" : "Make it and play (Tab)", ImVec2(-1.0F, 0.0F))) {
					Stroke stroke;
					stroke.Kind = Tool::PlayCharacter;
					stroke.Count = 0;
					s_Queue.push_back(stroke);
				}
				ToolButtons({Tool::PlayCharacter});
				ImGui::SameLine();
				ImGui::TextDisabled("then click where to start");
				ImGui::SeparatorText("What it can do");
				ToolUI::Checkbox("Can't be hurt", &s_Player.Unkillable);
				ImGui::SameLine();
				ToolUI::Checkbox("Endless jetpack", &s_Player.EndlessJetpack);
				ToolUI::Checkbox("Endless ammunition", &s_Player.EndlessAmmo);
				ToolUI::Checkbox("1 to 9 take out that item of the kit", &s_Player.NumberKeys);
				ToolUI::Checkbox("Enemies take no notice of it", &s_Player.Neutral);
				ImGui::SetItemTooltip("On: units run by the AI don't see your character as an enemy and don't pick it as a target, whatever side it is on. Stray shots and blasts still reach it.");
				ToolUI::Checkbox("N flies through anything", &s_Player.FlyKey);
				ImGui::SetItemTooltip("While playing, N lifts the character out of the physics: the movement keys fly it in any direction, through the ground and walls. N again drops it back in.");
				ImGui::SeparatorText("What it is");
				for (int side = 0; side < c_Sides; ++side) {
					if (side > 0) {
						ImGui::SameLine();
					}
					ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
					ToolUI::RadioButton((std::string(c_SideNames[side]) + "##you").c_str(), &s_Player.Team, side);
					ImGui::PopStyleColor();
				}
				static char kitFilter[48] = "";
				// The base class: any unit there is, picked from the same browser as the Spawn tab's, folded away until wanted.
				bool knownBody = FindPreset(s_Units, s_Player.Body) != nullptr;
				std::string baseHeader = "Base class: " + s_Player.Body + (knownBody ? "" : "  (not in this game)") + "###baseClass";
				bool baseOpen = ImGui::CollapsingHeader(baseHeader.c_str());
				ImGui::SetItemTooltip("The unit your character is made as. Open to pick any unit, as on the Spawn tab.");
				if (baseOpen) {
					ImGui::PushID("baseClass");
					PictureGrid(Tool::Unit, nullptr, &s_Player.Body);
					ImGui::PopID();
				}
				ToolUI::Checkbox("Inherit its equipment", &s_Player.InheritKit);
				ImGui::SetItemTooltip("On: the character also carries what a unit of this kind is spawned with (its own items and its faction's guns), besides the kit below.\nOff: it carries only the kit below.");
				ImGui::SeparatorText("What it carries");
				int removeItem = -1;
				for (size_t i = 0; i < s_Player.Kit.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));
					if (ToolUI::SmallButton("x")) {
						removeItem = static_cast<int>(i);
					}
					ImGui::SameLine();
					if (ToolUI::SmallButton("^") && i > 0) {
						std::swap(s_Player.Kit[i], s_Player.Kit[i - 1]);
					}
					ImGui::SameLine();
					bool known = FindPreset(s_Items, s_Player.Kit[i]) != nullptr;
					ImGui::TextColored(known ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%d  %s%s", static_cast<int>(i) + 1, s_Player.Kit[i].c_str(), known ? "" : "  (not in this game)");
					ImGui::PopID();
				}
				if (removeItem >= 0) {
					s_Player.Kit.erase(s_Player.Kit.begin() + removeItem);
				}
				if (ImGui::BeginCombo("##addKit", "Add an item...", ImGuiComboFlags_HeightLarge)) {
					ImGui::InputTextWithHint("##kitFilter", "Search...", kitFilter, sizeof(kitFilter));
					for (const Preset& item: s_Items) {
						if (ContainsIgnoringCase(item.Label, kitFilter) && ImGui::Selectable(item.Label.c_str())) {
							s_Player.Kit.push_back(item.PresetName);
						}
					}
					ImGui::EndCombo();
				}
				if (ToolUI::Button("Usual kit")) {
					s_Player.Kit = PlayerSetup().Kit;
				}
				ImGui::TextDisabled("A new base class or kit is used the next time the character is made.");
				ImGui::BeginDisabled(!exists);
				if (ToolUI::Button("Make it again now")) {
					Stroke stroke;
					stroke.Kind = Tool::PlayerRemake;
					s_Queue.push_back(stroke);
				}
				ImGui::SameLine();
				if (ToolUI::Button("Remove it")) {
					Stroke stroke;
					stroke.Kind = Tool::PlayerRemove;
					s_Queue.push_back(stroke);
				}
				ImGui::EndDisabled();
				EndSandboxTab();
				}
			}
			if (SandboxTab("Spawn")) {
				s_CurrentTab = "Spawn";
				ToolButtons({Tool::Unit, Tool::Drop, Tool::Brain, Tool::Item});
				if (CurrentTool().Kind == Tool::Structure) {
					s_ToolIndex = ToolIndex(Tool::Unit);
				}
				Tool kind = CurrentTool().Kind;
				if (kind == Tool::Unit || kind == Tool::Drop || kind == Tool::Brain || kind == Tool::Item) {
					PictureGrid(kind, nullptr);
				}
				if (kind == Tool::Unit || kind == Tool::Drop || kind == Tool::Brain || kind == Tool::Structure || kind == Tool::RallyPoint) {
					SideChooser();
				}
				if (kind == Tool::Drop) {
					ImGui::Combo("Craft", &s_Craft, "Dropship\0Rocket\0");
				}
				if (kind == Tool::Unit || kind == Tool::Drop) {
					ToolUI::Checkbox("Random units", &s_RandomUnits);
					ImGui::SetItemTooltip(kind == Tool::Drop ? "Each unit in the craft is picked at random, not the one chosen above." : "Each unit in the squad is picked at random, not the one chosen above. Squad size is how many.");
					if (s_RandomUnits) {
						ImGui::SameLine();
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6F);
						RandomSourceCombo("From##random", s_RandomFavourites, s_RandomFaction);
					}
				}
				if (kind == Tool::Unit || kind == Tool::Drop) {
					ToolUI::Checkbox("Jetpacks only", &s_JetpackOnly);
					ImGui::SetItemTooltip("Only units whose jetpack really flies them (lifts them 5 m or more). Units without one, or with one that only gives a hop, aren't listed or picked at random. Hover a unit to see its lift.");
					ImGui::SliderInt("Squad size", &s_SquadSize, 1, 10);
					LoadoutChooser();
					UnitOrderCombo("Orders");
				} else if (kind == Tool::Item) {
					ToolUI::Checkbox("Pull the pin (grenades)", &s_LitGrenade);
				} else if (kind == Tool::Structure) {
					ToolUI::Checkbox("Snap to the bunker grid", &s_SnapToGrid);
				}
				EndSandboxTab();
			}
			// The colony buildings work (scripts can still place them with SandboxDo) but their tab is hidden until they are taken further.
			if (c_ShowColonyTab && SandboxTab("Colony")) {
				s_CurrentTab = "Colony";
				ColonyTab();
				EndSandboxTab();
			}
			if (SandboxTab("Build")) {
				s_CurrentTab = "Build";
				// Coming to this tab picks up the building tool.
				static int shownLast = -10;
				if (ImGui::GetFrameCount() > shownLast + 1 || CurrentTool().Kind != Tool::Structure) {
					if (ImGui::GetFrameCount() > shownLast + 1) {
						s_ToolIndex = ToolIndex(Tool::Structure);
					}
				}
				shownLast = ImGui::GetFrameCount();
				ImGui::TextWrapped("Bunker pieces, placed straight into the world. Pick one: it follows the pointer as it will stand, and a click puts it there. Keep clicking to place more.");
				ToolButtons({Tool::Structure, Tool::Remove, Tool::None});
				const int groupCount = static_cast<int>(std::size(c_StructureGroups));
				std::string groupNames;
				for (const char* group: c_StructureGroups) {
					groupNames += std::string(group) + '\0';
				}
				groupNames += std::string("Everything") + '\0';
				ImGui::Combo("Kind", &s_StructureGroup, groupNames.c_str());
				PictureGrid(Tool::Structure, s_StructureGroup < groupCount ? c_StructureGroups[s_StructureGroup] : nullptr);
				ToolUI::Checkbox("Snap to the bunker grid", &s_SnapToGrid);
				ImGui::SetItemTooltip("On: pieces line up with each other on the 24 pixel grid bunkers are built on. Off: they go exactly where the pointer is.");
				ImGui::TextDisabled("Doors and turrets belong to:");
				SideChooser();
				ImGui::Separator();
				if (ToolUI::Button("The game's own build menu", ImVec2(-1.0F, 0.0F))) {
					// Placing through the game's build menu instead. Choose Done in its pie menu (or press Tab) to come back.
					Sandbox::SetBuildMode(true);
				}
				ImGui::SetItemTooltip("Puts these tools away and opens the build menu the game uses before a battle: everything it offers, moved and placed with the game's own cursor.\nDone in its menu, or Tab, comes back here.");
				EndSandboxTab();
			}
			if (SandboxTab("Orders")) {
				s_CurrentTab = "Orders";
				ImGui::TextWrapped("Give every unit on a side new orders. Units told to attack find a new target when theirs dies.");
				SideChooser();
				s_Order = std::clamp(s_Order, 0, c_OrderCount - 1);
				ImGui::Combo("Orders", &s_Order, OrderName, nullptr, c_OrderCount);
				if (static_cast<Order>(s_Order) == Order::MoveTo) {
					// Given by clicking the place: the pointer shows where each unit will stand first.
					ToolButtons({Tool::OrderMove});
					ImGui::TextWrapped("Click where the side should go. Each unit is shown a place of its own on the ground there, as close to the point as the ground allows, and the nearest unit takes the nearest place.");
				} else if (ToolUI::Button("Give orders", ImVec2(-1.0F, 0.0F))) {
					Stroke stroke;
					stroke.Kind = Tool::OrderSide;
					stroke.Team = s_Team;
					stroke.Orders = static_cast<Order>(s_Order);
					s_Queue.push_back(stroke);
				}
				if (ToolUI::Button("Everyone attack!", ImVec2(-1.0F, 0.0F))) {
					for (int side = 0; side < c_Sides; ++side) {
						Stroke stroke;
						stroke.Kind = Tool::OrderSide;
						stroke.Team = side;
						stroke.Orders = Order::Attack;
						s_Queue.push_back(stroke);
					}
				}
				if (ToolUI::Button("Remove this side's units", ImVec2(-1.0F, 0.0F))) {
					Stroke stroke;
					stroke.Kind = Tool::RemoveSide;
					stroke.Team = s_Team;
					s_Queue.push_back(stroke);
				}
				ImGui::TextDisabled("Rally point: pick the tool above and click to place this side's flag.");
				EndSandboxTab();
			}
			if (SandboxTab("Battle")) {
				s_CurrentTab = "Battle";
				BattleTab();
				EndSandboxTab();
			}
			if (IsGodMode() && SandboxTab("Gym")) {
				s_CurrentTab = "Gym";
				GymTab();
				EndSandboxTab();
			}
			if (SandboxTab("Paint")) {
				s_CurrentTab = "Paint";
				ImGui::SeparatorText("Elements");
				ToolButtons({Tool::Fire, Tool::Water, Tool::Lava, Tool::Acid, Tool::Oil, Tool::Smoke, Tool::ToxicGas});
				ToolButtons({Tool::Mud, Tool::Tar, Tool::Mercury, Tool::Fuel, Tool::Cryo, Tool::Blood, Tool::PourOther});
				{
					// Every other pourable (rubble, ash, mods' liquids), for the "Other" tool.
					std::vector<std::string> pourables = PourableNames();
					if (s_OtherPourable.empty() && !pourables.empty()) {
						s_OtherPourable = std::find(pourables.begin(), pourables.end(), "Earth Rubble") != pourables.end() ? "Earth Rubble" : pourables.front();
					}
					if (ImGui::BeginCombo("More...", s_OtherPourable.empty() ? "(nothing pourable)" : s_OtherPourable.c_str())) {
						for (const std::string& name: pourables) {
							if (ImGui::Selectable(name.c_str(), name == s_OtherPourable)) {
								s_OtherPourable = name;
								TookTool(ToolIndex(Tool::PourOther));
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SetItemTooltip("Every liquid and powder the game pours, mods' included. Picking one takes the Other tool.");
				}
				ImGui::SliderFloat("Flow", &s_Flow, 0.1F, 1.0F, "%.2f");
				ImGui::SetItemTooltip("How fast the liquid and loose-ground brushes pour while held. 1: as fast as they go.");
				ImGui::SeparatorText("Springs");
				ToolButtons({Tool::WaterSpawner});
				ImGui::SameLine();
				ImGui::BeginDisabled(s_WaterSpawners.empty());
				if (ToolUI::Button("Remove all springs")) {
					QueueSimChange(Tool::ClearWaterSpawners);
				}
				ImGui::EndDisabled();
				{
					// Remove all of one kind: the kinds placed, each with how many.
					static std::string removeKind;
					std::vector<std::pair<std::string, int>> kinds = SpringCounts();
					auto chosen = std::find_if(kinds.begin(), kinds.end(), [](const auto& kind) { return kind.first == removeKind; });
					if (chosen == kinds.end() && !kinds.empty()) {
						removeKind = kinds.front().first;
						chosen = kinds.begin();
					}
					ImGui::BeginDisabled(kinds.empty());
					std::string shown = chosen != kinds.end() ? chosen->first + " " + std::to_string(chosen->second) : "(none placed)";
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5F);
					if (ImGui::BeginCombo("##removeKind", shown.c_str())) {
						for (const auto& [name, count]: kinds) {
							if (ImGui::Selectable((name + " " + std::to_string(count)).c_str(), name == removeKind)) {
								removeKind = name;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					if (ToolUI::Button(("Remove all " + (chosen != kinds.end() ? removeKind : std::string("of one kind"))).c_str())) {
						Stroke stroke;
						stroke.Kind = Tool::ClearWaterSpawners;
						stroke.Material = removeKind;
						s_Queue.push_back(stroke);
					}
					ImGui::EndDisabled();
				}
				if (ImGui::BeginCombo("Springs pour", s_SpringLiquid.c_str())) {
					for (const std::string& name: PourableNames()) {
						if (ImGui::Selectable(name.c_str(), name == s_SpringLiquid)) {
							s_SpringLiquid = name;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("What new springs pour, and what the Boom tab's tank is filled with.");
				ImGui::SliderFloat("Spring rate", &s_SpringRate, 0.05F, 1.0F, "%.2f");
				ImGui::SetItemTooltip("How much of the time new springs pour. 1: they keep the air around them full.");
				// Each spring: what it pours, on or off, removed (as the overlay's Delete does, from the ImGui frame).
				int removeSpring = -1;
				for (size_t i = 0; i < s_WaterSpawners.size(); ++i) {
					WaterSpawner& spring = s_WaterSpawners[i];
					ImGui::PushID(static_cast<int>(i));
					ToolUI::Checkbox("##on", &spring.On);
					ImGui::SetItemTooltip("Pouring. Off, it stays put and pours nothing.");
					ImGui::SameLine();
					{
						// (A swatch in the colour of what it pours, as on the map.)
						float side = ImGui::GetTextLineHeight() * 0.7F;
						ImVec2 at = ImGui::GetCursorScreenPos();
						ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + side * 0.2F), ImVec2(at.x + side, at.y + side * 1.2F), MaterialMarkColor(spring.Liquid, spring.On ? 255 : 110));
						ImGui::Dummy(ImVec2(side, 0.0F));
						ImGui::SameLine();
					}
					ImGui::Text("%s, %d px, at %d,%d", spring.Liquid.c_str(), spring.Radius, spring.Position.GetFloorIntX(), spring.Position.GetFloorIntY());
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4F);
					ImGui::SliderFloat("##rate", &spring.Rate, 0.05F, 1.0F, "rate %.2f");
					ImGui::SameLine();
					if (ToolUI::Button("x")) {
						removeSpring = static_cast<int>(i);
					}
					ImGui::PopID();
				}
				if (removeSpring >= 0) {
					s_WaterSpawners.erase(s_WaterSpawners.begin() + removeSpring);
				}
				ImGui::SeparatorText("Loose things");
				ToolButtons({Tool::LooseSand, Tool::LooseSnow, Tool::Gravel, Tool::GlassShards, Tool::Boulder, Tool::Slab});
				ImGui::SeparatorText("Plants");
				ToolButtons({Tool::Plants, Tool::Cacti});
				ImGui::SliderInt("Plant spacing", &s_PlantSpacing, 2, 60, "%d px");
				ImGui::SetItemTooltip("How far apart the plants go along a stroke. Each is one of the game's own plant pictures, set into the ground under the pointer.");
				ImGui::SeparatorText("Terrain");
				ToolButtons({Tool::Dig, Tool::Earth, Tool::Sand, Tool::Ice, Tool::Grass, Tool::Wood, Tool::Concrete});
				ToolButtons({Tool::Stone, Tool::DenseEarth, Tool::GoldEarth, Tool::TerrainOther});
				{
					// The rest of the base game's ground, for the "Other terrain" tool.
					if (ImGui::BeginCombo("More terrain...", s_OtherTerrain.c_str())) {
						for (const char* name: c_TerrainMaterials) {
							const Material* material = g_SceneMan.GetMaterial(name);
							if (!material || material->GetIndex() == g_MaterialAir) {
								continue;
							}
							if (ImGui::Selectable(name, s_OtherTerrain == name)) {
								s_OtherTerrain = name;
								TookTool(ToolIndex(Tool::TerrainOther));
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SetItemTooltip("The base game's ground materials. Picking one takes the Other terrain tool.");
				}
				ImGui::SliderInt("Brush size", &s_Radius, 1, 40);
				int shape = static_cast<int>(s_BrushShape);
				ImGui::TextUnformatted("Brush shape");
				ImGui::SameLine();
				ImGui::RadioButton("Circle", &shape, 0);
				ImGui::SameLine();
				ImGui::RadioButton("Square", &shape, 1);
				ImGui::SameLine();
				ImGui::RadioButton("Spray", &shape, 2);
				s_BrushShape = static_cast<BrushShape>(shape);
				ImGui::SetItemTooltip("What the terrain brushes (Dig and the materials) paint and dig: a circle, a square as wide as the brush, or a soft spray that scatters it over the circle, thickest in the middle, building up while held.");
				ImGui::BeginDisabled(s_PaintUndo.empty());
				if (ToolUI::Button("Undo terrain")) {
					QueueSimChange(Tool::UndoTerrain);
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Puts back the terrain the last brush stroke or built thing changed (Ctrl+Z). The last 20 can be undone, one at a time, up to about 8 million pixels in all: the oldest go first, and a stroke held for more than a few seconds is undone in parts.");
				EndSandboxTab();
			}
			if (SandboxTab("Boom")) {
				s_CurrentTab = "Boom";
				ToolButtons({Tool::Grenade, Tool::BigBomb, Tool::Napalm, Tool::Lightning});
				ImGui::SeparatorText("Craters");
				ToolButtons({Tool::Demolition, Tool::BunkerBuster, Tool::Meteor});
				ImGui::SeparatorText("From the sky: click where it should land");
				ToolButtons({Tool::RocketStrike, Tool::RocketBarrage, Tool::CarpetBomb, Tool::Artillery, Tool::NapalmRain, Tool::OrbitalBeam, Tool::BoulderRain});
				ImGui::SeparatorText("Craft that don't make it: click where it comes down");
				ToolButtons({Tool::CrashRocket, Tool::CrashDropship});
				if (!s_Incoming.empty()) {
					ImGui::TextDisabled("%d on the way", static_cast<int>(s_Incoming.size()));
				}
				ImGui::SeparatorText("Things to knock down");
				ToolButtons({Tool::BuildBeam, Tool::BuildPillar, Tool::BuildRoom, Tool::BuildTower, Tool::BuildBridge, Tool::BuildIsland, Tool::BuildTank});
				EndSandboxTab();
			}
			if (SandboxTab("Effects")) {
				s_CurrentTab = "Effects";
				ImGui::TextWrapped("Pick one, then click in the world to put it down. They keep running until removed.");
				ImGui::SeparatorText("Lights");
				auto effectButtons = [](std::initializer_list<EffectKind> kinds) {
					int column = 0;
					for (EffectKind kind: kinds) {
						if (column++ % 3 != 0) {
							ImGui::SameLine();
						}
						int index = static_cast<int>(kind);
						bool chosen = c_Tools[s_ToolIndex].Kind == Tool::Effect && s_EffectChoice == index;
						if (ToolUI::RadioButton(c_Effects[index].Name, chosen)) {
							s_EffectChoice = index;
							TookTool(ToolIndex(Tool::Effect));
						}
						ImGui::SetItemTooltip("%s", c_Effects[index].Tip);
					}
				};
				effectButtons({EffectKind::NuclearGlow, EffectKind::StormCell, EffectKind::RedAlarm, EffectKind::PoliceLights, EffectKind::BlueBeacon, EffectKind::Floodlight, EffectKind::Searchlight, EffectKind::Strobe, EffectKind::Disco,
				               EffectKind::Candle, EffectKind::LavaGlow, EffectKind::Fireflies});
				ImGui::SeparatorText("Lights with particles");
				effectButtons({EffectKind::Campfire, EffectKind::WeldingArc, EffectKind::Portal, EffectKind::SparkFountain, EffectKind::FireJet, EffectKind::ToxicVent});
				ImGui::SeparatorText("Particles and air");
				effectButtons({EffectKind::EmberVent, EffectKind::SmokeStack, EffectKind::SmokePlume, EffectKind::MistVent, EffectKind::DustDevil, EffectKind::HeatShimmer, EffectKind::ShockwavePulse});
				ImGui::Separator();
				ImGui::BeginDisabled(s_Effects.empty());
				if (ToolUI::Button("Remove all effects")) {
					QueueSimChange(Tool::ClearEffects);
				}
				ImGui::SameLine();
				if (ToolUI::Button("Remove the last one")) {
					QueueSimChange(Tool::ClearEffects, 1);
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::TextDisabled("%d running", static_cast<int>(s_Effects.size()));
				EndSandboxTab();
			}
			if (SandboxTab("World")) {
				s_CurrentTab = "World";
				if (IsGodMode()) {
					SideStatus();
					TimeControls();
					ImGui::Separator();
				}
				LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
				ImGui::Combo("Weather", &settings.WeatherType, Weather::GetComboItems(settings.CustomWeather).c_str());
				ImGui::SliderFloat("Intensity", &settings.WeatherIntensity, 0.0F, 1.0F);
				ImGui::SliderFloat("Wind", &settings.Wind, -200.0F, 200.0F, "%.0f px/s");
				ImGui::SliderFloat("Hour", &settings.TimeOfDay, 0.0F, 24.0F, "%.1f");
				for (auto [name, hour]: {std::pair{"Dawn", 6.5F}, std::pair{"Day", 12.0F}, std::pair{"Dusk", 19.0F}, std::pair{"Night", 23.0F}}) {
					if (hour != 6.5F) {
						ImGui::SameLine();
					}
					if (ToolUI::Button(name)) {
						settings.TimeOfDay = hour;
					}
				}
				ImGui::Separator();
				float zoom = g_FrameMan.GetCameraZoom();
				if (ImGui::SliderFloat("Zoom", &zoom, FrameMan::c_MinCameraZoom, FrameMan::c_MaxCameraZoom, "%.2fx")) {
					g_FrameMan.SetCameraZoom(zoom);
				}
				ImGui::SameLine();
				if (ToolUI::SmallButton("1x")) {
					g_FrameMan.SetCameraZoom(1.0F);
				}
				ToolUI::Checkbox("Free camera", &s_FreeCamera);
				ImGui::SameLine();
				ToolUI::Checkbox("Follow the action", &s_FollowAction);
				ImGui::SameLine();
				// (Ticked whenever time runs slow, however it was set: set from the speed slider, the box read unticked at 0.25x.)
				s_SlowMotion = g_TimerMan.GetTimeScale() < 0.99F;
				if (ToolUI::Checkbox("Slow motion", &s_SlowMotion)) {
					g_TimerMan.SetTimeScale(s_SlowMotion ? 0.25F : 1.0F);
				}
				// The world simulations the Paint tools need, here as well as in F6, so a brush that does nothing says why and can be fixed on the spot.
				ImGui::SeparatorText("Simulations");
				{
					bool liquids = FluidSim::IsEnabled();
					if (ToolUI::Checkbox("Flowing liquids", &liquids)) {
						FluidSim::SetEnabled(liquids);
					}
					ImGui::SetItemTooltip("Off: liquids stay where they are and nothing more can be poured.");
					ImGui::SameLine();
					bool powders = FluidSim::PowdersEnabled();
					if (ToolUI::Checkbox("Loose ground", &powders)) {
						FluidSim::SetPowdersEnabled(powders);
					}
					ImGui::SetItemTooltip("Sand, snow, gravel and glass slide and pile. Off: they can't be poured.");
					ImGui::SameLine();
					bool blood = FluidSim::BloodFlows();
					if (ToolUI::Checkbox("Blood flows", &blood)) {
						FluidSim::SetBloodFlows(blood);
					}
					ImGui::SetItemTooltip("Spilt blood runs and pools, then soaks away. Off: it stays where it fell.");
					ImGui::SameLine();
					bool freezing = FluidSim::FreezingEnabled();
					if (ToolUI::Checkbox("Freezing", &freezing)) {
						FluidSim::SetFreezingEnabled(freezing);
					}
					ImGui::SetItemTooltip("Still water freezes over in snowy weather.");
				}
				ImGui::Text("%d burning, %d liquid pixels flowing", TerrainFire::GetCount(), FluidSim::GetActiveCount());
				if (ToolUI::Button("Put out all fire")) {
					TerrainFire::Clear();
				}
				EndSandboxTab();
			}
			if (SandboxTab("Keys")) {
				s_CurrentTab = "Keys";
				KeysPage();
				EndSandboxTab();
			}
		}
	}
	g_DebugMan.EndPanel();
}

void Sandbox::OnActivityStarted() {
	// (Called by ActivityMan::StartActivity for every game started, loaded or restarted, before its own start-up runs. A new game used to be
	// told by the activity's address changing, in two places, which a new game allocated where the last one was would not have changed.)
	Controller::SetAIPaused(false);
	Colony::Clear();
	// The bar is up at the start of every game, even if U put it away in the last one.
	s_BarShown = true;
	// A new game: nothing is left pouring or on its way in from the last one.
	s_WaterSpawners.clear();
	s_Incoming.clear();
	s_Effects.clear();
	s_PaintUndo.clear();
	// The same random stream from the start of every game, so the same inputs give the same game.
	s_Random = c_RandomSeed;
	// And none of the last game's units, orders or battle: in any game, not only a Sandbox one. (Reset only when the god view opened, an
	// auto battle started in a skirmish kept landing waves in the next game, and the selection, groups and rally points pointed into it.)
	s_Possessed = nullptr;
	s_PlayerUnit = UnitRef();
	s_PlayerEnterPending = 0;
	s_Flying = false;
	s_StepsWanted = 0;
	s_RallySet.fill(false);
	s_Selected.clear();
	for (std::vector<UnitRef>& group: s_Groups) {
		group.clear();
	}
	s_OrderMarks.clear();
	s_FollowTarget = UnitRef();
	ForgetBattle();
	s_PendingOrders.clear();
	s_Commander = false;
	// (And clicks queued in the last game, not yet applied: they were applied to this one.)
	s_Queue.clear();
	s_GodViewPending = true;
}

void Sandbox::Update() {
	std::vector<Stroke> strokes;
	strokes.swap(s_Queue);
	if (!InGame()) {
		s_Possessed = nullptr;
		s_Plans.clear();
		s_GuardPosts.clear();
		s_Commander = false;
		s_Paced.clear();
		s_MoveWatch.clear();
		s_NoRoutes.clear();
		s_Incoming.clear();
		s_WaterSpawners.clear();
		s_Effects.clear();
		return;
	}
	ApplyPendingOrders();
	s_StrokesApplied = strokes.size();
	for (const Stroke& stroke: strokes) {
		Apply(stroke);
	}
	ClosePaintUndoStep(false);
	UpdatePlans();
	UpdateCommander();
	UpdatePace();
	UpdateMoveWatch();
	UpdateIncoming();
	UpdateEffects();
	for (WaterSpawner& spawner: s_WaterSpawners) {
		if (!spawner.On) {
			continue;
		}
		spawner.Due += std::clamp(spawner.Rate, 0.05F, 1.0F);
		if (spawner.Due >= 1.0F) {
			spawner.Due -= 1.0F;
			FluidSim::Pour(spawner.Position, static_cast<float>(spawner.Radius), spawner.Liquid.c_str());
		}
	}
	// With the AI paused, the sandbox's own passes wait too: they walked defenders home once a second, and the battle kept dropping waves,
	// all on units held still. (Its wave clocks are held back as well, so the waves don't all come at once after.) Attackers pick their own
	// enemies in their AI (SharedBehaviors.AttackOrderUpdate), which the pause holds as it is.
	const bool aiPaused = Controller::IsAIPaused();
	if (!aiPaused && g_TimerMan.GetSimUpdateCount() % 60 == 0) {
		ReturnDefenders();
	}
	if (!aiPaused && g_TimerMan.GetSimUpdateCount() % 30 == 0) {
		UpdateGuards();
	}
	GymUpdate();
	// (No sandbox-side watchdog for units that have stopped: getting unstuck, waiting for fuel before a tall climb, and giving up on a route that
	// can't be had are the AI's own business now, and re-ordering a unit every three seconds only restarted whatever it was in the middle of.)
	UpdateBattle(aiPaused);
	Colony::Update();
	if (s_FollowAction && g_TimerMan.GetSimUpdateCount() % 30 == 0) {
		FindAction();
	}
	if (IsGodMode()) {
		GameActivity* game = CurrentGame();
		if (s_Possessed && (!g_MovableMan.IsActor(s_Possessed) || static_cast<long>(s_Possessed->GetUniqueID()) != s_PossessedID)) {
			// The unit you were controlling died: back to the god view.
			g_ConsoleMan.PrintString("SANDBOX: The unit you were controlling is gone; back to the god view.");
			s_Possessed = nullptr;
			s_Flying = false;
			g_DebugMan.OpenTools();
			s_FreeCameraStarted = false;
		} else if (s_Possessed && game && s_PlayerEnterPending == 0) {
			// The unit the game says you control is the one you control. Its own next/previous actor keys stay live while you're in a unit,
			// and switch among the activity's side (Red in the sandbox), which the sandbox never heard of: it went on watching the unit you
			// left, Tab released whichever the game had, and the character kept flying under the AI's keys.
			// A switch within the unit's own side is followed; one that crossed to another side is undone, back into the unit you were in.
			Actor* controlled = game->GetControlledActor(Players::PlayerOne);
			if (controlled != s_Possessed) {
				if (controlled && g_MovableMan.IsActor(controlled) && controlled->GetTeam() == s_Possessed->GetTeam()) {
					if (s_Possessed == GetRef(s_PlayerUnit)) {
						StopFlying();
					}
					SetPossessed(controlled);
				} else if (game->SwitchToActor(s_Possessed, Players::PlayerOne, s_Possessed->GetTeam())) {
					game->SetViewState(Activity::ViewState::Normal, Players::PlayerOne);
				} else {
					// (Not to be had back: to the god view, as when it dies.)
					StopFlying();
					s_Possessed = nullptr;
					g_DebugMan.OpenTools();
					s_FreeCameraStarted = false;
				}
			}
		}
		UpdatePlayer();
		if (game) {
			for (int team = 0; team < c_Sides; ++team) {
				if (game->GetTeamFunds(team) < 500000.0F) {
					game->SetTeamFunds(1000000.0F, team);
				}
			}
		}
		// The god doesn't get handed a unit: stay watching unless controlling one on purpose.
		if (game && !s_Possessed && game->GetViewState(Players::PlayerOne) != Activity::ViewState::Observe) {
			if (game->GetControlledActor(Players::PlayerOne)) {
				game->LoseControlOfActor(Players::PlayerOne);
			}
			game->SetViewState(Activity::ViewState::Observe, Players::PlayerOne);
		}
	}
}

void Sandbox::DrawOrderLabels() {
	if (!g_SettingsMan.ShowOrderLabels() || !g_ActivityMan.GetActivity()) {
		return;
	}
	// The AI modes (Actor::AIMode's order) in the words of the sandbox's orders.
	static const char* const modeOrders[] = {"do nothing", "hold", "patrol", "move", "hunt brains", "dig gold", "go back", "stay", "scuttle", "deliver", "bomb", "squad"};
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	GameViewRect view = g_WindowMan.GetGameViewRect();
	bool aiPaused = Controller::IsAIPaused();
	float lineHeight = ImGui::GetTextLineHeight();
	float pad = std::max(2.0F, lineHeight * 0.2F);
	for (const Actor* actor: SandboxAccess::Actors()) {
		if (!IsCombatant(actor)) {
			continue;
		}
		// Under the feet: a little below the unit's middle, by its size.
		ImVec2 at = ToScreen(actor->GetPos() + Vector(0.0F, actor->GetRadius() * 0.8F));
		if (at.x < view.x - 100.0F || at.x > view.x + view.w + 100.0F || at.y < view.y - 40.0F || at.y > view.y + view.h + 40.0F) {
			continue;
		}
		std::string order;
		if (actor->IsPlayerControlled()) {
			order = "player";
		} else if (actor->GetOrderTargetID() != 0) {
			order = "attack #" + std::to_string(static_cast<long long>(actor->GetOrderTargetID()));
		} else if (actor->GetOrderHasAttackPlace()) {
			order = "attack towards a place";
		} else if (actor->GetOrderAttack()) {
			order = "attack nearest";
		} else if (actor->GetOrderHasPost()) {
			order = "defend a spot";
		} else {
			int mode = actor->GetAIMode();
			order = mode >= 0 && mode < static_cast<int>(std::size(modeOrders)) ? modeOrders[mode] : "mode " + std::to_string(mode);
		}
		if (!actor->IsPlayerControlled()) {
			if (actor->NumberValueExists("AIRetreat")) {
				order += actor->GetNumberValue("AIRetreat") == 2 ? ", falling back to a medic" : ", falling back";
			} else if (actor->NumberValueExists("AIFlank")) {
				order += ", flanking";
			} else if (actor->NumberValueExists("AIInvestigate")) {
				order += ", checking where an enemy was seen";
			} else if (actor->NumberValueExists("AIMedic")) {
				order += ", seeing to a wounded friend";
			}
		}
		for (size_t group = 0; group < s_Groups.size(); ++group) {
			if (std::any_of(s_Groups[group].begin(), s_Groups[group].end(), [actor](const UnitRef& ref) { return GetRef(ref) == actor; })) {
				order += "  [" + std::to_string(group) + "]";
				break;
			}
		}
		bool paused = aiPaused && !actor->IsPlayerControlled();
		const char* badge = "AI paused";
		ImVec2 size = ImGui::CalcTextSize(order.c_str());
		float width = std::max(size.x, paused ? ImGui::CalcTextSize(badge).x : 0.0F);
		float height = lineHeight * (paused ? 2.0F : 1.0F);
		ImVec2 topLeft(std::floor(at.x - width * 0.5F - pad), std::floor(at.y));
		ImVec2 bottomRight(topLeft.x + width + pad * 2.0F, topLeft.y + height + pad * 2.0F);
		drawList->AddRectFilled(topLeft, bottomRight, IM_COL32(10, 12, 10, 170));
		drawList->AddText(ImVec2(topLeft.x + pad, topLeft.y + pad), c_SideColors[actor->GetTeam()], order.c_str());
		if (paused) {
			drawList->AddText(ImVec2(topLeft.x + pad, topLeft.y + pad + lineHeight), IM_COL32(255, 210, 80, 255), badge);
		}
	}
}
