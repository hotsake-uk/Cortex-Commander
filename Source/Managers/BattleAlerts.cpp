// Battle Command: alerts with jump-to, and a kill feed. What needs your eye (your units under attack or falling, breaking, a strike
// coming in, your flag taken, enemy reinforcements on the way) is listed at the top left of the view for a while: click one to look there,
// or press Space for the latest and Shift+Space for the ones before it. Each kind has a short sound of its own (B-8), and the last 50 are
// kept in the alert log, to look back through. Who killed whom is listed at the top right, yours in your colour. How many lines each shows
// and for how long are in the settings (B-9).

#include "SandboxInternal.h"

#include <deque>

namespace SandboxDetail {
	namespace {
		constexpr size_t c_MostLogged = 50; //!< Alerts kept in the log.
		constexpr float c_FadeSeconds = 2.0F; //!< The last of an alert's or a kill's time listed, fading out.
		constexpr float c_SameSeconds = 5.0F; //!< An alert like one this recent and near is added to it rather than listed again.
		constexpr float c_SameReach = 400.0F;
		constexpr float c_AttackAgainSeconds = 15.0F; //!< Your units under attack near where they were last said to be isn't said again for this long.
		constexpr float c_BlinkSeconds = 5.0F; //!< How long where an alert came blinks on the map window.
		constexpr double c_SoundGap = 0.4; //!< Seconds (real) at least between one alert sound and the next, so a burst of them is one sound.

		struct Alert {
			std::string Kind; //!< What it's about, for adding like ones together.
			std::string Text;
			int Count = 1;
			Vector Pos;
			ImU32 Color = 0;
			long long At = 0; //!< The sim update it was last added to.
			int Id = 0; //!< The same as its entry in the log.
		};
		std::deque<Alert> s_Alerts; //!< Newest first.
		std::deque<Alert> s_Log; //!< The alerts of the battle, newest first (one added to moves back to the front), up to c_MostLogged.
		int s_NextId = 1;
		int s_LogAt = -1; //!< The entry of the log last looked at with Space or Shift+Space, -1 for none.
		bool s_LogOpen = false;
		long long s_BattleStart = 0; //!< The sim update the alerts were last forgotten on: the battle's start, for the log's clock.
		std::vector<std::string> s_SoundsDue; //!< The kinds of the alerts that came since the last frame, to sound in it (on the main thread).

		struct Kill {
			std::string Killer; //!< Empty for a unit that fell with nobody of another team near.
			std::string Victim;
			int KillerTeam = -1;
			int VictimTeam = -1;
			long long At = 0;
		};
		std::deque<Kill> s_Kills; //!< Newest first.

		std::unordered_map<long, float> s_LastHealth; //!< Your units' health at the last look, by unique ID.

		/// The sound each kind of alert makes, from the game's own interface sounds; any kind not here makes the last one's.
		struct AlertSound {
			const char* Kind;
			const char* File;
		};
		constexpr AlertSound c_AlertSounds[] = {
		    {"attack", "Base.rte/Sounds/GUIs/PlacementBlip.flac"},
		    {"lost", "Base.rte/Sounds/GUIs/PieMenuExit.flac"},
		    {"brain", "Base.rte/Sounds/GUIs/UserError.flac"},
		    {"strike", "Base.rte/Sounds/GUIs/UserError.flac"},
		    {"flag", "Base.rte/Sounds/GUIs/BrainSwitch.flac"},
		    {"vip", "Base.rte/Sounds/GUIs/BrainSwitch.flac"},
		    {"breaking", "Base.rte/Sounds/GUIs/FocusChange.flac"},
		    {"wave", "Base.rte/Sounds/GUIs/MenuEnter.flac"},
		    {"escort", "Base.rte/Sounds/GUIs/MenuEnter.flac"},
		    {"pickup", "Base.rte/Sounds/GUIs/ItemChange.flac"},
		    {"airlift", "Base.rte/Sounds/GUIs/ActorSwitch.flac"},
		    {"breach", "Base.rte/Sounds/GUIs/SlicePicked.flac"},
		    {"build", "Base.rte/Sounds/GUIs/BackButtonPress.flac"},
		    {"army", "Base.rte/Sounds/GUIs/ButtonPress.flac"},
		    {"reinforce", "Base.rte/Sounds/GUIs/SelectionChange.flac"},
		};

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		float SecondsSince(long long at) { return static_cast<float>(g_TimerMan.GetSimUpdateCount() - at) / UpdatesPerSecond(); }

		bool InBattle() { return Sandbox::IsBattleCommand() && s_ModeRun.Running; }

		float AlertSeconds() { return static_cast<float>(g_SettingsMan.BattleAlertSeconds()); }

		float KillSeconds() { return static_cast<float>(g_SettingsMan.BattleKillSeconds()); }

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

