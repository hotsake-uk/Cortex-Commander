#include "ACrab.h"
#include "SmokeGrid.h"

#include "AtomGroup.h"
#include "Attachable.h"
#include "ThrownDevice.h"
#include "Turret.h"
#include "Leg.h"
#include "Controller.h"
#include "Matrix.h"
#include "AEJetpack.h"
#include "HDFirearm.h"
#include "Scene.h"
#include "SettingsMan.h"
#include "PresetMan.h"
#include "FrameMan.h"
#include "UInputMan.h"
#include "PieMenu.h"

#include "GUI.h"
#include "AllegroBitmap.h"

#include "tracy/Tracy.hpp"

using namespace RTE;

ConcreteClassInfo(ACrab, Actor, 20);

ACrab::ACrab() {
	Clear();
}

ACrab::~ACrab() {
	Destroy(true);
}

void ACrab::Clear() {
	m_pTurret = 0;
	m_pLFGLeg = 0;
	m_pLBGLeg = 0;
	m_pRFGLeg = 0;
	m_pRBGLeg = 0;
	m_pLFGFootGroup = 0;
	m_BackupLFGFootGroup = nullptr;
	m_pLBGFootGroup = 0;
	m_BackupLBGFootGroup = nullptr;
	m_pRFGFootGroup = 0;
	m_BackupRFGFootGroup = nullptr;
	m_pRBGFootGroup = 0;
	m_BackupRBGFootGroup = nullptr;
	m_StrideSound = nullptr;
	m_pJetpack = nullptr;
	m_MovementState = STAND;
	m_StrideFrame = false;
	for (int side = 0; side < SIDECOUNT; ++side) {
		for (int layer = 0; layer < LAYERCOUNT; ++layer) {
			for (int state = 0; state < MOVEMENTSTATECOUNT; ++state) {
				m_Paths[side][layer][state].Reset();
				m_Paths[side][layer][state].Terminate();
			}
		}
		m_StrideStart[side] = false;
		//        m_StrideTimer[side].Reset();
	}
	m_Aiming = false;
	m_AimRangeUpperLimit = -1;
	m_AimRangeLowerLimit = -1;
	m_LockMouseAimInput = false;
}

int ACrab::Create() {
	// Read all the properties
	if (Actor::Create() < 0) {
		return -1;
	}

	if (m_AIMode == Actor::AIMODE_NONE) {
		m_AIMode = Actor::AIMODE_BRAINHUNT;
	}

	// Create the background paths copied from the foreground ones which were already read in
	for (int side = 0; side < SIDECOUNT; ++side) {
		for (int i = 0; i < MOVEMENTSTATECOUNT; ++i) {
			m_Paths[side][BGROUND][i].Destroy();
			m_Paths[side][BGROUND][i].Create(m_Paths[side][FGROUND][i]);
		}
	}

	// All ACrabs by default avoid hitting each other ont he same team
	m_IgnoresTeamHits = true;

	// Check whether UpperLimit and LowerLimit are defined, if not, copy general AimRange value to preserve compatibility
	if (m_AimRangeUpperLimit == -1 || m_AimRangeLowerLimit == -1) {
		m_AimRangeUpperLimit = m_AimRange;
		m_AimRangeLowerLimit = m_AimRange;
	}

	return 0;
}

/*
int ACrab::Create(BITMAP *pSprite,
                   Controller *pController,
                   const float mass,
                   const Vector &position,
                   const Vector &velocity,
                   AtomGroup *hitBody,
                   const unsigned long lifetime,
                   Status status,
                   const int health)
{


    return Actor::Create(pSprite,
                         pController,
                         mass,
                         position,
                         velocity,
                         hitBody,
                         lifetime,
                         status,
                         health);
}
*/

int ACrab::Create(const ACrab& reference) {
	if (reference.m_pLBGLeg) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pLBGLeg->GetUniqueID());
	}
	if (reference.m_pRBGLeg) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pRBGLeg->GetUniqueID());
	}
	if (reference.m_pJetpack) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pJetpack->GetUniqueID());
	}
	if (reference.m_pTurret) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pTurret->GetUniqueID());
	}
	if (reference.m_pLFGLeg) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pLFGLeg->GetUniqueID());
	}
	if (reference.m_pRFGLeg) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_pRFGLeg->GetUniqueID());
	}

	Actor::Create(reference);

	// Note - hardcoded attachable copying is organized based on desired draw order here.
	if (reference.m_pLBGLeg) {
		SetLeftBGLeg(dynamic_cast<Leg*>(reference.m_pLBGLeg->Clone()));
	}
	if (reference.m_pRBGLeg) {
		SetRightBGLeg(dynamic_cast<Leg*>(reference.m_pRBGLeg->Clone()));
	}
	if (reference.m_pJetpack) {
		SetJetpack(dynamic_cast<AEJetpack*>(reference.m_pJetpack->Clone()));
	}
	if (reference.m_pTurret) {
		SetTurret(dynamic_cast<Turret*>(reference.m_pTurret->Clone()));
	}
	if (reference.m_pLFGLeg) {
		SetLeftFGLeg(dynamic_cast<Leg*>(reference.m_pLFGLeg->Clone()));
	}
	if (reference.m_pRFGLeg) {
		SetRightFGLeg(dynamic_cast<Leg*>(reference.m_pRFGLeg->Clone()));
	}

	AtomGroup* atomGroupToUseAsFootGroupLFG = reference.m_pLFGFootGroup ? dynamic_cast<AtomGroup*>(reference.m_pLFGFootGroup->Clone()) : m_pLFGLeg->GetFootGroupFromFootAtomGroup();
	RTEAssert(atomGroupToUseAsFootGroupLFG, "Failed to fallback to using LFGFoot AtomGroup as LFGFootGroup in preset " + this->GetModuleAndPresetName() + "!\nPlease define a LFGFootGroup or LFGLeg Foot attachable!");

	AtomGroup* atomGroupToUseAsFootGroupLBG = reference.m_pLBGFootGroup ? dynamic_cast<AtomGroup*>(reference.m_pLBGFootGroup->Clone()) : m_pLBGLeg->GetFootGroupFromFootAtomGroup();
	RTEAssert(atomGroupToUseAsFootGroupLBG, "Failed to fallback to using LBGFoot AtomGroup as LBGFootGroup in preset " + this->GetModuleAndPresetName() + "!\nPlease define a LBGFootGroup or LBGLeg Foot attachable!");

	AtomGroup* atomGroupToUseAsFootGroupRFG = reference.m_pRFGFootGroup ? dynamic_cast<AtomGroup*>(reference.m_pRFGFootGroup->Clone()) : m_pRFGLeg->GetFootGroupFromFootAtomGroup();
	RTEAssert(atomGroupToUseAsFootGroupRFG, "Failed to fallback to using RFGFoot AtomGroup as RFGFootGroup in preset " + this->GetModuleAndPresetName() + "!\nPlease define a RFGFootGroup or RFGLeg Foot attachable!");

	AtomGroup* atomGroupToUseAsFootGroupRBG = reference.m_pRBGFootGroup ? dynamic_cast<AtomGroup*>(reference.m_pRBGFootGroup->Clone()) : m_pRBGLeg->GetFootGroupFromFootAtomGroup();
	RTEAssert(atomGroupToUseAsFootGroupRBG, "Failed to fallback to using RBGFoot AtomGroup as RBGFootGroup in preset " + this->GetModuleAndPresetName() + "!\nPlease define a RBGFootGroup or RBGLeg Foot attachable!");

	m_pLFGFootGroup = atomGroupToUseAsFootGroupLFG;
	m_pLFGFootGroup->SetOwner(this);
	m_BackupLFGFootGroup = dynamic_cast<AtomGroup*>(atomGroupToUseAsFootGroupLFG->Clone());
	m_BackupLFGFootGroup->RemoveAllAtoms();
	m_BackupLFGFootGroup->SetOwner(this);
	m_BackupLFGFootGroup->SetLimbPos(atomGroupToUseAsFootGroupLFG->GetLimbPos());
	m_pLBGFootGroup = atomGroupToUseAsFootGroupLBG;
	m_pLBGFootGroup->SetOwner(this);
	m_BackupLBGFootGroup = dynamic_cast<AtomGroup*>(atomGroupToUseAsFootGroupLBG->Clone());
	m_BackupLBGFootGroup->RemoveAllAtoms();
	m_BackupLBGFootGroup->SetOwner(this);
	m_BackupLBGFootGroup->SetLimbPos(atomGroupToUseAsFootGroupLFG->GetLimbPos());
	m_pRFGFootGroup = atomGroupToUseAsFootGroupRFG;
	m_pRFGFootGroup->SetOwner(this);
	m_BackupRFGFootGroup = dynamic_cast<AtomGroup*>(atomGroupToUseAsFootGroupRFG->Clone());
	m_BackupRFGFootGroup->RemoveAllAtoms();
	m_BackupRFGFootGroup->SetOwner(this);
	m_BackupRFGFootGroup->SetLimbPos(atomGroupToUseAsFootGroupLFG->GetLimbPos());
	m_pRBGFootGroup = atomGroupToUseAsFootGroupRBG;
	m_pRBGFootGroup->SetOwner(this);
	m_BackupRBGFootGroup = dynamic_cast<AtomGroup*>(atomGroupToUseAsFootGroupRBG->Clone());
	m_BackupRBGFootGroup->RemoveAllAtoms();
	m_BackupRBGFootGroup->SetOwner(this);
	m_BackupRBGFootGroup->SetLimbPos(atomGroupToUseAsFootGroupLFG->GetLimbPos());

	if (reference.m_StrideSound) {
		m_StrideSound = dynamic_cast<SoundContainer*>(reference.m_StrideSound->Clone());
	}

	m_MovementState = reference.m_MovementState;

	for (int side = 0; side < SIDECOUNT; ++side) {
		for (int i = 0; i < MOVEMENTSTATECOUNT; ++i) {
			m_Paths[side][FGROUND][i].Create(reference.m_Paths[side][FGROUND][i]);
			m_Paths[side][BGROUND][i].Create(reference.m_Paths[side][BGROUND][i]);
		}
	}

	m_AimRangeUpperLimit = reference.m_AimRangeUpperLimit;
	m_AimRangeLowerLimit = reference.m_AimRangeLowerLimit;
	m_LockMouseAimInput = reference.m_LockMouseAimInput;

	return 0;
}

