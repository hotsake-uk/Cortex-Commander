// The Battle Director's modes: preset games played by its teams, set up from a few choices (how many a side, which teams, and a base
// drawn for each) rather than every team's card. Each mode is a row of c_Modes: its name, what it asks for, and its rules, which hook into the Battle
// Director as it buys and sends units (SandboxBattle.cpp). Capture the flag is the first.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		/// Sim updates in a second of game time.
		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		/// Units that come at a time to a team of so many: a quarter of it, from 1 to 10.
		int PerWave(const BattleModeSettings& settings) { return std::clamp((settings.TeamSize + 3) / 4, 1, 10); }

		/// Whether a team is in the mode's game: ticked, with its base drawn.
		bool TeamIn(const BattleModeSettings& settings, int side) { return side >= 0 && side < c_Sides && settings.Plays[side] && settings.Bases[side].size() >= 3; }

		/// How far across a base reaches either side of its middle, and where across its edges are.
		void BaseSpan(const std::vector<Vector>& base, float& left, float& right) {
			left = base.empty() ? 0.0F : base.front().m_X;
			right = left;
			for (const Vector& corner: base) {
				left = std::min(left, corner.m_X);
				right = std::max(right, corner.m_X);
			}
		}

		/// How far round its flag a team's guards stand: half its base across, from 60 to 400 px.
		float GuardRadius(const std::vector<Vector>& base) {
			float left = 0.0F;
			float right = 0.0F;
			BaseSpan(base, left, right);
			return std::clamp((right - left) * 0.5F, 60.0F, 400.0F);
		}

		/// Something that happened, said over the game for a few seconds and in the console.
		void Say(const std::string& note) {
			s_ModeRun.Note = note;
			s_ModeRun.NoteAt = g_TimerMan.GetSimUpdateCount();
			g_ConsoleMan.PrintString("BATTLE: " + note);
		}

		std::string SideName(int side) { return side >= 0 && side < c_Sides ? c_SideNames[side] : "?"; }

		/// The settings every mode's team plays by: its factions from its card, money without end, and up to the team size alive, appearing
		/// in its base (or coming in by ship over it).
		BattleSettings BaseTeam(const BattleModeSettings& settings, int side, const BattleSettings& card) {
			BattleSettings team;
			team.Factions = card.Factions;
			team.FavouritesOnly = card.FavouritesOnly;
			team.Craft = card.Craft;
			team.Active = TeamIn(settings, side);
			team.Style = BattleStyle::Attack;
			team.EndlessMoney = true;
			team.UnitLimit = std::clamp(settings.TeamSize, 1, 200);
			team.WaveSize = PerWave(settings);
			team.ZoneUnits = PerWave(settings);
			team.ZoneEverySeconds = 15;
			team.EverySeconds = 25;
			if (!team.Active) {
				return team;
			}
			const std::vector<Vector>& base = settings.Bases[side];
			if (settings.ByShip) {
				// Over the base, from one side of it to the other.
				float left = 0.0F;
				float right = 0.0F;
				BaseSpan(base, left, right);
				team.ShipsPerBurst = std::clamp(settings.TeamSize / 15 + 1, 1, 4);
				team.Invincible = true;
				team.DropOnLine = true;
				team.HasLine = true;
				team.LineA = Vector(left, base.front().m_Y);
				team.LineB = Vector(right, base.front().m_Y);
				g_SceneMan.WrapPosition(team.LineA);
				g_SceneMan.WrapPosition(team.LineB);
			} else {
				team.ShipsPerBurst = 0;
				team.SpawnZones.push_back(base);
			}
			return team;
		}

		/// A base drawn on the map: its area shaded in its team's colour.
		void DrawBase(ImDrawList* drawList, const std::vector<Vector>& base, ImU32 color) {
			if (base.size() < 3) {
				return;
			}
			std::vector<ImVec2> corners = ZoneOnScreen(base, std::max(ScenePixelsPerWindowPixel(), 0.01F));
			drawList->AddConcavePolyFilled(corners.data(), static_cast<int>(corners.size()), (color & 0x00FFFFFF) | (40u << 24));
			drawList->AddPolyline(corners.data(), static_cast<int>(corners.size()), (color & 0x00FFFFFF) | (200u << 24), ImDrawFlags_Closed, 2.0F);
		}

		/// Draws a flag on a pole standing at a place on screen, in a team's colour.
		void DrawFlag(ImDrawList* drawList, ImVec2 foot, ImU32 color, float size = 1.0F) {
			const float pole = 30.0F * size;
			drawList->AddLine(foot, ImVec2(foot.x, foot.y - pole), IM_COL32(230, 230, 230, 230), 2.0F);
			drawList->AddTriangleFilled(ImVec2(foot.x, foot.y - pole), ImVec2(foot.x + 18.0F * size, foot.y - pole + 6.0F * size), ImVec2(foot.x, foot.y - pole + 12.0F * size), color);
			drawList->AddTriangle(ImVec2(foot.x, foot.y - pole), ImVec2(foot.x + 18.0F * size, foot.y - pole + 6.0F * size), ImVec2(foot.x, foot.y - pole + 12.0F * size), IM_COL32(20, 20, 20, 200), 1.0F);
		}

		/// A line of text across the top of the picture of the game, on a dark plate.
		void Banner(const char* text, float fromTop, ImU32 color, float alpha) {
			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			ImVec2 size = ImGui::CalcTextSize(text);
			float scale = g_DebugMan.UsingPixelFont() ? 1.0F : 1.3F;
			GameViewRect view = g_DebugMan.GetUncoveredView();
			ImVec2 at(view.x + (view.w - size.x * scale) * 0.5F, view.y + fromTop);
			drawList->AddRectFilled(ImVec2(at.x - 10.0F, at.y - 4.0F), ImVec2(at.x + size.x * scale + 10.0F, at.y + size.y * scale + 4.0F), IM_COL32(0, 0, 0, static_cast<int>(150.0F * alpha)), 4.0F);
			drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, at, (color & 0x00FFFFFF) | (static_cast<ImU32>(255.0F * alpha) << 24), text);
		}

		/// The score of the mode's game, each team in its colour, across the top: with who has won, once someone has.
		void DrawScore(const char* title) {
			std::string line = title;
			for (int side = 0; side < c_Sides; ++side) {
				if (TeamIn(s_ModeRun.Settings, side)) {
					line += "    " + SideName(side) + " " + std::to_string(s_ModeRun.Score[side]);
				}
			}
			if (s_ModeRun.Settings.ScoreToWin > 0) {
				line += "    (first to " + std::to_string(s_ModeRun.Settings.ScoreToWin) + ")";
			}
			Banner(line.c_str(), 64.0F, IM_COL32(255, 255, 255, 255), 1.0F);
			if (s_ModeRun.Over && s_ModeRun.Winner >= 0) {
				Banner((SideName(s_ModeRun.Winner) + " wins!").c_str(), 92.0F, c_SideColors[s_ModeRun.Winner], 1.0F);
			} else if (s_ModeRun.NoteAt >= 0 && !s_ModeRun.Note.empty()) {
				// (Five seconds of game time, the last one fading.)
				float seconds = static_cast<float>(g_TimerMan.GetSimUpdateCount() - s_ModeRun.NoteAt) / UpdatesPerSecond();
				if (seconds < 5.0F) {
					Banner(s_ModeRun.Note.c_str(), 92.0F, IM_COL32(255, 210, 80, 255), std::clamp(5.0F - seconds, 0.0F, 1.0F));
				}
			}
		}

		// ---- Capture the flag ----

		/// Where a team's flag is.
		enum class FlagState {
			Home, //!< Standing at its base.
			Carried, //!< Taken by an enemy, on its way to that enemy's base.
			Dropped //!< Lying where its carrier fell, till a unit of its team touches it (back home), an enemy picks it up, or it goes home by itself.
		};

		struct Flag {
			FlagState State = FlagState::Home;
			Vector Home; //!< Where it stands in its base (moved when it can't be got to, with MoveStuckPoint).
			Vector Pos; //!< Where it is now: at home, its carrier's middle, or where it lies.
			UnitRef Carrier;
			long long DroppedAt = -1; //!< The sim update it was dropped on.
			int Unreachable = 0; //!< Checks in a row it was found out of an enemy's reach at home.
		};

		/// A capture the flag unit sent for a flag: an enemy's, to bring back to its own base.
		struct FlagRunner {
			int Team = 0;
			int Target = 0; //!< The team whose flag it goes for.
			Vector Sent; //!< Where it was last sent.
			bool HasSent = false;
			long long SentAt = 0;
			bool Seen = false; //!< Out in the world at least once: before that it is riding in its ship.
			long long Made = 0;
		};

		std::array<Flag, c_Sides> s_Flags;
		std::unordered_map<long, FlagRunner> s_Runners; //!< By unique ID.

		constexpr float c_FlagReach = 50.0F; //!< How near a unit's middle has to come to a flag to pick it up (or bring it home, or capture with it).
		constexpr float c_FlagReturnSeconds = 30.0F; //!< A dropped flag nobody touches goes home after this long.

		/// How far from its own flag a team's units appear, or as far as its base allows.
		constexpr float c_SpawnClear = 150.0F;

		void SetUpTeams();

		/// Somewhere in a team's base for a new unit to appear: never at its own flag. The first of a few picks at least c_SpawnClear from it,
		/// else the furthest of them (a base too small for that).
		Vector FlagsSpawnSpot(int side, const std::vector<Vector>& zone, float height) {
			const Vector flag = s_Flags[std::clamp(side, 0, c_Sides - 1)].Home;
			Vector best = SpotInZone(zone, height);
			float bestDistance = -1.0F;
			for (int attempt = 0; attempt < 16; ++attempt) {
				Vector spot = attempt == 0 ? best : SpotInZone(zone, height);
				float distance = g_SceneMan.ShortestDistance(spot, flag, g_SceneMan.SceneWrapsX()).GetMagnitude();
				if (distance >= c_SpawnClear) {
					return spot;
				}
				if (distance > bestDistance) {
					best = spot;
					bestDistance = distance;
				}
			}
			return best;
		}

		/// A capture the flag team's settings: as every mode's (BaseTeam), with its ships' drop line kept to the part of its base furthest from
		/// its flag, so they don't land their units on it either.
		BattleSettings FlagsTeam(const BattleModeSettings& settings, int side, const BattleSettings& card) {
			BattleSettings team = BaseTeam(settings, side, card);
			if (!team.Active || !team.HasLine) {
				return team;
			}
			float left = 0.0F;
			float right = 0.0F;
			BaseSpan(settings.Bases[side], left, right);
			const Vector& base = settings.Bases[side].front();
			// (The flag across, on the base's side of a wrap.)
			const float flagX = base.m_X + g_SceneMan.ShortestDistance(base, s_Flags[side].Home, g_SceneMan.SceneWrapsX()).m_X;
			const float clearLeft = flagX - c_SpawnClear;
			const float clearRight = flagX + c_SpawnClear;
			if (clearLeft - left >= right - clearRight && clearLeft > left) {
				team.LineA = Vector(left, base.m_Y);
				team.LineB = Vector(clearLeft, base.m_Y);
			} else if (clearRight < right) {
				team.LineA = Vector(clearRight, base.m_Y);
				team.LineB = Vector(right, base.m_Y);
			}
			g_SceneMan.WrapPosition(team.LineA);
			g_SceneMan.WrapPosition(team.LineB);
			return team;
		}

		/// A flag's carrier glows (Actor::SetHighlighted) while it has it, and stops when it hasn't.
		void SetCarrier(Flag& flag, Actor* carrier) {
			if (Actor* old = GetRef(flag.Carrier)) {
				old->SetHighlighted(false);
			}
			flag.Carrier = MakeRef(carrier);
			if (carrier) {
				carrier->SetHighlighted(true);
			}
		}

		/// Every carrier's glow off, and the flags with nobody carrying them (the game stopped or left).
		void ClearCarriers() {
			for (Flag& flag: s_Flags) {
				SetCarrier(flag, nullptr);
			}
			g_PostProcessMan.GetLightingSettings().HighlightUnits = false;
		}

		/// The ground under a place, for a flag dropped there to lie on.
		Vector Grounded(const Vector& at) {
			std::vector<Vector> spots = StandingSpots(at, 1);
			return spots.empty() ? at : spots.front();
		}

		/// A flag back at its base, and its team's guards back round it.
		void SendHome(int side) {
			Flag& flag = s_Flags[side];
			flag.State = FlagState::Home;
			flag.Pos = flag.Home;
			SetCarrier(flag, nullptr);
			flag.DroppedAt = -1;
			RecentreDefenders(side, flag.Home, false);
		}

		/// A team's flag stands somewhere new at home: its ships' drop line kept clear of it again (FlagsTeam).
		void FlagMoved(int side, const Vector& home) {
			s_Flags[side].Home = home;
			SendHome(side);
			SetUpTeams();
		}

		/// Where a team's flag stands at home: its point if placed in its base, else somewhere on the ground in its base.
		Vector FlagHome(const BattleModeSettings& settings, int side) {
			const std::vector<Vector>& base = settings.Bases[side];
			if (settings.HasPoint[side] && IsInZone(base, settings.Points[side])) {
				return settings.Points[side];
			}
			return base.size() >= 3 ? SpotInZone(base, 0.0F) : settings.Points[side];
		}

		/// The team whose flag a unit is carrying, or -1.
		int CarriedBy(const Actor* unit) {
			for (int side = 0; side < c_Sides; ++side) {
				if (s_Flags[side].State == FlagState::Carried && RefersTo(s_Flags[side].Carrier, unit)) {
					return side;
				}
			}
			return -1;
		}

		void FlagsStart() {
			ClearCarriers();
			s_Runners.clear();
			for (int side = 0; side < c_Sides; ++side) {
				s_Flags[side] = Flag();
				if (TeamIn(s_ModeRun.Settings, side)) {
					s_Flags[side].Home = FlagHome(s_ModeRun.Settings, side);
				}
				s_Flags[side].Pos = s_Flags[side].Home;
			}
		}

		/// The panel changed while the game is on: a flag placed somewhere new, or left outside its base when that was drawn again, moves there
		/// (or somewhere in the base), if it's at home.
		void FlagsSettingsChanged() {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			for (int side = 0; side < c_Sides; ++side) {
				Flag& flag = s_Flags[side];
				if (!TeamIn(settings, side)) {
					continue;
				}
				const bool placedElsewhere = settings.HasPoint[side] && IsInZone(settings.Bases[side], settings.Points[side]) && !g_SceneMan.ShortestDistance(flag.Home, settings.Points[side], g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(1.0F);
				if (placedElsewhere || !IsInZone(settings.Bases[side], flag.Home)) {
					flag.Home = FlagHome(settings, side);
					if (flag.State == FlagState::Home) {
						SendHome(side);
					}
					SetUpTeams();
				}
			}
		}

		/// A team's new units: some stay to guard its flag (defenders of its base, who go after it too while it's away), the rest go for an
		/// enemy's.
		void FlagsUnitsMade(int side, const std::vector<Actor*>& wave) {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			std::vector<int> enemies;
			for (int other = 0; other < c_Sides; ++other) {
				if (other != side && TeamIn(settings, other)) {
					enemies.push_back(other);
				}
			}
			BattleSettings guard;
			guard.DefendPos = s_Flags[side].State == FlagState::Home ? s_Flags[side].Home : s_Flags[side].Pos;
			guard.HasDefendPos = true;
			guard.DefendRadius = static_cast<int>(GuardRadius(settings.Bases[side]));
			guard.ChaseDistance = guard.DefendRadius + 250;
			guard.RoamPercent = 30;
			const long long now = g_TimerMan.GetSimUpdateCount();
			for (Actor* unit: wave) {
				if (enemies.empty() || Random01() * 100.0F < static_cast<float>(settings.GuardPercent)) {
					unit->SetOrderAttack(false);
					MakeDefender(unit, guard);
				} else {
					FlagRunner& runner = s_Runners[unit->GetUniqueID()];
					runner.Team = side;
					runner.Target = enemies[std::min(enemies.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(enemies.size())))];
					runner.Made = now;
				}
			}
		}

		/// Flags picked up, dropped, brought home and captured, and the game won.
		void UpdateFlags(long long now) {
			BattleModeSettings& settings = s_ModeRun.Settings;
			const bool wraps = g_SceneMan.SceneWrapsX();
			std::vector<Actor*> fighters;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (IsCombatant(actor) && !dynamic_cast<const ACraft*>(actor) && !actor->IsInGroup("Brains") && TeamIn(settings, actor->GetTeam())) {
					fighters.push_back(actor);
				}
			}
			for (int side = 0; side < c_Sides; ++side) {
				if (!TeamIn(settings, side)) {
					continue;
				}
				Flag& flag = s_Flags[side];
				if (flag.State == FlagState::Carried) {
					Actor* carrier = GetRef(flag.Carrier);
					if (!carrier || !IsCombatant(carrier)) {
						flag.State = FlagState::Dropped;
						flag.Pos = Grounded(flag.Pos);
						SetCarrier(flag, nullptr);
						flag.DroppedAt = now;
						RecentreDefenders(side, flag.Pos, true);
						Say(SideName(side) + "'s flag is down");
						continue;
					}
					flag.Pos = carrier->GetPos();
					const int team = carrier->GetTeam();
					const bool home = TeamIn(settings, team) && (IsInZone(settings.Bases[team], carrier->GetPos()) || g_SceneMan.ShortestDistance(carrier->GetPos(), s_Flags[team].Home, wraps).MagnitudeIsLessThan(c_FlagReach));
					if (home && s_Flags[team].State == FlagState::Home) {
						// Brought into its own base, with its own flag there: a capture.
						++s_ModeRun.Score[team];
						SendHome(side);
						if (settings.ScoreToWin > 0 && s_ModeRun.Score[team] >= settings.ScoreToWin) {
							s_ModeRun.Over = true;
							s_ModeRun.Winner = team;
							ClearCarriers();
							for (BattleTeam& battleTeam: s_BattleTeams) {
								battleTeam.Running = false;
							}
							Say(SideName(team) + " wins, " + std::to_string(s_ModeRun.Score[team]) + " captures");
							return;
						}
						Say(SideName(team) + " captured " + SideName(side) + "'s flag");
					}
					continue;
				}
				if (flag.State == FlagState::Dropped && static_cast<float>(now - flag.DroppedAt) > c_FlagReturnSeconds * UpdatesPerSecond()) {
					SendHome(side);
					Say(SideName(side) + "'s flag went back to its base");
					continue;
				}
				// Whoever is nearest it, if near enough: an enemy takes it, one of its own (when it is lying out) takes it home.
				Actor* nearest = nullptr;
				float nearestDistance = c_FlagReach * c_FlagReach;
				for (Actor* fighter: fighters) {
					if (flag.State == FlagState::Home && fighter->GetTeam() == side) {
						continue;
					}
					float distance = g_SceneMan.ShortestDistance(fighter->GetPos(), flag.Pos, wraps).GetSqrMagnitude();
					if (distance < nearestDistance) {
						nearest = fighter;
						nearestDistance = distance;
					}
				}
				if (!nearest) {
					continue;
				}
				if (nearest->GetTeam() == side) {
					SendHome(side);
					Say(SideName(side) + " took its flag back");
				} else if (CarriedBy(nearest) < 0) {
					flag.State = FlagState::Carried;
					SetCarrier(flag, nearest);
					flag.Pos = nearest->GetPos();
					Say(SideName(nearest->GetTeam()) + " has " + SideName(side) + "'s flag");
				}
			}
		}

		/// Whether a flag standing at a place is buried: ground where its cloth is.
		bool Buried(const Vector& at) {
			Vector cloth = at - Vector(0.0F, 6.0F);
			g_SceneMan.WrapPosition(cloth);
			return g_SceneMan.GetTerrMatter(cloth.GetFloorIntX(), cloth.GetFloorIntY()) != g_MaterialAir;
		}

		/// Whether a unit could walk (jump, dig, or break through, as it can) from where it is to a flag at a place.
		bool CanReach(const Actor* unit, const Vector& at) {
			Scene* scene = g_SceneMan.GetScene();
			if (!scene || !unit) {
				return true;
			}
			std::list<Vector> path;
			float cost = scene->CalculatePath(unit->GetPos(), at - Vector(0.0F, 10.0F), path, unit->EstimateJumpHeight(), unit->EstimateDigStrength(), static_cast<Activity::Teams>(unit->GetTeam()), unit->EstimateBreachStrength());
			return cost >= 0.0F && cost < 100000.0F;
		}

		/// Every five seconds, with MoveStuckPoint: a flag at home that is buried, has lost the ground under it, or can't be got to by an enemy
		/// twice running is moved somewhere else in its base that can (a dropped one buried is sent home).
		void UpdateStuckFlags() {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			for (int side = 0; side < c_Sides; ++side) {
				Flag& flag = s_Flags[side];
				if (!TeamIn(settings, side) || flag.State == FlagState::Carried) {
					continue;
				}
				if (flag.State == FlagState::Dropped) {
					if (Buried(flag.Pos)) {
						SendHome(side);
						Say(SideName(side) + "'s flag was buried: back to its base");
					}
					continue;
				}
				// An enemy's unit to try the way with (the nearest of any team after it).
				const Actor* enemy = nullptr;
				float nearest = 0.0F;
				for (Actor* actor: SandboxAccess::Actors()) {
					if (actor->GetTeam() == side || !TeamIn(settings, actor->GetTeam()) || !IsCombatant(actor) || dynamic_cast<const ACraft*>(actor) || actor->IsInGroup("Brains")) {
						continue;
					}
					float distance = g_SceneMan.ShortestDistance(actor->GetPos(), flag.Home, g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
					if (!enemy || distance < nearest) {
						enemy = actor;
						nearest = distance;
					}
				}
				// Ground blasted away under it: it falls onto what's below.
				Vector settled = Grounded(flag.Home);
				bool stuck = Buried(flag.Home) || !IsInZone(settings.Bases[side], settled);
				if (!stuck && !g_SceneMan.ShortestDistance(settled, flag.Home, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(2.0F)) {
					FlagMoved(side, settled);
				}
				if (!stuck && enemy) {
					flag.Unreachable = CanReach(enemy, flag.Home) ? 0 : flag.Unreachable + 1;
					stuck = flag.Unreachable >= 2;
				}
				if (!stuck) {
					continue;
				}
				for (int attempt = 0; attempt < 10; ++attempt) {
					Vector spot = SpotInZone(settings.Bases[side], 0.0F);
					if (!Buried(spot) && (!enemy || CanReach(enemy, spot))) {
						flag.Unreachable = 0;
						FlagMoved(side, spot);
						Say(SideName(side) + "'s flag couldn't be got to: moved in its base");
						break;
					}
				}
			}
		}

		/// Sends a runner somewhere, unless it is already on its way there (or near enough), so it isn't stopped and started every half second.
		void SendRunner(Actor* unit, FlagRunner& runner, const Vector& to, Actor* target, const char* reason, long long now) {
			const bool wraps = g_SceneMan.SceneWrapsX();
			const bool moved = !runner.HasSent || !g_SceneMan.ShortestDistance(runner.Sent, to, wraps).MagnitudeIsLessThan(target ? 120.0F : 40.0F);
			// (Not straight after being sent: the order is only taken up on the next update, and one with no way there drops it again.)
			const bool stopped = unit->GetAIMode() != Actor::AIMODE_GOTO && now - runner.SentAt > static_cast<long long>(3.0F * UpdatesPerSecond()) && !g_SceneMan.ShortestDistance(unit->GetPos(), to, wraps).MagnitudeIsLessThan(c_FlagReach);
			// (And again every few seconds while it chases someone, as they move.)
			const bool stale = target && now - runner.SentAt > static_cast<long long>(4.0F * UpdatesPerSecond());
			if (!moved && !stopped && !stale) {
				return;
			}
			runner.Sent = to;
			runner.HasSent = true;
			runner.SentAt = now;
			SendUnit(unit, to, target, target != nullptr, reason, false, true);
		}

		/// Each runner, every half second: a carrier makes for its own base, the rest for the flag they're after (or with whoever of theirs is
		/// carrying it, to see it home), and any near an enemy carrying their own team's flag go after that one. Each team's guards follow its
		/// flag while it is away from its base.
		void UpdateRunners(long long now) {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			const bool wraps = g_SceneMan.SceneWrapsX();
			for (int side = 0; side < c_Sides; ++side) {
				if (TeamIn(settings, side) && s_Flags[side].State == FlagState::Carried) {
					RecentreDefenders(side, s_Flags[side].Pos, false);
				}
			}
			std::unordered_map<long, Actor*> byID;
			for (Actor* actor: SandboxAccess::Actors()) {
				byID[actor->GetUniqueID()] = actor;
			}
			const long long riding = static_cast<long long>(60.0F * UpdatesPerSecond());
			for (auto entry = s_Runners.begin(); entry != s_Runners.end();) {
				FlagRunner& runner = entry->second;
				auto found = byID.find(entry->first);
				Actor* unit = found == byID.end() ? nullptr : found->second;
				if (!unit) {
					// Still riding in its ship (for a minute at most), or gone.
					entry = runner.Seen || now - runner.Made > riding ? s_Runners.erase(entry) : std::next(entry);
					continue;
				}
				runner.Seen = true;
				if (!IsCombatant(unit)) {
					entry = s_Runners.erase(entry);
					continue;
				}
				if (unit->IsPlayerControlled() || !TeamIn(settings, runner.Target)) {
					++entry;
					continue;
				}
				const int team = runner.Team;
				if (CarriedBy(unit) >= 0) {
					SendRunner(unit, runner, s_Flags[team].Home, nullptr, "flag: taking it home", now);
					++entry;
					continue;
				}
				// An enemy carrying our flag, near enough to go after.
				const Flag& ours = s_Flags[team];
				if (ours.State == FlagState::Carried) {
					Actor* thief = GetRef(ours.Carrier);
					if (thief && g_SceneMan.ShortestDistance(unit->GetPos(), thief->GetPos(), wraps).MagnitudeIsLessThan(500.0F)) {
						SendRunner(unit, runner, thief->GetPos(), thief, "flag: after the one with ours", now);
						++entry;
						continue;
					}
				}
				const Flag& theirs = s_Flags[runner.Target];
				Actor* carrier = theirs.State == FlagState::Carried ? GetRef(theirs.Carrier) : nullptr;
				if (carrier && carrier->GetTeam() == team) {
					SendRunner(unit, runner, carrier->GetPos(), nullptr, "flag: seeing it home", now);
				} else if (carrier) {
					// (Another team has it: after them.)
					SendRunner(unit, runner, carrier->GetPos(), carrier, "flag: after the one with it", now);
				} else {
					SendRunner(unit, runner, theirs.Pos, nullptr, "flag: going for it", now);
				}
				++entry;
			}
		}

		void FlagsUpdate(bool aiPaused) {
			const long long now = g_TimerMan.GetSimUpdateCount();
			if (aiPaused) {
				// (The clock a dropped flag goes home by held back too.)
				for (Flag& flag: s_Flags) {
					if (flag.State == FlagState::Dropped) {
						++flag.DroppedAt;
					}
				}
				return;
			}
			// A carried flag kept over its carrier every update, so it doesn't trail behind it on the map.
			for (Flag& flag: s_Flags) {
				if (Actor* carrier = flag.State == FlagState::Carried ? GetRef(flag.Carrier) : nullptr) {
					flag.Pos = carrier->GetPos();
				}
			}
			// (The outline pass that makes them glow runs only while someone has a flag.)
			g_PostProcessMan.GetLightingSettings().HighlightUnits = std::any_of(s_Flags.begin(), s_Flags.end(), [](const Flag& flag) { return flag.State == FlagState::Carried && GetRef(flag.Carrier); });
			if (s_ModeRun.Over) {
				return;
			}
			if (now % 6 == 0) {
				UpdateFlags(now);
			}
			if (s_ModeRun.Settings.MoveStuckPoint && !s_ModeRun.Over && now % 300 == 150) {
				UpdateStuckFlags();
			}
			if (!s_ModeRun.Over && now % 30 == 15) {
				UpdateRunners(now);
			}
		}

		void FlagsPanel(BattleModeSettings& setup, bool& changed) {
			changed |= ImGui::SliderInt("Captures to win", &setup.ScoreToWin, 0, 10, setup.ScoreToWin > 0 ? "%d" : "play on");
			ImGui::SetItemTooltip("The first team to bring this many enemy flags home to its own wins, and the battle stops. 0: it goes on till you stop it.");
			changed |= ImGui::SliderInt("Guards", &setup.GuardPercent, 0, 90, "%d%% of each team");
			ImGui::SetItemTooltip("The share of each team's units that stay to guard its flag, and go after it if it's taken. The rest go for the enemy's.");
			changed |= ToolUI::Checkbox("Move a flag nobody can get to", &setup.MoveStuckPoint);
			ImGui::SetItemTooltip("A flag that gets buried, loses the ground under it, or that the enemy can find no way to, moves somewhere else in its base they can get to. Off: it stays where it is.");
		}

		void FlagsDraw(bool running) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();
			const BattleModeSettings& settings = running ? s_ModeRun.Settings : s_ModeSetup;
			for (int side = 0; side < c_Sides; ++side) {
				if (!settings.Plays[side]) {
					continue;
				}
				const ImU32 color = c_SideColors[side];
				DrawBase(drawList, settings.Bases[side], color);
				if (!running) {
					if (settings.HasPoint[side]) {
						DrawFlag(drawList, ToScreen(settings.Points[side]), color);
					}
					continue;
				}
				if (!TeamIn(settings, side)) {
					continue;
				}
				const Flag& flag = s_Flags[side];
				if (flag.State != FlagState::Home) {
					// Where it stands at home, empty.
					ImVec2 home = ToScreen(flag.Home);
					drawList->AddCircle(ImVec2(home.x, home.y - 6.0F), 12.0F, (color & 0x00FFFFFF) | (90u << 24), 24, 2.0F);
				}
				if (flag.State == FlagState::Carried) {
					// Over its carrier's head (its middle, and a bit).
					ImVec2 over = ToScreen(flag.Pos);
					DrawFlag(drawList, ImVec2(over.x, over.y - 18.0F), color, 0.8F);
				} else {
					DrawFlag(drawList, ToScreen(flag.Pos), color);
				}
			}
			if (CurrentTool().Kind == Tool::BattleModeBase) {
				DrawZoneDraft(drawList, std::max(ScenePixelsPerWindowPixel(), 0.01F));
			}
			if (running) {
				DrawScore("CAPTURE THE FLAG");
			}
		}

		/// One of the Battle Director's modes: what it is called and asks for, and its rules. Any of the rules can be left out.
		struct BattleModeInfo {
			const char* Name;
			const char* Blurb; //!< What the game is, for the top of its panel.
			const char* PointName; //!< What each team's point in its base is to it, as "flag"; nullptr for none.
			int MinTeams;
			/// The settings a team in the game plays by (BaseTeam, unless the mode wants otherwise), from its card's.
			BattleSettings (*TeamSettings)(const BattleModeSettings& settings, int side, const BattleSettings& card);
			void (*Start)(); //!< Its own state afresh, as the game starts.
			void (*SettingsChanged)(); //!< The panel changed while the game is on.
			void (*UnitsMade)(int side, const std::vector<Actor*>& wave); //!< A team's new units, bought but not yet in (riding in, or about to appear).
			Vector (*SpawnSpot)(int side, const std::vector<Vector>& zone, float height); //!< Where in a spawn zone a team's new unit appears (SpotInZone, if left out).
			void (*Update)(bool aiPaused); //!< Each sim update, while its game is on.
			void (*Panel)(BattleModeSettings& setup, bool& changed); //!< Its own choices, on the Battle tab.
			void (*Draw)(bool running); //!< On the map: its bases and points while set up (running false), and its game while on.
		};

		constexpr BattleModeInfo c_Modes[] = {
		    {"Custom", "", nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
		    {"Capture the flag",
		     "Each team has a base, drawn on the map, that its units appear in (away from its flag), with its flag inside. Its units go for the enemy's flag and "
		     "bring it back into their own base, while some stay to guard theirs. A flag can only be captured while the team's own is at home. "
		     "A carrier glows, and if it falls it drops the flag: an enemy can pick it up, or one of its own team touch it to send it home (it "
		     "goes home by itself after 30 seconds).",
		     "flag", 2, FlagsTeam, FlagsStart, FlagsSettingsChanged, FlagsUnitsMade, FlagsSpawnSpot, FlagsUpdate, FlagsPanel, FlagsDraw},
		};
		static_assert(std::size(c_Modes) == static_cast<size_t>(BattleMode::Count), "c_Modes must describe each BattleMode.");

		const BattleModeInfo& ModeOf(BattleMode mode) { return c_Modes[std::clamp(static_cast<int>(mode), 0, static_cast<int>(BattleMode::Count) - 1)]; }

		/// Whether the mode's game can be started as set up, and if not, why.
		bool ModeReady(const BattleModeSettings& setup, std::string& why) {
			const BattleModeInfo& mode = ModeOf(setup.Mode);
			int ticked = 0;
			for (int side = 0; side < c_Sides; ++side) {
				if (!setup.Plays[side]) {
					continue;
				}
				++ticked;
				if (setup.Bases[side].size() < 3) {
					why = std::string(c_SideNames[side]) + "'s base isn't drawn yet.";
					return false;
				}
			}
			if (ticked < mode.MinTeams) {
				why = "Tick at least " + std::to_string(mode.MinTeams) + " teams.";
				return false;
			}
			return true;
		}

		/// Every team set up for the mode's game, from its card as it now is in the sim.
		void SetUpTeams() {
			const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode);
			if (!mode.TeamSettings) {
				return;
			}
			for (int side = 0; side < c_Sides; ++side) {
				s_BattleTeams[side].Settings = mode.TeamSettings(s_ModeRun.Settings, side, s_BattleTeams[side].Settings);
			}
		}

		void StopMode() {
			ClearCarriers();
			s_ModeRun.Running = false;
			s_ModeRun.Over = false;
			for (BattleTeam& team: s_BattleTeams) {
				team.Running = false;
			}
		}
		std::array<std::vector<Vector>, c_Sides> s_ScriptBaseDrafts; //!< The corners of a base a script is putting down, one SandboxDo at a time.

		/// The team being set up has its base drawn: sent, with the flag tool given to it next (to place its flag in it).
		void BaseDrawn(const std::vector<Vector>& base) {
			const int team = std::clamp(s_BattleEditTeam, 0, c_Sides - 1);
			s_ModeSetup.Bases[team] = base;
			if (s_ModeSetup.HasPoint[team] && !IsInZone(base, s_ModeSetup.Points[team])) {
				s_ModeSetup.HasPoint[team] = false;
			}
			SendBattleMode();
			TakeBattleTool(Tool::BattleModePoint, team);
		}
	} // namespace

	/// The next corner of the base being drawn (s_ZoneDraft), or the base closed, with one on its first corner: true then.
	bool ModeBaseCorner(const Vector& position, float closeWithin) {
		BattleSettings made;
		if (!AddZoneCorner(s_ZoneDraft, made, position, closeWithin) || made.SpawnZones.empty()) {
			return false;
		}
		BaseDrawn(made.SpawnZones.front());
		return true;
	}

	/// The base being drawn closed (Enter), if it has three corners or more.
	bool CloseModeBase() {
		BattleSettings made;
		if (!CloseSpawnZone(s_ZoneDraft, made) || made.SpawnZones.empty()) {
			return false;
		}
		BaseDrawn(made.SpawnZones.front());
		return true;
	}

	/// Sends the mode panel to the sim, as it now stands, with a start or stop if asked for.
	void SendBattleMode(int command) {
		Stroke stroke;
		stroke.Kind = Tool::BattleTeam;
		stroke.Team = -1;
		stroke.Count = command;
		stroke.Mode = s_ModeSetup;
		s_Queue.push_back(stroke);
	}

	/// A change made on the mode panel (or a team's point or base set by a script, with their tools), in the sim.
	void ApplyBattleMode(const Stroke& stroke) {
		if (stroke.Kind == Tool::BattleModeBase) {
			// From a script's SandboxDo: one corner a call, closed with one on its first corner.
			if (stroke.Team >= 0 && stroke.Team < c_Sides) {
				BattleSettings made;
				if (AddZoneCorner(s_ScriptBaseDrafts[stroke.Team], made, stroke.Position, 20.0F) && !made.SpawnZones.empty()) {
					s_ModeRun.Settings.Bases[stroke.Team] = made.SpawnZones.front();
					s_ModeSetup.Bases[stroke.Team] = made.SpawnZones.front();
				}
			}
		} else if (stroke.Kind == Tool::BattleModePoint) {
			// From a script's SandboxDo: the window sends the whole settings instead.
			if (stroke.Team >= 0 && stroke.Team < c_Sides) {
				Vector at = stroke.Position;
				g_SceneMan.WrapPosition(at);
				s_ModeRun.Settings.Points[stroke.Team] = at;
				s_ModeRun.Settings.HasPoint[stroke.Team] = true;
				s_ModeSetup.Points[stroke.Team] = at;
				s_ModeSetup.HasPoint[stroke.Team] = true;
			}
		} else {
			const bool sameMode = stroke.Mode.Mode == s_ModeRun.Settings.Mode;
			s_ModeRun.Settings = stroke.Mode;
			if (s_ModeRun.Running && !sameMode) {
				// (Another mode chosen while one's game is on: that game is over.)
				StopMode();
			}
		}
		const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode);
		if (stroke.Count == BattleModeStart && s_ModeRun.Settings.Mode != BattleMode::Custom) {
			if (!s_CatalogueBuilt) {
				BuildCatalogue();
			}
			s_ModeRun.Running = true;
			s_ModeRun.Over = false;
			s_ModeRun.Winner = -1;
			s_ModeRun.Score.fill(0);
			s_ModeRun.Note.clear();
			s_ModeRun.NoteAt = -1;
			s_BattleDefenders.clear();
			if (mode.Start) {
				mode.Start();
			}
			SetUpTeams();
			// Every team in the game starts afresh, a second apart, and the rest are stopped.
			long long delay = 0;
			for (int side = 0; side < c_Sides; ++side) {
				BattleTeam& team = s_BattleTeams[side];
				team.Running = team.Settings.Active;
				team.Spent = 0.0F;
				team.Sent = 0;
				team.Broke = false;
				team.NextWave = g_TimerMan.GetSimUpdateCount() + delay;
				team.NextZoneWave = team.NextWave;
				delay += team.Running ? 60 : 0;
			}
			Say(std::string(mode.Name) + ": go!");
		} else if (stroke.Count == BattleModeStop) {
			StopMode();
		} else if (s_ModeRun.Running) {
			SetUpTeams();
			if (mode.SettingsChanged) {
				mode.SettingsChanged();
			}
		}
	}

	/// The settings a team plays by while the mode's game is on, from its card's.
	BattleSettings ModeTeamSettings(int side, const BattleSettings& card) {
		const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode);
		return mode.TeamSettings ? mode.TeamSettings(s_ModeRun.Settings, side, card) : card;
	}

	void ModeUnitsMade(int side, const std::vector<Actor*>& wave) {
		if (const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode); s_ModeRun.Running && mode.UnitsMade) {
			mode.UnitsMade(side, wave);
		}
	}

	Vector ModeSpawnSpot(int side, const std::vector<Vector>& zone, float height) {
		const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode);
		return mode.SpawnSpot ? mode.SpawnSpot(side, zone, height) : SpotInZone(zone, height);
	}

	void UpdateBattleMode(bool aiPaused) {
		if (const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode); s_ModeRun.Running && mode.Update) {
			mode.Update(aiPaused);
		}
	}

	/// A new game: no mode's game is on, and the points set (on the last game's scene) are gone. The mode chosen stays.
	void ForgetBattleMode() {
		ClearCarriers();
		const BattleMode chosen = s_ModeSetup.Mode;
		s_ModeRun = BattleModeRun();
		s_ModeSetup.HasPoint.fill(false);
		for (int side = 0; side < c_Sides; ++side) {
			s_ModeSetup.Bases[side].clear();
			s_ScriptBaseDrafts[side].clear();
		}
		s_ModeRun.Settings = s_ModeSetup;
		s_ModeRun.Settings.Mode = chosen;
		s_Runners.clear();
		s_Flags = {};
	}

	/// The mode list at the top of the Battle tab. Whether a mode (not the cards) is chosen.
	bool BattleModeChooser() {
		int mode = static_cast<int>(s_ModeSetup.Mode);
		const char* names[static_cast<int>(BattleMode::Count)];
		for (int i = 0; i < static_cast<int>(BattleMode::Count); ++i) {
			names[i] = c_Modes[i].Name;
		}
		if (ImGui::Combo("Mode", &mode, names, static_cast<int>(BattleMode::Count))) {
			if (IsBattleTool(CurrentTool().Kind)) {
				PutDownBattleTool();
			}
			s_ModeSetup.Mode = static_cast<BattleMode>(mode);
			SendBattleMode();
		}
		ImGui::SetItemTooltip("Custom: set each team up on its card. Or a preset game, where you only pick how many a side and draw each team's base.");
		return s_ModeSetup.Mode != BattleMode::Custom;
	}

	/// The Battle tab for a mode: its team size, the teams in it with their bases, points and factions, its own choices, and the game started
	/// or stopped.
	void BattleModeTab() {
		BattleModeSettings& setup = s_ModeSetup;
		const BattleModeInfo& mode = ModeOf(setup.Mode);
		const bool running = s_ModeRun.Running && s_ModeRun.Settings.Mode == setup.Mode;
		bool changed = false;
		ImGui::TextWrapped("%s", mode.Blurb);

		std::string why;
		const bool ready = ModeReady(setup, why);
		float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
		ImGui::BeginDisabled(!ready);
		if (ToolUI::Button(running ? "Start again" : "Start", ImVec2(half, 0.0F))) {
			if (IsBattleTool(CurrentTool().Kind)) {
				PutDownBattleTool();
			}
			// (The cards first: the teams' factions come from them.)
			for (int side = 0; side < c_Sides; ++side) {
				SendBattleSettings(side);
			}
			SendBattleMode(BattleModeStart);
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", ready ? "The game starts afresh: scores back to nothing, every flag at home in its base." : why.c_str());
		ImGui::SameLine();
		ImGui::BeginDisabled(!running);
		if (ToolUI::Button("Stop", ImVec2(-1.0F, 0.0F))) {
			SendBattleMode(BattleModeStop);
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("The game ends, and no more units come. Those already in stay.");
		if (!ready) {
			ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.4F, 1.0F), "%s", why.c_str());
		}

		changed |= ImGui::SliderInt("Team size", &setup.TeamSize, 2, 100, "%d alive at most");
		ImGui::SetItemTooltip("Most units each team has alive at once. Fallen ones are replaced, %d at a time.", PerWave(setup));
		if (ToolUI::RadioButton("Appear in their base##arrive", !setup.ByShip)) {
			setup.ByShip = false;
			changed = true;
		}
		ImGui::SameLine();
		if (ToolUI::RadioButton("Come in by ship##arrive", setup.ByShip)) {
			setup.ByShip = true;
			changed = true;
		}
		ImGui::SetItemTooltip("Each team's units come in by ship over their base (each team's card says which craft), rather than appearing in it.");
		if (mode.Panel) {
			mode.Panel(setup, changed);
		}

		ImGui::SeparatorText("Teams");
		const std::string point = mode.PointName ? mode.PointName : "base";
		for (int side = 0; side < c_Sides; ++side) {
			ImGui::PushID(side);
			ImGui::BeginDisabled(running);
			ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
			changed |= ToolUI::Checkbox(c_SideNames[side], &setup.Plays[side]);
			ImGui::PopStyleColor();
			ImGui::EndDisabled();
			if (running) {
				ImGui::SetItemTooltip("Stop the game to change who plays.");
			}
			if (setup.Plays[side]) {
				ImGui::SameLine();
				const bool drawing = CurrentTool().Kind == Tool::BattleModeBase && s_BattleEditTeam == side;
				if (ToolUI::Button(drawing ? "Done##base" : (setup.Bases[side].empty() ? "Draw base##base" : "Redraw base##base"))) {
					if (drawing) {
						PutDownBattleTool();
					} else {
						TakeBattleTool(Tool::BattleModeBase, side);
					}
				}
				ImGui::SetItemTooltip("%s", drawing ? "Click the corners of the base on the map, then click the first corner again (or press Enter) to close it. Backspace takes back the last corner." : "Then click out the corners of this team's base on the map: its units appear in it.");
				if (mode.PointName && !setup.Bases[side].empty()) {
					ImGui::SameLine();
					const bool placing = CurrentTool().Kind == Tool::BattleModePoint && s_BattleEditTeam == side;
					std::string label = placing ? "Done (Enter)##point" : (setup.HasPoint[side] ? "Move " : "Place ") + point + "##point";
					if (ToolUI::Button(label.c_str())) {
						if (placing) {
							PutDownBattleTool();
						} else {
							TakeBattleTool(Tool::BattleModePoint, side);
						}
					}
					ImGui::SetItemTooltip("%s", placing ? "Click inside the base to put it there; Enter (or this) when it's where you want it." : ("Then click inside the base where this team's " + point + " is to stand. Not placed: somewhere in the base.").c_str());
				}
				ImGui::SameLine();
				if (running) {
					ImGui::TextDisabled("%d captures, %d in", s_ModeRun.Score[side], Sandbox::CountUnits(side));
				} else if (setup.Bases[side].empty()) {
					ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.4F, 1.0F), "no base");
				} else if (mode.PointName && !setup.HasPoint[side]) {
					ImGui::TextDisabled("%s anywhere in it", point.c_str());
				} else {
					ImGui::TextDisabled("ready");
				}
				ImGui::Indent();
				if (FactionPicker(s_BattleSetup[side])) {
					SendBattleSettings(side);
				}
				ImGui::Unindent();
			}
			ImGui::PopID();
		}
		if (changed) {
			SendBattleMode();
		}
	}

	/// On the map: the mode's bases and points while it is set up (with the Battle tab showing, or one of its tools in hand), and its game while on, with
	/// the window open or not.
	void DrawBattleMode() {
		const BattleModeInfo& mode = ModeOf(s_ModeSetup.Mode);
		if (!mode.Draw) {
			return;
		}
		if (s_ModeRun.Running && s_ModeRun.Settings.Mode == s_ModeSetup.Mode) {
			mode.Draw(true);
		} else if ((Sandbox::IsOpen() && s_CurrentTab == "Battle") || CurrentTool().Kind == Tool::BattleModePoint || CurrentTool().Kind == Tool::BattleModeBase) {
			mode.Draw(false);
		}
	}
} // namespace SandboxDetail
