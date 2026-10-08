#pragma once

#include "Entity.h"
#include "glm/vec3.hpp"

#include <string>
#include <vector>

namespace RTE {

	/// A kind of weather, defined in data (AddWeather = Weather): how its drops look and move, and what it does to the sky and the ground. Purely visual; what
	/// rain, snow and dust do to the game (fire, walking, sight) goes by the built-in weather slots in WeatherEffects.
	/// The weather menu, scenes, settings and Lua pick weather by slot: 0 is clear, 1 to 4 are the built-in slots (the presets named Rain, Snow, Ash Fall and
	/// Dust Storm, which a mod can replace by defining a Weather of the same name), and 5 on are every other Weather preset, in the order the modules load.
	class Weather : public Entity {

	public:
		EntityAllocation(Weather);
		SerializableOverrideMethods;
		ClassInfoGetters;

		/// How a drop is drawn.
		enum DropShape {
			Streak, //!< A line that fades towards its tail, as rain.
			Flake, //!< A soft round dot, as snow.
			Spark, //!< A line with a bright head and a short faint tail.
			Orb //!< A soft glowing ball that fades from its centre.
		};

		/// What a Weather preset says. The defaults are no weather at all.
		struct Params {
			// Drops.
			float DropsPerScreen = 0.0F; //!< Drops on a 960x540 screen at full intensity; scaled by the screen's area and the intensity.
			int Shape = Streak; //!< A DropShape.
			glm::vec3 DropColor{0.7F, 0.78F, 0.9F}; //!< Linear colour of a drop, lit by the sky and lights.
			glm::vec3 DropColor2{0.7F, 0.78F, 0.9F}; //!< Each drop's colour is somewhere between DropColor and this.
			float DropGlow = 0.0F; //!< How much light a drop gives off itself, on top of what lights it: 0 none, above 1 feeds the bloom.
			float AlphaMin = 0.25F, AlphaMax = 0.5F; //!< How solid a drop is, picked per drop.
			float LengthMin = 7.0F, LengthMax = 13.0F; //!< Pixels along the way it moves, picked per drop.
			float Width = 1.0F; //!< Pixels across.
			float FallSpeedMin = 520.0F, FallSpeedMax = 760.0F; //!< Pixels per second down, picked per drop. Negative rises.
			float WindFactor = 1.0F; //!< How much of the wind's speed the drops take.
			float Sway = 0.0F; //!< Pixels a drop drifts from side to side, as snow does.
			float SwayRateMin = 0.6F, SwayRateMax = 1.4F; //!< How fast it sways, radians per second, picked per drop.
			bool Blown = false; //!< Blown nearly level by the wind (a dust storm): sideways at the wind times BlownWindScale, at least BlownMinSpeed, and down at FallSpeed.
			float BlownWindScale = 2.5F;
			float BlownMinSpeed = 260.0F;
			float BlownSpeedMin = 0.7F, BlownSpeedMax = 1.3F; //!< Each blown drop's speed against the gale.
			float Swirl = 0.0F; //!< Pixels a drop circles around its path.
			float SwirlRate = 2.0F; //!< Radians per second it circles.
			float Jitter = 0.0F; //!< Pixels a drop jumps sideways, now and then.
			float JitterRate = 4.0F; //!< Jumps per second.
			float PulseRate = 0.0F; //!< How fast drops brighten and dim, cycles per second. 0 for steady.
			float PulseDepth = 0.0F; //!< How far they dim, 0 to 1.
			float Twinkle = 0.0F; //!< 0 to 1: how much drops sparkle on and off at random.
			bool Shelter = true; //!< Kept out from under roofs and overhangs along the way it falls. Off: falls everywhere, even in caves.
			std::string DropShader; //!< A Shader preset to draw the drops with instead of the game's (vertex Base.rte/Shaders/Lighting/Precipitation.vert). Empty for the game's.

			// Splashes where it lands (rain's).
			float Splashes = 0.0F; //!< How many splashes, 1 as rain. 0 for none.
			glm::vec3 SplashColor{0.85F, 0.92F, 1.0F};
			float SplashGlow = 0.0F; //!< How much light a splash gives off itself.

			// The ground.
			float Rain = 0.0F; //!< How much it counts as rain: wets the ground and fills puddles. 1 as rain.
			float SnowCover = 0.0F; //!< How much it settles on the ground as snow. 1 as snow.
			float Mist = 0.0F; //!< How much ground mist it brings, 0.35 as rain.

			// The sky.
			float Overcast = 0.0F; //!< How much it greys the sky and dims the sun, 1 as any built-in weather.
			float CloudCover = 0.0F; //!< How much cloud it brings in, 1 as rain, snow and ash.
			float Lightning = 0.0F; //!< Lightning in it once it's heavier than half: 1 as rain, more for more often. 0 for none.
			glm::vec3 HazeColor{0.0F, 0.0F, 0.0F}; //!< Colour it gives the distance haze.
			float HazeColorAmount = 0.0F; //!< How far the haze takes that colour, 0 to 1.
			float Haze = 0.0F; //!< How much thicker it makes the haze.
			glm::vec3 SceneTint{1.0F, 1.0F, 1.0F}; //!< Multiplies the sky's light on the scene at full intensity, e.g. a sickly green.

			// Hooks.
			std::string Sound; //!< A SoundContainer preset for a script to loop while it falls (PostProcessMan.WeatherSound). The engine doesn't play it itself.
		};

		Weather() = default;
		~Weather() override = default;

		int Create() override { return Entity::Create(); }
		int Create(const Weather& reference);
		void Reset() override {
			m_Params = Params();
			Entity::Reset();
		}

		/// Gets what this Weather says.
		const Params& GetParams() const { return m_Params; }

		/// Gets the mean way its drops fall, in pixels per second (y down), with the given wind: what the shelter map is made for.
		glm::vec2 GetMeanFall(float wind) const;

#pragma region Slots
		static constexpr int c_BuiltInCount = 4; //!< Slots 1 to 4.

		/// Gets the Weather for a slot. Null for clear, for a slot past the last, and for custom slots (5 on) when they're turned off.
		/// @param slot The slot. @param customOn Whether slots past the built-in ones are in use (LightingSettings::CustomWeather).
		static const Weather* GetSlot(int slot, bool customOn = true);

		/// Gets how many slots there are, clear included.
		static int GetSlotCount();

		/// Gets the names of the slots in order, "Clear" first.
		static std::vector<std::string> GetSlotNames();

		/// Gets the names as one string for an ImGui combo: each followed by a 0, the whole followed by another.
		static std::string GetComboItems(bool customOn = true);

		/// Gets the slot a Weather preset of this name is in, or -1. "Clear" is 0.
		static int FindSlot(const std::string& name);
#pragma endregion

	private:
		static Entity::ClassInfo m_sClass;

		Params m_Params;
	};
} // namespace RTE
