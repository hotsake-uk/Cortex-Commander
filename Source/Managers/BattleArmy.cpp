// Battle Command: pick your army (a choice on the battle panel). Before the game you pick how many of each reinforcement card your team brings,
// within a points budget. Your team's units then come from that roster, the first ones and every one that comes in after a unit falls, until
// it's used up: then no more come (reinforcements bought with points still do).

#include "SandboxInternal.h"

namespace SandboxDetail {
	static_assert(std::tuple_size_v<decltype(BattleModeSettings::Army)> == c_ReinforcementCardCount, "BattleModeSettings::Army is by c_ReinforcementCards");

	namespace {
		std::array<std::array<int, c_ReinforcementCardCount>, c_Sides> s_Left{}; //!< Each side's units of each card still to come.
		std::array<bool, c_Sides> s_Armed{}; //!< Whether the side's roster was taken up for this game.
		std::array<bool, c_Sides> s_Spent{}; //!< Whether the side was told its roster is used up.

		/// The team the player commands in a mode's settings, -1 if none.
		int CommandedIn(const BattleModeSettings& mode) {
			for (int side = 0; side < c_Sides; ++side) {
				if (mode.PlayerCommands[side] && mode.Plays[side]) {
					return side;
				}
			}
			return -1;
		}

		float RosterCost(const std::array<int, c_ReinforcementCardCount>& roster, int side) {
			float total = 0.0F;
			for (int card = 0; card < c_ReinforcementCardCount; ++card) {
				total += static_cast<float>(std::max(roster[card], 0)) * ReinforcementCost(card, side);
			}
			return total;
		}

		/// The side's roster taken up at the start of the game: the one picked, held to the budget (the dearest units left out first).
		void Arm(int side) {
			std::array<int, c_ReinforcementCardCount>& left = s_Left[side];
			for (int card = 0; card < c_ReinforcementCardCount; ++card) {
				left[card] = std::clamp(s_ModeRun.Settings.Army[card], 0, 99);
			}
			const float budget = static_cast<float>(std::max(s_ModeRun.Settings.ArmyPoints, 0));
			while (RosterCost(left, side) > budget) {
				int dearest = -1;
				for (int card = 0; card < c_ReinforcementCardCount; ++card) {
					if (left[card] > 0 && (dearest < 0 || ReinforcementCost(card, side) > ReinforcementCost(dearest, side))) {
						dearest = card;
					}
				}
				if (dearest < 0) {
					break;
				}
				--left[dearest];
			}
			s_Armed[side] = true;
			s_Spent[side] = false;
		}

		int Remaining(int side) {
			int total = 0;
			for (int count: s_Left[side]) {
				total += count;
			}
			return total;
		}

