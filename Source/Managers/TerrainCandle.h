#pragma once

#include "glm/glm.hpp"
#include <string>
#include <vector>

namespace RTE {

	/// Candles in the terrain (the sandbox's Paint > Plants > Candles): a body of candle wax with a wick of candle wick standing out of its top.
	/// Fire lights the wick (TerrainFire hands it over when it would set a wick alight), and a lit candle burns as a real one does: a small steady
	/// flame that lights what's round it, the wax melting down a pixel at a time from the middle out, a dish forming under the wick with the rim
	/// standing up round it, some of the rim running down the outside and setting there, the wick sinking as the wax goes. It goes out when water
	/// reaches it, when a strong wind or a blast blows on it, when rain falls on it in the open, when something covers it, and when it has burnt
	/// down to its foot.
	/// Part of the fire's simulation and deterministic like it: updated from TerrainFire::Update, with its own seeded random numbers.
	class TerrainCandle {

	public:
		/// A lit candle's flame, as drawn.
		struct Flame {
			glm::vec2 Tip; //!< The middle of the top of the wick, relative to the screen area asked about.
			float Size; //!< How wide the wick is, in pixels: a candle drawn bigger has a bigger flame.
			float Strength; //!< 0..1: it takes a moment to grow when lit.
			float Lean; //!< How far the wind leans it over, -1..1.
		};

		/// Gets how long a candle burns, in minutes for one 20 pixels tall (a gameplay setting); 0 for unlimited: they burn on and never melt down.
		static float GetBurnMinutes() { return s_BurnMinutes; }

		/// Sets how long a candle burns, in minutes for one 20 pixels tall; 0 (or less) for unlimited.
		static void SetBurnMinutes(float minutes) { s_BurnMinutes = minutes > 0.0F ? (minutes < 0.1F ? 0.1F : (minutes > 600.0F ? 600.0F : minutes)) : 0.0F; }

		/// Sees which materials are candle wax and wicks, for the current scene. Called with TerrainFire's fuel table.
		static void BuildTables();

		/// Gets whether a material is a candle's wick (its Burns says Wick). Cheap; safe to call from any thread once the tables are built.
		static bool IsWick(int materialID);

		/// Gets whether a material is candle wax.
		static bool IsWax(int materialID);

		/// Lights the candle whose wick is at a pixel (any pixel of the wick), if it isn't lit. Call from the simulation (main thread).
		static void Light(int x, int y);

		/// Lights the candles whose wicks are within a circle, as fire or a blast sweeping over them does. Call from the simulation (main thread).
		/// @param chance The chance each one in it catches.
		static void LightInArea(int centerX, int centerY, int radius, float chance);

		/// Puts out the candles whose flames are within a circle, where water splashed. Call from the simulation (main thread).
		static void Douse(int centerX, int centerY, int radius);

		/// Advances the candles one simulation update. Call once per sim update, from TerrainFire::Update.
		/// @param tick Whether this update is one of the fire's ticks, when the candles burn, melt and go out; their lights are registered every update.
		static void Update(bool tick);

		/// Gets the flames of the lit candles visible in a screen area.
		static void GetFlames(const glm::vec2& screenOrigin, int width, int height, std::vector<Flame>& flames);

		/// Gets how many candles are lit, for statistics.
		static int GetCount();

		/// Gets the current state as text, for saved games.
		static std::string GetSaveState();

		/// Sets state from a saved game, taken up when the loaded scene starts (StartScene).
		static void SetPendingLoadState(const std::string& state);

		/// Starts over on a new scene, lighting what the saved game had lit.
		static void StartScene();

		/// Puts out every candle, e.g. when the scene changes.
		static void Clear();

	private:
		static float s_BurnMinutes; //!< How long a candle 20 pixels tall burns, in minutes; 0 for unlimited.
	};
} // namespace RTE
