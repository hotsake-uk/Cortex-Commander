#pragma once

/// Header file for the HeldDevice class.
/// @author Daniel Tabar
/// data@datarealms.com
/// http://www.datarealms.com
/// Inclusions of header files
#include "Attachable.h"
#include "Actor.h"

#include <array>
#include <unordered_map>

namespace RTE {

	enum HeldDeviceType {
		WEAPON = 0,
		TOOL,
		SHIELD,
		BOMB,
	};

	enum HeldDeviceHotkeyType {
		PRIMARYHOTKEY = 0,
		AUXILIARYHOTKEY,
		HELDDEVICEHOTKEYTYPECOUNT
	};

	/// An articulated device that can be weilded by an Actor.
	/// 01/31/2007 Made concrete so Shields can be jsut HeldDevice:s
	class HeldDevice : public Attachable {

		/// Public member variable, method and friend function declarations
	public:
		// Concrete allocation and cloning definitions
		EntityAllocation(HeldDevice);
		SerializableOverrideMethods;
		ClassInfoGetters;

		/// Constructor method used to instantiate a HeldDevice object in system
		/// memory. Create() should be called before using the object.
		HeldDevice();

		/// Destructor method used to clean up a HeldDevice object before deletion
		/// from system memory.
		~HeldDevice() override;

		/// Makes the HeldDevice object ready for use.
		/// @return An error return value signaling sucess or any particular failure.
		/// Anything below 0 is an error signal.
		int Create() override;

		/// Creates a HeldDevice to be identical to another, by deep copy.
		/// @param reference A reference to the HeldDevice to deep copy.
		/// @return An error return value signaling sucess or any particular failure.
		/// Anything below 0 is an error signal.
		int Create(const HeldDevice& reference);

		/// Resets the entire HeldDevice, including its inherited members, to their
		/// default settings or values.
		void Reset() override {
			Clear();
			Attachable::Reset();
			m_MOType = MovableObject::TypeHeldDevice;
		}

		/// Destroys and resets (through Clear()) the SceneLayer object.
		/// @param notInherited Whether to only destroy the members defined in this derived class, or (default: false)
		/// to destroy all inherited members also.
		void Destroy(bool notInherited = false) override;

		/// Gets the absoltue position of the top of this' HUD stack.
		/// @return A Vector with the absolute position of this' HUD stack top point.
		Vector GetAboveHUDPos() const override { return m_Pos + Vector(0, -32); }

		/// Gets the absolute position of the support handhold that this HeldDevice
		/// offers.
		/// @return A vector describing the absolute world coordinates for the support
		/// position of this HeldDevice.
		Vector GetSupportPos() const;

		/// Gets the absolute position of the magazine or other equivalent point of
		/// this.
		/// @return A vector describing the absolute world coordinates for the magazine
		/// attachment point of this
		virtual Vector GetMagazinePos() const;

		/// Gets the absolute position of the muzzle or other equivalent point of
		/// this.
		/// @return A vector describing the absolute world coordinates for the muzzle point
		/// of this
		virtual Vector GetMuzzlePos() const { return m_Pos; }

		/// Gets the unrotated relative offset from the position to the muzzle or
		/// other equivalent point of this.
		/// @return A unrotated vector describing the relative for the muzzle point of
		/// this from this' position.
		virtual Vector GetMuzzleOffset() const { return Vector(); }

		/// Sets the unrotated relative offset from the position to the muzzle or
		/// other equivalent point of this.
		/// @param newOffset Bew ofsset value.
		virtual void SetMuzzleOffset(Vector newOffset) { /* Actually does something in inherited classes */
		}

		/// Gets the current position offset of this HeldDevice's joint relative
		/// from the parent Actor's position, if attached.
		/// @return A const reference to the current stance parent offset.
		virtual Vector GetStanceOffset() const;

		/// Sets the current position offset of this HeldDevice's joint relative
		/// from the parent Actor's position, if attached.
		/// @param newValue New value.
		void SetStanceOffset(Vector newValue) { m_StanceOffset = newValue; }

		/// Sets the current position offset of this HeldDevice's joint relative
		/// from the parent Actor's position, if attached.
		/// @param New value.
		Vector GetSharpStanceOffset() const { return m_SharpStanceOffset; }

		/// Sets the current position offset of this HeldDevice's joint relative
		/// from the parent Actor's position, if attached.
		/// @param newValue New value.
		void SetSharpStanceOffset(Vector newValue) { m_SharpStanceOffset = newValue; }

