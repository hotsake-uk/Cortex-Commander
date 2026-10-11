#include "ACShip.h"

#include "AEmitter.h"
#include "AHuman.h"
#include "ActivityMan.h"
#include "Controller.h"
#include "FrameMan.h"
#include "HeldDevice.h"
#include "MovableMan.h"
#include "PieSlice.h"
#include "SceneMan.h"
#include "Scene.h"
#include "SettingsMan.h"
#include "TimerMan.h"
#include "RTETools.h"
#include "Turret.h"

#include "GUI.h"
#include "AllegroBitmap.h"

#include "tracy/Tracy.hpp"

#include <algorithm>
#include <array>
#include <cmath>

using namespace RTE;

ConcreteClassInfo(ACShip, ACDropShip, 10);

namespace {
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
	m_FlownTimer.Reset();
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
	MatchProperty("NeedsPilot", { reader >> m_NeedsPilot; });
	MatchProperty("CanBeTaken", { reader >> m_CanBeTaken; });
	MatchProperty("SeatsCargo", { reader >> m_SeatsCargo; });
	MatchProperty("CrewThrowSpeed", { reader >> m_CrewThrowSpeed; });
	MatchProperty("GunRange", { reader >> m_GunRange; });
	MatchProperty("GunTurnSpeed", { reader >> m_GunTurnSpeed; });
	MatchProperty("GunAimRange", { reader >> m_GunAimRange; });
	MatchProperty("FlightSpeed", { reader >> m_FlightSpeed; });
	MatchProperty("ClimbSpeed", { reader >> m_ClimbSpeed; });

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
	writer.NewPropertyWithValue("NeedsPilot", m_NeedsPilot);
	writer.NewPropertyWithValue("CanBeTaken", m_CanBeTaken);
	writer.NewPropertyWithValue("SeatsCargo", m_SeatsCargo);
	writer.NewPropertyWithValue("CrewThrowSpeed", m_CrewThrowSpeed);
	writer.NewPropertyWithValue("GunRange", m_GunRange);
	writer.NewPropertyWithValue("GunTurnSpeed", m_GunTurnSpeed);
	writer.NewPropertyWithValue("GunAimRange", m_GunAimRange);
	writer.NewPropertyWithValue("FlightSpeed", m_FlightSpeed);
	writer.NewPropertyWithValue("ClimbSpeed", m_ClimbSpeed);

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

bool ACShip::FlyRouteTo(const Vector& spot) {
	m_EnginesOff = false;
	if (!CanFly() || !AutopilotStarted()) {
		m_Route.clear();
		return false;
	}
	const SettingsMan::DirectOrderTuning& tuning = g_SettingsMan.DirectOrders();
	const float direct = m_StandingOrder.Direct;
	const float care = std::clamp(-direct, 0.0F, 1.0F);
	const float radius = GetRadius();
	// A cautious order flies higher, up to half as high again over the ground, further from small arms below.
	const float extra = AutopilotHoverHeight() * 0.5F * care;
	const Vector goal = AutopilotHoverPoint(spot, extra);
	// (Sent again where it is already going, as the standing orders do: the route it has goes on.)
	if (!m_Route.empty() && m_AIMode == AIMODE_GOTO && g_SceneMan.ShortestDistance(m_Route.back(), goal).MagnitudeIsLessThan(24.0F)) {
		return true;
	}
	m_Route.clear();
	// Open air between two points for its body: along its middle and a little above and below.
	auto clearLine = [radius](const Vector& from, const Vector& to) {
		Vector way = g_SceneMan.ShortestDistance(from, to);
		Vector blocked;
		for (float dy: {-radius * 0.5F, 0.0F, radius * 0.5F}) {
			Vector start = from + Vector(0.0F, dy);
			g_SceneMan.WrapPosition(start);
			if (g_SceneMan.CastStrengthRay(start, way, 5.0F, blocked, 4)) {
				return false;
			}
		}
		return true;
	};
	Scene* scene = g_SceneMan.GetScene();
	// Full risk, or open air all the way and no call to keep clear of anyone: straight there, as it always flew.
	if (!scene || direct >= tuning.StraightFlightRisk || (care <= 0.0F && clearLine(m_Pos, goal))) {
		return AutopilotGoTo(goal);
	}
	// No call to keep clear of anyone: through the air, over what is in the way, wherever the air lets it.
	if (care <= 0.0F && AirRoute(m_Pos, goal, m_Route)) {
		return AutopilotGoTo(m_Route.front());
	}
	m_Route.clear();
	// The route a unit would be given over the ground, from the ground under it to the ground under the place (with the care for enemies of
	// a cautious order, GetPathAgent), is what it flies over. None to be had: straight there, as before.
	std::list<Vector> path;
	Vector start = g_SceneMan.MovePointToGround(m_Pos, 20, 6);
	Vector end = g_SceneMan.MovePointToGround(spot, 20, 6);
	if (scene->CalculatePath(start, end, path, GetPathAgent(), static_cast<Activity::Teams>(m_Team)) < 0.0F || path.size() < 2) {
		return AutopilotGoTo(goal);
	}
	// Each point raised to its hover height over the ground there, as far as the room over it lets it.
	const float want = AutopilotHoverHeight() + extra;
	std::vector<Vector> raised;
	raised.reserve(path.size());
	for (const Vector& node: path) {
		float room = 0.0F;
		while (room < want) {
			int y = static_cast<int>(node.m_Y - room - 8.0F);
			if (y < 0 || g_SceneMan.GetTerrMatter(static_cast<int>(node.m_X), y) != MaterialColorKeys::g_MaterialAir) {
				break;
			}
			room += 8.0F;
		}
		raised.emplace_back(node.m_X, std::max(node.m_Y - std::max(room - radius * 0.5F, 0.0F), radius));
	}
	// And straight on past the points it can see past: from each, to the furthest a little way on with open air between. (A cautious order
	// looks less far ahead, so the way round enemies isn't cut across.)
	const size_t lookAhead = care > 0.0F ? 6 : 40;
	Vector from = m_Pos;
	for (size_t i = 0; i < raised.size();) {
		size_t furthest = i;
		for (size_t j = std::min(i + lookAhead, raised.size() - 1); j > i; --j) {
			if (clearLine(from, raised[j])) {
				furthest = j;
				break;
			}
		}
		m_Route.push_back(raised[furthest]);
		from = raised[furthest];
		i = furthest + 1;
	}
	// Last, the place itself.
	if (!m_Route.empty() && clearLine(m_Route.back(), goal)) {
		m_Route.back() = goal;
	} else {
		m_Route.push_back(goal);
	}
	return AutopilotGoTo(m_Route.front());
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
	if (!m_CombatTimer.IsPastSimMS(250)) {
		return;
	}
	m_CombatTimer.Reset();
	auto enemyByID = [this](long id) -> const Actor* {
		const Actor* actor = id ? dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(id)) : nullptr;
		return actor && !actor->IsDead() && !actor->IsSetToDelete() && actor->GetTeam() != m_Team ? actor : nullptr;
	};
	Vector suppressCentre;
	float suppressRadius = 0.0F;
	const bool holding = m_AIMode == AIMODE_SENTRY || m_AIMode == AIMODE_STAY || (m_AIMode == AIMODE_GOTO && m_HasCombatSpot);
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
	const float direct = std::clamp(m_StandingOrder.Direct, -1.0F, 1.0F);
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
		// one), at its distance, nearer, then further, each at its hover height and higher.
		int firstSide = search && m_CombatSearching ? -side : side;
		Vector fallback;
		bool found = false;
		for (int s: {firstSide, -firstSide}) {
			for (float distance: {share, share * 0.7F, std::min(share * 1.3F, 0.8F)}) {
				for (float lift: {0.0F, -60.0F, -120.0F}) {
					Vector ground = keepNearPost(enemyPos + Vector(static_cast<float>(s) * distance * range, 0.0F));
					Vector candidate = AutopilotHoverPoint(ground) + Vector(0.0F, lift);
					candidate.m_Y = std::max(candidate.m_Y, radius);
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
	Vector blocked;
	if (!g_SceneMan.CastStrengthRay(m_Pos, g_SceneMan.ShortestDistance(m_Pos, spot, wraps), 5.0F, blocked, 4)) {
		m_Route.clear();
		AutopilotGoTo(spot);
	} else {
		FlyRouteTo(spot);
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
	// Landed with its engines off: it stays down till sent somewhere (a go-to, however given).
	if (m_EnginesOff) {
		if (m_AIMode != AIMODE_GOTO) {
			m_Route.clear();
			return;
		}
		m_EnginesOff = false;
	}
	UpdateCraftAvoidance();
	UpdateCombat();
	// Along its route (FlyRouteTo): on to the next point once near the one it is flying to. Any other order (it holds, or is sent somewhere
	// else) or the player flying it ends the route.
	if (!m_Route.empty()) {
		if (m_Controller.IsPlayerControlled() || m_AIMode != AIMODE_GOTO) {
			m_Route.clear();
		} else if (g_SceneMan.ShortestDistance(m_Pos, m_Route.front()).MagnitudeIsLessThan(std::max(GetRadius(), 40.0F))) {
			m_Route.pop_front();
			if (!m_Route.empty()) {
				AutopilotGoTo(m_Route.front());
			}
		}
	}
	// A ship stays in the fight: with nothing (more) to deliver it holds where it is rather than flying home as a drop ship does. Only an
	// order to go home (RETURN) sends it off.
	if (m_AIMode == AIMODE_DELIVER && IsInventoryEmpty()) {
		SetAIMode(AIMODE_STAY);
	}
	if (m_AIMode == AIMODE_SCUTTLE) {
		GibThis();
		return;
	}
	UpdateFlight();
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
			// Its cargo let out at its hover height over the ground under it, or, just come in (bought, from orbit), waiting there: the only
			// times it goes down to it by itself.
			m_HoldPoint = AutopilotHoverPoint(Vector(m_Pos.m_X, std::max(m_Pos.m_Y, radius)));
			m_HasHoldPoint = true;
		} else {
			// Any other: it holds where it is, at the height it is (in the scene).
			m_HoldPoint = Vector(m_Pos.m_X, std::max(m_Pos.m_Y, radius * 2.0F));
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
				ai.waypoint = AutopilotHoverPoint(m_MovePath.back());
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

void ACShip::SteerTo(const Vector& target, bool stop) {
	const bool wraps = g_SceneMan.SceneWrapsX();
	const float radius = GetRadius();
	// How far over the ground it keeps on the way: its body and a little more.
	const float clearance = std::max(radius * 1.2F, 30.0F);
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
		for (float side: {-0.8F, 0.0F, 0.8F}) {
			const float x = m_Pos.m_X + way * along + side * radius;
			floor = std::min(floor, GroundTopAt(x, lineY + clearance) - clearance);
		}
	}
	if (aim.m_Y > floor) {
		aim.m_Y = floor;
		toAim = g_SceneMan.ShortestDistance(m_Pos, aim, wraps);
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
	const float stickX = std::clamp((wantX - m_Vel.m_X) * 0.35F, -1.0F, 1.0F);
	const float stickY = std::clamp((wantY + 0.5F * (wantY - m_Vel.m_Y)) / 7.5F, -1.0F, 1.0F);
	m_Controller.SetState(MOVE_UP, false);
	m_Controller.SetState(MOVE_DOWN, false);
	m_Controller.SetAnalogMove(Vector(stickX, stickY));
}

bool ACShip::AirRoute(const Vector& from, const Vector& to, std::deque<Vector>& route) const {
	route.clear();
	const float radius = GetRadius();
	const float clearance = std::max(radius * 1.2F, 30.0F);
	const Vector way = g_SceneMan.ShortestDistance(from, to, g_SceneMan.SceneWrapsX());
	auto clearLine = [radius](const Vector& a, const Vector& b) {
		Vector line = g_SceneMan.ShortestDistance(a, b);
		Vector blocked;
		for (float dy: {-radius * 0.5F, 0.0F, radius * 0.5F}) {
			Vector start = a + Vector(0.0F, dy);
			g_SceneMan.WrapPosition(start);
			if (g_SceneMan.CastStrengthRay(start, line, 5.0F, blocked, 4)) {
				return false;
			}
		}
		return true;
	};
	// The straight line's points a little apart, each raised to its clearance over the highest ground there across its hull.
	const int steps = std::max(1, static_cast<int>(std::abs(way.m_X) / 32.0F));
	std::vector<Vector> points;
	points.reserve(steps + 1);
	for (int i = 1; i < steps; ++i) {
		const float share = static_cast<float>(i) / static_cast<float>(steps);
		Vector point = from + way * share;
		float top = FLT_MAX;
		for (float side: {-0.8F, 0.0F, 0.8F}) {
			top = std::min(top, GroundTopAt(point.m_X + side * radius, point.m_Y + clearance));
		}
		point.m_Y = std::max(std::min(point.m_Y, top - clearance), radius);
		g_SceneMan.WrapPosition(point);
		points.push_back(point);
	}
	points.push_back(to);
	// Cut to as few as have open air between, on from each to the furthest of the next forty it can reach.
	Vector at = from;
	for (size_t i = 0; i < points.size();) {
		size_t furthest = points.size();
		for (size_t j = std::min(i + 40, points.size() - 1) + 1; j-- > i;) {
			if (clearLine(at, points[j])) {
				furthest = j;
				break;
			}
		}
		if (furthest == points.size()) {
			route.clear();
			return false;
		}
		route.push_back(points[furthest]);
		at = points[furthest];
		i = furthest + 1;
	}
	return !route.empty();
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

void ACShip::Update() {
	ZoneScoped;

	// Going to orbit with its crew: they go with it, in its hold (so they're counted and kept, as whoever rides in a craft's hold is).
	if (m_CanEnterOrbit && (m_Pos.m_Y < -m_CharHeight || m_Pos.m_Y > g_SceneMan.GetSceneHeight() + m_CharHeight) && !m_Crew.IsEmpty()) {
		for (Actor* unit: m_Crew.TakeAllCrew()) {
			AddInventoryItem(unit);
		}
	}

	ACDropShip::Update();

	SeatCargo();
	m_Crew.UpdateSeatedCrew();

	// A knock hard enough in one update (a crash into a wall, a heavy hit) throws everyone out; going down, they bail out.
	if (m_CrewThrowSpeed > 0.0F && !m_Crew.IsEmpty() && (m_Vel - m_Crew.GetLastVel()).MagnitudeIsGreaterThan(m_CrewThrowSpeed)) {
		m_Crew.EjectCrew(true);
	} else if (m_Status == DYING && !m_Crew.IsEmpty()) {
		m_Crew.EjectCrew(false);
	}

	UpdateGuns();
	m_Crew.UpdateBoarding(false, m_CanBeTaken && m_Crew.IsEmpty());
	UpdateAIBoarding();

	m_Crew.SetLastVel(m_Vel);
	m_Crew.PlaceCrew();
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
	if (!m_OrderedBoardTimer.IsPastSimMS(250) || IsDead() || Controller::IsAIPaused()) {
		return;
	}
	m_OrderedBoardTimer.Reset();
	// Units sent to it (to follow it, as a move order onto it gives) get in when they get here, and stay in.
	for (Actor* unit: g_MovableMan.GetActorList()) {
		if (GetFreeSeat() < 0) {
			break;
		}
		if (unit->GetMOMoveTarget() == this && !unit->GetController()->IsPlayerControlled() && CanTakeIn(unit) && m_Crew.IsInReach(unit, 24.0F)) {
			unit->RemoveAIMOWaypoint(this);
			unit->ClearAIWaypoints();
			unit->SetAIMode(AIMODE_SENTRY);
			TakeSeat(unit, -1);
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
		} else if (manned) {
			// A gunner looks round now and then for the nearest enemy in sight, swings round to it and fires once it is on it.
			if (gun.TargetTimer.IsPastSimMS(400)) {
				gun.TargetID = FindGunTarget(turret);
				gun.TargetTimer.Reset();
			}
			const Actor* target = gun.TargetID ? dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(gun.TargetID)) : nullptr;
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
		} else {
			// Unmanned, it comes to rest pointing ahead.
			gun.AimAngle += std::clamp(rotationFacing - gun.AimAngle, -deltaTime, deltaTime);
			gun.TargetID = 0;
		}
		gun.AimAngle = std::clamp(gun.AimAngle, rotationFacing - m_GunAimRange, rotationFacing + m_GunAimRange);
		turret->SetMountedDeviceRotationOffset((gun.AimAngle * flip) - m_Rotation.GetRadAngle());
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
