#pragma once

#include "Serializable.h"
#include "Singleton.h"

#include <list>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

#define g_SettingsMan SettingsMan::Instance()

namespace RTE {

	/// The singleton manager over the application and misc settings.
	class SettingsMan : public Singleton<SettingsMan>, public Serializable {

	public:
		SerializableClassNameGetter;
		SerializableOverrideMethods;

#pragma region Creation
		/// Constructor method used to instantiate a SettingsMan object in system memory. Initialize() should be called before using the object.
		SettingsMan() { Clear(); }

		/// Makes the SettingsMan object ready for use.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Initialize();
#pragma endregion

#pragma region Destruction
		/// Resets the entire SettingsMan, including its inherited members, to their default settings or values.
		void Reset() override { Clear(); }
#pragma endregion

#pragma region Settings Manager Operations
		/// Gets whether Settings.ini needs to be overwritten with the complete list of settings or not. Will be true only if Settings.ini was created with default values on first load or after settings delete.
		/// @return Whether Settings.ini needs to be overwritten with the complete list of settings or not.
		bool SettingsNeedOverwrite() const { return m_SettingsNeedOverwrite; }

		/// Sets Settings.ini to be overwritten during the boot sequence for overrides to be applied (e.g. resolution validation).
		void SetSettingsNeedOverwrite() { m_SettingsNeedOverwrite = true; }

		/// Overwrites the settings file to save changes made from within the game.
		void UpdateSettingsFile() const;

		/// Writes the settings file when any setting differs from what was last written, so a change on any page is on disk within a second, not only when a menu closes or the game quits cleanly.
		/// Call once a frame; it looks at most once a second.
		void SaveSettingsIfChanged() const;

		/// Saves every setting in the settings panel (the look, time and weather, water, fire, falling ground, the AI, the HUD, the overlays) as a named preset, a file in Userdata/Presets.
		/// @param name The name. Characters that can't be in a file's name are dropped.
		/// @return The name it was saved under, or nothing if it couldn't be.
		std::string SavePreset(const std::string& name) const;

		/// Loads a preset saved by SavePreset over the settings as they are.
		/// @return Whether there was one of that name.
		bool LoadPreset(const std::string& name);

		/// Deletes a preset.
		/// @return Whether there was one of that name.
		bool DeletePreset(const std::string& name) const;

		/// Gets the names of the presets there are, in order.
		std::vector<std::string> ListPresets() const;

		/// Gets the preset loaded every time the game starts, over Settings.ini.
		/// @return The preset's name, or nothing for none.
		const std::string& GetStartupPreset() const { return m_StartupPreset; }

		/// Sets the preset loaded every time the game starts. Kept in Settings.ini.
		/// @param name The preset's name, or nothing for none.
		void SetStartupPreset(const std::string& name);

		/// Loads the preset set to load at start, if there is one. Call once at start-up, after the data modules are loaded.
		/// The panel's settings for the moment (game speed, frozen, the AI paused, debug views) are left as they are, so the game doesn't start in them.
		/// @return Whether a preset was loaded.
		bool LoadStartupPreset();

		/// Writes every setting in the settings panel: what a preset holds, and part of the settings file.
		/// @param forPreset Whether it's for a preset, which also holds what is only for the moment (game speed, the AI paused, the debug view, frozen simulation...) and says which speech is on as well as off.
		void SaveTunables(Writer& writer, const struct LightingSettings& lighting, bool forPreset) const;
#pragma endregion

#pragma region Engine Settings
		/// Returns whether LuaJit is disabled or not.
		/// @return Whether LuaJIT is disabled or not.
		bool DisableLuaJIT() const { return m_DisableLuaJIT; }

		/// Returns whether Lua debugging is disabled or not.
		/// @return Whether Lua debugging is disabled or not.
		bool EnableLuaDebugging() const { return m_EnableLuaDebugging; }

		/// Returns the recommended MOID count. If this amount is exceeded then some units may be removed at the start of the activity.
		/// @return Recommended MOID count.
		int RecommendedMOIDCount() const { return m_RecommendedMOIDCount; }

		/// Gets the Scene background layer auto-scaling mode.
		/// @return The Scene background layer auto-scaling mode. 0 for off, 1 for fit screen dimensions and 2 for always upscaled to x2.
		int GetSceneBackgroundAutoScaleMode() const { return m_SceneBackgroundAutoScaleMode; }

		/// Sets the Scene background layer auto-scaling mode.
		/// @param newMode The new Scene background layer auto-scaling mode. 0 for off, 1 for fit screen dimensions and 2 for always upscaled to x2.
		void SetSceneBackgroundAutoScaleMode(int newMode) { m_SceneBackgroundAutoScaleMode = std::clamp(newMode, 0, 2); }

		/// Gets whether faction BuyMenu theme support is disabled.
		/// @return Whether faction BuyMenu theme support is disabled.
		bool FactionBuyMenuThemesDisabled() const { return m_DisableFactionBuyMenuThemes; }

		/// Sets whether faction BuyMenu theme support is disabled.
		/// @param disable Whether faction BuyMenu theme support is disabled or not.
		void SetFactionBuyMenuThemesDisabled(bool disable) { m_DisableFactionBuyMenuThemes = disable; }

		/// Gets whether custom cursor support in faction BuyMenu themes is disabled.
		/// @return Whether faction BuyMenu theme support is disabled.
		bool FactionBuyMenuThemeCursorsDisabled() const { return m_DisableFactionBuyMenuThemeCursors; }

		/// Sets whether custom cursor support in faction BuyMenu themes is disabled.
		/// @param disable Whether custom cursor support in faction BuyMenu themes is disabled or not.
		void SetFactionBuyMenuThemeCursorsDisabled(bool disable) { m_DisableFactionBuyMenuThemeCursors = disable; }

		/// Gets the PathFinder grid node size.
		/// @return The PathFinder grid node size.
		int GetPathFinderGridNodeSize() const { return m_PathFinderGridNodeSize; }

		/// Returns whether or not any experimental settings are used.
		/// @return Whether or not any experimental settings are used.
		bool GetAnyExperimentalSettingsEnabled() const { return false; }

		/// Gets the AI update interval.
		/// @return How often Actor's AI is updated, in simulation updates.
		int GetAIUpdateInterval() const { return m_AIUpdateInterval; }

		/// Sets the AI update interval.
		/// @param newAIUpdateInterval How often Actor's AI will now be updated, in simulation updates.
		void SetAIUpdateInterval(int newAIUpdateInterval) { m_AIUpdateInterval = newAIUpdateInterval; }

		/// Gets how many threaded Lua states we'll use. -1 represents no override, which defaults to the maximum number of concurrent hardware threads.
		/// @return How many threaded Lua states we'll use.
		int GetNumberOfLuaStatesOverride() const { return m_NumberOfLuaStatesOverride; }

