#pragma once

/// Header file for the Actor class.
/// @author Daniel Tabar
/// data@datarealms.com
/// http://www.datarealms.com
/// Inclusions of header files
#include "MOSRotating.h"
#include "PathFinder.h"
#include "SettingsMan.h"

#include <array>

namespace RTE {

#pragma region Global Macro Definitions
#define DefaultPieMenuNameGetter(DEFAULTPIEMENUNAME) \
	std::string GetDefaultPieMenuName() const override { return DEFAULTPIEMENUNAME; }
#pragma endregion

	class AtomGroup;
	class HeldDevice;
	class Actor;
	struct PathRequest;
	enum class PieSliceType : int;

	/// An enemy an actor's scan saw (see Actor::ScanForEnemies): who, where the look landed on it, and how plainly.
	struct ActorSighting {
		Actor* Target = nullptr; //!< The enemy seen.
		Vector HitPos; //!< Where the look landed on it (its body, or its head when the body was hidden).
		float Visibility = 0.0F; //!< How plainly it was seen, 0 to 1: off the aim, far, dark, still and lying down all make it less.
		float Distance = 0.0F; //!< From the eyes, in pixels.
		bool Head = false; //!< Only its head was seen.
	};

#define AILINEDOTSPACING 16

	/// A sprite movable object that is autonomous.
	class Actor : public MOSRotating {
		friend struct EntityLuaBindings;

		/// Public member variable, method and friend function declarations
	public:
		enum Status {
			STABLE = 0,
			UNSTABLE,
			INACTIVE,
			DYING,
			DEAD,
			StatusCount
		};

		// TODO - move into ALocomotable intermediate class under ACrab/AHuman
		enum MovementState {
			NOMOVE = 0,
			CROUCH,
			STAND,
			WALK,
			RUN,
			JUMP,
			DISLODGE,
			PRONE,
			CRAWL,
			ARMCRAWL,
			CLIMB,
			WALKCROUCH, //!< Not a state of its own (a crouched walker is in WALK): the slot for the crouched walk's leg paths, which a walker more than half crouched strides on.
			MOVEMENTSTATECOUNT
		};

		enum AIMode {
			AIMODE_NONE = 0,
			AIMODE_SENTRY,
			AIMODE_PATROL,
			AIMODE_GOTO,
			AIMODE_BRAINHUNT,
			AIMODE_GOLDDIG,
			AIMODE_RETURN,
			AIMODE_STAY,
			AIMODE_SCUTTLE,
			AIMODE_DELIVER,
			AIMODE_BOMB,
			AIMODE_SQUAD,
			AIMODE_COUNT
		};

		enum ActorHotkeyType {
			PRIMARYHOTKEY = 0,
			AUXILIARYHOTKEY,
			ACTORHOTKEYTYPECOUNT
		};

		// Concrete allocation and cloning definitions
		EntityAllocation(Actor);
		AddScriptFunctionNames(MOSRotating, "ThreadedUpdateAI", "UpdateAI", "OnControllerInputModeChange");
		SerializableOverrideMethods;
		ClassInfoGetters;

		/// Constructor method used to instantiate a Actor object in system
		/// memory. Create() should be called before using the object.
		Actor();

		/// Destructor method used to clean up a Actor object before deletion
		/// from system memory.
		~Actor() override;

		/// Makes the Actor object ready for use.
		/// @return An error return value signaling sucess or any particular failure.
		/// Anything below 0 is an error signal.
		int Create() override;

		/// Creates a Actor to be identical to another, by deep copy.
		/// @param reference A reference to the Actor to deep copy.
		/// @return An error return value signaling sucess or any particular failure.
		/// Anything below 0 is an error signal.
		int Create(const Actor& reference);

		/// Resets the entire Actor, including its inherited members, to their
		/// default settings or values.
		void Reset() override {
			Clear();
			MOSRotating::Reset();
			m_MOType = MovableObject::TypeActor;
		}

		/// Cleans up and destroys the script state of this object, calling the Destroy callback in lua
		void DestroyScriptState();

		/// Destroys and resets (through Clear()) the SceneLayer object.
		/// @param notInherited Whether to only destroy the members defined in this derived class, or (default: false)
		/// to destroy all inherited members also.
		void Destroy(bool notInherited = false) override;

		/// Gets the mass of this Actor's inventory. Does not include any equipped item (for actor subtypes that have that).
		/// @return The mass of this Actor's inventory.
		float GetInventoryMass() const;

		/// Gets the mass of this Actor, including the mass of its Attachables, wounds and inventory.
		/// @return The mass of this Actor, its inventory and all its Attachables and wounds in Kilograms (kg).
		float GetMass() const override;

		/// Gets the mass that this actor had upon spawning, i.e with ini-defined inventory, gold and holding no items
		/// @return The base mass of this Actor, in Kilograms (kg).
		float GetBaseMass();

		/// Gets this Actor's Controller. Ownership IS NOT transferred!
		/// @return A const pointer to this Actor's Controller.
		Controller* GetController() { return &m_Controller; }

		/// Tells whether a player is currently controlling this.
		/// @return Whether a player is controlling this.
		bool IsPlayerControlled() const;

		/// Tells wheter the player can switch control to this at all
		/// @return Whether a player can control this at all.
		virtual bool IsControllable() const { return true; }

		/// Gets whether or not this Actor can be controlled by human players. Note that this does not protect the Actor's Controller from having its input mode forced to CIM_PLAYER (e.g. via Lua).
		/// @return Whether or not this Actor can be controlled by human players.
		bool IsPlayerControllable() const { return m_PlayerControllable; }

		/// Gets whether units run by the AI take no notice of this Actor: they don't see it as an enemy and don't pick it as a target.
		bool IsIgnoredByAI() const { return m_IgnoredByAI; }

		/// Sets whether units run by the AI take no notice of this Actor. For a player walking about a battle as a bystander.
		void SetIgnoredByAI(bool ignored) { m_IgnoredByAI = ignored; }

		/// Sets whether or not this Actor can be controlled by human players.
		/// @param playerControllable Whether or not this Actor should be able to be controlled by human players.
		void SetPlayerControllable(bool playerControllable) { m_PlayerControllable = playerControllable; }

		/// Returns the current Status of this.
		/// @return The status.
		int GetStatus() const { return m_Status; }

		/// Gets this Actor's health value.
		/// @return A float describing this Actor's health.
		float GetHealth() const { return m_Health; }

		/// Gets this Actor's previous health value, prior to this frame.
		/// @return A float describing this Actor's previous health.
		float GetPrevHealth() const { return m_PrevHealth; }

		/// Gets this Actor's maximum health value.
		/// @return A float describing this Actor's max health.
		float GetMaxHealth() const { return m_MaxHealth; }

		/// Gets this Actor's maximum health value.
		/// @param newValue New max health value.
		void SetMaxHealth(int newValue) { m_MaxHealth = newValue; }

		/// Gets the distance between the actor and the view point when not
		/// sharp aiming.
		/// @return A const int describing how far this actor aims/looks by default.
		int GetAimDistance() const { return m_AimDistance; }

		/// Gets the distance between the actor and the view point when not
		/// sharp aiming.
		/// @return A const int describing how far this actor aims/looks by default.
		void SetAimDistance(int newValue) { m_AimDistance = newValue; }

		/// Gets how many ounces of gold this Actor is carrying.
		/// @return The current amount of carried gold, in Oz.
		float GetGoldCarried() const { return m_GoldCarried; }

		/// Gets the total liquidation value of this Actor and all its carried
		/// gold and inventory.
		/// @param nativeModule If this is supposed to be adjusted for a specific Tech's subjective (default: 0)
		/// value, then pass in the native DataModule ID of that tech. 0 means
		/// no Tech is specified and the base value is returned.
		/// @param foreignMult How much to multiply the value if this happens to be a foreign Tech. (default: 1.0)
		/// @return The current value of this Actor and all his carried assets.
		float GetTotalValue(int nativeModule = 0, float foreignMult = 1.0, float nativeMult = 1.0) const override;

		/// Shows whether this is or carries a specifically named object in its
		/// inventory. Also looks through the inventories of potential passengers,
		/// as applicable.
		/// @param objectName The Preset name of the object to look for.
		/// @return Whetehr the object was found carried by this.
		bool HasObject(std::string objectName) const override;

