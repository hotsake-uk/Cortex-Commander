// Airlift: the selected units flown across the map. A gunship of their team (one for each six of them, at most three) comes down by them, they
// get in (the first one in takes the controls), and once they're all aboard, or after a while, it flies high to the place clicked, sets
// itself down there and lets them out; then it's taken back up. In Battle Command each ship costs reinforcement points.

#include "SandboxInternal.h"
#include "ACShip.h"

namespace SandboxDetail {
	namespace {
		constexpr const char* c_LiftShip = "Gunship";
		constexpr int c_MostShips = 3;
		constexpr float c_DropHeight = 260.0F; //!< How high over the ground by the units a ship comes in, to set itself down.
		constexpr float c_Cruise = 180.0F; //!< How high over the ground a ship flies to the place, before coming down into it.
		constexpr float c_BoardSeconds = 25.0F; //!< How long a landed ship waits for the rest before it goes with those aboard.
		constexpr float c_GoneSeconds = 3.0F; //!< How long an emptied ship sits before it's taken back up.

		enum class Stage { Landing, Boarding, Flying, Unloaded };

		struct Lift {
			UnitRef Ship;
			std::vector<UnitRef> Units;
			Vector To; //!< On the ground at the place clicked.
			int Team = -1;
			Stage At = Stage::Landing;
			long long Since = 0; //!< The sim update the stage began.
			Vector SentTo; //!< Where it was last flown to.
		};
		std::vector<Lift> s_Lifts;

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		float Seconds(long long since) { return static_cast<float>(g_TimerMan.GetSimUpdateCount() - since) / UpdatesPerSecond(); }

		Vector Offset(const Vector& from, const Vector& to) { return g_SceneMan.ShortestDistance(from, to, g_SceneMan.SceneWrapsX()); }

		bool PaidFor() { return Sandbox::IsBattleCommand() && s_ModeRun.Running; }

		/// Down, or as good as: low over the ground and slow, for units to get in and out.
		bool Down(ACShip* ship) { return ship->GetAltitude(1000, 4) < ship->GetRadius() * 0.9F && ship->GetVel().MagnitudeIsLessThan(3.0F); }

		bool Aboard(const ACShip* ship, const Actor* unit) {
			for (int seat = 0; seat < ship->GetSeatCount(); ++seat) {
				if (ship->GetSeatOccupant(seat) == unit) {
					return true;
				}
			}
			return false;
		}

		void Advance(Lift& lift, Stage stage) {
			lift.At = stage;
			lift.Since = g_TimerMan.GetSimUpdateCount();
		}

		/// Flies a ship to the place: high over the ground until above it, then down into it. Again when the place to fly to changes, or it stopped
		/// short (its go-to ends in a hold where it was).
		void FlyOn(ACShip* ship, Lift& lift) {
			const float across = std::abs(Offset(ship->GetPos(), lift.To).m_X);
			Vector to = lift.To - Vector(0.0F, ship->GetRadius() * 0.3F);
			if (across > 60.0F) {
				to.m_Y = std::max(std::min(lift.To.m_Y, g_SceneMan.MovePointToGround(ship->GetPos(), 0.0F, 4).m_Y) - c_Cruise, ship->GetRadius() * 1.5F);
			}
			const bool moved = !Offset(lift.SentTo, to).MagnitudeIsLessThan(40.0F);
			const bool stopped = ship->GetAIMode() != Actor::AIMODE_GOTO && !Offset(ship->GetPos(), to).MagnitudeIsLessThan(30.0F);
			if ((moved || stopped) && ship->FlyTo(to)) {
				lift.SentTo = to;
			}
		}
	} // namespace

	void ForgetAirlifts() { s_Lifts.clear(); }

	void SaveAirlifts(std::ostream& out) {
		const long long now = g_TimerMan.GetSimUpdateCount();
		out << ' ' << s_Lifts.size();
		for (const Lift& lift: s_Lifts) {
			out << ' ' << lift.Ship.ID;
			SaveRefs(out, lift.Units);
			out << ' ' << lift.To.m_X << ' ' << lift.To.m_Y << ' ' << lift.Team << ' ' << static_cast<int>(lift.At) << ' ' << now - lift.Since << ' ' << lift.SentTo.m_X << ' ' << lift.SentTo.m_Y;
		}
	}

