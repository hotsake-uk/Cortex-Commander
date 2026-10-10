#include "HeldDevice.h"

#include "CameraMan.h"
#include "MovableMan.h"
#include "AtomGroup.h"
#include "Arm.h"
#include "AHuman.h"

#include "GameActivity.h"

#include "GUI.h"
#include "AllegroBitmap.h"

#include "SettingsMan.h"
#include "FrameMan.h"
#include "PostProcessMan.h"
#include "SceneMan.h"
#include "TimerMan.h"
#include "MOPixel.h"
#include "MOSParticle.h"
#include "Atom.h"
#include "Leg.h"
#include "EffectsParticles.h"
#include "SoundContainer.h"

using namespace RTE;

namespace {
	/// Whether a material rings like metal when a blade strikes it, so the strike sparks: metal and armour, not dirt, rock or wood.
	bool IsMetalForBlades(const Material* material) {
		if (!material) {
			return false;
		}
		const std::string& name = material->GetPresetName();
		return name.find("Metal") != std::string::npos || name.find("Steel") != std::string::npos || name.find("Iron") != std::string::npos || name == "Military Stuff";
	}
} // namespace


ConcreteClassInfo(HeldDevice, Attachable, 50);

HeldDevice::HeldDevice() {
	Clear();
}

HeldDevice::~HeldDevice() {
	Destroy(true);
}

void HeldDevice::Clear() {
	m_HeldDeviceType = WEAPON;
	m_Activated = false;
	m_HotkeyActivated.fill(false);
	m_ActivationTimer.Reset();
	for (Timer timer : m_HotkeyActivationTimer) {
		timer.Reset();
	}
	m_OneHanded = false;
	m_DualWieldable = false;
	m_StanceOffset.Reset();
	m_SharpStanceOffset.Reset();
	m_SharpAim = 0.0F;
	m_MaxSharpLength = 0;
	m_Supportable = true;
	m_Supported = false;
	m_SupportAvailable = false;
	m_SupportOffset.Reset();
	m_UseSupportOffsetWhileReloading = false;
	m_SeenByPlayer.fill(false);
	m_IsUnPickupable = false;
	m_PickupableByPresetNames.clear();
	m_GripStrengthMultiplier = 1.0F;
	m_BlinkTimer.Reset();
	m_BlinkTimer.SetSimTimeLimitMS(1000);
	m_Loudness = -1;
	m_IsExplosiveWeapon = false;
	m_GetsHitByMOsWhenHeld = false;
	m_VisualRecoilMultiplier = 1.0F;

	m_BladeStart.Reset();
	m_BladeEnd.Reset();
	m_BladeHiltGap = 0.0F;
	m_BladeEnergy = false;
	m_BladeColor.SetRGB(255, 255, 255);
	m_BladeWidth = 1.6F;
	m_BladeBrightness = 1.0F;
	m_BladeLightRadius = -1.0F;
	m_BladeSharpness = -1.0F;
	m_BladeMass = -1.0F;
	m_BladeMinSpeed = -1.0F;
	m_BladeHitInterval = -1.0F;
	m_BladeCutsTerrain = -1.0F;
	m_BladeSevers = false;
	m_BladeSeversSet = -1;
	m_BladeDeflects = false;
	m_BladeDeflectsSet = -1;
	m_BladeIgniteTime = -1.0F;
	m_BladeLit = true;
	m_BladeExtension = 0.0F;
	m_BladeHitSound = nullptr;
	m_BladeClashSound = nullptr;
	m_BladeSwingSound = nullptr;
	m_BladeHumSound = nullptr;
	m_BladeIgniteSound = nullptr;
	m_MeleeSwingArc = 0.0F;
	m_MeleeSwingTime = 260.0F;
	m_MeleeRecoverTime = 200.0F;
	m_MeleeSwingPhase = MeleeSwingPhase::Idle;
	m_MeleeSwingAngle = 0.0F;
	m_MeleeSwingRecoverFrom = 0.0F;
	m_MeleeSwingTimer.Reset();
	m_BladePreviousValid = false;
	m_BladePreviousStart.Reset();
	m_BladePreviousEnd.Reset();
	m_BladePreviousRootID = 0;
	m_BladeLastCut.clear();
	m_BladeClashTimer.Reset();
	m_BladeClashTimer.SetElapsedSimTimeMS(10000.0);
	m_BladeHitTimer.Reset();
	m_BladeHitTimer.SetElapsedSimTimeMS(10000.0);
	m_BladeTerrainTimer.Reset();

	// NOTE: This special override of a parent class member variable avoids needing an extra variable to avoid overwriting INI values.
	m_CollidesWithTerrainWhileAttached = false;
}

int HeldDevice::Create() {
	if (Attachable::Create() < 0)
		return -1;

	// Set MO Type.
	m_MOType = MovableObject::TypeHeldDevice;

	// Set HeldDeviceType based on tags
	if (IsInGroup("Weapons"))
		m_HeldDeviceType = WEAPON;
	else if (IsInGroup("Tools"))
		m_HeldDeviceType = TOOL;
	else if (IsInGroup("Shields"))
		m_HeldDeviceType = SHIELD;

	if (IsInGroup("Weapons - Explosive"))
		m_IsExplosiveWeapon = true;
	else
		m_IsExplosiveWeapon = false;

	// Backwards compatibility so that the tag is added for sure
	if (m_HeldDeviceType == WEAPON)
		AddToGroup("Weapons");
	else if (m_HeldDeviceType == TOOL)
		AddToGroup("Tools");
	else if (m_HeldDeviceType == SHIELD)
		AddToGroup("Shields");

	// No Loudness set in the ini-file
	if (m_Loudness < 0) {
		if (m_HeldDeviceType == TOOL)
			m_Loudness = 0.5; // Force tools to make less noise
		else
			m_Loudness = 1.0;
	}

	// Make it so held devices are dropped gently when their parent gibs
	m_ParentGibBlastStrengthMultiplier = 0.0F;

	// Make it so users can't accidentally set this to true for HeldDevices, since it'll cause crashes when swapping inventory items around.
	m_DeleteWhenRemovedFromParent = false;

	// All HeldDevice:s by default avoid hitting and getting physically hit by AtomGoups when they are at rest
	m_IgnoresAGHitsWhenSlowerThan = 1.0;

	// By default, held items should not be able to be squished and destroyed into the ground at all
	m_CanBeSquished = false;

	return 0;
}