		/// Gets whether pathing requests will be forced to immediately complete for the next frame, or if they can take multiple frames to calculate.
		/// @return Whether pathing requests will be forced to immediately complete for the next frame
		bool GetForceImmediatePathingRequestCompletion() const { return m_ForceImmediatePathingRequestCompletion; }
#pragma endregion

#pragma region Gameplay Settings
		/// Returns true if endless MetaGame mode is enabled.
		/// @return Whether endless mode is enabled via settings.
		bool EndlessMetaGameMode() const { return m_EndlessMetaGameMode; }

		/// Sets whether endless MetaGame mode is enabled or not.
		/// @param enable Whether endless MetaGame mode is enabled or not.
		void SetEndlessMetaGameMode(bool enable) { m_EndlessMetaGameMode = enable; }

		/// Whether we need to play blips when unseen layer is revealed.
		/// @return Whether we need to play blips when unseen layer is revealed.
		bool BlipOnRevealUnseen() const { return m_BlipOnRevealUnseen; }

		/// Sets whether we need to play blips when unseen layer is revealed.
		/// @param newValue New value for Blip on reveal unseen option.
		void SetBlipOnRevealUnseen(bool newValue) { m_BlipOnRevealUnseen = newValue; }

		/// Gets the range in which devices on Scene will show the pick-up HUD.
		/// @return The range in which devices on Scene will show the pick-up HUD, in pixels. 0 means HUDs are hidden, -1 means unlimited range.
		float GetUnheldItemsHUDDisplayRange() const { return m_UnheldItemsHUDDisplayRange; }

		/// Sets the range in which devices on Scene will show the pick-up HUD.
		/// @param newRadius The new range in which devices on Scene will show the pick-up HUD, in pixels. 0 means HUDs are hidden, -1 means unlimited range.
		void SetUnheldItemsHUDDisplayRange(float newRadius) { m_UnheldItemsHUDDisplayRange = std::floor(newRadius); }

		/// Gets whether or not devices on Scene should always show their pick-up HUD when the player is in strategic mode.
		/// @return Whether or not devices on Scene should always show their pick-up HUD when the player is in strategic mode.
		bool AlwaysDisplayUnheldItemsInStrategicMode() const { return m_AlwaysDisplayUnheldItemsInStrategicMode; }

		/// Sets whether or not devices on Scene should always show their pick-up HUD when the player is in strategic mode.
		/// @param shouldShowUnheldItemsInStrategicMode Whether or not devices on Scene should always show their pick-up HUD when the player is in strategic mode.
		void SetAlwaysDisplayUnheldItemsInStrategicMode(bool shouldShowUnheldItemsInStrategicMode) { m_AlwaysDisplayUnheldItemsInStrategicMode = shouldShowUnheldItemsInStrategicMode; }

		/// Gets the number of MS a PieSlice with a sub-PieMenu needs to be hovered over for the sub-PieMenu to open.
		/// @return The number of MS a PieSlice with a sub-PieMenu needs to be hovered over for the sub-PieMenu to open.
		int GetSubPieMenuHoverOpenDelay() const { return m_SubPieMenuHoverOpenDelay; }

		/// Sets the number of MS a PieSlice with a sub-PieMenu needs to be hovered over for the sub-PieMenu to open.
		/// @param newSubPieMenuHoverOpenDelay The number of MS a PieSlice with a sb-PieMenu needs to be hovered over for the sub-PieMenu to open.
		void SetSubPieMenuHoverOpenDelay(int newSubPieMenuHoverOpenDelay) { m_SubPieMenuHoverOpenDelay = newSubPieMenuHoverOpenDelay; }

		/// Gets whether a unit's right-click menu is the classic pie wheel rather than the action menu (RC-12): a list over the pointer with
		/// every order and the unit's engagement rules on one layer.
		bool ClassicPieWheel() const { return m_ClassicPieWheel; }

		/// Sets ClassicPieWheel; see there.
		void SetClassicPieWheel(bool classic) { m_ClassicPieWheel = classic; }

		/// Whether red and white flashes appear when brain is damaged.
		/// @return Whether red and white flashes appear when brain is damaged.
		bool FlashOnBrainDamage() const { return m_FlashOnBrainDamage; }

		/// Sets whether red and white flashes appear when brain is damaged.
		/// @param newValue New value for Flash on brain damage setting.
		void SetFlashOnBrainDamage(bool newValue) { m_FlashOnBrainDamage = newValue; }

		/// Whether we need to show items from other factions in buy menu GUI.
		/// @return True if we need to show foreign items.
		bool ShowForeignItems() const { return m_ShowForeignItems; }

		/// Set whether we need to show items from other factions in buy menu GUI.
		/// @param newValue If we need to show foreign items.
		void SetShowForeignItems(bool newValue) { m_ShowForeignItems = newValue; }

		/// Gets whether the crab bomb effect is enabled or not.
		/// @return Whether the crab bomb effect is enabled or not. False means releasing whatever number of crabs will do nothing except release whatever number of crabs.
		bool CrabBombsEnabled() const { return m_EnableCrabBombs; }

		/// Whether actors pull themselves up onto ledges and over low obstacles they walk or jet into (see Actor::TryStartMantle).
		bool MantlingEnabled() const { return m_EnableMantling; }

		/// Whether everything that came off a unit and isn't flesh or bone (metal plating, gear, robot parts) settles into the terrain as Flesh Scraps,
		/// keeping its colours, as the flesh does (see Material::GetTerrainSettleMaterial).
		bool BodyGearSettlesAsScraps() const { return m_BodyGearSettlesAsScraps; }

		/// Sets whether what came off a unit settles as Flesh Scraps. Remains already in the terrain stay as they settled.
		void SetBodyGearSettlesAsScraps(bool enable) { m_BodyGearSettlesAsScraps = enable; }

		/// Whether every scene is played without wrapping horizontally, with hard left and right edges, whatever its terrain says (see Scene::LoadData).
		/// Takes effect when a scene is next loaded.
		bool NoSceneWrap() const { return m_NoSceneWrap; }

		/// How strongly fire pins units down and shakes them (Actor::GetSuppression and GetMorale): 0 for not at all, 1 as designed, 2 double.
		float AISuppression() const { return m_AISuppression; }

		/// How readily units with a digger tunnel through ground rather than go round it (PathFinder's dig edges): 0 only when there is no
		/// other way (each node dug priced at the material's integrity, as before), 1 as designed (a short cut through soft ground beats a long
		/// way round), 2 twice as readily.
		float AIDigWillingness() const { return m_AIDigWillingness; }

		/// How much the routes of units a game mode wants kept safe (Actor::GetRouteThreatAvoidance: a capture the flag carrier) keep clear of
		/// enemies (PathFinder::ThreatCost): 0 not at all, 1 as designed (a way past a crowd of enemies loses to a longer one past none; one
		/// sentry is skirted only when going round is short), 2 twice as much. Other units always take the shortest way.
		float AIThreatAvoidance() const { return m_AIThreatAvoidance; }
		/// How reckless AI units are on the move, 0 (careful) to 1 (reckless); 0.5 is as designed. It scales how long a unit steadies itself
		/// before it jets, how much fuel it waits for, and how much the route search shies from hard jumps and long drops (see AIMoveCaution).
		float AIRecklessness() const { return m_AIRecklessness; }

