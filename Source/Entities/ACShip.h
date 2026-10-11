#pragma once

#include "ACDropShip.h"
#include "CrewSeats.h"

#include <deque>

namespace RTE {

	class Turret;

	/// A flying ship (FS-1): a craft that flies as a drop ship does (its tilting engines, its steering, its hover autopilot, its hold and hatch),
	/// with a crew you can see. A unit climbs into the pilot's seat (seat 0) and flies it, gunners man its guns, and passengers ride in its
	/// open seats, where shots can reach them, as in a vehicle (CrewSeats). A small ship needs a pilot (NeedsPilot): with nobody at the
	/// controls it sets itself down gently and shuts off its engines until someone gets in. A big one, a frigate, flies itself with nobody
	/// aboard. An empty ship can be taken by anyone: whoever gets in first, of whatever team, takes it for their side.
	class ACShip : public ACDropShip {
		friend struct EntityLuaBindings;

	public:
		EntityAllocation(ACShip);
		SerializableOverrideMethods;
		ClassInfoGetters;
		DefaultPieMenuNameGetter("Default Ship Pie Menu");

#pragma region Creation
		/// Constructor method used to instantiate a ACShip object in system memory. Create() should be called before using the object.
		ACShip();

		/// Makes the ACShip object ready for use.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create() override { return ACDropShip::Create(); }

		/// Creates a ACShip to be identical to another, by deep copy.
		/// @param reference A reference to the ACShip to deep copy.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create(const ACShip& reference);
#pragma endregion

#pragma region Destruction
		/// Destructor method used to clean up a ACShip object before deletion from system memory.
		~ACShip() override;

		/// Destroys and resets (through Clear()) the ACShip object.
		/// @param notInherited Whether to only destroy the members defined in this derived class, or to destroy all inherited members also.
		void Destroy(bool notInherited = false) override;

		/// Resets the entire ACShip, including its inherited members, to their default settings or values.
		void Reset() override {
			Clear();
			ACDropShip::Reset();
		}
#pragma endregion

#pragma region Getters and Setters
		/// Gets the unit flying this, if any. Ownership is NOT transferred!
		Actor* GetPilot() const { return m_Crew.GetOccupant(0); }

		/// Gets whether a unit is in the pilot's seat.
		bool HasPilot() const { return m_Crew.GetOccupant(0) != nullptr; }

		/// Gets whether it only flies with someone in the pilot's seat (a small ship), or flies itself (a frigate).
		bool NeedsPilot() const { return m_NeedsPilot; }

		/// Gets whether it can fly now: someone is at the controls, or it flies itself, and it isn't wrecked.
		bool CanFly() const { return (HasPilot() || !m_NeedsPilot) && m_Status != DYING && m_Status != DEAD; }

		/// Gets whether it is sat on the ground with its engines off, waiting for a pilot.
		bool IsParked() const { return m_Parked; }

		/// Gets how many seats it has: the pilot's (seat 0) and any others (1 on), passengers' and gunners'.
		int GetSeatCount() const { return m_Crew.GetSeatCount(); }

		/// Gets who sits in a seat. Ownership is NOT transferred!
		/// @param seat The seat: 0 the pilot's, 1 on the others.
		Actor* GetSeatOccupant(int seat) const { return m_Crew.GetOccupant(seat); }

		/// Gets whether a seat mans a gun.
		bool IsGunnerSeat(int seat) const { return m_Crew.IsGunnerSeat(seat); }

		/// Gets how many units are in its seats, pilot included (not those in its hold).
		int GetCrewCount() const { return m_Crew.GetCrewCount(); }

		/// Gets the seat a unit getting in would take: the pilot's if free, then a gunner's, then a passenger's; -1 if they are all taken.
		int GetFreeSeat() const { return m_Crew.GetFreeSeat(); }

		/// Gets the seat of the unit the player in charge of this sits in, or -1 with nobody in it for them (they took charge of it another way).
		int GetControlSeat() const { return m_Crew.GetControlSeat(); }

		/// Gets whether a unit could get in now: one of its team's, or anyone's while it is empty (and can be taken).
		bool CanTakeIn(const Actor* unit) const;

