// The route-follower: how a humanoid under AI gets along the route the pathfinder gave it (AHuman::MoveAlongRoute). It owns every leg
// of the way, walks, crawls, drops, flights, ladders and doors, with one view of the route and one hand on the jet, where the movement
// script it replaces (SharedBehaviors.GoToWpt) had grown into a dozen controllers handing the unit between them, and every hand-over was
// a stutter of the jet or a turn in the air. Flights are flown by AHuman::PilotFlight. The script keeps its hook: it calls this each
// tick from a coroutine of its own (SharedBehaviors.GoToRoute), and a mod that replaces that keeps working.
#include "SoundContainer.h"
#include "AHuman.h"
#include "ActorWater.h"
#include "ADoor.h"
#include "ACraft.h"
#include "ACrab.h"
#include "SoundContainer.h"
#include "AEJetpack.h"
#include "AtomGroup.h"
#include "Arm.h"
#include "Leg.h"
#include "ConsoleMan.h"
#include "TimerMan.h"
#include "MovableMan.h"
#include "SceneMan.h"
#include "SettingsMan.h"
#include "Scene.h"
#include "PresetMan.h"
#include "Activity.h"
#include "PrimitiveMan.h"
#include "FluidSim.h"

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

	/// The Ladder material's index (the bunkers' rungs; see Materials.ini), 0 when there is none.
	unsigned char LadderMaterialID() {
		static int s_Ladder = -1;
		if (s_Ladder < 0) {
			const Material* ladder = g_SceneMan.GetMaterial("Ladder");
			s_Ladder = ladder ? ladder->GetIndex() : 0;
		}
		return static_cast<unsigned char>(s_Ladder);
	}

	/// Whether terrain of a material is something a walking body goes through as it comes, as the path grid has it (PathFinder::Open and
	/// WalkMaterialCost): grass, foliage, ash, at an integrity of 5 or under; not a liquid.
	bool WalkedThrough(unsigned char id) {
		const Material* material = g_SceneMan.GetMaterialFromID(id);
		return material && material->GetIntegrity() <= 5.0F && material->GetBehaviour().Flows != 1;
	}

	/// Solid for a climber: terrain that isn't air, a liquid, nor the ladder's own rungs. (Liquid pooled over the rungs read as a wall beside
	/// them or a floor under the feet, and the climb was refused or stopped in a flooded shaft.)
	bool SolidNotLadder(float x, float y) {
		unsigned char id = g_SceneMan.GetTerrMatter(static_cast<int>(x), static_cast<int>(y));
		return id != MaterialColorKeys::g_MaterialAir && id != LadderMaterialID() && !FluidSim::IsLiquid(id);
	}
} // namespace

// The base game's background ladders: a script node at the middle of each 24 px piece holds a humanoid in front of it, and moves it up when
// it aims up and presses up, down likewise. The nodes are found among the scene's particles now and then and kept.
std::vector<Vector> AHuman::s_LadderNodes;
double AHuman::s_LadderNodesSimTimeMS = -1.0;

void AHuman::RefreshLadderNodes() {
	// (Was done by whichever AI thread found the nodes out of date, under a lock every ladder lookup on every thread took. Here, on the main
	// thread before the AI runs, the lookups need none. A sim clock gone back means a new game.)
	double now = g_TimerMan.GetSimTimeMS();
	if (s_LadderNodesSimTimeMS >= 0.0 && now >= s_LadderNodesSimTimeMS && now - s_LadderNodesSimTimeMS <= 4000.0) {
		return;
	}
	s_LadderNodesSimTimeMS = now;
	static const std::string c_LadderNodeName = "Background Ladder Node";
	s_LadderNodes.clear();
	// Pinned first, as most particles aren't, then the name.
	for (const MovableObject* particle: g_MovableMan.GetParticleList()) {
		if (particle && particle->GetPinStrength() > 0.0F && particle->GetPresetName() == c_LadderNodeName) {
			s_LadderNodes.push_back(particle->GetPos());
		}
	}
}

std::optional<Vector> AHuman::LadderNear(const Vector& point, float reachX, float reachY) {
	// (Called from the AI's threads, several units at once: read only, refreshed by RefreshLadderNodes while none of them runs.)
	std::optional<Vector> best;
	float bestDistance = std::numeric_limits<float>::max();
	for (const Vector& node: s_LadderNodes) {
		Vector off = Towards(point, node);
		if (std::abs(off.m_X) <= reachX && std::abs(off.m_Y) <= reachY && off.GetMagnitude() < bestDistance) {
			best = node;
			bestDistance = off.GetMagnitude();
		}
	}
	return best;
}

// ---------------------------------------------------------------- The AI's motor

void AHuman::SetAIStance(int stance, float milliseconds) {
	m_AIStance = std::clamp(stance, 0, 2);
	m_AIStanceMS = std::max(milliseconds, 0.0F);
	m_AIStanceTimer.Reset();
}

void AHuman::TacticalMoveTo(const Vector& place, float milliseconds) {
	m_Tactical.active = true;
	m_Tactical.place = place;
	m_Tactical.limitMS = std::max(milliseconds, 100.0F);
	m_Tactical.timer.Reset();
}

void AHuman::UpdateAIMotor() {
	if (m_Controller.GetInputMode() != Controller::CIM_AI || m_Status == DYING || m_Status == DEAD) {
		return;
	}
	Controller& ctrl = m_Controller;
	const float h = m_CharHeight;
	const float feet = m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : h * 0.2F;

	// The stance held, for its while.
	if (m_AIStance != 0 && m_AIStanceTimer.IsPastSimMS(m_AIStanceMS)) {
		m_AIStance = 0;
	}
	// A body lying down may not jet or leap: while the route-follower is taking off (settling, at a leap's take-off, or pressing the jet or
	// the leap this frame), a prone stance gives way, and the jet and the leap wait the moment until the body is up. (Pressed prone every frame, a
	// move-order unit given ShootTarget's 2.5 s prone stance lit its jet lying down, or stood at the edge of a gap the leap refused.)
	bool takingOff = m_Mover.settling || (m_Mover.standUp && !m_Mover.standUpTimer.IsPastSimMS(250)) || ctrl.IsState(BODY_JUMPSTART) || ctrl.IsState(BODY_JUMP) || ctrl.IsState(BODY_LEAP);
	if (takingOff && m_ProneState != NOTPRONE) {
		ctrl.SetState(BODY_JUMPSTART, false);
		ctrl.SetState(BODY_JUMP, false);
		ctrl.SetState(BODY_LEAP, false);
		ctrl.SetState(BODY_PRONE, false);
	}
	if (m_AIStance == 2 && m_Status == STABLE && !m_Ladder.active && !takingOff) {
		ctrl.SetState(BODY_PRONE, true);
	} else if (m_AIStance == 1 && m_Status == STABLE && !m_Ladder.active) {
		ctrl.SetState(BODY_CROUCH, true);
	}

	// The tactical move: walked (or crawled) along this floor to the place.
	if (m_Tactical.active) {
		float dx = Towards(m_Pos, m_Tactical.place).m_X;
		const char* ended = nullptr;
		if (std::abs(dx) <= 6.0F) {
			ended = "there";
		} else if (m_Tactical.timer.IsPastSimMS(m_Tactical.limitMS)) {
			ended = "out of time";
		} else if (m_Status != STABLE || m_Ladder.active) {
			ended = "not on its feet";
		} else {
			float floorHere = FloorUnder(m_Pos, h * 0.5F + h * 0.33F);
			float floorY = floorHere >= 0.0F ? std::min(floorHere, m_Pos.m_Y + feet) : m_Pos.m_Y + feet;
			float standing = m_AIStance == 2 ? std::max(12.0F, h * 0.24F) : std::max(16.0F, h * 0.44F);
			Sensed sensed = SenseAhead(dx < 0.0F ? -1.0F : 1.0F, floorY, standing);
			if (sensed.wall && sensed.distance < std::abs(dx)) {
				ended = "a wall in the way";
			} else if (FloorUnder(m_Pos + Vector(dx < 0.0F ? -h * 0.3F : h * 0.3F, 0.0F), h * 0.9F) < 0.0F) {
				ended = "an edge in the way";
			}
		}
		if (ended) {
			m_Tactical.active = false;
			MoverTrace(std::string("tactical move ended: ") + ended);
		} else {
			ctrl.SetState(dx < 0.0F ? MOVE_RIGHT : MOVE_LEFT, false);
			ctrl.SetState(dx < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
		}
	}

	// A fall braked, off a route's flight (the pilot brakes those): falling with the floor coming up inside the jet's stop, the jet lit
	// straight up, leant against any drift. (The script did this, and lit the jet over the pilot's head on the route's flights too.)
	// Not while the route-follower is flying the fall itself (in the air with no flight, PilotFlight for the point, since the last AI update
	// or the one before): its safe-fall plan lets the body drop and lights the jet late, and this rewrote its jet key and lean every frame.
	const bool standardJet = m_pJetpack && m_pJetpack->IsAttached() && m_pJetpack->GetJetpackType() == AEJetpack::JetpackType::Standard;
	const bool fallPiloted = m_Mover.pilotedFallTick >= 0 && g_TimerMan.GetSimUpdateCount() - m_Mover.pilotedFallTick <= static_cast<long long>(std::max(1, g_SettingsMan.GetAIUpdateInterval()) * 2);
	if (standardJet && !fallPiloted && !m_Mover.flight.active && !m_Ladder.active && m_Vel.m_Y > 6.0F && m_pJetpack->GetJetTimeLeft() > 0.0F) {
		float accel = JetAccelAtFuel(m_pJetpack->GetJetTimeLeft()) - g_SceneMan.GetGlobalAcc().m_Y * c_PPM;
		float speed = m_Vel.m_Y * c_PPM;
		float stop = accel > 1.0F ? speed * speed / (2.0F * accel) : h * 6.0F;
		if (FloorUnder(m_Pos + Vector(0.0F, feet), stop * 1.3F + h * 0.4F) >= 0.0F) {
			ctrl.SetState(BODY_JUMP, true);
			ctrl.SetAnalogMove(Vector(std::clamp(-m_Vel.m_X * 0.3F, -0.6F, 0.6F), -1.0F));
		}
	}
}

// ---------------------------------------------------------------- The leap

float AHuman::GetLegJumpHeight() const {
	if (m_LegJumpHeight >= 0.0F) {
		return m_LegJumpHeight;
	}
	// A little over half the standing body (0.44 of the character height): a soldier clears a knee-high crate or a node-wide gap at a run.
	return std::max(16.0F, m_CharHeight * 0.44F) * 0.55F;
}

bool AHuman::CanLeap() const {
	if (m_Status != STABLE || m_Leaping || m_Mantling || m_GettingUp || m_Ladder.active || m_ProneState != NOTPRONE || (!m_pFGLeg && !m_pBGLeg) || GetLegJumpHeight() <= 0.0F) {
		return false;
	}
	// (A moment after landing before the next: the legs gather.)
	if (!m_LeapTimer.IsPastSimMS(250)) {
		return false;
	}
	const float h = m_CharHeight;
	const float feet = m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : h * 0.2F;
	return std::abs(m_Vel.m_Y) < 2.5F && FloorUnder(m_Pos, feet + 6.0F) >= 0.0F;
}

void AHuman::UpdateLeap() {
	Controller& ctrl = m_Controller;
	const float h = m_CharHeight;
	const float feet = m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : h * 0.2F;
	if (m_Leaping) {
		// Down again (on the floor and no longer rising, after the push has had a moment), or knocked over, climbing, mantling: done.
		bool landed = m_LeapTimer.IsPastSimMS(150) && m_Vel.m_Y >= -0.5F && FloorUnder(m_Pos, feet + 4.0F) >= 0.0F;
		if (landed || m_Status != STABLE || m_Ladder.active || m_Mantling) {
			m_Leaping = false;
			m_LeapTimer.Reset();
			if (m_MovementState == JUMP && !(m_pJetpack && m_pJetpack->IsEmitting())) {
				m_MovementState = STAND;
			}
		} else {
			m_MovementState = JUMP;
		}
		return;
	}
	if (!ctrl.IsState(BODY_LEAP) || !CanLeap()) {
		return;
	}
	// The push: up at the speed that rises the leap's height against gravity, and forward with a move key held (at least the leap's
	// speed, more if already running), else the speed it had. Momentum, not a scripted path: the body flies, lands and collides as ever.
	float gravity = std::max(0.1F, g_SceneMan.GetGlobalAcc().m_Y);
	float rise = std::sqrt(2.0F * gravity * GetLegJumpHeight() * c_MPP);
	float direction = ctrl.IsState(MOVE_RIGHT) ? 1.0F : (ctrl.IsState(MOVE_LEFT) ? -1.0F : 0.0F);
	float across = direction != 0.0F ? direction * std::max(m_LegJumpSpeed, m_Vel.m_X * direction) : m_Vel.m_X;
	// The AI leaps at the leap's own speed, the speed the path grid checked the arc at (PathFinder::LeapFits, PathAgent::LeapSpeed). At a
	// run's speed, as the route-follower runs, it flew further than checked: into the wall at the back of a two-node ledge, or past a
	// ledge one node wide. (A player's leap keeps its run-up.)
	if (direction != 0.0F && m_Controller.GetInputMode() == Controller::CIM_AI) {
		across = direction * m_LegJumpSpeed;
	}
	m_Vel.SetXY(across, std::min(m_Vel.m_Y, 0.0F) - rise);
	m_Leaping = true;
	m_LeapTimer.Reset();
	m_MovementState = JUMP;
	m_Paths[FGROUND][JUMP].Restart();
	m_Paths[BGROUND][JUMP].Restart();
	// (Off the floor cleanly: the deep check the jet's burst asks for too, so the feet don't catch on the ground they leave.)
	ForceDeepCheck();
	MoverTrace("leap");
}

// ---------------------------------------------------------------- The walk's sense of what is ahead

AHuman::Sensed AHuman::SenseAhead(float direction, float floorY, float standing) const {
	Sensed sensed;
	const float h = m_CharHeight;
	float bodyWidth = static_cast<float>(GetSpriteWidth());
	if (m_pHead) {
		bodyWidth = std::max(bodyWidth, static_cast<float>(m_pHead->GetSpriteWidth()));
	}
	const float halfWidth = std::clamp(bodyWidth * 0.5F + 1.0F, 5.0F, 16.0F);
	const float reach = h * 0.35F;
	const float crawl = std::max(12.0F, h * 0.24F);
	// (A plant ahead is walked through, not a wall or a step: sensed as one, every bush got a hop of the jet, or a leap from short of it.)
	auto blocks = [](float x, float y) {
		unsigned char id = g_SceneMan.GetTerrMatter(static_cast<int>(x), static_cast<int>(y));
		return id != MaterialColorKeys::g_MaterialAir && id != LadderMaterialID() && id != MaterialColorKeys::g_MaterialDoor && !WalkedThrough(id);
	};
	for (float d = halfWidth; d <= halfWidth + reach; d += 2.0F) {
		float x = m_Pos.m_X + direction * d;
		// What stands up from the floor here, and the first thing over it up to the head's top.
		float rise = 0.0F;
		while (rise < standing + 2.0F && blocks(x, floorY - 2.0F - rise)) {
			rise += 1.0F;
		}
		float over = -1.0F;
		for (float y = floorY - 2.0F - rise - 1.0F; y >= floorY - standing - 1.0F; y -= 2.0F) {
			if (blocks(x, y)) {
				over = floorY - y;
				break;
			}
		}
		if (rise <= 0.0F && over < 0.0F) {
			continue;
		}
		sensed.any = true;
		sensed.distance = d;
		sensed.rise = rise;
		if (rise >= standing - 2.0F) {
			sensed.wall = true;
		} else if (over >= 0.0F) {
			// Something over head height's run: room under it to crawl (from the floor, or over what stands there), or none.
			float gap = over - rise;
			if (gap >= crawl) {
				sensed.gapUnder = true;
			} else {
				sensed.wall = true;
			}
		}
		break;
	}
	return sensed;
}

// ---------------------------------------------------------------- Ladders

bool AHuman::FindLadderNear(const Vector& at, float reachX, float& bodyX, float& gripX, int& wallSide, bool& material, int way) const {
	const float h = m_CharHeight;
	const unsigned char ladder = LadderMaterialID();
	if (ladder != 0) {
		// The nearest column with rungs in it, across from the point, over most of a body's height (the rungs are 3 px bars every 8 px,
		// so the look is every 2 px down).
		const int x = static_cast<int>(at.m_X);
		// (From an overhead reach, about a body over the head's middle: a ladder whose foot is above the floor is reached up to and taken.)
		const int top = static_cast<int>(at.m_Y - h * 0.9F);
		const int bottom = static_cast<int>(at.m_Y + h * 0.35F);
		auto columnHas = [&](int cx) {
			for (int y = top; y <= bottom; y += 2) {
				if (g_SceneMan.GetTerrMatter(cx, y) == ladder) {
					return true;
				}
			}
			return false;
		};
		// (For a way up or down, the nearest ladder that goes that way: beside a shaft going up there can be one going down, nearer.)
		const float feet = m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : h * 0.2F;
		auto goesTheWay = [&](int cx) {
			if (way == 0) {
				return true;
			}
			int from = way < 0 ? top : static_cast<int>(at.m_Y + feet + 2.0F);
			int to = way < 0 ? static_cast<int>(at.m_Y - h * 0.15F) : static_cast<int>(at.m_Y + feet + h * 0.5F);
			for (int y = from; y <= to; y += 2) {
				if (g_SceneMan.GetTerrMatter(cx, y) == ladder) {
					return true;
				}
			}
			return false;
		};
		int found = INT_MIN;
		for (int d = 0; d <= static_cast<int>(reachX) && found == INT_MIN; ++d) {
			if (columnHas(x - d) && goesTheWay(x - d)) {
				found = x - d;
			} else if (d > 0 && columnHas(x + d) && goesTheWay(x + d)) {
				found = x + d;
			}
		}
		if (found != INT_MIN) {
			// The ladder's width (its rail and the rungs out from it), and the wall it stands from.
			int left = found;
			int right = found;
			while (found - left < 24 && (columnHas(left - 1) || columnHas(left - 2))) {
				left -= columnHas(left - 1) ? 1 : 2;
			}
			while (right - found < 24 && (columnHas(right + 1) || columnHas(right + 2))) {
				right += columnHas(right + 1) ? 1 : 2;
			}
			auto wallAt = [&](int wx) {
				for (int y = top; y <= bottom; y += 6) {
					if (SolidNotLadder(static_cast<float>(wx), static_cast<float>(y))) {
						return true;
					}
				}
				return false;
			};
			wallSide = wallAt(left - 2) ? -1 : (wallAt(right + 2) ? 1 : 0);
			// The rungs' outer ends are taken (the rail is against the wall); the body hangs a few pixels off them on the open side.
			if (wallSide < 0) {
				gripX = static_cast<float>(right) - 2.0F;
				bodyX = static_cast<float>(right) + 7.0F;
			} else if (wallSide > 0) {
				gripX = static_cast<float>(left) + 2.0F;
				bodyX = static_cast<float>(left) - 7.0F;
			} else {
				gripX = bodyX = static_cast<float>(left + right) * 0.5F;
			}
			material = true;
			return true;
		}
	}
	// A background ladder: no material, a node every 24 px that its script holds a body in front of.
	if (const std::optional<Vector> node = LadderNear(at, reachX, h * 0.5F)) {
		gripX = bodyX = node->m_X;
		wallSide = 0;
		material = false;
		return true;
	}
	return false;
}

void AHuman::LadderRungs(float fromY, float toY, std::vector<float>& rungs) const {
	rungs.clear();
	if (!m_Ladder.material) {
		// (A background ladder's rungs are drawn every 8 px.)
		for (float y = std::floor(fromY / 8.0F) * 8.0F + 4.0F; y <= toY; y += 8.0F) {
			if (y >= fromY) {
				rungs.push_back(y);
			}
		}
		return;
	}
	// The rows with rung at the rungs' outer ends: each run of them one rung, at its middle.
	const unsigned char ladder = LadderMaterialID();
	const int x = static_cast<int>(m_Ladder.gripX);
	bool inRung = false;
	int runStart = 0;
	for (int y = static_cast<int>(fromY); y <= static_cast<int>(toY) + 1; ++y) {
		bool has = g_SceneMan.GetTerrMatter(x, y) == ladder || g_SceneMan.GetTerrMatter(x - 1, y) == ladder || g_SceneMan.GetTerrMatter(x + 1, y) == ladder;
		if (has && !inRung) {
			inRung = true;
			runStart = y;
		} else if (!has && inRung) {
			inRung = false;
			rungs.push_back(static_cast<float>(runStart + y - 1) * 0.5F);
		}
	}
}

void AHuman::LetGoOfLadder(const Vector& velocity) {
	m_Ladder.active = false;
	m_Vel = velocity;
}

void AHuman::UpdateLadderInput() {
	Controller& ctrl = m_Controller;
	const bool up = ctrl.IsState(MOVE_UP);
	const bool down = ctrl.IsState(MOVE_DOWN);
	const bool left = ctrl.IsState(MOVE_LEFT);
	const bool right = ctrl.IsState(MOVE_RIGHT);
	const bool jump = ctrl.IsState(BODY_JUMP);
	const bool side = left || right;
	LadderClimb& ladder = m_Ladder;
	if (ladder.active) {
		if (m_Status != STABLE) {
			ladder.active = false;
			return;
		}
		// A side key steps off (onto a floor beside, or a jump away with the jet's key too); the jet's key alone lets go and jets. With up
		// held, the jet's key is climbing, not the jet: on the mouse and keyboard the two are the one key, W.
		if (side) {
			LetGoOfLadder(Vector(right ? 2.0F : -2.0F, -1.2F));
			return;
		}
		if (jump && !up) {
			LetGoOfLadder(Vector());
			return;
		}
		if (jump && up) {
			ctrl.SetState(BODY_JUMP, false);
			ctrl.SetState(BODY_JUMPSTART, false);
		}
		return;
	}
	// Taking hold: up or down pressed at a ladder, standing, with a hand to climb with; not while flying past (the jet lit lately), nor with
	// a side key held (walking past it, or stepping off).
	if (!(up || down) || side) {
		return;
	}
	// (Why not, in the trace, once a second: the trace is how a unit standing at a ladder's foot is understood.)
	auto refused = [&](const std::string& why) {
		if (m_Mover.traceTimer.IsPastSimMS(1000)) {
			m_Mover.traceTimer.Reset();
			MoverTrace("no hold on a ladder: " + why);
		}
	};
	if (m_Status != STABLE || m_Mantling || m_GettingUp || m_ProneState != NOTPRONE || IsJetFlying() || (!m_pFGArm && !m_pBGArm)) {
		refused(m_Status != STABLE ? "not stable" : (m_Mantling ? "mantling" : (m_GettingUp ? "getting up" : (m_ProneState != NOTPRONE ? "prone" : (IsJetFlying() ? "flying" : "no arms")))));
		return;
	}
	const float h = m_CharHeight;
	const float feet = m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : h * 0.2F;
	float bodyX = 0.0F;
	float gripX = 0.0F;
	int wallSide = 0;
	bool material = false;
	if (!FindLadderNear(m_Pos, h * 0.3F, bodyX, gripX, wallSide, material, up ? -1 : 1)) {
		refused("none in reach");
		return;
	}
	if (std::abs(Towards(m_Pos, Vector(bodyX, m_Pos.m_Y)).m_X) > h * 0.35F) {
		refused("too far across (" + std::to_string(static_cast<int>(bodyX)) + ")");
		return;
	}
	ladder = LadderClimb();
	ladder.material = material;
	ladder.bodyX = bodyX;
	ladder.gripX = gripX;
	ladder.wallSide = wallSide;
	std::vector<float> rungs;
	ladder.active = true;
	LadderRungs(m_Pos.m_Y - h * 0.95F, m_Pos.m_Y + feet + h * 0.5F, rungs);
	ladder.active = false;
	// Up wants rungs above the chest; down wants rungs under the feet (taking hold from the top of it).
	bool rungsAbove = std::any_of(rungs.begin(), rungs.end(), [&](float y) { return y < m_Pos.m_Y - h * 0.15F; });
	bool rungsBelow = std::any_of(rungs.begin(), rungs.end(), [&](float y) { return y > m_Pos.m_Y + feet + 2.0F; });
	// (Nor down onto a floor: a ladder that goes on behind the floor stood on is no way down through it. Taken hold of, the climb found the
	// floor under the feet and ended at once, and a unit whose route went down from there took hold and let go hundreds of times.)
	bool floorUnderFeet = false;
	for (float dx: {-4.0F, 0.0F, 4.0F}) {
		for (float dy = 1.0F; dy <= 3.0F; dy += 1.0F) {
			floorUnderFeet = floorUnderFeet || SolidNotLadder(m_Pos.m_X + dx, m_Pos.m_Y + feet + dy);
		}
	}
	if (down && !up && floorUnderFeet) {
		refused("a floor under the feet");
		return;
	}
	if ((up && !rungsAbove) || (down && !up && !rungsBelow)) {
		refused(std::string(up ? "no rungs above" : "no rungs below") + " (" + std::to_string(rungs.size()) + " rungs, grip x " + std::to_string(static_cast<int>(gripX)) + ")");
		return;
	}
	ladder.active = true;
	ladder.pos = m_Pos;
	ladder.startTimer.Reset();
	ladder.lostTimer.Reset();
	if (jump) {
		ctrl.SetState(BODY_JUMP, false);
		ctrl.SetState(BODY_JUMPSTART, false);
	}
	MoverTrace(std::string("took hold of a ladder (") + (material ? "rungs" : "background") + ", wall " + std::to_string(wallSide) + ")");
}

void AHuman::UpdateLadder() {
	LadderClimb& ladder = m_Ladder;
	if (!ladder.active) {
		return;
	}
	if (m_Status != STABLE || m_Mantling) {
		ladder.active = false;
		return;
	}
	const float h = m_CharHeight;
	const float feet = m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : h * 0.2F;
	const float dt = std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F);
	const double now = g_TimerMan.GetSimTimeMS();

	// The ladder, followed as the body goes (a ladder of pieces, a wall that changes); let go of when it is gone a quarter second.
	{
		float bodyX = 0.0F;
		float gripX = 0.0F;
		int wallSide = 0;
		bool material = false;
		const int way = m_Controller.IsState(MOVE_UP) ? -1 : (m_Controller.IsState(MOVE_DOWN) ? 1 : 0);
		if (FindLadderNear(ladder.pos, h * 0.45F, bodyX, gripX, wallSide, material, way) || (way != 0 && FindLadderNear(ladder.pos, h * 0.45F, bodyX, gripX, wallSide, material, 0))) {
			ladder.bodyX = bodyX;
			ladder.gripX = gripX;
			ladder.wallSide = wallSide;
			ladder.material = material;
			ladder.lostTimer.Reset();
		} else if (ladder.lostTimer.IsPastSimMS(250)) {
			MoverTrace("the ladder is gone; letting go");
			LetGoOfLadder(Vector());
			return;
		}
	}
	const int move = m_Controller.IsState(MOVE_UP) ? -1 : (m_Controller.IsState(MOVE_DOWN) ? 1 : 0);
	std::vector<float> rungs;
	LadderRungs(ladder.pos.m_Y - h * 1.2F, ladder.pos.m_Y + feet + h * 0.6F, rungs);
	const float feetY = ladder.pos.m_Y + feet;

	// The pace: a steady climb, quicker down; each pull of a hand a surge, eased off while the next hand reaches, and eased in at the start.
	float pace = (move < 0 ? 2.1F : 2.8F) * c_PPM;
	float pull = 1.0F;
	if (ladder.lastHandRegripMS >= 0.0) {
		float since = std::clamp(static_cast<float>(now - ladder.lastHandRegripMS) / 220.0F, 0.0F, 1.0F);
		pull = 0.55F + 0.45F * since * since * (3.0F - 2.0F * since);
	}
	float start = std::clamp(static_cast<float>(ladder.startTimer.GetElapsedSimTimeMS()) / 250.0F, 0.0F, 1.0F);
	float dy = static_cast<float>(move) * pace * pull * (0.35F + 0.65F * start) * dt;

	if (move < 0) {
		// No higher than standing on the top rung (it waits there for a side key, or for the floor to take it), nor into a ceiling.
		float topRung = rungs.empty() ? feetY : rungs.front();
		float highest = topRung - 2.0F - feet;
		if (ladder.pos.m_Y + dy < highest) {
			dy = std::min(0.0F, highest - ladder.pos.m_Y);
		}
		float headTop = ladder.pos.m_Y + dy - h * 0.26F - 2.0F;
		for (float dx: {-4.0F, 0.0F, 4.0F}) {
			if (SolidNotLadder(ladder.pos.m_X + dx, headTop)) {
				dy = 0.0F;
			}
		}
	} else if (move > 0) {
		// Down onto a floor: standing on it ends the climb. A ladder that ends in the air is let go of a little way past its last rung.
		for (float dx: {-4.0F, 0.0F, 4.0F}) {
			for (float y = feetY; y <= feetY + dy + 2.0F; y += 1.0F) {
				if (SolidNotLadder(ladder.pos.m_X + dx, y)) {
					ladder.active = false;
					m_Vel.Reset();
					MoverTrace("off the ladder at its foot");
					return;
				}
			}
		}
		float bottomRung = rungs.empty() ? feetY : rungs.back();
		if (feetY > bottomRung + h * 0.3F) {
			MoverTrace("past the ladder's last rung; letting go");
			LetGoOfLadder(Vector(0.0F, 1.0F));
			return;
		}
	}
	const Vector before = ladder.pos;
	ladder.pos.m_Y += dy;
	ladder.climbed += std::abs(dy);

	// Across: eased to the ladder's line, with a little sway towards the wall and back on each pull (over two rungs' climb).
	float sway = ladder.wallSide != 0 ? 1.2F * std::sin(ladder.climbed * (c_PI / 16.0F)) : 0.0F;
	float targetX = ladder.bodyX - static_cast<float>(ladder.wallSide) * std::abs(sway) * 0.5F;
	float gap = Towards(ladder.pos, Vector(targetX, ladder.pos.m_Y)).m_X;
	ladder.pos.m_X += gap * std::min(1.0F, dt * 10.0F);

	// The body is put where the climb has it, and its speed is what that movement is (the limbs and the camera go by it); upright; the
	// ladder's rungs passed (the limbs take them, the body hangs among them).
	// (The speed is the climb's own movement this frame, not the way back from where physics left the body: that way includes last frame's
	// speed already travelled, so each frame's speed undid the last one's, flipping fast and slow every frame, and the limbs, placed from
	// where physics left the body, shook the whole climber.)
	m_Vel = Towards(before, ladder.pos) * (c_MPP / dt);
	m_Pos = ladder.pos;
	m_AngularVel = 0.0F;
	m_Rotation.SetRadAngle(m_Rotation.GetRadAngle() * 0.8F);
	SetPassMaterial(LadderMaterialID());
	// The AI's unit faces the ladder and looks the way it climbs (a player's aim is the player's).
	if (m_Controller.GetInputMode() == Controller::CIM_AI) {
		if (ladder.wallSide != 0) {
			m_HFlipped = ladder.wallSide < 0;
		}
		SetAimAngle(move < 0 ? 0.9F : (move > 0 ? -0.6F : 0.2F));
	}

	// At the top with a floor under the feet (a landing reached by the last rung): standing, and done.
	if (move < 0 && dy == 0.0F && !rungs.empty() && feetY <= rungs.front() + 1.0F) {
		for (float dx: {-6.0F, 0.0F, 6.0F}) {
			if (SolidNotLadder(ladder.pos.m_X + dx, feetY + 2.0F)) {
				ladder.active = false;
				m_Vel.Reset();
				MoverTrace("over the top of the ladder");
				return;
			}
		}
	}
}

