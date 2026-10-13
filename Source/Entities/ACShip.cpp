#include "ACShip.h"
#include "AirRoute.h"

#include "ADoor.h"
#include "AEmitter.h"
#include "AHuman.h"
#include "AVehicle.h"
#include "ActivityMan.h"
#include "Controller.h"
#include "FrameMan.h"
#include "HeldDevice.h"
#include "MovableMan.h"
#include "PieSlice.h"
#include "PresetMan.h"
#include "SceneMan.h"
#include "Scene.h"
#include "SettingsMan.h"
#include "ThreatMemory.h"
#include "TimerMan.h"
#include "RTETools.h"
#include "Turret.h"

#include "GUI.h"
#include "AllegroBitmap.h"

#include "tracy/Tracy.hpp"

#include <algorithm>
#include <array>
#include <queue>
#include <cmath>

using namespace RTE;

ConcreteClassInfo(ACShip, ACDropShip, 10);

namespace {
	/// Whether a friend of a team's is the first thing a shot along a line would hit (its own hull and crew aside, and stopping at the ground):
	/// a gunner holds fire rather than put a round into the vehicle or squad beside it on the way to the enemy.
	bool FriendInLineOfFire(const RTE::MovableObject* self, int team, const RTE::Vector& from, const RTE::Vector& ray) {
		RTE::MOID hit = RTE::g_SceneMan.CastMORay(from, ray, self->GetRootID(), RTE::Activity::NoTeam, 0, false, 2);
		if (hit == RTE::g_NoMOID) {
			return false;
		}
		const RTE::MovableObject* part = RTE::g_MovableMan.GetMOFromID(hit);
		const RTE::Actor* actor = part ? dynamic_cast<const RTE::Actor*>(RTE::g_MovableMan.GetMOFromID(part->GetRootID())) : nullptr;
		return actor && actor->GetTeam() == team && !actor->IsDead();
	}

	/// An angle brought into -pi to pi.
	float Wrapped(float angle) {
		angle = std::fmod(angle, c_TwoPI);
		if (angle > c_PI) {
			angle -= c_TwoPI;
		} else if (angle < -c_PI) {
			angle += c_TwoPI;
		}
		return angle;
	}

	/// The controller states a drop ship flies by, let go of while nobody flies it.
	constexpr std::array<ControlState, 8> c_FlyingStates = {MOVE_UP, MOVE_DOWN, MOVE_LEFT, MOVE_RIGHT, AIM_UP, AIM_DOWN, PRESS_UP, PRESS_FACEBUTTON};
} // namespace

ACShip::ACShip() {
	m_Crew.SetHost(this);
	Clear();
}

ACShip::~ACShip() {
	Destroy(true);
}

void ACShip::Clear() {
	m_Crew.Clear();
	m_NeedsPilot = true;
	m_CanBeTaken = true;
	m_SeatsCargo = false;
	m_CrewThrowSpeed = 18.0F;
	m_GunRange = 700.0F;
	m_GunTurnSpeed = 2.5F;
	m_GunAimRange = 1.3F;
	m_Guns.clear();
	m_Parked = false;
	m_EnginesOff = false;
	m_TouchdownTimer.Reset();
	m_OrderedBoardTimer.Reset();
	m_EngageID = 0;
	m_RamID = 0;
	m_RamAtPlace = false;
	m_RamPoint.Reset();
	m_RamAuto = false;
	m_RamPhase = RamPhase::RunUp;
	m_RamSide = 1.0F;
	m_HadGuns = false;
	m_RamSpeedScale = 1.5F;
	m_RamDamage = 0.6F;
	m_RamSelfDamage = 0.25F;
	m_RouteSpot.Reset();
	m_RouteBest = FLT_MAX;
	m_RouteReplans = 0;
	m_UnstickStep = -1;
	m_UnstickTries = 0;
	m_UnstickSide = 1.0F;
	m_CombatTargetID = 0;
	m_CombatSpot.Reset();
	m_HasCombatSpot = false;
	m_CombatSearching = false;
	m_CombatHealth = -1.0F;
	m_FlightSpeed = 12.0F;
	m_ClimbSpeed = 6.0F;
	m_HoldPoint.Reset();
	m_HasHoldPoint = false;
	m_FlightTarget.Reset();
	m_FlightFloor = FLT_MAX;
	m_SteerTrim.Reset();
	m_FlownTimer.Reset();
	m_HeldByPause = false;
	m_PausedAt.Reset();
	m_FlightToRestore = FlightToRestore::None;
	m_GoalToRestore.Reset();
	m_RideStage = RideStage::None;
	m_RideRecruits.clear();
	m_RideGoal.Reset();
	m_RideTeam = Activity::NoTeam;
	m_RideStageTimer.Reset();
	m_RideWaitTimer.Reset();
	m_RideWaitStarted = false;
	m_RideScanTimer.Reset();
	m_RideSent = false;
	m_RideSends = 0;
	m_RideHealth = 0.0F;
	m_RideUnloadAt.Reset();
}

int ACShip::Create(const ACShip& reference) {
	ACDropShip::Create(reference);

	m_Crew.Create(reference.m_Crew);
	m_NeedsPilot = reference.m_NeedsPilot;
	m_CanBeTaken = reference.m_CanBeTaken;
	m_SeatsCargo = reference.m_SeatsCargo;
	m_CrewThrowSpeed = reference.m_CrewThrowSpeed;
	m_GunRange = reference.m_GunRange;
	m_GunTurnSpeed = reference.m_GunTurnSpeed;
	m_GunAimRange = reference.m_GunAimRange;
	m_Parked = reference.m_Parked;
	m_EnginesOff = reference.m_EnginesOff;
	m_FlightSpeed = reference.m_FlightSpeed;
	m_ClimbSpeed = reference.m_ClimbSpeed;
	// (A ship read from a saved game is copied into the scene: what it was doing in the air goes with it.)
	m_HoldPoint = reference.m_HoldPoint;
	m_HasHoldPoint = reference.m_HasHoldPoint;
	m_Route = reference.m_Route;
	m_FlightToRestore = reference.m_FlightToRestore;
	m_GoalToRestore = reference.m_GoalToRestore;
	m_RamSpeedScale = reference.m_RamSpeedScale;
	m_RamDamage = reference.m_RamDamage;
	m_RamSelfDamage = reference.m_RamSelfDamage;

	return 0;
}

void ACShip::Destroy(bool notInherited) {
	m_Crew.DeleteCrew();
	if (!notInherited) {
		ACDropShip::Destroy();
	}
	Clear();
}

