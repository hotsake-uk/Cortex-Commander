#include "AVehicle.h"

#include "AHuman.h"
#include "ActivityMan.h"
#include "Atom.h"
#include "AtomGroup.h"
#include "Attachable.h"
#include "Controller.h"
#include "FluidSim.h"
#include "FrameMan.h"
#include "MovableMan.h"
#include "PieSlice.h"
#include "PresetMan.h"
#include "Sandbox.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "TimerMan.h"

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

	/// Whether the ground at a point holds a wheel up: anything but air, liquid (a wheel sinks through water to the bottom) and plants.
	bool HoldsWheel(const Vector& point) {
		int material = g_SceneMan.GetTerrMatter(point.GetFloorIntX(), point.GetFloorIntY());
		return material != g_MaterialAir && !FluidSim::IsLiquid(material) && !IsPlant(material);
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
				if (IsPlant(terrain->GetMaterialPixel(x, y))) {
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
	m_Driver = nullptr;
	m_Throttle = 0.0F;
	m_Braking = false;
	m_BoardingTimer.Reset();
	m_UpsideDownTimer.Reset();
	m_BoarderInReach = nullptr;
	m_Buoyancy = 0.8F;
	m_BreakLandingSpeed = 0.0F;
	m_BreakSunkFraction = 0.0F;
	m_BreakNow = false;
	m_SunkTimer.Reset();
}

int AVehicle::Create(const AVehicle& reference) {
	for (const Wheel& wheel: reference.m_Wheels) {
		m_ReferenceHardcodedAttachableUniqueIDs.insert(wheel.Part->GetUniqueID());
	}

	Actor::Create(reference);

	for (const Wheel& wheel: reference.m_Wheels) {
		AddWheel(dynamic_cast<Attachable*>(wheel.Part->Clone()));
		m_Wheels.back().Mount = wheel.Mount;
		m_Wheels.back().Part->SetParentOffset(wheel.Mount);
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
	for (const Wheel& wheel: m_Wheels) {
		m_HardcodedAttachableUniqueIDsAndRemovers.erase(wheel.Part->GetUniqueID());
	}
	if (!notInherited) {
		Actor::Destroy();
	}
	Clear();
}

int AVehicle::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Actor::ReadProperty(propName, reader));

	MatchProperty("AddWheel", { AddWheel(dynamic_cast<Attachable*>(g_PresetMan.ReadReflectedPreset(reader))); });
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
	MatchProperty("Buoyancy", { reader >> m_Buoyancy; });
	MatchProperty("BreakLandingSpeed", { reader >> m_BreakLandingSpeed; });
	MatchProperty("BreakSunkFraction", { reader >> m_BreakSunkFraction; });

	EndPropertyList;
}

