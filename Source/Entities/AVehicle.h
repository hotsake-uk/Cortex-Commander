#pragma once

#include "Actor.h"
#include "CrewSeats.h"

namespace RTE {

	class Attachable;
	class AEmitter;
	class Turret;
	class SoundContainer;

	/// A wheeled vehicle (VH-1): a body held up off the ground by sprung wheels, which roll it over bumps and slopes. It is driven by a unit
	/// that climbs in to the seat, drawn sitting in it, and gets out again. The body's own atoms only meet the ground when the springs bottom
	/// out (a hard landing, a steep ledge) or it tips over.
	class AVehicle : public Actor {
		friend struct EntityLuaBindings;

	public:
		EntityAllocation(AVehicle);
		SerializableOverrideMethods;
		ClassInfoGetters;
		DefaultPieMenuNameGetter("Default Vehicle Pie Menu");

#pragma region Creation
		/// Constructor method used to instantiate a AVehicle object in system memory. Create() should be called before using the object.
		AVehicle();

		/// Makes the AVehicle object ready for use.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create() override { return Actor::Create(); }

		/// Creates a AVehicle to be identical to another, by deep copy.
		/// @param reference A reference to the AVehicle to deep copy.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create(const AVehicle& reference);
#pragma endregion

#pragma region Destruction
		/// Destructor method used to clean up a AVehicle object before deletion from system memory.
		~AVehicle() override;

		/// Destroys and resets (through Clear()) the AVehicle object.
		/// @param notInherited Whether to only destroy the members defined in this derived class, or to destroy all inherited members also.
		void Destroy(bool notInherited = false) override;

		/// Resets the entire AVehicle, including its inherited members, to their default settings or values.
		void Reset() override {
			Clear();
			Actor::Reset();
		}
#pragma endregion

#pragma region Getters and Setters
		/// Adds a wheel. Its ParentOffset is where its middle hangs with the spring let all the way out. Ownership IS transferred!
		/// @param wheel The wheel to add.
		void AddWheel(Attachable* wheel);

		/// Adds a strut to the wheel added last: a leg that rides up and down with the wheel's middle (it doesn't turn with the wheel), drawn
		/// behind the body so it slides up into it as the spring is pushed in. Its picture hangs up from where it is fixed. Ownership IS transferred!
		/// @param strut The strut to add.
		void AddStrut(Attachable* strut);

		/// Adds a point on the hull that liquid holds up (VH-2: boats): one along the keel or bottom, from the body's middle upright and facing right.
		/// Each is held up by as much of the column of HullDraft pixels above it as is in liquid, so a hull floats level at its waterline and rides waves.
		/// @param point The point to add.
		void AddHullPoint(const Vector& point) { m_HullPoints.push_back(point); }

		/// Sets the oar (a rowing boat's): it swings to and fro about its ParentOffset as the driver rows, its blade dipping on each stroke. Ownership IS transferred!
		/// @param newOar The oar to set. nullptr removes it.
		void SetOar(Attachable* newOar);

		/// Gets the oar, if it has one. Ownership is NOT transferred!
		Attachable* GetOar() const { return m_Oar; }

		/// Sets the exhaust: an emitter that runs while the engine does (smoke from a motor's stack). Ownership IS transferred!
		/// @param newExhaust The exhaust to set. nullptr removes it.
		void SetExhaust(AEmitter* newExhaust);

		/// Gets the exhaust, if it has one. Ownership is NOT transferred!
		AEmitter* GetExhaust() const { return m_Exhaust; }

		/// Gets whether its engine is running: it has one, and someone is driving (or it drives itself).
		bool IsEngineRunning() const { return m_EngineRunning; }

		/// Gets how hard the engine is working, 0 (ticking over) to 1 (flat out), eased towards the throttle.
		float GetEngineLoad() const { return m_EngineLoad; }

		/// Gets how much of its hull is in liquid, 0 (none, or no hull) to 1 (all its hull points under by the hull's draft), from the last update.
		float GetSubmergedFraction() const { return m_Submerged; }

		/// Gets whether it has a hull: points liquid holds up, and so drives in water.
		bool HasHull() const { return !m_HullPoints.empty(); }

		/// Gets the wheels this still has. Ownership is NOT transferred!
		std::vector<Attachable*> GetWheels() const;

		/// Gets how many of its wheels are on the ground.
		int GetWheelsOnGround() const;

		/// Gets how many wheels it keeps track of (for saving them), a bare strut whose wheel was shot off included.
		int GetWheelCount() const { return static_cast<int>(m_Wheels.size()); }

		/// Gets a wheel, its strut and where it is fixed (for saving them). Ownership is NOT transferred!
		/// @param index The wheel, 0 to GetWheelCount. @return The wheel or its strut, either nullptr if it has none.
		const Attachable* GetWheelPart(int index) const { return m_Wheels[index].Part; }
		const Attachable* GetWheelStrut(int index) const { return m_Wheels[index].Strut; }
		const Vector& GetWheelMount(int index) const { return m_Wheels[index].Mount; }