int ACShip::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return ACDropShip::ReadProperty(propName, reader));

	MatchProperty("PilotSeatOffset", {
		Vector offset;
		reader >> offset;
		m_Crew.SetMainSeatOffset(offset);
	});
	MatchForwards("AddSeat") MatchProperty("AddGunnerSeat", {
		Vector offset;
		reader >> offset;
		m_Crew.AddSeat(offset, propName == "AddGunnerSeat");
	});
	MatchProperty("ExitOffset", {
		Vector offset;
		reader >> offset;
		m_Crew.SetExitOffset(offset);
	});
	MatchProperty("BoardingReach", {
		float reach;
		reader >> reach;
		m_Crew.SetBoardingReach(reach);
	});
	MatchProperty("DrawsCrew", {
		bool drawsCrew;
		reader >> drawsCrew;
		m_Crew.SetDrawsCrew(drawsCrew);
	});
	MatchProperty("PassengersCanFire", {
		bool canFire;
		reader >> canFire;
		m_Crew.SetPassengersCanFire(canFire);
	});
	MatchProperty("NeedsPilot", { reader >> m_NeedsPilot; });
	MatchProperty("CanBeTaken", { reader >> m_CanBeTaken; });
	MatchProperty("SeatsCargo", { reader >> m_SeatsCargo; });
	MatchProperty("CrewThrowSpeed", { reader >> m_CrewThrowSpeed; });
	MatchProperty("GunRange", { reader >> m_GunRange; });
	MatchProperty("GunTurnSpeed", { reader >> m_GunTurnSpeed; });
	MatchProperty("GunAimRange", { reader >> m_GunAimRange; });
	MatchProperty("FlightSpeed", { reader >> m_FlightSpeed; });
	MatchProperty("ClimbSpeed", { reader >> m_ClimbSpeed; });
	// A saved game's (Scene::SaveSceneObject): its crew, and what it was doing in the air.
	MatchForwards("SpecialBehaviour_CrewSeat") MatchProperty("SpecialBehaviour_RiderSeat", {
		int seat;
		reader >> seat;
		m_Crew.SetSeatToLoad(seat, propName == "SpecialBehaviour_RiderSeat");
	});
	MatchProperty("SpecialBehaviour_Crew", { m_Crew.LoadSeated(dynamic_cast<Actor*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("SpecialBehaviour_Parked", { reader >> m_Parked; });
	MatchProperty("SpecialBehaviour_EnginesOff", { reader >> m_EnginesOff; });
	MatchProperty("SpecialBehaviour_HoldPoint", {
		reader >> m_HoldPoint;
		m_HasHoldPoint = true;
		if (m_FlightToRestore == FlightToRestore::None) {
			m_FlightToRestore = FlightToRestore::Hold;
		}
	});
	MatchProperty("SpecialBehaviour_FlightGoal", {
		reader >> m_GoalToRestore;
		m_FlightToRestore = FlightToRestore::Goal;
	});
	MatchProperty("SpecialBehaviour_AddRoutePoint", {
		Vector point;
		reader >> point;
		m_Route.push_back(point);
	});
	MatchProperty("RamSpeedScale", { reader >> m_RamSpeedScale; });
	MatchProperty("RamDamage", { reader >> m_RamDamage; });
	MatchProperty("RamSelfDamage", { reader >> m_RamSelfDamage; });

	EndPropertyList;
}

int ACShip::Save(Writer& writer) const {
	ACDropShip::Save(writer);

	writer.NewPropertyWithValue("PilotSeatOffset", m_Crew.GetMainSeatOffset());
	for (int seat = 1; seat < m_Crew.GetSeatCount(); ++seat) {
		writer.NewPropertyWithValue(m_Crew.IsGunnerSeat(seat) ? "AddGunnerSeat" : "AddSeat", m_Crew.GetSeatOffset(seat));
	}
	writer.NewPropertyWithValue("ExitOffset", m_Crew.GetExitOffset());
	writer.NewPropertyWithValue("BoardingReach", m_Crew.GetBoardingReach());
	writer.NewPropertyWithValue("DrawsCrew", m_Crew.DrawsCrew());
	writer.NewPropertyWithValue("PassengersCanFire", m_Crew.PassengersCanFire());
	writer.NewPropertyWithValue("NeedsPilot", m_NeedsPilot);
	writer.NewPropertyWithValue("CanBeTaken", m_CanBeTaken);
	writer.NewPropertyWithValue("SeatsCargo", m_SeatsCargo);
	writer.NewPropertyWithValue("CrewThrowSpeed", m_CrewThrowSpeed);
	writer.NewPropertyWithValue("GunRange", m_GunRange);
	writer.NewPropertyWithValue("GunTurnSpeed", m_GunTurnSpeed);
	writer.NewPropertyWithValue("GunAimRange", m_GunAimRange);
	writer.NewPropertyWithValue("FlightSpeed", m_FlightSpeed);
	writer.NewPropertyWithValue("ClimbSpeed", m_ClimbSpeed);
	writer.NewPropertyWithValue("RamSpeedScale", m_RamSpeedScale);
	writer.NewPropertyWithValue("RamDamage", m_RamDamage);
	writer.NewPropertyWithValue("RamSelfDamage", m_RamSelfDamage);

	return 0;
}

bool ACShip::CanTakeIn(const Actor* unit) const {
	return unit && m_Crew.CanCarry(unit, m_CanBeTaken && m_Crew.IsEmpty());
}

bool ACShip::TakeSeat(Actor* unit, int seat) {
	bool takeOver = unit && unit->GetTeam() != m_Team && m_CanBeTaken && m_Crew.IsEmpty();
	if (!m_Crew.TakeSeat(unit, seat, takeOver)) {
		return false;
	}
	if (takeOver) {
		// Taken by another side: whatever it was sent to do, it isn't doing for them.
		ClearAIWaypoints();
		SetAIMode(AIMODE_SENTRY);
	}
	return true;
}

bool ACShip::FlyTo(const Vector& spot) {
	StopRam();
	m_Route.clear();
	m_EnginesOff = false;
	return CanFly() && AutopilotGoTo(spot);
}

PathAgent ACShip::GetPathAgent() const {
	PathAgent agent = Actor::GetPathAgent();
	agent.JumpHeight = FLT_MAX;
	agent.MaxSafeFall = FLT_MAX;
	agent.DigStrength = 1.0F;
	agent.BreachStrength = 0.0F;
	agent.JetTimeMS = 0.0F;
	agent.ClimbsLadders = false;
	agent.WalksStairs = false;
	agent.Scrambles = false;
	agent.LeapHeight = 0.0F;
	agent.MantleHeight = 0.0F;
	agent.StraightFlight = false;
	agent.SkyRoute = false;
	agent.Lifts.reset();
	agent.Floats = false;
	agent.BreathSeconds = FLT_MAX;
	agent.CrossesLava = true;
	// (The grid measures head room up to 96 px: a ship wants all of that it can have over the ground its route follows.)
	agent.StandHeight = std::clamp(GetRadius() * 1.2F, 24.0F, 90.0F);
	agent.CrawlHeight = agent.StandHeight;
	agent.CrouchHeight = 0.0F;
	agent.HalfWidth = 16.0F;
	return agent;
}

float ACShip::HullUnderside() const {
	float underside = static_cast<float>(GetSpriteHeight()) + m_SpriteOffset.m_Y;
	// (A gun or engine hung under the hull counts; the crew in their seats don't.)
	for (const Attachable* attachable: m_Attachables) {
		if (attachable && !dynamic_cast<const Actor*>(attachable)) {
			underside = std::max(underside, attachable->GetParentOffset().m_Y + attachable->GetIndividualRadius() * 0.75F);
		}
	}
	return std::clamp(underside, 4.0F, GetRadius());
}

float ACShip::HullHalfWidth() const {
	float halfWidth = std::max(-m_SpriteOffset.m_X, static_cast<float>(GetSpriteWidth()) + m_SpriteOffset.m_X);
	// (Its engines and guns out at its ends count; the crew in their seats don't.)
	for (const Attachable* attachable: m_Attachables) {
		if (attachable && !dynamic_cast<const Actor*>(attachable)) {
			halfWidth = std::max(halfWidth, std::abs(attachable->GetParentOffset().m_X) + attachable->GetIndividualRadius() * 0.75F);
		}
	}
	return std::clamp(halfWidth, 8.0F, GetRadius());
}

float ACShip::HullTop() const {
	float top = -m_SpriteOffset.m_Y;
	for (const Attachable* attachable: m_Attachables) {
		if (attachable && !dynamic_cast<const Actor*>(attachable)) {
			top = std::max(top, -attachable->GetParentOffset().m_Y + attachable->GetIndividualRadius() * 0.75F);
		}
	}
	return std::clamp(top, 4.0F, GetRadius());
}

AirBody ACShip::GetAirBody() const {
	// (Its underside's figure takes in half its ground gap, as the route and the lines it flies keep: see AirRoute::LineClear.)
	AirBody body;
	body.HalfWidth = HullHalfWidth();
	body.Top = HullTop();
	body.Under = HullUnderside() + std::min(4.0F, g_SettingsMan.ShipGroundClearance() * 0.5F);
	body.SkyTop = SkyTop();
	body.OrbitGrace = GetOrbitGrace();
	body.Team = m_Team;
	body.Self = static_cast<const Actor*>(this);
	return body;
}

bool ACShip::HullLineClear(const Vector& from, const Vector& to) const {
	return AirRoute::LineClear(GetAirBody(), from, to);
}

float ACShip::GetOrbitGrace() const {
	return g_SettingsMan.ShipGraceZone();
}

float ACShip::LowClearance() const {
	return HullUnderside() + g_SettingsMan.ShipGroundClearance();
}

Vector ACShip::LowPoint(const Vector& spot, float extraAltitude) {
	// Where it's sent, as it was clicked: only raised off the ground enough for its underside to clear the highest of it under its hull, not
	// up to a drop ship's hover height. (From just above the place, so a click on the ground's surface finds that ground.)
	Vector to = spot;
	g_SceneMan.WrapPosition(to);
	const int keep = static_cast<int>(LowClearance() + std::max(extraAltitude, 0.0F));
	const float halfWidth = HullHalfWidth();
	float lowest = FLT_MAX;
	for (float side: {-halfWidth, 0.0F, halfWidth}) {
		Vector under(to.m_X + side, to.m_Y - 10.0F);
		g_SceneMan.WrapPosition(under);
		lowest = std::min(lowest, g_SceneMan.MovePointToGround(under, keep, 4).m_Y);
	}
	to.m_Y = std::max(std::min(to.m_Y, lowest), SkyTop());
	return to;
}

bool ACShip::FlyRouteTo(const Vector& spot) {
	StopRam();
	m_EnginesOff = false;
	if (!CanFly() || !AutopilotStarted()) {
		m_Route.clear();
		return false;
	}
	const SettingsMan::DirectOrderTuning& tuning = g_SettingsMan.DirectOrders();
	const float direct = g_SettingsMan.DirectOrders().Follows.Vehicles ? m_StandingOrder.Direct : 0.0F; // (Not following risk: a normal order's.)
	const float care = std::clamp(-direct, 0.0F, 1.0F);
	// A cautious order flies higher, up to a hull's height more over the ground, further from small arms below.
	const float extra = HullHeight() * care;
	const Vector goal = LowPoint(spot, extra);
	// (Sent again where it is already going, as the standing orders do: the route it has goes on.)
	if (!m_Route.empty() && m_AIMode == AIMODE_GOTO && g_SceneMan.ShortestDistance(m_RouteSpot, spot).MagnitudeIsLessThan(24.0F)) {
		return true;
	}
	m_Route.clear();
	m_RouteSpot = spot;
	m_RouteBest = FLT_MAX;
	m_RouteReplans = 0;
	m_UnstickTries = 0;
	m_UnstickStep = -1;
	m_RouteProgressTimer.Reset();
	m_RouteCheckTimer.Reset();
	// Open air all the way there for its whole hull, and no call to keep clear of anyone (or a full-risk order, which doesn't): straight there.
	const bool fullRisk = direct >= tuning.StraightFlightRisk;
	if (!g_SceneMan.GetScene() || ((care <= 0.0F || fullRisk) && HullLineClear(m_Pos, goal))) {
		return AutopilotGoTo(goal);
	}
	// Else through the air, over, under or round what is in the way (with no care for enemies on a full-risk order), as near as it can get.
	bool reached = false;
	if (AirPath(m_Pos, goal, fullRisk ? 0.0F : care, m_Route, reached) && !m_Route.empty()) {
		return AutopilotGoTo(m_Route.front());
	}
	// (No room to start from at all, wedged in somewhere: straight at it, as before.)
	m_Route.clear();
	return AutopilotGoTo(goal);
}

void ACShip::UpdateCraftAvoidance() {
	if (!m_AvoidTimer.IsPastSimMS(100)) {
		return;
	}
	m_AvoidTimer.Reset();
	m_AvoidOffset.Reset();
	const bool wraps = g_SceneMan.SceneWrapsX();
	const float radius = GetRadius();
	// How far ahead it looks, in seconds, and its own way in pixels a second.
	const float horizon = 2.0F;
	const Vector velocity = m_Vel * c_PPM;
	for (const Actor* actor: g_MovableMan.GetActorList()) {
		const ACraft* other = dynamic_cast<const ACraft*>(actor);
		if (!other || other == this || other->IsDead() || other->IsSetToDelete()) {
			continue;
		}
		// (A body's room each, a little less than the radius, which takes in the corners of a wide hull.)
		const float clearance = (radius + other->GetRadius()) * 0.85F;
		const Vector toOther = g_SceneMan.ShortestDistance(m_Pos, other->GetPos(), wraps);
		const Vector closing = other->GetVel() * c_PPM - velocity;
		if (toOther.MagnitudeIsGreaterThan(clearance + closing.GetMagnitude() * horizon + 40.0F)) {
			continue;
		}
		// Where the other will be from it when they are nearest, within the look ahead.
		float speedSquared = closing.GetSqrMagnitude();
		float when = speedSquared > 0.01F ? std::clamp(-(toOther.m_X * closing.m_X + toOther.m_Y * closing.m_Y) / speedSquared, 0.0F, horizon) : 0.0F;
		Vector nearest = toOther + closing * when;
		float gap = nearest.GetMagnitude();
		if (gap >= clearance) {
			continue;
		}
		// The sooner, the harder it steers; and the more it would overlap, the further.
		float need = (clearance - gap) * (1.0F - 0.6F * when / horizon);
		Vector away = gap > 1.0F ? -nearest / gap : Vector(toOther.m_X > 0.0F ? -1.0F : 1.0F, 0.0F);
		const ACShip* ship = dynamic_cast<const ACShip*>(other);
		bool otherGivesWay = ship && ship->CanFly() && !ship->IsPlayerControlled();
		// Of two ships, the higher (or, level, the one first made) climbs over; the other gives way, holding back to the side of it.
		bool climbs = !otherGivesWay || m_Pos.m_Y < other->GetPos().m_Y - 4.0F || (std::abs(m_Pos.m_Y - other->GetPos().m_Y) <= 4.0F && GetUniqueID() < other->GetUniqueID());
		Vector steer(away.m_X * need * 1.5F, 0.0F);
		if (climbs) {
			steer.m_Y = -need * 2.0F;
		} else if (away.m_Y > 0.0F) {
			// (Down only while there is room under it.)
			float room = g_SceneMan.CastNotMaterialRay(m_Pos, Vector(0.0F, radius * 2.0F + need), MaterialColorKeys::g_MaterialAir, 4);
			if (room < 0.0F) {
				steer.m_Y = away.m_Y * need;
			}
		}
		m_AvoidOffset += steer;
	}
	if (m_AvoidOffset.MagnitudeIsGreaterThan(radius * 3.0F)) {
		m_AvoidOffset.SetMagnitude(radius * 3.0F);
	}
}

bool ACShip::EngageTarget(const Actor* target) {
	if (!target || !CanFly() || !AutopilotStarted()) {
		return false;
	}
	m_EnginesOff = false;
	StopRam();
	if (m_EngageID != target->GetUniqueID()) {
		m_EngageID = target->GetUniqueID();
		m_HasCombatSpot = false;
		// (Looked over at once, not up to a quarter of a second on.)
		m_CombatTimer.SetElapsedSimTimeMS(1000);
	}
	return true;
}

void ACShip::UpdateCombat() {
	// Hit since it last looked: its health fell.
	if (float health = GetHealth(); m_CombatHealth >= 0.0F && health < m_CombatHealth - 0.25F) {
		m_HitTimer.Reset();
	}
	m_CombatHealth = GetHealth();
	if (IsRamming() || !m_CombatTimer.IsPastSimMS(250)) {
		return;
	}
	m_CombatTimer.Reset();
	auto enemyByID = [this](long id) -> const Actor* {
		const Actor* actor = id ? dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(id)) : nullptr;
		return actor && !actor->IsDead() && !actor->IsSetToDelete() && actor->GetTeam() != m_Team ? actor : nullptr;
	};
	Vector suppressCentre;
	float suppressRadius = 0.0F;
	// (Sent somewhere on a plain move, it stays where it was sent: its guns fight from there, it doesn't fly off after what they're on.)
	const bool holding = (m_AIMode == AIMODE_SENTRY || m_AIMode == AIMODE_STAY || (m_AIMode == AIMODE_GOTO && m_HasCombatSpot)) && m_StandingOrder.Kind != ORDER_MOVE;
	const Actor* target = enemyByID(m_EngageID);
	if (!target) {
		m_EngageID = 0;
		target = enemyByID(m_StandingOrder.TargetID);
	}
	// (Holding: the enemy its guns are on, unless it was told to stay put or to suppress a place.)
	if (!target && holding && !m_StandingOrder.Hold && !GetSuppressZone(suppressCentre, suppressRadius)) {
		for (const GunState& gun: m_Guns) {
			if ((target = enemyByID(gun.TargetID))) {
				break;
			}
		}
		// (Still fighting the one it has a position for, though no gun is on it just now.)
		if (!target && m_HasCombatSpot) {
			target = enemyByID(m_CombatTargetID);
		}
	}
	if (m_Controller.IsPlayerControlled() || !CanFly() || !target) {
		if (m_HasCombatSpot && !m_Controller.IsPlayerControlled() && CanFly()) {
			// The fight over: back over its post, or held where it is.
			if (m_StandingOrder.HasPost) {
				FlyRouteTo(m_StandingOrder.Post);
			} else {
				AutopilotGoTo(m_Pos);
			}
		}
		m_HasCombatSpot = false;
		m_CombatSearching = false;
		return;
	}
	const bool wraps = g_SceneMan.SceneWrapsX();
	const float radius = GetRadius();
	const float range = m_GunRange;
	const float direct = g_SettingsMan.DirectOrders().Follows.Vehicles ? std::clamp(m_StandingOrder.Direct, -1.0F, 1.0F) : 0.0F; // (Not following risk: a normal order's.)
	// How far off to fight from, as a share of its range: half on a normal order, three quarters on a cautious one, under a third on a direct one.
	const float share = direct < 0.0F ? 0.5F - 0.25F * direct : 0.5F - 0.2F * direct;
	const Vector enemyPos = target->GetPos();
	const Vector fromEnemy = g_SceneMan.ShortestDistance(enemyPos, m_Pos, wraps);
	int side = std::abs(fromEnemy.m_X) > radius * 0.5F ? (fromEnemy.m_X > 0.0F ? 1 : -1) : (m_HFlipped ? 1 : -1);
	// The pull of its post: it fights within the reach ReturnDefenders leaves it.
	const bool anchored = m_StandingOrder.HasPost;
	const float reach = AutopilotHoverHeight() * 1.5F + radius + 40.0F;

	bool hasShot = false;
	for (const GunState& gun: m_Guns) {
		hasShot = hasShot || gun.TargetID == target->GetUniqueID();
	}
	if (hasShot) {
		m_NoShotTimer.Reset();
		m_CombatSearching = false;
	}
	const bool settled = m_AIMode != AIMODE_GOTO;
	bool move = !m_HasCombatSpot || m_CombatTargetID != target->GetUniqueID();
	// The enemy moved out of reach of where it fights from, or right under it.
	if (!move && settled) {
		float across = std::abs(fromEnemy.m_X);
		move = across < range * std::max(share - 0.25F, 0.1F) || across > range * 0.85F || std::abs(fromEnemy.m_X) < radius;
	}
	bool search = false;
	if (!move && settled && m_NoShotTimer.IsPastSimMS(2000)) {
		move = true;
		search = true;
	}
	bool jink = false;
	const float jinkEvery = direct >= 0.9F ? -1.0F : (direct < 0.0F ? 1500.0F : 3000.0F + 2000.0F * direct);
	if (!move && settled && jinkEvery > 0.0F && !m_HitTimer.IsPastSimMS(1000) && m_JinkTimer.IsPastSimMS(static_cast<long long>(jinkEvery))) {
		move = true;
		jink = true;
	}
	if (!move) {
		return;
	}
	auto clearShot = [&enemyPos, wraps](const Vector& from) {
		Vector blocked;
		Vector ray = g_SceneMan.ShortestDistance(from, enemyPos, wraps);
		return !g_SceneMan.CastStrengthRay(from, ray, 5.0F, blocked, 4);
	};
	auto keepNearPost = [&](Vector spot) {
		if (anchored) {
			Vector off = g_SceneMan.ShortestDistance(m_StandingOrder.Post, spot, wraps);
			if (std::abs(off.m_X) > reach) {
				spot.m_X = m_StandingOrder.Post.m_X + (off.m_X > 0.0F ? reach : -reach);
			}
		}
		g_SceneMan.WrapPosition(spot);
		return spot;
	};
	Vector spot;
	if (jink) {
		// A little way off where it is, never nearer over the enemy; outward on a cautious order.
		float across = (RandomNum(0.0F, 1.0F) < (direct < 0.0F ? 0.8F : 0.5F) ? 1.0F : -1.0F) * RandomNum(0.08F, 0.16F) * range * static_cast<float>(side);
		spot = keepNearPost(m_Pos + Vector(across, RandomNum(-50.0F, 30.0F)));
		if (std::abs(g_SceneMan.ShortestDistance(enemyPos, spot, wraps).m_X) < range * 0.15F) {
			spot = keepNearPost(m_Pos + Vector(std::abs(across) * static_cast<float>(side), RandomNum(-50.0F, 0.0F)));
		}
		m_JinkTimer.Reset();
	} else {
		// The first of these with a clear line to the enemy: its side first (the other first when looking for a shot it didn't find on this
		// one), at its distance, nearer, then further, each just clear of the ground and higher.
		int firstSide = search && m_CombatSearching ? -side : side;
		Vector fallback;
		bool found = false;
		for (int s: {firstSide, -firstSide}) {
			for (float distance: {share, share * 0.7F, std::min(share * 1.3F, 0.8F)}) {
				for (float lift: {0.0F, -60.0F, -120.0F}) {
					Vector ground = keepNearPost(enemyPos + Vector(static_cast<float>(s) * distance * range, 0.0F));
					Vector candidate = LowPoint(ground) + Vector(0.0F, lift);
					candidate.m_Y = std::max(candidate.m_Y, SkyTop());
					if (fallback.IsZero()) {
						fallback = candidate;
					}
					if (clearShot(candidate)) {
						spot = candidate;
						found = true;
						break;
					}
				}
				if (found) {
					break;
				}
			}
			if (found) {
				break;
			}
		}
		if (!found) {
			spot = fallback;
		}
		m_CombatSearching = search;
		m_NoShotTimer.Reset();
	}
	m_CombatSpot = spot;
	m_CombatTargetID = target->GetUniqueID();
	m_HasCombatSpot = true;
	// Straight there through open air, at the height chosen; round what is in the way by a route otherwise.
	if (HullLineClear(m_Pos, spot)) {
		m_Route.clear();
		AutopilotGoTo(spot);
	} else {
		FlyRouteTo(spot);
	}
}

bool ACShip::RamTarget(const Actor* target, bool automatic) {
	if (!target || target->GetTeam() == m_Team || !CanFly() || !AutopilotStarted()) {
		return false;
	}
	m_EnginesOff = false;
	m_Route.clear();
	m_EngageID = 0;
	m_HasCombatSpot = false;
	m_RamAtPlace = false;
	if (m_RamID != target->GetUniqueID()) {
		m_RamID = target->GetUniqueID();
		// (From the side it is on already.)
		m_RamSide = g_SceneMan.ShortestDistance(target->GetPos(), m_Pos, g_SceneMan.SceneWrapsX()).m_X >= 0.0F ? 1.0F : -1.0F;
		m_RamPhase = RamPhase::RunUp;
		m_RamTimer.Reset();
	}
	m_RamAuto = automatic;
	// (Its own flying meanwhile: once the ram is over it holds where it is.)
	m_AIMode = AIMODE_SENTRY;
	m_Autopilot.lastAIMode = AIMODE_SENTRY;
	return true;
}

bool ACShip::RamPlace(const Vector& point) {
	if (!CanFly() || !AutopilotStarted()) {
		return false;
	}
	m_EnginesOff = false;
	m_Route.clear();
	m_EngageID = 0;
	m_HasCombatSpot = false;
	if (!m_RamAtPlace || g_SceneMan.ShortestDistance(m_RamPoint, point, g_SceneMan.SceneWrapsX()).MagnitudeIsGreaterThan(20.0F)) {
		m_RamSide = g_SceneMan.ShortestDistance(point, m_Pos, g_SceneMan.SceneWrapsX()).m_X >= 0.0F ? 1.0F : -1.0F;
		m_RamPhase = RamPhase::RunUp;
		m_RamTimer.Reset();
	}
	m_RamID = 0;
	m_RamAtPlace = true;
	m_RamPoint = point;
	m_RamAuto = false;
	m_AIMode = AIMODE_SENTRY;
	m_Autopilot.lastAIMode = AIMODE_SENTRY;
	return true;
}

int ACShip::ArmedGunCount() const {
	int armed = 0;
	for (const Attachable* attachable: m_Attachables) {
		if (const Turret* turret = dynamic_cast<const Turret*>(attachable); turret && turret->HasMountedDevice()) {
			++armed;
		}
	}
	return armed;
}

void ACShip::UpdateAutoRam() {
	if (ArmedGunCount() > 0) {
		m_HadGuns = true;
		return;
	}
	if (!m_HadGuns || IsRamming() || !m_RamLookTimer.IsPastSimMS(500) || m_Controller.IsPlayerControlled() || m_EnginesOff || m_StandingOrder.Hold || m_StandingOrder.Kind == ORDER_MOVE) {
		return;
	}
	m_RamLookTimer.Reset();
	const bool wraps = g_SceneMan.SceneWrapsX();
	const Actor* target = m_EngageID ? dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(m_EngageID)) : nullptr;
	if (!target || target->IsDead() || target->GetTeam() == m_Team) {
		target = nullptr;
		float nearest = m_GunRange;
		for (const Actor* actor: g_MovableMan.GetActorList()) {
			if (actor == this || actor->GetTeam() == m_Team || actor->GetTeam() == Activity::NoTeam || actor->IsDead() || actor->IsSetToDelete() || actor->GetHealth() <= 0.0F) {
				continue;
			}
			if (float distance = g_SceneMan.ShortestDistance(m_Pos, actor->GetPos(), wraps).GetMagnitude(); distance < nearest) {
				nearest = distance;
				target = actor;
			}
		}
	}
	if (target) {
		RamTarget(target, true);
	}
}