int HeldDevice::Create(const HeldDevice& reference) {
	Attachable::Create(reference);

	// Set MO Type.
	m_MOType = MovableObject::TypeHeldDevice;

	m_HeldDeviceType = reference.m_HeldDeviceType;

	m_Activated = reference.m_Activated;
	m_HotkeyActivated = reference.m_HotkeyActivated;
	m_ActivationTimer = reference.m_ActivationTimer;
	m_HotkeyActivationTimer = reference.m_HotkeyActivationTimer;

	m_OneHanded = reference.m_OneHanded;
	m_DualWieldable = reference.m_DualWieldable;
	m_StanceOffset = reference.m_StanceOffset;
	m_SharpStanceOffset = reference.m_SharpStanceOffset;
	m_SupportOffset = reference.m_SupportOffset;
	m_UseSupportOffsetWhileReloading = reference.m_UseSupportOffsetWhileReloading;
	m_Supportable = reference.m_Supportable;
	m_IsUnPickupable = reference.m_IsUnPickupable;
	for (std::string referenceActorWhoCanPickThisUp: reference.m_PickupableByPresetNames) {
		m_PickupableByPresetNames.insert(referenceActorWhoCanPickThisUp);
	}
	m_GripStrengthMultiplier = reference.m_GripStrengthMultiplier;

	m_SharpAim = reference.m_SharpAim;
	m_MaxSharpLength = reference.m_MaxSharpLength;
	m_Supportable = reference.m_Supportable;
	m_Supported = reference.m_Supported;
	m_SupportAvailable = reference.m_SupportAvailable;
	m_Loudness = reference.m_Loudness;
	m_IsExplosiveWeapon = reference.m_IsExplosiveWeapon;
	m_GetsHitByMOsWhenHeld = reference.m_GetsHitByMOsWhenHeld;
	m_VisualRecoilMultiplier = reference.m_VisualRecoilMultiplier;

	m_BladeStart = reference.m_BladeStart;
	m_BladeEnd = reference.m_BladeEnd;
	m_BladeHiltGap = reference.m_BladeHiltGap;
	m_BladeEnergy = reference.m_BladeEnergy;
	m_BladeColor = reference.m_BladeColor;
	m_BladeWidth = reference.m_BladeWidth;
	m_BladeBrightness = reference.m_BladeBrightness;
	m_BladeLightRadius = reference.m_BladeLightRadius;
	m_BladeSharpness = reference.m_BladeSharpness;
	m_BladeMass = reference.m_BladeMass;
	m_BladeMinSpeed = reference.m_BladeMinSpeed;
	m_BladeHitInterval = reference.m_BladeHitInterval;
	m_BladeCutsTerrain = reference.m_BladeCutsTerrain;
	m_BladeSevers = reference.m_BladeSevers;
	m_BladeSeversSet = reference.m_BladeSeversSet;
	m_BladeDeflects = reference.m_BladeDeflects;
	m_BladeDeflectsSet = reference.m_BladeDeflectsSet;
	m_BladeIgniteTime = reference.m_BladeIgniteTime;
	m_BladeLit = reference.m_BladeLit;
	m_BladeExtension = reference.m_BladeExtension;
	for (auto [sound, referenceSound]: {std::make_pair(&m_BladeHitSound, reference.m_BladeHitSound), std::make_pair(&m_BladeClashSound, reference.m_BladeClashSound), std::make_pair(&m_BladeSwingSound, reference.m_BladeSwingSound), std::make_pair(&m_BladeHumSound, reference.m_BladeHumSound), std::make_pair(&m_BladeIgniteSound, reference.m_BladeIgniteSound)}) {
		*sound = referenceSound ? dynamic_cast<SoundContainer*>(referenceSound->Clone()) : nullptr;
	}
	m_MeleeSwingArc = reference.m_MeleeSwingArc;
	m_MeleeSwingTime = reference.m_MeleeSwingTime;
	m_MeleeRecoverTime = reference.m_MeleeRecoverTime;

	return 0;
}

int HeldDevice::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Attachable::ReadProperty(propName, reader));

	MatchProperty("HeldDeviceType", { reader >> m_HeldDeviceType; });
	MatchProperty("OneHanded", { reader >> m_OneHanded; });
	MatchProperty("DualWieldable", { reader >> m_DualWieldable; });
	MatchProperty("StanceOffset", { reader >> m_StanceOffset; });
	MatchProperty("SharpStanceOffset", { reader >> m_SharpStanceOffset; });
	MatchProperty("Supportable", { reader >> m_Supportable; });
	MatchProperty("SupportOffset", { reader >> m_SupportOffset; });
	MatchProperty("UseSupportOffsetWhileReloading", { reader >> m_UseSupportOffsetWhileReloading; });
	MatchProperty("PickupableBy", {
		std::string pickupableByValue = reader.ReadPropValue();
		if (pickupableByValue == "PickupableByEntries") {
			while (reader.NextProperty()) {
				std::string pickupableByEntryType = reader.ReadPropName();
				if (pickupableByEntryType == "AddPresetNameEntry") {
					m_PickupableByPresetNames.insert(reader.ReadPropValue());
				} else if (pickupableByEntryType == "AddClassNameEntry ") {
					reader.ReportError("AddClassNameEntry is not yet supported.");
				} else if (pickupableByEntryType == "AddGroupEntry") {
					reader.ReportError("AddGroupEntry is not yet supported.");
				} else if (pickupableByEntryType == "AddDataModuleEntry ") {
					reader.ReportError("AddDataModuleEntry is not yet supported.");
				} else {
					break;
				}
			}
		} else if (pickupableByValue == "None") {
			SetUnPickupable(true);
		}
	});
	MatchProperty("GripStrengthMultiplier", { reader >> m_GripStrengthMultiplier; });
	MatchProperty("SharpLength", { reader >> m_MaxSharpLength; });
	MatchProperty("Loudness", { reader >> m_Loudness; });
	MatchProperty("GetsHitByMOsWhenHeld", { reader >> m_GetsHitByMOsWhenHeld; });
	MatchProperty("VisualRecoilMultiplier", { reader >> m_VisualRecoilMultiplier; });
	MatchProperty("BladeStart", { reader >> m_BladeStart; });
	MatchProperty("BladeEnd", { reader >> m_BladeEnd; });
	MatchProperty("BladeHiltGap", {
		reader >> m_BladeHiltGap;
		m_BladeHiltGap = std::max(m_BladeHiltGap, 0.0F);
	});
	MatchProperty("BladeEnergy", { reader >> m_BladeEnergy; });
	MatchProperty("BladeColor", { reader >> m_BladeColor; });
	MatchProperty("BladeWidth", { reader >> m_BladeWidth; });
	MatchProperty("BladeBrightness", { reader >> m_BladeBrightness; });
	MatchProperty("BladeLightRadius", { reader >> m_BladeLightRadius; });
	MatchProperty("BladeSharpness", { reader >> m_BladeSharpness; });
	MatchProperty("BladeMass", { reader >> m_BladeMass; });
	MatchProperty("BladeMinSpeed", { reader >> m_BladeMinSpeed; });
	MatchProperty("BladeHitInterval", { reader >> m_BladeHitInterval; });
	MatchProperty("BladeCutsTerrain", { reader >> m_BladeCutsTerrain; });
	MatchProperty("BladeSevers", {
		reader >> m_BladeSevers;
		m_BladeSeversSet = m_BladeSevers ? 1 : 0;
	});
	MatchProperty("BladeDeflects", {
		reader >> m_BladeDeflects;
		m_BladeDeflectsSet = m_BladeDeflects ? 1 : 0;
	});
	MatchProperty("BladeIgniteTime", { reader >> m_BladeIgniteTime; });
	MatchProperty("BladeLit", { reader >> m_BladeLit; });
	MatchProperty("BladeHitSound", {
		delete m_BladeHitSound;
		m_BladeHitSound = new SoundContainer;
		reader >> m_BladeHitSound;
	});
	MatchProperty("BladeClashSound", {
		delete m_BladeClashSound;
		m_BladeClashSound = new SoundContainer;
		reader >> m_BladeClashSound;
	});
	MatchProperty("BladeSwingSound", {
		delete m_BladeSwingSound;
		m_BladeSwingSound = new SoundContainer;
		reader >> m_BladeSwingSound;
	});
	MatchProperty("BladeHumSound", {
		delete m_BladeHumSound;
		m_BladeHumSound = new SoundContainer;
		reader >> m_BladeHumSound;
	});
	MatchProperty("BladeIgniteSound", {
		delete m_BladeIgniteSound;
		m_BladeIgniteSound = new SoundContainer;
		reader >> m_BladeIgniteSound;
	});
	MatchProperty("MeleeSwingArc", { reader >> m_MeleeSwingArc; });
	MatchProperty("MeleeSwingTime", { reader >> m_MeleeSwingTime; });
	MatchProperty("MeleeRecoverTime", { reader >> m_MeleeRecoverTime; });
	MatchProperty("SpecialBehaviour_Activated", { reader >> m_Activated; });
	MatchProperty("SpecialBehaviour_ActivationTimerElapsedSimTimeMS", {
		double elapsedSimTimeMS;
		reader >> elapsedSimTimeMS;
		m_ActivationTimer.SetElapsedSimTimeMS(elapsedSimTimeMS);
	});

	EndPropertyList;
}