int ACrab::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Actor::ReadProperty(propName, reader));

	MatchProperty("Turret", { SetTurret(dynamic_cast<Turret*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("Jetpack", { SetJetpack(AEJetpack::FromReadPreset(g_PresetMan.ReadReflectedPreset(reader))); });
	// Older mods set how the jetpack flies on the unit itself. Those settings live on the jetpack now, so they're passed on to it.
	MatchProperty("JumpTime", {
		float jumpTime;
		reader >> jumpTime;
		if (m_pJetpack) {
			m_pJetpack->SetJetTimeTotal(jumpTime * 1000.0F);
			m_pJetpack->SetJetTimeLeft(jumpTime * 1000.0F);
		}
	});
	MatchProperty("JumpReplenishRate", {
		float replenishRate;
		reader >> replenishRate;
		if (m_pJetpack) {
			m_pJetpack->SetJetReplenishRate(replenishRate);
		}
	});
	MatchProperty("JumpAngleRange", {
		float angleRange;
		reader >> angleRange;
		if (m_pJetpack) {
			m_pJetpack->SetJetAngleRange(angleRange);
		}
	});
	MatchForwards("LFGLeg") MatchProperty("LeftFGLeg", { SetLeftFGLeg(dynamic_cast<Leg*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("LBGLeg") MatchProperty("LeftBGLeg", { SetLeftBGLeg(dynamic_cast<Leg*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("RFGLeg") MatchProperty("RightFGLeg", { SetRightFGLeg(dynamic_cast<Leg*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("RBGLeg") MatchProperty("RightBGLeg", { SetRightBGLeg(dynamic_cast<Leg*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("LFootGroup") MatchProperty("LeftFootGroup", {
		delete m_pLFGFootGroup;
		delete m_pLBGFootGroup;
		delete m_BackupLFGFootGroup;
		delete m_BackupLBGFootGroup;
		m_pLFGFootGroup = new AtomGroup();
		m_pLBGFootGroup = new AtomGroup();
		reader >> m_pLFGFootGroup;
		m_pLBGFootGroup->Create(*m_pLFGFootGroup);
		m_pLFGFootGroup->SetOwner(this);
		m_pLBGFootGroup->SetOwner(this);
		m_BackupLFGFootGroup = new AtomGroup(*m_pLFGFootGroup);
		m_BackupLFGFootGroup->RemoveAllAtoms();
		m_BackupLBGFootGroup = new AtomGroup(*m_BackupLFGFootGroup);
	});
	MatchForwards("RFootGroup") MatchProperty("RightFootGroup", {
		delete m_pRFGFootGroup;
		delete m_pRBGFootGroup;
		delete m_BackupRFGFootGroup;
		delete m_BackupRBGFootGroup;
		m_pRFGFootGroup = new AtomGroup();
		m_pRBGFootGroup = new AtomGroup();
		reader >> m_pRFGFootGroup;
		m_pRBGFootGroup->Create(*m_pRFGFootGroup);
		m_pRFGFootGroup->SetOwner(this);
		m_pRBGFootGroup->SetOwner(this);
		m_BackupRFGFootGroup = new AtomGroup(*m_pRFGFootGroup);
		m_BackupRFGFootGroup->RemoveAllAtoms();
		m_BackupRBGFootGroup = new AtomGroup(*m_BackupRFGFootGroup);
	});
	MatchForwards("LFGFootGroup") MatchProperty("LeftFGFootGroup", {
		delete m_pLFGFootGroup;
		delete m_BackupLFGFootGroup;
		m_pLFGFootGroup = new AtomGroup();
		reader >> m_pLFGFootGroup;
		m_pLFGFootGroup->SetOwner(this);
		m_BackupLFGFootGroup = new AtomGroup(*m_pLFGFootGroup);
		m_BackupLFGFootGroup->RemoveAllAtoms();
	});
	MatchForwards("LBGFootGroup") MatchProperty("LeftBGFootGroup", {
		delete m_pLBGFootGroup;
		delete m_BackupLBGFootGroup;
		m_pLBGFootGroup = new AtomGroup();
		reader >> m_pLBGFootGroup;
		m_pLBGFootGroup->SetOwner(this);
		m_BackupLBGFootGroup = new AtomGroup(*m_pLBGFootGroup);
		m_BackupLBGFootGroup->RemoveAllAtoms();
	});
	MatchForwards("RFGFootGroup") MatchProperty("RightFGFootGroup", {
		delete m_pRFGFootGroup;
		delete m_BackupRFGFootGroup;
		m_pRFGFootGroup = new AtomGroup();
		reader >> m_pRFGFootGroup;
		m_pRFGFootGroup->SetOwner(this);
		m_BackupRFGFootGroup = new AtomGroup(*m_pRFGFootGroup);
		m_BackupRFGFootGroup->RemoveAllAtoms();
	});
	MatchForwards("RBGFootGroup") MatchProperty("RightBGFootGroup", {
		delete m_pRBGFootGroup;
		delete m_BackupRBGFootGroup;
		m_pRBGFootGroup = new AtomGroup();
		reader >> m_pRBGFootGroup;
		m_pRBGFootGroup->SetOwner(this);
		m_BackupRBGFootGroup = new AtomGroup(*m_pRBGFootGroup);
		m_BackupRBGFootGroup->RemoveAllAtoms();
	});
	MatchProperty("StrideSound", {
		m_StrideSound = new SoundContainer;
		reader >> m_StrideSound;
	});
	MatchForwards("LStandLimbPath") MatchProperty("LeftStandLimbPath", { reader >> m_Paths[LEFTSIDE][FGROUND][STAND]; });
	MatchForwards("LWalkLimbPath") MatchProperty("LeftWalkLimbPath", { reader >> m_Paths[LEFTSIDE][FGROUND][WALK]; });
	MatchForwards("LDislodgeLimbPath") MatchProperty("LeftDislodgeLimbPath", { reader >> m_Paths[LEFTSIDE][FGROUND][DISLODGE]; });
	MatchForwards("RStandLimbPath") MatchProperty("RightStandLimbPath", { reader >> m_Paths[RIGHTSIDE][FGROUND][STAND]; });
	MatchForwards("RWalkLimbPath") MatchProperty("RightWalkLimbPath", { reader >> m_Paths[RIGHTSIDE][FGROUND][WALK]; });
	MatchForwards("RDislodgeLimbPath") MatchProperty("RightDislodgeLimbPath", { reader >> m_Paths[RIGHTSIDE][FGROUND][DISLODGE]; });
	MatchProperty("AimRangeUpperLimit", { reader >> m_AimRangeUpperLimit; });
	MatchProperty("AimRangeLowerLimit", { reader >> m_AimRangeLowerLimit; });
	MatchProperty("LockMouseAimInput", { reader >> m_LockMouseAimInput; });

	EndPropertyList;
}

int ACrab::Save(Writer& writer) const {
	Actor::Save(writer);

	writer.NewProperty("Turret");
	writer << m_pTurret;
	writer.NewProperty("Jetpack");
	writer << m_pJetpack;
	writer.NewProperty("LFGLeg");
	writer << m_pLFGLeg;
	writer.NewProperty("LBGLeg");
	writer << m_pLBGLeg;
	writer.NewProperty("RFGLeg");
	writer << m_pRFGLeg;
	writer.NewProperty("RBGLeg");
	writer << m_pRBGLeg;
	writer.NewProperty("LFGFootGroup");
	writer << m_pLFGFootGroup;
	writer.NewProperty("LBGFootGroup");
	writer << m_pLBGFootGroup;
	writer.NewProperty("RFGFootGroup");
	writer << m_pRFGFootGroup;
	writer.NewProperty("RBGFootGroup");
	writer << m_pRBGFootGroup;
	writer.NewProperty("StrideSound");
	writer << m_StrideSound;

	writer.NewProperty("LStandLimbPath");
	writer << m_Paths[LEFTSIDE][FGROUND][STAND];
	writer.NewProperty("LWalkLimbPath");
	writer << m_Paths[LEFTSIDE][FGROUND][WALK];
	writer.NewProperty("LDislodgeLimbPath");
	writer << m_Paths[LEFTSIDE][FGROUND][DISLODGE];
	writer.NewProperty("RStandLimbPath");
	writer << m_Paths[RIGHTSIDE][FGROUND][STAND];
	writer.NewProperty("RWalkLimbPath");
	writer << m_Paths[RIGHTSIDE][FGROUND][WALK];
	writer.NewProperty("RDislodgeLimbPath");
	writer << m_Paths[RIGHTSIDE][FGROUND][DISLODGE];

	writer.NewProperty("AimRangeUpperLimit");
	writer << m_AimRangeUpperLimit;
	writer.NewProperty("AimRangeLowerLimit");
	writer << m_AimRangeLowerLimit;
	writer.NewProperty("LockMouseAimInput");
	writer << m_LockMouseAimInput;

	return 0;
}

void ACrab::Destroy(bool notInherited) {
	delete m_pLFGFootGroup;
	delete m_pLBGFootGroup;
	delete m_pRFGFootGroup;
	delete m_pRBGFootGroup;

	delete m_StrideSound;
	//    for (deque<LimbPath *>::iterator itr = m_WalkPaths.begin();
	//         itr != m_WalkPaths.end(); ++itr)
	//        delete *itr;

	if (!notInherited)
		Actor::Destroy();
	Clear();
}

/*
float ACrab::GetTotalValue(int nativeModule, float foreignMult) const
{
    float totalValue = Actor::GetTotalValue(nativeModule, foreignMult);

    return totalValue;
}
*/
/*
Vector ACrab::GetCPUPos() const
{
    if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->GetMountedMO())
        return m_pTurret->GetMountedMO()->GetPos();

    return m_Pos;
}
*/

Vector ACrab::GetEyePos() const {
	if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice())
		return m_pTurret->GetFirstMountedDevice()->GetPos();

	return m_Pos;
}

void ACrab::SetTurret(Turret* newTurret) {
	if (m_pTurret && m_pTurret->IsAttached()) {
		RemoveAndDeleteAttachable(m_pTurret);
	}
	if (newTurret == nullptr) {
		m_pTurret = nullptr;
	} else {
		m_pTurret = newTurret;
		AddAttachable(newTurret);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newTurret->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 Turret* castedAttachable = dynamic_cast<Turret*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetTurret");
			                                                 dynamic_cast<ACrab*>(parent)->SetTurret(castedAttachable);
		                                                 }});

		if (m_pTurret->HasNoSetDamageMultiplier()) {
			m_pTurret->SetDamageMultiplier(5.0F);
		}
	}
}

void ACrab::SetJetpack(AEJetpack* newJetpack) {
	if (m_pJetpack && m_pJetpack->IsAttached()) {
		RemoveAndDeleteAttachable(m_pJetpack);
	}
	if (newJetpack == nullptr) {
		m_pJetpack = nullptr;
	} else {
		m_pJetpack = newJetpack;
		AddAttachable(newJetpack);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newJetpack->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 AEJetpack* castedAttachable = dynamic_cast<AEJetpack*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetJetpack");
			                                                 dynamic_cast<ACrab*>(parent)->SetJetpack(castedAttachable);
		                                                 }});

		if (m_pJetpack->HasNoSetDamageMultiplier()) {
			m_pJetpack->SetDamageMultiplier(0.0F);
		}
		m_pJetpack->SetApplyTransferredForcesAtOffset(false);
		m_pJetpack->SetDeleteWhenRemovedFromParent(true);
	}
}

void ACrab::SetLeftFGLeg(Leg* newLeg) {
	if (m_pLFGLeg && m_pLFGLeg->IsAttached()) {
		RemoveAndDeleteAttachable(m_pLFGLeg);
	}
	if (newLeg == nullptr) {
		m_pLFGLeg = nullptr;
	} else {
		m_pLFGLeg = newLeg;
		AddAttachable(newLeg);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newLeg->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 Leg* castedAttachable = dynamic_cast<Leg*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetLeftFGLeg");
			                                                 dynamic_cast<ACrab*>(parent)->SetLeftFGLeg(castedAttachable);
		                                                 }});

		if (m_pLFGLeg->HasNoSetDamageMultiplier()) {
			m_pLFGLeg->SetDamageMultiplier(1.0F);
		}
		m_pLFGLeg->SetInheritsHFlipped(-1);
	}
}

void ACrab::SetLeftBGLeg(Leg* newLeg) {
	if (m_pLBGLeg && m_pLBGLeg->IsAttached()) {
		RemoveAndDeleteAttachable(m_pLBGLeg);
	}
	if (newLeg == nullptr) {
		m_pLBGLeg = nullptr;
	} else {
		m_pLBGLeg = newLeg;
		AddAttachable(newLeg);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newLeg->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 Leg* castedAttachable = dynamic_cast<Leg*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetLeftBGLeg");
			                                                 dynamic_cast<ACrab*>(parent)->SetLeftBGLeg(castedAttachable);
		                                                 }});

		if (m_pLBGLeg->HasNoSetDamageMultiplier()) {
			m_pLBGLeg->SetDamageMultiplier(1.0F);
		}
		m_pLBGLeg->SetInheritsHFlipped(-1);
	}
}

void ACrab::SetRightFGLeg(Leg* newLeg) {
	if (m_pRFGLeg && m_pRFGLeg->IsAttached()) {
		RemoveAndDeleteAttachable(m_pRFGLeg);
	}
	if (newLeg == nullptr) {
		m_pRFGLeg = nullptr;
	} else {
		m_pRFGLeg = newLeg;
		AddAttachable(newLeg);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newLeg->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 Leg* castedAttachable = dynamic_cast<Leg*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetRightFGLeg");
			                                                 dynamic_cast<ACrab*>(parent)->SetRightFGLeg(castedAttachable);
		                                                 }});

		if (m_pRFGLeg->HasNoSetDamageMultiplier()) {
			m_pRFGLeg->SetDamageMultiplier(1.0F);
		}
	}
}