		/// Shows whether this is or carries a specifically grouped object in its
		/// inventory. Also looks through the inventories of potential passengers,
		/// as applicable.
		/// @param groupName The name of the group to look for.
		/// @return Whetehr the object in the group was found carried by this.
		bool HasObjectInGroup(std::string groupName) const override;

		/// Gets this Actor's aim angle.
		/// @param adjustForFlipped Whether to adjust the angle for flipping or not. (default: true)
		/// @return The angle, in radians.
		float GetAimAngle(bool adjustForFlipped = true) const { return adjustForFlipped ? FacingAngle(m_AimAngle) : m_AimAngle; }

		/// Gets this Actor's passenger slots.
		/// @return The Actor's passenger plots
		int GetPassengerSlots() const { return m_PassengerSlots; }

		/// Gets the absoltue position of this' brain, or equivalent.
		/// @return A Vector with the absolute position of this' brain.
		virtual Vector GetCPUPos() const { return m_Pos; }

		/// Gets the absoltue position of this' eye, or equivalent, where look
		/// vector starts from.
		/// @return A Vector with the absolute position of this' eye or view point.
		virtual Vector GetEyePos() const { return m_Pos; }

		/// Gets the absoltue position of the top of this' HUD stack.
		/// @return A Vector with the absolute position of this' HUD stack top point.
		Vector GetAboveHUDPos() const override { return m_Pos + Vector(0, m_HUDStack + 6); }

		/// Gets the offset position of the holster where this Actor draws his devices from.
		/// @return The offset position of the holster.
		Vector GetHolsterOffset() const { return m_HolsterOffset; }

		/// Sets the offset position of the holster where this Actor draws his devices from.
		/// @param newOffset A new holster offset.
		void SetHolsterOffset(Vector newOffset) { m_HolsterOffset = newOffset; }

		/// Gets the offset position of where this Actor reloads his devices from.
		/// @return The offset position of the where this Actor reloads his devices from.
		Vector GetReloadOffset() const { return m_ReloadOffset; }

		/// Sets the offset position of the where this Actor reloads his devices from.
		/// @param newOffset The new offset position of where this Actor reloads his devices from.
		void SetReloadOffset(Vector newOffset) { m_ReloadOffset = newOffset; }

		/// Gets the point at which this actor is viewing, or that the scene frame
		/// should be centered on if tracking this Actor's view. In absolute scene
		/// coordinates.
		/// @return The point in absolute scene coordinates.
		Vector GetViewPoint() const { return m_ViewPoint.IsZero() ? m_Pos : m_ViewPoint; }

		/// Gets the item that is within reach of the Actor at this frame, ready to be be picked up. Ownership is NOT transferred!
		/// @return A pointer to the item that has been determined to be within reach of this Actor, if any.
		HeldDevice* GetItemInReach() const { return m_pItemInReach; }

		/// Gets the direction where this is looking/aiming.
		/// @return A Vector with the direction in which this is looking along.
		Vector GetLookVector() const { return m_ViewPoint - GetEyePos(); }

		/// Gets the normalized amount of sharp aim that has been achieved by this.
		/// @return Sharp aim progress between 0 - 1.0. 1.0 is fully aimed.
		float GetSharpAimProgress() const { return m_SharpAimProgress; }

		/// Gets the approximate height of this Actor, standing up.
		/// @return A float with the approximate height, in pixels.
		float GetHeight() const { return m_CharHeight; }

		/// Sets this Actor's new Controller input mode.
		/// @param newMode The new input mode.
		/// @param newPlayer The player which will control this if the input mode was set to player. (default: -1)
		void SetControllerMode(Controller::InputMode newMode, int newPlayer = -1);

		/// Forgets the engine route-follower's state, for the actor types that have one (AHuman, ACrab). Called on a change of controller,
		/// so a player who takes over mid-flight and hands back later doesn't leave a stale flight to be judged failed.
		virtual void ResetRouteMovement() {}

		/// Sets this Actor's Controller mode and gives back what it used to be.
		/// @param newMode The new mode to set to.
		/// @param newPlayer The player which will control this if the input mode was set to player. (default: -1)
		/// @return The old mode that it had before.
		Controller::InputMode SwapControllerModes(Controller::InputMode newMode, int newPlayer = -1);

		/// Sets this Actor's status.
		/// @param newStatus A Status enumeration.
		void SetStatus(Actor::Status newStatus) {
			m_Status = newStatus;
			if (newStatus == Actor::Status::UNSTABLE) {
				m_StableRecoverTimer.Reset();
			}
		}

		/// Gets this Actor's MovementState.
		/// @return This Actor's MovementState.
		MovementState GetMovementState() const { return m_MovementState; }

		/// Sets this Actor's MovementState to the new state.
		/// @param newMovementState This Actor's new MovementState.
		void SetMovementState(MovementState newMovementState) { m_MovementState = newMovementState; }

		/// Sets which team this Actor belongs to.
		/// @param team The assigned team number.
		void SetTeam(int team) override;

		/// Sets how many ounces of gold this Actor is carrying.
		/// @param goldOz The new amount of carried gold, in Oz.
		void SetGoldCarried(float goldOz) { m_GoldCarried = goldOz; }

		/// Sets this Actor's aim angle.
		/// @param newAngle A new angle, in radians.
		void SetAimAngle(float newAngle) {
			m_AimAngle = newAngle;
			Clamp(m_AimAngle, m_AimRange, -m_AimRange);
		}

		/// Sets this Actor's passenger slots.
		/// @param newPassengerSlots A new amount of passenger slots.
		void SetPassengerSlots(int newPassengerSlots) { m_PassengerSlots = newPassengerSlots; }

		/// Sets the point at which this actor is viewing, or that the scene frame
		/// should be centered on if tracking this Actor's view. In absolute scene
		/// coordinates.
		/// @param newPoint A new point in absolute scene coords.
		void SetViewPoint(Vector newPoint) { m_ViewPoint = newPoint; }

		/// Sets the item that is within reach of the Actor at this frame, so that
		/// it may be picked up. Ownership is NOT transferred!
		/// @param pItem A pointer to the item that has been determined to be within reach of
		/// this Actor. Ownership is NOT transferred!
		void SetItemInReach(HeldDevice* pItem) { m_pItemInReach = pItem; }

		/// Tells whether a point on the scene is within range of the currently
		/// used device and aiming status, if applicable.
		/// @param point A Vector witht he aboslute coordinates of a point to check.
		/// @return Whether the point is within range of this.
		virtual bool IsWithinRange(Vector& point) const { return false; }

		/// Casts an unseen-revealing ray in the direction of where this is facing.
		/// @param FOVSpread The degree angle to deviate from the current view point in the ray
		/// casting. A random ray will be chosen out of this +-range.
		/// @param range The range, in pixels, that the ray will have.
		/// @return Whether any unseen pixels were revealed by this look.
		virtual bool Look(float FOVSpread, float range);

		/// Adds a certain amount of ounces of gold to this' team's total funds.
		/// @param goldOz The amount in Oz with which to change this' team's gold tally.
		void AddGold(float goldOz);

		/// Does the calculations necessary to detect whether this Actor is at rest or not. IsAtRest() retrieves the answer.
		void RestDetection() override;

		/// Adds health points to this Actor's current health value.
		/// @param setHealth A float specifying the value to add.
		/// @return The resulting total health of this Actor.
		const float AddHealth(const float addedHealth) { return m_Health += addedHealth; }

		/// Sets this Actor's current health value.
		/// @param setHealth A float specifying the value to set to.
		void SetHealth(const float setHealth) { m_Health = setHealth; }

		/// Checks if this Actor is in a specific status.
		/// @param which Which status to check for.
		/// @return A bool with the answer.
		bool IsStatus(Status which) const { return m_Status == which; }

		/// Checks if this Actor is dead.
		/// @return A const bool with the answer.
		bool IsDead() const { return m_Status == DEAD; }

		/// Tries to handle the activated PieSlice in this object's PieMenu, if there is one, based on its SliceType.
		/// @param pieSliceType The SliceType of the PieSlice being handled.
		/// @return Whether or not the activated PieSlice SliceType was able to be handled.
		virtual bool HandlePieCommand(PieSliceType pieSliceType) { return false; }

		/// Gets this' AI mode.
		/// @return The current AI mode.
		int GetAIMode() const { return m_AIMode; }

