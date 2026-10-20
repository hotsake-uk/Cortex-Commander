// The Battle tab's setups saved with a map: every team's card, the mode and its choices, and the zones, points and lines drawn for them,
// kept in Userdata/BattlePresets/<map>.txt. One of a map's setups can be marked to load with it, so it is there again whenever the map
// starts: after a reset, or in a fresh game. The setup each battle was last started with is kept too (B-11), in a file of its own under
// Userdata/BattlePresets/LastSetup, and offered first in the list as "Last setup".
// Mods can bring battles too, in the same form, in <mod>.rte/Battles/*.txt (Cortex Studio's Battle editor writes them), each setup with a
// "Scene = " line naming its map. They're listed with the map's own (in the Battle tab and the map browser), to load and play but not to change,
// and a scenario built on Battle Command (a copy of it under the battle's name) sets up and starts the battle of its name when it starts.

#include "SandboxInternal.h"
#include "Notices.h"
#include "DataModule.h"

namespace SandboxDetail {
	namespace {
		using BattlePreset = SavedBattle;

		std::vector<BattlePreset> s_Presets; //!< The setups saved for s_PresetsScene.
		std::string s_PresetsScene; //!< The map they were read for ("" before any was).
		bool s_PresetsRead = false;
		bool s_PresetPending = false; //!< A game has started: its map's setup marked "with map" goes in on the next update.
		bool s_PresetResumed = false; //!< The game started is a saved one, its battle setup put back (LoadBattleSetups): none goes in over it.
		char s_PresetName[64] = "";
		std::string s_PresetNote; //!< What the last save or load did, shown under the list.
		std::string s_PresetOnStart; //!< The setup the map browser picked to go in when the next game starts, in place of the one with the map.
		bool s_NoPresetOnStart = false; //!< The map browser picked no setup for the next game: none goes in.
		bool s_SaveHoldZones = true; //!< Whether a setup saved now keeps the map's hold zones ("Hold ground") with it.
		bool s_StartOnStart = false; //!< The map browser's quick battle: the setup that goes in with the next game is started too.
		constexpr const char* c_LastSetupName = "Last setup"; //!< The name the last setup is kept under in its file.
		std::vector<BattlePreset> s_LastSetup; //!< The last setup started on s_PresetsScene, if one was kept (one at most).

		/// Linked battles: the setups being played in turn, by name, and which is on.
		struct LinkedRun {
			bool On = false;
			std::vector<std::string> Names;
			int At = 0;
		};
		LinkedRun s_Linked;

		/// A team's factions, saved by module name (a module's ID depends on which mods are loaded).
		struct FactionList {
			std::vector<int>& Ids;
		};

		/// Every field of a team's card, each to a visitor with its name: the one list both saving and loading go by.
		template <typename Visitor> void VisitCard(BattleSettings& card, Visitor& visit) {
			visit("Active", card.Active);
			FactionList factions{card.Factions};
			visit("Factions", factions);
			visit("FavouritesOnly", card.FavouritesOnly);
			visit("Crabs", card.Crabs);
			visit("JetpackOnly", card.JetpackOnly);
			visit("Style", card.Style);
			visit("EndlessMoney", card.EndlessMoney);
			visit("Budget", card.Budget);
			visit("WaveSize", card.WaveSize);
			visit("UnitLimit", card.UnitLimit);
			visit("Craft", card.Craft);
			visit("DropOnLine", card.DropOnLine);
			visit("HasLine", card.HasLine);
			visit("LineA", card.LineA);
			visit("LineB", card.LineB);
			visit("ShipsPerBurst", card.ShipsPerBurst);
			visit("SpawnZones", card.SpawnZones);
			visit("ZoneEverySeconds", card.ZoneEverySeconds);
			visit("ZoneUnits", card.ZoneUnits);
			visit("AppearAnywhere", card.AppearAnywhere);
			visit("KeepClear", card.KeepClear);
			visit("EverySeconds", card.EverySeconds);
			visit("Invincible", card.Invincible);
			visit("HasDefendPos", card.HasDefendPos);
			visit("DefendPos", card.DefendPos);
			visit("DefendRadius", card.DefendRadius);
			visit("ChaseDistance", card.ChaseDistance);
			visit("RoamPercent", card.RoamPercent);
		}

