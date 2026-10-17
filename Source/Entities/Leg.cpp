#include "Leg.h"
#include "PresetMan.h"
#include "AtomGroup.h"
#include "Actor.h"
#include "Draw.h"
#include "SceneMan.h"
#include "TimerMan.h"
#include "UnitSprings.h"

#include <cmath>

using namespace RTE;

ConcreteClassInfo(Leg, Attachable, 50);

Leg::Leg() {
	Clear();
}

Leg::~Leg() {
	Destroy(true);
}

void Leg::Clear() {
	m_Foot = nullptr;

	m_ContractedOffset.Reset();
	m_ExtendedOffset.Reset();

	m_MinExtension = 0;
	m_MaxExtension = 0;
	m_NormalizedExtension = 0;

	m_TargetPosition.Reset();
	m_IdleOffset.Reset();

	m_AnkleOffset.Reset();

	m_WillIdle = false;
	m_MoveSpeed = 0;

	m_ShinSpriteFile.Reset();
	m_ShinSpriteTexture = nullptr;
	m_KneeOffset.Reset();
	m_ShinJointOffset.Reset();
	m_ShinAnkleOffset.Reset();

	m_FootNudge.Reset();
	m_FootTilt = 0.0F;
	m_Flop = 0.0F;
	m_FlopSpeed = 0.0F;
	m_FlopLastVel.Reset();
}

int Leg::Create() {
	if (Attachable::Create() < 0) {
		return -1;
	}

	// Ensure Legs don't get flagged as inheriting RotAngle, since they never do and always set their RotAngle for themselves.
	m_InheritsRotAngle = false;

	// Ensure Legs don't collide with terrain when attached since their expansion/contraction is frame based so atom group doesn't know how to account for it.
	SetCollidesWithTerrainWhileAttached(false);

	if (m_ContractedOffset.GetSqrMagnitude() > m_ExtendedOffset.GetSqrMagnitude()) {
		std::swap(m_ContractedOffset, m_ExtendedOffset);
	}

	m_MinExtension = m_ContractedOffset.GetMagnitude();
	m_MaxExtension = m_ExtendedOffset.GetMagnitude();

	return 0;
}

int Leg::Create(const Leg& reference) {
	if (reference.m_Foot) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_Foot->GetUniqueID());
		SetFoot(dynamic_cast<Attachable*>(reference.m_Foot->Clone()));
	}
	Attachable::Create(reference);

	m_ContractedOffset = reference.m_ContractedOffset;
	m_ExtendedOffset = reference.m_ExtendedOffset;

	m_MinExtension = reference.m_MinExtension;
	m_MaxExtension = reference.m_MaxExtension;
	m_NormalizedExtension = reference.m_NormalizedExtension;

	m_TargetPosition = reference.m_TargetPosition;
	m_IdleOffset = reference.m_IdleOffset;

	m_AnkleOffset = reference.m_AnkleOffset;

	m_WillIdle = reference.m_WillIdle;
	m_MoveSpeed = reference.m_MoveSpeed;

	m_ShinSpriteFile = reference.m_ShinSpriteFile;
	m_ShinSpriteTexture = reference.m_ShinSpriteTexture;
	m_KneeOffset = reference.m_KneeOffset;
	m_ShinJointOffset = reference.m_ShinJointOffset;
	m_ShinAnkleOffset = reference.m_ShinAnkleOffset;

	return 0;
}

