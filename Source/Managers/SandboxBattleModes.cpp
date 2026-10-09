// The Battle Director's modes: preset games played by its teams, set up from a few choices (how many a side, which teams, and a base
// drawn for each) rather than every team's card. Each mode is a row of c_Modes: its name, what it asks for, and its rules, which hook into the Battle
// Director as it buys and sends units (SandboxBattle.cpp). Capture the flag, king of the hill, assault, last team standing and VIP hunt.

#include "SandboxInternal.h"

#include <deque>
#include <functional>
#include <unordered_set>

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
			team.Crabs = card.Crabs;
			team.Craft = card.Craft;
			team.Active = TeamIn(settings, side);
			team.Style = BattleStyle::Attack;
			team.EndlessMoney = true;
			team.UnitLimit = std::clamp(settings.TeamSize, 1, 200);
			team.WaveSize = PerWave(settings);
			team.ZoneUnits = PerWave(settings);
			// (Looked at every second, or every few for ships: how many come is held to those whose respawn time is up, ModeRoom.)
			team.ZoneEverySeconds = 1;
			team.EverySeconds = 5;
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
			const Tool held = CurrentTool().Kind;
			if (base.size() < 3 || (!s_ShowModeBases && held != Tool::BattleModeBase && held != Tool::BattleModePoint)) {
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

		/// The score line of the mode's game across the top, with how it ended once it has (or the latest happening, for a few seconds).
		void DrawScore(const std::string& line) {
			Banner(line.c_str(), 64.0F, IM_COL32(255, 255, 255, 255), 1.0F);
			if (s_ModeRun.Over) {
				const std::string result = !s_ModeRun.Result.empty() ? s_ModeRun.Result : (s_ModeRun.Winner >= 0 ? SideName(s_ModeRun.Winner) + " wins!" : "A draw");
				Banner(result.c_str(), 92.0F, s_ModeRun.Winner >= 0 ? c_SideColors[s_ModeRun.Winner] : IM_COL32(255, 255, 255, 255), 1.0F);
			} else if (s_ModeRun.NoteAt >= 0 && !s_ModeRun.Note.empty()) {
				// (Five seconds of game time, the last one fading.)
				float seconds = static_cast<float>(g_TimerMan.GetSimUpdateCount() - s_ModeRun.NoteAt) / UpdatesPerSecond();
				if (seconds < 5.0F) {
					Banner(s_ModeRun.Note.c_str(), 92.0F, IM_COL32(255, 210, 80, 255), std::clamp(5.0F - seconds, 0.0F, 1.0F));
				}
			}
		}

		/// A title with each team in the game's score after it, as "KING OF THE HILL    Red 34 s    Green 12 s".
		std::string Scores(const char* title, const char* unit) {
			std::string line = title;
			for (int side = 0; side < c_Sides; ++side) {
				if (TeamIn(s_ModeRun.Settings, side)) {
					line += "    " + SideName(side) + " " + std::to_string(s_ModeRun.Score[side]) + unit;
				}
			}
			return line;
		}

		/// Minutes and seconds, as "3:07".
		std::string Clock(float seconds) {
			const int whole = std::max(static_cast<int>(std::ceil(seconds)), 0);
			char text[16];
			std::snprintf(text, sizeof(text), "%d:%02d", whole / 60, whole % 60);
			return text;
		}

		void ClearHighlights();

		/// The game won (or ended without a winner, at -1): the teams stopped, every glow off, and how it ended said.
		void EndGame(int winner, const std::string& result) {
			s_ModeRun.Over = true;
			s_ModeRun.Winner = winner;
			s_ModeRun.Result = result;
			ClearHighlights();
			for (BattleTeam& team: s_BattleTeams) {
				team.Running = false;
			}
			Say(result);
		}

		/// The units of the teams in the game that fight: not ships, and not brains.
		std::vector<Actor*> Fighters() {
			std::vector<Actor*> fighters;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (IsCombatant(actor) && !dynamic_cast<const ACraft*>(actor) && !actor->IsInGroup("Brains") && TeamIn(s_ModeRun.Settings, actor->GetTeam())) {
					fighters.push_back(actor);
				}
			}
			return fighters;
		}

		/// The middle of a zone: its corners averaged (they are kept next to the first, so this holds across a wrap seam too).
		Vector ZoneMiddle(const std::vector<Vector>& zone) {
			Vector sum;
			for (const Vector& corner: zone) {
				sum += corner;
			}
			Vector middle = zone.empty() ? sum : sum * (1.0F / static_cast<float>(zone.size()));
			g_SceneMan.WrapPosition(middle);
			return middle;
		}

		/// How many of each team's fighters are in a zone.
		std::array<int, c_Sides> CountIn(const std::vector<Vector>& zone, const std::vector<Actor*>& fighters) {
			std::array<int, c_Sides> count{};
			for (const Actor* fighter: fighters) {
				if (fighter->GetTeam() >= 0 && fighter->GetTeam() < c_Sides && IsInZone(zone, fighter->GetPos())) {
					++count[fighter->GetTeam()];
				}
			}
			return count;
		}

		/// A place for a mode's units to defend (MakeDefender): its middle, how far round it they stand, how much further they go after an
		/// enemy, and the share that roam it rather than hold a post.
		BattleSettings PostAt(const Vector& at, float radius, float chase, int roamPercent) {
			BattleSettings post;
			post.Style = BattleStyle::Defend;
			post.DefendPos = at;
			post.HasDefendPos = true;
			post.DefendRadius = static_cast<int>(radius);
			post.ChaseDistance = static_cast<int>(chase);
			post.RoamPercent = roamPercent;
			return post;
		}

		/// New units sent to defend a place, rather than attack.
		void SendToDefend(const std::vector<Actor*>& wave, const BattleSettings& post) {
			for (Actor* unit: wave) {
				unit->SetOrderAttack(false);
				MakeDefender(unit, post);
			}
		}

		/// The bases of the teams ticked, shaded in their colours.
		void DrawBases(ImDrawList* drawList, const BattleModeSettings& settings) {
			for (int side = 0; side < c_Sides; ++side) {
				if (settings.Plays[side]) {
					DrawBase(drawList, settings.Bases[side], c_SideColors[side]);
				}
			}
		}

		/// One of the mode's zones on the map: shaded, outlined, and named over its middle.
		void DrawModeZone(ImDrawList* drawList, const std::vector<Vector>& zone, ImU32 color, int fill, int line, float thickness, const std::string& label) {
			if (zone.size() < 3) {
				return;
			}
			std::vector<ImVec2> corners = ZoneOnScreen(zone, std::max(ScenePixelsPerWindowPixel(), 0.01F));
			drawList->AddConcavePolyFilled(corners.data(), static_cast<int>(corners.size()), (color & 0x00FFFFFF) | (static_cast<ImU32>(std::clamp(fill, 0, 255)) << 24));
			drawList->AddPolyline(corners.data(), static_cast<int>(corners.size()), (color & 0x00FFFFFF) | (static_cast<ImU32>(std::clamp(line, 0, 255)) << 24), ImDrawFlags_Closed, thickness);
			if (!label.empty()) {
				ImVec2 middle = ToScreen(ZoneMiddle(zone));
				ImVec2 size = ImGui::CalcTextSize(label.c_str());
				ImVec2 at(middle.x - size.x * 0.5F, middle.y - size.y * 0.5F);
				drawList->AddRectFilled(ImVec2(at.x - 4.0F, at.y - 2.0F), ImVec2(at.x + size.x + 4.0F, at.y + size.y + 2.0F), IM_COL32(0, 0, 0, 140), 3.0F);
				drawList->AddText(at, (color & 0x00FFFFFF) | (static_cast<ImU32>(std::clamp(line, 120, 255)) << 24), label.c_str());
			}
		}

		/// A pulse from 0 to 1 and back, a few times a second, for what is to catch the eye.
		float Pulse(float speed = 5.0F) { return 0.5F + 0.5F * std::sin(static_cast<float>(ImGui::GetTime()) * speed); }

		// ---- Where new units appear ----

		std::vector<Vector> s_SpawnedAt; //!< Where units have appeared this update, so the next ones don't land on top of them.
		long long s_SpawnedOn = -1;

		/// Somewhere in a spawn zone for a new unit, of a dozen picks: the one least bad (by badness, if given), away from where the others
		/// appearing this update went. (All a team at once in a small base came out on top of each other, and spent their first half
		/// minute stepping round and hopping over one another.)
		Vector SpreadSpot(const std::vector<Vector>& zone, float height, const std::function<float(const Vector&)>& badness) {
			const long long now = g_TimerMan.GetSimUpdateCount();
			if (s_SpawnedOn != now) {
				s_SpawnedAt.clear();
				s_SpawnedOn = now;
			}
			const bool wraps = g_SceneMan.SceneWrapsX();
			Vector best;
			float bestScore = 0.0F;
			for (int attempt = 0; attempt < 12; ++attempt) {
				Vector spot = SpotInZone(zone, height);
				float score = badness ? badness(spot) : 0.0F;
				for (const Vector& other: s_SpawnedAt) {
					if (g_SceneMan.ShortestDistance(spot, other, wraps).MagnitudeIsLessThan(30.0F)) {
						score += 10000.0F;
					}
				}
				if (attempt == 0 || score < bestScore) {
					best = spot;
					bestScore = score;
				}
			}
			s_SpawnedAt.push_back(best);
			return best;
		}

		float DistanceBetween(const Vector& a, const Vector& b) { return g_SceneMan.ShortestDistance(a, b, g_SceneMan.SceneWrapsX()).GetMagnitude(); }

		// ---- Respawning ----

		std::array<std::unordered_set<long>, c_Sides> s_Alive; //!< The unique IDs of each team's units last seen alive.
		std::array<std::deque<long long>, c_Sides> s_FellAt; //!< When each team's fallen units fell, the oldest first, till they are replaced.
		std::array<int, c_Sides> s_Released{}; //!< Units each team may have sent so far: its team size, and one for each fallen unit whose time is up.
		int s_SizeReleased = 0; //!< The team size s_Released was given for.

		void StartRespawns() {
			for (int side = 0; side < c_Sides; ++side) {
				s_Alive[side].clear();
				s_FellAt[side].clear();
				s_Released[side] = s_ModeRun.Settings.TeamSize;
			}
			s_SizeReleased = s_ModeRun.Settings.TeamSize;
		}

		/// Twice a second: the units that have fallen since, each to be replaced once the respawn time is up. (Before, a fallen unit's place
		/// was filled at the team's next spawn, every 15 s.) With the AI paused, the clocks are held back.
		void UpdateRespawns(bool aiPaused) {
			if (aiPaused) {
				for (std::deque<long long>& fell: s_FellAt) {
					for (long long& at: fell) {
						++at;
					}
				}
				return;
			}
			const long long now = g_TimerMan.GetSimUpdateCount();
			if (s_SizeReleased != s_ModeRun.Settings.TeamSize) {
				// (The team size changed during the game: more room at once, or less as units fall.)
				for (int& released: s_Released) {
					released += std::max(s_ModeRun.Settings.TeamSize - s_SizeReleased, 0);
				}
				s_SizeReleased = s_ModeRun.Settings.TeamSize;
			}
			if (now % 30 != 0) {
				return;
			}
			std::array<std::unordered_set<long>, c_Sides> seen;
			for (Actor* actor: SandboxAccess::Actors()) {
				const int team = actor->GetTeam();
				if (team >= 0 && team < c_Sides && IsCombatant(actor) && !dynamic_cast<const ACraft*>(actor) && !actor->IsInGroup("Brains")) {
					seen[team].insert(actor->GetUniqueID());
				}
			}
			const long long wait = static_cast<long long>(static_cast<float>(std::max(s_ModeRun.Settings.RespawnSeconds, 0)) * UpdatesPerSecond());
			for (int side = 0; side < c_Sides; ++side) {
				for (long id: s_Alive[side]) {
					if (!seen[side].count(id)) {
						s_FellAt[side].push_back(now);
					}
				}
				s_Alive[side] = std::move(seen[side]);
				while (!s_FellAt[side].empty() && now - s_FellAt[side].front() >= wait) {
					s_FellAt[side].pop_front();
					++s_Released[side];
				}
			}
		}

		/// Seconds till a team's next fallen unit is replaced, or -1 with none waiting.
		float NextRespawnIn(int side) {
			if (s_FellAt[side].empty()) {
				return -1.0F;
			}
			return static_cast<float>(std::max(s_ModeRun.Settings.RespawnSeconds, 0)) - static_cast<float>(g_TimerMan.GetSimUpdateCount() - s_FellAt[side].front()) / UpdatesPerSecond();
		}

		// ---- Markers on the map ----

		/// A column of light standing up from a place, in a colour, pulsing, with a glow round its foot: seen from across the map.
		void DrawBeacon(ImDrawList* drawList, ImVec2 foot, ImU32 color, float strength = 1.0F) {
			const float pulse = Pulse(4.0F);
			const ImU32 rgb = color & 0x00FFFFFF;
			const float height = 240.0F;
			const auto alpha = [&](float a) { return static_cast<ImU32>(std::clamp(a * strength, 0.0F, 255.0F)) << 24; };
			// Soft and wide, then a bright core, each fading out going up.
			drawList->AddRectFilledMultiColor(ImVec2(foot.x - 16.0F, foot.y - height), ImVec2(foot.x + 16.0F, foot.y), rgb, rgb, rgb | alpha(70.0F + 40.0F * pulse), rgb | alpha(70.0F + 40.0F * pulse));
			drawList->AddRectFilledMultiColor(ImVec2(foot.x - 6.0F, foot.y - height), ImVec2(foot.x + 6.0F, foot.y), rgb, rgb, rgb | alpha(170.0F + 60.0F * pulse), rgb | alpha(170.0F + 60.0F * pulse));
			drawList->AddRectFilledMultiColor(ImVec2(foot.x - 1.5F, foot.y - height * 0.8F), ImVec2(foot.x + 1.5F, foot.y), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 230), IM_COL32(255, 255, 255, 230));
			for (int ring = 5; ring >= 1; --ring) {
				drawList->AddCircleFilled(ImVec2(foot.x, foot.y - 14.0F), (8.0F + 9.0F * static_cast<float>(ring)) * (0.9F + 0.2F * pulse), rgb | alpha(26.0F + 30.0F * pulse), 32);
			}
			drawList->AddCircle(ImVec2(foot.x, foot.y - 14.0F), 30.0F + 8.0F * pulse, rgb | alpha(200.0F * (1.0F - pulse) + 40.0F), 32, 2.5F);
		}

		/// A label on a dark plate, centred over a place on screen.
		void DrawTag(ImDrawList* drawList, ImVec2 over, const std::string& text, ImU32 color) {
			ImVec2 size = ImGui::CalcTextSize(text.c_str());
			ImVec2 at(over.x - size.x * 0.5F, over.y - size.y);
			drawList->AddRectFilled(ImVec2(at.x - 5.0F, at.y - 3.0F), ImVec2(at.x + size.x + 5.0F, at.y + size.y + 3.0F), IM_COL32(0, 0, 0, 190), 4.0F);
			drawList->AddRect(ImVec2(at.x - 5.0F, at.y - 3.0F), ImVec2(at.x + size.x + 5.0F, at.y + size.y + 3.0F), color, 4.0F, 0, 1.5F);
			drawList->AddText(at, color, text.c_str());
		}

		/// A place off the edge of the view: an arrow at the edge pointing to it, with its label, so it can always be found.
		void DrawOffScreen(ImDrawList* drawList, ImVec2 place, const std::string& label, ImU32 color) {
			const GameViewRect view = g_DebugMan.GetUncoveredView();
			const float margin = 28.0F;
			if (place.x >= view.x && place.x <= view.x + view.w && place.y >= view.y && place.y <= view.y + view.h) {
				return;
			}
			const ImVec2 middle(view.x + view.w * 0.5F, view.y + view.h * 0.5F);
			ImVec2 way(place.x - middle.x, place.y - middle.y);
			const float length = std::max(std::sqrt(way.x * way.x + way.y * way.y), 0.001F);
			way = ImVec2(way.x / length, way.y / length);
			// Out from the middle to the edge, along the way to it.
			const float reachX = way.x != 0.0F ? (view.w * 0.5F - margin) / std::abs(way.x) : 1.0e9F;
			const float reachY = way.y != 0.0F ? (view.h * 0.5F - margin) / std::abs(way.y) : 1.0e9F;
			const float reach = std::min(reachX, reachY);
			const ImVec2 tip(middle.x + way.x * reach, middle.y + way.y * reach);
			const ImVec2 side(-way.y, way.x);
			const float pulse = 0.8F + 0.2F * Pulse(6.0F);
			drawList->AddTriangleFilled(ImVec2(tip.x + way.x * 14.0F * pulse, tip.y + way.y * 14.0F * pulse), ImVec2(tip.x + side.x * 10.0F, tip.y + side.y * 10.0F), ImVec2(tip.x - side.x * 10.0F, tip.y - side.y * 10.0F), color);
			drawList->AddTriangle(ImVec2(tip.x + way.x * 14.0F * pulse, tip.y + way.y * 14.0F * pulse), ImVec2(tip.x + side.x * 10.0F, tip.y + side.y * 10.0F), ImVec2(tip.x - side.x * 10.0F, tip.y - side.y * 10.0F), IM_COL32(0, 0, 0, 200), 1.5F);
			DrawTag(drawList, ImVec2(tip.x - way.x * 22.0F, tip.y - way.y * 22.0F + 6.0F), label, color);
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

		/// How far from its own flag a team's units appear, or as far as its base allows.
		constexpr float c_SpawnClear = 150.0F;

		void SetUpTeams();

		/// Somewhere in a team's base for a new unit to appear: never at its own flag (at least c_SpawnClear from it, as far as the base
		/// allows). A unit going for an enemy's flag appears on the side of the base nearest it, so it doesn't have to make its way through its
		/// own guards first; a guard appears near the flag.
		Vector FlagsSpawnSpot(int side, const std::vector<Vector>& zone, const Actor* unit) {
			const Vector flag = s_Flags[std::clamp(side, 0, c_Sides - 1)].Home;
			auto runner = unit ? s_Runners.find(unit->GetUniqueID()) : s_Runners.end();
			const bool runs = runner != s_Runners.end() && runner->second.Target >= 0 && runner->second.Target < c_Sides;
			const Vector goal = runs ? s_Flags[runner->second.Target].Home : flag;
			return SpreadSpot(zone, unit ? unit->GetHeight() : 0.0F, [&](const Vector& spot) {
				const float fromFlag = DistanceBetween(spot, flag);
				return (fromFlag < c_SpawnClear ? 5000.0F + (c_SpawnClear - fromFlag) * 10.0F : 0.0F) + DistanceBetween(spot, goal);
			});
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

		/// A flag's carrier glows (Actor::SetHighlighted) while it has it, and stops when it hasn't; and while it has it, its routes take the
		/// safest viable way, round the enemy rather than through them (Actor::SetRouteThreatAvoidance).
		void SetCarrier(Flag& flag, Actor* carrier) {
			if (Actor* old = GetRef(flag.Carrier)) {
				old->SetHighlighted(false);
				old->SetRouteThreatAvoidance(0.0F);
			}
			flag.Carrier = MakeRef(carrier);
			if (carrier) {
				carrier->SetHighlighted(true);
				carrier->SetRouteThreatAvoidance(1.0F);
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

		/// Where a flag let go of at a place comes to rest: out of the ground if it's in it, then straight down onto whatever is below, however
		/// far. False when there is nothing below (off the map, or into a bottomless drop), so it is to go home rather than be lost.
		bool FallTo(const Vector& at, Vector& ground) {
			Vector spot = at;
			g_SceneMan.WrapPosition(spot);
			const int width = g_SceneMan.GetSceneWidth();
			const int height = g_SceneMan.GetSceneHeight();
			int x = spot.GetFloorIntX();
			int y = std::max(spot.GetFloorIntY(), 0);
			if (x < 0 || x >= width || y >= height - 1) {
				return false;
			}
			for (int up = 0; up < 200 && y > 0 && g_SceneMan.GetTerrMatter(x, y) != g_MaterialAir; ++up) {
				--y;
			}
			if (g_SceneMan.GetTerrMatter(x, y) != g_MaterialAir) {
				return false;
			}
			while (y < height - 1 && g_SceneMan.GetTerrMatter(x, y + 1) == g_MaterialAir) {
				++y;
			}
			if (y >= height - 1) {
				return false;
			}
			ground = Vector(static_cast<float>(x), static_cast<float>(y));
			return true;
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
			// (Close round the flag, not spread across the whole base, where they stood in the way of their own team going out.)
			guard.DefendRadius = static_cast<int>(std::min(GuardRadius(settings.Bases[side]), 110.0F));
			guard.ChaseDistance = guard.DefendRadius + 250;
			guard.RoamPercent = 30;
			const long long now = g_TimerMan.GetSimUpdateCount();
			for (Actor* unit: wave) {
				if (enemies.empty() || Random01() * 100.0F < static_cast<float>(settings.GuardPercent)) {
					unit->SetOrderAttack(false);
					MakeDefender(unit, guard);
				} else {
					// (Without the attack order it was made with: its AI took that up as soon as it had walked off its first waypoint, and went
					// after the nearest enemy instead of the flag.)
					unit->SetOrderAttack(false);
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
						// Down where its carrier fell (or was last seen), on the ground below: never lost off the map or down a pit with no
						// bottom, but home.
						Vector ground;
						if (!FallTo(flag.Pos, ground)) {
							SendHome(side);
							Say(SideName(side) + "'s flag fell where nobody could get it: back home");
							continue;
						}
						flag.State = FlagState::Dropped;
						flag.Pos = ground;
						SetCarrier(flag, nullptr);
						flag.DroppedAt = now;
						RecentreDefenders(side, flag.Pos, true);
						Say(SideName(side) + "'s flag is down: back home in " + std::to_string(std::max(settings.ReturnSeconds, 1)) + " s");
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
							EndGame(team, SideName(team) + " wins, " + std::to_string(s_ModeRun.Score[team]) + " captures");
							return;
						}
						Say(SideName(team) + " captured " + SideName(side) + "'s flag");
					}
					continue;
				}
				if (flag.State == FlagState::Dropped && static_cast<float>(now - flag.DroppedAt) > static_cast<float>(std::max(settings.ReturnSeconds, 1)) * UpdatesPerSecond()) {
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

		/// Whether a flag standing at a place is buried: ground where its cloth is (near the top of its pole: low down, a tuft of grass on a
		/// slope was enough to call a flag buried and move it).
		bool Buried(const Vector& at) {
			Vector cloth = at - Vector(0.0F, 22.0F);
			g_SceneMan.WrapPosition(cloth);
			return g_SceneMan.GetTerrMatter(cloth.GetFloorIntX(), cloth.GetFloorIntY()) != g_MaterialAir;
		}

		/// Whether a unit could walk (jump, dig, or break through, as it can) from where it is to a flag at a place.
		bool CanReach(const Actor* unit, const Vector& at) {
			if (!g_SceneMan.GetScene() || !unit) {
				return true;
			}
			// (From the ground under it, as its own AI searches: from a flying unit's place in the air no way was found.)
			return RouteReachable(RouteCost(unit, at - Vector(0.0F, 10.0F)));
		}

		/// Every five seconds: a dropped flag that is buried, has nothing left under it, or that no unit can get to twice running goes home, so
		/// it is never lost. With MoveStuckPoint, a flag at home that is buried, has lost the ground under it, or can't be got to by an enemy
		/// twice running is moved somewhere else in its base that can.
		void UpdateStuckFlags() {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			for (int side = 0; side < c_Sides; ++side) {
				Flag& flag = s_Flags[side];
				if (!TeamIn(settings, side) || flag.State == FlagState::Carried) {
					continue;
				}
				if (flag.State == FlagState::Dropped) {
					Vector ground;
					if (Buried(flag.Pos) || !FallTo(flag.Pos, ground)) {
						SendHome(side);
						Say(SideName(side) + "'s flag was lost: back to its base");
						continue;
					}
					// (Ground blasted away under it: it falls onto what's below.)
					flag.Pos = ground;
					// Anyone at all who could get to it: the nearest unit of any team in the game.
					const Actor* nearestUnit = nullptr;
					float nearestDistance = 0.0F;
					for (Actor* actor: SandboxAccess::Actors()) {
						if (!TeamIn(settings, actor->GetTeam()) || !IsCombatant(actor) || dynamic_cast<const ACraft*>(actor) || actor->IsInGroup("Brains")) {
							continue;
						}
						float distance = g_SceneMan.ShortestDistance(actor->GetPos(), flag.Pos, g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
						if (!nearestUnit || distance < nearestDistance) {
							nearestUnit = actor;
							nearestDistance = distance;
						}
					}
					flag.Unreachable = !nearestUnit || CanReach(nearestUnit, flag.Pos) ? 0 : flag.Unreachable + 1;
					if (flag.Unreachable >= 2) {
						flag.Unreachable = 0;
						SendHome(side);
						Say(SideName(side) + "'s flag lay where nobody could get it: back to its base");
					}
					continue;
				}
				if (!settings.MoveStuckPoint) {
					continue;
				}
				// The enemy's units to try the way with: the three nearest of any team after it, walkers first. (One was enough before, and the
				// nearest being a tank that can't climb, or a drone, was taken to mean nobody could get there.)
				std::vector<std::pair<float, const Actor*>> enemies;
				for (Actor* actor: SandboxAccess::Actors()) {
					if (actor->GetTeam() == side || !TeamIn(settings, actor->GetTeam()) || !IsCombatant(actor) || dynamic_cast<const ACraft*>(actor) || actor->IsInGroup("Brains")) {
						continue;
					}
					const float distance = g_SceneMan.ShortestDistance(actor->GetPos(), flag.Home, g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
					enemies.emplace_back(dynamic_cast<const AHuman*>(actor) ? distance : distance + 1.0e8F, actor);
				}
				std::sort(enemies.begin(), enemies.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
				enemies.resize(std::min<size_t>(enemies.size(), 3));
				const auto reachable = [&enemies](const Vector& at) {
					return enemies.empty() || std::any_of(enemies.begin(), enemies.end(), [&at](const auto& enemy) { return CanReach(enemy.second, at); });
				};
				// Ground blasted away under it: it falls onto what's below.
				Vector settled = Grounded(flag.Home);
				if (IsInZone(settings.Bases[side], settled) && !g_SceneMan.ShortestDistance(settled, flag.Home, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(2.0F)) {
					FlagMoved(side, settled);
				}
				// (Twice running, whatever the reason, so one look at a moment's mess doesn't move it.)
				const bool trapped = Buried(flag.Home) || !IsInZone(settings.Bases[side], settled) || !reachable(flag.Home);
				flag.Unreachable = trapped ? flag.Unreachable + 1 : 0;
				bool stuck = flag.Unreachable >= 2;
				if (!stuck) {
					continue;
				}
				for (int attempt = 0; attempt < 10; ++attempt) {
					Vector spot = SpotInZone(settings.Bases[side], 0.0F);
					if (!Buried(spot) && reachable(spot)) {
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
			// (Or still on GOTO with no way left to walk: an order whose route was given up on.)
			const bool idle = unit->GetAIMode() != Actor::AIMODE_GOTO || (unit->GetMovePathSize() == 0 && !unit->IsWaitingOnNewMovePath() && !unit->GetMOMoveTarget());
			const bool stopped = idle && now - runner.SentAt > static_cast<long long>(3.0F * UpdatesPerSecond()) && !g_SceneMan.ShortestDistance(unit->GetPos(), to, wraps).MagnitudeIsLessThan(c_FlagReach);
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
			if (!s_ModeRun.Over && now % 300 == 150) {
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
			changed |= ImGui::SliderInt("Dropped flag returns after", &setup.ReturnSeconds, 5, 180, "%d s");
			ImGui::SetItemTooltip("How long a dropped flag lies (glowing, with its seconds counting down over it) before it goes back home by itself, if nobody picks it up first.");
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
				const std::string name = SideName(side) + " flag";
				if (flag.State != FlagState::Home) {
					// Where it stands at home, empty.
					ImVec2 home = ToScreen(flag.Home);
					drawList->AddCircle(ImVec2(home.x, home.y - 6.0F), 12.0F, (color & 0x00FFFFFF) | (90u << 24), 24, 2.0F);
				}
				Actor* carrier = flag.State == FlagState::Carried ? GetRef(flag.Carrier) : nullptr;
				if (carrier) {
					// Over its carrier's head: the flag, its column of light, and who has it, in words.
					ImVec2 head = ToScreen(carrier->GetPos() - Vector(0.0F, carrier->GetHeight() * 0.5F));
					const float bob = 3.0F * Pulse(4.0F);
					DrawBeacon(drawList, ImVec2(head.x, head.y - 4.0F), color, 0.8F);
					DrawFlag(drawList, ImVec2(head.x, head.y - 6.0F - bob), color, 0.9F);
					DrawTag(drawList, ImVec2(head.x, head.y - 44.0F - bob), SideName(carrier->GetTeam()) + " HAS THE " + SideName(side) + " FLAG", color);
					DrawOffScreen(drawList, head, name + " (taken)", color);
				} else if (flag.State == FlagState::Dropped) {
					// Lying out: its light, and the seconds till it goes home over it.
					ImVec2 foot = ToScreen(flag.Pos);
					DrawBeacon(drawList, foot, color, 1.0F);
					DrawFlag(drawList, foot, color);
					const float left = static_cast<float>(std::max(settings.ReturnSeconds, 1)) - static_cast<float>(g_TimerMan.GetSimUpdateCount() - flag.DroppedAt) / UpdatesPerSecond();
					DrawTag(drawList, ImVec2(foot.x + 6.0F, foot.y - 40.0F), name + " DROPPED: home in " + std::to_string(std::max(static_cast<int>(std::ceil(left)), 0)) + " s", color);
					DrawOffScreen(drawList, foot, name + " (dropped)", color);
				} else {
					ImVec2 foot = ToScreen(flag.Pos);
					DrawBeacon(drawList, foot, color, 0.8F);
					DrawFlag(drawList, foot, color, 1.2F);
					DrawTag(drawList, ImVec2(foot.x + 6.0F, foot.y - 46.0F), name, color);
					DrawOffScreen(drawList, foot, name, color);
				}
			}
			if (running) {
				DrawScore(Scores("CAPTURE THE FLAG", "") + (settings.ScoreToWin > 0 ? "    (first to " + std::to_string(settings.ScoreToWin) + ")" : ""));
			}
		}

		std::string FlagsStatus(int side) { return std::to_string(s_ModeRun.Score[side]) + " captures, " + std::to_string(Sandbox::CountUnits(side)) + " in"; }

		// ---- Hunters (last team standing's units, and VIP hunt's) ----

		/// What a hunter goes after, of the fighters in the game: nullptr to leave it be.
		using Prey = Actor* (*)(const FlagRunner& runner, const Actor* unit, const std::vector<Actor*>& fighters);

		/// The nearest enemy of a unit's, or nullptr.
		Actor* NearestEnemy(const Actor* unit, const std::vector<Actor*>& fighters) {
			Actor* nearest = nullptr;
			float nearestDistance = 0.0F;
			for (Actor* other: fighters) {
				if (other->GetTeam() == unit->GetTeam()) {
					continue;
				}
				float distance = g_SceneMan.ShortestDistance(unit->GetPos(), other->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
				if (!nearest || distance < nearestDistance) {
					nearest = other;
					nearestDistance = distance;
				}
			}
			return nearest;
		}

		/// Each hunter (kept as capture the flag's runners are, in s_Runners), every half second: sent after its prey.
		void UpdateHunters(long long now, Prey prey, const char* reason) {
			const std::vector<Actor*> fighters = Fighters();
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
					entry = runner.Seen || now - runner.Made > riding ? s_Runners.erase(entry) : std::next(entry);
					continue;
				}
				runner.Seen = true;
				if (!IsCombatant(unit)) {
					entry = s_Runners.erase(entry);
					continue;
				}
				if (!unit->IsPlayerControlled()) {
					if (Actor* target = prey(runner, unit, fighters)) {
						SendRunner(unit, runner, target->GetPos(), target, reason, now);
					}
				}
				++entry;
			}
		}

		/// New units made hunters, each after one of the other teams in the game (picked at random).
		void MakeHunters(int side, const std::vector<Actor*>& wave) {
			std::vector<int> enemies;
			for (int other = 0; other < c_Sides; ++other) {
				if (other != side && TeamIn(s_ModeRun.Settings, other)) {
					enemies.push_back(other);
				}
			}
			for (Actor* unit: wave) {
				FlagRunner& runner = s_Runners[unit->GetUniqueID()];
				runner.Team = side;
				runner.Target = enemies.empty() ? -1 : enemies[std::min(enemies.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(enemies.size())))];
				runner.Made = g_TimerMan.GetSimUpdateCount();
			}
		}

		// ---- King of the hill ----

		int s_Hill = 0; //!< The hill in play, of the mode's zones.
		long long s_HillSince = 0; //!< The sim update it came into play on (held back while the AI is paused).
		std::array<float, c_Sides> s_Held{}; //!< Seconds each team has held the hill.
		int s_Holder = -1; //!< The team holding it now: -1 nobody, -2 more than one (contested).

		/// Where the teams fight for the hill in play: every unit defends it.
		BattleSettings HillPost() {
			const std::vector<std::vector<Vector>>& zones = s_ModeRun.Settings.Zones;
			const std::vector<Vector>& hill = zones[std::clamp(s_Hill, 0, static_cast<int>(zones.size()) - 1)];
			const float radius = GuardRadius(hill);
			return PostAt(ZoneMiddle(hill), radius, radius + 250.0F, 0);
		}

		void HillStart() {
			s_Hill = 0;
			s_HillSince = g_TimerMan.GetSimUpdateCount();
			s_Held.fill(0.0F);
			s_Holder = -1;
		}

		void HillSettingsChanged() {
			if (s_Hill >= static_cast<int>(s_ModeRun.Settings.Zones.size())) {
				s_Hill = 0;
			}
		}

		void HillUnitsMade(int side, const std::vector<Actor*>& wave) {
			if (!s_ModeRun.Settings.Zones.empty()) {
				SendToDefend(wave, HillPost());
			}
		}

		/// New units appear on the side of their base nearest the hill.
		Vector HillSpawnSpot(int side, const std::vector<Vector>& zone, const Actor* unit) {
			const Vector hill = HillPost().DefendPos;
			return SpreadSpot(zone, unit ? unit->GetHeight() : 0.0F, [&hill](const Vector& spot) { return DistanceBetween(spot, hill); });
		}

		/// The next hill comes into play: every team's units make for it.
		void MoveHill(int next) {
			s_Hill = next;
			s_HillSince = g_TimerMan.GetSimUpdateCount();
			s_Holder = -1;
			const BattleSettings post = HillPost();
			for (int side = 0; side < c_Sides; ++side) {
				RecentreDefenders(side, post.DefendPos, false, static_cast<float>(post.DefendRadius));
			}
			Say("The hill moves: hill " + std::to_string(next + 1) + "!");
		}

		/// Twice a second or so (every six updates): who is on the hill, and the seconds it scores them.
		void HillUpdate(bool aiPaused) {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			if (aiPaused) {
				++s_HillSince;
				return;
			}
			const long long now = g_TimerMan.GetSimUpdateCount();
			if (s_ModeRun.Over || settings.Zones.empty() || now % 6 != 0) {
				return;
			}
			std::array<int, c_Sides> on = CountIn(settings.Zones[s_Hill], Fighters());
			int holder = -1;
			int most = 0;
			int teams = 0;
			bool tied = false;
			for (int side = 0; side < c_Sides; ++side) {
				if (!TeamIn(settings, side) || on[side] == 0) {
					continue;
				}
				++teams;
				if (on[side] > most) {
					most = on[side];
					holder = side;
					tied = false;
				} else if (on[side] == most) {
					tied = true;
				}
			}
			if (teams > 1 && (!settings.MajorityScores || tied)) {
				holder = -2;
			}
			if (holder != s_Holder) {
				if (holder >= 0) {
					Say(SideName(holder) + " holds the hill");
				} else if (holder == -2) {
					Say("The hill is contested");
				}
				s_Holder = holder;
			}
			if (holder >= 0) {
				s_Held[holder] += 6.0F / UpdatesPerSecond();
				s_ModeRun.Score[holder] = static_cast<int>(s_Held[holder]);
				if (settings.HoldToWin > 0 && s_Held[holder] >= static_cast<float>(settings.HoldToWin)) {
					EndGame(holder, SideName(holder) + " is king of the hill!");
					return;
				}
			}
			if (settings.Zones.size() > 1 && settings.HillMoveSeconds > 0 && static_cast<float>(now - s_HillSince) >= static_cast<float>(settings.HillMoveSeconds) * UpdatesPerSecond()) {
				MoveHill((s_Hill + 1) % static_cast<int>(settings.Zones.size()));
			}
		}

		void HillPanel(BattleModeSettings& setup, bool& changed) {
			changed |= ImGui::SliderInt("Hold to win", &setup.HoldToWin, 0, 600, setup.HoldToWin > 0 ? "%d s" : "play on");
			ImGui::SetItemTooltip("Seconds a team has to hold the hill, all told, to win. It scores only while it alone has units on it. 0: it goes on till you stop it.");
			changed |= ImGui::SliderInt("Hill moves every", &setup.HillMoveSeconds, 0, 300, setup.HillMoveSeconds > 0 ? "%d s" : "never");
			ImGui::SetItemTooltip("With more than one hill drawn: seconds before the next one comes into play, and everyone has to run for it. Never: the first one stays.");
			changed |= ToolUI::Checkbox("Most units on it scores", &setup.MajorityScores);
			ImGui::SetItemTooltip("A hill with more than one team on it scores for the team with the most units there. Off: a contested hill scores for nobody.");
		}

		void HillDraw(bool running) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();
			const BattleModeSettings& settings = running ? s_ModeRun.Settings : s_ModeSetup;
			DrawBases(drawList, settings);
			for (int i = 0; i < static_cast<int>(settings.Zones.size()); ++i) {
				const std::string name = settings.Zones.size() > 1 ? "HILL " + std::to_string(i + 1) : "HILL";
				if (!running) {
					DrawModeZone(drawList, settings.Zones[i], IM_COL32(255, 255, 255, 255), 30, 200, 2.0F, name);
				} else if (i != s_Hill) {
					DrawModeZone(drawList, settings.Zones[i], IM_COL32(160, 160, 160, 255), 0, 70, 1.0F, "");
				} else {
					// In play: in the colour of whoever holds it, pulsing; orange while fought over.
					const ImU32 color = s_Holder >= 0 ? c_SideColors[s_Holder] : (s_Holder == -2 ? IM_COL32(255, 150, 40, 255) : IM_COL32(255, 255, 255, 255));
					const float pulse = Pulse();
					DrawModeZone(drawList, settings.Zones[i], color, static_cast<int>(40.0F + 40.0F * pulse), 230, 2.0F + pulse * 2.0F, s_Holder == -2 ? name + ": CONTESTED" : name);
				}
			}
			if (running) {
				std::string line = Scores("KING OF THE HILL", " s");
				if (settings.HoldToWin > 0) {
					line += "    (" + std::to_string(settings.HoldToWin) + " s wins)";
				}
				if (settings.Zones.size() > 1 && settings.HillMoveSeconds > 0 && !s_ModeRun.Over) {
					line += "    hill moves in " + Clock(static_cast<float>(settings.HillMoveSeconds) - static_cast<float>(g_TimerMan.GetSimUpdateCount() - s_HillSince) / UpdatesPerSecond());
				}
				DrawScore(line);
			}
		}

		std::string HillStatus(int side) { return std::to_string(s_ModeRun.Score[side]) + " s held, " + std::to_string(Sandbox::CountUnits(side)) + " in"; }

		// ---- Assault ----

		int s_Objective = 0; //!< The objective the attackers are after, of the mode's zones in order.
		float s_Progress = 0.0F; //!< Seconds they have stood in it with no defender there.
		bool s_Contested = false; //!< Both sides in it: the taking held where it is.
		long long s_Deadline = 0; //!< The sim update the attackers' time runs out on; 0 for no limit.

		int AttackerOf(const BattleModeSettings& settings) { return std::clamp(settings.Attacker, 0, c_Sides - 1); }

		/// Where both sides fight: the objective being taken.
		BattleSettings ObjectivePost() {
			const std::vector<std::vector<Vector>>& zones = s_ModeRun.Settings.Zones;
			const std::vector<Vector>& objective = zones[std::clamp(s_Objective, 0, static_cast<int>(zones.size()) - 1)];
			const float radius = GuardRadius(objective);
			return PostAt(ZoneMiddle(objective), radius, radius + 300.0F, 0);
		}

		bool AssaultReady(const BattleModeSettings& setup, std::string& why) {
			if (!setup.Plays[AttackerOf(setup)]) {
				why = std::string("Tick ") + c_SideNames[AttackerOf(setup)] + ", the attackers, or pick others to attack.";
				return false;
			}
			return true;
		}

		void AssaultStart() {
			s_Objective = 0;
			s_Progress = 0.0F;
			s_Contested = false;
			const int limit = s_ModeRun.Settings.TimeLimit;
			s_Deadline = limit > 0 ? g_TimerMan.GetSimUpdateCount() + static_cast<long long>(static_cast<float>(limit) * UpdatesPerSecond()) : 0;
		}

		void AssaultSettingsChanged() {
			if (s_Objective >= static_cast<int>(s_ModeRun.Settings.Zones.size())) {
				s_Objective = 0;
			}
		}

		void AssaultUnitsMade(int side, const std::vector<Actor*>& wave) {
			if (!s_ModeRun.Settings.Zones.empty()) {
				SendToDefend(wave, ObjectivePost());
			}
		}

		/// New units appear on the side of their base nearest the objective.
		Vector AssaultSpawnSpot(int side, const std::vector<Vector>& zone, const Actor* unit) {
			const Vector objective = ObjectivePost().DefendPos;
			return SpreadSpot(zone, unit ? unit->GetHeight() : 0.0F, [&objective](const Vector& spot) { return DistanceBetween(spot, objective); });
		}

		/// The first of the defending teams (who are named as the winners when time runs out), or -1 with more than one.
		int OnlyDefender(const BattleModeSettings& settings) {
			int defender = -1;
			for (int side = 0; side < c_Sides; ++side) {
				if (side != AttackerOf(settings) && TeamIn(settings, side)) {
					if (defender >= 0) {
						return -1;
					}
					defender = side;
				}
			}
			return defender;
		}

		/// Every six updates: the attackers take the objective while they stand in it with no defender there (fought over, it holds; left,
		/// it slips back), then the next; the defenders win if their time runs out first.
		void AssaultUpdate(bool aiPaused) {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			if (aiPaused) {
				s_Deadline += s_Deadline > 0 ? 1 : 0;
				return;
			}
			const long long now = g_TimerMan.GetSimUpdateCount();
			if (s_ModeRun.Over || settings.Zones.empty() || now % 6 != 0) {
				return;
			}
			const int attacker = AttackerOf(settings);
			std::array<int, c_Sides> in = CountIn(settings.Zones[s_Objective], Fighters());
			int defenders = 0;
			for (int side = 0; side < c_Sides; ++side) {
				defenders += side != attacker && TeamIn(settings, side) ? in[side] : 0;
			}
			const float step = 6.0F / UpdatesPerSecond();
			const bool wasContested = s_Contested;
			s_Contested = in[attacker] > 0 && defenders > 0;
			if (s_Contested && !wasContested) {
				Say("Objective " + std::to_string(s_Objective + 1) + " is being fought over");
			}
			if (in[attacker] > 0 && defenders == 0) {
				s_Progress += step;
			} else if (in[attacker] == 0) {
				s_Progress = std::max(s_Progress - step * 0.5F, 0.0F);
			}
			if (s_Progress >= static_cast<float>(std::max(settings.CaptureSeconds, 1))) {
				++s_ModeRun.Score[attacker];
				++s_Objective;
				s_Progress = 0.0F;
				s_Contested = false;
				if (s_Objective >= static_cast<int>(settings.Zones.size())) {
					s_Objective = static_cast<int>(settings.Zones.size()) - 1;
					EndGame(attacker, SideName(attacker) + " took every objective!");
					return;
				}
				if (s_Deadline > 0) {
					s_Deadline += static_cast<long long>(static_cast<float>(settings.BonusSeconds) * UpdatesPerSecond());
				}
				const BattleSettings post = ObjectivePost();
				for (int side = 0; side < c_Sides; ++side) {
					RecentreDefenders(side, post.DefendPos, false, static_cast<float>(post.DefendRadius));
				}
				Say("Objective " + std::to_string(s_Objective) + " taken" + (s_Deadline > 0 && settings.BonusSeconds > 0 ? ": +" + std::to_string(settings.BonusSeconds) + " s" : std::string()) + ". On to the next!");
			}
			if (s_Deadline > 0 && now >= s_Deadline) {
				const int defender = OnlyDefender(settings);
				EndGame(defender, "Time's up: " + (defender >= 0 ? SideName(defender) : std::string("the defenders")) + " held!");
			}
		}

		void AssaultPanel(BattleModeSettings& setup, bool& changed) {
			changed |= ImGui::Combo("Attackers", &setup.Attacker, c_SideNames, c_Sides);
			ImGui::SetItemTooltip("The team that attacks. Every other team ticked defends.");
			changed |= ImGui::SliderInt("Seconds to take one", &setup.CaptureSeconds, 3, 120, "%d s");
			ImGui::SetItemTooltip("How long the attackers have to stand in an objective, with no defender in it, to take it. Fought over, the count holds; left empty, it slips back.");
			changed |= ImGui::SliderInt("Time limit", &setup.TimeLimit, 0, 1800, setup.TimeLimit > 0 ? "%d s" : "none");
			ImGui::SetItemTooltip("Seconds the attackers have to take them all, or the defenders win. 0: no limit.");
			ImGui::BeginDisabled(setup.TimeLimit <= 0);
			changed |= ImGui::SliderInt("Added per objective", &setup.BonusSeconds, 0, 300, "%d s");
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Seconds added to the attackers' time for each objective they take.");
		}

		void AssaultDraw(bool running) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();
			const BattleModeSettings& settings = running ? s_ModeRun.Settings : s_ModeSetup;
			const int attacker = AttackerOf(settings);
			DrawBases(drawList, settings);
			for (int i = 0; i < static_cast<int>(settings.Zones.size()); ++i) {
				const std::string name = "OBJECTIVE " + std::to_string(i + 1);
				if (!running) {
					DrawModeZone(drawList, settings.Zones[i], IM_COL32(255, 255, 255, 255), 30, 200, 2.0F, name);
				} else if (i < s_Objective || (s_ModeRun.Over && s_ModeRun.Winner == attacker)) {
					DrawModeZone(drawList, settings.Zones[i], c_SideColors[attacker], 25, 120, 1.0F, name + ": TAKEN");
				} else if (i > s_Objective) {
					DrawModeZone(drawList, settings.Zones[i], IM_COL32(160, 160, 160, 255), 0, 90, 1.0F, name);
				} else {
					const float pulse = Pulse();
					const ImU32 color = s_Contested ? IM_COL32(255, 150, 40, 255) : (s_Progress > 0.0F ? c_SideColors[attacker] : IM_COL32(255, 255, 255, 255));
					DrawModeZone(drawList, settings.Zones[i], color, static_cast<int>(35.0F + 35.0F * pulse), 230, 2.0F + 2.0F * pulse, name);
					// How far it is taken, as a bar under its name.
					const float taken = std::clamp(s_Progress / static_cast<float>(std::max(settings.CaptureSeconds, 1)), 0.0F, 1.0F);
					ImVec2 middle = ToScreen(ZoneMiddle(settings.Zones[i]));
					ImVec2 from(middle.x - 40.0F, middle.y + 12.0F);
					drawList->AddRectFilled(from, ImVec2(from.x + 80.0F, from.y + 6.0F), IM_COL32(0, 0, 0, 160), 2.0F);
					drawList->AddRectFilled(from, ImVec2(from.x + 80.0F * taken, from.y + 6.0F), c_SideColors[attacker], 2.0F);
				}
			}
			if (running) {
				std::string line = "ASSAULT    " + SideName(attacker) + " attacking    objective " + std::to_string(std::min(s_Objective + 1, static_cast<int>(settings.Zones.size()))) + " of " + std::to_string(settings.Zones.size());
				if (s_Contested) {
					line += "    CONTESTED";
				} else if (s_Progress > 0.0F) {
					line += "    taking: " + std::to_string(static_cast<int>(100.0F * s_Progress / static_cast<float>(std::max(settings.CaptureSeconds, 1)))) + "%";
				}
				if (s_Deadline > 0 && !s_ModeRun.Over) {
					line += "    " + Clock(static_cast<float>(s_Deadline - g_TimerMan.GetSimUpdateCount()) / UpdatesPerSecond()) + " left";
				}
				DrawScore(line);
			}
		}

		std::string AssaultStatus(int side) { return std::string(side == AttackerOf(s_ModeRun.Settings) ? "attacking, " : "defending, ") + std::to_string(Sandbox::CountUnits(side)) + " in"; }

		// ---- Last team standing ----

		std::array<bool, c_Sides> s_Out{}; //!< Teams with no units left to send and none left in.

		int TicketsLeft(int side) { return std::max(s_ModeRun.Settings.Tickets - s_BattleTeams[side].Sent, 0); }

		void StandingStart() {
			s_Out.fill(false);
			for (int side = 0; side < c_Sides; ++side) {
				s_ModeRun.Score[side] = TeamIn(s_ModeRun.Settings, side) ? std::max(s_ModeRun.Settings.Tickets, 0) : 0;
			}
		}

		/// No more units than a team has tickets left for.
		int StandingRoom(int side, int room) { return TeamIn(s_ModeRun.Settings, side) ? std::min(room, TicketsLeft(side)) : room; }

		Actor* StandingPrey(const FlagRunner&, const Actor* unit, const std::vector<Actor*>& fighters) { return NearestEnemy(unit, fighters); }

		/// Every half second the hunters go after the nearest enemy; every second, a team with no tickets and no units left is out, and the
		/// last team in wins.
		void StandingUpdate(bool aiPaused) {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			const long long now = g_TimerMan.GetSimUpdateCount();
			if (aiPaused || s_ModeRun.Over) {
				return;
			}
			if (now % 30 == 15) {
				UpdateHunters(now, StandingPrey, "last team standing: after the nearest enemy");
			}
			if (now % 60 != 0) {
				return;
			}
			int left = 0;
			int last = -1;
			for (int side = 0; side < c_Sides; ++side) {
				if (!TeamIn(settings, side)) {
					continue;
				}
				s_ModeRun.Score[side] = TicketsLeft(side);
				if (!s_Out[side] && TicketsLeft(side) == 0 && Sandbox::CountUnits(side) == 0) {
					s_Out[side] = true;
					Say(SideName(side) + " is out!");
				}
				if (!s_Out[side]) {
					++left;
					last = side;
				}
			}
			if (left == 1) {
				EndGame(last, SideName(last) + " is the last team standing!");
			} else if (left == 0) {
				EndGame(-1, "Nobody is left standing: a draw");
			}
		}

		void StandingPanel(BattleModeSettings& setup, bool& changed) {
			changed |= ImGui::SliderInt("Tickets", &setup.Tickets, 5, 500, "%d units a team");
			ImGui::SetItemTooltip("Units each team gets in all, the first lot counted. Once a team's are spent and its last unit falls, it's out. The last team in wins.");
		}

		void StandingDraw(bool running) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();
			const BattleModeSettings& settings = running ? s_ModeRun.Settings : s_ModeSetup;
			DrawBases(drawList, settings);
			if (running) {
				std::string line = "LAST TEAM STANDING";
				for (int side = 0; side < c_Sides; ++side) {
					if (TeamIn(settings, side)) {
						line += "    " + SideName(side) + (s_Out[side] ? " out" : " " + std::to_string(TicketsLeft(side) + Sandbox::CountUnits(side)) + " left");
					}
				}
				DrawScore(line);
			}
		}

		std::string StandingStatus(int side) { return s_Out[side] ? std::string("out") : std::to_string(TicketsLeft(side)) + " tickets left, " + std::to_string(Sandbox::CountUnits(side)) + " in"; }

		// ---- VIP hunt ----

		struct Vip {
			UnitRef Unit;
			bool Has = false; //!< Its team has one (Unit, while it lives).
			Vector Pos; //!< Where it was last seen.
			long long NextAt = 0; //!< The sim update its team gets a new one on, once it has fallen.
		};
		std::array<Vip, c_Sides> s_Vips;

		/// Where a team's VIP keeps to: its point, if placed in its base, else the middle of its base.
		Vector VipSpot(const BattleModeSettings& settings, int side) {
			const std::vector<Vector>& base = settings.Bases[side];
			if (settings.HasPoint[side] && IsInZone(base, settings.Points[side])) {
				return settings.Points[side];
			}
			return ZoneMiddle(base);
		}

		/// The VIPs' glow off, and nobody a VIP.
		void ClearVips() {
			for (Vip& vip: s_Vips) {
				if (Actor* unit = GetRef(vip.Unit)) {
					unit->SetHighlighted(false);
				}
				vip = Vip();
			}
		}

		void VipStart() { ClearVips(); }

		/// A team's new units: some stay as the VIP's bodyguards, the rest hunt an enemy's.
		void VipUnitsMade(int side, const std::vector<Actor*>& wave) {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			const float radius = GuardRadius(settings.Bases[side]);
			const BattleSettings guard = PostAt(VipSpot(settings, side), radius * 0.6F, radius * 0.6F + 250.0F, 30);
			std::vector<Actor*> hunters;
			for (Actor* unit: wave) {
				if (Random01() * 100.0F < static_cast<float>(settings.GuardPercent)) {
					unit->SetOrderAttack(false);
					MakeDefender(unit, guard);
				} else {
					hunters.push_back(unit);
				}
			}
			MakeHunters(side, hunters);
		}

		Actor* LiveVip(int side) {
			Actor* unit = side >= 0 && side < c_Sides ? GetRef(s_Vips[side].Unit) : nullptr;
			return unit && IsCombatant(unit) ? unit : nullptr;
		}

		/// A hunter goes for the VIP of the team it was sent after, or the nearest enemy VIP while that team has none, or else the nearest enemy.
		Actor* VipPrey(const FlagRunner& runner, const Actor* unit, const std::vector<Actor*>& fighters) {
			if (Actor* vip = LiveVip(runner.Target)) {
				return vip;
			}
			Actor* nearest = nullptr;
			float nearestDistance = 0.0F;
			for (int side = 0; side < c_Sides; ++side) {
				Actor* vip = side != unit->GetTeam() ? LiveVip(side) : nullptr;
				if (!vip) {
					continue;
				}
				float distance = g_SceneMan.ShortestDistance(unit->GetPos(), vip->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
				if (!nearest || distance < nearestDistance) {
					nearest = vip;
					nearestDistance = distance;
				}
			}
			return nearest ? nearest : NearestEnemy(unit, fighters);
		}

		/// One of a team's units made its VIP: the one nearest its spot (not one you are playing). It glows, and keeps to its base.
		void PickVip(int side, const std::vector<Actor*>& fighters) {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			const Vector spot = VipSpot(settings, side);
			Actor* pick = nullptr;
			float nearest = 0.0F;
			for (Actor* fighter: fighters) {
				if (fighter->GetTeam() != side || fighter->IsPlayerControlled()) {
					continue;
				}
				float distance = g_SceneMan.ShortestDistance(fighter->GetPos(), spot, g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
				if (!pick || distance < nearest) {
					pick = fighter;
					nearest = distance;
				}
			}
			if (!pick) {
				return;
			}
			Vip& vip = s_Vips[side];
			vip.Unit = MakeRef(pick);
			vip.Has = true;
			vip.Pos = pick->GetPos();
			pick->SetHighlighted(true);
			s_Runners.erase(pick->GetUniqueID());
			pick->SetOrderAttack(false);
			const float radius = GuardRadius(settings.Bases[side]);
			MakeDefender(pick, PostAt(spot, radius * 0.5F, 60.0F, 60));
			Say(SideName(side) + " has a VIP");
		}

		/// Each update the VIPs are followed; every six, a fallen one is scored to the enemy team nearest it, and a team due one gets it.
		void VipUpdate(bool aiPaused) {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			if (aiPaused) {
				for (Vip& vip: s_Vips) {
					++vip.NextAt;
				}
				return;
			}
			const long long now = g_TimerMan.GetSimUpdateCount();
			bool anyAlive = false;
			for (Vip& vip: s_Vips) {
				if (Actor* unit = GetRef(vip.Unit)) {
					vip.Pos = unit->GetPos();
					anyAlive |= IsCombatant(unit);
				}
			}
			g_PostProcessMan.GetLightingSettings().HighlightUnits = anyAlive;
			if (s_ModeRun.Over) {
				return;
			}
			if (now % 6 == 3) {
				const std::vector<Actor*> fighters = Fighters();
				for (int side = 0; side < c_Sides; ++side) {
					if (!TeamIn(settings, side)) {
						continue;
					}
					Vip& vip = s_Vips[side];
					if (vip.Has && !LiveVip(side)) {
						if (Actor* unit = GetRef(vip.Unit)) {
							unit->SetHighlighted(false);
						}
						vip.Unit = MakeRef(nullptr);
						vip.Has = false;
						vip.NextAt = now + static_cast<long long>(static_cast<float>(std::max(settings.VipRespawnSeconds, 0)) * UpdatesPerSecond());
						// (Scored to whoever is nearest where it fell: the one most likely to have done it.)
						int killer = -1;
						float nearest = 0.0F;
						for (const Actor* fighter: fighters) {
							float distance = g_SceneMan.ShortestDistance(fighter->GetPos(), vip.Pos, g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
							if (fighter->GetTeam() != side && (killer < 0 || distance < nearest)) {
								killer = fighter->GetTeam();
								nearest = distance;
							}
						}
						if (killer < 0) {
							Say(SideName(side) + "'s VIP fell");
							continue;
						}
						++s_ModeRun.Score[killer];
						if (settings.KillsToWin > 0 && s_ModeRun.Score[killer] >= settings.KillsToWin) {
							EndGame(killer, SideName(killer) + " wins the VIP hunt!");
							return;
						}
						Say(SideName(killer) + " got " + SideName(side) + "'s VIP");
					} else if (!vip.Has && now >= vip.NextAt) {
						PickVip(side, fighters);
					}
				}
			}
			if (now % 30 == 15) {
				UpdateHunters(now, VipPrey, "VIP hunt: after their VIP");
			}
		}

		void VipPanel(BattleModeSettings& setup, bool& changed) {
			changed |= ImGui::SliderInt("VIPs to win", &setup.KillsToWin, 0, 20, setup.KillsToWin > 0 ? "%d" : "play on");
			ImGui::SetItemTooltip("The first team to bring down this many enemy VIPs wins. 0: it goes on till you stop it.");
			changed |= ImGui::SliderInt("Bodyguards", &setup.GuardPercent, 0, 90, "%d%% of each team");
			ImGui::SetItemTooltip("The share of each team's units that stay round its VIP. The rest hunt the enemy's.");
			changed |= ImGui::SliderInt("New VIP after", &setup.VipRespawnSeconds, 0, 120, "%d s");
			ImGui::SetItemTooltip("Seconds after a team's VIP falls before one of its units takes its place.");
		}

		void VipDraw(bool running) {
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();
			const BattleModeSettings& settings = running ? s_ModeRun.Settings : s_ModeSetup;
			DrawBases(drawList, settings);
			for (int side = 0; side < c_Sides; ++side) {
				if (!settings.Plays[side] || settings.Bases[side].size() < 3) {
					continue;
				}
				if (!running) {
					if (settings.HasPoint[side]) {
						ImVec2 at = ToScreen(settings.Points[side]);
						drawList->AddCircle(ImVec2(at.x, at.y - 10.0F), 10.0F, c_SideColors[side], 20, 2.0F);
						drawList->AddText(ImVec2(at.x - 10.0F, at.y - 34.0F), c_SideColors[side], "VIP");
					}
					continue;
				}
				// A crown over the head of each team's VIP, in its colour.
				if (Actor* vip = LiveVip(side)) {
					ImVec2 top = ToScreen(vip->GetPos() - Vector(0.0F, vip->GetHeight() * 0.5F + 14.0F));
					const float bob = 2.0F * Pulse(3.0F);
					ImVec2 crown[5] = {ImVec2(top.x - 9.0F, top.y - bob), ImVec2(top.x - 9.0F, top.y - 10.0F - bob), ImVec2(top.x - 3.0F, top.y - 5.0F - bob), ImVec2(top.x + 3.0F, top.y - 12.0F - bob), ImVec2(top.x + 9.0F, top.y - 10.0F - bob)};
					drawList->AddConvexPolyFilled(crown, 5, c_SideColors[side]);
					drawList->AddRectFilled(ImVec2(top.x - 9.0F, top.y - 2.0F - bob), ImVec2(top.x + 9.0F, top.y + 2.0F - bob), c_SideColors[side]);
					drawList->AddText(ImVec2(top.x - 10.0F, top.y - 28.0F - bob), IM_COL32(255, 255, 255, 230), "VIP");
				}
			}
			if (running) {
				std::string line = Scores("VIP HUNT", "") + (settings.KillsToWin > 0 ? "    (first to " + std::to_string(settings.KillsToWin) + ")" : "");
				for (int side = 0; side < c_Sides; ++side) {
					if (TeamIn(settings, side) && !s_Vips[side].Has && s_Vips[side].NextAt > g_TimerMan.GetSimUpdateCount()) {
						line += "    " + SideName(side) + "'s new VIP in " + std::to_string(static_cast<int>(std::ceil(static_cast<float>(s_Vips[side].NextAt - g_TimerMan.GetSimUpdateCount()) / UpdatesPerSecond()))) + " s";
					}
				}
				DrawScore(line);
			}
		}

		std::string VipStatus(int side) { return std::to_string(s_ModeRun.Score[side]) + " VIPs got, " + (LiveVip(side) ? "VIP up, " : "no VIP, ") + std::to_string(Sandbox::CountUnits(side)) + " in"; }

		/// Every mode's glows off: capture the flag's carriers, and the VIPs.
		void ClearHighlights() {
			ClearCarriers();
			ClearVips();
		}

		/// One of the Battle Director's modes: what it is called and asks for, and its rules. Any of the rules can be left out.
		struct BattleModeInfo {
			const char* Name;
			const char* Blurb; //!< What the game is, for the top of its panel.
			const char* PointName = nullptr; //!< What each team's point in its base is to it, as "flag"; nullptr for none.
			const char* ZoneName = nullptr; //!< What its own zones are, as "hill"; nullptr for none.
			int MinTeams = 2;
			int MinZones = 0;
			/// Whatever else its game needs set up to start, and if it isn't, why.
			bool (*Ready)(const BattleModeSettings& setup, std::string& why) = nullptr;
			/// The settings a team in the game plays by (BaseTeam, unless the mode wants otherwise), from its card's.
			BattleSettings (*TeamSettings)(const BattleModeSettings& settings, int side, const BattleSettings& card) = BaseTeam;
			void (*Start)() = nullptr; //!< Its own state afresh, as the game starts.
			void (*SettingsChanged)() = nullptr; //!< The panel changed while the game is on.
			void (*UnitsMade)(int side, const std::vector<Actor*>& wave) = nullptr; //!< A team's new units, bought but not yet in (riding in, or about to appear).
			Vector (*SpawnSpot)(int side, const std::vector<Vector>& zone, const Actor* unit) = nullptr; //!< Where in a spawn zone a team's new unit appears (spread out, if left out).
			int (*Room)(int side, int room) = nullptr; //!< How many more units a team may have now, of the room its unit limit leaves.
			void (*Update)(bool aiPaused) = nullptr; //!< Each sim update, while its game is on.
			void (*Panel)(BattleModeSettings& setup, bool& changed) = nullptr; //!< Its own choices, on the Battle tab.
			void (*Draw)(bool running) = nullptr; //!< On the map: its bases and zones while set up (running false), and its game while on.
			std::string (*Status)(int side) = nullptr; //!< How a team is getting on, for its row on the Battle tab while the game is on.
		};

		const BattleModeInfo c_Modes[] = {
		    {.Name = "Custom", .Blurb = "", .MinTeams = 0, .TeamSettings = nullptr},
		    {.Name = "Capture the flag",
		     .Blurb = "Each team has a base, drawn on the map, that its units appear in (away from its flag), with its flag inside. Its units go for the enemy's flag and "
		              "bring it back into their own base, while some stay to guard theirs. A flag can only be captured while the team's own is at home. "
		              "A carrier glows, and if it falls it drops the flag, which glows too: an enemy can pick it up, or one of its own team touch it to "
		              "send it home (it goes home by itself after the seconds set). A flag is never lost: one that falls off the map, gets buried or "
		              "lands where nobody can reach goes home.",
		     .PointName = "flag",
		     .TeamSettings = FlagsTeam,
		     .Start = FlagsStart,
		     .SettingsChanged = FlagsSettingsChanged,
		     .UnitsMade = FlagsUnitsMade,
		     .SpawnSpot = FlagsSpawnSpot,
		     .Update = FlagsUpdate,
		     .Panel = FlagsPanel,
		     .Draw = FlagsDraw,
		     .Status = FlagsStatus},
		    {.Name = "King of the hill",
		     .Blurb = "Draw a hill (or a few) on the map and a base for each team. Every unit fights for the hill: a team scores a second for every second it alone "
		              "has units on it, and the first to the seconds set wins. With more than one hill, it can move on every so often, and everyone has to run for "
		              "the next.",
		     .ZoneName = "hill",
		     .MinZones = 1,
		     .Start = HillStart,
		     .SettingsChanged = HillSettingsChanged,
		     .UnitsMade = HillUnitsMade,
		     .SpawnSpot = HillSpawnSpot,
		     .Update = HillUpdate,
		     .Panel = HillPanel,
		     .Draw = HillDraw,
		     .Status = HillStatus},
		    {.Name = "Assault",
		     .Blurb = "One team attacks a line of objectives, drawn in order on the map, and the rest defend them. The attackers take an objective by standing in it "
		              "with no defender there for the seconds set, then go on to the next, and every one taken buys them more time. They win by taking the last; "
		              "the defenders win if the clock runs out first.",
		     .ZoneName = "objective",
		     .MinZones = 1,
		     .Ready = AssaultReady,
		     .Start = AssaultStart,
		     .SettingsChanged = AssaultSettingsChanged,
		     .UnitsMade = AssaultUnitsMade,
		     .SpawnSpot = AssaultSpawnSpot,
		     .Update = AssaultUpdate,
		     .Panel = AssaultPanel,
		     .Draw = AssaultDraw,
		     .Status = AssaultStatus},
		    {.Name = "Last team standing",
		     .Blurb = "Every team gets so many units in all (its tickets), and they hunt down the nearest enemy wherever it is. Fallen units are replaced while a "
		              "team has tickets left; once they're spent and its last unit falls, it's out. The last team in wins.",
		     .Start = StandingStart,
		     .UnitsMade = MakeHunters,
		     .Room = StandingRoom,
		     .Update = StandingUpdate,
		     .Panel = StandingPanel,
		     .Draw = StandingDraw,
		     .Status = StandingStatus},
		    {.Name = "VIP hunt",
		     .Blurb = "One unit of each team is its VIP: it glows, wears a crown, and keeps to its base with bodyguards round it. The rest of the team hunt the enemy "
		              "VIPs. Bring one down and your team scores, and theirs gets a new VIP a few seconds later. The first to the VIPs set wins.",
		     .PointName = "VIP",
		     .Start = VipStart,
		     .UnitsMade = VipUnitsMade,
		     .Update = VipUpdate,
		     .Panel = VipPanel,
		     .Draw = VipDraw,
		     .Status = VipStatus},
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
			if (mode.ZoneName && static_cast<int>(setup.Zones.size()) < mode.MinZones) {
				why = std::string("Draw ") + (mode.MinZones > 1 ? std::to_string(mode.MinZones) + " " + mode.ZoneName + "s" : std::string("a ") + mode.ZoneName) + " on the map.";
				return false;
			}
			return !mode.Ready || mode.Ready(setup, why);
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
			ClearHighlights();
			s_ModeRun.Running = false;
			s_ModeRun.Over = false;
			for (BattleTeam& team: s_BattleTeams) {
				team.Running = false;
			}
		}
		std::array<std::vector<Vector>, c_Sides> s_ScriptBaseDrafts; //!< The corners of a base a script is putting down, one SandboxDo at a time.
		std::vector<Vector> s_ScriptZoneDraft; //!< The corners of a mode zone a script is putting down.
		constexpr size_t c_MaxModeZones = 8;

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

		/// One of the mode's zones drawn (a hill, the next objective): sent, with the tool kept in hand for another.
		void ZoneDrawn(const std::vector<Vector>& zone) {
			if (s_ModeSetup.Zones.size() < c_MaxModeZones) {
				s_ModeSetup.Zones.push_back(zone);
				SendBattleMode();
			}
			if (s_ModeSetup.Zones.size() >= c_MaxModeZones) {
				PutDownBattleTool();
			}
		}

		/// A base or a mode zone closed, by the tool in hand.
		void ModeZoneClosed(const std::vector<Vector>& zone) {
			if (CurrentTool().Kind == Tool::BattleModeZone) {
				ZoneDrawn(zone);
			} else {
				BaseDrawn(zone);
			}
		}
	} // namespace

	/// The next corner of the base or mode zone being drawn (s_ZoneDraft), or it closed, with one on its first corner: true then.
	bool ModeBaseCorner(const Vector& position, float closeWithin) {
		BattleSettings made;
		if (!AddZoneCorner(s_ZoneDraft, made, position, closeWithin) || made.SpawnZones.empty()) {
			return false;
		}
		ModeZoneClosed(made.SpawnZones.front());
		return true;
	}

	/// The base or mode zone being drawn closed (Enter), if it has three corners or more.
	bool CloseModeBase() {
		BattleSettings made;
		if (!CloseSpawnZone(s_ZoneDraft, made) || made.SpawnZones.empty()) {
			return false;
		}
		ModeZoneClosed(made.SpawnZones.front());
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
		} else if (stroke.Kind == Tool::BattleModeZone) {
			// From a script's SandboxDo: one corner a call, closed with one on its first corner, and added to the mode's zones.
			BattleSettings made;
			if (AddZoneCorner(s_ScriptZoneDraft, made, stroke.Position, 20.0F) && !made.SpawnZones.empty() && s_ModeSetup.Zones.size() < c_MaxModeZones) {
				s_ModeSetup.Zones.push_back(made.SpawnZones.front());
				s_ModeRun.Settings.Zones = s_ModeSetup.Zones;
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
			s_ModeRun.Result.clear();
			s_BattleDefenders.clear();
			ClearHighlights();
			s_Runners.clear();
			StartRespawns();
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
				// (The whole team in at the start, not a few at a time.)
				team.FillFirst = team.Running;
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
		// Route variety: that share of the units each get a taste in routes of their own; the rest take the shortest way.
		if (s_ModeRun.Running && s_ModeRun.Settings.RouteVariety > 0) {
			for (Actor* unit: wave) {
				if (unit && RandomNum<int>(0, 99) < s_ModeRun.Settings.RouteVariety) {
					unit->SetRouteSeed(static_cast<unsigned>(RandomNum<int>(1, 1 << 30)));
				}
			}
		}
		if (const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode); s_ModeRun.Running && mode.UnitsMade) {
			mode.UnitsMade(side, wave);
		}
	}

	Vector ModeSpawnSpot(int side, const std::vector<Vector>& zone, const Actor* unit) {
		const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode);
		return mode.SpawnSpot ? mode.SpawnSpot(side, zone, unit) : SpreadSpot(zone, unit ? unit->GetHeight() : 0.0F, nullptr);
	}

	int ModeRoom(int side, int room) {
		if (!s_ModeRun.Running || side < 0 || side >= c_Sides) {
			return room;
		}
		// Only as many as have been given back by the respawn time, of those fallen.
		room = std::min(room, s_Released[side] - s_BattleTeams[side].Sent);
		const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode);
		return mode.Room ? mode.Room(side, room) : room;
	}

	void UpdateBattleMode(bool aiPaused) {
		if (!s_ModeRun.Running) {
			return;
		}
		UpdateRespawns(aiPaused);
		if (const BattleModeInfo& mode = ModeOf(s_ModeRun.Settings.Mode); mode.Update) {
			mode.Update(aiPaused);
		}
	}

	/// A new game: no mode's game is on, and the points set (on the last game's scene) are gone. The mode chosen stays.
	void ForgetBattleMode() {
		ClearHighlights();
		const BattleMode chosen = s_ModeSetup.Mode;
		s_ModeRun = BattleModeRun();
		s_ModeSetup.HasPoint.fill(false);
		for (int side = 0; side < c_Sides; ++side) {
			s_ModeSetup.Bases[side].clear();
			s_ScriptBaseDrafts[side].clear();
		}
		s_ModeSetup.Zones.clear();
		s_ScriptZoneDraft.clear();
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
		ImGui::SetItemTooltip("%s", ready ? "The game starts afresh, with the scores back to nothing." : why.c_str());
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
		ImGui::SetItemTooltip("Most units each team has alive at once.");
		changed |= ImGui::SliderInt("Respawn after", &setup.RespawnSeconds, 0, 60, setup.RespawnSeconds > 0 ? "%d s" : "at once");
		ImGui::SetItemTooltip("Seconds after one of a team's units falls before another comes in its place.");
		changed |= ImGui::SliderInt("Route variety", &setup.RouteVariety, 0, 100, setup.RouteVariety > 0 ? "%d%% go their own way" : "all take the shortest way");
		ImGui::SetItemTooltip("The share of each team's units that each pick a way of their own to where they're going, so a team spreads over the routes across the map rather than filing down the one. At 50%% half take the shortest way and the rest spread over the others that are near enough as short (up to about half as long again). New units only: those already in keep their way.");
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
		ToolUI::Checkbox("Show bases on the map", &s_ShowModeBases);
		ImGui::SetItemTooltip("The outline and shading of each team's base. Off, they're hidden (still shown while you draw a base or place its point); flags, hills and the rest still show.");
		if (mode.Panel) {
			mode.Panel(setup, changed);
		}

		if (mode.ZoneName) {
			const std::string zone = mode.ZoneName;
			std::string title = zone + "s";
			title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
			ImGui::SeparatorText(title.c_str());
			ImGui::PushID("zones");
			ImGui::BeginDisabled(running);
			const bool drawing = CurrentTool().Kind == Tool::BattleModeZone;
			ImGui::BeginDisabled(!drawing && setup.Zones.size() >= c_MaxModeZones);
			if (ToolUI::Button(drawing ? "Done##zone" : ("Draw a " + zone + "##zone").c_str())) {
				if (drawing) {
					PutDownBattleTool();
				} else {
					TakeBattleTool(Tool::BattleModeZone, s_BattleEditTeam);
				}
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("%s", drawing ? "Click the corners on the map, then click the first corner again (or press Enter) to close it. Then draw the next, or Done." : ("Then click out the corners of a " + zone + " on the map. Up to " + std::to_string(c_MaxModeZones) + ".").c_str());
			ImGui::SameLine();
			ImGui::BeginDisabled(setup.Zones.empty());
			if (ToolUI::Button("Take back the last##zone")) {
				setup.Zones.pop_back();
				changed = true;
			}
			ImGui::SameLine();
			if (ToolUI::Button("Clear##zone")) {
				setup.Zones.clear();
				changed = true;
			}
			ImGui::EndDisabled();
			ImGui::EndDisabled();
			if (running) {
				ImGui::SetItemTooltip("Stop the game to change them.");
			}
			ImGui::SameLine();
			if (setup.Zones.empty()) {
				ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.4F, 1.0F), "none yet");
			} else {
				ImGui::TextDisabled("%d drawn", static_cast<int>(setup.Zones.size()));
			}
			ImGui::PopID();
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
					ImGui::TextDisabled("%s", mode.Status ? mode.Status(side).c_str() : (std::to_string(Sandbox::CountUnits(side)) + " in").c_str());
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
		const Tool held = CurrentTool().Kind;
		if (s_ModeRun.Running && s_ModeRun.Settings.Mode == s_ModeSetup.Mode) {
			mode.Draw(true);
		} else if ((Sandbox::IsOpen() && s_CurrentTab == "Battle") || held == Tool::BattleModePoint || held == Tool::BattleModeBase || held == Tool::BattleModeZone) {
			mode.Draw(false);
		}
		if (held == Tool::BattleModeBase || held == Tool::BattleModeZone) {
			DrawZoneDraft(ImGui::GetBackgroundDrawList(), std::max(ScenePixelsPerWindowPixel(), 0.01F));
		}
	}
} // namespace SandboxDetail

void Sandbox::StartBattleMode(int mode, int teamSize, bool byShip) {
	using namespace SandboxDetail;
	if (!InGame()) {
		return;
	}
	BattleModeSettings settings = s_ModeRun.Settings;
	settings.Mode = static_cast<BattleMode>(std::clamp(mode, 0, static_cast<int>(BattleMode::Count) - 1));
	settings.TeamSize = std::clamp(teamSize, 2, 100);
	settings.ByShip = byShip;
	for (int side = 0; side < c_Sides; ++side) {
		settings.Plays[side] = settings.Bases[side].size() >= 3;
	}
	s_ModeSetup = settings;
	Stroke stroke;
	stroke.Kind = Tool::BattleTeam;
	stroke.Team = -1;
	stroke.Count = settings.Mode == BattleMode::Custom ? BattleModeStop : BattleModeStart;
	stroke.Mode = settings;
	ApplyBattleMode(stroke);
}
