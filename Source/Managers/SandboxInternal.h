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
#include "TerrainFire.h"
#include "WeatherLightning.h"
#include "TerrainObject.h"
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
		UndoTerrain, //!< Puts back the terrain the last paint or build stroke changed (see s_PaintUndo).
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
		ClearMap //!< Clears one kind of thing off the whole map (Count a ClearKind): the World tab's Clear. Appended, so the tools before keep their numbers.
	};

	/// What the World tab's Clear takes off the map (Tool::ClearMap's Count).
	enum class ClearKind {
		Buildings, //!< Doors, the bunker parts put down and colony buildings; with Choice 1, the building materials too (c_BuildingMaterials).
		Liquids, //!< The liquids in Stroke::Materials; with Choice 1, the springs that pour them too.
		Units, //!< Every unit of the side in Team, or of every side with -1. Not doors, nor your character.
		Ground //!< The terrain materials in Stroke::Materials.
	};

	/// What bunkers and the things the sandbox builds are made of, cleared with the buildings.
	constexpr const char* c_BuildingMaterials[] = {"Concrete", "Metal", "Mega Metal", "Mangled Metal", "Door Metal", "Scrap Metal", "Glass", "Civilian Stuff", "Military Stuff", "Ladder"};

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
	struct Preset {
		std::string Label;
		std::string ClassName;
		std::string PresetName;
		std::string Module;
		int ModuleID = -1;
		std::string Group; //!< Structures: the kind of bunker piece ("Bunker Modules", "Bunker Lights"...), to list them by.
		std::string Kind; //!< A subcategory to list by: for units "Infantry", "Mecha", "Turrets"; for items "Primary weapons", "Grenades", "Tools"...
		bool Modded = false; //!< From a module that isn't one of the game's own.
		bool Jetpack = false; //!< Units: it has a jetpack it can fly with (one with some jet time).
		int Width = 0; //!< Structures: footprint, for the preview.
		int Height = 0;
		float OffsetX = 0.0F;
		float OffsetY = 0.0F;
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
	struct BattleModeSettings {
		BattleMode Mode = BattleMode::Custom;
		int TeamSize = 16; //!< Most units each team has alive at once.
		std::array<bool, c_Sides> Plays = {true, true, false, false}; //!< The teams taking part, by side.
		std::array<std::vector<std::vector<Vector>>, c_Sides> SpawnZones; //!< Each team's spawn zones, drawn as polygons: its units appear in them.
		std::array<bool, c_Sides> HasPoint{}; //!< Each team's point placed in its base (capture the flag: where its flag stands). Without, one is picked.
		std::array<Vector, c_Sides> Points;
		std::array<std::vector<Vector>, c_Sides> Goals; //!< One flag: each team's goal zone, drawn as a polygon, that it brings the flag into to score.
		bool HasFlagSpot = false; //!< One flag: whether the neutral flag's place is set. Without, the game can't start.
		Vector FlagSpot;
		bool ByShip = false; //!< Its units come in by ship over their base, rather than appearing in it.
		bool MoveStuckPoint = true; //!< Capture the flag: a flag nobody can get to (buried, or cut off) moves somewhere else in its base.
		int ScoreToWin = 3; //!< Capture the flag: captures that win. 0 plays on for good.
		int GuardPercent = 30; //!< Capture the flag: the share of each team's units, in percent, that stay to guard its flag.
		int ReturnSeconds = 30; //!< Capture the flag: how long a dropped flag lies before it goes back home by itself.
		int RespawnSeconds = 5; //!< Every mode: seconds after one of a team's units falls before another comes in its place.
		int MaxRespawns = 0; //!< Every mode: fallen units each team gets back in all, after its first team size. 0: no limit.
		int StuckSeconds = 20; //!< Every mode: seconds a unit can get no nearer its objective before it is respawned. 0: never.
		int RouteVariety = 0; //!< Every mode: the share of each team's units, in percent, given a taste in routes of their own (Actor::SetRouteSeed), so they spread over the ways to where they're going rather than all taking the shortest.
		std::vector<std::vector<Vector>> Zones; //!< The mode's own zones, drawn as polygons: king of the hill's hills, assault's objectives (in order).
		int HoldToWin = 120; //!< King of the hill: seconds holding the hill that win.
		int HillMoveSeconds = 0; //!< King of the hill, with more than one hill: seconds before the hill moves on to the next. 0: it stays put.
		bool MajorityScores = false; //!< King of the hill: a hill with more than one team on it scores for the team with the most there, not for none.
		int Attacker = 0; //!< Assault: the side that attacks; the rest defend.
		int CaptureSeconds = 15; //!< Assault: seconds attackers stand in an objective with no defender in it to take it.
		int TimeLimit = 300; //!< Assault: seconds the attackers have; each objective taken adds BonusSeconds.
		int BonusSeconds = 60;
		int Tickets = 60; //!< Last team standing: units each team gets in all, its first ones counted.
		int KillsToWin = 5; //!< VIP hunt: enemy VIPs a team has to bring down to win.
		int VipRespawnSeconds = 20; //!< VIP hunt: seconds before a fallen VIP's team has a new one.
	};

	/// One queued action, with the settings it was made with.
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
		std::string Material; //!< Springs, the tank and "Other": the liquid or powder poured, by preset name (taken at the click, not read in the sim).
		float Rate = 1.0F; //!< Springs: how much of the time they pour, 0.05 to 1.
		BattleSettings Battle; //!< Tool::BattleTeam: the team's settings.
		BattleModeSettings Mode; //!< Tool::BattleTeam with a BattleMode command: the mode's settings.
		std::vector<int> Materials; //!< Tool::ClearMap: the material IDs to clear (liquids or ground).
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
	inline bool s_ShowObjectives = true; //!< Each battle mode's objectives (its flags, hills, goals...) lit up on the map, each in the look it asks for.
	inline int s_ObjectiveLook = 0; //!< The look of zone objectives: 0 each mode's own, else an ObjectiveLook (plus one) for all of them.
	inline bool s_ShowModeBases = true; //!< The teams' spawn zones shaded and outlined on the map (always while one is being drawn, or a point placed).
	// The Unit and Drop tools' random units (copied into the stroke at the click): from every faction, one faction or the favourites.
	inline bool s_RandomUnits = false;
	inline bool s_RandomFavourites = false;
	inline int s_RandomFaction = -1; //!< -1 every faction, otherwise an index into s_FactionModules.
	inline bool s_JetpackOnly = false; //!< The Spawn tab's "Jetpacks only": units without one aren't listed, or picked at random.
	inline std::vector<int> s_FactionModules;
	inline std::vector<std::string> s_FactionNames;
	inline int s_Radius = 6;
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
	constexpr bool c_ShowColonyTab = false; //!< Whether the sandbox window offers the colony buildings.
	/// What the command tool does with a click on the world.
	enum class CommandMode {
		Move, //!< Each selected unit to its own spot round the point; a click on an enemy attacks it, a click on a friend selects it.
		Attack, //!< Go for the nearest enemy to the point, or the point itself with orders to fight.
		Guard, //!< Follow the friendly unit clicked and stay with it.
		AttackMove, //!< Walk to the point, stopping to fight any enemy met on the way, then carry on to it (RC-2).
		DefendAt, //!< Post the units round the point to hold it, facing the way the button was dragged (RC-4).
		Patrol //!< Each click a point of a patrol route; the command row starts it as a loop or back and forth (RC-4).
	};
	inline CommandMode s_CommandMode = CommandMode::Move;
	constexpr const char* c_CommandModeNames[] = {"Move", "Attack", "Guard", "Attack-move", "Defend at", "Patrol"};
	constexpr ImU32 c_CommandModeColors[] = {IM_COL32(110, 180, 250, 255), IM_COL32(239, 106, 91, 255), IM_COL32(120, 220, 120, 255), IM_COL32(245, 150, 70, 255), IM_COL32(242, 182, 61, 255), IM_COL32(120, 200, 220, 255)};
	inline std::vector<Vector> s_PatrolDraft; //!< The points of the patrol route being clicked out (RC-4), in order.
	constexpr const char* c_WeaponRuleNames[] = {"Fire at will", "Return fire", "Hold fire"}; //!< By Actor::WeaponRule.
	constexpr const char* c_MovementRuleNames[] = {"As ordered", "Engage", "Move only", "Hold ground"}; //!< By Actor::MovementRule.
	inline float s_Spacing = 18.0F; //!< How far apart units stand when sent somewhere together.
	/// The zone a Defend order holds, as a Battle Director team's defend place is (UpdateBattleDefenders): units stand inside the radius,
	/// go after enemies that come within the radius and chase distance, and come back after; a share of them roam the zone.
	inline int s_DefendRadius = 100; //!< px from the point.
	inline int s_DefendChase = 200; //!< px past the radius.
	inline int s_DefendRoam = 0; //!< % of the units that roam the zone rather than hold a post.
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

	/// Terrain painting's undo: each step is what one stroke of a paint or build tool changed (a drag of the brush is one step: changes
	/// less than a quarter second apart run together), pixel by pixel as it was before, the first change to each pixel only. The last 20
	/// steps are kept, up to c_PaintUndoPixels pixels in all (the oldest go first), and a new game forgets them. A stroke longer than a step
	/// holds goes on in a new step, so each Ctrl+Z takes back part of it rather than the rest being lost.
	struct PaintUndoPixel {
		unsigned short X; //!< (Scenes are well under 65536 px on a side.)
		unsigned short Y;
		unsigned char Material;
		unsigned char Color; //!< The 8 bit foreground colour.
	};

	struct PaintUndoStep {
		std::vector<PaintUndoPixel> Pixels;
		std::unordered_set<long long> Seen;
		int Left = INT_MAX;
		int Top = INT_MAX;
		int Right = INT_MIN;
		int Bottom = INT_MIN;
		long long LastUpdate = 0;
	};

	inline std::deque<PaintUndoStep> s_PaintUndo;

	inline bool s_RecordPaint = false; //!< While a paint or build stroke is applied (see Apply): only the player's own strokes are undone, not craters.

	constexpr size_t c_PaintUndoSteps = 20;

	constexpr size_t c_PaintUndoPixelsPerStep = 1000000; //!< About 3.5 s of the 40 px brush held down.

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
	};

	inline std::vector<PlacedEffect> s_Effects;

	/// A place water keeps pouring from until it's removed: a spring, a burst pipe, a tap left on.
	struct WaterSpawner {
		Vector Position;
		int Radius = 3; //!< How wide the pour is: air within this many pixels of the place is kept full of water.
		std::string Liquid = "Water"; //!< What it pours, by preset name: any liquid or powder FluidSim pours.
		float Rate = 1.0F; //!< How much of the time it pours, 0.05 to 1 (1 every update).
		float Due = 0.0F; //!< Rate summed since its last pour: it pours when this reaches 1.
		bool On = true; //!< Off, it stays where it is and pours nothing until turned on again.
	};

	inline std::vector<WaterSpawner> s_WaterSpawners;
	inline std::string s_SpringLiquid = "Water"; //!< What new springs and the tank pour (Paint > Springs).
	inline float s_SpringRate = 1.0F; //!< How much of the time new springs pour.
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

	inline int s_PlayerEnterPending = 0; //!< Updates left to wait for the character to be in the world before stepping into it; 0 when not waiting.

	/// One place a move order looks at, for the standing-spot reachability preview (SettingsMan::ShowSandboxSpotReach).
	struct SpotReach {
		Vector Spot;
		float Cost = -2.0F; //!< The leader's path cost to it; -1 no path, -2 not looked at (enough were reachable before it).
		bool Chosen = false; //!< A unit will be sent here.
	};

	inline std::deque<std::string> s_StrokeLog; //!< The last tool uses applied, oldest first, for the stroke log (SettingsMan::ShowSandboxStrokeLog).

	enum class Icon { Eye, Arrows, Target, Person, Cross, Flag, Jar, Gun, Wall, Down, Flame, Drop, Cloud, Grains, Chunk, Pick, Bomb, Rocket, Bolt, Star };

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
	void SpawnPuffs(const char* presetName, const Vector& position, int radius, int count);
	int PaintedColor(const Material* material, int x, int y, int color, int speckleColor);
	void RecordPaintPixel(const SLTerrain* terrain, int x, int y);
	void ClosePaintUndoStep(bool always);
	void UndoPaint();
	void NotePaint(const Box& area, const char* kind, const char* material, bool toldCollapse, bool toldLiquid, bool changed);
	void PaintTerrain(const Vector& center, int radius, const char* materialName);
	void PaintBox(const Vector& topLeft, int boxWidth, int boxHeight, const char* materialName);
	bool TakesSide(Tool kind);
	void ClearBox(const Vector& topLeft, int boxWidth, int boxHeight);
	void ClearMap(const Stroke& stroke);
	void ScanMapMaterials();
	void ClearMapPopup();
	void QueueSimChange(Tool kind, int count = 0);
	glm::vec3 Hue(float turn);
	void UpdateEffects();
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
	void AddNoRoute(Actor* unit, const Vector& destination);
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
	void PatrolSelected(const std::vector<Vector>& points, bool backAndForth);
	void DropPlan(const Actor* unit);
	void UpdatePlans();
	void DropPlanStep(long unitID, int step);
	Actor* EnemyNear(const Vector& point, int team);
	void OrderSelectedUnits(int choice, const Vector& point);
	int SelectedRule(bool weapons);
	int SelectedAIMode();
	void QueueRule(bool weapons, int rule);
	void FindAction();
	std::vector<const Preset*> FactionUnits(int moduleID);
	void UpdateBattle(bool aiPaused);
	void ApplyBattleStroke(const Stroke& stroke);
	void SendBattleSettings(int team, int command = BattleSet);
	void ForgetBattle();
	void UpdateBattleDefenders();
	void BattleTab();
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
	Vector ModeSpawnSpot(int side, const std::vector<Vector>& zone, const Actor* unit);
	int ModeRoom(int side, int room);
	void UpdateBattleMode(bool aiPaused);
	void ForgetBattleMode();
	void BattleModeTab();
	void DrawBattleMode();
	/// What a battle mode's tool in hand is for, in the mode chosen, by the pointer (as "Red flag", "Hill"); empty for the tool's own name.
	std::string BattleToolLabel(Tool kind);
	bool BattleModeChooser();
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
	int FindFavourite(Tool kind, const std::string& presetName);
	void SaveFavouritesFile();
	void LoadFavouritesFile();
	void ToggleFavourite(Tool kind, const std::string& presetName);
	void DrawFavouriteMark(ImDrawList* drawList, ImVec2 from);
	void DrawPinMark(ImDrawList* drawList, ImVec2 from, ImVec2 to);
	void ToolButtons(std::initializer_list<Tool> tools);
	void SideChooser();
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
	void DrawCursor();
	const PiecePicture& PictureOfFile(const std::string& path);
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
	void BarPlate();
	bool ContextRow();
	void DrawBar();
