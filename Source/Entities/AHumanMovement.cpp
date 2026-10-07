// The route-follower: how a humanoid under AI gets along the route the pathfinder gave it (AHuman::MoveAlongRoute). It owns every leg
// of the way, walks, crawls, drops, flights, ladders and doors, with one view of the route and one hand on the jet, where the movement
// script it replaces (SharedBehaviors.GoToWpt) had grown into a dozen controllers handing the unit between them, and every hand-over was
// a stutter of the jet or a turn in the air. Flights are flown by AHuman::PilotFlight. The script keeps its hook: it calls this each
// tick from a coroutine of its own (SharedBehaviors.GoToRoute), and a mod that replaces that keeps working.
#include "AHuman.h"
#include "ADoor.h"
#include "AEJetpack.h"
#include "ConsoleMan.h"
#include "TimerMan.h"
#include "MovableMan.h"
#include "SceneMan.h"
#include "SettingsMan.h"
#include "Scene.h"
#include "PresetMan.h"
#include "Activity.h"

using namespace RTE;

namespace {
	Vector Towards(const Vector& from, const Vector& to) {
		return g_SceneMan.ShortestDistance(from, to, g_SceneMan.SceneWrapsX());
	}

	// The floor under a point within so far: its y, or below zero for none.
	float FloorUnder(const Vector& point, float reach) {
		Vector hit;
		if (g_SceneMan.CastStrengthRay(point, Vector(0.0F, reach), 5.0F, hit, 2)) {
			return hit.m_Y;
		}
		return -1.0F;
	}

	bool Solid(float x, float y) {
		return g_SceneMan.GetTerrMatter(static_cast<int>(x), static_cast<int>(y)) != MaterialColorKeys::g_MaterialAir;
	}
} // namespace

// The base game's background ladders: a script node at the middle of each 24 px piece holds a humanoid in front of it, and moves it up when
// it aims up and presses up, down likewise. The nodes are found among the scene's particles now and then and kept.
std::vector<Vector> AHuman::s_LadderNodes;
double AHuman::s_LadderNodesSimTimeMS = -1.0;

const Vector* AHuman::LadderNear(const Vector& point, float reachX, float reachY) {
	double now = g_TimerMan.GetSimTimeMS();
	if (s_LadderNodesSimTimeMS < 0.0 || now - s_LadderNodesSimTimeMS > 4000.0) {
		s_LadderNodesSimTimeMS = now;
		s_LadderNodes.clear();
		for (const MovableObject* particle: g_MovableMan.GetParticleList()) {
			if (particle && particle->GetPresetName() == "Background Ladder Node" && particle->GetPinStrength() > 0.0F) {
				s_LadderNodes.push_back(particle->GetPos());
			}
		}
	}
	const Vector* best = nullptr;
	float bestDistance = std::numeric_limits<float>::max();
	for (const Vector& node: s_LadderNodes) {
		Vector off = Towards(point, node);
		if (std::abs(off.m_X) <= reachX && std::abs(off.m_Y) <= reachY && off.GetMagnitude() < bestDistance) {
			best = &node;
			bestDistance = off.GetMagnitude();
		}
	}
	return best;
}

// A door of ours, or no one's, whose moving part lies near the line from us to a point, within a body and a half.
ADoor* AHuman::DoorAhead(const Vector& toPoint) const {
	ADoor* best = nullptr;
	float bestDistance = std::numeric_limits<float>::max();
	Vector along = Towards(m_Pos, toPoint);
	float length = along.GetMagnitude();
	for (Actor* actor: g_MovableMan.GetActorList()) {
		ADoor* door = dynamic_cast<ADoor*>(actor);
		if (!door || (door->GetTeam() != m_Team && door->GetTeam() != Activity::NoTeam) || !door->GetDoor() || !door->GetDoor()->IsAttached()) {
			continue;
		}
		Vector toLeaf = Towards(m_Pos, door->GetDoor()->GetPos());
		if (toLeaf.MagnitudeIsGreaterThan(m_CharHeight * 1.5F)) {
			continue;
		}
		float t = length > 1.0F ? std::clamp(toLeaf.Dot(along) / (length * length), 0.0F, 1.0F) : 0.0F;
		float distance = (toLeaf - along * t).GetMagnitude();
		if (distance < m_CharHeight * 0.5F && distance < bestDistance) {
			best = door;
			bestDistance = distance;
		}
	}
	return best;
}