bool ACShip::UpdateRam() {
	if (!IsRamming()) {
		return false;
	}
	const bool wraps = g_SceneMan.SceneWrapsX();
	// (Ramming a place, there is no one to lose: it goes on till another order.)
	const Actor* target = m_RamAtPlace ? nullptr : dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(m_RamID));
	bool over = m_Controller.IsPlayerControlled() || !CanFly();
	if (!m_RamAtPlace) {
		over = over || !target || target->IsDead() || target->IsSetToDelete() || target->GetTeam() == m_Team;
	}
	if (!over && target && m_RamAuto && g_SceneMan.ShortestDistance(m_Pos, target->GetPos(), wraps).MagnitudeIsGreaterThan(m_GunRange * 1.5F)) {
		over = true;
	}
	if (over) {
		// Done: it holds where it is, for orders (or, guns gone, for the next enemy to come near).
		StopRam();
		m_HoldPoint = m_Pos;
		m_HasHoldPoint = true;
		m_AIMode = AIMODE_SENTRY;
		m_Autopilot.lastAIMode = AIMODE_SENTRY;
		return false;
	}
	const float radius = GetRadius();
	const Vector enemyPos = target ? target->GetPos() : m_RamPoint;
	const Vector enemyVel = target ? target->GetVel() : Vector();
	const float runUp = std::max(radius * 4.0F, 260.0F);
	const float above = std::max(radius, 60.0F);
	// Where it strikes: the enemy's middle, but not so low its hull goes into the ground under it.
	auto strikeAt = [&](const Vector& at) {
		Vector point = at;
		g_SceneMan.WrapPosition(point);
		const float ground = g_SceneMan.MovePointToGround(point, 0.0F, 4).m_Y;
		point.m_Y = std::max(std::min(point.m_Y, ground - radius * 0.6F), SkyTop());
		return point;
	};
	const float flightSpeed = m_FlightSpeed;
	switch (m_RamPhase) {
		case RamPhase::RunUp: {
			// Off to its side and a little above, for the run at it.
			const Vector spot = LowPoint(enemyPos + Vector(m_RamSide * runUp, -above));
			SteerTo(spot, true);
			if (g_SceneMan.ShortestDistance(m_Pos, spot, wraps).MagnitudeIsLessThan(std::max(radius, 50.0F)) || m_RamTimer.IsPastSimMS(6000)) {
				m_RamPhase = RamPhase::Charge;
				m_RamTimer.Reset();
			}
			break;
		}
		case RamPhase::Charge: {
			// Flat out at where it will be when it gets there, and on through it, down near the ground if that's where it is.
			const Vector toEnemy = g_SceneMan.ShortestDistance(m_Pos, enemyPos, wraps);
			m_FlightSpeed = flightSpeed * std::max(m_RamSpeedScale, 1.0F);
			const float time = std::min(toEnemy.GetMagnitude() / (m_FlightSpeed * c_PPM), 1.5F);
			const Vector aim = strikeAt(enemyPos + enemyVel * (c_PPM * time));
			const Vector through = aim + Vector(-m_RamSide * radius * 2.0F, 0.0F);
			SteerTo(through, false, 0.5F);
			m_FlightSpeed = flightSpeed;
			// Past it, or too long at it: it pulls away beyond.
			if (g_SceneMan.ShortestDistance(enemyPos, m_Pos, wraps).m_X * m_RamSide < -radius || m_RamTimer.IsPastSimMS(5000)) {
				m_RamPhase = RamPhase::PullAway;
				m_RamTimer.Reset();
			}
			break;
		}
		case RamPhase::PullAway: {
			// Up and on beyond it, then round at it again from that side.
			const Vector spot = LowPoint(enemyPos + Vector(-m_RamSide * runUp, -(radius * 3.0F + 80.0F)));
			SteerTo(spot, true);
			if (g_SceneMan.ShortestDistance(m_Pos, spot, wraps).MagnitudeIsLessThan(std::max(radius * 1.5F, 80.0F)) || m_RamTimer.IsPastSimMS(4000)) {
				m_RamSide = -m_RamSide;
				m_RamPhase = RamPhase::Charge;
				m_RamTimer.Reset();
			}
			break;
		}
	}
	return true;
}

