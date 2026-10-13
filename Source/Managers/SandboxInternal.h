#pragma once

// Shared by the sandbox's source files (Sandbox.cpp and the Sandbox*.cpp units beside it): the sandbox's state, its types and the
// helpers each unit calls in another. Not for use outside the sandbox. The state is C++17 inline variables, one copy for the program.

#include "WindowMan.h"
#include "DebugMan.h"
#include "DebugDraw.h"
#include "Sandbox.h"
#include "ACrab.h"
#include "ACraft.h"
#include "ACShip.h"
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
#include "SoftBodySim.h"
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
#include <unordered_map>
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
#include <functional>
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
		BattleModeFlag, //!< The Battle Director's modes: a click sets where the one neutral flag stands (one flag), or where the ship comes in (capture the ship).
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
		Decor, //!< A light or fire put in the background (Choice a DecorKind): it shines until a blast or a shot destroys it, and nothing collides with it.
		// Appended, so the tools before keep their numbers.
		LandingSpot, //!< Battle Command: a click marks where your reinforcements land (s_LandingSpot), and calls in the one waiting for it.
		PowerTarget, //!< Battle Command: a click is where the commander power waiting for one (s_PendingPower) is used.
		BattleRally, //!< Battle Command: a click marks your team's rally point, where its new units and reinforcements make for once they're in.
		// Appended, so the tools before keep their numbers.
		Vehicle, //!< Puts down the vehicle chosen in the Spawn tab's Vehicles list (s_Vehicles): a cart, a boat, a tank, a ship, a dropship. Nobody in it.
		// The battle's pickups (BattlePickups.cpp): appended, so the tools before keep their numbers.
		BattlePickupSpot, //!< The Battle tab's Pickups: a click puts down another place pickups can come in at.
		BattlePickupZone, //!< The Battle tab's Pickups: each click puts down a corner of a zone pickups can come in anywhere in.
		// Appended, so the tools before keep their numbers.
		Woodcutter, //!< A colony woodcutter: its staff fell the trees round it and bring the wood home (Colony::Kind::Woodcutter).
		// Appended, so the tools before keep their numbers.
		HoldGround, //!< A box dragged out, or a polygon clicked out, where the ground never falls (TerrainCollapse::AddHoldZone; SandboxHold.cpp). Choice 1 takes away the zone at Position, 2 all of them.
		RedoTerrain, //!< Makes again the newest step the undo took back (see s_PaintRedo).
		CopyArea, //!< Copy and paste (S-4): a box dragged out on the world is marked (s_CopyBox), Ctrl+C copies it and Ctrl+V gives a stamp of it to put down.
		// Not tools, but queued the same way: appended, so the tools before keep their numbers.
		SelectionAction, //!< Something done to the selected units (S-8): Count a SelectionDo, and Team the side for SelectionDo::ChangeSide.
		CopyTake, //!< Copies the box from Position to Position2 to the clipboard (s_Clipboard); Choice 1 with the units wholly inside it.
		PasteArea, //!< Puts the clipboard down with its top left corner at Position, one step of the undo history; Choice 1 flipped left to right.
		// Appended, so the tools before keep their numbers.
		GooBlob, //!< A click puts down a round blob (SoftBodySim) of the brush's size; Choice 2 takes every blob away. Material: its kind.
		// Appended, so the tools before keep their numbers.
		SpawnPoint //!< A spawn point on its own (SandboxSpawnPoints.cpp): units come in at it on a clock, as at a battle's spawn zone. Count a SpawnPointDo.
	};

	/// What the World tab's Clear takes off the map (Tool::ClearMap's Count).
	enum class ClearKind {
		Buildings, //!< Doors, the bunker parts put down and colony buildings; with Choice 1, the building materials too (c_BuildingMaterials).
		Liquids, //!< The liquids in Stroke::Materials; with Choice 1, the springs that pour them too.
		Units, //!< Every unit of the side in Team, or of every side with -1. Not doors, nor your character.
		Ground //!< The terrain materials in Stroke::Materials.
	};

	/// What the command strip's actions on the selection do to the selected units (Tool::SelectionAction's Count, S-8). None of them touch
	/// your character, nor the unit you are in, but Heal and Refill.
	enum class SelectionDo {
		Remove, //!< Taken away, one step of the undo history (which puts them back as they were).
		Heal, //!< Health full and every wound closed, as the Supply power and a Medkit do.
		Refill, //!< Every gun they hold or carry full, an empty one reloaded, and their lost gun and grenades given back where that is known.
		Duplicate, //!< A copy of each beside it, on its side with its kit, one step of the undo history; the copies are selected.
		ChangeSide //!< Put on the side in the stroke's Team, their orders forgotten.
	};

	/// What bunkers and the things the sandbox builds are made of, cleared with the buildings.
	constexpr const char* c_BuildingMaterials[] = {"Concrete", "Metal", "Mega Metal", "Mangled Metal", "Door Metal", "Scrap Metal", "Glass", "Civilian Stuff", "Military Stuff", "Ladder", "Gold Plate", "Silver Plate", "Bronze Plate", "Brass Plate", "Copper Plate", "Chrome Plate"};

	/// The Battle tab's tools that set something on a team's card, taken from it and put down with Enter (PutDownBattleTool).
	constexpr bool IsBattleTool(Tool kind) { return kind == Tool::BattleDefendPoint || kind == Tool::BattleDropLine || kind == Tool::BattleSpawnZone || kind == Tool::BattleModePoint || kind == Tool::BattleModeBase || kind == Tool::BattleModeZone || kind == Tool::BattleModeGoal || kind == Tool::BattleModeFlag || kind == Tool::BattlePickupSpot || kind == Tool::BattlePickupZone; }

	/// The colony tools: each builds one kind of colony building.
	constexpr bool IsColonyTool(Tool kind) { return kind == Tool::Barracks || kind == Tool::Extractor || kind == Tool::Generator || kind == Tool::Woodcutter; }

	/// The colony building a colony tool builds.
	constexpr Colony::Kind ColonyKindOf(Tool kind) {
		return kind == Tool::Extractor ? Colony::Kind::Extractor : kind == Tool::Generator ? Colony::Kind::Generator : kind == Tool::Woodcutter ? Colony::Kind::Woodcutter : Colony::Kind::Barracks;
	}

	/// The Battle Director's mode tools that draw a polygon a corner at a click (closed on its first corner, or Enter).
	constexpr bool IsModeZoneTool(Tool kind) { return kind == Tool::BattleModeBase || kind == Tool::BattleModeZone || kind == Tool::BattleModeGoal || kind == Tool::BattlePickupZone; }

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
	    {Tool::GooBlob, "Goo blob", 0.0F, true},
	    {Tool::ForceBlast, "Force blast", 0.0F, false},
	    {Tool::HugeForceBlast, "Huge force blast", 0.0F, false},
	    {Tool::Implosion, "Implosion", 0.0F, false},
	    {Tool::Updraft, "Updraft", 0.0F, false},
	    {Tool::GustRight, "Gust right", 0.0F, false},
	    {Tool::GustLeft, "Gust left", 0.0F, false},
	    {Tool::SmokeBomb, "Smoke bomb", 0.0F, false},
	    {Tool::Fireworks, "Fireworks", 0.0F, false},
	    {Tool::Decor, "Background light", 0.0F, false},
	    {Tool::LandingSpot, "Landing spot", 0.0F, false},
	    {Tool::PowerTarget, "Commander power", 0.0F, false},
	    {Tool::BattleRally, "Rally point", 0.0F, false},
	    {Tool::Vehicle, "Vehicles", 0.0F, false},
	    {Tool::BattlePickupSpot, "Pickup spot", 0.0F, false},
	    {Tool::BattlePickupZone, "Pickup zone", 0.0F, false},
	    {Tool::Woodcutter, "Woodcutter", 0.0F, false},
	    {Tool::HoldGround, "Hold ground", 0.0F, false},
	    {Tool::CopyArea, "Copy area", 0.0F, false},
	    {Tool::SpawnPoint, "Spawn point", 0.0F, false},
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

	/// The shapes the terrain brushes fill with Brush type Shape (s_ShapeFill): dragged out as a box (or a line), filled in one go.
	enum class FillShape {
		Circle, //!< The circle (or oval) in the box.
		Triangle, //!< Its point at the side of the box the drag began, its base along the other.
		Square, //!< The whole box.
		Line //!< A line from where the drag began to where it ends, s_LineWidth wide, its ends rounded (see InLineShape).
	};
	constexpr int c_FillShapeCount = 4;

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

	/// Whether a pixel is in a line dragged from start to end (scene pixels, end not wrapped round from start), width pixels wide with
	/// rounded ends: its middle no further than half the width from the line. (A line 1 px wide still joins up on the slant.)
	inline bool InLineShape(const Vector& start, const Vector& end, float width, int x, int y) {
		float px = static_cast<float>(x) + 0.5F - start.m_X;
		float py = static_cast<float>(y) + 0.5F - start.m_Y;
		float dx = end.m_X - start.m_X;
		float dy = end.m_Y - start.m_Y;
		float lengthSquared = dx * dx + dy * dy;
		float t = lengthSquared > 0.0F ? std::clamp((px * dx + py * dy) / lengthSquared, 0.0F, 1.0F) : 0.0F;
		float ox = px - dx * t;
		float oy = py - dy * t;
		float half = std::max(width * 0.5F, 0.75F);
		return ox * ox + oy * oy <= half * half;
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

	/// Calls off the Lua AI's own errands that put the order they interrupted back when they end: checking where an enemy was seen, and a
	/// non-combatant's run from danger. Taken off by an order the player gives, as a fall-back is (CancelRetreatAndFlank): the AI's own
	/// check for a new order goes by where its waypoints lead, and missed one to near the same place, or one whose waypoint was already
	/// walked into its route, so the old order came back over the new one when the errand ended.
	inline void CancelErrands(Actor* unit) {
		unit->RemoveNumberValue("AIInvestigate");
		unit->RemoveNumberValue("AIFlee");
		unit->RemoveNumberValue("AIFleeing");
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
		float Cost = 0.0F; //!< What its game files say it costs, health and mass, for sorting the lists by (SettingsMan::SandboxSpawnSort); 0 where it has none.
		float Health = 0.0F;
		float Mass = 0.0F;
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
	inline std::vector<Preset> s_Vehicles; //!< What is driven or flown (carts, boats, tanks, flying ships, dropships, rockets), listed apart from the units under the Spawn tab's Vehicles.
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
		bool AppearAnywhere = false; //!< Its units also appear anywhere on the map there is ground to stand on (AnywhereSpot), on the spawn zones' clock,
		                             //!< ZoneUnits a lot (for each zone drawn), rather than only in its zones.
		int KeepClear = 300; //!< Ships coming in anywhere, and units appearing anywhere: how far, in pixels, they keep from enemy units, enemy spawn
		                     //!< zones and a mode's objectives where they can. 0: no gap.
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
		BattleModeStop, //!< The mode's game stopped, and every team with it.
		BattlePower, //!< Battle Command: a commander power used by the team you command (Choice the CommanderPower, Position where).
		BattleReinforce, //!< Battle Command: reinforcements bought for the team you command (Choice the card, Radius how many, Craft, Position where they land).
		BattleSurrender //!< The mode's game given up by Team (SurrenderBattleGame).
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
		CaptureTheShip,
		WaveDefence,
		Escort,
		Extraction,
		ThePoint,
		Count
	};

	/// What the Battle tab says for a mode: the choices every mode shares, and those some use (each says which in its panel). The window keeps
	/// its own copy (s_ModeSetup) and sends it to the sim in a Tool::BattleTeam stroke (Stroke::Mode) whenever it changes.
	constexpr size_t c_MaxFlagSpots = 8; //!< Most flag positions one flag can have placed.
	constexpr size_t c_MaxPickupSpots = 8; //!< Most places pickups can come in at, placed.
	constexpr size_t c_MaxPickupZones = 8; //!< Most pickup zones drawn.
	constexpr int c_MaxPointRings = 4; //!< The point: most rings round its bullseye.
	/// Where a battle's pickups come in (BattleModeSettings::PickupPlaces), as bits: any of them ticked, each pickup at one of them picked at random.
	enum PickupPlace { PickupAnywhere = 1, PickupAtSpots = 2, PickupInZones = 4 };

	/// What a team you command has its units do as they come in (SpawnOrders.cpp): the order a spawn order gives each new unit.
	enum class SpawnOrderKind {
		Hold, //!< Hold ground where it comes in, with no order: as before spawn orders.
		Move,
		AttackMove,
		DefendAt, //!< Posts round the point, as Defend at gives, with the zone's size, chase and roam.
		Patrol, //!< The route, as a loop or back and forth.
		JetpackHere, //!< Flies to the point in the air (those with no jetpack go to the ground under it).
		Objective, //!< The battle objective nearest the point (the nearest to the unit with none): taken, or held if it's the team's own.
		Count
	};
	constexpr const char* c_SpawnOrderNames[] = {"Hold where they land", "Move", "Attack-move", "Defend at", "Patrol", "Jetpack here", "Battle objective"};
	static_assert(std::size(c_SpawnOrderNames) == static_cast<size_t>(SpawnOrderKind::Count), "c_SpawnOrderNames must name each SpawnOrderKind.");

	/// A spawn order: given to each of a team's new units once it is in and on its feet, as if the player had given it then.
	struct SpawnOrder {
		bool Set = false; //!< A spawn zone's: it has its own order, else its units take the team's. (A team's own is always set.)
		SpawnOrderKind Kind = SpawnOrderKind::Hold;
		std::vector<Vector> Points; //!< Where: the one point, a patrol's route, or none (a battle objective with no point).
		int Facing = 0; //!< Which way to face there: -1 left, 1 right, 0 either.
		float Direct = 0.0F; //!< How urgent, as an order placed by clicks is (Actor::StandingOrder::Direct): -1 cautious to 1 the shortest way whatever it takes.
		bool BackAndForth = false; //!< A patrol: back and forth rather than a loop.
		int Radius = 150; //!< Defend at: the zone's reach, chase and roam share, as on the command row when it was set.
		int Chase = 300;
		int Roam = 0;
	};

	/// A spawn point's settings (Tool::SpawnPoint, SandboxSpawnPoints.cpp): a point on its own, not part of a battle, that units come in at on a
	/// clock, picked as a battle team's are and put down as at its spawn zones (or brought in by ship over it).
	struct SpawnPointSettings {
		int Team = 1;
		BattleSettings Units; //!< Who comes: only its Factions, FavouritesOnly, Crabs and JetpackOnly are used (as a battle team's card picks them).
		int PerSpawn = 3; //!< Units each time.
		int EverySeconds = 30; //!< Seconds of game time between one lot and the next.
		bool FirstAtOnce = true; //!< The first lot as soon as it is put down, rather than after EverySeconds.
		int MostAlive = 10; //!< Most of its units alive at once (those still in a ship counted): it only tops up to it. 0: no cap.
		int TotalLimit = 0; //!< Units it makes in all, after which it stops. 0: no limit.
		int Radius = 60; //!< How far round the point, in pixels, its units appear.
		bool ByShip = false; //!< Brought in by ship over the point rather than appearing at it.
		int Craft = 0; //!< Index into c_Crafts.
		bool Invincible = false; //!< Its ships take no harm.
		Order Behaviour = Order::Hold; //!< What its units are made to do (Order::Hold, Attack or HuntBrains), before any spawn order.
		SpawnOrder Arrival; //!< The spawn order each is given once it's in and on its feet; Hold for none.
	};

	/// What a Tool::SpawnPoint stroke does (its Count). Choice is the point's ID, for all but SpawnPointPlace and SpawnPointRemoveAll.
	enum SpawnPointDo {
		SpawnPointPlace = 0, //!< A new point at Position, with the stroke's SpawnPoint settings.
		SpawnPointEdit, //!< Its settings set to the stroke's.
		SpawnPointRemove,
		SpawnPointRemoveAll,
		SpawnPointPause, //!< Stops it (Radius 0) or starts it again (Radius 1).
		SpawnPointNow, //!< Its next lot comes now.
		SpawnPointReset //!< The count of units it has made back to none, so a limit reached starts afresh.
	};

	struct BattleModeSettings {
		BattleMode Mode = BattleMode::Custom;
		std::array<int, c_Sides> TeamSize = {16, 16, 16, 16}; //!< Most units each team has alive at once, by side.
		std::array<bool, c_Sides> Plays = {true, true, false, false}; //!< The teams taking part, by side.
		std::array<std::vector<std::vector<Vector>>, c_Sides> SpawnZones; //!< Each team's spawn zones, drawn as polygons: its units appear in them.
		std::array<bool, c_Sides> HasPoint{}; //!< Each team's point placed in its base (capture the flag: where its flag stands). Without, one is picked.
		std::array<Vector, c_Sides> Points;
		std::array<std::vector<Vector>, c_Sides> Goals; //!< One flag: each team's goal zone, drawn as a polygon, that it brings the flag into to score.
		std::vector<Vector> FlagSpots; //!< One flag (and capture the ship, its ship positions): the flag positions placed (up to c_MaxFlagSpots), which the flag comes in at in turn, the next after each score.
		bool FlagByZones = false; //!< One flag: the flag comes in somewhere in one of the flag spawn zones (the mode's zones), picked at random, rather than at the positions placed.
		bool ByShip = false; //!< Its units come in by ship over their base, rather than appearing in it.
		bool Anywhere = false; //!< Every mode: its units come in anywhere on the map (appearing on any ground, or by ship anywhere across it, with
		                       //!< ByShip) rather than at their base. The spawn zones still mark each team's base (its flag, its defenders).
		int KeepClear = 300; //!< With Anywhere: how far, in pixels, new units keep from enemy units, enemy spawn zones and the objectives. 0: no gap.
		bool MoveStuckPoint = true; //!< Capture the flag: a flag nobody can get to (buried, or cut off) moves somewhere else in its base.
		int ScoreToWin = 3; //!< Capture the flag: captures that win. 0 plays on for good.
		int GuardPercent = 30; //!< Capture the flag: the share of each team's units, in percent, that stay to guard its flag.
		int EscortPercent = 50; //!< Capture the flag and one flag: the share of a carrier's team-mates, in percent, that go with it all the way to score; the rest go with it only until it's halfway home, then stay there to hold the ground ahead.
		int ReturnSeconds = 30; //!< Capture the flag: how long a dropped flag lies before it goes back home by itself.
		int RespawnSeconds = 5; //!< Every mode: seconds after one of a team's units falls before another comes in its place.
		int MaxRespawns = 0; //!< Every mode: fallen units each team gets back in all, after its first team size. 0: no limit.
		int StuckSeconds = 20; //!< Every mode: seconds a unit can get no nearer its objective before it is respawned. 0: never.
		std::array<int, c_Sides> RiskMin = {-100, -100, -100, -100}; //!< Every mode: the least risk each team's units take with the mode's orders, -100 (cautious, the default) to 100 (suicide), 0 normal (Actor::StandingOrder::Direct).
		std::array<int, c_Sides> RiskMax = {0, 0, 0, 0}; //!< And the most: each unit is given a risk in between as it comes in, the strongest (the biggest jetpack, the priciest kit) toward the top.
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
		bool BlockLastRespawns = false; //!< Assault: no more respawns for the defenders while the last objective is in play and being fought over or taken.
		int Tickets = 60; //!< Last team standing: units each team gets in all, its first ones counted.
		int KillsToWin = 5; //!< VIP hunt: enemy VIPs a team has to bring down to win.
		int VipRespawnSeconds = 20; //!< VIP hunt: seconds before a fallen VIP's team has a new one.
		int Defender = 0; //!< Wave defence: the side that holds the defence zone (the mode's first zone); the rest attack in waves.
		int Waves = 10; //!< Wave defence: waves the defenders have to beat to win. 0: endless.
		int FirstWave = 6; //!< Wave defence: attackers the first wave brings in all, shared between the attacking teams.
		int WaveGrowth = 3; //!< Wave defence: how many more each wave brings than the one before.
		int WaveBreak = 20; //!< Wave defence: seconds between a wave beaten and the next (and before the first).
		int EscortSide = 0; //!< Escort: the side that takes its VIP to the destination (the mode's first zone); the rest hunt it. Escort and
		                    //!< extraction use TimeLimit too (0: none).
		int ExtractSide = 0; //!< Extraction: the side that gets its units out through the extraction zone (the mode's first zone), from Tickets units in all.
		int ExtractToWin = 10; //!< Extraction: units it has to get out to win.
		int ExtractSeconds = 3; //!< Extraction: seconds a unit stands in the zone to be lifted out.
		int PointRings = 2; //!< The point: rings round its bullseye, 0 to c_MaxPointRings.
		std::array<int, c_MaxPointRings + 1> RingRadius = {60, 130, 200, 270, 340}; //!< The point: how far out from its middle (the mode's first flag
		                                                                          //!< position) each band reaches, the bullseye first, then each ring.
		std::array<int, c_MaxPointRings + 1> RingPoints = {5, 3, 1, 1, 1}; //!< The point: points a second for holding each band alone. Each is held,
		                                                                   //!< scored and ordered onto (a battle objective) on its own.
		int PointsToWin = 300; //!< The point: points that win. 0 plays on for good.
		std::array<bool, c_Sides> Commander = {true, true, true, true}; //!< Every mode but custom: each team an AI commander leads (UpdateCommanders),
		                                                               //!< as a player would: splitting its units between objectives, sending some
		                                                               //!< after a flag or to a VIP, picking who to go for. Not a team you command.
		int CommanderLevel = 1; //!< How hard the commanders play: 0 easy, 1 normal, 2 hard (how often they look, and how much they send).
		int CommanderReserve = 30; //!< Its share of each such team's units, in percent, held on the next objective while the one in play is safe.
		bool CommanderSquads = true; //!< Normal and hard: each such team's runners and hunters go in squads, following a leader (Commander squads,
		                             //!< SandboxBattleModes.cpp). Not its defenders, nor on assault, king of the hill or capture the ship.
		int CommanderFallBack = 60; //!< Assault defenders: how far the attackers' taking of the objective in play has got, in percent, when everyone
		                            //!< falls back to the next one.
		bool FogOfWar = false; //!< Battle Command: you see only the enemies your team's units can see (BattleFog.cpp).
		bool FieldBrain = false; //!< Battle Command: your brain is on the field at your base; if it dies you lose, and orders only reach your
		                         //!< units within FieldBrainRange of it (BattleFieldBrain.cpp).
		int FieldBrainRange = 900; //!< Its command range, in pixels.
		bool HotSeat = false; //!< Battle Command: two players on one screen, each commanding one of the teams ticked "You command this team" in
		                      //!< turn (BattleHotSeat.cpp).
		int TurnSeconds = 30; //!< Hot-seat: seconds of the battle each turn.
		bool PickArmy = false; //!< Battle Command: the team you command fields the army picked (Army) within ArmyPoints: its units, the first and
		                       //!< those after one falls, come from that roster till it's used up (BattleArmy.cpp).
		int ArmyPoints = 3000; //!< What the whole army may cost.
		std::array<int, 5> Army = {8, 2, 2, 2, 0}; //!< How many of each reinforcement card (by c_ReinforcementCards) the army brings.
		bool MovingSpawns = false; //!< Every mode: during the game, the player can move the spawn zones of a team they command, behind the spawn line (BattleEditMouse). Chosen before it starts.
		std::array<bool, c_Sides> PlayerCommands{}; //!< Every mode: each team the player commands. Its units come in on hold ground with no orders (or its spawn order) and
		                                            //!< no job from the mode, for the player to command (as in an RTS); the rest of the mode (spawns, scoring) is as for any team.
		std::array<SpawnOrder, c_Sides> SpawnOrders; //!< Every mode: what each team the player commands has its new units do (its reinforcements too), unless they come in
		                                             //!< in a spawn zone with its own order.
		std::array<std::vector<SpawnOrder>, c_Sides> ZoneSpawnOrders; //!< And each of its spawn zones' own, by the zone's place in SpawnZones (none past the end: the team's).
		bool Pickups = false; //!< Every mode: powerups (an energy shield, a stim, a spell scroll...) come in on the map now and then, for the first unit
		                      //!< to reach each to take into its inventory (BattlePickups.cpp).
		int PickupEvery = 45; //!< Seconds, about, between one pickup coming in and the next.
		int PickupMost = 3; //!< Most pickups on the map at once.
		int PickupLasts = 90; //!< Seconds a pickup nobody takes lies before it goes. 0: till it's taken.
		int PickupKinds = 0x7F; //!< Which kinds come in (bits by c_PickupKinds).
		int PickupPlaces = PickupAnywhere; //!< Where they come in (PickupPlace bits).
		std::vector<Vector> PickupSpots; //!< The places placed (up to c_MaxPickupSpots).
		std::vector<std::vector<Vector>> PickupZones; //!< The pickup zones drawn (up to c_MaxPickupZones).
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

	/// How a vehicle from the Spawn tab's Vehicles list is put down: as it comes, or with its driving tuned (multipliers on its own values),
	/// and empty or with every seat filled. Saved with the sandbox settings (Sandbox::GetVehicleSetup).
	struct VehicleTuning {
		bool Modified = false; //!< Whether the multipliers below are used. Off: the vehicle as its preset has it.
		bool Crewed = false; //!< Whether it is put down with new units in it (Driver, and Crew unless DriverOnly), a squad behind the driver.
		bool DriverOnly = false; //!< With Crewed: only the driver's seat filled (With driver), rather than every seat (Full crew).
		std::string Driver; //!< With Crewed: who drives (seat 0). "" the unit picked in the Units list (or random ones, as it says), "*" random units, else a unit by CrewPickKey.
		std::string Crew; //!< With Crewed: who takes the other seats (gunners and passengers), as Driver.
		float Power = 1.0F; //!< Times its Acceleration: how hard it speeds up, and so how steep a hill it drives up.
		float TopSpeed = 1.0F; //!< Times its MaxSpeed.
		float Grip = 1.0F; //!< Times its Grip: how much of its weight it can push with before the wheels slip.
		float Brakes = 1.0F; //!< Times its BrakeStrength.
		float Friction = 1.0F; //!< Times its RollingResistance: how quickly it slows coasting.
		float Jump = 1.0F; //!< Times its HopSpeed.
		float WaterPower = 1.0F; //!< Times its WaterThrust (boats).
		float WaterSpeed = 1.0F; //!< Times its WaterMaxSpeed (boats).
		float TerrainCrush = -1.0F; //!< How tough a material it smashes through (AVehicle::SetTerrainCrush), not a multiplier; -1 its own.
		float Health = 1.0F; //!< Times its MaxHealth (it is put down at full health).
	};
	inline VehicleTuning s_VehicleTuning;

	/// How units from the Spawn tab (put down, dropped, or crewing a vehicle) are made: as they come, or with their stats tuned (multipliers
	/// on each one's own values). Saved with the sandbox settings (Sandbox::GetUnitSetup).
	struct UnitTuning {
		bool Modified = false; //!< Whether the values below are used. Off: each unit as its preset has it.
		float Health = 1.0F; //!< Times its MaxHealth (it is put down at full health).
		float Toughness = 1.0F; //!< Its own toughness (Actor::SetToughnessMultiplier), on top of the Unit toughness setting.
		float Speed = 1.0F; //!< Times how fast it walks, runs and crawls (Actor::SetSpeedMultiplier).
		float Jump = 1.0F; //!< Times how high a leap of its legs lifts it.
		float JetFuel = 1.0F; //!< Times its jetpack's fuel (JetTimeTotal).
		float JetThrust = 1.0F; //!< Times its jetpack's push (AEJetpack::SetThrustMultiplier), at no extra fuel.
		bool JetEndless = false; //!< Its jetpack never runs dry (kept full each sim update, UpdateLoadoutUnits).
		float Sight = 1.0F; //!< Times how far it sees (SightDistance).
		float FireRate = 1.0F; //!< Times the rate of fire of every gun it is made with.
	};
	inline UnitTuning s_UnitTuning;
	constexpr float c_UnitTuneMin = 0.01F; //!< The least a unit stat multiplier goes down to.
	constexpr float c_UnitTuneMax = 100.0F; //!< The most a unit stat multiplier's slider goes up to.
	constexpr float c_TuneTypedMax = 1000.0F; //!< The most a unit or vehicle multiplier goes up to, typed in (Ctrl+click on its slider).
	constexpr float c_VehicleTuneMin = 0.01F; //!< The least a vehicle tuning multiplier goes down to.
	constexpr float c_VehicleTuneMax = 100.0F; //!< The most a vehicle tuning multiplier's slider goes up to.
	constexpr float c_VehicleCrushMax = 500.0F; //!< The most the Terrain damage slider goes up to: past metal (400).

	/// A unit of your own design (the Loadout Builder): a unit preset and exactly what it carries. Kept in c_LoadoutsFile, by name.
	struct CustomLoadout {
		std::string Name;
		std::string Body; //!< The unit preset it is made as.
		std::vector<std::string> Items; //!< What it carries, by item preset name, in order: the first is what it holds.
		int Temperament = -1; //!< Actor::Temperament it is given, -1 for the unit's own.
		bool FactionKit = false; //!< A squad's plain unit: armed as the Spawn tab's "Faction default" arms it, not with Items.
		std::string Jetpack; //!< "" the unit's own jetpack, "-" none, else the jetpack fitted, as "Module/PresetName" (JetpackKey).
		float JetFuel = 1.0F; //!< Times its jetpack's fuel (JetTimeTotal).
		bool JetEndless = false; //!< Its jetpack never runs dry (kept full each sim update, UpdateLoadoutUnits).
		float JetThrust = 1.0F; //!< Times its jetpack's push (AEJetpack::SetThrustMultiplier), at no extra fuel.
	};

	/// One line of a squad: a plain unit (with its faction's guns) or one of your loadouts, and how many of it.
	struct SquadSlot {
		std::string Name; //!< The unit preset's name, or the loadout's (Custom).
		bool Custom = false;
		int Count = 1;
	};

	/// A squad of your own design (the Squad Builder). Its first unit leads; the rest follow it.
	struct CustomSquad {
		std::string Name;
		std::vector<SquadSlot> Slots;
	};

	/// What the Spawn tab's units are made as besides the picture picked: nothing else, one of your loadouts, or one of your squads.
	struct CustomPick {
		enum Kinds { None, Loadout, Squad } Kind = None;
		std::string Name;
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
		int Clicks = 0; //!< Tool::Command: how many clicks in a row placed it, 0 for not by a click: one makes the order cautious, two or more normal (s_DirectOrder).
		bool HasRisk = false; //!< Tool::Command: placed by a double click dragged, so Risk says how urgent it is, not Clicks.
		float Risk = 0.0F; //!< With HasRisk, how far the drag went toward the full risk distance, 0 (normal) to 1 (fully direct).
		int OrderMode = -1; //!< Tool::Command: the CommandMode in hand when it was placed (the mode can go back to Command before it's given), -1 for the one in hand then.
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
		int Style = 0; //!< Colony buildings: how they look (Colony::Style), taken at the click.
		bool Lite = false; //!< Colony buildings: just the core, a box with its brain in (Colony::Building::Lite), taken at the click.
		VehicleTuning Vehicle; //!< Tool::Vehicle: how it is tuned and crewed (s_VehicleTuning), taken at the click.
		UnitTuning Stats; //!< Units, drops and a vehicle's crew: how their stats are tuned (s_UnitTuning), taken at the click.
		int DriverChoice = -1; //!< Tool::Vehicle with Vehicle.Crewed: the driver, an index into the Units list (Vehicle.Driver resolved at the click, ResolveCrewPick).
		bool DriverRandom = false; //!< Tool::Vehicle with Vehicle.Crewed: the driver a random unit, from FavouritesOnly and RandomFaction.
		int CrewChoice = -1; //!< Tool::Vehicle with Vehicle.Crewed: the rest of the crew, as DriverChoice (Vehicle.Crew).
		bool CrewRandom = false; //!< Tool::Vehicle with Vehicle.Crewed: the rest of the crew random units, as DriverRandom.
		std::vector<CustomLoadout> Customs; //!< Units and drops: each unit to make, from one of your loadouts or squads (a squad laid out a unit each), taken at the click. Empty for the unit picked.
		std::string SquadName; //!< With Customs from a squad: its name. They are spawned as that squad (FormSquad).
		std::vector<CustomLoadout> DriverCustom; //!< Tool::Vehicle with Vehicle.Crewed: the driver is one of your loadouts (one entry), taken at the click.
		std::vector<CustomLoadout> CrewCustom; //!< As DriverCustom, for the rest of the crew.
		SpawnPointSettings SpawnPoint; //!< Tool::SpawnPoint: the point's settings, for SpawnPointPlace and SpawnPointEdit.
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
	inline int s_ClickCount = 0; //!< How many clicks in a row the drag or click under way began with: how urgent its order is (s_DirectOrder).
	/// While an order placed by clicks is given (a command stroke's Clicks or Risk), how urgent it is: -1 one click (cautious), 0 two (normal, and
	/// any order not placed by a click), and a double click dragged 0 to 1 by how far (SettingsMan::DirectOrderTuning::RiskDragPixels). Every unit sent
	/// (SendUnit) takes that share of the Battle Behavior tuning (Actor::StandingOrder::Direct), and one over 0 isn't held to the group's pace.
	/// Set round the stroke, so every kind of order placed by a click (move, attack-move, dig to, defend, a map order) is made so the same way.
	inline float s_DirectOrder = 0.0F;
	/// While an order to a point in the air is given (Jetpack here, or a move clicked in open air; see MoveUnitsTo), whether the units sent
	/// fly to the point itself (Actor::StandingOrder::SkyGoal).
	inline bool s_SkyOrder = false;
	inline bool s_ClickOrder = false; //!< While a command stroke the player placed is given: only those take a move clicked in open air for a sky order.
	/// Whether a point is open air with nothing under it for 120 px: a move or attack-move clicked there flies the units with a jet to it.
	bool IsSkyPoint(const Vector& point);
	/// Whether a place a unit is sent to is up in the air, out of reach of one standing on the ground under it (open air there and for
	/// most of the unit's height below): a battle objective drawn or put in the air, which it flies to rather than stopping under.
	bool OffTheGround(const Actor* unit, const Vector& point);
	/// Sends a unit with a jet that is going to a place off the ground (OffTheGround) to fly to the place itself, as Jetpack here does
	/// (Actor::StandingOrder::SkyGoal); any other place, or a unit with no jet, is gone to on the ground as usual. True when it flies.
	bool FlyIfOffTheGround(Actor* unit, const Vector& point);
	/// Whether a unit sent to a place off the ground has come down short of it (its tank ran out: a sky order ends then) and is to be
	/// sent to fly there again, which it does once its tank has filled enough.
	bool FlyAgainFor(const Actor* unit, const Vector& point);
	/// The selected units to the point in the air (CommandMode::JetpackHere): those with a jet fly to it, the rest go to the ground under it.
	void JetpackHereSelected(const Vector& point, bool shift, int facing = 0);
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
	// What the battle shows on the map, kept between launches in the settings (B-12): ShowBattleModeZones (a mode's own zones: hills, assault
	// objectives, goal zones; always while one is being drawn), ShowBattleInfo (the panel under the score), ShowBattleObjectives (each mode's
	// objectives lit up), BattleObjectiveLook (0 each mode's own look, else an ObjectiveLook plus one for all of them) and ShowBattleSpawnZones
	// (the teams' spawn zones; always while one is being drawn, or a point placed).
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
	/// A word shown by the pointer for a moment from s_PointerNoteAt (ImGui time): the brush's new size ([ ], Ctrl+wheel), or what the
	/// eyedropper picked (Alt+click).
	inline std::string s_PointerNote;
	inline double s_PointerNoteAt = -10.0;
	inline bool s_EyedropHeld = false; //!< The left button is still down from an Alt+click that picked something: it paints and places nothing until let go.
	inline float s_PlantScale = 1.0F; //!< How big the plant brushes draw their plants, 1 as the game's own art (Paint > Plants).
	inline int s_PlantSpacing = 10; //!< How far apart along the stroke the plant brushes put plants, in pixels (Paint > Plants).
	inline float s_LastPlantX = 0.0F; //!< Where across the plant brush last put a plant, for the spacing.
	inline bool s_ShapeFill = false; //!< Brush type Shape: the terrain brushes fill a shape dragged out on the world rather than painting where the pointer goes.
	inline FillShape s_FillShape = FillShape::Square; //!< The shape they fill then.
	inline int s_LineWidth = 8; //!< How wide the Line shape is, in pixels.
	constexpr int c_MaxLineWidth = 120; //!< The widest the Line shape goes.
	inline bool s_ShapeDragging = false; //!< A shape being dragged out, from s_ShapeStart.
	inline bool s_ShapeDigging = false; //!< The shape being dragged out with the right button: dug out when let go, whatever brush is in hand.
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
	inline int s_GooType = 0; //!< What the Goo blob tool puts down (SoftBodySim::GetType).
	inline std::vector<Vector> s_RopeDraft; //!< The window's: the points of the rope being put down, clicked so far (the line to the pointer is drawn from the last).
	inline ImVec2 s_RopeRightStart; //!< Where the right button went down with the Rope tool in hand: let go about there, it finishes the rope.
	inline bool s_RopeRightDown = false;
	inline int s_RopeDrawing = 0; //!< The sim's: the rope its Rope clicks carry on, 0 for none (the next click starts one).
	inline Vector s_ShapeStart;
	inline bool s_LineDragging = false; //!< A line being dragged out on the world, from s_LineStart: a lift's or moving platform's track (DragsTrack) or a bunker run (DragsRun).
	inline Vector s_LineStart;
	constexpr int c_MaxDropSide = 800; //!< The biggest box "Make it fall" takes either way, in pixels.
	inline std::vector<Vector> s_HoldDraft; //!< "Hold ground": the corners of the polygon being clicked out, each placed from the first.

	/// What Copy area copied (S-4): the ground of a box, pixel by pixel, and the units wholly inside it. Kept in memory only, until the
	/// game is closed (a new game keeps it, so ground can be taken from one map to another).
	struct Clipboard {
		/// A unit copied: a copy as it was (never in the world), and where it was from the box's top left corner.
		struct Unit {
			std::shared_ptr<Actor> Copy;
			Vector Offset;
		};
		int Width = 0;
		int Height = 0;
		std::vector<unsigned char> Material; //!< Width x Height, row by row; 0 is air.
		std::vector<unsigned char> Foreground; //!< The 8 bit foreground colours.
		std::vector<unsigned char> Background; //!< The 8 bit background colours.
		std::vector<Unit> Units;
		int Version = 0; //!< Counts up with each copy, so the window knows to draw its preview again.

		bool Empty() const { return Width <= 0 || Height <= 0; }
	};
	inline Clipboard s_Clipboard; //!< The sim's: written by Tool::CopyTake, read by the window for the stamp's preview (between updates).
	constexpr int c_MaxCopySide = 800; //!< The biggest box Copy area takes either way, in pixels.
	inline bool s_CopyBoxSet = false; //!< A box is marked with Copy area, from s_CopyBoxA to s_CopyBoxB (scene positions).
	inline Vector s_CopyBoxA;
	inline Vector s_CopyBoxB;
	inline bool s_Stamping = false; //!< Copy area: the clipboard follows the pointer as a stamp, to be put down with a click.
	inline bool s_StampFlipped = false; //!< The stamp is flipped left to right (F while stamping).
	inline bool s_CopyUnits = true; //!< Copy area's "Include units": a copy takes the units wholly inside the box as well.
	inline bool s_PasteAir = false; //!< Copy area's "Paste the air too": the air in the clipboard clears what is under the stamp.

	/// Whether the tool in hand is used by dragging out a shape on the world: the terrain brushes with Brush type Shape, "Make it fall"'s box,
	/// and Copy area's box (not while it is stamping).
	inline bool DragsShape(Tool kind) { return (IsTerrainBrush(kind) && s_ShapeFill) || kind == Tool::CollapseArea || (kind == Tool::CopyArea && !s_Stamping); }
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
	inline int s_VehicleChoice = 0;
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
	constexpr float c_EarthGravity = 20.0F; //!< The game's stock gravity (Scene GlobalAcceleration), m/s/s down, which the World tab's gravity calls Earth's.
	constexpr float c_MoonGravity = 0.165F; //!< The Moon's gravity, as a share of Earth's.
	inline Vector s_MapGravity; //!< The map's own gravity, as it was when the game started (the World tab's "Map's own" puts it back).
	inline bool s_MapGravityKnown = false; //!< Whether s_MapGravity has been taken for the game under way: taken on its first sandbox update.
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
		bool RemoveOnLeave = true; //!< Leaving play mode takes the character away (P makes a new one). Off, it stays where you left it.
		bool RespawnInstant = false; //!< When it dies with a respawn point set: back at the point at once. Off, the view stays where it died for a few seconds first.
	};
	inline PlayerSetup s_Player;

	/// The respawn point for your character: it is made again here, and stepped into, when it dies while you are playing it.
	inline std::vector<long> s_PlayerMade; //!< Every character made as yours so far (this game): any but the current one still about is taken away.
	inline bool s_RespawnSet = false;
	inline Vector s_RespawnPoint;
	constexpr float c_RespawnWaitSeconds = 4.0F; //!< How long the view stays where it died, when not respawning instantly.
	inline bool s_PlayerWasPlaying = false; //!< Your character was last seen being played: if it is then lost, that was a death.
	inline int s_PlayerGoneUpdates = 0; //!< Updates the character has been missing while it was being played (a moment's gap while it leaves a vehicle isn't a death).
	inline float s_RespawnWait = -1.0F; //!< Seconds left until it respawns; negative when not waiting.
	/// The joint strength and gib limits each part of your character had before it was made unbreakable, to put back when that is turned off.
	inline std::unordered_map<long, std::array<float, 3>> s_PlayerPartLimits;

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
		JetpackHere, //!< Fly to the point clicked, in the air, not on the ground under it, and hover there till the tank runs low (Actor::StandingOrder::SkyGoal).
		Select, //!< Clicks only pick units: one clicked (Shift adds, double click all of its kind in view, Ctrl+double click on the whole map), or none on a click on nothing.
		Command, //!< The order fits what's under the pointer (CommandTargetAt): with nothing selected, as Select; else a friend is selected, an enemy
		        //!< attacked, a battle objective gone for, open air flown to, anything else moved to. What the Command tool starts in, and what
		        //!< a one-off order's mode goes back to once it's placed (and Esc). (After Select, so the ring's slices keep their places.)
		Breach, //!< Dig through to the point, then attack (SandboxBreach.cpp): the diggers tunnel there, the rest follow them in, and once
		        //!< through they all go for the nearest enemy there. (After Command, so the ring's slices keep their places.)
		Build, //!< Build the piece picked (s_BuildPiece) at the point (SandboxBuild.cpp): the nearest selected unit with a Constructor walks
		       //!< there and puts it up. In Battle Command it costs reinforcement points.
		Airlift, //!< Fly the selected units to the point (SandboxAirlift.cpp): a gunship comes down by them, they get in, and it flies them
		         //!< there and lets them out. In Battle Command each ship costs reinforcement points.
		Pen, //!< Pen the selected animals (the livestock among them): each click a corner of the pen, or a box dragged out; the command row
		    //!< pens them (PenSelected). They live inside it from then on (AnimalBehaviors.lua). (After Command, so the ring's slices keep their places.)
		Dropoff, //!< The selected vehicles and flying ships go to the point and let their passengers out there, the driver or pilot staying at
		        //!< the controls and holding there for orders (DropoffSelected). (After Pen, so the ring's slices keep their places.)
		Land, //!< The selected flying ships fly to the point, set down on the ground there and turn their engines off (a handbrake), and stay
		      //!< down till given another order (LandSelected). (After Drop off, so the ring's slices keep their places.)
		Park, //!< The selected ground vehicles drive to the point and put their handbrakes on there, everyone staying aboard, till given another
		      //!< order (ParkSelected); ships among them land. (After Land, so the ring's slices keep their places.)
		Ram //!< The selected flying ships ram what was clicked (anything not their side's) or the place itself, as a suppress is given: run after
		    //!< run, striking whatever is there, till it is gone or they are given another order (RamSelected);
		    //!< anyone else selected attacks it or goes there. (Last, so the ring's slices keep their places.)
	};
	inline CommandMode s_CommandMode = CommandMode::Command;
	inline bool s_SpawnOrderArmed = false; //!< The next order placed sets the spawn order of your team (or of s_SpawnOrderZone) instead (TakeSpawnOrder).
	inline double s_SpawnOrderDoneAt = -1.0; //!< When (ImGui time) setting it is over, once placed: after a second click of a double click can follow.
	inline int s_SpawnOrderZone = -1; //!< Which of your team's spawn zones it is for, -1 for the team's own.
	constexpr const char* c_CommandModeNames[] = {"Move", "Attack", "Guard", "Attack-move", "Defend at", "Patrol", "Dig to", "Suppress", "Jetpack here", "Select units", "Command", "Breach", "Build", "Airlift", "Pen animals", "Drop off", "Land", "Park", "Ram"};
	constexpr ImU32 c_CommandModeColors[] = {IM_COL32(110, 180, 250, 255), IM_COL32(239, 106, 91, 255), IM_COL32(120, 220, 120, 255), IM_COL32(245, 150, 70, 255), IM_COL32(242, 182, 61, 255), IM_COL32(120, 200, 220, 255), IM_COL32(214, 160, 90, 255), IM_COL32(205, 120, 235, 255), IM_COL32(140, 230, 255, 255), IM_COL32(230, 230, 230, 255), IM_COL32(255, 214, 120, 255), IM_COL32(235, 110, 55, 255), IM_COL32(120, 210, 190, 255), IM_COL32(170, 190, 255, 255), IM_COL32(205, 175, 115, 255), IM_COL32(150, 225, 140, 255), IM_COL32(190, 170, 140, 255), IM_COL32(200, 200, 120, 255), IM_COL32(255, 95, 150, 255)};
	/// A mode whose order is given once and then goes back to Command (s_BackToCommandAt): not Select, Command, Patrol or Pen (clicked out point by point).
	inline bool OneOffMode(CommandMode mode) { return mode != CommandMode::Select && mode != CommandMode::Command && mode != CommandMode::Patrol && mode != CommandMode::Pen; }
	inline double s_BackToCommandAt = -1.0; //!< When (ImGui time) the one-off mode in hand goes back to Command: once a double click can no longer follow.
	inline CommandMode s_BackToCommandFrom = CommandMode::Command; //!< The one-off mode that was in hand then: only that goes back.
	/// What a click in Command mode comes to at a point (CommandTargetAt).
	struct CommandTarget {
		enum Kind {
			Select, //!< Nothing selected, so the click selects (as Select mode).
			Move, //!< As Move mode: a friend under it is selected, an enemy attacked, else moved to.
			Objective, //!< A battle objective of the mode on now: gone for, or held if the selection's team defends it.
			Air, //!< Open air, nowhere near the ground: flown to.
			GetIn //!< A friendly vehicle or flying ship with a free seat, with units on foot selected that can sit: they get in.
		} What = Select;
		Actor* Vehicle = nullptr; //!< With GetIn: the vehicle. Not owned.
		int Boarders = 0; //!< With GetIn: how many of the selection will get in (as many as can, up to its free seats).
		BattleObjective Goal; //!< With Objective: which.
		bool Defend = false; //!< With Objective: the selection's team holds it rather than goes for it.
	};

	/// What a dig-to to the point under the cursor would come to for the selected units (RC-11; DigToPreview).
	struct DigPreview {
		DigPlan Plan; //!< The lead digger's plan: the first unit that can dig there, else the first unit.
		float LeadStrength = 0.0F; //!< What that unit's digger cuts.
		int Units = 0; //!< How many units were checked.
		int CanDig = 0; //!< How many of them can dig there.
	};
	inline std::vector<Vector> s_PatrolDraft; //!< The points of the patrol route being clicked out (RC-4), in order.
	inline std::vector<Vector> s_PenDraft; //!< The corners of the animal pen being clicked out, in order.
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
		float Direct = 0.0F; //!< How urgent the order was (s_DirectOrder): over 0 drawn with a second, wider ring, wider the more direct; under 0 (cautious) with a small inner one.
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
	inline int s_ColonyStaff = 2; //!< How many staff a new woodcutter keeps.
	inline int s_ColonyRemoveID = -1; //!< The colony building the Colony tab was asked to close down this frame, or -1.
	inline int s_BuildingPanelID = -1; //!< The colony building whose panel is open beside it, or -1.
	inline int s_RallyPickID = -1; //!< The barracks whose rally point the next click in the world sets, or -1.
	inline std::string s_BuildingPanelNote; //!< What the panel last had to say (why an upgrade couldn't be done).
	inline Colony::Style s_ColonyStyle = Colony::Style::Fantasy; //!< How new colony buildings look.
	inline bool s_ColonyLite = false; //!< Whether new colony buildings are lite: just a box with their brain in, put where clicked, to build your own building round.
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
		Wait, //!< Stay a while where it is (a patrol's pause at each point, RC-4).
		Suppress, //!< Fire into a place from where the step before leaves it; never done, so it ends a plan, as a defend does.
		Dropoff, //!< A vehicle or flying ship goes to the place and lets its passengers out there (StartDropoff); done once they are out.
		Land, //!< A flying ship sets down at the place and turns its engines off (StartDropoff, staying down); done once it is down.
		Park //!< A ground vehicle drives to the place and puts its handbrake on (StartDropoff, staying put); done once it is there.
	};

	/// One step of a unit's plan: a shift-clicked order to carry out after the ones before it.
	struct PlanStep {
		PlanKind Kind = PlanKind::Move;
		Vector Place; //!< Where: the unit's own spot for a move, the post for a defend (where the step before leaves it), the target's place when queued otherwise.
		UnitRef Target; //!< The enemy to attack or the friend to guard.
		int Facing = 0; //!< A defend's way to face: -1 left, 1 right, 0 either (RC-4).
		int Updates = 0; //!< A wait's length, in sim updates.
		int Radius = 0; //!< A suppress's spread round its place, px (the Suppress radius when it was queued).
		bool Sky = false; //!< A Fly step (Shift with Fly here): Place is the point in the air, flown to by those with a jetpack, the rest going to the ground under it.
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

	/// A vehicle or flying ship on its way to let its passengers out (Dropoff).
	struct Dropoff {
		UnitRef Unit;
		Vector Place; //!< Where they get out.
		int Stage = 0; //!< 0 on the way; 1 (a ship) coming down to hover low for them, or to set down on a Land order.
		long long Since = 0; //!< The sim update it got to that stage.
		bool StayDown = false; //!< A Land or Park order: a ship stays down with its engines off, a vehicle with its handbrake on, everyone aboard, rather than letting them out (which a ship does hovering low, without setting down).
		Vector Hover; //!< Where a ship was sent to hover or set down for it, once on its way there (Stage 1).
	};
	inline std::map<long, Dropoff> s_Dropoffs; //!< Drop-offs under way, by the vehicle's unique ID. Any other order given it drops its own (DropPlan).
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

	/// A saved game's units' unique IDs, from those they had when saved to those they have now it is loaded (ActivityMan::RemapLoadedUniqueIDs):
	/// what the sandbox kept about units by ID (their plans, drop-offs, guard posts...) is put back on the same units.
	struct LoadedIDs {
		const std::unordered_map<long, long>& Map;

		/// A unit's new unique ID, from its old one: 0 if it wasn't saved (or none was given).
		long operator()(long oldID) const {
			auto found = oldID != 0 ? Map.find(oldID) : Map.end();
			return found != Map.end() ? found->second : 0;
		}

		/// The unit with that old unique ID, in the loaded game, or null.
		Actor* Unit(long oldID) const {
			const long newID = (*this)(oldID);
			return newID != 0 ? dynamic_cast<Actor*>(g_MovableMan.FindObjectByUniqueID(newID)) : nullptr;
		}

		/// A reference to the unit with that old unique ID, empty if it isn't in the loaded game.
		UnitRef Ref(long oldID) const { return MakeRef(Unit(oldID)); }
	};

	/// Writes a list of units by their unique IDs, for a saved game's sandbox state.
	inline void SaveRefs(std::ostream& out, const std::vector<UnitRef>& refs) {
		out << ' ' << refs.size();
		for (const UnitRef& ref: refs) {
			out << ' ' << ref.ID;
		}
	}

	/// Reads a list written by SaveRefs, onto the loaded game's units: those not in it are left out.
	inline std::vector<UnitRef> LoadRefs(std::istream& in, const LoadedIDs& ids) {
		std::vector<UnitRef> refs;
		size_t count = 0;
		in >> count;
		for (size_t i = 0; i < count && in; ++i) {
			long id = 0;
			in >> id;
			if (UnitRef ref = ids.Ref(id); ref.Unit) {
				refs.push_back(ref);
			}
		}
		return refs;
	}

	/// Writes the orders under way that the sandbox keeps itself, not on the units (plans and patrols, drop-offs, guard posts, flying ships'
	/// team orders, units told to defend a place), for a saved game (Sandbox::GetSaveState). Times as how long ago, by sim updates.
	void SaveOrders(std::ostream& out);
	/// Reads them back, made by SaveOrders, onto the loaded game's units; anything about a unit that isn't in it is dropped.
	void LoadOrders(std::istream& in, const LoadedIDs& ids);
	/// Writes your squads in the world (the Squad Builder's: who is in each and who leads) for a saved game.
	void SavePlayerSquads(std::ostream& out);
	/// Reads them back, made by SavePlayerSquads.
	void LoadPlayerSquads(std::istream& in, const LoadedIDs& ids);
	/// Writes your character in the Sandbox game mode (which unit it is, the vehicle it sits in, its respawn point) for a saved game, and reads
	/// it back: the loaded game's character is yours again, rather than a stray unit beside a new one made with P.
	void SavePlayer(std::ostream& out);
	void LoadPlayer(std::istream& in, const LoadedIDs& ids);
	/// Writes the airlifts under way for a saved game, and reads them back.
	void SaveAirlifts(std::ostream& out);
	void LoadAirlifts(std::istream& in, const LoadedIDs& ids);
	/// Writes the builds under way (engineers' sites) for a saved game, and reads them back.
	void SaveBuilds(std::ostream& out);
	void LoadBuilds(std::istream& in, const LoadedIDs& ids);
	/// Writes the breaches under way for a saved game, and reads them back.
	void SaveBreaches(std::ostream& out);
	void LoadBreaches(std::istream& in, const LoadedIDs& ids);
	/// Writes the spawn points (Tool::SpawnPoint) with the units each has made, for a saved game, and reads them back.
	void SaveSpawnPoints(std::ostream& out);
	void LoadSpawnPoints(std::istream& in, const LoadedIDs& ids);

	/// Writes a sim update as how long before now it was (below 0 for one still to come), for a saved game's sandbox state; "-" for the value
	/// that stands for none (as -1 for "not yet", or 0 for "no time limit"), so it means none again once loaded.
	inline void SaveUpdate(std::ostream& out, long long at, long long none = -1) {
		if (at == none) {
			out << " -";
		} else {
			out << ' ' << g_TimerMan.GetSimUpdateCount() - at;
		}
	}

	/// Reads a sim update written by SaveUpdate, onto the loaded game's clock: none for "-".
	inline long long LoadUpdate(std::istream& in, long long none = -1) {
		std::string word;
		in >> word;
		if (word.empty() || word == "-") {
			return none;
		}
		return g_TimerMan.GetSimUpdateCount() - std::atoll(word.c_str());
	}

	/// Writes text (a name, a note) as one word, with nothing a saved game's line would end at: spaces, tabs, line ends and "//" as %XX, and
	/// nothing at all as "-".
	inline void SaveText(std::ostream& out, const std::string& text) {
		std::string word;
		for (unsigned char letter: text) {
			if (letter <= ' ' || letter == '%' || letter == '/' || (text == "-" && letter == '-')) {
				char code[4];
				std::snprintf(code, sizeof(code), "%%%02X", letter);
				word += code;
			} else {
				word += static_cast<char>(letter);
			}
		}
		out << ' ' << (word.empty() ? "-" : word);
	}

	/// Reads text written by SaveText.
	inline std::string LoadText(std::istream& in) {
		std::string word;
		in >> word;
		std::string text;
		for (size_t at = 0; word != "-" && at < word.size(); ++at) {
			if (word[at] == '%' && at + 2 < word.size()) {
				text += static_cast<char>(std::strtol(word.substr(at + 1, 2).c_str(), nullptr, 16));
				at += 2;
			} else {
				text += word[at];
			}
		}
		return text;
	}

	/// Writes a Battle Director defender (one a player's order told to defend, or a battle's own) under its unit's unique ID, for a saved game.
	void SaveDefender(std::ostream& out, long id, const BattleDefender& defender);
	/// Reads one written by SaveDefender onto the loaded game's units. Its unit's new unique ID, 0 if it isn't in the loaded game.
	long LoadDefender(std::istream& in, const LoadedIDs& ids, BattleDefender& defender);

	/// Writes the battle under way for a saved game (SandboxBattle.cpp): the Battle tab's setup and the sim's (the mode, which teams you
	/// command, the cards, zones and points), the mode's game (its score, clocks, flags, VIPs, waves...), the teams' waves and ships, the
	/// battle's own defenders, rally points, and Battle Command's points, powers, enemy commanders, field brain, army, veterans and pickups.
	void SaveBattle(std::ostream& out);
	/// Reads it back, made by SaveBattle, so the battle goes on where it was rather than starting over: anything about a unit that isn't in
	/// the loaded game is dropped. If what is read doesn't add up, the battle is forgotten, as for a save without one.
	void LoadBattle(std::istream& in, const LoadedIDs& ids);
	/// A saved game's battle was put back (LoadBattle) as the game started: the first frame's set-up keeps the rally points, and the battle
	/// panel shut while its game is on, rather than opening as for a new game. Cleared by each game's start (Sandbox::OnActivityStarted).
	inline bool s_BattleLoaded = false;

	// The parts of the battle each kept in its own file, written and read in turn by SaveBattle and LoadBattle.
	/// The Battle tab's cards and mode panel, the mode's settings in the sim and each team's (SandboxBattlePresets.cpp, as setups are saved),
	/// and the linked battles' progress. The setup saved to load with the map isn't put in over them. False if they couldn't be read.
	void SaveBattleSetups(std::ostream& out);
	bool LoadBattleSetups(std::istream& in);
	/// The mode's game (SandboxBattleModes.cpp): on or over, its score and clocks, and each mode's own: flags and their carriers, hills,
	/// objectives, waves, VIPs, the ship; the runners, commanders, AI squads, rushers and units watched for being stuck.
	void SaveBattleModes(std::ostream& out);
	void LoadBattleModes(std::istream& in, const LoadedIDs& ids);
	/// Battle Command's reinforcement points, and the scores they were last earned from (Reinforcements.cpp).
	void SaveReinforcements(std::ostream& out);
	void LoadReinforcements(std::istream& in);
	/// The commander powers' cooldowns, and the Scan power's lifting of the fog (CommanderPowers.cpp).
	void SavePowers(std::ostream& out);
	void LoadPowers(std::istream& in);
	/// The enemy commanders' strikes coming in, and when they next look to buy and to use a power (BattleEnemy.cpp).
	void SaveEnemyCommander(std::ostream& out);
	void LoadEnemyCommander(std::istream& in);
	/// Your brain on the field, once put down (BattleFieldBrain.cpp).
	void SaveFieldBrain(std::ostream& out);
	void LoadFieldBrain(std::istream& in, const LoadedIDs& ids);
	/// What's left of each side's picked army (BattleArmy.cpp).
	void SaveArmy(std::ostream& out);
	void LoadArmy(std::istream& in);
	/// The units' kits noted for re-arming (BattleSupply.cpp).
	void SaveKits(std::ostream& out);
	void LoadKits(std::istream& in, const LoadedIDs& ids);
	/// The veterans: each unit's name, kills, time in and rank (Veterancy.cpp).
	void SaveVeterans(std::ostream& out);
	void LoadVeterans(std::istream& in, const LoadedIDs& ids);
	/// The pickups lying on the map, the units sent to fetch them, and when the next comes in (BattlePickups.cpp).
	void SaveBattlePickups(std::ostream& out);
	void LoadBattlePickups(std::istream& in, const LoadedIDs& ids);
	/// The new units waiting for their spawn orders (SpawnOrders.cpp).
	void SaveSpawnOrderUnits(std::ostream& out);
	void LoadSpawnOrderUnits(std::istream& in, const LoadedIDs& ids);
	/// Whose turn it is in a hot-seat game (BattleHotSeat.cpp): the loaded game hands the seat to that player.
	void SaveHotSeat(std::ostream& out);
	void LoadHotSeat(std::istream& in);

	/// Commander mode (RC-9): your side's units commanded from above in an ordinary game.
	inline bool s_Commander = false;
	inline int s_CommanderTeam = 0; //!< The side you command: your own in the game.
	inline UnitRef s_CommanderReturnTo; //!< The unit you were playing, to go back into.

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

	/// The sandbox's undo, one history for painting and placing (Ctrl+Z takes back the newest step, whichever it was). A paint step is what
	/// one stroke of a paint or build tool changed (a drag of the brush is one step: changes less than a quarter second apart run together),
	/// pixel by pixel as it was before, the first change to each pixel only. A placing step is one click of the Spawn or Build tools: the
	/// units, craft, items, doors and colony buildings it made (taken away again), and the ground a bunker piece or building drew over (put
	/// back). A paste (S-4) is a step of both: the ground it changed, and the units it put down. The selection's Remove and Duplicate (S-8)
	/// are a step each: the units removed (put back) or the copies made (taken away). The last 20 steps are kept, up to c_PaintUndoPixels
	/// pixels in all (the oldest go first), and a new game forgets them. A stroke longer than a step holds goes on in a new step, so each
	/// Ctrl+Z takes back part of it rather than the rest being lost.
	/// What only lands over the next moments (poured liquids and loose ground, boulders, the Boom tab's blasts) is kept as a box of the
	/// ground round it, taken before it lands (RecordPaintBox): the undo puts the whole box back. A Remove click keeps a copy of what it
	/// took, which the undo puts back; a spring step, the springs it put down or took away.
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
		std::vector<std::unique_ptr<MovableObject>> Removed; //!< Copies of what the step took out of the world (a Remove click; for the redo, what an undo took away), put back as they were.
		std::vector<WaterSpawner> SpringsAdded; //!< Springs the step put down, taken away again.
		std::vector<WaterSpawner> SpringsRemoved; //!< Springs the step took away, put back.
		int ColonyBuilding = -1; //!< The colony building a placing step built, or -1.
		bool Sealed = false; //!< A placing step: the next stroke starts a step of its own, however soon it comes.
		int Drop = 0; //!< A "Make it fall" step: the drop TerrainCollapse::DropArea gave, taken back with TerrainCollapse::TakeBackDrop.
		int Rope = 0; //!< A rope put down (RopeSim), taken away again.
		long long PouredAt = -1; //!< The sim update a pour or boulder of the step was last let go in: what it lets go lands at the next liquid or collapse update, and the undo waits for that.
		bool HoldZones = false; //!< A "Hold ground" step: the hold zones put back to HoldZonesBefore.
		std::vector<std::vector<Vector>> HoldZonesBefore; //!< The hold zones before the step.

		bool Empty() const { return Pixels.empty() && Placed.empty() && Removed.empty() && SpringsAdded.empty() && SpringsRemoved.empty() && ColonyBuilding < 0 && Drop == 0 && Rope == 0 && !HoldZones; }
	};

	inline std::deque<PaintUndoStep> s_PaintUndo;

	/// The redo (Ctrl+Y, Ctrl+Shift+Z): the steps the undo took back, newest last, each holding what the undo changed so it can be changed
	/// back. A redo puts its step back on the undo history. Anything new done with a tool forgets them, as does a new game. A step with a
	/// colony building, a "Make it fall" drop or a rope in it can't be made again: undoing one forgets the redo.
	inline std::deque<PaintUndoStep> s_PaintRedo;

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

	enum class Icon { Eye, Arrows, Target, Person, Cross, Flag, Jar, Gun, Wall, Down, Flame, Drop, Cloud, Grains, Chunk, Pick, Bomb, Rocket, Bolt, Star, Plant, Candle, Rope, Wheels, Copy };

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
	    // Wheels
	    "............"
	    "............"
	    "....####...."
	    "...#+..#...."
	    ".##########."
	    "#++#######+#"
	    "############"
	    ".##########."
	    "..##....##.."
	    ".#++#..#++#."
	    ".####..####."
	    "..##....##..",
	    // Copy
	    "#######....."
	    "#+.....#...."
	    "#.......#..."
	    "#....#######"
	    "#....#+....#"
	    "#....#.....#"
	    "#....#.....#"
	    "######.....#"
	    ".....#.....#"
	    ".....#.....#"
	    ".....#.....#"
	    ".....#######",
	};

	struct ToolLook {
		Icon Art;
		ImU32 Color;
	};

	inline std::string s_WantedTab; //!< The tab of the sandbox window to bring to the front, asked for from the bar.

	inline std::string s_CurrentTab; //!< The tab of the sandbox window that is showing.

	inline std::map<std::string, int> s_LastToolOfTab; //!< The tool last picked on each tab, so coming back to the tab from the bar picks it up again.

	inline std::map<std::string, int> s_LastToolOfGroup; //!< The tool last taken from each group of the bar's shelf, taken again when the group is.

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

	/// What was last used with the tools (S-6), newest first, shown as a Recent row over its list: the units, brains, items, vehicles and
	/// bunker pieces put down (each list its own), the Paint tab's liquids, loose ground and terrain, and the effects. Up to
	/// c_RecentPerList of each list. As the pins, a tool and what it makes (a preset, a material or an effect by name; "" for a material
	/// tool's own), the player's whatever game is played, kept in their own file the moment they change.
	inline std::vector<Pin> s_Recent;
	constexpr size_t c_RecentPerList = 8;
	constexpr const char* c_RecentFile = "Userdata/SandboxRecent.txt";

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
	/// How many of a unit one click of the Unit tool puts down: its preset's SpawnGroup number value (a swarm creature, CR-1: giant rats
	/// by the eight), 1 for any other.
	int SpawnGroupSize(const Preset& preset);
	/// Whether a unit from the Units list can sit in a vehicle's seat: a humanoid, and not a brain.
	bool CanCrew(const Preset* unit);
	/// The text a unit is kept by as a vehicle's crew pick (VehicleTuning::Driver and Crew): its module and preset name.
	std::string CrewPickKey(const Preset& unit);
	/// Works out a vehicle's crew pick (VehicleTuning::Driver or Crew) as the Spawn tab stands: the unit picked in the Units list (or random, as it
	/// says), random units, or the one named, by its index in the Units list. One no longer loaded is taken as the Units list pick.
	void ResolveCrewPick(const std::string& pick, int& choice, bool& random);
	/// A vehicle's crew pick as words, for the Spawn tab: "Units list pick", the random units or the unit's name.
	std::string CrewPickName(const std::string& pick);
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
	void TemperamentCombo(const char* label, int* temperament = nullptr); //!< @param temperament What it sets; the Spawn tab's (s_SpawnTemperament) if null.
	bool IsSelectable(const Actor* actor);
	MovableObject* ObjectUnder(const Vector& position, bool actorsOnly);
	void SendUnit(Actor* unit, const Vector& waypoint, Actor* target, bool attack, const char* reason, bool lock = false, bool resend = false);
	void HoldUnit(Actor* unit);
	void ApplyPendingOrders();
	void GiveOrder(Actor* actor, Order order);
	void AnswerOrder(Actor* unit, const char* trigger);
	const char* OrderTrigger(Order order);
	void ReturnDefenders();
	/// Flying ships (ACShip) on one of their side's orders (GiveOrder): attack and hunt send each after the nearest enemy (a brain first
	/// for hunt), again whenever the one it was after is dead; patrol flies it to and fro over where it was told, going after enemies that
	/// come near. Ships have no AI script to do this for them, as a unit on foot's does. Twice a second.
	void UpdateShipTeamOrders();
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
	void RedoPaint();
	/// Keeps every pixel of a box of the terrain as it is, for the undo of what lands there over the next moments (see s_PaintUndo).
	/// Scene pixels, inclusive; wrapped round a scene that wraps, and cut to the scene.
	void RecordPaintBox(int left, int top, int right, int bottom);
	void NotePlaced(const MovableObject* object);
	/// Does one of the command strip's actions (SelectionDo) to the selected units: Tool::SelectionAction applied.
	void DoToSelection(const Stroke& stroke);
	/// Queues one of the command strip's actions on the selected units (SelectionDo), for the sim to do (DoToSelection).
	/// @param team The side, for SelectionDo::ChangeSide.
	void QueueSelectionAction(SelectionDo what, int team = 0);
	inline int s_DeleteKeyTakenFrame = -1; //!< The ImGui frame Delete last removed something under the pointer (a battle zone, an effect, a spring): not the selection then.
	/// Copy area's copy (Ctrl+C, or its Copy button): the marked box (s_CopyBox) to the clipboard, with its units if "Include units" is
	/// ticked, for the sim to do (Tool::CopyTake).
	void CopyMarkedBox();
	/// Copy area's paste (Ctrl+V, or its Paste button): the Copy area tool taken up with the clipboard as a stamp on the pointer.
	void TakeUpStamp();
	/// Copy area's stamp and box, drawn on the world while the tool is in hand (see DrawCursor).
	void DrawCopyArea();
	/// Where the stamp's top left corner is for the pointer (held by its middle), in the scene: where a click puts the clipboard down.
	Vector StampCorner();
	/// Lets the stamp's preview go (ForgetPictures, and a new copy).
	void ForgetStampPicture();
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
	/// A unit not yet in the world set up to follow an order once it is (CreateUnit's orders): what a craft's passengers are given.
	void ReadyOrder(Actor* actor, int team, Order order);
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
	/// Whether the tool in hand puts down the Build piece picked by dragging a line: a lift or moving platform (an AMover), dragged from where
	/// its deck is first stood on to where it runs to.
	bool DragsTrack(Tool kind);
	/// How wide the deck of a lift or moving platform piece is, in pixels (0 for a piece that isn't one), for the drag's preview.
	float TrackDeckWidth(const Preset& preset);
	/// How many stops a lift or moving platform piece has (spread evenly along its dragged track), at least 2.
	int TrackStopCount(const Preset& preset);
	/// A point of a line dragged on the world, on the bunker grid when snapping: across to the nearest 12 px, up and down to the nearest
	/// 24 px (the floors bunker pieces line up on).
	Vector SnapLinePoint(const Vector& point, bool snap);
	/// Which ways a line dragged with Shift may go: any of the 8, or never straight up and down (Across) or straight across (UpDown).
	enum class LineLock { Nearer, Across, UpDown };
	/// Where a line dragged from a start toward the pointer ends: any way, or with Shift (eightWays) the nearest of the 8 ways, every 45
	/// degrees, that lock allows; on the bunker grid when snapping (a slanted line kept at 45); no longer than c_MaxLineDrag.
	Vector LineDragEnd(const Vector& start, const Vector& pointer, bool eightWays, bool snap, LineLock lock = LineLock::Nearer);
	constexpr float c_MaxLineDrag = 2400.0F; //!< The longest line a drag puts down (a lift's track), in pixels.
	constexpr float c_MinLineDrag = 24.0F; //!< A drag shorter than this is a click.
	/// One piece of a bunker run (see LayOutRun): which piece, and where along the run it starts and how long it is, in pixels.
	struct RunTile {
		const Preset* Piece = nullptr;
		float Start = 0.0F;
		float Length = 0.0F;
	};
	/// A run of bunker pieces dragged out from one end to the other: a tunnel, a wall, a floor, a ladder... laid end to end, its first piece where a
	/// click at the start would have put it. Dragged at an angle (Shift), it is slanted by shearing: each column of a run across (each row of one
	/// up and down) moved along by Slope pixels for every pixel along, so the pixels stay upright, as the game's own sloped tunnels are drawn.
	struct RunLayout {
		std::vector<RunTile> Tiles; //!< From the left end of a run across, the top end of one up and down.
		bool UpDown = false; //!< Laid up and down, not across.
		Vector Corner; //!< The top left corner of the first tile, before it is slanted.
		float Slope = 0.0F; //!< The slant: how far across (up and down) the run moves for every pixel it goes along, 0 for straight.
		float Pivot = 0.0F; //!< How far along the run its slant is measured from: the middle of its first tile, where the drag started.
		float Thickness = 0.0F; //!< How thick the run is, its thickest tile across the way it runs.
		float Length() const { return Tiles.empty() ? 0.0F : Tiles.back().Start + Tiles.back().Length; }
		/// How far the run is moved across the way it runs at a place along it.
		float Offset(float along) const { return Slope * (along - Pivot); }
		/// Where a point of the run is in the scene, from how far along it is and how far across from its unslanted edge.
		Vector At(float along, float across) const { return UpDown ? Corner + Vector(across + Offset(along), along) : Corner + Vector(along, across + Offset(along)); }
	};
	/// Whether the Build piece picked can be dragged out into a run of itself (a tunnel, a wall, a floor, a ladder...).
	bool DragsRun(Tool kind);
	/// Whether the tool in hand is used by dragging a line on the world: a lift's track or a bunker run.
	inline bool DragsLine(Tool kind) { return DragsTrack(kind) || DragsRun(kind); }
	/// Which way a bunker run's drag is kept straight without Shift: the way its piece runs, or the nearer way for a piece with a partner that
	/// runs the other way (a tunnel's is the shaft).
	LineLock RunLock(const Preset& preset);
	/// The run a drag from start to end makes with a piece: whole pieces, as many as fit the drag, the piece's partner if dragged more the other
	/// way, its end pieces at the ends (a bridge's), and slanted to the end when it doesn't run straight (only for pieces that can be).
	RunLayout LayOutRun(const Preset& preset, const Vector& start, const Vector& end, bool snap);
	void TakeControl(const Vector& position);
	void StopFlying();
	/// Lets go of the unit you control and goes back above. @param leavingPlay Whether this is leaving play mode (not stepping from one unit into your character),
	/// which takes your character away if "Remove it when I leave play" is ticked.
	void ReleaseControl(bool leavingPlay = true);
	/// Out of play mode with "Remove it when I leave play" ticked, takes away your character if one is left, in the world, a vehicle or a craft.
	void RemovePlayerOutOfPlay();
	const Preset* FindPreset(const std::vector<Preset>& list, const std::string& presetName);
	Vector OpenAirAt(Vector place);
	Vector StandingPlaceBelow(Vector place);
	Actor* MakePlayer(const Vector& place);
	Actor* PlayerHolder();
	Actor* TakeOutPlayer();
	void RemovePlayer();
	void ClearRespawn();
	void RemoveStrayPlayers();
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
	void ForgetNoRoutes(const Actor* unit);
	int OrderKindFor(const char* reason);
	std::vector<DigPlan> DigPlansFor(const std::vector<Actor*>& units, const Vector& point);
	int DigFailReason(const DigPlan& plan);
	void DigUnitsTo(const std::vector<Actor*>& units, const Vector& point);
	const DigPreview& DigToPreview(const std::vector<Actor*>& units, const Vector& point);
	std::string DigVerdict(const DigPreview& preview);
	void UpdateMoveWatch();
	void ReissueNoRoute(const Vector& destination);
	void MapOrder(const Vector& point, bool shift);
	/// What a Command mode click at a point would do for the selected units (CommandTarget), as the pointer shows and the click gives.
	CommandTarget CommandTargetAt(const Vector& point);
	/// A click of the command tool's has placed an order in the mode in hand: a one-off mode goes back to Command (OneOffMode), once a second click
	/// of a double click can no longer follow.
	void BackToCommandAfterOrder(bool shift);
	/// Sends the selected units for a battle objective (Command mode): into a zone, or round a place their team holds, as a defend zone the
	/// size of the objective; to a place they go for (an enemy flag), as an attack-move. With shift, steps of their plans.
	void ObjectiveSelected(const CommandTarget& target, bool shift);
	/// Sends the selected units to a point in open air (Command mode), as Jetpack here; with Shift, a plan step to the ground under it.
	void FlySelectedTo(const Vector& point, bool shift);
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
	void SuppressSelected(const Vector& point, bool shift = false);
	void SuppressUnit(Actor* unit, const Vector& point, int radius);
	bool SuppressZoneOf(const Actor* unit, Vector& centre, float& radius);
	void ClearSuppress(Actor* unit);
	void PatrolSelected(const std::vector<Vector>& points, bool backAndForth);

	/// Whether a unit is an animal (livestock), which takes the animal orders: roam free, stay, penned (AnimalBehaviors.lua).
	bool IsAnimal(const Actor* actor);

	/// Pens the selected animals in a polygon (or box) of points, at least three: they roam, graze, drink and rest inside it from then on.
	void PenSelected(const std::vector<Vector>& points);

	/// Gives the selected animals an animal order that needs no place: 0 roam free (round where they are), 2 stay where they are. Lets them out of any pen.
	void AnimalOrderSelected(int order);

	/// Gets the animal order the selected animals have: 0 roam free, 1 penned, 2 stay; -1 when they differ, -2 when none of them is an animal.
	int SelectedAnimalOrder();

	/// Gets an animal's pen, if it is penned: its corners, in order.
	bool PenOf(const Actor* animal, std::vector<Vector>& corners);
	void DropPlan(const Actor* unit);
	void UpdatePlans();
	void DropPlanStep(long unitID, int step);
	Actor* EnemyNear(const Vector& point, int team);
	void OrderSelectedUnits(int choice, const Vector& point);
	/// How many seats of a vehicle (AVehicle) or flying ship (ACShip) are free; 0 for anything else, or one that is dead.
	int FreeSeatsOf(const Actor* vehicle);
	/// Whether a unit could get into a vehicle or flying ship when it reaches it: a humanoid of its side not under a player's control.
	bool CanBoard(const Actor* vehicle, const Actor* unit);
	/// Sends the units that can sit to get into a vehicle or flying ship, the nearest first, as many as it has seats free; the others that
	/// could have stand guard by it (Command mode's Get in).
	void GetInVehicle(const std::vector<Actor*>& units, Actor* vehicle);
	/// Lets everyone out of a vehicle or flying ship: a squad, the rest following whoever got out first (the driver). A vehicle is no longer
	/// commanded as one unit (AVehicle::IsCommanded). With keepDriver, only the passengers get out, a squad behind the first of them, and
	/// the driver or pilot stays at the controls, still under orders. @return Those who got out.
	std::vector<Actor*> EveryoneOut(Actor* vehicle, bool keepDriver = false);
	/// The units of a list that aren't vehicles or flying ships: for orders only those on foot can carry out (breach, build).
	std::vector<Actor*> OnFoot(std::vector<Actor*> units);
	/// When a commanded vehicle (AVehicle::IsCommanded) has given up on the way it was sent (VC-2): unless it was sent to attack or its crew
	/// was told to stay in, its crew get out and carry on on foot (to defend the place, for a defend), and the vehicle holds.
	/// @param destination Where the order sent it. @return Whether its crew went on on foot.
	bool CrewGoOnFoot(Actor* unit, const Vector& destination);
	/// Whether the selected vehicles' crews stay in: 1 all do, 0 none do, -1 mixed, -2 no vehicle selected.
	int SelectedStayIn();

	/// Whether the selected vehicles and flying ships are locked (Actor::IsVehicleLocked): 1 all, 0 none, -1 some, -2 there are none.
	int SelectedLocked();
	/// Whether a vehicle or flying ship is among the selected units.
	bool SelectionHasVehicle();
	/// Whether everyone selected (and alive) is a vehicle or flying ship: then the command bar shows only the orders they can carry out.
	bool SelectionOnlyVehicles();
	/// Whether vehicles and flying ships can carry out an order mode: no digging, jetpacks, breaching, building, airlifts or pens.
	bool ModeFitsVehicles(CommandMode mode);
	/// The selected vehicles and flying ships go to the point and let their passengers out there (Shift: as a step of their plans).
	void DropoffSelected(const Vector& point, bool shift);
	/// Sends one vehicle or flying ship to let its passengers out at a place: it drives or flies there by the usual route and risk; there,
	/// a ship comes down near the ground; its passengers get out as a squad and hold there, and it holds there for orders (a ship back up at
	/// its hover height). Nobody to let out: just the move.
	/// @param stayDown A Land or Park order: whether or not it carries anyone, a ship sets down there and turns its engines off, a vehicle puts its
	/// handbrake on, and it stays there, nobody getting out.
	void StartDropoff(Actor* vehicle, const Vector& place, bool stayDown = false);
	/// The selected flying ships set down at the point and turn their engines off, staying down (Shift: as a step of their plans).
	/// Anyone else selected goes there.
	void LandSelected(const Vector& point, bool shift);
	/// The selected ground vehicles drive to the point and put their handbrakes on (ships among them land), staying there (Shift: as a step of
	/// their plans). Anyone else selected goes there.
	void ParkSelected(const Vector& point, bool shift);
	/// The Ram order: the selected flying ships ram what was clicked (ACShip::RamTarget), or with nothing clicked the place (ACShip::RamPlace);
	/// anyone else selected attacks it, or goes there.
	/// @param target What was clicked, not of their side, or nullptr. @param point The place clicked.
	void RamSelected(Actor* target, const Vector& point);
	/// Whether a ground vehicle (not a flying ship) is among those selected.
	bool SelectionHasCar();
	/// Whether a flying ship is among those selected.
	bool SelectionHasShip();
	/// Moves the drop-offs under way along. Once a sim update.
	void UpdateDropoffs();
	/// Where a flying ship's drop-off ring is centred for a drop-off at a place: the ground under it.
	Vector DropRingCentre(const Vector& place);
	/// Whether a flying ship is inside the drop-off ring ("Ship drop-off ring", as for AI ship rides) round a drop-off at a place, and low
	/// enough there for its passengers to jump out without a long fall (about a hull and a half off the ground).
	bool ReadyToDropOff(ACShip* ship, const Vector& place);
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

	/// One of the Battle tab's setups saved with a map (SandboxBattlePresets.cpp), as kept in Userdata/BattlePresets/<map>.txt.
	struct SavedBattle {
		std::string Name;
		bool WithMap = false; //!< Set up again whenever the map starts.
		bool Linked = false; //!< One of the map's linked battles, played in the list's order.
		std::array<BattleSettings, c_Sides> Cards;
		BattleModeSettings Mode;
		bool HasHoldZones = false; //!< Saved with the hold zones ("Hold ground"): loading it puts them in place of those on the map.
		std::vector<std::vector<Vector>> HoldZones;
	};

	/// The setups saved with a map, read from its file, in their order there. None if it has none.
	std::vector<SavedBattle> ReadSavedBattles(const std::string& scene);

	/// The next game started (the map browser's Play): the setup of this name goes in once its map is there, in place of the one marked to
	/// load with the map. With none, nothing does, not even that one.
	/// @param name The setup's name. Empty to leave it to the one marked to load with the map.
	/// @param none Load no setup at all.
	void LoadBattleOnStart(const std::string& name, bool none);

	/// The next game started from the map browser's quick battle (the main menu's Quick Battle): the setup it loads is started as well, once it
	/// is in and ready.
	void StartBattleOnStart(bool start);

	/// The battle setup now on the Battle tab kept as the map's last setup (B-11), in Userdata/BattlePresets/LastSetup/<map>.txt: offered first
	/// in the saved setups as "Last setup". Each time a battle is started.
	void RememberLastBattleSetup();

	/// A button's click asked about first (B-13), in a popup with a "Don't ask again" box (BattleAskFirst in the settings): true once it is to
	/// happen, at once with asking off or Ctrl held. Call right after the button (and its tooltip), in the same ID scope.
	/// @param clicked Whether the button was clicked this frame.
	/// @param popup The popup's ID, unique in the ID scope.
	/// @param question What is asked, as "Delete "Rush"?".
	/// @param yes The button that goes ahead, as "Yes, delete".
	bool AskFirst(bool clicked, const char* popup, const std::string& question, const char* yes);

	/// Linked battles (SandboxBattlePresets.cpp): which of them is on, and once it's won, the button to the next. Call inside an ImGui window.
	/// True when a battle was started from it.
	bool LinkedBattleControls();
	void DrawBattleMarks();
	/// "Hold ground" (SandboxHold.cpp): the mouse on the world with it in hand (a drag a box, clicks a polygon's corners, a right click takes a zone
	/// away), and Enter, Backspace and Escape for the polygon being drawn. Call each frame; it lets go of a part drawn polygon once the tool is put down.
	void HoldGroundInput();
	/// The hold zones on the map, with the setting on or "Hold ground" in hand, and the box or polygon being drawn.
	void DrawHoldZones();
	void TakeBattleTool(Tool kind, int team);
	bool AddZoneCorner(std::vector<Vector>& draft, BattleSettings& settings, const Vector& position, float closeWithin);
	bool CloseSpawnZone(std::vector<Vector>& draft, BattleSettings& settings);
	float ZoneCloseDistance();
	std::vector<ImVec2> ZoneOnScreen(const std::vector<Vector>& zone, float scale);
	void DrawZoneDraft(ImDrawList* drawList, float scale);
	/// Whether a place is inside a zone drawn as a polygon (a spawn zone, a mode's base), across a wrap or not.
	bool IsInZone(const std::vector<Vector>& zone, const Vector& at);
	/// The battle panel's "Keep clear of enemies" slider (BattleSettings::KeepClear, BattleModeSettings::KeepClear), with a button back to
	/// its default. True when changed. The id (an ImGui "##" suffix) keeps one apart from another in the same panel.
	bool KeepClearSlider(int& keepClear, const char* id);
	/// A place picked at random inside a zone drawn as a polygon, on its ground, for something this tall to stand at (its middle).
	Vector SpotInZone(const std::vector<Vector>& zone, float height);
	/// The ground straight under a mode zone's columns that have no floor in them (a gap, or the zone drawn in the air or its floor blown
	/// away), however far down: the first solid point under each, a few pixels apart, as its glowing line shows it. Standing there counts as
	/// in the zone (CountIn), and it's where the zone's units are posted when it has no floor at all.
	std::vector<Vector> GroundUnderZone(const std::vector<Vector>& zone);
	bool ModeBaseCorner(const Vector& position, float closeWithin);
	bool CloseModeBase();
	void PutDownBattleTool();
	bool FactionPicker(BattleSettings& setup);
	void MakeDefender(Actor* unit, const BattleSettings& settings);
	void RecentreDefenders(int team, const Vector& centre, bool atIt, float radius = -1.0F);
	void MoveDefender(BattleDefender& defender, const Vector& centre, bool atIt, float radius = -1.0F);
	void CommandDefender(Actor* unit, const Vector& centre, const Vector& post, float radius = -1.0F);
	void MoveCommandedZone(Actor* unit, const Vector& centre, const Vector& post);
	void DrawDefendZone(ImDrawList* drawList, const Vector& centre, float radius, float chase, ImU32 color);
	void DrawCommandedZones(ImDrawList* drawList);
	/// The orders the selected ones are given, for any units: defend at a point (posts round it, facing), a patrol round points, a battle objective.
	void DefendUnitsAt(std::vector<Actor*> units, const Vector& point, int facing);
	void PatrolUnits(std::vector<Actor*> units, const std::vector<Vector>& points, bool backAndForth);
	void ObjectiveUnits(const std::vector<Actor*>& units, const BattleObjective& objective, bool defend);
	bool JoinBattleObjective(Actor* unit);
	void ReleaseFromBattleMode(const Actor* unit);
	void SendBattleMode(int command = BattleModeSet);
	void ApplyBattleMode(const Stroke& stroke);
	BattleSettings ModeTeamSettings(int side, const BattleSettings& card);
	void ModeUnitsMade(int side, const std::vector<Actor*>& wave);

	// Spawn orders (SpawnOrders.cpp): a team you command has its new units given an order as they come in.
	/// New units of a team the player commands, made (not yet in): each is given the spawn order once it is in and on its feet.
	void SpawnOrderUnitsMade(int side, const std::vector<Actor*>& wave);
	/// Each update: the new units in and ready given their spawn orders.
	void UpdateSpawnOrders(bool aiPaused);
	/// The units waiting for their spawn orders forgotten (a game started or stopped).
	void ForgetSpawnOrderUnits();
	/// The spawn order a team's unit at a place takes: its spawn zone's own, else the team's.
	const SpawnOrder& SpawnOrderAt(const BattleModeSettings& settings, int side, const Vector& at);
	/// With a spawn order being set (s_SpawnOrderArmed), an order placed by the command tool becomes it instead of going to the selection.
	/// False when it isn't being set, and the stroke is given as usual.
	bool TakeSpawnOrder(const Stroke& stroke);
	/// A spawn order set for your team, or a zone of it (zone -1 for the team's), and sent.
	void SetSpawnOrder(int side, int zone, const SpawnOrder& order);
	/// The team whose spawn orders the Command tool's row sets: the one you command, or -1 for none.
	int SpawnOrderSide();
	/// The colour a spawn order is drawn in: its command mode's.
	ImU32 SpawnOrderColor(SpawnOrderKind kind);
	/// Where each spawn order of the team you command sends its units: a line from the zone (or every zone) to the place, in the order's colour.
	void DrawSpawnOrders();
	/// Gives units a spawn order now, as the order would be given the selection (with its urgency, and Defend at's zone as it was set).
	void GiveSpawnOrderTo(int side, const SpawnOrder& order, std::vector<Actor*> units);

	// Spawn points (SandboxSpawnPoints.cpp): points on their own that units come in at on a clock (Tool::SpawnPoint).
	/// A Tool::SpawnPoint stroke, in the sim.
	void ApplySpawnPointStroke(const Stroke& stroke);
	/// Each point's clock, its units' spawn orders and its count of those alive. Held while the AI is paused.
	void UpdateSpawnPoints(bool aiPaused);
	/// Every point dropped (a new game, or none on).
	void ForgetSpawnPoints();
	/// The points on the map, each with its area, its order and how far it is to the next lot.
	void DrawSpawnPoints();
	/// The tool's clicks: put down a point, pick one to change, right click one away, or place the order of the one being set up.
	void SpawnPointInput();
	/// The tool's settings on the Spawn tab: for the next point put down, or the one picked.
	void SpawnPointPanel();
	/// A line about the tool in hand for the bar: the point picked, or what the next one will be.
	std::string SpawnPointSummary();
	/// The units a battle team's card picks from (its factions, favourites, crabs and jetpacks), as the battle buys them (SandboxBattle.cpp).
	std::vector<const Preset*> BattleUnitsFor(const BattleSettings& settings);
	/// Somewhere inside a zone on its ground for a unit that high, as a battle's spawn zones put units (SandboxBattle.cpp).
	Vector SpawnSpotInZone(const std::vector<Vector>& zone, float height);

	/// Whether a team is one the player commands in the mode's game that is on: its units get no orders from the mode or the Battle Director.
	bool BattlePlayerCommands(int side);
	/// Whether a team is led by an AI commander in the mode's game that is on (never one the player commands).
	bool BattleHasCommander(int side);
	/// How hard the AI commanders play: 0 easy, 1 normal, 2 hard.
	int BattleCommanderLevel();
	/// Where a team in the mode's game that is on comes in: the middle of its widest spawn zone, else its point. False for a team not in it.
	bool BattleTeamBase(int side, Vector& at);
	/// Whether a circle at a point would reach into, or be in the sky above, a spawn zone of a team (in the mode's game that is on) other than a side's.
	bool InEnemySpawnZone(int side, const Vector& at, float radius);
	/// The mode's game ended: won by a side (or -1, no one), with how it ended said.
	void EndBattleGame(int winner, const std::string& result);
	/// A side gives up the mode's game that is on (the pause menu's Surrender, through a BattleSurrender stroke): it ends, won by the only other
	/// team in it (or nobody, with more than one), and in Battle Command the after-battle report opens.
	void SurrenderBattleGame(int side);
	/// Whether the mode set up on the Battle tab (not custom) can be started as it is, and if not, why.
	bool BattleModeReady(std::string& why);
	/// Whether a mode's setup (not necessarily the Battle tab's) can be started as it is, and if not, why.
	bool BattleModeSetupReady(const BattleModeSettings& setup, std::string& why);
	// Battle layouts (BattleLayout.cpp): a preset mode laid out on the map loaded, from a seed.
	/// Lays the setup's mode out on the map: its playing teams' spawn zones and the mode's points, zones, goals and positions, replacing
	/// those it had, from a seed (the same map, mode and seed give the same layout). False, with why in note, if it couldn't; true with
	/// what it did in note, and any check it couldn't meet (a team that can't get to an objective, routes far from even). Each playing
	/// team gets zonesPerTeam spawn zones (1 to 4) where the map has room: its base and more round it.
	bool LayoutBattle(BattleModeSettings& setup, unsigned seed, int zonesPerTeam, std::string& note);
	/// The Battle tab's "Generate layout": a seed, and the mode laid out with it on the map (the teams' cards sent; changed set for the mode's).
	void BattleLayoutPanel(BattleModeSettings& setup, bool& changed);
	/// The battle mode's objectives, flags and their carriers, spawn zones and mode zones on the sandbox's map window (B-7), as the "Show..."
	/// choices on the battle panel have them on the view.
	void DrawBattleModeOnMap(ImDrawList* drawList, const std::function<ImVec2(const Vector&)>& toMap);
	Vector ModeSpawnSpot(int side, const std::vector<Vector>& zone, const Actor* unit);
	/// The places new units coming in anywhere keep clear of while a mode's game is on, besides enemy units and spawn zones: its flags.
	void ModeKeepClearSpots(std::vector<Vector>& spots);
	int ModeRoom(int side, int room);
	void UpdateBattleMode(bool aiPaused);
	void ForgetBattleMode();
	void BattleModeTab();
	void DrawBattleMode();
	/// The battle's zones and points edited on the map with the Command tool, while the battle panel (or the Battle tab) shows: a click
	/// selects one, a drag moves it or its corners. Called with the pointer on the world; true when it took the mouse this frame.
	bool BattleEditMouse();
	/// On the map: what is selected for editing, what is under the pointer, and while the game is on, where your spawn zones may go.
	void DrawBattleEdit();
	/// Why the last move was taken back, by the pointer for a moment.
	void DrawBattleEditNote();
	/// On the battle panel: the "Moving spawns" choice, and how to edit on the map.
	void BattleEditPanel();
	/// What a battle mode's tool in hand is for, in the mode chosen, by the pointer (as "Red flag", "Hill"); empty for the tool's own name.
	std::string BattleToolLabel(Tool kind);
	// The battle's pickups (BattlePickups.cpp).
	void BattlePickupsPanel(BattleModeSettings& setup, bool running, bool& changed);
	std::string BattlePickupToolLabel(Tool kind);
	void PickupSpotPlaced(const Vector& position);
	void PickupZoneDrawn(const std::vector<Vector>& zone);
	void ApplyPickupStroke(const Stroke& stroke);
	void StartBattlePickups();
	void StopBattlePickups();
	void UpdateBattlePickups(bool aiPaused);
	void DrawBattlePickups();
	void DrawBattlePickupsOnMap(ImDrawList* drawList, const std::function<ImVec2(const Vector&)>& toMap);
	// What BattlePickups.cpp borrows from the battle modes (SandboxBattleModes.cpp).
	bool BattleTeamIn(int side);
	std::vector<Actor*> BattleFighters();
	bool BattleUnitBusy(const Actor* unit);
	float BattleGapToZone(const std::vector<Vector>& zone, const Vector& at);
	std::vector<std::vector<Vector>> BattleSpawnZonesOf(const BattleModeSettings& settings, int side);
	float BattlePulse(float speed);
	void DrawBattleBeacon(ImDrawList* drawList, ImVec2 foot, ImU32 color, float strength);
	void DrawBattleTag(ImDrawList* drawList, ImVec2 over, const std::string& text, ImU32 color);
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
	/// The list a tool's recent uses are shown over (s_Recent): a spawn or build tool's own list (its kind), Tool::PourOther for every
	/// Paint tab material (liquids, loose ground, terrain), Tool::Effect for effects; Tool::None for a tool whose uses aren't kept.
	Tool RecentList(Tool kind);
	/// Notes a use of a tool, newest first in its list's Recent row, and keeps the file. @param name The preset, material or effect it was used with ("" for a material tool's own).
	void NoteRecent(Tool kind, const std::string& name);
	/// Reads the Recent rows from their file, the first time only.
	void LoadRecentFile();
	/// The Recent row over a list: a tile for each thing of it last used, newest first, a click taking it up again. Nothing while there are none.
	/// @param list The list, as RecentList gives it.
	void RecentRow(Tool list);
	/// The brush in hand a tenth bigger (way 1) or smaller (-1), by a pixel at least, within its slider's range ([ and ], Ctrl+wheel over
	/// the world): its size, or with Brush type Shape's Line, the line's width. The size is shown by the pointer for a moment.
	/// @return Whether the tool in hand has a size to change.
	bool StepBrushSize(int way);
	/// Whether Alt+click is the eyedropper with this tool in hand: the Paint tab's brushes and the Spawn tab's tools.
	bool CanEyedrop(Tool kind);
	/// The eyedropper (Alt+click): what is under the pointer taken up. A unit, brain, vehicle or item picks it in the Spawn tab's list,
	/// with its tool; a liquid, loose ground or terrain the Paint tab's tool that pours or paints it. With a Spawn tool in hand a thing is
	/// looked for first, with a Paint tool the ground. What was picked is shown by the pointer.
	void Eyedrop(const Vector& at);
	void SideChooser();
	void UndoButton(); //!< Undo and Redo: the newest step of the shared paint and placing undo taken back (Tool::UndoTerrain), as Ctrl+Z does, or made again (Tool::RedoTerrain), as Ctrl+Y does.
	/// Asks whether the player is sure before something that can't be taken back, as the Reset button does: a click on the button opens the
	/// question, and true comes back on the frame they say yes. A Ctrl+click skips the question (say so in the button's tooltip).
	/// Called after the button, and outside any BeginDisabled, with the ID stack as it was for the button.
	/// @param clicked Whether the button was clicked this frame. @param title The question's title, which is also its ID.
	/// @param question What is lost, after "Are you sure?". @param yes The words on the button that goes ahead.
	bool Confirmed(bool clicked, const char* title, const char* question, const char* yes);
	void PresetList(Tool kind, const char* group = nullptr, float rows = 8.0F);
	/// @param mine Whether your loadouts and squads (s_SpawnPick) are offered too: the Spawn tab's units and drops.
	void LoadoutChooser(const char* label = "Loadout", bool mine = false);
	void ForgetPictures();
	void PaletteColor(int index, unsigned char* rgb);
	const PiecePicture& PictureOf(const Preset& preset);
	bool ChoiceCombo(const char* label, std::string& chosen, const std::vector<std::string>& values);
	/// The picture browser of a list, as on the Spawn tab. @param pickInto If given, a click sets this to the preset name picked (and that
	/// one is shown as chosen) instead of taking up the tool, with search and filters of its own.
	/// @param rows With pickInto, how many rows of tiles tall it is (0 for a few, below 0 the rest of the window); else it fills the rest of the window. @param pickerSlot With
	/// pickInto, which picker's search and filters it keeps (0 the You tab's base class; others for the Loadouts window's own).
	void PictureGrid(Tool kind, const char* group, std::string* pickInto = nullptr, float rows = 0.0F, int pickerSlot = 0);

	/// The Spawn tab's vehicle options: Default or Modified, the tuning sliders (with the vehicle chosen's own values beside them) and the crew.
	void VehicleOptions();
	void UnitStatsOptions(const Actor* unit, const char* heading);
	void FormationCombo(const char* id);
	void DrawOrderFeedback();
	void DrawMinimap();
	const NoRoute* NoRouteAt(const ImVec2& mouse);
	void LookAtUnits(const std::vector<UnitRef>& units);
	/// Every idle unit of the selection's side selected at once (B-2, Ctrl+.); with add, added to the selection instead.
	void SelectAllIdle(bool add);
	/// Every unit of the kinds already selected (RC-6): in view (Q), or on the whole map (B-4, Ctrl+Q).
	void SelectSameKinds(bool wholeMap);
	/// The camera bookmark keys (B-10): Ctrl+Shift and 1 to 4 keeps the view for this map, Ctrl+Alt and the number goes back to it. Kept in
	/// Userdata/CameraBookmarks.txt between games. Call from the ImGui frame while above the world.
	void CameraBookmarkKeys();
	/// The Army overview window (B-6): your side's units by kind, with how many, their average health and how many are idle; a click on a
	/// kind selects them all, a double click looks at them too. Shown while the setting is on (SettingsMan::ShowArmyOverview). Call from the ImGui frame.
	void DrawArmyOverview();
	/// Sets the selected units off round the patrol route clicked out (s_PatrolDraft), a loop or back and forth, or takes it as the spawn order
	/// being set; the route is cleared. Nothing with fewer than two points.
	void StartPatrolDraft(bool backAndForth);
	void CommandHotkeys();
	void KeysPage();
	void ToggleWindowTab(const char* tab);
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
	void DrawBuildingPanel();
	void UnmarkSelection();
	void DrawSelection();
	void DrawRallyPoints();
	void DrawRespawnPoint();
	void SideStatus();
	void TimeControls();
	void BarDivider();
	bool ShelfRow();
	void DrawBar();
	void PictureText(ImDrawList* drawList, ImVec2 at, float room, ImU32 color, const char* text);
