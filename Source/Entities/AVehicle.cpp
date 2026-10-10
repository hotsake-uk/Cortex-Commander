#include "AVehicle.h"

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
#include "Sandbox.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "SoundContainer.h"
#include "TimerMan.h"
#include "Turret.h"

#include "HeldDevice.h"

#include "GUI.h"
#include "AllegroBitmap.h"

#include "tracy/Tracy.hpp"

#include <algorithm>
#include <array>
#include <cmath>

using namespace RTE;

ConcreteClassInfo(AVehicle, Actor, 20);

namespace {
	constexpr double c_BoardingDelayMS = 400.0; //!< After getting in or out, how long before the same key does the opposite.

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
	m_SeatOffset.Reset();
	m_ExitOffset.SetXY(20.0F, -10.0F);
	m_BoardingReach = 24.0F;
	m_NeedsDriver = true;
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
	m_Seats.clear();
	m_PlayerSeat = -1;
	m_Turret = nullptr;
	m_GunRange = 600.0F;
	m_GunTurnSpeed = 2.5F;
	m_GunTargetID = 0;
	m_GunTargetTimer.Reset();
	m_CrewThrowSpeed = 14.0F;
	m_ThrowsCrewWhenFlipped = true;
	m_LastVel.Reset();
	m_Driver = nullptr;
	m_Throttle = 0.0F;
	m_Braking = false;
	m_BoardingTimer.Reset();
	m_UpsideDownTimer.Reset();
	m_BoarderInReach = nullptr;
	m_BoarderPushes = false;
	m_Buoyancy = 0.8F;
	m_BreakLandingSpeed = 0.0F;
	m_BreakSunkFraction = 0.0F;
	m_BreakNow = false;
	m_SunkTimer.Reset();
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
	for (const Seat& seat: reference.m_Seats) {
		m_Seats.push_back(seat);
		m_Seats.back().Occupant = seat.Occupant ? dynamic_cast<Actor*>(seat.Occupant->Clone()) : nullptr;
	}
	m_PlayerSeat = reference.m_PlayerSeat;
	m_GunRange = reference.m_GunRange;
	m_GunTurnSpeed = reference.m_GunTurnSpeed;
	m_CrewThrowSpeed = reference.m_CrewThrowSpeed;
	m_ThrowsCrewWhenFlipped = reference.m_ThrowsCrewWhenFlipped;
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
	m_SuspensionTravel = reference.m_SuspensionTravel;
	m_SuspensionStiffness = reference.m_SuspensionStiffness;
	m_SuspensionDamping = reference.m_SuspensionDamping;
	m_Acceleration = reference.m_Acceleration;
	m_BrakeStrength = reference.m_BrakeStrength;
	m_RollingResistance = reference.m_RollingResistance;
	m_MaxSpeed = reference.m_MaxSpeed;
	m_Grip = reference.m_Grip;
	m_SeatOffset = reference.m_SeatOffset;
	m_ExitOffset = reference.m_ExitOffset;
	m_BoardingReach = reference.m_BoardingReach;
	m_NeedsDriver = reference.m_NeedsDriver;
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
	if (reference.m_Driver) {
		m_Driver = dynamic_cast<Actor*>(reference.m_Driver->Clone());
	}

	return 0;
}