void ACShip::UpdateRamHits() {
	if (!IsRamming() || !m_RamHitTimer.IsPastSimMS(500) || m_Status == DYING || m_Status == DEAD) {
		return;
	}
	const bool wraps = g_SceneMan.SceneWrapsX();
	const float radius = GetRadius();
	bool hit = false;
	// Whatever the hull has reached that isn't its own side's: the enemy it rams, or anything at the place it rams.
	for (Actor* other: g_MovableMan.GetActorList()) {
		if (other == this || other->GetTeam() == m_Team || other->IsDead() || other->IsSetToDelete() || (m_RamID && !m_RamAtPlace && static_cast<long>(other->GetUniqueID()) != m_RamID)) {
			continue;
		}
		const Vector toTarget = g_SceneMan.ShortestDistance(m_Pos, other->GetPos(), wraps);
		if (toTarget.MagnitudeIsGreaterThan(radius * 0.7F + other->GetRadius() * 0.5F) || toTarget.MagnitudeIsLessThan(0.01F)) {
			continue;
		}
		// How fast the hull closes on it, in m/s: under 4 it's a nudge, not a ram.
		const Vector way = toTarget / toTarget.GetMagnitude();
		const float speed = (m_Vel - other->GetVel()).Dot(way);
		if (speed < 4.0F) {
			continue;
		}
		hit = true;
		const float share = std::clamp((speed - 4.0F) / 12.0F, 0.0F, 1.25F);
		const float damage = share * m_RamDamage * other->GetMaxHealth();
		other->AddHealth(-damage);
		// It's thrown aside, as hard as the ship's weight and speed would throw it, up to 12 m/s more.
		const float otherMass = std::max(other->GetMass(), 1.0F);
		const float throwImpulse = std::min(GetMass() * speed * 0.4F, otherMass * 12.0F);
		other->AddAbsImpulseForce((way + Vector(0.0F, -0.4F)) * throwImpulse, other->GetPos());
		// The ship takes a share of it.
		AddHealth(-std::min(damage * m_RamSelfDamage, GetMaxHealth() / 12.0F));
	}
	if (hit) {
		// It loses some of its speed, and pulls away to come round again.
		m_RamHitTimer.Reset();
		m_Vel *= 0.8F;
		m_RamPhase = RamPhase::PullAway;
		m_RamTimer.Reset();
	}
}

void ACShip::UpdateAutopilot() {
	if (!CanFly()) {
		// (Left on a delivery or a trip home, a pilot getting in would fly off with it: it holds still for them instead.)
		if (m_AIMode == AIMODE_DELIVER || m_AIMode == AIMODE_RETURN || m_AIMode == AIMODE_BRAINHUNT || m_AIMode == AIMODE_BOMB) {
			SetAIMode(AIMODE_SENTRY);
		}
		m_HasHoldPoint = false;
		return;
	}
	DropShipAutopilot& ai = m_Autopilot;
	if (!ai.begun) {
		ai.begun = true;
		ai.lastAIMode = AIMODE_NONE;
		ai.savedHoverHeightModifier = m_HoverHeightModifier;
		ai.hoverAlt = GetDiameter() + m_HoverHeightModifier;
		// (A human team's empty ship isn't sent off on a delivery of nothing.)
		if (m_AIMode == AIMODE_DELIVER && IsInventoryEmpty() && g_ActivityMan.GetActivity() && g_ActivityMan.GetActivity()->IsHumanTeam(m_Team)) {
			m_AIMode = AIMODE_STAY;
		}
		m_FlownTimer.Reset();
		RestoreFlight();
	}
	if (ai.savedHoverHeightModifier != m_HoverHeightModifier) {
		ai.savedHoverHeightModifier = m_HoverHeightModifier;
		ai.hoverAlt = GetDiameter() + m_HoverHeightModifier;
	}
	// A player flew it a while (no flight controller for half a second): it holds where it was let go, at that height, rather than going
	// back down to its hover height as a drop ship does.
	if (m_FlownTimer.IsPastSimMS(500) && m_AIMode != AIMODE_GOTO) {
		m_HoldPoint = m_Pos;
		m_HasHoldPoint = true;
	}
	m_FlownTimer.Reset();
	// The AI paused: it hovers where it was when the pause came and does nothing else (no fighting, ramming, or going on along its route),
	// then carries on with what it was doing once the AI is let loose.
	if (!Controller::AIMayAct()) {
		if (!m_HeldByPause) {
			m_HeldByPause = true;
			m_PausedAt = m_Pos;
		}
		if (!m_EnginesOff) {
			SteerTo(m_PausedAt, true);
		}
		return;
	}
	m_HeldByPause = false;
	// Landed with its engines off: it stays down till sent somewhere (a go-to, however given).
	if (m_EnginesOff) {
		if (m_AIMode != AIMODE_GOTO) {
			m_Route.clear();
			return;
		}
		m_EnginesOff = false;
	}
	UpdateCraftAvoidance();
	UpdateAutoRam();
	UpdateCombat();
	// Along its route (FlyRouteTo, UpdateRoute).
	UpdateRoute();
	// A ship stays in the fight: with nothing (more) to deliver it holds where it is rather than flying home as a drop ship does. Only an
	// order to go home (RETURN) sends it off.
	if (m_AIMode == AIMODE_DELIVER && IsInventoryEmpty()) {
		SetAIMode(AIMODE_STAY);
	}
	if (m_AIMode == AIMODE_SCUTTLE) {
		GibThis();
		return;
	}
	if (UpdateRam()) {
		return;
	}
	// (Working itself free, UpdateRoute: to the move it is making.)
	if (m_UnstickStep >= 0) {
		SteerTo(m_UnstickPoint, true, 0.5F);
		return;
	}
	UpdateFlight();
}

void ACShip::RestoreFlight() {
	const FlightToRestore restore = m_FlightToRestore;
	m_FlightToRestore = FlightToRestore::None;
	if (restore == FlightToRestore::Goal) {
		// (AutopilotGoTo puts it in a go-to whatever AI mode it was read with: one there already is saved as holding.)
		std::deque<Vector> route = m_Route;
		if (!route.empty()) {
			AutopilotGoTo(route.front());
			m_Route = route;
		} else {
			AutopilotGoTo(m_GoalToRestore);
		}
	} else if (restore == FlightToRestore::Hold && m_AIMode != AIMODE_GOTO && m_AIMode != AIMODE_RETURN && m_AIMode != AIMODE_DELIVER) {
		// Told of its AI mode as if it had seen it begin, so it holds where it held rather than taking the mode up afresh.
		m_Route.clear();
		m_Autopilot.lastAIMode = static_cast<AIMode>(m_AIMode);
	}
}

void ACShip::UpdateFlight() {
	DropShipAutopilot& ai = m_Autopilot;
	const float radius = GetRadius();
	const bool wraps = g_SceneMan.SceneWrapsX();
	// A new AI mode: where it holds for it. (A go-to sets its own place, AutopilotGoTo.)
	if (m_AIMode != ai.lastAIMode) {
		const bool wasDelivering = ai.lastAIMode == AIMODE_DELIVER;
		const bool first = ai.lastAIMode == AIMODE_NONE;
		ai.lastAIMode = static_cast<AIMode>(m_AIMode);
		if (m_AIMode == AIMODE_GOTO) {
			// (Set to go somewhere by its waypoints, not AutopilotGoTo: it flies to the end of the path they give.)
			ai.hasWaypoint = false;
			UpdateMovePath();
		} else if (m_AIMode == AIMODE_RETURN) {
			// Home: off the scene's orbit edge.
			switch (g_SceneMan.GetSceneOrbitDirection()) {
				case Directions::Down:
					m_HoldPoint = Vector(m_Pos.m_X, static_cast<float>(g_SceneMan.GetSceneHeight()) + 5000.0F);
					break;
				case Directions::Left:
					m_HoldPoint = Vector(-5000.0F, m_Pos.m_Y);
					break;
				case Directions::Right:
					m_HoldPoint = Vector(static_cast<float>(g_SceneMan.GetSceneWidth()) + 5000.0F, m_Pos.m_Y);
					break;
				default:
					m_HoldPoint = Vector(m_Pos.m_X, -5000.0F);
					break;
			}
			m_HasHoldPoint = true;
		} else if (m_AIMode == AIMODE_DELIVER || (m_AIMode == AIMODE_STAY && (wasDelivering || first))) {
			// Its cargo let out low over the ground under it, or, just come in (bought, from orbit), waiting there: the only
			// times it goes down to it by itself.
			// (Low, but with room under it for whoever gets out to land on their feet.)
			m_HoldPoint = DropOffPoint(Vector(m_Pos.m_X, std::max(m_Pos.m_Y, SkyTop())));
			m_HasHoldPoint = true;
		} else {
			// Any other: it holds where it is, at the height it is (in the scene).
			m_HoldPoint = Vector(m_Pos.m_X, std::max(m_Pos.m_Y, SkyTop() + radius));
			m_HasHoldPoint = true;
		}
		if (wasDelivering && m_AIMode != AIMODE_DELIVER) {
			CloseHatch();
		}
	}
	if (!m_HasHoldPoint) {
		m_HoldPoint = m_Pos;
		m_HasHoldPoint = true;
	}

	Vector target = m_HoldPoint;
	bool stop = m_AIMode != AIMODE_RETURN;
	if (m_AIMode == AIMODE_GOTO) {
		if (IsWaitingOnNewMovePath()) {
			// (Its path to the waypoints still being found: it holds where it is meanwhile.)
			ai.hasWaypoint = false;
			m_HoldPoint = m_Pos;
		} else {
			if (!ai.hasWaypoint && !m_MovePath.empty()) {
				ai.waypoint = LowPoint(m_MovePath.back());
				ai.hasWaypoint = true;
			}
			if (ai.hasWaypoint) {
				target = ai.waypoint;
				stop = m_Route.size() <= 1;
				const Vector off = g_SceneMan.ShortestDistance(m_Pos, ai.waypoint, wraps);
				// There: near the place, or sat on the ground right by it (sent down onto it, its middle can't get all the way).
				const bool there = off.MagnitudeIsLessThan(20.0F) ||
				                   (stop && std::abs(off.m_X) < radius && off.m_Y > 0.0F && m_Vel.MagnitudeIsLessThan(1.0F) && GetAltitude(1000, 4) < radius * 0.9F);
				if (stop && there) {
					if (!m_Waypoints.empty()) {
						ClearMovePath();
						UpdateMovePath();
						ai.hasWaypoint = false;
					} else {
						// It holds there, at the height it was sent to.
						m_AIMode = AIMODE_SENTRY;
						ai.lastAIMode = AIMODE_SENTRY;
						m_HoldPoint = off.MagnitudeIsLessThan(20.0F) ? ai.waypoint : m_Pos;
						target = m_HoldPoint;
					}
				}
			} else {
				// Nowhere to go: it holds.
				m_AIMode = AIMODE_SENTRY;
				ai.lastAIMode = AIMODE_SENTRY;
				m_HoldPoint = m_Pos;
				target = m_HoldPoint;
			}
		}
	} else if (m_AIMode == AIMODE_DELIVER && m_HatchState == CLOSED) {
		// Over the place and still: the hatch opens and the cargo gets out (and with it empty it holds there, STAY).
		if (g_SceneMan.ShortestDistance(m_Pos, m_HoldPoint, wraps).MagnitudeIsLessThan(radius) && m_Vel.MagnitudeIsLessThan(2.0F)) {
			OpenHatch();
		}
	}
	SteerTo(target, stop);
}

