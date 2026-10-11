#pragma once

#include "Actor.h"

namespace RTE {

	class Attachable;

	/// A lift or moving platform: a deck that travels in straight lines between two or more stops, pausing at each. The deck is drawn into the
	/// terrain's material where it is, so bodies stand on it and shots hit it, and whatever stands on it is carried along with it. A mover belongs
	/// to no side (anyone rides it), and stops and backs off rather than crush a unit in its way.
	class AMover : public Actor {

	public:
		EntityAllocation(AMover);
		SerializableOverrideMethods;
		ClassInfoGetters;

#pragma region Creation
		/// Constructor method used to instantiate a AMover object in system memory. Create() should be called before using the object.
		AMover();

		/// Makes the AMover object ready for use.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create() override;

		/// Creates a AMover to be identical to another, by deep copy.
		/// @param reference A reference to the AMover to deep copy.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create(const AMover& reference);
#pragma endregion

#pragma region Destruction
		/// Destructor method used to clean up a AMover object before deletion from system memory.
		~AMover() override;

		/// Destroys and resets (through Clear()) the AMover object.
		/// @param notInherited Whether to only destroy the members defined in this derived class, or to destroy all inherited members also.
		void Destroy(bool notInherited = false) override;

		/// Resets the entire AMover, including its inherited members, to their default settings or values.
		void Reset() override {
			Clear();
			Actor::Reset();
		}
#pragma endregion

#pragma region Getters and Setters
		/// Gets the moving deck Attachable of this AMover. Ownership is NOT transferred!
		Attachable* GetDeck() const { return m_Deck; }

		/// Sets the moving deck Attachable of this AMover. Ownership IS transferred!
		void SetDeck(Attachable* newDeck);

		/// How many stops this mover has.
		int GetStopCount() const { return static_cast<int>(m_Stops.size()); }

		/// The stop the deck is standing at, or -1 while it travels between stops.
		int GetCurrentStop() const { return m_Moving || m_StopIndex < 0 || m_StopIndex >= GetStopCount() || m_DeckOffset != m_Stops[m_StopIndex] ? -1 : m_StopIndex; }

		/// The stop the deck is travelling to, or the one it stands at.
		int GetTargetStop() const { return m_Moving ? m_TargetStop : (m_CalledStop >= 0 ? m_CalledStop : m_StopIndex); }

		/// Whether the deck is travelling.
		bool IsMoving() const { return m_Moving; }

		/// Where a unit stands on the deck at a stop: the middle of the deck's top, in scene coordinates. A zero vector for a stop that isn't one.
		/// @param stop The stop's index.
		Vector GetStopStandingPos(int stop) const;

		/// The middle of the deck's top where it is now, in scene coordinates.
		Vector GetDeckStandingPos() const;

		/// The deck's speed along its path, in pixels per second.
		float GetSpeed() const { return m_Speed; }

		/// Sets the deck's speed along its path, in pixels per second.
		void SetSpeed(float speed) { m_Speed = std::max(speed, 1.0F); }

		/// How long the deck stands at each stop before it moves on, in ms.
		int GetStopWait() const { return m_StopWait; }

		/// Sets how long the deck stands at each stop before it moves on, in ms.
		void SetStopWait(int stopWait) { m_StopWait = std::max(stopWait, 0); }

		/// Whether this mover waits at its stop until it is called (by a unit waiting at another stop, a rider, or CallTo), rather than shuttling.
		bool GetOnCall() const { return m_OnCall; }

		/// Sets whether this mover waits at its stop until it is called, rather than shuttling.
		void SetOnCall(bool onCall) { m_OnCall = onCall; }

		/// A mover belongs to no side: anyone rides it, and no side counts it as one of its units.
		void SetTeam(int team) override;

		/// Tells whether the player can switch control to this at all.
		bool IsControllable() const override { return false; }

		/// Gets the area of the deck drawn in the terrain, if it is drawn.
		/// @param box Set to the area.
		/// @return Whether the deck is drawn; the box is left as it was if not.
		bool GetDeckMaterialBox(Box& box) const;
#pragma endregion

#pragma region Concrete Methods
		/// Sends the deck to a stop, from wherever it is.
		/// @param stop The stop's index.
		void CallTo(int stop);
#pragma endregion

#pragma region Virtual Override Methods
		/// Destroys this AMover and creates its specified Gibs in its place with appropriate velocities.
		void GibThis(const Vector& impactImpulse = Vector(), MovableObject* movableObjectToIgnore = nullptr) override;

		/// Ensures all attachables and wounds are positioned and rotated correctly.
		void CorrectAttachableAndWoundPositionsAndRotations() const override;