		/// Gets the percentage, 0 to 100, of units that are handed a digger when they come into the scene without one.
		float AISpawnDiggerChance() const { return m_AISpawnDiggerChance; }

		/// Gets the percentage, 0 to 100, of wounds (hits, and limbs torn off) that keep bleeding until the unit bleeds out or is patched up,
		/// rather than stopping when their own definition says. 0, the default, is every wound as it is defined.
		float BleedOutChance() const { return m_BleedOutChance; }

		/// Gets which digger those units are handed: 0 Light, 1 Medium, 2 Heavy, 3 a random one of the three.
		int AISpawnDiggerType() const { return m_AISpawnDiggerType; }

		/// The recklessness as a multiplier on the AI's movement caution: 2 at the careful end, 1 as designed, 0.5 at the reckless end.
		float AIMoveCaution() const { return std::pow(2.0F, (0.5F - m_AIRecklessness) * 2.0F); }

		/// Whether AI units stand still and upright before a jetpack climb or jump (on, as designed), or take off mid-stride.
		bool AISteadiesBeforeJet() const { return m_AISteadyBeforeJet; }

		/// Whether AI units wait at a take-off for the fuel the flight needs (on, as designed), or go with what is in the tank.
		bool AIWaitsForFuel() const { return m_AIWaitForFuel; }

		/// Gets what the navigation debug overlay shows: 0 nothing, 1 the path grid in view (where a unit stands, crawls or doesn't fit, and the
		/// step-overs, stairs and leaps between), 2 that and each flight's landing and the engine pilot's predicted path (see PathFinder::DrawDebug),
		/// 3 that and the node under the pointer: what the grid makes of it and every way out of it with its cost (see DebugOverlays::DrawNavNode).
		/// @return The level.
		int NavDebugOverlay() const { return m_NavDebugOverlay; }

		/// Gets whether the frame rate and the game's version are shown, small, in the top right of the window (a debug aid, on by default).
		/// @return Whether they are shown.
		bool ShowFPSAndVersion() const { return m_ShowFPSAndVersion; }

		/// Sets whether the frame rate and the game's version are shown in the top right of the window.
		/// @param show Whether to show them.
		void SetShowFPSAndVersion(bool show) { m_ShowFPSAndVersion = show; }

		/// The debug text channels: kinds of debug lines written to the console (and LogConsole.txt). Each keeps its line prefix (AITRACE, PATHLOG, SANDBOX, PERF), so log readers still match.
		enum class DebugChannel { AI, Path, Pilot, Climb, Combat, Squad, Sandbox, Perf, Grid, Count };

		/// Gets the name of a debug channel, as the settings panel and Lua know it.
		static const char* DebugChannelName(DebugChannel channel);

		/// Gets the debug channel with a name (any case), or DebugChannel::Count if there's none by that name.
		static DebugChannel DebugChannelFromName(const std::string& name);

		/// Gets whether a debug channel is on: ticked in the settings, or switched on for this run by its CCCP_* environment variable (CCCP_AI_LOG turns on AI, Pilot, Climb, Combat and Squad; CCCP_PATH_LOG, CCCP_SANDBOX_LOG and CCCP_PERF_LOG their own).
		bool DebugChannelOn(DebugChannel channel) const;

		/// Gets whether a debug channel is ticked in the settings (the environment aside).
		bool DebugChannelTicked(DebugChannel channel) const { return (m_DebugChannels >> static_cast<int>(channel)) & 1; }

		/// Ticks or unticks a debug channel in the settings.
		void SetDebugChannel(DebugChannel channel, bool on) { m_DebugChannels = on ? (m_DebugChannels | (1u << static_cast<int>(channel))) : (m_DebugChannels & ~(1u << static_cast<int>(channel))); }

		/// For Lua: whether the debug channel with this name is on.
		bool IsDebugChannelOn(const std::string& name) const { DebugChannel channel = DebugChannelFromName(name); return channel != DebugChannel::Count && DebugChannelOn(channel); }

		/// Gets whether the AI channels trace every unit, not just the inspected ones (ticked, or CCCP_AI_LOG=all).
		bool TraceAllUnits() const;

		/// Sets whether the AI channels trace every unit.
		void SetTraceAllUnits(bool all) { m_TraceAllUnits = all; }

		/// Gets the team whose view the debug overlays show (the navigation overlay's path grid, for one, differs by team at doors).
		/// @return The team, 0 to 3.
		int DebugTeam() const { return m_DebugTeam; }

		/// Sets the team whose view the debug overlays show.
		/// @param team 0 to 3.
		void SetDebugTeam(int team) { m_DebugTeam = std::clamp(team, 0, 3); }

		/// Gets which units the unit inspector overlay labels with their AI state: 0 none, 1 the inspected ones (Ctrl+I, sandbox selection, the one a player controls), 2 every unit in view.
		int UnitInspector() const { return m_UnitInspector; }

		/// Sets which units the unit inspector labels; see UnitInspector.
		/// @param which 0, 1 or 2.
		void SetUnitInspector(int which) { m_UnitInspector = std::clamp(which, 0, 2); }

		/// Gets which units the combat AI overlay draws for: 0 none, 1 the inspected ones, 2 every unit in view. It shows the line to the target, the range held, and the cover, flank and retreat spots.
		int CombatOverlay() const { return m_CombatOverlay; }

		/// Sets which units the combat AI overlay draws for; see CombatOverlay.
		/// @param which 0, 1 or 2.
		void SetCombatOverlay(int which) { m_CombatOverlay = std::clamp(which, 0, 2); }

		/// Gets what the world simulation overlay shows: 0 nothing, 1 moving liquid, 2 burning ground, 3 smoke thick enough to hide things,
		/// 4 loose falling pieces of terrain, 5 the weather (wind and what's falling), 6 ropes (how hard each is pulled, and where it's tied).
		int WorldSimOverlay() const { return m_WorldSimOverlay; }

		/// Sets what the world simulation overlay shows; see WorldSimOverlay.
		/// @param which 0 to 6.
		void SetWorldSimOverlay(int which) { m_WorldSimOverlay = std::clamp(which, 0, 6); }

		/// Gets whether the sandbox's stroke log is on.
		bool ShowSandboxStrokeLog() const { return m_SandboxStrokeLog; }

		/// Sets whether the sandbox's stroke log is on.
		void SetShowSandboxStrokeLog(bool show) { m_SandboxStrokeLog = show; }

		/// Gets whether move previews show whether each standing spot can be reached.
		bool ShowSandboxSpotReach() const { return m_SandboxSpotReach; }

