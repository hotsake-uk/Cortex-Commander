#pragma once

#include "ACrab.h"

namespace RTE {

	class MechLeg;

	/// A mech (MK-1): a big walking machine on two legs, an Imperial Knight first. It is a crab underneath (ACrab), so it is selected, ordered,
	/// routed and fought with as a crab is: the turret's mounted weapons, the crab's AI and route follower, its jetpack and leap if it has them.
	/// What is its own is the walking: two legs of thigh, shin and foot (MechLeg), one foot always planted while the other steps to a spot
	/// found on the ground ahead, the hull carried over the feet, a slow turn on the spot, and heavy footfalls that shake the ground and
	/// stamp on small enemies underfoot. It can brace (plant its feet and hold steady) and breach (kick through doors and thin walls in its way).
	/// Its weapons are mech-only items (HeldDevice::IsMechOnly): nothing else can pick them up, and it uses nothing else.
	class AMech : public ACrab {
		friend struct EntityLuaBindings;

	public:
		EntityAllocation(AMech);
		SerializableOverrideMethods;
		ClassInfoGetters;
		DefaultPieMenuNameGetter("Default Crab Pie Menu");

#pragma region Creation
		AMech();

		~AMech() override;

		int Create() override;

		int Create(const AMech& reference);

		void Reset() override {
			Clear();
			ACrab::Reset();
		}

		void Destroy(bool notInherited = false) override;
#pragma endregion

#pragma region Getters and Setters
		/// The near leg. Ownership is NOT transferred!
		MechLeg* GetFGLeg() const { return m_FGLeg; }
		/// Sets the near leg. Ownership IS transferred!
		void SetFGLeg(MechLeg* newLeg);

		/// The far leg. Ownership is NOT transferred!
		MechLeg* GetBGLeg() const { return m_BGLeg; }
		/// Sets the far leg. Ownership IS transferred!
		void SetBGLeg(MechLeg* newLeg);

		/// How fast it walks, in m/s.
		float GetWalkSpeed() const { return m_WalkSpeed; }
		void SetWalkSpeed(float speed) { m_WalkSpeed = std::max(speed, 0.0F); }

		/// How long a stride is, in pixels: how far a foot steps ahead of the hip.
		float GetStrideLength() const;

		/// The highest ledge a step goes up, in pixels.
		float GetMaxStepUp() const;

		/// The deepest drop a step goes down, in pixels.
		float GetMaxStepDown() const;

		/// How tall it stands, from the soles to the top of the hull, in pixels.
		float GetStandingHeight() const;

		/// Whether it is braced (MK-3): feet planted, it won't walk, holds its aim steady and is hardly pushed.
		bool IsBraced() const { return m_Braced; }
		/// Braces or lets go.
		void SetBraced(bool braced);

		/// The strongest door or wall it kicks through (MK-3), as a material's strength; 0 for none.
		float GetBreachStrength() const { return m_BreachStrength; }
		void SetBreachStrength(float strength) { m_BreachStrength = std::max(strength, 0.0F); }

		/// Whether it is turning round on the spot.
		bool IsTurning() const { return m_TurnProgress >= 0.0F; }

		/// Whether it stands on its feet now (one or both planted on the ground), rather than falling, jetting or lying down.
		bool IsStanding() const { return !m_Airborne; }

		/// Whether the walk is held up by something it can't step over or kick through.
		bool IsBlocked() const { return m_Blocked; }

		/// Where a foot is, in the scene (0 the near foot, 1 the far one), and whether it is planted. For the debug overlay.
		Vector GetFootPos(int foot) const { return m_Feet[foot & 1].Pos; }
		bool IsFootPlanted(int foot) const { return m_Feet[foot & 1].Planted; }
		/// Where the stepping foot is going, and whether one is stepping.
		bool IsStepping() const { return m_Feet[0].Swinging || m_Feet[1].Swinging; }
		Vector GetStepTarget() const { return m_Feet[0].Swinging ? m_Feet[0].To : m_Feet[1].To; }
#pragma endregion

#pragma region Override Methods
		void PreControllerUpdate() override;

		void Update() override;

		void Draw(BITMAP* pTargetBitmap, const Vector& targetPos = Vector(), DrawMode mode = g_DrawColor, bool onlyPhysical = false) const override;

		/// What a mech is to the path grid: as tall as it stands and wide, walking up stairs and steps up to its stride's rise, never crawling,
		/// climbing ladders or digging, through doors it can kick in, and over lava.
		PathAgent GetPathAgent() const override;