int HeldDevice::Save(Writer& writer) const {
	Attachable::Save(writer);
	/*
	    writer.NewLine();
	    writer << "// 0 = Offensive Weapon, 1 = Tool, 2 = Shield";
	    writer.NewProperty("HeldDeviceType");
	    writer << m_HeldDeviceType;
	*/
	writer.NewProperty("OneHanded");
	writer << m_OneHanded;
	writer.NewProperty("StanceOffset");
	writer << m_StanceOffset;
	writer.NewProperty("SharpStanceOffset");
	writer << m_SharpStanceOffset;
	writer.NewPropertyWithValue("Supportable", m_Supportable);
	writer.NewProperty("SupportOffset");
	writer << m_SupportOffset;
	writer.NewPropertyWithValue("UseSupportOffsetWhileReloading", m_UseSupportOffsetWhileReloading);
	writer.NewProperty("GripStrengthMultiplier");
	writer << m_GripStrengthMultiplier;
	writer.NewProperty("SharpLength");
	writer << m_MaxSharpLength;
	writer.NewProperty("Loudness");
	writer << m_Loudness;
	writer.NewProperty("GetsHitByMOsWhenHeld");
	writer << m_GetsHitByMOsWhenHeld;
	writer.NewProperty("VisualRecoilMultiplier");
	writer << m_VisualRecoilMultiplier;
	if (HasBlade()) {
		writer.NewPropertyWithValue("BladeStart", m_BladeStart);
		writer.NewPropertyWithValue("BladeEnd", m_BladeEnd);
		writer.NewPropertyWithValue("BladeHiltGap", m_BladeHiltGap);
		writer.NewPropertyWithValue("BladeEnergy", m_BladeEnergy);
		writer.NewPropertyWithValue("BladeColor", m_BladeColor);
		writer.NewPropertyWithValue("BladeWidth", m_BladeWidth);
		writer.NewPropertyWithValue("BladeBrightness", m_BladeBrightness);
		writer.NewPropertyWithValue("BladeLightRadius", m_BladeLightRadius);
		writer.NewPropertyWithValue("BladeSharpness", m_BladeSharpness);
		writer.NewPropertyWithValue("BladeMass", m_BladeMass);
		writer.NewPropertyWithValue("BladeMinSpeed", m_BladeMinSpeed);
		writer.NewPropertyWithValue("BladeHitInterval", m_BladeHitInterval);
		writer.NewPropertyWithValue("BladeCutsTerrain", m_BladeCutsTerrain);
		if (m_BladeSeversSet >= 0) {
			writer.NewPropertyWithValue("BladeSevers", m_BladeSevers);
		}
		if (m_BladeDeflectsSet >= 0) {
			writer.NewPropertyWithValue("BladeDeflects", m_BladeDeflects);
		}
		writer.NewPropertyWithValue("BladeIgniteTime", m_BladeIgniteTime);
		writer.NewPropertyWithValue("BladeLit", m_BladeLit);
		for (auto [name, sound]: {std::make_pair("BladeHitSound", m_BladeHitSound), std::make_pair("BladeClashSound", m_BladeClashSound), std::make_pair("BladeSwingSound", m_BladeSwingSound), std::make_pair("BladeHumSound", m_BladeHumSound), std::make_pair("BladeIgniteSound", m_BladeIgniteSound)}) {
			if (sound) {
				writer.NewProperty(name);
				writer << sound;
			}
		}
	}
	if (m_MeleeSwingArc > 0.0F) {
		writer.NewPropertyWithValue("MeleeSwingArc", m_MeleeSwingArc);
		writer.NewPropertyWithValue("MeleeSwingTime", m_MeleeSwingTime);
		writer.NewPropertyWithValue("MeleeRecoverTime", m_MeleeRecoverTime);
	}

	return 0;
}

void HeldDevice::Destroy(bool notInherited) {
	for (SoundContainer* sound: {m_BladeHitSound, m_BladeClashSound, m_BladeSwingSound, m_BladeHumSound, m_BladeIgniteSound}) {
		if (sound) {
			sound->Stop();
			delete sound;
		}
	}

	if (!notInherited)
		Attachable::Destroy();
	Clear();
}

Vector HeldDevice::GetStanceOffset() const {
	if (m_SharpAim > 0) {
		float rotAngleScalar = std::abs(std::sin(GetRootParent()->GetRotAngle()));
		// Deviate the vertical axis towards regular StanceOffset based on the user's rotation so that sharp aiming doesn't look awkward when prone
		return Vector(m_SharpStanceOffset.GetX(), m_SharpStanceOffset.GetY() * (1.0F - rotAngleScalar) + m_StanceOffset.GetY() * rotAngleScalar).GetXFlipped(m_HFlipped);
	} else
		return m_StanceOffset.GetXFlipped(m_HFlipped);
}

Vector HeldDevice::GetSupportPos() const {
	/*
	    Vector rotOff(m_SupportOffset.GetYFlipped(m_HFlipped));
	    rotOff.RadRotate(m_HFlipped ? (c_PI + m_Rotation) : m_Rotation);
	    return m_Pos + rotOff;
	*/
	return m_Pos + RotateOffset(m_SupportOffset);
}

Vector HeldDevice::GetMagazinePos() const {
	return m_Pos;
}

bool HeldDevice::IsBeingHeld() const {
	return dynamic_cast<const Arm*>(m_Parent);
}

void HeldDevice::RemovePickupableByPresetName(const std::string& actorPresetName) {
	std::unordered_set<std::string>::iterator pickupableByPresetNameEntry = m_PickupableByPresetNames.find(actorPresetName);
	if (pickupableByPresetNameEntry != m_PickupableByPresetNames.end()) {
		m_PickupableByPresetNames.erase(pickupableByPresetNameEntry);
	}
}

bool HeldDevice::CollideAtPoint(HitData& hd) {
	if (!m_GetsHitByMOsWhenHeld && IsBeingHeld()) {
		return false;
	}

	return Attachable::CollideAtPoint(hd);
}

void HeldDevice::Activate() {
	if (!m_Activated) {
		m_ActivationTimer.Reset();
	}

	m_Activated = true;
	StartMeleeSwing();
}

void HeldDevice::Deactivate() {
	m_Activated = false;
}

void HeldDevice::ActivateHotkeyAction(HeldDeviceHotkeyType hotkeyType) {
	if (!m_HotkeyActivated[hotkeyType]) {
		m_HotkeyActivationTimer[hotkeyType].Reset();
	}

	m_HotkeyActivated[hotkeyType] = true;
}

void HeldDevice::DeactivateHotkeyAction(HeldDeviceHotkeyType hotkeyType) {
	m_HotkeyActivated[hotkeyType] = false;
}

