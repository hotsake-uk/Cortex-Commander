#pragma once

// Shared by the sandbox's source files (Sandbox.cpp and the Sandbox*.cpp units beside it): the sandbox's state, its types and the
// helpers each unit calls in another. Not for use outside the sandbox. The state is C++17 inline variables, one copy for the program.

#include "WindowMan.h"
#include "DebugMan.h"
#include "DebugDraw.h"
#include "Sandbox.h"
#include "ACrab.h"
#include "ACraft.h"
#include "ADoor.h"
#include "AHuman.h"
#include "ActivityMan.h"
#include "CameraMan.h"
#include "Colony.h"
#include "ConsoleMan.h"
#include "Controller.h"
#include "Constants.h"
#include "EffectsParticles.h"
#include "FluidSim.h"
#include "FrameMan.h"
#include "GameActivity.h"
#include "HDFirearm.h"
#include "Material.h"
#include "MovableMan.h"
#include "PostProcessMan.h"
#include "PresetMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "SceneLighting.h"
#include "SceneMan.h"
#include "SettingsMan.h"
#include "Reader.h"
#include "SoundContainer.h"
#include "TDExplosive.h"
#include "TerrainCollapse.h"
#include "RopeSim.h"
#include "ActorFire.h"
#include "TerrainFire.h"
#include "WeatherLightning.h"
#include "TerrainObject.h"
#include "TerrainDebris.h"
#include "TimerMan.h"
#include "UInputMan.h"
#include "AEJetpack.h"
#include "Magazine.h"
#include "imgui/imgui.h"
#include "ToolWidgets.h"
#include "glad/gl.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstring>
#include <cctype>
#include <cmath>
#include <execution>
#include <initializer_list>
#include <list>
#include <deque>
#include <map>
#include <unordered_map>
#include <climits>
#include <unordered_set>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace RTE;

namespace RTE {
	/// Lets the sandbox's helper functions reach its private parts.
	struct SandboxAccess {
		static std::deque<Actor*>& Actors() { return Sandbox::Actors(); }
		static std::deque<MovableObject*>& Items() { return Sandbox::Items(); }
	};
} // namespace RTE

namespace SandboxDetail {
	enum class Tool {
		None,
		Possess,
		Remove,
		RallyPoint,
		Unit,
		Brain,
		Item,
		Structure,
		Drop,
		Command,
		Follow,
		Fire,
		Water,
		Lava,
		Acid,
		Oil,
		WaterSpawner,
		LooseSand,
		LooseSnow,
		Boulder,
		Slab,
		Smoke,
		ToxicGas,
		Dig,
		Earth,
		Sand,
		Ice,
		Grass,
		Wood,
		Concrete,
		Grenade,
		BigBomb,
		Demolition,
		BunkerBuster,
		Meteor,
		RocketStrike,
		RocketBarrage,
		CarpetBomb,
		Artillery,
		NapalmRain,
		OrbitalBeam,
		BoulderRain,
		CrashRocket,
		CrashDropship,
		Effect,
		BuildBeam,
		BuildPillar,
		BuildRoom,
		BuildTower,
		BuildIsland,
		BuildTank,
		BuildBridge,
		Napalm,
		Lightning,
		PlayCharacter,
		Barracks,
		Extractor,
		OrderMove,
		GymStart, //!< The gym: where a course starts.
		GymGoal, //!< The gym: where a course ends.
		// Not tools, but queued the same way.
		PlayerRemake,
		PlayerRemove,
		OrderSide,
		RemoveSide,
		Release,
		Select,
		OrderSelected,
		GymRun, //!< Count: the course to run, or -1 for all of them.
		GymRemove,
		ClearWaterSpawners,
		ClearEffects, //!< Count: 1 the last one only, else all.
		UndoTerrain, //!< Takes back the newest step of the undo history, a paint stroke or a placing click (see s_PaintUndo).
		BattleTeam, //!< The Battle Director: Team's settings (Battle) set, and Count a BattleCommand. (Was the auto battle's, AutoBattle: renamed, so the tools after keep their numbers.)
		// The new liquids and loose materials (SB-2), poured like water: appended, so the tools before keep their numbers.
		Mud,
		Tar,
		Mercury,
		Gravel,
		GlassShards,
		Fuel,
		Cryo,
		Blood, //!< Pours blood, turning flowing blood on (FluidSim::BloodFlows) if it is off.
		PourOther, //!< Pours the liquid or powder chosen under "More..." (Stroke::Material).
		BattleDefendPoint, //!< The Battle Director: a click sets the place the team being set up defends (s_BattleEditTeam).
		BattleDropLine, //!< The Battle Director: a drag draws the line the team's ships come in over (s_BattleEditTeam).
		BattleSpawnZone, //!< The Battle Director: each click puts down a corner of a spawn zone for the team (s_BattleEditTeam); a click on the first corner, or Enter, closes it.
		BattleModePoint, //!< The Battle Director's modes: a click sets the team's point (s_BattleEditTeam), as capture the flag's flag.
		BattleModeBase, //!< The Battle Director's modes: each click puts down a corner of another of the team's spawn zones (s_BattleEditTeam).
		BattleModeZone, //!< The Battle Director's modes: each click puts down a corner of one of the mode's own zones (a hill, an objective).
		BattleModeGoal, //!< The Battle Director's modes: each click puts down a corner of the team's goal zone (s_BattleEditTeam), as one flag's.
		BattleModeFlag, //!< The Battle Director's modes: a click sets where the one neutral flag stands (one flag).
		ClearMap, //!< Clears one kind of thing off the whole map (Count a ClearKind): the World tab's Clear. Appended, so the tools before keep their numbers.
		// More terrain to paint, from the base game's materials: appended, so the tools before keep their numbers.
		Stone,
		DenseEarth, //!< The base game's "Dense Earth": darker, tougher earth.
		GoldEarth, //!< Earth with gold in it, as the base game's scenes have (c_GoldEarthShare of it gold).
		TerrainOther, //!< Paints the terrain material chosen under "More terrain..." (Stroke::Material).
		// Plants, drawn from the base game's own plant pictures (its "Plants", "Cacti" and "Small Cacti" terrain debris): appended, so the tools before keep their numbers.
		Plants,
		Cacti,
		// Gases that live in the gas grid (SB-6): appended, so the tools before keep their numbers.
		Methane,
		Steam,
		// More plants (appended, so the tools before keep their numbers): the base game's mushrooms, and trees drawn for the sandbox (Tools/MakeTreeSprites.py).
		Mushrooms,
		Trees,
		// Appended, so the tools before keep their numbers.
		TreeTrunk, //!< The base game's "Tree Trunk": wood, darker, like a tree's.
		// Appended, so the tools before keep their numbers.
		Generator, //!< A colony generator: powers its side's buildings in range (Colony::NeedsPower).
		// Appended, so the tools before keep their numbers.
		GrowGrass, //!< Grows a layer of grass up from the top of the ground under the brush, as the base game's maps have on their topsoil.
		// Appended, so the tools before keep their numbers.
		Candles, //!< Puts candles on the ground (Tools/MakeCandleSprites.py): wax with a wick, which fire lights and which burn down (TerrainCandle).
		// Appended, so the tools before keep their numbers.
		Metal, //!< Paints the metal chosen under Metals (c_PaintMetals; Stroke::Material): the bunkers' plating, or a polished metal that shines.
		// Appended, so the tools before keep their numbers.
		CollapseArea, //!< A box dragged out on the world (Position to Position2): all the ground in it breaks loose and falls (TerrainCollapse::DropArea).
		// Appended, so the tools before keep their numbers.
		Rope, //!< Each click a point of a rope (RopeSim), tied to what is there; Choice 1 finishes it, 2 takes every rope away. Material: its kind; Rate: its slack.
		RopeCut, //!< A click cuts the ropes under the pointer.
		// The Boom tab's blasts of force, which shove and scatter but burn and harm nothing: appended, so the tools before keep their numbers.
		ForceBlast, //!< A burst that throws units, things, debris and smoke out from the point.
		HugeForceBlast, //!< The same, wider and harder.
		Implosion, //!< The reverse: everything near is pulled in to the point.
		Updraft, //!< A column of air that lifts what is over the point.
		GustRight, //!< A gale across the point, to the right.
		GustLeft, //!< A gale across the point, to the left.
		SmokeBomb, //!< A burst of thick smoke, with no blast.
		Fireworks, //!< Bursts of coloured sparks in the air above the point.
		// Appended, so the tools before keep their numbers.
		Decor //!< A light or fire put in the background (Choice a DecorKind): it shines until a blast or a shot destroys it, and nothing collides with it.
	};

	/// What the World tab's Clear takes off the map (Tool::ClearMap's Count).
	enum class ClearKind {
		Buildings, //!< Doors, the bunker parts put down and colony buildings; with Choice 1, the building materials too (c_BuildingMaterials).
		Liquids, //!< The liquids in Stroke::Materials; with Choice 1, the springs that pour them too.
		Units, //!< Every unit of the side in Team, or of every side with -1. Not doors, nor your character.
		Ground //!< The terrain materials in Stroke::Materials.
	};

	/// What bunkers and the things the sandbox builds are made of, cleared with the buildings.
	constexpr const char* c_BuildingMaterials[] = {"Concrete", "Metal", "Mega Metal", "Mangled Metal", "Door Metal", "Scrap Metal", "Glass", "Civilian Stuff", "Military Stuff", "Ladder", "Gold Plate", "Silver Plate", "Bronze Plate", "Brass Plate", "Copper Plate", "Chrome Plate"};

	/// The Battle tab's tools that set something on a team's card, taken from it and put down with Enter (PutDownBattleTool).
	constexpr bool IsBattleTool(Tool kind) { return kind == Tool::BattleDefendPoint || kind == Tool::BattleDropLine || kind == Tool::BattleSpawnZone || kind == Tool::BattleModePoint || kind == Tool::BattleModeBase || kind == Tool::BattleModeZone || kind == Tool::BattleModeGoal || kind == Tool::BattleModeFlag; }

	/// The Battle Director's mode tools that draw a polygon a corner at a click (closed on its first corner, or Enter).
	constexpr bool IsModeZoneTool(Tool kind) { return kind == Tool::BattleModeBase || kind == Tool::BattleModeZone || kind == Tool::BattleModeGoal; }

	struct ToolInfo {
		Tool Kind;
		const char* Name;
		float Interval; //!< Seconds between strokes while the button is held; 0 means once per click.
		bool UsesRadius;
	};

	constexpr ToolInfo c_Tools[] = {
	    {Tool::None, "Look around", 0.0F, false},
	    {Tool::Possess, "Take control", 0.0F, false},
	    {Tool::Remove, "Remove", 0.0F, false},
	    {Tool::RallyPoint, "Rally point", 0.0F, false},
	    {Tool::Unit, "Units", 0.0F, false},
	    {Tool::Brain, "Brain", 0.0F, false},
	    {Tool::Item, "Item", 0.0F, false},
	    {Tool::Structure, "Structure", 0.0F, false},
	    {Tool::Drop, "Drop squad", 0.0F, false},
	    {Tool::Command, "Command", 0.0F, false},
	    {Tool::Follow, "Follow", 0.0F, false},
	    {Tool::Fire, "Fire", 0.08F, true},
	    {Tool::Water, "Water", 0.03F, true},
	    {Tool::Lava, "Lava", 0.03F, true},
	    {Tool::Acid, "Acid", 0.03F, true},
	    {Tool::Oil, "Oil", 0.03F, true},
	    {Tool::WaterSpawner, "Spring", 0.0F, true},
	    {Tool::LooseSand, "Loose sand", 0.03F, true},
	    {Tool::LooseSnow, "Loose snow", 0.03F, true},
	    {Tool::Boulder, "Boulder", 0.0F, true},
	    {Tool::Slab, "Concrete lump", 0.0F, true},
	    {Tool::Smoke, "Smoke", 0.06F, true},
	    {Tool::ToxicGas, "Toxic gas", 0.06F, true},
	    {Tool::Dig, "Dig", 0.03F, true},
	    {Tool::Earth, "Earth", 0.03F, true},
	    {Tool::Sand, "Sand", 0.03F, true},
	    {Tool::Ice, "Ice", 0.03F, true},
	    {Tool::Grass, "Grass", 0.03F, true},
	    {Tool::Wood, "Wood", 0.03F, true},
	    {Tool::Concrete, "Concrete", 0.03F, true},
	    {Tool::Grenade, "Grenade blast", 0.0F, false},
	    {Tool::BigBomb, "Big bomb", 0.0F, false},
	    {Tool::Demolition, "Demolition charge", 0.0F, false},
	    {Tool::BunkerBuster, "Bunker buster", 0.0F, false},
	    {Tool::Meteor, "Meteor strike", 0.0F, false},
	    {Tool::RocketStrike, "Rocket strike", 0.0F, false},
	    {Tool::RocketBarrage, "Rocket barrage", 0.0F, false},
	    {Tool::CarpetBomb, "Carpet bombing", 0.0F, false},
	    {Tool::Artillery, "Artillery", 0.0F, false},
	    {Tool::NapalmRain, "Napalm rain", 0.0F, false},
	    {Tool::OrbitalBeam, "Orbital beam", 0.0F, false},
	    {Tool::BoulderRain, "Boulder rain", 0.0F, false},
	    {Tool::CrashRocket, "Crashing rocket", 0.0F, false},
	    {Tool::CrashDropship, "Crashing dropship", 0.0F, false},
	    {Tool::Effect, "Effect", 0.0F, false},
	    {Tool::BuildBeam, "Concrete beam", 0.0F, false},
	    {Tool::BuildPillar, "Concrete pillar", 0.0F, false},
	    {Tool::BuildRoom, "Concrete room", 0.0F, false},
	    {Tool::BuildTower, "Tower", 0.0F, false},
	    {Tool::BuildIsland, "Floating island", 0.0F, false},
	    {Tool::BuildTank, "Tank of water", 0.0F, false},
	    {Tool::BuildBridge, "Wooden bridge", 0.0F, false},
	    {Tool::Napalm, "Napalm burst", 0.0F, false},
	    {Tool::Lightning, "Lightning", 0.0F, false},
	    {Tool::PlayCharacter, "Play from here", 0.0F, false},
	    {Tool::Barracks, "Barracks", 0.0F, false},
	    {Tool::Extractor, "Extractor", 0.0F, false},
	    {Tool::OrderMove, "Move a side here", 0.0F, false},
	    {Tool::GymStart, "Gym start", 0.0F, false},
	    {Tool::GymGoal, "Gym goal", 0.0F, false},
	    {Tool::Mud, "Mud", 0.03F, true},
	    {Tool::Tar, "Tar", 0.03F, true},
	    {Tool::Mercury, "Mercury", 0.03F, true},
	    {Tool::Gravel, "Gravel", 0.03F, true},
	    {Tool::GlassShards, "Glass shards", 0.03F, true},
	    {Tool::Fuel, "Fuel", 0.03F, true},
	    {Tool::Cryo, "Cryogenic fluid", 0.03F, true},
	    {Tool::Blood, "Blood", 0.03F, true},
	    {Tool::PourOther, "Other", 0.03F, true},
	    {Tool::BattleDefendPoint, "Defence point", 0.0F, false},
	    {Tool::BattleDropLine, "Drop line", 0.0F, false},
	    {Tool::BattleSpawnZone, "Spawn zone", 0.0F, false},
	    {Tool::BattleModePoint, "Flag", 0.0F, false},
	    {Tool::BattleModeBase, "Team's spawn zone", 0.0F, false},
	    {Tool::BattleModeZone, "Mode zone", 0.0F, false},
	    {Tool::BattleModeGoal, "Team's goal zone", 0.0F, false},
	    {Tool::BattleModeFlag, "Neutral flag", 0.0F, false},
	    {Tool::Stone, "Stone", 0.03F, true},
	    {Tool::DenseEarth, "Dark earth", 0.03F, true},
	    {Tool::GoldEarth, "Earth with gold", 0.03F, true},
	    {Tool::TerrainOther, "Other terrain", 0.03F, true},
	    {Tool::Plants, "Plants", 0.03F, true},
	    {Tool::Cacti, "Cacti", 0.03F, true},
	    {Tool::Mushrooms, "Mushrooms", 0.03F, true},
	    {Tool::Trees, "Trees", 0.03F, true},
	    {Tool::Methane, "Methane", 0.06F, true},
	    {Tool::Steam, "Steam", 0.06F, true},
	    {Tool::TreeTrunk, "Tree trunk", 0.03F, true},
	    {Tool::Generator, "Generator", 0.0F, false},
	    {Tool::GrowGrass, "Grow grass", 0.03F, true},
	    {Tool::Candles, "Candles", 0.03F, true},
	    {Tool::Metal, "Metal", 0.03F, true},
	    {Tool::CollapseArea, "Make it fall", 0.0F, false},
	    {Tool::Rope, "Rope", 0.0F, false},
	    {Tool::RopeCut, "Cut rope", 0.0F, false},
	    {Tool::ForceBlast, "Force blast", 0.0F, false},
	    {Tool::HugeForceBlast, "Huge force blast", 0.0F, false},
	    {Tool::Implosion, "Implosion", 0.0F, false},
	    {Tool::Updraft, "Updraft", 0.0F, false},
	    {Tool::GustRight, "Gust right", 0.0F, false},
	    {Tool::GustLeft, "Gust left", 0.0F, false},
	    {Tool::SmokeBomb, "Smoke bomb", 0.0F, false},
	    {Tool::Fireworks, "Fireworks", 0.0F, false},
	    {Tool::Decor, "Background light", 0.0F, false},
	};
	constexpr int c_ToolCount = static_cast<int>(std::size(c_Tools));