		/// Gets how much farther an Actor which holds this device can see when
		/// aiming this HeldDevice sharply.
		/// @return The length in world pixel units.
		float GetSharpLength() const { return m_MaxSharpLength; }

		/// Sets how much farther an Actor which holds this device can see when
		/// aiming this HeldDevice sharply.
		/// @param newLength The length in world pixel units.
		void SetSharpLength(float newLength) { m_MaxSharpLength = newLength; }

		/// Gets whether this HeldDevice can be supported when held.
		/// @return Whether this HeldDevice can be supported when held.
		bool IsSupportable() const { return m_Supportable; }

		/// Sets whether this HeldDevice can be supported when held.
		/// @param shouldBeSupportable Whether this HeldDevice can be supported when held.
		void SetSupportable(bool shouldBeSupportable) { m_Supportable = shouldBeSupportable; }

		/// Gets whether this HeldDevice is currently supported by a second Arm.
		/// @return Whether this HeldDevice is supported or not.
		bool GetSupported() const { return m_Supportable && m_Supported; }

		/// Sets whether this HeldDevice is currently supported by a second Arm.
		/// @param supported Whether this HeldDevice is being supported.
		void SetSupported(bool supported) { m_Supported = m_Supportable && supported; }

		/// Gets whether this HeldDevice's parent has a second Arm available to provide support (or this is on a Turret).
		/// @return Whether this HeldDevice's parent has a second Arm available to provide support (or this is on a Turret).
		bool GetSupportAvailable() const { return m_Supportable && m_SupportAvailable; }

		/// Sets whether this HeldDevice's parent has a second Arm available to provide support (or this is on a Turret).
		/// @param supported Whether this HeldDevice's parent has a second Arm available to provide support (or this is on a Turret).
		void SetSupportAvailable(bool supportAvailable) { m_SupportAvailable = m_Supportable && supportAvailable; }

		/// Gets whether this HeldDevice while be held at the support offset with the off-hand when reloading.
		/// @return Whether this HeldDevice while be held at the support offset with the off-hand when reloading.
		bool GetUseSupportOffsetWhileReloading() const { return m_UseSupportOffsetWhileReloading; }

		/// Sets whether this HeldDevice while be held at the support offset with the off-hand when reloading.
		/// @param value Whether this HeldDevice while be held at the support offset with the off-hand when reloading.
		void SetUseSupportOffsetWhileReloading(bool value) { m_UseSupportOffsetWhileReloading = value; }

		/// Returns support offset.
		/// @return Support offset value.
		Vector GetSupportOffset() const { return m_SupportOffset; }

		/// Sets support offset.
		/// @param newOffset New support offset value.
		void SetSupportOffset(Vector newOffset) { m_SupportOffset = newOffset; }

		/// Gets whether this HeldDevice has any limitations on what can pick it up.
		/// @return Whether this HeldDevice has any limitations on what can pick it up.
		bool HasPickupLimitations() const { return IsUnPickupable() || !m_PickupableByPresetNames.empty(); }

		/// Gets whether this HeldDevice cannot be picked up at all.
		/// @return Whether this HeldDevice cannot be picked up at all.
		bool IsUnPickupable() const { return m_IsUnPickupable; }

		/// Sets whether this HeldDevice cannot be picked up at all.
		/// @param shouldBeUnPickupable Whether this HeldDevice cannot be picked up at all. True means it cannot, false means any other limitations will apply normally.
		void SetUnPickupable(bool shouldBeUnPickupable) { m_IsUnPickupable = shouldBeUnPickupable; }

		/// Checks whether the given Actor can pick up this HeldDevice.
		/// @param actor The Actor to check. Ownership is NOT transferred.
		/// @return Whether the given Actor can pick up this HeldDevice.
		bool IsPickupableBy(const Actor* actor) const { return !HasPickupLimitations() || m_PickupableByPresetNames.find(actor->GetPresetName()) != m_PickupableByPresetNames.end(); }

		/// Specify that objects with the given PresetName can pick up this HeldDevice.
		/// @param presetName The PresetName of an object that should be able to pick up this HeldDevice.
		void AddPickupableByPresetName(const std::string& presetName) {
			SetUnPickupable(false);
			m_PickupableByPresetNames.insert(presetName);
		}

		/// Remove allowance for objects with the given PresetName to pick up this HeldDevice.
		/// Note that if the last allowance is removed, the HeldDevice will no longer have pickup limitations, rather than setting itself as unpickupable.
		/// @param actorPresetName The PresetName of an object that should no longer be able to pick up this HeldDevice.
		void RemovePickupableByPresetName(const std::string& actorPresetName);

