// Battle Command: the after-battle report. While a battle is on, every unit in it is kept on record (its kind, its kills, whether it fell),
// with each team's score every five seconds and the time each team spent on each objective; when the battle ends, the world is paused and the
// report opens over it: who won, how long it took, the best of your units, kills and losses by kind, time on the objectives and the score
// over time.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		constexpr const char* c_KindKey = "BattleCard"; //!< The string value a unit called in from a card carries: the card's name.
		constexpr const char* c_OtherKind = "Other";
		constexpr float c_SampleSeconds = 5.0F;
		constexpr long long c_HoldEvery = 30; //!< Sim updates between looks at who is on each objective.
		constexpr const char* c_ModeNames[] = {"Custom", "Capture the flag", "King of the hill", "Assault", "Last team standing", "VIP hunt", "One flag", "Capture the ship"};
		static_assert(std::size(c_ModeNames) == static_cast<size_t>(BattleMode::Count), "A name for each mode.");

		/// One unit's record in the battle.
		struct ReportUnit {
			int Team = -1;
			std::string Kind;
			std::string Name; //!< Its veterancy name, once it has one.
			int Rank = 0;
			int Kills = 0;
			bool Fell = false;
			long long Since = 0; //!< The sim update it was first seen on.
			long long Until = -1; //!< The sim update it fell or the battle ended on, -1 while neither.
		};

		struct Report {
			bool Recording = false;
			long long StartAt = 0;
			long long EndAt = 0;
			long long LastSample = 0;
			BattleMode Mode = BattleMode::Custom;
			std::array<bool, c_Sides> Plays{};
			int Yours = -1;
			int Winner = -1;
			std::string Result;
			std::array<int, c_Sides> Score{};
			std::unordered_map<long, ReportUnit> Units;
			std::vector<std::array<int, c_Sides>> Samples; //!< Each team's score, every c_SampleSeconds from the start (and once at the end).
			std::vector<std::pair<std::string, std::array<float, c_Sides>>> Holds; //!< Seconds each team was alone on each objective.
		};
		Report s_Report;
		bool s_ReportOpen = false;
		bool s_ReportJustOpened = false; //!< Opened in the sim: the world is paused at the next frame.
		bool s_PausedByReport = false;

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		float Seconds(long long updates) { return static_cast<float>(updates) / UpdatesPerSecond(); }

		std::string Clock(float seconds) {
			const int whole = std::max(static_cast<int>(std::round(seconds)), 0);
			char text[16];
			std::snprintf(text, sizeof(text), "%d:%02d", whole / 60, whole % 60);
			return text;
		}

		bool CarriesGroup(const AHuman* human, const char* group) {
			for (const HeldDevice* held: {human->GetEquippedItem(), human->GetEquippedBGItem()}) {
				if (held && held->IsInGroup(group)) {
					return true;
				}
			}
			for (const MovableObject* item: *human->GetInventory()) {
				if (item && item->IsInGroup(group)) {
					return true;
				}
			}
			return false;
		}

		/// What a unit is, by the reinforcement cards: the card it was called in from, else worked out from what it is and carries.
		std::string KindOf(const Actor* actor) {
			if (actor->StringValueExists(c_KindKey)) {
				return actor->GetStringValue(c_KindKey);
			}
			if (dynamic_cast<const ACrab*>(actor)) {
				return "Crab";
			}
			if (const AHuman* human = dynamic_cast<const AHuman*>(actor)) {
				if (CarriesGroup(human, "Tools - Diggers")) {
					return "Digger";
				}
				if (CarriesGroup(human, "Weapons - Sniper")) {
					return "Sniper";
				}
				if (CarriesGroup(human, "Weapons - Heavy")) {
					return "Heavy";
				}
				return "Rifleman";
			}
			return c_OtherKind;
		}

		/// Its veterancy name and rank copied, while it has them (they're forgotten when it falls).
		void CopyVeteran(long id, ReportUnit& unit) {
			if (auto found = s_Veterans.find(id); found != s_Veterans.end()) {
				unit.Name = found->second.Name;
				unit.Rank = found->second.Rank;
			}
		}

		bool Counts(const Actor* actor) {
			return IsCombatant(actor) && !dynamic_cast<const ACraft*>(actor) && !actor->IsInGroup("Brains") && actor->GetTeam() >= 0 && actor->GetTeam() < c_Sides;
		}

		void Sample() {
			s_Report.Samples.push_back(s_ModeRun.Score);
		}

		/// Who is on each live objective: a team alone on one has it for the time since the last look.
		void LookAtObjectives(float seconds) {
			std::vector<Actor*> fighters;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (Counts(actor) && s_Report.Plays[actor->GetTeam()]) {
					fighters.push_back(actor);
				}
			}
			for (const BattleObjective& objective: BattleObjectives()) {
				if (!objective.Live || objective.Name.empty()) {
					continue;
				}
				std::array<int, c_Sides> on{};
				for (const Actor* fighter: fighters) {
					const bool there = objective.Zone.empty() ? g_SceneMan.ShortestDistance(objective.Pos, fighter->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(objective.Radius) : IsInZone(objective.Zone, fighter->GetPos());
					if (there) {
						++on[fighter->GetTeam()];
					}
				}
				int alone = -1;
				for (int side = 0; side < c_Sides; ++side) {
					if (on[side] > 0) {
						alone = alone == -1 ? side : -2;
					}
				}
				auto hold = std::find_if(s_Report.Holds.begin(), s_Report.Holds.end(), [&objective](const auto& entry) { return entry.first == objective.Name; });
				if (hold == s_Report.Holds.end()) {
					s_Report.Holds.emplace_back(objective.Name, std::array<float, c_Sides>{});
					hold = s_Report.Holds.end() - 1;
				}
				if (alone >= 0) {
					hold->second[alone] += seconds;
				}
			}
		}

		void EndReport(long long now) {
			s_Report.Recording = false;
			s_Report.EndAt = now;
			s_Report.Winner = s_ModeRun.Winner;
			s_Report.Result = s_ModeRun.Result;
			s_Report.Score = s_ModeRun.Score;
			Sample();
			for (auto& [id, unit]: s_Report.Units) {
				if (unit.Until < 0) {
					unit.Until = now;
				}
			}
			s_ReportOpen = true;
			s_ReportJustOpened = true;
		}

		std::string ResultLine() {
			if (!s_Report.Result.empty()) {
				return s_Report.Result;
			}
			return s_Report.Winner >= 0 ? std::string(c_SideNames[s_Report.Winner]) + " wins!" : std::string("A draw");
		}

		/// Your unit with the most kills (the longest in, of those level), or nullptr if none of yours made one.
		const ReportUnit* BestOfYours() {
			const ReportUnit* best = nullptr;
			for (const auto& [id, unit]: s_Report.Units) {
				if (unit.Team != s_Report.Yours || unit.Kills == 0) {
					continue;
				}
				if (!best || unit.Kills > best->Kills || (unit.Kills == best->Kills && unit.Until - unit.Since > best->Until - best->Since)) {
					best = &unit;
				}
			}
			return best;
		}

		/// The kinds in the battle, in the cards' order, then any others.
		std::vector<std::string> Kinds() {
			std::vector<std::string> kinds;
			for (const ReinforcementCard& card: c_ReinforcementCards) {
				kinds.emplace_back(card.Name);
			}
			kinds.emplace_back(c_OtherKind);
			std::erase_if(kinds, [](const std::string& kind) { return std::none_of(s_Report.Units.begin(), s_Report.Units.end(), [&kind](const auto& entry) { return entry.second.Kind == kind; }); });
			return kinds;
		}

		void DrawKillsTable() {
			std::vector<int> sides;
			for (int side = 0; side < c_Sides; ++side) {
				if (s_Report.Plays[side]) {
					sides.push_back(side);
				}
			}
			if (sides.empty() || s_Report.Units.empty()) {
				return;
			}
			ImGui::SeparatorText("Kills and losses");
			if (!ImGui::BeginTable("##kills", 1 + static_cast<int>(sides.size()) * 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				return;
			}
			ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 2.0F);
			for (int side: sides) {
				ImGui::TableSetupColumn((std::string(c_SideNames[side]) + " kills").c_str());
				ImGui::TableSetupColumn((std::string(c_SideNames[side]) + " lost").c_str());
			}
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("Kind");
			for (int side: sides) {
				const ImVec4 color = ImGui::ColorConvertU32ToFloat4(c_SideColors[side]);
				ImGui::TableNextColumn();
				ImGui::TextColored(color, "%s kills", c_SideNames[side]);
				ImGui::TableNextColumn();
				ImGui::TextColored(color, "lost");
			}
			std::array<int, c_Sides> allKills{};
			std::array<int, c_Sides> allLost{};
			for (const std::string& kind: Kinds()) {
				std::array<int, c_Sides> kills{};
				std::array<int, c_Sides> lost{};
				for (const auto& [id, unit]: s_Report.Units) {
					if (unit.Kind == kind) {
						kills[unit.Team] += unit.Kills;
						lost[unit.Team] += unit.Fell ? 1 : 0;
					}
				}
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(kind.c_str());
				for (int side: sides) {
					ImGui::TableNextColumn();
					ImGui::Text("%d", kills[side]);
					ImGui::TableNextColumn();
					ImGui::Text("%d", lost[side]);
					allKills[side] += kills[side];
					allLost[side] += lost[side];
				}
			}
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("All");
			for (int side: sides) {
				ImGui::TableNextColumn();
				ImGui::Text("%d", allKills[side]);
				ImGui::TableNextColumn();
				ImGui::Text("%d", allLost[side]);
			}
			ImGui::EndTable();
		}

		void DrawHoldsTable() {
			if (s_Report.Holds.empty()) {
				return;
			}
			ImGui::SeparatorText("Time on the objectives");
			std::vector<int> sides;
			for (int side = 0; side < c_Sides; ++side) {
				if (s_Report.Plays[side]) {
					sides.push_back(side);
				}
			}
			if (!ImGui::BeginTable("##holds", 1 + static_cast<int>(sides.size()), ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
				return;
			}
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted("Objective");
			for (int side: sides) {
				ImGui::TableNextColumn();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c_SideColors[side]), "%s", c_SideNames[side]);
			}
			for (const auto& [name, seconds]: s_Report.Holds) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(name.c_str());
				for (int side: sides) {
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(Clock(seconds[side]).c_str());
				}
			}
			ImGui::EndTable();
			ImGui::TextDisabled("Time a team had units on it and the others none.");
		}

		/// Each team's score over the battle, as lines.
		void DrawScoreChart() {
			if (s_Report.Samples.size() < 2) {
				return;
			}
			ImGui::SeparatorText("Score over time");
			int top = 1;
			for (const auto& sample: s_Report.Samples) {
				for (int side = 0; side < c_Sides; ++side) {
					if (s_Report.Plays[side]) {
						top = std::max(top, sample[side]);
					}
				}
			}
			const float height = 150.0F;
			const ImVec2 corner = ImGui::GetCursorScreenPos();
			const float labelRoom = ImGui::CalcTextSize(std::to_string(top).c_str()).x + 6.0F;
			const float width = ImGui::GetContentRegionAvail().x;
			ImGui::Dummy(ImVec2(width, height + ImGui::GetTextLineHeight() + 4.0F));
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 plotMin(corner.x + labelRoom, corner.y);
			const ImVec2 plotMax(corner.x + width, corner.y + height);
			const ImU32 axis = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			drawList->AddRectFilled(plotMin, plotMax, IM_COL32(0, 0, 0, 60), 2.0F);
			drawList->AddLine(ImVec2(plotMin.x, plotMax.y), plotMax, axis);
			drawList->AddLine(plotMin, ImVec2(plotMin.x, plotMax.y), axis);
			drawList->AddText(ImVec2(corner.x, plotMin.y), axis, std::to_string(top).c_str());
			drawList->AddText(ImVec2(corner.x + labelRoom - ImGui::CalcTextSize("0").x - 6.0F, plotMax.y - ImGui::GetTextLineHeight()), axis, "0");
			drawList->AddText(ImVec2(plotMin.x, plotMax.y + 2.0F), axis, "0:00");
			const std::string end = Clock(Seconds(s_Report.EndAt - s_Report.StartAt));
			drawList->AddText(ImVec2(plotMax.x - ImGui::CalcTextSize(end.c_str()).x, plotMax.y + 2.0F), axis, end.c_str());
			const float span = static_cast<float>(s_Report.Samples.size() - 1);
			for (int side = 0; side < c_Sides; ++side) {
				if (!s_Report.Plays[side]) {
					continue;
				}
				std::vector<ImVec2> points;
				points.reserve(s_Report.Samples.size());
				for (size_t i = 0; i < s_Report.Samples.size(); ++i) {
					const float x = plotMin.x + (plotMax.x - plotMin.x) * static_cast<float>(i) / span;
					const float y = plotMax.y - (plotMax.y - plotMin.y - 2.0F) * static_cast<float>(s_Report.Samples[i][side]) / static_cast<float>(top);
					points.emplace_back(x, y);
				}
				drawList->AddPolyline(points.data(), static_cast<int>(points.size()), c_SideColors[side], ImDrawFlags_None, 2.0F);
			}
		}
	} // namespace

	void ForgetBattleReport() {
		s_Report = Report();
		s_ReportOpen = false;
		s_ReportJustOpened = false;
		if (s_ModeRun.Running && !s_ModeRun.Over && Sandbox::IsBattleCommand()) {
			const long long now = g_TimerMan.GetSimUpdateCount();
			s_Report.Recording = true;
			s_Report.StartAt = now;
			s_Report.LastSample = now;
			s_Report.Mode = s_ModeRun.Settings.Mode;
			s_Report.Plays = s_ModeRun.Settings.Plays;
			s_Report.Yours = CommandedTeam();
			Sample();
		}
	}

	void NoteReportKill(long killerID, long victimID) {
		if (!s_Report.Recording) {
			return;
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		if (auto victim = s_Report.Units.find(victimID); victim != s_Report.Units.end() && !victim->second.Fell) {
			CopyVeteran(victimID, victim->second);
			victim->second.Fell = true;
			victim->second.Until = now;
		}
		if (auto killer = s_Report.Units.find(killerID); killerID != 0 && killer != s_Report.Units.end()) {
			++killer->second.Kills;
		}
	}

	void UpdateBattleReport() {
		if (!Sandbox::IsBattleCommand() || !s_ModeRun.Running) {
			s_Report.Recording = false;
			if (!Sandbox::IsBattleCommand()) {
				s_ReportOpen = false;
			}
			return;
		}
		if (!s_Report.Recording) {
			return;
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		if (s_ModeRun.Over) {
			EndReport(now);
			return;
		}
		s_Report.Yours = CommandedTeam();
		const bool lookAgain = now % c_HoldEvery == 0;
		for (const Actor* actor: SandboxAccess::Actors()) {
			if (!Counts(actor)) {
				continue;
			}
			const long id = static_cast<long>(actor->GetUniqueID());
			auto found = s_Report.Units.find(id);
			if (found == s_Report.Units.end()) {
				ReportUnit unit;
				unit.Team = actor->GetTeam();
				unit.Kind = KindOf(actor);
				unit.Since = now;
				found = s_Report.Units.emplace(id, unit).first;
			}
			if (lookAgain) {
				CopyVeteran(id, found->second);
			}
		}
		if (lookAgain) {
			LookAtObjectives(Seconds(c_HoldEvery));
		}
		if (Seconds(now - s_Report.LastSample) >= c_SampleSeconds) {
			s_Report.LastSample = now;
			Sample();
		}
	}

	void TagCardUnit(Actor* unit, int card) {
		unit->SetStringValue(c_KindKey, c_ReinforcementCards[std::clamp(card, 0, c_ReinforcementCardCount - 1)].Name);
	}

	bool BattleReportReady() {
		return Sandbox::IsBattleCommand() && s_ModeRun.Over && s_Report.EndAt > s_Report.StartAt;
	}

	void OpenBattleReport() {
		s_ReportOpen = BattleReportReady();
	}

	void DrawBattleReport() {
		if (s_ReportJustOpened) {
			s_ReportJustOpened = false;
			if (!g_DebugMan.IsUserPaused()) {
				g_DebugMan.ToggleUserPause();
				s_PausedByReport = true;
			}
		}
		if (!s_ReportOpen) {
			if (s_PausedByReport) {
				s_PausedByReport = false;
				if (g_DebugMan.IsUserPaused()) {
					g_DebugMan.ToggleUserPause();
				}
			}
			return;
		}
		GameViewRect view = g_WindowMan.GetGameViewRect();
		ImGui::SetNextWindowSize(ImVec2(620.0F, 0.0F), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(ImVec2(view.x + view.w * 0.5F, view.y + view.h * 0.45F), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
		if (ImGui::Begin("After-battle report###BattleReport", &s_ReportOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
			const ImU32 resultColor = s_Report.Winner >= 0 ? c_SideColors[s_Report.Winner] : IM_COL32(255, 255, 255, 255);
			ImGui::SetWindowFontScale(1.4F);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(resultColor), "%s", ResultLine().c_str());
			ImGui::SetWindowFontScale(1.0F);
			const int mode = static_cast<int>(s_Report.Mode);
			ImGui::TextDisabled("%s, lasted %s", c_ModeNames[std::clamp(mode, 0, static_cast<int>(std::size(c_ModeNames)) - 1)], Clock(Seconds(s_Report.EndAt - s_Report.StartAt)).c_str());
			// Final scores.
			bool first = true;
			for (int side = 0; side < c_Sides; ++side) {
				if (!s_Report.Plays[side]) {
					continue;
				}
				if (!first) {
					ImGui::SameLine(0.0F, 24.0F);
				}
				first = false;
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c_SideColors[side]), "%s %d%s", c_SideNames[side], s_Report.Score[side], side == s_Report.Yours ? " (you)" : "");
			}

			ImGui::SeparatorText("Best of yours");
			if (const ReportUnit* best = BestOfYours()) {
				static constexpr const char* c_Ranks[] = {"Private", "Corporal", "Sergeant", "Veteran"};
				const std::string name = best->Name.empty() ? std::string("A ") + best->Kind : std::string(c_Ranks[std::clamp(best->Rank, 0, 3)]) + " " + best->Name;
				ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.35F, 1.0F), "%s", name.c_str());
				ImGui::SameLine();
				ImGui::Text("(%s): %d %s, %s %s", best->Kind.c_str(), best->Kills, best->Kills == 1 ? "kill" : "kills", best->Fell ? "fell after" : "in for", Clock(Seconds(best->Until - best->Since)).c_str());
			} else {
				ImGui::TextDisabled("None of your units made a kill.");
			}

			DrawKillsTable();
			DrawHoldsTable();
			DrawScoreChart();

			ImGui::Spacing();
			const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
			if (ToolUI::Button("Play again", ImVec2(half, 0.0F))) {
				s_ReportOpen = false;
				for (int side = 0; side < c_Sides; ++side) {
					SendBattleSettings(side);
				}
				SendBattleMode(BattleModeStart);
			}
			ImGui::SetItemTooltip("The same battle again from the start, set up as it was.");
			ImGui::SameLine();
			if (ToolUI::Button("Close", ImVec2(-1.0F, 0.0F))) {
				s_ReportOpen = false;
			}
			ImGui::SetItemTooltip("Back to the battlefield as it ended (the world goes on). The report can be opened again from the battle panel.");
		}
		ImGui::End();
	}
} // namespace SandboxDetail
