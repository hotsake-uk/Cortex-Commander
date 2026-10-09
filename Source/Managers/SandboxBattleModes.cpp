// The Battle Director's modes: preset games played by its teams, set up from a few choices (how big, which teams, and a point for each)
// rather than every team's card. Each mode is a row of c_Modes: its name, what it asks for, and its rules, which hook into the Battle
// Director as it buys and sends units (SandboxBattle.cpp). Capture the flag is the first.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		/// Sim updates in a second of game time.
		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		/// What a mode's size makes of each team: most units it has in at once, how many come at a time and how often, and how far round its
		/// base its guards stand and chase.
		struct SizeInfo {
			int Units;
			int PerWave;
			int EverySeconds;
			int Ships;
			int GuardRadius;
			int GuardChase;
		};
		constexpr SizeInfo c_Sizes[] = {{8, 2, 20, 1, 120, 250}, {16, 3, 20, 1, 160, 350}, {28, 4, 25, 2, 200, 450}, {45, 6, 25, 3, 260, 550}};
		static_assert(std::size(c_Sizes) == static_cast<size_t>(BattleSize::Count), "c_Sizes must have a row for each BattleSize.");

		const SizeInfo& SizeOf(const BattleModeSettings& settings) { return c_Sizes[std::clamp(static_cast<int>(settings.Size), 0, static_cast<int>(BattleSize::Count) - 1)]; }

		/// Whether a team is in the mode's game: ticked, with its point set.
		bool TeamIn(const BattleModeSettings& settings, int side) { return side >= 0 && side < c_Sides && settings.Plays[side] && settings.HasPoint[side]; }

		/// Something that happened, said over the game for a few seconds and in the console.
		void Say(const std::string& note) {
			s_ModeRun.Note = note;
			s_ModeRun.NoteAt = g_TimerMan.GetSimUpdateCount();
			g_ConsoleMan.PrintString("BATTLE: " + note);
		}

		std::string SideName(int side) { return side >= 0 && side < c_Sides ? c_SideNames[side] : "?"; }

		/// The settings every mode's team plays by: its factions from its card, money without end, and as many units as the size says,
		/// appearing at its base (or coming in by ship over it).
		BattleSettings BaseTeam(const BattleModeSettings& settings, int side, const BattleSettings& card) {
			const SizeInfo& size = SizeOf(settings);
			BattleSettings team;
			team.Factions = card.Factions;
			team.FavouritesOnly = card.FavouritesOnly;
			team.Craft = card.Craft;
			team.Active = TeamIn(settings, side);
			team.Style = BattleStyle::Attack;
			team.EndlessMoney = true;
			team.UnitLimit = size.Units;
			team.WaveSize = size.PerWave;
			team.ZoneUnits = size.PerWave;
			team.ZoneEverySeconds = size.EverySeconds;
			team.EverySeconds = size.EverySeconds + 10;
			if (!team.Active) {
				return team;
			}
			const Vector base = settings.Points[side];
			if (settings.ByShip) {
				team.ShipsPerBurst = size.Ships;
				team.Invincible = true;
				team.DropOnLine = true;
				team.HasLine = true;
				team.LineA = base - Vector(150.0F, 0.0F);
				team.LineB = base + Vector(150.0F, 0.0F);
				g_SceneMan.WrapPosition(team.LineA);
				g_SceneMan.WrapPosition(team.LineB);
			} else {
				team.ShipsPerBurst = 0;
				team.SpawnZones.push_back(base);
			}
			return team;
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
			Vector Home; //!< Its base.
			Vector Pos; //!< Where it is now: at its base, its carrier's middle, or where it lies.
			UnitRef Carrier;
			long long DroppedAt = -1; //!< The sim update it was dropped on.
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
			flag.Carrier = UnitRef();
			flag.DroppedAt = -1;
			RecentreDefenders(side, flag.Home, false);
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
			s_Runners.clear();
			for (int side = 0; side < c_Sides; ++side) {
				s_Flags[side] = Flag();
				s_Flags[side].Home = s_ModeRun.Settings.Points[side];
				s_Flags[side].Pos = s_Flags[side].Home;
			}
		}

		/// The base moved on the panel while the game is on: a flag standing there goes with it.
		void FlagsSettingsChanged() {
			for (int side = 0; side < c_Sides; ++side) {
				Flag& flag = s_Flags[side];
				const Vector home = s_ModeRun.Settings.Points[side];
				if (!g_SceneMan.ShortestDistance(flag.Home, home, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(1.0F)) {
					flag.Home = home;
					if (flag.State == FlagState::Home) {
						SendHome(side);
					}
				}
			}
		}

		/// A team's new units: some stay to guard its flag (defenders of its base, who go after it too while it's away), the rest go for an
		/// enemy's.
		void FlagsUnitsMade(int side, const std::vector<Actor*>& wave) {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			const SizeInfo& size = SizeOf(settings);
			std::vector<int> enemies;
			for (int other = 0; other < c_Sides; ++other) {
				if (other != side && TeamIn(settings, other)) {
					enemies.push_back(other);
				}
			}
			BattleSettings guard;
			guard.DefendPos = s_Flags[side].State == FlagState::Home ? s_Flags[side].Home : s_Flags[side].Pos;
			guard.HasDefendPos = true;
			guard.DefendRadius = size.GuardRadius;
			guard.ChaseDistance = size.GuardChase;
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
						flag.Carrier = UnitRef();
						flag.DroppedAt = now;
						RecentreDefenders(side, flag.Pos, true);
						Say(SideName(side) + "'s flag is down");
						continue;
					}
					flag.Pos = carrier->GetPos();
					const int team = carrier->GetTeam();
					if (TeamIn(settings, team) && s_Flags[team].State == FlagState::Home && g_SceneMan.ShortestDistance(carrier->GetPos(), s_Flags[team].Home, wraps).MagnitudeIsLessThan(c_FlagReach)) {
						// Brought home, with its own flag there: a capture.
						++s_ModeRun.Score[team];
						SendHome(side);
						if (settings.ScoreToWin > 0 && s_ModeRun.Score[team] >= settings.ScoreToWin) {
							s_ModeRun.Over = true;
							s_ModeRun.Winner = team;
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
					flag.Carrier = MakeRef(nearest);
					flag.Pos = nearest->GetPos();
					Say(SideName(nearest->GetTeam()) + " has " + SideName(side) + "'s flag");
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
			if (s_ModeRun.Over) {
				return;
			}
			if (now % 6 == 0) {
				UpdateFlags(now);
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
		}

		void FlagsDraw(bool running) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();
			const BattleModeSettings& settings = running ? s_ModeRun.Settings : s_ModeSetup;
			for (int side = 0; side < c_Sides; ++side) {
				if (!settings.Plays[side] || !settings.HasPoint[side]) {
					continue;
				}
				const ImU32 color = c_SideColors[side];
				const ImU32 faint = (color & 0x00FFFFFF) | (90u << 24);
				if (!running) {
					DrawFlag(drawList, ToScreen(settings.Points[side]), color);
					continue;
				}
				const Flag& flag = s_Flags[side];
				ImVec2 home = ToScreen(flag.Home);
				if (flag.State != FlagState::Home) {
					// Its base, empty.
					drawList->AddCircle(ImVec2(home.x, home.y - 6.0F), 12.0F, faint, 24, 2.0F);
				}
				if (flag.State == FlagState::Carried) {
					// Over its carrier's head (its middle, and a bit).
					ImVec2 over = ToScreen(flag.Pos);
					DrawFlag(drawList, ImVec2(over.x, over.y - 18.0F), color, 0.8F);
				} else {
					DrawFlag(drawList, ToScreen(flag.Pos), color);
				}
			}
			if (running) {
				DrawScore("CAPTURE THE FLAG");
			}
		}

		/// One of the Battle Director's modes: what it is called and asks for, and its rules. Any of the rules can be left out.
		struct BattleModeInfo {
			const char* Name;
			const char* Blurb; //!< What the game is, for the top of its panel.
			const char* PointName; //!< What each team's point is to it (its base), as "flag".
			int MinTeams;
			/// The settings a team in the game plays by (BaseTeam, unless the mode wants otherwise), from its card's.
			BattleSettings (*TeamSettings)(const BattleModeSettings& settings, int side, const BattleSettings& card);
			void (*Start)(); //!< Its own state afresh, as the game starts.
			void (*SettingsChanged)(); //!< The panel changed while the game is on.
			void (*UnitsMade)(int side, const std::vector<Actor*>& wave); //!< A team's new units, bought but not yet in (riding in, or about to appear).
			void (*Update)(bool aiPaused); //!< Each sim update, while its game is on.
			void (*Panel)(BattleModeSettings& setup, bool& changed); //!< Its own choices, on the Battle tab.
			void (*Draw)(bool running); //!< On the map: its points while set up (running false), and its game while on.
		};

		constexpr BattleModeInfo c_Modes[] = {
		    {"Custom", "", nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
		    {"Capture the flag",
		     "Each team has a flag at its base. Its units go for the enemy's flag and bring it back to their own, while some stay to guard "
		     "theirs. A flag can only be captured while the team's own is at home. A carrier who falls drops it: an enemy can pick it up, or "
		     "one of its own team touch it to send it home (it goes home by itself after 30 seconds).",
		     "flag", 2, BaseTeam, FlagsStart, FlagsSettingsChanged, FlagsUnitsMade, FlagsUpdate, FlagsPanel, FlagsDraw},
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
				if (mode.PointName && !setup.HasPoint[side]) {
					why = std::string(c_SideNames[side]) + "'s " + mode.PointName + " isn't placed yet.";
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
			s_ModeRun.Running = false;
			s_ModeRun.Over = false;
			for (BattleTeam& team: s_BattleTeams) {
				team.Running = false;
			}
		}
	} // namespace

	/// Sends the mode panel to the sim, as it now stands, with a start or stop if asked for.
	void SendBattleMode(int command) {
		Stroke stroke;
		stroke.Kind = Tool::BattleTeam;
		stroke.Team = -1;
		stroke.Count = command;
		stroke.Mode = s_ModeSetup;
		s_Queue.push_back(stroke);
	}

	/// A change made on the mode panel (or a team's point set by a script, with the point tool), in the sim.
	void ApplyBattleMode(const Stroke& stroke) {
		if (stroke.Kind == Tool::BattleModePoint) {
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

	void UpdateBattleMode(bool aiPaused) {
		if (const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode); s_ModeRun.Running && mode.Update) {
			mode.Update(aiPaused);
		}
	}

	/// A new game: no mode's game is on, and the points set (on the last game's scene) are gone. The mode chosen stays.
	void ForgetBattleMode() {
		const BattleMode chosen = s_ModeSetup.Mode;
		s_ModeRun = BattleModeRun();
		s_ModeSetup.HasPoint.fill(false);
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
		ImGui::SetItemTooltip("Custom: set each team up on its card. Or a preset game, where you only pick its size and where each team's base is.");
		return s_ModeSetup.Mode != BattleMode::Custom;
	}

	/// The Battle tab for a mode: its size, the teams in it with their points and factions, its own choices, and the game started or stopped.
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
		ImGui::SetItemTooltip("%s", ready ? "The game starts afresh: scores back to nothing, every flag at its base." : why.c_str());
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

		int size = static_cast<int>(setup.Size);
		if (ImGui::Combo("Size", &size, c_BattleSizeNames, static_cast<int>(BattleSize::Count))) {
			setup.Size = static_cast<BattleSize>(size);
			changed = true;
		}
		const SizeInfo& sized = SizeOf(setup);
		ImGui::SetItemTooltip("Up to %d units a team at once, %d at a time.", sized.Units, sized.PerWave);
		if (ToolUI::RadioButton("Appear at their base##arrive", !setup.ByShip)) {
			setup.ByShip = false;
			changed = true;
		}
		ImGui::SameLine();
		if (ToolUI::RadioButton("Come in by ship##arrive", setup.ByShip)) {
			setup.ByShip = true;
			changed = true;
		}
		ImGui::SetItemTooltip("Each team's units come in by ship over their base (each team's card says which craft), rather than appearing there.");
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
				bool placing = CurrentTool().Kind == Tool::BattleModePoint && s_BattleEditTeam == side;
				std::string label = placing ? "Done (Enter)##point" : (setup.HasPoint[side] ? "Move " : "Place ") + point + "##point";
				if (ToolUI::Button(label.c_str())) {
					if (placing) {
						PutDownBattleTool();
					} else {
						TakeBattleTool(Tool::BattleModePoint, side);
					}
				}
				ImGui::SetItemTooltip("%s", placing ? "Click the map to put it there; Enter (or this) when it's where you want it." : ("Then click the map where this team's " + point + " is to be.").c_str());
				ImGui::SameLine();
				if (running) {
					ImGui::TextDisabled("%d captures, %d in", s_ModeRun.Score[side], Sandbox::CountUnits(side));
				} else if (setup.HasPoint[side]) {
					ImGui::TextDisabled("placed");
				} else {
					ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.4F, 1.0F), "not placed");
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

	/// On the map: the mode's points while it is set up (with the Battle tab showing, or its point tool in hand), and its game while on, with
	/// the window open or not.
	void DrawBattleMode() {
		const BattleModeInfo& mode = ModeOf(s_ModeSetup.Mode);
		if (!mode.Draw) {
			return;
		}
		if (s_ModeRun.Running && s_ModeRun.Settings.Mode == s_ModeSetup.Mode) {
			mode.Draw(true);
		} else if ((Sandbox::IsOpen() && s_CurrentTab == "Battle") || CurrentTool().Kind == Tool::BattleModePoint) {
			mode.Draw(false);
		}
	}
} // namespace SandboxDetail
