// Orders to units: sending, holding and standing orders, selection and group moves.

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
		// (In commander mode, RC-9, only your own side's.)
		return IsCombatant(actor) && !actor->IsInGroup("Brains") && !dynamic_cast<const ACraft*>(actor) && (!s_Commander || actor->GetTeam() == s_CommanderTeam);
	}

	/// Whether an actor is hidden from you in commander mode (RC-9): another side's, where your side can't see. Orders can't be aimed at it.
	bool HiddenFromCommander(const Actor* actor) {
		return s_Commander && actor && actor->GetTeam() != s_CommanderTeam && g_SceneMan.IsUnseen(actor->GetPos().GetFloorIntX(), actor->GetPos().GetFloorIntY(), s_CommanderTeam);
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
		// (A new order goes back to its own movement rule, RC-1; one the standing orders resend keeps what the player set. And a new order the
		// player gives drops the unit's plan, RC-3.)
		if (!resend) {
			standing.Movement = Actor::MOVE_FOLLOW_ORDER;
			standing.PostFacing = 0;
			unit->SetPaceLimit(0.0F);
			s_GuardPosts.erase(unit->GetUniqueID());
			s_BattleDefenders.erase(unit->GetUniqueID());
			DropPlan(unit);
		}
		if (!attack) {
			standing.HasAttackPlace = false;
		}
		// An earlier order still waiting is dropped.
		s_PendingOrders.erase(std::remove_if(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); }), s_PendingOrders.end());
		s_PendingOrders.push_back({MakeRef(unit), waypoint, target, target ? static_cast<long>(target->GetUniqueID()) : 0, attack});
		// A move to a place is watched till it gets there (RC-7); anything else isn't.
		if (!attack && !target) {
			s_MoveWatch[unit->GetUniqueID()] = {waypoint, g_TimerMan.GetSimUpdateCount()};
		} else {
			s_MoveWatch.erase(unit->GetUniqueID());
		}
		// The unit answers the player's order (unit speech): not one the standing orders resend, nor a plan's next step (UpdatePlans has its own).
		if (!resend && !s_FollowingPlan) {
			std::string_view why = reason ? reason : "";
			const char* trigger = nullptr;
			if (why == "move" || why == "to the rally point" || why == "sent again (no route)") {
				trigger = s_MoveAnswer ? s_MoveAnswer : "OrderMove";
			} else if (why == "attack-move") {
				trigger = "OrderAttackMove";
			} else if (why == "attack" || why == "attack (map)") {
				trigger = "OrderAttack";
			} else if (why == "guard") {
				trigger = "OrderGuard";
			} else if (why == "defend at") {
				trigger = "OrderDefend";
			}
			AnswerOrder(unit, trigger);
		}
	}

	/// A unit answers an order the player gave it (unit speech, US-2): one of the trigger's lines over its head, on the speech settings'
	/// chance. Not the unit the player is in, which is the player.
	void AnswerOrder(Actor* unit, const char* trigger) {
		if (unit && trigger && !unit->IsPlayerControlled()) {
			unit->SayOrder(trigger);
		}
	}

	/// The trigger a unit answers one of the side's orders with (unit speech), null for none.
	const char* OrderTrigger(Order order) {
		switch (order) {
			case Order::Hold:
				return "OrderHold";
			case Order::Attack:
			case Order::HuntBrains:
				return "OrderAttack";
			case Order::Patrol:
				return "OrderPatrol";
			case Order::DigGold:
				return "OrderDig";
			default:
				return nullptr;
		}
	}

	/// Holds a unit where it is, forgetting every order it had.
	void HoldUnit(Actor* unit) {
		DropPlan(unit);
		s_MoveWatch.erase(unit->GetUniqueID());
		s_GuardPosts.erase(unit->GetUniqueID());
		s_BattleDefenders.erase(unit->GetUniqueID());
		CancelRetreatAndFlank(unit);
		unit->ClearStandingOrder();
		unit->SetPaceLimit(0.0F);
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
		DropPlan(actor);
		s_MoveWatch.erase(actor->GetUniqueID());
		s_GuardPosts.erase(actor->GetUniqueID());
		s_BattleDefenders.erase(actor->GetUniqueID());
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
	/// Each is where a unit's feet go. The spacing is the sandbox's own (s_Spacing) unless one is given.
	std::vector<Vector> StandingSpots(const Vector& around, int count, float spacing) {
		std::vector<Vector> spots;
		if (count <= 0 || !g_SceneMan.GetScene()) {
			return spots;
		}
		const int sceneHeight = g_SceneMan.GetSceneHeight();
		const float stride = spacing > 0.0F ? std::clamp(spacing, 4.0F, 120.0F) : std::clamp(s_Spacing, 8.0F, 60.0F);
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


	/// Which way a group sent to a point faces: the way given, or else the way it is going (from the middle of the units to the point), or else right.
	int GroupFacing(const std::vector<Actor*>& units, const Vector& point, int facing) {
		if (facing != 0 || units.empty()) {
			return facing < 0 ? -1 : 1;
		}
		float across = 0.0F;
		for (const Actor* unit: units) {
			across += g_SceneMan.ShortestDistance(unit->GetPos(), point, g_SceneMan.SceneWrapsX()).m_X;
		}
		return across < 0.0F ? -1 : 1;
	}

	/// How tough a unit is, for who goes first in a wedge: its health, and how many wounds its body takes.
	float Toughness(const Actor* unit) { return unit->GetHealth() + static_cast<float>(unit->GetGibWoundLimit()) * 0.25F; }

	/// The places for units sent to a point to stand in the chosen formation (RC-5), first the front (or for a line the middle) and on from
	/// there, as many as asked for (more than the units, so those no one can reach can be passed over). Units are given them in order: the
	/// nearest to the point first, or for a wedge the toughest (OrderForFormation).
	std::vector<Vector> FormationSpots(const std::vector<Actor*>& units, const Vector& point, int count, int facing) {
		const float spacing = std::clamp(s_Spacing, 8.0F, 60.0F);
		switch (s_Formation) {
			case Formation::Spread:
				return StandingSpots(point, count, spacing * 2.0F);
			case Formation::Column:
			case Formation::Wedge: {
				// Back from the point, against the way they face: a rank at a time, each on the ground there, never too near one already taken.
				const float behind = -static_cast<float>(GroupFacing(units, point, facing));
				const float stride = s_Formation == Formation::Column ? spacing : std::max(spacing * 0.6F, 8.0F);
				std::vector<Vector> spots;
				for (int rank = 0; rank < count * 3 && static_cast<int>(spots.size()) < count; ++rank) {
					Vector probe = point + Vector(behind * stride * static_cast<float>(rank), 0.0F);
					g_SceneMan.WrapPosition(probe);
					std::vector<Vector> found = StandingSpots(probe, 1, stride);
					if (found.empty() || std::any_of(spots.begin(), spots.end(), [&found, stride](const Vector& taken) { return g_SceneMan.ShortestDistance(taken, found.front(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(stride * 0.7F); })) {
						continue;
					}
					spots.push_back(found.front());
				}
				return spots;
			}
			default:
				return StandingSpots(point, count);
		}
	}

	/// Puts units in the order they take the formation's places in (FormationSpots): the nearest to the point first, or for a wedge the toughest.
	void OrderForFormation(std::vector<Actor*>& units, const Vector& point) {
		if (s_Formation == Formation::Wedge) {
			std::stable_sort(units.begin(), units.end(), [](const Actor* a, const Actor* b) { return Toughness(a) > Toughness(b); });
			return;
		}
		std::stable_sort(units.begin(), units.end(), [&point](const Actor* a, const Actor* b) {
			return g_SceneMan.ShortestDistance(point, a->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude() < g_SceneMan.ShortestDistance(point, b->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
		});
	}

	/// The pace a unit walks at, m/s, for keeping a group to its slowest (RC-5); 0 for one that doesn't walk.
	float WalkPace(Actor* unit) {
		if (AHuman* human = dynamic_cast<AHuman*>(unit)) {
			return human->GetLimbPathTravelSpeed(Actor::WALK) * 0.5F;
		}
		if (ACrab* crab = dynamic_cast<ACrab*>(unit)) {
			return crab->GetLimbPathTravelSpeed(Actor::WALK) * 0.5F;
		}
		return 0.0F;
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
		for (const Vector& spot: FormationSpots(units, point, static_cast<int>(units.size()) * 2)) {
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
	void MoveUnitsTo(std::vector<Actor*> units, const Vector& point, bool attackMove, int facing) {
		std::vector<Vector> spots = FormationSpots(units, point, static_cast<int>(units.size()) * 2, facing);
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
			} else {
				// Nowhere there they can get to (RC-7): sent to the nearest spots all the same, as before, and marked as having no route.
				for (Actor* unit: units) {
					AddNoRoute(unit, point);
				}
			}
		}
		spots.resize(std::min(spots.size(), units.size()));
		OrderForFormation(units, point);
		// Kept together (RC-5): each no faster than the slowest walker among them, till it gets there (UpdatePace).
		float pace = 0.0F;
		if (s_KeepPace && units.size() > 1) {
			for (Actor* unit: units) {
				if (float own = WalkPace(unit); own > 0.0F && (pace == 0.0F || own < pace)) {
					pace = own;
				}
			}
		}
		for (size_t i = 0; i < units.size(); ++i) {
			Actor* unit = units[i];
			const Vector& spot = spots[std::min(i, spots.size() - 1)];
			// The waypoint just over the ground where the feet go (the AI puts it at its own standing height from there). Half the unit's height up,
			// as it was, was inside the ceiling of a low corridor, and a waypoint inside a thin slab is taken to be on top of it: a unit sent a few
			// steps along a bunker corridor went out and round to the roof over it.
			// (Sent as a group kept together, they answer as one: unit speech.)
			s_MoveAnswer = pace > 0.0F ? "OrderKeepTogether" : nullptr;
			SendUnit(unit, spot + Vector(0.0F, -4.0F), nullptr, false, attackMove ? "attack-move" : "move");
			s_MoveAnswer = nullptr;
			if (attackMove) {
				// Attack-move (RC-2): a move whose movement rule is Engage, so the unit's own sight picks what it fights on the way (no target is
				// chosen for it), and once nothing is left its route takes it on to the spot.
				unit->SetMovementRule(Actor::MOVE_ENGAGE);
			}
			// Facing the way given when they get there (RC-5), as a defend-at does (RC-4).
			unit->SetOrderPostFacing(facing);
			if (pace > 0.0F) {
				unit->SetPaceLimit(pace);
				s_Paced.push_back(MakeRef(unit));
			}
		}
	}

	/// A move (or attack-move, in that mode) to a point, facing the way dragged when they get there (RC-5); with Shift, a step of their plans.
	void FacingMoveSelected(const Vector& point, const Vector& facingPoint, bool shift) {
		float across = g_SceneMan.ShortestDistance(point, facingPoint, g_SceneMan.SceneWrapsX()).m_X;
		int facing = across > 12.0F ? 1 : (across < -12.0F ? -1 : 0);
		bool attackMove = s_CommandMode == CommandMode::AttackMove;
		if (shift) {
			PlanStepFor(UnitsToMove(0, true), attackMove ? PlanKind::AttackMove : PlanKind::Move, point, nullptr, facing);
		} else {
			MoveUnitsTo(UnitsToMove(0, true), point, attackMove, facing);
		}
		MarkOrder(point, c_CommandModeColors[static_cast<int>(attackMove ? CommandMode::AttackMove : CommandMode::Move)]);
	}

	/// Marks a place a unit can't get to (RC-7), with any other marker near it, so a group sent there has one marker.
	void AddNoRoute(Actor* unit, const Vector& destination) {
		long long now = g_TimerMan.GetSimUpdateCount();
		auto near = std::find_if(s_NoRoutes.begin(), s_NoRoutes.end(), [&destination](const NoRoute& marker) { return g_SceneMan.ShortestDistance(marker.Destination, destination, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(40.0F); });
		if (near == s_NoRoutes.end()) {
			s_NoRoutes.push_back({destination, {}, now});
			near = std::prev(s_NoRoutes.end());
		}
		near->At = now;
		if (std::none_of(near->Units.begin(), near->Units.end(), [unit](const UnitRef& ref) { return RefersTo(ref, unit); })) {
			near->Units.push_back(MakeRef(unit));
			// It says it can't get there (unit speech).
			if (!unit->IsPlayerControlled()) {
				unit->Say("NoRoute");
			}
		}
	}

	/// Each unit sent somewhere (RC-7): forgotten once it gets there, and marked "no route" if it has stopped going (on no route, with
	/// nothing left to walk) well short of it a second or more after the order. Old markers fade.
	void UpdateMoveWatch() {
		long long now = g_TimerMan.GetSimUpdateCount();
		for (auto watch = s_MoveWatch.begin(); watch != s_MoveWatch.end();) {
			Actor* unit = nullptr;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (static_cast<long>(actor->GetUniqueID()) == watch->first) {
					unit = actor;
					break;
				}
			}
			if (!unit) {
				watch = s_MoveWatch.erase(watch);
				continue;
			}
			float distance = g_SceneMan.ShortestDistance(unit->GetPos(), watch->second.Destination, g_SceneMan.SceneWrapsX()).GetMagnitude();
			bool pending = std::any_of(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); });
			bool stopped = !pending && now - watch->second.Issued > 60 && unit->GetAIMode() != Actor::AIMODE_GOTO && unit->GetWaypointsSize() == 0;
			if (distance < 60.0F || unit->IsPlayerControlled()) {
				watch = s_MoveWatch.erase(watch);
			} else if (stopped) {
				AddNoRoute(unit, watch->second.Destination);
				watch = s_MoveWatch.erase(watch);
			} else {
				++watch;
			}
		}
		s_NoRoutes.erase(std::remove_if(s_NoRoutes.begin(), s_NoRoutes.end(), [now](const NoRoute& marker) { return now - marker.At > c_NoRouteUpdates || std::none_of(marker.Units.begin(), marker.Units.end(), [](const UnitRef& ref) { return GetRef(ref) != nullptr; }); }), s_NoRoutes.end());
	}

	/// Sends the units of the "no route" marker at a place (RC-7) there again (the ground may have changed, or a door opened), and drops
	/// the marker; they are watched again as any move is.
	void ReissueNoRoute(const Vector& destination) {
		auto marker = std::find_if(s_NoRoutes.begin(), s_NoRoutes.end(), [&destination](const NoRoute& each) { return g_SceneMan.ShortestDistance(each.Destination, destination, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(4.0F); });
		if (marker == s_NoRoutes.end()) {
			return;
		}
		std::vector<UnitRef> units = marker->Units;
		s_NoRoutes.erase(marker);
		for (const UnitRef& ref: units) {
			if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled()) {
				SendUnit(unit, destination, nullptr, false, "sent again (no route)");
			}
		}
		MarkOrder(destination, IM_COL32(110, 180, 250, 255));
	}

	/// The selected units' order at a place clicked on the map (RC-8), as the command mode says, but for the place alone: on the map a click
	/// can't pick out one unit, so an attack goes for the nearest enemy to it, a guard for the nearest friend not selected, and a move is
	/// a move whatever is there. With Shift, a step of their plans.
	void MapOrder(const Vector& point, bool shift) {
		std::vector<Actor*> units = UnitsToMove(0, true);
		if (units.empty()) {
			return;
		}
		int team = SelectionTeam();
		switch (s_CommandMode) {
			case CommandMode::Attack:
				if (Actor* enemy = EnemyNear(point, team)) {
					if (shift) {
						PlanStepFor(units, PlanKind::Attack, enemy->GetPos(), enemy);
					} else {
						for (Actor* unit: units) {
							SendUnit(unit, enemy->GetPos(), enemy, true, "attack (map)", true);
						}
					}
					MarkOrder(enemy->GetPos(), c_CommandModeColors[static_cast<int>(CommandMode::Attack)]);
				}
				return;
			case CommandMode::Guard: {
				Actor* friendNear = nullptr;
				float nearest = 120.0F * 120.0F;
				for (Actor* actor: SandboxAccess::Actors()) {
					if (actor->GetTeam() != team || !IsSelectable(actor) || std::find(units.begin(), units.end(), actor) != units.end()) {
						continue;
					}
					if (float distance = g_SceneMan.ShortestDistance(point, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude(); distance < nearest) {
						nearest = distance;
						friendNear = actor;
					}
				}
				if (friendNear) {
					if (shift) {
						PlanStepFor(units, PlanKind::Guard, friendNear->GetPos(), friendNear);
					} else {
						for (Actor* unit: units) {
							GuardUnit(unit, friendNear);
						}
					}
					MarkOrder(friendNear->GetPos(), c_CommandModeColors[static_cast<int>(CommandMode::Guard)]);
				}
				return;
			}
			case CommandMode::DefendAt:
				DefendAtSelected(point, point, shift);
				return;
			default: {
				bool attackMove = s_CommandMode == CommandMode::AttackMove;
				if (shift) {
					PlanStepFor(units, attackMove ? PlanKind::AttackMove : PlanKind::Move, point, nullptr);
				} else {
					MoveUnitsTo(units, point, attackMove);
				}
				MarkOrder(point, c_CommandModeColors[static_cast<int>(attackMove ? CommandMode::AttackMove : CommandMode::Move)]);
				return;
			}
		}
	}

	/// What can be guarded at a point other than a unit (RC-10): your side's craft, or a loose object (a crate, a dropped weapon), the nearest within reach.
	MovableObject* GuardableObjectAt(const Vector& position, int team) {
		MovableObject* found = nullptr;
		float nearest = 30.0F * 30.0F;
		for (Actor* actor: SandboxAccess::Actors()) {
			if (const ACraft* craft = dynamic_cast<const ACraft*>(actor); craft && actor->GetTeam() == team) {
				float reach = std::max(actor->GetRadius(), 30.0F);
				if (float distance = g_SceneMan.ShortestDistance(position, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude(); distance < reach * reach && (!found || distance < nearest)) {
					found = actor;
					nearest = distance;
				}
			}
		}
		if (found) {
			return found;
		}
		for (MovableObject* item: SandboxAccess::Items()) {
			if (float distance = g_SceneMan.ShortestDistance(position, item->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude(); distance < nearest) {
				found = item;
				nearest = distance;
			}
		}
		return found;
	}

	/// The colony building whose plot a point is on (RC-10), if any.
	const Colony::Building* BuildingAt(const Vector& position) {
		for (const Colony::Building& building: Colony::Buildings()) {
			const Colony::Type& type = Colony::GetType(building.What);
			Vector off = g_SceneMan.ShortestDistance(building.Ground, position, g_SceneMan.SceneWrapsX());
			if (std::abs(off.m_X) <= static_cast<float>(type.Width) * 0.5F && off.m_Y <= 4.0F && off.m_Y >= -static_cast<float>(type.Height) - 8.0F) {
				return &building;
			}
		}
		return nullptr;
	}

	/// Sets units to guard a craft, an object or a building (RC-10): each holds a post round it, as a defend does, fighting from there; the
	/// posts follow the thing when it moves (UpdateGuards).
	void GuardObject(const std::vector<Actor*>& units, MovableObject* object, const Colony::Building* building) {
		if (units.empty() || (!object && !building)) {
			return;
		}
		Vector place = object ? object->GetPos() : building->Ground;
		std::vector<Vector> spots = StandingSpots(place, static_cast<int>(units.size()));
		for (size_t i = 0; i < units.size(); ++i) {
			Actor* unit = units[i];
			if (unit == object) {
				continue;
			}
			Vector spot = spots.empty() ? place : spots[std::min(i, spots.size() - 1)];
			SendUnit(unit, spot + Vector(0.0F, -4.0F), nullptr, false, "guard");
			unit->SetOrderPost(spot);
			s_GuardPosts[unit->GetUniqueID()] = {object ? static_cast<long>(object->GetUniqueID()) : 0, building ? building->ID : 0, place};
		}
	}

	/// Keeps guards by what they guard (RC-10), every half second: a post moved along when the thing has moved a way, and the guard let go
	/// (holding where it is) when the thing is gone or the unit was given another order.
	void UpdateGuards() {
		for (auto guard = s_GuardPosts.begin(); guard != s_GuardPosts.end();) {
			Actor* unit = nullptr;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (static_cast<long>(actor->GetUniqueID()) == guard->first) {
					unit = actor;
					break;
				}
			}
			if (!unit || !unit->GetOrderHasPost()) {
				guard = s_GuardPosts.erase(guard);
				continue;
			}
			bool gone = true;
			Vector now;
			if (guard->second.ObjectID != 0) {
				if (const MovableObject* object = g_MovableMan.FindObjectByUniqueID(guard->second.ObjectID)) {
					now = object->GetPos();
					gone = false;
				}
			} else {
				for (const Colony::Building& building: Colony::Buildings()) {
					if (building.ID == guard->second.BuildingID) {
						now = building.Ground;
						gone = false;
						break;
					}
				}
			}
			if (gone) {
				guard = s_GuardPosts.erase(guard);
				continue;
			}
			if (g_SceneMan.ShortestDistance(guard->second.Place, now, g_SceneMan.SceneWrapsX()).MagnitudeIsGreaterThan(40.0F)) {
				// Its post moves with it, the same way off it as before.
				Vector post = unit->GetOrderPost() + g_SceneMan.ShortestDistance(guard->second.Place, now, g_SceneMan.SceneWrapsX());
				g_SceneMan.WrapPosition(post);
				std::vector<Vector> spot = StandingSpots(post, 1);
				post = spot.empty() ? post : spot.front();
				SendUnit(unit, post + Vector(0.0F, -4.0F), nullptr, false, "guard (moved)", false, true);
				unit->SetOrderPost(post);
				guard->second.Place = now;
			}
			++guard;
		}
	}

	/// Lets each unit kept to a group's pace (RC-5) walk at its own again once it has got there, been given another order, or been taken over.
	void UpdatePace() {
		s_Paced.erase(std::remove_if(s_Paced.begin(), s_Paced.end(), [](const UnitRef& ref) {
			Actor* unit = GetRef(ref);
			if (!unit) {
				return true;
			}
			bool pending = std::any_of(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); });
			bool arrived = !pending && unit->GetAIMode() != Actor::AIMODE_GOTO && unit->GetWaypointsSize() == 0;
			if (unit->GetPaceLimit() == 0.0F || arrived || unit->IsPlayerControlled()) {
				unit->SetPaceLimit(0.0F);
				return true;
			}
			return false;
		}), s_Paced.end());
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
		if (!s_FollowingPlan) {
			AnswerOrder(unit, "OrderGuard");
		}
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
		if (HiddenFromCommander(target)) {
			// (What your side can't see isn't there to click on, RC-9.)
			target = nullptr;
		}
		if (g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Sandbox)) {
			g_ConsoleMan.PrintString("SANDBOX: command at " + std::to_string(static_cast<int>(position.m_X)) + "," + std::to_string(static_cast<int>(position.m_Y)) + " selected " + std::to_string(s_Selected.size()) + " target " + (target ? target->GetPresetName() : std::string("none")) + " mode " + std::to_string(static_cast<int>(s_CommandMode)));
		}
		bool friendly = target && IsSelectable(target) && (s_Selected.empty() || target->GetTeam() == SelectionTeam());
		bool selected = target && std::any_of(s_Selected.begin(), s_Selected.end(), [target](const UnitRef& ref) { return RefersTo(ref, target); });
		if (s_CommandMode == CommandMode::Guard) {
			// Follow the friend clicked, or (RC-10) your side's brain; or stand guard by a craft, a crate or a colony building. With nothing
			// there, nothing happens.
			const Colony::Building* building = target ? nullptr : BuildingAt(position);
			MovableObject* object = !target || dynamic_cast<const ACraft*>(target) ? GuardableObjectAt(position, SelectionTeam()) : nullptr;
			if (target && target->IsInGroup("Brains") && target->GetTeam() == SelectionTeam() && !dynamic_cast<const ACraft*>(target)) {
				for (const UnitRef& ref: s_Selected) {
					if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled() && unit != target) {
						GuardUnit(unit, target);
					}
				}
				MarkOrder(target->GetPos(), c_CommandModeColors[static_cast<int>(CommandMode::Guard)]);
				return;
			}
			if (!(friendly && !selected) && (object || building)) {
				GuardObject(UnitsToMove(0, true), object, building);
				MarkOrder(object ? object->GetPos() : building->Ground, c_CommandModeColors[static_cast<int>(CommandMode::Guard)]);
				return;
			}
			if (friendly && !selected) {
				if (modifier == 1) {
					// Shift: a step of the plan (RC-3).
					PlanStepFor(UnitsToMove(0, true), PlanKind::Guard, target->GetPos(), target);
				} else {
					for (const UnitRef& ref: s_Selected) {
						if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled() && unit != target) {
							GuardUnit(unit, target);
						}
					}
				}
				MarkOrder(target->GetPos(), IM_COL32(120, 220, 120, 255));
			}
			return;
		}
		if (s_CommandMode == CommandMode::Attack) {
			if (modifier == 1) {
				if (Actor* enemy = EnemyNear(position, SelectionTeam())) {
					PlanStepFor(UnitsToMove(0, true), PlanKind::Attack, enemy->GetPos(), enemy);
					MarkOrder(enemy->GetPos(), c_CommandModeColors[static_cast<int>(CommandMode::Attack)]);
				}
				return;
			}
			OrderSelectedUnits(1, position);
			return;
		}
		if (s_CommandMode == CommandMode::AttackMove) {
			if (modifier == 1) {
				PlanStepFor(UnitsToMove(0, true), PlanKind::AttackMove, position, nullptr);
			} else {
				MoveUnitsTo(UnitsToMove(0, true), position, true);
			}
			MarkOrder(position, c_CommandModeColors[static_cast<int>(CommandMode::AttackMove)]);
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
					Vector onScreen = FromCamera(actor->GetPos());
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
		if (attack && modifier == 1) {
			// Shift: attacking it is a step of the plan (RC-3).
			PlanStepFor(UnitsToMove(0, true), PlanKind::Attack, target->GetPos(), target);
			MarkOrder(target->GetPos(), c_CommandModeColors[static_cast<int>(CommandMode::Attack)]);
		} else if (attack) {
			for (const UnitRef& ref: s_Selected) {
				// (Never at one of its own side, whatever the selection holds.)
				if (Actor* unit = GetRef(ref); unit && !unit->IsPlayerControlled() && unit->GetTeam() != target->GetTeam()) {
					SendUnit(unit, target->GetPos(), target, true, "attack", true);
				}
			}
			MarkOrder(target->GetPos(), IM_COL32(239, 106, 91, 255));
		} else if (modifier == 1) {
			// Shift: on to here after what they're doing, a step of the plan (RC-3).
			PlanStepFor(UnitsToMove(0, true), PlanKind::Move, position, nullptr);
			MarkOrder(position, IM_COL32(110, 180, 250, 255));
		} else {
			MoveUnitsTo(UnitsToMove(0, true), position);
			MarkOrder(position, IM_COL32(110, 180, 250, 255));
		}
	}

	/// The nearest enemy of a side to a point, within 400 px: what Attack goes for at a click near it. Null for none.
	Actor* EnemyNear(const Vector& point, int team) {
		Actor* target = nullptr;
		float nearest = 400.0F * 400.0F;
		for (Actor* actor: SandboxAccess::Actors()) {
			if (!IsCombatant(actor) || actor->IsIgnoredByAI() || actor->GetTeam() == team || HiddenFromCommander(actor)) {
				continue;
			}
			float distance = g_SceneMan.ShortestDistance(point, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
			if (distance < nearest) {
				nearest = distance;
				target = actor;
			}
		}
		return target;
	}

	/// Forgets a unit's plan (RC-3): any order given to it other than by its plan.
	void DropPlan(const Actor* unit) {
		if (!s_FollowingPlan && unit) {
			s_Plans.erase(unit->GetUniqueID());
		}
	}

	/// Gives a unit a step of its plan now.
	void StartPlanStep(Actor* unit, Plan& plan, const PlanStep& step) {
		s_FollowingPlan = true;
		Actor* target = GetRef(step.Target);
		switch (step.Kind) {
			case PlanKind::Move:
			case PlanKind::AttackMove:
				// (Just over the ground under the spot, as for a move: see MoveUnitsTo.)
				SendUnit(unit, step.Place + Vector(0.0F, -4.0F), nullptr, false, step.Kind == PlanKind::Move ? "move (plan)" : "attack-move (plan)");
				if (step.Kind == PlanKind::AttackMove) {
					unit->SetMovementRule(Actor::MOVE_ENGAGE);
				}
				unit->SetOrderPostFacing(step.Facing);
				break;
			case PlanKind::Attack:
				if (target) {
					SendUnit(unit, target->GetPos(), target, true, "attack (plan)", true);
				}
				break;
			case PlanKind::Guard:
				if (target && target != unit) {
					GuardUnit(unit, target);
				}
				break;
			case PlanKind::Defend:
				HoldUnit(unit);
				unit->SetOrderPost(unit->GetPos());
				unit->SetOrderPostFacing(step.Facing);
				break;
			case PlanKind::Wait:
				// (It stays where the step before left it: nothing to give.)
				break;
		}
		s_FollowingPlan = false;
		plan.Current = step;
		plan.Running = true;
		plan.Started = g_TimerMan.GetSimUpdateCount();
	}

	/// Whether the step a unit is on is over, so the next can start: a move when it has arrived (no longer going anywhere), an attack when
	/// the enemy is dead, a guard when the friend is gone; a defend never is. Half a second's grace first, for the order to be taken up.
	bool PlanStepDone(Actor* unit, const Plan& plan) {
		if (g_TimerMan.GetSimUpdateCount() - plan.Started < 30 || std::any_of(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); })) {
			return false;
		}
		const Actor* target = GetRef(plan.Current.Target);
		switch (plan.Current.Kind) {
			case PlanKind::Move:
			case PlanKind::AttackMove:
				return unit->GetAIMode() != Actor::AIMODE_GOTO && unit->GetWaypointsSize() == 0;
			case PlanKind::Attack:
			case PlanKind::Guard:
				return !target || target->IsDead();
			case PlanKind::Defend:
				return false;
			case PlanKind::Wait:
				return g_TimerMan.GetSimUpdateCount() - plan.Started >= plan.Current.Updates;
		}
		return false;
	}

	/// Adds a step to the plans of units (a shift-click, RC-3). A unit with nothing under way starts it at once; one carrying out an order
	/// starts it once that order is over. Moves and attack-moves spread the units over standing spots round the place, as a move does; a
	/// defend holds where the step before leaves the unit.
	void PlanStepFor(std::vector<Actor*> units, PlanKind kind, const Vector& place, Actor* target, int facing) {
		bool toPlace = kind == PlanKind::Move || kind == PlanKind::AttackMove;
		std::vector<Vector> spots = toPlace ? StandingSpots(place, static_cast<int>(units.size())) : std::vector<Vector>();
		if (toPlace) {
			std::sort(units.begin(), units.end(), [&place](Actor* a, Actor* b) {
				return g_SceneMan.ShortestDistance(place, a->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude() < g_SceneMan.ShortestDistance(place, b->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
			});
		}
		for (size_t i = 0; i < units.size(); ++i) {
			Actor* unit = units[i];
			if (unit->IsPlayerControlled() || (target && (kind == PlanKind::Attack ? target->GetTeam() == unit->GetTeam() : target == unit))) {
				continue;
			}
			Plan& plan = s_Plans[unit->GetUniqueID()];
			plan.Unit = MakeRef(unit);
			PlanStep step;
			step.Kind = kind;
			step.Target = MakeRef(target);
			step.Facing = facing;
			if (toPlace) {
				step.Place = spots.empty() ? place : spots[std::min(i, spots.size() - 1)];
			} else if (kind == PlanKind::Defend) {
				step.Place = !plan.Steps.empty() ? plan.Steps.back().Place : (plan.Running ? plan.Current.Place : unit->GetPos());
			} else {
				step.Place = target ? target->GetPos() : place;
			}
			bool busy = unit->GetAIMode() == Actor::AIMODE_GOTO || unit->GetAIMode() == Actor::AIMODE_SQUAD || std::any_of(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); });
			if (!plan.Running && plan.Steps.empty() && busy) {
				// What it is doing now is the plan's first step, done when it gets there.
				plan.Running = true;
				plan.Current = PlanStep();
				plan.Current.Place = unit->GetLastAIWaypoint();
				plan.Started = g_TimerMan.GetSimUpdateCount();
			}
			if (!plan.Running && plan.Steps.empty()) {
				StartPlanStep(unit, plan, step);
				// (Given now, it's answered as the order it is; the step's own sending is a plan's, which doesn't answer.)
				const char* const answers[] = {"OrderMove", "OrderAttackMove", "OrderAttack", "OrderGuard", "OrderDefend", nullptr};
				int kindIndex = static_cast<int>(step.Kind);
				AnswerOrder(unit, kindIndex >= 0 && kindIndex < static_cast<int>(std::size(answers)) ? answers[kindIndex] : nullptr);
			} else {
				plan.Steps.push_back(step);
				AnswerOrder(unit, "OrderQueued");
			}
		}
	}

	/// Adds a patrol's leg to a plan: on to the point, fighting what is met (an attack-move, so it closes in on what it sees and then carries
	/// on), and a pause there.
	void PatrolLeg(Plan& plan, const Vector& point) {
		PlanStep move;
		move.Kind = PlanKind::AttackMove;
		move.Place = point;
		plan.Steps.push_back(move);
		PlanStep wait;
		wait.Kind = PlanKind::Wait;
		wait.Place = point;
		wait.Updates = static_cast<int>(3.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F));
		plan.Steps.push_back(wait);
	}

	/// Moves each unit with a plan on to its next step when the one it is on is over, and forgets the plans of units that are gone or have
	/// finished. Once a sim update.
	void UpdatePlans() {
		for (auto entry = s_Plans.begin(); entry != s_Plans.end();) {
			Plan& plan = entry->second;
			Actor* unit = GetRef(plan.Unit);
			if (!unit || unit->IsDead()) {
				entry = s_Plans.erase(entry);
				continue;
			}
			if (plan.Running && !PlanStepDone(unit, plan)) {
				++entry;
				continue;
			}
			if (plan.Steps.empty() && !plan.Route.empty()) {
				// A patrol: round its points again (RC-4). A loop goes from the last back to the first; back and forth turns at each end, the
				// point it is at not walked to again.
				size_t count = plan.Route.size();
				std::vector<size_t> order;
				if (!plan.BackAndForth) {
					for (size_t i = 0; i < count; ++i) {
						order.push_back(i);
					}
				} else {
					plan.Forward = !plan.Forward;
					for (size_t i = 1; i < count; ++i) {
						order.push_back(plan.Forward ? i : count - 1 - i);
					}
				}
				for (size_t i: order) {
					PatrolLeg(plan, plan.Route[i]);
				}
			}
			if (plan.Steps.empty()) {
				entry = s_Plans.erase(entry);
				continue;
			}
			PlanStep step = plan.Steps.front();
			plan.Steps.pop_front();
			// On to the next step of a plan the player gave (unit speech): not a patrol's legs, which go round for ever, nor a pause.
			bool next = plan.Running && plan.Route.empty() && step.Kind != PlanKind::Wait;
			StartPlanStep(unit, plan, step);
			if (next && !unit->IsPlayerControlled()) {
				unit->Say("PlanNext");
			}
			++entry;
		}
	}

	/// Posts the selected units round a point to hold it (RC-4), facing the way the button was dragged (to facingPoint) if it was dragged
	/// far enough to tell. Each walks to its own spot round the point and holds ground there, and is sent back if moved off. With Shift,
	/// going there and holding are the next steps of their plans.
	void DefendAtSelected(const Vector& point, const Vector& facingPoint, bool shift) {
		std::vector<Actor*> units = UnitsToMove(0, true);
		float across = g_SceneMan.ShortestDistance(point, facingPoint, g_SceneMan.SceneWrapsX()).m_X;
		int facing = across > 12.0F ? 1 : (across < -12.0F ? -1 : 0);
		if (shift) {
			PlanStepFor(units, PlanKind::Move, point, nullptr);
			PlanStepFor(units, PlanKind::Defend, point, nullptr, facing);
		} else {
			std::vector<Vector> spots = StandingSpots(point, static_cast<int>(units.size()));
			std::sort(units.begin(), units.end(), [&point](Actor* a, Actor* b) {
				return g_SceneMan.ShortestDistance(point, a->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude() < g_SceneMan.ShortestDistance(point, b->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
			});
			for (size_t i = 0; i < units.size(); ++i) {
				Vector spot = spots.empty() ? point : spots[std::min(i, spots.size() - 1)];
				// (Just over the ground under the spot, as for a move: see MoveUnitsTo.) Going there it's a move (OrderKind); there, a defend.
				SendUnit(units[i], spot + Vector(0.0F, -4.0F), nullptr, false, "defend at");
				units[i]->SetOrderPost(spot);
				units[i]->SetOrderPostFacing(facing);
			}
		}
		MarkOrder(point, c_CommandModeColors[static_cast<int>(CommandMode::DefendAt)]);
	}

	/// Sends the selected units on a patrol (RC-4): round the points in order, fighting what they meet and pausing at each, then round again,
	/// as a loop or back and forth. Each unit has its own spot at each point, as a move gives it. A patrol is a plan that never runs out, so
	/// any other order ends it.
	void PatrolSelected(const std::vector<Vector>& points, bool backAndForth) {
		std::vector<Actor*> units = UnitsToMove(0, true);
		if (points.empty() || units.empty()) {
			return;
		}
		std::vector<std::vector<Vector>> spots;
		for (const Vector& point: points) {
			spots.push_back(StandingSpots(point, static_cast<int>(units.size())));
		}
		const Vector& first = points.front();
		std::sort(units.begin(), units.end(), [&first](Actor* a, Actor* b) {
			return g_SceneMan.ShortestDistance(first, a->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude() < g_SceneMan.ShortestDistance(first, b->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
		});
		for (size_t i = 0; i < units.size(); ++i) {
			Actor* unit = units[i];
			if (unit->IsPlayerControlled()) {
				continue;
			}
			HoldUnit(unit);
			Plan& plan = s_Plans[unit->GetUniqueID()];
			plan = Plan();
			plan.Unit = MakeRef(unit);
			plan.BackAndForth = backAndForth;
			for (size_t p = 0; p < points.size(); ++p) {
				plan.Route.push_back(spots[p].empty() ? points[p] : spots[p][std::min(i, spots[p].size() - 1)]);
			}
			for (const Vector& point: plan.Route) {
				PatrolLeg(plan, point);
			}
			PlanStep step = plan.Steps.front();
			plan.Steps.pop_front();
			StartPlanStep(unit, plan, step);
			AnswerOrder(unit, "OrderPatrol");
		}
		for (const Vector& point: points) {
			MarkOrder(point, c_CommandModeColors[static_cast<int>(CommandMode::Patrol)]);
		}
	}

	/// The command ring's choices for the selected units, about a point: 0 move there, 1 attack the enemy nearest it, 2 cancel, 3 defend where they
	/// are; 13 defend as the last step of their plans, 20 clear their plans (RC-3).
	void OrderSelectedUnits(int choice, const Vector& point) {
		std::vector<Actor*> units = UnitsToMove(0, true);
		if (choice == 0) {
			MoveUnitsTo(units, point);
		} else if (choice == 1) {
			// The nearest enemy to the point, if there is one close: the player's pick, kept after while it lives. With none near, nothing (RC-2:
			// fighting towards a place is Attack-move's).
			Actor* target = units.empty() ? nullptr : EnemyNear(point, units.front()->GetTeam());
			if (!target) {
				return;
			}
			for (Actor* unit: units) {
				SendUnit(unit, target->GetPos(), target, true, "attack", true);
			}
			MarkOrder(target->GetPos(), c_CommandModeColors[static_cast<int>(CommandMode::Attack)]);
		} else if (choice == 2) {
			// Cancel: every order forgotten, and the side's standing orders apply.
			for (Actor* unit: units) {
				HoldUnit(unit);
				if (static_cast<Order>(s_Order) != Order::MoveTo) {
					GiveOrder(unit, static_cast<Order>(s_Order));
				}
				AnswerOrder(unit, "OrderCancel");
				MarkOrder(unit->GetPos(), IM_COL32(200, 160, 120, 255));
			}
		} else if (choice == 3) {
			// Defend: stand this ground and fight from it, moving as little as can be; a unit shoved or drawn off its post is sent back.
			for (Actor* unit: units) {
				HoldUnit(unit);
				unit->SetOrderPost(unit->GetPos());
				AnswerOrder(unit, "OrderDefend");
				MarkOrder(unit->GetPos(), IM_COL32(242, 182, 61, 255));
			}
		} else if (choice == 13) {
			// Defend with Shift: holding ground where the plan leaves them, as its last step (RC-3).
			PlanStepFor(units, PlanKind::Defend, point, nullptr);
		} else if (choice == 20) {
			// The plans of the units picked, cleared (they carry on with the step they're on).
			for (Actor* unit: units) {
				if (auto plan = s_Plans.find(unit->GetUniqueID()); plan != s_Plans.end()) {
					plan->second.Steps.clear();
				}
			}
		}
	}

	/// Drops one step from a unit's plan (a right click on its marker, RC-3).
	void DropPlanStep(long unitID, int step) {
		if (auto plan = s_Plans.find(unitID); plan != s_Plans.end() && step >= 0 && static_cast<size_t>(step) < plan->second.Steps.size()) {
			plan->second.Steps.erase(plan->second.Steps.begin() + step);
		}
	}

	/// The engagement rule the selected units share (RC-1), the weapons rule or the movement rule: its value, -1 when they differ, -2 with none selected.
	int SelectedRule(bool weapons) {
		int shared = -2;
		for (const UnitRef& ref: s_Selected) {
			if (const Actor* unit = GetRef(ref)) {
				int rule = weapons ? unit->GetWeaponRule() : unit->GetMovementRule();
				if (shared == -2) {
					shared = rule;
				} else if (shared != rule) {
					return -1;
				}
			}
		}
		return shared;
	}

	/// The AI mode the selected units share, as the command menu lists them (Sentry, Hunt brains, Dig for gold, Rally point, Do nothing):
	/// its place, or -1 when they differ, have none of those (on their way somewhere) or none are selected.
	int SelectedAIMode() {
		int shared = -2;
		for (const UnitRef& ref: s_Selected) {
			if (const Actor* unit = GetRef(ref)) {
				int mode = -1;
				switch (unit->GetAIMode()) {
					case Actor::AIMODE_SENTRY:
						mode = 0;
						break;
					case Actor::AIMODE_BRAINHUNT:
						mode = 1;
						break;
					case Actor::AIMODE_GOLDDIG:
						mode = 2;
						break;
					case Actor::AIMODE_NONE:
						mode = 4;
						break;
					default:
						break;
				}
				if (shared == -2) {
					shared = mode;
				} else if (shared != mode) {
					return -1;
				}
			}
		}
		return shared < 0 ? -1 : shared;
	}

	/// Gives the selected units an engagement rule (RC-1), on the next sim update like any order.
	void QueueRule(bool weapons, int rule) {
		Stroke stroke;
		stroke.Kind = Tool::OrderSelected;
		stroke.Count = (weapons ? 200 : 300) + rule;
		s_Queue.push_back(stroke);
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

	/// The faction's units the Battle Director can buy: soldiers mostly, the odd crab.
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