int Leg::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Attachable::ReadProperty(propName, reader));

	MatchProperty("Foot", { SetFoot(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("ContractedOffset", {
		reader >> m_ContractedOffset;
		m_MinExtension = m_ContractedOffset.GetMagnitude();
	});
	MatchProperty("ExtendedOffset", {
		reader >> m_ExtendedOffset;
		m_MaxExtension = m_ExtendedOffset.GetMagnitude();
	});
	MatchProperty("IdleOffset", { reader >> m_IdleOffset; });
	MatchProperty("WillIdle", { reader >> m_WillIdle; });
	MatchProperty("MoveSpeed", { reader >> m_MoveSpeed; });
	// Two pictures, opt in: the leg's own sprite is then the thigh, and this the shin hung from its knee (see HasShin).
	MatchProperty("ShinSprite", {
		reader >> m_ShinSpriteFile;
		m_ShinSpriteTexture = m_ShinSpriteFile.GetAsTexture();
	});
	MatchProperty("KneeOffset", { reader >> m_KneeOffset; });
	MatchProperty("ShinJointOffset", { reader >> m_ShinJointOffset; });
	MatchProperty("ShinAnkleOffset", { reader >> m_ShinAnkleOffset; });

	EndPropertyList;
}

int Leg::Save(Writer& writer) const {
	Attachable::Save(writer);

	writer.NewProperty("Foot");
	writer << m_Foot;
	writer.NewProperty("ContractedOffset");
	writer << m_ContractedOffset;
	writer.NewProperty("ExtendedOffset");
	writer << m_ExtendedOffset;
	writer.NewProperty("IdleOffset");
	writer << m_IdleOffset;
	writer.NewProperty("WillIdle");
	writer << m_WillIdle;
	writer.NewProperty("MoveSpeed");
	writer << m_MoveSpeed;
	if (m_ShinSpriteTexture) {
		writer.NewPropertyWithValue("ShinSprite", m_ShinSpriteFile);
		writer.NewPropertyWithValue("KneeOffset", m_KneeOffset);
		writer.NewPropertyWithValue("ShinJointOffset", m_ShinJointOffset);
		writer.NewPropertyWithValue("ShinAnkleOffset", m_ShinAnkleOffset);
	}

	return 0;
}

void Leg::SetFoot(Attachable* newFoot) {
	if (m_Foot && m_Foot->IsAttached()) {
		RemoveAndDeleteAttachable(m_Foot);
	}
	if (newFoot == nullptr) {
		m_Foot = nullptr;
	} else {
		m_Foot = newFoot;
		AddAttachable(newFoot);

		m_HardcodedAttachableUniqueIDsAndSetters.insert({newFoot->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                 dynamic_cast<Leg*>(parent)->SetFoot(attachable);
		                                                 }});

		if (m_Foot->HasNoSetDamageMultiplier()) {
			m_Foot->SetDamageMultiplier(1.0F);
		}
		m_Foot->SetInheritsRotAngle(false);
		// (Set on the ground by itself, UpdateFootOnGround, not moved with the body's springs as the hip is.)
		m_Foot->SetInheritsDrawNudge(false);
		m_Foot->SetParentGibBlastStrengthMultiplier(0.0F);
		m_Foot->SetCollidesWithTerrainWhileAttached(false);
	}
}

AtomGroup* Leg::GetFootGroupFromFootAtomGroup() {
	if (!m_Foot) {
		return nullptr;
	}

	AtomGroup* footGroup = dynamic_cast<AtomGroup*>(m_Foot->GetAtomGroup()->Clone());

	int atomLeftMostOffset = m_Foot->GetSpriteWidth();
	int atomTopMostOffset = m_Foot->GetSpriteHeight();
	int atomBottomMostOffset = 0;

	for (const Atom* atom: footGroup->GetAtomList()) {
		int atomOffsetX = atom->GetOriginalOffset().GetFloorIntX();
		int atomOffsetY = atom->GetOriginalOffset().GetFloorIntY();
		// Auto-generated AtomGroups are created empty so a single Atom is added at 0,0. Ignore it and any others to not screw up detecting the left-most and top-most offsets.
		if (atomOffsetX != 0 && atomOffsetY != 0) {
			atomLeftMostOffset = std::min(atomLeftMostOffset, atomOffsetX);
			atomTopMostOffset = std::min(atomTopMostOffset, atomOffsetY);
			atomBottomMostOffset = std::max(atomBottomMostOffset, atomOffsetY);
		}
	}
	int groupCenterOffsetY = (atomTopMostOffset + atomBottomMostOffset) / 2;

	std::vector<Atom*> filteredAtomList;

	// We want the FootGroup to end up with an "L" shape, so filter all the top and right Atoms while taking into account the heel might be slant and the sole might not be flat. The extra Atoms are not necessary and might (further) screw up some walking physics.
	// Start from the bottom so we can filter any Atom that might be above the bottom-most one on the same X offset.
	for (auto atomItr = footGroup->GetAtomList().crbegin(); atomItr != footGroup->GetAtomList().crend(); ++atomItr) {
		int atomOffsetX = (*atomItr)->GetOriginalOffset().GetFloorIntX();
		int atomOffsetY = (*atomItr)->GetOriginalOffset().GetFloorIntY();

		bool haveBottomMostAtomOnThisXOffset = false;
		for (const Atom* filteredAtom: filteredAtomList) {
			haveBottomMostAtomOnThisXOffset = (filteredAtom->GetOriginalOffset().GetFloorIntX() == atomOffsetX) && (filteredAtom->GetOriginalOffset().GetFloorIntY() > atomOffsetY);
			if (haveBottomMostAtomOnThisXOffset) {
				break;
			}
		}

		if (atomOffsetX == atomLeftMostOffset || atomOffsetY == atomBottomMostOffset || (!haveBottomMostAtomOnThisXOffset && atomOffsetX > atomLeftMostOffset && atomOffsetY >= groupCenterOffsetY)) {
			Atom* newAtom = new Atom(*(*atomItr));
			newAtom->SetMaterial(g_SceneMan.GetMaterialFromID(MaterialColorKeys::g_MaterialRubber));

			filteredAtomList.emplace_back(newAtom);
		}
	}
	footGroup->SetAtomList(filteredAtomList);
	footGroup->SetJointOffset(m_Foot->GetJointOffset());

	return footGroup;
}

void Leg::Update() {
	Attachable::PreUpdate();

	UpdateCurrentAnkleOffset();

	UpdateLegRotation();

	if (m_Foot) {
		// In order to keep the foot in the right place, we need to convert its offset (the ankle offset) to work as the ParentOffset for the foot.
		// The foot will then use this to set its JointPos when it's updated. Unfortunately UnRotateOffset doesn't work for this, since it's Vector/Matrix division, which isn't commutative.
		Vector ankleOffsetAsParentOffset = RotateOffset(m_JointOffset) + m_AnkleOffset;
		ankleOffsetAsParentOffset.RadRotate(-m_Rotation.GetRadAngle()).FlipX(m_HFlipped);
		m_Foot->SetParentOffset(ankleOffsetAsParentOffset);
	}

	Attachable::Update();

	if (m_FrameCount != 1) {
		m_NormalizedExtension = std::clamp((m_AnkleOffset.GetMagnitude() - m_MinExtension) / (m_MaxExtension - m_MinExtension), 0.0F, 1.0F);
		m_Frame = std::min(m_FrameCount - 1, static_cast<unsigned int>(std::floor(m_NormalizedExtension * static_cast<float>(m_FrameCount))));
	}

	UpdateFootFrameAndRotation();

	if (IsAttached()) {
		UpdateFootOnGround();
	} else {
		UpdateFlop();
	}
}

void Leg::UpdateCurrentAnkleOffset() {
	if (IsAttached()) {
		Vector targetOffset = g_SceneMan.ShortestDistance(m_JointPos, m_TargetPosition, g_SceneMan.SceneWrapsX());
		Vector rotatedTargetOffset = targetOffset.GetRadRotatedCopy(m_Parent->GetRotAngle());
		if (m_WillIdle && rotatedTargetOffset.m_Y < -std::abs(rotatedTargetOffset.m_X)) {
			targetOffset = m_Parent->RotateOffset(m_IdleOffset);
		}

		Vector distanceFromTargetOffsetToAnkleOffset(targetOffset - m_AnkleOffset);
		m_AnkleOffset += distanceFromTargetOffsetToAnkleOffset * m_MoveSpeed;
		m_AnkleOffset.ClampMagnitude(m_MaxExtension, m_MinExtension + 0.1F);
	} else {
		m_AnkleOffset.SetXY(m_MaxExtension * 0.60F, 0);
		m_AnkleOffset.RadRotate((m_HFlipped ? c_PI : 0) + m_Rotation.GetRadAngle());
	}
}

void Leg::UpdateLegRotation() {
	if (IsAttached()) {
		m_Rotation.SetRadAngle(GetAngleToReach(m_AnkleOffset, m_NormalizedExtension));
		m_AngularVel = 0.0F;
	}
}

float Leg::GetAngleToReach(const Vector& ankleOffset, float normalizedExtension) const {
	float angle = ankleOffset.GetAbsRadAngle() + (m_HFlipped ? c_PI : 0);

	// Get a normalized scalar for where the Leg should be rotated to between the contracted and extended offsets. EaseOut is used to get the sine effect needed.
	float extraRotationRatio = m_MaxExtension > m_MinExtension ? (EaseOut(m_MinExtension, m_MaxExtension, normalizedExtension) - m_MinExtension) / (m_MaxExtension - m_MinExtension) : 0.0F;

	// The contracted offset's inverse angle is the base for the rotation correction.
	float extraRotation = -(m_ContractedOffset.GetAbsRadAngle());

	// Get the actual amount of extra rotation correction needed from the ratio, somewhere on the arc between contracted and extended angles.
	// This is negative because it's a correction, the bitmap needs to rotate back to align the ankle with where it's supposed to be in the sprite.
	extraRotation -= (m_ExtendedOffset.GetAbsRadAngle() - m_ContractedOffset.GetAbsRadAngle()) * extraRotationRatio;

	return angle + extraRotation * GetFlipFactor();
}

Vector Leg::GetKnee(const Vector& hip, const Vector& ankle) const {
	float thigh = std::max(m_KneeOffset.GetMagnitude(), 1.0F);
	float shin = std::max(m_ShinAnkleOffset.GetMagnitude(), 1.0F);
	Vector toAnkle = ankle - hip;
	float reach = std::clamp(toAnkle.GetMagnitude(), std::abs(thigh - shin) + 0.01F, thigh + shin - 0.01F);
	if (toAnkle.MagnitudeIsLessThan(0.01F)) {
		toAnkle.SetXY(0.0F, 1.0F);
	}
	toAnkle.SetMagnitude(1.0F);
	// The angle at the hip between the ankle and the knee (two-bone reach, as MechLeg's), bent to whichever side is the way it faces.
	float hipAngle = std::acos(std::clamp((thigh * thigh + reach * reach - shin * shin) / (2.0F * thigh * reach), -1.0F, 1.0F));
	Vector one = toAnkle.GetRadRotatedCopy(hipAngle) * thigh;
	Vector other = toAnkle.GetRadRotatedCopy(-hipAngle) * thigh;
	float facing = m_HFlipped ? -1.0F : 1.0F;
	return hip + (one.m_X * facing >= other.m_X * facing ? one : other);
}

void Leg::UpdateFootOnGround() {
	if (!m_Foot) {
		return;
	}
	Vector nudge;
	float tilt = 0.0F;
	const Actor* owner = dynamic_cast<const Actor*>(GetRootParent());
	// Planted (drawn flat, as UpdateFootFrameAndRotation does: the ankle well below the hip), on a unit standing up.
	if (owner && UnitSprings::Enabled() && owner->GetStatus() == Actor::STABLE && m_AnkleOffset.GetY() > std::abs(m_AnkleOffset.GetX() * 0.3F)) {
		float width = static_cast<float>(std::max(m_Foot->GetSpriteWidth(), 3));
		float height = static_cast<float>(std::max(m_Foot->GetSpriteHeight(), 2));
		Vector middle = m_Foot->GetPos();
		float bottom = middle.m_Y + height * 0.5F;
		UnitSprings::GroundFeel ground = UnitSprings::FeelGround(middle.m_X - width * 0.45F, middle.m_X + width * 0.45F, bottom - 3.0F, 6.0F);
		if (ground.Found && std::abs(ground.Y - bottom) <= 3.0F) {
			// On the ground it really stands on, not a pixel above or into it, and turned to its slope.
			nudge.m_Y = std::clamp(ground.Y - bottom, -2.5F, 2.5F);
			tilt = std::clamp(std::atan(ground.Slope), -0.6F, 0.6F);
		}
	}
	// Eased there, so a foot doesn't snap as it is lifted and set down.
	m_FootNudge += (nudge - m_FootNudge) * 0.5F;
	m_FootTilt += (tilt - m_FootTilt) * 0.5F;
	m_Foot->SetDrawNudge(m_FootNudge, m_FootTilt);
}

void Leg::UpdateFlop() {
	if (m_Foot && !m_FootNudge.IsZero()) {
		m_FootNudge.Reset();
		m_FootTilt = 0.0F;
		m_Foot->SetDrawNudge(Vector());
	}
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	if (!UnitSprings::Enabled() || deltaTime <= 0.0F || m_MaxExtension <= 0.0F) {
		return;
	}
	// The lower leg is a pendulum hung at the knee: it swings with the leg's own pushes (a landing, a blast) and gravity, against a little
	// stiffness left in it, and the further it swings the more the knee bends. It only goes as far as the ground lets it.
	Vector push = (m_Vel - m_FlopLastVel) / deltaTime;
	m_FlopLastVel = m_Vel;
	Vector felt = g_SceneMan.GetGlobalAcc() - push;
	float baseAngle = (m_HFlipped ? c_PI : 0) + m_Rotation.GetRadAngle();
	Vector along = Vector(1.0F, 0.0F).RadRotate(baseAngle + m_Flop);
	float length = std::max(m_MaxExtension * 0.5F, 1.0F) * c_MPP;
	float swing = (felt.m_X * -along.m_Y + felt.m_Y * along.m_X) / length;
	// (And the leg's own spin: the lower leg lags it.)
	float steps = 4.0F;
	float step = deltaTime / steps;
	for (int i = 0; i < 4; ++i) {
		m_FlopSpeed += (swing * 0.5F - 12.0F * m_Flop - 2.0F * m_FlopSpeed) * step;
		m_Flop += m_FlopSpeed * step;
	}
	m_FlopSpeed -= m_AngularVel * 0.02F;
	if (std::abs(m_Flop) > 1.3F) {
		m_Flop = std::copysign(1.3F, m_Flop);
		m_FlopSpeed = 0.0F;
	}
	float bend = std::min(std::abs(m_Flop) / 1.3F, 1.0F);
	float extension = m_MaxExtension - (m_MaxExtension - m_MinExtension) * 0.9F * bend;
	Vector ankle = Vector(extension, 0.0F).RadRotate(baseAngle + m_Flop);
	// Into the ground: swung back towards straight until it isn't.
	for (int tries = 0; tries < 4 && UnitSprings::HoldsFoot(m_JointPos.m_X + ankle.m_X, m_JointPos.m_Y + ankle.m_Y); ++tries) {
		m_Flop *= 0.6F;
		m_FlopSpeed = 0.0F;
		bend = std::min(std::abs(m_Flop) / 1.3F, 1.0F);
		extension = m_MaxExtension - (m_MaxExtension - m_MinExtension) * 0.9F * bend;
		ankle = Vector(extension, 0.0F).RadRotate(baseAngle + m_Flop);
	}
	m_AnkleOffset = ankle;
	if (m_FrameCount != 1) {
		m_NormalizedExtension = std::clamp((extension - m_MinExtension) / (m_MaxExtension - m_MinExtension), 0.0F, 1.0F);
		m_Frame = std::min(m_FrameCount - 1, static_cast<unsigned int>(std::floor(m_NormalizedExtension * static_cast<float>(m_FrameCount))));
	}
	if (m_Foot) {
		Vector ankleOffsetAsParentOffset = RotateOffset(m_JointOffset) + m_AnkleOffset;
		ankleOffsetAsParentOffset.RadRotate(-m_Rotation.GetRadAngle()).FlipX(m_HFlipped);
		m_Foot->SetParentOffset(ankleOffsetAsParentOffset);
	}
}

bool Leg::GetDrawPose(DrawPose& pose) const {
	bool shin = m_ShinSpriteTexture != nullptr;
	if (!shin && (!IsAttached() || !UnitSprings::Enabled())) {
		return false;
	}
	// The hip, moved with the body's springs; the ankle, where its foot is drawn (set on the ground by itself).
	Vector hip = InterpolateRenderPosition(m_RenderPrevPos, m_Pos) + (m_JointPos - m_Pos) + GetDrawNudge();
	Vector ankle = m_Foot ? m_Foot->GetRenderPos() + (m_Foot->GetJointPos() - m_Foot->GetPos()) : hip - GetDrawNudge() + m_AnkleOffset;
	Vector ankleOffset = g_SceneMan.ShortestDistance(hip, ankle, g_SceneMan.SceneWrapsX());
	if (!shin && (ankleOffset - m_AnkleOffset).GetSqrMagnitude() < 0.06F) {
		return false;
	}
	float angle;
	unsigned int frame = 0;
	if (shin) {
		// The thigh: from the hip to the knee.
		Vector knee = GetKnee(Vector(), ankleOffset);
		angle = knee.GetAbsRadAngle() + (m_HFlipped ? c_PI : 0) - m_KneeOffset.GetAbsRadAngle() * GetFlipFactor();
	} else {
		// The one picture, bent by its frames: nearer the hip is a more bent frame, as the walk's own.
		ankleOffset.ClampMagnitude(m_MaxExtension, m_MinExtension + 0.1F);
		float extension = m_MaxExtension > m_MinExtension ? std::clamp((ankleOffset.GetMagnitude() - m_MinExtension) / (m_MaxExtension - m_MinExtension), 0.0F, 1.0F) : 0.0F;
		frame = m_FrameCount != 1 ? std::min(m_FrameCount - 1, static_cast<unsigned int>(std::floor(extension * static_cast<float>(m_FrameCount)))) : m_Frame;
		angle = GetAngleToReach(ankleOffset, extension);
	}
	Vector joint = m_JointOffset.GetXFlipped(m_HFlipped);
	Matrix turn(angle);
	joint *= turn;
	pose.Pos = hip - joint;
	pose.Angle = angle;
	pose.Frame = frame;
	return true;
}

void Leg::Draw(const Camera& camera) const {
	Attachable::Draw(camera);
	if (!m_ShinSpriteTexture || !camera.IsVisible(m_Pos, m_SpriteRadius * 2.0F)) {
		return;
	}
	// The shin: from the knee to the ankle, where its foot is drawn.
	Vector hip = InterpolateRenderPosition(m_RenderPrevPos, m_Pos) + (m_JointPos - m_Pos) + GetDrawNudge();
	Vector ankle = m_Foot ? m_Foot->GetRenderPos() + (m_Foot->GetJointPos() - m_Foot->GetPos()) : hip - GetDrawNudge() + m_AnkleOffset;
	Vector toAnkle = g_SceneMan.ShortestDistance(hip, ankle, g_SceneMan.SceneWrapsX());
	Vector knee = hip + GetKnee(Vector(), toAnkle);
	Vector kneeToAnkle = (hip + toAnkle) - knee;
	float angle = kneeToAnkle.GetAbsRadAngle() + (m_HFlipped ? c_PI : 0) - m_ShinAnkleOffset.GetAbsRadAngle() * GetFlipFactor();
	Vector joint = m_ShinJointOffset.GetXFlipped(m_HFlipped);
	Matrix turn(angle);
	joint *= turn;
	Vector middle = (knee - joint).GetRounded();
	float width = static_cast<float>(m_ShinSpriteTexture->GetDimensions().w);
	float height = static_cast<float>(m_ShinSpriteTexture->GetDimensions().h);
	glm::vec2 scale(m_HFlipped ? -1.0F : 1.0F, 1.0F);
	Draw::DrawTexture(m_ShinSpriteTexture.get(), glm::vec2(middle.m_X, middle.m_Y), glm::vec2(-std::floor(width * 0.5F), -std::floor(height * 0.5F)), -angle, scale);
}

void Leg::UpdateFootFrameAndRotation() {
	if (m_Foot) {
		if (IsAttached() && m_AnkleOffset.GetY() > std::abs(m_AnkleOffset.GetX() * 0.3F)) {
			float ankleOffsetHorizontalDistanceAccountingForFlipping = m_AnkleOffset.GetXFlipped(m_HFlipped).GetX();
			if (ankleOffsetHorizontalDistanceAccountingForFlipping < -m_MaxExtension * 0.6F) {
				m_Foot->SetFrame(3);
			} else if (ankleOffsetHorizontalDistanceAccountingForFlipping < -m_MaxExtension * 0.4F) {
				m_Foot->SetFrame(2);
			} else if (ankleOffsetHorizontalDistanceAccountingForFlipping > m_MaxExtension * 0.4F) {
				m_Foot->SetFrame(1);
			} else {
				m_Foot->SetFrame(0);
			}
			m_Foot->SetRotAngle(0.0F);
		} else {
			m_Foot->SetRotAngle(m_Rotation.GetRadAngle() + c_HalfPI * GetFlipFactor());
		}
	}
}