	inline int ToolIndex(Tool kind) {
		for (int i = 0; i < c_ToolCount; ++i) {
			if (c_Tools[i].Kind == kind) {
				return i;
			}
		}
		return 0;
	}

	/// The Paint tab's terrain brushes: dig and the materials painted into the air, the ones the brush shape (s_BrushShape) is for.
	constexpr bool IsTerrainBrush(Tool kind) {
		switch (kind) {
			case Tool::Dig:
			case Tool::Earth:
			case Tool::Sand:
			case Tool::Ice:
			case Tool::Grass:
			case Tool::Wood:
			case Tool::TreeTrunk:
			case Tool::Concrete:
			case Tool::Stone:
			case Tool::DenseEarth:
			case Tool::GoldEarth:
			case Tool::TerrainOther:
			case Tool::Metal:
			case Tool::GrowGrass:
				return true;
			default:
				return false;
		}
	}

	/// The plant brushes: each puts the game's own plant pictures on the ground along the stroke, s_PlantSpacing apart.
	constexpr bool IsPlantBrush(Tool kind) { return kind == Tool::Plants || kind == Tool::Cacti || kind == Tool::Mushrooms || kind == Tool::Trees || kind == Tool::Candles; }

	/// How many rows at the bottom of a tree picture are its roots, set into the ground (as Tools/MakeTreeSprites.py draws them).
	constexpr int c_TreeRootDepth = 12;

	/// How the terrain brushes lay down what they paint or dig (Paint > Terrain).
	enum class BrushShape {
		Circle,
		Square, //!< A square of the brush size either way of the point.
		Spray //!< A soft spray: scattered pixels over the circle, thickest in the middle, building up while held.
	};

	/// The shapes the terrain brushes fill with Brush type Shape (s_ShapeFill): dragged out as a box, filled in one go.
	enum class FillShape {
		Circle, //!< The circle (or oval) in the box.
		Triangle, //!< Its point at the side of the box the drag began, its base along the other.
		Square //!< The whole box.
	};

	/// Whether a pixel is in a filled shape dragged out from start to end (scene pixels, end not wrapped round from start).
	inline bool InFillShape(FillShape shape, const Vector& start, const Vector& end, int x, int y) {
		float left = std::min(start.m_X, end.m_X);
		float right = std::max(start.m_X, end.m_X);
		float top = std::min(start.m_Y, end.m_Y);
		float bottom = std::max(start.m_Y, end.m_Y);
		float px = static_cast<float>(x) + 0.5F;
		float py = static_cast<float>(y) + 0.5F;
		if (px < left || px > right + 1.0F || py < top || py > bottom + 1.0F) {
			return false;
		}
		float halfWidth = std::max((right + 1.0F - left) * 0.5F, 0.5F);
		float halfHeight = std::max((bottom + 1.0F - top) * 0.5F, 0.5F);
		float middleX = left + halfWidth;
		switch (shape) {
			case FillShape::Circle: {
				float nx = (px - middleX) / halfWidth;
				float ny = (py - (top + halfHeight)) / halfHeight;
				return nx * nx + ny * ny <= 1.0F;
			}
			case FillShape::Triangle: {
				// Dragged down: the point at the top. Dragged up: at the bottom.
				bool pointUp = end.m_Y >= start.m_Y;
				float along = (pointUp ? py - top : bottom + 1.0F - py) / (halfHeight * 2.0F);
				return std::abs(px - middleX) <= along * halfWidth;
			}
			default:
				return true;
		}
	}

	/// How much of what the "Earth with gold" brush paints is gold.
	constexpr float c_GoldEarthShare = 0.06F;

	/// The metals offered under the Paint tab's Metals: the material painted, its button's name and colour, and what it is, for the tooltip.
	struct PaintMetal {
		const char* Material;
		const char* Name;
		unsigned char R, G, B;
		const char* About;
	};
	constexpr PaintMetal c_PaintMetals[] = {
	    {"Metal", "Steel", 175, 189, 199, "The bunkers' own metal: plated, bolted and as tough. Shines a little."},
	    {"Mega Metal", "Heavy plate", 205, 215, 220, "The bunkers' toughest metal, as tread plate: half as strong again as steel, and glossier."},
	    {"Scrap Metal", "Scrap", 150, 120, 100, "Rusted, battered plates: weak, and it breaks up like debris."},
	    {"Gold Plate", "Gold", 240, 200, 60, "Polished gold plating: as strong as steel, and it glints gold in the sun and in lamplight. Not gold to dig for funds."},
	    {"Silver Plate", "Silver", 200, 205, 210, "Polished silver plating: mirrors the sky and glints white."},
	    {"Bronze Plate", "Bronze", 180, 110, 45, "Bronze plating: a warm, softer sheen."},
	    {"Brass Plate", "Brass", 200, 185, 75, "Polished brass plating: glints a greenish gold."},
	    {"Copper Plate", "Copper", 190, 95, 50, "Copper plating: glints a rosy orange."},
	    {"Chrome Plate", "Chrome", 215, 230, 240, "Mirror-bright chrome: shows the sky above it and the dark ground below, and flashes in the light."}};

	/// The textures metals the base game gives none are painted with in the sandbox, so a stroke of them looks like the bunkers' plating
	/// rather than a flat colour (Tools/MakeMetalTextures.py). Their materials are left as they are, so the maps that use them don't change.
	struct PaintTexture {
		const char* Material;
		const char* Path;
	};
	constexpr PaintTexture c_PaintTextures[] = {
	    {"Metal", "Base.rte/Scenes/Textures/Metals/SteelPlate.png"},
	    {"Mega Metal", "Base.rte/Scenes/Textures/Metals/TreadPlate.png"},
	    {"Scrap Metal", "Base.rte/Scenes/Textures/Metals/ScrapPlate.png"}};

	/// The base game's ground materials offered under "More terrain..." (those a game doesn't have are left out).
	constexpr const char* c_TerrainMaterials[] = {"Topsoil", "Earth", "Dense Earth", "Stone", "Bedrock", "Gold", "Red Earth", "Dense Red Earth", "Red Stone", "Lunar Earth", "Dense Lunar Earth", "Lunar Stone", "Snow", "Dense Snow", "Ice", "Sand", "Cave Floor", "Cave Ceiling", "Grass", "Vegetation", "Wood", "Tree Trunk", "Charcoal", "Concrete", "Metal", "Scrap Metal", "Glass", "Sandbag", "Rubber"};

	/// The Paint tab's tools (its brushes, loose things, springs and terrain): with one in hand the right button digs (see Sandbox::DrawGUI).
	inline bool IsPaintTool(Tool kind) { return c_Tools[ToolIndex(kind)].UsesRadius; }

	constexpr int c_Sides = 4;
	// The game's own team colours, as on the team icons over units' heads.
	constexpr const char* c_SideNames[c_Sides] = {"Red", "Green", "Blue", "Yellow"};
	constexpr ImU32 c_SideColors[c_Sides] = {IM_COL32(249, 120, 100, 255), IM_COL32(170, 210, 100, 255), IM_COL32(110, 180, 250, 255), IM_COL32(248, 230, 100, 255)};

	enum class Order {
		Hold,
		Attack,
		HuntBrains,
		Patrol,
		Rally,
		Idle,
		DigGold,
		BattleObjective,
		MoveTo
	};
	/// What the sandbox says about an order, by Order: one table for the names and for where each may be given, in place of name lists and
	/// clamps kept in step by hand.
	struct OrderInfo {
		const char* Name;
		bool ForUnit; //!< A unit can be made with it (placed, a barracks' trainees, a script's units). Not "Move to a place", which needs a place clicked.
	};
	constexpr OrderInfo c_Orders[] = {
	    {"Hold position", true},
	    {"Attack nearest enemy", true},
	    {"Hunt brains", true},
	    {"Patrol", true},
	    {"Go to rally point", true},
	    {"Do nothing", true},
	    {"Dig for gold", true},
	    {"Battle objective", true},
	    {"Move to a place", false},
	};
	constexpr int c_OrderCount = static_cast<int>(sizeof(c_Orders) / sizeof(c_Orders[0]));
	static_assert(c_OrderCount == static_cast<int>(Order::MoveTo) + 1, "c_Orders must have an entry for each Order, in its order.");
	/// How many orders, from the first, a unit can be made with: the unit order lists offer these. (They come first, so a list's index is the order.)
	constexpr int c_UnitOrderCount = [] {
		int count = 0;
		while (count < c_OrderCount && c_Orders[count].ForUnit) {
			++count;
		}
		return count;
	}();
	static_assert([] {
		for (int order = c_UnitOrderCount; order < c_OrderCount; ++order) {
			if (c_Orders[order].ForUnit) {
				return false;
			}
		}
		return true;
	}(), "The orders a unit can be made with must come first in c_Orders.");
	inline const char* OrderName(void*, int order) { return c_Orders[order].Name; }
	/// An order number from outside (a script, a saved barracks) as an order a unit can be made with: anything else holds its position.
	/// (Offered all eight, a barracks told "Dig for gold" or "Move to a place" trained units that did nothing.)
	inline Order UnitOrder(int order) {
		return order >= 0 && order < c_OrderCount && c_Orders[order].ForUnit ? static_cast<Order>(order) : Order::Hold;
	}
	// (What a unit was told to do is its standing order, Actor::GetStandingOrder: attack, a target to keep after, the enemy picked for it, a place
	// to attack towards, a post to defend, hold. Once six number values under string keys here and in the AI scripts.)
	constexpr const char* c_RetreatTag = "AIRetreat"; //!< Number values the Lua AI keeps on a unit falling back or working round a flank; taken off
	constexpr const char* c_FlankTag = "AIFlank";     //!< by a new order, which tells the AI the order it would put back after is gone.
	constexpr const char* c_MedicTag = "AIMedic";     //!< Likewise a medic on its way to see to a hurt friend (AC-7).

	/// A new order ends a fall-back or a flank under way: the AI drops it without putting the old order back.
	inline void CancelRetreatAndFlank(Actor* unit) {
		unit->RemoveNumberValue(c_RetreatTag);
		unit->RemoveNumberValue(c_FlankTag);
		unit->RemoveNumberValue(c_MedicTag);
	}

	/// A preset the sandbox can spawn.
	/// How high, in metres, a unit's jetpack has to lift it to count as flying, for "Jetpacks only". Jetpacks that only fake a hop (as
	/// many mods' units have, from before units could leap) lift a couple of metres; ones that fly lift well over ten.
	constexpr float c_JetpackFlyingLift = 5.0F;

	struct Preset {
		std::string Label;
		std::string ClassName;
		std::string PresetName;
		std::string Module;
		int ModuleID = -1;
		std::string Group; //!< Structures: the kind of bunker piece ("Bunker Modules", "Bunker Lights"...), to list them by.
		std::string Kind; //!< A subcategory to list by: for units "Infantry", "Mecha", "Turrets"; for items "Primary weapons", "Grenades", "Tools"...
		bool Modded = false; //!< From a module that isn't one of the game's own.
		bool NonCombatant = false; //!< Units: a non-combatant by its game files (Actor::IsNonCombatant, NC-1): an animal, a civilian.
		int Temperament = 0; //!< Units: its temperament by its game files (Actor::Temperament).
		bool Jetpack = false; //!< Units: its jetpack really flies it: lifts it at least c_JetpackFlyingLift.
		float JetLift = 0.0F; //!< Units: how high its jetpack lifts it from a standstill, in metres (Actor::EstimateJumpHeight); -1 for without limit.
		int Width = 0; //!< Structures: footprint, for the preview.
		int Height = 0;
		float OffsetX = 0.0F;
		float OffsetY = 0.0F;
		std::string Stats; //!< What its game files say about it (cost, health, mass, fire rate...), a line each, for the Spawn tab's tooltip (SpawnStats).
		std::string StatsShort; //!< The few that matter most, short enough to go under its picture on the tile, a line each.
		mutable std::string PictureKey; //!< "ClassName/Module/PresetName", made the first time its picture is asked for (PictureOf), not each frame it is drawn.
	};

	/// Weapons a faction hands its units by default.
	struct FactionArmoury {
		std::vector<const Preset*> Primaries;
		std::vector<const Preset*> Secondaries;
		std::vector<const Preset*> Grenades;
	};

	// The kinds of bunker piece the Build tab offers, as the game's own build menu groups them.
	constexpr const char* c_StructureGroups[] = {"Bunker Modules", "Bunker Systems", "Bunker Lights", "Bunker Backgrounds", "Bunker Bits", "Bunker Clutter", "Bunker Odds & Ends", "Terrain Objects"};
	inline int s_StructureGroup = 0; //!< Which of them the Build tab is listing; one past the last for all.
	inline std::vector<Preset> s_Units;
	inline std::vector<Preset> s_Brains;
	inline std::vector<Preset> s_Items;
	inline std::vector<Preset> s_Structures;
	inline std::vector<std::pair<int, FactionArmoury>> s_Armouries; //!< By module ID.
	inline std::vector<const Preset*> s_Weapons; //!< Guns among the items, for loadouts.
	inline bool s_CatalogueBuilt = false;

	/// How a Battle Director team fights (BattleSettings::Style). All but Defend are the unit orders of the same names.
	enum class BattleStyle {
		Attack,
		HuntBrains,
		Defend,
		Patrol,
		Hold,
		Count
	};
	constexpr const char* c_BattleStyleNames[] = {"Attack nearest enemy", "Hunt brains", "Defend a place", "Patrol", "Hold position"};
	static_assert(std::size(c_BattleStyleNames) == static_cast<size_t>(BattleStyle::Count), "c_BattleStyleNames must name each BattleStyle.");

	/// What the Battle tab's card for one team says: which units it buys, how it fights, and how its ships come in. The window keeps its own
	/// copy (s_BattleSetup) and sends it to the sim in a stroke (Tool::BattleTeam) whenever it changes, as every other choice is sent.
	struct BattleSettings {
		bool Active = false; //!< Takes part: started by "Start battle".
		std::vector<int> Factions; //!< The module IDs of the factions its units come from; none for any faction.
		bool FavouritesOnly = false; //!< Only units marked as favourites (of those factions); any, when none are.
		bool Crabs = false; //!< Crabs among them (ACrab: crabs, and the tanks and walkers built on them). Off by default.
		bool JetpackOnly = false; //!< Only units with a jetpack.
		BattleStyle Style = BattleStyle::Attack;
		bool EndlessMoney = false; //!< Budget is ignored: it never runs out.
		int Budget = 5000; //!< What it may spend in all, in oz.
		int WaveSize = 5; //!< Units in each ship.
		int UnitLimit = 0; //!< Most units it has in at once, counting those still in its ships: none sent past it, fewer to top it up. 0 for no limit.
		int Craft = 0; //!< Index into c_Crafts.
		bool DropOnLine = false; //!< Ships come in over the drop line, not anywhere across the scene.
		bool HasLine = false;
		Vector LineA; //!< The drop line's ends: only its span across counts, as ships come in from the top (or the bottom).
		Vector LineB;
		int ShipsPerBurst = 1; //!< Ships that set off together, each with a wave of its own. 0 for none: the team's units come only from its spawn zones.
		std::vector<std::vector<Vector>> SpawnZones; //!< Areas of the map drawn as polygons (their corners in order, each next to the first, not wrapped), that its units appear in, besides (or instead of) coming in by ship.
		int ZoneEverySeconds = 30; //!< Seconds of game time between one lot of units at the spawn zones and the next.
		int ZoneUnits = 3; //!< Units that appear at each spawn zone each time.
		int EverySeconds = 30; //!< Seconds of game time between bursts.
		bool Invincible = false; //!< Its ships take no harm, and are taken away once they've unloaded and left.
		bool HasDefendPos = false;
		Vector DefendPos; //!< Defend: the middle of the place its units hold.
		int DefendRadius = 150; //!< Defend: how far round DefendPos its units stand and fight.
		int ChaseDistance = 300; //!< Defend: how far past the radius they go after an enemy before giving up and going back.
		int RoamPercent = 0; //!< Defend: the share of its defenders, in percent, that roam the whole chase zone rather than hold a post.
	};