		/// Minutes and seconds, as "3:07".
		std::string Clock(float seconds) {
			const int whole = std::max(static_cast<int>(seconds), 0);
			char text[16];
			std::snprintf(text, sizeof(text), "%d:%02d", whole / 60, whole % 60);
			return text;
		}

		/// An alert's text with how many it counts, as "3x Your units are under attack".
		std::string Line(const Alert& alert) { return (alert.Count > 1 ? std::to_string(alert.Count) + "x " : std::string()) + alert.Text; }

		/// The sounds of the alerts that came since the last frame: one, of the first, so a burst of them isn't a din. On the main thread.
		void PlayAlertSounds() {
			if (s_SoundsDue.empty()) {
				return;
			}
			const std::string kind = s_SoundsDue.front();
			s_SoundsDue.clear();
			static double lastPlayed = -100.0;
			const double now = ImGui::GetTime();
			if (!g_SettingsMan.BattleAlertSounds() || g_SettingsMan.BattleAlertVolume() <= 0.0F || now - lastPlayed < c_SoundGap) {
				return;
			}
			const AlertSound* sound = &c_AlertSounds[std::size(c_AlertSounds) - 1];
			for (const AlertSound& each: c_AlertSounds) {
				if (kind == each.Kind) {
					sound = &each;
					break;
				}
			}
			// (Made once each, the first time it's wanted, and kept for the game's life: never destroyed, so not after the audio is shut down at exit.)
			static std::unordered_map<std::string, SoundContainer*> made;
			SoundContainer*& container = made[sound->File];
			if (!container) {
				container = new SoundContainer();
				container->Create(sound->File, true, false, SoundContainer::BusRouting::UI);
			}
			container->SetVolume(g_SettingsMan.BattleAlertVolume());
			container->Play();
			lastPlayed = now;
		}

		/// The alert log's window: the battle's last alerts, newest first, each with when it came; a click looks there.
		void DrawAlertLog() {
			if (!s_LogOpen) {
				return;
			}
			const GameViewRect view = g_WindowMan.GetGameViewRect();
			ImGui::SetNextWindowSize(ImVec2(420.0F, 320.0F), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(ImVec2(view.x + 12.0F, view.y + 200.0F), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Alert log###BattleAlertLog", &s_LogOpen, ImGuiWindowFlags_NoFocusOnAppearing)) {
				if (s_Log.empty()) {
					ImGui::TextDisabled("No alerts yet in this battle.");
				} else if (ImGui::BeginTable("##alertLog", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed);
					ImGui::TableSetupColumn("Alert", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableHeadersRow();
					for (int i = 0; i < static_cast<int>(s_Log.size()); ++i) {
						const Alert& alert = s_Log[i];
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::PushID(alert.Id);
						const std::string when = Clock(static_cast<float>(alert.At - s_BattleStart) / UpdatesPerSecond());
						if (ImGui::Selectable(when.c_str(), i == s_LogAt, ImGuiSelectableFlags_SpanAllColumns)) {
							s_LogAt = i;
							LookAt(alert.Pos);
						}
						ImGui::SetItemTooltip("%s ago, %s into the battle. Click to look there.", Clock(SecondsSince(alert.At)).c_str(), when.c_str());
						ImGui::PopID();
						ImGui::TableNextColumn();
						ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(WithAlpha(alert.Color, 1.0F)), "%s", Line(alert).c_str());
					}
					ImGui::EndTable();
				}
			}
			ImGui::End();
		}
	} // namespace