		/// Puts a unit in a seat, taking it out of the scene. A unit of another team, getting into an empty ship, takes it for their side.
		/// @param unit The unit getting in. @param seat The seat, or -1 for the one GetFreeSeat gives.
		/// @return Whether it got in.
		bool TakeSeat(Actor* unit, int seat);

		/// Lets the unit in a seat out beside this, back into the scene. See CrewSeats::LetOut.
		Actor* EjectSeat(int seat, bool thrown) { return m_Crew.LetOut(seat, thrown); }

		/// Lets everyone in its seats out, or throws them out.
		void EjectCrew(bool thrown) { m_Crew.EjectCrew(thrown); }

		/// Puts a unit just made, not in the scene (a ship spawned with its crew), straight in a seat: nobody is copied, deleted or told.
		/// @param unit The unit. Ownership IS transferred if it went in. @param seat The seat, or -1 for the one GetFreeSeat gives.
		/// @return Whether it went in.
		bool SeatNewCrew(Actor* unit, int seat = -1) { return m_Crew.SeatNew(unit, seat); }

		/// Moves the unit the player is in charge of to the next empty seat, as the Change Seat pie slice does.
		bool ChangeSeat() { return m_Crew.ChangeSeat(); }

		/// Sends it flying to a place by its autopilot, straight there, to hold still at it (a go-to whose last point it is). For a battle mode
		/// steering an AI crew's ship. Nothing happens while it can't fly or its autopilot hasn't started yet (nobody has flown it).
		/// @param spot Where its middle goes. @return Whether it was sent.
		bool FlyTo(const Vector& spot);

		/// Sends it to hover over a place, as a move order does: at its hover height over the ground there, or at the place if that is in the
		/// open air higher up. Nothing happens while it can't fly or its autopilot hasn't started yet. @return Whether it was sent.
		bool HoverOver(const Vector& spot) {
			m_Route.clear();
			return CanFly() && AutopilotHoverOver(spot);
		}

		/// Sends it to hover over a place as the player's commands do (VC-3): by the risk of its standing order (StandingOrder::Direct), along a
		/// route round what is in the way, the route the pathfinder gives (with its care for enemies on a cautious order), flown at its hover
		/// height over the ground along it and straight across wherever the air is open. Straight there, as HoverOver, when the way is open and
		/// the order isn't cautious, or on a full-risk order. A cautious order also flies higher. Nothing happens while it can't fly or its
		/// autopilot hasn't started yet. @param spot The place. @return Whether it was sent.
		bool FlyRouteTo(const Vector& spot);

		/// Gets the points of the route FlyRouteTo sent it along that it has still to fly to, the next first.
		const std::deque<Vector>& GetRoute() const { return m_Route; }

		/// Gets how far its gunners look for something to shoot, in pixels.
		float GetGunRange() const { return m_GunRange; }
#pragma endregion

#pragma region Override Methods
		/// What this ship is to the path grid, for a route round what is in the way (FlyRouteTo): it flies, digs nothing, and wants the room it
		/// is high (as much as the grid tells) over the ground the route follows. The order's risk and the route caution as any unit's.
		PathAgent GetPathAgent() const override;

		/// The drop ship's autopilot, while it can fly, taking it along the route FlyRouteTo sent it on a point at a time. With nobody at the
		/// controls of a ship that needs a pilot, it does nothing (the ship sets itself down) and any delivery it was on is dropped for holding
		/// still, so a pilot getting in later doesn't take it off home.
		void UpdateAutopilot() override;

		/// The drop ship's flying from the controller, unless nobody flies it: then the stick is let go, and a ship that needs a pilot sets
		/// itself down and shuts off. A player at a gun or in a passenger's seat doesn't fly it: it holds where it is.
		void PreControllerUpdate() override;

		/// Updates this MovableObject. Supposed to be done every frame.
		void Update() override;

		/// Tries to handle the activated PieSlice in this object's PieMenu, if there is one, based on its SliceType.
		bool HandlePieCommand(PieSliceType pieSliceType) override;

