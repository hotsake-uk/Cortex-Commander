#include "ACDropShip.h"
#include "AtomGroup.h"
#include "Controller.h"
#include "Matrix.h"
#include "AEmitter.h"
#include "PresetMan.h"
#include "ActivityMan.h"
#include "Activity.h"
#include "MovableMan.h"
#include "SceneMan.h"
#include "SettingsMan.h"

#include <limits>

#include "tracy/Tracy.hpp"

using namespace RTE;

ConcreteClassInfo(ACDropShip, ACraft, 10);

ACDropShip::ACDropShip() {
	Clear();
}

ACDropShip::~ACDropShip() {
	Destroy(true);
}

void ACDropShip::Clear() {
	m_pBodyAG = 0;
	m_pRThruster = 0;
	m_pLThruster = 0;
	m_pURThruster = 0;
	m_pULThruster = 0;
	m_pRHatch = 0;
	m_pLHatch = 0;
	m_HatchSwingRange.SetDegAngle(90);
	m_HatchOpeness = 0;
	m_LateralControl = 0;
	m_LateralControlSpeed = 6.0f;
	m_AutoStabilize = 1;
	m_MaxEngineAngle = 20.0f;
	m_HoverHeightModifier = 0;
}

int ACDropShip::Create() {
	if (ACraft::Create() < 0)
		return -1;

	// Save the AtomGroup read in by MOSRotating, as we are going to make it
	// into a composite group, and want to have the base body stored for reference.
	m_pBodyAG = dynamic_cast<AtomGroup*>(m_pAtomGroup->Clone());

	return 0;
}

int ACDropShip::Create(const ACDropShip& reference) {
	if (reference.m_pRThruster) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pRThruster->GetUniqueID());
	}
	if (reference.m_pLThruster) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pLThruster->GetUniqueID());
	}
	if (reference.m_pURThruster) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pURThruster->GetUniqueID());
	}
	if (reference.m_pULThruster) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pULThruster->GetUniqueID());
	}
	if (reference.m_pRHatch) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pRHatch->GetUniqueID());
	}
	if (reference.m_pLHatch) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pLHatch->GetUniqueID());
	}

	ACraft::Create(reference);

	if (reference.m_pRThruster) {
		SetRightThruster(dynamic_cast<AEmitter*>(reference.m_pRThruster->Clone()));
	}
	if (reference.m_pLThruster) {
		SetLeftThruster(dynamic_cast<AEmitter*>(reference.m_pLThruster->Clone()));
	}
	if (reference.m_pURThruster) {
		SetURightThruster(dynamic_cast<AEmitter*>(reference.m_pURThruster->Clone()));
	}
	if (reference.m_pULThruster) {
		SetULeftThruster(dynamic_cast<AEmitter*>(reference.m_pULThruster->Clone()));
	}
	if (reference.m_pRHatch) {
		SetRightHatch(dynamic_cast<Attachable*>(reference.m_pRHatch->Clone()));
	}
	if (reference.m_pLHatch) {
		SetLeftHatch(dynamic_cast<Attachable*>(reference.m_pLHatch->Clone()));
	}

	m_pBodyAG = dynamic_cast<AtomGroup*>(reference.m_pBodyAG->Clone());
	m_pBodyAG->SetOwner(this);
	m_HatchSwingRange = reference.m_HatchSwingRange;
	m_HatchOpeness = reference.m_HatchOpeness;

	m_LateralControl = reference.m_LateralControl;
	m_LateralControlSpeed = reference.m_LateralControlSpeed;
	m_AutoStabilize = reference.m_AutoStabilize;

	m_MaxEngineAngle = reference.m_MaxEngineAngle;

	m_HoverHeightModifier = reference.m_HoverHeightModifier;

	return 0;
}