		/// Updates this AMover. Supposed to be done every frame.
		void Update() override;

		/// A mover has no HUD.
		void DrawHUD(BITMAP* targetBitmap, const Vector& targetPos = Vector(), int whichScreen = 0, bool playerControlled = false) override {}
		void DrawHUD(const Camera& camera) override {}
#pragma endregion

	protected:
		static Entity::ClassInfo m_sClass; //!< ClassInfo for this class.

		Attachable* m_Deck; //!< The deck that moves. Owned by this.
		std::vector<Vector> m_Stops; //!< Where the deck stands at each stop, as its offset from this' position.
		float m_Speed; //!< How fast the deck travels, in pixels per second.
		int m_StopWait; //!< How long the deck stands at a stop before moving on, in ms.
		bool m_OnCall; //!< Whether the deck waits at its stop until called, rather than shuttling.

		Vector m_DeckOffset; //!< Where the deck is now, as its offset from this' position.
		int m_StopIndex; //!< The stop the deck stands at, or last left.
		int m_TargetStop; //!< The stop the deck travels to.
		int m_Direction; //!< Which way along the stops the deck shuttles: 1 up the list, -1 down it.
		bool m_Moving; //!< Whether the deck is travelling.
		int m_CalledStop; //!< A stop the deck was called to and hasn't reached yet, or -1.
		Timer m_StopTimer; //!< How long the deck has stood at its stop (or waited after being blocked).
		int m_PauseMS; //!< How long the deck stands before it moves on this time.
		Timer m_CallSenseTimer; //!< Times the look for units waiting at the other stops.
		Timer m_MaterialRedrawTimer; //!< Times the redraw of the deck's material while it stands, so a dug or blasted deck is mended.

		unsigned char m_DeckMaterialID; //!< The material the deck is drawn into the terrain with.
		std::vector<std::pair<int, int>> m_DrawnPixels; //!< The terrain pixels the deck turned from air into its material where it was last drawn.
		bool m_DeckMaterialDrawn; //!< Whether the deck's material is drawn into the terrain.
		Vector m_DrawnDeckPos; //!< Where the deck was when its material was drawn, whole pixels.

		int m_InitialSpriteAnimDuration; //!< The motor's animation time as set, for winding it down when the deck is lost.

		std::unique_ptr<SoundContainer> m_MoveStartSound; //!< Played when the deck sets off from a stop.
		std::unique_ptr<SoundContainer> m_MoveSound; //!< Played while the deck travels.
		std::unique_ptr<SoundContainer> m_MoveEndSound; //!< Played when the deck reaches a stop.

	private:
		/// The deck's position (the attachable's position, where its sprite is drawn from) for an offset from this' position, whole pixels.
		Vector DeckPosAt(const Vector& offset) const;

		/// The box of the deck's sprite with its position at a point.
		Box DeckBoxAt(const Vector& deckPos) const;

		/// Draws the deck's material into the terrain with it at a point: only over air (and cavity), recording what it turned.
		/// @return How many pixels of solid terrain (not liquid, not this deck's) lay where the deck would be.
		int DrawDeckMaterial(const Vector& deckPos, bool notifyGrid);

		/// Turns back to air the pixels the deck's material was drawn over that are still its material.
		void EraseDeckMaterial(bool notifyGrid);

		/// Counts the solid terrain (not liquid) where the deck's sprite would be with it at a point. Call with the deck's material erased.
		int SolidPixelsAt(const Vector& deckPos) const;

		/// Whether a body (an actor or item, not attached to anything) stands on the deck as it is drawn now.
		bool StandsOnDeck(const MovableObject* mo, float deckVelY) const;

		/// Whether a unit that isn't riding is in the way of the deck moving to a point (the MO layer where the deck would be).
		/// @param riders The roots riding the deck, left out.
		bool UnitInSweep(const Vector& toDeckPos, const std::vector<MovableObject*>& riders) const;

		/// The stop after the current one, shuttling back and forth along the list.
		int NextShuttleStop();

		/// Sets off for a stop.
		void SetOff(int stop);

		/// Arrives at the target stop.
		void Arrive();

		/// A unit standing where the deck would be at a stop it isn't at, wanting it: the first such stop, or -1.
		int StopWithUnitWaiting() const;

		/// Moves the deck along its path for this update, carrying its riders.
		void UpdateTravel();

		/// Clears all the member variables of this AMover, effectively resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		AMover(const AMover& reference) = delete;
		AMover& operator=(const AMover& rhs) = delete;
	};
} // namespace RTE