		/// Sets whether move previews show whether each standing spot can be reached.
		void SetShowSandboxSpotReach(bool show) { m_SandboxSpotReach = show; }

		/// Whether units in a sandbox control group show the group's number by them (RC-6).
		bool ShowSandboxGroupBadges() const { return m_SandboxGroupBadges; }

		/// Sets whether units in a sandbox control group show the group's number by them.
		void SetShowSandboxGroupBadges(bool show) { m_SandboxGroupBadges = show; }

		/// Which units in the sandbox show their order as a mark over them (RC-7): 0 none, 1 the selected ones, 2 all.
		int SandboxOrderGlyphs() const { return m_SandboxOrderGlyphs; }

		/// Sets SandboxOrderGlyphs; see there.
		void SetSandboxOrderGlyphs(int which) { m_SandboxOrderGlyphs = std::clamp(which, 0, 2); }

		/// How the sandbox's Spawn tab shows what units and items are like (cost, health, mass, fire rate...): 0 not at all, 1 in the tooltip of the one under the pointer, 2 on every tile as well.
		int SandboxSpawnStats() const { return m_SandboxSpawnStats; }

		/// Sets SandboxSpawnStats; see there.
		void SetSandboxSpawnStats(int which) { m_SandboxSpawnStats = std::clamp(which, 0, 2); }

		/// Whether the sandbox pings where units of the selection's side come under fire (RC-7).
		bool ShowSandboxAttackPings() const { return m_SandboxAttackPings; }

		/// Sets whether the sandbox pings where units of the selection's side come under fire.
		void SetShowSandboxAttackPings(bool show) { m_SandboxAttackPings = show; }

		/// Whether the sandbox's map window is shown (RC-8).
		bool ShowSandboxMinimap() const { return m_SandboxMinimap; }

		/// Sets whether the sandbox's map window is shown.
		void SetShowSandboxMinimap(bool show) { m_SandboxMinimap = show; }

		/// Gets whether the lighting-by-source readout is on.
		bool ShowLightsBySource() const { return m_LightsBySource; }

		/// Sets whether the lighting-by-source readout is on.
		void SetShowLightsBySource(bool show) { m_LightsBySource = show; }

		/// Gets whether the sandbox's character state line is on.
		bool ShowSandboxCharacterState() const { return m_SandboxCharacterState; }

		/// Sets whether the sandbox's character state line is on.
		void SetShowSandboxCharacterState(bool show) { m_SandboxCharacterState = show; }

		/// Gets whether the sandbox's auto battle and colony readout is on.
		bool ShowSandboxAutoBattle() const { return m_SandboxAutoBattle; }

		/// Sets whether the sandbox's auto battle and colony readout is on.
		void SetShowSandboxAutoBattle(bool show) { m_SandboxAutoBattle = show; }

		/// Gets whether the sandbox's terrain paint audit is on: the last two dozen discs and boxes of terrain painted, dug, filled or cleared, with the material and whether falling ground and liquid were told of the change.
		bool ShowSandboxPaintAudit() const { return m_SandboxPaintAudit; }

		/// Sets whether the sandbox's terrain paint audit is on.
		void SetShowSandboxPaintAudit(bool show) { m_SandboxPaintAudit = show; }

		/// Gets whether the sandbox's selection and camera overlay is on: a drag box as the selection will actually use it (map wrapping included), the unit the game controls against the one the sandbox thinks you're in, the observation target and the free camera's centre, and the view's scale.
		bool ShowSandboxSelectionCamera() const { return m_SandboxSelectionCamera; }

		/// Sets whether the sandbox's selection and camera overlay is on.
		void SetShowSandboxSelectionCamera(bool show) { m_SandboxSelectionCamera = show; }

		/// Gets whether the sandbox's incoming and effects overlay is on: each thing on its way in from the sky with its line, where it will land and its crater, each effect put down with its light's reach, each water spring, and the storm cells' next flash.
		bool ShowSandboxEffects() const { return m_SandboxEffects; }

		/// Sets whether the sandbox's incoming and effects overlay is on.
		void SetShowSandboxEffects(bool show) { m_SandboxEffects = show; }

		/// Gets whether the sandbox's gas overlay is on: each cell of the gas grid (SB-6) in view, tinted by the gas in it (methane too, which can't otherwise be seen).
		bool ShowSandboxGas() const { return m_SandboxGas; }

		/// Sets whether the sandbox's gas overlay is on.
		void SetShowSandboxGas(bool show) { m_SandboxGas = show; }

		/// Gets whether the sandbox's air overlay is on: the pressure and movement of blast waves (SB-5) in view, the area they are worked out over, and the wind and where it is sheltered.
		bool ShowSandboxAir() const { return m_SandboxAir; }

		/// Sets whether the sandbox's air overlay is on.
		void SetShowSandboxAir(bool show) { m_SandboxAir = show; }

		/// Gets whether the sandbox's sim state readout is on: what is pausing the world, the AI pause, sim updates per drawn frame, the sandbox's queued and applied tool uses and steps, and the time scale.
		bool ShowSandboxSimState() const { return m_SandboxSimState; }

		/// Sets whether the sandbox's sim state readout is on.
		void SetShowSandboxSimState(bool show) { m_SandboxSimState = show; }

		/// Gets whether the right-hand group of the sandbox's bottom bar is shown: each side's unit count, the AI pause, the speed of time, the step and undo. Off by default, which keeps the bar short.
		bool ShowSandboxBarRight() const { return m_SandboxBarRight; }

		/// Sets whether the right-hand group of the sandbox's bottom bar is shown.
		void SetShowSandboxBarRight(bool show) { m_SandboxBarRight = show; }

		/// Gets which units the sandbox orders overlay draws for: 0 none, 1 the sandbox's selection (or inspected units), 2 every unit in view. It shows each unit's order waiting for the next update, its standing order, and a red flash when the standing orders send it again.
		int SandboxOrdersOverlay() const { return m_SandboxOrdersOverlay; }

		/// Sets SandboxOrdersOverlay; see there.
		/// @param which 0 to 2.
		void SetSandboxOrdersOverlay(int which) { m_SandboxOrdersOverlay = std::clamp(which, 0, 2); }

		/// Gets whether the light sources overlay is on: every light on player 1's screen as a reach circle and colour dot (cones as wedges),
		/// the scenery lamps with what they hang on, and counts by kind with the fill cost.
		bool ShowLightSources() const { return m_ShowLightSources; }

		/// Sets whether the light sources overlay is on.
		void SetShowLightSources(bool show) { m_ShowLightSources = show; }

		/// Gets whether the sun direction overlay is on: an arrow from the middle of the screen towards the sun (or moon), with its shadow strength.
		bool ShowSunDirection() const { return m_ShowSunDirection; }

		/// Sets whether the sun direction overlay is on.
		void SetShowSunDirection(bool show) { m_ShowSunDirection = show; }

