#include "ControlLink.h"
#include "Actor.h"
#include "SettingsMan.h"
#include "SDL3/SDL_keyboard.h"
#include "Weather.h"
#include <algorithm>
#include <array>
#include <initializer_list>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include "TextOverlay.h"
#include "TerrainFire.h"
#include "RopeSim.h"
#include "TerrainCandle.h"
#include "WeatherLightning.h"
#include "TerrainCollapse.h"
#include "ZeroGravity.h"
#include "TerrainTrees.h"
#include "FluidSim.h"
#include "Material.h"
#include "SceneMan.h"
#include "ThreatMemory.h"
#include "GasGrid.h"
#include "AirPressure.h"
#include "MagicEffects.h"
#include "Sandbox.h"
#include "SmokeGrid.h"
#include "ActorFire.h"
#include "ActorWater.h"
#include "UnitSpeech.h"
#include "UnitSpeechVoice.h"
#include "ModernHUD.h"
#include "ToolWidgets.h"
#include "ConsoleMan.h"
#include "CameraMan.h"
#include "MovableMan.h"
#include "WindowMan.h"
#include "FrameMan.h"
#include "PostProcessMan.h"
#include "AudioMan.h"
#include "PerformanceMan.h"
#include "UInputMan.h"
#include "DebugMan.h"
#include "TimerMan.h"
#include "Controller.h"
#include "System.h"

#include <sstream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <cctype>
#include <algorithm>
#include <stdexcept>
using namespace RTE;

namespace {
	/// Parses "r g b" (commas also accepted), keeping the fallback for anything missing.
	glm::vec3 ReadVec3(std::string value, const glm::vec3& fallback) {
		std::replace(value.begin(), value.end(), ',', ' ');
		std::istringstream stream(value);
		glm::vec3 result = fallback;
		stream >> result.x >> result.y >> result.z;
		return result;
	}

	std::string WriteVec3(const glm::vec3& value) {
		std::ostringstream stream;
		stream << value.x << " " << value.y << " " << value.z;
		return stream.str();
	}

	/// Bumped when lighting defaults change enough that values saved by older versions should be dropped in favour of the new defaults.
	/// 2: brighter ambient light in interiors and caves.
	constexpr int c_LightingSettingsVersion = 2;
	int s_ReadLightingSettingsVersion = 0; //!< Version of the lighting settings in the file being read, 0 when it has none.
	bool s_OldPathLineWeight = false; //!< The settings file read had PathLineWeight at 0, the old default, from before PathLineWeightByRisk (written after it) existed.
	bool s_MarkingGroups = false; //!< Whether SaveTunables marks where each group of settings starts, for TunableGroups.
	const std::string c_GroupMarker = "PresetGroupStartsHere"; //!< The marker's name. Never in a file.
	bool s_ReadingStartupPreset = false; //!< Whether the preset loaded at start is being read, when the panel's settings for the moment are passed over.

	/// The retro layer settings (LightingSettings::RetroLayers) that are floats, by the name they're saved under after their prefix.
	const std::pair<const char*, float LightingSettings::RetroLayers::*> c_RetroFloatFields[] = {
	    {"PaletteStrength", &LightingSettings::RetroLayers::PaletteStrength}, {"NeonGlow", &LightingSettings::RetroLayers::NeonGlow}, {"EdgeLines", &LightingSettings::RetroLayers::EdgeLines},
	    {"Sky", &LightingSettings::RetroLayers::RetroSky}, {"GridSpeed", &LightingSettings::RetroLayers::GridSpeed}, {"VHS", &LightingSettings::RetroLayers::VHS},
	    {"Glitch", &LightingSettings::RetroLayers::Glitch}, {"Curvature", &LightingSettings::RetroLayers::Curvature}, {"CRT", &LightingSettings::RetroLayers::CRT},
	    {"Flicker", &LightingSettings::RetroLayers::Flicker}, {"Posterize", &LightingSettings::RetroLayers::Posterize}, {"Dither", &LightingSettings::RetroLayers::Dither},
	    {"Pixelate", &LightingSettings::RetroLayers::Pixelate}, {"ASCII", &LightingSettings::RetroLayers::ASCII}, {"ASCIIBackground", &LightingSettings::RetroLayers::ASCIIBackground},
	    {"Afterglow", &LightingSettings::RetroLayers::Afterglow}, {"TerminalOverlay", &LightingSettings::RetroLayers::TerminalOverlay}, {"CelShading", &LightingSettings::RetroLayers::CelShading},
	    {"CelBands", &LightingSettings::RetroLayers::CelBands}, {"Halftone", &LightingSettings::RetroLayers::Halftone}, {"ColorHalftone", &LightingSettings::RetroLayers::ColorHalftone},
	    {"PaperGrain", &LightingSettings::RetroLayers::PaperGrain}, {"Misregister", &LightingSettings::RetroLayers::Misregister}, {"ComicFX", &LightingSettings::RetroLayers::ComicFX},
	    {"PanelFrame", &LightingSettings::RetroLayers::PanelFrame}, {"TrackingBand", &LightingSettings::RetroLayers::TrackingBand},
	    {"LCD", &LightingSettings::RetroLayers::LCD}, {"LCDGrid", &LightingSettings::RetroLayers::LCDGrid}, {"LCDGhosting", &LightingSettings::RetroLayers::LCDGhosting},
	    {"ChunkyPixels", &LightingSettings::RetroLayers::ChunkyPixels}, {"BlastFlash", &LightingSettings::RetroLayers::BlastFlash}, {"Bezel", &LightingSettings::RetroLayers::Bezel},
	    {"ChipSound", &LightingSettings::RetroLayers::ChipSound}};

	/// The retro layer settings that aren't floats, by the name they're saved under after their prefix.
	const char* const c_RetroOtherFields[] = {"Look", "Palette", "LCDPalette", "InkEdges", "GlitchOnBlasts", "PaperBackgroundOnly"};

	/// Reads one retro layer setting, saved as its prefix ("Retro" for the player's own, "Synthwave" and so on for a screen style's, "Variant" for the chosen variant's, none in a filter preset file) and then field.
	void ReadRetroField(LightingSettings::RetroLayers& layers, std::string_view field, const std::string& value) {
		if (field == "Look") {
			layers.Look = std::clamp(std::stoi(value), static_cast<int>(LightingSettings::StyleSynthwave), static_cast<int>(LightingSettings::StyleCount) - 1);
		} else if (field == "Palette") {
			layers.Palette = std::clamp(std::stoi(value), 0, static_cast<int>(LightingSettings::PaletteCount) - 1);
		} else if (field == "LCDPalette") {
			layers.LCDPalette = std::clamp(std::stoi(value), 0, static_cast<int>(LightingSettings::LCDPaletteCount) - 1);
		} else if (field == "InkEdges") {
			layers.InkEdges = std::stoi(value) != 0;
		} else if (field == "GlitchOnBlasts") {
			layers.GlitchOnBlasts = std::stoi(value) != 0;
		} else if (field == "PaperBackgroundOnly") {
			layers.PaperBackgroundOnly = std::stoi(value) != 0;
		} else {
			for (const auto& [name, member]: c_RetroFloatFields) {
				if (field == name) {
					layers.*member = std::clamp(std::stof(value), 0.0F, 5.0F);
				}
			}
		}
	}

	void WriteRetroLayers(Writer& writer, const std::string& prefix, const LightingSettings::RetroLayers& layers) {
		writer.NewPropertyWithValue(prefix + "Look", layers.Look);
		writer.NewPropertyWithValue(prefix + "Palette", layers.Palette);
		writer.NewPropertyWithValue(prefix + "LCDPalette", layers.LCDPalette);
		writer.NewPropertyWithValue(prefix + "InkEdges", layers.InkEdges);
		writer.NewPropertyWithValue(prefix + "GlitchOnBlasts", layers.GlitchOnBlasts);
		writer.NewPropertyWithValue(prefix + "PaperBackgroundOnly", layers.PaperBackgroundOnly);
		for (const auto& [name, member]: c_RetroFloatFields) {
			writer.NewPropertyWithValue(prefix + name, layers.*member);
		}
	}

	/// Reads a retro layer setting that has no property of its own below (the Game Boy style's, and the fields added since): its prefix picks whose
	/// layers, the rest which field. False, reading nothing, if it isn't one.
	bool ReadAnyRetroProperty(std::string_view propName, Reader& reader) {
		LightingSettings& lighting = g_PostProcessMan.GetLightingSettings();
		const std::pair<std::string_view, LightingSettings::RetroLayers*> owners[] = {
		    {"Retro", &lighting.Retro}, {"Synthwave", &lighting.StyleRetro[LightingSettings::StyleSynthwave]}, {"Hackerman", &lighting.StyleRetro[LightingSettings::StyleHackerman]},
		    {"Comic", &lighting.StyleRetro[LightingSettings::StyleComic]}, {"GameBoy", &lighting.StyleRetro[LightingSettings::StyleGameBoy]}, {"Variant", &lighting.VariantRetro}};
		for (const auto& [prefix, layers]: owners) {
			if (!propName.starts_with(prefix)) {
				continue;
			}
			std::string_view field = propName.substr(prefix.size());
			bool known = std::find(std::begin(c_RetroOtherFields), std::end(c_RetroOtherFields), field) != std::end(c_RetroOtherFields);
			for (const auto& [name, member]: c_RetroFloatFields) {
				known = known || field == name;
			}
			if (known) {
				ReadRetroField(*layers, field, reader.ReadPropValue());
				return true;
			}
		}
		return false;
	}
} // namespace

const std::string SettingsMan::c_ClassName = "SettingsMan";

const char* SettingsMan::DebugChannelName(DebugChannel channel) {
	static const char* const names[] = {"AI", "Path", "Pilot", "Climb", "Combat", "Squad", "Sandbox", "Perf", "Grid"};
	static_assert(std::size(names) == static_cast<size_t>(DebugChannel::Count));
	return channel < DebugChannel::Count ? names[static_cast<int>(channel)] : "";
}

SettingsMan::DebugChannel SettingsMan::DebugChannelFromName(const std::string& name) {
	for (int i = 0; i < static_cast<int>(DebugChannel::Count); ++i) {
		const char* channelName = DebugChannelName(static_cast<DebugChannel>(i));
		if (name.size() == std::strlen(channelName) && std::equal(name.begin(), name.end(), channelName, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })) {
			return static_cast<DebugChannel>(i);
		}
	}
	return DebugChannel::Count;
}

bool SettingsMan::DebugChannelOn(DebugChannel channel) const {
	// The environment variables the test harness (Gym.ps1 and friends) sets, read once: they switch channels on for the run whatever the settings say.
	static const unsigned s_FromEnvironment = []() {
		unsigned bits = 0;
		auto set = [&bits](DebugChannel which) { bits |= 1u << static_cast<int>(which); };
		if (std::getenv("CCCP_AI_LOG")) {
			for (DebugChannel which: {DebugChannel::AI, DebugChannel::Pilot, DebugChannel::Climb, DebugChannel::Combat, DebugChannel::Squad}) {
				set(which);
			}
		}
		if (std::getenv("CCCP_PATH_LOG")) {
			set(DebugChannel::Path);
			set(DebugChannel::Grid);
		}
		if (std::getenv("CCCP_SANDBOX_LOG")) {
			set(DebugChannel::Sandbox);
		}
		if (std::getenv("CCCP_PERF_LOG")) {
			set(DebugChannel::Perf);
		}
		return bits;
	}();
	return ((m_DebugChannels | s_FromEnvironment) >> static_cast<int>(channel)) & 1;
}

bool SettingsMan::TraceAllUnits() const {
	static const bool s_AllFromEnvironment = []() { const char* log = std::getenv("CCCP_AI_LOG"); return log && std::string(log) == "all"; }();
	return m_TraceAllUnits || s_AllFromEnvironment;
}

void SettingsMan::Clear() {
	m_SettingsPath = System::GetUserdataDirectory() + "Settings.ini";
	m_SettingsNeedOverwrite = false;
	for (std::string& startup: m_StartupPresets) {
		startup.clear();
	}
	s_ReadLightingSettingsVersion = 0;

	m_FlashOnBrainDamage = true;
	m_BlipOnRevealUnseen = false;
	m_UnheldItemsHUDDisplayRange = 0;
	m_AlwaysDisplayUnheldItemsInStrategicMode = true;
	m_SubPieMenuHoverOpenDelay = 1000;
	m_ClassicPieWheel = false;
	m_UnitMenuClickToOpen = true;
	m_UnitMenuTimeScale = 0.25F;
	m_PauseAIKey = "Ctrl+P";
	m_DefaultBackground.clear();
	m_EndlessMetaGameMode = false;
	m_EnableCrabBombs = false;
	m_EnableMantling = true;
	m_StrictClimbLimits = true;
	m_MeasuredBodySize = false;
	m_UnitWaterBehaviour = 2;
	m_UnitsStepOverObjects = true;
	m_UnitsCrossVehicles = true;
	m_UnitsPushObjects = true;
	m_ObjectPushStrength = 1.0F;
	m_SensesPassPlants = true;
	m_SensesPassRungs = true;
	m_SensesPassSpecks = true;
	m_EnableStuckPush = false;
	m_BodyGearSettlesAsScraps = true;
	m_GibChunks = GibChunkTuning();
	m_AICombat = AICombatTuning();
	m_SlowdownFixes = SlowdownFixTuning();
	m_NoSceneWrap = true;
	std::fill(std::begin(m_Toughness), std::end(m_Toughness), 1.0F);
	m_AISuppression = 1.0F;
	m_AIDigWillingness = 1.0F;
	m_AIDigsWithTracers = true;
	m_AIThreatAvoidance = 1.0F;
	m_AIRecklessness = 0.5F;
	ResetLadderSettings();
	ResetCarryWeightSettings();
	m_AISpawnDiggerChance = 0.0F;
	m_BleedOutChance = 0.0F;
	m_WoundBleedTime = 1.0F;
	m_ShipGroundClearance = 12.0F;
	m_ShipGraceZone = 400.0F;
	m_CameraOverscroll = 400.0F;
	m_WeaponTerrainDamage = 1.0F;
	m_WeaponUnitDamage = 1.0F;
	m_WeaponUnitDamageGibs = false;
	m_AISpawnDiggerType = 1;
	m_AISteadyBeforeJet = true;
	m_AIWaitForFuel = true;
	m_AIWaitsAtDoors = true;
	m_AIEnemyBuysReinforcements = true;
	m_AIEnemyUsesPowers = true;
	m_AISquadFollowsYou = true;
	m_AIUsesVehicles = true;
	m_AIRidesShips = true;
	m_AIShipRideNoRoute = true;
	m_AIShipRideLongWalk = true;
	m_AIShipRideLongWalkDistance = 500.0F;
	m_AIShipRideRange = 1500.0F;
	m_AIShipRideWait = 20.0F;
	m_AIShipRideDropRing = 150.0F;
	m_AIVehiclesWaitForRiders = true;
	m_BoardFromBelow = true;
	m_BoardFromBelowReach = 24.0F;
	m_AICoverDigs = true;
	m_AICoverFallsBack = true;
	m_AICoverLiesFlat = true;
	m_AIBreaksWhenShaken = true;
	m_AISharedSpotting = false;
	m_AISharedSpottingHeadStart = 1.0F;
	m_AIScavengesBetterWeapons = true;
	m_AIMaxUnitsPerSpot = 2;
	m_AIUnitsCallLifts = true;
	m_DeadCrewFallOut = true;
	m_PassengersFireFromVehicles = true;
	m_DirectOrders = DirectOrderTuning();
	m_DigSafety = DigSafetyTuning();
	m_AIDodgesFallingGround = true;
	m_PathTuning = PathTuning();
	m_NavDebugOverlay = 0;
	m_DebugTeam = 0;
	m_UnitInspector = 0;
	m_ShowSquadLinks = false;
	m_ShowOrderLabels = false;
	m_CombatOverlay = 0;
	m_ShowRecentSolves = false;
	m_ShowTerrainUpdates = false;
	m_ShowLightSources = false;
	m_ShowSunDirection = false;
	m_WorldSimOverlay = 0;
	m_SandboxStrokeLog = false;
	m_SandboxSpotReach = false;
	m_SandboxGroupBadges = true;
	m_CommanderBarKey = "F11";
	m_ScenarioFactionPicks.clear();
	m_ScenarioSettingValues.clear();
	m_ScenarioFlyingCraft = false;
	m_SandboxOrderGlyphs = 1;
	m_SandboxSpawnStats = 1;
	m_SandboxSpawnSort = 0;
	m_SandboxAttackPings = true;
	m_SandboxNoRouteMarkers = true;
	m_SandboxMinimap = false;
	m_BattleAlerts = true;
	m_BattleAlertLines = 6;
	m_BattleAlertSeconds = 12;
	m_BattleKillFeed = true;
	m_BattleKillLines = 6;
	m_BattleKillSeconds = 10;
	m_BattleAlertSounds = true;
	m_BattleAlertVolume = 0.6F;
	m_BattleAskFirst = true;
	m_BattleShowInfo = true;
	m_BattleShowObjectives = true;
	m_BattleObjectiveLook = 0;
	m_BattleShowSpawnZones = true;
	m_BattleShowModeZones = true;
	m_ArmyOverview = false;
	m_LightsBySource = false;
	m_SandboxCharacterState = false;
	m_SandboxAutoBattle = false;
	m_SandboxPaintAudit = false;
	m_SandboxSelectionCamera = false;
	m_SandboxEffects = false;
	m_SandboxGas = false;
	m_SandboxAir = false;
	m_SandboxSimState = false;
	m_SandboxBarRight = false;
	m_SandboxOrdersOverlay = 0;
	m_DebugChannels = 0;
	m_TraceAllUnits = false;
	m_ShowFPSAndVersion = true;
	m_ShowNotices = true;
	m_MapBrowserFilters = MapBrowserFilters();
	m_LastMap.clear();
	m_CrabBombThreshold = 42;
	m_ShowEnemyHUD = true;
	m_ShowUnitTags = false;
	m_ShowCPUAimReticles = true;
	m_FreeAimThroughTerrain = false;
	m_ShowAlarmExclamation = true;
	m_EnableSmartBuyMenuNavigation = true;
	m_AutomaticGoldDeposit = true;

	m_NetworkServerAddress = "127.0.0.1:8000";
	m_PlayerNetworkName = "Dummy";
	m_NATServiceAddress = "127.0.0.1:61111";
	m_NATServerName = "DefaultServerName";
	m_NATServerPassword = "DefaultServerPassword";
	m_UseExperimentalMultiplayerSpeedBoosts = true;

	m_AllowSavingToBase = false;
	m_ShowForeignItems = true;
	m_ShowMetaScenes = false;

	m_DisableLuaJIT = false;
	m_EnableLuaDebugging = false;
	m_RecommendedMOIDCount = 512;
	m_SceneBackgroundAutoScaleMode = 1;
	m_DisableFactionBuyMenuThemes = false;
	m_DisableMedievalUnits = false;
	m_DisableFactionBuyMenuThemeCursors = false;
	m_PathFinderGridNodeSize = SCENEGRIDSIZE;
	m_AIUpdateInterval = 2;
	m_NumberOfLuaStatesOverride = -1;
	m_ForceImmediatePathingRequestCompletion = false;

	m_SkipIntro = false;
	m_ShowToolTips = true;
	m_DisableLoadingScreenProgressReport = true;
	m_LoadingScreenProgressReportPrecision = 100;
	m_MenuTransitionDurationMultiplier = 1.0F;

	m_DrawAtomGroupVisualizations = false;
	m_DrawHandAndFootGroupVisualizations = false;
	m_DrawLimbPathVisualizations = false;
	m_PrintDebugInfo = false;
	m_MeasureModuleLoadTime = false;

	m_DisabledMods.clear();
	m_EnabledGlobalScripts.clear();
}

int SettingsMan::Initialize() {
	if (const char* settingsTempPath = std::getenv("CCCP_SETTINGSPATH")) {
		m_SettingsPath = std::string(settingsTempPath);
	}

	Reader settingsReader(m_SettingsPath, false, nullptr, true, true);

	if (!settingsReader.ReaderOK()) {
		Writer settingsWriter(m_SettingsPath);
		RTEAssert(settingsWriter.WriterOK(), "After failing to open the " + m_SettingsPath + ", could not then even create a new one to save settings to!\nAre you trying to run the game from a read-only disk?\nYou need to install the game to a writable area before running it!");

		// Settings file doesn't need to be populated with anything right now besides this manager's ClassName for serialization. It will be overwritten with the full list of settings with default values from all the managers before modules start loading.
		settingsWriter.ObjectStart(GetClassName());
		settingsWriter.EndWrite();

		m_SettingsNeedOverwrite = true;

		Reader newSettingsReader(m_SettingsPath, false, nullptr, false, true);
		return Serializable::Create(newSettingsReader);
	}

	s_OldPathLineWeight = false;
	int failureCode = Serializable::Create(settingsReader);
	if (s_OldPathLineWeight) {
		// Saved before route straightness followed the risk slider, at the 0 it came with then: the new default, as a fresh install has.
		m_PathTuning.LineWeight = PathTuning().LineWeight;
		s_OldPathLineWeight = false;
	}

	if (GetAnyExperimentalSettingsEnabled()) {
		// Show a message box to annoy people as much as possible while they're using experimental settings, so they can't leave it on accidentally
		RTEError::ShowMessageBox("Experimental settings are enabled!\nThis may break mods, crash the game, corrupt saves or worse.\nUse at your own risk.");
	}

	return failureCode;
}

namespace {
	/// The settings file's text for the settings as they are now.
	std::string SettingsText() {
		auto stream = std::make_unique<std::ostringstream>();
		*stream << std::fixed << std::setprecision(6);
		std::ostringstream* text = stream.get();
		Writer settingsWriter(std::move(stream));
		g_SettingsMan.Save(settingsWriter);
		return text->str();
	}
} // namespace

void SettingsMan::UpdateSettingsFile() const {
	std::string text = SettingsText();
	std::ofstream file(m_SettingsPath, std::ios::out | std::ios::trunc);
	file << text;
	file.close();
	if (file) {
		m_LastWrittenSettings = std::move(text);
	}
}

std::string SettingsMan::KeyComboName(int scancode, unsigned int modifiers) {
	SDL_Scancode key = static_cast<SDL_Scancode>(scancode);
	if (key == SDL_SCANCODE_UNKNOWN || (key >= SDL_SCANCODE_LCTRL && key <= SDL_SCANCODE_RGUI)) {
		return std::string();
	}
	std::string name;
	if (modifiers & SDL_KMOD_CTRL) {
		name += "Ctrl+";
	}
	if (modifiers & SDL_KMOD_ALT) {
		name += "Alt+";
	}
	if (modifiers & SDL_KMOD_SHIFT) {
		name += "Shift+";
	}
	return name + SDL_GetScancodeName(key);
}

bool SettingsMan::IsPauseAIKey(int scancode, unsigned int modifiers) const {
	// The GUI (Windows) key is left out of the name, so a combo held with it isn't this key.
	return !m_PauseAIKey.empty() && !(modifiers & SDL_KMOD_GUI) && KeyComboName(scancode, modifiers) == m_PauseAIKey;
}

void SettingsMan::SaveSettingsIfChanged() const {
	static std::chrono::steady_clock::time_point s_LastLook;
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (now - s_LastLook < std::chrono::seconds(1)) {
		return;
	}
	s_LastLook = now;
	if (SettingsText() != m_LastWrittenSettings) {
		UpdateSettingsFile();
	}
}

int SettingsMan::ReadProperty(const std::string_view& propName, Reader& reader) {
	// The values are mostly read with std::stoi and std::stof, which throw on a value that isn't a number ("1,5" in a European locale, an empty
	// value, a file from another build). Nothing above caught it, so the game ended at start-up with nothing in the log. Now the property keeps
	// its default and the console log says which one it was.
	try {
		return ReadPropertyUnchecked(propName, reader);
	} catch (const std::logic_error&) {
		g_ConsoleMan.PrintString("ERROR: Settings.ini: the value of " + std::string(propName) + " isn't a number this version can read; the default is kept.");
		return 0;
	}
}