bool AHuman::InDoorSweep() const {
	for (Actor* actor: g_MovableMan.GetActorList()) {
		ADoor* door = dynamic_cast<ADoor*>(actor);
		if (door && (door->GetTeam() == m_Team || door->GetTeam() == Activity::NoTeam) && door->GetDoor() && door->GetDoorState() != ADoor::CLOSED && Towards(m_Pos, door->GetPos()).MagnitudeIsLessThan(m_CharHeight * 1.2F) && door->SweepContains(m_Pos, m_CharHeight * 0.3F)) {
			return true;
		}
	}
	return false;
}

void AHuman::ResetRouteMovement() {
	m_Mover = RouteMover();
	m_Mover.lastProgressPos = m_Pos;
}

// The fuel a flight takes, in ms: the climb at about 4 m/s, the crossing lit about half the time, and a third over, never more than a tank.
float AHuman::FlightFuelNeeded(const Vector& landing, float landingFloorY) const {
	Vector to = Towards(m_Pos, landing);
	float feetY = m_Pos.m_Y + (m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : m_CharHeight * 0.2F);
	float rise = std::max(0.0F, feetY - landingFloorY + 12.0F);
	// (The climb at about 8 m/s on average, burn and coast; at 4 a 190 px shaft was reckoned beyond a tank that takes it.)
	float needed = (rise / (8.0F * c_PPM) + std::abs(to.m_X) / (5.0F * c_PPM) * 0.5F) * 1000.0F * 1.2F + 200.0F;
	return m_pJetpack ? std::min(needed, m_pJetpack->GetJetTimeTotal() * 0.98F) : needed;
}

// Whether the way to a landing is open air for a flight: up at the unit's column to over the landing's floor, then across.
bool AHuman::FlightWayClear(const Vector& landing, float landingFloorY) const {
	float h = m_CharHeight;
	float cruiseY = std::min(m_Pos.m_Y, landingFloorY - 12.0F - h * 0.5F);
	Vector up(0.0F, cruiseY - m_Pos.m_Y - h * 0.3F);
	Vector over = Towards(Vector(m_Pos.m_X, cruiseY), Vector(landing.m_X, cruiseY));
	Vector obstacle;
	Vector free;
	for (float dx: {-h * 0.15F, h * 0.15F}) {
		if (up.m_Y < -2.0F && g_SceneMan.CastObstacleRay(m_Pos + Vector(dx, -h * 0.3F), up, obstacle, free, m_MOID, IgnoresWhichTeam(), 0, 3) >= 0.0F) {
			return false;
		}
	}
	for (float dy: {-h * 0.35F, h * 0.35F}) {
		if (g_SceneMan.CastObstacleRay(Vector(m_Pos.m_X, cruiseY + dy), over, obstacle, free, m_MOID, IgnoresWhichTeam(), 0, 3) >= 0.0F) {
			return false;
		}
	}
	return true;
}

// Whether a landing can be walked to: the floor followed from under us to under it, every 12 px, never up more than a step the legs take
// or mantle (a quarter of the height) nor down more than a safe step (0.6 of it), with room to crawl all the way. Asked before any jet.
bool AHuman::CanWalkTo(const Vector& landing, float landingFloorY) const {
	float h = m_CharHeight;
	float stepUp = h * 0.24F;
	float stepDown = h * 0.6F;
	float room = h * 0.24F;
	float floorY = FloorUnder(m_Pos, h * 0.8F);
	if (floorY < 0.0F) {
		return false;
	}
	Vector to = Towards(m_Pos, landing);
	if (std::abs(to.m_X) > 360.0F) {
		return false;
	}
	int steps = static_cast<int>(std::abs(to.m_X) / 12.0F);
	float direction = to.m_X > 0.0F ? 1.0F : -1.0F;
	for (int k = 1; k <= steps; ++k) {
		float x = m_Pos.m_X + direction * static_cast<float>(k) * 12.0F;
		if (Solid(x, floorY - stepUp)) {
			return false;
		}
		Vector next;
		if (!g_SceneMan.CastStrengthRay(Vector(x, floorY - stepUp), Vector(0.0F, stepUp + stepDown), 5.0F, next, 1)) {
			return false;
		}
		floorY = next.m_Y;
		Vector hit;
		if (g_SceneMan.CastStrengthRay(Vector(x, floorY - 2.0F), Vector(0.0F, -room), 5.0F, hit, 1)) {
			return false;
		}
	}
	return std::abs(floorY - landingFloorY) <= 6.0F;
}

