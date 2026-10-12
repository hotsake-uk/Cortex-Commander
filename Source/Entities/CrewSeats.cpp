#include "CrewSeats.h"

#include "AHuman.h"
#include "ActivityMan.h"
#include "AtomGroup.h"
#include "Controller.h"
#include "MovableMan.h"
#include "Sandbox.h"
#include "SceneMan.h"
#include "SettingsMan.h"
#include "TimerMan.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace RTE;

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
} // namespace

void CrewSeats::Clear() {
	m_Seats.assign(1, Seat());
	m_Slumped.clear();
	m_PlayerSeat = -1;
	m_ExitOffset.SetXY(20.0F, -10.0F);
	m_BoardingReach = 24.0F;
	m_DrawsCrew = true;
	m_PassengersCanFire = false;
	m_PassengerTargetTimer.Reset();
	m_LastVel.Reset();
	m_BoardingTimer.Reset();
	m_BoarderInReach = nullptr;
	m_BoarderPushes = false;
	m_SeatToLoad = -1;
	m_RiderToLoad = false;
}

void CrewSeats::Create(const CrewSeats& reference) {
	m_Seats.clear();
	for (const Seat& seat: reference.m_Seats) {
		m_Seats.push_back(seat);
		m_Seats.back().Occupant = seat.Occupant ? dynamic_cast<Actor*>(seat.Occupant->Clone()) : nullptr;
	}
	m_PlayerSeat = reference.m_PlayerSeat;
	m_ExitOffset = reference.m_ExitOffset;
	m_BoardingReach = reference.m_BoardingReach;
	m_DrawsCrew = reference.m_DrawsCrew;
	m_PassengersCanFire = reference.m_PassengersCanFire;
}

void CrewSeats::DeleteCrew() {
	for (Seat& seat: m_Seats) {
		delete seat.Occupant;
		seat.Occupant = nullptr;
	}
	for (const SlumpedBody& slumped: m_Slumped) {
		delete slumped.Body;
	}
	m_Slumped.clear();
}

void CrewSeats::LoadSeated(Actor* unit) {
	const int seat = m_SeatToLoad;
	m_SeatToLoad = -1;
	if (!unit) {
		return;
	}
	if (seat < 0 || seat >= GetSeatCount() || m_Seats[seat].Occupant) {
		delete unit;
		return;
	}
	m_Seats[seat].Occupant = unit;
	m_Seats[seat].Rider = m_RiderToLoad;
}

void CrewSeats::SetOccupant(int seat, Actor* unit) {
	if (seat >= 0 && seat < GetSeatCount()) {
		m_Seats[seat].Occupant = unit;
		if (!unit) {
			m_Seats[seat].Rider = false;
		}
	}
}

int CrewSeats::GetGunOfSeat(int seat) const {
	if (!IsGunnerSeat(seat)) {
		return -1;
	}
	int gun = 0;
	for (int each = 1; each < seat; ++each) {
		gun += m_Seats[each].Gunner ? 1 : 0;
	}
	return gun;
}

int CrewSeats::GetSeatOfGun(int gun) const {
	for (int seat = 1; seat < GetSeatCount(); ++seat) {
		if (m_Seats[seat].Gunner && gun-- == 0) {
			return seat;
		}
	}
	return -1;
}

void CrewSeats::SetRider(int seat, bool rider) {
	if (seat >= 0 && seat < GetSeatCount()) {
		m_Seats[seat].Rider = rider;
	}
}

int CrewSeats::GetCrewCount() const {
	return static_cast<int>(std::count_if(m_Seats.begin(), m_Seats.end(), [](const Seat& seat) { return seat.Occupant != nullptr; }));
}

int CrewSeats::GetFreeSeat() const {
	if (!m_Seats[0].Occupant) {
		return 0;
	}
	// A gunner's seat before a passenger's: whoever gets in second mans the gun.
	for (bool gunner: {true, false}) {
		for (int seat = 1; seat < GetSeatCount(); ++seat) {
			if (m_Seats[seat].Gunner == gunner && !m_Seats[seat].Occupant) {
				return seat;
			}
		}
	}
	return -1;
}

int CrewSeats::GetControlSeat() const {
	if (m_PlayerSeat >= 0 && GetOccupant(m_PlayerSeat)) {
		return m_PlayerSeat;
	}
	return m_Seats[0].Occupant ? 0 : -1;
}

