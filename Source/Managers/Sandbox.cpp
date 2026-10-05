#include "WindowMan.h"
#include "DebugMan.h"
#include "Sandbox.h"
#include "ACrab.h"
#include "ACraft.h"
#include "ADoor.h"
#include "AHuman.h"
#include "ActivityMan.h"
#include "CameraMan.h"
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

#include "imgui/imgui.h"

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <list>
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
		LooseSand,
		LooseSnow,
		Boulder,
		Slab,
		Smoke,
		ToxicGas,
		Dig,
		Earth,
		Sand,
		Grass,
		Wood,
		Concrete,
		Grenade,
		BigBomb,
		Napalm,
		Lightning,
		// Not tools, but queued the same way.
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
	    {Tool::LooseSand, "Loose sand", 0.03F, true},
	    {Tool::LooseSnow, "Loose snow", 0.03F, true},
	    {Tool::Boulder, "Boulder", 0.0F, true},
	    {Tool::Slab, "Concrete lump", 0.0F, true},
	    {Tool::Smoke, "Smoke", 0.06F, true},
	    {Tool::ToxicGas, "Toxic gas", 0.06F, true},
	    {Tool::Dig, "Dig", 0.03F, true},
	    {Tool::Earth, "Earth", 0.03F, true},
	    {Tool::Sand, "Sand", 0.03F, true},
	    {Tool::Grass, "Grass", 0.03F, true},
	    {Tool::Wood, "Wood", 0.03F, true},
	    {Tool::Concrete, "Concrete", 0.03F, true},
	    {Tool::Grenade, "Grenade blast", 0.0F, false},
	    {Tool::BigBomb, "Big bomb", 0.0F, false},
	    {Tool::Napalm, "Napalm burst", 0.0F, false},
	    {Tool::Lightning, "Lightning", 0.0F, false},
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
	void AddPresets(std::vector<Preset>& list, const std::list<Entity*>& entities, bool buyableOnly, bool skipBrains) {
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
		for (const char* group: {"Bunker Modules", "Bunker Systems", "Bunker Lights"}) {
			std::list<Entity*> entities;
			g_PresetMan.GetAllOfGroup(entities, group, "All");
			AddPresets(s_Structures, entities, false, false);
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

	int& ChoiceFor(Tool kind) {
		switch (kind) {
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
			if (actor == of || !IsCombatant(actor) || actor->GetTeam() == of->GetTeam()) {
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
			if (!materialName) {
				TerrainCollapse::QueueCheck(center, static_cast<float>(radius + 30));
			}
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

	void PlaceStructure(const Stroke& stroke) {
		const Preset* preset = ChosenPreset(Tool::Structure, stroke.Choice);
		const Entity* entity = preset ? g_PresetMan.GetEntityPreset(preset->ClassName, preset->PresetName, preset->ModuleID) : nullptr;
		SceneObject* object = entity ? dynamic_cast<SceneObject*>(entity->Clone()) : nullptr;
		if (!object) {
			return;
		}
		if (dynamic_cast<TerrainObject*>(object)) {
			// Position plus the bitmap offset is the top left corner.
			object->SetPos(StructureCorner(*preset, stroke.Position, stroke.Count > 0) - Vector(preset->OffsetX, preset->OffsetY));
		} else {
			// Doors and other moving bunker parts belong to a side, and open for it.
			object->SetPos(stroke.Position);
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
			Sandbox::Toggle();
			g_ConsoleMan.PrintString("SANDBOX: You're controlling " + actor->GetPresetName() + ". Press F7 to go back to the god view.");
		}
	}

	void ReleaseControl() {
		if (GameActivity* game = CurrentGame()) {
			game->LoseControlOfActor(Players::PlayerOne);
			game->SetViewState(Activity::ViewState::Observe, Players::PlayerOne);
		}
		s_Possessed = nullptr;
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
		if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
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

	void ToolButtons(std::initializer_list<Tool> tools) {
		int column = 0;
		for (Tool kind: tools) {
			if (column++ % 4 != 0) {
				ImGui::SameLine();
			}
			ImGui::RadioButton(c_Tools[ToolIndex(kind)].Name, &s_ToolIndex, ToolIndex(kind));
		}
	}

	void SideChooser() {
		for (int side = 0; side < c_Sides; ++side) {
			if (side > 0) {
				ImGui::SameLine();
			}
			ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
			ImGui::RadioButton(c_SideNames[side], &s_Team, side);
			ImGui::PopStyleColor();
		}
	}

	void PresetList(Tool kind) {
		const std::vector<Preset>& list = ListFor(kind);
		int& choice = ChoiceFor(kind);
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::InputTextWithHint("##filter", "Search...", s_Filter, sizeof(s_Filter));
		if (ImGui::BeginListBox("##presets", ImVec2(-1.0F, ImGui::GetTextLineHeightWithSpacing() * 8.0F))) {
			for (int i = 0; i < static_cast<int>(list.size()); ++i) {
				if (!ContainsIgnoringCase(list[i].Label, s_Filter)) {
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

	void DrawCursor() {
		ImGuiIO& io = ImGui::GetIO();
		const ToolInfo& tool = CurrentTool();
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		ImU32 white = IM_COL32(255, 255, 255, 170);
		std::string label = tool.Name;
		if (tool.Kind == Tool::Structure) {
			if (const Preset* preset = ChosenPreset(Tool::Structure, s_StructureChoice); preset && preset->Width > 0) {
				// The footprint, where it will land.
				Vector corner = StructureCorner(*preset, MouseScenePosition(), s_SnapToGrid) - g_CameraMan.GetOffset(0);
				ImVec2 topLeft(ViewOrigin().x + corner.m_X / scale, ViewOrigin().y + corner.m_Y / scale);
				drawList->AddRect(topLeft, ImVec2(topLeft.x + static_cast<float>(preset->Width) / scale, topLeft.y + static_cast<float>(preset->Height) / scale), white, 0.0F, 0, 1.5F);
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
		drawList->AddText(ImVec2(io.MousePos.x + 14.0F, io.MousePos.y - 8.0F), IM_COL32(255, 255, 255, 220), label.c_str());
	}

	ImVec2 ToScreen(const Vector& scenePosition) {
		Vector onScreen = g_SceneMan.ShortestDistance(g_CameraMan.GetOffset(0), scenePosition, g_SceneMan.SceneWrapsX());
		float scale = ScenePixelsPerWindowPixel();
		return ImVec2(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
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
#pragma endregion
} // namespace

bool Sandbox::Do(const std::string& toolName, const Vector& position, int team, int order, int count, const std::string& presetName) {
	if (!InGame()) {
		return false;
	}
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
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
	if (stroke.Kind == Tool::Unit || stroke.Kind == Tool::Drop || stroke.Kind == Tool::Brain || stroke.Kind == Tool::Item || stroke.Kind == Tool::Structure) {
		const std::vector<Preset>& list = ListFor(stroke.Kind);
		auto found = std::find_if(list.begin(), list.end(), [&presetName](const Preset& preset) { return preset.PresetName == presetName; });
		if (found == list.end()) {
			return false;
		}
		stroke.Choice = static_cast<int>(found - list.begin());
		if (stroke.Kind == Tool::Structure) {
			stroke.Count = 1;
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
		s_Open = false;
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

bool Sandbox::IsGodMode() {
	const Activity* activity = g_ActivityMan.GetActivity();
	return activity && InGame() && activity->GetPresetName() == "Sandbox";
}

bool Sandbox::WantsWheelZoom() {
	return s_Open && IsGodMode() && !s_Possessed && !ImGui::GetIO().WantCaptureMouse;
}

bool Sandbox::CapturesWorldClicks() {
	return s_Open && CurrentTool().Kind != Tool::None && InGame() && !ImGui::GetIO().WantCaptureMouse;
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
	if (Controller::IsAIPaused() && InGame()) {
		// A reminder that nobody will move until it's resumed.
		const char* banner = "AI PAUSED";
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		ImVec2 size = ImGui::CalcTextSize(banner);
		float scale = 1.6F;
		ImVec2 at(g_WindowMan.GetGameViewRect().x + (g_WindowMan.GetGameViewRect().w - size.x * scale) * 0.5F, g_WindowMan.GetGameViewRect().y + 36.0F);
		drawList->AddRectFilled(ImVec2(at.x - 10.0F, at.y - 4.0F), ImVec2(at.x + size.x * scale + 10.0F, at.y + size.y * scale + 4.0F), IM_COL32(0, 0, 0, 150), 4.0F);
		drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, at, IM_COL32(255, 210, 80, 255), banner);
	}
	if (!s_Open) {
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
		DrawCursor();
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

	ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 445.0F, 40.0F), ImGuiCond_FirstUseEver);
	if (g_DebugMan.BeginPanel(IsGodMode() ? "Sandbox (F7)###Sandbox" : "Sandbox tools (F7)###Sandbox", &s_Open, DebugMan::PanelSide::Left)) {
		if (!InGame()) {
			ImGui::TextWrapped("Start a game to use the sandbox. Pick \"Sandbox\" in the scenario menu for the full god mode.");
			ImGui::End();
			return;
		}
		SideStatus();
		bool aiPaused = Controller::IsAIPaused();
		ImGui::PushStyleColor(ImGuiCol_Text, aiPaused ? IM_COL32(255, 210, 80, 255) : ImGui::GetColorU32(ImGuiCol_Text));
		if (ImGui::Checkbox("Pause AI (set things up, then let them loose)", &aiPaused)) {
			Controller::SetAIPaused(aiPaused);
		}
		ImGui::PopStyleColor();
		ImGui::TextDisabled("Left click: use tool.  Right drag / WASD: move camera.  Wheel: zoom.");
		ToolButtons({Tool::None, Tool::Command, Tool::Follow, Tool::Possess});
		ToolButtons({Tool::Remove, Tool::RallyPoint});

		if (ImGui::BeginTabBar("SandboxTabs")) {
			if (ImGui::BeginTabItem("Spawn")) {
				ToolButtons({Tool::Unit, Tool::Drop, Tool::Brain, Tool::Item});
				if (CurrentTool().Kind == Tool::Structure) {
					s_ToolIndex = ToolIndex(Tool::Unit);
				}
				if (ImGui::Button("Build bunkers with the build menu", ImVec2(-1.0F, 0.0F))) {
					// The game's own build menu, placing straight into the world. Choose Done in its pie menu (or press F7) to come back.
					Sandbox::SetBuildMode(true);
				}
				Tool kind = CurrentTool().Kind;
				if (kind == Tool::Unit || kind == Tool::Drop || kind == Tool::Brain || kind == Tool::Item || kind == Tool::Structure) {
					PresetList(kind);
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
					ImGui::Checkbox("Pull the pin (grenades)", &s_LitGrenade);
				} else if (kind == Tool::Structure) {
					ImGui::Checkbox("Snap to the bunker grid", &s_SnapToGrid);
				}
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Orders")) {
				ImGui::TextWrapped("Give every unit on a side new orders. Units told to attack find a new target when theirs dies.");
				SideChooser();
				ImGui::Combo("Orders", &s_Order, c_OrderNames);
				if (ImGui::Button("Give orders", ImVec2(-1.0F, 0.0F))) {
					Stroke stroke;
					stroke.Kind = Tool::OrderSide;
					stroke.Team = s_Team;
					stroke.Orders = static_cast<Order>(s_Order);
					s_Queue.push_back(stroke);
				}
				if (ImGui::Button("Everyone attack!", ImVec2(-1.0F, 0.0F))) {
					for (int side = 0; side < c_Sides; ++side) {
						Stroke stroke;
						stroke.Kind = Tool::OrderSide;
						stroke.Team = side;
						stroke.Orders = Order::Attack;
						s_Queue.push_back(stroke);
					}
				}
				if (ImGui::Button("Remove this side's units", ImVec2(-1.0F, 0.0F))) {
					Stroke stroke;
					stroke.Kind = Tool::RemoveSide;
					stroke.Team = s_Team;
					s_Queue.push_back(stroke);
				}
				ImGui::TextDisabled("Rally point: pick the tool above and click to place this side's flag.");

				ImGui::SeparatorText("Selected units");
				int selected = static_cast<int>(std::count_if(s_Selected.begin(), s_Selected.end(), [](const UnitRef& ref) { return GetRef(ref) != nullptr; }));
				ImGui::Text("%d selected. Use the Command tool: drag a box to select, click to move or attack.", selected);
				if (ImGui::Button("Give the selected these orders") && selected > 0) {
					Stroke stroke;
					stroke.Kind = Tool::OrderSelected;
					stroke.Orders = static_cast<Order>(s_Order);
					s_Queue.push_back(stroke);
				}
				ImGui::SameLine();
				if (ImGui::Button("Clear selection")) {
					s_Selected.clear();
				}

				ImGui::SeparatorText("Auto battle");
				ImGui::TextWrapped("Each side buys waves of its faction's units with its budget and drops them in to attack, until one side is left.");
				for (int side = 0; side < c_Sides && !s_FactionNames.empty(); ++side) {
					AutoSide& autoSide = s_AutoSides[side];
					ImGui::PushID(side);
					ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
					ImGui::Checkbox(c_SideNames[side], &autoSide.Active);
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
					if (ImGui::Button("Start auto battle", ImVec2(-1.0F, 0.0F))) {
						Sandbox::StartAutoBattle();
					}
				} else if (ImGui::Button("Stop auto battle", ImVec2(-1.0F, 0.0F))) {
					s_AutoRunning = false;
				}
				if (s_AutoWinner >= 0) {
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c_SideColors[s_AutoWinner]), "%s won the last battle.", c_SideNames[s_AutoWinner]);
				} else if (s_AutoWinner == -1) {
					ImGui::Text("The last battle was a draw.");
				}
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Paint")) {
				ImGui::SeparatorText("Elements");
				ToolButtons({Tool::Fire, Tool::Water, Tool::Lava, Tool::Acid, Tool::Oil, Tool::Smoke, Tool::ToxicGas});
				ImGui::SeparatorText("Loose things");
				ToolButtons({Tool::LooseSand, Tool::LooseSnow, Tool::Boulder, Tool::Slab});
				ImGui::SeparatorText("Terrain");
				ToolButtons({Tool::Dig, Tool::Earth, Tool::Sand, Tool::Grass, Tool::Wood, Tool::Concrete});
				ImGui::SliderInt("Brush size", &s_Radius, 1, 40);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Boom")) {
				ToolButtons({Tool::Grenade, Tool::BigBomb, Tool::Napalm, Tool::Lightning});
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("World")) {
				LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
				ImGui::Combo("Weather", &settings.WeatherType, "Clear\0Rain\0Snow\0Ash fall\0Dust storm\0");
				ImGui::SliderFloat("Intensity", &settings.WeatherIntensity, 0.0F, 1.0F);
				ImGui::SliderFloat("Wind", &settings.Wind, -200.0F, 200.0F, "%.0f px/s");
				ImGui::SliderFloat("Hour", &settings.TimeOfDay, 0.0F, 24.0F, "%.1f");
				for (auto [name, hour]: {std::pair{"Dawn", 6.5F}, std::pair{"Day", 12.0F}, std::pair{"Dusk", 19.0F}, std::pair{"Night", 23.0F}}) {
					if (hour != 6.5F) {
						ImGui::SameLine();
					}
					if (ImGui::Button(name)) {
						settings.TimeOfDay = hour;
					}
				}
				ImGui::Separator();
				float zoom = g_FrameMan.GetCameraZoom();
				if (ImGui::SliderFloat("Zoom", &zoom, FrameMan::c_MinCameraZoom, FrameMan::c_MaxCameraZoom, "%.2fx")) {
					g_FrameMan.SetCameraZoom(zoom);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("1x")) {
					g_FrameMan.SetCameraZoom(1.0F);
				}
				ImGui::Checkbox("Free camera", &s_FreeCamera);
				ImGui::SameLine();
				ImGui::Checkbox("Follow the action", &s_FollowAction);
				ImGui::SameLine();
				if (ImGui::Checkbox("Slow motion", &s_SlowMotion)) {
					g_TimerMan.SetTimeScale(s_SlowMotion ? 0.25F : 1.0F);
				}
				ImGui::Text("%d burning, %d liquid pixels flowing", TerrainFire::GetCount(), FluidSim::GetActiveCount());
				if (ImGui::Button("Put out all fire")) {
					TerrainFire::Clear();
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void Sandbox::Update() {
	static const Activity* lastActivity = nullptr;
	if (g_ActivityMan.GetActivity() != lastActivity) {
		lastActivity = g_ActivityMan.GetActivity();
		Controller::SetAIPaused(false);
	}
	std::vector<Stroke> strokes;
	strokes.swap(s_Queue);
	if (!InGame()) {
		s_Possessed = nullptr;
		return;
	}
	for (const Stroke& stroke: strokes) {
		Apply(stroke);
	}
	if (g_TimerMan.GetSimUpdateCount() % 60 == 0) {
		RetargetAttackers();
	}
	UpdateAutoBattle();
	if (s_FollowAction && g_TimerMan.GetSimUpdateCount() % 30 == 0) {
		FindAction();
	}
	if (IsGodMode()) {
		GameActivity* game = CurrentGame();
		if (s_Possessed && !g_MovableMan.IsActor(s_Possessed)) {
			// The unit you were controlling died: back to the god view.
			s_Possessed = nullptr;
			s_Open = true;
			s_FreeCameraStarted = false;
		}
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