		/// Gets whether the terrain update boxes overlay is on: the areas of changed terrain waiting for the path grid (orange) and the nodes
		/// re-sampled for them (red), each shown for a moment after it happens. Not saved.
		bool ShowTerrainUpdates() const { return m_ShowTerrainUpdates; }

		/// Sets whether the terrain update boxes overlay is on.
		void SetShowTerrainUpdates(bool show) { m_ShowTerrainUpdates = show; }

		/// Gets whether the recent path solves overlay is on: the debug team's last few routes found, each step with its kind and cost. Not saved.
		/// While it's on the path finder keeps those routes, which costs a little time per search.
		bool ShowRecentSolves() const { return m_ShowRecentSolves; }

		/// Sets whether the recent path solves overlay is on.
		void SetShowRecentSolves(bool show) { m_ShowRecentSolves = show; }

		/// Gets whether the squad links and trails overlay is on: for inspected squad units, the leader-to-follower line, the leader's trail and each follower's place in line (drawn by the AI scripts).
		bool ShowSquadLinks() const { return m_ShowSquadLinks; }

		/// Sets whether the squad links and trails overlay is on.
		void SetShowSquadLinks(bool show) { m_ShowSquadLinks = show; }

		/// Gets whether the order labels overlay is on: under each unit in view its sandbox order (or AI mode), its control group and whether the AI is paused, and in the sandbox's World tab each auto battle side's budget, spending and next wave.
		bool ShowOrderLabels() const { return m_ShowOrderLabels; }

		/// Sets whether the order labels overlay is on.
		void SetShowOrderLabels(bool show) { m_ShowOrderLabels = show; }

		/// Sets what the navigation debug overlay shows; see NavDebugOverlay.
		/// @param level 0 to 3.
		void SetNavDebugOverlay(int level) { m_NavDebugOverlay = std::clamp(level, 0, 3); }

		/// Sets whether actors mantle ledges and vault low obstacles.
		void SetMantlingEnabled(bool enable) { m_EnableMantling = enable; }

		/// Sets whether every scene is played without wrapping horizontally. Takes effect when a scene is next loaded.
		void SetNoSceneWrap(bool enable) { m_NoSceneWrap = enable; }

		/// Sets how strongly fire pins units down and shakes them, 0 to 2.
		void SetAISuppression(float scale) { m_AISuppression = std::clamp(scale, 0.0F, 2.0F); }

		/// Sets how readily units with a digger tunnel; see AIDigWillingness.
		void SetAIDigWillingness(float scale) { m_AIDigWillingness = std::clamp(scale, 0.0F, 2.0F); }
		/// Sets how much safe-route units keep clear of enemies, 0 to 2; see AIThreatAvoidance.
		void SetAIThreatAvoidance(float scale) { m_AIThreatAvoidance = std::clamp(scale, 0.0F, 2.0F); }
		/// Sets how reckless AI units are on the move, 0 to 1 (0.5 as designed).
		void SetAIRecklessness(float recklessness) { m_AIRecklessness = std::clamp(recklessness, 0.0F, 1.0F); }
		/// Sets the percentage of units handed a digger as they come into the scene; see AISpawnDiggerChance.
		void SetAISpawnDiggerChance(float percent) { m_AISpawnDiggerChance = std::clamp(percent, 0.0F, 100.0F); }
		/// Sets the percentage of wounds that keep bleeding; see BleedOutChance.
		void SetBleedOutChance(float percent) { m_BleedOutChance = std::clamp(percent, 0.0F, 100.0F); }
		/// Sets which digger those units are handed; see AISpawnDiggerType.
		void SetAISpawnDiggerType(int type) { m_AISpawnDiggerType = std::clamp(type, 0, 3); }

		/// Sets whether AI units steady themselves before they jet.
		void SetAISteadiesBeforeJet(bool steady) { m_AISteadyBeforeJet = steady; }

		/// Sets whether AI units wait for fuel before they jet.
		void SetAIWaitsForFuel(bool wait) { m_AIWaitForFuel = wait; }

		/// Sets whether the crab bomb effect is enabled or not.
		/// @param enable Enable the crab bomb effect or not. False means releasing whatever number of crabs will do nothing except release whatever number of crabs.
		void SetCrabBombsEnabled(bool enable) { m_EnableCrabBombs = enable; }

		/// Gets the number of crabs needed to be released at once to trigger the crab bomb effect.
		/// @return The number of crabs needed to be released at once to trigger the crab bomb effect.
		int GetCrabBombThreshold() const { return m_CrabBombThreshold; }

		/// Sets the number of crabs needed to be released at once to trigger the crab bomb effect.
		/// @param newThreshold The new number of crabs needed to be released at once to trigger the crab bomb effect.
		void SetCrabBombThreshold(int newThreshold) { m_CrabBombThreshold = newThreshold; }

		/// Gets whether the HUD of enemy Actors is set to be visible to the player or not.
		/// @return Whether the HUD of enemy Actors is visible to the player.
		bool ShowEnemyHUD() const { return m_ShowEnemyHUD; }

		/// Sets whether the HUD of enemy Actors should to be visible to the player or not.
		/// @param showHUD Whether the HUD of enemy Actors should be visible to the player or not.
		void SetShowEnemyHUD(bool showHUD) { m_ShowEnemyHUD = showHUD; }

		/// Whether each unit's side (its team icon) and health are drawn beside it. In the Sandbox game mode every side's are, the side you
		/// look from being only who the tools are for; elsewhere other sides' follow ShowEnemyHUD and what your side has seen.
		bool ShowUnitTags() const { return m_ShowUnitTags; }

		/// Sets whether each unit's side and health are drawn beside it.
		void SetShowUnitTags(bool show) { m_ShowUnitTags = show; }

		/// Gets whether smart BuyMenu navigation is enabled, meaning swapping to equipment mode and back will change active tabs in the BuyMenu.
		/// @return Whether smart BuyMenu navigation is enabled or not.
		bool SmartBuyMenuNavigationEnabled() const { return m_EnableSmartBuyMenuNavigation; }

		/// Sets whether smart BuyMenu navigation is enabled, meaning swapping to equipment mode and back will change active tabs in the BuyMenu.
		/// @param enable Whether to enable smart BuyMenu navigation or not.
		void SetSmartBuyMenuNavigation(bool enable) { m_EnableSmartBuyMenuNavigation = enable; }

		/// Gets whether gold gathered by Actors is automatically added into team funds.
		/// @return Whether gold gathered by Actors is automatically added into team funds.
		bool GetAutomaticGoldDeposit() const { return m_AutomaticGoldDeposit; }
#pragma endregion

#pragma region Network Settings
		/// Gets the player name that is used in network multiplayer matches.
		/// @return String with the network player name.
		const std::string& GetPlayerNetworkName() const { return m_PlayerNetworkName; }

		/// Sets the player name that will be used in network multiplayer matches.
		/// @param newName String with the new player name to use.
		void SetPlayerNetworkName(const std::string& newName) { m_PlayerNetworkName = newName.empty() ? "Dummy" : newName; }

