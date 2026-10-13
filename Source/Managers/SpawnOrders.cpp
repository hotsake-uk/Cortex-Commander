// Spawn orders: a team you command in a battle mode has each new unit given an order as it comes in (move, attack-move, defend at, patrol,
// jetpack here or a battle objective), for the whole team or for one of its spawn zones, rather than standing where it lands for you to
// send. Set on the Command tool's row (SpawnOrderRow) by placing an order as for units, and kept with the mode's settings and presets.

#include "SandboxInternal.h"
#include "ModernHUD.h"

namespace SandboxDetail {
	namespace {
		/// A new unit waiting for its spawn order: made, then in (once it is out of its ship), then given it once it has stood a moment.
		struct Waiting {
			UnitRef Unit;
			int Side = 0;
			long long MadeAt = 0;
			long long InAt = -1;
		};
		std::vector<Waiting> s_Waiting;
		constexpr float c_ReadySeconds = 0.75F; //!< How long a new unit is in before it is given its order: on its feet, out of its ship.
		constexpr float c_GiveUpSeconds = 120.0F; //!< How long one is waited for (a ship shot down with it aboard never lets it out).

		constexpr int c_MaxSpawnZoneOrders = 16; //!< Most spawn zones a team keeps orders for (it has up to 8).

		/// The middle of a zone's corners.
		Vector ZoneCentre(const std::vector<Vector>& zone) {
			Vector sum;
			for (const Vector& corner: zone) {
				sum += corner;
			}
			return zone.empty() ? sum : sum / static_cast<float>(zone.size());
		}

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		/// The battle objective a click at a point is on, for a side: one it goes for or holds, in its zone or near its place (as CommandTargetAt).
		bool ObjectiveClicked(int side, const Vector& point) {
			if (!s_ModeRun.Running || s_ModeRun.Over) {
				return false;
			}
			for (const BattleObjective& objective: BattleObjectives()) {
				if (!objective.Live || !objective.Shown || (!objective.AttackedBy(side) && !objective.DefendedBy(side))) {
					continue;
				}
				const float distance = g_SceneMan.ShortestDistance(point, objective.Pos, g_SceneMan.SceneWrapsX()).GetMagnitude();
				if (objective.Zone.empty() ? distance <= std::clamp(objective.Radius, 16.0F, 40.0F) : IsInZone(objective.Zone, point)) {
					return true;
				}
			}
			return false;
		}

		/// Which way to face, from a point and the place the button was dragged to (as Defend at's and a facing move's).
		int FacingOf(const Vector& point, const Vector& facingPoint) {
			const float across = g_SceneMan.ShortestDistance(point, facingPoint, g_SceneMan.SceneWrapsX()).m_X;
			return across > 12.0F ? 1 : (across < -12.0F ? -1 : 0);
		}