		/// Every field of the mode panel, as VisitCard.
		template <typename Visitor> void VisitMode(BattleModeSettings& mode, Visitor& visit) {
			visit("Mode", mode.Mode);
			visit("TeamSizes", mode.TeamSize);
			visit("Plays", mode.Plays);
			visit("SpawnZones", mode.SpawnZones);
			visit("HasPoint", mode.HasPoint);
			visit("Points", mode.Points);
			visit("Goals", mode.Goals);
			visit("FlagSpots", mode.FlagSpots);
			visit("FlagByZones", mode.FlagByZones);
			visit("ByShip", mode.ByShip);
			visit("Anywhere", mode.Anywhere);
			visit("KeepClear", mode.KeepClear);
			visit("MoveStuckPoint", mode.MoveStuckPoint);
			visit("ScoreToWin", mode.ScoreToWin);
			visit("GuardPercent", mode.GuardPercent);
			visit("EscortPercent", mode.EscortPercent);
			visit("ReturnSeconds", mode.ReturnSeconds);
			visit("RespawnSeconds", mode.RespawnSeconds);
			visit("MaxRespawns", mode.MaxRespawns);
			visit("StuckSeconds", mode.StuckSeconds);
			visit("RushPercent", mode.RushPercent);
			visit("RiskMin", mode.RiskMin);
			visit("RiskMax", mode.RiskMax);
			visit("RouteVariety", mode.RouteVariety);
			visit("Zones", mode.Zones);
			visit("HoldToWin", mode.HoldToWin);
			visit("HillMoveSeconds", mode.HillMoveSeconds);
			visit("MajorityScores", mode.MajorityScores);
			visit("HillsToWin", mode.HillsToWin);
			visit("Attacker", mode.Attacker);
			visit("CaptureSeconds", mode.CaptureSeconds);
			visit("TimeLimit", mode.TimeLimit);
			visit("BonusSeconds", mode.BonusSeconds);
			visit("BlockLastRespawns", mode.BlockLastRespawns);
			visit("AttackerSpawn", mode.AttackerSpawn);
			visit("Tickets", mode.Tickets);
			visit("KillsToWin", mode.KillsToWin);
			visit("VipRespawnSeconds", mode.VipRespawnSeconds);
			visit("Defender", mode.Defender);
			visit("Waves", mode.Waves);
			visit("FirstWave", mode.FirstWave);
			visit("WaveGrowth", mode.WaveGrowth);
			visit("WaveBreak", mode.WaveBreak);
			visit("EscortSide", mode.EscortSide);
			visit("PointRings", mode.PointRings);
			visit("RingRadius", mode.RingRadius);
			visit("RingPoints", mode.RingPoints);
			visit("PointsToWin", mode.PointsToWin);
			visit("ExtractSide", mode.ExtractSide);
			visit("ExtractToWin", mode.ExtractToWin);
			visit("ExtractSeconds", mode.ExtractSeconds);
			visit("Commander", mode.Commander);
			visit("CommanderLevel", mode.CommanderLevel);
			visit("CommanderReserve", mode.CommanderReserve);
			visit("CommanderFallBack", mode.CommanderFallBack);
			visit("CommanderSquads", mode.CommanderSquads);
			visit("PlayerCommands", mode.PlayerCommands);
			visit("FogOfWar", mode.FogOfWar);
			visit("FieldBrain", mode.FieldBrain);
			visit("FieldBrainRange", mode.FieldBrainRange);
			visit("HotSeat", mode.HotSeat);
			visit("TurnSeconds", mode.TurnSeconds);
			visit("PickArmy", mode.PickArmy);
			visit("ArmyPoints", mode.ArmyPoints);
			visit("Army", mode.Army);
			visit("MovingSpawns", mode.MovingSpawns);
			visit("SpawnOrders", mode.SpawnOrders);
			visit("ZoneSpawnOrders", mode.ZoneSpawnOrders);
			visit("Pickups", mode.Pickups);
			visit("PickupEvery", mode.PickupEvery);
			visit("PickupMost", mode.PickupMost);
			visit("PickupLasts", mode.PickupLasts);
			visit("PickupKinds", mode.PickupKinds);
			visit("PickupPlaces", mode.PickupPlaces);
			visit("PickupSpots", mode.PickupSpots);
			visit("PickupZones", mode.PickupZones);
			visit("ShowObjectives", mode.ShowObjectives);
			visit("ObjectiveLook", mode.ObjectiveLook);
			visit("ShowSpawnZones", mode.ShowSpawnZones);
			visit("ShowModeZones", mode.ShowModeZones);
			visit("ShowInfo", mode.ShowInfo);
		}

		// Values as text: a point "x,y", a line of points with spaces between, zones with " | " between.
		std::string ToText(bool value) { return value ? "1" : "0"; }
		std::string ToText(int value) { return std::to_string(value); }
		template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0> std::string ToText(E value) { return std::to_string(static_cast<int>(value)); }
		std::string ToText(const Vector& value) { return std::to_string(static_cast<int>(std::round(value.m_X))) + "," + std::to_string(static_cast<int>(std::round(value.m_Y))); }
		std::string ToText(const std::vector<Vector>& points) {
			std::string text;
			for (const Vector& point: points) {
				text += (text.empty() ? "" : " ") + ToText(point);
			}
			return text;
		}
		std::string ToText(const std::vector<std::vector<Vector>>& zones) {
			std::string text;
			for (size_t i = 0; i < zones.size(); ++i) {
				text += (i ? " | " : "") + ToText(zones[i]);
			}
			return text;
		}
		// A spawn order: "set kind facing risk% backAndForth radius chase roam; x,y x,y" (its points after the semicolon); a zone's of them with " | " between.
		std::string ToText(const SpawnOrder& order) {
			return ToText(order.Set) + " " + ToText(order.Kind) + " " + ToText(order.Facing) + " " + ToText(static_cast<int>(std::round(order.Direct * 100.0F))) + " " + ToText(order.BackAndForth) + " " + ToText(order.Radius) + " " + ToText(order.Chase) + " " + ToText(order.Roam) + "; " + ToText(order.Points);
		}
		std::string ToText(const std::vector<SpawnOrder>& orders) {
			std::string text;
			for (size_t i = 0; i < orders.size(); ++i) {
				text += (i ? " | " : "") + ToText(orders[i]);
			}
			return text;
		}
		std::string ToText(const FactionList& factions) {
			std::string text;
			for (int id: factions.Ids) {
				if (id >= 0 && id < g_PresetMan.GetTotalModuleCount()) {
					text += (text.empty() ? "" : ";") + g_PresetMan.GetDataModuleName(id);
				}
			}
			return text;
		}

