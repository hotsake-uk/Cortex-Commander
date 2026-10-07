#include "ACRocket.h"
#include "AtomGroup.h"
#include "Attachable.h"
#include "Leg.h"
#include "Controller.h"
#include "Matrix.h"
#include "AEmitter.h"
#include "SettingsMan.h"
#include "PresetMan.h"
#include "ActivityMan.h"
#include "Activity.h"
#include "MovableMan.h"
#include "SceneMan.h"

#include "GUI.h"
#include "AllegroBitmap.h"

using namespace RTE;

ConcreteClassInfo(ACRocket, ACraft, 10);

ACRocket::ACRocket() {
	Clear();
}

ACRocket::~ACRocket() {
	Destroy(true);
}

void ACRocket::Clear() {
	//    m_pCapsule = 0;
	m_pRLeg = 0;
	m_pLLeg = 0;
	m_pBodyAG = 0;
	m_pRFootGroup = 0;
	m_pLFootGroup = 0;
	m_pMThruster = 0;
	m_pRThruster = 0;
	m_pLThruster = 0;
	m_pURThruster = 0;
	m_pULThruster = 0;
	m_GearState = RAISED;
	for (int i = 0; i < GearStateCount; ++i) {
		m_Paths[RIGHT][i].Reset();
		m_Paths[LEFT][i].Reset();
		m_Paths[RIGHT][i].Terminate();
		m_Paths[LEFT][i].Terminate();
	}
	m_MaxGimbalAngle = 0;
}

int ACRocket::Create() {
	// Read all the properties
	if (ACraft::Create() < 0)
		return -1;

	// Save the AtomGroup read in by MOSRotating, as we are going to make it
	// into a composite group, and want to have the base body stored for reference.
	m_pBodyAG = dynamic_cast<AtomGroup*>(m_pAtomGroup->Clone());

	// Mirror the limb paths
	for (int i = 0; i < GearStateCount; ++i) {
		m_Paths[LEFT][i].Create(m_Paths[RIGHT][i]);
	}

	// Override the body animation mode
	m_SpriteAnimMode = PINGPONGOPENCLOSE;

	return 0;
}

int ACRocket::Create(const ACRocket& reference) {
	if (reference.m_pRLeg) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pRLeg->GetUniqueID());
	}
	if (reference.m_pLLeg) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pLLeg->GetUniqueID());
	}
	if (reference.m_pMThruster) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pMThruster->GetUniqueID());
	}
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

	ACraft::Create(reference);

	if (reference.m_pRLeg) {
		SetRightLeg(dynamic_cast<Leg*>(reference.m_pRLeg->Clone()));
	}
	if (reference.m_pLLeg) {
		SetLeftLeg(dynamic_cast<Leg*>(reference.m_pLLeg->Clone()));
	}
	if (reference.m_pMThruster) {
		SetMainThruster(dynamic_cast<AEmitter*>(reference.m_pMThruster->Clone()));
	}
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

	m_pBodyAG = dynamic_cast<AtomGroup*>(reference.m_pBodyAG->Clone());
	m_pBodyAG->SetOwner(this);

	if (reference.m_pLFootGroup) {
		m_pLFootGroup = dynamic_cast<AtomGroup*>(reference.m_pLFootGroup->Clone());
	} else if (m_pLLeg) {
		m_pLFootGroup = m_pLLeg->GetFootGroupFromFootAtomGroup();
	}
	if (m_pLFootGroup) {
		m_pLFootGroup->SetOwner(this);
	}

	if (reference.m_pRFootGroup) {
		m_pRFootGroup = dynamic_cast<AtomGroup*>(reference.m_pRFootGroup->Clone());
	} else if (m_pRLeg) {
		m_pRFootGroup = m_pRLeg->GetFootGroupFromFootAtomGroup();
	}
	if (m_pRFootGroup) {
		m_pRFootGroup->SetOwner(this);
	}

	m_GearState = reference.m_GearState;

	for (int i = 0; i < GearStateCount; ++i) {
		m_Paths[RIGHT][i].Create(reference.m_Paths[RIGHT][i]);
		m_Paths[LEFT][i].Create(reference.m_Paths[LEFT][i]);
	}

	m_MaxGimbalAngle = reference.m_MaxGimbalAngle;

	return 0;
}