void ACrab::SetRightBGLeg(Leg* newLeg) {
	if (m_pRBGLeg && m_pRBGLeg->IsAttached()) {
		RemoveAndDeleteAttachable(m_pRBGLeg);
	}
	if (newLeg == nullptr) {
		m_pRBGLeg = nullptr;
	} else {
		m_pRBGLeg = newLeg;
		AddAttachable(newLeg);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newLeg->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 Leg* castedAttachable = dynamic_cast<Leg*>(attachable);
			                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetRightBGLeg");
			                                                 dynamic_cast<ACrab*>(parent)->SetRightBGLeg(castedAttachable);
		                                                 }});

		if (m_pRBGLeg->HasNoSetDamageMultiplier()) {
			m_pRBGLeg->SetDamageMultiplier(1.0F);
		}
	}
}

BITMAP* ACrab::GetGraphicalIcon() const {
	return m_GraphicalIcon ? m_GraphicalIcon : (m_pTurret ? m_pTurret->GetSpriteFrame(0) : GetSpriteFrame(0));
}

bool ACrab::HandlePieCommand(PieSliceType pieSliceIndex) {
	if (pieSliceIndex != PieSliceType::NoType) {
		if (pieSliceIndex == PieSliceType::Reload) {
			m_Controller.SetState(WEAPON_RELOAD);
		} else if (pieSliceIndex == PieSliceType::Sentry) {
			m_AIMode = AIMODE_SENTRY;
		} else if (pieSliceIndex == PieSliceType::Patrol) {
			m_AIMode = AIMODE_PATROL;
		} else if (pieSliceIndex == PieSliceType::BrainHunt) {
			m_AIMode = AIMODE_BRAINHUNT;
			ClearAIWaypoints();
		} else if (pieSliceIndex == PieSliceType::GoTo) {
			m_AIMode = AIMODE_GOTO;
			ClearAIWaypoints();
		} else {
			return Actor::HandlePieCommand(pieSliceIndex);
		}
	}
	return false;
}

MovableObject* ACrab::GetEquippedItem() const {
	if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		return m_pTurret->GetFirstMountedDevice();
	}

	return 0;
}

bool ACrab::FirearmIsReady() const {
	if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		for (const HeldDevice* mountedDevice: m_pTurret->GetMountedDevices()) {
			if (const HDFirearm* mountedFirearm = dynamic_cast<const HDFirearm*>(mountedDevice); mountedFirearm && mountedFirearm->GetRoundInMagCount() != 0) {
				return true;
			}
		}
	}

	return false;
}

bool ACrab::FirearmIsEmpty() const {
	return !FirearmIsReady() && m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice();
}

bool ACrab::FirearmsAreFull() const {
	if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		for (const HeldDevice* mountedDevice: m_pTurret->GetMountedDevices()) {
			if (const HDFirearm* mountedFirearm = dynamic_cast<const HDFirearm*>(mountedDevice); mountedFirearm && !mountedFirearm->IsFull()) {
				return false;
			}
		}
	}
	return true;
}

bool ACrab::FirearmNeedsReload() const {
	if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		for (const HeldDevice* mountedDevice: m_pTurret->GetMountedDevices()) {
			if (const HDFirearm* mountedFirearm = dynamic_cast<const HDFirearm*>(mountedDevice); mountedFirearm && mountedFirearm->NeedsReloading()) {
				return true;
			}
		}
	}

	return false;
}

bool ACrab::FirearmIsSemiAuto() const {
	if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		HDFirearm* pWeapon = dynamic_cast<HDFirearm*>(m_pTurret->GetFirstMountedDevice());
		return pWeapon && !pWeapon->IsFullAuto();
	}
	return false;
}

void ACrab::ReloadFirearms() {
	if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		for (HeldDevice* mountedDevice: m_pTurret->GetMountedDevices()) {
			if (HDFirearm* mountedFirearm = dynamic_cast<HDFirearm*>(mountedDevice)) {
				mountedFirearm->Reload();
			}
		}
	}
}

int ACrab::FirearmActivationDelay() const {
	// Check if the currently held device is already the desired type
	if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		HDFirearm* pWeapon = dynamic_cast<HDFirearm*>(m_pTurret->GetFirstMountedDevice());
		if (pWeapon)
			return pWeapon->GetActivationDelay();
	}

	return 0;
}

bool ACrab::IsWithinRange(Vector& point) const {
	if (m_SharpAimMaxedOut)
		return true;

	Vector diff = g_SceneMan.ShortestDistance(m_Pos, point, false);
	float sqrDistance = diff.GetSqrMagnitude();

	// Really close!
	if (sqrDistance <= (m_CharHeight * m_CharHeight)) {
		return true;
	}

	// Start with the default aim distance
	float range = m_AimDistance;

	// Add the sharp range of the equipped weapon
	if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		range += m_pTurret->GetFirstMountedDevice()->GetSharpLength() * m_SharpAimProgress;
	}

	return sqrDistance <= (range * range);
}

bool ACrab::Look(float FOVSpread, float range) {
	if (!g_SceneMan.AnythingUnseen(m_Team) || m_CanRevealUnseen == false) {
		return false;
	}

	// Set the length of the look vector
	float aimDistance = m_AimDistance + range;
	Vector aimPos = GetCPUPos();

	// If aiming down the barrel, look through that
	if (m_Controller.IsState(AIM_SHARP) && m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		aimPos = m_pTurret->GetFirstMountedDevice()->GetPos();
		aimDistance += m_pTurret->GetFirstMountedDevice()->GetSharpLength();
	}
	// If just looking, use the sensors on the turret instead
	else if (m_pTurret && m_pTurret->IsAttached()) {
		aimPos = GetEyePos();
	}

	// Create the vector to trace along
	Vector lookVector(aimDistance, 0);
	// Set the rotation to the actual aiming angle
	Matrix aimMatrix(m_HFlipped ? -m_AimAngle : m_AimAngle);
	aimMatrix.SetXFlipped(m_HFlipped);
	lookVector *= aimMatrix;
	// Add the spread
	lookVector.DegRotate(FOVSpread * LookRandomNormalNum());

	// The smallest dimension of the fog block, divided by two, but always at least one, as the step for the casts
	int step = (int)g_SceneMan.GetUnseenResolution(m_Team).GetSmallest() / 2;

	// TODO: generate an alarm event if we spot an enemy actor?

	Vector ignored(0, 0);
	return g_SceneMan.CastSeeRay(m_Team, aimPos, lookVector, ignored, 25, step);
}

MovableObject* ACrab::LookForMOs(float FOVSpread, unsigned char ignoreMaterial, bool ignoreAllTerrain) {
	MovableObject* pSeenMO = 0;
	Vector aimPos = m_Pos;
	float aimDistance = m_AimDistance + g_FrameMan.GetPlayerScreenWidth() * 0.51; // Set the length of the look vector

	// If aiming down the barrel, look through that
	if (m_Controller.IsState(AIM_SHARP) && m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		aimPos = m_pTurret->GetFirstMountedDevice()->GetPos();
		aimDistance += m_pTurret->GetFirstMountedDevice()->GetSharpLength();
	}
	// If just looking, use the sensors on the turret instead
	else if (m_pTurret && m_pTurret->IsAttached())
		aimPos = GetEyePos();
	// If no turret...
	else
		aimPos = GetCPUPos();

	// Create the vector to trace along
	Vector lookVector(aimDistance, 0);
	// Set the rotation to the actual aiming angle
	Matrix aimMatrix(m_HFlipped ? -m_AimAngle : m_AimAngle);
	aimMatrix.SetXFlipped(m_HFlipped);
	lookVector *= aimMatrix;
	// Add the spread
	lookVector.DegRotate(FOVSpread * RandomNormalNum());

	// Night: the look ray reaches less far in the dark (see GetNightSightScale).
	lookVector *= GetNightSightScale();

	MOID seenMOID = g_SceneMan.CastMORay(aimPos, lookVector, m_MOID, IgnoresWhichTeam(), ignoreMaterial, ignoreAllTerrain, 5);
	pSeenMO = g_MovableMan.GetMOFromID(seenMOID);
	// Somebody the AI is to take no notice of isn't seen at all.
	if (pSeenMO) {
		if (const Actor* seenActor = dynamic_cast<const Actor*>(g_MovableMan.GetMOFromID(pSeenMO->GetRootID())); seenActor && seenActor->IsIgnoredByAI()) {
			return nullptr;
		}
	}
	// Thick smoke between the eyes and what they'd see hides it.
	if (pSeenMO && SmokeGrid::BlocksSight(aimPos, pSeenMO->GetPos())) {
		return nullptr;
	}
	if (pSeenMO)
		return pSeenMO->GetRootParent();

	return pSeenMO;
}