void AVehicle::Destroy(bool notInherited) {
	delete m_Driver;
	m_Driver = nullptr;
	for (Seat& seat: m_Seats) {
		delete seat.Occupant;
		seat.Occupant = nullptr;
	}
	if (m_Turret) {
		m_HardcodedAttachableUniqueIDsAndRemovers.erase(m_Turret->GetUniqueID());
	}
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
	MatchProperty("RollingResistance", { reader >> m_RollingResistance; });
	MatchProperty("MaxSpeed", { reader >> m_MaxSpeed; });
	MatchProperty("Grip", { reader >> m_Grip; });
	MatchProperty("SeatOffset", { reader >> m_SeatOffset; });
	MatchProperty("ExitOffset", { reader >> m_ExitOffset; });
	MatchProperty("BoardingReach", { reader >> m_BoardingReach; });
	MatchProperty("NeedsDriver", { reader >> m_NeedsDriver; });
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
	});
	MatchProperty("Exhaust", { SetExhaust(dynamic_cast<AEmitter*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchForwards("AddSeat") MatchProperty("AddGunnerSeat", {
		Seat seat;
		reader >> seat.Offset;
		seat.Gunner = propName == "AddGunnerSeat";
		m_Seats.push_back(seat);
	});
	MatchProperty("Turret", { SetTurret(dynamic_cast<Turret*>(g_PresetMan.ReadReflectedPreset(reader))); });
	MatchProperty("GunRange", { reader >> m_GunRange; });
	MatchProperty("GunTurnSpeed", { reader >> m_GunTurnSpeed; });
	MatchProperty("CrewThrowSpeed", { reader >> m_CrewThrowSpeed; });
	MatchProperty("ThrowsCrewWhenFlipped", { reader >> m_ThrowsCrewWhenFlipped; });
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
	writer.NewPropertyWithValue("RollingResistance", m_RollingResistance);
	writer.NewPropertyWithValue("MaxSpeed", m_MaxSpeed);
	writer.NewPropertyWithValue("Grip", m_Grip);
	writer.NewPropertyWithValue("SeatOffset", m_SeatOffset);
	writer.NewPropertyWithValue("ExitOffset", m_ExitOffset);
	writer.NewPropertyWithValue("BoardingReach", m_BoardingReach);
	writer.NewPropertyWithValue("NeedsDriver", m_NeedsDriver);
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
	for (const Seat& seat: m_Seats) {
		writer.NewPropertyWithValue(seat.Gunner ? "AddGunnerSeat" : "AddSeat", seat.Offset);
	}
	if (m_Turret) {
		writer.NewProperty("Turret");
		writer << m_Turret;
	}
	writer.NewPropertyWithValue("GunRange", m_GunRange);
	writer.NewPropertyWithValue("GunTurnSpeed", m_GunTurnSpeed);
	writer.NewPropertyWithValue("CrewThrowSpeed", m_CrewThrowSpeed);
	writer.NewPropertyWithValue("ThrowsCrewWhenFlipped", m_ThrowsCrewWhenFlipped);
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
	return m_Driver ? m_Pos + RotateOffset(m_SeatOffset - Vector(0.0F, m_Driver->GetHeight() * 0.3F)) : m_Pos;
}

Actor* AVehicle::GetSeatOccupant(int seat) const {
	if (seat == 0) {
		return m_Driver;
	}
	return seat >= 1 && seat <= static_cast<int>(m_Seats.size()) ? m_Seats[seat - 1].Occupant : nullptr;
}

void AVehicle::SetSeatOccupant(int seat, Actor* unit) {
	if (seat == 0) {
		m_Driver = unit;
	} else if (seat >= 1 && seat <= static_cast<int>(m_Seats.size())) {
		m_Seats[seat - 1].Occupant = unit;
	}
}

int AVehicle::GetCrewCount() const {
	int count = m_Driver ? 1 : 0;
	for (const Seat& seat: m_Seats) {
		count += seat.Occupant ? 1 : 0;
	}
	return count;
}

int AVehicle::GetFreeSeat() const {
	if (!m_Driver) {
		return 0;
	}
	// A gunner's seat before a passenger's: whoever gets in second mans the gun.
	for (bool gunner: {true, false}) {
		for (int seat = 1; seat <= static_cast<int>(m_Seats.size()); ++seat) {
			if (m_Seats[seat - 1].Gunner == gunner && !m_Seats[seat - 1].Occupant) {
				return seat;
			}
		}
	}
	return -1;
}

int AVehicle::GetControlSeat() const {
	if (m_PlayerSeat >= 0 && GetSeatOccupant(m_PlayerSeat)) {
		return m_PlayerSeat;
	}
	return m_Driver ? 0 : -1;
}

bool AVehicle::CanCarry(const Actor* unit) const {
	if (!unit || unit == this || unit->IsDead() || unit->IsSetToDelete() || unit->GetTeam() != m_Team || !dynamic_cast<const AHuman*>(unit) || unit->IsInGroup("Brains") || IsDead()) {
		return false;
	}
	if (const Activity* activity = g_ActivityMan.GetActivity()) {
		for (int player = Players::PlayerOne; player < Players::MaxPlayerCount; ++player) {
			// (A brain that walks about, as in the Sandbox's units, stays out: the side would lose with it gone from the scene.)
			if (activity->GetPlayerBrain(player) == unit) {
				return false;
			}
		}
	}
	return true;
}

bool AVehicle::TakeDriver(Actor* unit) {
	return !m_Driver && TakeSeat(unit, 0);
}

bool AVehicle::TakeSeat(Actor* unit, int seat) {
	if (seat < 0) {
		seat = GetFreeSeat();
	}
	if (seat < 0 || seat >= GetSeatCount() || GetSeatOccupant(seat) || !CanCarry(unit)) {
		return false;
	}
	bool playerUnit = unit->GetController()->IsPlayerControlled();
	// (One player at a time: a unit of another player's can't climb into a vehicle someone else is in charge of.)
	if (playerUnit && m_Controller.IsPlayerControlled() && m_Controller.GetPlayer() != unit->GetController()->GetPlayer()) {
		return false;
	}
	// As a craft takes in who boards it: a copy is kept, the unit itself is deleted from the scene, and the side isn't counted a loss for it.
	Actor* occupant = dynamic_cast<Actor*>(unit->Clone());
	if (!occupant) {
		return false;
	}
	occupant->GetController()->SetInputMode(Controller::CIM_AI);
	occupant->SetVel(Vector());
	SetSeatOccupant(seat, occupant);
	unit->SetToDelete(true);
	if (Activity* activity = g_ActivityMan.GetActivity()) {
		activity->ReportDeath(unit->GetTeam(), -1);
		if (playerUnit) {
			activity->SwitchToActor(this, unit->GetController()->GetPlayer(), m_Team);
			m_PlayerSeat = seat;
		}
	}
	// (The Sandbox keeps its own hold on the unit you control and on your character: they go with it into the seat.)
	Sandbox::OnUnitBoarded(unit, this);
	m_BoardingTimer.Reset();
	PlaceCrew();
	// (Said by the vehicle, where they now sit: they are out of the scene while in here.)
	Say("BoardVehicle");
	return true;
}

Actor* AVehicle::EjectSeat(int seat, bool thrown) {
	Actor* occupant = GetSeatOccupant(seat);
	if (!occupant) {
		return nullptr;
	}
	// Out on the side it faces, or the other, or straight up out of the seat if both are blocked; thrown out, straight up first.
	Vector above = m_Pos + RotateOffset(GetSeatOffsetOf(seat)) - Vector(0.0F, occupant->GetHeight() * 0.5F);
	std::array<Vector, 3> exits = {m_Pos + RotateOffset(m_ExitOffset), m_Pos + RotateOffset(Vector(-m_ExitOffset.m_X, m_ExitOffset.m_Y)), above};
	if (thrown) {
		std::swap(exits[0], exits[2]);
	}
	occupant->SetRotAngle(0.0F);
	occupant->SetAngularVel(0.0F);
	const AtomGroup* atoms = occupant->GetAtomGroup();
	const Vector* out = nullptr;
	for (const Vector& exit: exits) {
		if (!atoms || atoms->FitsAt(exit)) {
			out = &exit;
			break;
		}
	}
	if (!out) {
		if (!IsDead() && !thrown) {
			return nullptr;
		}
		// (Wrecked or thrown: out on top whatever is there, rather than lost with it.)
		out = &above;
	}
	int controlSeat = GetControlSeat();
	SetSeatOccupant(seat, nullptr);
	occupant->SetPos(*out);
	if (thrown) {
		// Flung on with the speed it had before the knock, and up and out of the seat.
		occupant->SetVel(m_LastVel * 0.8F + Vector(RandomNum(-2.0F, 2.0F), -4.0F));
		occupant->SetAngularVel(RandomNum(-6.0F, 6.0F));
	} else {
		occupant->SetVel(m_Vel + Vector(0.0F, -2.0F));
	}
	occupant->SetWhichMOToNotHit(this, 0.5F);
	SetWhichMOToNotHit(occupant, 0.5F);
	occupant->ResetAllTimers();
	g_MovableMan.AddActor(occupant);
	if (Activity* activity = g_ActivityMan.GetActivity(); activity && seat == controlSeat && m_Controller.IsPlayerControlled() && activity->GetControlledActor(m_Controller.GetPlayer()) == this) {
		activity->SwitchToActor(occupant, m_Controller.GetPlayer(), occupant->GetTeam());
	}
	if (seat == m_PlayerSeat) {
		m_PlayerSeat = -1;
	}
	Sandbox::OnUnitLeftVehicle(this, occupant);
	m_BoardingTimer.Reset();
	occupant->Say(thrown || IsDead() || m_Health <= 0.0F ? "BailOut" : "LeaveVehicle");
	return occupant;
}

void AVehicle::EjectCrew(bool thrown) {
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		EjectSeat(seat, thrown);
	}
}

bool AVehicle::ChangeSeat() {
	int from = GetControlSeat();
	if (from < 0) {
		return false;
	}
	for (int step = 1; step < GetSeatCount(); ++step) {
		int to = (from + step) % GetSeatCount();
		if (!GetSeatOccupant(to)) {
			SetSeatOccupant(to, GetSeatOccupant(from));
			SetSeatOccupant(from, nullptr);
			m_PlayerSeat = to;
			m_BoardingTimer.Reset();
			PlaceCrew();
			return true;
		}
	}
	return false;
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
	Actor::GibThis(impactImpulse, movableObjectToIgnore);
}

void AVehicle::PlaceCrew() const {
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		if (Actor* unit = GetSeatOccupant(seat)) {
			unit->SetPos(m_Pos + RotateOffset(GetSeatOffsetOf(seat)));
			unit->SetVel(m_Vel);
			unit->SetRotAngle(m_Rotation.GetRadAngle());
			unit->SetHFlipped(m_HFlipped);
			unit->CorrectAttachableAndWoundPositionsAndRotations();
		}
	}
}

