#pragma once

/// Header file for the AHuman class.
/// @author Daniel Tabar
/// data@datarealms.com
/// http://www.datarealms.com
/// Inclusions of header files
#include "Actor.h"
#include "Arm.h"
#include "Leg.h"
#include "LimbPath.h"

#include <array>
#include <deque>
#include <optional>
#include <shared_mutex>
#include <vector>

struct BITMAP;

namespace RTE {

	class ADoor;

	class AEJetpack;

	/// A humanoid actor.
	class AHuman : public Actor {
		friend struct EntityLuaBindings;

		enum UpperBodyState {
			WEAPON_READY = 0,
			AIMING_SHARP,
			HOLSTERING_BACK,
			HOLSTERING_BELT,
			DEHOLSTERING_BACK,
			DEHOLSTERING_BELT,
			THROWING_PREP,
			THROWING_RELEASE
		};

		enum ProneState {
			NOTPRONE = 0,
			GOPRONE,
			LAYINGPRONE,
			PRONESTATECOUNT
		};

		enum Layer {
			FGROUND = 0,
			BGROUND
		};

		/// Public member variable, method and friend function declarations
	public:
		// Concrete allocation and cloning definitions
		EntityAllocation(AHuman);
		AddScriptFunctionNames(Actor, "OnStride");
		SerializableOverrideMethods;
		ClassInfoGetters;
		DefaultPieMenuNameGetter("Default Human Pie Menu");

		/// Constructor method used to instantiate a AHuman object in system
		/// memory. Create() should be called before using the object.
		AHuman();

		/// Destructor method used to clean up a AHuman object before deletion
		/// from system memory.
		~AHuman() override;

		/// Makes the AHuman object ready for use.
		/// @return An error return value signaling sucess or any particular failure.
		/// Anything below 0 is an error signal.
		int Create() override;

		/// Creates a AHuman to be identical to another, by deep copy.
		/// @param reference A reference to the AHuman to deep copy.
		/// @return An error return value signaling sucess or any particular failure.
		/// Anything below 0 is an error signal.
		int Create(const AHuman& reference);

		/// Resets the entire AHuman, including its inherited members, to their
		/// default settings or values.
		void Reset() override {
			Clear();
			Actor::Reset();
		}

		/// Destroys and resets (through Clear()) the SceneLayer object.
		/// @param notInherited Whether to only destroy the members defined in this derived class, or (default: false)
		/// to destroy all inherited members also.
		void Destroy(bool notInherited = false) override;

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

		/// Gets the absoltue position of this' brain, or equivalent.
		/// @return A Vector with the absolute position of this' brain.
		Vector GetCPUPos() const override;

		/// Gets the absoltue position of this' eye, or equivalent, where look
		/// vector starts from.
		/// @return A Vector with the absolute position of this' eye or view point.
		Vector GetEyePos() const override;

		/// Gets the head of this AHuman.
		/// @return A pointer to the head of this AHuman. Ownership is NOT transferred.
		Attachable* GetHead() const { return m_pHead; }

		/// Sets the head for this AHuman.
		/// @param newHead The new head to use.
		void SetHead(Attachable* newHead);

		/// Gets the jetpack of this AHuman.
		/// @return A pointer to the jetpack of this AHuman. Ownership is NOT transferred.
		AEJetpack* GetJetpack() const { return m_pJetpack; }

		/// Sets the jetpack for this AHuman.
		/// @param newJetpack The new jetpack to use.
		void SetJetpack(AEJetpack* newJetpack);

		/// Gets the foreground Arm of this AHuman.
		/// @return A pointer to the foreground Arm of this AHuman. Ownership is NOT transferred.
		Arm* GetFGArm() const { return m_pFGArm; }

		/// Sets the foreground Arm for this AHuman.
		/// @param newArm The new Arm to use.
		void SetFGArm(Arm* newArm);

		/// Gets the background arm of this AHuman.
		/// @return A pointer to the background arm of this AHuman. Ownership is NOT transferred.
		Arm* GetBGArm() const { return m_pBGArm; }

		/// Sets the background Arm for this AHuman.
		/// @param newArm The new Arm to use.
		void SetBGArm(Arm* newArm);

		/// Gets the foreground Leg of this AHuman.
		/// @return A pointer to the foreground Leg of this AHuman. Ownership is NOT transferred.
		Leg* GetFGLeg() const { return m_pFGLeg; }

		/// Sets the foreground Leg for this AHuman.
		/// @param newLeg The new Leg to use.
		void SetFGLeg(Leg* newLeg);

		/// Gets the background Leg of this AHuman.
		/// @return A pointer to the background Leg of this AHuman. Ownership is NOT transferred.
		Leg* GetBGLeg() const { return m_pBGLeg; }

		/// Sets the background Leg for this AHuman.
		/// @param newLeg The new Leg to use.
		void SetBGLeg(Leg* newLeg);

		/// Gets the foot Attachable of this AHuman's foreground Leg.
		/// @return A pointer to the foot Attachable of this AHuman's foreground Leg. Ownership is NOT transferred!
		Attachable* GetFGFoot() const { return m_pFGLeg ? m_pFGLeg->GetFoot() : nullptr; }

		/// Sets the foot Attachable of this AHuman's foreground Leg.
		/// @param newFoot The new foot for this AHuman's foreground Leg to use.
		void SetFGFoot(Attachable* newFoot) {
			if (m_pFGLeg && m_pFGLeg->IsAttached()) {
				m_pFGLeg->SetFoot(newFoot);
			}
		}

		/// Gets the foot Attachable of this AHuman's background Leg.
		/// @return A pointer to the foot Attachable of this AHuman's background Leg. Ownership is NOT transferred!
		Attachable* GetBGFoot() const { return m_pBGLeg ? m_pBGLeg->GetFoot() : nullptr; }

		/// Sets the foot Attachable of this AHuman's background Leg.
		/// @param newFoot The new foot for this AHuman's background Leg to use.
		void SetBGFoot(Attachable* newFoot) {
			if (m_pBGLeg && m_pBGLeg->IsAttached()) {
				m_pBGLeg->SetFoot(newFoot);
			}
		}

		/// Gets this AHuman's UpperBodyState.
		/// @return This AHuman's UpperBodyState.
		UpperBodyState GetUpperBodyState() const { return m_ArmsState; }

		/// Sets this AHuman's UpperBodyState to the new state.
		/// @param newUpperBodyState This AHuman's new UpperBodyState.
		void SetUpperBodyState(UpperBodyState newUpperBodyState) { m_ArmsState = newUpperBodyState; }

		/// Gets this AHuman's ProneState.
		/// @return This AHuman's ProneState.
		ProneState GetProneState() const { return m_ProneState; }

		/// Sets this AHuman's ProneState to the new state.
		/// @param newProneState This AHuman's new ProneState.
		void SetProneState(ProneState newProneState) { m_ProneState = newProneState; }

		/// Tries to handle the activated PieSlice in this object's PieMenu, if there is one, based on its SliceType.
		/// @param pieSliceType The SliceType of the PieSlice being handled.
		/// @return Whether or not the activated PieSlice SliceType was able to be handled.
		bool HandlePieCommand(PieSliceType pieSliceType) override;

