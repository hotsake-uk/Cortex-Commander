// Battle Command: the morale of the team you command shown. The engine already tracks each unit's nerve (Actor::GetMorale) and its AI pulls
// a unit back once it's gone; here those of yours that are breaking get a mark over them, and several breaking near each other at once is
// said, so you know to Rally them (the Rally commander power).

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		constexpr long long c_MoraleEvery = 30; //!< Sim updates between looks.
		constexpr float c_TogetherReach = 250.0F; //!< Breaking units this near each other break together.
		constexpr int c_BreakingTogether = 3; //!< This many of them, and it's said.
		constexpr float c_SayAgainSeconds = 20.0F; //!< A unit already said to be breaking isn't said again for this long.

		std::unordered_map<long, long long> s_SaidAt; //!< When each of your units was last said to be breaking, by unique ID.

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		bool InBattle() { return Sandbox::IsBattleCommand() && s_ModeRun.Running && !s_ModeRun.Over; }

		/// (A machine never loses its nerve: its morale stays at 1.)
		bool Shaken(const Actor* actor) { return actor->GetStatus() != Actor::DYING && actor->GetStatus() != Actor::DEAD && actor->GetMorale() < c_ShakenMorale; }

		void Note(const std::string& note) {
			s_ModeRun.Note = note;
			s_ModeRun.NoteAt = g_TimerMan.GetSimUpdateCount();
			g_ConsoleMan.PrintString("BATTLE: " + note);
		}
	} // namespace

	void UpdateMorale() {
		if (!InBattle()) {
			s_SaidAt.clear();
			return;
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		if (now % c_MoraleEvery != 0) {
			return;
		}
		const int yours = CommandedTeam();
		std::vector<const Actor*> shaken;
		for (const Actor* actor: SandboxAccess::Actors()) {
			if (actor->GetTeam() == yours && IsSelectable(actor) && Shaken(actor)) {
				shaken.push_back(actor);
			}
		}
		const long long sayAgain = static_cast<long long>(c_SayAgainSeconds * UpdatesPerSecond());
		auto fresh = [&](const Actor* actor) {
			auto said = s_SaidAt.find(static_cast<long>(actor->GetUniqueID()));
			return said == s_SaidAt.end() || now - said->second > sayAgain;
		};
		for (const Actor* actor: shaken) {
			if (!fresh(actor)) {
				continue;
			}
			std::vector<const Actor*> together;
			for (const Actor* other: shaken) {
				if (g_SceneMan.ShortestDistance(actor->GetPos(), other->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(c_TogetherReach)) {
					together.push_back(other);
				}
			}
			if (static_cast<int>(together.size()) >= c_BreakingTogether && std::count_if(together.begin(), together.end(), fresh) >= c_BreakingTogether) {
				for (const Actor* other: together) {
					s_SaidAt[static_cast<long>(other->GetUniqueID())] = now;
				}
				Note(std::to_string(together.size()) + " of your units are breaking: Rally them!");
				break;
			}
		}
		std::erase_if(s_SaidAt, [now, sayAgain](const auto& entry) { return now - entry.second > sayAgain * 3; });
	}

	void DrawMorale() {
		if (!InBattle() || g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const float scale = ScenePixelsPerWindowPixel();
		const int yours = CommandedTeam();
		const float radius = 6.0F;
		for (const Actor* actor: SandboxAccess::Actors()) {
			if (actor->GetTeam() != yours || !Shaken(actor)) {
				continue;
			}
			// Over its rank stars (DrawVeterans), pulsing.
			Vector onScreen = FromCamera(actor->GetPos() - Vector(0.0F, actor->GetHeight() * 0.5F + 28.0F));
			ImVec2 at(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
			const float pulse = 0.65F + 0.35F * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0F);
			const int alpha = static_cast<int>(255.0F * pulse);
			drawList->AddCircleFilled(at, radius, IM_COL32(240, 150, 40, alpha), 16);
			drawList->AddCircle(at, radius, IM_COL32(40, 20, 0, alpha), 16, 1.0F);
			const ImVec2 size = ImGui::CalcTextSize("!");
			drawList->AddText(ImVec2(std::floor(at.x - size.x * 0.5F), std::floor(at.y - size.y * 0.5F)), IM_COL32(30, 15, 0, alpha), "!");
		}
	}
} // namespace SandboxDetail