int ACDropShip::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return ACraft::ReadProperty(propName, reader));

	MatchForwards("RThruster") MatchForwards("RightThruster") MatchProperty("RightEngine", { SetRightThruster(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("LThruster") MatchForwards("LeftThruster") MatchProperty("LeftEngine", { SetLeftThruster(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("URThruster") MatchProperty("UpRightThruster", { SetURightThruster(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("ULThruster") MatchProperty("UpLeftThruster", { SetULeftThruster(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("RHatchDoor") MatchProperty("RightHatchDoor", { SetRightHatch(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("LHatchDoor") MatchProperty("LeftHatchDoor", { SetLeftHatch(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("HatchDoorSwingRange", { reader >> m_HatchSwingRange; });
	MatchProperty("AutoStabilize", { reader >> m_AutoStabilize; });
	MatchProperty("MaxEngineAngle", { reader >> m_MaxEngineAngle; });
	MatchProperty("LateralControlSpeed", { reader >> m_LateralControlSpeed; });
	MatchProperty("HoverHeightModifier", { reader >> m_HoverHeightModifier; });

	EndPropertyList;
}

int ACDropShip::Save(Writer& writer) const {
	ACraft::Save(writer);

	writer.NewProperty("RThruster");
	writer << m_pRThruster;
	writer.NewProperty("LThruster");
	writer << m_pLThruster;
	writer.NewProperty("URThruster");
	writer << m_pURThruster;
	writer.NewProperty("ULThruster");
	writer << m_pULThruster;
	writer.NewProperty("RHatchDoor");
	writer << m_pRHatch;
	writer.NewProperty("LHatchDoor");
	writer << m_pLHatch;
	writer.NewProperty("HatchDoorSwingRange");
	writer << m_HatchSwingRange;
	writer.NewProperty("AutoStabilize");
	writer << m_AutoStabilize;
	writer.NewProperty("MaxEngineAngle");
	writer << m_MaxEngineAngle;
	writer.NewProperty("LateralControlSpeed");
	writer << m_LateralControlSpeed;
	writer.NewPropertyWithValue("HoverHeightModifier", m_HoverHeightModifier);

	return 0;
}

void ACDropShip::Destroy(bool notInherited) {
	delete m_pBodyAG;

	if (!notInherited)
		ACraft::Destroy();
	Clear();
}

float ACDropShip::GetAltitude(int max, int accuracy) {
	// Check altitude both thrusters, and report the one closest to the ground.
	Vector rPos, lPos;

	if (m_pRThruster && m_pRThruster->IsAttached())
		rPos = m_Pos + RotateOffset(m_pRThruster->GetParentOffset()); // + Vector(m_pRThruster->GetRadius(), 0));
	else
		rPos = m_Pos;

	if (m_pLThruster && m_pLThruster->IsAttached())
		lPos = m_Pos + RotateOffset(m_pLThruster->GetParentOffset()); // + Vector(-m_pLThruster->GetRadius(), 0));
	else
		lPos = m_Pos;

	// Wrap the engine positions
	g_SceneMan.WrapPosition(lPos);
	g_SceneMan.WrapPosition(rPos);

	// Check center too
	float cAlt = g_SceneMan.FindAltitude(m_Pos, max, accuracy, true);
	float rAlt = g_SceneMan.FindAltitude(rPos, max, accuracy, true);
	float lAlt = g_SceneMan.FindAltitude(lPos, max, accuracy, true);

	// Return the lowest of the three
	return MIN(cAlt, MIN(rAlt, lAlt));
}

MOID ACDropShip::DetectObstacle(float distance) {
	// Check altitude both thrusters, and report the one closest to the ground.
	Vector rPos, lPos;

	if (m_pRThruster && m_pRThruster->IsAttached())
		rPos = m_Pos + RotateOffset(m_pRThruster->GetParentOffset() + Vector(m_pRThruster->GetRadius(), 0));
	else
		rPos = m_Pos;

	if (m_pLThruster && m_pLThruster->IsAttached())
		lPos = m_Pos + RotateOffset(m_pLThruster->GetParentOffset() + Vector(-m_pLThruster->GetRadius(), 0));
	else
		lPos = m_Pos;

	// Wrap the engine positions
	g_SceneMan.WrapPosition(lPos);
	g_SceneMan.WrapPosition(rPos);

	// Make the ray to check along point in an appropriate direction
	Vector checkRay;
	if (m_AltitudeMoveState == DESCEND)
		checkRay.m_Y = distance;
	else if (m_AltitudeMoveState == ASCEND)
		checkRay.m_Y = -distance;
	// Just rotate it to align iwth the velocity
	else {
		checkRay.m_X = distance;
		checkRay.AbsRotateTo(m_Vel);
	}

	MOID detected = g_NoMOID;

	// Check center too?
	if ((detected = g_SceneMan.CastMORay(m_Pos, checkRay, m_RootMOID, Activity::NoTeam, 0, true, 30)) != g_NoMOID)
		return detected;
	if ((detected = g_SceneMan.CastMORay(rPos, checkRay, m_RootMOID, Activity::NoTeam, 0, true, 30)) != g_NoMOID)
		return detected;
	if ((detected = g_SceneMan.CastMORay(lPos, checkRay, m_RootMOID, Activity::NoTeam, 0, true, 30)) != g_NoMOID)
		return detected;

	return false;
}

void ACDropShip::PreControllerUpdate() {
	ZoneScoped;

	ACraft::PreControllerUpdate();

	// TODO: Improve and make optional thrusters more robust!
	if (m_Status != DEAD && m_Status != DYING) {
		float targetYVel = 0.0F;
		float throttleRange = 7.5f;

		if (m_Controller.IsState(PRESS_UP)) {
			// This is to make sure se get loose from being sideways stuck
			m_ForceDeepCheck = true;
		}
		// TODO: make framerate independent!
		// Altitude control, check analog first
		if (fabs(m_Controller.GetAnalogMove().m_Y) > 0.1) {
			targetYVel = -m_Controller.GetAnalogMove().m_Y * throttleRange;
		}
		// Fall back to digital altitude control
		else if (m_Controller.IsState(MOVE_UP) || m_Controller.IsState(AIM_UP))
			targetYVel = throttleRange;
		else if (m_Controller.IsState(MOVE_DOWN) || m_Controller.IsState(AIM_DOWN))
			targetYVel = -throttleRange;

		//////////////////////////////////////////////////////
		// Main thruster throttling to stay hovering

		// ugly hacks. the entire trimming to hover system is shit and should be replaced

		// This is to trim the hover so it's perfectly still altitude-wise
		float trimming = -2.6f;

		float throttle = (targetYVel + m_Vel.m_Y + trimming) / throttleRange;

		// Adjust trim based on weight. Dropships hover nicely at zero weight, but tend to drop when they have a large inventory
		float massAdjustment = GetMass() / GetBaseMass();

		// Right main thruster
		if (m_pRThruster && m_pRThruster->IsAttached()) {
			float baseThrottleForThruster = m_pRThruster->GetThrottleForThrottleFactor(1.0f);
			float rightThrottle = m_pRThruster->GetScaledThrottle(throttle + baseThrottleForThruster, massAdjustment);

			// Throttle override control for correcting heavy tilt, only applies if both engines are present
			if (m_pLThruster && m_pLThruster->IsAttached()) {
				if (m_Rotation.GetRadAngle() > c_SixteenthPI) {
					rightThrottle = -0.8f;
				} else if (m_Rotation.GetRadAngle() < -c_SixteenthPI) {
					rightThrottle = 0.8f;
				}
			}

			if (rightThrottle > m_pRThruster->GetThrottle()) {
				rightThrottle = rightThrottle * 0.3f + m_pRThruster->GetThrottle() * 0.7f; // Increase throttle slowly
			}

			m_pRThruster->EnableEmission(m_Status == STABLE);
			m_pRThruster->SetThrottle(rightThrottle);
			m_pRThruster->SetFlashScale((m_pRThruster->GetThrottle() + 1.5f) / 2.0f);
			// Engines are noisy! Make AI aware of them
			m_pRThruster->AlarmOnEmit(m_Team);
		}
		// Left main thruster
		if (m_pLThruster && m_pLThruster->IsAttached()) {
			float baseThrottleForThruster = m_pLThruster->GetThrottleForThrottleFactor(1.0f);
			float leftThrottle = m_pLThruster->GetScaledThrottle(throttle + baseThrottleForThruster, massAdjustment);

			// Throttle override control for correcting heavy tilt, only applies if both engines are present
			if (m_pRThruster && m_pRThruster->IsAttached()) {
				if (m_Rotation.GetRadAngle() > c_SixteenthPI) {
					leftThrottle = 0.8f;
				} else if (m_Rotation.GetRadAngle() < -c_SixteenthPI) {
					leftThrottle = -0.8f;
				}
			}

			if (leftThrottle > m_pLThruster->GetThrottle()) {
				leftThrottle = leftThrottle * 0.3f + m_pLThruster->GetThrottle() * 0.7f; // Increase throttle slowly
			}

			m_pLThruster->EnableEmission(m_Status == STABLE);
			m_pLThruster->SetThrottle(leftThrottle);
			m_pLThruster->SetFlashScale((m_pLThruster->GetThrottle() + 1.5f) / 2.0F);
			// Engines are noisy! Make AI aware of them
			m_pLThruster->AlarmOnEmit(m_Team);
		}

		///////////////////////////////////////////////
		// Lateral control

		// Check analog first
		if (fabs(m_Controller.GetAnalogMove().m_X) > 0.1) {
			if (m_LateralControl < -m_Controller.GetAnalogMove().m_X)
				m_LateralControl += m_LateralControlSpeed * g_TimerMan.GetDeltaTimeSecs(); // 0.1 per update at 60fps
			else if (m_LateralControl > -m_Controller.GetAnalogMove().m_X)
				m_LateralControl -= m_LateralControlSpeed * g_TimerMan.GetDeltaTimeSecs();
		}
		// Fall back to digital lateral control
		else {
			if (m_Controller.IsState(MOVE_RIGHT))
				m_LateralControl -= m_LateralControlSpeed * g_TimerMan.GetDeltaTimeSecs();
			else if (m_Controller.IsState(MOVE_LEFT))
				m_LateralControl += m_LateralControlSpeed * g_TimerMan.GetDeltaTimeSecs();
			else if (m_LateralControl != 0.0)
				m_LateralControl *= 54.0f * g_TimerMan.GetDeltaTimeSecs(); // 90% per update at 60fps
		}

		// Clamp the lateral control
		if (m_LateralControl > 1.0)
			m_LateralControl = 1.0;
		else if (m_LateralControl < -1.0)
			m_LateralControl = -1.0;

		if (m_Controller.IsState(PRESS_FACEBUTTON)) {
			if (m_HatchState == CLOSED)
				DropAllInventory();
			else if (m_HatchState == OPEN)
				CloseHatch();
		}
	}
	// No Controller present, or dead
	else {
		if (m_pRThruster && m_pRThruster->IsAttached())
			m_pRThruster->EnableEmission(false);
		if (m_pLThruster && m_pLThruster->IsAttached())
			m_pLThruster->EnableEmission(false);
		/*
		        if (m_pURThruster && m_pURThruster->IsAttached())
		            m_pURThruster->EnableEmission(false);
		        if (m_pULThruster && m_pULThruster->IsAttached())
		            m_pULThruster->EnableEmission(false);
		*/
	}

	////////////////////////////////////////
	// Hatch Operation

	if (m_HatchState == OPENING) {
		if (m_HatchDelay > 0 && !m_HatchTimer.IsPastSimMS(m_HatchDelay))
			m_HatchOpeness = (float)m_HatchTimer.GetElapsedSimTimeMS() / (float)m_HatchDelay;
		else {
			m_HatchOpeness = 1.0;
			m_HatchState = OPEN;
			DropAllInventory();
		}
	} else if (m_HatchState == CLOSING) {
		if (m_HatchDelay > 0 && !m_HatchTimer.IsPastSimMS(m_HatchDelay))
			m_HatchOpeness = 1.0 - ((float)m_HatchTimer.GetElapsedSimTimeMS() / (float)m_HatchDelay);
		else {
			m_HatchOpeness = 0;
			m_HatchState = CLOSED;
		}
	}

	/////////////////////////////////
	// Manage Attachable:s
	Matrix engineRot = 0;
	if (m_Rotation.GetDegAngle() > m_MaxEngineAngle) {
		engineRot.SetDegAngle(m_Rotation.GetDegAngle() - m_MaxEngineAngle);
	} else if (m_Rotation.GetDegAngle() < -m_MaxEngineAngle) {
		engineRot.SetDegAngle(m_Rotation.GetDegAngle() + m_MaxEngineAngle);
	} else {
		// Lateral control application
		engineRot.SetDegAngle(m_MaxEngineAngle * m_LateralControl);
	}

	if (m_pRThruster && m_pRThruster->IsAttached()) {
		m_pRThruster->SetRotAngle(engineRot.GetRadAngle());
		m_pRThruster->SetAngularVel(0.0F);
	}

	if (m_pLThruster && m_pLThruster->IsAttached()) {
		m_pLThruster->SetRotAngle(engineRot.GetRadAngle());
		m_pLThruster->SetAngularVel(0.0F);
	}

	// Auto balancing with the up thrusters
	if (m_pURThruster && m_pURThruster->IsAttached() && m_pULThruster && m_pULThruster->IsAttached()) {
		if (m_AutoStabilize) {
			// Use a PD-controller for balance
			float change = 0.9F * m_AngularVel + 0.8F * m_Rotation.GetRadAngle();
			if (change > 0.2F) {
				if (!m_pURThruster->IsEmitting()) {
					m_pURThruster->TriggerBurst();
				}
				m_pURThruster->EnableEmission(true);
			} else {
				m_pURThruster->EnableEmission(false);
			}

			if (change < -0.2F) {
				if (!m_pULThruster->IsEmitting()) {
					m_pULThruster->TriggerBurst();
				}
				m_pULThruster->EnableEmission(true);
			} else {
				m_pULThruster->EnableEmission(false);
			}
		}
	}

	// Hatch door pieces
	if (m_pRHatch && m_pRHatch->IsAttached()) {
		m_pRHatch->SetRotAngle(m_Rotation.GetRadAngle() + m_HatchSwingRange.GetRadAngle() * m_HatchOpeness);
	}

	if (m_pLHatch && m_pLHatch->IsAttached()) {
		m_pLHatch->SetRotAngle(m_Rotation.GetRadAngle() - m_HatchSwingRange.GetRadAngle() * m_HatchOpeness);
	}
}

void ACDropShip::SetRightThruster(AEmitter* newThruster) {
	if (m_pRThruster && m_pRThruster->IsAttached()) {
		RemoveAndDeleteAttachable(m_pRThruster);
	}
	if (newThruster == nullptr) {
		m_pRThruster = nullptr;
	} else {
		m_pRThruster = newThruster;
		AddAttachable(newThruster);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newThruster->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 AEmitter* castedAttachable = dynamic_cast<AEmitter*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetRightThruster");
			                                                 dynamic_cast<ACDropShip*>(parent)->SetRightThruster(castedAttachable);
		                                                 }});

		if (m_pRThruster->HasNoSetDamageMultiplier()) {
			m_pRThruster->SetDamageMultiplier(1.0F);
		}
		m_pRThruster->SetInheritsRotAngle(false);
	}
}

void ACDropShip::SetLeftThruster(AEmitter* newThruster) {
	if (m_pLThruster && m_pLThruster->IsAttached()) {
		RemoveAndDeleteAttachable(m_pLThruster);
	}
	if (newThruster == nullptr) {
		m_pLThruster = nullptr;
	} else {
		m_pLThruster = newThruster;
		AddAttachable(newThruster);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newThruster->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 AEmitter* castedAttachable = dynamic_cast<AEmitter*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetLeftThruster");
			                                                 dynamic_cast<ACDropShip*>(parent)->SetLeftThruster(castedAttachable);
		                                                 }});

		if (m_pLThruster->HasNoSetDamageMultiplier()) {
			m_pLThruster->SetDamageMultiplier(1.0F);
		}
		m_pLThruster->SetInheritsRotAngle(false);
	}
}

void ACDropShip::SetURightThruster(AEmitter* newThruster) {
	if (m_pURThruster && m_pURThruster->IsAttached()) {
		RemoveAndDeleteAttachable(m_pURThruster);
	}
	if (newThruster == nullptr) {
		m_pURThruster = nullptr;
	} else {
		m_pURThruster = newThruster;
		AddAttachable(newThruster);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newThruster->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 AEmitter* castedAttachable = dynamic_cast<AEmitter*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetURightThruster");
			                                                 dynamic_cast<ACDropShip*>(parent)->SetURightThruster(castedAttachable);
		                                                 }});

		if (m_pURThruster->HasNoSetDamageMultiplier()) {
			m_pURThruster->SetDamageMultiplier(1.0F);
		}
	}
}

void ACDropShip::SetULeftThruster(AEmitter* newThruster) {
	if (m_pULThruster && m_pULThruster->IsAttached()) {
		RemoveAndDeleteAttachable(m_pULThruster);
	}
	if (newThruster == nullptr) {
		m_pULThruster = nullptr;
	} else {
		m_pULThruster = newThruster;
		AddAttachable(newThruster);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newThruster->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 AEmitter* castedAttachable = dynamic_cast<AEmitter*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetULeftThruster");
			                                                 dynamic_cast<ACDropShip*>(parent)->SetULeftThruster(castedAttachable);
		                                                 }});

		if (m_pULThruster->HasNoSetDamageMultiplier()) {
			m_pULThruster->SetDamageMultiplier(1.0F);
		}
	}
}

void ACDropShip::SetRightHatch(Attachable* newHatch) {
	if (m_pRHatch && m_pRHatch->IsAttached()) {
		RemoveAndDeleteAttachable(m_pRHatch);
	}
	if (newHatch == nullptr) {
		m_pRHatch = nullptr;
	} else {
		m_pRHatch = newHatch;
		AddAttachable(newHatch);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newHatch->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 dynamic_cast<ACDropShip*>(parent)->SetRightHatch(attachable);
		                                                 }});

		if (m_pRHatch->HasNoSetDamageMultiplier()) {
			m_pRHatch->SetDamageMultiplier(1.0F);
		}
		m_pRHatch->SetInheritsRotAngle(false);
	}
}

void ACDropShip::SetLeftHatch(Attachable* newHatch) {
	if (m_pLHatch && m_pLHatch->IsAttached()) {
		RemoveAndDeleteAttachable(m_pLHatch);
	}
	if (newHatch == nullptr) {
		m_pLHatch = nullptr;
	} else {
		m_pLHatch = newHatch;
		AddAttachable(newHatch);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newHatch->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 dynamic_cast<ACDropShip*>(parent)->SetLeftHatch(attachable);
		                                                 }});

		if (m_pLHatch->HasNoSetDamageMultiplier()) {
			m_pLHatch->SetDamageMultiplier(1.0F);
		}
		m_pLHatch->SetInheritsRotAngle(false);
	}
}