		/// Gets its seats and who sits in them (for saving them).
		const CrewSeats& GetCrewSeats() const { return m_Crew; }

		/// Gets the unit driving this, if any. Ownership is NOT transferred!
		Actor* GetDriver() const { return m_Crew.GetOccupant(0); }

		/// Gets whether a unit is in the driver's seat.
		bool HasDriver() const { return m_Crew.GetOccupant(0) != nullptr; }

		/// Gets whether it can be driven now: someone is in the driver's seat, or it drives itself.
		bool CanBeDriven() const { return HasDriver() || !m_NeedsDriver; }

		/// Gets how many seats it has: the driver's (seat 0) and any others (1 on), passengers' and gunners'.
		int GetSeatCount() const { return m_Crew.GetSeatCount(); }

		/// Gets who sits in a seat. Ownership is NOT transferred!
		/// @param seat The seat: 0 the driver's, 1 on the others.
		/// @return The unit in it, or nullptr if it is empty or there is no such seat.
		Actor* GetSeatOccupant(int seat) const { return m_Crew.GetOccupant(seat); }

		/// Gets whether a seat mans the turret.
		/// @param seat The seat: 0 the driver's, 1 on the others.
		bool IsGunnerSeat(int seat) const { return m_Crew.IsGunnerSeat(seat); }

		/// Gets how many units are in it, driver included.
		int GetCrewCount() const { return m_Crew.GetCrewCount(); }

		/// Whether a unit could get in at all: alive, a humanoid on this one's side, not a side's brain.
		bool CanCarry(const Actor* unit) const { return m_Crew.CanCarry(unit); }

		/// Gets the seat a unit getting in would take: the driver's if free, then a gunner's, then a passenger's.
		/// @return The seat, or -1 if they are all taken.
		int GetFreeSeat() const { return m_Crew.GetFreeSeat(); }

		/// Gets the seat of the unit the player in charge of this sits in: the one they got in as, or the driver's.
		/// @return The seat, or -1 with nobody in it for them.
		int GetControlSeat() const { return m_Crew.GetControlSeat(); }

		/// Puts a unit in a seat, taking it out of the scene, as TakeDriver does.
		/// @param unit The unit getting in.
		/// @param seat The seat, or -1 for the one GetFreeSeat gives.
		/// @return Whether it got in.
		bool TakeSeat(Actor* unit, int seat) { return m_Crew.TakeSeat(unit, seat); }

		/// Lets the unit in a seat out beside this, back into the scene; the player goes with them if they were in charge from that seat.
		/// @param seat The seat: 0 the driver's, 1 on the others.
		/// @param thrown Whether they are thrown out (a crash, a roll, a wreck): flung clear with the speed it had, and put out above it if there's no room beside.
		/// @return The unit that got out, or nullptr if the seat was empty or there was no room to get out.
		Actor* EjectSeat(int seat, bool thrown) { return m_Crew.LetOut(seat, thrown); }

		/// Lets everyone in it out, or throws them out.
		/// @param thrown Whether they are thrown out (see EjectSeat).
		void EjectCrew(bool thrown) { m_Crew.EjectCrew(thrown); }

		/// Puts a unit just made, not in the scene (a vehicle spawned with its crew), straight in a seat: nobody is copied, deleted or told.
		/// @param unit The unit. Ownership IS transferred if it went in. @param seat The seat, or -1 for the one GetFreeSeat gives.
		/// @return Whether it went in: a free seat, and a unit that could get in.
		bool SeatNewCrew(Actor* unit, int seat = -1) { return m_Crew.SeatNew(unit, seat); }

		/// Moves the unit the player is in charge of to the next empty seat, as the Change Seat pie slice does.
		/// @return Whether they moved.
		bool ChangeSeat() { return m_Crew.ChangeSeat(); }

		/// Sets the tracks: a belt round the wheels whose picture's frames step along as it drives, so the links run round (a tank's). Ownership IS transferred!
		/// @param newTracks The tracks to set. nullptr removes them.
		void SetTracks(Attachable* newTracks);

		/// Gets the tracks, if it has them. Ownership is NOT transferred!
		Attachable* GetTracks() const { return m_Tracks; }

		/// Gets whether the crew is drawn in their seats (an open vehicle) or hidden inside (a closed one, a tank).
		bool DrawsCrew() const { return m_Crew.DrawsCrew(); }

		/// Gets the turret its gun is on, if it has one. Ownership is NOT transferred!
		Turret* GetTurret() const { return m_Turret; }

		/// Sets the turret its gun is on: manned from a gunner's seat, or by the driver if it has none. Ownership IS transferred!
		/// @param newTurret The turret to set. nullptr removes it.
		void SetTurret(Turret* newTurret);

		/// Gets the throwing arm, if it is a catapult. Ownership is NOT transferred!
		Attachable* GetThrowingArm() const { return m_ThrowingArm; }