		void FromText(const std::string& text, bool& value) { value = std::atoi(text.c_str()) != 0; }
		void FromText(const std::string& text, int& value) { value = std::atoi(text.c_str()); }
		template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0> void FromText(const std::string& text, E& value) {
			value = static_cast<E>(std::clamp(std::atoi(text.c_str()), 0, static_cast<int>(E::Count) - 1));
		}
		void FromText(const std::string& text, Vector& value) {
			float x = 0.0F;
			float y = 0.0F;
			if (std::sscanf(text.c_str(), "%f,%f", &x, &y) == 2) {
				value = Vector(x, y);
			}
		}
		void FromText(const std::string& text, std::vector<Vector>& points) {
			points.clear();
			std::istringstream words(text);
			std::string word;
			while (words >> word) {
				Vector point;
				if (std::count(word.begin(), word.end(), ',') == 1) {
					FromText(word, point);
					points.push_back(point);
				}
			}
		}
		void FromText(const std::string& text, std::vector<std::vector<Vector>>& zones) {
			zones.clear();
			std::istringstream parts(text);
			std::string part;
			while (std::getline(parts, part, '|')) {
				std::vector<Vector> zone;
				FromText(part, zone);
				if (zone.size() >= 3) {
					zones.push_back(std::move(zone));
				}
			}
		}
		void FromText(const std::string& text, SpawnOrder& order) {
			order = SpawnOrder();
			const size_t split = text.find(';');
			int set = 0;
			int kind = 0;
			int risk = 0;
			int backAndForth = 0;
			if (std::sscanf(text.substr(0, split).c_str(), "%d %d %d %d %d %d %d %d", &set, &kind, &order.Facing, &risk, &backAndForth, &order.Radius, &order.Chase, &order.Roam) >= 2) {
				order.Set = set != 0;
				order.Kind = static_cast<SpawnOrderKind>(std::clamp(kind, 0, static_cast<int>(SpawnOrderKind::Count) - 1));
				order.Facing = std::clamp(order.Facing, -1, 1);
				order.Direct = std::clamp(static_cast<float>(risk) / 100.0F, -1.0F, 1.0F);
				order.BackAndForth = backAndForth != 0;
			}
			if (split != std::string::npos) {
				FromText(text.substr(split + 1), order.Points);
			}
		}
		void FromText(const std::string& text, std::vector<SpawnOrder>& orders) {
			orders.clear();
			std::istringstream parts(text);
			std::string part;
			while (std::getline(parts, part, '|')) {
				SpawnOrder order;
				FromText(part, order);
				orders.push_back(order);
			}
		}
		void FromText(const std::string& text, FactionList& factions) {
			factions.Ids.clear();
			std::istringstream names(text);
			std::string name;
			while (std::getline(names, name, ';')) {
				// (A faction whose mod isn't loaded now is left out.)
				int id = name.empty() ? -1 : g_PresetMan.GetModuleID(name);
				if (id >= 0 && std::find(factions.Ids.begin(), factions.Ids.end(), id) == factions.Ids.end()) {
					factions.Ids.push_back(id);
				}
			}
		}

		/// Writes each field visited as "prefix.Name = value", a line each.
		struct Saver {
			std::ostream& Out;
			std::string Prefix;

			template <typename T> void operator()(const char* name, T& value) { Out << Prefix << name << " = " << ToText(value) << '\n'; }
			template <typename T, size_t N> void operator()(const char* name, std::array<T, N>& values) {
				for (size_t i = 0; i < N; ++i) {
					(*this)((std::string(name) + "." + std::to_string(i)).c_str(), values[i]);
				}
			}
		};

		/// Reads each field visited from the lines read, by its name; a field not there keeps the value it has.
		struct Loader {
			const std::map<std::string, std::string>& Lines;
			std::string Prefix;

			template <typename T> void operator()(const char* name, T& value) {
				auto found = Lines.find(Prefix + name);
				if (found != Lines.end()) {
					FromText(found->second, value);
				}
			}
			template <typename T, size_t N> void operator()(const char* name, std::array<T, N>& values) {
				for (size_t i = 0; i < N; ++i) {
					(*this)((std::string(name) + "." + std::to_string(i)).c_str(), values[i]);
				}
			}
		};

		std::string SceneName() { return g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetPresetName() : ""; }

		std::string PresetsFile(const std::string& scene) {
			std::string file = scene;
			for (char& c: file) {
				if (std::strchr("<>:\"/\\|?*", c) || static_cast<unsigned char>(c) < 32) {
					c = '_';
				}
			}
			return "Userdata/BattlePresets/" + file + ".txt";
		}

		/// The file the setup a map's battle was last started with is kept in: apart from its saved setups, so the map browser doesn't count it.
		std::string LastSetupFile(const std::string& scene) {
			std::string file = PresetsFile(scene);
			return "Userdata/BattlePresets/LastSetup/" + file.substr(std::strlen("Userdata/BattlePresets/"));
		}

		std::string Trim(const std::string& text) {
			size_t from = text.find_first_not_of(" \t\r");
			size_t to = text.find_last_not_of(" \t\r");
			return from == std::string::npos ? std::string() : text.substr(from, to - from + 1);
		}

		/// A team's card from the lines read, its fields under a prefix ("Team0."): a field not there keeps the value it has.
		void ReadCard(const std::map<std::string, std::string>& lines, const std::string& prefix, BattleSettings& card) {
			Loader load{lines, prefix};
			VisitCard(card, load);
			card.Craft = std::clamp(card.Craft, 0, static_cast<int>(std::size(c_Crafts)) - 1);
		}

