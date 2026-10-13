// Battle Command: two players on one screen (hot-seat, a choice on the battle panel). Tick "You command this team" on two teams and each player
// gets the battle in turn: so many seconds of it, live, commanding only their own team and seeing only what it sees with fog of war on. Between
// turns the game pauses and the screen is covered, so the player handing over doesn't show the next one their side.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		enum class SeatPhase {
			Off, //!< No hot-seat game on.
			Handoff, //!< Paused and covered, for the next player to take the seat.
			Playing //!< The turn's player has the battle.
		};

		SeatPhase s_Phase = SeatPhase::Off;
		int s_Turn = 0; //!< Of the teams the players command, in side order.
		long long s_TurnEndsAt = 0; //!< The sim update the turn ends on (it doesn't run on while paused).
		int s_LoadedTurn = -1; //!< A saved hot-seat game loaded: the turn it was on, handed to that player when the game starts, not the first's.

		/// The teams the players command, in side order.
		std::vector<int> Seats() {
			std::vector<int> seats;
			for (int side = 0; side < c_Sides; ++side) {
				if (BattlePlayerCommands(side)) {
					seats.push_back(side);
				}
			}
			return seats;
		}

		bool On() { return Sandbox::IsBattleCommand() && s_ModeRun.Running && s_ModeRun.Settings.HotSeat && Seats().size() >= 2; }

		void Pause(bool paused) {
			if (g_DebugMan.IsUserPaused() != paused) {
				g_DebugMan.ToggleUserPause();
			}
		}

		/// The turn over: paused, the selection dropped, and the seat to the next player.
		void EndTurn(const std::vector<int>& seats) {
			Pause(true);
			s_Selected.clear();
			s_Turn = (s_Turn + 1) % static_cast<int>(seats.size());
			s_Phase = SeatPhase::Handoff;
		}

		void StartTurn(const std::vector<int>& seats) {
			s_Phase = SeatPhase::Playing;
			s_TurnEndsAt = g_TimerMan.GetSimUpdateCount() + std::max(1LL, static_cast<long long>(static_cast<float>(std::max(s_ModeRun.Settings.TurnSeconds, 5)) / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F)));
			Pause(false);
			// One of the team whose turn it is says so.
			const int side = seats[s_Turn];
			for (Actor* actor: SandboxAccess::Actors()) {
				if (actor->GetTeam() == side && IsSoldier(actor) && !actor->IsPlayerControlled() && actor->Say("HotSeatTurn")) {
					break;
				}
			}
		}
	} // namespace

	void SaveHotSeat(std::ostream& out) {
		// (Saved in the hand-over, the seat goes to the next player: the turn was already passed on.)
		out << ' ' << (s_Phase == SeatPhase::Off ? -1 : s_Turn);
	}

	void LoadHotSeat(std::istream& in) {
		int turn = -1;
		in >> turn;
		s_Phase = SeatPhase::Off;
		s_LoadedTurn = s_ModeRun.Running && !s_ModeRun.Over && s_ModeRun.Settings.HotSeat ? turn : -1;
	}

	int HotSeatSide() {
		if (s_Phase == SeatPhase::Off || !On()) {
			return -1;
		}
		const std::vector<int> seats = Seats();
		return seats[std::clamp(s_Turn, 0, static_cast<int>(seats.size()) - 1)];
	}

	void DrawHotSeat() {
		if (!On() || s_ModeRun.Over) {
			if (s_Phase != SeatPhase::Off && s_ModeRun.Over) {
				// (The game over: the report takes it from here.)
				s_Phase = SeatPhase::Off;
			} else if (!s_ModeRun.Running) {
				s_Phase = SeatPhase::Off;
			}
			return;
		}
		const std::vector<int> seats = Seats();
		if (s_Phase == SeatPhase::Off) {
			// A new game: the first player's seat (a loaded one's: the seat whose turn it was, from the start of the turn).
			s_Turn = std::max(s_LoadedTurn, 0);
			s_LoadedTurn = -1;
			s_Selected.clear();
			Pause(true);
			s_Phase = SeatPhase::Handoff;
		}
		s_Turn = std::clamp(s_Turn, 0, static_cast<int>(seats.size()) - 1);
		const int side = seats[s_Turn];
		const ImVec4 color = ImGui::ColorConvertU32ToFloat4(c_SideColors[side]);
		const GameViewRect view = g_WindowMan.GetGameViewRect();
		if (s_Phase == SeatPhase::Handoff) {
			// The whole view covered, on top of everything, till the next player is ready.
			ImGui::SetNextWindowPos(ImVec2(view.x, view.y));
			ImGui::SetNextWindowSize(ImVec2(view.w, view.h));
			ImGui::SetNextWindowFocus();
			ImGui::SetNextWindowBgAlpha(1.0F);
			if (ImGui::Begin("##hotSeatCover", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {
				const std::string title = std::string(c_SideNames[side]) + "'s turn";
				const std::string note = "Pass the seat to " + std::string(c_SideNames[side]) + "'s player. You get " + std::to_string(std::max(s_ModeRun.Settings.TurnSeconds, 5)) + " s of the battle (pausing doesn't use it up).";
				ImGui::SetWindowFontScale(2.0F);
				const ImVec2 titleSize = ImGui::CalcTextSize(title.c_str());
				ImGui::SetCursorPos(ImVec2((view.w - titleSize.x) * 0.5F, view.h * 0.4F));
				ImGui::TextColored(color, "%s", title.c_str());
				ImGui::SetWindowFontScale(1.0F);
				const ImVec2 noteSize = ImGui::CalcTextSize(note.c_str());
				ImGui::SetCursorPos(ImVec2((view.w - noteSize.x) * 0.5F, view.h * 0.4F + titleSize.y + 12.0F));
				ImGui::TextUnformatted(note.c_str());
				const float width = 200.0F;
				ImGui::SetCursorPos(ImVec2((view.w - width) * 0.5F, view.h * 0.4F + titleSize.y + noteSize.y + 30.0F));
				if (ToolUI::Button("Ready", ImVec2(width, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
					StartTurn(seats);
				}
			}
			ImGui::End();
			return;
		}
		// Playing: the turn's time, and the button to end it early, at the top of the view.
		const long long now = g_TimerMan.GetSimUpdateCount();
		if (now >= s_TurnEndsAt) {
			EndTurn(seats);
			return;
		}
		const float left = static_cast<float>(s_TurnEndsAt - now) * g_TimerMan.GetDeltaTimeSecs();
		ImGui::SetNextWindowPos(ImVec2(view.x + view.w * 0.5F, view.y + 8.0F), ImGuiCond_Always, ImVec2(0.5F, 0.0F));
		ImGui::SetNextWindowBgAlpha(0.8F);
		if (ImGui::Begin("##hotSeatTurn", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
			ImGui::TextColored(color, "%s's turn: %d s", c_SideNames[side], static_cast<int>(std::ceil(left)));
			ImGui::SameLine();
			if (ImGui::SmallButton("End turn")) {
				EndTurn(seats);
			}
		}
		ImGui::End();
	}
} // namespace SandboxDetail