	void ForgetAlerts() {
		s_Alerts.clear();
		s_Log.clear();
		s_LogAt = -1;
		s_SoundsDue.clear();
		s_Kills.clear();
		s_LastHealth.clear();
		s_BattleStart = g_TimerMan.GetSimUpdateCount();
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
				// (In the log too, moved to the front as the newest.)
				auto logged = std::find_if(s_Log.begin(), s_Log.end(), [&alert](const Alert& entry) { return entry.Id == alert.Id; });
				if (logged != s_Log.end()) {
					const int from = static_cast<int>(logged - s_Log.begin());
					s_Log.erase(logged);
					s_LogAt = s_LogAt == from ? 0 : (s_LogAt >= 0 && s_LogAt < from ? s_LogAt + 1 : s_LogAt);
				} else if (s_LogAt >= 0) {
					++s_LogAt;
				}
				s_Log.push_front(alert);
				while (s_Log.size() > c_MostLogged) {
					s_Log.pop_back();
				}
				s_LogAt = std::min(s_LogAt, static_cast<int>(s_Log.size()) - 1);
				return;
			}
		}
		const Alert alert{kind, text, 1, at, color, now, s_NextId++};
		s_Alerts.push_front(alert);
		while (s_Alerts.size() > static_cast<size_t>(g_SettingsMan.BattleAlertLines())) {
			s_Alerts.pop_back();
		}
		s_Log.push_front(alert);
		if (s_LogAt >= 0) {
			++s_LogAt;
		}
		while (s_Log.size() > c_MostLogged) {
			s_Log.pop_back();
		}
		s_LogAt = std::min(s_LogAt, static_cast<int>(s_Log.size()) - 1);
		s_SoundsDue.push_back(kind);
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
		while (s_Kills.size() > static_cast<size_t>(g_SettingsMan.BattleKillLines())) {
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

	void JumpToAlert(bool back) {
		if (s_Log.empty()) {
			return;
		}
		// Space: the newest. Shift+Space: one further back each press (from none looked at yet, the newest), staying on the oldest kept.
		s_LogAt = back ? std::min(s_LogAt + 1, static_cast<int>(s_Log.size()) - 1) : 0;
		LookAt(s_Log[std::max(s_LogAt, 0)].Pos);
	}

	void ToggleAlertLog() { s_LogOpen = !s_LogOpen; }

	bool AlertLogOpen() { return s_LogOpen; }

	void DrawAlertsOnMap(ImDrawList* drawList, const std::function<ImVec2(const Vector&)>& toMap) {
		if (!InBattle()) {
			return;
		}
		// Each new alert's place blinks a few times a second for a few seconds, a ring growing out of it as it does.
		const bool lit = std::fmod(ImGui::GetTime(), 0.5) < 0.3;
		const float grow = static_cast<float>(std::fmod(ImGui::GetTime(), 1.0));
		for (const Alert& alert: s_Log) {
			const float age = SecondsSince(alert.At);
			if (age > c_BlinkSeconds) {
				break;
			}
			const ImVec2 at = toMap(alert.Pos);
			const float fade = std::clamp((c_BlinkSeconds - age) / 1.5F, 0.0F, 1.0F);
			if (lit) {
				drawList->AddCircleFilled(at, 3.5F, WithAlpha(alert.Color, fade));
			}
			drawList->AddCircle(at, 4.0F + grow * 8.0F, WithAlpha(alert.Color, fade * (1.0F - grow)), 0, 1.5F);
		}
	}

	void DrawAlerts() {
		if (!InBattle()) {
			s_LogOpen = s_LogOpen && Sandbox::IsBattleCommand();
			return;
		}
		PlayAlertSounds();
		if (g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		const float alertSeconds = AlertSeconds();
		const float killSeconds = KillSeconds();
		std::erase_if(s_Alerts, [alertSeconds](const Alert& alert) { return SecondsSince(alert.At) > alertSeconds; });
		std::erase_if(s_Kills, [killSeconds](const Kill& kill) { return SecondsSince(kill.At) > killSeconds; });
		while (s_Alerts.size() > static_cast<size_t>(g_SettingsMan.BattleAlertLines())) {
			s_Alerts.pop_back();
		}
		while (s_Kills.size() > static_cast<size_t>(g_SettingsMan.BattleKillLines())) {
			s_Kills.pop_back();
		}
		const GameViewRect view = g_WindowMan.GetGameViewRect();
		// The alerts, top left: each a button that looks there, and above them the log's. (Not while you play a unit: the view is on it.)
		if (g_SettingsMan.ShowBattleAlerts() && !s_Alerts.empty() && !s_Possessed) {
			ImGui::SetNextWindowPos(ImVec2(ViewOrigin().x + 12.0F, ViewOrigin().y + 56.0F));
			ImGui::SetNextWindowBgAlpha(0.0F);
			const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
			if (ImGui::Begin("##BattleAlerts", nullptr, flags)) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0F, 0.0F, 0.0F, s_LogOpen ? 0.8F : 0.55F));
				if (ImGui::SmallButton(s_LogOpen ? "Log (open)##alertLog" : "Log##alertLog")) {
					ToggleAlertLog();
				}
				ImGui::PopStyleColor();
				ImGui::SetItemTooltip("Opens or closes the alert log: the last 50 alerts of this battle with when each came, to look back through.\nShift+Space steps back through them too, one each press.");
				for (size_t i = 0; i < s_Alerts.size(); ++i) {
					const Alert& alert = s_Alerts[i];
					const float alpha = Alpha(SecondsSince(alert.At), alertSeconds);
					const std::string text = Line(alert) + "##alert" + std::to_string(i);
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(WithAlpha(alert.Color, alpha)));
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0F, 0.0F, 0.0F, 0.55F * alpha));
					if (ImGui::Button(text.c_str())) {
						LookAt(alert.Pos);
					}
					ImGui::PopStyleColor(2);
					ImGui::SetItemTooltip("%s", i == 0 ? "Click to look there (or press Space; Shift+Space for the ones before)." : "Click to look there.");
				}
			}
			ImGui::End();
		}
		DrawAlertLog();
		// The kill feed, top right.
		if (g_SettingsMan.ShowBattleKillFeed() && !s_Kills.empty()) {
			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			float y = ViewOrigin().y + 56.0F;
			const float right = ViewOrigin().x + static_cast<float>(view.w) - 12.0F;
			for (const Kill& kill: s_Kills) {
				const float alpha = Alpha(SecondsSince(kill.At), killSeconds);
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