PathAgent ACrab::GetPathAgent() const {
	PathAgent agent = Actor::GetPathAgent();
	// The legs take stairs: on the test machine the Dreadnought walked the base game's steep stairs (6 px risers on 3 px treads) up in
	// eleven seconds and down in five, offered the walking edge by a chance labelling of a jump's landing.
	agent.WalksStairs = true;
	// Ledges it pulls itself up onto (Actor::TryStartMantle), when the setting is on.
	agent.MantleHeight = g_SettingsMan.MantlingEnabled() ? std::max(m_CharHeight, 20.0F) * 0.3F : 0.0F;
	return agent;
}

float ACrab::EstimateJumpHeight() const {
	if (!m_pJetpack) {
		return 0.0F;
	}

	float totalMass = GetMass();
	float fuelTime = m_pJetpack->GetJetTimeTotal();
	float fuelUseMultiplier = m_pJetpack->GetThrottleFactor();
	float impulseBurst = m_pJetpack->EstimateImpulse(true) / totalMass;
	float impulseThrust = m_pJetpack->EstimateImpulse(false) / totalMass;

	Vector globalAcc = g_SceneMan.GetGlobalAcc() * g_TimerMan.GetDeltaTimeSecs();
	Vector currentVelocity = Vector(0.0F, -impulseBurst);
	float totalHeight = currentVelocity.GetY() * g_TimerMan.GetDeltaTimeSecs() * c_PPM;
	do {
		currentVelocity += globalAcc;
		totalHeight += currentVelocity.GetY() * g_TimerMan.GetDeltaTimeSecs() * c_PPM;
		if (fuelTime > 0.0F) {
			currentVelocity.m_Y -= impulseThrust;
			fuelTime -= g_TimerMan.GetDeltaTimeMS() * fuelUseMultiplier;
		}
	} while (currentVelocity.GetY() < 0.0F);

	float finalCalculatedHeight = totalHeight * -1.0F * c_MPP;
	float finalHeightMultipler = 0.6f; // Make us think we can do less because AI path following is shit
	return finalCalculatedHeight * finalHeightMultipler;
}

void ACrab::OnNewMovePath() {
	Actor::OnNewMovePath();
}

