#pragma once

#include "ACDropShip.h"
#include "AirRoute.h"
#include "CrewSeats.h"

#include <algorithm>
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

		static constexpr float c_DropOffRoom = 40.0F; //!< How much higher than LowPoint a ship hovers to let its passengers out (DropOffPoint).

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

		/// Gets whether its engines are shut off where it has landed (a Land order, as a vehicle's handbrake): it stays down till sent anywhere
		/// or flown.
		bool IsEnginesOff() const { return m_EnginesOff; }

		/// Shuts its engines off where it is (once landed), or starts them again. Sending it anywhere, or a player flying it, starts them.
		void SetEnginesOff(bool off) { m_EnginesOff = off; }

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

		/// Sends it to hover over a place as the player's commands do (VC-3): straight there where its whole hull has open air all the way and
		/// the order isn't cautious (or is full-risk), else along a route through the air round what is in the way, terrain, buildings and
		/// doors alike (AirPath), kept off enemies on a cautious order, as near as it can get where there is no way all the way. A cautious order
		/// also flies higher. Nothing happens while it can't fly or its autopilot hasn't started yet. @param spot The place. @return Whether it
		/// was sent.
		bool FlyRouteTo(const Vector& spot);

		/// Gets how far over the ground its middle keeps when sent down low: how far its hull (and the guns under it) reaches below its
		/// middle, and the gap the Ship ground clearance setting keeps under that.
		float LowClearance() const;

		/// Gets how far its hull reaches below its middle, level: the bottom of its picture, or of a gun or engine hung lower.
		float HullUnderside() const;

		/// Gets half the width of its hull, level: the further of its picture's two ends from its middle, or of an engine or gun out further.
		float HullHalfWidth() const;

		/// Gets how far its hull reaches above its middle, level: the top of its picture, or of an engine or gun up higher.
		float HullTop() const;

		/// Gets whether there's open air for its whole hull, engines and guns and all, flown straight from one place to another.
		/// @param from Where it starts. @param to Where it goes. @return Whether nothing is in the way.
		bool HullLineClear(const Vector& from, const Vector& to) const;

		/// Its hull's shape for planning a way through the air (AirRoute): its ends, top and underside, the last with half its ground gap.
		AirBody GetAirBody() const;

		/// Gets how much past the map's edges it may go before it counts as gone to orbit: the Ship room past map edges setting.
		float GetOrbitGrace() const override;

		/// Gets the highest it is sent to in the scene: as far over the top of the map as its room past the edges lets it, its whole body
		/// within that. (A y, which is down the scene.)
		float SkyTop() const { return GetRadius() - GetOrbitGrace(); }

		/// Gets the height of its hull's picture.
		float HullHeight() const { return static_cast<float>(std::max(GetSpriteHeight(), 1)); }

		/// Gets the place it flies to when sent to a spot: the spot itself, raised only as far as it must be to clear the ground under it.
		/// @param spot The spot. @param extraAltitude How much higher than that to keep.
		Vector LowPoint(const Vector& spot, float extraAltitude = 0.0F);

		/// Gets the place it hovers at to let its passengers out over a spot, or to wait when it comes in: low over the ground there, with
		/// room under it for them to drop out and land on their feet. @param spot The spot.
		Vector DropOffPoint(const Vector& spot) { return LowPoint(spot, c_DropOffRoom); }

		/// Gets the points of the route FlyRouteTo sent it along that it has still to fly to, the next first.
		const std::deque<Vector>& GetRoute() const { return m_Route; }

		/// Gets where it is flying to on a go-to: the end of its route, else the autopilot's place. @param goal Set to the place. @return Whether it is going anywhere.
		bool GetFlightGoal(Vector& goal) const {
			if (m_AIMode == AIMODE_GOTO && !m_Route.empty()) {
				goal = m_Route.back();
				return true;
			}
			return AutopilotGoal(goal);
		}

		/// Sends it after an enemy (an attack order): it doesn't hover over the enemy, where its side guns can't bear, but fights it from a
		/// firing position off to one side (UpdateCombat). Sent after the same enemy again, it keeps the position it has.
		/// @param target The enemy. @return Whether it can fly there (false while it can't fly or its autopilot hasn't started).
		bool EngageTarget(const Actor* target);

		/// Sends it to ram an enemy (a Ram order, or by itself once its guns are all shot off): it backs off for a run-up, charges it at speed
		/// (RamSpeedScale times its top speed) and pulls up past it, then comes round and goes again, till the enemy is dead or it is given any
		/// other order. Each hit hurts the enemy by how fast it struck (RamDamage) and the ship a share of that (RamSelfDamage).
		/// @param target The enemy. @param automatic Whether it took this on itself (its guns gone): it gives up on one that gets far off.
		/// @return Whether it can (false while it can't fly or its autopilot hasn't started).
		bool RamTarget(const Actor* target, bool automatic = false);

		/// Sends it to ram a place (a Ram order on no enemy, as a suppress is given): run after run through it, as RamTarget, striking whatever
		/// that isn't its own side's it meets there, till it is given another order.
		/// @param point The place. @return Whether it can.
		bool RamPlace(const Vector& point);

		/// Stops ramming: any other order.
		void StopRam() { m_RamID = 0; m_RamAtPlace = false; }

		/// Gets whether it is ramming an enemy or a place (RamTarget, RamPlace).
		bool IsRamming() const { return m_RamID != 0 || m_RamAtPlace; }

		/// Gets the unique ID of the enemy it is ramming, 0 for none.
		long GetRamTargetID() const { return m_RamID; }

		/// Gets the unique ID of the enemy EngageTarget sent it after, 0 for none (or once that enemy is dead or gone).
		long GetEngageID() const { return m_EngageID; }

		/// Drops the enemy EngageTarget sent it after: any other order.
		void Disengage() { m_EngageID = 0; m_HasCombatSpot = false; }

		/// Gets whether it is moving about a fight (UpdateCombat): its post's pull (ReturnDefenders) waits while it is near the post.
		bool IsManoeuvring() const { return m_HasCombatSpot; }

		/// Gets how far its gunners look for something to shoot, in pixels.
		float GetGunRange() const { return m_GunRange; }

		/// Gets where its flight controller is steering it just now (UpdateFlight): the place it is flying to or holding at, raised over the
		/// ground ahead and moved off to keep clear of other craft. For the debug overlay.
		const Vector& GetFlightTarget() const { return m_FlightTarget; }

		/// Gets the lowest it may fly just now for the ground ahead (a scene height: the larger, the lower), FLT_MAX for no limit. For the debug overlay.
		float GetFlightFloor() const { return m_FlightFloor; }

		/// Gets the place it holds at while not going anywhere: where it was told to go, or where it was let go of, at that height.
		const Vector& GetHoldPoint() const { return m_HoldPoint; }

		/// Gets whether it has a place to hold at yet (GetHoldPoint).
		bool HasHoldPoint() const { return m_HasHoldPoint; }

		/// Gets its seats and who sits in them (for saving them).
		const CrewSeats& GetCrewSeats() const { return m_Crew; }

		/// Gets whether it is taking AI units somewhere that asked for a ride (SettingsMan::AIRidesShips): calling them over and waiting for
		/// them, or flying them there and letting them out (UpdateAIRides).
		bool IsGivingRide() const { return m_RideStage != RideStage::None; }

		/// Gets whether it may be called on for a ride now: its AI flies it (or can, once someone gets in), nobody has given it an order, no
		/// player flies it or sits in it, and it isn't in a fight, ramming or airlifting.
		bool OffersRides() const;