	void LoadAirlifts(std::istream& in, const LoadedIDs& ids) {
		const long long now = g_TimerMan.GetSimUpdateCount();
		size_t count = 0;
		in >> count;
		for (size_t i = 0; i < count && in; ++i) {
			Lift lift;
			long ship = 0;
			int stage = 0;
			long long ago = 0;
			in >> ship;
			lift.Units = LoadRefs(in, ids);
			in >> lift.To.m_X >> lift.To.m_Y >> lift.Team >> stage >> ago >> lift.SentTo.m_X >> lift.SentTo.m_Y;
			lift.At = static_cast<Stage>(std::clamp(stage, 0, static_cast<int>(Stage::Unloaded)));
			lift.Since = now - ago;
			lift.Ship = ids.Ref(ship);
			if (lift.Ship.Unit) {
				s_Lifts.push_back(std::move(lift));
			}
		}
	}

	int AirliftShipsFor(size_t units) { return std::min(static_cast<int>((units + 5) / 6), c_MostShips); }

	void AirliftSelected(const std::vector<Actor*>& units, const Vector& point) {
		std::vector<Actor*> going;
		for (Actor* unit: units) {
			if (!unit->IsPlayerControlled() && !dynamic_cast<const ACraft*>(unit) && IsCombatant(unit)) {
				going.push_back(unit);
			}
		}
		if (going.empty()) {
			return;
		}
		const int team = going.front()->GetTeam();
		const int ships = AirliftShipsFor(going.size());
		going.resize(std::min(going.size(), static_cast<size_t>(ships * 6)));
		if (PaidFor() && team >= 0 && team < c_Sides) {
			const float cost = static_cast<float>(c_AirliftCost * ships);
			if (s_CommandPoints[team].Points < cost) {
				g_ConsoleMan.PrintString("AIRLIFT: not enough points (" + std::to_string(c_AirliftCost * ships) + " for " + std::to_string(ships) + (ships == 1 ? " ship)." : " ships)."));
				return;
			}
			s_CommandPoints[team].Points -= cost;
			s_CommandPoints[team].Spent += cost;
		}
		const Vector to = g_SceneMan.MovePointToGround(point, 0.0F, 4);
		for (int i = 0; i < ships; ++i) {
			const size_t first = static_cast<size_t>(i) * 6;
			const size_t last = std::min(first + 6, going.size());
			// Down by the middle of its share of them.
			Vector middle = going[first]->GetPos();
			for (size_t u = first + 1; u < last; ++u) {
				middle += Offset(going[first]->GetPos(), going[u]->GetPos()) / static_cast<float>(last - first);
			}
			ACShip* ship = dynamic_cast<ACShip*>(CreateBaseObject("ACShip", c_LiftShip));
			if (!ship) {
				g_ConsoleMan.PrintString(std::string("AIRLIFT: the ") + c_LiftShip + " isn't loaded.");
				return;
			}
			Vector at = g_SceneMan.MovePointToGround(middle, 0.0F, 4) - Vector(0.0F, c_DropHeight);
			at.m_Y = std::max(at.m_Y, ship->GetRadius() * 1.5F);
			g_SceneMan.WrapPosition(at);
			ship->SetPos(at);
			ship->SetTeam(team);
			ship->SetAIMode(Actor::AIMODE_SENTRY);
			// (Its seats are the airlift's: it gives no rides of its own, ACShip::UpdateAIRides.)
			ship->SetNumberValue("NoShipRides", 1.0);
			ActivateSide(team);
			AddObject(ship);
			Lift lift;
			lift.Ship = MakeRef(ship);
			lift.To = to;
			lift.Team = team;
			lift.Since = g_TimerMan.GetSimUpdateCount();
			for (size_t u = first; u < last; ++u) {
				lift.Units.push_back(MakeRef(going[u]));
				// Told to wait where they are for it; sent to it once it's down.
				HoldUnit(going[u]);
				AnswerOrder(going[u], "OrderAirlift");
			}
			s_Lifts.push_back(lift);
		}
		MarkOrder(to, c_CommandModeColors[static_cast<int>(CommandMode::Airlift)]);
	}