void AVehicle::UpdateBoarding() {
	m_BoarderInReach = nullptr;
	m_BoarderPushes = false;
	if (!m_BoardingTimer.IsPastSimMS(c_BoardingDelayMS)) {
		return;
	}
	if (IsDead()) {
		EjectCrew(false);
		return;
	}
	// The player in charge gets their unit out.
	if (m_Controller.IsPlayerControlled() && m_Controller.IsState(WEAPON_PICKUP) && GetControlSeat() >= 0) {
		EjectSeat(GetControlSeat(), false);
		return;
	}
	// On its side or roof and still, a unit beside it pushes it back over instead of getting in (an open one would only throw them out).
	bool pushOver = std::abs(Wrapped(m_Rotation.GetRadAngle())) > 1.2F && m_Vel.MagnitudeIsLessThan(2.0F);
	if (GetFreeSeat() < 0 && !pushOver) {
		return;
	}
	for (Actor* unit: g_MovableMan.GetActorList()) {
		if (unit == this || unit->GetTeam() != m_Team || unit->IsDead() || unit->IsSetToDelete() || !unit->GetController()->IsPlayerControlled() || !dynamic_cast<AHuman*>(unit)) {
			continue;
		}
		// In reach of any of its seats.
		bool inReach = false;
		for (int seat = 0; seat < GetSeatCount() && !inReach; ++seat) {
			inReach = !g_SceneMan.ShortestDistance(m_Pos + RotateOffset(GetSeatOffsetOf(seat)), unit->GetPos()).MagnitudeIsGreaterThan(m_BoardingReach + unit->GetRadius() * 0.25F);
		}
		if (!inReach) {
			continue;
		}
		m_BoarderInReach = unit;
		m_BoarderPushes = pushOver;
		if (unit->GetController()->IsState(WEAPON_PICKUP)) {
			if (pushOver) {
				float rotation = Wrapped(m_Rotation.GetRadAngle());
				m_AngularVel = rotation > 0.0F ? -5.0F : 5.0F;
				m_Vel.m_Y -= 4.0F;
				m_BoardingTimer.Reset();
				m_BoarderInReach = nullptr;
			} else if (TakeSeat(unit, -1)) {
				m_BoarderInReach = nullptr;
			}
		}
		break;
	}
}

