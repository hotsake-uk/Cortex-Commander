// The sandbox window, bar, rings and cursor.

#include "SandboxInternal.h"
#include "ActionMenu.h"
#include "GUISound.h"

namespace SandboxDetail {
	void UpdateFreeCamera() {
		if (!s_FreeCamera || !InGame()) {
			s_FreeCameraStarted = false;
			return;
		}
		if (s_CameraWarmupFrames > 0) {
			--s_CameraWarmupFrames;
			return;
		}
		if (!s_FreeCameraStarted) {
			s_FreeCameraStarted = true;
			s_CameraCenter = g_CameraMan.GetOffset(0) + Vector(static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F, static_cast<float>(g_FrameMan.GetPlayerScreenHeight()) * 0.5F);
		}
		ImGuiIO& io = ImGui::GetIO();
		bool movedByHand = false;
		// Dragging with the right button moves the view, unless the tool in hand makes things for a side or is a Paint tool: then the right button is for the
		// ring of sides or digs, and the middle button (or the keys) moves the view.
		bool rightPans = !(TakesSide(CurrentTool().Kind) || (IsPaintTool(CurrentTool().Kind) && !s_Possessed)) || !Sandbox::CapturesWorldClicks();
		if (!io.WantCaptureMouse && !s_RingOpen && ((rightPans && ImGui::IsMouseDown(ImGuiMouseButton_Right)) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))) {
			s_CameraCenter -= Vector(io.MouseDelta.x, io.MouseDelta.y) * ScenePixelsPerWindowPixel();
			movedByHand = io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F;
		}
		if (!io.WantCaptureKeyboard) {
			float keySpeed = 400.0F * io.DeltaTime * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0F : 1.0F);
			bool left = ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_A);
			bool right = ImGui::IsKeyDown(ImGuiKey_RightArrow) || ImGui::IsKeyDown(ImGuiKey_D);
			bool up = ImGui::IsKeyDown(ImGuiKey_UpArrow) || ImGui::IsKeyDown(ImGuiKey_W);
			bool down = ImGui::IsKeyDown(ImGuiKey_DownArrow) || ImGui::IsKeyDown(ImGuiKey_S);
			s_CameraCenter.m_X += (right ? keySpeed : 0.0F) - (left ? keySpeed : 0.0F);
			s_CameraCenter.m_Y += (down ? keySpeed : 0.0F) - (up ? keySpeed : 0.0F);
			movedByHand = movedByHand || left || right || up || down;
		}
		if (movedByHand) {
			s_FollowTarget = UnitRef();
			s_FollowAction = false;
		}
		if (Actor* followed = GetRef(s_FollowTarget)) {
			s_CameraCenter += g_SceneMan.ShortestDistance(s_CameraCenter, followed->GetPos(), g_SceneMan.SceneWrapsX()) * std::min(1.0F, io.DeltaTime * 8.0F);
		} else if (s_FollowAction && s_ActionSpotValid) {
			s_CameraCenter += g_SceneMan.ShortestDistance(s_CameraCenter, s_ActionSpot, g_SceneMan.SceneWrapsX()) * std::min(1.0F, io.DeltaTime * 2.0F);
		}
		g_SceneMan.WrapPosition(s_CameraCenter);
		g_SceneMan.ForceBounds(s_CameraCenter);
		g_CameraMan.SetScrollTarget(s_CameraCenter, 1.0F, 0);
		// The god view's own camera follows along, so the two don't fight.
		if (GameActivity* game = CurrentGame(); game && (Sandbox::IsGodMode() || s_Commander)) {
			game->SetObservationTarget(s_CameraCenter, Players::PlayerOne);
		}
	}



	std::string RandomSourceName(bool favouritesOnly, int faction) {
		if (favouritesOnly) {
			return "Random favourites";
		}
		if (faction >= 0 && faction < static_cast<int>(s_FactionNames.size())) {
			return "Random " + s_FactionNames[faction];
		}
		return "Random units";
	}

	bool RandomSourceCombo(const char* label, bool& favouritesOnly, int& faction) {
		bool changed = false;
		if (faction >= static_cast<int>(s_FactionNames.size())) {
			faction = -1;
		}
		std::string shown = favouritesOnly ? "Favourites" : faction >= 0 ? s_FactionNames[faction] : "All factions";
		if (ImGui::BeginCombo(label, shown.c_str(), ImGuiComboFlags_HeightLarge)) {
			if (ImGui::Selectable("All factions", !favouritesOnly && faction < 0)) {
				favouritesOnly = false;
				faction = -1;
				changed = true;
			}
			if (ImGui::Selectable("Favourites", favouritesOnly)) {
				favouritesOnly = true;
				faction = -1;
				changed = true;
			}
			ImGui::Separator();
			for (size_t i = 0; i < s_FactionNames.size(); ++i) {
				if (ImGui::Selectable(s_FactionNames[i].c_str(), !favouritesOnly && faction == static_cast<int>(i))) {
					favouritesOnly = false;
					faction = static_cast<int>(i);
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Where random units come from: every faction, only the units marked as favourites (Ctrl+click on a tile; with none marked, every unit), or one faction.");
		return changed;
	}

	std::vector<std::string> PourableNames() {
		std::vector<std::string> names;
		for (int id = 1; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (!material || material->GetIndex() != id) {
				continue;
			}
			const std::string& name = material->GetPresetName();
			// (The powder rule is FluidSim's own, BuildTables: Powder set, or unset and one of the stock powder names. Liquids it says itself.)
			const MaterialBehaviour& behaviour = material->GetBehaviour();
			bool powderByName = name == "Sand" || name == "Snow" || name == "Earth Rubble" || name == "Ashes";
			bool powder = FluidSim::PowdersEnabled() && !FluidSim::IsLiquid(id) && (behaviour.Powder >= 0 ? behaviour.Powder == 1 : powderByName);
			if (FluidSim::IsLiquid(id) || powder) {
				names.push_back(name);
			}
		}
		std::sort(names.begin(), names.end());
		names.erase(std::unique(names.begin(), names.end()), names.end());
		return names;
	}

	ImU32 MaterialMarkColor(const std::string& name, int alpha) {
		const Material* material = g_SceneMan.GetMaterial(name);
		if (!material || material->GetColor().GetIndex() <= 0) {
			return IM_COL32(70, 160, 255, alpha);
		}
		return IM_COL32(std::min(material->GetColor().GetR() + 50, 255), std::min(material->GetColor().GetG() + 50, 255), std::min(material->GetColor().GetB() + 50, 255), alpha);
	}

	std::vector<std::pair<std::string, int>> SpringCounts() {
		std::map<std::string, int> counts;
		for (const WaterSpawner& spring: s_WaterSpawners) {
			++counts[spring.Liquid];
		}
		return std::vector<std::pair<std::string, int>>(counts.begin(), counts.end());
	}

	bool PoursLiquid(Tool kind) {
		switch (kind) {
			case Tool::Water:
			case Tool::Lava:
			case Tool::Acid:
			case Tool::Oil:
			case Tool::Mud:
			case Tool::Tar:
			case Tool::Mercury:
			case Tool::Fuel:
			case Tool::Cryo:
			case Tool::Blood:
			case Tool::PourOther:
			case Tool::WaterSpawner:
			case Tool::BuildTank:
				return true;
			default:
				return PoursPowder(kind);
		}
	}

	bool PoursPowder(Tool kind) {
		return kind == Tool::LooseSand || kind == Tool::LooseSnow || kind == Tool::Gravel || kind == Tool::GlassShards;
	}

	const char* ToolUnavailableReason(Tool kind) {
		if (PoursLiquid(kind) && !FluidSim::IsEnabled()) {
			return "Flowing liquids are off (World > Simulations, or F6 > Water): nothing is poured.";
		}
		if (PoursPowder(kind) && !FluidSim::PowdersEnabled()) {
			return "Loose ground is off (World > Simulations, or F6 > Water): sand, snow, gravel and glass aren't poured.";
		}
		return nullptr;
	}

	const char* ToolTipText(Tool kind) {
		switch (kind) {
			case Tool::Fire:
				return "Sets what burns alight: grass, wood, oil and fuel catch; rock doesn't.";
			case Tool::Water:
				return "Flows, pools and puts out fire. Freezes in snowy weather if that is on.";
			case Tool::Lava:
				return "Slow and heavy. Sets things alight, burns units and turns to stone where it meets water.";
			case Tool::Acid:
				return "Eats through soft ground and hurts units standing in it.";
			case Tool::Oil:
				return "A dark, glossy liquid that floats on water and burns.";
			case Tool::Smoke:
				return "Thick smoke that drifts with the wind and hides units from sight.";
			case Tool::ToxicGas:
				return "Poisonous gas that hurts units in it.";
			case Tool::Mud:
				return "Thick and slow; units wade through it sluggishly. Dries back to earth over time.";
			case Tool::Tar:
				return "Very sticky: units get stuck in it. Burns slowly.";
			case Tool::Mercury:
				return "Heavy and harmful: units float high on it and are hurt by it.";
			case Tool::Fuel:
				return "Runs like water and explodes when it burns.";
			case Tool::Cryo:
				return "Freezes water it touches, chills and frosts units, and boils off over time.";
			case Tool::Blood:
				return "Runs and pools, then soaks away. Turns on \"Spilt blood runs and pools\" (F6 > Water) if it is off.";
			case Tool::PourOther:
				return "Pours the liquid or powder chosen under \"More...\": every one the game has, mods' included (rubble, ash, ...).";
			case Tool::WaterSpawner:
				return "Click to place a spring that keeps pouring, as wide as the brush. What it pours and how fast are set under Springs.";
			case Tool::LooseSand:
				return "Falls and piles into slopes.";
			case Tool::LooseSnow:
				return "Falls and piles, a little sticky; melts to water.";
			case Tool::Gravel:
				return "Falls and piles like sand, heavier.";
			case Tool::GlassShards:
				return "Falls and piles, and cuts units walking through it.";
			case Tool::BuildTank:
				return "An open concrete tank, filled with what the springs pour (Paint > Springs).";
			case Tool::DenseEarth:
				return "The base game's dense earth: darker and tougher to dig than earth.";
			case Tool::GoldEarth:
				return "Earth with flecks of gold in it, as the base game's maps have, for units to dig out.";
			case Tool::Plants:
				return "Drag along the ground to put down rows of the game's own plants, as its maps have them, as far apart as Plant spacing says.";
			case Tool::Cacti:
				return "Drag along the ground to put down rows of the game's own cacti, big and small.";
			case Tool::TerrainOther:
				return "Paints the terrain chosen under \"More terrain...\": the base game's ground (topsoil, bedrock, red and lunar earth, snow, metal, ...).";
			default:
				return nullptr;
		}
	}

	ToolLook LookOf(Tool kind) {
		switch (kind) {
			case Tool::None:
				return {Icon::Eye, IM_COL32(232, 224, 190, 255)};
			case Tool::Command:
				return {Icon::Arrows, IM_COL32(232, 224, 190, 255)};
			case Tool::Follow:
				return {Icon::Target, IM_COL32(232, 224, 190, 255)};
			case Tool::Possess:
				return {Icon::Person, IM_COL32(242, 182, 61, 255)};
			case Tool::Remove:
				return {Icon::Cross, IM_COL32(239, 106, 91, 255)};
			case Tool::RallyPoint:
				return {Icon::Flag, IM_COL32(242, 182, 61, 255)};
			case Tool::Unit:
				return {Icon::Person, IM_COL32(232, 224, 190, 255)};
			case Tool::Brain:
				return {Icon::Jar, IM_COL32(240, 150, 170, 255)};
			case Tool::Item:
				return {Icon::Gun, IM_COL32(200, 205, 215, 255)};
			case Tool::Structure:
				return {Icon::Wall, IM_COL32(170, 170, 165, 255)};
			case Tool::Drop:
				return {Icon::Down, IM_COL32(232, 224, 190, 255)};
			case Tool::PlayCharacter:
				return {Icon::Person, IM_COL32(130, 220, 120, 255)};
			case Tool::Barracks:
				return {Icon::Wall, IM_COL32(242, 182, 61, 255)};
			case Tool::Extractor:
				return {Icon::Wall, IM_COL32(120, 200, 230, 255)};
			case Tool::OrderMove:
				return {Icon::Arrows, IM_COL32(242, 182, 61, 255)};
			case Tool::GymStart:
				return {Icon::Person, IM_COL32(120, 220, 120, 255)};
			case Tool::GymGoal:
				return {Icon::Flag, IM_COL32(242, 182, 61, 255)};
			case Tool::BattleDefendPoint:
				return {Icon::Flag, IM_COL32(120, 200, 220, 255)};
			case Tool::BattleDropLine:
				return {Icon::Down, IM_COL32(120, 200, 220, 255)};
			case Tool::BattleSpawnZone:
				return {Icon::Person, IM_COL32(120, 200, 220, 255)};
			case Tool::BattleModePoint:
				return {Icon::Flag, IM_COL32(242, 182, 61, 255)};
			case Tool::BattleModeBase:
				return {Icon::Wall, IM_COL32(242, 182, 61, 255)};
			case Tool::BattleModeZone:
				return {Icon::Target, IM_COL32(120, 220, 160, 255)};
			case Tool::BattleModeGoal:
				return {Icon::Wall, IM_COL32(120, 220, 160, 255)};
			case Tool::BattleModeFlag:
				return {Icon::Flag, IM_COL32(240, 240, 240, 255)};
			case Tool::Fire:
				return {Icon::Flame, IM_COL32(255, 140, 40, 255)};
			case Tool::Napalm:
				return {Icon::Flame, IM_COL32(255, 90, 30, 255)};
			case Tool::NapalmRain:
				return {Icon::Flame, IM_COL32(255, 60, 30, 255)};
			case Tool::Water:
				return {Icon::Drop, IM_COL32(90, 170, 240, 255)};
			case Tool::Lava:
				return {Icon::Drop, IM_COL32(255, 110, 30, 255)};
			case Tool::Acid:
				return {Icon::Drop, IM_COL32(140, 230, 60, 255)};
			case Tool::Oil:
				return {Icon::Drop, IM_COL32(120, 90, 140, 255)};
			case Tool::Mud:
				return {Icon::Drop, IM_COL32(150, 105, 60, 255)};
			case Tool::Tar:
				return {Icon::Drop, IM_COL32(70, 60, 55, 255)};
			case Tool::Mercury:
				return {Icon::Drop, IM_COL32(200, 205, 215, 255)};
			case Tool::Fuel:
				return {Icon::Drop, IM_COL32(220, 190, 60, 255)};
			case Tool::Cryo:
				return {Icon::Drop, IM_COL32(180, 235, 255, 255)};
			case Tool::Blood:
				return {Icon::Drop, IM_COL32(170, 20, 25, 255)};
			case Tool::PourOther:
				return {Icon::Grains, IM_COL32(200, 180, 150, 255)};
			case Tool::WaterSpawner:
				return {Icon::Down, IM_COL32(90, 170, 240, 255)};
			case Tool::Smoke:
				return {Icon::Cloud, IM_COL32(190, 190, 190, 255)};
			case Tool::ToxicGas:
				return {Icon::Cloud, IM_COL32(150, 220, 80, 255)};
			case Tool::Methane:
				return {Icon::Cloud, IM_COL32(240, 170, 90, 255)};
			case Tool::Steam:
				return {Icon::Cloud, IM_COL32(235, 240, 245, 255)};
			case Tool::LooseSand:
				return {Icon::Grains, IM_COL32(222, 190, 120, 255)};
			case Tool::LooseSnow:
				return {Icon::Grains, IM_COL32(240, 245, 255, 255)};
			case Tool::Gravel:
				return {Icon::Grains, IM_COL32(150, 145, 135, 255)};
			case Tool::GlassShards:
				return {Icon::Grains, IM_COL32(190, 225, 235, 255)};
			case Tool::Sand:
				return {Icon::Grains, IM_COL32(200, 170, 100, 255)};
			case Tool::Boulder:
				return {Icon::Chunk, IM_COL32(150, 140, 130, 255)};
			case Tool::Slab:
				return {Icon::Chunk, IM_COL32(180, 180, 175, 255)};
			case Tool::Earth:
				return {Icon::Chunk, IM_COL32(150, 100, 60, 255)};
			case Tool::Ice:
				return {Icon::Chunk, IM_COL32(170, 220, 250, 255)};
			case Tool::Grass:
				return {Icon::Chunk, IM_COL32(110, 180, 70, 255)};
			case Tool::Wood:
				return {Icon::Chunk, IM_COL32(170, 120, 70, 255)};
			case Tool::Concrete:
				return {Icon::Chunk, IM_COL32(170, 170, 165, 255)};
			case Tool::Stone:
				return {Icon::Chunk, IM_COL32(135, 130, 125, 255)};
			case Tool::DenseEarth:
				return {Icon::Chunk, IM_COL32(105, 70, 45, 255)};
			case Tool::GoldEarth:
				return {Icon::Chunk, IM_COL32(230, 190, 60, 255)};
			case Tool::TerrainOther:
				return {Icon::Chunk, IM_COL32(200, 160, 120, 255)};
			case Tool::Plants:
				return {Icon::Plant, IM_COL32(110, 190, 80, 255)};
			case Tool::Cacti:
				return {Icon::Plant, IM_COL32(150, 190, 90, 255)};
			case Tool::BoulderRain:
				return {Icon::Chunk, IM_COL32(150, 140, 130, 255)};
			case Tool::Dig:
				return {Icon::Pick, IM_COL32(232, 224, 190, 255)};
			case Tool::Grenade:
				return {Icon::Bomb, IM_COL32(120, 150, 90, 255)};
			case Tool::BigBomb:
				return {Icon::Bomb, IM_COL32(90, 90, 100, 255)};
			case Tool::Demolition:
				return {Icon::Bomb, IM_COL32(239, 106, 91, 255)};
			case Tool::BunkerBuster:
				return {Icon::Bomb, IM_COL32(242, 182, 61, 255)};
			case Tool::CarpetBomb:
				return {Icon::Bomb, IM_COL32(150, 150, 160, 255)};
			case Tool::Artillery:
				return {Icon::Bomb, IM_COL32(200, 160, 90, 255)};
			case Tool::Meteor:
				return {Icon::Rocket, IM_COL32(255, 140, 40, 255)};
			case Tool::RocketStrike:
				return {Icon::Rocket, IM_COL32(220, 220, 225, 255)};
			case Tool::RocketBarrage:
				return {Icon::Rocket, IM_COL32(239, 106, 91, 255)};
			case Tool::CrashRocket:
				return {Icon::Rocket, IM_COL32(242, 182, 61, 255)};
			case Tool::CrashDropship:
				return {Icon::Rocket, IM_COL32(120, 200, 230, 255)};
			case Tool::Lightning:
				return {Icon::Bolt, IM_COL32(255, 240, 120, 255)};
			case Tool::OrbitalBeam:
				return {Icon::Bolt, IM_COL32(120, 220, 255, 255)};
			case Tool::Effect:
				return {Icon::Star, IM_COL32(255, 220, 120, 255)};
			case Tool::BuildBeam:
				return {Icon::Wall, IM_COL32(170, 170, 165, 255)};
			case Tool::BuildPillar:
				return {Icon::Wall, IM_COL32(170, 170, 165, 255)};
			case Tool::BuildRoom:
				return {Icon::Wall, IM_COL32(170, 170, 165, 255)};
			case Tool::BuildTower:
				return {Icon::Wall, IM_COL32(200, 200, 195, 255)};
			case Tool::BuildIsland:
				return {Icon::Chunk, IM_COL32(150, 100, 60, 255)};
			case Tool::BuildTank:
				return {Icon::Drop, IM_COL32(90, 170, 240, 255)};
			case Tool::BuildBridge:
				return {Icon::Wall, IM_COL32(170, 120, 70, 255)};
			default:
				return {Icon::Star, IM_COL32(232, 224, 190, 255)};
		}
	}

	/// Draws one of the tool pictures, each of its pixels a square of the size given.
	void DrawIcon(ImDrawList* drawList, Icon icon, ImVec2 at, float pixel, ImU32 color) {
		const char* art = c_IconArt[static_cast<int>(icon)];
		ImU32 highlight = IM_COL32(255, 255, 255, (color >> IM_COL32_A_SHIFT) & 0xFF);
		for (int y = 0; y < 12; ++y) {
			for (int x = 0; x < 12; ++x) {
				char dot = art[y * 12 + x];
				if (dot != '.') {
					drawList->AddRectFilled(ImVec2(at.x + static_cast<float>(x) * pixel, at.y + static_cast<float>(y) * pixel), ImVec2(at.x + static_cast<float>(x + 1) * pixel, at.y + static_cast<float>(y + 1) * pixel), dot == '+' ? highlight : color);
				}
			}
		}
	}


	/// Notes that a tool was picked on the tab showing, for the bar to bring back with the tab.
	void TookTool(int toolIndex) {
		s_ToolIndex = toolIndex;
		if (!s_CurrentTab.empty()) {
			s_LastToolOfTab[s_CurrentTab] = toolIndex;
		}
	}

	/// Says whether a tab of the sandbox window is to be brought to the front this frame: because the bar asked for it, or a test run did (they can't click, so
	/// they name the one the window is to open on: CCCP_TEST_TAB=Spawn).
	ImGuiTabItemFlags TestTab(const char* name) {
		static const char* testWanted = std::getenv("CCCP_TEST_TAB");
		static int testFrames = 0;
		if (testWanted && std::string(testWanted) == name && testFrames < 3) {
			++testFrames;
			return ImGuiTabItemFlags_SetSelected;
		}
		if (!s_WantedTab.empty() && s_WantedTab == name) {
			s_WantedTab.clear();
			return ImGuiTabItemFlags_SetSelected;
		}
		return ImGuiTabItemFlags_None;
	}


	std::vector<const char*> VisibleTabs() {
		std::vector<const char*> tabs;
		if (Sandbox::IsGodMode()) {
			tabs.push_back("You");
		}
		tabs.push_back("Spawn");
		if (c_ShowColonyTab) {
			tabs.push_back("Colony");
		}
		tabs.push_back("Build");
		tabs.push_back("Orders");
		tabs.push_back("Battle");
		if (Sandbox::IsGodMode()) {
			tabs.push_back("Gym");
		}
		for (const char* name: {"Paint", "Boom", "Effects", "World", "Keys"}) {
			tabs.push_back(name);
		}
		return tabs;
	}

	bool DrawTabRows() {
		std::vector<const char*> tabs = VisibleTabs();
		// (Asked for from the bar, or by a test run: TestTab says so for the tab named.)
		for (const char* name: tabs) {
			if (TestTab(name) & ImGuiTabItemFlags_SetSelected) {
				s_CurrentTab = name;
			}
		}
		if (std::none_of(tabs.begin(), tabs.end(), [](const char* name) { return s_CurrentTab == name; })) {
			s_CurrentTab = tabs.front();
		}
		// Two rows, the first the longer by one when the count is odd; more when the panel is too narrow for the names to fit. Each button takes an equal
		// share of the width, so the rows are laid out here, not left to the wrapping of controls that don't fit.
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float room = ImGui::GetContentRegionAvail().x;
		float widest = 0.0F;
		for (const char* name: tabs) {
			widest = std::max(widest, ImGui::CalcTextSize(name).x + ImGui::GetStyle().FramePadding.x * 2.0F);
		}
		size_t fits = std::max<size_t>(1, static_cast<size_t>((room + spacing) / (widest + spacing)));
		size_t perRow = std::min((tabs.size() + 1) / 2, fits);
		for (size_t row = 0; row * perRow < tabs.size(); ++row) {
			size_t first = row * perRow;
			size_t last = std::min(tabs.size(), first + perRow);
			float width = std::floor((room - spacing * static_cast<float>(perRow - 1)) / static_cast<float>(perRow));
			for (size_t i = first; i < last; ++i) {
				if (i > first) {
					ImGui::SameLine();
				}
				bool showing = s_CurrentTab == tabs[i];
				if (showing) {
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
				}
				std::string label = std::string(tabs[i]) + "##tab";
				if (ToolUI::Button(label.c_str(), ImVec2(width, 0.0F)) && !showing) {
					s_CurrentTab = tabs[i];
				}
				if (showing) {
					ImGui::PopStyleColor(2);
				}
			}
		}
		ImGui::Separator();
		return true;
	}

	bool SandboxTab(const char* name) {
		if (s_CurrentTab != name) {
			return false;
		}
		ImGui::PushID(name);
		return true;
	}

	void EndSandboxTab() {
		ImGui::PopID();
	}


	int FindPin(Tool kind, const std::string& presetName) {
		for (size_t i = 0; i < s_Pins.size(); ++i) {
			if (s_Pins[i].Kind == kind && s_Pins[i].PresetName == presetName) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void TogglePin(Tool kind, const std::string& presetName) {
		if (int at = FindPin(kind, presetName); at >= 0) {
			s_Pins.erase(s_Pins.begin() + at);
		} else if (s_Pins.size() < 24) {
			s_Pins.push_back({kind, presetName});
		}
		SavePinsFile();
	}

	void SavePinsFile() {
		if (std::ofstream file(c_PinsFile, std::ios::trunc); file) {
			file << Sandbox::GetPins() << '\n';
		}
	}


	int FindFavourite(Tool kind, const std::string& presetName) {
		for (size_t i = 0; i < s_Favourites.size(); ++i) {
			if (s_Favourites[i].Kind == kind && s_Favourites[i].PresetName == presetName) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}


	void SaveFavouritesFile() {
		if (std::ofstream file(c_FavouritesFile, std::ios::trunc); file) {
			file << Sandbox::GetFavourites() << '\n';
		}
	}

	void LoadFavouritesFile() {
		static bool loaded = false;
		if (loaded) {
			return;
		}
		loaded = true;
		if (std::ifstream file(c_FavouritesFile); file) {
			std::string line;
			std::getline(file, line);
			if (!line.empty()) {
				Sandbox::SetFavourites(line);
			}
		}
	}

	void ToggleFavourite(Tool kind, const std::string& presetName) {
		if (int at = FindFavourite(kind, presetName); at >= 0) {
			s_Favourites.erase(s_Favourites.begin() + at);
		} else {
			s_Favourites.push_back({kind, presetName});
		}
		SaveFavouritesFile();
	}

	/// A small gold star in the top left corner of a tile that is a favourite.
	void DrawFavouriteMark(ImDrawList* drawList, ImVec2 from) {
		float pixel = ToolUI::Pixel();
		ImVec2 centre(from.x + pixel * 6.0F, from.y + pixel * 6.0F);
		float outer = pixel * 5.0F;
		float inner = pixel * 2.0F;
		ImVec2 points[10];
		for (int i = 0; i < 10; ++i) {
			float angle = -1.5708F + 0.6283F * static_cast<float>(i);
			float reach = (i % 2 == 0) ? outer : inner;
			points[i] = ImVec2(centre.x + std::cos(angle) * reach, centre.y + std::sin(angle) * reach);
		}
		drawList->AddConcavePolyFilled(points, 10, IM_COL32(242, 182, 61, 255));
	}

	/// A small gold corner on a tile that is pinned to the bar.
	void DrawPinMark(ImDrawList* drawList, ImVec2 from, ImVec2 to) {
		float size = ToolUI::Pixel() * 5.0F;
		drawList->AddTriangleFilled(ImVec2(to.x - size, from.y), ImVec2(to.x, from.y), ImVec2(to.x, from.y + size), IM_COL32(242, 182, 61, 255));
	}

	/// The tools to pick from, as a row of tiles: each its picture with its name under it, the one in hand lit up.
	void ToolButtons(std::initializer_list<Tool> tools) {
		const ImGuiStyle& style = ImGui::GetStyle();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		float pixel = ToolUI::Pixel() * 2.0F;
		const int perRow = 4;
		float gap = style.ItemSpacing.x * 0.5F;
		float width = std::floor((ImGui::GetContentRegionAvail().x - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow));
		float pad = pixel * 2.0F;
		float height = pad + pixel * 12.0F + ImGui::GetTextLineHeight() * 2.0F + pad;
		int column = 0;
		for (Tool kind: tools) {
			int index = ToolIndex(kind);
			if (column++ % perRow != 0) {
				ImGui::SameLine(0.0F, gap);
			}
			ImGui::PushID(index);
			ImVec2 at = ImGui::GetCursorScreenPos();
			// A tool its simulation is off for does nothing: shown greyed, with the reason (it can still be taken, to work once it's on).
			const char* unavailable = ToolUnavailableReason(kind);
			if (ImGui::InvisibleButton("##tool", ImVec2(width, height))) {
				TookTool(index);
			}
			if (const char* tip = ToolTipText(kind); tip || unavailable) {
				ImGui::SetItemTooltip("%s%s%s", tip ? tip : "", tip && unavailable ? "\n\n" : "", unavailable ? unavailable : "");
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && Sandbox::IsGodMode()) {
				TogglePin(kind, "");
			}
			bool hovered = ImGui::IsItemHovered();
			bool selected = s_ToolIndex == index;
			ImVec2 to(at.x + width, at.y + height);
			drawList->AddRectFilled(at, to, ImGui::GetColorU32(selected ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
			drawList->AddRect(at, to, ImGui::GetColorU32(selected ? ImGuiCol_SliderGrab : ImGuiCol_Border), 0.0F, 0, selected ? ToolUI::Pixel() * 2.0F : ToolUI::Pixel());
			ToolLook look = LookOf(kind);
			if (unavailable) {
				look.Color = (look.Color & 0x00FFFFFF) | (static_cast<ImU32>(90) << IM_COL32_A_SHIFT);
			}
			DrawIcon(drawList, look.Art, ImVec2(std::floor(at.x + (width - pixel * 12.0F) * 0.5F), at.y + pad), pixel, look.Color);
			if (FindPin(kind, "") >= 0) {
				DrawPinMark(drawList, at, to);
			}
			const char* name = c_Tools[index].Name;
			float wrap = width - pad;
			ImVec2 nameSize = ImGui::CalcTextSize(name, nullptr, false, wrap);
			ImGui::PushClipRect(at, to, true);
			drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(std::floor(at.x + std::max((width - nameSize.x) * 0.5F, pad * 0.5F)), at.y + pad + pixel * 12.0F + ToolUI::Pixel()), ImGui::GetColorU32(selected ? ImGuiCol_SliderGrab : (unavailable ? ImGuiCol_TextDisabled : ImGuiCol_Text)), name, nullptr, wrap);
			ImGui::PopClipRect();
			ImGui::PopID();
		}
	}

	void UndoButton() {
		ImGui::BeginDisabled(s_PaintUndo.empty());
		if (ToolUI::Button("Undo")) {
			QueueSimChange(Tool::UndoTerrain);
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Takes back the last brush stroke or the last thing placed, whichever came last (Ctrl+Z): painting and placing share one history. Placed units, craft, items, doors and buildings are taken away (not your character, the unit you are in, or a thing a unit has picked up), and the ground they or a stroke changed is put back. The last 20 can be undone, one at a time, up to about 8 million pixels in all: the oldest go first, and a stroke held for more than a few seconds is undone in parts.");
	}

	void SideChooser() {
		for (int side = 0; side < c_Sides; ++side) {
			if (side > 0) {
				ImGui::SameLine();
			}
			ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
			ToolUI::RadioButton(c_SideNames[side], &s_Team, side);
			ImGui::PopStyleColor();
		}
	}

	void PresetList(Tool kind, const char* group, float rows) {
		const std::vector<Preset>& list = ListFor(kind);
		int& choice = ChoiceFor(kind);
		char* filter = FilterFor(kind, false);
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::InputTextWithHint("##filter", "Search...", filter, 64);
		if (ImGui::BeginListBox("##presets", ImVec2(-1.0F, ImGui::GetTextLineHeightWithSpacing() * rows))) {
			for (int i = 0; i < static_cast<int>(list.size()); ++i) {
				if (!ContainsIgnoringCase(list[i].Label, filter) || (group && list[i].Group != group)) {
					continue;
				}
				if (ImGui::Selectable(list[i].Label.c_str(), i == choice)) {
					choice = i;
				}
				if (i == choice && ImGui::IsWindowAppearing()) {
					ImGui::SetScrollHereY();
				}
			}
			ImGui::EndListBox();
		}
	}

	void LoadoutChooser(const char* label) {
		const char* current = s_Loadout == 0 ? "Faction default" : (s_Loadout == 1 ? "Unarmed" : (s_Loadout - 2 < static_cast<int>(s_Weapons.size()) ? s_Weapons[s_Loadout - 2]->Label.c_str() : "?"));
		if (ImGui::BeginCombo(label, current)) {
			if (ImGui::Selectable("Faction default", s_Loadout == 0)) {
				s_Loadout = 0;
			}
			if (ImGui::Selectable("Unarmed", s_Loadout == 1)) {
				s_Loadout = 1;
			}
			for (int i = 0; i < static_cast<int>(s_Weapons.size()); ++i) {
				if (ImGui::Selectable(s_Weapons[i]->Label.c_str(), s_Loadout == i + 2)) {
					s_Loadout = i + 2;
				}
			}
			ImGui::EndCombo();
		}
	}



	/// Frees every picture's texture, for when the sandbox is left: they're made again as they're next needed.
	void ForgetPictures() {
		for (std::map<std::string, PiecePicture>* pictures: {&s_PresetPictures, &s_FilePictures}) {
			for (const auto& [key, picture]: *pictures) {
				if (picture.Texture != 0) {
					GLuint texture = picture.Texture;
					glDeleteTextures(1, &texture);
				}
			}
			pictures->clear();
		}
	}

	/// The colour the game shows for a palette index, as 8-bit RGB: the same conversion the renderer uploads the palette with (Allegro palettes are 6 bits a channel).
	void PaletteColor(int index, unsigned char* rgb) {
		rgb[0] = static_cast<unsigned char>(getr8(index));
		rgb[1] = static_cast<unsigned char>(getg8(index));
		rgb[2] = static_cast<unsigned char>(getb8(index));
	}

	/// Gets the picture of a bunker piece, making it the first time it is asked for. A piece with no art of its own gets an empty picture.
	const PiecePicture& PictureOf(const Preset& preset) {
		std::map<std::string, PiecePicture>& pictures = s_PresetPictures;
		if (preset.PictureKey.empty()) {
			preset.PictureKey = preset.ClassName + "/" + preset.Module + "/" + preset.PresetName;
		}
		const std::string& key = preset.PictureKey;
		if (auto found = pictures.find(key); found != pictures.end()) {
			return found->second;
		}
		PiecePicture& picture = pictures[key];
		const Entity* entity = g_PresetMan.GetEntityPreset(preset.ClassName, preset.PresetName, preset.ModuleID);
		std::vector<BITMAP*> layers;
		std::unique_ptr<BITMAP, void (*)(BITMAP*)> portrait(nullptr, destroy_bitmap);
		int cropX = 0, cropY = 0, cropWidth = 0, cropHeight = 0; //!< If set, the part of the one layer that is the picture.
		if (const TerrainObject* terrainObject = dynamic_cast<const TerrainObject*>(entity)) {
			layers = {terrainObject->GetBGColorBitmap(), terrainObject->GetFGColorBitmap()};
			picture.OffsetX = terrainObject->GetBitmapOffset().m_X;
			picture.OffsetY = terrainObject->GetBitmapOffset().m_Y;
		} else if (const Actor* actorPreset = dynamic_cast<const Actor*>(entity); actorPreset && !dynamic_cast<const ADoor*>(entity)) {
			// A unit is many parts: a copy of it is drawn whole, the way the game's build menu shows the thing in hand, and the picture cut to fit.
			// The copy is never updated: an update during drawing would play its sounds, spawn particles and register lights at the picture's spot in the scene and use the sim's random numbers. Its parts are just put in place, as when a unit is added to the world.
			const int room = 160;
			portrait.reset(create_bitmap_ex(8, room, room));
			clear_to_color(portrait.get(), ColorKeys::g_MaskColor);
			if (Actor* copy = dynamic_cast<Actor*>(actorPreset->Clone())) {
				copy->SetPos(Vector(static_cast<float>(room / 2), static_cast<float>(room / 2)));
				copy->SetTeam(0);
				copy->CorrectAttachableAndWoundPositionsAndRotations();
				copy->Draw(portrait.get(), Vector(), g_DrawColor, true);
				delete copy;
			}
			int left = room, top = room, right = -1, bottom = -1;
			for (int y = 0; y < room; ++y) {
				for (int x = 0; x < room; ++x) {
					if (portrait->line[y][x] != ColorKeys::g_MaskColor) {
						left = std::min(left, x);
						right = std::max(right, x);
						top = std::min(top, y);
						bottom = std::max(bottom, y);
					}
				}
			}
			if (right >= left) {
				cropX = left;
				cropY = top;
				cropWidth = right - left + 1;
				cropHeight = bottom - top + 1;
				layers = {portrait.get()};
			}
		} else if (const MOSprite* sprite = dynamic_cast<const MOSprite*>(entity)) {
			layers = {sprite->GetGraphicalIcon()};
			picture.OffsetX = sprite->GetSpriteOffset().m_X;
			picture.OffsetY = sprite->GetSpriteOffset().m_Y;
		}
		for (const BITMAP* layer: layers) {
			if (layer && bitmap_color_depth(const_cast<BITMAP*>(layer)) == 8) {
				picture.Width = std::max(picture.Width, cropWidth > 0 ? cropWidth : layer->w);
				picture.Height = std::max(picture.Height, cropHeight > 0 ? cropHeight : layer->h);
			}
		}
		if (picture.Width <= 0 || picture.Height <= 0 || picture.Width > 4096 || picture.Height > 4096) {
			picture.Width = picture.Height = 0;
			return picture;
		}
		std::vector<unsigned char> pixels(static_cast<size_t>(picture.Width) * picture.Height * 4, 0);
		for (const BITMAP* layer: layers) {
			if (!layer || bitmap_color_depth(const_cast<BITMAP*>(layer)) != 8) {
				continue;
			}
			for (int y = 0; y < std::min(layer->h - cropY, picture.Height); ++y) {
				for (int x = 0; x < std::min(layer->w - cropX, picture.Width); ++x) {
					int index = layer->line[y + cropY][x + cropX];
					if (index == ColorKeys::g_MaskColor) {
						continue;
					}
					unsigned char* pixel = &pixels[(static_cast<size_t>(y) * picture.Width + x) * 4];
					PaletteColor(index, pixel);
					pixel[3] = 255;
				}
			}
		}
		GLint boundBefore = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundBefore);
		glGenTextures(1, &picture.Texture);
		glBindTexture(GL_TEXTURE_2D, picture.Texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, picture.Width, picture.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundBefore));
		return picture;
	}


	/// A combo of the distinct values of one field over the list, with "All" first. @return Whether the choice changed.
	bool ChoiceCombo(const char* label, std::string& chosen, const std::vector<std::string>& values) {
		std::string items = "All";
		items.push_back(0);
		int current = 0;
		for (size_t i = 0; i < values.size(); ++i) {
			items += values[i];
			items.push_back(0);
			if (values[i] == chosen) {
				current = static_cast<int>(i) + 1;
			}
		}
		items.push_back(0);
		if (ImGui::Combo(label, &current, items.c_str())) {
			chosen = current == 0 ? "" : values[current - 1];
			return true;
		}
		return false;
	}

	void PictureGrid(Tool kind, const char* group, std::string* pickInto) {
		LoadFavouritesFile();
		const std::vector<Preset>& list = ListFor(kind);
		int& choice = ChoiceFor(kind);
		// A picker keeps its search and filters apart from the tool's, under the character's tool.
		Tool filterKey = pickInto ? Tool::PlayCharacter : kind;
		char* filter = FilterFor(filterKey, true);
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::InputTextWithHint("##filter", "Search...", filter, 64);
		// Narrowing the list: by subcategory (not for structures, whose own Kind combo does that), by mod, and whether mods are listed at all.
		{
			std::vector<std::string> kinds;
			std::vector<std::string> mods;
			for (const Preset& preset: list) {
				if (group && preset.Group != group) {
					continue;
				}
				if (!preset.Kind.empty() && std::find(kinds.begin(), kinds.end(), preset.Kind) == kinds.end()) {
					kinds.push_back(preset.Kind);
				}
				if ((s_ShowModded || !preset.Modded) && std::find(mods.begin(), mods.end(), preset.Module) == mods.end()) {
					mods.push_back(preset.Module);
				}
			}
			std::sort(kinds.begin(), kinds.end());
			std::sort(mods.begin(), mods.end());
			float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0F) / 3.0F;
			ToolUI::Checkbox("Favourites", &s_FavouritesOnly);
			ImGui::SetItemTooltip("Only the things marked as favourites (Ctrl+click on a tile marks one, and again unmarks it). Favourites are yours, kept whatever game is played.");
			if (s_FavouritesOnly && !s_Favourites.empty()) {
				ImGui::SameLine();
				if (ToolUI::SmallButton("Clear")) {
					s_Favourites.clear();
					SaveFavouritesFile();
				}
				ImGui::SetItemTooltip("Unmarks every favourite.");
			}
			ImGui::SameLine();
			ToolUI::Checkbox("Show modded", &s_ShowModded);
			ImGui::SetItemTooltip("Whether things from mods are listed, as well as the game's own.");
			if (kind != Tool::Structure) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(third);
				ChoiceCombo("##kind", s_KindFilter[filterKey], kinds);
				ImGui::SetItemTooltip("The kind of thing listed.");
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(third);
			ChoiceCombo("##mod", s_ModFilter[filterKey], mods);
			ImGui::SetItemTooltip("Only things from this module (faction or mod).");
		}
		const ImGuiStyle& style = ImGui::GetStyle();
		float cell = ImGui::GetFontSize() * 6.0F;
		float labelHeight = ImGui::GetTextLineHeight() * 2.0F;
		// A picker sits among other settings, so it keeps to a few rows rather than filling the rest of the window.
		float height = pickInto ? (cell + labelHeight + style.ItemSpacing.y) * 2.6F : std::max(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 6.5F, cell * 2.5F);
		ImGui::BeginChild("##pictures", ImVec2(-1.0F, height), ImGuiChildFlags_Borders);
		int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / (cell + style.ItemSpacing.x)));
		int shown = 0;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (int i = 0; i < static_cast<int>(list.size()); ++i) {
			const Preset& preset = list[i];
			if (!ContainsIgnoringCase(preset.Label, filter) || (group && preset.Group != group)) {
				continue;
			}
			if ((!s_ShowModded && preset.Modded) || (!s_KindFilter[filterKey].empty() && preset.Kind != s_KindFilter[filterKey]) || (!s_ModFilter[filterKey].empty() && preset.Module != s_ModFilter[filterKey])) {
				continue;
			}
			if (s_FavouritesOnly && FindFavourite(kind, preset.PresetName) < 0) {
				continue;
			}
			if (s_JetpackOnly && (kind == Tool::Unit || kind == Tool::Drop) && !pickInto && !preset.Jetpack) {
				continue;
			}
			if (shown++ % columns != 0) {
				ImGui::SameLine();
			}
			ImGui::PushID(i);
			ImVec2 at = ImGui::GetCursorScreenPos();
			ImVec2 size(cell, cell + labelHeight);
			bool picked = ImGui::InvisibleButton("##piece", size);
			bool hovered = ImGui::IsItemHovered();
			// Only the ones on screen have their pictures made.
			if (ImGui::IsItemVisible()) {
				bool selected = pickInto ? preset.PresetName == *pickInto : i == choice;
				// In the colours of the game's own menu skin: navy cells, the picked one lit with a gold edge.
				drawList->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), selected ? ToolTheme::Panel : hovered ? ToolTheme::WellHover : ToolTheme::Well);
				drawList->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), selected ? ToolTheme::Gold : ToolTheme::Edge, 0.0F, 0, selected ? 2.0F : 1.0F);
				const PiecePicture& picture = PictureOf(preset);
				if (picture.Width > 0) {
					// As big as fits, by whole pixels when it can be so the art stays crisp.
					float room = cell - 8.0F;
					float fit = std::min(room / static_cast<float>(picture.Width), room / static_cast<float>(picture.Height));
					if (fit >= 1.0F) {
						fit = std::floor(fit);
					}
					fit = std::min(fit, 3.0F);
					ImVec2 pictureSize(static_cast<float>(picture.Width) * fit, static_cast<float>(picture.Height) * fit);
					ImVec2 corner(std::floor(at.x + (cell - pictureSize.x) * 0.5F), std::floor(at.y + (cell - pictureSize.y) * 0.5F));
					drawList->AddImage(static_cast<ImTextureID>(picture.Texture), corner, ImVec2(corner.x + pictureSize.x, corner.y + pictureSize.y));
				}
				ImGui::PushClipRect(ImVec2(at.x + 2.0F, at.y + cell), ImVec2(at.x + size.x - 2.0F, at.y + size.y), true);
				ImVec2 nameSize = ImGui::CalcTextSize(preset.PresetName.c_str(), nullptr, false, cell - 4.0F);
				drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + std::max((cell - nameSize.x) * 0.5F, 2.0F), at.y + cell), ToolTheme::Text, preset.PresetName.c_str(), nullptr, cell - 4.0F);
				ImGui::PopClipRect();
			}
			if (hovered) {
				std::string size = preset.Width > 0 ? "\n" + std::to_string(preset.Width) + " x " + std::to_string(preset.Height) + " pixels" : "";
				if (kind == Tool::Unit || kind == Tool::Drop) {
					size = preset.JetLift < 0.0F ? "\nJetpack: flies without limit" : preset.JetLift <= 0.0F ? "\nNo jetpack, or one too weak to lift it" : "\nJetpack lifts it about " + std::to_string(static_cast<int>(std::round(preset.JetLift))) + " m" + (preset.Jetpack ? "" : " (too little to fly)");
				}
				ImGui::SetTooltip("%s\n%s%s%s\nCtrl+click: a favourite, or not", preset.PresetName.c_str(), preset.Module.c_str(), size.c_str(), Sandbox::IsGodMode() && !pickInto ? "\nRight click: keep it on the bar, or take it off" : "");
			}
			if (picked && ImGui::GetIO().KeyCtrl) {
				ToggleFavourite(kind, preset.PresetName);
			} else if (picked && pickInto) {
				*pickInto = preset.PresetName;
			} else if (picked) {
				choice = i;
				TookTool(ToolIndex(kind));
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && Sandbox::IsGodMode() && !pickInto) {
				TogglePin(kind, preset.PresetName);
			}
			if (ImGui::IsItemVisible() && FindPin(kind, preset.PresetName) >= 0) {
				DrawPinMark(drawList, at, ImVec2(at.x + size.x, at.y + size.y));
			}
			if (ImGui::IsItemVisible() && FindFavourite(kind, preset.PresetName) >= 0) {
				DrawFavouriteMark(drawList, at);
			}
			ImGui::PopID();
		}
		if (shown == 0) {
			ImGui::TextDisabled("Nothing of that kind matches.");
		}
		ImGui::EndChild();
	}


	/// A choice of formation for moves (RC-5), each with what it does.
	void FormationCombo(const char* id) {
		int current = std::clamp(static_cast<int>(s_Formation), 0, static_cast<int>(Formation::Count) - 1);
		if (ImGui::BeginCombo(id, c_FormationNames[current])) {
			for (int i = 0; i < static_cast<int>(Formation::Count); ++i) {
				if (ImGui::Selectable(c_FormationNames[i], i == current)) {
					s_Formation = static_cast<Formation>(i);
				}
				ImGui::SetItemTooltip("%s", c_FormationTips[i]);
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("How units sent somewhere together stand there: %s\nAlt+drag with a move or attack-move faces them the way dragged; column and wedge then line up back from the front.", c_FormationTips[current]);
	}

	/// The "no route" marker under the pointer (RC-7), if any.
	const NoRoute* NoRouteAt(const ImVec2& mouse) {
		for (const NoRoute& marker: s_NoRoutes) {
			ImVec2 at = ToScreen(marker.Destination);
			if ((mouse.x - at.x) * (mouse.x - at.x) + (mouse.y - at.y) * (mouse.y - at.y) <= 11.0F * 11.0F) {
				return &marker;
			}
		}
		return nullptr;
	}

	/// What the sandbox shows of orders as they play out (RC-7): a mark over each unit for its order (as the settings say, the selected
	/// units or all) with a faint line to where it is going or what it is after for the selected ones; a "no route" marker where units
	/// sent couldn't get to (a click on it sends them again); and pings where units of the selection's side come under fire.
	void DrawOrderFeedback() {
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const ImGuiIO& io = ImGui::GetIO();
		float scale = ScenePixelsPerWindowPixel();
		GameViewRect view = g_WindowMan.GetGameViewRect();
		auto inView = [&view](const ImVec2& at, float margin) { return at.x >= view.x - margin && at.y >= view.y - margin && at.x <= view.x + view.w + margin && at.y <= view.y + view.h + margin; };
		auto fade = [](ImU32 color, int alpha) { return (color & 0x00FFFFFF) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << IM_COL32_A_SHIFT); };
		auto selected = [](const Actor* unit) { return std::any_of(s_Selected.begin(), s_Selected.end(), [unit](const UnitRef& ref) { return RefersTo(ref, unit); }); };

		// The order marks.
		if (int which = g_SettingsMan.SandboxOrderGlyphs(); which > 0) {
			std::unordered_map<long, const Actor*> byID;
			for (const Actor* actor: SandboxAccess::Actors()) {
				byID[static_cast<long>(actor->GetUniqueID())] = actor;
			}
			for (Actor* unit: SandboxAccess::Actors()) {
				if (!IsCombatant(unit) || unit->IsPlayerControlled() || (s_Commander && unit->GetTeam() != s_CommanderTeam)) {
					continue;
				}
				bool isSelected = selected(unit);
				if (which == 1 && !isSelected) {
					continue;
				}
				ImVec2 at = ToScreen(unit->GetPos());
				if (!inView(at, 40.0F)) {
					continue;
				}
				// What it is doing, as the sandbox gave it: its plan's patrol, an attack, a post, a guard, a move or an attack-move, or a hold.
				CommandMode kind = CommandMode::Move;
				bool shown = true;
				bool hold = false;
				bool hasGoal = false;
				Vector goal;
				auto plan = s_Plans.find(unit->GetUniqueID());
				const Actor* leader = FollowedBy(unit);
				if (plan != s_Plans.end() && !plan->second.Route.empty()) {
					kind = CommandMode::Patrol;
				} else if (unit->GetOrderTargetID() != 0 || unit->GetOrderAttack()) {
					kind = CommandMode::Attack;
					if (auto target = byID.find(unit->GetOrderTargetID()); target != byID.end()) {
						goal = target->second->GetPos();
						hasGoal = true;
					}
				} else if (auto guard = s_GuardPosts.find(unit->GetUniqueID()); guard != s_GuardPosts.end()) {
					// Guarding a thing (RC-10): a guard, with its line to the thing.
					kind = CommandMode::Guard;
					goal = guard->second.Place;
					hasGoal = true;
				} else if (unit->GetOrderHasPost()) {
					kind = CommandMode::DefendAt;
					goal = unit->GetOrderPost();
					hasGoal = g_SceneMan.ShortestDistance(unit->GetPos(), goal, g_SceneMan.SceneWrapsX()).GetMagnitude() > 30.0F;
				} else if (leader && leader->GetTeam() == unit->GetTeam()) {
					kind = CommandMode::Guard;
					goal = leader->GetPos();
					hasGoal = true;
				} else if (unit->GetAIMode() == Actor::AIMODE_GOTO) {
					kind = unit->GetMovementRule() == Actor::MOVE_ENGAGE ? CommandMode::AttackMove : CommandMode::Move;
					if (unit->GetWaypointsSize() > 0) {
						goal = unit->GetLastAIWaypoint();
						hasGoal = true;
					}
				} else if (unit->GetOrderHold()) {
					hold = true;
				} else {
					shown = false;
				}
				if (!shown) {
					continue;
				}
				ImU32 color = hold ? IM_COL32(200, 200, 190, 255) : c_CommandModeColors[static_cast<int>(kind)];
				if (hasGoal && isSelected) {
					drawList->AddLine(at, ToScreen(goal), fade(color, 90), 1.0F);
				}
				// Over the head, above the rule tag (RC-1) if it has one.
				ImVec2 mark(at.x, std::floor(at.y - std::max(unit->GetRadius() / scale, 8.0F) - ImGui::GetTextLineHeight() - 12.0F));
				float r = 5.0F;
				drawList->AddCircleFilled(mark, r + 3.0F, IM_COL32(0, 0, 0, 150));
				float toward = hasGoal && g_SceneMan.ShortestDistance(unit->GetPos(), goal, g_SceneMan.SceneWrapsX()).m_X < 0.0F ? -1.0F : 1.0F;
				if (hold) {
					drawList->AddRectFilled(ImVec2(mark.x - r * 0.7F, mark.y - r * 0.7F), ImVec2(mark.x + r * 0.7F, mark.y + r * 0.7F), color);
				} else if (kind == CommandMode::Move || kind == CommandMode::AttackMove) {
					drawList->AddTriangleFilled(ImVec2(mark.x + toward * r, mark.y), ImVec2(mark.x - toward * r, mark.y - r), ImVec2(mark.x - toward * r, mark.y + r), color);
					if (kind == CommandMode::AttackMove) {
						drawList->AddLine(ImVec2(mark.x - r, mark.y - r - 3.0F), ImVec2(mark.x + r, mark.y - r - 3.0F), color, 1.5F);
					}
				} else if (kind == CommandMode::Attack) {
					drawList->AddCircle(mark, r, color, 0, 1.5F);
					drawList->AddLine(ImVec2(mark.x - r - 2.0F, mark.y), ImVec2(mark.x + r + 2.0F, mark.y), color, 1.5F);
					drawList->AddLine(ImVec2(mark.x, mark.y - r - 2.0F), ImVec2(mark.x, mark.y + r + 2.0F), color, 1.5F);
				} else if (kind == CommandMode::Guard) {
					drawList->AddTriangleFilled(ImVec2(mark.x - r, mark.y - r), ImVec2(mark.x + r, mark.y - r), ImVec2(mark.x, mark.y + r), color);
				} else if (kind == CommandMode::DefendAt) {
					drawList->AddLine(ImVec2(mark.x - r * 0.6F, mark.y + r), ImVec2(mark.x - r * 0.6F, mark.y - r), color, 1.5F);
					drawList->AddTriangleFilled(ImVec2(mark.x - r * 0.6F, mark.y - r), ImVec2(mark.x + r, mark.y - r * 0.4F), ImVec2(mark.x - r * 0.6F, mark.y + r * 0.2F), color);
				} else if (kind == CommandMode::Patrol) {
					drawList->AddCircle(mark, r, color, 0, 1.5F);
					drawList->AddTriangleFilled(ImVec2(mark.x + r, mark.y - 3.0F), ImVec2(mark.x + r + 3.0F, mark.y + 1.0F), ImVec2(mark.x + r - 3.0F, mark.y + 1.0F), color);
				}
			}
		}

		// What is guarded that isn't a unit (RC-10): a green ring round it (or its plot) with how many guard it, with the command tool in hand.
		if (CurrentTool().Kind == Tool::Command && !s_GuardPosts.empty()) {
			std::map<std::pair<long, int>, int> guarded;
			for (const auto& [unitID, post]: s_GuardPosts) {
				++guarded[{post.ObjectID, post.BuildingID}];
			}
			ImU32 green = c_CommandModeColors[static_cast<int>(CommandMode::Guard)];
			for (const auto& [what, guards]: guarded) {
				ImVec2 labelAt;
				if (what.first != 0) {
					const MovableObject* object = g_MovableMan.FindObjectByUniqueID(what.first);
					if (!object) {
						continue;
					}
					float radius = std::max(object->GetRadius(), 10.0F) / scale + 4.0F;
					ImVec2 at = ToScreen(object->GetPos());
					drawList->AddCircle(at, radius, fade(green, 200), 0, 1.5F);
					labelAt = ImVec2(at.x + radius + 3.0F, at.y - radius);
				} else {
					auto building = std::find_if(Colony::Buildings().begin(), Colony::Buildings().end(), [&what](const Colony::Building& each) { return each.ID == what.second; });
					if (building == Colony::Buildings().end()) {
						continue;
					}
					const Colony::Type& type = Colony::GetType(building->What);
					ImVec2 corner = ToScreen(building->Ground - Vector(static_cast<float>(type.Width) * 0.5F, static_cast<float>(type.Height)));
					drawList->AddRect(corner, ToScreen(building->Ground + Vector(static_cast<float>(type.Width) * 0.5F, 0.0F)), fade(green, 200), 0.0F, 0, 1.5F);
					labelAt = ImVec2(corner.x, corner.y - ImGui::GetTextLineHeight() - 2.0F);
				}
				if (!inView(labelAt, 40.0F)) {
					continue;
				}
				std::string text = std::to_string(guards) + (guards == 1 ? " guard" : " guards");
				drawList->AddText(labelAt, fade(green, 230), text.c_str());
			}
		}

		// "No route" markers: a red cross where they were sent, a dashed line from each unit, how many, and a click to send them again.
		long long now = g_TimerMan.GetSimUpdateCount();
		const NoRoute* hovered = NoRouteAt(io.MousePos);
		for (const NoRoute& marker: s_NoRoutes) {
			ImVec2 at = ToScreen(marker.Destination);
			float life = 1.0F - static_cast<float>(now - marker.At) / static_cast<float>(c_NoRouteUpdates);
			int alpha = static_cast<int>(255.0F * std::clamp(life * 3.0F, 0.0F, 1.0F));
			ImU32 red = IM_COL32(239, 90, 80, alpha);
			int alive = 0;
			for (const UnitRef& ref: marker.Units) {
				if (const Actor* unit = GetRef(ref)) {
					++alive;
					ImVec2 from = ToScreen(unit->GetPos());
					ImVec2 step((at.x - from.x), (at.y - from.y));
					float length = std::sqrt(step.x * step.x + step.y * step.y);
					for (float d = 0.0F; d < length; d += 10.0F) {
						float e = std::min(d + 5.0F, length);
						drawList->AddLine(ImVec2(from.x + step.x * d / length, from.y + step.y * d / length), ImVec2(from.x + step.x * e / length, from.y + step.y * e / length), fade(red, alpha / 2), 1.0F);
					}
				}
			}
			bool over = &marker == hovered;
			drawList->AddCircleFilled(at, 9.0F, IM_COL32(0, 0, 0, alpha * 2 / 3));
			drawList->AddLine(ImVec2(at.x - 5.0F, at.y - 5.0F), ImVec2(at.x + 5.0F, at.y + 5.0F), red, over ? 3.0F : 2.0F);
			drawList->AddLine(ImVec2(at.x - 5.0F, at.y + 5.0F), ImVec2(at.x + 5.0F, at.y - 5.0F), red, over ? 3.0F : 2.0F);
			std::string text = over ? "No route for " + std::to_string(alive) + (alive == 1 ? " unit: click to send it again" : " units: click to send them again") : std::string("No route");
			drawList->AddText(ImVec2(at.x + 12.0F, at.y - ImGui::GetTextLineHeight() * 0.5F), red, text.c_str());
		}

		// Under-fire pings, for the selection's side: a health drop sets one off, at most one in a stretch of ground every few seconds.
		double time = ImGui::GetTime();
		static std::unordered_map<long, float> lastHealth;
		if (lastHealth.size() > 4096) {
			lastHealth.clear();
		}
		int team = SelectionTeam();
		for (const Actor* unit: SandboxAccess::Actors()) {
			if (!IsCombatant(unit)) {
				continue;
			}
			long id = static_cast<long>(unit->GetUniqueID());
			float health = unit->GetHealth();
			auto last = lastHealth.find(id);
			bool hurt = last != lastHealth.end() && health < last->second - 0.5F;
			lastHealth[id] = health;
			if (!hurt || unit->GetTeam() != team || !g_SettingsMan.ShowSandboxAttackPings()) {
				continue;
			}
			bool recent = std::any_of(s_AttackPings.begin(), s_AttackPings.end(), [&](const AttackPing& ping) { return time - ping.Time < 6.0 && g_SceneMan.ShortestDistance(ping.Position, unit->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(250.0F); });
			if (!recent) {
				s_AttackPings.push_back({unit->GetPos(), time});
			}
		}
		s_AttackPings.erase(std::remove_if(s_AttackPings.begin(), s_AttackPings.end(), [time](const AttackPing& ping) { return time - ping.Time > 6.0; }), s_AttackPings.end());
		ImVec2 middle(view.x + view.w * 0.5F, view.y + view.h * 0.5F);
		for (const AttackPing& ping: s_AttackPings) {
			float age = static_cast<float>(time - ping.Time);
			if (age > 3.0F) {
				continue;
			}
			int alpha = static_cast<int>(230.0F * (1.0F - age / 3.0F));
			ImVec2 at = ToScreen(ping.Position);
			if (inView(at, 0.0F)) {
				float pulse = std::fmod(age, 1.0F);
				drawList->AddCircle(at, 10.0F + pulse * 30.0F, IM_COL32(255, 70, 60, static_cast<int>(alpha * (1.0F - pulse))), 0, 2.5F);
			} else {
				// At the edge of the picture, pointing the way.
				ImVec2 way(at.x - middle.x, at.y - middle.y);
				float length = std::max(std::sqrt(way.x * way.x + way.y * way.y), 1.0F);
				way = ImVec2(way.x / length, way.y / length);
				float reach = std::min(std::abs(way.x) > 0.001F ? (view.w * 0.5F - 24.0F) / std::abs(way.x) : 1e9F, std::abs(way.y) > 0.001F ? (view.h * 0.5F - 24.0F) / std::abs(way.y) : 1e9F);
				ImVec2 tip(middle.x + way.x * reach, middle.y + way.y * reach);
				ImVec2 side(-way.y * 9.0F, way.x * 9.0F);
				drawList->AddTriangleFilled(ImVec2(tip.x + way.x * 12.0F, tip.y + way.y * 12.0F), ImVec2(tip.x + side.x, tip.y + side.y), ImVec2(tip.x - side.x, tip.y - side.y), IM_COL32(255, 70, 60, alpha));
			}
		}
	}

	/// Puts the free camera over the middle of some units (RC-6: a control group's number pressed twice, or the idle-unit keys).
	void LookAtUnits(const std::vector<UnitRef>& units) {
		Vector sum;
		const Actor* first = nullptr;
		int count = 0;
		for (const UnitRef& ref: units) {
			if (const Actor* unit = GetRef(ref)) {
				first = first ? first : unit;
				// (Measured from the first, so a group across the scene's seam is looked at where it is, not halfway round the scene.)
				sum += g_SceneMan.ShortestDistance(first->GetPos(), unit->GetPos(), g_SceneMan.SceneWrapsX());
				++count;
			}
		}
		if (!first) {
			return;
		}
		Vector middle = first->GetPos() + sum / static_cast<float>(count);
		g_SceneMan.WrapPosition(middle);
		s_FreeCamera = true;
		s_FollowTarget = UnitRef();
		s_FollowAction = false;
		s_CameraCenter = middle;
	}

	/// Whether a unit is idle (RC-6): on no order, going nowhere, with no plan or order still to be given.
	bool IsIdle(Actor* unit) {
		if (!IsSelectable(unit) || unit->IsPlayerControlled() || unit->GetWaypointsSize() > 0 || s_Plans.count(unit->GetUniqueID())) {
			return false;
		}
		if (std::any_of(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); })) {
			return false;
		}
		int mode = unit->GetAIMode();
		return (mode == Actor::AIMODE_SENTRY || mode == Actor::AIMODE_NONE) && !unit->GetOrderHasPost() && !unit->GetOrderHold();
	}

	/// The next (or previous) idle unit of the selection's side after the one last gone to, by unique ID, selected and looked at; with Shift
	/// added to the selection instead.
	void CycleIdle(bool forward, bool add) {
		int team = SelectionTeam();
		std::vector<Actor*> idle;
		for (Actor* actor: SandboxAccess::Actors()) {
			if (actor->GetTeam() == team && IsCombatant(actor) && !dynamic_cast<const ACraft*>(actor) && IsIdle(actor)) {
				idle.push_back(actor);
			}
		}
		if (idle.empty()) {
			MarkOrder(MouseScenePosition(), IM_COL32(150, 150, 140, 255));
			return;
		}
		std::sort(idle.begin(), idle.end(), [](const Actor* a, const Actor* b) { return a->GetUniqueID() < b->GetUniqueID(); });
		Actor* next = nullptr;
		if (forward) {
			auto after = std::find_if(idle.begin(), idle.end(), [](const Actor* actor) { return static_cast<long>(actor->GetUniqueID()) > s_LastIdleID; });
			next = after != idle.end() ? *after : idle.front();
		} else {
			auto before = std::find_if(idle.rbegin(), idle.rend(), [](const Actor* actor) { return static_cast<long>(actor->GetUniqueID()) < s_LastIdleID; });
			next = before != idle.rend() ? *before : idle.back();
		}
		s_LastIdleID = static_cast<long>(next->GetUniqueID());
		if (!add) {
			s_Selected.clear();
		}
		if (std::none_of(s_Selected.begin(), s_Selected.end(), [next](const UnitRef& ref) { return RefersTo(ref, next); })) {
			s_Selected.push_back(MakeRef(next));
		}
		LookAtUnits({MakeRef(next)});
	}

	/// Every unit in the view of the kinds already selected (RC-6), as a double click on one does for its kind.
	void SelectKindsInView() {
		std::unordered_set<std::string> kinds;
		int team = SelectionTeam();
		for (const UnitRef& ref: s_Selected) {
			if (const Actor* unit = GetRef(ref)) {
				kinds.insert(unit->GetPresetName());
			}
		}
		if (kinds.empty()) {
			return;
		}
		GameViewRect view = g_WindowMan.GetGameViewRect();
		float scale = ScenePixelsPerWindowPixel();
		for (Actor* actor: SandboxAccess::Actors()) {
			Vector onScreen = FromCamera(actor->GetPos());
			if (IsSelectable(actor) && actor->GetTeam() == team && kinds.count(actor->GetPresetName()) && onScreen.m_X >= 0.0F && onScreen.m_Y >= 0.0F && onScreen.m_X <= view.w * scale && onScreen.m_Y <= view.h * scale &&
			    std::none_of(s_Selected.begin(), s_Selected.end(), [actor](const UnitRef& ref) { return RefersTo(ref, actor); })) {
				s_Selected.push_back(MakeRef(actor));
			}
		}
	}

	/// The order keys of the command tool (RC-6), each listed on the Keys page. None of them is WASD or the arrows, which move the view, and
	/// none is read while you play a unit or with Ctrl or Alt held (the caller sees to that).
	void CommandHotkeys() {
		const ImGuiIO& io = ImGui::GetIO();
		auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
		auto mode = [](CommandMode to) {
			s_CommandMode = to;
			if (to == CommandMode::Patrol) {
				s_PatrolDraft.clear();
			}
		};
		if (pressed(ImGuiKey_M)) {
			mode(CommandMode::Move);
		} else if (pressed(ImGuiKey_T)) {
			mode(CommandMode::Attack);
		} else if (pressed(ImGuiKey_F)) {
			mode(CommandMode::AttackMove);
		} else if (pressed(ImGuiKey_G)) {
			mode(CommandMode::Guard);
		} else if (pressed(ImGuiKey_B)) {
			mode(CommandMode::DefendAt);
		} else if (pressed(ImGuiKey_R)) {
			mode(CommandMode::Patrol);
		}
		if (!s_Selected.empty() && (pressed(ImGuiKey_H) || pressed(ImGuiKey_C))) {
			// Defend where they stand (with Shift, the last step of their plans), or cancel their orders: as the ring's slices.
			Stroke stroke;
			stroke.Kind = Tool::OrderSelected;
			stroke.Position = MouseScenePosition();
			stroke.Count = 100 + (ImGui::IsKeyPressed(ImGuiKey_H, false) ? (io.KeyShift ? 13 : 3) : 2);
			s_Queue.push_back(stroke);
		}
		if (!s_Selected.empty() && pressed(ImGuiKey_O)) {
			// Focus on objective: their team's job in the battle, as the ring's slice.
			QueueOrder(Order::BattleObjective);
		}
		if (!s_Selected.empty() && pressed(ImGuiKey_V)) {
			// The next weapons rule (from mixed, the first).
			QueueRule(true, (std::max(SelectedRule(true), -1) + 1) % static_cast<int>(Actor::WEAPONRULECOUNT));
		}
		if (!s_Selected.empty() && pressed(ImGuiKey_Y)) {
			QueueRule(false, (std::max(SelectedRule(false), -1) + 1) % static_cast<int>(Actor::MOVEMENTRULECOUNT));
		}
		if (pressed(ImGuiKey_L)) {
			s_Formation = static_cast<Formation>((static_cast<int>(s_Formation) + 1) % static_cast<int>(Formation::Count));
		}
		if (pressed(ImGuiKey_K)) {
			s_KeepPace = !s_KeepPace;
		}
		if (pressed(ImGuiKey_Period)) {
			CycleIdle(true, io.KeyShift);
		} else if (pressed(ImGuiKey_Comma)) {
			CycleIdle(false, io.KeyShift);
		}
		if (pressed(ImGuiKey_Q)) {
			SelectKindsInView();
		}
		if (pressed(ImGuiKey_N)) {
			g_SettingsMan.SetShowSandboxMinimap(!g_SettingsMan.ShowSandboxMinimap());
		}
	}

	/// The Keys page of the sandbox window (RC-6): every key the sandbox's tools answer to, and the group badges.
	void KeysPage() {
		static ImGuiTextFilter filter;
		filter.Draw("Search##keys", ImGui::GetContentRegionAvail().x * 0.6F);
		struct Key {
			const char* Keys;
			const char* What;
		};
		static const Key camera[] = {{"WASD / arrows", "Move the view (Shift: faster)"}, {"Right drag", "Move the view (with a Paint tool in hand: dig)"}, {"Middle drag", "Move the view"}, {"Wheel", "Zoom"}, {"Tab", "Hide or show the tools (God mode: into your character with nothing in hand)"}, {"P", "Into your character and back out"}, {"Shift+Tab", "Put your character where the mouse points and go into it"}, {"F7", "The sandbox window"}, {"U", "Hide or show the bar along the bottom"}, {"F9", "Commander view, outside the Sandbox game mode: your side from above, and back into your unit"}, {"Ctrl+Z", "Undo the last paint stroke or the last thing placed"}};
		static const Key command[] = {{"Left click", "Order the selection, as the mode says; on a friend, select it"}, {"Left drag", "Select units in a box"}, {"Shift+click", "Add to the selection; with an order, add it to their plans"}, {"Double click", "Every unit of that kind in view"}, {"Right button", "The order ring (right click a plan's numbered step to drop it)"}, {"Click a red cross", "Send the units that had no route there again"}, {"Alt+drag", "Move or attack-move facing the way dragged"}, {"M / T / F / G", "Move, Attack, Attack-move (fight), Guard"}, {"B / R", "Defend at, Patrol"}, {"H", "Defend where they stand (Shift: last step of their plans)"}, {"C", "Cancel their orders"}, {"O", "Focus on objective: their team's job in the battle (a flag, a hill, the place it defends)"}, {"V / Y", "Next weapons rule, next movement rule"}, {"L / K", "Next formation, keep together on or off"}, {". / ,", "Next or previous idle unit (Shift: add it)"}, {"Q", "Every unit in view of the kinds selected"}, {"N", "The map: click to look, drag to select, right click to order"}, {"Ctrl+number", "Keep the selection as a group"}, {"Number", "Bring a group back; twice quickly, look at it"}, {"Ctrl+A", "Everyone on the selection's side"}};
		auto table = [](const char* id, const Key* keys, size_t count) {
			if (ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				for (size_t i = 0; i < count; ++i) {
					if (!filter.PassFilter(keys[i].Keys) && !filter.PassFilter(keys[i].What)) {
						continue;
					}
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(keys[i].Keys);
					ImGui::TableNextColumn();
					ImGui::TextWrapped("%s", keys[i].What);
				}
				ImGui::EndTable();
			}
		};
		ImGui::SeparatorText("View");
		table("##keysView", camera, std::size(camera));
		ImGui::SeparatorText("Command tool");
		ImGui::TextDisabled("Not while you play a unit: its keys are its own then.");
		table("##keysCommand", command, std::size(command));
		bool badges = g_SettingsMan.ShowSandboxGroupBadges();
		if (ToolUI::Checkbox("Group numbers over units", &badges)) {
			g_SettingsMan.SetShowSandboxGroupBadges(badges);
		}
		ImGui::SetItemTooltip("A unit kept in a control group (Ctrl+number) shows the group's number by its feet.");
	}

	void DrawCursor() {
		ImGuiIO& io = ImGui::GetIO();
		const ToolInfo& tool = CurrentTool();
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		ImU32 white = IM_COL32(255, 255, 255, 170);
		std::string label = IsBattleTool(tool.Kind) ? BattleToolLabel(tool.Kind) : std::string();
		if (label.empty()) {
			label = tool.Name;
		}
		if (tool.Kind == Tool::Barracks || tool.Kind == Tool::Extractor) {
			// The plot it will take, on the ground under the pointer.
			const Colony::Type& type = Colony::GetType(tool.Kind == Tool::Barracks ? Colony::Kind::Barracks : Colony::Kind::Extractor);
			Vector ground = MouseScenePosition();
			int sceneHeight = g_SceneMan.GetSceneHeight();
			for (int tries = 0; tries < 600 && g_SceneMan.GetTerrMatter(ground.GetFloorIntX(), ground.GetFloorIntY()) != g_MaterialAir; ++tries) {
				ground.m_Y -= 1.0F;
			}
			while (ground.m_Y < static_cast<float>(sceneHeight - 2) && g_SceneMan.GetTerrMatter(ground.GetFloorIntX(), ground.GetFloorIntY() + 1) == g_MaterialAir) {
				ground.m_Y += 1.0F;
			}
			Vector corner = FromCamera(ground + Vector(-static_cast<float>(type.Width / 2), 1.0F - static_cast<float>(type.Height)));
			ImVec2 topLeft(ViewOrigin().x + corner.m_X / scale, ViewOrigin().y + corner.m_Y / scale);
			drawList->AddRect(topLeft, ImVec2(topLeft.x + static_cast<float>(type.Width) / scale, topLeft.y + static_cast<float>(type.Height) / scale), c_SideColors[s_Team], 0.0F, 0, 1.5F);
		} else if (tool.Kind == Tool::Structure) {
			if (const Preset* preset = ChosenPreset(Tool::Structure, s_StructureChoice)) {
				// The piece itself, see-through, exactly where a click will put it, with its outline.
				const PiecePicture& picture = PictureOf(*preset);
				if (picture.Width > 0) {
					Vector corner = FromCamera(StructurePosition(*preset, MouseScenePosition(), s_SnapToGrid) + Vector(picture.OffsetX, picture.OffsetY));
					ImVec2 topLeft(ViewOrigin().x + corner.m_X / scale, ViewOrigin().y + corner.m_Y / scale);
					ImVec2 bottomRight(topLeft.x + static_cast<float>(picture.Width) / scale, topLeft.y + static_cast<float>(picture.Height) / scale);
					GameViewRect view = g_WindowMan.GetGameViewRect();
					drawList->PushClipRect(ImVec2(view.x, view.y), ImVec2(view.x + view.w, view.y + view.h));
					drawList->AddImage(static_cast<ImTextureID>(picture.Texture), topLeft, bottomRight, ImVec2(0.0F, 0.0F), ImVec2(1.0F, 1.0F), IM_COL32(255, 255, 255, 190));
					drawList->AddRect(topLeft, bottomRight, IM_COL32(255, 255, 255, 110), 0.0F, 0, 1.0F);
					drawList->PopClipRect();
				}
			}
		} else {
			float outline = tool.UsesRadius ? static_cast<float>(s_Radius) / scale : 6.0F;
			if (IsTerrainBrush(tool.Kind) && s_BrushShape == BrushShape::Square) {
				// The square brush: the square it paints.
				float half = std::max(outline, 3.0F);
				drawList->AddRect(ImVec2(io.MousePos.x - half, io.MousePos.y - half), ImVec2(io.MousePos.x + half, io.MousePos.y + half), white, 0.0F, 0, 1.5F);
			} else {
				drawList->AddCircle(io.MousePos, std::max(outline, 3.0F), tool.Kind == Tool::Unit || tool.Kind == Tool::Brain || tool.Kind == Tool::RallyPoint ? c_SideColors[s_Team] : white, 0, 1.5F);
			}
		}
		if (const Preset* preset = (tool.Kind == Tool::Unit || tool.Kind == Tool::Brain || tool.Kind == Tool::Item || tool.Kind == Tool::Structure) ? ChosenPreset(tool.Kind, ChoiceFor(tool.Kind)) : nullptr) {
			label = preset->PresetName;
			if (tool.Kind == Tool::Unit && s_SquadSize > 1) {
				label += " x" + std::to_string(s_SquadSize);
			}
		}
		float pixel = ToolUI::Pixel();
		auto flag = [&](const Vector& spot, ImU32 color) {
			ImVec2 at = ToScreen(spot);
			drawList->AddTriangleFilled(ImVec2(at.x, at.y - pixel * 2.0F), ImVec2(at.x - pixel * 3.0F, at.y - pixel * 7.0F), ImVec2(at.x + pixel * 3.0F, at.y - pixel * 7.0F), color);
			drawList->AddRectFilled(ImVec2(at.x - pixel * 4.0F, at.y - pixel), ImVec2(at.x + pixel * 4.0F, at.y + pixel), color);
		};
		auto crosshair = [&](const Vector& where, ImU32 color, float reach) {
			ImVec2 at = ToScreen(where);
			drawList->AddCircle(at, reach, color, 0, pixel);
			drawList->AddLine(ImVec2(at.x - reach * 1.4F, at.y), ImVec2(at.x - reach * 0.5F, at.y), color, pixel);
			drawList->AddLine(ImVec2(at.x + reach * 0.5F, at.y), ImVec2(at.x + reach * 1.4F, at.y), color, pixel);
			drawList->AddLine(ImVec2(at.x, at.y - reach * 1.4F), ImVec2(at.x, at.y - reach * 0.5F), color, pixel);
			drawList->AddLine(ImVec2(at.x, at.y + reach * 0.5F), ImVec2(at.x, at.y + reach * 1.4F), color, pixel);
		};
		// With the reachability preview on: each spot the order will look at, ringed green where a unit goes, red where the first unit has no path,
		// grey where it wasn't needed, with the path cost.
		auto reachMarks = [&](const std::vector<Actor*>& units, const Vector& point) {
			if (!g_SettingsMan.ShowSandboxSpotReach() || units.empty()) {
				return;
			}
			for (const SpotReach& entry: SpotReachPreview(units, point)) {
				ImVec2 at = ToScreen(entry.Spot - Vector(0.0F, 4.0F));
				ImU32 color = entry.Cost == -1.0F ? IM_COL32(239, 90, 80, 255) : entry.Chosen ? IM_COL32(120, 230, 110, 255) : IM_COL32(150, 150, 140, 200);
				drawList->AddCircle(at, pixel * 7.0F, color, 0, entry.Chosen ? pixel * 1.5F : pixel);
				std::string cost = entry.Cost == -2.0F ? "not tried" : entry.Cost < 0.0F ? "no path" : std::to_string(static_cast<int>(entry.Cost + 0.5F));
				drawList->AddText(ImVec2(at.x + pixel * 9.0F, at.y - ImGui::GetTextLineHeight() * 0.5F), color, cost.c_str());
			}
		};
		if (tool.Kind == Tool::OrderMove) {
			// Where each unit will stand: a marker on the ground for every one, so the order can be seen before it is given.
			std::vector<Actor*> units = UnitsToMove(s_Team, false);
			reachMarks(units, MouseScenePosition());
			for (const Vector& spot: StandingSpots(MouseScenePosition(), static_cast<int>(units.size()))) {
				flag(spot, c_SideColors[s_Team]);
			}
			label = units.empty() ? std::string(c_SideNames[s_Team]) + " has no units to move" : std::to_string(units.size()) + (units.size() == 1 ? " unit will come here" : " units will come here");
		} else if (tool.Kind == Tool::Command) {
			// The zones the selected units already defend.
			DrawCommandedZones(drawList);
			// What the click will do, in the mode's own colour and marks.
			Vector point = MouseScenePosition();
			Actor* under = dynamic_cast<Actor*>(ObjectUnder(point, true));
			if (HiddenFromCommander(under)) {
				under = nullptr;
			}
			bool underIsUnit = under && IsCombatant(under) && !under->IsInGroup("Brains");
			bool underIsFriend = underIsUnit && (s_Selected.empty() || under->GetTeam() == SelectionTeam());
			std::vector<Actor*> units = UnitsToMove(0, true);
			std::string count = std::to_string(units.size()) + (units.size() == 1 ? " unit" : " units");
			// A move's formation (RC-5): at the pointer, or while Alt-dragging at where the drag began, facing the way dragged.
			Vector formationPoint = point;
			int formationFacing = 0;
			if (s_Dragging && io.KeyAlt) {
				formationPoint = g_CameraMan.GetOffset(0) + Vector(s_DragStart.x - ViewOrigin().x, s_DragStart.y - ViewOrigin().y) * scale;
				g_SceneMan.WrapPosition(formationPoint);
				float across = io.MousePos.x - s_DragStart.x;
				formationFacing = across * scale > 12.0F ? 1 : (across * scale < -12.0F ? -1 : 0);
			}
			std::string formation = std::string(" in ") + c_FormationNames[static_cast<int>(s_Formation)] + (s_KeepPace ? ", kept together" : "") + (s_Dragging && io.KeyAlt ? "" : "  (Alt-drag: face a way)");
			if (units.empty() || (underIsFriend && s_CommandMode == CommandMode::Move)) {
				if (underIsUnit) {
					drawList->AddCircle(ToScreen(under->GetPos()), std::max(under->GetRadius() / scale, 8.0F) + pixel * 2.0F, IM_COL32(255, 255, 255, 200), 0, pixel);
					label = "Select " + under->GetPresetName() + "  (Shift: add, double click: all of this kind)";
				} else {
					label = units.empty() ? "Drag a box round units to select them" : "Move " + count + " here";
				}
			} else if (s_CommandMode == CommandMode::Attack || (s_CommandMode == CommandMode::Move && underIsUnit && !underIsFriend)) {
				ImU32 red = IM_COL32(239, 106, 91, 255);
				Actor* target = (underIsUnit && !underIsFriend) ? under : nullptr;
				float nearest = 400.0F * 400.0F;
				for (Actor* actor: SandboxAccess::Actors()) {
					if (target || !IsCombatant(actor) || actor->IsIgnoredByAI() || actor->GetTeam() == SelectionTeam() || HiddenFromCommander(actor)) {
						continue;
					}
					float distance = g_SceneMan.ShortestDistance(point, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
					if (distance < nearest) {
						nearest = distance;
						target = actor;
					}
				}
				if (target && !(underIsUnit && !underIsFriend)) {
					// Found near the point rather than under the pointer.
					for (Actor* actor: SandboxAccess::Actors()) {
						if (IsCombatant(actor) && !actor->IsIgnoredByAI() && actor->GetTeam() != SelectionTeam() && !HiddenFromCommander(actor) && g_SceneMan.ShortestDistance(point, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude() <= nearest) {
							target = actor;
						}
					}
				}
				if (target) {
					crosshair(target->GetPos(), red, std::max(target->GetRadius() / scale, 8.0F) + pixel * 3.0F);
					drawList->AddLine(io.MousePos, ToScreen(target->GetPos()), (red & 0x00FFFFFF) | (120u << IM_COL32_A_SHIFT), pixel);
					label = "Attack " + target->GetPresetName() + " with " + count;
				} else {
					crosshair(point, (red & 0x00FFFFFF) | (110u << IM_COL32_A_SHIFT), pixel * 6.0F);
					label = "Attack: point at or near an enemy  (to fight towards a place, Attack-move)";
				}
			} else if (s_CommandMode == CommandMode::AttackMove) {
				// Attack-move (RC-2): where each will stand, and the crosshair over the place, in the mode's orange.
				ImU32 orange = c_CommandModeColors[static_cast<int>(CommandMode::AttackMove)];
				for (const Vector& spot: FormationSpots(units, formationPoint, static_cast<int>(units.size()), formationFacing)) {
					flag(spot, orange);
				}
				crosshair(formationPoint, orange, pixel * 6.0F);
				reachMarks(units, formationPoint);
				label = "Attack-move " + count + " here" + formation + ": they fight what they meet on the way";
			} else if (s_CommandMode == CommandMode::DefendAt) {
				// Defend at (RC-4): where each will stand to hold the place.
				ImU32 amber = c_CommandModeColors[static_cast<int>(CommandMode::DefendAt)];
				for (const Vector& spot: StandingSpots(point, static_cast<int>(units.size()))) {
					flag(spot, amber);
				}
				// And the zone they'll hold: the radius they go after enemies in, and the chase past it (faint).
				DrawDefendZone(drawList, point, static_cast<float>(s_DefendRadius), static_cast<float>(s_DefendChase), amber);
				label = "Defend here with " + count + "  (drag left or right to face that way)";
			} else if (s_CommandMode == CommandMode::Patrol) {
				label = s_PatrolDraft.empty() ? "Click the first point of the patrol route" : "Click point " + std::to_string(s_PatrolDraft.size() + 1) + " of the route, or start it on the command row";
			} else if (s_CommandMode == CommandMode::Guard) {
				ImU32 green = IM_COL32(120, 220, 120, 255);
				auto guardRing = [&](const Vector& where, float sceneRadius) {
					ImVec2 at = ToScreen(where);
					float reach = std::max(sceneRadius / scale, 8.0F) + pixel * 3.0F;
					drawList->AddCircle(at, reach, green, 0, pixel * 1.5F);
					drawList->AddCircle(at, reach + pixel * 3.0F, (green & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT), 0, pixel);
				};
				// (Besides a friend to follow, RC-10: your brain, your craft, a crate or other loose object, or a colony building.)
				bool brain = under && under->IsInGroup("Brains") && under->GetTeam() == SelectionTeam() && !dynamic_cast<const ACraft*>(under);
				const Colony::Building* building = under ? nullptr : BuildingAt(point);
				MovableObject* object = !under || dynamic_cast<const ACraft*>(under) ? GuardableObjectAt(point, SelectionTeam()) : nullptr;
				if (underIsFriend || brain) {
					guardRing(under->GetPos(), under->GetRadius());
					label = count + " guard " + under->GetPresetName();
				} else if (object) {
					guardRing(object->GetPos(), std::max(object->GetRadius(), 10.0F));
					for (const Vector& spot: StandingSpots(object->GetPos(), static_cast<int>(units.size()))) {
						flag(spot, green);
					}
					DrawDefendZone(drawList, object->GetPos(), static_cast<float>(s_DefendRadius), static_cast<float>(s_DefendChase), green);
					label = count + " guard " + object->GetPresetName() + ", holding posts round it and seeing off enemies that come near";
				} else if (building) {
					const Colony::Type& type = Colony::GetType(building->What);
					drawList->AddRect(ToScreen(building->Ground - Vector(static_cast<float>(type.Width) * 0.5F, static_cast<float>(type.Height))), ToScreen(building->Ground + Vector(static_cast<float>(type.Width) * 0.5F, 0.0F)), green, 0.0F, 0, pixel * 1.5F);
					for (const Vector& spot: StandingSpots(building->Ground, static_cast<int>(units.size()))) {
						flag(spot, green);
					}
					DrawDefendZone(drawList, building->Ground, static_cast<float>(s_DefendRadius), static_cast<float>(s_DefendChase), green);
					label = count + " guard the " + type.Name + ", holding posts round it and seeing off enemies that come near";
				} else {
					label = "Guard: point at a friendly unit, your brain or craft, a crate or a colony building for " + count + " to stay with";
				}
			} else {
				for (const Vector& spot: FormationSpots(units, formationPoint, static_cast<int>(units.size()), formationFacing)) {
					flag(spot, IM_COL32(110, 180, 250, 255));
				}
				reachMarks(units, formationPoint);
				label = "Move " + count + " here" + formation;
			}
			// With Shift held, the order is a further step of their plans (RC-3), not one for now.
			if (io.KeyShift && !units.empty() && !(underIsFriend && s_CommandMode == CommandMode::Move) && !label.empty()) {
				label = "Then: " + label + "  (added to the plan)";
			}
		}
		if (TakesSide(tool.Kind) && tool.Kind != Tool::Structure) {
			// Whose it will be, in that side's colour, and how to change it.
			std::string whose = std::string(c_SideNames[s_Team]) + "  (right button: change side)";
			ImVec2 at(io.MousePos.x + 14.0F, io.MousePos.y + 10.0F);
			drawList->AddText(ImVec2(at.x + 1.0F, at.y + 1.0F), IM_COL32(0, 0, 0, 200), whose.c_str());
			drawList->AddText(at, c_SideColors[s_Team], whose.c_str());
		}
		// A bunker piece is shown as itself, so its name would only be in the way; under the ring of sides nothing is.
		if (tool.Kind != Tool::Structure && !s_RingOpen) {
			drawList->AddText(ImVec2(io.MousePos.x + 14.0F, io.MousePos.y - 8.0F), IM_COL32(255, 255, 255, 220), label.c_str());
		}
	}


	/// A picture made from one of the game's own 8-bit image files, the first time it is asked for: the pie menu's icons and cursor.
	const PiecePicture& PictureOfFile(const std::string& path) {
		std::map<std::string, PiecePicture>& pictures = s_FilePictures;
		if (auto found = pictures.find(path); found != pictures.end()) {
			return found->second;
		}
		PiecePicture& picture = pictures[path];
		BITMAP* bitmap = ContentFile(path.c_str()).GetAsBitmap();
		if (!bitmap || bitmap_color_depth(bitmap) != 8) {
			return picture;
		}
		picture.Width = bitmap->w;
		picture.Height = bitmap->h;
		std::vector<unsigned char> pixels(static_cast<size_t>(picture.Width) * picture.Height * 4, 0);
		for (int y = 0; y < picture.Height; ++y) {
			for (int x = 0; x < picture.Width; ++x) {
				int index = bitmap->line[y][x];
				if (index == ColorKeys::g_MaskColor) {
					continue;
				}
				unsigned char* pixel = &pixels[(static_cast<size_t>(y) * picture.Width + x) * 4];
				PaletteColor(index, pixel);
				pixel[3] = 255;
			}
		}
		GLint boundBefore = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundBefore);
		glGenTextures(1, &picture.Texture);
		glBindTexture(GL_TEXTURE_2D, picture.Texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, picture.Width, picture.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundBefore));
		return picture;
	}

	/// A ring of choices round where the right button went down, held open while it is held, in the look of the game's own pie menu: a dark
	/// band with the choices' icons round it, separated by lines, the cursor on the inner edge pointing at the one under the pointer, and
	/// that one's name outside the band. Letting go takes it.
	/// @param items The choices, from the top going clockwise. @param current The one in force now, named when the pointer is in the middle.
	/// @param sticky The ring stays up after the right button is let go, and a left click takes the choice under the pointer (a right click, none).
	/// @return The choice let go over, -1 for none (let go in the middle), or -2 while the ring is still up.
	int DrawRing(const std::vector<RingItem>& items, int current, bool sticky) {
		ImGuiIO& io = ImGui::GetIO();
		float pixel = ToolUI::Pixel();
		// The game's pie menu: an inner radius of 58 and a band 16 thick, in game pixels. A little thicker here, for icons at twice the size.
		float inner = pixel * 50.0F;
		float thickness = pixel * 26.0F;
		float outer = inner + thickness;
		int count = static_cast<int>(items.size());
		ImVec2 away(io.MousePos.x - s_RingCenter.x, io.MousePos.y - s_RingCenter.y);
		float distance = std::sqrt(away.x * away.x + away.y * away.y);
		// Each choice has an equal slice; the first is centred straight up.
		const float slice = 6.2832F / static_cast<float>(count);
		int under = -1;
		if (distance > inner * 0.5F) {
			float angle = std::atan2(away.y, away.x) + 1.5708F + slice * 0.5F; // 0 at the top edge of the first slice, growing clockwise.
			while (angle < 0.0F) {
				angle += 6.2832F;
			}
			under = static_cast<int>(angle / slice) % count;
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		const ImU32 band = IM_COL32(0, 0, 0, 128); // The pie menu's background: black at half.
		const ImU32 separator = IM_COL32(0, 0, 0, 220);
		// The band.
		drawList->AddCircle(s_RingCenter, (inner + outer) * 0.5F, band, 64, thickness);
		// The slice under the pointer, lit a little (an arc as thick as the band: a filled sector isn't convex, and bled into the middle).
		if (under >= 0) {
			float from = -1.5708F - slice * 0.5F + slice * static_cast<float>(under);
			drawList->PathClear();
			drawList->PathArcTo(s_RingCenter, (inner + outer) * 0.5F, from, from + slice, 16);
			drawList->PathStroke(IM_COL32(255, 255, 255, 40), 0, thickness);
		}
		// The separators, and the edges.
		for (int i = 0; i < count; ++i) {
			float edge = -1.5708F - slice * 0.5F + slice * static_cast<float>(i);
			ImVec2 a(s_RingCenter.x + std::cos(edge) * inner, s_RingCenter.y + std::sin(edge) * inner);
			ImVec2 b(s_RingCenter.x + std::cos(edge) * outer, s_RingCenter.y + std::sin(edge) * outer);
			drawList->AddLine(a, b, separator, pixel * 2.0F);
		}
		drawList->AddCircle(s_RingCenter, inner, separator, 64, pixel);
		drawList->AddCircle(s_RingCenter, outer, separator, 64, pixel);
		// The icons, each in the middle of its slice, at twice their size; a choice without one shows its colour.
		for (int i = 0; i < count; ++i) {
			float middle = -1.5708F + slice * static_cast<float>(i);
			ImVec2 at(s_RingCenter.x + std::cos(middle) * (inner + thickness * 0.5F), s_RingCenter.y + std::sin(middle) * (inner + thickness * 0.5F));
			// (Each icon's path made once, not per item per frame.)
			static std::unordered_map<const char*, std::string> s_RingIconPaths;
			const PiecePicture* picture = nullptr;
			if (items[i].Icon) {
				auto [path, added] = s_RingIconPaths.try_emplace(items[i].Icon);
				if (added) {
					path->second = std::string("Base.rte/GUIs/PieMenus/PieIcons/") + items[i].Icon + "000.png";
				}
				picture = &PictureOfFile(path->second);
			}
			if (picture && picture->Texture) {
				float w = static_cast<float>(picture->Width) * pixel * 2.0F;
				float h = static_cast<float>(picture->Height) * pixel * 2.0F;
				ImVec2 corner(std::floor(at.x - w * 0.5F), std::floor(at.y - h * 0.5F));
				ImU32 tint = (i == under || (under < 0 && i == current)) ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 255);
				drawList->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(picture->Texture)), corner, ImVec2(corner.x + w, corner.y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
			} else {
				drawList->AddCircleFilled(at, pixel * 6.0F, items[i].Color);
				drawList->AddCircle(at, pixel * 6.0F, separator, 0, pixel);
			}
		}
		// The cursor on the inner edge, pointing at the choice under the pointer (or the one in force), as the pie menu's does.
		int shown = under >= 0 ? under : current;
		if (shown >= 0 && shown < count) {
			float middle = -1.5708F + slice * static_cast<float>(shown);
			const PiecePicture& cursor = PictureOfFile("Base.rte/GUIs/PieMenus/PieCursor.png");
			if (cursor.Texture) {
				// The cursor art points right; it is turned to the slice.
				float w = static_cast<float>(cursor.Width) * pixel * 2.0F;
				float h = static_cast<float>(cursor.Height) * pixel * 2.0F;
				ImVec2 at(s_RingCenter.x + std::cos(middle) * (inner - w * 0.5F), s_RingCenter.y + std::sin(middle) * (inner - w * 0.5F));
				float c = std::cos(middle);
				float s = std::sin(middle);
				auto turned = [&](float x, float y) { return ImVec2(at.x + x * c - y * s, at.y + x * s + y * c); };
				drawList->AddImageQuad(static_cast<ImTextureID>(static_cast<intptr_t>(cursor.Texture)), turned(-w * 0.5F, -h * 0.5F), turned(w * 0.5F, -h * 0.5F), turned(w * 0.5F, h * 0.5F), turned(-w * 0.5F, h * 0.5F));
			}
			// Its name, outside the band on that side.
			const char* label = items[shown].Label;
			ImVec2 nameSize = ImGui::CalcTextSize(label);
			float textReach = outer + pixel * 4.0F;
			ImVec2 anchor(s_RingCenter.x + std::cos(middle) * textReach, s_RingCenter.y + std::sin(middle) * textReach);
			float x = std::cos(middle) > 0.3F ? anchor.x : (std::cos(middle) < -0.3F ? anchor.x - nameSize.x : anchor.x - nameSize.x * 0.5F);
			float y = std::sin(middle) > 0.3F ? anchor.y : (std::sin(middle) < -0.3F ? anchor.y - nameSize.y : anchor.y - nameSize.y * 0.5F);
			ImVec2 pos(std::floor(x), std::floor(y));
			drawList->AddText(ImVec2(pos.x + pixel, pos.y + pixel), IM_COL32(0, 0, 0, 220), label);
			drawList->AddText(pos, IM_COL32(255, 255, 255, 255), label);
		}
		static const bool testHeld = std::getenv("CCCP_TEST_RING") != nullptr;
		if (sticky) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s_RingOpen = false;
				return under;
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				s_RingOpen = false;
				return -1;
			}
			return -2;
		}
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || testHeld) {
			return -2;
		}
		s_RingOpen = false;
		return under;
	}

	/// A queued step of a selected unit's plan, where it is drawn (RC-3).
	struct PlanMarker {
		long UnitID;
		int Step; //!< Its place in the plan's steps still to come.
		Vector Place;
		PlanKind Kind;
	};

	/// The queued steps of the selected units' plans, in order, each where it is drawn: its place, or for an attack or guard where the
	/// enemy or friend is now.
	std::vector<PlanMarker> PlanMarkers() {
		std::vector<PlanMarker> markers;
		for (const UnitRef& ref: s_Selected) {
			const Actor* unit = GetRef(ref);
			auto plan = unit ? s_Plans.find(unit->GetUniqueID()) : s_Plans.end();
			if (plan == s_Plans.end()) {
				continue;
			}
			for (size_t i = 0; i < plan->second.Steps.size(); ++i) {
				const PlanStep& step = plan->second.Steps[i];
				const Actor* target = GetRef(step.Target);
				markers.push_back({unit->GetUniqueID(), static_cast<int>(i), target ? target->GetPos() : step.Place, step.Kind});
			}
		}
		return markers;
	}

	/// The colour a step of a plan is drawn in: its command mode's.
	ImU32 PlanColor(PlanKind kind) {
		switch (kind) {
			case PlanKind::AttackMove:
				return c_CommandModeColors[static_cast<int>(CommandMode::AttackMove)];
			case PlanKind::Attack:
				return c_CommandModeColors[static_cast<int>(CommandMode::Attack)];
			case PlanKind::Guard:
				return c_CommandModeColors[static_cast<int>(CommandMode::Guard)];
			case PlanKind::Defend:
				return IM_COL32(242, 182, 61, 255);
			default:
				return c_CommandModeColors[static_cast<int>(CommandMode::Move)];
		}
	}

	/// The command tool's right-click menu (RC-12), in place of its rings unless the classic wheel is asked for: every command on one layer,
	/// a list above the pointer in the action menu's style (ActionMenu). Held, letting go over a row picks it; a quick click leaves it up
	/// for a click. The units' state and the settings in it (the selected units' AI mode, weapons and movement rules, the formation, keeping
	/// together, the order markers) stay up for more; a command or a mode for the clicks to come ends it.
	void DrawCommandMenu() {
		ImGuiIO& io = ImGui::GetIO();
		static int lastFrame = -10;
		static double openedAt = 0.0;
		static bool sticky = false;
		int frame = ImGui::GetFrameCount();
		if (frame != lastFrame + 1) {
			// Just opened (the ring flag was set by the right click this frame).
			openedAt = ImGui::GetTime();
			sticky = false;
		}
		lastFrame = frame;

		enum MenuAction { ClickMode, Now, AIMode, Weapons, Movement, FormationPick, KeepPacePick, MarkersPick };
		float scale = std::clamp(g_WindowMan.GetGameViewRect().h / 720.0F, 0.9F, 2.2F);
		ActionMenu::MenuLayout menu(scale);
		// In three parts, each drawn its own way (ActionMenu::Kind): what is done to the selected units now, how they are set now, and your own
		// settings for the orders to come.
		using Kind = ActionMenu::Kind;
		menu.Heading("Selected units", Kind::Command);
		menu.Choices(Now, {"Defend here", "Cancel orders", "Deselect", "Focus on objective", "Follow team orders"}, -1);
		menu.Heading("AI mode", Kind::State);
		menu.Choices(AIMode, {"Sentry", "Hunt brains", "Dig for gold", "Rally point", "Do nothing"}, SelectedAIMode(), 3);
		menu.Heading("Weapons", Kind::State);
		menu.Choices(Weapons, {std::begin(c_WeaponRuleNames), std::end(c_WeaponRuleNames)}, SelectedRule(true));
		menu.Heading("Movement", Kind::State);
		menu.Choices(Movement, {std::begin(c_MovementRuleNames), std::end(c_MovementRuleNames)}, SelectedRule(false), 2);
		menu.Heading("What your clicks do", Kind::Setting);
		menu.Choices(ClickMode, {std::begin(c_CommandModeNames), std::end(c_CommandModeNames)}, static_cast<int>(s_CommandMode), 3);
		menu.Heading("Group orders", Kind::Setting);
		menu.Choices(FormationPick, {std::begin(c_FormationNames), std::end(c_FormationNames)}, static_cast<int>(s_Formation));
		menu.Choices(KeepPacePick, {"Free", "Keep together"}, s_KeepPace ? 1 : 0);
		menu.Heading("Order markers", Kind::Setting);
		menu.Choices(MarkersPick, {"Off", "Selected", "All"}, g_SettingsMan.SandboxOrdersOverlay());
		menu.PlaceAbove(s_RingCenter);

		// An empty window over the panel, so the clicks on it are the menu's and not the world's.
		ImGui::SetNextWindowPos(menu.Min);
		ImGui::SetNextWindowSize(ImVec2(menu.Max.x - menu.Min.x, menu.Max.y - menu.Min.y));
		ImGui::Begin("##CommandMenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
		ImGui::End();
		int hover = ActionMenu::CellAt(menu.Cells, io.MousePos);
		ActionMenu::DrawMenu(menu, hover, scale);
		bool inside = io.MousePos.x >= menu.Min.x && io.MousePos.x < menu.Max.x && io.MousePos.y >= menu.Min.y && io.MousePos.y < menu.Max.y;

		// @return Whether the menu stays up: true for a setting.
		auto pick = [&](const ActionMenu::Cell& cell) {
			switch (cell.Action) {
				case ClickMode:
					s_CommandMode = static_cast<CommandMode>(cell.Value);
					if (s_CommandMode == CommandMode::Patrol) {
						s_PatrolDraft.clear(); // (A route of points clicked out, RC-4.)
					}
					g_GUISound.SlicePickedSound()->Play();
					return false;
				case Now:
					if (cell.Value == 2) {
						s_Selected.clear();
					} else if (cell.Value == 3) {
						QueueOrder(Order::BattleObjective);
					} else if (cell.Value == 4) {
						Stroke stroke;
						stroke.Kind = Tool::OrderSelected;
						stroke.Count = 122;
						s_Queue.push_back(stroke);
					} else {
						// Defend where they stand (Shift: as the last step of their plans, RC-3), or cancel their orders.
						Stroke stroke;
						stroke.Kind = Tool::OrderSelected;
						stroke.Position = s_RingScenePoint;
						stroke.Count = 100 + (cell.Value == 0 ? (io.KeyShift ? 13 : 3) : 2);
						s_Queue.push_back(stroke);
					}
					g_GUISound.SlicePickedSound()->Play();
					return false;
				case AIMode: {
					static const Order orders[] = {Order::Hold, Order::HuntBrains, Order::DigGold, Order::Rally, Order::Idle};
					Stroke stroke;
					stroke.Kind = Tool::OrderSelected;
					stroke.Position = s_RingScenePoint;
					stroke.Orders = orders[std::clamp(cell.Value, 0, 4)];
					s_Queue.push_back(stroke);
					break;
				}
				case Weapons:
				case Movement:
					QueueRule(cell.Action == Weapons, cell.Value);
					break;
				case FormationPick:
					s_Formation = static_cast<Formation>(cell.Value);
					// (A formation also puts the clicks to moving, as on the ring.)
					if (s_CommandMode != CommandMode::AttackMove) {
						s_CommandMode = CommandMode::Move;
					}
					break;
				case KeepPacePick:
					s_KeepPace = cell.Value != 0;
					break;
				case MarkersPick:
					g_SettingsMan.SetSandboxOrdersOverlay(cell.Value);
					break;
				default:
					return true;
			}
			if (!cell.Chosen) {
				g_GUISound.SelectionChangeSound()->Play();
			}
			return true;
		};

		if (!sticky) {
			// Held: a left click picks (a setting leaves it up); letting go picks what it is over and ends it, except a quick click, which
			// leaves it up.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hover >= 0) {
				if (!pick(menu.Cells[hover])) {
					s_RingOpen = false;
				}
				return;
			}
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				if (hover >= 0) {
					pick(menu.Cells[hover]);
					s_RingOpen = false;
				} else if (ImGui::GetTime() - openedAt < 0.3) {
					sticky = true;
				} else {
					s_RingOpen = false;
				}
			}
			return;
		}
		// Left up: a click on a row picks it; a click off the menu, a right click off it or Escape puts it away.
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (hover >= 0) {
				if (!pick(menu.Cells[hover])) {
					s_RingOpen = false;
				}
			} else if (!inside) {
				s_RingOpen = false;
			}
		} else if ((ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !inside) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			s_RingOpen = false;
		}
	}

	/// The rings the right button opens, by the tool in hand: the sides for anything made for a side, the commands for the command tool.
	void DrawSideRing() {
		ImGuiIO& io = ImGui::GetIO();
		Tool kind = CurrentTool().Kind;
		bool hasRing = TakesSide(kind) || kind == Tool::Command;
		if (!s_RingOpen) {
			// A right click on a numbered step of a selected unit's plan drops that step (RC-3) rather than opening the ring.
			if (kind == Tool::Command && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse) {
				for (const PlanMarker& marker: PlanMarkers()) {
					ImVec2 at = ToScreen(marker.Place);
					float dx = io.MousePos.x - at.x;
					float dy = io.MousePos.y - at.y;
					if (dx * dx + dy * dy <= 9.0F * 9.0F) {
						Stroke stroke;
						stroke.Kind = Tool::OrderSelected;
						stroke.Count = 400;
						stroke.UnitID = marker.UnitID;
						stroke.Choice = marker.Step;
						s_Queue.push_back(stroke);
						return;
					}
				}
			}
			// Not while you play a unit: the right button is its own then.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse && hasRing && !s_Possessed) {
				s_RingOpen = true;
				s_RingPage = 0;
				s_RingCenter = io.MousePos;
				s_RingScenePoint = MouseScenePosition();
			}
			return;
		}
		// The command tool's list in place of its rings (RC-12), unless the classic wheel is asked for.
		if (kind == Tool::Command && !g_SettingsMan.ClassicPieWheel()) {
			DrawCommandMenu();
			return;
		}
		if (kind == Tool::Command && s_RingPage == 1) {
			// The game's own AI modes for the units picked, as the pie menu offers them when playing a unit. Up until a click, since the button
			// that held the first ring open has been let go.
			// (With the two modes of RC-4, Defend at and Patrol, and the formations of RC-5.)
			static const std::vector<RingItem> modes = {{"Sentry", IM_COL32(242, 182, 61, 255), "Eye"}, {"Patrol", IM_COL32(120, 200, 220, 255), "Cycle"}, {"Hunt brains", IM_COL32(239, 106, 91, 255), "Brain"}, {"Dig for gold", IM_COL32(230, 200, 80, 255), "Dig"}, {"Rally point", IM_COL32(180, 140, 240, 255), "Flag"}, {"Do nothing", IM_COL32(150, 150, 140, 255), "Blank"}, {"Defend at", c_CommandModeColors[static_cast<int>(CommandMode::DefendAt)], "Reorient"}, {"Formation...", IM_COL32(110, 180, 250, 255), "SubPieMenu1"}, {"Back", IM_COL32(110, 180, 250, 255), "Return"}};
			static const Order orders[] = {Order::Hold, Order::Patrol, Order::HuntBrains, Order::DigGold, Order::Rally, Order::Idle};
			int picked = DrawRing(modes, -1, true);
			if (picked == -2) {
				return;
			}
			if (picked == 1) {
				// Patrol: a route of points clicked out, as the Patrol mode makes them (RC-4), rather than the game's own pacing to and fro.
				s_CommandMode = CommandMode::Patrol;
				s_PatrolDraft.clear();
			} else if (picked >= 0 && picked < 6) {
				Stroke stroke;
				stroke.Kind = Tool::OrderSelected;
				stroke.Position = s_RingScenePoint;
				stroke.Orders = orders[picked];
				s_Queue.push_back(stroke);
			} else if (picked == 6) {
				s_CommandMode = CommandMode::DefendAt;
			} else if (picked == 7) {
				s_RingOpen = true;
				s_RingPage = 5;
			} else if (picked == 8) {
				s_RingOpen = true;
				s_RingPage = 2;
			}
			return;
		}
		if (kind == Tool::Command && s_RingPage == 5) {
			// The formation for moves (RC-5), the one in use lit, and keeping together; up until a click. A formation also puts the clicks to moving.
			static std::vector<RingItem> formations;
			formations = {{c_FormationNames[0], IM_COL32(110, 180, 250, 255), "GoTo"}, {c_FormationNames[1], IM_COL32(110, 180, 250, 255), "Move"}, {c_FormationNames[2], IM_COL32(110, 180, 250, 255), "Cycle"}, {c_FormationNames[3], IM_COL32(110, 180, 250, 255), "Death"},
			              {s_KeepPace ? "Keep together: on" : "Keep together: off", IM_COL32(120, 220, 120, 255), "Follow"}, {"Back", IM_COL32(110, 180, 250, 255), "Return"}};
			int picked = DrawRing(formations, static_cast<int>(s_Formation), true);
			if (picked == -2) {
				return;
			}
			if (picked >= 0 && picked < static_cast<int>(Formation::Count)) {
				s_Formation = static_cast<Formation>(picked);
				if (s_CommandMode != CommandMode::AttackMove) {
					s_CommandMode = CommandMode::Move;
				}
			} else if (picked == 4) {
				s_KeepPace = !s_KeepPace;
			} else if (picked == 5) {
				s_RingOpen = true;
				s_RingPage = 1;
			}
			return;
		}
		if (kind == Tool::Command && (s_RingPage == 3 || s_RingPage == 4)) {
			// The engagement rules (RC-1) for the units picked: what they may shoot at (3), and how they move when they meet an enemy (4).
			// The one they all have is lit; up until a click.
			bool weapons = s_RingPage == 3;
			static const std::vector<RingItem> weaponRules = {{c_WeaponRuleNames[0], IM_COL32(239, 106, 91, 255), "Death"}, {c_WeaponRuleNames[1], IM_COL32(242, 182, 61, 255), "Reorient"}, {c_WeaponRuleNames[2], IM_COL32(150, 150, 140, 255), "Cancel"}, {"Back", IM_COL32(110, 180, 250, 255), "Return"}};
			static const std::vector<RingItem> movementRules = {{c_MovementRuleNames[0], IM_COL32(200, 200, 200, 255), "Cycle"}, {c_MovementRuleNames[1], IM_COL32(239, 106, 91, 255), "Move"}, {c_MovementRuleNames[2], IM_COL32(110, 180, 250, 255), "GoTo"}, {c_MovementRuleNames[3], IM_COL32(242, 182, 61, 255), "Flag"}, {"Back", IM_COL32(110, 180, 250, 255), "Return"}};
			const std::vector<RingItem>& rules = weapons ? weaponRules : movementRules;
			int picked = DrawRing(rules, SelectedRule(weapons), true);
			if (picked == -2) {
				return;
			}
			int count = static_cast<int>(rules.size());
			if (picked >= 0 && picked < count - 1) {
				QueueRule(weapons, picked);
			} else if (picked == count - 1) {
				s_RingOpen = true;
				s_RingPage = 2;
			}
			return;
		}
		if (kind == Tool::Command) {
			// (The two rules show what the units picked have, or "mixed".)
			auto ruleLabel = [](bool weapons) {
				static std::string labels[2];
				int rule = SelectedRule(weapons);
				std::string& label = labels[weapons ? 0 : 1];
				label = std::string(weapons ? "Weapons: " : "Movement: ") + (rule == -1 ? "mixed" : (rule < 0 ? "..." : (weapons ? c_WeaponRuleNames[rule] : c_MovementRuleNames[rule])));
				return label.c_str();
			};
			std::vector<RingItem> commands = {{"Move", IM_COL32(110, 180, 250, 255), "GoTo"}, {"Attack", IM_COL32(239, 106, 91, 255), "Death"}, {"Guard", IM_COL32(120, 220, 120, 255), "Follow"}, {"Attack-move", c_CommandModeColors[static_cast<int>(CommandMode::AttackMove)], "Speed"}, {"Defend", IM_COL32(242, 182, 61, 255), "Eye"}, {"Cancel", IM_COL32(200, 160, 120, 255), "Cancel"}, {"Deselect", IM_COL32(150, 150, 140, 255), "Remove"}, {ruleLabel(true), IM_COL32(242, 182, 61, 255), "Reload"}, {ruleLabel(false), IM_COL32(120, 220, 120, 255), "Move"}, {"Focus on objective", IM_COL32(180, 140, 240, 255), "Flag"}, {"More...", IM_COL32(200, 200, 200, 255), "SubPieMenu1"}};
			int picked = DrawRing(commands, static_cast<int>(s_CommandMode), s_RingPage == 2);
			if (picked == -2) {
				return;
			}
			if (picked >= 0 && picked <= 3) {
				// The mode for the clicks to come (the slices go in CommandMode's order).
				s_CommandMode = static_cast<CommandMode>(picked);
			} else if (picked == 4 || picked == 5) {
				// Defend where they stand (4), or cancel their orders (5).
				Stroke stroke;
				stroke.Kind = Tool::OrderSelected;
				stroke.Position = s_RingScenePoint;
				// (Defend with Shift held is the last step of their plans, RC-3.)
				stroke.Count = 100 + (picked == 4 ? (ImGui::GetIO().KeyShift ? 13 : 3) : 2);
				s_Queue.push_back(stroke);
			} else if (picked == 6) {
				s_Selected.clear();
			} else if (picked == 7 || picked == 8) {
				s_RingOpen = true;
				s_RingPage = picked == 7 ? 3 : 4;
			} else if (picked == 9) {
				// Their team's objective in the battle (the "Battle objective" order): a flag to take, a hill to hold, the place it defends.
				QueueOrder(Order::BattleObjective);
			} else if (picked == 10) {
				s_RingOpen = true;
				s_RingPage = 1;
			}
			return;
		}
		std::vector<RingItem> sides;
		static const char* teamIcons[] = {"Team1", "Team2", "Team3", "Team4"};
		for (int side = 0; side < c_Sides; ++side) {
			sides.push_back({c_SideNames[side], c_SideColors[side], side < 4 ? teamIcons[side] : nullptr});
		}
		int picked = DrawRing(sides, s_Team);
		if (picked >= 0) {
			s_Team = picked;
		}
	}

	ImVec2 ToScreen(const Vector& scenePosition) { return DebugDraw::ToScreen(scenePosition); }


	/// A label over each colony building: whose it is, what it is doing and how far along.
	void DrawColony() {
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		GameViewRect view = g_WindowMan.GetGameViewRect();
		float scale = ScenePixelsPerWindowPixel();
		// Kept to the picture of the game, so a label never lies over a tool panel.
		drawList->PushClipRect(ImVec2(view.x, view.y), ImVec2(view.x + view.w, view.y + view.h));
		for (const Colony::Building& building: Colony::Buildings()) {
			const Colony::Type& type = Colony::GetType(building.What);
			ImVec2 top = ToScreen(building.Ground + Vector(0.0F, -static_cast<float>(type.Height) - 6.0F));
			if (top.x < view.x - 100.0F || top.x > view.x + view.w + 100.0F || top.y < view.y || top.y > view.y + view.h + 40.0F) {
				continue;
			}
			std::string label = std::string(type.Name) + (building.What == Colony::Kind::Barracks ? "  " + std::to_string(building.Alive.size()) + "/" + std::to_string(building.KeepAlive) : "");
			ImVec2 size = ImGui::CalcTextSize(label.c_str());
			ImVec2 at(top.x - size.x * 0.5F, top.y - size.y - 6.0F);
			drawList->AddRectFilled(ImVec2(at.x - 4.0F, at.y - 2.0F), ImVec2(at.x + size.x + 4.0F, at.y + size.y + 2.0F), IM_COL32(0, 0, 0, 140), 3.0F);
			drawList->AddText(at, c_SideColors[building.Team], label.c_str());
			if (building.What == Colony::Kind::Barracks && building.Paid) {
				float barWidth = std::max(static_cast<float>(type.Width) / scale * 0.6F, 30.0F);
				ImVec2 barAt(top.x - barWidth * 0.5F, top.y - 3.0F);
				drawList->AddRectFilled(barAt, ImVec2(barAt.x + barWidth, barAt.y + 4.0F), IM_COL32(0, 0, 0, 160));
				drawList->AddRectFilled(barAt, ImVec2(barAt.x + barWidth * std::clamp(building.Progress, 0.0F, 1.0F), barAt.y + 4.0F), c_SideColors[building.Team]);
			}
		}
		drawList->PopClipRect();
	}

	/// The Colony tab of the sandbox window.
	void ColonyTab() {
		ImGui::TextWrapped("Buildings that work for a side. A barracks trains a unit, sends it out with its orders, and trains another whenever fewer than its number are alive. An extractor earns supply. They are built of concrete: wreck one and it stops.");
		ToolUI::Checkbox("Training is free", &Colony::Free());
		ImGui::SetItemTooltip("Off: a barracks pays for each unit from the supply of its side, which grows slowly by itself and faster with extractors.");
		if (!Colony::Free()) {
			for (int side = 0; side < c_Sides; ++side) {
				ImGui::PushID(side);
				ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4F);
				ImGui::DragFloat(c_SideNames[side], &Colony::Supply(side), 10.0F, 0.0F, 999999.0F, "%.0f supply");
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
		}
		ImGui::SeparatorText("Build");
		ToolButtons({Tool::Barracks, Tool::Extractor});
		Tool kind = CurrentTool().Kind;
		if (kind == Tool::Barracks || kind == Tool::Extractor) {
			SideChooser();
		}
		if (kind == Tool::Barracks) {
			ImGui::TextDisabled("It trains:");
			PresetList(Tool::Unit);
			UnitOrderCombo("Their orders");
			ImGui::SliderInt("Keeps this many alive", &s_ColonyKeep, 1, 20);
		}
		ImGui::SeparatorText("Standing");
		std::vector<Colony::Building>& buildings = Colony::Buildings();
		if (buildings.empty()) {
			ImGui::TextDisabled("Nothing built yet.");
		}
		int removeID = -1;
		for (Colony::Building& building: buildings) {
			ImGui::PushID(building.ID);
			const Colony::Type& type = Colony::GetType(building.What);
			ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[building.Team]);
			bool open = ImGui::TreeNode("##building", "%s %d (%s)", type.Name, building.ID, c_SideNames[building.Team]);
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::TextDisabled("%s", building.Status.c_str());
			if (open) {
				if (building.What == Colony::Kind::Barracks) {
					if (building.Paid) {
						ImGui::ProgressBar(building.Progress, ImVec2(-1.0F, 0.0F));
					}
					if (ImGui::BeginCombo("Trains", building.Unit.c_str(), ImGuiComboFlags_HeightLarge)) {
						for (const Preset& unit: s_Units) {
							if (ImGui::Selectable(unit.Label.c_str(), unit.PresetName == building.Unit)) {
								building.Unit = unit.PresetName;
							}
						}
						ImGui::EndCombo();
					}
					building.Orders = static_cast<int>(UnitOrder(building.Orders));
					ImGui::Combo("Their orders", &building.Orders, OrderName, nullptr, c_UnitOrderCount);
					ImGui::SliderInt("Keeps this many alive", &building.KeepAlive, 1, 20);
					ImGui::Text("%d alive, %d trained in all. One takes %.0f s%s.", static_cast<int>(building.Alive.size()), building.Produced, Colony::TrainingSeconds(std::max(Sandbox::UnitCost(building.Unit), 20.0F)),
					            Colony::Free() ? "" : (" and " + std::to_string(static_cast<int>(std::max(Sandbox::UnitCost(building.Unit), 20.0F))) + " supply").c_str());
				} else {
					ImGui::TextDisabled("%s", type.Description);
				}
				ToolUI::Checkbox("Stopped", &building.Paused);
				ImGui::SameLine();
				if (ToolUI::SmallButton("Look at it")) {
					s_FreeCamera = true;
					s_FollowTarget = UnitRef();
					s_FollowAction = false;
					s_CameraCenter = building.Ground + Vector(0.0F, -40.0F);
				}
				ImGui::SameLine();
				if (ToolUI::SmallButton("Close it down")) {
					removeID = building.ID;
				}
				ImGui::SetItemTooltip("It stops being a building. What it was built of stays standing.");
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (removeID >= 0) {
			Colony::Remove(removeID);
		}
	}

	/// Takes the selection arrow off the units that carry it.
	void UnmarkSelection() {
		for (const UnitRef& ref: s_MarkedSelected) {
			if (Actor* unit = GetRef(ref)) {
				unit->SetSandboxSelected(false);
			}
		}
		s_MarkedSelected.clear();
	}

	void DrawSelection() {
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		UnmarkSelection();
		for (const UnitRef& ref: s_Selected) {
			if (Actor* unit = GetRef(ref)) {
				unit->SetSandboxSelected(true);
				s_MarkedSelected.push_back(ref);
			}
		}
		// The patrol route being clicked out (RC-4): its points joined up, and on to the pointer.
		if (!s_PatrolDraft.empty() && s_CommandMode == CommandMode::Patrol && CurrentTool().Kind == Tool::Command) {
			ImU32 color = c_CommandModeColors[static_cast<int>(CommandMode::Patrol)];
			ImVec2 from = ToScreen(s_PatrolDraft.front());
			for (size_t i = 0; i < s_PatrolDraft.size(); ++i) {
				ImVec2 at = ToScreen(s_PatrolDraft[i]);
				if (i > 0) {
					drawList->AddLine(from, at, color, 2.0F);
				}
				drawList->AddCircleFilled(at, 5.0F, color);
				from = at;
			}
			drawList->AddLine(from, ImGui::GetIO().MousePos, (color & 0x00FFFFFF) | (110u << IM_COL32_A_SHIFT), 1.5F);
		}
		// The plans of the selected units (RC-3): a line from each unit through the step it is on and those still to come, with a numbered
		// marker at each queued step in its order's colour (a right click on one drops it).
		for (const UnitRef& ref: s_Selected) {
			const Actor* unit = GetRef(ref);
			auto plan = unit ? s_Plans.find(unit->GetUniqueID()) : s_Plans.end();
			if (plan == s_Plans.end() || plan->second.Steps.empty()) {
				continue;
			}
			auto placeOf = [](const PlanStep& step) {
				const Actor* target = GetRef(step.Target);
				return target ? target->GetPos() : step.Place;
			};
			ImVec2 from = ToScreen(unit->GetPos());
			if (plan->second.Running) {
				ImVec2 to = ToScreen(placeOf(plan->second.Current));
				drawList->AddLine(from, to, IM_COL32(255, 255, 255, 90), 1.5F);
				from = to;
			}
			int number = 1;
			for (const PlanStep& step: plan->second.Steps) {
				ImVec2 to = ToScreen(placeOf(step));
				ImU32 color = PlanColor(step.Kind);
				drawList->AddLine(from, to, (color & 0x00FFFFFF) | (150u << IM_COL32_A_SHIFT), 1.5F);
				drawList->AddCircleFilled(to, 8.0F, IM_COL32(0, 0, 0, 170));
				drawList->AddCircle(to, 8.0F, color, 0, 1.5F);
				std::string text = std::to_string(number++);
				ImVec2 size = ImGui::CalcTextSize(text.c_str());
				drawList->AddText(ImVec2(std::floor(to.x - size.x * 0.5F), std::floor(to.y - size.y * 0.5F)), IM_COL32(255, 255, 255, 255), text.c_str());
				from = to;
			}
		}
		// The engagement rules a selected unit has that aren't the usual (RC-1), in a small tag over it: HF hold fire, RF return fire, and
		// EN engage, MO move only, HG hold ground.
		for (const UnitRef& ref: s_Selected) {
			const Actor* unit = GetRef(ref);
			if (!unit || (unit->GetWeaponRule() == Actor::WEAPONS_AT_WILL && unit->GetMovementRule() == Actor::MOVE_FOLLOW_ORDER)) {
				continue;
			}
			static const char* weaponTags[] = {"", "RF", "HF"};
			static const char* movementTags[] = {"", "EN", "MO", "HG"};
			std::string tag = weaponTags[std::clamp(unit->GetWeaponRule(), 0, 2)];
			const char* movementTag = movementTags[std::clamp(unit->GetMovementRule(), 0, 3)];
			if (*movementTag) {
				tag += tag.empty() ? movementTag : std::string(" ") + movementTag;
			}
			ImVec2 size = ImGui::CalcTextSize(tag.c_str());
			ImVec2 at = ToScreen(unit->GetPos() - Vector(0.0F, unit->GetRadius() + 4.0F));
			ImVec2 corner(std::floor(at.x - size.x * 0.5F), std::floor(at.y - size.y));
			drawList->AddRectFilled(ImVec2(corner.x - 2.0F, corner.y - 1.0F), ImVec2(corner.x + size.x + 2.0F, corner.y + size.y + 1.0F), IM_COL32(0, 0, 0, 150), 2.0F);
			drawList->AddText(corner, unit->GetWeaponRule() == Actor::WEAPONS_HOLD ? IM_COL32(170, 170, 160, 255) : IM_COL32(242, 182, 61, 255), tag.c_str());
		}
		DrawOrderFeedback();
		// The control groups a unit is in (RC-6), as small numbers by its feet, in view only and with the command tool in hand.
		if (g_SettingsMan.ShowSandboxGroupBadges() && CurrentTool().Kind == Tool::Command) {
			std::unordered_map<long, std::string> badges;
			for (int number = 1; number <= 10; ++number) {
				for (const UnitRef& ref: s_Groups[number % 10]) {
					if (GetRef(ref)) {
						std::string& badge = badges[ref.ID];
						badge += badge.empty() ? std::to_string(number % 10) : "," + std::to_string(number % 10);
					}
				}
			}
			GameViewRect view = g_WindowMan.GetGameViewRect();
			for (const Actor* unit: SandboxAccess::Actors()) {
				auto badge = badges.find(static_cast<long>(unit->GetUniqueID()));
				if (badge == badges.end()) {
					continue;
				}
				ImVec2 at = ToScreen(unit->GetPos() + Vector(unit->GetRadius() * 0.6F, unit->GetRadius() * 0.5F));
				if (at.x < view.x || at.y < view.y || at.x > view.x + view.w || at.y > view.y + view.h) {
					continue;
				}
				ImVec2 size = ImGui::CalcTextSize(badge->second.c_str());
				drawList->AddRectFilled(ImVec2(at.x - 2.0F, at.y - 1.0F), ImVec2(at.x + size.x + 2.0F, at.y + size.y + 1.0F), IM_COL32(0, 0, 0, 160), 2.0F);
				drawList->AddText(at, c_SideColors[std::clamp(unit->GetTeam(), 0, c_Sides - 1)], badge->second.c_str());
			}
		}
		// (No line from each unit to where it is going: the game draws the route itself, as Routes on the command row has it.)
		// The marks of orders just given, fading.
		float seconds = ImGui::GetIO().DeltaTime;
		for (OrderMark& mark: s_OrderMarks) {
			mark.Life -= seconds;
			float size = 6.0F + (1.0F - mark.Life) * 10.0F;
			ImU32 color = (mark.Color & 0x00FFFFFF) | (static_cast<ImU32>(std::clamp(mark.Life, 0.0F, 1.0F) * 220.0F) << IM_COL32_A_SHIFT);
			drawList->AddCircle(ToScreen(mark.Position), size, color, 0, 2.0F);
		}
		s_OrderMarks.erase(std::remove_if(s_OrderMarks.begin(), s_OrderMarks.end(), [](const OrderMark& mark) { return mark.Life <= 0.0F; }), s_OrderMarks.end());
		if (s_CurrentTab == "Gym") {
			DrawGym(drawList);
		}
		if (const Actor* followed = GetRef(s_FollowTarget)) {
			ImVec2 at = ToScreen(followed->GetPos() - Vector(0.0F, followed->GetRadius() + 8.0F));
			drawList->AddTriangleFilled(ImVec2(at.x - 6.0F, at.y - 8.0F), ImVec2(at.x + 6.0F, at.y - 8.0F), ImVec2(at.x, at.y), IM_COL32(255, 255, 255, 230));
		}
	}

	/// Flags marking each side's rally point.
	void DrawRallyPoints() {
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		for (int side = 0; side < c_Sides; ++side) {
			if (!s_RallySet[side]) {
				continue;
			}
			Vector onScreen = FromCamera(s_RallyPoints[side]);
			ImVec2 base(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
			drawList->AddLine(base, ImVec2(base.x, base.y - 26.0F), IM_COL32(230, 230, 230, 220), 2.0F);
			drawList->AddTriangleFilled(ImVec2(base.x, base.y - 26.0F), ImVec2(base.x + 16.0F, base.y - 21.0F), ImVec2(base.x, base.y - 16.0F), c_SideColors[side]);
		}
	}

	void SideStatus() {
		// The fighting units each side has, as the Battle Director counts them (Sandbox::CountUnits): not brains or craft, but a craft's passengers.
		// ("Red 7" was one brain, one dropship and five soldiers.)
		std::array<int, c_Sides> counts{};
		for (int side = 0; side < c_Sides; ++side) {
			counts[side] = Sandbox::CountUnits(side);
		}
		for (int side = 0; side < c_Sides; ++side) {
			if (side > 0) {
				ImGui::SameLine();
			}
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c_SideColors[side]), "%s %d", c_SideNames[side], counts[side]);
		}
		// The order labels overlay adds how each Battle Director team stands.
		if (g_SettingsMan.ShowOrderLabels()) {
			long long now = g_TimerMan.GetSimUpdateCount();
			float perSecond = 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F);
			for (int side = 0; side < c_Sides; ++side) {
				const BattleTeam& team = s_BattleTeams[side];
				if (!team.Running) {
					continue;
				}
				std::string wave = team.Broke ? std::string("broke") : "next ships " + std::to_string(static_cast<int>(static_cast<float>(std::max(0LL, team.NextWave - now)) / perSecond)) + "s";
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c_SideColors[side]), "%s: spent %.0f, sent %d, %s", c_SideNames[side], team.Spent, team.Sent, wave.c_str());
			}
		}
	}
	/// How fast time runs, the AI's pause, and in the Sandbox game mode whether the world stands still while the window is open.
	void TimeControls() {
		bool aiPaused = Controller::IsAIPaused();
		float timeScale = g_TimerMan.GetTimeScale();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45F);
		if (ImGui::SliderFloat("Speed of time", &timeScale, 0.05F, 3.0F, "%.2fx")) {
			g_TimerMan.SetTimeScale(timeScale);
		}
		for (const auto& [label, scale]: {std::pair<const char*, float>{"Slow", 0.25F}, {"Normal", 1.0F}, {"Fast", 2.0F}}) {
			ImGui::SameLine();
			if (ToolUI::SmallButton(label)) {
				g_TimerMan.SetTimeScale(scale);
			}
		}
		ImGui::PushStyleColor(ImGuiCol_Text, aiPaused ? IM_COL32(255, 210, 80, 255) : ImGui::GetColorU32(ImGuiCol_Text));
		if (ToolUI::Checkbox("Pause AI (set things up, then let them loose)", &aiPaused)) {
			Controller::SetAIPaused(aiPaused);
		}
		ImGui::PopStyleColor();
		if (Sandbox::IsGodMode()) {
			ToolUI::Checkbox("The world stands still while this window is open", &s_PauseInMenus);
			ImGui::SetItemTooltip("On: time stops while this window is open and starts when it is put away (Tab) or you go and play (P). What you do with a tool still happens at once.\nOff: the world carries on while you work.");
			if (s_PausedByMenus) {
				ImGui::SameLine();
				if (ToolUI::SmallButton("Step")) {
					s_StepsWanted += 1;
				}
				ImGui::SetItemTooltip("Lets the world move one update, a sixtieth of a second. Hold Ctrl and click for a second's worth.");
				if (ImGui::IsItemDeactivated() && ImGui::GetIO().KeyCtrl) {
					s_StepsWanted += 59;
				}
			}
		}
		// Reset: starts the game over on the same map, once the player has said they are sure.
		if (ToolUI::SmallButton("Reset")) {
			ImGui::OpenPopup("Reset the map?##sandboxReset");
		}
		ImGui::SetItemTooltip("Throws away everything made here and loads the current map afresh.");
		if (ImGui::BeginPopupModal("Reset the map?##sandboxReset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("Are you sure? Everything on the map is lost and it loads afresh.");
			if (ToolUI::SmallButton("Yes, reset")) {
				g_ActivityMan.SetRestartActivity();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ToolUI::SmallButton("Cancel")) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		if (ToolUI::SmallButton("Clear...")) {
			ImGui::OpenPopup("Clear the map##sandboxClear");
			ScanMapMaterials();
		}
		ImGui::SetItemTooltip("Takes one kind of thing off the whole map: the buildings, liquids (some or all), units, or kinds of ground (some or all).");
		ClearMapPopup();
	}

	/// The materials on the map, each with how many pixels of it there are, for the Clear window's lists. Taken when the window opens.
	struct MapMaterial {
		int ID;
		std::string Name;
		int Pixels;
		bool Liquid;
	};
	std::vector<MapMaterial> s_MapMaterials;
	std::set<int> s_ClearPicked; //!< The liquids or kinds of ground ticked in the Clear window.
	int s_ClearKind = 0; //!< Which ClearKind the Clear window is set to.
	int s_ClearSide = -1; //!< The side whose units go, -1 for every side.
	bool s_ClearBuildingMaterials = true; //!< Buildings: what they were built of goes too.
	bool s_ClearSprings = true; //!< Liquids: the springs that pour them go too.
	bool s_ClearAsking = false; //!< The Clear window is asking whether the player is sure.

	void ScanMapMaterials() {
		s_MapMaterials.clear();
		s_ClearPicked.clear();
		s_ClearAsking = false;
		Scene* scene = g_SceneMan.GetScene();
		if (!scene || !scene->GetTerrain()) {
			return;
		}
		const BITMAP* materials = scene->GetTerrain()->GetMaterialBitmap();
		std::array<int, 256> counts{};
		for (int y = 0; y < materials->h; ++y) {
			for (int x = 0; x < materials->w; ++x) {
				++counts[materials->line[y][x]];
			}
		}
		for (int id = 0; id < 256; ++id) {
			if (counts[id] == 0 || id == g_MaterialAir || id == g_MaterialOutOfBounds) {
				continue;
			}
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			s_MapMaterials.push_back({id, material ? material->GetPresetName() : "Material " + std::to_string(id), counts[id], FluidSim::IsLiquid(id)});
		}
		std::sort(s_MapMaterials.begin(), s_MapMaterials.end(), [](const MapMaterial& a, const MapMaterial& b) { return a.Pixels > b.Pixels; });
	}

	/// The Clear window: what to clear, then whether the player is sure (as Reset asks).
	void ClearMapPopup() {
		ImGui::SetNextWindowSizeConstraints(ImVec2(ToolUI::Pixel() * 260.0F, 0.0F), ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y * 0.8F));
		if (!ImGui::BeginPopupModal("Clear the map##sandboxClear", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		const ClearKind kind = static_cast<ClearKind>(s_ClearKind);
		const bool listed = kind == ClearKind::Liquids || kind == ClearKind::Ground;
		auto pickedNames = [listed]() {
			std::string names;
			if (!listed) {
				return names;
			}
			for (const MapMaterial& material: s_MapMaterials) {
				if (s_ClearPicked.count(material.ID)) {
					names += (names.empty() ? "" : ", ") + material.Name;
				}
			}
			return names;
		};
		if (!s_ClearAsking) {
			for (auto [label, choice]: {std::pair{"Buildings", ClearKind::Buildings}, std::pair{"Liquids", ClearKind::Liquids}, std::pair{"Units", ClearKind::Units}, std::pair{"Ground", ClearKind::Ground}}) {
				if (choice != ClearKind::Buildings) {
					ImGui::SameLine();
				}
				if (ToolUI::RadioButton(label, &s_ClearKind, static_cast<int>(choice))) {
					s_ClearPicked.clear();
				}
			}
			ImGui::Separator();
			switch (kind) {
				case ClearKind::Buildings:
					ImGui::TextUnformatted("Every door and bunker part, and the colony buildings.");
					ToolUI::Checkbox("And what they're built of", &s_ClearBuildingMaterials);
					ImGui::SetItemTooltip("Every pixel of concrete, metal, glass and bunker material on the map, the built things on the Boom tab included.");
					break;
				case ClearKind::Units:
					if (ImGui::BeginCombo("Whose", s_ClearSide < 0 ? "Every side" : c_SideNames[s_ClearSide])) {
						if (ImGui::Selectable("Every side", s_ClearSide < 0)) {
							s_ClearSide = -1;
						}
						for (int side = 0; side < c_Sides; ++side) {
							if (ImGui::Selectable(c_SideNames[side], s_ClearSide == side)) {
								s_ClearSide = side;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::TextDisabled("Craft and brains go too; doors and your character stay.");
					break;
				case ClearKind::Liquids:
				case ClearKind::Ground: {
					bool liquids = kind == ClearKind::Liquids;
					int shown = 0;
					for (const MapMaterial& material: s_MapMaterials) {
						shown += material.Liquid == liquids ? 1 : 0;
					}
					if (shown == 0) {
						ImGui::TextDisabled(liquids ? "There is no liquid on the map." : "There is no ground on the map.");
						break;
					}
					if (ToolUI::SmallButton("All")) {
						for (const MapMaterial& material: s_MapMaterials) {
							if (material.Liquid == liquids) {
								s_ClearPicked.insert(material.ID);
							}
						}
					}
					ImGui::SameLine();
					if (ToolUI::SmallButton("None")) {
						s_ClearPicked.clear();
					}
					if (ImGui::BeginChild("##clearKinds", ImVec2(0.0F, ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(std::min(shown, 12) + 1)), ImGuiChildFlags_Borders)) {
						for (const MapMaterial& material: s_MapMaterials) {
							if (material.Liquid != liquids) {
								continue;
							}
							bool picked = s_ClearPicked.count(material.ID) != 0;
							std::string label = material.Name + "  (" + std::to_string(material.Pixels) + " pixels)##" + std::to_string(material.ID);
							if (ToolUI::Checkbox(label.c_str(), &picked)) {
								if (picked) {
									s_ClearPicked.insert(material.ID);
								} else {
									s_ClearPicked.erase(material.ID);
								}
							}
						}
					}
					ImGui::EndChild();
					if (liquids) {
						ToolUI::Checkbox("And the springs that pour them", &s_ClearSprings);
					}
					break;
				}
			}
			ImGui::Separator();
			ImGui::BeginDisabled(listed && s_ClearPicked.empty());
			if (ToolUI::SmallButton("Clear")) {
				s_ClearAsking = true;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ToolUI::SmallButton("Cancel")) {
				ImGui::CloseCurrentPopup();
			}
		} else {
			std::string what;
			switch (kind) {
				case ClearKind::Buildings:
					what = s_ClearBuildingMaterials ? "every door, bunker part and colony building, and all the concrete, metal and glass on the map" : "every door, bunker part and colony building";
					break;
				case ClearKind::Liquids:
					what = "all the " + pickedNames() + (s_ClearSprings ? ", and the springs that pour it" : "");
					break;
				case ClearKind::Units:
					what = s_ClearSide < 0 ? "every side's units" : std::string("the ") + c_SideNames[s_ClearSide] + " side's units";
					break;
				case ClearKind::Ground:
					what = "all the " + pickedNames();
					break;
			}
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ToolUI::Pixel() * 320.0F);
			ImGui::TextWrapped("Are you sure? This takes %s off the map, and it can't be undone.", what.c_str());
			ImGui::PopTextWrapPos();
			if (ToolUI::SmallButton("Yes, clear")) {
				Stroke stroke;
				stroke.Kind = Tool::ClearMap;
				stroke.Count = s_ClearKind;
				stroke.Team = s_ClearSide;
				stroke.Choice = (kind == ClearKind::Buildings && s_ClearBuildingMaterials) || (kind == ClearKind::Liquids && s_ClearSprings) ? 1 : 0;
				stroke.Materials.assign(s_ClearPicked.begin(), s_ClearPicked.end());
				s_Queue.push_back(stroke);
				s_ClearAsking = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ToolUI::SmallButton("Back")) {
				s_ClearAsking = false;
			}
		}
		ImGui::EndPopup();
	}


	/// A thin upright gold rule between groups of tiles on the bar; none where the bar's width has put the next group on a line of its own.
	void BarDivider() {
		float lineY = ImGui::GetCursorPosY();
		ImGui::SameLine(0.0F, ToolUI::Pixel() * 4.0F);
		if (ImGui::GetCursorPosY() >= lineY) {
			return;
		}
		ImVec2 at = ImGui::GetCursorScreenPos();
		float height = ToolUI::Pixel() * 24.0F;
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + ToolUI::Pixel()), ImVec2(at.x + ToolUI::Pixel(), at.y + height - ToolUI::Pixel()), IM_COL32(170, 128, 48, 200));
		ImGui::Dummy(ImVec2(ToolUI::Pixel(), height));
		ImGui::SameLine(0.0F, ToolUI::Pixel() * 4.0F);
	}

	/// The bar's plate: an olive slab with cut corners and a gold line round it, drawn behind the window's contents.
	void BarPlate() {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 at = ImGui::GetWindowPos();
		ImVec2 size = ImGui::GetWindowSize();
		ImVec2 to(at.x + size.x, at.y + size.y);
		float pixel = ToolUI::Pixel();
		float cut = pixel * 5.0F;
		auto shape = [&](float grow) {
			drawList->PathClear();
			drawList->PathLineTo(ImVec2(at.x - grow + cut, at.y - grow));
			drawList->PathLineTo(ImVec2(to.x + grow - cut, at.y - grow));
			drawList->PathLineTo(ImVec2(to.x + grow, at.y - grow + cut));
			drawList->PathLineTo(ImVec2(to.x + grow, to.y + grow - cut));
			drawList->PathLineTo(ImVec2(to.x + grow - cut, to.y + grow));
			drawList->PathLineTo(ImVec2(at.x - grow + cut, to.y + grow));
			drawList->PathLineTo(ImVec2(at.x - grow, to.y + grow - cut));
			drawList->PathLineTo(ImVec2(at.x - grow, at.y - grow + cut));
		};
		shape(pixel * 2.0F);
		drawList->PathFillConvex((ToolTheme::EdgeDark & 0x00FFFFFF) | (230u << IM_COL32_A_SHIFT));
		shape(0.0F);
		drawList->PathFillConvex((ToolTheme::Panel & 0x00FFFFFF) | (245u << IM_COL32_A_SHIFT));
		shape(pixel);
		drawList->PathStroke(ToolTheme::Edge, ImDrawFlags_Closed, pixel);
		// A faint lighter band along the top, as a lip.
		drawList->AddRectFilled(ImVec2(at.x + cut, at.y + pixel), ImVec2(to.x - cut, at.y + pixel * 2.0F), (ToolTheme::EdgeLight & 0x00FFFFFF) | (60u << IM_COL32_A_SHIFT));
		// Studs in the corners.
		for (ImVec2 corner: {ImVec2(at.x + cut, at.y + cut), ImVec2(to.x - cut, at.y + cut), ImVec2(at.x + cut, to.y - cut), ImVec2(to.x - cut, to.y - cut)}) {
			drawList->AddRectFilled(ImVec2(corner.x - pixel, corner.y - pixel), ImVec2(corner.x + pixel, corner.y + pixel), (ToolTheme::Gold & 0x00FFFFFF) | (160u << IM_COL32_A_SHIFT));
		}
	}

	/// The settings of the tool in hand, in a row at the top of the bar: brush size for painting, squad size and orders for units, and so on.
	/// Returns whether anything was shown.
	bool ContextRow() {
		const ToolInfo& tool = CurrentTool();
		float pixel = ToolUI::Pixel();
		float field = ImGui::GetFontSize() * 9.0F;
		bool shown = false;
		auto start = [&](const char* what) {
			ImGui::TextDisabled("%s", what);
			ImGui::SameLine(0.0F, pixel * 4.0F);
			shown = true;
		};
		if (tool.UsesRadius) {
			start(tool.Name);
			ImGui::SetNextItemWidth(field);
			ImGui::SliderInt("##brush", &s_Radius, 1, c_MaxBrushRadius, "Brush %d px", ImGuiSliderFlags_Logarithmic);
			ImGui::SameLine();
			for (const auto& [label, size]: {std::pair<const char*, int>{"S", 4}, {"M", 10}, {"L", 24}, {"XL", 60}}) {
				if (ToolUI::SmallButton(label)) {
					s_Radius = size;
				}
				ImGui::SameLine();
			}
			if (IsTerrainBrush(tool.Kind)) {
				// Circle, Square, Spray, round and round.
				static const char* const shapes[] = {"Circle", "Square", "Spray"};
				if (ToolUI::SmallButton(shapes[static_cast<int>(s_BrushShape)])) {
					s_BrushShape = static_cast<BrushShape>((static_cast<int>(s_BrushShape) + 1) % 3);
				}
			}
			ImGui::NewLine();
		} else if (tool.Kind == Tool::Unit || tool.Kind == Tool::Drop) {
			const Preset* preset = ChosenPreset(tool.Kind, ChoiceFor(tool.Kind));
			std::string randomName = RandomSourceName(s_RandomFavourites, s_RandomFaction);
			start(s_RandomUnits ? randomName.c_str() : preset ? preset->PresetName.c_str() : tool.Name);
			ImGui::SetNextItemWidth(field * 0.7F);
			ImGui::SliderInt("##squad", &s_SquadSize, 1, 10, "Squad of %d");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(field);
			UnitOrderCombo("##orders");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(field);
			LoadoutChooser("##loadout");
			if (tool.Kind == Tool::Drop) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(field * 0.7F);
				ImGui::Combo("##craft", &s_Craft, "Dropship\0Rocket\0");
			}
		} else if (tool.Kind == Tool::Structure) {
			const Preset* preset = ChosenPreset(Tool::Structure, s_StructureChoice);
			start(preset ? preset->PresetName.c_str() : tool.Name);
			ToolUI::Checkbox("Snap to the bunker grid", &s_SnapToGrid);
		} else if (tool.Kind == Tool::Item) {
			const Preset* preset = ChosenPreset(Tool::Item, s_ItemChoice);
			start(preset ? preset->PresetName.c_str() : tool.Name);
			ToolUI::Checkbox("Pull the pin (grenades)", &s_LitGrenade);
		} else if (tool.Kind == Tool::Barracks) {
			start(tool.Name);
			ImGui::SetNextItemWidth(field);
			ImGui::SliderInt("##keep", &s_ColonyKeep, 1, 20, "Keeps %d alive");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(field);
			UnitOrderCombo("##orders");
		} else if (tool.Kind == Tool::Effect) {
			start(tool.Name);
			ImGui::SetNextItemWidth(field * 1.4F);
			if (ImGui::BeginCombo("##effect", c_Effects[std::clamp(s_EffectChoice, 0, static_cast<int>(EffectKind::Count) - 1)].Name)) {
				for (int i = 0; i < static_cast<int>(EffectKind::Count); ++i) {
					if (ImGui::Selectable(c_Effects[i].Name, i == s_EffectChoice)) {
						s_EffectChoice = i;
					}
				}
				ImGui::EndCombo();
			}
		} else if (tool.Kind == Tool::OrderMove || tool.Kind == Tool::RallyPoint || tool.Kind == Tool::Brain) {
			start(tool.Name);
			for (int side = 0; side < c_Sides; ++side) {
				if (side > 0) {
					ImGui::SameLine();
				}
				ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
				ToolUI::RadioButton(c_SideNames[side], &s_Team, side);
				ImGui::PopStyleColor();
			}
		} else if (tool.Kind == Tool::Command) {
			start(tool.Name);
			float rowStart = ImGui::GetCursorPosX();
			// The mode of the clicks, in its colours.
			for (int mode = 0; mode < static_cast<int>(std::size(c_CommandModeNames)); ++mode) {
				if (mode > 0) {
					ImGui::SameLine();
				}
				ImGui::PushStyleColor(ImGuiCol_Text, c_CommandModeColors[mode]);
				int current = static_cast<int>(s_CommandMode);
				if (ToolUI::RadioButton(c_CommandModeNames[mode], &current, mode)) {
					s_CommandMode = static_cast<CommandMode>(current);
				}
				ImGui::PopStyleColor();
				// (Its key, RC-6; all of them are on the Keys page.)
				static const char* keys[] = {"M", "T", "G", "F", "B", "R"};
				ImGui::SetItemTooltip("Key: %s", keys[std::min<size_t>(static_cast<size_t>(mode), std::size(keys) - 1)]);
			}
			// The patrol route being clicked out (RC-4): started as a loop or back and forth once it has two points.
			if (s_CommandMode == CommandMode::Patrol) {
				ImGui::SameLine(0.0F, pixel * 6.0F);
				ImGui::TextDisabled("%d points", static_cast<int>(s_PatrolDraft.size()));
				for (int backAndForth = 0; backAndForth < 2; ++backAndForth) {
					ImGui::SameLine();
					ImGui::BeginDisabled(s_PatrolDraft.size() < 2 || s_Selected.empty());
					if (ToolUI::SmallButton(backAndForth ? "Back and forth" : "Loop")) {
						Stroke stroke;
						stroke.Kind = Tool::Command;
						stroke.Count = backAndForth ? 21 : 20;
						stroke.Position = s_PatrolDraft.front();
						stroke.Points = s_PatrolDraft;
						s_Queue.push_back(stroke);
						s_PatrolDraft.clear();
					}
					ImGui::EndDisabled();
				}
				ImGui::SetItemTooltip("Loop: round the points and back to the first, again and again.\nBack and forth: along the points to the last, then back the same way.\nThey stop a few seconds at each point and fight whatever they meet on the way.");
				ImGui::SameLine();
				ImGui::BeginDisabled(s_PatrolDraft.empty());
				if (ToolUI::SmallButton("Clear##patrol")) {
					s_PatrolDraft.clear();
				}
				ImGui::EndDisabled();
			}
			// How a move or attack-move puts them when they get there (RC-5), and whether they keep together on the way.
			if (s_CommandMode == CommandMode::Move || s_CommandMode == CommandMode::AttackMove) {
				ImGui::SameLine(0.0F, pixel * 6.0F);
				ImGui::SetNextItemWidth(field * 0.7F);
				FormationCombo("##formation");
				ImGui::SameLine();
				ToolUI::Checkbox("Keep together", &s_KeepPace);
				ImGui::SetItemTooltip("On: units sent together walk at the pace of the slowest of them till they get there, so the fast ones don't arrive alone.");
			}
			// The zone a defend or guard holds, as a Battle Director team's defend place (its card's same three settings).
			if (s_CommandMode == CommandMode::DefendAt || s_CommandMode == CommandMode::Guard) {
				ImGui::SameLine(0.0F, pixel * 6.0F);
				ImGui::SetNextItemWidth(field * 0.7F);
				ImGui::SliderInt("##defendRadius", &s_DefendRadius, 30, 600, "Zone %d px");
				ImGui::SetItemTooltip("Defend (and guarding a craft, crate or building): how far round the place the zone reaches.\nThey stand inside it and go after any enemy that comes into it.");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(field * 0.7F);
				ImGui::SliderInt("##defendChase", &s_DefendChase, 0, 1500, "Chase %d px");
				ImGui::SetItemTooltip("How much further than the zone they go after an enemy, before coming back to their posts.");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(field * 0.6F);
				ImGui::SliderInt("##defendRoam", &s_DefendRoam, 0, 100, "Roam %d%%");
				ImGui::SetItemTooltip("The share of them that walk about the zone from spot to spot, rather than holding a post.");
			}
			ImGui::SameLine(0.0F, pixel * 6.0F);
			// What is drawn of orders as they play out (RC-7) and of control groups (RC-6), kept in the settings.
			if (ToolUI::SmallButton("Show...")) {
				ImGui::OpenPopup("##commandShow");
			}
			ImGui::SetItemTooltip("Order marks over units, \"no route\" markers, under-fire pings, group numbers and the map.");
			if (ImGui::BeginPopup("##commandShow")) {
				ImGui::TextDisabled("Order marks over units");
				int glyphs = g_SettingsMan.SandboxOrderGlyphs();
				static const char* glyphNames[] = {"None", "Selected", "All"};
				for (int which = 0; which < 3; ++which) {
					if (which > 0) {
						ImGui::SameLine();
					}
					if (ToolUI::RadioButton(glyphNames[which], &glyphs, which)) {
						g_SettingsMan.SetSandboxOrderGlyphs(glyphs);
					}
				}
				ImGui::SetItemTooltip("A mark over each unit for what it was told: an arrow for a move (barred for attack-move), a crosshair for an attack,\na wedge for a guard, a flag for a post, a ring for a patrol, a square for a hold. Selected units also get a faint line to where they're going.");
				bool pings = g_SettingsMan.ShowSandboxAttackPings();
				if (ToolUI::Checkbox("Under-fire pings", &pings)) {
					g_SettingsMan.SetShowSandboxAttackPings(pings);
				}
				ImGui::SetItemTooltip("When a unit of the selection's side is hurt: a ring where it is, or an arrow at the edge of the picture pointing the way.");
				bool tags = g_SettingsMan.ShowUnitTags();
				if (ToolUI::Checkbox("Side and health", &tags)) {
					g_SettingsMan.SetShowUnitTags(tags);
				}
				ImGui::SetItemTooltip("Each unit's team icon and health number beside it, for every side.");
				bool badges = g_SettingsMan.ShowSandboxGroupBadges();
				if (ToolUI::Checkbox("Group numbers", &badges)) {
					g_SettingsMan.SetShowSandboxGroupBadges(badges);
				}
				bool map = g_SettingsMan.ShowSandboxMinimap();
				if (ToolUI::Checkbox("Map  (N)", &map)) {
					g_SettingsMan.SetShowSandboxMinimap(map);
				}
				ImGui::SetItemTooltip("The whole scene small, with every unit, the view, pings and \"no route\" crosses.\nClick: look there. Drag: select. Right click: the selected units' order there, as the mode says (Shift: add it to their plans).");
				ImGui::TextDisabled("A \"no route\" cross shows where units couldn't get to; click it to send them again.");
				ImGui::EndPopup();
			}
			// A second row for the selected units, so the bar doesn't stretch across the picture: who they are, and what can be done
			// with them and how they are set.
			// What is selected, by kind.
			std::map<std::string, int> kinds;
			int alive = 0;
			for (const UnitRef& ref: s_Selected) {
				if (const Actor* unit = GetRef(ref)) {
					++kinds[unit->GetPresetName()];
					++alive;
				}
			}
			std::string what = alive == 0 ? "Nothing selected" : std::to_string(alive) + " selected:";
			for (const auto& [name, number]: kinds) {
				what += " " + std::to_string(number) + " " + name + ",";
			}
			if (!kinds.empty()) {
				what.pop_back();
			}
			ImGui::SetCursorPosX(rowStart);
			ImGui::TextDisabled("%s", what.c_str());
			ImGui::SameLine();
			ImGui::BeginDisabled(alive == 0);
			if (ToolUI::SmallButton("Deselect")) {
				s_Selected.clear();
			}
			ImGui::EndDisabled();
			// Their plans (RC-3), if any have steps still to come: cleared, each carrying on with the step it is on.
			ImGui::SameLine();
			ImGui::BeginDisabled(PlanMarkers().empty());
			if (ToolUI::SmallButton("Clear plans")) {
				Stroke stroke;
				stroke.Kind = Tool::OrderSelected;
				stroke.Count = 120;
				s_Queue.push_back(stroke);
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Shift with any order adds it to the selected units' plans: they carry out each when the one before is over\n(a move when they get there, an attack when the enemy is dead). Defend with Shift held ends the plan holding ground.\nA right click on a numbered marker drops that step.");
			ImGui::SameLine();
			ImGui::BeginDisabled(alive == 0);
			if (ToolUI::SmallButton("Clear all orders")) {
				Stroke stroke;
				stroke.Kind = Tool::OrderSelected;
				stroke.Count = 121;
				s_Queue.push_back(stroke);
			}
			ImGui::SetItemTooltip("Every order the selected units have, forgotten: where they were going, what they were after, what they defend or guard,\ntheir plans and patrols, a battle mode's job for them. They stand where they are and fight back from there.\n(Cancel instead puts them back on their side's standing orders.)");
			ImGui::EndDisabled();
			// Handing them back (RC-9's commander, or anyone): to the battle, or else the side's orders.
			ImGui::SameLine();
			ImGui::BeginDisabled(alive == 0);
			if (ToolUI::SmallButton("Follow team orders")) {
				Stroke stroke;
				stroke.Kind = Tool::OrderSelected;
				stroke.Count = 122;
				s_Queue.push_back(stroke);
			}
			ImGui::SetItemTooltip("Hand the selected units back: everything you told them forgotten, and they take up their team's orders again.\nIn a battle mode's game, its job for them (and its AI commander's, where the team has one); with the Battle Director\ndefending a place for their team, a post there; else the side's orders as set in the Orders list.");
			ImGui::SameLine();
			if (ToolUI::SmallButton("Focus on objective")) {
				QueueOrder(Order::BattleObjective);
			}
			ImGui::SetItemTooltip("Send the selected units after their team's objective in the battle: an enemy flag, an enemy VIP, the hill or the\nobjective in play, or the place their Battle Director card defends; with none, they attack. (Key: O)");
			// The Orders tab's list, for the selected units rather than a whole side: the same choice, kept in step with the tab.
			ImGui::SameLine();
			s_Order = std::clamp(s_Order, 0, c_OrderCount - 1);
			ImGui::SetNextItemWidth(field * 0.9F);
			ImGui::Combo("##selectedOrders", &s_Order, OrderName, nullptr, c_OrderCount);
			ImGui::SetItemTooltip("Orders for the selected units, as the Orders tab gives a whole side.");
			ImGui::SameLine();
			const bool moveTo = static_cast<Order>(s_Order) == Order::MoveTo;
			if (ToolUI::SmallButton(moveTo ? "Click where##giveSelected" : "Give orders##giveSelected")) {
				if (moveTo) {
					// (A move needs a place: the clicks are put to moving.)
					s_CommandMode = CommandMode::Move;
				} else {
					QueueOrder(static_cast<Order>(s_Order));
				}
			}
			ImGui::SetItemTooltip(moveTo ? "Move to a place: click on the map where the selected units should go (the command tool's Move)." : "Give the selected units the order in the list.");
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(alive == 0);
			if (ToolUI::SmallButton("Follow")) {
				s_FollowTarget = s_Selected.empty() ? UnitRef() : s_Selected.front();
				s_FollowAction = false;
			}
			ImGui::EndDisabled();
			// The engagement rules of what is selected (RC-1): the one they share, or "mixed"; a choice gives it to them all.
			ImGui::BeginDisabled(alive == 0);
			for (bool weapons: {true, false}) {
				ImGui::SameLine(0.0F, pixel * 6.0F);
				int rule = SelectedRule(weapons);
				const char* const* names = weapons ? c_WeaponRuleNames : c_MovementRuleNames;
				int ruleCount = weapons ? static_cast<int>(std::size(c_WeaponRuleNames)) : static_cast<int>(std::size(c_MovementRuleNames));
				ImGui::SetNextItemWidth(field * 0.75F);
				if (ImGui::BeginCombo(weapons ? "##weaponRule" : "##movementRule", rule == -1 ? "Mixed" : (rule < 0 ? (weapons ? "Weapons" : "Movement") : names[rule]))) {
					for (int choice = 0; choice < ruleCount; ++choice) {
						if (ImGui::Selectable(names[choice], choice == rule)) {
							QueueRule(weapons, choice);
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("%s", weapons ? "What the selected units may shoot at.\nFire at will: any enemy they see. Return fire: only while they are being shot at. Hold fire: never; they aim, and open up the moment this changes.\nKept until changed." : "How the selected units move when they meet an enemy.\nAs ordered: a move keeps walking, an attack closes in, a post is held. Engage: stop and fight, closing in. Move only: keep going, firing on the way. Hold ground: fight from where they stand.\nEach new order goes back to As ordered.");
			}
			ImGui::EndDisabled();
			ImGui::SameLine(0.0F, pixel * 6.0F);
			// Whose routes are drawn: the game's own AI path drawing, as the settings have it.
			ImGui::TextDisabled("Routes");
			ImGui::SameLine();
			{
				int paths = Actor::ShowAIPaths();
				static const char* routeNames[] = {"Never", "Always", "Selected"};
				for (int mode = 0; mode < 3; ++mode) {
					if (mode > 0) {
						ImGui::SameLine();
					}
					int shown = (mode + 1) % 3; // Shown in the order Always, Selected, Never.
					if (ToolUI::RadioButton(routeNames[shown], &paths, shown)) {
						Actor::SetShowAIPaths(paths);
					}
				}
			}
			ImGui::SameLine(0.0F, pixel * 6.0F);
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderFloat("##spacing", &s_Spacing, 8.0F, 60.0F, "Spacing %.0f px");
			ImGui::SetItemTooltip("How far apart units stand when sent somewhere together.\nDrag a box to select; Shift+click adds a unit, and Shift with any order adds it to their plans (RC-3); double click takes all of a kind in sight; Ctrl+A everyone on the side.\nCtrl+number keeps the selection, the number brings it back. Hold the right button over the world for the ring.");
		}
		return shown;
	}

	/// The sandbox's bar along the bottom of the picture, in the Sandbox game mode while you're above it all: the main tools, the parts of the sandbox window to
	/// open, and the things you've pinned. It is there whether the window is open or not.
	void DrawBar() {
		// In the middle of the picture, and it stays there: opening a panel doesn't shove it along.
		GameViewRect view = g_WindowMan.GetGameViewRect();
		const ImGuiStyle& style = ImGui::GetStyle();
		float pixel = ToolUI::Pixel();
		struct Part {
			const char* Name;
			Icon Art;
			ImU32 Color;
			const char* Tip;
		};
		static const Part parts[] = {
		    {"Spawn", Icon::Person, ToolTheme::Text, "Spawn: units, squads dropped from orbit, brains and items"},
		    {"Build", Icon::Wall, IM_COL32(170, 170, 165, 255), "Build: bunker pieces, placed straight into the world"},
		    {"Paint", Icon::Drop, IM_COL32(90, 170, 240, 255), "Paint: fire, liquids, smoke, loose and solid ground"},
		    {"Boom", Icon::Bomb, IM_COL32(239, 106, 91, 255), "Boom: blasts, strikes from the sky, and things to knock down"},
		    {"Effects", Icon::Star, IM_COL32(255, 220, 120, 255), "Effects: lights and particle effects to put down"},
		    {"Orders", Icon::Flag, IM_COL32(242, 182, 61, 255), "Orders: orders for whole sides"},
		    {"Battle", Icon::Rocket, IM_COL32(239, 106, 91, 255), "Battle: teams that keep dropping in waves to fight, attack or defend"},
		    {"World", Icon::Cloud, IM_COL32(190, 190, 190, 255), "World: time, weather, the speed of the world, the camera"},
		    {"You", Icon::Person, IM_COL32(130, 220, 120, 255), "You: your own character, what it is, carries and can do"},
		};
		static const Tool mainTools[] = {Tool::None, Tool::Command, Tool::Follow, Tool::Possess, Tool::Remove, Tool::RallyPoint};

		ImGui::SetNextWindowPos(ImVec2(view.x + view.w * 0.5F, view.y + view.h - pixel * 6.0F), ImGuiCond_Always, ImVec2(0.5F, 1.0F));
		// A fixed share of the picture's width, and as tall as its contents: the tiles and controls go onto more lines to fit (new ones too, wherever they are added).
		ImGui::SetNextWindowSize(ImVec2(std::floor(view.w * g_DebugMan.GetBarWidthShare()), 0.0F), ImGuiCond_Always);
		ImGui::PushWrapSameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(8.0F, 8.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pixel * 8.0F, pixel * 5.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pixel * 2.0F, pixel * 3.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
		if (ImGui::Begin("##SandboxBar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
			BarPlate();
			// The settings of the tool in hand, in a row of their own at the top.
			if (ContextRow()) {
				ImVec2 at = ImGui::GetCursorScreenPos();
				float width = ImGui::GetContentRegionAvail().x;
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + pixel), ImVec2(at.x + width, at.y + pixel * 2.0F), (ToolTheme::Edge & 0x00FFFFFF) | (160u << IM_COL32_A_SHIFT));
				ImGui::Dummy(ImVec2(width, pixel * 3.0F));
			}
			// What you've pinned, in a row of its own above the rest.
			int unpin = -1;
			if (!s_Pins.empty()) {
				ImGui::TextDisabled("Pinned");
				ImGui::SameLine(0.0F, pixel * 4.0F);
			}
			for (size_t i = 0; i < s_Pins.size(); ++i) {
				const Pin& pin = s_Pins[i];
				int toolIndex = ToolIndex(pin.Kind);
				const std::vector<Preset>& list = ListFor(pin.Kind);
				int presetIndex = -1;
				if (!pin.PresetName.empty()) {
					for (size_t j = 0; j < list.size(); ++j) {
						if (list[j].PresetName == pin.PresetName) {
							presetIndex = static_cast<int>(j);
							break;
						}
					}
				}
				if (i > 0) {
					ImGui::SameLine();
				}
				ImGui::PushID(static_cast<int>(i) + 1000);
				bool inHand = s_ToolIndex == toolIndex && (pin.PresetName.empty() || ChoiceFor(pin.Kind) == presetIndex);
				std::string tip = (pin.PresetName.empty() ? std::string(c_Tools[toolIndex].Name) : pin.PresetName + "  (" + c_Tools[toolIndex].Name + ")") + "\nRight click: take it off the bar";
				int clicked = BarTile("##pin", tip.c_str(), inHand, [&](ImDrawList* drawList, ImVec2 at, float room) {
					const PiecePicture* picture = presetIndex >= 0 ? &PictureOf(list[presetIndex]) : nullptr;
					if (picture && picture->Width > 0) {
						float fit = std::min(room / static_cast<float>(picture->Width), room / static_cast<float>(picture->Height));
						if (fit >= 1.0F) {
							fit = std::floor(fit);
						}
						ImVec2 size(static_cast<float>(picture->Width) * fit, static_cast<float>(picture->Height) * fit);
						ImVec2 corner(std::floor(at.x + (room - size.x) * 0.5F), std::floor(at.y + (room - size.y) * 0.5F));
						drawList->AddImage(static_cast<ImTextureID>(picture->Texture), corner, ImVec2(corner.x + size.x, corner.y + size.y));
					} else {
						ToolLook look = LookOf(pin.Kind);
						DrawIcon(drawList, look.Art, at, room / 12.0F, look.Color);
					}
				});
				if (clicked == 1) {
					s_ToolIndex = toolIndex;
					if (presetIndex >= 0) {
						ChoiceFor(pin.Kind) = presetIndex;
					}
				} else if (clicked == 2) {
					unpin = static_cast<int>(i);
				}
				ImGui::PopID();
			}
			if (unpin >= 0) {
				s_Pins.erase(s_Pins.begin() + unpin);
				SavePinsFile();
			}
			if (!s_Pins.empty()) {
				// A gold rule between the pins and the rest.
				ImVec2 at = ImGui::GetCursorScreenPos();
				float width = ImGui::GetContentRegionAvail().x;
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + pixel), ImVec2(at.x + width, at.y + pixel * 2.0F), (ToolTheme::Edge & 0x00FFFFFF) | (160u << IM_COL32_A_SHIFT));
				ImGui::Dummy(ImVec2(width, pixel * 3.0F));
			}

			// Into your character.
			if (s_Player.EnterOnClose) {
				if (BarTile("##play", "Play: step into your own character (P). Shift+P puts it down where the mouse points first.", false, [&](ImDrawList* drawList, ImVec2 at, float room) { DrawIcon(drawList, Icon::Person, at, room / 12.0F, IM_COL32(130, 220, 120, 255)); }) == 1) {
					Sandbox::TogglePlay(false);
				}
				BarDivider();
			}
			// The main tools.
			for (size_t i = 0; i < std::size(mainTools); ++i) {
				int index = ToolIndex(mainTools[i]);
				ToolLook look = LookOf(mainTools[i]);
				ImGui::PushID(index);
				if (i > 0) {
					ImGui::SameLine();
				}
				if (BarTile("##main", c_Tools[index].Name, s_ToolIndex == index, [&](ImDrawList* drawList, ImVec2 at, float room) { DrawIcon(drawList, look.Art, at, room / 12.0F, look.Color); }) == 1) {
					s_ToolIndex = index;
				}
				ImGui::PopID();
			}
			BarDivider();
			// The side things are made for, in its colour: a click goes round the sides.
			{
				std::string tip = std::string("Side: ") + c_SideNames[s_Team] + ". Click to go to the next side; or hold the right button over the world with a unit in hand for the ring of sides.";
				if (BarTile("##side", tip.c_str(), false, [&](ImDrawList* drawList, ImVec2 at, float room) {
					    float inset = room * 0.2F;
					    drawList->AddRectFilled(ImVec2(at.x + inset, at.y + inset), ImVec2(at.x + room - inset, at.y + room - inset), c_SideColors[s_Team]);
					    drawList->AddRect(ImVec2(at.x + inset, at.y + inset), ImVec2(at.x + room - inset, at.y + room - inset), ToolTheme::EdgeDark, 0.0F, 0, pixel);
				    }) == 1) {
					s_Team = (s_Team + 1) % c_Sides;
				}
			}
			BarDivider();
			// The parts of the sandbox window: a click opens the window on that part, and a click on the one showing puts the window away.
			for (size_t i = 0; i < std::size(parts); ++i) {
				const Part& part = parts[i];
				if (std::string(part.Name) == "You" && !Sandbox::IsGodMode()) {
					continue;
				}
				bool showing = Sandbox::IsOpen() && s_CurrentTab == part.Name;
				ImGui::PushID(static_cast<int>(i) + 500);
				if (i > 0) {
					ImGui::SameLine();
				}
				if (BarTile("##part", part.Tip, showing, [&](ImDrawList* drawList, ImVec2 at, float room) { DrawIcon(drawList, part.Art, at, room / 12.0F, part.Color); }) == 1) {
					if (showing) {
						Sandbox::SetOpen(false);
					} else {
						Sandbox::SetOpen(true);
						s_WantedTab = part.Name;
						s_CurrentTab = part.Name;
						// Whatever was last picked on that part comes back to hand with it; the first time, the part's first tool.
						static const std::map<std::string, Tool> firstTools = {{"Spawn", Tool::Unit}, {"Build", Tool::Structure}, {"Paint", Tool::Fire}, {"Boom", Tool::Grenade}, {"Effects", Tool::Effect}, {"Orders", Tool::Command}, {"Battle", Tool::None}, {"You", Tool::PlayCharacter}};
						if (auto remembered = s_LastToolOfTab.find(part.Name); remembered != s_LastToolOfTab.end()) {
							s_ToolIndex = remembered->second;
						} else if (auto first = firstTools.find(part.Name); first != firstTools.end()) {
							s_ToolIndex = ToolIndex(first->second);
						}
					}
				}
				ImGui::PopID();
			}
		}
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(4);
		ImGui::PopWrapSameLine();
	}
} // namespace SandboxDetail
