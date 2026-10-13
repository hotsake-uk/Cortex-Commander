// Engineer builds: the Build order. Pick a piece (sandbags, a concrete wall, a small bunker or a turret) and click where it goes: the nearest of
// the selected units carrying a Constructor walks there and builds it, standing at it a few seconds. In Battle Command it's paid for from your
// reinforcement points when ordered, and paid back if the builder falls or is sent elsewhere first.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		constexpr float c_BuildReach = 60.0F; //!< How near the site the builder works from.

		struct Site {
			int Piece = 0;
			Vector At; //!< On the ground: the middle of the piece's foot.
			int Team = -1;
			UnitRef Builder;
			float Done = 0.0F; //!< Seconds built so far.
			float Paid = 0.0F; //!< Points paid, for paying back.
			bool Started = false; //!< The builder got there.
		};
		std::vector<Site> s_Sites;

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		float Distance(const Vector& from, const Vector& to) { return g_SceneMan.ShortestDistance(from, to, g_SceneMan.SceneWrapsX()).GetMagnitude(); }

		bool PaidFor() { return Sandbox::IsBattleCommand() && s_ModeRun.Running; }

		bool CarriesConstructor(const Actor* unit) { return unit && unit->HasObject("Constructor"); }

		/// Whether the builder is still on the job: alive, not taken over, and either there or still walking there.
		bool StillOnIt(Actor* unit, const Site& site) {
			if (!unit || !IsCombatant(unit) || unit->IsPlayerControlled()) {
				return false;
			}
			if (Distance(unit->GetPos(), site.At) <= c_BuildReach) {
				return true;
			}
			return !site.Started && unit->GetAIMode() == Actor::AIMODE_GOTO && unit->GetWaypointsSize() > 0 && Distance(unit->GetLastAIWaypoint(), site.At) < 40.0F;
		}

		void PayBack(const Site& site) {
			if (site.Paid > 0.0F && site.Team >= 0 && site.Team < c_Sides) {
				s_CommandPoints[site.Team].Points += site.Paid;
				s_CommandPoints[site.Team].Spent -= site.Paid;
			}
		}

		/// The piece put up at the site.
		void Place(const Site& site) {
			const Vector& at = site.At;
			switch (site.Piece) {
				case 0:
					// Sandbags: a low wall to crouch behind.
					PaintBox(at + Vector(-30.0F, -16.0F), 60, 16, "Sandbag");
					break;
				case 1:
					// A concrete wall, head high.
					PaintBox(at + Vector(-6.0F, -64.0F), 12, 64, "Concrete");
					break;
				case 2:
					// A small bunker: a roof on two walls, open low on both sides to shoot and get in through.
					PaintBox(at + Vector(-56.0F, -64.0F), 112, 8, "Concrete");
					PaintBox(at + Vector(-56.0F, -56.0F), 8, 30, "Concrete");
					PaintBox(at + Vector(48.0F, -56.0F), 8, 30, "Concrete");
					break;
				case 3: {
					// A turret of the team's.
					const Entity* preset = g_PresetMan.GetEntityPreset("ACrab", "TradeStar Turret", "Base.rte");
					Actor* turret = preset ? dynamic_cast<Actor*>(CreateObject(preset->GetClassName(), preset->GetPresetName(), preset->GetModuleID())) : nullptr;
					if (turret) {
						turret->SetPos(at + Vector(0.0F, -12.0F));
						turret->SetTeam(site.Team);
						turret->SetControllerMode(Controller::CIM_AI);
						ActivateSide(site.Team);
						g_MovableMan.AddActor(turret);
					}
					break;
				}
				default:
					break;
			}
		}
	} // namespace

	void ForgetBuilds() { s_Sites.clear(); }

	void SaveBuilds(std::ostream& out) {
		out << ' ' << s_Sites.size();
		for (const Site& site: s_Sites) {
			out << ' ' << site.Piece << ' ' << site.At.m_X << ' ' << site.At.m_Y << ' ' << site.Team << ' ' << site.Builder.ID << ' ' << site.Done << ' ' << site.Paid << ' ' << site.Started;
		}
	}

	void LoadBuilds(std::istream& in, const LoadedIDs& ids) {
		size_t count = 0;
		in >> count;
		for (size_t i = 0; i < count && in; ++i) {
			Site site;
			long builder = 0;
			in >> site.Piece >> site.At.m_X >> site.At.m_Y >> site.Team >> builder >> site.Done >> site.Paid >> site.Started;
			site.Builder = ids.Ref(builder);
			if (site.Builder.Unit) {
				s_Sites.push_back(site);
			}
		}
	}

	Actor* BuilderFor(const std::vector<Actor*>& units, const Vector& point) {
		Actor* best = nullptr;
		for (Actor* unit: units) {
			const bool busy = std::any_of(s_Sites.begin(), s_Sites.end(), [unit](const Site& site) { return RefersTo(site.Builder, unit); });
			if (!busy && !unit->IsPlayerControlled() && CarriesConstructor(unit) && (!best || Distance(unit->GetPos(), point) < Distance(best->GetPos(), point))) {
				best = unit;
			}
		}
		return best;
	}

	void BuildSelected(const std::vector<Actor*>& units, const Vector& point) {
		const int piece = std::clamp(s_BuildPiece, 0, static_cast<int>(std::size(c_BuildPieces)) - 1);
		const Vector at = g_SceneMan.MovePointToGround(point, 0.0F, 4);
		Actor* builder = BuilderFor(units, at);
		if (!builder) {
			g_ConsoleMan.PrintString("BUILD: none of the selected units carries a Constructor (or they're all building already).");
			return;
		}
		const int team = builder->GetTeam();
		float cost = 0.0F;
		if (PaidFor() && team >= 0 && team < c_Sides) {
			cost = static_cast<float>(c_BuildPieces[piece].Cost);
			if (s_CommandPoints[team].Points < cost) {
				g_ConsoleMan.PrintString("BUILD: not enough points for " + std::string(c_BuildPieces[piece].Name) + " (" + std::to_string(c_BuildPieces[piece].Cost) + ").");
				return;
			}
			s_CommandPoints[team].Points -= cost;
			s_CommandPoints[team].Spent += cost;
		}
		SendUnit(builder, at, nullptr, false, "build");
		Site site;
		site.Piece = piece;
		site.At = at;
		site.Team = team;
		site.Builder = MakeRef(builder);
		site.Paid = cost;
		s_Sites.push_back(site);
		MarkOrder(at, c_CommandModeColors[static_cast<int>(CommandMode::Build)]);
	}

	void UpdateBuilds() {
		if (s_Sites.empty() || g_TimerMan.GetSimUpdateCount() % 6 != 1) {
			return;
		}
		const float seconds = 6.0F / UpdatesPerSecond();
		std::erase_if(s_Sites, [seconds](Site& site) {
			Actor* builder = GetRef(site.Builder);
			if (!StillOnIt(builder, site)) {
				PayBack(site);
				if (builder && IsCombatant(builder) && site.Team == CommandedTeam()) {
					AddAlert("build", std::string(c_BuildPieces[site.Piece].Name) + " not built: the builder left it", site.At, IM_COL32(200, 160, 120, 255));
				}
				return true;
			}
			if (Distance(builder->GetPos(), site.At) > c_BuildReach) {
				return false;
			}
			if (!site.Started) {
				// There: it stands and builds.
				site.Started = true;
				HoldUnit(builder);
			}
			site.Done += seconds;
			if (site.Done < static_cast<float>(c_BuildPieces[site.Piece].Seconds)) {
				return false;
			}
			Place(site);
			builder->Say("BuildDone");
			return true;
		});
	}

	void DrawBuilds(ImDrawList* drawList) {
		if (s_Sites.empty() || g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		const float scale = ScenePixelsPerWindowPixel();
		const ImU32 color = c_CommandModeColors[static_cast<int>(CommandMode::Build)];
		for (const Site& site: s_Sites) {
			if (OnlySide() >= 0 && site.Team != OnlySide()) {
				continue;
			}
			const BuildPiece& piece = c_BuildPieces[site.Piece];
			const ImVec2 foot = ToScreen(site.At);
			const ImVec2 from(foot.x - piece.Width * 0.5F / scale, foot.y - piece.Height / scale);
			const ImVec2 to(foot.x + piece.Width * 0.5F / scale, foot.y);
			// The piece's outline, and how far on it is.
			drawList->AddRect(from, to, (color & 0x00FFFFFF) | (170u << IM_COL32_A_SHIFT), 0.0F, 0, 1.5F);
			const float share = std::clamp(site.Done / static_cast<float>(std::max(piece.Seconds, 1)), 0.0F, 1.0F);
			const ImVec2 bar(from.x, from.y - 8.0F);
			const float width = std::max(to.x - from.x, 30.0F);
			drawList->AddRectFilled(bar, ImVec2(bar.x + width, bar.y + 4.0F), IM_COL32(0, 0, 0, 150));
			drawList->AddRectFilled(bar, ImVec2(bar.x + width * share, bar.y + 4.0F), color);
			const std::string text = std::string(piece.Name) + (site.Started ? " (building)" : " (on the way)");
			drawList->AddText(ImVec2(bar.x, bar.y - ImGui::GetTextLineHeight() - 2.0F), color, text.c_str());
		}
	}
} // namespace SandboxDetail
