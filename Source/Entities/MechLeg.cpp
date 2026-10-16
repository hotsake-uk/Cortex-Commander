#include "MechLeg.h"
#include "PresetMan.h"

using namespace RTE;

ConcreteClassInfo(MechLeg, Attachable, 20);

MechLeg::MechLeg() {
	Clear();
}

MechLeg::~MechLeg() {
	Destroy(true);
}

void MechLeg::Clear() {
	m_Shin = nullptr;
	m_Foot = nullptr;
	m_KneeOffset.SetXY(0.0F, 20.0F);
	m_AnkleOffset.SetXY(0.0F, 20.0F);
	m_FootDrop = 0.0F;
	m_AnkleTarget.Reset();
	m_HasTarget = false;
	m_FootAngle = 0.0F;
	m_KneePos.Reset();
	m_AnklePos.Reset();
}

int MechLeg::Create() {
	if (Attachable::Create() < 0) {
		return -1;
	}
	// The bones are turned by the leg itself (as a Leg is), and don't drag on the ground as they swing: the feet are put down by the owner.
	m_InheritsRotAngle = false;
	SetCollidesWithTerrainWhileAttached(false);
	HangBones();
	return 0;
}

int MechLeg::Create(const MechLeg& reference) {
	if (reference.m_Shin) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_Shin->GetUniqueID());
		SetShin(dynamic_cast<Attachable*>(reference.m_Shin->Clone()));
	}
	Attachable::Create(reference);

	m_KneeOffset = reference.m_KneeOffset;
	m_AnkleOffset = reference.m_AnkleOffset;
	m_FootDrop = reference.m_FootDrop;
	m_AnkleTarget = reference.m_AnkleTarget;
	m_HasTarget = reference.m_HasTarget;
	m_FootAngle = reference.m_FootAngle;
	m_KneePos = reference.m_KneePos;
	m_AnklePos = reference.m_AnklePos;

	HangBones();
	return 0;
}

void MechLeg::HangBones() {
	if (!m_Shin) {
		return;
	}
	m_Shin->SetParentOffset(m_JointOffset + m_KneeOffset);
	// (The foot is the shin's own attachable, cloned or read with it: found again here.)
	if (!m_Foot && !m_Shin->GetAttachableList().empty()) {
		m_Foot = m_Shin->GetAttachableList().front();
	}
	if (m_Foot) {
		m_Foot->SetInheritsRotAngle(false);
		m_Foot->SetParentOffset(m_Shin->GetJointOffset() + m_AnkleOffset);
	}
}