int ACRocket::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return ACraft::ReadProperty(propName, reader));

	MatchForwards("RLeg") MatchProperty("RightLeg", { SetRightLeg(dynamic_cast<Leg*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("LLeg") MatchProperty("LeftLeg", { SetLeftLeg(dynamic_cast<Leg*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("RFootGroup") MatchProperty("RightFootGroup", {
		delete m_pRFootGroup;
		m_pRFootGroup = new AtomGroup();
		reader >> m_pRFootGroup;
		m_pRFootGroup->SetOwner(this);
	});
	MatchForwards("LFootGroup") MatchProperty("LeftFootGroup", {
		delete m_pLFootGroup;
		m_pLFootGroup = new AtomGroup();
		reader >> m_pLFootGroup;
		m_pLFootGroup->SetOwner(this);
	});
	MatchForwards("MThruster") MatchProperty("MainThruster", { SetMainThruster(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("RThruster") MatchProperty("RightThruster", { SetRightThruster(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("LThruster") MatchProperty("LeftThruster", { SetLeftThruster(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("URThruster") MatchProperty("UpRightThruster", { SetURightThruster(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("ULThruster") MatchProperty("UpLeftThruster", { SetULeftThruster(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("RaisedGearLimbPath", { reader >> m_Paths[RIGHT][RAISED]; });
	MatchProperty("LoweredGearLimbPath", { reader >> m_Paths[RIGHT][LOWERED]; });
	MatchProperty("LoweringGearLimbPath", { reader >> m_Paths[RIGHT][LOWERING]; });
	MatchProperty("RaisingGearLimbPath", { reader >> m_Paths[RIGHT][RAISING]; });
	MatchProperty("MaxGimbalAngle", {
		reader >> m_MaxGimbalAngle;
		m_MaxGimbalAngle *= (c_PI / 180.0F);
	});

	EndPropertyList;
}

int ACRocket::Save(Writer& writer) const {
	ACraft::Save(writer);

	writer.NewProperty("RLeg");
	writer << m_pRLeg;
	writer.NewProperty("LLeg");
	writer << m_pLLeg;
	writer.NewProperty("RFootGroup");
	writer << m_pRFootGroup;
	writer.NewProperty("LFootGroup");
	writer << m_pLFootGroup;
	writer.NewProperty("MThruster");
	writer << m_pMThruster;
	writer.NewProperty("RThruster");
	writer << m_pRThruster;
	writer.NewProperty("LThruster");
	writer << m_pLThruster;
	writer.NewProperty("URThruster");
	writer << m_pURThruster;
	writer.NewProperty("ULThruster");
	writer << m_pULThruster;
	writer.NewProperty("RaisedGearLimbPath");
	writer << m_Paths[RIGHT][RAISED];
	writer.NewProperty("LoweredGearLimbPath");
	writer << m_Paths[RIGHT][LOWERED];
	writer.NewProperty("LoweringGearLimbPath");
	writer << m_Paths[RIGHT][LOWERING];
	writer.NewProperty("RaisingGearLimbPath");
	writer << m_Paths[RIGHT][RAISING];
	writer.NewProperty("MaxGimbalAngle");
	writer << m_MaxGimbalAngle / (c_PI / 180.0F);

	return 0;
}

void ACRocket::Destroy(bool notInherited) {
	delete m_pBodyAG;
	delete m_pRFootGroup;
	delete m_pLFootGroup;

	//    for (deque<LimbPath *>::iterator itr = m_WalkPaths.begin();
	//         itr != m_WalkPaths.end(); ++itr)
	//        delete *itr;

	if (!notInherited)
		ACraft::Destroy();
	Clear();
}

float ACRocket::GetAltitude(int max, int accuracy) {
	// Use the main thruster's position as the position ot measure from
	Vector pos;
	if (m_pMThruster && m_pMThruster->IsAttached())
		pos = m_Pos + RotateOffset(m_pMThruster->GetParentOffset());
	else
		pos = m_Pos;

	return g_SceneMan.FindAltitude(pos, max, accuracy, true);
}

void ACRocket::PreControllerUpdate() {
	ACraft::PreControllerUpdate();

	float deltaTime = g_TimerMan.GetDeltaTimeSecs();

	// Look/aim update, make the scanner point aftward if the rocket is falling
	m_AimAngle = m_Vel.m_Y < 0 ? c_HalfPI : -c_HalfPI;

	/////////////////////////////////
	// Controller update and handling

	if ((m_Status == STABLE || m_Status == UNSTABLE) && !m_Controller.IsDisabled()) {
		if (m_pMThruster) {
			if (m_MaxGimbalAngle != 0) {
				m_pMThruster->SetInheritedRotAngleOffset(std::sin(m_Rotation.GetRadAngle()) * m_MaxGimbalAngle - c_HalfPI);
			}

			if (m_Controller.IsState(MOVE_UP) || m_Controller.IsState(AIM_UP)) {
				if (!m_pMThruster->IsEmitting()) {
					m_pMThruster->TriggerBurst();
					m_ForceDeepCheck = true;
				}
				m_pMThruster->EnableEmission(true);
				m_pMThruster->AlarmOnEmit(m_Team);

				if (m_HatchState == OPEN) {
					CloseHatch();
					m_HatchTimer.Reset();
				}
			} else {
				m_pMThruster->EnableEmission(false);
			}
		}
		if (m_pULThruster || m_pURThruster) {
			if (m_Controller.IsState(MOVE_DOWN) || m_Controller.IsState(AIM_DOWN)) {
				if (m_pURThruster) {
					if (!m_pURThruster->IsEmitting()) {
						m_pURThruster->TriggerBurst();
					}
					m_pURThruster->EnableEmission(true);
				}
				if (m_pULThruster) {
					if (!m_pULThruster->IsEmitting()) {
						m_pULThruster->TriggerBurst();
					}
					m_pULThruster->EnableEmission(true);
				}
			} else {
				if (m_pURThruster) {
					m_pURThruster->EnableEmission(false);
				}
				if (m_pULThruster) {
					m_pULThruster->EnableEmission(false);
				}
			}
		}
		if (m_pLThruster) {
			if (m_Controller.IsState(MOVE_RIGHT)) {
				if (!m_pLThruster->IsEmitting()) {
					m_pLThruster->TriggerBurst();
				}
				m_pLThruster->EnableEmission(true);
			} else {
				m_pLThruster->EnableEmission(false);
			}
		}
		if (m_pRThruster) {
			if (m_Controller.IsState(MOVE_LEFT)) {
				if (!m_pRThruster->IsEmitting()) {
					m_pRThruster->TriggerBurst();
				}
				m_pRThruster->EnableEmission(true);
			} else {
				m_pRThruster->EnableEmission(false);
			}
		}
		if (m_Controller.IsState(PRESS_FACEBUTTON)) {
			if (m_HatchState == CLOSED) {
				DropAllInventory();
			} else if (m_HatchState == OPEN) {
				CloseHatch();
			}
		}
	} else {
		if (m_pMThruster) {
			m_pMThruster->EnableEmission(false);
		}
		if (m_pRThruster) {
			m_pRThruster->EnableEmission(false);
		}
		if (m_pLThruster) {
			m_pLThruster->EnableEmission(false);
		}
		if (m_pURThruster) {
			m_pURThruster->EnableEmission(false);
		}
		if (m_pULThruster) {
			m_pULThruster->EnableEmission(false);
		}
	}

	///////////////////////////////////////////////////
	// Travel the landing gear AtomGroup:s

	if (m_pMThruster) {
		m_GearState = m_pMThruster->IsEmitting() ? LandingGearState::RAISED : LandingGearState::LOWERED;

		m_Paths[RIGHT][m_GearState].SetHFlip(m_HFlipped);
		m_Paths[LEFT][m_GearState].SetHFlip(!m_HFlipped);

		if (!m_LimbPushForcesAndCollisionsDisabled) {
			if (m_pRLeg) {
				m_pRFootGroup->PushAsLimb(m_Pos.GetFloored() + RotateOffset(m_pRLeg->GetParentOffset()), m_pRLeg->GetMaxLength(), m_Vel, m_Rotation, m_Paths[RIGHT][m_GearState], deltaTime, nullptr, true);
			}
			if (m_pLLeg) {
				m_pLFootGroup->PushAsLimb(m_Pos.GetFloored() + RotateOffset(m_pLLeg->GetParentOffset()), m_pLLeg->GetMaxLength(), m_Vel, m_Rotation, m_Paths[LEFT][m_GearState], deltaTime, nullptr, true);
			}
		} else {
			if (m_pRLeg) {
				m_pRFootGroup->PushAsLimb(m_Pos.GetFloored() + RotateOffset(m_pRLeg->GetParentOffset()), m_pRLeg->GetMaxLength(), m_Vel, m_Rotation, m_Paths[RIGHT][m_GearState], deltaTime, nullptr, true);
			}
			if (m_pLLeg) {
				m_pLFootGroup->PushAsLimb(m_Pos.GetFloored() + RotateOffset(m_pLLeg->GetParentOffset()), m_pLLeg->GetMaxLength(), m_Vel, m_Rotation, m_Paths[LEFT][m_GearState], deltaTime, nullptr, true);
			}
		}
	}

	/////////////////////////////////
	// Manage Attachable:s
	if (m_pRLeg && m_pRLeg->IsAttached()) {
		m_pRLeg->SetTargetPosition(m_pRFootGroup->GetLimbPos(m_HFlipped));
	}

	if (m_pLLeg && m_pLLeg->IsAttached()) {
		m_pLLeg->SetTargetPosition(m_pLFootGroup->GetLimbPos(!m_HFlipped));
	}
}

void ACRocket::Update() {
	ACraft::Update();

	////////////////////////////////////////
	// Hatch Operation

	unsigned int lastFrame = m_FrameCount - 1;

	if (m_HatchState == OPENING) {
		if (m_HatchTimer.GetElapsedSimTimeMS() <= m_HatchDelay && m_HatchDelay)
			m_Frame = static_cast<unsigned int>(static_cast<double>(lastFrame) * (m_HatchTimer.GetElapsedSimTimeMS() / static_cast<double>(m_HatchDelay)));
		else {
			m_Frame = lastFrame;
			m_HatchState = OPEN;
			DropAllInventory();
		}
	} else if (m_HatchState == CLOSING) {
		if (m_HatchTimer.GetElapsedSimTimeMS() <= m_HatchDelay && m_HatchDelay)
			m_Frame = lastFrame - static_cast<unsigned int>(static_cast<double>(lastFrame) * (m_HatchTimer.GetElapsedSimTimeMS() / static_cast<double>(m_HatchDelay)));
		else {
			m_Frame = 0;
			m_HatchState = CLOSED;
		}
	}
}

void ACRocket::SetRightLeg(Leg* newLeg) {
	if (m_pRLeg && m_pRLeg->IsAttached()) {
		RemoveAndDeleteAttachable(m_pRLeg);
	}
	if (newLeg == nullptr) {
		m_pRLeg = nullptr;
	} else {
		m_pRLeg = newLeg;
		AddAttachable(newLeg);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newLeg->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 Leg* castedAttachable = dynamic_cast<Leg*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetRightLeg");
			                                                 dynamic_cast<ACRocket*>(parent)->SetRightLeg(castedAttachable);
		                                                 }});

		if (m_pRLeg->HasNoSetDamageMultiplier()) {
			m_pRLeg->SetDamageMultiplier(1.0F);
		}
	}
}

void ACRocket::SetLeftLeg(Leg* newLeg) {
	if (m_pLLeg && m_pLLeg->IsAttached()) {
		RemoveAndDeleteAttachable(m_pLLeg);
	}
	if (newLeg == nullptr) {
		m_pLLeg = nullptr;
	} else {
		m_pLLeg = newLeg;
		AddAttachable(newLeg);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newLeg->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 Leg* castedAttachable = dynamic_cast<Leg*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetLeftLeg");
			                                                 dynamic_cast<ACRocket*>(parent)->SetLeftLeg(castedAttachable);
		                                                 }});

		if (m_pLLeg->HasNoSetDamageMultiplier()) {
			m_pLLeg->SetDamageMultiplier(1.0F);
		}
		m_pLLeg->SetInheritsHFlipped(-1);
	}
}

void ACRocket::SetMainThruster(AEmitter* newThruster) {
	if (m_pMThruster && m_pMThruster->IsAttached()) {
		RemoveAndDeleteAttachable(m_pMThruster);
	}
	if (newThruster == nullptr) {
		m_pMThruster = nullptr;
	} else {
		m_pMThruster = newThruster;
		AddAttachable(newThruster);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newThruster->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 AEmitter* castedAttachable = dynamic_cast<AEmitter*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetMainThruster");
			                                                 dynamic_cast<ACRocket*>(parent)->SetMainThruster(castedAttachable);
		                                                 }});

		if (m_pMThruster->HasNoSetDamageMultiplier()) {
			m_pMThruster->SetDamageMultiplier(1.0F);
		}
		m_pMThruster->SetInheritedRotAngleOffset(-c_HalfPI);
	}
}

void ACRocket::SetRightThruster(AEmitter* newThruster) {
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
			                                                 dynamic_cast<ACRocket*>(parent)->SetRightThruster(castedAttachable);
		                                                 }});

		if (m_pRThruster->HasNoSetDamageMultiplier()) {
			m_pRThruster->SetDamageMultiplier(1.0F);
		}
		m_pRThruster->SetInheritedRotAngleOffset(c_EighthPI);
	}
}

void ACRocket::SetLeftThruster(AEmitter* newThruster) {
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
			                                                 dynamic_cast<ACRocket*>(parent)->SetLeftThruster(castedAttachable);
		                                                 }});

		if (m_pLThruster->HasNoSetDamageMultiplier()) {
			m_pLThruster->SetDamageMultiplier(1.0F);
		}
		m_pLThruster->SetInheritedRotAngleOffset(c_PI - c_EighthPI);
	}
}

void ACRocket::SetURightThruster(AEmitter* newThruster) {
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
			                                                 dynamic_cast<ACRocket*>(parent)->SetURightThruster(castedAttachable);
		                                                 }});

		if (m_pURThruster->HasNoSetDamageMultiplier()) {
			m_pURThruster->SetDamageMultiplier(1.0F);
		}
		m_pURThruster->SetInheritedRotAngleOffset(c_HalfPI - c_EighthPI);
	}
}