		/// Sets the throwing arm: a catapult's, swung about where it is fixed (its ParentOffset) to fling what it throws, manned from a
		/// gunner's seat as a turret is. Ownership IS transferred!
		/// @param newArm The arm to set. nullptr removes it.
		void SetThrowingArm(Attachable* newArm);

		/// Gets the name of what a catapult throws next (the preset of its loaded ammunition), or an empty string.
		std::string GetLaunchAmmoName() const;

		/// Puts a unit in the driver's seat, taking it out of the scene, and gives this its player if it had one. The unit must be in MovableMan
		/// and on this one's team; a copy of it is kept (as a craft keeps who goes in) and it is deleted.
		/// @param unit The unit getting in.
		/// @return Whether it got in: not when the seat is taken, it is dead, or not a unit that sits (only humanoids drive).
		bool TakeDriver(Actor* unit) { return !HasDriver() && TakeSeat(unit, 0); }

		/// Lets the driver out beside this, back into the scene, and hands the player back to it if this was theirs.
		/// @return The unit that got out, or nullptr if nobody was driving or there was no room to get out.
		Actor* EjectDriver() { return EjectSeat(0, false); }

		/// Gets how hard the driver is driving it along, -1 (full to the left) to 1 (full to the right), from this update.
		float GetThrottle() const { return m_Throttle; }

		/// Gets whether the handbrake is on: the wheels held braked and the throttle ignored, whoever drives, till it is let off.
		bool IsHandbrakeOn() const { return m_Handbrake; }

		/// Puts the handbrake on or lets it off (the unit menu's Handbrake).
		void SetHandbrake(bool on) { m_Handbrake = on; }

		/// Gets how much of the body is inside the ground, 0 to 1: the share of its atoms in anything but air and liquid.
		float GetSunkFraction() const;

		/// Gets the top speed it drives at on the flat, in m/s.
		float GetMaxSpeed() const { return m_MaxSpeed; }

		/// Sets the top speed it drives at on the flat, in m/s.
		void SetMaxSpeed(float newSpeed) { m_MaxSpeed = newSpeed; }

		/// Gets how hard it speeds up with the wheels down, in m/s each second: its power, and so the steepest slope it can drive up.
		float GetAcceleration() const { return m_Acceleration; }

		/// Sets how hard it speeds up with the wheels down, in m/s each second.
		void SetAcceleration(float newAcceleration) { m_Acceleration = std::max(newAcceleration, 0.0F); }

		/// Gets how much of its weight on the wheels it can push or brake with before they slip, 0 to about 1 (more for tracks).
		float GetGrip() const { return m_Grip; }

		/// Sets how much of its weight on the wheels it can push or brake with before they slip.
		void SetGrip(float newGrip) { m_Grip = std::max(newGrip, 0.0F); }

		/// Gets how hard it slows with the brake on, in m/s each second.
		float GetBrakeStrength() const { return m_BrakeStrength; }

		/// Sets how hard it slows with the brake on, in m/s each second.
		void SetBrakeStrength(float newStrength) { m_BrakeStrength = std::max(newStrength, 0.0F); }

		/// Gets how much rolling on its own slows it, in m/s each second: how far it coasts.
		float GetRollingResistance() const { return m_RollingResistance; }

		/// Sets how much rolling on its own slows it, in m/s each second.
		void SetRollingResistance(float newResistance) { m_RollingResistance = std::max(newResistance, 0.0F); }

		/// Gets how tough a material it smashes through when it drives into it (a material's StructuralIntegrity: earth 70, concrete 200,
		/// metal 400). 0 smashes nothing (plants are crushed regardless).
		float GetTerrainCrush() const { return m_TerrainCrush; }

		/// Sets how tough a material it smashes through when it drives into it. 0 smashes nothing.
		void SetTerrainCrush(float newCrush) { m_TerrainCrush = std::max(newCrush, 0.0F); }

		/// Gets roughly the steepest slope it can drive up, in degrees: as far as its power lifts it against gravity, and its grip holds.
		float GetClimbAngle() const;

		/// Gets how hard it speeds up in water when driven, in m/s each second. 0: it doesn't drive in water (it isn't a boat).
		float GetWaterThrust() const { return m_WaterThrust; }

		/// Sets how hard it speeds up in water when driven, in m/s each second (nothing for one with no hull).
		void SetWaterThrust(float newThrust) { m_WaterThrust = std::max(newThrust, 0.0F); }

		/// Gets the top speed it is driven at in water, in m/s.
		float GetWaterMaxSpeed() const { return m_WaterMaxSpeed; }

		/// Sets the top speed it is driven at in water, in m/s.
		void SetWaterMaxSpeed(float newSpeed) { m_WaterMaxSpeed = std::max(newSpeed, 0.0F); }

		/// Gets whether its crew stays in (the Stay in vehicle order): riders don't get out where they were going, nor when it stops.
		bool GetCrewStaysIn() const { return m_CrewStaysIn; }