		/// Gets the order serial: a count bumped by every order given to this, a change of AI mode, a waypoint added or the waypoints
		/// cleared. The AI compares it with the count it saw after its own last update, so an order given in between, even one to the
		/// mode it is already in, is seen as new without dropping the unit out of its mode for an update first.
		/// @return The order serial of this.
		unsigned int GetAIOrderSerial() const { return m_AIOrderSerial; }

		/// Gets the icon bitmap associated with this' current AI mode and team.
		/// @return The current AI mode icon of this. Ownership is NOT transferred!
		BITMAP* GetAIModeIcon();

		/// Sets this' AI mode.
		/// @param newMode The new AI mode. (default: AIMODE_SENTRY)
		void SetAIMode(AIMode newMode = AIMODE_SENTRY) {
			// (Only a change counts: scripts that set the mode they want every update would otherwise restart the AI each time.)
			if (newMode != m_AIMode) {
				++m_AIOrderSerial;
			}
			m_AIMode = newMode;
		}

		/// Adds an absolute scene point to the list of waypoints this is going to
		/// go to, in order
		/// @param m_Waypoints.push_back(std::pair<Vector The new scene point this should try to get to after all other waypoints
		/// are reached.
		void AddAISceneWaypoint(const Vector& waypoint) {
			m_Waypoints.push_back(std::pair<Vector, MovableObject*>(waypoint, (MovableObject*)NULL));
			m_WaitingAtDoor = false; // (A new order is asked for at once, not after the wait at the last one's door.)
			++m_AIOrderSerial;
		}

		/// Adds an MO in the scene as the next waypoint for this to go to, in order
		/// @param pMOWaypoint The new MO this should try to get to after all other waypoints are reached.
		/// OWNERSHIP IS NOT TRANSFERRED!
		void AddAIMOWaypoint(const MovableObject* pMOWaypoint);

		/// Removes all AI waypoints and clears the current path to the current
		/// waypoint. The AI Actor will stop in its tracks.
		void ClearAIWaypoints() {
			m_pMOMoveTarget = 0;
			m_Waypoints.clear();
			m_MovePath.clear();
			m_MovePathKinds.clear();
			m_MoveTarget = m_Pos;
			m_MoveVector.Reset();
			m_HasMovePathGoal = false;
			m_WaitingAtDoor = false;
			++m_AIOrderSerial;
		}

		/// Gets the last or furthest set AI waypoint of this: the last of its waypoints, else the place its route was asked for to, else
		/// the route's last point. If none, this' pos is returned. (The route's last point is not the place when the route was cut short
		/// at an obstacle: taken for it, a unit that walked to the cut had "arrived" and stood there for good.)
		/// @return The furthest set AI waypoint of this.
		Vector GetLastAIWaypoint() const {
			if (!m_Waypoints.empty()) {
				return m_Waypoints.back().first;
			} else if (m_HasMovePathGoal) {
				return m_MovePathGoal;
			} else if (!m_MovePath.empty()) {
				return m_MovePath.back();
			}
			return m_Pos;
		}

		/// Gets the ID of the last set AI MO waypoint of this. If none, g_NoMOID is returned.
		/// @return The furthest set AI MO waypoint of this.
		MOID GetAIMOWaypointID() const;

		/// Gets the list of waypoints for this Actor.
		/// @return The list of waypoints for this Actor.
		const std::list<std::pair<Vector, const MovableObject*>>& GetWaypointList() const { return m_Waypoints; }

		/// Gets how many waypoints this actor have.
		/// @return How many waypoints.
		int GetWaypointsSize() { return m_Waypoints.size(); };

		/// Clears the list of coordinates in this' current MovePath, ie the path
		/// to the next Waypoint.
		void ClearMovePath() {
			m_MovePath.clear();
			m_MovePathKinds.clear();
			m_MoveTarget = m_Pos;
			m_MoveVector.Reset();
			m_HasMovePathGoal = false; // The path and the place it was for go together: a route a sentry asked for to face along must not become a later order's goal.
			m_WaitingAtDoor = false;
		}

		/// Adds a coordinate to the beginning of the MovePath, meaning the one
		/// closest to this Actor.
		/// @param newCoordinate The new coordinate to add to the front of the MovePath.
		void AddToMovePathBeginning(Vector newCoordinate) {
			m_MovePath.push_front(newCoordinate);
			m_MovePathKinds.push_front(PathStepKind::Walk);
			m_MoveTarget = newCoordinate;
			m_MoveVector.Reset();
		}

		/// Adds a coordinate to the end of the MovePath, meaning the one
		/// closest to this Actor's next waypoint.
		/// @param m_MovePath.push_back(newCoordinate The new coordinate to add to the end of the MovePath.
		void AddToMovePathEnd(Vector newCoordinate) {
			m_MovePath.push_back(newCoordinate);
			m_MovePathKinds.push_back(PathStepKind::Walk);
		}

		/// What the step to the first point of the MovePath is (see PathStepKind), or -1 with no path.
		int GetMovePathStepKind() const { return m_MovePathKinds.empty() ? -1 : static_cast<int>(m_MovePathKinds.front()); }

		/// What the step to the second point of the MovePath is, or -1 when there is no second point.
		int GetMovePathNextStepKind() const {
			if (m_MovePathKinds.size() < 2) {
				return -1;
			}
			return static_cast<int>(*std::next(m_MovePathKinds.begin()));
		}

		/// The searcher this actor is to the path grid: what it can jump, dig and breach, and how big it is.
		virtual PathAgent GetPathAgent() const;

		/// Gets the last position in this Actor's move path, or otherwise the current move target.
		/// @return The last position in this Actor's move path, or otherwise the current move target.
		Vector GetMovePathEnd() const {
			// The route's end is the place it was asked for to, not where an obstacle cut it.
			if (m_HasMovePathGoal) {
				return m_MovePathGoal;
			}
			if (!m_MovePath.empty()) {
				return m_MovePath.back();
			}
			// In case move path is empty, check our own path request.
			if (m_PathRequest) {
				return const_cast<Vector&>(m_PathRequest->targetPos);
			}
			// In case *that* is empty, just return the move target.
			return m_MoveTarget;
		}

		/// Removes a coordinate from the beginning of the MovePath, meaning the
		/// one closest to this Actor.
		/// @return Whether there was any coordinate to remove. If false, the MovePath
		/// is empty.
		bool RemoveMovePathBeginning() {
			if (!m_MovePath.empty()) {
				m_MovePath.pop_front();
				if (!m_MovePathKinds.empty()) {
					m_MovePathKinds.pop_front();
				}
				m_MoveTarget = m_MovePath.empty() ? m_Pos : m_MovePath.front();
				m_MoveVector.Reset();
				return true;
			}
			return false;
		}

		/// Removes a coordinate from the end of the MovePath, meaning the
		/// one farthest from this Actor.
		/// @return Whether there was any coordinate to remove. If false, the MovePath
		/// is empty.
		bool RemoveMovePathEnd() {
			if (!m_MovePath.empty()) {
				m_MovePath.pop_back();
				if (!m_MovePathKinds.empty()) {
					m_MovePathKinds.pop_back();
				}
				return true;
			}
			return false;
		}

		/// Gets a pointer to the MovableObject move target of this Actor.
		/// @return A pointer to the MovableObject move target of this Actor.
		const MovableObject* GetMOMoveTarget() const { return m_pMOMoveTarget; }

		/// Sets this' perceptiveness to alarming events going on around him.
		/// @param newPerceptiveness The current perceptiveness, 0.0 - 1.0
		void SetPerceptiveness(float newPerceptiveness) { m_Perceptiveness = newPerceptiveness; }

		/// Gets this' perceptiveness to alarming events going on around him.
		/// @return The current perceptiveness, 0.0 - 1.0
		float GetPerceptiveness() const { return m_Perceptiveness; }

		/// How bright this unit's own headlamp is next to others': 1 is the usual, 0 means it has none (a drone with no lamp, a creature).
		float GetHeadlampBrightness() const { return m_HeadlampBrightness; }
		void SetHeadlampBrightness(float brightness) { m_HeadlampBrightness = std::max(brightness, 0.0F); }

		/// Gives this unit's headlamp a color of its own, 0-255, in place of the player's setting.
		void SetHeadlampColor(int red, int green, int blue) {
			m_HeadlampColor.SetRGB(red, green, blue);
			m_HeadlampHasColor = true;
		}
		/// Goes back to the headlamp color of the player's settings.
		void ClearHeadlampColor() { m_HeadlampHasColor = false; }

