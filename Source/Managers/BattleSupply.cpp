// Battle Command: re-arming. Each unit's kit (its guns and what it has to throw) is noted when it's first seen in the battle; the Supply power
// then gives back what it's lost: a gun to one that has none, and its grenades back up to what it came with.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		struct Kit {
			std::string ClassName;
			std::string PresetName;
			int ModuleID = 0;
			int Count = 1;
		};
		struct Loadout {
			std::vector<Kit> Guns; //!< Best first.
			std::vector<Kit> Thrown;
		};
		std::unordered_map<long, Loadout> s_Loadouts; //!< By unique ID.

		bool IsGun(const MovableObject* item) {
			HDFirearm* gun = dynamic_cast<HDFirearm*>(const_cast<MovableObject*>(item));
			return gun && !gun->IsTool() && !gun->IsInGroup("Tools - Diggers");
		}

		bool IsThrown(const MovableObject* item) { return dynamic_cast<const TDExplosive*>(item) != nullptr; }

		/// Calls each item a unit carries, in hand or not.
		template <typename Each> void ForEachItem(const AHuman* human, Each each) {
			for (const MovableObject* item: {static_cast<const MovableObject*>(human->GetEquippedItem()), static_cast<const MovableObject*>(human->GetEquippedBGItem())}) {
				if (item) {
					each(item);
				}
			}
			for (const MovableObject* item: *human->GetInventory()) {
				if (item) {
					each(item);
				}
			}
		}

		void Note(std::vector<Kit>& kits, const MovableObject* item) {
			for (Kit& kit: kits) {
				if (kit.PresetName == item->GetPresetName() && kit.ModuleID == item->GetModuleID()) {
					++kit.Count;
					return;
				}
			}
			kits.push_back({item->GetClassName(), item->GetPresetName(), item->GetModuleID(), 1});
		}
	} // namespace

	void ForgetLoadouts() { s_Loadouts.clear(); }

	namespace {
		/// A list of kit by its names, the module by name too (its ID depends on the mods loaded), for a saved game.
		void SaveKitList(std::ostream& out, const std::vector<Kit>& kits) {
			out << ' ' << kits.size();
			for (const Kit& kit: kits) {
				SaveText(out, kit.ClassName);
				SaveText(out, kit.PresetName);
				SaveText(out, kit.ModuleID >= 0 && kit.ModuleID < g_PresetMan.GetTotalModuleCount() ? g_PresetMan.GetDataModuleName(kit.ModuleID) : std::string());
				out << ' ' << kit.Count;
			}
		}

		/// Reads one written by SaveKitList: kit from a mod not loaded now is left out.
		std::vector<Kit> LoadKitList(std::istream& in) {
			std::vector<Kit> kits;
			size_t count = 0;
			in >> count;
			for (size_t i = 0; i < count && in; ++i) {
				Kit kit;
				kit.ClassName = LoadText(in);
				kit.PresetName = LoadText(in);
				const std::string module = LoadText(in);
				in >> kit.Count;
				kit.ModuleID = module.empty() ? -1 : g_PresetMan.GetModuleID(module);
				if (kit.ModuleID >= 0) {
					kits.push_back(kit);
				}
			}
			return kits;
		}
	} // namespace

	void SaveKits(std::ostream& out) {
		// (Only those of units still about: the fallen are only forgotten now and then.)
		std::vector<std::pair<long, const Loadout*>> kept;
		for (const auto& [id, loadout]: s_Loadouts) {
			if (g_MovableMan.FindObjectByUniqueID(id)) {
				kept.emplace_back(id, &loadout);
			}
		}
		out << ' ' << kept.size();
		for (const auto& [id, loadout]: kept) {
			out << ' ' << id;
			SaveKitList(out, loadout->Guns);
			SaveKitList(out, loadout->Thrown);
		}
	}

	void LoadKits(std::istream& in, const LoadedIDs& ids) {
		size_t count = 0;
		in >> count;
		for (size_t i = 0; i < count && in; ++i) {
			long id = 0;
			in >> id;
			Loadout loadout;
			loadout.Guns = LoadKitList(in);
			loadout.Thrown = LoadKitList(in);
			if (const long unit = ids(id); unit != 0) {
				s_Loadouts[unit] = std::move(loadout);
			}
		}
	}

	void UpdateLoadouts() {
		if (!Sandbox::IsBattleCommand() || !s_ModeRun.Running || g_TimerMan.GetSimUpdateCount() % 60 != 23) {
			return;
		}
		if (s_Loadouts.size() > 4096) {
			// The fallen forgotten, now and then.
			std::erase_if(s_Loadouts, [](const auto& entry) { return !g_MovableMan.FindObjectByUniqueID(entry.first); });
		}
		for (Actor* actor: SandboxAccess::Actors()) {
			const AHuman* human = dynamic_cast<const AHuman*>(actor);
			const long id = static_cast<long>(actor->GetUniqueID());
			if (!human || !IsCombatant(actor) || s_Loadouts.contains(id)) {
				continue;
			}
			Loadout loadout;
			std::vector<const MovableObject*> guns;
			ForEachItem(human, [&](const MovableObject* item) {
				if (IsGun(item)) {
					guns.push_back(item);
				} else if (IsThrown(item)) {
					Note(loadout.Thrown, item);
				}
			});
			std::sort(guns.begin(), guns.end(), [](const MovableObject* a, const MovableObject* b) { return a->GetGoldValue() > b->GetGoldValue(); });
			for (const MovableObject* gun: guns) {
				Note(loadout.Guns, gun);
			}
			// (One seen with nothing yet, as it comes out of a craft's hold, is noted again later.)
			if (!loadout.Guns.empty() || !loadout.Thrown.empty()) {
				s_Loadouts.emplace(id, std::move(loadout));
			}
		}
	}

	int RearmUnit(AHuman* human) {
		auto found = s_Loadouts.find(static_cast<long>(human->GetUniqueID()));
		if (found == s_Loadouts.end()) {
			return 0;
		}
		const Loadout& loadout = found->second;
		int given = 0;
		bool hasGun = false;
		std::unordered_map<std::string, int> thrown;
		ForEachItem(human, [&](const MovableObject* item) {
			hasGun = hasGun || IsGun(item);
			if (IsThrown(item)) {
				++thrown[item->GetPresetName()];
			}
		});
		if (!hasGun && !loadout.Guns.empty()) {
			const Kit& kit = loadout.Guns.front();
			if (MovableObject* gun = CreateObject(kit.ClassName, kit.PresetName, kit.ModuleID)) {
				human->AddInventoryItem(gun);
				human->EquipFirearm(true);
				++given;
			}
		}
		for (const Kit& kit: loadout.Thrown) {
			for (int have = thrown[kit.PresetName]; have < kit.Count; ++have) {
				if (MovableObject* item = CreateObject(kit.ClassName, kit.PresetName, kit.ModuleID)) {
					human->AddInventoryItem(item);
					++given;
				}
			}
		}
		return given;
	}
} // namespace SandboxDetail
