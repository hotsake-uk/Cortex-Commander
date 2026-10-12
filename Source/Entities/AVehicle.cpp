#include "AVehicle.h"

#include "ACShip.h"
#include "AEmitter.h"
#include "AHuman.h"
#include "ActivityMan.h"
#include "Atom.h"
#include "AtomGroup.h"
#include "Attachable.h"
#include "Controller.h"
#include "FluidSim.h"
#include "TerrainTrees.h"
#include "FrameMan.h"
#include "MovableMan.h"
#include "PieSlice.h"
#include "PresetMan.h"
#include "PrimitiveMan.h"
#include "Sandbox.h"
#include "SceneMan.h"
#include "SettingsMan.h"
#include "SLTerrain.h"
#include "SoundContainer.h"
#include "TimerMan.h"
#include "Turret.h"

#include "HeldDevice.h"
#include "HDFirearm.h"

#include "GUI.h"
#include "AllegroBitmap.h"

#include "tracy/Tracy.hpp"

#include <algorithm>
#include <array>
#include <cmath>

using namespace RTE;

ConcreteClassInfo(AVehicle, Actor, 20);

namespace {
	/// Whether a friend of a team's is the first thing a shot along a line would hit (its own hull and crew aside, and stopping at the ground):
	/// a gunner holds fire rather than put a round into the vehicle or squad beside it on the way to the enemy.
	bool FriendInLineOfFire(const RTE::MovableObject* self, int team, const RTE::Vector& from, const RTE::Vector& ray) {
		RTE::MOID hit = RTE::g_SceneMan.CastMORay(from, ray, self->GetRootID(), RTE::Activity::NoTeam, 0, false, 2);
		if (hit == RTE::g_NoMOID) {
			return false;
		}
		const RTE::MovableObject* part = RTE::g_MovableMan.GetMOFromID(hit);
		const RTE::Actor* actor = part ? dynamic_cast<const RTE::Actor*>(RTE::g_MovableMan.GetMOFromID(part->GetRootID())) : nullptr;
		return actor && actor->GetTeam() == team && !actor->IsDead();
	}

	/// Whether a terrain material is a plant: the base game's plants, cacti and mushrooms, and trees' leaves (not the grass on topsoil, which
	/// is ground). A wheel rolls through plants, crushing them, rather than up over them as if they were rock.
	bool IsPlant(int material) {
		static std::array<signed char, 256> s_Plant{}; // 0 not looked at yet, 1 a plant, -1 not.
		unsigned char id = static_cast<unsigned char>(material);
		if (s_Plant[id] == 0) {
			const std::string& name = g_SceneMan.GetMaterialFromID(id)->GetPresetName();
			bool plant = name.find("Vegetation") != std::string::npos || name.find("Leaf") != std::string::npos || name.find("Leaves") != std::string::npos ||
			             name.find("Foliage") != std::string::npos || name.find("Plant") != std::string::npos;
			s_Plant[id] = plant ? 1 : -1;
		}
		return s_Plant[id] > 0;
	}

	/// Whether the ground at a point holds a wheel up: anything but air, liquid (a wheel sinks through water to the bottom), plants, and trees
	/// while vehicles don't bump into them.
	bool HoldsWheel(const Vector& point) {
		int material = g_SceneMan.GetTerrMatter(point.GetFloorIntX(), point.GetFloorIntY());
		return material != g_MaterialAir && !FluidSim::IsLiquid(material) && !IsPlant(material) && !TerrainTrees::ActorsPass(material);
	}

	/// Crushes the plants under a wheel: every plant pixel inside its circle goes.
	void CrushPlants(const Vector& centre, float radius) {
		SLTerrain* terrain = g_SceneMan.GetTerrain();
		if (!terrain) {
			return;
		}
		int reach = static_cast<int>(std::ceil(radius));
		int centreX = centre.GetFloorIntX();
		int centreY = centre.GetFloorIntY();
		for (int dy = -reach; dy <= reach; ++dy) {
			for (int dx = -reach; dx <= reach; ++dx) {
				if (static_cast<float>(dx * dx + dy * dy) > radius * radius) {
					continue;
				}
				int x = centreX + dx;
				int y = centreY + dy;
				if (!g_SceneMan.WrapPosition(x, y) && !g_SceneMan.IsWithinBounds(x, y)) {
					continue;
				}
				// (Not a tree's leaves while vehicles drive through trees: the tree is left whole.)
				if (int material = terrain->GetMaterialPixel(x, y); IsPlant(material) && !TerrainTrees::ActorsPass(material)) {
					terrain->SetMaterialPixel(x, y, g_MaterialAir);
					terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
				}
			}
		}
	}

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

AVehicle::AVehicle() {
	m_Crew.SetHost(this);
	Clear();
}

AVehicle::~AVehicle() {
	Destroy(true);
}

void AVehicle::Clear() {
	m_Wheels.clear();
	m_SuspensionTravel = 6.0F;
	m_SuspensionStiffness = 80.0F;
	m_SuspensionDamping = 9.0F;
	m_Acceleration = 4.0F;
	m_BrakeStrength = 9.0F;
	m_RollingResistance = 0.25F;
	m_MaxSpeed = 7.0F;
	m_Grip = 0.8F;
	m_NeedsDriver = true;
	m_CrewStaysIn = false;
	m_TerrainCrush = 0.0F;
	m_HopSpeed = 0.0F;
	m_HopTimer.Reset();
	m_HullPoints.clear();
	m_HullDraft = 10.0F;
	m_WaterThrust = 0.0F;
	m_WaterMaxSpeed = 4.0F;
	m_WaterDrag = 0.4F;
	m_RowingStroke = 0.0F;
	m_PlaningTrim = 0.0F;
	m_PropellerOffset.Reset();
	m_HasPropeller = false;
	m_Oar = nullptr;
	m_OarSweep = 0.6F;
	m_StrokePhase = 0.0F;
	m_Submerged = 0.0F;
	m_WakeTimer.Reset();
	m_EngineSound = nullptr;
	m_Exhaust = nullptr;
	m_EngineRunning = false;
	m_EngineLoad = 0.0F;
	m_Crew.Clear();
	m_Turret = nullptr;
	m_GunRange = 600.0F;
	m_GunTurnSpeed = 2.5F;
	m_AimRangeUp = -1.0F;
	m_GunTargetID = 0;
	m_GunTargetTimer.Reset();
	m_CrewThrowSpeed = 14.0F;
	m_ThrowsCrewWhenFlipped = true;
	m_Tracks = nullptr;
	m_TrackFrameLength = 2.0F;
	m_TrackTravel = 0.0F;
	m_ThrowingArm = nullptr;
	m_ArmLength = 30.0F;
	m_ArmThrowAngle = 1.9F;
	m_LaunchSpeed = 25.0F;
	m_LaunchReloadTime = 4000.0F;
	m_MinLaunchAngle = 0.25F;
	m_MaxLaunchAngle = 1.3F;
	m_LaunchAmmo.clear();
	m_LaunchAmmoIndex = 0;
	m_LaunchPhase = LaunchPhase::Loaded;
	m_LaunchTimer.Reset();
	m_Released = false;
	m_AmmoSwitchTimer.Reset();
	m_AIThrows = 0;
	m_Recruits.clear();
	m_RecruitRange = 300.0F;
	m_RecruitTimer.Reset();
	m_OrderedBoardTimer.Reset();
	m_AIStuckTimer.Reset();
	m_AIReverseTimer.Reset();
	m_AIStuckCount = 0;
	m_AIReversing = false;
	m_AIHop = false;
	m_AIWantsStop = false;
	m_AIGaveUp = false;
	m_AIGaveUpTimer.Reset();
	m_AIRiderWaitTimer.Reset();
	m_Commanded = false;
	m_AIRoutesAsked = 0;
	m_AIFaceTimer.Reset();
	m_AIOrderSerialSeen = 0;
	m_Throttle = 0.0F;
	m_Braking = false;
	m_Handbrake = false;
	m_HandbrakeHeldAgainst = false;
	m_UpsideDownTimer.Reset();
	m_Buoyancy = 0.8F;
	m_BreakLandingSpeed = 0.0F;
	m_BreakSunkFraction = 0.0F;
	m_BreakNow = false;
	m_SunkTimer.Reset();
	m_AIManoeuvring = false;
	m_AIManoeuvreX = 0.0F;
	m_AIManoeuvreLimitMS = 4000;
	m_AIKeepFacing = false;
	m_AICombatHealth = -1.0F;
}

int AVehicle::Create(const AVehicle& reference) {
	for (const Wheel& wheel: reference.m_Wheels) {
		if (wheel.Part) {
			m_ReferenceHardcodedAttachableUniqueIDs.insert(wheel.Part->GetUniqueID());
		}
		if (wheel.Strut) {
			m_ReferenceHardcodedAttachableUniqueIDs.insert(wheel.Strut->GetUniqueID());
		}
	}

	if (reference.m_Oar) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_Oar->GetUniqueID());
	}
	if (reference.m_Exhaust) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_Exhaust->GetUniqueID());
	}
	if (reference.m_Turret) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_Turret->GetUniqueID());
	}
	if (reference.m_Tracks) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_Tracks->GetUniqueID());
	}
	if (reference.m_ThrowingArm) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(reference.m_ThrowingArm->GetUniqueID());
	}

	Actor::Create(reference);

	if (reference.m_Oar) {
		SetOar(dynamic_cast<Attachable*>(reference.m_Oar->Clone()));
	}
	if (reference.m_Exhaust) {
		SetExhaust(dynamic_cast<AEmitter*>(reference.m_Exhaust->Clone()));
	}
	if (reference.m_EngineSound) {
		m_EngineSound = dynamic_cast<SoundContainer*>(reference.m_EngineSound->Clone());
	}
	if (reference.m_Turret) {
		SetTurret(dynamic_cast<Turret*>(reference.m_Turret->Clone()));
	}
	m_Crew.Create(reference.m_Crew);
	m_GunRange = reference.m_GunRange;
	m_GunTurnSpeed = reference.m_GunTurnSpeed;
	m_AimRangeUp = reference.m_AimRangeUp;
	m_CrewThrowSpeed = reference.m_CrewThrowSpeed;
	m_ThrowsCrewWhenFlipped = reference.m_ThrowsCrewWhenFlipped;
	m_RecruitRange = reference.m_RecruitRange;
	for (const Wheel& wheel: reference.m_Wheels) {
		// (A bare strut whose wheel was shot off isn't copied.)
		if (!wheel.Part) {
			continue;
		}
		AddWheel(dynamic_cast<Attachable*>(wheel.Part->Clone()));
		m_Wheels.back().Mount = wheel.Mount;
		m_Wheels.back().Part->SetParentOffset(wheel.Mount);
		if (wheel.Strut) {
			AddStrut(dynamic_cast<Attachable*>(wheel.Strut->Clone()));
			m_Wheels.back().Strut->SetParentOffset(wheel.Mount);
		}
	}
	if (reference.m_Tracks) {
		SetTracks(dynamic_cast<Attachable*>(reference.m_Tracks->Clone()));
	}
	m_TrackFrameLength = reference.m_TrackFrameLength;
	if (reference.m_ThrowingArm) {
		SetThrowingArm(dynamic_cast<Attachable*>(reference.m_ThrowingArm->Clone()));
	}
	m_ArmLength = reference.m_ArmLength;
	m_ArmThrowAngle = reference.m_ArmThrowAngle;
	m_LaunchSpeed = reference.m_LaunchSpeed;
	m_LaunchReloadTime = reference.m_LaunchReloadTime;
	m_MinLaunchAngle = reference.m_MinLaunchAngle;
	m_MaxLaunchAngle = reference.m_MaxLaunchAngle;
	for (const MovableObject* ammo: reference.m_LaunchAmmo) {
		m_LaunchAmmo.push_back(dynamic_cast<MovableObject*>(ammo->Clone()));
	}
	m_LaunchAmmoIndex = reference.m_LaunchAmmoIndex;
	m_SuspensionTravel = reference.m_SuspensionTravel;
	m_SuspensionStiffness = reference.m_SuspensionStiffness;
	m_SuspensionDamping = reference.m_SuspensionDamping;
	m_Acceleration = reference.m_Acceleration;
	m_BrakeStrength = reference.m_BrakeStrength;
	m_Handbrake = reference.m_Handbrake;
	m_RollingResistance = reference.m_RollingResistance;
	m_MaxSpeed = reference.m_MaxSpeed;
	m_Grip = reference.m_Grip;
	m_NeedsDriver = reference.m_NeedsDriver;
	m_CrewStaysIn = reference.m_CrewStaysIn;
	// (A vehicle read from a saved game is copied into the scene: commanded as one unit, it still is.)
	m_Commanded = reference.m_Commanded;
	m_TerrainCrush = reference.m_TerrainCrush;
	m_HopSpeed = reference.m_HopSpeed;
	m_HullPoints = reference.m_HullPoints;
	m_HullDraft = reference.m_HullDraft;
	m_WaterThrust = reference.m_WaterThrust;
	m_WaterMaxSpeed = reference.m_WaterMaxSpeed;
	m_WaterDrag = reference.m_WaterDrag;
	m_RowingStroke = reference.m_RowingStroke;
	m_PlaningTrim = reference.m_PlaningTrim;
	m_PropellerOffset = reference.m_PropellerOffset;
	m_HasPropeller = reference.m_HasPropeller;
	m_OarSweep = reference.m_OarSweep;
	m_Buoyancy = reference.m_Buoyancy;
	m_BreakLandingSpeed = reference.m_BreakLandingSpeed;
	m_BreakSunkFraction = reference.m_BreakSunkFraction;

	return 0;
}

void AVehicle::Destroy(bool notInherited) {
	m_Crew.DeleteCrew();
	if (m_Turret) {
		m_HardcodedAttachableUniqueIDsAndRemovers.erase(m_Turret->GetUniqueID());
	}
	if (m_Tracks) {
		m_HardcodedAttachableUniqueIDsAndRemovers.erase(m_Tracks->GetUniqueID());
	}
	if (m_ThrowingArm) {
		m_HardcodedAttachableUniqueIDsAndRemovers.erase(m_ThrowingArm->GetUniqueID());
	}
	for (MovableObject* ammo: m_LaunchAmmo) {
		delete ammo;
	}
	m_LaunchAmmo.clear();
	for (const Wheel& wheel: m_Wheels) {
		if (wheel.Part) {
			m_HardcodedAttachableUniqueIDsAndRemovers.erase(wheel.Part->GetUniqueID());
		}
		if (wheel.Strut) {
			m_HardcodedAttachableUniqueIDsAndRemovers.erase(wheel.Strut->GetUniqueID());
		}
	}
	if (m_Oar) {
		m_HardcodedAttachableUniqueIDsAndRemovers.erase(m_Oar->GetUniqueID());
	}
	if (m_Exhaust) {
		m_HardcodedAttachableUniqueIDsAndRemovers.erase(m_Exhaust->GetUniqueID());
	}
	if (m_EngineSound) {
		m_EngineSound->Stop();
	}
	delete m_EngineSound;
	if (!notInherited) {
		Actor::Destroy();
	}
	Clear();
}