#pragma endregion

#pragma region Battle Command (CommanderBar.cpp)
	/// While Battle Command is in development, the Commander Toolbar key (F11 unless rebound) swaps its Commander Toolbar for the sandbox's own bar
	/// (and back), so the sandbox's tools can be tried out in it. Off, the Commander Toolbar is the only bar. (The Sandbox game mode always has the switch.)
	constexpr bool c_CommanderBarSwitch = true;
	inline bool s_CommanderBar = true; //!< The Commander Toolbar along the bottom (true), or the sandbox's bar (the Commander Toolbar key). Battle Command starts on the toolbar, the Sandbox game mode on its bar.
	inline bool s_BattlePanelOpen = false; //!< Battle Command: the battle panel (setting the battle up, starting and stopping it) is open.

	/// The side you command with the Commander Toolbar up: in Battle Command the team ticked "You command this team" in the battle (the first
	/// such), else Red; in the Sandbox game mode the side picked in the sandbox. -1 in any other game, or with the sandbox's bar up instead,
	/// when every side is yours as in the sandbox.
	int CommandedTeam();

	/// The side whose units alone can be selected and commanded: yours in commander mode (RC-9) and in Battle Command; -1 for every side.
	int OnlySide();

	/// Whether the battle is being set up on screen: the sandbox's Battle tab showing, or Battle Command's battle panel open. Its spawn zones,
	/// drop lines and defence points are drawn on the map then.
	bool BattleSetupShowing();

	/// Battle Command: keeps the battle's commanded team one of the teams in it (Red if none is ticked), every frame. Call from the ImGui frame.
	void KeepCommandedTeam();

	/// The Commander Toolbar along the bottom of the picture in Battle Command (and in the Sandbox game mode, swapped in for its bar): the command tools, the battle panel, your side and its units,
	/// the speed of time and the pause. Above it, the command row (formations, rules, groups) while the Command tool is in hand.
	void DrawCommanderBar();

	/// Battle Command's battle panel: the battle's mode, teams, spawn zones and objectives, started and stopped. What the sandbox's Battle tab
	/// is in the Sandbox game mode, which Battle Command doesn't offer.
	void DrawCommanderPanel();