		/// Adds an inventory item to this AHuman. This also puts that item
		/// directly in the hands of this if they are empty.
		/// @param pItemToAdd An pointer to the new item to add. Ownership IS TRANSFERRED!
		void AddInventoryItem(MovableObject* pItemToAdd) override;

		/// Swaps the next MovableObject carried by this AHuman and puts one not currently carried into the back of the inventory of this.
		/// For safety reasons, this will dump any non-HeldDevice inventory items it finds into MovableMan, ensuring the returned item is a HeldDevice (but not casted to one, for overload purposes).
		/// @param inventoryItemToSwapIn A pointer to the external MovableObject to swap in. Ownership IS transferred.
		/// @param muteSound Whether or not to mute the sound on this event.
		/// @return The next HeldDevice in this AHuman's inventory, if there are any.
		MovableObject* SwapNextInventory(MovableObject* inventoryItemToSwapIn = nullptr, bool muteSound = false) override;

		/// Swaps the previous MovableObject carried by this AHuman and puts one not currently carried into the back of the inventory of this.
		/// For safety reasons, this will dump any non-HeldDevice inventory items it finds into MovableMan, ensuring the returned item is a HeldDevice (but not casted to one, for overload purposes).
		/// @param inventoryItemToSwapIn A pointer to the external MovableObject to swap in. Ownership IS transferred.
		/// @return The previous HeldDevice in this AHuman's inventory, if there are any.
		MovableObject* SwapPrevInventory(MovableObject* inventoryItemToSwapIn = nullptr) override;

		/// Switches the currently held device (if any) to the first found firearm
		/// in the inventory. If the held device already is a firearm, or no
		/// firearm is in inventory, nothing happens.
		/// @param doEquip Whether to actually equip any matching item found in the inventory, (default: true)
		/// or just report that it's there or not.
		/// @return Whether a firearm was successfully switched to, or already held.
		bool EquipFirearm(bool doEquip = true);

		/// Switches the currently held device (if any) to the first found device
		/// of the specified group in the inventory. If the held device already
		/// is of that group, or no device is in inventory, nothing happens.
		/// @param group The group the device must belong to.
		/// @param doEquip Whether to actually equip any matching item found in the inventory, (default: true)
		/// or just report that it's there or not.
		/// @return Whether a firearm was successfully switched to, or already held.
		bool EquipDeviceInGroup(const std::string& group, bool doEquip = true);

		/// Switches the currently held device (if any) to the first loaded HDFirearm
		/// of the specified group in the inventory. If no such weapon is in the
		/// inventory, nothing happens.
		/// @param group The group the HDFirearm must belong to. "Any" for all groups.
		/// @param exludeGroup The group the HDFirearm must *not* belong to. "None" for no group.
		/// @param doEquip Whether to actually equip any matching item found in the inventory, (default: true)
		/// or just report that it's there or not.
		/// @return Whether a firearm was successfully switched to, or already held.
		bool EquipLoadedFirearmInGroup(const std::string& group, const std::string& exludeGroup, bool doEquip = true);

		/// Switches the equipped HeldDevice (if any) to the first found device with the specified preset name in the inventory.
		/// If the equipped HeldDevice is of that module and preset name, nothing happens.
		/// @param presetName The preset name of the HeldDevice to equip.
		/// @param doEquip Whether to actually equip any matching item found in the inventory, or just report whether or not it's there.
		/// @return Whether a matching HeldDevice was successfully found/switched -o, or already held.
		bool EquipNamedDevice(const std::string& presetName, bool doEquip) { return EquipNamedDevice("", presetName, doEquip); }

		/// Switches the equipped HeldDevice (if any) to the first found device with the specified module and preset name in the inventory.
		/// If the equipped HeldDevice is of that module and preset name, nothing happens.
		/// @param moduleName The module name of the HeldDevice to equip.
		/// @param presetName The preset name of the HeldDevice to equip.
		/// @param doEquip Whether to actually equip any matching item found in the inventory, or just report whether or not it's there.
		/// @return Whether a matching HeldDevice was successfully found/switched -o, or already held.
		bool EquipNamedDevice(const std::string& moduleName, const std::string& presetName, bool doEquip);

		/// Switches the currently held device (if any) to the first found ThrownDevice
		/// in the inventory. If the held device already is a ThrownDevice, or no
		/// ThrownDevice  is in inventory, nothing happens.
		/// @param doEquip Whether to actually equip any matching item found in the inventory, (default: true)
		/// or just report that it's there or not.
		/// @return Whether a ThrownDevice was successfully switched to, or already held.
		bool EquipThrowable(bool doEquip = true);

		/// Switches the currently held device (if any) to the strongest digging tool in the inventory.
		/// @param doEquip Whether to actually equip the strongest digging tool, or just report whether a digging tool was found.
		/// @return Whether or not the strongest digging tool was successfully equipped.
		bool EquipDiggingTool(bool doEquip = true);

		/// Estimates what material strength any digger this AHuman is carrying can penetrate.
		/// @return The maximum material strength this AHuman's digger can penetrate, or a default dig strength if they don't have a digger.
		float EstimateDigStrength() const override;

		/// Estimates what door this AHuman can get through: with a digger, or by shooting it open with the strongest firearm carried.
		/// @return The strongest door material it can breach.
		float EstimateBreachStrength() const override;

		/// A human to the path grid: it can crawl, so it needs less head room than it stands.
		PathAgent GetPathAgent() const override;

		// Estimates how high this actor can jump.
		/// @return The actor's jump height.
		virtual float EstimateJumpHeight() const override;

		/// Flies the jetpack for a tick towards a point, the way a person flies one: predicted, not reacted. The jet's real push is known
		/// (learned in flight: see m_JetAccelRatio), and for each of ten choices (the jet on or off, leant hard or a little either way, or
		/// straight) the next second is flown in simulation, the choice held for the first fifth of it and a tracking rule after, against the
		/// terrain; the choice whose flight best follows the speeds wanted (up to just over the landing's height, across at a speed the jet's
		/// lean can stop from in the room left, then down onto it) and hits nothing is taken. The lean is set by the analog stick, which
		/// tilts the nozzle without turning the body round, so braking needs no turn.
		/// @param target The landing point, or the point to pass through.
		/// @param floorY The landing's floor (its top surface's y), or below zero for a point in the air to pass through.
		/// @return The analog stick's X to hold (with -1 for its Y), and in Y 1 to jet or 0 not to.
		Vector PilotFlight(const Vector& target, float floorY);

		/// Moves this a tick along the route the pathfinder gave it, under AI: every leg, walks, crawls, drops, flights (AHuman::PilotFlight),
		/// ladders and doors, with the controls set here. Called each AI tick by the movement script (SharedBehaviors.GoToRoute) in place of
		/// its own route-follower. See AHumanMovement.cpp.
		/// @return 0 while moving, 1 arrived at the last waypoint, 2 when there is no route to it.
		int MoveAlongRoute();

		/// Adds the route-follower's state to the unit's debug state: what it's doing (walk, fuel wait, flight, refuel, shaft, step), its progress and stuck timers, stuck level, fuel wait, impossible answers and the flight's landing.
		void GetDebugState(std::vector<DebugStateField>& fields) const override;

		/// Forgets the route-follower's state (a new order, or another behaviour taking over).
		void ResetRouteMovement() override;