		/// The mode panel from the lines read, as ReadCard.
		void ReadMode(const std::map<std::string, std::string>& lines, const std::string& prefix, BattleModeSettings& mode) {
			Loader load{lines, prefix};
			// (Saved before each team had its own size: the one size, for every team.)
			if (auto size = lines.find(prefix + "TeamSize"); size != lines.end()) {
				int teamSize = mode.TeamSize[0];
				FromText(size->second, teamSize);
				mode.TeamSize.fill(teamSize);
			}
			VisitMode(mode, load);
			mode.Attacker = std::clamp(mode.Attacker, 0, c_Sides - 1);
			for (int* show: {&mode.ShowObjectives, &mode.ShowSpawnZones, &mode.ShowModeZones, &mode.ShowInfo}) {
				*show = std::clamp(*show, -1, 1);
			}
			mode.ObjectiveLook = std::clamp(mode.ObjectiveLook, -1, static_cast<int>(ObjectiveLook::Count));
			if (mode.FlagSpots.size() > c_MaxFlagSpots) {
				mode.FlagSpots.resize(c_MaxFlagSpots);
			}
			if (mode.PickupSpots.size() > c_MaxPickupSpots) {
				mode.PickupSpots.resize(c_MaxPickupSpots);
			}
			if (mode.PickupZones.size() > c_MaxPickupZones) {
				mode.PickupZones.resize(c_MaxPickupZones);
			}
		}

		/// A setup made from the lines read under its name.
		BattlePreset ReadPreset(const std::string& name, const std::map<std::string, std::string>& lines) {
			BattlePreset preset;
			preset.Name = name;
			if (auto scene = lines.find("Scene"); scene != lines.end()) {
				preset.Scene = scene->second;
			}
			auto withMap = lines.find("WithMap");
			preset.WithMap = withMap != lines.end() && withMap->second == "1";
			auto linked = lines.find("Linked");
			preset.Linked = linked != lines.end() && linked->second == "1";
			if (auto hold = lines.find("HoldZones"); hold != lines.end()) {
				preset.HasHoldZones = true;
				FromText(hold->second, preset.HoldZones);
			}
			for (int side = 0; side < c_Sides; ++side) {
				ReadCard(lines, "Team" + std::to_string(side) + ".", preset.Cards[side]);
			}
			ReadMode(lines, "Mode.", preset.Mode);
			return preset;
		}

		/// Reads the setups in a file in the saved form.
		std::vector<BattlePreset> ReadPresetsFrom(const std::string& path) {
			std::vector<BattlePreset> presets;
			std::ifstream file(path);
			std::string line;
			std::string name;
			std::map<std::string, std::string> lines;
			bool inPreset = false;
			auto finish = [&]() {
				if (inPreset) {
					presets.push_back(ReadPreset(name, lines));
				}
				lines.clear();
			};
			while (std::getline(file, line)) {
				line = Trim(line);
				if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
					finish();
					name = line.substr(1, line.size() - 2);
					inPreset = true;
				} else if (size_t equals = line.find('='); equals != std::string::npos) {
					lines[Trim(line.substr(0, equals))] = Trim(line.substr(equals + 1));
				}
			}
			finish();
			return presets;
		}

		/// Reads the setups saved for a map from its file (or its last setup's).
		std::vector<BattlePreset> ReadPresetsFile(const std::string& scene, bool lastSetup = false) { return ReadPresetsFrom(lastSetup ? LastSetupFile(scene) : PresetsFile(scene)); }

		std::vector<BattlePreset> s_ModBattles; //!< Every loaded mod's battles, for any map.
		bool s_ModBattlesRead = false;
		std::vector<BattlePreset> s_ModPresets; //!< The mods' battles for s_PresetsScene.

		/// Every loaded mod's battles (<mod>.rte/Battles/*.txt, its files in name order), read once until ForgetModBattles.
		const std::vector<BattlePreset>& ModBattles() {
			if (s_ModBattlesRead) {
				return s_ModBattles;
			}
			s_ModBattlesRead = true;
			s_ModBattles.clear();
			for (int i = 0; i < g_PresetMan.GetTotalModuleCount(); ++i) {
				const DataModule* module = g_PresetMan.GetDataModule(i);
				if (!module) {
					continue;
				}
				std::error_code error;
				const std::filesystem::path folder = g_PresetMan.GetFullModulePath(module->GetFileName() + "/Battles");
				if (!std::filesystem::is_directory(folder, error)) {
					continue;
				}
				std::vector<std::filesystem::path> files;
				for (const std::filesystem::directory_entry& entry: std::filesystem::directory_iterator(folder, error)) {
					std::string extension = entry.path().extension().string();
					std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (entry.is_regular_file(error) && extension == ".txt") {
						files.push_back(entry.path());
					}
				}
				std::sort(files.begin(), files.end());
				for (const std::filesystem::path& path: files) {
					for (BattlePreset& preset: ReadPresetsFrom(path.generic_string())) {
						if (preset.Scene.empty()) {
							continue;
						}
						// (The player's own say which of a map's setups loads with it, and which are linked: not a mod's.)
						preset.Mod = module->GetFileName();
						preset.WithMap = false;
						preset.Linked = false;
						s_ModBattles.push_back(std::move(preset));
					}
				}
			}
			return s_ModBattles;
		}

		std::vector<BattlePreset> ModBattlesFor(const std::string& scene) {
			std::vector<BattlePreset> found;
			for (const BattlePreset& preset: ModBattles()) {
				if (preset.Scene == scene) {
					found.push_back(preset);
				}
			}
			return found;
		}

		/// The mod a battle came from, as the lists show it.
		std::string ModLabel(const BattlePreset& preset) {
			const DataModule* module = g_PresetMan.GetDataModule(g_PresetMan.GetModuleID(preset.Mod));
			return module && !module->GetFriendlyName().empty() ? module->GetFriendlyName() : preset.Mod;
		}