		/// Gives units their spawn order, as the order would be given the selection: with its urgency, and Defend at's zone as it was set.
		void GiveSpawnOrder(int side, const SpawnOrder& order, std::vector<Actor*> units) {
			if (units.empty() || order.Kind == SpawnOrderKind::Hold) {
				return;
			}
			struct Scope {
				float Direct = s_DirectOrder;
				bool Click = s_ClickOrder;
				bool Sky = s_SkyOrder;
				int Radius = s_DefendRadius;
				int Chase = s_DefendChase;
				int Roam = s_DefendRoam;
				~Scope() {
					s_DirectOrder = Direct;
					s_ClickOrder = Click;
					s_SkyOrder = Sky;
					s_DefendRadius = Radius;
					s_DefendChase = Chase;
					s_DefendRoam = Roam;
				}
			} scope;
			s_DirectOrder = order.Direct;
			// (As placed by a click: a move to a point in open air is flown to.)
			s_ClickOrder = true;
			s_SkyOrder = false;
			s_DefendRadius = order.Radius;
			s_DefendChase = order.Chase;
			s_DefendRoam = order.Roam;
			const Vector point = order.Points.empty() ? Vector() : order.Points.front();
			switch (order.Kind) {
				case SpawnOrderKind::Move:
				case SpawnOrderKind::AttackMove:
					if (!order.Points.empty()) {
						MoveUnitsTo(units, point, order.Kind == SpawnOrderKind::AttackMove, order.Facing);
					}
					break;
				case SpawnOrderKind::JetpackHere:
					if (!order.Points.empty()) {
						s_SkyOrder = true;
						MoveUnitsTo(units, point, false, order.Facing);
					}
					break;
				case SpawnOrderKind::DefendAt:
					if (!order.Points.empty()) {
						DefendUnitsAt(units, point, order.Facing);
					}
					break;
				case SpawnOrderKind::Patrol:
					if (order.Points.size() >= 2) {
						PatrolUnits(units, order.Points, order.BackAndForth);
					}
					break;
				case SpawnOrderKind::Objective: {
					// The objective nearest the point clicked (it may have moved: a flag carried, the next assault objective in play), or the unit.
					BattleObjective objective;
					bool defend = false;
					if (BattleObjectiveFor(side, order.Points.empty() ? units.front()->GetPos() : point, objective, defend)) {
						ObjectiveUnits(units, objective, defend);
					} else if (!order.Points.empty()) {
						MoveUnitsTo(units, point, true, order.Facing);
					}
					break;
				}
				default:
					break;
			}
		}
	}

	int SpawnOrderSide() {
		if (int side = CommandedTeam(); side >= 0) {
			return side;
		}
		for (int side = 0; side < c_Sides; ++side) {
			if (s_ModeSetup.PlayerCommands[side] && s_ModeSetup.Plays[side]) {
				return side;
			}
		}
		return -1;
	}

	ImU32 SpawnOrderColor(SpawnOrderKind kind) {
		switch (kind) {
			case SpawnOrderKind::Move:
				return c_CommandModeColors[static_cast<int>(CommandMode::Move)];
			case SpawnOrderKind::AttackMove:
				return c_CommandModeColors[static_cast<int>(CommandMode::AttackMove)];
			case SpawnOrderKind::DefendAt:
				return c_CommandModeColors[static_cast<int>(CommandMode::DefendAt)];
			case SpawnOrderKind::Patrol:
				return c_CommandModeColors[static_cast<int>(CommandMode::Patrol)];
			case SpawnOrderKind::JetpackHere:
				return c_CommandModeColors[static_cast<int>(CommandMode::JetpackHere)];
			case SpawnOrderKind::Objective:
				return c_CommandModeColors[static_cast<int>(CommandMode::Command)];
			default:
				return IM_COL32(200, 200, 200, 255);
		}
	}

	void SpawnOrderUnitsMade(int side, const std::vector<Actor*>& wave) {
		const long long now = g_TimerMan.GetSimUpdateCount();
		for (Actor* unit: wave) {
			if (unit && !dynamic_cast<const ACraft*>(unit)) {
				s_Waiting.push_back({MakeRef(unit), side, now, -1});
			}
		}
	}

	void ForgetSpawnOrderUnits() { s_Waiting.clear(); }

	void SaveSpawnOrderUnits(std::ostream& out) {
		out << ' ' << s_Waiting.size();
		for (const Waiting& waiting: s_Waiting) {
			out << ' ' << waiting.Unit.ID << ' ' << waiting.Side;
			SaveUpdate(out, waiting.MadeAt);
			SaveUpdate(out, waiting.InAt);
		}
	}

	void LoadSpawnOrderUnits(std::istream& in, const LoadedIDs& ids) {
		size_t count = 0;
		in >> count;
		s_Waiting.clear();
		for (size_t i = 0; i < count && in; ++i) {
			long unit = 0;
			Waiting waiting;
			in >> unit >> waiting.Side;
			waiting.MadeAt = LoadUpdate(in);
			waiting.InAt = LoadUpdate(in);
			waiting.Unit = ids.Ref(unit);
			if (waiting.Unit.Unit) {
				s_Waiting.push_back(waiting);
			}
		}
	}

