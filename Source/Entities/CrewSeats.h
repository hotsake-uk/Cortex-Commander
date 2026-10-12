#pragma once

#include "Constants.h"
#include "Entity.h"
#include "Timer.h"
#include "Vector.h"

#include <vector>

struct BITMAP;

namespace RTE {

	class Actor;
	class Camera;
	class MovableObject;

	/// The seats of something units climb into and ride in, drawn sitting in them (VH-3): a vehicle's (AVehicle) or a flying ship's (ACShip).
	/// Seat 0 is the one it is driven or flown from; the others are passengers' and gunners'. Whoever gets in is taken out of the scene and a copy
	/// kept here (as a craft keeps who goes in), put back beside it when they get out. In an open one (DrawsCrew) they are drawn where they sit,
	/// and shots can reach them there; the dead fall out or slump in their seats. It belongs to the unit it is part of, its host, which hands
	/// it its updates.
	class CrewSeats {

	public:
		/// A seat: where its unit sits, and who that is.
		struct Seat {
			Vector Offset; //!< Where its unit sits (their middle), from the host's middle upright and facing right.
			bool Gunner = false; //!< Whether it mans a gun.
			bool Rider = false; //!< Whether its unit got in by itself to ride somewhere, and gets out when it is near there.
			Actor* Occupant = nullptr; //!< Who sits in it, out of the scene while they're in here. Owned.
			long TargetID = 0; //!< The enemy its passenger shoots at out of it (PassengersCanFire), or 0.
		};

		/// Makes an empty set of seats: the one seat 0, at the host's middle.
		CrewSeats() :
		    m_Host(nullptr) { Clear(); }

		/// Deletes whoever is in the seats, and the dead slumped in them.
		~CrewSeats() { DeleteCrew(); }

		/// Sets the unit these seats are part of. Done once, by it, as it is made.
		void SetHost(Actor* host) { m_Host = host; }

		/// Resets the seats to one empty seat 0 and the default settings, without deleting anyone in them (DeleteCrew does that).
		void Clear();

		/// Makes these the same as another host's seats: their settings, with copies of whoever is in them.
		/// @param reference The seats to copy.
		void Create(const CrewSeats& reference);

		/// Deletes whoever is in the seats, and the dead slumped in them, leaving them empty.
		void DeleteCrew();

#pragma region Settings
		/// Adds a seat besides seat 0: a passenger's, or a gunner's.
		/// @param offset Where its unit sits, from the host's middle upright and facing right.
		/// @param gunner Whether it mans a gun.
		void AddSeat(const Vector& offset, bool gunner) { m_Seats.push_back({offset, gunner}); }

		/// Gets where seat 0's unit sits, from the host's middle upright and facing right.
		const Vector& GetMainSeatOffset() const { return m_Seats[0].Offset; }

		/// Sets where seat 0's unit sits, from the host's middle upright and facing right.
		void SetMainSeatOffset(const Vector& offset) { m_Seats[0].Offset = offset; }

		/// Gets where units get out, from the host's middle upright and facing right (mirrored to whichever side is clear).
		const Vector& GetExitOffset() const { return m_ExitOffset; }

		/// Sets where units get out, from the host's middle upright and facing right.
		void SetExitOffset(const Vector& offset) { m_ExitOffset = offset; }

		/// Gets how close to a seat a unit has to be to get in.
		float GetBoardingReach() const { return m_BoardingReach; }

		/// Sets how close to a seat a unit has to be to get in.
		void SetBoardingReach(float reach) { m_BoardingReach = reach; }

		/// Gets whether the crew is drawn in their seats (an open one) or hidden inside (a closed one, a tank).
		bool DrawsCrew() const { return m_DrawsCrew; }

		/// Sets whether the crew is drawn in their seats, where shots can reach them, or hidden inside.
		void SetDrawsCrew(bool drawsCrew) { m_DrawsCrew = drawsCrew; }

		/// Gets whether units in its passenger seats shoot out of it with the guns they hold (when PassengersFireFromVehicles is on in the
		/// settings too). Never the one in seat 0, who drives or flies it, nor gunners, who man its guns.
		bool PassengersCanFire() const { return m_PassengersCanFire; }

		/// Sets whether units in its passenger seats shoot out of it (see PassengersCanFire).
		void SetPassengersCanFire(bool canFire) { m_PassengersCanFire = canFire; }
#pragma endregion

#pragma region Seats
		/// Gets how many seats there are, seat 0 included.
		int GetSeatCount() const { return static_cast<int>(m_Seats.size()); }

		/// Gets the seats, seat 0 first (for saving them).
		const std::vector<Seat>& GetSeats() const { return m_Seats; }

		/// Gets who sits in a seat. Ownership is NOT transferred!
		/// @param seat The seat: 0 the main one, 1 on the others.
		/// @return The unit in it, or nullptr if it is empty or there is no such seat.
		Actor* GetOccupant(int seat) const { return seat >= 0 && seat < GetSeatCount() ? m_Seats[seat].Occupant : nullptr; }