void ACrab::PreControllerUpdate() {
	ZoneScoped;

	Actor::PreControllerUpdate();

	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	float mass = GetMass();

	Vector analogAim = m_Controller.GetAnalogAim();
	const float analogAimDeadzone = 0.1F;

	// Set Default direction of all the paths!
	for (int side = 0; side < SIDECOUNT; ++side) {
		for (int layer = 0; layer < LAYERCOUNT; ++layer) {
			m_Paths[side][layer][WALK].SetHFlip(m_HFlipped);
			m_Paths[side][layer][STAND].SetHFlip(m_HFlipped);
		}
	}

	if (m_pJetpack && m_pJetpack->IsAttached()) {
		m_pJetpack->UpdateBurstState(*this);
	}

	////////////////////////////////////
	// Movement direction
	const float movementThreshold = 1.0F;
	bool isStill = (m_Vel + m_PrevVel).MagnitudeIsLessThan(movementThreshold);

	// If the pie menu is on, try to preserve whatever move state we had before it going into effect.
	// This is only done for digital input, where the user needs to use the keyboard to choose pie slices.
	// For analog input, this doesn't matter - the mouse or aiming analog stick controls the pie menu.
	bool keepOldState = m_Controller.IsKeyboardOnlyControlled() && m_Controller.IsState(PIE_MENU_ACTIVE);

	if (!keepOldState) {
		if (m_Controller.IsState(MOVE_RIGHT) || m_Controller.IsState(MOVE_LEFT) || m_MovementState == JUMP && m_Status != INACTIVE) {
			if (m_MovementState != JUMP) {
				// Restart the stride if we're just starting to walk or crawl
				if (m_MovementState != WALK) {
					m_StrideStart[LEFTSIDE] = true;
					m_StrideStart[RIGHTSIDE] = true;
					MoveOutOfTerrain(g_MaterialGrass);
				}

				m_MovementState = WALK;

				// Was never actually used
				//for (int side = 0; side < SIDECOUNT; ++side) {
				//	m_Paths[side][FGROUND][m_MovementState].SetSpeed(m_Controller.IsState(MOVE_FAST) ? FAST : NORMAL);
				//	m_Paths[side][BGROUND][m_MovementState].SetSpeed(m_Controller.IsState(MOVE_FAST) ? FAST : NORMAL);
				//}
			}

			// Walk backwards if the aiming is already focused in the opposite direction of travel.
			if (std::abs(analogAim.m_X) > 0 || m_Controller.IsState(AIM_SHARP)) {
				for (int side = 0; side < SIDECOUNT; ++side) {
					m_Paths[side][FGROUND][m_MovementState].SetHFlip(m_Controller.IsState(MOVE_LEFT));
					m_Paths[side][BGROUND][m_MovementState].SetHFlip(m_Controller.IsState(MOVE_LEFT));
				}
			} else if ((m_Controller.IsState(MOVE_RIGHT) && m_HFlipped) || (m_Controller.IsState(MOVE_LEFT) && !m_HFlipped)) {
				SetHFlipped(!m_HFlipped);
				m_CheckTerrIntersection = true;
				MoveOutOfTerrain(g_MaterialGrass);
				for (int side = 0; side < SIDECOUNT; ++side) {
					for (int layer = 0; layer < LAYERCOUNT; ++layer) {
						m_Paths[side][layer][m_MovementState].SetHFlip(m_HFlipped);
						m_Paths[side][layer][WALK].Terminate();
						m_Paths[side][layer][STAND].Terminate();
					}
					m_StrideStart[side] = true;
				}
			}
		} else {
			m_MovementState = STAND;
		}
	}

	////////////////////////////////////
	// Reload held MO, if applicable

	if (m_Controller.IsState(WEAPON_RELOAD) && !FirearmsAreFull() && m_Status != INACTIVE) {
		ReloadFirearms();

		if (m_DeviceSwitchSound) {
			m_DeviceSwitchSound->Play(m_Pos);
		}

		// Interrupt sharp aiming
		m_SharpAimTimer.Reset();
		m_SharpAimProgress = 0;
	}

	////////////////////////////////////
	// Aiming

	// Get rotation angle of crab
	float rotAngle = GetRotAngle();

	// Adjust AimRange limits to crab rotation
	float adjustedAimRangeUpperLimit = (m_HFlipped) ? m_AimRangeUpperLimit - rotAngle : m_AimRangeUpperLimit + rotAngle;
	float adjustedAimRangeLowerLimit = (m_HFlipped) ? -m_AimRangeLowerLimit - rotAngle : -m_AimRangeLowerLimit + rotAngle;

	if (m_Controller.IsState(AIM_UP) && m_Status != INACTIVE) {
		// Set the timer to a base number so we don't get a sluggish feeling at start.
		if (m_AimState != AIMUP) {
			m_AimTmr.SetElapsedSimTimeMS(m_AimState == AIMSTILL ? 150 : 300);
		}
		m_AimState = AIMUP;
		m_AimAngle += m_Controller.IsState(AIM_SHARP) ? std::min(static_cast<float>(m_AimTmr.GetElapsedSimTimeMS()) * 0.00005F, 0.05F) : std::min(static_cast<float>(m_AimTmr.GetElapsedSimTimeMS()) * 0.00015F, 0.15F) * m_Controller.GetDigitalAimSpeed();

	} else if (m_Controller.IsState(AIM_DOWN) && m_Status != INACTIVE) {
		// Set the timer to a base number so we don't get a sluggish feeling at start.
		if (m_AimState != AIMDOWN) {
			m_AimTmr.SetElapsedSimTimeMS(m_AimState == AIMSTILL ? 150 : 300);
		}
		m_AimState = AIMDOWN;
		m_AimAngle -= m_Controller.IsState(AIM_SHARP) ? std::min(static_cast<float>(m_AimTmr.GetElapsedSimTimeMS()) * 0.00005F, 0.05F) : std::min(static_cast<float>(m_AimTmr.GetElapsedSimTimeMS()) * 0.00015F, 0.15F) * m_Controller.GetDigitalAimSpeed();

	} else if (analogAim.MagnitudeIsGreaterThan(analogAimDeadzone) && m_Status != INACTIVE) {
		// Hack to avoid the GetAbsRadAngle to mangle an aim angle straight down
		if (analogAim.m_X == 0) {
			analogAim.m_X += 0.01F * GetFlipFactor();
		}
		m_AimAngle = analogAim.GetAbsRadAngle();

		// Check for flip change
		if ((analogAim.m_X > 0 && m_HFlipped) || (analogAim.m_X < 0 && !m_HFlipped)) {
			SetHFlipped(!m_HFlipped);
			// Instead of simply carving out a silhouette of the now flipped actor, isntead disable any atoms which are embedded int eh terrain until they emerge again
			// m_ForceDeepCheck = true;
			m_CheckTerrIntersection = true;
			MoveOutOfTerrain(g_MaterialGrass);
			for (int side = 0; side < SIDECOUNT; ++side) {
				for (int layer = 0; layer < LAYERCOUNT; ++layer) {
					m_Paths[side][layer][m_MovementState].SetHFlip(m_HFlipped);
					m_Paths[side][layer][WALK].Terminate();
					m_Paths[side][layer][STAND].Terminate();
				}
				m_StrideStart[side] = true;
			}
		}
		// Correct angle based on flip
		m_AimAngle = FacingAngle(m_AimAngle);

		// Clamp the analog aim too, so it doesn't feel "sticky" at the edges of the aim limit
		if (m_Controller.IsPlayerControlled() && m_LockMouseAimInput) {
			float mouseAngle = g_UInputMan.AnalogAimValues(m_Controller.GetPlayer()).GetAbsRadAngle();
			Clamp(mouseAngle, FacingAngle(adjustedAimRangeUpperLimit), FacingAngle(adjustedAimRangeLowerLimit));
			g_UInputMan.SetMouseValueAngle(mouseAngle, m_Controller.GetPlayer());
		}
	} else
		m_AimState = AIMSTILL;

	// Clamp aim angle so it's within adjusted limit ranges, for all control types
	Clamp(m_AimAngle, adjustedAimRangeUpperLimit, adjustedAimRangeLowerLimit);

	//////////////////////////////
	// Sharp aim calculation

	if (m_Controller.IsState(AIM_SHARP) && m_Status == STABLE && m_Vel.MagnitudeIsLessThan(5.0F)) {
		float aimMag = analogAim.GetMagnitude();

		// If aim sharp is being done digitally, then translate to full magnitude.
		if (aimMag < 0.1F) {
			aimMag = 1.0F;
		}
		if (m_MovementState == WALK) {
			aimMag *= 0.3F;
		}

		if (m_SharpAimTimer.IsPastSimMS(m_SharpAimDelay)) {
			// Only go slower outward.
			if (m_SharpAimProgress < aimMag) {
				m_SharpAimProgress += (aimMag - m_SharpAimProgress) * 0.035F;
			} else {
				m_SharpAimProgress = aimMag;
			}
		} else {
			m_SharpAimProgress *= 0.95F;
		}
	} else {
		m_SharpAimProgress = std::max(m_SharpAimProgress * 0.95F - 0.1F, 0.0F);
	}

	////////////////////////////////////
	// Fire/Activate held devices

	if (m_pTurret && m_pTurret->IsAttached() && m_Status != INACTIVE) {
		for (HeldDevice* mountedDevice: m_pTurret->GetMountedDevices()) {
			mountedDevice->SetSharpAim(m_SharpAimProgress);
			if (m_Controller.IsState(WEAPON_FIRE)) {
				mountedDevice->Activate();
				if (mountedDevice->IsEmpty()) {
					mountedDevice->Reload();
				}
			} else {
				mountedDevice->Deactivate();
			}
			if (m_Controller.IsState(WEAPON_PRIMARY_HOTKEY)) {
				mountedDevice->ActivateHotkeyAction(HeldDeviceHotkeyType::PRIMARYHOTKEY);
			} else {
				mountedDevice->DeactivateHotkeyAction(HeldDeviceHotkeyType::PRIMARYHOTKEY);
			}
		}
	}

	// Controller disabled
	if (m_Controller.IsDisabled()) {
		m_MovementState = STAND;
		if (m_pJetpack && m_pJetpack->IsAttached())
			m_pJetpack->EnableEmission(false);
	}

	//    m_aSprite->SetAngle((m_AimAngle / 180) * 3.141592654);
	//    m_aSprite->SetScale(2.0);

	///////////////////////////////////////////////////
	// Travel the limb AtomGroup:s

	m_StrideFrame = false;

	if (m_Status == STABLE && !m_LimbPushForcesAndCollisionsDisabled) {
		// This exists to support disabling foot collisions if the limbpath has that flag set.
		if ((m_pLFGFootGroup->GetAtomCount() == 0 && m_BackupLFGFootGroup->GetAtomCount() > 0) != m_Paths[LEFTSIDE][FGROUND][m_MovementState].FootCollisionsShouldBeDisabled()) {
			m_BackupLFGFootGroup->SetLimbPos(m_pLFGFootGroup->GetLimbPos());
			std::swap(m_pLFGFootGroup, m_BackupLFGFootGroup);
		}
		if ((m_pLBGFootGroup->GetAtomCount() == 0 && m_BackupLBGFootGroup->GetAtomCount() > 0) != m_Paths[LEFTSIDE][BGROUND][m_MovementState].FootCollisionsShouldBeDisabled()) {
			m_BackupLBGFootGroup->SetLimbPos(m_pLBGFootGroup->GetLimbPos());
			std::swap(m_pLBGFootGroup, m_BackupLBGFootGroup);
		}
		if ((m_pRFGFootGroup->GetAtomCount() == 0 && m_BackupRFGFootGroup->GetAtomCount() > 0) != m_Paths[RIGHTSIDE][FGROUND][m_MovementState].FootCollisionsShouldBeDisabled()) {
			m_BackupRFGFootGroup->SetLimbPos(m_pRFGFootGroup->GetLimbPos());
			std::swap(m_pRFGFootGroup, m_BackupRFGFootGroup);
		}
		if ((m_pRBGFootGroup->GetAtomCount() == 0 && m_BackupRBGFootGroup->GetAtomCount() > 0) != m_Paths[RIGHTSIDE][BGROUND][m_MovementState].FootCollisionsShouldBeDisabled()) {
			m_BackupRBGFootGroup->SetLimbPos(m_pRBGFootGroup->GetLimbPos());
			std::swap(m_pRBGFootGroup, m_BackupRBGFootGroup);
		}

		// WALKING
		if (m_MovementState == WALK) {
			for (int side = 0; side < SIDECOUNT; ++side)
				for (int layer = 0; layer < LAYERCOUNT; ++layer)
					m_Paths[side][layer][STAND].Terminate();

			float LFGLegProg = m_Paths[LEFTSIDE][FGROUND][WALK].GetRegularProgress();
			float LBGLegProg = m_Paths[LEFTSIDE][BGROUND][WALK].GetRegularProgress();
			float RFGLegProg = m_Paths[RIGHTSIDE][FGROUND][WALK].GetRegularProgress();
			float RBGLegProg = m_Paths[RIGHTSIDE][BGROUND][WALK].GetRegularProgress();

			bool restarted = false;
			Matrix walkAngle(rotAngle * 0.5F);

			// Make sure we are starting a stride if we're basically stopped.
			if (isStill) {
				m_StrideStart[LEFTSIDE] = true;
			}

			//////////////////
			// LEFT LEGS

			if (m_pLFGLeg && (!m_pLBGLeg || (!(m_Paths[LEFTSIDE][FGROUND][WALK].PathEnded() && LBGLegProg < 0.5F) || m_StrideStart[LEFTSIDE]))) {
				m_StrideTimer[LEFTSIDE].Reset();
				m_pLFGFootGroup->PushAsLimb(m_Pos + RotateOffset(m_pLFGLeg->GetParentOffset()), m_pLFGLeg->GetMaxLength(), m_Vel, walkAngle, m_Paths[LEFTSIDE][FGROUND][WALK], deltaTime, &restarted);
			}

			if (m_pLBGLeg) {
				if (!m_pLFGLeg || !(m_Paths[LEFTSIDE][BGROUND][WALK].PathEnded() && LFGLegProg < 0.5F)) {
					m_StrideStart[LEFTSIDE] = false;
					m_StrideTimer[LEFTSIDE].Reset();
					m_pLBGFootGroup->PushAsLimb(m_Pos + RotateOffset(m_pLBGLeg->GetParentOffset()), m_pLBGLeg->GetMaxLength(), m_Vel, walkAngle, m_Paths[LEFTSIDE][BGROUND][WALK], deltaTime);
				} else {
					m_pLBGFootGroup->FlailAsLimb(m_Pos, RotateOffset(m_pLBGLeg->GetParentOffset()), m_pLBGLeg->GetMaxLength(), m_PrevVel, m_AngularVel, m_pLBGLeg->GetMass(), deltaTime);
				}
			}

			// Reset the left-side walking stride if it's taking longer than it should.
			if (m_StrideTimer[LEFTSIDE].IsPastSimMS(static_cast<double>(m_Paths[LEFTSIDE][FGROUND][WALK].GetTotalPathTime() * 1.1F))) {
				m_StrideStart[LEFTSIDE] = true;
			}

			///////////////////
			// RIGHT LEGS

			if (m_pRFGLeg) {
				if (!m_pRBGLeg || !(m_Paths[RIGHTSIDE][FGROUND][WALK].PathEnded() && RBGLegProg < 0.5F)) {
					m_StrideStart[RIGHTSIDE] = false;
					m_StrideTimer[RIGHTSIDE].Reset();
					m_pRFGFootGroup->PushAsLimb(m_Pos + RotateOffset(m_pRFGLeg->GetParentOffset()), m_pRFGLeg->GetMaxLength(), m_Vel, walkAngle, m_Paths[RIGHTSIDE][FGROUND][WALK], deltaTime, &restarted);
				} else {
					m_pRFGFootGroup->FlailAsLimb(m_Pos, RotateOffset(m_pRFGLeg->GetParentOffset()), m_pRFGLeg->GetMaxLength(), m_PrevVel, m_AngularVel, m_pRFGLeg->GetMass(), deltaTime);
				}
			}

			if (m_pRBGLeg && (!m_pRFGLeg || (!(m_Paths[RIGHTSIDE][BGROUND][WALK].PathEnded() && RFGLegProg < 0.5F) || m_StrideStart[RIGHTSIDE]))) {
				m_StrideTimer[RIGHTSIDE].Reset();
				m_pRBGFootGroup->PushAsLimb(m_Pos + RotateOffset(m_pRBGLeg->GetParentOffset()), m_pRBGLeg->GetMaxLength(), m_Vel, walkAngle, m_Paths[RIGHTSIDE][BGROUND][WALK], deltaTime);
			}

			// Reset the right-side walking stride if it's taking longer than it should.
			if (m_StrideTimer[RIGHTSIDE].IsPastSimMS(static_cast<double>(m_Paths[RIGHTSIDE][FGROUND][WALK].GetTotalPathTime() * 1.1F))) {
				m_StrideStart[RIGHTSIDE] = true;
			}

			if (m_StrideSound) {
				m_StrideSound->SetPosition(m_Pos);
				if (m_StrideSound->GetLoopSetting() < 0) {
					if (!m_StrideSound->IsBeingPlayed()) {
						m_StrideSound->Play();
					}
				} else if (restarted) {
					m_StrideSound->Play();
				}
			}

			if (restarted) {
				m_StrideFrame = true;
				RunScriptedFunctionInAppropriateScripts("OnStride");
			}
		} else if (m_pLFGLeg || m_pLBGLeg || m_pRFGLeg || m_pRBGLeg) {
			if (m_MovementState == JUMP) {
				// TODO: Utilize jump paths in an intuitive way?
				if (m_pLFGLeg) {
					m_pLFGFootGroup->FlailAsLimb(m_Pos, RotateOffset(m_pLFGLeg->GetParentOffset()), m_pLFGLeg->GetMaxLength(), m_PrevVel, m_AngularVel, m_pLFGLeg->GetMass(), deltaTime);
				}
				if (m_pLBGLeg) {
					m_pLBGFootGroup->FlailAsLimb(m_Pos, RotateOffset(m_pLBGLeg->GetParentOffset()), m_pLBGLeg->GetMaxLength(), m_PrevVel, m_AngularVel, m_pLBGLeg->GetMass(), deltaTime);
				}
				if (m_pRFGLeg) {
					m_pRFGFootGroup->FlailAsLimb(m_Pos, RotateOffset(m_pRFGLeg->GetParentOffset()), m_pRFGLeg->GetMaxLength(), m_PrevVel, m_AngularVel, m_pRFGLeg->GetMass(), deltaTime);
				}
				if (m_pRBGLeg) {
					m_pRBGFootGroup->FlailAsLimb(m_Pos, RotateOffset(m_pRBGLeg->GetParentOffset()), m_pRBGLeg->GetMaxLength(), m_PrevVel, m_AngularVel, m_pRBGLeg->GetMass(), deltaTime);
				}

				if (m_pJetpack == nullptr || m_pJetpack->IsOutOfFuel()) {
					m_MovementState = STAND;
					m_Paths[LEFTSIDE][FGROUND][JUMP].Terminate();
					m_Paths[LEFTSIDE][BGROUND][JUMP].Terminate();
					m_Paths[LEFTSIDE][FGROUND][STAND].Terminate();
					m_Paths[LEFTSIDE][BGROUND][STAND].Terminate();
					m_Paths[LEFTSIDE][FGROUND][WALK].Terminate();
					m_Paths[LEFTSIDE][BGROUND][WALK].Terminate();
					m_Paths[RIGHTSIDE][FGROUND][JUMP].Terminate();
					m_Paths[RIGHTSIDE][BGROUND][JUMP].Terminate();
					m_Paths[RIGHTSIDE][FGROUND][STAND].Terminate();
					m_Paths[RIGHTSIDE][BGROUND][STAND].Terminate();
					m_Paths[RIGHTSIDE][FGROUND][WALK].Terminate();
					m_Paths[RIGHTSIDE][BGROUND][WALK].Terminate();
				}
			} else {
				for (int side = 0; side < SIDECOUNT; ++side) {
					for (int layer = 0; layer < LAYERCOUNT; ++layer) {
						m_Paths[side][layer][WALK].Terminate();
					}
				}
				if (m_pLFGLeg) {
					m_pLFGFootGroup->PushAsLimb(m_Pos + RotateOffset(m_pLFGLeg->GetParentOffset()), m_pLFGLeg->GetMaxLength(), m_Vel, m_Rotation, m_Paths[LEFTSIDE][FGROUND][STAND], deltaTime, nullptr, !m_pRFGLeg);
				}

				if (m_pLBGLeg) {
					m_pLBGFootGroup->PushAsLimb(m_Pos + RotateOffset(m_pLBGLeg->GetParentOffset()), m_pLBGLeg->GetMaxLength(), m_Vel, m_Rotation, m_Paths[LEFTSIDE][BGROUND][STAND], deltaTime);
				}

				if (m_pRFGLeg) {
					m_pRFGFootGroup->PushAsLimb(m_Pos + RotateOffset(m_pRFGLeg->GetParentOffset()), m_pRFGLeg->GetMaxLength(), m_Vel, m_Rotation, m_Paths[RIGHTSIDE][FGROUND][STAND], deltaTime, nullptr, !m_pLFGLeg);
				}

				if (m_pRBGLeg) {
					m_pRBGFootGroup->PushAsLimb(m_Pos + RotateOffset(m_pRBGLeg->GetParentOffset()), m_pRBGLeg->GetMaxLength(), m_Vel, m_Rotation, m_Paths[RIGHTSIDE][BGROUND][STAND], deltaTime);
				}
			}
		}
	} else {
		// Not stable/standing, so make sure the end of limbs are moving around limply in a ragdoll fashion.
		// TODO: Make the limb atom groups fly around and react to terrain, without getting stuck etc.
		if (m_pLFGLeg) {
			m_pLFGFootGroup->FlailAsLimb(m_Pos, RotateOffset(m_pLFGLeg->GetParentOffset()), m_pLFGLeg->GetMaxLength(), m_PrevVel * m_pLFGLeg->GetJointStiffness(), m_AngularVel, m_pLFGLeg->GetMass(), deltaTime);
		}

		if (m_pLBGLeg) {
			m_pLBGFootGroup->FlailAsLimb(m_Pos, RotateOffset(m_pLBGLeg->GetParentOffset()), m_pLBGLeg->GetMaxLength(), m_PrevVel * m_pLBGLeg->GetJointStiffness(), m_AngularVel, m_pLBGLeg->GetMass(), deltaTime);
		}

		if (m_pRFGLeg) {
			m_pRFGFootGroup->FlailAsLimb(m_Pos, RotateOffset(m_pRFGLeg->GetParentOffset()), m_pRFGLeg->GetMaxLength(), m_PrevVel * m_pRFGLeg->GetJointStiffness(), m_AngularVel, m_pRFGLeg->GetMass(), deltaTime);
		}

		if (m_pRBGLeg) {
			m_pRBGFootGroup->FlailAsLimb(m_Pos, RotateOffset(m_pRBGLeg->GetParentOffset()), m_pRBGLeg->GetMaxLength(), m_PrevVel * m_pRBGLeg->GetJointStiffness(), m_AngularVel, m_pRBGLeg->GetMass(), deltaTime);
		}
	}
	if (m_MovementState != WALK && m_StrideSound && m_StrideSound->GetLoopSetting() < 0) {
		m_StrideSound->Stop();
	}

	/////////////////////////////////
	// Manage Attachable:s
	if (m_pTurret && m_pTurret->IsAttached()) {
		m_pTurret->SetMountedDeviceRotationOffset((m_AimAngle * GetFlipFactor()) - m_Rotation.GetRadAngle());
	}

	if (m_pLFGLeg && m_pLFGLeg->IsAttached()) {
		m_pLFGLeg->EnableIdle(m_Status != UNSTABLE);
		m_pLFGLeg->SetTargetPosition(m_pLFGFootGroup->GetLimbPos(m_HFlipped));
	}

	if (m_pLBGLeg && m_pLBGLeg->IsAttached()) {
		m_pLBGLeg->EnableIdle(m_Status != UNSTABLE);
		m_pLBGLeg->SetTargetPosition(m_pLBGFootGroup->GetLimbPos(m_HFlipped));
	}

	if (m_pRFGLeg && m_pRFGLeg->IsAttached()) {
		m_pRFGLeg->EnableIdle(m_Status != UNSTABLE);
		m_pRFGLeg->SetTargetPosition(m_pRFGFootGroup->GetLimbPos(m_HFlipped));
	}

	if (m_pRBGLeg && m_pRBGLeg->IsAttached()) {
		m_pRBGLeg->EnableIdle(m_Status != UNSTABLE);
		m_pRBGLeg->SetTargetPosition(m_pRBGFootGroup->GetLimbPos(m_HFlipped));
	}
}