#pragma endregion

#pragma region Reinforcements (Reinforcements.cpp)
	/// The units a team you command can call in, by Loadout preset: each from the team's first faction (Coalition with none picked).
	struct ReinforcementCard {
		const char* Name;
		const char* Loadout;
		const char* Tip;
	};
	constexpr ReinforcementCard c_ReinforcementCards[] = {
	    {"Rifleman", "Infantry Light", "A light soldier with a rifle and a sidearm."},
	    {"Sniper", "Infantry Sniper", "A light soldier with a sniper rifle: long range, slow to fire."},
	    {"Heavy", "Infantry Heavy", "A heavy soldier with a heavy weapon: tough and hard-hitting, slow."},
	    {"Digger", "Infantry Engineer", "A soldier with a digger: tunnels through the ground, and digs gold when left to it."},
	    {"Crab", "Mecha", "The faction's crab: armoured and heavily armed, but can't climb or jump much."},
	};
	constexpr int c_ReinforcementCardCount = static_cast<int>(std::size(c_ReinforcementCards));

	/// A team's points for reinforcements while a battle mode's game is on and you command it: earned by its kills, the objectives it takes
	/// and the gold it digs, spent on units called in by ship.
	struct CommandPoints {
		float Points = 0.0F;
		float FromKills = 0.0F;
		float FromObjectives = 0.0F;
		float FromGold = 0.0F;
		int Kills = 0;
		int Called = 0; //!< Units called in.
		float Spent = 0.0F;
	};
	inline std::array<CommandPoints, c_Sides> s_CommandPoints;

	// The Commander Toolbar's reinforcements, in the window: where they land, and how.
	inline Vector s_LandingSpot;
	inline bool s_HasLandingSpot = false;
	inline int s_PendingCard = -1; //!< A card clicked with no landing spot marked: called in once one is (Tool::LandingSpot).
	inline int s_CallCraft = 0; //!< Index into c_Crafts.
	inline int s_CallSize = 1; //!< Units a card calls in at a time: 1, 3 or 5.

	/// Every team's points back to the start, and the kills, scores and gold they're earned from forgotten. At a mode's start and a new game.
	void ForgetReinforcements();

	/// The points earned since the last update, by the teams you command. Call once per sim update, from UpdateBattle.
	void UpdateReinforcements();

	/// A call for reinforcements (Tool::BattleTeam, BattleReinforce), in the sim: bought from the team's points and sent in by ship.
	void ApplyReinforcement(const Stroke& stroke);

	/// A team's reinforcements bought from its points and sent in by ship over a point: a card's units, so many, by a craft (of c_Crafts).
	/// False, with nothing spent, when it can't pay or its faction has no such unit.
	bool CallReinforcements(int side, int card, int count, int craft, const Vector& at);

	/// What a card's unit costs, as bought, for a side's faction. 0 if its faction has no such unit. Kept once worked out.
	float ReinforcementCost(int card, int side);

	/// The next battle's start (a linked battle) keeps the points the teams you command have left now, and their units' ranks.
	void CarryIntoNextBattle();

	/// One of a card's units for a side, of its faction, on its team and AI-controlled, or nothing if the faction has no such unit. Ownership
	/// is the caller's.
	Actor* MakeArmyUnit(int card, int side);

	/// The Commander Toolbar's reinforcements group: the points, the cards, the landing spot, the craft and how many at a time.
	void ReinforcementTiles();

	/// A click on the world with the landing spot tool: marks the spot, and calls in the card waiting for one.
	void LandingSpotClicked(const Vector& position);

	/// A click on the world with the rally point tool (Tool::BattleRally): your team's rally point put there.
	void RallySpotClicked(const Vector& position);

	/// Battle Command: the landing spot marked on the map. Call from the ImGui frame.
	void DrawLandingSpot();
