#pragma once

#include "Actor.h"

namespace RTE {

	class Attachable;
	class AEmitter;
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

		/// Gets the unit driving this, if any. Ownership is NOT transferred!
		Actor* GetDriver() const { return m_Driver; }

		/// Gets whether a unit is in the driver's seat.
		bool HasDriver() const { return m_Driver != nullptr; }

		/// Puts a unit in the driver's seat, taking it out of the scene, and gives this its player if it had one. The unit must be in MovableMan
		/// and on this one's team; a copy of it is kept (as a craft keeps who goes in) and it is deleted.
		/// @param unit The unit getting in.
		/// @return Whether it got in: not when the seat is taken, it is dead, or not a unit that sits (only humanoids drive).
		bool TakeDriver(Actor* unit);

		/// Lets the driver out beside this, back into the scene, and hands the player back to it if this was theirs.
		/// @return The unit that got out, or nullptr if nobody was driving or there was no room to get out.
		Actor* EjectDriver();

		/// Gets how hard the driver is driving it along, -1 (full to the left) to 1 (full to the right), from this update.
		float GetThrottle() const { return m_Throttle; }

		/// Gets how much of the body is inside the ground, 0 to 1: the share of its atoms in anything but air and liquid.
		float GetSunkFraction() const;

		/// Gets the top speed it drives at on the flat, in m/s.
		float GetMaxSpeed() const { return m_MaxSpeed; }

		/// Sets the top speed it drives at on the flat, in m/s.
		void SetMaxSpeed(float newSpeed) { m_MaxSpeed = newSpeed; }

		/// Gets how fast it jumps up off its springs when the driver jumps, in m/s. 0 can't.
		float GetHopSpeed() const { return m_HopSpeed; }

		/// Sets how fast it jumps up off its springs when the driver jumps, in m/s. 0 can't.
		void SetHopSpeed(float newSpeed) { m_HopSpeed = newSpeed; }

		/// Gets where the driver sits, from the middle of the body when upright and facing right.
		const Vector& GetSeatOffset() const { return m_SeatOffset; }
#pragma endregion

#pragma region Override Methods
		/// Gets the absolute position of the driver's eye, or the middle of the body with no driver.
		Vector GetEyePos() const override;

		/// Tries to handle the activated PieSlice in this object's PieMenu, if there is one, based on its SliceType.
		/// @param pieSliceType The SliceType of the PieSlice being handled.
		/// @return Whether or not the activated PieSlice SliceType was able to be handled.
		bool HandlePieCommand(PieSliceType pieSliceType) override;

		/// Gibs this, letting the driver out first (thrown clear, as from a wreck).
		void GibThis(const Vector& impactImpulse = Vector(), MovableObject* movableObjectToIgnore = nullptr) override;

		/// Updates this MovableObject. Supposed to be done every frame.
		void Update() override;

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
		float m_RollingResistance; //!< How much rolling on its own slows it, in m/s each second (low: it rolls down slopes).
		float m_MaxSpeed; //!< The top speed it is driven at on the flat, in m/s.
		float m_Grip; //!< How much of its weight on the wheels it can push or brake with before they slip, 0 to about 1.
		Vector m_SeatOffset; //!< Where the driver sits, from the body's middle, upright and facing right (their middle, for drawing them).
		Vector m_ExitOffset; //!< Where the driver gets out, from the body's middle, upright and facing right (mirrored to whichever side is clear).
		float m_BoardingReach; //!< How close to the seat a unit has to be to get in.
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

		Actor* m_Driver; //!< The unit in the driver's seat, out of the scene while it's in here. Owned.
		float m_Throttle; //!< How hard it is being driven this update, -1 to 1.
		bool m_Braking; //!< Whether the brake is on this update.
		Timer m_BoardingTimer; //!< Since the driver last got in or out, so the key that did it doesn't do the opposite straight after.
		Timer m_UpsideDownTimer; //!< How long it has lain on its side or roof, still: then the driver rocks it back over.
		Actor* m_BoarderInReach; //!< A friendly player-controlled unit beside the seat this update, for the hint. Not owned.
		float m_Buoyancy; //!< How hard liquid pushes it up against its weight, with its middle under (1 floats level; a boat's hull is VH-2).
		float m_BreakLandingSpeed; //!< How fast it can come down on a bottomed-out spring before it breaks apart, in m/s. 0 never breaks it so.
		float m_BreakSunkFraction; //!< How much of its body can be inside the ground before it breaks apart, 0 to 1. 0 never breaks it so.
		bool m_BreakNow; //!< Whether something this update was enough to break it apart, which it does at the end of the update.
		Timer m_SunkTimer; //!< How long it has been sunk past the limit: a moment's overlap (a wheel's step, a landing) doesn't break it.

	private:
		/// Works out each wheel's spring against the ground and pushes the body by them, drives and brakes the wheels on the ground, and turns them.
		void UpdateWheels();

		/// Holds the hull up on the liquid it is in, slows it in it and keeps it upright, and drives it along with the oars or motor.
		/// @return Whether any of the hull is in liquid.
		bool UpdateHull();

		/// Runs the engine while someone drives: its sound, its pitch with the load, and the exhaust.
		/// @param canDrive Whether it can be driven this update (someone in the seat, or it drives itself, and not wrecked).
		void UpdateEngine(bool canDrive);

		/// Lets a friendly unit beside the seat that asks to get in, get in; lets the driver out when they ask.
		void UpdateBoarding();

		/// Puts the driver at the seat, as the body lies now, so it is drawn there.
		void PlaceDriver() const;

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