		/// Sets whether its crew stays in (see GetCrewStaysIn). Being thrown out (a crash, a roll, a wreck) still throws them out.
		void SetCrewStaysIn(bool staysIn) { m_CrewStaysIn = staysIn; }

		/// Takes a unit back as one called over to ride in it, on its way here: a saved game's, whose way here (this as its waypoint) was
		/// saved with it but not that this called it (ActivityMan::RemapLoadedUniqueIDs).
		/// @param unit The unit coming.
		void AddLoadedRecruit(const Actor* unit) { m_Recruits.push_back({unit->GetUniqueID(), Timer()}); }

		/// Gets whether the player commands it as one unit, crew and all (command mode, VC-1): it drives where it is ordered whatever the AI
		/// vehicles setting, and its crew stay aboard where it stops until they are ordered out, or until it can't get where it was sent
		/// (the sandbox lets them out to go on on foot then, unless the crew is told to stay in).
		bool IsCommanded() const { return m_Commanded; }

		/// Sets whether the player commands it as one unit (see IsCommanded).
		void SetCommanded(bool commanded) { m_Commanded = commanded; }

		/// Gets how far its gunner looks for something to shoot, in pixels.
		float GetGunRange() const { return m_GunRange; }

		/// Finds a firing position on the ground to shoot at a place from: either side of it, a share of its gun range off, the near side first
		/// and the nearest to where it is now, where its gun would have a clear line to it within its swing up or down, with no friend in the
		/// way, and with room clear of its side's other vehicles (and where they are driving to). Not for a catapult, which lobs over.
		/// @param aimAt The place. @param spot Set to where its middle would be there. @return Whether it found one.
		bool FindFiringSpot(const Vector& aimAt, Vector& spot) const;

		/// Gets whether its gun could hit a place from a point: in range, within its swing up or down (level), a clear line to it, no friend in the way.
		/// @param gunAt Where the gun would be. @param aimAt The place.
		bool CanHitFrom(const Vector& gunAt, const Vector& aimAt) const;

		/// Gets how far above the way it faces its gun lifts, in radians: AimRangeUp, else as far as it swings down.
		float GetAimRangeUp() const { return m_AimRangeUp >= 0.0F ? m_AimRangeUp : m_AimRange; }

		/// Gets where across a commanded vehicle is driving to about a fight (UpdateAIManoeuvre), so others keep clear of it.
		/// @param x Set to the place across. @return Whether it is driving to one.
		bool GetManoeuvreGoal(float& x) const {
			x = m_AIManoeuvreX;
			return m_AIManoeuvring;
		}

		/// Gets whether its AI driver gave up on the way it was last sent (stuck, a drop, no way on), until it is sent somewhere again.
		bool HasGivenUpDriving() const { return m_AIGaveUp; }

		/// Gets how fast it jumps up off its springs when the driver jumps, in m/s. 0 can't.
		float GetHopSpeed() const { return m_HopSpeed; }

		/// Sets how fast it jumps up off its springs when the driver jumps, in m/s. 0 can't.
		void SetHopSpeed(float newSpeed) { m_HopSpeed = newSpeed; }

		/// Gets where the driver sits, from the middle of the body when upright and facing right.
		const Vector& GetSeatOffset() const { return m_Crew.GetMainSeatOffset(); }
#pragma endregion

#pragma region Override Methods
		/// Gets the absolute position of the driver's eye, or the middle of the body with no driver.
		Vector GetEyePos() const override;

		/// What this vehicle is to the path grid (VC-2): its height for head room, no jets, ladders, stairs, leaps or digging, a hop of its springs
		/// for a step up, and no drop higher than it lands from whole or would drive off. The order's risk and the route caution as any unit's.
		PathAgent GetPathAgent() const override;

		/// Tries to handle the activated PieSlice in this object's PieMenu, if there is one, based on its SliceType.
		/// @param pieSliceType The SliceType of the PieSlice being handled.
		/// @return Whether or not the activated PieSlice SliceType was able to be handled.
		bool HandlePieCommand(PieSliceType pieSliceType) override;

		/// Gibs this, letting the driver out first (thrown clear, as from a wreck).
		void GibThis(const Vector& impactImpulse = Vector(), MovableObject* movableObjectToIgnore = nullptr) override;

		/// Updates this MovableObject. Supposed to be done every frame.
		void Update() override;

		/// Puts the IDs of this and its parts in a list, and of the crew sat in its open seats (whom bullets can hit there).
		/// @param MOIDs The list to add them to.
		void GetMOIDs(std::vector<MOID>& MOIDs) const override;

