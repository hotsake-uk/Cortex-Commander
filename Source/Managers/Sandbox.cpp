#include "WindowMan.h"
#include "DebugMan.h"
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

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <list>
#include <map>
#include <memory>
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
		// Not tools, but queued the same way.
		PlayerRemake,
		PlayerRemove,
		OrderSide,
		RemoveSide,
		Release,
		Select,
		OrderSelected
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
		Idle
	};
	constexpr const char* c_OrderNames = "Hold position\0Attack nearest enemy\0Hunt brains\0Patrol\0Go to rally point\0Do nothing\0";
	constexpr const char* c_AttackTag = "SandboxAttack"; //!< Number value on units told to attack, so they get a new target when theirs dies.

	/// A preset the sandbox can spawn.
	struct Preset {
		std::string Label;
		std::string ClassName;
		std::string PresetName;
		std::string Module;
		int ModuleID = -1;
		std::string Group; //!< Structures: the kind of bunker piece ("Bunker Modules", "Bunker Lights"...), to list them by.
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
	ImVec2 s_DragStart;
	std::array<AutoSide, 4> s_AutoSides;
	bool s_AutoRunning = false;
	int s_AutoWinner = -2; //!< -2 no result yet, -1 a draw, otherwise the winning side.
	Vector s_AutoCenter;
	std::vector<int> s_FactionModules;
	std::vector<std::string> s_FactionNames;
	int s_Radius = 6;
	int s_UnitChoice = 0;
	int s_BrainChoice = 0;
	int s_ItemChoice = 0;
	int s_StructureChoice = 0;
	char s_Filter[64] = "";
	int s_Team = 1;
	int s_Order = static_cast<int>(Order::Attack);
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
	Actor* s_Possessed = nullptr; //!< The unit you're controlling in the god mode, checked with IsActor before use.

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
	bool s_RingOpen = false; //!< The ring of sides is up, round where the right button went down with a side-taking tool in hand.
	ImVec2 s_RingCenter;
	int s_ColonyKeep = 4; //!< How many of its units a new barracks keeps alive.
	bool s_PauseInMenus = true; //!< In the Sandbox game mode the world stands still while the tools are open.
	bool s_PausedByMenus = false; //!< Whether it is this that has paused the simulation, so only this is undone.
	int s_StepsWanted = 0; //!< Updates to let the paused world do.
	unsigned long long s_GodStartUpdate = 0; //!< The simulation update the Sandbox game started on: it runs a moment before it first pauses.
	float s_PlayHintSeconds = 0.0F; //!< How much longer the reminder of the keys shows after stepping into the character.
	bool s_Flying = false;
	int s_KitKeyPending = -1; //!< A kit number pressed last update, taken out this one (after the game's own weapon keys have had their say).
	const Activity* s_GodActivity = nullptr; //!< The Sandbox game the window was last set up for.
	unsigned int s_Random = 0x5A17B0Bu;
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

	/// How many scene pixels one window pixel covers (player 1's screen fills the window).
	float ScenePixelsPerWindowPixel() { return static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) / std::max(1.0F, g_WindowMan.GetGameViewRect().w); }

	/// The top left corner of the game's picture in the window. With tool panels docked at the sides it isn't the window's own corner.
	ImVec2 ViewOrigin() {
		GameViewRect view = g_WindowMan.GetGameViewRect();
		return ImVec2(view.x, view.y);
	}

	Vector MouseScenePosition() {
		const ImVec2& mouse = ImGui::GetIO().MousePos;
		ImVec2 origin = ViewOrigin();
		Vector position = g_CameraMan.GetOffset(0) + Vector(mouse.x - origin.x, mouse.y - origin.y) * ScenePixelsPerWindowPixel();
		g_SceneMan.WrapPosition(position);
		return position;
	}

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

	Actor* NearestEnemy(const Actor* of) {
		Actor* nearest = nullptr;
		float nearestDistance = 0.0F;
		for (Actor* actor: SandboxAccess::Actors()) {
			if (actor == of || !IsCombatant(actor) || actor->GetTeam() == of->GetTeam() || actor->IsIgnoredByAI()) {
				continue;
			}
			float distance = g_SceneMan.ShortestDistance(of->GetPos(), actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
			if (!nearest || distance < nearestDistance) {
				nearest = actor;
				nearestDistance = distance;
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

	void GiveOrder(Actor* actor, Order order) {
		if (!actor || dynamic_cast<ADoor*>(actor) || actor->IsInGroup("Brains")) {
			return;
		}
		actor->RemoveNumberValue(c_AttackTag);
		switch (order) {
			case Order::Attack:
				actor->SetNumberValue(c_AttackTag, 1.0);
				actor->ClearAIWaypoints();
				if (Actor* enemy = NearestEnemy(actor)) {
					actor->AddAIMOWaypoint(enemy);
					actor->SetAIMode(Actor::AIMODE_GOTO);
				} else {
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
				actor->ClearAIWaypoints();
				if (int team = actor->GetTeam(); team >= 0 && team < c_Sides && s_RallySet[team]) {
					actor->AddAISceneWaypoint(s_RallyPoints[team]);
					actor->SetAIMode(Actor::AIMODE_GOTO);
				} else {
					actor->SetAIMode(Actor::AIMODE_SENTRY);
				}
				break;
			case Order::Idle:
				actor->SetAIMode(Actor::AIMODE_NONE);
				break;
			default:
				actor->SetAIMode(Actor::AIMODE_SENTRY);
				break;
		}
	}

	/// Units told to attack get a new target when theirs is gone, and go on guard when no enemies are left.
	void RetargetAttackers() {
		for (Actor* actor: SandboxAccess::Actors()) {
			if (actor->GetNumberValue(c_AttackTag) <= 0.0 || actor->IsPlayerControlled() || !IsCombatant(actor) || actor->NumberValueExists("OnFire")) {
				continue;
			}
			const MovableObject* target = actor->GetMOMoveTarget();
			const Actor* targetActor = target && g_MovableMan.ValidMO(target) ? dynamic_cast<const Actor*>(target) : nullptr;
			if (actor->GetAIMode() == Actor::AIMODE_GOTO && targetActor && IsCombatant(targetActor) && targetActor->GetTeam() != actor->GetTeam()) {
				continue;
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

	/// Paints a disc of terrain material into the air, or digs one out when there's no material.
	void PaintTerrain(const Vector& center, int radius, const char* materialName) {
		SLTerrain* terrain = g_SceneMan.GetScene()->GetTerrain();
		int width = terrain->GetBitmap()->w;
		int height = terrain->GetBitmap()->h;
		int material = g_MaterialAir;
		int color = ColorKeys::g_MaskColor;
		int speckleColor = color;
		if (materialName) {
			const Material* found = g_SceneMan.GetMaterial(materialName);
			if (!found || found->GetIndex() == g_MaterialAir) {
				return;
			}
			material = found->GetIndex();
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
				// A little speckle, so painted ground isn't one flat colour.
				terrain->SetFGColorPixel(x, y, (materialName && Random01() < 0.25F) ? speckleColor : color);
				changed = true;
			}
		}
		if (changed) {
			terrain->AddUpdatedMaterialArea(Box(Vector(static_cast<float>(centerX - radius), static_cast<float>(centerY - radius)), static_cast<float>(radius * 2 + 1), static_cast<float>(radius * 2 + 1)));
			// Liquid around the change may flow into it, and dug-out ground may be left hanging.
			FluidSim::Disturb(center, static_cast<float>(radius + 2));

		}
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
				terrain->SetFGColorPixel(x, y, Random01() < 0.25F ? speckleColor : color);
			}
		}
		terrain->AddUpdatedMaterialArea(Box(topLeft, static_cast<float>(boxWidth), static_cast<float>(boxHeight)));
	}

	/// Whether what a tool makes belongs to a side, so the side is shown with it and the ring of sides is offered.
	bool TakesSide(Tool kind) {
		return kind == Tool::Unit || kind == Tool::Drop || kind == Tool::Brain || kind == Tool::RallyPoint || kind == Tool::Structure || kind == Tool::Barracks || kind == Tool::Extractor;
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
			bool arrived = object == nullptr || --incoming.Life <= 0;
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
			} else if (incoming.ClassName == "TDExplosive") {
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
		float top = std::max(g_CameraMan.GetOffset(0).m_Y - 20.0F, 0.0F);
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

	Actor* GetRef(const UnitRef& ref) { return ref.Unit && g_MovableMan.IsActor(ref.Unit) && ref.Unit->GetUniqueID() == ref.ID ? ref.Unit : nullptr; }

	UnitRef MakeRef(Actor* actor) { return {actor, actor ? actor->GetUniqueID() : 0}; }

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
			actor->SetHFlipped(stroke.Position.m_X > g_CameraMan.GetOffset(0).m_X + static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F);
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
			s_Possessed = actor;
			s_PlayHintSeconds = 9.0F;
			// Every tool window goes away, not only the sandbox's: any left open would keep the mouse from the unit.
			g_DebugMan.CloseTools();
			g_ConsoleMan.PrintString("SANDBOX: You're controlling " + actor->GetPresetName() + ". Press Tab to go back to the god view.");
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
					s_Possessed = actor;
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
		for (Actor* actor: SandboxAccess::Actors()) {
			const Vector& position = actor->GetPos();
			if (IsCombatant(actor) && !actor->IsInGroup("Brains") && position.m_X >= left && position.m_X <= right && position.m_Y >= top && position.m_Y <= bottom) {
				s_Selected.push_back(MakeRef(actor));
			}
		}
	}

	/// The selected units move to a point, or attack the unit there.
	void CommandSelected(const Vector& position) {
		Actor* target = dynamic_cast<Actor*>(ObjectUnder(position, true));
		bool attack = target && IsCombatant(target) && std::none_of(s_Selected.begin(), s_Selected.end(), [target](const UnitRef& ref) { return ref.Unit == target; });
		for (const UnitRef& ref: s_Selected) {
			if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled()) {
				unit->RemoveNumberValue(c_AttackTag);
				unit->ClearAIWaypoints();
				if (attack) {
					unit->AddAIMOWaypoint(target);
				} else {
					unit->AddAISceneWaypoint(position);
				}
				unit->SetAIMode(Actor::AIMODE_GOTO);
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
		Vector lane = s_AutoCenter + Vector(lanes[side] * static_cast<float>(g_FrameMan.GetPlayerScreenWidth()), 0.0F);
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
			float waveBudget = std::min(left, 900.0F);
			std::vector<Actor*> wave;
			float waveCost = 0.0F;
			for (int attempt = 0; attempt < 12 && wave.size() < 5 && !choices.empty(); ++attempt) {
				const Preset* pick = choices[std::min(choices.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(choices.size())))];
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
			autoSide.Sent += static_cast<int>(wave.size());
			autoSide.Spent += DropUnits(wave, side, AutoLaneX(side), 0);
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
					Colony::Place(Colony::Kind::Barracks, at, stroke.Team, unit->PresetName, static_cast<int>(stroke.Orders), stroke.Count);
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
				CommandSelected(at);
				break;
			case Tool::OrderSelected:
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
				for (Actor* actor: SandboxAccess::Actors()) {
					if (actor->GetTeam() == stroke.Team && IsCombatant(actor) && !actor->IsPlayerControlled()) {
						GiveOrder(actor, stroke.Orders);
					}
				}
				break;
			case Tool::RemoveSide:
				for (Actor* actor: SandboxAccess::Actors()) {
					if (actor->GetTeam() == stroke.Team && !dynamic_cast<ADoor*>(actor)) {
						actor->SetToDelete(true);
					}
				}
				break;
			case Tool::Fire:
				TerrainFire::QueueIgniteArea(at, radius);
				// Something to see even over rock, which doesn't burn.
				if (MovableObject* flame = CreateBaseObject("MOSParticle", "Flame Hurt Short")) {
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
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::InputTextWithHint("##filter", "Search...", s_Filter, sizeof(s_Filter));
		if (ImGui::BeginListBox("##presets", ImVec2(-1.0F, ImGui::GetTextLineHeightWithSpacing() * rows))) {
			for (int i = 0; i < static_cast<int>(list.size()); ++i) {
				if (!ContainsIgnoringCase(list[i].Label, s_Filter) || (group && list[i].Group != group)) {
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

	void LoadoutChooser() {
		const char* current = s_Loadout == 0 ? "Faction default" : (s_Loadout == 1 ? "Unarmed" : (s_Loadout - 2 < static_cast<int>(s_Weapons.size()) ? s_Weapons[s_Loadout - 2]->Label.c_str() : "?"));
		if (ImGui::BeginCombo("Loadout", current)) {
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

	/// Gets the picture of a bunker piece, making it the first time it is asked for. A piece with no art of its own gets an empty picture.
	const PiecePicture& PictureOf(const Preset& preset) {
		static std::map<std::string, PiecePicture> pictures;
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
			// A unit is many parts: a copy of it is stood up and drawn whole, the way the game's build menu shows the thing in hand, and the picture cut to fit.
			const int room = 160;
			portrait.reset(create_bitmap_ex(8, room, room));
			clear_to_color(portrait.get(), ColorKeys::g_MaskColor);
			if (Actor* copy = dynamic_cast<Actor*>(actorPreset->Clone())) {
				copy->SetPos(Vector(static_cast<float>(room / 2), static_cast<float>(room / 2)));
				copy->SetTeam(0);
				copy->FullUpdate();
				copy->SetPos(Vector(static_cast<float>(room / 2), static_cast<float>(room / 2)));
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
		PALETTE palette;
		get_palette(palette);
		// Palettes come with channels up to 63 or up to 255, depending on who made them.
		int brightest = 1;
		for (int i = 0; i < 256; ++i) {
			brightest = std::max({brightest, static_cast<int>(palette[i].r), static_cast<int>(palette[i].g), static_cast<int>(palette[i].b)});
		}
		int scale = brightest <= 63 ? 4 : 1;
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
					pixel[0] = static_cast<unsigned char>(std::min(palette[index].r * scale, 255));
					pixel[1] = static_cast<unsigned char>(std::min(palette[index].g * scale, 255));
					pixel[2] = static_cast<unsigned char>(std::min(palette[index].b * scale, 255));
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
	void PictureGrid(Tool kind, const char* group) {
		const std::vector<Preset>& list = ListFor(kind);
		int& choice = ChoiceFor(kind);
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::InputTextWithHint("##filter", "Search...", s_Filter, sizeof(s_Filter));
		const ImGuiStyle& style = ImGui::GetStyle();
		float cell = ImGui::GetFontSize() * 6.0F;
		float labelHeight = ImGui::GetTextLineHeight() * 2.0F;
		ImGui::BeginChild("##pictures", ImVec2(-1.0F, std::max(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 6.5F, cell * 2.5F)), ImGuiChildFlags_Borders);
		int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / (cell + style.ItemSpacing.x)));
		int shown = 0;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (int i = 0; i < static_cast<int>(list.size()); ++i) {
			const Preset& preset = list[i];
			if (!ContainsIgnoringCase(preset.Label, s_Filter) || (group && preset.Group != group)) {
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
				ImGui::SetTooltip("%s\n%s%s%s", preset.PresetName.c_str(), preset.Module.c_str(), size.c_str(), Sandbox::IsGodMode() ? "\nRight click: keep it on the bar, or take it off" : "");
			}
			if (picked) {
				choice = i;
				TookTool(ToolIndex(kind));
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && Sandbox::IsGodMode()) {
				TogglePin(kind, preset.PresetName);
			}
			if (ImGui::IsItemVisible() && FindPin(kind, preset.PresetName) >= 0) {
				DrawPinMark(drawList, at, ImVec2(at.x + size.x, at.y + size.y));
			}
			ImGui::PopID();
		}
		if (shown == 0) {
			ImGui::TextDisabled("Nothing of that kind matches.");
		}
		ImGui::EndChild();
	}

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

	/// The ring of sides: held open with the right button while a side-taking tool is in hand, four coloured quarters round the pointer; let go over one to take it.
	void DrawSideRing() {
		ImGuiIO& io = ImGui::GetIO();
		if (!s_RingOpen) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse && TakesSide(CurrentTool().Kind)) {
				s_RingOpen = true;
				s_RingCenter = io.MousePos;
			}
			return;
		}
		float pixel = ToolUI::Pixel();
		float inner = pixel * 14.0F;
		float outer = pixel * 34.0F;
		ImVec2 away(io.MousePos.x - s_RingCenter.x, io.MousePos.y - s_RingCenter.y);
		float distance = std::sqrt(away.x * away.x + away.y * away.y);
		// The quarters: Red above, Green to the right, Blue below, Yellow to the left.
		int under = -1;
		if (distance > inner * 0.6F) {
			float angle = std::atan2(away.y, away.x); // 0 to the right, positive downwards.
			if (angle > -2.356F && angle <= -0.785F) {
				under = 0;
			} else if (angle > -0.785F && angle <= 0.785F) {
				under = 1;
			} else if (angle > 0.785F && angle <= 2.356F) {
				under = 2;
			} else {
				under = 3;
			}
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		const float quarterStart[c_Sides] = {-2.356F, -0.785F, 0.785F, 2.356F};
		for (int side = 0; side < c_Sides; ++side) {
			bool lit = side == under || (under < 0 && side == s_Team);
			ImU32 color = c_SideColors[side];
			ImU32 fill = (color & 0x00FFFFFF) | (static_cast<ImU32>(lit ? 230 : 110) << IM_COL32_A_SHIFT);
			float from = quarterStart[side] + 0.06F;
			float to = quarterStart[side] + 1.571F - 0.06F;
			drawList->PathClear();
			drawList->PathArcTo(s_RingCenter, lit ? outer + pixel * 3.0F : outer, from, to, 12);
			drawList->PathArcTo(s_RingCenter, inner, to, from, 12);
			drawList->PathFillConvex(fill);
			drawList->PathClear();
			drawList->PathArcTo(s_RingCenter, lit ? outer + pixel * 3.0F : outer, from, to, 12);
			drawList->PathArcTo(s_RingCenter, inner, to, from, 12);
			drawList->PathStroke(IM_COL32(20, 24, 16, 230), ImDrawFlags_Closed, pixel);
			float middle = (from + to) * 0.5F;
			float reach = (inner + outer) * 0.5F;
			ImVec2 nameSize = ImGui::CalcTextSize(c_SideNames[side]);
			ImVec2 at(s_RingCenter.x + std::cos(middle) * reach - nameSize.x * 0.5F, s_RingCenter.y + std::sin(middle) * reach - nameSize.y * 0.5F);
			drawList->AddText(ImVec2(at.x + pixel, at.y + pixel), IM_COL32(0, 0, 0, 200), c_SideNames[side]);
			drawList->AddText(at, IM_COL32(255, 255, 255, 255), c_SideNames[side]);
		}
		// The side in hand, in the middle.
		drawList->AddCircleFilled(s_RingCenter, inner - pixel * 2.0F, IM_COL32(20, 24, 16, 220));
		drawList->AddCircleFilled(s_RingCenter, inner - pixel * 5.0F, c_SideColors[under >= 0 ? under : s_Team]);
		static const bool testHeld = std::getenv("CCCP_TEST_RING") != nullptr;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !testHeld) {
			if (under >= 0) {
				s_Team = under;
			}
			s_RingOpen = false;
		}
	}

	ImVec2 ToScreen(const Vector& scenePosition) {
		Vector onScreen = g_SceneMan.ShortestDistance(g_CameraMan.GetOffset(0), scenePosition, g_SceneMan.SceneWrapsX());
		float scale = ScenePixelsPerWindowPixel();
		return ImVec2(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
	}

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
			ImGui::Combo("Their orders", &s_Order, c_OrderNames);
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
					ImGui::Combo("Their orders", &building.Orders, c_OrderNames);
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

	/// Rings over selected units, and a marker over the followed one.
	void DrawSelection() {
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		for (const UnitRef& ref: s_Selected) {
			if (const Actor* unit = GetRef(ref)) {
				ImVec2 at = ToScreen(unit->GetPos());
				int team = std::clamp(unit->GetTeam(), 0, c_Sides - 1);
				drawList->AddCircle(at, std::max(unit->GetRadius() / scale, 8.0F), c_SideColors[team], 0, 2.0F);
			}
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
		std::array<int, c_Sides> counts{};
		for (const Actor* actor: SandboxAccess::Actors()) {
			if (IsCombatant(actor)) {
				counts[actor->GetTeam()]++;
			}
		}
		for (int side = 0; side < c_Sides; ++side) {
			if (side > 0) {
				ImGui::SameLine();
			}
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c_SideColors[side]), "%s %d", c_SideNames[side], counts[side]);
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
						// Whatever was last picked on that part comes back to hand with it.
						if (auto remembered = s_LastToolOfTab.find(part.Name); remembered != s_LastToolOfTab.end()) {
							s_ToolIndex = remembered->second;
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
	stroke.Orders = static_cast<Order>(std::clamp(order, 0, static_cast<int>(Order::Idle)));
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
			if (std::getenv("CCCP_TEST_POINTER")) {
				// Test runs that show the building preview: the piece a script placed stays in hand.
				s_StructureChoice = stroke.Choice;
				s_ToolIndex = ToolIndex(Tool::Structure);
			}
		}
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
		if (IsCombatant(actor) && actor->GetTeam() == team && !dynamic_cast<const ACraft*>(actor)) {
			++count;
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
	Actor* actor = preset ? CreateUnit(*preset, team, 0, static_cast<Order>(std::clamp(order, 0, static_cast<int>(Order::Idle)))) : nullptr;
	if (!actor) {
		return nullptr;
	}
	ActivateSide(team);
	actor->SetPos(position);
	g_MovableMan.AddActor(actor);
	return actor;
}

float Sandbox::UnitCost(const std::string& presetName) {
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
	return (s_Open || IsLookingAround()) && CurrentTool().Kind != Tool::None && InGame() && !ImGui::GetIO().WantCaptureMouse;
}

void Sandbox::DrawGUI() {
	// A new Sandbox game opens the god view: the window and the free camera.
	if (IsGodMode()) {
		if (s_GodActivity != g_ActivityMan.GetActivity()) {
			s_GodActivity = g_ActivityMan.GetActivity();
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
		s_GodActivity = nullptr;
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
		bool wantPause = IsGodMode() && s_Open && s_PauseInMenus && !g_DebugMan.IsPhotoModeOpen() && g_TimerMan.GetSimUpdateCount() > s_GodStartUpdate + 90;
		if (wantPause) {
			g_TimerMan.PauseSim(true);
			s_PausedByMenus = true;
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
	if (static const char* testPointer = std::getenv("CCCP_TEST_POINTER")) {
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
	// Paint or spawn with the left mouse button on the world.
	if (CapturesWorldClicks()) {
		const ToolInfo& tool = CurrentTool();
		Vector position = MouseScenePosition();
		if (tool.Kind == Tool::Command) {
			// Drag a box to select units; click the ground to send them there, or an enemy to attack it.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s_Dragging = true;
				s_DragStart = io.MousePos;
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
					ImGui::Combo("Orders", &s_Order, c_OrderNames);
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
				if (ToolUI::Button("Give orders", ImVec2(-1.0F, 0.0F))) {
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

				ImGui::SeparatorText("Selected units");
				int selected = static_cast<int>(std::count_if(s_Selected.begin(), s_Selected.end(), [](const UnitRef& ref) { return GetRef(ref) != nullptr; }));
				ImGui::Text("%d selected. Use the Command tool: drag a box to select, click to move or attack.", selected);
				if (ToolUI::Button("Give the selected these orders") && selected > 0) {
					Stroke stroke;
					stroke.Kind = Tool::OrderSelected;
					stroke.Orders = static_cast<Order>(s_Order);
					s_Queue.push_back(stroke);
				}
				ImGui::SameLine();
				if (ToolUI::Button("Clear selection")) {
					s_Selected.clear();
				}

				ImGui::SeparatorText("Auto battle");
				ImGui::TextWrapped("Each side buys waves of its faction's units with its budget and drops them in to attack, until one side is left.");
				for (int side = 0; side < c_Sides && !s_FactionNames.empty(); ++side) {
					AutoSide& autoSide = s_AutoSides[side];
					ImGui::PushID(side);
					ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
					ToolUI::Checkbox(c_SideNames[side], &autoSide.Active);
					ImGui::PopStyleColor();
					ImGui::SameLine(90.0F);
					ImGui::SetNextItemWidth(120.0F);
					if (ImGui::BeginCombo("##faction", s_FactionNames[std::clamp(autoSide.Faction, 0, static_cast<int>(s_FactionNames.size()) - 1)].c_str())) {
						for (int faction = 0; faction < static_cast<int>(s_FactionNames.size()); ++faction) {
							if (ImGui::Selectable(s_FactionNames[faction].c_str(), faction == autoSide.Faction)) {
								autoSide.Faction = faction;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					ImGui::SetNextItemWidth(-1.0F);
					ImGui::SliderInt("##budget", &autoSide.Budget, 500, 30000, "%d oz");
					if (autoSide.Sent > 0) {
						int alive = Sandbox::CountUnits(side);
						ImGui::TextDisabled("    spent %.0f oz, sent %d, alive %d, lost %d%s", autoSide.Spent, autoSide.Sent, alive, std::max(autoSide.Sent - alive, 0), autoSide.Broke ? ", out of money" : "");
					}
					ImGui::PopID();
				}
				if (!s_AutoRunning) {
					if (ToolUI::Button("Start auto battle", ImVec2(-1.0F, 0.0F))) {
						Sandbox::StartAutoBattle();
					}
				} else if (ToolUI::Button("Stop auto battle", ImVec2(-1.0F, 0.0F))) {
					s_AutoRunning = false;
				}
				if (s_AutoWinner >= 0) {
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c_SideColors[s_AutoWinner]), "%s won the last battle.", c_SideNames[s_AutoWinner]);
				} else if (s_AutoWinner == -1) {
					ImGui::Text("The last battle was a draw.");
				}
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
					s_WaterSpawners.clear();
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
					s_Effects.clear();
				}
				ImGui::SameLine();
				if (ToolUI::Button("Remove the last one")) {
					s_Effects.pop_back();
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

void Sandbox::Update() {
	static const Activity* lastActivity = nullptr;
	if (g_ActivityMan.GetActivity() != lastActivity) {
		lastActivity = g_ActivityMan.GetActivity();
		Controller::SetAIPaused(false);
		Colony::Clear();
		// A new game: nothing is left pouring or on its way in from the last one.
		s_WaterSpawners.clear();
		s_Incoming.clear();
		s_Effects.clear();
	}
	std::vector<Stroke> strokes;
	strokes.swap(s_Queue);
	if (!InGame()) {
		s_Possessed = nullptr;
		s_Incoming.clear();
		s_WaterSpawners.clear();
		s_Effects.clear();
		return;
	}
	for (const Stroke& stroke: strokes) {
		Apply(stroke);
	}
	UpdateIncoming();
	UpdateEffects();
	for (const WaterSpawner& spawner: s_WaterSpawners) {
		FluidSim::Pour(spawner.Position, static_cast<float>(spawner.Radius), "Water");
	}
	if (g_TimerMan.GetSimUpdateCount() % 60 == 0) {
		RetargetAttackers();
	}
	UpdateAutoBattle();
	Colony::Update();
	if (s_FollowAction && g_TimerMan.GetSimUpdateCount() % 30 == 0) {
		FindAction();
	}
	if (IsGodMode()) {
		GameActivity* game = CurrentGame();
		if (s_Possessed && !g_MovableMan.IsActor(s_Possessed)) {
			// The unit you were controlling died: back to the god view.
			s_Possessed = nullptr;
			s_Flying = false;
			g_DebugMan.OpenTools();
			s_FreeCameraStarted = false;
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