		/// Puts a unit straight in a seat, or empties it (nullptr), with nothing else done: no one is deleted, let out or told.
		/// @param seat The seat. @param unit The unit, whose ownership IS transferred, or nullptr.
		void SetOccupant(int seat, Actor* unit);

		/// Gets where a seat's unit sits, from the host's middle upright and facing right.
		const Vector& GetSeatOffset(int seat) const { return m_Seats[seat >= 0 && seat < GetSeatCount() ? seat : 0].Offset; }

		/// Gets whether a seat mans a gun.
		bool IsGunnerSeat(int seat) const { return seat >= 1 && seat < GetSeatCount() && m_Seats[seat].Gunner; }

		/// Gets which gun a seat mans: 0 for the first gunner's seat, 1 for the second, and so on; -1 if it isn't a gunner's.
		int GetGunOfSeat(int seat) const;

		/// Gets the seat that mans a gun: the first gunner's seat for gun 0, and so on.
		/// @return The seat, or -1 if there aren't that many gunners' seats.
		int GetSeatOfGun(int gun) const;

		/// Gets whether a seat's unit got in by itself to ride somewhere.
		bool IsRider(int seat) const { return seat >= 0 && seat < GetSeatCount() && m_Seats[seat].Rider; }

		/// Sets whether a seat's unit got in by itself to ride somewhere.
		void SetRider(int seat, bool rider);

		/// Gets how many units are in the seats.
		int GetCrewCount() const;

		/// Gets whether nobody alive is in it and no dead are slumped in it.
		bool IsEmpty() const { return GetCrewCount() == 0 && m_Slumped.empty(); }

		/// Gets the seat a unit getting in would take: seat 0 if free, then a gunner's, then a passenger's.
		/// @return The seat, or -1 if they are all taken.
		int GetFreeSeat() const;

		/// Gets the seat of the unit the player in charge of the host sits in: the one they got in as, or seat 0.
		/// @return The seat, or -1 with nobody in it for them.
		int GetControlSeat() const;

		/// Gets the seat the unit of the player in charge got in to, or -1.
		int GetPlayerSeat() const { return m_PlayerSeat; }
#pragma endregion

#pragma region Saved Games
		/// Notes the seat (and whether its unit is a rider) the next unit read from a saved game goes in (LoadSeated).
		/// @param seat The seat. @param rider Whether its unit got in by itself to ride somewhere.
		void SetSeatToLoad(int seat, bool rider) {
			m_SeatToLoad = seat;
			m_RiderToLoad = rider;
		}

		/// Puts a unit read from a saved game back in the seat SetSeatToLoad named, as it sat when the game was saved: nobody is copied, deleted
		/// or told. With no such seat free (the preset has changed), the unit is deleted.
		/// @param unit The unit. Ownership IS transferred.
		void LoadSeated(Actor* unit);
#pragma endregion

#pragma region Getting In And Out
		/// Gets whether a unit can get in: alive, a humanoid, not a brain, and on the host's team (or, with anyTeam, any team's).
		/// @param unit The unit. @param anyTeam Whether a unit of another team can get in too (an empty ship anyone can take).
		bool CanCarry(const Actor* unit, bool anyTeam = false) const;

		/// Puts a unit in a seat, taking it out of the scene: a copy of it is kept and it is deleted, and the side isn't counted a loss for it. The
		/// player goes with it into the host, if it was theirs. A unit of another team (anyTeam) takes the host over for its team.
		/// @param unit The unit getting in. @param seat The seat, or -1 for the one GetFreeSeat gives. @param anyTeam See CanCarry.
		/// @return Whether it got in.
		bool TakeSeat(Actor* unit, int seat, bool anyTeam = false);

		/// Puts a unit just made, not in the scene (a vehicle spawned with its crew), straight in a seat, as TakeSeat does but with no copy
		/// kept: nobody is deleted or told, and no death reported. @param unit The unit. Ownership IS transferred if it went in.
		/// @param seat The seat, or -1 for the one GetFreeSeat gives. @return Whether it went in: a free seat, and a unit that could get in.
		bool SeatNew(Actor* unit, int seat);

		/// Lets the unit in a seat out beside the host, back into the scene; the player goes with them if they were in charge from that seat.
		/// @param seat The seat. @param thrown Whether they are thrown out (a crash, a roll, a wreck): flung clear with the speed it had, and
		/// put out above it if there's no room beside. @param fallen Whether it's a body falling out of its seat: out whatever is there.
		/// @return The unit that got out, or nullptr if the seat was empty or there was no room to get out.
		Actor* LetOut(int seat, bool thrown, bool fallen = false);

		/// Lets everyone in it out, or throws them out, and tips out the dead slumped in it.
		void EjectCrew(bool thrown);

		/// Tips out every body slumped in its seats, beside it or (thrown) flung with the speed it had.
		void TipOutAllSlumped(bool thrown);

		/// Takes everyone out of the seats and hands them over, as they are (out of the scene), deleting the dead slumped in them: a ship going
		/// to orbit takes its crew with it in its hold.
		/// @return The units that were in the seats. Ownership IS transferred!
		std::vector<Actor*> TakeAllCrew();