		/// Draws this AVehicle's current graphical representation to a BITMAP of choice, the driver in the seat behind the body's near side.
		/// @param pTargetBitmap A pointer to a BITMAP to draw on.
		/// @param targetPos The absolute position of the target bitmap's upper left corner in the Scene.
		/// @param mode In which mode to draw in. See the DrawMode enumeration for the modes.
		/// @param onlyPhysical Whether to not draw any extra 'ghost' items of this MovableObject, indicator arrows or hovering HUD text and so on.
		void Draw(BITMAP* pTargetBitmap, const Vector& targetPos = Vector(), DrawMode mode = g_DrawColor, bool onlyPhysical = false) const override;
		void Draw(const Camera& camera) const override;

		/// Draws this Actor's current graphical HUD overlay representation to a BITMAP of choice: with it, the hint to get in for a unit beside it.
		/// @param pTargetBitmap A pointer to a BITMAP to draw on.
		/// @param targetPos The absolute position of the target bitmap's upper left corner in the Scene.
		/// @param whichScreen Which player's screen this is being drawn to. May affect what HUD elements get drawn etc.
		/// @param playerControlled Whether or not this MovableObject is currently player controlled (not applicable for MovableObject).
		void DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos = Vector(), int whichScreen = 0, bool playerControlled = false) override;
		void DrawHUD(const Camera& camera) override;
#pragma endregion

	protected:
		static Entity::ClassInfo m_sClass; //!< ClassInfo for this class.
		static constexpr float c_HoldSpeed = 1.0F; //!< Below this speed along the ground, in m/s, with no throttle, its wheels hold it still (on a slope too).

		/// Gives the attachables IDs, as any body's, then the crew in its open seats, so they're drawn to the layer that shots look for MOs
		/// on and can be hit where they sit. A closed vehicle's crew (DrawsCrew 0) stays out of reach.
		void UpdateChildMOIDs(std::vector<MovableObject*>& MOIDIndex, MOID rootMOID = g_NoMOID, bool makeNewMOID = true) override;

		/// A wheel and how it hangs: where its spring is fixed, how far the spring is pushed in, and how far round it has turned.
		struct Wheel {
			Attachable* Part = nullptr; //!< The wheel. Owned by this, as an attachable.
			Attachable* Strut = nullptr; //!< The leg it hangs on, if drawn: rides up and down with the wheel. Owned by this, as an attachable.
			Vector Mount; //!< Where its middle is with the spring all the way out, from the body's middle, upright and facing right.
			float Compression = 0.0F; //!< How far the spring is pushed in, in pixels, 0 to the suspension travel.
			float Spin = 0.0F; //!< How far it has turned, in radians, the way it turns on the ground (rolling right is negative).
			float SpinSpeed = 0.0F; //!< How fast it turns, in radians a second.
			bool OnGround = false; //!< Whether it touched the ground in the last update.
		};

		std::vector<Wheel> m_Wheels; //!< The wheels it has left.
		float m_SuspensionTravel; //!< How far each wheel's spring can be pushed in, in pixels.
		float m_SuspensionStiffness; //!< How hard the springs push, as acceleration per metre pushed in, shared between the wheels: about 9.8 over it is how far in they sit at rest.
		float m_SuspensionDamping; //!< How much the springs resist moving, as acceleration per m/s, shared between the wheels.
		float m_Acceleration; //!< How hard it speeds up with the wheels down, in m/s each second.
		float m_BrakeStrength; //!< How hard it slows with the brake on, in m/s each second.
		float m_RollingResistance; //!< How much rolling on its own slows it, in m/s each second (low: it coasts far).
		float m_MaxSpeed; //!< The top speed it is driven at on the flat, in m/s.
		float m_Grip; //!< How much of its weight on the wheels it can push or brake with before they slip, 0 to about 1.
		float m_TerrainCrush; //!< How tough a material it smashes through driving into it (see GetTerrainCrush). 0 none.
		bool m_CrewStaysIn; //!< Whether its crew stays in (the Stay in vehicle order; see GetCrewStaysIn).
		bool m_NeedsDriver; //!< Whether it only drives with someone in the seat (a cart does; a drone vehicle wouldn't).
		float m_HopSpeed; //!< How fast it jumps up off its springs when the driver jumps, in m/s. 0 can't.

		std::vector<Vector> m_HullPoints; //!< Points on the hull that liquid holds up, from the body's middle, upright and facing right. None: it has no hull.
		float m_HullDraft; //!< How deep a hull point goes under before liquid holds it up as hard as it can, in pixels.
		float m_WaterThrust; //!< How hard it speeds up in water with the driver rowing or the motor on, in m/s each second. 0: it doesn't drive in water.
		float m_WaterMaxSpeed; //!< The top speed it is driven at in water, in m/s.
		float m_WaterDrag; //!< How much water slows it going along, in a share of its speed each second at full depth (side on and up and down, a good deal more).
		float m_PlaningTrim; //!< How far the bow lifts at its top speed in water, in radians: a speedboat rides up on the water as it goes.
		float m_RowingStroke; //!< How long a stroke of the oars takes, in ms: it is pushed along in pulses, as the blades pull. 0: a steady push (a motor or a paddle wheel).
		Vector m_PropellerOffset; //!< Where the push comes from, from the body's middle upright and facing right: it only drives with that point in liquid.
		bool m_HasPropeller; //!< Whether a PropellerOffset was given. Without, it drives with any of the hull in liquid.
		Attachable* m_Oar; //!< The oar, swung to and fro as the driver rows. Owned by this, as an attachable.
		float m_OarSweep; //!< How far the oar swings either way from its rest, in radians.
		float m_StrokePhase; //!< How far through a stroke the oar is, in radians: the blade pulls in the first half.
		float m_Submerged; //!< How much of the hull was in liquid in the last update, 0 to 1.
		Timer m_WakeTimer; //!< Since it last left froth or spray on the water, so it leaves a little at a time.