		/// Gets the LAN server address to connect to.
		/// @return The current LAN server address to connect to.
		const std::string& GetNetworkServerAddress() const { return m_NetworkServerAddress; }

		/// Sets the LAN server address to connect to.
		/// @param newName New LAN server address to connect to.
		void SetNetworkServerAddress(const std::string& newAddress) { m_NetworkServerAddress = newAddress.empty() ? "127.0.0.1:8000" : newAddress; }

		/// Gets the NAT punch-through server address.
		/// @return The current NAT punch-through server address to connect to.
		const std::string& GetNATServiceAddress() { return m_NATServiceAddress; }

		/// Sets the NAT punch-through server address.
		/// @param newValue New NAT punch-through server address to connect to.
		void SetNATServiceAddress(const std::string& newAddress) { m_NATServiceAddress = newAddress.empty() ? "127.0.0.1:61111" : newAddress; }

		/// Gets the server name used when connecting via NAT punch-through service.
		/// @return Name of the NAT punch-through server.
		const std::string& GetNATServerName() { return m_NATServerName; }

		/// Sets the server name to use when connecting via NAT punch-through service.
		/// @param newValue New NAT punch-through server name.
		void SetNATServerName(const std::string& newName) { m_NATServerName = newName.empty() ? "DefaultServerName" : newName; }

		/// Gets the server password to use when connecting via NAT punch-through service.
		/// @return The server password to use when connecting via NAT punch-through service.
		const std::string& GetNATServerPassword() { return m_NATServerPassword; }

		/// Sets the server password to use when connecting via NAT punch-through service.
		/// @param newValue New password to use when connecting via NAT punch-through service.
		void SetNATServerPassword(const std::string& newValue) { m_NATServerPassword = newValue.empty() ? "DefaultServerPassword" : newValue; }

		/// Gets whether or not experimental multiplayer speedboosts should be used.
		/// @return Whether or not experimental multiplayer speedboosts should be used.
		bool UseExperimentalMultiplayerSpeedBoosts() const { return m_UseExperimentalMultiplayerSpeedBoosts; }

		/// Sets whether or not experimental multiplayer speedboosts should be used.
		/// @param newValue Whether or not experimental multiplayer speedboosts should be used.
		void SetUseExperimentalMultiplayerSpeedBoosts(bool newValue) { m_UseExperimentalMultiplayerSpeedBoosts = newValue; }
#pragma endregion

#pragma region Editor Settings
		/// Returns the list of visible assembly groups.
		/// @return List of visible assembly groups.
		const std::list<std::string>& GetVisibleAssemblyGroupsList() const { return m_VisibleAssemblyGroupsList; }

		/// Whether editors will allow to select Base.rte as a module to save in
		/// @return True of editors are allowed to select Base.rte as a module to save in.
		bool AllowSavingToBase() const { return m_AllowSavingToBase; }

		/// Whether we need to show MetaScenes in editors and scenario UI.
		/// @return True if we need to show MetaScenes.
		bool ShowMetascenes() const { return m_ShowMetaScenes; }
#pragma endregion

#pragma region Mod and Script Management
		/// Gets the map of mods which are disabled.
		/// @return Map of mods which are disabled.
		std::unordered_map<std::string, bool>& GetDisabledModsMap() { return m_DisabledMods; }

		/// Gets whether the specified mod is disabled in the settings.
		/// @param modModule Mod to check.
		/// @return Whether the mod is disabled via settings.
		bool IsModDisabled(const std::string& modModule) const { return (m_DisabledMods.find(modModule) != m_DisabledMods.end()) ? m_DisabledMods.at(modModule) : false; }

		/// Gets the map of global scripts which are enabled.
		/// @return Map of global scripts which are enabled.
		std::unordered_map<std::string, bool>& GetEnabledGlobalScriptMap() { return m_EnabledGlobalScripts; }

		/// Gets whether the specified global script is enabled in the settings.
		/// @param scriptName Global script to check.
		/// @return Whether the global script is enabled via settings.
		bool IsGlobalScriptEnabled(const std::string& scriptName) const { return (m_EnabledGlobalScripts.find(scriptName) != m_EnabledGlobalScripts.end()) ? m_EnabledGlobalScripts.at(scriptName) : false; }
#pragma endregion

#pragma region Misc Settings
		/// Gets whether the game intro is set to be skipped on game startup or not.
		/// @return Whether intro is set to be skipped or not.
		bool SkipIntro() const { return m_SkipIntro; }

		/// Sets whether the game intro should be skipped on game startup or not.
		/// @param play Whether to skip game intro or not.
		void SetSkipIntro(bool play) { m_SkipIntro = play; }

		/// Gets whether tooltip display on certain UI elements is enabled or not.
		/// @return Whether tooltips are displayed or not.
		bool ShowToolTips() const { return m_ShowToolTips; }

		/// Sets whether to display tooltips on certain UI elements or not.
		/// @param showToolTips Whether to display tooltips or not.
		void SetShowToolTips(bool showToolTips) { m_ShowToolTips = showToolTips; }

		/// Gets whether to draw AtomGroup visualizations or not.
		/// @return Whether to draw AtomGroup visualizations or not.
		bool DrawAtomGroupVisualizations() const { return m_DrawAtomGroupVisualizations; }

		/// Sets whether to draw AtomGroup visualizations or not.
		/// @param drawAtomGroupVisualizations Whether to draw AtomGroup visualizations or not.
		void SetDrawAtomGroupVisualizations(bool drawAtomGroupVisualizations) { m_DrawAtomGroupVisualizations = drawAtomGroupVisualizations; }

		/// Gets whether to draw HandGroup and FootGroup visualizations or not.
		/// @return Whether to draw HandGroup and FootGroup visualizations or not.
		bool DrawHandAndFootGroupVisualizations() const { return m_DrawHandAndFootGroupVisualizations; }

		/// Sets whether to draw HandGroup and FootGroup visualizations or not.
		/// @param drawHandAndFootGroupVisualizations Whether to draw HandGroup and FootGroup visualizations or not.
		void SetDrawHandAndFootGroupVisualizations(bool drawHandAndFootGroupVisualizations) { m_DrawHandAndFootGroupVisualizations = drawHandAndFootGroupVisualizations; }

		/// Gets whether to draw LimbPath visualizations or not.
		/// @return Whether to draw LimbPath visualizations or not.
		bool DrawLimbPathVisualizations() const { return m_DrawLimbPathVisualizations; }

		/// Sets whether to draw LimbPath visualizations or not.
		/// @param drawAtomGroupVisualizations Whether to draw AtomGroup visualizations or not.
		void SetDrawLimbPathVisualizations(bool drawLimbPathVisualizations) { m_DrawLimbPathVisualizations = drawLimbPathVisualizations; }

		/// Gets whether debug print mode is enabled or not.
		/// @return Whether debug print mode is enabled or not.
		bool PrintDebugInfo() const { return m_PrintDebugInfo; }

