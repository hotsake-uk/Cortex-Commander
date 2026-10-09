// The catalogue: what can be placed, by faction and kind, and the presets the tools list.

#include "SandboxInternal.h"

namespace SandboxDetail {
	void AddPresets(std::vector<Preset>& list, const std::list<Entity*>& entities, bool buyableOnly, bool skipBrains, const char* group) {
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
			if (const Actor* actor = dynamic_cast<const Actor*>(object)) {
				// How high its jetpack lifts it, worked out from the jet's thrust and fuel against the unit's weight, as the path finder does.
				// A jetpack is no use for this unless it really flies: many mods' units carry one only to fake a hop, from before units
				// could leap on their legs, and those can't get up what a flying unit can.
				const float lift = actor->EstimateJumpHeight();
				preset.JetLift = lift == FLT_MAX ? -1.0F : lift;
				preset.Jetpack = lift >= c_JetpackFlyingLift;
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
} // namespace SandboxDetail