		/// Gets the multiplier for how well this HeldDevice can be gripped by Arms.
		/// @return The grip strength multiplier for this HeldDevice.
		float GetGripStrengthMultiplier() const { return m_GripStrengthMultiplier; }

		/// Sets the multiplier for how well this HeldDevice can be gripped by Arms.
		/// @param gripStrengthMultiplier The new grip strength multiplier for this HeldDevice.
		void SetGripStrengthMultiplier(float gripStrengthMultiplier) { m_GripStrengthMultiplier = gripStrengthMultiplier; }

		/// Gets whether this can get hit by MOs when held.
		/// @return Whether this can get hit by MOs when held.
		bool GetsHitByMOsWhenHeld() const { return m_GetsHitByMOsWhenHeld; }

		/// Sets whether this can get hit by MOs when held.
		/// @param value Whether this can get hit by MOs when held.
		void SetGetsHitByMOsWhenHeld(bool value) { m_GetsHitByMOsWhenHeld = value; }

		/// Gets whether this HeldDevice is currently being held or not.
		/// @return Whether this HeldDevice is currently being held or not.
		bool IsBeingHeld() const;

		/// Gets the visual recoil multiplier.
		/// @return A float with the scalar value.
		float GetVisualRecoilMultiplier() const { return m_VisualRecoilMultiplier; }

		/// Sets the visual recoil multiplier.
		/// @param value The new recoil multiplier scalar.
		void SetVisualRecoilMultiplier(float value) { m_VisualRecoilMultiplier = value; }

		/// Sets the degree to which this is being aimed sharp. This will
		/// affect the accuracy and what GetParentOffset returns.
		/// @param sharpAim A normalized scalar between 0 (no sharp aim) to 1.0 (best aim).
		void SetSharpAim(float sharpAim) { m_SharpAim = sharpAim; }

		/// Indicates whether this is an offensive weapon or not.
		/// @return Offensive weapon or not.
		bool IsWeapon() { return m_HeldDeviceType == WEAPON; }

		/// Indicates whether this is a tool or not.
		/// @return Tool or not.
		bool IsTool() { return m_HeldDeviceType == TOOL; }

		/// Indicates whether this is a shield or not.
		/// @return Shield or not.
		bool IsShield() { return m_HeldDeviceType == SHIELD; }

		/// Indicates whether this is a dual wieldable weapon or not.
		/// @return Dual wieldable or not.
		bool IsDualWieldable() const { return m_DualWieldable; }

		/// Sets whether this is a dual wieldable weapon or not.
		/// @param isDualWieldable Dual wieldable or not.
		void SetDualWieldable(bool isDualWieldable) { m_DualWieldable = isDualWieldable; }

		/// Indicates whether this can be held and operated effectively with one
		/// hand or not.
		/// @return One handed device or not.
		bool IsOneHanded() const { return m_OneHanded; }

		/// Sets whether this can be held and operated effectively with one
		/// hand or not.
		/// @param newValue New value.
		void SetOneHanded(bool newValue) { m_OneHanded = newValue; }

		/// Calculates the collision response when another MO's Atom collides with
		/// this MO's physical representation. The effects will be applied
		/// directly to this MO, and also represented in the passed in HitData.
		/// @param hitData Reference to the HitData struct which describes the collision. This
		/// will be modified to represent the results of the collision.
		/// @return Whether the collision has been deemed valid. If false, then disregard
		/// any impulses in the Hitdata.
		bool CollideAtPoint(HitData& hitData) override;

		/// Activates one of this HDFirearm's features. Analogous to 'pulling
		/// the trigger'.
		virtual void Activate();

		/// Deactivates one of this HDFirearm's features. Analogous to 'releasing
		/// the trigger'.
		virtual void Deactivate();

		/// Tells whether the device is currently being activated.
		/// @return Whether being activated.
		virtual bool IsActivated() const { return m_Activated; }

		/// Activates one of this HDFirearm's hotkey features.
		/// @param hotkeyType Which hotkey type to activate.
		virtual void ActivateHotkeyAction(HeldDeviceHotkeyType hotkeyType);

		/// Deactivates one of this HDFirearm's hotkey features.
		/// @param hotkeyType Which hotkey type to deactivate.
		virtual void DeactivateHotkeyAction(HeldDeviceHotkeyType hotkeyType);

