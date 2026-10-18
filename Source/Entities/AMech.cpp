#include "AMech.h"
#include "DetMath.h"
#include "MechLeg.h"
#include "ADoor.h"
#include "AEJetpack.h"
#include "CameraMan.h"
#include "Controller.h"
#include "FrameMan.h"
#include "HeldDevice.h"
#include "MovableMan.h"
#include "PresetMan.h"
#include "SceneMan.h"
#include "SettingsMan.h"
#include "TimerMan.h"
#include "Turret.h"
#include "ZeroGravity.h"

#include "tracy/Tracy.hpp"

using namespace RTE;

ConcreteClassInfo(AMech, ACrab, 10);

AMech::AMech() {
	Clear();
}

AMech::~AMech() {
	Destroy(true);
}

void AMech::Clear() {
	m_FGLeg = nullptr;
	m_BGLeg = nullptr;
	m_Feet[0] = Foot();
	m_Feet[1] = Foot();
	m_WalkSpeed = 2.5F;
	m_StrideLength = -1.0F;
	m_StepTime = 0.55F;
	m_StepLift = -1.0F;
	m_StandExtension = 0.86F;
	m_MaxStepUp = -1.0F;
	m_MaxStepDown = -1.0F;
	m_TurnTime = 0.7F;
	m_BodyBob = 2.0F;
	m_FootfallShake = 3.0F;
	m_StompDamage = 0.6F;
	m_StompMassRatio = 0.15F;
	m_BreachStrength = 0.0F;
	m_Airborne = true;
	m_Blocked = false;
	m_TurnProgress = -1.0F;
	m_Braced = false;
	m_KickTimer.Reset();
	m_WalkDirection = 0;
}

int AMech::Create() {
	if (ACrab::Create() < 0) {
		return -1;
	}
	// (Its legs are its own, not the crab's four: it isn't a legless crawler, CR-9.)
	SetBornLegless(false);
	return 0;
}

int AMech::Create(const AMech& reference) {
	if (reference.m_FGLeg) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_FGLeg->GetUniqueID());
	}
	if (reference.m_BGLeg) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_BGLeg->GetUniqueID());
	}
	ACrab::Create(reference);

	// (The far leg first, so it is drawn behind the near one.)
	if (reference.m_BGLeg) {
		SetBGLeg(dynamic_cast<MechLeg*>(reference.m_BGLeg->Clone()));
	}
	if (reference.m_FGLeg) {
		SetFGLeg(dynamic_cast<MechLeg*>(reference.m_FGLeg->Clone()));
	}
	m_WalkSpeed = reference.m_WalkSpeed;
	m_StrideLength = reference.m_StrideLength;
	m_StepTime = reference.m_StepTime;
	m_StepLift = reference.m_StepLift;
	m_StandExtension = reference.m_StandExtension;
	m_MaxStepUp = reference.m_MaxStepUp;
	m_MaxStepDown = reference.m_MaxStepDown;
	m_TurnTime = reference.m_TurnTime;
	m_BodyBob = reference.m_BodyBob;
	m_FootfallShake = reference.m_FootfallShake;
	m_StompDamage = reference.m_StompDamage;
	m_StompMassRatio = reference.m_StompMassRatio;
	m_BreachStrength = reference.m_BreachStrength;
	m_Braced = reference.m_Braced;
	SetBornLegless(false);
	return 0;
}