void ACrab::Update() {
	ZoneScoped;

	Actor::Update();

	// Up onto ledges and over low obstacles, as humanoids do (Actor::TryStartMantle).
	if (!m_Mantling) {
		bool rising = m_pJetpack && m_pJetpack->IsEmitting() && m_Vel.m_Y < 0.5F;
		TryStartMantle(nullptr, rising, static_cast<float>(GetSpriteWidth()));
	}
	UpdateMantle();

	////////////////////////////////////
	// Update viewpoint

	// Set viewpoint based on how we are aiming etc.
	Vector aimSight(m_AimDistance, 0);
	Matrix aimMatrix(m_HFlipped ? -m_AimAngle : m_AimAngle);
	aimMatrix.SetXFlipped(m_HFlipped);
	// Reset this each frame
	m_SharpAimMaxedOut = false;

	if (m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		float maxLength = m_pTurret->GetFirstMountedDevice()->GetSharpLength();

		// Use a non-terrain check ray to cap the magnitude, so we can't see into objects etc
		if (m_SharpAimProgress > 0) {
			Vector notUsed;
			Vector sharpAimVector(maxLength, 0);
			sharpAimVector *= aimMatrix;

			// See how far along the sharp aim vector there is opaque air
			//            float result = g_SceneMan.CastNotMaterialRay(m_pLFGLeg->GetFirstMountedDevice()->GetMuzzlePos(), sharpAimVector, g_MaterialAir, 5);
			float result = g_SceneMan.CastObstacleRay(m_pTurret->GetFirstMountedDevice()->GetMuzzlePos(), sharpAimVector, notUsed, notUsed, GetRootID(), IgnoresWhichTeam(), g_MaterialAir, 5);
			// If we didn't find anything but air before the sharpdistance, then don't alter the sharp distance
			if (result >= 0 && result < (maxLength * m_SharpAimProgress)) {
				m_SharpAimProgress = result / maxLength;
				m_SharpAimMaxedOut = true;
			}
		}
		// Indicate maxed outedness if we really are, too
		if (m_SharpAimProgress > 0.9)
			m_SharpAimMaxedOut = true;

		//        sharpDistance *= m_Controller.GetAnalogAim().GetMagnitude();
		aimSight.m_X += maxLength * m_SharpAimProgress;
	}

	// Rotate the aiming spot vector and add it to the view point
	aimSight *= aimMatrix;
	m_ViewPoint = m_Pos.GetFloored() + aimSight;

	// Add velocity also so the viewpoint moves ahead at high speeds
	if (m_Vel.MagnitudeIsGreaterThan(10.0F))
		m_ViewPoint += m_Vel * std::sqrt(m_Vel.GetMagnitude() * 0.1F);

	////////////////////////////////////////
	// Balance stuff

	// Get the rotation in radians.
	float rot = m_Rotation.GetRadAngle();
	//        rot = fabs(rot) < c_QuarterPI ? rot : (rot > 0 ? c_QuarterPI : -c_QuarterPI);
	// Eliminate full rotations
	while (fabs(rot) > c_TwoPI) {
		rot -= rot > 0 ? c_TwoPI : -c_TwoPI;
	}
	// Eliminate rotations over half a turn
	if (fabs(rot) > c_PI) {
		rot = (rot > 0 ? -c_PI : c_PI) + (rot - (rot > 0 ? c_PI : -c_PI));
		// If we're upside down, we're unstable damnit
		if (m_Status == STABLE) {
			m_Status = UNSTABLE;
		}
		m_StableRecoverTimer.Reset();
	}

	// Rotational balancing spring calc
	if (m_Status == STABLE) {
		// Upright body posture
		m_AngularVel = m_AngularVel * 0.9F - (rot * 0.3F);
	}
	// While dying, pull body quickly toward down toward horizontal
	else if (m_Status == DYING) {
		float rotTarget = rot > 0 ? c_HalfPI : -c_HalfPI;
		//        float rotTarget = m_HFlipped ? c_HalfPI : -c_HalfPI;
		float rotDiff = rotTarget - rot;
		if (!m_DeathTmr.IsPastSimMS(125) && fabs(rotDiff) > 0.1 && fabs(rotDiff) < c_PI) {
			m_AngularVel += rotDiff * 0.5; // fabs(rotDiff);
			//            m_Vel.m_X += (m_HFlipped ? -fabs(rotDiff) : fabs(rotDiff)) * 0.35;
			m_Vel.m_X += (rotTarget > 0 ? -fabs(rotDiff) : fabs(rotDiff)) * 0.35;
		} else
			m_Status = DEAD;

		//        else if (fabs(m_AngularVel) > 0.1)
		//            m_AngularVel *= 0.5;
	}
	m_Rotation.SetRadAngle(rot);

	///////////////////////////////////////////////////
	// Death detection and handling

	// Losing all limbs should kill... eventually
	if (!m_pLFGLeg && !m_pLBGLeg && !m_pRFGLeg && !m_pRBGLeg && m_Status != DYING && m_Status != DEAD)
		m_Health -= 0.1;

	/////////////////////////////////////////
	// Misc.

	//    m_DeepCheck = true/*m_Status == DEAD*/;
}

