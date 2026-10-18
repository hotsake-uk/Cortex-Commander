// Battle Command: your brain on the field (a choice on the battle panel). Your brain is put down at your team's base when the game starts; if it
// dies you lose, and your orders only reach your units within its command range. Those further out carry on with what they were last told.

#include "SandboxInternal.h"
#include "DetMath.h"

namespace SandboxDetail {
	namespace {
		constexpr const char* c_BrainPreset = "Brain Robot";

		UnitRef s_Brain; //!< The brain on the field, once put down.
		int s_BrainSide = -1; //!< The team it commands; -1 before it's put down.
		float s_BrainHealth = 0.0F; //!< At the last look, for the alert when it's hit.

		bool On() { return Sandbox::IsBattleCommand() && s_ModeRun.Running && s_ModeRun.Settings.FieldBrain; }

		Actor* Brain() {
			Actor* brain = GetRef(s_Brain);
			return brain && IsCombatant(brain) ? brain : nullptr;
		}

		float Range() { return static_cast<float>(std::clamp(s_ModeRun.Settings.FieldBrainRange, 100, 10000)); }

		float Distance(const Vector& from, const Vector& to) { return g_SceneMan.ShortestDistance(from, to, g_SceneMan.SceneWrapsX()).GetMagnitude(); }

		/// The side that wins when a side's brain falls: the only other team in the game, else nobody.
		int WinnerAgainst(int side) {
			int winner = -1;
			int others = 0;
			for (int other = 0; other < c_Sides; ++other) {
				if (other != side && s_ModeRun.Settings.Plays[other]) {
					winner = other;
					++others;
				}
			}
			return others == 1 ? winner : -1;
		}
	} // namespace

	void ForgetFieldBrain() {
		if (Actor* brain = GetRef(s_Brain)) {
			brain->SetToDelete(true);
		}
		s_Brain = UnitRef();
		s_BrainSide = -1;
		s_BrainHealth = 0.0F;
	}

	void SaveFieldBrain(std::ostream& out) {
		out << ' ' << s_Brain.ID << ' ' << s_BrainSide << ' ' << s_BrainHealth;
	}

	void LoadFieldBrain(std::istream& in, const LoadedIDs& ids) {
		long brain = 0;
		in >> brain >> s_BrainSide >> s_BrainHealth;
		// (Put down already: not put down again. One not in the loaded game has fallen, and the game is lost, as when it dies.)
		s_Brain = ids.Ref(brain);
	}

	void UpdateFieldBrain() {
		if (!On() || s_ModeRun.Over || g_TimerMan.GetSimUpdateCount() % 15 != 3) {
			return;
		}
		const int side = CommandedTeam();
		if (s_BrainSide < 0) {
			// Put down at the base, on the ground, once there's a team to command.
			Vector at;
			if (side < 0 || !BattleTeamBase(side, at)) {
				return;
			}
			at = g_SceneMan.MovePointToGround(at, 20.0F, 4);
			if (Actor* brain = Sandbox::SpawnBrain(c_BrainPreset, side, at)) {
				s_Brain = MakeRef(brain);
				s_BrainSide = side;
				s_BrainHealth = brain->GetHealth();
				g_ConsoleMan.PrintString("BATTLE: your brain is on the field at your base. If it dies you lose; orders reach " + std::to_string(static_cast<int>(Range())) + " px from it.");
			}
			return;
		}
		Actor* brain = Brain();
		if (!brain) {
			const int winner = WinnerAgainst(s_BrainSide);
			// Your units near where it fell say so.
			for (Actor* actor: SandboxAccess::Actors()) {
				if (actor->GetTeam() == s_BrainSide && IsSoldier(actor) && !actor->IsPlayerControlled() && actor->Say("BrainLost")) {
					break;
				}
			}
			EndBattleGame(winner, std::string(c_SideNames[std::clamp(s_BrainSide, 0, c_Sides - 1)]) + "'s brain has fallen" + (winner >= 0 ? ": " + std::string(c_SideNames[winner]) + " wins" : ": they lose"));
			return;
		}
		if (brain->GetHealth() < s_BrainHealth - 1.0F) {
			AddAlert("brain", "Your brain is under attack!", brain->GetPos(), IM_COL32(255, 80, 200, 255));
		}
		s_BrainHealth = brain->GetHealth();
	}

	bool InCommandRange(const Actor* unit) {
		if (!On() || !unit || unit->GetTeam() != s_BrainSide) {
			return true;
		}
		const Actor* brain = Brain();
		return !brain || unit == brain || Distance(unit->GetPos(), brain->GetPos()) <= Range();
	}