#pragma endregion

#pragma region Commander powers (CommanderPowers.cpp)
	/// What the team you command can call on besides reinforcements, each with a cooldown.
	enum class CommanderPower {
		Artillery, //!< Shells lobbed in on a point.
		Orbital, //!< A beam from orbit: bores down through anything under the point.
		Smoke, //!< A thick cloud of smoke at the point.
		Supply, //!< Your units round the point patched up (wounds closed, health back) and their guns reloaded.
		Scan, //!< The fog of war lifted everywhere for a while.
		Evac, //!< Your units round the point pulled out, half what they were worth back in points.
		Rally, //!< Your shaken units round the point steadied: their nerve back, so they stop falling back.
		Count
	};
	struct CommanderPowerInfo {
		const char* Name;
		const char* Tip;
		float Cooldown; //!< Seconds before it can be used again.
		float Radius; //!< Pixels round the point it works in, 0 for no point.
	};
	constexpr CommanderPowerInfo c_CommanderPowers[static_cast<int>(CommanderPower::Count)] = {
	    {"Artillery", "Five shells lobbed in on the point from far off.", 60.0F, 60.0F},
	    {"Orbital", "A beam straight down from orbit: bores a shaft through whatever is under the point and sets it alight.", 150.0F, 20.0F},
	    {"Smoke", "A thick cloud of smoke at the point, to cover a move.", 30.0F, 40.0F},
	    {"Supply", "Your units round the point are patched up (wounds closed, health back) and their guns reloaded.", 90.0F, 150.0F},
	    {"Scan", "Lifts the fog of war everywhere for 10 seconds.", 90.0F, 0.0F},
	    {"Evac", "Your units round the point are pulled out of the battle, and half what they were worth comes back in points.", 120.0F, 120.0F},
	    {"Rally", "Your units round the point find their nerve again: those breaking stop falling back and fight on.", 45.0F, 250.0F},
	};
	inline int s_PendingPower = -1; //!< A power picked on the Commander Toolbar, waiting for a click on the map (Tool::PowerTarget).
	inline long long s_ScanUntil = -1; //!< The sim update the Scan power's lifting of the fog of war lasts till.

	/// Every team's powers ready again. At a mode's start and a new game.
	void ForgetPowers();

	/// Every team's powers start their full cooldown, so none can be used the moment a battle starts.
	void StartPowerCooldowns();

	/// A power used (Tool::BattleTeam, BattlePower), in the sim: if it's ready, done, and its cooldown started.
	void ApplyPower(const Stroke& stroke);

	/// A team's power used at a point, if it's ready: done, and its cooldown started. False if it wasn't used.
	bool UsePowerAs(int side, int power, const Vector& at);

	/// Seconds before a team's power can be used again, 0 if it can now.
	float PowerSecondsLeft(int side, int power);

	/// Whether a team's strike (artillery or orbital) at a point would be on or above an enemy spawn zone, where it can't be called.
	bool StrikeOnSpawn(int side, int power, const Vector& at);

	/// The Commander Toolbar's powers group: a tile each, greyed with the seconds left while it cools down.
	void PowerTiles();

	/// Whether a tile's key on the Commander Toolbar was pressed this frame (B-1): Alt and its number for the reinforcement cards, Alt+Shift and
	/// its number for the powers, counted from 1 in the toolbar's order. Never with Ctrl, nor while a text box has the keys.
	bool ToolbarKeyPressed(int number, bool shift);

	/// The game's speed a step slower (step -1) or faster (1) through the speed tile's speeds, stopping at the ends; 0 back to 1x (B-5, - and =).
	void StepGameSpeed(int step);

	/// A click on the world with the power target tool: the power waiting for one is used there.
	void PowerTargetClicked(const Vector& position);

	/// The circle a power waiting for a target will hit, under the pointer. Call from the ImGui frame.
	void DrawPowerTarget();