		/// Gibs this, its crew thrown clear first.
		void GibThis(const Vector& impactImpulse = Vector(), MovableObject* movableObjectToIgnore = nullptr) override;

		/// Puts the IDs of this and its parts in a list, and of the crew sat in its open seats (whom bullets can hit there).
		void GetMOIDs(std::vector<MOID>& MOIDs) const override;

		/// Draws this ACShip: its crew in their seats first, so the hull's near side hides them where it covers them.
		void Draw(BITMAP* pTargetBitmap, const Vector& targetPos = Vector(), DrawMode mode = g_DrawColor, bool onlyPhysical = false) const override;
		void Draw(const Camera& camera) const override;

		/// Draws this Actor's HUD: with it, the hint to get in for a unit beside it, and what the player's seat does.
		void DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos = Vector(), int whichScreen = 0, bool playerControlled = false) override;
		void DrawHUD(const Camera& camera) override;
#pragma endregion

	protected:
		static Entity::ClassInfo m_sClass; //!< ClassInfo for this class.

		/// Gives the attachables IDs, then the crew in its open seats, so they're drawn to the layer that shots look for MOs on.
		void UpdateChildMOIDs(std::vector<MovableObject*>& MOIDIndex, MOID rootMOID = g_NoMOID, bool makeNewMOID = true) override;

		/// How one of its manned guns (a Turret) is aimed.
		struct GunState {
			long TurretID = 0; //!< The unique ID of the turret it is.
			float AimAngle = 0.0F; //!< Radians above level, the way the gun faces.
			long TargetID = 0; //!< The unique ID of what its AI gunner is shooting at, or 0.
			Timer TargetTimer; //!< Since its gunner last looked round for something to shoot.
			Vector SuppressPoint; //!< Where in the place it was ordered to suppress its gunner is firing at, with nobody to shoot.
			Timer SuppressTimer; //!< Since its gunner last picked a point in that place.
		};

		CrewSeats m_Crew; //!< Its seats, the pilot's (seat 0) and the rest, and who sits in them.
		bool m_NeedsPilot; //!< Whether it only flies with someone in the pilot's seat.
		bool m_CanBeTaken; //!< Whether anyone can get in while it's empty, taking it for their side.
		bool m_SeatsCargo; //!< Whether the units it is bought with take its seats, the first to fly it, rather than ride in its hold.
		float m_CrewThrowSpeed; //!< How hard a knock (a change in speed in one update, m/s) throws its crew out. 0 never does.
		float m_GunRange; //!< How far a gunner looks for something to shoot, in pixels.
		float m_GunTurnSpeed; //!< How fast a gunner swings a gun round, in radians a second.
		float m_GunAimRange; //!< How far a gun swings up or down from level, in radians.
		std::vector<GunState> m_Guns; //!< Its manned guns' aim, by turret.
		bool m_Parked; //!< Whether it is sat on the ground with its engines off (nobody at the controls of a ship that needs a pilot).
		Timer m_TouchdownTimer; //!< How long it has been down and still while setting itself down.
		Timer m_OrderedBoardTimer; //!< Since it last looked for units sent to it to get in.
		std::deque<Vector> m_Route; //!< The points of the route FlyRouteTo sent it along still to fly to, the one it is flying to first.

	private:
		/// Aims and fires its manned guns: from the player's aim in a gunner's seat, or the gunner's own, at the nearest enemy in sight and range.
		void UpdateGuns();

		/// Gets the nearest enemy a gun can shoot at: in range, on its side, within its swing and in sight.
		/// @param turret The gun. @return The unique ID of the enemy, or 0.
		long FindGunTarget(const Turret* turret) const;

		/// Lets in the AI units sent to it (a move order onto it) once they get here.
		void UpdateAIBoarding();

		/// Moves the units in its hold into its empty seats (SeatsCargo): a ship bought with its crew comes in crewed.
		void SeatCargo();

		/// Clears all the member variables of this ACShip, effectively resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		ACShip(const ACShip& reference) = delete;
		ACShip& operator=(const ACShip& rhs) = delete;
	};
} // namespace RTE