		/// Gets whether this is getting up from having been knocked over (see UpdateGetUp).
		/// @return Whether it is getting up.
		bool IsGettingUp() const { return m_GettingUp; }

		/// Gets what this has learned of its jet's real push, against what the jetpack's numbers say. 1 is as modelled.
		/// @return The ratio.
		float GetJetAccelRatio() const { return m_JetAccelRatio; }
		/// Whether the body is flying on its jet just now (lit, or lit in the last 0.4 s): it passes the ladders' rungs, and a ladder lets it go.
		bool IsJetFlying() const { return m_JetFlying && !m_Ladder.active; }
		/// Whether the route-follower is flying a planned flight just now (take-off to landing; see MoveAlongRoute).
		bool IsFlyingRoute() const { return m_Mover.flight.active; }
		/// Whether the body is climbing a ladder, hand over hand (see UpdateLadder).
		bool IsClimbingLadder() const { return m_Ladder.active; }

		/// How high a leap on the legs lifts this (the body's centre, in pixels): set in the preset (LegJumpHeight), or by default a little
		/// over half the standing body's height, which every humanoid has, mods' included.
		/// @return The leap's height.
		float GetLegJumpHeight() const;
		/// Sets how high a leap lifts this, in pixels; below zero for the default, zero for no leap.
		/// @param height The height.
		void SetLegJumpHeight(float height) { m_LegJumpHeight = height; }
		/// How fast a leap carries this forward when a move key is held (m/s): set in the preset (LegJumpSpeed), by default 4.
		/// @return The speed.
		float GetLegJumpSpeed() const { return m_LegJumpSpeed; }
		/// Sets how fast a leap carries this forward, in m/s.
		/// @param speed The speed.
		void SetLegJumpSpeed(float speed) { m_LegJumpSpeed = speed; }
		/// Whether this is in the air on a leap of its legs just now.
		/// @return Whether leaping.
		bool IsLeaping() const { return m_Leaping; }
		/// Whether a leap could begin now: on its feet on the floor, with a leg, not lying down, climbing, mantling or just landed.
		/// @return Whether it can leap.
		bool CanLeap() const;

		/// The AI's motor (see UpdateAIMotor): what an AI script asks of the body, done by the engine. A script decides where to go and how to
		/// hold itself; the engine walks, crawls, climbs and flies. (A script may still press the controls itself, as a mod's may.)
		/// Holds a stance for a while: 0 none (as the movement has it), 1 crouched, 2 prone (crawling, when it also moves).
		void SetAIStance(int stance, float milliseconds);
		int GetAIStance() const { return m_AIStance; }
		/// A short move on this floor, for a while, without touching the unit's orders: a step into cover and back out, a crawl forward to
		/// shoot. Walked or crawled (as the stance has it), never flown; ended at the place, at a wall the walk's sense finds, or at the time.
		void TacticalMoveTo(const Vector& place, float milliseconds);
		void CancelTacticalMove() { m_Tactical.active = false; }
		bool IsTacticalMoveActive() const { return m_Tactical.active; }

		/// Switches the currently held device (if any) to the first found shield
		/// in the inventory. If the held device already is a shield, or no
		/// shield is in inventory, nothing happens.
		/// @return Whether a shield was successfully switched to, or already held.
		bool EquipShield();

		/// Tries to equip the first shield in inventory to the background arm;
		/// this only works if nothing is held at all, or the FG arm holds a
		/// one-handed device, or we're in inventory mode.
		/// @return Whether a shield was successfully equipped in the background arm.
		bool EquipShieldInBGArm(bool depositToFront = false);

		/// Tries to equip the first dual-wieldable in inventory to the background arm;
		/// this only works if nothing is held at all, or the FG arm holds a
		/// one-handed device, or we're in inventory mode.
		/// @return Whether a shield was successfully equipped in the background arm.
		//	bool EquipDualWieldableInBGArm();

		/// Gets the throw chargeup progress of this AHuman.
		/// @return The throw chargeup progress, as a scalar from 0 to 1.
		float GetThrowProgress() const { return m_ThrowPrepTime > 0 ? static_cast<float>(std::min(m_ThrowTmr.GetElapsedSimTimeMS() / static_cast<double>(m_ThrowPrepTime), 1.0)) : 1.0F; }

		/// Unequips whatever is in the FG arm and puts it into the inventory.
		/// @return Whether there was anything to unequip.
		bool UnequipFGArm();

		/// Unequips whatever is in the BG arm and puts it into the inventory.
		/// @return Whether there was anything to unequip.
		bool UnequipBGArm();

		/// Unequips whatever is in either of the arms and puts them into the inventory.
		void UnequipArms() {
			UnequipBGArm();
			UnequipFGArm();
		}

		/// Gets the FG Arm's HeldDevice. Ownership is NOT transferred.
		/// @return The FG Arm's HeldDevice.
		HeldDevice* GetEquippedItem() const { return m_pFGArm ? m_pFGArm->GetHeldDevice() : nullptr; }

		/// Gets the BG Arm's HeldDevice. Ownership is NOT transferred.
		/// @return The BG Arm's HeldDevice.
		HeldDevice* GetEquippedBGItem() const { return m_pBGArm ? m_pBGArm->GetHeldDevice() : nullptr; }

		/// Gets the total mass of this AHuman's currently equipped devices.
		/// @return The mass of this AHuman's equipped devices.
		float GetEquippedMass() const;

		/// Indicates whether the currently held HDFirearm's is ready for use, and has
		/// ammo etc.
		/// @return Whether a currently HDFirearm (if any) is ready for use.
		bool FirearmIsReady() const;

		/// Indicates whether the currently held ThrownDevice's is ready to go.
		/// @return Whether a currently held ThrownDevice (if any) is ready for use.
		bool ThrowableIsReady() const;

		/// Indicates whether the currently held HDFirearm's is out of ammo.
		/// @return Whether a currently HDFirearm (if any) is out of ammo.
		bool FirearmIsEmpty() const;

		/// Indicates whether any currently held HDFirearms are almost out of ammo.
		/// @return Whether a currently HDFirearm (if any) has less than half of ammo left.
		bool FirearmNeedsReload() const;

		/// Indicates whether currently held HDFirearms are reloading. If the parameter is true, it will only return true if all firearms are reloading, otherwise it will return whether any firearm is reloading.
		/// @return Whether or not currently held HDFirearms are reloading.
		bool FirearmsAreReloading(bool onlyIfAllFirearmsAreReloading) const;

		/// Indicates whether the currently held HDFirearm's is semi or full auto.
		/// @return Whether a currently HDFirearm (if any) is a semi auto device.
		bool FirearmIsSemiAuto() const;

		/// Returns the currently held device's delay between pulling the trigger
		/// and activating.
		/// @return Delay in ms or zero if not a HDFirearm.
		int FirearmActivationDelay() const;

		/// Reloads the currently held firearm, if any. Will only reload the BG Firearm if the FG one is full already, to support reloading guns one at a time.
		/// @param onlyReloadEmptyFirearms Whether or not to only reload empty fireams. (default: false)
		void ReloadFirearms(bool onlyReloadEmptyFirearms = false);