		/// Tells whether a hotkey action of the device is currently being activated.
		/// @param hotkeyType Which hotkey type to check for activation.
		/// @return Whether hotkey is being activated.
		virtual bool HotkeyActionIsActivated(HeldDeviceHotkeyType hotkeyType) const { return m_HotkeyActivated[hotkeyType]; }

		/// Throws out the currently used Magazine, if any, and puts in a new one
		/// after the reload delay is up.
		virtual void Reload() {}

		/// Gets the activation Timer for this HeldDevice.
		/// @return The activation Timer for this HeldDevice.
		const Timer& GetActivationTimer() const { return m_ActivationTimer; }

		/// Tells whether the device is curtrently being reloaded.
		/// @return Whetehr being reloaded.
		virtual bool IsReloading() const { return false; }

		/// Tells whether the device just finished reloading this frame.
		/// @return Whether just done reloading this frame.
		virtual bool DoneReloading() const { return false; }

		/// Tells whether the device is curtrently in need of being reloaded.
		/// @return Whetehr in need of reloading (ie not full).
		virtual bool NeedsReloading() const { return false; }

		/// Tells whether the device is curtrently full and reloading won't have
		/// any effect.
		/// @return Whetehr magazine is full or not.
		virtual bool IsFull() const { return true; }

		/// Tells whether this HeldDevice is currently empty of ammo.
		/// @return Whether this HeldDevice is empty.
		virtual bool IsEmpty() const { return false; }

		/// Updates this MovableObject. Supposed to be done every frame.
#pragma region Melee
		/// Gets whether this has a blade (BladeStart and BladeEnd apart): it cuts what it's swept through, by how fast the blade moves, as a melee weapon.
		bool HasBlade() const { return m_BladeEnd != m_BladeStart; }

		/// Gets whether this' blade is an energy blade (a lightsaber): massless, it burns through what it touches even held still, cuts terrain, glows and lights its surroundings.
		bool IsBladeEnergy() const { return m_BladeEnergy; }

		/// Sets whether this' blade is an energy blade.
		void SetBladeEnergy(bool energy) { m_BladeEnergy = energy; }

		/// Gets the blade's base, relative to this' position, unflipped and unrotated like MuzzleOffset.
		Vector GetBladeStart() const { return m_BladeStart; }

		/// Sets the blade's base.
		void SetBladeStart(const Vector& start) { m_BladeStart = start; }

		/// Gets the blade's tip, relative to this' position, unflipped and unrotated like MuzzleOffset.
		Vector GetBladeEnd() const { return m_BladeEnd; }

		/// Sets the blade's tip.
		void SetBladeEnd(const Vector& end) { m_BladeEnd = end; }

		/// Gets the length in the middle of a double-bladed staff's blade where the hilt is and there's no blade, in pixels. 0 for a blade with one end.
		float GetBladeHiltGap() const { return m_BladeHiltGap; }

		/// Sets the length in the middle of the blade where the hilt is, making it a double-bladed staff: BladeStart and BladeEnd are then its two tips.
		void SetBladeHiltGap(float gap) { m_BladeHiltGap = std::max(gap, 0.0F); }

		/// Gets the scene position of the blade's base, as it is now (an energy blade's grows from it as it ignites). A double-bladed staff's other tip,
		/// short of BladeStart while it ignites.
		Vector GetBladeStartPos() const { return m_Pos + RotateOffset(m_BladeHiltGap > 0.0F ? GetBladeMiddle() - GetBladeHalf() * GetBladeReach() : m_BladeStart); }

		/// Gets the scene position of the blade's tip, as it is now: short of BladeEnd while an energy blade ignites or goes out.
		Vector GetBladeEndPos() const { return m_Pos + RotateOffset(m_BladeHiltGap > 0.0F ? GetBladeMiddle() + GetBladeHalf() * GetBladeReach() : m_BladeStart + (m_BladeEnd - m_BladeStart) * m_BladeExtension); }

		/// Gets the blade's colour: an energy blade's glow and light.
		Color GetBladeColor() const { return m_BladeColor; }

		/// Sets the blade's colour.
		void SetBladeColor(const Color& color) { m_BladeColor = color; }

		/// Gets how far an energy blade's light reaches, in pixels. 0 for none.
		float GetBladeLightRadius() const { return m_BladeLightRadius >= 0.0F ? m_BladeLightRadius : (m_BladeEnergy ? 60.0F : 0.0F); }

		/// Sets how far an energy blade's light reaches, in pixels.
		void SetBladeLightRadius(float radius) { m_BladeLightRadius = radius; }