		/// Reads the setups saved for the map now in play, unless they were read for it already.
		void ReadPresets() {
			std::string scene = SceneName();
			if (s_PresetsRead && scene == s_PresetsScene) {
				return;
			}
			s_PresetsRead = true;
			s_PresetsScene = scene;
			s_Presets.clear();
			s_ModPresets.clear();
			s_PresetNote.clear();
			if (scene.empty()) {
				return;
			}
			s_Presets = ReadPresetsFile(scene);
			s_ModPresets = ModBattlesFor(scene);
			s_LastSetup = ReadPresetsFile(scene, true);
			s_LastSetup.resize(std::min<size_t>(s_LastSetup.size(), 1));
		}

		/// Writes setups to a file, each under its name.
		void WritePresetsTo(const std::string& path, const std::string& heading, std::vector<BattlePreset>& presets) {
			std::filesystem::create_directories(std::filesystem::path(path).parent_path());
			std::ofstream file(path, std::ios::trunc);
			file << heading << '\n';
			for (BattlePreset& preset: presets) {
				file << "\n[" << preset.Name << "]\n";
				file << "WithMap = " << ToText(preset.WithMap) << '\n';
				file << "Linked = " << ToText(preset.Linked) << '\n';
				if (preset.HasHoldZones) {
					file << "HoldZones = " << ToText(preset.HoldZones) << '\n';
				}
				for (int side = 0; side < c_Sides; ++side) {
					Saver save{file, "Team" + std::to_string(side) + "."};
					VisitCard(preset.Cards[side], save);
				}
				Saver save{file, "Mode."};
				VisitMode(preset.Mode, save);
			}
		}

		void WritePresets() {
			if (s_PresetsScene.empty()) {
				return;
			}
			WritePresetsTo(PresetsFile(s_PresetsScene), "// Battle Director setups for " + s_PresetsScene + ", saved from the Battle tab.", s_Presets);
		}

		/// Puts a setup on the Battle tab and sends it to the sim, as if each choice had been made there. A battle on keeps going.
		void UsePreset(const BattlePreset& preset) {
			if (IsBattleTool(CurrentTool().Kind)) {
				PutDownBattleTool();
			}
			s_ZoneDraft.clear();
			s_BattleSetup = preset.Cards;
			s_ModeSetup = preset.Mode;
			for (int side = 0; side < c_Sides; ++side) {
				SendBattleSettings(side);
			}
			SendBattleMode();
			if (preset.HasHoldZones) {
				TerrainCollapse::SetHoldZones(preset.HoldZones);
			}
		}

		/// The hold zones on the map, kept with a setup being saved if "Hold zones" is ticked.
		void TakeHoldZones(BattlePreset& preset) {
			preset.HasHoldZones = s_SaveHoldZones;
			preset.HoldZones = s_SaveHoldZones ? TerrainCollapse::GetHoldZones() : std::vector<std::vector<Vector>>();
		}

		/// Plays a linked battle: its setup on the Battle tab, and its game started. With carry, your team's points and survivors (and their
		/// ranks) go on into it.
		bool PlayLinked(int at, bool carry) {
			if (at < 0 || at >= static_cast<int>(s_Linked.Names.size())) {
				return false;
			}
			auto preset = std::find_if(s_Presets.begin(), s_Presets.end(), [&](const BattlePreset& each) { return each.Name == s_Linked.Names[at]; });
			if (preset == s_Presets.end()) {
				s_PresetNote = "Linked battle \"" + s_Linked.Names[at] + "\" is no longer saved: the linked battles end here.";
				s_Linked.On = false;
				return false;
			}
			s_Linked.At = at;
			if (carry) {
				CarryIntoNextBattle();
			}
			UsePreset(*preset);
			SendBattleMode(BattleModeStart);
			s_PresetNote = "Linked battle " + std::to_string(at + 1) + " of " + std::to_string(s_Linked.Names.size()) + ": \"" + preset->Name + "\".";
			return true;
		}

		void StartLinked() {
			s_Linked = LinkedRun();
			for (const BattlePreset& preset: s_Presets) {
				if (preset.Linked) {
					s_Linked.Names.push_back(preset.Name);
				}
			}
			s_Linked.On = !s_Linked.Names.empty();
			PlayLinked(0, false);
		}