#pragma endregion

#pragma region Override Methods
		/// What this ship is to the path grid, for a route round what is in the way (FlyRouteTo): it flies, digs nothing, and wants the room it
		/// is high (as much as the grid tells) over the ground the route follows. The order's risk and the route caution as any unit's.
		PathAgent GetPathAgent() const override;

		/// The ship's own flight (not the drop ship's autopilot): while it can fly, it is taken along the route FlyRouteTo sent it on a point at a
		/// time by its flight controller (UpdateFlight), which holds the height it is given. With nobody at the controls of a ship that needs a
		/// pilot, it does nothing (the ship sets itself down) and any delivery it was on is dropped for holding still, so a pilot getting in later
		/// doesn't take it off home.
		void UpdateAutopilot() override;

		/// Gets how far the autopilot steers off its place to keep clear of other craft (UpdateCraftAvoidance).
		Vector AutopilotSteerOffset() const override { return m_AvoidOffset; }

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
		bool m_EnginesOff; //!< Whether its engines are shut off where it landed (IsEnginesOff).
		Timer m_TouchdownTimer; //!< How long it has been down and still while setting itself down.
		Timer m_OrderedBoardTimer; //!< Since it last looked for units sent to it to get in.
		std::deque<Vector> m_Route; //!< The points of the route FlyRouteTo sent it along still to fly to, the one it is flying to first.
		Vector m_AvoidOffset; //!< How far the autopilot steers off its place just now to keep clear of other craft.
		Timer m_AvoidTimer; //!< Since it last looked for craft in its way.

		/// Looks, ten times a second, for other craft it would come within a body of soon at the speeds both are going, and sets how far to
		/// steer off its place to pass them: the higher of two ships climbs over, the other holds back and gives way to the side; one that isn't
		/// a ship flown by the autopilot (a drop ship, a rocket, one a player flies) is passed by both. Not read until the autopilot steers.
		void UpdateCraftAvoidance();

		float m_FlightSpeed; //!< Its top speed across, in m/s (FlightSpeed).
		float m_ClimbSpeed; //!< Its top speed up, in m/s (ClimbSpeed); down is a little slower. At most the drop ship's throttle range, 7.5.
		Vector m_HoldPoint; //!< Where it holds while not going anywhere (GetHoldPoint).
		bool m_HasHoldPoint; //!< Whether it has a place to hold at yet.
		Vector m_FlightTarget; //!< Where the controller is steering it just now (GetFlightTarget).
		float m_FlightFloor; //!< The lowest it may fly just now for the ground ahead (GetFlightFloor).
		Vector m_SteerTrim; //!< What the controller has built up, near a place it stops at, against whatever drifts it off (its trim, its load, a lean): the off, in metre seconds.
		Timer m_FlownTimer; //!< Since its flight controller last ran: a gap means a player flew it.
		bool m_HeldByPause; //!< Whether its autopilot is holding it for the paused AI (Controller::AIMayAct).
		Vector m_PausedAt; //!< Where it hovers while the AI is paused.
		/// What a saved game it was read from had it doing in the air, taken up when its flight controller first runs (RestoreFlight).
		enum class FlightToRestore { None, Hold, Goal };
		FlightToRestore m_FlightToRestore; //!< What it was doing.
		Vector m_GoalToRestore; //!< Where it was going, with Goal.

		/// Takes up what a saved game had it doing in the air (m_FlightToRestore), once its autopilot has started: on its way along the route
		/// it had, or to the place it was going; or holding where it held, without the drop of a ship just come in to its hover height.
		void RestoreFlight();

		/// Its flight controller, each AI update: what it is to do by its AI mode (hold where it was told or let go, fly to its go-to's place,
		/// deliver its hold's cargo, go home), then SteerTo there.
		void UpdateFlight();

		/// Steers it toward a place: a speed wanted across and up or down, by how far off it is (braking to stop there, or flying on through it
		/// to the next point of a route), turned into the drop ship's stick (an up or down speed, and a sideways tilt). The place is raised to
		/// keep clear of the ground ahead along the way it goes, so it climbs over a rise rather than flying into it, and slows across till it
		/// has; though not near the place itself, so it can come down onto the ground there.
		/// @param target The place, its middle there. @param stop Whether to stop there (else it is a point of a route flown through).
		/// @param clearanceScale How much of its usual clearance over the ground ahead it keeps (less when it rams something on the ground).
		void SteerTo(const Vector& target, bool stop, float clearanceScale = 1.0F);

		/// The top of the ground at a point of the scene: the first air going up from it if it is in the ground, FLT_MAX if it is in the open air
		/// (nothing to keep over there).
		float GroundTopAt(float x, float y) const;

		/// Plans a route through the air from one place to another (FlyRouteTo), over, under or round whatever is in the way, terrain and
		/// buildings and doors alike: the shortest way on a grid of the places its whole hull fits (its ends, engines and guns, and its gap over
		/// the ground under it), however high that takes it, and, on a cautious order, kept off enemies. Then cut to as few
		/// points as have open air for its hull between (HullLineClear). With no way all the way there, the route goes as near as it can get.
		/// @param from Where it starts. @param to Where it is going. @param care How much a cautious order keeps it off enemies, 0 to 1.
		/// @param route Filled with the route's points, the first to fly to first. @param reached Set to whether the route gets all the way there.
		/// @return Whether a route was found at all (false where it has no room to start from).
		bool AirPath(const Vector& from, const Vector& to, float care, std::deque<Vector>& route, bool& reached) const;

		Vector m_RouteSpot; //!< The place FlyRouteTo was last sent to, as given, for planning again when the way it has is blocked.
		Timer m_RouteCheckTimer; //!< Since it last looked whether the way to its route's next point is still open.
		Timer m_RouteProgressTimer; //!< Since it last got nearer its route's next point.
		float m_RouteBest; //!< The nearest it has got to its route's next point, for telling it is stuck.
		int m_RouteReplans; //!< How many times running it has planned its route again without getting to a point of it.
		int m_UnstickStep; //!< Which of its moves to work itself free it is making, -1 for none (UpdateRoute).
		int m_UnstickTries; //!< How many times running it has tried to work itself free without getting anywhere.
		Timer m_UnstickTimer; //!< Since it began the move it is making to work itself free.
		Vector m_UnstickFrom; //!< Where it got stuck, its moves to work free made from.
		Vector m_UnstickPoint; //!< Where the move it is making takes it.
		Vector m_UnstickGoal; //!< Where it was going when it got stuck, planned for again once free.
		float m_UnstickSide; //!< Which way it was going across (1 right, -1 left), so it backs off the other way.

		/// Follows the route FlyRouteTo planned: on to the next point once near the one it is flying to; planned again from where it is if the
		/// way to that point is found blocked (terrain dug or built). On any go-to, if it gets no nearer where it is going for a while, barely
		/// moving (caught on something), it works itself free with a few short moves (back and up, up, back and down, on and up), as a unit on
		/// foot does, then plans its way there again.
		void UpdateRoute();

		/// Picks the next of its moves to work itself free (UpdateRoute) that its hull has the room for. @return Whether there was one.
		bool NextUnstickMove();

		long m_EngageID; //!< The unique ID of the enemy an attack order sent it after (EngageTarget), 0 for none.

		/// Where it is in a ram (RamTarget).
		enum class RamPhase {
			RunUp, //!< Backing off to one side of the enemy for a run at it.
			Charge, //!< Flying at it flat out.
			PullAway //!< Past it or having hit it: climbing away beyond it, to come round again.
		};
		long m_RamID; //!< The unique ID of the enemy it is ramming, 0 for none.
		bool m_RamAuto; //!< Whether it took the ram on itself, its guns gone.
		bool m_RamAtPlace; //!< Whether it is ramming a place (RamPlace) rather than an enemy.
		Vector m_RamPoint; //!< That place.
		RamPhase m_RamPhase; //!< Where it is in the ram.
		float m_RamSide; //!< Which side of the enemy it charges from: 1 its right, -1 its left.
		Timer m_RamTimer; //!< Since this part of the ram began.
		Timer m_RamHitTimer; //!< Since it last struck the enemy.
		Timer m_RamLookTimer; //!< Since it last looked for an enemy to ram with its guns gone.
		bool m_HadGuns; //!< Whether it has had a gun on it: one that never had any doesn't take to ramming by itself.
		float m_RamSpeedScale; //!< How much faster than its top speed it charges (RamSpeedScale).
		float m_RamDamage; //!< How much of the enemy's full health a hit at full charge takes off (RamDamage); less the slower it struck.
		float m_RamSelfDamage; //!< The share of the damage a hit does that the ship takes itself (RamSelfDamage), at most a twelfth of its own full health.

		/// Flies the ram, each AI update while it has one: the run-up, the charge and pulling away, by turns.
		/// @return Whether it is ramming (and has been steered for it).
		bool UpdateRam();

		/// Strikes whatever not of its own side the hull has reached going fast enough while it rams, each update.
		void UpdateRamHits();

		/// With all its guns shot off (and given no order to hold or a plain move), it rams the enemy it was sent after, or the nearest in reach.
		void UpdateAutoRam();

		/// Gets how many of its guns are still on it, with a weapon mounted.
		int ArmedGunCount() const;
		long m_CombatTargetID; //!< The unique ID of the enemy its firing position (m_CombatSpot) is for.
		Vector m_CombatSpot; //!< Where it fights that enemy from.
		bool m_HasCombatSpot; //!< Whether it has a firing position.
		bool m_CombatSearching; //!< Whether it has moved for want of a shot and not had one since (the next move tries further round).
		float m_CombatHealth; //!< Its health when last looked at, to tell when it is hit.
		Timer m_CombatTimer; //!< Since it last looked over the fight.
		Timer m_NoShotTimer; //!< Since one of its guns last had the enemy.
		Timer m_HitTimer; //!< Since it was last hit.
		Timer m_JinkTimer; //!< Since it last moved for being hit.

		/// Fights the enemy it was sent after, or, holding (a defend or guard post, or just holding with no order to stay put), the one its
		/// guns are on, from a firing position, four times a second: off to one side of the enemy, at a share of its gun range by the
		/// order's risk (Actor::StandingOrder::Direct: further on a cautious order, closer on a direct one), in the air its hover height up,
		/// with a clear line to it; and within its post's reach when it has one. A new position when the enemy moves out of reach of the one
		/// it has, or its guns have had no shot for two seconds (round to the other side and further up when one move didn't do it); and,
		/// hit, a jink to one near it, sooner on a cautious order and never on a full-risk one. With the enemy gone, it goes back over its
		/// post, or holds where it is.
		void UpdateCombat();

	private:
		/// Aims and fires its manned guns: from the player's aim in a gunner's seat, or the gunner's own, at the nearest enemy in sight and range.
		void UpdateGuns();

		/// Gets the nearest enemy a gun can shoot at: in range, on its side, within its swing and in sight.
		/// @param turret The gun. @return The unique ID of the enemy, or 0.
		long FindGunTarget(const Turret* turret) const;

		/// Lets in the AI units sent to it (a move order onto it) once they get here.
		void UpdateAIBoarding();

		/// Where it is in giving AI units a ride (UpdateAIRides).
		enum class RideStage {
			None, //!< Not giving one.
			Gathering, //!< Calling units over and waiting for them to get in, down by the ground.
			Flying, //!< Flying them to where they're going.
			Unloading //!< Down low there (or wherever it gave up), letting them out.
		};

		/// A unit called over to ride, on its way to get in.
		struct RideRecruit {
			long ID = 0; //!< Its unique ID.
			Timer Since; //!< Since it was called: it is let go if it hasn't got here in a while.
		};

		RideStage m_RideStage; //!< Where it is in giving a ride.
		std::vector<RideRecruit> m_RideRecruits; //!< The units called over to ride, still on their way.
		Vector m_RideGoal; //!< Where its riders are going, on the ground: everyone it calls over is going near there.
		int m_RideTeam; //!< The team of the units it is giving a ride to (an empty ship can be taken by anyone's).
		Timer m_RideStageTimer; //!< Since the stage began.
		Timer m_RideWaitTimer; //!< Since the first rider got in: the wait for the rest (SettingsMan::AIShipRideWait) runs from then.
		bool m_RideWaitStarted; //!< Whether anyone has got in yet for this ride.
		Timer m_RideScanTimer; //!< Since it last looked round for units that want a ride.
		bool m_RideSent; //!< Whether it has been sent where this stage takes it (down to the ground, or there).
		int m_RideSends; //!< How many times it has been sent there this stage (it stopped short, and was sent again).
		float m_RideHealth; //!< Its health when the ride began, to tell it has been hit and should go.
		Vector m_RideUnloadAt; //!< Where it comes down to let its riders out.

		/// Gives AI units a ride where they can't walk, or have a long way to walk (SettingsMan::AIRidesShips): looks round now and then, while
		/// idle, for units of its side within reach that want one and can walk to it, all going near the same place it can fly to, and calls
		/// them over; comes down to the ground for them; once the first is in, waits for the rest (or for its seats to fill, the wait to run
		/// out, or a hit), the first in flying it if it needs a pilot; flies them there by the air route, comes down low and lets them out to
		/// carry on. With its pilot shot, a passenger takes the controls; with nobody to fly it, it sets down and they get out.
		void UpdateAIRides();

		/// Gets how many of its seats hold a unit riding (that got in for a ride).
		int RiderCount() const;

		/// Lets every rider out (the last seat first, the pilot last), each to carry on to where it was going.
		void LetRidersOut();

		/// Lets the units still on their way to it go, to walk on (or look for another ride).
		void ReleaseRideRecruits();

		/// Moves a rider into the empty pilot's seat, so a ship that needs a pilot flies on. @return Whether someone moved.
		bool RiderTakesControls();

		/// Starts a stage of the ride.
		void SetRideStage(RideStage stage);

		/// Moves the units in its hold into its empty seats (SeatsCargo): a ship bought with its crew comes in crewed.
		void SeatCargo();

		/// Clears all the member variables of this ACShip, effectively resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		ACShip(const ACShip& reference) = delete;
		ACShip& operator=(const ACShip& rhs) = delete;
	};
} // namespace RTE