	/// What a Tool::BattleTeam stroke does, by its Count, besides setting Team's settings.
	enum BattleCommand {
		BattleSet = 0, //!< Only the settings.
		BattleStartTeam, //!< Team starts (or carries on, if it ran before) sending waves.
		BattleStopTeam, //!< Team stops sending waves. Its units already in stay.
		BattleStartAll, //!< Every active team starts afresh: spent and sent back to nothing.
		BattleStopAll, //!< Every team stops.
		BattleClearCraft, //!< Team's ships, all of them, taken off the map (with anyone still aboard).
		BattleModeSet, //!< No team's settings: the mode's (Stroke::Mode) only.
		BattleModeStart, //!< The mode's settings, and its game started afresh, every team in it set up by it (BattleModeInfo::TeamSettings).
		BattleModeStop //!< The mode's game stopped, and every team with it.
	};

	/// The Battle Director's preset modes: a game with rules of its own, set up from a few choices (how big, which teams, and a point for
	/// each) instead of every team's card. Custom is the cards as they are. Each is described by its BattleModeInfo (SandboxBattleModes.cpp),
	/// so another slots in by adding to this and to that table.
	enum class BattleMode {
		Custom,
		CaptureTheFlag,
		KingOfTheHill,
		Assault,
		LastTeamStanding,
		VipHunt,
		OneFlag,
		Count
	};

	/// What the Battle tab says for a mode: the choices every mode shares, and those some use (each says which in its panel). The window keeps
	/// its own copy (s_ModeSetup) and sends it to the sim in a Tool::BattleTeam stroke (Stroke::Mode) whenever it changes.
	constexpr size_t c_MaxFlagSpots = 8; //!< Most flag positions one flag can have placed.

	struct BattleModeSettings {
		BattleMode Mode = BattleMode::Custom;
		std::array<int, c_Sides> TeamSize = {16, 16, 16, 16}; //!< Most units each team has alive at once, by side.
		std::array<bool, c_Sides> Plays = {true, true, false, false}; //!< The teams taking part, by side.
		std::array<std::vector<std::vector<Vector>>, c_Sides> SpawnZones; //!< Each team's spawn zones, drawn as polygons: its units appear in them.
		std::array<bool, c_Sides> HasPoint{}; //!< Each team's point placed in its base (capture the flag: where its flag stands). Without, one is picked.
		std::array<Vector, c_Sides> Points;
		std::array<std::vector<Vector>, c_Sides> Goals; //!< One flag: each team's goal zone, drawn as a polygon, that it brings the flag into to score.
		std::vector<Vector> FlagSpots; //!< One flag: the flag positions placed (up to c_MaxFlagSpots), which the flag comes in at in turn, the next after each score.
		bool FlagByZones = false; //!< One flag: the flag comes in somewhere in one of the flag spawn zones (the mode's zones), picked at random, rather than at the positions placed.
		bool ByShip = false; //!< Its units come in by ship over their base, rather than appearing in it.
		bool MoveStuckPoint = true; //!< Capture the flag: a flag nobody can get to (buried, or cut off) moves somewhere else in its base.
		int ScoreToWin = 3; //!< Capture the flag: captures that win. 0 plays on for good.
		int GuardPercent = 30; //!< Capture the flag: the share of each team's units, in percent, that stay to guard its flag.
		int EscortPercent = 50; //!< Capture the flag and one flag: the share of a carrier's team-mates, in percent, that go with it all the way to score; the rest go with it only until it's halfway home, then stay there to hold the ground ahead.
		int ReturnSeconds = 30; //!< Capture the flag: how long a dropped flag lies before it goes back home by itself.
		int RespawnSeconds = 5; //!< Every mode: seconds after one of a team's units falls before another comes in its place.
		int MaxRespawns = 0; //!< Every mode: fallen units each team gets back in all, after its first team size. 0: no limit.
		int StuckSeconds = 20; //!< Every mode: seconds a unit can get no nearer its objective before it is respawned. 0: never.
		std::array<int, c_Sides> RushPercent = {30, 30, 30, 30}; //!< Every mode: the share of each team's units, in percent, that rush the objective: on their way there they keep moving, shooting as they go, and don't take cover, flank, fall back or stop to fight.
		int RouteVariety = 0; //!< Every mode: the share of each team's units, in percent, given a taste in routes of their own (Actor::SetRouteSeed), so they spread over the ways to where they're going rather than all taking the shortest.
		std::vector<std::vector<Vector>> Zones; //!< The mode's own zones, drawn as polygons: king of the hill's hills, assault's objectives (in order).
		int HoldToWin = 120; //!< King of the hill: seconds holding the hill that win.
		int HillMoveSeconds = 0; //!< King of the hill, with more than one hill: seconds before the hill moves on to the next. 0: it stays put.
		bool MajorityScores = false; //!< King of the hill: a hill with more than one team on it scores for the team with the most there, not for none.
		int HillsToWin = 0; //!< King of the hill: hills a team has to take to win, each taken by holding it HoldToWin seconds, after which the next
		                    //!< comes into play. 0: the seconds held, all told, win instead.
		int Attacker = 0; //!< Assault: the side that attacks; the rest defend.
		int CaptureSeconds = 15; //!< Assault: seconds attackers stand in an objective with no defender in it to take it.
		int TimeLimit = 300; //!< Assault: seconds the attackers have; each objective taken adds BonusSeconds.
		int BonusSeconds = 60;
		int Tickets = 60; //!< Last team standing: units each team gets in all, its first ones counted.
		int KillsToWin = 5; //!< VIP hunt: enemy VIPs a team has to bring down to win.
		int VipRespawnSeconds = 20; //!< VIP hunt: seconds before a fallen VIP's team has a new one.
		std::array<bool, c_Sides> Commander{}; //!< Assault and king of the hill: each team whose units an AI commander splits between the
		                                        //!< objective in play and the next one (UpdateCommanders), as a player would with defend zones.
		int CommanderReserve = 30; //!< Its share of each such team's units, in percent, held on the next objective while the one in play is safe.
		int CommanderFallBack = 60; //!< Assault defenders: how far the attackers' taking of the objective in play has got, in percent, when everyone
		                            //!< falls back to the next one.
		std::array<bool, c_Sides> PlayerCommands{}; //!< Every mode: each team the player commands. Its units come in on hold ground with no orders and
		                                            //!< no job from the mode, for the player to command (as in an RTS); the rest of the mode (spawns, scoring) is as for any team.
	};

	/// One queued action, with the settings it was made with.
	/// The chances a plant brush's next plant is made from (which picture, mirrored or not, where in the ground), taken before it is put
	/// down so the cursor can show that very plant (DrawCursor) and the stroke then puts down what was shown.
	struct PlantRoll {
		float Variant = 0.0F; //!< Which of the brush's debris presets (cacti big or small, which mushrooms), 0 to 1.
		float Piece = 0.0F; //!< Which of the preset's pictures, 0 to 1.
		float Jitter = 0.5F; //!< Where across, a little either side of the point, 0 to 1.
		float Depth = 0.0F; //!< How deep into the ground, within the preset's depths, 0 to 1.
		bool Mirror = false;
		int Entry = -1; //!< Which picture of the brush's gallery (PlantGallery) exactly, or -1 to take it from the chances above.
		Tool Kind = Tool::Plants; //!< The brush Entry is a picture of.
	};

	/// One ingredient of an effect you make yourself: a light, a source of particles, a force or a bit of the air. An effect is a list of them, all running at once.
	enum class LayerKind {
		Light, //!< A round light: Size is how far it reaches.
		Spotlight, //!< A beam: Size is its reach, Spread how wide, Angle which way, Spin how fast it turns.
		Sparks,
		Embers,
		Smoke, //!< Looks only: it doesn't block sight.
		Dust,
		Mist,
		Debris,
		Gas, //!< Real gas in the gas grid (Option: smoke, toxic gas, methane or steam).
		Flames, //!< Real fire: it burns.
		Shimmer,
		Shockwave, //!< Pulses of rippling air (Rate a second). No blast.
		Lightning, //!< Real strikes (Rate a second): fire and harm where they land.
		Force, //!< Pushes units, things and debris about, harming nothing (Option: blow along Angle, push out, pull in, lift).
		Count
	};

	/// Which of an EffectLayer's controls a kind uses.
	enum LayerControl : unsigned {
		LcSize = 1,
		LcRate = 2,
		LcSpeed = 4,
		LcSpread = 8,
		LcAngle = 16,
		LcSpin = 32,
		LcIntensity = 64,
		LcFlicker = 128,
		LcPulse = 256,
		LcOption = 512,
		LcColour = 1024,
		LcOffset = 2048
	};

	struct LayerInfo {
		const char* Name;
		const char* Tip;
		unsigned Uses;
	};

	constexpr LayerInfo c_Layers[static_cast<int>(LayerKind::Count)] = {
	    {"Light", "A round light, which can flicker and pulse.", LcSize | LcIntensity | LcFlicker | LcPulse | LcColour | LcOffset},
	    {"Spotlight", "A beam of light, which can point where you like and turn.", LcSize | LcSpread | LcAngle | LcSpin | LcIntensity | LcFlicker | LcPulse | LcColour | LcOffset},
	    {"Sparks", "Glowing streaks thrown out.", LcSize | LcRate | LcSpeed | LcSpread | LcAngle | LcSpin | LcColour | LcOffset},
	    {"Embers", "Glowing specks that rise and drift.", LcSize | LcRate | LcSpeed | LcSpread | LcAngle | LcSpin | LcColour | LcOffset},
	    {"Smoke", "Soft dark puffs that roll up and linger. Looks only: units see through it.", LcSize | LcRate | LcSpeed | LcSpread | LcAngle | LcSpin | LcColour | LcOffset},
	    {"Dust", "Soft puffs of dust.", LcSize | LcRate | LcSpeed | LcSpread | LcAngle | LcSpin | LcColour | LcOffset},
	    {"Mist", "A soft pale spray that hangs and thins.", LcSize | LcRate | LcSpeed | LcSpread | LcAngle | LcSpin | LcColour | LcOffset},
	    {"Debris", "Little chips that bounce.", LcSize | LcRate | LcSpeed | LcSpread | LcAngle | LcSpin | LcColour | LcOffset},
	    {"Gas", "Real gas let out into the air: it drifts, rises and settles as gas does (gas must be on in F6).", LcSize | LcRate | LcIntensity | LcOption | LcOffset},
	    {"Flames", "Real flame: it burns what it touches.", LcSize | LcRate | LcSpeed | LcSpread | LcAngle | LcSpin | LcOffset},
	    {"Heat shimmer", "The air shimmering, as over something hot.", LcSize | LcIntensity | LcOffset},
	    {"Shockwave pulses", "A blast wave rippling out, over and over. No blast.", LcSize | LcRate | LcIntensity | LcOffset},
	    {"Lightning strikes", "Real bolts from the sky across the area: fire and harm where they land.", LcSize | LcRate | LcOffset},
	    {"Force field", "Shoves units, things, debris and smoke about and harms nothing.", LcSize | LcSpeed | LcAngle | LcOption | LcIntensity | LcOffset},
	};

	/// A layer's controls. What each means depends on the kind (see LayerInfo::Uses).
	struct EffectLayer {
		LayerKind Kind = LayerKind::Light;
		float Size = 60.0F; //!< Pixels: how far a light reaches, how wide the place particles come from is, how far a force or shimmer reaches.
		float Rate = 12.0F; //!< Particles, pulses or strikes a second.
		float Speed = 4.0F; //!< Metres a second particles leave at; a force's strength.
		float Spread = 0.4F; //!< 0 (straight) to 1 (every way); a spotlight's width.
		float Angle = 0.0F; //!< Degrees: 0 up, 90 right, 180 down.
		float Spin = 0.0F; //!< Degrees a second the angle turns.
		float Intensity = 1.0F; //!< How bright, how thick or how strong.
		float Flicker = 0.0F; //!< 0 to 1: how much a light flickers.
		float Pulse = 0.0F; //!< Times a second a light swells and fades.
		int Option = 0;
		bool OwnColour = false; //!< Particles: use Colour rather than the kind's own.
		float Colour[3] = {1.0F, 0.72F, 0.35F}; //!< 0 to 1.
		float OffsetX = 0.0F; //!< Pixels from where the effect is put.
		float OffsetY = 0.0F;
	};

	/// An effect made in the Effects tab, kept in Userdata/SandboxEffects.txt.
	struct CustomEffect {
		std::string Name;
		std::vector<EffectLayer> Layers;
	};

	/// A light or fire for the background (Tool::Decor).
	enum class DecorKind {
		WallLamp,
		CeilingLamp,
		Lantern,
		StripLight,
		WarningLight,
		Candle,
		Candelabra,
		Torch,
		Campfire,
		Count
	};

	struct Stroke {
		Tool Kind;
		Vector Position;
		int Radius = 0;
		int Choice = 0; //!< Index into the list the tool spawns from.
		int Team = 0;
		Order Orders = Order::Hold;
		int Loadout = 0; //!< 0 faction default, 1 unarmed, 2+ a weapon from s_Weapons.
		int Count = 1;
		bool LitGrenade = false;
		bool Direct = false; //!< Tool::Command: placed with a triple click, so the order is a direct one (s_DirectOrder).
		long UnitID = 0; //!< Dropping a step of a plan: whose (and Choice which step).
		std::vector<Vector> Points; //!< A patrol route's points (RC-4).
		Vector Position2; //!< Selection box: the other corner.
		int Craft = 0; //!< Drops: index into c_Crafts.
		bool HasView = false; //!< Whether ViewMiddleX was taken, when the stroke was made on screen (not by a script).
		float ViewMiddleX = 0.0F; //!< The middle of the view across, at the click: spawned units face it. (Taken then, not read in the sim.)
		bool Random = false; //!< Units and drops: random units rather than the one chosen.
		bool FavouritesOnly = false; //!< With Random: only units marked as favourites (any, when none are).
		int RandomFaction = -1; //!< With Random: only this faction's units (an index into s_FactionModules), -1 for every faction.
		bool JetpackOnly = false; //!< With Random: only units with a jetpack.
		std::vector<EffectLayer> Layers; //!< Tool::Effect with a made effect (Choice from EffectKind::Count): its layers, taken at the click.
		std::string Material; //!< Springs, the tank and "Other": the liquid or powder poured, by preset name (taken at the click, not read in the sim).
		float RopeStrength = 1.0F; //!< Rope: the multiplier on what the kind holds (s_RopeStrength).
		float RopeAnchor = 0.0F; //!< Rope: how hard, in kg, a tie can be pulled before it lets go, 0 never (s_RopeAnchor).
		float Rate = 1.0F; //!< Springs: how much of the time they pour, 0.05 to 1.
		float Life = 0.0F; //!< Springs: how many seconds what they pour lasts, 0 for ever.
		float Scale = 1.0F; //!< Plant brushes: how big the plant is drawn, 1 as the game's own art (s_PlantScale).
		bool HasPlantRoll = false; //!< Plant brushes: whether Plant was taken at the click (the plant the cursor showed), else it is rolled in the sim.
		PlantRoll Plant; //!< Plant brushes: the plant to put down, when HasPlantRoll.
		BattleSettings Battle; //!< Tool::BattleTeam: the team's settings.
		BattleModeSettings Mode; //!< Tool::BattleTeam with a BattleMode command: the mode's settings.
		std::vector<int> Materials; //!< Tool::ClearMap: the material IDs to clear (liquids or ground).
		BrushShape Shape = BrushShape::Circle; //!< Terrain brushes: how they lay it down (s_BrushShape).
		int Over = 0; //!< Terrain and liquid brushes: what they may paint over besides air, PaintOver flags (Paint > Paint over), taken at the click.
		int Fill = -1; //!< Terrain brushes: a FillShape filled from Position to Position2 (Brush type Shape), or -1 for a brush stroke at Position.
		int Temperament = -1; //!< Units and drops: the temperament they are given (Actor::Temperament, NC-1), -1 for each one's own.
	};

