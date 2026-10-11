#include "Turret.h"

#include "HeldDevice.h"
#include "HDFirearm.h"
#include "FrameMan.h"
#include "PostProcessMan.h"
#include "SceneMan.h"
#include "GUI.h"
#include "AllegroBitmap.h"
#include "PresetMan.h"

using namespace RTE;

ConcreteClassInfo(Turret, Attachable, 20);

Turret::Turret() {
	Clear();
}

Turret::~Turret() {
	Destroy(true);
}

void Turret::Clear() {
	m_MountedDevices.clear();
	m_MountedDeviceRotationOffset = 0;
}

int Turret::Create(const Turret& reference) {
	if (!reference.m_MountedDevices.empty()) {
		for (const HeldDevice* referenceMountedDevice: reference.m_MountedDevices) {
			m_ReferenceHardcodedAttachableUniqueIDs.insert(referenceMountedDevice->GetUniqueID());
			AddMountedDevice(dynamic_cast<HeldDevice*>(referenceMountedDevice->Clone()));
		}
	}
	Attachable::Create(reference);

	m_MountedDeviceRotationOffset = reference.m_MountedDeviceRotationOffset;

	return 0;
}

void Turret::Destroy(bool notInherited) {
	if (!notInherited) {
		Attachable::Destroy();
	}
	for (const HeldDevice* mountedDevice: m_MountedDevices) {
		m_HardcodedAttachableUniqueIDsAndRemovers.erase(mountedDevice->GetUniqueID());
	}
	Clear();
}