void AHuman::UpdateLadderLimbs() {
	LadderClimb& ladder = m_Ladder;
	const float h = m_CharHeight;
	const double now = g_TimerMan.GetSimTimeMS();
	std::vector<float> rungs;
	LadderRungs(m_Pos.m_Y - h * 1.2F, m_Pos.m_Y + h * 0.9F, rungs);
	if (rungs.empty()) {
		return;
	}
	const int move = m_Controller.IsState(MOVE_UP) ? -1 : (m_Controller.IsState(MOVE_DOWN) ? 1 : 0);

	// Each limb's joint and reach: the shoulders and hips. A hand holding something stays on it (a rifle is carried up a ladder).
	struct Limb {
		bool present;
		bool hand;
		Vector joint;
		float reach;
	};
	std::array<Limb, 4> limbs = {Limb{m_pFGArm && !m_pFGArm->GetHeldDevice(), true, m_pFGArm ? m_pFGArm->GetJointPos() : m_Pos, m_pFGArm ? m_pFGArm->GetMaxLength() : 0.0F},
	                             Limb{m_pBGArm && !m_pBGArm->GetHeldDevice(), true, m_pBGArm ? m_pBGArm->GetJointPos() : m_Pos, m_pBGArm ? m_pBGArm->GetMaxLength() : 0.0F},
	                             Limb{m_pFGLeg != nullptr, false, m_pFGLeg ? m_Pos + RotateOffset(m_pFGLeg->GetParentOffset()) : m_Pos, m_pFGLeg ? m_pFGLeg->GetMaxLength() : 0.0F},
	                             Limb{m_pBGLeg != nullptr, false, m_pBGLeg ? m_Pos + RotateOffset(m_pBGLeg->GetParentOffset()) : m_Pos, m_pBGLeg ? m_pBGLeg->GetMaxLength() : 0.0F}};
	// The rung nearest a height, within a band if any is (else the nearest of all).
	auto rungNear = [&](float want, float low, float high) {
		float best = rungs.front();
		float bestScore = std::numeric_limits<float>::max();
		for (float y: rungs) {
			float score = std::abs(y - want) + ((y < low || y > high) ? 1000.0F : 0.0F);
			if (score < bestScore) {
				bestScore = score;
				best = y;
			}
		}
		return best;
	};
	// Which limb most wants a new rung: one with none, one outside its comfortable band, or one the climb has left behind (a hand come down
	// to the shoulder going up, a foot stretched out below; the other way round going down). One limb at a time, and the hand and opposite
	// foot after one another, as a climber goes: front hand, back foot, back hand, front foot.
	int pick = -1;
	float pickScore = 0.0F;
	for (int i = 0; i < 4; ++i) {
		const Limb& limb = limbs[i];
		if (!limb.present || limb.reach <= 0.0F) {
			continue;
		}
		float rel = ladder.grip[i] - limb.joint.m_Y;
		float low = limb.hand ? -0.95F * limb.reach : 0.35F * limb.reach;
		float high = limb.hand ? -0.15F * limb.reach : 0.98F * limb.reach;
		float over = 0.0F;
		if (!ladder.gripped[i]) {
			over = 1000.0F;
		} else if (rel < low) {
			over = low - rel;
		} else if (rel > high) {
			over = rel - high;
		} else if (move < 0) {
			over = limb.hand ? rel - (-0.3F * limb.reach) : rel - 0.9F * limb.reach;
		} else if (move > 0) {
			over = limb.hand ? (-0.92F * limb.reach) - rel : (0.45F * limb.reach) - rel;
		}
		if (over <= 0.0F) {
			continue;
		}
		int partner = ladder.lastLimb == 0 ? 3 : (ladder.lastLimb == 3 ? 1 : (ladder.lastLimb == 1 ? 2 : (ladder.lastLimb == 2 ? 0 : -1)));
		float score = over + (i == partner ? 6.0F : 0.0F);
		if (score > pickScore) {
			pickScore = score;
			pick = i;
		}
	}
	if (pick >= 0 && (ladder.lastRegripMS < 0.0 || now - ladder.lastRegripMS > 70.0)) {
		const Limb& limb = limbs[pick];
		float want = 0.0F;
		if (move < 0) {
			want = limb.hand ? -0.85F : 0.5F;
		} else if (move > 0) {
			want = limb.hand ? -0.35F : 0.9F;
		} else {
			want = limb.hand ? -0.6F : 0.75F;
		}
		float low = limb.joint.m_Y + (limb.hand ? -0.95F : 0.35F) * limb.reach;
		float high = limb.joint.m_Y + (limb.hand ? -0.15F : 0.98F) * limb.reach;
		bool inReach = std::any_of(rungs.begin(), rungs.end(), [&](float y) { return y >= low && y <= high; });
		// (A foot with no rung in reach hangs, straight down from its hip, until the climb brings one: reaching up to a ladder whose foot is
		// over the floor, or at the bottom of one that ends in the air. A hand with none takes the nearest, which is the reach up.)
		ladder.grip[pick] = (!limb.hand && !inReach) ? limb.joint.m_Y + 0.9F * limb.reach : rungNear(limb.joint.m_Y + want * limb.reach, low, high);
		bool first = !ladder.gripped[pick];
		ladder.gripped[pick] = true;
		ladder.lastRegripMS = now;
		ladder.lastLimb = pick;
		if (limb.hand) {
			ladder.lastHandRegripMS = now;
		} else if (!first && m_StrideSound && m_StrideSound->GetLoopSetting() >= 0) {
			// (A foot set on a rung: the step's sound, as on the ground.)
			m_StrideSound->Play(m_Pos);
		}
	}

	// The feet on their rungs, just in from the rungs' ends, one a little behind the other. (The hands are given theirs with the arms'.)
	const float inward = ladder.wallSide != 0 ? -static_cast<float>(ladder.wallSide) : 0.0F;
	if (m_pFGLeg && ladder.gripped[2]) {
		m_pFGFootGroup->SetLimbPos(Vector(ladder.gripX + inward * 1.0F - 1.0F, ladder.grip[2] - 1.0F), m_HFlipped);
	}
	if (m_pBGLeg && ladder.gripped[3]) {
		m_pBGFootGroup->SetLimbPos(Vector(ladder.gripX + inward * 1.0F + 1.0F, ladder.grip[3] - 1.0F), m_HFlipped);
	}
}

