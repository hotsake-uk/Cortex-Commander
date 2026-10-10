#pragma once

#include "Actor.h"

namespace RTE {

	class Attachable;

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

		/// Lets a friendly unit beside the seat that asks to get in, get in; lets the driver out when they ask.
		void UpdateBoarding();

		/// Puts the driver at the seat, as the body lies now, so it is drawn there.
		void PlaceDriver() const;

		/// Removes a wheel from the wheels this keeps track of, when it is shot off or otherwise taken away. Ownership passes to the caller.
		void RemoveWheel(const Attachable* wheel);

		/// Clears all the member variables of this AVehicle, effectively resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		AVehicle(const AVehicle& reference) = delete;
		AVehicle& operator=(const AVehicle& rhs) = delete;
	};
} // namespace RTE