int Turret::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Attachable::ReadProperty(propName, reader));

	MatchProperty("MountedDevice", { SetFirstMountedDevice(dynamic_cast<HeldDevice*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("AddMountedDevice", { AddMountedDevice(dynamic_cast<HeldDevice*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("MountedDeviceRotationOffset", { reader >> m_MountedDeviceRotationOffset; });

	EndPropertyList;
}

int Turret::Save(Writer& writer) const {
	Attachable::Save(writer);

	for (const HeldDevice* mountedDevice: m_MountedDevices) {
		writer.NewProperty("AddMountedDevice");
		writer << mountedDevice;
	}
	writer.NewProperty("MountedDeviceRotationOffset");
	writer << m_MountedDeviceRotationOffset;

	return 0;
}

void Turret::SetFirstMountedDevice(HeldDevice* newMountedDevice) {
	if (HasMountedDevice()) {
		RemoveAndDeleteAttachable(m_MountedDevices[0]);
	}
	if (newMountedDevice != nullptr) {
		m_MountedDevices.emplace(m_MountedDevices.begin(), newMountedDevice);
		AddAttachable(newMountedDevice);

		m_HardcodedAttachableUniqueIDsAndRemovers.insert({newMountedDevice->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
			                                                  HeldDevice* castedAttachable = dynamic_cast<HeldDevice*>(attachable);
			                                                  RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to RemoveMountedDevice.");
			                                                  dynamic_cast<Turret*>(parent)->RemoveMountedDevice(castedAttachable);
		                                                  }});

		m_MountedDevices[0]->SetInheritsRotAngle(false);
		m_MountedDevices[0]->SetUnPickupable(true);
		m_MountedDevices[0]->SetGibWithParentChance(1.0F);
		// Force weapons mounted on turrets to never be removed due to forces. This doesn't affect them gibbing from hitting their impulse limits though.
		m_MountedDevices[0]->SetJointStrength(0.0F);
		m_MountedDevices[0]->SetSupportable(true);
		m_MountedDevices[0]->SetSupportAvailable(true);
	}
}

void Turret::AddMountedDevice(HeldDevice* newMountedDevice) {
	if (newMountedDevice == nullptr) {
		return;
	}
	m_MountedDevices.emplace_back(newMountedDevice);
	AddAttachable(newMountedDevice);

	m_HardcodedAttachableUniqueIDsAndRemovers.insert({newMountedDevice->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                  HeldDevice* castedAttachable = dynamic_cast<HeldDevice*>(attachable);
		                                                  RTEAssert(!attachable || castedAttachable, "Tried to pass incorrect Attachable subtype " + (attachable ? attachable->GetClassName() : "") + " to RemoveMountedDevice.");
		                                                  dynamic_cast<Turret*>(parent)->RemoveMountedDevice(castedAttachable);
	                                                  }});

	newMountedDevice->SetInheritsRotAngle(true);
	newMountedDevice->SetUnPickupable(true);
	newMountedDevice->SetGibWithParentChance(1.0F);
	// Force weapons mounted on turrets to never be removed due to forces. This doesn't affect them gibbing from hitting their impulse limits though.
	newMountedDevice->SetJointStrength(0.0F);
	newMountedDevice->SetSupportable(true);
	newMountedDevice->SetSupportAvailable(true);
}

void Turret::Update() {
	for (HeldDevice* mountedDevice: m_MountedDevices) {
		mountedDevice->SetMountedRotAngleOffset(m_MountedDeviceRotationOffset);
	}
	Attachable::Update();
}

void Turret::Draw(BITMAP* pTargetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const {
	Attachable::Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
	// TODO replace this with a relative draw order property or something that lets you organize attachable drawing so it doesn't need special hardcoding crap. Use this for ahuman limbs and arm held mo if possible.
	for (HeldDevice* mountedDevice: m_MountedDevices) {
		if (mountedDevice->IsDrawnAfterParent()) {
			mountedDevice->Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
		}
	}
}

void Turret::Draw(const Camera& camera) const {
	// (The mounted guns are among the attachables, so they're drawn with it.)
	Attachable::Draw(camera);
}

void Turret::DrawGunnerHUD(BITMAP* pTargetBitmap, const Vector& targetPos, const Vector& drawPos, int& hudStack) const {
	GUIFont* symbolFont = g_FrameMan.GetLargeFont();
	GUIFont* smallFont = g_FrameMan.GetSmallFont();
	std::string textString;
	for (const HeldDevice* mountedDevice: m_MountedDevices) {
		const HDFirearm* mountedFirearm = dynamic_cast<const HDFirearm*>(mountedDevice);
		if (!mountedFirearm) {
			continue;
		}
		// The reticle: a few glowing dots out along the barrel from the muzzle (vehicle guns have no sharp aim, so the firearm's own reticle never shows).
		if (!mountedFirearm->IsReloading()) {
			int glowStrength = RandomNum(95, 159);
			for (int i = 0; i < 4; ++i) {
				Vector aimPoint = mountedFirearm->GetMuzzlePos() + mountedFirearm->RotateOffset(Vector(36.0F + 8.0F * static_cast<float>(i), 0.0F));
				g_PostProcessMan.RegisterGlowDotEffect(aimPoint, YellowDot, glowStrength);
				aimPoint -= targetPos;
				g_SceneMan.WrapPosition(aimPoint);
				putpixel(pTargetBitmap, aimPoint.GetFloorIntX(), aimPoint.GetFloorIntY(), g_YellowGlowColor);
			}
		}
		// Rounds left, or a reload bar, as a crab's turret shows them.
		if (!textString.empty()) {
			textString += " | ";
		}
		int totalTextWidth = smallFont ? smallFont->CalculateWidth(textString) : 0;
		if (mountedFirearm->IsReloading()) {
			textString += "Reloading";
			rectfill(pTargetBitmap, drawPos.GetFloorIntX() + 1 + totalTextWidth, drawPos.GetFloorIntY() + hudStack + 13, drawPos.GetFloorIntX() + 29 + totalTextWidth, drawPos.GetFloorIntY() + hudStack + 14, 245);
			rectfill(pTargetBitmap, drawPos.GetFloorIntX() + totalTextWidth, drawPos.GetFloorIntY() + hudStack + 12, drawPos.GetFloorIntX() + static_cast<int>(28.0F * mountedFirearm->GetReloadProgress() + 0.5F) + totalTextWidth, drawPos.GetFloorIntY() + hudStack + 13, 77);
		} else {
			textString += mountedFirearm->GetRoundInMagCount() < 0 ? "Infinite" : std::to_string(mountedFirearm->GetRoundInMagCount());
		}
	}
	if (!textString.empty() && symbolFont && smallFont) {
		AllegroBitmap allegroBitmap(pTargetBitmap);
		char str[2] = {-56, 0};
		symbolFont->DrawAligned(&allegroBitmap, drawPos.GetFloorIntX() - 10, drawPos.GetFloorIntY() + hudStack, str, GUIFont::Left);
		smallFont->DrawAligned(&allegroBitmap, drawPos.GetFloorIntX(), drawPos.GetFloorIntY() + hudStack + 3, textString, GUIFont::Left);
		hudStack -= 9;
	}
}

void Turret::SetParent(MOSRotating* newParent) {
	Attachable::SetParent(newParent);
	for (HeldDevice* mountedDevice: m_MountedDevices) {
		mountedDevice->Deactivate();
	}
}

void Turret::RemoveMountedDevice(const HeldDevice* mountedDeviceToRemove) {
	std::vector<HeldDevice*>::iterator mountedDeviceIterator = std::find_if(m_MountedDevices.begin(), m_MountedDevices.end(), [&mountedDeviceToRemove](const HeldDevice* mountedDevice) {
		return mountedDevice == mountedDeviceToRemove;
	});
	m_MountedDevices.erase(mountedDeviceIterator);
}