void ACrab::Draw(BITMAP* pTargetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const {
	Actor::Draw(pTargetBitmap, targetPos, mode, onlyPhysical);

	if (mode == g_DrawColor && !onlyPhysical && g_SettingsMan.DrawHandAndFootGroupVisualizations()) {
		m_pLFGFootGroup->Draw(pTargetBitmap, targetPos, true, 13);
		m_pLBGFootGroup->Draw(pTargetBitmap, targetPos, true, 13);
		m_pRFGFootGroup->Draw(pTargetBitmap, targetPos, true, 13);
		m_pRBGFootGroup->Draw(pTargetBitmap, targetPos, true, 13);
	}

	if (mode == g_DrawColor && !onlyPhysical && g_SettingsMan.DrawLimbPathVisualizations()) {
		m_Paths[LEFTSIDE][BGROUND][WALK].Draw(pTargetBitmap, targetPos, 122);
		m_Paths[LEFTSIDE][FGROUND][WALK].Draw(pTargetBitmap, targetPos, 122);
		m_Paths[RIGHTSIDE][BGROUND][WALK].Draw(pTargetBitmap, targetPos, 122);
		m_Paths[RIGHTSIDE][FGROUND][WALK].Draw(pTargetBitmap, targetPos, 122);
	}
}

void ACrab::Draw(const Camera& camera) const {

	Actor::Draw(camera);

	if (g_SettingsMan.DrawHandAndFootGroupVisualizations()) {
		m_pLFGFootGroup->Draw(camera, true, 13);
		m_pLBGFootGroup->Draw(camera, true, 13);
		m_pRFGFootGroup->Draw(camera, true, 13);
		m_pRBGFootGroup->Draw(camera, true, 13);
	}

	if (g_SettingsMan.DrawLimbPathVisualizations()) {
		m_Paths[LEFTSIDE][BGROUND][WALK].Draw(camera, 122);
		m_Paths[LEFTSIDE][FGROUND][WALK].Draw(camera, 122);
		m_Paths[RIGHTSIDE][BGROUND][WALK].Draw(camera, 122);
		m_Paths[RIGHTSIDE][FGROUND][WALK].Draw(camera, 122);
	}
}

void ACrab::DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos, int whichScreen, bool playerControlled) {
	m_HUDStack = -m_CharHeight / 2;

	// Only do HUD if on a team
	if (m_Team < 0)
		return;

	// Only draw if the team viewing this is on the same team OR has seen the space where this is located.
	int viewingTeam = g_ActivityMan.GetActivity()->GetTeamOfPlayer(g_ActivityMan.GetActivity()->PlayerOfScreen(whichScreen));
	if (viewingTeam != m_Team && viewingTeam != Activity::NoTeam && (!g_SettingsMan.ShowEnemyHUD() || g_SceneMan.IsUnseen(m_Pos.GetFloorIntX(), m_Pos.GetFloorIntY(), viewingTeam))) {
		return;
	}

	Actor::DrawHUD(pTargetBitmap, targetPos, whichScreen);

	if (!m_HUDVisible) {
		return;
	}

	// Player AI drawing

	if ((m_Controller.IsState(AIM_SHARP) || (m_Controller.IsPlayerControlled() && !m_Controller.IsState(PIE_MENU_ACTIVE))) && m_pTurret && m_pTurret->IsAttached() && m_pTurret->HasMountedDevice()) {
		m_pTurret->GetFirstMountedDevice()->DrawHUD(pTargetBitmap, targetPos, whichScreen, m_Controller.IsState(AIM_SHARP) && m_Controller.IsPlayerControlled());
	}
	//////////////////////////////////////
	// Draw stat info HUD
	char str[64];

	GUIFont* pSymbolFont = g_FrameMan.GetLargeFont();
	GUIFont* pSmallFont = g_FrameMan.GetSmallFont();

	// Only show extra HUD if this guy is controlled by the same player that this screen belongs to
	if (m_Controller.IsPlayerControlled() && g_ActivityMan.GetActivity()->ScreenOfPlayer(m_Controller.GetPlayer()) == whichScreen && pSmallFont && pSymbolFont) {
		AllegroBitmap allegroBitmap(pTargetBitmap);

		Vector drawPos = m_Pos - targetPos;

		// Adjust the draw position to work if drawn to a target screen bitmap that is straddling a scene seam
		if (!targetPos.IsZero()) {
			// Spans vertical scene seam
			int sceneWidth = g_SceneMan.GetSceneWidth();
			if (g_SceneMan.SceneWrapsX() && pTargetBitmap->w < sceneWidth) {
				if ((targetPos.m_X < 0) && (m_Pos.m_X > (sceneWidth - pTargetBitmap->w)))
					drawPos.m_X -= sceneWidth;
				else if (((targetPos.m_X + pTargetBitmap->w) > sceneWidth) && (m_Pos.m_X < pTargetBitmap->w))
					drawPos.m_X += sceneWidth;
			}
			// Spans horizontal scene seam
			int sceneHeight = g_SceneMan.GetSceneHeight();
			if (g_SceneMan.SceneWrapsY() && pTargetBitmap->h < sceneHeight) {
				if ((targetPos.m_Y < 0) && (m_Pos.m_Y > (sceneHeight - pTargetBitmap->h)))
					drawPos.m_Y -= sceneHeight;
				else if (((targetPos.m_Y + pTargetBitmap->h) > sceneHeight) && (m_Pos.m_Y < pTargetBitmap->h))
					drawPos.m_Y += sceneHeight;
			}
		}

		// Held-related GUI stuff
		if (m_pTurret) {
			std::string textString;
			for (const HeldDevice* mountedDevice: m_pTurret->GetMountedDevices()) {
				if (const HDFirearm* mountedFirearm = dynamic_cast<const HDFirearm*>(mountedDevice)) {
					if (!textString.empty()) {
						textString += " | ";
					}
					int totalTextWidth = pSmallFont->CalculateWidth(textString);
					if (mountedFirearm->IsReloading()) {
						textString += "Reloading";
						rectfill(pTargetBitmap, drawPos.GetFloorIntX() + 1 + totalTextWidth, drawPos.GetFloorIntY() + m_HUDStack + 13, drawPos.GetFloorIntX() + 29 + totalTextWidth, drawPos.GetFloorIntY() + m_HUDStack + 14, 245);
						rectfill(pTargetBitmap, drawPos.GetFloorIntX() + totalTextWidth, drawPos.GetFloorIntY() + m_HUDStack + 12, drawPos.GetFloorIntX() + static_cast<int>(28.0F * mountedFirearm->GetReloadProgress() + 0.5F) + totalTextWidth, drawPos.GetFloorIntY() + m_HUDStack + 13, 77);
					} else {
						textString += mountedFirearm->GetRoundInMagCount() < 0 ? "Infinite" : std::to_string(mountedFirearm->GetRoundInMagCount());
					}
				}
			}
			if (!textString.empty()) {
				str[0] = -56;
				str[1] = 0;
				pSymbolFont->DrawAligned(&allegroBitmap, drawPos.GetFloorIntX() - 10, drawPos.GetFloorIntY() + m_HUDStack, str, GUIFont::Left);
				pSmallFont->DrawAligned(&allegroBitmap, drawPos.GetFloorIntX() - 0, drawPos.GetFloorIntY() + m_HUDStack + 3, textString, GUIFont::Left);
				m_HUDStack -= 9;
			}
		} else {
			std::snprintf(str, sizeof(str), "NO TURRET!");
			pSmallFont->DrawAligned(&allegroBitmap, drawPos.m_X + 2, drawPos.m_Y + m_HUDStack + 3, str, GUIFont::Centre);
			m_HUDStack += -9;
		}

		if (m_pJetpack && m_Status != INACTIVE && !m_Controller.IsState(PIE_MENU_ACTIVE) && (m_Controller.IsState(BODY_JUMP) || !m_pJetpack->IsFullyFueled())) {
			if (m_pJetpack->GetJetTimeLeft() < 100.0F) {
				str[0] = m_IconBlinkTimer.AlternateSim(100) ? -26 : -25;
			} else if (m_pJetpack->IsEmitting()) {
				float acceleration = m_pJetpack->EstimateImpulse(false) / std::max(GetMass(), 0.1F);
				if (acceleration > 0.41F) {
					str[0] = acceleration > 0.47F ? -31 : -30;
				} else {
					str[0] = acceleration > 0.35F ? -29 : -28;
					if (m_IconBlinkTimer.AlternateSim(200)) {
						str[0] = -27;
					}
				}
			} else {
				str[0] = -27;
			}
			str[1] = 0;
			pSymbolFont->DrawAligned(&allegroBitmap, drawPos.GetFloorIntX() - 7, drawPos.GetFloorIntY() + m_HUDStack, str, GUIFont::Centre);

			rectfill(pTargetBitmap, drawPos.GetFloorIntX() + 1, drawPos.GetFloorIntY() + m_HUDStack + 7, drawPos.GetFloorIntX() + 15, drawPos.GetFloorIntY() + m_HUDStack + 8, 245);
			if (m_pJetpack->GetJetTimeTotal() > 0.0F) {
				float jetTimeRatio = m_pJetpack->GetJetTimeRatio();
				int gaugeColor;
				if (jetTimeRatio > 0.75F) {
					gaugeColor = 149;
				} else if (jetTimeRatio > 0.5F) {
					gaugeColor = 133;
				} else if (jetTimeRatio > 0.375F) {
					gaugeColor = 77;
				} else if (jetTimeRatio > 0.25F) {
					gaugeColor = 48;
				} else {
					gaugeColor = 13;
				}
				rectfill(pTargetBitmap, drawPos.GetFloorIntX(), drawPos.GetFloorIntY() + m_HUDStack + 6, drawPos.GetFloorIntX() + static_cast<int>(15.0F * jetTimeRatio), drawPos.GetFloorIntY() + m_HUDStack + 7, gaugeColor);
			}
			m_HUDStack -= 9;
		}
	}
}

void ACrab::DrawHUD(const Camera& camera) {}

float ACrab::GetLimbPathTravelSpeed(MovementState movementState) {
	return m_Paths[LEFTSIDE][FGROUND][movementState].GetTravelSpeed();
}

void ACrab::SetLimbPathTravelSpeed(MovementState movementState, float newSpeed) {
	m_Paths[LEFTSIDE][FGROUND][movementState].SetTravelSpeed(newSpeed);
	m_Paths[RIGHTSIDE][FGROUND][movementState].SetTravelSpeed(newSpeed);

	m_Paths[LEFTSIDE][BGROUND][movementState].SetTravelSpeed(newSpeed);
	m_Paths[RIGHTSIDE][BGROUND][movementState].SetTravelSpeed(newSpeed);
}

float ACrab::GetLimbPathPushForce(MovementState movementState) {
	return m_Paths[LEFTSIDE][FGROUND][movementState].GetPushForce();
}

void ACrab::SetLimbPathPushForce(MovementState movementState, float newForce) {
	m_Paths[LEFTSIDE][FGROUND][movementState].SetPushForce(newForce);
	m_Paths[RIGHTSIDE][FGROUND][movementState].SetPushForce(newForce);

	m_Paths[LEFTSIDE][BGROUND][movementState].SetPushForce(newForce);
	m_Paths[RIGHTSIDE][BGROUND][movementState].SetPushForce(newForce);
}

int ACrab::WhilePieMenuOpenListener(const PieMenu* pieMenu) {
	int result = Actor::WhilePieMenuOpenListener(pieMenu);

	for (PieSlice* pieSlice: GetPieMenu()->GetPieSlices()) {
		if (pieSlice->GetType() == PieSliceType::Reload) {
			if (m_pTurret && m_pTurret->HasMountedDevice()) {
				pieSlice->SetDescription("Reload");
				pieSlice->SetEnabled(!FirearmsAreFull());
			} else {
				pieSlice->SetDescription(m_pTurret ? "No Weapons" : "No Turret");
				pieSlice->SetEnabled(false);
			}
			break;
		}
	}
	return result;
}

// ---------------------------------------------------------------- The crab's route-follower

namespace {
	RTE::Vector CrabTowards(const RTE::Vector& from, const RTE::Vector& to) {
		return RTE::g_SceneMan.ShortestDistance(from, to, RTE::g_SceneMan.SceneWrapsX());
	}