bool CrewSeats::CanCarry(const Actor* unit, bool anyTeam) const {
	if (!m_Host || !unit || unit == m_Host || unit->IsDead() || unit->IsSetToDelete() || (!anyTeam && unit->GetTeam() != m_Host->GetTeam()) || !dynamic_cast<const AHuman*>(unit) || unit->IsInGroup("Brains") || m_Host->IsDead()) {
		return false;
	}
	if (const Activity* activity = g_ActivityMan.GetActivity()) {
		for (int player = Players::PlayerOne; player < Players::MaxPlayerCount; ++player) {
			// (A brain that walks about, as in the Sandbox's units, stays out: the side would lose with it gone from the scene.)
			if (activity->GetPlayerBrain(player) == unit) {
				return false;
			}
		}
	}
	return true;
}

bool CrewSeats::TakeSeat(Actor* unit, int seat, bool anyTeam) {
	if (seat < 0) {
		seat = GetFreeSeat();
	}
	if (seat < 0 || seat >= GetSeatCount() || GetOccupant(seat) || !CanCarry(unit, anyTeam)) {
		return false;
	}
	Controller* hostController = m_Host->GetController();
	bool playerUnit = unit->GetController()->IsPlayerControlled();
	// (One player at a time: a unit of another player's can't climb into something someone else is in charge of.)
	if (playerUnit && hostController->IsPlayerControlled() && hostController->GetPlayer() != unit->GetController()->GetPlayer()) {
		return false;
	}
	// Taken over by another team: it is theirs now.
	if (unit->GetTeam() != m_Host->GetTeam()) {
		m_Host->SetTeam(unit->GetTeam());
	}
	// As a craft takes in who boards it: a copy is kept, the unit itself is deleted from the scene, and the side isn't counted a loss for it.
	Actor* occupant = dynamic_cast<Actor*>(unit->Clone());
	if (!occupant) {
		return false;
	}
	occupant->GetController()->SetInputMode(Controller::CIM_AI);
	occupant->SetVel(Vector());
	SetOccupant(seat, occupant);
	unit->SetToDelete(true);
	// (Anyone slumped dead in the seat is tipped out of it to make room.)
	for (size_t index = 0; index < m_Slumped.size(); ++index) {
		if (m_Slumped[index].Seat == seat) {
			TipOutSlumped(index, false);
			break;
		}
	}
	if (Activity* activity = g_ActivityMan.GetActivity()) {
		activity->ReportDeath(unit->GetTeam(), -1);
		if (playerUnit) {
			activity->SwitchToActor(m_Host, unit->GetController()->GetPlayer(), m_Host->GetTeam());
			m_PlayerSeat = seat;
		}
	}
	// (The Sandbox keeps its own hold on the unit you control and on your character: they go with it into the seat.)
	Sandbox::OnUnitBoarded(unit, m_Host);
	m_BoardingTimer.Reset();
	PlaceCrew();
	// (Said by the host, where they now sit: they are out of the scene while in here.)
	m_Host->Say("BoardVehicle");
	return true;
}

bool CrewSeats::SeatNew(Actor* unit, int seat) {
	if (seat < 0) {
		seat = GetFreeSeat();
	}
	if (seat < 0 || seat >= GetSeatCount() || GetOccupant(seat) || !CanCarry(unit)) {
		return false;
	}
	unit->GetController()->SetInputMode(Controller::CIM_AI);
	unit->SetVel(Vector());
	SetOccupant(seat, unit);
	PlaceCrew();
	return true;
}