	void UpdateAirlifts() {
		if (s_Lifts.empty() || g_TimerMan.GetSimUpdateCount() % 10 != 4) {
			return;
		}
		std::erase_if(s_Lifts, [](Lift& lift) {
			ACShip* ship = dynamic_cast<ACShip*>(GetRef(lift.Ship));
			std::erase_if(lift.Units, [](const UnitRef& ref) {
				const Actor* unit = GetRef(ref);
				return !unit || unit->IsDead() || unit->GetHealth() <= 0.0F;
			});
			if (!ship || ship->IsDead() || ship->GetStatus() == Actor::DYING) {
				if (lift.Team == CommandedTeam()) {
					AddAlert("airlift", "Airlift ship lost", lift.To, IM_COL32(239, 90, 80, 255));
				}
				return true;
			}
			switch (lift.At) {
				case Stage::Landing:
					if (Down(ship)) {
						for (const UnitRef& ref: lift.Units) {
							if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled()) {
								SendUnit(unit, ship->GetPos(), ship, false, "airlift: getting in", false, true);
							}
						}
						Advance(lift, Stage::Boarding);
					} else if (Seconds(lift.Since) > 20.0F) {
						// Never got down (a ledge, the water): no airlift.
						ship->SetToDelete(true);
						return true;
					}
					break;
				case Stage::Boarding: {
					int aboard = 0;
					int waiting = 0;
					for (const UnitRef& ref: lift.Units) {
						if (Actor* unit = GetRef(ref)) {
							if (Aboard(ship, unit)) {
								++aboard;
							} else if (!unit->IsPlayerControlled() && unit->GetMOMoveTarget() == ship) {
								++waiting;
							}
						}
					}
					const bool full = ship->GetFreeSeat() < 0;
					if (ship->HasPilot() && aboard > 0 && (waiting == 0 || full || Seconds(lift.Since) > c_BoardSeconds)) {
						// (Those left behind stop where they are.)
						for (const UnitRef& ref: lift.Units) {
							if (Actor* unit = GetRef(ref); unit && !Aboard(ship, unit) && unit->GetMOMoveTarget() == ship) {
								HoldUnit(unit);
							}
						}
						Advance(lift, Stage::Flying);
						FlyOn(ship, lift);
					} else if (aboard == 0 && waiting == 0 && Seconds(lift.Since) > c_BoardSeconds) {
						ship->SetToDelete(true);
						return true;
					}
					break;
				}
				case Stage::Flying:
					if (!ship->HasPilot()) {
						// The pilot shot out of its seat: it sets itself down wherever it is, and lets the rest out there.
						ship->EjectCrew(false);
						Advance(lift, Stage::Unloaded);
						break;
					}
					if (Offset(ship->GetPos(), lift.To).MagnitudeIsLessThan(ship->GetRadius() * 2.0F) && Down(ship)) {
						ship->EjectCrew(false);
						for (const UnitRef& ref: lift.Units) {
							if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled()) {
								HoldUnit(unit);
							}
						}
						if (Actor* first = lift.Units.empty() ? nullptr : GetRef(lift.Units.front())) {
							first->Say("AirliftLanded");
						}
						if (lift.Team == CommandedTeam()) {
							AddAlert("airlift", "Airlift landed", lift.To, c_CommandModeColors[static_cast<int>(CommandMode::Airlift)]);
						}
						Advance(lift, Stage::Unloaded);
					} else {
						FlyOn(ship, lift);
					}
					break;
				case Stage::Unloaded:
					if (Seconds(lift.Since) > c_GoneSeconds) {
						// Taken back up: gone in a puff of its engines.
						EffectsParticles::Emit("Smoke", ship->GetPos(), Vector(0.0F, -2.0F), 1.0F, 10, 0);
						ship->SetToDelete(true);
						return true;
					}
					break;
			}
			return false;
		});
	}

	void DrawAirlifts(ImDrawList* drawList) {
		if (s_Lifts.empty() || g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		const ImU32 color = c_CommandModeColors[static_cast<int>(CommandMode::Airlift)];
		for (const Lift& lift: s_Lifts) {
			if (OnlySide() >= 0 && lift.Team != OnlySide()) {
				continue;
			}
			const Actor* ship = GetRef(lift.Ship);
			if (!ship) {
				continue;
			}
			const ImVec2 from = ToScreen(ship->GetPos());
			const ImVec2 to = ToScreen(lift.To);
			// A dashed line from the ship to where it's going, and a landing mark there.
			const ImVec2 step(to.x - from.x, to.y - from.y);
			const float length = std::sqrt(step.x * step.x + step.y * step.y);
			for (float d = 0.0F; d < length; d += 14.0F) {
				const float e = std::min(d + 7.0F, length);
				drawList->AddLine(ImVec2(from.x + step.x * d / length, from.y + step.y * d / length), ImVec2(from.x + step.x * e / length, from.y + step.y * e / length), (color & 0x00FFFFFF) | (110u << IM_COL32_A_SHIFT), 1.5F);
			}
			drawList->AddCircle(to, 9.0F, color, 0, 2.0F);
			drawList->AddLine(ImVec2(to.x - 5.0F, to.y), ImVec2(to.x + 5.0F, to.y), color, 2.0F);
			static const char* const stages[] = {"Airlift: coming down", "Airlift: boarding", "Airlift: on the way", "Airlift: landed"};
			drawList->AddText(ImVec2(from.x + 14.0F, from.y - ImGui::GetTextLineHeight()), color, stages[static_cast<int>(lift.At)]);
		}
	}
} // namespace SandboxDetail
