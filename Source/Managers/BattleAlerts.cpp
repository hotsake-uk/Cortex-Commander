// Battle Command: alerts with jump-to, and a kill feed. What needs your eye (your units under attack or falling, breaking, a strike
// coming in, your flag taken, enemy reinforcements on the way) is listed at the top left of the view for a while: click one to look there,
// or press Space for the latest. Who killed whom is listed at the top right, yours in your colour.

#include "SandboxInternal.h"

#include <deque>

namespace SandboxDetail {
	namespace {
		constexpr size_t c_MostAlerts = 6;
		constexpr size_t c_MostKills = 6;
		constexpr float c_AlertSeconds = 12.0F; //!< How long an alert stays listed.
		constexpr float c_KillSeconds = 10.0F; //!< And a kill.
		constexpr float c_FadeSeconds = 2.0F; //!< The last of that, fading out.
		constexpr float c_SameSeconds = 5.0F; //!< An alert like one this recent and near is added to it rather than listed again.
		constexpr float c_SameReach = 400.0F;
		constexpr float c_AttackAgainSeconds = 15.0F; //!< Your units under attack near where they were last said to be isn't said again for this long.

		struct Alert {
			std::string Kind; //!< What it's about, for adding like ones together.
			std::string Text;
			int Count = 1;
			Vector Pos;
			ImU32 Color = 0;
			long long At = 0; //!< The sim update it was last added to.
		};
		std::deque<Alert> s_Alerts; //!< Newest first.

		struct Kill {
			std::string Killer; //!< Empty for a unit that fell with nobody of another team near.
			std::string Victim;
			int KillerTeam = -1;
			int VictimTeam = -1;
			long long At = 0;
		};
		std::deque<Kill> s_Kills; //!< Newest first.

		std::unordered_map<long, float> s_LastHealth; //!< Your units' health at the last look, by unique ID.

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		float SecondsSince(long long at) { return static_cast<float>(g_TimerMan.GetSimUpdateCount() - at) / UpdatesPerSecond(); }

		bool InBattle() { return Sandbox::IsBattleCommand() && s_ModeRun.Running; }

		/// A unit's name in the feed: its rank and name if it has them, else its kind.
		std::string Label(long id) {
			const std::string title = VeteranTitle(id);
			const std::string kind = ReportUnitKind(id);
			if (title.empty()) {
				return kind.empty() ? std::string("a unit") : kind;
			}
			return kind.empty() ? title : title + " (" + kind + ")";
		}

		/// Moves the view to a point, as the minimap does.
		void LookAt(const Vector& at) {
			s_FreeCamera = true;
			s_FollowTarget = UnitRef();
			s_FollowAction = false;
			s_CameraCenter = at;
		}

		float Alpha(float age, float life) { return std::clamp((life - age) / c_FadeSeconds, 0.0F, 1.0F); }