		/// Gets how bright an energy blade's glow and light are, 1 for a lightsaber.
		float GetBladeBrightness() const { return m_BladeBrightness; }

		/// Sets how bright an energy blade's glow and light are.
		void SetBladeBrightness(float brightness) { m_BladeBrightness = brightness; }

		/// Gets whether the blade is lit (an energy blade) or drawn (any): an energy blade that isn't neither glows nor cuts.
		bool IsBladeLit() const { return m_BladeLit; }

		/// Lights or puts out an energy blade. It grows or shrinks over BladeIgniteTime.
		void SetBladeLit(bool lit) { m_BladeLit = lit; }

		/// Gets how far out an energy blade is, 0 (out) to 1 (fully lit).
		float GetBladeExtension() const { return m_BladeExtension; }

		/// Gets how sharp the blade is: how well its cuts get through armour, like a particle's Sharpness.
		float GetBladeSharpness() const { return m_BladeSharpness >= 0.0F ? m_BladeSharpness : (m_BladeEnergy ? 80.0F : 8.0F); }

		/// Sets how sharp the blade is.
		void SetBladeSharpness(float sharpness) { m_BladeSharpness = sharpness; }

		/// Gets the mass the blade strikes with, in kg: with the blade's speed, how hard its cuts and blows land.
		float GetBladeMass() const { return m_BladeMass >= 0.0F ? m_BladeMass : (m_BladeEnergy ? 1.0F : std::max(0.2F, m_Mass * 0.5F)); }

		/// Sets the mass the blade strikes with.
		void SetBladeMass(float mass) { m_BladeMass = mass; }

		/// Gets how strong a terrain material the blade cuts through (its structural integrity), 0 for none.
		float GetBladeCutsTerrain() const { return m_BladeCutsTerrain >= 0.0F ? m_BladeCutsTerrain : (m_BladeEnergy ? 70.0F : 0.0F); }

		/// Sets how strong a terrain material the blade cuts through.
		void SetBladeCutsTerrain(float strength) { m_BladeCutsTerrain = strength; }

		/// Gets the arc, in degrees, the engine swings this through when it's activated (0: no swing; the blade still cuts as it's moved, by a script or the arm).
		float GetMeleeSwingArc() const { return m_MeleeSwingArc; }

		/// Sets the arc the engine swings this through when it's activated.
		void SetMeleeSwingArc(float arc) { m_MeleeSwingArc = arc; }

		/// Gets the angle the engine's swing has this turned by now, in radians, for the holding arm.
		float GetMeleeSwingAngle() const { return m_MeleeSwingAngle; }

		/// Gets whether a swing is under way.
		bool IsSwinging() const { return m_MeleeSwingPhase != MeleeSwingPhase::Idle; }

		/// Starts a swing now, if this has a swing and isn't swinging already. Activating it does the same.
		void StartMeleeSwing();

		/// Gets whether the blade struck another blade in the last moment (a parry), for scripts.
		bool BladeJustClashed() const { return m_BladeClashTimer.GetElapsedSimTimeMS() < 120.0; }

		/// Gets the time since the blade last cut something, in sim ms.
		double GetTimeSinceBladeHit() const { return m_BladeHitTimer.GetElapsedSimTimeMS(); }

		/// Moves the blade on for this sim update after everything has moved: sweeps it from where it was to where it is now, cutting what it went
		/// through, clashing with other blades, deflecting shots, cutting terrain, and drawing and lighting an energy blade.
		void PostUpdate() override;
#pragma endregion

		void Update() override;

		/// Draws this HeldDevice's current graphical representation to a
		/// BITMAP of choice.
		/// @param pTargetBitmap A pointer to a BITMAP to draw on.
		/// @param targetPos The absolute position of the target bitmap's upper left corner in the Scene. (default: Vector())
		/// @param mode In which mode to draw in. See the DrawMode enumeration for the modes. (default: g_DrawColor)
		/// @param onlyPhysical Whether to not draw any extra 'ghost' items of this MovableObject, (default: false)
		/// indicator arrows or hovering HUD text and so on.
		void Draw(BITMAP* pTargetBitmap, const Vector& targetPos = Vector(), DrawMode mode = g_DrawColor, bool onlyPhysical = false) const override;
		using Attachable::Draw;