	struct CraftChoice {
		const char* Label;
		const char* ClassName;
		const char* PresetName;
	};
	constexpr CraftChoice c_Crafts[] = {{"Dropship", "ACDropShip", "Dropship MK1"}, {"Rocket", "ACRocket", "Rocket MK2"}};

	/// A unit the sandbox keeps a hold of, checked against its unique ID before use (its memory may be reused by a new object).
	struct UnitRef {
		Actor* Unit = nullptr;
		long ID = 0;
	};

	inline Actor* GetRef(const UnitRef& ref) { return ref.Unit && g_MovableMan.IsActor(ref.Unit) && ref.Unit->GetUniqueID() == ref.ID ? ref.Unit : nullptr; }

	inline UnitRef MakeRef(Actor* actor) { return {actor, actor ? actor->GetUniqueID() : 0}; }

	/// Whether a reference is to this very unit: its address and its unique ID. (By address alone, a unit that died and a new one made at the
	/// same address was taken for it: the new unit counted as selected, or had the dead one's pending order.)
	inline bool RefersTo(const UnitRef& ref, const Actor* actor) { return actor && ref.Unit == actor && ref.ID == static_cast<long>(actor->GetUniqueID()); }

	/// What a unit's route from where it stands to a place costs, searched as its own AI searches (the whole of its PathAgent, from the
	/// ground under it, on its team's grid); -1 for no route. 100000 or more is a route only through ground it can't get through.
	/// (Searched with just its jump, dig and breach strengths, the searcher had no stairs, slopes, ladders, leaps, mantles or jetpack
	/// flights, and a soldier "couldn't get to" any spot up a step or a stair that its AI then walked to: "no route" markers and the unit
	/// saying so on orders that went fine, the formation's spots picked among the wrong ones, and flags sent home.)
	inline float RouteCost(const Actor* unit, const Vector& to) {
		Scene* scene = g_SceneMan.GetScene();
		if (!scene || !unit) {
			return -1.0F;
		}
		std::list<Vector> path;
		return scene->CalculatePath(unit->GetPathStart(), to, path, unit->GetPathAgent(), static_cast<Activity::Teams>(unit->GetTeam()));
	}

	/// Whether RouteCost found a way there the unit can take.
	inline bool RouteReachable(float cost) { return cost >= 0.0F && cost < 100000.0F; }


	/// One team in the Battle Director, as the sim runs it: the settings last sent from the window, and how it is getting on.
	struct BattleTeam {
		BattleSettings Settings;
		bool Running = false; //!< Sending waves.
		float Spent = 0.0F;
		int Sent = 0;
		long long NextWave = 0; //!< The sim update its next burst of ships sets off on.
		long long NextZoneWave = 0; //!< The sim update units next appear at its spawn zones on.
		std::vector<Vector> ZoneDraft; //!< The corners of a spawn zone a script is putting down, one SandboxDo at a time, till it closes it.
		bool Broke = false; //!< Can't afford another unit.
		bool FillFirst = false; //!< Its next lot of ships or spawn zone units brings it up to its unit limit at once (a mode's game starting with whole teams).
	};

	/// A Battle Director unit told to defend a place: it holds a post there and goes after enemies near it, but only so far (UpdateBattleDefenders).
	struct BattleDefender {
		int Team = 0; //!< Its team, whose card's place, radius and chase distance it goes by while the team still defends one.
		Vector Center; //!< The place it defends.
		float Radius = 150.0F;
		float Chase = 300.0F; //!< How far past Radius from Center it may go after an enemy.
		Vector Post; //!< Where it stands when there's nothing to chase (a roamer: the spot it's walking to, or waiting at).
		float RoamRoll = 0.0F; //!< Its own 0-1 roll, fixed when bought: it roams while that's under the card's RoamPercent.
		bool Roams = false; //!< Roams the chase zone, from one spot to another, rather than holding a post.
		long long IdleSince = -1; //!< A roamer: the sim update it was first seen waiting at its spot, -1 while on its way.
		long long Dwell = 0; //!< A roamer: how long it waits at a spot before going on, in sim updates.
		long ChasingID = 0; //!< The enemy it was sent after, 0 when at (or on its way back to) its post.
		bool Seen = false; //!< Out in the world at least once: before that it is riding in its ship.
		long long Made = 0; //!< The sim update it was made on.
		bool Commanded = false; //!< Told to defend by a player's order (Defend at, defend here, guard): its own place, radius and chase distance,
		                        //!< kept whatever the team cards and modes do, till it is given another order.
	};

	/// A Battle Director ship that can't be hurt, kept whole until it has delivered and left (UpdateBattleCraft).
	struct BattleCraft {
		UnitRef Ship;
		long long Emptied = -1; //!< The sim update it was first seen empty after delivering, -1 until then.
	};

	inline int s_ToolIndex = 0;
	inline int s_Craft = 0;
	inline std::vector<UnitRef> s_Selected;
	inline UnitRef s_FollowTarget;
	inline bool s_FollowAction = false;
	inline Vector s_ActionSpot;
	inline bool s_ActionSpotValid = false;
	inline bool s_Dragging = false;
	inline bool s_DoubleClick = false; //!< The drag or click under way began with a double click.
	inline bool s_TripleClick = false; //!< The drag or click under way began with a third click or more in a row: its order is a direct one (s_DirectOrder).
	/// While an order the player triple-clicked is given (a command stroke with Direct): every unit sent (SendUnit) goes the shortest way there
	/// whatever it takes (Actor::StandingOrder::Direct), and isn't held to the group's pace. Set round the stroke, so every kind of order
	/// placed by a click (move, attack-move, dig to, defend, a map order) is made direct the same way.
	inline bool s_DirectOrder = false;
	inline ImVec2 s_DragStart;
	inline std::array<BattleTeam, c_Sides> s_BattleTeams; //!< The Battle Director's teams, as the sim runs them.
	inline std::array<BattleSettings, c_Sides> s_BattleSetup = [] { //!< The Battle tab's cards, the window's copy (sent to the sim as each changes).
		std::array<BattleSettings, c_Sides> setup;
		// Red against Green to start with.
		setup[0].Active = true;
		setup[1].Active = true;
		return setup;
	}();
	inline int s_BattleEditTeam = 0; //!< The team the defence point and drop line tools set.
	inline std::vector<Vector> s_ZoneDraft; //!< The corners of the spawn zone being drawn with the Battle tab's tool, in order.
	inline int s_ToolBeforeBattle = -1; //!< The tool in hand before the card's defence point or drop line button took one, given back by PutDownBattleTool.
	inline std::unordered_map<long, BattleDefender> s_BattleDefenders; //!< By unique ID.
	inline bool s_DefendersMoved = false; //!< A defender's place moved (MoveDefender) since UpdateBattleDefenders last ran: it runs on the next update rather than at its half-second turn.
	inline std::vector<BattleCraft> s_BattleCraft;

	/// The Battle Director's mode as the sim runs it: the settings last sent from the window, and how the game is going.
	/// How a battle objective is lit up on the map, with "Show battle objectives" on (DrawObjectives).
	enum class ObjectiveLook {
		Marker, //!< A place (a flag, a VIP): a glowing ring on the ground round it.
		Outline, //!< A zone: a glowing line round its edge.
		Ground, //!< A zone: the ground across it glowing, a line along the top of the terrain (a hill's crest).
		Glow, //!< A zone: the terrain and buildings in it glowing, brightest at their edges.
		Count
	};

	/// Something a battle mode's game is about, the same for every mode (BattleModeInfo::Objectives): where it is, who goes for it and who holds it,
	/// and how it shows. The commander's "Battle objective" order, the stuck check and the map all read these.
	struct BattleObjective {
		std::string Name; //!< As "Red flag", "Hill 2".
		Vector Pos; //!< Where to go for it: the place, or the middle of the zone.
		std::vector<Vector> Zone; //!< Its area, or empty for a place.
		float Radius = 60.0F; //!< How near a place counts as at it (a zone: being in it).
		ImU32 Color = IM_COL32(255, 255, 255, 255);
		ObjectiveLook Look = ObjectiveLook::Marker;
		unsigned Attackers = 0; //!< A bit per team that goes for it (to take it, capture it, kill it, score in it).
		unsigned Defenders = 0; //!< A bit per team that holds it.
		bool Live = true; //!< In play now; false for one taken, not yet in play, or a flag's empty stand.
		bool Shown = true; //!< Lit up on the map with "Show battle objectives" on; false for a carried flag (the flag over its carrier's head marks it).

		bool AttackedBy(int side) const { return side >= 0 && side < 32 && (Attackers >> side) & 1u; }
		bool DefendedBy(int side) const { return side >= 0 && side < 32 && (Defenders >> side) & 1u; }
	};

	/// The objectives of the battle mode's game now (with it on), or as set up on the Battle tab (with it not).
	std::vector<BattleObjective> BattleObjectives();
	/// The objective a team's unit at a place should go for in the mode's game on now: the nearest live one its team attacks, else the nearest it
	/// defends (defend true). False with no game on, or none for that team.
	bool BattleObjectiveFor(int side, const Vector& from, BattleObjective& objective, bool& defend);

	struct BattleModeRun {
		BattleModeSettings Settings;
		bool Running = false; //!< A mode's game is on: its rules run each update.
		bool Over = false; //!< Won: the teams stopped, and the result shown till the mode is started again, stopped or left.
		int Winner = -1;
		std::array<int, c_Sides> Score{};
		std::string Result; //!< Once over: how it ended, as "Red is the last team standing" (or, when empty, who won).
		std::string Note; //!< The latest happening, shown over the game for a few seconds (as "Green has Red's flag").
		long long NoteAt = -1; //!< The sim update it happened on.
	};
	inline BattleModeRun s_ModeRun;
	inline BattleModeSettings s_ModeSetup; //!< The Battle tab's mode panel, the window's copy (sent to the sim as it changes).
	inline bool s_ShowModeZones = true; //!< A mode's own zones (hills, assault objectives, goal zones) shaded and outlined on the map (always while one is being drawn).
	inline bool s_ShowBattleInfo = true; //!< A panel under the score while a battle is on: each team's units in, fallen, respawns left and next one in.
	inline bool s_ShowObjectives = true; //!< Each battle mode's objectives (its flags, hills, goals...) lit up on the map, each in the look it asks for.
	inline int s_ObjectiveLook = 0; //!< The look of zone objectives: 0 each mode's own, else an ObjectiveLook (plus one) for all of them.
	inline bool s_ShowModeBases = true; //!< The teams' spawn zones shaded and outlined on the map (always while one is being drawn, or a point placed).
	// The Unit and Drop tools' random units (copied into the stroke at the click): from every faction, one faction or the favourites.
	inline bool s_RandomUnits = false;
	inline bool s_RandomFavourites = false;
	inline int s_RandomFaction = -1; //!< -1 every faction, otherwise an index into s_FactionModules.
	inline int s_UnitsShown = 0; //!< The Spawn tab's unit list (NC-1): 0 every unit, 1 fighters only, 2 non-combatants only.
	inline int s_SpawnTemperament = -1; //!< The temperament units are spawned with (Actor::Temperament, NC-1), -1 for each one's own.
	inline bool s_JetpackOnly = false; //!< The Spawn tab's "Jetpacks only": units without one aren't listed, or picked at random.
	inline std::vector<int> s_FactionModules;
	inline std::vector<std::string> s_FactionNames;
	inline int s_Radius = 6;
	constexpr int c_MaxBrushRadius = 120; //!< The biggest the brush size goes (was 40).
	inline float s_PlantScale = 1.0F; //!< How big the plant brushes draw their plants, 1 as the game's own art (Paint > Plants).
	inline int s_PlantSpacing = 10; //!< How far apart along the stroke the plant brushes put plants, in pixels (Paint > Plants).
	inline float s_LastPlantX = 0.0F; //!< Where across the plant brush last put a plant, for the spacing.
	inline bool s_ShapeFill = false; //!< Brush type Shape: the terrain brushes fill a shape dragged out on the world rather than painting where the pointer goes.
	inline FillShape s_FillShape = FillShape::Square; //!< The shape they fill then.
	inline bool s_ShapeDragging = false; //!< A shape being dragged out, from s_ShapeStart.
	inline int s_RopeType = 0; //!< What the Rope tool puts down (RopeSim::GetType).
	inline float s_RopeSlack = 0.1F; //!< How much longer than the straight line between its points the Rope tool's rope is.
	inline float s_RopeStrength = 1.0F; //!< How much more (or less) than its kind's own the Rope tool's rope holds before it snaps.
	inline float s_RopeAnchor = 0.0F; //!< How hard, in kg, a tie of the Rope tool's rope can be pulled before it lets go; 0 never.
	/// Whether the Rope tool's slack, strength or tie strength differ from a kind's own.
	inline bool RopeSettingsChanged() { return std::abs(s_RopeSlack - 0.1F) > 1e-4F || s_RopeStrength != 1.0F || s_RopeAnchor != 0.0F; }
	/// Puts the Rope tool's slack, strength and tie strength back to a kind's own.
	inline void ResetRopeSettings() {
		s_RopeSlack = 0.1F;
		s_RopeStrength = 1.0F;
		s_RopeAnchor = 0.0F;
	}
	inline std::vector<Vector> s_RopeDraft; //!< The window's: the points of the rope being put down, clicked so far (the line to the pointer is drawn from the last).
	inline ImVec2 s_RopeRightStart; //!< Where the right button went down with the Rope tool in hand: let go about there, it finishes the rope.
	inline bool s_RopeRightDown = false;
	inline int s_RopeDrawing = 0; //!< The sim's: the rope its Rope clicks carry on, 0 for none (the next click starts one).
	inline Vector s_ShapeStart;
	constexpr int c_MaxDropSide = 800; //!< The biggest box "Make it fall" takes either way, in pixels.

	/// Whether the tool in hand is used by dragging out a shape on the world: the terrain brushes with Brush type Shape, and "Make it fall"'s box.
	inline bool DragsShape(Tool kind) { return (IsTerrainBrush(kind) && s_ShapeFill) || kind == Tool::CollapseArea; }
	/// What the Paint tab's brushes may paint over besides air (flags): Paint > Paint over.
	namespace PaintOver {
		constexpr int Liquids = 1; //!< Liquids and loose ground (sand, snow, rubble): replaced by what is painted.
		constexpr int Terrain = 2; //!< Solid terrain: replaced by what is painted.
	} // namespace PaintOver
	inline bool s_PaintOverLiquids = false; //!< Paint > Paint over > Liquids.
	inline bool s_PaintOverTerrain = false; //!< Paint > Paint over > Terrain.
	inline int CurrentPaintOver() { return (s_PaintOverLiquids ? PaintOver::Liquids : 0) | (s_PaintOverTerrain ? PaintOver::Terrain : 0); }
	inline BrushShape s_BrushShape = BrushShape::Circle; //!< How the terrain brushes paint and dig: circles, squares or a spray (Paint > Terrain).
	inline std::string s_PaintMetal = "Metal"; //!< What the Metal tool paints, picked under Metals (a c_PaintMetals material).
	inline std::string s_OtherTerrain = "Topsoil"; //!< What the "Other terrain" tool paints, picked under "More terrain...".
	inline int s_UnitChoice = 0;
	inline int s_BrainChoice = 0;
	inline int s_ItemChoice = 0;
	inline int s_StructureChoice = 0;
	/// The search boxes' text, one for each list: by the tool it lists for, a picture grid's apart from a plain list's. (One shared box filtered
	/// the Spawn, Build and Colony lists alike, so a search typed in one emptied the others.)
	inline std::map<int, std::array<char, 64>> s_Filters;
	inline char* FilterFor(Tool kind, bool pictures) { return s_Filters[static_cast<int>(kind) * 2 + (pictures ? 1 : 0)].data(); }
	inline int s_Team = 1;
	inline int s_Order = static_cast<int>(Order::Hold); //!< Units placed hold their position, firing back, until told otherwise.