long AVehicle::FindGunTarget() const {
	if (!m_Turret) {
		return 0;
	}
	Vector pivot = m_Turret->GetPos();
	float rotationFacing = Wrapped(m_Rotation.GetRadAngle()) * GetFlipFactor();
	long best = 0;
	float bestDistance = m_GunRange;
	for (const Actor* actor: g_MovableMan.GetActorList()) {
		if (actor == this || actor->GetTeam() == m_Team || actor->GetTeam() == Activity::NoTeam || actor->IsDead() || actor->IsSetToDelete() || actor->GetHealth() <= 0.0F) {
			continue;
		}
		Vector toTarget = g_SceneMan.ShortestDistance(pivot, actor->GetPos());
		float distance = toTarget.GetMagnitude();
		// (In front of it, and within the gun's swing: it can't turn the vehicle round.)
		if (distance >= bestDistance || toTarget.m_X * GetFlipFactor() <= 0.0F) {
			continue;
		}
		float angle = std::atan2(-toTarget.m_Y, std::abs(toTarget.m_X));
		if (std::abs(Wrapped(angle - rotationFacing)) > m_AimRange) {
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

void AVehicle::UpdateGun() {
	if (!m_Turret) {
		return;
	}
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	// The seat that mans it: the first gunner's, or the driver's with none.
	int gunnerSeat = 0;
	for (int seat = 1; seat <= static_cast<int>(m_Seats.size()); ++seat) {
		if (m_Seats[seat - 1].Gunner) {
			gunnerSeat = seat;
			break;
		}
	}
	bool manned = GetSeatOccupant(gunnerSeat) && m_Status != DYING && m_Status != DEAD;
	float rotationFacing = Wrapped(m_Rotation.GetRadAngle()) * GetFlipFactor();
	bool fire = false;
	if (manned && m_Controller.IsPlayerControlled() && GetControlSeat() == gunnerSeat) {
		// The player aims it, as a unit aims its gun: the aim keys swing it, the mouse or stick points it.
		float speed = m_Controller.IsState(AIM_SHARP) ? 0.6F : 1.8F;
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
		m_GunTargetID = 0;
	} else if (manned) {
		// A gunner looks round now and then for the nearest enemy in sight, swings round to it and fires once it is on it.
		if (m_GunTargetTimer.IsPastSimMS(400)) {
			m_GunTargetID = FindGunTarget();
			m_GunTargetTimer.Reset();
		}
		const Actor* target = m_GunTargetID ? dynamic_cast<const Actor*>(g_MovableMan.FindObjectByUniqueID(m_GunTargetID)) : nullptr;
		if (target && !target->IsDead()) {
			Vector toTarget = g_SceneMan.ShortestDistance(m_Turret->GetPos(), target->GetPos() - Vector(0.0F, target->GetHeight() * 0.1F));
			float wanted = std::atan2(-toTarget.m_Y, std::max(toTarget.m_X * GetFlipFactor(), 0.0F));
			float turn = std::clamp(Wrapped(wanted - m_AimAngle), -m_GunTurnSpeed * deltaTime, m_GunTurnSpeed * deltaTime);
			m_AimAngle += turn;
			fire = std::abs(Wrapped(wanted - m_AimAngle)) < 0.1F;
		} else {
			m_GunTargetID = 0;
		}
	}
	if (!manned) {
		// Unmanned, it comes to rest pointing ahead.
		m_AimAngle += std::clamp(rotationFacing - m_AimAngle, -deltaTime, deltaTime);
		m_GunTargetID = 0;
	}
	m_AimAngle = std::clamp(m_AimAngle, rotationFacing - m_AimRange, rotationFacing + m_AimRange);
	m_Turret->SetMountedDeviceRotationOffset((m_AimAngle * GetFlipFactor()) - m_Rotation.GetRadAngle());
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
		float push = 0.0F;
		if (m_Throttle != 0.0F && m_Throttle * speedAlong >= -0.1F) {
			if (std::abs(speedAlong) < m_MaxSpeed) {
				push = std::min(m_Acceleration * onGroundShare * std::abs(m_Throttle), (m_MaxSpeed - std::abs(speedAlong)) / deltaTime) * (m_Throttle > 0.0F ? 1.0F : -1.0F);
			}
		} else if (m_Throttle != 0.0F || m_Braking) {
			push = -direction * std::min(m_BrakeStrength * onGroundShare, stopping);
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

	// A knock hard enough in one update (a crash into a wall, a tumble down a cliff) throws everyone out.
	if (m_CrewThrowSpeed > 0.0F && GetCrewCount() > 0 && (m_Vel - m_LastVel).MagnitudeIsGreaterThan(m_CrewThrowSpeed)) {
		EjectCrew(true);
	}

	bool canDrive = (m_Driver || !m_NeedsDriver) && m_Status != DYING && m_Status != DEAD && m_Status != INACTIVE;
	// (The player in another seat, the gunner's or a passenger's, doesn't drive: the driver is left to it.)
	bool steering = canDrive && (!m_Controller.IsPlayerControlled() || GetControlSeat() == 0 || !m_NeedsDriver);
	bool left = steering && m_Controller.IsState(MOVE_LEFT);
	bool right = steering && m_Controller.IsState(MOVE_RIGHT);
	m_Throttle = left == right ? 0.0F : (right ? 1.0F : -1.0F);
	m_Braking = steering && (m_Controller.IsState(MOVE_DOWN) || m_Controller.IsState(BODY_CROUCH));
	// Facing the way it's driven, as a unit turns to walk: only when slow, so it doesn't flip round while braking from speed.
	if (m_Throttle != 0.0F && (m_Throttle > 0.0F) == m_HFlipped && m_Vel.MagnitudeIsLessThan(1.5F)) {
		SetHFlipped(!m_HFlipped);
	}

	// Jumping: with at least half its wheels down, its springs throw it up off the ground, along the way it stands.
	if (steering && m_HopSpeed > 0.0F && m_Controller.IsState(BODY_JUMPSTART) && m_HopTimer.IsPastSimMS(1000)) {
		if (std::vector<Attachable*> wheels = GetWheels(); !wheels.empty() && GetWheelsOnGround() * 2 >= static_cast<int>(wheels.size())) {
			m_Vel += Vector(0.0F, -m_HopSpeed).GetRadRotatedCopy(m_Rotation.GetRadAngle());
			m_HopTimer.Reset();
		}
	}

	UpdateEngine(canDrive);
	UpdateWheels();
	UpdateHull();
	if (m_Oar) {
		// Back and forth with the stroke: the blade goes from forward to back as it pulls, and forward again through the air.
		float swing = m_RowingStroke > 0.0F ? std::cos(m_StrokePhase) * m_OarSweep : 0.0F;
		m_Oar->SetInheritedRotAngleOffset(swing * GetFlipFactor());
	}
	UpdateGun();
	UpdateBoarding();

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

	// Rolled onto its roof: everyone falls out of an open vehicle.
	float rotation = Wrapped(m_Rotation.GetRadAngle());
	if (m_ThrowsCrewWhenFlipped && std::abs(rotation) > 2.0F && GetCrewCount() > 0) {
		EjectCrew(true);
		canDrive = (m_Driver || !m_NeedsDriver) && m_Status != DYING && m_Status != DEAD && m_Status != INACTIVE;
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

	m_LastVel = m_Vel;
	PlaceCrew();
}

void AVehicle::Draw(BITMAP* pTargetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const {
	// Everyone in it first, so the body's near side hides their legs: sat in it, not stood on it.
	if (mode == g_DrawColor || mode == g_DrawWhite || mode == g_DrawTrans || mode == g_DrawAlpha) {
		for (int seat = 0; seat < GetSeatCount(); ++seat) {
			if (const Actor* unit = GetSeatOccupant(seat)) {
				unit->Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
			}
		}
	}
	Actor::Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
}

void AVehicle::Draw(const Camera& camera) const {
	for (int seat = 0; seat < GetSeatCount(); ++seat) {
		if (const Actor* unit = GetSeatOccupant(seat)) {
			unit->Draw(camera);
		}
	}
	Actor::Draw(camera);
}

void AVehicle::DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos, int whichScreen, bool playerControlled) {
	Actor::DrawHUD(pTargetBitmap, targetPos, whichScreen, playerControlled);

	Activity* activity = g_ActivityMan.GetActivity();
	GUIFont* font = g_FrameMan.GetSmallFont();
	if (!activity || !font || !m_HUDVisible) {
		return;
	}
	const char* hint = nullptr;
	if (m_BoarderInReach && activity->ScreenOfPlayer(m_BoarderInReach->GetController()->GetPlayer()) == whichScreen) {
		hint = m_BoarderPushes ? "Pick up: push it back over" : "Pick up: get in";
	} else if (GetControlSeat() >= 0 && m_Controller.IsPlayerControlled() && activity->ScreenOfPlayer(m_Controller.GetPlayer()) == whichScreen && !m_BoardingTimer.IsPastSimMS(4000)) {
		int seat = GetControlSeat();
		if (seat == 0) {
			hint = m_HopSpeed > 0.0F ? "Pick up: get out   Jump: hop" : "Pick up: get out";
		} else if (IsGunnerSeat(seat)) {
			hint = "Gunner: aim and fire   Pick up: get out";
		} else {
			hint = "Passenger   Pick up: get out";
		}
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