void ACRocket::SetULeftThruster(AEmitter* newThruster) {
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
			                                                 dynamic_cast<ACRocket*>(parent)->SetULeftThruster(castedAttachable);
		                                                 }});

		if (m_pULThruster->HasNoSetDamageMultiplier()) {
			m_pULThruster->SetDamageMultiplier(1.0F);
		}
		m_pULThruster->SetInheritedRotAngleOffset(c_HalfPI + c_EighthPI);
	}
}

void ACRocket::Draw(BITMAP* pTargetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const {
	ACraft::Draw(pTargetBitmap, targetPos, mode, onlyPhysical);

	if (mode == g_DrawColor && !onlyPhysical && g_SettingsMan.DrawHandAndFootGroupVisualizations()) {
		m_pRFootGroup->Draw(pTargetBitmap, targetPos, true, 13);
		m_pLFootGroup->Draw(pTargetBitmap, targetPos, true, 13);
	}
}

void ACRocket::Draw(const Camera& camera) const {

	ACraft::Draw(camera);

	if (g_SettingsMan.DrawHandAndFootGroupVisualizations()) {
		m_pRFootGroup->Draw(camera, true, 13);
		m_pLFootGroup->Draw(camera, true, 13);
	}
}

// ---------------------------------------------------------------- The rocket's autopilot (a port of Base.rte/AI/RocketAI.lua)

float ACRocket::AutopilotPID::Update(float rawInput, float target) {
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

void ACRocket::AutopilotMoveLZ() {
	RocketAutopilot& ai = m_Autopilot;
	const float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	Vector futurePos = m_Pos + m_Vel * 7.0F;
	if (futurePos.m_X > sceneWidth) {
		futurePos.m_X = g_SceneMan.SceneWrapsX() ? futurePos.m_X - sceneWidth : sceneWidth - GetRadius();
	} else if (futurePos.m_X < 0.0F) {
		futurePos.m_X = g_SceneMan.SceneWrapsX() ? futurePos.m_X + sceneWidth : GetRadius();
	}
	if (ai.deliveryState == LAUNCH) {
		ai.lzPos.m_X = futurePos.m_X;
	} else {
		ai.lzPos = g_SceneMan.MovePointToGround(Vector(futurePos.m_X, std::min(futurePos.m_Y, m_Pos.m_Y)), static_cast<int>(ai.groundDist), 5);
	}
}

void ACRocket::UpdateAutopilot() {
	RocketAutopilot& ai = m_Autopilot;
	Controller& ctrl = m_Controller;
	const int interval = std::max(1, g_SettingsMan.GetAIUpdateInterval());
	const float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());

	// The script's Create: the landing zone under it, the controllers, and a human team's empty rocket kept from going home at once.
	if (!ai.begun) {
		ai.begun = true;
		ai.stableTimer.Reset();
		ai.stuckTimer.Reset();
		ai.doorTimer.Reset();
		ai.obstacleTimer.SetSimTimeLimitMS(100);
		ai.playerInterferedTimer.SetSimTimeLimitMS(500);
		ai.playerInterferedTimer.Reset();
		ai.deliveryState = FALL;
		ai.lastAIMode = AIMODE_NONE;
		ai.groundDist = GetRadius() / 1.35F;
		ai.lzPos = g_SceneMan.MovePointToGround(m_Pos, static_cast<int>(ai.groundDist), 9);
		ai.velIntegrator = 0.0F;
		ai.angPID = AutopilotPID{2.7F, 0.01F, 0.9F, 0.6F, 150.0F, interval, GetRotAngle(), GetRotAngle()};
		ai.xPID = AutopilotPID{0.2F, 0.0F, 0.5F, 0.6F, 100.0F, interval};
		ai.yPID = AutopilotPID{0.02F, 0.07F, 4.0F, 0.5F, 30.0F, interval, ai.lzPos.m_Y, ai.lzPos.m_Y};
		if (m_AIMode == AIMODE_DELIVER && IsInventoryEmpty() && g_ActivityMan.GetActivity() && g_ActivityMan.GetActivity()->IsHumanTeam(m_Team)) {
			m_AIMode = AIMODE_STAY;
		}
	}

	// The player flew it a while (no autopilot update for half a second): the landing zone brought back near where it is now.
	if (ai.playerInterferedTimer.IsPastSimTimeLimit()) {
		ai.stuckTimer.Reset();
		AutopilotMoveLZ();
	}
	ai.playerInterferedTimer.Reset();

	// A new order: home to orbit, or down.
	if (m_AIMode != ai.lastAIMode) {
		ai.lastAIMode = static_cast<AIMode>(m_AIMode);
		if (m_AIMode == AIMODE_RETURN) {
			ai.deliveryState = LAUNCH;
			ai.lzPos.m_Y = -10000.0F;
		} else {
			ai.deliveryState = FALL;
		}
		AutopilotMoveLZ();
	}

	// Stable and upright for a while: the landing zone right under it.
	ai.velIntegrator = ai.velIntegrator * 0.8F + m_Vel.GetMagnitude() * 0.2F;
	if (ai.velIntegrator > 5.0F || std::abs(GetAngularVel()) > 1.0F) {
		ai.stableTimer.Reset();
	} else {
		ai.lzPos.m_X = m_Pos.m_X;
	}

	auto isCraft = [](const MovableObject* mo) { return mo && (mo->GetClassName() == "ACDropShip" || mo->GetClassName() == "ACRocket"); };

	// The delivery.
	if (ai.deliveryState == FALL) {
		// About to hit something on the way down: the landing zone brought nearer.
		if (ai.lzPos.m_Y - m_Pos.m_Y > GetRadius()) {
			Vector obstaclePos;
			Vector freePos;
			if (g_SceneMan.CastObstacleRay(m_Pos, (m_Vel + Vector(0.0F, 4.0F)) * 15.0F, obstaclePos, freePos, GetID(), IgnoresWhichTeam(), 0, 10) > -1.0F) {
				AutopilotMoveLZ();
			}
		}
		// Something in the way of the descent: a craft, or a team mate, stepped round.
		if (ai.obstacleTimer.IsPastSimTimeLimit()) {
			ai.obstacleTimer.Reset();
			MOID hit = g_SceneMan.CastMORay(m_Pos, Vector(m_Vel.m_X + RandomNum(-1.0F, 1.0F), std::max(m_Vel.m_Y, 4.0F)) * 40.0F, GetID(), IgnoresWhichTeam(), 0, false, 5);
			if (hit != g_NoMOID) {
				MovableObject* mo = g_MovableMan.GetMOFromID(g_MovableMan.GetRootMOID(hit));
				if (isCraft(mo)) {
					ai.lzPos.SetXY(mo->GetPos().m_X + mo->GetRadius() * 3.0F, std::max(m_Pos.m_Y - 200.0F, 0.0F));
					ai.obstacleTimer.SetSimTimeLimitMS(750);
					ai.obstacle = true;
				} else if (mo && g_MovableMan.IsActor(mo) && mo->GetTeam() == m_Team) {
					float newLZx = mo->GetPos().m_X + mo->GetDiameter() + GetDiameter();
					if (newLZx > sceneWidth) {
						newLZx = g_SceneMan.SceneWrapsX() ? newLZx - sceneWidth : mo->GetPos().m_X - (mo->GetDiameter() + GetDiameter());
					}
					ai.lzPos.SetXY(newLZx, std::max(mo->GetPos().m_Y - 200.0F, 0.0F));
				}
			} else if (ai.obstacle) {
				ai.obstacle = false;
				AutopilotMoveLZ();
				ai.obstacleTimer.SetSimTimeLimitMS(100);
			}
		}
		if (m_AIMode == AIMODE_DELIVER && IsInventoryEmpty()) {
			// Nothing to deliver: home.
			m_AIMode = AIMODE_RETURN;
			ai.deliveryState = LAUNCH;
			ai.lzPos.m_Y = -10000.0F;
		} else {
			if (ai.stableTimer.IsPastSimMS(500)) {
				ai.lzPos = g_SceneMan.MovePointToGround(m_Pos, static_cast<int>(ai.groundDist), 6);
			}
			if (m_AIMode != AIMODE_STAY && g_SceneMan.ShortestDistance(m_Pos, ai.lzPos, false).MagnitudeIsLessThan(25.0F)) {
				ai.deliveryState = UNLOAD;
			}
		}
	} else if (ai.deliveryState == UNLOAD) {
		if (IsInventoryEmpty() && m_AIMode != AIMODE_STAY) {
			// Empty: a pause, then home.
			if (ai.doorTimer.IsPastSimMS(750)) {
				m_AIMode = AIMODE_RETURN;
				ai.deliveryState = LAUNCH;
				ai.lzPos.m_Y = -10000.0F;
				if (GetHatchState() == OPEN) {
					CloseHatch();
				}
			}
		} else if (ai.stableTimer.IsPastSimMS(300) && GetHatchState() == CLOSED) {
			OpenHatch();
			ai.doorTimer.Reset();
		}
	} else if (ai.deliveryState == LAUNCH) {
		// Another craft in the way of the climb: stepped round.
		if (ai.obstacleTimer.IsPastSimTimeLimit()) {
			ai.obstacleTimer.Reset();
			MOID hit = g_SceneMan.CastMORay(m_Pos, Vector(m_Vel.m_X + RandomNum(-1.0F, 1.0F), std::min(m_Vel.m_Y, -1.0F)) * 50.0F, GetID(), IgnoresWhichTeam(), 0, false, 5);
			if (hit != g_NoMOID) {
				MovableObject* mo = g_MovableMan.GetMOFromID(g_MovableMan.GetRootMOID(hit));
				if (isCraft(mo)) {
					ai.lzPos.SetXY(mo->GetPos().m_X - (mo->GetDiameter() + GetDiameter()), m_Pos.m_Y + m_Vel.m_Y);
					ai.obstacleTimer.SetSimTimeLimitMS(750);
					ai.obstacle = true;
				}
			} else if (ai.obstacle) {
				ai.obstacle = false;
				ai.lzPos.m_Y = -10000.0F;
				AutopilotMoveLZ();
				ai.obstacleTimer.SetSimTimeLimitMS(100);
			} else {
				AutopilotMoveLZ();
			}
		}
	}

	if (m_AIMode == AIMODE_GOTO) {
		ai.lzPos = GetLastAIWaypoint();
		ai.deliveryState = LAUNCH;
	}

	const float rotAngle = GetRotAngle();
	// Up and down: the main engine, in bursts so it doesn't overshoot.
	if (ai.deliveryState != UNLOAD) {
		float change = ai.yPID.Update(g_SceneMan.ShortestDistance(m_Pos + m_Vel, ai.lzPos, false).m_Y, 0.0F);
		if (std::abs(rotAngle) < 0.9F) {
			if (m_AIMode == AIMODE_GOTO) {
				if (change < 0.0F) {
					ai.burstUp = false;
					ctrl.SetState(MOVE_UP, true);
				}
			} else if (ai.deliveryState == LAUNCH && change < -7.0F) {
				// (No bursts on the way home.)
				ai.burstUp = false;
				ctrl.SetState(MOVE_UP, true);
			} else if (change < -2.2F && !ai.burstUp) {
				ai.burstUp = true;
				ai.burstUpWait = std::max(9.0F - change, 2.0F);
				if (change < -6.0F) {
					ctrl.SetState(MOVE_UP, true);
					ai.burstUp = false;
				}
			} else if (change > 20.0F) {
				ai.burstUp = false;
				ctrl.SetState(MOVE_DOWN, true);
			}
		} else if (rotAngle > 2.14F && rotAngle < 4.14F) {
			// Upside down.
			ctrl.SetState(MOVE_DOWN, true);
		}
	}

	// Sideways: the rocket leans to move.
	float change = ai.xPID.Update(g_SceneMan.ShortestDistance(m_Pos + m_Vel * 20.0F, ai.lzPos, false).m_X, 0.0F);
	float targetAng = 0.0F;
	if (m_Vel.m_Y > 0.0F) {
		if (change < -4.0F) {
			targetAng = -std::max(change / 40.0F, -0.5F);
		} else if (change > 4.0F) {
			targetAng = -std::min(change / 40.0F, 0.5F);
		}
	} else {
		if (change > 4.0F) {
			targetAng = -std::max(change / 40.0F, -0.5F);
		} else if (change < -4.0F) {
			targetAng = -std::min(change / 40.0F, 0.5F);
		}
	}

	// The lean, on the side thrusters, in bursts (none for a slight tilt).
	change = ai.angPID.Update(rotAngle + GetAngularVel(), targetAng);
	if (static_cast<int>(std::floor(std::abs(rotAngle) + 0.9F)) != 0) {
		if (change > 1.1F && !ai.burstRight) {
			ai.burstRight = true;
			ai.burstRightWait = std::max(5.0F - change, 2.0F);
		} else if (change < -1.1F && !ai.burstLeft) {
			ai.burstLeft = true;
			ai.burstLeftWait = std::max(5.0F + change, 2.0F);
		}
	}

	// The bursts: a wait, then fired for a few frames.
	if (ai.burstRight) {
		ai.burstRightWait -= static_cast<float>(interval);
		if (ai.burstRightWait < 0.0F) {
			ctrl.SetState(MOVE_RIGHT, true);
			if (ai.burstRightWait < -4.0F) {
				ai.burstRight = false;
			}
		}
	}
	if (ai.burstLeft) {
		ai.burstLeftWait -= static_cast<float>(interval);
		if (ai.burstLeftWait < 0.0F) {
			ctrl.SetState(MOVE_LEFT, true);
			if (ai.burstLeftWait < -4.0F) {
				ai.burstLeft = false;
			}
		}
	}
	if (ai.burstUp) {
		ai.burstUpWait -= static_cast<float>(interval);
		if (ai.burstUpWait < 0.0F) {
			ctrl.SetState(MOVE_UP, true);
			if (ai.burstUpWait < -8.0F) {
				ai.burstUp = false;
			}
		}
	}

	if (m_AIMode == AIMODE_STAY) {
		ai.stuckTimer.Reset();
		// (No main engine at a standstill: it closes the hatch.)
		if (m_Vel.GetLargest() < 6.0F) {
			ctrl.SetState(MOVE_UP, false);
		}
	} else if (m_Vel.GetLargest() > 3.0F) {
		ai.stuckTimer.Reset();
	} else if (m_AIMode == AIMODE_SCUTTLE || ai.stuckTimer.IsPastSimMS(40000)) {
		// Hopelessly stuck: scuttled.
		GibThis();
	}
}
