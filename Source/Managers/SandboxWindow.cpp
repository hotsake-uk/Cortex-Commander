// The sandbox window, bar, rings and cursor.

#include "SandboxInternal.h"
#include "ActionMenu.h"
#include "GUISound.h"
#include "TerrainCandle.h"

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
		// Held in where the view stops at a hard edge, so panning back moves the view straight away.
		s_CameraCenter = g_CameraMan.ClampScrollCenter(s_CameraCenter, 0);
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
		if (faction == -2) {
			return "Random non-combatants";
		}
		return "Random units";
	}

	bool RandomSourceCombo(const char* label, bool& favouritesOnly, int& faction) {
		bool changed = false;
		if (faction >= static_cast<int>(s_FactionNames.size())) {
			faction = -1;
		}
		std::string shown = favouritesOnly ? "Favourites" : faction >= 0 ? s_FactionNames[faction] : faction == -2 ? "Animals and civilians" : "All factions";
		if (ImGui::BeginCombo(label, shown.c_str(), ImGuiComboFlags_HeightLarge)) {
			if (ImGui::Selectable("All factions", !favouritesOnly && faction == -1)) {
				favouritesOnly = false;
				faction = -1;
				changed = true;
			}
			if (ImGui::Selectable("Favourites", favouritesOnly)) {
				favouritesOnly = true;
				faction = -1;
				changed = true;
			}
			if (ImGui::Selectable("Animals and civilians", !favouritesOnly && faction == -2)) {
				favouritesOnly = false;
				faction = -2;
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
		ImGui::SetItemTooltip("Where random units come from: every faction (soldiers only), only the units marked as favourites (Ctrl+click on a tile; with none marked, every unit), only the animals and civilians (non-combatants), or one faction.");
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
		if (kind == Tool::CollapseArea && !TerrainCollapse::IsEnabled()) {
			return "Collapsing terrain is off (F6 > Falling ground): nothing falls.";
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
			case Tool::ForceBlast:
				return "A burst of force: throws units, dropped things, debris and smoke out from the point, with no fire and no blast damage (a hard landing still hurts).";
			case Tool::HugeForceBlast:
				return "The same, far wider and harder. Clears a whole area.";
			case Tool::Implosion:
				return "The reverse: pulls everything loose around the point in towards it. No harm done.";
			case Tool::Updraft:
				return "A column of air that lifts units, things and debris up over the point.";
			case Tool::GustRight:
				return "A gale across the point to the right: blows units, things, debris and smoke along.";
			case Tool::GustLeft:
				return "A gale across the point to the left: blows units, things, debris and smoke along.";
			case Tool::SmokeBomb:
				return "A thick cloud of smoke and no blast (gas needs to be on in F6 for it to hang about).";
			case Tool::Fireworks:
				return "Bursts of coloured sparks in the air above the point. They light up the sky and harm nothing.";
			case Tool::BuildTank:
				return "An open concrete tank, filled with what the springs pour (Paint > Springs).";
			case Tool::TreeTrunk:
				return "Wood, darker, like a tree's trunk. Burns like wood.";
			case Tool::DenseEarth:
				return "The base game's dense earth: darker and tougher to dig than earth.";
			case Tool::GoldEarth:
				return "Earth with flecks of gold in it, as the base game's maps have, for units to dig out.";
			case Tool::Plants:
				return "Drag along the ground to put down rows of the game's own plants, as its maps have them, as far apart as Plant spacing says.";
			case Tool::Cacti:
				return "Drag along the ground to put down rows of the game's own cacti, big and small.";
			case Tool::Mushrooms:
				return "Drag along the ground to put down the game's own red and yellow mushrooms, mostly small ones.";
			case Tool::GrowGrass:
				return "Brush over the ground to grow grass on top of it, a few pixels thick, as the game's own maps have on their topsoil. Grows only up into the air, and only where there's no grass yet.";
			case Tool::Trees:
				return "Drag along the ground to plant big trees: leafy, pine, tall and autumn ones, their trunks of tree trunk (wood without it) and their leaves of vegetation, so they burn and can be cut down.";
			case Tool::Candles:
				return "Drag along the ground to put down candles: tapers, pillars and stubs in white, ivory, red and beeswax. Set one alight with fire (the Fire brush, a flame, burning grass beside it) and it burns like a real one: a small steady flame that lights up round it, the wax melting down from the top and running down the sides, until it's burnt down. Water, a strong wind, a blast or rain in the open puts it out; light it again and it carries on.";
			case Tool::CollapseArea:
				return "Drag out a box on the world: all the ground in it breaks loose and falls, rock, earth, sand, wood, buildings and all (not doors). Each piece lands as its material does: concrete and glass shatter, earth and stone crack, sand crumbles, wood splinters, metal bends. What only the box held up comes down too. A big box falls as rubble. Shift keeps it square, Escape drops it, Ctrl+Z puts it all back. Up to 800 px either way.";
			case Tool::TerrainOther:
				return "Paints the terrain chosen under \"More terrain...\": the base game's ground (topsoil, bedrock, red and lunar earth, snow, metal, ...).";
			case Tool::Rope:
				return "Click to put down a rope of the kind picked under Ropes: each click is a point it's tied at, to the unit, thing or loose falling piece of ground clicked (which it then follows and pulls on), else the ground there; a click in the air leaves it loose there. Right click, Enter or Escape finishes it. It swings and sags, is pulled taut by what's tied to it, and can be cut by bullets and blasts or burnt (the kinds that burn). Ctrl+Z takes the whole rope away.";
			case Tool::RopeCut:
				return "Click on a rope to cut it there.";
			case Tool::Metal:
				return "Paints the metal chosen under Metals: the bunkers' plating, or gold, silver, bronze, brass, copper and chrome, which catch the sun and lamplight in their own colour.";
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
			case Tool::Generator:
				return {Icon::Bolt, IM_COL32(250, 230, 90, 255)};
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
			case Tool::PourOther: {
				const Material* material = g_SceneMan.GetMaterial(s_OtherPourable);
				return {material && FluidSim::IsLiquid(material->GetIndex()) ? Icon::Drop : Icon::Grains, s_OtherPourable.empty() ? IM_COL32(200, 180, 150, 255) : MaterialMarkColor(s_OtherPourable, 255)};
			}
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
			case Tool::TreeTrunk:
				return {Icon::Chunk, IM_COL32(95, 65, 40, 255)};
			case Tool::Concrete:
				return {Icon::Chunk, IM_COL32(170, 170, 165, 255)};
			case Tool::Stone:
				return {Icon::Chunk, IM_COL32(135, 130, 125, 255)};
			case Tool::DenseEarth:
				return {Icon::Chunk, IM_COL32(105, 70, 45, 255)};
			case Tool::GoldEarth:
				return {Icon::Chunk, IM_COL32(230, 190, 60, 255)};
			case Tool::TerrainOther:
				return {Icon::Chunk, MaterialMarkColor(s_OtherTerrain, 255)};
			case Tool::CollapseArea:
				return {Icon::Down, IM_COL32(242, 150, 60, 255)};
			case Tool::Rope: {
				const RopeSim::TypeInfo& type = RopeSim::GetType(s_RopeType);
				return {Icon::Rope, IM_COL32(type.R, type.G, type.B, 255)};
			}
			case Tool::RopeCut:
				return {Icon::Cross, IM_COL32(230, 120, 100, 255)};
			case Tool::Metal:
				for (const PaintMetal& metal: c_PaintMetals) {
					if (s_PaintMetal == metal.Material) {
						return {Icon::Chunk, IM_COL32(metal.R, metal.G, metal.B, 255)};
					}
				}
				return {Icon::Chunk, IM_COL32(175, 189, 199, 255)};
			case Tool::Plants:
				return {Icon::Plant, IM_COL32(110, 190, 80, 255)};
			case Tool::Cacti:
				return {Icon::Plant, IM_COL32(150, 190, 90, 255)};
			case Tool::Mushrooms:
				return {Icon::Plant, IM_COL32(230, 90, 70, 255)};
			case Tool::Trees:
				return {Icon::Plant, IM_COL32(70, 140, 60, 255)};
			case Tool::GrowGrass:
				return {Icon::Plant, IM_COL32(140, 210, 80, 255)};
			case Tool::Candles:
				return {Icon::Candle, IM_COL32(232, 226, 205, 255)};
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
			case Tool::Decor:
				return {Icon::Candle, IM_COL32(255, 200, 120, 255)};
			case Tool::ForceBlast:
				return {Icon::Star, IM_COL32(160, 215, 255, 255)};
			case Tool::HugeForceBlast:
				return {Icon::Star, IM_COL32(110, 160, 255, 255)};
			case Tool::Implosion:
				return {Icon::Target, IM_COL32(190, 140, 255, 255)};
			case Tool::Updraft:
				return {Icon::Arrows, IM_COL32(160, 235, 220, 255)};
			case Tool::GustRight:
			case Tool::GustLeft:
				return {Icon::Cloud, IM_COL32(200, 225, 240, 255)};
			case Tool::SmokeBomb:
				return {Icon::Cloud, IM_COL32(140, 140, 150, 255)};
			case Tool::Fireworks:
				return {Icon::Star, IM_COL32(255, 120, 200, 255)};
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

	/// How many tiles go to a row, and how wide each is (into width): about the given size, whatever the window's width, so a wider window
	/// (the large view) fits more of them to a row rather than making them bigger. Never fewer than the given count, as in a narrow panel.
	int TileColumns(float tile, int fewest, float gap, float& width) {
		float room = ImGui::GetContentRegionAvail().x;
		const int perRow = std::max(fewest, static_cast<int>((room + gap) / (tile + gap)));
		width = std::floor((room - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow));
		return perRow;
	}

	/// The tools to pick from, as a row of tiles: each its picture with its name under it, the one in hand lit up.
	/// The Effects tab's maker: the effects you have made, and the controls of the one picked, layer by layer. Changes are kept in Userdata/SandboxEffects.txt as soon as no control is being dragged.
	void CustomEffectsUI() {
		LoadCustomEffects();
		const int builtIn = static_cast<int>(EffectKind::Count);
		ImGui::SeparatorText("Make your own");
		ImGui::TextWrapped("An effect is a stack of layers (lights, sparks, smoke, gas, forces...) that all run at once. Start from one below, change its layers, and click in the world to put it down. What you make is kept for next time.");
		static int startFrom = 0;
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
		if (ImGui::BeginCombo("##start", EffectTemplateName(startFrom))) {
			for (int i = 0; i < c_EffectTemplateCount; ++i) {
				if (ImGui::Selectable(EffectTemplateName(i), i == startFrom)) {
					startFrom = i;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ToolUI::Button("Make a new effect")) {
			s_CustomEffects.push_back(EffectTemplate(startFrom));
			s_EffectChoice = builtIn + static_cast<int>(s_CustomEffects.size()) - 1;
			TookTool(ToolIndex(Tool::Effect));
			s_CustomEffectsDirty = true;
		}
		ImGui::SetItemTooltip("Adds an effect made of a few layers to start from. Pick it, then click in the world.");
		if (s_CustomEffects.empty()) {
			ImGui::TextDisabled("None made yet.");
		}
		int column = 0;
		for (size_t i = 0; i < s_CustomEffects.size(); ++i) {
			if (column++ % 3 != 0) {
				ImGui::SameLine();
			}
			const int choice = builtIn + static_cast<int>(i);
			ImGui::PushID(static_cast<int>(i) + 5000);
			if (ToolUI::RadioButton(s_CustomEffects[i].Name.c_str(), c_Tools[s_ToolIndex].Kind == Tool::Effect && s_EffectChoice == choice)) {
				s_EffectChoice = choice;
				TookTool(ToolIndex(Tool::Effect));
			}
			ImGui::PopID();
		}
		const int selected = s_EffectChoice - builtIn;
		if (selected >= 0 && selected < static_cast<int>(s_CustomEffects.size())) {
			CustomEffect& effect = s_CustomEffects[static_cast<size_t>(selected)];
			ImGui::Separator();
			char name[64];
			std::snprintf(name, sizeof(name), "%s", effect.Name.c_str());
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0F);
			if (ImGui::InputText("Name", name, sizeof(name))) {
				effect.Name = name;
				s_CustomEffectsDirty = true;
			}
			ImGui::SameLine();
			if (ToolUI::SmallButton("Copy")) {
				CustomEffect copy = effect;
				copy.Name += " copy";
				s_CustomEffects.push_back(copy);
				s_EffectChoice = builtIn + static_cast<int>(s_CustomEffects.size()) - 1;
				s_CustomEffectsDirty = true;
				return;
			}
			ImGui::SameLine();
			if (ToolUI::SmallButton("Delete")) {
				s_CustomEffects.erase(s_CustomEffects.begin() + selected);
				s_EffectChoice = 0;
				s_CustomEffectsDirty = true;
				return;
			}
			ImGui::SetItemTooltip("Deletes this effect from the list (those already put down in the world carry on).");
			int removeLayer = -1;
			for (size_t li = 0; li < effect.Layers.size(); ++li) {
				EffectLayer& layer = effect.Layers[li];
				const LayerInfo& info = c_Layers[static_cast<int>(layer.Kind)];
				ImGui::PushID(static_cast<int>(li));
				const bool open = ImGui::CollapsingHeader((std::string(info.Name) + "###layer").c_str(), ImGuiTreeNodeFlags_DefaultOpen);
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 0.5F);
				if (ToolUI::SmallButton("x")) {
					removeLayer = static_cast<int>(li);
				}
				ImGui::SetItemTooltip("Takes this layer out.");
				if (open) {
					bool changed = false;
					const bool emits = (info.Uses & LcRate) != 0 && (info.Uses & LcSpeed) != 0 && layer.Kind != LayerKind::Flames;
					const bool isLight = layer.Kind == LayerKind::Light || layer.Kind == LayerKind::Spotlight;
					if (info.Uses & LcSize) {
						const char* label = isLight || layer.Kind == LayerKind::Shimmer || layer.Kind == LayerKind::Force || layer.Kind == LayerKind::Shockwave ? "Reach (px)" : "Width (px)";
						changed |= ImGui::SliderFloat("Size", &layer.Size, 1.0F, 600.0F, (std::string(label) + " %.0f").c_str(), ImGuiSliderFlags_Logarithmic);
					}
					if (info.Uses & LcRate) {
						const char* label = layer.Kind == LayerKind::Shockwave ? "Pulses a second %.1f" : (layer.Kind == LayerKind::Lightning ? "Strikes a second %.2f" : "A second %.1f");
						changed |= ImGui::SliderFloat("Rate", &layer.Rate, layer.Kind == LayerKind::Lightning ? 0.02F : 0.1F, layer.Kind == LayerKind::Shockwave || layer.Kind == LayerKind::Lightning ? 4.0F : 300.0F, label, ImGuiSliderFlags_Logarithmic);
					}
					if (info.Uses & LcSpeed) {
						changed |= ImGui::SliderFloat(layer.Kind == LayerKind::Force ? "Strength" : "Speed", &layer.Speed, 0.0F, 30.0F, "%.1f");
					}
					if (info.Uses & LcSpread) {
						changed |= ImGui::SliderFloat(layer.Kind == LayerKind::Spotlight ? "Beam width" : "Spread", &layer.Spread, 0.0F, 1.0F, "%.2f");
					}
					if (info.Uses & LcAngle) {
						changed |= ImGui::SliderFloat("Direction", &layer.Angle, 0.0F, 360.0F, "%.0f deg (0 up, 90 right)");
					}
					if (info.Uses & LcSpin) {
						changed |= ImGui::SliderFloat("Turns", &layer.Spin, -360.0F, 360.0F, "%.0f deg a second");
					}
					if (info.Uses & LcIntensity) {
						changed |= ImGui::SliderFloat(isLight ? "Brightness" : "Amount", &layer.Intensity, 0.0F, 6.0F, "%.2f");
					}
					if (info.Uses & LcFlicker) {
						changed |= ImGui::SliderFloat("Flicker", &layer.Flicker, 0.0F, 1.0F, "%.2f");
					}
					if (info.Uses & LcPulse) {
						changed |= ImGui::SliderFloat("Pulse", &layer.Pulse, 0.0F, 8.0F, "%.2f a second");
					}
					if (info.Uses & LcOption) {
						const char* gases[] = {"Smoke", "Toxic gas", "Methane", "Steam"};
						const char* forces[] = {"Blows along its direction", "Pushes out", "Pulls in", "Lifts"};
						const bool gas = layer.Kind == LayerKind::Gas;
						layer.Option = std::clamp(layer.Option, 0, 3);
						changed |= ImGui::Combo(gas ? "Gas" : "Force", &layer.Option, gas ? gases : forces, 4);
					}
					if (info.Uses & LcColour) {
						if (emits) {
							changed |= ImGui::Checkbox("Own colour", &layer.OwnColour);
						}
						if (isLight || layer.OwnColour) {
							changed |= ImGui::ColorEdit3("Colour", layer.Colour, ImGuiColorEditFlags_NoInputs);
						}
					}
					if (info.Uses & LcOffset) {
						changed |= ImGui::SliderFloat("Across", &layer.OffsetX, -300.0F, 300.0F, "%.0f px");
						changed |= ImGui::SliderFloat("Up/down", &layer.OffsetY, -300.0F, 300.0F, "%.0f px");
					}
					if (changed) {
						s_CustomEffectsDirty = true;
					}
				}
				ImGui::PopID();
			}
			if (removeLayer >= 0) {
				effect.Layers.erase(effect.Layers.begin() + removeLayer);
				s_CustomEffectsDirty = true;
			}
			static int newLayer = 0;
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0F);
			if (ImGui::BeginCombo("##layerkind", c_Layers[newLayer].Name)) {
				for (int i = 0; i < static_cast<int>(LayerKind::Count); ++i) {
					if (ImGui::Selectable(c_Layers[i].Name, i == newLayer)) {
						newLayer = i;
					}
					ImGui::SetItemTooltip("%s", c_Layers[i].Tip);
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ToolUI::Button("Add layer") && effect.Layers.size() < 24) {
				EffectLayer layer;
				layer.Kind = static_cast<LayerKind>(newLayer);
				effect.Layers.push_back(layer);
				s_CustomEffectsDirty = true;
			}
			ImGui::SetItemTooltip("%s", c_Layers[newLayer].Tip);
		}
		// Written once no control is being dragged, not on every step of a slider.
		if (s_CustomEffectsDirty && !ImGui::IsAnyItemActive()) {
			SaveCustomEffects();
		}
	}

	void ToolButtons(std::initializer_list<Tool> tools) {
		const ImGuiStyle& style = ImGui::GetStyle();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		float pixel = ToolUI::Pixel() * 2.0F;
		// About the size they are four to a row in the side panel; the metal and material tiles are the same size.
		float gap = style.ItemSpacing.x * 0.5F;
		float width = 0.0F;
		const int perRow = TileColumns(ImGui::GetFontSize() * 6.5F, 4, gap, width);
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

	void MetalButtons() {
		const ImGuiStyle& style = ImGui::GetStyle();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		float pixel = ToolUI::Pixel() * 2.0F;
		float gap = style.ItemSpacing.x * 0.5F;
		float width = 0.0F;
		const int perRow = TileColumns(ImGui::GetFontSize() * 6.5F, 4, gap, width);
		float pad = pixel * 2.0F;
		float height = pad + pixel * 12.0F + ImGui::GetTextLineHeight() * 2.0F + pad;
		int toolIndex = ToolIndex(Tool::Metal);
		int column = 0;
		for (const PaintMetal& metal: c_PaintMetals) {
			const Material* material = g_SceneMan.GetMaterial(metal.Material);
			if (!material || material->GetIndex() == g_MaterialAir) {
				continue;
			}
			if (column++ % perRow != 0) {
				ImGui::SameLine(0.0F, gap);
			}
			ImGui::PushID(metal.Material);
			ImVec2 at = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##metal", ImVec2(width, height))) {
				s_PaintMetal = metal.Material;
				TookTool(toolIndex);
			}
			ImGui::SetItemTooltip("%s", metal.About);
			bool hovered = ImGui::IsItemHovered();
			bool selected = s_ToolIndex == toolIndex && s_PaintMetal == metal.Material;
			ImVec2 to(at.x + width, at.y + height);
			drawList->AddRectFilled(at, to, ImGui::GetColorU32(selected ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
			drawList->AddRect(at, to, ImGui::GetColorU32(selected ? ImGuiCol_SliderGrab : ImGuiCol_Border), 0.0F, 0, selected ? ToolUI::Pixel() * 2.0F : ToolUI::Pixel());
			DrawIcon(drawList, Icon::Chunk, ImVec2(std::floor(at.x + (width - pixel * 12.0F) * 0.5F), at.y + pad), pixel, IM_COL32(metal.R, metal.G, metal.B, 255));
			float wrap = width - pad;
			ImVec2 nameSize = ImGui::CalcTextSize(metal.Name, nullptr, false, wrap);
			ImGui::PushClipRect(at, to, true);
			drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(std::floor(at.x + std::max((width - nameSize.x) * 0.5F, pad * 0.5F)), at.y + pad + pixel * 12.0F + ToolUI::Pixel()), ImGui::GetColorU32(selected ? ImGuiCol_SliderGrab : ImGuiCol_Text), metal.Name, nullptr, wrap);
			ImGui::PopClipRect();
			ImGui::PopID();
		}
	}

	void MaterialButtons(Tool kind, std::string& chosen, const std::vector<std::string>& names) {
		// (Drawn as the metals' buttons are.)
		const ImGuiStyle& style = ImGui::GetStyle();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		float pixel = ToolUI::Pixel() * 2.0F;
		float gap = style.ItemSpacing.x * 0.5F;
		float width = 0.0F;
		const int perRow = TileColumns(ImGui::GetFontSize() * 6.5F, 4, gap, width);
		float pad = pixel * 2.0F;
		float height = pad + pixel * 12.0F + ImGui::GetTextLineHeight() * 2.0F + pad;
		int toolIndex = ToolIndex(kind);
		int column = 0;
		for (const std::string& name: names) {
			const Material* material = g_SceneMan.GetMaterial(name);
			if (!material || material->GetIndex() == g_MaterialAir) {
				continue;
			}
			if (column++ % perRow != 0) {
				ImGui::SameLine(0.0F, gap);
			}
			ImGui::PushID(name.c_str());
			ImVec2 at = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##material", ImVec2(width, height))) {
				chosen = name;
				TookTool(toolIndex);
			}
			bool liquid = FluidSim::IsLiquid(material->GetIndex());
			ImGui::SetItemTooltip(kind == Tool::TerrainOther ? "Paints %s, the base game's own." : (liquid ? "Pours %s." : "Pours %s, which falls and piles."), name.c_str());
			bool hovered = ImGui::IsItemHovered();
			bool selected = s_ToolIndex == toolIndex && chosen == name;
			ImVec2 to(at.x + width, at.y + height);
			drawList->AddRectFilled(at, to, ImGui::GetColorU32(selected ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
			drawList->AddRect(at, to, ImGui::GetColorU32(selected ? ImGuiCol_SliderGrab : ImGuiCol_Border), 0.0F, 0, selected ? ToolUI::Pixel() * 2.0F : ToolUI::Pixel());
			DrawIcon(drawList, kind == Tool::TerrainOther ? Icon::Chunk : (liquid ? Icon::Drop : Icon::Grains), ImVec2(std::floor(at.x + (width - pixel * 12.0F) * 0.5F), at.y + pad), pixel, MaterialMarkColor(name, 255));
			float wrap = width - pad;
			ImVec2 nameSize = ImGui::CalcTextSize(name.c_str(), nullptr, false, wrap);
			ImGui::PushClipRect(at, to, true);
			drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(std::floor(at.x + std::max((width - nameSize.x) * 0.5F, pad * 0.5F)), at.y + pad + pixel * 12.0F + ToolUI::Pixel()), ImGui::GetColorU32(selected ? ImGuiCol_SliderGrab : ImGuiCol_Text), name.c_str(), nullptr, wrap);
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
				// From the unit's position to the picture's corner, for drawing it where a unit put there stands (DrawCursor).
				picture.OffsetX = static_cast<float>(left - room / 2);
				picture.OffsetY = static_cast<float>(top - room / 2);
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
		// How the tiles show what each thing is like: not at all, in the tooltip, or on the tile too. One setting for every list.
		const char* statsModes[] = {"Stats: off", "Stats: on hover", "Stats: always"};
		float statsWidth = ImGui::CalcTextSize(statsModes[1]).x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0F;
		ImGui::SetNextItemWidth(-(statsWidth + ImGui::GetStyle().ItemSpacing.x));
		ImGui::InputTextWithHint("##filter", "Search...", filter, 64);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.0F);
		int statsMode = g_SettingsMan.SandboxSpawnStats();
		if (ImGui::Combo("##stats", &statsMode, statsModes, IM_ARRAYSIZE(statsModes))) {
			g_SettingsMan.SetSandboxSpawnStats(statsMode);
		}
		ImGui::SetItemTooltip("What each thing is like, from its game files: cost, and for units health, mass and how many wounds blow them apart; for guns fire rate, magazine, muzzle speed and penetration.\nOff: the tooltip shows only its name and click keys. On hover: the tooltip shows its stats. Always: the main ones are on every tile too.\nCtrl+click a tile: a favourite, or not. Right click (god mode): keep it on the bar. With stats on, hold Ctrl over a tile to see these.");
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
		// Always: room under the name for up to three short lines of stats.
		constexpr int c_TileStatLines = 3;
		float labelHeight = ImGui::GetTextLineHeight() * (statsMode == 2 ? 2.0F + static_cast<float>(c_TileStatLines) : 2.0F);
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
			if ((kind == Tool::Unit || kind == Tool::Drop) && !pickInto && ((s_UnitsShown == 1 && preset.NonCombatant) || (s_UnitsShown == 2 && !preset.NonCombatant))) {
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
				if (statsMode == 2 && !preset.StatsShort.empty()) {
					// Under the name's two lines, each stat on its own line, centred, in the game's gold.
					float lineHeight = ImGui::GetTextLineHeight();
					float y = at.y + cell + lineHeight * 2.0F;
					ImGui::PushClipRect(ImVec2(at.x + 2.0F, y), ImVec2(at.x + size.x - 2.0F, at.y + size.y), true);
					int lines = 0;
					for (size_t from = 0; from < preset.StatsShort.size() && lines < c_TileStatLines; ++lines) {
						size_t to = preset.StatsShort.find('\n', from);
						to = to == std::string::npos ? preset.StatsShort.size() : to;
						const char* begin = preset.StatsShort.c_str() + from;
						const char* end = preset.StatsShort.c_str() + to;
						float width = ImGui::CalcTextSize(begin, end).x;
						drawList->AddText(ImVec2(at.x + std::max((cell - width) * 0.5F, 2.0F), y), ToolTheme::Gold, begin, end);
						y += lineHeight;
						from = to + 1;
					}
					ImGui::PopClipRect();
				}
			}
			if (hovered && statsMode > 0 && !ImGui::GetIO().KeyCtrl) {
				// Its stats in place of the click keys, which holding Ctrl brings back.
				std::string size = preset.Width > 0 ? "\n" + std::to_string(preset.Width) + " x " + std::to_string(preset.Height) + " pixels" : "";
				ImGui::SetTooltip("%s\n%s%s%s", preset.PresetName.c_str(), preset.Module.c_str(), size.c_str(), preset.Stats.c_str());
			} else if (hovered) {
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
				} else if (unit->IsDiggingTo()) {
					// Digging to a place (RC-11), with its line to the place.
					kind = CommandMode::DigTo;
					goal = unit->GetOrderDigTarget();
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
				} else if (kind == CommandMode::DigTo) {
					// A spade: the handle, and the blade pointing into the ground.
					drawList->AddLine(ImVec2(mark.x, mark.y - r), ImVec2(mark.x, mark.y), color, 1.5F);
					drawList->AddTriangleFilled(ImVec2(mark.x - r * 0.8F, mark.y), ImVec2(mark.x + r * 0.8F, mark.y), ImVec2(mark.x, mark.y + r), color);
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
			// (With why, RC-7: "too hard to dig: Concrete", "lost its digger", as the unit's order or the dig-to's check found.)
			std::string reason = marker.Reason.empty() ? std::string("no route") : marker.Reason;
			reason[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(reason[0])));
			std::string text = over ? reason + " for " + std::to_string(alive) + (alive == 1 ? " unit: click to " : " units: click to ") + (marker.Dig ? "dig again" : "send again") : reason;
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
			if (actor->GetTeam() == team && IsSoldier(actor) && !dynamic_cast<const ACraft*>(actor) && IsIdle(actor)) {
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
		} else if (pressed(ImGuiKey_X)) {
			mode(CommandMode::DigTo);
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
		static const Key command[] = {{"Left click", "Order the selection, as the mode says; on a friend, select it"}, {"Left drag", "Select units in a box"}, {"Shift+click", "Add to the selection; with an order, add it to their plans"}, {"Double click", "Every unit of that kind in view"}, {"Right button", "The order ring (right click a plan's numbered step to drop it)"}, {"Click a red cross", "Send the units that had no route there again"}, {"Alt+drag", "Move or attack-move facing the way dragged"}, {"M / T / F / G", "Move, Attack, Attack-move (fight), Guard"}, {"B / R", "Defend at, Patrol"}, {"X", "Dig to: tunnel to the point, in the ground or not (the units with a digger that cuts the way)"}, {"H", "Defend where they stand (Shift: last step of their plans)"}, {"C", "Cancel their orders"}, {"O", "Focus on objective: their team's job in the battle (a flag, a hill, the place it defends)"}, {"V / Y", "Next weapons rule, next movement rule"}, {"L / K", "Next formation, keep together on or off"}, {". / ,", "Next or previous idle unit (Shift: add it)"}, {"Q", "Every unit in view of the kinds selected"}, {"N", "The map: click to look, drag to select, right click to order"}, {"Ctrl+number", "Keep the selection as a group"}, {"Number", "Bring a group back; twice quickly, look at it"}, {"Ctrl+A", "Everyone on the selection's side"}};
		static const Key plants[] = {{"E / Shift+E", "With a plant, cactus, mushroom, tree or candle brush in hand: the next of its pictures, or the one before (just one picked in its gallery: that pick moves on)"}, {"F", "Flip the next one the other way"}};
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
		ImGui::SeparatorText("Plant brushes");
		table("##keysPlants", plants, std::size(plants));
		ImGui::SeparatorText("Command tool");
		ImGui::TextDisabled("Not while you play a unit: its keys are its own then.");
		table("##keysCommand", command, std::size(command));
		bool badges = g_SettingsMan.ShowSandboxGroupBadges();
		if (ToolUI::Checkbox("Group numbers over units", &badges)) {
			g_SettingsMan.SetShowSandboxGroupBadges(badges);
		}
		ImGui::SetItemTooltip("A unit kept in a control group (Ctrl+number) shows the group's number by its feet.");
	}

	void PlantPickPanel(Tool kind) {
		std::vector<PlantPicture> gallery = PlantGallery(kind);
		int count = static_cast<int>(gallery.size());
		if (count == 0) {
			return;
		}
		ImGuiIO& io = ImGui::GetIO();
		PlantPick& pick = s_PlantPicks[kind];
		pick.Chosen.erase(std::remove_if(pick.Chosen.begin(), pick.Chosen.end(), [count](int entry) { return entry < 0 || entry >= count; }), pick.Chosen.end());
		if (s_NextPlant.Kind != kind) {
			s_NextPlant = RollPlant(kind);
		}
		int picked = -1; // A picture clicked: from now on the next one put down.
		bool changed = false;
		ImGui::PushID("plantPick");
		ImGui::AlignTextToFramePadding();
		if (pick.Chosen.empty()) {
			ImGui::Text("Any of its %d pictures, at random", count);
		} else {
			ImGui::Text("%d of its %d pictures picked", static_cast<int>(pick.Chosen.size()), count);
			ImGui::SameLine();
			if (ToolUI::Button("Any")) {
				pick.Chosen.clear();
				changed = true;
			}
			ImGui::SetItemTooltip("Back to any of them, at random.");
		}
		ImGui::BeginDisabled(pick.Chosen.size() < 2);
		int order = pick.InTurn ? 1 : 0;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3F);
		if (ImGui::Combo("##order", &order, "At random\0In turn\0")) {
			pick.InTurn = order == 1;
			pick.Turn = 0;
			changed = true;
		}
		ImGui::SetItemTooltip("With more than one picked (Ctrl+click), whether the brush puts them down at random or one after another, in the order you picked them.");
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45F);
		ImGui::Combo("Facing", &pick.Facing, "Either way\0As drawn\0Mirrored\0");
		ImGui::SetItemTooltip("Which way they face: either way at random, as the art is drawn, or mirrored. F flips the next one.");
		ImGui::TextDisabled("E: next picture, Shift+E: the one before, F: flip it");

		// The gallery: every picture, grouped by kind, in square cells. A click picks just that one, Ctrl+click adds it to the ones picked
		// (or takes it away). The next one to go down is outlined.
		float pixel = ToolUI::Pixel();
		float cell = 36.0F * pixel;
		float gap = ImGui::GetStyle().ItemSpacing.x;
		float width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize - ImGui::GetStyle().WindowPadding.x * 2.0F;
		int columns = std::max(1, static_cast<int>((width + gap) / (cell + gap)));
		bool grouped = gallery.front().Group != gallery.back().Group;
		float contentHeight = 0.0F;
		for (int i = 0, inGroup = 0; i < count; ++i) {
			bool newGroup = i == 0 || gallery[i].Group != gallery[i - 1].Group;
			inGroup = newGroup ? 0 : inGroup + 1;
			if (newGroup && grouped) {
				contentHeight += ImGui::GetTextLineHeightWithSpacing();
			}
			if (inGroup % columns == 0) {
				contentHeight += cell + ImGui::GetStyle().ItemSpacing.y;
			}
		}
		float height = std::min(contentHeight, (cell + ImGui::GetStyle().ItemSpacing.y) * 4.5F) + ImGui::GetStyle().WindowPadding.y * 2.0F;
		int next = PlantEntryOf(kind, s_NextPlant, gallery);
		if (ImGui::BeginChild("##gallery", ImVec2(0.0F, height), ImGuiChildFlags_Borders)) {
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			int column = 0;
			for (int i = 0; i < count; ++i) {
				const PlantPicture& picture = gallery[i];
				if (i == 0 || picture.Group != gallery[i - 1].Group) {
					if (grouped) {
						ImGui::TextDisabled("%s", picture.Group);
					}
					column = 0;
				}
				if (column > 0) {
					ImGui::SameLine();
				}
				column = (column + 1) % columns;
				ImGui::PushID(i);
				ImVec2 at = ImGui::GetCursorScreenPos();
				bool clicked = ImGui::InvisibleButton("##picture", ImVec2(cell, cell));
				bool hovered = ImGui::IsItemHovered();
				bool chosen = std::find(pick.Chosen.begin(), pick.Chosen.end(), i) != pick.Chosen.end();
				ImVec2 end(at.x + cell, at.y + cell);
				drawList->AddRectFilled(at, end, chosen ? IM_COL32(64, 104, 58, 255) : (hovered ? IM_COL32(72, 72, 72, 255) : IM_COL32(38, 38, 38, 255)));
				// The picture as big as fits, in whole steps where it is drawn bigger so it keeps the look of the pixel art.
				int pictureWidth = std::max(picture.Piece->w, picture.LeafPiece ? picture.LeafPiece->w : 0);
				int pictureHeight = std::max(picture.Piece->h, picture.LeafPiece ? picture.LeafPiece->h : 0);
				float room = cell - 4.0F * pixel;
				float fit = std::min(room / static_cast<float>(std::max(pictureWidth, 1)), room / static_cast<float>(std::max(pictureHeight, 1)));
				fit = fit >= 1.0F ? std::min(std::floor(fit), 4.0F * pixel) : fit;
				ImVec2 corner(std::floor(at.x + (cell - static_cast<float>(pictureWidth) * fit) * 0.5F), std::floor(at.y + (cell - static_cast<float>(pictureHeight) * fit) * 0.5F));
				bool mirror = pick.Facing == 2;
				for (BITMAP* layer: {picture.Piece, picture.LeafPiece}) {
					if (!layer) {
						continue;
					}
					const PiecePicture& texture = PictureOfBitmap(layer);
					if (texture.Texture != 0) {
						drawList->AddImage(static_cast<ImTextureID>(texture.Texture), corner, ImVec2(corner.x + static_cast<float>(layer->w) * fit, corner.y + static_cast<float>(layer->h) * fit), ImVec2(mirror ? 1.0F : 0.0F, 0.0F), ImVec2(mirror ? 0.0F : 1.0F, 1.0F));
					}
				}
				if (i == next) {
					drawList->AddRect(at, end, IM_COL32(240, 200, 90, 255), 0.0F, 0, 2.0F * pixel);
				} else if (chosen) {
					drawList->AddRect(at, end, IM_COL32(120, 190, 100, 255), 0.0F, 0, pixel);
				}
				if (hovered) {
					ImGui::SetTooltip("%s, %d of %d (%d x %d px)\nClick: just this one. Ctrl+click: add it to the ones picked, or take it away.", picture.Group, i + 1, count, pictureWidth, pictureHeight);
				}
				if (clicked) {
					if (io.KeyCtrl) {
						if (chosen) {
							pick.Chosen.erase(std::find(pick.Chosen.begin(), pick.Chosen.end(), i));
						} else {
							pick.Chosen.push_back(i);
							picked = i;
						}
					} else {
						pick.Chosen = {i};
						picked = i;
					}
					pick.Turn = 0;
					changed = true;
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::PopID();
		if (changed) {
			s_NextPlant = RollPlant(kind);
			if (picked >= 0) {
				s_NextPlant.Entry = picked;
				if (pick.InTurn && !pick.Chosen.empty()) {
					int inPicked = static_cast<int>(std::find(pick.Chosen.begin(), pick.Chosen.end(), picked) - pick.Chosen.begin());
					pick.Turn = (inPicked + 1) % static_cast<int>(pick.Chosen.size());
				}
			}
		}
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
		if (tool.Kind == Tool::Barracks || tool.Kind == Tool::Extractor || tool.Kind == Tool::Generator) {
			// The plot it will take, on the ground under the pointer.
			const Colony::Type& type = Colony::GetType(tool.Kind == Tool::Barracks ? Colony::Kind::Barracks : tool.Kind == Tool::Extractor ? Colony::Kind::Extractor : Colony::Kind::Generator);
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
			if (tool.Kind == Tool::Generator) {
				// How far it reaches.
				Vector middle = FromCamera(ground);
				drawList->AddCircle(ImVec2(ViewOrigin().x + middle.m_X / scale, ViewOrigin().y + middle.m_Y / scale), Colony::PowerRange() / scale, (c_SideColors[s_Team] & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 120), 96, 1.0F);
			}
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
			// Brush type Shape: the brush draws no stamp of its own, a drag marks out the shape (drawn as it is dragged), so the pointer is only the smallest cursor.
			bool shapeFill = IsTerrainBrush(tool.Kind) && s_ShapeFill;
			float outline = shapeFill ? 1.0F / scale : (tool.UsesRadius ? static_cast<float>(s_Radius) / scale : 6.0F);
			bool square = IsTerrainBrush(tool.Kind) && s_BrushShape == BrushShape::Square && !shapeFill;
			float half = std::max(outline, 3.0F);
			GameViewRect view = g_WindowMan.GetGameViewRect();
			drawList->PushClipRect(ImVec2(view.x, view.y), ImVec2(view.x + view.w, view.y + view.h));
			if (IsPlantBrush(tool.Kind)) {
				// The very plant a click will put down (s_NextPlant), see-through, standing on the ground where it will go.
				if (s_NextPlant.Kind != tool.Kind) {
					s_NextPlant = RollPlant(tool.Kind);
				}
				PlantPlacement plan;
				if (PlanPlant(MouseScenePosition(), s_Radius, tool.Kind, s_PlantScale, s_NextPlant, plan)) {
					for (BITMAP* layer: {plan.Piece, plan.LeafPiece}) {
						const PiecePicture& picture = layer ? PictureOfBitmap(layer) : PiecePicture();
						if (picture.Texture == 0) {
							continue;
						}
						Vector corner = FromCamera(Vector(static_cast<float>(plan.Left), static_cast<float>(plan.Upper)));
						ImVec2 topLeft(ViewOrigin().x + corner.m_X / scale, ViewOrigin().y + corner.m_Y / scale);
						ImVec2 bottomRight(topLeft.x + std::max(1.0F, static_cast<float>(picture.Width) * plan.Scale) / scale, topLeft.y + std::max(1.0F, static_cast<float>(picture.Height) * plan.Scale) / scale);
						drawList->AddImage(static_cast<ImTextureID>(picture.Texture), topLeft, bottomRight, ImVec2(plan.Mirror ? 1.0F : 0.0F, 0.0F), ImVec2(plan.Mirror ? 0.0F : 1.0F, 1.0F), IM_COL32(255, 255, 255, 170));
					}
				}
			} else if ((tool.Kind == Tool::Unit && !s_RandomUnits) || tool.Kind == Tool::Brain) {
				// The units a click puts down, see-through, where they will stand (SpawnUnits): the squad spread out sideways from the point,
				// facing the middle of the view.
				if (const Preset* preset = ChosenPreset(tool.Kind, ChoiceFor(tool.Kind))) {
					const PiecePicture& picture = PictureOf(*preset);
					if (picture.Texture != 0) {
						Vector mouse = MouseScenePosition();
						Vector viewMiddle(g_CameraMan.GetOffset(0).m_X + static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F, mouse.m_Y);
						bool flipped = g_SceneMan.ShortestDistance(mouse, viewMiddle, g_SceneMan.SceneWrapsX()).m_X < 0.0F;
						int count = tool.Kind == Tool::Brain ? 1 : std::max(s_SquadSize, 1);
						for (int i = 0; i < count; ++i) {
							float spread = (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5F) * 16.0F;
							// Mirrored about the unit's position when it faces left, as the game draws it.
							float left = flipped ? -picture.OffsetX - static_cast<float>(picture.Width) + 1.0F : picture.OffsetX;
							ImVec2 topLeft = ToScreen(mouse + Vector(spread + left, picture.OffsetY));
							ImVec2 bottomRight(topLeft.x + static_cast<float>(picture.Width) / scale, topLeft.y + static_cast<float>(picture.Height) / scale);
							drawList->AddImage(static_cast<ImTextureID>(picture.Texture), topLeft, bottomRight, ImVec2(flipped ? 1.0F : 0.0F, 0.0F), ImVec2(flipped ? 0.0F : 1.0F, 1.0F), IM_COL32(255, 255, 255, 150));
						}
					}
				}
			} else if (tool.UsesRadius && !shapeFill) {
				// What the brush lays down, see-through, over just the area it covers. Ground with a terrain texture shows that texture, lined up with
				// the scene as the brush paints it (PaintedColor), so the preview is the very pixels a stroke puts there; other brushes show their
				// colour (dig darkens what it takes out).
				ImU32 fill = LookOf(tool.Kind).Color;
				BITMAP* texture = nullptr;
				if (const char* materialName = TerrainBrushMaterial(tool.Kind)) {
					if (const Material* material = g_SceneMan.GetMaterial(materialName); material && material->GetIndex() != g_MaterialAir) {
						Color color = material->GetColor();
						fill = IM_COL32(color.GetR(), color.GetG(), color.GetB(), 255);
						texture = material->GetFGTexture();
					}
				}
				fill = tool.Kind == Tool::Dig ? IM_COL32(0, 0, 0, 120) : (fill & ~IM_COL32_A_MASK) | (static_cast<ImU32>(160) << IM_COL32_A_SHIFT);
				bool spray = IsTerrainBrush(tool.Kind) && s_BrushShape == BrushShape::Spray;
				const PiecePicture* tiled = texture && tool.Kind != Tool::Dig ? &PictureOfBitmap(texture, true) : nullptr;
				if (tiled && tiled->Texture != 0) {
					// The scene pixels the brush covers (PaintTerrain: the center's pixel, the radius either way), and the texture's place over them.
					Vector mouse = MouseScenePosition();
					int radius = std::max(s_Radius, 1);
					int left = mouse.GetFloorIntX() - radius;
					int top = mouse.GetFloorIntY() - radius;
					int side = radius * 2 + 1;
					if (g_SceneMan.SceneWrapsX() && g_SceneMan.GetSceneWidth() > 0) {
						left = ((left % g_SceneMan.GetSceneWidth()) + g_SceneMan.GetSceneWidth()) % g_SceneMan.GetSceneWidth();
					}
					float u = static_cast<float>(((left % tiled->Width) + tiled->Width) % tiled->Width) / static_cast<float>(tiled->Width);
					float v = static_cast<float>(((top % tiled->Height) + tiled->Height) % tiled->Height) / static_cast<float>(tiled->Height);
					ImVec2 uvMin(u, v);
					ImVec2 uvMax(u + static_cast<float>(side) / static_cast<float>(tiled->Width), v + static_cast<float>(side) / static_cast<float>(tiled->Height));
					ImVec2 middle = ToScreen(Vector(static_cast<float>(mouse.GetFloorIntX()) + 0.5F, static_cast<float>(mouse.GetFloorIntY()) + 0.5F));
					float reach = static_cast<float>(side) * 0.5F / scale;
					ImVec2 topLeft(middle.x - reach, middle.y - reach);
					ImVec2 bottomRight(middle.x + reach, middle.y + reach);
					auto textureID = static_cast<ImTextureID>(tiled->Texture);
					if (square) {
						drawList->AddImage(textureID, topLeft, bottomRight, uvMin, uvMax, IM_COL32(255, 255, 255, 190));
					} else if (spray) {
						// The spray: thin at the edge, thicker towards the middle, as it builds up.
						for (float share: {1.0F, 0.66F, 0.33F}) {
							float shrink = reach * (1.0F - share);
							float uShrink = (uvMax.x - uvMin.x) * (1.0F - share) * 0.5F;
							float vShrink = (uvMax.y - uvMin.y) * (1.0F - share) * 0.5F;
							drawList->AddImageRounded(textureID, ImVec2(topLeft.x + shrink, topLeft.y + shrink), ImVec2(bottomRight.x - shrink, bottomRight.y - shrink), ImVec2(uvMin.x + uShrink, uvMin.y + vShrink), ImVec2(uvMax.x - uShrink, uvMax.y - vShrink), IM_COL32(255, 255, 255, 70), reach * share);
						}
					} else {
						drawList->AddImageRounded(textureID, topLeft, bottomRight, uvMin, uvMax, IM_COL32(255, 255, 255, 190), reach);
					}
				} else if (square) {
					drawList->AddRectFilled(ImVec2(io.MousePos.x - half, io.MousePos.y - half), ImVec2(io.MousePos.x + half, io.MousePos.y + half), fill);
				} else if (spray) {
					// The spray: thin at the edge, thicker towards the middle, as it builds up.
					ImU32 thin = (fill & ~IM_COL32_A_MASK) | (static_cast<ImU32>(55) << IM_COL32_A_SHIFT);
					for (float share: {1.0F, 0.66F, 0.33F}) {
						drawList->AddCircleFilled(io.MousePos, half * share, thin);
					}
				} else {
					drawList->AddCircleFilled(io.MousePos, half, fill);
				}
			}
			drawList->PopClipRect();
			if (square) {
				// The square brush: the square it paints.
				drawList->AddRect(ImVec2(io.MousePos.x - half, io.MousePos.y - half), ImVec2(io.MousePos.x + half, io.MousePos.y + half), white, 0.0F, 0, 1.5F);
			} else {
				drawList->AddCircle(io.MousePos, half, tool.Kind == Tool::Unit || tool.Kind == Tool::Brain || tool.Kind == Tool::RallyPoint ? c_SideColors[s_Team] : white, 0, 1.5F);
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
			if (s_CommandMode == CommandMode::Select) {
				const bool pickable = under && IsSelectable(under);
				if (pickable) {
					drawList->AddCircle(ToScreen(under->GetPos()), std::max(under->GetRadius() / scale, 8.0F) + pixel * 2.0F, IM_COL32(255, 255, 255, 200), 0, pixel);
					label = "Select " + under->GetPresetName() + "  (Shift: add, double click: all of this kind)";
				} else {
					label = units.empty() ? "Click a unit or drag a box round several to select them" : count + " selected: pick an order (M, T, F, G, B...) or right click";
				}
			} else if (units.empty() || (underIsFriend && s_CommandMode == CommandMode::Move)) {
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
			} else if (s_CommandMode == CommandMode::DigTo) {
				// Dig to (RC-11): the lead digger's way there, walked parts in the mode's colour and dug parts from yellow (soft) to red-orange
				// (near its digger's limit); where it can't go, a red cross on what stops it. And the verdict in words.
				ImU32 sand = c_CommandModeColors[static_cast<int>(CommandMode::DigTo)];
				const DigPreview& preview = DigToPreview(units, point);
				const DigPlan& plan = preview.Plan;
				auto kind = plan.Kinds.begin();
				for (auto at = plan.Route.begin(); at != plan.Route.end() && std::next(at) != plan.Route.end(); ++at) {
					const Vector& from = *at;
					const Vector& to = *std::next(at);
					ImU32 color = (sand & 0x00FFFFFF) | (150u << IM_COL32_A_SHIFT);
					float width = pixel * 1.5F;
					if (kind != plan.Kinds.end() && *kind == PathStepKind::Dig) {
						float hardness = std::clamp(g_SceneMan.CastMaxStrengthRay(from, to, 2) / std::max(preview.LeadStrength, 1.0F), 0.0F, 1.0F);
						color = IM_COL32(250, static_cast<int>(220.0F - 130.0F * hardness), static_cast<int>(90.0F - 60.0F * hardness), 235);
						width = pixel * 3.0F;
					}
					drawList->AddLine(ToScreen(from), ToScreen(to), color, width);
					if (kind != plan.Kinds.end()) {
						++kind;
					}
				}
				if (plan.Result == DigPlan::TooHard) {
					ImU32 red = IM_COL32(239, 90, 80, 255);
					ImVec2 at = ToScreen(plan.BlockingAt);
					float arm = pixel * 5.0F;
					drawList->AddLine(ImVec2(at.x - arm, at.y - arm), ImVec2(at.x + arm, at.y + arm), red, pixel * 2.0F);
					drawList->AddLine(ImVec2(at.x - arm, at.y + arm), ImVec2(at.x + arm, at.y - arm), red, pixel * 2.0F);
				}
				crosshair(point, plan.Result == DigPlan::Ok ? sand : IM_COL32(239, 90, 80, 200), pixel * 6.0F);
				label = "Dig to here with " + count + ": " + DigVerdict(preview);
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
			if (io.KeyShift && !units.empty() && s_CommandMode != CommandMode::Select && !(underIsFriend && s_CommandMode == CommandMode::Move) && !label.empty()) {
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


	/// Makes a picture's texture from an 8-bit bitmap, its mask colour see-through.
	void MakePicture(PiecePicture& picture, BITMAP* bitmap) {
		if (!bitmap || bitmap_color_depth(bitmap) != 8) {
			return;
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
	}

	/// A picture made from one of the game's own 8-bit image files, the first time it is asked for: the pie menu's icons and cursor.
	const PiecePicture& PictureOfFile(const std::string& path) {
		std::map<std::string, PiecePicture>& pictures = s_FilePictures;
		if (auto found = pictures.find(path); found != pictures.end()) {
			return found->second;
		}
		PiecePicture& picture = pictures[path];
		MakePicture(picture, ContentFile(path.c_str()).GetAsBitmap());
		return picture;
	}

	/// A picture of one of the game's loaded bitmaps (a plant brush's pieces, a material's terrain texture), the first time it is asked for.
	/// Kept with the file pictures, by the bitmap's address: the presets' bitmaps last as long as the game's data.
	/// @param repeat Whether it tiles when drawn past its edges (a terrain texture laid over the scene), rather than stopping at them.
	const PiecePicture& PictureOfBitmap(BITMAP* bitmap, bool repeat) {
		char key[48];
		std::snprintf(key, sizeof(key), "bitmap:%p%s", static_cast<void*>(bitmap), repeat ? ":tiled" : "");
		std::map<std::string, PiecePicture>& pictures = s_FilePictures;
		if (auto found = pictures.find(key); found != pictures.end()) {
			return found->second;
		}
		PiecePicture& picture = pictures[key];
		MakePicture(picture, bitmap);
		if (repeat && picture.Texture != 0) {
			GLint boundBefore = 0;
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundBefore);
			glBindTexture(GL_TEXTURE_2D, picture.Texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundBefore));
		}
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
			std::vector<RingItem> commands = {{"Move", IM_COL32(110, 180, 250, 255), "GoTo"}, {"Attack", IM_COL32(239, 106, 91, 255), "Death"}, {"Guard", IM_COL32(120, 220, 120, 255), "Follow"}, {"Attack-move", c_CommandModeColors[static_cast<int>(CommandMode::AttackMove)], "Speed"}, {"Defend", IM_COL32(242, 182, 61, 255), "Eye"}, {"Cancel", IM_COL32(200, 160, 120, 255), "Cancel"}, {"Deselect", IM_COL32(150, 150, 140, 255), "Remove"}, {ruleLabel(true), IM_COL32(242, 182, 61, 255), "Reload"}, {ruleLabel(false), IM_COL32(120, 220, 120, 255), "Move"}, {"Focus on objective", IM_COL32(180, 140, 240, 255), "Flag"}, {"More...", IM_COL32(200, 200, 200, 255), "SubPieMenu1"}, {"Dig to", c_CommandModeColors[static_cast<int>(CommandMode::DigTo)], "Dig"}};
			int picked = DrawRing(commands, s_CommandMode == CommandMode::DigTo ? 11 : static_cast<int>(s_CommandMode), s_RingPage == 2);
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
			} else if (picked == 11) {
				// Dig to (RC-11): the clicks to come tunnel to the point.
				s_CommandMode = CommandMode::DigTo;
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
			// Short of power, said in red after the name.
			const char* power = building.NoPower ? (building.Power > 0.0F ? "  Low power" : "  No power") : "";
			ImVec2 size = ImGui::CalcTextSize(label.c_str());
			ImVec2 powerSize = ImGui::CalcTextSize(power);
			ImVec2 at(top.x - (size.x + powerSize.x) * 0.5F, top.y - size.y - 6.0F);
			drawList->AddRectFilled(ImVec2(at.x - 4.0F, at.y - 2.0F), ImVec2(at.x + size.x + powerSize.x + 4.0F, at.y + size.y + 2.0F), IM_COL32(0, 0, 0, 140), 3.0F);
			drawList->AddText(at, c_SideColors[building.Team], label.c_str());
			if (*power) {
				drawList->AddText(ImVec2(at.x + size.x, at.y), IM_COL32(255, 90, 70, 255), power);
			}
			if (building.What == Colony::Kind::Generator && Colony::NeedsPower() && !building.Paused) {
				// How far it reaches, faintly.
				drawList->AddCircle(ToScreen(building.Ground), Colony::PowerRange() / scale, (c_SideColors[building.Team] & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 60), 96, 1.0F);
			}
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
		ImGui::TextWrapped("Buildings that work for a side. A barracks trains a unit, sends it out with its orders, and trains another whenever fewer than its number are alive. An extractor earns supply. A generator powers its side's buildings near it, when buildings need power. They are built of concrete: wreck one and it stops.");
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
		ToolUI::Checkbox("Buildings need power", &Colony::NeedsPower());
		ImGui::SetItemTooltip("On: a barracks draws %.0f power while it trains, from its side's generators within %.0f pixels. Each generator gives %.0f.", Colony::TrainingPower(), Colony::PowerRange(), Colony::GeneratorPower());
		if (Colony::NeedsPower()) {
			int withoutPower = static_cast<int>(Colony::WithoutPower());
			const char* choices[] = {"stops training", "trains slower"};
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4F);
			if (ImGui::Combo("Short of power, a barracks", &withoutPower, choices, 2)) {
				Colony::WithoutPower() = static_cast<Colony::NoPower>(withoutPower);
			}
			ImGui::SetItemTooltip("Slower: it trains at the share of its power it gets, and at a quarter pace with none.");
		}
		ImGui::SeparatorText("Build");
		ToolButtons({Tool::Barracks, Tool::Extractor, Tool::Generator});
		Tool kind = CurrentTool().Kind;
		if (kind == Tool::Barracks || kind == Tool::Extractor || kind == Tool::Generator) {
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
					if (Colony::NeedsPower()) {
						ImGui::Text("Needs %.0f power while it trains%s.", Colony::TrainingPower(), building.NoPower ? (building.Power > 0.0F ? ", and is getting " + std::to_string(static_cast<int>(building.Power * 100.0F)) + "% of it" : ", and has none: build a generator near it").c_str() : "");
					}
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
					ImGui::SetItemTooltip("Every pixel of concrete, metal, glass and bunker material on the map, the things built with the Build tab included.");
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

	/// A thin upright rule between groups on the bar, as tall as what came before it; none where the bar's width has put the next group on a line of its own.
	void BarDivider() {
		float height = ImGui::GetItemRectSize().y;
		float lineY = ImGui::GetCursorPosY();
		ImGui::SameLine(0.0F, ToolUI::Pixel() * 5.0F);
		if (ImGui::GetCursorPosY() >= lineY) {
			return;
		}
		ImVec2 at = ImGui::GetCursorScreenPos();
		float pixel = ToolUI::Pixel();
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + pixel * 3.0F), ImVec2(at.x + pixel, at.y + height - pixel * 3.0F), (ToolTheme::Edge & 0x00FFFFFF) | (150u << IM_COL32_A_SHIFT));
		ImGui::Dummy(ImVec2(pixel, height));
		ImGui::SameLine(0.0F, pixel * 5.0F);
	}

	/// Puts the items of a row of the bar one after another: the first where the row starts, the rest on the same line (or the next, when they don't fit).
	struct BarLine {
		bool Any = false; //!< Whether anything has been put in the row.
		bool Placed = false; //!< Whether a rule has put the cursor where the next item goes already.
		void Next(float gap = -1.0F) {
			if (Any && !Placed) {
				ImGui::SameLine(0.0F, gap);
			}
			Any = true;
			Placed = false;
		}
		/// A rule between groups of the row, if there is anything before it.
		void Divide() {
			if (Any && !Placed) {
				BarDivider();
				Placed = true;
			}
		}
	};

	/// A word on the bar to click, in a colour of its own (a stripe down its left) or plain; filled in that colour when chosen. A key for it, if
	/// given, shows faintly after the word. Greyed (and not clickable) inside ImGui::BeginDisabled.
	/// @return 1 if clicked, 2 if right-clicked, 0 otherwise.
	int BarChip(const char* label, bool chosen, ImU32 color = ToolTheme::Text, const char* key = nullptr, const char* tip = nullptr) {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImGuiStyle& style = ImGui::GetStyle();
		float pixel = ToolUI::Pixel();
		const char* end = ImGui::FindRenderedTextEnd(label);
		float textWidth = ImGui::CalcTextSize(label, end).x;
		float keyWidth = key ? ImGui::CalcTextSize(key).x + pixel * 5.0F : 0.0F;
		bool plain = color == ToolTheme::Text;
		ImVec2 size(std::floor(textWidth + keyWidth + style.FramePadding.x * 2.0F + (plain ? 0.0F : pixel * 2.0F)), ImGui::GetFrameHeight());
		ImVec2 at = ImGui::GetCursorScreenPos();
		int result = ImGui::InvisibleButton(label, size) ? 1 : 0;
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			result = 2;
		}
		bool hovered = ImGui::IsItemHovered();
		bool held = ImGui::IsItemActive();
		// (Faded as ImGui fades what is disabled.)
		float alpha = style.Alpha;
		auto faded = [alpha](ImU32 tint, float share) { return (tint & 0x00FFFFFF) | (static_cast<ImU32>(static_cast<float>((tint >> IM_COL32_A_SHIFT) & 0xFF) * share * alpha) << IM_COL32_A_SHIFT); };
		ImVec2 to(at.x + size.x, at.y + size.y);
		if (chosen) {
			// A darker shade of its colour, so the light lettering (whose pixel font has a dark edge of its own) reads on it.
			ImU32 fill = plain ? ToolTheme::Edge : color;
			auto shade = [fill](int shift) { return static_cast<ImU32>(static_cast<float>((fill >> shift) & 0xFF) * 0.45F) << shift; };
			drawList->AddRectFilled(at, to, faded(shade(IM_COL32_R_SHIFT) | shade(IM_COL32_G_SHIFT) | shade(IM_COL32_B_SHIFT) | IM_COL32_A_MASK, 1.0F));
			drawList->AddRect(at, to, faded(plain ? ToolTheme::EdgeLight : color, 1.0F), 0.0F, 0, pixel);
			drawList->AddRectFilled(ImVec2(at.x, to.y - pixel * 2.0F), to, faded(ToolTheme::Gold, 1.0F));
		} else {
			drawList->AddRectFilled(at, to, faded(held ? ToolTheme::WellPressed : hovered ? ToolTheme::WellHover
			                                                                              : ToolTheme::Well,
			                                      0.85F));
			if (!plain) {
				drawList->AddRectFilled(at, ImVec2(at.x + pixel * 2.0F, to.y), faded(color, 1.0F));
			}
		}
		float textX = at.x + style.FramePadding.x + (plain ? 0.0F : pixel * 2.0F);
		drawList->AddText(ImVec2(std::floor(textX), at.y + style.FramePadding.y), faded(chosen || plain ? ToolTheme::Text : color, 1.0F), label, end);
		if (key) {
			drawList->AddText(ImVec2(std::floor(textX + textWidth + pixel * 5.0F), at.y + style.FramePadding.y), faded(ToolTheme::Text, chosen ? 0.7F : 0.45F), key);
		}
		if (tip && *tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
			ImGui::SetTooltip("%s", tip);
		}
		return result;
	}

	/// Text in the middle of a tile's picture (the speed, a count), for the tiles that show a number rather than a picture.
	void PictureText(ImDrawList* drawList, ImVec2 at, float room, ImU32 color, const char* text) {
		ImVec2 size = ImGui::CalcTextSize(text);
		drawList->AddText(ImVec2(std::floor(at.x + (room - size.x) * 0.5F), std::floor(at.y + (room - size.y) * 0.5F)), color, text);
	}

	/// The tools of each part of the sandbox, in the groups the window has them in, for the shelf of the bar: the part of the tool in hand, so
	/// its neighbours are a click away without opening the window.
	struct BarShelf {
		const char* Tab; //!< The part of the sandbox window the tools are on.
		const char* Group; //!< Which of its groups.
		std::vector<Tool> Tools;
	};

	const std::vector<BarShelf>& BarShelves() {
		static const std::vector<BarShelf> shelves = {
		    {"Spawn", "Spawn", {Tool::Unit, Tool::Drop, Tool::Brain, Tool::Item}},
		    {"Build", "Build", {Tool::Structure, Tool::Decor}},
		    {"Paint", "Liquids", {Tool::Water, Tool::Lava, Tool::Acid, Tool::Oil, Tool::Mud, Tool::Tar, Tool::Mercury, Tool::Fuel, Tool::Cryo, Tool::Blood, Tool::PourOther, Tool::WaterSpawner}},
		    {"Paint", "Fire and gas", {Tool::Fire, Tool::Smoke, Tool::ToxicGas, Tool::Methane, Tool::Steam}},
		    {"Paint", "Loose", {Tool::LooseSand, Tool::LooseSnow, Tool::Gravel, Tool::GlassShards, Tool::Boulder, Tool::Slab}},
		    {"Paint", "Terrain", {Tool::Dig, Tool::Earth, Tool::Sand, Tool::Ice, Tool::Grass, Tool::Wood, Tool::TreeTrunk, Tool::Concrete, Tool::Stone, Tool::DenseEarth, Tool::GoldEarth, Tool::Metal, Tool::TerrainOther, Tool::CollapseArea}},
		    {"Paint", "Plants", {Tool::Plants, Tool::Cacti, Tool::Mushrooms, Tool::Trees, Tool::GrowGrass, Tool::Candles}},
		    {"Paint", "Ropes", {Tool::Rope, Tool::RopeCut}},
		    {"Boom", "Blasts", {Tool::Grenade, Tool::BigBomb, Tool::Napalm, Tool::Lightning, Tool::Demolition, Tool::BunkerBuster, Tool::Meteor}},
		    {"Boom", "Force", {Tool::ForceBlast, Tool::HugeForceBlast, Tool::Implosion, Tool::Updraft, Tool::GustRight, Tool::GustLeft, Tool::SmokeBomb, Tool::Fireworks}},
		    {"Boom", "From the sky", {Tool::RocketStrike, Tool::RocketBarrage, Tool::CarpetBomb, Tool::Artillery, Tool::NapalmRain, Tool::OrbitalBeam, Tool::BoulderRain, Tool::CrashRocket, Tool::CrashDropship}},
		    {"Effects", "Effects", {Tool::Effect}},
		    {"Colony", "Colony", {Tool::Barracks, Tool::Extractor, Tool::Generator}},
		    {"Gym", "Gym", {Tool::GymStart, Tool::GymGoal}},
		};
		return shelves;
	}

	/// The shelf a tool is on, or none for the bar's own tools (Look around, Command, Remove...).
	const BarShelf* ShelfOf(Tool kind) {
		for (const BarShelf& shelf: BarShelves()) {
			if (std::find(shelf.Tools.begin(), shelf.Tools.end(), kind) != shelf.Tools.end()) {
				return &shelf;
			}
		}
		return nullptr;
	}

	std::map<std::string, int> s_LastToolOfGroup; //!< The tool last taken from each group of the shelf, taken again when the group is.

	/// Takes a tool from the bar's shelf, remembered for its group and part; the window, if open, turns to that part.
	void TakeFromShelf(const BarShelf& shelf, int toolIndex) {
		s_ToolIndex = toolIndex;
		s_LastToolOfTab[shelf.Tab] = toolIndex;
		s_LastToolOfGroup[shelf.Group] = toolIndex;
		if (Sandbox::IsOpen() && s_CurrentTab != shelf.Tab) {
			s_WantedTab = shelf.Tab;
			s_CurrentTab = shelf.Tab;
		}
	}

	/// Opens the sandbox window on a part of it, or puts it away if it is showing that part already.
	void ToggleWindowTab(const char* tab) {
		if (Sandbox::IsOpen() && s_CurrentTab == tab) {
			Sandbox::SetOpen(false);
		} else {
			Sandbox::SetOpen(true);
			s_WantedTab = tab;
			s_CurrentTab = tab;
		}
	}

	/// The settings of the tool in hand, after its name, on the shelf row of the bar: brush size and shape for painting, squad size and orders
	/// for units, what a spring pours, and so on: what the window has for it, short.
	/// @param named Whether to put the tool's name first even if it has nothing to set (it has a shelf, so the row is there anyway).
	void ToolSettings(BarLine& line, bool named) {
		const ToolInfo& tool = CurrentTool();
		const Tool kind = tool.Kind;
		float pixel = ToolUI::Pixel();
		float field = ImGui::GetFontSize() * 8.0F;
		bool started = false;
		auto start = [&]() {
			if (started) {
				return;
			}
			started = true;
			line.Divide();
			line.Next();
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(ToolTheme::Vec(ToolTheme::Gold), "%s", tool.Name);
			if (const char* unavailable = ToolUnavailableReason(kind)) {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.3F, 1.0F), "(off)");
				ImGui::SetItemTooltip("%s", unavailable);
			}
		};
		auto next = [&]() {
			start();
			line.Next();
		};
		auto wide = [&]() {
			start();
			line.Next(pixel * 10.0F);
		};
		if (named) {
			start();
		}
		// What is painted with.
		if (kind == Tool::PourOther || kind == Tool::TerrainOther || kind == Tool::Metal) {
			wide();
			ImGui::SetNextItemWidth(field);
			if (kind == Tool::Metal) {
				const char* shown = s_PaintMetal.c_str();
				for (const PaintMetal& metal: c_PaintMetals) {
					shown = s_PaintMetal == metal.Material ? metal.Name : shown;
				}
				if (ImGui::BeginCombo("##paintWhat", shown)) {
					for (const PaintMetal& metal: c_PaintMetals) {
						const Material* material = g_SceneMan.GetMaterial(metal.Material);
						if (material && material->GetIndex() != g_MaterialAir && ImGui::Selectable(metal.Name, s_PaintMetal == metal.Material)) {
							s_PaintMetal = metal.Material;
						}
					}
					ImGui::EndCombo();
				}
			} else {
				std::string& chosen = kind == Tool::PourOther ? s_OtherPourable : s_OtherTerrain;
				std::vector<std::string> names = kind == Tool::PourOther ? PourableNames() : std::vector<std::string>(std::begin(c_TerrainMaterials), std::end(c_TerrainMaterials));
				if (chosen.empty() && !names.empty()) {
					chosen = names.front();
				}
				if (ImGui::BeginCombo("##paintWhat", chosen.c_str(), ImGuiComboFlags_HeightLarge)) {
					for (const std::string& name: names) {
						if (ImGui::Selectable(name.c_str(), name == chosen)) {
							chosen = name;
						}
					}
					ImGui::EndCombo();
				}
			}
			ImGui::SetItemTooltip("What it paints.");
		}
		// The brush.
		const bool terrainBrush = IsTerrainBrush(kind);
		if (terrainBrush) {
			wide();
			if (BarChip("Brush", !s_ShapeFill, ToolTheme::Text, nullptr, "Paint (or dig) along where the pointer goes, the brush's size and shape.") == 1) {
				s_ShapeFill = false;
			}
			next();
			if (BarChip("Shape", s_ShapeFill, ToolTheme::Text, nullptr, "Click and drag out a circle, triangle or square on the world, and it is filled in one go (Dig digs it out).\nShift keeps it as wide as tall; Escape drops it.") == 1) {
				s_ShapeFill = true;
			}
		}
		if (terrainBrush && s_ShapeFill) {
			static const char* const fills[] = {"Circle", "Triangle", "Square"};
			for (int i = 0; i < 3; ++i) {
				i == 0 ? wide() : next();
				ImGui::PushID(i + 40);
				if (BarChip(fills[i], static_cast<int>(s_FillShape) == i) == 1) {
					s_FillShape = static_cast<FillShape>(i);
				}
				ImGui::PopID();
			}
		} else if (tool.UsesRadius) {
			wide();
			ImGui::SetNextItemWidth(field);
			ImGui::SliderInt("##brush", &s_Radius, 1, c_MaxBrushRadius, "Size %d px", ImGuiSliderFlags_Logarithmic);
			for (const auto& [label, size]: {std::pair<const char*, int>{"S", 4}, {"M", 10}, {"L", 24}, {"XL", 60}}) {
				next();
				if (BarChip(label, s_Radius == size) == 1) {
					s_Radius = size;
				}
			}
			if (terrainBrush) {
				static const char* const shapes[] = {"Circle", "Square", "Spray"};
				for (int i = 0; i < 3; ++i) {
					i == 0 ? wide() : next();
					ImGui::PushID(i + 50);
					if (BarChip(shapes[i], static_cast<int>(s_BrushShape) == i, ToolTheme::Text, nullptr, i == 2 ? "A soft spray: scattered over the circle, thickest in the middle, building up while held." : nullptr) == 1) {
						s_BrushShape = static_cast<BrushShape>(i);
					}
					ImGui::PopID();
				}
			}
		}
		// Pouring.
		if (PoursLiquid(kind) && kind != Tool::WaterSpawner) {
			wide();
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderFloat("##flow", &s_Flow, 0.1F, 1.0F, "Flow %.2f");
			ImGui::SetItemTooltip("How fast it pours while held. 1: as fast as it goes.");
		}
		if ((PoursLiquid(kind) || terrainBrush) && kind != Tool::Dig && kind != Tool::WaterSpawner) {
			wide();
			if (BarChip("Over liquids", s_PaintOverLiquids, ToolTheme::Text, nullptr, "On: replaces water, lava, oil, sand, snow and the like, instead of only filling air.") == 1) {
				s_PaintOverLiquids = !s_PaintOverLiquids;
			}
			next();
			if (BarChip("Over terrain", s_PaintOverTerrain, ToolTheme::Text, nullptr, "On: replaces solid terrain (earth, rock, concrete, metal...) instead of only filling air. The edge of the world stays.") == 1) {
				s_PaintOverTerrain = !s_PaintOverTerrain;
			}
		}
		// Springs.
		if (kind == Tool::WaterSpawner) {
			wide();
			ImGui::SetNextItemWidth(field);
			if (ImGui::BeginCombo("##springPours", s_SpringLiquid.c_str(), ImGuiComboFlags_HeightLarge)) {
				for (const std::string& name: PourableNames()) {
					if (ImGui::Selectable(name.c_str(), name == s_SpringLiquid)) {
						s_SpringLiquid = name;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SetItemTooltip("What new springs pour.");
			next();
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderFloat("##springRate", &s_SpringRate, 0.05F, 1.0F, "Rate %.2f");
			ImGui::SetItemTooltip("How much of the time new springs pour. 1: they keep the air around them full.");
			next();
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderFloat("##springLife", &s_SpringLife, 0.0F, 120.0F, s_SpringLife <= 0.0F ? "Lasts for ever" : "Lasts %.0f s");
			ImGui::SetItemTooltip("How long what a new spring pours lasts before it is gone, wherever it has flowed to. For ever: it stays, and the pool builds up.");
			wide();
			ImGui::BeginDisabled(s_WaterSpawners.empty());
			std::string removeAll = "Remove all " + std::to_string(s_WaterSpawners.size());
			if (BarChip(removeAll.c_str(), false, IM_COL32(239, 106, 91, 255), nullptr, "Takes away every spring on the map.") == 1) {
				QueueSimChange(Tool::ClearWaterSpawners);
			}
			ImGui::EndDisabled();
		}
		// Plants.
		if (IsPlantBrush(kind)) {
			wide();
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderFloat("##plantSize", &s_PlantScale, 0.5F, 3.0F, "Size x%.1f");
			ImGui::SetItemTooltip("How big they are drawn. x1 is the game's own art.");
			next();
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderInt("##plantSpacing", &s_PlantSpacing, 2, 60, "Apart %d px");
			ImGui::SetItemTooltip("How far apart they go along a stroke.");
			// Which of its pictures: a gallery of them all, in a window over the bar.
			const PlantPick& pick = s_PlantPicks[kind];
			std::string which = pick.Chosen.empty() ? std::string("Any, pick...") : std::to_string(pick.Chosen.size()) + " picked...";
			next();
			if (BarChip(which.c_str(), !pick.Chosen.empty(), ToolTheme::Text, nullptr, "Which of its pictures it puts down: any at random, or the ones you pick from a gallery of them all. E: the next one (Shift+E: back), F: flip it.") == 1) {
				ImGui::OpenPopup("##plantPick");
			}
			ImGui::SetNextWindowSize(ImVec2(380.0F * ToolUI::Pixel(), 0.0F));
			if (ImGui::BeginPopup("##plantPick")) {
				PlantPickPanel(kind);
				ImGui::EndPopup();
			}
			if (kind == Tool::Candles) {
				bool forever = TerrainCandle::GetBurnMinutes() <= 0.0F;
				next();
				if (BarChip("Burn forever", forever, ToolTheme::Text, nullptr, "Lit candles keep burning and never melt down. Off, they burn down (Settings > Fire and smoke for how fast).") == 1) {
					TerrainCandle::SetBurnMinutes(forever ? 2.0F : 0.0F);
				}
			}
		}
		// Ropes.
		if (kind == Tool::Rope || kind == Tool::RopeCut) {
			if (kind == Tool::Rope) {
				wide();
				ImGui::SetNextItemWidth(field);
				s_RopeType = std::clamp(s_RopeType, 0, std::max(RopeSim::GetTypeCount() - 1, 0));
				if (RopeSim::GetTypeCount() > 0 && ImGui::BeginCombo("##ropeType", RopeSim::GetType(s_RopeType).Name)) {
					for (int type = 0; type < RopeSim::GetTypeCount(); ++type) {
						if (ImGui::Selectable(RopeSim::GetType(type).Name, type == s_RopeType)) {
							s_RopeType = type;
						}
						ImGui::SetItemTooltip("%s", RopeSim::GetType(type).About);
					}
					ImGui::EndCombo();
				}
				next();
				int slack = static_cast<int>(std::round(s_RopeSlack * 100.0F));
				ImGui::SetNextItemWidth(field * 0.8F);
				if (ImGui::SliderInt("##slack", &slack, 0, 100, "Slack %d%%")) {
					s_RopeSlack = static_cast<float>(slack) / 100.0F;
				}
				ImGui::SetItemTooltip("How much longer than the straight line between its points: 0 strung tight, 50%% droops well down.\nEach click puts down a point, tied to what is there; a right click finishes the rope.");
				next();
				ImGui::SetNextItemWidth(field * 0.8F);
				ImGui::SliderFloat("##ropeStrength", &s_RopeStrength, 0.1F, 100.0F, "Strength x%.1f", ImGuiSliderFlags_Logarithmic);
				ImGui::SetItemTooltip("How much more (or less) than the kind's own it holds before it snaps. Everything else about it stays: a rope can be made to hold a dropship and still burn and be cut by a bullet.");
				next();
				ImGui::SetNextItemWidth(field * 0.8F);
				ImGui::SliderFloat("##ropeAnchor", &s_RopeAnchor, 0.0F, 5000.0F, s_RopeAnchor <= 0.0F ? "Ties unbreakable" : "Ties fail at %.0f kg", ImGuiSliderFlags_Logarithmic);
				ImGui::SetItemTooltip("How hard a tie can be pulled, in kg, before it lets go of the rope (pulled out of the ground or off the unit). Far left: the ties never fail, only the rope can.");
			}
			wide();
			ImGui::BeginDisabled(RopeSim::GetCount() == 0);
			if (BarChip("Remove all ropes", false, IM_COL32(239, 106, 91, 255)) == 1) {
				Stroke stroke;
				stroke.Kind = Tool::Rope;
				stroke.Choice = 2;
				s_Queue.push_back(stroke);
				s_RopeDraft.clear();
			}
			ImGui::EndDisabled();
		}
		// Spawning.
		if (kind == Tool::Unit || kind == Tool::Drop) {
			const Preset* preset = ChosenPreset(kind, ChoiceFor(kind));
			std::string randomName = RandomSourceName(s_RandomFavourites, s_RandomFaction);
			wide();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(s_RandomUnits ? randomName.c_str() : preset ? preset->PresetName.c_str()
			                                                                   : "(pick one in the Spawn panel)");
			next();
			if (BarChip("Random", s_RandomUnits, ToolTheme::Text, nullptr, "Each unit is picked at random (from what the Spawn panel says), not the one chosen.") == 1) {
				s_RandomUnits = !s_RandomUnits;
			}
			wide();
			ImGui::SetNextItemWidth(field * 0.7F);
			ImGui::SliderInt("##squad", &s_SquadSize, 1, 10, "Squad of %d");
			next();
			ImGui::SetNextItemWidth(field);
			UnitOrderCombo("##orders");
			next();
			ImGui::SetNextItemWidth(field);
			LoadoutChooser("##loadout");
			next();
			ImGui::SetNextItemWidth(field);
			TemperamentCombo("##temperament");
			if (kind == Tool::Drop) {
				next();
				ImGui::SetNextItemWidth(field * 0.7F);
				ImGui::Combo("##craft", &s_Craft, "Dropship\0Rocket\0");
			}
		} else if (kind == Tool::Item || kind == Tool::Brain) {
			const Preset* preset = ChosenPreset(kind, ChoiceFor(kind));
			wide();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(preset ? preset->PresetName.c_str() : "(pick one in the Spawn panel)");
			if (kind == Tool::Item) {
				next();
				if (BarChip("Pull the pin", s_LitGrenade, ToolTheme::Text, nullptr, "Grenades are put down live.") == 1) {
					s_LitGrenade = !s_LitGrenade;
				}
			}
		} else if (kind == Tool::Structure) {
			const Preset* preset = ChosenPreset(Tool::Structure, s_StructureChoice);
			wide();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(preset ? preset->PresetName.c_str() : "(pick one in the Build panel)");
			next();
			const int groupCount = static_cast<int>(std::size(c_StructureGroups));
			s_StructureGroup = std::clamp(s_StructureGroup, 0, groupCount);
			ImGui::SetNextItemWidth(field);
			if (ImGui::BeginCombo("##structureKind", s_StructureGroup < groupCount ? c_StructureGroups[s_StructureGroup] : "Everything")) {
				for (int group = 0; group <= groupCount; ++group) {
					if (ImGui::Selectable(group < groupCount ? c_StructureGroups[group] : "Everything", group == s_StructureGroup)) {
						s_StructureGroup = group;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SetItemTooltip("Which kind of pieces the Build panel lists.");
			next();
			if (BarChip("Snap to grid", s_SnapToGrid, ToolTheme::Text, nullptr, "On: pieces line up on the 24 pixel grid bunkers are built on. Off: they go exactly where the pointer is.") == 1) {
				s_SnapToGrid = !s_SnapToGrid;
			}
			next();
			if (BarChip("Game's build menu", false, ToolTheme::Text, nullptr, "The build menu the game uses before a battle, with its own cursor. Done in its menu, or Tab, comes back.") == 1) {
				Sandbox::SetBuildMode(true);
			}
		} else if (kind == Tool::Barracks) {
			wide();
			ImGui::SetNextItemWidth(field);
			ImGui::SliderInt("##keep", &s_ColonyKeep, 1, 20, "Keeps %d alive");
			next();
			ImGui::SetNextItemWidth(field);
			UnitOrderCombo("##orders");
		} else if (kind == Tool::Effect) {
			wide();
			ImGui::SetNextItemWidth(field * 1.4F);
			LoadCustomEffects();
			const int builtInEffects = static_cast<int>(EffectKind::Count);
			s_EffectChoice = std::clamp(s_EffectChoice, 0, builtInEffects + static_cast<int>(s_CustomEffects.size()) - 1);
			const char* shownEffect = s_EffectChoice < builtInEffects ? c_Effects[s_EffectChoice].Name : s_CustomEffects[static_cast<size_t>(s_EffectChoice - builtInEffects)].Name.c_str();
			if (ImGui::BeginCombo("##effect", shownEffect, ImGuiComboFlags_HeightLarge)) {
				for (int i = 0; i < builtInEffects; ++i) {
					if (ImGui::Selectable(c_Effects[i].Name, i == s_EffectChoice)) {
						s_EffectChoice = i;
					}
					ImGui::SetItemTooltip("%s", c_Effects[i].Tip);
				}
				for (size_t i = 0; i < s_CustomEffects.size(); ++i) {
					ImGui::PushID(static_cast<int>(i) + 7000);
					if (ImGui::Selectable(s_CustomEffects[i].Name.c_str(), builtInEffects + static_cast<int>(i) == s_EffectChoice)) {
						s_EffectChoice = builtInEffects + static_cast<int>(i);
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			wide();
			ImGui::BeginDisabled(s_Effects.empty());
			if (BarChip("Remove last", false, IM_COL32(239, 106, 91, 255)) == 1) {
				QueueSimChange(Tool::ClearEffects, 1);
			}
			next();
			std::string removeAll = "Remove all " + std::to_string(s_Effects.size());
			if (BarChip(removeAll.c_str(), false, IM_COL32(239, 106, 91, 255)) == 1) {
				QueueSimChange(Tool::ClearEffects);
			}
			ImGui::EndDisabled();
		} else if (kind == Tool::Decor) {
			wide();
			ImGui::SetNextItemWidth(field * 1.4F);
			if (ImGui::BeginCombo("##decor", DecorName(s_DecorChoice))) {
				for (int i = 0; i < static_cast<int>(DecorKind::Count); ++i) {
					if (ImGui::Selectable(DecorName(i), i == s_DecorChoice)) {
						s_DecorChoice = i;
					}
					ImGui::SetItemTooltip("%s", DecorTip(i));
				}
				ImGui::EndCombo();
			}
		} else if (kind == Tool::OrderMove || kind == Tool::RallyPoint) {
			for (int side = 0; side < c_Sides; ++side) {
				side == 0 ? wide() : next();
				ImGui::PushID(side + 60);
				if (BarChip(c_SideNames[side], s_Team == side, c_SideColors[side]) == 1) {
					s_Team = side;
				}
				ImGui::PopID();
			}
		}
		// What is still to come down from the sky.
		if (ShelfOf(kind) && std::string(ShelfOf(kind)->Tab) == "Boom" && !s_Incoming.empty()) {
			wide();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%d on the way", static_cast<int>(s_Incoming.size()));
		}
	}

	/// The shelf: the groups of the tool in hand's part of the sandbox (Paint's liquids, terrain, plants...), the tools of its group, and the
	/// window's panel for the part; then the tool's settings.
	void ShelfTools(BarLine& line, const BarShelf& inHand) {
		float pixel = ToolUI::Pixel();
		// The part's groups, when it has more than one.
		std::vector<const BarShelf*> groups;
		for (const BarShelf& shelf: BarShelves()) {
			if (std::string(shelf.Tab) == inHand.Tab) {
				groups.push_back(&shelf);
			}
		}
		if (groups.size() > 1) {
			for (const BarShelf* group: groups) {
				line.Next(pixel * 2.0F);
				if (BarChip(group->Group, group == &inHand) == 1 && group != &inHand) {
					auto remembered = s_LastToolOfGroup.find(group->Group);
					TakeFromShelf(*group, remembered != s_LastToolOfGroup.end() ? remembered->second : ToolIndex(group->Tools.front()));
				}
			}
			line.Divide();
		}
		// Its tools: right click pins one to the bar, as in the window.
		for (Tool kind: inHand.Tools) {
			int index = ToolIndex(kind);
			const char* about = ToolTipText(kind);
			const char* unavailable = ToolUnavailableReason(kind);
			std::string tip = std::string(c_Tools[index].Name) + (about ? std::string("\n") + about : "") + (unavailable ? std::string("\n\n") + unavailable : "") + (FindPin(kind, "") >= 0 ? "\nRight click: take it off the bar" : "\nRight click: pin it to the bar");
			line.Next(pixel);
			ImGui::PushID(index + 2000);
			int clicked = BarTile("##shelf", nullptr, tip.c_str(), s_ToolIndex == index, [&](ImDrawList* drawList, ImVec2 at, float room) {
				ToolLook look = LookOf(kind);
				if (unavailable) {
					look.Color = (look.Color & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT);
				}
				DrawIcon(drawList, look.Art, at, room / 12.0F, look.Color);
			});
			if (clicked == 1) {
				TakeFromShelf(inHand, index);
			} else if (clicked == 2) {
				TogglePin(kind, "");
			}
			ImGui::PopID();
		}
		line.Next(pixel * 4.0F);
		std::string more = std::string("Everything on the ") + inHand.Tab + " panel, with pictures to pick from: open it, or put it away.";
		if (BarChip(Sandbox::IsOpen() && s_CurrentTab == inHand.Tab ? "Panel <" : "Panel >", Sandbox::IsOpen() && s_CurrentTab == inHand.Tab, ToolTheme::Text, nullptr, more.c_str()) == 1) {
			ToggleWindowTab(inHand.Tab);
		}
	}

	/// The strip of the Command tool, the sandbox's RTS controls: the order the clicks give, with its settings; then the selection, the groups, what
	/// can be done with the units and how they fight.
	void CommandStrip() {
		float pixel = ToolUI::Pixel();
		float field = ImGui::GetFontSize() * 7.0F;
		const ImU32 red = IM_COL32(239, 106, 91, 255);
		// What is selected, by kind.
		std::map<std::string, int> kinds;
		int alive = 0;
		for (const UnitRef& ref: s_Selected) {
			if (const Actor* unit = GetRef(ref)) {
				++kinds[unit->GetPresetName()];
				++alive;
			}
		}
		// With nothing selected there is nothing to order: the clicks select, and the orders wait till some are (a script's command clicks still
		// waiting keep their mode).
		if (alive == 0 && s_CommandMode != CommandMode::Select && std::none_of(s_Queue.begin(), s_Queue.end(), [](const Stroke& queued) { return queued.Kind == Tool::Command; })) {
			s_CommandMode = CommandMode::Select;
			s_PatrolDraft.clear();
		}

		// The first row: what a click does.
		BarLine line;
		line.Next();
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(ToolTheme::Vec(ToolTheme::Gold), "Click to");
		static const char* const keys[] = {"M", "T", "G", "F", "B", "R", "X"};
		static const char* const tips[] = {"Walk to the place clicked, in the formation set.", "Go after the enemy clicked, and keep after it while it lives.", "Stay by the friend, craft, crate or building clicked and fight off what comes at it.", "Walk to the place, fighting whatever they meet on the way.", "Hold a zone round the place clicked and go after any enemy that comes into it.", "Click out a route, point by point; then Loop or Back and forth sets them off.", "Tunnel to the place clicked, through the ground or not (the units with a digger cut the way)."};
		{
			int current = static_cast<int>(s_CommandMode);
			line.Next(pixel * 4.0F);
			const int select = static_cast<int>(CommandMode::Select);
			if (BarChip("Select", current == select, c_CommandModeColors[select], nullptr, "Click a unit to select it, drag a box for several (Shift adds, double click: all of that kind in view).\nA click on the Command tool on the bar while selecting lets them all go.") == 1) {
				s_CommandMode = CommandMode::Select;
			}
			ImGui::BeginDisabled(alive == 0);
			for (int mode = 0; mode < select; ++mode) {
				line.Next(pixel * 2.0F);
				ImGui::PushID(mode + 300);
				if (BarChip(c_CommandModeNames[mode], current == mode, c_CommandModeColors[mode], keys[mode], tips[mode]) == 1) {
					s_CommandMode = static_cast<CommandMode>(mode);
					if (s_CommandMode == CommandMode::Patrol) {
						s_PatrolDraft.clear();
					}
				}
				ImGui::PopID();
			}
			ImGui::EndDisabled();
		}
		// The order's own settings.
		if (s_CommandMode == CommandMode::Move || s_CommandMode == CommandMode::AttackMove) {
			line.Divide();
			line.Next();
			ImGui::SetNextItemWidth(field * 0.8F);
			FormationCombo("##formation");
			line.Next();
			if (BarChip("Keep together", s_KeepPace, ToolTheme::Text, "K", "On: units sent together walk at the pace of the slowest of them till they get there, so the fast ones don't arrive alone.") == 1) {
				s_KeepPace = !s_KeepPace;
			}
			line.Next();
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderFloat("##spacing", &s_Spacing, 8.0F, 60.0F, "Apart %.0f px");
			ImGui::SetItemTooltip("How far apart units stand when sent somewhere together.");
		} else if (s_CommandMode == CommandMode::DefendAt || s_CommandMode == CommandMode::Guard) {
			line.Divide();
			line.Next();
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderInt("##defendRadius", &s_DefendRadius, 30, 600, "Zone %d px");
			ImGui::SetItemTooltip("How far round the place the zone reaches. They stand inside it and go after any enemy that comes into it.");
			line.Next();
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderInt("##defendChase", &s_DefendChase, 0, 1500, "Chase %d px");
			ImGui::SetItemTooltip("How much further than the zone they go after an enemy, before coming back to their posts.");
			line.Next();
			ImGui::SetNextItemWidth(field * 0.7F);
			ImGui::SliderInt("##defendRoam", &s_DefendRoam, 0, 100, "Roam %d%%");
			ImGui::SetItemTooltip("The share of them that walk about the zone from spot to spot, rather than holding a post.");
		} else if (s_CommandMode == CommandMode::Patrol) {
			line.Divide();
			line.Next();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%d points", static_cast<int>(s_PatrolDraft.size()));
			for (int backAndForth = 0; backAndForth < 2; ++backAndForth) {
				line.Next();
				ImGui::BeginDisabled(s_PatrolDraft.size() < 2 || s_Selected.empty());
				ImGui::PushID(backAndForth + 310);
				if (BarChip(backAndForth ? "Back and forth" : "Loop", false, c_CommandModeColors[static_cast<int>(CommandMode::Patrol)], nullptr, backAndForth ? "Along the points to the last, then back the same way, again and again." : "Round the points and back to the first, again and again.") == 1) {
					Stroke stroke;
					stroke.Kind = Tool::Command;
					stroke.Count = backAndForth ? 21 : 20;
					stroke.Position = s_PatrolDraft.front();
					stroke.Points = s_PatrolDraft;
					s_Queue.push_back(stroke);
					s_PatrolDraft.clear();
				}
				ImGui::PopID();
				ImGui::EndDisabled();
			}
			line.Next();
			ImGui::BeginDisabled(s_PatrolDraft.empty());
			if (BarChip("Clear route", false) == 1) {
				s_PatrolDraft.clear();
			}
			ImGui::EndDisabled();
		}
		// What is drawn of the orders, the routes and the groups, kept in the settings.
		line.Divide();
		line.Next();
		if (BarChip("Show...", false, ToolTheme::Text, nullptr, "Order marks over units, routes, \"no route\" markers, under-fire pings, group numbers and the map.") == 1) {
			ImGui::OpenPopup("##commandShow");
		}
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
			ImGui::TextDisabled("Routes");
			int paths = Actor::ShowAIPaths();
			static const char* routeNames[] = {"Never", "Always", "Selected"};
			for (int mode = 0; mode < 3; ++mode) {
				if (mode > 0) {
					ImGui::SameLine();
				}
				int shown = (mode + 1) % 3; // Shown in the order Always, Selected, Never.
				if (ToolUI::RadioButton((std::string(routeNames[shown]) + "##routes").c_str(), &paths, shown)) {
					Actor::SetShowAIPaths(paths);
				}
			}
			ImGui::SetItemTooltip("The paths the units are taking, drawn on the world.");
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
		line.Next();
		bool map = g_SettingsMan.ShowSandboxMinimap();
		if (BarChip("Map", map, ToolTheme::Text, "N", "The whole scene small, with every unit. Click: look there. Drag: select. Right click: order there.") == 1) {
			g_SettingsMan.SetShowSandboxMinimap(!map);
		}

		// The second row: the selection.
		line = BarLine();
		line.Next();
		ImGui::AlignTextToFramePadding();
		if (alive == 0) {
			ImGui::TextDisabled("No units selected");
		} else {
			ImGui::TextColored(ToolTheme::Vec(ToolTheme::Gold), "%d selected", alive);
		}
		// Each kind in it: a click keeps only that kind, a right click lets that kind go.
		{
			int shown = 0;
			std::string keepOnly;
			std::string letGo;
			for (const auto& [name, number]: kinds) {
				if (shown == 4) {
					line.Next();
					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("+%d more kinds", static_cast<int>(kinds.size()) - shown);
					break;
				}
				++shown;
				line.Next(pixel * 2.0F);
				std::string label = std::to_string(number) + " " + name;
				int clicked = BarChip(label.c_str(), false, ToolTheme::Text, nullptr, "Click: keep only these selected.\nRight click: let these go.");
				if (clicked == 1) {
					keepOnly = name;
				} else if (clicked == 2) {
					letGo = name;
				}
			}
			if (!keepOnly.empty() || !letGo.empty()) {
				s_Selected.erase(std::remove_if(s_Selected.begin(), s_Selected.end(),
				                                [&](const UnitRef& ref) {
					                                const Actor* unit = GetRef(ref);
					                                return !unit || (!keepOnly.empty() && unit->GetPresetName() != keepOnly) || (!letGo.empty() && unit->GetPresetName() == letGo);
				                                }),
				                 s_Selected.end());
			}
		}
		// Taking units.
		line.Divide();
		line.Next();
		if (BarChip("All", false, ToolTheme::Text, "Ctrl+A", "Every unit on the selection's side.") == 1) {
			int team = SelectionTeam();
			s_Selected.clear();
			for (Actor* actor: SandboxAccess::Actors()) {
				if (IsSelectable(actor) && actor->GetTeam() == team) {
					s_Selected.push_back(MakeRef(actor));
				}
			}
		}
		line.Next();
		int idleClick = BarChip("Idle", false, ToolTheme::Text, ".", "The next idle unit of the side, selected and looked at (right click: the one before). Shift adds it to the selection.");
		if (idleClick != 0) {
			CycleIdle(idleClick == 1, ImGui::GetIO().KeyShift);
		}
		line.Next();
		ImGui::BeginDisabled(alive == 0);
		if (BarChip("Same kind", false, ToolTheme::Text, "Q", "Every unit in view of the kinds selected.") == 1) {
			SelectKindsInView();
		}
		line.Next();
		if (BarChip("Deselect", false) == 1) {
			s_Selected.clear();
		}
		ImGui::EndDisabled();
		// The control groups kept: a click brings one back, twice quickly looks at it; Ctrl+click keeps the selection under it, right click forgets it.
		{
			bool any = false;
			for (int number = 1; number <= 10; ++number) {
				int group = number % 10;
				int members = 0;
				for (const UnitRef& ref: s_Groups[group]) {
					members += GetRef(ref) ? 1 : 0;
				}
				if (members == 0) {
					continue;
				}
				if (!any) {
					line.Divide();
					line.Next();
					ImGui::AlignTextToFramePadding();
					ImGui::TextDisabled("Groups");
					any = true;
				}
				line.Next(pixel * 2.0F);
				std::string label = std::to_string(group) + ":" + std::to_string(members);
				ImGui::PushID(group + 320);
				int clicked = BarChip(label.c_str(), false, ToolTheme::Text, nullptr, "Click (or its number): bring the group back; twice quickly, look at it.\nCtrl+click: keep the selection under this number instead. Right click: forget the group.");
				if (clicked == 1) {
					if (ImGui::GetIO().KeyCtrl) {
						s_Groups[group] = s_Selected;
					} else {
						s_Selected = s_Groups[group];
						if (ImGui::GetIO().MouseClickedLastCount[ImGuiMouseButton_Left] >= 2) {
							LookAtUnits(s_Selected);
						}
					}
				} else if (clicked == 2) {
					s_Groups[group].clear();
				}
				ImGui::PopID();
			}
			if (!any && alive > 0) {
				line.Next(pixel * 6.0F);
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("Ctrl+1-9: keep as a group");
			}
		}
		// What can be done with them.
		line.Divide();
		ImGui::BeginDisabled(alive == 0);
		auto orderSelected = [](int count) {
			Stroke stroke;
			stroke.Kind = Tool::OrderSelected;
			stroke.Position = MouseScenePosition();
			stroke.Count = count;
			s_Queue.push_back(stroke);
		};
		line.Next();
		if (BarChip("Hold here", false, c_CommandModeColors[static_cast<int>(CommandMode::DefendAt)], "H", "Defend where they stand: fight from there, moving as little as can be.") == 1) {
			orderSelected(103);
		}
		line.Next();
		if (BarChip("Objective", false, c_CommandModeColors[static_cast<int>(CommandMode::AttackMove)], "O", "After their team's objective in the battle: an enemy flag, an enemy VIP, the hill or the objective in play, or the place\ntheir Battle Director card defends; with none, they attack.") == 1) {
			QueueOrder(Order::BattleObjective);
		}
		line.Next();
		if (BarChip("Cancel", false, red, "C", "Every order forgotten, and the side's standing orders (the list below) apply.") == 1) {
			orderSelected(102);
		}
		line.Next();
		if (BarChip("Stop", false, red, nullptr, "Every order they have forgotten (where they were going, what they were after, their plans and patrols, a battle mode's job):\nthey stand where they are and fight back from there.") == 1) {
			orderSelected(121);
		}
		line.Next();
		ImGui::BeginDisabled(PlanMarkers().empty());
		if (BarChip("Clear plans", false, red, nullptr, "Shift with any order adds it to their plans: they carry out each when the one before is over.\nThis clears the steps still to come; each carries on with the one it is on. (Right click a numbered marker drops one step.)") == 1) {
			orderSelected(120);
		}
		ImGui::EndDisabled();
		line.Next();
		if (BarChip("Team orders", false, ToolTheme::Text, nullptr, "Hand them back: everything you told them forgotten, and they take up their team's orders again\n(a battle mode's job, the Battle Director's defend place, or the side's orders).") == 1) {
			orderSelected(122);
		}
		line.Next();
		if (BarChip("Camera", s_FollowTarget.ID != 0 && !s_Selected.empty() && s_FollowTarget.ID == s_Selected.front().ID, ToolTheme::Text, nullptr, "The view follows the first of them.") == 1) {
			s_FollowTarget = s_Selected.empty() ? UnitRef() : s_Selected.front();
			s_FollowAction = false;
		}
		// How they fight: the rule they share, or "Mixed"; a choice gives it to them all.
		line.Divide();
		for (bool weapons: {true, false}) {
			line.Next();
			int rule = SelectedRule(weapons);
			const char* const* names = weapons ? c_WeaponRuleNames : c_MovementRuleNames;
			int ruleCount = weapons ? static_cast<int>(std::size(c_WeaponRuleNames)) : static_cast<int>(std::size(c_MovementRuleNames));
			ImGui::SetNextItemWidth(field * 0.85F);
			if (ImGui::BeginCombo(weapons ? "##weaponRule" : "##movementRule", rule == -1 ? "Mixed" : (rule < 0 ? (weapons ? "Weapons" : "Movement") : names[rule]))) {
				for (int choice = 0; choice < ruleCount; ++choice) {
					if (ImGui::Selectable(names[choice], choice == rule)) {
						QueueRule(weapons, choice);
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SetItemTooltip("%s", weapons ? "What they may shoot at (V: the next).\nFire at will: any enemy they see. Return fire: only while they are being shot at. Hold fire: never; they aim, and open up the moment this changes.\nKept until changed." : "How they move when they meet an enemy (Y: the next).\nAs ordered: a move keeps walking, an attack closes in, a post is held. Engage: stop and fight, closing in. Move only: keep going, firing on the way. Hold ground: fight from where they stand.\nEach new order goes back to As ordered.");
		}
		// The Orders tab's list, for them rather than a whole side.
		line.Divide();
		line.Next();
		s_Order = std::clamp(s_Order, 0, c_OrderCount - 1);
		ImGui::SetNextItemWidth(field);
		ImGui::Combo("##selectedOrders", &s_Order, OrderName, nullptr, c_OrderCount);
		ImGui::SetItemTooltip("Orders for them, as the Orders panel gives a whole side.");
		line.Next(pixel * 2.0F);
		const bool moveTo = static_cast<Order>(s_Order) == Order::MoveTo;
		if (BarChip(moveTo ? "Click where" : "Give", false, ToolTheme::Text, nullptr, moveTo ? "Move to a place: click on the map where they should go." : "Give them the order in the list.") == 1) {
			if (moveTo) {
				s_CommandMode = CommandMode::Move;
			} else {
				QueueOrder(static_cast<Order>(s_Order));
			}
		}
		ImGui::EndDisabled();
	}

	/// The row above the bar's main strip, for the tool in hand: the Command tool's strip; or the tools of its part with its settings; or only its
	/// settings. Returns whether anything was shown.
	bool ShelfRow() {
		const ToolInfo& tool = CurrentTool();
		if (tool.Kind == Tool::Command) {
			CommandStrip();
			return true;
		}
		BarLine line;
		const BarShelf* shelf = ShelfOf(tool.Kind);
		if (shelf) {
			ShelfTools(line, *shelf);
		}
		ToolSettings(line, shelf != nullptr);
		return line.Any;
	}

	/// The sandbox's bar across the bottom of the picture, in the Sandbox game mode while you're above it all: along the bottom the main tools, the side,
	/// the parts of the sandbox, the things you've pinned, and at the right, when the F6 setting shows it, the sides' units, the AI, the speed of time and undo; above them, the tools
	/// and settings of the tool in hand. It is there whether the window is open or not.
	void DrawBar() {
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
		    {"Build", Icon::Wall, IM_COL32(170, 170, 165, 255), "Build: bunker pieces and background lights, placed straight into the world"},
		    {"Paint", Icon::Drop, IM_COL32(90, 170, 240, 255), "Paint: liquids, fire and gas, loose and solid ground, plants and ropes"},
		    {"Boom", Icon::Bomb, IM_COL32(239, 106, 91, 255), "Boom: blasts, force, strikes from the sky"},
		    {"Effects", Icon::Star, IM_COL32(255, 220, 120, 255), "Effects: lights and particle effects to put down"},
		    {"Orders", Icon::Flag, IM_COL32(242, 182, 61, 255), "Orders: orders for whole sides"},
		    {"Battle", Icon::Rocket, IM_COL32(239, 106, 91, 255), "Battle: teams that keep dropping in waves to fight, attack or defend"},
		    {"World", Icon::Cloud, IM_COL32(190, 190, 190, 255), "World: time, weather, the speed of the world, the camera, clearing the map"},
		    {"You", Icon::Person, IM_COL32(130, 220, 120, 255), "You: your own character, what it is, carries and can do"},
		};
		struct MainTool {
			Tool Kind;
			const char* Label;
		};
		static const MainTool mainTools[] = {{Tool::None, "Look"}, {Tool::Command, "Command"}, {Tool::Follow, "Follow"}, {Tool::Possess, "Control"}, {Tool::Remove, "Remove"}, {Tool::RallyPoint, "Rally"}};
		static float rightWidth = 0.0F; // How wide the right-hand group was last frame, to put it against the right edge.

		// Across the whole picture, its bottom on the picture's bottom edge, as tall as its contents.
		ImGui::SetNextWindowPos(ImVec2(view.x, view.y + view.h), ImGuiCond_Always, ImVec2(0.0F, 1.0F));
		ImGui::SetNextWindowSize(ImVec2(std::floor(view.w), 0.0F), ImGuiCond_Always);
		ImGui::PushWrapSameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(8.0F, 8.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pixel * 6.0F, pixel * 4.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pixel * 2.0F, pixel * 3.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pixel * 4.0F, pixel * 2.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
		if (ImGui::Begin("##SandboxBar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
			// Behind every other window: the settings panel and the sandbox window open over it.
			ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			// The strips' backgrounds go under what is on them, drawn once it is known where the shelf ends.
			drawList->ChannelsSplit(2);
			drawList->ChannelsSetCurrent(1);
			const ImVec2 windowPos = ImGui::GetWindowPos();
			const float windowRight = windowPos.x + ImGui::GetWindowWidth();
			const float bottom = view.y + view.h;

			// The shelf, when the tool in hand has one.
			float split = windowPos.y;
			float shelfRight = windowRight;
			if (ShelfRow()) {
				shelfRight = std::min(windowRight, ImGui::GetCurrentWindow()->DC.CursorMaxPos.x + style.WindowPadding.x);
				split = ImGui::GetCursorScreenPos().y - style.ItemSpacing.y + pixel * 3.0F;
				ImGui::Dummy(ImVec2(0.0F, pixel));
			}

			// The main strip. Into your character.
			if (s_Player.EnterOnClose) {
				if (BarTile("##play", "Play", "Play: step into your own character (P). Shift+P puts it down where the mouse points first.", false, [&](ImDrawList* tileList, ImVec2 at, float room) { DrawIcon(tileList, Icon::Person, at, room / 12.0F, IM_COL32(130, 220, 120, 255)); }) == 1) {
					Sandbox::TogglePlay(false);
				}
				BarDivider();
			}
			// The main tools.
			for (size_t i = 0; i < std::size(mainTools); ++i) {
				int index = ToolIndex(mainTools[i].Kind);
				ToolLook look = LookOf(mainTools[i].Kind);
				ImGui::PushID(index);
				if (i > 0) {
					ImGui::SameLine();
				}
				std::string tip = std::string(c_Tools[index].Name) + (ToolTipText(mainTools[i].Kind) ? std::string("\n") + ToolTipText(mainTools[i].Kind) : "");
				if (BarTile("##main", mainTools[i].Label, tip.c_str(), s_ToolIndex == index, [&](ImDrawList* tileList, ImVec2 at, float room) { DrawIcon(tileList, look.Art, at, room / 12.0F, look.Color); }) == 1) {
					if (mainTools[i].Kind == Tool::Command) {
						// The Command tool always starts by selecting units; picked again while selecting, it lets them all go.
						if (s_ToolIndex == index && s_CommandMode == CommandMode::Select) {
							s_Selected.clear();
						}
						s_CommandMode = CommandMode::Select;
						s_PatrolDraft.clear();
					}
					s_ToolIndex = index;
				}
				ImGui::PopID();
			}
			BarDivider();
			// The side things are made for, in its colour: a click goes round the sides.
			{
				std::string tip = std::string("Side: ") + c_SideNames[s_Team] + ". What is spawned and built is theirs.\nClick: the next side. Right click: the one before. (Or hold the right button over the world with a unit in hand for the ring of sides.)";
				int clicked = BarTile(
				    "##side", c_SideNames[s_Team], tip.c_str(), false, [&](ImDrawList* tileList, ImVec2 at, float room) {
					    float inset = room * 0.18F;
					    tileList->AddRectFilled(ImVec2(at.x + inset, at.y + inset), ImVec2(at.x + room - inset, at.y + room - inset), c_SideColors[s_Team]);
					    tileList->AddRect(ImVec2(at.x + inset, at.y + inset), ImVec2(at.x + room - inset, at.y + room - inset), ToolTheme::EdgeDark, 0.0F, 0, pixel);
				    },
				    c_SideColors[s_Team]);
				if (clicked == 1) {
					s_Team = (s_Team + 1) % c_Sides;
				} else if (clicked == 2) {
					s_Team = (s_Team + c_Sides - 1) % c_Sides;
				}
			}
			BarDivider();
			// The parts of the sandbox. One with tools: a click takes them up (the last one used), and the shelf shows the rest; a click on it
			// with them in hand opens the window on it, or puts the window away. One without: the window, on it or away.
			const BarShelf* inHand = ShelfOf(CurrentTool().Kind);
			for (size_t i = 0; i < std::size(parts); ++i) {
				const Part& part = parts[i];
				if (std::string(part.Name) == "You" && !Sandbox::IsGodMode()) {
					continue;
				}
				const BarShelf* firstShelf = nullptr;
				for (const BarShelf& shelf: BarShelves()) {
					if (std::string(shelf.Tab) == part.Name) {
						firstShelf = &shelf;
						break;
					}
				}
				bool showing = Sandbox::IsOpen() && s_CurrentTab == part.Name;
				bool holding = inHand && std::string(inHand->Tab) == part.Name;
				ImGui::PushID(static_cast<int>(i) + 500);
				if (i > 0) {
					ImGui::SameLine();
				}
				std::string tip = std::string(part.Tip) + (firstShelf ? "\nClick: take up its tools (Panel > on the shelf opens the full panel)" : (showing ? "\nClick: put the panel away" : "\nClick: open its panel"));
				if (BarTile("##part", part.Name, tip.c_str(), showing || holding, [&](ImDrawList* tileList, ImVec2 at, float room) { DrawIcon(tileList, part.Art, at, room / 12.0F, part.Color); }) == 1) {
					if (firstShelf && holding) {
						// Its tools are in hand already: the window opens from the shelf's Panel > button, never from here.
					} else if (firstShelf) {
						// The tool last used on this part, if it is one of its own; else its first.
						int toolIndex = ToolIndex(firstShelf->Tools.front());
						if (auto remembered = s_LastToolOfTab.find(part.Name); remembered != s_LastToolOfTab.end()) {
							const BarShelf* rememberedShelf = ShelfOf(c_Tools[remembered->second].Kind);
							if (rememberedShelf && std::string(rememberedShelf->Tab) == part.Name) {
								toolIndex = remembered->second;
							}
						}
						const BarShelf* shelf = ShelfOf(c_Tools[toolIndex].Kind);
						TakeFromShelf(shelf ? *shelf : *firstShelf, toolIndex);
					} else {
						ToggleWindowTab(part.Name);
						if (!showing && std::string(part.Name) == "Orders") {
							s_ToolIndex = ToolIndex(Tool::Command);
						}
					}
				}
				ImGui::PopID();
			}
			// What you've pinned.
			if (!s_Pins.empty()) {
				BarDivider();
			}
			int unpin = -1;
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
				bool pinInHand = s_ToolIndex == toolIndex && (pin.PresetName.empty() || ChoiceFor(pin.Kind) == presetIndex);
				std::string name = pin.PresetName.empty() ? std::string(c_Tools[toolIndex].Name) : pin.PresetName;
				std::string label = name.size() > 10 ? name.substr(0, 9) + "." : name;
				std::string tip = (pin.PresetName.empty() ? name : pin.PresetName + "  (" + c_Tools[toolIndex].Name + ")") + "\nRight click: take it off the bar";
				int clicked = BarTile("##pin", label.c_str(), tip.c_str(), pinInHand, [&](ImDrawList* tileList, ImVec2 at, float room) {
					const PiecePicture* picture = presetIndex >= 0 ? &PictureOf(list[presetIndex]) : nullptr;
					if (picture && picture->Width > 0) {
						float fit = std::min(room / static_cast<float>(picture->Width), room / static_cast<float>(picture->Height));
						if (fit >= 1.0F) {
							fit = std::floor(fit);
						}
						ImVec2 size(static_cast<float>(picture->Width) * fit, static_cast<float>(picture->Height) * fit);
						ImVec2 corner(std::floor(at.x + (room - size.x) * 0.5F), std::floor(at.y + (room - size.y) * 0.5F));
						tileList->AddImage(static_cast<ImTextureID>(picture->Texture), corner, ImVec2(corner.x + size.x, corner.y + size.y));
					} else {
						ToolLook look = LookOf(pin.Kind);
						DrawIcon(tileList, look.Art, at, room / 12.0F, look.Color);
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

			if (g_SettingsMan.ShowSandboxBarRight()) {
				// At the right: each side's units, the AI, the speed of time, undo.
				ImGui::SameLine(0.0F, pixel * 10.0F);
				{
					float contentRight = windowRight - style.WindowPadding.x;
					ImVec2 cursor = ImGui::GetCursorScreenPos();
					if (cursor.x < contentRight - rightWidth) {
						ImGui::SetCursorScreenPos(ImVec2(contentRight - rightWidth, cursor.y));
					}
				}
				const float rightStart = ImGui::GetCursorScreenPos().x;
				for (int side = 0; side < c_Sides; ++side) {
					if (side > 0) {
						ImGui::SameLine();
					}
					int count = Sandbox::CountUnits(side);
					std::string number = std::to_string(count);
					std::string tip = std::string(c_SideNames[side]) + ": " + number + " fighting units (not brains or craft, but a craft's passengers).\nClick: select them all, with the Command tool. Double click: look at them too.";
					ImGui::PushID(side + 700);
					if (BarTile(
					        "##count", c_SideNames[side], tip.c_str(), false, [&](ImDrawList* tileList, ImVec2 at, float room) {
						        float inset = room * 0.08F;
						        tileList->AddRectFilled(ImVec2(at.x + inset, at.y + inset), ImVec2(at.x + room - inset, at.y + room - inset), (c_SideColors[side] & 0x00FFFFFF) | ((count > 0 ? 110u : 50u) << IM_COL32_A_SHIFT));
						        PictureText(tileList, at, room, count > 0 ? ToolTheme::Text : IM_COL32(200, 200, 200, 160), number.c_str());
					        },
					        c_SideColors[side]) == 1) {
						s_Selected.clear();
						for (Actor* actor: SandboxAccess::Actors()) {
							if (IsSelectable(actor) && actor->GetTeam() == side) {
								s_Selected.push_back(MakeRef(actor));
							}
						}
						s_ToolIndex = ToolIndex(Tool::Command);
						s_CommandMode = CommandMode::Select;
						s_PatrolDraft.clear();
						if (ImGui::GetIO().MouseClickedLastCount[ImGuiMouseButton_Left] >= 2) {
							LookAtUnits(s_Selected);
						}
					}
					ImGui::PopID();
				}
				BarDivider();
				{
					// The AI: running, or paused while things are set up.
					bool aiPaused = Controller::IsAIPaused();
					if (BarTile("##ai", aiPaused ? "AI paused" : "AI on", "Pause the AI: everyone stands still while you set things up, then let them loose.", aiPaused, [&](ImDrawList* tileList, ImVec2 at, float room) {
						    if (aiPaused) {
							    float bar = room * 0.18F;
							    tileList->AddRectFilled(ImVec2(at.x + room * 0.25F, at.y + room * 0.2F), ImVec2(at.x + room * 0.25F + bar, at.y + room * 0.8F), IM_COL32(255, 210, 80, 255));
							    tileList->AddRectFilled(ImVec2(at.x + room * 0.75F - bar, at.y + room * 0.2F), ImVec2(at.x + room * 0.75F, at.y + room * 0.8F), IM_COL32(255, 210, 80, 255));
						    } else {
							    tileList->AddTriangleFilled(ImVec2(at.x + room * 0.28F, at.y + room * 0.18F), ImVec2(at.x + room * 0.82F, at.y + room * 0.5F), ImVec2(at.x + room * 0.28F, at.y + room * 0.82F), IM_COL32(130, 220, 120, 255));
						    }
					    }) == 1) {
						Controller::SetAIPaused(!aiPaused);
					}
				}
				ImGui::SameLine();
				{
					// How fast time runs: a click goes up through the speeds, a right click back to normal.
					static const float speeds[] = {0.25F, 0.5F, 1.0F, 2.0F, 3.0F};
					float timeScale = g_TimerMan.GetTimeScale();
					char shown[16];
					std::snprintf(shown, sizeof(shown), "%.3gx", timeScale);
					int clicked = BarTile("##speed", "Speed", "How fast time runs. Click: faster (0.25x, 0.5x, 1x, 2x, 3x, and round again). Right click: back to 1x.\nThe World panel has a slider for any speed.", timeScale < 0.99F || timeScale > 1.01F, [&](ImDrawList* tileList, ImVec2 at, float room) { PictureText(tileList, at, room, ToolTheme::Text, shown); });
					if (clicked == 1) {
						float nextSpeed = speeds[0];
						for (float speed: speeds) {
							if (speed > timeScale + 0.01F) {
								nextSpeed = speed;
								break;
							}
						}
						g_TimerMan.SetTimeScale(nextSpeed);
					} else if (clicked == 2) {
						g_TimerMan.SetTimeScale(1.0F);
					}
				}
				if (s_PausedByMenus) {
					// The world stands still while the window is open: a step at a time.
					ImGui::SameLine();
					if (BarTile("##step", "Step", "Lets the world move one update, a sixtieth of a second. Ctrl+click: a second's worth.", false, [&](ImDrawList* tileList, ImVec2 at, float room) {
						    tileList->AddTriangleFilled(ImVec2(at.x + room * 0.2F, at.y + room * 0.2F), ImVec2(at.x + room * 0.62F, at.y + room * 0.5F), ImVec2(at.x + room * 0.2F, at.y + room * 0.8F), ToolTheme::Text);
						    tileList->AddRectFilled(ImVec2(at.x + room * 0.66F, at.y + room * 0.2F), ImVec2(at.x + room * 0.8F, at.y + room * 0.8F), ToolTheme::Text);
					    }) == 1) {
						s_StepsWanted += ImGui::GetIO().KeyCtrl ? 60 : 1;
					}
				}
				ImGui::SameLine();
				{
					// Undo: the last paint stroke or thing placed.
					bool canUndo = !s_PaintUndo.empty();
					if (BarTile("##undo", "Undo", canUndo ? "Takes back the last brush stroke or the last thing placed (Ctrl+Z)." : "Nothing to undo: brush strokes and things placed can be taken back (Ctrl+Z).", false, [&](ImDrawList* tileList, ImVec2 at, float room) {
						    ImU32 ink = canUndo ? ToolTheme::Text : IM_COL32(200, 200, 200, 80);
						    float thick = std::max(pixel * 1.5F, 1.5F);
						    ImVec2 points[] = {ImVec2(at.x + room * 0.78F, at.y + room * 0.82F), ImVec2(at.x + room * 0.78F, at.y + room * 0.38F), ImVec2(at.x + room * 0.36F, at.y + room * 0.38F)};
						    tileList->AddPolyline(points, 3, ink, ImDrawFlags_None, thick);
						    tileList->AddTriangleFilled(ImVec2(at.x + room * 0.12F, at.y + room * 0.38F), ImVec2(at.x + room * 0.4F, at.y + room * 0.16F), ImVec2(at.x + room * 0.4F, at.y + room * 0.6F), ink);
					    }) == 1 &&
					    canUndo) {
						QueueSimChange(Tool::UndoTerrain);
					}
				}
				rightWidth = ImGui::GetItemRectMax().x - rightStart;
			} else {
				rightWidth = 0.0F;
			}

			// The backgrounds: the main strip full width; the shelf above it, as wide as what is on it.
			drawList->ChannelsSetCurrent(0);
			drawList->AddRectFilled(ImVec2(windowPos.x, split), ImVec2(windowRight, bottom), (ToolTheme::Panel & 0x00FFFFFF) | (245u << IM_COL32_A_SHIFT));
			drawList->AddRectFilled(ImVec2(windowPos.x, split), ImVec2(windowRight, split + pixel), ToolTheme::EdgeDark);
			drawList->AddRectFilled(ImVec2(windowPos.x, split + pixel), ImVec2(windowRight, split + pixel * 2.0F), (ToolTheme::Edge & 0x00FFFFFF) | (200u << IM_COL32_A_SHIFT));
			if (split > windowPos.y) {
				drawList->AddRectFilled(ImVec2(windowPos.x, windowPos.y), ImVec2(shelfRight, split), (ToolTheme::EdgeDark & 0x00FFFFFF) | (235u << IM_COL32_A_SHIFT));
				drawList->AddRectFilled(ImVec2(windowPos.x, windowPos.y), ImVec2(shelfRight, windowPos.y + pixel), (ToolTheme::Edge & 0x00FFFFFF) | (200u << IM_COL32_A_SHIFT));
				drawList->AddRectFilled(ImVec2(shelfRight - pixel, windowPos.y), ImVec2(shelfRight, split), (ToolTheme::Edge & 0x00FFFFFF) | (200u << IM_COL32_A_SHIFT));
			}
			drawList->ChannelsMerge();
			s_BarHeight = bottom - windowPos.y;
		}
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(6);
		ImGui::PopWrapSameLine();
	}
} // namespace SandboxDetail