		/// Tells whether a point on the scene is within close range of the currently
		/// used device and aiming status, if applicable.
		/// @param point A Vector with the aboslute coordinates of a point to check.
		/// @return Whether the point is within close range of this.
		bool IsWithinRange(Vector& point) const override;

		/// Casts an unseen-revealing ray in the direction of where this is facing.
		/// @param FOVSpread The degree angle to deviate from the current view point in the ray
		/// casting. A random ray will be chosen out of this +-range.
		/// @param range The range, in pixels, beyond the actors sharp aim that the ray will have.
		/// @return Whether any unseen pixels were revealed by this look.
		bool Look(float FOVSpread, float range) override;

		/// Casts a material detecting ray in the direction of where this is facing.
		/// @param FOVSpread The degree angle to deviate from the current view point in the ray
		/// casting. A random ray will be chosen out of this +-range.
		/// @param range The range, in pixels, that the ray will have.
		/// @param foundLocation A Vector which will be filled with the absolute coordinates of any
		/// found gold. It will be unaltered if false is returned.
		/// @return Whether gold was spotted by this ray cast. If so, foundLocation
		/// has been filled out with the absolute location of the gold.
		bool LookForGold(float FOVSpread, float range, Vector& foundLocation) const;

		/// Casts an MO detecting ray in the direction of where the head is looking
		/// at the time. Factors including head rotation, sharp aim mode, and
		/// other variables determine how this ray is cast.
		/// @param FOVSpread The degree angle to deviate from the current view point in the ray (default: 45)
		/// casting. A random ray will be chosen out of this +-range.
		/// @param ignoreMaterial A specific material ID to ignore (see through) (default: 0)
		/// @param ignoreAllTerrain Whether to ignore all terrain or not (true means 'x-ray vision'). (default: false)
		/// @return A pointer to the MO seen while looking.
		MovableObject* LookForMOs(float FOVSpread = 45, unsigned char ignoreMaterial = 0, bool ignoreAllTerrain = false);

		/// Gets the GUI representation of this AHuman, only defaulting to its Head or body if no GraphicalIcon has been defined.
		/// @return The graphical representation of this AHuman as a BITMAP.
		BITMAP* GetGraphicalIcon() const override;

		/// Resest all the timers used by this. Can be emitters, etc. This is to
		/// prevent backed up emissions to come out all at once while this has been
		/// held dormant in an inventory.
		void ResetAllTimers() override;

		/// Gets the walk path rotation for the specified Layer.
		/// @param whichLayer The Layer in question.
		/// @return The walk angle in radians.
		float GetWalkAngle(AHuman::Layer whichLayer) const { return m_WalkAngle[whichLayer].GetRadAngle(); }

		/// Sets the walk path rotation for the specified Layer.
		/// @param whichLayer The Layer in question.
		/// @param angle The angle to set.
		void SetWalkAngle(AHuman::Layer whichLayer, float angle) { m_WalkAngle[whichLayer] = Matrix(angle); }

		/// Gets whether this AHuman has just taken a stride this frame.
		/// @return Whether this AHuman has taken a stride this frame or not.
		bool StrideFrame() const { return m_StrideFrame; }

		/// Gets whether this AHuman is currently attempting to climb something, using arms.
		/// @return Whether this AHuman is currently climbing or not.
		bool IsClimbing() const { return m_ArmClimbing[FGROUND] || m_ArmClimbing[BGROUND]; }

		/// Update called prior to controller update. Ugly hack. Supposed to be done every frame.
		void PreControllerUpdate() override;

		/// Updates this MovableObject. Supposed to be done every frame.
		void Update() override;

		/// Draws this AHuman's current graphical representation to a
		/// BITMAP of choice.
		/// @param pTargetBitmap A pointer to a BITMAP to draw on.
		/// @param targetPos The absolute position of the target bitmap's upper left corner in the Scene. (default: Vector())
		/// @param mode In which mode to draw in. See the DrawMode enumeration for the modes. (default: g_DrawColor)
		/// @param onlyPhysical Whether to not draw any extra 'ghost' items of this MovableObject, (default: false)
		/// indicator arrows or hovering HUD text and so on.
		void Draw(BITMAP* pTargetBitmap, const Vector& targetPos = Vector(), DrawMode mode = g_DrawColor, bool onlyPhysical = false) const override;
		void Draw(const Camera& camera) const override;

		/// Draws this Actor's current graphical HUD overlay representation to a
		/// BITMAP of choice.
		/// @param pTargetBitmap A pointer to a BITMAP to draw on.
		/// @param targetPos The absolute position of the target bitmap's upper left corner in the Scene. (default: Vector())
		/// @param whichScreen Which player's screen this is being drawn to. May affect what HUD elements (default: 0)
		/// get drawn etc.
		void DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos = Vector(), int whichScreen = 0, bool playerControlled = false) override;
		void DrawHUD(const Camera& camera) override;

		/// Gets the LimbPath corresponding to the passed in Layer and MovementState values.
		/// @param layer Whether to get foreground or background LimbPath.
		/// @param movementState Which movement state to get the LimbPath for.
		/// @return The LimbPath corresponding to the passed in Layer and MovementState values.
		LimbPath* GetLimbPath(Layer layer, MovementState movementState) { return &m_Paths[layer][movementState]; }

		/// Shortcut to get the speed of a particular move state's FG (and left side if relevant) limb path.
		/// @param movementState Which movement state to get the limb path speed for.
		/// @return Limb path speed for the specified movement state in m/s.
		float GetLimbPathTravelSpeed(MovementState movementState);

		/// Shortcut to set the speed of a particular move state's limb path, including all layers (and sides if relevant)
		/// @param movementState Which movement state to set the limb path speed for.
		/// @param newSpeed New speed value in m/s.
		void SetLimbPathTravelSpeed(MovementState movementState, float newSpeed);

		/// Shortcut to get the push force of a particular move state's FG (and left side if relevant) limb path.
		/// @return The push force, in kg * m/s^2.
		float GetLimbPathPushForce(MovementState movementState);

		/// Shortcut to set the push force of a particular move state's limb path, including all layers (and sides if relevant)
		/// @param movementState Which movement state to set the limb path speed for.
		/// @param newForce New push force value in kg * m/s^2.
		void SetLimbPathPushForce(MovementState movementState, float newForce);

		/// Gets the target rot angle for the given MovementState.
		/// @param movementState The MovementState to get the rot angle target for.
		/// @return The target rot angle for the given MovementState.
		float GetRotAngleTarget(MovementState movementState) { return m_RotAngleTargets[movementState]; }

		/// Sets the target rot angle for the given MovementState.
		/// @param movementState The MovementState to get the rot angle target for.
		/// @param newRotAngleTarget The new rot angle target to use.
		void SetRotAngleTarget(MovementState movementState, float newRotAngleTarget) { m_RotAngleTargets[movementState] = newRotAngleTarget; }

		/// Gets the duration it takes this AHuman to fully charge a throw.
		/// @return The duration it takes to fully charge a throw in MS.
		long GetThrowPrepTime() const { return m_ThrowPrepTime; }

		/// Sets the duration it takes this AHuman to fully charge a throw.
		/// @param newPrepTime New duration to fully charge a throw in MS.
		void SetThrowPrepTime(long newPrepTime) { m_ThrowPrepTime = newPrepTime; }

