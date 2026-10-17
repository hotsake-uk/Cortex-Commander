#pragma once

#include "Attachable.h"

namespace RTE {

	/// A mech's leg (AMech): a thigh (this, from the hip down to the knee), a shin hung from its knee and a foot hung from the shin's ankle.
	/// Its owner says where the ankle should be each update (SetAnkleTarget), and the two bones are turned to reach it, the knee bent to the
	/// front (two-bone inverse kinematics). A soldier's or crab's Leg is one picture stretched between hip and foot, which can't bend a knee.
	class MechLeg : public Attachable {

	public:
		EntityAllocation(MechLeg);
		SerializableOverrideMethods;
		ClassInfoGetters;

#pragma region Creation
		MechLeg();

		int Create() override;

		int Create(const MechLeg& reference);
#pragma endregion

#pragma region Destruction
		~MechLeg() override;

		void Destroy(bool notInherited = false) override {
			if (!notInherited) {
				Attachable::Destroy();
			}
			Clear();
		}

		void Reset() override {
			Clear();
			Attachable::Reset();
		}
#pragma endregion

#pragma region Getters and Setters
		/// The shin, hung from the knee. Ownership is NOT transferred!
		Attachable* GetShin() const { return m_Shin; }
		/// Sets the shin. Ownership IS transferred!
		void SetShin(Attachable* newShin);

		/// The foot, hung from the shin's ankle. Ownership is NOT transferred!
		Attachable* GetFoot() const { return m_Foot; }
		/// Sets the foot. Ownership IS transferred!
		void SetFoot(Attachable* newFoot);

		/// How long the thigh is, hip to knee, in pixels.
		float GetThighLength() const { return m_KneeOffset.GetMagnitude(); }

		/// How long the shin is, knee to ankle, in pixels.
		float GetShinLength() const { return m_AnkleOffset.GetMagnitude(); }

		/// How far the ankle reaches from the hip with the leg straight, in pixels.
		float GetReach() const { return GetThighLength() + GetShinLength(); }

		/// How far under the ankle the sole of the foot is, in pixels: where it stands on the ground.
		float GetFootDrop() const { return m_FootDrop; }

		/// Where the ankle should be, in the scene. The bones are turned to reach for it each update.
		void SetAnkleTarget(const Vector& target) { m_AnkleTarget = target; m_HasTarget = true; }

		/// Where the ankle is aimed at.
		const Vector& GetAnkleTarget() const { return m_AnkleTarget; }

		/// The angle the foot is turned to, in radians (0 flat), to lie on a slope.
		void SetFootAngle(float angle) { m_FootAngle = angle; }

		/// Where the knee is in the scene, as last worked out.
		const Vector& GetKneePos() const { return m_KneePos; }

		/// Where the ankle is in the scene, as last worked out: the target, or as near as the leg reaches.
		const Vector& GetAnklePos() const { return m_AnklePos; }

		/// Where the hip is in the scene (the thigh's joint).
		Vector GetHipPos() const;
#pragma endregion

#pragma region Override Methods
		void Update() override;
#pragma endregion

	private:
		/// Bends the bones as drawn to reach the ankle from the hip moved with the body's springs (UnitSprings): the thigh and shin each nudged
		/// and turned so the knee bends as the hull dips, the foot left where it stands. Drawing only.
		void UpdateDrawBones();

	protected:
		static Entity::ClassInfo m_sClass; //!< ClassInfo for this class.

		Attachable* m_Shin; //!< The shin, hung from the knee.
		Attachable* m_Foot; //!< The foot, hung from the ankle.

		Vector m_KneeOffset; //!< From the hip (the joint) to the knee in the thigh's picture, unflipped and unrotated.
		Vector m_AnkleOffset; //!< From the knee (the shin's joint) to the ankle in the shin's picture, unflipped and unrotated.
		float m_FootDrop; //!< How far under the ankle the sole of the foot is, in pixels.

		Vector m_AnkleTarget; //!< Where the ankle should be.
		bool m_HasTarget; //!< Whether it has been given one (else the leg hangs straight down).
		float m_FootAngle; //!< The angle the foot lies at.
		Vector m_KneePos; //!< Where the knee is, as last worked out.
		Vector m_AnklePos; //!< Where the ankle is, as last worked out.

	private:
		/// Works out where the knee and ankle go for the target and turns the thigh, the shin and the foot to them.
		void SolveBones();

		/// Hangs the shin from the knee and the foot from the ankle, by the offsets (once they and the joint offset are known).
		void HangBones();

		void Clear();

		// Disallow the use of some implicit methods.
		MechLeg(const MechLeg& reference) = delete;
		MechLeg& operator=(const MechLeg& rhs) = delete;
	};
} // namespace RTE