int AMech::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return ACrab::ReadProperty(propName, reader));

	MatchProperty("FGLeg", { SetFGLeg(dynamic_cast<MechLeg*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("BGLeg", { SetBGLeg(dynamic_cast<MechLeg*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("WalkSpeed", { reader >> m_WalkSpeed; });
	MatchProperty("StrideLength", { reader >> m_StrideLength; });
	MatchProperty("StepTime", { reader >> m_StepTime; });
	MatchProperty("StepLift", { reader >> m_StepLift; });
	MatchProperty("StandExtension", { reader >> m_StandExtension; });
	MatchProperty("MaxStepUp", { reader >> m_MaxStepUp; });
	MatchProperty("MaxStepDown", { reader >> m_MaxStepDown; });
	MatchProperty("TurnTime", { reader >> m_TurnTime; });
	MatchProperty("BodyBob", { reader >> m_BodyBob; });
	MatchProperty("FootfallShake", { reader >> m_FootfallShake; });
	MatchProperty("StompDamage", { reader >> m_StompDamage; });
	MatchProperty("StompMassRatio", { reader >> m_StompMassRatio; });
	MatchProperty("BreachStrength", { reader >> m_BreachStrength; });
	MatchProperty("Braced", { reader >> m_Braced; });

	EndPropertyList;
}

int AMech::Save(Writer& writer) const {
	ACrab::Save(writer);

	writer.NewProperty("FGLeg");
	writer << m_FGLeg;
	writer.NewProperty("BGLeg");
	writer << m_BGLeg;
	writer.NewPropertyWithValue("WalkSpeed", m_WalkSpeed);
	writer.NewPropertyWithValue("StrideLength", m_StrideLength);
	writer.NewPropertyWithValue("StepTime", m_StepTime);
	writer.NewPropertyWithValue("StepLift", m_StepLift);
	writer.NewPropertyWithValue("StandExtension", m_StandExtension);
	writer.NewPropertyWithValue("MaxStepUp", m_MaxStepUp);
	writer.NewPropertyWithValue("MaxStepDown", m_MaxStepDown);
	writer.NewPropertyWithValue("TurnTime", m_TurnTime);
	writer.NewPropertyWithValue("BodyBob", m_BodyBob);
	writer.NewPropertyWithValue("FootfallShake", m_FootfallShake);
	writer.NewPropertyWithValue("StompDamage", m_StompDamage);
	writer.NewPropertyWithValue("StompMassRatio", m_StompMassRatio);
	writer.NewPropertyWithValue("BreachStrength", m_BreachStrength);
	writer.NewPropertyWithValue("Braced", m_Braced);

	return 0;
}

void AMech::Destroy(bool notInherited) {
	if (!notInherited) {
		ACrab::Destroy();
	}
	Clear();
}

void AMech::SetFGLeg(MechLeg* newLeg) {
	if (m_FGLeg && m_FGLeg->IsAttached()) {
		RemoveAndDeleteAttachable(m_FGLeg);
	}
	m_FGLeg = newLeg;
	if (!m_FGLeg) {
		return;
	}
	AddAttachable(m_FGLeg);
	m_HardcodedAttachableUniqueIDsAndSetters.insert({m_FGLeg->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                 MechLeg* castedAttachable = dynamic_cast<MechLeg*>(attachable);
		                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetFGLeg");
		                                                 dynamic_cast<AMech*>(parent)->SetFGLeg(castedAttachable);
	                                                 }});
	if (m_FGLeg->HasNoSetDamageMultiplier()) {
		m_FGLeg->SetDamageMultiplier(1.0F);
	}
	m_FGLeg->SetInheritsRotAngle(false);
}

void AMech::SetBGLeg(MechLeg* newLeg) {
	if (m_BGLeg && m_BGLeg->IsAttached()) {
		RemoveAndDeleteAttachable(m_BGLeg);
	}
	m_BGLeg = newLeg;
	if (!m_BGLeg) {
		return;
	}
	AddAttachable(m_BGLeg);
	m_HardcodedAttachableUniqueIDsAndSetters.insert({m_BGLeg->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                 MechLeg* castedAttachable = dynamic_cast<MechLeg*>(attachable);
		                                                 RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to SetBGLeg");
		                                                 dynamic_cast<AMech*>(parent)->SetBGLeg(castedAttachable);
	                                                 }});
	if (m_BGLeg->HasNoSetDamageMultiplier()) {
		m_BGLeg->SetDamageMultiplier(1.0F);
	}
	m_BGLeg->SetInheritsRotAngle(false);
}

MechLeg* AMech::LegAt(int foot) const {
	MechLeg* leg = foot == 0 ? m_FGLeg : m_BGLeg;
	return leg && leg->IsAttached() ? leg : nullptr;
}

bool AMech::HasLegsLeft() const {
	return HasLeg(0) || HasLeg(1);
}

float AMech::GetLegReach() const {
	float reach = 0.0F;
	for (int foot = 0; foot < 2; ++foot) {
		if (const MechLeg* leg = LegAt(foot)) {
			reach = std::max(reach, leg->GetReach());
		}
	}
	return reach > 0.0F ? reach : std::max(m_CharHeight * 0.45F, 20.0F);
}

float AMech::GetStrideLength() const {
	return m_StrideLength >= 0.0F ? m_StrideLength : GetLegReach() * 0.75F;
}

float AMech::GetMaxStepUp() const {
	return m_MaxStepUp >= 0.0F ? m_MaxStepUp : GetLegReach() * 0.4F;
}

float AMech::GetMaxStepDown() const {
	return m_MaxStepDown >= 0.0F ? m_MaxStepDown : GetLegReach() * 0.9F;
}

float AMech::GetStandingHeight() const {
	return std::max(m_CharHeight, 20.0F);
}

void AMech::SetBraced(bool braced) {
	m_Braced = braced;
	if (braced) {
		m_TurnProgress = -1.0F;
	}
}

void AMech::OnFlippedByAim() {
	// (Turned by the aim, not the walk: any turn on the spot under way is done.)
	m_TurnProgress = -1.0F;
}

bool AMech::MountedDeviceMayFire(const HeldDevice* device) const {
	if (!device->HasBlade() && device->GetMeleeSwingArc() <= 0.0F) {
		return true;
	}
	if (m_Controller.IsPlayerControlled()) {
		return true;
	}
	// The AI swings its blade only at an enemy within its reach (it would swing at the air at every target in sight, else).
	const float reach = (device->GetBladeEnd() - device->GetBladeStart()).GetMagnitude() + 16.0F;
	Vector distance;
	return g_MovableMan.GetClosestEnemyActor(m_Team, device->GetPos(), static_cast<int>(reach), distance) != nullptr;
}

float AMech::GroundUnder(float x, float fromY, float depth) {
	Vector hit;
	// (The first solid pixel, not air or a liquid's surface standing on the ground: SceneMan's "not air" ray.)
	if (g_SceneMan.CastNotMaterialRay(Vector(x, fromY), Vector(0.0F, depth), MaterialColorKeys::g_MaterialAir, hit, 0, false)) {
		return hit.m_Y;
	}
	return -1.0F;
}

void AMech::UpdateWalkIntent() {
	m_WalkDirection = 0;
	if (m_Controller.IsDisabled() || m_Status == INACTIVE || m_Braced) {
		m_MovementState = STAND;
		m_TurnProgress = -1.0F;
		return;
	}
	int want = m_Controller.IsState(MOVE_RIGHT) ? 1 : (m_Controller.IsState(MOVE_LEFT) ? -1 : 0);
	const int facing = m_HFlipped ? -1 : 1;
	// Walking backwards when the aim holds the other way, as a crab does.
	const bool aimHeld = std::abs(m_Controller.GetAnalogAim().m_X) > 0.0F || m_Controller.IsState(AIM_SHARP);
	if (want != 0 && want != facing && !aimHeld && m_TurnProgress < 0.0F && !m_Airborne) {
		m_TurnProgress = 0.0F;
	}
	if (m_TurnProgress >= 0.0F) {
		// Turning round on the spot: half the turn facing the old way, then the hull swings round.
		const float before = m_TurnProgress;
		m_TurnProgress += g_TimerMan.GetDeltaTimeSecs() / std::max(m_TurnTime, 0.05F);
		if (before < 0.5F && m_TurnProgress >= 0.5F) {
			SetHFlipped(!m_HFlipped);
			m_CheckTerrIntersection = true;
			MoveOutOfTerrain(g_MaterialGrass);
		}
		if (m_TurnProgress >= 1.0F || want == 0) {
			m_TurnProgress = -1.0F;
		}
		m_MovementState = STAND;
		return;
	}
	m_WalkDirection = want;
	m_MovementState = want != 0 ? WALK : STAND;
}

void AMech::PreControllerUpdate() {
	ZoneScoped;

	Actor::PreControllerUpdate();

	float deltaTime = g_TimerMan.GetDeltaTimeSecs();

	// A jetpack and a leap of the legs, as a crab's (a mech built with them).
	UpdateLeap();
	if (m_pJetpack && m_pJetpack->IsAttached()) {
		m_pJetpack->UpdateBurstState(*this);
		if (m_Leaping) {
			m_MovementState = JUMP;
		}
	}

	bool keepOldState = m_Controller.IsKeyboardOnlyControlled() && m_Controller.IsState(PIE_MENU_ACTIVE);
	if (!keepOldState) {
		UpdateWalkIntent();
	}

	UpdateAimAndFire(m_Controller.GetAnalogAim());
	if (m_Braced && m_pTurret && m_pTurret->IsAttached()) {
		// Braced: its aim held steady, as a soldier's sharp aim is.
		for (HeldDevice* mountedDevice: m_pTurret->GetMountedDevices()) {
			mountedDevice->SetSharpAim(std::max(m_SharpAimProgress, 0.7F));
		}
	}

	if (m_Controller.IsDisabled()) {
		m_MovementState = STAND;
		m_WalkDirection = 0;
		if (m_pJetpack && m_pJetpack->IsAttached()) {
			m_pJetpack->EnableEmission(false);
		}
	}

	m_StrideFrame = false;
	UpdateGait(deltaTime);
	if (m_MovementState != WALK && m_StrideSound && m_StrideSound->GetLoopSetting() < 0) {
		m_StrideSound->Stop();
	}

	if (m_pTurret && m_pTurret->IsAttached()) {
		m_pTurret->SetMountedDeviceRotationOffset((m_AimAngle * GetFlipFactor()) - m_Rotation.GetRadAngle());
	}
}

void AMech::UpdateGait(float deltaTime) {
	if (!HasLegsLeft()) {
		m_Airborne = true;
		return;
	}
	// Held up by something else: knocked down, dying, jetting, in a leap, pulled up a ledge, in a gravity lift or floating weightless.
	const bool jetting = m_pJetpack && m_pJetpack->IsAttached() && m_pJetpack->IsEmitting();
	const bool weightless = ZeroGravity::UnitsWeightless();
	if (m_Status != STABLE || jetting || m_Leaping || IsMantling() || HeldByAutomover() || weightless) {
		if (!m_Airborne) {
			m_Feet[0].Planted = m_Feet[1].Planted = false;
			m_Feet[0].Swinging = m_Feet[1].Swinging = false;
			m_Airborne = true;
		}
		UpdateAirborne();
		return;
	}

	// A planted foot whose ground has gone (blown away, or a door opened under it) is lifted.
	for (Foot& foot: m_Feet) {
		if (foot.Planted && GroundUnder(foot.Pos.m_X, foot.Pos.m_Y - 4.0F, 12.0F) < 0.0F) {
			foot.Planted = false;
		}
	}
	if (m_Airborne || (!m_Feet[0].Planted && !m_Feet[1].Planted && !m_Feet[0].Swinging && !m_Feet[1].Swinging)) {
		m_Airborne = true;
		UpdateAirborne();
		if (m_Airborne) {
			return;
		}
	}

	const float reach = GetLegReach();
	const float stride = GetStrideLength();
	const int facing = m_HFlipped ? -1 : 1;
	float hipX = 0.0F;
	float hipY = 0.0F;
	int hips = 0;
	for (int foot = 0; foot < 2; ++foot) {
		if (const MechLeg* leg = LegAt(foot)) {
			hipX += leg->GetHipPos().m_X;
			hipY += leg->GetHipPos().m_Y;
			++hips;
		}
	}
	hipX /= static_cast<float>(hips);
	hipY /= static_cast<float>(hips);

	// The step under way.
	int swinging = m_Feet[0].Swinging ? 0 : (m_Feet[1].Swinging ? 1 : -1);
	if (swinging >= 0) {
		Foot& foot = m_Feet[swinging];
		// (Slower when the walk is slowed: a group's pace, venom, a heavy load.)
		foot.Progress += deltaTime / std::max(m_StepTime, 0.05F);
		float s = std::min(foot.Progress, 1.0F);
		float lift = m_StepLift >= 0.0F ? m_StepLift : reach / 6.0F;
		foot.Pos = foot.From + (foot.To - foot.From) * s + Vector(0.0F, -lift * DetMath::Sin(s * c_PI));
		if (foot.Progress >= 1.0F) {
			Footfall(swinging);
			swinging = -1;
		}
	}

	// The next step: walking, the foot furthest behind steps out ahead; standing, a foot far from where it stands is brought back under.
	m_Blocked = false;
	if (swinging < 0) {
		int stepper = -1;
		float stepTo = 0.0F;
		if (m_WalkDirection != 0) {
			const float dir = static_cast<float>(m_WalkDirection);
			int back = -1;
			float mostBehind = -FLT_MAX;
			for (int foot = 0; foot < 2; ++foot) {
				if (!HasLeg(foot)) {
					continue;
				}
				float behind = (hipX - m_Feet[foot].Pos.m_X) * dir;
				if (behind > mostBehind) {
					mostBehind = behind;
					back = foot;
				}
			}
			if (back >= 0 && mostBehind > -stride * 0.1F) {
				stepper = back;
				stepTo = hipX + dir * stride * 0.55F + m_Vel.m_X * c_PPM * m_StepTime * 0.5F;
			}
		} else {
			for (int foot = 0; foot < 2; ++foot) {
				if (!HasLeg(foot)) {
					continue;
				}
				// (The near foot a little ahead of the hip and the far one a little behind: a stance.)
				float stand = hipX + static_cast<float>(facing) * stride * (foot == 0 ? 0.12F : -0.12F);
				if (std::abs(m_Feet[foot].Pos.m_X - stand) > stride * 0.3F) {
					stepper = foot;
					stepTo = stand;
					break;
				}
			}
		}
		if (stepper >= 0 && !StartStep(stepper, stepTo)) {
			// No step that far: a shorter one, else held up where it is.
			const float shorter = hipX + (stepTo - hipX) * 0.4F;
			if (!StartStep(stepper, shorter)) {
				m_Blocked = m_WalkDirection != 0;
			}
		}
	}

	CarryHull(deltaTime);

	for (int foot = 0; foot < 2; ++foot) {
		if (MechLeg* leg = LegAt(foot)) {
			leg->SetAnkleTarget(m_Feet[foot].Pos);
			leg->SetFootAngle(m_Feet[foot].Planted ? m_Feet[foot].Angle : 0.0F);
		}
	}
}

void AMech::UpdateAirborne() {
	const float reach = GetLegReach();
	const float stand = reach * m_StandExtension;
	// Down on the ground again: falling or still, with ground under the hips within the legs' stand, and not pushed up by a jet.
	const bool jetting = m_pJetpack && m_pJetpack->IsAttached() && m_pJetpack->IsEmitting();
	if (m_Status == STABLE && !jetting && !m_Leaping && !IsMantling() && !HeldByAutomover() && !ZeroGravity::UnitsWeightless() && m_Vel.m_Y > -1.0F) {
		bool landed = false;
		for (int foot = 0; foot < 2; ++foot) {
			MechLeg* leg = LegAt(foot);
			if (!leg) {
				continue;
			}
			Vector hip = leg->GetHipPos();
			float x = hip.m_X + GetFlipFactor() * GetStrideLength() * (foot == 0 ? 0.12F : -0.12F);
			float ground = GroundUnder(x, hip.m_Y, stand * 1.05F + leg->GetFootDrop());
			if (ground >= 0.0F) {
				m_Feet[foot].Pos.SetXY(x, ground - leg->GetFootDrop());
				m_Feet[foot].Planted = true;
				m_Feet[foot].Swinging = false;
				m_Feet[foot].Angle = 0.0F;
				landed = true;
			}
		}
		if (landed) {
			// The landing shakes the ground as hard as it came down.
			if (m_Airborne && m_Vel.m_Y > 3.0F) {
				g_CameraMan.AddScreenShake(m_FootfallShake * std::min(m_Vel.m_Y / 3.0F, 4.0F) * g_SettingsMan.MechFootfallShake(), m_Pos);
				m_Vel.m_Y *= 0.3F;
			}
			m_Airborne = false;
			return;
		}
	}
	// The legs hang under the hips, a little forward, the feet following.
	for (int foot = 0; foot < 2; ++foot) {
		if (MechLeg* leg = LegAt(foot)) {
			Vector hang = leg->GetHipPos() + RotateOffset(Vector(reach * (foot == 0 ? 0.2F : 0.05F), reach * 0.9F));
			m_Feet[foot].Pos = hang;
			m_Feet[foot].Planted = false;
			m_Feet[foot].Swinging = false;
			leg->SetAnkleTarget(hang);
			leg->SetFootAngle(GetRotAngle());
		}
	}
}

bool AMech::StartStep(int foot, float targetX) {
	MechLeg* leg = LegAt(foot);
	if (!leg) {
		return false;
	}
	const Vector hip = leg->GetHipPos();
	// The ground under the planted foot (or the hip, with none planted) is where the step's rise and drop are measured from.
	const int other = 1 - foot;
	float floorY = m_Feet[other].Planted ? m_Feet[other].Pos.m_Y + leg->GetFootDrop() : (m_Feet[foot].Planted ? m_Feet[foot].Pos.m_Y + leg->GetFootDrop() : hip.m_Y + GetLegReach());
	const float up = GetMaxStepUp();
	const float down = GetMaxStepDown();

	// A wall between, taller than a step: kicked at, if it can be, else no step.
	const float lookFrom = floorY - up - 2.0F;
	Vector wallHit;
	const float span = targetX - m_Feet[foot].Pos.m_X;
	if (std::abs(span) > 1.0F && g_SceneMan.CastNotMaterialRay(Vector(m_Feet[foot].Pos.m_X, lookFrom), Vector(span, 0.0F), MaterialColorKeys::g_MaterialAir, wallHit, 0, false)) {
		Kick(wallHit.m_X, floorY);
		return false;
	}
	// The foot's ground: from a step up over the floor, down as far as a step goes.
	float ground = GroundUnder(targetX, lookFrom, up + down + 2.0F);
	if (ground < 0.0F) {
		return false;
	}
	// (Within the leg's reach of the hip, standing.)
	if (ground - leg->GetFootDrop() - hip.m_Y > leg->GetReach() * 1.02F) {
		return false;
	}
	Foot& step = m_Feet[foot];
	step.From = step.Pos;
	step.To.SetXY(targetX, ground - leg->GetFootDrop());
	step.Progress = 0.0F;
	step.Swinging = true;
	step.Planted = false;
	// The slope it will stand on, from the ground either side.
	float left = GroundUnder(targetX - 6.0F, lookFrom, up + down + 2.0F);
	float right = GroundUnder(targetX + 6.0F, lookFrom, up + down + 2.0F);
	step.Angle = (left >= 0.0F && right >= 0.0F) ? std::clamp(-DetMath::Atan2(right - left, 12.0F), -0.5F, 0.5F) : 0.0F;
	return true;
}

void AMech::Footfall(int foot) {
	Foot& step = m_Feet[foot];
	step.Pos = step.To;
	step.Swinging = false;
	step.Planted = true;
	step.Progress = 0.0F;

	m_StrideFrame = true;
	RunScriptedFunctionInAppropriateScripts("OnStride");
	if (m_StrideSound) {
		m_StrideSound->Play(step.Pos);
	}
	if (m_FootfallShake > 0.0F) {
		g_CameraMan.AddScreenShake(m_FootfallShake * g_SettingsMan.MechFootfallShake(), step.Pos);
	}

	// What's under the foot: a small enemy is stamped on, a small friend pushed out from under it. Anything heavy it steps beside.
	const MechLeg* leg = LegAt(foot);
	const float footHalfWidth = leg && leg->GetFoot() ? static_cast<float>(leg->GetFoot()->GetSpriteWidth()) * 0.5F : 12.0F;
	const float sole = step.Pos.m_Y + (leg ? leg->GetFootDrop() : 0.0F);
	const bool wraps = g_SceneMan.SceneWrapsX();
	const float myMass = GetMass();
	for (Actor* other: g_MovableMan.GetActorList()) {
		if (other == this || other->IsDead() || other->IsSetToDelete() || other->GetRootParent() != other) {
			continue;
		}
		const Vector toOther = g_SceneMan.ShortestDistance(Vector(step.Pos.m_X, sole), other->GetPos(), wraps);
		const float radius = other->GetRadius();
		if (std::abs(toOther.m_X) > footHalfWidth + radius * 0.5F || toOther.m_Y > 4.0F || toOther.m_Y < -radius * 2.0F - 8.0F) {
			continue;
		}
		if (other->GetMass() > myMass * m_StompMassRatio) {
			continue;
		}
		const float side = toOther.m_X >= 0.0F ? 1.0F : -1.0F;
		const float otherMass = std::max(other->GetMass(), 1.0F);
		const bool enemy = other->GetTeam() != m_Team || other->IsHostileToAll();
		if (enemy && m_StompDamage > 0.0F) {
			other->AddHealth(-other->GetMaxHealth() * m_StompDamage);
			other->AddAbsImpulseForce(Vector(side * 4.0F, 2.0F) * otherMass, other->GetPos());
		} else {
			other->AddAbsImpulseForce(Vector(side * 5.0F, -2.0F) * otherMass, other->GetPos());
		}
	}
}

void AMech::CarryHull(float deltaTime) {
	const float reach = GetLegReach();
	const float stand = reach * m_StandExtension;
	float ankleY = 0.0F;
	int planted = 0;
	for (const Foot& foot: m_Feet) {
		if (foot.Planted) {
			ankleY += foot.Pos.m_Y;
			++planted;
		}
	}
	if (planted == 0) {
		return;
	}
	ankleY /= static_cast<float>(planted);

	// The hips' height over the feet: the legs' stand, the hull rising a little mid-step, and never higher than the planted legs reach.
	float hipOffsetY = 0.0F;
	float hipX = 0.0F;
	int hips = 0;
	for (int foot = 0; foot < 2; ++foot) {
		if (const MechLeg* leg = LegAt(foot)) {
			Vector hip = leg->GetHipPos();
			hipOffsetY += hip.m_Y - m_Pos.m_Y;
			hipX += hip.m_X;
			++hips;
		}
	}
	hipOffsetY /= static_cast<float>(hips);
	hipX /= static_cast<float>(hips);
	float bob = 0.0F;
	for (const Foot& foot: m_Feet) {
		if (foot.Swinging) {
			bob = m_BodyBob * DetMath::Sin(std::min(foot.Progress, 1.0F) * c_PI);
		}
	}
	float hipY = ankleY - stand - bob;
	for (int foot = 0; foot < 2; ++foot) {
		if (m_Feet[foot].Planted && HasLeg(foot)) {
			float across = std::abs(m_Feet[foot].Pos.m_X - hipX);
			float legReach = LegAt(foot)->GetReach() * 0.98F;
			float highest = m_Feet[foot].Pos.m_Y - std::sqrt(std::max(legReach * legReach - across * across, 1.0F));
			hipY = std::max(hipY, highest);
		}
	}
	const float wantY = hipY - hipOffsetY;
	// Up and down: held to the height by a stiff spring, its speed set outright (the legs carry it, not gravity).
	m_Vel.m_Y = std::clamp((wantY - m_Pos.m_Y) * 10.0F * c_MPP, -6.0F, 6.0F);

	// Along: the walk's pace (a group's pace, venom, a load), eased into; held up, braced or stood still, it stops on its feet.
	float pace = m_WalkSpeed * m_SpeedMultiplier * GetAfflictionSpeedMultiplier() * GetOverloadSpeedMultiplier();
	if (m_PaceLimit > 0.0F) {
		pace = std::min(pace, m_PaceLimit);
	}
	float wantX = m_Blocked || m_Braced ? 0.0F : static_cast<float>(m_WalkDirection) * pace;
	// (Not out past the front foot: the body waits for a step.)
	if (m_WalkDirection != 0) {
		const float dir = static_cast<float>(m_WalkDirection);
		float front = -FLT_MAX;
		for (const Foot& foot: m_Feet) {
			if (foot.Planted || foot.Swinging) {
				front = std::max(front, (foot.Swinging ? foot.To.m_X : foot.Pos.m_X) * dir);
			}
		}
		if (front > -FLT_MAX && hipX * dir > front + GetStrideLength() * 0.25F) {
			wantX = 0.0F;
		}
	}
	const float accel = (m_Braced ? 40.0F : 8.0F) * deltaTime;
	m_Vel.m_X += std::clamp(wantX - m_Vel.m_X, -accel, accel);
	m_AngularVel *= m_Braced ? 0.5F : 0.9F;
}

void AMech::Kick(float wallX, float floorY) {
	// (Only walking into it: a wall beside a foot settling under it is left alone.)
	if (m_BreachStrength <= 0.0F || !m_KickTimer.IsPastSimMS(700) || m_WalkDirection == 0) {
		return;
	}
	const float dir = wallX >= m_Pos.m_X ? 1.0F : -1.0F;
	const float top = floorY - GetStandingHeight();
	const bool wraps = g_SceneMan.SceneWrapsX();

	// A door across the way that isn't its own side's: its leaf broken off.
	for (ADoor* door: g_MovableMan.GetDoorList()) {
		if (door->GetTeam() == m_Team || !door->GetDoor() || !door->GetDoor()->IsAttached()) {
			continue;
		}
		Vector toLeaf = g_SceneMan.ShortestDistance(Vector(wallX, (top + floorY) * 0.5F), door->GetDoor()->GetPos(), wraps);
		if (std::abs(toLeaf.m_X) < 24.0F && std::abs(toLeaf.m_Y) < (floorY - top) * 0.6F) {
			m_KickTimer.Reset();
			door->GetDoor()->GibThis();
			g_CameraMan.AddScreenShake(m_FootfallShake * 2.0F * g_SettingsMan.MechFootfallShake(), door->GetPos());
			return;
		}
	}

	// A wall: a slab of it as tall as the mech knocked out, if nothing in it is stronger than it kicks.
	const float depth = 8.0F;
	const float x0 = dir > 0.0F ? wallX : wallX - depth;
	const float x1 = x0 + depth;
	for (float x = x0; x <= x1; x += 4.0F) {
		if (g_SceneMan.CastMaxStrengthRay(Vector(x, top), Vector(x, floorY - 2.0F), 2) > m_BreachStrength) {
			return;
		}
	}
	m_KickTimer.Reset();
	std::vector<MOPixel*>* knocked = g_SceneMan.DislodgePixelBox(Vector(x0, top), Vector(x1, floorY - 2.0F), true);
	delete knocked;
	g_CameraMan.AddScreenShake(m_FootfallShake * 2.0F * g_SettingsMan.MechFootfallShake(), Vector(wallX, floorY));
}

void AMech::Update() {
	ZoneScoped;

	ACrab::Update();

	// The hull is held upright (its gyros): on its feet hard, so a hit, a gun's kick or a knock to a leg never tips it over the way it
	// would a crab (the crab's own righting only works while it's stable, and a fall leaves it unstable for a while); in the air more gently.
	// Only once it's dying does it go over.
	if (m_Status != DYING && m_Status != DEAD && HasLegsLeft()) {
		float rot = m_Rotation.GetRadAngle();
		while (rot > c_PI) {
			rot -= c_TwoPI;
		}
		while (rot < -c_PI) {
			rot += c_TwoPI;
		}
		const float rate = m_Airborne ? 3.0F : 12.0F;
		rot -= rot * std::min(1.0F, rate * g_TimerMan.GetDeltaTimeSecs());
		m_Rotation.SetRadAngle(rot);
		m_AngularVel = 0.0F;
	}

	// Braced, it is hardly pushed about: what knocks it is mostly soaked by its planted feet.
	if (m_Braced && !m_Airborne) {
		m_Vel.m_X *= 0.5F;
	}
}

PathAgent AMech::GetPathAgent() const {
	PathAgent agent = ACrab::GetPathAgent();
	// As tall as it stands, never crouching or crawling under anything lower, and as wide as its hull (out to the grid's two nodes a side).
	agent.StandHeight = std::clamp(GetStandingHeight(), 8.0F, static_cast<float>(PathNode::c_ClearanceReach));
	agent.CrawlHeight = agent.StandHeight;
	agent.CrouchHeight = 0.0F;
	agent.HalfWidth = std::clamp(static_cast<float>(GetSpriteWidth()) * 0.3F, 8.0F, 48.0F);
	// Its legs step up ledges and stairs (the steps the gait takes), and down drops a little longer than its legs.
	agent.WalksStairs = true;
	agent.ClimbsLadders = false;
	agent.Scrambles = false;
	agent.MantleHeight = GetMaxStepUp();
	if (!m_pJetpack) {
		agent.MaxSafeFall = std::min(agent.MaxSafeFall, GetLegReach() * 1.6F);
	}
	// Doors it kicks in (MK-3); a machine, over lava.
	agent.BreachStrength = m_BreachStrength;
	agent.CrossesLava = true;
	ApplyMovementProfile(agent);
	return agent;
}

void AMech::Draw(BITMAP* pTargetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const {
	Actor::Draw(pTargetBitmap, targetPos, mode, onlyPhysical);

	// Debug (the Pilot channel): the gait. A planted foot green, a stepping one red, where the step lands a yellow cross, and a red bar
	// ahead of the hull when the walk is blocked.
	if (mode == g_DrawColor && !onlyPhysical && g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Pilot)) {
		for (int foot = 0; foot < 2; ++foot) {
			const Vector at = m_Feet[foot].Pos - targetPos;
			circle(pTargetBitmap, at.GetFloorIntX(), at.GetFloorIntY(), 4, m_Feet[foot].Planted ? g_GreenColor : g_RedColor);
		}
		if (IsStepping()) {
			const Vector to = GetStepTarget() - targetPos;
			line(pTargetBitmap, to.GetFloorIntX() - 4, to.GetFloorIntY() - 4, to.GetFloorIntX() + 4, to.GetFloorIntY() + 4, g_YellowGlowColor);
			line(pTargetBitmap, to.GetFloorIntX() - 4, to.GetFloorIntY() + 4, to.GetFloorIntX() + 4, to.GetFloorIntY() - 4, g_YellowGlowColor);
		}
		if (m_Blocked) {
			const Vector ahead = m_Pos - targetPos + Vector(GetFlipFactor() * GetRadius() * 0.6F, 0.0F);
			line(pTargetBitmap, ahead.GetFloorIntX(), ahead.GetFloorIntY() - 30, ahead.GetFloorIntX(), ahead.GetFloorIntY() + 30, g_RedColor);
		}
	}
}