		/// Gets the rate at which this AHuman's Arms will swing with Leg movement, if they're not holding or supporting a HeldDevice.
		/// @return The arm swing rate of this AHuman.
		float GetArmSwingRate() const { return m_ArmSwingRate; }

		/// Sets the rate at which this AHuman's Arms will swing with Leg movement, if they're not holding or supporting a HeldDevice.
		/// @param newValue The new arm swing rate for this AHuman.
		void SetArmSwingRate(float newValue) { m_ArmSwingRate = newValue; }

		/// Gets the rate at which this AHuman's Arms will sway with Leg movement, if they're holding or supporting a HeldDevice.
		/// @return The device arm sway rate of this AHuman.
		float GetDeviceArmSwayRate() const { return m_DeviceArmSwayRate; }

		/// Sets the rate at which this AHuman's Arms will sway with Leg movement, if they're holding or supporting a HeldDevice.
		/// @param newValue The new device arm sway rate for this AHuman.
		void SetDeviceArmSwayRate(float newValue) { m_DeviceArmSwayRate = newValue; }

		/// Gets this AHuman's max walkpath adjustment upwards to crouch below low ceilings.
		/// @return This AHuman's max walkpath adjustment.
		float GetMaxWalkPathCrouchShift() const { return m_MaxWalkPathCrouchShift; }

		/// Sets this AHuman's max walkpath adjustment upwards to crouch below low ceilings.
		/// @param newValue The new value for this AHuman's max walkpath adjustment.
		void SetMaxWalkPathCrouchShift(float newValue) { m_MaxWalkPathCrouchShift = newValue; }

		/// Gets how tall this AHuman is crouched, as a fraction of its height (the standing body is 0.44 of it, see GetPathAgent).
		/// @return The crouch height fraction.
		float GetCrouchHeightFraction() const { return m_CrouchHeightFraction; }

		/// Sets how tall this AHuman is crouched, as a fraction of its height.
		/// @param newValue The new crouch height fraction.
		void SetCrouchHeightFraction(float newValue) { m_CrouchHeightFraction = newValue; }

		/// Gets the head room this AHuman needs crouched, in pixels: between its crawl and standing heights.
		/// @return The crouched height, in pixels.
		float GetCrouchHeight() const;

		/// Gets how far this AHuman's walk path is shifted up when fully crouched, in pixels: from standing height down to crouched height, or
		/// MaxWalkPathCrouchShift if that is more.
		/// @return The full crouch's walk path shift, in pixels.
		float GetCrouchShift() const;

		/// How much of a target this body makes, for the sight of others (see Actor::ScanForEnemies): lying down a little over half, crouched
		/// four fifths.
		float GetSightProfile() const override { return m_ProneState != NOTPRONE ? 0.55F : 1.0F - 0.2F * m_CrouchAmount; }

		/// Gets this AHuman's current crouch amount. 0.0 == fully standing, 1.0 == fully crouched.
		/// @return This AHuman's current crouch amount.
		float GetCrouchAmount() const { return m_CrouchAmount; }

		/// Gets this AHuman's current crouch amount override. 0.0 == fully standing, 1.0 == fully crouched, -1 == no override.
		/// @return This AHuman's current crouch amount override.
		float GetCrouchAmountOverride() const { return m_CrouchAmountOverride; }

		/// Sets this AHuman's current crouch amount override.
		/// @param newValue The new value for this AHuman's current crouch amount override.
		void SetCrouchAmountOverride(float newValue) { m_CrouchAmountOverride = newValue; }

		/// Gets this AHuman's stride sound. Ownership is NOT transferred!
		/// @return The SoundContainer for this AHuman's stride sound.
		SoundContainer* GetStrideSound() const { return m_StrideSound; }

		/// Sets this AHuman's stride sound. Ownership IS transferred!
		/// @param newSound The new SoundContainer for this AHuman's stride sound.
		void SetStrideSound(SoundContainer* newSound) { m_StrideSound = newSound; }

		/// Protected member variable and method declarations
	protected:
		/// Function that is called when we get a new movepath.
		/// This processes and cleans up the movepath.
		void OnNewMovePath() override;

		/// Draws an aiming aid in front of this AHuman for throwing.
		/// @param targetBitmap A pointer to a BITMAP to draw on.
		/// @param targetPos The absolute position of the target bitmap's upper left corner in the Scene.
		/// @param progressScalar A normalized scalar that determines the magnitude of the reticle, to indicate force in the throw.
		void DrawThrowingReticle(BITMAP* targetBitmap, const Vector& targetPos = Vector(), float progressScalar = 1.0F) const;

		/// Detects slopes in terrain and updates the walk path rotation for the corresponding Layer accordingly.
		/// @param whichLayer The Layer in question.
		/// Corner correction: walking into a step, or rising on the jetpack into a lip, by no more than 3 px, the body is shifted past it
		/// (lifted over the step, or slid aside off the lip), provided the whole body fits where it is shifted to.
		void CorrectCorners();

		void UpdateWalkAngle(AHuman::Layer whichLayer);

		/// Detects overhead ceilings and crouches for them.
		void UpdateCrouching();

		/// Updates our limbpath speed based on our current movement speed and style (crouching, walking etc).
		void UpdateLimbPathSpeed();