		/// Draws this' current graphical HUD overlay representation to a
		/// BITMAP of choice.
		/// @param pTargetBitmap A pointer to a BITMAP to draw on.
		/// @param targetPos The absolute position of the target bitmap's upper left corner in the Scene. (default: Vector())
		/// @param whichScreen Which player's screen this is being drawn to. May affect what HUD elements (default: 0)
		/// get drawn etc.
		void DrawHUD(BITMAP* pTargetBitmap, const Vector& targetPos = Vector(), int whichScreen = 0, bool playerControlled = false) override;
		void DrawHUD(const Camera& camera) override;

		/// Gets how metallic and glossy this looks when it hasn't been set for it. Guns, tools and shields are steel whoever is holding them, unless their INI says otherwise.
		/// @param metalness Filled in with how metallic, 0 to 1.
		/// @param gloss Filled in with how glossy, 0 to 1.
		void GetDefaultSurface(float& metalness, float& gloss) const override;

		/// Resest all the timers used by this. Can be emitters, etc. This is to prevent backed up emissions to come out all at once while this has been held dormant in an inventory.
		void ResetAllTimers() override {
			Attachable::ResetAllTimers();
			m_ActivationTimer.Reset();
		}

#pragma region Force Transferral
		/// Bundles up all the accumulated impulse forces of this HeldDevice and calculates how they transfer to the joint, and therefore to the parent.
		/// If the accumulated impulse forces exceed the joint strength or gib impulse limit of this HeldDevice, the jointImpulses Vector will be filled up to that limit and false will be returned.
		/// Additionally, in this case, the HeldDevice will remove itself from its parent, destabilizing said parent if it's an Actor, and gib itself if appropriate.
		/// @param jointImpulses A vector that will have the impulse forces affecting the joint ADDED to it.
		/// @param jointStiffnessValueToUse An optional override for the HeldDevice's joint stiffness for this function call. Primarily used to allow subclasses to perform special behavior.
		/// @param jointStrengthValueToUse An optional override for the HeldDevice's joint strength for this function call. Primarily used to allow subclasses to perform special behavior.
		/// @param gibImpulseLimitValueToUse An optional override for the HeldDevice's gib impulse limit for this function call. Primarily used to allow subclasses to perform special behavior.
		/// @return False if the HeldDevice has no parent or its accumulated forces are greater than its joint strength or gib impulse limit, otherwise true.
		bool TransferJointImpulses(Vector& jointImpulses, float jointStiffnessValueToUse = -1, float jointStrengthValueToUse = -1, float gibImpulseLimitValueToUse = -1) override;
#pragma endregion

		/// Protected member variable and method declarations
	protected:
		// Member variables
		static Entity::ClassInfo m_sClass;
		// Indicates what kind of held device this is, see the HeldDeviceType enum
		int m_HeldDeviceType;
		// Is this HeldDevice that are currently activated?
		bool m_Activated;
		// An array that holds activation states for the various hotkey actions of this HeldDevice.
		std::array<bool, HELDDEVICEHOTKEYTYPECOUNT> m_HotkeyActivated;
		// Timer for timing how long a feature has been activated.
		Timer m_ActivationTimer;
		// An array that holds activation timers for the various hotkey actions of this HeldDevice.
		std::array<Timer, HELDDEVICEHOTKEYTYPECOUNT> m_HotkeyActivationTimer;
		// Can be weilded well with one hand or not
		bool m_OneHanded;
		// Can be weilded with bg hand or not
		bool m_DualWieldable;
		// Position offset from the parent's own position to this HeldDevice's joint, which
		// defines the normal stance that an arm that is holding this device should have.
		Vector m_StanceOffset;
		// The alternative parent offset stance that is used when the device is carefully aimed.
		Vector m_SharpStanceOffset;
		// The point at which the other arm of the holder can support this HeldDevice.
		// Relative to the m_Pos. This is like a seconday handle position.
		Vector m_SupportOffset;
		// Whether the actor using this gun should keep hold of the support offset when reloading, instead of using their ReloadOffset/HolsterOffset
		bool m_UseSupportOffsetWhileReloading;
		// The degree as to this is being aimed carefully. 0 means no sharp aim, and 1.0 means best aim.
		float m_SharpAim;
		// How much farther the player can see when aiming this sharply.
		float m_MaxSharpLength;
		bool m_Supportable; //!< Whether or not this HeldDevice can be supported.
		bool m_Supported; //!< Whether or not this HeldDevice is currently being supported by another Arm.
		bool m_SupportAvailable; //!< Whether or not this HeldDevice's parent has a second Arm available to provide support (or this is on a Turret).
		bool m_IsUnPickupable; //!< Whether or not this HeldDevice should be able to be picked up at all.
		// TODO: move this smelly thing elsewhere
		std::array<bool, Players::MaxPlayerCount> m_SeenByPlayer; //!< An array of players that can currently see the pickup HUD of this HeldDevice.
		std::unordered_set<std::string> m_PickupableByPresetNames; //!< The unordered set of PresetNames that can pick up this HeldDevice if it's dropped. An empty set means there are no PresetName limitations.
		float m_GripStrengthMultiplier; //!< The multiplier for how well this HeldDevice can be gripped by Arms.
		// Blink timer for the icon
		Timer m_BlinkTimer;
		// How loud this device is when activated. 0 means perfectly quiet 0.5 means half of normal (normal equals audiable from ~half a screen)
		float m_Loudness;
		// If this weapon belongs to the "Explosive Weapons" group or not
		bool m_IsExplosiveWeapon;
		// If this device can be hit by MOs whenever it's held
		bool m_GetsHitByMOsWhenHeld;
		/// The multiplier for visual recoil
		float m_VisualRecoilMultiplier;