// ---------------------------------------------------------------- The drop ship's autopilot (a port of Base.rte/AI/NativeDropShipAI.lua)

float ACDropShip::AutopilotPID::Update(float rawInput, float target) {
	float err = 0.0F;
	float change = 0.0F;
	for (int tick = 0; tick < ticks; ++tick) {
		filtered = filtered * (1.0F - leak) + rawInput * leak;
		err = filtered - target;
		change = filtered - lastInput;
		lastInput = filtered;
		integral = std::clamp(integral + err, -integralMax, integralMax);
	}
	return p * err + i * integral + d * change;
}

namespace {
	// Where the scene's orbit is, from a point: far off the edge the craft goes home by.
	RTE::Vector DropShipOrbitPoint(const RTE::Vector& pos) {
		switch (RTE::g_SceneMan.GetSceneOrbitDirection()) {
			case RTE::Directions::Down:
				return RTE::Vector(pos.m_X, static_cast<float>(RTE::g_SceneMan.GetSceneHeight()) + 5000.0F);
			case RTE::Directions::Left:
				return RTE::Vector(-5000.0F, pos.m_Y);
			case RTE::Directions::Right:
				return RTE::Vector(static_cast<float>(RTE::g_SceneMan.GetSceneWidth()) + 5000.0F, pos.m_Y);
			default:
				return RTE::Vector(pos.m_X, -5000.0F);
		}
	}
} // namespace