		// Member variables
		static Entity::ClassInfo m_sClass;
		// Articulated head.
		Attachable* m_pHead;
		// Ratio at which the head's rotation follows the aim angle
		float m_LookToAimRatio;
		// Foreground arm.
		Arm* m_pFGArm;
		// Background arm.
		Arm* m_pBGArm;
		// Foreground leg.
		Leg* m_pFGLeg;
		// Background leg.
		Leg* m_pBGLeg;
		// Limb AtomGroups.
		AtomGroup* m_pFGHandGroup;
		AtomGroup* m_pBGHandGroup;
		AtomGroup* m_pFGFootGroup;
		AtomGroup* m_BackupFGFootGroup;
		AtomGroup* m_pBGFootGroup;
		AtomGroup* m_BackupBGFootGroup;
		// The sound of the actor taking a step (think robot servo)
		SoundContainer* m_StrideSound;
		// Jetpack booster.
		AEJetpack* m_pJetpack;
		bool m_CanActivateBGItem; //!< A flag for whether or not the BG item is waiting to be activated separately. Used for dual-wielding. TODO: Should this be able to be toggled off per actor, device, or controller?
		bool m_TriggerPulled; //!< Internal flag for whether this AHuman is currently holding down the trigger of a HDFirearm. Used for dual-wielding.
		bool m_WaitingToReloadOffhand; //!< A flag for whether or not the offhand HeldDevice is waiting to be reloaded.
		// Blink timer
		Timer m_IconBlinkTimer;
		// Current upper body state.
		UpperBodyState m_ArmsState;
		// Whether the guy is currently lying down on the ground, rotational spring pulling him that way
		// This is engaged if the player first crouches (still upright spring), and then presses left/right
		// It is disengaged as soon as the crouch button/direction is released
		ProneState m_ProneState;
		// Timer for the going prone procedural animation
		Timer m_ProneTimer;
		// The maximum amount our walkpath can be shifted upwards to crouch, whether manually or automatically.
		float m_MaxWalkPathCrouchShift;
		float m_CrouchHeightFraction; //!< How tall the body is crouched, as a fraction of its height: the full crouch lowers it from standing (0.44) to this.
		bool m_CrouchWalking; //!< Whether it is striding on the crouched walk's leg paths (WALKCROUCH): more than half crouched while walking.
		bool m_CrouchWalkFromWalk; //!< Whether the crouched walk's leg paths were made from the walk's (none given), so they follow its speed and push.
		// The current crouching amount from 0.0 to 1.0, where 1.0 is applying maximum walk path shift.
		float m_CrouchAmount;
		// The script-set forced crouching amount. 0.0 == fully standing, 1.0 == fully crouched, -1 == no override.
		float m_CrouchAmountOverride;
		// Limb paths for different movement states.
		// [0] is for the foreground limbs, and [1] is for BG.
		LimbPath m_Paths[2][MOVEMENTSTATECOUNT];
		std::array<float, MOVEMENTSTATECOUNT> m_RotAngleTargets; //!< An array of rot angle targets for different movement states.
		// Whether was aiming during the last frame too.
		bool m_Aiming;
		// Whether the BG Arm is helping with locomotion or not.
		bool m_ArmClimbing[2];
		// Whether a stride was taken this frame or not.
		bool m_StrideFrame = false;
		// Controls the start of leg synch.
		bool m_StrideStart;
		// Times the stride to see if it is taking too long and needs restart
		Timer m_StrideTimer;
		// For timing throws
		Timer m_ThrowTmr;
		// The duration it takes this AHuman to fully charge a throw.
		long m_ThrowPrepTime;
		Timer m_SharpAimRevertTimer; //!< For timing the transition from sharp aim back to regular aim.
		float m_FGArmFlailScalar; //!< The rate at which this AHuman's FG Arm follows the the bodily rotation. Best to keep this at 0 so it doesn't complicate aiming.
		float m_BGArmFlailScalar; //!< The rate at which this AHuman's BG Arm follows the the bodily rotation. Set to a negative value for a "counterweight" effect.
		Timer m_EquipHUDTimer; //!< Timer for showing the name of any newly equipped Device.
		std::array<Matrix, 2> m_WalkAngle; //!< An array of rot angle targets for different movement states.
		Vector m_WalkPathOffset;
		float m_ArmSwingRate; //!< Controls the rate at which this AHuman's Arms follow the movement of its Legs while they're not holding device(s).
		float m_DeviceArmSwayRate; //!< Controls the rate at which this AHuman's Arms follow the movement of its Legs while they're holding device(s). One-handed devices sway half as much as two-handed ones. Defaults to three quarters of Arm swing rate.

		// AI States
		enum DeviceHandlingState {
			STILL = 0,
			POINTING,
			SCANNING,
			AIMING,
			FIRING,
			THROWING,
			DIGGING
		};

		enum SweepState {
			NOSWEEP = 0,
			SWEEPINGUP,
			SWEEPUPPAUSE,
			SWEEPINGDOWN,
			SWEEPDOWNPAUSE
		};

		enum DigState {
			NOTDIGGING = 0,
			PREDIG,
			STARTDIG,
			TUNNELING,
			FINISHINGDIG,
			PAUSEDIGGER
		};

		enum JumpState {
			NOTJUMPING = 0,
			FORWARDJUMP,
			PREUPJUMP,
			UPJUMP,
			APEXJUMP,
			LANDJUMP
		};

#pragma region Event Handling
		/// Event listener to be run while this AHuman's PieMenu is opened.
		/// @param pieMenu The PieMenu this event listener needs to listen to. This will always be this' m_PieMenu and only exists for std::bind.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int WhilePieMenuOpenListener(const PieMenu* pieMenu) override;
#pragma endregion

		/// Private member variable and method declarations
	private:
		float m_JetAccelRatio = 1.0F; //!< The jet's measured push against its modelled one, learned while it flies free (see PilotFlight).
		mutable std::array<float, 256> m_ClimbFuelCache; //!< ClimbFuelNeeded by 8 px of height and reserve, refreshed every couple of seconds.
		mutable double m_ClimbFuelCacheTimeMS = -1.0;
		Vector m_JetPrevVel; //!< Last frame's velocity, for the learning.
		bool m_JetPrevFree = false; //!< Whether last frame the jet was lit with nothing touching the body.
		double m_JetLastLitSimMS = -1.0; //!< When the jet was last lit (sim ms), for passing the ladders' rungs while flying (see LearnFlight).
		Timer m_FightAimTimer; //!< Since the unit last fired or aimed sharp: an AI unit's aim holds its facing (walking backwards) only in a fight.
		const Attachable* m_HeadRimFor = nullptr; //!< The head given its soft rim (see Update), so a new head gets one too.
		float m_LegJumpHeight = -1.0F; //!< How high a leap lifts the body, px (see GetLegJumpHeight); below zero for the default.
		float m_LegJumpSpeed = 4.0F; //!< How fast a leap carries the body forward with a move key held, m/s.
		bool m_Leaping = false; //!< In the air on a leap just now.
		Timer m_LeapTimer; //!< Since the leap began, or since it landed (for the pause before the next).
		/// The leap on the legs (BODY_LEAP): begun from the floor with a push of the body's speed, the jump pose held while in the air, ended
		/// on landing. Called before the jetpack's update each frame.
		void UpdateLeap();
		bool m_JetFlying = false; //!< Flying on the jet just now: lit, or lit in the last 0.4 s (see LearnFlight).
		float m_FeetBelowPos = -1.0F; //!< How far under Pos the floor is when this stands, learned standing; below zero until seen.
		int m_PilotLastChoice = -1;
		bool m_WasAirborne = false; //!< Whether last frame nothing was under the feet (for the landing squat).
		float m_LandingSquat = 0.0F; //!< How deep the landing squat goes, 0 to 1; eases off over a moment after touchdown.
		Timer m_LandingTimer;
		bool m_CrouchOverrideOurs = false; //!< Whether the crouch override is this class's (the squat or the mantle), to put back after.
		bool m_GettingUp = false; //!< Rising from lying knocked over: the body is lifted and righted over a moment (see UpdateGetUp).
		Timer m_GetUpTimer;
		float m_GetUpFromRot = 0.0F;
		Vector m_GetUpFrom;
		Vector m_GetUpTo;
		/// Recovers early from being knocked over once the body lies still, and runs the get-up. Called from Update.
		void UpdateGetUp(float& rot);
		bool m_PilotJetOn = false; //!< Whether PilotFlight last asked for the jet, and since when: a decision is held a moment (see PilotFlight).
		Timer m_PilotJetTimer;
		float m_PilotLean = 0.0F; //!< The lean PilotFlight last gave, which it moves towards the one chosen at a thumb's pace.
		Timer m_PilotTraceTimer; //!< For PilotFlight's trace lines. //!< PilotFlight's last choice, kept unless another is clearly better.

		/// The jet's push at full lean-less burn now, in px/s^2, as learned.
		float JetAccelNow() const;