// The landing of the flight ahead on the route, if there is one: where the route leaves the ground (its points or the legs between them in
// the air), the first point after with floor under it; or the furthest such that can be flown to straight, on one tank, so a route that
// dives into a valley and climbs out is flown over instead. @return Whether there is one; the landing, its floor and how many route points it is.
bool AHuman::FindLanding(Vector& landing, float& landingFloorY, int& pointsToLanding) const {
	float h = m_CharHeight;
	bool airborne = false;
	int index = 0;
	Vector last = m_Pos;
	struct Candidate {
		Vector pos;
		float floorY;
		int index;
	};
	std::vector<Candidate> candidates;
	for (const Vector& point: m_MovePath) {
		++index;
		if (index > 30) {
			break;
		}
		Vector leg = Towards(last, point);
		int samples = static_cast<int>(leg.GetMagnitude() / 16.0F);
		for (int k = 1; k < samples && !airborne; ++k) {
			Vector sample = last + leg * (static_cast<float>(k) / static_cast<float>(samples));
			if (FloorUnder(sample, h * 0.8F) < 0.0F) {
				airborne = true;
			}
		}
		float floorY = FloorUnder(point, h * 0.8F);
		if (floorY < 0.0F) {
			airborne = true;
		} else if (airborne) {
			candidates.push_back({point, floorY, index});
			airborne = false;
		}
		last = point;
	}
	if (candidates.empty()) {
		return false;
	}
	for (int k = static_cast<int>(candidates.size()) - 1; k >= std::max(1, static_cast<int>(candidates.size()) - 4); --k) {
		const Candidate& candidate = candidates[k];
		if (m_pJetpack && FlightFuelNeeded(candidate.pos, candidate.floorY) <= m_pJetpack->GetJetTimeTotal() * 0.95F && FlightWayClear(candidate.pos, candidate.floorY)) {
			landing = candidate.pos;
			landingFloorY = candidate.floorY;
			pointsToLanding = candidate.index;
			return true;
		}
	}
	landing = candidates.front().pos;
	landingFloorY = candidates.front().floorY;
	pointsToLanding = candidates.front().index;
	return true;
}

bool AHuman::ShaftHere(float& middleX, float& width) const {
	// Walls both sides at any of a few heights from the chest to a body over the head: a shaft entered from the corridor under its mouth
	// has its walls above the corridor's ceiling, where a look at chest height along the open corridor found none.
	float h = m_CharHeight;
	bool found = false;
	for (float up: {h * 0.1F, h * 0.45F, h * 0.8F, h * 1.15F}) {
		Vector from(m_Pos.m_X, m_Pos.m_Y - up);
		if (Solid(from.m_X, from.m_Y)) {
			continue; // (Inside the ceiling: no shaft at this height.)
		}
		Vector leftHit;
		Vector rightHit;
		Vector free;
		bool left = g_SceneMan.CastObstacleRay(from, Vector(-h, 0.0F), leftHit, free, m_MOID, IgnoresWhichTeam(), 0, 2) >= 0.0F;
		bool right = g_SceneMan.CastObstacleRay(from, Vector(h, 0.0F), rightHit, free, m_MOID, IgnoresWhichTeam(), 0, 2) >= 0.0F;
		if (left && right) {
			float w = Towards(leftHit, rightHit).m_X;
			if (w <= h * 1.2F && (!found || w < width)) {
				width = w;
				middleX = leftHit.m_X + w * 0.5F;
				found = true;
			}
		}
	}
	return found;
}

void AHuman::RefreshRoute() {
	Vector goal = m_HasMovePathGoal ? m_MovePathGoal : GetLastAIWaypoint();
	bool hadGoal = m_HasMovePathGoal || !m_MovePath.empty();
	m_MovePath.clear();
	m_MovePathKinds.clear();
	if (hadGoal) {
		m_MovePathGoal = goal;
		m_HasMovePathGoal = true;
		m_MoveTarget = goal;
	}
	UpdateMovePath();
}

void AHuman::PopRoutePoint() {
	if (m_MovePath.empty()) {
		return;
	}
	m_PrevPathTarget = m_MovePath.front();
	m_MovePath.pop_front();
	if (!m_MovePathKinds.empty()) {
		m_MovePathKinds.pop_front();
	}
}

void AHuman::MoverTrace(const std::string& text) const {
	if (std::getenv("CCCP_AI_LOG") && NumberValueExists("AITrace")) {
		g_ConsoleMan.PrintString("AITRACE mover: " + text + " at " + std::to_string(m_Pos.GetFloorIntX()) + "," + std::to_string(m_Pos.GetFloorIntY()));
	}
}