Actor* CrewSeats::LetOut(int seat, bool thrown, bool fallen) {
	Actor* occupant = GetOccupant(seat);
	if (!occupant) {
		return nullptr;
	}
	const Vector& hostPos = m_Host->GetPos();
	// Out on the side it faces, or the other, or straight up out of the seat if both are blocked; thrown out, straight up first.
	Vector above = hostPos + m_Host->RotateOffset(GetSeatOffset(seat)) - Vector(0.0F, occupant->GetHeight() * 0.5F);
	std::array<Vector, 3> exits = {hostPos + m_Host->RotateOffset(m_ExitOffset), hostPos + m_Host->RotateOffset(Vector(-m_ExitOffset.m_X, m_ExitOffset.m_Y)), above};
	if (thrown) {
		std::swap(exits[0], exits[2]);
	}
	occupant->SetRotAngle(0.0F);
	occupant->SetAngularVel(0.0F);
	const AtomGroup* atoms = occupant->GetAtomGroup();
	const Vector* out = nullptr;
	for (const Vector& exit: exits) {
		if (!atoms || atoms->FitsAt(exit)) {
			out = &exit;
			break;
		}
	}
	if (!out) {
		if (!m_Host->IsDead() && !thrown && !fallen) {
			return nullptr;
		}
		// (Wrecked or thrown: out on top whatever is there, rather than lost with it.)
		out = &above;
	}
	int controlSeat = GetControlSeat();
	SetOccupant(seat, nullptr);
	occupant->SetPos(*out);
	if (thrown) {
		// Flung on with the speed it had before the knock, and up and out of the seat.
		occupant->SetVel(m_LastVel * 0.8F + Vector(RandomNum(-2.0F, 2.0F), -4.0F));
		occupant->SetAngularVel(RandomNum(-6.0F, 6.0F));
	} else {
		occupant->SetVel(m_Host->GetVel() + Vector(0.0F, -2.0F));
	}
	occupant->SetWhichMOToNotHit(m_Host, 0.5F);
	m_Host->SetWhichMOToNotHit(occupant, 0.5F);
	occupant->ResetAllTimers();
	g_MovableMan.AddActor(occupant);
	Controller* hostController = m_Host->GetController();
	if (Activity* activity = g_ActivityMan.GetActivity(); activity && seat == controlSeat && hostController->IsPlayerControlled() && activity->GetControlledActor(hostController->GetPlayer()) == m_Host) {
		activity->SwitchToActor(occupant, hostController->GetPlayer(), occupant->GetTeam());
	}
	if (seat == m_PlayerSeat) {
		m_PlayerSeat = -1;
	}
	Sandbox::OnUnitLeftVehicle(m_Host, occupant);
	m_BoardingTimer.Reset();
	if (!fallen) {
		occupant->Say(thrown || m_Host->IsDead() || m_Host->GetHealth() <= 0.0F ? "BailOut" : "LeaveVehicle");
	}
	return occupant;
}

void CrewSeats::EjectCrew(bool thrown) {
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		LetOut(seat, thrown);
	}
	TipOutAllSlumped(thrown);
}

void CrewSeats::TipOutSlumped(size_t index, bool thrown) {
	if (index >= m_Slumped.size()) {
		return;
	}
	Actor* body = m_Slumped[index].Body;
	int seat = m_Slumped[index].Seat;
	m_Slumped.erase(m_Slumped.begin() + static_cast<long>(index));
	const Vector& hostPos = m_Host->GetPos();
	// Over the side it faces, or the other, or up out of the seat whatever is there: a body isn't left in it for want of room.
	Vector above = hostPos + m_Host->RotateOffset(GetSeatOffset(seat)) - Vector(0.0F, body->GetHeight() * 0.5F);
	std::array<Vector, 2> exits = {hostPos + m_Host->RotateOffset(m_ExitOffset), hostPos + m_Host->RotateOffset(Vector(-m_ExitOffset.m_X, m_ExitOffset.m_Y))};
	Vector out = above;
	const AtomGroup* atoms = body->GetAtomGroup();
	for (const Vector& exit: exits) {
		if (!thrown && (!atoms || atoms->FitsAt(exit))) {
			out = exit;
			break;
		}
	}
	body->SetPos(out);
	body->SetRotAngle(0.0F);
	body->SetVel(thrown ? m_LastVel * 0.8F + Vector(RandomNum(-2.0F, 2.0F), -4.0F) : m_Host->GetVel());
	body->SetAngularVel(thrown ? RandomNum(-6.0F, 6.0F) : 0.0F);
	body->SetWhichMOToNotHit(m_Host, 0.5F);
	m_Host->SetWhichMOToNotHit(body, 0.5F);
	body->ResetAllTimers();
	g_MovableMan.AddActor(body);
}

void CrewSeats::TipOutAllSlumped(bool thrown) {
	while (!m_Slumped.empty()) {
		TipOutSlumped(m_Slumped.size() - 1, thrown);
	}
}