		/// Where the engine's swing is (see m_MeleeSwingArc).
		enum class MeleeSwingPhase : unsigned char {
			Idle,
			WindUp, //!< Drawing back.
			Strike, //!< Coming through the arc.
			Recover //!< Going back to the stance, after the strike or after bouncing off something hard.
		};

		/// A double-bladed staff's middle, and the way from it to BladeEnd, unflipped and unrotated.
		Vector GetBladeMiddle() const { return (m_BladeStart + m_BladeEnd) * 0.5F; }
		Vector GetBladeHalf() const { return (m_BladeEnd - m_BladeStart) * 0.5F; }

		/// How far out a double-bladed staff's blades reach now, as a fraction of the half length: from the hilt's end when out to the tips when lit.
		float GetBladeReach() const {
			float half = GetBladeHalf().GetMagnitude();
			float hilt = half > 0.0F ? std::min(m_BladeHiltGap * 0.5F / half, 1.0F) : 0.0F;
			return hilt + (1.0F - hilt) * m_BladeExtension;
		}

		Vector m_BladeStart; //!< The blade's base, relative to this' position, unflipped and unrotated.
		Vector m_BladeEnd; //!< The blade's tip. The same as m_BladeStart for no blade.
		float m_BladeHiltGap; //!< A double-bladed staff's hilt length in the middle of the blade, pixels. 0 for a blade with one end.
		bool m_BladeEnergy; //!< An energy blade (lightsaber): see IsBladeEnergy.
		Color m_BladeColor; //!< An energy blade's glow and light colour.
		float m_BladeWidth; //!< An energy blade's core width, in pixels.
		float m_BladeBrightness; //!< An energy blade's glow and light brightness.
		float m_BladeLightRadius; //!< How far an energy blade's light reaches, pixels. Below 0 until set: 60 for an energy blade.
		float m_BladeSharpness; //!< Like a particle's Sharpness, for the blade's cuts. Below 0 until set.
		float m_BladeMass; //!< The mass the blade strikes with, kg. Below 0 until set: an energy blade's 1, a physical blade's half of this' mass.
		float m_BladeMinSpeed; //!< The least speed a cut lands with, m/s: an energy blade burns through what it's held against. Below 0 until set.
		float m_BladeHitInterval; //!< The least time between two cuts on the same thing, sim ms.
		float m_BladeCutsTerrain; //!< The strongest terrain material the blade cuts through while attacking. Below 0 until set: 70 for an energy blade, none for a physical one.
		bool m_BladeSevers; //!< Fast cuts through limbs take them off. An energy blade's by default.
		int m_BladeSeversSet; //!< -1 until BladeSevers is read, so the default can follow BladeEnergy.
		bool m_BladeDeflects; //!< Shots that cross the blade bounce off it. An energy blade's by default.
		int m_BladeDeflectsSet; //!< -1 until BladeDeflects is read.
		float m_BladeIgniteTime; //!< How long an energy blade takes to grow or shrink, sim ms.
		bool m_BladeLit; //!< See IsBladeLit.
		float m_BladeExtension; //!< See GetBladeExtension.
		SoundContainer* m_BladeHitSound; //!< Played where the blade cuts something.
		SoundContainer* m_BladeClashSound; //!< Played where the blade strikes another blade, or bounces off something it can't cut.
		SoundContainer* m_BladeSwingSound; //!< Played as a swing comes through.
		SoundContainer* m_BladeHumSound; //!< Looped while an energy blade is lit and held.
		SoundContainer* m_BladeIgniteSound; //!< Played as an energy blade lights.
		float m_MeleeSwingArc; //!< The arc the engine swings this through on activation, degrees. 0 for none.
		float m_MeleeSwingTime; //!< How long the wind-up and strike take, sim ms.
		float m_MeleeRecoverTime; //!< How long getting back to the stance takes, sim ms.
		MeleeSwingPhase m_MeleeSwingPhase; //!< Where the swing is.
		float m_MeleeSwingAngle; //!< The angle the swing has this turned by now, radians, positive up when facing right.
		float m_MeleeSwingRecoverFrom; //!< The angle the recovery started from.
		Timer m_MeleeSwingTimer; //!< Times the swing's current phase.
		bool m_BladePreviousValid; //!< Whether m_BladePrevious... hold the blade's place last sim update.
		Vector m_BladePreviousStart; //!< The blade's base in the scene last sim update.
		Vector m_BladePreviousEnd; //!< The blade's tip in the scene last sim update.
		long m_BladePreviousRootID; //!< The unique ID of what held this last sim update: a blade that changes hands, or is dropped, doesn't sweep from where it was.
		std::unordered_map<long, double> m_BladeLastCut; //!< When the blade last cut each thing (by its root's unique ID), sim ms.
		Timer m_BladeClashTimer; //!< Since the blade last clashed with another.
		Timer m_BladeHitTimer; //!< Since the blade last cut something.
		Timer m_BladeTerrainTimer; //!< Since the blade last bounced off or melted terrain, for the effects' pace.