		/// Gets whether this actor is able to reveal unseen areas by looking.
		/// @return Whether this actor can reveal unseen areas.
		bool GetCanRevealUnseen() const { return m_CanRevealUnseen; }

		/// Sets whether this actor can reveal unseen areas by looking.
		/// @param newCanRevealUnseen Whether this actor can reveal unseen areas.
		void SetCanRevealUnseen(bool newCanRevealUnseen) { m_CanRevealUnseen = newCanRevealUnseen; }

		/// Sets this' PainThreshold value above which it will play PainSound
		/// @param newPainThreshold Desired PainThreshold value
		void SetPainThreshold(float newPainThreshold) { m_PainThreshold = newPainThreshold; }

		/// Gets this' PainThreshold value above which it will play PainSound
		/// @return The current PainThreshold
		float GetPainThreshold() const { return m_PainThreshold; }

		/// Makes this alarmed about a certian point on in the scene, overriding
		/// the current AI mode until a certain time has passed.
		/// @param alarmPoint The new scene point this should look at and see if anything dangerous
		/// is there.
		void AlarmPoint(const Vector& alarmPoint);

		/// Gets any point on the scene this actor should be alarmed about this frame.
		/// @return The new scene point this should look at and see if anything dangerous
		/// is there or (0,0) if nothing is alarming.
		/// Gets the last point this actor was alarmed about: when it was hit, a point back along where the shot came from.
		const Vector& GetLastAlarmPos() const { return m_LastAlarmPos; }

		Vector GetAlarmPoint() {
			if (m_AlarmTimer.GetElapsedSimTimeMS() > g_TimerMan.GetDeltaTimeMS()) {
				return Vector();
			}
			return m_LastAlarmPos;
		}

		/// Adds an inventory item to this Actor.
		/// @param AddToInventoryBack(pItemToAdd An pointer to the new item to add. Ownership IS TRANSFERRED!
		/// @return None..
		virtual void AddInventoryItem(MovableObject* pItemToAdd) { AddToInventoryBack(pItemToAdd); }

		/// Removes a specified item from the actor's inventory. Only one item is removed at a time.
		/// @param RemoveInventoryItem("" Preset name of an item to remove.
		void RemoveInventoryItem(const std::string& presetName) { RemoveInventoryItem("", presetName); }

		/// Removes the first inventory item with the given module name and preset name.
		/// @param moduleName The module name of the item to remove.
		/// @param presetName The preset name of the item to remove.
		void RemoveInventoryItem(const std::string& moduleName, const std::string& presetName);

		/// Removes and returns the inventory item at the given index. Ownership IS transferred.
		/// @param inventoryIndex The index of the inventory item to remove.
		/// @return An owning pointer to the removed inventory item.
		MovableObject* RemoveInventoryItemAtIndex(int inventoryIndex);

		/// Swaps the next MovableObject carried by this Actor and puts one not
		/// currently carried into the into the back of the inventory of this.
		/// @param pSwapIn A pointer to the external MovableObject to trade in. Ownership IS xferred! (default: nullptr)
		/// @param muteSound If 0 is passed in, nothing will be added to the inventory. (default: false)
		/// Whether to mute the sound on this event. Override for the loading screen hack.
		/// @return The next MovableObject in this Actor's inventory. Ownership IS xferred!
		/// If there are no MovableObject:s in inventory, 0 will be returned.
		virtual MovableObject* SwapNextInventory(MovableObject* pSwapIn = nullptr, bool muteSound = false);

		/// Swaps the prev MovableObject carried by this Actor and puts one not
		/// currently carried into the into the back of the inventory of this.
		/// @param pSwapIn A pointer to the external MovableObject to trade in. Ownership IS xferred! (default: nullptr)
		/// If 0 is passed in, nothing will be added to the inventory.
		/// @return The prev MovableObject in this Actor's inventory. Ownership IS xferred!
		/// If there are no MovableObject:s in inventory, 0 will be returned.
		virtual MovableObject* SwapPrevInventory(MovableObject* pSwapIn = nullptr);

		/// Swaps the inventory items at the given indices. Will return false if a given index is invalid.
		/// @param inventoryIndex1 The index of one item.
		/// @param inventoryIndex2 The index of the other item.
		/// @return Whether or not the swap was successful.
		bool SwapInventoryItemsByIndex(int inventoryIndex1, int inventoryIndex2);

		/// Sets the inventory item at the given index as the new inventory item, and gives back the one that was originally there.
		/// If an invalid index is given, the new item will be put in the back of the inventory, and nullptr will be returned.
		/// @param newInventoryItem The new item that should be at the given inventory index. Cannot be a nullptr. Ownership IS transferred.
		/// @param inventoryIndex The inventory index the new item should be placed at.
		/// @return The inventory item that used to be at the inventory index. Ownership IS transferred.
		MovableObject* SetInventoryItemAtIndex(MovableObject* newInventoryItem, int inventoryIndex);

		/// Ejects all inventory items that this is carrying. It may not happen
		/// instantaneously, so check for ejection being complete with
		/// IsInventoryEmpty().
		virtual void DropAllInventory();

		/// Converts all of the Gold carried by this Actor into MovableObjects and ejects them into the Scene.
		virtual void DropAllGold();

		/// Tells how many things are in the invetory
		/// @return The number of things in the inventory
		int GetInventorySize() const { return m_Inventory.size(); }

		/// Tells whether inventory is completely empty
		/// @return Whether inventory is completely empty.
		bool IsInventoryEmpty() { return m_Inventory.empty(); }

		/// Gets the deque of inventory of this. Ownership is NOT transferred.
		/// @return A const pointer to the inventory deque of this. OWNERSHIP IS NOT TRANSFERRED!
		const std::deque<MovableObject*>* GetInventory() const { return &m_Inventory; }

		/// Returns the maximum total mass this Actor can carry in its inventory.
		/// @return The maximum carriable mass of this Actor.
		float GetMaxInventoryMass() const { return m_MaxInventoryMass; }

		/// Attempts to add an item to the front of our inventory.
		/// @return Whether we succeeded in adding the item. We may fail if the object doesn't exist or is set to delete.
		bool AddToInventoryFront(MovableObject* itemToAdd);

		/// Attempts to add an item to the back of our inventory.
		/// @return Whether we succeeded in adding the item. We may fail if the object doesn't exist or is set to delete.
		bool AddToInventoryBack(MovableObject* itemToAdd);

		/// The limit of this actors aiming angle, in each direction, in radians.
		/// @return The arc range of the aiming angle in radians.
		/// Eg if HalfPI, it means full 180 degree range
		float GetAimRange() const { return m_AimRange; }

		/// Sets the limit of this actors aiming angle, in each direction, in radians.
		/// @param range The arc range of the aiming angle in radians.
		/// Eg if HalfPI, it means full 180 degree range
		void SetAimRange(float range) { m_AimRange = range; }

		/// Makes this draw its current waypoints and related data on the scene in
		/// its HUD drawing stage.
		/// @param drawWaypoints Whether to enable or disable the drawing of the waypoints. (default: true)
		void DrawWaypoints(bool drawWaypoints = true) { m_DrawWaypoints = drawWaypoints; }

		/// Gets whether the dotted paths of units moving under AI are drawn. Off, only the unit a player is controlling shows its path (a setting).
		/// How the paths of units moving under AI are drawn: 0 never (only the unit being played), 1 always, 2 for the units selected in the sandbox.
		static int ShowAIPaths() { return s_ShowAIPaths; }

		/// Sets whether the dotted paths of units moving under AI are drawn.
		static void SetShowAIPaths(int show) { s_ShowAIPaths = show; }

		/// Whether this unit is selected in the sandbox: it carries the selection arrow and, with paths shown for selected units, its path.
		bool IsSandboxSelected() const { return m_SandboxSelected; }
		void SetSandboxSelected(bool selected) { m_SandboxSelected = selected; }

		/// Whether the debug overlays and AI tracing look at this unit: it was pinned for inspection (the inspect key, or DebugInspected from Lua), it's selected in the sandbox, or a player is controlling it.
		/// This, not the AITrace number value, is what decides it now; AITrace still counts, for the gym scripts that set it.
		bool IsDebugInspected() const { return m_DebugInspected || m_SandboxSelected || NumberValueExists("AITrace") || IsPlayerControlled(); }