// A door of ours, or no one's, whose moving part lies near the line from us to a point, within a body and a half.
ADoor* AHuman::DoorAhead(const Vector& toPoint) const {
	ADoor* best = nullptr;
	float bestDistance = std::numeric_limits<float>::max();
	Vector along = Towards(m_Pos, toPoint);
	float length = along.GetMagnitude();
	for (ADoor* door: g_MovableMan.GetDoorList()) {
		if ((door->GetTeam() != m_Team && door->GetTeam() != Activity::NoTeam) || !door->GetDoor() || !door->GetDoor()->IsAttached()) {
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

Actor* AHuman::UnitAhead(float direction, float reach) const {
	const float h = m_CharHeight;
	Actor* nearest = nullptr;
	float nearestAhead = std::min(h * 0.5F + 10.0F, reach);
	for (Actor* actor: g_MovableMan.GetActorList()) {
		if (!actor || actor == this || actor->GetTeam() != m_Team || actor->GetStatus() == DYING || actor->GetStatus() == DEAD || dynamic_cast<ADoor*>(actor) || dynamic_cast<ACraft*>(actor)) {
			continue;
		}
		// (Not something that never moves out of the way: a turret, a crab with no legs.)
		if (const ACrab* crab = dynamic_cast<const ACrab*>(actor); crab && !crab->GetLeftFGLeg() && !crab->GetLeftBGLeg() && !crab->GetRightFGLeg() && !crab->GetRightBGLeg()) {
			continue;
		}
		Vector to = Towards(m_Pos, actor->GetPos());
		float ahead = to.m_X * direction;
		if (ahead <= 0.0F || ahead >= nearestAhead || std::abs(to.m_Y) > h * 0.6F) {
			continue;
		}
		nearest = actor;
		nearestAhead = ahead;
	}
	return nearest;
}

bool AHuman::InDoorSweep() const {
	for (ADoor* door: g_MovableMan.GetDoorList()) {
		if ((door->GetTeam() == m_Team || door->GetTeam() == Activity::NoTeam) && door->GetDoor() && door->GetDoorState() != ADoor::CLOSED && Towards(m_Pos, door->GetPos()).MagnitudeIsLessThan(m_CharHeight * 1.2F) && door->SweepContains(m_Pos, m_CharHeight * 0.3F)) {
			return true;
		}
	}
	return false;
}

void AHuman::RememberStuckRemedy(const Vector& spot, int remedy, bool worked) {
	const float near = static_cast<float>(g_SettingsMan.GetPathFinderGridNodeSize()) * 1.5F;
	// (One entry per remedy and spot: the newest outcome stands.)
	for (auto entry = m_StuckRemedyMemory.begin(); entry != m_StuckRemedyMemory.end();) {
		bool same = entry->Remedy == remedy && g_SceneMan.ShortestDistance(entry->Spot, spot, g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY()).MagnitudeIsLessThan(near);
		entry = same || entry->Age.IsPastSimMS(120000) ? m_StuckRemedyMemory.erase(entry) : std::next(entry);
	}
	m_StuckRemedyMemory.push_back({spot, remedy, worked, Timer()});
	while (m_StuckRemedyMemory.size() > 24) {
		m_StuckRemedyMemory.pop_front();
	}
}

int AHuman::PickStuckRemedy(const Vector& spot, const std::array<bool, static_cast<int>(StuckRemedy::Count)>& allowed, unsigned int tried) const {
	const float near = static_cast<float>(g_SettingsMan.GetPathFinderGridNodeSize()) * 1.5F;
	std::array<int, static_cast<int>(StuckRemedy::Count)> known;
	known.fill(0); // 0 nothing known here, 1 worked, -1 failed (in the last two minutes).
	for (const StuckRemedyMemory& entry: m_StuckRemedyMemory) {
		if (!entry.Age.IsPastSimMS(120000) && g_SceneMan.ShortestDistance(entry.Spot, spot, g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY()).MagnitudeIsLessThan(near)) {
			known[entry.Remedy] = entry.Worked ? 1 : -1;
		}
	}
	// (A leap that worked here keeps its place in the order, after the duck and the back-off, and isn't tried first: it is for when the walk
	// can't go, and a leap credited once, often for a stuck spell the walk would have cleared anyway, made the unit leap at once at that
	// spot every time after.)
	for (int pass = 0; pass < 2; ++pass) {
		for (int remedy = 0; remedy < static_cast<int>(StuckRemedy::Count); ++remedy) {
			int knownHere = remedy == static_cast<int>(StuckRemedy::Leap) && known[remedy] == 1 ? 0 : known[remedy];
			if (allowed[remedy] && !(tried & (1U << remedy)) && knownHere == (pass == 0 ? 1 : 0)) {
				return remedy;
			}
		}
	}
	return -1;
}

void AHuman::ResetRouteMovement() {
	m_Mover = RouteMover();
	m_Mover.lastProgressPos = m_Pos;
}

// The jet's push at a fuel level. A jetpack's throttle follows the fuel left (AEJetpack::UpdateBurstState: from its negative throttle
// multiplier when empty to its positive one when full, 0.8 to 1.2 for the base game's packs), unless it adjusts for weight instead, when
// it is what it is now. The push now, as learned in flight (JetAccelNow), is scaled by the throttle there against the throttle now.
float AHuman::JetAccelAtFuel(float fuel) const {
	if (!m_pJetpack) {
		return 0.0F;
	}
	float accelNow = JetAccelNow();
	float factorNow = m_pJetpack->GetThrottleFactor();
	if (m_pJetpack->GetAdjustsThrottleForWeight() || factorNow <= 0.05F) {
		return accelNow;
	}
	float total = std::max(1.0F, m_pJetpack->GetJetTimeTotal());
	float throttle = std::clamp(fuel / total, 0.0F, 1.0F) * 2.0F - 1.0F;
	float low = m_pJetpack->GetNegativeThrottleMultiplier();
	float high = m_pJetpack->GetPositiveThrottleMultiplier();
	float factor = low + (high - low) * (throttle + 1.0F) * 0.5F;
	return accelNow / factorNow * factor;
}

float AHuman::JetRelightFuel() const {
	if (!m_pJetpack) {
		return 0.0F;
	}
	return std::max(250.0F * std::max(m_pJetpack->GetThrottleFactor(), 0.5F), m_pJetpack->GetMinimumFuelRatio() * m_pJetpack->GetJetTimeTotal()) + 30.0F;
}

// The climb flown in thirtieths of a second, as the pilot flies it: full burn to the speed cap (12 m/s), held there, the jet out from the
// height gravity alone stops it in, to 32 px under the top (the hover and the step off are the reserve's). The push falls with the tank;
// the tank fills while the jet is out. (8.0's SharedBehaviors.ClimbFuelLeft; a fixed 8 m/s climb, as the reckoning was, took no account of
// the unit's own jet: a heavy unit on a weak pack climbs slower and burns more of its tank for the same shaft.)
float AHuman::ClimbFuelLeft(float height, float fuel) const {
	if (!m_pJetpack) {
		return -1.0F;
	}
	const float gravity = g_SceneMan.GetGlobalAcc().m_Y * c_PPM;
	const float total = std::max(1.0F, m_pJetpack->GetJetTimeTotal());
	const float cap = 12.0F * c_PPM;
	const float relight = JetRelightFuel();
	const float replenish = m_pJetpack->GetJetReplenishRate();
	const float use = m_pJetpack->GetAdjustsThrottleForWeight() ? std::max(m_pJetpack->GetThrottleFactor(), 0.1F) : 1.0F;
	const float dt = 1.0F / 30.0F;
	if (JetAccelAtFuel(total) <= gravity + 1.0F) {
		return -1.0F; // (Not even a full tank lifts it.)
	}
	height = std::max(0.0F, height - 32.0F);
	float y = 0.0F;
	float up = 0.0F;
	float t = 0.0F;
	bool lit = false;
	fuel -= 200.0F; // The burst that lights it.
	while (y < height) {
		if (t > 6.0F || fuel <= 0.0F) {
			return -1.0F;
		}
		float rate = std::clamp(std::sqrt(2.0F * gravity * std::max(0.0F, height - y)), 20.0F, cap);
		// (The jet won't relight on less than its minimum; a climb that needs another pulse with less has none.)
		if (!lit && up < rate - 20.0F && fuel <= relight) {
			return -1.0F;
		}
		lit = up < (lit ? rate + 20.0F : rate - 20.0F);
		if (lit) {
			up += (JetAccelAtFuel(fuel) - gravity) * dt;
			fuel -= dt * 1000.0F * use;
		} else {
			up -= gravity * dt;
			fuel = std::min(total, fuel + dt * 1000.0F * replenish);
		}
		y += up * dt;
		t += dt;
	}
	return fuel;
}

// The least fuel to begin a climb on and have the reserve left at the top: found by halving between none and a full tank. A climb no
// full tank makes asks for most of a tank (the flight then climbs in stages, refuelling on the way; see MoveAlongRoute).
float AHuman::ClimbFuelNeeded(float height, float reserve) const {
	if (!m_pJetpack) {
		return 0.0F;
	}
	float total = m_pJetpack->GetJetTimeTotal();
	double now = g_TimerMan.GetSimTimeMS();
	if (m_ClimbFuelCacheTimeMS < 0.0 || now - m_ClimbFuelCacheTimeMS > 2000.0) {
		m_ClimbFuelCacheTimeMS = now;
		m_ClimbFuelCache.fill(-1.0F);
	}
	int index = std::clamp(static_cast<int>(height / 8.0F), 0, 127) * 2 + (reserve > 350.0F ? 1 : 0);
	if (m_ClimbFuelCache[index] >= 0.0F) {
		return m_ClimbFuelCache[index];
	}
	float needed = total * 0.98F;
	if (ClimbFuelLeft(height, total) >= reserve) {
		float low = 0.0F;
		float high = total;
		for (int k = 0; k < 7; ++k) {
			float middle = (low + high) * 0.5F;
			if (ClimbFuelLeft(height, middle) >= reserve) {
				high = middle;
			} else {
				low = middle;
			}
		}
		needed = high;
	}
	m_ClimbFuelCache[index] = needed;
	return needed;
}

float AHuman::ClimbFuelPerPixel() const {
	if (!m_pJetpack || m_pJetpack->GetJetTimeTotal() <= 0.0F) {
		return 6.0F;
	}
	float total = m_pJetpack->GetJetTimeTotal();
	for (float height: {200.0F, 100.0F, 50.0F}) {
		float left = ClimbFuelLeft(height, total);
		if (left >= 0.0F) {
			return std::max(0.5F, (total - 200.0F - left) / height);
		}
	}
	return total; // (No climb worth the name: no flight link goes up.)
}

// The fuel a flight takes, in ms: the climb on this unit's jet with a reserve for the top (450 ms for a landing off to one side, which
// is hovered across to, 300 for straight up), or the burst for one that doesn't climb, and the crossing lit about half the time.
float AHuman::FlightFuelNeeded(const Vector& landing, float landingFloorY) const {
	Vector to = Towards(m_Pos, landing);
	float feetY = m_Pos.m_Y + (m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : m_CharHeight * 0.2F);
	float rise = feetY - landingFloorY + 12.0F;
	float climb = rise > 16.0F ? ClimbFuelNeeded(rise, std::abs(to.m_X) >= 10.0F ? 450.0F : 300.0F) : 200.0F;
	float needed = climb + std::abs(to.m_X) / (5.0F * c_PPM) * 0.5F * 1000.0F * 1.2F;
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
void AHuman::PopRouteToLanding(const Vector& landing, int pointsToLanding) {
	// Up to and including the route's point nearest the landing, when one is near it: the route may have been replaced in the air (route
	// checks run during flights, and an adopted one starts where the unit was), so the count of points to the landing the flight was
	// planned with can belong to another route, and popping that many dropped the wrong points. The count is only the fallback.
	const float reach = std::max(m_CharHeight * 0.75F, 24.0F);
	int nearestIndex = -1;
	float nearestDistance = reach;
	int index = 0;
	for (const Vector& point: m_MovePath) {
		if (index >= 30) {
			break;
		}
		float distance = Towards(point, landing).GetMagnitude();
		if (distance < nearestDistance) {
			nearestDistance = distance;
			nearestIndex = index;
		}
		++index;
	}
	int toPop = nearestIndex >= 0 ? nearestIndex + 1 : pointsToLanding;
	for (int k = 0; k < toPop && !m_MovePath.empty(); ++k) {
		PopRoutePoint();
	}
}

bool AHuman::FindLanding(Vector& landing, float& landingFloorY, int& pointsToLanding) const {
	Vector takeOff;
	return FindLanding(landing, landingFloorY, pointsToLanding, takeOff);
}

bool AHuman::FindLanding(Vector& landing, float& landingFloorY, int& pointsToLanding, Vector& takeOff) const {
	float h = m_CharHeight;
	bool airborne = false;
	int index = 0;
	Vector last = m_Pos;
	bool takeOffFound = false;
	takeOff = m_Pos;
	auto kindIt = m_MovePathKinds.begin();
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
		// (A leg up or down a ladder is climbed, a leap is leapt, a mantle pulled up onto and a scramble scrambled up: no flight, and where it
		// ends is no landing for one.)
		PathStepKind legKind = kindIt != m_MovePathKinds.end() ? *kindIt : PathStepKind::Walk;
		if (kindIt != m_MovePathKinds.end()) {
			++kindIt;
		}
		if (legKind == PathStepKind::Ladder || legKind == PathStepKind::Leap || legKind == PathStepKind::Mantle || legKind == PathStepKind::Scramble) {
			if (airborne) {
				break;
			}
			last = point;
			continue;
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
		}
		if (airborne && !takeOffFound) {
			takeOffFound = true;
			takeOff = last; // (The last point on the floor before the air: the unit itself when the first leg leaves the floor.)
		}
		if (floorY < 0.0F) {
		} else if (airborne) {
			candidates.push_back({point, floorY, index});
			airborne = false;
		}
		last = point;
	}
	if (candidates.empty()) {
		return false;
	}
	// A further landing only over a dip: every landing before it well below both the floor here and its own (a valley the route goes down
	// into and climbs out of). Indoors, taking the furthest that could be flown to, a unit flew for a landing 330 px off, came down short,
	// took the near one on its next route, then the far one again, back and forth for the minute.
	float floorHere = FloorUnder(m_Pos, h * 0.8F);
	if (floorHere < 0.0F) {
		floorHere = m_Pos.m_Y + h * 0.2F;
	}
	for (int k = static_cast<int>(candidates.size()) - 1; k >= std::max(1, static_cast<int>(candidates.size()) - 4); --k) {
		const Candidate& candidate = candidates[k];
		bool dip = true;
		for (int j = 0; j < k && dip; ++j) {
			dip = candidates[j].floorY > std::max(floorHere, candidate.floorY) + h * 0.5F;
		}
		if (!dip) {
			continue;
		}
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

bool AHuman::ColumnOpen(float x, float fromY, float toY) const {
	if (toY >= fromY) {
		return true;
	}
	Vector hit;
	return !g_SceneMan.CastStrengthRay(Vector(x, fromY), Vector(0.0F, toY - fromY), 5.0F, hit, 2, MaterialColorKeys::g_MaterialDoor);
}

bool AHuman::ShaftColumn(float columnX, float topHeadY, float& middleX, float& width) const {
	// Looked at every few pixels from where the head starts to where it will be at the top, at the route's column: wherever there are
	// walls both sides within reach, the nearest faces bound the channel the body goes up, and the narrowest of them is the shaft. (Looked
	// at only from where the unit stood, a shaft whose walls begin above the corridor's ceiling, or one beside the unit, wasn't seen, and
	// a unit beside a 130 px shaft stood there the whole minute. 8.0's SharedBehaviors.ClimbPlan looked along the climb, as this does.)
	float h = m_CharHeight;
	float headAbove = h * 0.24F;
	float fromY = m_Pos.m_Y - headAbove;
	float span = fromY - topHeadY;
	if (span <= 0.0F) {
		return false;
	}
	float step = std::max(6.0F, span / 30.0F);
	float reach = h * 0.9F;
	float leftFace = -std::numeric_limits<float>::max();
	float rightFace = std::numeric_limits<float>::max();
	bool found = false;
	for (float y = fromY; y >= topHeadY; y -= step) {
		if (Solid(columnX, y)) {
			continue; // (Inside a ceiling or a wall at this height: nothing to measure from.)
		}
		Vector from(columnX, y);
		Vector leftHit;
		Vector rightHit;
		bool left = g_SceneMan.CastStrengthRay(from, Vector(-reach, 0.0F), 5.0F, leftHit, 1, MaterialColorKeys::g_MaterialDoor);
		bool right = g_SceneMan.CastStrengthRay(from, Vector(reach, 0.0F), 5.0F, rightHit, 1, MaterialColorKeys::g_MaterialDoor);
		if (left && right) {
			leftFace = std::max(leftFace, columnX + Towards(from, leftHit).m_X);
			rightFace = std::min(rightFace, columnX + Towards(from, rightHit).m_X);
			found = true;
		}
	}
	if (!found || rightFace <= leftFace) {
		return false;
	}
	width = rightFace - leftFace;
	if (width > h * 1.6F) {
		return false;
	}
	middleX = (leftFace + rightFace) * 0.5F;
	// The middle, or the nearest line beside it that is open all the way up (a ladder's rungs, a lip): 8.0 climbed a soldier up a node's
	// own x with its side in the rungs, and it burned a tank getting two thirds of the way.
	for (float dx: {0.0F, -h * 0.08F, h * 0.08F, -h * 0.16F, h * 0.16F, -h * 0.24F, h * 0.24F}) {
		if (ColumnOpen(middleX + dx, fromY + 2.0F, topHeadY + 2.0F)) {
			middleX += dx;
			break;
		}
	}
	return true;
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
	if (IsAITraced()) {
		g_ConsoleMan.PrintString("AITRACE mover: " + text + " at " + std::to_string(m_Pos.GetFloorIntX()) + "," + std::to_string(m_Pos.GetFloorIntY()));
	}
}

void AHuman::DrawMoverDebug() const {
	static const unsigned char routeColor = static_cast<unsigned char>(Color(90, 200, 255).GetIndex());
	static const unsigned char pointColor = static_cast<unsigned char>(Color(255, 255, 255).GetIndex());
	static const unsigned char takeOffColor = static_cast<unsigned char>(Color(255, 150, 40).GetIndex());
	static const unsigned char landingColor = static_cast<unsigned char>(Color(80, 230, 90).GetIndex());
	static const unsigned char viaColor = static_cast<unsigned char>(Color(220, 90, 255).GetIndex());
	// The route ahead, its first dozen points, and the point in hand.
	Vector last = m_Pos;
	int count = 0;
	for (const Vector& point: m_MovePath) {
		if (++count > 12) {
			break;
		}
		g_PrimitiveMan.DrawLinePrimitive(last, point, routeColor);
		last = point;
	}
	if (!m_MovePath.empty()) {
		g_PrimitiveMan.DrawCirclePrimitive(m_MovePath.front(), 3, pointColor);
	}
	const RouteMover& mover = m_Mover;
	if (mover.flight.active) {
		g_PrimitiveMan.DrawLinePrimitive(m_Pos, mover.flight.via ? mover.flight.viaPoint : mover.flight.landing, landingColor);
		g_PrimitiveMan.DrawCircleFillPrimitive(Vector(mover.flight.landing.m_X, mover.flight.floorY), 3, landingColor);
		if (mover.flight.via) {
			g_PrimitiveMan.DrawCirclePrimitive(mover.flight.viaPoint, 4, viaColor);
		}
		if (mover.flight.step) {
			g_PrimitiveMan.DrawLinePrimitive(Vector(m_Pos.m_X - 8.0F, mover.flight.holdY), Vector(m_Pos.m_X + 8.0F, mover.flight.holdY), viaColor);
		}
	} else if (mover.hasTakeOff) {
		g_PrimitiveMan.DrawCirclePrimitive(mover.debugTakeOff, 4, takeOffColor);
	}
	// The flight as planned (white) beside the way it went (yellow), and over the head a bar of the tank: what the flight was expected to
	// take (the white tick) against what it has burned (green while within it, red past it). Shown for four seconds after the flight ends.
	const RouteMover::FlightRecord& record = mover.debugFlight;
	if (!record.trail.empty() && (record.recording || !record.sinceEnd.IsPastSimMS(4000))) {
		static const unsigned char plannedColor = static_cast<unsigned char>(Color(235, 235, 235).GetIndex());
		static const unsigned char trailColor = static_cast<unsigned char>(Color(255, 220, 60).GetIndex());
		static const unsigned char withinColor = static_cast<unsigned char>(Color(80, 220, 90).GetIndex());
		static const unsigned char overColor = static_cast<unsigned char>(Color(240, 70, 60).GetIndex());
		static const unsigned char barColor = static_cast<unsigned char>(Color(120, 120, 120).GetIndex());
		for (size_t i = 1; i < record.planned.size(); ++i) {
			g_PrimitiveMan.DrawLinePrimitive(record.planned[i - 1], record.planned[i], plannedColor);
		}
		for (size_t i = 1; i < record.trail.size(); ++i) {
			g_PrimitiveMan.DrawLinePrimitive(record.trail[i - 1], record.trail[i], trailColor);
		}
		if (record.tank > 0.0F) {
			const float width = 30.0F;
			Vector barLeft = m_Pos + Vector(-width * 0.5F, -m_CharHeight * 0.6F - 10.0F);
			float used = std::min(1.0F, record.fuelUsed / record.tank);
			float predicted = std::min(1.0F, record.fuelPredicted / record.tank);
			g_PrimitiveMan.DrawBoxPrimitive(barLeft, barLeft + Vector(width, 3.0F), barColor);
			if (used > 0.0F) {
				g_PrimitiveMan.DrawBoxFillPrimitive(barLeft, barLeft + Vector(width * used, 3.0F), record.fuelUsed > record.fuelPredicted ? overColor : withinColor);
			}
			g_PrimitiveMan.DrawLinePrimitive(barLeft + Vector(width * predicted, -2.0F), barLeft + Vector(width * predicted, 5.0F), plannedColor);
			g_PrimitiveMan.DrawTextPrimitive(barLeft + Vector(width * 0.5F, -10.0F), "fuel " + std::to_string(static_cast<int>(record.fuelUsed)) + " of " + std::to_string(static_cast<int>(record.fuelPredicted)), true, 1);
		}
	}
	std::string state = mover.flight.active ? (mover.flight.refuelling ? "refuel" : (mover.flight.step ? "step" : (mover.flight.via ? "shaft" : "flight"))) : (mover.fuelWaiting ? "fuel wait" : "walk");
	g_PrimitiveMan.DrawTextPrimitive(m_Pos + Vector(0.0F, -m_CharHeight * 0.6F), state, true, 1);
}

void AHuman::RecordFlightDebug() {
	RouteMover::FlightRecord& record = m_Mover.debugFlight;
	const RouteMover::Flight& flight = m_Mover.flight;
	float fuel = m_pJetpack ? m_pJetpack->GetJetTimeLeft() : 0.0F;
	if (flight.active) {
		bool sameFlight = record.recording && record.takeOff.m_X == flight.takeOff.m_X && record.takeOff.m_Y == flight.takeOff.m_Y && record.landing.m_X == flight.landing.m_X && record.landing.m_Y == flight.landing.m_Y;
		if (!sameFlight) {
			// A new flight (seen the update after take-off, a pixel or two up): its plan in the body's place, as the pilot steers it.
			record = RouteMover::FlightRecord();
			record.recording = true;
			record.takeOff = flight.takeOff;
			record.landing = flight.landing;
			record.planned.push_back(flight.takeOff);
			if (flight.via) {
				record.planned.push_back(flight.viaPoint);
			}
			float feet = m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : m_CharHeight * 0.2F;
			record.planned.push_back(Vector(flight.landing.m_X, flight.floorY - feet));
			record.fuelPredicted = FlightFuelNeeded(flight.landing, flight.floorY);
			record.tank = m_pJetpack ? m_pJetpack->GetJetTimeTotal() : 0.0F;
			record.lastFuel = fuel;
		}
		record.fuelUsed += std::max(0.0F, record.lastFuel - fuel);
		record.lastFuel = fuel;
		if (record.trail.empty() || g_SceneMan.ShortestDistance(record.trail.back(), m_Pos).GetMagnitude() >= 4.0F) {
			record.trail.push_back(m_Pos);
			if (record.trail.size() > 240) {
				record.trail.pop_front();
			}
		}
	} else if (record.recording) {
		record.recording = false;
		record.sinceEnd.Reset();
	}
}

void AHuman::GetDebugState(std::vector<DebugStateField>& fields) const {
	Actor::GetDebugState(fields);
	const RouteMover& mover = m_Mover;
	auto number = [&fields](const char* name, double value) { fields.push_back({name, std::to_string(static_cast<long long>(std::llround(value))), false}); };
	fields.push_back({"mover", mover.flight.active ? (mover.flight.refuelling ? "refuel" : (mover.flight.step ? "step" : (mover.flight.via ? "shaft" : "flight"))) : (mover.fuelWaiting ? "fuel wait" : (mover.settling ? "settle" : "walk")), true});
	number("progressMs", mover.progressTimer.GetElapsedSimTimeMS());
	number("stuckLevel", mover.stuckLevel);
	{
		const char* const remedyNames[] = {"crouch", "back off", "leap", "hop", "lie down", "stand up"};
		fields.push_back({"stuckRemedy", mover.remedy >= 0 && mover.remedy < static_cast<int>(StuckRemedy::Count) ? remedyNames[mover.remedy] : "none", true});
	}
	number("impossibleAnswers", mover.impossibleAnswers);
	if (mover.fuelWaiting) {
		number("fuelWaitMs", mover.fuelWaitTimer.GetElapsedSimTimeMS());
	}
	if (mover.flight.active) {
		number("landingX", mover.flight.landing.m_X);
		number("landingY", mover.flight.floorY);
		number("flightMs", mover.flight.totalTimer.GetElapsedSimTimeMS());
		number("refuels", mover.flight.stages);
	}
	fields.push_back({"prone", m_ProneState != NOTPRONE ? "true" : "false", false});
}

int AHuman::MoveAlongRoute() {
	RouteMover& mover = m_Mover;
	// Not called for a while: whoever drives it held the unit (to fight, to look at an alarm, to follow someone near at hand). Its timers
	// ran on through the hold, and the first call after a long fight would take the time for being stuck, mark the step it was on avoided
	// and ask for a new route, so they start again here, whatever the caller remembers to do. A flight in hand is kept: it is flown on.
	const long long tick = g_TimerMan.GetSimUpdateCount();
	if (mover.lastCallTick >= 0 && tick - mover.lastCallTick > static_cast<long long>(std::max(1, g_SettingsMan.GetAIUpdateInterval()) * 2 + 1) && !mover.flight.active) {
		ResetRouteMovement();
	}
	mover.lastCallTick = tick;
	// Pulling up onto a ledge (a mantle, or a ledge caught in the air, LM-6): the body is the mantle's until it is over. The flight in hand
	// waits for it, so a catch under the landing ends on the landing (judged there, the route popped to it) rather than being taken for a
	// flight out of fuel short of it while the hands pulled.
	if (m_Mantling) {
		mover.progressTimer.Reset();
		return RouteMover::Moving;
	}
	if (g_SettingsMan.NavDebugOverlay() >= 2) {
		RecordFlightDebug();
		DrawMoverDebug();
	} else if (!mover.debugFlight.trail.empty()) {
		mover.debugFlight = RouteMover::FlightRecord();
	}
	if (!mover.begun) {
		mover.begun = true;
		mover.lastProgressPos = m_Pos;
	}
	const float h = m_CharHeight;
	const float feet = m_FeetBelowPos >= 0.0F ? m_FeetBelowPos : h * 0.2F;
	const bool standardJet = m_pJetpack && m_pJetpack->GetJetpackType() == AEJetpack::JetpackType::Standard;
	// On the ground: floor under the middle or under either side of the body (8.0's look; one standing on a ledge's very edge, or on a
	// door's leaf, has nothing under its middle), or at rest. (Taken for in the air with no floor under the middle, a unit standing on a
	// hatch's lip was handed to the pilot, whose lean moves nothing on the ground, and stood there the minute.)
	float floorHere = FloorUnder(m_Pos, h * 0.5F + h * 0.33F);
	for (float side: {-h * 0.12F, h * 0.12F}) {
		if (floorHere < 0.0F) {
			floorHere = FloorUnder(m_Pos + Vector(side, 0.0F), h * 0.5F + h * 0.33F);
		}
	}
	if (floorHere < 0.0F && m_Vel.MagnitudeIsLessThan(0.3F) && m_Status == STABLE) {
		floorHere = m_Pos.m_Y + feet;
	}
	// In liquid with the body under (ActorWater's depth 2 or more, LM-4): swum, or the bottom walked by what sinks; not in the air, whatever
	// is under it (FloorUnder sees through liquid to the bottom, and a swimmer with none within reach was flown by the pilot).
	const int liquidDepth = GetLiquidDepth();
	const bool inLiquid = liquidDepth >= 2;
	if (!inLiquid) {
		mover.swimming = false;
	}
	const bool airborne = floorHere < 0.0F && !inLiquid;
	Controller& ctrl = m_Controller;

	// Nothing to go to.
	if (m_Waypoints.empty() && m_MovePath.empty() && !m_HasMovePathGoal && !g_MovableMan.ValidMO(m_pMOMoveTarget)) {
		return RouteMover::Arrived;
	}
	// A tactical move (a step into cover, a crawl forward to shoot; see TacticalMoveTo) has the legs: the route waits for it.
	if (m_Tactical.active) {
		mover.progressTimer.Reset();
		return RouteMover::Moving;
	}
	// Knocked over, or getting up: no keys (the legs' walking while down was the flailing), and not stuck for it.
	if (m_Status != STABLE || m_GettingUp) {
		mover.progressTimer.Reset();
		return RouteMover::Moving;
	}
	// A leap of the route's, come down: short of the landing (in the gap, under the lip) or past it, the leap is marked failed for a while,
	// as a flight is, so the next route crosses somewhere else or another way. (Leaps had no failure memory: one that kept falling short was
	// planned the same way after every re-path, and only the 6 s stuck handling, marking the next point, got the unit out of it.)
	if (mover.leapWatch && !m_Leaping && !airborne && mover.leapTimer.IsPastSimMS(400)) {
		mover.leapWatch = false;
		Vector toLanding = Towards(m_Pos, mover.leapTo);
		if (std::abs(toLanding.m_X) > std::max(36.0F, h * 0.6F) || toLanding.m_Y < -h * 0.6F) {
			MoverTrace("leap to " + std::to_string(static_cast<int>(mover.leapTo.m_X)) + "," + std::to_string(static_cast<int>(mover.leapTo.m_Y)) + " came down off it; a route another way");
			AvoidPathLink(mover.leapFrom, mover.leapTo, 20000.0F);
			mover.bestGap = -1.0F;
			mover.progressTimer.Reset();
			RefreshRoute();
			return RouteMover::Moving;
		}
	}

	// The route: asked for when there is none, now and then anyway (the world changes), when the next point has been out of sight a while on
	// the ground, and when stuck. A route check (the same answer, taken only if it is better) runs in the air.
	if (m_MovePath.empty() && !IsWaitingOnNewMovePath()) {
		if (m_ImpossiblePaths > 0 && mover.impossibleAnswers >= 3) {
			MoverTrace("no route to it; giving up");
			return RouteMover::Impossible;
		}
		UpdateMovePath();
		// Each answer once: for three seconds after an impossible answer UpdateMovePath only waits, and counting every tick of that wait
		// gave up on the goal in three ticks, before it had been asked again at all.
		if (m_ImpossiblePaths > 0 && m_ImpossiblePaths != mover.impossibleSeen) {
			++mover.impossibleAnswers;
		}
		mover.impossibleSeen = m_ImpossiblePaths;
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
	mover.impossibleSeen = m_ImpossiblePaths;

	// Arrived: the route walked out and the last waypoint, on its floor, within reach.
	if (m_MovePath.empty() && !mover.flight.active) {
		return RouteMover::Moving;
	}
	// A leg done with more waypoints queued: the route to this one is dropped, so the next tick asks for the route to the next. (Arrival was
	// only ever judged against the last waypoint queued, so a unit at the end of its first leg stood there, hopped at 2.5 s and at 6 s
	// marked its own goal avoided for its whole team before the stuck handling's re-path loaded the next leg.)
	if (m_MovePath.size() <= 1 && !mover.flight.active && !m_Waypoints.empty() && m_HasMovePathGoal && !g_MovableMan.ValidMO(m_pMOMoveTarget)) {
		Vector legGround = Solid(m_MovePathGoal.m_X, m_MovePathGoal.m_Y) ? m_MovePathGoal : g_SceneMan.MovePointToGround(m_MovePathGoal, static_cast<int>(h * 0.2F), 4);
		if (Towards(m_Pos, legGround).MagnitudeIsLessThan(std::min(h * 0.4F, m_MoveProximityLimit * 1.5F)) && !airborne) {
			MoverTrace("leg done; on to the next waypoint");
			m_MovePath.clear();
			m_MovePathKinds.clear();
			m_HasMovePathGoal = false;
			mover.bestGap = -1.0F;
			mover.progressTimer.Reset();
			return RouteMover::Moving;
		}
	}
	if (m_MovePath.size() <= 1 && !mover.flight.active && m_Waypoints.empty() && !g_MovableMan.ValidMO(m_pMOMoveTarget)) {
		Vector goal = GetLastAIWaypoint();
		Vector goalGround = Solid(goal.m_X, goal.m_Y) ? goal : g_SceneMan.MovePointToGround(goal, static_cast<int>(h * 0.2F), 4);
		Vector toGoal = Towards(m_Pos, goalGround);
		// (Within reach is the waypoint's own limit and a little: a unit standing 42 px from a point it called reached was, to everything
		// waiting on it, 42 px short.)
		if (toGoal.MagnitudeIsLessThan(std::min(h * 0.4F, m_MoveProximityLimit * 1.5F)) && !airborne && m_Vel.MagnitudeIsLessThan(2.0F)) {
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
		// (In liquid, across only: the route's points are on the surface, and a sinker walking the bottom under them, or a swimmer bobbing,
		// gets no nearer in height whatever it does.)
		Vector toFront = Towards(m_Pos, m_MovePath.front());
		float gap = inLiquid ? std::abs(toFront.m_X) : toFront.GetMagnitude();
		if (mover.bestGap < 0.0F || gap < mover.bestGap - 4.0F || mover.flight.active) {
			mover.bestGap = gap;
			mover.progressTimer.Reset();
		}
	}
	bool stuck = mover.progressTimer.IsPastSimMS(2500);
	if (stuck && mover.progressTimer.IsPastSimMS(6000)) {
		// Long stuck: the route asked for afresh from here, and the place avoided.
		AvoidPathPoint(m_MovePath.empty() ? m_Pos : m_MovePath.front(), 20000.0F);
		// Stuck again on a step to much the same place (within a node and a half, inside the minute): the one big step there isn't working
		// for this unit, so that step (from here to there: the flight's take-off and landing when there is one ahead, else the next point) is
		// made dearer, more each time, until a route of smaller steps to the same place wins: onto the ledge first and then over, rather
		// than up and over in one. Only that step: the steps of a route by way of somewhere else start or end elsewhere and cost the same,
		// and the rest of the route stands. (Avoided only lightly, a unit was sent at the same jump time after time, where waypoints placed
		// by hand broke it into two easy ones.)
		{
			Vector from = m_Pos;
			Vector to = m_MovePath.empty() ? m_Pos : m_MovePath.front();
			Vector landing;
			Vector takeOff;
			float landingFloorY = 0.0F;
			int pointsToLanding = 0;
			if (FindLanding(landing, landingFloorY, pointsToLanding, takeOff)) {
				from = takeOff;
				to = landing;
				// (The climb is one step in the route, from the take-off to the top rung beside the landing: marking the route's next point,
				// the top of the climb that every way up there passes, left the same climb the cheapest, and the unit stood at the same spot.)
				AvoidPathLink(takeOff, landing, 20000.0F);
			}
			float near = static_cast<float>(g_SettingsMan.GetPathFinderGridNodeSize()) * 1.5F;
			if (mover.stuckLevel > 0 && Towards(mover.stuckSpot, to).MagnitudeIsLessThan(near) && !mover.stuckSpotTimer.IsPastSimMS(60000)) {
				++mover.stuckLevel;
			} else {
				mover.stuckLevel = 1;
				mover.stuckSpot = to;
				mover.stuckSpotTimer.Reset();
			}
			if (mover.stuckLevel >= 2) {
				// (Each mark is another 25 on that step: two the second time, four the third and after.)
				int marks = mover.stuckLevel == 2 ? 2 : 4;
				for (int k = 0; k < marks; ++k) {
					AvoidPathLink(from, to, 30000.0F);
				}
				MoverTrace("stuck again on the step to " + std::to_string(static_cast<int>(to.m_X)) + "," + std::to_string(static_cast<int>(to.m_Y)) + " (" + std::to_string(mover.stuckLevel) + " times); smaller steps there");
			} else {
				MoverTrace("stuck; new route");
			}
		}
		RefreshRoute();
		mover.flight = RouteMover::Flight();
		mover.bestGap = -1.0F;
		mover.progressTimer.Reset();
		// (A remedy still being tried when the re-path came didn't work: noted so, not taken for having worked when the timer starts again.)
		if (mover.remedy >= 0) {
			RememberStuckRemedy(mover.remedySpot, mover.remedy, false);
			mover.remedy = -1;
		}
		mover.remedyTried = 0;
		return RouteMover::Moving;
	}
	// Now and then, or the next point out of sight on the ground for a second: a fresh route.
	if (!mover.flight.active && !m_MovePath.empty()) {
		Vector toPoint = Towards(m_Pos, m_MovePath.front());
		Vector obstacle;
		Vector free;
		// (Terrain only: another unit between us and the point, as in any squad in a corridor, asked for a new route every second, and each
		// one could begin from a node behind us; our doors open as we come.)
		bool inSight = !g_SceneMan.CastStrengthRay(m_Pos, toPoint, 5.0F, obstacle, 4, MaterialColorKeys::g_MaterialDoor);
		// (A ladder's next point is often out of sight, over a lip or down a hatch: the climb goes to it, not a new route every second.)
		bool ladderStep = !m_MovePathKinds.empty() && m_MovePathKinds.front() == PathStepKind::Ladder;
		// (Nor a dig's: its point is in the ground being dug, out of sight until the cut reaches it. With a new route every second, the
		// digger never got as far as firing, and a unit routed through a bank of earth stood at its face for good.)
		bool digStep = ((!m_MovePathKinds.empty() && m_MovePathKinds.front() == PathStepKind::Dig) || mover.digging) && HasObjectInGroup("Tools - Diggers");
		// (Nor from under the water: the bank's point is over the lip from down there.)
		if (inSight || airborne || inLiquid || ladderStep || digStep || DoorAhead(m_MovePath.front())) {
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

	// A door of ours (or no one's) across the way to a point, not open: held short of it, on the ground at its sensor, in the air hovering
	// (a hatch across a shaft: its leaves gib what is in their sweep, and a unit flown up into them died at full health). Given up on
	// after a while (walked or flown into) for 5 s. @return Whether holding for it this tick.
	auto holdForDoor = [&](const Vector& towards, bool inAir) -> bool {
		ADoor* door = DoorAhead(towards);
		if (!door) {
			return false;
		}
		ADoor::DoorState state = door->GetDoorState();
		if (state == ADoor::OPEN || state == ADoor::OPENING) {
			mover.doorWaitID = 0;
			return false;
		}
		if (door->GetUniqueID() == mover.doorIgnoreID) {
			return false;
		}
		if (door->GetUniqueID() != mover.doorWaitID) {
			mover.doorWaitID = door->GetUniqueID();
			mover.doorWaitTimer.Reset();
		}
		if (mover.doorWaitTimer.IsPastSimMS(inAir ? 3000 : 2000)) {
			MoverTrace(inAir ? "door didn't open; flying into it" : "door didn't open; walking into it");
			mover.doorIgnoreID = door->GetUniqueID();
			mover.doorIgnoreTimer.Reset();
			return false;
		}
		mover.progressTimer.Reset();
		if (inAir) {
			// A hover where we are: lit when sinking, the stick against any drift.
			ctrl.SetState(BODY_JUMP, m_Vel.m_Y > 0.3F && m_pJetpack && m_pJetpack->GetJetTimeLeft() > 0.0F);
			ctrl.SetAnalogMove(Vector(std::clamp(-m_Vel.m_X * 0.4F, -0.6F, 0.6F), -1.0F));
			return true;
		}
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
		return true;
	};
	if (mover.doorIgnoreID != 0 && mover.doorIgnoreTimer.IsPastSimMS(5000)) {
		mover.doorIgnoreID = 0;
	}

	// ---- On a ladder: up or down it to the route's point, and off its side or over its top to one beside. ----
	if (IsClimbingLadder()) {
		mover.flight = RouteMover::Flight();
		mover.fuelWaiting = false;
		if (!m_MovePath.empty()) {
			Vector to = Towards(m_Pos, m_MovePath.front());
			if (to.m_Y < -6.0F) {
				ctrl.SetState(MOVE_UP, true);
			} else if (to.m_Y > 6.0F) {
				ctrl.SetState(MOVE_DOWN, true);
			} else if (std::abs(to.m_X) > 8.0F) {
				ctrl.SetState(to.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
			}
		}
		return RouteMover::Moving;
	}

	// ---- In liquid (LM-4): swum for the point along the surface, or walked along the bottom by what sinks (the walk below), and out
	// up the bank by the hands (pressing up and into it: Actor::TryCatchLedge and TryStartMantle pull the body out over the lip). ----
	// (A flight taken off from the surface is the flight's below while it climbs out, not a coming down in the water.)
	const bool flyingOut = mover.flight.active && mover.flight.fromWater && !mover.flight.timer.IsPastSimMS(1500) && m_Vel.m_Y < 1.0F;
	// (Not for a ladder step: swum, the body stroked for the point and never pressed up or down the rungs. Every liquid holds bodies since
	// L-5, so a ladder with its foot in any pool was out of use.)
	const bool ladderStepHere = !m_MovePathKinds.empty() && m_MovePathKinds.front() == PathStepKind::Ladder;
	if (inLiquid && !m_MovePath.empty() && !flyingOut && !ladderStepHere) {
		const Vector point = m_MovePath.front();
		const Vector toPoint = Towards(m_Pos, point);
		const PathStepKind kind = m_MovePathKinds.empty() ? PathStepKind::Walk : m_MovePathKinds.front();
		// A flight that came down in the water is over: the route goes on from here.
		if (mover.flight.active) {
			MoverTrace("came down in the water; flight over");
			mover.flight = RouteMover::Flight();
			mover.fuelWaiting = false;
		}
		// Fallen in (knocked in, the floor gone, a flood) on a route that didn't mean to swim: a route from here, which takes the water in
		// its own terms and makes for a bank. (Once each time in.)
		if (!mover.swimming) {
			mover.swimming = true;
			if (kind != PathStepKind::Swim && kind != PathStepKind::Wade && !IsWaitingOnNewMovePath()) {
				MoverTrace("fell in the water; new route");
				mover.bestGap = -1.0F;
				mover.progressTimer.Reset();
				RefreshRoute();
				return RouteMover::Moving;
			}
		}
		// Out of air with the head under (four seconds of it left, for what breathes): whatever the route says, straight up for the
		// surface, stroking and jumping. (The grid only routes a sinker across water its breath does, but a unit that stood, fought or was
		// pushed about down there runs out all the same.)
		const float breath = ActorWater::GetBreathSeconds(this);
		if (breath < FLT_MAX && liquidDepth >= 3 && GetAirLeft() * breath < 4.0F) {
			ctrl.SetState(MOVE_UP, true);
			ctrl.SetState(BODY_JUMP, true);
			if (std::abs(toPoint.m_X) > 3.0F) {
				ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
			}
			mover.progressTimer.Reset();
			if (mover.traceTimer.IsPastSimMS(1000)) {
				mover.traceTimer.Reset();
				MoverTrace("out of air; surfacing");
			}
			return RouteMover::Moving;
		}
		// At the surface with a jet, and the route going up out of the water close by (a bank higher than the hands reach, a ledge or a deck over
		// the water): flown out from here, as off a floor. (Only a bank low enough to climb got a unit out before: in open water under a cliff
		// it swam to the wall and pressed up against it.)
		if (liquidDepth == 2 && standardJet && m_pJetpack->GetJetTimeLeft() > JetRelightFuel()) {
			Vector landing;
			float landingFloorY = 0.0F;
			int pointsToLanding = 0;
			if (FindLanding(landing, landingFloorY, pointsToLanding)) {
				Vector toLanding = Towards(m_Pos, landing);
				if (toLanding.m_Y < -h * 0.5F && std::abs(toLanding.m_X) <= h * 2.5F && FlightWayClear(landing, landingFloorY)) {
					mover.flight = RouteMover::Flight();
					mover.flight.active = true;
					mover.flight.fromWater = true;
					mover.flight.landing = landing;
					mover.flight.floorY = landingFloorY;
					mover.flight.pointsToLanding = pointsToLanding;
					mover.flight.timer.Reset();
					mover.flight.totalTimer.Reset();
					mover.flight.riseTimer.Reset();
					mover.flight.startY = m_Pos.m_Y;
					mover.flight.bestY = m_Pos.m_Y;
					mover.flight.takeOff = m_Pos;
					mover.fuelWaiting = false;
					mover.settling = false;
					MoverTrace("out of the water on the jet for " + std::to_string(static_cast<int>(landing.m_X)) + "," + std::to_string(static_cast<int>(landingFloorY)));
					Vector command = PilotFlight(landing, landingFloorY);
					ctrl.SetState(BODY_JUMPSTART, true);
					ctrl.SetState(BODY_JUMP, true);
					ctrl.SetAnalogMove(Vector(command.m_X, -1.0F));
					mover.progressTimer.Reset();
					return RouteMover::Moving;
				}
			}
		}
		// A sinker with the bottom under its feet walks it: the walk below, as on any floor.
		if (!(floorHere >= 0.0F && !IsFloater())) {
			if (std::abs(toPoint.m_X) > 3.0F) {
				ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
			}
			// Up for a point over the surface (the bank): the stroke up lifts the body to the lip, and the key into the bank pulls it out.
			// Down for one well under (a dive the route means); otherwise left to the water, which holds a floater at the surface.
			if (toPoint.m_Y < -h * 0.25F) {
				ctrl.SetState(MOVE_UP, true);
			} else if (toPoint.m_Y > h * 0.6F && kind != PathStepKind::Swim) {
				ctrl.SetState(MOVE_DOWN, true);
			}
			return RouteMover::Moving;
		}
	}

	// ---- The flight: planned take-off to touchdown, flown by the pilot. ----
	if (mover.flight.active) {
		RouteMover::Flight& flight = mover.flight;
		Vector to = Towards(m_Pos, flight.landing);
		float feetY = m_Pos.m_Y + feet;
		bool onLanding = std::abs(to.m_X) < h * 0.25F && std::abs(feetY - flight.floorY) < h * 0.3F;
		bool ended = false;
		// The climb's failure tests (8.0's SharedBehaviors.ClimbUpdate): no height gained for a second while the way is up, fallen back a
		// third of a body under where it began, or too long at it (twelve seconds, and four more for each refuel). Any of them ends the
		// flight and asks for a new route from here. (With only the landing, the tank and fifteen seconds to end it, a unit pinned under a
		// lip hovered there burning the tank for the fifteen.)
		float targetY = flight.via ? flight.viaPoint.m_Y : flight.floorY - feet;
		bool climbingUp = targetY < m_Pos.m_Y - h * 0.3F && !flight.refuelling;
		if (m_Pos.m_Y < flight.bestY - 3.0F || !climbingUp || !airborne) {
			flight.bestY = std::min(flight.bestY, m_Pos.m_Y);
			flight.riseTimer.Reset();
		}
		const char* failed = nullptr;
		if (climbingUp && airborne && flight.riseTimer.IsPastSimMS(1000)) {
			failed = "no rise";
		} else if (climbingUp && flight.stages == 0 && m_Pos.m_Y > flight.startY + h * 0.3F) {
			failed = "fell back";
		} else if (flight.totalTimer.IsPastSimMS(12000 + 4000 * flight.stages)) {
			failed = "took too long";
		}
		if (failed) {
			MoverTrace(std::string("flight failed (") + failed + "); new route");
			// (This take-off for that landing is dearer for a while, for this unit and its team: the next try goes from elsewhere, or for
			// another landing; see Actor::AvoidPathLink.)
			AvoidPathLink(flight.takeOff, flight.landing, 20000.0F);
			flight = RouteMover::Flight();
			mover.bestGap = -1.0F;
			mover.progressTimer.Reset();
			RefreshRoute();
			return RouteMover::Moving;
		}
		// (A take-off from the water is still in it, not airborne, for its first moments: it ends at its own 1.5 s, as flyingOut does, rather
		// than at 1.2 s while still rising out of it, which with a weak jet began take-off after take-off.)
		if (!airborne && flight.timer.IsPastSimMS(400) && (onLanding || flight.timer.IsPastSimMS(flight.fromWater ? 1500 : 1200))) {
			ended = true;
		} else if (airborne && standardJet && m_pJetpack->GetJetTimeLeft() < 60.0F && !onLanding && !flight.refuelling) {
			// Under the landing with the tank dry: a climb in stages, as a player does up a tall shaft. The jet goes out, the tank fills as
			// the body falls (it fills as fast as it empties), and the climb goes on from lower down; each stage gains a storey or so. Ending
			// the flight here instead lost the lean, asked for a route, and began a flight again a storey lower each time (a 500 px shaft took
			// 40 s and the unit scraped a wall).
			if ((to.m_Y < -h * 0.3F || flight.via) && flight.stages < 6) {
				flight.refuelling = true;
				++flight.stages;
				MoverTrace("out of fuel under the landing; refuelling on the way down");
			} else {
				MoverTrace("out of fuel short of the landing");
				ended = true;
			}
		}
		if (flight.refuelling && standardJet) {
			// Climbing on at a fuel level, not a time: what the rest of the climb takes from here (the climb on this unit's jet, with the
			// reserve for the top). When no tank holds the rest, as soon as the tank holds the stop of the fall so far and a third of the tank
			// to climb on with; never under what the jet lights on. And whatever the level, the fall is stopped before the floor below: the
			// stop's length from the jet's push at this fuel, and the floor looked for a third past it.
			float fuel = m_pJetpack->GetJetTimeLeft();
			float total = m_pJetpack->GetJetTimeTotal();
			float rest = m_Pos.m_Y + feet - (flight.via ? flight.viaPoint.m_Y + feet : flight.floorY);
			float restNeeded = ClimbFuelNeeded(std::max(0.0F, rest), flight.via ? 300.0F : 450.0F);
			float netAccel = JetAccelAtFuel(fuel) - g_SceneMan.GetGlobalAcc().m_Y * c_PPM;
			float fallSpeed = std::max(0.0F, m_Vel.m_Y) * c_PPM;
			float stopFuel = netAccel > 1.0F ? fallSpeed / netAccel * 1000.0F : total;
			float level = restNeeded < total * 0.95F ? restNeeded : stopFuel + total / 3.0F;
			level = std::max(std::min(level, total * 0.85F), JetRelightFuel());
			float stopLength = netAccel > 1.0F ? fallSpeed * fallSpeed / (2.0F * netAccel) : h * 4.0F;
			bool floorNear = m_Vel.m_Y > 1.0F && FloorUnder(m_Pos + Vector(0.0F, feet), stopLength * 1.3F + h * 0.3F) >= 0.0F;
			if (fuel >= level || (floorNear && fuel >= JetRelightFuel())) {
				flight.refuelling = false;
				flight.timer.Reset();
				flight.bestY = m_Pos.m_Y;
				flight.riseTimer.Reset();
				MoverTrace(std::string(fuel >= level ? "refuelled; climbing on with " : "floor coming up; climbing on with ") + std::to_string(static_cast<int>(fuel)) + " of " + std::to_string(static_cast<int>(level)));
			}
		}
		if (ended) {
			if (!onLanding && !airborne) {
				MoverTrace("flight ended on the ground short of the landing");
			}
			if (onLanding) {
				// The route's points up to the landing are done with.
				PopRouteToLanding(flight.landing, flight.pointsToLanding);
				MoverTrace("landed");
			}
			// Down again short of a landing above: the climb failed (fallen back down the hatch, or onto the wrong lip). A new route from
			// here, not the rest of the old one followed from where it was never meant to start.
			bool landingAbove = flight.floorY < feetY - h * 0.3F;
			Vector takeOffWas = flight.takeOff;
			Vector landingWas = flight.landing;
			flight = RouteMover::Flight();
			mover.bestGap = -1.0F;
			mover.progressTimer.Reset();
			mover.lastJetTime = -1.0;
			if (!onLanding && !airborne && landingAbove) {
				MoverTrace("down again under the landing; new route");
				AvoidPathLink(takeOffWas, landingWas, 20000.0F);
				RefreshRoute();
				return RouteMover::Moving;
			}
		} else {
			// Up a shaft: to the point over its mouth first, straight up its middle, and only then for the landing; a landing off to one side
			// is stepped across onto (below).
			if (flight.via && m_Pos.m_Y <= flight.viaPoint.m_Y + 8.0F) {
				flight.via = false;
				flight.step = std::abs(Towards(flight.viaPoint, flight.landing).m_X) >= 10.0F;
				flight.holdY = flight.viaPoint.m_Y;
				flight.stepTimer.Reset();
				MoverTrace(flight.step ? "out of the shaft; stepping across" : "out of the shaft; for the landing");
			}
			// The step off the top (8.0's ClimbUpdate "step"): the height held, and only once the way across is clear at foot and shin height
			// does it move across, at a walking pace, until there is floor under the feet; while the feet would foul the lip, it rises a pixel
			// at a time. (Handed to the pilot, which flies for the landing's point and brakes for its floor, a unit hung on the lip.)
			if (flight.step && !flight.refuelling) {
				float sideways = Towards(flight.viaPoint, flight.landing).m_X;
				float side = sideways > 0.0F ? 1.0F : -1.0F;
				const char* stepFailed = nullptr;
				if (flight.stepTimer.IsPastSimMS(2500)) {
					stepFailed = "couldn't step across";
				} else if (m_Pos.m_Y > flight.holdY + h * 0.4F) {
					stepFailed = "dropped from the top";
				}
				if (stepFailed) {
					MoverTrace(std::string("flight failed (") + stepFailed + "); new route");
					AvoidPathLink(flight.takeOff, flight.landing, 20000.0F);
					flight = RouteMover::Flight();
					mover.bestGap = -1.0F;
					mover.progressTimer.Reset();
					RefreshRoute();
					return RouteMover::Moving;
				}
				Vector reach(side * (std::abs(to.m_X) + h * 0.15F), 0.0F);
				Vector hit;
				bool feetClear = !g_SceneMan.CastStrengthRay(m_Pos + Vector(0.0F, feet - 2.0F), reach, 5.0F, hit, 3, MaterialColorKeys::g_MaterialDoor) && !g_SceneMan.CastStrengthRay(m_Pos + Vector(0.0F, h * 0.1F), reach, 5.0F, hit, 3, MaterialColorKeys::g_MaterialDoor);
				if (!feetClear) {
					flight.holdY = std::max(flight.floorY - feet - h * 0.3F, flight.holdY - 1.0F);
				}
				float across = Towards(flight.viaPoint, m_Pos).m_X * side;
				bool floorUnderFeet = FloorUnder(m_Pos + Vector(0.0F, feet - 2.0F), h * 0.3F) >= 0.0F;
				if (floorUnderFeet && across >= std::min(std::abs(sideways), h * 0.25F)) {
					PopRouteToLanding(flight.landing, flight.pointsToLanding);
					MoverTrace("stepped off onto the landing");
					flight = RouteMover::Flight();
					mover.bestGap = -1.0F;
					mover.progressTimer.Reset();
					ctrl.SetState(side > 0.0F ? MOVE_RIGHT : MOVE_LEFT, true);
					return RouteMover::Moving;
				}
				// The height: lit when below it and not rising fast, or sinking near it.
				float error = m_Pos.m_Y - flight.holdY; // (Positive when below.)
				bool jet = (error > 0.0F && m_Vel.m_Y > -1.5F) || (error > -6.0F && m_Vel.m_Y > 1.0F);
				ctrl.SetState(BODY_JUMP, jet && standardJet && m_pJetpack->GetJetTimeLeft() > 0.0F);
				// Sideways by speed: across at 2.5 m/s with the way clear, else kept over the column.
				float wantVelX = feetClear ? side * 2.5F : std::clamp(Towards(m_Pos, flight.viaPoint).m_X / 6.0F, -1.0F, 1.0F);
				ctrl.SetAnalogMove(Vector(std::clamp((wantVelX - m_Vel.m_X) * 0.6F, -1.0F, 1.0F), -1.0F));
				if (feetClear && (!airborne || m_Vel.MagnitudeIsLessThan(0.6F))) {
					ctrl.SetState(side > 0.0F ? MOVE_RIGHT : MOVE_LEFT, true);
				}
				flight.riseTimer.Reset();
				mover.progressTimer.Reset();
				return RouteMover::Moving;
			}
			if (!flight.refuelling && holdForDoor(flight.via ? flight.viaPoint : flight.landing, true)) {
				flight.riseTimer.Reset();
				return RouteMover::Moving;
			}
			Vector command = flight.via ? PilotFlight(flight.viaPoint, -1.0F) : PilotFlight(flight.landing, flight.floorY);
			if (flight.refuelling) {
				// Falling with the jet out: a lean only, to stay under the landing (or the shaft's middle), off the walls.
				float dx = Towards(m_Pos, flight.via ? flight.viaPoint : flight.landing).m_X;
				command = Vector(std::clamp(dx / (h * 0.5F), -0.6F, 0.6F), 0.0F);
			}
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

	// ---- In the air on a leap: the arc is the legs', steered for the point with the body's air control. The jet only to save a leap
	// coming down short of a landing above (falling, the feet under its floor and well out from it): the pilot below has it then. ----
	if (IsLeaping()) {
		bool short_ = m_Vel.m_Y > 0.0F && pointFloor >= 0.0F && m_Pos.m_Y + feet > pointFloor + 4.0F && std::abs(toPoint.m_X) > h * 0.2F;
		if (!(short_ && standardJet && m_pJetpack->GetJetTimeLeft() > 200.0F)) {
			if (std::abs(toPoint.m_X) > 3.0F) {
				ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
			}
			mover.progressTimer.Reset();
			return RouteMover::Moving;
		}
		if (mover.traceTimer.IsPastSimMS(1000)) {
			mover.traceTimer.Reset();
			MoverTrace("leap coming down short; the jet saves it");
		}
	}

	// ---- In the air with no flight: the pilot flies for the point, braking for its floor. ----
	if (airborne) {
		if (standardJet) {
			Vector landing;
			float landingFloorY = 0.0F;
			int pointsToLanding = 0;
			if (m_pJetpack->GetJetTimeLeft() > 200.0F && FindLanding(landing, landingFloorY, pointsToLanding) && !(std::abs(Towards(m_Pos, landing).m_X) < h * 0.5F && landingFloorY > m_Pos.m_Y && !ctrl.IsState(BODY_JUMP))) {
				mover.flight = RouteMover::Flight();
				mover.flight.active = true;
				mover.flight.landing = landing;
				mover.flight.floorY = landingFloorY;
				mover.flight.pointsToLanding = pointsToLanding;
				mover.flight.timer.Reset();
				mover.flight.totalTimer.Reset();
				mover.flight.riseTimer.Reset();
				mover.flight.startY = m_Pos.m_Y;
				mover.flight.bestY = m_Pos.m_Y;
				mover.flight.takeOff = m_Pos;
				MoverTrace("flight to " + std::to_string(static_cast<int>(landing.m_X)) + "," + std::to_string(static_cast<int>(landingFloorY)));
			}
			if (holdForDoor(point, true)) {
				return RouteMover::Moving;
			}
			Vector command = PilotFlight(point, pointFloor);
			mover.pilotedFallTick = g_TimerMan.GetSimUpdateCount();
			// (A drop onto the point's floor is left to gravity until the brake is wanted: the pilot's safe-fall rule asks for the jet only then.)
			ctrl.SetState(BODY_JUMP, command.m_Y > 0.5F);
			ctrl.SetAnalogMove(Vector(command.m_X, -1.0F));
		} else {
			// No jet: over a drop deeper than the body lands from unhurt (one the route didn't mean: knocked off, or the floor gone), held
			// straight rather than steered for the point, so it lands square on its feet and doesn't clip a lip on the way down (LM-9).
			float maxFall = GetMaxSafeFallHeight();
			bool deepDrop = maxFall < FLT_MAX && FloorUnder(m_Pos, maxFall + h) < 0.0F;
			if (!deepDrop) {
				ctrl.SetState(toPoint.m_X < -3.0F ? MOVE_LEFT : MOVE_RIGHT, std::abs(toPoint.m_X) > 3.0F);
			} else if (mover.traceTimer.IsPastSimMS(1000)) {
				mover.traceTimer.Reset();
				MoverTrace("falling further than is safe; held straight");
			}
		}
		return RouteMover::Moving;
	}

	// ---- On the ground. ----
	const std::optional<Vector> ladder = LadderNear(m_Pos, h * 0.2F, h * 0.3F);
	const float above = -toPoint.m_Y;

	// A door of ours in the way: closed, waited for short of it, on its sensor; given up on after 2 s (walked into) for 5 s.
	if (holdForDoor(point, false)) {
		return RouteMover::Moving;
	}

	// ---- A leap: walked to the take-off, the edge of the gap or the foot of the lip, and leapt from there with the move key held (the
	// push forward goes the way the key does). At the take-off and not yet able to leap (the legs gather a moment after a landing), it
	// waits there rather than walking off the edge. Not getting nearer for a second short of the take-off, it leaps from where it is. ----
	if (kind == PathStepKind::Leap && std::abs(toPoint.m_X) > 3.0F) {
		const float direction = toPoint.m_X < 0.0F ? -1.0F : 1.0F;
		const float standing = std::max(16.0F, h * 0.44F);
		const float floorY = std::min(floorHere, m_Pos.m_Y + feet);
		float floorAhead = FloorUnder(m_Pos + Vector(direction * h * 0.25F, 0.0F), h * 0.9F);
		bool edge = floorAhead < 0.0F || floorAhead > floorY + h * 0.3F;
		Sensed sensed = SenseAhead(direction, floorY, standing);
		bool lip = sensed.any && sensed.distance < h * 0.3F;
		bool stalled = mover.progressTimer.IsPastSimMS(1000);
		if (edge || lip || stalled) {
			if (CanLeap()) {
				ctrl.SetState(BODY_LEAP, true);
				ctrl.SetState(direction < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
				mover.progressTimer.Reset();
				mover.leapWatch = true;
				mover.leapFrom = m_Pos;
				mover.leapTo = point;
				mover.leapTimer.Reset();
				MoverTrace(std::string("leap ") + (edge ? "from the edge" : (lip ? "onto the lip" : "from here")) + " for " + std::to_string(static_cast<int>(point.m_X)) + "," + std::to_string(static_cast<int>(point.m_Y)));
				return RouteMover::Moving;
			}
			if (m_ProneState != NOTPRONE) {
				mover.standUp = true;
				mover.standUpTimer.Reset();
			}
			if (edge) {
				return RouteMover::Moving;
			}
		}
		ctrl.SetState(direction < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
		return RouteMover::Moving;
	}

	// ---- A mantle: walked into the ledge with the move key held, which pulls the body up onto it (Actor::TryStartMantle starts the pull
	// when the way on is blocked and there is room up and over). No jet. While pulling, the pull has the body. ----
	if (kind == PathStepKind::Mantle && std::abs(toPoint.m_X) > 3.0F) {
		if (!IsMantling()) {
			ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
		} else {
			mover.progressTimer.Reset();
		}
		return RouteMover::Moving;
	}

	// A dig step (ground this unit's digger cuts, on the route): the digger out, aimed along the way and swept a little either side, fired
	// while there is ground within reach ahead, and the legs on into the cut once its first part is clear; put away again when the way is
	// open. (The script's follower did this for diggers; now the engine's does, and diggers follow routes like everyone else.)
	// It digs only what its digger cuts, and not for ever: ground ahead harder than that (the grid's view was stale, or the step crossed a
	// seam of stone), or standing at the face three times as long as a node of it should take, and the step is marked for this unit and its
	// side and a route asked for afresh, which goes round, or through somewhere softer. (With no limit, a unit sent at ground its digger only
	// scratched stood at the face firing for ever: the progress timer was reset every tick it dug, so the stuck remedies never ran.)
	// A unit with a digger stuck on a step of another kind, with ground it cuts between it and the point, digs too: what is left of a cut
	// (the top of a plug, debris fallen back in) reads to the grid as a lip to crawl under or hop, and the unit was stuck at it for good.
	const bool hasDigger = HasObjectInGroup("Tools - Diggers");
	bool digRefused = mover.digRefusedSet && !mover.digRefusedTimer.IsPastSimMS(30000) && Towards(mover.digRefused, point).MagnitudeIsLessThan(1.0F);
	if (kind != PathStepKind::Dig && !mover.digging && hasDigger && !digRefused && mover.progressTimer.IsPastSimMS(2000) && kind != PathStepKind::Ladder && kind != PathStepKind::Door && toPoint.MagnitudeIsLessThan(h)) {
		mover.unstickDig = point;
		mover.unstickDigSet = true;
	}
	const bool unstickDig = mover.unstickDigSet && Towards(mover.unstickDig, point).MagnitudeIsLessThan(1.0F);
	if ((kind == PathStepKind::Dig || unstickDig) && !digRefused && hasDigger) {
		Vector way = toPoint;
		if (way.MagnitudeIsGreaterThan(1.0F)) {
			way.Normalize();
			Vector hit;
			// What the step goes through: towards its point and no further, so not the floor under the point or a roof over it, which the
			// digger's sweep scratches but the step doesn't cross.
			const float gap = toPoint.GetMagnitude();
			const Vector along = way * std::min(h * 0.5F, gap);
			bool groundAhead = g_SceneMan.CastStrengthRay(m_Pos, along, 5.0F, hit, 2, MaterialColorKeys::g_MaterialDoor);
			const float cuts = groundAhead ? EstimateDigStrength() : 0.0F;
			if (!groundAhead) {
				mover.unstickDigSet = false;
			}
			if (groundAhead && (!mover.digging || Towards(mover.digPoint, point).MagnitudeIsGreaterThan(1.0F))) {
				// A new cut, or on to the next point of this one: the time it may take from here.
				mover.digPoint = point;
				mover.digTimer.Reset();
				mover.digBestGap = gap;
				float hardest = g_SceneMan.CastMaxStrengthRay(m_Pos, m_Pos + along, 2);
				mover.digBudgetMS = std::max(8000.0, 3000.0 * static_cast<double>(PathFinder::DigSecondsPerNode(PathFinder::DigHardness(hardest, cuts))));
			} else if (groundAhead && gap < mover.digBestGap - 4.0F) {
				// Getting on into the cut: the time again from here. (Only standing at the face for the whole of it is given up on, not a cut
				// that is slow but going: a unit most of the way through a plug was sent the long way round.)
				mover.digBestGap = gap;
				mover.digTimer.Reset();
			}
			const char* giveUp = nullptr;
			if (groundAhead && g_SceneMan.CastStrengthRay(m_Pos, along, cuts, hit, 2, MaterialColorKeys::g_MaterialDoor)) {
				giveUp = "ground ahead harder than the digger cuts";
			} else if (groundAhead && mover.digTimer.IsPastSimMS(mover.digBudgetMS)) {
				giveUp = "the cut is taking too long";
			}
			if (giveUp && kind != PathStepKind::Dig) {
				// (Dug only to get unstuck: the step itself is no dig, so it isn't marked, and the stuck remedies have it again.)
				MoverTrace(std::string("dig to get unstuck given up: ") + giveUp);
				mover.unstickDigSet = false;
				mover.digRefused = point;
				mover.digRefusedSet = true;
				mover.digRefusedTimer.Reset();
				groundAhead = false;
			} else if (giveUp) {
				MoverTrace(std::string("dig given up: ") + giveUp + "; a way round");
				// (Four marks, a hundred on that step: about what the dig itself was priced at, so the way round wins unless there is none.)
				for (int k = 0; k < 4; ++k) {
					AvoidPathLink(m_Pos, point, 30000.0F);
				}
				mover.digRefused = point;
				mover.digRefusedSet = true;
				mover.digRefusedTimer.Reset();
				if (mover.digging) {
					mover.digging = false;
					EquipFirearm(true);
				}
				RefreshRoute();
				mover.progressTimer.Reset();
				return RouteMover::Moving;
			}
			if (groundAhead && EquipDiggingTool(true)) {
				mover.digging = true;
				if (std::abs(way.m_X) > 0.15F) {
					m_HFlipped = way.m_X < 0.0F;
				}
				const float dt = std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F);
				mover.digSweep += (mover.digSweepUp ? 1.0F : -1.0F) * 2.5F * dt;
				if (std::abs(mover.digSweep) > 0.4F) {
					mover.digSweep = std::clamp(mover.digSweep, -0.4F, 0.4F);
					mover.digSweepUp = !mover.digSweepUp;
				}
				// (The aim is relative to the facing: up positive, the way's angle off level.)
				float angle = std::atan2(-way.m_Y, std::abs(way.m_X));
				SetAimAngle(std::clamp(angle + mover.digSweep, -GetAimRange(), GetAimRange()));
				ctrl.SetState(WEAPON_FIRE, true);
				// On into the cut once its first part is clear at the chest.
				if (!g_SceneMan.CastStrengthRay(m_Pos, way * (h * 0.3F), 5.0F, hit, 2, MaterialColorKeys::g_MaterialDoor) && std::abs(toPoint.m_X) > 3.0F) {
					ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
				}
				mover.progressTimer.Reset();
				return RouteMover::Moving;
			}
		}
	}
	if (mover.digging) {
		mover.digging = false;
		EquipFirearm(true);
	}

	// The route goes up or down a ladder from here: to the ladder's line first (a side key held is no grab), then up or down to take hold.
	if (kind == PathStepKind::Ladder && std::abs(toPoint.m_Y) > h * 0.2F) {
		if (std::abs(toPoint.m_X) > h * 0.25F) {
			ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
		} else {
			ctrl.SetState(toPoint.m_Y < 0.0F ? MOVE_UP : MOVE_DOWN, true);
			SetAimAngle(toPoint.m_Y < 0.0F ? 0.9F : -0.6F);
			// Lying down, it gets up for the ladder (the motor lets a prone stance go while standUp holds, see UpdateAIMotor): the climb
			// refuses a prone body, and an AI renews its prone stance every tick for any order but a move, so an attack, patrol or guard
			// unit that went prone once crawled to the ladder's foot and lay there pressing up.
			if (m_ProneState != NOTPRONE || m_AIStance == 2) {
				mover.standUp = true;
				mover.standUpTimer.Reset();
			}
		}
		mover.progressTimer.Reset();
		return RouteMover::Moving;
	}

	// A drop straight down from where we stand (the point well below and nearly straight under us), with floor still under the feet: a
	// step towards the side where the floor falls away. The route's point is a node's middle, which can sit 2 px from the unit at the very
	// edge of the slab beside the hole, and with no key for a point that close the unit stood on the lip for the rest of the minute (8.0's
	// GoToWpt had this; the port had lost it). Floor under either side of the body counts: one wedged on the hole's corner has its middle
	// over the hole already.
	if (!ladder && kind != PathStepKind::Jump && kind != PathStepKind::Dig && toPoint.m_Y > h * 0.3F && std::abs(toPoint.m_X) < h * 0.3F && std::abs(m_Vel.m_Y) < 1.0F) {
		auto floorAt = [&](float dx) {
			Vector hit;
			return g_SceneMan.CastStrengthRay(Vector(m_Pos.m_X + dx, m_Pos.m_Y), Vector(0.0F, h * 0.75F), 5.0F, hit, 2);
		};
		float side = h * 0.12F;
		if (floorAt(0.0F) || floorAt(-side) || floorAt(side)) {
			float reach = h * 0.25F;
			float towards = toPoint.m_X < 0.0F ? -1.0F : 1.0F;
			float holeSide = 0.0F;
			if (!floorAt(towards * reach)) {
				holeSide = towards;
			} else if (!floorAt(-towards * reach)) {
				holeSide = -towards;
			}
			if (holeSide != 0.0F) {
				if (mover.traceTimer.IsPastSimMS(1000)) {
					mover.traceTimer.Reset();
					MoverTrace(std::string("drop: floor under the feet; stepping ") + (holeSide < 0.0F ? "left" : "right") + " into the hole");
				}
				ctrl.SetState(holeSide < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
				SetAimAngle(0.0F);
				return RouteMover::Moving;
			}
		}
	}

	// A flight wanted: the point well above, or a gap in the floor on the way, as the route's legs say.
	// Only at the take-off: where the route leaves the floor, near (within half a body across and most of one up or down). FindLanding
	// looks thirty points ahead, and with the flight's rules let loose wherever there was a flight anywhere ahead, a unit in a corridor
	// stood waiting for fuel for a shaft twenty points on, or walked for the far landing's side instead of its next point, back and forth.
	// (8.0's GoToWpt began a climb only when the waypoint in hand was the jump.) Until there, the route is walked.
	Vector landing;
	float landingFloorY = 0.0F;
	int pointsToLanding = 0;
	Vector takeOff;
	bool flightAhead = standardJet && FindLanding(landing, landingFloorY, pointsToLanding, takeOff);
	mover.hasTakeOff = flightAhead;
	mover.debugTakeOff = takeOff;
	if (flightAhead) {
		Vector toTakeOff = Towards(m_Pos, takeOff);
		Vector toLandingHere = Towards(m_Pos, landing);
		bool atTakeOff = std::abs(toTakeOff.m_X) <= h * 0.5F && std::abs(toTakeOff.m_Y) <= h * 0.8F;
		// Once at a take-off, the flight's rules hold while the unit lines up for it nearby (under a shaft's middle, a step to an open
		// column), for up to four seconds. (Let go of as soon as a step took it half a body off the take-off, the walk took it back to the
		// route's point, where the step took it off again: back and forth at the foot of a shaft before every climb.)
		if (atTakeOff) {
			if (!mover.takeOffCommitted || Towards(mover.takeOffCommit, takeOff).MagnitudeIsGreaterThan(30.0F)) {
				mover.takeOffCommitTimer.Reset();
			}
			mover.takeOffCommitted = true;
			mover.takeOffCommit = takeOff;
		} else if (mover.takeOffCommitted) {
			Vector toCommit = Towards(m_Pos, mover.takeOffCommit);
			if (Towards(mover.takeOffCommit, takeOff).MagnitudeIsLessThan(30.0F) && std::abs(toCommit.m_X) <= h * 1.2F && std::abs(toCommit.m_Y) <= h * 0.8F && !mover.takeOffCommitTimer.IsPastSimMS(4000)) {
				atTakeOff = true;
			} else {
				mover.takeOffCommitted = false;
			}
		}
		// (A ledge above and to one side: from up to a body and a half short of the take-off, on the way to it, wherever the way up and
		// across is open from here, as a player jets for a ledge from a few steps back and arcs onto it. Walked to the take-off, the foot
		// of the wall, the way straight up was under the lip, and the unit pressed against the wall.)
		bool approach = !atTakeOff && std::abs(toTakeOff.m_X) <= h * 1.5F && std::abs(toTakeOff.m_Y) <= h * 0.8F && toLandingHere.m_Y < -h * 0.3F && toTakeOff.m_X * toLandingHere.m_X >= 0.0F && FlightWayClear(landing, landingFloorY);
		if (!atTakeOff && !approach) {
			flightAhead = false;
			mover.fuelWaiting = false;
		}
		// Down is walked: a landing below is reached by walking off the edge (the drop step and the walk), the pilot braking in the air,
		// as 8.0 did it. Only a long jump across, more across than down and more than a body and a half, is flown from the ground. (Flown
		// "to" a landing straight down a drop, the pilot had no jet to give and leant one way and the other over the edge, and the unit paced
		// back and forth on the lip, began the flight again every second and a half, and so for the minute; down stairs, it flew them.)
		if (flightAhead && toLandingHere.m_Y > h * 0.3F) {
			bool longJump = std::abs(toLandingHere.m_X) > h * 1.5F && std::abs(toLandingHere.m_X) > toLandingHere.m_Y;
			if (!longJump) {
				flightAhead = false;
				mover.fuelWaiting = false;
			}
		}
	} else {
		mover.takeOffCommitted = false;
		mover.noTakeOff = false;
		// (No flight ahead any more: no fuel wait either. Left set, the wall sense and the stuck hop stayed off for the rest of the order.)
		mover.fuelWaiting = false;
	}
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
		// (The route's column: its first point well above us, near; else where we stand.)
		float columnX = m_Pos.m_X;
		{
			int index = 0;
			for (const Vector& routePoint: m_MovePath) {
				if (++index > pointsToLanding) {
					break;
				}
				Vector off = Towards(m_Pos, routePoint);
				if (off.m_Y < -h * 0.3F && std::abs(off.m_X) < h * 1.5F) {
					columnX = m_Pos.m_X + off.m_X;
					break;
				}
			}
		}
		const float standingHeight = std::max(16.0F, h * 0.44F);
		float shaftMiddle = 0.0F;
		float shaftWidth = 0.0F;
		bool inShaft = toLanding.m_Y < -h * 0.3F && ShaftColumn(columnX, landingFloorY - standingHeight, shaftMiddle, shaftWidth);
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
		// Off only where the flight can begin: the way up and across open from here, a shaft to go up, or a level hop from the edge.
		// Otherwise the walk goes on, towards the landing's side of here (the route's next point is straight up, which is no key), which
		// leads to where it can: under the shaft's mouth, to the edge. Taken off wherever the landing was well above, a unit at a corridor's
		// end jetted into its ceiling, burned the tank, refilled, and did it again for the whole minute, 60 px short of the shaft it was to
		// go up; and one that waited for fuel where no flight could begin, then hopped when the wait ran out, waited again for the minute.
		bool wayUpOpen = FlightWayClear(landing, landingFloorY);
		// Round a corner: a landing above that the body can't fly to in a straight line (a lip, an overhang, a ledge's underside in the way)
		// is flown to by way of the route's own corner, as up a shaft: straight to a point at the corner, level with standing over the
		// landing's floor, then stepped across onto it. The pilot otherwise flies straight for the landing whatever the route's points
		// between, and the route's turn round the lip was cut: units flew into the underside of the ledge the route went round, where
		// the same route given as waypoints by hand, corner and all, was flown easily. The corner: the route's last point before the
		// landing that both legs are clear to, at the landing's height, or else the column over here (the way FlightWayClear tests).
		bool cornerVia = false;
		Vector cornerPoint;
		if (!inShaft && toLanding.m_Y < -h * 0.3F) {
			// (The body's outline along the line: head, feet and both sides.)
			auto bodyLineClear = [&](const Vector& from, const Vector& to) {
				Vector line = Towards(from, to);
				Vector hit;
				for (const Vector& offset: {Vector(0.0F, -h * 0.3F), Vector(0.0F, feet - 4.0F), Vector(-h * 0.15F, 0.0F), Vector(h * 0.15F, 0.0F)}) {
					if (g_SceneMan.CastStrengthRay(from + offset, line, 5.0F, hit, 2, MaterialColorKeys::g_MaterialDoor)) {
						return false;
					}
				}
				return true;
			};
			Vector standOver(landing.m_X, landingFloorY - feet - 2.0F);
			if (!bodyLineClear(m_Pos, standOver)) {
				float cornerY = landingFloorY - feet - 8.0F;
				std::vector<float> columns;
				int index = 0;
				for (const Vector& routePoint: m_MovePath) {
					if (++index >= pointsToLanding) {
						break;
					}
					columns.push_back(m_Pos.m_X + Towards(m_Pos, routePoint).m_X);
				}
				std::reverse(columns.begin(), columns.end());
				columns.push_back(m_Pos.m_X);
				for (float x: columns) {
					Vector corner(x, cornerY);
					if (corner.m_Y < m_Pos.m_Y - h * 0.3F && bodyLineClear(m_Pos, corner) && bodyLineClear(corner, standOver)) {
						cornerVia = true;
						cornerPoint = corner;
						break;
					}
				}
			}
		}
		bool canTakeOff = (levelHop && edgeAhead) || inShaft || wayUpOpen || cornerVia;
		// (Timed only within a node of the take-off (or the half body the steps about it go): walking the last node or two to it, the way up can be blocked from where the unit is
		// yet open on arrival, and a good take-off was given up on the approach.)
		Vector toTakeOffNow = Towards(m_Pos, takeOff);
		bool nearTakeOff = std::abs(toTakeOffNow.m_X) <= std::max(24.0F, h * 0.5F) && std::abs(toTakeOffNow.m_Y) <= h * 0.8F;
		if (canTakeOff || !nearTakeOff) {
			mover.noTakeOff = false;
		} else if (!mover.noTakeOff) {
			mover.noTakeOff = true;
			mover.noTakeOffTimer.Reset();
		} else if (mover.noTakeOffTimer.IsPastSimMS(2500)) {
			// No flight from this take-off for two and a half seconds, stepping about it included: the route's take-off is wrong for this
			// unit here (under the ledge's lip, the way up or across blocked). Any step from near here to near that landing is made dearer and
			// the route asked again, so the next climb goes from elsewhere: a column further out, or another way up. (Left to the stuck
			// handling, the unit stood six seconds and the route that came back was the same climb from the same spot.)
			MoverTrace("no take-off from here for " + std::to_string(static_cast<int>(landing.m_X)) + "," + std::to_string(static_cast<int>(landingFloorY)) + "; a route from elsewhere");
			AvoidPathLink(takeOff, landing, 20000.0F);
			AvoidPathLink(takeOff, landing, 20000.0F);
			mover.noTakeOff = false;
			mover.bestGap = -1.0F;
			mover.progressTimer.Reset();
			RefreshRoute();
			return RouteMover::Moving;
		}
		if (!canTakeOff) {
			mover.fuelWaiting = false;
			if (mover.traceTimer.IsPastSimMS(1000)) {
				mover.traceTimer.Reset();
				MoverTrace(std::string("no take-off here for ") + std::to_string(static_cast<int>(landing.m_X)) + "," + std::to_string(static_cast<int>(landingFloorY)) + ": shaft " + (inShaft ? "yes" : "no") + ", way up " + (wayUpOpen ? "open" : "blocked") + ", hop " + (levelHop ? (edgeAhead ? "at the edge" : "not at the edge") : "no"));
			}
			// Under a ceiling with the landing above: a step to the nearest line open from the head up to the head's height at the landing,
			// 0, a quarter and half a body either way, the landing's side first (8.0's SharedBehaviors.OpenColumnNear). Else on towards the
			// landing's side.
			if (toLanding.m_Y < -h * 0.3F) {
				float headTop = m_Pos.m_Y - h * 0.24F + 2.0F;
				float topHeadY = landingFloorY - standingHeight;
				float side = toLanding.m_X < 0.0F ? -1.0F : 1.0F;
				for (float dx: {0.0F, side * h * 0.25F, -side * h * 0.25F, side * h * 0.5F, -side * h * 0.5F}) {
					if (dx != 0.0F && ColumnOpen(m_Pos.m_X + dx, headTop, topHeadY)) {
						if (mover.traceTimer.IsPastSimMS(1000)) {
							mover.traceTimer.Reset();
							MoverTrace("under a ceiling; stepping " + std::to_string(static_cast<int>(dx)) + " to an open column");
						}
						ctrl.SetState(dx < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
						return RouteMover::Moving;
					}
					if (dx == 0.0F && ColumnOpen(m_Pos.m_X, headTop, topHeadY)) {
						break; // (Open straight up from here: the way across is what's blocked, and the walk goes on.)
					}
				}
				if (std::abs(toLanding.m_X) > 3.0F && std::abs(toPoint.m_X) <= 3.0F) {
					ctrl.SetState(toLanding.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
					return RouteMover::Moving;
				}
			}
		}
		// On the ground, with the fuel the flight takes: off. Short of it, waits (walking the while if the point is level).
		// (Never more than 85 hundredths of the tank, which a standing unit's own hops kept it from; and the wait ends after six seconds
		// whatever the tank says, on a timer of its own, since standing still here is not being stuck.)
		// (The AI movement settings: a careful unit waits for a little more than the flight takes and settles longer, a reckless one less;
		// with the fuel wait off it goes on a tenth of what it needs, with the steadying off it takes off mid-stride.)
		const float caution = g_SettingsMan.AIMoveCaution();
		float needed = std::min(FlightFuelNeeded(landing, landingFloorY) * std::clamp(0.8F + caution * 0.2F, 0.9F, 1.2F), m_pJetpack->GetJetTimeTotal() * 0.85F);
		if (!g_SettingsMan.AIWaitsForFuel()) {
			needed *= 0.1F;
		}
		if (canTakeOff && !mover.fuelWaiting) {
			mover.fuelWaiting = true;
			mover.fuelWaitTimer.Reset();
		}
		// Steadied first: a climb or a jump (anything but a hop across a gap at this level, which wants its run-up) is taken off from a stand,
		// the walk's speed let die before the jet is lit, as a player does. Taken off mid-stride, the step's speed went into the flight, often
		// the wrong way, and the pilot spent the jump fighting it: a unit took a step, threw itself off the ledge, and did it again.
		bool hop = levelHop && edgeAhead;
		// The longer the flight, the longer it settles first, as a player lines up a long jump: standing still (sideways and up and down)
		// and upright for up to most of a second, by how far the landing is past three bodies off. A small error at take-off is a big one
		// at the end of a long flight, and long flights were the ones missed and flown again.
		float flightLength = toLanding.GetMagnitude();
		float settleMS = std::clamp((flightLength - h * 3.0F) / h * 150.0F, 0.0F, 900.0F) * caution;
		// (Upright against its own standing pose, which leans for some bodies: measured against straight up, a soldier whose stance leans
		// more than the tolerance never counted as settled, and stood at the foot of the ledge for good.)
		float uprightError = std::abs(std::abs(GetRotAngle()) - std::abs(m_RotAngleTargets[STAND]));
		bool still = std::abs(m_Vel.m_X) <= (settleMS > 0.0F ? 0.3F : 0.6F) && (settleMS <= 0.0F || (std::abs(m_Vel.m_Y) <= 0.5F && uprightError < 0.2F && std::abs(m_AngularVel) < 0.6F));
		if (!still || m_Status != STABLE) {
			mover.steadyTimer.Reset();
		}
		// (Never more than a second and a half of it, whatever the body does: the wait resets the stuck handling, and unbounded, a unit that
		// never quite settled stood at its take-off with nothing to move it on.)
		bool wantsSettle = canTakeOff && !hop && g_SettingsMan.AISteadiesBeforeJet() && (!still || m_Status != STABLE || !mover.steadyTimer.IsPastSimMS(static_cast<double>(settleMS)));
		if (wantsSettle && !mover.settling) {
			mover.settling = true;
			mover.settleWaitTimer.Reset();
		} else if (!wantsSettle) {
			mover.settling = false;
		}
		if (wantsSettle && mover.settleWaitTimer.IsPastSimMS(1500.0 * std::max(1.0F, caution))) {
			wantsSettle = false;
		}
		if (wantsSettle) {
			mover.progressTimer.Reset();
			mover.hopTimer.Reset();
			if (mover.traceTimer.IsPastSimMS(1000)) {
				mover.traceTimer.Reset();
				MoverTrace("steadying before take-off");
			}
			return RouteMover::Moving;
		}
		if (canTakeOff && (m_pJetpack->GetJetTimeLeft() >= needed || mover.fuelWaitTimer.IsPastSimMS(6000))) {
			mover.fuelWaiting = false;
			{
				mover.flight = RouteMover::Flight();
				mover.flight.active = true;
				mover.settling = false;
				mover.flight.landing = landing;
				mover.flight.floorY = landingFloorY;
				mover.flight.pointsToLanding = pointsToLanding;
				mover.flight.timer.Reset();
				mover.flight.totalTimer.Reset();
				mover.flight.riseTimer.Reset();
				mover.flight.startY = m_Pos.m_Y;
				mover.flight.bestY = m_Pos.m_Y;
				mover.flight.takeOff = m_Pos;
				mover.flight.via = inShaft || cornerVia;
				if (cornerVia && !inShaft) {
					mover.flight.viaPoint = cornerPoint;
					MoverTrace("round the corner via " + std::to_string(static_cast<int>(cornerPoint.m_X)) + "," + std::to_string(static_cast<int>(cornerPoint.m_Y)));
				}
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
		} else if (canTakeOff) {
			mover.progressTimer.Reset();
			// (Settled, and now waiting on the tank: the settle starts afresh once there is fuel. Left set, its 1.5 s cap had already run out
			// by then and the take-off went without settling at all.)
			mover.settling = false;
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
	// (Not through a floor under the feet: see UpdateLadderInput.)
	bool floorUnderFeet = false;
	for (float dx: {-4.0F, 0.0F, 4.0F}) {
		floorUnderFeet = floorUnderFeet || SolidNotLadder(m_Pos.m_X + dx, m_Pos.m_Y + feet + 2.0F);
	}
	if (ladder && !floorUnderFeet && toPoint.m_Y > h * 0.3F && std::abs(toPoint.m_X) < h * 0.4F) {
		ctrl.SetState(MOVE_DOWN, true);
		SetAimAngle(-c_HalfPI);
		mover.progressTimer.Reset();
		return RouteMover::Moving;
	}

	// ---- The walk. ----
	bool crawl = kind == PathStepKind::Crawl;
	// Head room here when standing: the standing height up from the floor; too little, or a crawl step near, and it goes prone.
	float standing = std::max(16.0F, h * 0.44F);
	// (The floor never deeper than the standing feet: over the edge of a drop the floor is further down, and a look from there sat at chest
	// height and read a knee-high step ahead as no head room.)
	float floorY = floorHere >= 0.0F ? std::min(floorHere, m_Pos.m_Y + feet) : m_Pos.m_Y + feet;
	float topHeadY = std::min(m_Pos.m_Y - 4.0F, floorY - standing);
	Vector heading = toPoint.GetNormalized() * (h * 0.5F);
	Vector hit;
	bool noRoomHere = g_SceneMan.CastStrengthRay(m_Pos, Vector(0.0F, topHeadY - m_Pos.m_Y), 5.0F, hit, 4, MaterialColorKeys::g_MaterialDoor);
	bool noRoomAhead = g_SceneMan.CastStrengthRay(Vector(m_Pos.m_X, topHeadY), heading, 5.0F, hit, 4, MaterialColorKeys::g_MaterialDoor);
	{
		// (Not the ladders' rungs, which the body passes: see AHuman::LearnFlight.)
		if (noRoomAhead && g_SceneMan.GetTerrMatter(static_cast<int>(hit.m_X), static_cast<int>(hit.m_Y)) == LadderMaterialID()) {
			noRoomAhead = false;
		}
	}
	bool crawlNear = crawl && toPoint.MagnitudeIsLessThan(h * 0.65F);
	// What is a short stride ahead, the body's whole outline looked at (see SenseAhead): the walk's own eyes, besides the route's.
	Sensed sensed;
	if (std::abs(toPoint.m_X) > 3.0F && kind != PathStepKind::Stairs && kind != PathStepKind::Scramble) {
		sensed = SenseAhead(toPoint.m_X < 0.0F ? -1.0F : 1.0F, floorY, standing);
	}
	if (sensed.gapUnder) {
		noRoomAhead = true;
	}
	// 8.0's crawl rules: only for a way on that is fairly flat (within 30 degrees); a steep one is a climb, and a body lying down may not jet,
	// so it stands, unless there is no room to stand right here (the mouth of a low tunnel: stood up for a point above, a unit put its head
	// into the slab over it). Kept down a moment after the way looks clear, or a crawl through a slot was stood up in the middle of.
	bool steep = std::abs(toPoint.m_Y) > std::abs(toPoint.m_X) * 0.577F;
	// The crouch first (LM-1): a crouch step near, or too little room to stand here or a half-body ahead but room crouched (the same two
	// rays at the crouched head's height, and the sense ahead for the crouched body), is walked ducking, not crawled. Only what a crouch
	// doesn't fit under, or a crawl step, lays the body down.
	const float crouched = GetCrouchHeight();
	bool crouchStepNear = kind == PathStepKind::Crouch && toPoint.MagnitudeIsLessThan(h * 0.65F);
	bool crouchFits = false;
	Sensed sensedCrouched;
	if (crouched < standing - 1.0F && !crawlNear && (crouchStepNear || noRoomHere || noRoomAhead)) {
		float topCrouchedY = std::min(m_Pos.m_Y - 4.0F, floorY - crouched);
		bool roomHere = !g_SceneMan.CastStrengthRay(m_Pos, Vector(0.0F, topCrouchedY - m_Pos.m_Y), 5.0F, hit, 4, MaterialColorKeys::g_MaterialDoor);
		bool roomAhead = !g_SceneMan.CastStrengthRay(Vector(m_Pos.m_X, topCrouchedY), heading, 5.0F, hit, 4, MaterialColorKeys::g_MaterialDoor);
		if (!roomAhead && g_SceneMan.GetTerrMatter(static_cast<int>(hit.m_X), static_cast<int>(hit.m_Y)) == LadderMaterialID()) {
			roomAhead = true;
		}
		if (roomHere && roomAhead && std::abs(toPoint.m_X) > 3.0F && kind != PathStepKind::Stairs) {
			sensedCrouched = SenseAhead(toPoint.m_X < 0.0F ? -1.0F : 1.0F, floorY, crouched);
			roomAhead = !sensedCrouched.gapUnder && !(sensedCrouched.wall && sensedCrouched.rise < crouched - 2.0F);
		}
		crouchFits = roomHere && roomAhead;
	}
	bool prone = false;
	if (steep && !(m_ProneState == PRONE && noRoomHere)) {
		prone = false;
	} else if (crawlNear || ((noRoomHere || noRoomAhead) && !crouchFits)) {
		prone = true;
		mover.proneHoldTimer.Reset();
	} else {
		prone = m_ProneState == PRONE && !mover.proneHoldTimer.IsPastSimMS(400);
	}
	if (prone) {
		ctrl.SetState(BODY_PRONE, true);
	}
	bool crouch = false;
	if (!prone && kind == PathStepKind::Scramble) {
		// Up a rough slope too steep for stairs (LM-10): walked at crouched, so the body leans into the face and the arms find holds and
		// climb (AHuman's arm climbing on the walk), not jetted or hopped.
		crouch = true;
		mover.crouchHoldTimer.Reset();
	} else if (!prone && !steep) {
		if (crouchFits) {
			crouch = true;
			mover.crouchHoldTimer.Reset();
		} else {
			crouch = mover.crouching && !mover.crouchHoldTimer.IsPastSimMS(400);
		}
	}
	mover.crouching = crouch;
	if (crouch) {
		ctrl.SetState(BODY_CROUCH, true);
		// (The low thing ahead as the crouched body sees it: the beam being ducked under is no gap to crawl. What stands on the floor is
		// left as the standing body saw it, a step or a low obstacle to hop, as before.)
		if (sensed.gapUnder) {
			sensed = sensedCrouched;
		}
	}
	if (std::abs(toPoint.m_X) > 3.0F) {
		ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
	} else if (kind == PathStepKind::Scramble && above > h * 0.1F) {
		// (Under the top of a scramble, still on the face: on up it the way the body faces, which is the way the slope goes.)
		ctrl.SetState(m_HFlipped ? MOVE_LEFT : MOVE_RIGHT, true);
	}
	// Running on a long, level, open stretch: the point two bodies or more away and no higher, head room to stand, no door near, and
	// floor the whole way (a run off an edge or into a door is no way to arrive). The script used to roll a die for the run key.
	if (!prone && !crouch && std::abs(toPoint.m_X) > h * 2.0F && std::abs(toPoint.m_Y) < h * 0.25F && !noRoomAhead && !DoorAhead(point)) {
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
	// In the sweep of an open door of ours: on, whatever else stopped the legs. A door closes a second and a half after its sensors last saw
	// a body, on whatever is in its way, and units died at full health under doors of their own team.
	if (!ctrl.IsState(MOVE_LEFT) && !ctrl.IsState(MOVE_RIGHT) && InDoorSweep()) {
		ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
		mover.progressTimer.Reset();
	}
	// A unit of our own side in the way (LM-2). The bodies of a side collide, and the walk pushed into one until the stuck handling blamed
	// the ground there and marked it impassable for the whole team. Now: one going our way is followed at its pace; one lying down (or a
	// low crab) is leapt over, or hopped with the jet; two coming at each other, the one with the lower ID backs off until there is a body's
	// gap and lets the other by; one standing in the way is hopped over when there is a jet and room, else waited for. Only a unit nearer
	// than the point being walked to counts, and one standing on the goal itself is the goal reached. Behind the same unit for 3 s (through
	// gaps under a second), the route is asked again with that step avoided for 5 s; 3 s more and the unit is left to the ordinary walk, the
	// wall sense and the stuck handling for 8 s. (Not in a door's sweep, which goes on.)
	if (mover.blockIgnoreID != 0 && mover.blockIgnoreTimer.IsPastSimMS(8000)) {
		mover.blockIgnoreID = 0;
	}
	if (!prone && !mover.flight.active && kind != PathStepKind::Fall && std::abs(toPoint.m_X) > 3.0F && !InDoorSweep()) {
		const float way = toPoint.m_X < 0.0F ? -1.0F : 1.0F;
		auto release = [&ctrl]() {
			ctrl.SetState(MOVE_LEFT, false);
			ctrl.SetState(MOVE_RIGHT, false);
			ctrl.SetState(MOVE_FAST, false);
		};
		// Giving way, head on: back off to a body and a half's gap, then lie down so the other can leap or hop over (in a corridor there is no
		// other way past), until it is past us, for 3 s at most.
		if (mover.yieldTo != 0) {
			const Actor* other = dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(mover.yieldTo));
			float gap = other ? Towards(m_Pos, other->GetPos()).m_X * way : -1.0F;
			if (!other || gap <= 0.0F || mover.yieldTimer.IsPastSimMS(3000)) {
				mover.yieldTo = 0;
			} else {
				mover.progressTimer.Reset();
				release();
				if (gap < h * 1.5F && !mover.yieldTimer.IsPastSimMS(1500)) {
					ctrl.SetState(way < 0.0F ? MOVE_RIGHT : MOVE_LEFT, true);
				} else {
					ctrl.SetState(BODY_CROUCH, false);
					ctrl.SetState(BODY_PRONE, true);
				}
				return RouteMover::Moving;
			}
		}
		const Actor* blocker = UnitAhead(way, std::abs(toPoint.m_X) + h * 0.3F);
		if (blocker && blocker->GetUniqueID() == mover.blockIgnoreID) {
			blocker = nullptr;
		}
		// On the last leg, with the unit in the way on the goal or the goal within a body: there.
		if (blocker && m_MovePath.size() <= 1 && m_Waypoints.empty() && !g_MovableMan.ValidMO(m_pMOMoveTarget)) {
			Vector goal = GetLastAIWaypoint();
			if (Towards(m_Pos, goal).MagnitudeIsLessThan(h) || Towards(blocker->GetPos(), goal).MagnitudeIsLessThan(h)) {
				MoverTrace("arrived (a unit of ours on the goal)");
				return RouteMover::Arrived;
			}
		}
		if (!blocker) {
			if (mover.blocker != 0 && mover.blockSeenTimer.IsPastSimMS(1000)) {
				mover.blocker = 0;
			}
		} else {
			if (mover.blocker != blocker->GetUniqueID()) {
				mover.blocker = blocker->GetUniqueID();
				mover.blockTimer.Reset();
				mover.blockRepathed = false;
				mover.blockActionTimer.SetElapsedSimTimeMS(10000.0);
				MoverTrace("a unit of ours in the way (" + blocker->GetPresetName() + ")");
			}
			mover.blockSeenTimer.Reset();
			if (mover.blockTimer.IsPastSimMS(3000) && !mover.blockRepathed && !m_MovePath.empty()) {
				MoverTrace("behind the same unit for 3 s; new route round it");
				mover.blockRepathed = true;
				AvoidPathLink(m_Pos, m_MovePath.front(), 5000.0F);
				RefreshRoute();
				return RouteMover::Moving;
			}
			if (mover.blockTimer.IsPastSimMS(6000)) {
				MoverTrace("still behind the same unit; leaving it to the walk for a while");
				mover.blockIgnoreID = mover.blocker;
				mover.blockIgnoreTimer.Reset();
				mover.blocker = 0;
			} else {
				// (Not stuck on the ground for it: the terrain is not to blame.)
				mover.progressTimer.Reset();
				const AHuman* human = dynamic_cast<const AHuman*>(blocker);
				const bool low = (human && human->GetProneState() == PRONE) || blocker->GetHeight() < h * 0.55F;
				const float along = blocker->GetVel().m_X * way;
				const bool jetReady = standardJet && !mover.fuelWaiting && m_pJetpack->GetJetTimeLeft() > JetRelightFuel() + 150.0F && ColumnOpen(m_Pos.m_X, topHeadY + 2.0F, topHeadY - h * 0.6F);
				if (low) {
					if (CanLeap()) {
						if (mover.blockActionTimer.IsPastSimMS(1000)) {
							mover.blockActionTimer.Reset();
							MoverTrace("a unit lying in the way; leap over");
						}
						if (!mover.blockActionTimer.IsPastSimMS(200)) {
							ctrl.SetState(BODY_LEAP, true);
						}
					} else if (jetReady) {
						if (mover.blockActionTimer.IsPastSimMS(1200)) {
							mover.blockActionTimer.Reset();
							MoverTrace("a unit lying in the way; hop over");
						}
						if (!mover.blockActionTimer.IsPastSimMS(350)) {
							ctrl.SetState(BODY_JUMP, true);
							SetAimAngle(0.2F);
						}
					} else {
						release();
					}
				} else if (along > 0.5F) {
					// Going our way: behind it, at its pace.
					release();
				} else if (along < -0.5F && GetUniqueID() < blocker->GetUniqueID()) {
					// Head on, and ours to give way.
					MoverTrace("head on with a unit of ours; giving way");
					mover.yieldTo = blocker->GetUniqueID();
					mover.yieldTimer.Reset();
					release();
					ctrl.SetState(way < 0.0F ? MOVE_RIGHT : MOVE_LEFT, true);
				} else if (jetReady && mover.blockTimer.IsPastSimMS(600)) {
					// Standing in the way: over it with the jet.
					if (mover.blockActionTimer.IsPastSimMS(1500)) {
						mover.blockActionTimer.Reset();
						MoverTrace("a unit standing in the way; hop over");
					}
					if (!mover.blockActionTimer.IsPastSimMS(500)) {
						ctrl.SetState(BODY_JUMP, true);
						SetAimAngle(c_HalfPI * 0.5F);
					}
				} else {
					release();
				}
				return RouteMover::Moving;
			}
		}
	}
	// A wall ahead on the way (8.0's chest and head rays, a little under a body ahead, terrain only so other units and our doors aren't
	// walls): both blocked at much the same distance is a face higher than the body, hopped at once when there is head room and fuel,
	// rather than after the 2.5 s with no progress the stuck handling waits. Chest alone is a step or a slope, the legs' and the mantle's.
	// (Not for a point below us, a drop, nor lying down.)
	// What the sense found ahead, acted on at once rather than after the 2.5 s with no progress the stuck handling waits:
	// - something low (over a step the legs take and the mantle's reach, under the body's height) is hopped, with head room here and fuel;
	// - a wall with the route's point level and beyond it is something the grid didn't know (a crate, a lip, a frame smaller than its
	//   cells): the place is marked for a while and a new route asked for, rather than walking into it for six seconds;
	// - a wall with the route's point above is the climb's, hopped as before when there is head room.
	// (Before, the walk only went for the route's point and found out by not getting there; the chest, knee and head rays it had saw
	// three heights of the body's middle line.)
	bool wallAhead = false;
	bool lowObstacle = false;
	bool leapOver = false;
	if (sensed.any && !prone && kind != PathStepKind::Fall && !mover.fuelWaiting && !DoorAhead(point)) {
		const float stepUp = h * 0.15F;
		const float mantle = g_SettingsMan.MantlingEnabled() ? std::max(h, 20.0F) * 0.3F : 0.0F;
		const bool canHop = standardJet && m_pJetpack->GetJetTimeLeft() > JetRelightFuel() + 100.0F && ColumnOpen(m_Pos.m_X, topHeadY + 2.0F, topHeadY - h * 0.5F);
		const float direction = toPoint.m_X < 0.0F ? -1.0F : 1.0F;
		if (sensed.wall) {
			bool pointBeyond = toPoint.m_X * direction > sensed.distance + 4.0F && std::abs(toPoint.m_Y) < h * 0.3F;
			if (pointBeyond && kind != PathStepKind::Jump && mover.senseRerouteTimer.IsPastSimMS(2000)) {
				mover.senseRerouteTimer.Reset();
				Vector wallAt(m_Pos.m_X + direction * (sensed.distance + 6.0F), floorY - standing * 0.5F);
				MoverTrace("wall ahead the route didn't know (" + std::to_string(static_cast<int>(wallAt.m_X)) + "," + std::to_string(static_cast<int>(wallAt.m_Y)) + "); new route round it");
				AvoidPathPoint(wallAt, 15000.0F);
				RefreshRoute();
				return RouteMover::Moving;
			}
			wallAhead = !pointBeyond && above > -h * 0.2F && canHop;
		} else if (!sensed.gapUnder && sensed.rise > stepUp && above > -h * 0.2F) {
			// Something low on the floor, a step-over (LM-5) whether the route said so or not: leapt on the legs when the leap clears it, at its
			// edge; pulled over by the mantle (pressing into it) when that reaches; only higher than both, hopped with the jet. (Every lump the
			// legs didn't step was the jet's, and a unit with no jet stood at it till the stuck handling came.)
			const float bodyHalf = std::clamp(static_cast<float>(GetSpriteWidth()) * 0.5F + 1.0F, 5.0F, 16.0F);
			if (sensed.rise <= GetLegJumpHeight() + 2.0F && CanLeap()) {
				leapOver = sensed.distance <= bodyHalf + 8.0F;
			} else if (sensed.rise > mantle + 2.0F && canHop) {
				lowObstacle = true;
			}
		}
	}
	// Stuck (no progress for 2.5 s, and a new route at 6): the small things a player tries before the big one (LM-3), one at a time, each
	// for a moment and judged by progress: duck, back off half a body and come on again, leap, hop with the jet, lie down and crawl, stand
	// up. Each only when it can do something here (room to duck, floor behind, the legs to leap, a jet with head room, room to crawl, room
	// to stand), none twice in one stuck spell, one that failed at this spot lately left out, and one that worked here tried first. (As
	// unconditional moves, 8.0's back-offs read as pacing and its lying down as lying down at random, and a jetless unit had nothing.)
	const float direction = toPoint.m_X < 0.0F ? -1.0F : 1.0F;
	const char* const remedyNames[] = {"crouch", "back off", "leap", "hop", "lie down", "stand up"};
	static constexpr int remedyWindowMS[] = {1000, 1000, 800, 1200, 1500, 800};
	if (!stuck || mover.fuelWaiting) {
		if (mover.remedy >= 0 && !mover.fuelWaiting) {
			RememberStuckRemedy(mover.remedySpot, mover.remedy, true);
			MoverTrace(std::string("stuck remedy worked: ") + remedyNames[mover.remedy]);
		}
		mover.remedy = -1;
		mover.remedyTried = 0;
	} else {
		if (mover.remedy >= 0 && mover.remedyTimer.IsPastSimMS(remedyWindowMS[mover.remedy])) {
			RememberStuckRemedy(mover.remedySpot, mover.remedy, false);
			MoverTrace(std::string("stuck remedy didn't work: ") + remedyNames[mover.remedy]);
			mover.remedy = -1;
		}
		if (mover.remedy < 0) {
			std::array<bool, static_cast<int>(StuckRemedy::Count)> allowed{};
			allowed[static_cast<int>(StuckRemedy::Crouch)] = !prone && !crouch && m_ProneState == NOTPRONE && crouched < standing - 1.0F;
			float floorBehind = FloorUnder(m_Pos + Vector(-direction * h * 0.5F, 0.0F), feet + h * 0.3F);
			allowed[static_cast<int>(StuckRemedy::BackOff)] = !prone && m_ProneState == NOTPRONE && floorBehind >= 0.0F && std::abs(floorBehind - floorY) < h * 0.3F;
			allowed[static_cast<int>(StuckRemedy::Leap)] = !prone && CanLeap();
			allowed[static_cast<int>(StuckRemedy::Hop)] = standardJet && !prone && m_pJetpack->GetJetTimeLeft() > 300.0F && ColumnOpen(m_Pos.m_X, topHeadY + 2.0F, topHeadY - h * 0.5F);
			const float crawlRoom = std::max(8.0F, h * 0.2F);
			allowed[static_cast<int>(StuckRemedy::Prone)] = !prone && !steep && m_ProneState == NOTPRONE && !g_SceneMan.CastStrengthRay(Vector(m_Pos.m_X, floorY - crawlRoom), heading, 5.0F, hit, 4, MaterialColorKeys::g_MaterialDoor);
			allowed[static_cast<int>(StuckRemedy::Stand)] = m_ProneState == PRONE && !noRoomHere && !noRoomAhead;
			mover.remedy = PickStuckRemedy(m_Pos, allowed, mover.remedyTried);
			if (mover.remedy >= 0) {
				mover.remedyTried |= 1U << mover.remedy;
				mover.remedyTimer.Reset();
				mover.remedySpot = m_Pos;
				MoverTrace(std::string("stuck; ") + remedyNames[mover.remedy]);
				Say("Stuck");
			}
		}
		switch (static_cast<StuckRemedy>(mover.remedy)) {
			case StuckRemedy::Crouch:
				ctrl.SetState(BODY_PRONE, false);
				ctrl.SetState(BODY_CROUCH, true);
				break;
			case StuckRemedy::BackOff:
				if (!mover.remedyTimer.IsPastSimMS(400)) {
					ctrl.SetState(direction < 0.0F ? MOVE_LEFT : MOVE_RIGHT, false);
					ctrl.SetState(direction < 0.0F ? MOVE_RIGHT : MOVE_LEFT, true);
					ctrl.SetState(MOVE_FAST, false);
				}
				break;
			case StuckRemedy::Leap:
				if (!mover.remedyTimer.IsPastSimMS(200)) {
					ctrl.SetState(BODY_LEAP, true);
				}
				break;
			case StuckRemedy::Prone:
				ctrl.SetState(BODY_CROUCH, false);
				ctrl.SetState(BODY_PRONE, true);
				break;
			case StuckRemedy::Stand:
				ctrl.SetState(BODY_PRONE, false);
				break;
			default:
				break;
		}
	}
	// The stuck remedy's hop with the jet, at the start of its moment.
	if (mover.remedy == static_cast<int>(StuckRemedy::Hop) && standardJet && !mover.remedyTimer.IsPastSimMS(350)) {
		ctrl.SetState(BODY_JUMP, true);
		SetAimAngle(above > h * 0.2F ? c_HalfPI * 0.7F : 0.2F);
	}
	// Over something low on the legs, at its edge.
	if (leapOver && mover.remedy < 0) {
		if (mover.hopTimer.IsPastSimMS(800)) {
			mover.hopTimer.Reset();
			MoverTrace("low obstacle; leap over");
		}
		ctrl.SetState(BODY_LEAP, true);
	}
	// A wall, or a step up the legs don't take: a hop (the mantle takes most steps).
	if ((wallAhead || lowObstacle) && standardJet && !prone && !mover.fuelWaiting && m_pJetpack->GetJetTimeLeft() > 300.0F) {
		if (mover.hopTimer.IsPastSimMS(1200)) {
			mover.hopTimer.Reset();
			MoverTrace(wallAhead ? "wall ahead; hop" : "low obstacle; hop");
		}
		if (!mover.hopTimer.IsPastSimMS(350)) {
			ctrl.SetState(BODY_JUMP, true);
			SetAimAngle(above > h * 0.2F ? c_HalfPI * 0.7F : 0.2F);
		}
	}
	// Walking off an edge onto the point below: the walk is the drop; the brake comes in the air.
	return RouteMover::Moving;
}