bool HeldDevice::TransferJointImpulses(Vector& jointImpulses, float jointStiffnessValueToUse, float jointStrengthValueToUse, float gibImpulseLimitValueToUse) {
	MovableObject* parent = m_Parent;
	if (!parent) {
		return false;
	}
	if (m_ImpulseForces.empty()) {
		return true;
	}
	const Arm* parentAsArm = dynamic_cast<Arm*>(parent);
	if (parentAsArm && parentAsArm->GetGripStrength() > 0 && jointStrengthValueToUse < 0) {
		jointStrengthValueToUse = parentAsArm->GetGripStrength() * m_GripStrengthMultiplier;
		if (m_Supported) {
			if (const AHuman* rootParentAsAHuman = dynamic_cast<AHuman*>(GetRootParent())) {
				jointStrengthValueToUse += rootParentAsAHuman->GetBGArm() ? rootParentAsAHuman->GetBGArm()->GetGripStrength() * m_GripStrengthMultiplier : 0.0F;
			}
		}
	}
	bool intact = Attachable::TransferJointImpulses(jointImpulses, jointStiffnessValueToUse, jointStrengthValueToUse, gibImpulseLimitValueToUse);
	if (!intact) {
		Actor* rootParentAsActor = dynamic_cast<Actor*>(parent->GetRootParent());
		if (rootParentAsActor && rootParentAsActor->GetStatus() == Actor::STABLE) {
			rootParentAsActor->SetStatus(Actor::UNSTABLE);
		}
	}
	return intact;
}

/*
void HeldDevice::Travel()
{
    Attachable::Travel();
}
*/

void HeldDevice::Update() {
	Attachable::Update();

	// Remove loose items that have completely disappeared into the terrain, unless they're pinned
	if (!m_Parent && m_PinStrength <= 0 && m_RestTimer.IsPastSimMS(20000) && m_CanBeSquished && m_pAtomGroup->RatioInTerrain() > 0.9)
		GibThis();

	if (m_Activated)
		m_RestTimer.Reset();

	////////////////////////////////////////
	// Animate the sprite, if applicable

	if (m_FrameCount > 1) {
		if (m_SpriteAnimMode == LOOPWHENACTIVE && m_Activated) {
			float cycleTime = ((long)m_SpriteAnimTimer.GetElapsedSimTimeMS()) % m_SpriteAnimDuration;
			m_Frame = std::floor((cycleTime / (float)m_SpriteAnimDuration) * (float)m_FrameCount);
		}
	}

	if (!m_Parent) {

	} else {
		/////////////////////////////////
		// Update and apply rotations and scale

		// Taken care of by holder/owner Arm.
		//        m_Pos += m_ParentOffset;
		// Don't apply state changes to BITMAP anywhere else than Draw().
		//        m_aSprite->SetAngle(m_Rotation);
		//        m_aSprite->SetScale(m_Scale);
	}

	if (m_BlinkTimer.IsPastSimTimeLimit()) {
		m_BlinkTimer.Reset();
	}

	if (HasBlade() || m_MeleeSwingArc > 0.0F) {
		UpdateMeleeSwingAndBlade();
	}
}

void HeldDevice::GetDefaultSurface(float& metalness, float& gloss) const {
	metalness = 0.8F;
	gloss = 0.6F;
}

void HeldDevice::Draw(BITMAP* pTargetBitmap,
                      const Vector& targetPos,
                      DrawMode mode,
                      bool onlyPhysical) const {
	Attachable::Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
	/*
	    // Draw suporting hand if applicable.
	    if (m_Supported) {
	        Vector handPos(m_Pos.GetFloored() +
	                       RotateOffset(m_SupportOffset) +
	                       (m_Recoiled ? m_RecoilOffset : Vector()) -
	                       targetPos);
	        handPos.m_X -= m_pSupportHand->GetWidth() >> 1;
	        handPos.m_Y -= m_pSupportHand->GetHeight() >> 1;
	        if (!m_HFlipped)
	            m_pSupportHand->DrawTrans(pTargetBitmap, handPos.m_X, handPos.m_Y);
	        else
	            m_pSupportHand->DrawTransHFlip(pTargetBitmap, handPos.m_X, handPos.m_Y);
	    }
	*/
	/*
	#ifdef DEBUG_BUILD
	    if (mode == g_DrawColor && !onlyPhysical)
	    {
	        m_pAtomGroup->Draw(pTargetBitmap, targetPos, false, 122);
	        m_pDeepGroup->Draw(pTargetBitmap, targetPos, false, 13);
	    }
	#endif
	*/
}

