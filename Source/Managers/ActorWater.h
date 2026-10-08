#pragma once

namespace RTE {
	class Actor;

	/// Units in liquid: they wade slowly, light ones float and heavy ones sink, flesh and blood units run out of air with their heads under, and acid eats at anything standing in it.
	/// Lava is ActorFire's business. Part of the simulation and deterministic: fixed sim steps, units handled in list order, no random numbers.
	class ActorWater {

	public:
		/// Gets whether units swim, float and drown (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether units swim, float and drown.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Works out how deep each unit is in liquid and applies drag, buoyancy, drowning and acid. Call once per sim update, from the main thread, after the liquids update.
		static void Update();

		/// Gets how deep a unit is in liquid: 0 dry, 1 feet in, 2 body in, 3 head under.
		static int GetDepth(const Actor* actor);

		/// Gets how fast a unit walks compared to normal, slower the deeper it wades.
		static float GetWalkSpeedMultiplier(const Actor* actor);

		/// Gets how much air a unit has left, from 1 (full, or it doesn't breathe) to 0 (drowning).
		static float GetAir(const Actor* actor);

		/// Gets how many seconds of air a unit holds when its head goes under: 12 for flesh and blood, forever (FLT_MAX) for what doesn't breathe.
		static float GetBreathSeconds(const Actor* actor);

		/// Gets how hard liquid pushes a unit up against its weight: over 1 it floats, under 1 it sinks.
		static float GetBuoyancy(const Actor* actor);

		/// Gets whether a unit floats: it rises to the surface of deep liquid and swims along it, rather than walking the bottom.
		static bool IsFloater(const Actor* actor) { return GetBuoyancy(actor) > 1.0F; }

		/// How fast a unit swims, in metres a second.
		static constexpr float c_SwimSpeed = 2.0F;

	private:
		static bool s_Enabled; //!< Whether units swim, float and drown.
	};
} // namespace RTE