		/// Sets print debug info mode.
		/// @param printDebugInfo New debug print mode value.
		void SetPrintDebugInfo(bool printDebugInfo) { m_PrintDebugInfo = printDebugInfo; }

		/// Gets whether displaying the reader progress report during module loading is disabled or not.
		/// @return Whether the reader progress report is being displayed during module loading or not.
		bool GetLoadingScreenProgressReportDisabled() const { return m_DisableLoadingScreenProgressReport; }

		/// Sets whether the reader progress report should be displayed during module loading or not.
		/// @param disable Whether to display the reader progress report during module loading or not.
		void SetLoadingScreenProgressReportDisabled(bool disable) { m_DisableLoadingScreenProgressReport = disable; }

		/// Gets how accurately the reader progress report tells what line it's reading during module loading.
		/// @return How accurately the reader progress report tells what line it's reading during module loading.
		int LoadingScreenProgressReportPrecision() const { return m_LoadingScreenProgressReportPrecision; }

		/// Gets the multiplier value for the transition durations between different menus.
		/// @return The multiplier value for the transition durations between different menus. Lower values equal faster transitions.
		float GetMenuTransitionDurationMultiplier() const { return m_MenuTransitionDurationMultiplier; }

		/// Sets the multiplier value for the transition durations between different menus.
		/// @param newSpeed New multiplier value for the transition durations between different menus. Lower values equal faster transitions.
		void SetMenuTransitionDurationMultiplier(float newSpeed) { m_MenuTransitionDurationMultiplier = std::max(0.0F, newSpeed); }

		/// Gets whether the duration of module loading (extraction included) is being measured or not. For benchmarking purposes.
		/// @return Whether duration is being measured or not.
		bool IsMeasuringModuleLoadTime() const { return m_MeasureModuleLoadTime; }

		/// Sets whether the duration of module loading (extraction included) should be measured or not. For benchmarking purposes.
		/// @param measure Whether duration should be measured or not.
		void MeasureModuleLoadTime(bool measure) { m_MeasureModuleLoadTime = measure; }
#pragma endregion

	protected:
		bool m_SettingsNeedOverwrite; //!< Whether the settings file was generated with minimal defaults and needs to be overwritten to be fully populated.

		bool m_ShowForeignItems; //!< Do not show foreign items in buy menu.
		bool m_FlashOnBrainDamage; //!< Whether red flashes on brain damage are on or off.
		bool m_BlipOnRevealUnseen; //!< Blip if unseen is revealed.
		float m_UnheldItemsHUDDisplayRange; //!< Range in which devices on Scene will show the pick-up HUD, in pixels. 0 means HUDs are hidden, -1 means unlimited range.
		bool m_AlwaysDisplayUnheldItemsInStrategicMode; //!< Whether or not devices on Scene should always show their pick-up HUD when when the player is in strategic mode.
		int m_SubPieMenuHoverOpenDelay; //!< The number of MS a PieSlice with a sub-PieMenu needs to be hovered over for the sub-PieMenu to open.
		bool m_ClassicPieWheel; //!< Whether a unit's right-click menu is the classic pie wheel rather than the action menu (see ClassicPieWheel).
		bool m_EndlessMetaGameMode; //!< Endless MetaGame mode.
		bool m_ShowFPSAndVersion; //!< Whether the frame rate and version are shown in the top right (see ShowFPSAndVersion).
		int m_NavDebugOverlay; //!< What the navigation debug overlay shows (see NavDebugOverlay).
		int m_DebugTeam; //!< The team whose view the debug overlays show (see DebugTeam).
		int m_UnitInspector; //!< Which units the unit inspector overlay labels (see UnitInspector).
		int m_WorldSimOverlay; //!< What the world simulation overlay shows (see WorldSimOverlay).
		bool m_SandboxStrokeLog; //!< Whether the sandbox's stroke log is on (see ShowSandboxStrokeLog).
		bool m_SandboxSpotReach; //!< Whether move previews show each standing spot's reachability (see ShowSandboxSpotReach).
		bool m_SandboxGroupBadges; //!< Whether control-group units show their group's number (see ShowSandboxGroupBadges).
		int m_SandboxOrderGlyphs; //!< Which units show their order as a mark (see SandboxOrderGlyphs).
		int m_SandboxSpawnStats; //!< How the Spawn tab shows units' and items' stats (see SandboxSpawnStats).
		bool m_SandboxAttackPings; //!< Whether units coming under fire are pinged (see ShowSandboxAttackPings).
		bool m_SandboxMinimap; //!< Whether the sandbox's map window is shown (see ShowSandboxMinimap).
		bool m_LightsBySource; //!< Whether the lighting-by-source readout is on (see ShowLightsBySource).
		bool m_SandboxCharacterState; //!< Whether the sandbox's character state line is on (see ShowSandboxCharacterState).
		bool m_SandboxAutoBattle; //!< Whether the sandbox's battle and colony readout is on (see ShowSandboxAutoBattle). (Named for the auto battle the Battle Director replaced, so settings files keep it.)
		bool m_SandboxPaintAudit; //!< Whether the sandbox's terrain paint audit is on (see ShowSandboxPaintAudit).
		bool m_SandboxSelectionCamera; //!< Whether the sandbox's selection and camera overlay is on (see ShowSandboxSelectionCamera).
		bool m_SandboxEffects; //!< Whether the sandbox's incoming and effects overlay is on (see ShowSandboxEffects).
		bool m_SandboxGas; //!< Whether the sandbox's gas overlay is on (see ShowSandboxGas).
		bool m_SandboxAir; //!< Whether the sandbox's air overlay is on (see ShowSandboxAir).
		bool m_SandboxSimState; //!< Whether the sandbox's sim state readout is on (see ShowSandboxSimState).
		bool m_SandboxBarRight; //!< Whether the right-hand group of the sandbox's bottom bar is shown (see ShowSandboxBarRight).
		int m_SandboxOrdersOverlay; //!< Which units the sandbox orders overlay draws for (see SandboxOrdersOverlay).
		bool m_ShowLightSources; //!< Whether the light sources overlay is on (see ShowLightSources).
		bool m_ShowSunDirection; //!< Whether the sun direction overlay is on (see ShowSunDirection).
		bool m_ShowTerrainUpdates; //!< Whether the terrain update boxes overlay is on (see ShowTerrainUpdates).
		bool m_ShowRecentSolves; //!< Whether the recent path solves overlay is on (see ShowRecentSolves).
		int m_CombatOverlay; //!< Which units the combat AI overlay draws for (see CombatOverlay).
		bool m_ShowSquadLinks; //!< Whether the squad links and trails overlay is on (see ShowSquadLinks).
		bool m_ShowOrderLabels; //!< Whether the order labels overlay is on (see ShowOrderLabels).
		unsigned m_DebugChannels; //!< The debug text channels ticked in the settings, a bit per DebugChannel.
		bool m_TraceAllUnits; //!< Whether the AI channels trace every unit (see TraceAllUnits).
		bool m_BodyGearSettlesAsScraps; //!< Whether what came off a unit settles into the terrain as Flesh Scraps (see BodyGearSettlesAsScraps).
		bool m_EnableMantling; //!< Whether actors pull themselves up onto ledges and over low obstacles (players and the AI alike).
		bool m_NoSceneWrap; //!< Whether every scene is played with hard left and right edges instead of wrapping.
		float m_AISuppression; //!< How strongly fire pins units down and shakes them, 0 to 2 (see AISuppression).
		float m_AIDigWillingness; //!< How readily units with a digger tunnel rather than go round, 0 to 2 (see AIDigWillingness).
		float m_AIThreatAvoidance; //!< How much safe-route units keep clear of enemies, 0 to 2 (see AIThreatAvoidance).
		float m_AIRecklessness; //!< How reckless AI units are on the move, 0 to 1 (see AIRecklessness).
		float m_AISpawnDiggerChance; //!< Percentage of units handed a digger as they come into the scene, 0 to 100 (see AISpawnDiggerChance).
		float m_BleedOutChance; //!< Percentage of wounds that keep bleeding rather than stopping, 0 to 100 (see BleedOutChance).
		int m_AISpawnDiggerType; //!< Which digger they're handed: 0 Light, 1 Medium, 2 Heavy, 3 random (see AISpawnDiggerType).
		bool m_AISteadyBeforeJet; //!< Whether AI units steady themselves before they jet (see AISteadiesBeforeJet).
		bool m_AIWaitForFuel; //!< Whether AI units wait for fuel before they jet (see AIWaitsForFuel).
		bool m_EnableCrabBombs; //!< Whether all actors (except Brains and Doors) should be annihilated if a number exceeding the crab bomb threshold is released at once.
		int m_CrabBombThreshold; //!< The number of crabs needed to be released at once to trigger the crab bomb effect.
		bool m_ShowEnemyHUD; //!< Whether the HUD of enemy actors should be visible to the player.
		bool m_ShowUnitTags; //!< Whether each unit's side and health are drawn beside it.
		bool m_EnableSmartBuyMenuNavigation; //!< Whether swapping to equipment mode and back should change active tabs in the BuyMenu.
		bool m_AutomaticGoldDeposit; //!< Whether gold gathered by Actors is automatically added into team funds. False means that gold needs to be manually transported into orbit via Craft.