		SoundContainer* m_EngineSound; //!< The engine's running sound, looped while it runs, its pitch going up with the load. None: no engine.
		AEmitter* m_Exhaust; //!< Runs while the engine does. Owned by this, as an attachable.
		bool m_EngineRunning; //!< Whether the engine ran in the last update.
		float m_EngineLoad; //!< How hard the engine is working, 0 to 1, eased towards the throttle.
		Timer m_HopTimer; //!< Since it last jumped, so it can't bounce itself up a cliff.

		CrewSeats m_Crew; //!< Its seats, the driver's (seat 0) and the rest, and who sits in them.
		Turret* m_Turret; //!< The turret its gun is on, if it has one. Owned by this, as an attachable.
		float m_GunRange; //!< How far a gunner looks for something to shoot, in pixels.
		float m_GunTurnSpeed; //!< How fast a gunner swings the gun round, in radians a second.
		float m_AimRangeUp; //!< How far above the way it faces its gun lifts, in radians, for aircraft; below 0, as far as AimRange.
		long m_GunTargetID; //!< The unique ID of what the gunner is shooting at, or 0.
		Timer m_GunTargetTimer; //!< Since the gunner last looked round for something to shoot.
		float m_CrewThrowSpeed; //!< How hard a knock (a change in speed in one update, m/s) throws everyone out. 0 never does.
		bool m_ThrowsCrewWhenFlipped; //!< Whether everyone falls out when it rolls onto its roof (an open vehicle; a closed one keeps them in).
		Attachable* m_Tracks; //!< The tracks, whose frames step along as it drives. Owned by this, as an attachable.
		Attachable* m_ThrowingArm; //!< A catapult's throwing arm, if it has one. Owned by this, as an attachable.
		float m_ArmLength; //!< How far the arm's bucket is from where it turns, in pixels (its picture lies back from there, the way it faces).
		float m_ArmThrowAngle; //!< How far the arm swings over throwing, in radians.
		float m_LaunchSpeed; //!< How fast the arm flings what it throws, in m/s.
		float m_LaunchReloadTime; //!< How long from one throw to the next, in ms: the arm let back down, wound and loaded.
		float m_MinLaunchAngle; //!< The lowest it can throw, in radians above level.
		float m_MaxLaunchAngle; //!< The highest it can throw, in radians above level.
		std::vector<MovableObject*> m_LaunchAmmo; //!< What it can throw, one kind loaded at a time (Next and Prev. Device change it). Owned.
		int m_LaunchAmmoIndex; //!< Which of m_LaunchAmmo is loaded.
		enum class LaunchPhase { Loaded, Winding, Throwing, Reloading };
		LaunchPhase m_LaunchPhase; //!< Where it is in a throw.
		Timer m_LaunchTimer; //!< Since the throw's phase began.
		bool m_Released; //!< Whether this throw has let go of its load.
		Timer m_AmmoSwitchTimer; //!< Since the loaded kind last changed, so a held key doesn't spin through them.
		int m_AIThrows; //!< How many an AI crew has thrown, to vary what it loads.
		float m_TrackFrameLength; //!< How far it drives for the tracks to step on a frame, in pixels.
		float m_TrackTravel; //!< How far the tracks have run round, in pixels, the way they run driving forward.