		/// The roster used up: an alert, and one of the side's units says so.
		void TellSpent(int side) {
			s_Spent[side] = true;
			Vector at;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (actor->GetTeam() == side && IsSoldier(actor) && !actor->IsPlayerControlled()) {
					at = actor->GetPos();
					if (actor->Say("ArmySpent")) {
						break;
					}
				}
			}
			if (side == CommandedTeam()) {
				AddAlert("army", "Your army is all in: no more units will come", at, IM_COL32(220, 180, 90, 255));
			}
		}
	} // namespace

	void ForgetArmy() {
		s_Left = {};
		s_Armed.fill(false);
		s_Spent.fill(false);
	}

	bool ArmyPicked(int side) { return Sandbox::IsBattleCommand() && s_ModeRun.Running && s_ModeRun.Settings.PickArmy && BattlePlayerCommands(side); }

	std::vector<Actor*> ArmyWave(int side, int size) {
		std::vector<Actor*> wave;
		if (!ArmyPicked(side)) {
			return wave;
		}
		if (!s_Armed[side]) {
			Arm(side);
		}
		std::array<int, c_ReinforcementCardCount>& left = s_Left[side];
		while (static_cast<int>(wave.size()) < size) {
			const int remaining = Remaining(side);
			if (remaining <= 0) {
				break;
			}
			// A card picked by how many of it are left, so the roster's mix comes in mixed.
			int roll = std::min(static_cast<int>(Random01() * static_cast<float>(remaining)), remaining - 1);
			int card = 0;
			while (card < c_ReinforcementCardCount - 1 && roll >= left[card]) {
				roll -= left[card];
				++card;
			}
			Actor* unit = MakeArmyUnit(card, side);
			if (!unit) {
				// The faction has no such unit: none of them come.
				left[card] = 0;
				continue;
			}
			unit->SetAIMode(Actor::AIMODE_SENTRY);
			--left[card];
			wave.push_back(unit);
		}
		if (Remaining(side) <= 0 && !s_Spent[side]) {
			TellSpent(side);
		}
		return wave;
	}

	void ArmyPanel() {
		if (ToolUI::Checkbox("Pick your army", &s_ModeSetup.PickArmy)) {
			SendBattleMode();
		}
		ImGui::SetItemTooltip("Pick how many of each unit your team brings, within a points budget. Your units come from that roster: the first ones,\nand each that comes in after one of yours falls. When it's used up no more come, though reinforcements bought with points still do.");
		if (!s_ModeSetup.PickArmy) {
			return;
		}
		ImGui::Indent();
		if (s_ModeRun.Running && s_ModeRun.Settings.PickArmy) {
			// During the game: what's left of it.
			const int side = CommandedIn(s_ModeRun.Settings);
			if (side >= 0) {
				for (int card = 0; card < c_ReinforcementCardCount; ++card) {
					const int picked = std::max(s_ModeRun.Settings.Army[card], 0);
					if (picked > 0 || s_Left[side][card] > 0) {
						ImGui::Text("%s: %d of %d to come", c_ReinforcementCards[card].Name, s_Armed[side] ? s_Left[side][card] : picked, picked);
					}
				}
			}
			ImGui::Unindent();
			return;
		}
		const int side = CommandedIn(s_ModeSetup);
		if (side < 0) {
			ImGui::TextDisabled("Tick \"You command this team\" on your team to price its units.");
		}
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0F);
		if (ImGui::SliderInt("Army points", &s_ModeSetup.ArmyPoints, 500, 20000, "%d")) {
			SendBattleMode();
		}
		ImGui::SetItemTooltip("What your whole army may cost.");
		const float points = static_cast<float>(s_ModeSetup.ArmyPoints);
		const float total = side >= 0 ? RosterCost(s_ModeSetup.Army, side) : 0.0F;
		for (int card = 0; card < c_ReinforcementCardCount; ++card) {
			ImGui::PushID(card);
			const float cost = side >= 0 ? ReinforcementCost(card, side) : 0.0F;
			int& count = s_ModeSetup.Army[card];
			ImGui::BeginDisabled(count <= 0);
			if (ToolUI::SmallButton("-")) {
				--count;
				SendBattleMode();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(side < 0 || cost <= 0.0F || total + cost > points || count >= 99);
			if (ToolUI::SmallButton("+")) {
				++count;
				SendBattleMode();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::Text("%2d  %s", count, c_ReinforcementCards[card].Name);
			ImGui::SameLine();
			if (side >= 0 && cost <= 0.0F) {
				ImGui::TextDisabled("(not in this faction)");
			} else if (side >= 0) {
				ImGui::TextDisabled("(%d each)", static_cast<int>(cost));
			}
			ImGui::SetItemTooltip("%s", c_ReinforcementCards[card].Tip);
			ImGui::PopID();
		}
		if (side >= 0) {
			int units = 0;
			for (int count: s_ModeSetup.Army) {
				units += std::max(count, 0);
			}
			if (total > points) {
				ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.35F, 1.0F), "%d units, %d of %d points: over by %d (the dearest are left out)", units, static_cast<int>(total), static_cast<int>(points), static_cast<int>(total - points));
			} else {
				ImGui::Text("%d units, %d of %d points", units, static_cast<int>(total), static_cast<int>(points));
			}
		}
		ImGui::Unindent();
	}
} // namespace SandboxDetail
