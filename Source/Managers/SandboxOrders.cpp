// Orders to units: sending, holding and standing orders, selection, group moves and the auto battle.

#include "SandboxInternal.h"

namespace SandboxDetail {
	MovableObject* CreateObject(const std::string& className, const std::string& presetName, int moduleID) {
		const Entity* preset = g_PresetMan.GetEntityPreset(className, presetName, moduleID);
		return preset ? dynamic_cast<MovableObject*>(preset->Clone()) : nullptr;
	}

	MovableObject* CreateBaseObject(const char* className, const char* presetName) {
		const Entity* preset = g_PresetMan.GetEntityPreset(className, presetName, "Base.rte");
		return preset ? dynamic_cast<MovableObject*>(preset->Clone()) : nullptr;
	}

	void AddObject(MovableObject* object) {
		if (Actor* actor = dynamic_cast<Actor*>(object)) {
			g_MovableMan.AddActor(actor);
		} else if (HeldDevice* device = dynamic_cast<HeldDevice*>(object)) {
			g_MovableMan.AddItem(device);
		} else {
			g_MovableMan.AddParticle(object);
		}
	}

	/// Whether an actor is a real unit on a side (not a door or other neutral scenery).
	bool IsCombatant(const Actor* actor) {
		return actor && actor->GetTeam() >= 0 && actor->GetTeam() < c_Sides && !actor->IsDead() && !dynamic_cast<const ADoor*>(actor) && actor->GetHealth() > 0.0F;
	}

	/// Whether a unit can be selected and commanded: a combatant on a side, not a brain, not a craft (a ship is ordered by its own AI; sent off
	/// with the squad, a dropship delivering hovered with its passengers inside).
	bool IsSelectable(const Actor* actor) {
		return IsCombatant(actor) && !actor->IsInGroup("Brains") && !dynamic_cast<const ACraft*>(actor);
	}

	/// The actor (or, failing that, loose item) closest to a point, within reach.
	MovableObject* ObjectUnder(const Vector& position, bool actorsOnly) {
		MovableObject* found = nullptr;
		float foundDistance = 24.0F * 24.0F;
		for (Actor* actor: SandboxAccess::Actors()) {
			float distance = g_SceneMan.ShortestDistance(position, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
			if (distance < foundDistance) {
				found = actor;
				foundDistance = distance;
			}
		}
		if (!found && !actorsOnly) {
			foundDistance = 16.0F * 16.0F;
			for (MovableObject* item: SandboxAccess::Items()) {
				float distance = g_SceneMan.ShortestDistance(position, item->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
				if (distance < foundDistance) {
					found = item;
					foundDistance = distance;
				}
			}
		}
		return found;
	}



	/// Sends a unit to a place, or after a unit, from the next update (see PendingOrder).
	/// @param reason Why, in a few words, for the orders overlay.
	/// @param lock Whether the unit keeps after this enemy while it lives (an enemy picked by the player), rather than being free to fight
	/// what it meets on the way (one picked for it).
	/// @param resend Whether the standing orders are sending it again.
	void SendUnit(Actor* unit, const Vector& waypoint, Actor* target, bool attack, const char* reason, bool lock, bool resend) {
		// (Never a craft: set to GOTO, a dropship delivering stopped its unload, which only runs in STAY or DELIVER, and hovered with the
		// squad inside.)
		if (dynamic_cast<const ACraft*>(unit)) {
			return;
		}
		if (s_SendNotes.size() > 512) {
			std::unordered_set<long> alive;
			for (const Actor* actor: SandboxAccess::Actors()) {
				alive.insert(actor->GetUniqueID());
			}
			for (auto note = s_SendNotes.begin(); note != s_SendNotes.end();) {
				note = alive.count(note->first) ? std::next(note) : s_SendNotes.erase(note);
			}
		}
		s_SendNotes[unit->GetUniqueID()] = {reason, resend, g_TimerMan.GetSimUpdateCount()};
		CancelRetreatAndFlank(unit);
		Actor::StandingOrder& standing = unit->GetStandingOrder();
		standing.Attack = false;
		standing.HasPost = false;
		standing.AutoTargetID = 0;
		standing.Hold = false;
		standing.TargetID = attack && target && lock ? static_cast<long>(target->GetUniqueID()) : 0;
		if (!attack) {
			standing.HasAttackPlace = false;
		}
		// An earlier order still waiting is dropped.
		s_PendingOrders.erase(std::remove_if(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); }), s_PendingOrders.end());
		s_PendingOrders.push_back({MakeRef(unit), waypoint, target, target ? static_cast<long>(target->GetUniqueID()) : 0, attack});
	}