void CrewSeats::UpdateSeatedCrew() {
	// (A closed one's crew sit out of reach, and nothing in here can hurt them.)
	if (!m_DrawsCrew) {
		return;
	}
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		Actor* unit = GetOccupant(seat);
		if (!unit) {
			continue;
		}
		// Blown apart where they sat: their pieces are out in the scene already, and nobody is left in the seat.
		if (unit->IsSetToDelete()) {
			SetOccupant(seat, nullptr);
			if (seat == m_PlayerSeat) {
				m_PlayerSeat = -1;
			}
			delete unit;
			continue;
		}
		unit->UpdateWhileSeated();
		const AHuman* human = dynamic_cast<const AHuman*>(unit);
		if (unit->GetHealth() > 0.0F && (!human || human->GetHead())) {
			continue;
		}
		unit->SetHealth(std::min(unit->GetHealth(), 0.0F));
		// The player's own unit always falls out, so its death is seen and dealt with as anywhere else.
		bool players = m_Host->GetController()->IsPlayerControlled() && seat == GetControlSeat();
		if (g_SettingsMan.DeadCrewFallOut() || players) {
			LetOut(seat, false, true);
		} else {
			SetOccupant(seat, nullptr);
			if (seat == m_PlayerSeat) {
				m_PlayerSeat = -1;
			}
			m_Slumped.push_back({unit, seat});
		}
	}
	for (size_t index = 0; index < m_Slumped.size();) {
		Actor* body = m_Slumped[index].Body;
		if (body->IsSetToDelete()) {
			delete body;
			m_Slumped.erase(m_Slumped.begin() + static_cast<long>(index));
			continue;
		}
		body->UpdateWhileSeated();
		++index;
	}
}

std::vector<Actor*> CrewSeats::TakeAllCrew() {
	std::vector<Actor*> crew;
	for (Seat& seat: m_Seats) {
		if (seat.Occupant) {
			crew.push_back(seat.Occupant);
			seat.Occupant = nullptr;
			seat.Rider = false;
		}
	}
	for (const SlumpedBody& slumped: m_Slumped) {
		delete slumped.Body;
	}
	m_Slumped.clear();
	m_PlayerSeat = -1;
	return crew;
}

bool CrewSeats::ChangeSeat() {
	int from = GetControlSeat();
	if (from < 0) {
		return false;
	}
	for (int step = 1; step < GetSeatCount(); ++step) {
		int to = (from + step) % GetSeatCount();
		if (!GetOccupant(to)) {
			bool rider = IsRider(from);
			SetOccupant(to, GetOccupant(from));
			SetOccupant(from, nullptr);
			SetRider(to, rider);
			m_PlayerSeat = to;
			m_BoardingTimer.Reset();
			PlaceCrew();
			return true;
		}
	}
	return false;
}

bool CrewSeats::IsInReach(const Actor* unit, float extraReach) const {
	// Anywhere close round it, under it too (BoardFromBelow): a unit jetting up under a hovering ship gets in from below.
	if (g_SettingsMan.BoardFromBelow() && !g_SceneMan.ShortestDistance(m_Host->GetPos(), unit->GetPos()).MagnitudeIsGreaterThan(m_Host->GetRadius() + g_SettingsMan.BoardFromBelowReach() + extraReach)) {
		return true;
	}
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		if (!g_SceneMan.ShortestDistance(m_Host->GetPos() + m_Host->RotateOffset(GetSeatOffset(seat)), unit->GetPos()).MagnitudeIsGreaterThan(m_BoardingReach + extraReach + unit->GetRadius() * 0.25F)) {
			return true;
		}
	}
	return false;
}