float ACShip::GroundTopAt(float x, float y) const {
	Vector point(x, std::clamp(y, 0.0F, static_cast<float>(g_SceneMan.GetSceneHeight() - 1)));
	g_SceneMan.WrapPosition(point);
	int px = static_cast<int>(point.m_X);
	int py = static_cast<int>(point.m_Y);
	if (g_SceneMan.GetTerrMatter(px, py) == MaterialColorKeys::g_MaterialAir) {
		return FLT_MAX;
	}
	while (py > 0 && g_SceneMan.GetTerrMatter(px, py) != MaterialColorKeys::g_MaterialAir) {
		py -= 8;
	}
	return static_cast<float>(std::max(py, 0));
}

void ACShip::SteerTo(const Vector& target, bool stop, float clearanceScale) {
	const bool wraps = g_SceneMan.SceneWrapsX();
	const float radius = GetRadius();
	// How far over the ground it keeps on the way: its body and a little more.
	const float clearance = LowClearance() * clearanceScale;
	const float halfWidth = HullHalfWidth();
	Vector aim = target + m_AvoidOffset;
	Vector toAim = g_SceneMan.ShortestDistance(m_Pos, aim, wraps);

	// The ground ahead, along the line to the place (as far as it would go in two and a half seconds, and at least two bodies), and under
	// it now, each with the width of its hull: the lowest it may fly is its clearance over the highest of it. Not near the place itself,
	// which it may be sent right down onto.
	float floor = FLT_MAX;
	const float speedAcross = std::abs(m_Vel.m_X) * c_PPM;
	const float ahead = std::min(std::abs(toAim.m_X), std::clamp(speedAcross * 2.5F, radius * 2.0F, 600.0F));
	const float way = toAim.m_X >= 0.0F ? 1.0F : -1.0F;
	const float step = std::max(radius, 16.0F);
	for (float along = 0.0F; along <= ahead + 0.5F; along += step) {
		if (std::abs(toAim.m_X) - along < radius * 1.5F) {
			break;
		}
		// (Down the straight line to the place, where it would be at that point, its underside.)
		const float share = std::abs(toAim.m_X) > 1.0F ? along / std::abs(toAim.m_X) : 0.0F;
		const float lineY = m_Pos.m_Y + toAim.m_Y * share;
		for (float side: {-1.0F, 0.0F, 1.0F}) {
			const float x = m_Pos.m_X + way * along + side * halfWidth;
			floor = std::min(floor, GroundTopAt(x, lineY + clearance) - clearance);
		}
	}
	if (aim.m_Y > floor) {
		aim.m_Y = floor;
		toAim = g_SceneMan.ShortestDistance(m_Pos, aim, wraps);
	}
	// (A new place, or not stopping at it: what it built up against drift starts again.)
	if (!stop || g_SceneMan.ShortestDistance(m_FlightTarget, aim, wraps).MagnitudeIsGreaterThan(20.0F)) {
		m_SteerTrim.Reset();
	}
	m_FlightTarget = aim;
	m_FlightFloor = floor;

	// The speeds wanted, in m/s: across, by how far there is to go, braking in time to stop at the place (what its tilting engines can
	// slow it by: the gravity times the tangent of their tilt, with a margin), or on through it to a route's next point. Slower across while
	// it has a rise to climb over.
	const float gravity = std::max(g_SceneMan.GetGlobalAcc().GetMagnitude(), 1.0F);
	const float brakeAcross = std::max(gravity * std::tan(m_MaxEngineAngle * c_PI / 180.0F) * 0.6F, 0.5F);
	const float across = std::abs(toAim.m_X) / c_PPM;
	float wantX = stop ? std::min({m_FlightSpeed, std::sqrt(2.0F * brakeAcross * across), across * 1.2F}) : std::min(m_FlightSpeed, across * 1.2F + m_FlightSpeed * 0.5F);
	if (const float climb = m_Pos.m_Y - floor; climb > radius * 0.5F) {
		wantX *= std::clamp(1.0F - climb / (radius * 4.0F), 0.15F, 1.0F);
	}
	wantX *= toAim.m_X >= 0.0F ? 1.0F : -1.0F;
	// Up and down likewise (the y is down the scene): braking at half a g coming down, so it doesn't drop onto the place.
	const float down = toAim.m_Y / c_PPM;
	const float climbSpeed = std::clamp(m_ClimbSpeed, 1.0F, 7.5F);
	float wantY = down < 0.0F ? -std::min(climbSpeed, std::abs(down) * 1.2F) : std::min({climbSpeed * 0.7F, std::sqrt(2.0F * gravity * 0.5F * down), down * 1.2F});

	// The stick: up or down is a speed to hold (the drop ship's throttle, its range 7.5 m/s), across a tilt toward the speed wanted.
	// Near the place, what's left off it builds up on the stick, so the drop ship's hover trim (made for an empty one), a load, a lean or the
	// stick's dead zone don't leave it held off to one side or hanging high or low of it.
	if (stop && toAim.MagnitudeIsLessThan(std::max(radius * 3.0F, 120.0F))) {
		const float deltaTime = g_TimerMan.GetDeltaTimeSecs();
		m_SteerTrim += Vector(toAim.m_X, toAim.m_Y) * (deltaTime / c_PPM);
		m_SteerTrim.m_X = std::clamp(m_SteerTrim.m_X, -3.0F, 3.0F);
		m_SteerTrim.m_Y = std::clamp(m_SteerTrim.m_Y, -3.0F, 3.0F);
	}
	const float stickX = std::clamp((wantX - m_Vel.m_X) * 0.35F + m_SteerTrim.m_X * 0.15F, -1.0F, 1.0F);
	const float stickY = std::clamp((wantY + 0.5F * (wantY - m_Vel.m_Y)) / 7.5F + m_SteerTrim.m_Y * 0.12F, -1.0F, 1.0F);
	m_Controller.SetState(MOVE_UP, false);
	m_Controller.SetState(MOVE_DOWN, false);
	m_Controller.SetAnalogMove(Vector(stickX, stickY));
}

bool ACShip::AirPath(const Vector& from, const Vector& to, float care, std::deque<Vector>& route, bool& reached) const {
	return AirRoute::Plan(GetAirBody(), from, to, care, route, reached);
}

void ACShip::UpdateRoute() {
	// Any other order (it holds, or is sent somewhere else) or the player flying it ends the route and any working free.
	if (m_Controller.IsPlayerControlled() || m_AIMode != AIMODE_GOTO || m_RamID) {
		m_Route.clear();
		m_UnstickStep = -1;
		m_RouteBest = FLT_MAX;
		m_RouteProgressTimer.Reset();
		return;
	}
	const bool wraps = g_SceneMan.SceneWrapsX();
	// Working itself free: a few short moves off where it got stuck, back and up, up, back and down, on and up, each skipped where its hull
	// hasn't the room, then the way there planned again from where that leaves it.
	if (m_UnstickStep >= 0) {
		if (!m_UnstickTimer.IsPastSimMS(700)) {
			return;
		}
		m_UnstickTimer.Reset();
		if (NextUnstickMove()) {
			return;
		}
		const int tries = m_UnstickTries;
		m_Route.clear();
		FlyRouteTo(m_UnstickGoal);
		m_UnstickTries = tries;
		return;
	}
	const Vector target = m_Route.empty() ? m_Autopilot.waypoint : m_Route.front();
	const float distance = g_SceneMan.ShortestDistance(m_Pos, target, wraps).GetMagnitude();
	// On to the next point of its route once near the one it is flying to.
	if (!m_Route.empty() && distance < std::max(GetRadius(), 40.0F)) {
		m_Route.pop_front();
		m_RouteBest = FLT_MAX;
		m_RouteProgressTimer.Reset();
		m_RouteReplans = 0;
		m_UnstickTries = 0;
		if (!m_Route.empty()) {
			AutopilotGoTo(m_Route.front());
		}
		return;
	}
	if (distance < m_RouteBest - 8.0F) {
		m_RouteBest = distance;
		m_RouteProgressTimer.Reset();
	}
	// The way to its route's next point found blocked (dug, built, a door shut): planned again from where it is, a few times running at most.
	if (!m_Route.empty() && m_RouteCheckTimer.IsPastSimMS(500)) {
		m_RouteCheckTimer.Reset();
		if (m_RouteReplans < 3 && !HullLineClear(m_Pos, m_Route.front())) {
			const int replans = m_RouteReplans + 1;
			const int tries = m_UnstickTries;
			m_Route.clear();
			FlyRouteTo(m_RouteSpot);
			m_RouteReplans = replans;
			m_UnstickTries = tries;
			return;
		}
	}
	// No nearer where it is going for a couple of seconds, not there yet and barely moving: caught on something. It works itself free, as a
	// unit on foot does, a few times running at most.
	if (m_Autopilot.hasWaypoint && !IsWaitingOnNewMovePath() && m_RouteProgressTimer.IsPastSimMS(2500) && distance > 30.0F && m_Vel.MagnitudeIsLessThan(1.5F) && m_UnstickTries < 3) {
		++m_UnstickTries;
		m_UnstickGoal = m_Route.empty() ? target : m_RouteSpot;
		m_UnstickFrom = m_Pos;
		m_UnstickSide = g_SceneMan.ShortestDistance(m_Pos, target, wraps).m_X >= 0.0F ? 1.0F : -1.0F;
		m_UnstickStep = -1;
		m_RouteBest = FLT_MAX;
		m_RouteProgressTimer.Reset();
		m_UnstickTimer.Reset();
		NextUnstickMove();
	}
}

bool ACShip::NextUnstickMove() {
	// Back and up, up, back and down, on and up, off where it got stuck (back being away from where it was going).
	const std::array<Vector, 4> moves{Vector(-60.0F, -60.0F), Vector(0.0F, -100.0F), Vector(-60.0F, 40.0F), Vector(40.0F, -80.0F)};
	while (++m_UnstickStep < static_cast<int>(moves.size())) {
		Vector point = m_UnstickFrom + Vector(moves[m_UnstickStep].m_X * m_UnstickSide, moves[m_UnstickStep].m_Y);
		g_SceneMan.WrapPosition(point);
		if (HullLineClear(m_Pos, point)) {
			m_UnstickPoint = point;
			return true;
		}
	}
	m_UnstickStep = -1;
	return false;
}

void ACShip::PreControllerUpdate() {
	ZoneScoped;

	bool flown = CanFly();
	// (A player taking the controls starts the engines of a ship landed with them off.)
	if (m_EnginesOff && (!flown || (m_Controller.IsPlayerControlled() && GetControlSeat() <= 0))) {
		m_EnginesOff = false;
	}
	// (A player at a gun or in a passenger's seat doesn't fly it: the pilot, or the ship itself, holds it where it is. Nor does anyone
	// while its engines are off.)
	bool letGo = !flown || m_EnginesOff || (m_Controller.IsPlayerControlled() && GetControlSeat() > 0);
	if (!letGo) {
		m_Parked = false;
		m_TouchdownTimer.Reset();
		ACDropShip::PreControllerUpdate();
		return;
	}

	// The stick let go (or eased down, setting itself down), and given back after for the guns to aim by.
	std::array<bool, c_FlyingStates.size()> held{};
	for (size_t i = 0; i < c_FlyingStates.size(); ++i) {
		held[i] = m_Controller.IsState(c_FlyingStates[i]);
		m_Controller.SetState(c_FlyingStates[i], false);
	}
	Vector analogMove = m_Controller.GetAnalogMove();
	m_Controller.SetAnalogMove(Vector());

	if (!flown) {
		// Nobody at the controls: it comes down gently, faster the higher it is, and once it's down and still, the engines go off until someone gets in.
		float altitude = GetAltitude(1000, 4);
		bool down = altitude < GetRadius() * 0.75F && m_Vel.MagnitudeIsLessThan(1.5F);
		if (!down) {
			m_TouchdownTimer.Reset();
			if (m_Parked && altitude > GetRadius() * 1.5F) {
				// (Knocked off where it sat: it flies itself down again.)
				m_Parked = false;
			}
		} else if (m_TouchdownTimer.IsPastSimMS(600) && !m_Parked) {
			m_Parked = true;
			// Whoever it carried in its hold gets out, now it's down.
			if (!IsInventoryEmpty()) {
				OpenHatch();
			}
		}
		if (!m_Parked) {
			m_Controller.SetAnalogMove(Vector(0.0F, std::clamp(altitude / 300.0F, 0.25F, 1.0F)));
		}
	}

	ACDropShip::PreControllerUpdate();

	if ((!flown && m_Parked) || m_EnginesOff) {
		for (AEmitter* engine: {m_pLThruster, m_pRThruster, m_pULThruster, m_pURThruster}) {
			if (engine && engine->IsAttached()) {
				engine->EnableEmission(false);
			}
		}
	}

	for (size_t i = 0; i < c_FlyingStates.size(); ++i) {
		m_Controller.SetState(c_FlyingStates[i], held[i]);
	}
	m_Controller.SetAnalogMove(analogMove);
}