	const SpawnOrder& SpawnOrderAt(const BattleModeSettings& settings, int side, const Vector& at) {
		side = std::clamp(side, 0, c_Sides - 1);
		const std::vector<std::vector<Vector>>& zones = settings.SpawnZones[side];
		const std::vector<SpawnOrder>& own = settings.ZoneSpawnOrders[side];
		for (size_t i = 0; i < zones.size() && i < own.size(); ++i) {
			if (own[i].Set && zones[i].size() >= 3 && IsInZone(zones[i], at)) {
				return own[i];
			}
		}
		return settings.SpawnOrders[side];
	}

	void UpdateSpawnOrders(bool aiPaused) {
		if (s_Waiting.empty() || aiPaused) {
			return;
		}
		if (!s_ModeRun.Running) {
			s_Waiting.clear();
			return;
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		const float perSecond = UpdatesPerSecond();
		// Those ready now, by side and order: a group given the same order together, as a selection is (in formation, answering as one).
		std::vector<std::tuple<int, const SpawnOrder*, std::vector<Actor*>>> groups;
		for (auto waiting = s_Waiting.begin(); waiting != s_Waiting.end();) {
			Actor* unit = GetRef(waiting->Unit);
			if (!unit) {
				// Not in yet (still in its ship), or never to be.
				const bool gone = static_cast<float>(now - waiting->MadeAt) > c_GiveUpSeconds * perSecond || waiting->InAt >= 0;
				waiting = gone ? s_Waiting.erase(waiting) : std::next(waiting);
				continue;
			}
			if (waiting->InAt < 0) {
				waiting->InAt = now;
			}
			if (static_cast<float>(now - waiting->InAt) < c_ReadySeconds * perSecond) {
				++waiting;
				continue;
			}
			const int side = waiting->Side;
			waiting = s_Waiting.erase(waiting);
			if (!BattlePlayerCommands(side) || unit->GetTeam() != side || unit->IsPlayerControlled() || unit->IsDead()) {
				continue;
			}
			// One you have ordered already (selected as it came in) keeps your order.
			const Actor::StandingOrder& standing = unit->GetStandingOrder();
			const long id = unit->GetUniqueID();
			if (standing.Kind != Actor::ORDER_NONE || standing.Hold || standing.HasPost || standing.TargetID != 0 || s_Plans.count(id) || s_BattleDefenders.count(id) ||
			    std::any_of(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); })) {
				continue;
			}
			const SpawnOrder* order = &SpawnOrderAt(s_ModeRun.Settings, side, unit->GetPos());
			if (order->Kind == SpawnOrderKind::Hold) {
				continue;
			}
			auto group = std::find_if(groups.begin(), groups.end(), [side, order](const auto& entry) { return std::get<0>(entry) == side && std::get<1>(entry) == order; });
			if (group == groups.end()) {
				groups.emplace_back(side, order, std::vector<Actor*>{unit});
			} else {
				std::get<2>(*group).push_back(unit);
			}
		}
		for (auto& [side, order, units]: groups) {
			GiveSpawnOrder(side, *order, std::move(units));
		}
	}

	void SetSpawnOrder(int side, int zone, const SpawnOrder& order) {
		if (side < 0 || side >= c_Sides) {
			return;
		}
		if (zone < 0) {
			s_ModeSetup.SpawnOrders[side] = order;
			s_ModeSetup.SpawnOrders[side].Set = true;
		} else if (zone < c_MaxSpawnZoneOrders) {
			std::vector<SpawnOrder>& own = s_ModeSetup.ZoneSpawnOrders[side];
			if (static_cast<int>(own.size()) <= zone) {
				own.resize(zone + 1);
			}
			own[zone] = order;
		}
		SendBattleMode();
	}

	bool TakeSpawnOrder(const Stroke& stroke) {
		// (Not a pen: that is for the animals selected, not a side's spawn order.)
		if (!s_SpawnOrderArmed || stroke.Kind != Tool::Command || stroke.Count == 40 || stroke.Count == 41 || stroke.Count == 42 || stroke.Count == 50) {
			return false;
		}
		const int side = SpawnOrderSide();
		if (side < 0) {
			s_SpawnOrderArmed = false;
			return false;
		}
		SpawnOrder order;
		order.Set = true;
		order.Direct = stroke.HasRisk ? stroke.Risk : (stroke.Clicks == 1 ? -1.0F : 0.0F);
		order.Radius = s_DefendRadius;
		order.Chase = s_DefendChase;
		order.Roam = s_DefendRoam;
		const CommandMode mode = stroke.OrderMode >= 0 ? static_cast<CommandMode>(stroke.OrderMode) : s_CommandMode;
		Vector at = stroke.Position;
		g_SceneMan.WrapPosition(at);
		order.Points = {at};
		if (stroke.Count == 10 || stroke.Count == 11) {
			order.Kind = SpawnOrderKind::DefendAt;
			order.Facing = FacingOf(at, stroke.Position2);
		} else if (stroke.Count == 20 || stroke.Count == 21) {
			order.Kind = SpawnOrderKind::Patrol;
			order.Points = stroke.Points;
			order.BackAndForth = stroke.Count == 21;
		} else if (stroke.Count == 30 || stroke.Count == 31) {
			order.Kind = mode == CommandMode::AttackMove ? SpawnOrderKind::AttackMove : SpawnOrderKind::Move;
			order.Facing = FacingOf(at, stroke.Position2);
		} else {
			switch (mode) {
				case CommandMode::AttackMove:
				case CommandMode::Attack:
					order.Kind = SpawnOrderKind::AttackMove;
					break;
				case CommandMode::DefendAt:
					order.Kind = SpawnOrderKind::DefendAt;
					break;
				case CommandMode::JetpackHere:
					order.Kind = SpawnOrderKind::JetpackHere;
					break;
				case CommandMode::Command:
					// As Command mode fits the order to what's there: a battle objective gone for, open air flown to, else a move.
					order.Kind = ObjectiveClicked(side, at) ? SpawnOrderKind::Objective : (IsSkyPoint(at) ? SpawnOrderKind::JetpackHere : SpawnOrderKind::Move);
					break;
				default:
					order.Kind = SpawnOrderKind::Move;
					break;
			}
		}
		SetSpawnOrder(side, s_SpawnOrderZone, order);
		// Done once a second click of a double click (which places it again, normal rather than cautious) can no longer follow.
		s_SpawnOrderDoneAt = ImGui::GetTime() + ImGui::GetIO().MouseDoubleClickTime;
		if (order.Kind == SpawnOrderKind::Patrol) {
			s_SpawnOrderArmed = false;
			s_CommandMode = CommandMode::Command;
		}
		const std::string whose = s_SpawnOrderZone < 0 ? std::string("New units") : "Spawn zone " + std::to_string(s_SpawnOrderZone + 1);
		ModernHUD::AddFeedLine(whose + ": " + c_SpawnOrderNames[static_cast<int>(order.Kind)], side);
		return true;
	}

	void DrawSpawnOrders() {
		if (s_SpawnOrderDoneAt >= 0.0 && !s_Dragging && ImGui::GetTime() >= s_SpawnOrderDoneAt) {
			s_SpawnOrderArmed = false;
			s_SpawnOrderDoneAt = -1.0;
		}
		if (!InGame() || CurrentTool().Kind != Tool::Command) {
			return;
		}
		const int side = SpawnOrderSide();
		if (side < 0 || s_ModeSetup.Mode == BattleMode::Custom) {
			return;
		}
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const float scale = ScenePixelsPerWindowPixel();
		auto toScreen = [scale](const Vector& at) {
			Vector onScreen = FromCamera(at);
			return ImVec2(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
		};
		auto drawOrder = [&](const SpawnOrder& order, const std::vector<Vector>& from, bool highlighted) {
			if (order.Kind == SpawnOrderKind::Hold || order.Points.empty()) {
				return;
			}
			const ImU32 color = SpawnOrderColor(order.Kind);
			const ImU32 faint = (color & 0x00FFFFFF) | ((highlighted ? 220u : 120u) << IM_COL32_A_SHIFT);
			const ImVec2 to = toScreen(order.Points.front());
			// (From each zone it's for, dashed: the way new units will go.)
			for (const Vector& start: from) {
				const ImVec2 a = toScreen(start);
				const float dx = to.x - a.x;
				const float dy = to.y - a.y;
				const float length = std::sqrt(dx * dx + dy * dy);
				const int dashes = std::clamp(static_cast<int>(length / 14.0F), 1, 200);
				for (int i = 0; i < dashes; i += 2) {
					const float t0 = static_cast<float>(i) / static_cast<float>(dashes);
					const float t1 = static_cast<float>(i + 1) / static_cast<float>(dashes);
					drawList->AddLine(ImVec2(a.x + dx * t0, a.y + dy * t0), ImVec2(a.x + dx * t1, a.y + dy * t1), faint, highlighted ? 2.5F : 1.5F);
				}
			}
			for (size_t i = 1; i < order.Points.size(); ++i) {
				drawList->AddLine(toScreen(order.Points[i - 1]), toScreen(order.Points[i]), faint, 1.5F);
			}
			if (order.Kind == SpawnOrderKind::Patrol && !order.BackAndForth && order.Points.size() > 2) {
				drawList->AddLine(toScreen(order.Points.back()), toScreen(order.Points.front()), faint, 1.5F);
			}
			for (const Vector& point: order.Points) {
				drawList->AddCircleFilled(toScreen(point), 3.0F, color);
			}
			drawList->AddCircle(to, 9.0F, color, 0, 2.0F);
			const char* name = c_SpawnOrderNames[static_cast<int>(order.Kind)];
			drawList->AddText(ImVec2(to.x + 12.0F, to.y - 20.0F), IM_COL32(0, 0, 0, 200), name);
			drawList->AddText(ImVec2(to.x + 11.0F, to.y - 21.0F), color, name);
		};
		const BattleModeSettings& settings = s_ModeSetup;
		const std::vector<std::vector<Vector>>& zones = settings.SpawnZones[side];
		const std::vector<SpawnOrder>& own = settings.ZoneSpawnOrders[side];
		std::vector<Vector> teamFrom;
		for (size_t i = 0; i < zones.size(); ++i) {
			if (zones[i].size() < 3) {
				continue;
			}
			if (i < own.size() && own[i].Set) {
				drawOrder(own[i], {ZoneCentre(zones[i])}, s_SpawnOrderArmed && s_SpawnOrderZone == static_cast<int>(i));
			} else {
				teamFrom.push_back(ZoneCentre(zones[i]));
			}
		}
		drawOrder(settings.SpawnOrders[side], teamFrom, s_SpawnOrderArmed && s_SpawnOrderZone < 0);
		// The zone a spawn order is being set for, outlined.
		if (s_SpawnOrderArmed && s_SpawnOrderZone >= 0 && s_SpawnOrderZone < static_cast<int>(zones.size())) {
			const std::vector<Vector>& zone = zones[s_SpawnOrderZone];
			for (size_t i = 0; i < zone.size(); ++i) {
				drawList->AddLine(toScreen(zone[i]), toScreen(zone[(i + 1) % zone.size()]), c_SideColors[side], 3.0F);
			}
		}
	}
} // namespace SandboxDetail
