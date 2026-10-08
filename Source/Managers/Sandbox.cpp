#include "SandboxInternal.h"
#include "ActorWater.h"
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
	for (int i = 0; i < c_ToolCount; ++i) {
		std::string name = c_Tools[i].Name;
		if (name.size() == toolName.size() && ContainsIgnoringCase(name, toolName.c_str())) {
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
	if (stroke.Kind == Tool::Unit || stroke.Kind == Tool::Drop || stroke.Kind == Tool::Brain || stroke.Kind == Tool::Item || stroke.Kind == Tool::Structure || stroke.Kind == Tool::Barracks) {
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

void Sandbox::SetAutoBattleSide(int team, const std::string& faction, int budget) {
	if (!s_CatalogueBuilt && InGame()) {
		BuildCatalogue();
	}
	if (team < 0 || team >= c_Sides) {
		return;
	}
	AutoSide& autoSide = s_AutoSides[team];
	autoSide.Active = budget > 0;
	autoSide.Budget = budget;
	for (size_t i = 0; i < s_FactionNames.size(); ++i) {
		if (s_FactionNames[i] == faction || s_FactionNames[i] + ".rte" == faction) {
			autoSide.Faction = static_cast<int>(i);
		}
	}
}

void Sandbox::SetAIPaused(bool paused) {
	Controller::SetAIPaused(paused);
}

void Sandbox::StartAutoBattle() {
	if (!InGame()) {
		return;
	}
	// (A script's battle is between the factions it set up for each side.)
	s_AutoRandom = false;
	s_AutoFavourites = false;
	BeginAutoBattle(g_CameraMan.GetOffset(0) + Vector(static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F, static_cast<float>(g_FrameMan.GetPlayerScreenHeight()) * 0.5F), static_cast<float>(g_FrameMan.GetPlayerScreenWidth()));
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
		// was in the air was "gone", and the auto battle was called for the other side.)
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
	for (bool flag: {s_Player.Unkillable, s_Player.EndlessJetpack, s_Player.EndlessAmmo, s_Player.NumberKeys, s_Player.FlyKey, s_Player.EnterOnClose, s_PauseInMenus, s_Player.Neutral}) {
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

void Sandbox::SetPins(const std::string& pins) {
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
	bool* flags[] = {&s_Player.Unkillable, &s_Player.EndlessJetpack, &s_Player.EndlessAmmo, &s_Player.NumberKeys, &s_Player.FlyKey, &s_Player.EnterOnClose, &s_PauseInMenus, &s_Player.Neutral};
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

bool Sandbox::IsLookingAround() {
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
			s_AutoRunning = false;
			s_AutoWinner = -2;
			s_ToolIndex = ToolIndex(Tool::Unit);
		}
	} else {
		if (s_GodViewSetUp) {
			// Left the sandbox: its pictures aren't needed until it's next opened.
			ForgetPictures();
		}
		s_GodViewSetUp = false;
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
			g_TimerMan.PauseSim(true);
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
		} else if (s_PausedByMenus) {
			s_PausedByMenus = false;
			s_StepsWanted = 0;
			if (!g_DebugMan.IsPhotoModeOpen()) {
				g_TimerMan.PauseSim(false);
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
			hint += "    Right drag / WASD: move    Wheel: zoom";
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
	// With the tools hidden in the Sandbox game mode you're still above it all: the view goes on moving with the mouse and keys, and the tool in hand goes on
	// working. Only the window itself is left out.
	bool hiddenButAbove = !s_Open && IsLookingAround();
	// The bar is there whenever you're above the world and not playing a unit, whatever else is open or hidden.
	if (IsGodMode() && InGame() && !s_Possessed && s_PlayerEnterPending == 0 && !g_DebugMan.IsPhotoModeHidingHUD()) {
		if (!s_CatalogueBuilt) {
			BuildCatalogue();
		}
		DrawBar();
	}
	if (!s_Open && !hiddenButAbove) {
		// The selection's arrows come off while the window is away (they stayed on the units of a game with the window shut, till it was
		// opened again); the selection itself is kept for when it is.
		UnmarkSelection();
		if (s_FreeCameraStarted && IsGodMode() && std::getenv("CCCP_HIDE_PANELS") != nullptr) {
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
			if (std::getenv("CCCP_TEST_RING") && !s_RingOpen) {
				s_RingOpen = true;
				s_RingCenter = ImVec2(x - 40.0F, y - 10.0F);
			}
		}
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
		} else if (tool.Interval <= 0.0F) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				QueueStroke(tool.Kind, position);
			}
		} else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			s_StrokeTimer -= io.DeltaTime;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || s_StrokeTimer <= 0.0F) {
				s_StrokeTimer = tool.Interval;
				QueueStroke(tool.Kind, position);
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
		if (defendAt || facingMove) {
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
			if (defendAt) {
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
	if (g_DebugMan.BeginPanel(IsGodMode() ? "Sandbox (F7)###Sandbox" : "Sandbox tools (F7)###Sandbox", &s_Open, DebugMan::PanelSide::Left)) {
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
		}
		if (ImGui::BeginTabBar("SandboxTabs")) {
			if (IsGodMode() && ImGui::BeginTabItem("You", nullptr, TestTab("You"))) {
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
					ImGui::EndTabItem();
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
				static char bodyFilter[48] = "";
				static char kitFilter[48] = "";
				if (ImGui::BeginCombo("Body", s_Player.Body.c_str(), ImGuiComboFlags_HeightLarge)) {
					ImGui::InputTextWithHint("##bodyFilter", "Search...", bodyFilter, sizeof(bodyFilter));
					for (const Preset& unit: s_Units) {
						if (ContainsIgnoringCase(unit.Label, bodyFilter) && ImGui::Selectable(unit.Label.c_str(), unit.PresetName == s_Player.Body)) {
							s_Player.Body = unit.PresetName;
						}
					}
					ImGui::EndCombo();
				}
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
				ImGui::TextDisabled("A new body or kit is used the next time the character is made.");
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
				ImGui::EndTabItem();
				}
			}
			if (ImGui::BeginTabItem("Spawn", nullptr, TestTab("Spawn"))) {
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
					ToolUI::Checkbox("Random units", &s_DropRandom);
					ImGui::SetItemTooltip("Each unit in the craft is picked at random from every faction's units, not the one chosen above.");
					if (s_DropRandom) {
						ImGui::SameLine();
						ToolUI::Checkbox("Favourites only##drop", &s_DropFavourites);
						ImGui::SetItemTooltip("Picks only from the units marked as favourites (Ctrl+click on a tile). With none marked, from every unit.");
					}
				}
				if (kind == Tool::Unit || kind == Tool::Drop) {
					ImGui::SliderInt("Squad size", &s_SquadSize, 1, 10);
					LoadoutChooser();
					UnitOrderCombo("Orders");
				} else if (kind == Tool::Item) {
					ToolUI::Checkbox("Pull the pin (grenades)", &s_LitGrenade);
				} else if (kind == Tool::Structure) {
					ToolUI::Checkbox("Snap to the bunker grid", &s_SnapToGrid);
				}
				ImGui::EndTabItem();
			}
			// The colony buildings work (scripts can still place them with SandboxDo) but their tab is hidden until they are taken further.
			if (c_ShowColonyTab && ImGui::BeginTabItem("Colony", nullptr, TestTab("Colony"))) {
				s_CurrentTab = "Colony";
				ColonyTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Build", nullptr, TestTab("Build"))) {
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
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Orders", nullptr, TestTab("Orders"))) {
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

				ImGui::SeparatorText("Auto battle");
				ImGui::TextWrapped("Each side buys waves of units with its budget and drops them in to attack, until one side is left.");
				ImGui::SliderInt("Sides", &s_AutoSideCount, 2, c_Sides);
				ImGui::SetItemTooltip("Red and Green, then Blue, then Yellow.");
				ImGui::SliderInt("Budget per side", &s_AutoBudget, 500, 50000, "%d oz", ImGuiSliderFlags_Logarithmic);
				ToolUI::Checkbox("Random units", &s_AutoRandomChoice);
				ImGui::SetItemTooltip("Every wave is random units from every faction. Off, each side buys from a faction of its own.");
				if (s_AutoRandomChoice) {
					ImGui::SameLine();
					ToolUI::Checkbox("Favourites only##auto", &s_AutoFavouritesChoice);
					ImGui::SetItemTooltip("Picks only from the units marked as favourites (Ctrl+click on a tile). With none marked, from every unit.");
				}
				if (ToolUI::Button(s_AutoRunning ? "Start again" : "Start auto battle", ImVec2(s_AutoRunning ? ImGui::GetContentRegionAvail().x * 0.5F : -1.0F, 0.0F))) {
					Stroke stroke;
					stroke.Kind = Tool::AutoBattle;
					stroke.Count = s_AutoSideCount;
					stroke.Choice = s_AutoBudget;
					stroke.Random = s_AutoRandomChoice;
					stroke.FavouritesOnly = s_AutoFavouritesChoice;
					// (The view's middle and width now, so the sim doesn't read the camera: see S4.)
					stroke.Position = g_CameraMan.GetOffset(0) + Vector(static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F, static_cast<float>(g_FrameMan.GetPlayerScreenHeight()) * 0.5F);
					stroke.Radius = g_FrameMan.GetPlayerScreenWidth();
					s_Queue.push_back(stroke);
				}
				if (s_AutoRunning) {
					ImGui::SameLine();
					if (ToolUI::Button("Stop", ImVec2(-1.0F, 0.0F))) {
						QueueSimChange(Tool::AutoBattle, 0);
					}
				}
				ImGui::EndTabItem();
			}
			if (IsGodMode() && ImGui::BeginTabItem("Gym", nullptr, TestTab("Gym"))) {
				s_CurrentTab = "Gym";
				GymTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Paint", nullptr, TestTab("Paint"))) {
				s_CurrentTab = "Paint";
				ImGui::SeparatorText("Elements");
				ToolButtons({Tool::Fire, Tool::Water, Tool::Lava, Tool::Acid, Tool::Oil, Tool::Smoke, Tool::ToxicGas});
				ToolButtons({Tool::Mud, Tool::Tar, Tool::Mercury, Tool::Fuel, Tool::Cryo});
				ImGui::SeparatorText("Water that keeps coming");
				ToolButtons({Tool::WaterSpawner});
				ImGui::SetItemTooltip("Click to place a spring that pours water for good, as wide as the brush size below. Place as many as you like.");
				ImGui::SameLine();
				ImGui::BeginDisabled(s_WaterSpawners.empty());
				if (ToolUI::Button("Remove all water spawners")) {
					QueueSimChange(Tool::ClearWaterSpawners);
				}
				ImGui::EndDisabled();
				if (!s_WaterSpawners.empty()) {
					ImGui::SameLine();
					ImGui::TextDisabled("%d pouring", static_cast<int>(s_WaterSpawners.size()));
				}
				ImGui::SeparatorText("Loose things");
				ToolButtons({Tool::LooseSand, Tool::LooseSnow, Tool::Gravel, Tool::GlassShards, Tool::Boulder, Tool::Slab});
				ImGui::SeparatorText("Terrain");
				ToolButtons({Tool::Dig, Tool::Earth, Tool::Sand, Tool::Ice, Tool::Grass, Tool::Wood, Tool::Concrete});
				ImGui::SliderInt("Brush size", &s_Radius, 1, 40);
				ImGui::BeginDisabled(s_PaintUndo.empty());
				if (ToolUI::Button("Undo terrain")) {
					QueueSimChange(Tool::UndoTerrain);
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Puts back the terrain the last brush stroke or built thing changed (Ctrl+Z). The last 20 can be undone, one at a time.");
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Boom", nullptr, TestTab("Boom"))) {
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
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Effects", nullptr, TestTab("Effects"))) {
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
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("World", nullptr, TestTab("World"))) {
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
				ImGui::Text("%d burning, %d liquid pixels flowing", TerrainFire::GetCount(), FluidSim::GetActiveCount());
				if (ToolUI::Button("Put out all fire")) {
					TerrainFire::Clear();
				}
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Keys", nullptr, TestTab("Keys"))) {
				s_CurrentTab = "Keys";
				KeysPage();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	g_DebugMan.EndPanel();
}

void Sandbox::OnActivityStarted() {
	// (Called by ActivityMan::StartActivity for every game started, loaded or restarted, before its own start-up runs. A new game used to be
	// told by the activity's address changing, in two places, which a new game allocated where the last one was would not have changed.)
	Controller::SetAIPaused(false);
	Colony::Clear();
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
	s_AutoRunning = false;
	s_AutoWinner = -2;
	s_PendingOrders.clear();
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
	UpdatePace();
	UpdateMoveWatch();
	UpdateIncoming();
	UpdateEffects();
	for (const WaterSpawner& spawner: s_WaterSpawners) {
		FluidSim::Pour(spawner.Position, static_cast<float>(spawner.Radius), "Water");
	}
	// With the AI paused, the sandbox's own passes wait too: they walked defenders home once a second, and auto battle kept dropping waves,
	// all on units held still. (Its wave clocks are held back as well, so the waves don't all come at once after.) Attackers pick their own
	// enemies in their AI (SharedBehaviors.AttackOrderUpdate), which the pause holds as it is.
	const bool aiPaused = Controller::IsAIPaused();
	if (!aiPaused && g_TimerMan.GetSimUpdateCount() % 60 == 0) {
		ReturnDefenders();
	}
	GymUpdate();
	// (No sandbox-side watchdog for units that have stopped: getting unstuck, waiting for fuel before a tall climb, and giving up on a route that
	// can't be had are the AI's own business now, and re-ordering a unit every three seconds only restarted whatever it was in the middle of.)
	if (aiPaused) {
		for (AutoSide& autoSide: s_AutoSides) {
			++autoSide.NextWave;
		}
	} else {
		UpdateAutoBattle();
	}
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
				order += ", falling back";
			} else if (actor->NumberValueExists("AIFlank")) {
				order += ", flanking";
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