void ACShip::OnSave() {
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		if (Actor* crew = GetSeatOccupant(seat)) {
			crew->OnSave();
		}
	}
	Actor::OnSave();
}

void ACShip::Update() {
	ZoneScoped;

	// Going to orbit with its crew: they go with it, in its hold (so they're counted and kept, as whoever rides in a craft's hold is).
	if (m_CanEnterOrbit && (m_Pos.m_Y < -m_CharHeight - GetOrbitGrace() || m_Pos.m_Y > g_SceneMan.GetSceneHeight() + m_CharHeight + GetOrbitGrace()) && !m_Crew.IsEmpty()) {
		for (Actor* unit: m_Crew.TakeAllCrew()) {
			AddInventoryItem(unit);
		}
	}

	ACDropShip::Update();

	SeatCargo();
	m_Crew.UpdateSeatedCrew();

	// A knock hard enough in one update (a crash into a wall, a heavy hit) throws everyone out; going down, they bail out.
	// (Ramming, they're braced for it: only a knock three times as hard throws them out.)
	UpdateRamHits();
	const float throwSpeed = IsRamming() ? m_CrewThrowSpeed * 3.0F : m_CrewThrowSpeed;
	if (m_CrewThrowSpeed > 0.0F && !m_Crew.IsEmpty() && (m_Vel - m_Crew.GetLastVel()).MagnitudeIsGreaterThan(throwSpeed)) {
		m_Crew.EjectCrew(true);
	} else if (m_Status == DYING && !m_Crew.IsEmpty()) {
		m_Crew.EjectCrew(false);
	}

	UpdateGuns();
	m_Crew.UpdateBoarding(false, m_CanBeTaken && m_Crew.IsEmpty());
	UpdateAIRides();
	UpdateAIBoarding();

	m_Crew.SetLastVel(m_Vel);
	m_Crew.PlaceCrew();
	m_Crew.UpdatePassengerFire();
}

void ACShip::SeatCargo() {
	if (!m_SeatsCargo || m_HatchState != CLOSED || m_Inventory.empty() || m_Status == DYING || m_Status == DEAD) {
		return;
	}
	bool seated = false;
	for (auto item = m_Inventory.begin(); item != m_Inventory.end() && GetFreeSeat() >= 0;) {
		Actor* unit = dynamic_cast<Actor*>(*item);
		if (!unit || !dynamic_cast<AHuman*>(unit) || unit->IsInGroup("Brains")) {
			++item;
			continue;
		}
		unit->SetTeam(m_Team);
		unit->GetController()->SetInputMode(Controller::CIM_AI);
		unit->SetVel(Vector());
		m_Crew.SetOccupant(GetFreeSeat(), unit);
		item = m_Inventory.erase(item);
		seated = true;
	}
	// (Crewed, it isn't delivering anyone: it comes down and holds there for its gunners, rather than going straight home.)
	if (seated && m_AIMode == AIMODE_DELIVER) {
		SetAIMode(AIMODE_STAY);
	}
}

void ACShip::UpdateAIBoarding() {
	if (!m_OrderedBoardTimer.IsPastSimMS(250) || IsDead() || !Controller::AIMayAct()) {
		return;
	}
	m_OrderedBoardTimer.Reset();
	// Units sent to it (to follow it, as a move order onto it gives) get in when they get here, and stay in.
	for (Actor* unit: g_MovableMan.GetActorList()) {
		if (GetFreeSeat() < 0) {
			break;
		}
		// (Not those called over for a ride, UpdateAIRides: they get in as riders, keeping where they're going.)
		if (std::any_of(m_RideRecruits.begin(), m_RideRecruits.end(), [unit](const RideRecruit& recruit) { return recruit.ID == unit->GetUniqueID(); })) {
			continue;
		}
		if (unit->GetMOMoveTarget() == this && !unit->GetController()->IsPlayerControlled() && CanTakeIn(unit) && m_Crew.IsInReach(unit, 24.0F)) {
			unit->RemoveAIMOWaypoint(this);
			unit->ClearAIWaypoints();
			unit->SetAIMode(AIMODE_SENTRY);
			TakeSeat(unit, -1);
		}
	}
}

bool ACShip::OffersRides() const {
	if (IsVehicleLocked() || IsDead() || m_Status == DYING || m_Controller.IsPlayerControlled() || m_Crew.GetPlayerSeat() >= 0 || IsRamming() || m_HasCombatSpot || m_EngageID != 0 || NumberValueExists("NoShipRides")) {
		return false;
	}
	// (An empty ship that needs a pilot does nothing it is told: sent along with the units it stands by, its order is one it can't follow,
	// and its AI mode is whatever it was placed or bought with. It is free for a ride, the first in flying it.)
	if (Stranded()) {
		return true;
	}
	// (Given an order by the player, or a battle mode: it is theirs, ride or not. Nor an airlift's ship, which has its own passengers.)
	if (HasOrder()) {
		return false;
	}
	// (Busy with something of its own: a delivery, a trip home. A go-to is only its own ride's.)
	if (m_AIMode == AIMODE_DELIVER || m_AIMode == AIMODE_RETURN || m_AIMode == AIMODE_BRAINHUNT || m_AIMode == AIMODE_BOMB || m_AIMode == AIMODE_SCUTTLE || (m_AIMode == AIMODE_GOTO && m_RideStage == RideStage::None)) {
		return false;
	}
	// (A crewed ship's crew all its own: anyone in it that isn't a rider is its AI crew, who fly or man the guns for whoever rides.)
	return true;
}

bool ACShip::HasOrder() const {
	return m_StandingOrder.Kind != ORDER_NONE || m_StandingOrder.Hold || m_StandingOrder.HasPost || m_StandingOrder.Attack || m_StandingOrder.TargetID != 0;
}

int ACShip::RiderCount() const {
	int riders = 0;
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		riders += m_Crew.GetOccupant(seat) && m_Crew.IsRider(seat) ? 1 : 0;
	}
	return riders;
}

void ACShip::LetRidersOut() {
	for (int seat = GetSeatCount() - 1; seat >= 0; --seat) {
		if (m_Crew.GetOccupant(seat) && m_Crew.IsRider(seat)) {
			if (Actor* out = m_Crew.LetOut(seat, false)) {
				out->ResumeAIWaypoints();
			}
		}
	}
}

void ACShip::ReleaseRideRecruits() {
	for (const RideRecruit& recruit: m_RideRecruits) {
		if (Actor* unit = dynamic_cast<Actor*>(g_MovableMan.FindObjectByUniqueID(recruit.ID))) {
			unit->RemoveAIMOWaypoint(this);
		}
	}
	m_RideRecruits.clear();
}

bool ACShip::RiderTakesControls() {
	if (!m_NeedsPilot || HasPilot() || m_Status == DYING || m_Status == DEAD) {
		return false;
	}
	for (int seat = 1; seat < GetSeatCount(); ++seat) {
		if (Actor* unit = m_Crew.GetOccupant(seat); unit && m_Crew.IsRider(seat)) {
			m_Crew.SetOccupant(0, unit);
			m_Crew.SetOccupant(seat, nullptr);
			m_Crew.SetRider(0, true);
			return true;
		}
	}
	return false;
}

void ACShip::AddLoadedRideRecruit(const Actor* unit) {
	if (m_RideStage == RideStage::None) {
		m_RideGoal = unit->GetLastAIWaypoint();
		m_RideTeam = unit->GetTeam();
		m_RideHealth = GetHealth();
		m_RideWaitStarted = RiderCount() > 0;
		m_RideWaitTimer.Reset();
		SetRideStage(RideStage::Gathering);
	}
	m_RideRecruits.push_back({unit->GetUniqueID(), Timer()});
}

void ACShip::SetRideStage(RideStage stage) {
	m_RideStage = stage;
	m_RideStageTimer.Reset();
	m_RideSent = false;
	m_RideSends = 0;
}