	std::vector<UnitRef> TakeOutOfRange() {
		std::vector<UnitRef> out;
		if (!On() || !Brain()) {
			return out;
		}
		std::erase_if(s_Selected, [&out](const UnitRef& ref) {
			const Actor* unit = GetRef(ref);
			if (unit && !InCommandRange(unit)) {
				out.push_back(ref);
				return true;
			}
			return false;
		});
		if (!out.empty()) {
			g_ConsoleMan.PrintString("BATTLE: " + std::to_string(out.size()) + (out.size() == 1 ? " unit is" : " units are") + " out of your brain's command range: not ordered.");
		}
		return out;
	}

	void PutBackOutOfRange(const std::vector<UnitRef>& out, const std::vector<UnitRef>& before) {
		// (Only into the selection the order left as it found it: a click that picked other units keeps its own.)
		if (out.empty() || s_Selected.size() != before.size() || !std::equal(s_Selected.begin(), s_Selected.end(), before.begin(), [](const UnitRef& a, const UnitRef& b) { return GetRef(a) == GetRef(b); })) {
			return;
		}
		s_Selected.insert(s_Selected.end(), out.begin(), out.end());
	}

	void DrawFieldBrain(ImDrawList* drawList) {
		const Actor* brain = On() ? Brain() : nullptr;
		if (!brain || g_DebugMan.IsPhotoModeHidingHUD() || (OnlySide() >= 0 && OnlySide() != s_BrainSide)) {
			return;
		}
		const float scale = ScenePixelsPerWindowPixel();
		const ImU32 pink = IM_COL32(255, 120, 210, 255);
		const ImVec2 at = ToScreen(brain->GetPos());
		// The command range: a faint dashed ring.
		const float radius = Range() / scale;
		constexpr int c_Dashes = 72;
		for (int i = 0; i < c_Dashes; i += 2) {
			const float a = 6.2832F * static_cast<float>(i) / c_Dashes;
			const float b = 6.2832F * static_cast<float>(i + 1) / c_Dashes;
			drawList->AddLine(ImVec2(at.x + DetMath::Cos(a) * radius, at.y + DetMath::Sin(a) * radius), ImVec2(at.x + DetMath::Cos(b) * radius, at.y + DetMath::Sin(b) * radius), IM_COL32(255, 120, 210, 70), 1.5F);
		}
		// Over its head: a diamond, and "Brain".
		const Vector head = FromCamera(OverHeadPos(brain) - Vector(0.0F, 18.0F));
		const ImVec2 mark(ViewOrigin().x + head.m_X / scale, ViewOrigin().y + head.m_Y / scale);
		drawList->AddQuadFilled(ImVec2(mark.x, mark.y - 6.0F), ImVec2(mark.x + 6.0F, mark.y), ImVec2(mark.x, mark.y + 6.0F), ImVec2(mark.x - 6.0F, mark.y), pink);
		drawList->AddText(ImVec2(mark.x + 9.0F, mark.y - ImGui::GetTextLineHeight() * 0.5F), pink, "Brain");
		// Selected units out of range: a grey ring with a slash, no signal.
		for (const UnitRef& ref: s_Selected) {
			const Actor* unit = GetRef(ref);
			if (!unit || InCommandRange(unit)) {
				continue;
			}
			const ImVec2 where = ToScreen(unit->GetPos());
			const float r = std::max(unit->GetRadius() / scale, 8.0F) + 4.0F;
			drawList->AddCircle(where, r, IM_COL32(160, 160, 160, 200), 0, 1.5F);
			drawList->AddLine(ImVec2(where.x - r * 0.7F, where.y + r * 0.7F), ImVec2(where.x + r * 0.7F, where.y - r * 0.7F), IM_COL32(160, 160, 160, 200), 1.5F);
		}
	}

	void DrawFieldBrainOnMap(ImDrawList* drawList, const std::function<ImVec2(const Vector&)>& toMap, float mapScale) {
		const Actor* brain = On() ? Brain() : nullptr;
		if (!brain || (OnlySide() >= 0 && OnlySide() != s_BrainSide)) {
			return;
		}
		// The command range as a faint ring (across a wrap seam it is cut at the map's edge), and the brain as a pink diamond.
		const ImVec2 at = toMap(brain->GetPos());
		drawList->AddCircle(at, Range() * mapScale, IM_COL32(255, 120, 210, 110), 0, 1.0F);
		drawList->AddQuadFilled(ImVec2(at.x, at.y - 4.5F), ImVec2(at.x + 4.5F, at.y), ImVec2(at.x, at.y + 4.5F), ImVec2(at.x - 4.5F, at.y), IM_COL32(255, 120, 210, 255));
		drawList->AddQuad(ImVec2(at.x, at.y - 4.5F), ImVec2(at.x + 4.5F, at.y), ImVec2(at.x, at.y + 4.5F), ImVec2(at.x - 4.5F, at.y), IM_COL32(0, 0, 0, 220), 1.0F);
	}
} // namespace SandboxDetail