	/// The order in hand, chosen from the orders a unit can be made with (OrderInfo::ForUnit), for the unit and barracks tools. The side orders share
	/// it: "Move to a place" chosen there shows here as "Hold position", which is what a unit placed with it does, and stays as it is unless a
	/// choice is made here. (Offered all eight, "Move to a place" was there to pick and did nothing a hold doesn't.)
	inline bool UnitOrderCombo(const char* label) {
		int choice = static_cast<int>(UnitOrder(s_Order));
		if (ImGui::Combo(label, &choice, OrderName, nullptr, c_UnitOrderCount)) {
			s_Order = choice;
			return true;
		}
		return false;
	}
	inline int s_Loadout = 0;
	inline int s_SquadSize = 1;
	inline bool s_LitGrenade = false;
	inline bool s_SnapToGrid = true;
	inline bool s_FreeCamera = false;
	inline bool s_SlowMotion = false;
	inline bool s_FreeCameraStarted = false;
	inline int s_CameraWarmupFrames = 0; //!< Frames to leave the camera alone at the start of a game, while the game mode points it somewhere sensible.
	inline Vector s_CameraCenter;
	inline float s_StrokeTimer = 0.0F;
	inline float s_DigTimer = 0.0F; //!< As s_StrokeTimer, for the right button's digging with a Paint tool in hand.
	inline std::vector<Stroke> s_Queue;
	inline std::array<Vector, c_Sides> s_RallyPoints;
	inline std::array<bool, c_Sides> s_RallySet{};
	inline Actor* s_Possessed = nullptr; //!< The unit you're controlling in the god mode, checked with IsActor and its unique ID before use.
	inline long s_PossessedID = 0; //!< Its unique ID: a unit that died and a new one made at the same address passed the IsActor check alone.

	inline void SetPossessed(Actor* actor) {
		s_Possessed = actor;
		s_PossessedID = actor ? static_cast<long>(actor->GetUniqueID()) : 0;
	}

	/// Your character's own gib limits while it can't be hurt (0 is "never" for both), to put back when that is turned off.
	struct SavedGibLimits {
		long ID = 0;
		float Impulse = 0.0F;
		int Wounds = 0;
	};
	inline SavedGibLimits s_PlayerGibLimits;

	/// Your own character in the Sandbox game mode: what it is, what it carries and what it can do.
	struct PlayerSetup {
		std::string Body = "Soldier Heavy";
		std::vector<std::string> Kit = {"Heavy Digger", "Laser Rifle", "Giga Pulsar", "Assault Rifle", "Constructor", "Medikit"};
		int Team = 0;
		bool Unkillable = true;
		bool EndlessJetpack = true;
		bool EndlessAmmo = true;
		bool NumberKeys = true; //!< 1 to 9 take out that item of the kit.
		bool FlyKey = true; //!< N switches flying through anything on and off.
		bool EnterOnClose = true; //!< There is a character at all: putting the tools away puts you in it. Off, you only ever look around.
		bool Neutral = false; //!< On no side as far as the AI goes: its units take no notice of the character.
		bool InheritKit = false; //!< Whether the character also carries what a unit of its base class (Body) is spawned with, besides the kit.
	};
	inline PlayerSetup s_Player;

	/// Whether there is a character of your own to step into: ticked on the You tab, and only in the Sandbox game mode itself (Battle Command
	/// has none: you command a team from above, and take over its units with the Control tool).
	inline bool HasCharacter() { return s_Player.EnterOnClose && !Sandbox::IsBattleCommand(); }
	constexpr bool c_ShowColonyTab = true; //!< Whether the sandbox window offers the colony buildings.
	/// What the command tool does with a click on the world.
	enum class CommandMode {
		Move, //!< Each selected unit to its own spot round the point; a click on an enemy attacks it, a click on a friend selects it.
		Attack, //!< Go for the nearest enemy to the point, or the point itself with orders to fight.
		Guard, //!< Follow the friendly unit clicked and stay with it.
		AttackMove, //!< Walk to the point, stopping to fight any enemy met on the way, then carry on to it (RC-2).
		DefendAt, //!< Post the units round the point to hold it, facing the way the button was dragged (RC-4).
		Patrol, //!< Each click a point of a patrol route; the command row starts it as a loop or back and forth (RC-4).
		DigTo, //!< Dig to the point, in the ground or not (RC-11): those with a digger that cuts the way are sent, to the point itself.
		Suppress, //!< Fire into a zone round the point and keep firing: no target needed, whatever else is in sight; they walk in range first.
		Select //!< Clicks only pick units: one clicked (Shift adds, double click all of its kind in view), or none on a click on nothing. What
		       //!< the Command tool starts in, from the bar. (Last, so the ring's slices keep their places; the command row shows it first.)
	};
	inline CommandMode s_CommandMode = CommandMode::Select;
	constexpr const char* c_CommandModeNames[] = {"Move", "Attack", "Guard", "Attack-move", "Defend at", "Patrol", "Dig to", "Suppress", "Select units"};
	constexpr ImU32 c_CommandModeColors[] = {IM_COL32(110, 180, 250, 255), IM_COL32(239, 106, 91, 255), IM_COL32(120, 220, 120, 255), IM_COL32(245, 150, 70, 255), IM_COL32(242, 182, 61, 255), IM_COL32(120, 200, 220, 255), IM_COL32(214, 160, 90, 255), IM_COL32(205, 120, 235, 255), IM_COL32(230, 230, 230, 255)};

	/// What a dig-to to the point under the cursor would come to for the selected units (RC-11; DigToPreview).
	struct DigPreview {
		DigPlan Plan; //!< The lead digger's plan: the first unit that can dig there, else the first unit.
		float LeadStrength = 0.0F; //!< What that unit's digger cuts.
		int Units = 0; //!< How many units were checked.
		int CanDig = 0; //!< How many of them can dig there.
	};
	inline std::vector<Vector> s_PatrolDraft; //!< The points of the patrol route being clicked out (RC-4), in order.
	constexpr const char* c_WeaponRuleNames[] = {"Fire at will", "Return fire", "Hold fire"}; //!< By Actor::WeaponRule.
	constexpr const char* c_MovementRuleNames[] = {"As ordered", "Engage", "Move only", "Hold ground"}; //!< By Actor::MovementRule.
	inline float s_Spacing = 18.0F; //!< How far apart units stand when sent somewhere together.
	/// The zone a Defend order holds, as a Battle Director team's defend place is (UpdateBattleDefenders): units stand inside the radius,
	/// go after enemies that come within the radius and chase distance, and come back after; a share of them roam the zone.
	inline int s_DefendRadius = 100; //!< px from the point.
	inline int s_DefendChase = 200; //!< px past the radius.
	inline int s_DefendRoam = 0; //!< % of the units that roam the zone rather than hold a post.
	inline int s_SuppressRadius = 80; //!< px round the point a Suppress order fires into.
	/// How units sent somewhere together stand there (RC-5). Side on, a formation is an order along the ground: who is in front and how close.
	enum class Formation {
		Line, //!< Abreast round the point at the spacing, the nearest unit in the middle: as moves always were.
		Column, //!< Single file back from the point, the nearest unit on it, at the spacing.
		Spread, //!< Round the point at twice the spacing, so one blast catches fewer.
		Wedge, //!< The toughest unit on the point, the rest close behind it, toughest first.
		Count
	};
	inline Formation s_Formation = Formation::Line;
	constexpr const char* c_FormationNames[] = {"Line", "Column", "Spread", "Wedge"};
	constexpr const char* c_FormationTips[] = {"Abreast round the point at the spacing, the nearest unit in the middle.", "Single file back from the point, the nearest unit on it.", "Round the point at twice the spacing, so one blast catches fewer.", "The toughest unit on the point, the rest close behind it, toughest first."};
	inline bool s_KeepPace = false; //!< Units sent together walk at the slowest one's pace till they get there (RC-5).
	inline std::vector<UnitRef> s_Paced; //!< The units held to a group's pace, cleared as each arrives or is given another order.
	inline std::array<std::vector<UnitRef>, 10> s_Groups; //!< Control groups: Ctrl+number keeps the selection, the number alone brings it back.
	inline long s_LastIdleID = -1; //!< The idle unit the idle keys last went to (RC-6), so the next press goes on to the next.

	/// A mark left where an order was given, fading over a moment.
	struct OrderMark {
		Vector Position;
		float Life; //!< Seconds left.
		ImU32 Color;
		bool Direct = false; //!< A triple-clicked order (s_DirectOrder): drawn with a second, wider ring.
	};
	inline std::vector<OrderMark> s_OrderMarks;
	// The gym's start and goal being made (see the Gym region).
	inline Vector s_GymFrom;
	inline Vector s_GymTo;
	inline bool s_GymFromSet = false;
	inline bool s_GymToSet = false;

	inline bool s_RingOpen = false; //!< A ring of choices is up, round where the right button went down.
	inline int s_RingPage = 0; //!< Which ring the command tool shows: 0 the basic commands while the button is held, 1 the native AI modes ("More"), 2 the basic ring held up until a click, 3 the weapons rules and 4 the movement rules (RC-1).
	inline ImVec2 s_RingCenter;
	inline Vector s_RingScenePoint; //!< Where in the world the right button went down, which the choice is about.
	inline int s_ColonyKeep = 4; //!< How many of its units a new barracks keeps alive.
	inline bool s_PauseInMenus = true; //!< In the Sandbox game mode the world stands still while the tools are open.
	inline bool s_PausedByMenus = false; //!< Whether it is this that has paused the simulation, so only this is undone.
	inline int s_StepsWanted = 0; //!< Updates to let the paused world do.
	inline size_t s_StrokesApplied = 0; //!< How many queued tool uses the last sim update applied, for the sim state readout.
	inline unsigned long long s_GodStartUpdate = 0; //!< The simulation update the Sandbox game started on: it runs a moment before it first pauses.
	inline float s_PlayHintSeconds = 0.0F; //!< How much longer the reminder of the keys shows after stepping into the character.
	inline bool s_Flying = false;
	inline int s_KitKeyPending = -1; //!< A kit number pressed last update, taken out this one (after the game's own weapon keys have had their say).
	inline bool s_GodViewSetUp = false; //!< Whether the god view's window and camera are set up for the Sandbox game under way.
	inline bool s_GodViewPending = false; //!< A game has started since (see Sandbox::OnActivityStarted): set them up afresh.
	constexpr unsigned int c_RandomSeed = 0x5A17B0Bu;
	inline unsigned int s_Random = c_RandomSeed;

