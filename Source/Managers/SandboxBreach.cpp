// Breach: one order that digs through to a place and then attacks. The units with a digger that cuts the way tunnel to the point (as Dig to);
// the rest follow the nearest digger in through the tunnel. Once a digger is through, all of them go for the nearest enemy there, or fight
// their way to the point if there's none in sight. (Ctrl+X, or the order ring's More... page.)

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		constexpr float c_ThroughReach = 70.0F; //!< How near the point a digger is to have broken through.
		constexpr float c_StoppedReach = 160.0F; //!< A digger that stopped digging this near the point counts as through too.

		struct Breach {
			std::vector<UnitRef> Diggers;
			std::vector<UnitRef> Followers;
			Vector Point;
			int Team = -1;
			bool Open = false;
			long long OpenAt = 0; //!< The sim update it broke through, for its marker to fade.
		};
		std::vector<Breach> s_Breaches;

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		float Distance(const Vector& from, const Vector& to) { return g_SceneMan.ShortestDistance(from, to, g_SceneMan.SceneWrapsX()).GetMagnitude(); }

		/// A unit taken out of any breach it was in (it's been given another).
		void TakeOut(const Actor* unit) {
			for (Breach& breach: s_Breaches) {
				for (std::vector<UnitRef>* refs: {&breach.Diggers, &breach.Followers}) {
					std::erase_if(*refs, [unit](const UnitRef& ref) { return RefersTo(ref, unit); });
				}
			}
		}

		void FollowDigger(Actor* unit, Actor* digger) {
			HoldUnit(unit);
			unit->SetAIMode(Actor::AIMODE_SQUAD);
			unit->AddAIMOWaypoint(digger);
			unit->SetMovePathToUpdate();
		}

		/// Still at it: a digger still digging to the point, a follower still following.
		bool StillDigging(const Actor* unit, const Breach& breach) { return unit->IsDiggingTo() && Distance(unit->GetOrderDigTarget(), breach.Point) < 2.0F; }
		bool StillFollowing(const Actor* unit) { return unit->GetAIMode() == Actor::AIMODE_SQUAD; }

		/// Through: everyone left goes for the nearest enemy at the point, or fights their way to the point.
		void OpenBreach(Breach& breach, Actor* through) {
			breach.Open = true;
			breach.OpenAt = g_TimerMan.GetSimUpdateCount();
			std::vector<Actor*> units;
			for (std::vector<UnitRef>* refs: {&breach.Diggers, &breach.Followers}) {
				for (const UnitRef& ref: *refs) {
					if (Actor* unit = GetRef(ref); unit && IsCombatant(unit) && !unit->IsPlayerControlled()) {
						units.push_back(unit);
					}
				}
			}
			if (through) {
				through->Say("BreachOpen");
			}
			if (Actor* enemy = EnemyNear(breach.Point, breach.Team)) {
				for (Actor* unit: units) {
					unit->SetMovementRule(Actor::MOVE_ENGAGE);
					SendUnit(unit, enemy->GetPos(), enemy, true, "breach open", true, true);
				}
			} else if (!units.empty()) {
				MoveUnitsTo(units, breach.Point, true);
			}
			if (breach.Team == CommandedTeam()) {
				AddAlert("breach", "Breach open: they're through", breach.Point, c_CommandModeColors[static_cast<int>(CommandMode::Breach)]);
			}
		}
	} // namespace

	void ForgetBreaches() { s_Breaches.clear(); }

	void SaveBreaches(std::ostream& out) {
		const long long now = g_TimerMan.GetSimUpdateCount();
		out << ' ' << s_Breaches.size();
		for (const Breach& breach: s_Breaches) {
			SaveRefs(out, breach.Diggers);
			SaveRefs(out, breach.Followers);
			out << ' ' << breach.Point.m_X << ' ' << breach.Point.m_Y << ' ' << breach.Team << ' ' << breach.Open << ' ' << now - breach.OpenAt;
		}
	}

	void LoadBreaches(std::istream& in, const LoadedIDs& ids) {
		const long long now = g_TimerMan.GetSimUpdateCount();
		size_t count = 0;
		in >> count;
		for (size_t i = 0; i < count && in; ++i) {
			Breach breach;
			long long ago = 0;
			breach.Diggers = LoadRefs(in, ids);
			breach.Followers = LoadRefs(in, ids);
			in >> breach.Point.m_X >> breach.Point.m_Y >> breach.Team >> breach.Open >> ago;
			breach.OpenAt = now - ago;
			if (!breach.Diggers.empty() || !breach.Followers.empty()) {
				s_Breaches.push_back(std::move(breach));
			}
		}
	}

	void LeaveBreaches(const Actor* unit) { TakeOut(unit); }

	void BreachSelected(const std::vector<Actor*>& units, const Vector& point) {
		std::vector<Actor*> sent;
		for (Actor* unit: units) {
			if (!unit->IsPlayerControlled() && !dynamic_cast<const ACraft*>(unit)) {
				sent.push_back(unit);
			}
		}
		if (sent.empty()) {
			return;
		}
		for (Actor* unit: sent) {
			TakeOut(unit);
		}
		const std::vector<DigPlan> plans = DigPlansFor(sent, point);
		Breach breach;
		breach.Point = point;
		breach.Team = sent.front()->GetTeam();
		std::vector<Actor*> diggers;
		std::vector<Actor*> others;
		for (size_t i = 0; i < sent.size(); ++i) {
			(plans[i].Result == DigPlan::Ok ? diggers : others).push_back(sent[i]);
		}
		if (diggers.empty()) {
			// No one can cut the way: nobody goes, and the markers say why.
			for (size_t i = 0; i < sent.size(); ++i) {
				AddNoRoute(sent[i], point, Actor::OrderFailText(DigFailReason(plans[i]), plans[i].BlockingMaterial), true);
			}
			return;
		}
		for (Actor* digger: diggers) {
			SendUnit(digger, point, nullptr, false, "breach");
			digger->SetOrderDigTarget(point);
			// (As a dig-to: it keeps digging, shooting back only as its weapons rule lets it.)
			digger->SetMovementRule(Actor::MOVE_ONLY);
			breach.Diggers.push_back(MakeRef(digger));
		}
		for (Actor* unit: others) {
			Actor* nearest = diggers.front();
			for (Actor* digger: diggers) {
				if (Distance(unit->GetPos(), digger->GetPos()) < Distance(unit->GetPos(), nearest->GetPos())) {
					nearest = digger;
				}
			}
			FollowDigger(unit, nearest);
			breach.Followers.push_back(MakeRef(unit));
		}
		s_Breaches.push_back(breach);
		MarkOrder(point, c_CommandModeColors[static_cast<int>(CommandMode::Breach)]);
	}

	void UpdateBreaches() {
		if (s_Breaches.empty() || g_TimerMan.GetSimUpdateCount() % 15 != 7) {
			return;
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		for (Breach& breach: s_Breaches) {
			if (breach.Open) {
				continue;
			}
			Actor* through = nullptr;
			std::erase_if(breach.Diggers, [&breach, &through](const UnitRef& ref) {
				Actor* unit = GetRef(ref);
				if (!unit || !IsCombatant(unit) || unit->IsPlayerControlled()) {
					return true;
				}
				const float distance = Distance(unit->GetPos(), breach.Point);
				const bool digging = StillDigging(unit, breach);
				if (distance < c_ThroughReach || (!digging && distance < c_StoppedReach)) {
					through = through ? through : unit;
					return false;
				}
				// Stopped short: given another order, or it couldn't get on.
				return !digging;
			});
			std::erase_if(breach.Followers, [](const UnitRef& ref) {
				const Actor* unit = GetRef(ref);
				return !unit || !IsCombatant(unit) || unit->IsPlayerControlled() || !StillFollowing(unit);
			});
			if (through) {
				OpenBreach(breach, through);
			} else if (breach.Diggers.empty()) {
				// No digger got through: those following stop where they are, yours to order again.
				for (const UnitRef& ref: breach.Followers) {
					if (Actor* unit = GetRef(ref)) {
						HoldUnit(unit);
					}
				}
				breach.Followers.clear();
			} else {
				// A follower whose digger fell or was ordered elsewhere follows another of them.
				for (const UnitRef& ref: breach.Followers) {
					Actor* unit = GetRef(ref);
					const MovableObject* leading = unit ? unit->GetAIMOWaypointID() != g_NoMOID ? g_MovableMan.GetMOFromID(unit->GetAIMOWaypointID()) : nullptr : nullptr;
					const bool onADigger = leading && std::any_of(breach.Diggers.begin(), breach.Diggers.end(), [leading](const UnitRef& digger) { return RefersTo(digger, dynamic_cast<const Actor*>(leading)); });
					if (unit && !onADigger) {
						if (Actor* digger = GetRef(breach.Diggers.front())) {
							FollowDigger(unit, digger);
						}
					}
				}
			}
		}
		// An open breach's marker stays a few seconds; one with nobody left in it goes.
		std::erase_if(s_Breaches, [now](const Breach& breach) {
			if (breach.Open) {
				return static_cast<float>(now - breach.OpenAt) / UpdatesPerSecond() > 4.0F;
			}
			return breach.Diggers.empty() && breach.Followers.empty();
		});
	}

	void DrawBreaches(ImDrawList* drawList) {
		if (s_Breaches.empty() || g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		const ImU32 color = c_CommandModeColors[static_cast<int>(CommandMode::Breach)];
		const long long now = g_TimerMan.GetSimUpdateCount();
		for (const Breach& breach: s_Breaches) {
			if (OnlySide() >= 0 && breach.Team != OnlySide()) {
				continue;
			}
			const float age = breach.Open ? static_cast<float>(now - breach.OpenAt) / UpdatesPerSecond() : 0.0F;
			const int alpha = static_cast<int>(255.0F * std::clamp(1.0F - age / 4.0F, 0.0F, 1.0F));
			const ImU32 faded = (color & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
			const ImVec2 at = ToScreen(breach.Point);
			// A burst: a ring with four spikes, the way they'll come through.
			drawList->AddCircleFilled(at, 10.0F, IM_COL32(0, 0, 0, alpha / 2));
			drawList->AddCircle(at, 7.0F, faded, 0, 2.0F);
			for (int i = 0; i < 4; ++i) {
				const float angle = 0.785F + 1.5708F * static_cast<float>(i);
				drawList->AddLine(ImVec2(at.x + std::cos(angle) * 8.0F, at.y + std::sin(angle) * 8.0F), ImVec2(at.x + std::cos(angle) * 13.0F, at.y + std::sin(angle) * 13.0F), faded, 2.0F);
			}
			// The diggers' lines to it, while they're still on the way.
			int diggers = 0;
			for (const UnitRef& ref: breach.Diggers) {
				if (const Actor* unit = GetRef(ref)) {
					++diggers;
					if (!breach.Open) {
						drawList->AddLine(ToScreen(unit->GetPos()), at, (color & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT), 1.0F);
					}
				}
			}
			const std::string text = breach.Open ? "Breach open" : "Breach: " + std::to_string(diggers) + " digging, " + std::to_string(breach.Followers.size()) + " following";
			drawList->AddText(ImVec2(at.x + 16.0F, at.y - ImGui::GetTextLineHeight() * 0.5F), faded, text.c_str());
		}
	}

} // namespace SandboxDetail