#pragma endregion

#pragma region Veterancy (Veterancy.cpp)
	/// Where marks over a unit go: just over the top of what its own HUD drew above its head last frame (health, team, weapon), or, with
	/// that not drawn, just over its head.
	inline Vector OverHeadPos(const Actor* actor) {
		const float stack = actor->GetAboveHUDPos().m_Y - actor->GetPos().m_Y - 6.0F;
		return actor->GetPos() + Vector(0.0F, std::min(stack, -actor->GetHeight() * 0.5F) - 2.0F);
	}

	/// A unit's record in a Battle Command battle: the name it's given, its kills, when it came in and the rank they've earned it.
	struct Veteran {
		int Team = -1;
		int Kills = 0;
		long long Since = 0; //!< The sim update it was first seen in the battle.
		int Rank = 0; //!< 0 Private, 1 Corporal, 2 Sergeant, 3 Veteran (Actor::SetVeterancy).
		std::string Name; //!< A surname, the same for the same unit every time it's asked for.
	};
	inline std::unordered_map<long, Veteran> s_Veterans; //!< By unique ID.

	/// The battle's veterans forgotten. At a mode's start and a new game.
	void ForgetVeterans();

	/// A kill, to the unit nearest the one that fell (UpdateReinforcements' count): toward its next rank. A ranked unit of yours that fell
	/// is said by name.
	void NoteVeteranKill(long killerID, long victimID);

	/// Every unit in a Battle Command battle given its record, and its rank from its kills and time in. Call once per sim update, from UpdateBattle.
	void UpdateVeterans();

	/// The rank badges over units in view, with the names of yours selected or under the pointer. Call from the ImGui frame.
	void DrawVeterans();

	/// A unit's rank and name, short, as "Sgt. Hale"; empty if it has none.
	std::string VeteranTitle(long id);
