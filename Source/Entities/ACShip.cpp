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
#include "TimerMan.h"
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
	m_TouchdownTimer.Reset();
	m_OrderedBoardTimer.Reset();
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
	return CanFly() && AutopilotGoTo(spot);
}

void ACShip::UpdateAutopilot() {
	if (!CanFly()) {
		// (Left on a delivery or a trip home, a pilot getting in would fly off with it: it holds still for them instead.)
		if (m_AIMode == AIMODE_DELIVER || m_AIMode == AIMODE_RETURN || m_AIMode == AIMODE_BRAINHUNT || m_AIMode == AIMODE_BOMB) {
			SetAIMode(AIMODE_SENTRY);
		}
		return;
	}
	ACDropShip::UpdateAutopilot();
}

void ACShip::PreControllerUpdate() {
	ZoneScoped;

	bool flown = CanFly();
	// (A player at a gun or in a passenger's seat doesn't fly it: the pilot, or the ship itself, holds it where it is.)
	bool letGo = !flown || (m_Controller.IsPlayerControlled() && GetControlSeat() > 0);
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

	if (!flown && m_Parked) {
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
			if (target && !target->IsDead()) {
				Vector toTarget = g_SceneMan.ShortestDistance(turret->GetPos(), target->GetPos() - Vector(0.0F, target->GetHeight() * 0.1F));
				float wanted = std::atan2(-toTarget.m_Y, std::max(toTarget.m_X * flip, 0.0F));
				gun.AimAngle += std::clamp(Wrapped(wanted - gun.AimAngle), -m_GunTurnSpeed * deltaTime, m_GunTurnSpeed * deltaTime);
				fire = std::abs(Wrapped(wanted - gun.AimAngle)) < 0.1F;
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