		/// A unit called over to ride in it, on its way.
		struct Recruit {
			long ID = 0; //!< Its unique ID.
			Timer Since; //!< Since it was called: it is let go after a while if it hasn't got here.
		};
		std::vector<Recruit> m_Recruits; //!< The units called over to ride in it.
		float m_RecruitRange; //!< How far off an AI unit can be called over to ride, in pixels.
		Timer m_RecruitTimer; //!< Since it last looked round for units to call over.
		Timer m_OrderedBoardTimer; //!< Since it last looked for units sent to it to get in.
		Timer m_AIStuckTimer; //!< How long the AI driver has made no headway.
		Timer m_AIReverseTimer; //!< How long the AI driver has backed up for, to get unstuck.
		int m_AIStuckCount; //!< How many times running the AI driver has got stuck on the way: it gives up after a few.
		bool m_AIReversing; //!< Whether the AI driver is backing up to get unstuck.
		bool m_AIHop; //!< Whether the AI driver jumps this update, to get unstuck.
		bool m_AIWantsStop; //!< Whether someone wants to get out, so the AI driver stops.
		bool m_AIGaveUp; //!< Whether the AI driver gave up on the way (stuck, a drop, the shore): its riders get out and walk.
		bool m_Commanded; //!< Whether the player commands it as one unit (see IsCommanded).
		int m_AIRoutesAsked; //!< How many times running the AI driver has asked for a new route because it got stuck on the one it had.
		Vector m_SuppressPoint; //!< Where in the place it was ordered to suppress its gunner is firing at, with nobody to shoot.
		Timer m_SuppressTimer; //!< Since its gunner last picked a point in that place.
		Timer m_AIFaceTimer; //!< Since the AI driver of a commanded vehicle last looked round to face an enemy, standing still.
		bool m_AIManoeuvring; //!< Whether the AI driver is moving about a fight (UpdateAIManoeuvre), to m_AIManoeuvreX.
		float m_AIManoeuvreX; //!< Where across it is moving to.
		bool m_AIKeepFacing; //!< Whether it keeps facing the way it faces this update, backing off rather than turning round.
		float m_AICombatHealth; //!< Its health when last looked at, to tell when it is hit.
		Timer m_AIManoeuvreTimer; //!< Since it started moving about the fight.
		long long m_AIManoeuvreLimitMS; //!< How long it gives the drive about the fight before stopping where it is (further, longer).
		Timer m_SuppressNoShotTimer; //!< Since its gunner, ordered to suppress a place, last had it (or someone) in its sights.
		Timer m_AISpacingTimer; //!< Since it last looked for a vehicle of its side stopped on top of it.
		Timer m_AIHitTimer; //!< Since it was last hit.
		Timer m_AIJinkTimer; //!< Since it last moved for being hit.
		Timer m_AINoShotTimer; //!< Since its gun last had the enemy it was sent after in its sights.
		Timer m_AIGaveUpTimer; //!< Since it gave up: it doesn't call riders over again for a while.
		Timer m_AIRiderWaitTimer; //!< Since its driver called the first passenger over: it waits for them that long at most (AIVehiclesWaitForRiders).
		unsigned int m_AIOrderSerialSeen; //!< The order serial its AI driver last started on: a new order starts afresh.
		float m_Throttle; //!< How hard it is being driven this update, -1 to 1.
		bool m_Braking; //!< Whether the brake is on this update.
		bool m_Handbrake; //!< Whether the handbrake is on (see IsHandbrakeOn).
		bool m_HandbrakeHeldAgainst; //!< Whether its player tried to drive it with the handbrake on this update, to be told so.
		Timer m_UpsideDownTimer; //!< How long it has lain on its side or roof, still: then the driver rocks it back over.
		float m_Buoyancy; //!< How hard liquid pushes it up against its weight, with its middle under (1 floats level; a boat's hull is VH-2).
		float m_BreakLandingSpeed; //!< How fast it can come down on a bottomed-out spring before it breaks apart, in m/s. 0 never breaks it so.
		float m_BreakSunkFraction; //!< How much of its body can be inside the ground before it breaks apart, 0 to 1. 0 never breaks it so.
		bool m_BreakNow; //!< Whether something this update was enough to break it apart, which it does at the end of the update.
		Timer m_SunkTimer; //!< How long it has been sunk past the limit: a moment's overlap (a wheel's step, a landing) doesn't break it.

	private:
		/// Works out each wheel's spring against the ground and pushes the body by them, drives and brakes the wheels on the ground, and turns them.
		void UpdateWheels();

		/// Smashes the ground it drives into, in front of it (behind, backing up) and above its axles, never under it: each pixel of a material
		/// no tougher than TerrainCrush goes as rubble, slowing it by how tough it was. Only while it moves along or is driven into it.
		void UpdateTerrainCrush();

		/// Holds the hull up on the liquid it is in, slows it in it and keeps it upright, and drives it along with the oars or motor.
		/// @return Whether any of the hull is in liquid.
		bool UpdateHull();

		/// Runs the engine while someone drives: its sound, its pitch with the load, and the exhaust.
		/// @param canDrive Whether it can be driven this update (someone in the seat, or it drives itself, and not wrecked).
		void UpdateEngine(bool canDrive);

		/// Lets a friendly unit beside the seat that asks to get in, get in (or push it back over, on its side or roof); lets the driver out when they ask.
		void UpdateBoarding();

		/// Sets who sits in a seat, without taking anyone in or letting them out.
		void SetSeatOccupant(int seat, Actor* unit) { m_Crew.SetOccupant(seat, unit); }

		/// Gets where a seat is, from the body's middle, upright and facing right.
		const Vector& GetSeatOffsetOf(int seat) const { return m_Crew.GetSeatOffset(seat); }