	// The floor under a point within so far: its y, or below zero for none.
	float CrabFloorUnder(const RTE::Vector& point, float reach) {
		RTE::Vector hit;
		if (RTE::g_SceneMan.CastStrengthRay(point, RTE::Vector(0.0F, reach), 5.0F, hit, 2)) {
			return hit.m_Y;
		}
		return -1.0F;
	}
} // namespace

void ACrab::ResetRouteMovement() {
	m_CrabMover = CrabMover();
}

int ACrab::MoveAlongRoute() {
	CrabMover& mover = m_CrabMover;
	// Not called for a while (held by its script): the timers start again (see AHuman::MoveAlongRoute).
	const long long tick = g_TimerMan.GetSimUpdateCount();
	if (mover.lastCallTick >= 0 && tick - mover.lastCallTick > static_cast<long long>(std::max(1, g_SettingsMan.GetAIUpdateInterval()) * 2 + 1)) {
		ResetRouteMovement();
	}
	mover.lastCallTick = tick;
	if (!mover.begun) {
		mover.begun = true;
		mover.progressTimer.Reset();
	}
	const float h = std::max(m_CharHeight, 30.0F);
	const float ppm = c_PPM;
	const float gravity = g_SceneMan.GetGlobalAcc().m_Y * ppm;
	Controller& ctrl = m_Controller;
	const bool standardJet = m_pJetpack && m_pJetpack->IsAttached() && m_pJetpack->GetJetpackType() == AEJetpack::JetpackType::Standard;

	// Nothing to go to.
	if (m_Waypoints.empty() && m_MovePath.empty() && !m_HasMovePathGoal && !g_MovableMan.ValidMO(m_pMOMoveTarget)) {
		return 1;
	}
	if (m_Status != STABLE) {
		mover.progressTimer.Reset();
		return 0;
	}

	// The route: asked for when there is none, and now and then anyway.
	auto refresh = [&]() {
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
		mover.repathTimer.Reset();
		mover.noSightTimer.Reset();
		mover.bestGap = -1.0F;
		mover.progressTimer.Reset();
	};
	if (m_MovePath.empty() && !IsWaitingOnNewMovePath()) {
		if (m_ImpossiblePaths > 0 && mover.impossibleAnswers >= 3) {
			return 2;
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
	if (IsWaitingOnNewMovePath() || m_MovePath.empty()) {
		return 0;
	}
	mover.impossibleAnswers = 0;
	mover.impossibleSeen = m_ImpossiblePaths;

	// On the ground: floor under its middle or either side of its body (a crab is wide).
	float floorHere = CrabFloorUnder(m_Pos, h * 0.9F);
	for (float side: {-h * 0.3F, h * 0.3F}) {
		if (floorHere < 0.0F) {
			floorHere = CrabFloorUnder(m_Pos + Vector(side, 0.0F), h * 0.9F);
		}
	}
	const bool airborne = floorHere < 0.0F;

	// A leg done with more waypoints queued: on to the next (see AHuman::MoveAlongRoute).
	if (m_MovePath.size() <= 1 && !m_Waypoints.empty() && m_HasMovePathGoal && !g_MovableMan.ValidMO(m_pMOMoveTarget) && !airborne) {
		if (CrabTowards(m_Pos, m_MovePathGoal).MagnitudeIsLessThan(std::max(m_MoveProximityLimit * 1.5F, h * 0.4F))) {
			m_MovePath.clear();
			m_MovePathKinds.clear();
			m_HasMovePathGoal = false;
			mover.bestGap = -1.0F;
			mover.progressTimer.Reset();
			return 0;
		}
	}
	// Arrived: the last point, the goal within reach, standing.
	if (m_MovePath.size() <= 1 && m_Waypoints.empty() && !g_MovableMan.ValidMO(m_pMOMoveTarget)) {
		Vector goal = GetLastAIWaypoint();
		if (CrabTowards(m_Pos, goal).MagnitudeIsLessThan(std::max(m_MoveProximityLimit * 1.5F, h * 0.4F)) && !airborne && m_Vel.MagnitudeIsLessThan(2.0F)) {
			return 1;
		}
	}

	// Points passed are dropped: within reach, or behind with the next in plain sight (terrain only).
	{
		float tolerance = m_MoveProximityLimit * (airborne ? 2.0F : 1.0F);
		for (int guard = 0; guard < 6 && m_MovePath.size() > 1; ++guard) {
			Vector toPoint = CrabTowards(m_Pos, m_MovePath.front());
			Vector toNext = CrabTowards(m_Pos, *std::next(m_MovePath.begin()));
			Vector hit;
			bool passed = toPoint.MagnitudeIsLessThan(tolerance) || (toNext.MagnitudeIsLessThan(toPoint.GetMagnitude()) && !g_SceneMan.CastStrengthRay(m_Pos, toNext, 5.0F, hit, 4, MaterialColorKeys::g_MaterialDoor));
			if (!passed) {
				break;
			}
			m_PrevPathTarget = m_MovePath.front();
			m_MovePath.pop_front();
			if (!m_MovePathKinds.empty()) {
				m_MovePathKinds.pop_front();
			}
		}
	}

	// Progress, and being stuck: a hop at 2.5 s, a new route round the place at 6.
	{
		float gap = CrabTowards(m_Pos, m_MovePath.front()).GetMagnitude();
		if (mover.bestGap < 0.0F || gap < mover.bestGap - 4.0F) {
			mover.bestGap = gap;
			mover.progressTimer.Reset();
		}
	}
	const bool stuck = mover.progressTimer.IsPastSimMS(2500);
	if (mover.progressTimer.IsPastSimMS(6000)) {
		AvoidPathPoint(m_MovePath.front(), 20000.0F);
		refresh();
		return 0;
	}
	// A fresh route now and then (the world changes), or when the next point has been out of sight on the ground for a second.
	if (!airborne) {
		Vector hit;
		bool inSight = !g_SceneMan.CastStrengthRay(m_Pos, CrabTowards(m_Pos, m_MovePath.front()), 5.0F, hit, 4, MaterialColorKeys::g_MaterialDoor);
		if (inSight) {
			mover.noSightTimer.Reset();
		}
		if (mover.noSightTimer.IsPastSimMS(1000) || mover.repathTimer.IsPastSimMS(7500)) {
			refresh();
			return 0;
		}
	}

	const Vector point = m_MovePath.front();
	const PathStepKind kind = m_MovePathKinds.empty() ? PathStepKind::Walk : m_MovePathKinds.front();
	const Vector toPoint = CrabTowards(m_Pos, point);
	const float above = -toPoint.m_Y;
	// The jet's push now, against gravity: what a climb can hold and a brake can stop.
	const float push = (standardJet && GetMass() > 0.0F && g_TimerMan.GetDeltaTimeSecs() > 0.0F) ? m_pJetpack->EstimateImpulse(false) / GetMass() / g_TimerMan.GetDeltaTimeSecs() * ppm : 0.0F;
	const float netUp = push - gravity;
	// The stick for the jet: up, leant by up to the nozzle's full tilt (0.27 across is about fourteen degrees), in screen terms.
	auto jetWith = [&](float lean) {
		ctrl.SetState(BODY_JUMP, true);
		ctrl.SetAnalogMove(Vector(std::clamp(lean, -1.0F, 1.0F) * 0.27F, -1.0F));
	};

	// ---- In the air: a climb for a point above, held to a rate the coast just reaches it at; else the fall braked for its floor. ----
	if (airborne) {
		if (standardJet && m_pJetpack->GetJetTimeLeft() > 0.0F) {
			float lean = std::clamp(toPoint.m_X / h, -1.0F, 1.0F) - std::clamp(m_Vel.m_X * 0.2F, -0.5F, 0.5F);
			if (above > h * 0.1F) {
				float rate = std::min(8.0F, std::sqrt(2.0F * gravity * std::max(0.0F, above - 6.0F)) / ppm);
				if (m_Vel.m_Y > -rate) {
					jetWith(lean);
				}
			} else if (m_Vel.m_Y > 2.0F && netUp > 1.0F) {
				float speed = m_Vel.m_Y * ppm;
				float stop = speed * speed / (2.0F * netUp);
				if (CrabFloorUnder(m_Pos, stop * 1.3F + h * 0.6F) >= 0.0F) {
					jetWith(std::clamp(-m_Vel.m_X * 0.3F, -1.0F, 1.0F));
				}
			}
		}
		if (std::abs(toPoint.m_X) > 3.0F) {
			ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
		}
		return 0;
	}

	// ---- On the ground. ----
	// A mantle: walked into the ledge, which pulls the crab up onto it (Actor::TryStartMantle); no jet.
	if (kind == PathStepKind::Mantle && std::abs(toPoint.m_X) > 3.0F) {
		if (!IsMantling()) {
			ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
		} else {
			mover.progressTimer.Reset();
		}
		return 0;
	}
	// A climb the legs don't take (the route's jump, or the point well above): off from a stand under the way up, with fuel for it.
	if (standardJet && netUp > 1.0F && (kind == PathStepKind::Jump || above > h * 0.45F) && std::abs(toPoint.m_X) < h * 1.5F && above > h * 0.2F) {
		if (std::abs(m_Vel.m_X) > 0.6F) {
			// (Steadied first: the walk's speed into a climb is a push the wrong way, and a crab's jet can barely lean against it.)
			mover.progressTimer.Reset();
			return 0;
		}
		float seconds = above / std::max(1.0F, std::min(8.0F * ppm, std::sqrt(netUp * above)));
		float needed = std::min(m_pJetpack->GetJetTimeTotal() * 0.85F, seconds * 1000.0F * 1.3F + 300.0F);
		if (m_pJetpack->GetJetTimeLeft() < needed) {
			mover.progressTimer.Reset();
			return 0;
		}
		// The burst once, at the start of the climb: the body stays "on the ground" until it is most of a height clear of the floor, and a
		// burst sent every tick until then (a base jetpack has no spacing between bursts) emptied the tank under the lip. Again only if a
		// second on it is still standing here, the climb having come to nothing.
		if (!mover.climbing || mover.climbTimer.IsPastSimMS(1000)) {
			ctrl.SetState(BODY_JUMPSTART, true);
			mover.climbing = true;
			mover.climbTimer.Reset();
		}
		jetWith(std::clamp(toPoint.m_X / h, -1.0F, 1.0F));
		mover.progressTimer.Reset();
		return 0;
	}
	mover.climbing = false;
	// The walk.
	if (std::abs(toPoint.m_X) > 3.0F) {
		ctrl.SetState(toPoint.m_X < 0.0F ? MOVE_LEFT : MOVE_RIGHT, true);
	}
	// Stuck on something the legs don't take: a hop, now and then.
	if (stuck && standardJet && m_pJetpack->GetJetTimeLeft() > 300.0F) {
		if (mover.hopTimer.IsPastSimMS(1200)) {
			mover.hopTimer.Reset();
		}
		if (!mover.hopTimer.IsPastSimMS(350)) {
			jetWith(std::clamp(toPoint.m_X / h, -1.0F, 1.0F));
		}
	}
	return 0;
}