void ACShip::UpdateAIRides() {
	if (IsDead() || m_Status == DYING || !Controller::AIMayAct()) {
		return;
	}
	const bool ridesOn = g_SettingsMan.AIRidesShips();
	const float radius = GetRadius();
	auto down = [this, radius]() { return GetAltitude(1000, 4) < radius * 0.9F && m_Vel.MagnitudeIsLessThan(3.0F); };
	// Inside the drop-off ring round where the riders are going (across from it, and not on ground far above or below it), and whether it's
	// low enough there for them to jump without a long fall.
	const float dropRing = g_SettingsMan.AIShipRideDropRing();
	auto inDropRing = [this, radius, dropRing]() {
		const Vector toGoal = g_SceneMan.ShortestDistance(m_Pos, m_RideGoal);
		return std::abs(toGoal.m_X) < dropRing && std::abs(toGoal.m_Y) < dropRing + radius * 2.0F;
	};
	auto lowEnoughToJump = [this, radius]() { return GetAltitude(1000, 4) < radius * 1.5F + 60.0F; };

	// Those called over get in once they're here, as riders keeping where they're going; or are let go if they've stopped coming (a new
	// order, dead, or too long on the way), the seats have filled, or it has gone.
	for (auto recruit = m_RideRecruits.begin(); recruit != m_RideRecruits.end();) {
		Actor* unit = dynamic_cast<Actor*>(g_MovableMan.FindObjectByUniqueID(recruit->ID));
		bool coming = unit && (unit->GetMOMoveTarget() == this || std::any_of(unit->GetWaypointList().begin(), unit->GetWaypointList().end(), [this](const std::pair<Vector, const MovableObject*>& waypoint) { return waypoint.second == this; }));
		bool keep = coming && ridesOn && m_RideStage == RideStage::Gathering && !unit->IsDead() && !unit->IsSetToDelete() && !unit->GetController()->IsPlayerControlled() && !recruit->Since.IsPastSimMS(45000) && GetFreeSeat() >= 0 && CanTakeIn(unit);
		if (keep && m_Crew.IsInReach(unit, 24.0F)) {
			unit->RemoveAIMOWaypoint(this);
			int seat = GetFreeSeat();
			if (TakeSeat(unit, seat)) {
				m_Crew.SetRider(seat, true);
				if (!m_RideWaitStarted) {
					m_RideWaitStarted = true;
					m_RideWaitTimer.Reset();
				}
			}
			recruit = m_RideRecruits.erase(recruit);
			continue;
		}
		if (!keep) {
			if (unit && coming) {
				unit->RemoveAIMOWaypoint(this);
			}
			recruit = m_RideRecruits.erase(recruit);
			continue;
		}
		++recruit;
	}

	// Taken over by a player (flown, sat in, or given an order): the ride is off. Those coming walk on, and those in it ride along as the
	// player's passengers, to be ordered out as they like.
	// (An order given to it while it stood empty, which it couldn't follow, was dropped when the ride began.)
	// (Or locked, the command bar's Lock: likewise.)
	const bool taken = m_Controller.IsPlayerControlled() || m_Crew.GetPlayerSeat() >= 0 || HasOrder() || IsVehicleLocked();
	if (taken) {
		if (m_RideStage != RideStage::None) {
			ReleaseRideRecruits();
			for (int seat = 0; seat < GetSeatCount(); ++seat) {
				m_Crew.SetRider(seat, false);
			}
			SetRideStage(RideStage::None);
		}
		return;
	}

	const int riders = RiderCount();
	// Riders in it with no ride under way (read from a saved game): it takes them on to where they're going.
	if (m_RideStage == RideStage::None && riders > 0) {
		for (int seat = 0; seat < GetSeatCount(); ++seat) {
			if (Actor* unit = m_Crew.GetOccupant(seat); unit && m_Crew.IsRider(seat)) {
				m_RideGoal = unit->GetLastAIWaypoint();
				m_RideTeam = unit->GetTeam();
				break;
			}
		}
		m_RideWaitStarted = true;
		m_RideWaitTimer.Reset();
		m_RideHealth = GetHealth();
		SetRideStage(RideStage::Gathering);
	}
	// With its pilot shot (or out), a rider takes the controls.
	if (m_RideStage != RideStage::None) {
		RiderTakesControls();
	}

	switch (m_RideStage) {
		case RideStage::Gathering: {
			// Rides turned off, or nobody (left) to take: those coming go back to walking, and any in it get out once it's down.
			if (!ridesOn || (riders == 0 && m_RideRecruits.empty())) {
				ReleaseRideRecruits();
				if (riders > 0) {
					m_RideUnloadAt = DropOffPoint(m_Pos);
					SetRideStage(RideStage::Unloading);
				} else {
					SetRideStage(RideStage::None);
				}
				break;
			}
			// Down by the ground for them to get in (a parked one already is; one with nobody to fly it can't come).
			if (!m_RideSent && CanFly() && AutopilotStarted() && !down()) {
				Vector ground = g_SceneMan.MovePointToGround(m_Pos, 0.0F, 4);
				m_RideSent = FlyTo(ground - Vector(0.0F, radius * 0.3F));
			}
			// Off once everyone it called is in, its seats are full, the wait since the first got in has run out, or it's been hit.
			const bool everyone = m_RideRecruits.empty();
			const bool full = GetFreeSeat() < 0;
			const bool waited = m_RideWaitStarted && m_RideWaitTimer.IsPastSimMS(static_cast<double>(g_SettingsMan.AIShipRideWait()) * 1000.0);
			const bool hit = GetHealth() < m_RideHealth - 1.0F;
			if (riders > 0 && (everyone || full || waited || hit)) {
				if (!CanFly()) {
					// (Nobody to fly it, and no rider can: a ship whose pilot's seat a rider can't take. They get out and walk.)
					if (m_RideStageTimer.IsPastSimMS(static_cast<double>(g_SettingsMan.AIShipRideWait()) * 1000.0 + 15000.0)) {
						ReleaseRideRecruits();
						m_RideUnloadAt = DropOffPoint(m_Pos);
						SetRideStage(RideStage::Unloading);
					}
					break;
				}
				ReleaseRideRecruits();
				SetRideStage(RideStage::Flying);
			}
			break;
		}
		case RideStage::Flying: {
			if (riders == 0) {
				SetRideStage(RideStage::None);
				break;
			}
			if (!CanFly()) {
				// Nobody to fly it: it sets itself down, and they get out there.
				m_RideUnloadAt = DropOffPoint(m_Pos);
				SetRideStage(RideStage::Unloading);
				break;
			}
			// Into the drop-off ring: out as soon as it's low enough, without slowing to settle on a spot; higher up, it comes down first.
			if (inDropRing()) {
				if (lowEnoughToJump()) {
					LetRidersOut();
					SetRideStage(RideStage::None);
				} else {
					m_RideUnloadAt = DropOffPoint(m_RideGoal);
					SetRideStage(RideStage::Unloading);
				}
				break;
			}
			// Sent there by its air route (again whenever it stopped short: its go-to ended in a hold); it gives up after a few tries, or two
			// minutes, and lets them out where it is.
			const bool stopped = m_RideSent && m_AIMode != AIMODE_GOTO;
			if (!m_RideSent || stopped) {
				if (m_RideSends >= 4 || m_RideStageTimer.IsPastSimMS(120000)) {
					m_RideUnloadAt = DropOffPoint(m_Pos);
					SetRideStage(RideStage::Unloading);
					break;
				}
				if (FlyRouteTo(m_RideGoal)) {
					m_RideSent = true;
					++m_RideSends;
				}
			}
			break;
		}
		case RideStage::Unloading: {
			if (riders == 0) {
				SetRideStage(RideStage::None);
				break;
			}
			if (CanFly() && AutopilotStarted() && !m_RideSent) {
				m_RideSent = FlyTo(m_RideUnloadAt);
			}
			// Low enough inside the drop-off ring (or settled wherever it set itself down, or it's taking too long): out.
			if ((inDropRing() && lowEnoughToJump()) || down() || m_RideStageTimer.IsPastSimMS(6000)) {
				LetRidersOut();
				SetRideStage(RideStage::None);
			}
			break;
		}
		default:
			break;
	}

	// Now and then, while it has room and is idle (or gathering), it looks round for units that want a ride, going near where it's going.
	if (!ridesOn || !m_RideScanTimer.IsPastSimMS(1000) || (m_RideStage != RideStage::None && m_RideStage != RideStage::Gathering)) {
		return;
	}
	m_RideScanTimer.Reset();
	if (!OffersRides() || (!g_SettingsMan.AIShipRideNoRoute() && !g_SettingsMan.AIShipRideLongWalk())) {
		return;
	}
	int room = 0;
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		room += m_Crew.GetOccupant(seat) ? 0 : 1;
	}
	room -= static_cast<int>(m_RideRecruits.size());
	if (room <= 0) {
		return;
	}
	Scene* scene = g_SceneMan.GetScene();
	if (!scene) {
		return;
	}
	bool gathering = m_RideStage == RideStage::Gathering;
	const float range = g_SettingsMan.AIShipRideRange();
	const float longWalk = g_SettingsMan.AIShipRideLongWalkDistance();
	// (Routes are searched on the spot: only a few a look, the nearest units first.)
	int searchesLeft = 3;
	std::vector<std::pair<float, Actor*>> candidates;
	for (Actor* unit: g_MovableMan.GetActorList()) {
		if (unit->GetController()->IsPlayerControlled() || unit->IsDead() || !CanTakeIn(unit) || unit->IsDiggingTo()) {
			continue;
		}
		// (All of a ride on one side: an empty ship anyone can take is taken by the first in, for theirs.)
		if ((gathering || !m_Crew.IsEmpty()) && unit->GetTeam() != (gathering ? m_RideTeam : m_Team)) {
			continue;
		}
		// (Not one already called over by a vehicle or ship, or sent to one.)
		const MovableObject* following = unit->GetMOMoveTarget();
		if ((following && (dynamic_cast<const AVehicle*>(following) || dynamic_cast<const ACShip*>(following))) ||
		    std::any_of(unit->GetWaypointList().begin(), unit->GetWaypointList().end(), [](const std::pair<Vector, const MovableObject*>& waypoint) { return waypoint.second && (dynamic_cast<const AVehicle*>(waypoint.second) || dynamic_cast<const ACShip*>(waypoint.second)); })) {
			continue;
		}
		Vector toShip = g_SceneMan.ShortestDistance(unit->GetPos(), m_Pos);
		if (toShip.MagnitudeIsGreaterThan(range)) {
			continue;
		}
		candidates.emplace_back(toShip.GetMagnitude(), unit);
	}
	std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	for (const auto& [distance, unit]: candidates) {
		if (room <= 0 || searchesLeft <= 0) {
			break;
		}
		// Why it wants a ride: no way there on foot, or a long way to walk (and the ship no further from there than it is).
		Vector goal;
		bool noRoute = g_SettingsMan.AIShipRideNoRoute() && unit->WantsShipRide(goal);
		if (!noRoute) {
			if (!g_SettingsMan.AIShipRideLongWalk() || unit->GetAIMode() != AIMODE_GOTO || unit->GetMOMoveTarget()) {
				continue;
			}
			goal = unit->GetLastAIWaypoint();
			float walk = g_SceneMan.ShortestDistance(unit->GetPos(), goal).GetMagnitude();
			// (A ship a little way back, behind them, still: up to a quarter of the walk further from there than they are.)
			if (walk < longWalk || g_SceneMan.ShortestDistance(m_Pos, goal).GetMagnitude() > walk + std::max(100.0F, walk * 0.25F)) {
				continue;
			}
		}
		// (Not one already near there: let out by a ship that couldn't get it closer, it would only be flown round again.)
		if (g_SceneMan.ShortestDistance(unit->GetPos(), goal).MagnitudeIsLessThan(400.0F)) {
			continue;
		}
		// Going near where the others it has called are going.
		if (gathering && g_SceneMan.ShortestDistance(m_RideGoal, goal).MagnitudeIsGreaterThan(300.0F)) {
			continue;
		}
		// It can walk to the ship, and (for the first of a ride) the ship can fly there.
		--searchesLeft;
		std::list<Vector> path;
		Vector shipGround = g_SceneMan.MovePointToGround(m_Pos, 0.0F, 4) - Vector(0.0F, 10.0F);
		float cost = scene->CalculatePath(unit->GetPathStart(), shipGround, path, unit->GetPathAgent(), static_cast<Activity::Teams>(unit->GetTeam()));
		if (cost < 0.0F || cost >= 100000.0F) {
			continue;
		}
		// (Planned from its flying height over where it stands: from a ship sat on the ground, the start had no room for its hull and the
		// plan failed, so a parked ship never offered a ride. A route that only gets part way still does, if it ends near enough to walk.)
		if (!gathering) {
			std::deque<Vector> route;
			bool reached = false;
			if (AirPath(LowPoint(m_Pos), LowPoint(goal), 0.0F, route, reached) && !reached && !route.empty() && g_SceneMan.ShortestDistance(route.back(), goal).MagnitudeIsGreaterThan(400.0F)) {
				continue;
			}
		}
		// Called over, keeping where it was going (put back for one that had stood down, its goal forgotten).
		unit->ForgetShipRide();
		if (g_SceneMan.ShortestDistance(unit->GetLastAIWaypoint(), goal).MagnitudeIsGreaterThan(20.0F)) {
			unit->ClearAIWaypoints();
			unit->AddAISceneWaypoint(goal);
		}
		unit->SetAIMode(AIMODE_GOTO);
		unit->AddAIMOWaypointFirst(this);
		m_RideRecruits.push_back({unit->GetUniqueID(), Timer()});
		--room;
		if (!gathering) {
			// (Any order it was given standing empty goes: from now on it is flown for the ride.)
			if (Stranded() && HasOrder()) {
				ClearStandingOrder();
			}
			gathering = true;
			m_RideGoal = goal;
			m_RideTeam = unit->GetTeam();
			m_RideWaitStarted = false;
			m_RideHealth = GetHealth();
			SetRideStage(RideStage::Gathering);
		}
	}
}

long ACShip::FindGunTarget(const Turret* turret) const {
	Vector pivot = turret->GetPos();
	float flip = turret->GetFlipFactor();
	float rotationFacing = Wrapped(m_Rotation.GetRadAngle()) * flip;
	long best = 0;
	float bestDistance = m_GunRange;
	for (const Actor* actor: g_MovableMan.GetActorList()) {
		if (actor == this || actor->GetTeam() == m_Team || actor->GetTeam() == Activity::NoTeam || actor->IsDead() || actor->IsSetToDelete() || actor->GetHealth() <= 0.0F) {
			continue;
		}
		Vector toTarget = g_SceneMan.ShortestDistance(pivot, actor->GetPos());
		float distance = toTarget.GetMagnitude();
		// (On the gun's side, and within its swing: a door gun can't shoot across the ship.)
		if (distance >= bestDistance || toTarget.m_X * flip <= 0.0F) {
			continue;
		}
		float angle = std::atan2(-toTarget.m_Y, std::abs(toTarget.m_X));
		if (std::abs(Wrapped(angle - rotationFacing)) > m_GunAimRange) {
			continue;
		}
		Vector blocked;
		if (g_SceneMan.CastStrengthRay(pivot, toTarget, 5.0F, blocked, 3)) {
			continue;
		}
		best = actor->GetUniqueID();
		bestDistance = distance;
	}
	return best;
}

