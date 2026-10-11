// Battle Command: the commander powers of the team you command. Each is used from the Commander Toolbar (on a point picked on the map, for
// those that need one), then cools down before it can be used again. The strikes are the sandbox's own Boom tools.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		constexpr int c_PowerCount = static_cast<int>(CommanderPower::Count);
		constexpr float c_ScanSeconds = 10.0F;
		constexpr float c_EvacRefund = 0.5F;

		/// The sim update each side's powers are ready again on.
		std::array<std::array<long long, c_PowerCount>, c_Sides> s_ReadyAt{};

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		/// Seconds before a side's power can be used again, 0 if it can now.
		float SecondsLeft(int side, int power) {
			const long long left = s_ReadyAt[side][power] - g_TimerMan.GetSimUpdateCount();
			return left > 0 ? static_cast<float>(left) / UpdatesPerSecond() : 0.0F;
		}

		/// Your units within reach of a point.
		std::vector<Actor*> UnitsAround(int side, const Vector& at, float radius) {
			std::vector<Actor*> units;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (IsSelectable(actor) && actor->GetTeam() == side && g_SceneMan.ShortestDistance(at, actor->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(radius)) {
					units.push_back(actor);
				}
			}
			return units;
		}

		void Note(const std::string& note) {
			s_ModeRun.Note = note;
			s_ModeRun.NoteAt = g_TimerMan.GetSimUpdateCount();
			g_ConsoleMan.PrintString("BATTLE: " + note);
		}

		void UsePower(int power) {
			Stroke stroke;
			stroke.Kind = Tool::BattleTeam;
			stroke.Team = CommandedTeam();
			stroke.Count = BattlePower;
			stroke.Choice = power;
			s_Queue.push_back(stroke);
		}
	} // namespace

	void ForgetPowers() {
		for (auto& side: s_ReadyAt) {
			side.fill(0);
		}
		s_ScanUntil = -1;
		s_PendingPower = -1;
	}

	void ApplyPower(const Stroke& stroke) {
		const int side = stroke.Team;
		const int power = stroke.Choice;
		if (side < 0 || side >= c_Sides || power < 0 || power >= c_PowerCount || !BattlePlayerCommands(side) || SecondsLeft(side, power) > 0.0F) {
			return;
		}
		const CommanderPowerInfo& info = c_CommanderPowers[power];
		Vector at = stroke.Position;
		g_SceneMan.WrapPosition(at);
		const long long now = g_TimerMan.GetSimUpdateCount();
		switch (static_cast<CommanderPower>(power)) {
			case CommanderPower::Artillery:
			case CommanderPower::Orbital:
			case CommanderPower::Smoke: {
				Stroke strike;
				strike.Kind = power == static_cast<int>(CommanderPower::Artillery) ? Tool::Artillery : (power == static_cast<int>(CommanderPower::Orbital) ? Tool::OrbitalBeam : Tool::SmokeBomb);
				strike.Position = at;
				strike.Team = side;
				Apply(strike);
				break;
			}
			case CommanderPower::Supply: {
				std::vector<Actor*> units = UnitsAround(side, at, info.Radius);
				for (Actor* unit: units) {
					unit->RemoveWounds(unit->GetWoundCount());
					unit->SetHealth(unit->GetMaxHealth());
					if (AHuman* human = dynamic_cast<AHuman*>(unit)) {
						human->ReloadFirearms();
					}
				}
				Note(std::string(c_SideNames[side]) + ": supplies to " + std::to_string(units.size()) + (units.size() == 1 ? " unit" : " units"));
				break;
			}
			case CommanderPower::Scan:
				s_ScanUntil = now + static_cast<long long>(c_ScanSeconds * UpdatesPerSecond());
				Note(std::string(c_SideNames[side]) + " scans the battlefield");
				break;
			case CommanderPower::Evac: {
				std::vector<Actor*> units = UnitsAround(side, at, info.Radius);
				if (units.empty()) {
					// (Nobody there: the power isn't spent.)
					Note("Nobody of yours there to pull out");
					return;
				}
				float refund = 0.0F;
				for (Actor* unit: units) {
					refund += unit->GetTotalValue(unit->GetModuleID(), 1.0F) * c_EvacRefund;
					EffectsParticles::Emit("Smoke", unit->GetPos(), Vector(0.0F, -2.0F), 1.0F, 8, 0);
					unit->SetToDelete(true);
				}
				s_CommandPoints[side].Points += refund;
				Note(std::string(c_SideNames[side]) + ": " + std::to_string(units.size()) + (units.size() == 1 ? " unit" : " units") + " pulled out");
				break;
			}
			default:
				return;
		}
		s_ReadyAt[side][power] = now + static_cast<long long>(info.Cooldown * UpdatesPerSecond());
	}

	void PowerTargetClicked(const Vector& position) {
		if (s_PendingPower >= 0) {
			UsePower(s_PendingPower);
			s_Queue.back().Position = position;
			s_PendingPower = -1;
		}
		s_ToolIndex = ToolIndex(Tool::Command);
		s_CommandMode = CommandMode::Select;
	}

	void PowerTiles() {
		const int side = std::clamp(CommandedTeam(), 0, c_Sides - 1);
		const bool running = BattlePlayerCommands(side);
		if (CurrentTool().Kind != Tool::PowerTarget) {
			s_PendingPower = -1;
		}
		static const Icon icons[] = {Icon::Bomb, Icon::Bolt, Icon::Cloud, Icon::Cross, Icon::Eye, Icon::Rocket};
		static_assert(std::size(icons) == c_PowerCount, "An icon for each power.");
		for (int power = 0; power < c_PowerCount; ++power) {
			const CommanderPowerInfo& info = c_CommanderPowers[power];
			const float left = SecondsLeft(side, power);
			const bool fogless = power == static_cast<int>(CommanderPower::Scan) && !s_ModeRun.Settings.FogOfWar;
			const bool ready = running && left <= 0.0F && !fogless;
			char seconds[16];
			std::snprintf(seconds, sizeof(seconds), "%.0f", std::ceil(left));
			char tip[400];
			std::snprintf(tip, sizeof(tip), "%s: %s\nThen %.0f seconds before it can be used again.%s%s", info.Name, info.Tip, info.Cooldown, info.Radius > 0.0F ? "\nClick, then click on the map where." : "",
			              !running ? "\nOnce the battle is on." : (fogless ? "\nOnly with fog of war on." : (left > 0.0F ? "\nCooling down." : "")));
			if (power > 0) {
				ImGui::SameLine();
			}
			ImGui::PushID(power + 900);
			const bool waiting = s_PendingPower == power;
			if (BarTile("##power", info.Name, tip, waiting, [&](ImDrawList* tileList, ImVec2 at, float room) {
				    DrawIcon(tileList, icons[power], at, room / 12.0F, ready ? IM_COL32(239, 160, 100, 255) : IM_COL32(150, 150, 150, 110));
				    if (left > 0.0F) {
					    PictureText(tileList, at, room, IM_COL32(255, 220, 120, 255), seconds);
				    }
			    },
			            ready ? 0 : IM_COL32(160, 160, 160, 160)) == 1 &&
			    ready) {
				if (info.Radius > 0.0F) {
					s_PendingPower = waiting ? -1 : power;
					s_ToolIndex = ToolIndex(waiting ? Tool::Command : Tool::PowerTarget);
				} else {
					UsePower(power);
				}
			}
			ImGui::PopID();
		}
	}

	void DrawPowerTarget() {
		if (CurrentTool().Kind != Tool::PowerTarget || s_PendingPower < 0 || ImGui::GetIO().WantCaptureMouse) {
			return;
		}
		const CommanderPowerInfo& info = c_CommanderPowers[s_PendingPower];
		const float radius = info.Radius / ScenePixelsPerWindowPixel();
		const ImVec2 at = ImGui::GetIO().MousePos;
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		drawList->AddCircleFilled(at, radius, IM_COL32(239, 106, 91, 40), 48);
		drawList->AddCircle(at, radius, IM_COL32(239, 106, 91, 200), 48, 2.0F);
		drawList->AddText(ImVec2(at.x + radius + 6.0F, at.y - ImGui::GetFontSize() * 0.5F), IM_COL32(239, 160, 100, 230), info.Name);
	}
} // namespace SandboxDetail
