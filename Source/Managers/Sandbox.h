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

		/// Opens or closes the sandbox window.
		static void SetOpen(bool open) { s_Open = open; }

		/// Told when every tool window has been put away with the one key. In the Sandbox game mode this is where you step into your character.
		/// @param atPointer Put the character down where the mouse points instead of where it stands.
		static void OnToolsClosed(bool atPointer);

		/// Gets whether the current game is the Sandbox game mode.
		static bool IsGodMode();

		/// Whether the sandbox holds the world still (god mode with its window open and "pause in menus" on), photo mode or not.
		static bool WantsWorldPaused();

		/// Gets whether clicks on the world go to the sandbox instead of the game: the window is open, a tool is picked and the mouse isn't over a debug window.
		static bool CapturesWorldClicks();

		/// The play key (P) in the Sandbox game mode: from above, steps into your character; from a unit, goes back above with the tools still hidden.
		/// @param atPointer Stepping in: put the character down where the mouse points first.
		static void TogglePlay(bool atPointer);

		/// Gets whether you're looking around the Sandbox game mode from above, in no unit, with or without the tools showing. The mouse then moves the view.
		static bool IsLookingAround();

		/// Commander mode (RC-9, F9): in any game but the Sandbox game mode, leaves the unit you play for an overhead view of your side, under
		/// its own fog of war and on its own funds, commanding its units with the sandbox's command tool; again goes back into the unit.
		static void ToggleCommander();

		/// Gets whether commander mode is on.
		static bool IsCommander();

		/// Gets whether the mouse wheel zooms the camera: the sandbox window is open in the god view and the mouse isn't over a debug window.
		static bool WantsWheelZoom();

		/// Draws the sandbox window, the brush outline and the free camera. Call from the ImGui frame.
		static void DrawGUI();

		/// The order labels debug overlay (SettingsMan::ShowOrderLabels): under each unit in view, its sandbox order or AI mode, its control group and the AI's pause. Call from the ImGui frame.
		static void DrawOrderLabels();

		/// The sandbox's own debug overlays, from the settings panel's Sandbox debug page: drawn whether or not the sandbox window is open, in
		/// any game. Call from the ImGui frame (DebugMan::DrawOverlays).
		static void DrawDebug();

		/// Applies what was queued from the window, keeps attacking units on a target and keeps the god view. Call once per sim update, from the main thread, before the fire, liquid and object updates.
		static void Update();

		/// Forgets the last game's sandbox state (orders, selection, effects, battle, colonies, the AI pause) and has the god view set up
		/// afresh. Called by ActivityMan::StartActivity for every game it starts, before the game's own start-up.
		static void OnActivityStarted();

		/// Uses a sandbox tool from a script, as if clicked at a point (Lua: SandboxDo). Applied in the next sim update.
		/// @param toolName The tool's name as shown in the window ("Units", "Brain", "Item", "Structure", "Fire", "Water", "Lightning", "Rally point", "Take control", "Remove"...) or "Orders" to order a whole side.
		/// @param position Where to use it.
		/// @param team The side: 0 Red, 1 Green, 2 Blue, 3 Yellow (the game's team colours).
		/// @param order For units and "Orders": 0 hold, 1 attack nearest enemy, 2 hunt brains, 3 patrol, 4 go to rally point, 5 do nothing, 6 dig for gold.
		/// @param count Squad size for units, brush size for painting.
		/// @param presetName What to spawn, for units, brains, items and structures. For "Drop squad", "Random units" or "Random favourites" drops random units.
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

		/// Sets up one team of the Battle Director, as its card on the Battle tab does (Lua: SandboxBattleTeam). The team's other settings stay as they are.
		/// @param team The side: 0 Red, 1 Green, 2 Blue, 3 Yellow.
		/// @param factions The factions its units come from, by module name ("Coalition" or "Browncoats.rte"), comma-separated; empty for any.
		/// @param style How it fights: 0 attack nearest enemy, 1 hunt brains, 2 defend a place (SandboxBattleDefend), 3 patrol, 4 hold position.
		/// @param budget How much it can spend in all; 0 for no limit, below 0 to leave the team out.
		static void SetBattleTeam(int team, const std::string& factions, int style, int budget);

		/// Sets how a Battle Director team's ships come in (Lua: SandboxBattleDrops).
		/// @param team The side.
		/// @param craft 0 dropship, 1 rocket.
		/// @param ships How many set off together, each with a wave of its own.
		/// @param everySeconds Seconds of game time between them.
		/// @param waveSize Units in each.
		/// @param invincible Whether the ships take no harm (and are taken away once they've left).
		static void SetBattleDrops(int team, int craft, int ships, int everySeconds, int waveSize, bool invincible);

		/// Sets the place a Battle Director team defends, for its "defend a place" style (Lua: SandboxBattleDefend).
		/// @param team The side.
		/// @param place The middle of the place.
		/// @param radius How far round it the team's units stand and fight.
		/// @param chase How far past that they go after an enemy before going back.
		static void SetBattleDefend(int team, const Vector& place, int radius, int chase);

		/// Starts every team set up for the Battle Director afresh (Lua: SandboxBattleStart). It runs until stopped.
		static void StartBattle();

		/// Stops every Battle Director team sending waves (Lua: SandboxBattleStop). Their units already in stay.
		static void StopBattle();

		/// Starts one of the Battle Director's modes (Lua: SandboxBattleMode), with the teams whose spawn zones are drawn ("Team's spawn
		/// zone" with SandboxDo, one zone per closed polygon) and the mode's other settings as the Battle tab has them. A mode of 0 (custom) stops the mode's game.
		/// @param mode 1 capture the flag, 2 king of the hill, 3 assault, 4 last team standing, 5 VIP hunt, 6 one flag.
		/// @param teamSize Most units each team has alive at once.
		/// @param byShip Whether they come in by ship over their widest spawn zone, rather than appearing in their zones.
		static void StartBattleMode(int mode, int teamSize, bool byShip);

		/// The old auto battle's calls, kept for one release for scripts that use them: a team attacking with one faction's units, or any
		/// faction's (Lua: SandboxAutoBattleSide, SandboxAutoBattleRandom, SandboxStartAutoBattle). Use the SandboxBattle calls instead.
		static void SetAutoBattleSide(int team, const std::string& faction, int budget);
		static void SetAutoBattleRandom(bool random, bool favouritesOnly);
		static void StartAutoBattle();

		/// Pauses or resumes the AI everywhere: AI-run units stand still until it's resumed (Lua: SandboxPauseAI).
		/// @param paused Whether to pause.
		static void SetAIPaused(bool paused);

		/// Makes a unit with its faction's usual weapons and puts it in the world, for things that produce units (Colony's barracks).
		/// @param presetName The unit. @param team The side. @param position Where. @param order Its orders, as for Do.
		/// @return The unit, or nothing if there is no such unit.
		static Actor* SpawnUnit(const std::string& presetName, int team, const Vector& position, int order);

		/// Gets what a unit costs, without weapons. 0 if there is no such unit.
		static float UnitCost(const std::string& presetName);

		/// Fills a box of the terrain with a material where there is air, or with an empty name clears it to air.
		static void FillBox(const Vector& topLeft, int width, int height, const std::string& materialName);

		/// Gets how your character in the Sandbox game mode is set up (its body, kit and abilities), as one line of text for the settings file.
		static std::string GetCharacterSetup();

		/// Gets the things pinned to the sandbox's bar, as one line of text (as kept in Userdata/SandboxPins.txt).
		static std::string GetPins();

		/// The things marked as favourites in the sandbox's lists, as a line of text for the settings file.
		static std::string GetFavourites();

		/// Sets the favourites from a line of text made by GetFavourites.
		static void SetFavourites(const std::string& favourites);

		/// Sets the things pinned to the sandbox's bar from a line of text made by GetPins, and keeps them in their file.
		static void SetPins(const std::string& pins);

		/// Reads the pins from their file, the first time only: they are the player's, the same in every game and save.
		/// @param fromOldSave The pins an older saved game held, taken instead when there is no pins file yet.
		static void LoadPins(const std::string& fromOldSave);

		/// Sets up your character in the Sandbox game mode from a line of text made by GetCharacterSetup.
		static void SetCharacterSetup(const std::string& setup);

	private:
		static bool s_Open; //!< Whether the sandbox window is open.

		/// Gets every actor in the game.
		static std::deque<Actor*>& Actors();

		/// Gets every loose item in the game.
		static std::deque<MovableObject*>& Items();

		friend struct SandboxAccess;
	};
} // namespace RTE