		/// Whether this unit was pinned for inspection, by the inspect key or from Lua (Lua's DebugInspected; IsInspected reads IsDebugInspected).
		bool IsDebugPinned() const { return m_DebugInspected; }

		/// Pins or unpins this unit for inspection by the debug overlays and AI tracing.
		void SetDebugInspected(bool inspected) { m_DebugInspected = inspected; }

		/// One name and value of a unit's debug state, for DescribeDebugState.
		struct DebugStateField {
			std::string Name;
			std::string Value;
			bool Text = false; //!< Whether the value is text (quoted in JSON) rather than a number or true/false.
		};

		/// Gets this unit's debug state, the unit inspector's fields: who and where it is, its AI mode, its route, and the AI values its scripts have written (those named AI... or Sandbox...). Derived units add their own (a human's route-follower).
		/// @param fields Appended to.
		virtual void GetDebugState(std::vector<DebugStateField>& fields) const;

		/// Gets this unit's debug state as one line ("name value, ..."), or as a JSON object, for the gym's stall lines, the control link's inspect command and the Copy state button.
		/// @param json Whether to give JSON rather than a line.
		std::string DescribeDebugState(bool json) const;

		/// Whether a debug overlay wants this unit's AI scripts to write their state into its values (AI_Behavior, AI_TargetID and the rest; see SharedBehaviors.ExportDebugState).
		/// Off unless an overlay that reads them is on, so the scripts write nothing in normal play.
		bool WantsDebugExport() const {
			int inspector = std::max(g_SettingsMan.UnitInspector(), g_SettingsMan.CombatOverlay());
			return inspector == 2 || (inspector == 1 && IsDebugInspected());
		}

		/// Destroys this MOSRotating and creates its specified Gibs in its place with appropriate velocities.
		/// Any Attachables are removed and also given appropriate velocities.
		/// @param impactImpulse The impulse (kg * m/s) of the impact causing the gibbing to happen.
		/// @param movableObjectToIgnore A pointer to an MO which the Gibs and Attachables should not be colliding with.
		void GibThis(const Vector& impactImpulse = Vector(), MovableObject* movableObjectToIgnore = nullptr) override;

		/// Determines whether a particle which has hit this MO will penetrate,
		/// and if so, whether it gets lodged or exits on the other side of this
		/// MO. Appropriate effects will be determined and applied ONLY IF there
		/// was penetration! If not, nothing will be affected.
		/// @param hd The HitData describing the collision in detail, the impulses have to
		/// have been filled out!
		/// @return Whether the particle managed to penetrate into this MO or not. If
		/// somehting but a MOPixel or MOSParticle is being passed in as hitor,
		/// false will trivially be returned here.
		bool ParticlePenetration(HitData& hd) override;

		/// Does stuff that needs to be done before Travel(). Always call before
		/// calling Travel.
		void PreTravel() override {
			MOSRotating::PreTravel();
			m_GoldPicked = false;
		}

		/// Sets this' AI's move path to be updated. Will update the path to the
		/// current waypoint, if any.
		void SetMovePathToUpdate() { m_UpdateMovePath = true; }

		/// Gets how many waypoints there are in the MovePath currently
		/// @return The number of waypoints in the MovePath.
		int GetMovePathSize() const { return m_MovePath.size(); }

		/// Returns whether we're waiting on a new pending movepath.
		/// @return Whether we're waiting on a new pending movepath.
		bool IsWaitingOnNewMovePath() const { return m_PathRequest != nullptr || m_UpdateMovePath; }

		/// Estimates what material strength this actor can penetrate.
		/// @return The actor's dig strength.
		virtual float EstimateDigStrength() const;

		/// Estimates what door this actor can get through: by digging, or by shooting it open with what it carries.
		/// @return The strongest door material it can breach. Default is the dig strength.
		virtual float EstimateBreachStrength() const { return EstimateDigStrength(); }

		/// Estimates how high this actor can jump. Default implementation returns FLT_MAX.
		/// @return The actor's jump height.
		virtual float EstimateJumpHeight() const;

		/// Gets the highest drop this actor lands from unhurt with no jet to brake it: the landing speed at which the impact reaches its
		/// ImpulseDamageThreshold (Update's travel damage starts there), as a height in gravity, never under a storey (96 px, what a hatch
		/// drops). @return The height, in pixels; FLT_MAX in no gravity or with no threshold.
		float GetMaxSafeFallHeight() const;

		/// Gets this Actor's base dig strength, or the strength of terrain they can expect to walk through without tools.
		/// @return The actors base dig strength.
		float GetAIBaseDigStrength() const { return m_AIBaseDigStrength; }

		/// Sets this Actor's base dig strength, or the strength of terrain they can expect to walk through without tools.
		/// @param newAIBaseDigStrength The new base dig strength for this Actor.
		void SetAIBaseDigStrength(float newAIBaseDigStrength) { m_AIBaseDigStrength = newAIBaseDigStrength; }

		/// Update called prior to controller update. Ugly hack. Supposed to be done every frame.
		virtual void PreControllerUpdate();

		/// Updates this MovableObject. Supposed to be done every frame.
		void Update() override;

		/// Cast see rays for this actor.
		void CastSeeRays();

		/// Updates the full state of this object in one call. (PreControllerUpdate(), Controller::Update(), and Update())
		virtual void FullUpdate() override;

		/// Activates one of this Actor's hotkey features.
		/// /// @param hotkeyType Which hotkey type to activate.
		void ActivateHotkeyAction(ActorHotkeyType hotkeyType) { m_HotkeyActivated[hotkeyType] = true; }

		/// Deactivates one of this Actor's hotkey features.
		/// @param hotkeyType Which hotkey type to deactivate.
		void DeactivateHotkeyAction(ActorHotkeyType hotkeyType) { m_HotkeyActivated[hotkeyType] = false; }

		/// Tells whether a hotkey action of the actor is currently being activated.
		/// @param hotkeyType Which hotkey type to check for activation.
		/// @return Whether hotkey is being activated.
		bool HotkeyActionIsActivated(ActorHotkeyType hotkeyType) const { return m_HotkeyActivated[hotkeyType]; }

		/// Description:		Sets deployment ID for this actor
		/// @param newID New deployment id.
		void SetDeploymentID(unsigned int newID) { m_DeploymentID = newID; }

		/// Description:		Gets deployment ID of this actor
		/// @return Returns deployment id of this actor.
		unsigned int GetDeploymentID() const { return m_DeploymentID; }

		/// Description:		Returns actor's sight distance.
		/// @return Returns actor's sight distance.
		float GetSightDistance() const { return m_SightDistance; }

		/// Gets how dark it is from the time of day, 0 by day to 1 at full night.
		static float GetNightAmount();

		/// Gets how far this actor can see relative to daylight: less at night, unless it has a headlamp on (night gameplay).
		float GetNightSightScale() const;

		/// Looks for enemies the way a person would, for the AI's scripts: the enemy actors within range inside a wide field of view about the
		/// facing (and a narrow, longer one about the aim when sharp-aiming), the likeliest first (nearest the aim, then nearest), looked at
		/// with at most budget rays (body, then head), through terrain, other bodies and thick smoke. Nothing behind it is seen.
		/// @param fovDegrees The field of view about the facing, in degrees.
		/// @param range How far it sees in daylight, in pixels; less at night and in dust (GetNightSightScale).
		/// @param budget How many rays it may cast.
		/// @return What was seen this call, most visible first: valid until the next call.
		std::vector<ActorSighting>& ScanForEnemies(float fovDegrees, float range, int budget);

		/// How pinned down this actor is by fire, 0 to 1: raised by shots passing close, blasts near it and hits, and wearing off over a few
		/// seconds (quicker for a better team). Machines feel none. Read by the AI's scripts (more aim error, ducking, cover).
		float GetSuppression() const { return m_Suppression; }

		/// How steady this actor's nerve is, 0 to 1: worn down by being pinned down, wounds, friends dying in sight and burning, and coming
		/// back towards a level that is higher among friends and near its brain and lower when hurt. Under 0.3 the AI pulls back.
		float GetMorale() const { return m_Morale; }

		/// Adds to how pinned down this actor is (by the AISuppression setting; nothing for a machine).
		void AddSuppression(float amount);

