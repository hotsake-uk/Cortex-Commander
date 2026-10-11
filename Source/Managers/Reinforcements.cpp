// Battle Command: reinforcements for the team you command. Points come from its kills, the objectives it takes and the gold it digs, and buy
// units (by their factions' Loadout presets) that come in by dropship or rocket over a landing spot you mark. The ships aren't protected: enemy
// fire can bring one down on the way in.

#include "SandboxInternal.h"
#include "Loadout.h"

#include <unordered_map>

namespace SandboxDetail {
	namespace {
		constexpr float c_StartPoints = 600.0F; //!< Each team's points at the start of a mode's game.
		constexpr float c_KillShare = 0.5F; //!< A kill earns this share of what the unit killed was worth.
		constexpr float c_ObjectivePoints = 300.0F; //!< A capture, a goal, an objective or hill taken, a VIP brought down.
		constexpr float c_HillSecondPoints = 3.0F; //!< King of the hill on seconds held: each second.
		constexpr float c_KillReach = 500.0F; //!< How near one of a team's units has to be to a unit that fell for the kill to be its.
		constexpr float c_MostGoldAtOnce = 100000.0F; //!< More gold than this at once isn't dug: it's the sandbox topping the funds up.

		/// A fighting unit seen in the last count: whose, where, and what it was worth.
		struct Seen {
			int Team = -1;
			Vector Pos;
			float Value = 0.0F;
		};
		std::unordered_map<long, Seen> s_Seen;
		std::array<int, c_Sides> s_LastScore{};
		std::array<float, c_Sides> s_LastFunds = {-1.0F, -1.0F, -1.0F, -1.0F};
		std::unordered_map<long long, float> s_CostCache; //!< By module and card.

		void Note(const std::string& note) {
			s_ModeRun.Note = note;
			s_ModeRun.NoteAt = g_TimerMan.GetSimUpdateCount();
			g_ConsoleMan.PrintString("BATTLE: " + note);
		}

		/// The module a side's reinforcements come from: its first faction, else Coalition (else any module that has the card).
		int ReinforcementModule(const std::vector<int>& factions) {
			return !factions.empty() ? factions.front() : g_PresetMan.GetModuleID("Coalition.rte");
		}

		/// A card's unit, with what its Loadout gives it, or nothing if no module has the card. Ownership is the caller's.
		Actor* MakeCardUnit(int card, int moduleID) {
			const Loadout* loadout = dynamic_cast<const Loadout*>(g_PresetMan.GetEntityPreset("Loadout", c_ReinforcementCards[std::clamp(card, 0, c_ReinforcementCardCount - 1)].Loadout, moduleID));
			if (!loadout) {
				return nullptr;
			}
			float tally = 0.0F;
			return loadout->CreateFirstActor(std::max(moduleID, 0), 1.0F, 1.0F, tally);
		}

		/// Points for a team's score going up by some, by what the mode's score counts.
		float ObjectivePoints(int scored) {
			switch (s_ModeRun.Settings.Mode) {
				case BattleMode::LastTeamStanding:
					// (Its score is the units it has left to send, which only goes down.)
					return 0.0F;
				case BattleMode::KingOfTheHill:
					return static_cast<float>(scored) * (s_ModeRun.Settings.HillsToWin > 0 ? c_ObjectivePoints : c_HillSecondPoints);
				default:
					return static_cast<float>(scored) * c_ObjectivePoints;
			}
		}

		bool AnyCommanded() {
			for (int side = 0; side < c_Sides; ++side) {
				if (BattlePlayerCommands(side)) {
					return true;
				}
			}
			return false;
		}
	} // namespace

	void ForgetReinforcements() {
		for (CommandPoints& points: s_CommandPoints) {
			points = CommandPoints();
			points.Points = c_StartPoints;
		}
		s_Seen.clear();
		s_LastScore.fill(0);
		s_LastFunds.fill(-1.0F);
		s_PendingCard = -1;
		s_HasLandingSpot = false;
		ForgetPowers();
	}