		/// Moves the unit the player is in charge of to the next empty seat, as the Change Seat pie slice does.
		/// @return Whether they moved.
		bool ChangeSeat();

		/// Gets the timer since someone last got in or out, so the key that did it doesn't do the opposite straight after.
		Timer& GetBoardingTimer() { return m_BoardingTimer; }
		const Timer& GetBoardingTimer() const { return m_BoardingTimer; }

		/// Lets the player in charge get out with the pick-up key, and a friendly player's unit in reach of a seat that asks to get in, get in.
		/// @param pushOver Whether a unit in reach pushes the host back over instead of getting in (it lies on its side or roof).
		/// @param anyTeam Whether a player's unit of another team can get in too (see CanCarry).
		void UpdateBoarding(bool pushOver, bool anyTeam = false);

		/// Gets the friendly player's unit beside a seat in the last UpdateBoarding, for the hint to get in. Not owned.
		Actor* GetBoarderInReach() const { return m_BoarderInReach; }

		/// Gets whether that unit would push the host back over rather than get in.
		bool GetBoarderPushes() const { return m_BoarderPushes; }

		/// Gets whether a unit is in reach of any seat.
		/// @param unit The unit. @param extraReach How much further than the boarding reach counts.
		bool IsInReach(const Actor* unit, float extraReach) const;
#pragma endregion

#pragma region Updating And Drawing
		/// Lets the crew in open seats bleed from their wounds and take the damage they've been dealt there, and sees to those who die of it:
		/// they fall out (DeadCrewFallOut), or slump in their seats. Anyone blown apart in their seat is gone from it.
		void UpdateSeatedCrew();

		/// Notes the host's velocity at the end of its update, which units thrown out are flung on with.
		void SetLastVel(const Vector& vel) { m_LastVel = vel; }

		/// Gets the host's velocity at the end of its last update.
		const Vector& GetLastVel() const { return m_LastVel; }

		/// Puts everyone in it at their seats, as the host lies now, so they are drawn there; the dead slumped forward in theirs.
		void PlaceCrew() const;

		/// Lets the units in its passenger seats shoot out of it (PassengersCanFire), as it lies now (call after PlaceCrew): the player's own
		/// unit where the player aims, with the fire key; the others at the nearest enemy in front of them they can see. What they fire goes
		/// through the host and everyone sat in it.
		void UpdatePassengerFire();

		/// Draws the crew in open seats (call before drawing the host, so its near side hides their legs).
		void Draw(BITMAP* targetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const;
		void Draw(const Camera& camera) const;

		/// Gives the crew in open seats their IDs, each its own root, so they're drawn to the layer shots look for MOs on.
		void UpdateMOIDs(std::vector<MovableObject*>& MOIDIndex) const;

		/// Puts the IDs of the crew in open seats in a list.
		void GetMOIDs(std::vector<MOID>& MOIDs) const;
#pragma endregion

	private:
		/// Someone killed in a seat, left slumped in it (DeadCrewFallOut off).
		struct SlumpedBody {
			Actor* Body = nullptr; //!< Owned.
			int Seat = 0; //!< The seat they slump in, which is free for someone to take (they tip the body out getting in).
		};

		Actor* m_Host; //!< The unit these seats are part of. Not owned.
		std::vector<Seat> m_Seats; //!< The seats, seat 0 first.
		std::vector<SlumpedBody> m_Slumped; //!< The dead slumped in its seats, out of the scene like the crew.
		int m_PlayerSeat; //!< The seat the unit of the player in charge got in to, or -1.
		Vector m_ExitOffset; //!< Where units get out, from the host's middle, upright and facing right.
		float m_BoardingReach; //!< How close to a seat a unit has to be to get in.
		bool m_DrawsCrew; //!< Whether the crew is drawn in their seats, or hidden inside.
		bool m_PassengersCanFire; //!< Whether units in its passenger seats shoot out of it (see PassengersCanFire).
		Timer m_PassengerTargetTimer; //!< Since its passengers last looked round for enemies to shoot at.
		Vector m_LastVel; //!< The host's velocity at the end of its last update.
		Timer m_BoardingTimer; //!< Since someone last got in or out.
		Actor* m_BoarderInReach; //!< A friendly player-controlled unit beside a seat in the last UpdateBoarding. Not owned.
		bool m_BoarderPushes; //!< Whether that unit would push the host back over rather than get in.
		int m_SeatToLoad; //!< The seat the next unit read from a saved game goes in (SetSeatToLoad).
		bool m_RiderToLoad; //!< Whether that unit is a rider.

		/// Finds the enemy a passenger would shoot at out of it: the nearest alive in front of them, within their sight and aim, with nothing
		/// solid between. @param unit The passenger. @return Its unique ID, or 0 if there is none.
		long FindPassengerTarget(const Actor* unit) const;

		/// Tips a body slumped in a seat out of it, into the scene beside the host (or flung, from a crash or a wreck).
		void TipOutSlumped(size_t index, bool thrown);

		// Not copied as they are: the units in them are owned (Create copies them).
		CrewSeats(const CrewSeats& reference) = delete;
		CrewSeats& operator=(const CrewSeats& rhs) = delete;
	};
} // namespace RTE