#pragma endregion

#pragma region After-battle report (BattleReport.cpp)
	/// The battle's record forgotten, and a new one started if a Battle Command battle has just started. At a mode's start and a new game.
	void ForgetBattleReport();

	/// A unit fallen (UpdateReinforcements' count), with the unit nearest it of another team (0 for none) credited with the kill.
	void NoteReportKill(long killerID, long victimID);

	/// Every unit in a Battle Command battle kept on record, with the scores and the objectives' holders; the report opened when it ends. Call
	/// once per sim update, from UpdateBattle.
	void UpdateBattleReport();

	/// A unit called in from a reinforcement card marked with the card's name: its kind in the report.
	void TagCardUnit(Actor* unit, int card);

	/// A unit's kind on record (Rifleman, Heavy, Crab...): empty for one not on record.
	std::string ReportUnitKind(long id);

	/// Whether a Battle Command battle has ended with its report to show.
	bool BattleReportReady();
	void OpenBattleReport();

	/// The report window, over the paused world, once the battle has ended. Call from the ImGui frame.
	void DrawBattleReport();
#pragma endregion

#pragma region Morale (BattleMorale.cpp)
	constexpr float c_ShakenMorale = 0.3F; //!< Under this a unit's nerve has gone and its AI pulls it back (SharedBehaviors.Shaken).

	/// Your units breaking (shaken) noticed, and said when several near each other do at once. Call once per sim update, from UpdateBattle.
	void UpdateMorale();

	/// A mark over each of your units that is breaking. Call from the ImGui frame.
	void DrawMorale();