int AVehicle::Save(Writer& writer) const {
	Actor::Save(writer);

	for (const Wheel& wheel: m_Wheels) {
		writer.NewProperty("AddWheel");
		writer << wheel.Part;
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

void AVehicle::RemoveWheel(const Attachable* wheel) {
	std::erase_if(m_Wheels, [wheel](const Wheel& each) { return each.Part == wheel; });
}

std::vector<Attachable*> AVehicle::GetWheels() const {
	std::vector<Attachable*> wheels;
	for (const Wheel& wheel: m_Wheels) {
		wheels.push_back(wheel.Part);
	}
	return wheels;
}

int AVehicle::GetWheelsOnGround() const {
	return static_cast<int>(std::count_if(m_Wheels.begin(), m_Wheels.end(), [](const Wheel& wheel) { return wheel.OnGround; }));
}

Vector AVehicle::GetEyePos() const {
	return m_Driver ? m_Pos + RotateOffset(m_SeatOffset - Vector(0.0F, m_Driver->GetHeight() * 0.3F)) : m_Pos;
}

bool AVehicle::TakeDriver(Actor* unit) {
	if (m_Driver || !unit || unit == this || unit->IsDead() || unit->IsSetToDelete() || unit->GetTeam() != m_Team || !dynamic_cast<AHuman*>(unit) || unit->IsInGroup("Brains") || IsDead()) {
		return false;
	}
	Activity* activity = g_ActivityMan.GetActivity();
	if (activity) {
		for (int player = Players::PlayerOne; player < Players::MaxPlayerCount; ++player) {
			// (A brain that walks about, as in the Sandbox's units, stays out: the side would lose with it gone from the scene.)
			if (activity->GetPlayerBrain(player) == unit) {
				return false;
			}
		}
	}
	// As a craft takes in who boards it: a copy is kept, the unit itself is deleted from the scene, and the side isn't counted a loss for it.
	m_Driver = dynamic_cast<Actor*>(unit->Clone());
	if (!m_Driver) {
		return false;
	}
	m_Driver->GetController()->SetInputMode(Controller::CIM_AI);
	m_Driver->SetVel(Vector());
	unit->SetToDelete(true);
	if (activity) {
		activity->ReportDeath(unit->GetTeam(), -1);
		if (unit->GetController()->IsPlayerControlled()) {
			activity->SwitchToActor(this, unit->GetController()->GetPlayer(), m_Team);
		}
	}
	// (The Sandbox keeps its own hold on the unit you control and on your character: they go with it into the seat.)
	Sandbox::OnUnitBoarded(unit, this);
	m_BoardingTimer.Reset();
	PlaceDriver();
	// (Said by the vehicle, where the driver now sits: the driver is out of the scene while it's in here.)
	Say("BoardVehicle");
	return true;
}

Actor* AVehicle::EjectDriver() {
	if (!m_Driver) {
		return nullptr;
	}
	// Out on the side it faces, or the other, or straight up out of the seat if both are blocked.
	Vector seat = m_Pos + RotateOffset(m_SeatOffset);
	std::array<Vector, 3> exits = {m_Pos + RotateOffset(m_ExitOffset), m_Pos + RotateOffset(Vector(-m_ExitOffset.m_X, m_ExitOffset.m_Y)), seat - Vector(0.0F, m_Driver->GetHeight() * 0.5F)};
	m_Driver->SetRotAngle(0.0F);
	m_Driver->SetAngularVel(0.0F);
	const AtomGroup* atoms = m_Driver->GetAtomGroup();
	const Vector* out = nullptr;
	for (const Vector& exit: exits) {
		if (!atoms || atoms->FitsAt(exit)) {
			out = &exit;
			break;
		}
	}
	if (!out) {
		if (!IsDead()) {
			return nullptr;
		}
		// (Wrecked: out on top whatever is there, rather than lost with it.)
		out = &exits[2];
	}
	Actor* driver = m_Driver;
	m_Driver = nullptr;
	driver->SetPos(*out);
	driver->SetVel(m_Vel + Vector(0.0F, -2.0F));
	driver->SetWhichMOToNotHit(this, 0.5F);
	SetWhichMOToNotHit(driver, 0.5F);
	driver->ResetAllTimers();
	g_MovableMan.AddActor(driver);
	if (Activity* activity = g_ActivityMan.GetActivity(); activity && m_Controller.IsPlayerControlled() && activity->GetControlledActor(m_Controller.GetPlayer()) == this) {
		activity->SwitchToActor(driver, m_Controller.GetPlayer(), driver->GetTeam());
	}
	Sandbox::OnUnitLeftVehicle(this, driver);
	m_BoardingTimer.Reset();
	driver->Say(IsDead() || m_Health <= 0.0F ? "BailOut" : "LeaveVehicle");
	return driver;
}

bool AVehicle::HandlePieCommand(PieSliceType pieSliceType) {
	if (pieSliceType == PieSliceType::GetOut) {
		EjectDriver();
		return true;
	}
	return Actor::HandlePieCommand(pieSliceType);
}

void AVehicle::GibThis(const Vector& impactImpulse, MovableObject* movableObjectToIgnore) {
	if (Actor* driver = m_Driver ? EjectDriver() : nullptr) {
		// Thrown clear of the wreck.
		driver->SetVel(driver->GetVel() + impactImpulse / std::max(GetMass(), 1.0F) * 0.5F + Vector(0.0F, -4.0F));
		driver->Say("BailOut");
	}
	Actor::GibThis(impactImpulse, movableObjectToIgnore);
}

void AVehicle::PlaceDriver() const {
	if (!m_Driver) {
		return;
	}
	m_Driver->SetPos(m_Pos + RotateOffset(m_SeatOffset));
	m_Driver->SetVel(m_Vel);
	m_Driver->SetRotAngle(m_Rotation.GetRadAngle());
	m_Driver->SetHFlipped(m_HFlipped);
	m_Driver->CorrectAttachableAndWoundPositionsAndRotations();
}

void AVehicle::UpdateBoarding() {
	m_BoarderInReach = nullptr;
	if (!m_BoardingTimer.IsPastSimMS(c_BoardingDelayMS)) {
		return;
	}
	if (m_Driver) {
		if (m_Controller.IsState(WEAPON_PICKUP) || IsDead()) {
			EjectDriver();
		}
		return;
	}
	if (IsDead()) {
		return;
	}
	Vector seat = m_Pos + RotateOffset(m_SeatOffset);
	for (Actor* unit: g_MovableMan.GetActorList()) {
		if (unit == this || unit->GetTeam() != m_Team || unit->IsDead() || unit->IsSetToDelete() || !unit->GetController()->IsPlayerControlled() || !dynamic_cast<AHuman*>(unit)) {
			continue;
		}
		if (g_SceneMan.ShortestDistance(seat, unit->GetPos()).MagnitudeIsGreaterThan(m_BoardingReach + unit->GetRadius() * 0.25F)) {
			continue;
		}
		m_BoarderInReach = unit;
		if (unit->GetController()->IsState(WEAPON_PICKUP) && TakeDriver(unit)) {
			m_BoarderInReach = nullptr;
		}
		break;
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
	float share = m_Wheels.empty() ? 0.0F : 1.0F / static_cast<float>(m_Wheels.size());

	for (Wheel& wheel: m_Wheels) {
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
	if (FluidSim::IsEnabled() && FluidSim::IsLiquid(middleMaterial)) {
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
		float radius = std::max(static_cast<float>(wheel.Part->GetSpriteWidth()) * 0.5F, 1.0F);
		if (wheel.OnGround) {
			wheel.SpinSpeed = -speedAlong * c_PPM / radius;
		} else {
			wheel.SpinSpeed *= std::max(1.0F - 0.6F * deltaTime, 0.0F);
		}
		wheel.Spin = Wrapped(wheel.Spin + wheel.SpinSpeed * deltaTime);
		wheel.Part->SetParentOffset(wheel.Mount - Vector(0.0F, wheel.Compression));
		// Rolling or landing on plants flattens them (a wheel standing still on one leaves it be, so a parked cart doesn't eat what it's in).
		if (wheel.OnGround && (std::abs(wheel.SpinSpeed) > 0.5F || m_Vel.MagnitudeIsGreaterThan(1.0F))) {
			CrushPlants(m_Pos + RotateOffset(wheel.Mount - Vector(0.0F, wheel.Compression)), radius + 1.0F);
		}
		wheel.Part->SetInheritedRotAngleOffset(wheel.Spin * GetFlipFactor());
	}
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

	bool canDrive = (m_Driver || !m_NeedsDriver) && m_Status != DYING && m_Status != DEAD && m_Status != INACTIVE;
	bool left = canDrive && m_Controller.IsState(MOVE_LEFT);
	bool right = canDrive && m_Controller.IsState(MOVE_RIGHT);
	m_Throttle = left == right ? 0.0F : (right ? 1.0F : -1.0F);
	m_Braking = canDrive && (m_Controller.IsState(MOVE_DOWN) || m_Controller.IsState(BODY_CROUCH));
	// Facing the way it's driven, as a unit turns to walk: only when slow, so it doesn't flip round while braking from speed.
	if (m_Throttle != 0.0F && (m_Throttle > 0.0F) == m_HFlipped && m_Vel.MagnitudeIsLessThan(1.5F)) {
		SetHFlipped(!m_HFlipped);
	}

	UpdateWheels();
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

	// On its side or roof and still: the driver rocks it back over, after a moment.
	float rotation = Wrapped(m_Rotation.GetRadAngle());
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

	PlaceDriver();
}

void AVehicle::Draw(BITMAP* pTargetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const {
	// The driver first, so the body's near side hides their legs: sat in it, not stood on it.
	if (m_Driver && (mode == g_DrawColor || mode == g_DrawWhite || mode == g_DrawTrans || mode == g_DrawAlpha)) {
		m_Driver->Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
	}
	Actor::Draw(pTargetBitmap, targetPos, mode, onlyPhysical);
}

void AVehicle::Draw(const Camera& camera) const {
	if (m_Driver) {
		m_Driver->Draw(camera);
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
		hint = "Pick up: get in";
	} else if (m_Driver && m_Controller.IsPlayerControlled() && activity->ScreenOfPlayer(m_Controller.GetPlayer()) == whichScreen && !m_BoardingTimer.IsPastSimMS(4000)) {
		hint = "Pick up: get out";
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