		float EstimateBreachStrength() const override { return m_BreachStrength; }
#pragma endregion

	protected:
		static Entity::ClassInfo m_sClass;

		bool HasLegsLeft() const override;
		void OnFlippedByAim() override;
		bool MountedDeviceMayFire(const HeldDevice* device) const override;

	private:
		/// One foot's walk.
		struct Foot {
			Vector Pos; //!< Where the ankle is.
			Vector From; //!< Where a step started.
			Vector To; //!< Where a step lands.
			float Progress = 0.0F; //!< How far through the step, 0 to 1.
			bool Swinging = false; //!< Stepping.
			bool Planted = false; //!< On the ground.
			float Angle = 0.0F; //!< The ground's slope under it, radians.
		};

		MechLeg* m_FGLeg; //!< The near leg.
		MechLeg* m_BGLeg; //!< The far leg.
		Foot m_Feet[2]; //!< The near foot and the far one.

		// Tuning (INI).
		float m_WalkSpeed; //!< How fast it walks, m/s.
		float m_StrideLength; //!< How far a foot steps ahead of the hip, px; below zero for three quarters of the leg's reach.
		float m_StepTime; //!< How long a step takes, s.
		float m_StepLift; //!< How high a foot lifts in a step, px; below zero for a sixth of the reach.
		float m_StandExtension; //!< How straight the legs stand, as a share of their reach.
		float m_MaxStepUp; //!< The highest step up, px; below zero for two fifths of the reach.
		float m_MaxStepDown; //!< The deepest step down, px; below zero for nine tenths of the reach.
		float m_TurnTime; //!< How long a turn on the spot takes, s.
		float m_BodyBob; //!< How far the hull rises at the middle of each step, px.
		float m_FootfallShake; //!< How hard each footfall shakes the screen near it.
		float m_StompDamage; //!< The share of its health a small enemy loses to a foot landing on it.
		float m_StompMassRatio; //!< Only things lighter than this share of its mass are stamped on; heavier ones it steps beside.
		float m_BreachStrength; //!< The strongest door or wall it kicks through (see GetBreachStrength).

		// State.
		bool m_Airborne; //!< No foot planted: falling, jetting, leaping or lying down.
		bool m_Blocked; //!< The walk is held up (see IsBlocked).
		float m_TurnProgress; //!< How far through a turn on the spot, 0 to 1; below zero when not turning.
		bool m_Braced; //!< See IsBraced.
		Timer m_KickTimer; //!< Since its last kick at a door or wall.
		int m_WalkDirection; //!< The way it wants to walk this update: 1 right, -1 left, 0 not at all.

		/// The leg, 0 near or 1 far, if it is still on.
		MechLeg* LegAt(int foot) const;

		/// Whether the leg, 0 near or 1 far, is still on.
		bool HasLeg(int foot) const { return LegAt(foot) != nullptr; }

		/// The longest leg's reach, px.
		float GetLegReach() const;

		/// The way it wants to walk, from its controls, and the turn on the spot when that's behind it. Sets m_WalkDirection and the movement state.
		void UpdateWalkIntent();

		/// The legs and the hull over them (see the class description). Each update, after the controls.
		void UpdateGait(float deltaTime);

		/// The feet hang when nothing holds it up (falling, jetting, knocked down), and are put down again when the ground is under them.
		void UpdateAirborne();

		/// Starts a step of a foot to land near an x: on the ground there, found from the planted foot's level within the steps it takes.
		/// @return Whether it can step there (false for a wall too high, or no ground in reach).
		bool StartStep(int foot, float targetX);

		/// A foot coming down at the end of its step: planted, the ground shaken, what's under it stamped on.
		void Footfall(int foot);

		/// Holds the hull at standing height over the planted feet and walks it along.
		void CarryHull(float deltaTime);

		/// Kicks at the door or wall in its way (MK-3): an enemy's door broken, a wall it is strong enough for knocked through, a kick at a time.
		void Kick(float wallX, float floorY);

		/// The ground's surface under a point, looking down from it: the first solid pixel's y, or -1 for none within the depth.
		static float GroundUnder(float x, float fromY, float depth);

		void Clear();

		// Disallow the use of some implicit methods.
		AMech(const AMech& reference) = delete;
		AMech& operator=(const AMech& rhs) = delete;
	};

} // namespace RTE
