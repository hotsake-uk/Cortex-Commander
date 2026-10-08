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
#include <cctype>
#include <cmath>
#include <execution>
#include <initializer_list>
#include <list>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace RTE;

bool Sandbox::s_Open = false;

std::deque<Actor*>& Sandbox::Actors() {
	return g_MovableMan.m_Actors;
}

std::deque<MovableObject*>& Sandbox::Items() {
	return g_MovableMan.m_Items;
}

namespace RTE {
	/// Lets the sandbox's helper functions reach its private parts.
	struct SandboxAccess {
		static std::deque<Actor*>& Actors() { return Sandbox::Actors(); }
		static std::deque<MovableObject*>& Items() { return Sandbox::Items(); }
	};
} // namespace RTE

namespace {
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
		ClearEffects //!< Count: 1 the last one only, else all.
	};

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
	    {Tool::WaterSpawner, "Water spawner", 0.0F, true},
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
	};
	constexpr int c_ToolCount = static_cast<int>(std::size(c_Tools));

	int ToolIndex(Tool kind) {
		for (int i = 0; i < c_ToolCount; ++i) {
			if (c_Tools[i].Kind == kind) {
				return i;
			}
		}
		return 0;
	}

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
		MoveTo
	};
	constexpr const char* c_OrderNames = "Hold position\0Attack nearest enemy\0Hunt brains\0Patrol\0Go to rally point\0Do nothing\0Dig for gold\0Move to a place\0";
	// The orders a unit can be made with (a barracks' trainees, a script's units): all but "Move to a place", which needs a place clicked.
	// (Offered all eight, a barracks told "Dig for gold" or "Move to a place" trained units that did nothing: the number was clamped to
	// "Do nothing" on the way in.)
	constexpr const char* c_UnitOrderNames = "Hold position\0Attack nearest enemy\0Hunt brains\0Patrol\0Go to rally point\0Do nothing\0Dig for gold\0";
	constexpr int c_LastUnitOrder = static_cast<int>(Order::DigGold);
	constexpr const char* c_AttackTag = "SandboxAttack"; //!< Number value on units told to attack, so they get a new target when theirs dies.
	constexpr const char* c_TargetTag = "SandboxTarget"; //!< Number value on units told to attack one enemy in particular: its unique ID. They keep after it while it lives.
	constexpr const char* c_AutoTargetTag = "SandboxAutoTarget"; //!< Number value on units told to attack the nearest enemy: the unique ID of the one picked for them, which isn't held to.
	constexpr const char* c_AttackXTag = "SandboxAttackX"; //!< Number values on units told to attack towards a place: they fight what is near it, and hold there otherwise.
	constexpr const char* c_AttackYTag = "SandboxAttackY";
	constexpr const char* c_DefendXTag = "SandboxDefendX"; //!< Number values on units told to defend a spot: they fight from it and go back to it when moved off.
	constexpr const char* c_DefendYTag = "SandboxDefendY";
	constexpr const char* c_HoldTag = "SandboxHold"; //!< Number value on units told to hold position: the AI neither wanders off (a hurt sentry patrols) nor falls back.
	constexpr const char* c_RetreatTag = "AIRetreat"; //!< Number values the Lua AI keeps on a unit falling back or working round a flank; taken off
	constexpr const char* c_FlankTag = "AIFlank";     //!< by a new order, which tells the AI the order it would put back after is gone.

	/// A new order ends a fall-back or a flank under way: the AI drops it without putting the old order back.
	void CancelRetreatAndFlank(Actor* unit) {
		unit->RemoveNumberValue(c_RetreatTag);
		unit->RemoveNumberValue(c_FlankTag);
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
		int Width = 0; //!< Structures: footprint, for the preview.
		int Height = 0;
		float OffsetX = 0.0F;
		float OffsetY = 0.0F;
	};

	/// Weapons a faction hands its units by default.
	struct FactionArmoury {
		std::vector<const Preset*> Primaries;
		std::vector<const Preset*> Secondaries;
		std::vector<const Preset*> Grenades;
	};

	// The kinds of bunker piece the Build tab offers, as the game's own build menu groups them.
	constexpr const char* c_StructureGroups[] = {"Bunker Modules", "Bunker Systems", "Bunker Lights", "Bunker Backgrounds", "Bunker Bits", "Bunker Clutter", "Bunker Odds & Ends", "Terrain Objects"};
	int s_StructureGroup = 0; //!< Which of them the Build tab is listing; one past the last for all.
	std::vector<Preset> s_Units;
	std::vector<Preset> s_Brains;
	std::vector<Preset> s_Items;
	std::vector<Preset> s_Structures;
	std::vector<std::pair<int, FactionArmoury>> s_Armouries; //!< By module ID.
	std::vector<const Preset*> s_Weapons; //!< Guns among the items, for loadouts.
	bool s_CatalogueBuilt = false;

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
		Vector Position2; //!< Selection box: the other corner.
		int Craft = 0; //!< Drops: index into c_Crafts.
		bool HasView = false; //!< Whether ViewMiddleX was taken, when the stroke was made on screen (not by a script).
		float ViewMiddleX = 0.0F; //!< The middle of the view across, at the click: spawned units face it. (Taken then, not read in the sim.)
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

	Actor* GetRef(const UnitRef& ref) { return ref.Unit && g_MovableMan.IsActor(ref.Unit) && ref.Unit->GetUniqueID() == ref.ID ? ref.Unit : nullptr; }

	UnitRef MakeRef(Actor* actor) { return {actor, actor ? actor->GetUniqueID() : 0}; }

	/// Whether a reference is to this very unit: its address and its unique ID. (By address alone, a unit that died and a new one made at the
	/// same address was taken for it: the new unit counted as selected, or had the dead one's pending order.)
	bool RefersTo(const UnitRef& ref, const Actor* actor) { return actor && ref.Unit == actor && ref.ID == static_cast<long>(actor->GetUniqueID()); }


	/// One side in an auto battle.
	struct AutoSide {
		bool Active = false;
		int Faction = 0; //!< Index into s_FactionModules.
		int Budget = 5000;
		float Spent = 0.0F;
		int Sent = 0;
		long long NextWave = 0;
		bool Broke = false; //!< Can't afford another unit.
	};

	int s_ToolIndex = 0;
	int s_Craft = 0;
	std::vector<UnitRef> s_Selected;
	UnitRef s_FollowTarget;
	bool s_FollowAction = false;
	Vector s_ActionSpot;
	bool s_ActionSpotValid = false;
	bool s_Dragging = false;
	bool s_DoubleClick = false; //!< The drag or click under way began with a double click.
	ImVec2 s_DragStart;
	std::array<AutoSide, 4> s_AutoSides;
	bool s_AutoRunning = false;
	int s_AutoWinner = -2; //!< -2 no result yet, -1 a draw, otherwise the winning side.
	Vector s_AutoCenter;
	float s_AutoLaneWidth = 0.0F; //!< The view's width when the auto battle began: the lanes the waves land in are spaced by it.
	std::vector<int> s_FactionModules;
	std::vector<std::string> s_FactionNames;
	int s_Radius = 6;
	int s_UnitChoice = 0;
	int s_BrainChoice = 0;
	int s_ItemChoice = 0;
	int s_StructureChoice = 0;
	/// The search boxes' text, one for each list: by the tool it lists for, a picture grid's apart from a plain list's. (One shared box filtered
	/// the Spawn, Build and Colony lists alike, so a search typed in one emptied the others.)
	std::map<int, std::array<char, 64>> s_Filters;
	char* FilterFor(Tool kind, bool pictures) { return s_Filters[static_cast<int>(kind) * 2 + (pictures ? 1 : 0)].data(); }
	int s_Team = 1;
	int s_Order = static_cast<int>(Order::Hold); //!< Units placed hold their position, firing back, until told otherwise.

	/// The order in hand, chosen from the orders a unit can be made with (c_UnitOrderNames), for the unit and barracks tools. The side orders share
	/// it: "Move to a place" chosen there shows here as "Hold position", which is what a unit placed with it does, and stays as it is unless a
	/// choice is made here. (Offered all eight, "Move to a place" was there to pick and did nothing a hold doesn't.)
	bool UnitOrderCombo(const char* label) {
		int choice = s_Order > c_LastUnitOrder ? static_cast<int>(Order::Hold) : s_Order;
		if (ImGui::Combo(label, &choice, c_UnitOrderNames)) {
			s_Order = choice;
			return true;
		}
		return false;
	}
	int s_Loadout = 0;
	int s_SquadSize = 1;
	bool s_LitGrenade = false;
	bool s_SnapToGrid = true;
	bool s_FreeCamera = false;
	bool s_SlowMotion = false;
	bool s_FreeCameraStarted = false;
	int s_CameraWarmupFrames = 0; //!< Frames to leave the camera alone at the start of a game, while the game mode points it somewhere sensible.
	Vector s_CameraCenter;
	float s_StrokeTimer = 0.0F;
	std::vector<Stroke> s_Queue;
	std::array<Vector, c_Sides> s_RallyPoints;
	std::array<bool, c_Sides> s_RallySet{};
	Actor* s_Possessed = nullptr; //!< The unit you're controlling in the god mode, checked with IsActor and its unique ID before use.
	long s_PossessedID = 0; //!< Its unique ID: a unit that died and a new one made at the same address passed the IsActor check alone.

	void SetPossessed(Actor* actor) {
		s_Possessed = actor;
		s_PossessedID = actor ? static_cast<long>(actor->GetUniqueID()) : 0;
	}

	/// Your character's own gib limits while it can't be hurt (0 is "never" for both), to put back when that is turned off.
	struct SavedGibLimits {
		long ID = 0;
		float Impulse = 0.0F;
		int Wounds = 0;
	};
	SavedGibLimits s_PlayerGibLimits;

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
	};
	PlayerSetup s_Player;
	constexpr bool c_ShowColonyTab = false; //!< Whether the sandbox window offers the colony buildings.
	/// What the command tool does with a click on the world.
	enum class CommandMode {
		Move, //!< Each selected unit to its own spot round the point; a click on an enemy attacks it, a click on a friend selects it.
		Attack, //!< Go for the nearest enemy to the point, or the point itself with orders to fight.
		Guard //!< Follow the friendly unit clicked and stay with it.
	};
	CommandMode s_CommandMode = CommandMode::Move;
	constexpr const char* c_CommandModeNames[] = {"Move", "Attack", "Guard"};
	float s_Spacing = 18.0F; //!< How far apart units stand when sent somewhere together.
	std::array<std::vector<UnitRef>, 10> s_Groups; //!< Control groups: Ctrl+number keeps the selection, the number alone brings it back.

	/// A mark left where an order was given, fading over a moment.
	struct OrderMark {
		Vector Position;
		float Life; //!< Seconds left.
		ImU32 Color;
	};
	std::vector<OrderMark> s_OrderMarks;
	// The gym's start and goal being made (see the Gym region).
	Vector s_GymFrom;
	Vector s_GymTo;
	bool s_GymFromSet = false;
	bool s_GymToSet = false;

	bool s_RingOpen = false; //!< A ring of choices is up, round where the right button went down.
	int s_RingPage = 0; //!< Which ring the command tool shows: 0 the basic commands while the button is held, 1 the native AI modes ("More"), 2 the basic ring held up until a click.
	ImVec2 s_RingCenter;
	Vector s_RingScenePoint; //!< Where in the world the right button went down, which the choice is about.
	int s_ColonyKeep = 4; //!< How many of its units a new barracks keeps alive.
	bool s_PauseInMenus = true; //!< In the Sandbox game mode the world stands still while the tools are open.
	bool s_PausedByMenus = false; //!< Whether it is this that has paused the simulation, so only this is undone.
	int s_StepsWanted = 0; //!< Updates to let the paused world do.
	size_t s_StrokesApplied = 0; //!< How many queued tool uses the last sim update applied, for the sim state readout.
	unsigned long long s_GodStartUpdate = 0; //!< The simulation update the Sandbox game started on: it runs a moment before it first pauses.
	float s_PlayHintSeconds = 0.0F; //!< How much longer the reminder of the keys shows after stepping into the character.
	bool s_Flying = false;
	int s_KitKeyPending = -1; //!< A kit number pressed last update, taken out this one (after the game's own weapon keys have had their say).
	bool s_GodViewSetUp = false; //!< Whether the god view's window and camera are set up for the Sandbox game under way.
	bool s_GodViewPending = false; //!< A game has started since (see Sandbox::OnActivityStarted): set them up afresh.
	constexpr unsigned int c_RandomSeed = 0x5A17B0Bu;
	unsigned int s_Random = c_RandomSeed;
	SoundContainer* s_Thunder = nullptr; //!< Never deleted: it would outlive the audio system at exit.

	float Random01() {
		s_Random ^= s_Random << 13;
		s_Random ^= s_Random >> 17;
		s_Random ^= s_Random << 5;
		return static_cast<float>(s_Random & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	const ToolInfo& CurrentTool() { return c_Tools[s_ToolIndex]; }

	bool InGame() {
		const Activity* activity = g_ActivityMan.GetActivity();
		return activity && activity->GetActivityState() >= Activity::ActivityState::Editing && activity->GetActivityState() <= Activity::ActivityState::Running && g_SceneMan.GetScene() && g_SceneMan.GetScene()->GetTerrain();
	}

	GameActivity* CurrentGame() { return dynamic_cast<GameActivity*>(g_ActivityMan.GetActivity()); }

	// Scene and window positions: shared with the debug overlays (DebugDraw.h), so they work outside the sandbox too.
	float ScenePixelsPerWindowPixel() { return DebugDraw::ScenePixelsPerWindowPixel(); }
	ImVec2 ViewOrigin() { return DebugDraw::ViewOrigin(); }
	Vector MouseScenePosition() { return DebugDraw::MouseScenePosition(); }

	bool ContainsIgnoringCase(const std::string& text, const char* filter) {
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

#pragma region Catalogue
	void AddPresets(std::vector<Preset>& list, const std::list<Entity*>& entities, bool buyableOnly, bool skipBrains, const char* group = "") {
		for (const Entity* entity: entities) {
			const SceneObject* object = dynamic_cast<const SceneObject*>(entity);
			if (!object || (buyableOnly && !object->IsBuyable()) || (skipBrains && object->IsInGroup("Brains"))) {
				continue;
			}
			Preset preset;
			preset.ClassName = entity->GetClassName();
			preset.PresetName = entity->GetPresetName();
			preset.ModuleID = entity->GetModuleID();
			preset.Module = g_PresetMan.GetDataModuleName(preset.ModuleID);
			std::string faction = preset.Module.substr(0, preset.Module.find(".rte"));
			preset.Label = preset.PresetName + "  (" + faction + ")";
			preset.Group = group;
			preset.Modded = !g_PresetMan.IsModuleOfficial(preset.Module);
			// The subcategory, from the groups the game files put the thing in.
			if (preset.ClassName == "AHuman") {
				preset.Kind = object->IsInGroup("Brains") ? "Brains" : "Infantry";
			} else if (preset.ClassName == "ACrab") {
				preset.Kind = object->IsInGroup("Turrets") ? "Turrets" : "Mecha";
			} else if (preset.ClassName == "HDFirearm") {
				preset.Kind = object->IsInGroup("Tools - Diggers") ? "Diggers" : (object->IsInGroup("Tools") ? "Tools" : (object->IsInGroup("Weapons - Secondary") ? "Secondary weapons" : (object->IsInGroup("Weapons - Explosive") ? "Explosive weapons" : "Primary weapons")));
			} else if (preset.ClassName == "TDExplosive") {
				preset.Kind = object->IsInGroup("Bombs - Grenades") ? "Grenades" : "Bombs";
			} else if (preset.ClassName == "HeldDevice") {
				preset.Kind = object->IsInGroup("Shields") ? "Shields" : "Other items";
			} else {
				preset.Kind = group;
			}
			if (const TerrainObject* terrainObject = dynamic_cast<const TerrainObject*>(entity)) {
				preset.Width = terrainObject->GetBitmapWidth();
				preset.Height = terrainObject->GetBitmapHeight();
				preset.OffsetX = terrainObject->GetBitmapOffset().m_X;
				preset.OffsetY = terrainObject->GetBitmapOffset().m_Y;
			}
			list.push_back(std::move(preset));
		}
	}

	void SortAndDedupe(std::vector<Preset>& list) {
		std::sort(list.begin(), list.end(), [](const Preset& a, const Preset& b) { return a.Label < b.Label; });
		list.erase(std::unique(list.begin(), list.end(), [](const Preset& a, const Preset& b) { return a.Label == b.Label && a.ClassName == b.ClassName; }), list.end());
	}

	FactionArmoury& ArmouryOf(int moduleID) {
		for (auto& [id, armoury]: s_Armouries) {
			if (id == moduleID) {
				return armoury;
			}
		}
		s_Armouries.emplace_back(moduleID, FactionArmoury());
		return s_Armouries.back().second;
	}

	void BuildCatalogue() {
		s_Units.clear();
		s_Brains.clear();
		s_Items.clear();
		s_Structures.clear();
		s_Armouries.clear();
		s_Weapons.clear();
		for (const char* type: {"AHuman", "ACrab"}) {
			std::list<Entity*> entities;
			g_PresetMan.GetAllOfType(entities, type);
			AddPresets(s_Units, entities, true, true);
		}
		{
			std::list<Entity*> entities;
			g_PresetMan.GetAllOfGroup(entities, "Brains", "Actor");
			AddPresets(s_Brains, entities, false, false);
		}
		for (const char* type: {"HDFirearm", "TDExplosive", "HeldDevice"}) {
			std::list<Entity*> entities;
			g_PresetMan.GetAllOfType(entities, type);
			AddPresets(s_Items, entities, true, false);
		}
		for (const char* group: c_StructureGroups) {
			std::list<Entity*> entities;
			g_PresetMan.GetAllOfGroup(entities, group, "All");
			AddPresets(s_Structures, entities, false, false, group);
		}
		SortAndDedupe(s_Units);
		SortAndDedupe(s_Brains);
		SortAndDedupe(s_Items);
		SortAndDedupe(s_Structures);
		// What each faction arms its units with.
		for (const Preset& item: s_Items) {
			const Entity* entity = g_PresetMan.GetEntityPreset(item.ClassName, item.PresetName, item.ModuleID);
			if (!entity) {
				continue;
			}
			if (item.ClassName == "HDFirearm") {
				s_Weapons.push_back(&item);
			}
			FactionArmoury& armoury = ArmouryOf(item.ModuleID);
			if (entity->IsInGroup("Weapons - Primary")) {
				armoury.Primaries.push_back(&item);
			} else if (entity->IsInGroup("Weapons - Secondary")) {
				armoury.Secondaries.push_back(&item);
			} else if (entity->IsInGroup("Bombs - Grenades")) {
				// Bandoliers unpack themselves by deleting themselves when held; hand out plain grenades instead, so battlefields aren't strewn with them.
				const MovableObject* grenade = dynamic_cast<const MovableObject*>(entity);
				if (grenade && !grenade->StringValueExists("GrenadeName")) {
					armoury.Grenades.push_back(&item);
				}
			}
		}
		auto preferredIndex = [](const std::vector<Preset>& list, const char* name) {
			for (size_t i = 0; i < list.size(); ++i) {
				if (list[i].PresetName == name) {
					return static_cast<int>(i);
				}
			}
			return 0;
		};
		s_FactionModules.clear();
		s_FactionNames.clear();
		for (const Preset& unit: s_Units) {
			if (std::find(s_FactionModules.begin(), s_FactionModules.end(), unit.ModuleID) == s_FactionModules.end()) {
				s_FactionModules.push_back(unit.ModuleID);
				s_FactionNames.push_back(unit.Module.substr(0, unit.Module.find(".rte")));
			}
		}
		for (size_t side = 0; side < s_AutoSides.size(); ++side) {
			s_AutoSides[side].Faction = std::min(static_cast<int>(side), static_cast<int>(s_FactionModules.size()) - 1);
		}
		s_UnitChoice = preferredIndex(s_Units, "Soldier Light");
		s_BrainChoice = preferredIndex(s_Brains, "Brain Case");
		s_ItemChoice = preferredIndex(s_Items, "Frag Grenade");
		s_CatalogueBuilt = true;
	}

	const std::vector<Preset>& ListFor(Tool kind) {
		switch (kind) {
			case Tool::Brain:
				return s_Brains;
			case Tool::Item:
				return s_Items;
			case Tool::Structure:
				return s_Structures;
			default:
				return s_Units;
		}
	}

	int s_EffectChoice = 0;

	int& ChoiceFor(Tool kind) {
		switch (kind) {
			case Tool::Effect:
				return s_EffectChoice;
			case Tool::Brain:
				return s_BrainChoice;
			case Tool::Item:
				return s_ItemChoice;
			case Tool::Structure:
				return s_StructureChoice;
			default:
				return s_UnitChoice;
		}
	}

	const Preset* ChosenPreset(Tool kind, int choice) {
		const std::vector<Preset>& list = ListFor(kind);
		return choice >= 0 && choice < static_cast<int>(list.size()) ? &list[choice] : nullptr;
	}
#pragma endregion

#pragma region World helpers
	MovableObject* CreateObject(const std::string& className, const std::string& presetName, int moduleID) {
		const Entity* preset = g_PresetMan.GetEntityPreset(className, presetName, moduleID);
		return preset ? dynamic_cast<MovableObject*>(preset->Clone()) : nullptr;
	}

	MovableObject* CreateBaseObject(const char* className, const char* presetName) {
		const Entity* preset = g_PresetMan.GetEntityPreset(className, presetName, "Base.rte");
		return preset ? dynamic_cast<MovableObject*>(preset->Clone()) : nullptr;
	}

	void AddObject(MovableObject* object) {
		if (Actor* actor = dynamic_cast<Actor*>(object)) {
			g_MovableMan.AddActor(actor);
		} else if (HeldDevice* device = dynamic_cast<HeldDevice*>(object)) {
			g_MovableMan.AddItem(device);
		} else {
			g_MovableMan.AddParticle(object);
		}
	}

	/// Whether an actor is a real unit on a side (not a door or other neutral scenery).
	bool IsCombatant(const Actor* actor) {
		return actor && actor->GetTeam() >= 0 && actor->GetTeam() < c_Sides && !actor->IsDead() && !dynamic_cast<const ADoor*>(actor) && actor->GetHealth() > 0.0F;
	}

	/// Whether a unit can be selected and commanded: a combatant on a side, not a brain, not a craft (a ship is ordered by its own AI; sent off
	/// with the squad, a dropship delivering hovered with its passengers inside).
	bool IsSelectable(const Actor* actor) {
		return IsCombatant(actor) && !actor->IsInGroup("Brains") && !dynamic_cast<const ACraft*>(actor);
	}

	/// Enemies a unit sent at the nearest one gave up on (no way to them): the unit's unique ID to the enemy's and when, so the next pick is
	/// another for a while, not the same one again every second.
	std::unordered_map<long, std::pair<long, double>> s_GaveUpOn;
	constexpr double c_GaveUpOnMS = 20000.0;

	/// The nearest enemy to send a unit told to attack at: not craft (a ship overhead is no place to walk to), and a brain only when nothing
	/// else is left (one in a sealed bunker drew every unit to the bunker's wall), nor one the unit lately had no way to.
	Actor* NearestEnemy(const Actor* of) {
		long gaveUpOn = 0;
		if (auto it = s_GaveUpOn.find(static_cast<long>(of->GetUniqueID())); it != s_GaveUpOn.end()) {
			if (g_TimerMan.GetSimTimeMS() - it->second.second < c_GaveUpOnMS) {
				gaveUpOn = it->second.first;
			} else {
				s_GaveUpOn.erase(it);
			}
		}
		Actor* nearest = nullptr;
		float nearestDistance = 0.0F;
		bool nearestIsBrain = false;
		for (Actor* actor: SandboxAccess::Actors()) {
			if (actor == of || !IsCombatant(actor) || actor->GetTeam() == of->GetTeam() || actor->IsIgnoredByAI() || dynamic_cast<const ACraft*>(actor) || (gaveUpOn != 0 && static_cast<long>(actor->GetUniqueID()) == gaveUpOn)) {
				continue;
			}
			bool brain = actor->IsInGroup("Brains");
			float distance = g_SceneMan.ShortestDistance(of->GetPos(), actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
			if (!nearest || (nearestIsBrain && !brain) || (brain == nearestIsBrain && distance < nearestDistance)) {
				nearest = actor;
				nearestDistance = distance;
				nearestIsBrain = brain;
			}
		}
		return nearest;
	}

	/// The actor (or, failing that, loose item) closest to a point, within reach.
	MovableObject* ObjectUnder(const Vector& position, bool actorsOnly) {
		MovableObject* found = nullptr;
		float foundDistance = 24.0F * 24.0F;
		for (Actor* actor: SandboxAccess::Actors()) {
			float distance = g_SceneMan.ShortestDistance(position, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
			if (distance < foundDistance) {
				found = actor;
				foundDistance = distance;
			}
		}
		if (!found && !actorsOnly) {
			foundDistance = 16.0F * 16.0F;
			for (MovableObject* item: SandboxAccess::Items()) {
				float distance = g_SceneMan.ShortestDistance(position, item->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
				if (distance < foundDistance) {
					found = item;
					foundDistance = distance;
				}
			}
		}
		return found;
	}

	/// An order to a unit that is put into effect on the next update: the AI only looks at a new destination when its mode changes, so the unit is dropped out
	/// of GOTO for one update and put back into it with the new waypoint. Orders given straight would be ignored by a unit already going somewhere.
	struct PendingOrder {
		UnitRef Unit;
		Vector Waypoint;
		Actor* Target = nullptr; //!< An enemy to go for instead of a place.
		long TargetID = 0;
		bool Attack = false; //!< Keep attacking (a new target when this one dies).
		std::vector<Vector> Then; //!< Further places to go on to, in order (shift-clicks).
	};
	std::vector<PendingOrder> s_PendingOrders;

	/// Why and when a unit was last sent somewhere, for the sandbox orders overlay: kept by unique ID, the dead pruned when the list grows.
	struct SendNote {
		const char* Reason = "";
		bool Resend = false; //!< Sent again by the standing orders (ReturnDefenders, RetargetAttackers), not by anyone's click.
		long long At = 0; //!< The sim update it was sent on.
	};
	std::unordered_map<long, SendNote> s_SendNotes;

	/// Sends a unit to a place, or after a unit, from the next update (see PendingOrder).
	/// @param reason Why, in a few words, for the orders overlay.
	/// @param lock Whether the unit keeps after this enemy while it lives (an enemy picked by the player), rather than being free to fight
	/// what it meets on the way (one picked for it).
	/// @param resend Whether the standing orders are sending it again.
	void SendUnit(Actor* unit, const Vector& waypoint, Actor* target, bool attack, const char* reason, bool lock = false, bool resend = false) {
		// (Never a craft: set to GOTO, a dropship delivering stopped its unload, which only runs in STAY or DELIVER, and hovered with the
		// squad inside.)
		if (dynamic_cast<const ACraft*>(unit)) {
			return;
		}
		if (s_SendNotes.size() > 512) {
			std::unordered_set<long> alive;
			for (const Actor* actor: SandboxAccess::Actors()) {
				alive.insert(actor->GetUniqueID());
			}
			for (auto note = s_SendNotes.begin(); note != s_SendNotes.end();) {
				note = alive.count(note->first) ? std::next(note) : s_SendNotes.erase(note);
			}
		}
		s_SendNotes[unit->GetUniqueID()] = {reason, resend, g_TimerMan.GetSimUpdateCount()};
		CancelRetreatAndFlank(unit);
		unit->RemoveNumberValue(c_AttackTag);
		unit->RemoveNumberValue(c_DefendXTag);
		unit->RemoveNumberValue(c_DefendYTag);
		unit->RemoveNumberValue(c_AutoTargetTag);
		unit->RemoveNumberValue(c_HoldTag);
		if (attack && target && lock) {
			unit->SetNumberValue(c_TargetTag, static_cast<double>(target->GetUniqueID()));
		} else {
			unit->RemoveNumberValue(c_TargetTag);
		}
		if (!attack) {
			unit->RemoveNumberValue(c_AttackXTag);
			unit->RemoveNumberValue(c_AttackYTag);
		}
		unit->ClearAIWaypoints();
		unit->SetAIMode(Actor::AIMODE_SENTRY);
		// An earlier order still waiting is dropped.
		s_PendingOrders.erase(std::remove_if(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); }), s_PendingOrders.end());
		s_PendingOrders.push_back({MakeRef(unit), waypoint, target, target ? static_cast<long>(target->GetUniqueID()) : 0, attack});
	}

	/// Holds a unit where it is, forgetting every order it had.
	void HoldUnit(Actor* unit) {
		CancelRetreatAndFlank(unit);
		unit->RemoveNumberValue(c_AttackTag);
		unit->RemoveNumberValue(c_TargetTag);
		unit->RemoveNumberValue(c_AutoTargetTag);
		unit->RemoveNumberValue(c_AttackXTag);
		unit->RemoveNumberValue(c_AttackYTag);
		unit->RemoveNumberValue(c_DefendXTag);
		unit->RemoveNumberValue(c_DefendYTag);
		unit->RemoveNumberValue(c_HoldTag);
		unit->ClearAIWaypoints();
		unit->SetAIMode(Actor::AIMODE_SENTRY);
		s_PendingOrders.erase(std::remove_if(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); }), s_PendingOrders.end());
	}

	Actor* ActorWithID(long id) {
		if (id == 0) {
			return nullptr;
		}
		for (Actor* actor: SandboxAccess::Actors()) {
			if (static_cast<long>(actor->GetUniqueID()) == id) {
				return actor;
			}
		}
		return nullptr;
	}

	/// The nearest enemy of a side to a point within a reach, or none: a brain there only when nothing else of the enemy's is, as NearestEnemy.
	/// (Brains were left out altogether, so an attack ordered on an enemy brain's bunker walked up to it and stood there.)
	Actor* NearestEnemyTo(const Vector& point, int team, float reach) {
		Actor* nearest = nullptr;
		float best = reach * reach;
		bool nearestIsBrain = false;
		for (Actor* actor: SandboxAccess::Actors()) {
			if (!IsCombatant(actor) || actor->IsIgnoredByAI() || actor->GetTeam() == team) {
				continue;
			}
			bool brain = actor->IsInGroup("Brains");
			float distance = g_SceneMan.ShortestDistance(point, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
			if (distance >= reach * reach) {
				continue;
			}
			if (!nearest || (nearestIsBrain && !brain) || (brain == nearestIsBrain && distance < best)) {
				best = distance;
				nearest = actor;
				nearestIsBrain = brain;
			}
		}
		return nearest;
	}

	void ApplyPendingOrders() {
		std::vector<PendingOrder> orders;
		orders.swap(s_PendingOrders);
		for (const PendingOrder& order: orders) {
			Actor* unit = GetRef(order.Unit);
			if (!unit) {
				continue;
			}
			unit->ClearAIWaypoints();
			if (order.Target && g_MovableMan.IsActor(order.Target) && static_cast<long>(order.Target->GetUniqueID()) == order.TargetID) {
				unit->AddAIMOWaypoint(order.Target);
			} else {
				unit->AddAISceneWaypoint(order.Waypoint);
			}
			for (const Vector& then: order.Then) {
				unit->AddAISceneWaypoint(then);
			}
			unit->SetAIMode(Actor::AIMODE_GOTO);
			if (g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Sandbox)) {
				g_ConsoleMan.PrintString("SANDBOX: " + unit->GetPresetName() + " sent to " + std::to_string(static_cast<int>(order.Waypoint.m_X)) + "," + std::to_string(static_cast<int>(order.Waypoint.m_Y)) + (order.Target ? " after " + order.Target->GetPresetName() : "") + " mode now " + std::to_string(unit->GetAIMode()));
			}
			if (order.Attack) {
				unit->SetNumberValue(c_AttackTag, 1.0);
			}
		}
	}

	void GiveOrder(Actor* actor, Order order) {
		if (!actor || dynamic_cast<ADoor*>(actor) || dynamic_cast<const ACraft*>(actor) || actor->IsInGroup("Brains")) {
			return;
		}
		CancelRetreatAndFlank(actor);
		// Every earlier order's tags go, as HoldUnit does: a defender told to patrol was dragged back to its post every second by
		// ReturnDefenders, and to the AI ("defend") never closed in, flanked or fell back; an old target or attack-place pulled it there.
		actor->RemoveNumberValue(c_AttackTag);
		actor->RemoveNumberValue(c_TargetTag);
		actor->RemoveNumberValue(c_AutoTargetTag);
		actor->RemoveNumberValue(c_AttackXTag);
		actor->RemoveNumberValue(c_AttackYTag);
		actor->RemoveNumberValue(c_DefendXTag);
		actor->RemoveNumberValue(c_DefendYTag);
		actor->RemoveNumberValue(c_HoldTag);
		// And the old order's way there, queued or still to be applied: a unit told to hold (or patrol, hunt or idle) kept its waypoints, and
		// anything that later put a GOTO back (a fall-back's RestoreOrder, the AI's own new-order check) walked it off along them.
		actor->ClearAIWaypoints();
		s_PendingOrders.erase(std::remove_if(s_PendingOrders.begin(), s_PendingOrders.end(), [actor](const PendingOrder& pending) { return RefersTo(pending.Unit, actor); }), s_PendingOrders.end());
		switch (order) {
			case Order::Attack:
				// The nearest enemy is where it is sent, not one it has to keep after: on the way the AI fights whatever it meets, and the
				// unit is only sent again when it has nothing to go for. (Held to the pick, as it was, the unit was pulled back to it every
				// second from whatever it had stopped to fight, which ended that fight each time.)
				if (Actor* enemy = NearestEnemy(actor)) {
					SendUnit(actor, enemy->GetPos(), enemy, true, "attack order");
					actor->SetNumberValue(c_AutoTargetTag, static_cast<double>(enemy->GetUniqueID()));
				} else {
					actor->SetNumberValue(c_AttackTag, 1.0);
					actor->ClearAIWaypoints();
					actor->SetAIMode(Actor::AIMODE_SENTRY);
				}
				break;
			case Order::HuntBrains:
				actor->SetAIMode(Actor::AIMODE_BRAINHUNT);
				break;
			case Order::Patrol:
				actor->SetAIMode(Actor::AIMODE_PATROL);
				break;
			case Order::Rally:
				if (int team = actor->GetTeam(); team >= 0 && team < c_Sides && s_RallySet[team]) {
					SendUnit(actor, s_RallyPoints[team], nullptr, false, "to the rally point");
				} else {
					actor->ClearAIWaypoints();
					actor->SetAIMode(Actor::AIMODE_SENTRY);
				}
				break;
			case Order::Idle:
				actor->SetAIMode(Actor::AIMODE_NONE);
				break;
			case Order::DigGold:
				actor->ClearAIWaypoints();
				actor->SetAIMode(Actor::AIMODE_GOLDDIG);
				break;
			case Order::Hold:
				actor->SetAIMode(Actor::AIMODE_SENTRY);
				actor->SetNumberValue(c_HoldTag, 1.0);
				break;
			default:
				actor->SetAIMode(Actor::AIMODE_SENTRY);
				break;
		}
	}

	/// Units told to defend a spot go back to it when they've been moved off it (shoved, blown, or drawn after an enemy), and stand guard there again.
	void ReturnDefenders() {
		for (Actor* actor: SandboxAccess::Actors()) {
			if (!actor->NumberValueExists(c_DefendXTag) || actor->IsPlayerControlled() || !IsCombatant(actor)) {
				continue;
			}
			Vector post(static_cast<float>(actor->GetNumberValue(c_DefendXTag)), static_cast<float>(actor->GetNumberValue(c_DefendYTag)));
			float off = g_SceneMan.ShortestDistance(actor->GetPos(), post, g_SceneMan.SceneWrapsX()).GetMagnitude();
			if (actor->GetAIMode() == Actor::AIMODE_GOTO) {
				// On the way back: once there, guard again.
				if (off < 30.0F) {
					actor->ClearAIWaypoints();
					actor->SetAIMode(Actor::AIMODE_SENTRY);
				}
			} else if (off > 60.0F) {
				double x = post.m_X;
				double y = post.m_Y;
				SendUnit(actor, post, nullptr, false, "back to its post", false, true);
				actor->SetNumberValue(c_DefendXTag, x);
				actor->SetNumberValue(c_DefendYTag, y);
			}
		}
	}

	/// Units told to attack get a new target when theirs is gone, and go on guard when no enemies are left.
	void RetargetAttackers() {
		for (Actor* actor: SandboxAccess::Actors()) {
			// (A unit falling back hurt or working round a flank is left to it; the AI puts its order back after.)
			if (actor->GetNumberValue(c_AttackTag) <= 0.0 || actor->IsPlayerControlled() || !IsCombatant(actor) || actor->NumberValueExists("OnFire") || actor->NumberValueExists("AIRetreat") || actor->NumberValueExists("AIFlank")) {
				continue;
			}
			// (Nor one with an order about to take: between being sent and the order taking it is after nothing.)
			if (std::any_of(s_PendingOrders.begin(), s_PendingOrders.end(), [actor](const PendingOrder& order) { return RefersTo(order.Unit, actor); })) {
				continue;
			}
			const MovableObject* target = actor->GetMOMoveTarget();
			const Actor* targetActor = target && g_MovableMan.ValidMO(target) ? dynamic_cast<const Actor*>(target) : nullptr;
			// (Any enemy: the one it was sent at, or one the AI went after itself, in whatever mode its attack runs.)
			bool chasingEnemy = targetActor && IsCombatant(targetActor) && targetActor->GetTeam() != actor->GetTeam();
			// An enemy chosen for it is kept after while it lives, whatever else is about.
			if (Actor* chosen = ActorWithID(static_cast<long>(actor->GetNumberValue(c_TargetTag))); chosen && IsCombatant(chosen) && chosen->GetTeam() != actor->GetTeam()) {
				if (!chasingEnemy || targetActor != chosen) {
					bool towardsPlace = actor->NumberValueExists(c_AttackXTag);
					double x = actor->GetNumberValue(c_AttackXTag);
					double y = actor->GetNumberValue(c_AttackYTag);
					SendUnit(actor, chosen->GetPos(), chosen, true, "after its target", true, true);
					if (towardsPlace) {
						actor->SetNumberValue(c_AttackXTag, x);
						actor->SetNumberValue(c_AttackYTag, y);
					}
				}
				continue;
			}
			actor->RemoveNumberValue(c_TargetTag);
			if (chasingEnemy) {
				continue;
			}
			// Told to attack towards a place: the nearest enemy to it, else go there and stand ready.
			if (actor->NumberValueExists(c_AttackXTag)) {
				Vector place(static_cast<float>(actor->GetNumberValue(c_AttackXTag)), static_cast<float>(actor->GetNumberValue(c_AttackYTag)));
				double x = place.m_X;
				double y = place.m_Y;
				if (Actor* enemy = NearestEnemyTo(place, actor->GetTeam(), 500.0F)) {
					SendUnit(actor, enemy->GetPos(), enemy, true, "enemy near its place", false, true);
				} else if (!g_SceneMan.ShortestDistance(actor->GetPos(), place, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(60.0F) && actor->GetAIMode() != Actor::AIMODE_GOTO) {
					SendUnit(actor, place, nullptr, true, "back to its place", false, true);
				} else {
					continue;
				}
				actor->SetNumberValue(c_AttackXTag, x);
				actor->SetNumberValue(c_AttackYTag, y);
				continue;
			}
			// Not after anything: the enemy picked for it last time, still there, is one it had no way to (its route came back impossible and
			// the order was dropped), so it is given a rest from that one.
			// (Only with no waypoint left: an order applied this same update has its MO waypoint queued but not yet loaded as the move
			// target, so a unit just sent reads as after nothing. A stand-down on an impossible route clears the waypoints, whatever the
			// mode is left at.)
			if (Actor* picked = ActorWithID(static_cast<long>(actor->GetNumberValue(c_AutoTargetTag))); picked && IsCombatant(picked) && actor->GetWaypointsSize() == 0) {
				s_GaveUpOn[static_cast<long>(actor->GetUniqueID())] = {static_cast<long>(picked->GetUniqueID()), g_TimerMan.GetSimTimeMS()};
			}
			GiveOrder(actor, Order::Attack);
		}
	}

	void ActivateSide(int team) {
		if (Activity* activity = g_ActivityMan.GetActivity(); activity && team >= 0 && team < c_Sides) {
			activity->ForceSetTeamAsActive(team);
		}
	}
#pragma endregion

#pragma region Actions
	void Detonate(const char* presetName, const Vector& position) {
		if (MovableObject* object = CreateBaseObject("TDExplosive", presetName)) {
			object->SetPos(position);
			MOSRotating* explosive = dynamic_cast<MOSRotating*>(object);
			AddObject(object);
			if (explosive) {
				explosive->GibThis();
			}
		}
	}

	void SpawnPuffs(const char* presetName, const Vector& position, int radius, int count) {
		for (int i = 0; i < count; ++i) {
			if (MovableObject* puff = CreateBaseObject("MOSParticle", presetName)) {
				puff->SetPos(position + Vector((Random01() - 0.5F) * 2.0F * static_cast<float>(radius), (Random01() - 0.5F) * 2.0F * static_cast<float>(radius)));
				puff->SetVel(Vector((Random01() - 0.5F) * 2.0F, (Random01() - 0.5F) * 2.0F));
				g_MovableMan.AddParticle(puff);
			}
		}
	}

	/// The foreground colour painted ground gets at a scene pixel: the material's own terrain texture, tiled across the scene the way generated terrain is, so painted ground matches the real thing. A material without a texture gets its flat colour with a darker speckle.
	int PaintedColor(const Material* material, int x, int y, int color, int speckleColor) {
		if (BITMAP* texture = material ? material->GetFGTexture() : nullptr; texture && texture->w > 0 && texture->h > 0 && bitmap_color_depth(texture) == 8) {
			int texel = _getpixel(texture, x % texture->w, y % texture->h);
			if (texel != ColorKeys::g_MaskColor) {
				return texel;
			}
		}
		return Random01() < 0.25F ? speckleColor : color;
	}

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
	std::deque<PaintRecord> s_PaintRecords;

	void NotePaint(const Box& area, const char* kind, const char* material, bool toldCollapse, bool toldLiquid, bool changed) {
		if (!g_SettingsMan.ShowSandboxPaintAudit()) {
			s_PaintRecords.clear();
			return;
		}
		s_PaintRecords.push_back({area, kind, material ? material : "air", toldCollapse, toldLiquid, changed, g_TimerMan.GetSimUpdateCount()});
		while (s_PaintRecords.size() > 24) {
			s_PaintRecords.pop_front();
		}
	}

	/// Paints a disc of terrain material into the air, or digs one out when there's no material.
	void PaintTerrain(const Vector& center, int radius, const char* materialName) {
		SLTerrain* terrain = g_SceneMan.GetScene()->GetTerrain();
		int width = terrain->GetBitmap()->w;
		int height = terrain->GetBitmap()->h;
		int material = g_MaterialAir;
		const Material* paintMaterial = nullptr;
		int color = ColorKeys::g_MaskColor;
		int speckleColor = color;
		if (materialName) {
			const Material* found = g_SceneMan.GetMaterial(materialName);
			if (!found || found->GetIndex() == g_MaterialAir) {
				return;
			}
			material = found->GetIndex();
			paintMaterial = found;
			Color materialColor = found->GetColor();
			materialColor.RecalculateIndex();
			color = materialColor.GetIndex();
			Color darker = materialColor;
			darker.SetRGB(materialColor.GetR() * 4 / 5, materialColor.GetG() * 4 / 5, materialColor.GetB() * 4 / 5);
			darker.RecalculateIndex();
			speckleColor = darker.GetIndex() > 1 ? darker.GetIndex() : color;
		}
		int centerX = center.GetFloorIntX();
		int centerY = center.GetFloorIntY();
		if (!materialName) {
			// Dug-out ground may be left hanging. Told before the digging, so it knows what was hanging already.
			TerrainCollapse::BeginChange(center, static_cast<float>(radius + 30));
		}
		bool changed = false;
		for (int dy = -radius; dy <= radius; ++dy) {
			for (int dx = -radius; dx <= radius; ++dx) {
				if (dx * dx + dy * dy > radius * radius) {
					continue;
				}
				int x = centerX + dx;
				int y = centerY + dy;
				if (g_SceneMan.SceneWrapsX()) {
					x = ((x % width) + width) % width;
				}
				if (x < 0 || y < 0 || x >= width || y >= height) {
					continue;
				}
				int existing = terrain->GetMaterialPixel(x, y);
				// Painting only fills air; digging removes anything but the indestructible edge of the world.
				if (materialName ? existing != g_MaterialAir : (existing == g_MaterialAir || existing == g_MaterialOutOfBounds)) {
					continue;
				}
				terrain->SetMaterialPixel(x, y, material);
				terrain->SetFGColorPixel(x, y, materialName ? PaintedColor(paintMaterial, x, y, color, speckleColor) : color);
				changed = true;
			}
		}
		if (changed) {
			terrain->AddUpdatedMaterialArea(Box(Vector(static_cast<float>(centerX - radius), static_cast<float>(centerY - radius)), static_cast<float>(radius * 2 + 1), static_cast<float>(radius * 2 + 1)));
			// Liquid around the change may flow into it, and dug-out ground may be left hanging.
			FluidSim::Disturb(center, static_cast<float>(radius + 2));

		}
		NotePaint(Box(Vector(static_cast<float>(centerX - radius), static_cast<float>(centerY - radius)), static_cast<float>(radius * 2 + 1), static_cast<float>(radius * 2 + 1)), materialName ? "paint" : "dig", materialName, !materialName, changed, changed);
	}

	/// Fills a box with a terrain material, where there's air (or everything, to build over what's there).
	void PaintBox(const Vector& topLeft, int boxWidth, int boxHeight, const char* materialName) {
		SLTerrain* terrain = g_SceneMan.GetScene()->GetTerrain();
		int width = terrain->GetBitmap()->w;
		int height = terrain->GetBitmap()->h;
		const Material* found = g_SceneMan.GetMaterial(materialName);
		if (!found || found->GetIndex() == g_MaterialAir) {
			return;
		}
		Color materialColor = found->GetColor();
		materialColor.RecalculateIndex();
		int color = materialColor.GetIndex();
		Color darker = materialColor;
		darker.SetRGB(materialColor.GetR() * 4 / 5, materialColor.GetG() * 4 / 5, materialColor.GetB() * 4 / 5);
		darker.RecalculateIndex();
		int speckleColor = darker.GetIndex() > 1 ? darker.GetIndex() : color;
		int left = topLeft.GetFloorIntX();
		int top = topLeft.GetFloorIntY();
		for (int dy = 0; dy < boxHeight; ++dy) {
			for (int dx = 0; dx < boxWidth; ++dx) {
				int x = left + dx;
				int y = top + dy;
				if (g_SceneMan.SceneWrapsX()) {
					x = ((x % width) + width) % width;
				}
				if (x < 0 || y < 0 || x >= width || y >= height || terrain->GetMaterialPixel(x, y) != g_MaterialAir) {
					continue;
				}
				terrain->SetMaterialPixel(x, y, found->GetIndex());
				terrain->SetFGColorPixel(x, y, PaintedColor(found, x, y, color, speckleColor));
			}
		}
		terrain->AddUpdatedMaterialArea(Box(topLeft, static_cast<float>(boxWidth), static_cast<float>(boxHeight)));
		// Liquid round it takes the new shape (as PaintTerrain's): a box built into a stream was dry, the water left standing where it had been.
		FluidSim::Disturb(topLeft + Vector(static_cast<float>(boxWidth) * 0.5F, static_cast<float>(boxHeight) * 0.5F), static_cast<float>(std::max(boxWidth, boxHeight)) * 0.75F + 2.0F);
		NotePaint(Box(topLeft, static_cast<float>(boxWidth), static_cast<float>(boxHeight)), "fill box", materialName, false, true, true);
	}

	/// Whether what a tool makes belongs to a side, so the side is shown with it and the ring of sides is offered.
	bool TakesSide(Tool kind) {
		return kind == Tool::Unit || kind == Tool::Drop || kind == Tool::Brain || kind == Tool::RallyPoint || kind == Tool::Structure || kind == Tool::Barracks || kind == Tool::Extractor || kind == Tool::OrderMove;
	}

	/// Clears a box of the terrain to air.
	void ClearBox(const Vector& topLeft, int boxWidth, int boxHeight) {
		SLTerrain* terrain = g_SceneMan.GetScene()->GetTerrain();
		int width = terrain->GetBitmap()->w;
		int height = terrain->GetBitmap()->h;
		int left = topLeft.GetFloorIntX();
		int top = topLeft.GetFloorIntY();
		TerrainCollapse::BeginChange(topLeft + Vector(static_cast<float>(boxWidth) * 0.5F, static_cast<float>(boxHeight) * 0.5F), static_cast<float>(std::max(boxWidth, boxHeight)) * 0.75F + 30.0F);
		for (int dy = 0; dy < boxHeight; ++dy) {
			for (int dx = 0; dx < boxWidth; ++dx) {
				int x = left + dx;
				int y = top + dy;
				if (g_SceneMan.SceneWrapsX()) {
					x = ((x % width) + width) % width;
				}
				if (x < 0 || y < 0 || x >= width || y >= height) {
					continue;
				}
				int existing = terrain->GetMaterialPixel(x, y);
				if (existing == g_MaterialAir || existing == g_MaterialOutOfBounds) {
					continue;
				}
				terrain->SetMaterialPixel(x, y, g_MaterialAir);
				terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
			}
		}
		terrain->AddUpdatedMaterialArea(Box(topLeft, static_cast<float>(boxWidth), static_cast<float>(boxHeight)));
		// Liquid above or beside the cleared box flows into it (as PaintTerrain's dig does); it stayed put until something else woke it.
		FluidSim::Disturb(topLeft + Vector(static_cast<float>(boxWidth) * 0.5F, static_cast<float>(boxHeight) * 0.5F), static_cast<float>(std::max(boxWidth, boxHeight)) * 0.75F + 2.0F);
		NotePaint(Box(topLeft, static_cast<float>(boxWidth), static_cast<float>(boxHeight)), "clear box", nullptr, true, true, true);
	}

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
	std::vector<PlacedEffect> s_Effects;

	void StrikeLightning(const Vector& target);
	bool GymRunCourse(int index);
	bool GymRunAll();
	void GymRemoveUnits();

	/// Queues a change the window asks for, to be made in the next simulation update like a click on the world. (Made from the window
	/// directly, gym units appeared with no sim step and their timers started on the spot, and the effect and spring lists were cleared
	/// under the update that walks them.)
	void QueueSimChange(Tool kind, int count = 0) {
		Stroke stroke;
		stroke.Kind = kind;
		stroke.Count = count;
		s_Queue.push_back(stroke);
	}

	glm::vec3 Hue(float turn) {
		turn -= std::floor(turn);
		return glm::vec3(255.0F) * glm::clamp(glm::abs(glm::fract(glm::vec3(turn) + glm::vec3(0.0F, 2.0F / 3.0F, 1.0F / 3.0F)) * 6.0F - 3.0F) - 1.0F, 0.0F, 1.0F);
	}

	/// Runs the effects that have been put down, once per sim update. They are lights registered afresh each update and visual particles, so removing one leaves nothing behind
	/// (but for smoke, gas and fire already let out, which are real).
	void UpdateEffects() {
		if (s_Effects.empty()) {
			return;
		}
		long long update = g_TimerMan.GetSimUpdateCount();
		float time = static_cast<float>(update) * g_TimerMan.GetDeltaTimeSecs();
		for (PlacedEffect& effect: s_Effects) {
			const Vector& at = effect.Position;
			float phase = time + effect.Seed * 20.0F;
			auto every = [&](int updates) { return (update + static_cast<long long>(effect.Seed * 997.0F)) % updates == 0; };
			switch (effect.Kind) {
				case EffectKind::NuclearGlow:
					g_PostProcessMan.RegisterLight(at, glm::vec3(80.0F, 255.0F, 60.0F), 320.0F, 2.2F + 0.7F * std::sin(phase * 1.7F));
					g_PostProcessMan.RegisterLight(at, glm::vec3(170.0F, 255.0F, 130.0F), 70.0F, 3.0F);
					g_PostProcessMan.RegisterShimmer(at, 90.0F, 0.7F);
					if (every(5)) {
						EffectsParticles::Emit("Embers", at + Vector((Random01() - 0.5F) * 90.0F, (Random01() - 0.5F) * 30.0F), Vector(0.0F, -1.0F), 1.0F, 1, 0x60FF40);
					}
					break;
				case EffectKind::StormCell:
					if (--effect.Wait <= 0) {
						effect.Flash = 0.6F + Random01() * 0.6F;
						effect.Wait = 15 + static_cast<int>(Random01() * 150.0F);
						if (Random01() < 0.3F) {
							StrikeLightning(at + Vector((Random01() - 0.5F) * 260.0F, 0.0F));
						}
					}
					effect.Flash *= 0.8F;
					g_PostProcessMan.RegisterLight(at + Vector(0.0F, -90.0F), glm::vec3(195.0F, 215.0F, 255.0F), 560.0F, 0.12F + effect.Flash * 7.0F);
					break;
				case EffectKind::RedAlarm: {
					Vector direction(std::cos(phase * 4.0F), std::sin(phase * 4.0F));
					g_PostProcessMan.RegisterConeLight(at, direction, 26.0F, glm::vec3(255.0F, 28.0F, 18.0F), 280.0F, 3.2F);
					g_PostProcessMan.RegisterConeLight(at, direction * -1.0F, 26.0F, glm::vec3(255.0F, 28.0F, 18.0F), 280.0F, 3.2F);
					g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 40.0F, 25.0F), 26.0F, 1.5F);
					break;
				}
				case EffectKind::PoliceLights: {
					bool red = std::fmod(phase * 3.0F, 1.0F) < 0.5F;
					bool lit = std::fmod(phase * 12.0F, 1.0F) < 0.6F;
					if (lit) {
						g_PostProcessMan.RegisterLight(at + Vector(red ? -8.0F : 8.0F, 0.0F), red ? glm::vec3(255.0F, 25.0F, 20.0F) : glm::vec3(30.0F, 80.0F, 255.0F), 240.0F, 3.0F);
					}
					break;
				}
				case EffectKind::BlueBeacon:
					g_PostProcessMan.RegisterLight(at, glm::vec3(40.0F, 120.0F, 255.0F), 220.0F, 0.2F + 3.0F * std::pow(std::max(std::sin(phase * 2.6F), 0.0F), 4.0F));
					break;
				case EffectKind::Floodlight:
					g_PostProcessMan.RegisterConeLight(at, Vector(0.0F, 1.0F), 36.0F, glm::vec3(255.0F, 244.0F, 222.0F), 460.0F, 3.2F);
					g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 244.0F, 222.0F), 22.0F, 1.6F);
					break;
				case EffectKind::Searchlight: {
					float angle = 1.5708F + 0.95F * std::sin(phase * 0.8F);
					g_PostProcessMan.RegisterConeLight(at, Vector(std::cos(angle), std::sin(angle)), 8.0F, glm::vec3(225.0F, 238.0F, 255.0F), 640.0F, 4.5F);
					g_PostProcessMan.RegisterLight(at, glm::vec3(225.0F, 238.0F, 255.0F), 20.0F, 1.5F);
					break;
				}
				case EffectKind::Strobe:
					if (std::fmod(phase * 9.0F, 1.0F) < 0.22F) {
						g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 255.0F, 255.0F), 340.0F, 4.0F);
					}
					break;
				case EffectKind::Disco:
					for (int beam = 0; beam < 3; ++beam) {
						float angle = phase * (1.3F + 0.4F * static_cast<float>(beam)) * (beam == 1 ? -1.0F : 1.0F) + static_cast<float>(beam) * 2.1F;
						g_PostProcessMan.RegisterConeLight(at, Vector(std::cos(angle), std::sin(angle)), 14.0F, Hue(phase * 0.25F + static_cast<float>(beam) / 3.0F), 320.0F, 3.4F);
					}
					g_PostProcessMan.RegisterLight(at, Hue(phase * 0.5F), 30.0F, 1.6F);
					break;
				case EffectKind::Campfire: {
					float flicker = 0.6F * std::sin(phase * 11.0F) + 0.4F * std::sin(phase * 23.0F + 1.3F);
					g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 150.0F, 60.0F), 160.0F + 10.0F * flicker, 1.7F + 0.45F * flicker);
					g_PostProcessMan.RegisterShimmer(at + Vector(0.0F, -14.0F), 24.0F, 0.5F);
					if (every(3)) {
						EffectsParticles::Emit("Embers", at + Vector((Random01() - 0.5F) * 10.0F, -2.0F), Vector(0.0F, -1.5F), 0.7F, 1, 0);
					}
					if (every(10)) {
						EffectsParticles::Emit("Sparks", at, Vector((Random01() - 0.5F) * 2.0F, -5.0F), 0.6F, 2, 0);
					}
					if (every(120)) {
						SpawnPuffs("Thick Smoke Ball", at + Vector(0.0F, -8.0F), 3, 1);
					}
					break;
				}
				case EffectKind::Candle:
					g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 180.0F, 95.0F), 62.0F, 1.0F + 0.2F * std::sin(phase * 9.0F) + 0.1F * std::sin(phase * 31.0F));
					break;
				case EffectKind::LavaGlow:
					g_PostProcessMan.RegisterLight(at, glm::vec3(255.0F, 85.0F, 18.0F), 240.0F, 1.7F + 0.35F * std::sin(phase * 0.9F));
					g_PostProcessMan.RegisterShimmer(at + Vector(0.0F, -20.0F), 60.0F, 0.6F);
					if (every(8)) {
						EffectsParticles::Emit("Embers", at + Vector((Random01() - 0.5F) * 120.0F, 0.0F), Vector(0.0F, -1.0F), 1.0F, 1, 0);
					}
					break;
				case EffectKind::WeldingArc:
					if (Random01() < 0.6F) {
						g_PostProcessMan.RegisterLight(at, glm::vec3(170.0F, 200.0F, 255.0F), 150.0F, 2.5F + Random01() * 3.5F);
						EffectsParticles::Emit("Sparks", at, Vector((Random01() - 0.5F) * 6.0F, -2.0F - Random01() * 4.0F), 1.0F, 2, 0xCFE4FF);
					}
					break;
				case EffectKind::Fireflies:
					for (int fly = 0; fly < 7; ++fly) {
						float own = static_cast<float>(fly) * 1.618F + effect.Seed * 6.0F;
						Vector where = at + Vector(std::sin(phase * (0.5F + 0.13F * static_cast<float>(fly)) + own) * 60.0F, std::cos(phase * (0.37F + 0.09F * static_cast<float>(fly)) + own * 2.0F) * 34.0F);
						float glow = std::pow(std::max(std::sin(phase * 2.3F + own * 3.0F), 0.0F), 2.0F);
						if (glow > 0.05F) {
							g_PostProcessMan.RegisterLight(where, glm::vec3(190.0F, 255.0F, 90.0F), 18.0F, 1.6F * glow);
						}
					}
					break;
				case EffectKind::Portal:
					g_PostProcessMan.RegisterLight(at, glm::vec3(170.0F, 60.0F, 255.0F), 200.0F, 1.8F + 0.8F * std::sin(phase * 3.1F));
					g_PostProcessMan.RegisterShimmer(at, 46.0F, 1.2F);
					if (every(2)) {
						float angle = Random01() * 6.2832F;
						EffectsParticles::Emit("Sparks", at + Vector(std::cos(angle), std::sin(angle)) * 30.0F, Vector(-std::cos(angle) * 3.0F, -std::sin(angle) * 3.0F), 0.2F, 1, 0xC070FF);
					}
					break;
				case EffectKind::SparkFountain:
					EffectsParticles::Emit("Sparks", at, Vector((Random01() - 0.5F) * 2.0F, -9.0F), 0.4F, 3, 0);
					g_PostProcessMan.RegisterLight(at + Vector(0.0F, -10.0F), glm::vec3(255.0F, 205.0F, 130.0F), 80.0F, 1.3F);
					break;
				case EffectKind::EmberVent:
					if (every(2)) {
						EffectsParticles::Emit("Embers", at + Vector((Random01() - 0.5F) * 30.0F, 0.0F), Vector(0.0F, -2.0F), 0.8F, 1, 0);
					}
					break;
				case EffectKind::SmokeStack:
					if (every(14)) {
						SpawnPuffs("Thick Smoke Ball", at, 4, 1);
					}
					break;
				case EffectKind::SmokePlume:
					EffectsParticles::Emit("Smoke", at + Vector(std::sin(phase * 5.0F) * 12.0F, -std::fmod(phase * 16.0F, 30.0F)), Vector(std::cos(phase * 5.0F) * 3.0F, -2.5F), 0.4F, 1, 0);
					break;
				case EffectKind::ToxicVent:
					g_PostProcessMan.RegisterLight(at, glm::vec3(120.0F, 255.0F, 70.0F), 90.0F, 0.9F);
					if (every(22)) {
						SpawnPuffs("Toxic Gas Ball", at, 4, 1);
					}
					break;
				case EffectKind::MistVent:
					EffectsParticles::Emit("Mist", at + Vector((Random01() - 0.5F) * 8.0F, 0.0F), Vector((Random01() - 0.5F) * 2.0F, -3.5F), 0.8F, 1, 0);
					break;
				case EffectKind::DustDevil:
					EffectsParticles::Emit("Dust", at + Vector(std::sin(phase * 6.0F) * 14.0F, -std::fmod(phase * 20.0F, 40.0F)), Vector(std::cos(phase * 6.0F) * 4.0F, -3.0F), 0.4F, 1, 0);
					break;
				case EffectKind::FireJet:
					g_PostProcessMan.RegisterLight(at + Vector(0.0F, -20.0F), glm::vec3(255.0F, 140.0F, 50.0F), 130.0F, 1.8F + 0.4F * std::sin(phase * 17.0F));
					if (every(2)) {
						if (MovableObject* flame = CreateBaseObject("MOSParticle", "Flame Hurt Short")) {
							flame->SetPos(at);
							flame->SetVel(Vector((Random01() - 0.5F) * 2.0F, -7.0F - Random01() * 3.0F));
							g_MovableMan.AddParticle(flame);
						}
					}
					break;
				case EffectKind::HeatShimmer:
					g_PostProcessMan.RegisterShimmer(at, 80.0F, 1.3F);
					break;
				case EffectKind::ShockwavePulse:
					if (every(90)) {
						g_PostProcessMan.RegisterShockwave(at, 6000.0F);
					}
					break;
				default:
					break;
			}
		}
	}

	/// A place water keeps pouring from until it's removed: a spring, a burst pipe, a tap left on.
	struct WaterSpawner {
		Vector Position;
		int Radius = 3; //!< How wide the pour is: air within this many pixels of the place is kept full of water.
	};
	std::vector<WaterSpawner> s_WaterSpawners;

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
	std::vector<Incoming> s_Incoming;

	void Launch(int delay, const Vector& from, const Vector& target, float speed, const char* preset, int crater, const char* className = "TDExplosive", int team = 0) {
		if (s_Incoming.size() < 200) {
			Incoming incoming;
			incoming.ClassName = className;
			incoming.Team = team;
			incoming.Delay = delay;
			incoming.From = from;
			incoming.Target = target;
			incoming.Speed = speed;
			incoming.Preset = preset;
			incoming.Crater = crater;
			incoming.LastPos = from;
			s_Incoming.push_back(incoming);
		}
	}

	void UpdateIncoming() {
		for (size_t i = 0; i < s_Incoming.size();) {
			Incoming& incoming = s_Incoming[i];
			if (incoming.Delay > 0) {
				--incoming.Delay;
				++i;
				continue;
			}
			Vector line = g_SceneMan.ShortestDistance(incoming.From, incoming.Target, g_SceneMan.SceneWrapsX());
			Vector direction = line.GetMagnitude() > 0.01F ? line / line.GetMagnitude() : Vector(0.0F, 1.0F);
			// Object speeds are in metres a second: 20 pixels to the metre, 60 updates a second.
			Vector velocity = direction * (incoming.Speed * 3.0F);
			if (incoming.Id == 0) {
				MovableObject* object = CreateBaseObject(incoming.ClassName.c_str(), incoming.Preset.c_str());
				if (!object) {
					s_Incoming.erase(s_Incoming.begin() + static_cast<std::ptrdiff_t>(i));
					continue;
				}
				object->SetPos(incoming.From);
				object->SetVel(velocity);
				if (Actor* craft = dynamic_cast<Actor*>(object)) {
					// A craft under its own AI, engines burning, that isn't going to make it.
					craft->SetTeam(incoming.Team);
					craft->SetControllerMode(Controller::CIM_AI);
				}
				if (incoming.ClassName != "ACDropShip") {
					object->SetRotAngle(incoming.ClassName == "ACRocket" ? direction.GetAbsRadAngle() + 1.5708F : direction.GetAbsRadAngle());
				}
				incoming.Id = object->GetUniqueID();
				AddObject(object);
				++i;
				continue;
			}
			MovableObject* object = g_MovableMan.FindObjectByUniqueID(incoming.Id);
			// Gone before it got there: the game blew it up on the way (it hit something) or it was removed. Its blast, if any, has been.
			const bool goneEarly = object == nullptr;
			bool arrived = goneEarly || --incoming.Life <= 0;
			Vector position = object ? object->GetPos() : incoming.LastPos;
			if (object) {
				incoming.LastPos = position;
				// Kept on its line, whatever gravity and the air would do to it.
				object->SetVel(velocity);
				if (incoming.ClassName == "ACDropShip") {
					// A dropship comes down level but out of control, rocking as it goes.
					object->SetRotAngle(0.35F * std::sin(static_cast<float>(incoming.Life) * 0.21F) + (direction.m_X > 0.0F ? -0.25F : 0.25F));
				} else {
					object->SetRotAngle(incoming.ClassName == "ACRocket" ? direction.GetAbsRadAngle() + 1.5708F : direction.GetAbsRadAngle());
				}
				EffectsParticles::Emit("Sparks", position - direction * 6.0F, Vector(-velocity.m_X * 0.15F, -velocity.m_Y * 0.15F), 0.5F, 2, 0);
				EffectsParticles::Emit("Dust", position - direction * 8.0F, Vector(0.0F, -0.5F), 1.0F, 1, 0x8C8C8C);
				Vector left = g_SceneMan.ShortestDistance(position, incoming.Target, g_SceneMan.SceneWrapsX());
				arrived = arrived || left.GetMagnitude() < incoming.Speed * 1.5F || left.Dot(direction) < 0.0F;
				// Or it has run into the ground, or a building, on the way.
				for (float ahead = 0.0F; ahead <= incoming.Speed && !arrived; ahead += 3.0F) {
					Vector probe = position + direction * ahead;
					arrived = probe.m_Y > 0.0F && g_SceneMan.GetTerrMatter(probe.GetFloorIntX(), probe.GetFloorIntY()) != g_MaterialAir;
				}
			}
			if (!arrived) {
				++i;
				continue;
			}
			if (incoming.Crater > 0) {
				PaintTerrain(position, incoming.Crater, nullptr);
			}
			if (MOSRotating* explosive = dynamic_cast<MOSRotating*>(object)) {
				explosive->GibThis();
			} else if (incoming.ClassName == "TDExplosive" && !goneEarly) {
				// (Not again for one the game already set off: that was a second blast where the first had been.)
				Detonate(incoming.Preset.c_str(), position);
			}
			if (incoming.ClassName != "TDExplosive") {
				// A craft full of fuel hitting the ground goes up harder than its wreckage alone.
				Detonate("Standard Bomb", position);
				Detonate("Napalm Bomb", position + Vector(0.0F, -6.0F));
			}
			s_Incoming.erase(s_Incoming.begin() + static_cast<std::ptrdiff_t>(i));
		}
	}

	/// A lightning strike: a jagged bolt from the sky to the first thing below the point, a flash, fire and harm where it lands, and thunder.
	void StrikeLightning(const Vector& target) {
		Vector ground = target;
		for (int i = 0; i < 600 && g_SceneMan.GetTerrMatter(ground.GetFloorIntX(), ground.GetFloorIntY()) == g_MaterialAir && ground.m_Y < static_cast<float>(g_SceneMan.GetSceneHeight() - 1); ++i) {
			ground.m_Y += 1.0F;
		}
		// From the open sky over the point, at most 480 px above the ground: up through the air from the point, not from the top of the
		// view. (From the view, the bolt's particles, their number and places, went by where the camera was, so a storm ran differently
		// in a replay; and zoomed in or underground the bolt started inside the earth.)
		float top = target.m_Y;
		while (top > 0.0F && top > ground.m_Y - 480.0F && g_SceneMan.GetTerrMatter(ground.GetFloorIntX(), static_cast<int>(top) - 1) == g_MaterialAir) {
			top -= 1.0F;
		}
		auto boltDot = [](const Vector& at) {
			if (MovableObject* spark = CreateBaseObject("MOPixel", "Lightning Bolt Particle")) {
				spark->SetPos(at);
				g_MovableMan.AddParticle(spark);
			}
		};
		// The bolt: a few jagged segments, with a short side branch.
		Vector from(ground.m_X + (Random01() - 0.5F) * 60.0F, top);
		constexpr int segments = 9;
		for (int segment = 1; segment <= segments; ++segment) {
			float t = static_cast<float>(segment) / static_cast<float>(segments);
			Vector to = segment == segments ? ground : Vector(Lerp(0.0F, 1.0F, from.m_X, ground.m_X, t) + (Random01() - 0.5F) * 26.0F, Lerp(0.0F, 1.0F, top, ground.m_Y, t));
			Vector step = to - from;
			int dots = std::max(1, static_cast<int>(step.GetMagnitude() / 2.0F));
			for (int dot = 0; dot < dots; ++dot) {
				boltDot(from + step * (static_cast<float>(dot) / static_cast<float>(dots)));
			}
			if (segment == segments / 2) {
				Vector branch = to;
				for (int dot = 0; dot < 14; ++dot) {
					branch += Vector(Random01() < 0.5F ? -2.0F : 2.0F, 2.0F);
					boltDot(branch);
				}
			}
			from = to;
		}
		if (SceneLighting* lighting = g_PostProcessMan.GetSceneLighting()) {
			lighting->TriggerLightning();
		}
		EffectsParticles::SpawnExplosion(ground, 900.0F);
		TerrainFire::QueueIgniteArea(ground, 12.0F);
		TerrainFire::QueueIgniteArea(ground, 6.0F);
		for (Actor* actor: SandboxAccess::Actors()) {
			float distance = g_SceneMan.ShortestDistance(ground, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetMagnitude();
			if (distance < 30.0F) {
				actor->SetHealth(actor->GetHealth() - 80.0F * (1.0F - distance / 30.0F));
			}
		}
		if (!s_Thunder) {
			if (const Entity* preset = g_PresetMan.GetEntityPreset("SoundContainer", "Explosion Large", "Base.rte")) {
				s_Thunder = dynamic_cast<SoundContainer*>(preset->Clone());
			}
		}
		if (s_Thunder) {
			s_Thunder->Play(ground);
		}
	}

	void GiveLoadout(Actor* actor, const Preset& unit, int loadout) {
		if (loadout == 1) {
			return;
		}
		if (loadout >= 2) {
			if (size_t weapon = static_cast<size_t>(loadout - 2); weapon < s_Weapons.size()) {
				if (MovableObject* gun = CreateObject(s_Weapons[weapon]->ClassName, s_Weapons[weapon]->PresetName, s_Weapons[weapon]->ModuleID)) {
					actor->AddInventoryItem(gun);
				}
			}
			return;
		}
		// The faction's own kit, or the Coalition's for factions without guns of their own.
		const FactionArmoury* armoury = &ArmouryOf(unit.ModuleID);
		if (armoury->Primaries.empty()) {
			armoury = &ArmouryOf(g_PresetMan.GetModuleID("Coalition.rte"));
		}
		auto pick = [](const std::vector<const Preset*>& from) { return from.empty() ? nullptr : from[std::min(from.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(from.size())))]; };
		for (const Preset* item: {pick(armoury->Primaries), pick(armoury->Secondaries), Random01() < 0.5F ? pick(armoury->Grenades) : nullptr}) {
			if (item) {
				if (MovableObject* object = CreateObject(item->ClassName, item->PresetName, item->ModuleID)) {
					actor->AddInventoryItem(object);
				}
			}
		}
	}

	UnitRef s_PlayerUnit; //!< Your character in the Sandbox game mode, while it lives.
	int s_PlayerEnterPending = 0; //!< Updates left to wait for the character to be in the world before stepping into it; 0 when not waiting.

	/// Makes a unit ready to go: armed, on a side, run by the AI, with orders to follow once it's in the world.
	Actor* CreateUnit(const Preset& preset, int team, int loadout, Order order) {
		Actor* actor = dynamic_cast<Actor*>(CreateObject(preset.ClassName, preset.PresetName, preset.ModuleID));
		if (!actor) {
			return nullptr;
		}
		GiveLoadout(actor, preset, loadout);
		actor->SetTeam(team);
		actor->SetControllerMode(Controller::CIM_AI);
		switch (order) {
			case Order::Attack:
				// Gets its target once it's out among the enemy.
				actor->SetNumberValue(c_AttackTag, 1.0);
				actor->SetAIMode(Actor::AIMODE_SENTRY);
				break;
			case Order::HuntBrains:
				actor->SetAIMode(Actor::AIMODE_BRAINHUNT);
				break;
			case Order::Patrol:
				actor->SetAIMode(Actor::AIMODE_PATROL);
				break;
			case Order::Rally:
				if (team >= 0 && team < c_Sides && s_RallySet[team]) {
					actor->AddAISceneWaypoint(s_RallyPoints[team]);
					actor->SetAIMode(Actor::AIMODE_GOTO);
				}
				break;
			case Order::Idle:
				actor->SetAIMode(Actor::AIMODE_NONE);
				break;
			case Order::DigGold:
				actor->ClearAIWaypoints();
				actor->SetAIMode(Actor::AIMODE_GOLDDIG);
				break;
			default:
				actor->SetAIMode(Actor::AIMODE_SENTRY);
				break;
		}
		return actor;
	}

	/// Sends units in by dropship or rocket, which comes down from the sky over a point, unloads and leaves. Returns what the units cost.
	float DropUnits(std::vector<Actor*>& units, int team, float x, int craft) {
		const CraftChoice& choice = c_Crafts[std::clamp(craft, 0, static_cast<int>(std::size(c_Crafts)) - 1)];
		ACraft* ship = dynamic_cast<ACraft*>(CreateBaseObject(choice.ClassName, choice.PresetName));
		float cost = 0.0F;
		if (!ship) {
			for (Actor* unit: units) {
				delete unit;
			}
			units.clear();
			return cost;
		}
		ActivateSide(team);
		for (Actor* unit: units) {
			cost += unit->GetTotalValue(unit->GetModuleID(), 1.0F);
			ship->AddInventoryItem(unit);
		}
		units.clear();
		bool fromBelow = g_SceneMan.GetTerrain() && g_SceneMan.GetTerrain()->GetOrbitDirection() == Directions::Down;
		ship->SetPos(Vector(x, fromBelow ? static_cast<float>(g_SceneMan.GetSceneHeight()) : 0.0F));
		ship->SetTeam(team);
		ship->SetControllerMode(Controller::CIM_AI);
		ship->SetAIMode(Actor::AIMODE_DELIVER);
		ship->ResetAllTimers();
		g_MovableMan.AddActor(ship);
		return cost;
	}

	void SpawnUnits(const Stroke& stroke, bool brain) {
		const Preset* preset = ChosenPreset(brain ? Tool::Brain : Tool::Unit, stroke.Choice);
		if (!preset) {
			return;
		}
		ActivateSide(stroke.Team);
		int count = brain ? 1 : stroke.Count;
		for (int i = 0; i < count; ++i) {
			Actor* actor = dynamic_cast<Actor*>(CreateObject(preset->ClassName, preset->PresetName, preset->ModuleID));
			if (!actor) {
				return;
			}
			if (!brain) {
				GiveLoadout(actor, *preset, stroke.Loadout);
			}
			// A squad spreads out sideways from the click.
			float spread = (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5F) * 16.0F;
			actor->SetPos(stroke.Position + Vector(spread, 0.0F));
			actor->SetTeam(stroke.Team);
			actor->SetControllerMode(Controller::CIM_AI);
			// (Facing the middle of the view as it was at the click: read from the camera here, in the sim, a replay faced them by
			// wherever the view happened to be.)
			actor->SetHFlipped(stroke.HasView && g_SceneMan.ShortestDistance(stroke.Position, Vector(stroke.ViewMiddleX, stroke.Position.m_Y), g_SceneMan.SceneWrapsX()).m_X < 0.0F);
			g_MovableMan.AddActor(actor);
			GiveOrder(actor, brain ? Order::Hold : stroke.Orders);
		}
	}

	void DropSquad(const Stroke& stroke) {
		const Preset* preset = ChosenPreset(Tool::Unit, stroke.Choice);
		if (!preset) {
			return;
		}
		std::vector<Actor*> units;
		for (int i = 0; i < stroke.Count; ++i) {
			if (Actor* unit = CreateUnit(*preset, stroke.Team, stroke.Loadout, stroke.Orders)) {
				units.push_back(unit);
			}
		}
		DropUnits(units, stroke.Team, stroke.Position.m_X, stroke.Craft);
	}

	void SpawnItem(const Stroke& stroke) {
		const Preset* preset = ChosenPreset(Tool::Item, stroke.Choice);
		MovableObject* item = preset ? CreateObject(preset->ClassName, preset->PresetName, preset->ModuleID) : nullptr;
		if (!item) {
			return;
		}
		item->SetPos(stroke.Position);
		if (stroke.LitGrenade) {
			if (TDExplosive* explosive = dynamic_cast<TDExplosive*>(item)) {
				explosive->Activate();
			}
		}
		AddObject(item);
	}

	Vector StructureCorner(const Preset& preset, const Vector& center, bool snap) {
		Vector topLeft = center - Vector(static_cast<float>(preset.Width) * 0.5F, static_cast<float>(preset.Height) * 0.5F);
		if (snap) {
			topLeft.SetXY(std::round(topLeft.m_X / 24.0F) * 24.0F, std::round(topLeft.m_Y / 24.0F) * 24.0F);
		}
		return topLeft;
	}

	/// Where a bunker piece's position goes for a click at a place: pieces of terrain are centred on it (and can snap to the bunker grid), doors and the like sit on it.
	Vector StructurePosition(const Preset& preset, const Vector& click, bool snap) {
		if (preset.Width > 0) {
			return StructureCorner(preset, click, snap) - Vector(preset.OffsetX, preset.OffsetY);
		}
		return snap ? Vector(std::round(click.m_X / 12.0F) * 12.0F, std::round(click.m_Y / 12.0F) * 12.0F) : click;
	}

	void PlaceStructure(const Stroke& stroke) {
		const Preset* preset = ChosenPreset(Tool::Structure, stroke.Choice);
		const Entity* entity = preset ? g_PresetMan.GetEntityPreset(preset->ClassName, preset->PresetName, preset->ModuleID) : nullptr;
		SceneObject* object = entity ? dynamic_cast<SceneObject*>(entity->Clone()) : nullptr;
		if (!object) {
			return;
		}
		// The same place the preview showed it.
		object->SetPos(StructurePosition(*preset, stroke.Position, stroke.Count > 0));
		if (!dynamic_cast<TerrainObject*>(object)) {
			// Doors and other moving bunker parts belong to a side, and open for it.
			object->SetTeam(stroke.Team);
			ActivateSide(stroke.Team);
			// Marked as placed, so the game mode's start-up (Sandbox.lua) leaves its side alone when a saved game is loaded.
			if (Actor* placedActor = dynamic_cast<Actor*>(object)) {
				placedActor->SetNumberValue("SandboxPlaced", 1.0);
			}
		}
		g_SceneMan.AddSceneObject(object);
	}

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
		s_Possessed = nullptr;
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
				if (s_Possessed && s_Possessed != actor) {
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
		bool playing = s_Possessed == actor;
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

	void SelectInBox(const Vector& cornerA, const Vector& cornerB) {
		s_Selected.clear();
		float left = std::min(cornerA.m_X, cornerB.m_X);
		float right = std::max(cornerA.m_X, cornerB.m_X);
		float top = std::min(cornerA.m_Y, cornerB.m_Y);
		float bottom = std::max(cornerA.m_Y, cornerB.m_Y);
		// Measured from the box's middle the short way round: the corners come from the camera unwrapped, and actors' positions are
		// wrapped, so on a wrapping map a box across the seam (or drawn with the view past the right edge) took nothing on one side.
		Vector middle((left + right) * 0.5F, (top + bottom) * 0.5F);
		float halfWidth = (right - left) * 0.5F;
		float halfHeight = (bottom - top) * 0.5F;
		std::vector<Actor*> inBox;
		std::array<int, c_Sides> perSide {};
		for (Actor* actor: SandboxAccess::Actors()) {
			Vector offset = g_SceneMan.ShortestDistance(middle, actor->GetPos(), g_SceneMan.SceneWrapsX());
			if (IsSelectable(actor) && std::abs(offset.m_X) <= halfWidth && std::abs(offset.m_Y) <= halfHeight) {
				inBox.push_back(actor);
				++perSide[actor->GetTeam()];
			}
		}
		// One side only: the side picked in the panel when it has units in the box, else the side with the most there. (Every side's units
		// were taken, and the command went by the first one's side: a click on a soldier of one side sent the other side's at it.)
		int side = s_Team >= 0 && s_Team < c_Sides && perSide[s_Team] > 0 ? s_Team : static_cast<int>(std::max_element(perSide.begin(), perSide.end()) - perSide.begin());
		for (Actor* actor: inBox) {
			if (actor->GetTeam() == side) {
				s_Selected.push_back(MakeRef(actor));
			}
		}
		if (g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Sandbox)) {
			std::string where;
			for (const Actor* actor: SandboxAccess::Actors()) {
				where += " " + actor->GetPresetName() + "@" + std::to_string(static_cast<int>(actor->GetPos().m_X)) + "," + std::to_string(static_cast<int>(actor->GetPos().m_Y));
			}
			g_ConsoleMan.PrintString("SANDBOX: select box " + std::to_string(static_cast<int>(left)) + "," + std::to_string(static_cast<int>(top)) + " to " + std::to_string(static_cast<int>(right)) + "," + std::to_string(static_cast<int>(bottom)) + " took " + std::to_string(s_Selected.size()) + "; actors:" + where);
		}
	}

	/// The selected units move to a point, or attack the unit there.
	/// Places for a number of units to stand as near a point as the ground allows: on the ground, spread out either side of it, never inside anything.
	/// Each is where a unit's feet go.
	std::vector<Vector> StandingSpots(const Vector& around, int count) {
		std::vector<Vector> spots;
		if (count <= 0 || !g_SceneMan.GetScene()) {
			return spots;
		}
		const int sceneHeight = g_SceneMan.GetSceneHeight();
		const float stride = std::clamp(s_Spacing, 8.0F, 60.0F);
		// Outwards from the point: there, then left and right in turn, further each time.
		for (int step = 0; step < count * 6 && static_cast<int>(spots.size()) < count; ++step) {
			float offset = step == 0 ? 0.0F : (static_cast<float>((step + 1) / 2) * stride) * ((step % 2 == 1) ? -1.0F : 1.0F);
			Vector probe = around + Vector(offset, 0.0F);
			g_SceneMan.WrapPosition(probe);
			int x = probe.GetFloorIntX();
			int y = probe.GetFloorIntY();
			// Out of the ground if inside it, by the nearer side (down when they're even), then down to the ground beneath, within a short way of
			// the point. (Only ever up, a click on a bunker's ceiling slab sent the unit a storey up, or onto the roof.)
			if (g_SceneMan.GetTerrMatter(x, y) != g_MaterialAir) {
				int up = 0;
				while (up < 120 && y - up > 0 && g_SceneMan.GetTerrMatter(x, y - up) != g_MaterialAir) {
					++up;
				}
				int down = 0;
				while (down < 120 && y + down < sceneHeight - 2 && g_SceneMan.GetTerrMatter(x, y + down) != g_MaterialAir) {
					++down;
				}
				if (down < 120 && down <= up) {
					y += down;
				} else if (up < 120) {
					y -= up;
				} else {
					continue;
				}
			}
			int fell = 0;
			while (fell < 240 && y < sceneHeight - 2 && g_SceneMan.GetTerrMatter(x, y + 1) == g_MaterialAir) {
				++y;
				++fell;
			}
			if (fell >= 240 || y >= sceneHeight - 2) {
				continue;
			}
			// Head room, and nothing already chosen too close.
			bool clear = true;
			for (int up = 2; up <= 40 && clear; up += 4) {
				clear = g_SceneMan.GetTerrMatter(x, y - up) == g_MaterialAir;
			}
			for (const Vector& taken: spots) {
				if (clear && g_SceneMan.ShortestDistance(taken, Vector(static_cast<float>(x), static_cast<float>(y)), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(stride * 0.7F)) {
					clear = false;
				}
			}
			if (clear) {
				spots.emplace_back(static_cast<float>(x), static_cast<float>(y));
			}
		}
		return spots;
	}

	/// The units a move order from a point goes to: a side's, or the selected ones.
	std::vector<Actor*> UnitsToMove(int team, bool selectedOnly) {
		std::vector<Actor*> units;
		if (selectedOnly) {
			for (const UnitRef& ref: s_Selected) {
				if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled() && !dynamic_cast<const ACraft*>(unit)) {
					units.push_back(unit);
				}
			}
		} else {
			for (Actor* actor: SandboxAccess::Actors()) {
				if (actor->GetTeam() == team && IsCombatant(actor) && !actor->IsPlayerControlled() && !actor->IsInGroup("Brains") && !dynamic_cast<const ACraft*>(actor)) {
					units.push_back(actor);
				}
			}
		}
		return units;
	}

	/// Sends units to stand round a point, each to its own spot, the nearest unit to the nearest spot.
	void MoveUnitsTo(std::vector<Actor*> units, const Vector& point) {
		std::vector<Vector> spots = StandingSpots(point, static_cast<int>(units.size()) * 2);
		if (spots.empty()) {
			return;
		}
		// Spots no unit can get to (walled off, across a gap too wide) are passed over, so nobody is sent to stand at a wall.
		// The searches run side by side, a batch of as many spots as there are units at a time, nearest first, till enough are found: one by
		// one on the main thread, a move of twenty units was up to forty searches in a row, and the game hitched for each such order.
		// (The grid isn't rebuilt under them: that happens on this thread, which waits here.)
		if (Scene* scene = g_SceneMan.GetScene(); scene && !units.empty()) {
			std::vector<Vector> reachable;
			// With the unit's own reach, as its AI will search: the same jump height, dig strength and breaching, on its team's grid.
			const Actor* leader = units.front();
			const Vector from = leader->GetPos();
			const float jumpHeight = leader->EstimateJumpHeight();
			const float digStrength = leader->EstimateDigStrength();
			const float breachStrength = leader->EstimateBreachStrength();
			const Activity::Teams team = static_cast<Activity::Teams>(leader->GetTeam());
			size_t batch = std::max<size_t>(units.size(), 4);
			for (size_t first = 0; first < spots.size() && reachable.size() < units.size(); first += batch) {
				size_t count = std::min(batch, spots.size() - first);
				std::vector<char> reaches(count, 0);
				std::vector<size_t> indices(count);
				std::iota(indices.begin(), indices.end(), size_t{0});
				std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t i) {
					std::list<Vector> path;
					float cost = scene->CalculatePath(from, spots[first + i], path, jumpHeight, digStrength, team, breachStrength);
					reaches[i] = cost >= 0.0F && cost < 100000.0F ? 1 : 0;
				});
				for (size_t i = 0; i < count && reachable.size() < units.size(); ++i) {
					if (reaches[i]) {
						reachable.push_back(spots[first + i]);
					}
				}
			}
			if (!reachable.empty()) {
				spots = reachable;
			}
		}
		spots.resize(std::min(spots.size(), units.size()));
		std::sort(units.begin(), units.end(), [&point](Actor* a, Actor* b) {
			return g_SceneMan.ShortestDistance(point, a->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude() < g_SceneMan.ShortestDistance(point, b->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
		});
		for (size_t i = 0; i < units.size(); ++i) {
			Actor* unit = units[i];
			const Vector& spot = spots[std::min(i, spots.size() - 1)];
			// The waypoint just over the ground where the feet go (the AI puts it at its own standing height from there). Half the unit's height up,
			// as it was, was inside the ceiling of a low corridor, and a waypoint inside a thin slab is taken to be on top of it: a unit sent a few
			// steps along a bunker corridor went out and round to the roof over it.
			SendUnit(unit, spot + Vector(0.0F, -4.0F), nullptr, false, "move");
		}
	}

	/// Who a unit is following, if anyone: the actor it is to go to, loaded (its move target) or still queued as its last waypoint.
	const Actor* FollowedBy(const Actor* unit) {
		if (const Actor* leader = dynamic_cast<const Actor*>(unit->GetMOMoveTarget()); leader && g_MovableMan.IsActor(leader)) {
			return leader;
		}
		const auto& waypoints = unit->GetWaypointList();
		if (!waypoints.empty()) {
			if (const Actor* leader = dynamic_cast<const Actor*>(waypoints.back().second); leader && g_MovableMan.IsActor(leader)) {
				return leader;
			}
		}
		return nullptr;
	}

	/// Sets a unit to guard another: a squad follower of it, as the game's own squads are (AIMODE_SQUAD and the leader as its MO waypoint), so
	/// it gets the trail, its place in the formation and the dead-leader handling. (A GOTO to the leader, as it was, took the old shoving path
	/// squads were fixed away from.) A follow that would close a loop (the leader following this unit, or one that does) is broken there: the
	/// one in the loop who followed this unit holds where it is instead, or the two walked into each other for ever.
	void GuardUnit(Actor* unit, Actor* leader) {
		const Actor* along = leader;
		for (int i = 0; along && i < 16; ++i) {
			const Actor* next = FollowedBy(along);
			if (next == unit) {
				HoldUnit(const_cast<Actor*>(along));
				break;
			}
			along = next;
		}
		HoldUnit(unit);
		unit->SetAIMode(Actor::AIMODE_SQUAD);
		unit->AddAIMOWaypoint(leader);
		unit->SetMovePathToUpdate();
	}

	void OrderSelectedUnits(int choice, const Vector& point);

	/// The side the selection belongs to: the first selected unit's, else the side in hand.
	int SelectionTeam() {
		for (const UnitRef& ref: s_Selected) {
			if (const Actor* unit = GetRef(ref)) {
				return unit->GetTeam();
			}
		}
		return s_Team;
	}

	void MarkOrder(const Vector& at, ImU32 color) { s_OrderMarks.push_back({at, 1.0F, color}); }

	/// A click on the world with the command tool, as the mode says. Count: 0 a plain click, 1 with Shift held (add to the selection), 2 a double click
	/// (select every unit of that kind in sight).
	void QueueWaypoint(std::vector<Actor*> units, const Vector& point);

	void CommandSelected(const Vector& position, int modifier) {
		Actor* target = dynamic_cast<Actor*>(ObjectUnder(position, true));
		if (g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Sandbox)) {
			g_ConsoleMan.PrintString("SANDBOX: command at " + std::to_string(static_cast<int>(position.m_X)) + "," + std::to_string(static_cast<int>(position.m_Y)) + " selected " + std::to_string(s_Selected.size()) + " target " + (target ? target->GetPresetName() : std::string("none")) + " mode " + std::to_string(static_cast<int>(s_CommandMode)));
		}
		bool friendly = target && IsSelectable(target) && (s_Selected.empty() || target->GetTeam() == SelectionTeam());
		bool selected = target && std::any_of(s_Selected.begin(), s_Selected.end(), [target](const UnitRef& ref) { return RefersTo(ref, target); });
		if (s_CommandMode == CommandMode::Guard) {
			// Follow the friend clicked; with nobody there, nothing happens.
			if (friendly && !selected) {
				for (const UnitRef& ref: s_Selected) {
					if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled() && unit != target) {
						GuardUnit(unit, target);
					}
				}
				MarkOrder(target->GetPos(), IM_COL32(120, 220, 120, 255));
			}
			return;
		}
		if (s_CommandMode == CommandMode::Attack) {
			OrderSelectedUnits(1, position);
			MarkOrder(position, IM_COL32(239, 106, 91, 255));
			return;
		}
		// Move: a friend is picked up into the selection, an enemy attacked, the ground gone to.
		if (friendly && (modifier != 0 || !selected || s_Selected.size() == 1)) {
			if (modifier == 2) {
				// Every unit of that kind in sight.
				GameViewRect view = g_WindowMan.GetGameViewRect();
				float scale = ScenePixelsPerWindowPixel();
				Vector corner = g_CameraMan.GetOffset(0);
				Vector far = corner + Vector(view.w * scale, view.h * scale);
				for (Actor* actor: SandboxAccess::Actors()) {
					Vector onScreen = g_SceneMan.ShortestDistance(corner, actor->GetPos(), g_SceneMan.SceneWrapsX());
					if (IsSelectable(actor) && actor->GetTeam() == target->GetTeam() && actor->GetPresetName() == target->GetPresetName() && onScreen.m_X >= 0.0F && onScreen.m_Y >= 0.0F && onScreen.m_X <= far.m_X - corner.m_X && onScreen.m_Y <= far.m_Y - corner.m_Y &&
					    std::none_of(s_Selected.begin(), s_Selected.end(), [actor](const UnitRef& ref) { return RefersTo(ref, actor); })) {
						s_Selected.push_back(MakeRef(actor));
					}
				}
			} else if (modifier == 1) {
				if (selected) {
					s_Selected.erase(std::remove_if(s_Selected.begin(), s_Selected.end(), [target](const UnitRef& ref) { return RefersTo(ref, target); }), s_Selected.end());
				} else {
					s_Selected.push_back(MakeRef(target));
				}
			} else {
				s_Selected.clear();
				s_Selected.push_back(MakeRef(target));
			}
			return;
		}
		bool attack = target && IsCombatant(target) && !selected && !friendly;
		if (attack) {
			for (const UnitRef& ref: s_Selected) {
				// (Never at one of its own side, whatever the selection holds.)
				if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled() && unit->GetTeam() != target->GetTeam()) {
					SendUnit(unit, target->GetPos(), target, true, "attack", true);
				}
			}
			MarkOrder(target->GetPos(), IM_COL32(239, 106, 91, 255));
		} else if (modifier == 1) {
			// Shift: on to here after where they're going.
			QueueWaypoint(UnitsToMove(0, true), position);
			MarkOrder(position, IM_COL32(110, 180, 250, 255));
		} else {
			MoveUnitsTo(UnitsToMove(0, true), position);
			MarkOrder(position, IM_COL32(110, 180, 250, 255));
		}
	}

	/// A further place for the selected units to go on to after where they're going (a shift-click): the route is then the player's own, leg by leg.
	/// A unit going nowhere is simply sent there.
	void QueueWaypoint(std::vector<Actor*> units, const Vector& point) {
		std::vector<Vector> spots = StandingSpots(point, 1);
		for (Actor* unit: units) {
			// (Just over the ground under the point, as for a move: see MoveUnitsTo.)
			Vector waypoint = (spots.empty() ? point : spots.front()) + Vector(0.0F, -4.0F);
			auto pending = std::find_if(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); });
			if (pending != s_PendingOrders.end()) {
				pending->Then.push_back(waypoint);
			} else if (unit->GetAIMode() == Actor::AIMODE_GOTO) {
				unit->AddAISceneWaypoint(waypoint);
			} else {
				SendUnit(unit, waypoint, nullptr, false, "move (queued)");
			}
		}
	}

	/// The command ring's choices for the selected units, about a point: 0 move there, 1 attack there, 2 hold where they are.
	void OrderSelectedUnits(int choice, const Vector& point) {
		std::vector<Actor*> units = UnitsToMove(0, true);
		if (choice == 0) {
			MoveUnitsTo(units, point);
		} else if (choice == 1) {
			// The nearest enemy to the point, if there is one close, else the place itself with orders to fight whatever is met.
			Actor* target = nullptr;
			float nearest = 400.0F * 400.0F;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (!IsCombatant(actor) || actor->IsIgnoredByAI() || units.empty() || actor->GetTeam() == units.front()->GetTeam()) {
					continue;
				}
				float distance = g_SceneMan.ShortestDistance(point, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
				if (distance < nearest) {
					nearest = distance;
					target = actor;
				}
			}
			for (Actor* unit: units) {
				SendUnit(unit, point, target, true, "attack there");
				unit->SetNumberValue(c_AttackXTag, point.m_X);
				unit->SetNumberValue(c_AttackYTag, point.m_Y);
			}
		} else if (choice == 2) {
			// Cancel: every order forgotten, and the side's standing orders apply.
			for (Actor* unit: units) {
				HoldUnit(unit);
				if (static_cast<Order>(s_Order) != Order::MoveTo) {
					GiveOrder(unit, static_cast<Order>(s_Order));
				}
				MarkOrder(unit->GetPos(), IM_COL32(200, 160, 120, 255));
			}
		} else if (choice == 3) {
			// Defend: stand this ground and fight from it, moving as little as can be; a unit shoved or drawn off its post is sent back.
			for (Actor* unit: units) {
				HoldUnit(unit);
				unit->SetNumberValue(c_DefendXTag, unit->GetPos().m_X);
				unit->SetNumberValue(c_DefendYTag, unit->GetPos().m_Y);
				MarkOrder(unit->GetPos(), IM_COL32(242, 182, 61, 255));
			}
		}
	}

	/// The closest pair of enemies anywhere: where the fighting is.
	void FindAction() {
		s_ActionSpotValid = false;
		float best = 0.0F;
		std::deque<Actor*>& actors = SandboxAccess::Actors();
		for (size_t i = 0; i < actors.size(); ++i) {
			if (!IsCombatant(actors[i])) {
				continue;
			}
			for (size_t j = i + 1; j < actors.size(); ++j) {
				if (!IsCombatant(actors[j]) || actors[j]->GetTeam() == actors[i]->GetTeam()) {
					continue;
				}
				Vector between = g_SceneMan.ShortestDistance(actors[i]->GetPos(), actors[j]->GetPos(), g_SceneMan.SceneWrapsX());
				float distance = between.GetSqrMagnitude();
				if (!s_ActionSpotValid || distance < best) {
					best = distance;
					s_ActionSpot = actors[i]->GetPos() + between * 0.5F;
					s_ActionSpotValid = true;
				}
			}
		}
	}

	/// The faction's units an auto battle can buy: soldiers mostly, the odd crab.
	std::vector<const Preset*> FactionUnits(int moduleID) {
		std::vector<const Preset*> units;
		for (const Preset& unit: s_Units) {
			const Entity* entity = unit.ModuleID == moduleID ? g_PresetMan.GetEntityPreset(unit.ClassName, unit.PresetName, unit.ModuleID) : nullptr;
			if (entity && !entity->IsInGroup("Actors - Turrets")) {
				units.push_back(&unit);
			}
		}
		return units;
	}

	float AutoLaneX(int side) {
		if (s_RallySet[side]) {
			return s_RallyPoints[side].m_X;
		}
		static constexpr float lanes[c_Sides] = {-0.7F, 0.7F, -0.35F, 0.35F};
		// (Spaced by the view's width at the start, not now: zooming during the battle moved where the waves landed.)
		Vector lane = s_AutoCenter + Vector(lanes[side] * s_AutoLaneWidth, 0.0F);
		g_SceneMan.WrapPosition(lane);
		return lane.m_X;
	}

	/// Each side in an auto battle buys a wave every so often with what's left of its budget and sends it in to attack, until one side is left.
	void UpdateAutoBattle() {
		if (!s_AutoRunning) {
			return;
		}
		long long now = g_TimerMan.GetSimUpdateCount();
		for (int side = 0; side < c_Sides; ++side) {
			AutoSide& autoSide = s_AutoSides[side];
			if (!autoSide.Active || autoSide.Broke || now < autoSide.NextWave || s_FactionModules.empty()) {
				continue;
			}
			autoSide.NextWave = now + 900;
			std::vector<const Preset*> choices = FactionUnits(s_FactionModules[std::clamp(autoSide.Faction, 0, static_cast<int>(s_FactionModules.size()) - 1)]);
			float left = static_cast<float>(autoSide.Budget) - autoSide.Spent;
			// What each of the faction's units costs as bought (with its loadout), and the cheapest. The wave's budget is at least the
			// cheapest unit, and picks are made only from what still fits: a faction whose cheapest unit cost over 900 (heavy mechs, some
			// mods) never filled a wave and was called broke before buying anything, and twelve random picks over budget did the same to
			// a side that could still afford its cheapest.
			std::vector<std::pair<const Preset*, float>> priced;
			float cheapest = -1.0F;
			for (const Preset* choice: choices) {
				if (Actor* unit = CreateUnit(*choice, side, 0, Order::Attack)) {
					float cost = unit->GetTotalValue(unit->GetModuleID(), 1.0F);
					delete unit;
					priced.emplace_back(choice, cost);
					cheapest = cheapest < 0.0F ? cost : std::min(cheapest, cost);
				}
			}
			float waveBudget = std::min(left, std::max(900.0F, cheapest));
			std::vector<Actor*> wave;
			float waveCost = 0.0F;
			for (int attempt = 0; attempt < 12 && wave.size() < 5; ++attempt) {
				std::vector<const Preset*> affordable;
				for (const auto& [choice, cost]: priced) {
					if (waveCost + cost <= waveBudget) {
						affordable.push_back(choice);
					}
				}
				if (affordable.empty()) {
					break;
				}
				const Preset* pick = affordable[std::min(affordable.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(affordable.size())))];
				Actor* unit = CreateUnit(*pick, side, 0, Order::Attack);
				float cost = unit ? unit->GetTotalValue(unit->GetModuleID(), 1.0F) : 0.0F;
				if (unit && waveCost + cost <= waveBudget) {
					wave.push_back(unit);
					waveCost += cost;
				} else {
					delete unit;
				}
			}
			if (wave.empty()) {
				autoSide.Broke = true;
				continue;
			}
			// (Counted as sent only once a craft took them: with no craft to be had, DropUnits deletes the units and returns nothing.)
			int waveSize = static_cast<int>(wave.size());
			float paid = DropUnits(wave, side, AutoLaneX(side), 0);
			if (paid > 0.0F) {
				autoSide.Sent += waveSize;
				autoSide.Spent += paid;
			}
		}
		// One side left standing wins.
		if (now % 60 == 0) {
			int standing = 0;
			int lastStanding = -1;
			bool anySent = false;
			for (int side = 0; side < c_Sides; ++side) {
				const AutoSide& autoSide = s_AutoSides[side];
				if (!autoSide.Active) {
					continue;
				}
				anySent = anySent || autoSide.Sent > 0;
				if (!autoSide.Broke || Sandbox::CountUnits(side) > 0) {
					++standing;
					lastStanding = side;
				}
			}
			if (anySent && standing <= 1) {
				s_AutoRunning = false;
				s_AutoWinner = standing == 1 ? lastStanding : -1;
				std::string result = s_AutoWinner >= 0 ? std::string(c_SideNames[s_AutoWinner]) + " wins!" : std::string("It's a draw!");
				g_FrameMan.SetScreenText(result, 0, 0, 6000, true);
				g_ConsoleMan.PrintString("SANDBOX: Auto battle over. " + result);
			}
		}
	}

	void Apply(const Stroke& stroke) {
		const Vector& at = stroke.Position;
		float radius = static_cast<float>(stroke.Radius);
		switch (stroke.Kind) {
			case Tool::Possess:
				TakeControl(at);
				break;
			case Tool::Release:
				ReleaseControl();
				break;
			case Tool::PlayCharacter:
				EnterPlayer(stroke.Count > 0, at);
				break;
			case Tool::Barracks:
				if (const Preset* unit = ChosenPreset(Tool::Unit, stroke.Choice)) {
					ActivateSide(stroke.Team);
					// ("Move to a place" has no place for trainees: they hold where they come out instead.)
					Colony::Place(Colony::Kind::Barracks, at, stroke.Team, unit->PresetName, static_cast<int>(stroke.Orders == Order::MoveTo ? Order::Hold : stroke.Orders), stroke.Count);
				}
				break;
			case Tool::Extractor:
				ActivateSide(stroke.Team);
				Colony::Place(Colony::Kind::Extractor, at, stroke.Team, "", 0, 1);
				break;
			case Tool::PlayerRemake:
				if (Actor* old = GetRef(s_PlayerUnit)) {
					Vector place = old->GetPos();
					StopFlying();
					old->SetToDelete(true);
					s_PlayerUnit = UnitRef();
					MakePlayer(place);
				}
				break;
			case Tool::PlayerRemove:
				if (Actor* old = GetRef(s_PlayerUnit)) {
					StopFlying();
					old->SetToDelete(true);
				}
				s_PlayerUnit = UnitRef();
				s_PlayerEnterPending = 0;
				break;
			case Tool::Remove:
				if (MovableObject* object = ObjectUnder(at, false)) {
					// (Not your character while it can't be hurt: it vanished, and the god view came back with nothing said.)
					if (s_Player.Unkillable && object->GetRootParent() == GetRef(s_PlayerUnit)) {
						g_ConsoleMan.PrintString("SANDBOX: Your character can't be removed while it can't be hurt; turn that off first, or use Remove it on the You tab.");
						break;
					}
					object->SetToDelete(true);
				}
				break;
			case Tool::RallyPoint:
				if (stroke.Team >= 0 && stroke.Team < c_Sides) {
					s_RallyPoints[stroke.Team] = at;
					s_RallySet[stroke.Team] = true;
				}
				break;
			case Tool::Unit:
				SpawnUnits(stroke, false);
				break;
			case Tool::Brain:
				SpawnUnits(stroke, true);
				break;
			case Tool::Item:
				SpawnItem(stroke);
				break;
			case Tool::Drop:
				DropSquad(stroke);
				break;
			case Tool::Select:
				SelectInBox(stroke.Position, stroke.Position2);
				break;
			case Tool::Command:
				CommandSelected(at, stroke.Count);
				break;
			case Tool::OrderSelected:
				if (stroke.Count >= 100) {
					// From the command ring: move, attack or hold, about a point.
					OrderSelectedUnits(stroke.Count - 100, at);
					break;
				}
				for (const UnitRef& ref: s_Selected) {
					if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled()) {
						GiveOrder(unit, stroke.Orders);
					}
				}
				break;
			case Tool::Follow:
				s_FollowTarget = MakeRef(dynamic_cast<Actor*>(ObjectUnder(at, true)));
				s_FollowAction = false;
				break;
			case Tool::Structure:
				PlaceStructure(stroke);
				break;
			case Tool::OrderSide:
				if (stroke.Orders == Order::MoveTo) {
					break;
				}
				// Not your character (an AI unit only while you're out of it: told to attack with the rest, it ran off to fight), nor craft
				// (a dropship delivering was sent off with its squad still in it, and tagged to attack, yanked about every second after).
				for (Actor* actor: SandboxAccess::Actors()) {
					if (actor->GetTeam() == stroke.Team && IsCombatant(actor) && !actor->IsPlayerControlled() && actor != GetRef(s_PlayerUnit) && !dynamic_cast<const ACraft*>(actor)) {
						GiveOrder(actor, stroke.Orders);
					}
				}
				break;
			case Tool::OrderMove:
				MoveUnitsTo(UnitsToMove(stroke.Team, false), at);
				break;
			case Tool::GymStart:
				s_GymFrom = at;
				s_GymFromSet = true;
				break;
			case Tool::GymGoal:
				s_GymTo = at;
				s_GymToSet = true;
				break;
			case Tool::GymRun:
				if (stroke.Count < 0) {
					GymRunAll();
				} else {
					GymRunCourse(stroke.Count);
				}
				break;
			case Tool::GymRemove:
				GymRemoveUnits();
				break;
			case Tool::ClearWaterSpawners:
				s_WaterSpawners.clear();
				break;
			case Tool::ClearEffects:
				if (stroke.Count == 1) {
					if (!s_Effects.empty()) {
						s_Effects.pop_back();
					}
				} else {
					s_Effects.clear();
				}
				break;
			case Tool::RemoveSide:
				// (Not your character: it's yours, not the side's.)
				for (Actor* actor: SandboxAccess::Actors()) {
					if (actor->GetTeam() == stroke.Team && !dynamic_cast<ADoor*>(actor) && actor != GetRef(s_PlayerUnit)) {
						actor->SetToDelete(true);
					}
				}
				break;
			case Tool::Fire:
				TerrainFire::QueueIgniteArea(at, radius);
				// Something to see even over rock, which doesn't burn. (Only to see: a stroke of the brush is a dozen of these a second, and as
				// they were they hit and hurt units the fire wasn't painted on. What burns is the fire itself.)
				if (MovableObject* flame = CreateBaseObject("MOSParticle", "Flame Hurt Short")) {
					flame->SetToHitMOs(false);
					flame->SetPos(at + Vector((Random01() - 0.5F) * radius, (Random01() - 0.5F) * radius));
					flame->SetVel(Vector((Random01() - 0.5F) * 1.5F, -1.0F - Random01()));
					g_MovableMan.AddParticle(flame);
				}
				break;
			case Tool::Water:
				FluidSim::Pour(at, radius * 0.5F, "Water");
				break;
			case Tool::Lava:
				FluidSim::Pour(at, radius * 0.5F, "Lava");
				break;
			case Tool::Acid:
				FluidSim::Pour(at, radius * 0.5F, "Acid");
				break;
			case Tool::Oil:
				FluidSim::Pour(at, radius * 0.5F, "Oil");
				break;
			case Tool::WaterSpawner:
				if (s_WaterSpawners.size() < 64) {
					s_WaterSpawners.push_back({at, std::max(1, stroke.Radius / 2)});
				}
				break;
			case Tool::LooseSand:
				FluidSim::Pour(at, radius * 0.5F, "Sand");
				break;
			case Tool::LooseSnow:
				FluidSim::Pour(at, radius * 0.5F, "Snow");
				break;
			case Tool::Boulder:
				TerrainCollapse::SpawnChunk(at, radius * 1.5F + 4.0F, "Stone");
				break;
			case Tool::Slab:
				TerrainCollapse::SpawnChunk(at, radius * 1.5F + 4.0F, "Concrete");
				break;
			case Tool::Smoke:
				SpawnPuffs("Thick Smoke Ball", at, stroke.Radius, 2);
				break;
			case Tool::ToxicGas:
				SpawnPuffs("Toxic Gas Ball", at, stroke.Radius, 2);
				// The invisible cloud that does the harm, now and then so painting doesn't stack hundreds of them.
				if (Random01() < 0.15F) {
					SpawnPuffs("Toxic Gas Cloud", at, 0, 1);
				}
				break;
			case Tool::Dig:
				PaintTerrain(at, stroke.Radius, nullptr);
				break;
			case Tool::Earth:
				PaintTerrain(at, stroke.Radius, "Earth");
				break;
			case Tool::Sand:
				PaintTerrain(at, stroke.Radius, "Sand");
				break;
			case Tool::Ice:
				PaintTerrain(at, stroke.Radius, "Ice");
				break;
			case Tool::Grass:
				PaintTerrain(at, stroke.Radius, "Grass");
				break;
			case Tool::Wood:
				PaintTerrain(at, stroke.Radius, "Wood");
				break;
			case Tool::Concrete:
				PaintTerrain(at, stroke.Radius, "Concrete");
				break;
			case Tool::Grenade:
				Detonate("Frag Grenade", at);
				break;
			case Tool::RocketStrike:
				// One heavy rocket out of the sky, from one side or the other, into the point marked.
				Launch(0, at + Vector(Random01() < 0.5F ? -320.0F : 320.0F, -560.0F), at, 10.0F, "Standard Bomb", 26);
				break;
			case Tool::RocketBarrage:
				for (int i = 0; i < 8; ++i) {
					Vector target = at + Vector((Random01() - 0.5F) * 160.0F, (Random01() - 0.5F) * 40.0F);
					Launch(i * 9, target + Vector(-380.0F + Random01() * 120.0F, -560.0F), target, 11.0F, i % 3 == 0 ? "Standard Bomb" : "Frag Grenade", 14);
				}
				break;
			case Tool::CarpetBomb:
				// A stick of bombs dropped in a line across the point, one after another.
				for (int i = 0; i < 10; ++i) {
					Vector target = at + Vector(-225.0F + 50.0F * static_cast<float>(i), 0.0F);
					Launch(i * 7, target + Vector(-60.0F, -520.0F), target + Vector(0.0F, 400.0F), 8.0F, "Standard Bomb", 10);
				}
				break;
			case Tool::Artillery:
				// Shells lobbed in from far off to one side, landing around the point.
				for (int i = 0; i < 5; ++i) {
					Vector target = at + Vector((Random01() - 0.5F) * 90.0F, 0.0F);
					Launch(i * 28, target + Vector(-760.0F, -430.0F), target + Vector(120.0F, 68.0F), 13.0F, "Standard Bomb", 18);
				}
				break;
			case Tool::NapalmRain:
				for (int i = 0; i < 7; ++i) {
					Vector target = at + Vector((Random01() - 0.5F) * 260.0F, 0.0F);
					Launch(i * 10, target + Vector(0.0F, -520.0F), target + Vector(0.0F, 400.0F), 7.0F, "Napalm Bomb", 0);
				}
				break;
			case Tool::OrbitalBeam: {
				// A beam straight down from the sky: it bores a shaft through whatever is under the point, a long way down, and sets fire to what will burn.
				StrikeLightning(at);
				int depth = 0;
				for (float y = 0.0F; y < static_cast<float>(g_SceneMan.GetSceneHeight()) && depth < 420; y += 6.0F) {
					Vector point(at.m_X, y);
					bool ground = g_SceneMan.GetTerrMatter(point.GetFloorIntX(), point.GetFloorIntY()) != g_MaterialAir;
					if (!ground && depth == 0) {
						continue;
					}
					depth += 6;
					PaintTerrain(point, 6, nullptr);
					if (depth % 72 == 6) {
						Detonate("Frag Grenade", point);
						TerrainFire::QueueIgniteArea(point, 14.0F);
					}
				}
				break;
			}
			case Tool::Effect:
				if (s_Effects.size() < 120) {
					s_Effects.push_back({static_cast<EffectKind>(std::clamp(stroke.Choice, 0, static_cast<int>(EffectKind::Count) - 1)), at, Random01(), 0.0F, 0});
				}
				break;
			case Tool::CrashRocket:
				Launch(0, at + Vector(Random01() < 0.5F ? -260.0F : 260.0F, -620.0F), at, 7.5F, "Rocket MK2", 24, "ACRocket", stroke.Team);
				break;
			case Tool::CrashDropship:
				Launch(0, at + Vector(Random01() < 0.5F ? -620.0F : 620.0F, -420.0F), at, 6.5F, "Dropship MK1", 30, "ACDropShip", stroke.Team);
				break;
			case Tool::BoulderRain:
				for (int i = 0; i < 8; ++i) {
					TerrainCollapse::SpawnChunk(at + Vector((Random01() - 0.5F) * 300.0F, -260.0F - Random01() * 220.0F), 8.0F + Random01() * 16.0F, "Stone");
				}
				break;
			case Tool::BuildBeam:
				PaintBox(at + Vector(-80.0F, -5.0F), 160, 10, "Concrete");
				break;
			case Tool::BuildPillar:
				PaintBox(at + Vector(-6.0F, -70.0F), 12, 140, "Concrete");
				break;
			case Tool::BuildRoom:
				// Four walls with a doorway in each side.
				PaintBox(at + Vector(-70.0F, -45.0F), 140, 8, "Concrete");
				PaintBox(at + Vector(-70.0F, 37.0F), 140, 8, "Concrete");
				PaintBox(at + Vector(-70.0F, -45.0F), 8, 52, "Concrete");
				PaintBox(at + Vector(62.0F, -45.0F), 8, 52, "Concrete");
				break;
			case Tool::BuildTower:
				// Four storeys of concrete floors and walls, standing on the point marked: something tall to bring down.
				for (int floor = 0; floor < 4; ++floor) {
					float top = -72.0F * static_cast<float>(floor + 1);
					PaintBox(at + Vector(-50.0F, top), 100, 8, "Concrete");
					PaintBox(at + Vector(-50.0F, top), 8, floor % 2 == 0 ? 72 : 44, "Concrete");
					PaintBox(at + Vector(42.0F, top), 8, floor % 2 == 0 ? 44 : 72, "Concrete");
				}
				PaintBox(at + Vector(-50.0F, -8.0F), 100, 8, "Concrete");
				break;
			case Tool::BuildIsland: {
				// A lump of earth and rock hanging in the air, to chip at and cut up.
				static constexpr float lumps[7][3] = {{-42.0F, 0.0F, 20.0F}, {-14.0F, 4.0F, 24.0F}, {16.0F, 2.0F, 24.0F}, {44.0F, -2.0F, 18.0F}, {-4.0F, 22.0F, 16.0F}, {22.0F, 20.0F, 12.0F}, {-26.0F, -14.0F, 12.0F}};
				for (const auto& lump: lumps) {
					PaintTerrain(at + Vector(lump[0], lump[1]), static_cast<int>(lump[2]), lump[2] > 17.0F ? "Earth" : "Stone");
				}
				break;
			}
			case Tool::BuildTank:
				// An open concrete tank, filled with water.
				PaintBox(at + Vector(-70.0F, 40.0F), 140, 8, "Concrete");
				PaintBox(at + Vector(-70.0F, -48.0F), 8, 90, "Concrete");
				PaintBox(at + Vector(62.0F, -48.0F), 8, 90, "Concrete");
				for (float y = -30.0F; y <= 26.0F; y += 14.0F) {
					for (float x = -48.0F; x <= 48.0F; x += 16.0F) {
						FluidSim::Pour(at + Vector(x, y), 9.0F, "Water");
					}
				}
				break;
			case Tool::BuildBridge:
				PaintBox(at + Vector(-110.0F, -3.0F), 220, 6, "Wood");
				for (float x = -100.0F; x <= 100.0F; x += 50.0F) {
					PaintBox(at + Vector(x - 2.0F, 3.0F), 4, 14, "Wood");
				}
				break;
			case Tool::Demolition:
			case Tool::BunkerBuster:
			case Tool::Meteor: {
				// Blasts that take out a real hole: everything within the crater goes (but for the edge of the world), with bombs going off across it for the fire, the flying debris and the harm.
				int crater = stroke.Kind == Tool::Demolition ? 34 : (stroke.Kind == Tool::BunkerBuster ? 62 : 100);
				PaintTerrain(at, crater, nullptr);
				Detonate("Standard Bomb", at);
				int extra = stroke.Kind == Tool::Demolition ? 2 : (stroke.Kind == Tool::BunkerBuster ? 5 : 9);
				for (int i = 0; i < extra; ++i) {
					float angle = 6.2832F * static_cast<float>(i) / static_cast<float>(extra);
					Detonate(i % 2 == 0 ? "Standard Bomb" : "Frag Grenade", at + Vector(std::cos(angle), std::sin(angle)) * (static_cast<float>(crater) * 0.6F));
				}
				break;
			}
			case Tool::BigBomb:
				Detonate("Standard Bomb", at);
				break;
			case Tool::Napalm:
				Detonate("Napalm Bomb", at);
				break;
			case Tool::Lightning:
				StrikeLightning(at);
				break;
			default:
				break;
		}
	}

	void QueueStroke(Tool kind, const Vector& position) {
		Stroke stroke;
		stroke.Kind = kind;
		stroke.Position = position;
		stroke.Radius = s_Radius;
		stroke.Choice = ChoiceFor(kind);
		stroke.Team = s_Team;
		stroke.Orders = static_cast<Order>(s_Order);
		stroke.Loadout = s_Loadout;
		stroke.Count = kind == Tool::Structure ? (s_SnapToGrid ? 1 : 0) : s_SquadSize;
		if (kind == Tool::PlayCharacter) {
			stroke.Count = 1;
		} else if (kind == Tool::Barracks) {
			stroke.Count = s_ColonyKeep;
		}
		stroke.LitGrenade = s_LitGrenade;
		stroke.Craft = s_Craft;
		stroke.HasView = true;
		stroke.ViewMiddleX = g_CameraMan.GetOffset(0).m_X + static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F;
		s_Queue.push_back(stroke);
	}