		ImU32 WithAlpha(ImU32 color, float alpha) { return (color & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 255.0F) << IM_COL32_A_SHIFT); }
	} // namespace

	void ForgetAlerts() {
		s_Alerts.clear();
		s_Kills.clear();
		s_LastHealth.clear();
	}

	void AddAlert(const std::string& kind, const std::string& text, const Vector& at, ImU32 color) {
		if (!InBattle()) {
			return;
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		for (Alert& alert: s_Alerts) {
			if (alert.Kind == kind && SecondsSince(alert.At) < c_SameSeconds && g_SceneMan.ShortestDistance(alert.Pos, at, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(c_SameReach)) {
				++alert.Count;
				alert.Text = text;
				alert.At = now;
				return;
			}
		}
		s_Alerts.push_front({kind, text, 1, at, color, now});
		while (s_Alerts.size() > c_MostAlerts) {
			s_Alerts.pop_back();
		}
	}

	void NoteKillFeed(long killerID, long victimID, int victimTeam, const Vector& where) {
		if (!InBattle()) {
			return;
		}
		Kill kill;
		kill.Victim = Label(victimID);
		kill.VictimTeam = victimTeam;
		if (killerID != 0) {
			kill.Killer = Label(killerID);
			if (const Actor* killer = dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(killerID))) {
				kill.KillerTeam = killer->GetTeam();
			}
		}
		kill.At = g_TimerMan.GetSimUpdateCount();
		s_Kills.push_front(kill);
		while (s_Kills.size() > c_MostKills) {
			s_Kills.pop_back();
		}
		if (victimTeam == CommandedTeam()) {
			AddAlert("lost", kill.Victim + " lost", where, IM_COL32(239, 90, 80, 255));
		}
	}

	void UpdateAlerts() {
		if (!InBattle() || s_ModeRun.Over) {
			s_LastHealth.clear();
			return;
		}
		if (g_TimerMan.GetSimUpdateCount() % 30 != 11) {
			return;
		}
		// Your units under attack: one that has lost health since the last look, with none said to be near it lately.
		const int yours = CommandedTeam();
		std::unordered_map<long, float> health;
		for (Actor* actor: SandboxAccess::Actors()) {
			if (actor->GetTeam() != yours || !IsSelectable(actor) || !IsCombatant(actor)) {
				continue;
			}
			const long id = static_cast<long>(actor->GetUniqueID());
			health[id] = actor->GetHealth();
			auto before = s_LastHealth.find(id);
			if (before == s_LastHealth.end() || actor->GetHealth() >= before->second - 1.0F || actor->IsPlayerControlled()) {
				continue;
			}
			const bool said = std::any_of(s_Alerts.begin(), s_Alerts.end(), [actor](const Alert& alert) {
				return (alert.Kind == "attack" || alert.Kind == "lost") && SecondsSince(alert.At) < c_AttackAgainSeconds && g_SceneMan.ShortestDistance(alert.Pos, actor->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(c_SameReach);
			});
			if (!said) {
				AddAlert("attack", "Your units are under attack", actor->GetPos(), IM_COL32(239, 160, 100, 255));
			}
		}
		s_LastHealth.swap(health);
	}

	void JumpToLatestAlert() {
		if (!s_Alerts.empty()) {
			LookAt(s_Alerts.front().Pos);
		}
	}

	void DrawAlerts() {
		if (!InBattle() || g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		std::erase_if(s_Alerts, [](const Alert& alert) { return SecondsSince(alert.At) > c_AlertSeconds; });
		std::erase_if(s_Kills, [](const Kill& kill) { return SecondsSince(kill.At) > c_KillSeconds; });
		const GameViewRect view = g_WindowMan.GetGameViewRect();
		// The alerts, top left: each a button that looks there. (Not while you play a unit: the view is on it.)
		if (!s_Alerts.empty() && !s_Possessed) {
			ImGui::SetNextWindowPos(ImVec2(ViewOrigin().x + 12.0F, ViewOrigin().y + 56.0F));
			ImGui::SetNextWindowBgAlpha(0.0F);
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
			if (ImGui::Begin("##BattleAlerts", nullptr, flags)) {
				for (size_t i = 0; i < s_Alerts.size(); ++i) {
					const Alert& alert = s_Alerts[i];
					const float alpha = Alpha(SecondsSince(alert.At), c_AlertSeconds);
					const std::string text = (alert.Count > 1 ? std::to_string(alert.Count) + "x " : std::string()) + alert.Text + "##alert" + std::to_string(i);
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(WithAlpha(alert.Color, alpha)));
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0F, 0.0F, 0.0F, 0.55F * alpha));
					if (ImGui::Button(text.c_str())) {
						LookAt(alert.Pos);
					}
					ImGui::PopStyleColor(2);
					ImGui::SetItemTooltip("%s", i == 0 ? "Click to look there (or press Space)." : "Click to look there.");
				}
			}
			ImGui::End();
		}
		// The kill feed, top right.
		if (!s_Kills.empty()) {
			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			float y = ViewOrigin().y + 56.0F;
			const float right = ViewOrigin().x + static_cast<float>(view.w) - 12.0F;
			for (const Kill& kill: s_Kills) {
				const float alpha = Alpha(SecondsSince(kill.At), c_KillSeconds);
				const ImU32 killerColor = WithAlpha(kill.KillerTeam >= 0 && kill.KillerTeam < c_Sides ? c_SideColors[kill.KillerTeam] : IM_COL32(200, 200, 200, 255), alpha);
				const ImU32 victimColor = WithAlpha(kill.VictimTeam >= 0 && kill.VictimTeam < c_Sides ? c_SideColors[kill.VictimTeam] : IM_COL32(200, 200, 200, 255), alpha);
				const std::string middle = kill.Killer.empty() ? "" : "  killed  ";
				const std::string tail = kill.Killer.empty() ? "  fell" : "";
				const float width = ImGui::CalcTextSize(kill.Killer.c_str()).x + ImGui::CalcTextSize(middle.c_str()).x + ImGui::CalcTextSize(kill.Victim.c_str()).x + ImGui::CalcTextSize(tail.c_str()).x;
				const float height = ImGui::GetFontSize();
				float x = right - width;
				drawList->AddRectFilled(ImVec2(x - 6.0F, y - 2.0F), ImVec2(right + 6.0F, y + height + 2.0F), IM_COL32(0, 0, 0, static_cast<int>(120.0F * alpha)), 3.0F);
				auto put = [&](const std::string& text, ImU32 color) {
					drawList->AddText(ImVec2(x, y), color, text.c_str());
					x += ImGui::CalcTextSize(text.c_str()).x;
				};
				put(kill.Killer, killerColor);
				put(middle, WithAlpha(IM_COL32(200, 200, 200, 255), alpha));
				put(kill.Victim, victimColor);
				put(tail, WithAlpha(IM_COL32(200, 200, 200, 255), alpha));
				y += height + 6.0F;
			}
		}
	}
} // namespace SandboxDetail