#pragma endregion

#pragma region Enemy commander (BattleEnemy.cpp)
	/// The enemy's commanders' strikes still coming, and when each was due, forgotten. At a mode's start and a new game.
	void ForgetEnemyCommander();

	/// Each enemy team led by an AI commander in a Battle Command battle buys reinforcements and uses powers, as you do. Call once per sim
	/// update, from UpdateBattle.
	void UpdateEnemyCommander(bool aiPaused);

	/// Where the enemy's strikes are coming down, marked till they land. Call from the ImGui frame.
	void DrawEnemyStrikes();
#pragma endregion

#pragma region Loadout and Squad Builders (SandboxLoadouts.cpp)
	constexpr const char* c_LoadoutsFile = "Userdata/SandboxLoadouts.txt";
	inline std::vector<CustomLoadout> s_CustomLoadouts;
	inline std::vector<CustomSquad> s_CustomSquads;
	inline CustomPick s_SpawnPick; //!< What the Spawn tab's Units and Drop squad make instead of the unit picked, if anything.
	inline bool s_LoadoutWindowOpen = false; //!< The Loadouts window (the Loadout and Squad Builders) is open.

	/// Your loadouts and squads read from c_LoadoutsFile, the first time they're wanted.
	void LoadLoadoutsFile();
	void SaveLoadoutsFile();
	const CustomLoadout* FindCustomLoadout(const std::string& name);
	const CustomSquad* FindCustomSquad(const std::string& name);

	/// What a unit carries when spawned with its faction's kit, as item preset names: its own inventory, then the first of its faction's
	/// primaries, secondaries and grenades (the Coalition's for factions without guns). Where the Loadout Builder starts a unit from.
	std::vector<std::string> DefaultKitOf(const Preset& unit);

	/// The units a pick makes, one entry each: a loadout once, a squad laid out slot by slot. Empty for none, or one no longer there.
	std::vector<CustomLoadout> UnitsOfPick(const CustomPick& pick);

	/// A unit made as a loadout says, on a side, run by the AI: not yet in the world, with no orders. Null if its unit isn't in this game.
	Actor* MakeCustomUnit(const CustomLoadout& loadout, int team);

	/// Notes units just made as one of your squads: the first leads, with the order given; the others follow it once they're in the world
	/// (out of a craft that brought them), and a new leader is picked if it falls. They are drawn as a squad and selected together.
	void FormSquad(const std::vector<Actor*>& members, const std::string& name, int team, Order order);

	/// The squad a unit is in, as its members (leader first), or none. For selecting one picks up all of them.
	std::vector<Actor*> SquadMatesOf(const Actor* unit);

	/// Keeps your squads together: followers joined to the leader once out in the world, a new leader for a fallen one. Each sim update.
	void UpdatePlayerSquads();

	/// Units made from loadouts with an endless jetpack kept topped up. Each sim update.
	void UpdateLoadoutUnits();

	/// A new game (ActivityMan::StartActivity, a saved one included): none of the last game's squads, and endless jetpacks looked for if it is
	/// a saved game's, whose units may have them.
	void ForgetLoadoutUnits();

	/// A unit just made from the Spawn tab, its stats tuned as the Stats sliders say (nothing, unless Modified).
	void TuneUnit(Actor* actor, const UnitTuning& tuning);

	/// Each of your squads' name over its leader, with a line to each member. Call from the ImGui frame.
	void DrawPlayerSquads();

	/// The squads forgotten, on a new scene.
	void ForgetPlayerSquads();

	/// Your loadouts and squads to choose from, for inside a combo: Selectables that set pick. @param squads Whether squads are offered too.
	/// @return Whether one was picked.
	bool CustomPickItems(CustomPick& pick, bool squads);

	/// The Loadouts window: the Loadout Builder and the Squad Builder. Call from the ImGui frame.
	void LoadoutWindow();
