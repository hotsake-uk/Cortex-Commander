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

		/// Saves every setting that can be tuned while the game runs (the look, time and weather, water, fire, falling ground) as a named preset, a file in Userdata/Presets.
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

		/// Writes the settings that can be tuned while the game runs: what a preset holds, and part of the settings file.
		void SaveTunables(Writer& writer, const struct LightingSettings& lighting) const;
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

		/// Gets whether the recent path solves overlay is on: the debug team's last few routes found, each step with its kind and cost. Not saved.
		/// While it's on the path finder keeps those routes, which costs a little time per search.
		bool ShowRecentSolves() const { return m_ShowRecentSolves; }

		/// Sets whether the recent path solves overlay is on.
		void SetShowRecentSolves(bool show) { m_ShowRecentSolves = show; }

		/// Gets whether the squad links and trails overlay is on: for inspected squad units, the leader-to-follower line, the leader's trail and each follower's place in line (drawn by the AI scripts).
		bool ShowSquadLinks() const { return m_ShowSquadLinks; }

		/// Sets whether the squad links and trails overlay is on.
		void SetShowSquadLinks(bool show) { m_ShowSquadLinks = show; }

		/// Sets what the navigation debug overlay shows; see NavDebugOverlay.
		/// @param level 0 to 3.
		void SetNavDebugOverlay(int level) { m_NavDebugOverlay = std::clamp(level, 0, 3); }

		/// Sets whether actors mantle ledges and vault low obstacles.
		void SetMantlingEnabled(bool enable) { m_EnableMantling = enable; }

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
		bool m_EndlessMetaGameMode; //!< Endless MetaGame mode.
		bool m_ShowFPSAndVersion; //!< Whether the frame rate and version are shown in the top right (see ShowFPSAndVersion).
		int m_NavDebugOverlay; //!< What the navigation debug overlay shows (see NavDebugOverlay).
		int m_DebugTeam; //!< The team whose view the debug overlays show (see DebugTeam).
		int m_UnitInspector; //!< Which units the unit inspector overlay labels (see UnitInspector).
		bool m_ShowRecentSolves; //!< Whether the recent path solves overlay is on (see ShowRecentSolves).
		int m_CombatOverlay; //!< Which units the combat AI overlay draws for (see CombatOverlay).
		bool m_ShowSquadLinks; //!< Whether the squad links and trails overlay is on (see ShowSquadLinks).
		unsigned m_DebugChannels; //!< The debug text channels ticked in the settings, a bit per DebugChannel.
		bool m_TraceAllUnits; //!< Whether the AI channels trace every unit (see TraceAllUnits).
		bool m_EnableMantling; //!< Whether actors pull themselves up onto ledges and over low obstacles (players and the AI alike).
		bool m_EnableCrabBombs; //!< Whether all actors (except Brains and Doors) should be annihilated if a number exceeding the crab bomb threshold is released at once.
		int m_CrabBombThreshold; //!< The number of crabs needed to be released at once to trigger the crab bomb effect.
		bool m_ShowEnemyHUD; //!< Whether the HUD of enemy actors should be visible to the player.
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
		static const std::string c_ClassName; //!< A string with the friendly-formatted type name of this.

		std::string m_SettingsPath; //!< String containing the Path to the Settings.ini file.

		/// Clears all the member variables of this SettingsMan, effectively resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		SettingsMan(const SettingsMan& reference) = delete;
		SettingsMan& operator=(const SettingsMan& rhs) = delete;
	};
} // namespace RTE