void HeldDevice::DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos, int whichScreen, bool playerControlled) {
	if (!m_HUDVisible) {
		return;
	}

	Attachable::DrawHUD(pTargetBitmap, targetPos, whichScreen);

	if (!IsUnPickupable()) {
		if (m_Parent) {
			m_SeenByPlayer.fill(false);
			m_BlinkTimer.Reset();
		} else {
			int viewingPlayer = g_ActivityMan.GetActivity()->PlayerOfScreen(whichScreen);
			if (viewingPlayer == -1) {
				return;
			}
			// Only draw if the team viewing this has seen the space where this is located.
			int viewingTeam = g_ActivityMan.GetActivity()->GetTeamOfPlayer(viewingPlayer);
			if (viewingTeam == Activity::NoTeam || g_SceneMan.IsUnseen(m_Pos.GetFloorIntX(), m_Pos.GetFloorIntY(), viewingTeam)) {
				return;
			}

			Vector drawPos = m_Pos - targetPos;
			// Adjust the draw position to work if drawn to a target screen bitmap that is straddling a scene seam.
			if (!targetPos.IsZero()) {
				drawPos += g_SceneMan.GetWrapToScreen(drawPos, pTargetBitmap->w, pTargetBitmap->h);
			}

			GUIFont* pSymbolFont = g_FrameMan.GetLargeFont();
			GUIFont* pTextFont = g_FrameMan.GetSmallFont();
			if (pSymbolFont && pTextFont) {
				const Activity* activity = g_ActivityMan.GetActivity();
				float unheldItemDisplayRange = activity->GetActivityState() == Activity::ActivityState::Running ? g_SettingsMan.GetUnheldItemsHUDDisplayRange() : -1.0F;
				if (g_SettingsMan.AlwaysDisplayUnheldItemsInStrategicMode()) {
					const GameActivity* gameActivity = dynamic_cast<const GameActivity*>(activity);
					if (gameActivity && gameActivity->GetViewState(viewingPlayer) == GameActivity::ViewState::ActorSelect) {
						unheldItemDisplayRange = -1.0F;
					}
				}
				if (!m_SeenByPlayer[viewingPlayer]) {
					m_SeenByPlayer[viewingPlayer] = unheldItemDisplayRange < 0 || (unheldItemDisplayRange > 0 && m_Vel.MagnitudeIsLessThan(2.0F) && g_SceneMan.ShortestDistance(m_Pos, g_CameraMan.GetScrollTarget(whichScreen), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(unheldItemDisplayRange));
				} else {
					// Note - to avoid item HUDs flickering in and out, we need to add a little leeway when hiding them if they're already displayed.
					if (unheldItemDisplayRange > 0) {
						unheldItemDisplayRange += 4.0F;
					}
					m_SeenByPlayer.at(viewingPlayer) = unheldItemDisplayRange < 0 || (unheldItemDisplayRange > 0 && g_SceneMan.ShortestDistance(m_Pos, g_CameraMan.GetScrollTarget(whichScreen), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(unheldItemDisplayRange));

					char pickupArrowString[64];
					pickupArrowString[0] = 0;
					if (m_BlinkTimer.GetElapsedSimTimeMS() < 250) {
						pickupArrowString[0] = 0;
					} else if (m_BlinkTimer.GetElapsedSimTimeMS() < 500) {
						pickupArrowString[0] = -42;
						pickupArrowString[1] = 0;
					} else if (m_BlinkTimer.GetElapsedSimTimeMS() < 750) {
						pickupArrowString[0] = -41;
						pickupArrowString[1] = 0;
					} else if (m_BlinkTimer.GetElapsedSimTimeMS() < 1000) {
						pickupArrowString[0] = -40;
						pickupArrowString[1] = 0;
					}

					AllegroBitmap targetAllegroBitmap(pTargetBitmap);
					pSymbolFont->DrawAligned(&targetAllegroBitmap, drawPos.GetFloorIntX() - 1, drawPos.GetFloorIntY() - 20, pickupArrowString, GUIFont::Centre);
					pTextFont->DrawAligned(&targetAllegroBitmap, drawPos.GetFloorIntX(), drawPos.GetFloorIntY() - 29, m_PresetName, GUIFont::Centre);
				}
			}
		}
	}
}

void HeldDevice::DrawHUD(const Camera& camera) {}

namespace {
	/// A blade as it was swept this sim update, for blades to find each other and clash.
	struct BladeSweep {
		HeldDevice* Device;
		long RootID;
		Vector Start, End, PreviousStart, PreviousEnd;
	};
	std::vector<BladeSweep> s_BladeSweeps; //!< The blades swept so far this sim update.
	long long s_BladeSweepsUpdate = -1; //!< The sim update s_BladeSweeps are of.

	/// The closest points of two segments, in one frame of reference.
	/// @return The distance between them.
	float SegmentsClosestPoints(const Vector& a0, const Vector& a1, const Vector& b0, const Vector& b1, Vector& onA, Vector& onB) {
		Vector d1 = a1 - a0;
		Vector d2 = b1 - b0;
		Vector r = a0 - b0;
		float a = d1.Dot(d1);
		float e = d2.Dot(d2);
		float f = d2.Dot(r);
		float s = 0.0F;
		float t = 0.0F;
		if (a <= 1e-6F && e <= 1e-6F) {
			s = t = 0.0F;
		} else if (a <= 1e-6F) {
			t = std::clamp(f / e, 0.0F, 1.0F);
		} else {
			float c = d1.Dot(r);
			if (e <= 1e-6F) {
				s = std::clamp(-c / a, 0.0F, 1.0F);
			} else {
				float b = d1.Dot(d2);
				float denom = a * e - b * b;
				s = denom > 1e-6F ? std::clamp((b * f - c * e) / denom, 0.0F, 1.0F) : 0.0F;
				t = (b * s + f) / e;
				if (t < 0.0F) {
					t = 0.0F;
					s = std::clamp(-c / a, 0.0F, 1.0F);
				} else if (t > 1.0F) {
					t = 1.0F;
					s = std::clamp((b - c) / a, 0.0F, 1.0F);
				}
			}
		}
		onA = a0 + d1 * s;
		onB = b0 + d2 * t;
		return (onA - onB).GetMagnitude();
	}

	/// Where a segment crosses another, if it does.
	bool SegmentsCross(const Vector& a0, const Vector& a1, const Vector& b0, const Vector& b1, float& alongA) {
		Vector r = a1 - a0;
		Vector s = b1 - b0;
		float denom = r.m_X * s.m_Y - r.m_Y * s.m_X;
		if (std::abs(denom) < 1e-6F) {
			return false;
		}
		Vector q = b0 - a0;
		float t = (q.m_X * s.m_Y - q.m_Y * s.m_X) / denom;
		float u = (q.m_X * r.m_Y - q.m_Y * r.m_X) / denom;
		alongA = t;
		return t >= 0.0F && t <= 1.0F && u >= 0.0F && u <= 1.0F;
	}

	unsigned int ColorToRGB(const Color& color) {
		return (static_cast<unsigned int>(color.GetR()) << 16) | (static_cast<unsigned int>(color.GetG()) << 8) | static_cast<unsigned int>(color.GetB());
	}
} // namespace

void HeldDevice::SetParent(MOSRotating* newParent) {
	Attachable::SetParent(newParent);
	if (!newParent) {
		if (m_BladeHumSound) {
			m_BladeHumSound->Stop();
		}
		if (m_BladeEnergy) {
			m_BladeExtension = 0.0F;
		}
		m_MeleeSwingPhase = MeleeSwingPhase::Idle;
		m_MeleeSwingAngle = 0.0F;
		m_BladePreviousValid = false;
	}
}

void HeldDevice::StartMeleeSwing() {
	if (m_MeleeSwingArc <= 0.0F || m_MeleeSwingPhase != MeleeSwingPhase::Idle || !m_Parent) {
		return;
	}
	m_MeleeSwingPhase = MeleeSwingPhase::WindUp;
	m_MeleeSwingTimer.Reset();
}

void HeldDevice::UpdateMeleeSwingAndBlade() {
	if (m_MeleeSwingArc > 0.0F && m_MeleeSwingPhase != MeleeSwingPhase::Idle) {
		auto smooth = [](float p) { return p * p * (3.0F - 2.0F * p); };
		float arc = m_MeleeSwingArc * c_PI / 180.0F;
		float elapsed = static_cast<float>(m_MeleeSwingTimer.GetElapsedSimTimeMS());
		float windUpTime = std::max(m_MeleeSwingTime * 0.35F, 1.0F);
		float strikeTime = std::max(m_MeleeSwingTime * 0.65F, 1.0F);
		switch (m_MeleeSwingPhase) {
			case MeleeSwingPhase::WindUp: {
				float progress = std::min(elapsed / windUpTime, 1.0F);
				m_MeleeSwingAngle = arc * 0.55F * (1.0F - (1.0F - progress) * (1.0F - progress));
				if (progress >= 1.0F) {
					m_MeleeSwingPhase = MeleeSwingPhase::Strike;
					m_MeleeSwingTimer.Reset();
					if (m_BladeSwingSound) {
						m_BladeSwingSound->Play(m_Pos);
					}
				}
				break;
			}
			case MeleeSwingPhase::Strike: {
				// Accelerates through the arc: most of the speed, and so most of the cutting, is in the middle and end of it.
				float progress = std::min(elapsed / strikeTime, 1.0F);
				m_MeleeSwingAngle = arc * (0.55F - smooth(progress * progress * 0.5F + progress * 0.5F));
				if (progress >= 1.0F) {
					m_MeleeSwingPhase = MeleeSwingPhase::Recover;
					m_MeleeSwingRecoverFrom = m_MeleeSwingAngle;
					m_MeleeSwingTimer.Reset();
				}
				break;
			}
			case MeleeSwingPhase::Recover: {
				float progress = std::min(elapsed / std::max(m_MeleeRecoverTime, 1.0F), 1.0F);
				m_MeleeSwingAngle = m_MeleeSwingRecoverFrom * (1.0F - smooth(progress));
				if (progress >= 1.0F) {
					m_MeleeSwingPhase = MeleeSwingPhase::Idle;
					m_MeleeSwingAngle = 0.0F;
					if (m_Activated) {
						StartMeleeSwing();
					}
				}
				break;
			}
			default:
				break;
		}
	}

	if (!HasBlade()) {
		return;
	}
	bool wantLit = m_BladeLit && (!m_BladeEnergy || m_Parent);
	float wasExtension = m_BladeExtension;
	if (GetBladeIgniteTime() <= 0.0F) {
		m_BladeExtension = wantLit ? 1.0F : 0.0F;
	} else {
		float step = g_TimerMan.GetDeltaTimeMS() / GetBladeIgniteTime();
		m_BladeExtension = std::clamp(m_BladeExtension + (wantLit ? step : -step), 0.0F, 1.0F);
	}
	if (m_BladeEnergy && wasExtension <= 0.0F && m_BladeExtension > 0.0F && m_BladeIgniteSound) {
		m_BladeIgniteSound->Play(m_Pos);
	}
	if (m_BladeHumSound) {
		if (m_BladeExtension > 0.0F && m_Parent) {
			if (!m_BladeHumSound->IsBeingPlayed()) {
				m_BladeHumSound->Play(m_Pos);
			} else {
				m_BladeHumSound->SetPosition(m_Pos);
			}
		} else if (m_BladeHumSound->IsBeingPlayed()) {
			m_BladeHumSound->Stop();
		}
	}
}

void HeldDevice::PostUpdate() {
	Attachable::PostUpdate();

	if (!HasBlade() || m_BladeExtension <= 0.0F) {
		m_BladePreviousValid = false;
		return;
	}

	// Only a blade in a living unit's hands cuts, clashes or strikes: one lying on the ground, or still in a dead unit's grip, is just a thing.
	const Actor* wielder = dynamic_cast<const Actor*>(GetRootParent());
	if (!m_Parent || !wielder || wielder->GetStatus() == Actor::DYING || wielder->GetStatus() == Actor::DEAD) {
		m_BladePreviousValid = false;
		return;
	}

	Vector start = GetBladeStartPos();
	Vector end = GetBladeEndPos();
	Vector bladeVec = g_SceneMan.ShortestDistance(start, end, g_SceneMan.SceneWrapsX());
	MovableObject* holder = GetRootParent();
	long rootID = holder->GetUniqueID();
	bool attacking = m_MeleeSwingPhase == MeleeSwingPhase::Strike || m_Activated;
	float deltaSecs = std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F);

	if (m_BladeEnergy) {
		// Flickers a little, like it's alive.
		float flicker = 0.93F + 0.07F * std::sin(static_cast<float>(g_TimerMan.GetSimTimeMS()) * 0.037F + static_cast<float>(m_UniqueID));
		glm::vec3 color(m_BladeColor.GetR(), m_BladeColor.GetG(), m_BladeColor.GetB());
		float brightness = m_BladeBrightness * flicker * (0.4F + 0.6F * m_BladeExtension);
		if (m_BladeHiltGap > 0.0F) {
			// A double-bladed staff: a blade out of each end of the hilt.
			Vector middle = start + bladeVec * 0.5F;
			float hilt = std::min(m_BladeHiltGap * 0.5F / std::max(bladeVec.GetMagnitude() * 0.5F, 0.001F), 1.0F);
			g_PostProcessMan.RegisterEnergyBeam(middle + bladeVec * (0.5F * hilt), start + bladeVec, color, m_BladeWidth, brightness, GetBladeLightRadius());
			g_PostProcessMan.RegisterEnergyBeam(middle - bladeVec * (0.5F * hilt), start, color, m_BladeWidth, brightness, GetBladeLightRadius());
		} else {
			g_PostProcessMan.RegisterEnergyBeam(start, start + bladeVec, color, m_BladeWidth, brightness, GetBladeLightRadius());
		}
	}

	bool sweepValid = m_BladePreviousValid && m_BladePreviousRootID == rootID && !g_SceneMan.ShortestDistance(m_BladePreviousStart, start, g_SceneMan.SceneWrapsX()).MagnitudeIsGreaterThan(80.0F);
	Vector previousStart = sweepValid ? m_BladePreviousStart : start;
	Vector previousBladeVec = sweepValid ? g_SceneMan.ShortestDistance(m_BladePreviousStart, m_BladePreviousEnd, g_SceneMan.SceneWrapsX()) : bladeVec;
	Vector startMove = g_SceneMan.ShortestDistance(previousStart, start, g_SceneMan.SceneWrapsX());

	// Clash with other blades swept this sim update.
	if (s_BladeSweepsUpdate != g_TimerMan.GetSimUpdateCount()) {
		s_BladeSweeps.clear();
		s_BladeSweepsUpdate = g_TimerMan.GetSimUpdateCount();
	}
	for (const BladeSweep& other: s_BladeSweeps) {
		if (other.RootID == rootID || other.Device == this) {
			continue;
		}
		// In this blade's frame of reference, from its base now.
		Vector otherStart = g_SceneMan.ShortestDistance(start, other.Start, g_SceneMan.SceneWrapsX());
		if (otherStart.MagnitudeIsGreaterThan(bladeVec.GetMagnitude() + g_SceneMan.ShortestDistance(other.Start, other.End).GetMagnitude() + 60.0F)) {
			continue;
		}
		Vector otherEnd = otherStart + g_SceneMan.ShortestDistance(other.Start, other.End, g_SceneMan.SceneWrapsX());
		Vector otherPreviousStart = g_SceneMan.ShortestDistance(start, other.PreviousStart, g_SceneMan.SceneWrapsX());
		Vector otherPreviousEnd = otherPreviousStart + g_SceneMan.ShortestDistance(other.PreviousStart, other.PreviousEnd, g_SceneMan.SceneWrapsX());
		Vector myPreviousStart = startMove * -1.0F;
		Vector myPreviousEnd = myPreviousStart + previousBladeVec;

		float reach = (m_BladeEnergy ? m_BladeWidth + 1.5F : 1.5F) + (other.Device->IsBladeEnergy() ? 1.5F : 0.0F);
		Vector onMine;
		Vector onOther;
		bool touching = SegmentsClosestPoints(Vector(), bladeVec, otherStart, otherEnd, onMine, onOther) <= reach;
		if (!touching) {
			// Half way through the sim update, so blades swung through each other between updates still meet.
			touching = SegmentsClosestPoints((myPreviousStart) * 0.5F, (myPreviousEnd + bladeVec) * 0.5F, (otherPreviousStart + otherStart) * 0.5F, (otherPreviousEnd + otherEnd) * 0.5F, onMine, onOther) <= reach;
		}
		if (!touching || (BladeJustClashed() && other.Device->BladeJustClashed())) {
			continue;
		}
		Vector where = start + (onMine + onOther) * 0.5F;
		// Each blade's speed where they meet, from how far that point moved.
		float myAlong = bladeVec.GetSqrMagnitude() > 0.0F ? std::clamp(onMine.Dot(bladeVec) / bladeVec.GetSqrMagnitude(), 0.0F, 1.0F) : 0.0F;
		Vector myVel = ((Vector() + bladeVec * myAlong) - (myPreviousStart + previousBladeVec * myAlong)) / (c_PPM * deltaSecs);
		Vector otherBladeVec = otherEnd - otherStart;
		float otherAlong = otherBladeVec.GetSqrMagnitude() > 0.0F ? std::clamp((onOther - otherStart).Dot(otherBladeVec) / otherBladeVec.GetSqrMagnitude(), 0.0F, 1.0F) : 0.0F;
		Vector otherVel = ((otherStart + otherBladeVec * otherAlong) - (otherPreviousStart + (otherPreviousEnd - otherPreviousStart) * otherAlong)) / (c_PPM * deltaSecs);
		Vector relativeVel = myVel - otherVel;
		// A firm knock apart, at least, even when they meet slowly, so blades held together don't sit inside each other.
		Vector apart = (onMine - onOther).GetSqrMagnitude() > 0.01F ? (onMine - onOther).GetNormalized() : relativeVel.GetNormalized() * -1.0F;
		if (apart.GetSqrMagnitude() < 0.01F) {
			apart = Vector(0.0F, -1.0F);
		}
		float closing = std::max(relativeVel.GetMagnitude(), 4.0F);
		float reducedMass = (GetBladeMass() * other.Device->GetBladeMass()) / std::max(GetBladeMass() + other.Device->GetBladeMass(), 0.01F);
		Vector push = apart * closing * reducedMass * 1.6F;
		BladeStruck(where, push, true);
		other.Device->BladeStruck(where, push * -1.0F, true);
	}
	s_BladeSweeps.push_back({this, rootID, start, start + bladeVec, previousStart, previousStart + previousBladeVec});

	// Cut what the blade went through, each thing once, where the blade was moving fastest through it.
	std::vector<MOID> ignoreMOIDs = {holder->GetRootID()};
	int ignoreTeam = holder->IgnoresTeamHits() ? holder->GetTeam() : Activity::NoTeam;
	int samples = std::clamp(static_cast<int>(std::ceil(bladeVec.GetMagnitude() / 3.0F)) + 1, 2, 40);
	struct BladeHit {
		MOID ID;
		Vector Pos;
		Vector Vel;
	};
	std::vector<BladeHit> hits;
	auto addHit = [&](MOID id, const Vector& pos, const Vector& vel) {
		MovableObject* hitMO = g_MovableMan.GetMOFromID(id);
		if (!hitMO) {
			return;
		}
		MOID root = hitMO->GetRootID();
		for (BladeHit& hit: hits) {
			if (g_MovableMan.GetMOFromID(hit.ID) && g_MovableMan.GetMOFromID(hit.ID)->GetRootID() == root) {
				if (vel.GetSqrMagnitude() > hit.Vel.GetSqrMagnitude()) {
					hit = {id, pos, vel};
				}
				return;
			}
		}
		hits.push_back({id, pos, vel});
	};
	int terrainBites = 0;
	bool struckTerrain = false;
	Vector terrainStrikePos;
	Vector terrainStrikeVel;
	bool terrainStrikeMetal = false;
	for (int i = 0; i < samples; ++i) {
		float along = static_cast<float>(i) / static_cast<float>(samples - 1);
		Vector now = start + bladeVec * along;
		Vector before = previousStart + previousBladeVec * along;
		Vector move = g_SceneMan.ShortestDistance(before, now, g_SceneMan.SceneWrapsX());
		Vector vel = move / (c_PPM * deltaSecs);
		if (move.MagnitudeIsGreaterThan(0.5F)) {
			if (MOID hitID = g_SceneMan.CastMORay(before, move, ignoreMOIDs, ignoreTeam, 0, true, 0); hitID != g_NoMOID) {
				addHit(hitID, g_SceneMan.GetLastRayHitPos(), vel);
			}
		}

		// Ground: an energy blade, or a blade made to, cuts through what's weak enough while attacking; anything else stops the blade.
		if (i > 0 && (attacking || move.MagnitudeIsGreaterThan(2.0F))) {
			unsigned char terrain = g_SceneMan.GetTerrMatter(static_cast<int>(now.m_X), static_cast<int>(now.m_Y));
			if (terrain != g_MaterialAir) {
				const Material* material = g_SceneMan.GetMaterialFromID(terrain);
				if (attacking && material->GetIntegrity() <= GetBladeCutsTerrain()) {
					if (terrainBites < 6) {
						++terrainBites;
						g_SceneMan.DislodgePixelBool(static_cast<int>(now.m_X), static_cast<int>(now.m_Y), m_BladeEnergy);
						if (m_BladeEnergy && RandomNum() < 0.3F) {
							EffectsParticles::Emit(RandomNum() < 0.5F ? "Embers" : "Sparks", now, vel * 0.1F + Vector(0.0F, -2.0F), 0.6F, 1, ColorToRGB(m_BladeColor));
						}
					}
				} else if (!struckTerrain && move.MagnitudeIsGreaterThan(1.5F) && material->GetIntegrity() > 0.0F) {
					struckTerrain = true;
					terrainStrikePos = now;
					terrainStrikeVel = vel;
					terrainStrikeMetal = IsMetalForBlades(material);
				}
			}
		}
	}
	if (m_BladeEnergy) {
		// An energy blade burns what's held against it, even still.
		if (MOID hitID = g_SceneMan.CastMORay(start, bladeVec, ignoreMOIDs, ignoreTeam, 0, true, 0); hitID != g_NoMOID) {
			addHit(hitID, g_SceneMan.GetLastRayHitPos(), Vector());
		}
		if (terrainBites > 0 && m_BladeTerrainTimer.IsPastSimMS(70)) {
			m_BladeTerrainTimer.Reset();
			EffectsParticles::Emit("Smoke", end, Vector(0.0F, -1.0F), 0.4F, 1, 0);
		}
	}
	for (const BladeHit& hit: hits) {
		CutWithBlade(hit.ID, hit.Pos, hit.Vel, holder, attacking);
	}
	if (struckTerrain && m_BladeTerrainTimer.IsPastSimMS(90)) {
		m_BladeTerrainTimer.Reset();
		BladeStruck(terrainStrikePos, terrainStrikeVel * (-GetBladeMass() * 0.8F), false, terrainStrikeMetal);
	}

	if (BladeDeflects()) {
		DeflectShots(start, start + bladeVec, holder);
	}

	m_BladePreviousValid = true;
	m_BladePreviousStart = start;
	m_BladePreviousEnd = start + bladeVec;
	m_BladePreviousRootID = rootID;
}

bool HeldDevice::CutWithBlade(MOID hitMOID, const Vector& hitPos, const Vector& bladeVel, MovableObject* holder, bool attacking) {
	MovableObject* hitMO = g_MovableMan.GetMOFromID(hitMOID);
	if (!hitMO) {
		return false;
	}
	MovableObject* hitRoot = hitMO->GetRootParent();
	if (hitRoot == holder || hitMO == this) {
		return false;
	}
	// Blades meet blades in the clash, not here.
	if (const HeldDevice* hitDevice = dynamic_cast<const HeldDevice*>(hitMO); hitDevice && hitDevice->HasBlade() && hitDevice->GetBladeExtension() > 0.0F) {
		return false;
	}
	double now = static_cast<double>(g_TimerMan.GetSimTimeMS());
	if (auto lastCut = m_BladeLastCut.find(hitRoot->GetUniqueID()); lastCut != m_BladeLastCut.end() && now - lastCut->second < GetBladeHitInterval()) {
		return false;
	}

	Vector relativeVel = bladeVel - hitMO->GetVel();
	if (relativeVel.MagnitudeIsLessThan(GetBladeMinSpeed())) {
		Vector inward = g_SceneMan.ShortestDistance(hitPos, hitMO->GetPos(), g_SceneMan.SceneWrapsX());
		Vector direction = relativeVel.MagnitudeIsGreaterThan(0.5F) ? relativeVel : (inward.MagnitudeIsGreaterThan(0.5F) ? inward : Vector(1.0F, 0.0F));
		relativeVel = direction.GetNormalized() * GetBladeMinSpeed();
	}
	// A physical blade has to be moving to cut; laid against someone it does nothing.
	if (relativeVel.MagnitudeIsLessThan(3.0F)) {
		return false;
	}

	if (m_BladeLastCut.size() > 64) {
		m_BladeLastCut.clear();
	}
	m_BladeLastCut[hitRoot->GetUniqueID()] = now;
	m_BladeHitTimer.Reset();

	// The cut itself is a heavy, sharp particle at the blade's speed, so it hurts, wounds, penetrates armour and knocks like any other hit.
	static unsigned char s_CutMaterial = g_SceneMan.GetMaterial("Bullet Metal") ? g_SceneMan.GetMaterial("Bullet Metal")->GetIndex() : g_MaterialAir;
	Vector direction = relativeVel.GetNormalized();
	Color cutColor = m_BladeEnergy ? m_BladeColor : Color(200, 200, 200);
	MOPixel* cut = new MOPixel(cutColor, GetBladeMass(), hitPos - direction * 3.0F, hitMO->GetVel() + relativeVel, new Atom(Vector(), s_CutMaterial, nullptr, cutColor, 0), 60);
	cut->SetSharpness(GetBladeSharpness());
	cut->SetToHitMOs(true);
	cut->SetToGetHitByMOs(false);
	cut->SetWhichMOToNotHit(holder, -1.0F);
	cut->SetTeam(holder->GetTeam());
	cut->SetIgnoresTeamHits(holder->IgnoresTeamHits());
	cut->SetGlobalAccScalar(0.0F);
	g_MovableMan.AddParticle(cut);

	if (MOSprite* hitSprite = dynamic_cast<MOSprite*>(hitMO); hitSprite && m_BladeEnergy) {
		hitMO->AddHeatAt(hitSprite->UnRotateOffset(g_SceneMan.ShortestDistance(hitMO->GetPos(), hitPos, g_SceneMan.SceneWrapsX())), 0.6F, 4.0F);
	}
	if (m_BladeEnergy) {
		EffectsParticles::Emit("Sparks", hitPos, direction * -4.0F, 0.7F, 5, ColorToRGB(m_BladeColor));
		EffectsParticles::Emit("Smoke", hitPos, Vector(0.0F, -1.0F), 0.5F, 1, 0);
		g_PostProcessMan.RegisterLight(hitPos, glm::vec3(255.0F, 215.0F, 150.0F), 30.0F, 1.2F, LightSource::Objects);
	} else {
		// A physical blade gives up some of its speed to what it cuts into, and strikes sparks off armour.
		AddImpulseForce(relativeVel * (-GetBladeMass() * 0.35F));
		if (IsMetalForBlades(hitMO->GetMaterial())) {
			EffectsParticles::Emit("Sparks", hitPos, direction * -4.0F, 0.7F, 3, 0xFFE0A0);
		}
	}
	if (m_BladeHitSound) {
		m_BladeHitSound->Play(hitPos);
	}

	// Fast cuts through a limb take it off.
	if (BladeSevers() && attacking && relativeVel.MagnitudeIsGreaterThan(10.0F) && hitMO != hitRoot) {
		Attachable* limb = dynamic_cast<Attachable*>(hitMO);
		const AHuman* human = dynamic_cast<const AHuman*>(hitRoot);
		bool isHead = human && limb == human->GetHead();
		if (limb && limb->IsAttached() && (dynamic_cast<Arm*>(limb) || dynamic_cast<Leg*>(limb) || isHead) && RandomNum() < (isHead ? 0.25F : 0.4F)) {
			if (MOSRotating* limbParent = dynamic_cast<MOSRotating*>(limb->GetParent())) {
				limbParent->RemoveAttachable(limb, true, true);
				if (m_BladeEnergy) {
					EffectsParticles::Emit("Sparks", hitPos, Vector(), 1.0F, 8, ColorToRGB(m_BladeColor));
				}
			}
		}
	}
	return true;
}

void HeldDevice::BladeStruck(const Vector& where, const Vector& push, bool clash, bool metal) {
	if (clash) {
		m_BladeClashTimer.Reset();
	}
	AddImpulseForce(push);
	// Steel on steel sparks and rings; steel on dirt or rock just stops. An energy blade flares on anything.
	if (m_BladeEnergy || clash || metal) {
		unsigned int sparkColor = m_BladeEnergy ? ColorToRGB(m_BladeColor) : 0xFFE0A0;
		EffectsParticles::Emit("Sparks", where, push.GetNormalized() * 6.0F, 0.9F, clash ? 10 : 5, sparkColor);
		if (m_BladeEnergy || clash) {
			g_PostProcessMan.RegisterLight(where, m_BladeEnergy ? glm::vec3(255.0F, 240.0F, 215.0F) : glm::vec3(255.0F, 200.0F, 130.0F), clash ? 55.0F : 30.0F, clash ? 2.2F : 1.0F, LightSource::Objects);
		}
		if (m_BladeClashSound) {
			m_BladeClashSound->Play(where);
		}
	}
	// A swing that strikes something it can't go through stops there.
	if (m_MeleeSwingPhase == MeleeSwingPhase::Strike || m_MeleeSwingPhase == MeleeSwingPhase::WindUp) {
		m_MeleeSwingPhase = MeleeSwingPhase::Recover;
		m_MeleeSwingRecoverFrom = m_MeleeSwingAngle;
		m_MeleeSwingTimer.Reset();
	}
}

void HeldDevice::DeflectShots(const Vector& start, const Vector& end, MovableObject* holder) {
	Vector bladeVec = g_SceneMan.ShortestDistance(start, end, g_SceneMan.SceneWrapsX());
	float halfLength = bladeVec.GetMagnitude() * 0.5F;
	float deltaPixels = c_PPM * g_TimerMan.GetDeltaTimeSecs();
	for (MovableObject* particle: g_MovableMan.GetParticleList()) {
		if (!particle->HitsMOs() || particle->IsSetToDelete() || particle->GetTeam() == holder->GetTeam() || particle->GetVel().MagnitudeIsLessThan(25.0F)) {
			continue;
		}
		Vector step = particle->GetVel() * deltaPixels;
		// Relative to the blade's base: where it was last update, to where it'll be next.
		Vector now = g_SceneMan.ShortestDistance(start, particle->GetPos(), g_SceneMan.SceneWrapsX());
		if ((now - bladeVec * 0.5F).MagnitudeIsGreaterThan(halfLength + step.GetMagnitude() * 2.0F + 2.0F)) {
			continue;
		}
		float alongPath = 0.0F;
		if (!SegmentsCross(now - step, now + step, Vector(), bladeVec, alongPath)) {
			continue;
		}
		Vector crossing = start + (now - step) + step * 2.0F * alongPath;
		// Back where it came from, roughly: the blade's edge turns it, and a lightsaber sends it back at its shooter.
		Vector back = particle->GetVel() * -1.0F;
		back.RadRotate(RandomNormalNum() * 0.35F);
		Vector bladeNormal = Vector(-bladeVec.m_Y, bladeVec.m_X).GetNormalized();
		if (bladeNormal.Dot(particle->GetVel()) > 0.0F) {
			bladeNormal *= -1.0F;
		}
		particle->SetPos(crossing + bladeNormal * 2.0F);
		particle->SetVel(back * 0.9F);
		particle->SetWhichMOToNotHit(holder, 0.25F);
		particle->SetTeam(holder->GetTeam());
		AddImpulseForce(particle->GetVel() * (-particle->GetMass() * 0.5F));
		EffectsParticles::Emit("Sparks", crossing, bladeNormal * 5.0F, 0.8F, 4, m_BladeEnergy ? ColorToRGB(m_BladeColor) : 0xFFE0A0);
		g_PostProcessMan.RegisterLight(crossing, glm::vec3(255.0F, 230.0F, 200.0F), 22.0F, 1.0F, LightSource::Objects);
		if (m_BladeClashSound) {
			m_BladeClashSound->Play(crossing);
		}
	}
}
