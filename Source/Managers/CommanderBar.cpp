// Battle Command, the RTS game mode: the Commander Toolbar along the bottom and the battle panel. Built on the Sandbox game mode's view
// and command tool, without its other tools (they stay the sandbox's, on F7 and F11 while Battle Command is in development).

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		constexpr float c_Speeds[] = {0.25F, 0.5F, 1.0F, 2.0F, 3.0F}; //!< The speed tile's speeds, slowest first.
	} // namespace

	bool ToolbarKeyPressed(int number, bool shift) {
		const ImGuiIO& io = ImGui::GetIO();
		// (The toolbar itself is only up while you're above the world, not while you play a unit.)
		return number >= 1 && number <= 9 && io.KeyAlt && !io.KeyCtrl && io.KeyShift == shift && !io.WantTextInput && !s_Possessed && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + number), false);
	}

	void StepGameSpeed(int step) {
		const float timeScale = g_TimerMan.GetTimeScale();
		float next = 1.0F;
		if (step > 0) {
			// The next speed up, or the fastest again.
			next = c_Speeds[std::size(c_Speeds) - 1];
			for (float speed: c_Speeds) {
				if (speed > timeScale + 0.01F) {
					next = speed;
					break;
				}
			}
		} else if (step < 0) {
			// The next speed down, or the slowest again.
			next = c_Speeds[0];
			for (float speed: c_Speeds) {
				if (speed < timeScale - 0.01F) {
					next = speed;
				}
			}
		}
		g_TimerMan.SetTimeScale(next);
	}

	int CommandedTeam() {
		if (!Sandbox::IsGodMode() || !s_CommanderBar) {
			return -1;
		}
		// (The Sandbox game mode with the Commander Toolbar swapped in: the side picked in the sandbox.)
		if (!Sandbox::IsBattleCommand()) {
			return std::clamp(s_Team, 0, c_Sides - 1);
		}
		if (int seat = HotSeatSide(); seat >= 0) {
			return seat;
		}
		const BattleModeSettings& settings = s_ModeRun.Running ? s_ModeRun.Settings : s_ModeSetup;
		for (int side = 0; side < c_Sides; ++side) {
			if (settings.PlayerCommands[side] && settings.Plays[side]) {
				return side;
			}
		}
		return 0;
	}

	int OnlySide() {
		return s_Commander ? s_CommanderTeam : CommandedTeam();
	}

	bool BattleSetupShowing() {
		return Sandbox::IsBattleCommand() ? s_BattlePanelOpen : Sandbox::IsOpen() && s_CurrentTab == "Battle";
	}

	void KeepCommandedTeam() {
		BattleModeSettings& setup = s_ModeSetup;
		bool changed = false;
		// Battle Command plays the battle modes, not the Battle Director's custom cards (a setup saved with the map in the sandbox may be one).
		if (setup.Mode == BattleMode::Custom) {
			setup.Mode = BattleMode::CaptureTheFlag;
			changed = true;
		}
		// You command one team, always one in the battle: ticking another moves you to it, and with none ticked it's the first one in. (Two
		// with hot-seat: one for each player.)
		static int yours = -1;
		int ticked = 0;
		int firstIn = -1;
		for (int side = 0; side < c_Sides; ++side) {
			ticked += setup.PlayerCommands[side] && setup.Plays[side] ? 1 : 0;
			if (firstIn < 0 && setup.Plays[side]) {
				firstIn = side;
			}
		}
		if (ticked > (setup.HotSeat ? 2 : 1) && yours >= 0 && yours < c_Sides && setup.PlayerCommands[yours]) {
			setup.PlayerCommands[yours] = false;
			changed = true;
		}
		for (int side = 0; side < c_Sides; ++side) {
			if (!setup.Plays[side] && setup.PlayerCommands[side]) {
				setup.PlayerCommands[side] = false;
				changed = true;
			}
		}
		if (std::none_of(setup.PlayerCommands.begin(), setup.PlayerCommands.end(), [](bool on) { return on; })) {
			setup.PlayerCommands.fill(false);
			setup.PlayerCommands[std::max(firstIn, 0)] = true;
			setup.Plays[std::max(firstIn, 0)] = true;
			changed = true;
		}
		for (int side = 0; side < c_Sides; ++side) {
			if (setup.PlayerCommands[side]) {
				yours = side;
				break;
			}
		}
		if (changed) {
			SendBattleMode();
		}
		// What the Control and rally tools work with is your side.
		if (int side = CommandedTeam(); side >= 0) {
			s_Team = side;
		}
	}

	void DrawCommanderBar() {
		GameViewRect view = g_WindowMan.GetGameViewRect();
		const ImGuiStyle& style = ImGui::GetStyle();
		float pixel = ToolUI::Pixel();
		const int side = std::clamp(CommandedTeam(), 0, c_Sides - 1);
		struct MainTool {
			Tool Kind;
			const char* Label;
		};
		static const MainTool mainTools[] = {{Tool::Command, "Command"}, {Tool::Follow, "Follow"}, {Tool::Possess, "Control"}};
		static float rightWidth = 0.0F; // How wide the right-hand group was last frame, to put it against the right edge.

		// Across the whole picture, its bottom on the picture's bottom edge, as tall as its contents (as the sandbox's bar).
		ImGui::SetNextWindowPos(ImVec2(view.x, view.y + view.h), ImGuiCond_Always, ImVec2(0.0F, 1.0F));
		ImGui::SetNextWindowSize(ImVec2(std::floor(view.w), 0.0F), ImGuiCond_Always);
		ImGui::PushWrapSameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(8.0F, 8.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pixel * 6.0F, pixel * 4.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pixel * 2.0F, pixel * 3.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pixel * 4.0F, pixel * 2.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
		if (ImGui::Begin("##CommanderBar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
			ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->ChannelsSplit(2);
			drawList->ChannelsSetCurrent(1);
			const ImVec2 windowPos = ImGui::GetWindowPos();
			const float windowRight = windowPos.x + ImGui::GetWindowWidth();
			const float bottom = view.y + view.h;

			// The shelf: with the Command tool, its row of formations, rules and groups; with a battle tool (drawing a spawn zone), its settings.
			float split = windowPos.y;
			float shelfRight = windowRight;
			if (ShelfRow()) {
				shelfRight = std::min(windowRight, ImGui::GetCurrentWindow()->DC.CursorMaxPos.x + style.WindowPadding.x);
				split = ImGui::GetCursorScreenPos().y - style.ItemSpacing.y + pixel * 3.0F;
				ImGui::Dummy(ImVec2(0.0F, pixel));
			}

			// The command tools.
			for (size_t i = 0; i < std::size(mainTools); ++i) {
				int index = ToolIndex(mainTools[i].Kind);
				ToolLook look = LookOf(mainTools[i].Kind);
				ImGui::PushID(index);
				if (i > 0) {
					ImGui::SameLine();
				}
				std::string tip = std::string(c_Tools[index].Name) + (ToolTipText(mainTools[i].Kind) ? std::string("\n") + ToolTipText(mainTools[i].Kind) : "");
				if (BarTile("##main", mainTools[i].Label, tip.c_str(), s_ToolIndex == index, [&](ImDrawList* tileList, ImVec2 at, float room) { DrawIcon(tileList, look.Art, at, room / 12.0F, look.Color); }) == 1) {
					if (mainTools[i].Kind == Tool::Command) {
						// Picked again while selecting, it lets them all go.
						if (s_ToolIndex == index && (s_CommandMode == CommandMode::Select || s_CommandMode == CommandMode::Command)) {
							s_Selected.clear();
						}
						s_CommandMode = CommandMode::Command;
						s_PatrolDraft.clear();
					}
					if (IsBattleTool(CurrentTool().Kind)) {
						PutDownBattleTool();
					}
					s_ToolIndex = index;
				}
				ImGui::PopID();
			}
			BarDivider();
			// The battle panel: setting the battle up, starting and stopping it.
			{
				const bool running = s_ModeRun.Running;
				std::string tip = running ? "Battle: the game on now, its teams and their scores; start it again or stop it." : "Battle: pick the mode, draw the teams' spawn zones and objectives, choose your team, and start.";
				// (In the Sandbox game mode the battle is set up on the sandbox window's Battle tab.)
				if (BarTile("##battle", "Battle", tip.c_str(), BattleSetupShowing(), [&](ImDrawList* tileList, ImVec2 at, float room) { DrawIcon(tileList, Icon::Flag, at, room / 12.0F, IM_COL32(242, 182, 61, 255)); }) == 1) {
					if (Sandbox::IsBattleCommand()) {
						// (Before the battle is started the panel stays open.)
						if (running) {
							s_BattlePanelOpen = !s_BattlePanelOpen;
						}
					} else {
						ToggleWindowTab("Battle");
					}
				}
				// The alert log: the battle's last alerts with when each came (B-8).
				if (Sandbox::IsBattleCommand()) {
					ImGui::SameLine();
					if (BarTile("##alertLog", "Alerts", "The alert log: the last 50 alerts of this battle with when each came; click one to look there.\nSpace looks at the latest, Shift+Space steps back through the ones before.", AlertLogOpen(), [&](ImDrawList* tileList, ImVec2 at, float room) { PictureText(tileList, at, room, IM_COL32(239, 160, 100, 255), "!"); }) == 1) {
						ToggleAlertLog();
					}
				}
			}
			BarDivider();
			// Your side, and its units: a click selects them all, a double click looks at them too.
			{
				int count = Sandbox::CountUnits(side);
				std::string number = std::to_string(count);
				std::string tip = std::string("You command ") + c_SideNames[side] + ": " + number + " fighting units in.\nClick: select them all. Double click: look at them too.\n" + (Sandbox::IsBattleCommand() ? "Which team is yours is set on the battle panel." : "Which team is yours is the side picked in the sandbox.");
				if (BarTile(
				        "##yours", c_SideNames[side], tip.c_str(), false, [&](ImDrawList* tileList, ImVec2 at, float room) {
					        float inset = room * 0.08F;
					        tileList->AddRectFilled(ImVec2(at.x + inset, at.y + inset), ImVec2(at.x + room - inset, at.y + room - inset), (c_SideColors[side] & 0x00FFFFFF) | ((count > 0 ? 110u : 50u) << IM_COL32_A_SHIFT));
					        PictureText(tileList, at, room, count > 0 ? ToolTheme::Text : IM_COL32(200, 200, 200, 160), number.c_str());
				        },
				        c_SideColors[side]) == 1) {
					s_Selected.clear();
					for (Actor* actor: SandboxAccess::Actors()) {
						if (IsSelectable(actor) && actor->GetTeam() == side) {
							s_Selected.push_back(MakeRef(actor));
						}
					}
					s_ToolIndex = ToolIndex(Tool::Command);
					s_CommandMode = CommandMode::Command;
					s_PatrolDraft.clear();
					if (ImGui::GetIO().MouseClickedLastCount[ImGuiMouseButton_Left] >= 2) {
						LookAtUnits(s_Selected);
					}
				}
			}
			ImGui::SameLine();
			// The Army overview: your side's units by kind.
			{
				const bool shown = g_SettingsMan.ShowArmyOverview();
				if (BarTile("##army", "Army", "The Army overview: your side's units by kind, with how many, their average health and how many are idle.\nClick a kind there to select them all; double click to look at them too.", shown, [&](ImDrawList* tileList, ImVec2 at, float room) { DrawIcon(tileList, Icon::Person, at, room / 12.0F, shown ? ToolTheme::Gold : ToolTheme::Text); }) == 1) {
					g_SettingsMan.SetShowArmyOverview(!shown);
				}
			}

			// Reinforcements: the points, the cards to call in, and where and how they come.
			BarDivider();
			ReinforcementTiles();
			// Commander powers, each with its cooldown.
			BarDivider();
			PowerTiles();

			// At the right: the speed of time and the pause.
			ImGui::SameLine(0.0F, pixel * 10.0F);
			{
				float contentRight = windowRight - style.WindowPadding.x;
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				if (cursor.x < contentRight - rightWidth) {
					ImGui::SetCursorScreenPos(ImVec2(contentRight - rightWidth, cursor.y));
				}
			}
			const float rightStart = ImGui::GetCursorScreenPos().x;
			{
				float timeScale = g_TimerMan.GetTimeScale();
				char shown[16];
				std::snprintf(shown, sizeof(shown), "%.3gx", timeScale);
				int clicked = BarTile("##speed", "Speed", "How fast time runs. Click: faster (0.25x, 0.5x, 1x, 2x, 3x, and round again). Right click: back to 1x.\nKeys: - slower, = faster, Shift with either back to 1x.", timeScale < 0.99F || timeScale > 1.01F, [&](ImDrawList* tileList, ImVec2 at, float room) { PictureText(tileList, at, room, ToolTheme::Text, shown); });
				if (clicked == 1) {
					float nextSpeed = c_Speeds[0];
					for (float speed: c_Speeds) {
						if (speed > timeScale + 0.01F) {
							nextSpeed = speed;
							break;
						}
					}
					g_TimerMan.SetTimeScale(nextSpeed);
				} else if (clicked == 2) {
					g_TimerMan.SetTimeScale(1.0F);
				}
			}
			ImGui::SameLine();
			{
				const bool paused = g_DebugMan.IsUserPaused();
				if (BarTile("##pause", paused ? "Paused" : "Pause", "Holds the world still till pressed again (Pause, or O). Orders can still be given meanwhile.", paused, [&](ImDrawList* tileList, ImVec2 at, float room) {
					    float bar = room * 0.18F;
					    ImU32 ink = paused ? IM_COL32(255, 210, 80, 255) : ToolTheme::Text;
					    tileList->AddRectFilled(ImVec2(at.x + room * 0.25F, at.y + room * 0.2F), ImVec2(at.x + room * 0.25F + bar, at.y + room * 0.8F), ink);
					    tileList->AddRectFilled(ImVec2(at.x + room * 0.75F - bar, at.y + room * 0.2F), ImVec2(at.x + room * 0.75F, at.y + room * 0.8F), ink);
				    }) == 1) {
					g_DebugMan.ToggleUserPause();
				}
			}
			rightWidth = ImGui::GetItemRectMax().x - rightStart;

			// The backgrounds: the main strip full width; the shelf above it, as wide as what is on it.
			drawList->ChannelsSetCurrent(0);
			drawList->AddRectFilled(ImVec2(windowPos.x, split), ImVec2(windowRight, bottom), (ToolTheme::Panel & 0x00FFFFFF) | (245u << IM_COL32_A_SHIFT));
			drawList->AddRectFilled(ImVec2(windowPos.x, split), ImVec2(windowRight, split + pixel), ToolTheme::EdgeDark);
			drawList->AddRectFilled(ImVec2(windowPos.x, split + pixel), ImVec2(windowRight, split + pixel * 2.0F), (ToolTheme::Edge & 0x00FFFFFF) | (200u << IM_COL32_A_SHIFT));
			if (split > windowPos.y) {
				drawList->AddRectFilled(ImVec2(windowPos.x, windowPos.y), ImVec2(shelfRight, split), (ToolTheme::EdgeDark & 0x00FFFFFF) | (235u << IM_COL32_A_SHIFT));
				drawList->AddRectFilled(ImVec2(windowPos.x, windowPos.y), ImVec2(shelfRight, windowPos.y + pixel), (ToolTheme::Edge & 0x00FFFFFF) | (200u << IM_COL32_A_SHIFT));
				drawList->AddRectFilled(ImVec2(shelfRight - pixel, windowPos.y), ImVec2(shelfRight, split), (ToolTheme::Edge & 0x00FFFFFF) | (200u << IM_COL32_A_SHIFT));
			}
			drawList->ChannelsMerge();
			s_BarHeight = bottom - windowPos.y;
		}
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(6);
		ImGui::PopWrapSameLine();
	}

	void DrawCommanderPanel() {
		GameViewRect view = g_WindowMan.GetGameViewRect();
		ImGui::SetNextWindowSize(ImVec2(430.0F, std::max(200.0F, view.h - s_BarHeight - 80.0F)), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(view.x + 15.0F, view.y + 40.0F), ImGuiCond_FirstUseEver);
		// No close button till the battle is started: the panel is the only way to start it.
		if (ImGui::Begin("Battle###CommanderPanel", s_ModeRun.Running ? &s_BattlePanelOpen : nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
			if (!s_ModeRun.Running) {
				ImGui::TextWrapped("Pick a mode, tick the teams in it and draw each one's spawn zone, then tick \"You command this team\" on yours and start. Your team's units are yours alone to order; the rest fight as the mode has them.");
			}
			BattlePresetsPanel();
			BattleModeChooser(false);
			if (ToolUI::Checkbox("Fog of war", &s_ModeSetup.FogOfWar)) {
				SendBattleMode();
			}
			ImGui::SetItemTooltip("%s", ("You see only the enemies your team's units can see: those in sight of one of them, not behind the ground. Where one was last seen\nis marked, fading, on the map and the minimap. Scouts and jetpack spotters up high see furthest. Can be changed during the game.\nThe sandbox's bar (" + Sandbox::CommanderBarKeyName() + ") sees everything.").c_str());
			if (ToolUI::Checkbox("Your brain on the field", &s_ModeSetup.FieldBrain)) {
				SendBattleMode();
			}
			ImGui::SetItemTooltip("Your brain is put down at your team's base when the game starts. If it dies, you lose. Orders only reach your units within its\ncommand range (the ring round it): those further out keep doing what they were told last, and the order skips them.");
			if (s_ModeSetup.FieldBrain) {
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0F);
				if (ImGui::SliderInt("Command range", &s_ModeSetup.FieldBrainRange, 300, 3000, "%d px")) {
					SendBattleMode();
				}
				ImGui::SetItemTooltip("How far from your brain your orders reach.");
			}
			if (ToolUI::Checkbox("Two players (hot-seat)", &s_ModeSetup.HotSeat)) {
				SendBattleMode();
			}
			ImGui::SetItemTooltip("Two players on one screen. Tick \"You command this team\" on two teams: each player gets the battle in turn, live, commanding only\ntheir own team (and with fog of war, seeing only what it sees). Between turns the game pauses and the screen is covered for the hand-over.");
			if (s_ModeSetup.HotSeat) {
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0F);
				if (ImGui::SliderInt("Turn", &s_ModeSetup.TurnSeconds, 5, 180, "%d s")) {
					SendBattleMode();
				}
				ImGui::SetItemTooltip("Seconds of the battle each player gets a turn. Pausing doesn't use them up.");
				int commanded = 0;
				for (int side = 0; side < c_Sides; ++side) {
					commanded += s_ModeSetup.PlayerCommands[side] && s_ModeSetup.Plays[side] ? 1 : 0;
				}
				if (commanded < 2) {
					ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.3F, 1.0F), "Tick \"You command this team\" on a second team.");
				}
			}
			ArmyPanel();
			LinkedBattleControls();
			if (BattleReportReady()) {
				if (ToolUI::Button("After-battle report", ImVec2(-1.0F, 0.0F))) {
					OpenBattleReport();
				}
				ImGui::SetItemTooltip("How the battle went: kills and losses, the best of your units, time on the objectives and the score over time.");
			}
			BattleModeTab();
		}
		ImGui::End();
	}
} // namespace SandboxDetail