float ACDropShip::AutopilotHoverAltitude() const {
	if (m_AIMode == AIMODE_BRAINHUNT) {
		return GetRadius() * 1.7F + m_HoverHeightModifier;
	} else if (m_AIMode == AIMODE_BOMB) {
		return GetRadius() * 6.0F + m_HoverHeightModifier;
	}
	return GetDiameter() + m_HoverHeightModifier;
}

float ACDropShip::AutopilotGroundBelow(const Vector& from, float hoverAlt) {
	float radius = GetRadius();
	int height = static_cast<int>(hoverAlt);
	float lowest = g_SceneMan.MovePointToGround(Vector(from.m_X - radius, from.m_Y), height, 12).m_Y;
	lowest = std::min(lowest, g_SceneMan.MovePointToGround(Vector(from.m_X, from.m_Y), height, 12).m_Y);
	return std::min(lowest, g_SceneMan.MovePointToGround(Vector(from.m_X + radius, from.m_Y), height, 12).m_Y);
}

int ACDropShip::AutopilotObstacleAhead() {
	MOID obstacle = DetectObstacle(GetDiameter() + m_Vel.GetMagnitude() * 70.0F);
	if (obstacle > 0 && obstacle != g_NoMOID) {
		const MovableObject* root = g_MovableMan.GetMOFromID(g_MovableMan.GetRootMOID(obstacle));
		if (root && (root->GetClassName() == "ACDropShip" || root->GetClassName() == "ACRocket")) {
			return 2;
		}
		return 1;
	}
	return 0;
}