void ACShip::UpdateGuns() {
	std::vector<Turret*> turrets;
	for (Attachable* attachable: m_Attachables) {
		if (Turret* turret = dynamic_cast<Turret*>(attachable)) {
			turrets.push_back(turret);
		}
	}
	// (A gun shot off takes its aim with it.)
	std::erase_if(m_Guns, [&turrets](const GunState& gun) { return std::none_of(turrets.begin(), turrets.end(), [&gun](const Turret* turret) { return turret->GetUniqueID() == gun.TurretID; }); });

	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	for (size_t index = 0; index < turrets.size(); ++index) {
		Turret* turret = turrets[index];
		auto found = std::find_if(m_Guns.begin(), m_Guns.end(), [turret](const GunState& gun) { return gun.TurretID == turret->GetUniqueID(); });
		if (found == m_Guns.end()) {
			m_Guns.push_back({turret->GetUniqueID()});
			found = m_Guns.end() - 1;
		}
		GunState& gun = *found;
		// The first gunner's seat mans the first gun, and so on.
		int seat = m_Crew.GetSeatOfGun(static_cast<int>(index));
		bool manned = seat >= 0 && m_Crew.GetOccupant(seat) && m_Status != DYING && m_Status != DEAD;
		float flip = turret->GetFlipFactor();
		float rotationFacing = Wrapped(m_Rotation.GetRadAngle()) * flip;
		bool fire = false;
		bool playerAims = manned && m_Controller.IsPlayerControlled() && GetControlSeat() == seat;
		if (playerAims) {
			// The player aims it, as a unit aims its gun: the aim keys swing it, the mouse or stick points it.
			float speed = m_Controller.IsState(AIM_SHARP) ? 0.6F : 1.8F;
			if (m_Controller.IsState(AIM_UP)) {
				gun.AimAngle += speed * deltaTime;
			} else if (m_Controller.IsState(AIM_DOWN)) {
				gun.AimAngle -= speed * deltaTime;
			}
			if (Vector analogAim = m_Controller.GetAnalogAim(); analogAim.MagnitudeIsGreaterThan(0.1F)) {
				// (Across the ship, the gun stays at the end of its swing on that side, up or down.)
				gun.AimAngle = std::atan2(-analogAim.m_Y, std::max(analogAim.m_X * flip, 0.0F));
			}
			fire = m_Controller.IsState(WEAPON_FIRE);
			gun.TargetID = 0;
		} else if (manned && Controller::AIMayAct()) {
			// A gunner looks round now and then for the nearest enemy in sight, swings round to it and fires once it is on it. (With the AI
			// paused, it holds its aim and fire.)
			const bool lookedRound = gun.TargetTimer.IsPastSimMS(400);
			if (lookedRound) {
				gun.TargetID = FindGunTarget(turret);
				gun.TargetTimer.Reset();
			}
			const Actor* target = gun.TargetID ? dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(gun.TargetID)) : nullptr;
			// The team hears of what its gunner has in its sights (AC-2), so those riding get out knowing it.
			if (lookedRound && target) {
				ThreatMemory::Report(this, target);
			}
			Vector suppressCentre;
			float suppressRadius = 0.0F;
			if (target && !target->IsDead()) {
				Vector toTarget = g_SceneMan.ShortestDistance(turret->GetPos(), target->GetPos() - Vector(0.0F, target->GetHeight() * 0.1F));
				float wanted = std::atan2(-toTarget.m_Y, std::max(toTarget.m_X * flip, 0.0F));
				gun.AimAngle += std::clamp(Wrapped(wanted - gun.AimAngle), -m_GunTurnSpeed * deltaTime, m_GunTurnSpeed * deltaTime);
				fire = std::abs(Wrapped(wanted - gun.AimAngle)) < 0.1F;
			} else if (GetSuppressZone(suppressCentre, suppressRadius)) {
				// Ordered to suppress a place, with nobody in sight to shoot: the gun on that side fires into it, about it at random, when it is
				// in range and within the gun's swing. No line of sight is wanted: the fire is to keep heads down there.
				gun.TargetID = 0;
				if (gun.SuppressTimer.IsPastSimMS(500)) {
					gun.SuppressTimer.Reset();
					gun.SuppressPoint = suppressCentre + Vector(RandomNum(-1.0F, 1.0F) * suppressRadius, RandomNum(-0.5F, 0.5F) * suppressRadius);
				}
				Vector toPoint = g_SceneMan.ShortestDistance(turret->GetPos(), gun.SuppressPoint);
				float angle = std::atan2(-toPoint.m_Y, std::abs(toPoint.m_X));
				if (toPoint.m_X * flip > 0.0F && toPoint.MagnitudeIsLessThan(m_GunRange) && std::abs(Wrapped(angle - rotationFacing)) <= m_GunAimRange) {
					gun.AimAngle += std::clamp(Wrapped(angle - gun.AimAngle), -m_GunTurnSpeed * deltaTime, m_GunTurnSpeed * deltaTime);
					fire = std::abs(Wrapped(angle - gun.AimAngle)) < 0.1F;
				}
			} else {
				gun.TargetID = 0;
			}
		} else if (!manned) {
			// Unmanned, it comes to rest pointing ahead.
			gun.AimAngle += std::clamp(rotationFacing - gun.AimAngle, -deltaTime, deltaTime);
			gun.TargetID = 0;
		}
		gun.AimAngle = std::clamp(gun.AimAngle, rotationFacing - m_GunAimRange, rotationFacing + m_GunAimRange);
		turret->SetMountedDeviceRotationOffset((gun.AimAngle * flip) - m_Rotation.GetRadAngle());
		// (The AI's gunner holds fire with a friend in the way, out to what it shoots at, or where it fires into.)
		if (fire && !playerAims) {
			float reach = m_GunRange;
			if (const MovableObject* target = gun.TargetID ? g_MovableMan.FindObjectByUniqueID(gun.TargetID) : nullptr) {
				reach = std::min(reach, g_SceneMan.ShortestDistance(turret->GetPos(), target->GetPos()).GetMagnitude());
			} else if (!gun.SuppressPoint.IsZero()) {
				reach = std::min(reach, g_SceneMan.ShortestDistance(turret->GetPos(), gun.SuppressPoint).GetMagnitude());
			}
			const Vector ray(std::cos(gun.AimAngle) * flip * reach, -std::sin(gun.AimAngle) * reach);
			fire = !FriendInLineOfFire(this, m_Team, turret->GetPos(), ray);
		}
		for (HeldDevice* device: turret->GetMountedDevices()) {
			if (fire) {
				device->Activate();
				if (device->IsEmpty()) {
					device->Reload();
				}
			} else {
				device->Deactivate();
			}
		}
	}
}

bool ACShip::HandlePieCommand(PieSliceType pieSliceType) {
	if (pieSliceType == PieSliceType::GetOut) {
		EjectSeat(m_Controller.IsPlayerControlled() ? GetControlSeat() : 0, false);
		return true;
	}
	if (pieSliceType == PieSliceType::ChangeSeat) {
		ChangeSeat();
		return true;
	}
	return ACDropShip::HandlePieCommand(pieSliceType);
}

void ACShip::GibThis(const Vector& impactImpulse, MovableObject* movableObjectToIgnore) {
	// Everyone is thrown clear of the wreck.
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		if (Actor* unit = EjectSeat(seat, true)) {
			unit->SetVel(unit->GetVel() + impactImpulse / std::max(GetMass(), 1.0F) * 0.5F);
		}
	}
	m_Crew.TipOutAllSlumped(true);
	ACDropShip::GibThis(impactImpulse, movableObjectToIgnore);
}

void ACShip::Draw(BITMAP* pTargetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const {
	// Everyone in it first, so the hull hides them where it covers them. (And on the MOID layer, so shots hit them where they show.)
	m_Crew.Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
	ACDropShip::Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
}

void ACShip::Draw(const Camera& camera) const {
	m_Crew.Draw(camera);
	ACDropShip::Draw(camera);
}

void ACShip::UpdateChildMOIDs(std::vector<MovableObject*>& MOIDIndex, MOID rootMOID, bool makeNewMOID) {
	ACDropShip::UpdateChildMOIDs(MOIDIndex, rootMOID, makeNewMOID);
	m_Crew.UpdateMOIDs(MOIDIndex);
}

void ACShip::GetMOIDs(std::vector<MOID>& MOIDs) const {
	ACDropShip::GetMOIDs(MOIDs);
	m_Crew.GetMOIDs(MOIDs);
}

void ACShip::DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos, int whichScreen, bool playerControlled) {
	ACDropShip::DrawHUD(pTargetBitmap, targetPos, whichScreen, playerControlled);

	Activity* activity = g_ActivityMan.GetActivity();
	GUIFont* font = g_FrameMan.GetSmallFont();
	if (!activity || !font || !m_HUDVisible) {
		return;
	}
	// The reticle and rounds of the gun the player mans: the first gunner's seat mans the first gun, and so on, as UpdateGuns has it.
	if (int seat = GetControlSeat(); seat >= 0 && IsGunnerSeat(seat) && m_Controller.IsPlayerControlled() && !m_Controller.IsState(PIE_MENU_ACTIVE) && activity->ScreenOfPlayer(m_Controller.GetPlayer()) == whichScreen) {
		int gunIndex = 0;
		for (const Attachable* attachable: m_Attachables) {
			if (const Turret* turret = dynamic_cast<const Turret*>(attachable)) {
				if (m_Crew.GetSeatOfGun(gunIndex++) == seat) {
					Vector drawPos = m_Pos - targetPos;
					if (!targetPos.IsZero()) {
						drawPos += g_SceneMan.GetWrapToScreen(drawPos, pTargetBitmap->w, pTargetBitmap->h);
					}
					turret->DrawGunnerHUD(pTargetBitmap, targetPos, drawPos, m_HUDStack);
					break;
				}
			}
		}
	}
	// With the AI paths shown: what ride it is giving, and how many are in and still coming.
	if (m_RideStage != RideStage::None && (s_ShowAIPaths == 1 || (s_ShowAIPaths == 2 && m_SandboxSelected))) {
		static const char* const stages[] = {"", "Ride: waiting", "Ride: flying", "Ride: letting out"};
		std::string text = std::string(stages[static_cast<int>(m_RideStage)]) + " " + std::to_string(RiderCount()) + " in";
		if (!m_RideRecruits.empty()) {
			text += ", " + std::to_string(m_RideRecruits.size()) + " coming";
		}
		Vector drawPos = m_Pos - targetPos;
		if (!targetPos.IsZero()) {
			drawPos += g_SceneMan.GetWrapToScreen(drawPos, pTargetBitmap->w, pTargetBitmap->h);
		}
		AllegroBitmap bitmap(pTargetBitmap);
		font->DrawAligned(&bitmap, drawPos.GetFloorIntX(), drawPos.GetFloorIntY() - static_cast<int>(m_CharHeight * 0.5F) - 26, text, GUIFont::Centre);
	}
	const char* hint = nullptr;
	if (Actor* boarder = m_Crew.GetBoarderInReach(); boarder && activity->ScreenOfPlayer(boarder->GetController()->GetPlayer()) == whichScreen) {
		hint = boarder->GetTeam() != m_Team ? "Pick up: take it" : "Pick up: get in";
	} else if (GetControlSeat() >= 0 && m_Controller.IsPlayerControlled() && activity->ScreenOfPlayer(m_Controller.GetPlayer()) == whichScreen && !m_Crew.GetBoardingTimer().IsPastSimMS(4000)) {
		int seat = GetControlSeat();
		if (seat == 0) {
			hint = "Pilot: fly   Pick up: get out";
		} else if (IsGunnerSeat(seat)) {
			hint = "Gunner: aim and fire   Pick up: get out";
		} else {
			hint = "Passenger   Pick up: get out";
		}
	} else if (m_NeedsPilot && !HasPilot() && m_Controller.IsPlayerControlled() && activity->ScreenOfPlayer(m_Controller.GetPlayer()) == whichScreen) {
		hint = "No pilot: it sets itself down";
	}
	if (hint) {
		Vector drawPos = m_Pos - targetPos;
		if (!targetPos.IsZero()) {
			drawPos += g_SceneMan.GetWrapToScreen(drawPos, pTargetBitmap->w, pTargetBitmap->h);
		}
		AllegroBitmap bitmap(pTargetBitmap);
		font->DrawAligned(&bitmap, drawPos.GetFloorIntX(), drawPos.GetFloorIntY() - static_cast<int>(m_CharHeight * 0.5F) - 14, hint, GUIFont::Centre);
	}
}

void ACShip::DrawHUD(const Camera& camera) {
	ACDropShip::DrawHUD(camera);
}
