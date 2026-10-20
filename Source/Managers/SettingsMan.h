#pragma once

#include "Serializable.h"
#include "Singleton.h"
#include "LightingSettings.h"

#include <list>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
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

		/// What a preset holds: every setting, or only one of the settings panel's two groups. Each setting the panel saves is in one of the groups or
		/// in Session (what is only for the moment or about the tool windows: game speed, the AI paused, debug views, the panels' layout), which only a full preset holds.
		enum class PresetGroup {
			Everything,
			Gameplay,
			Visual,
			Session
		};

		/// Gets a group's name as the settings panel shows it.
		static const char* PresetGroupName(PresetGroup group);

		/// Saves the settings in the settings panel as a named preset: every one of them (a file in Userdata/Presets), or only one group's (in Userdata/Presets/Gameplay or Visual).
		/// @param name The name. Characters that can't be in a file's name are dropped.
		/// @param group Everything, Gameplay or Visual.
		/// @return The name it was saved under, or nothing if it couldn't be.
		std::string SavePreset(const std::string& name, PresetGroup group = PresetGroup::Everything) const;

		/// Loads a preset saved by SavePreset over the settings as they are.
		/// @param from Which presets it is one of: Everything, Gameplay or Visual.
		/// @param part Only this group's settings from it (Gameplay or Visual), the rest left as they are; Everything for all it holds.
		/// @return Whether there was one of that name.
		bool LoadPreset(const std::string& name, PresetGroup from = PresetGroup::Everything, PresetGroup part = PresetGroup::Everything);

		/// Deletes a preset.
		/// @return Whether there was one of that name.
		bool DeletePreset(const std::string& name, PresetGroup group = PresetGroup::Everything) const;

		/// Gets the names of the presets there are of a kind (Everything, Gameplay or Visual), in order.
		std::vector<std::string> ListPresets(PresetGroup group = PresetGroup::Everything) const;

		/// Saves a screen style's filters (LightingSettings::RetroLayers) as the player's own variant, a file in Userdata/FilterPresets apart from the
		/// whole-settings presets. Gets the file name it was saved under (the name made safe), empty if it couldn't be.
		std::string SaveFilterPreset(const std::string& name, const LightingSettings::RetroLayers& layers) const;

		/// Loads a filter preset saved by SaveFilterPreset into layers. False if there is none of that name.
		bool LoadFilterPreset(const std::string& name, LightingSettings::RetroLayers& layers) const;

		/// Deletes a filter preset. False if there was none of that name.
		bool DeleteFilterPreset(const std::string& name) const;

		/// Gets the names of the filter presets saved, sorted.
		std::vector<std::string> ListFilterPresets() const;

		/// Gets the name a filter preset of this name is saved under (made safe for a file name), empty if none can be.
		std::string FilterPresetName(const std::string& name) const;

		/// Gets the preset loaded every time the game starts, over Settings.ini: the full one, or a group's, loaded after it.
		/// @return The preset's name, or nothing for none.
		const std::string& GetStartupPreset(PresetGroup group = PresetGroup::Everything) const { return m_StartupPresets[StartupSlot(group)]; }

		/// Sets the preset loaded every time the game starts. Kept in Settings.ini.
		/// @param name The preset's name, or nothing for none.
		void SetStartupPreset(const std::string& name, PresetGroup group = PresetGroup::Everything);

		/// Loads the presets set to load at start, if there are any: the full one, then the gameplay and visual ones over it. Call once at start-up, after the data modules are loaded.
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

		/// Gets whether the Kingdom (Medieval.rte) and Fell Legion (FellLegion.rte) units are switched off: the two stop being factions and their units can't be bought.
		bool MedievalUnitsDisabled() const { return m_DisableMedievalUnits; }

		/// Sets whether the Kingdom and Fell Legion units are switched off. Takes effect from the next activity.
		void SetMedievalUnitsDisabled(bool disable) { m_DisableMedievalUnits = disable; }

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

		/// Gets whether a click of the right button opens the unit menu and leaves it up till you click away (true), rather than it being up
		/// only while the button is held (false).
		bool UnitMenuClickToOpen() const { return m_UnitMenuClickToOpen; }

		/// Sets UnitMenuClickToOpen; see there.
		void SetUnitMenuClickToOpen(bool click) { m_UnitMenuClickToOpen = click; }

		/// Gets how fast time runs while the unit menu is up, 0.05 to 1 (1: as fast as ever).
		float UnitMenuTimeScale() const { return m_UnitMenuTimeScale; }

		/// Sets UnitMenuTimeScale; see there.
		void SetUnitMenuTimeScale(float scale) { m_UnitMenuTimeScale = std::clamp(scale, 0.05F, 1.0F); }
		/// Gets the key that pauses and resumes the AI in any game, as written: modifiers then SDL's name for the key, e.g. "Ctrl+P". Empty for none.
		const std::string& GetPauseAIKey() const { return m_PauseAIKey; }

		/// Sets the key that pauses and resumes the AI; see GetPauseAIKey.
		void SetPauseAIKey(const std::string& key) { m_PauseAIKey = key; }

		/// The BackgroundSet (module and preset name) put behind every map played fresh in place of its own backgrounds, or empty for each map's own.
		const std::string& GetDefaultBackground() const { return m_DefaultBackground; }

		/// Sets the BackgroundSet put behind every map played fresh; see GetDefaultBackground.
		void SetDefaultBackground(const std::string& backgroundSet) { m_DefaultBackground = backgroundSet; }

		/// Whether a key press (an SDL_Scancode with the SDL_Keymod held) is the key that pauses and resumes the AI. Left and right modifiers count alike.
		bool IsPauseAIKey(int scancode, unsigned int modifiers) const;

		/// The name for a key press as GetPauseAIKey writes it, e.g. "Ctrl+Shift+F10". Empty for a modifier on its own.
		static std::string KeyComboName(int scancode, unsigned int modifiers);

		/// Whether the settings panel is waiting for a key to bind, so the keys pressed for it do nothing else. Not saved.
		bool IsTakingKey() const { return m_TakingKey; }

		/// Sets IsTakingKey; see there.
		void SetTakingKey(bool taking) { m_TakingKey = taking; }

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

		/// Whether creatures that climb (a giant spider, an ooze; Actor::GetClimbing) crawl up walls and along ceilings, and are routed that way.
		/// Off, they keep to the ground as other units do.
		bool CreaturesClimbWalls() const { return m_CreaturesClimbWalls; }

		/// How hard a mech's footfalls shake the screen near them (MK-1), as a multiple of its own: 0 for none, 1 as designed.
		float MechFootfallShake() const { return m_MechFootfallShake; }

		/// How fast climbing creatures crawl on walls and ceilings, as a multiple of their own speed (Actor::GetCrawlSpeed); 1 as they come.
		float CreatureClimbSpeed() const { return m_CreatureClimbSpeed; }

		/// Whether creatures that fly (a bat, a wyvern, a wisp; Actor::GetFlight) take to the air, and are routed through it.
		/// @return Whether they fly.
		bool CreaturesFly() const { return m_CreaturesFly; }

		/// How fast flying creatures fly, as a multiple of their own speed (Actor::GetFlySpeed); 1 as they come.
		float CreatureFlySpeed() const { return m_CreatureFlySpeed; }

		/// Whether an AI humanoid stuck on the ground, its own moves spent, is given a small nudge free (off by default).
		bool StuckPushEnabled() const { return m_EnableStuckPush; }

		/// Whether everything that came off a unit and isn't flesh or bone (metal plating, gear, robot parts) settles into the terrain as Flesh Scraps,
		/// keeping its colours, as the flesh does (see Material::GetTerrainSettleMaterial).
		bool BodyGearSettlesAsScraps() const { return m_BodyGearSettlesAsScraps; }

		/// Sets whether what came off a unit settles as Flesh Scraps. Remains already in the terrain stay as they settled.
		void SetBodyGearSettlesAsScraps(bool enable) { m_BodyGearSettlesAsScraps = enable; }

		/// Whether humanoids (AHumans) die as whole ragdolls (off as the game comes): a hit or wound that would blow one apart kills it instead,
		/// and its head and limbs stay on while the body falls limp, its hands and feet swinging under their own weight and catching on the
		/// ground, until it all lies still and settles. Only a hit past RagdollBlowApartScale times the part's limit still breaks it.
		bool RagdollDeaths() const { return m_RagdollDeaths; }

		/// Sets whether humanoids die as whole ragdolls; see RagdollDeaths.
		void SetRagdollDeaths(bool enable) { m_RagdollDeaths = enable; }

		/// Whether humanoids (AHumans) are never dismembered (off as the game comes): no hit, blast or wound, however big, takes off a head, arm,
		/// leg or foot or blows the body apart, alive or dead. One that would have done so to the torso or head kills the unit instead.
		bool NoDismemberment() const { return m_NoDismemberment; }

		/// Sets whether humanoids are never dismembered; see NoDismemberment.
		void SetNoDismemberment(bool enable) { m_NoDismemberment = enable; }

		/// With RagdollDeaths on, how long, in seconds, a humanoid's body stays a body after it dies before it may settle into the terrain, 0 to 600:
		/// till then it lies about whole, to be pushed, shot and blown about, however still. 0: as soon as it lies still, as the game comes.
		int RagdollBodySeconds() const { return m_RagdollBodySeconds; }

		/// Sets how long a ragdoll's body stays before it may settle; see RagdollBodySeconds.
		void SetRagdollBodySeconds(int seconds) { m_RagdollBodySeconds = std::clamp(seconds, 0, 600); }

		/// With RagdollDeaths on, how many times a body part's own gib or joint limit a single hit has to be to still blow it apart or off, 1.5 to 10.
		float RagdollBlowApartScale() const { return m_RagdollBlowApartScale; }

		/// Sets how big a hit still blows a ragdolling body apart; see RagdollBlowApartScale.
		void SetRagdollBlowApartScale(float scale) { m_RagdollBlowApartScale = std::clamp(scale, 1.5F, 10.0F); }

		/// The gib chunk settings ("Gibs" in the settings panel): the solid pieces (plain MOSRotatings, not units or what was attached to the
		/// thing) a body or wreck breaks into when it gibs, which tumble about and cost far more to move than a pixel of blood. Each member's
		/// default is how the game has always behaved, so GibChunkTuning() is the reset.
		struct GibChunkTuning {
			int CountPercent = 100; //!< How many of each gib's chunks come out, in percent of what its preset says, 0 to 200. 100: all of them.
			int MaxLoose = 0; //!< The most chunks loose at once, 0 to 5000; past it the oldest that have slowed settle where they lie (see MovableMan::Update). 0: no limit.
			int SettleMS = -1; //!< How long a chunk lies still before it settles, in ms, 0 to 10000. -1: each preset's own RestThreshold.
			int MaxBounces = 10; //!< The most hits a chunk takes in one update before it stops moving for that update, 1 to 10 (10 for everything else).
			int AtomSpacing = 0; //!< Pixels between the collision points round a new chunk's outline, 1 to 16. 0: each preset's own AtomGroup Resolution.
			bool SettleOnBodies = false; //!< Whether a chunk lying still on a dead body or another chunk, rather than on the ground, settles there (never on a living unit).
		};

		/// Gets the gib chunk tuning, to read or change ("Gibs" settings).
		GibChunkTuning& GibChunks() { return m_GibChunks; }

		/// Clamps every gib chunk setting into its range (after reading them).
		void ClampGibChunks();

		/// The AI combat settings ("AI combat" on the AI behaviour page of the settings panel): how well AI units shoot, how quickly they react
		/// and spot, how far they look, how long they remember a target, how readily they throw and how long they hold the trigger. Each
		/// scales what the AI scripts already work out (team AI skill, the unit's own overrides, veteran rank); at its default the scripts
		/// skip it, so AICombatTuning() is the reset and plays exactly as the game always has.
		struct AICombatTuning {
			float Accuracy = 1.0F; //!< How well they aim, 0.25 to 4: their aim error is divided by it. 1: as the game comes.
			float Reaction = 1.0F; //!< How long they take to fire and settle their aim, 0.25 to 3: those delays are multiplied by it. 1: as the game comes.
			float Spotting = 1.0F; //!< How quickly they notice an enemy in view, 0.25 to 3: the rate noticing fills is multiplied by it. 1: as the game comes.
			float SightRange = 1.0F; //!< How far they look for enemies, 0.5 to 2: the scan's reach is multiplied by it. 1: as the game comes.
			float TargetMemory = 1.0F; //!< How long they keep on a target after losing sight of it, 0.25 to 4: each behaviour's own time (about a second while shooting) is multiplied by it. 1: as the game comes.
			float GrenadeUse = 1.0F; //!< The chance, 0 to 1, they take a grenade or thrown weapon throw the AI judges worth making. 1: every one, as the game comes.
			float Burst = 1.0F; //!< How long an automatic stays on the trigger in each burst, 0.5 to 2. 1: as the game comes.
			float Hearing = 1.0F; //!< How far they hear gunfire, blasts and footsteps, 0.25 to 3: the hearing range is multiplied by it. 1: as the game comes.
			bool InvestigateNoises = true; //!< Whether a unit that hears fighting it can't see (and isn't a sentry or under orders) goes a short way toward it to look.
		};

		/// Gets the AI combat tuning, to read or change ("AI combat" settings).
		AICombatTuning& AICombat() { return m_AICombat; }

		/// Clamps every AI combat setting into its range (after reading or changing them).
		void ClampAICombat();

		/// Each AI combat setting on its own, for the AI scripts (see AICombatTuning).
		float AICombatAccuracy() const { return m_AICombat.Accuracy; }
		float AICombatReaction() const { return m_AICombat.Reaction; }
		float AICombatSpotting() const { return m_AICombat.Spotting; }
		float AICombatSightRange() const { return m_AICombat.SightRange; }
		float AICombatTargetMemory() const { return m_AICombat.TargetMemory; }
		float AICombatGrenadeUse() const { return m_AICombat.GrenadeUse; }
		float AICombatBurst() const { return m_AICombat.Burst; }
		float AICombatHearing() const { return m_AICombat.Hearing; }
		bool AICombatInvestigateNoises() const { return m_AICombat.InvestigateNoises; }
		/// Whether a unit sent somewhere goes after only enemies near its way or the place it was sent (Battle Behavior's Stay on task).
		bool AIStayOnTask() const { return m_DirectOrders.StayOnTask; }

		/// The late-battle slowdown fixes ("Performance" in the settings panel), each with a switch so it can be tried against the old behaviour: things a battle
		/// leaves behind (blood pools, wrecks, debris, gas, scattered terrain changes) that kept costing every frame after the fighting, all actors gone or not.
		struct SlowdownFixTuning {
			bool BigMaps = true; //!< The big-map speedups, all together: the lighting works only where it can be seen or the terrain changed instead of over the whole map every frame, the smoke grid clears only what it used, gib chunks are limited, and path searches give up early on places there's no way to. Off: all of it as before.
			bool LiquidTellsOnlyMoves = true; //!< Liquid tells the path grid only where it moved, in separate boxes; off: every pixel coming to rest again, all in one box (re-sampling the grid over every pool on the map, for ever).
			int WreckSettleSeconds = 20; //!< How long a vehicle's or ship's wreck lies still before it settles into the terrain as its own material, 0 to 600. 0: never (it stays a live object, as before).
			bool DebrisRestsAcrossGaps = true; //!< A piece lying still looks for ground under both sides as well as its middle, so one across a crack or crater lip settles; off: the middle only.
			bool GasAreaShrinks = true; //!< The gas grid's worked-out area draws in to where gas still is; off: it only grows until no gas is left anywhere.
			bool LightingSplitsChanges = true; //!< The lighting handles each changed patch of terrain on its own; off: every patch changed in a frame as one box round them all.
			int MaxLooseBits = 2000; //!< The most loose bits (pixels and small sprite particles that never expire: blood, flecks, chips) at once, 0 to 50000; past it the oldest that have slowed settle where they lie, or are removed where they can't (see MovableMan::Update). 0: no limit.
			std::array<int, 6> MaxLooseBitsOf{200, 0, 0, 0, 0, 0}; //!< The most loose bits of each kind of material (see LooseBitKind) at once, 0 to 50000, each within the overall most. 0: no limit of its own. Blood & flesh 200 by default (blood was most of what piled up), the rest none.
		};

		/// The kinds of material loose bits are limited by, each on its own as well as all together (SlowdownFixTuning::MaxLooseBitsOf).
		enum LooseBitKind { BloodAndFlesh, Liquids, EarthAndStone, Metal, WoodAndPlants, OtherBits, LooseBitKindCount };
		static constexpr const char* c_LooseBitKindNames[LooseBitKindCount] = {"Blood & flesh", "Liquids", "Earth & stone", "Metal", "Wood & plants", "Everything else"};

		/// Which kind a loose bit of a material is, by the material's name (looked up once per material).
		/// @param materialIndex The material's index, 0 to 255.
		static LooseBitKind GetLooseBitKind(int materialIndex);

		/// Gets the late-battle slowdown fixes, to read or change.
		SlowdownFixTuning& SlowdownFixes() { return m_SlowdownFixes; }

		/// Whether every scene is played without wrapping horizontally, with hard left and right edges, whatever its terrain says (see Scene::LoadData).
		/// Takes effect when a scene is next loaded.
		bool NoSceneWrap() const { return m_NoSceneWrap; }

		/// The kinds of actor that each have their own toughness (see Toughness).
		enum class ToughnessKind {
			Units, //!< Units on foot.
			Vehicles, //!< Ground vehicles: wheeled vehicles and the crab-legged bodies that aren't animals (tanks, dreadnoughts, turrets).
			Craft, //!< Flying craft: drop ships, rockets and flying ships.
			Animals, //!< Livestock and wildlife (Actor::IsAnimal).
			Creatures, //!< Monsters (Actor::IsCreature, CR-1).
			Mechs, //!< Mechs (AMech, MK-1): big walking machines such as the Imperial Knight.
			Count
		};

		static constexpr float c_ToughnessMin = 0.1F; //!< The least a toughness can be: ten times the damage.
		static constexpr float c_ToughnessMax = 10.0F; //!< The most a toughness can be before it is invincible.
		static constexpr float c_ToughnessInvincible = 20.0F; //!< The toughness that means invincible, past c_ToughnessMax.

		/// How tough one kind of actor is: 1 as designed, 2 takes half the damage and needs twice the wounds and hits to be blown apart, 0.5
		/// the other way round, and c_ToughnessInvincible takes no damage and is never blown apart (Actor::ApplyToughness, GibToughness).
		float Toughness(ToughnessKind kind) const { return m_Toughness[static_cast<int>(kind)]; }

		/// Whether one kind of actor is set to be invincible.
		bool IsInvincible(ToughnessKind kind) const { return Toughness(kind) >= c_ToughnessInvincible; }

		/// Sets how tough one kind of actor is, c_ToughnessMin to c_ToughnessMax, and anything past that is invincible.
		void SetToughness(ToughnessKind kind, float toughness) { m_Toughness[static_cast<int>(kind)] = toughness > c_ToughnessMax ? c_ToughnessInvincible : std::max(toughness, c_ToughnessMin); }

		/// How strongly fire pins units down and shakes them (Actor::GetSuppression and GetMorale): 0 for not at all, 1 as designed, 2 double.
		float AISuppression() const { return m_AISuppression; }

		/// How readily units with a digger tunnel through ground rather than go round it (PathFinder's dig edges): 0 only when there is no
		/// other way (each node dug priced at the material's integrity, as before), 1 as designed (a short cut through soft ground beats a long
		/// way round), 2 twice as readily.
		float AIDigWillingness() const { return m_AIDigWillingness; }

		/// Whether units dig through ground that only their digger's tracers cut (the stronger round some diggers fire one in several: the Heavy
		/// Digger's cuts Metal): routed through it, priced as that many times slower, where going round is far or there's no way round. Off,
		/// they dig only what the regular rounds cut. On by default.
		bool AIDigsWithTracers() const { return m_AIDigsWithTracers; }

		/// How much the routes of units a game mode wants kept safe (Actor::GetRouteThreatAvoidance: a capture the flag carrier) keep clear of
		/// enemies (PathFinder::ThreatCost): 0 not at all, 1 as designed (a way past a crowd of enemies loses to a longer one past none; one
		/// sentry is skirted only when going round is short), 2 twice as much. Other units always take the shortest way.
		float AIThreatAvoidance() const { return m_AIThreatAvoidance; }
		/// How reckless AI units are on the move, 0 (careful) to 1 (reckless); 0.5 is as designed. It scales how long a unit steadies itself
		/// before it jets, how much fuel it waits for, and how much the route search shies from hard jumps and long drops (see AIMoveCaution).
		float AIRecklessness() const { return m_AIRecklessness; }
		/// How long, in seconds, an AI unit climbing a ladder that has stopped rising waits before letting go and finding another way (the rungs
		/// shot away above, the ladder ending short, the way up it blocked). 1.5 s as the game comes.
		float AILadderGiveUpSeconds() const { return m_AILadderGiveUpSeconds; }
		/// Whether the route search and the AI's walk keep to the climbs a body can make (on as the game comes): stairs only where the slope has
		/// footholds (spots no steeper than about 50 degrees every few pixels of height), a scramble only up a face with no sheer section taller
		/// than a step and for a unit with a hand free to climb with, no walk step over a near-vertical lip hidden in a 40 degree cell, and a
		/// stairs or scramble climb that stops gaining height given up on and routed round. Off: as before (any 51 to 68 degree rise was
		/// stairs, any soft 68 to 73 degree face a scramble). See PathFinder::UpdateNodeCosts and AHuman::UpdateAIMotor.
		bool StrictClimbLimits() const { return m_StrictClimbLimits; }
		/// Whether a humanoid's route search and walk size it by its measured body (the top of its head and torso atoms down to its feet, and
		/// their width) rather than by its CharHeight in fractions tuned on the Soldier Light (off as the game comes). A mod unit whose CharHeight
		/// doesn't match its body is then routed and walked for the body it has. Movement only: the HUD, aim and the rest keep CharHeight. See
		/// Actor::GetMoveHeight and AHuman::MeasureBody.
		bool MeasuredBodySize() const { return m_MeasuredBodySize; }
		/// How much what a unit carries (its inventory, the guns in its hands and its gold) weighs, as a multiple of the items' own mass,
		/// 0 to 5; 1 as the game comes (Actor::GetMass).
		float CarriedWeightMultiplier() const { return m_CarriedWeightMultiplier; }
		/// How hard every unit's limbs push along their paths walking, crawling and climbing, and how high a leap of the legs goes, 0.25 to 3;
		/// 1 as the game comes.
		float LegStrength() const { return m_LegStrength; }
		/// Whether jetpacks raise their thrust for a unit's load, burning fuel faster: 0 as each jetpack says (AdjustsThrottleForWeight),
		/// 1 always, 2 never.
		int JetpackWeightCompensation() const { return m_JetpackWeightCompensation; }
		/// How units' own senses treat liquid in the terrain (the walk's look ahead, the jet pilot, leaps, getting up, flights' clear way): 0 as
		/// terrain, as before (wading read as a step, a stream pouring off a ledge as a wall or a ceiling); 1 as the open water bodies already
		/// move through (FluidSim::UnitsPassThrough); 2 that, and the path grid wades every flowing liquid, mud, tar, mercury and fuel among
		/// them, as it does oil (it knew only water, oil, acid and lava, and took the rest for ground). 2 as the game comes.
		int UnitWaterBehaviour() const { return m_UnitWaterBehaviour; }
		/// Whether the AI's walk sees loose bodies its own collides with in the way (a corpse, a gib, a limb: AHuman::ObjectAhead) and leaps
		/// them, hops them with the jet, routes round or waits, as it does a wreck. Off as before: it pressed into them until the stuck handling.
		bool UnitsStepOverObjects() const { return m_UnitsStepOverObjects; }
		/// Whether the AI's walk gets over a landed craft (a dropship or rocket set down) or a parked vehicle in its way, not only round it: it
		/// leaps up onto a low one and walks across, hops it with the jet, or pulls itself up onto it and walks across, and keeps trying a
		/// little before giving up. Off as before: landed craft unseen, and a vehicle only leapt clean over or hopped. On as the game comes.
		bool UnitsCrossVehicles() const { return m_UnitsCrossVehicles; }
		/// Whether an AI soldier whose walk is held up by a loose object (a corpse, a gib, a crate) no heavier than itself shoves it along out of
		/// the way (AHuman::PushObjectAhead), before stepping over it or going round. On as the game comes.
		bool UnitsPushObjects() const { return m_UnitsPushObjects; }
		/// How hard units shove loose objects out of their way, times each unit's own ObjectPushStrength: 1 as the game comes, 0 to 3.
		float ObjectPushStrength() const { return m_ObjectPushStrength; }
		/// Whether units' own senses (the walk's look ahead, getting up, leaps, landings, mantling, ducking) see through what their bodies push
		/// through: grass and soft plants (integrity 5 or under, not flowing). Off as before: grass read as ground over the dirt the floor
		/// rays find, and units ducked under hanging plants and mantled onto bushes. See Actor::SensesPassTerrain.
		bool SensesPassPlants() const { return m_SensesPassPlants; }
		/// Whether a living soldier's senses see through ladder rungs, which its body passes: off as before, they were floor under it in mid air,
		/// a wall beside a walk and in the way of a flight, and a lip to mantle onto.
		bool SensesPassRungs() const { return m_SensesPassRungs; }
		/// Whether units' senses see through specks, the crumbs of four pixels or fewer their bodies brush through (Atom::IsTerrainSpeck): off as
		/// before, they ducked, crawled and re-routed round crumbs left by blasts and digging, and mantled against them.
		bool SensesPassSpecks() const { return m_SensesPassSpecks; }
		/// How much a load slows a unit's walk and lowers its leap, 0 to 1 (0 as the game comes): this share of its speed is lost once what it
		/// carries weighs as much as its own body, less below that (Actor::GetOverloadSpeedMultiplier).
		float OverloadSlowdown() const { return m_OverloadSlowdown; }
		/// How much fuel every jetpack's tank holds, as a multiple of its own JumpTime, 0.25 to 5; 1 as the game comes.
		float JetpackFuelMultiplier() const { return m_JetpackFuelMultiplier; }
		/// Whether every jetpack's tank stays full however long it fires (AEJetpack::UpdateBurstState); off as the game comes.
		bool InfiniteJetpackFuel() const { return m_InfiniteJetpackFuel; }
		/// How many times units of a team must get stuck on the same ladder within a minute before the whole team avoids it, 1 to 5 (2 as the game
		/// comes). Fewer, the unit stuck avoids it alone.
		int AILadderTeamStalls() const { return m_AILadderTeamStalls; }
		/// Whether AI units take hold of a ladder only when they mean to climb it (AHuman::GrabLadder), on as the game comes. Off: as a player
		/// does, whenever up or down is pressed at one.
		bool AIGrabsLaddersOnPurpose() const { return m_AIGrabsLaddersOnPurpose; }
		/// Whether an AI unit meaning to climb a ladder can take hold of it with its jetpack lit (cutting the jet), on as the game comes.
		bool AIGrabsLaddersMidJet() const { return m_AIGrabsLaddersMidJet; }
		/// Whether AI units pull themselves up onto ledges and over obstacles, or catch ledges in the air, only when they mean to (AHuman::WantMantle).
		bool AIMantlesOnPurpose() const { return m_AIMantlesOnPurpose; }
		/// Whether an AI unit whose flight comes up short beside its landing catches the landing's lip in the air.
		bool AICatchesLedgesMidAir() const { return m_AICatchesLedgesMidAir; }
		/// Whether risky orders fly past ladders a careful route climbs: they jet up a background ladder's shaft when the tank makes it, and the
		/// route search prices a ladder climb dearer as their flights are cheaper. On as the game comes.
		bool RiskyOrdersFlyPastLadders() const { return m_RiskyOrdersFlyPastLadders; }
		/// Whether humanoid units take hold of ropes (RopeSim): in the air with up or the jet held, or pressing up under one, they catch it
		/// and hang from it, swinging on the rope's own physics (AHuman::UpdateRopeHold). On as the game comes.
		bool UnitsGrabRopes() const { return m_UnitsGrabRopes; }
		/// Whether an AI unit falling with no jet to save it catches a rope it falls past, and climbs it to the nearest floor. On as the game comes.
		bool AICatchesRopes() const { return m_AICatchesRopes; }
		/// How fast units climb a rope hand over hand, as a multiple of the normal pace, 0.25 to 3.
		float RopeClimbSpeed() const { return m_RopeClimbSpeed; }

		/// Gets the percentage, 0 to 100, of units that are handed a digger when they come into the scene without one.
		float AISpawnDiggerChance() const { return m_AISpawnDiggerChance; }

		/// Gets the percentage, 0 to 100, of wounds (hits, and limbs torn off) that keep bleeding until the unit bleeds out or is patched up,
		/// rather than stopping when their own definition says. 0, the default, is every wound as it is defined.
		float BleedOutChance() const { return m_BleedOutChance; }

		/// Gets how long wounds that do stop bleeding bleed for, as a multiple of their own definition: 2 bleeds twice as long and so
		/// does twice the damage, 0.5 half. 1, the default, is every wound as it is defined. Wounds kept bleeding by BleedOutChance never stop.
		float WoundBleedTime() const { return m_WoundBleedTime; }

		/// Gets the gap, in pixels, a flying ship flown by its autopilot keeps between the underside of its hull and the ground when it is
		/// sent down low, on its way and where it stops. 12, the default, is just clear of it.
		float ShipGroundClearance() const { return m_ShipGroundClearance; }

		/// Gets how far, in pixels, flying ships may go past the top of the map, and past its sides where it doesn't wrap, before they count as
		/// gone to orbit: room to fly over and drop people onto buildings at the very top or ends of the map. Other craft are unaffected. 400 is
		/// the default; 0 is the old line, just over the edge.
		float ShipGraceZone() const { return m_ShipGraceZone; }

		/// Gets how far, in pixels, the camera may scroll past the top, sides and bottom of a map that doesn't wrap. 400 is the default.
		float CameraOverscroll() const { return m_CameraOverscroll; }

		/// Gets how hard weapon fire (bullets, shrapnel and the other particles that hit units) strikes terrain and buildings, as a multiple
		/// of the force it would: the impulse it meets each terrain pixel with is scaled by this, so it cuts stronger materials and slows less
		/// going through them. 1, the default, is every weapon as it is defined.
		float WeaponTerrainDamage() const { return m_WeaponTerrainDamage; }

		/// Gets how much damage weapon fire (bullets, shrapnel and the other particles that hit units) does to the units, vehicles and craft it
		/// hits, as a multiple of what it would: the wounds it leaves (their hit and their bleeding) and the flat damage it carries are scaled by
		/// this. 1, the default, is every weapon as it is defined.
		float WeaponUnitDamage() const { return m_WeaponUnitDamage; }

		/// Gets whether WeaponUnitDamage also scales how soon units are blown apart or lose limbs: their wound and blow limits are divided by it.
		bool WeaponUnitDamageGibs() const { return m_WeaponUnitDamageGibs; }

		/// Gets which digger those units are handed: 0 Light, 1 Medium, 2 Heavy, 3 a random one of the four, 4 Mega.
		int AISpawnDiggerType() const { return m_AISpawnDiggerType; }

		/// The recklessness as a multiplier on the AI's movement caution: 2 at the careful end, 1 as designed, 0.5 at the reckless end.
		float AIMoveCaution() const { return std::pow(2.0F, (0.5F - m_AIRecklessness) * 2.0F); }

		/// Whether AI units stand still and upright before a jetpack climb or jump (on, as designed), or take off mid-stride.
		bool AISteadiesBeforeJet() const { return m_AISteadyBeforeJet; }
		/// Take cover with no cover near: whether a unit carrying a digger digs a foxhole where it stands.
		bool AICoverDigs() const { return m_AICoverDigs; }
		/// Take cover with no cover near: whether a unit falls back away from the enemy when it is close or the unit is pinned down.
		bool AICoverFallsBack() const { return m_AICoverFallsBack; }
		/// Take cover with no cover near: whether a unit lies flat where it is (else it crouches).
		bool AICoverLiesFlat() const { return m_AICoverLiesFlat; }
		/// Whether a unit whose nerve has gone (morale under 0.3) breaks and falls back, whatever its health.
		bool AIBreaksWhenShaken() const { return m_AIBreaksWhenShaken; }
		/// Whether an AI unit takes an enemy its team saw in the last few seconds (a teammate, a ship's gunner, a craft looking out for its
		/// riders) as partly or wholly noticed already, rather than noticing it from scratch (see AISharedSpottingHeadStart).
		bool AISharedSpotting() const { return m_AISharedSpotting; }
		/// How much of the noticing is done already for such an enemy, 0 to 1 (1: it is noticed at once).
		float AISharedSpottingHeadStart() const { return m_AISharedSpottingHeadStart; }
		/// Whether armed AI units with nothing to fight go and pick up a loose gun clearly better than theirs, or a grenade when they have nothing to throw.
		bool AIScavengesBetterWeapons() const { return m_AIScavengesBetterWeapons; }
		/// How many AI units of one side may stand on the same spot; standing extras move to the nearest free spot along the floor. 0: no limit.
		int AIMaxUnitsPerSpot() const { return m_AIMaxUnitsPerSpot; }

		/// Whether AI units call a lift or moving platform (AMover) to the stop they wait at, and on to the stop they ride to (LM-11). Off, they
		/// still ride lifts that shuttle, waiting for the deck's own round, and leave alone the ones that only come when called.
		bool AIUnitsCallLifts() const { return m_AIUnitsCallLifts; }

		/// Whether AI units wait at a take-off for the fuel the flight needs (on, as designed), or go with what is in the tank.
		bool AIWaitsForFuel() const { return m_AIWaitForFuel; }
		/// Whether AI units wait for a door of their side on its sensor until it is fully open: hovering in a hatch's sensor under it, braking
		/// short of the leaf, and giving up only after 4 s seen by the sensor with the door still shut (on). Off, as before: held wherever the
		/// door came near, an opening door taken as open, and given up on after 2-3 s.
		bool AIWaitsAtDoors() const { return m_AIWaitsAtDoors; }
		/// Battle Command: whether an enemy team's AI commander spends its points on reinforcements, as you do.
		bool AIEnemyBuysReinforcements() const { return m_AIEnemyBuysReinforcements; }
		/// Battle Command: whether an enemy team's AI commander uses commander powers, as you do.
		bool AIEnemyUsesPowers() const { return m_AIEnemyUsesPowers; }
		/// Battle Command: whether the units you had selected follow the unit you take control of, and go for what it shoots at.
		bool AISquadFollowsYou() const { return m_AISquadFollowsYou; }

		/// How units behave on a cautious or a direct order (an RTS order placed with one click, or three or four, Actor::StandingOrder::Direct):
		/// the "Battle Behavior" settings. Each
		/// member's default is the normal tuning, so DirectOrderTuning() is the reset.
		struct DirectOrderTuning {
			float Caution = 0.1F; //!< The most route caution they have (PathAgent::Caution): 1 is a normal unit's, lower shies less from hard flights and long drops.
			float FlightCostScale = 0.5F; //!< What a jet flight's time, take-off and fuel cost them as a share of the usual (PathAgent::FlightCostScale): lower jumps more where others walk.
			float FuelShare = 0.6F; //!< The share of a flight's fuel they wait for at a take-off before going (with the fuel wait on).
			float RecklessSteadyCut = 0.0F; //!< How much risk cuts the steadying before a take-off short, 0 to 1: the steady is this share shorter at full risk, in the order's share of it (1: none at all at full risk). 0: they come to a stand as any unit does, then go. (Skipped, a risky order's take-offs mid-stride often failed, and it never got off the ground properly.)
			float FlightSpeed = 1.5F; //!< How fast they fly, as a multiple of the usual speed limits up, across and down.
			float Touchdown = 7.0F; //!< How fast they let themselves come down onto the floor, m/s (5 is the usual).
			bool SafeLandings = true; //!< Whether that landing is held under the speed the body is hurt at (AHuman::PilotFlight).
			float DigCostScale = 0.02F; //!< What a node dug costs them on a dig-to, as a share of the usual (PathAgent::DigCostScale): lower tunnels straighter.
			float LineWeight = 0.1F; //!< On a dig-to, what each node off the straight line costs them, in nodes walked (PathFinder::LineCost).
			float MoveLineWeight = 0.6F; //!< On any other order, what each node off the straight line costs them at full risk, in nodes walked: less in a lesser risk's share.
			bool IgnoreEnemies = true; //!< Whether their routes take no notice of enemies on the way.
			bool StayOnTask = true; //!< Whether a unit sent somewhere, on any order, goes after only enemies near its way or the place it was sent (off: any its order lets it chase, as before).
			bool KeepPace = false; //!< Whether a group sent together is still kept to its slowest walker's pace (s_KeepPace on the command row).
			float StraightFlightRisk = 0.9F; //!< From how much risk (0.5 to 1) a unit with a jet flies straight at the goal when the air between is open and a tank flies it, rather than taking the route.
			/// How much of a full tank a full-risk flight may need and still be flown (the risk accepted: it may come down short), by the order's
			/// risk: 0.95 at StraightFlightRisk, up to 1.4 at full risk.
			float FlightReach(float direct) const { return 0.95F + 0.45F * (StraightFlightRisk >= 1.0F ? (direct >= 1.0F ? 1.0F : 0.0F) : std::clamp((direct - StraightFlightRisk) / (1.0F - StraightFlightRisk), 0.0F, 1.0F)); }
			float SkyRouteRisk = 0.8F; //!< From how much risk (0.5 to 1) any order's route may be one flight up into open sky and down onto the goal (PathAgent::SkyRoute), when there is open air over the unit and that is quicker than the ground route; the fuel for it is waited for.
			bool Dives = true; //!< Whether units on a risky order dive on flights at all (DiveRisk, DiveChance).
			float DiveRisk = 0.8F; //!< From how much risk (0.5 to 1) a unit may dive on a flight across (go prone in the air, the jet pushing it along), for speed rather than a controlled flight.
			float DiveChance = 0.25F; //!< The chance of a dive on such a flight at DiveRisk, 0 to 1: it rises to every flight at full risk.
			/// The chance a flight on an order of this risk is dived (DiveRisk, DiveChance); 0 under DiveRisk.
			float DiveChanceAt(float direct) const { return (!Dives || direct < DiveRisk) ? 0.0F : (DiveRisk >= 1.0F ? 1.0F : DiveChance + (1.0F - DiveChance) * std::clamp((direct - DiveRisk) / (1.0F - DiveRisk), 0.0F, 1.0F)); }
			float RiskDragPixels = 600.0F; //!< How far on screen a double click is dragged for a fully direct order, 200 to 1200 pixels: a shorter drag gets less of the direct tuning, on a curve (about 70% at four fifths of it).
			float CautiousCaution = 2.0F; //!< The least route caution a cautious (single-clicked) order has (PathAgent::Caution): over 1 shies more from hard flights and long drops.
			float CautiousFlightCostScale = 3.0F; //!< What a jet flight costs a cautious order, as a share of the usual (PathAgent::FlightCostScale): over 1 walks round where others jump.
			float CautiousFuelShare = 1.2F; //!< The share of a flight's fuel a cautious order waits for at a take-off (never more than the tank allows).
			float CautiousFlightSpeed = 0.75F; //!< How fast a cautious order flies, as a multiple of the usual speed limits.
			float CautiousTouchdown = 3.5F; //!< How fast a cautious order comes down onto the floor, m/s.
			float CautiousEnemyAvoidance = 1.0F; //!< How much a cautious order's route keeps clear of enemies (PathAgent::ThreatWeight, times the Safe routes setting), 0 to 2.
			/// Which behaviours follow the order's risk (the "Recklessness" settings): each on as the game comes. Off, that behaviour is a normal
			/// order's whatever the risk, and takes no notice of the AI movement recklessness setting either (see Follows).
			struct RiskFollowTuning {
				bool Steadying = true; //!< How long a unit steadies itself before a jetpack take-off (off: as long as a normal unit, always).
				bool FuelWait = true; //!< How much of a flight's fuel it waits for at a take-off.
				bool RouteCaution = true; //!< How much hard jumps and long drops put its route search off (PathAgent::Caution).
				bool JetPreference = true; //!< How readily its route takes a jet flight over a walk (PathAgent::FlightCostScale).
				bool FlightSpeed = true; //!< How fast it flies.
				bool Landing = true; //!< How fast it comes down onto the floor.
				bool StraightFlight = true; //!< Whether a full-risk order flies straight at the goal (StraightFlightRisk).
				bool SkyRoute = true; //!< Whether a risky order's route may be a flight up into the sky and down (SkyRouteRisk).
				bool EnemyAvoidance = true; //!< Whether a risky order's route ignores enemies, and a cautious one's keeps clear of them.
				bool Running = true; //!< Whether a cautious order walks where others run.
				bool Vehicles = true; //!< How far off a vehicle fights from, and how high a cautious dropship flies.
			};
			RiskFollowTuning Follows; //!< See RiskFollowTuning.
		};

		/// Gets the direct order tuning, to read or change ("Battle Behavior" settings).
		DirectOrderTuning& DirectOrders() { return m_DirectOrders; }

		/// How units with a digger keep from bringing the ground down on themselves: the "Digging" settings. Each member's default is the
		/// designed behaviour, so DigSafetyTuning() is the reset.
		struct DigSafetyTuning {
			bool AvoidCaveIns = true; //!< Before each node of a tunnel, a digger looks whether cutting it would bring ground down into the tunnel (TerrainCollapse::TunnelWouldCaveIn), and if so asks for a way round.
			bool BackOff = true; //!< With no way round, it digs on, and backs out from under the ground expected to fall while it comes down, waiting for it to settle before going on.
			bool DigToOrders = true; //!< Units on a dig-to order do the same, but never go round: the player asked for that tunnel, so they dig it and back off.
			bool PreviewWarns = true; //!< The dig-to preview says when the tunnel would bring the roof down.
			bool NearbyOnly = true; //!< A dig-to is planned on the ground near the digger only: ground further off is taken for ordinary ground it digs, and found out when it gets there (PathFinder's known reach).
		};

		/// Gets the digging safety tuning, to read or change ("Digging" settings).
		DigSafetyTuning& DigSafety() { return m_DigSafety; }

		/// Whether AI units get out from under falling ground: a loose piece of terrain coming down on a unit has it step out to the nearer side it can get to.
		bool AIDodgesFallingGround() const { return m_AIDodgesFallingGround; }

		/// Sets whether AI units get out from under falling ground.
		void SetAIDodgesFallingGround(bool on) { m_AIDodgesFallingGround = on; }
		/// The route search's and route follower's tuning (the "Pathfinding" settings). Each member's default is the tuning the game comes
		/// with, so PathTuning() is the reset.
		struct PathTuning {
			// The search (PathFinder::AdjacentCost). Costs are in nodes walked: a node's walk costs about 1.
			float LineWeight = 1.75F; //!< On every order, what each node off the straight line from start to goal costs, in nodes walked (PathFinder::LineCost), 0 to 2; added to a direct order's own.
			bool LineWeightByRisk = true; //!< Whether route straightness follows the order's risk (Actor::StandingOrder::Direct): none on a fully cautious order, LineWeight on a normal one, FullRiskLineWeight on a full-risk one, in between by the risk. Off: LineWeight on every order alike, a risky one adding DirectOrderTuning::MoveLineWeight in its share.
			float FullRiskLineWeight = 2.35F; //!< With LineWeightByRisk: the straightness of a full-risk order, 0 to 4, the top of the risk scale.
			float UpCost = 1.5F; //!< The extra cost of each node gone up, on foot or on the jet, 0 to 5 (extraUpCost): lower goes over hills, higher walks round them.
			float FlightCostScale = 1.0F; //!< What a jet flight's time, take-off and fuel cost every unit, as a share of the usual (PathAgent::FlightCostScale), 0.2 to 1; a direct or cautious order's own scale multiplies it.
			float JumpHeightCost = 0.5F; //!< How much dearer each node of a jet jump up a column is than the one below it, 0 to 2: higher keeps jumps short.
			float LadderCostScale = 1.0F; //!< Ladder climbs' cost (2 a node up, 1.2 down), as a multiple, 0.8 to 5.
			float FallCostPerNode = 2.5F; //!< The cost of each node of a drop past a storey (PathFinder::FallCost), 0 to 10.
			float RouteVariety = 1.0F; //!< How much each unit's own taste in routes (PathFinder::VarietyCost) weighs, 0 to 4: 0 sends everyone the same shortest way.
			float FailedSpotCost = 25.0F; //!< What a place or flight a unit or its team failed at lately costs (PathFinder::AvoidCost, AvoidLinks), 0 to 100.
			float JetOverClimb = 0.0F; //!< 0 to 1: how much a unit with a jetpack and fuel jets up rather than walking or scrambling up a slope or stairs. The climb on foot costs 1 / (1 - this) as much (from half a tank; less below), and at 1 it is a last resort.
			// The follower (AHuman::MoveAlongRoute).
			float RefreshSeconds = 7.5F; //!< How often a unit on the move asks for its route afresh, s.
			float NoSightSeconds = 1.0F; //!< How long the route's next point may be out of sight on the ground before a new route is asked for, s.
			float StuckRemedySeconds = 2.5F; //!< How long without getting nearer the next point before the stuck remedies start, s.
			float StuckRepathSeconds = 6.0F; //!< How long without getting nearer before the place is avoided and a new route asked for, s.
			float AirCheckSeconds = 0.5F; //!< How often a unit in the air off a planned flight, leap or drop checks for a better route, s (taken only when better).
			float ReachScale = 1.0F; //!< How near a route point counts as reached, as a multiple of the unit's own reach (twice that in the air), 0.5 to 3: higher cuts corners.
			float NoRouteRetrySeconds = 3.0F; //!< How long a unit told there is no way waits before asking again, s.
		};

		/// Gets the pathfinding tuning, to read or change ("Pathfinding" settings).
		PathTuning& PathTunings() { return m_PathTuning; }
		/// Gets the pathfinding tuning, to read.
		const PathTuning& PathTunings() const { return m_PathTuning; }

		/// Whether AI units use vehicles (VH-4): one going a long way walks to a friendly vehicle on its way and rides it there, or drives it.
		bool AIUsesVehicles() const { return m_AIUsesVehicles; }

		/// Whether AI units get a ride in a friendly flying ship (ACShip) standing idle near them to where they can't walk, or that is a very long
		/// walk: they walk to it, it waits for the others coming, then flies them there and lets them out. Each of the two reasons has its own
		/// switch (AIShipRideNoRoute, AIShipRideLongWalk). Never a ship a player flies, sits in or has given an order.
		bool AIRidesShips() const { return m_AIRidesShips; }

		/// Whether a unit with no route on foot to where it was sent looks for a ship to fly it there (with AIRidesShips).
		bool AIShipRideNoRoute() const { return m_AIShipRideNoRoute; }

		/// Whether a unit sent further than AIShipRideLongWalkDistance looks for a ship to fly it there (with AIRidesShips), though it could
		/// walk: whenever a ship near it can get it there.
		bool AIShipRideLongWalk() const { return m_AIShipRideLongWalk; }

		/// The shortest walk worth a ship ride, in pixels in a straight line: a unit sent further takes a ship near it that can fly it
		/// there (AIShipRideLongWalk). (Land vehicles keep their own 700 px.)
		float AIShipRideLongWalkDistance() const { return m_AIShipRideLongWalkDistance; }

		/// How far off an idle ship looks for units that want a ride, in pixels.
		float AIShipRideRange() const { return m_AIShipRideRange; }

		/// The longest a ship waits, in seconds from the first rider getting in, for the rest coming to it before it flies with those aboard.
		float AIShipRideWait() const { return m_AIShipRideWait; }

		/// How far round the place riders are going a ship lets them out, in pixels: they jump as soon as it is inside this ring.
		float AIShipRideDropRing() const { return m_AIShipRideDropRing; }

		/// Whether an AI driver of a land vehicle waits for the passengers it called over to get in before it drives off (up to AIShipRideWait),
		/// rather than leaving as soon as it has somewhere to go.
		bool AIVehiclesWaitForRiders() const { return m_AIVehiclesWaitForRiders; }

		/// Whether a unit (the player's or the AI's) can get into a vehicle or ship from anywhere close round it, from underneath too, as well
		/// as at its seats: within its size and BoardFromBelowReach of its middle. So a unit can jet up under a hovering ship and get in.
		bool BoardFromBelow() const { return m_BoardFromBelow; }

		/// How far beyond a vehicle's size a unit can be and still get in with BoardFromBelow, in pixels.
		float BoardFromBelowReach() const { return m_BoardFromBelowReach; }

		/// Whether someone killed in an open vehicle's seat falls out of it (on, as the game comes), or slumps in the seat until the vehicle is
		/// wrecked, thrown over or someone takes the seat. A player's own unit falls out either way, so its death is seen.
		bool DeadCrewFallOut() const { return m_DeadCrewFallOut; }

		/// Whether units in the passenger seats of a vehicle or ship built to let them (PassengersCanFire) shoot out of it with the guns they
		/// hold (on, as the game comes). Their shots never hit their own vehicle or anyone sat in it.
		bool PassengersFireFromVehicles() const { return m_PassengersFireFromVehicles; }

		/// Gets what the navigation debug overlay shows: 0 nothing, 1 the path grid in view (where a unit stands, crawls or doesn't fit, and the
		/// step-overs, stairs and leaps between), 2 that and each flight's landing and the engine pilot's predicted path (see PathFinder::DrawDebug),
		/// 3 that and the node under the pointer: what the grid makes of it and every way out of it with its cost (see DebugOverlays::DrawNavNode),
		/// 4 the flights alone: each unit's flight (take-off, landing, the plan, the way it went, its fuel) and the pilot's predicted path, with no
		/// grid and no route.
		/// @return The level.
		int NavDebugOverlay() const { return m_NavDebugOverlay; }

		/// Gets whether the frame rate and the game's version are shown, small, in the top right of the window (a debug aid, on by default).
		/// @return Whether they are shown.
		bool ShowFPSAndVersion() const { return m_ShowFPSAndVersion; }

		/// Sets whether the frame rate and the game's version are shown in the top right of the window.
		/// @param show Whether to show them.
		void SetShowFPSAndVersion(bool show) { m_ShowFPSAndVersion = show; }

		/// Gets whether short notices ("Game saved", "Nothing to undo") are shown at the top of the screen (see Notices; on by default).
		bool ShowNotices() const { return m_ShowNotices; }

		/// Sets whether short notices are shown at the top of the screen.
		void SetShowNotices(bool show) { m_ShowNotices = show; }

		/// The main menu's map browser's filters and order (U-3), kept between launches. The "With saved battles" filter isn't kept: it's on for
		/// Battle Command and off for the Sandbox each time the browser opens. See SandboxMapBrowser.cpp.
		struct MapBrowserFilters {
			std::string Mod; //!< Only maps of this mod, by its file name ("Base.rte"); empty for any.
			int Source = -1; //!< Only maps of this kind (official, from a mod, made in the editor); -1 for any.
			int Size = -1; //!< Only maps of this size (small to extra large); -1 for any.
			bool OnlyFavourites = false; //!< Only the maps starred as favourites.
			bool OnlyRecent = false; //!< Only the maps played lately, the latest first.
			int Sort = 0; //!< What the maps are sorted by: name, size, mod or saved battles.
		};

		/// Gets the map browser's kept filters and order, to read or change.
		MapBrowserFilters& MapBrowserFilterSettings() { return m_MapBrowserFilters; }

		/// Gets the map last played (any game started on a map, not a saved game or an editor), for the main menu's "Continue on" (U-3).
		/// @return The map's preset name, or nothing if none has been played yet.
		const std::string& LastMap() const { return m_LastMap; }

		/// Sets the map last played. Kept in Settings.ini.
		void SetLastMap(const std::string& mapName) { m_LastMap = mapName; }

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
		/// 4 loose falling pieces of terrain, 5 the weather (wind and what's falling), 6 ropes (how hard each is pulled, and where it's tied),
		/// 7 soft bodies, 8 vehicle tyres (how squashed each is and where the ground pushes on it).
		int WorldSimOverlay() const { return m_WorldSimOverlay; }

		/// Sets what the world simulation overlay shows; see WorldSimOverlay.
		/// @param which 0 to 8.
		void SetWorldSimOverlay(int which) { m_WorldSimOverlay = std::clamp(which, 0, 8); }

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

		/// The key that swaps the bar along the bottom between the sandbox's bar and the Commander Toolbar, in the Sandbox game mode and Battle
		/// Command: an SDL key name ("F11").
		const std::string& CommanderBarKey() const { return m_CommanderBarKey; }

		/// Sets the key that swaps the sandbox's bar and the Commander Toolbar (an SDL key name).
		void SetCommanderBarKey(const std::string& key) { m_CommanderBarKey = key; }

		/// The factions last ticked for each team on the scenario setup's "-Pick several-", and which teams used them, so they are there again
		/// next time: "1:Coalition.rte|Ronin.rte;0:Imperatus.rte;0:;0:".
		const std::string& ScenarioFactionPicks() const { return m_ScenarioFactionPicks; }

		/// Sets the remembered scenario faction picks (see ScenarioFactionPicks).
		void SetScenarioFactionPicks(const std::string& picks) { m_ScenarioFactionPicks = picks; }

		/// The values last set on each scenario's "Scenario settings" panel, so they are there again next time:
		/// "Maginot Defence|AttackersPerCraft=3|SpawnInterval=0.5;Wave Defense|WaveSize=2". Settings left at their defaults aren't kept.
		const std::string& ScenarioSettingValues() const { return m_ScenarioSettingValues; }

		/// Sets the remembered scenario setting values (see ScenarioSettingValues).
		void SetScenarioSettingValues(const std::string& values) { m_ScenarioSettingValues = values; }

		/// Whether the AI may use the flying ships (Gunship, Frigate) as delivery craft in scenarios. Off by default: it can't fly them well yet.
		bool ScenarioFlyingCraft() const { return m_ScenarioFlyingCraft; }

		/// Sets whether the AI may use the flying ships in scenarios (see ScenarioFlyingCraft).
		void SetScenarioFlyingCraft(bool allowed) { m_ScenarioFlyingCraft = allowed; }

		/// Which units in the sandbox show their order as a mark over them (RC-7): 0 none, 1 the selected ones, 2 all.
		int SandboxOrderGlyphs() const { return m_SandboxOrderGlyphs; }

		/// Sets SandboxOrderGlyphs; see there.
		void SetSandboxOrderGlyphs(int which) { m_SandboxOrderGlyphs = std::clamp(which, 0, 2); }

		/// How the sandbox's Spawn tab shows what units and items are like (cost, health, mass, fire rate...): 0 not at all, 1 in the tooltip of the one under the pointer, 2 on every tile as well.
		int SandboxSpawnStats() const { return m_SandboxSpawnStats; }

		/// Sets SandboxSpawnStats; see there.
		void SetSandboxSpawnStats(int which) { m_SandboxSpawnStats = std::clamp(which, 0, 2); }

		/// How the sandbox's picture lists (the Spawn tab's and the Build tab's) are sorted: 0 by name, 1 by cost, 2 by health, 3 by mass (the most first).
		int SandboxSpawnSort() const { return m_SandboxSpawnSort; }

		/// Sets SandboxSpawnSort; see there.
		void SetSandboxSpawnSort(int which) { m_SandboxSpawnSort = std::clamp(which, 0, 3); }

		/// Whether the sandbox pings where units of the selection's side come under fire (RC-7).
		bool ShowSandboxAttackPings() const { return m_SandboxAttackPings; }

		/// Sets whether the sandbox pings where units of the selection's side come under fire.
		void SetShowSandboxAttackPings(bool show) { m_SandboxAttackPings = show; }

		/// Whether the sandbox shows its "no route" markers where units couldn't get to (RC-7).
		bool ShowSandboxNoRouteMarkers() const { return m_SandboxNoRouteMarkers; }

		/// Sets whether the sandbox shows its "no route" markers.
		void SetShowSandboxNoRouteMarkers(bool show) { m_SandboxNoRouteMarkers = show; }

		/// Whether the sandbox's map window is shown (RC-8).
		bool ShowSandboxMinimap() const { return m_SandboxMinimap; }

		/// Sets whether the sandbox's map window is shown.
		void SetShowSandboxMinimap(bool show) { m_SandboxMinimap = show; }

		/// Whether Battle Command's alerts (your units under attack, your flag taken...) are listed at the top left of the view.
		bool ShowBattleAlerts() const { return m_BattleAlerts; }

		/// Sets ShowBattleAlerts; see there.
		void SetShowBattleAlerts(bool show) { m_BattleAlerts = show; }

		/// The most Battle Command alerts listed at once.
		int BattleAlertLines() const { return m_BattleAlertLines; }

		/// Sets BattleAlertLines; see there.
		void SetBattleAlertLines(int lines) { m_BattleAlertLines = std::clamp(lines, 1, 12); }

		/// Seconds a Battle Command alert stays listed.
		int BattleAlertSeconds() const { return m_BattleAlertSeconds; }

		/// Sets BattleAlertSeconds; see there.
		void SetBattleAlertSeconds(int seconds) { m_BattleAlertSeconds = std::clamp(seconds, 3, 60); }

		/// Whether Battle Command's kill feed (who killed whom) is listed at the top right of the view.
		bool ShowBattleKillFeed() const { return m_BattleKillFeed; }

		/// Sets ShowBattleKillFeed; see there.
		void SetShowBattleKillFeed(bool show) { m_BattleKillFeed = show; }

		/// The most kills the kill feed lists at once.
		int BattleKillLines() const { return m_BattleKillLines; }

		/// Sets BattleKillLines; see there.
		void SetBattleKillLines(int lines) { m_BattleKillLines = std::clamp(lines, 1, 12); }

		/// Seconds a kill stays in the kill feed.
		int BattleKillSeconds() const { return m_BattleKillSeconds; }

		/// Sets BattleKillSeconds; see there.
		void SetBattleKillSeconds(int seconds) { m_BattleKillSeconds = std::clamp(seconds, 3, 60); }

		/// Whether each Battle Command alert plays a short sound, one for each kind.
		bool BattleAlertSounds() const { return m_BattleAlertSounds; }

		/// Sets BattleAlertSounds; see there.
		void SetBattleAlertSounds(bool on) { m_BattleAlertSounds = on; }

		/// How loud the alert sounds are, 0 to 1 (under the game's sound volume).
		float BattleAlertVolume() const { return m_BattleAlertVolume; }

		/// Sets BattleAlertVolume; see there.
		void SetBattleAlertVolume(float volume) { m_BattleAlertVolume = std::clamp(volume, 0.0F, 1.0F); }

		/// Whether the battle panel asks before something that loses a setup or a battle on: a saved setup deleted or saved over, a battle started again or stopped.
		bool BattleAskFirst() const { return m_BattleAskFirst; }

		/// Sets BattleAskFirst; see there.
		void SetBattleAskFirst(bool ask) { m_BattleAskFirst = ask; }

		/// Whether the battle info panel (each team's tally, units in and fallen, respawns) is shown under the score while a battle is on.
		bool ShowBattleInfo() const { return m_BattleShowInfo; }

		/// Sets ShowBattleInfo; see there.
		void SetShowBattleInfo(bool show) { m_BattleShowInfo = show; }

		/// Whether a battle mode's objectives (flags, hills, goals...) are lit up on the map.
		bool ShowBattleObjectives() const { return m_BattleShowObjectives; }

		/// Sets ShowBattleObjectives; see there.
		void SetShowBattleObjectives(bool show) { m_BattleShowObjectives = show; }

		/// The look of zone objectives: 0 each mode's own, else an objective look (plus one) for all of them.
		int BattleObjectiveLook() const { return m_BattleObjectiveLook; }

		/// Sets BattleObjectiveLook; see there.
		void SetBattleObjectiveLook(int look) { m_BattleObjectiveLook = std::max(look, 0); }

		/// Whether the teams' spawn zones are shaded and outlined on the map.
		bool ShowBattleSpawnZones() const { return m_BattleShowSpawnZones; }

		/// Sets ShowBattleSpawnZones; see there.
		void SetShowBattleSpawnZones(bool show) { m_BattleShowSpawnZones = show; }

		/// Whether a battle mode's own zones (hills, assault objectives, goal zones) are shaded and outlined on the map.
		bool ShowBattleModeZones() const { return m_BattleShowModeZones; }

		/// Sets ShowBattleModeZones; see there.
		void SetShowBattleModeZones(bool show) { m_BattleShowModeZones = show; }
		/// Whether the Army overview window is shown (B-6): your side's units by kind, with how many, their health and how many are idle.
		bool ShowArmyOverview() const { return m_ArmyOverview; }

		/// Sets whether the Army overview window is shown.
		void SetShowArmyOverview(bool show) { m_ArmyOverview = show; }

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
		/// @param level 0 to 4.
		void SetNavDebugOverlay(int level) { m_NavDebugOverlay = std::clamp(level, 0, 4); }

		/// Sets whether actors mantle ledges and vault low obstacles.
		void SetMantlingEnabled(bool enable) { m_EnableMantling = enable; }
		void SetCreaturesClimbWalls(bool climb) { m_CreaturesClimbWalls = climb; }
		void SetMechFootfallShake(float shake) { m_MechFootfallShake = std::clamp(shake, 0.0F, 3.0F); }
		void SetCreatureClimbSpeed(float multiplier) { m_CreatureClimbSpeed = std::clamp(multiplier, 0.25F, 3.0F); }
		void SetCreaturesFly(bool fly) { m_CreaturesFly = fly; }
		void SetCreatureFlySpeed(float multiplier) { m_CreatureFlySpeed = std::clamp(multiplier, 0.25F, 3.0F); }
		void SetStuckPushEnabled(bool enable) { m_EnableStuckPush = enable; }

		/// Sets whether every scene is played without wrapping horizontally. Takes effect when a scene is next loaded.
		void SetNoSceneWrap(bool enable) { m_NoSceneWrap = enable; }

		/// Sets how strongly fire pins units down and shakes them, 0 to 2.
		void SetAISuppression(float scale) { m_AISuppression = std::clamp(scale, 0.0F, 2.0F); }

		/// Sets how readily units with a digger tunnel; see AIDigWillingness.
		void SetAIDigWillingness(float scale) { m_AIDigWillingness = std::clamp(scale, 0.0F, 2.0F); }

		/// Sets whether units dig through ground only their digger's tracers cut; see AIDigsWithTracers.
		void SetAIDigsWithTracers(bool on) { m_AIDigsWithTracers = on; }
		/// Sets how much safe-route units keep clear of enemies, 0 to 2; see AIThreatAvoidance.
		void SetAIThreatAvoidance(float scale) { m_AIThreatAvoidance = std::clamp(scale, 0.0F, 2.0F); }
		/// Sets how reckless AI units are on the move, 0 to 1 (0.5 as designed).
		void SetAIRecklessness(float recklessness) { m_AIRecklessness = std::clamp(recklessness, 0.0F, 1.0F); }
		void SetStrictClimbLimits(bool on) { m_StrictClimbLimits = on; }
		void SetMeasuredBodySize(bool on) { m_MeasuredBodySize = on; }
		void SetAILadderGiveUpSeconds(float seconds) { m_AILadderGiveUpSeconds = std::clamp(seconds, 0.5F, 10.0F); }
		void SetAILadderTeamStalls(int stalls) { m_AILadderTeamStalls = std::clamp(stalls, 1, 5); }
		void SetAIGrabsLaddersOnPurpose(bool on) { m_AIGrabsLaddersOnPurpose = on; }
		void SetAIGrabsLaddersMidJet(bool on) { m_AIGrabsLaddersMidJet = on; }
		void SetAIMantlesOnPurpose(bool on) { m_AIMantlesOnPurpose = on; }
		void SetAICatchesLedgesMidAir(bool on) { m_AICatchesLedgesMidAir = on; }
		void SetRiskyOrdersFlyPastLadders(bool on) { m_RiskyOrdersFlyPastLadders = on; }
		void SetUnitsGrabRopes(bool on) { m_UnitsGrabRopes = on; }
		void SetAICatchesRopes(bool on) { m_AICatchesRopes = on; }
		void SetRopeClimbSpeed(float speed) { m_RopeClimbSpeed = std::clamp(speed, 0.25F, 3.0F); }
		/// Puts the Movement category's rope settings back as the game comes.
		void ResetRopeHoldSettings() { m_UnitsGrabRopes = true; m_AICatchesRopes = true; m_RopeClimbSpeed = 1.0F; }
		void SetCarriedWeightMultiplier(float multiplier) { m_CarriedWeightMultiplier = std::clamp(multiplier, 0.0F, 5.0F); }
		void SetLegStrength(float strength) { m_LegStrength = std::clamp(strength, 0.25F, 3.0F); }
		void SetJetpackWeightCompensation(int mode) { m_JetpackWeightCompensation = std::clamp(mode, 0, 2); }
		void SetUnitWaterBehaviour(int mode) { m_UnitWaterBehaviour = std::clamp(mode, 0, 2); }
		void SetUnitsStepOverObjects(bool on) { m_UnitsStepOverObjects = on; }
		void SetUnitsCrossVehicles(bool on) { m_UnitsCrossVehicles = on; }
		void SetUnitsPushObjects(bool on) { m_UnitsPushObjects = on; }
		void SetObjectPushStrength(float strength) { m_ObjectPushStrength = std::clamp(strength, 0.0F, 3.0F); }
		void SetSensesPassPlants(bool on) { m_SensesPassPlants = on; }
		void SetSensesPassRungs(bool on) { m_SensesPassRungs = on; }
		void SetSensesPassSpecks(bool on) { m_SensesPassSpecks = on; }
		void SetOverloadSlowdown(float slowdown) { m_OverloadSlowdown = std::clamp(slowdown, 0.0F, 1.0F); }
		void SetJetpackFuelMultiplier(float multiplier) { m_JetpackFuelMultiplier = std::clamp(multiplier, 0.25F, 5.0F); }
		void SetInfiniteJetpackFuel(bool on) { m_InfiniteJetpackFuel = on; }
		/// Puts the Movement category's carry weight settings back as the game comes.
		void ResetCarryWeightSettings() { m_CarriedWeightMultiplier = 1.0F; m_LegStrength = 1.0F; m_JetpackWeightCompensation = 0; m_OverloadSlowdown = 0.0F; m_JetpackFuelMultiplier = 1.0F; m_InfiniteJetpackFuel = false; }
		/// Puts the Movement category's ladder settings back as the game comes.
		void ResetLadderSettings() { m_AILadderGiveUpSeconds = 1.5F; m_AILadderTeamStalls = 2; m_AIGrabsLaddersOnPurpose = true; m_AIGrabsLaddersMidJet = true; m_RiskyOrdersFlyPastLadders = true; }
		/// Sets the percentage of units handed a digger as they come into the scene; see AISpawnDiggerChance.
		void SetAISpawnDiggerChance(float percent) { m_AISpawnDiggerChance = std::clamp(percent, 0.0F, 100.0F); }
		/// Sets the percentage of wounds that keep bleeding; see BleedOutChance.
		void SetBleedOutChance(float percent) { m_BleedOutChance = std::clamp(percent, 0.0F, 100.0F); }
		/// Sets how long wounds bleed for; see WoundBleedTime.
		void SetWoundBleedTime(float scale) { m_WoundBleedTime = std::clamp(scale, 0.1F, 10.0F); }
		/// Sets the gap ships keep over the ground; see ShipGroundClearance.
		void SetShipGroundClearance(float gap) { m_ShipGroundClearance = std::clamp(gap, 0.0F, 200.0F); }
		/// Sets how far past the map's edges ships may fly; see ShipGraceZone. (Short of the 1000 px out of the scene where anything is deleted.)
		void SetShipGraceZone(float zone) { m_ShipGraceZone = std::clamp(zone, 0.0F, 700.0F); }
		/// Sets how far past the map's edges the camera may scroll; see CameraOverscroll.
		void SetCameraOverscroll(float overscroll) { m_CameraOverscroll = std::clamp(overscroll, 0.0F, 2000.0F); }
		/// Sets how hard weapon fire strikes terrain; see WeaponTerrainDamage.
		void SetWeaponTerrainDamage(float scale) { m_WeaponTerrainDamage = std::clamp(scale, 0.1F, 20.0F); }
		/// Sets how much damage weapon fire does to units; see WeaponUnitDamage.
		void SetWeaponUnitDamage(float scale) { m_WeaponUnitDamage = std::clamp(scale, 0.1F, 20.0F); }
		/// Sets whether the weapon damage to units also scales blowing apart; see WeaponUnitDamageGibs.
		void SetWeaponUnitDamageGibs(bool gibs) { m_WeaponUnitDamageGibs = gibs; }
		/// Sets which digger those units are handed; see AISpawnDiggerType.
		void SetAISpawnDiggerType(int type) { m_AISpawnDiggerType = std::clamp(type, 0, 4); }

		/// Sets whether AI units steady themselves before they jet.
		void SetAISteadiesBeforeJet(bool steady) { m_AISteadyBeforeJet = steady; }
		void SetAICoverDigs(bool on) { m_AICoverDigs = on; }
		void SetAICoverFallsBack(bool on) { m_AICoverFallsBack = on; }
		void SetAICoverLiesFlat(bool on) { m_AICoverLiesFlat = on; }
		void SetAIBreaksWhenShaken(bool on) { m_AIBreaksWhenShaken = on; }
		void SetAISharedSpotting(bool on) { m_AISharedSpotting = on; }
		void SetAISharedSpottingHeadStart(float headStart) { m_AISharedSpottingHeadStart = std::clamp(headStart, 0.0F, 1.0F); }
		void SetAIScavengesBetterWeapons(bool on) { m_AIScavengesBetterWeapons = on; }
		void SetAIMaxUnitsPerSpot(int count) { m_AIMaxUnitsPerSpot = std::clamp(count, 0, 8); }
		/// Sets whether AI units call lifts; see AIUnitsCallLifts.
		void SetAIUnitsCallLifts(bool on) { m_AIUnitsCallLifts = on; }

		/// Sets whether AI units wait for fuel before they jet.
		void SetAIWaitsForFuel(bool wait) { m_AIWaitForFuel = wait; }
		void SetAIWaitsAtDoors(bool wait) { m_AIWaitsAtDoors = wait; }
		void SetAIEnemyBuysReinforcements(bool on) { m_AIEnemyBuysReinforcements = on; }
		void SetAIEnemyUsesPowers(bool on) { m_AIEnemyUsesPowers = on; }
		void SetAISquadFollowsYou(bool on) { m_AISquadFollowsYou = on; }

		/// Sets whether AI units use vehicles.
		void SetAIUsesVehicles(bool use) { m_AIUsesVehicles = use; }

		/// Sets the ship ride settings; see AIRidesShips and the rest.
		void SetAIRidesShips(bool ride) { m_AIRidesShips = ride; }
		void SetAIShipRideNoRoute(bool ride) { m_AIShipRideNoRoute = ride; }
		void SetAIShipRideLongWalk(bool ride) { m_AIShipRideLongWalk = ride; }
		void SetAIShipRideLongWalkDistance(float distance) { m_AIShipRideLongWalkDistance = std::clamp(distance, 200.0F, 10000.0F); }
		void SetAIShipRideRange(float range) { m_AIShipRideRange = std::clamp(range, 200.0F, 5000.0F); }
		void SetAIShipRideWait(float seconds) { m_AIShipRideWait = std::clamp(seconds, 0.0F, 120.0F); }
		void SetAIShipRideDropRing(float range) { m_AIShipRideDropRing = std::clamp(range, 30.0F, 1000.0F); }
		void SetAIVehiclesWaitForRiders(bool wait) { m_AIVehiclesWaitForRiders = wait; }
		void SetBoardFromBelow(bool on) { m_BoardFromBelow = on; }
		void SetBoardFromBelowReach(float reach) { m_BoardFromBelowReach = std::clamp(reach, 0.0F, 200.0F); }

		/// Sets whether crew killed in a vehicle fall out of it; see DeadCrewFallOut.
		void SetDeadCrewFallOut(bool fallOut) { m_DeadCrewFallOut = fallOut; }

		/// Sets whether passengers shoot out of vehicles built to let them; see PassengersFireFromVehicles.
		void SetPassengersFireFromVehicles(bool fire) { m_PassengersFireFromVehicles = fire; }

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

		/// Whether the aim reticles (the yellow dots along the sights) of CPU-controlled units' held weapons are drawn. The player's own are always drawn.
		bool ShowCPUAimReticles() const { return m_ShowCPUAimReticles; }

		/// Sets whether the aim reticles of CPU-controlled units' held weapons are drawn.
		void SetShowCPUAimReticles(bool show) { m_ShowCPUAimReticles = show; }

		/// Whether the sharp aim of the unit the player controls passes through terrain and objects instead of being cut short at the first one, so its reticle and the camera reach the weapon's full sharp-aim length in any direction. CPU units always stop at them.
		bool FreeAimThroughTerrain() const { return m_FreeAimThroughTerrain; }

		/// Sets whether the player's sharp aim passes through terrain and objects.
		void SetFreeAimThroughTerrain(bool free) { m_FreeAimThroughTerrain = free; }

		/// Whether the exclamation mark that pops up over a unit's head when it is alerted is drawn.
		bool ShowAlarmExclamation() const { return m_ShowAlarmExclamation; }

		/// Sets whether the exclamation mark over an alerted unit's head is drawn.
		void SetShowAlarmExclamation(bool show) { m_ShowAlarmExclamation = show; }

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
		bool m_UnitMenuClickToOpen; //!< Whether a click opens the unit menu and leaves it up (see UnitMenuClickToOpen).
		float m_UnitMenuTimeScale; //!< How fast time runs while the unit menu is up (see UnitMenuTimeScale).
		std::string m_PauseAIKey; //!< The key that pauses and resumes the AI (see GetPauseAIKey).
		std::string m_DefaultBackground; //!< The BackgroundSet behind every map played fresh, empty for each map's own (see GetDefaultBackground).
		bool m_TakingKey = false; //!< Whether the settings panel is waiting for a key to bind (see IsTakingKey).
		bool m_EndlessMetaGameMode; //!< Endless MetaGame mode.
		bool m_ShowFPSAndVersion; //!< Whether the frame rate and version are shown in the top right (see ShowFPSAndVersion).
		bool m_ShowNotices; //!< Whether short notices are shown at the top of the screen (see ShowNotices).
		MapBrowserFilters m_MapBrowserFilters; //!< The map browser's kept filters and order (see MapBrowserFilterSettings).
		std::string m_LastMap; //!< The map last played (see LastMap).
		int m_NavDebugOverlay; //!< What the navigation debug overlay shows (see NavDebugOverlay).
		int m_DebugTeam; //!< The team whose view the debug overlays show (see DebugTeam).
		int m_UnitInspector; //!< Which units the unit inspector overlay labels (see UnitInspector).
		int m_WorldSimOverlay; //!< What the world simulation overlay shows (see WorldSimOverlay).
		bool m_SandboxStrokeLog; //!< Whether the sandbox's stroke log is on (see ShowSandboxStrokeLog).
		bool m_SandboxSpotReach; //!< Whether move previews show each standing spot's reachability (see ShowSandboxSpotReach).
		bool m_SandboxGroupBadges; //!< Whether control-group units show their group's number (see ShowSandboxGroupBadges).
		std::string m_CommanderBarKey; //!< The key that swaps the sandbox's bar and the Commander Toolbar (see CommanderBarKey).
		std::string m_ScenarioFactionPicks; //!< The factions last picked for each team in the scenario setup (see ScenarioFactionPicks).
		std::string m_ScenarioSettingValues; //!< The values last set on each scenario's settings panel (see ScenarioSettingValues).
		bool m_ScenarioFlyingCraft; //!< Whether the AI may use the flying ships in scenarios (see ScenarioFlyingCraft).
		int m_SandboxOrderGlyphs; //!< Which units show their order as a mark (see SandboxOrderGlyphs).
		int m_SandboxSpawnStats; //!< How the Spawn tab shows units' and items' stats (see SandboxSpawnStats).
		int m_SandboxSpawnSort; //!< How the sandbox's picture lists are sorted (see SandboxSpawnSort).
		bool m_SandboxAttackPings; //!< Whether units coming under fire are pinged (see ShowSandboxAttackPings).
		bool m_SandboxNoRouteMarkers; //!< Whether "no route" markers are shown (see ShowSandboxNoRouteMarkers).
		bool m_SandboxMinimap; //!< Whether the sandbox's map window is shown (see ShowSandboxMinimap).
		bool m_BattleAlerts; //!< Whether Battle Command's alerts are listed (see ShowBattleAlerts).
		int m_BattleAlertLines; //!< The most alerts listed at once (see BattleAlertLines).
		int m_BattleAlertSeconds; //!< Seconds an alert stays listed (see BattleAlertSeconds).
		bool m_BattleKillFeed; //!< Whether the kill feed is listed (see ShowBattleKillFeed).
		int m_BattleKillLines; //!< The most kills listed at once (see BattleKillLines).
		int m_BattleKillSeconds; //!< Seconds a kill stays listed (see BattleKillSeconds).
		bool m_BattleAlertSounds; //!< Whether alerts play a sound (see BattleAlertSounds).
		float m_BattleAlertVolume; //!< How loud they are (see BattleAlertVolume).
		bool m_BattleAskFirst; //!< Whether the battle panel asks before losing a setup or a battle (see BattleAskFirst).
		bool m_BattleShowInfo; //!< Whether the battle info panel is shown (see ShowBattleInfo).
		bool m_BattleShowObjectives; //!< Whether battle objectives are lit up on the map (see ShowBattleObjectives).
		int m_BattleObjectiveLook; //!< The look of zone objectives (see BattleObjectiveLook).
		bool m_BattleShowSpawnZones; //!< Whether spawn zones are shown on the map (see ShowBattleSpawnZones).
		bool m_BattleShowModeZones; //!< Whether a mode's own zones are shown on the map (see ShowBattleModeZones).
		bool m_ArmyOverview; //!< Whether the Army overview window is shown (see ShowArmyOverview).
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
		bool m_RagdollDeaths; //!< Whether humanoids die as whole ragdolls (see RagdollDeaths).
		int m_RagdollBodySeconds; //!< How long a ragdoll's body stays before it may settle (see RagdollBodySeconds).
		bool m_NoDismemberment; //!< Whether humanoids are never dismembered or blown apart (see NoDismemberment).
		float m_RagdollBlowApartScale; //!< How big a hit still blows a ragdolling body apart (see RagdollBlowApartScale).
		GibChunkTuning m_GibChunks; //!< How gib chunks come out, move and settle (see GibChunks).
		AICombatTuning m_AICombat; //!< How well AI units shoot, react, spot and throw (see AICombat).
		bool m_EnableMantling; //!< Whether actors pull themselves up onto ledges and over low obstacles (players and the AI alike).
		bool m_CreaturesClimbWalls = true; //!< See CreaturesClimbWalls.
		float m_MechFootfallShake = 1.0F; //!< See MechFootfallShake.
		float m_CreatureClimbSpeed = 1.0F; //!< See CreatureClimbSpeed.
		bool m_CreaturesFly = true; //!< See CreaturesFly.
		float m_CreatureFlySpeed = 1.0F; //!< See CreatureFlySpeed.
		bool m_EnableStuckPush; //!< Whether an AI humanoid stuck on the ground is nudged free once its own moves are spent (AHuman::PushFree).
		bool m_NoSceneWrap; //!< Whether every scene is played with hard left and right edges instead of wrapping.
		float m_Toughness[static_cast<int>(ToughnessKind::Count)]; //!< How tough each kind of actor is (see Toughness).
		float m_AISuppression; //!< How strongly fire pins units down and shakes them, 0 to 2 (see AISuppression).
		float m_AIDigWillingness; //!< How readily units with a digger tunnel rather than go round, 0 to 2 (see AIDigWillingness).
		bool m_AIDigsWithTracers; //!< See AIDigsWithTracers.
		float m_AIThreatAvoidance; //!< How much safe-route units keep clear of enemies, 0 to 2 (see AIThreatAvoidance).
		float m_AIRecklessness; //!< How reckless AI units are on the move, 0 to 1 (see AIRecklessness).
		float m_AILadderGiveUpSeconds; //!< See AILadderGiveUpSeconds.
		bool m_StrictClimbLimits = true; //!< See StrictClimbLimits.
		bool m_MeasuredBodySize = false; //!< See MeasuredBodySize.
		float m_CarriedWeightMultiplier = 1.0F; //!< See CarriedWeightMultiplier.
		float m_LegStrength = 1.0F; //!< See LegStrength.
		int m_JetpackWeightCompensation = 0; //!< See JetpackWeightCompensation.
		int m_UnitWaterBehaviour = 2; //!< See UnitWaterBehaviour.
		bool m_UnitsStepOverObjects = true; //!< See UnitsStepOverObjects.
		bool m_UnitsCrossVehicles = true; //!< See UnitsCrossVehicles.
		bool m_UnitsPushObjects = true; //!< See UnitsPushObjects.
		float m_ObjectPushStrength = 1.0F; //!< See ObjectPushStrength.
		bool m_SensesPassPlants = true; //!< See SensesPassPlants.
		bool m_SensesPassRungs = true; //!< See SensesPassRungs.
		bool m_SensesPassSpecks = true; //!< See SensesPassSpecks.
		float m_OverloadSlowdown = 0.0F; //!< See OverloadSlowdown.
		float m_JetpackFuelMultiplier = 1.0F; //!< See JetpackFuelMultiplier.
		bool m_InfiniteJetpackFuel = false; //!< See InfiniteJetpackFuel.
		SlowdownFixTuning m_SlowdownFixes; //!< See SlowdownFixes.
		int m_AILadderTeamStalls; //!< See AILadderTeamStalls.
		bool m_AIGrabsLaddersOnPurpose; //!< See AIGrabsLaddersOnPurpose.
		bool m_AIGrabsLaddersMidJet; //!< See AIGrabsLaddersMidJet.
		bool m_AIMantlesOnPurpose = true; //!< See AIMantlesOnPurpose.
		bool m_AICatchesLedgesMidAir = true; //!< See AICatchesLedgesMidAir.
		bool m_RiskyOrdersFlyPastLadders; //!< See RiskyOrdersFlyPastLadders.
		bool m_UnitsGrabRopes = true; //!< See UnitsGrabRopes.
		bool m_AICatchesRopes = true; //!< See AICatchesRopes.
		float m_RopeClimbSpeed = 1.0F; //!< See RopeClimbSpeed.
		float m_AISpawnDiggerChance; //!< Percentage of units handed a digger as they come into the scene, 0 to 100 (see AISpawnDiggerChance).
		float m_BleedOutChance; //!< Percentage of wounds that keep bleeding rather than stopping, 0 to 100 (see BleedOutChance).
		float m_WoundBleedTime; //!< Multiple of how long wounds bleed for before they stop, 0.1 to 10 (see WoundBleedTime).
		float m_ShipGroundClearance; //!< Pixels flying ships keep between their underside and the ground, 0 to 200 (see ShipGroundClearance).
		float m_ShipGraceZone; //!< Pixels past the map's top and sides flying ships may go, 0 to 700 (see ShipGraceZone).
		float m_CameraOverscroll; //!< Pixels past the map's edges the camera may scroll, 0 to 2000 (see CameraOverscroll).
		float m_WeaponTerrainDamage; //!< Multiple of the force weapon fire strikes terrain with, 0.1 to 20 (see WeaponTerrainDamage).
		float m_WeaponUnitDamage; //!< Multiple of the damage weapon fire does to units, 0.1 to 20 (see WeaponUnitDamage).
		bool m_WeaponUnitDamageGibs; //!< Whether WeaponUnitDamage scales blowing apart too (see WeaponUnitDamageGibs).
		int m_AISpawnDiggerType; //!< Which digger they're handed: 0 Light, 1 Medium, 2 Heavy, 3 random, 4 Mega (see AISpawnDiggerType).
		bool m_AISteadyBeforeJet; //!< Whether AI units steady themselves before they jet (see AISteadiesBeforeJet).
		bool m_AICoverDigs; //!< See AICoverDigs.
		bool m_AICoverFallsBack; //!< See AICoverFallsBack.
		bool m_AICoverLiesFlat; //!< See AICoverLiesFlat.
		bool m_AIBreaksWhenShaken; //!< See AIBreaksWhenShaken.
		bool m_AISharedSpotting; //!< See AISharedSpotting.
		float m_AISharedSpottingHeadStart; //!< See AISharedSpottingHeadStart.
		bool m_AIScavengesBetterWeapons; //!< See AIScavengesBetterWeapons.
		int m_AIMaxUnitsPerSpot; //!< See AIMaxUnitsPerSpot.
		bool m_AIUnitsCallLifts; //!< See AIUnitsCallLifts.
		bool m_AIWaitForFuel; //!< Whether AI units wait for fuel before they jet (see AIWaitsForFuel).
		bool m_AIWaitsAtDoors = true; //!< See AIWaitsAtDoors.
		bool m_AIEnemyBuysReinforcements; //!< See AIEnemyBuysReinforcements.
		bool m_AIEnemyUsesPowers; //!< See AIEnemyUsesPowers.
		bool m_AISquadFollowsYou; //!< See AISquadFollowsYou.
		bool m_AIUsesVehicles; //!< Whether AI units use vehicles (see AIUsesVehicles).
		bool m_AIRidesShips; //!< See AIRidesShips.
		bool m_AIShipRideNoRoute; //!< See AIShipRideNoRoute.
		bool m_AIShipRideLongWalk; //!< See AIShipRideLongWalk.
		float m_AIShipRideLongWalkDistance; //!< See AIShipRideLongWalkDistance.
		float m_AIShipRideRange; //!< See AIShipRideRange.
		float m_AIShipRideWait; //!< See AIShipRideWait.
		float m_AIShipRideDropRing; //!< See AIShipRideDropRing.
		bool m_AIVehiclesWaitForRiders; //!< See AIVehiclesWaitForRiders.
		bool m_BoardFromBelow; //!< See BoardFromBelow.
		float m_BoardFromBelowReach; //!< See BoardFromBelowReach.
		bool m_DeadCrewFallOut; //!< Whether crew killed in a vehicle fall out of it (see DeadCrewFallOut).
		bool m_PassengersFireFromVehicles; //!< Whether passengers shoot out of vehicles built to let them (see PassengersFireFromVehicles).
		DirectOrderTuning m_DirectOrders; //!< How units behave on a direct order (see DirectOrders).
		DigSafetyTuning m_DigSafety; //!< How diggers keep from bringing the ground down on themselves (see DigSafety).
		bool m_AIDodgesFallingGround; //!< See AIDodgesFallingGround.
		PathTuning m_PathTuning; //!< The route search's and follower's tuning (see PathTunings).
		bool m_EnableCrabBombs; //!< Whether all actors (except Brains and Doors) should be annihilated if a number exceeding the crab bomb threshold is released at once.
		int m_CrabBombThreshold; //!< The number of crabs needed to be released at once to trigger the crab bomb effect.
		bool m_ShowEnemyHUD; //!< Whether the HUD of enemy actors should be visible to the player.
		bool m_ShowUnitTags; //!< Whether each unit's side and health are drawn beside it.
		bool m_ShowCPUAimReticles; //!< Whether the aim reticles of CPU-controlled units' held weapons are drawn.
		bool m_FreeAimThroughTerrain; //!< Whether the player's sharp aim passes through terrain and objects instead of stopping at them.
		bool m_ShowAlarmExclamation; //!< Whether the exclamation mark over an alerted unit's head is drawn.
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
		bool m_DisableMedievalUnits; //!< Whether the Kingdom and Fell Legion units are switched off (mod manager).
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
		std::array<std::string, 3> m_StartupPresets; //!< The presets loaded every time the game starts, over Settings.ini: the full one, then the gameplay and visual ones. Nothing for none.

		/// Gets where a kind of preset's startup preset is kept in m_StartupPresets.
		static size_t StartupSlot(PresetGroup group) { return group == PresetGroup::Gameplay ? 1 : (group == PresetGroup::Visual ? 2 : 0); }

		/// Gets which group each setting SaveTunables writes is in, by its name. What is written before the first group (the version) is in every one (Everything).
		std::unordered_map<std::string, PresetGroup> TunableGroups() const;

		/// Clears all the member variables of this SettingsMan, effectively resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		SettingsMan(const SettingsMan& reference) = delete;
		SettingsMan& operator=(const SettingsMan& rhs) = delete;
	};
} // namespace RTE