int AVehicle::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Actor::ReadProperty(propName, reader));

	MatchProperty("AddWheel", { AddWheel(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("AddStrut", { AddStrut(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("SuspensionTravel", { reader >> m_SuspensionTravel; });
	MatchProperty("SuspensionStiffness", { reader >> m_SuspensionStiffness; });
	MatchProperty("SuspensionDamping", { reader >> m_SuspensionDamping; });
	MatchProperty("Acceleration", { reader >> m_Acceleration; });
	MatchProperty("BrakeStrength", { reader >> m_BrakeStrength; });
	MatchProperty("Handbrake", { reader >> m_Handbrake; });
	MatchProperty("RollingResistance", { reader >> m_RollingResistance; });
	MatchProperty("MaxSpeed", { reader >> m_MaxSpeed; });
	MatchProperty("Grip", { reader >> m_Grip; });
	MatchProperty("SeatOffset", {
		Vector offset;
		reader >> offset;
		m_Crew.SetMainSeatOffset(offset);
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
	MatchProperty("NeedsDriver", { reader >> m_NeedsDriver; });
	MatchProperty("CrewStaysIn", { reader >> m_CrewStaysIn; });
	MatchProperty("TerrainCrush", { reader >> m_TerrainCrush; });
	MatchProperty("HopSpeed", { reader >> m_HopSpeed; });
	MatchProperty("AddHullPoint", {
		Vector point;
		reader >> point;
		AddHullPoint(point);
	});
	MatchProperty("HullDraft", { reader >> m_HullDraft; });
	MatchProperty("WaterThrust", { reader >> m_WaterThrust; });
	MatchProperty("WaterMaxSpeed", { reader >> m_WaterMaxSpeed; });
	MatchProperty("WaterDrag", { reader >> m_WaterDrag; });
	MatchProperty("RowingStroke", { reader >> m_RowingStroke; });
	MatchProperty("PlaningTrim", { reader >> m_PlaningTrim; });
	MatchProperty("PropellerOffset", {
		reader >> m_PropellerOffset;
		m_HasPropeller = true;
	});
	MatchProperty("Oar", { SetOar(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("OarSweep", { reader >> m_OarSweep; });
	MatchProperty("EngineSound", {
		delete m_EngineSound;
		m_EngineSound = new SoundContainer;
		reader >> m_EngineSound;
		m_EngineSound->SetDefaultMixCategory(AudioMan::MIX_ENGINES);
	});
	MatchProperty("Exhaust", { SetExhaust(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("AddSeat") MatchProperty("AddGunnerSeat", {
		Vector offset;
		reader >> offset;
		m_Crew.AddSeat(offset, propName == "AddGunnerSeat");
	});
	MatchProperty("Turret", { SetTurret(dynamic_cast<Turret*>(g_PresetMan.ReadReflectedPreset(reader))); });
	// A saved game's (Scene::SaveSceneObject): where the wheel just added is fixed, and the vehicle's state and crew as they were.
	MatchProperty("SpecialBehaviour_WheelMount", {
		Vector mount;
		reader >> mount;
		if (!m_Wheels.empty() && m_Wheels.back().Part) {
			m_Wheels.back().Mount = mount;
			m_Wheels.back().Part->SetParentOffset(mount);
		}
	});
	MatchProperty("SpecialBehaviour_Commanded", { reader >> m_Commanded; });
	MatchForwards("SpecialBehaviour_CrewSeat") MatchProperty("SpecialBehaviour_RiderSeat", {
		int seat;
		reader >> seat;
		m_Crew.SetSeatToLoad(seat, propName == "SpecialBehaviour_RiderSeat");
	});
	MatchProperty("SpecialBehaviour_Crew", { m_Crew.LoadSeated(dynamic_cast<Actor*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("GunRange", { reader >> m_GunRange; });
	MatchProperty("GunTurnSpeed", { reader >> m_GunTurnSpeed; });
	MatchProperty("AimRangeUp", { reader >> m_AimRangeUp; });
	MatchProperty("CrewThrowSpeed", { reader >> m_CrewThrowSpeed; });
	MatchProperty("ThrowsCrewWhenFlipped", { reader >> m_ThrowsCrewWhenFlipped; });
	MatchProperty("AIRecruitRange", { reader >> m_RecruitRange; });
	MatchProperty("Tracks", { SetTracks(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("TrackFrameLength", { reader >> m_TrackFrameLength; });
	MatchProperty("DrawsCrew", {
		bool drawsCrew;
		reader >> drawsCrew;
		m_Crew.SetDrawsCrew(drawsCrew);
	});
	MatchProperty("PassengersCanFire", {
		bool canFire;
		reader >> canFire;
		m_Crew.SetPassengersCanFire(canFire);
	});
	MatchProperty("ThrowingArm", { SetThrowingArm(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("ArmLength", { reader >> m_ArmLength; });
	MatchProperty("ArmThrowAngle", { reader >> m_ArmThrowAngle; });
	MatchProperty("LaunchSpeed", { reader >> m_LaunchSpeed; });
	MatchProperty("LaunchReloadTime", { reader >> m_LaunchReloadTime; });
	MatchProperty("MinLaunchAngle", { reader >> m_MinLaunchAngle; });
	MatchProperty("MaxLaunchAngle", { reader >> m_MaxLaunchAngle; });
	MatchProperty("AddLaunchAmmo", {
		if (MovableObject* ammo = dynamic_cast<MovableObject*>(g_PresetMan.ReadReflectedPreset(reader))) {
			m_LaunchAmmo.push_back(ammo);
		}
	});
	MatchProperty("Buoyancy", { reader >> m_Buoyancy; });
	MatchProperty("BreakLandingSpeed", { reader >> m_BreakLandingSpeed; });
	MatchProperty("BreakSunkFraction", { reader >> m_BreakSunkFraction; });

	EndPropertyList;
}

int AVehicle::Save(Writer& writer) const {
	Actor::Save(writer);

	for (const Wheel& wheel: m_Wheels) {
		if (!wheel.Part) {
			continue;
		}
		writer.NewProperty("AddWheel");
		writer << wheel.Part;
		if (wheel.Strut) {
			writer.NewProperty("AddStrut");
			writer << wheel.Strut;
		}
	}
	writer.NewPropertyWithValue("SuspensionTravel", m_SuspensionTravel);
	writer.NewPropertyWithValue("SuspensionStiffness", m_SuspensionStiffness);
	writer.NewPropertyWithValue("SuspensionDamping", m_SuspensionDamping);
	writer.NewPropertyWithValue("Acceleration", m_Acceleration);
	writer.NewPropertyWithValue("BrakeStrength", m_BrakeStrength);
	writer.NewPropertyWithValue("Handbrake", m_Handbrake);
	writer.NewPropertyWithValue("RollingResistance", m_RollingResistance);
	writer.NewPropertyWithValue("MaxSpeed", m_MaxSpeed);
	writer.NewPropertyWithValue("Grip", m_Grip);
	writer.NewPropertyWithValue("SeatOffset", m_Crew.GetMainSeatOffset());
	writer.NewPropertyWithValue("ExitOffset", m_Crew.GetExitOffset());
	writer.NewPropertyWithValue("BoardingReach", m_Crew.GetBoardingReach());
	writer.NewPropertyWithValue("NeedsDriver", m_NeedsDriver);
	writer.NewPropertyWithValue("CrewStaysIn", m_CrewStaysIn);
	writer.NewPropertyWithValue("TerrainCrush", m_TerrainCrush);
	writer.NewPropertyWithValue("HopSpeed", m_HopSpeed);
	for (const Vector& point: m_HullPoints) {
		writer.NewPropertyWithValue("AddHullPoint", point);
	}
	writer.NewPropertyWithValue("HullDraft", m_HullDraft);
	writer.NewPropertyWithValue("WaterThrust", m_WaterThrust);
	writer.NewPropertyWithValue("WaterMaxSpeed", m_WaterMaxSpeed);
	writer.NewPropertyWithValue("WaterDrag", m_WaterDrag);
	writer.NewPropertyWithValue("RowingStroke", m_RowingStroke);
	writer.NewPropertyWithValue("PlaningTrim", m_PlaningTrim);
	if (m_HasPropeller) {
		writer.NewPropertyWithValue("PropellerOffset", m_PropellerOffset);
	}
	if (m_Oar) {
		writer.NewProperty("Oar");
		writer << m_Oar;
	}
	writer.NewPropertyWithValue("OarSweep", m_OarSweep);
	if (m_EngineSound) {
		writer.NewProperty("EngineSound");
		writer << m_EngineSound;
	}
	if (m_Exhaust) {
		writer.NewProperty("Exhaust");
		writer << m_Exhaust;
	}
	for (int seat = 1; seat < m_Crew.GetSeatCount(); ++seat) {
		writer.NewPropertyWithValue(m_Crew.IsGunnerSeat(seat) ? "AddGunnerSeat" : "AddSeat", m_Crew.GetSeatOffset(seat));
	}
	if (m_Turret) {
		writer.NewProperty("Turret");
		writer << m_Turret;
	}
	writer.NewPropertyWithValue("GunRange", m_GunRange);
	writer.NewPropertyWithValue("GunTurnSpeed", m_GunTurnSpeed);
	writer.NewPropertyWithValue("AimRangeUp", m_AimRangeUp);
	writer.NewPropertyWithValue("CrewThrowSpeed", m_CrewThrowSpeed);
	writer.NewPropertyWithValue("ThrowsCrewWhenFlipped", m_ThrowsCrewWhenFlipped);
	writer.NewPropertyWithValue("AIRecruitRange", m_RecruitRange);
	if (m_Tracks) {
		writer.NewProperty("Tracks");
		writer << m_Tracks;
	}
	writer.NewPropertyWithValue("TrackFrameLength", m_TrackFrameLength);
	writer.NewPropertyWithValue("DrawsCrew", m_Crew.DrawsCrew());
	writer.NewPropertyWithValue("PassengersCanFire", m_Crew.PassengersCanFire());
	if (m_ThrowingArm) {
		writer.NewProperty("ThrowingArm");
		writer << m_ThrowingArm;
	}
	writer.NewPropertyWithValue("ArmLength", m_ArmLength);
	writer.NewPropertyWithValue("ArmThrowAngle", m_ArmThrowAngle);
	writer.NewPropertyWithValue("LaunchSpeed", m_LaunchSpeed);
	writer.NewPropertyWithValue("LaunchReloadTime", m_LaunchReloadTime);
	writer.NewPropertyWithValue("MinLaunchAngle", m_MinLaunchAngle);
	writer.NewPropertyWithValue("MaxLaunchAngle", m_MaxLaunchAngle);
	for (const MovableObject* ammo: m_LaunchAmmo) {
		writer.NewProperty("AddLaunchAmmo");
		writer << ammo;
	}
	writer.NewPropertyWithValue("Buoyancy", m_Buoyancy);
	writer.NewPropertyWithValue("BreakLandingSpeed", m_BreakLandingSpeed);
	writer.NewPropertyWithValue("BreakSunkFraction", m_BreakSunkFraction);

	return 0;
}

void AVehicle::AddWheel(Attachable* wheel) {
	if (!wheel) {
		return;
	}
	Wheel added;
	added.Part = wheel;
	added.Mount = wheel->GetParentOffset();
	m_Wheels.push_back(added);
	AddAttachable(wheel);

	m_HardcodedAttachableUniqueIDsAndRemovers.insert({wheel->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                  dynamic_cast<AVehicle*>(parent)->RemoveWheel(attachable);
	                                                  }});

	// The wheels turn on their own and ride up and down on their springs (UpdateWheels); the body's atoms are what meet the ground hard.
	wheel->SetInheritsRotAngle(true);
	wheel->SetCollidesWithTerrainWhileAttached(false);
}

void AVehicle::AddStrut(Attachable* strut) {
	if (!strut) {
		return;
	}
	if (m_Wheels.empty() || m_Wheels.back().Strut) {
		// (A strut goes with the wheel before it: one without a wheel of its own has nothing to hang from.)
		delete strut;
		return;
	}
	strut->SetParentOffset(m_Wheels.back().Mount);
	m_Wheels.back().Strut = strut;
	AddAttachable(strut);

	m_HardcodedAttachableUniqueIDsAndRemovers.insert({strut->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                  dynamic_cast<AVehicle*>(parent)->RemoveStrut(attachable);
	                                                  }});

	// Behind the body, so its top slides up out of sight into it as the spring is pushed in; the body's atoms meet the ground, not the strut's.
	strut->SetInheritsRotAngle(true);
	strut->SetCollidesWithTerrainWhileAttached(false);
	strut->SetDrawnAfterParent(false);
}

void AVehicle::SetOar(Attachable* newOar) {
	if (m_Oar && m_Oar->IsAttached()) {
		RemoveAndDeleteAttachable(m_Oar);
	}
	m_Oar = newOar;
	if (!newOar) {
		return;
	}
	AddAttachable(newOar);

	m_HardcodedAttachableUniqueIDsAndRemovers.insert({newOar->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                  AVehicle* vehicle = dynamic_cast<AVehicle*>(parent);
		                                                  if (vehicle->m_Oar == attachable) {
			                                                  vehicle->m_Oar = nullptr;
		                                                  }
	                                                  }});

	// It swings about where it is fixed (UpdateHull); it goes into the water, not the ground.
	newOar->SetInheritsRotAngle(true);
	newOar->SetCollidesWithTerrainWhileAttached(false);
}

void AVehicle::SetExhaust(AEmitter* newExhaust) {
	if (m_Exhaust && m_Exhaust->IsAttached()) {
		RemoveAndDeleteAttachable(m_Exhaust);
	}
	m_Exhaust = newExhaust;
	if (!newExhaust) {
		return;
	}
	AddAttachable(newExhaust);

	m_HardcodedAttachableUniqueIDsAndRemovers.insert({newExhaust->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                  AVehicle* vehicle = dynamic_cast<AVehicle*>(parent);
		                                                  if (vehicle->m_Exhaust == attachable) {
			                                                  vehicle->m_Exhaust = nullptr;
		                                                  }
	                                                  }});

	newExhaust->SetInheritsRotAngle(true);
	newExhaust->SetCollidesWithTerrainWhileAttached(false);
	newExhaust->EnableEmission(false);
}

void AVehicle::SetTurret(Turret* newTurret) {
	if (m_Turret && m_Turret->IsAttached()) {
		RemoveAndDeleteAttachable(m_Turret);
	}
	m_Turret = newTurret;
	if (!newTurret) {
		return;
	}
	AddAttachable(newTurret);

	m_HardcodedAttachableUniqueIDsAndRemovers.insert({newTurret->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                  AVehicle* vehicle = dynamic_cast<AVehicle*>(parent);
		                                                  if (vehicle->m_Turret == attachable) {
			                                                  vehicle->m_Turret = nullptr;
		                                                  }
	                                                  }});

	newTurret->SetInheritsRotAngle(true);
	newTurret->SetCollidesWithTerrainWhileAttached(false);
}

void AVehicle::SetTracks(Attachable* newTracks) {
	if (m_Tracks && m_Tracks->IsAttached()) {
		RemoveAndDeleteAttachable(m_Tracks);
	}
	m_Tracks = newTracks;
	if (!newTracks) {
		return;
	}
	AddAttachable(newTracks);

	m_HardcodedAttachableUniqueIDsAndRemovers.insert({newTracks->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                  AVehicle* vehicle = dynamic_cast<AVehicle*>(parent);
		                                                  if (vehicle->m_Tracks == attachable) {
			                                                  vehicle->m_Tracks = nullptr;
		                                                  }
	                                                  }});

	// The wheels hold it up; the belt is only for the eye.
	newTracks->SetInheritsRotAngle(true);
	newTracks->SetCollidesWithTerrainWhileAttached(false);
}

void AVehicle::SetThrowingArm(Attachable* newArm) {
	if (m_ThrowingArm && m_ThrowingArm->IsAttached()) {
		RemoveAndDeleteAttachable(m_ThrowingArm);
	}
	m_ThrowingArm = newArm;
	if (!newArm) {
		return;
	}
	AddAttachable(newArm);

	m_HardcodedAttachableUniqueIDsAndRemovers.insert({newArm->GetUniqueID(), [](MOSRotating* parent, Attachable* attachable) {
		                                                  AVehicle* vehicle = dynamic_cast<AVehicle*>(parent);
		                                                  if (vehicle->m_ThrowingArm == attachable) {
			                                                  vehicle->m_ThrowingArm = nullptr;
		                                                  }
	                                                  }});

	// It swings about where it is fixed (UpdateThrowingArm), over the frame, not into the ground.
	newArm->SetInheritsRotAngle(true);
	newArm->SetCollidesWithTerrainWhileAttached(false);
}

std::string AVehicle::GetLaunchAmmoName() const {
	return m_LaunchAmmo.empty() ? std::string() : m_LaunchAmmo[std::clamp(m_LaunchAmmoIndex, 0, static_cast<int>(m_LaunchAmmo.size()) - 1)]->GetPresetName();
}

void AVehicle::UpdateEngine(bool canDrive) {
	bool running = canDrive && (m_EngineSound || m_Exhaust);
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	// The load follows the throttle over about half a second: it revs up and dies down rather than jump.
	float target = running ? std::abs(m_Throttle) : 0.0F;
	m_EngineLoad += (target - m_EngineLoad) * std::min(deltaTime * 2.5F, 1.0F);
	if (m_EngineSound) {
		if (running) {
			if (!m_EngineSound->IsBeingPlayed()) {
				m_EngineSound->Play(m_Pos);
			}
			m_EngineSound->SetPosition(m_Pos);
			m_EngineSound->SetPitch(0.75F + m_EngineLoad * 0.7F);
		} else if (m_EngineSound->IsBeingPlayed()) {
			m_EngineSound->Stop();
		}
	}
	if (m_Exhaust) {
		m_Exhaust->EnableEmission(running);
		// (Thicker working hard.)
		m_Exhaust->SetThrottle(m_EngineLoad * 2.0F - 1.0F);
	}
	m_EngineRunning = running;
}

void AVehicle::RemoveWheel(const Attachable* wheel) {
	std::erase_if(m_Wheels, [wheel](const Wheel& each) { return each.Part == wheel && !each.Strut; });
	for (Wheel& each: m_Wheels) {
		if (each.Part == wheel) {
			// (Its strut stays on, a bare leg: kept in the list so it still rides with the spring, hanging all the way out.)
			each.Part = nullptr;
			each.OnGround = false;
		}
	}
}

void AVehicle::RemoveStrut(const Attachable* strut) {
	for (Wheel& each: m_Wheels) {
		if (each.Strut == strut) {
			each.Strut = nullptr;
		}
	}
	std::erase_if(m_Wheels, [](const Wheel& each) { return !each.Part && !each.Strut; });
}

std::vector<Attachable*> AVehicle::GetWheels() const {
	std::vector<Attachable*> wheels;
	for (const Wheel& wheel: m_Wheels) {
		if (wheel.Part) {
			wheels.push_back(wheel.Part);
		}
	}
	return wheels;
}

int AVehicle::GetWheelsOnGround() const {
	return static_cast<int>(std::count_if(m_Wheels.begin(), m_Wheels.end(), [](const Wheel& wheel) { return wheel.OnGround; }));
}

Vector AVehicle::GetEyePos() const {
	const Actor* driver = GetDriver();
	return driver ? m_Pos + RotateOffset(GetSeatOffset() - Vector(0.0F, driver->GetHeight() * 0.3F)) : m_Pos;
}

bool AVehicle::HandlePieCommand(PieSliceType pieSliceType) {
	if (pieSliceType == PieSliceType::GetOut) {
		EjectSeat(m_Controller.IsPlayerControlled() ? GetControlSeat() : 0, false);
		return true;
	}
	if (pieSliceType == PieSliceType::ChangeSeat) {
		ChangeSeat();
		return true;
	}
	return Actor::HandlePieCommand(pieSliceType);
}

void AVehicle::GibThis(const Vector& impactImpulse, MovableObject* movableObjectToIgnore) {
	// Everyone is thrown clear of the wreck.
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		if (Actor* unit = EjectSeat(seat, true)) {
			unit->SetVel(unit->GetVel() + impactImpulse / std::max(GetMass(), 1.0F) * 0.5F);
		}
	}
	m_Crew.TipOutAllSlumped(true);
	Actor::GibThis(impactImpulse, movableObjectToIgnore);
}

void AVehicle::UpdateBoarding() {
	// On its side or roof and still, a unit beside it pushes it back over instead of getting in (an open one would only throw them out).
	m_Crew.UpdateBoarding(std::abs(Wrapped(m_Rotation.GetRadAngle())) > 1.2F && m_Vel.MagnitudeIsLessThan(2.0F));
}

void AVehicle::TakeOrdersFrom(const Actor* unit) {
	ClearAIWaypoints();
	Vector destination = unit->GetLastAIWaypoint();
	if (g_SceneMan.ShortestDistance(m_Pos, destination).MagnitudeIsGreaterThan(40.0F)) {
		AddAISceneWaypoint(destination);
		SetAIMode(AIMODE_GOTO);
	}
	m_AIGaveUp = false;
}

bool AVehicle::CanDriveOnTo(float direction) const {
	bool boat = HasHull() && m_Wheels.empty();
	Vector probe = m_Pos + Vector(direction * (GetRadius() * 0.6F + 16.0F), 0.0F);
	// From a little above, down: a wheeled vehicle wants ground within a drop it can take (deep water only if it floats); a boat wants
	// water, not ground, ahead of its bow.
	for (float down = -40.0F; down < 200.0F; down += 4.0F) {
		Vector point(probe.m_X, probe.m_Y + down);
		int material = g_SceneMan.GetTerrMatter(point.GetFloorIntX(), point.GetFloorIntY());
		if (FluidSim::IsLiquid(material)) {
			return boat || HasHull();
		}
		if (boat) {
			if (material != g_MaterialAir && !IsPlant(material)) {
				return false;
			}
		} else if (HoldsWheel(point)) {
			return true;
		}
	}
	return false;
}

void AVehicle::GiveUpDriving() {
	ClearAIWaypoints();
	SetAIMode(AIMODE_SENTRY);
	m_AIGaveUp = true;
	m_AIGaveUpTimer.Reset();
	m_AIOrderSerialSeen = GetAIOrderSerial();
}

PathAgent AVehicle::GetPathAgent() const {
	PathAgent agent = Actor::GetPathAgent();
	const float gravity = std::max(std::abs(g_SceneMan.GetGlobalAcc().m_Y), 0.01F);
	// Up: what a hop of its springs lifts it (none, without), in metres; the grid lets anything a node up, as a wheel rolls up a step.
	agent.JumpHeight = m_HopSpeed > 0.0F ? m_HopSpeed * m_HopSpeed / (2.0F * gravity) : 0.0F;
	// Down: no drop it would break on landing from, nor one deeper than its driver drives off (CanDriveOnTo looks 200 px down).
	float safeFall = m_BreakLandingSpeed > 0.0F ? m_BreakLandingSpeed * m_BreakLandingSpeed / (2.0F * gravity) * c_PPM : GetMaxSafeFallHeight();
	agent.MaxSafeFall = std::clamp(safeFall, 48.0F, 180.0F);
	// No digging, no doors but its own side's (which open for it), no jets, ladders, stairs, scrambles, leaps or ledges pulled up onto.
	agent.DigStrength = 1.0F;
	agent.BreachStrength = 0.0F;
	agent.JetTimeMS = 0.0F;
	agent.ClimbsLadders = false;
	agent.WalksStairs = false;
	agent.Scrambles = false;
	agent.LeapHeight = 0.0F;
	agent.MantleHeight = 0.0F;
	agent.StraightFlight = false;
	agent.SkyRoute = false;
	agent.Lifts.reset();
	// Its height for head room, and it can't crouch under anything lower. (As wide as the grid can tell: a node's clearances reach no further.)
	agent.StandHeight = std::clamp(GetRadius() * 0.9F, 24.0F, 90.0F);
	agent.CrawlHeight = agent.StandHeight;
	agent.CrouchHeight = 0.0F;
	agent.HalfWidth = 16.0F;
	agent.Floats = HasHull();
	agent.BreathSeconds = FLT_MAX;
	return agent;
}

bool AVehicle::InFiringPosition(const MovableObject* target) const {
	const Actor* enemy = dynamic_cast<const Actor*>(target);
	// (Manned as UpdateGun mans it: from the first gunner's seat, or the driver's with none.)
	const int gunnerSeat = std::max(m_Crew.GetSeatOfGun(0), 0);
	if (!enemy || enemy->GetTeam() == m_Team || (!m_Turret && !m_ThrowingArm) || !GetSeatOccupant(gunnerSeat)) {
		return false;
	}
	Vector pivot = m_Turret ? m_Turret->GetPos() : GetReleasePoint();
	Vector toTarget = g_SceneMan.ShortestDistance(pivot, enemy->GetPos());
	if (toTarget.MagnitudeIsGreaterThan(m_GunRange * 0.8F)) {
		return false;
	}
	if (!m_Turret) {
		float angle = 0.0F;
		return std::abs(toTarget.m_X) >= 120.0F && LobAngleTo(toTarget, angle);
	}
	Vector blocked;
	return !g_SceneMan.CastStrengthRay(pivot, toTarget, 5.0F, blocked, 3);
}

bool AVehicle::CanHitFrom(const Vector& gunAt, const Vector& aimAt) const {
	const Vector toAim = g_SceneMan.ShortestDistance(gunAt, aimAt);
	const float elevation = std::atan2(-toAim.m_Y, std::abs(toAim.m_X));
	if (toAim.MagnitudeIsGreaterThan(m_GunRange * 0.85F) || elevation > GetAimRangeUp() || elevation < -m_AimRange) {
		return false;
	}
	// (A line clear to it, or to the ground right at it: a place on the ground is fired into.)
	Vector blocked;
	if (g_SceneMan.CastStrengthRay(gunAt, toAim, 5.0F, blocked, 3) && !g_SceneMan.ShortestDistance(blocked, aimAt).MagnitudeIsLessThan(24.0F)) {
		return false;
	}
	return !FriendInLineOfFire(this, m_Team, gunAt, toAim);
}

bool AVehicle::FindFiringSpot(const Vector& aimAt, Vector& spot) const {
	if (!m_Turret) {
		return false;
	}
	const bool wraps = g_SceneMan.SceneWrapsX();
	const float radius = GetRadius();
	// How high its middle rides over the ground, and its gun over its middle.
	const float ride = std::max(g_SceneMan.MovePointToGround(m_Pos, 0, 4).m_Y - m_Pos.m_Y, 0.0F);
	const float gunUp = m_Turret->GetPos().m_Y - m_Pos.m_Y;
	const float near = g_SceneMan.ShortestDistance(aimAt, m_Pos, wraps).m_X >= 0.0F ? 1.0F : -1.0F;
	// Room clear of its side's other vehicles, where they are and where they are driving to.
	auto crowded = [this, wraps, radius](const Vector& at) {
		for (const Actor* actor: g_MovableMan.GetActorList()) {
			const AVehicle* other = dynamic_cast<const AVehicle*>(actor);
			if (!other || other == this || other->GetTeam() != m_Team || other->IsDead()) {
				continue;
			}
			const float room = (radius + other->GetRadius()) * 1.1F;
			const Vector off = g_SceneMan.ShortestDistance(other->GetPos(), at, wraps);
			if (std::abs(off.m_X) < room && std::abs(off.m_Y) < room * 1.5F) {
				return true;
			}
			if (float goal = 0.0F; other->GetManoeuvreGoal(goal) && std::abs(g_SceneMan.ShortestDistance(Vector(goal, at.m_Y), at, wraps).m_X) < room) {
				return true;
			}
		}
		return false;
	};
	bool found = false;
	float best = FLT_MAX;
	for (float side: {near, -near}) {
		for (float share: {0.35F, 0.45F, 0.55F, 0.65F, 0.75F}) {
			Vector probe(aimAt.m_X + side * share * m_GunRange, aimAt.m_Y - m_GunRange * 0.25F);
			g_SceneMan.WrapPosition(probe);
			probe.m_Y = std::max(probe.m_Y, 0.0F);
			// The ground there: up out of it if the point is in it, else down onto it.
			int px = static_cast<int>(probe.m_X);
			int py = static_cast<int>(std::min(probe.m_Y, static_cast<float>(g_SceneMan.GetSceneHeight() - 1)));
			Vector ground;
			if (g_SceneMan.GetTerrMatter(px, py) != MaterialColorKeys::g_MaterialAir) {
				while (py > 0 && g_SceneMan.GetTerrMatter(px, py) != MaterialColorKeys::g_MaterialAir) {
					py -= 4;
				}
				ground = Vector(probe.m_X, static_cast<float>(py + 4));
			} else {
				ground = g_SceneMan.MovePointToGround(probe, 0, 4);
				if (ground.m_Y >= static_cast<float>(g_SceneMan.GetSceneHeight() - 2)) {
					continue;
				}
			}
			const Vector at(ground.m_X, ground.m_Y - ride);
			if (!CanHitFrom(at + Vector(0.0F, gunUp), aimAt) || crowded(at)) {
				continue;
			}
			// The nearest, on its own side (the far side means driving past it).
			float score = std::abs(g_SceneMan.ShortestDistance(m_Pos, at, wraps).m_X) + (side != near ? m_GunRange : 0.0F);
			if (score < best) {
				best = score;
				spot = at;
				found = true;
			}
		}
	}
	return found;
}

void AVehicle::FaceForCommander() {
	if (!m_AIFaceTimer.IsPastSimMS(1000) || m_Vel.MagnitudeIsGreaterThan(1.0F) || m_Handbrake) {
		return;
	}
	m_AIFaceTimer.Reset();
	// (Its gunner has something in front: nothing to turn for.)
	if (m_GunTargetID != 0 || m_Status == DYING || m_Status == DEAD) {
		return;
	}
	int facing = 0;
	if (m_Turret || m_ThrowingArm) {
		Vector pivot = m_Turret ? m_Turret->GetPos() : GetReleasePoint();
		float nearest = m_GunRange;
		for (const Actor* actor: g_MovableMan.GetActorList()) {
			if (actor == this || actor->GetTeam() == m_Team || actor->GetTeam() == Activity::NoTeam || actor->IsDead() || actor->GetHealth() <= 0.0F) {
				continue;
			}
			Vector toTarget = g_SceneMan.ShortestDistance(pivot, actor->GetPos());
			float distance = toTarget.GetMagnitude();
			if (distance >= nearest || toTarget.m_X * GetFlipFactor() > 0.0F) {
				continue;
			}
			Vector blocked;
			if (m_Turret && g_SceneMan.CastStrengthRay(pivot, toTarget, 5.0F, blocked, 3)) {
				continue;
			}
			nearest = distance;
			facing = toTarget.m_X > 0.0F ? 1 : -1;
		}
	}
	// (Ordered to suppress a place: it faces the place, its gun only swinging in front.)
	Vector suppressCentre;
	float suppressRadius = 0.0F;
	if (facing == 0 && GetSuppressZone(suppressCentre, suppressRadius)) {
		facing = g_SceneMan.ShortestDistance(m_Pos, suppressCentre).m_X > 0.0F ? 1 : -1;
	}
	if (facing == 0) {
		facing = m_StandingOrder.PostFacing;
	}
	if (facing != 0 && (facing > 0) == m_HFlipped) {
		SetHFlipped(!m_HFlipped);
	}
}

bool AVehicle::UpdateAIManoeuvre(const Actor* enemy) {
	const bool wraps = g_SceneMan.SceneWrapsX();
	if (m_AIManoeuvring) {
		// On the way: there, or held up (blocked, an edge, no headway, too long at it): stopped where it is.
		float toX = g_SceneMan.ShortestDistance(m_Pos, Vector(m_AIManoeuvreX, m_Pos.m_Y), wraps).m_X;
		float direction = toX > 0.0F ? 1.0F : -1.0F;
		bool stalled = m_AIManoeuvreTimer.IsPastSimMS(1200) && std::abs(m_Vel.m_X) < 0.3F;
		if (std::abs(toX) < 8.0F || m_AIManoeuvreTimer.IsPastSimMS(m_AIManoeuvreLimitMS) || stalled || !CanDriveOnTo(direction)) {
			m_AIManoeuvring = false;
			m_AIStuckTimer.Reset();
			return false;
		}
		m_Throttle = direction;
		m_Braking = false;
		m_AIKeepFacing = true;
		m_AIStuckTimer.Reset();
		return true;
	}
	if (m_Status == DYING || m_Status == DEAD || (!m_Turret && !m_ThrowingArm)) {
		return false;
	}
	const float direct = std::clamp(m_StandingOrder.Direct, -1.0F, 1.0F);
	auto start = [this, wraps](float x) {
		Vector to(x, m_Pos.m_Y);
		g_SceneMan.WrapPosition(to);
		m_AIManoeuvreX = to.m_X;
		m_AIManoeuvring = true;
		// (Four seconds for a short move, more for a long drive round to a firing position, at a slow crawl's pace.)
		m_AIManoeuvreLimitMS = std::max(4000LL, static_cast<long long>(std::abs(g_SceneMan.ShortestDistance(m_Pos, to, wraps).m_X) / 60.0F * 1000.0F) + 2000LL);
		m_AIManoeuvreTimer.Reset();
		m_AIJinkTimer.Reset();
		return true;
	};
	const bool hit = !m_AIHitTimer.IsPastSimMS(1000);
	// How often it moves for being hit: soon on a cautious order, a while on a normal one, never on a full-risk one.
	const float jinkEvery = direct >= 0.9F ? -1.0F : (direct < 0.0F ? 2000.0F : 3500.0F + 2000.0F * direct);
	const bool mayJink = hit && jinkEvery > 0.0F && m_AIJinkTimer.IsPastSimMS(static_cast<long long>(jinkEvery));
	if (enemy) {
		Vector toEnemy = g_SceneMan.ShortestDistance(m_Pos, enemy->GetPos(), wraps);
		float across = std::abs(toEnemy.m_X);
		float away = toEnemy.m_X > 0.0F ? -1.0F : 1.0F;
		// Too close, or right under it, to bring its gun to bear (a quarter of its range to a sixth, by the order's care): back off to where it can.
		float nearest = std::max(GetRadius() * 1.5F, m_GunRange * (0.3F - 0.15F * direct));
		bool inPosition = InFiringPosition(enemy);
		if (inPosition) {
			m_AINoShotTimer.Reset();
		}
		if (across < nearest && (!inPosition || toEnemy.m_Y < -GetRadius())) {
			return start(m_Pos.m_X + away * (nearest * 1.3F - across));
		}
		// In range but no shot (over a lip, behind cover, a friend in the way), a while: round to a firing position.
		if (!inPosition && across < m_GunRange * 0.8F && across >= nearest * 1.4F && m_AINoShotTimer.IsPastSimMS(2500)) {
			m_AINoShotTimer.Reset();
			if (Vector spot; FindFiringSpot(enemy->GetPos(), spot)) {
				return start(spot.m_X);
			}
		}
		if (!inPosition && across < nearest * 1.4F) {
			// Near, with no shot (over a lip, behind cover): it waits there facing it rather than driving back in under it; after a while
			// with none, it tries a little further out, where the angle is lower.
			if (m_AINoShotTimer.IsPastSimMS(2500) && across < m_GunRange * 0.7F) {
				m_AINoShotTimer.Reset();
				// Round to a firing position with a clear line to it, else a little further out, where the angle is lower.
				if (Vector spot; FindFiringSpot(enemy->GetPos(), spot)) {
					return start(spot.m_X);
				}
				return start(m_Pos.m_X + away * 80.0F);
			}
			if ((toEnemy.m_X > 0.0F) == m_HFlipped && std::abs(m_Vel.m_X) < 1.5F) {
				SetHFlipped(!m_HFlipped);
			}
			m_AIStuckTimer.Reset();
			return true;
		}
		if (inPosition && mayJink) {
			if (direct < 0.0F) {
				// Cautious: out to the edge of its reach.
				float edge = m_GunRange * 0.75F;
				if (across < edge - 20.0F) {
					return start(m_Pos.m_X + away * std::min(edge - across, m_GunRange * 0.25F));
				}
			}
			// A shuffle, never nearer than it may go.
			float step = RandomNum(30.0F, 60.0F) * (RandomNum(0.0F, 1.0F) < 0.5F ? 1.0F : -1.0F);
			if (across + step * away < nearest) {
				step = std::abs(step) * away;
			}
			return start(m_Pos.m_X + step);
		}
		return false;
	}
	// Ordered to suppress a place, with no shot into it for a while from where it holds: round to a firing position.
	Vector suppressCentre;
	float suppressRadius = 0.0F;
	if (m_AIMode != AIMODE_GOTO && m_Turret && GetSuppressZone(suppressCentre, suppressRadius) && m_SuppressNoShotTimer.IsPastSimMS(3000)) {
		m_SuppressNoShotTimer.Reset();
		if (Vector spot; FindFiringSpot(suppressCentre, spot) && std::abs(g_SceneMan.ShortestDistance(m_Pos, spot, wraps).m_X) > 16.0F) {
			return start(spot.m_X);
		}
	}
	// Stopped right by another of its side's vehicles, also stopped: the one made later moves off along the ground, to give them both room.
	if (m_AIMode != AIMODE_GOTO && !m_Handbrake && m_AISpacingTimer.IsPastSimMS(1000)) {
		m_AISpacingTimer.Reset();
		for (const Actor* actor: g_MovableMan.GetActorList()) {
			const AVehicle* other = dynamic_cast<const AVehicle*>(actor);
			if (!other || other == this || other->GetTeam() != m_Team || other->IsDead() || other->GetUniqueID() > GetUniqueID() || other->GetVel().MagnitudeIsGreaterThan(1.0F)) {
				continue;
			}
			const Vector off = g_SceneMan.ShortestDistance(other->GetPos(), m_Pos, wraps);
			const float room = (GetRadius() + other->GetRadius()) * 0.8F;
			if (std::abs(off.m_X) < room && std::abs(off.m_Y) < room) {
				const float away = off.m_X >= 0.0F ? 1.0F : -1.0F;
				const float step = room - std::abs(off.m_X) + 20.0F;
				if (CanDriveOnTo(away)) {
					return start(m_Pos.m_X + away * step);
				}
				return start(m_Pos.m_X - away * (std::abs(off.m_X) + room + 20.0F));
			}
		}
	}
	// Holding a post, hit while its gunner has someone: a shuffle about the post, within the reach ReturnDefenders leaves it.
	if (m_AIMode == AIMODE_SENTRY && m_StandingOrder.HasPost && m_GunTargetID != 0 && mayJink) {
		float slack = GetRadius() * 0.8F + 30.0F;
		float offPost = g_SceneMan.ShortestDistance(m_StandingOrder.Post, m_Pos, wraps).m_X;
		float step = RandomNum(25.0F, 50.0F) * (RandomNum(0.0F, 1.0F) < 0.5F ? 1.0F : -1.0F);
		return start(m_StandingOrder.Post.m_X + std::clamp(offPost + step, -slack, slack));
	}
	return false;
}

void AVehicle::UpdateAIDriving() {
	m_Throttle = 0.0F;
	m_Braking = true;
	m_AIHop = false;
	// (One the player commands drives where it's told whatever the AI vehicles setting, which is about AI units taking vehicles of their own.)
	if (!Controller::AIMayAct() || (!g_SettingsMan.AIUsesVehicles() && !m_Commanded)) {
		return;
	}
	// A new order starts afresh.
	if (GetAIOrderSerial() != m_AIOrderSerialSeen) {
		m_AIOrderSerialSeen = GetAIOrderSerial();
		m_AIStuckCount = 0;
		m_AIRoutesAsked = 0;
		m_AIReversing = false;
		m_AIStuckTimer.Reset();
		m_AIGaveUp = false;
	}
	// Hit since it last looked: its health fell.
	if (float health = GetHealth(); m_AICombatHealth >= 0.0F && health < m_AICombatHealth - 0.25F) {
		m_AIHitTimer.Reset();
	}
	m_AICombatHealth = GetHealth();
	m_AIKeepFacing = false;
	const MovableObject* following = g_MovableMan.ValidMO(GetMOMoveTarget()) ? GetMOMoveTarget() : nullptr;
	if (m_Commanded) {
		const Actor* enemy = m_AIMode == AIMODE_GOTO ? dynamic_cast<const Actor*>(following) : nullptr;
		if (UpdateAIManoeuvre(enemy && enemy->GetTeam() != m_Team && !enemy->IsDead() ? enemy : nullptr)) {
			return;
		}
	}
	if (m_AIMode != AIMODE_GOTO) {
		if (m_Commanded) {
			FaceForCommander();
		}
		return;
	}
	// Passengers it called over still on their way: it waits for them, braked, for at most the ride wait from when the first was called.
	if (!m_Commanded && HasDriver() && !m_Recruits.empty() && g_SettingsMan.AIVehiclesWaitForRiders() && !m_AIRiderWaitTimer.IsPastSimMS(static_cast<double>(g_SettingsMan.AIShipRideWait()) * 1000.0)) {
		m_AIStuckTimer.Reset();
		return;
	}
	// Commanded after an enemy (VC-4): it stops where its gun reaches it and fires from there, rather than driving onto it. It keeps after it,
	// so it drives on if the enemy gets out of reach or out of sight.
	if (m_Commanded && following && InFiringPosition(following)) {
		Vector toEnemy = g_SceneMan.ShortestDistance(m_Pos, following->GetPos());
		if ((toEnemy.m_X > 0.0F) == m_HFlipped && std::abs(m_Vel.m_X) < 1.5F) {
			SetHFlipped(!m_HFlipped);
		}
		m_AIStuckTimer.Reset();
		return;
	}
	Vector toGoal = g_SceneMan.ShortestDistance(m_Pos, following ? following->GetPos() : GetLastAIWaypoint());
	float arrive = GetRadius() * 0.4F + 20.0F;
	float speed = m_Vel.m_X;
	// Along its route (VC-2): the route the pathfinder gave it, as a unit on foot is given one (Actor::UpdateMovePath, from GetPathAgent), its
	// points passed as it comes level with them. It can't steer up or down, so a point is passed when it's level with it across. With no route
	// (still being asked for, or none at all), and for a boat, which the grid doesn't know to keep to the water: straight at where it is going.
	bool boat = HasHull() && m_Wheels.empty();
	Vector toNext = toGoal;
	bool lastLeg = true;
	if (!boat) {
		while (!m_MovePath.empty() && std::abs(g_SceneMan.ShortestDistance(m_Pos, m_MovePath.front()).m_X) < arrive * 0.5F + 4.0F) {
			RemoveMovePathBeginning();
		}
		if (!m_MovePath.empty()) {
			toNext = g_SceneMan.ShortestDistance(m_Pos, m_MovePath.front());
			lastLeg = m_MovePath.size() <= 1;
		}
	}
	// (A goal straight under or over it on another level isn't here: the route goes there. Till the route comes, it waits.)
	bool levelWithGoal = std::abs(toGoal.m_Y) < GetRadius() * 2.0F + 48.0F || (m_MovePath.size() <= 1 && !IsWaitingOnNewMovePath());
	if (!m_AIWantsStop && std::abs(toGoal.m_X) < arrive && !levelWithGoal && m_MovePath.empty()) {
		m_AIStuckTimer.Reset();
		return;
	}
	if (m_AIWantsStop || (std::abs(toGoal.m_X) < arrive && levelWithGoal)) {
		// There (or someone wants out): stop, and with nothing to follow, the order is done.
		if (!m_AIWantsStop && !following && std::abs(speed) < 1.0F) {
			ClearAIWaypoints();
			SetAIMode(AIMODE_SENTRY);
		}
		m_AIStuckTimer.Reset();
		return;
	}
	float direction = toNext.m_X > 0.0F ? 1.0F : -1.0F;
	if (m_AIReversing) {
		m_Throttle = -direction;
		m_Braking = false;
		if (m_AIReverseTimer.IsPastSimMS(900)) {
			m_AIReversing = false;
			m_AIStuckTimer.Reset();
		}
		return;
	}
	// Braking in time to stop where it's going: at the goal on the last leg of its route, or at a point where the route turns back.
	float stopping = speed * speed / (2.0F * std::max(m_BrakeStrength, 1.0F)) * c_PPM + arrive * 0.5F;
	bool turnsBack = false;
	if (!lastLeg) {
		auto after = std::next(m_MovePath.begin());
		turnsBack = (g_SceneMan.ShortestDistance(m_MovePath.front(), *after).m_X > 0.0F) != (direction > 0.0F);
	}
	float brakeFor = lastLeg ? std::abs(toGoal.m_X) : (turnsBack ? std::abs(toNext.m_X) : FLT_MAX);
	if (speed * direction > 0.5F && brakeFor < stopping) {
		return;
	}
	if (!CanDriveOnTo(direction)) {
		// (Rolling slowly up to the edge isn't stuck: it has nowhere to go on to.)
		if (std::abs(speed) < 0.5F) {
			GiveUpDriving();
		}
		return;
	}
	m_Throttle = direction;
	m_Braking = false;
	// No headway for a while: back up a little and try again, or jump if it can, and now and then ask for a new route from where it is; after
	// a few goes, give up. (Commanded, it tries a little longer: its crew then get out and walk.)
	if (std::abs(speed) > 0.7F) {
		m_AIStuckTimer.Reset();
	} else if (m_AIStuckTimer.IsPastSimMS(2500)) {
		m_AIStuckTimer.Reset();
		if (++m_AIStuckCount > (m_Commanded ? 6 : 4)) {
			GiveUpDriving();
			return;
		}
		if (m_AIStuckCount % 2 == 0 && !boat && m_AIRoutesAsked < 3 && !IsWaitingOnNewMovePath()) {
			++m_AIRoutesAsked;
			m_UpdateMovePath = true;
		}
		if (m_HopSpeed > 0.0F && m_AIStuckCount % 2 == 1) {
			m_AIHop = true;
		} else {
			m_AIReversing = true;
			m_AIReverseTimer.Reset();
		}
	}
}

float AVehicle::GetClimbAngle() const {
	float gravity = std::max(std::abs(g_SceneMan.GetGlobalAcc().m_Y), 0.01F);
	float byPower = std::asin(std::clamp(m_Acceleration / gravity, 0.0F, 1.0F));
	float byGrip = std::atan(std::max(m_Grip, 0.0F));
	return std::min(byPower, byGrip) * 180.0F / c_PI;
}

void AVehicle::UpdateAIBoarding() {
	if (!g_SettingsMan.AIUsesVehicles() || IsDead() || !Controller::AIMayAct()) {
		m_Recruits.clear();
		return;
	}
	// (A little further than a player's reach: the AI walks up to its middle and stops, not to a seat.)
	auto inReach = [this](const Actor* unit) { return m_Crew.IsInReach(unit, 24.0F); };

	// Those called over get in once they're here, or are let go after a while if they haven't made it.
	for (auto recruit = m_Recruits.begin(); recruit != m_Recruits.end();) {
		Actor* unit = dynamic_cast<Actor*>(g_MovableMan.FindObjectByUniqueID(recruit->ID));
		bool keep = unit && !unit->IsDead() && !unit->IsSetToDelete() && !unit->GetController()->IsPlayerControlled() && !recruit->Since.IsPastSimMS(25000) && GetFreeSeat() >= 0;
		if (keep && inReach(unit)) {
			unit->RemoveAIMOWaypoint(this);
			int seat = GetFreeSeat();
			if (TakeSeat(unit, seat)) {
				SetRider(seat, true);
				if (seat == 0) {
					TakeOrdersFrom(GetSeatOccupant(0));
				}
			}
			recruit = m_Recruits.erase(recruit);
			continue;
		}
		if (!keep) {
			if (unit) {
				unit->RemoveAIMOWaypoint(this);
			}
			recruit = m_Recruits.erase(recruit);
			continue;
		}
		++recruit;
	}

	// Units sent to it (to follow it, as a move order onto it gives) get in when they get here, and stay in.
	if (m_OrderedBoardTimer.IsPastSimMS(250)) {
		m_OrderedBoardTimer.Reset();
		if (GetFreeSeat() >= 0) {
			for (Actor* unit: g_MovableMan.GetActorList()) {
				if (unit->GetMOMoveTarget() == this && !unit->GetController()->IsPlayerControlled() && CanCarry(unit) && inReach(unit) &&
				    std::none_of(m_Recruits.begin(), m_Recruits.end(), [unit](const Recruit& recruit) { return recruit.ID == unit->GetUniqueID(); })) {
					unit->RemoveAIMOWaypoint(this);
					unit->ClearAIWaypoints();
					unit->SetAIMode(AIMODE_SENTRY);
					TakeSeat(unit, -1);
					if (GetFreeSeat() < 0) {
						break;
					}
				}
			}
		}
	}

	// Now and then, AI units near it going a long way its way are called over: the first to drive, then any going where it's going.
	if (!m_RecruitTimer.IsPastSimMS(1000)) {
		return;
	}
	m_RecruitTimer.Reset();
	int room = 0;
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		room += GetSeatOccupant(seat) ? 0 : 1;
	}
	room -= static_cast<int>(m_Recruits.size());
	// (Nor into one the player commands: its seats are the player's to fill.)
	if (room <= 0 || m_Commanded || m_Controller.IsPlayerControlled() || (m_AIGaveUp && !m_AIGaveUpTimer.IsPastSimMS(10000))) {
		return;
	}
	// A boat only takes riders while it is afloat.
	if (HasHull() && m_Wheels.empty() && m_Submerged <= 0.0F) {
		return;
	}
	bool driverComing = !HasDriver() && !m_Recruits.empty();
	// (Passengers only once someone drives, so it is known where it's going.)
	if (driverComing) {
		return;
	}
	Vector going = HasDriver() ? GetLastAIWaypoint() : m_Pos;
	bool goingSomewhere = HasDriver() && m_AIMode == AIMODE_GOTO && g_SceneMan.ShortestDistance(m_Pos, going).MagnitudeIsGreaterThan(200.0F);
	if (HasDriver() && !goingSomewhere) {
		return;
	}
	int routeSearches = 2; // (Searched on the spot: a couple a look.)
	for (Actor* unit: g_MovableMan.GetActorList()) {
		if (unit->GetController()->IsPlayerControlled() || unit->GetAIMode() != AIMODE_GOTO || !CanCarry(unit)) {
			continue;
		}
		// (Not one already called over by a vehicle, or sent to one.)
		const MovableObject* following = unit->GetMOMoveTarget();
		if ((following && (dynamic_cast<const AVehicle*>(following) || dynamic_cast<const ACShip*>(following))) ||
		    std::any_of(unit->GetWaypointList().begin(), unit->GetWaypointList().end(), [](const std::pair<Vector, const MovableObject*>& waypoint) { return waypoint.second && (dynamic_cast<const AVehicle*>(waypoint.second) || dynamic_cast<const ACShip*>(waypoint.second)); })) {
			continue;
		}
		Vector toVehicle = g_SceneMan.ShortestDistance(unit->GetPos(), m_Pos);
		if (toVehicle.MagnitudeIsGreaterThan(m_RecruitRange) || std::abs(toVehicle.m_Y) > 150.0F) {
			continue;
		}
		// Going further than the shortest walk worth a ride, and the vehicle not much further from where it's going than it is (a little
		// way back to get in, up to a quarter of the walk).
		Vector destination = unit->GetLastAIWaypoint();
		float walk = g_SceneMan.ShortestDistance(unit->GetPos(), destination).GetMagnitude();
		if (walk < g_SettingsMan.AIShipRideLongWalkDistance() || g_SceneMan.ShortestDistance(m_Pos, destination).GetMagnitude() > walk + std::max(100.0F, walk * 0.25F)) {
			continue;
		}
		// With nobody driving yet, only if it can drive there (or near enough to walk the rest): the route its driver would take.
		if (!HasDriver()) {
			if (routeSearches-- <= 0) {
				break;
			}
			if (Scene* scene = g_SceneMan.GetScene()) {
				std::list<Vector> path;
				float cost = scene->CalculatePath(GetPathStart(), destination, path, GetPathAgent(), static_cast<Activity::Teams>(m_Team));
				bool drives = cost >= 0.0F && cost < 100000.0F && !path.empty() && g_SceneMan.ShortestDistance(path.back(), destination).MagnitudeIsLessThan(300.0F);
				if (!drives) {
					continue;
				}
			}
		}
		// With a driver, only going where it's going.
		if (HasDriver() && g_SceneMan.ShortestDistance(going, destination).GetMagnitude() > walk * 0.5F) {
			continue;
		}
		// (The wait for passengers, AIVehiclesWaitForRiders, runs from the first called over once someone drives.)
		if (HasDriver() && m_Recruits.empty()) {
			m_AIRiderWaitTimer.Reset();
		}
		unit->AddAIMOWaypointFirst(this);
		m_Recruits.push_back({unit->GetUniqueID(), Timer()});
		if (--room <= 0 || !HasDriver()) {
			break;
		}
	}
}

void AVehicle::UpdateAIUnloading() {
	m_AIWantsStop = false;
	// (Told to stay in, commanded as one unit, VC-1, or the AI paused: nobody gets out on their own. A commanded one's crew get out when ordered out, or are
	// let out by the sandbox when it can't get where it was sent.)
	if (m_CrewStaysIn || m_Commanded || !Controller::AIMayAct()) {
		return;
	}
	// (Stopped for good with nowhere more to go: riders get out even short of where they're going, a cliff it can't climb, say.)
	bool stopped = !m_Controller.IsPlayerControlled() && m_AIMode != AIMODE_GOTO;
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		Actor* unit = GetSeatOccupant(seat);
		if (!unit || !IsRider(seat)) {
			continue;
		}
		Vector toDestination = g_SceneMan.ShortestDistance(m_Pos, unit->GetLastAIWaypoint());
		bool there = std::abs(toDestination.m_X) < 150.0F && std::abs(toDestination.m_Y) < 250.0F;
		// The driver got out and the rider has further to go: they move over to the wheel and drive on.
		if (seat != 0 && !HasDriver() && !there && !m_AIGaveUp && !m_Controller.IsPlayerControlled() && g_SettingsMan.AIUsesVehicles()) {
			SetSeatOccupant(0, unit);
			SetSeatOccupant(seat, nullptr);
			SetRider(0, true);
			TakeOrdersFrom(unit);
			continue;
		}
		if (there || m_AIGaveUp || stopped || (!HasDriver() && seat != 0)) {
			m_AIWantsStop = true;
			if (m_Vel.MagnitudeIsLessThan(1.5F)) {
				if (Actor* out = EjectSeat(seat, false)) {
					out->ResumeAIWaypoints();
				}
			}
		}
	}
}

long AVehicle::FindGunTarget() const {
	return FindGunTargetFacing(GetFlipFactor());
}

long AVehicle::FindGunTargetBehind() const {
	return FindGunTargetFacing(-GetFlipFactor());
}

long AVehicle::FindGunTargetFacing(float flip) const {
	if (!m_Turret && !m_ThrowingArm) {
		return 0;
	}
	// A catapult lobs: it reaches over walls to what its crew knows is there, but not what its arc can't reach, nor right under it.
	bool lobs = !m_Turret;
	Vector pivot = lobs ? GetReleasePoint() : m_Turret->GetPos();
	float rotationFacing = Wrapped(m_Rotation.GetRadAngle()) * flip;
	long best = 0;
	float bestDistance = m_GunRange;
	// The enemy it was ordered to attack first, when the gun can be brought to bear on it (VC-4); else the nearest it can.
	long ordered = m_StandingOrder.TargetID;
	if (const MovableObject* following = GetMOMoveTarget(); !ordered && m_Commanded && following && g_MovableMan.ValidMO(following)) {
		ordered = following->GetUniqueID();
	}
	for (const Actor* actor: g_MovableMan.GetActorList()) {
		if (actor == this || actor->GetTeam() == m_Team || actor->GetTeam() == Activity::NoTeam || actor->IsDead() || actor->IsSetToDelete() || actor->GetHealth() <= 0.0F) {
			continue;
		}
		Vector toTarget = g_SceneMan.ShortestDistance(pivot, actor->GetPos());
		float distance = toTarget.GetMagnitude();
		// (In front of it, and within the gun's swing: it can't turn the vehicle round. The ordered one anywhere in range, nearer ones or not.)
		const bool isOrdered = ordered != 0 && static_cast<long>(actor->GetUniqueID()) == ordered;
		if (distance >= (isOrdered ? m_GunRange : bestDistance) || toTarget.m_X * flip <= 0.0F) {
			continue;
		}
		if (lobs) {
			float angle = 0.0F;
			if (std::abs(toTarget.m_X) < 120.0F || !LobAngleTo(toTarget, angle)) {
				continue;
			}
		} else {
			// (Up further than down, for aircraft, when it has AimRangeUp.)
			float angle = Wrapped(std::atan2(-toTarget.m_Y, std::abs(toTarget.m_X)) - rotationFacing);
			if (angle > GetAimRangeUp() || angle < -m_AimRange) {
				continue;
			}
			Vector blocked;
			if (g_SceneMan.CastStrengthRay(pivot, toTarget, 5.0F, blocked, 3)) {
				continue;
			}
		}
		if (isOrdered) {
			return ordered;
		}
		best = actor->GetUniqueID();
		bestDistance = distance;
	}
	return best;
}

void AVehicle::UpdateGun() {
	if (!m_Turret && !m_ThrowingArm) {
		return;
	}
	bool lobs = !m_Turret;
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	// The seat that mans it: the first gunner's, or the driver's with none.
	int gunnerSeat = 0;
	if (int seat = m_Crew.GetSeatOfGun(0); seat >= 0) {
		gunnerSeat = seat;
	}
	bool manned = GetSeatOccupant(gunnerSeat) && m_Status != DYING && m_Status != DEAD;
	float rotationFacing = Wrapped(m_Rotation.GetRadAngle()) * GetFlipFactor();
	// A gun swings either side of level; a catapult throws between its lowest and highest angles.
	float lowest = lobs ? rotationFacing + m_MinLaunchAngle : rotationFacing - m_AimRange;
	float highest = lobs ? rotationFacing + m_MaxLaunchAngle : rotationFacing + GetAimRangeUp();
	bool fire = false;
	bool playerAims = manned && m_Controller.IsPlayerControlled() && GetControlSeat() == gunnerSeat;
	if (playerAims) {
		// The player aims it, as a unit aims its gun: the aim keys swing it, the mouse or stick points it.
		float speed = m_Controller.IsState(AIM_SHARP) ? 0.6F : 1.8F;
		if (lobs) {
			speed *= 0.5F;
		}
		if (m_Controller.IsState(AIM_UP)) {
			m_AimAngle += speed * deltaTime;
		} else if (m_Controller.IsState(AIM_DOWN)) {
			m_AimAngle -= speed * deltaTime;
		}
		if (Vector analogAim = m_Controller.GetAnalogAim(); analogAim.MagnitudeIsGreaterThan(0.1F)) {
			// (Behind it, the gun stays at the end of its swing on that side, up or down.)
			m_AimAngle = std::atan2(-analogAim.m_Y, std::max(analogAim.m_X * GetFlipFactor(), 0.0F));
		}
		fire = m_Controller.IsState(WEAPON_FIRE);
		// A catapult's load is changed as a unit changes what it holds, before it is wound.
		if (lobs && m_LaunchAmmo.size() > 1 && m_LaunchPhase == LaunchPhase::Loaded && m_AmmoSwitchTimer.IsPastSimMS(250)) {
			int count = static_cast<int>(m_LaunchAmmo.size());
			if (m_Controller.IsState(WEAPON_CHANGE_NEXT)) {
				m_LaunchAmmoIndex = (m_LaunchAmmoIndex + 1) % count;
				m_AmmoSwitchTimer.Reset();
			} else if (m_Controller.IsState(WEAPON_CHANGE_PREV)) {
				m_LaunchAmmoIndex = (m_LaunchAmmoIndex + count - 1) % count;
				m_AmmoSwitchTimer.Reset();
			}
		}
		m_GunTargetID = 0;
	} else if (manned && Controller::AIMayAct()) {
		// A gunner looks round now and then for the nearest enemy in sight, swings round to it and fires once it is on it. (With the AI
		// paused, it holds its aim and fire.)
		if (m_GunTargetTimer.IsPastSimMS(400)) {
			m_GunTargetID = FindGunTarget();
			m_GunTargetTimer.Reset();
			// Nothing in front, but an enemy behind it (an aircraft come over, most often): sat still with no one driving it on, it turns
			// round to bring its gun to bear, as a unit turns to face one.
			if (!m_GunTargetID && !m_Controller.IsPlayerControlled() && m_Throttle == 0.0F && std::abs(m_Vel.m_X) < 1.5F) {
				if (long behind = FindGunTargetBehind()) {
					SetHFlipped(!m_HFlipped);
					m_GunTargetID = behind;
					rotationFacing = Wrapped(m_Rotation.GetRadAngle()) * GetFlipFactor();
					lowest = lobs ? rotationFacing + m_MinLaunchAngle : rotationFacing - m_AimRange;
					highest = lobs ? rotationFacing + m_MaxLaunchAngle : rotationFacing + GetAimRangeUp();
				}
			}
		}
		const Actor* target = m_GunTargetID ? dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(m_GunTargetID)) : nullptr;
		float wanted = m_AimAngle;
		bool onIt = false;
		if (target && !target->IsDead()) {
			if (lobs) {
				// A catapult crew works out the lob that lands on it.
				onIt = LobAngleTo(g_SceneMan.ShortestDistance(GetReleasePoint(), target->GetPos()), wanted);
			} else {
				Vector toTarget = g_SceneMan.ShortestDistance(m_Turret->GetPos(), target->GetPos() - Vector(0.0F, target->GetHeight() * 0.1F));
				// Leading what moves (an aircraft most of all) by where it'll be when the round gets there.
				if (HDFirearm* gun = m_Turret->HasMountedDevice() ? dynamic_cast<HDFirearm*>(m_Turret->GetFirstMountedDevice()) : nullptr; gun && gun->GetAIFireVel() > 0.0F) {
					Vector lead = target->GetVel() * (toTarget.GetMagnitude() / gun->GetAIFireVel());
					toTarget += lead.CapMagnitude(toTarget.GetMagnitude() * 0.5F);
				}
				wanted = std::atan2(-toTarget.m_Y, std::max(toTarget.m_X * GetFlipFactor(), 0.0F));
				onIt = true;
			}
		}
		Vector suppressCentre;
		float suppressRadius = 0.0F;
		if (!onIt && GetSuppressZone(suppressCentre, suppressRadius)) {
			// Ordered to suppress a place, with nobody in sight to shoot: it fires into it, about it at random, when the place is in range in
			// front of it. No line of sight is wanted: the fire is to keep heads down there.
			if (m_SuppressTimer.IsPastSimMS(500)) {
				m_SuppressTimer.Reset();
				m_SuppressPoint = suppressCentre + Vector(RandomNum(-1.0F, 1.0F) * suppressRadius, RandomNum(-0.5F, 0.5F) * suppressRadius);
			}
			Vector pivot = lobs ? GetReleasePoint() : m_Turret->GetPos();
			Vector toPoint = g_SceneMan.ShortestDistance(pivot, m_SuppressPoint);
			if (toPoint.m_X * GetFlipFactor() > 0.0F && toPoint.MagnitudeIsLessThan(m_GunRange)) {
				if (lobs) {
					onIt = std::abs(toPoint.m_X) >= 120.0F && LobAngleTo(toPoint, wanted);
				} else {
					wanted = std::atan2(-toPoint.m_Y, std::abs(toPoint.m_X));
					float off = Wrapped(wanted - rotationFacing);
					onIt = off <= GetAimRangeUp() && off >= -m_AimRange;
				}
			}
		}
		// (Someone in its sights, or a clear line into the place it suppresses: it has a shot from here, and needn't move for one.)
		if (target && onIt) {
			m_SuppressNoShotTimer.Reset();
		} else if (onIt && !lobs) {
			Vector blocked;
			Vector toPoint = g_SceneMan.ShortestDistance(m_Turret->GetPos(), m_SuppressPoint);
			if (!g_SceneMan.CastStrengthRay(m_Turret->GetPos(), toPoint, 5.0F, blocked, 3) || g_SceneMan.ShortestDistance(blocked, m_SuppressPoint).MagnitudeIsLessThan(suppressRadius * 0.5F + 20.0F)) {
				m_SuppressNoShotTimer.Reset();
			}
		} else if (!GetSuppressZone(suppressCentre, suppressRadius)) {
			m_SuppressNoShotTimer.Reset();
		}
		if (onIt) {
			float turn = std::clamp(Wrapped(wanted - m_AimAngle), -m_GunTurnSpeed * deltaTime, m_GunTurnSpeed * deltaTime);
			m_AimAngle += turn;
			fire = std::abs(Wrapped(wanted - m_AimAngle)) < (lobs ? 0.02F : 0.1F);
			// Mostly boulders, and every third throw a flaming pot (the second kind it carries).
			if (lobs && fire && m_LaunchPhase == LaunchPhase::Loaded && m_LaunchAmmo.size() > 1) {
				m_LaunchAmmoIndex = m_AIThrows % 3 == 2 ? 1 : 0;
			}
		} else {
			m_GunTargetID = 0;
		}
	}
	if (!manned) {
		// Unmanned, it comes to rest pointing ahead.
		m_AimAngle += std::clamp(rotationFacing - m_AimAngle, -deltaTime, deltaTime);
		m_GunTargetID = 0;
	}
	m_AimAngle = std::clamp(m_AimAngle, lowest, highest);
	if (lobs) {
		UpdateThrowingArm(fire && manned);
		if (playerAims) {
			DrawLaunchArc();
		}
		return;
	}
	m_Turret->SetMountedDeviceRotationOffset((m_AimAngle * GetFlipFactor()) - m_Rotation.GetRadAngle());
	// (The AI's gunner holds fire with a friend in the way, out to what it shoots at or its range.)
	if (fire && !playerAims) {
		float reach = m_GunRange;
		if (const MovableObject* target = m_GunTargetID ? g_MovableMan.FindObjectByUniqueID(m_GunTargetID) : nullptr) {
			reach = std::min(reach, g_SceneMan.ShortestDistance(m_Turret->GetPos(), target->GetPos()).GetMagnitude());
		} else if (!m_SuppressPoint.IsZero()) {
			reach = std::min(reach, g_SceneMan.ShortestDistance(m_Turret->GetPos(), m_SuppressPoint).GetMagnitude());
		}
		const Vector ray(std::cos(m_AimAngle) * GetFlipFactor() * reach, -std::sin(m_AimAngle) * reach);
		fire = !FriendInLineOfFire(this, m_Team, m_Turret->GetPos(), ray);
	}
	for (HeldDevice* device: m_Turret->GetMountedDevices()) {
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

void AVehicle::UpdateThrowingArm(bool fire) {
	// How long each part of a throw takes: wound back, swung over, then let back down and loaded for the rest of the reload.
	constexpr float windTime = 450.0F;
	constexpr float throwTime = 220.0F;
	constexpr float lowerTime = 900.0F;
	constexpr float releaseAt = 0.7F;
	float swing = 0.0F;
	switch (m_LaunchPhase) {
		case LaunchPhase::Loaded:
			if (fire && !m_LaunchAmmo.empty()) {
				m_LaunchPhase = LaunchPhase::Winding;
				m_LaunchTimer.Reset();
			}
			break;
		case LaunchPhase::Winding: {
			// Drawn a little further back as the rope is wound tight.
			float progress = std::min(static_cast<float>(m_LaunchTimer.GetElapsedSimTimeMS()) / windTime, 1.0F);
			swing = 0.12F * progress;
			if (progress >= 1.0F) {
				m_LaunchPhase = LaunchPhase::Throwing;
				m_LaunchTimer.Reset();
				m_Released = false;
			}
			break;
		}
		case LaunchPhase::Throwing: {
			// Over it goes, quick at first: the load flies from the bucket most of the way round.
			float progress = std::min(static_cast<float>(m_LaunchTimer.GetElapsedSimTimeMS()) / throwTime, 1.0F);
			float eased = 1.0F - (1.0F - progress) * (1.0F - progress);
			swing = 0.12F - (m_ArmThrowAngle + 0.12F) * eased;
			if (!m_Released && eased >= releaseAt && !m_LaunchAmmo.empty()) {
				m_Released = true;
				const MovableObject* preset = m_LaunchAmmo[std::clamp(m_LaunchAmmoIndex, 0, static_cast<int>(m_LaunchAmmo.size()) - 1)];
				if (MovableObject* load = dynamic_cast<MovableObject*>(preset->Clone())) {
					load->SetPos(GetReleasePoint());
					load->SetVel(GetLaunchVelocity(m_AimAngle));
					load->SetAngularVel(RandomNum(-4.0F, 4.0F));
					load->SetTeam(m_Team);
					load->SetWhichMOToNotHit(this, 0.5F);
					g_MovableMan.AddMO(load);
				}
				// (The arm slams into its stop and the frame bucks.)
				m_Vel += Vector(-1.0F * GetFlipFactor(), -0.5F);
				if (!m_Controller.IsPlayerControlled()) {
					++m_AIThrows;
				}
			}
			if (progress >= 1.0F) {
				m_LaunchPhase = LaunchPhase::Reloading;
				m_LaunchTimer.Reset();
			}
			break;
		}
		case LaunchPhase::Reloading: {
			// Let back down to rest, then loaded again by the end of the reload.
			float progress = std::min(static_cast<float>(m_LaunchTimer.GetElapsedSimTimeMS()) / lowerTime, 1.0F);
			swing = -m_ArmThrowAngle * (1.0F - progress);
			if (m_LaunchTimer.IsPastSimMS(std::max(m_LaunchReloadTime - windTime - throwTime, lowerTime))) {
				m_LaunchPhase = LaunchPhase::Loaded;
			}
			break;
		}
	}
	m_ThrowingArm->SetInheritedRotAngleOffset(swing * GetFlipFactor());
}

bool AVehicle::LobAngleTo(const Vector& toTarget, float& angle) const {
	float x = toTarget.m_X * GetFlipFactor() * c_MPP;
	float y = -toTarget.m_Y * c_MPP;
	if (x <= 0.0F) {
		return false;
	}
	float v2 = m_LaunchSpeed * m_LaunchSpeed;
	float g = GetLaunchGravity();
	float discriminant = v2 * v2 - g * (g * x * x + 2.0F * y * v2);
	if (discriminant < 0.0F) {
		return false;
	}
	float rotationFacing = Wrapped(m_Rotation.GetRadAngle()) * GetFlipFactor();
	for (float sign: {1.0F, -1.0F}) {
		float lob = std::atan((v2 + sign * std::sqrt(discriminant)) / (g * x));
		if (lob - rotationFacing >= m_MinLaunchAngle && lob - rotationFacing <= m_MaxLaunchAngle) {
			angle = lob;
			return true;
		}
	}
	return false;
}

Vector AVehicle::GetReleasePoint() const {
	if (!m_ThrowingArm) {
		return m_Pos;
	}
	float armRotation = m_Rotation.GetRadAngle() - m_ArmThrowAngle * 0.7F * GetFlipFactor();
	return m_ThrowingArm->GetPos() + Vector(-m_ArmLength * GetFlipFactor(), 0.0F).GetRadRotatedCopy(armRotation);
}

Vector AVehicle::GetLaunchVelocity(float angle) const {
	return Vector(std::cos(angle) * GetFlipFactor(), -std::sin(angle)) * m_LaunchSpeed;
}

float AVehicle::GetLaunchGravity() const {
	float scalar = m_LaunchAmmo.empty() ? 1.0F : m_LaunchAmmo[std::clamp(m_LaunchAmmoIndex, 0, static_cast<int>(m_LaunchAmmo.size()) - 1)]->GetGlobalAccScalar();
	return std::max(g_SceneMan.GetGlobalAcc().m_Y * scalar, 0.1F);
}

void AVehicle::DrawLaunchArc() const {
	// Where it would fly, a dot a tenth of a second apart, to where it first meets the ground.
	static const unsigned char arcColor = static_cast<unsigned char>(Color(250, 220, 70).GetIndex());
	int player = m_Controller.GetPlayer();
	Vector position = GetReleasePoint();
	Vector velocity = GetLaunchVelocity(m_AimAngle);
	float gravity = GetLaunchGravity();
	constexpr float step = 0.025F;
	for (int i = 1; i <= 400; ++i) {
		velocity.m_Y += gravity * step;
		position += velocity * step * c_PPM;
		if (position.m_Y > static_cast<float>(g_SceneMan.GetSceneHeight())) {
			break;
		}
		Vector wrapped = position;
		g_SceneMan.WrapPosition(wrapped);
		if (g_SceneMan.GetTerrMatter(wrapped.GetFloorIntX(), wrapped.GetFloorIntY()) != g_MaterialAir) {
			g_PrimitiveMan.DrawCircleFillPrimitive(player, wrapped, 3, arcColor);
			break;
		}
		if (i % 4 == 0) {
			g_PrimitiveMan.DrawCircleFillPrimitive(player, wrapped, 1, arcColor);
		}
	}
}

void AVehicle::UpdateTerrainCrush() {
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	SLTerrain* terrain = g_SceneMan.GetTerrain();
	if (m_TerrainCrush <= 0.0F || deltaTime <= 0.0F || !terrain || m_Wheels.empty() || IsDead()) {
		return;
	}
	Vector along = Vector(1.0F, 0.0F).GetRadRotatedCopy(m_Rotation.GetRadAngle());
	float speedAlong = m_Vel.Dot(along);
	// Moving along, or driven into what is in the way (stopped against a wall, it would never get going again).
	float heading = std::abs(speedAlong) >= 1.0F ? (speedAlong > 0.0F ? 1.0F : -1.0F) : (GetWheelsOnGround() > 0 && m_Throttle != 0.0F && !m_Handbrake ? (m_Throttle > 0.0F ? 1.0F : -1.0F) : 0.0F);
	if (heading == 0.0F) {
		return;
	}
	// The way it is going, from its middle upright (RotateOffset mirrors a vehicle facing left, so its nose is +x either way).
	float forward = heading * GetFlipFactor();

	// The band it smashes, from its middle upright and facing right: from its nose (or tail) out as far as it goes this update and a little
	// more, and from its top down to its lowest axle, so the ground its wheels roll on, and the slope ahead they climb, is left.
	float nose = static_cast<float>(GetSpriteWidth()) * 0.5F;
	float axles = -static_cast<float>(GetSpriteHeight()) * 0.5F;
	for (const Wheel& wheel: m_Wheels) {
		if (wheel.Part) {
			nose = std::max(nose, std::abs(wheel.Mount.m_X) + static_cast<float>(wheel.Part->GetSpriteWidth()) * 0.5F);
			axles = std::max(axles, wheel.Mount.m_Y - wheel.Compression);
		}
	}
	float top = -static_cast<float>(GetSpriteHeight()) * 0.5F;
	float reach = 2.0F + std::abs(speedAlong) * deltaTime * c_PPM * 1.5F;
	float mass = std::max(GetMass(), 1.0F);

	// (A handful of the pixels fly as rubble; the rest are just gone, so a wall knocked down doesn't flood the scene with particles.)
	int smashed = 0;
	float slowing = 0.0F;
	constexpr int maxSmashed = 400;
	for (float out = nose - 1.0F; out <= nose + reach && smashed < maxSmashed; out += 1.0F) {
		for (float down = top; down <= axles - 1.0F && smashed < maxSmashed; down += 1.0F) {
			Vector at = m_Pos + RotateOffset(Vector(out * forward, down));
			int x = at.GetFloorIntX();
			int y = at.GetFloorIntY();
			if (!g_SceneMan.WrapPosition(x, y) && !g_SceneMan.IsWithinBounds(x, y)) {
				continue;
			}
			int material = terrain->GetMaterialPixel(x, y);
			if (material == g_MaterialAir || FluidSim::IsLiquid(material) || TerrainTrees::ActorsPass(material)) {
				continue;
			}
			const Material* kind = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(material));
			float toughness = kind ? kind->GetIntegrity() : -1.0F;
			if (toughness < 0.0F || toughness > m_TerrainCrush) {
				continue;
			}
			g_SceneMan.DislodgePixelBool(x, y, smashed % 6 != 0);
			++smashed;
			slowing += std::max(toughness, 1.0F);
		}
	}
	if (smashed > 0) {
		// Each pixel takes some of its way: a kilogram of push for each point of toughness, at most what stops it.
		float loss = std::min(slowing * 0.02F / mass, std::abs(speedAlong));
		m_Vel -= along * (loss * (speedAlong >= 0.0F ? 1.0F : -1.0F));
	}
}

void AVehicle::UpdateWheels() {
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	if (deltaTime <= 0.0F) {
		return;
	}
	float rotation = m_Rotation.GetRadAngle();
	Vector down = Vector(0.0F, 1.0F).GetRadRotatedCopy(rotation);
	Vector along = Vector(1.0F, 0.0F).GetRadRotatedCopy(rotation);
	float mass = std::max(GetMass(), 1.0F);
	float inertia = m_pAtomGroup ? m_pAtomGroup->GetMomentOfInertia() : mass;

	Vector velocityChange;
	float angularChange = 0.0F;
	float pushingUp = 0.0F; //!< The springs' push this update, as acceleration: how much weight is on the wheels, for their grip.
	int onGround = 0;
	float bottomedOut = 0.0F; //!< How far the deepest bottomed-out wheel is into the ground, in pixels.
	float bottomedShare = 0.0F; //!< How much of the cart's weight is on wheels that have bottomed out.
	int wheelCount = static_cast<int>(std::count_if(m_Wheels.begin(), m_Wheels.end(), [](const Wheel& wheel) { return wheel.Part != nullptr; }));
	float share = wheelCount == 0 ? 0.0F : 1.0F / static_cast<float>(wheelCount);

	for (Wheel& wheel: m_Wheels) {
		if (!wheel.Part) {
			continue;
		}
		float radius = std::max(static_cast<float>(wheel.Part->GetSpriteWidth()) * 0.5F, 1.0F);
		Vector top = m_Pos + RotateOffset(wheel.Mount) - down * m_SuspensionTravel;
		// Three looks down from the top of the spring's travel, across the wheel's width: how far down its middle can come before its rim
		// meets the ground. (One look under the middle drops it into every crack and onto every point.)
		float reach = m_SuspensionTravel + 2.0F;
		float lowest = reach;
		for (float across: {-0.6F, 0.0F, 0.6F}) {
			float side = across * radius;
			float rim = std::sqrt(radius * radius - side * side);
			Vector start = top + along * side;
			for (float step = 0.0F; step <= reach + rim; step += 1.0F) {
				if (HoldsWheel(start + down * step)) {
					lowest = std::min(lowest, step - rim);
					break;
				}
			}
		}
		float previous = wheel.Compression;
		wheel.OnGround = lowest <= m_SuspensionTravel;
		if (wheel.OnGround) {
			wheel.Compression = std::clamp(m_SuspensionTravel - lowest, 0.0F, m_SuspensionTravel);
			++onGround;
			// (Held to a sensible speed: set down with a wheel in the ground, the spring is all the way in at once, and would fling it up.)
			float squeeze = std::clamp((wheel.Compression - previous) * c_MPP / deltaTime, -3.0F, 3.0F);
			float push = share * (m_SuspensionStiffness * wheel.Compression * c_MPP + m_SuspensionDamping * squeeze);
			// Bottomed out with the ground still higher (a hard landing, driving into a step): the spring is solid (see below).
			if (lowest < 0.0F) {
				bottomedOut = std::max(bottomedOut, -lowest);
				bottomedShare += share;
			}
			push = std::max(push, 0.0F);
			pushingUp += push;
			Vector acceleration = down * -push;
			velocityChange += acceleration * deltaTime;
			// Pushing up off-centre turns the body: a wheel on a bump lifts that end.
			Vector arm = (top + down * m_SuspensionTravel - m_Pos) * c_MPP;
			angularChange += (arm.m_Y * acceleration.m_X - arm.m_X * acceleration.m_Y) * mass / inertia * deltaTime;
		} else {
			// Hanging: the spring lets the wheel down again.
			wheel.Compression = std::max(wheel.Compression - m_SuspensionTravel * 8.0F * deltaTime, 0.0F);
		}
	}

	// Driving, braking and rolling along the ground, through the wheels that are on it, as far as they grip.
	float speedAlong = m_Vel.Dot(along);
	if (onGround > 0) {
		float onGroundShare = static_cast<float>(onGround) * share;
		float grip = m_Grip * pushingUp;
		float stopping = std::abs(speedAlong) / deltaTime; // (No more than brings it to a stop, so it doesn't rock back and forth on the spot.)
		float direction = speedAlong >= 0.0F ? 1.0F : -1.0F;
		// Gravity's pull along the ground, which it gets on its next move: braking takes that off as well as its speed, or it creeps downhill.
		float slopePull = g_SceneMan.GetGlobalAcc().Dot(along) * m_GlobalAccScalar;
		float brake = m_BrakeStrength * onGroundShare;
		// Left alone and slow (parked, empty, or its driver idle), its wheels hold it where it stands, as far as the brake and grip go.
		// (Faster than that it coasts on, slowed by its rolling resistance.)
		bool holding = m_Throttle == 0.0F && std::abs(speedAlong) < c_HoldSpeed;
		float push = 0.0F;
		if (m_Throttle != 0.0F && m_Throttle * speedAlong >= -0.1F) {
			if (std::abs(speedAlong) < m_MaxSpeed) {
				push = std::min(m_Acceleration * onGroundShare * std::abs(m_Throttle), (m_MaxSpeed - std::abs(speedAlong)) / deltaTime) * (m_Throttle > 0.0F ? 1.0F : -1.0F);
			} else {
				// Running away downhill past its top speed: it brakes back down to it.
				push = -direction * std::min(brake, (std::abs(speedAlong) - m_MaxSpeed) / deltaTime + std::max(slopePull * direction, 0.0F));
			}
		} else if (m_Throttle != 0.0F || m_Braking || holding) {
			push = std::clamp(-direction * stopping - slopePull, -brake, brake);
		}
		push = std::clamp(push, -grip, grip);
		push -= direction * std::min(m_RollingResistance * onGroundShare, std::max(stopping - std::abs(push), 0.0F));
		velocityChange += along * push * deltaTime;
		// The body steadies on its springs rather than rocking on and on.
		m_AngularVel *= std::max(1.0F - 1.5F * onGroundShare * deltaTime, 0.0F);
	}

	// In liquid (VH-2 gives boats hulls): dragged, and held up a little; a cart full of iron fittings sinks slowly.
	int middleMaterial = g_SceneMan.GetTerrMatter(m_Pos.GetFloorIntX(), m_Pos.GetFloorIntY());
	if (m_HullPoints.empty() && FluidSim::IsEnabled() && FluidSim::IsLiquid(middleMaterial)) {
		m_Vel *= std::max(1.0F - 2.0F * deltaTime, 0.0F);
		m_AngularVel *= std::max(1.0F - 2.0F * deltaTime, 0.0F);
		velocityChange.m_Y -= g_SceneMan.GetGlobalAcc().m_Y * m_Buoyancy * deltaTime;
	}

	m_Vel += velocityChange;
	m_AngularVel += angularChange;

	// A bottomed-out spring is solid: the body goes no further down onto it, and is lifted back out of the ground as far as the wheel went in.
	// Without this a hard landing or a step drives the body down onto the ground, and a heavy one digs itself in to soft ground and sticks.
	if (bottomedShare > 0.0F) {
		float into = m_Vel.Dot(down);
		// Too hard a landing (a long fall, a ram into a wall on its wheels): it breaks apart.
		if (m_BreakLandingSpeed > 0.0F && into > m_BreakLandingSpeed) {
			m_BreakNow = true;
		}
		if (into > 0.0F) {
			m_Vel -= down * into * std::min(bottomedShare, 1.0F);
		}
		m_Pos -= down * std::min(bottomedOut, 2.0F) * std::min(bottomedShare, 1.0F);
	}

	// The wheels ride up on their springs and turn as far as they've rolled.
	speedAlong = m_Vel.Dot(along);
	if (m_Tracks && m_Tracks->GetFrameCount() > 1) {
		// (Run round the way they run driving forward, which is the way it faces: mirrored with it.)
		if (onGround > 0) {
			m_TrackTravel += speedAlong * GetFlipFactor() * c_PPM * deltaTime;
		}
		int frames = static_cast<int>(m_Tracks->GetFrameCount());
		int frame = static_cast<int>(std::floor(m_TrackTravel / std::max(m_TrackFrameLength, 0.1F)));
		m_TrackTravel = std::fmod(m_TrackTravel, static_cast<float>(frames) * std::max(m_TrackFrameLength, 0.1F));
		m_Tracks->SetFrame(static_cast<unsigned int>(((frame % frames) + frames) % frames));
	}
	for (Wheel& wheel: m_Wheels) {
		if (!wheel.Part) {
			// A bare strut: its wheel shot off, it hangs all the way out.
			wheel.Compression = std::max(wheel.Compression - m_SuspensionTravel * 8.0F * deltaTime, 0.0F);
			if (wheel.Strut) {
				wheel.Strut->SetParentOffset(wheel.Mount - Vector(0.0F, wheel.Compression));
			}
			continue;
		}
		float radius = std::max(static_cast<float>(wheel.Part->GetSpriteWidth()) * 0.5F, 1.0F);
		if (wheel.OnGround) {
			wheel.SpinSpeed = -speedAlong * c_PPM / radius;
		} else {
			wheel.SpinSpeed *= std::max(1.0F - 0.6F * deltaTime, 0.0F);
		}
		wheel.Spin = Wrapped(wheel.Spin + wheel.SpinSpeed * deltaTime);
		wheel.Part->SetParentOffset(wheel.Mount - Vector(0.0F, wheel.Compression));
		if (wheel.Strut) {
			wheel.Strut->SetParentOffset(wheel.Mount - Vector(0.0F, wheel.Compression));
		}
		// Rolling or landing on plants flattens them (a wheel standing still on one leaves it be, so a parked cart doesn't eat what it's in).
		if (wheel.OnGround && (std::abs(wheel.SpinSpeed) > 0.5F || m_Vel.MagnitudeIsGreaterThan(1.0F))) {
			CrushPlants(m_Pos + RotateOffset(wheel.Mount - Vector(0.0F, wheel.Compression)), radius + 1.0F);
		}
		wheel.Part->SetInheritedRotAngleOffset(wheel.Spin * GetFlipFactor());
	}
}

bool AVehicle::UpdateHull() {
	m_Submerged = 0.0F;
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	if (m_HullPoints.empty() || deltaTime <= 0.0F) {
		return false;
	}
	float rotation = m_Rotation.GetRadAngle();
	Vector along = Vector(1.0F, 0.0F).GetRadRotatedCopy(rotation);
	float mass = std::max(GetMass(), 1.0F);
	float inertia = m_pAtomGroup ? m_pAtomGroup->GetMomentOfInertia() : mass;
	float gravity = g_SceneMan.GetGlobalAcc().m_Y;
	float share = 1.0F / static_cast<float>(m_HullPoints.size());
	int draft = std::max(static_cast<int>(m_HullDraft), 1);

	/// How much of the column of the draft's height above a point is liquid, 0 to 1, and which liquid is at the top of it (0 if none).
	auto depthAt = [draft](const Vector& point, int* liquidAtTop = nullptr) {
		int x = point.GetFloorIntX();
		int y = point.GetFloorIntY();
		int depth = 0;
		for (int up = 0; up < draft; ++up) {
			int material = g_SceneMan.GetTerrMatter(x, y - up);
			if (!FluidSim::IsLiquid(material)) {
				// (Up through the hull's own space only: air or ground above ends the column.)
				if (up > 0 || material == g_MaterialAir) {
					break;
				}
				continue;
			}
			++depth;
			if (liquidAtTop) {
				*liquidAtTop = material;
			}
		}
		return static_cast<float>(depth) / static_cast<float>(draft);
	};

	// Each point is held up by as much of its column as is under: a hull floats at its waterline, its ends lifted as they dip, so it rides level on
	// the water and pitches with the waves.
	Vector velocityChange;
	float angularChange = 0.0F;
	for (const Vector& point: m_HullPoints) {
		Vector world = m_Pos + RotateOffset(point);
		float depth = depthAt(world);
		if (depth <= 0.0F) {
			continue;
		}
		m_Submerged += depth * share;
		Vector acceleration(0.0F, -gravity * m_Buoyancy * depth * share);
		velocityChange += acceleration * deltaTime;
		Vector arm = (world - m_Pos) * c_MPP;
		angularChange += (arm.m_Y * acceleration.m_X - arm.m_X * acceleration.m_Y) * mass / inertia * deltaTime;
	}
	if (m_Submerged <= 0.0F) {
		return false;
	}
	m_Vel += velocityChange;
	m_AngularVel += angularChange;

	// Water slows it: a little going along the way the hull points, a good deal more side on or bobbing up and down, and its rocking dies away.
	// The keel keeps it the right way up, as a hull rights itself on the water.
	float speedAlong = m_Vel.Dot(along);
	Vector across = m_Vel - along * speedAlong;
	speedAlong *= std::max(1.0F - m_WaterDrag * m_Submerged * deltaTime, 0.0F);
	across *= std::max(1.0F - 3.0F * m_Submerged * deltaTime, 0.0F);
	m_Vel = along * speedAlong + across;
	m_AngularVel *= std::max(1.0F - 2.5F * m_Submerged * deltaTime, 0.0F);
	// (Going fast bow first, a planing hull rides with its bow up: it is kept at that trim instead of level.)
	float forward = std::clamp(speedAlong * GetFlipFactor() / std::max(m_WaterMaxSpeed, 0.1F), 0.0F, 1.0F);
	float trim = m_PlaningTrim * forward * GetFlipFactor();
	m_AngularVel -= Wrapped(rotation - trim) * 6.0F * m_Submerged * deltaTime;

	// Driven along by the oars or the motor, if the blades or the propeller are in the water.
	bool rowing = m_Throttle != 0.0F && m_WaterThrust > 0.0F;
	Vector propeller = m_Pos + RotateOffset(m_PropellerOffset);
	int liquid = 0;
	bool pushing = rowing && (m_HasPropeller ? depthAt(propeller, &liquid) > 0.0F : true);
	float power = 1.0F;
	if (m_RowingStroke > 0.0F) {
		// In strokes: the blades pull through the first half and come back through the air in the second.
		float before = m_StrokePhase;
		if (rowing || std::abs(Wrapped(m_StrokePhase)) > 0.2F) {
			m_StrokePhase = Wrapped(m_StrokePhase + c_TwoPI * deltaTime * 1000.0F / m_RowingStroke);
		}
		power = std::max(std::sin(m_StrokePhase), 0.0F) * 1.6F;
		// Each blade's dip leaves froth where it goes in.
		if (pushing && before < 0.0F && m_StrokePhase >= 0.0F && m_Oar) {
			Vector blade = m_Oar->GetPos() + Vector(0.0F, static_cast<float>(m_Oar->GetSpriteHeight()) * 0.4F).GetRadRotatedCopy(m_Oar->GetRotAngle());
			FluidSim::Froth(blade, 6.0F, 2, 0);
		}
	}
	if (pushing && std::abs(speedAlong) < m_WaterMaxSpeed) {
		float push = std::min(m_WaterThrust * std::abs(m_Throttle) * power, (m_WaterMaxSpeed - std::abs(speedAlong)) / deltaTime);
		m_Vel += along * push * (m_Throttle > 0.0F ? 1.0F : -1.0F) * deltaTime;
		// A motor churns the water behind it.
		if (m_RowingStroke <= 0.0F && m_WakeTimer.IsPastSimMS(120)) {
			FluidSim::Froth(propeller, 8.0F, 2, 0);
			m_WakeTimer.Reset();
		}
	} else if (m_Braking) {
		m_Vel -= along * speedAlong * std::min(2.0F * deltaTime, 1.0F);
	}
	// Spray thrown off the bow going fast.
	if (std::abs(speedAlong) > m_WaterMaxSpeed * 0.6F && m_WakeTimer.IsPastSimMS(250)) {
		// (At the hull point farthest forward the way it is going.)
		Vector bow = m_Pos;
		float farthest = -1.0E6F;
		for (const Vector& point: m_HullPoints) {
			Vector world = RotateOffset(point);
			if (float forward = world.Dot(along) * (speedAlong > 0.0F ? 1.0F : -1.0F); forward > farthest) {
				farthest = forward;
				bow = m_Pos + world;
			}
		}
		FluidSim::VisualSplash(bow, 6.0F, std::abs(speedAlong) * 0.5F, 0);
		m_WakeTimer.Reset();
	}
	return true;
}

float AVehicle::GetSunkFraction() const {
	if (!m_pAtomGroup || m_pAtomGroup->GetAtomList().empty()) {
		return 0.0F;
	}
	int sunk = 0;
	for (const Atom* atom: m_pAtomGroup->GetAtomList()) {
		if (HoldsWheel(m_Pos + RotateOffset(atom->GetOffset()))) {
			++sunk;
		}
	}
	return static_cast<float>(sunk) / static_cast<float>(m_pAtomGroup->GetAtomList().size());
}

void AVehicle::Update() {
	ZoneScoped;

	Actor::Update();

	m_Crew.UpdateSeatedCrew();
	// (Nobody left in it: there's no unit to command, and it is free for AI units to take again.)
	if (m_Commanded && m_Crew.IsEmpty()) {
		m_Commanded = false;
	}

	// A knock hard enough in one update (a crash into a wall, a tumble down a cliff) throws everyone out.
	if (m_CrewThrowSpeed > 0.0F && !m_Crew.IsEmpty() && (m_Vel - m_Crew.GetLastVel()).MagnitudeIsGreaterThan(m_CrewThrowSpeed)) {
		EjectCrew(true);
	}

	bool canDrive = (HasDriver() || !m_NeedsDriver) && m_Status != DYING && m_Status != DEAD && m_Status != INACTIVE;
	// (The player in another seat, the gunner's or a passenger's, doesn't drive: the driver is left to it.)
	bool steering = canDrive && (!m_Controller.IsPlayerControlled() || GetControlSeat() == 0 || !m_NeedsDriver);
	bool left = steering && m_Controller.IsState(MOVE_LEFT);
	bool right = steering && m_Controller.IsState(MOVE_RIGHT);
	m_Throttle = left == right ? 0.0F : (right ? 1.0F : -1.0F);
	m_Braking = steering && (m_Controller.IsState(MOVE_DOWN) || m_Controller.IsState(BODY_CROUCH));
	// With no player in charge, its driver drives it where it was sent.
	m_AIHop = false;
	m_AIKeepFacing = false;
	if (canDrive && !m_Controller.IsPlayerControlled()) {
		UpdateAIDriving();
	}
	// The handbrake holds it whoever drives: the wheels braked, the throttle let go of (its player told so if they try to drive).
	m_HandbrakeHeldAgainst = m_Handbrake && m_Throttle != 0.0F && m_Controller.IsPlayerControlled();
	if (m_Handbrake) {
		m_Throttle = 0.0F;
		m_Braking = true;
	}
	// Facing the way it's driven, as a unit turns to walk: only when slow, so it doesn't flip round while braking from speed.
	// (Not while it backs away from a fight facing the enemy, UpdateAIManoeuvre.)
	if (m_Throttle != 0.0F && (m_Throttle > 0.0F) == m_HFlipped && m_Vel.MagnitudeIsLessThan(1.5F) && !m_AIKeepFacing) {
		SetHFlipped(!m_HFlipped);
	}

	// Jumping: with at least half its wheels down, its springs throw it up off the ground, along the way it stands.
	if (((steering && m_Controller.IsState(BODY_JUMPSTART)) || (canDrive && m_AIHop)) && m_HopSpeed > 0.0F && m_HopTimer.IsPastSimMS(1000)) {
		if (std::vector<Attachable*> wheels = GetWheels(); !wheels.empty() && GetWheelsOnGround() * 2 >= static_cast<int>(wheels.size())) {
			m_Vel += Vector(0.0F, -m_HopSpeed).GetRadRotatedCopy(m_Rotation.GetRadAngle());
			m_HopTimer.Reset();
		}
	}

	UpdateEngine(canDrive);
	UpdateWheels();
	UpdateTerrainCrush();
	UpdateHull();
	if (m_Oar) {
		// Back and forth with the stroke: the blade goes from forward to back as it pulls, and forward again through the air.
		float swing = m_RowingStroke > 0.0F ? std::cos(m_StrokePhase) * m_OarSweep : 0.0F;
		m_Oar->SetInheritedRotAngleOffset(swing * GetFlipFactor());
	}
	UpdateGun();
	UpdateBoarding();
	UpdateAIBoarding();
	UpdateAIUnloading();

	// Sunk well into the ground (driven or dropped through it somehow): it breaks apart rather than lie stuck in it.
	if (m_BreakSunkFraction > 0.0F && GetSunkFraction() > m_BreakSunkFraction) {
		if (m_SunkTimer.IsPastSimMS(250)) {
			m_BreakNow = true;
		}
	} else {
		m_SunkTimer.Reset();
	}
	if (m_BreakNow && m_Status != DYING && m_Status != DEAD) {
		m_BreakNow = false;
		GibThis();
		return;
	}
	// Killed rather than blown apart (its health run out before its wounds or a knock broke it): dead, it breaks into its wreck all the same, so
	// the chassis is always left, not the dead vehicle lying there till it settles into the ground or is cleared away.
	if (m_Status == DEAD && !m_Gibs.empty() && !IsSetToDelete()) {
		GibThis();
		return;
	}

	// Rolled onto its roof: everyone falls out of an open vehicle.
	float rotation = Wrapped(m_Rotation.GetRadAngle());
	if (m_ThrowsCrewWhenFlipped && std::abs(rotation) > 2.0F && !m_Crew.IsEmpty()) {
		EjectCrew(true);
		canDrive = (HasDriver() || !m_NeedsDriver) && m_Status != DYING && m_Status != DEAD && m_Status != INACTIVE;
	}

	// On its side or roof and still: the driver rocks it back over, after a moment.
	if (std::abs(rotation) < 1.2F || !m_Vel.MagnitudeIsLessThan(1.0F) || !canDrive) {
		m_UpsideDownTimer.Reset();
	} else if (m_UpsideDownTimer.IsPastSimMS(2000)) {
		m_AngularVel = rotation > 0.0F ? -5.0F : 5.0F;
		m_Vel.m_Y -= 4.0F;
		m_UpsideDownTimer.Reset();
	}

	m_ViewPoint = m_Pos.GetFloored();
	if (m_Vel.MagnitudeIsGreaterThan(3.0F)) {
		m_ViewPoint += m_Vel * 4.0F;
	}

	m_Crew.SetLastVel(m_Vel);
	m_Crew.PlaceCrew();
	m_Crew.UpdatePassengerFire();
}

void AVehicle::Draw(BITMAP* pTargetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const {
	// Everyone in it first, so the body's near side hides their legs: sat in it, not stood on it. (And on the MOID layer, so shots hit them.)
	m_Crew.Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
	Actor::Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
}

void AVehicle::Draw(const Camera& camera) const {
	m_Crew.Draw(camera);
	Actor::Draw(camera);
}

void AVehicle::UpdateChildMOIDs(std::vector<MovableObject*>& MOIDIndex, MOID rootMOID, bool makeNewMOID) {
	Actor::UpdateChildMOIDs(MOIDIndex, rootMOID, makeNewMOID);
	// (Each its own root, as the separate units they are: a shot that hits one hurts them, not the vehicle.)
	m_Crew.UpdateMOIDs(MOIDIndex);
}

void AVehicle::GetMOIDs(std::vector<MOID>& MOIDs) const {
	Actor::GetMOIDs(MOIDs);
	m_Crew.GetMOIDs(MOIDs);
}

void AVehicle::DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos, int whichScreen, bool playerControlled) {
	Actor::DrawHUD(pTargetBitmap, targetPos, whichScreen, playerControlled);

	Activity* activity = g_ActivityMan.GetActivity();
	GUIFont* font = g_FrameMan.GetSmallFont();
	if (!activity || !font || !m_HUDVisible) {
		return;
	}
	// The gun's reticle and rounds, to the player laying it.
	if (m_Turret && m_Turret->IsAttached() && m_Controller.IsPlayerControlled() && !m_Controller.IsState(PIE_MENU_ACTIVE) && activity->ScreenOfPlayer(m_Controller.GetPlayer()) == whichScreen) {
		int gunnerSeat = std::max(m_Crew.GetSeatOfGun(0), 0);
		if (GetControlSeat() == gunnerSeat && GetSeatOccupant(gunnerSeat)) {
			Vector drawPos = m_Pos - targetPos;
			if (!targetPos.IsZero()) {
				drawPos += g_SceneMan.GetWrapToScreen(drawPos, pTargetBitmap->w, pTargetBitmap->h);
			}
			m_Turret->DrawGunnerHUD(pTargetBitmap, targetPos, drawPos, m_HUDStack);
		}
	}
	const char* hint = nullptr;
	if (Actor* boarder = m_Crew.GetBoarderInReach(); boarder && activity->ScreenOfPlayer(boarder->GetController()->GetPlayer()) == whichScreen) {
		hint = m_Crew.GetBoarderPushes() ? "Pick up: push it back over" : "Pick up: get in";
	} else if (GetControlSeat() >= 0 && m_Controller.IsPlayerControlled() && activity->ScreenOfPlayer(m_Controller.GetPlayer()) == whichScreen && !m_Crew.GetBoardingTimer().IsPastSimMS(4000)) {
		int seat = GetControlSeat();
		if (seat == 0) {
			hint = m_HopSpeed > 0.0F ? "Pick up: get out   Jump: hop" : "Pick up: get out";
		} else if (IsGunnerSeat(seat)) {
			hint = "Gunner: aim and fire   Pick up: get out";
		} else {
			hint = "Passenger   Pick up: get out";
		}
	}
	// A catapult's crew is told what is loaded, or that it is being loaded.
	std::string loadLine;
	if (m_ThrowingArm && !m_LaunchAmmo.empty() && GetControlSeat() >= 0 && m_Controller.IsPlayerControlled() && activity->ScreenOfPlayer(m_Controller.GetPlayer()) == whichScreen) {
		loadLine = m_LaunchPhase == LaunchPhase::Loaded ? "Loaded: " + GetLaunchAmmoName() + (m_LaunchAmmo.size() > 1 ? "   (Next/Prev. Device to change)" : "") : std::string("Winding and loading...");
		Vector drawPos = m_Pos - targetPos;
		if (!targetPos.IsZero()) {
			drawPos += g_SceneMan.GetWrapToScreen(drawPos, pTargetBitmap->w, pTargetBitmap->h);
		}
		AllegroBitmap bitmap(pTargetBitmap);
		font->DrawAligned(&bitmap, drawPos.GetFloorIntX(), drawPos.GetFloorIntY() - static_cast<int>(m_CharHeight * 0.5F) - 26, loadLine, GUIFont::Centre);
	}
	// The handbrake, to its player: always that it is on, and how to let it off when they try to drive.
	if (m_Handbrake && GetControlSeat() >= 0 && m_Controller.IsPlayerControlled() && activity->ScreenOfPlayer(m_Controller.GetPlayer()) == whichScreen) {
		Vector drawPos = m_Pos - targetPos;
		if (!targetPos.IsZero()) {
			drawPos += g_SceneMan.GetWrapToScreen(drawPos, pTargetBitmap->w, pTargetBitmap->h);
		}
		AllegroBitmap bitmap(pTargetBitmap);
		font->DrawAligned(&bitmap, drawPos.GetFloorIntX(), drawPos.GetFloorIntY() - static_cast<int>(m_CharHeight * 0.5F) - (loadLine.empty() ? 26 : 38), m_HandbrakeHeldAgainst ? "Handbrake on: right click to let it off" : "Handbrake on", GUIFont::Centre);
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

void AVehicle::DrawHUD(const Camera& camera) {}