		/// Changes this actor's morale; a loss is scaled by the AISuppression setting, and nothing changes for a machine.
		void ChangeMorale(float change);

		/// A shot (a fast, sharp particle that hits bodies) passing along its last step: the actors it went close by and missed are pinned
		/// down a little. Called by MOPixel and MOSParticle; only so many checks a sim update.
		static void ShotPassing(const MovableObject& shot);

		/// How much of a target this actor's body makes, for the sight of others (see ScanForEnemies): 1 standing.
		virtual float GetSightProfile() const { return 1.0F; }

		/// Description:		Sets actor's sight distance.
		/// @param newValue New sight distance value.
		void SetSightDistance(float newValue) { m_SightDistance = newValue; }

		/// Draws this Actor's current graphical HUD overlay representation to a
		/// BITMAP of choice.
		/// @param pTargetBitmap A pointer to a BITMAP to draw on.
		/// @param targetPos The absolute position of the target bitmap's upper left corner in the Scene. (default: Vector())
		/// @param whichScreen Which player's screen this is being drawn to. May affect what HUD elements (default: 0)
		/// get drawn etc.
		void DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos = Vector(), int whichScreen = 0, bool playerControlled = false) override;
		void DrawHUD(const Camera& camera) override;

		/// Verifieis whether all actor's MO has correct IDs. Should be used in Debug mode only.
		void VerifyMOIDs();

		/// Returns Threshold for taking damage from travel impulses, in kg * m/s
		/// @return Threshold for taking damage from travel impulses, in kg * m/s
		float GetTravelImpulseDamage() const { return m_TravelImpulseDamage; }

		/// Sets Threshold for taking damage from travel impulses, in kg * m/s
		/// @param value Threshold for taking damage from travel impulses, in kg * m/s
		void SetTravelImpulseDamage(float value) { m_TravelImpulseDamage = value; }

		/// Gets this Actor's body hit sound. Ownership is NOT transferred!
		/// @return The SoundContainer for this Actor's body hit sound.
		SoundContainer* GetBodyHitSound() const { return m_BodyHitSound; }

		/// Sets this Actor's body hit sound. Ownership IS transferred!
		/// @param newSound The new SoundContainer for this Actor's body hit sound.
		void SetBodyHitSound(SoundContainer* newSound) { m_BodyHitSound = newSound; }

		/// Gets this Actor's alarm sound. Ownership is NOT transferred!
		/// @return The SoundContainer for this Actor's alarm sound.
		SoundContainer* GetAlarmSound() const { return m_AlarmSound; }

		/// Sets this Actor's alarm sound. Ownership IS transferred!
		/// @param newSound The new SoundContainer for this Actor's alarm sound.
		void SetAlarmSound(SoundContainer* newSound) { m_AlarmSound = newSound; }

		/// Gets this Actor's pain sound. Ownership is NOT transferred!
		/// @return The SoundContainer for this Actor's pain sound.
		SoundContainer* GetPainSound() const { return m_PainSound; }

		/// Sets this Actor's pain sound. Ownership IS transferred!
		/// @param newSound The new SoundContainer for this Actor's pain sound.
		void SetPainSound(SoundContainer* newSound) { m_PainSound = newSound; }

		/// Gets this Actor's death sound. Ownership is NOT transferred!
		/// @return The SoundContainer for this Actor's death sound.
		SoundContainer* GetDeathSound() const { return m_DeathSound; }

		/// Sets this Actor's death sound. Ownership IS transferred!
		/// @param newSound The new SoundContainer for this Actor's death sound.
		void SetDeathSound(SoundContainer* newSound) { m_DeathSound = newSound; }

		/// Gets this Actor's device switch sound. Ownership is NOT transferred!
		/// @return The SoundContainer for this Actor's device switch sound.
		SoundContainer* GetDeviceSwitchSound() const { return m_DeviceSwitchSound; }

		/// Sets this Actor's device switch sound. Ownership IS transferred!
		/// @param newSound The new SoundContainer for this Actor's device switch sound.
		void SetDeviceSwitchSound(SoundContainer* newSound) { m_DeviceSwitchSound = newSound; }

		/// Gets the X and Y thresholds for how fast the actor can travel before losing stability.
		/// @return A Vector with the X and Y thresholds for how fast the actor can travel before losing stability.
		Vector GetStableVel() const { return m_StableVel; }

		/// Sets the X and Y thresholds for how fast the actor can travel before losing stability.
		/// @param newVelX New value for how fast the actor can travel before losing stability on X axis.
		/// @param newVelY New value for how fast the actor can travel before losing stability on Y axis.
		void SetStableVel(float newVelX, float newVelY) { m_StableVel.SetXY(newVelX, newVelY); }

		/// Sets the X and Y thresholds for how fast the actor can travel before losing stability.
		/// @param newVelVector Vector with new values for how fast the actor can travel before losing stability on both axis.
		void SetStableVel(Vector newVelVector) { m_StableVel = newVelVector; }

		/// Gets the recovery delay from UNSTABLE to STABLE, in MS.
		/// @return The recovery delay, in MS.
		int GetStableRecoverDelay() const { return m_StableRecoverDelay; }

		/// Sets the recovery delay from UNSTABLE to STABLE, in MS.
		/// @param newRecoverDelay The recovery delay, in MS.
		void SetStableRecoverDelay(int newRecoverDelay) { m_StableRecoverDelay = newRecoverDelay; }

		/// Gets whether this can run or not.
		/// @return Whether this can run or not.
		int GetCanRun() const { return m_CanRun; }

		/// Sets whether this can run or not.
		/// @param newCanRun The new value for whether this can run or not.
		void SetCanRun(bool newCanRun) { m_CanRun = newCanRun; }

		/// Gets the crouch walk speed multiplier.
		/// @return The crouch walk speed multiplier.
		float GetCrouchWalkSpeedMultiplier() const { return m_CrouchWalkSpeedMultiplier; }

		/// Sets the crouch walk speed multiplier.
		/// @param newSpeed The new value for the crouch walk speed multiplier.
		void SetCrouchWalkSpeedMultiplier(float newSpeed) { m_CrouchWalkSpeedMultiplier = newSpeed; }

		/// Gets the distance in which the Actor will have considered itself to have reached it's waypoint.
		/// @return The move proximity limit.
		float GetMoveProximityLimit() const { return m_MoveProximityLimit; }

		/// Sets the distance in which the Actor will have considered itself to have reached it's waypoint.
		/// @param newProximityLimit The move proximity limit.
		void SetMoveProximityLimit(float newProximityLimit) { m_MoveProximityLimit = newProximityLimit; }

		/// Gets whether or not this Actor has the organic flag set and should be considered as organic.
		/// @return Whether or not this Actor has the organic flag set and should be considered as organic.
		bool IsOrganic() const { return m_Organic; }

		/// Gets whether or not this Actor has the mechanical flag set and should be considered as mechanical.
		/// @return Whether or not this Actor has the mechanical flag set and should be considered as mechanical.
		bool IsMechanical() const { return m_Mechanical; }

		/// Gets whether or not this Actor's limb push forces have been disabled.
		/// @return Whether or not this Actor's limb push forces have been disabled.
		bool GetLimbPushForcesAndCollisionsDisabled() const { return m_LimbPushForcesAndCollisionsDisabled; }

		/// Sets whether or not this Actor's limb push forces should be disabled.
		/// @param newLimbPushForcesAndCollisionsDisabled Whether or not this Actor's limb push forces should be disabled.
		void SetLimbPushForcesAndCollisionsDisabled(bool newLimbPushForcesAndCollisionsDisabled) { m_LimbPushForcesAndCollisionsDisabled = newLimbPushForcesAndCollisionsDisabled; }

		/// Gets the default PieMenu name for this type.
		/// @return The default PieMenu name for this type.
		virtual std::string GetDefaultPieMenuName() const { return "Default Actor Pie Menu"; }

		/// Gets a pointer to the PieMenu for this Actor. Ownership is NOT transferred.
		/// @return The PieMenu for this Actor.
		PieMenu* GetPieMenu() const { return m_PieMenu.get(); }

		/// Sets the PieMenu for this Actor. Ownership IS transferred.
		/// @param newPieMenu The new PieMenu for this Actor.
		void SetPieMenu(PieMenu* newPieMenu);