#pragma endregion

	/// One tile of the bar: a small picture drawn by the caller, lit when it is the one in use, its name as a tooltip.
	/// @return 1 if clicked, 2 if right-clicked, 0 otherwise.
	template <typename DrawPicture> int BarTile(const char* id, const char* tip, bool selected, DrawPicture drawPicture) {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		float pixel = ToolUI::Pixel();
		float dot = pixel * 1.5F; // One pixel of the picture.
		float picture = dot * 12.0F;
		float pad = pixel * 3.0F;
		ImVec2 size(picture + pad * 2.0F, picture + pad * 2.0F);
		ImVec2 at = ImGui::GetCursorScreenPos();
		int result = ImGui::InvisibleButton(id, size) ? 1 : 0;
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			result = 2;
		}
		bool hovered = ImGui::IsItemHovered();
		ImVec2 to(at.x + size.x, at.y + size.y);
		// A sunken socket; the one in use sits raised and gold-edged, the one under the pointer lightens.
		ImU32 well = selected ? ToolTheme::Panel : hovered ? ToolTheme::WellHover : ToolTheme::Well;
		drawList->AddRectFilled(at, to, well);
		if (selected) {
			drawList->AddRect(at, to, ToolTheme::Gold, 0.0F, 0, pixel);
			drawList->AddRectFilled(ImVec2(at.x + pixel, at.y + pixel), ImVec2(to.x - pixel, at.y + pixel * 2.0F), (ToolTheme::EdgeLight & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT));
		} else {
			drawList->AddRectFilled(at, ImVec2(to.x, at.y + pixel), ToolTheme::EdgeDark);
			drawList->AddRectFilled(at, ImVec2(at.x + pixel, to.y), ToolTheme::EdgeDark);
			drawList->AddRectFilled(ImVec2(at.x, to.y - pixel), to, (ToolTheme::Edge & 0x00FFFFFF) | (150u << IM_COL32_A_SHIFT));
		}
		drawPicture(drawList, ImVec2(at.x + pad, at.y + pad), picture);
		if (hovered && tip && *tip) {
			ImGui::SetTooltip("%s", tip);
		}
		return result;
	}
} // namespace SandboxDetail

using namespace SandboxDetail;
