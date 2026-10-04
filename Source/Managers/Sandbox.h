#pragma once

#include <deque>
#include <string>

namespace RTE {
	class Actor;
	class MovableObject;
	class Vector;

	/// The sandbox (F7): spawn units, brains, items and bunker pieces for any side and give them orders, paint fire, liquids, smoke and terrain, set off explosions and lightning, and change the weather.
	/// In the Sandbox game mode it's the whole game: you're a god with a free camera, every unit is run by the AI, and you can take control of any unit. In other games it's a debug tool.
	/// The window only queues what you do; it's applied during the next simulation update on the main thread, like any other change to the world.
	class Sandbox {

	public:
		/// Gets whether the sandbox window is open.
		static bool IsOpen() { return s_Open; }

		/// Opens or closes the sandbox window. Opening it while controlling a unit in the Sandbox game mode goes back to the god view.
		static void Toggle() { s_Open = !s_Open; }

		/// Gets whether the current game is the Sandbox game mode.
		static bool IsGodMode();

		/// Gets whether clicks on the world go to the sandbox instead of the game: the window is open, a tool is picked and the mouse isn't over a debug window.
		static bool CapturesWorldClicks();

		/// Draws the sandbox window, the brush outline and the free camera. Call from the ImGui frame.
		static void DrawGUI();

		/// Applies what was queued from the window, keeps attacking units on a target and keeps the god view. Call once per sim update, from the main thread, before the fire, liquid and object updates.
		static void Update();

		/// Uses a sandbox tool from a script, as if clicked at a point (Lua: SandboxDo). Applied in the next sim update.
		/// @param toolName The tool's name as shown in the window ("Units", "Brain", "Item", "Structure", "Fire", "Water", "Lightning", "Rally point", "Take control", "Remove"...) or "Orders" to order a whole side.
		/// @param position Where to use it.
		/// @param team The side: 0 Red, 1 Green, 2 Blue, 3 Yellow (the game's team colours).
		/// @param order For units and "Orders": 0 hold, 1 attack nearest enemy, 2 hunt brains, 3 patrol, 4 go to rally point, 5 do nothing.
		/// @param count Squad size for units, brush size for painting.
		/// @param presetName What to spawn, for units, brains, items and structures.
		/// @return Whether the tool and preset were found.
		static bool Do(const std::string& toolName, const Vector& position, int team, int order, int count, const std::string& presetName);

		/// Gets how many fighting units a side has (Lua: SandboxCountUnits).
		/// @param team The side.
		/// @return The number of units.
		static int CountUnits(int team);

		/// Opens or closes the game's build menu in the middle of play, placing straight into the world (Lua: SandboxBuildMode).
		/// @param build Whether to build.
		/// @return Whether the game is now building.
		static bool SetBuildMode(bool build);

		/// Sets up one side of an auto battle (Lua: SandboxAutoBattleSide).
		/// @param team The side.
		/// @param faction The faction's module name, like "Coalition" or "Browncoats.rte".
		/// @param budget How much the side can spend, 0 to leave it out.
		static void SetAutoBattleSide(int team, const std::string& faction, int budget);

		/// Starts an auto battle between the sides set up for it (Lua: SandboxStartAutoBattle).
		static void StartAutoBattle();

	private:
		static bool s_Open; //!< Whether the sandbox window is open.

		/// Gets every actor in the game.
		static std::deque<Actor*>& Actors();

		/// Gets every loose item in the game.
		static std::deque<MovableObject*>& Items();

		friend struct SandboxAccess;
	};
} // namespace RTE