		std::string m_PlayerNetworkName; //!< Player name used in network multiplayer matches.
		std::string m_NetworkServerAddress; //!< LAN server address to connect to.
		std::string m_NATServiceAddress; //!< NAT punch-through server address.
		std::string m_NATServerName; //!< Server name to use when connecting via NAT punch-through service.
		std::string m_NATServerPassword; //!< Server password to use when connecting via NAT punch-through service.
		bool m_UseExperimentalMultiplayerSpeedBoosts; //!< Turns on/off code changes from topkek that may speed up multiplayer.

		bool m_AllowSavingToBase; //!< Whether editors will allow to select Base.rte as a module to save in.
		bool m_ShowMetaScenes; //!< Show MetaScenes in editors and activities.

		bool m_DisableLuaJIT; //!< Whether to disable LuaJIT or not. Disabling will skip loading the JIT library entirely as just setting 'jit.off()' seems to have no visible effect.
		bool m_EnableLuaDebugging; //!< Whether the Lua debugger mode is enabled or not. This will disable MT and attempt to connect to a debugger on launch.
		int m_RecommendedMOIDCount; //!< Recommended max MOID's before removing actors from scenes.
		int m_SceneBackgroundAutoScaleMode; //!< Scene background layer auto-scaling mode. 0 for off, 1 for fit screen dimensions and 2 for always upscaled to x2.
		bool m_DisableFactionBuyMenuThemes; //!< Whether faction BuyMenu theme support is disabled.
		bool m_DisableFactionBuyMenuThemeCursors; //!< Whether custom cursor support in faction BuyMenu themes is disabled.
		int m_PathFinderGridNodeSize; //!< The grid size used by the PathFinder, in pixels.
		int m_AIUpdateInterval; //!< How often actor's AI should be updated, i.e. every n simulation updates.
		int m_NumberOfLuaStatesOverride; //!< Overrides how many threaded Lua states we'll use. -1 for no override, which defaults to the maximum number of concurrent hardware threads.
		bool m_ForceImmediatePathingRequestCompletion; //!< Whether pathing requests will be forced to immediately complete for the next frame, or if they can take multiple frames to calculate.

		bool m_SkipIntro; //!< Whether to play the intro of the game or skip directly to the main menu.
		bool m_ShowToolTips; //!< Whether ToolTips are enabled or not.
		bool m_DisableLoadingScreenProgressReport; //!< Whether to display the reader progress report during module loading or not. Greatly increases loading speeds when disabled.
		int m_LoadingScreenProgressReportPrecision; //!< How accurately the reader progress report tells what line it's reading during module loading. Lower values equal more precision at the cost of loading speed.
		float m_MenuTransitionDurationMultiplier; //!< Multiplier value for the transition durations between different menus. Lower values equal faster transitions.

		bool m_DrawAtomGroupVisualizations; //!< Whether to draw MOSRotating AtomGroups to the Scene MO color Bitmap.
		bool m_DrawHandAndFootGroupVisualizations; //!< Whether to draw Actor HandGroups and FootGroups to the Scene MO color Bitmap.
		bool m_DrawLimbPathVisualizations; //!< Whether to draw Actor LimbPaths to the Scene MO color Bitmap.
		bool m_PrintDebugInfo; //!< Print some debug info in console.
		bool m_MeasureModuleLoadTime; //!< Whether to measure the duration of data module loading (extraction included). For benchmarking purposes.

		std::list<std::string> m_VisibleAssemblyGroupsList; //!< List of assemblies groups always shown in editors.
		std::unordered_map<std::string, bool> m_DisabledMods; //!< Map of the module names we disabled.
		std::unordered_map<std::string, bool> m_EnabledGlobalScripts; //!< Map of the global script names we enabled.

	private:
		/// Reads one property, letting std::stoi and std::stof throw on a bad value (ReadProperty catches it).
		int ReadPropertyUnchecked(const std::string_view& propName, Reader& reader);

		static const std::string c_ClassName; //!< A string with the friendly-formatted type name of this.

		std::string m_SettingsPath; //!< String containing the Path to the Settings.ini file.
		mutable std::string m_LastWrittenSettings; //!< The settings file's text as this game last wrote it, to tell when a setting has changed since.
		std::string m_StartupPreset; //!< The preset loaded every time the game starts, over Settings.ini. Nothing for none.

		/// Clears all the member variables of this SettingsMan, effectively resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		SettingsMan(const SettingsMan& reference) = delete;
		SettingsMan& operator=(const SettingsMan& rhs) = delete;
	};
} // namespace RTE
