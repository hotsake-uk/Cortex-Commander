#pragma once

#include "Color.h"
#include "Serializable.h"

#include <string>

namespace RTE {

	/// A timed change to a unit (a buff or a curse, SE-1): what a spell such as Haste, Stoneskin or Petrify leaves on whoever it is cast on,
	/// or what a creature has about it for good (a troll's regeneration). Each field left alone changes nothing, so an effect says only what
	/// it does. One of a name at a time: casting the same effect again refreshes it. Set from INI (an Actor's PassiveEffect), from Lua
	/// (StatusEffect("Haste"), then actor:AddStatusEffect(effect, seconds)) or from C++; Actor runs them (Actor::UpdateStatusEffects).
	class StatusEffect : public Serializable {

	public:
		SerializableClassNameGetter;
		SerializableOverrideMethods;

		StatusEffect() = default;
		explicit StatusEffect(const std::string& name) :
		    Name(name) {}

		std::string Name; //!< What it is called, and what tells one effect from another: the same name refreshes rather than stacks.
		std::string Label; //!< What the HUD shows over the unit; the name when empty.
		bool Harmful = false; //!< A curse rather than a blessing: Dispel's "remove the bad" takes it, and the HUD shows it in a warning colour.
		float SpeedMultiplier = 1.0F; //!< How fast it walks and runs, as a share of its pace (1 unchanged).
		float DamageTakenMultiplier = 1.0F; //!< How much of the harm it takes reaches its health (0.5 half, 2 double).
		float DamageDealtMultiplier = 1.0F; //!< How much harder what it fires, throws and cuts with hurts.
		float HealthPerSecond = 0.0F; //!< Health it gains (or loses, below zero) a second; a gain also closes a wound now and then.
		float EnergyPerSecond = 0.0F; //!< Energy it gains (or loses) a second, for a caster.
		bool FireProof = false; //!< Fire doesn't catch on it meanwhile, and what is burning on it goes out.
		bool Held = false; //!< Held fast: it can't walk, leap, jump, fly or fire (petrified, frozen solid).
		bool Silenced = false; //!< It can't cast spells (its spells count as unaffordable).
		std::string Shader; //!< A Shader preset it is drawn with meanwhile; the highest Priority of the active effects' shaders wins.
		int Priority = 0; //!< Which effect's shader is drawn when several have one; higher wins, later wins a tie.
		Color LightColor = Color(0, 0, 0); //!< A light it gives off meanwhile, when LightRadius is above 0.
		float LightRadius = 0.0F;
		float LightIntensity = 0.5F;
		float Seconds = 0.0F; //!< INI only: how long a PassiveEffect lasts once added, 0 for as long as the unit lives.

		/// The sim time, ms, it lasts until; 0 for as long as the unit lives. Runtime only.
		long long Until = 0;

		/// Whether this is still running at a sim time.
		bool IsActiveAt(long long nowMS) const { return Until == 0 || Until > nowMS; }

		/// What the HUD shows for it.
		const std::string& GetLabel() const { return Label.empty() ? Name : Label; }

	private:
		static const std::string c_ClassName; //!< A string with the friendly-formatted type name of this.
	};
} // namespace RTE