#pragma endregion

#pragma region Window
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
		// Dragging with the right button moves the view, unless the tool in hand makes things for a side: then the right button is for the ring of sides, and the
		// middle button (or the keys) moves the view.
		bool rightPans = !TakesSide(CurrentTool().Kind) || !Sandbox::CapturesWorldClicks();
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
		if (GameActivity* game = CurrentGame(); game && Sandbox::IsGodMode()) {
			game->SetObservationTarget(s_CameraCenter, Players::PlayerOne);
		}
	}

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
			case Tool::WaterSpawner:
				return {Icon::Down, IM_COL32(90, 170, 240, 255)};
			case Tool::Smoke:
				return {Icon::Cloud, IM_COL32(190, 190, 190, 255)};
			case Tool::ToxicGas:
				return {Icon::Cloud, IM_COL32(150, 220, 80, 255)};
			case Tool::LooseSand:
				return {Icon::Grains, IM_COL32(222, 190, 120, 255)};
			case Tool::LooseSnow:
				return {Icon::Grains, IM_COL32(240, 245, 255, 255)};
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

	std::string s_WantedTab; //!< The tab of the sandbox window to bring to the front, asked for from the bar.
	std::string s_CurrentTab; //!< The tab of the sandbox window that is showing.
	std::map<std::string, int> s_LastToolOfTab; //!< The tool last picked on each tab, so coming back to the tab from the bar picks it up again.

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

	/// A thing kept to hand on the bar: a tool, or a tool with what it makes (a unit to spawn, a bunker piece to build).
	struct Pin {
		Tool Kind = Tool::None;
		std::string PresetName; //!< Empty for a tool alone.
	};
	std::vector<Pin> s_Pins;

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
	}

	/// Things marked as favourites (Ctrl+click on a tile): a star on the tile, and a filter to list only them.
	std::vector<Pin> s_Favourites;

	int FindFavourite(Tool kind, const std::string& presetName) {
		for (size_t i = 0; i < s_Favourites.size(); ++i) {
			if (s_Favourites[i].Kind == kind && s_Favourites[i].PresetName == presetName) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	/// Favourites are the player's, whatever game is played, and are kept in their own file the moment they change (the settings file is
	/// only written when asked to be, and favourites marked in a game were lost at the next start).
	constexpr const char* c_FavouritesFile = "Userdata/SandboxFavourites.txt";

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
			if (ImGui::InvisibleButton("##tool", ImVec2(width, height))) {
				TookTool(index);
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
			DrawIcon(drawList, look.Art, ImVec2(std::floor(at.x + (width - pixel * 12.0F) * 0.5F), at.y + pad), pixel, look.Color);
			if (FindPin(kind, "") >= 0) {
				DrawPinMark(drawList, at, to);
			}
			const char* name = c_Tools[index].Name;
			float wrap = width - pad;
			ImVec2 nameSize = ImGui::CalcTextSize(name, nullptr, false, wrap);
			ImGui::PushClipRect(at, to, true);
			drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(std::floor(at.x + std::max((width - nameSize.x) * 0.5F, pad * 0.5F)), at.y + pad + pixel * 12.0F + ToolUI::Pixel()), ImGui::GetColorU32(selected ? ImGuiCol_SliderGrab : ImGuiCol_Text), name, nullptr, wrap);
			ImGui::PopClipRect();
			ImGui::PopID();
		}
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

	void PresetList(Tool kind, const char* group = nullptr, float rows = 8.0F) {
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

	void LoadoutChooser(const char* label = "Loadout") {
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

	/// A bunker piece as a picture ImGui can draw: its background and foreground art put together.
	struct PiecePicture {
		unsigned int Texture = 0;
		int Width = 0;
		int Height = 0;
		float OffsetX = 0.0F; //!< From the piece's position to the picture's top left corner.
		float OffsetY = 0.0F;
	};

	std::map<std::string, PiecePicture> s_PresetPictures; //!< Pictures made by PictureOf, by preset.
	std::map<std::string, PiecePicture> s_FilePictures; //!< Pictures made by PictureOfFile, by file.

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
		std::string key = preset.ClassName + "/" + preset.Module + "/" + preset.PresetName;
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

	/// A list of presets to pick from as a grid of their pictures, each with its name under it. For things whose look is what you choose them by.
	bool s_ShowModded = true; //!< Whether things from mods are listed at all.
	bool s_FavouritesOnly = false; //!< Whether only favourites are listed.
	std::map<Tool, std::string> s_KindFilter; //!< Per tool, the subcategory listed ("" for all).
	std::map<Tool, std::string> s_ModFilter; //!< Per tool, the module listed ("" for all).

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

	void PictureGrid(Tool kind, const char* group) {
		LoadFavouritesFile();
		const std::vector<Preset>& list = ListFor(kind);
		int& choice = ChoiceFor(kind);
		char* filter = FilterFor(kind, true);
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
				ChoiceCombo("##kind", s_KindFilter[kind], kinds);
				ImGui::SetItemTooltip("The kind of thing listed.");
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(third);
			ChoiceCombo("##mod", s_ModFilter[kind], mods);
			ImGui::SetItemTooltip("Only things from this module (faction or mod).");
		}
		const ImGuiStyle& style = ImGui::GetStyle();
		float cell = ImGui::GetFontSize() * 6.0F;
		float labelHeight = ImGui::GetTextLineHeight() * 2.0F;
		ImGui::BeginChild("##pictures", ImVec2(-1.0F, std::max(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 6.5F, cell * 2.5F)), ImGuiChildFlags_Borders);
		int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / (cell + style.ItemSpacing.x)));
		int shown = 0;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (int i = 0; i < static_cast<int>(list.size()); ++i) {
			const Preset& preset = list[i];
			if (!ContainsIgnoringCase(preset.Label, filter) || (group && preset.Group != group)) {
				continue;
			}
			if ((!s_ShowModded && preset.Modded) || (!s_KindFilter[kind].empty() && preset.Kind != s_KindFilter[kind]) || (!s_ModFilter[kind].empty() && preset.Module != s_ModFilter[kind])) {
				continue;
			}
			if (s_FavouritesOnly && FindFavourite(kind, preset.PresetName) < 0) {
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
				bool selected = i == choice;
				// In the colours of the game's own menus: olive cells, the picked one brighter with a gold edge.
				drawList->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), selected ? IM_COL32(85, 96, 68, 255) : hovered ? IM_COL32(57, 75, 42, 255) : IM_COL32(24, 29, 21, 255));
				drawList->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), selected ? IM_COL32(242, 182, 61, 255) : IM_COL32(60, 70, 48, 255), 0.0F, 0, selected ? 2.0F : 1.0F);
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
				drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + std::max((cell - nameSize.x) * 0.5F, 2.0F), at.y + cell), IM_COL32(230, 232, 238, 255), preset.PresetName.c_str(), nullptr, cell - 4.0F);
				ImGui::PopClipRect();
			}
			if (hovered) {
				std::string size = preset.Width > 0 ? "\n" + std::to_string(preset.Width) + " x " + std::to_string(preset.Height) + " pixels" : "";
				ImGui::SetTooltip("%s\n%s%s%s\nCtrl+click: a favourite, or not", preset.PresetName.c_str(), preset.Module.c_str(), size.c_str(), Sandbox::IsGodMode() ? "\nRight click: keep it on the bar, or take it off" : "");
			}
			if (picked && ImGui::GetIO().KeyCtrl) {
				ToggleFavourite(kind, preset.PresetName);
			} else if (picked) {
				choice = i;
				TookTool(ToolIndex(kind));
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && Sandbox::IsGodMode()) {
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

	ImVec2 ToScreen(const Vector& scenePosition);

	void DrawCursor() {
		ImGuiIO& io = ImGui::GetIO();
		const ToolInfo& tool = CurrentTool();
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		ImU32 white = IM_COL32(255, 255, 255, 170);
		std::string label = tool.Name;
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
			Vector corner = g_SceneMan.ShortestDistance(g_CameraMan.GetOffset(0), ground + Vector(-static_cast<float>(type.Width / 2), 1.0F - static_cast<float>(type.Height)), g_SceneMan.SceneWrapsX());
			ImVec2 topLeft(ViewOrigin().x + corner.m_X / scale, ViewOrigin().y + corner.m_Y / scale);
			drawList->AddRect(topLeft, ImVec2(topLeft.x + static_cast<float>(type.Width) / scale, topLeft.y + static_cast<float>(type.Height) / scale), c_SideColors[s_Team], 0.0F, 0, 1.5F);
		} else if (tool.Kind == Tool::Structure) {
			if (const Preset* preset = ChosenPreset(Tool::Structure, s_StructureChoice)) {
				// The piece itself, see-through, exactly where a click will put it, with its outline.
				const PiecePicture& picture = PictureOf(*preset);
				if (picture.Width > 0) {
					Vector corner = g_SceneMan.ShortestDistance(g_CameraMan.GetOffset(0), StructurePosition(*preset, MouseScenePosition(), s_SnapToGrid) + Vector(picture.OffsetX, picture.OffsetY), g_SceneMan.SceneWrapsX());
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
			drawList->AddCircle(io.MousePos, std::max(outline, 3.0F), tool.Kind == Tool::Unit || tool.Kind == Tool::Brain || tool.Kind == Tool::RallyPoint ? c_SideColors[s_Team] : white, 0, 1.5F);
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
		if (tool.Kind == Tool::OrderMove) {
			// Where each unit will stand: a marker on the ground for every one, so the order can be seen before it is given.
			std::vector<Actor*> units = UnitsToMove(s_Team, false);
			for (const Vector& spot: StandingSpots(MouseScenePosition(), static_cast<int>(units.size()))) {
				flag(spot, c_SideColors[s_Team]);
			}
			label = units.empty() ? std::string(c_SideNames[s_Team]) + " has no units to move" : std::to_string(units.size()) + (units.size() == 1 ? " unit will come here" : " units will come here");
		} else if (tool.Kind == Tool::Command) {
			// What the click will do, in the mode's own colour and marks.
			Vector point = MouseScenePosition();
			Actor* under = dynamic_cast<Actor*>(ObjectUnder(point, true));
			bool underIsUnit = under && IsCombatant(under) && !under->IsInGroup("Brains");
			bool underIsFriend = underIsUnit && (s_Selected.empty() || under->GetTeam() == SelectionTeam());
			std::vector<Actor*> units = UnitsToMove(0, true);
			std::string count = std::to_string(units.size()) + (units.size() == 1 ? " unit" : " units");
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
					if (target || !IsCombatant(actor) || actor->IsIgnoredByAI() || actor->GetTeam() == SelectionTeam()) {
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
						if (IsCombatant(actor) && !actor->IsIgnoredByAI() && actor->GetTeam() != SelectionTeam() && g_SceneMan.ShortestDistance(point, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude() <= nearest) {
							target = actor;
						}
					}
				}
				if (target) {
					crosshair(target->GetPos(), red, std::max(target->GetRadius() / scale, 8.0F) + pixel * 3.0F);
					drawList->AddLine(io.MousePos, ToScreen(target->GetPos()), (red & 0x00FFFFFF) | (120u << IM_COL32_A_SHIFT), pixel);
					label = "Attack " + target->GetPresetName() + " with " + count;
				} else {
					crosshair(point, red, pixel * 6.0F);
					label = count + " attack towards here (no enemy near)";
				}
			} else if (s_CommandMode == CommandMode::Guard) {
				ImU32 green = IM_COL32(120, 220, 120, 255);
				if (underIsFriend) {
					ImVec2 at = ToScreen(under->GetPos());
					float reach = std::max(under->GetRadius() / scale, 8.0F) + pixel * 3.0F;
					drawList->AddCircle(at, reach, green, 0, pixel * 1.5F);
					drawList->AddCircle(at, reach + pixel * 3.0F, (green & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT), 0, pixel);
					label = count + " guard " + under->GetPresetName();
				} else {
					label = "Guard: point at a friendly unit for " + count + " to stay with";
				}
			} else {
				for (const Vector& spot: StandingSpots(point, static_cast<int>(units.size()))) {
					flag(spot, IM_COL32(110, 180, 250, 255));
				}
				label = "Move " + count + " here";
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

	/// One choice on a ring.
	struct RingItem {
		const char* Label;
		ImU32 Color;
		const char* Icon = nullptr; //!< One of the game's pie menu icons (Base.rte/GUIs/PieMenus/PieIcons/<Icon>000.png).
	};

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
	int DrawRing(const std::vector<RingItem>& items, int current, bool sticky = false) {
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
			const PiecePicture* picture = items[i].Icon ? &PictureOfFile(std::string("Base.rte/GUIs/PieMenus/PieIcons/") + items[i].Icon + "000.png") : nullptr;
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

	/// The rings the right button opens, by the tool in hand: the sides for anything made for a side, the commands for the command tool.
	void DrawSideRing() {
		ImGuiIO& io = ImGui::GetIO();
		Tool kind = CurrentTool().Kind;
		bool hasRing = TakesSide(kind) || kind == Tool::Command;
		if (!s_RingOpen) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse && hasRing) {
				s_RingOpen = true;
				s_RingPage = 0;
				s_RingCenter = io.MousePos;
				s_RingScenePoint = MouseScenePosition();
			}
			return;
		}
		if (kind == Tool::Command && s_RingPage == 1) {
			// The game's own AI modes for the units picked, as the pie menu offers them when playing a unit. Up until a click, since the button
			// that held the first ring open has been let go.
			static const std::vector<RingItem> modes = {{"Sentry", IM_COL32(242, 182, 61, 255), "Eye"}, {"Patrol", IM_COL32(120, 200, 220, 255), "Cycle"}, {"Hunt brains", IM_COL32(239, 106, 91, 255), "Brain"}, {"Dig for gold", IM_COL32(230, 200, 80, 255), "Dig"}, {"Rally point", IM_COL32(180, 140, 240, 255), "Flag"}, {"Do nothing", IM_COL32(150, 150, 140, 255), "Blank"}, {"Back", IM_COL32(110, 180, 250, 255), "Return"}};
			static const Order orders[] = {Order::Hold, Order::Patrol, Order::HuntBrains, Order::DigGold, Order::Rally, Order::Idle};
			int picked = DrawRing(modes, -1, true);
			if (picked == -2) {
				return;
			}
			if (picked >= 0 && picked < 6) {
				Stroke stroke;
				stroke.Kind = Tool::OrderSelected;
				stroke.Position = s_RingScenePoint;
				stroke.Orders = orders[picked];
				s_Queue.push_back(stroke);
			} else if (picked == 6) {
				s_RingOpen = true;
				s_RingPage = 2;
			}
			return;
		}
		if (kind == Tool::Command) {
			static const std::vector<RingItem> commands = {{"Move", IM_COL32(110, 180, 250, 255), "GoTo"}, {"Attack", IM_COL32(239, 106, 91, 255), "Death"}, {"Guard", IM_COL32(120, 220, 120, 255), "Follow"}, {"Defend", IM_COL32(242, 182, 61, 255), "Eye"}, {"Cancel", IM_COL32(200, 160, 120, 255), "Cancel"}, {"Deselect", IM_COL32(150, 150, 140, 255), "Remove"}, {"More...", IM_COL32(200, 200, 200, 255), "SubPieMenu1"}};
			int picked = DrawRing(commands, static_cast<int>(s_CommandMode), s_RingPage == 2);
			if (picked == -2) {
				return;
			}
			if (picked >= 0 && picked <= 2) {
				// The mode for the clicks to come.
				s_CommandMode = static_cast<CommandMode>(picked);
			} else if (picked == 3 || picked == 4) {
				// Defend where they stand (3), or cancel their orders (4).
				Stroke stroke;
				stroke.Kind = Tool::OrderSelected;
				stroke.Position = s_RingScenePoint;
				stroke.Count = 100 + (picked == 3 ? 3 : 2);
				s_Queue.push_back(stroke);
			} else if (picked == 5) {
				s_Selected.clear();
			} else if (picked == 6) {
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

#pragma region Gym
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
	std::vector<GymCourse> s_GymCourses;
	std::string s_GymScene; //!< The scene the courses were loaded for.
	std::vector<GymRun> s_GymRuns;
	bool s_GymReported = true;
	char s_GymName[64] = "";
	char s_GymSettings[1024] = ""; //!< The gym's own settings for this map, "Key = Value" a line, as in Settings.ini.
	constexpr float c_GymArrivedWithin = 40.0F;
	constexpr double c_GymGiveUpMS = 60000.0;
	constexpr const char* c_GymUnitTag = "GymUnit";

	std::string GymSceneName() {
		return g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetPresetName() : "";
	}

	std::string GymFile() {
		return "Userdata/Gyms/" + GymSceneName() + ".txt";
	}

	std::string GymSettingsFile() {
		return "Userdata/Gyms/" + GymSceneName() + ".settings.txt";
	}

	/// Puts the gym's settings into the game as the settings file would (TerrainCollapse = 0 keeps the buildings up, say). They are also
	/// read into the settings the map is opened with by Tools/RenderTest/Gym.ps1, so they hold from the start.
	void GymApplySettings() {
		std::istringstream lines(s_GymSettings);
		std::string line;
		while (std::getline(lines, line)) {
			size_t equals = line.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			auto trim = [](std::string text) {
				size_t from = text.find_first_not_of(" \t\r");
				size_t to = text.find_last_not_of(" \t\r");
				return from == std::string::npos ? std::string() : text.substr(from, to - from + 1);
			};
			std::string key = trim(line.substr(0, equals));
			std::string value = trim(line.substr(equals + 1));
			if (key.empty()) {
				continue;
			}
			Reader reader(std::make_unique<std::istringstream>(value), "gym settings");
			g_SettingsMan.ReadProperty(key, reader);
		}
	}

	void GymLoadSettings() {
		s_GymSettings[0] = 0;
		std::ifstream file(GymSettingsFile());
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		std::strncpy(s_GymSettings, text.c_str(), sizeof(s_GymSettings) - 1);
		s_GymSettings[sizeof(s_GymSettings) - 1] = 0;
		if (s_GymSettings[0]) {
			GymApplySettings();
		}
	}

	void GymSaveSettings() {
		std::filesystem::create_directories("Userdata/Gyms");
		std::ofstream file(GymSettingsFile());
		file << s_GymSettings;
	}

	void GymLoad() {
		std::string scene = GymSceneName();
		if (scene == s_GymScene) {
			return;
		}
		s_GymScene = scene;
		s_GymCourses.clear();
		s_GymRuns.clear();
		GymLoadSettings();
		std::ifstream file(GymFile());
		std::string line;
		while (std::getline(file, line)) {
			size_t first = line.find('|');
			if (first == std::string::npos) {
				continue;
			}
			GymCourse course;
			course.Name = line.substr(0, first);
			float x1 = 0.0F;
			float y1 = 0.0F;
			float x2 = 0.0F;
			float y2 = 0.0F;
			if (std::sscanf(line.c_str() + first + 1, "%f,%f|%f,%f", &x1, &y1, &x2, &y2) == 4) {
				course.From = Vector(x1, y1);
				course.To = Vector(x2, y2);
				s_GymCourses.push_back(course);
			}
		}
	}

	void GymSave() {
		std::filesystem::create_directories("Userdata/Gyms");
		std::ofstream file(GymFile());
		for (const GymCourse& course: s_GymCourses) {
			file << course.Name << '|' << static_cast<int>(course.From.m_X) << ',' << static_cast<int>(course.From.m_Y) << '|' << static_cast<int>(course.To.m_X) << ',' << static_cast<int>(course.To.m_Y) << '\n';
		}
	}

	/// A standing spot for a unit near a clicked point: down out of any wall or slab the click landed in, then down to the floor, and up
	/// off it by a fifth of the body. (A click a few pixels into a bunker's floor put the unit inside the floor.)
	Vector GymSettle(const Vector& point, float height) {
		int x = static_cast<int>(point.m_X);
		int y = static_cast<int>(point.m_Y);
		int limit = 0;
		while (g_SceneMan.GetTerrMatter(x, y) != MaterialColorKeys::g_MaterialAir && limit++ < 200) {
			++y;
		}
		limit = 0;
		while (g_SceneMan.GetTerrMatter(x, y) == MaterialColorKeys::g_MaterialAir && limit++ < 400) {
			++y;
		}
		return Vector(static_cast<float>(x), static_cast<float>(y) - height * 0.2F);
	}

	/// Starts a run of one course. @return Whether a unit was put down for it.
	bool GymRunCourse(int index) {
		if (index < 0 || index >= static_cast<int>(s_GymCourses.size())) {
			return false;
		}
		const Preset* preset = ChosenPreset(Tool::Unit, s_UnitChoice);
		if (!preset) {
			return false;
		}
		Actor* unit = CreateUnit(*preset, s_Team, s_Loadout, Order::Hold);
		if (!unit) {
			return false;
		}
		GymCourse& course = s_GymCourses[index];
		Vector from = GymSettle(course.From, unit->GetHeight());
		Vector to = GymSettle(course.To, unit->GetHeight());
		unit->SetPos(from);
		unit->SetNumberValue(c_GymUnitTag, 1.0);
		if (g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::AI)) {
			unit->SetNumberValue("AITrace", 1.0);
		}
		g_MovableMan.AddActor(unit);
		unit->ClearAIWaypoints();
		unit->AddAISceneWaypoint(to);
		unit->SetAIMode(Actor::AIMODE_GOTO);
		GymRun run;
		run.Course = index;
		run.Unit = MakeRef(unit);
		run.LastPos = from;
		run.Goal = to;
		s_GymRuns.push_back(run);
		course.Result = "running";
		s_GymReported = false;
		g_ConsoleMan.PrintString("GYM " + course.Name + ": started, " + preset->PresetName + " from " + std::to_string(static_cast<int>(course.From.m_X)) + "," + std::to_string(static_cast<int>(course.From.m_Y)) + " to " + std::to_string(static_cast<int>(course.To.m_X)) + "," + std::to_string(static_cast<int>(course.To.m_Y)));
		return true;
	}

	/// Starts every course of this map at once. @return Whether there were any.
	bool GymRunAll() {
		GymLoad();
		bool any = false;
		for (int i = 0; i < static_cast<int>(s_GymCourses.size()); ++i) {
			any = GymRunCourse(i) || any;
		}
		return any;
	}

	void GymRemoveUnits() {
		for (Actor* actor: SandboxAccess::Actors()) {
			if (actor->NumberValueExists(c_GymUnitTag)) {
				actor->SetToDelete(true);
			}
		}
		s_GymRuns.clear();
	}

	/// Watches the runs: arrived within reach of the goal, dead, or given up after a minute, and how long it stood still.
	void GymUpdate() {
		bool allDone = true;
		for (GymRun& run: s_GymRuns) {
			if (run.Done) {
				continue;
			}
			GymCourse& course = s_GymCourses[run.Course];
			Actor* unit = GetRef(run.Unit);
			std::string seconds = std::to_string(static_cast<int>(run.Clock.GetElapsedSimTimeMS() / 100) / 10.0).substr(0, 4);
			if (!unit || unit->GetHealth() <= 0.0F) {
				course.Result = "died after " + seconds + " s";
				run.Done = true;
			} else {
				if (run.StillClock.IsPastSimMS(1000)) {
					run.StillClock.Reset();
					if (g_SceneMan.ShortestDistance(run.LastPos, unit->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(4.0F)) {
						++run.Still;
					}
					run.LastPos = unit->GetPos();
				}
				float left = g_SceneMan.ShortestDistance(unit->GetPos(), run.Goal, g_SceneMan.SceneWrapsX()).GetMagnitude();
				if (left < c_GymArrivedWithin) {
					course.Result = "arrived in " + seconds + " s, stood still " + std::to_string(run.Still) + " s";
					run.Done = true;
				} else if (run.Clock.IsPastSimMS(c_GymGiveUpMS)) {
					course.Result = "gave up after 60 s, " + std::to_string(static_cast<int>(left)) + " px short at " + std::to_string(static_cast<int>(unit->GetPos().m_X)) + "," + std::to_string(static_cast<int>(unit->GetPos().m_Y)) + ", stood still " + std::to_string(run.Still) + " s";
					run.Done = true;
				}
			}
			if (run.Done) {
				g_ConsoleMan.PrintString("GYM " + course.Name + ": " + course.Result);
			} else {
				allDone = false;
			}
		}
		if (allDone && !s_GymReported) {
			s_GymReported = true;
			g_ConsoleMan.PrintString("GYM done");
		}
	}

	/// The courses' starts and goals over the map, while the Gym tab is open.
	void DrawGym(ImDrawList* drawList) {
		auto mark = [&](const Vector& at, ImU32 color, bool goal) {
			ImVec2 on = ToScreen(at);
			if (goal) {
				drawList->AddTriangleFilled(ImVec2(on.x, on.y - 10.0F), ImVec2(on.x + 7.0F, on.y + 2.0F), ImVec2(on.x - 7.0F, on.y + 2.0F), color);
			} else {
				drawList->AddCircleFilled(on, 5.0F, color);
			}
		};
		for (const GymCourse& course: s_GymCourses) {
			drawList->AddLine(ToScreen(course.From), ToScreen(course.To), IM_COL32(255, 255, 255, 50), 1.0F);
			mark(course.From, IM_COL32(120, 220, 120, 220), false);
			mark(course.To, IM_COL32(242, 182, 61, 220), true);
		}
		if (s_GymFromSet) {
			mark(s_GymFrom, IM_COL32(120, 220, 120, 255), false);
		}
		if (s_GymToSet) {
			mark(s_GymTo, IM_COL32(242, 182, 61, 255), true);
		}
	}

	void GymTab() {
		GymLoad();
		ImGui::TextWrapped("Courses for the AI on this map. Click a start and a goal on the map with the two tools, name the course and add it. A run puts a unit of the kind picked under Spawn (that side, that loadout) at the start, sends it to the goal and times it. The courses are kept in Userdata/Gyms, and the test harness can run them too.");
		ToolButtons({Tool::GymStart, Tool::GymGoal});
		ImGui::Text("Start: %s", s_GymFromSet ? (std::to_string(static_cast<int>(s_GymFrom.m_X)) + "," + std::to_string(static_cast<int>(s_GymFrom.m_Y))).c_str() : "(click with the start tool)");
		ImGui::Text("Goal: %s", s_GymToSet ? (std::to_string(static_cast<int>(s_GymTo.m_X)) + "," + std::to_string(static_cast<int>(s_GymTo.m_Y))).c_str() : "(click with the goal tool)");
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::InputTextWithHint("##gymname", "Course name", s_GymName, sizeof(s_GymName));
		if (ToolUI::Button("Add course", ImVec2(-1.0F, 0.0F)) && s_GymFromSet && s_GymToSet) {
			GymCourse course;
			course.Name = s_GymName[0] ? s_GymName : ("Course " + std::to_string(s_GymCourses.size() + 1));
			course.From = s_GymFrom;
			course.To = s_GymTo;
			s_GymCourses.push_back(course);
			GymSave();
			s_GymName[0] = 0;
			s_GymFromSet = false;
			s_GymToSet = false;
		}
		ImGui::Separator();
		if (ToolUI::Button("Run all", ImVec2(ImGui::GetContentRegionAvail().x * 0.5F - ImGui::GetStyle().ItemSpacing.x * 0.5F, 0.0F))) {
			QueueSimChange(Tool::GymRun, -1);
		}
		ImGui::SameLine();
		if (ToolUI::Button("Remove gym units", ImVec2(-1.0F, 0.0F))) {
			QueueSimChange(Tool::GymRemove);
		}
		int remove = -1;
		for (int i = 0; i < static_cast<int>(s_GymCourses.size()); ++i) {
			GymCourse& course = s_GymCourses[i];
			ImGui::PushID(i);
			ImGui::Separator();
			ImGui::Text("%s", course.Name.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Run")) {
				QueueSimChange(Tool::GymRun, i);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("X")) {
				remove = i;
			}
			ImGui::TextDisabled("%d,%d to %d,%d", static_cast<int>(course.From.m_X), static_cast<int>(course.From.m_Y), static_cast<int>(course.To.m_X), static_cast<int>(course.To.m_Y));
			if (!course.Result.empty()) {
				ImGui::TextWrapped("%s", course.Result.c_str());
			}
			ImGui::PopID();
		}
		if (remove >= 0) {
			s_GymCourses.erase(s_GymCourses.begin() + remove);
			s_GymRuns.clear();
			GymSave();
		}
		ImGui::Separator();
		ImGui::TextWrapped("Settings for this gym, a \"Key = Value\" a line as in Settings.ini (TerrainCollapse = 0 keeps the buildings up). They take effect here when applied, and whenever the map is opened through Tools/RenderTest/Gym.ps1.");
		ImGui::InputTextMultiline("##gymsettings", s_GymSettings, sizeof(s_GymSettings), ImVec2(-1.0F, ImGui::GetTextLineHeight() * 5.0F));
		if (ToolUI::Button("Apply and save settings", ImVec2(-1.0F, 0.0F))) {
			GymApplySettings();
			GymSaveSettings();
		}
	}
#pragma endregion

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
					building.Orders = std::clamp(building.Orders, 0, c_LastUnitOrder);
					ImGui::Combo("Their orders", &building.Orders, c_UnitOrderNames);
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

	/// Selected units are marked by the game's own selection arrow (drawn by the unit's HUD, with a glow), and the followed one by a marker.
	std::vector<UnitRef> s_MarkedSelected; //!< The units carrying the arrow last time, so it can be taken off them.
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
			Vector onScreen = g_SceneMan.ShortestDistance(g_CameraMan.GetOffset(0), s_RallyPoints[side], g_SceneMan.SceneWrapsX());
			ImVec2 base(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
			drawList->AddLine(base, ImVec2(base.x, base.y - 26.0F), IM_COL32(230, 230, 230, 220), 2.0F);
			drawList->AddTriangleFilled(ImVec2(base.x, base.y - 26.0F), ImVec2(base.x + 16.0F, base.y - 21.0F), ImVec2(base.x, base.y - 16.0F), c_SideColors[side]);
		}
	}

	void SideStatus() {
		// The fighting units each side has, as the auto battle counts them (Sandbox::CountUnits): not brains or craft, but a craft's passengers.
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
		// The order labels overlay adds how each auto battle side stands.
		if (g_SettingsMan.ShowOrderLabels() && s_AutoRunning) {
			long long now = g_TimerMan.GetSimUpdateCount();
			for (int side = 0; side < c_Sides; ++side) {
				const AutoSide& autoSide = s_AutoSides[side];
				if (!autoSide.Active) {
					continue;
				}
				std::string wave = autoSide.Broke ? std::string("broke") : "next wave " + std::to_string(std::max(0LL, autoSide.NextWave - now) / 60) + "s";
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c_SideColors[side]), "%s: budget %d, spent %.0f, sent %d, %s", c_SideNames[side], autoSide.Budget, autoSide.Spent, autoSide.Sent, wave.c_str());
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
	}

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
		ImU32 well = selected ? IM_COL32(105, 121, 71, 255) : hovered ? IM_COL32(68, 82, 54, 255) : IM_COL32(30, 37, 26, 255);
		drawList->AddRectFilled(at, to, well);
		if (selected) {
			drawList->AddRect(at, to, IM_COL32(242, 182, 61, 255), 0.0F, 0, pixel);
			drawList->AddRectFilled(ImVec2(at.x + pixel, at.y + pixel), ImVec2(to.x - pixel, at.y + pixel * 2.0F), IM_COL32(255, 240, 180, 90));
		} else {
			drawList->AddRectFilled(at, ImVec2(to.x, at.y + pixel), IM_COL32(0, 0, 0, 110));
			drawList->AddRectFilled(at, ImVec2(at.x + pixel, to.y), IM_COL32(0, 0, 0, 110));
			drawList->AddRectFilled(ImVec2(at.x, to.y - pixel), to, IM_COL32(255, 240, 180, 24));
		}
		drawPicture(drawList, ImVec2(at.x + pad, at.y + pad), picture);
		if (hovered && tip && *tip) {
			ImGui::SetTooltip("%s", tip);
		}
		return result;
	}

	/// A thin upright gold rule between groups of tiles on the bar.
	void BarDivider() {
		ImGui::SameLine(0.0F, ToolUI::Pixel() * 4.0F);
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
		drawList->PathFillConvex(IM_COL32(14, 17, 12, 230));
		shape(0.0F);
		drawList->PathFillConvex(IM_COL32(44, 53, 37, 245));
		shape(pixel);
		drawList->PathStroke(IM_COL32(170, 128, 48, 255), ImDrawFlags_Closed, pixel);
		// A faint lighter band along the top, as a lip.
		drawList->AddRectFilled(ImVec2(at.x + cut, at.y + pixel), ImVec2(to.x - cut, at.y + pixel * 2.0F), IM_COL32(255, 240, 180, 30));
		// Studs in the corners.
		for (ImVec2 corner: {ImVec2(at.x + cut, at.y + cut), ImVec2(to.x - cut, at.y + cut), ImVec2(at.x + cut, to.y - cut), ImVec2(to.x - cut, to.y - cut)}) {
			drawList->AddRectFilled(ImVec2(corner.x - pixel, corner.y - pixel), ImVec2(corner.x + pixel, corner.y + pixel), IM_COL32(242, 182, 61, 160));
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
			ImGui::SliderInt("##brush", &s_Radius, 1, 40, "Brush %d px");
			ImGui::SameLine();
			for (const auto& [label, size]: {std::pair<const char*, int>{"S", 4}, {"M", 10}, {"L", 24}}) {
				if (ToolUI::SmallButton(label)) {
					s_Radius = size;
				}
				ImGui::SameLine();
			}
			ImGui::NewLine();
		} else if (tool.Kind == Tool::Unit || tool.Kind == Tool::Drop) {
			const Preset* preset = ChosenPreset(tool.Kind, ChoiceFor(tool.Kind));
			start(preset ? preset->PresetName.c_str() : tool.Name);
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
			// The mode of the clicks, in its colours.
			for (int mode = 0; mode < 3; ++mode) {
				if (mode > 0) {
					ImGui::SameLine();
				}
				static const ImU32 modeColors[] = {IM_COL32(110, 180, 250, 255), IM_COL32(239, 106, 91, 255), IM_COL32(120, 220, 120, 255)};
				ImGui::PushStyleColor(ImGuiCol_Text, modeColors[mode]);
				int current = static_cast<int>(s_CommandMode);
				if (ToolUI::RadioButton(c_CommandModeNames[mode], &current, mode)) {
					s_CommandMode = static_cast<CommandMode>(current);
				}
				ImGui::PopStyleColor();
			}
			ImGui::SameLine(0.0F, pixel * 6.0F);
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
			ImGui::TextDisabled("%s", what.c_str());
			ImGui::SameLine();
			ImGui::BeginDisabled(alive == 0);
			if (ToolUI::SmallButton("Deselect")) {
				s_Selected.clear();
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
			ImGui::BeginDisabled(alive == 0);
			ImGui::SameLine();
			if (ToolUI::SmallButton("Follow")) {
				s_FollowTarget = s_Selected.empty() ? UnitRef() : s_Selected.front();
				s_FollowAction = false;
			}
			ImGui::EndDisabled();
			ImGui::SameLine(0.0F, pixel * 6.0F);
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderFloat("##spacing", &s_Spacing, 8.0F, 60.0F, "Spacing %.0f px");
			ImGui::SetItemTooltip("How far apart units stand when sent somewhere together.\nDrag a box to select; Shift+click adds a unit, or on the ground queues another place to go on to; double click takes all of a kind in sight; Ctrl+A everyone on the side.\nCtrl+number keeps the selection, the number brings it back. Hold the right button over the world for the ring.");
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
		    {"Spawn", Icon::Person, IM_COL32(232, 224, 190, 255), "Spawn: units, squads dropped from orbit, brains and items"},
		    {"Build", Icon::Wall, IM_COL32(170, 170, 165, 255), "Build: bunker pieces, placed straight into the world"},
		    {"Paint", Icon::Drop, IM_COL32(90, 170, 240, 255), "Paint: fire, liquids, smoke, loose and solid ground"},
		    {"Boom", Icon::Bomb, IM_COL32(239, 106, 91, 255), "Boom: blasts, strikes from the sky, and things to knock down"},
		    {"Effects", Icon::Star, IM_COL32(255, 220, 120, 255), "Effects: lights and particle effects to put down"},
		    {"Orders", Icon::Flag, IM_COL32(242, 182, 61, 255), "Orders: orders for whole sides, and auto battles"},
		    {"World", Icon::Cloud, IM_COL32(190, 190, 190, 255), "World: time, weather, the speed of the world, the camera"},
		    {"You", Icon::Person, IM_COL32(130, 220, 120, 255), "You: your own character, what it is, carries and can do"},
		};
		static const Tool mainTools[] = {Tool::None, Tool::Command, Tool::Follow, Tool::Possess, Tool::Remove, Tool::RallyPoint};

		ImGui::SetNextWindowPos(ImVec2(view.x + view.w * 0.5F, view.y + view.h - pixel * 6.0F), ImGuiCond_Always, ImVec2(0.5F, 1.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(8.0F, 8.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pixel * 8.0F, pixel * 5.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pixel * 2.0F, pixel * 3.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
		if (ImGui::Begin("##SandboxBar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
			BarPlate();
			// The settings of the tool in hand, in a row of their own at the top.
			if (ContextRow()) {
				ImVec2 at = ImGui::GetCursorScreenPos();
				float width = ImGui::GetContentRegionAvail().x;
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + pixel), ImVec2(at.x + width, at.y + pixel * 2.0F), IM_COL32(170, 128, 48, 160));
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
			}
			if (!s_Pins.empty()) {
				// A gold rule between the pins and the rest.
				ImVec2 at = ImGui::GetCursorScreenPos();
				float width = ImGui::GetContentRegionAvail().x;
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + pixel), ImVec2(at.x + width, at.y + pixel * 2.0F), IM_COL32(170, 128, 48, 160));
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
					    drawList->AddRect(ImVec2(at.x + inset, at.y + inset), ImVec2(at.x + room - inset, at.y + room - inset), IM_COL32(20, 24, 16, 255), 0.0F, 0, pixel);
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
						static const std::map<std::string, Tool> firstTools = {{"Spawn", Tool::Unit}, {"Build", Tool::Structure}, {"Paint", Tool::Fire}, {"Boom", Tool::Grenade}, {"Effects", Tool::Effect}, {"Orders", Tool::Command}, {"You", Tool::PlayCharacter}};
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
	}
#pragma endregion
} // namespace

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
	stroke.Orders = static_cast<Order>(std::clamp(order, 0, c_LastUnitOrder));
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
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	s_AutoCenter = g_CameraMan.GetOffset(0) + Vector(static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F, static_cast<float>(g_FrameMan.GetPlayerScreenHeight()) * 0.5F);
	s_AutoLaneWidth = static_cast<float>(g_FrameMan.GetPlayerScreenWidth());
	long long now = g_TimerMan.GetSimUpdateCount();
	for (int side = 0; side < c_Sides; ++side) {
		AutoSide& autoSide = s_AutoSides[side];
		autoSide.Spent = 0.0F;
		autoSide.Sent = 0;
		autoSide.Broke = false;
		// Staggered, so the first ships don't all arrive at once.
		autoSide.NextWave = now + side * 60;
	}
	s_AutoWinner = -2;
	s_AutoRunning = true;
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
	Actor* actor = preset ? CreateUnit(*preset, team, 0, static_cast<Order>(std::clamp(order, 0, c_LastUnitOrder))) : nullptr;
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
	// Control groups: Ctrl and a number keeps the selection under it, the number alone brings it back; Ctrl+A takes the whole side. With the
	// command tool in hand, wherever the pointer is, so long as no text box has the keys. (Only while the pointer was over the world, as
	// these were, they did nothing with it resting on the window.)
	if (InGame() && CurrentTool().Kind == Tool::Command && !io.WantTextInput) {
		for (int number = 0; number < 10; ++number) {
			if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + number), false)) {
				if (io.KeyCtrl) {
					s_Groups[number] = s_Selected;
				} else if (!s_Groups[number].empty()) {
					s_Selected = s_Groups[number];
				}
			}
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
			// Drag a box to select units; click the ground to send them there, or an enemy to attack it.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
		ImGui::GetForegroundDrawList()->AddRect(ImVec2(std::min(s_DragStart.x, now.x), std::min(s_DragStart.y, now.y)), ImVec2(std::max(s_DragStart.x, now.x), std::max(s_DragStart.y, now.y)), IM_COL32(255, 255, 255, 200), 0.0F, 0, 1.5F);
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			s_Dragging = false;
			Stroke stroke;
			float scale = ScenePixelsPerWindowPixel();
			Vector start = g_CameraMan.GetOffset(0) + Vector(s_DragStart.x - ViewOrigin().x, s_DragStart.y - ViewOrigin().y) * scale;
			Vector end = g_CameraMan.GetOffset(0) + Vector(now.x - ViewOrigin().x, now.y - ViewOrigin().y) * scale;
			if (std::abs(now.x - s_DragStart.x) + std::abs(now.y - s_DragStart.y) > 8.0F) {
				stroke.Kind = Tool::Select;
				stroke.Position = start;
				stroke.Position2 = end;
			} else {
				stroke.Kind = Tool::Command;
				stroke.Position = end;
				g_SceneMan.WrapPosition(stroke.Position);
				stroke.Count = io.KeyShift ? 1 : (s_DoubleClick ? 2 : 0);
			}
			s_Queue.push_back(stroke);
		}
	}
	if (InGame()) {
		DrawSelection();
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
				ImGui::Combo("Orders", &s_Order, c_OrderNames);
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

				// Auto battles are still there for scripts (SandboxAutoBattleSide, SandboxStartAutoBattle); their controls were taken out of the window.
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
				ToolButtons({Tool::LooseSand, Tool::LooseSnow, Tool::Boulder, Tool::Slab});
				ImGui::SeparatorText("Terrain");
				ToolButtons({Tool::Dig, Tool::Earth, Tool::Sand, Tool::Ice, Tool::Grass, Tool::Wood, Tool::Concrete});
				ImGui::SliderInt("Brush size", &s_Radius, 1, 40);
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
				ImGui::Combo("Weather", &settings.WeatherType, "Clear\0Rain\0Snow\0Ash fall\0Dust storm\0");
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
	UpdateIncoming();
	UpdateEffects();
	for (const WaterSpawner& spawner: s_WaterSpawners) {
		FluidSim::Pour(spawner.Position, static_cast<float>(spawner.Radius), "Water");
	}
	// With the AI paused, the sandbox's own passes wait too: they re-sent attackers and walked defenders home once a second, and auto battle
	// kept dropping waves, all on units held still. (Its wave clocks are held back as well, so the waves don't all come at once after.)
	const bool aiPaused = Controller::IsAIPaused();
	if (!aiPaused && g_TimerMan.GetSimUpdateCount() % 60 == 0) {
		RetargetAttackers();
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
		} else if (actor->NumberValueExists(c_TargetTag)) {
			order = "attack #" + std::to_string(static_cast<long long>(actor->GetNumberValue(c_TargetTag)));
		} else if (actor->NumberValueExists(c_AttackXTag)) {
			order = "attack towards a place";
		} else if (actor->NumberValueExists(c_AttackTag)) {
			order = "attack nearest";
		} else if (actor->NumberValueExists(c_DefendXTag)) {
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

namespace {
	/// A dashed line between window positions, as the orders overlay draws an order waiting for the next update.
	void DashedLine(ImDrawList* drawList, const ImVec2& from, const ImVec2& to, ImU32 color, float thickness) {
		float dx = to.x - from.x;
		float dy = to.y - from.y;
		float length = std::sqrt(dx * dx + dy * dy);
		if (length < 1.0F || length > 6000.0F) {
			drawList->AddLine(from, to, color, thickness);
			return;
		}
		for (float t = 0.0F; t < length; t += 12.0F) {
			float end = std::min(t + 6.0F, length);
			drawList->AddLine(ImVec2(from.x + dx * t / length, from.y + dy * t / length), ImVec2(from.x + dx * end / length, from.y + dy * end / length), color, thickness);
		}
	}

	/// Whether a window position is on the game's picture, give or take a margin.
	bool OnPicture(const ImVec2& at, float margin) {
		GameViewRect view = g_WindowMan.GetGameViewRect();
		return at.x > view.x - margin && at.x < view.x + view.w + margin && at.y > view.y - margin && at.y < view.y + view.h + margin;
	}

	/// The sandbox orders overlay (SettingsMan::SandboxOrdersOverlay): for each unit, the order waiting for the next update as a dashed line to
	/// where it goes, its standing order as a tag over its head (with a line back to its post or place when it's off it), why it was last sent
	/// for two seconds after, and a red flash each time the standing orders send it again.
	/// The sim state readout (SettingsMan::ShowSandboxSimState), in the bottom right of the picture: what holds the world still (the sandbox's
	/// tools, photo mode, Freeze simulation, the game's own pause), the AI pause, how many sim updates ran for this drawn frame, the tool uses
	/// queued, applied last update and steps still wanted, and the time scale against the speed the sim actually manages.
	void DrawSimState() {
		if (!g_SettingsMan.ShowSandboxSimState()) {
			return;
		}
		static long long lastCount = -1;
		long long count = g_TimerMan.GetSimUpdateCount();
		long long thisFrame = lastCount < 0 ? 0 : count - lastCount;
		lastCount = count;
		std::string pausedBy;
		auto because = [&pausedBy](const char* what) { pausedBy += pausedBy.empty() ? what : std::string(", ") + what; };
		if (s_PausedByMenus) {
			because("sandbox tools open");
		}
		if (g_DebugMan.IsPhotoModeOpen()) {
			because("photo mode");
		}
		if (g_DebugMan.IsSimFrozen()) {
			because("Freeze simulation");
		}
		if (g_ActivityMan.ActivityPaused()) {
			because("game paused");
		}
		std::vector<std::string> lines;
		lines.push_back(g_TimerMan.IsSimPaused() ? "world paused" + (pausedBy.empty() ? std::string() : " by " + pausedBy) : std::string("world running") + (pausedBy.empty() ? "" : " (asked to pause by " + pausedBy + ")"));
		if (Controller::IsAIPaused()) {
			lines.push_back("AI paused");
		}
		lines.push_back("sim updates this frame " + std::to_string(thisFrame) + ", update " + std::to_string(count));
		lines.push_back("tool uses queued " + std::to_string(s_Queue.size()) + ", applied last update " + std::to_string(s_StrokesApplied) + ", steps wanted " + std::to_string(s_StepsWanted));
		char speed[64];
		std::snprintf(speed, sizeof(speed), "time scale x%.2f, sim running at x%.2f", g_TimerMan.GetTimeScale(), g_TimerMan.GetSimSpeed());
		lines.push_back(speed);
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		GameViewRect view = g_WindowMan.GetGameViewRect();
		float lineHeight = ImGui::GetTextLineHeight();
		float width = 0.0F;
		for (const std::string& line: lines) {
			width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
		}
		ImVec2 corner(std::floor(view.x + view.w - width - 12.0F), std::floor(view.y + view.h - lineHeight * static_cast<float>(lines.size()) - 12.0F));
		drawList->AddRectFilled(ImVec2(corner.x - 4.0F, corner.y - 4.0F), ImVec2(corner.x + width + 4.0F, corner.y + lineHeight * static_cast<float>(lines.size()) + 4.0F), IM_COL32(10, 12, 10, 190));
		for (size_t i = 0; i < lines.size(); ++i) {
			ImU32 color = i == 0 && g_TimerMan.IsSimPaused() ? IM_COL32(150, 210, 255, 255) : lines[i] == "AI paused" ? IM_COL32(255, 210, 80, 255) : IM_COL32(230, 230, 220, 255);
			drawList->AddText(ImVec2(corner.x, corner.y + lineHeight * static_cast<float>(i)), color, lines[i].c_str());
		}
	}

	/// The incoming and effects overlay (SettingsMan::ShowSandboxEffects): each thing on its way in from the sky as its line from where it
	/// comes to where it's aimed, the point it will hit (the first ground on its line) with its crater, its preset and the updates it has
	/// left; each effect put down with its name and its main light's reach as a ring (storm cells with their next flash); each water spring as
	/// its pour. With the pointer over an effect or a spring, Delete removes that one (the window's buttons only take the last or all).
	void DrawEffectsOverlay() {
		if (!g_SettingsMan.ShowSandboxEffects()) {
			return;
		}
		// The reach of each effect's main light, in EffectKind's order; 0 for those that make no light.
		static const float lightReach[] = {320.0F, 560.0F, 280.0F, 240.0F, 220.0F, 460.0F, 640.0F, 340.0F, 320.0F, 165.0F, 62.0F, 240.0F, 150.0F, 18.0F, 200.0F, 80.0F, 0.0F, 0.0F, 0.0F, 90.0F, 0.0F, 0.0F, 130.0F, 0.0F, 0.0F};
		static_assert(sizeof(lightReach) / sizeof(lightReach[0]) == static_cast<size_t>(EffectKind::Count), "a reach for each effect");
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		const ImVec2& mouse = ImGui::GetIO().MousePos;
		auto label = [drawList](const ImVec2& at, const std::string& text, ImU32 color) {
			ImVec2 size = ImGui::CalcTextSize(text.c_str());
			ImVec2 corner(std::floor(at.x - size.x * 0.5F), std::floor(at.y));
			drawList->AddRectFilled(ImVec2(corner.x - 2.0F, corner.y), ImVec2(corner.x + size.x + 2.0F, corner.y + size.y), IM_COL32(10, 12, 10, 170));
			drawList->AddText(corner, color, text.c_str());
		};
		auto pointedAt = [&mouse](const ImVec2& at) { return std::abs(mouse.x - at.x) < 10.0F && std::abs(mouse.y - at.y) < 10.0F; };
		bool removeKey = !ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Delete, false);
		ImU32 incomingColor = IM_COL32(255, 150, 60, 230);
		for (const Incoming& incoming: s_Incoming) {
			Vector now = incoming.Id != 0 ? incoming.LastPos : incoming.From;
			Vector line = g_SceneMan.ShortestDistance(incoming.From, incoming.Target, g_SceneMan.SceneWrapsX());
			float length = line.GetMagnitude();
			Vector direction = length > 0.01F ? line / length : Vector(0.0F, 1.0F);
			// Where it will hit: the first ground on its line from here, looked for as far as its target (and no more than 1500 px ahead).
			Vector impact = incoming.Target;
			float left = std::min(g_SceneMan.ShortestDistance(now, incoming.Target, g_SceneMan.SceneWrapsX()).GetMagnitude(), 1500.0F);
			for (float ahead = 0.0F; ahead <= left; ahead += 4.0F) {
				Vector probe = now + direction * ahead;
				if (probe.m_Y > 0.0F && g_SceneMan.GetTerrMatter(probe.GetFloorIntX(), probe.GetFloorIntY()) != g_MaterialAir) {
					impact = probe;
					break;
				}
			}
			ImVec2 from = ToScreen(incoming.From);
			ImVec2 at = ToScreen(now);
			ImVec2 hit = ToScreen(impact);
			drawList->AddLine(from, at, IM_COL32(255, 150, 60, 110), 1.0F);
			drawList->AddLine(at, hit, incomingColor, 1.5F);
			drawList->AddCircle(hit, std::max(static_cast<float>(incoming.Crater) / scale, 4.0F), IM_COL32(255, 80, 50, 230), 0, 2.0F);
			std::string text = incoming.Preset + (incoming.Delay > 0 ? "  in " + std::to_string(incoming.Delay) + " updates" : "  life " + std::to_string(incoming.Life)) + (incoming.Crater > 0 ? "  crater " + std::to_string(incoming.Crater) : "");
			label(ImVec2(at.x, at.y + 8.0F), text, incomingColor);
		}
		int removeEffect = -1;
		for (size_t i = 0; i < s_Effects.size(); ++i) {
			const PlacedEffect& effect = s_Effects[i];
			ImVec2 at = ToScreen(effect.Position);
			int kind = std::clamp(static_cast<int>(effect.Kind), 0, static_cast<int>(EffectKind::Count) - 1);
			bool hovered = pointedAt(at);
			ImU32 color = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 160, 255, 230);
			if (lightReach[kind] > 0.0F) {
				drawList->AddCircle(at, lightReach[kind] / scale, IM_COL32(200, 160, 255, 90), 48, 1.0F);
			}
			drawList->AddRect(ImVec2(at.x - 5.0F, at.y - 5.0F), ImVec2(at.x + 5.0F, at.y + 5.0F), color, 0.0F, 0, 2.0F);
			std::string text = std::to_string(i + 1) + " " + c_Effects[kind].Name;
			if (effect.Kind == EffectKind::StormCell) {
				char wait[32];
				std::snprintf(wait, sizeof(wait), "  next flash %.1fs", static_cast<float>(std::max(effect.Wait, 0)) / 60.0F);
				text += wait;
			}
			if (hovered) {
				text += "  (Delete: remove)";
				if (removeKey) {
					removeEffect = static_cast<int>(i);
				}
			}
			label(ImVec2(at.x, at.y + 7.0F), text, color);
		}
		int removeSpawner = -1;
		for (size_t i = 0; i < s_WaterSpawners.size(); ++i) {
			const WaterSpawner& spawner = s_WaterSpawners[i];
			ImVec2 at = ToScreen(spawner.Position);
			bool hovered = pointedAt(at);
			ImU32 color = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(90, 170, 255, 230);
			drawList->AddCircle(at, std::max(static_cast<float>(spawner.Radius) / scale, 4.0F), color, 0, 2.0F);
			std::string text = "water " + std::to_string(spawner.Radius) + " px";
			if (hovered) {
				text += "  (Delete: remove)";
				if (removeKey && removeEffect < 0) {
					removeSpawner = static_cast<int>(i);
				}
			}
			label(ImVec2(at.x, at.y + 7.0F), text, color);
		}
		// (Removed from the ImGui frame, as the window's own Remove buttons do.)
		if (removeEffect >= 0) {
			s_Effects.erase(s_Effects.begin() + removeEffect);
		} else if (removeSpawner >= 0) {
			s_WaterSpawners.erase(s_WaterSpawners.begin() + removeSpawner);
		}
	}

	/// The selection and camera overlay (SettingsMan::ShowSandboxSelectionCamera): while a box is dragged, the box as SelectInBox will take it
	/// (scene coordinates as worked out from the view, unwrapped: any part past the scene's seam picks nobody, review S6) with a ring on each
	/// unit it would take; the unit the game controls against the one the sandbox thinks you're in (review S1); the observation target and
	/// the free camera's centre as two crosses; and the view's scale.
	void DrawSelectionCameraOverlay() {
		if (!g_SettingsMan.ShowSandboxSelectionCamera()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		const ImGuiIO& io = ImGui::GetIO();
		float scale = ScenePixelsPerWindowPixel();
		ImVec2 origin = ViewOrigin();
		Vector offset = g_CameraMan.GetOffset(0);
		// Scene to window without wrapping, so the box shows where its scene coordinates really are.
		auto unwrapped = [&](const Vector& scene) { return ImVec2(origin.x + (scene.m_X - offset.m_X) / scale, origin.y + (scene.m_Y - offset.m_Y) / scale); };
		std::vector<std::string> lines;
		if (s_Dragging) {
			Vector start = offset + Vector(s_DragStart.x - origin.x, s_DragStart.y - origin.y) * scale;
			Vector end = offset + Vector(io.MousePos.x - origin.x, io.MousePos.y - origin.y) * scale;
			float left = std::min(start.m_X, end.m_X);
			float right = std::max(start.m_X, end.m_X);
			float top = std::min(start.m_Y, end.m_Y);
			float bottom = std::max(start.m_Y, end.m_Y);
			float width = static_cast<float>(g_SceneMan.GetSceneWidth());
			drawList->AddRect(unwrapped(Vector(left, top)), unwrapped(Vector(right, bottom)), IM_COL32(120, 255, 160, 220), 0.0F, 0, 1.0F);
			if (g_SceneMan.SceneWrapsX() && (left < 0.0F || right > width)) {
				// The part of the box off the scene's x range: units there sit at the other end of the scene's coordinates, so it takes none.
				float from = left < 0.0F ? left : std::max(left, width);
				float to = left < 0.0F ? std::min(right, 0.0F) : right;
				drawList->AddRectFilled(unwrapped(Vector(from, top)), unwrapped(Vector(to, bottom)), IM_COL32(255, 60, 50, 60));
				lines.push_back("box crosses the scene's seam: the red part selects nobody");
			}
			int taken = 0;
			for (const Actor* actor: SandboxAccess::Actors()) {
				const Vector& position = actor->GetPos();
				if (IsCombatant(actor) && !actor->IsInGroup("Brains") && position.m_X >= left && position.m_X <= right && position.m_Y >= top && position.m_Y <= bottom) {
					drawList->AddCircle(ToScreen(position), std::max(actor->GetRadius() / scale, 8.0F), IM_COL32(120, 255, 160, 230), 0, 1.5F);
					++taken;
				}
			}
			lines.push_back("box " + std::to_string(static_cast<int>(left)) + "," + std::to_string(static_cast<int>(top)) + " to " + std::to_string(static_cast<int>(right)) + "," + std::to_string(static_cast<int>(bottom)) + " takes " + std::to_string(taken));
		}
		GameActivity* game = CurrentGame();
		const Actor* controlled = game ? game->GetControlledActor(Players::PlayerOne) : nullptr;
		const Actor* possessed = s_Possessed && g_MovableMan.IsActor(s_Possessed) ? s_Possessed : nullptr;
		if (controlled) {
			drawList->AddCircle(ToScreen(controlled->GetPos()), std::max(controlled->GetRadius() / scale, 10.0F) + 3.0F, IM_COL32(120, 230, 120, 230), 0, 2.0F);
		}
		if (possessed && possessed != controlled) {
			drawList->AddCircle(ToScreen(possessed->GetPos()), std::max(possessed->GetRadius() / scale, 10.0F) + 7.0F, IM_COL32(110, 180, 250, 230), 0, 2.0F);
		}
		lines.push_back("game controls: " + (controlled ? controlled->GetPresetName() + " #" + std::to_string(controlled->GetUniqueID()) : std::string("nobody")));
		lines.push_back(std::string("sandbox thinks you're in: ") + (possessed ? possessed->GetPresetName() + " #" + std::to_string(possessed->GetUniqueID()) : s_Possessed ? std::string("a unit that's gone") : std::string("nobody")) + (s_Possessed != controlled && (s_Possessed || Sandbox::IsGodMode()) ? "  (they differ)" : ""));
		auto cross = [drawList](const ImVec2& at, ImU32 color) {
			drawList->AddLine(ImVec2(at.x - 8.0F, at.y - 8.0F), ImVec2(at.x + 8.0F, at.y + 8.0F), color, 2.0F);
			drawList->AddLine(ImVec2(at.x - 8.0F, at.y + 8.0F), ImVec2(at.x + 8.0F, at.y - 8.0F), color, 2.0F);
		};
		if (game) {
			Vector observing = game->GetObservationTarget(Players::PlayerOne);
			cross(ToScreen(observing), IM_COL32(255, 220, 80, 230));
			lines.push_back("observation target (yellow) " + std::to_string(observing.GetFloorIntX()) + "," + std::to_string(observing.GetFloorIntY()));
		}
		if (Sandbox::IsGodMode()) {
			cross(ToScreen(s_CameraCenter), IM_COL32(90, 230, 255, 230));
			lines.push_back("free camera centre (cyan) " + std::to_string(s_CameraCenter.GetFloorIntX()) + "," + std::to_string(s_CameraCenter.GetFloorIntY()));
		}
		char text[64];
		std::snprintf(text, sizeof(text), "scene pixels per window pixel %.3f", scale);
		lines.push_back(text);
		GameViewRect view = g_WindowMan.GetGameViewRect();
		float lineHeight = ImGui::GetTextLineHeight();
		float width = 0.0F;
		for (const std::string& line: lines) {
			width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
		}
		ImVec2 corner(std::floor(view.x + 8.0F), std::floor(view.y + view.h * 0.35F));
		drawList->AddRectFilled(ImVec2(corner.x - 4.0F, corner.y - 4.0F), ImVec2(corner.x + width + 4.0F, corner.y + lineHeight * static_cast<float>(lines.size()) + 4.0F), IM_COL32(10, 12, 10, 190));
		for (size_t i = 0; i < lines.size(); ++i) {
			bool warn = lines[i].find("seam") != std::string::npos || lines[i].find("(they differ)") != std::string::npos;
			drawList->AddText(ImVec2(corner.x, corner.y + lineHeight * static_cast<float>(i)), warn ? IM_COL32(255, 120, 100, 255) : IM_COL32(230, 230, 220, 255), lines[i].c_str());
		}
	}

	/// The terrain paint audit (SettingsMan::ShowSandboxPaintAudit): the last two dozen changes the paint helpers made, each as its box (dug
	/// and cleared orange, painted and filled green, grey if nothing changed), fading over ten seconds, labelled with the material and whether
	/// falling ground (TerrainCollapse::BeginChange) and liquid (FluidSim::Disturb) were told: a "no" in red is review S13's inconsistency.
	void DrawPaintAudit() {
		if (!g_SettingsMan.ShowSandboxPaintAudit() || s_PaintRecords.empty()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		long long now = g_TimerMan.GetSimUpdateCount();
		float lineHeight = ImGui::GetTextLineHeight();
		for (size_t i = 0; i < s_PaintRecords.size(); ++i) {
			const PaintRecord& record = s_PaintRecords[i];
			float age = static_cast<float>(std::max(0LL, now - record.At)) / 600.0F;
			if (age >= 1.0F) {
				continue;
			}
			int alpha = static_cast<int>(230.0F * (1.0F - age * 0.7F));
			bool removes = record.Material == "air";
			ImU32 color = !record.Changed ? IM_COL32(160, 160, 160, alpha) : removes ? IM_COL32(255, 160, 60, alpha) : IM_COL32(120, 230, 120, alpha);
			ImVec2 topLeft = ToScreen(record.Area.GetCorner());
			ImVec2 bottomRight(topLeft.x + record.Area.GetWidth() / scale, topLeft.y + record.Area.GetHeight() / scale);
			drawList->AddRect(topLeft, bottomRight, color, 0.0F, 0, 1.5F);
			// Only the newest few are labelled, so a long brush stroke doesn't bury the picture in text.
			if (i + 6 < s_PaintRecords.size()) {
				continue;
			}
			std::string head = std::string(record.Kind) + " " + record.Material + (record.Changed ? "" : " (nothing changed)");
			std::string collapse = std::string("falling ground told: ") + (record.ToldCollapse ? "yes" : "no");
			std::string liquid = std::string("liquid told: ") + (record.ToldLiquid ? "yes" : "no");
			ImVec2 at(bottomRight.x + 4.0F, topLeft.y);
			const std::string* lines[] = {&head, &collapse, &liquid};
			float width = 0.0F;
			for (const std::string* line: lines) {
				width = std::max(width, ImGui::CalcTextSize(line->c_str()).x);
			}
			drawList->AddRectFilled(ImVec2(at.x - 2.0F, at.y), ImVec2(at.x + width + 2.0F, at.y + lineHeight * 3.0F), IM_COL32(10, 12, 10, std::min(alpha, 180)));
			drawList->AddText(at, color, head.c_str());
			// Removing ground needs falling ground told; any change may need liquid told.
			bool collapseMissing = removes && !record.ToldCollapse;
			bool liquidMissing = record.Changed && !record.ToldLiquid;
			drawList->AddText(ImVec2(at.x, at.y + lineHeight), collapseMissing ? IM_COL32(255, 90, 70, alpha) : IM_COL32(220, 220, 210, alpha), collapse.c_str());
			drawList->AddText(ImVec2(at.x, at.y + lineHeight * 2.0F), liquidMissing ? IM_COL32(255, 90, 70, alpha) : IM_COL32(220, 220, 210, alpha), liquid.c_str());
		}
	}

	void DrawOrdersOverlay() {
		int which = g_SettingsMan.SandboxOrdersOverlay();
		if (which == 0) {
			return;
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		float lineHeight = ImGui::GetTextLineHeight();
		long long now = g_TimerMan.GetSimUpdateCount();
		for (const Actor* actor: SandboxAccess::Actors()) {
			if (!IsCombatant(actor)) {
				continue;
			}
			if (which == 1 && !actor->IsDebugInspected() && std::none_of(s_Selected.begin(), s_Selected.end(), [actor](const UnitRef& ref) { return GetRef(ref) == actor; })) {
				continue;
			}
			ImVec2 at = ToScreen(actor->GetPos());
			if (!OnPicture(at, 150.0F)) {
				continue;
			}
			ImU32 side = c_SideColors[actor->GetTeam()];
			for (const PendingOrder& order: s_PendingOrders) {
				if (order.Unit.Unit == actor) {
					DashedLine(drawList, at, ToScreen(order.Waypoint), IM_COL32(255, 255, 255, 220), 1.5F);
				}
			}
			std::string tag;
			bool hasPost = false;
			Vector post;
			if (actor->NumberValueExists(c_TargetTag)) {
				tag = "ATTACK #" + std::to_string(static_cast<long long>(actor->GetNumberValue(c_TargetTag)));
			} else if (actor->NumberValueExists(c_AttackXTag)) {
				post.SetXY(static_cast<float>(actor->GetNumberValue(c_AttackXTag)), static_cast<float>(actor->GetNumberValue(c_AttackYTag)));
				hasPost = true;
				tag = "ATTACK@ " + std::to_string(post.GetFloorIntX()) + "," + std::to_string(post.GetFloorIntY());
			} else if (actor->NumberValueExists(c_AttackTag)) {
				tag = "ATTACK";
			} else if (actor->NumberValueExists(c_DefendXTag)) {
				post.SetXY(static_cast<float>(actor->GetNumberValue(c_DefendXTag)), static_cast<float>(actor->GetNumberValue(c_DefendYTag)));
				hasPost = true;
				tag = "DEFEND";
			} else if (actor->GetAIMode() == Actor::AIMODE_GOTO) {
				const MovableObject* target = actor->GetMOMoveTarget();
				const Actor* leader = target && g_MovableMan.ValidMO(target) ? dynamic_cast<const Actor*>(target) : nullptr;
				tag = leader && leader->GetTeam() == actor->GetTeam() ? "GUARD #" + std::to_string(leader->GetUniqueID()) : std::string("MOVE");
			} else if (actor->GetAIMode() == Actor::AIMODE_SENTRY) {
				tag = "HOLD";
			}
			if (hasPost && g_SceneMan.ShortestDistance(actor->GetPos(), post, g_SceneMan.SceneWrapsX()).GetMagnitude() > 30.0F) {
				ImVec2 postAt = ToScreen(post);
				drawList->AddLine(at, postAt, side, 1.5F);
				drawList->AddCircle(postAt, 5.0F, side, 0, 2.0F);
			}
			std::string why;
			if (auto note = s_SendNotes.find(actor->GetUniqueID()); note != s_SendNotes.end()) {
				long long ago = now - note->second.At;
				if (ago >= 0 && ago < 120) {
					why = std::string(note->second.Resend ? "sent again: " : "sent: ") + note->second.Reason;
				}
				if (note->second.Resend && ago >= 0 && ago < 20) {
					int alpha = static_cast<int>(230.0F * (1.0F - static_cast<float>(ago) / 20.0F));
					drawList->AddCircle(at, std::max(actor->GetRadius() / scale, 10.0F) + 4.0F, IM_COL32(255, 60, 50, alpha), 0, 3.0F);
				}
			}
			float top = at.y - std::max(actor->GetRadius() / scale, 10.0F) - 6.0F;
			for (const std::string* line: {&why, &tag}) {
				if (line->empty()) {
					continue;
				}
				top -= lineHeight;
				ImVec2 size = ImGui::CalcTextSize(line->c_str());
				ImVec2 corner(std::floor(at.x - size.x * 0.5F), std::floor(top));
				drawList->AddRectFilled(ImVec2(corner.x - 2.0F, corner.y), ImVec2(corner.x + size.x + 2.0F, corner.y + size.y), IM_COL32(10, 12, 10, 170));
				drawList->AddText(corner, line == &tag ? side : IM_COL32(230, 230, 220, 255), line->c_str());
			}
		}
	}
} // namespace

void Sandbox::DrawDebug() {
	if (!g_ActivityMan.GetActivity() || !g_SceneMan.GetScene()) {
		return;
	}
	DrawOrdersOverlay();
	DrawSimState();
	DrawEffectsOverlay();
	DrawSelectionCameraOverlay();
	DrawPaintAudit();
}