		/// Aims and fires the gun: the player's aim from the gunner's seat (or the driver's, with no gunner's seat), else the gunner looks for
		/// the nearest enemy in sight and range in front of it, swings the gun round to it and fires.
		void UpdateGun();

		/// Gets whether a seat's unit got in by itself to ride somewhere.
		bool IsRider(int seat) const { return m_Crew.IsRider(seat); }

		/// Sets whether a seat's unit got in by itself to ride somewhere.
		void SetRider(int seat, bool rider) { m_Crew.SetRider(seat, rider); }

		/// Drives it with no player in charge: towards where it was sent, braking to stop there, backing up or jumping when stuck, and stopping
		/// at a drop, deep water (or the shore, for a boat) or when someone wants to get out.
		void UpdateAIDriving();

		/// Gets whether there's ground (or water, for a boat) to drive onto just ahead, the way it is going.
		/// @param direction 1 to the right, -1 to the left.
		bool CanDriveOnTo(float direction) const;

		/// Gives up on the way it was sent: stops, and lets its riders out to walk.
		void GiveUpDriving();

		/// Turns a commanded vehicle standing still round to an enemy behind it within its gun's range (the gun only swings in front), or, with
		/// none, to the way its post faces (a defend's facing).
		void FaceForCommander();

		/// A commanded vehicle in a fight, as a ship does (ACShip::UpdateCombat), by its order's risk (Actor::StandingOrder::Direct): sent
		/// after an enemy, it backs away from one too close, or over it, to where its gun can bear; in position and hit, it backs off to
		/// the edge of its range on a cautious order, or shuffles along on a normal one (never on a full-risk one); and holding a post, hit
		/// while its gunner has someone, it shuffles about the post. Each move is a short drive along the ground, facing the enemy. With no shot
		/// at the enemy for a while, or at a place it was ordered to suppress, it drives round to a firing position (FindFiringSpot); and
		/// stopped right by another of its side's vehicles, one of the two moves off, so they don't sit in each other's way.
		/// @param enemy The enemy it was sent after, or null. @return Whether it is driving for the fight this update.
		bool UpdateAIManoeuvre(const Actor* enemy);

		/// Gets whether a commanded vehicle sent after an enemy is near enough to it, with its gun in sight of it, to stop and fire from where it is.
		/// @param target What it was sent after.
		bool InFiringPosition(const MovableObject* target) const;

		/// Lets in the AI units sent to it or called over that have got here; calls over AI units near it going a long way its way.
		void UpdateAIBoarding();

		/// Lets out riders near where they're going (or all of them, when it gave up), and moves a rider to the wheel if the driver left.
		void UpdateAIUnloading();

		/// Takes its orders from a unit: it is sent to where that unit was going.
		void TakeOrdersFrom(const Actor* unit);

		/// Works a catapult's arm through a throw (wound back, swung over, let back down to load) and lets go of its load on the way over.
		/// @param fire Whether the one manning it pulls the release this update.
		void UpdateThrowingArm(bool fire);

		/// Gets the angle above level to throw at to land on a point, the high lob if it can (over walls), else the low one.
		/// @param toTarget From where the throw starts to the point, in pixels.
		/// @param angle Where the angle is put, in radians above level, the way it faces.
		/// @return Whether it can reach there at all, within its angles.
		bool LobAngleTo(const Vector& toTarget, float& angle) const;

		/// Gets where a throw lets go of its load: the bucket's place with the arm swung most of the way over.
		Vector GetReleasePoint() const;

		/// Gets the velocity a throw at an angle flings its load with.
		/// @param angle Radians above level, the way it faces.
		Vector GetLaunchVelocity(float angle) const;

		/// Gets how hard gravity pulls on what is loaded, in m/s².
		float GetLaunchGravity() const;

		/// Draws, for the player manning it, the dotted arc a throw at the angle aimed at would take, to where it would land.
		void DrawLaunchArc() const;

		/// Finds what a gunner would shoot at: the nearest enemy unit in range and in sight, that the gun can swing to.
		/// @return Its unique ID, or 0 if there is none.
		long FindGunTarget() const;

		/// Gets the enemy nearest behind it that its gun would bear on were it turned round, else 0.
		long FindGunTargetBehind() const;

		/// Gets the enemy its gun would go for, facing one way: the ordered one if it bears on it, else the nearest that it does.
		/// @param flip 1 facing right, -1 facing left.
		long FindGunTargetFacing(float flip) const;

		/// Removes a wheel from the wheels this keeps track of, when it is shot off or otherwise taken away. Ownership passes to the caller.
		void RemoveWheel(const Attachable* wheel);

		/// Forgets a wheel's strut, when it is shot off or otherwise taken away. Ownership passes to the caller.
		void RemoveStrut(const Attachable* strut);

		/// Clears all the member variables of this AVehicle, effectively resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		AVehicle(const AVehicle& reference) = delete;
		AVehicle& operator=(const AVehicle& rhs) = delete;
	};
} // namespace RTE