	void UpdateReinforcements() {
		if (!s_ModeRun.Running || !AnyCommanded()) {
			s_Seen.clear();
			s_LastFunds.fill(-1.0F);
			return;
		}
		if (g_TimerMan.GetSimUpdateCount() % 10 != 0) {
			return;
		}
		GameActivity* game = CurrentGame();
		for (int side = 0; side < c_Sides; ++side) {
			const int score = s_ModeRun.Score[side];
			const int scored = score - s_LastScore[side];
			s_LastScore[side] = score;
			if (!BattlePlayerCommands(side)) {
				s_LastFunds[side] = -1.0F;
				continue;
			}
			CommandPoints& points = s_CommandPoints[side];
			if (scored > 0) {
				float earned = ObjectivePoints(scored);
				points.Points += earned;
				points.FromObjectives += earned;
			}
			// Gold dug goes into the team's funds.
			if (game) {
				float funds = game->GetTeamFunds(side);
				if (s_LastFunds[side] >= 0.0F && funds > s_LastFunds[side] && funds - s_LastFunds[side] < c_MostGoldAtOnce) {
					float gold = funds - s_LastFunds[side];
					points.Points += gold;
					points.FromGold += gold;
				}
				s_LastFunds[side] = funds;
			}
		}

		// Kills: a fighting unit seen last time and gone (or fallen) now is the kill of the team with a unit nearest where it was, if near enough.
		std::unordered_map<long, const Actor*> live;
		std::vector<const Actor*> fighters;
		for (const Actor* actor: SandboxAccess::Actors()) {
			if (IsCombatant(actor) && !dynamic_cast<const ACraft*>(actor) && !actor->IsInGroup("Brains")) {
				live[static_cast<long>(actor->GetUniqueID())] = actor;
				fighters.push_back(actor);
			}
		}
		for (const auto& [id, seen]: s_Seen) {
			if (live.count(id)) {
				continue;
			}
			int killer = -1;
			float nearest = c_KillReach * c_KillReach;
			for (const Actor* fighter: fighters) {
				if (fighter->GetTeam() == seen.Team) {
					continue;
				}
				float distance = g_SceneMan.ShortestDistance(fighter->GetPos(), seen.Pos, g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
				if (distance < nearest) {
					killer = fighter->GetTeam();
					nearest = distance;
				}
			}
			if (killer >= 0 && BattlePlayerCommands(killer)) {
				CommandPoints& points = s_CommandPoints[killer];
				float earned = seen.Value * c_KillShare;
				points.Points += earned;
				points.FromKills += earned;
				++points.Kills;
			}
		}
		std::unordered_map<long, Seen> now;
		for (const auto& [id, actor]: live) {
			Seen& seen = now[id];
			auto before = s_Seen.find(id);
			seen.Team = actor->GetTeam();
			seen.Pos = actor->GetPos();
			seen.Value = before != s_Seen.end() ? before->second.Value : actor->GetTotalValue(actor->GetModuleID(), 1.0F);
		}
		s_Seen.swap(now);
	}

	void ApplyReinforcement(const Stroke& stroke) {
		const int side = stroke.Team;
		if (side < 0 || side >= c_Sides || !BattlePlayerCommands(side)) {
			return;
		}
		const int card = std::clamp(stroke.Choice, 0, c_ReinforcementCardCount - 1);
		const int count = std::clamp(stroke.Radius, 1, 5);
		const int moduleID = ReinforcementModule(s_BattleTeams[side].Settings.Factions);
		std::vector<Actor*> units;
		float cost = 0.0F;
		for (int i = 0; i < count; ++i) {
			Actor* unit = MakeCardUnit(card, moduleID);
			if (!unit) {
				break;
			}
			cost += unit->GetTotalValue(unit->GetModuleID(), 1.0F);
			unit->SetTeam(side);
			unit->SetControllerMode(Controller::CIM_AI);
			units.push_back(unit);
		}
		CommandPoints& points = s_CommandPoints[side];
		if (units.empty() || cost > points.Points) {
			for (Actor* unit: units) {
				delete unit;
			}
			Note(units.empty() ? std::string("No ") + c_ReinforcementCards[card].Name + " to be had for " + c_SideNames[side] : std::string("Not enough points for that"));
			return;
		}
		// Yours to command once out: no job from the mode, holding where they land.
		ModeUnitsMade(side, units);
		Vector at = stroke.Position;
		g_SceneMan.WrapPosition(at);
		const float paid = DropUnits(units, side, at.m_X, stroke.Craft, false);
		if (paid > 0.0F) {
			points.Points -= paid;
			points.Spent += paid;
			points.Called += count;
			Note(std::string(c_SideNames[side]) + ": " + (count > 1 ? std::to_string(count) + " " + c_ReinforcementCards[card].Name + "s" : std::string("a ") + c_ReinforcementCards[card].Name) + " on the way by " + c_Crafts[std::clamp(stroke.Craft, 0, static_cast<int>(std::size(c_Crafts)) - 1)].Label);
		}
	}

	float ReinforcementCost(int card, int side) {
		if (side < 0 || side >= c_Sides) {
			return 0.0F;
		}
		const int moduleID = ReinforcementModule(s_BattleSetup[side].Factions);
		const long long key = static_cast<long long>(moduleID) * 64 + card;
		if (auto known = s_CostCache.find(key); known != s_CostCache.end()) {
			return known->second;
		}
		float cost = 0.0F;
		if (Actor* unit = MakeCardUnit(card, moduleID)) {
			cost = unit->GetTotalValue(unit->GetModuleID(), 1.0F);
			delete unit;
		}
		s_CostCache[key] = cost;
		return cost;
	}

	namespace {
		/// Calls a card in over the landing spot, from the window: applied in the next sim update.
		void CallIn(int card) {
			Stroke stroke;
			stroke.Kind = Tool::BattleTeam;
			stroke.Team = CommandedTeam();
			stroke.Count = BattleReinforce;
			stroke.Choice = card;
			stroke.Radius = s_CallSize;
			stroke.Craft = s_CallCraft;
			stroke.Position = s_LandingSpot;
			s_Queue.push_back(stroke);
		}

		/// Takes up the landing spot tool, keeping the tool in hand to go back to once it's marked.
		void TakeLandingTool() {
			s_ToolIndex = ToolIndex(Tool::LandingSpot);
		}
	} // namespace

	void LandingSpotClicked(const Vector& position) {
		s_LandingSpot = position;
		g_SceneMan.WrapPosition(s_LandingSpot);
		s_HasLandingSpot = true;
		if (s_PendingCard >= 0) {
			CallIn(s_PendingCard);
			s_PendingCard = -1;
		}
		s_ToolIndex = ToolIndex(Tool::Command);
		s_CommandMode = CommandMode::Select;
	}

	void ReinforcementTiles() {
		const int side = std::clamp(CommandedTeam(), 0, c_Sides - 1);
		const bool running = BattlePlayerCommands(side);
		const CommandPoints& points = s_CommandPoints[side];
		float pixel = ToolUI::Pixel();
		if (CurrentTool().Kind != Tool::LandingSpot) {
			s_PendingCard = -1;
		}
		// The points.
		{
			char shown[16];
			std::snprintf(shown, sizeof(shown), "%.0f", running ? points.Points : 0.0F);
			char tip[512];
			std::snprintf(tip, sizeof(tip),
			              "Points for reinforcements: %.0f\nEarned from kills %.0f (%d kills), objectives %.0f, gold dug %.0f. Spent %.0f on %d units.\n\nEach team you command starts a battle with %.0f. A kill earns half what the unit killed was worth;\na capture, goal, objective or hill taken, or VIP brought down, %.0f (king of the hill on time held: %.0f a second);\ngold your diggers bring in, its worth.",
			              points.Points, points.FromKills, points.Kills, points.FromObjectives, points.FromGold, points.Spent, points.Called, c_StartPoints, c_ObjectivePoints, c_HillSecondPoints);
			BarTile("##points", "Points", running ? tip : "Points for reinforcements: earned once the battle is on.", false, [&](ImDrawList* tileList, ImVec2 at, float room) { PictureText(tileList, at, room, running ? IM_COL32(255, 220, 120, 255) : IM_COL32(200, 200, 200, 120), shown); });
		}
		// The cards.
		static const Icon icons[] = {Icon::Gun, Icon::Target, Icon::Bomb, Icon::Pick, Icon::Chunk};
		static_assert(std::size(icons) == c_ReinforcementCardCount, "An icon for each card.");
		for (int card = 0; card < c_ReinforcementCardCount; ++card) {
			const float cost = ReinforcementCost(card, side) * static_cast<float>(s_CallSize);
			const bool exists = cost > 0.0F;
			const bool affordable = running && exists && cost <= points.Points;
			char costText[16];
			std::snprintf(costText, sizeof(costText), "%.0f", cost);
			std::string tip = std::string(c_ReinforcementCards[card].Name) + (s_CallSize > 1 ? " x" + std::to_string(s_CallSize) : std::string()) + ": " + (exists ? std::string(costText) + " points" : std::string("not in this faction")) + "\n" + c_ReinforcementCards[card].Tip +
			                  "\n\nClick: call in over the landing spot (marked first, if there isn't one). Right click: mark a new spot for this one." + (running ? "" : "\nOnce the battle is on.");
			ImGui::SameLine();
			ImGui::PushID(card + 800);
			const bool waiting = s_PendingCard == card;
			int clicked = BarTile("##card", c_ReinforcementCards[card].Name, tip.c_str(), waiting, [&](ImDrawList* tileList, ImVec2 at, float room) {
				DrawIcon(tileList, icons[card], at, room / 12.0F, affordable ? IM_COL32(232, 224, 190, 255) : IM_COL32(150, 150, 150, 110));
				tileList->AddText(ImVec2(at.x + pixel, at.y + room - ImGui::GetFontSize()), affordable ? IM_COL32(255, 220, 120, 255) : IM_COL32(200, 120, 100, 200), costText);
			},
			                      affordable ? 0 : IM_COL32(160, 160, 160, 160));
			if (clicked != 0 && affordable) {
				if (clicked == 1 && s_HasLandingSpot) {
					CallIn(card);
				} else {
					s_PendingCard = card;
					TakeLandingTool();
				}
			}
			ImGui::PopID();
		}
		ImGui::SameLine();
		// Where they land, by what, and how many at a time.
		{
			const bool marking = CurrentTool().Kind == Tool::LandingSpot;
			if (BarTile("##landing", "Land at", s_HasLandingSpot ? "The landing spot: click, then click on the map to move it." : "Mark the landing spot: click, then click on the map where your reinforcements are to come down.", marking, [&](ImDrawList* tileList, ImVec2 at, float room) { DrawIcon(tileList, Icon::Down, at, room / 12.0F, c_SideColors[side]); }) == 1) {
				s_PendingCard = -1;
				TakeLandingTool();
			}
		}
		ImGui::SameLine();
		{
			const char* craft = c_Crafts[std::clamp(s_CallCraft, 0, static_cast<int>(std::size(c_Crafts)) - 1)].Label;
			if (BarTile("##craft", craft, "What they come in by. Click: dropship or rocket. A ship can be shot down on the way in, with them aboard.", false, [&](ImDrawList* tileList, ImVec2 at, float room) { DrawIcon(tileList, Icon::Rocket, at, room / 12.0F, IM_COL32(190, 190, 190, 255)); }) == 1) {
				s_CallCraft = (s_CallCraft + 1) % static_cast<int>(std::size(c_Crafts));
			}
		}
		ImGui::SameLine();
		{
			std::string shown = "x" + std::to_string(s_CallSize);
			int clicked = BarTile("##size", "At once", "How many each card calls in, in one ship. Click: 1, 3 or 5. Right click: back to 1.", false, [&](ImDrawList* tileList, ImVec2 at, float room) { PictureText(tileList, at, room, ToolTheme::Text, shown.c_str()); });
			if (clicked == 1) {
				s_CallSize = s_CallSize >= 5 ? 1 : s_CallSize + 2;
			} else if (clicked == 2) {
				s_CallSize = 1;
			}
		}
	}

	void DrawLandingSpot() {
		if (!s_HasLandingSpot && CurrentTool().Kind != Tool::LandingSpot) {
			return;
		}
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		const ImU32 color = c_SideColors[std::clamp(CommandedTeam(), 0, c_Sides - 1)];
		auto mark = [&](const ImVec2& base, ImU32 ink) {
			drawList->AddCircle(base, 14.0F, ink, 24, 2.0F);
			drawList->AddLine(ImVec2(base.x - 20.0F, base.y), ImVec2(base.x - 8.0F, base.y), ink, 2.0F);
			drawList->AddLine(ImVec2(base.x + 8.0F, base.y), ImVec2(base.x + 20.0F, base.y), ink, 2.0F);
			drawList->AddTriangleFilled(ImVec2(base.x - 6.0F, base.y - 30.0F), ImVec2(base.x + 6.0F, base.y - 30.0F), ImVec2(base.x, base.y - 20.0F), ink);
		};
		if (s_HasLandingSpot) {
			Vector onScreen = FromCamera(s_LandingSpot);
			ImVec2 base(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
			mark(base, color);
			drawList->AddText(ImVec2(base.x + 16.0F, base.y - 28.0F), color, "Landing");
		}
		if (CurrentTool().Kind == Tool::LandingSpot && !ImGui::GetIO().WantCaptureMouse) {
			mark(ImGui::GetIO().MousePos, (color & 0x00FFFFFF) | (150u << IM_COL32_A_SHIFT));
		}
	}
} // namespace SandboxDetail