void ACDropShip::UpdateAutopilot() {
	DropShipAutopilot& ai = m_Autopilot;
	Controller& ctrl = m_Controller;
	const float radius = GetRadius();
	const float diameter = GetDiameter();
	const float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	const bool orbitUp = g_SceneMan.GetSceneOrbitDirection() == Directions::Up;

	// The script's Create: the hover height, the controllers, and a human team's empty ship kept from going home at once.
	if (!ai.begun) {
		ai.begun = true;
		ai.avoidTimer.SetSimTimeLimitMS(500);
		ai.playerInterferedTimer.SetSimTimeLimitMS(500);
		ai.playerInterferedTimer.Reset();
		ai.stuckTimer.Reset();
		ai.hatchTimer.Reset();
		ai.lastAIMode = AIMODE_NONE;
		ai.savedHoverHeightModifier = m_HoverHeightModifier;
		ai.hoverAlt = diameter + m_HoverHeightModifier;
		int ticks = std::max(1, g_SettingsMan.GetAIUpdateInterval());
		ai.xPID = AutopilotPID{0.05F, 0.01F, 2.5F, 0.8F, 50.0F, ticks};
		ai.yPID = AutopilotPID{0.1F, 0.0F, 2.5F, 0.6F, std::numeric_limits<float>::max() / 2.0F, ticks};
		if (m_AIMode == AIMODE_DELIVER && IsInventoryEmpty() && g_ActivityMan.GetActivity() && g_ActivityMan.GetActivity()->IsHumanTeam(m_Team)) {
			m_AIMode = AIMODE_STAY;
		}
	}

	// A new hover height, or a new order: a new waypoint and delivery stage.
	bool hoverHeightModifierChanged = ai.savedHoverHeightModifier != m_HoverHeightModifier;
	if (hoverHeightModifierChanged) {
		ai.savedHoverHeightModifier = m_HoverHeightModifier;
		ai.hoverAlt = AutopilotHoverAltitude();
	}
	if (hoverHeightModifierChanged || m_AIMode != ai.lastAIMode) {
		UpdateMovePath();
		ai.lastAIMode = static_cast<AIMode>(m_AIMode);
		if (m_AIMode == AIMODE_RETURN) {
			ai.deliveryState = LAUNCH;
			ai.waypoint = DropShipOrbitPoint(m_Pos);
			ai.hasWaypoint = true;
		} else if (m_AIMode == AIMODE_GOTO) {
			ai.hasWaypoint = false;
		} else if (m_AIMode == AIMODE_SENTRY) {
			ai.waypoint = m_Pos;
			ai.hasWaypoint = true;
			ai.deliveryState = STANDBY;
		} else {
			float startingHeight = orbitUp ? (hoverHeightModifierChanged ? radius * 1.25F : std::max(radius * 1.25F, m_Pos.m_Y)) : m_Pos.m_Y;
			ai.waypoint = Vector(m_Pos.m_X, AutopilotGroundBelow(Vector(m_Pos.m_X, startingHeight), ai.hoverAlt));
			ai.hasWaypoint = true;
			ai.deliveryState = FALL;
		}
	}

	// The player flew it a while (no autopilot update for half a second): the waypoint brought back near where it is now.
	if (ai.playerInterferedTimer.IsPastSimTimeLimit()) {
		ai.stuckTimer.Reset();
		Vector futurePos = m_Pos + m_Vel * 20.0F;
		if (futurePos.m_X > sceneWidth) {
			futurePos.m_X = g_SceneMan.SceneWrapsX() ? futurePos.m_X - sceneWidth : sceneWidth - radius;
		} else if (futurePos.m_X < 0.0F) {
			futurePos.m_X = g_SceneMan.SceneWrapsX() ? futurePos.m_X + sceneWidth : radius;
		}
		if (ai.hasWaypoint && std::abs(g_SceneMan.ShortestDistance(futurePos, ai.waypoint, false).m_X) > 100.0F) {
			if (ai.deliveryState == LAUNCH) {
				ai.waypoint = Vector(futurePos.m_X, -500.0F);
			} else {
				ai.waypoint = Vector(m_Pos.m_X, AutopilotGroundBelow(Vector(m_Pos.m_X, std::max(radius * 1.25F, m_Pos.m_Y)), ai.hoverAlt));
			}
		}
	}
	ai.playerInterferedTimer.Reset();

	// Go-to: the route's points in turn, and a sentry at the last.
	if (m_AIMode == AIMODE_GOTO) {
		if (IsWaitingOnNewMovePath()) {
			ai.reachedWaypoint = false;
			ai.hasWaypoint = false;
			return;
		}
		if (!ai.hasWaypoint || ai.reachedWaypoint) {
			ai.reachedWaypoint = false;
			if (!m_MovePath.empty()) {
				ai.waypoint = m_MovePath.back();
				ai.hasWaypoint = true;
			}
		} else if (g_SceneMan.ShortestDistance(m_Pos, ai.waypoint, false).MagnitudeIsLessThan(20.0F)) {
			if (m_Waypoints.empty()) {
				m_AIMode = AIMODE_SENTRY;
				ai.waypoint = m_Pos;
			} else {
				ClearMovePath();
				UpdateMovePath();
				ai.reachedWaypoint = true;
			}
		}
	}
	// (The script stopped here with an error when it had nowhere to go; this holds still instead.)
	if (!ai.hasWaypoint) {
		ctrl.SetState(MOVE_UP, false);
		ctrl.SetState(MOVE_DOWN, false);
		return;
	}

	// Sideways, on a PID of where it will be in a while.
	float change = ai.xPID.Update(g_SceneMan.ShortestDistance(m_Pos + m_Vel * 30.0F, ai.waypoint, false).m_X, 0.0F);
	if (std::abs(change) > 0.6F) {
		ctrl.SetAnalogMove(Vector(change / 8.0F, 0.0F));
	}
	// Up and down, likewise.
	change = ai.yPID.Update(g_SceneMan.ShortestDistance(m_Pos + m_Vel * 5.0F, ai.waypoint, false).m_Y, 0.0F);
	if (change > 2.0F) {
		ai.altitudeMoveState = DESCEND;
	} else if (change < -2.0F) {
		ai.altitudeMoveState = ASCEND;
	}

	// The delivery.
	if (m_AIMode == AIMODE_STAY || m_AIMode == AIMODE_DELIVER) {
		if (ai.deliveryState == FALL) {
			if (IsInventoryEmpty() && m_AIMode != AIMODE_BRAINHUNT) {
				// Nothing to deliver: home.
				if (m_AIMode != AIMODE_STAY) {
					ai.deliveryState = LAUNCH;
					ai.hatchTimer.Reset();
					ai.waypoint = DropShipOrbitPoint(m_Pos);
				}
			} else if (g_SceneMan.ShortestDistance(m_Pos, ai.waypoint, false).MagnitudeIsLessThan(radius) && std::abs(change) < 3.0F && std::abs(m_Vel.m_X) < 4.0F) {
				// Hovering at the waypoint: unload when the ground is near enough.
				ai.waypoint = Vector(m_Pos.m_X, AutopilotGroundBelow(m_Pos + Vector(0.0F, -radius), ai.hoverAlt));
				if (g_SceneMan.ShortestDistance(m_Pos, ai.waypoint, false).MagnitudeIsLessThan(diameter)) {
					if (m_AIMode == AIMODE_STAY) {
						ai.deliveryState = STANDBY;
					} else {
						ai.deliveryState = UNLOAD;
						ai.hatchTimer.Reset();
					}
				}
			} else {
				// Something in the way of the descent: another craft (every second check), stepped round; terrain (the others), hovered over.
				if (ai.avoidTimer.IsPastSimTimeLimit()) {
					ai.avoidTimer.Reset();
					ai.search = !ai.search;
					if (ai.search) {
						int obstacle = AutopilotObstacleAhead();
						if (obstacle == 2) {
							ai.avoidHover = true;
							ai.waypoint.m_X += diameter * 2.0F;
							if (ai.waypoint.m_X > sceneWidth) {
								ai.waypoint.m_X = g_SceneMan.SceneWrapsX() ? ai.waypoint.m_X - sceneWidth : sceneWidth - radius;
							}
						} else if (obstacle == 0) {
							ai.avoidHover = false;
						}
					} else {
						Vector free;
						Vector start = m_Pos + Vector(radius, 0.0F);
						Vector trace = m_Vel * (radius / 2.0F) + Vector(0.0F, 50.0F);
						if (RandomNum() < 0.5F) {
							start.m_X -= diameter;
						}
						if (g_SceneMan.CastStrengthRay(start, trace, 0.0F, free, 4, 0, true)) {
							ai.waypoint = Vector(m_Pos.m_X, free.m_Y - ai.hoverAlt);
						}
					}
				}
				if (ai.avoidHover) {
					ai.altitudeMoveState = HOVER;
				}
			}
		} else if (ai.deliveryState == UNLOAD) {
			if (ai.hatchTimer.IsPastSimMS(500)) {
				ai.hatchTimer.Reset();
				OpenHatch();
				if (m_AIMode == AIMODE_BRAINHUNT && HasObjectInGroup("Brains")) {
					m_AIMode = AIMODE_RETURN;
				} else {
					ai.deliveryState = FALL;
				}
			}
		} else if (ai.deliveryState == LAUNCH) {
			if (ai.hatchTimer.IsPastSimMS(1000)) {
				ai.hatchTimer.Reset();
				CloseHatch();
			}
			// Another craft in the way of the climb: stepped round the other way.
			if (ai.avoidTimer.IsPastSimTimeLimit()) {
				ai.avoidTimer.Reset();
				int obstacle = AutopilotObstacleAhead();
				if (obstacle == 2) {
					ai.avoidHover = true;
					ai.waypoint.m_X -= diameter * 2.0F;
					if (ai.waypoint.m_X < 0.0F) {
						ai.waypoint.m_X = g_SceneMan.SceneWrapsX() ? ai.waypoint.m_X + sceneWidth : radius;
					}
				} else if (obstacle == 0) {
					ai.avoidHover = false;
				}
			}
			if (ai.avoidHover) {
				ai.altitudeMoveState = HOVER;
			}
		}
	} else {
		ai.deliveryState = FALL;
	}

	// The stick.
	if (ai.altitudeMoveState == ASCEND) {
		ctrl.SetState(MOVE_UP, true);
	} else if (ai.altitudeMoveState == DESCEND) {
		ctrl.SetState(MOVE_DOWN, true);
	} else {
		ctrl.SetState(MOVE_UP, false);
		ctrl.SetState(MOVE_DOWN, false);
	}

	// Hopelessly stuck (or both engines gone, which leaves it five seconds): scuttled.
	if (!m_pLThruster && !m_pRThruster && !ai.stuckTimer.IsPastSimMS(35000)) {
		ai.stuckTimer.SetElapsedSimTimeMS(35000);
	}
	if (m_Vel.GetLargest() > 3.0F || m_AIMode == AIMODE_STAY || m_AIMode == AIMODE_SENTRY || m_AIMode == AIMODE_GOTO) {
		ai.stuckTimer.Reset();
	} else if (m_AIMode == AIMODE_SCUTTLE || ai.stuckTimer.IsPastSimMS(40000)) {
		GibThis();
	}
}