		/// The route-follower's state (see MoveAlongRoute).
		struct RouteMover {
			enum Result {
				Moving = 0,
				Arrived = 1,
				Impossible = 2
			};
			struct Flight {
				bool active = false;
				Vector landing;
				float floorY = 0.0F;
				int pointsToLanding = 0;
				Timer timer;
				bool via = false; //!< Up a shaft first: flown to a point over its mouth before turning for the landing.
				Vector viaPoint;
				bool refuelling = false; //!< The tank ran dry under the landing: falling with the jet out until there is enough to go on.
				int stages = 0; //!< How many times it has refuelled on this flight.
				float startY = 0.0F; //!< Where the flight began, and the highest it has been (y), for the climb's failure tests.
				float bestY = 0.0F;
				Timer riseTimer; //!< Since the climb last gained height.
				Timer totalTimer; //!< Since the flight began (the other timer starts again at each refuel).
				Vector takeOff; //!< Where the flight began: a failed flight is remembered as this take-off for that landing (see Actor::AvoidPathLink).
				bool step = false; //!< Out of a shaft with the landing to one side: holding the height and stepping across onto it.
				float holdY = 0.0F; //!< The height held while stepping across (raised a pixel at a time until the feet clear the lip).
				Timer stepTimer;
				bool fromWater = false; //!< Taken off from the surface of a liquid (LM-4): still in it for the first moments of the climb, which is no coming down in it.
			};
			bool begun = false;
			long long lastCallTick = -1; //!< The sim update the follower was last called on, to tell a hold by whoever drives it (see MoveAlongRoute).
			Flight flight;
			Timer progressTimer; //!< Since the unit last got nearer the route's point.
			float bestGap = -1.0F;
			Vector lastProgressPos;
			Timer repathTimer;
			Timer noSightTimer;
			Timer routeCheckTimer;
			Timer doorWaitTimer;
			Timer doorIgnoreTimer;
			long doorWaitID = 0;
			long doorIgnoreID = 0;
			Timer proneHoldTimer;
			bool crouching = false; //!< The walk is ducking under something low (see MoveAlongRoute's walk): kept up 400 ms after the room returns.
			Timer crouchHoldTimer;
			Timer hopTimer;
			Vector stuckSpot; //!< Where the step it was last long stuck on led (its landing, or the route's next point).
			int stuckLevel = 0; //!< How many times running it has been long stuck on a step to much that same place.
			Timer stuckSpotTimer; //!< Since it was first stuck there.
			Timer steadyTimer; //!< Since the unit was last not standing still and upright, for the settle before a long flight.
			bool settling = false; //!< Waiting to settle before a take-off just now.
			bool leapWatch = false; //!< A leap of the route's is under way: where from and to, to judge it by when it comes down.
			Vector leapFrom;
			Vector leapTo;
			Timer leapTimer;
			long long pilotedFallTick = -1; //!< The sim update the route-follower last flew a fall without a flight (PilotFlight for the point), for the motor's brake to keep off.
			bool standUp = false; //!< At a leap's take-off lying down: the motor's prone stance gives way (see UpdateAIMotor), for a moment after.
			Timer standUpTimer;
			bool noTakeOff = false; //!< At a take-off that the flight can't begin from, just now.
			Timer noTakeOffTimer; //!< Since then.
			Timer settleWaitTimer; //!< Since the wait to settle began: it never lasts more than a second and a half.
			Timer traceTimer;
			bool fuelWaiting = false; //!< Standing for the tank to fill before a flight, and since when.
			Timer fuelWaitTimer;
			int impossibleAnswers = 0;
			int impossibleSeen = 0; //!< The actor's impossible-answer count when last looked at: an answer is counted when it changes.
			double lastJetTime = -1.0;
			Timer senseRerouteTimer; //!< Since the sense last asked for a route round a wall the grid didn't know.
			bool digging = false; //!< Digging along the route (a Dig step), the digger out; put away again after.
			float digSweep = 0.0F; //!< The digger's sweep either side of the way, radians.
			bool digSweepUp = true;
			int remedy = -1; //!< The stuck remedy being tried just now (StuckRemedy), or -1 (see MoveAlongRoute's walk).
			Timer remedyTimer; //!< Since it began.
			Vector remedySpot; //!< Where the unit was stuck when it began.
			unsigned int remedyTried = 0; //!< The remedies tried this time stuck, one bit each.
			bool swimming = false; //!< In liquid with the body under (LM-4), since the follower last looked: for the re-route on falling in.
			Vector debugTakeOff; //!< Where the flight ahead takes off, for the overlay; hasTakeOff when there is one.
			bool hasTakeOff = false;
			bool takeOffCommitted = false; //!< Reached a take-off, and lining up for it nearby: the flight's rules hold until off or a while.
			Vector takeOffCommit;
			Timer takeOffCommitTimer;
			/// The last flight as the navigation overlay shows it (level 2): the way it was planned against the way it went, and the fuel it
			/// was expected to take against what it burned. Kept only while the overlay is on (see RecordFlightDebug); nothing reads it but the overlay.
			struct FlightRecord {
				bool recording = false;
				Vector takeOff; //!< The flight's take-off and landing, to tell one flight from the next.
				Vector landing;
				std::vector<Vector> planned; //!< Take-off, the point over a shaft's mouth when there is one, and the landing, where the body is meant to be.
				std::deque<Vector> trail; //!< Where the body went, a point every few pixels, the oldest dropped past 240.
				float fuelPredicted = 0.0F; //!< FlightFuelNeeded as the flight began, ms of jet.
				float fuelUsed = 0.0F; //!< What the tank lost over the flight, refills not counted back, ms of jet.
				float lastFuel = 0.0F;
				float tank = 0.0F; //!< The jet's full tank, ms.
				Timer sinceEnd; //!< Since it landed or gave up, so the record still shows for a few seconds after.
			};
			FlightRecord debugFlight;
		};
		RouteMover m_Mover;

		/// The small things a stuck unit tries before the follower's re-path at 6 s, in the order they are tried (LM-3).
		enum class StuckRemedy {
			Crouch, //!< Duck and keep walking.
			BackOff, //!< Half a body back, then on again.
			Leap, //!< A leap on the legs.
			Hop, //!< A hop with the jet.
			Prone, //!< Lie down and crawl.
			Stand, //!< Stand up from lying down.
			Count
		};
		/// What a remedy did at a spot: kept across routes and orders (the route-follower's own state starts again with each), so a remedy
		/// that failed at a spot is not tried there again for a while, and one that worked is tried first.
		struct StuckRemedyMemory {
			Vector Spot;
			int Remedy = 0;
			bool Worked = false;
			Timer Age;
		};
		std::deque<StuckRemedyMemory> m_StuckRemedyMemory;
		void RememberStuckRemedy(const Vector& spot, int remedy, bool worked);
		/// Picks the next remedy to try at a spot, of those allowed now and not tried this time stuck: one that worked there first, then the
		/// rest in order, leaving out those that failed there last time. @return The remedy, or -1 for none.
		int PickStuckRemedy(const Vector& spot, const std::array<bool, static_cast<int>(StuckRemedy::Count)>& allowed, unsigned int tried) const;