		/// A name fit for a "[Name]" line.
		std::string CleanName(const char* typed) {
			std::string name = Trim(typed);
			name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c == '[' || c == ']' || c == '\n' || c == '\r'; }), name.end());
			return name;
		}
	} // namespace

	std::vector<SavedBattle> ReadSavedBattles(const std::string& scene) {
		if (scene.empty()) {
			return {};
		}
		std::vector<SavedBattle> battles = ReadPresetsFile(scene);
		for (SavedBattle& battle: ModBattlesFor(scene)) {
			battles.push_back(std::move(battle));
		}
		return battles;
	}

	void ForgetModBattles() {
		s_ModBattlesRead = false;
		s_ModBattles.clear();
		s_PresetsRead = false;
	}

	void LoadBattleOnStart(const std::string& name, bool none) {
		s_PresetOnStart = none ? std::string() : name;
		s_NoPresetOnStart = none;
	}

	void StartBattleOnStart(bool start) {
		s_StartOnStart = start;
	}

	void RememberLastBattleSetup() {
		ReadPresets();
		if (s_PresetsScene.empty()) {
			return;
		}
		BattlePreset last;
		last.Name = c_LastSetupName;
		last.Cards = s_BattleSetup;
		last.Mode = s_ModeSetup;
		s_LastSetup = {last};
		WritePresetsTo(LastSetupFile(s_PresetsScene), "// The battle setup last started on " + s_PresetsScene + ", kept by itself each time a battle starts.", s_LastSetup);
	}

	bool AskFirst(bool clicked, const char* popup, const std::string& question, const char* yes) {
		bool confirmed = false;
		if (clicked) {
			// (Ctrl+click, or asking turned off: at once.)
			if (!g_SettingsMan.BattleAskFirst() || ImGui::GetIO().KeyCtrl) {
				confirmed = true;
			} else {
				ImGui::OpenPopup(popup);
			}
		}
		if (ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted(question.c_str());
			bool dontAsk = !g_SettingsMan.BattleAskFirst();
			if (ToolUI::Checkbox("Don't ask again", &dontAsk)) {
				g_SettingsMan.SetBattleAskFirst(!dontAsk);
			}
			ImGui::SetItemTooltip("Saved setups deleted or saved over, and a battle started again or stopped, go ahead at once from now on.\nTurn asking back on under Game & HUD in the settings (F6). Ctrl+click always skips the question.");
			if (ToolUI::SmallButton(yes)) {
				confirmed = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemTooltip("Goes ahead.");
			ImGui::SameLine();
			if (ToolUI::SmallButton("Cancel")) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::SetItemTooltip("Leaves things as they are.");
			ImGui::EndPopup();
		}
		return confirmed;
	}

	void BattlePresetsNewGame() {
		ForgetModBattles();
		s_PresetPending = true;
		s_PresetResumed = false;
		s_Linked = LinkedRun();
	}

	void SaveBattleSetups(std::ostream& out) {
		// As a setup is saved to its file, a field a line ("Team0.Budget = 5000"), with the sim's copies besides the Battle tab's: the mode as
		// its game runs ("Run.") and each team as it plays it ("Sim0.", a mode's teams being set up from their cards by the mode). All as one word.
		std::ostringstream text;
		for (int side = 0; side < c_Sides; ++side) {
			Saver card{text, "Team" + std::to_string(side) + "."};
			VisitCard(s_BattleSetup[side], card);
			Saver team{text, "Sim" + std::to_string(side) + "."};
			VisitCard(s_BattleTeams[side].Settings, team);
		}
		Saver mode{text, "Mode."};
		VisitMode(s_ModeSetup, mode);
		Saver run{text, "Run."};
		VisitMode(s_ModeRun.Settings, run);
		SaveText(out, text.str());
		out << ' ' << s_Linked.On << ' ' << s_Linked.At << ' ' << s_Linked.Names.size();
		for (const std::string& name: s_Linked.Names) {
			SaveText(out, name);
		}
	}

	bool LoadBattleSetups(std::istream& in) {
		std::istringstream text(LoadText(in));
		std::map<std::string, std::string> lines;
		for (std::string line; std::getline(text, line);) {
			if (size_t equals = line.find('='); equals != std::string::npos) {
				lines[Trim(line.substr(0, equals))] = Trim(line.substr(equals + 1));
			}
		}
		LinkedRun linked;
		size_t names = 0;
		in >> linked.On >> linked.At >> names;
		for (size_t i = 0; i < names && in; ++i) {
			linked.Names.push_back(LoadText(in));
		}
		if (!in || !lines.count("Mode.Mode") || !lines.count("Run.Mode")) {
			return false;
		}
		for (int side = 0; side < c_Sides; ++side) {
			ReadCard(lines, "Team" + std::to_string(side) + ".", s_BattleSetup[side]);
			ReadCard(lines, "Sim" + std::to_string(side) + ".", s_BattleTeams[side].Settings);
		}
		ReadMode(lines, "Mode.", s_ModeSetup);
		ReadMode(lines, "Run.", s_ModeRun.Settings);
		s_Linked = linked;
		// The setup saved with the game is the one it was played with: not the one marked to load with the map (BattlePresetsUpdate), nor one
		// the map browser picked.
		s_PresetResumed = true;
		return true;
	}

	bool LinkedBattleControls() {
		if (!s_Linked.On || s_Linked.Names.empty()) {
			return false;
		}
		const int count = static_cast<int>(s_Linked.Names.size());
		const int at = std::clamp(s_Linked.At, 0, count - 1);
		ImGui::TextColored(ImVec4(0.6F, 0.85F, 1.0F, 1.0F), "Linked battles: %d of %d, \"%s\"", at + 1, count, s_Linked.Names[at].c_str());
		if (!s_ModeRun.Running || !s_ModeRun.Over) {
			return false;
		}
		const int yours = CommandedTeam();
		const bool won = yours < 0 || s_ModeRun.Winner == yours;
		if (!won) {
			ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "Lost: the linked battles end here.");
			if (ToolUI::Button("Start the linked battles over", ImVec2(-1.0F, 0.0F))) {
				StartLinked();
				return true;
			}
			return false;
		}
		if (at + 1 >= count) {
			ImGui::TextColored(ImVec4(0.5F, 1.0F, 0.5F, 1.0F), "Every linked battle won!");
			return false;
		}
		const std::string label = "Next battle: " + s_Linked.Names[at + 1];
		const bool clicked = ToolUI::Button(label.c_str(), ImVec2(-1.0F, 0.0F));
		if (clicked) {
			PlayLinked(at + 1, true);
		}
		ImGui::SetItemTooltip("On to the next linked battle. Your points left over and your units still standing (with their ranks) go on into it, and count towards your team's size there.");
		return clicked;
	}

	void BattlePresetsUpdate() {
		if (!s_PresetPending || !g_SceneMan.GetScene()) {
			return;
		}
		s_PresetPending = false;
		ReadPresets();
		// The map browser's pick, if it made one, for this game only.
		std::string picked = s_PresetOnStart;
		bool none = s_NoPresetOnStart;
		const bool startPicked = s_StartOnStart;
		LoadBattleOnStart("", false);
		StartBattleOnStart(false);
		if (none || s_PresetResumed) {
			s_PresetResumed = false;
			return;
		}
		bool start = startPicked;
		// A scenario built on Battle Command under a mod battle's name (made in Cortex Studio): that battle, started at once.
		if (const Activity* activity = g_ActivityMan.GetActivity(); picked.empty() && activity && Sandbox::IsBattleCommand() && activity->GetPresetName() != "Battle Command") {
			const std::string& own = activity->GetPresetName();
			if (std::any_of(s_ModPresets.begin(), s_ModPresets.end(), [&own](const BattlePreset& preset) { return preset.Name == own; })) {
				picked = own;
				start = true;
			}
		}
		auto withMap = std::find_if(s_Presets.begin(), s_Presets.end(), [&picked](const BattlePreset& preset) { return picked.empty() ? preset.WithMap : preset.Name == picked; });
		// (A mod's battle when picked by its name and none of the player's own has it.)
		auto fromMod = withMap != s_Presets.end() || picked.empty() ? s_ModPresets.end() : std::find_if(s_ModPresets.begin(), s_ModPresets.end(), [&picked](const BattlePreset& preset) { return preset.Name == picked; });
		// (The last setup only when picked by its name, and no saved setup has that name.)
		const bool last = withMap == s_Presets.end() && fromMod == s_ModPresets.end() && !s_LastSetup.empty() && picked == c_LastSetupName;
		if (withMap == s_Presets.end() && fromMod == s_ModPresets.end() && !last) {
			return;
		}
		const BattlePreset& preset = last ? s_LastSetup.front() : withMap != s_Presets.end() ? *withMap : *fromMod;
		UsePreset(preset);
		g_ConsoleMan.PrintString("SANDBOX: Battle setup \"" + preset.Name + "\" loaded with the map.");
		Notices::Post("Battle setup \"" + preset.Name + "\" loaded with the map");
		// The main menu's quick battle: started at once, if it can be.
		if (start) {
			std::string why;
			if (BattleModeReady(why)) {
				SendBattleMode(BattleModeStart);
			} else {
				g_ConsoleMan.PrintString("SANDBOX: Battle setup \"" + preset.Name + "\" can't be started as it is: " + why);
			}
		}
	}

	void BattlePresetsPanel() {
		ReadPresets();
		const size_t count = s_Presets.size() + s_ModPresets.size();
		std::string header = "Saved setups for this map" + (count == 0 ? std::string() : " (" + std::to_string(count) + ")") + "###battlePresets";
		if (!ImGui::CollapsingHeader(header.c_str())) {
			return;
		}
		if (s_PresetsScene.empty()) {
			ImGui::TextDisabled("No map open.");
			return;
		}
		ImGui::TextWrapped("Saves every team's card, the mode and its choices, the spawn zones, points, goals, flag spots and lines drawn on this map, and (with Hold zones ticked) where the ground never falls. Tick \"With map\" on one and it is set up again whenever this map starts: after a reset, or in a new game.");
		float buttonWidth = ImGui::CalcTextSize("Save").x + ImGui::GetStyle().FramePadding.x * 2.0F;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
		bool entered = ImGui::InputTextWithHint("##battlePresetName", "Setup name", s_PresetName, sizeof(s_PresetName), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		std::string name = CleanName(s_PresetName);
		ImGui::BeginDisabled(name.empty());
		const bool saveClicked = (ToolUI::Button("Save", ImVec2(-1.0F, 0.0F)) || entered) && !name.empty();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Saves the Battle tab as it is now, under this name (over the one of the same name, if there is one: you're asked first, Ctrl+click not).");
		const bool exists = std::any_of(s_Presets.begin(), s_Presets.end(), [&name](const BattlePreset& preset) { return preset.Name == name; });
		if (exists ? AskFirst(saveClicked, "Save over?##battlePresetSaveSame", "Save over \"" + name + "\"? The setup saved under that name is lost.", "Yes, save over") : saveClicked) {
			auto same = std::find_if(s_Presets.begin(), s_Presets.end(), [&name](const BattlePreset& preset) { return preset.Name == name; });
			BattlePreset preset;
			preset.Name = name;
			preset.Cards = s_BattleSetup;
			preset.Mode = s_ModeSetup;
			TakeHoldZones(preset);
			if (same != s_Presets.end()) {
				preset.WithMap = same->WithMap;
				*same = preset;
				s_PresetNote = "\"" + name + "\" saved over.";
			} else {
				// The first setup saved for a map loads with it, until another is ticked.
				preset.WithMap = s_Presets.empty();
				s_Presets.push_back(preset);
				s_PresetNote = "\"" + name + "\" saved.";
			}
			WritePresets();
			Notices::Post("Battle setup \"" + name + "\" saved");
			s_PresetName[0] = 0;
		}
		std::string holdLabel = "Hold zones (" + std::to_string(TerrainCollapse::GetHoldZones().size()) + ")###battlePresetHold";
		ToolUI::Checkbox(holdLabel.c_str(), &s_SaveHoldZones);
		ImGui::SetItemTooltip("Saves the map's hold zones (Paint > Terrain > Hold ground: where the ground never falls) with the setup, so loading it puts them back\nin place of those on the map then. Off: the setup leaves the hold zones alone.");

		// The setup the last battle on this map was started with, kept by itself, first.
		if (!s_LastSetup.empty()) {
			ImGui::PushID("lastSetup");
			if (ImGui::SmallButton("Load")) {
				UsePreset(s_LastSetup.front());
				s_PresetNote = "The last setup loaded.";
				Notices::Post("The last battle setup loaded");
			}
			ImGui::SetItemTooltip("Puts the setup the last battle on this map was started with on the Battle tab (kept by itself each time a battle starts).\nA battle that's on keeps going: start it again to play the setup.");
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.6F, 0.85F, 1.0F, 1.0F), "%s", c_LastSetupName);
			ImGui::SameLine();
			ImGui::TextDisabled("(kept each time a battle starts)");
			ImGui::PopID();
		}

		int remove = -1;
		bool changed = false;
		for (int i = 0; i < static_cast<int>(s_Presets.size()); ++i) {
			BattlePreset& preset = s_Presets[i];
			ImGui::PushID(i);
			if (ToolUI::Checkbox("With map", &preset.WithMap)) {
				// Only one loads with the map.
				for (BattlePreset& other: s_Presets) {
					other.WithMap = &other == &preset && preset.WithMap;
				}
				changed = true;
			}
			ImGui::SetItemTooltip("Set this up whenever the map starts: after a reset, or in a new game.");
			ImGui::SameLine();
			changed |= ToolUI::Checkbox("Linked", &preset.Linked);
			ImGui::SetItemTooltip("One of this map's linked battles: played one after another, in this list's order, with your points left over and your survivors\ngoing on from each won battle into the next.");
			ImGui::SameLine();
			if (ImGui::SmallButton("Load")) {
				UsePreset(preset);
				s_PresetNote = "\"" + preset.Name + "\" loaded.";
				Notices::Post("Battle setup \"" + preset.Name + "\" loaded");
			}
			ImGui::SetItemTooltip("Puts this setup on the Battle tab. A battle that's on keeps going: start it again to play the setup.");
			ImGui::SameLine();
			const bool saveOver = ImGui::SmallButton("Save over");
			ImGui::SetItemTooltip("Saves the Battle tab as it is now in place of this setup. You're asked first (Ctrl+click: not).");
			if (AskFirst(saveOver, "Save over?##battlePresetSaveOver", "Save over \"" + preset.Name + "\"? The setup saved now is lost.", "Yes, save over")) {
				preset.Cards = s_BattleSetup;
				preset.Mode = s_ModeSetup;
				TakeHoldZones(preset);
				s_PresetNote = "\"" + preset.Name + "\" saved over.";
				Notices::Post("Battle setup \"" + preset.Name + "\" saved");
				changed = true;
			}
			ImGui::SameLine();
			const bool deleteClicked = ImGui::SmallButton("X");
			ImGui::SetItemTooltip("Deletes this setup. You're asked first (Ctrl+click: not).");
			if (AskFirst(deleteClicked, "Delete the setup?##battlePresetDelete", "Delete \"" + preset.Name + "\"? It can't be brought back.", "Yes, delete")) {
				remove = i;
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(preset.Name.c_str());
			ImGui::PopID();
		}
		// The mods' battles for this map: to load, not to change.
		for (int i = 0; i < static_cast<int>(s_ModPresets.size()); ++i) {
			const BattlePreset& preset = s_ModPresets[i];
			ImGui::PushID(("mod" + std::to_string(i)).c_str());
			if (ImGui::SmallButton("Load")) {
				UsePreset(preset);
				s_PresetNote = "\"" + preset.Name + "\" loaded, from " + ModLabel(preset) + ".";
				Notices::Post("Battle setup \"" + preset.Name + "\" loaded");
			}
			ImGui::SetItemTooltip("Puts this battle on the Battle tab. It comes with a mod: save it under a name of your own to change it.\nA battle that's on keeps going: start it again to play the setup.");
			ImGui::SameLine();
			ImGui::TextUnformatted(preset.Name.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("(from %s)", ModLabel(preset).c_str());
			ImGui::PopID();
		}
		const int linked = static_cast<int>(std::count_if(s_Presets.begin(), s_Presets.end(), [](const BattlePreset& preset) { return preset.Linked; }));
		if (linked > 0) {
			const std::string label = "Play the linked battles (" + std::to_string(linked) + ")";
			if (ToolUI::Button(label.c_str(), ImVec2(-1.0F, 0.0F))) {
				StartLinked();
			}
			ImGui::SetItemTooltip("Starts the first linked setup. Win it and the next can be started from the battle panel or the after-battle report, your points left over\nand your units still standing going on into it. Lose and the run ends.");
		}
		if (remove >= 0) {
			s_PresetNote = "\"" + s_Presets[remove].Name + "\" deleted.";
			s_Presets.erase(s_Presets.begin() + remove);
			changed = true;
		}
		if (changed) {
			WritePresets();
		}
		if (!s_PresetNote.empty()) {
			ImGui::TextDisabled("%s", s_PresetNote.c_str());
		}
		ImGui::Separator();
	}
} // namespace SandboxDetail

bool Sandbox::LoadBattleSetup(const std::string& file, const std::string& name, bool start) {
	using namespace SandboxDetail;
	if (!InGame()) {
		return false;
	}
	// (No file: the setups of the map in play, the player's own and the mods' (ReadSavedBattles).)
	std::vector<BattlePreset> presets = file.empty() ? ReadSavedBattles(SceneName()) : ReadPresetsFrom(g_PresetMan.GetFullModulePath(file));
	auto found = std::find_if(presets.begin(), presets.end(), [&name](const BattlePreset& preset) { return name.empty() || preset.Name == name; });
	if (found == presets.end()) {
		g_ConsoleMan.PrintString("SANDBOX: No battle setup " + (name.empty() ? std::string() : "\"" + name + "\" ") + (file.empty() ? "saved for " + SceneName() : "in " + file) + ".");
		return false;
	}
	UsePreset(*found);
	if (!start) {
		return true;
	}
	std::string why;
	if (!BattleModeReady(why)) {
		g_ConsoleMan.PrintString("SANDBOX: Battle setup \"" + found->Name + "\" can't be started as it is: " + why);
		return false;
	}
	SendBattleMode(BattleModeStart);
	return true;
}