#pragma endregion

#pragma region Squad follows me (BattleSquad.cpp)
	/// The squad of the unit you control in a Battle Command battle: the units you had selected follow it, and go for what it shoots at; T
	/// and X (SquadKeys) have them hold or follow, and regroup. Call once per sim update, from UpdateBattle.
	void UpdateSquad();

	/// T and X while you play a unit with a squad. Call from the ImGui frame, with no text box or Ctrl or Alt taking the keys.
	void SquadKeys();

	/// A mark over each of the squad, and what it's doing with the keys at the top of the view. Call from the ImGui frame.
	void DrawSquad();

	/// With the squad links overlay on: the number of each of the AI commanders' squads over its leader, and a line to each of its
	/// followers. Call from the ImGui frame.
	void DrawAISquads();
#pragma endregion

#pragma region Your brain on the field (BattleFieldBrain.cpp)
	/// The brain on the field forgotten (and the last game's taken away). At a mode's start and a new game.
	void ForgetFieldBrain();

	/// With the choice on: your brain put down at your base when the game starts, and the game lost if it dies. Call once per sim update.
	void UpdateFieldBrain();

	/// Whether one of your units is in reach of your brain's orders: always, unless the choice is on and it's of the team the brain commands.
	bool InCommandRange(const Actor* unit);

	/// The selected units out of command range taken out of the selection, for an order to skip them; PutBackOutOfRange puts them back after
	/// it, unless the order changed the selection.
	std::vector<UnitRef> TakeOutOfRange();
	void PutBackOutOfRange(const std::vector<UnitRef>& out, const std::vector<UnitRef>& before);

	/// The brain's command range, a mark over it, and a mark on the selected units out of range. Call from the ImGui frame, with the order marks.
	void DrawFieldBrain(ImDrawList* drawList);

	/// The brain and its command range on the sandbox's map window, mapScale its pixels for one of the scene's.
	void DrawFieldBrainOnMap(ImDrawList* drawList, const std::function<ImVec2(const Vector&)>& toMap, float mapScale);
#pragma endregion

#pragma region Hot-seat (BattleHotSeat.cpp)
	/// The team whose turn it is in a hot-seat game, or -1 with none on.
	int HotSeatSide();

	/// A hot-seat game's turns: the cover between them and the turn's clock. Call from the ImGui frame, after the other windows.
	void DrawHotSeat();
#pragma endregion

#pragma region Pick your army (BattleArmy.cpp)
	/// Each side's roster forgotten. At a mode's start and a new game.
	void ForgetArmy();

	/// Whether a side's units come from its picked army: the team you command, with the choice on.
	bool ArmyPicked(int side);

	/// Up to so many of the side's next units from what's left of its army, made and on its team (ownership is the caller's). Fewer, or none,
	/// once it's used up.
	std::vector<Actor*> ArmyWave(int side, int size);

	/// The battle panel's roster: the choice, the budget and how many of each card (what's left of them during the game).
	void ArmyPanel();
#pragma endregion

#pragma region Re-arming (BattleSupply.cpp)
	/// The units' kits forgotten. At a mode's start and a new game.
	void ForgetLoadouts();

	/// Each unit's kit (its guns, best first, and what it has to throw) noted the first time it's seen with any. Call once per sim update.
	void UpdateLoadouts();

	/// What a unit has lost of its kit given back (the Supply power): its best gun if it has none, and its grenades up to what it came with.
	/// How many items it was given.
	int RearmUnit(AHuman* human);
#pragma endregion

#pragma region Airlift (SandboxAirlift.cpp)
	constexpr int c_AirliftCost = 120; //!< Reinforcement points a ship, in Battle Command.

	/// The airlifts under way forgotten. When out of a game.
	void ForgetAirlifts();

	/// How many ships an airlift of so many units takes: one for each six, at most three (those past eighteen stay).
	int AirliftShipsFor(size_t units);

	/// The Airlift order: a gunship of their team for each six of the units comes down by them, they get in, and it flies them to the point
	/// and lets them out. Paid for now in Battle Command.
	void AirliftSelected(const std::vector<Actor*>& units, const Vector& point);

	/// The airlifts' ships watched through landing, boarding, flying and letting out. Call once per sim update.
	void UpdateAirlifts();

	/// Each airlift's ship, the way to where it's going and what it's doing. Call from the ImGui frame, with the order marks.
	void DrawAirlifts(ImDrawList* drawList);
#pragma endregion

#pragma region Engineer builds (SandboxBuild.cpp)
	/// A piece the Build order puts up: its name, its cost in reinforcement points (Battle Command only), how long it takes to build, and
	/// its size for the outline (scene pixels, standing on the point).
	struct BuildPiece {
		const char* Name;
		int Cost;
		int Seconds;
		float Width;
		float Height;
		const char* Tip;
	};
	constexpr BuildPiece c_BuildPieces[] = {
	    {"Sandbags", 40, 3, 60.0F, 16.0F, "A low wall of sandbags to crouch behind."},
	    {"Concrete wall", 80, 5, 12.0F, 64.0F, "A head-high concrete wall."},
	    {"Bunker", 200, 10, 112.0F, 64.0F, "A concrete roof on two walls, open low on both sides to shoot and get in through."},
	    {"Turret", 250, 8, 24.0F, 24.0F, "A turret of your team's that shoots at enemies in range."}};
	inline int s_BuildPiece = 0; //!< The piece the Build order puts up (c_BuildPieces).

	/// The building sites forgotten. When out of a game.
	void ForgetBuilds();

	/// The nearest of the units to the point carrying a Constructor and not building already; nullptr for none.
	Actor* BuilderFor(const std::vector<Actor*>& units, const Vector& point);

	/// The Build order: the nearest of the units carrying a Constructor sent to put up the piece picked at the point, paid for now in Battle
	/// Command (and paid back if it isn't built).
	void BuildSelected(const std::vector<Actor*>& units, const Vector& point);

	/// The builders watched: one at its site builds, and when done the piece goes up. Call once per sim update.
	void UpdateBuilds();

	/// Each site's outline and how far on it is. Call from the ImGui frame, with the order marks.
	void DrawBuilds(ImDrawList* drawList);
#pragma endregion

#pragma region Breach (SandboxBreach.cpp)
	/// The breaches under way forgotten. When out of a game.
	void ForgetBreaches();
	/// Takes a unit out of any breach it is digging or following in: given another order that keeps it in squad mode (a guard), it was
	/// still taken for a follower and put back behind a digger.
	void LeaveBreaches(const Actor* unit);

	/// The units sent to breach to a point: those with a digger that cuts the way dig there, the rest follow the nearest of them, and once a
	/// digger is through they all attack. If none can dig there, nobody goes, and the no-route markers say why.
	void BreachSelected(const std::vector<Actor*>& units, const Vector& point);

	/// Each breach watched for a digger getting through (then everyone attacks), or none left to. Call once per sim update.
	void UpdateBreaches();

	/// A marker at each breach's point, with the diggers' lines to it. Call from the ImGui frame, with the order marks.
	void DrawBreaches(ImDrawList* drawList);
#pragma endregion

#pragma region Alerts and kill feed (BattleAlerts.cpp)
	/// The alerts and the kill feed emptied. At a mode's start and a new game.
	void ForgetAlerts();

	/// Something in a Battle Command battle that needs your eye, listed at the top left for a while for you to click and look at. One of the
	/// same kind this recent and near is counted in with it rather than listed again.
	void AddAlert(const std::string& kind, const std::string& text, const Vector& at, ImU32 color);

	/// A unit fallen (UpdateReinforcements' count), with the unit nearest it of another team (0 for none): listed in the kill feed, and one of
	/// yours an alert.
	void NoteKillFeed(long killerID, long victimID, int victimTeam, const Vector& where);

	/// Your units under attack noticed. Call once per sim update, from UpdateBattle.
	void UpdateAlerts();

	/// The view moved to the latest alert (Space), or with back, to the one before the last looked at this way (Shift+Space, again and again).
	void JumpToAlert(bool back);

	/// The alerts, top left (with the log's button by them), and the kill feed, top right; the alert log, when open; and each new alert's sound.
	/// Call from the ImGui frame.
	void DrawAlerts();

	/// Opens or closes the alert log: the last alerts of the battle, each with when it came, to click and look there.
	void ToggleAlertLog();
	bool AlertLogOpen();

	/// Where alerts have just come, blinking for a few seconds on the sandbox's map window.
	void DrawAlertsOnMap(ImDrawList* drawList, const std::function<ImVec2(const Vector&)>& toMap);
#pragma endregion

#pragma region Fog of war (BattleFog.cpp)
	/// Where an enemy was last seen, once your units lost sight of it: shown fading on the map and the minimap.
	struct FogGhost {
		long ID = 0;
		int Team = -1;
		Vector Pos;
		bool Brain = false;
		long long SeenAt = 0; //!< The sim update it was last seen on.
	};
	inline std::vector<FogGhost> s_FogGhosts;
	constexpr float c_FogGhostSeconds = 20.0F; //!< How long a "last seen" mark lasts, fading.

	/// Whether the battle's fog of war is on now: Battle Command, a mode's game on with fog of war ticked, and the Commander Toolbar up (the
	/// sandbox's bar, F11, sees everything).
	bool FogOn();

	/// Hides from view each enemy none of your team's units can see, and marks where those lost sight of were last. With the fog off, every
	/// unit is shown again. Call once per sim update, from UpdateBattle.
	void UpdateFog();

	/// How long ago a "last seen" mark was made, as its share of c_FogGhostSeconds (0 new, 1 gone).
	float FogGhostAge(const FogGhost& ghost);

	/// The "last seen" marks on the map. Call from the ImGui frame.
	void DrawFogGhosts();
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