		/// Climbing a ladder: the body held to the ladder's line and moved along it by the climb (as the mantle moves it: gravity, the jet and
		/// the walls are nothing to it meanwhile), the hands and feet on the rungs, one limb at a time, hand and opposite foot in turn.
		struct LadderClimb {
			bool active = false;
			bool material = false; //!< Rungs of the Ladder material; else a background ladder (rungs every 8 px).
			float bodyX = 0.0F; //!< Where the body hangs.
			float gripX = 0.0F; //!< Where the hands and feet take the rungs (the rungs' outer ends).
			int wallSide = 0; //!< Which side the ladder stands from a wall: -1 the wall on the left, 1 on the right, 0 free-standing.
			Vector pos; //!< Where the body is held.
			float climbed = 0.0F; //!< Distance climbed, for the sway.
			std::array<float, 4> grip = {}; //!< The rung (y) each limb holds: FG hand, BG hand, FG foot, BG foot.
			std::array<bool, 4> gripped = {};
			double lastRegripMS = -1.0;
			double lastHandRegripMS = -1.0;
			int lastLimb = -1;
			Timer lostTimer;
			Timer startTimer;
		};
		LadderClimb m_Ladder;
		struct TacticalMove {
			bool active = false;
			Vector place;
			Timer timer;
			float limitMS = 0.0F;
		};
		TacticalMove m_Tactical;
		int m_AIStance = 0;
		Timer m_AIStanceTimer;
		float m_AIStanceMS = 0.0F;
		/// The AI's motor, before anything reads the controls: the stance held, the tactical move walked, and a fall braked (off a route's
		/// flight, which the pilot brakes): for every AI humanoid, whatever its script, so a script needn't press the jet or the keys itself.
		void UpdateAIMotor();
		/// What is in the way a short stride ahead on the ground, the body's whole outline looked at (every 2 px across and up; rungs and doors
		/// aside), as the walk sees it: nothing, a step the legs take, a low obstacle (and its height), room only to crawl under, or a wall.
		struct Sensed {
			bool any = false;
			float distance = 0.0F; //!< From the body's middle to it.
			float rise = 0.0F; //!< The height of what stands up from the floor there.
			bool gapUnder = false; //!< Open underneath to crawl height, blocked at the head.
			bool wall = false; //!< Blocked from the floor to over the head (or too low a gap to crawl).
		};
		Sensed SenseAhead(float direction, float floorY, float standing) const;
		/// The ladder within reach across of a point, if any: where a climber's body hangs on it, where its rungs are taken, which side its wall
		/// is, and whether its rungs are Ladder material (else a background ladder). @return Whether there is one.
		/// @param way -1 for a ladder going up from here (rungs over the chest), 1 for one going down (rungs under the feet), 0 for either.
		bool FindLadderNear(const Vector& at, float reachX, float& bodyX, float& gripX, int& wallSide, bool& material, int way = 0) const;
		/// The rungs (their y) of the ladder being climbed between two heights, top first.
		void LadderRungs(float fromY, float toY, std::vector<float>& rungs) const;
		/// Before the jet: taking hold of a ladder (up or down pressed at one), and letting go (a side key, or the jet's key alone).
		void UpdateLadderInput();
		/// The body along the ladder, each frame: the climb's pace with the pull of each hand, the top and the bottom.
		void UpdateLadder();
		/// The hands and feet on the rungs.
		void UpdateLadderLimbs();
		void LetGoOfLadder(const Vector& velocity);
		static std::vector<Vector> s_LadderNodes; //!< The scene's background ladder nodes, found now and then (see LadderNear).
		static std::shared_mutex s_LadderNodesMutex; //!< The AI's route-following runs on several threads at once: one refreshes the nodes
		                                             //!< while the others read them.
		static double s_LadderNodesSimTimeMS; //!< When the nodes were last found, in sim ms; below zero until they have been. (A plain
		                                      //!< number, not a Timer: a static Timer is built at program start, before the timing manager it
		                                      //!< reads, and crashed the game before its window opened.)
		static std::optional<Vector> LadderNear(const Vector& point, float reachX, float reachY);
		/// Drops the route's points up to a flight's landing: up to the one nearest the landing, or the planned count if none is near it.
		void PopRouteToLanding(const Vector& landing, int pointsToLanding);
		ADoor* DoorAhead(const Vector& toPoint) const;
		bool InDoorSweep() const;
		float FlightFuelNeeded(const Vector& landing, float landingFloorY) const;
		/// The jet's push at a given fuel left, in px/s^2: the push now (as learned in flight) scaled by the throttle, which follows the tank.
		float JetAccelAtFuel(float fuel) const;
		/// The least fuel the jet lights on from rest, in ms (the engine's 250 ms at the throttle, and the pack's own minimum ratio).
		float JetRelightFuel() const;
		/// What the tank has left at the top of a straight climb of a height, begun with so much fuel, flown as the pilot flies it (full burn
		/// to the speed cap, held, coasting from the height gravity stops it in); or below zero when the climb can't be made on it.
		float ClimbFuelLeft(float height, float fuel) const;
		/// The least fuel a climb of a height takes with a reserve left at the top, in ms; most of a tank when no tank makes it.
		float ClimbFuelNeeded(float height, float reserve) const;
		/// The fuel a climb burns per pixel of height on this unit's jet, for the path finder's flight links (see PathAgent::JetClimbMSPerPx).
		float ClimbFuelPerPixel() const;
		bool FlightWayClear(const Vector& landing, float landingFloorY) const;
		bool CanWalkTo(const Vector& landing, float landingFloorY) const;
		bool FindLanding(Vector& landing, float& landingFloorY, int& pointsToLanding) const;
		/// FindLanding, and where the flight would take off: the last point on the floor before the route leaves it (the unit itself when the
		/// route leaves the floor from where it stands).
		bool FindLanding(Vector& landing, float& landingFloorY, int& pointsToLanding, Vector& takeOff) const;
		/// The shaft a climb goes up, if it is one: walls both sides of the route's column, looked at every few pixels from the head's start
		/// to the head's height at the top. @param columnX The route's column. @param topHeadY Where the head will be at the top.
		/// @return Whether a shaft; its middle (moved to a line open all the way up, when the middle isn't) and its width.
		bool ShaftColumn(float columnX, float topHeadY, float& middleX, float& width) const;
		/// Whether a vertical line is open from one height up to another (terrain only: a door across it opens as we come).
		bool ColumnOpen(float x, float fromY, float toY) const;
		void PopRoutePoint();
		/// Drops the route but keeps the place it was for, and asks for a new one to it. (ClearMovePath forgets the goal too, and set the
		/// target to the unit's own place: every refresh told the unit it had arrived.)
		void RefreshRoute();
		void MoverTrace(const std::string& text) const;
		/// The navigation debug overlay's view of the route-follower (level 2): the route ahead, the point in hand, and a flight's take-off,
		/// shaft point and landing.
		void DrawMoverDebug() const;

		/// Keeps the flight record the navigation overlay draws beside the flight (RouteMover::debugFlight): the plan as the flight began, the
		/// trail it has flown and the fuel it has burned. Called each update while the overlay is at level 2 or more, before DrawMoverDebug.
		void RecordFlightDebug();

		/// Learns the jet's real push and the standing height, each frame (see PilotFlight).
		void LearnFlight();

		/// Clears all the member variables of this AHuman, effectively
		/// resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		AHuman(const AHuman& reference) = delete;
		AHuman& operator=(const AHuman& rhs) = delete;
	};

} // namespace RTE