	/// Holds a unit where it is, forgetting every order it had.
	void HoldUnit(Actor* unit) {
		CancelRetreatAndFlank(unit);
		unit->ClearStandingOrder();
		unit->ClearAIWaypoints();
		unit->SetAIMode(Actor::AIMODE_SENTRY);
		s_PendingOrders.erase(std::remove_if(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); }), s_PendingOrders.end());
	}

	void ApplyPendingOrders() {
		std::vector<PendingOrder> orders;
		orders.swap(s_PendingOrders);
		for (const PendingOrder& order: orders) {
			Actor* unit = GetRef(order.Unit);
			if (!unit) {
				continue;
			}
			unit->ClearAIWaypoints();
			if (order.Target && g_MovableMan.IsActor(order.Target) && static_cast<long>(order.Target->GetUniqueID()) == order.TargetID) {
				unit->AddAIMOWaypoint(order.Target);
			} else {
				unit->AddAISceneWaypoint(order.Waypoint);
			}
			for (const Vector& then: order.Then) {
				unit->AddAISceneWaypoint(then);
			}
			unit->SetAIMode(Actor::AIMODE_GOTO);
			if (g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Sandbox)) {
				g_ConsoleMan.PrintString("SANDBOX: " + unit->GetPresetName() + " sent to " + std::to_string(static_cast<int>(order.Waypoint.m_X)) + "," + std::to_string(static_cast<int>(order.Waypoint.m_Y)) + (order.Target ? " after " + order.Target->GetPresetName() : "") + " mode now " + std::to_string(unit->GetAIMode()));
			}
			if (order.Attack) {
				unit->SetOrderAttack(true);
			}
		}
	}

	void GiveOrder(Actor* actor, Order order) {
		if (!actor || dynamic_cast<ADoor*>(actor) || dynamic_cast<const ACraft*>(actor) || actor->IsInGroup("Brains")) {
			return;
		}
		CancelRetreatAndFlank(actor);
		// Every earlier order's tags go, as HoldUnit does: a defender told to patrol was dragged back to its post every second by
		// ReturnDefenders, and to the AI ("defend") never closed in, flanked or fell back; an old target or attack-place pulled it there.
		actor->ClearStandingOrder();
		// And the old order's way there, queued or still to be applied: a unit told to hold (or patrol, hunt or idle) kept its waypoints, and
		// anything that later put a GOTO back (a fall-back's RestoreOrder, the AI's own new-order check) walked it off along them.
		actor->ClearAIWaypoints();
		s_PendingOrders.erase(std::remove_if(s_PendingOrders.begin(), s_PendingOrders.end(), [actor](const PendingOrder& pending) { return RefersTo(pending.Unit, actor); }), s_PendingOrders.end());
		switch (order) {
			case Order::Attack:
				// Which enemy to go for is the unit's AI's to pick (SharedBehaviors.AttackOrderUpdate): the nearest it has a route to, on its
				// next update, and again whenever it has nothing left to go for. It fights whatever it meets on the way, and is never pulled out
				// of a fight to be re-sent from here, as the sandbox's once-a-second retarget pass did.
				s_SendNotes[actor->GetUniqueID()] = {"attack order", false, g_TimerMan.GetSimUpdateCount()};
				actor->SetOrderAttack(true);
				actor->SetAIMode(Actor::AIMODE_SENTRY);
				break;
			case Order::HuntBrains:
				actor->SetAIMode(Actor::AIMODE_BRAINHUNT);
				break;
			case Order::Patrol:
				actor->SetAIMode(Actor::AIMODE_PATROL);
				break;
			case Order::Rally:
				if (int team = actor->GetTeam(); team >= 0 && team < c_Sides && s_RallySet[team]) {
					SendUnit(actor, s_RallyPoints[team], nullptr, false, "to the rally point");
				} else {
					actor->ClearAIWaypoints();
					actor->SetAIMode(Actor::AIMODE_SENTRY);
				}
				break;
			case Order::Idle:
				actor->SetAIMode(Actor::AIMODE_NONE);
				break;
			case Order::DigGold:
				actor->ClearAIWaypoints();
				actor->SetAIMode(Actor::AIMODE_GOLDDIG);
				break;
			case Order::Hold:
				actor->SetAIMode(Actor::AIMODE_SENTRY);
				actor->SetOrderHold(true);
				break;
			default:
				actor->SetAIMode(Actor::AIMODE_SENTRY);
				break;
		}
	}

	/// Units told to defend a spot go back to it when they've been moved off it (shoved, blown, or drawn after an enemy), and stand guard there again.
	void ReturnDefenders() {
		for (Actor* actor: SandboxAccess::Actors()) {
			if (!actor->GetOrderHasPost() || actor->IsPlayerControlled() || !IsCombatant(actor)) {
				continue;
			}
			Vector post = actor->GetOrderPost();
			float off = g_SceneMan.ShortestDistance(actor->GetPos(), post, g_SceneMan.SceneWrapsX()).GetMagnitude();
			if (actor->GetAIMode() == Actor::AIMODE_GOTO) {
				// On the way back: once there, guard again.
				if (off < 30.0F) {
					actor->ClearAIWaypoints();
					actor->SetAIMode(Actor::AIMODE_SENTRY);
				}
			} else if (off > 60.0F) {
				SendUnit(actor, post, nullptr, false, "back to its post", false, true);
				actor->SetOrderPost(post);
			}
		}
	}

	void ActivateSide(int team) {
		if (Activity* activity = g_ActivityMan.GetActivity(); activity && team >= 0 && team < c_Sides) {
			activity->ForceSetTeamAsActive(team);
		}
	}

	void SelectInBox(const Vector& cornerA, const Vector& cornerB) {
		s_Selected.clear();
		float left = std::min(cornerA.m_X, cornerB.m_X);
		float right = std::max(cornerA.m_X, cornerB.m_X);
		float top = std::min(cornerA.m_Y, cornerB.m_Y);
		float bottom = std::max(cornerA.m_Y, cornerB.m_Y);
		// Measured from the box's middle the short way round: the corners come from the camera unwrapped, and actors' positions are
		// wrapped, so on a wrapping map a box across the seam (or drawn with the view past the right edge) took nothing on one side.
		Vector middle((left + right) * 0.5F, (top + bottom) * 0.5F);
		float halfWidth = (right - left) * 0.5F;
		float halfHeight = (bottom - top) * 0.5F;
		std::vector<Actor*> inBox;
		std::array<int, c_Sides> perSide {};
		for (Actor* actor: SandboxAccess::Actors()) {
			Vector offset = g_SceneMan.ShortestDistance(middle, actor->GetPos(), g_SceneMan.SceneWrapsX());
			if (IsSelectable(actor) && std::abs(offset.m_X) <= halfWidth && std::abs(offset.m_Y) <= halfHeight) {
				inBox.push_back(actor);
				++perSide[actor->GetTeam()];
			}
		}
		// One side only: the side picked in the panel when it has units in the box, else the side with the most there. (Every side's units
		// were taken, and the command went by the first one's side: a click on a soldier of one side sent the other side's at it.)
		int side = s_Team >= 0 && s_Team < c_Sides && perSide[s_Team] > 0 ? s_Team : static_cast<int>(std::max_element(perSide.begin(), perSide.end()) - perSide.begin());
		for (Actor* actor: inBox) {
			if (actor->GetTeam() == side) {
				s_Selected.push_back(MakeRef(actor));
			}
		}
		if (g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Sandbox)) {
			std::string where;
			for (const Actor* actor: SandboxAccess::Actors()) {
				where += " " + actor->GetPresetName() + "@" + std::to_string(static_cast<int>(actor->GetPos().m_X)) + "," + std::to_string(static_cast<int>(actor->GetPos().m_Y));
			}
			g_ConsoleMan.PrintString("SANDBOX: select box " + std::to_string(static_cast<int>(left)) + "," + std::to_string(static_cast<int>(top)) + " to " + std::to_string(static_cast<int>(right)) + "," + std::to_string(static_cast<int>(bottom)) + " took " + std::to_string(s_Selected.size()) + "; actors:" + where);
		}
	}

	/// The selected units move to a point, or attack the unit there.
	/// Places for a number of units to stand as near a point as the ground allows: on the ground, spread out either side of it, never inside anything.
	/// Each is where a unit's feet go.
	std::vector<Vector> StandingSpots(const Vector& around, int count) {
		std::vector<Vector> spots;
		if (count <= 0 || !g_SceneMan.GetScene()) {
			return spots;
		}
		const int sceneHeight = g_SceneMan.GetSceneHeight();
		const float stride = std::clamp(s_Spacing, 8.0F, 60.0F);
		// Outwards from the point: there, then left and right in turn, further each time.
		for (int step = 0; step < count * 6 && static_cast<int>(spots.size()) < count; ++step) {
			float offset = step == 0 ? 0.0F : (static_cast<float>((step + 1) / 2) * stride) * ((step % 2 == 1) ? -1.0F : 1.0F);
			Vector probe = around + Vector(offset, 0.0F);
			g_SceneMan.WrapPosition(probe);
			int x = probe.GetFloorIntX();
			int y = probe.GetFloorIntY();
			// Out of the ground if inside it, by the nearer side (down when they're even), then down to the ground beneath, within a short way of
			// the point. (Only ever up, a click on a bunker's ceiling slab sent the unit a storey up, or onto the roof.)
			if (g_SceneMan.GetTerrMatter(x, y) != g_MaterialAir) {
				int up = 0;
				while (up < 120 && y - up > 0 && g_SceneMan.GetTerrMatter(x, y - up) != g_MaterialAir) {
					++up;
				}
				int down = 0;
				while (down < 120 && y + down < sceneHeight - 2 && g_SceneMan.GetTerrMatter(x, y + down) != g_MaterialAir) {
					++down;
				}
				if (down < 120 && down <= up) {
					y += down;
				} else if (up < 120) {
					y -= up;
				} else {
					continue;
				}
			}
			int fell = 0;
			while (fell < 240 && y < sceneHeight - 2 && g_SceneMan.GetTerrMatter(x, y + 1) == g_MaterialAir) {
				++y;
				++fell;
			}
			if (fell >= 240 || y >= sceneHeight - 2) {
				continue;
			}
			// Head room, and nothing already chosen too close.
			bool clear = true;
			for (int up = 2; up <= 40 && clear; up += 4) {
				clear = g_SceneMan.GetTerrMatter(x, y - up) == g_MaterialAir;
			}
			for (const Vector& taken: spots) {
				if (clear && g_SceneMan.ShortestDistance(taken, Vector(static_cast<float>(x), static_cast<float>(y)), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(stride * 0.7F)) {
					clear = false;
				}
			}
			if (clear) {
				spots.emplace_back(static_cast<float>(x), static_cast<float>(y));
			}
		}
		return spots;
	}


	/// What MoveUnitsTo will make of a move to a point, step for step: twice as many spots as units, each tried with the first unit's reach until
	/// there are enough it can get to, those taken (or, if none can be reached, the nearest spots regardless). Worked out again only when the
	/// point, the units or the first unit change, or half a second of frames on, since each try is a path search.
	const std::vector<SpotReach>& SpotReachPreview(const std::vector<Actor*>& units, const Vector& point) {
		static std::vector<SpotReach> preview;
		static Vector lastPoint;
		static size_t lastCount = 0;
		static long lastLeader = -1;
		static int lastFrame = -1000;
		long leaderID = units.empty() ? -1 : static_cast<long>(units.front()->GetUniqueID());
		int frame = ImGui::GetFrameCount();
		if (units.size() == lastCount && leaderID == lastLeader && frame - lastFrame < 30 && g_SceneMan.ShortestDistance(point, lastPoint, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(3.0F)) {
			return preview;
		}
		lastPoint = point;
		lastCount = units.size();
		lastLeader = leaderID;
		lastFrame = frame;
		preview.clear();
		for (const Vector& spot: StandingSpots(point, static_cast<int>(units.size()) * 2)) {
			preview.push_back({spot});
		}
		Scene* scene = g_SceneMan.GetScene();
		size_t reachable = 0;
		if (scene && !units.empty()) {
			std::list<Vector> path;
			const Actor* leader = units.front();
			for (SpotReach& entry: preview) {
				if (reachable >= units.size()) {
					break;
				}
				float cost = scene->CalculatePath(leader->GetPos(), entry.Spot, path, leader->EstimateJumpHeight(), leader->EstimateDigStrength(), static_cast<Activity::Teams>(leader->GetTeam()), leader->EstimateBreachStrength());
				entry.Cost = cost >= 0.0F && cost < 100000.0F ? cost : -1.0F;
				reachable += entry.Cost >= 0.0F ? 1 : 0;
			}
		}
		size_t chosen = 0;
		for (SpotReach& entry: preview) {
			if (chosen < units.size() && (reachable == 0 || entry.Cost >= 0.0F)) {
				entry.Chosen = true;
				++chosen;
			}
		}
		return preview;
	}

	/// The units a move order from a point goes to: a side's, or the selected ones.
	std::vector<Actor*> UnitsToMove(int team, bool selectedOnly) {
		std::vector<Actor*> units;
		if (selectedOnly) {
			for (const UnitRef& ref: s_Selected) {
				if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled() && !dynamic_cast<const ACraft*>(unit)) {
					units.push_back(unit);
				}
			}
		} else {
			for (Actor* actor: SandboxAccess::Actors()) {
				if (actor->GetTeam() == team && IsCombatant(actor) && !actor->IsPlayerControlled() && !actor->IsInGroup("Brains") && !dynamic_cast<const ACraft*>(actor)) {
					units.push_back(actor);
				}
			}
		}
		return units;
	}

	/// Sends units to stand round a point, each to its own spot, the nearest unit to the nearest spot.
	void MoveUnitsTo(std::vector<Actor*> units, const Vector& point) {
		std::vector<Vector> spots = StandingSpots(point, static_cast<int>(units.size()) * 2);
		if (spots.empty()) {
			return;
		}
		// Spots no unit can get to (walled off, across a gap too wide) are passed over, so nobody is sent to stand at a wall.
		// The searches run side by side, a batch of as many spots as there are units at a time, nearest first, till enough are found: one by
		// one on the main thread, a move of twenty units was up to forty searches in a row, and the game hitched for each such order.
		// (The grid isn't rebuilt under them: that happens on this thread, which waits here.)
		if (Scene* scene = g_SceneMan.GetScene(); scene && !units.empty()) {
			std::vector<Vector> reachable;
			// With the unit's own reach, as its AI will search: the same jump height, dig strength and breaching, on its team's grid.
			const Actor* leader = units.front();
			const Vector from = leader->GetPos();
			const float jumpHeight = leader->EstimateJumpHeight();
			const float digStrength = leader->EstimateDigStrength();
			const float breachStrength = leader->EstimateBreachStrength();
			const Activity::Teams team = static_cast<Activity::Teams>(leader->GetTeam());
			size_t batch = std::max<size_t>(units.size(), 4);
			for (size_t first = 0; first < spots.size() && reachable.size() < units.size(); first += batch) {
				size_t count = std::min(batch, spots.size() - first);
				std::vector<char> reaches(count, 0);
				std::vector<size_t> indices(count);
				std::iota(indices.begin(), indices.end(), size_t{0});
				std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t i) {
					std::list<Vector> path;
					float cost = scene->CalculatePath(from, spots[first + i], path, jumpHeight, digStrength, team, breachStrength);
					reaches[i] = cost >= 0.0F && cost < 100000.0F ? 1 : 0;
				});
				for (size_t i = 0; i < count && reachable.size() < units.size(); ++i) {
					if (reaches[i]) {
						reachable.push_back(spots[first + i]);
					}
				}
			}
			if (!reachable.empty()) {
				spots = reachable;
			}
		}
		spots.resize(std::min(spots.size(), units.size()));
		std::sort(units.begin(), units.end(), [&point](Actor* a, Actor* b) {
			return g_SceneMan.ShortestDistance(point, a->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude() < g_SceneMan.ShortestDistance(point, b->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
		});
		for (size_t i = 0; i < units.size(); ++i) {
			Actor* unit = units[i];
			const Vector& spot = spots[std::min(i, spots.size() - 1)];
			// The waypoint just over the ground where the feet go (the AI puts it at its own standing height from there). Half the unit's height up,
			// as it was, was inside the ceiling of a low corridor, and a waypoint inside a thin slab is taken to be on top of it: a unit sent a few
			// steps along a bunker corridor went out and round to the roof over it.
			SendUnit(unit, spot + Vector(0.0F, -4.0F), nullptr, false, "move");
		}
	}

	/// Who a unit is following, if anyone: the actor it is to go to, loaded (its move target) or still queued as its last waypoint.
	const Actor* FollowedBy(const Actor* unit) {
		if (const Actor* leader = dynamic_cast<const Actor*>(unit->GetMOMoveTarget()); leader && g_MovableMan.IsActor(leader)) {
			return leader;
		}
		const auto& waypoints = unit->GetWaypointList();
		if (!waypoints.empty()) {
			if (const Actor* leader = dynamic_cast<const Actor*>(waypoints.back().second); leader && g_MovableMan.IsActor(leader)) {
				return leader;
			}
		}
		return nullptr;
	}

	/// Sets a unit to guard another: a squad follower of it, as the game's own squads are (AIMODE_SQUAD and the leader as its MO waypoint), so
	/// it gets the trail, its place in the formation and the dead-leader handling. (A GOTO to the leader, as it was, took the old shoving path
	/// squads were fixed away from.) A follow that would close a loop (the leader following this unit, or one that does) is broken there: the
	/// one in the loop who followed this unit holds where it is instead, or the two walked into each other for ever.
	void GuardUnit(Actor* unit, Actor* leader) {
		const Actor* along = leader;
		for (int i = 0; along && i < 16; ++i) {
			const Actor* next = FollowedBy(along);
			if (next == unit) {
				HoldUnit(const_cast<Actor*>(along));
				break;
			}
			along = next;
		}
		HoldUnit(unit);
		unit->SetAIMode(Actor::AIMODE_SQUAD);
		unit->AddAIMOWaypoint(leader);
		unit->SetMovePathToUpdate();
	}


	/// The side the selection belongs to: the first selected unit's, else the side in hand.
	int SelectionTeam() {
		for (const UnitRef& ref: s_Selected) {
			if (const Actor* unit = GetRef(ref)) {
				return unit->GetTeam();
			}
		}
		return s_Team;
	}

	void MarkOrder(const Vector& at, ImU32 color) { s_OrderMarks.push_back({at, 1.0F, color}); }


	/// A click on the world with the command tool, as the mode says. Count: 0 a plain click, 1 with Shift held (add to the selection), 2 a double click
	/// (select every unit of that kind in sight).
	void CommandSelected(const Vector& position, int modifier) {
		Actor* target = dynamic_cast<Actor*>(ObjectUnder(position, true));
		if (g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Sandbox)) {
			g_ConsoleMan.PrintString("SANDBOX: command at " + std::to_string(static_cast<int>(position.m_X)) + "," + std::to_string(static_cast<int>(position.m_Y)) + " selected " + std::to_string(s_Selected.size()) + " target " + (target ? target->GetPresetName() : std::string("none")) + " mode " + std::to_string(static_cast<int>(s_CommandMode)));
		}
		bool friendly = target && IsSelectable(target) && (s_Selected.empty() || target->GetTeam() == SelectionTeam());
		bool selected = target && std::any_of(s_Selected.begin(), s_Selected.end(), [target](const UnitRef& ref) { return RefersTo(ref, target); });
		if (s_CommandMode == CommandMode::Guard) {
			// Follow the friend clicked; with nobody there, nothing happens.
			if (friendly && !selected) {
				for (const UnitRef& ref: s_Selected) {
					if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled() && unit != target) {
						GuardUnit(unit, target);
					}
				}
				MarkOrder(target->GetPos(), IM_COL32(120, 220, 120, 255));
			}
			return;
		}
		if (s_CommandMode == CommandMode::Attack) {
			OrderSelectedUnits(1, position);
			MarkOrder(position, IM_COL32(239, 106, 91, 255));
			return;
		}
		// Move: a friend is picked up into the selection, an enemy attacked, the ground gone to.
		if (friendly && (modifier != 0 || !selected || s_Selected.size() == 1)) {
			if (modifier == 2) {
				// Every unit of that kind in sight.
				GameViewRect view = g_WindowMan.GetGameViewRect();
				float scale = ScenePixelsPerWindowPixel();
				Vector corner = g_CameraMan.GetOffset(0);
				Vector far = corner + Vector(view.w * scale, view.h * scale);
				for (Actor* actor: SandboxAccess::Actors()) {
					Vector onScreen = g_SceneMan.ShortestDistance(corner, actor->GetPos(), g_SceneMan.SceneWrapsX());
					if (IsSelectable(actor) && actor->GetTeam() == target->GetTeam() && actor->GetPresetName() == target->GetPresetName() && onScreen.m_X >= 0.0F && onScreen.m_Y >= 0.0F && onScreen.m_X <= far.m_X - corner.m_X && onScreen.m_Y <= far.m_Y - corner.m_Y &&
					    std::none_of(s_Selected.begin(), s_Selected.end(), [actor](const UnitRef& ref) { return RefersTo(ref, actor); })) {
						s_Selected.push_back(MakeRef(actor));
					}
				}
			} else if (modifier == 1) {
				if (selected) {
					s_Selected.erase(std::remove_if(s_Selected.begin(), s_Selected.end(), [target](const UnitRef& ref) { return RefersTo(ref, target); }), s_Selected.end());
				} else {
					s_Selected.push_back(MakeRef(target));
				}
			} else {
				s_Selected.clear();
				s_Selected.push_back(MakeRef(target));
			}
			return;
		}
		bool attack = target && IsCombatant(target) && !selected && !friendly;
		if (attack) {
			for (const UnitRef& ref: s_Selected) {
				// (Never at one of its own side, whatever the selection holds.)
				if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled() && unit->GetTeam() != target->GetTeam()) {
					SendUnit(unit, target->GetPos(), target, true, "attack", true);
				}
			}
			MarkOrder(target->GetPos(), IM_COL32(239, 106, 91, 255));
		} else if (modifier == 1) {
			// Shift: on to here after where they're going.
			QueueWaypoint(UnitsToMove(0, true), position);
			MarkOrder(position, IM_COL32(110, 180, 250, 255));
		} else {
			MoveUnitsTo(UnitsToMove(0, true), position);
			MarkOrder(position, IM_COL32(110, 180, 250, 255));
		}
	}

	/// A further place for the selected units to go on to after where they're going (a shift-click): the route is then the player's own, leg by leg.
	/// A unit going nowhere is simply sent there.
	void QueueWaypoint(std::vector<Actor*> units, const Vector& point) {
		std::vector<Vector> spots = StandingSpots(point, 1);
		for (Actor* unit: units) {
			// (Just over the ground under the point, as for a move: see MoveUnitsTo.)
			Vector waypoint = (spots.empty() ? point : spots.front()) + Vector(0.0F, -4.0F);
			auto pending = std::find_if(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); });
			if (pending != s_PendingOrders.end()) {
				pending->Then.push_back(waypoint);
			} else if (unit->GetAIMode() == Actor::AIMODE_GOTO) {
				unit->AddAISceneWaypoint(waypoint);
			} else {
				SendUnit(unit, waypoint, nullptr, false, "move (queued)");
			}
		}
	}

	/// The command ring's choices for the selected units, about a point: 0 move there, 1 attack there, 2 hold where they are.
	void OrderSelectedUnits(int choice, const Vector& point) {
		std::vector<Actor*> units = UnitsToMove(0, true);
		if (choice == 0) {
			MoveUnitsTo(units, point);
		} else if (choice == 1) {
			// The nearest enemy to the point, if there is one close, else the place itself with orders to fight whatever is met.
			Actor* target = nullptr;
			float nearest = 400.0F * 400.0F;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (!IsCombatant(actor) || actor->IsIgnoredByAI() || units.empty() || actor->GetTeam() == units.front()->GetTeam()) {
					continue;
				}
				float distance = g_SceneMan.ShortestDistance(point, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
				if (distance < nearest) {
					nearest = distance;
					target = actor;
				}
			}
			for (Actor* unit: units) {
				SendUnit(unit, point, target, true, "attack there");
				unit->SetOrderAttackPlace(point);
			}
		} else if (choice == 2) {
			// Cancel: every order forgotten, and the side's standing orders apply.
			for (Actor* unit: units) {
				HoldUnit(unit);
				if (static_cast<Order>(s_Order) != Order::MoveTo) {
					GiveOrder(unit, static_cast<Order>(s_Order));
				}
				MarkOrder(unit->GetPos(), IM_COL32(200, 160, 120, 255));
			}
		} else if (choice == 3) {
			// Defend: stand this ground and fight from it, moving as little as can be; a unit shoved or drawn off its post is sent back.
			for (Actor* unit: units) {
				HoldUnit(unit);
				unit->SetOrderPost(unit->GetPos());
				MarkOrder(unit->GetPos(), IM_COL32(242, 182, 61, 255));
			}
		}
	}

	/// The closest pair of enemies anywhere: where the fighting is.
	void FindAction() {
		s_ActionSpotValid = false;
		float best = 0.0F;
		std::deque<Actor*>& actors = SandboxAccess::Actors();
		for (size_t i = 0; i < actors.size(); ++i) {
			if (!IsCombatant(actors[i])) {
				continue;
			}
			for (size_t j = i + 1; j < actors.size(); ++j) {
				if (!IsCombatant(actors[j]) || actors[j]->GetTeam() == actors[i]->GetTeam()) {
					continue;
				}
				Vector between = g_SceneMan.ShortestDistance(actors[i]->GetPos(), actors[j]->GetPos(), g_SceneMan.SceneWrapsX());
				float distance = between.GetSqrMagnitude();
				if (!s_ActionSpotValid || distance < best) {
					best = distance;
					s_ActionSpot = actors[i]->GetPos() + between * 0.5F;
					s_ActionSpotValid = true;
				}
			}
		}
	}

	/// The faction's units an auto battle can buy: soldiers mostly, the odd crab.
	std::vector<const Preset*> FactionUnits(int moduleID) {
		std::vector<const Preset*> units;
		for (const Preset& unit: s_Units) {
			const Entity* entity = unit.ModuleID == moduleID ? g_PresetMan.GetEntityPreset(unit.ClassName, unit.PresetName, unit.ModuleID) : nullptr;
			if (entity && !entity->IsInGroup("Actors - Turrets")) {
				units.push_back(&unit);
			}
		}
		return units;
	}

	float AutoLaneX(int side) {
		if (s_RallySet[side]) {
			return s_RallyPoints[side].m_X;
		}
		static constexpr float lanes[c_Sides] = {-0.7F, 0.7F, -0.35F, 0.35F};
		// (Spaced by the view's width at the start, not now: zooming during the battle moved where the waves landed.)
		Vector lane = s_AutoCenter + Vector(lanes[side] * s_AutoLaneWidth, 0.0F);
		g_SceneMan.WrapPosition(lane);
		return lane.m_X;
	}

	/// Each side in an auto battle buys a wave every so often with what's left of its budget and sends it in to attack, until one side is left.
	void UpdateAutoBattle() {
		if (!s_AutoRunning) {
			return;
		}
		long long now = g_TimerMan.GetSimUpdateCount();
		for (int side = 0; side < c_Sides; ++side) {
			AutoSide& autoSide = s_AutoSides[side];
			if (!autoSide.Active || autoSide.Broke || now < autoSide.NextWave || s_FactionModules.empty()) {
				continue;
			}
			autoSide.NextWave = now + 900;
			std::vector<const Preset*> choices;
			if (s_AutoRandom) {
				// Random units from every faction (or the favourites): a few dozen of them, picked afresh each wave, are priced and bought
				// from, not the whole catalogue (each pricing makes the unit and its loadout).
				choices = RandomUnitPool(s_AutoFavourites);
				for (size_t i = 0; i < choices.size() && i < 24; ++i) {
					size_t other = i + std::min(choices.size() - i - 1, static_cast<size_t>(Random01() * static_cast<float>(choices.size() - i)));
					std::swap(choices[i], choices[other]);
				}
				if (choices.size() > 24) {
					choices.resize(24);
				}
			} else {
				choices = FactionUnits(s_FactionModules[std::clamp(autoSide.Faction, 0, static_cast<int>(s_FactionModules.size()) - 1)]);
			}
			float left = static_cast<float>(autoSide.Budget) - autoSide.Spent;
			// What each of the faction's units costs as bought (with its loadout), and the cheapest. The wave's budget is at least the
			// cheapest unit, and picks are made only from what still fits: a faction whose cheapest unit cost over 900 (heavy mechs, some
			// mods) never filled a wave and was called broke before buying anything, and twelve random picks over budget did the same to
			// a side that could still afford its cheapest.
			std::vector<std::pair<const Preset*, float>> priced;
			float cheapest = -1.0F;
			for (const Preset* choice: choices) {
				if (Actor* unit = CreateUnit(*choice, side, 0, Order::Attack)) {
					float cost = unit->GetTotalValue(unit->GetModuleID(), 1.0F);
					delete unit;
					priced.emplace_back(choice, cost);
					cheapest = cheapest < 0.0F ? cost : std::min(cheapest, cost);
				}
			}
			float waveBudget = std::min(left, std::max(900.0F, cheapest));
			std::vector<Actor*> wave;
			float waveCost = 0.0F;
			for (int attempt = 0; attempt < 12 && wave.size() < 5; ++attempt) {
				std::vector<const Preset*> affordable;
				for (const auto& [choice, cost]: priced) {
					if (waveCost + cost <= waveBudget) {
						affordable.push_back(choice);
					}
				}
				if (affordable.empty()) {
					break;
				}
				const Preset* pick = affordable[std::min(affordable.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(affordable.size())))];
				Actor* unit = CreateUnit(*pick, side, 0, Order::Attack);
				float cost = unit ? unit->GetTotalValue(unit->GetModuleID(), 1.0F) : 0.0F;
				if (unit && waveCost + cost <= waveBudget) {
					wave.push_back(unit);
					waveCost += cost;
				} else {
					delete unit;
				}
			}
			if (wave.empty()) {
				autoSide.Broke = true;
				continue;
			}
			// (Counted as sent only once a craft took them: with no craft to be had, DropUnits deletes the units and returns nothing.)
			int waveSize = static_cast<int>(wave.size());
			float paid = DropUnits(wave, side, AutoLaneX(side), 0);
			if (paid > 0.0F) {
				autoSide.Sent += waveSize;
				autoSide.Spent += paid;
			}
		}
		// One side left standing wins.
		if (now % 60 == 0) {
			int standing = 0;
			int lastStanding = -1;
			bool anySent = false;
			for (int side = 0; side < c_Sides; ++side) {
				const AutoSide& autoSide = s_AutoSides[side];
				if (!autoSide.Active) {
					continue;
				}
				anySent = anySent || autoSide.Sent > 0;
				if (!autoSide.Broke || Sandbox::CountUnits(side) > 0) {
					++standing;
					lastStanding = side;
				}
			}
			if (anySent && standing <= 1) {
				s_AutoRunning = false;
				s_AutoWinner = standing == 1 ? lastStanding : -1;
				std::string result = s_AutoWinner >= 0 ? std::string(c_SideNames[s_AutoWinner]) + " wins!" : std::string("It's a draw!");
				g_FrameMan.SetScreenText(result, 0, 0, 6000, true);
				g_ConsoleMan.PrintString("SANDBOX: Auto battle over. " + result);
			}
		}
	}

	/// Starts an auto battle between the active sides, the waves landing in lanes about a middle spaced by a width (the view's, taken when
	/// it was asked for).
	void BeginAutoBattle(const Vector& center, float laneWidth) {
		if (!s_CatalogueBuilt) {
			BuildCatalogue();
		}
		s_AutoCenter = center;
		s_AutoLaneWidth = laneWidth;
		long long now = g_TimerMan.GetSimUpdateCount();
		for (int side = 0; side < c_Sides; ++side) {
			AutoSide& autoSide = s_AutoSides[side];
			autoSide.Spent = 0.0F;
			autoSide.Sent = 0;
			autoSide.Broke = false;
			// Staggered, so the first ships don't all arrive at once.
			autoSide.NextWave = now + side * 60;
		}
		s_AutoWinner = -2;
		s_AutoRunning = true;
	}


	/// Notes a tool use as it is applied, for the stroke log and, with the Sandbox debug channel on, the console: the update, the tool, where,
	/// the side and orders, and the choice and count it was made with.
	void LogStroke(const Stroke& stroke) {
		bool toConsole = g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Sandbox);
		if (!g_SettingsMan.ShowSandboxStrokeLog()) {
			s_StrokeLog.clear();
			if (!toConsole) {
				return;
			}
		}
		const char* toolName = "?";
		for (const ToolInfo& tool: c_Tools) {
			if (tool.Kind == stroke.Kind) {
				toolName = tool.Name;
				break;
			}
		}
		int order = static_cast<int>(stroke.Orders);
		const char* orderName = order >= 0 && order < c_OrderCount ? c_Orders[order].Name : "";
		char line[256];
		std::snprintf(line, sizeof(line), "#%lld %s at %d,%d r%d, %s, %s, choice %d x%d", g_TimerMan.GetSimUpdateCount(), toolName, stroke.Position.GetFloorIntX(), stroke.Position.GetFloorIntY(), stroke.Radius,
		              stroke.Team >= 0 && stroke.Team < c_Sides ? c_SideNames[stroke.Team] : "no side", *orderName ? orderName : "?", stroke.Choice, stroke.Count);
		if (toConsole) {
			g_ConsoleMan.PrintString(std::string("SANDBOX: ") + line);
		}
		if (g_SettingsMan.ShowSandboxStrokeLog()) {
			s_StrokeLog.emplace_back(line);
			while (s_StrokeLog.size() > 20) {
				s_StrokeLog.pop_front();
			}
		}
	}
} // namespace SandboxDetail