int MechLeg::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Attachable::ReadProperty(propName, reader));

	MatchProperty("Shin", { SetShin(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("Foot", { SetFoot(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("KneeOffset", { reader >> m_KneeOffset; });
	MatchProperty("AnkleOffset", { reader >> m_AnkleOffset; });
	MatchProperty("FootDrop", { reader >> m_FootDrop; });

	EndPropertyList;
}

int MechLeg::Save(Writer& writer) const {
	Attachable::Save(writer);

	writer.NewProperty("Shin");
	writer << m_Shin;
	writer.NewPropertyWithValue("KneeOffset", m_KneeOffset);
	writer.NewPropertyWithValue("AnkleOffset", m_AnkleOffset);
	writer.NewPropertyWithValue("FootDrop", m_FootDrop);

	return 0;
}

void MechLeg::SetShin(Attachable* newShin) {
	if (m_Shin && m_Shin->IsAttached()) {
		RemoveAndDeleteAttachable(m_Shin);
		m_Foot = nullptr;
	}
	m_Shin = newShin;
	if (!m_Shin) {
		return;
	}
	AddAttachable(m_Shin);
	m_HardcodedAttachableUniqueIDsAndSetters.insert({m_Shin->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                 dynamic_cast<MechLeg*>(parent)->SetShin(attachable);
	                                                 }});
	if (m_Shin->HasNoSetDamageMultiplier()) {
		m_Shin->SetDamageMultiplier(1.0F);
	}
	m_Shin->SetInheritsRotAngle(false);
	m_Shin->SetCollidesWithTerrainWhileAttached(false);
	HangBones();
}

void MechLeg::SetFoot(Attachable* newFoot) {
	if (!m_Shin) {
		delete newFoot;
		return;
	}
	if (m_Foot && m_Foot->IsAttached()) {
		m_Shin->RemoveAndDeleteAttachable(m_Foot);
	}
	m_Foot = newFoot;
	if (!m_Foot) {
		return;
	}
	m_Shin->AddAttachable(m_Foot);
	if (m_Foot->HasNoSetDamageMultiplier()) {
		m_Foot->SetDamageMultiplier(1.0F);
	}
	m_Foot->SetInheritsRotAngle(false);
	m_Foot->SetParentGibBlastStrengthMultiplier(0.0F);
	m_Foot->SetCollidesWithTerrainWhileAttached(false);
	HangBones();
}

Vector MechLeg::GetHipPos() const {
	return m_Parent ? m_Parent->GetPos() + m_Parent->RotateOffset(m_ParentOffset) : m_Pos + RotateOffset(m_JointOffset);
}

void MechLeg::SolveBones() {
	const Vector hip = GetHipPos();
	const float flip = m_HFlipped ? -1.0F : 1.0F;
	const float thigh = std::max(GetThighLength(), 1.0F);
	const float shin = std::max(GetShinLength(), 1.0F);

	// Off its owner, or not told where to go: hanging straight down, the knee a little forward.
	Vector toTarget = m_HasTarget && m_Parent ? g_SceneMan.ShortestDistance(hip, m_AnkleTarget, g_SceneMan.SceneWrapsX()) : Vector(flip * (thigh + shin) * 0.15F, (thigh + shin) * 0.95F);
	float reach = toTarget.GetMagnitude();
	const float minReach = std::abs(thigh - shin) + 1.0F;
	const float maxReach = thigh + shin - 0.01F;
	if (reach < 0.001F) {
		toTarget.SetXY(0.0F, 1.0F);
		reach = 1.0F;
	}
	Vector direction = toTarget / reach;
	reach = std::clamp(reach, minReach, maxReach);

	// The angle at the hip between the line to the ankle and the thigh (the law of cosines), the knee bent to the front.
	float cosHip = std::clamp((thigh * thigh + reach * reach - shin * shin) / (2.0F * thigh * reach), -1.0F, 1.0F);
	float hipAngle = std::acos(cosHip);
	Vector kneeA = direction.GetRadRotatedCopy(hipAngle) * thigh;
	Vector kneeB = direction.GetRadRotatedCopy(-hipAngle) * thigh;
	Vector knee = kneeA.m_X * flip >= kneeB.m_X * flip ? kneeA : kneeB;
	m_KneePos = hip + knee;
	m_AnklePos = hip + direction * reach;

	// Each bone turned so that its picture's own line (hip to knee, knee to ankle) lies along the one worked out.
	auto turnTo = [this](const Vector& want, const Vector& own) {
		return want.GetAbsRadAngle() - own.GetXFlipped(m_HFlipped).GetAbsRadAngle();
	};
	m_Rotation.SetRadAngle(turnTo(knee, m_KneeOffset));
	m_AngularVel = 0.0F;
	if (m_Shin) {
		m_Shin->SetRotAngle(turnTo(m_AnklePos - m_KneePos, m_AnkleOffset));
		m_Shin->SetAngularVel(0.0F);
	}
	if (m_Foot) {
		m_Foot->SetRotAngle(m_Parent ? m_FootAngle : m_Rotation.GetRadAngle());
		m_Foot->SetAngularVel(0.0F);
	}
}

void MechLeg::Update() {
	Attachable::PreUpdate();
	if (IsAttached()) {
		SolveBones();
	}
	Attachable::Update();
}