int SettingsMan::ReadPropertyUnchecked(const std::string_view& propName, Reader& reader) {
	StartPropertyList(if (ReadAnyRetroProperty(propName, reader)) { return 0; } return Serializable::ReadProperty(propName, reader));

	MatchProperty("PaletteFile", { reader >> g_FrameMan.m_PaletteFile; });
	MatchProperty("ResolutionX", { reader >> g_WindowMan.m_ResX; });
	MatchProperty("ResolutionY", { reader >> g_WindowMan.m_ResY; });
	MatchProperty("ResolutionMultiplier", { reader >> g_WindowMan.m_ResMultiplier; });
	MatchProperty("EnableVSync", { reader >> g_WindowMan.m_EnableVSync; });
	MatchProperty("Fullscreen", { reader >> g_WindowMan.m_Fullscreen; });
	MatchProperty("UseMultiDisplays", { reader >> g_WindowMan.m_UseMultiDisplays; });
	MatchProperty("TwoPlayerSplitscreenVertSplit", { reader >> g_FrameMan.m_TwoPlayerVSplit; });
	MatchProperty("SwimmingAndDrowning", { ActorWater::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("BurningUnits", { ActorFire::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("SmokeBlocksSight", { SmokeGrid::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("FlowingLiquids", { FluidSim::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("CollapseFloatingStays", { TerrainCollapse::GetTuning().FloatingStays = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("CollapseNeckWidth", { TerrainCollapse::GetTuning().NeckWidth = std::clamp(std::stoi(reader.ReadPropValue()), 0, 16); });
	MatchProperty("CollapseThinNeckWidth", { TerrainCollapse::GetTuning().ThinNeckWidth = std::clamp(std::stoi(reader.ReadPropValue()), 0, 16); });
	MatchProperty("CollapseOverhangLength", { TerrainCollapse::GetTuning().OverhangLength = std::clamp(std::stoi(reader.ReadPropValue()), 0, 400); });
	MatchProperty("CollapseMaxPiece", { TerrainCollapse::GetTuning().MaxPiecePixels = std::clamp(std::stoi(reader.ReadPropValue()), 500, 200000); });
	MatchProperty("CollapseCheckMinForce", { TerrainCollapse::GetTuning().CheckMinForce = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 10000.0F); });
	MatchProperty("CollapseMinFitting", { TerrainCollapse::GetTuning().MinFittingPixels = std::clamp(std::stoi(reader.ReadPropValue()), 0, 5000); });
	MatchProperty("CollapseBreakStrength", { TerrainCollapse::GetTuning().BreakStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.1F, 10.0F); });
	MatchProperty("CollapseShatterSpeed", { TerrainCollapse::GetTuning().ShatterSpeed = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 100.0F); });
	MatchProperty("CollapseCrackSpeed", { TerrainCollapse::GetTuning().CrackSpeed = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 100.0F); });
	MatchProperty("CollapseCrumbleSpeed", { TerrainCollapse::GetTuning().CrumbleSpeed = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 100.0F); });
	MatchProperty("CollapseSplinterSpeed", { TerrainCollapse::GetTuning().SplinterSpeed = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 100.0F); });
	MatchProperty("CollapseBendSpeed", { TerrainCollapse::GetTuning().BendSpeed = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 100.0F); });
	MatchProperty("CollapseBlastPush", { TerrainCollapse::GetTuning().BlastPush = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("CollapseBlastLoosens", { TerrainCollapse::GetTuning().BlastLoosens = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("CollapseBlastLoosenStrength", { TerrainCollapse::GetTuning().BlastLoosenStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("CollapseBlastSnapsScenery", { TerrainCollapse::GetTuning().BlastSnapsScenery = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("CollapseBlastPushesLateLook", { TerrainCollapse::GetTuning().BlastPushesLateLook = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("CollapseAirWavePushesPieces", { TerrainCollapse::GetTuning().AirWavePushesPieces = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("CollapseCrushPixels", { TerrainCollapse::GetTuning().CrushPixels = std::clamp(std::stoi(reader.ReadPropValue()), 0, 500); });
	MatchProperty("CollapseScuffStrength", { TerrainCollapse::GetTuning().ScuffStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 3.0F); });
	MatchProperty("CollapseRestSeconds", { TerrainCollapse::GetTuning().RestSeconds = std::clamp(std::stof(reader.ReadPropValue()), 0.1F, 30.0F); });
	MatchProperty("CollapseHitDamage", { TerrainCollapse::GetTuning().HitDamage = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 20.0F); });
	MatchProperty("CollapseHitMinSpeed", { TerrainCollapse::GetTuning().HitMinSpeed = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 30.0F); });
	MatchProperty("CollapseMaxDebrisPerUpdate", { TerrainCollapse::GetTuning().MaxDebrisPerUpdate = std::clamp(std::stoi(reader.ReadPropValue()), 0, 5000); });
	MatchProperty("CollapseDebrisKeptPercent", { TerrainCollapse::GetTuning().DebrisKeptPercent = std::clamp(std::stoi(reader.ReadPropValue()), 0, 100); });
	MatchProperty("CollapseHitMinPixels", { TerrainCollapse::GetTuning().HitMinPixels = std::clamp(std::stoi(reader.ReadPropValue()), 0, 5000); });
	MatchProperty("CollapseHitMassCap", { TerrainCollapse::GetTuning().HitMassCap = std::clamp(std::stof(reader.ReadPropValue()), 0.1F, 50.0F); });
	MatchProperty("CollapseHitKnockback", { TerrainCollapse::GetTuning().HitKnockback = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 10.0F); });
	MatchProperty("CollapseBuildings", { TerrainCollapse::SetBuildingsFall(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("TreeStrayBulletPercent", { TerrainTrees::SetStrayBulletPercent(std::stoi(reader.ReadPropValue())); });
	MatchProperty("UnitsBumpIntoTrees", { TerrainTrees::SetUnitsCollide(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("FallingGroundPassesTrees", { TerrainCollapse::SetPassesTrees(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("ShowHoldZones", { TerrainCollapse::SetShowHoldZones(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("ZeroGTerrainFollowsGravity", { ZeroGravity::SetTerrainFollowsGravity(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("ZeroGLiquidsFloat", { ZeroGravity::SetLiquidsFloat(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("ZeroGCountZones", { ZeroGravity::SetCountZones(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("ZeroGJetpackAnyDirection", { ZeroGravity::SetJetpackAnyDirection(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("ZeroGAIFlies", { ZeroGravity::SetAIFlies(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("ZeroGPushOff", { ZeroGravity::SetPushOff(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("ZeroGFlySpeed", { ZeroGravity::SetFlySpeed(std::clamp(std::stof(reader.ReadPropValue()), 1.0F, 20.0F)); });
	MatchProperty("ZeroGPushSpeed", { ZeroGravity::SetPushSpeed(std::clamp(std::stof(reader.ReadPropValue()), 1.0F, 15.0F)); });
	MatchProperty("UnitsInFrontOfTrees", { TerrainTrees::SetDrawnBehindUnits(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("CollapseLeafBreakEase", { TerrainCollapse::GetTuning().LeafBreakEase = std::clamp(std::stof(reader.ReadPropValue()), 1.0F, 50.0F); });
	MatchProperty("CollapseLeafLitter", { TerrainCollapse::GetTuning().LeafLitter = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("LightningStrikes", { WeatherLightning::SetStrikes(static_cast<WeatherLightning::Strikes>(std::clamp(std::stoi(reader.ReadPropValue()), 0, 2))); });
	MatchProperty("AIThreatMemory", { ThreatMemory::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("Gas", { GasGrid::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("GasShown", { GasGrid::SetShown(std::stof(reader.ReadPropValue())); });
	MatchProperty("AirAndWind", { AirPressure::SetOn(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("WindGusts", { AirPressure::GetTuning().Gusts = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 3.0F); });
	MatchProperty("WindShifts", { AirPressure::GetTuning().Shifts = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 3.0F); });
	MatchProperty("WindBreeze", { AirPressure::GetTuning().Breeze = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 200.0F); });
	MatchProperty("AirOverall", { AirPressure::GetTuning().Overall = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("AirBlastStrength", { AirPressure::GetTuning().BlastStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("AirBlastReach", { AirPressure::GetTuning().BlastReach = std::clamp(std::stof(reader.ReadPropValue()), 0.25F, 3.0F); });
	MatchProperty("AirPushStrength", { AirPressure::GetTuning().PushStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("AirUnitPush", { AirPressure::GetTuning().UnitPush = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("AirLiquidThrow", { AirPressure::GetTuning().LiquidThrow = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("WindStrength", { AirPressure::GetTuning().WindStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("WindCarriesGas", { AirPressure::GetTuning().WindGas = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("BlastWaves", { AirPressure::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("WindMovesSmoke", { AirPressure::SetWindMovesSmoke(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("Magic", { MagicEffects::SetOn(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("MagicPushStrength", { MagicEffects::GetTuning().PushStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("MagicUnitPush", { MagicEffects::GetTuning().UnitPush = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("MagicAirGust", { MagicEffects::GetTuning().AirGust = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("MagicSpellEnergy", { MagicEffects::GetTuning().SpellEnergy = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("MagicAICasting", { MagicEffects::GetTuning().AICasting = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("MagicEnergyRegen", { MagicEffects::GetTuning().EnergyRegen = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("WaterFreezes", { FluidSim::SetFreezingEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("BloodFlows", { FluidSim::SetBloodFlows(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("LiquidsDrainBottom", { FluidSim::SetDrainsBottom(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("LiquidsDrainSides", { FluidSim::SetDrainsSides(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("PowdersFallOut", { FluidSim::SetPowdersFallOut(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("LoosePowders", { FluidSim::SetPowdersEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("TerrainCollapse", { TerrainCollapse::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("TerrainFire", { TerrainFire::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("EmberIgniteChance", { TerrainFire::SetEmberIgniteChance(std::stof(reader.ReadPropValue())); });
	MatchProperty("RopeReaction", { RopeSim::SetReaction(std::stof(reader.ReadPropValue())); });
	MatchProperty("RopeSettleSeconds", { RopeSim::SetSettleSeconds(std::stof(reader.ReadPropValue())); });
	MatchProperty("CandleBurnMinutes", { TerrainCandle::SetBurnMinutes(std::stof(reader.ReadPropValue())); });
	MatchProperty("ModernHUD", { ModernHUD::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("UILight", { ToolLight::Strength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 2.0F); });
	MatchProperty("UILightOwn", { ToolLight::Mode = std::stoi(reader.ReadPropValue()) != 0 ? ToolLight::Source::OwnLight : ToolLight::Source::FollowGame; });
	MatchProperty("UILightAngle", { ToolLight::OwnAngle = std::clamp(std::stof(reader.ReadPropValue()), -90.0F, 90.0F); });
	MatchProperty("UILightHeight", { ToolLight::OwnHeight = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("UILightBrightness", { ToolLight::OwnBrightness = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("UILightRed", { ToolLight::OwnColor[0] = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("UILightGreen", { ToolLight::OwnColor[1] = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("UILightBlue", { ToolLight::OwnColor[2] = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("UINearbyLights", { ToolLight::NearbyLights = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("SmoothHUDText", { TextOverlay::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("LightingSettingsVersion", { s_ReadLightingSettingsVersion = std::stoi(reader.ReadPropValue()); });
	MatchProperty("GraphicsQuality", {
		g_PostProcessMan.GetLightingSettings().GraphicsQuality = std::clamp(std::stoi(reader.ReadPropValue()), 0, static_cast<int>(LightingSettings::QualityCustom));
		// Settings saved before the shadow effects existed have no values for them: follow the saved preset. Values in the file come after this and win.
		g_PostProcessMan.GetLightingSettings().ApplyShadowPreset(g_PostProcessMan.GetLightingSettings().GraphicsQuality);
		// The light-spreading steps weren't saved before: follow the saved preset too (Low 3, Ultra 12), so they don't go back to 6.
		LightingSettings preset;
		preset.ApplyQualityPreset(g_PostProcessMan.GetLightingSettings().GraphicsQuality);
		g_PostProcessMan.GetLightingSettings().PropagationIterationsPerFrame = preset.PropagationIterationsPerFrame;
	});
	MatchProperty("LightingPropagationSteps", { g_PostProcessMan.GetLightingSettings().PropagationIterationsPerFrame = std::clamp(std::stoi(reader.ReadPropValue()), 1, 32); });
	MatchProperty("UnitShadows", { g_PostProcessMan.GetLightingSettings().UnitShadows = std::stof(reader.ReadPropValue()); });
	MatchProperty("SunShadows", { g_PostProcessMan.GetLightingSettings().SunShadows = std::stof(reader.ReadPropValue()); });
	MatchProperty("SunShadowMap", { g_PostProcessMan.GetLightingSettings().SunShadowMap = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("ShelterMask", { g_PostProcessMan.GetLightingSettings().ShelterMask = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("ShelterSoftness", { g_PostProcessMan.GetLightingSettings().ShelterSoftness = std::stof(reader.ReadPropValue()); });
	MatchProperty("SunShadowSoftness", { g_PostProcessMan.GetLightingSettings().SunShadowSoftness = std::stof(reader.ReadPropValue()); });
	MatchProperty("BackgroundShadows", { g_PostProcessMan.GetLightingSettings().BackgroundShadows = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("BackgroundShadowLength", { g_PostProcessMan.GetLightingSettings().BackgroundShadowLength = std::clamp(std::stof(reader.ReadPropValue()), 0.25F, 3.0F); });
	MatchProperty("ContactShading", { g_PostProcessMan.GetLightingSettings().ContactShading = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightingEnabled", { g_PostProcessMan.GetLightingSettings().Enabled = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LightingAmbient", {
		std::string value = reader.ReadPropValue();
		if (s_ReadLightingSettingsVersion >= 2) {
			g_PostProcessMan.GetLightingSettings().Ambient = ReadVec3(value, g_PostProcessMan.GetLightingSettings().Ambient);
		}
	});
	MatchProperty("LightingForegroundAmbient", {
		std::string value = reader.ReadPropValue();
		if (s_ReadLightingSettingsVersion >= 2) {
			g_PostProcessMan.GetLightingSettings().ForegroundAmbient = ReadVec3(value, g_PostProcessMan.GetLightingSettings().ForegroundAmbient);
		}
	});
	MatchProperty("LightingSkyColor", { g_PostProcessMan.GetLightingSettings().SkyColor = ReadVec3(reader.ReadPropValue(), g_PostProcessMan.GetLightingSettings().SkyColor); });
	MatchProperty("LightingAirFalloff", { g_PostProcessMan.GetLightingSettings().AirFalloff = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightingSolidFalloff", { g_PostProcessMan.GetLightingSettings().SolidFalloff = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightingDebugView", { int view = std::stoi(reader.ReadPropValue()); if (!s_ReadingStartupPreset) { g_PostProcessMan.GetLightingSettings().DebugView = view; } }); // Read only, for automated screenshots.
	MatchProperty("DeepNightDarkness", { g_PostProcessMan.GetLightingSettings().DeepNightDarkness = std::stof(reader.ReadPropValue()); });
	MatchProperty("SkyFollowsTime", { g_PostProcessMan.GetLightingSettings().SkyFollowsTime = std::stof(reader.ReadPropValue()); });
	MatchProperty("GodRays", { g_PostProcessMan.GetLightingSettings().GodRays = std::stof(reader.ReadPropValue()); });
	MatchProperty("FogVolume", { g_PostProcessMan.GetLightingSettings().FogVolume = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.5F); });
	MatchProperty("FogMorningMist", { g_PostProcessMan.GetLightingSettings().FogMorningMist = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("FogOpacity", { g_PostProcessMan.GetLightingSettings().FogOpacity = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("FogClearSeconds", { g_PostProcessMan.GetLightingSettings().FogClearSeconds = std::clamp(std::stof(reader.ReadPropValue()), 3.0F, 120.0F); });
	MatchProperty("LightningBolts", { g_PostProcessMan.GetLightingSettings().LightningBolts = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LightningBrightness", { g_PostProcessMan.GetLightingSettings().LightningBrightness = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 2.0F); });
	MatchProperty("StormFlashes", { g_PostProcessMan.GetLightingSettings().StormFlashes = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("GodRayDecay", { reader.ReadPropValue(); }); // In older settings files. Light shafts now follow where the sun reaches, so they have no decay to set.
	MatchProperty("AtmosphereHaze", { g_PostProcessMan.GetLightingSettings().AtmosphereHaze = std::stof(reader.ReadPropValue()); });
	MatchProperty("AtmosphereColor", { g_PostProcessMan.GetLightingSettings().AtmosphereColor = ReadVec3(reader.ReadPropValue(), g_PostProcessMan.GetLightingSettings().AtmosphereColor); });
	MatchProperty("WeatherType", { g_PostProcessMan.GetLightingSettings().WeatherType = std::stoi(reader.ReadPropValue()); });
	MatchProperty("WeatherName", { g_PostProcessMan.GetLightingSettings().WeatherName = reader.ReadPropValue(); });
	MatchProperty("OverrideMapWeather", { g_PostProcessMan.GetLightingSettings().OverrideMapWeather = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("CustomWeather", { g_PostProcessMan.GetLightingSettings().CustomWeather = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WeatherGlow", { g_PostProcessMan.GetLightingSettings().WeatherGlow = std::stof(reader.ReadPropValue()); });
	MatchProperty("WeatherIntensity", { g_PostProcessMan.GetLightingSettings().WeatherIntensity = std::stof(reader.ReadPropValue()); });
	MatchProperty("WeatherSpeed", { g_PostProcessMan.GetLightingSettings().WeatherSpeed = std::stof(reader.ReadPropValue()); });
	MatchProperty("WeatherEverywhere", { g_PostProcessMan.GetLightingSettings().WeatherEverywhere = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WeatherOverBackground", { g_PostProcessMan.GetLightingSettings().WeatherOverBackground = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("Wind", { g_PostProcessMan.GetLightingSettings().Wind = std::stof(reader.ReadPropValue()); });
	MatchProperty("TimeOfDay", { g_PostProcessMan.GetLightingSettings().TimeOfDay = std::stof(reader.ReadPropValue()); });
	MatchProperty("DayLengthMinutes", { g_PostProcessMan.GetLightingSettings().DayLengthMinutes = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightingGlowIntensity", { g_PostProcessMan.GetLightingSettings().GlowLightIntensity = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightingGlowRadiusScale", { g_PostProcessMan.GetLightingSettings().GlowLightRadiusScale = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightingMaxScreenLights", { g_PostProcessMan.GetLightingSettings().MaxScreenLights = std::max(std::stoi(reader.ReadPropValue()), 0); });
	MatchProperty("LightingShadowStrength", { g_PostProcessMan.GetLightingSettings().ShadowStrength = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightShadowField", { g_PostProcessMan.GetLightingSettings().LightShadowField = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("SoftWallLight", { g_PostProcessMan.GetLightingSettings().SoftWallLight = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LightShadowSoftness", { g_PostProcessMan.GetLightingSettings().LightShadowSoftness = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 2.0F); });
	MatchProperty("RadianceCascades", { g_PostProcessMan.GetLightingSettings().RadianceCascades = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("GIStrength", { g_PostProcessMan.GetLightingSettings().GIStrength = std::stof(reader.ReadPropValue()); });
	MatchProperty("GIBounce", { g_PostProcessMan.GetLightingSettings().GIBounce = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightingIndirect", { g_PostProcessMan.GetLightingSettings().IndirectLight = std::stof(reader.ReadPropValue()); });
	MatchProperty("PostScanlines", { g_PostProcessMan.GetLightingSettings().Scanlines = std::stof(reader.ReadPropValue()); });
	MatchProperty("CRTStyle", { g_PostProcessMan.GetLightingSettings().CRTStyle = std::clamp(std::stoi(reader.ReadPropValue()), 0, 3); });
	MatchProperty("ScreenStyle", { g_PostProcessMan.SetScreenStyle(std::stoi(reader.ReadPropValue())); });
	MatchProperty("ScreenStyleStrength", { g_PostProcessMan.SetScreenStyleStrength(std::stof(reader.ReadPropValue())); });
	MatchProperty("AsciiModeEverywhere", { g_PostProcessMan.GetLightingSettings().Ascii.Everywhere = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("AsciiModeBackground", { g_PostProcessMan.GetLightingSettings().Ascii.Background = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("AsciiModeSize", { g_PostProcessMan.GetLightingSettings().Ascii.Size = std::clamp(std::stoi(reader.ReadPropValue()), 1, 4); });
	MatchProperty("AsciiModeEdgeGlyphs", { g_PostProcessMan.GetLightingSettings().Ascii.EdgeGlyphs = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("AsciiModeContrast", { g_PostProcessMan.GetLightingSettings().Ascii.Contrast = std::clamp(std::stof(reader.ReadPropValue()), 0.5F, 2.0F); });
	MatchProperty("AsciiModeExposure", { g_PostProcessMan.GetLightingSettings().Ascii.Exposure = std::clamp(std::stof(reader.ReadPropValue()), 0.25F, 3.0F); });
	MatchProperty("AsciiModeColours", { g_PostProcessMan.GetLightingSettings().Ascii.Colours = std::clamp(std::stoi(reader.ReadPropValue()), 0, static_cast<int>(LightingSettings::AsciiColourCount) - 1); });
	MatchProperty("AsciiModeCellFill", { g_PostProcessMan.GetLightingSettings().Ascii.CellFill = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("AsciiModeGlow", { g_PostProcessMan.GetLightingSettings().Ascii.Glow = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("AsciiModeChurn", { g_PostProcessMan.GetLightingSettings().Ascii.Churn = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("AsciiModeWorldLocked", { g_PostProcessMan.GetLightingSettings().Ascii.WorldLocked = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("RetroLook", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Look", reader.ReadPropValue()); });
	MatchProperty("RetroPalette", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Palette", reader.ReadPropValue()); });
	MatchProperty("RetroInkEdges", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "InkEdges", reader.ReadPropValue()); });
	MatchProperty("RetroPaperBackgroundOnly", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "PaperBackgroundOnly", reader.ReadPropValue()); });
	MatchProperty("RetroGlitchOnBlasts", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "GlitchOnBlasts", reader.ReadPropValue()); });
	MatchProperty("RetroPaletteStrength", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "PaletteStrength", reader.ReadPropValue()); });
	MatchProperty("RetroNeonGlow", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "NeonGlow", reader.ReadPropValue()); });
	MatchProperty("RetroEdgeLines", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "EdgeLines", reader.ReadPropValue()); });
	MatchProperty("RetroSky", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Sky", reader.ReadPropValue()); });
	MatchProperty("RetroGridSpeed", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "GridSpeed", reader.ReadPropValue()); });
	MatchProperty("RetroVHS", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "VHS", reader.ReadPropValue()); });
	MatchProperty("RetroGlitch", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Glitch", reader.ReadPropValue()); });
	MatchProperty("RetroCurvature", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Curvature", reader.ReadPropValue()); });
	MatchProperty("RetroCRT", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "CRT", reader.ReadPropValue()); });
	MatchProperty("RetroFlicker", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Flicker", reader.ReadPropValue()); });
	MatchProperty("RetroPosterize", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Posterize", reader.ReadPropValue()); });
	MatchProperty("RetroDither", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Dither", reader.ReadPropValue()); });
	MatchProperty("RetroPixelate", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Pixelate", reader.ReadPropValue()); });
	MatchProperty("RetroASCII", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "ASCII", reader.ReadPropValue()); });
	MatchProperty("RetroASCIIBackground", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "ASCIIBackground", reader.ReadPropValue()); });
	MatchProperty("RetroAfterglow", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Afterglow", reader.ReadPropValue()); });
	MatchProperty("RetroTerminalOverlay", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "TerminalOverlay", reader.ReadPropValue()); });
	MatchProperty("RetroCelShading", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "CelShading", reader.ReadPropValue()); });
	MatchProperty("RetroCelBands", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "CelBands", reader.ReadPropValue()); });
	MatchProperty("RetroHalftone", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Halftone", reader.ReadPropValue()); });
	MatchProperty("RetroColorHalftone", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "ColorHalftone", reader.ReadPropValue()); });
	MatchProperty("RetroPaperGrain", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "PaperGrain", reader.ReadPropValue()); });
	MatchProperty("RetroMisregister", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "Misregister", reader.ReadPropValue()); });
	MatchProperty("RetroComicFX", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "ComicFX", reader.ReadPropValue()); });
	MatchProperty("RetroPanelFrame", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "PanelFrame", reader.ReadPropValue()); });
	MatchProperty("RetroTrackingBand", { ReadRetroField(g_PostProcessMan.GetLightingSettings().Retro, "TrackingBand", reader.ReadPropValue()); });
	MatchProperty("SynthwaveLook", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Look", reader.ReadPropValue()); });
	MatchProperty("SynthwavePalette", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Palette", reader.ReadPropValue()); });
	MatchProperty("SynthwaveInkEdges", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "InkEdges", reader.ReadPropValue()); });
	MatchProperty("SynthwavePaperBackgroundOnly", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "PaperBackgroundOnly", reader.ReadPropValue()); });
	MatchProperty("SynthwaveGlitchOnBlasts", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "GlitchOnBlasts", reader.ReadPropValue()); });
	MatchProperty("SynthwavePaletteStrength", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "PaletteStrength", reader.ReadPropValue()); });
	MatchProperty("SynthwaveNeonGlow", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "NeonGlow", reader.ReadPropValue()); });
	MatchProperty("SynthwaveEdgeLines", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "EdgeLines", reader.ReadPropValue()); });
	MatchProperty("SynthwaveSky", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Sky", reader.ReadPropValue()); });
	MatchProperty("SynthwaveGridSpeed", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "GridSpeed", reader.ReadPropValue()); });
	MatchProperty("SynthwaveVHS", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "VHS", reader.ReadPropValue()); });
	MatchProperty("SynthwaveGlitch", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Glitch", reader.ReadPropValue()); });
	MatchProperty("SynthwaveCurvature", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Curvature", reader.ReadPropValue()); });
	MatchProperty("SynthwaveCRT", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "CRT", reader.ReadPropValue()); });
	MatchProperty("SynthwaveFlicker", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Flicker", reader.ReadPropValue()); });
	MatchProperty("SynthwavePosterize", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Posterize", reader.ReadPropValue()); });
	MatchProperty("SynthwaveDither", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Dither", reader.ReadPropValue()); });
	MatchProperty("SynthwavePixelate", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Pixelate", reader.ReadPropValue()); });
	MatchProperty("SynthwaveASCII", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "ASCII", reader.ReadPropValue()); });
	MatchProperty("SynthwaveASCIIBackground", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "ASCIIBackground", reader.ReadPropValue()); });
	MatchProperty("SynthwaveAfterglow", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Afterglow", reader.ReadPropValue()); });
	MatchProperty("SynthwaveTerminalOverlay", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "TerminalOverlay", reader.ReadPropValue()); });
	MatchProperty("SynthwaveCelShading", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "CelShading", reader.ReadPropValue()); });
	MatchProperty("SynthwaveCelBands", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "CelBands", reader.ReadPropValue()); });
	MatchProperty("SynthwaveHalftone", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Halftone", reader.ReadPropValue()); });
	MatchProperty("SynthwaveColorHalftone", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "ColorHalftone", reader.ReadPropValue()); });
	MatchProperty("SynthwavePaperGrain", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "PaperGrain", reader.ReadPropValue()); });
	MatchProperty("SynthwaveMisregister", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "Misregister", reader.ReadPropValue()); });
	MatchProperty("SynthwaveComicFX", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "ComicFX", reader.ReadPropValue()); });
	MatchProperty("SynthwavePanelFrame", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "PanelFrame", reader.ReadPropValue()); });
	MatchProperty("SynthwaveTrackingBand", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleSynthwave], "TrackingBand", reader.ReadPropValue()); });
	MatchProperty("HackermanLook", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Look", reader.ReadPropValue()); });
	MatchProperty("HackermanPalette", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Palette", reader.ReadPropValue()); });
	MatchProperty("HackermanInkEdges", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "InkEdges", reader.ReadPropValue()); });
	MatchProperty("HackermanPaperBackgroundOnly", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "PaperBackgroundOnly", reader.ReadPropValue()); });
	MatchProperty("HackermanGlitchOnBlasts", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "GlitchOnBlasts", reader.ReadPropValue()); });
	MatchProperty("HackermanPaletteStrength", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "PaletteStrength", reader.ReadPropValue()); });
	MatchProperty("HackermanNeonGlow", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "NeonGlow", reader.ReadPropValue()); });
	MatchProperty("HackermanEdgeLines", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "EdgeLines", reader.ReadPropValue()); });
	MatchProperty("HackermanSky", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Sky", reader.ReadPropValue()); });
	MatchProperty("HackermanGridSpeed", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "GridSpeed", reader.ReadPropValue()); });
	MatchProperty("HackermanVHS", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "VHS", reader.ReadPropValue()); });
	MatchProperty("HackermanGlitch", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Glitch", reader.ReadPropValue()); });
	MatchProperty("HackermanCurvature", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Curvature", reader.ReadPropValue()); });
	MatchProperty("HackermanCRT", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "CRT", reader.ReadPropValue()); });
	MatchProperty("HackermanFlicker", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Flicker", reader.ReadPropValue()); });
	MatchProperty("HackermanPosterize", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Posterize", reader.ReadPropValue()); });
	MatchProperty("HackermanDither", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Dither", reader.ReadPropValue()); });
	MatchProperty("HackermanPixelate", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Pixelate", reader.ReadPropValue()); });
	MatchProperty("HackermanASCII", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "ASCII", reader.ReadPropValue()); });
	MatchProperty("HackermanASCIIBackground", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "ASCIIBackground", reader.ReadPropValue()); });
	MatchProperty("HackermanAfterglow", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Afterglow", reader.ReadPropValue()); });
	MatchProperty("HackermanTerminalOverlay", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "TerminalOverlay", reader.ReadPropValue()); });
	MatchProperty("HackermanCelShading", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "CelShading", reader.ReadPropValue()); });
	MatchProperty("HackermanCelBands", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "CelBands", reader.ReadPropValue()); });
	MatchProperty("HackermanHalftone", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Halftone", reader.ReadPropValue()); });
	MatchProperty("HackermanColorHalftone", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "ColorHalftone", reader.ReadPropValue()); });
	MatchProperty("HackermanPaperGrain", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "PaperGrain", reader.ReadPropValue()); });
	MatchProperty("HackermanMisregister", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "Misregister", reader.ReadPropValue()); });
	MatchProperty("HackermanComicFX", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "ComicFX", reader.ReadPropValue()); });
	MatchProperty("HackermanPanelFrame", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "PanelFrame", reader.ReadPropValue()); });
	MatchProperty("HackermanTrackingBand", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleHackerman], "TrackingBand", reader.ReadPropValue()); });
	MatchProperty("ComicLook", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Look", reader.ReadPropValue()); });
	MatchProperty("ComicPalette", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Palette", reader.ReadPropValue()); });
	MatchProperty("ComicInkEdges", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "InkEdges", reader.ReadPropValue()); });
	MatchProperty("ComicPaperBackgroundOnly", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "PaperBackgroundOnly", reader.ReadPropValue()); });
	MatchProperty("ComicGlitchOnBlasts", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "GlitchOnBlasts", reader.ReadPropValue()); });
	MatchProperty("ComicPaletteStrength", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "PaletteStrength", reader.ReadPropValue()); });
	MatchProperty("ComicNeonGlow", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "NeonGlow", reader.ReadPropValue()); });
	MatchProperty("ComicEdgeLines", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "EdgeLines", reader.ReadPropValue()); });
	MatchProperty("ComicSky", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Sky", reader.ReadPropValue()); });
	MatchProperty("ComicGridSpeed", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "GridSpeed", reader.ReadPropValue()); });
	MatchProperty("ComicVHS", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "VHS", reader.ReadPropValue()); });
	MatchProperty("ComicGlitch", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Glitch", reader.ReadPropValue()); });
	MatchProperty("ComicCurvature", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Curvature", reader.ReadPropValue()); });
	MatchProperty("ComicCRT", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "CRT", reader.ReadPropValue()); });
	MatchProperty("ComicFlicker", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Flicker", reader.ReadPropValue()); });
	MatchProperty("ComicPosterize", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Posterize", reader.ReadPropValue()); });
	MatchProperty("ComicDither", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Dither", reader.ReadPropValue()); });
	MatchProperty("ComicPixelate", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Pixelate", reader.ReadPropValue()); });
	MatchProperty("ComicASCII", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "ASCII", reader.ReadPropValue()); });
	MatchProperty("ComicASCIIBackground", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "ASCIIBackground", reader.ReadPropValue()); });
	MatchProperty("ComicAfterglow", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Afterglow", reader.ReadPropValue()); });
	MatchProperty("ComicTerminalOverlay", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "TerminalOverlay", reader.ReadPropValue()); });
	MatchProperty("ComicCelShading", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "CelShading", reader.ReadPropValue()); });
	MatchProperty("ComicCelBands", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "CelBands", reader.ReadPropValue()); });
	MatchProperty("ComicHalftone", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Halftone", reader.ReadPropValue()); });
	MatchProperty("ComicColorHalftone", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "ColorHalftone", reader.ReadPropValue()); });
	MatchProperty("ComicPaperGrain", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "PaperGrain", reader.ReadPropValue()); });
	MatchProperty("ComicMisregister", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "Misregister", reader.ReadPropValue()); });
	MatchProperty("ComicComicFX", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "ComicFX", reader.ReadPropValue()); });
	MatchProperty("ComicPanelFrame", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "PanelFrame", reader.ReadPropValue()); });
	MatchProperty("ComicTrackingBand", { ReadRetroField(g_PostProcessMan.GetLightingSettings().StyleRetro[LightingSettings::StyleComic], "TrackingBand", reader.ReadPropValue()); });
	MatchProperty("VariantLook", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Look", reader.ReadPropValue()); });
	MatchProperty("VariantPalette", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Palette", reader.ReadPropValue()); });
	MatchProperty("VariantInkEdges", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "InkEdges", reader.ReadPropValue()); });
	MatchProperty("VariantPaperBackgroundOnly", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "PaperBackgroundOnly", reader.ReadPropValue()); });
	MatchProperty("VariantGlitchOnBlasts", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "GlitchOnBlasts", reader.ReadPropValue()); });
	MatchProperty("VariantPaletteStrength", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "PaletteStrength", reader.ReadPropValue()); });
	MatchProperty("VariantNeonGlow", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "NeonGlow", reader.ReadPropValue()); });
	MatchProperty("VariantEdgeLines", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "EdgeLines", reader.ReadPropValue()); });
	MatchProperty("VariantSky", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Sky", reader.ReadPropValue()); });
	MatchProperty("VariantGridSpeed", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "GridSpeed", reader.ReadPropValue()); });
	MatchProperty("VariantVHS", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "VHS", reader.ReadPropValue()); });
	MatchProperty("VariantGlitch", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Glitch", reader.ReadPropValue()); });
	MatchProperty("VariantCurvature", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Curvature", reader.ReadPropValue()); });
	MatchProperty("VariantCRT", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "CRT", reader.ReadPropValue()); });
	MatchProperty("VariantFlicker", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Flicker", reader.ReadPropValue()); });
	MatchProperty("VariantPosterize", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Posterize", reader.ReadPropValue()); });
	MatchProperty("VariantDither", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Dither", reader.ReadPropValue()); });
	MatchProperty("VariantPixelate", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Pixelate", reader.ReadPropValue()); });
	MatchProperty("VariantASCII", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "ASCII", reader.ReadPropValue()); });
	MatchProperty("VariantASCIIBackground", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "ASCIIBackground", reader.ReadPropValue()); });
	MatchProperty("VariantAfterglow", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Afterglow", reader.ReadPropValue()); });
	MatchProperty("VariantTerminalOverlay", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "TerminalOverlay", reader.ReadPropValue()); });
	MatchProperty("VariantCelShading", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "CelShading", reader.ReadPropValue()); });
	MatchProperty("VariantCelBands", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "CelBands", reader.ReadPropValue()); });
	MatchProperty("VariantHalftone", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Halftone", reader.ReadPropValue()); });
	MatchProperty("VariantColorHalftone", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "ColorHalftone", reader.ReadPropValue()); });
	MatchProperty("VariantPaperGrain", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "PaperGrain", reader.ReadPropValue()); });
	MatchProperty("VariantMisregister", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "Misregister", reader.ReadPropValue()); });
	MatchProperty("VariantComicFX", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "ComicFX", reader.ReadPropValue()); });
	MatchProperty("VariantPanelFrame", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "PanelFrame", reader.ReadPropValue()); });
	MatchProperty("VariantTrackingBand", { ReadRetroField(g_PostProcessMan.GetLightingSettings().VariantRetro, "TrackingBand", reader.ReadPropValue()); });
	MatchProperty("ScreenVariant", {
		std::string name = reader.ReadPropValue();
		g_PostProcessMan.GetLightingSettings().ScreenVariant = name == "None" ? std::string() : name;
	});
	MatchProperty("UpscaleSharpness", { g_PostProcessMan.GetLightingSettings().UpscaleSharpness = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("IntegerScaling", { g_WindowMan.SetIntegerScaling(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("LightingSpecular", { g_PostProcessMan.GetLightingSettings().Specular = std::stof(reader.ReadPropValue()); });
	MatchProperty("UnitShineLights", { g_PostProcessMan.GetLightingSettings().UnitShineLights = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("UnitShineLamps", { g_PostProcessMan.GetLightingSettings().UnitShineLamps = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("UnitShineSun", { g_PostProcessMan.GetLightingSettings().UnitShineSun = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LightingMetals", { g_PostProcessMan.GetLightingSettings().Metals = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightingRelief", { g_PostProcessMan.GetLightingSettings().Relief = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightingEdgeLighting", { g_PostProcessMan.GetLightingSettings().EdgeLighting = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightingEmissiveIntensity", { g_PostProcessMan.GetLightingSettings().EmissiveIntensity = std::stof(reader.ReadPropValue()); });
	MatchProperty("DistortionEnabled", { g_PostProcessMan.GetLightingSettings().DistortionEnabled = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("HeatHaze", { g_PostProcessMan.GetLightingSettings().HeatHaze = std::stof(reader.ReadPropValue()); });
	MatchProperty("HazeFromHeat", { g_PostProcessMan.GetLightingSettings().HazeFromHeat = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("FireStyle", { g_PostProcessMan.GetLightingSettings().FireStyle = std::clamp(std::stoi(reader.ReadPropValue()), 0, 2); });
	// Before FireStyle: the shader's flames on (now both, which is what was asked for) or off (pixel fire only).
	MatchProperty("FireShader", { g_PostProcessMan.GetLightingSettings().FireStyle = std::stoi(reader.ReadPropValue()) != 0 ? LightingSettings::FireBoth : LightingSettings::FirePixel; });
	MatchProperty("FireFlameSize", { g_PostProcessMan.GetLightingSettings().FireFlameSize = std::stof(reader.ReadPropValue()); });
	MatchProperty("UnitOutline", { g_PostProcessMan.GetLightingSettings().UnitOutline = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("UnitOutlineOverEverything", { g_PostProcessMan.GetLightingSettings().UnitOutlineOverEverything = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("UnitOutlineWidth", { g_PostProcessMan.GetLightingSettings().UnitOutlineWidth = std::clamp(std::stof(reader.ReadPropValue()), 1.0F, 4.0F); });
	MatchProperty("UnitTeamColorParts", { g_PostProcessMan.GetLightingSettings().UnitTeamColorParts = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("UnitOutlineTeamColor", { g_PostProcessMan.GetLightingSettings().UnitOutlineTeamColor = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("UnitOutlineColor", { g_PostProcessMan.GetLightingSettings().UnitOutlineColor = glm::clamp(ReadVec3(reader.ReadPropValue(), g_PostProcessMan.GetLightingSettings().UnitOutlineColor), glm::vec3(0.0F), glm::vec3(1.0F)); });
	MatchProperty("UnitOutlineOpacity", { g_PostProcessMan.GetLightingSettings().UnitOutlineOpacity = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("UnitOutlineGlow", { g_PostProcessMan.GetLightingSettings().UnitOutlineGlow = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 2.0F); });
	MatchProperty("PaletteAnimation", { g_PostProcessMan.GetLightingSettings().PaletteAnimation = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("PaletteAnimationStrength", { g_PostProcessMan.GetLightingSettings().PaletteAnimationStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("FireFlameBrightness", { g_PostProcessMan.GetLightingSettings().FireFlameBrightness = std::stof(reader.ReadPropValue()); });
	MatchProperty("ShockwaveStrength", { g_PostProcessMan.GetLightingSettings().ShockwaveStrength = std::stof(reader.ReadPropValue()); });
	MatchProperty("SmokeScattering", { g_PostProcessMan.GetLightingSettings().SmokeScattering = std::stof(reader.ReadPropValue()); });
	MatchProperty("SmokeShading", { g_PostProcessMan.GetLightingSettings().SmokeShading = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("SmokeShadingStrength", { g_PostProcessMan.GetLightingSettings().SmokeShadingStrength = std::stof(reader.ReadPropValue()); });
	MatchProperty("EffectsParticles", {
		// The one setting sparks, dust and debris shared before they were split: carried over to all three.
		float amount = std::stof(reader.ReadPropValue());
		LightingSettings& lighting = g_PostProcessMan.GetLightingSettings();
		lighting.EffectsSparks = amount;
		lighting.EffectsDust = amount;
		lighting.EffectsDebris = amount;
	});
	MatchProperty("EffectsSparks", { g_PostProcessMan.GetLightingSettings().EffectsSparks = std::stof(reader.ReadPropValue()); });
	MatchProperty("EffectsDust", { g_PostProcessMan.GetLightingSettings().EffectsDust = std::stof(reader.ReadPropValue()); });
	MatchProperty("EffectsDebris", { g_PostProcessMan.GetLightingSettings().EffectsDebris = std::stof(reader.ReadPropValue()); });
	MatchProperty("SparkLights", { g_PostProcessMan.GetLightingSettings().SparkLights = std::max(std::stof(reader.ReadPropValue()), 0.0F); });
	MatchProperty("Embers", { g_PostProcessMan.GetLightingSettings().Embers = std::stof(reader.ReadPropValue()); });
	MatchProperty("Headlamps", { g_PostProcessMan.GetLightingSettings().Headlamps = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("NightAffectsAI", { g_PostProcessMan.GetLightingSettings().NightAffectsAI = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LivingWorld", { g_PostProcessMan.GetLightingSettings().LivingWorld = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("Stains", { g_PostProcessMan.GetLightingSettings().Stains = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WetnessMap", { g_PostProcessMan.GetLightingSettings().WetnessMap = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WetDrySeconds", { g_PostProcessMan.GetLightingSettings().WetDrySeconds = std::clamp(std::stof(reader.ReadPropValue()), 10.0F, 600.0F); });
	MatchProperty("Puddles", { g_PostProcessMan.GetLightingSettings().Puddles = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("ScorchMarks", { g_PostProcessMan.GetLightingSettings().ScorchMarks = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("StainSurface", { g_PostProcessMan.GetLightingSettings().StainSurface = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("StainShine", { g_PostProcessMan.GetLightingSettings().StainShine = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("DecalsFade", { g_PostProcessMan.GetLightingSettings().DecalsFade = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("DecalFadeMinutes", { g_PostProcessMan.GetLightingSettings().DecalFadeMinutes = std::clamp(std::stof(reader.ReadPropValue()), 0.5F, 60.0F); });
	MatchProperty("BloomEnabled", { g_PostProcessMan.GetLightingSettings().BloomEnabled = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("BloomThreshold", { g_PostProcessMan.GetLightingSettings().BloomThreshold = std::stof(reader.ReadPropValue()); });
	MatchProperty("BloomKnee", { g_PostProcessMan.GetLightingSettings().BloomKnee = std::clamp(std::stof(reader.ReadPropValue()), 0.01F, 1.0F); });
	MatchProperty("ShoulderStart", { g_PostProcessMan.GetLightingSettings().ShoulderStart = std::clamp(std::stof(reader.ReadPropValue()), 0.3F, 1.0F); });
	MatchProperty("HotSpotSeconds", { g_PostProcessMan.GetLightingSettings().HotSpotSeconds = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 10.0F); });
	MatchProperty("BloomIntensity", { g_PostProcessMan.GetLightingSettings().BloomIntensity = std::stof(reader.ReadPropValue()); });
	MatchProperty("AutoExposureLow", { g_PostProcessMan.GetLightingSettings().AutoExposureLow = std::stof(reader.ReadPropValue()); });
	MatchProperty("AutoExposureHigh", { g_PostProcessMan.GetLightingSettings().AutoExposureHigh = std::stof(reader.ReadPropValue()); });
	MatchProperty("AutoExposure", { g_PostProcessMan.GetLightingSettings().AutoExposure = std::stof(reader.ReadPropValue()); });
	MatchProperty("AutoExposureGameTime", { g_PostProcessMan.GetLightingSettings().AutoExposureGameTime = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("PostExposure", { g_PostProcessMan.GetLightingSettings().Exposure = std::stof(reader.ReadPropValue()); });
	MatchProperty("GradeTemperature", { g_PostProcessMan.GetLightingSettings().Temperature = std::stof(reader.ReadPropValue()); });
	MatchProperty("GradeTint", { g_PostProcessMan.GetLightingSettings().Tint = std::stof(reader.ReadPropValue()); });
	MatchProperty("GradeContrast", { g_PostProcessMan.GetLightingSettings().Contrast = std::stof(reader.ReadPropValue()); });
	MatchProperty("GradeShadowTint", { g_PostProcessMan.GetLightingSettings().ShadowTint = ReadVec3(reader.ReadPropValue(), g_PostProcessMan.GetLightingSettings().ShadowTint); });
	MatchProperty("GradeHighlightTint", { g_PostProcessMan.GetLightingSettings().HighlightTint = ReadVec3(reader.ReadPropValue(), g_PostProcessMan.GetLightingSettings().HighlightTint); });
	MatchProperty("FilmGrain", { g_PostProcessMan.GetLightingSettings().FilmGrain = std::stof(reader.ReadPropValue()); });
	MatchProperty("EventLooks", { g_PostProcessMan.GetLightingSettings().EventLooks = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("EventLookStrength", { g_PostProcessMan.GetLightingSettings().EventLookStrength = std::stof(reader.ReadPropValue()); });
	MatchProperty("EventBlastFlash", { g_PostProcessMan.GetLightingSettings().EventBlastFlash = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("EventHurtLook", { g_PostProcessMan.GetLightingSettings().EventHurtLook = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("EventFireWarmth", { g_PostProcessMan.GetLightingSettings().EventFireWarmth = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("SunDisc", { g_PostProcessMan.GetLightingSettings().SunDisc = std::stof(reader.ReadPropValue()); });
	MatchProperty("CloudShadows", { g_PostProcessMan.GetLightingSettings().CloudShadows = std::stof(reader.ReadPropValue()); });
	MatchProperty("CloudLayer", { g_PostProcessMan.GetLightingSettings().CloudLayer = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("CloudCover", { g_PostProcessMan.GetLightingSettings().CloudCover = std::stof(reader.ReadPropValue()); });
	MatchProperty("CloudOpacity", { g_PostProcessMan.GetLightingSettings().CloudOpacity = std::stof(reader.ReadPropValue()); });
	MatchProperty("CloudSize", { g_PostProcessMan.GetLightingSettings().CloudSize = std::stof(reader.ReadPropValue()); });
	MatchProperty("CloudHeight", { g_PostProcessMan.GetLightingSettings().CloudHeight = std::stof(reader.ReadPropValue()); });
	MatchProperty("SurfaceStates", { g_PostProcessMan.GetLightingSettings().SurfaceStates = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("TracerLights", { g_PostProcessMan.GetLightingSettings().TracerLights = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WaterFoam", { g_PostProcessMan.GetLightingSettings().WaterFoam = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterFoamStray", { g_PostProcessMan.GetLightingSettings().WaterFoamStray = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterFoamBrightness", { g_PostProcessMan.GetLightingSettings().WaterFoamBrightness = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterFoamGlow", { g_PostProcessMan.GetLightingSettings().WaterFoamGlow = std::stof(reader.ReadPropValue()); });
	MatchProperty("SoftSmoke", { g_PostProcessMan.GetLightingSettings().SoftSmoke = std::stof(reader.ReadPropValue()); });
	MatchProperty("EffectLayers", { g_PostProcessMan.GetLightingSettings().EffectLayers = std::clamp(std::stoi(reader.ReadPropValue()), 0, 2); });
	MatchProperty("LayerSmoke", { g_PostProcessMan.GetLightingSettings().EffectBehind[LightingSettings::LayerSmoke] = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LayerSoftSmoke", { g_PostProcessMan.GetLightingSettings().EffectBehind[LightingSettings::LayerSoftSmoke] = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LayerMist", { g_PostProcessMan.GetLightingSettings().EffectBehind[LightingSettings::LayerMist] = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LayerSplash", { g_PostProcessMan.GetLightingSettings().EffectBehind[LightingSettings::LayerSplash] = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LayerFroth", { g_PostProcessMan.GetLightingSettings().EffectBehind[LightingSettings::LayerFroth] = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LayerDust", { g_PostProcessMan.GetLightingSettings().EffectBehind[LightingSettings::LayerDust] = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LayerDebris", { g_PostProcessMan.GetLightingSettings().EffectBehind[LightingSettings::LayerDebris] = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LayerSparks", { g_PostProcessMan.GetLightingSettings().EffectBehind[LightingSettings::LayerSparks] = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LayerEmbers", { g_PostProcessMan.GetLightingSettings().EffectBehind[LightingSettings::LayerEmbers] = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LayerFire", { g_PostProcessMan.GetLightingSettings().EffectBehind[LightingSettings::LayerFire] = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WaterLightGlow", { g_PostProcessMan.GetLightingSettings().WaterLightGlow = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterReflections", { g_PostProcessMan.GetLightingSettings().WaterReflections = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WaterReflectionStrength", { g_PostProcessMan.GetLightingSettings().WaterReflectionStrength = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterRefraction", { g_PostProcessMan.GetLightingSettings().WaterRefraction = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterRipples", { g_PostProcessMan.GetLightingSettings().WaterRipples = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterSoftReflection", { g_PostProcessMan.GetLightingSettings().WaterSoftReflection = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WaterMirrorSurface", { g_PostProcessMan.GetLightingSettings().WaterMirrorSurface = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WaterFoamBubbles", { g_PostProcessMan.GetLightingSettings().WaterFoamBubbles = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterCaustics", { g_PostProcessMan.GetLightingSettings().WaterCaustics = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WaterFlowSurface", { g_PostProcessMan.GetLightingSettings().WaterFlowSurface = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WaterFlowStrength", { g_PostProcessMan.GetLightingSettings().WaterFlowStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("DistinctLiquidLooks", { g_PostProcessMan.GetLightingSettings().DistinctLiquidLooks = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WaterMistSize", { g_PostProcessMan.GetLightingSettings().WaterMistSize = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterMistLife", { g_PostProcessMan.GetLightingSettings().WaterMistLife = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterMistOpacity", { g_PostProcessMan.GetLightingSettings().WaterMistOpacity = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterMistSpread", { g_PostProcessMan.GetLightingSettings().WaterMistSpread = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterMist", { g_PostProcessMan.GetLightingSettings().WaterMist = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterThinFlow", { g_PostProcessMan.GetLightingSettings().WaterThinFlow = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashFroth", { g_PostProcessMan.GetLightingSettings().SplashFroth = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashFrothDensity", { g_PostProcessMan.GetLightingSettings().SplashFrothDensity = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashFrothSpecks", { g_PostProcessMan.GetLightingSettings().SplashFrothSpecks = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashFrothSize", { g_PostProcessMan.GetLightingSettings().SplashFrothSize = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashFrothLife", { g_PostProcessMan.GetLightingSettings().SplashFrothLife = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashFrothOpacity", { g_PostProcessMan.GetLightingSettings().SplashFrothOpacity = std::stof(reader.ReadPropValue()); });
	MatchProperty("PuffVariety", { g_PostProcessMan.GetLightingSettings().PuffVariety = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WaterSplash", { g_PostProcessMan.GetLightingSettings().WaterSplash = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashDrops", { g_PostProcessMan.GetLightingSettings().SplashDrops = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashHeight", { g_PostProcessMan.GetLightingSettings().SplashHeight = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashWidth", { g_PostProcessMan.GetLightingSettings().SplashWidth = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashDropSize", { g_PostProcessMan.GetLightingSettings().SplashDropSize = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashSpray", { g_PostProcessMan.GetLightingSettings().SplashSpray = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashUnder", { g_PostProcessMan.GetLightingSettings().SplashUnder = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashUnderLiquidColor", { g_PostProcessMan.GetLightingSettings().SplashUnderLiquidColor = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashUnderHeight", { g_PostProcessMan.GetLightingSettings().SplashUnderHeight = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashUnderWidth", { g_PostProcessMan.GetLightingSettings().SplashUnderWidth = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashUnderDropSize", { g_PostProcessMan.GetLightingSettings().SplashUnderDropSize = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashUnderOpacity", { g_PostProcessMan.GetLightingSettings().SplashUnderOpacity = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashUnderScatter", { g_PostProcessMan.GetLightingSettings().SplashUnderScatter = std::stof(reader.ReadPropValue()); });
	MatchProperty("SplashUnderColor", { g_PostProcessMan.GetLightingSettings().SplashUnderColor = ReadVec3(reader.ReadPropValue(), g_PostProcessMan.GetLightingSettings().SplashUnderColor); });
	MatchProperty("WaterMistBrightness", { g_PostProcessMan.GetLightingSettings().WaterMistBrightness = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterMistGlow", { g_PostProcessMan.GetLightingSettings().WaterMistGlow = std::stof(reader.ReadPropValue()); });
	MatchProperty("RainSplashes", { g_PostProcessMan.GetLightingSettings().RainSplashes = std::stof(reader.ReadPropValue()); });
	MatchProperty("WeatherLight", { g_PostProcessMan.GetLightingSettings().WeatherLight = std::stof(reader.ReadPropValue()); });
	MatchProperty("TracerGlow", { g_PostProcessMan.GetLightingSettings().TracerGlow = std::stof(reader.ReadPropValue()); });
	MatchProperty("TracerLightBrightness", { g_PostProcessMan.GetLightingSettings().TracerLightBrightness = std::stof(reader.ReadPropValue()); });
	MatchProperty("TracerLightRandomness", { g_PostProcessMan.GetLightingSettings().TracerLightRandomness = std::stof(reader.ReadPropValue()); });
	MatchProperty("SaberLightBrightness", { g_PostProcessMan.GetLightingSettings().SaberLightBrightness = std::max(std::stof(reader.ReadPropValue()), 0.0F); });
	MatchProperty("SaberLightReach", { g_PostProcessMan.GetLightingSettings().SaberLightReach = std::max(std::stof(reader.ReadPropValue()), 0.0F); });
	MatchProperty("JetpackLightBrightness", { g_PostProcessMan.GetLightingSettings().JetpackLightBrightness = std::max(std::stof(reader.ReadPropValue()), 0.0F); });
	MatchProperty("JetpackLightReach", { g_PostProcessMan.GetLightingSettings().JetpackLightReach = std::max(std::stof(reader.ReadPropValue()), 0.0F); });
	MatchProperty("SaberAirGlow", { g_PostProcessMan.GetLightingSettings().SaberAirGlow = std::max(std::stof(reader.ReadPropValue()), 0.0F); });
	MatchProperty("TracerLightReach", { g_PostProcessMan.GetLightingSettings().TracerLightReach = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightSaturation", { g_PostProcessMan.GetLightingSettings().LightSaturation = std::stof(reader.ReadPropValue()); });
	MatchProperty("LightTint", { g_PostProcessMan.GetLightingSettings().LightTint = ReadVec3(reader.ReadPropValue(), g_PostProcessMan.GetLightingSettings().LightTint); });
	MatchProperty("LampBrightness", { g_PostProcessMan.GetLightingSettings().LampBrightness = std::stof(reader.ReadPropValue()); });
	MatchProperty("LampReach", { g_PostProcessMan.GetLightingSettings().LampReach = std::stof(reader.ReadPropValue()); });
	MatchProperty("LampTint", { g_PostProcessMan.GetLightingSettings().LampTint = ReadVec3(reader.ReadPropValue(), g_PostProcessMan.GetLightingSettings().LampTint); });
	MatchProperty("LampCache", { g_PostProcessMan.GetLightingSettings().LampCache = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LampCacheDetail", { g_PostProcessMan.GetLightingSettings().LampCacheDetail = std::clamp(std::stoi(reader.ReadPropValue()), 0, 2); });
	MatchProperty("HeadlampBrightness", { g_PostProcessMan.GetLightingSettings().HeadlampBrightness = std::stof(reader.ReadPropValue()); });
	MatchProperty("HeadlampReach", { g_PostProcessMan.GetLightingSettings().HeadlampReach = std::stof(reader.ReadPropValue()); });
	MatchProperty("HeadlampWidth", { g_PostProcessMan.GetLightingSettings().HeadlampWidth = std::stof(reader.ReadPropValue()); });
	MatchProperty("HeadlampColor", { g_PostProcessMan.GetLightingSettings().HeadlampColor = ReadVec3(reader.ReadPropValue(), g_PostProcessMan.GetLightingSettings().HeadlampColor); });
	MatchProperty("HeadlampGlow", { g_PostProcessMan.GetLightingSettings().HeadlampGlow = std::stof(reader.ReadPropValue()); });
	MatchProperty("HeadlampTeamTint", { g_PostProcessMan.GetLightingSettings().HeadlampTeamTint = std::stof(reader.ReadPropValue()); });
	MatchProperty("ShowAIPaths", { Actor::SetShowAIPaths(std::clamp(std::stoi(reader.ReadPropValue()), 0, 2)); });
	MatchProperty("AimDotsLight", { g_PostProcessMan.GetLightingSettings().AimDotsLight = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("HeadlampsByDay", { g_PostProcessMan.GetLightingSettings().HeadlampsByDay = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("HeadlampDarkThreshold", { g_PostProcessMan.GetLightingSettings().HeadlampDarkThreshold = std::stof(reader.ReadPropValue()); });
	MatchProperty("HeadlampsOnlyInDark", { g_PostProcessMan.GetLightingSettings().HeadlampsOnlyInDark = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("LightPropagationSteps", { g_PostProcessMan.GetLightingSettings().PropagationIterationsPerFrame = std::clamp(std::stoi(reader.ReadPropValue()), 1, 32); });
	MatchProperty("PanelsOverlay", { g_DebugMan.m_PanelsOverlay = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("DockPanels", { g_DebugMan.m_DockPanels = std::stoi(reader.ReadPropValue()) != 0; });
	// (Not from the preset loaded at start: Settings.ini has them as they were last left, a preset as they were when it was saved.)
	MatchProperty("SandboxCharacter", { std::string setup = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { Sandbox::SetCharacterSetup(setup); } });
	MatchProperty("SandboxVehicles", { std::string setup = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { Sandbox::SetVehicleSetup(setup); } });
	MatchProperty("SandboxUnits", { std::string setup = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { Sandbox::SetUnitSetup(setup); } });
	MatchProperty("SandboxPanel", { std::string setup = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { Sandbox::SetPanelSetup(setup); } });
	MatchProperty("SandboxPins", { /* Pins used to live here; they belong to the saved game now. */ reader.ReadPropValue(); });
	MatchProperty("SandboxFavourites", { /* Favourites used to live here; they have their own file now. */ reader.ReadPropValue(); });
	MatchProperty("PixelToolFont", { g_DebugMan.m_PixelFont = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("ToolScale", { g_DebugMan.m_ToolScale = std::clamp(std::stof(reader.ReadPropValue()), 0.4F, 1.5F); });
	MatchProperty("PanelWidth", { /* Was in pixels (380 by default); about 18 of them to a percent of a 1080p window. */ g_DebugMan.m_PanelWidth = std::clamp(std::stof(reader.ReadPropValue()) / 18.0F, 10.0F, 40.0F); });
	MatchProperty("PanelWidthPercent", { g_DebugMan.m_PanelWidth = std::clamp(std::stof(reader.ReadPropValue()), 10.0F, 45.0F); });
	MatchProperty("RightPanelWidthPercent", { g_DebugMan.m_PanelWidthRight = std::clamp(std::stof(reader.ReadPropValue()), 10.0F, 45.0F); });
	MatchProperty("SandboxPanelPlacement", { g_DebugMan.m_SandboxPlacement = std::clamp(std::stoi(reader.ReadPropValue()), 0, 2); });
	MatchProperty("SandboxBarWidthPercent", { reader.ReadPropValue(); }); // (The bar is the picture's width now; kept so older settings files still read.)
	MatchProperty("BackgroundBlur", { g_PostProcessMan.GetLightingSettings().BackgroundBlur = std::stof(reader.ReadPropValue()); });
	MatchProperty("DepthOfField", { g_PostProcessMan.GetLightingSettings().DepthOfField = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("DepthOfFieldFocus", { g_PostProcessMan.GetLightingSettings().DepthOfFieldFocus = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("DepthOfFieldStrength", { g_PostProcessMan.GetLightingSettings().DepthOfFieldStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 2.0F); });
	MatchProperty("TiltShift", { g_PostProcessMan.GetLightingSettings().TiltShift = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("TiltShiftLine", { g_PostProcessMan.GetLightingSettings().TiltShiftLine = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("TiltShiftStrength", { g_PostProcessMan.GetLightingSettings().TiltShiftStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 2.0F); });
	MatchProperty("FocusEffectsInPhotoModeOnly", { g_PostProcessMan.GetLightingSettings().FocusEffectsInPhotoModeOnly = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("ChromaticAberration", { g_PostProcessMan.GetLightingSettings().ChromaticAberration = std::stof(reader.ReadPropValue()); });
	MatchProperty("ModShaders", { g_PostProcessMan.GetLightingSettings().ModShaders = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("ModShaderStrength", { g_PostProcessMan.GetLightingSettings().ModShaderStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("SpriteMaps", { g_PostProcessMan.GetLightingSettings().SpriteMaps = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("SpriteMapStrength", { g_PostProcessMan.GetLightingSettings().SpriteMapStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("PostVignette", { g_PostProcessMan.GetLightingSettings().Vignette = std::stof(reader.ReadPropValue()); });
	MatchProperty("PostSaturation", { g_PostProcessMan.GetLightingSettings().Saturation = std::stof(reader.ReadPropValue()); });
	MatchProperty("MasterVolume", { g_AudioMan.SetMasterVolume(std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("MuteMaster", { reader >> g_AudioMan.m_MuteMaster; });
	MatchProperty("MusicVolume", { g_AudioMan.SetMusicVolume(std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("MuteMusic", { reader >> g_AudioMan.m_MuteMusic; });
	MatchProperty("SoundVolume", { g_AudioMan.SetSoundsVolume(std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("MuteSounds", { reader >> g_AudioMan.m_MuteSounds; });
	MatchProperty("MuteAudioOnFocusLoss", { reader >> g_AudioMan.m_MuteAudioOnFocusLoss; });
	MatchProperty("SoundMixWeaponsVolume", { g_AudioMan.SetSoundMixVolume(AudioMan::MIX_WEAPONS, std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("SoundMixExplosionsVolume", { g_AudioMan.SetSoundMixVolume(AudioMan::MIX_EXPLOSIONS, std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("SoundMixImpactsVolume", { g_AudioMan.SetSoundMixVolume(AudioMan::MIX_IMPACTS, std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("SoundMixFootstepsVolume", { g_AudioMan.SetSoundMixVolume(AudioMan::MIX_FOOTSTEPS, std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("SoundMixVoicesVolume", { g_AudioMan.SetSoundMixVolume(AudioMan::MIX_VOICES, std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("SoundMixEnginesVolume", { g_AudioMan.SetSoundMixVolume(AudioMan::MIX_ENGINES, std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("SoundMixWorldVolume", { g_AudioMan.SetSoundMixVolume(AudioMan::MIX_WORLD, std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("SoundMixUIVolume", { g_AudioMan.SetSoundMixVolume(AudioMan::MIX_UI, std::stof(reader.ReadPropValue()) / 100.0F); });
	MatchProperty("SoundPanningEffectStrength", {
		reader >> g_AudioMan.m_SoundPanningEffectStrength;

		//////////////////////////////////////////////////
		// TODO These need to be removed when our soundscape is sorted out. They're only here temporarily to allow for easier tweaking by pawnis.
	});
	MatchProperty("ListenerZOffset", { reader >> g_AudioMan.m_ListenerZOffset; });
	MatchProperty("MinimumDistanceForPanning", {
		reader >> g_AudioMan.m_MinimumDistanceForPanning;
		//////////////////////////////////////////////////
	});
	MatchProperty("ShowForeignItems", { reader >> m_ShowForeignItems; });
	MatchProperty("FlashOnBrainDamage", { reader >> m_FlashOnBrainDamage; });
	MatchProperty("BlipOnRevealUnseen", { reader >> m_BlipOnRevealUnseen; });
	MatchProperty("MaxUnheldItems", { reader >> g_MovableMan.m_MaxDroppedItems; });
	MatchProperty("UnheldItemsHUDDisplayRange", { SetUnheldItemsHUDDisplayRange(std::stof(reader.ReadPropValue())); });
	MatchProperty("AlwaysDisplayUnheldItemsInStrategicMode", { reader >> m_AlwaysDisplayUnheldItemsInStrategicMode; });
	MatchProperty("SubPieMenuHoverOpenDelay", { reader >> m_SubPieMenuHoverOpenDelay; });
	MatchProperty("ClassicPieWheel", { reader >> m_ClassicPieWheel; });
	MatchProperty("UnitMenuClickToOpen", { reader >> m_UnitMenuClickToOpen; });
	MatchProperty("UnitMenuTimeScale", {
		float scale;
		reader >> scale;
		SetUnitMenuTimeScale(scale);
	});
	MatchProperty("PauseAIKey", { m_PauseAIKey = reader.ReadPropValue(); });
	MatchProperty("DefaultBackground", { m_DefaultBackground = reader.ReadPropValue(); });
	MatchProperty("EndlessMetaGameMode", { reader >> m_EndlessMetaGameMode; });
	MatchProperty("EndlessMode", { reader >> m_EndlessMetaGameMode; }); // Legacy name, kept for old Settings.ini files.
	MatchProperty("EnableCrabBombs", { reader >> m_EnableCrabBombs; });
	MatchProperty("EnableMantling", { reader >> m_EnableMantling; });
	MatchProperty("CreaturesClimbWalls", { reader >> m_CreaturesClimbWalls; });
	MatchProperty("CreatureClimbSpeed", { float speed = 1.0F; reader >> speed; SetCreatureClimbSpeed(speed); });
	MatchProperty("CreaturesFly", { reader >> m_CreaturesFly; });
	MatchProperty("CreatureFlySpeed", { float speed = 1.0F; reader >> speed; SetCreatureFlySpeed(speed); });
	MatchProperty("EnableStuckPush", { reader >> m_EnableStuckPush; });
	MatchProperty("BodyGearSettlesAsScraps", { reader >> m_BodyGearSettlesAsScraps; });
	MatchProperty("GibChunkPercent", { reader >> m_GibChunks.CountPercent; ClampGibChunks(); });
	MatchProperty("MaxLooseGibChunks", { reader >> m_GibChunks.MaxLoose; ClampGibChunks(); });
	MatchProperty("GibChunkSettleMS", { reader >> m_GibChunks.SettleMS; ClampGibChunks(); });
	MatchProperty("GibChunkBounces", { reader >> m_GibChunks.MaxBounces; ClampGibChunks(); });
	MatchProperty("GibChunkAtomSpacing", { reader >> m_GibChunks.AtomSpacing; ClampGibChunks(); });
	MatchProperty("GibChunksSettleOnBodies", { reader >> m_GibChunks.SettleOnBodies; });
	MatchProperty("AICombatAccuracy", { reader >> m_AICombat.Accuracy; ClampAICombat(); });
	MatchProperty("AICombatReaction", { reader >> m_AICombat.Reaction; ClampAICombat(); });
	MatchProperty("AICombatSpotting", { reader >> m_AICombat.Spotting; ClampAICombat(); });
	MatchProperty("AICombatSightRange", { reader >> m_AICombat.SightRange; ClampAICombat(); });
	MatchProperty("AICombatTargetMemory", { reader >> m_AICombat.TargetMemory; ClampAICombat(); });
	MatchProperty("AICombatGrenadeUse", { reader >> m_AICombat.GrenadeUse; ClampAICombat(); });
	MatchProperty("AICombatBurst", { reader >> m_AICombat.Burst; ClampAICombat(); });
	MatchProperty("FixLiquidTellsOnlyMoves", { reader >> m_SlowdownFixes.LiquidTellsOnlyMoves; });
	MatchProperty("WreckSettleSeconds", { reader >> m_SlowdownFixes.WreckSettleSeconds; m_SlowdownFixes.WreckSettleSeconds = std::clamp(m_SlowdownFixes.WreckSettleSeconds, 0, 600); });
	MatchProperty("FixDebrisRestsAcrossGaps", { reader >> m_SlowdownFixes.DebrisRestsAcrossGaps; });
	MatchProperty("FixGasAreaShrinks", { reader >> m_SlowdownFixes.GasAreaShrinks; });
	MatchProperty("FixLightingSplitsChanges", { reader >> m_SlowdownFixes.LightingSplitsChanges; });
	MatchProperty("MaxLooseBits", { reader >> m_SlowdownFixes.MaxLooseBits; m_SlowdownFixes.MaxLooseBits = std::clamp(m_SlowdownFixes.MaxLooseBits, 0, 50000); });
	MatchProperty("MaxLooseBitsOfKinds", {
		for (int& most: m_SlowdownFixes.MaxLooseBitsOf) {
			reader >> most;
			most = std::clamp(most, 0, 50000);
		}
	});
	MatchProperty("NoSceneWrap", { reader >> m_NoSceneWrap; });
	MatchProperty("UnitSpeech", { bool on = true; reader >> on; UnitSpeech::SetEnabled(on); });
	MatchProperty("UnitSpeechChance", { int percent = 40; reader >> percent; UnitSpeech::SetChance(percent); });
	MatchProperty("UnitSpeechEnemies", { bool on = true; reader >> on; UnitSpeech::SetShowsEnemies(on); });
	MatchProperty("UnitSpeechBubbles", { bool on = true; reader >> on; UnitSpeech::SetShowsBubbles(on); });
	MatchProperty("UnitSpeechVoices", { bool on = true; reader >> on; UnitSpeechVoice::SetEnabled(on); });
	MatchProperty("UnitSpeechVoiceVolume", { int percent = 70; reader >> percent; UnitSpeechVoice::SetVolume(percent); });
	MatchProperty("UnitSpeechVoiceRadio", { int percent = 75; reader >> percent; UnitSpeechVoice::SetRadio(percent); });
	MatchProperty("UnitSpeechVoiceEnemies", { bool on = true; reader >> on; UnitSpeechVoice::SetHearsEnemies(on); });
	MatchProperty("UnitSpeechVoiceWords", { bool on = true; reader >> on; UnitSpeechVoice::SetSpeaksWords(on); });
	MatchProperty("UnitSpeechOff", { UnitSpeech::SetTriggerOn(reader.ReadPropValue(), false); });
	MatchProperty("UnitSpeechOn", { UnitSpeech::SetTriggerOn(reader.ReadPropValue(), true); });
	MatchProperty("UnitSpeechTones1", { UnitSpeech::SetTeamTonesText(0, reader.ReadPropValue()); });
	MatchProperty("UnitSpeechTones2", { UnitSpeech::SetTeamTonesText(1, reader.ReadPropValue()); });
	MatchProperty("UnitSpeechTones3", { UnitSpeech::SetTeamTonesText(2, reader.ReadPropValue()); });
	MatchProperty("UnitSpeechTones4", { UnitSpeech::SetTeamTonesText(3, reader.ReadPropValue()); });
	MatchProperty("UnitSpeechToneMix", { UnitSpeech::SetToneWeightsText(reader.ReadPropValue()); });
	MatchProperty("UnitToughness", { float toughness = 1.0F; reader >> toughness; SetToughness(ToughnessKind::Units, toughness); });
	MatchProperty("VehicleToughness", { float toughness = 1.0F; reader >> toughness; SetToughness(ToughnessKind::Vehicles, toughness); });
	MatchProperty("CraftToughness", { float toughness = 1.0F; reader >> toughness; SetToughness(ToughnessKind::Craft, toughness); });
	MatchProperty("AnimalToughness", { float toughness = 1.0F; reader >> toughness; SetToughness(ToughnessKind::Animals, toughness); });
	MatchProperty("CreatureToughness", { float toughness = 1.0F; reader >> toughness; SetToughness(ToughnessKind::Creatures, toughness); });
	MatchProperty("AISuppression", {
		reader >> m_AISuppression;
		m_AISuppression = std::clamp(m_AISuppression, 0.0F, 2.0F);
	});
	MatchProperty("AIDigWillingness", { float scale = 1.0F; reader >> scale; SetAIDigWillingness(scale); });
	MatchProperty("AIDigsWithTracers", { reader >> m_AIDigsWithTracers; });
	MatchProperty("AIThreatAvoidance", { float scale = 1.0F; reader >> scale; SetAIThreatAvoidance(scale); });
	MatchProperty("AIRecklessness", { float recklessness = 0.5F; reader >> recklessness; SetAIRecklessness(recklessness); });
	MatchProperty("StrictClimbLimits", { reader >> m_StrictClimbLimits; });
	MatchProperty("MeasuredBodySize", { reader >> m_MeasuredBodySize; });
	MatchProperty("AILadderGiveUpSeconds", { float seconds = 1.5F; reader >> seconds; SetAILadderGiveUpSeconds(seconds); });
	MatchProperty("CarriedWeightMultiplier", { float multiplier = 1.0F; reader >> multiplier; SetCarriedWeightMultiplier(multiplier); });
	MatchProperty("LegStrength", { float strength = 1.0F; reader >> strength; SetLegStrength(strength); });
	MatchProperty("JetpackWeightCompensation", { int mode = 0; reader >> mode; SetJetpackWeightCompensation(mode); });
	MatchProperty("UnitWaterBehaviour", { int mode = 2; reader >> mode; SetUnitWaterBehaviour(mode); });
	MatchProperty("UnitsStepOverObjects", { reader >> m_UnitsStepOverObjects; });
	MatchProperty("UnitsCrossVehicles", { reader >> m_UnitsCrossVehicles; });
	MatchProperty("UnitsPushObjects", { reader >> m_UnitsPushObjects; });
	MatchProperty("ObjectPushStrength", { float strength = 1.0F; reader >> strength; SetObjectPushStrength(strength); });
	MatchProperty("SensesPassPlants", { reader >> m_SensesPassPlants; });
	MatchProperty("SensesPassRungs", { reader >> m_SensesPassRungs; });
	MatchProperty("SensesPassSpecks", { reader >> m_SensesPassSpecks; });
	MatchProperty("OverloadSlowdown", { float slowdown = 0.0F; reader >> slowdown; SetOverloadSlowdown(slowdown); });
	MatchProperty("JetpackFuelMultiplier", { float multiplier = 1.0F; reader >> multiplier; SetJetpackFuelMultiplier(multiplier); });
	MatchProperty("InfiniteJetpackFuel", { reader >> m_InfiniteJetpackFuel; });
	MatchProperty("AILadderTeamStalls", { int stalls = 2; reader >> stalls; SetAILadderTeamStalls(stalls); });
	MatchProperty("AIGrabsLaddersOnPurpose", { reader >> m_AIGrabsLaddersOnPurpose; });
	MatchProperty("AIGrabsLaddersMidJet", { reader >> m_AIGrabsLaddersMidJet; });
	MatchProperty("AIMantlesOnPurpose", { reader >> m_AIMantlesOnPurpose; });
	MatchProperty("AICatchesLedgesMidAir", { reader >> m_AICatchesLedgesMidAir; });
	MatchProperty("RiskyOrdersFlyPastLadders", { reader >> m_RiskyOrdersFlyPastLadders; });
	MatchProperty("AISpawnDiggerChance", { float percent = 0.0F; reader >> percent; SetAISpawnDiggerChance(percent); });
	MatchProperty("BleedOutChance", { float percent = 0.0F; reader >> percent; SetBleedOutChance(percent); });
	MatchProperty("WoundBleedTime", { float scale = 1.0F; reader >> scale; SetWoundBleedTime(scale); });
	MatchProperty("ShipGroundClearance", { float gap = 12.0F; reader >> gap; SetShipGroundClearance(gap); });
	MatchProperty("ShipGraceZone", { float zone = 400.0F; reader >> zone; SetShipGraceZone(zone); });
	MatchProperty("CameraOverscroll", { float overscroll = 400.0F; reader >> overscroll; SetCameraOverscroll(overscroll); });
	MatchProperty("WeaponTerrainDamage", { float scale = 1.0F; reader >> scale; SetWeaponTerrainDamage(scale); });
	MatchProperty("WeaponUnitDamage", { float scale = 1.0F; reader >> scale; SetWeaponUnitDamage(scale); });
	MatchProperty("WeaponUnitDamageGibs", { reader >> m_WeaponUnitDamageGibs; });
	MatchProperty("AISpawnDiggerType", { int type = 1; reader >> type; SetAISpawnDiggerType(type); });
	MatchProperty("AISteadyBeforeJet", { reader >> m_AISteadyBeforeJet; });
	MatchProperty("AIWaitForFuel", { reader >> m_AIWaitForFuel; });
	MatchProperty("AIWaitsAtDoors", { reader >> m_AIWaitsAtDoors; });
	MatchProperty("AIEnemyBuysReinforcements", { reader >> m_AIEnemyBuysReinforcements; });
	MatchProperty("AIEnemyUsesPowers", { reader >> m_AIEnemyUsesPowers; });
	MatchProperty("AISquadFollowsYou", { reader >> m_AISquadFollowsYou; });
	MatchProperty("AIUsesVehicles", { reader >> m_AIUsesVehicles; });
	MatchProperty("AIRidesShips", { reader >> m_AIRidesShips; });
	MatchProperty("AIShipRideNoRoute", { reader >> m_AIShipRideNoRoute; });
	MatchProperty("AIShipRideLongWalk", { reader >> m_AIShipRideLongWalk; });
	MatchProperty("AIShipRideLongWalkDistance", { float distance = 0.0F; reader >> distance; SetAIShipRideLongWalkDistance(distance); });
	MatchProperty("AIShipRideRange", { float range = 0.0F; reader >> range; SetAIShipRideRange(range); });
	MatchProperty("AIShipRideWait", { float seconds = 0.0F; reader >> seconds; SetAIShipRideWait(seconds); });
	MatchProperty("AIShipRideDropRing", { float range = 0.0F; reader >> range; SetAIShipRideDropRing(range); });
	MatchProperty("AIVehiclesWaitForRiders", { reader >> m_AIVehiclesWaitForRiders; });
	MatchProperty("BoardFromBelow", { reader >> m_BoardFromBelow; });
	MatchProperty("BoardFromBelowReach", { float reach = 0.0F; reader >> reach; SetBoardFromBelowReach(reach); });
	MatchProperty("AICoverDigs", { reader >> m_AICoverDigs; });
	MatchProperty("AICoverFallsBack", { reader >> m_AICoverFallsBack; });
	MatchProperty("AICoverLiesFlat", { reader >> m_AICoverLiesFlat; });
	MatchProperty("AIBreaksWhenShaken", { reader >> m_AIBreaksWhenShaken; });
	MatchProperty("AISharedSpotting", { reader >> m_AISharedSpotting; });
	MatchProperty("AISharedSpottingHeadStart", { float headStart = 1.0F; reader >> headStart; SetAISharedSpottingHeadStart(headStart); });
	MatchProperty("AIScavengesBetterWeapons", { reader >> m_AIScavengesBetterWeapons; });
	MatchProperty("AIMaxUnitsPerSpot", { int count; reader >> count; SetAIMaxUnitsPerSpot(count); });
	MatchProperty("AIUnitsCallLifts", { reader >> m_AIUnitsCallLifts; });
	MatchProperty("DeadCrewFallOut", { reader >> m_DeadCrewFallOut; });
	MatchProperty("PassengersFireFromVehicles", { reader >> m_PassengersFireFromVehicles; });
	MatchProperty("PathLineWeight", {
		reader >> m_PathTuning.LineWeight;
		m_PathTuning.LineWeight = std::clamp(m_PathTuning.LineWeight, 0.0F, 2.0F);
		s_OldPathLineWeight = m_PathTuning.LineWeight == 0.0F;
	});
	MatchProperty("PathLineWeightByRisk", {
		reader >> m_PathTuning.LineWeightByRisk;
		s_OldPathLineWeight = false;
	});
	MatchProperty("PathFullRiskLineWeight", { reader >> m_PathTuning.FullRiskLineWeight; m_PathTuning.FullRiskLineWeight = std::clamp(m_PathTuning.FullRiskLineWeight, 0.0F, 4.0F); });
	MatchProperty("PathUpCost", { reader >> m_PathTuning.UpCost; m_PathTuning.UpCost = std::clamp(m_PathTuning.UpCost, 0.0F, 5.0F); });
	MatchProperty("PathFlightCost", { reader >> m_PathTuning.FlightCostScale; m_PathTuning.FlightCostScale = std::clamp(m_PathTuning.FlightCostScale, 0.2F, 1.0F); });
	MatchProperty("PathJumpHeightCost", { reader >> m_PathTuning.JumpHeightCost; m_PathTuning.JumpHeightCost = std::clamp(m_PathTuning.JumpHeightCost, 0.0F, 2.0F); });
	MatchProperty("PathLadderCost", { reader >> m_PathTuning.LadderCostScale; m_PathTuning.LadderCostScale = std::clamp(m_PathTuning.LadderCostScale, 0.8F, 5.0F); });
	MatchProperty("PathFallCost", { reader >> m_PathTuning.FallCostPerNode; m_PathTuning.FallCostPerNode = std::clamp(m_PathTuning.FallCostPerNode, 0.0F, 10.0F); });
	MatchProperty("PathRouteVariety", { reader >> m_PathTuning.RouteVariety; m_PathTuning.RouteVariety = std::clamp(m_PathTuning.RouteVariety, 0.0F, 4.0F); });
	MatchProperty("PathFailedSpotCost", { reader >> m_PathTuning.FailedSpotCost; m_PathTuning.FailedSpotCost = std::clamp(m_PathTuning.FailedSpotCost, 0.0F, 100.0F); });
	MatchProperty("PathJetOverClimb", { reader >> m_PathTuning.JetOverClimb; m_PathTuning.JetOverClimb = std::clamp(m_PathTuning.JetOverClimb, 0.0F, 1.0F); });
	MatchProperty("PathRefreshSeconds", { reader >> m_PathTuning.RefreshSeconds; m_PathTuning.RefreshSeconds = std::clamp(m_PathTuning.RefreshSeconds, 1.0F, 30.0F); });
	MatchProperty("PathNoSightSeconds", { reader >> m_PathTuning.NoSightSeconds; m_PathTuning.NoSightSeconds = std::clamp(m_PathTuning.NoSightSeconds, 0.25F, 5.0F); });
	MatchProperty("PathStuckRemedySeconds", { reader >> m_PathTuning.StuckRemedySeconds; m_PathTuning.StuckRemedySeconds = std::clamp(m_PathTuning.StuckRemedySeconds, 0.5F, 10.0F); });
	MatchProperty("PathStuckRepathSeconds", { reader >> m_PathTuning.StuckRepathSeconds; m_PathTuning.StuckRepathSeconds = std::clamp(m_PathTuning.StuckRepathSeconds, 1.0F, 20.0F); });
	MatchProperty("PathAirCheckSeconds", { reader >> m_PathTuning.AirCheckSeconds; m_PathTuning.AirCheckSeconds = std::clamp(m_PathTuning.AirCheckSeconds, 0.1F, 3.0F); });
	MatchProperty("PathReachScale", { reader >> m_PathTuning.ReachScale; m_PathTuning.ReachScale = std::clamp(m_PathTuning.ReachScale, 0.5F, 3.0F); });
	MatchProperty("PathNoRouteRetrySeconds", { reader >> m_PathTuning.NoRouteRetrySeconds; m_PathTuning.NoRouteRetrySeconds = std::clamp(m_PathTuning.NoRouteRetrySeconds, 0.5F, 15.0F); });
	MatchProperty("DirectCaution", { reader >> m_DirectOrders.Caution; m_DirectOrders.Caution = std::clamp(m_DirectOrders.Caution, 0.0F, 1.0F); });
	MatchProperty("DirectFlightCost", { reader >> m_DirectOrders.FlightCostScale; m_DirectOrders.FlightCostScale = std::clamp(m_DirectOrders.FlightCostScale, 0.2F, 1.0F); });
	MatchProperty("DirectFuelShare", { reader >> m_DirectOrders.FuelShare; m_DirectOrders.FuelShare = std::clamp(m_DirectOrders.FuelShare, 0.1F, 1.2F); });
	MatchProperty("DirectRecklessSteadyCut", { reader >> m_DirectOrders.RecklessSteadyCut; m_DirectOrders.RecklessSteadyCut = std::clamp(m_DirectOrders.RecklessSteadyCut, 0.0F, 1.0F); });
	MatchProperty("DirectSteady", { bool unused; reader >> unused; }); // (The older "still steady" switch, off by default: its saved value is not carried over, so risky orders steady again.)
	MatchProperty("DirectFlightSpeed", { reader >> m_DirectOrders.FlightSpeed; m_DirectOrders.FlightSpeed = std::clamp(m_DirectOrders.FlightSpeed, 1.0F, 3.0F); });
	MatchProperty("DirectSafeLandings", { reader >> m_DirectOrders.SafeLandings; });
	MatchProperty("DirectTouchdown", { reader >> m_DirectOrders.Touchdown; m_DirectOrders.Touchdown = std::clamp(m_DirectOrders.Touchdown, 3.0F, 15.0F); });
	MatchProperty("DirectDigCost", { reader >> m_DirectOrders.DigCostScale; m_DirectOrders.DigCostScale = std::clamp(m_DirectOrders.DigCostScale, 0.01F, 1.0F); });
	MatchProperty("DirectMoveLineWeight", { reader >> m_DirectOrders.MoveLineWeight; m_DirectOrders.MoveLineWeight = std::clamp(m_DirectOrders.MoveLineWeight, 0.0F, 2.0F); });
	MatchProperty("DirectLineWeight", { reader >> m_DirectOrders.LineWeight; m_DirectOrders.LineWeight = std::clamp(m_DirectOrders.LineWeight, 0.0F, 1.0F); });
	MatchProperty("DirectIgnoreEnemies", { reader >> m_DirectOrders.IgnoreEnemies; });
	MatchProperty("DirectKeepPace", { reader >> m_DirectOrders.KeepPace; });
	MatchProperty("DirectStraightFlightRisk", { reader >> m_DirectOrders.StraightFlightRisk; m_DirectOrders.StraightFlightRisk = std::clamp(m_DirectOrders.StraightFlightRisk, 0.5F, 1.0F); });
	MatchProperty("DirectSkyRouteRisk", { reader >> m_DirectOrders.SkyRouteRisk; m_DirectOrders.SkyRouteRisk = std::clamp(m_DirectOrders.SkyRouteRisk, 0.5F, 1.0F); });
	MatchProperty("DirectDives", { reader >> m_DirectOrders.Dives; });
	MatchProperty("RiskFollowsSteadying", { reader >> m_DirectOrders.Follows.Steadying; });
	MatchProperty("RiskFollowsFuelWait", { reader >> m_DirectOrders.Follows.FuelWait; });
	MatchProperty("RiskFollowsRouteCaution", { reader >> m_DirectOrders.Follows.RouteCaution; });
	MatchProperty("RiskFollowsJetPreference", { reader >> m_DirectOrders.Follows.JetPreference; });
	MatchProperty("RiskFollowsFlightSpeed", { reader >> m_DirectOrders.Follows.FlightSpeed; });
	MatchProperty("RiskFollowsLanding", { reader >> m_DirectOrders.Follows.Landing; });
	MatchProperty("RiskFollowsStraightFlight", { reader >> m_DirectOrders.Follows.StraightFlight; });
	MatchProperty("RiskFollowsSkyRoute", { reader >> m_DirectOrders.Follows.SkyRoute; });
	MatchProperty("RiskFollowsEnemyAvoidance", { reader >> m_DirectOrders.Follows.EnemyAvoidance; });
	MatchProperty("RiskFollowsRunning", { reader >> m_DirectOrders.Follows.Running; });
	MatchProperty("RiskFollowsVehicles", { reader >> m_DirectOrders.Follows.Vehicles; });
	MatchProperty("DigAvoidCaveIns", { reader >> m_DigSafety.AvoidCaveIns; });
	MatchProperty("DigBackOff", { reader >> m_DigSafety.BackOff; });
	MatchProperty("DigToAvoidCaveIns", { reader >> m_DigSafety.DigToOrders; });
	MatchProperty("DigToPreviewWarns", { reader >> m_DigSafety.PreviewWarns; });
	MatchProperty("AIDodgesFallingGround", { reader >> m_AIDodgesFallingGround; });
	MatchProperty("DirectDiveRisk", { reader >> m_DirectOrders.DiveRisk; m_DirectOrders.DiveRisk = std::clamp(m_DirectOrders.DiveRisk, 0.5F, 1.0F); });
	MatchProperty("DirectDiveChance", { reader >> m_DirectOrders.DiveChance; m_DirectOrders.DiveChance = std::clamp(m_DirectOrders.DiveChance, 0.0F, 1.0F); });
	MatchProperty("DirectFullRiskDrag", { reader >> m_DirectOrders.RiskDragPixels; m_DirectOrders.RiskDragPixels = std::clamp(m_DirectOrders.RiskDragPixels, 200.0F, 1200.0F); });
	MatchProperty("DirectRiskDragPixels", { float unused; reader >> unused; }); // (The straight 300 px drag's, before the curve: its saved value is not carried over.)
	MatchProperty("DirectRiskyClickShare", { float unused; reader >> unused; }); // (The three click tier's strength, before the risk drag took its place.)
	MatchProperty("DirectDoubleClickShare", { float unused; reader >> unused; }); // (Its older name.)
	MatchProperty("CautiousCaution", { reader >> m_DirectOrders.CautiousCaution; m_DirectOrders.CautiousCaution = std::clamp(m_DirectOrders.CautiousCaution, 1.0F, 4.0F); });
	MatchProperty("CautiousFlightCost", { reader >> m_DirectOrders.CautiousFlightCostScale; m_DirectOrders.CautiousFlightCostScale = std::clamp(m_DirectOrders.CautiousFlightCostScale, 1.0F, 5.0F); });
	MatchProperty("CautiousFuelShare", { reader >> m_DirectOrders.CautiousFuelShare; m_DirectOrders.CautiousFuelShare = std::clamp(m_DirectOrders.CautiousFuelShare, 1.0F, 1.5F); });
	MatchProperty("CautiousFlightSpeed", { reader >> m_DirectOrders.CautiousFlightSpeed; m_DirectOrders.CautiousFlightSpeed = std::clamp(m_DirectOrders.CautiousFlightSpeed, 0.4F, 1.0F); });
	MatchProperty("CautiousTouchdown", { reader >> m_DirectOrders.CautiousTouchdown; m_DirectOrders.CautiousTouchdown = std::clamp(m_DirectOrders.CautiousTouchdown, 2.0F, 5.0F); });
	MatchProperty("CautiousEnemyAvoidance", { reader >> m_DirectOrders.CautiousEnemyAvoidance; m_DirectOrders.CautiousEnemyAvoidance = std::clamp(m_DirectOrders.CautiousEnemyAvoidance, 0.0F, 2.0F); });
	MatchProperty("NavDebugOverlay", { int level = 0; reader >> level; SetNavDebugOverlay(level); });
	MatchProperty("DebugTeam", { int team = 0; reader >> team; SetDebugTeam(team); });
	MatchProperty("UnitInspector", { int which = 0; reader >> which; SetUnitInspector(which); });
	MatchProperty("ShowSquadLinks", { reader >> m_ShowSquadLinks; });
	MatchProperty("ShowRecentSolves", { reader >> m_ShowRecentSolves; });
	MatchProperty("ShowTerrainUpdates", { reader >> m_ShowTerrainUpdates; });
	// Only in presets (see SaveTunables): what the panel holds for the moment.
	// The panel's settings for the moment: in presets only, and passed over in the one loaded at start so the game doesn't start in them.
	MatchProperty("GameSpeed", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { g_TimerMan.SetTimeScale(std::clamp(std::stof(value), 0.1F, 4.0F)); } });
	MatchProperty("PauseAI", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { Controller::SetAIPaused(std::stoi(value) != 0); } });
	MatchProperty("CameraZoom", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { g_FrameMan.SetCameraZoom(std::stof(value)); } });
	MatchProperty("FreezeSimulation", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { g_DebugMan.m_FreezeSim = std::stoi(value) != 0; } });
	MatchProperty("ShowPerformanceStats", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { g_DebugMan.m_ShowPerformanceMan = std::stoi(value) != 0; } });
	MatchProperty("ShowActorDebugDrawing", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { g_DebugMan.m_ShowActorDebugGui = std::stoi(value) != 0; } });
	MatchProperty("DrawCameraBounds", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { g_DebugMan.m_DrawCameraBounds = std::stoi(value) != 0; } });
	MatchProperty("DrawSpriteBounds", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { g_DebugMan.m_DrawSpriteBounds = std::stoi(value) != 0; } });
	MatchProperty("ImGuiDemoWindow", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { g_DebugMan.m_ImGuiDemoWindow = std::stoi(value) != 0; } });
	MatchProperty("FreeCam", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { g_DebugMan.m_EnableFreeCam = std::stoi(value) != 0; } });
	MatchProperty("FreeCamZoom", { std::string value = reader.ReadPropValue(); if (!s_ReadingStartupPreset) { g_DebugMan.m_FreeCamZoom = std::stof(value); } });
	MatchProperty("ShowOrderLabels", { reader >> m_ShowOrderLabels; });
	MatchProperty("CombatOverlay", { int which = 0; reader >> which; SetCombatOverlay(which); });
	MatchProperty("ShowLightSources", { reader >> m_ShowLightSources; });
	MatchProperty("ShowSunDirection", { reader >> m_ShowSunDirection; });
	MatchProperty("WorldSimOverlay", { int which = 0; reader >> which; SetWorldSimOverlay(which); });
	MatchProperty("SandboxStrokeLog", { reader >> m_SandboxStrokeLog; });
	MatchProperty("SandboxSpotReach", { reader >> m_SandboxSpotReach; });
	MatchProperty("SandboxGroupBadges", { reader >> m_SandboxGroupBadges; });
	MatchProperty("CommanderBarKey", { m_CommanderBarKey = reader.ReadPropValue(); });
	MatchProperty("ScenarioFactionPicks", { m_ScenarioFactionPicks = reader.ReadPropValue(); });
	MatchProperty("ScenarioSettingValues", { m_ScenarioSettingValues = reader.ReadPropValue(); });
	MatchProperty("ScenarioFlyingCraft", { reader >> m_ScenarioFlyingCraft; });
	MatchProperty("SandboxOrderGlyphs", { int which = 1; reader >> which; SetSandboxOrderGlyphs(which); });
	MatchProperty("SandboxSpawnStats", { int which = 1; reader >> which; SetSandboxSpawnStats(which); });
	MatchProperty("SandboxSpawnSort", { int which = 0; reader >> which; SetSandboxSpawnSort(which); });
	MatchProperty("SandboxAttackPings", { reader >> m_SandboxAttackPings; });
	MatchProperty("SandboxNoRouteMarkers", { reader >> m_SandboxNoRouteMarkers; });
	MatchProperty("SandboxMinimap", { reader >> m_SandboxMinimap; });
	MatchProperty("BattleAlerts", { reader >> m_BattleAlerts; });
	MatchProperty("BattleAlertLines", { int lines = 6; reader >> lines; SetBattleAlertLines(lines); });
	MatchProperty("BattleAlertSeconds", { int seconds = 12; reader >> seconds; SetBattleAlertSeconds(seconds); });
	MatchProperty("BattleKillFeed", { reader >> m_BattleKillFeed; });
	MatchProperty("BattleKillLines", { int lines = 6; reader >> lines; SetBattleKillLines(lines); });
	MatchProperty("BattleKillSeconds", { int seconds = 10; reader >> seconds; SetBattleKillSeconds(seconds); });
	MatchProperty("BattleAlertSounds", { reader >> m_BattleAlertSounds; });
	MatchProperty("BattleAlertVolume", { float volume = 0.6F; reader >> volume; SetBattleAlertVolume(volume); });
	MatchProperty("BattleAskFirst", { reader >> m_BattleAskFirst; });
	MatchProperty("BattleShowInfo", { reader >> m_BattleShowInfo; });
	MatchProperty("BattleShowObjectives", { reader >> m_BattleShowObjectives; });
	MatchProperty("BattleObjectiveLook", { int look = 0; reader >> look; SetBattleObjectiveLook(look); });
	MatchProperty("BattleShowSpawnZones", { reader >> m_BattleShowSpawnZones; });
	MatchProperty("BattleShowModeZones", { reader >> m_BattleShowModeZones; });
	MatchProperty("ArmyOverview", { reader >> m_ArmyOverview; });
	MatchProperty("LightsBySource", { reader >> m_LightsBySource; });
	MatchProperty("SandboxCharacterState", { reader >> m_SandboxCharacterState; });
	MatchProperty("SandboxAutoBattle", { reader >> m_SandboxAutoBattle; });
	MatchProperty("SandboxPaintAudit", { reader >> m_SandboxPaintAudit; });
	MatchProperty("SandboxSelectionCamera", { reader >> m_SandboxSelectionCamera; });
	MatchProperty("SandboxEffects", { reader >> m_SandboxEffects; });
	MatchProperty("SandboxGas", { reader >> m_SandboxGas; });
	MatchProperty("SandboxAir", { reader >> m_SandboxAir; });
	MatchProperty("SandboxSimState", { reader >> m_SandboxSimState; });
	MatchProperty("SandboxBarRight", { reader >> m_SandboxBarRight; });
	MatchProperty("SandboxOrdersOverlay", { int which = 0; reader >> which; SetSandboxOrdersOverlay(which); });
	MatchProperty("DebugChannels", { reader >> m_DebugChannels; });
	MatchProperty("TraceAllUnits", { reader >> m_TraceAllUnits; });
	MatchProperty("ShowFPSAndVersion", { reader >> m_ShowFPSAndVersion; });
	MatchProperty("ShowNotices", { reader >> m_ShowNotices; });
	MatchProperty("CrabBombThreshold", { reader >> m_CrabBombThreshold; });
	MatchProperty("ShowEnemyHUD", { reader >> m_ShowEnemyHUD; });
	MatchProperty("ShowUnitTags", { reader >> m_ShowUnitTags; });
	MatchProperty("ShowCPUAimReticles", { reader >> m_ShowCPUAimReticles; });
	MatchProperty("FreeAimThroughTerrain", { reader >> m_FreeAimThroughTerrain; });
	MatchProperty("ShowAlarmExclamation", { reader >> m_ShowAlarmExclamation; });
	MatchProperty("SmartBuyMenuNavigation", { reader >> m_EnableSmartBuyMenuNavigation; });
	MatchProperty("ScrapCompactingHeight", { reader >> g_SceneMan.m_ScrapCompactingHeight; });
	MatchProperty("AutomaticGoldDeposit", { reader >> m_AutomaticGoldDeposit; });
	MatchProperty("ScreenShakeStrength", { reader >> g_CameraMan.m_ScreenShakeStrength; });
	MatchProperty("HitStopStrength", { reader >> g_CameraMan.m_HitStopStrength; });
	MatchProperty("FrameCap", { g_WindowMan.SetFrameCap(std::stoi(reader.ReadPropValue())); });
	MatchProperty("ScreenShakeDecay", { reader >> g_CameraMan.m_ScreenShakeDecay; });
	MatchProperty("MaxScreenShakeTime", { reader >> g_CameraMan.m_MaxScreenShakeTime; });
	MatchProperty("DefaultShakePerUnitOfGibEnergy", { reader >> g_CameraMan.m_DefaultShakePerUnitOfGibEnergy; });
	MatchProperty("DefaultShakePerUnitOfRecoilEnergy", { reader >> g_CameraMan.m_DefaultShakePerUnitOfRecoilEnergy; });
	MatchProperty("DefaultShakeFromRecoilMaximum", { reader >> g_CameraMan.m_DefaultShakeFromRecoilMaximum; });
	MatchProperty("LaunchIntoActivity", { reader >> g_ActivityMan.m_LaunchIntoActivity; });
	MatchProperty("DefaultActivityType", { reader >> g_ActivityMan.m_DefaultActivityType; });
	MatchProperty("DefaultActivityName", { reader >> g_ActivityMan.m_DefaultActivityName; });
	MatchProperty("DefaultSceneName", { reader >> g_SceneMan.m_DefaultSceneName; });
	MatchProperty("LastMap", { m_LastMap = reader.ReadPropValue(); });
	MatchProperty("MapBrowserMod", { m_MapBrowserFilters.Mod = reader.ReadPropValue(); });
	MatchProperty("MapBrowserType", { reader >> m_MapBrowserFilters.Source; });
	MatchProperty("MapBrowserSize", { reader >> m_MapBrowserFilters.Size; });
	MatchProperty("MapBrowserFavouritesOnly", { reader >> m_MapBrowserFilters.OnlyFavourites; });
	MatchProperty("MapBrowserRecentOnly", { reader >> m_MapBrowserFilters.OnlyRecent; });
	MatchProperty("MapBrowserSort", { reader >> m_MapBrowserFilters.Sort; });
	MatchProperty("DisableLuaJIT", { reader >> m_DisableLuaJIT; });
	MatchProperty("EnableLuaDebugging", { reader >> m_EnableLuaDebugging; });
	MatchProperty("RecommendedMOIDCount", { reader >> m_RecommendedMOIDCount; });
	MatchProperty("SceneBackgroundAutoScaleMode", { SetSceneBackgroundAutoScaleMode(std::stoi(reader.ReadPropValue())); });
	MatchProperty("DisableFactionBuyMenuThemes", { reader >> m_DisableFactionBuyMenuThemes; });
	MatchProperty("DisableMedievalUnits", { reader >> m_DisableMedievalUnits; });
	MatchProperty("DisableFactionBuyMenuThemeCursors", { reader >> m_DisableFactionBuyMenuThemeCursors; });
	MatchProperty("PathFinderGridNodeSize", { reader >> m_PathFinderGridNodeSize; });
	MatchProperty("AIUpdateInterval", { reader >> m_AIUpdateInterval; });
	MatchProperty("NumberOfLuaStatesOverride", { reader >> m_NumberOfLuaStatesOverride; });
	MatchProperty("ForceImmediatePathingRequestCompletion", { reader >> m_ForceImmediatePathingRequestCompletion; });
	MatchProperty("EnableParticleSettling", { reader >> g_MovableMan.m_SettlingEnabled; });
	MatchProperty("EnableMOSubtraction", { reader >> g_MovableMan.m_MOSubtractionEnabled; });
	MatchProperty("DeltaTime", { g_TimerMan.SetDeltaTimeSecs(std::stof(reader.ReadPropValue())); });
	MatchProperty("AllowSavingToBase", { reader >> m_AllowSavingToBase; });
	MatchProperty("ShowMetaScenes", { reader >> m_ShowMetaScenes; });
	MatchProperty("SkipIntro", { reader >> m_SkipIntro; });
	MatchProperty("ShowToolTips", { reader >> m_ShowToolTips; });
	MatchProperty("CaseSensitiveFilePaths", { System::EnableFilePathCaseSensitivity(std::stoi(reader.ReadPropValue())); });
	MatchProperty("DisableLoadingScreenProgressReport", { reader >> m_DisableLoadingScreenProgressReport; });
	MatchProperty("LoadingScreenProgressReportPrecision", { reader >> m_LoadingScreenProgressReportPrecision; });
	MatchProperty("ConsoleScreenRatio", { g_ConsoleMan.SetConsoleScreenSize(std::stof(reader.ReadPropValue())); });
	MatchProperty("ConsoleUseMonospaceFont", { reader >> g_ConsoleMan.m_ConsoleUseMonospaceFont; });
	MatchProperty("AdvancedPerformanceStats", { reader >> g_PerformanceMan.m_AdvancedPerfStats; });
	MatchProperty("MenuTransitionDurationMultiplier", { SetMenuTransitionDurationMultiplier(std::stof(reader.ReadPropValue())); });
	MatchProperty("DrawAtomGroupVisualizations", { reader >> m_DrawAtomGroupVisualizations; });
	MatchProperty("DrawHandAndFootGroupVisualizations", { reader >> m_DrawHandAndFootGroupVisualizations; });
	MatchProperty("DrawLimbPathVisualizations", { reader >> m_DrawLimbPathVisualizations; });
	MatchProperty("DrawRaycastVisualizations", { reader >> g_SceneMan.m_DrawRayCastVisualizations; });
	MatchProperty("DrawPixelCheckVisualizations", { reader >> g_SceneMan.m_DrawPixelCheckVisualizations; });
	MatchProperty("PrintDebugInfo", { reader >> m_PrintDebugInfo; });
	MatchProperty("EnableDebugMenus", { reader >> g_DebugMan.m_ShowDebugWindow; });
	MatchProperty("StartupPreset", { m_StartupPresets[StartupSlot(PresetGroup::Everything)] = reader.ReadPropValue(); });
	MatchProperty("StartupGameplayPreset", { m_StartupPresets[StartupSlot(PresetGroup::Gameplay)] = reader.ReadPropValue(); });
	MatchProperty("StartupVisualPreset", { m_StartupPresets[StartupSlot(PresetGroup::Visual)] = reader.ReadPropValue(); });
	MatchProperty("ControlLinkPort", { ControlLink::s_SettingsPort = std::stoi(reader.ReadPropValue()); });
	MatchProperty("ShowGraphicsLab", { g_DebugMan.m_ShowGraphicsLab = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("ShowWorldDebug", { g_DebugMan.m_ShowWorldDebug = std::stoi(reader.ReadPropValue()) != 0; }); // Read only, for automated captures.
	MatchProperty("MeasureModuleLoadTime", { reader >> m_MeasureModuleLoadTime; });
	MatchProperty("VisibleAssemblyGroup", { m_VisibleAssemblyGroupsList.push_back(reader.ReadPropValue()); });
	MatchProperty("DisableMod", { m_DisabledMods.try_emplace(reader.ReadPropValue(), true); });
	MatchProperty("EnableGlobalScript", { m_EnabledGlobalScripts.try_emplace(reader.ReadPropValue(), true); });
	MatchProperty("ForceDisableMultimouse", { reader >> g_UInputMan.m_ForceDisableMultiMouseKeyboard; });
	MatchProperty("MouseSensitivity", { reader >> g_UInputMan.m_MouseSensitivity; });
	MatchForwards("Player1Scheme") MatchForwards("Player2Scheme") MatchForwards("Player3Scheme") MatchProperty("Player4Scheme", {
		for (int player = Players::PlayerOne; player < Players::MaxPlayerCount; player++) {
			std::string playerNum = std::to_string(player + 1);
			if (propName == "Player" + playerNum + "Scheme") {
				g_UInputMan.m_ControlScheme[player].Reset();
				reader >> g_UInputMan.m_ControlScheme[player];
				break;
			}
		}
	});

	EndPropertyList;
}

SettingsMan::LooseBitKind SettingsMan::GetLooseBitKind(int materialIndex) {
	static std::array<signed char, 256> s_Kinds = [] {
		std::array<signed char, 256> kinds;
		kinds.fill(-1);
		return kinds;
	}();
	materialIndex &= 0xFF;
	if (s_Kinds[materialIndex] >= 0) {
		return static_cast<LooseBitKind>(s_Kinds[materialIndex]);
	}
	const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(materialIndex));
	if (!material) {
		return OtherBits;
	}
	const std::string& name = material->GetPresetName();
	auto has = [&name](std::initializer_list<const char*> parts) {
		for (const char* part: parts) {
			if (name.find(part) != std::string::npos) {
				return true;
			}
		}
		return false;
	};
	LooseBitKind kind = OtherBits;
	if (material->IsBody() || has({"Blood", "Flesh", "Bone"})) {
		kind = BloodAndFlesh;
	} else if (FluidSim::IsLiquid(materialIndex)) {
		kind = Liquids;
	} else if (has({"Earth", "Stone", "Sand", "Snow", "Ice", "Gravel", "Concrete", "Rubble", "Ash", "soil", "Cave", "Rock", "Bedrock"})) {
		kind = EarthAndStone;
	} else if (has({"Metal", "Military", "Plate", "Bullet", "Casing", "Ladder", "Gold"})) {
		kind = Metal;
	} else if (has({"Wood", "Vegetation", "Grass", "Tree", "Charcoal", "Candle", "Leaves"})) {
		kind = WoodAndPlants;
	}
	s_Kinds[materialIndex] = static_cast<signed char>(kind);
	return kind;
}

void SettingsMan::ClampGibChunks() {
	m_GibChunks.CountPercent = std::clamp(m_GibChunks.CountPercent, 0, 200);
	m_GibChunks.MaxLoose = std::clamp(m_GibChunks.MaxLoose, 0, 5000);
	m_GibChunks.SettleMS = std::clamp(m_GibChunks.SettleMS, -1, 10000);
	m_GibChunks.MaxBounces = std::clamp(m_GibChunks.MaxBounces, 1, 10);
	m_GibChunks.AtomSpacing = std::clamp(m_GibChunks.AtomSpacing, 0, 16);
}

void SettingsMan::ClampAICombat() {
	m_AICombat.Accuracy = std::clamp(m_AICombat.Accuracy, 0.25F, 4.0F);
	m_AICombat.Reaction = std::clamp(m_AICombat.Reaction, 0.25F, 3.0F);
	m_AICombat.Spotting = std::clamp(m_AICombat.Spotting, 0.25F, 3.0F);
	m_AICombat.SightRange = std::clamp(m_AICombat.SightRange, 0.5F, 2.0F);
	m_AICombat.TargetMemory = std::clamp(m_AICombat.TargetMemory, 0.25F, 4.0F);
	m_AICombat.GrenadeUse = std::clamp(m_AICombat.GrenadeUse, 0.0F, 1.0F);
	m_AICombat.Burst = std::clamp(m_AICombat.Burst, 0.5F, 2.0F);
}

void SettingsMan::SaveTunables(Writer& writer, const LightingSettings& lighting, bool forPreset) const {
	// Which group of presets the settings after it are in (see TunableGroups): written only while they are being sorted, never to a file.
	auto group = [&writer](PresetGroup kind) {
		if (s_MarkingGroups) {
			writer.NewPropertyWithValue(c_GroupMarker, static_cast<int>(kind));
		}
	};
	writer.NewPropertyWithValue("LightingSettingsVersion", c_LightingSettingsVersion);
	group(PresetGroup::Visual);
	writer.NewPropertyWithValue("GraphicsQuality", lighting.GraphicsQuality);
	writer.NewPropertyWithValue("LightingEnabled", lighting.Enabled);
	writer.NewPropertyWithValue("LightingAmbient", WriteVec3(lighting.Ambient));
	writer.NewPropertyWithValue("LightingSkyColor", WriteVec3(lighting.SkyColor));
	writer.NewPropertyWithValue("LightingForegroundAmbient", WriteVec3(lighting.ForegroundAmbient));
	writer.NewPropertyWithValue("LightingAirFalloff", lighting.AirFalloff);
	writer.NewPropertyWithValue("LightingSolidFalloff", lighting.SolidFalloff);
	writer.NewPropertyWithValue("LightingPropagationSteps", lighting.PropagationIterationsPerFrame);
	writer.NewPropertyWithValue("GodRays", lighting.GodRays);
	writer.NewPropertyWithValue("FogVolume", lighting.FogVolume);
	writer.NewPropertyWithValue("FogMorningMist", lighting.FogMorningMist);
	writer.NewPropertyWithValue("FogOpacity", lighting.FogOpacity);
	writer.NewPropertyWithValue("FogClearSeconds", lighting.FogClearSeconds);
	writer.NewPropertyWithValue("LightningBolts", lighting.LightningBolts);
	writer.NewPropertyWithValue("LightningBrightness", lighting.LightningBrightness);
	writer.NewPropertyWithValue("StormFlashes", lighting.StormFlashes);
	writer.NewPropertyWithValue("SkyFollowsTime", lighting.SkyFollowsTime);
	writer.NewPropertyWithValue("DeepNightDarkness", lighting.DeepNightDarkness);
	writer.NewPropertyWithValue("AtmosphereHaze", lighting.AtmosphereHaze);
	writer.NewPropertyWithValue("AtmosphereColor", WriteVec3(lighting.AtmosphereColor));
	writer.NewPropertyWithValue("WeatherType", lighting.WeatherType);
	if (!lighting.WeatherName.empty()) {
		// Still waiting to be found among the presets: kept as it was asked for.
		writer.NewPropertyWithValue("WeatherName", lighting.WeatherName);
	} else if (lighting.WeatherType > Weather::c_BuiltInCount) {
		// A custom weather's slot depends on the mods loaded, so it's found again by name.
		if (const Weather* weather = Weather::GetSlot(lighting.WeatherType)) {
			writer.NewPropertyWithValue("WeatherName", weather->GetPresetName());
		}
	}
	writer.NewPropertyWithValue("OverrideMapWeather", lighting.OverrideMapWeather);
	writer.NewPropertyWithValue("CustomWeather", lighting.CustomWeather);
	writer.NewPropertyWithValue("WeatherGlow", lighting.WeatherGlow);
	writer.NewPropertyWithValue("WeatherIntensity", lighting.WeatherIntensity);
	writer.NewPropertyWithValue("WeatherSpeed", lighting.WeatherSpeed);
	writer.NewPropertyWithValue("WeatherEverywhere", lighting.WeatherEverywhere);
	writer.NewPropertyWithValue("WeatherOverBackground", lighting.WeatherOverBackground);
	// (Both on the Time & weather page, under Visual & lighting: a preset holds what is under its heading.)
	writer.NewPropertyWithValue("WaterFreezes", FluidSim::FreezingEnabled());
	writer.NewPropertyWithValue("LightningStrikes", static_cast<int>(WeatherLightning::GetStrikes()));
	writer.NewPropertyWithValue("Wind", lighting.Wind);
	writer.NewPropertyWithValue("TimeOfDay", lighting.TimeOfDay);
	writer.NewPropertyWithValue("DayLengthMinutes", lighting.DayLengthMinutes);
	writer.NewPropertyWithValue("LightingGlowIntensity", lighting.GlowLightIntensity);
	writer.NewPropertyWithValue("LightingGlowRadiusScale", lighting.GlowLightRadiusScale);
	writer.NewPropertyWithValue("LightingMaxScreenLights", lighting.MaxScreenLights);
	writer.NewPropertyWithValue("LightingShadowStrength", lighting.ShadowStrength);
	writer.NewPropertyWithValue("LightShadowField", lighting.LightShadowField);
	writer.NewPropertyWithValue("SoftWallLight", lighting.SoftWallLight);
	writer.NewPropertyWithValue("LightShadowSoftness", lighting.LightShadowSoftness);
	writer.NewPropertyWithValue("UnitShadows", lighting.UnitShadows);
	writer.NewPropertyWithValue("SunShadows", lighting.SunShadows);
	writer.NewPropertyWithValue("SunShadowMap", lighting.SunShadowMap);
	writer.NewPropertyWithValue("SunShadowSoftness", lighting.SunShadowSoftness);
	writer.NewPropertyWithValue("ShelterMask", lighting.ShelterMask);
	writer.NewPropertyWithValue("ShelterSoftness", lighting.ShelterSoftness);
	writer.NewPropertyWithValue("BackgroundShadows", lighting.BackgroundShadows);
	writer.NewPropertyWithValue("BackgroundShadowLength", lighting.BackgroundShadowLength);
	writer.NewPropertyWithValue("ContactShading", lighting.ContactShading);
	writer.NewPropertyWithValue("LightingEmissiveIntensity", lighting.EmissiveIntensity);
	writer.NewPropertyWithValue("LightingEdgeLighting", lighting.EdgeLighting);
	writer.NewPropertyWithValue("LightingSpecular", lighting.Specular);
	writer.NewPropertyWithValue("UnitShineLights", lighting.UnitShineLights);
	writer.NewPropertyWithValue("UnitShineLamps", lighting.UnitShineLamps);
	writer.NewPropertyWithValue("UnitShineSun", lighting.UnitShineSun);
	writer.NewPropertyWithValue("LightingMetals", lighting.Metals);
	writer.NewPropertyWithValue("LightingRelief", lighting.Relief);
	writer.NewPropertyWithValue("PostScanlines", lighting.Scanlines);
	writer.NewPropertyWithValue("CRTStyle", lighting.CRTStyle);
	writer.NewPropertyWithValue("ScreenStyle", lighting.ScreenStyle);
	writer.NewPropertyWithValue("ScreenStyleStrength", lighting.ScreenStyleStrength);
	WriteRetroLayers(writer, "Retro", lighting.Retro);
	WriteRetroLayers(writer, "Synthwave", lighting.StyleRetro[LightingSettings::StyleSynthwave]);
	WriteRetroLayers(writer, "Hackerman", lighting.StyleRetro[LightingSettings::StyleHackerman]);
	WriteRetroLayers(writer, "Comic", lighting.StyleRetro[LightingSettings::StyleComic]);
	WriteRetroLayers(writer, "GameBoy", lighting.StyleRetro[LightingSettings::StyleGameBoy]);
	writer.NewPropertyWithValue("ScreenVariant", lighting.ScreenVariant.empty() ? std::string("None") : lighting.ScreenVariant);
	WriteRetroLayers(writer, "Variant", lighting.VariantRetro);
	writer.NewPropertyWithValue("AsciiModeEverywhere", lighting.Ascii.Everywhere);
	writer.NewPropertyWithValue("AsciiModeBackground", lighting.Ascii.Background);
	writer.NewPropertyWithValue("AsciiModeSize", lighting.Ascii.Size);
	writer.NewPropertyWithValue("AsciiModeEdgeGlyphs", lighting.Ascii.EdgeGlyphs);
	writer.NewPropertyWithValue("AsciiModeContrast", lighting.Ascii.Contrast);
	writer.NewPropertyWithValue("AsciiModeExposure", lighting.Ascii.Exposure);
	writer.NewPropertyWithValue("AsciiModeColours", lighting.Ascii.Colours);
	writer.NewPropertyWithValue("AsciiModeCellFill", lighting.Ascii.CellFill);
	writer.NewPropertyWithValue("AsciiModeGlow", lighting.Ascii.Glow);
	writer.NewPropertyWithValue("AsciiModeChurn", lighting.Ascii.Churn);
	writer.NewPropertyWithValue("AsciiModeWorldLocked", lighting.Ascii.WorldLocked);
	writer.NewPropertyWithValue("UpscaleSharpness", lighting.UpscaleSharpness);
	writer.NewPropertyWithValue("IntegerScaling", g_WindowMan.GetIntegerScaling());
	writer.NewPropertyWithValue("LightingIndirect", lighting.IndirectLight);
	writer.NewPropertyWithValue("RadianceCascades", lighting.RadianceCascades);
	writer.NewPropertyWithValue("GIStrength", lighting.GIStrength);
	writer.NewPropertyWithValue("GIBounce", lighting.GIBounce);
	writer.NewPropertyWithValue("DistortionEnabled", lighting.DistortionEnabled);
	writer.NewPropertyWithValue("HeatHaze", lighting.HeatHaze);
	writer.NewPropertyWithValue("HazeFromHeat", lighting.HazeFromHeat);
	writer.NewPropertyWithValue("FireStyle", lighting.FireStyle);
	writer.NewPropertyWithValue("FireFlameSize", lighting.FireFlameSize);
	writer.NewPropertyWithValue("UnitOutline", lighting.UnitOutline);
	writer.NewPropertyWithValue("UnitOutlineOverEverything", lighting.UnitOutlineOverEverything);
	writer.NewPropertyWithValue("UnitOutlineWidth", lighting.UnitOutlineWidth);
	writer.NewPropertyWithValue("UnitTeamColorParts", lighting.UnitTeamColorParts);
	writer.NewPropertyWithValue("UnitOutlineTeamColor", lighting.UnitOutlineTeamColor);
	writer.NewPropertyWithValue("UnitOutlineColor", WriteVec3(lighting.UnitOutlineColor));
	writer.NewPropertyWithValue("UnitOutlineOpacity", lighting.UnitOutlineOpacity);
	writer.NewPropertyWithValue("UnitOutlineGlow", lighting.UnitOutlineGlow);
	writer.NewPropertyWithValue("PaletteAnimation", lighting.PaletteAnimation);
	writer.NewPropertyWithValue("PaletteAnimationStrength", lighting.PaletteAnimationStrength);
	writer.NewPropertyWithValue("FireFlameBrightness", lighting.FireFlameBrightness);
	writer.NewPropertyWithValue("ShockwaveStrength", lighting.ShockwaveStrength);
	writer.NewPropertyWithValue("Embers", lighting.Embers);
	writer.NewPropertyWithValue("EffectsSparks", lighting.EffectsSparks);
	writer.NewPropertyWithValue("EffectsDust", lighting.EffectsDust);
	writer.NewPropertyWithValue("EffectsDebris", lighting.EffectsDebris);
	writer.NewPropertyWithValue("SparkLights", lighting.SparkLights);
	writer.NewPropertyWithValue("SmokeScattering", lighting.SmokeScattering);
	writer.NewPropertyWithValue("SmokeShading", lighting.SmokeShading);
	writer.NewPropertyWithValue("SmokeShadingStrength", lighting.SmokeShadingStrength);
	writer.NewPropertyWithValue("ScorchMarks", lighting.ScorchMarks);
	writer.NewPropertyWithValue("Stains", lighting.Stains);
	writer.NewPropertyWithValue("StainSurface", lighting.StainSurface);
	writer.NewPropertyWithValue("StainShine", lighting.StainShine);
	writer.NewPropertyWithValue("DecalsFade", lighting.DecalsFade);
	writer.NewPropertyWithValue("DecalFadeMinutes", lighting.DecalFadeMinutes);
	writer.NewPropertyWithValue("WetnessMap", lighting.WetnessMap);
	writer.NewPropertyWithValue("WetDrySeconds", lighting.WetDrySeconds);
	writer.NewPropertyWithValue("Puddles", lighting.Puddles);
	writer.NewPropertyWithValue("LivingWorld", lighting.LivingWorld);
	writer.NewPropertyWithValue("Headlamps", lighting.Headlamps);
	group(PresetGroup::Gameplay);
	writer.NewPropertyWithValue("NightAffectsAI", lighting.NightAffectsAI);
	group(PresetGroup::Visual);
	writer.NewPropertyWithValue("BloomEnabled", lighting.BloomEnabled);
	writer.NewPropertyWithValue("BloomThreshold", lighting.BloomThreshold);
	writer.NewPropertyWithValue("BloomKnee", lighting.BloomKnee);
	writer.NewPropertyWithValue("ShoulderStart", lighting.ShoulderStart);
	writer.NewPropertyWithValue("HotSpotSeconds", lighting.HotSpotSeconds);
	writer.NewPropertyWithValue("BloomIntensity", lighting.BloomIntensity);
	writer.NewPropertyWithValue("PostExposure", lighting.Exposure);
	writer.NewPropertyWithValue("AutoExposure", lighting.AutoExposure);
	writer.NewPropertyWithValue("AutoExposureLow", lighting.AutoExposureLow);
	writer.NewPropertyWithValue("AutoExposureHigh", lighting.AutoExposureHigh);
	writer.NewPropertyWithValue("AutoExposureGameTime", lighting.AutoExposureGameTime);
	writer.NewPropertyWithValue("PostVignette", lighting.Vignette);
	writer.NewPropertyWithValue("GradeTemperature", lighting.Temperature);
	writer.NewPropertyWithValue("GradeTint", lighting.Tint);
	writer.NewPropertyWithValue("GradeContrast", lighting.Contrast);
	writer.NewPropertyWithValue("GradeShadowTint", WriteVec3(lighting.ShadowTint));
	writer.NewPropertyWithValue("GradeHighlightTint", WriteVec3(lighting.HighlightTint));
	writer.NewPropertyWithValue("FilmGrain", lighting.FilmGrain);
	writer.NewPropertyWithValue("EventLooks", lighting.EventLooks);
	writer.NewPropertyWithValue("EventLookStrength", lighting.EventLookStrength);
	writer.NewPropertyWithValue("EventBlastFlash", lighting.EventBlastFlash);
	writer.NewPropertyWithValue("EventHurtLook", lighting.EventHurtLook);
	writer.NewPropertyWithValue("EventFireWarmth", lighting.EventFireWarmth);
	writer.NewPropertyWithValue("SunDisc", lighting.SunDisc);
	writer.NewPropertyWithValue("CloudShadows", lighting.CloudShadows);
	writer.NewPropertyWithValue("CloudLayer", lighting.CloudLayer);
	writer.NewPropertyWithValue("CloudCover", lighting.CloudCover);
	writer.NewPropertyWithValue("CloudOpacity", lighting.CloudOpacity);
	writer.NewPropertyWithValue("CloudSize", lighting.CloudSize);
	writer.NewPropertyWithValue("CloudHeight", lighting.CloudHeight);
	writer.NewPropertyWithValue("SurfaceStates", lighting.SurfaceStates);
	writer.NewPropertyWithValue("TracerLights", lighting.TracerLights);
	writer.NewPropertyWithValue("WeatherLight", lighting.WeatherLight);
	writer.NewPropertyWithValue("RainSplashes", lighting.RainSplashes);
	writer.NewPropertyWithValue("WaterFoam", lighting.WaterFoam);
	writer.NewPropertyWithValue("WaterFoamStray", lighting.WaterFoamStray);
	writer.NewPropertyWithValue("WaterFoamBrightness", lighting.WaterFoamBrightness);
	writer.NewPropertyWithValue("WaterFoamGlow", lighting.WaterFoamGlow);
	writer.NewPropertyWithValue("WaterMist", lighting.WaterMist);
	writer.NewPropertyWithValue("WaterSplash", lighting.WaterSplash);
	writer.NewPropertyWithValue("SplashDrops", lighting.SplashDrops);
	writer.NewPropertyWithValue("SplashHeight", lighting.SplashHeight);
	writer.NewPropertyWithValue("SplashWidth", lighting.SplashWidth);
	writer.NewPropertyWithValue("SplashDropSize", lighting.SplashDropSize);
	writer.NewPropertyWithValue("SplashSpray", lighting.SplashSpray);
	writer.NewPropertyWithValue("SplashUnder", lighting.SplashUnder);
	writer.NewPropertyWithValue("SplashUnderLiquidColor", lighting.SplashUnderLiquidColor);
	writer.NewPropertyWithValue("SplashUnderHeight", lighting.SplashUnderHeight);
	writer.NewPropertyWithValue("SplashUnderWidth", lighting.SplashUnderWidth);
	writer.NewPropertyWithValue("SplashUnderDropSize", lighting.SplashUnderDropSize);
	writer.NewPropertyWithValue("SplashUnderOpacity", lighting.SplashUnderOpacity);
	writer.NewPropertyWithValue("SplashUnderScatter", lighting.SplashUnderScatter);
	writer.NewPropertyWithValue("SplashUnderColor", WriteVec3(lighting.SplashUnderColor));
	writer.NewPropertyWithValue("PuffVariety", lighting.PuffVariety);
	writer.NewPropertyWithValue("SplashFroth", lighting.SplashFroth);
	writer.NewPropertyWithValue("SplashFrothDensity", lighting.SplashFrothDensity);
	writer.NewPropertyWithValue("SplashFrothSpecks", lighting.SplashFrothSpecks);
	writer.NewPropertyWithValue("SplashFrothSize", lighting.SplashFrothSize);
	writer.NewPropertyWithValue("SplashFrothLife", lighting.SplashFrothLife);
	writer.NewPropertyWithValue("SplashFrothOpacity", lighting.SplashFrothOpacity);
	writer.NewPropertyWithValue("WaterThinFlow", lighting.WaterThinFlow);
	writer.NewPropertyWithValue("WaterFoamBubbles", lighting.WaterFoamBubbles);
	writer.NewPropertyWithValue("DistinctLiquidLooks", lighting.DistinctLiquidLooks);
	writer.NewPropertyWithValue("WaterCaustics", lighting.WaterCaustics);
	writer.NewPropertyWithValue("WaterFlowSurface", lighting.WaterFlowSurface);
	writer.NewPropertyWithValue("WaterFlowStrength", lighting.WaterFlowStrength);
	writer.NewPropertyWithValue("WaterLightGlow", lighting.WaterLightGlow);
	writer.NewPropertyWithValue("WaterReflections", lighting.WaterReflections);
	writer.NewPropertyWithValue("WaterReflectionStrength", lighting.WaterReflectionStrength);
	writer.NewPropertyWithValue("WaterRefraction", lighting.WaterRefraction);
	writer.NewPropertyWithValue("WaterRipples", lighting.WaterRipples);
	writer.NewPropertyWithValue("WaterMirrorSurface", lighting.WaterMirrorSurface);
	writer.NewPropertyWithValue("WaterSoftReflection", lighting.WaterSoftReflection);
	writer.NewPropertyWithValue("SoftSmoke", lighting.SoftSmoke);
	writer.NewPropertyWithValue("EffectLayers", lighting.EffectLayers);
	for (int layer = 0; layer < LightingSettings::EffectLayerCount; ++layer) {
		writer.NewPropertyWithValue(LightingSettings::EffectLayerKeys[layer], lighting.EffectBehind[layer]);
	}
	writer.NewPropertyWithValue("WaterMistSize", lighting.WaterMistSize);
	writer.NewPropertyWithValue("WaterMistLife", lighting.WaterMistLife);
	writer.NewPropertyWithValue("WaterMistOpacity", lighting.WaterMistOpacity);
	writer.NewPropertyWithValue("WaterMistSpread", lighting.WaterMistSpread);
	writer.NewPropertyWithValue("WaterMistBrightness", lighting.WaterMistBrightness);
	writer.NewPropertyWithValue("WaterMistGlow", lighting.WaterMistGlow);
	writer.NewPropertyWithValue("TracerGlow", lighting.TracerGlow);
	writer.NewPropertyWithValue("TracerLightBrightness", lighting.TracerLightBrightness);
	writer.NewPropertyWithValue("TracerLightReach", lighting.TracerLightReach);
	writer.NewPropertyWithValue("TracerLightRandomness", lighting.TracerLightRandomness);
	writer.NewPropertyWithValue("SaberLightBrightness", lighting.SaberLightBrightness);
	writer.NewPropertyWithValue("SaberLightReach", lighting.SaberLightReach);
	writer.NewPropertyWithValue("JetpackLightBrightness", lighting.JetpackLightBrightness);
	writer.NewPropertyWithValue("JetpackLightReach", lighting.JetpackLightReach);
	writer.NewPropertyWithValue("SaberAirGlow", lighting.SaberAirGlow);
	writer.NewPropertyWithValue("LightSaturation", lighting.LightSaturation);
	writer.NewPropertyWithValue("LightTint", WriteVec3(lighting.LightTint));
	writer.NewPropertyWithValue("LampBrightness", lighting.LampBrightness);
	writer.NewPropertyWithValue("LampReach", lighting.LampReach);
	writer.NewPropertyWithValue("LampTint", WriteVec3(lighting.LampTint));
	writer.NewPropertyWithValue("LampCache", lighting.LampCache);
	writer.NewPropertyWithValue("LampCacheDetail", lighting.LampCacheDetail);
	writer.NewPropertyWithValue("HeadlampBrightness", lighting.HeadlampBrightness);
	writer.NewPropertyWithValue("HeadlampReach", lighting.HeadlampReach);
	writer.NewPropertyWithValue("HeadlampWidth", lighting.HeadlampWidth);
	writer.NewPropertyWithValue("HeadlampColor", WriteVec3(lighting.HeadlampColor));
	writer.NewPropertyWithValue("HeadlampGlow", lighting.HeadlampGlow);
	writer.NewPropertyWithValue("HeadlampTeamTint", lighting.HeadlampTeamTint);
	writer.NewPropertyWithValue("HeadlampsByDay", lighting.HeadlampsByDay);
	writer.NewPropertyWithValue("HeadlampDarkThreshold", lighting.HeadlampDarkThreshold);
	writer.NewPropertyWithValue("HeadlampsOnlyInDark", lighting.HeadlampsOnlyInDark);
	writer.NewPropertyWithValue("AimDotsLight", lighting.AimDotsLight);
	group(PresetGroup::Session);
	writer.NewPropertyWithValue("ShowAIPaths", Actor::ShowAIPaths());
	group(PresetGroup::Visual);
	writer.NewPropertyWithValue("BackgroundBlur", lighting.BackgroundBlur);
	writer.NewPropertyWithValue("DepthOfField", lighting.DepthOfField);
	writer.NewPropertyWithValue("DepthOfFieldFocus", lighting.DepthOfFieldFocus);
	writer.NewPropertyWithValue("DepthOfFieldStrength", lighting.DepthOfFieldStrength);
	writer.NewPropertyWithValue("TiltShift", lighting.TiltShift);
	writer.NewPropertyWithValue("TiltShiftLine", lighting.TiltShiftLine);
	writer.NewPropertyWithValue("TiltShiftStrength", lighting.TiltShiftStrength);
	writer.NewPropertyWithValue("FocusEffectsInPhotoModeOnly", lighting.FocusEffectsInPhotoModeOnly);
	writer.NewPropertyWithValue("ChromaticAberration", lighting.ChromaticAberration);
	writer.NewPropertyWithValue("ModShaders", lighting.ModShaders);
	writer.NewPropertyWithValue("ModShaderStrength", lighting.ModShaderStrength);
	writer.NewPropertyWithValue("SpriteMaps", lighting.SpriteMaps);
	writer.NewPropertyWithValue("SpriteMapStrength", lighting.SpriteMapStrength);
	writer.NewPropertyWithValue("PostSaturation", lighting.Saturation);
	group(PresetGroup::Gameplay);
	writer.NewPropertyWithValue("TerrainFire", TerrainFire::IsEnabled());
	writer.NewPropertyWithValue("EmberIgniteChance", TerrainFire::GetEmberIgniteChance());
	writer.NewPropertyWithValue("RopeReaction", RopeSim::GetReaction());
	writer.NewPropertyWithValue("RopeSettleSeconds", RopeSim::GetSettleSeconds());
	writer.NewPropertyWithValue("CandleBurnMinutes", TerrainCandle::GetBurnMinutes());
	writer.NewPropertyWithValue("TerrainCollapse", TerrainCollapse::IsEnabled());
	writer.NewPropertyWithValue("FlowingLiquids", FluidSim::IsEnabled());
	writer.NewPropertyWithValue("LoosePowders", FluidSim::PowdersEnabled());
	writer.NewPropertyWithValue("BloodFlows", FluidSim::BloodFlows());
	writer.NewPropertyWithValue("LiquidsDrainBottom", FluidSim::DrainsBottom());
	writer.NewPropertyWithValue("LiquidsDrainSides", FluidSim::DrainsSides());
	writer.NewPropertyWithValue("PowdersFallOut", FluidSim::PowdersFallOut());
	writer.NewPropertyWithValue("CollapseBuildings", TerrainCollapse::BuildingsFall());
	writer.NewPropertyWithValue("UnitsBumpIntoTrees", TerrainTrees::UnitsCollide());
	writer.NewPropertyWithValue("TreeStrayBulletPercent", TerrainTrees::StrayBulletPercent());
	writer.NewPropertyWithValue("FallingGroundPassesTrees", TerrainCollapse::PassesTrees());
	writer.NewPropertyWithValue("ShowHoldZones", TerrainCollapse::ShowsHoldZones());
	writer.NewPropertyWithValue("ZeroGTerrainFollowsGravity", ZeroGravity::TerrainFollowsGravity());
	writer.NewPropertyWithValue("ZeroGLiquidsFloat", ZeroGravity::LiquidsFloat());
	writer.NewPropertyWithValue("ZeroGCountZones", ZeroGravity::CountZones());
	writer.NewPropertyWithValue("ZeroGJetpackAnyDirection", ZeroGravity::JetpackAnyDirection());
	writer.NewPropertyWithValue("ZeroGAIFlies", ZeroGravity::AIFlies());
	writer.NewPropertyWithValue("ZeroGPushOff", ZeroGravity::PushOff());
	writer.NewPropertyWithValue("ZeroGFlySpeed", ZeroGravity::FlySpeed());
	writer.NewPropertyWithValue("ZeroGPushSpeed", ZeroGravity::PushSpeed());
	// (On the Falling ground page, under Gameplay & physics: a preset holds what is under its heading.)
	writer.NewPropertyWithValue("UnitsInFrontOfTrees", TerrainTrees::DrawnBehindUnits());
	writer.NewPropertyWithValue("CollapseFloatingStays", TerrainCollapse::GetTuning().FloatingStays);
	writer.NewPropertyWithValue("CollapseNeckWidth", TerrainCollapse::GetTuning().NeckWidth);
	writer.NewPropertyWithValue("CollapseThinNeckWidth", TerrainCollapse::GetTuning().ThinNeckWidth);
	writer.NewPropertyWithValue("CollapseOverhangLength", TerrainCollapse::GetTuning().OverhangLength);
	writer.NewPropertyWithValue("CollapseMaxPiece", TerrainCollapse::GetTuning().MaxPiecePixels);
	writer.NewPropertyWithValue("CollapseMinFitting", TerrainCollapse::GetTuning().MinFittingPixels);
	writer.NewPropertyWithValue("CollapseCheckMinForce", TerrainCollapse::GetTuning().CheckMinForce);
	writer.NewPropertyWithValue("CollapseBreakStrength", TerrainCollapse::GetTuning().BreakStrength);
	writer.NewPropertyWithValue("CollapseShatterSpeed", TerrainCollapse::GetTuning().ShatterSpeed);
	writer.NewPropertyWithValue("CollapseCrackSpeed", TerrainCollapse::GetTuning().CrackSpeed);
	writer.NewPropertyWithValue("CollapseCrumbleSpeed", TerrainCollapse::GetTuning().CrumbleSpeed);
	writer.NewPropertyWithValue("CollapseSplinterSpeed", TerrainCollapse::GetTuning().SplinterSpeed);
	writer.NewPropertyWithValue("CollapseLeafBreakEase", TerrainCollapse::GetTuning().LeafBreakEase);
	writer.NewPropertyWithValue("CollapseLeafLitter", TerrainCollapse::GetTuning().LeafLitter);
	writer.NewPropertyWithValue("CollapseBendSpeed", TerrainCollapse::GetTuning().BendSpeed);
	writer.NewPropertyWithValue("CollapseScuffStrength", TerrainCollapse::GetTuning().ScuffStrength);
	writer.NewPropertyWithValue("CollapseRestSeconds", TerrainCollapse::GetTuning().RestSeconds);
	writer.NewPropertyWithValue("CollapseCrushPixels", TerrainCollapse::GetTuning().CrushPixels);
	writer.NewPropertyWithValue("CollapseBlastPush", TerrainCollapse::GetTuning().BlastPush);
	writer.NewPropertyWithValue("CollapseBlastLoosens", TerrainCollapse::GetTuning().BlastLoosens);
	writer.NewPropertyWithValue("CollapseBlastLoosenStrength", TerrainCollapse::GetTuning().BlastLoosenStrength);
	writer.NewPropertyWithValue("CollapseBlastSnapsScenery", TerrainCollapse::GetTuning().BlastSnapsScenery);
	writer.NewPropertyWithValue("CollapseBlastPushesLateLook", TerrainCollapse::GetTuning().BlastPushesLateLook);
	writer.NewPropertyWithValue("CollapseAirWavePushesPieces", TerrainCollapse::GetTuning().AirWavePushesPieces);
	writer.NewPropertyWithValue("CollapseHitDamage", TerrainCollapse::GetTuning().HitDamage);
	writer.NewPropertyWithValue("CollapseHitMinSpeed", TerrainCollapse::GetTuning().HitMinSpeed);
	writer.NewPropertyWithValue("CollapseMaxDebrisPerUpdate", TerrainCollapse::GetTuning().MaxDebrisPerUpdate);
	writer.NewPropertyWithValue("CollapseDebrisKeptPercent", TerrainCollapse::GetTuning().DebrisKeptPercent);
	writer.NewPropertyWithValue("CollapseHitMinPixels", TerrainCollapse::GetTuning().HitMinPixels);
	writer.NewPropertyWithValue("CollapseHitMassCap", TerrainCollapse::GetTuning().HitMassCap);
	writer.NewPropertyWithValue("CollapseHitKnockback", TerrainCollapse::GetTuning().HitKnockback);
	writer.NewPropertyWithValue("SmokeBlocksSight", SmokeGrid::IsEnabled());
	writer.NewPropertyWithValue("AIThreatMemory", ThreatMemory::IsEnabled());
	writer.NewPropertyWithValue("Gas", GasGrid::IsEnabled());
	group(PresetGroup::Visual);
	writer.NewPropertyWithValue("GasShown", GasGrid::GetShown());
	group(PresetGroup::Gameplay);
	writer.NewPropertyWithValue("AirAndWind", AirPressure::IsOn());
	writer.NewPropertyWithValue("WindGusts", AirPressure::GetTuning().Gusts);
	writer.NewPropertyWithValue("WindShifts", AirPressure::GetTuning().Shifts);
	writer.NewPropertyWithValue("WindBreeze", AirPressure::GetTuning().Breeze);
	writer.NewPropertyWithValue("AirOverall", AirPressure::GetTuning().Overall);
	writer.NewPropertyWithValue("AirBlastStrength", AirPressure::GetTuning().BlastStrength);
	writer.NewPropertyWithValue("AirBlastReach", AirPressure::GetTuning().BlastReach);
	writer.NewPropertyWithValue("AirPushStrength", AirPressure::GetTuning().PushStrength);
	writer.NewPropertyWithValue("AirUnitPush", AirPressure::GetTuning().UnitPush);
	writer.NewPropertyWithValue("AirLiquidThrow", AirPressure::GetTuning().LiquidThrow);
	writer.NewPropertyWithValue("WindStrength", AirPressure::GetTuning().WindStrength);
	writer.NewPropertyWithValue("WindCarriesGas", AirPressure::GetTuning().WindGas);
	writer.NewPropertyWithValue("BlastWaves", AirPressure::IsEnabled());
	writer.NewPropertyWithValue("WindMovesSmoke", AirPressure::WindMovesSmoke());
	writer.NewPropertyWithValue("Magic", MagicEffects::IsOn());
	writer.NewPropertyWithValue("MagicPushStrength", MagicEffects::GetTuning().PushStrength);
	writer.NewPropertyWithValue("MagicUnitPush", MagicEffects::GetTuning().UnitPush);
	writer.NewPropertyWithValue("MagicAirGust", MagicEffects::GetTuning().AirGust);
	writer.NewPropertyWithValue("MagicSpellEnergy", MagicEffects::GetTuning().SpellEnergy);
	writer.NewPropertyWithValue("MagicEnergyRegen", MagicEffects::GetTuning().EnergyRegen);
	writer.NewPropertyWithValue("MagicAICasting", MagicEffects::GetTuning().AICasting);
	writer.NewPropertyWithValue("BurningUnits", ActorFire::IsEnabled());
	writer.NewPropertyWithValue("SwimmingAndDrowning", ActorWater::IsEnabled());
	group(PresetGroup::Visual);
	writer.NewPropertyWithValue("LightPropagationSteps", lighting.PropagationIterationsPerFrame);
	writer.NewPropertyWithValue("HitStopStrength", g_CameraMan.m_HitStopStrength);

	// The rest of the settings panel: the AI, the HUD and speech, the overlays, and the tool windows' own layout.
	group(PresetGroup::Gameplay);
	writer.NewPropertyWithValue("EnableMantling", m_EnableMantling);
	writer.NewPropertyWithValue("CreaturesClimbWalls", m_CreaturesClimbWalls);
	writer.NewPropertyWithValue("CreatureClimbSpeed", m_CreatureClimbSpeed);
	writer.NewPropertyWithValue("CreaturesFly", m_CreaturesFly);
	writer.NewPropertyWithValue("CreatureFlySpeed", m_CreatureFlySpeed);
	writer.NewPropertyWithValue("EnableStuckPush", m_EnableStuckPush);
	writer.NewPropertyWithValue("BodyGearSettlesAsScraps", m_BodyGearSettlesAsScraps);
	writer.NewPropertyWithValue("GibChunkPercent", m_GibChunks.CountPercent);
	writer.NewPropertyWithValue("MaxLooseGibChunks", m_GibChunks.MaxLoose);
	writer.NewPropertyWithValue("GibChunkSettleMS", m_GibChunks.SettleMS);
	writer.NewPropertyWithValue("GibChunkBounces", m_GibChunks.MaxBounces);
	writer.NewPropertyWithValue("GibChunkAtomSpacing", m_GibChunks.AtomSpacing);
	writer.NewPropertyWithValue("GibChunksSettleOnBodies", m_GibChunks.SettleOnBodies);
	writer.NewPropertyWithValue("AICombatAccuracy", m_AICombat.Accuracy);
	writer.NewPropertyWithValue("AICombatReaction", m_AICombat.Reaction);
	writer.NewPropertyWithValue("AICombatSpotting", m_AICombat.Spotting);
	writer.NewPropertyWithValue("AICombatSightRange", m_AICombat.SightRange);
	writer.NewPropertyWithValue("AICombatTargetMemory", m_AICombat.TargetMemory);
	writer.NewPropertyWithValue("AICombatGrenadeUse", m_AICombat.GrenadeUse);
	writer.NewPropertyWithValue("AICombatBurst", m_AICombat.Burst);
	writer.NewPropertyWithValue("FixLiquidTellsOnlyMoves", m_SlowdownFixes.LiquidTellsOnlyMoves);
	writer.NewPropertyWithValue("WreckSettleSeconds", m_SlowdownFixes.WreckSettleSeconds);
	writer.NewPropertyWithValue("FixDebrisRestsAcrossGaps", m_SlowdownFixes.DebrisRestsAcrossGaps);
	writer.NewPropertyWithValue("FixGasAreaShrinks", m_SlowdownFixes.GasAreaShrinks);
	writer.NewPropertyWithValue("FixLightingSplitsChanges", m_SlowdownFixes.LightingSplitsChanges);
	writer.NewPropertyWithValue("MaxLooseBits", m_SlowdownFixes.MaxLooseBits);
	{
		std::string kinds;
		for (int most: m_SlowdownFixes.MaxLooseBitsOf) {
			kinds += (kinds.empty() ? "" : " ") + std::to_string(most);
		}
		writer.NewPropertyWithValue("MaxLooseBitsOfKinds", kinds);
	}
	writer.NewPropertyWithValue("NoSceneWrap", m_NoSceneWrap);
	writer.NewPropertyWithValue("UnitToughness", Toughness(ToughnessKind::Units));
	writer.NewPropertyWithValue("VehicleToughness", Toughness(ToughnessKind::Vehicles));
	writer.NewPropertyWithValue("CraftToughness", Toughness(ToughnessKind::Craft));
	writer.NewPropertyWithValue("AnimalToughness", Toughness(ToughnessKind::Animals));
	writer.NewPropertyWithValue("CreatureToughness", Toughness(ToughnessKind::Creatures));
	writer.NewPropertyWithValue("AISuppression", m_AISuppression);
	writer.NewPropertyWithValue("AIDigWillingness", m_AIDigWillingness);
	writer.NewPropertyWithValue("AIDigsWithTracers", m_AIDigsWithTracers);
	writer.NewPropertyWithValue("AIThreatAvoidance", m_AIThreatAvoidance);
	writer.NewPropertyWithValue("AISpawnDiggerChance", m_AISpawnDiggerChance);
	writer.NewPropertyWithValue("AISpawnDiggerType", m_AISpawnDiggerType);
	writer.NewPropertyWithValue("BleedOutChance", m_BleedOutChance);
	writer.NewPropertyWithValue("WoundBleedTime", m_WoundBleedTime);
	writer.NewPropertyWithValue("ShipGroundClearance", m_ShipGroundClearance);
	writer.NewPropertyWithValue("ShipGraceZone", m_ShipGraceZone);
	writer.NewPropertyWithValue("CameraOverscroll", m_CameraOverscroll);
	writer.NewPropertyWithValue("WeaponTerrainDamage", m_WeaponTerrainDamage);
	writer.NewPropertyWithValue("WeaponUnitDamage", m_WeaponUnitDamage);
	writer.NewPropertyWithValue("WeaponUnitDamageGibs", m_WeaponUnitDamageGibs);
	writer.NewPropertyWithValue("AIRecklessness", m_AIRecklessness);
	writer.NewPropertyWithValue("StrictClimbLimits", m_StrictClimbLimits);
	writer.NewPropertyWithValue("MeasuredBodySize", m_MeasuredBodySize);
	writer.NewPropertyWithValue("AILadderGiveUpSeconds", m_AILadderGiveUpSeconds);
	writer.NewPropertyWithValue("CarriedWeightMultiplier", m_CarriedWeightMultiplier);
	writer.NewPropertyWithValue("LegStrength", m_LegStrength);
	writer.NewPropertyWithValue("JetpackWeightCompensation", m_JetpackWeightCompensation);
	writer.NewPropertyWithValue("UnitWaterBehaviour", m_UnitWaterBehaviour);
	writer.NewPropertyWithValue("UnitsStepOverObjects", m_UnitsStepOverObjects);
	writer.NewPropertyWithValue("UnitsCrossVehicles", m_UnitsCrossVehicles);
	writer.NewPropertyWithValue("UnitsPushObjects", m_UnitsPushObjects);
	writer.NewPropertyWithValue("ObjectPushStrength", m_ObjectPushStrength);
	writer.NewPropertyWithValue("SensesPassPlants", m_SensesPassPlants);
	writer.NewPropertyWithValue("SensesPassRungs", m_SensesPassRungs);
	writer.NewPropertyWithValue("SensesPassSpecks", m_SensesPassSpecks);
	writer.NewPropertyWithValue("OverloadSlowdown", m_OverloadSlowdown);
	writer.NewPropertyWithValue("JetpackFuelMultiplier", m_JetpackFuelMultiplier);
	writer.NewPropertyWithValue("InfiniteJetpackFuel", m_InfiniteJetpackFuel);
	writer.NewPropertyWithValue("AILadderTeamStalls", m_AILadderTeamStalls);
	writer.NewPropertyWithValue("AIGrabsLaddersOnPurpose", m_AIGrabsLaddersOnPurpose);
	writer.NewPropertyWithValue("AIGrabsLaddersMidJet", m_AIGrabsLaddersMidJet);
	writer.NewPropertyWithValue("AIMantlesOnPurpose", m_AIMantlesOnPurpose);
	writer.NewPropertyWithValue("AICatchesLedgesMidAir", m_AICatchesLedgesMidAir);
	writer.NewPropertyWithValue("RiskyOrdersFlyPastLadders", m_RiskyOrdersFlyPastLadders);
	writer.NewPropertyWithValue("AISteadyBeforeJet", m_AISteadyBeforeJet);
	writer.NewPropertyWithValue("AIWaitForFuel", m_AIWaitForFuel);
	writer.NewPropertyWithValue("AIWaitsAtDoors", m_AIWaitsAtDoors);
	writer.NewPropertyWithValue("AIEnemyBuysReinforcements", m_AIEnemyBuysReinforcements);
	writer.NewPropertyWithValue("AIEnemyUsesPowers", m_AIEnemyUsesPowers);
	writer.NewPropertyWithValue("AISquadFollowsYou", m_AISquadFollowsYou);
	writer.NewPropertyWithValue("AIUsesVehicles", m_AIUsesVehicles);
	writer.NewPropertyWithValue("AIRidesShips", m_AIRidesShips);
	writer.NewPropertyWithValue("AIShipRideNoRoute", m_AIShipRideNoRoute);
	writer.NewPropertyWithValue("AIShipRideLongWalk", m_AIShipRideLongWalk);
	writer.NewPropertyWithValue("AIShipRideLongWalkDistance", m_AIShipRideLongWalkDistance);
	writer.NewPropertyWithValue("AIShipRideRange", m_AIShipRideRange);
	writer.NewPropertyWithValue("AIShipRideWait", m_AIShipRideWait);
	writer.NewPropertyWithValue("AIShipRideDropRing", m_AIShipRideDropRing);
	writer.NewPropertyWithValue("AIVehiclesWaitForRiders", m_AIVehiclesWaitForRiders);
	writer.NewPropertyWithValue("BoardFromBelow", m_BoardFromBelow);
	writer.NewPropertyWithValue("BoardFromBelowReach", m_BoardFromBelowReach);
	writer.NewPropertyWithValue("AICoverDigs", m_AICoverDigs);
	writer.NewPropertyWithValue("AICoverFallsBack", m_AICoverFallsBack);
	writer.NewPropertyWithValue("AICoverLiesFlat", m_AICoverLiesFlat);
	writer.NewPropertyWithValue("AIBreaksWhenShaken", m_AIBreaksWhenShaken);
	writer.NewPropertyWithValue("AISharedSpotting", m_AISharedSpotting);
	writer.NewPropertyWithValue("AISharedSpottingHeadStart", m_AISharedSpottingHeadStart);
	writer.NewPropertyWithValue("AIScavengesBetterWeapons", m_AIScavengesBetterWeapons);
	writer.NewPropertyWithValue("AIMaxUnitsPerSpot", m_AIMaxUnitsPerSpot);
	writer.NewPropertyWithValue("AIUnitsCallLifts", m_AIUnitsCallLifts);
	writer.NewPropertyWithValue("DeadCrewFallOut", m_DeadCrewFallOut);
	writer.NewPropertyWithValue("PassengersFireFromVehicles", m_PassengersFireFromVehicles);
	writer.NewPropertyWithValue("PathLineWeight", m_PathTuning.LineWeight);
	writer.NewPropertyWithValue("PathLineWeightByRisk", m_PathTuning.LineWeightByRisk);
	writer.NewPropertyWithValue("PathFullRiskLineWeight", m_PathTuning.FullRiskLineWeight);
	writer.NewPropertyWithValue("PathUpCost", m_PathTuning.UpCost);
	writer.NewPropertyWithValue("PathFlightCost", m_PathTuning.FlightCostScale);
	writer.NewPropertyWithValue("PathJumpHeightCost", m_PathTuning.JumpHeightCost);
	writer.NewPropertyWithValue("PathLadderCost", m_PathTuning.LadderCostScale);
	writer.NewPropertyWithValue("PathFallCost", m_PathTuning.FallCostPerNode);
	writer.NewPropertyWithValue("PathRouteVariety", m_PathTuning.RouteVariety);
	writer.NewPropertyWithValue("PathFailedSpotCost", m_PathTuning.FailedSpotCost);
	writer.NewPropertyWithValue("PathJetOverClimb", m_PathTuning.JetOverClimb);
	writer.NewPropertyWithValue("PathRefreshSeconds", m_PathTuning.RefreshSeconds);
	writer.NewPropertyWithValue("PathNoSightSeconds", m_PathTuning.NoSightSeconds);
	writer.NewPropertyWithValue("PathStuckRemedySeconds", m_PathTuning.StuckRemedySeconds);
	writer.NewPropertyWithValue("PathStuckRepathSeconds", m_PathTuning.StuckRepathSeconds);
	writer.NewPropertyWithValue("PathAirCheckSeconds", m_PathTuning.AirCheckSeconds);
	writer.NewPropertyWithValue("PathReachScale", m_PathTuning.ReachScale);
	writer.NewPropertyWithValue("PathNoRouteRetrySeconds", m_PathTuning.NoRouteRetrySeconds);
	writer.NewPropertyWithValue("DirectCaution", m_DirectOrders.Caution);
	writer.NewPropertyWithValue("DirectFlightCost", m_DirectOrders.FlightCostScale);
	writer.NewPropertyWithValue("DirectFuelShare", m_DirectOrders.FuelShare);
	writer.NewPropertyWithValue("DirectRecklessSteadyCut", m_DirectOrders.RecklessSteadyCut);
	writer.NewPropertyWithValue("DirectFlightSpeed", m_DirectOrders.FlightSpeed);
	writer.NewPropertyWithValue("DirectTouchdown", m_DirectOrders.Touchdown);
	writer.NewPropertyWithValue("DirectSafeLandings", m_DirectOrders.SafeLandings);
	writer.NewPropertyWithValue("DirectDigCost", m_DirectOrders.DigCostScale);
	writer.NewPropertyWithValue("DirectLineWeight", m_DirectOrders.LineWeight);
	writer.NewPropertyWithValue("DirectMoveLineWeight", m_DirectOrders.MoveLineWeight);
	writer.NewPropertyWithValue("DirectIgnoreEnemies", m_DirectOrders.IgnoreEnemies);
	writer.NewPropertyWithValue("DirectKeepPace", m_DirectOrders.KeepPace);
	writer.NewPropertyWithValue("DirectFullRiskDrag", m_DirectOrders.RiskDragPixels);
	writer.NewPropertyWithValue("DirectStraightFlightRisk", m_DirectOrders.StraightFlightRisk);
	writer.NewPropertyWithValue("DirectSkyRouteRisk", m_DirectOrders.SkyRouteRisk);
	writer.NewPropertyWithValue("DirectDives", m_DirectOrders.Dives);
	writer.NewPropertyWithValue("DigAvoidCaveIns", m_DigSafety.AvoidCaveIns);
	writer.NewPropertyWithValue("DigBackOff", m_DigSafety.BackOff);
	writer.NewPropertyWithValue("DigToAvoidCaveIns", m_DigSafety.DigToOrders);
	writer.NewPropertyWithValue("DigToPreviewWarns", m_DigSafety.PreviewWarns);
	writer.NewPropertyWithValue("AIDodgesFallingGround", m_AIDodgesFallingGround);
	writer.NewPropertyWithValue("DirectDiveRisk", m_DirectOrders.DiveRisk);
	writer.NewPropertyWithValue("DirectDiveChance", m_DirectOrders.DiveChance);
	writer.NewPropertyWithValue("CautiousCaution", m_DirectOrders.CautiousCaution);
	writer.NewPropertyWithValue("CautiousFlightCost", m_DirectOrders.CautiousFlightCostScale);
	writer.NewPropertyWithValue("CautiousFuelShare", m_DirectOrders.CautiousFuelShare);
	writer.NewPropertyWithValue("CautiousFlightSpeed", m_DirectOrders.CautiousFlightSpeed);
	writer.NewPropertyWithValue("CautiousTouchdown", m_DirectOrders.CautiousTouchdown);
	writer.NewPropertyWithValue("CautiousEnemyAvoidance", m_DirectOrders.CautiousEnemyAvoidance);
	writer.NewPropertyWithValue("RiskFollowsSteadying", m_DirectOrders.Follows.Steadying);
	writer.NewPropertyWithValue("RiskFollowsFuelWait", m_DirectOrders.Follows.FuelWait);
	writer.NewPropertyWithValue("RiskFollowsRouteCaution", m_DirectOrders.Follows.RouteCaution);
	writer.NewPropertyWithValue("RiskFollowsJetPreference", m_DirectOrders.Follows.JetPreference);
	writer.NewPropertyWithValue("RiskFollowsFlightSpeed", m_DirectOrders.Follows.FlightSpeed);
	writer.NewPropertyWithValue("RiskFollowsLanding", m_DirectOrders.Follows.Landing);
	writer.NewPropertyWithValue("RiskFollowsStraightFlight", m_DirectOrders.Follows.StraightFlight);
	writer.NewPropertyWithValue("RiskFollowsSkyRoute", m_DirectOrders.Follows.SkyRoute);
	writer.NewPropertyWithValue("RiskFollowsEnemyAvoidance", m_DirectOrders.Follows.EnemyAvoidance);
	writer.NewPropertyWithValue("RiskFollowsRunning", m_DirectOrders.Follows.Running);
	writer.NewPropertyWithValue("RiskFollowsVehicles", m_DirectOrders.Follows.Vehicles);
	group(PresetGroup::Visual);
	writer.NewPropertyWithValue("ShowFPSAndVersion", m_ShowFPSAndVersion);
	writer.NewPropertyWithValue("ShowNotices", m_ShowNotices);
	writer.NewPropertyWithValue("ModernHUD", ModernHUD::IsEnabled());
	writer.NewPropertyWithValue("UILight", ToolLight::Strength);
	writer.NewPropertyWithValue("UILightOwn", ToolLight::Mode == ToolLight::Source::OwnLight);
	writer.NewPropertyWithValue("UILightAngle", ToolLight::OwnAngle);
	writer.NewPropertyWithValue("UILightHeight", ToolLight::OwnHeight);
	writer.NewPropertyWithValue("UILightBrightness", ToolLight::OwnBrightness);
	writer.NewPropertyWithValue("UILightRed", ToolLight::OwnColor[0]);
	writer.NewPropertyWithValue("UILightGreen", ToolLight::OwnColor[1]);
	writer.NewPropertyWithValue("UILightBlue", ToolLight::OwnColor[2]);
	writer.NewPropertyWithValue("UINearbyLights", ToolLight::NearbyLights);
	writer.NewPropertyWithValue("ShowUnitTags", m_ShowUnitTags);
	writer.NewPropertyWithValue("ShowCPUAimReticles", m_ShowCPUAimReticles);
	writer.NewPropertyWithValue("FreeAimThroughTerrain", m_FreeAimThroughTerrain);
	writer.NewPropertyWithValue("ShowAlarmExclamation", m_ShowAlarmExclamation);
	writer.NewPropertyWithValue("ClassicPieWheel", m_ClassicPieWheel);
	writer.NewPropertyWithValue("UnitMenuClickToOpen", m_UnitMenuClickToOpen);
	writer.NewPropertyWithValue("UnitMenuTimeScale", m_UnitMenuTimeScale);
	writer.NewPropertyWithValue("PauseAIKey", m_PauseAIKey);
	writer.NewPropertyWithValue("SmoothHUDText", TextOverlay::IsEnabled());
	writer.NewPropertyWithValue("FrameCap", g_WindowMan.GetFrameCap());
	writer.NewPropertyWithValue("UnitSpeech", UnitSpeech::IsEnabled());
	writer.NewPropertyWithValue("UnitSpeechChance", UnitSpeech::GetChance());
	writer.NewPropertyWithValue("UnitSpeechEnemies", UnitSpeech::ShowsEnemies());
	writer.NewPropertyWithValue("UnitSpeechBubbles", UnitSpeech::ShowsBubbles());
	writer.NewPropertyWithValue("UnitSpeechVoices", UnitSpeechVoice::IsEnabled());
	writer.NewPropertyWithValue("UnitSpeechVoiceVolume", UnitSpeechVoice::GetVolume());
	writer.NewPropertyWithValue("UnitSpeechVoiceRadio", UnitSpeechVoice::GetRadio());
	writer.NewPropertyWithValue("UnitSpeechVoiceEnemies", UnitSpeechVoice::HearsEnemies());
	writer.NewPropertyWithValue("UnitSpeechVoiceWords", UnitSpeechVoice::SpeaksWords());
	for (int team = 0; team < 4; ++team) {
		writer.NewPropertyWithValue("UnitSpeechTones" + std::to_string(team + 1), UnitSpeech::GetTeamTonesText(team));
	}
	writer.NewPropertyWithValue("UnitSpeechToneMix", UnitSpeech::GetToneWeightsText());
	if (forPreset) {
		// The settings file only lists what is off, over everything on; a preset loads over what is set now, so it says what is on too.
		for (const UnitSpeech::Trigger& trigger: UnitSpeech::GetTriggers()) {
			if (UnitSpeech::IsTriggerOn(trigger.Key)) {
				writer.NewPropertyWithValue("UnitSpeechOn", trigger.Key);
			}
		}
	}
	for (const std::string& off: UnitSpeech::GetTriggersOff()) {
		writer.NewPropertyWithValue("UnitSpeechOff", off);
	}
	group(PresetGroup::Session);
	writer.NewPropertyWithValue("NavDebugOverlay", m_NavDebugOverlay);
	writer.NewPropertyWithValue("DebugTeam", m_DebugTeam);
	writer.NewPropertyWithValue("UnitInspector", m_UnitInspector);
	writer.NewPropertyWithValue("CombatOverlay", m_CombatOverlay);
	writer.NewPropertyWithValue("ShowRecentSolves", m_ShowRecentSolves);
	writer.NewPropertyWithValue("ShowSquadLinks", m_ShowSquadLinks);
	writer.NewPropertyWithValue("ShowOrderLabels", m_ShowOrderLabels);
	writer.NewPropertyWithValue("ShowTerrainUpdates", m_ShowTerrainUpdates);
	writer.NewPropertyWithValue("DebugChannels", m_DebugChannels);
	writer.NewPropertyWithValue("TraceAllUnits", m_TraceAllUnits);
	writer.NewPropertyWithValue("ShowLightSources", m_ShowLightSources);
	writer.NewPropertyWithValue("LightsBySource", m_LightsBySource);
	writer.NewPropertyWithValue("ShowSunDirection", m_ShowSunDirection);
	writer.NewPropertyWithValue("WorldSimOverlay", m_WorldSimOverlay);
	writer.NewPropertyWithValue("SandboxOrdersOverlay", m_SandboxOrdersOverlay);
	writer.NewPropertyWithValue("SandboxSimState", m_SandboxSimState);
	writer.NewPropertyWithValue("SandboxBarRight", m_SandboxBarRight);
	writer.NewPropertyWithValue("SandboxEffects", m_SandboxEffects);
	writer.NewPropertyWithValue("SandboxGas", m_SandboxGas);
	writer.NewPropertyWithValue("SandboxAir", m_SandboxAir);
	writer.NewPropertyWithValue("SandboxSelectionCamera", m_SandboxSelectionCamera);
	writer.NewPropertyWithValue("SandboxPaintAudit", m_SandboxPaintAudit);
	writer.NewPropertyWithValue("SandboxAutoBattle", m_SandboxAutoBattle);
	writer.NewPropertyWithValue("SandboxCharacterState", m_SandboxCharacterState);
	writer.NewPropertyWithValue("SandboxSpotReach", m_SandboxSpotReach);
	writer.NewPropertyWithValue("SandboxStrokeLog", m_SandboxStrokeLog);
	writer.NewPropertyWithValue("DockPanels", g_DebugMan.m_DockPanels);
	writer.NewPropertyWithValue("PanelsOverlay", g_DebugMan.m_PanelsOverlay);
	writer.NewPropertyWithValue("PanelWidthPercent", g_DebugMan.m_PanelWidth);
	writer.NewPropertyWithValue("RightPanelWidthPercent", g_DebugMan.m_PanelWidthRight);
	writer.NewPropertyWithValue("SandboxPanelPlacement", g_DebugMan.m_SandboxPlacement);
	writer.NewPropertyWithValue("ToolScale", g_DebugMan.m_ToolScale);
	writer.NewPropertyWithValue("PixelToolFont", g_DebugMan.m_PixelFont);
	writer.NewPropertyWithValue("DefaultBackground", m_DefaultBackground);

	if (forPreset) {
		// The game's own settings from the Settings menu, so a preset loaded at start brings them back too. Settings.ini keeps them in its own sections.
		group(PresetGroup::Gameplay);
		writer.NewPropertyWithValue("ShowForeignItems", m_ShowForeignItems);
		writer.NewPropertyWithValue("MaxUnheldItems", g_MovableMan.m_MaxDroppedItems);
		writer.NewPropertyWithValue("EndlessMetaGameMode", m_EndlessMetaGameMode);
		writer.NewPropertyWithValue("EnableCrabBombs", m_EnableCrabBombs);
		writer.NewPropertyWithValue("CrabBombThreshold", m_CrabBombThreshold);
		writer.NewPropertyWithValue("AutomaticGoldDeposit", m_AutomaticGoldDeposit);
		group(PresetGroup::Visual);
		writer.NewPropertyWithValue("FlashOnBrainDamage", m_FlashOnBrainDamage);
		writer.NewPropertyWithValue("BlipOnRevealUnseen", m_BlipOnRevealUnseen);
		writer.NewPropertyWithValue("UnheldItemsHUDDisplayRange", m_UnheldItemsHUDDisplayRange);
		writer.NewPropertyWithValue("AlwaysDisplayUnheldItemsInStrategicMode", m_AlwaysDisplayUnheldItemsInStrategicMode);
		writer.NewPropertyWithValue("SubPieMenuHoverOpenDelay", m_SubPieMenuHoverOpenDelay);
		writer.NewPropertyWithValue("ShowEnemyHUD", m_ShowEnemyHUD);
		writer.NewPropertyWithValue("SmartBuyMenuNavigation", m_EnableSmartBuyMenuNavigation);
		writer.NewPropertyWithValue("SandboxGroupBadges", m_SandboxGroupBadges);
		writer.NewPropertyWithValue("CommanderBarKey", m_CommanderBarKey);
		writer.NewPropertyWithValue("SandboxOrderGlyphs", m_SandboxOrderGlyphs);
		writer.NewPropertyWithValue("SandboxSpawnStats", m_SandboxSpawnStats);
		writer.NewPropertyWithValue("SandboxSpawnSort", m_SandboxSpawnSort);
		writer.NewPropertyWithValue("SandboxAttackPings", m_SandboxAttackPings);
		writer.NewPropertyWithValue("SandboxNoRouteMarkers", m_SandboxNoRouteMarkers);
		writer.NewPropertyWithValue("SandboxMinimap", m_SandboxMinimap);
		writer.NewPropertyWithValue("BattleAlerts", m_BattleAlerts);
		writer.NewPropertyWithValue("BattleAlertLines", m_BattleAlertLines);
		writer.NewPropertyWithValue("BattleAlertSeconds", m_BattleAlertSeconds);
		writer.NewPropertyWithValue("BattleKillFeed", m_BattleKillFeed);
		writer.NewPropertyWithValue("BattleKillLines", m_BattleKillLines);
		writer.NewPropertyWithValue("BattleKillSeconds", m_BattleKillSeconds);
		writer.NewPropertyWithValue("BattleAlertSounds", m_BattleAlertSounds);
		writer.NewPropertyWithValue("BattleAlertVolume", m_BattleAlertVolume);
		writer.NewPropertyWithValue("BattleAskFirst", m_BattleAskFirst);
		writer.NewPropertyWithValue("BattleShowInfo", m_BattleShowInfo);
		writer.NewPropertyWithValue("BattleShowObjectives", m_BattleShowObjectives);
		writer.NewPropertyWithValue("BattleObjectiveLook", m_BattleObjectiveLook);
		writer.NewPropertyWithValue("BattleShowSpawnZones", m_BattleShowSpawnZones);
		writer.NewPropertyWithValue("BattleShowModeZones", m_BattleShowModeZones);
		writer.NewPropertyWithValue("ArmyOverview", m_ArmyOverview);
		writer.NewPropertyWithValue("ScreenShakeStrength", g_CameraMan.m_ScreenShakeStrength);

		// The panel's settings for the moment: kept in a preset, but not in Settings.ini, so the game doesn't start sped up, frozen, with the AI paused or in a debug view.
	group(PresetGroup::Session);
		writer.NewPropertyWithValue("GameSpeed", g_TimerMan.GetTimeScale());
		writer.NewPropertyWithValue("PauseAI", Controller::IsAIPaused());
		writer.NewPropertyWithValue("CameraZoom", g_FrameMan.GetCameraZoom());
		writer.NewPropertyWithValue("LightingDebugView", lighting.DebugView);
		writer.NewPropertyWithValue("FreezeSimulation", g_DebugMan.m_FreezeSim);
		writer.NewPropertyWithValue("ShowPerformanceStats", g_DebugMan.m_ShowPerformanceMan);
		writer.NewPropertyWithValue("ShowActorDebugDrawing", g_DebugMan.m_ShowActorDebugGui);
		writer.NewPropertyWithValue("DrawCameraBounds", g_DebugMan.m_DrawCameraBounds);
		writer.NewPropertyWithValue("DrawSpriteBounds", g_DebugMan.m_DrawSpriteBounds);
		writer.NewPropertyWithValue("ImGuiDemoWindow", g_DebugMan.m_ImGuiDemoWindow);
		writer.NewPropertyWithValue("FreeCam", g_DebugMan.m_EnableFreeCam);
		writer.NewPropertyWithValue("FreeCamZoom", g_DebugMan.m_FreeCamZoom);
	}
	// The sandbox's You tab (your character), how its Spawn tab puts vehicles down and how its window was left: in Settings.ini and in a preset alike.
	writer.NewPropertyWithValue("SandboxCharacter", Sandbox::GetCharacterSetup());
	writer.NewPropertyWithValue("SandboxVehicles", Sandbox::GetVehicleSetup());
	writer.NewPropertyWithValue("SandboxUnits", Sandbox::GetUnitSetup());
	// How the sandbox's window was left: its tab, the tools last taken, the brush and the catalogue's filters.
	writer.NewPropertyWithValue("SandboxPanel", Sandbox::GetPanelSetup());
}

namespace {
	/// Makes a name safe to be a file's: letters, digits, spaces, dashes and underscores.
	std::string PresetFileName(const std::string& name) {
		std::string safe;
		for (char letter: name) {
			if (std::isalnum(static_cast<unsigned char>(letter)) || letter == ' ' || letter == '-' || letter == '_') {
				safe += letter;
			}
		}
		while (!safe.empty() && safe.back() == ' ') {
			safe.pop_back();
		}
		while (!safe.empty() && safe.front() == ' ') {
			safe.erase(safe.begin());
		}
		return safe.substr(0, 48);
	}

	std::string PresetFolder() { return System::GetUserdataDirectory() + "Presets/"; }
	std::string BuiltInPresetFolder() { return System::GetDataDirectory() + "Presets/"; } //!< Presets that ship with the game. The player's own of the same name come first.
} // namespace

const char* SettingsMan::PresetGroupName(PresetGroup group) {
	switch (group) {
		case PresetGroup::Gameplay:
			return "Gameplay & physics";
		case PresetGroup::Visual:
			return "Visual & lighting";
		case PresetGroup::Session:
			return "For the moment";
		default:
			return "Everything";
	}
}

namespace {
	/// Where a kind of preset is kept: the full ones in the folder itself, a group's in a folder of its own inside it.
	std::string PresetSubfolder(SettingsMan::PresetGroup group) {
		switch (group) {
			case SettingsMan::PresetGroup::Gameplay:
				return "Gameplay/";
			case SettingsMan::PresetGroup::Visual:
				return "Visual/";
			default:
				return "";
		}
	}

	/// Gets the name of the setting a line of a preset or settings file sets, or nothing if it sets none (the class's name at the top, a comment, a blank line).
	std::string LineKey(const std::string& line) {
		size_t start = line.find_first_not_of(" \t");
		size_t equals = line.find(" = ");
		if (start == std::string::npos || equals == std::string::npos || equals <= start) {
			return "";
		}
		std::string key = line.substr(start, equals - start);
		return std::all_of(key.begin(), key.end(), [](char letter) { return std::isalnum(static_cast<unsigned char>(letter)) || letter == '_'; }) ? key : "";
	}

	/// Keeps from a preset's text only the lines setting what is in one group (and what is in every group), and the class's name it starts with.
	std::string KeepGroup(const std::string& text, SettingsMan::PresetGroup part, const std::unordered_map<std::string, SettingsMan::PresetGroup>& groups) {
		std::istringstream lines(text);
		std::string kept;
		std::string line;
		bool first = true;
		while (std::getline(lines, line)) {
			std::string key = LineKey(line);
			bool keep = first;
			if (!key.empty()) {
				auto found = groups.find(key);
				keep = found != groups.end() && (found->second == part || found->second == SettingsMan::PresetGroup::Everything);
			}
			if (keep) {
				kept += line + "\n";
			}
			first = first && line.find_first_not_of(" \t\r") == std::string::npos;
		}
		return kept;
	}
} // namespace

std::unordered_map<std::string, SettingsMan::PresetGroup> SettingsMan::TunableGroups() const {
	auto stream = std::make_unique<std::ostringstream>();
	std::ostringstream* text = stream.get();
	Writer writer(std::move(stream));
	s_MarkingGroups = true;
	SaveTunables(writer, g_PostProcessMan.GetLightingSettings(), true);
	s_MarkingGroups = false;

	std::unordered_map<std::string, PresetGroup> groups;
	PresetGroup group = PresetGroup::Everything;
	std::istringstream lines(text->str());
	std::string line;
	while (std::getline(lines, line)) {
		std::string key = LineKey(line);
		if (key == c_GroupMarker) {
			group = static_cast<PresetGroup>(std::stoi(line.substr(line.find(" = ") + 3)));
		} else if (!key.empty()) {
			groups.emplace(key, group);
		}
	}
	// The lines written only some of the time, in the group of a line always written beside them: not written as things are now, they'd be
	// missing here, and a preset's lines for them left out when one group's half of it is loaded.
	for (const auto& [sometimes, beside]: {std::pair<const char*, const char*>{"WeatherName", "WeatherType"}, {"UnitSpeechOn", "UnitSpeechToneMix"}, {"UnitSpeechOff", "UnitSpeechToneMix"}}) {
		if (auto found = groups.find(beside); found != groups.end()) {
			groups.emplace(sometimes, found->second);
		}
	}
	return groups;
}

std::string SettingsMan::SavePreset(const std::string& name, PresetGroup group) const {
	std::string safe = PresetFileName(name);
	if (safe.empty()) {
		return "";
	}
	std::string folder = PresetFolder() + PresetSubfolder(group);
	std::error_code error;
	std::filesystem::create_directories(folder, error);
	if (group != PresetGroup::Everything) {
		// Everything written as for a full preset, then only this group's lines kept.
		auto stream = std::make_unique<std::ostringstream>();
		std::ostringstream* text = stream.get();
		Writer all(std::move(stream));
		all.ObjectStart(GetClassName());
		SaveTunables(all, g_PostProcessMan.GetLightingSettings(), true);
		all.ObjectEnd();
		std::ofstream file(folder + safe + ".ini");
		if (!file) {
			return "";
		}
		file << KeepGroup(text->str(), group, TunableGroups());
		return file.good() ? safe : "";
	}
	Writer writer(folder + safe + ".ini");
	if (!writer.WriterOK()) {
		return "";
	}
	writer.ObjectStart(GetClassName());
	// As they are on screen now, not as the player's own behind a scene that sets its time and weather.
	SaveTunables(writer, g_PostProcessMan.GetLightingSettings(), true);
	writer.ObjectEnd();
	writer.EndWrite();
	return safe;
}

bool SettingsMan::LoadPreset(const std::string& name, PresetGroup from, PresetGroup part) {
	std::string path = PresetFolder() + PresetSubfolder(from) + PresetFileName(name) + ".ini";
	if (!std::filesystem::exists(path)) {
		path = BuiltInPresetFolder() + PresetSubfolder(from) + PresetFileName(name) + ".ini";
		if (!std::filesystem::exists(path)) {
			return false;
		}
	}
	std::ifstream file(path);
	if (!file) {
		return false;
	}
	std::ostringstream text;
	text << file.rdbuf();
	// The whole file, or only one group's half of a full preset: the other lines are left out before it is read.
	std::string kept = part == PresetGroup::Everything || part == from ? text.str() : KeepGroup(text.str(), part, TunableGroups());
	// The same reading as the settings file gets, of a file that holds only the tunable settings.
	Reader reader(std::make_unique<std::istringstream>(kept), path, false, nullptr, true, true);
	if (!reader.ReaderOK() || CreateSerializable(reader, true, false, false) < 0) {
		return false;
	}
	// The weather the preset sets is the player's own from now on, kept from map to map, not only until the next one loads. Only what it
	// sets: a preset with no weather lines (a Gameplay one, say) leaves the player's weather be, rather than taking the map's showing now.
	bool setsType = false;
	bool setsIntensity = false;
	bool setsOverride = false;
	std::istringstream lines(kept);
	for (std::string line; std::getline(lines, line);) {
		std::string key = LineKey(line);
		setsType = setsType || key == "WeatherType" || key == "WeatherName";
		setsIntensity = setsIntensity || key == "WeatherIntensity";
		setsOverride = setsOverride || key == "OverrideMapWeather";
	}
	if (setsType || setsIntensity) {
		g_PostProcessMan.AdoptWeatherAsPlayers(setsType, setsIntensity);
	}
	if (setsType || setsIntensity || setsOverride) {
		// And the weather it means goes up now, not when the next map loads: the player's own, or the map's when the preset leaves the map
		// its weather (or a game's or a saved game's pick, which still win).
		g_PostProcessMan.SetOverrideMapWeather(g_PostProcessMan.GetLightingSettings().OverrideMapWeather);
	}
	return true;
}

void SettingsMan::SetStartupPreset(const std::string& name, PresetGroup group) {
	m_StartupPresets[StartupSlot(group)] = PresetFileName(name);
}

bool SettingsMan::LoadStartupPreset() {
	bool loadedAny = false;
	s_ReadingStartupPreset = true;
	for (PresetGroup group: {PresetGroup::Everything, PresetGroup::Gameplay, PresetGroup::Visual}) {
		const std::string& name = GetStartupPreset(group);
		if (name.empty()) {
			continue;
		}
		if (LoadPreset(name, group)) {
			loadedAny = true;
		} else {
			std::string folder = PresetSubfolder(group);
			g_ConsoleMan.PrintString("WARNING: The preset to load at start, \"" + name + "\", isn't in Userdata/Presets/" + folder + " or Data/Presets/" + folder + ". It is left out.");
		}
	}
	s_ReadingStartupPreset = false;
	return loadedAny;
}

bool SettingsMan::DeletePreset(const std::string& name, PresetGroup group) const {
	std::error_code error;
	return std::filesystem::remove(PresetFolder() + PresetSubfolder(group) + PresetFileName(name) + ".ini", error);
}

std::vector<std::string> SettingsMan::ListPresets(PresetGroup group) const {
	std::vector<std::string> names;
	std::error_code error;
	for (const std::string& folder: {PresetFolder() + PresetSubfolder(group), BuiltInPresetFolder() + PresetSubfolder(group)}) {
		for (const auto& entry: std::filesystem::directory_iterator(folder, error)) {
			if (entry.is_regular_file() && entry.path().extension() == ".ini") {
				names.push_back(entry.path().stem().string());
			}
		}
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	return names;
}

namespace {
	std::string FilterPresetFolder() { return System::GetUserdataDirectory() + "FilterPresets/"; }
} // namespace

std::string SettingsMan::SaveFilterPreset(const std::string& name, const LightingSettings::RetroLayers& layers) const {
	std::string safe = PresetFileName(name);
	if (safe.empty()) {
		return "";
	}
	std::error_code error;
	std::filesystem::create_directories(FilterPresetFolder(), error);
	std::ofstream file(FilterPresetFolder() + safe + ".ini");
	if (!file) {
		return "";
	}
	// One "Field = value" a line, as the settings file has them without its prefix, read back by LoadFilterPreset.
	file.imbue(std::locale::classic());
	file << "// A screen style filter preset (Camera & image > Screen style), saved by the settings panel.\n";
	file << "Look = " << layers.Look << "\n";
	file << "Palette = " << layers.Palette << "\n";
	file << "LCDPalette = " << layers.LCDPalette << "\n";
	file << "InkEdges = " << (layers.InkEdges ? 1 : 0) << "\n";
	file << "GlitchOnBlasts = " << (layers.GlitchOnBlasts ? 1 : 0) << "\n";
	file << "PaperBackgroundOnly = " << (layers.PaperBackgroundOnly ? 1 : 0) << "\n";
	for (const auto& [field, member]: c_RetroFloatFields) {
		file << field << " = " << layers.*member << "\n";
	}
	return file ? safe : "";
}

bool SettingsMan::LoadFilterPreset(const std::string& name, LightingSettings::RetroLayers& layers) const {
	std::ifstream file(FilterPresetFolder() + PresetFileName(name) + ".ini");
	if (!file) {
		return false;
	}
	// Fields missing from the file (one saved before they existed) keep how the game comes.
	LightingSettings::RetroLayers loaded;
	std::string line;
	while (std::getline(file, line)) {
		size_t equals = line.find('=');
		if (line.rfind("//", 0) == 0 || equals == std::string::npos) {
			continue;
		}
		auto trim = [](std::string text) {
			size_t first = text.find_first_not_of(" \t\r");
			size_t last = text.find_last_not_of(" \t\r");
			return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
		};
		try {
			ReadRetroField(loaded, trim(line.substr(0, equals)), trim(line.substr(equals + 1)));
		} catch (const std::logic_error&) {
			g_ConsoleMan.PrintString("ERROR: The filter preset \"" + name + "\" has a value this version can't read: " + line);
		}
	}
	layers = loaded;
	return true;
}

bool SettingsMan::DeleteFilterPreset(const std::string& name) const {
	std::error_code error;
	return std::filesystem::remove(FilterPresetFolder() + PresetFileName(name) + ".ini", error);
}

std::vector<std::string> SettingsMan::ListFilterPresets() const {
	std::vector<std::string> names;
	std::error_code error;
	for (const auto& entry: std::filesystem::directory_iterator(FilterPresetFolder(), error)) {
		if (entry.is_regular_file() && entry.path().extension() == ".ini") {
			names.push_back(entry.path().stem().string());
		}
	}
	std::sort(names.begin(), names.end());
	return names;
}

std::string SettingsMan::FilterPresetName(const std::string& name) const {
	return PresetFileName(name);
}

int SettingsMan::Save(Writer& writer) const {
	Serializable::Save(writer);

	writer.NewDivider(false);
	writer.NewLineString("// Display Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("PaletteFile", g_FrameMan.m_PaletteFile);
	writer.NewPropertyWithValue("ResolutionX", g_WindowMan.m_ResX);
	writer.NewPropertyWithValue("ResolutionY", g_WindowMan.m_ResY);
	writer.NewPropertyWithValue("ResolutionMultiplier", g_WindowMan.m_ResMultiplier);
	writer.NewPropertyWithValue("Fullscreen", g_WindowMan.m_Fullscreen);
	writer.NewPropertyWithValue("EnableVSync", g_WindowMan.m_EnableVSync);
	writer.NewPropertyWithValue("UseMultiDisplays", g_WindowMan.m_UseMultiDisplays);
	writer.NewPropertyWithValue("TwoPlayerSplitscreenVertSplit", g_FrameMan.m_TwoPlayerVSplit);

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Lighting and Post-Processing Settings (colors are linear R G B)", false);
	writer.NewLine(false);
	const LightingSettings lighting = g_PostProcessMan.GetLightingSettingsToSave();
	if (ControlLink::s_SettingsPort > 0) {
		writer.NewPropertyWithValue("ControlLinkPort", ControlLink::s_SettingsPort);
	}
	const std::pair<const char*, PresetGroup> startupKeys[] = {{"StartupPreset", PresetGroup::Everything}, {"StartupGameplayPreset", PresetGroup::Gameplay}, {"StartupVisualPreset", PresetGroup::Visual}};
	for (const auto& [key, group]: startupKeys) {
		if (!GetStartupPreset(group).empty()) {
			writer.NewPropertyWithValue(key, GetStartupPreset(group));
		}
	}
	SaveTunables(writer, lighting, false);

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Audio Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("MasterVolume", g_AudioMan.m_MasterVolume * 100);
	writer.NewPropertyWithValue("MuteMaster", g_AudioMan.m_MuteMaster);
	writer.NewPropertyWithValue("MusicVolume", g_AudioMan.m_MusicVolume * 100);
	writer.NewPropertyWithValue("MuteMusic", g_AudioMan.m_MuteMusic);
	writer.NewPropertyWithValue("SoundVolume", g_AudioMan.m_SoundsVolume * 100);
	writer.NewPropertyWithValue("MuteSounds", g_AudioMan.m_MuteSounds);
	writer.NewPropertyWithValue("MuteAudioOnFocusLoss", g_AudioMan.m_MuteAudioOnFocusLoss);
	for (int category = 0; category < AudioMan::MIX_CATEGORY_COUNT; ++category) {
		writer.NewPropertyWithValue("SoundMix" + AudioMan::c_SoundMixCategoryNames[category] + "Volume", g_AudioMan.m_SoundMixVolumes[category] * 100);
	}
	writer.NewPropertyWithValue("SoundPanningEffectStrength", g_AudioMan.m_SoundPanningEffectStrength);

	//////////////////////////////////////////////////
	// TODO These need to be removed when our soundscape is sorted out. They're only here temporarily to allow for easier tweaking.
	writer.NewPropertyWithValue("ListenerZOffset", g_AudioMan.m_ListenerZOffset);
	writer.NewPropertyWithValue("MinimumDistanceForPanning", g_AudioMan.m_MinimumDistanceForPanning);
	//////////////////////////////////////////////////

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Gameplay Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("ShowForeignItems", m_ShowForeignItems);
	writer.NewPropertyWithValue("FlashOnBrainDamage", m_FlashOnBrainDamage);
	writer.NewPropertyWithValue("BlipOnRevealUnseen", m_BlipOnRevealUnseen);
	writer.NewPropertyWithValue("MaxUnheldItems", g_MovableMan.m_MaxDroppedItems);
	writer.NewPropertyWithValue("UnheldItemsHUDDisplayRange", m_UnheldItemsHUDDisplayRange);
	writer.NewPropertyWithValue("AlwaysDisplayUnheldItemsInStrategicMode", m_AlwaysDisplayUnheldItemsInStrategicMode);
	writer.NewPropertyWithValue("SubPieMenuHoverOpenDelay", m_SubPieMenuHoverOpenDelay);
	writer.NewPropertyWithValue("EndlessMetaGameMode", m_EndlessMetaGameMode);
	writer.NewPropertyWithValue("EnableCrabBombs", m_EnableCrabBombs);
	writer.NewPropertyWithValue("SandboxGroupBadges", m_SandboxGroupBadges);
	writer.NewPropertyWithValue("CommanderBarKey", m_CommanderBarKey);
	writer.NewPropertyWithValue("ScenarioFactionPicks", m_ScenarioFactionPicks);
	writer.NewPropertyWithValue("ScenarioSettingValues", m_ScenarioSettingValues);
	writer.NewPropertyWithValue("ScenarioFlyingCraft", m_ScenarioFlyingCraft);
	writer.NewPropertyWithValue("SandboxOrderGlyphs", m_SandboxOrderGlyphs);
	writer.NewPropertyWithValue("SandboxSpawnStats", m_SandboxSpawnStats);
	writer.NewPropertyWithValue("SandboxSpawnSort", m_SandboxSpawnSort);
	writer.NewPropertyWithValue("SandboxAttackPings", m_SandboxAttackPings);
	writer.NewPropertyWithValue("SandboxNoRouteMarkers", m_SandboxNoRouteMarkers);
	writer.NewPropertyWithValue("SandboxMinimap", m_SandboxMinimap);
	writer.NewPropertyWithValue("BattleAlerts", m_BattleAlerts);
	writer.NewPropertyWithValue("BattleAlertLines", m_BattleAlertLines);
	writer.NewPropertyWithValue("BattleAlertSeconds", m_BattleAlertSeconds);
	writer.NewPropertyWithValue("BattleKillFeed", m_BattleKillFeed);
	writer.NewPropertyWithValue("BattleKillLines", m_BattleKillLines);
	writer.NewPropertyWithValue("BattleKillSeconds", m_BattleKillSeconds);
	writer.NewPropertyWithValue("BattleAlertSounds", m_BattleAlertSounds);
	writer.NewPropertyWithValue("BattleAlertVolume", m_BattleAlertVolume);
	writer.NewPropertyWithValue("BattleAskFirst", m_BattleAskFirst);
	writer.NewPropertyWithValue("BattleShowInfo", m_BattleShowInfo);
	writer.NewPropertyWithValue("BattleShowObjectives", m_BattleShowObjectives);
	writer.NewPropertyWithValue("BattleObjectiveLook", m_BattleObjectiveLook);
	writer.NewPropertyWithValue("BattleShowSpawnZones", m_BattleShowSpawnZones);
	writer.NewPropertyWithValue("BattleShowModeZones", m_BattleShowModeZones);
	writer.NewPropertyWithValue("ArmyOverview", m_ArmyOverview);
	writer.NewPropertyWithValue("CrabBombThreshold", m_CrabBombThreshold);
	writer.NewPropertyWithValue("ShowEnemyHUD", m_ShowEnemyHUD);
	writer.NewPropertyWithValue("SmartBuyMenuNavigation", m_EnableSmartBuyMenuNavigation);
	writer.NewPropertyWithValue("ScrapCompactingHeight", g_SceneMan.m_ScrapCompactingHeight);
	writer.NewPropertyWithValue("AutomaticGoldDeposit", m_AutomaticGoldDeposit);

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Screen Shake Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("ScreenShakeStrength", g_CameraMan.m_ScreenShakeStrength);
	writer.NewPropertyWithValue("ScreenShakeDecay", g_CameraMan.m_ScreenShakeDecay);
	writer.NewPropertyWithValue("MaxScreenShakeTime", g_CameraMan.m_MaxScreenShakeTime);
	writer.NewPropertyWithValue("DefaultShakePerUnitOfGibEnergy", g_CameraMan.m_DefaultShakePerUnitOfGibEnergy);
	writer.NewPropertyWithValue("DefaultShakePerUnitOfRecoilEnergy", g_CameraMan.m_DefaultShakePerUnitOfRecoilEnergy);
	writer.NewPropertyWithValue("DefaultShakeFromRecoilMaximum", g_CameraMan.m_DefaultShakeFromRecoilMaximum);

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Default Activity Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("LaunchIntoActivity", g_ActivityMan.m_LaunchIntoActivity);
	writer.NewPropertyWithValue("DefaultActivityType", g_ActivityMan.m_DefaultActivityType);
	writer.NewPropertyWithValue("DefaultActivityName", g_ActivityMan.m_DefaultActivityName);
	writer.NewPropertyWithValue("DefaultSceneName", g_SceneMan.m_DefaultSceneName);

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Map Browser Settings (the main menu's map picker, and the map last played for \"Continue on\")", false);
	writer.NewLine(false);
	// (The two names only when there is one, as StartupPreset.)
	if (!m_LastMap.empty()) {
		writer.NewPropertyWithValue("LastMap", m_LastMap);
	}
	if (!m_MapBrowserFilters.Mod.empty()) {
		writer.NewPropertyWithValue("MapBrowserMod", m_MapBrowserFilters.Mod);
	}
	writer.NewPropertyWithValue("MapBrowserType", m_MapBrowserFilters.Source);
	writer.NewPropertyWithValue("MapBrowserSize", m_MapBrowserFilters.Size);
	writer.NewPropertyWithValue("MapBrowserFavouritesOnly", m_MapBrowserFilters.OnlyFavourites);
	writer.NewPropertyWithValue("MapBrowserRecentOnly", m_MapBrowserFilters.OnlyRecent);
	writer.NewPropertyWithValue("MapBrowserSort", m_MapBrowserFilters.Sort);

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Engine Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("DisableLuaJIT", m_DisableLuaJIT);
	writer.NewPropertyWithValue("EnableLuaDebugging", m_EnableLuaDebugging);
	writer.NewPropertyWithValue("RecommendedMOIDCount", m_RecommendedMOIDCount);
	writer.NewPropertyWithValue("SceneBackgroundAutoScaleMode", m_SceneBackgroundAutoScaleMode);
	writer.NewPropertyWithValue("DisableFactionBuyMenuThemes", m_DisableFactionBuyMenuThemes);
	writer.NewPropertyWithValue("DisableMedievalUnits", m_DisableMedievalUnits);
	writer.NewPropertyWithValue("DisableFactionBuyMenuThemeCursors", m_DisableFactionBuyMenuThemeCursors);
	writer.NewPropertyWithValue("PathFinderGridNodeSize", m_PathFinderGridNodeSize);
	writer.NewPropertyWithValue("AIUpdateInterval", m_AIUpdateInterval);
	writer.NewPropertyWithValue("NumberOfLuaStatesOverride", m_NumberOfLuaStatesOverride);
	writer.NewPropertyWithValue("ForceImmediatePathingRequestCompletion", m_ForceImmediatePathingRequestCompletion);
	writer.NewPropertyWithValue("EnableParticleSettling", g_MovableMan.m_SettlingEnabled);
	writer.NewPropertyWithValue("EnableMOSubtraction", g_MovableMan.m_MOSubtractionEnabled);
	writer.NewPropertyWithValue("DeltaTime", g_TimerMan.GetDeltaTimeSecs());

	// No experimental settings right now :)
	// writer.NewLine(false, 2);
	// writer.NewDivider(false);
	// writer.NewLineString("// Engine Settings - EXPERIMENTAL", false);
	// writer.NewLineString("// These settings are experimental! They may break mods, crash the game, corrupt saves or worse. Use at your own risk.", false);
	// writer.NewLine(false);

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Editor Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("AllowSavingToBase", m_AllowSavingToBase);
	writer.NewPropertyWithValue("ShowMetaScenes", m_ShowMetaScenes);

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Misc Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("SkipIntro", m_SkipIntro);
	writer.NewPropertyWithValue("ShowToolTips", m_ShowToolTips);
	writer.NewPropertyWithValue("CaseSensitiveFilePaths", System::FilePathsCaseSensitive());
	writer.NewPropertyWithValue("DisableLoadingScreenProgressReport", m_DisableLoadingScreenProgressReport);
	writer.NewPropertyWithValue("LoadingScreenProgressReportPrecision", m_LoadingScreenProgressReportPrecision);
	writer.NewPropertyWithValue("ConsoleScreenRatio", g_ConsoleMan.m_ConsoleScreenRatio);
	writer.NewPropertyWithValue("ConsoleUseMonospaceFont", g_ConsoleMan.m_ConsoleUseMonospaceFont);
	writer.NewPropertyWithValue("AdvancedPerformanceStats", g_PerformanceMan.m_AdvancedPerfStats);
	writer.NewPropertyWithValue("MenuTransitionDurationMultiplier", m_MenuTransitionDurationMultiplier);

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Modder Debug Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("DrawAtomGroupVisualizations", m_DrawAtomGroupVisualizations);
	writer.NewPropertyWithValue("DrawHandAndFootGroupVisualizations", m_DrawHandAndFootGroupVisualizations);
	writer.NewPropertyWithValue("DrawLimbPathVisualizations", m_DrawLimbPathVisualizations);
	writer.NewPropertyWithValue("DrawRaycastVisualizations", g_SceneMan.m_DrawRayCastVisualizations);
	writer.NewPropertyWithValue("DrawPixelCheckVisualizations", g_SceneMan.m_DrawPixelCheckVisualizations);
	writer.NewPropertyWithValue("PrintDebugInfo", m_PrintDebugInfo);
	writer.NewPropertyWithValue("MeasureModuleLoadTime", m_MeasureModuleLoadTime);

	if (!m_VisibleAssemblyGroupsList.empty()) {
		writer.NewLine(false, 2);
		writer.NewDivider(false);
		writer.NewLineString("// Enabled Bunker Assembly Groups", false);
		writer.NewLine(false);
		for (const std::string& visibleAssembly: m_VisibleAssemblyGroupsList) {
			writer.NewPropertyWithValue("VisibleAssemblyGroup", visibleAssembly);
		}
	}

	if (!m_DisabledMods.empty()) {
		writer.NewLine(false, 2);
		writer.NewDivider(false);
		writer.NewLineString("// Disabled Mods", false);
		writer.NewLine(false);
		for (const auto& [modPath, modDisabled]: m_DisabledMods) {
			if (modDisabled) {
				writer.NewPropertyWithValue("DisableMod", modPath);
			}
		}
	}

	if (!m_EnabledGlobalScripts.empty()) {
		writer.NewLine(false, 2);
		writer.NewDivider(false);
		writer.NewLineString("// Enabled Global Scripts", false);
		writer.NewLine(false);
		for (const auto& [scriptPresetName, scriptEnabled]: m_EnabledGlobalScripts) {
			if (scriptEnabled) {
				writer.NewPropertyWithValue("EnableGlobalScript", scriptPresetName);
			}
		}
	}

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Input Mapping", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("ForceDisableMultimouse", g_UInputMan.m_ForceDisableMultiMouseKeyboard);
	writer.NewPropertyWithValue("MouseSensitivity", g_UInputMan.m_MouseSensitivity);

	writer.NewLine(false);
	writer.NewLineString("// Input Devices:  0 = Keyboard Only, 1 = Mouse + Keyboard, 2 = Gamepad One, 3 = Gamepad Two, , 4 = Gamepad Three, 5 = Gamepad Four");
	writer.NewLineString("// Scheme Presets: 0 = No Preset, 1 = Arrow Keys, 2 = WASD Keys, 3 = Mouse + WASD Keys, 4 = Generic DPad, 5 = Generic Dual Analog, 6 = SNES, 7 = DualShock 4, 8 = XBox 360");

	for (int player = Players::PlayerOne; player < Players::MaxPlayerCount; player++) {
		std::string playerNum = std::to_string(player + 1);
		writer.NewLine(false, 2);
		writer.NewDivider(false);
		writer.NewLineString("// Player " + playerNum, false);
		writer.NewLine(false);
		writer.NewPropertyWithValue("Player" + playerNum + "Scheme", g_UInputMan.m_ControlScheme[player]);
	}

	writer.ObjectEnd();

	return 0;
}