		/// Protected member variable and method declarations
	protected:
		/// Function that is called when we get a new movepath.
		/// This processes and cleans up the movepath.
		virtual void OnNewMovePath();

		/// Starts updating this Actor's movepath.
		virtual void UpdateMovePath();

		/// Asks for the route to the current goal again, from where this is now, the same check an order makes. The answer replaces the
		/// route only when it says the goal is reachable from here (solved, and not only through ground or a door that can't be got through);
		/// otherwise the route being followed is kept. For a unit in flight, whose route can fall behind it: checked often, a jet's
		/// overshoot is a new route forward from where it got to, not a turn back for a point already passed.
		void RequestRouteCheck();

		/// Whether this is pulling itself up onto a ledge or over an obstacle right now (see TryStartMantle).
		bool IsMantling() const { return m_Mantling; }

		/// Marks a place where this failed a jump: for so long, routes through it cost more for this (see PathAgent::Avoid).
		/// @param place The place, a point of the route the jump was to.
		/// @param milliseconds For how long, in sim time.
		void AvoidPathPoint(const Vector& place, float milliseconds);
		/// A flight failed: from that take-off for that landing is dearer for a while, for this unit and (for half as long) its team.
		void AvoidPathLink(const Vector& from, const Vector& to, float milliseconds);
		/// Whether this unit's AI decisions are written to the console log (LogConsole.txt, saved when the game closes): every unit when
		/// tracing all units is ticked (or CCCP_AI_LOG is "all"), else the units being inspected (IsDebugInspected: pinned, selected in the sandbox, or given the AITrace value as the gyms' runners are).
		/// Which lines go is up to the debug channels (SettingsMan::DebugChannel), ticked in the settings or switched on by CCCP_AI_LOG.
		/// @param channel The channel the line belongs to: AI for the mover and route lines, Pilot for flights, Climb, Combat, Squad.
		/// @return Whether traced.
		bool IsAITraced(SettingsMan::DebugChannel channel = SettingsMan::DebugChannel::AI) const;

		/// For Lua: whether this unit's lines on the named debug channel ("AI", "Pilot", "Climb", "Combat", "Squad") are written.
		bool IsAITracedOn(const std::string& channel) const { SettingsMan::DebugChannel which = SettingsMan::DebugChannelFromName(channel); return which != SettingsMan::DebugChannel::Count && IsAITraced(which); }

		// Member variables
		static Entity::ClassInfo m_sClass;

		enum ActionState {
			MOVING = 0,
			MOVING_FAST,
			FIRING,
			ActionStateCount
		};

		enum AimState {
			AIMSTILL = 0,
			AIMUP,
			AIMDOWN,
			AimStateCount
		};

		AtomGroup* m_pHitBody;
		Controller m_Controller;
		bool m_PlayerControllable; //!< Whether or not this Actor can be controlled by human players.
		bool m_IgnoredByAI = false; //!< Whether units run by the AI take no notice of this Actor. Not copied or saved: it is set on the one Actor while it lives.

		// Sounds
		SoundContainer* m_BodyHitSound;
		SoundContainer* m_AlarmSound;
		SoundContainer* m_PainSound;
		SoundContainer* m_DeathSound;
		SoundContainer* m_DeviceSwitchSound;

		int m_Status;
		float m_Health;
		// Maximum health
		float m_MaxHealth;
		// The health of the previous frame, so we can track damage
		float m_PrevHealth;
		// Not owned by this!
		const Icon* m_pTeamIcon;
		// Not owned by this!
		const Icon* m_pControllerIcon;
		// Timing the last second to store the position each second so we can determine larger movement
		Timer m_LastSecondTimer;
		// This' position up to a second ago
		Vector m_LastSecondPos;
		// Movement since last whole second
		Vector m_RecentMovement;
		// Threshold for taking damage from travel impulses, in kg * m/s
		float m_TravelImpulseDamage;
		// Timer for timing the delay before regaining stability after losing it
		Timer m_StableRecoverTimer;
		// Thresholds in both x and y for how fast the actor can travel before losing stability. Meters per second (m/s).
		Vector m_StableVel;
		int m_StableRecoverDelay; //!< The delay before regaining stability after losing it, in MS
		// Timer for the heartbeat of this Actor
		Timer m_HeartBeat;
		// Timer for timing how long this has been under Control
		Timer m_NewControlTmr;
		// Death timing timer
		Timer m_DeathTmr;
		// Amount of Gold carried, in ounces.
		float m_GoldCarried;
		// Whether or not any gold was picked up this frame.
		bool m_GoldPicked;
		// Whether this can engage RUN state or not
		bool m_CanRun;
		// Multiplier applied to walking speed at maximum crouch.
		float m_CrouchWalkSpeedMultiplier;
		// Aiming state
		char m_AimState;
		// The arc range of the aiming angle, in each direction, in radians. Eg if HalfPI, it means full 180 degree range
		float m_AimRange;
		// Current Aim angle within the AimRange
		float m_AimAngle;
		// How far the actor aims/looks by default
		float m_AimDistance;
		// Aiming timing timer
		Timer m_AimTmr;
		// For timing the transition from regular aim to sharp aim
		Timer m_SharpAimTimer;
		// The time it takes to achieve complete full sharp aiming
		int m_SharpAimDelay;
		// The velocity
		float m_SharpAimSpeed;
		// Normalzied scalar showing storing much sharp aim progress has been made
		float m_SharpAimProgress;
		// If sharp aim has been maxed out, ie it's either at its max, or being limited by some obstruction
		bool m_SharpAimMaxedOut;
		// Point at this target when devicestate is in POINTING mode
		Vector m_PointingTarget;
		// Last seen enemy target
		Vector m_SeenTargetPos;
		// Timer measuring how long this has been alarmed by a nearby gunshot etc.
		Timer m_AlarmTimer;
		// Position of the last thing that alarmed us
		Vector m_LastAlarmPos;
		// How far this guy's AI can see when he's just looking ahead
		float m_SightDistance;
		// How perceptive this is of alarming events going on around him, 0.0 - 1.0
		float m_Perceptiveness;
		float m_HeadlampBrightness; //!< This unit's headlamp next to the usual: 1 the same, 0 none.
		Color m_HeadlampColor; //!< This unit's own headlamp color, if it has one.
		bool m_HeadlampHasColor; //!< Whether m_HeadlampColor is used instead of the player's setting.
		/// Damage value above which this will play PainSound
		float m_PainThreshold;
		// Whether or not this actor can reveal unseen areas by looking
		bool m_CanRevealUnseen;
		// About How tall is the Actor, in pixels?
		float m_CharHeight;
		// Speed at which the m_AimAngle will change, in radians/s.
		//    float
		// The offset position of the holster where this Actor draws his devices from.
		Vector m_HolsterOffset;
		Vector m_ReloadOffset; //!< The offset position of where this Actor reloads his devices from.
		// The point at which this actor is viewing, or the scene frame
		// should be centered on if tracking this Actor's view.
		// In absolute scene coordinates.
		Vector m_ViewPoint;
		// The inventory of carried MovableObjects of this Actor. They are also Owned by this.
		std::deque<MovableObject*> m_Inventory;
		float m_MaxInventoryMass; //!< The mass limit for this Actor's inventory. -1 means there's no limit.
		// The device that can/will be picked up
		HeldDevice* m_pItemInReach;
		// An array that holds activation states for the various hotkey actions of this Actor.
		std::array<bool, ACTORHOTKEYTYPECOUNT> m_HotkeyActivated;
		// HUD positioning aid
		int m_HUDStack;
		// ID of deployment which spawned this actor
		unsigned int m_DeploymentID;
		// How many passenger slots this actor will take in a craft
		int m_PassengerSlots;
		// Most actors can walk through stuff that's soft enough, so we start with a base penetration amount
		float m_AIBaseDigStrength;
		// The mass that this actor had upon spawning, i.e with no inventory, no gold and holding no items
		float m_BaseMass;

		////////////////////
		// AI States

		enum LateralMoveState {
			LAT_STILL = 0,
			LAT_LEFT,
			LAT_RIGHT
		};

		enum ObstacleState {
			PROCEEDING = 0,
			BACKSTEPPING,
			DIGPAUSING,
			JUMPING,
			SOFTLANDING
		};