void CrewSeats::UpdateBoarding(bool pushOver, bool anyTeam) {
	constexpr double c_BoardingDelayMS = 400.0; // After getting in or out, how long before the same key does the opposite.
	m_BoarderInReach = nullptr;
	m_BoarderPushes = false;
	if (!m_BoardingTimer.IsPastSimMS(c_BoardingDelayMS)) {
		return;
	}
	if (m_Host->IsDead()) {
		EjectCrew(false);
		return;
	}
	// The player in charge gets their unit out.
	const Controller* hostController = m_Host->GetController();
	if (hostController->IsPlayerControlled() && hostController->IsState(WEAPON_PICKUP) && GetControlSeat() >= 0) {
		LetOut(GetControlSeat(), false);
		return;
	}
	if (GetFreeSeat() < 0 && !pushOver) {
		return;
	}
	for (Actor* unit: g_MovableMan.GetActorList()) {
		if (unit == m_Host || (!anyTeam && unit->GetTeam() != m_Host->GetTeam()) || unit->IsDead() || unit->IsSetToDelete() || !unit->GetController()->IsPlayerControlled() || !dynamic_cast<AHuman*>(unit)) {
			continue;
		}
		if (!IsInReach(unit, 0.0F)) {
			continue;
		}
		m_BoarderInReach = unit;
		m_BoarderPushes = pushOver;
		if (unit->GetController()->IsState(WEAPON_PICKUP)) {
			if (pushOver) {
				float rotation = Wrapped(m_Host->GetRotAngle());
				m_Host->SetAngularVel(rotation > 0.0F ? -5.0F : 5.0F);
				m_Host->SetVel(m_Host->GetVel() - Vector(0.0F, 4.0F));
				m_BoardingTimer.Reset();
				m_BoarderInReach = nullptr;
			} else if (TakeSeat(unit, -1, anyTeam)) {
				m_BoarderInReach = nullptr;
			}
		}
		break;
	}
}

void CrewSeats::PlaceCrew() const {
	const Vector& hostPos = m_Host->GetPos();
	const Vector& hostVel = m_Host->GetVel();
	float rotation = m_Host->GetRotAngle();
	bool flipped = m_Host->IsHFlipped();
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		if (Actor* unit = GetOccupant(seat)) {
			unit->SetPos(hostPos + m_Host->RotateOffset(GetSeatOffset(seat)));
			unit->SetVel(hostVel);
			unit->SetRotAngle(rotation);
			unit->SetHFlipped(flipped);
			unit->CorrectAttachableAndWoundPositionsAndRotations();
		}
	}
	for (const SlumpedBody& slumped: m_Slumped) {
		// Slumped forward and down in the seat.
		slumped.Body->SetPos(hostPos + m_Host->RotateOffset(GetSeatOffset(slumped.Seat) + Vector(0.0F, slumped.Body->GetHeight() * 0.08F)));
		slumped.Body->SetVel(hostVel);
		slumped.Body->SetRotAngle(rotation - 0.7F * m_Host->GetFlipFactor());
		slumped.Body->SetHFlipped(flipped);
		slumped.Body->CorrectAttachableAndWoundPositionsAndRotations();
	}
}

void CrewSeats::UpdatePassengerFire() {
	if (!m_PassengersCanFire || !g_SettingsMan.PassengersFireFromVehicles() || m_Host->GetStatus() == Actor::DYING || m_Host->GetStatus() == Actor::DEAD) {
		return;
	}
	bool lookRound = m_PassengerTargetTimer.IsPastSimMS(400);
	if (lookRound) {
		m_PassengerTargetTimer.Reset();
	}
	const Controller* controller = m_Host->GetController();
	int playerSeat = controller->IsPlayerControlled() ? GetControlSeat() : -1;
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	// (Seat 0's unit drives or flies it, and never shoots; gunners man its guns.)
	for (int seat = 1; seat < GetSeatCount(); ++seat) {
		AHuman* unit = dynamic_cast<AHuman*>(GetOccupant(seat));
		if (!unit || IsGunnerSeat(seat) || unit->GetHealth() <= 0.0F) {
			continue;
		}
		if (seat == playerSeat) {
			// The player aims as on foot: the aim keys swing it, the mouse or stick points it.
			m_Seats[seat].TargetID = 0;
			float aim = unit->GetAimAngle(false);
			float speed = controller->IsState(AIM_SHARP) ? 0.6F : 1.8F;
			if (controller->IsState(AIM_UP)) {
				aim += speed * deltaTime;
			} else if (controller->IsState(AIM_DOWN)) {
				aim -= speed * deltaTime;
			}
			if (Vector analogAim = controller->GetAnalogAim(); analogAim.MagnitudeIsGreaterThan(0.1F)) {
				// (Behind it, the aim stays at the end of its swing on that side, up or down.)
				aim = std::atan2(-analogAim.m_Y, std::max(analogAim.m_X * m_Host->GetFlipFactor(), 0.0F));
			}
			unit->UpdateFiringFromSeat(aim, controller->IsState(WEAPON_FIRE), controller->IsState(WEAPON_RELOAD), false, m_Host);
			continue;
		}
		if (!Controller::AIMayAct()) {
			// (The AI paused: they hold their aim and fire.)
			unit->UpdateFiringFromSeat(unit->GetAimAngle(false), false, false, true, m_Host);
			continue;
		}
		if (lookRound) {
			m_Seats[seat].TargetID = FindPassengerTarget(unit);
		}
		const Actor* target = m_Seats[seat].TargetID ? dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(m_Seats[seat].TargetID)) : nullptr;
		if (target && (target->IsDead() || target->IsSetToDelete() || target->GetHealth() <= 0.0F)) {
			m_Seats[seat].TargetID = 0;
			target = nullptr;
		}
		float aim = 0.0F;
		if (target) {
			// At its middle, a little high: the chest more than the legs.
			Vector toTarget = g_SceneMan.ShortestDistance(unit->GetEyePos(), target->GetPos() - Vector(0.0F, target->GetHeight() * 0.1F));
			aim = std::atan2(-toTarget.m_Y, std::max(toTarget.m_X * m_Host->GetFlipFactor(), 0.0F));
		}
		unit->UpdateFiringFromSeat(aim, target != nullptr, false, true, m_Host);
	}
}