int AHuman::MoveAlongRoute() {
	RouteMover& mover = m_Mover;
	if (!mover.begun) {
		mover.begun = true;
		mover.lastProgressPos = m_Pos;
	}
	const float h = m_CharHeight;
	const float feet = m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : h * 0.2F;
	const bool standardJet = m_pJetpack && m_pJetpack->GetJetpackType() == AEJetpack::JetpackType::Standard;
	const float floorHere = FloorUnder(m_Pos, h * 0.5F + h * 0.33F);
	const bool airborne = floorHere < 0.0F;
	Controller& ctrl = m_Controller;

	// Nothing to go to.
	if (m_Waypoints.empty() && m_MovePath.empty() && !m_HasMovePathGoal && !g_MovableMan.ValidMO(m_pMOMoveTarget)) {
		return RouteMover::Arrived;
	}
	// Knocked over, or getting up: no keys (the legs' walking while down was the flailing), and not stuck for it.
	if (m_Status != STABLE || m_GettingUp) {
		mover.progressTimer.Reset();
		return RouteMover::Moving;
	}

	// The route: asked for when there is none, now and then anyway (the world changes), when the next point has been out of sight a while on
	// the ground, and when stuck. A route check (the same answer, taken only if it is better) runs in the air.
	if (m_MovePath.empty() && !IsWaitingOnNewMovePath()) {
		if (m_ImpossiblePaths > 0 && mover.impossibleAnswers >= 3) {
			MoverTrace("no route to it; giving up");
			return RouteMover::Impossible;
		}
		UpdateMovePath();
		if (m_ImpossiblePaths > 0) {
			++mover.impossibleAnswers;
		}
		mover.repathTimer.Reset();
	}
	if (IsWaitingOnNewMovePath()) {
		// (Standing still while the search runs, unless in the air, where the last flight goes on.)
		if (!mover.flight.active) {
			return RouteMover::Moving;
		}
	}
	if (m_MovePath.empty() && !mover.flight.active) {
		return RouteMover::Moving;
	}
	mover.impossibleAnswers = 0;

	// Arrived: the route walked out and the last waypoint, on its floor, within reach.
	if (m_MovePath.empty() && !mover.flight.active) {
		return RouteMover::Moving;
	}
	if (m_MovePath.size() <= 1 && !mover.flight.active && m_Waypoints.size() <= 1 && !g_MovableMan.ValidMO(m_pMOMoveTarget)) {
		Vector goal = GetLastAIWaypoint();
		Vector goalGround = Solid(goal.m_X, goal.m_Y) ? goal : g_SceneMan.MovePointToGround(goal, static_cast<int>(h * 0.2F), 4);
		Vector toGoal = Towards(m_Pos, goalGround);
		if (toGoal.GetLargest() < h * 0.4F && !airborne && m_Vel.MagnitudeIsLessThan(2.0F)) {
			MoverTrace("arrived");
			return RouteMover::Arrived;
		}
	}

	// Points passed are dropped: within reach, or behind with the next in plain sight (the points are 24 px apart, and waiting to stand on
	// each pulled a running unit back to every one). Not while a flight is on: the flight pops its own on landing.
	if (!mover.flight.active && !m_MovePath.empty()) {
		float tolerance = m_MoveProximityLimit * (airborne ? 2.0F : 1.0F);
		for (int guard = 0; guard < 6 && !m_MovePath.empty(); ++guard) {
			Vector toPoint = Towards(m_Pos, m_MovePath.front());
			bool last = m_MovePath.size() == 1;
			if (toPoint.MagnitudeIsLessThan(tolerance) && !last) {
				PopRoutePoint();
				continue;
			}
			if (!last) {
				Vector toNext = Towards(m_Pos, *std::next(m_MovePath.begin()));
				Vector obstacle;
				Vector free;
				bool passed = toNext.MagnitudeIsLessThan(toPoint.GetMagnitude()) || (toPoint.m_X * toNext.m_X < 0.0F && std::abs(toPoint.m_X) > 6.0F && std::abs(toPoint.m_X) < h * 0.5F && std::abs(toPoint.m_Y) < h * 0.5F);
				// (A point above us under a low ceiling is not passed from below it: the climb reaches it in its own terms.)
				bool lowCeiling = m_ProneState == PRONE && m_MovePath.front().m_Y < m_Pos.m_Y - 6.0F;
				if (passed && !lowCeiling && g_SceneMan.CastObstacleRay(m_Pos, toNext, obstacle, free, m_MOID, IgnoresWhichTeam(), 0, 9) < 0.0F) {
					PopRoutePoint();
					continue;
				}
			}
			break;
		}
	}
	if (m_MovePath.empty() && !mover.flight.active) {
		return RouteMover::Moving;
	}

	// Progress: how far from where it last got nearer the route's point; none for a while is stuck.
	if (!m_MovePath.empty()) {
		float gap = Towards(m_Pos, m_MovePath.front()).GetMagnitude();
		if (mover.bestGap < 0.0F || gap < mover.bestGap - 4.0F || mover.flight.active) {
			mover.bestGap = gap;
			mover.progressTimer.Reset();
		}
	}
	bool stuck = mover.progressTimer.IsPastSimMS(2500);
	if (stuck && mover.progressTimer.IsPastSimMS(6000)) {
		// Long stuck: the route asked for afresh from here, and the place avoided.
		MoverTrace("stuck; new route");
		AvoidPathPoint(m_MovePath.empty() ? m_Pos : m_MovePath.front(), 20000.0F);
		RefreshRoute();
		mover.flight = RouteMover::Flight();
		mover.bestGap = -1.0F;
		mover.progressTimer.Reset();
		return RouteMover::Moving;
	}
	// Now and then, or the next point out of sight on the ground for a second: a fresh route.
	if (!mover.flight.active && !m_MovePath.empty()) {
		Vector toPoint = Towards(m_Pos, m_MovePath.front());
		Vector obstacle;
		Vector free;
		bool inSight = g_SceneMan.CastObstacleRay(m_Pos, toPoint, obstacle, free, m_MOID, IgnoresWhichTeam(), 0, 9) < 0.0F;
		if (inSight || airborne || DoorAhead(m_MovePath.front())) {
			mover.noSightTimer.Reset();
		}
		if ((mover.noSightTimer.IsPastSimMS(1000) || mover.repathTimer.IsPastSimMS(7500)) && !IsWaitingOnNewMovePath()) {
			MoverTrace(mover.noSightTimer.IsPastSimMS(1000) ? "next point out of sight; new route" : "route refreshed");
			RefreshRoute();
			mover.repathTimer.Reset();
			mover.noSightTimer.Reset();
			return RouteMover::Moving;
		}
	} else if (airborne && mover.routeCheckTimer.IsPastSimMS(500)) {
		mover.routeCheckTimer.Reset();
		RequestRouteCheck();
	}

	// ---- The flight: planned take-off to touchdown, flown by the pilot. ----
	if (mover.flight.active) {
		RouteMover::Flight& flight = mover.flight;
		Vector to = Towards(m_Pos, flight.landing);
		float feetY = m_Pos.m_Y + feet;
		bool onLanding = std::abs(to.m_X) < h * 0.25F && std::abs(feetY - flight.floorY) < h * 0.3F;
		bool ended = false;
		if (!airborne && flight.timer.IsPastSimMS(400) && (onLanding || flight.timer.IsPastSimMS(1200))) {
			ended = true;
		} else if (flight.timer.IsPastSimMS(15000)) {
			ended = true;
		} else if (airborne && standardJet && m_pJetpack->GetJetTimeLeft() < 60.0F && !onLanding) {
			MoverTrace("out of fuel short of the landing");
			ended = true;
		}
		if (ended) {
			if (onLanding) {
				// The route's points up to the landing are done with.
				for (int k = 0; k < flight.pointsToLanding && !m_MovePath.empty(); ++k) {
					PopRoutePoint();
				}
				MoverTrace("landed");
			}
			flight = RouteMover::Flight();
			mover.bestGap = -1.0F;
			mover.progressTimer.Reset();
			mover.lastJetTime = -1.0;
		} else {
			// Up a shaft: to the point over its mouth first, straight up its middle, and only then for the landing.
			if (flight.via && m_Pos.m_Y <= flight.viaPoint.m_Y + 8.0F) {
				flight.via = false;
				MoverTrace("out of the shaft; for the landing");
			}
			Vector command = flight.via ? PilotFlight(flight.viaPoint, -1.0F) : PilotFlight(flight.landing, flight.floorY);
			ctrl.SetState(BODY_JUMP, command.m_Y > 0.5F);
			ctrl.SetAnalogMove(Vector(command.m_X, -1.0F));
			// Still on the ground at the start of the flight: the legs carry it towards the landing (off the edge, for one that is level or
			// below), and the pilot has it once it is in the air. The stick alone moves nothing on the ground, and a flight whose pilot
			// wanted no jet yet (a drop) stood at the edge and began again every second.
			// (And whenever the flight is all but stopped: hanging on an edge, the floor test says air while the body rests on the lip, and
			// with no key pressed it hung there for the rest of the minute.)
			if ((!airborne || m_Vel.MagnitudeIsLessThan(0.6F)) && !flight.via) {
				float dx = Towards(m_Pos, flight.landing).m_X;
				if (std::abs(dx) > 4.0F) {
					ctrl.SetState(dx < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
				}
			}
			return RouteMover::Moving;
		}
	}
	if (m_MovePath.empty()) {
		return RouteMover::Moving;
	}

	const Vector point = m_MovePath.front();
	const PathStepKind kind = m_MovePathKinds.empty() ? PathStepKind::Walk : m_MovePathKinds.front();
	const Vector toPoint = Towards(m_Pos, point);
	const float pointFloor = FloorUnder(point, h * 0.8F);

	// ---- In the air with no flight: the pilot flies for the point, braking for its floor. ----
	if (airborne) {
		if (standardJet) {
			Vector landing;
			float landingFloorY = 0.0F;
			int pointsToLanding = 0;
			if (m_pJetpack->GetJetTimeLeft() > 200.0F && FindLanding(landing, landingFloorY, pointsToLanding) && !(std::abs(Towards(m_Pos, landing).m_X) < h * 0.5F && landingFloorY > m_Pos.m_Y && !ctrl.IsState(BODY_JUMP))) {
				mover.flight.active = true;
				mover.flight.landing = landing;
				mover.flight.floorY = landingFloorY;
				mover.flight.pointsToLanding = pointsToLanding;
				mover.flight.timer.Reset();
				MoverTrace("flight to " + std::to_string(static_cast<int>(landing.m_X)) + "," + std::to_string(static_cast<int>(landingFloorY)));
			}
			Vector command = PilotFlight(point, pointFloor);
			// (A drop onto the point's floor is left to gravity until the brake is wanted: the pilot's safe-fall rule asks for the jet only then.)
			ctrl.SetState(BODY_JUMP, command.m_Y > 0.5F);
			ctrl.SetAnalogMove(Vector(command.m_X, -1.0F));
		} else {
			ctrl.SetState(toPoint.m_X < -3.0F ? MOVE_LEFT : MOVE_RIGHT, std::abs(toPoint.m_X) > 3.0F);
		}
		return RouteMover::Moving;
	}

	// ---- On the ground. ----
	const Vector* ladder = LadderNear(m_Pos, h * 0.2F, h * 0.3F);
	const float above = -toPoint.m_Y;

	// A door of ours in the way: closed, waited for short of it, on its sensor; given up on after 2 s (walked into) for 5 s.
	if (ADoor* door = DoorAhead(point)) {
		ADoor::DoorState state = door->GetDoorState();
		if ((state == ADoor::CLOSED || state == ADoor::CLOSING) && door->GetUniqueID() != mover.doorIgnoreID) {
			if (door->GetUniqueID() != mover.doorWaitID) {
				mover.doorWaitID = door->GetUniqueID();
				mover.doorWaitTimer.Reset();
			}
			if (mover.doorWaitTimer.IsPastSimMS(2000)) {
				MoverTrace("door didn't open; walking into it");
				mover.doorIgnoreID = door->GetUniqueID();
				mover.doorIgnoreTimer.Reset();
			} else {
				// To the nearest sensor's line, 6 px short of it, and hold there.
				Vector sense = door->GetPos();
				float nearest = std::numeric_limits<float>::max();
				for (const ADSensor& sensor: door->GetSensors()) {
					Vector start = door->GetPos() + sensor.GetStartOffset().GetXFlipped(door->IsHFlipped()) * door->GetRotMatrix();
					Vector end = start + sensor.GetSensorRay().GetXFlipped(door->IsHFlipped()) * door->GetRotMatrix();
					Vector mid = (start + end) * 0.5F;
					float distance = Towards(m_Pos, mid).GetMagnitude();
					if (distance < nearest) {
						nearest = distance;
						sense = mid;
					}
				}
				float dx = Towards(m_Pos, sense).m_X;
				if (std::abs(dx) > 6.0F) {
					ctrl.SetState(dx < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
				}
				mover.progressTimer.Reset();
				return RouteMover::Moving;
			}
		} else if (state == ADoor::OPEN || state == ADoor::OPENING) {
			mover.doorWaitID = 0;
		}
	}
	if (mover.doorIgnoreID != 0 && mover.doorIgnoreTimer.IsPastSimMS(5000)) {
		mover.doorIgnoreID = 0;
	}

	// A flight wanted: the point well above, or a gap in the floor on the way, as the route's legs say.
	Vector landing;
	float landingFloorY = 0.0F;
	int pointsToLanding = 0;
	bool flightAhead = standardJet && FindLanding(landing, landingFloorY, pointsToLanding);
	bool wantsClimb = above > h * 0.3F && (kind == PathStepKind::Jump || !CanWalkTo(point, pointFloor >= 0.0F ? pointFloor : point.m_Y + h * 0.4F));
	if (flightAhead && !CanWalkTo(landing, landingFloorY)) {
		Vector toLanding = Towards(m_Pos, landing);
		// A ladder here and the way up along it: climbed, no jet (the ladder's script moves the unit up while it aims up and presses up).
		if (ladder && above > h * 0.3F) {
			ctrl.SetState(MOVE_UP, true);
			SetAimAngle(c_HalfPI);
			mover.progressTimer.Reset();
			return RouteMover::Moving;
		}
		// In a shaft (walls both sides) with the landing above: lined up under its middle first, walking there with no jet, and flown up
		// the middle to a point over the mouth before turning for the landing. Lit beside the middle, a unit scraped up one wall and burned
		// the tank pinned under the lip.
		float shaftMiddle = 0.0F;
		float shaftWidth = 0.0F;
		bool inShaft = toLanding.m_Y < -h * 0.3F && ShaftHere(shaftMiddle, shaftWidth);
		if (inShaft && std::abs(shaftMiddle - m_Pos.m_X) > std::max(3.0F, h * 0.05F)) {
			ctrl.SetState(shaftMiddle < m_Pos.m_X ? MOVE_LEFT : MOVE_RIGHT, true);
			mover.progressTimer.Reset();
			return RouteMover::Moving;
		}
		// A hop across a gap in the floor at this level (a corridor crossing a shaft's mouth): taken off from the edge, in one arc, as a
		// person hops it. The way-clear test wants cruising room half a body over the floor, which a 48 px corridor hasn't, so no flight
		// was ever begun; the unit walked off the edge, fell, and jetted back up from under the far lip (the pilot keeps the hop low under
		// the ceiling by its own look at the terrain).
		float direction = toLanding.m_X > 0.0F ? 1.0F : -1.0F;
		bool levelHop = std::abs(toLanding.m_Y) <= h * 0.3F && std::abs(toLanding.m_X) <= h * 4.0F;
		bool edgeAhead = false;
		if (levelHop) {
			for (float ahead = 6.0F; ahead <= h * 0.45F && !edgeAhead; ahead += 6.0F) {
				float floorAhead = FloorUnder(m_Pos + Vector(direction * ahead, 0.0F), h * 0.9F);
				edgeAhead = floorAhead < 0.0F || floorAhead > floorHere + h * 0.4F;
			}
		}
		// On the ground, with the fuel the flight takes: off. Short of it, waits (walking the while if the point is level).
		// (Never more than nine tenths of the tank, which a standing unit's own hops kept it from; and the wait ends after six seconds
		// whatever the tank says, on a timer of its own, since standing still here is not being stuck.)
		float needed = std::min(FlightFuelNeeded(landing, landingFloorY), m_pJetpack->GetJetTimeTotal() * 0.9F);
		if (!mover.fuelWaiting) {
			mover.fuelWaiting = true;
			mover.fuelWaitTimer.Reset();
		}
		if (m_pJetpack->GetJetTimeLeft() >= needed || mover.fuelWaitTimer.IsPastSimMS(6000)) {
			mover.fuelWaiting = false;
			// Off only where the flight can begin: the way up and across open from here, a shaft to go up, or a level hop from the edge.
			// Otherwise the walk goes on along the route, which leads to where it can (under the shaft's mouth, to the edge). Taken off
			// wherever the landing was well above, a unit at a corridor's end jetted into its ceiling, burned the tank, refilled, and did
			// it again for the whole minute, 60 px short of the shaft it was to go up.
			bool wayUpOpen = FlightWayClear(landing, landingFloorY);
			if ((levelHop && edgeAhead) || inShaft || wayUpOpen) {
				mover.flight.active = true;
				mover.flight.landing = landing;
				mover.flight.floorY = landingFloorY;
				mover.flight.pointsToLanding = pointsToLanding;
				mover.flight.timer.Reset();
				mover.flight.via = inShaft;
				if (inShaft) {
					// Over the mouth: at the shaft's middle, where the body's centre is standing on the landing's floor and a little over (the
					// feet just clear of the lip). A body's height over it was in the ceiling of the corridor the landing is in, and the
					// unit hovered under it until the tank ran dry.
					mover.flight.viaPoint = Vector(shaftMiddle, landingFloorY - feet - 8.0F);
					MoverTrace("up the shaft via " + std::to_string(static_cast<int>(shaftMiddle)) + "," + std::to_string(static_cast<int>(mover.flight.viaPoint.m_Y)));
				}
				MoverTrace(std::string(levelHop && edgeAhead ? "hop from the edge for " : "take-off for ") + std::to_string(static_cast<int>(landing.m_X)) + "," + std::to_string(static_cast<int>(landingFloorY)));
				Vector command = PilotFlight(landing, landingFloorY);
				ctrl.SetState(BODY_JUMPSTART, true);
				ctrl.SetState(BODY_JUMP, true);
				ctrl.SetAnalogMove(Vector(command.m_X, -1.0F));
				return RouteMover::Moving;
			}
		} else {
			mover.progressTimer.Reset();
			if (mover.traceTimer.IsPastSimMS(1000)) {
				mover.traceTimer.Reset();
				MoverTrace("waiting for fuel: " + std::to_string(static_cast<int>(m_pJetpack->GetJetTimeLeft())) + " of " + std::to_string(static_cast<int>(needed)));
			}
			// (Standing still while the tank fills: no hop from the stuck handling meanwhile, which spent what the wait was for.)
			mover.hopTimer.Reset();
			if (std::abs(toLanding.m_Y) > h * 0.3F) {
				return RouteMover::Moving;
			}
		}
	}
	// Down a ladder: the point well below, nearly straight down, and a ladder here.
	if (ladder && toPoint.m_Y > h * 0.3F && std::abs(toPoint.m_X) < h * 0.4F) {
		ctrl.SetState(MOVE_DOWN, true);
		SetAimAngle(-c_HalfPI);
		mover.progressTimer.Reset();
		return RouteMover::Moving;
	}

	// ---- The walk. ----
	bool crawl = kind == PathStepKind::Crawl;
	// Head room here when standing: the standing height up from the floor; too little, or a crawl step near, and it goes prone.
	float standing = std::max(16.0F, h * 0.44F);
	float floorY = floorHere >= 0.0F ? floorHere : m_Pos.m_Y + feet;
	float topHeadY = std::min(m_Pos.m_Y - 4.0F, floorY - standing);
	Vector heading = toPoint.GetNormalized() * (h * 0.5F);
	Vector hit;
	bool noRoomHere = g_SceneMan.CastStrengthRay(m_Pos, Vector(0.0F, topHeadY - m_Pos.m_Y), 5.0F, hit, 4, MaterialColorKeys::g_MaterialDoor);
	bool noRoomAhead = g_SceneMan.CastStrengthRay(Vector(m_Pos.m_X, topHeadY), heading, 5.0F, hit, 4, MaterialColorKeys::g_MaterialDoor);
	bool crawlNear = crawl && toPoint.MagnitudeIsLessThan(h * 0.65F);
	if (noRoomHere || noRoomAhead || crawlNear) {
		mover.proneHoldTimer.Reset();
	}
	bool prone = !mover.proneHoldTimer.IsPastSimMS(400) && (noRoomHere || noRoomAhead || crawlNear || m_ProneState == PRONE);
	if (prone) {
		ctrl.SetState(BODY_PRONE, true);
	}
	if (std::abs(toPoint.m_X) > 3.0F) {
		ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
	}
	// Running on a long, level, open stretch: the point two bodies or more away and no higher, head room to stand, no door near, and
	// floor the whole way (a run off an edge or into a door is no way to arrive). The script used to roll a die for the run key.
	if (!prone && std::abs(toPoint.m_X) > h * 2.0F && std::abs(toPoint.m_Y) < h * 0.25F && !noRoomAhead && !DoorAhead(point)) {
		bool floorAllTheWay = true;
		float stepDirection = toPoint.m_X > 0.0F ? 1.0F : -1.0F;
		for (float ahead = 12.0F; ahead <= h * 1.5F && floorAllTheWay; ahead += 12.0F) {
			float floorAhead = FloorUnder(m_Pos + Vector(stepDirection * ahead, 0.0F), h * 0.9F);
			floorAllTheWay = floorAhead >= 0.0F && std::abs(floorAhead - floorY) < h * 0.3F;
		}
		if (floorAllTheWay) {
			ctrl.SetState(MOVE_FAST, true);
		}
	}
	SetAimAngle(0.0F);
	// A step up the legs don't take, or a wall: after a moment with no progress, a hop (the mantle takes most steps).
	if (stuck && standardJet && !prone && m_pJetpack->GetJetTimeLeft() > 300.0F) {
		if (mover.hopTimer.IsPastSimMS(1200)) {
			mover.hopTimer.Reset();
			MoverTrace("stuck; hop");
		}
		if (!mover.hopTimer.IsPastSimMS(350)) {
			ctrl.SetState(BODY_JUMP, true);
			SetAimAngle(above > h * 0.2F ? c_HalfPI * 0.7F : 0.2F);
		}
	}
	// Walking off an edge onto the point below: the walk is the drop; the brake comes in the air.
	return RouteMover::Moving;
}