		enum TeamBlockState {
			NOTBLOCKED = 0,
			BLOCKED,
			IGNORINGBLOCK,
			FOLLOWWAIT
		};
		// Unknown team icon
		static std::vector<BITMAP*> m_apNoTeamIcon;
		// The AI mode icons
		static BITMAP* m_apAIIcons[AIMODE_COUNT];
		// Selection arrow
		static std::vector<BITMAP*> m_apSelectArrow;
		// Selection arrow
		static std::vector<BITMAP*> m_apAlarmExclamation;
		// Whether the static icons have been loaded yet or not
		static bool m_sIconsLoaded;
		// The current mode the AI is set to perform as
		AIMode m_AIMode;
		unsigned int m_AIOrderSerial; //!< Bumped by every order given to this (see GetAIOrderSerial).
		// The list of waypoints remaining between which the paths are made. If this is empty, the last path is in teh MovePath
		// The MO pointer in the pair is nonzero if the waypoint is tied to an MO in the scene, and gets updated each UpdateAI. This needs to be checked for validity/existence each UpdateAI
		std::list<std::pair<Vector, const MovableObject*>> m_Waypoints;
		// Whether to draw the waypoints or not in the HUD
		bool m_DrawWaypoints;
		static int s_ShowAIPaths; //!< How the paths of units moving under AI are drawn: 0 never, 1 always, 2 for the units selected in the sandbox.
		bool m_SandboxSelected = false; //!< Selected in the sandbox: carries the selection arrow, and its path when paths are shown for the selection.
		bool m_DebugInspected = false; //!< Pinned for inspection by the debug overlays and AI tracing (see IsDebugInspected).
		// Absolute target to move to on the scene; this is usually the point at the front of the movepath list
		Vector m_MoveTarget;
		// The MO we're currently following, if any. If still valid, this' position will update the MoveTarget each UpdateAI.
		const MovableObject* m_pMOMoveTarget;
		// The point previous on the path to the one currently assigned the move target
		Vector m_PrevPathTarget;
		Vector m_MovePathGoal; //!< The place the current route was asked for to (a scene point, not an MO), kept apart from the route's last point, which a cut-short route stops short of.
		bool m_HasMovePathGoal; //!< Whether m_MovePathGoal holds a place.
		// The relative, scene-wrapped difference between the current m_Pos and the m_MoveTarget.
		Vector m_MoveVector;
		// The calculated path to get to that move-to target
		std::list<Vector> m_MovePath;
		// What each step of that path is, kept alongside it.
		std::list<PathStepKind> m_MovePathKinds;
		std::vector<ActorSighting> m_Sightings; //!< What the last ScanForEnemies saw.
		float m_Suppression = 0.0F; //!< How pinned down by fire, 0 to 1 (see GetSuppression).
		float m_Morale = 1.0F; //!< How steady its nerve is, 0 to 1 (see GetMorale).
		float m_MoraleLevel = 0.7F; //!< What morale comes back towards: higher among friends and near the brain, lower when hurt.
		long long m_NearMissUpdate = -1; //!< The sim update of the last near miss, and how much they have pinned it down in it (capped).
		float m_NearMissThisUpdate = 0.0F;
		bool m_DeathReported = false; //!< Whether friends in sight have been shaken by its death.
		/// Suppression wearing off, morale's ups and downs, and a death's effect on the friends who saw it; once a sim update.
		void UpdateSuppressionAndMorale();
		/// Whether this actor feels fire at all: not a machine (mechanical and not organic, or metal), a door or a craft.
		bool FeelsFire() const;
		// The current pathfinding request
		std::shared_ptr<volatile PathRequest> m_PathRequest;
		// Whether it's time to update the path
		bool m_UpdateMovePath;
		// How many path answers in a row needed digging the unit can't do; at six the unit stands down.
		int m_ImpossiblePaths;
		bool m_PathImpossible; //!< Whether the last route asked for came back with no way there (or only through ground that can't be dug), for the path display.
		// Since the last of those: no new path is asked for until a few seconds have gone by.
		Timer m_PathRetryTimer;
		std::vector<std::pair<Vector, double>> m_AvoidPoints; //!< Places this failed jumps at, and the sim time (ms) each is avoided until.
		struct FailedLink {
			Vector from;
			Vector to;
			double until;
		};
		std::vector<FailedLink> m_AvoidLinks; //!< Flights this failed lately (take-off, landing), and until when (sim ms).
		// The mantle: a pull up onto a ledge, or over a low obstacle, that the legs or the jet alone would struggle with (see TryStartMantle).
		float m_PathCostAtAdoption = 0.0F; //!< The cost of the route being followed when it was taken, and how many points it had then: for weighing a route check's answer against what is left of it.
		int m_PathSizeAtAdoption = 0;
		bool m_Mantling = false;
		Vector m_MantleLip; //!< The ledge's lip the hands reach for while mantling.
		float m_MantleProgress = 0.0F; //!< How far through the mantle, 0 to 1.
		/// Gets how far the mantle has got, 0 to 1 (0 when not mantling).
		float GetMantleProgress() const { return m_Mantling ? m_MantleProgress : 0.0F; }
		/// Gets which way the mantle goes, -1 or 1 (0 when not mantling).
		float GetMantleDir() const { return m_Mantling ? m_MantleDir : 0.0F; }
		/// Gets the lip the hands reach for while mantling.
		const Vector& GetMantleLip() const { return m_MantleLip; }
		float m_MantleDir = 0.0F; //!< -1 left, 1 right.
		Vector m_MantleStart; //!< Where the body was.
		Vector m_MantleUp; //!< Where it is lifted to first.
		Vector m_MantleEnd; //!< Where it ends, over the ledge.
		float m_MantleDurationMS = 0.0F;
		Timer m_MantleTimer;

		/// Whether the body (and a head, for a body that has one) would be clear of terrain shifted by so much.
		bool BodyFitsShifted(const Vector& shift, MOSRotating* head) const;

		/// Starts a mantle if this is pressing left or right into something it can get up onto: a ledge or an obstacle whose top is no
		/// higher than 0.3 of the body (0.55 while rising on the jet), with room for the whole body on top and ground under it there.
		/// Any body works, from its own sizes; the move itself is the same for all.
		/// @param head The head, if the body has one, so it fits too.
		/// @param rising Whether the body is going up on its jet.
		/// @param bodyWidth How wide the body is, for how far over the edge it has to go.
		/// @return Whether a mantle started.
		bool TryStartMantle(MOSRotating* head, bool rising, float bodyWidth);

		/// Catches a ledge in the air (LM-6): falling past a lip, or rising beside one with the jet giving out, slowly enough to grab it, with
		/// a lip at the hands' reach on the side pressed (or asked for) and room to stand over it, the hands take it and the body is pulled
		/// up onto it by the mantle's move from where it hangs.
		/// @param head The head, if the body has one, so it fits too.
		/// @param bodyWidth How wide the body is.
		/// @param wantDir The side to catch on, -1 or 1, or 0 for the side the move keys press.
		/// @param lipNearY Only a lip within a third of the body of this height, or any lip when negative.
		/// @return Whether a catch began (as a mantle).
		bool TryCatchLedge(MOSRotating* head, float bodyWidth, float wantDir = 0.0F, float lipNearY = -1.0F);

		/// Moves a mantle on: up, then over, then done.
		void UpdateMantle();

		bool m_RouteCheck = false; //!< The path request in flight is a route check (see RequestRouteCheck): its answer is only taken if reachable.
		bool m_WaitingAtDoor = false; //!< At the end of a route cut short at a door: the route is asked for again every couple of seconds, until the door is open or gone.
		// The minimum range to consider having reached a move target is considered
		float m_MoveProximityLimit;
		// Current movement state.
		MovementState m_MovementState;

		bool m_Organic; //!< Flag for whether or not this Actor is organic. Useful for lua purposes and mod support.
		bool m_Mechanical; //!< Flag for whether or not this Actor is robotic. Useful for lua purposes and mod support.

		bool m_LimbPushForcesAndCollisionsDisabled; //<! Whether or limb push forces and collisions have been disabled (likely for Lua purposes).

		/// Private member variable and method declarations
	private:
		std::unique_ptr<PieMenu> m_PieMenu;

		/// Clears all the member variables of this Actor, effectively
		/// resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		Actor(const Actor& reference) = delete;
		Actor& operator=(const Actor& rhs) = delete;
	};

} // namespace RTE
