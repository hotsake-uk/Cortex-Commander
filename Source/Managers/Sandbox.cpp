#include "Sandbox.h"
#include "ACrab.h"
#include "ADoor.h"
#include "AHuman.h"
#include "ActivityMan.h"
#include "CameraMan.h"
#include "ConsoleMan.h"
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
		Fire,
		Water,
		Lava,
		Acid,
		Oil,
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
		Release
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
	    {Tool::Fire, "Fire", 0.08F, true},
	    {Tool::Water, "Water", 0.03F, true},
	    {Tool::Lava, "Lava", 0.03F, true},
	    {Tool::Acid, "Acid", 0.03F, true},
	    {Tool::Oil, "Oil", 0.03F, true},
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
	};

	int s_ToolIndex = 0;
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
	float ScenePixelsPerWindowPixel() { return static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) / std::max(1.0F, ImGui::GetIO().DisplaySize.x); }

	Vector MouseScenePosition() {
		const ImVec2& mouse = ImGui::GetIO().MousePos;
		Vector position = g_CameraMan.GetOffset(0) + Vector(mouse.x, mouse.y) * ScenePixelsPerWindowPixel();
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
		for (const char* group: {"Bunker Modules", "Bunker Systems"}) {
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
			if (actor->GetNumberValue(c_AttackTag) <= 0.0 || actor->IsPlayerControlled() || !IsCombatant(actor)) {
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
		if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			s_CameraCenter -= Vector(io.MouseDelta.x, io.MouseDelta.y) * ScenePixelsPerWindowPixel();
		}
		if (!io.WantCaptureKeyboard) {
			float keySpeed = 400.0F * io.DeltaTime * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0F : 1.0F);
			bool left = ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_A);
			bool right = ImGui::IsKeyDown(ImGuiKey_RightArrow) || ImGui::IsKeyDown(ImGuiKey_D);
			bool up = ImGui::IsKeyDown(ImGuiKey_UpArrow) || ImGui::IsKeyDown(ImGuiKey_W);
			bool down = ImGui::IsKeyDown(ImGuiKey_DownArrow) || ImGui::IsKeyDown(ImGuiKey_S);
			s_CameraCenter.m_X += (right ? keySpeed : 0.0F) - (left ? keySpeed : 0.0F);
			s_CameraCenter.m_Y += (down ? keySpeed : 0.0F) - (up ? keySpeed : 0.0F);
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
				ImVec2 topLeft(corner.m_X / scale, corner.m_Y / scale);
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

	/// Flags marking each side's rally point.
	void DrawRallyPoints() {
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		for (int side = 0; side < c_Sides; ++side) {
			if (!s_RallySet[side]) {
				continue;
			}
			Vector onScreen = g_SceneMan.ShortestDistance(g_CameraMan.GetOffset(0), s_RallyPoints[side], g_SceneMan.SceneWrapsX());
			ImVec2 base(onScreen.m_X / scale, onScreen.m_Y / scale);
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
	if (stroke.Kind == Tool::Unit || stroke.Kind == Tool::Brain || stroke.Kind == Tool::Item || stroke.Kind == Tool::Structure) {
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
		if (IsCombatant(actor) && actor->GetTeam() == team) {
			++count;
		}
	}
	return count;
}

bool Sandbox::IsGodMode() {
	const Activity* activity = g_ActivityMan.GetActivity();
	return activity && InGame() && activity->GetPresetName() == "Sandbox";
}

bool Sandbox::CapturesWorldClicks() {
	return s_Open && CurrentTool().Kind != Tool::None && InGame() && !ImGui::GetIO().WantCaptureMouse;
}

void Sandbox::DrawGUI() {
	// A new Sandbox game opens the god view: the window and the free camera.
	if (IsGodMode()) {
		if (s_GodActivity != g_ActivityMan.GetActivity()) {
			s_GodActivity = g_ActivityMan.GetActivity();
			s_Open = true;
			s_FreeCamera = true;
			s_FreeCameraStarted = false;
			s_CameraWarmupFrames = 30;
			s_Possessed = nullptr;
			s_RallySet.fill(false);
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
	if (!s_Open) {
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
		if (tool.Interval <= 0.0F) {
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

	ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 445.0F, 40.0F), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(IsGodMode() ? "Sandbox (F7)###Sandbox" : "Sandbox tools (F7)###Sandbox", &s_Open)) {
		if (!InGame()) {
			ImGui::TextWrapped("Start a game to use the sandbox. Pick \"Sandbox\" in the scenario menu for the full god mode.");
			ImGui::End();
			return;
		}
		SideStatus();
		ImGui::TextDisabled("Left click: use tool.  Right drag / WASD: move camera.");
		ToolButtons({Tool::None, Tool::Possess, Tool::Remove, Tool::RallyPoint});

		if (ImGui::BeginTabBar("SandboxTabs")) {
			if (ImGui::BeginTabItem("Spawn")) {
				ToolButtons({Tool::Unit, Tool::Brain, Tool::Item});
				if (CurrentTool().Kind == Tool::Structure) {
					s_ToolIndex = ToolIndex(Tool::Unit);
				}
				if (ImGui::Button("Build bunkers with the build menu", ImVec2(-1.0F, 0.0F))) {
					// The game's own build menu, placing straight into the world. Choose Done in its pie menu (or press F7) to come back.
					Sandbox::SetBuildMode(true);
				}
				Tool kind = CurrentTool().Kind;
				if (kind == Tool::Unit || kind == Tool::Brain || kind == Tool::Item || kind == Tool::Structure) {
					PresetList(kind);
				}
				if (kind == Tool::Unit || kind == Tool::Brain || kind == Tool::Structure || kind == Tool::RallyPoint) {
					SideChooser();
				}
				if (kind == Tool::Unit) {
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
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Paint")) {
				ImGui::SeparatorText("Elements");
				ToolButtons({Tool::Fire, Tool::Water, Tool::Lava, Tool::Acid, Tool::Oil, Tool::Smoke, Tool::ToxicGas});
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
				ImGui::Combo("Weather", &settings.WeatherType, "Clear\0Rain\0Snow\0");
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
				ImGui::Checkbox("Free camera", &s_FreeCamera);
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