	inline float Random01() {
		s_Random ^= s_Random << 13;
		s_Random ^= s_Random >> 17;
		s_Random ^= s_Random << 5;
		return static_cast<float>(s_Random & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	inline const ToolInfo& CurrentTool() { return c_Tools[s_ToolIndex]; }

	inline bool InGame() {
		const Activity* activity = g_ActivityMan.GetActivity();
		return activity && activity->GetActivityState() >= Activity::ActivityState::Editing && activity->GetActivityState() <= Activity::ActivityState::Running && g_SceneMan.GetScene() && g_SceneMan.GetScene()->GetTerrain();
	}

	inline GameActivity* CurrentGame() { return dynamic_cast<GameActivity*>(g_ActivityMan.GetActivity()); }

	// Scene and window positions: shared with the debug overlays (DebugDraw.h), so they work outside the sandbox too.
	inline float ScenePixelsPerWindowPixel() { return DebugDraw::ScenePixelsPerWindowPixel(); }
	inline ImVec2 ViewOrigin() { return DebugDraw::ViewOrigin(); }
	inline Vector FromCamera(const Vector& scenePosition) { return DebugDraw::FromCamera(scenePosition); }
	inline Vector MouseScenePosition() { return DebugDraw::MouseScenePosition(); }

	inline bool ContainsIgnoringCase(const std::string& text, const char* filter) {
		if (!filter[0]) {
			return true;
		}
		auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
		std::string haystack(text.size(), ' ');
		std::transform(text.begin(), text.end(), haystack.begin(), lower);
		std::string needle(filter);
		std::transform(needle.begin(), needle.end(), needle.begin(), lower);
		return haystack.find(needle) != std::string::npos;
	}


#pragma region Types and state of the units

	inline int s_EffectChoice = 0;

	/// An order to a unit that is put into effect on the next update, all at once (waypoints and mode), so later clicks before then (shift-clicks) join
	/// it. The AI sees it by the unit's order serial (Actor::GetAIOrderSerial), so the unit is no longer dropped out of GOTO for an update first,
	/// which stopped it dead for that update and ended whatever it was doing.
	struct PendingOrder {
		UnitRef Unit;
		Vector Waypoint;
		Actor* Target = nullptr; //!< An enemy to go for instead of a place.
		long TargetID = 0;
		bool Attack = false; //!< Keep attacking (a new target when this one dies).
	};

	inline std::vector<PendingOrder> s_PendingOrders;

	/// What one step of a unit's plan is (RC-3).
	enum class PlanKind {
		Move, //!< Go to the place (also the order the unit was already carrying out when its first step was queued: done when it arrives).
		AttackMove, //!< Go to the place fighting what is met (RC-2).
		Attack, //!< Go after the enemy until it is dead.
		Guard, //!< Stay with the friend; done only if the friend is gone.
		Defend, //!< Hold ground where it stands; never done, so it ends a plan.
		Wait //!< Stay a while where it is (a patrol's pause at each point, RC-4).
	};

	/// One step of a unit's plan: a shift-clicked order to carry out after the ones before it.
	struct PlanStep {
		PlanKind Kind = PlanKind::Move;
		Vector Place; //!< Where: the unit's own spot for a move, the post for a defend (where the step before leaves it), the target's place when queued otherwise.
		UnitRef Target; //!< The enemy to attack or the friend to guard.
		int Facing = 0; //!< A defend's way to face: -1 left, 1 right, 0 either (RC-4).
		int Updates = 0; //!< A wait's length, in sim updates.
	};

	/// A unit's plan (RC-3): the step under way and the ones still to come, worked through one at a time. Any order given without Shift drops it.
	struct Plan {
		UnitRef Unit;
		bool Running = false; //!< Whether Current is under way.
		PlanStep Current;
		long long Started = 0; //!< The sim update Current was started on.
		std::deque<PlanStep> Steps;
		std::vector<Vector> Route; //!< A patrol's points (this unit's own spot at each, RC-4): the steps go round them again whenever they run out.
		bool BackAndForth = false; //!< The patrol walks the route back the other way at each end, rather than from the last point to the first.
		bool Forward = true; //!< Which way a back-and-forth patrol is going.
	};

	inline std::map<long, Plan> s_Plans; //!< Units' plans by unique ID (a map, so they're stepped in a fixed order).
	inline const char* s_MoveAnswer = nullptr; //!< While a group move is sent: the trigger its units answer with in place of a plain move's (unit speech).
	inline bool s_FollowingPlan = false; //!< Set while a plan's step is being given, so giving it doesn't drop the plan.

	/// Why and when a unit was last sent somewhere, for the sandbox orders overlay: kept by unique ID, the dead pruned when the list grows.
	struct SendNote {
		const char* Reason = "";
		bool Resend = false; //!< Sent again by the standing orders (ReturnDefenders), not by anyone's click.
		long long At = 0; //!< The sim update it was sent on.
	};

	inline std::unordered_map<long, SendNote> s_SendNotes;

	/// Where a unit was sent (RC-7), watched till it gets there, so a move the game drops on the way (no route, or given up) is shown.
	struct MoveWatch {
		Vector Destination;
		long long Issued = 0; //!< The sim update it was sent on.
	};
	inline std::unordered_map<long, MoveWatch> s_MoveWatch; //!< By unit unique ID.

	/// A place units were sent to and couldn't get to (RC-7): marked there till it fades, or a click on it sends them again.
	struct NoRoute {
		Vector Destination;
		std::vector<UnitRef> Units;
		long long At = 0; //!< The sim update it was last added to.
		std::string Reason = "no route"; //!< Why they couldn't get there, in the player's words (Actor::OrderFailText), the last one added.
		bool Dig = false; //!< Whether it was a dig-to (RC-11): a click sends them to dig there again, rather than to move there.
	};
	inline std::vector<NoRoute> s_NoRoutes;
	constexpr long long c_NoRouteUpdates = 60 * 10; //!< How long a "no route" marker stays, in sim updates.

	/// A unit of the selection's side coming under fire (RC-7), pinged where it was: in view a ring, out of view an arrow at the edge.
	struct AttackPing {
		Vector Position;
		double Time = 0.0; //!< When, in ImGui time (it is only drawn).
	};
	inline std::vector<AttackPing> s_AttackPings;

	/// What a unit is guarding that isn't a unit (RC-10): a craft, a crate or other loose object, or a colony building. The unit holds a post
	/// by it, moved along when the thing moves; once it is gone the unit holds where it is.
	struct GuardPost {
		long ObjectID = 0; //!< The craft's or object's unique ID, or 0 for a building.
		int BuildingID = 0; //!< The colony building's ID, or 0 for an object.
		Vector Place; //!< Where the thing was when the post was last set.
	};
	inline std::unordered_map<long, GuardPost> s_GuardPosts; //!< By the guarding unit's unique ID.

	/// Commander mode (RC-9): your side's units commanded from above in an ordinary game.
	inline bool s_Commander = false;
	inline int s_CommanderTeam = 0; //!< The side you command: your own in the game.
	inline UnitRef s_CommanderReturnTo; //!< The unit you were playing, to go back into.

	/// The sandbox's undo, one history for painting and placing (Ctrl+Z takes back the newest step, whichever it was). A paint step is what
	/// one stroke of a paint or build tool changed (a drag of the brush is one step: changes less than a quarter second apart run together),
	/// pixel by pixel as it was before, the first change to each pixel only. A placing step is one click of the Spawn or Build tools: the
	/// units, craft, items, doors and colony buildings it made (taken away again), and the ground a bunker piece or building drew over (put
	/// back). The last 20 steps are kept, up to c_PaintUndoPixels pixels in all (the oldest go first), and a new game forgets them. A stroke
	/// longer than a step holds goes on in a new step, so each Ctrl+Z takes back part of it rather than the rest being lost.
	struct PaintUndoPixel {
		unsigned short X; //!< (Scenes are well under 65536 px on a side.)
		unsigned short Y;
		unsigned char Material;
		unsigned char Color; //!< The 8 bit foreground colour.
	};

	/// A background pixel as it was, for a bunker piece's undo (only those draw on the background).
	struct UndoBackgroundPixel {
		unsigned short X;
		unsigned short Y;
		unsigned char Color;
	};

	struct PaintUndoStep {
		std::vector<PaintUndoPixel> Pixels;
		std::unordered_set<long long> Seen;
		int Left = INT_MAX;
		int Top = INT_MAX;
		int Right = INT_MIN;
		int Bottom = INT_MIN;
		long long LastUpdate = 0;
		std::vector<UndoBackgroundPixel> Background; //!< A bunker piece's: the background it drew over.
		std::vector<long> Placed; //!< The unique IDs of what a placing step made.
		int ColonyBuilding = -1; //!< The colony building a placing step built, or -1.
		bool Sealed = false; //!< A placing step: the next stroke starts a step of its own, however soon it comes.
		int Drop = 0; //!< A "Make it fall" step: the drop TerrainCollapse::DropArea gave, taken back with TerrainCollapse::TakeBackDrop.
		int Rope = 0; //!< A rope put down (RopeSim), taken away again.

		bool Empty() const { return Pixels.empty() && Placed.empty() && ColonyBuilding < 0 && Drop == 0 && Rope == 0; }
	};

	inline std::deque<PaintUndoStep> s_PaintUndo;

	inline bool s_RecordPaint = false; //!< While a paint or build stroke is applied (see Apply): only the player's own strokes are undone, not craters.

	inline bool s_RecordPlaced = false; //!< While a placing click is applied (see Apply): what it makes is noted for the undo (NotePlaced).

	constexpr size_t c_PaintUndoSteps = 20;

	constexpr size_t c_PaintUndoPixelsPerStep = 1000000; //!< About 3.5 s of a 40 px brush held down (a stroke past it is undone in parts).

	constexpr size_t c_PaintUndoPixels = 8000000; //!< All the steps together: 48 MB.

	/// A change the paint helpers made to the terrain, kept for the paint audit overlay (only while it's on).
	struct PaintRecord {
		Box Area;
		const char* Kind = "";
		std::string Material;
		bool ToldCollapse = false; //!< TerrainCollapse::BeginChange was called first.
		bool ToldLiquid = false; //!< FluidSim::Disturb was called after.
		bool Changed = false; //!< Any pixel changed (the pathfinder was given the area).
		long long At = 0; //!< The sim update.
	};

	inline std::deque<PaintRecord> s_PaintRecords;

	/// Things that can be put down and left running, for trying the lights, particles and shaders against: each is a light, a source of particles, or both.
	enum class EffectKind {
		NuclearGlow,
		StormCell,
		RedAlarm,
		PoliceLights,
		BlueBeacon,
		Floodlight,
		Searchlight,
		Strobe,
		Disco,
		Campfire,
		Candle,
		LavaGlow,
		WeldingArc,
		Fireflies,
		Portal,
		SparkFountain,
		EmberVent,
		SmokeStack,
		SmokePlume,
		ToxicVent,
		MistVent,
		DustDevil,
		FireJet,
		HeatShimmer,
		ShockwavePulse,
		Count
	};

	struct EffectInfo {
		const char* Name;
		const char* Tip;
	};

	constexpr EffectInfo c_Effects[static_cast<int>(EffectKind::Count)] = {
	    {"Nuclear glow", "A big sickly green light that throbs, with green embers rising and the air shimmering over it."},
	    {"Storm cell", "Flashes of lightning that light the whole area at odd moments, and now and then a bolt that strikes."},
	    {"Red alarm", "A red warning lamp sweeping round."},
	    {"Police lights", "Red and blue flashing by turns."},
	    {"Blue beacon", "A blue light that pulses slowly."},
	    {"Floodlight", "A wide white beam straight down."},
	    {"Searchlight", "A narrow white beam sweeping from side to side."},
	    {"Strobe", "A white light flashing fast."},
	    {"Disco", "Three coloured beams turning, changing colour as they go."},
	    {"Campfire", "A flickering orange light with sparks and embers, and a little smoke."},
	    {"Candle", "A small warm light that wavers."},
	    {"Lava glow", "A wide deep-orange glow that breathes slowly, with embers."},
	    {"Welding arc", "A harsh blue-white light that stutters, throwing sparks."},
	    {"Fireflies", "A handful of tiny green lights wandering about."},
	    {"Portal", "A purple light that pulses, with sparks and shimmering air."},
	    {"Spark fountain", "A steady jet of sparks."},
	    {"Ember vent", "Embers drifting up."},
	    {"Smoke stack", "Thick smoke, which lamps light up and units can't see through."},
	    {"Smoke plume", "Soft smoke whirled up like the dust devil. Looks only: it doesn't block sight."},
	    {"Toxic vent", "Green gas with a dim green light."},
	    {"Mist vent", "Soft pale spray."},
	    {"Dust devil", "Dust whirled about."},
	    {"Fire jet", "A jet of flame. This one is real fire: it burns."},
	    {"Heat shimmer", "The air shimmering, as over something hot. No light."},
	    {"Shockwave pulse", "A blast wave rippling out every second and a half. No blast."},
	};

	struct PlacedEffect {
		EffectKind Kind;
		Vector Position;
		float Seed = 0.0F; //!< 0 to 1, so two of a kind side by side aren't in step.
		float Flash = 0.0F; //!< Storms: how bright the current flash is.
		int Wait = 0; //!< Storms: sim updates until the next flash.
		std::string Name; //!< A made effect (Kind EffectKind::Count): its name,
		std::vector<EffectLayer> Layers; //!< and its layers, as they were when it was put down.
		std::vector<float> Due; //!< Each layer's particles owed, so a slow rate still comes out evenly.
	};

	inline std::vector<PlacedEffect> s_Effects;

	/// The effects the player made, kept in Userdata/SandboxEffects.txt the moment they change. Picked in the Effects tab as s_EffectChoice
	/// from EffectKind::Count on (the first is EffectKind::Count).
	inline std::vector<CustomEffect> s_CustomEffects;
	inline bool s_CustomEffectsLoaded = false;
	inline bool s_CustomEffectsDirty = false;
	constexpr const char* c_CustomEffectsFile = "Userdata/SandboxEffects.txt";
	inline int s_DecorChoice = 0;

	/// A background light or fire put down with Tool::Decor: its light is one of the scenery's lamps (so a blast or a shot destroys it), and its picture is painted into the background layer, put back as it was when the light goes.
	struct PlacedDecor {
		DecorKind Kind;
		Vector Position;
		float Seed = 0.0F;
		int Age = 0; //!< Sim updates since it was put down: the light is given a moment to appear before it is looked for.
		int Left = 0; //!< Where the picture's top left corner is, in scene pixels,
		int Top = 0;
		int Width = 0;
		int Height = 0;
		std::vector<int> Behind; //!< and what the background layer held under each of its pixels (-1: not painted over).
	};

	inline std::vector<PlacedDecor> s_Decor;

	/// A place water keeps pouring from until it's removed: a spring, a burst pipe, a tap left on.
	struct WaterSpawner {
		Vector Position;
		int Radius = 3; //!< How wide the pour is: air within this many pixels of the place is kept full of water.
		std::string Liquid = "Water"; //!< What it pours, by preset name: any liquid or powder FluidSim pours.
		float Rate = 1.0F; //!< How much of the time it pours, 0.05 to 1 (1 every update).
		float Life = 0.0F; //!< How many seconds what it pours lasts before it is gone wherever it has flowed to, 0 for ever: a stream or waterfall that doesn't fill up what it runs into.
		float Due = 0.0F; //!< Rate summed since its last pour: it pours when this reaches 1.
		bool On = true; //!< Off, it stays where it is and pours nothing until turned on again.
	};

	inline std::vector<WaterSpawner> s_WaterSpawners;
	inline std::string s_SpringLiquid = "Water"; //!< What new springs and the tank pour (Paint > Springs).
	inline float s_SpringRate = 1.0F; //!< How much of the time new springs pour.
	inline float s_SpringLife = 0.0F; //!< How many seconds what new springs pour lasts, 0 for ever.
	inline std::string s_OtherPourable; //!< The liquid or powder the "Other" tool pours, picked under "More...".
	inline float s_Flow = 1.0F; //!< How fast the pouring tools pour while held, 0.1 to 1 (they pour every 0.03 s at 1).

	/// The preset names of every material FluidSim pours with the simulations as they are now (liquids, and powders while they slide), mods'
	/// included, sorted. Read from the materials' behaviour as FluidSim sorts them (IsLiquid; Powder, or the stock powder names).
	std::vector<std::string> PourableNames();

	/// A liquid's or powder's colour (its terrain colour, brightened a little so dark ones like tar and oil still show), by preset name: the
	/// marker of a spring that pours it. A blue for a name that is not a material.
	ImU32 MaterialMarkColor(const std::string& name, int alpha = 230);

	/// The springs placed, by what they pour, sorted by name: for the "remove all" choice.
	std::vector<std::pair<std::string, int>> SpringCounts();

	/// Whether a tool pours a liquid or powder (FluidSim), and then whether it needs loose powders on to do anything.
	bool PoursLiquid(Tool kind);
	bool PoursPowder(Tool kind);

	/// Why a tool would do nothing as the settings are, or nothing: flowing liquids or loose powders off.
	const char* ToolUnavailableReason(Tool kind);

	/// What a tool does, for its button's tooltip, or nothing.
	const char* ToolTipText(Tool kind);

	/// Something on its way in from the sky: a rocket, a shell or a bomb. It is kept on its line until it gets there or hits something, then goes off.
	struct Incoming {
		int Delay = 0; //!< Sim updates until it's launched.
		long Id = 0; //!< The flying object's unique ID once launched, 0 before.
		Vector From;
		Vector Target;
		float Speed = 9.0F; //!< Pixels per update.
		std::string ClassName = "TDExplosive";
		std::string Preset = "Standard Bomb";
		int Team = 0;
		int Crater = 0; //!< Radius of ground it takes out where it lands, on top of what its blast does.
		int Life = 900;
		Vector LastPos;
	};

	inline std::vector<Incoming> s_Incoming;

	inline UnitRef s_PlayerUnit; //!< Your character in the Sandbox game mode, while it lives.
	inline UnitRef s_PlayerVehicle; //!< The vehicle your character is sitting in (VH-1), while it is: it is your character again when it gets out.

	inline int s_PlayerEnterPending = 0; //!< Updates left to wait for the character to be in the world before stepping into it; 0 when not waiting.

	/// One place a move order looks at, for the standing-spot reachability preview (SettingsMan::ShowSandboxSpotReach).
	struct SpotReach {
		Vector Spot;
		float Cost = -2.0F; //!< The leader's path cost to it; -1 no path, -2 not looked at (enough were reachable before it).
		bool Chosen = false; //!< A unit will be sent here.
	};

	inline std::deque<std::string> s_StrokeLog; //!< The last tool uses applied, oldest first, for the stroke log (SettingsMan::ShowSandboxStrokeLog).

	enum class Icon { Eye, Arrows, Target, Person, Cross, Flag, Jar, Gun, Wall, Down, Flame, Drop, Cloud, Grains, Chunk, Pick, Bomb, Rocket, Bolt, Star, Plant, Candle, Rope };

	// Twelve by twelve pixels each: # in the tool's own colour, + a highlight.
	constexpr const char* c_IconArt[] = {
	    // Eye
	    "............"
	    "............"
	    "...######..."
	    "..#......#.."
	    ".#..####..#."
	    "#..#++##...#"
	    "#..#+###...#"
	    ".#..####..#."
	    "..#......#.."
	    "...######..."
	    "............"
	    "............",
	    // Arrows
	    ".....##....."
	    "....####...."
	    "...######..."
	    ".....##....."
	    "..#..##..#.."
	    ".##########."
	    ".##########."
	    "..#..##..#.."
	    ".....##....."
	    "...######..."
	    "....####...."
	    ".....##.....",
	    // Target
	    ".....##....."
	    "...######..."
	    "..##.##.##.."
	    ".##..##..##."
	    ".#........#."
	    "####.++.####"
	    "####.++.####"
	    ".#........#."
	    ".##..##..##."
	    "..##.##.##.."
	    "...######..."
	    ".....##.....",
	    // Person
	    "....####...."
	    "...#++###..."
	    "...#+####..."
	    "....####...."
	    "..########.."
	    ".##.####.##."
	    ".##.####.##."
	    ".##.####.##."
	    "....####...."
	    "....#..#...."
	    "...##..##..."
	    "...##..##...",
	    // Cross
	    "............"
	    ".##......##."
	    ".###....###."
	    "..###..###.."
	    "...######..."
	    "....####...."
	    "....####...."
	    "...######..."
	    "..###..###.."
	    ".###....###."
	    ".##......##."
	    "............",
	    // Flag
	    "..#........."
	    "..######...."
	    "..#+#####..."
	    "..########.."
	    "..#######..."
	    "..######...."
	    "..#........."
	    "..#........."
	    "..#........."
	    "..#........."
	    ".####......."
	    "######......",
	    // Jar
	    "...######..."
	    "..########.."
	    "..#......#.."
	    ".#..####..#."
	    ".#.#++###.#."
	    ".#.#+####.#."
	    ".#.######.#."
	    ".#..####..#."
	    ".#........#."
	    "..########.."
	    ".##########."
	    ".##########.",
	    // Gun
	    "............"
	    "............"
	    ".##########."
	    ".#+########."
	    ".##########."
	    ".####..#...."
	    ".###..##...."
	    ".###........"
	    ".###........"
	    ".##........."
	    "............"
	    "............",
	    // Wall
	    "############"
	    "#+###+###+##"
	    "############"
	    "............"
	    "############"
	    "##+###+###+#"
	    "############"
	    "............"
	    "############"
	    "#+###+###+##"
	    "############"
	    "............",
	    // Down
	    "...######..."
	    "..########.."
	    ".##########."
	    ".#.#.##.#.#."
	    ".....##....."
	    ".....##....."
	    "..#..##..#.."
	    "..########.."
	    "...######..."
	    "....####...."
	    ".....##....."
	    "............",
	    // Flame
	    ".....#......"
	    "....##......"
	    "....###..#.."
	    "...####.##.."
	    "..#########."
	    "..#########."
	    ".####++####."
	    ".###++++###."
	    ".###++++###."
	    "..##++++##.."
	    "...######..."
	    "............",
	    // Drop
	    ".....#......"
	    ".....##....."
	    "....###....."
	    "....####...."
	    "...######..."
	    "..########.."
	    "..#+######.."
	    ".##+#######."
	    ".##+#######."
	    "..########.."
	    "...######..."
	    "............",
	    // Cloud
	    "............"
	    "....###....."
	    "...#####.##."
	    "..##########"
	    ".###########"
	    "############"
	    "############"
	    ".##########."
	    "............"
	    "..#...#...#."
	    "....#...#..."
	    "............",
	    // Grains
	    "............"
	    "............"
	    ".....#......"
	    "....#+#....."
	    "...#####...."
	    "..#+####.#.."
	    ".########+#."
	    ".#########.."
	    "############"
	    "############"
	    "............"
	    "............",
	    // Chunk
	    "............"
	    "...#####...."
	    "..#++####..."
	    ".#+#######.."
	    ".##########."
	    ".##########."
	    ".##########."
	    ".##########."
	    "..########.."
	    "...######..."
	    "............"
	    "............",
	    // Pick
	    "..######...."
	    ".########..."
	    "##....####.."
	    "#.......###."
	    "........###."
	    ".......###.."
	    "......###..."
	    ".....###...."
	    "....###....."
	    "...###......"
	    "..###......."
	    "..##........",
	    // Bomb
	    "........#.#."
	    ".......#.#.."
	    "......##...."
	    "....####...."
	    "..########.."
	    ".##########."
	    ".#+########."
	    ".#+########."
	    ".##########."
	    ".##########."
	    "..########.."
	    "....####....",
	    // Rocket
	    ".........##."
	    "........###."
	    "......####.."
	    "....######.."
	    "...#+####..."
	    "..#+####...."
	    "..#####....."
	    ".#.###......"
	    "#..##......."
	    ".#.........."
	    "#.#........."
	    "............",
	    // Bolt
	    "......###..."
	    ".....###...."
	    "....###....."
	    "...###......"
	    "..#######..."
	    ".....###...."
	    "....###....."
	    "...###......"
	    "..###......."
	    "..##........"
	    ".##........."
	    ".#..........",
	    // Star
	    ".....#......"
	    ".....#......"
	    "....###....."
	    "....###....."
	    "###########."
	    ".#########.."
	    "..#######..."
	    "...#####...."
	    "..###.###..."
	    "..##...##..."
	    ".##.....##.."
	    "............",
	    // Plant
	    "......#....."
	    ".....###...."
	    "..##.#+#...."
	    ".####.#.##.."
	    "..##+.####.."
	    "...##.#+#..."
	    "#....##....#"
	    ".##..#...##."
	    "..##.#.###.."
	    "...#####...."
	    ".....#......"
	    "....###.....",
	    // Candle
	    "......+....."
	    ".....+++...."
	    ".....+++...."
	    "......+....."
	    "......#....."
	    "....#####..."
	    "....#####..."
	    "...######..."
	    "...######..."
	    "....#####..."
	    "....#####..."
	    "..#########.",
	    // Rope
	    "..........##"
	    ".........#+#"
	    "........#+#."
	    ".......#+#.."
	    "......#+#..."
	    ".....#+#...."
	    "....#+#....."
	    "...#+#......"
	    "..#+#......."
	    ".#+#........"
	    "#+#........."
	    "##..........",
	};

	struct ToolLook {
		Icon Art;
		ImU32 Color;
	};

	inline std::string s_WantedTab; //!< The tab of the sandbox window to bring to the front, asked for from the bar.

	inline std::string s_CurrentTab; //!< The tab of the sandbox window that is showing.

	inline std::map<std::string, int> s_LastToolOfTab; //!< The tool last picked on each tab, so coming back to the tab from the bar picks it up again.

	/// A thing kept to hand on the bar: a tool, or a tool with what it makes (a unit to spawn, a bunker piece to build).
	struct Pin {
		Tool Kind = Tool::None;
		std::string PresetName; //!< Empty for a tool alone.
	};

	inline std::vector<Pin> s_Pins;

	inline float s_BarHeight = 0.0F; //!< How tall the bar along the bottom was drawn last, 0 when it isn't there: what is shown at the foot of the picture goes above it.
	inline bool s_BarShown = true; //!< Whether the bar along the bottom is up (U hides and shows it); up at the start of every game.

	/// Things marked as favourites (Ctrl+click on a tile): a star on the tile, and a filter to list only them.
	inline std::vector<Pin> s_Favourites;

	/// Favourites are the player's, whatever game is played, and are kept in their own file the moment they change (the settings file is
	/// only written when asked to be, and favourites marked in a game were lost at the next start).
	constexpr const char* c_FavouritesFile = "Userdata/SandboxFavourites.txt";

	/// The bar's pins are the player's too, the same in every game and save, and kept in their own file the moment they change.
	constexpr const char* c_PinsFile = "Userdata/SandboxPins.txt";

	/// A bunker piece as a picture ImGui can draw: its background and foreground art put together.
	struct PiecePicture {
		unsigned int Texture = 0;
		int Width = 0;
		int Height = 0;
		float OffsetX = 0.0F; //!< From the piece's position to the picture's top left corner.
		float OffsetY = 0.0F;
	};

	inline std::map<std::string, PiecePicture> s_PresetPictures; //!< Pictures made by PictureOf, by preset.

	inline std::map<std::string, PiecePicture> s_FilePictures; //!< Pictures made by PictureOfFile, by file.

	/// A list of presets to pick from as a grid of their pictures, each with its name under it. For things whose look is what you choose them by.
	inline bool s_ShowModded = true; //!< Whether things from mods are listed at all.

	inline bool s_FavouritesOnly = false; //!< Whether only favourites are listed.

	inline std::map<Tool, std::string> s_KindFilter; //!< Per tool, the subcategory listed ("" for all).

	inline std::map<Tool, std::string> s_ModFilter; //!< Per tool, the module listed ("" for all).

	/// One choice on a ring.
	struct RingItem {
		const char* Label;
		ImU32 Color;
		const char* Icon = nullptr; //!< One of the game's pie menu icons (Base.rte/GUIs/PieMenus/PieIcons/<Icon>000.png).
	};

	// The gym: courses for the AI on this map, made in play (a start and a goal clicked with the two gym tools, and a name) and kept in
	// Userdata/Gyms/<scene>.txt, one a line as "name|x,y|x,y". A run puts a unit of the kind picked under Spawn at the start, sends it to
	// the goal and times it; the outcome goes to the console as a GYM line (and the test harness, Tools/RenderTest/AIBunker.ps1, runs
	// them all the same way through SandboxDo("Run gym")).
	struct GymCourse {
		std::string Name;
		Vector From;
		Vector To;
		std::string Result; //!< What happened the last time it was run.
	};

	struct GymRun {
		int Course = 0;
		UnitRef Unit;
		Vector Goal; //!< The goal settled onto its floor.
		Timer Clock;
		Timer StillClock;
		Vector LastPos;
		int Still = 0;
		bool Done = false;
	};

	inline std::vector<GymCourse> s_GymCourses;

	inline std::string s_GymScene; //!< The scene the courses were loaded for.

	inline std::vector<GymRun> s_GymRuns;

	inline bool s_GymReported = true;

	inline char s_GymName[64] = "";

	inline char s_GymSettings[1024] = ""; //!< The gym's own settings for this map, "Key = Value" a line, as in Settings.ini.

	constexpr float c_GymArrivedWithin = 40.0F;

	constexpr double c_GymGiveUpMS = 60000.0;

	constexpr const char* c_GymUnitTag = "GymUnit";

	/// Selected units are marked by the game's own selection arrow (drawn by the unit's HUD, with a glow), and the followed one by a marker.
	inline std::vector<UnitRef> s_MarkedSelected; //!< The units carrying the arrow last time, so it can be taken off them.
#pragma endregion

#pragma region Helpers defined in the units (Sandbox*.cpp)
	void AddPresets(std::vector<Preset>& list, const std::list<Entity*>& entities, bool buyableOnly, bool skipBrains, const char* group = "");
	void SortAndDedupe(std::vector<Preset>& list);
	FactionArmoury& ArmouryOf(int moduleID);
	void BuildCatalogue();
	const std::vector<Preset>& ListFor(Tool kind);
	int& ChoiceFor(Tool kind);
	const Preset* ChosenPreset(Tool kind, int choice);
	MovableObject* CreateObject(const std::string& className, const std::string& presetName, int moduleID);
	MovableObject* CreateBaseObject(const char* className, const char* presetName);
	void AddObject(MovableObject* object);
	bool IsCombatant(const Actor* actor);
	/// Whether an actor is a soldier (NC-1): a combatant (IsCombatant) that isn't a non-combatant (an animal, a civilian). What is counted,
	/// sent into battle, carries flags and is hunted; a non-combatant can still be selected and ordered about.
	bool IsSoldier(const Actor* actor);
	/// Whether a preset is a non-combatant's (an Actor whose IsNonCombatant holds, or one in the "Non-combatants" group): kept out of
	/// the units battles and random picks are made from.
	bool IsNonCombatantPreset(const Entity* entity);
	/// Gives a spawned unit a temperament (Actor::Temperament), unless it is -1 (each one's own).
	void ApplyTemperament(Actor* actor, int temperament);
	/// The Spawn tab's temperament choice for spawned units (s_SpawnTemperament).
	void TemperamentCombo(const char* label);
	bool IsSelectable(const Actor* actor);
	MovableObject* ObjectUnder(const Vector& position, bool actorsOnly);
	void SendUnit(Actor* unit, const Vector& waypoint, Actor* target, bool attack, const char* reason, bool lock = false, bool resend = false);
	void HoldUnit(Actor* unit);
	void ApplyPendingOrders();
	void GiveOrder(Actor* actor, Order order);
	void AnswerOrder(Actor* unit, const char* trigger);
	const char* OrderTrigger(Order order);
	void ReturnDefenders();
	void ActivateSide(int team);
	void Detonate(const char* presetName, const Vector& position);
	/// How a ForceBurst pushes.
	enum class ForceShape { Out, In, Up, Along };
	void ForceBurst(const Vector& position, float radius, float speed, ForceShape shape, const Vector& direction = Vector(), bool show = true);
	void SpawnPuffs(const char* presetName, const Vector& position, int radius, int count);
	int PaintedColor(const Material* material, int x, int y, int color, int speckleColor);
	void RecordPaintPixel(const SLTerrain* terrain, int x, int y);
	void ClosePaintUndoStep(bool always);
	void UndoPaint();
	void NotePlaced(const MovableObject* object);
	void NotePaint(const Box& area, const char* kind, const char* material, bool toldCollapse, bool toldLiquid, bool changed);
	void PaintTerrain(const Vector& center, int radius, const char* materialName, BrushShape shape = BrushShape::Circle, float goldShare = 0.0F, int over = 0);
	void FillTerrainShape(const Stroke& stroke);
	/// Where and how a plant brush's plant goes on the ground (PlanPlant): the pictures it is drawn from and its top left corner, in scene pixels.
	struct PlantPlacement {
		const TerrainDebris* Debris = nullptr;
		const TerrainDebris* Leaves = nullptr; //!< Trees' leaves and candles' wicks, drawn over the piece, or none.
		BITMAP* Piece = nullptr;
		BITMAP* LeafPiece = nullptr;
		int Left = 0;
		int Upper = 0;
		int GroundX = 0; //!< The last air over the ground it stands on.
		int GroundY = 0;
		float Scale = 1.0F; //!< How big it is drawn (candles in whole steps).
		bool Mirror = false;
	};
	bool PlanPlant(const Vector& at, int radius, Tool kind, float scale, const PlantRoll& roll, PlantPlacement& out);
	void PlacePlant(const Vector& at, int radius, Tool kind, float scale = 1.0F, const PlantRoll* roll = nullptr);
	inline PlantRoll s_NextPlant; //!< The plant the plant brush in hand puts down next, shown under the cursor; rolled again as each is queued.
	/// One picture a plant brush can put down: one piece of one of its debris presets (and its leaves or wick, for trees and candles).
	struct PlantPicture {
		const TerrainDebris* Debris = nullptr;
		const TerrainDebris* Leaves = nullptr;
		BITMAP* Piece = nullptr;
		BITMAP* LeafPiece = nullptr;
		const char* Group = ""; //!< Which of the brush's kinds it is (small cacti, red mushrooms, ...).
	};
	/// Every picture a plant brush can put down, in order: its gallery (Paint > Plants).
	std::vector<PlantPicture> PlantGallery(Tool kind);
	/// Which of its pictures a plant brush puts down (Paint > Plants): any, or only the ones picked from its gallery, at random or in turn,
	/// facing either way or one.
	struct PlantPick {
		std::vector<int> Chosen; //!< Gallery pictures picked, in the order picked; none for any of them.
		bool InTurn = false; //!< The ones picked one after another, rather than at random.
		int Turn = 0; //!< The next of the ones picked, in turn.
		int Facing = 0; //!< 0 either way at random, 1 as drawn, 2 mirrored.
	};
	inline std::map<Tool, PlantPick> s_PlantPicks; //!< Each plant brush's pick (Paint > Plants).
	PlantRoll RollPlant(Tool kind);
	/// The gallery picture a roll puts down for a plant brush, or -1 for none.
	int PlantEntryOf(Tool kind, const PlantRoll& roll, const std::vector<PlantPicture>& gallery);
	/// The next plant shown under the cursor moved on to the next of the brush's pictures (the next picked, or the next of all), or back (E, Shift+E).
	void StepNextPlant(Tool kind, int step);
	/// The next plant shown under the cursor turned the other way (F).
	void FlipNextPlant(Tool kind);
	const char* TerrainBrushMaterial(Tool kind);
	void PaintBox(const Vector& topLeft, int boxWidth, int boxHeight, const char* materialName);
	bool TakesSide(Tool kind);
	void ClearBox(const Vector& topLeft, int boxWidth, int boxHeight);
	void ClearMap(const Stroke& stroke);
	void ScanMapMaterials();
	void ClearMapPopup();
	void QueueSimChange(Tool kind, int count = 0);
	glm::vec3 Hue(float turn);
	void UpdateEffects();
	void UpdateDecor();
	const char* DecorName(int kind);
	const char* DecorTip(int kind);
	void PlaceDecor(DecorKind kind, const Vector& position);
	/// The effects made in the Effects tab: read from and written to their file, and the starting points offered for a new one.
	void LoadCustomEffects();
	void SaveCustomEffects();
	constexpr int c_EffectTemplateCount = 7;
	const char* EffectTemplateName(int index);
	CustomEffect EffectTemplate(int index);
	void RunEffectLayers(PlacedEffect& effect, const Vector& at, float phase);
	void Launch(int delay, const Vector& from, const Vector& target, float speed, const char* preset, int crater, const char* className = "TDExplosive", int team = 0);
	void UpdateIncoming();
	void StrikeLightning(const Vector& target);
	void GiveLoadout(Actor* actor, const Preset& unit, int loadout);
	Actor* CreateUnit(const Preset& preset, int team, int loadout, Order order);
	float DropUnits(std::vector<Actor*>& units, int team, float x, int craft, bool invincible = false);
	void KeepCraftWhole(ACraft* ship);
	void SpawnUnits(const Stroke& stroke, bool brain);
	std::vector<const Preset*> RandomUnitPool(bool favouritesOnly, int faction = -1);

	/// A combo to pick where random units come from: every faction, the favourites, or one faction. Sets the three values, which are
	/// the arguments of RandomUnitPool. Returns whether the choice changed. (The Battle tab's factions use the same pool call.)
	bool RandomSourceCombo(const char* label, bool& favouritesOnly, int& faction);

	/// A short name for such a source, for the tool bar and tooltips: "Random units", "Random favourites" or "Random Coalition".
	std::string RandomSourceName(bool favouritesOnly, int faction);

	/// Takes the units without a jetpack out of a pool to pick from.
	inline void DropJetless(std::vector<const Preset*>& pool) {
		std::erase_if(pool, [](const Preset* unit) { return !unit->Jetpack; });
	}
	const Preset* RandomPick(const std::vector<const Preset*>& pool);
	void DropSquad(const Stroke& stroke);
	void SpawnItem(const Stroke& stroke);
	Vector StructureCorner(const Preset& preset, const Vector& center, bool snap);
	Vector StructurePosition(const Preset& preset, const Vector& click, bool snap);
	void PlaceStructure(const Stroke& stroke);
	void TakeControl(const Vector& position);
	void StopFlying();
	void ReleaseControl();
	const Preset* FindPreset(const std::vector<Preset>& list, const std::string& presetName);
	Vector OpenAirAt(Vector place);
	Vector StandingPlaceBelow(Vector place);
	Actor* MakePlayer(const Vector& place);
	void EnterPlayer(bool atPlace, const Vector& place);
	void UpdatePlayer();
	void SelectInBox(const Vector& cornerA, const Vector& cornerB);
	std::vector<Vector> StandingSpots(const Vector& around, int count, float stride = 0.0F);
	std::vector<Vector> FormationSpots(const std::vector<Actor*>& units, const Vector& point, int count, int facing = 0);
	const std::vector<SpotReach>& SpotReachPreview(const std::vector<Actor*>& units, const Vector& point);
	std::vector<Actor*> UnitsToMove(int team, bool selectedOnly);
	void MoveUnitsTo(std::vector<Actor*> units, const Vector& point, bool attackMove = false, int facing = 0);
	void FacingMoveSelected(const Vector& point, const Vector& facingPoint, bool shift);
	void AddNoRoute(Actor* unit, const Vector& destination, const std::string& reason = std::string(), bool dig = false);
	int OrderKindFor(const char* reason);
	std::vector<DigPlan> DigPlansFor(const std::vector<Actor*>& units, const Vector& point);
	int DigFailReason(const DigPlan& plan);
	void DigUnitsTo(const std::vector<Actor*>& units, const Vector& point);
	const DigPreview& DigToPreview(const std::vector<Actor*>& units, const Vector& point);
	std::string DigVerdict(const DigPreview& preview);
	void UpdateMoveWatch();
	void ReissueNoRoute(const Vector& destination);
	void MapOrder(const Vector& point, bool shift);
	bool HiddenFromCommander(const Actor* actor);
	MovableObject* GuardableObjectAt(const Vector& position, int team);
	const Colony::Building* BuildingAt(const Vector& position);
	void GuardObject(const std::vector<Actor*>& units, MovableObject* object, const Colony::Building* building);
	void UpdateGuards();
	bool CommanderLooking();
	void UpdateCommander();
	void CommanderPanel();
	void UpdatePace();
	const Actor* FollowedBy(const Actor* unit);
	void GuardUnit(Actor* unit, Actor* leader);
	int SelectionTeam();
	void MarkOrder(const Vector& at, ImU32 color);
	void CommandSelected(const Vector& position, int modifier);
	void PlanStepFor(std::vector<Actor*> units, PlanKind kind, const Vector& place, Actor* target, int facing = 0);
	void DefendAtSelected(const Vector& point, const Vector& facingPoint, bool shift);
	void SuppressSelected(const Vector& point);
	bool SuppressZoneOf(const Actor* unit, Vector& centre, float& radius);
	void ClearSuppress(Actor* unit);
	void PatrolSelected(const std::vector<Vector>& points, bool backAndForth);
	void DropPlan(const Actor* unit);
	void UpdatePlans();
	void DropPlanStep(long unitID, int step);
	Actor* EnemyNear(const Vector& point, int team);
	void OrderSelectedUnits(int choice, const Vector& point);
	int SelectedRule(bool weapons);
	int SelectedAIMode();
	void QueueRule(bool weapons, int rule);
	void QueueOrder(Order order);
	void FindAction();
	std::vector<const Preset*> FactionUnits(int moduleID);
	void UpdateBattle(bool aiPaused);
	void ApplyBattleStroke(const Stroke& stroke);
	void SendBattleSettings(int team, int command = BattleSet);
	void ForgetBattle();
	void UpdateBattleDefenders();
	void BattleTab();
	/// A game has started: the Battle tab's setup saved to load with its map goes in once the map is there (BattlePresetsUpdate).
	void BattlePresetsNewGame();
	void BattlePresetsUpdate();
	/// The Battle tab's setups saved for the map, to save, load, delete, or mark to load with the map.
	void BattlePresetsPanel();
	void DrawBattleMarks();
	void TakeBattleTool(Tool kind, int team);
	bool AddZoneCorner(std::vector<Vector>& draft, BattleSettings& settings, const Vector& position, float closeWithin);
	bool CloseSpawnZone(std::vector<Vector>& draft, BattleSettings& settings);
	float ZoneCloseDistance();
	std::vector<ImVec2> ZoneOnScreen(const std::vector<Vector>& zone, float scale);
	void DrawZoneDraft(ImDrawList* drawList, float scale);
	/// Whether a place is inside a zone drawn as a polygon (a spawn zone, a mode's base), across a wrap or not.
	bool IsInZone(const std::vector<Vector>& zone, const Vector& at);
	/// A place picked at random inside a zone drawn as a polygon, on its ground, for something this tall to stand at (its middle).
	Vector SpotInZone(const std::vector<Vector>& zone, float height);
	bool ModeBaseCorner(const Vector& position, float closeWithin);
	bool CloseModeBase();
	void PutDownBattleTool();
	bool FactionPicker(BattleSettings& setup);
	void MakeDefender(Actor* unit, const BattleSettings& settings);
	void RecentreDefenders(int team, const Vector& centre, bool atIt, float radius = -1.0F);
	void MoveDefender(BattleDefender& defender, const Vector& centre, bool atIt, float radius = -1.0F);
	void CommandDefender(Actor* unit, const Vector& centre, const Vector& post);
	void MoveCommandedZone(Actor* unit, const Vector& centre, const Vector& post);
	void DrawDefendZone(ImDrawList* drawList, const Vector& centre, float radius, float chase, ImU32 color);
	void DrawCommandedZones(ImDrawList* drawList);
	bool JoinBattleObjective(Actor* unit);
	void ReleaseFromBattleMode(const Actor* unit);
	void SendBattleMode(int command = BattleModeSet);
	void ApplyBattleMode(const Stroke& stroke);
	BattleSettings ModeTeamSettings(int side, const BattleSettings& card);
	void ModeUnitsMade(int side, const std::vector<Actor*>& wave);

	/// Whether a team is one the player commands in the mode's game that is on: its units get no orders from the mode or the Battle Director.
	bool BattlePlayerCommands(int side);
	Vector ModeSpawnSpot(int side, const std::vector<Vector>& zone, const Actor* unit);
	int ModeRoom(int side, int room);
	void UpdateBattleMode(bool aiPaused);
	void ForgetBattleMode();
	void BattleModeTab();
	void DrawBattleMode();
	/// What a battle mode's tool in hand is for, in the mode chosen, by the pointer (as "Red flag", "Hill"); empty for the tool's own name.
	std::string BattleToolLabel(Tool kind);
	/// @param withCustom Whether custom (the Battle Director's cards) is one of the choices. Battle Command plays the modes only.
	bool BattleModeChooser(bool withCustom = true);
	void LogStroke(const Stroke& stroke);
	void Apply(const Stroke& stroke);
	void QueueStroke(Tool kind, const Vector& position);
	void UpdateFreeCamera();
	ToolLook LookOf(Tool kind);
	void DrawIcon(ImDrawList* drawList, Icon icon, ImVec2 at, float pixel, ImU32 color);
	void TookTool(int toolIndex);
	ImGuiTabItemFlags TestTab(const char* name);

	/// The sandbox window's tabs, in two rows of buttons (a tab bar doesn't wrap): the names of the tabs on offer now, in order.
	std::vector<const char*> VisibleTabs();

	/// Brings the tab asked for (from the bar, or a test run) to the front, then draws the rows of tab buttons, the one showing lit.
	/// Returns whether there is a tab to draw.
	bool DrawTabRows();

	/// Whether a tab is the one showing, to be drawn with its page, and then closed with EndSandboxTab. (As ImGui::BeginTabItem and EndTabItem.)
	bool SandboxTab(const char* name);
	void EndSandboxTab();
	int FindPin(Tool kind, const std::string& presetName);
	void TogglePin(Tool kind, const std::string& presetName);
	void SavePinsFile();
	void CustomEffectsUI();
	int FindFavourite(Tool kind, const std::string& presetName);
	void SaveFavouritesFile();
	void LoadFavouritesFile();
	void ToggleFavourite(Tool kind, const std::string& presetName);
	void DrawFavouriteMark(ImDrawList* drawList, ImVec2 from);
	void DrawPinMark(ImDrawList* drawList, ImVec2 from, ImVec2 to);
	void ToolButtons(std::initializer_list<Tool> tools);
	/// The Paint tab's Metals: a button for each of c_PaintMetals the game has, each taking the Metal tool with that metal.
	void MetalButtons();
	/// A button for each of a list of materials, in its own colour, each taking a tool with that material (the Paint tab's other liquids
	/// and powders, and its other terrain), as the other tools' buttons are.
	/// @param kind The tool each button takes (Tool::PourOther or Tool::TerrainOther).
	/// @param chosen What that tool uses, set by a click.
	/// @param names The materials, by preset name.
	void MaterialButtons(Tool kind, std::string& chosen, const std::vector<std::string>& names);
	void SideChooser();
	void UndoButton(); //!< Takes back the newest step of the shared paint and placing undo (Tool::UndoTerrain), as Ctrl+Z does.
	void PresetList(Tool kind, const char* group = nullptr, float rows = 8.0F);
	void LoadoutChooser(const char* label = "Loadout");
	void ForgetPictures();
	void PaletteColor(int index, unsigned char* rgb);
	const PiecePicture& PictureOf(const Preset& preset);
	bool ChoiceCombo(const char* label, std::string& chosen, const std::vector<std::string>& values);
	/// The picture browser of a list, as on the Spawn tab. @param pickInto If given, a click sets this to the preset name picked (and that
	/// one is shown as chosen) instead of taking up the tool, with search and filters of its own.
	void PictureGrid(Tool kind, const char* group, std::string* pickInto = nullptr);
	void FormationCombo(const char* id);
	void DrawOrderFeedback();
	void DrawMinimap();
	const NoRoute* NoRouteAt(const ImVec2& mouse);
	void LookAtUnits(const std::vector<UnitRef>& units);
	void CommandHotkeys();
	void KeysPage();
	/// Paint > Plants, with a plant brush in hand: which of its pictures it puts down (PlantPick), from a gallery of them all.
	void PlantPickPanel(Tool kind);
	void DrawCursor();
	const PiecePicture& PictureOfFile(const std::string& path);
	const PiecePicture& PictureOfBitmap(BITMAP* bitmap, bool repeat = false);
	int DrawRing(const std::vector<RingItem>& items, int current, bool sticky = false);
	void DrawSideRing();
	ImVec2 ToScreen(const Vector& scenePosition);
	std::string GymSceneName();
	std::string GymFile();
	std::string GymSettingsFile();
	void GymApplySettings();
	void GymLoadSettings();
	void GymSaveSettings();
	void GymLoad();
	void GymSave();
	Vector GymSettle(const Vector& point, float height);
	bool GymRunCourse(int index);
	bool GymRunAll();
	void GymRemoveUnits();
	void GymUpdate();
	void DrawGym(ImDrawList* drawList);
	void GymTab();
	void DrawColony();
	void ColonyTab();
	void UnmarkSelection();
	void DrawSelection();
	void DrawRallyPoints();
	void SideStatus();
	void TimeControls();
	void BarDivider();
	bool ShelfRow();
	void DrawBar();
	void PictureText(ImDrawList* drawList, ImVec2 at, float room, ImU32 color, const char* text);
#pragma endregion

#pragma region Battle Command (CommanderBar.cpp)
	/// While Battle Command is in development, F11 swaps its Commander Toolbar for the sandbox's own bar (and back), so the sandbox's tools can be
	/// tried out in it. Off, the Commander Toolbar is the only bar.
	constexpr bool c_CommanderBarSwitch = true;
	inline bool s_CommanderBar = true; //!< Battle Command: the Commander Toolbar along the bottom (true), or the sandbox's bar (F11, c_CommanderBarSwitch).
	inline bool s_BattlePanelOpen = false; //!< Battle Command: the battle panel (setting the battle up, starting and stopping it) is open.

	/// The side you command in Battle Command with the Commander Toolbar up: the team ticked "You command this team" in the battle (the first
	/// such), else Red. -1 in any other game, or with the sandbox's bar up instead (F11), when every side is yours as in the sandbox.
	int CommandedTeam();

	/// The side whose units alone can be selected and commanded: yours in commander mode (RC-9) and in Battle Command; -1 for every side.
	int OnlySide();

	/// Whether the battle is being set up on screen: the sandbox's Battle tab showing, or Battle Command's battle panel open. Its spawn zones,
	/// drop lines and defence points are drawn on the map then.
	bool BattleSetupShowing();

	/// Battle Command: keeps the battle's commanded team one of the teams in it (Red if none is ticked), every frame. Call from the ImGui frame.
	void KeepCommandedTeam();

	/// The Commander Toolbar along the bottom of the picture in Battle Command: the command tools, the battle panel, your side and its units,
	/// the speed of time and the pause. Above it, the command row (formations, rules, groups) while the Command tool is in hand.
	void DrawCommanderBar();

	/// Battle Command's battle panel: the battle's mode, teams, spawn zones and objectives, started and stopped. What the sandbox's Battle tab
	/// is in the Sandbox game mode, which Battle Command doesn't offer.
	void DrawCommanderPanel();
#pragma endregion

	/// One tile of the bar: a small picture drawn by the caller, with its name under it when given (else the tile is only the picture). The one in use
	/// sits in a dark well with a gold line along its foot and its name in gold; the one under the pointer lightens.
	/// @param labelColor The colour of the name, 0 for the usual.
	/// @return 1 if clicked, 2 if right-clicked, 0 otherwise.
	template <typename DrawPicture> int BarTile(const char* id, const char* label, const char* tip, bool selected, DrawPicture drawPicture, ImU32 labelColor = 0) {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		float pixel = ToolUI::Pixel();
		float picture = std::floor(pixel * 1.5F * 12.0F);
		float pad = pixel * 2.0F;
		float labelWidth = label ? ImGui::CalcTextSize(label).x : 0.0F;
		float width = std::floor(label ? std::max({picture + pad * 4.0F, labelWidth + pad * 4.0F, ImGui::GetFontSize() * 3.4F}) : picture + pad * 2.0F);
		float height = std::floor(pad * 2.0F + picture + (label ? ImGui::GetFontSize() : 0.0F));
		ImVec2 at = ImGui::GetCursorScreenPos();
		int result = ImGui::InvisibleButton(id, ImVec2(width, height)) ? 1 : 0;
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			result = 2;
		}
		bool hovered = ImGui::IsItemHovered();
		ImVec2 to(at.x + width, at.y + height);
		if (selected) {
			drawList->AddRectFilled(at, to, (ToolTheme::Well & 0x00FFFFFF) | (200u << IM_COL32_A_SHIFT));
			drawList->AddRectFilled(ImVec2(at.x, to.y - pixel * 2.0F), to, ToolTheme::Gold);
		} else if (hovered) {
			drawList->AddRectFilled(at, to, (ToolTheme::WellHover & 0x00FFFFFF) | (200u << IM_COL32_A_SHIFT));
		}
		drawPicture(drawList, ImVec2(std::floor(at.x + (width - picture) * 0.5F), at.y + pad), picture);
		if (label) {
			ImU32 ink = selected ? ToolTheme::Gold : labelColor != 0 ? labelColor
			                                                         : (ToolTheme::Text & 0x00FFFFFF) | (215u << IM_COL32_A_SHIFT);
			drawList->AddText(ImVec2(std::floor(at.x + (width - labelWidth) * 0.5F), at.y + pad + picture), ink, label);
		}
		if (hovered && tip && *tip) {
			ImGui::SetTooltip("%s", tip);
		}
		return result;
	}
} // namespace SandboxDetail

using namespace SandboxDetail;
