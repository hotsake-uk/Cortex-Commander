// The Battle tab's setups saved with a map: every team's card, the mode and its choices, and the zones, points and lines drawn for them,
// kept in Userdata/BattlePresets/<map>.txt. One of a map's setups can be marked to load with it, so it is there again whenever the map
// starts: after a reset, or in a fresh game.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		/// One saved setup.
		struct BattlePreset {
			std::string Name;
			bool WithMap = false; //!< Set up again whenever the map starts.
			bool Linked = false; //!< One of the map's linked battles, played in the list's order.
			std::array<BattleSettings, c_Sides> Cards;
			BattleModeSettings Mode;
		};

		std::vector<BattlePreset> s_Presets; //!< The setups saved for s_PresetsScene.
		std::string s_PresetsScene; //!< The map they were read for ("" before any was).
		bool s_PresetsRead = false;
		bool s_PresetPending = false; //!< A game has started: its map's setup marked "with map" goes in on the next update.
		char s_PresetName[64] = "";
		std::string s_PresetNote; //!< What the last save or load did, shown under the list.

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
			visit("Attacker", mode.Attacker);
			visit("CaptureSeconds", mode.CaptureSeconds);
			visit("TimeLimit", mode.TimeLimit);
			visit("BonusSeconds", mode.BonusSeconds);
			visit("Tickets", mode.Tickets);
			visit("KillsToWin", mode.KillsToWin);
			visit("VipRespawnSeconds", mode.VipRespawnSeconds);
			visit("Defender", mode.Defender);
			visit("Waves", mode.Waves);
			visit("FirstWave", mode.FirstWave);
			visit("WaveGrowth", mode.WaveGrowth);
			visit("WaveBreak", mode.WaveBreak);
			visit("EscortSide", mode.EscortSide);
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

		std::string Trim(const std::string& text) {
			size_t from = text.find_first_not_of(" \t\r");
			size_t to = text.find_last_not_of(" \t\r");
			return from == std::string::npos ? std::string() : text.substr(from, to - from + 1);
		}

		/// A setup made from the lines read under its name.
		BattlePreset ReadPreset(const std::string& name, const std::map<std::string, std::string>& lines) {
			BattlePreset preset;
			preset.Name = name;
			auto withMap = lines.find("WithMap");
			preset.WithMap = withMap != lines.end() && withMap->second == "1";
			auto linked = lines.find("Linked");
			preset.Linked = linked != lines.end() && linked->second == "1";
			for (int side = 0; side < c_Sides; ++side) {
				Loader load{lines, "Team" + std::to_string(side) + "."};
				VisitCard(preset.Cards[side], load);
				preset.Cards[side].Craft = std::clamp(preset.Cards[side].Craft, 0, static_cast<int>(std::size(c_Crafts)) - 1);
			}
			Loader load{lines, "Mode."};
			// (Saved before each team had its own size: the one size, for every team.)
			if (auto size = lines.find("Mode.TeamSize"); size != lines.end()) {
				int teamSize = preset.Mode.TeamSize[0];
				FromText(size->second, teamSize);
				preset.Mode.TeamSize.fill(teamSize);
			}
			VisitMode(preset.Mode, load);
			preset.Mode.Attacker = std::clamp(preset.Mode.Attacker, 0, c_Sides - 1);
			if (preset.Mode.FlagSpots.size() > c_MaxFlagSpots) {
				preset.Mode.FlagSpots.resize(c_MaxFlagSpots);
			}
			return preset;
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
			s_PresetNote.clear();
			if (scene.empty()) {
				return;
			}
			std::ifstream file(PresetsFile(scene));
			std::string line;
			std::string name;
			std::map<std::string, std::string> lines;
			bool inPreset = false;
			auto finish = [&]() {
				if (inPreset) {
					s_Presets.push_back(ReadPreset(name, lines));
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
		}

		void WritePresets() {
			if (s_PresetsScene.empty()) {
				return;
			}
			std::filesystem::create_directories("Userdata/BattlePresets");
			std::ofstream file(PresetsFile(s_PresetsScene), std::ios::trunc);
			file << "// Battle Director setups for " << s_PresetsScene << ", saved from the Battle tab.\n";
			for (BattlePreset& preset: s_Presets) {
				file << "\n[" << preset.Name << "]\n";
				file << "WithMap = " << ToText(preset.WithMap) << '\n';
				file << "Linked = " << ToText(preset.Linked) << '\n';
				for (int side = 0; side < c_Sides; ++side) {
					Saver save{file, "Team" + std::to_string(side) + "."};
					VisitCard(preset.Cards[side], save);
				}
				Saver save{file, "Mode."};
				VisitMode(preset.Mode, save);
			}
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

	void BattlePresetsNewGame() {
		s_PresetPending = true;
		s_Linked = LinkedRun();
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
		auto withMap = std::find_if(s_Presets.begin(), s_Presets.end(), [](const BattlePreset& preset) { return preset.WithMap; });
		if (withMap != s_Presets.end()) {
			UsePreset(*withMap);
			g_ConsoleMan.PrintString("SANDBOX: Battle setup \"" + withMap->Name + "\" loaded with the map.");
		}
	}

	void BattlePresetsPanel() {
		ReadPresets();
		std::string header = "Saved setups for this map" + (s_Presets.empty() ? std::string() : " (" + std::to_string(s_Presets.size()) + ")") + "###battlePresets";
		if (!ImGui::CollapsingHeader(header.c_str())) {
			return;
		}
		if (s_PresetsScene.empty()) {
			ImGui::TextDisabled("No map open.");
			return;
		}
		ImGui::TextWrapped("Saves every team's card, the mode and its choices, and the spawn zones, points, goals, flag spots and lines drawn on this map. Tick \"With map\" on one and it is set up again whenever this map starts: after a reset, or in a new game.");
		float buttonWidth = ImGui::CalcTextSize("Save").x + ImGui::GetStyle().FramePadding.x * 2.0F;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
		bool entered = ImGui::InputTextWithHint("##battlePresetName", "Setup name", s_PresetName, sizeof(s_PresetName), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		std::string name = CleanName(s_PresetName);
		ImGui::BeginDisabled(name.empty());
		if ((ToolUI::Button("Save", ImVec2(-1.0F, 0.0F)) || entered) && !name.empty()) {
			auto same = std::find_if(s_Presets.begin(), s_Presets.end(), [&name](const BattlePreset& preset) { return preset.Name == name; });
			BattlePreset preset;
			preset.Name = name;
			preset.Cards = s_BattleSetup;
			preset.Mode = s_ModeSetup;
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
			s_PresetName[0] = 0;
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Saves the Battle tab as it is now, under this name (over the one of the same name, if there is one).");

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
			}
			ImGui::SetItemTooltip("Puts this setup on the Battle tab. A battle that's on keeps going: start it again to play the setup.");
			ImGui::SameLine();
			if (ImGui::SmallButton("Save over")) {
				preset.Cards = s_BattleSetup;
				preset.Mode = s_ModeSetup;
				s_PresetNote = "\"" + preset.Name + "\" saved over.";
				changed = true;
			}
			ImGui::SetItemTooltip("Saves the Battle tab as it is now in place of this setup.");
			ImGui::SameLine();
			if (ImGui::SmallButton("X")) {
				remove = i;
			}
			ImGui::SetItemTooltip("Deletes this setup.");
			ImGui::SameLine();
			ImGui::TextUnformatted(preset.Name.c_str());
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