		/// Sets this' parent, and when it's let go of (dropped, or put away), puts an energy blade out and stops its hum, and ends a swing.
		/// @param newParent The new parent, nullptr for none. Ownership is NOT transferred!
		void SetParent(MOSRotating* newParent) override;

		/// Private member variable and method declarations
	private:
		/// Clears all the member variables of this HeldDevice, effectively
		/// resetting the members of this abstraction level only.
		void Clear();

		/// The blade properties left unset (below 0) follow whether it's an energy or a physical blade.
		float GetBladeMinSpeed() const { return m_BladeMinSpeed >= 0.0F ? m_BladeMinSpeed : (m_BladeEnergy ? 18.0F : 0.0F); }
		float GetBladeHitInterval() const { return m_BladeHitInterval >= 0.0F ? m_BladeHitInterval : (m_BladeEnergy ? 90.0F : 160.0F); }
		float GetBladeIgniteTime() const { return m_BladeIgniteTime >= 0.0F ? m_BladeIgniteTime : (m_BladeEnergy ? 250.0F : 0.0F); }
		bool BladeSevers() const { return m_BladeSeversSet >= 0 ? m_BladeSevers : m_BladeEnergy; }
		bool BladeDeflects() const { return m_BladeDeflectsSet >= 0 ? m_BladeDeflects : m_BladeEnergy; }

		/// Moves the engine's swing on, and lights or puts out an energy blade. Done in Update.
		void UpdateMeleeSwingAndBlade();

		/// Cuts what the blade went through: sends a heavy, sharp particle into it at the blade's speed, so it's hurt, wounded and knocked like by any hit.
		/// @param hitMOID What the blade struck.
		/// @param hitPos Where.
		/// @param bladeVel How fast that part of the blade was moving, m/s.
		/// @param holder What holds this (this, when dropped).
		/// @param attacking Whether this is being swung or used, which lets an energy blade sever limbs.
		/// @return Whether it was cut (not cut again too soon, nor moving too slowly to cut).
		bool CutWithBlade(MOID hitMOID, const Vector& hitPos, const Vector& bladeVel, MovableObject* holder, bool attacking);

		/// The blade strikes something it doesn't cut through: another blade, or hard ground. Sparks, sound and light, a knock back through the arm, and a swing cut short.
		/// @param where Where.
		/// @param push The impulse on this, Ns.
		/// @param clash Whether it was another blade.
		void BladeStruck(const Vector& where, const Vector& push, bool clash, bool metal = true);

		/// Bounces shots off the blade that crossed it this sim update or will next.
		void DeflectShots(const Vector& start, const Vector& end, MovableObject* holder);

		// Disallow the use of some implicit methods.
		HeldDevice(const HeldDevice& reference) = delete;
		HeldDevice& operator=(const HeldDevice& rhs) = delete;
	};

} // namespace RTE