long CrewSeats::FindPassengerTarget(const Actor* unit) const {
	const Vector eye = unit->GetEyePos();
	const int team = m_Host->GetTeam();
	const float flip = m_Host->GetFlipFactor();
	float bestDistance = unit->GetSightDistance();
	long best = 0;
	for (const Actor* actor: g_MovableMan.GetActorList()) {
		if (actor == m_Host || actor->GetTeam() == team || actor->GetTeam() == Activity::NoTeam || actor->IsDead() || actor->IsSetToDelete() || actor->GetHealth() <= 0.0F) {
			continue;
		}
		Vector toTarget = g_SceneMan.ShortestDistance(eye, actor->GetPos());
		// (In front, the way it faces, within the swing of their arm: they can't turn round in the seat.)
		if (toTarget.m_X * flip <= 0.0F || toTarget.MagnitudeIsGreaterThan(bestDistance) || std::abs(std::atan2(-toTarget.m_Y, std::abs(toTarget.m_X))) > unit->GetAimRange()) {
			continue;
		}
		Vector blocked;
		if (g_SceneMan.CastStrengthRay(eye, toTarget, 5.0F, blocked, 3)) {
			continue;
		}
		bestDistance = toTarget.GetMagnitude();
		best = static_cast<long>(actor->GetUniqueID());
	}
	return best;
}

void CrewSeats::Draw(BITMAP* targetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const {
	// (On the MOID layer too, so shots hit them.)
	if (!m_DrawsCrew || !(mode == g_DrawColor || mode == g_DrawWhite || mode == g_DrawTrans || mode == g_DrawAlpha || mode == g_DrawMOID)) {
		return;
	}
	for (const Seat& seat: m_Seats) {
		if (seat.Occupant) {
			seat.Occupant->Draw(targetBitmap, targetPos, mode, onlyPhysical);
		}
	}
	for (const SlumpedBody& slumped: m_Slumped) {
		slumped.Body->Draw(targetBitmap, targetPos, mode, onlyPhysical);
	}
}

void CrewSeats::Draw(const Camera& camera) const {
	if (!m_DrawsCrew) {
		return;
	}
	for (const Seat& seat: m_Seats) {
		if (seat.Occupant) {
			seat.Occupant->Draw(camera);
		}
	}
	for (const SlumpedBody& slumped: m_Slumped) {
		slumped.Body->Draw(camera);
	}
}

void CrewSeats::UpdateMOIDs(std::vector<MovableObject*>& MOIDIndex) const {
	if (!m_DrawsCrew) {
		return;
	}
	// Each its own root, as the separate units they are: a shot that hits one hurts them, not the host.
	for (const Seat& seat: m_Seats) {
		if (seat.Occupant) {
			seat.Occupant->UpdateMOID(MOIDIndex);
		}
	}
	for (const SlumpedBody& slumped: m_Slumped) {
		slumped.Body->UpdateMOID(MOIDIndex);
	}
}

void CrewSeats::GetMOIDs(std::vector<MOID>& MOIDs) const {
	if (!m_DrawsCrew) {
		return;
	}
	for (const Seat& seat: m_Seats) {
		if (seat.Occupant) {
			seat.Occupant->GetMOIDs(MOIDs);
		}
	}
	for (const SlumpedBody& slumped: m_Slumped) {
		slumped.Body->GetMOIDs(MOIDs);
	}
}
