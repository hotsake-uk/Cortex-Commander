#include "ControlLink.h"
#include "Actor.h"
#include "SettingsMan.h"
#include "Weather.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include "TextOverlay.h"
#include "TerrainFire.h"
#include "WeatherLightning.h"
#include "TerrainCollapse.h"
#include "FluidSim.h"
#include "ThreatMemory.h"
#include "Sandbox.h"
#include "SmokeGrid.h"
#include "ActorFire.h"
#include "ActorWater.h"
#include "UnitSpeech.h"
#include "ModernHUD.h"
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
#include "System.h"

#include <sstream>
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
	s_ReadLightingSettingsVersion = 0;

	m_FlashOnBrainDamage = true;
	m_BlipOnRevealUnseen = false;
	m_UnheldItemsHUDDisplayRange = 25 * c_PPM;
	m_AlwaysDisplayUnheldItemsInStrategicMode = true;
	m_SubPieMenuHoverOpenDelay = 1000;
	m_ClassicPieWheel = false;
	m_EndlessMetaGameMode = false;
	m_EnableCrabBombs = false;
	m_EnableMantling = true;
	m_AISuppression = 1.0F;
	m_AIDigWillingness = 1.0F;
	m_AIRecklessness = 0.5F;
	m_AISteadyBeforeJet = true;
	m_AIWaitForFuel = true;
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
	m_SandboxOrderGlyphs = 1;
	m_SandboxAttackPings = true;
	m_SandboxMinimap = false;
	m_LightsBySource = false;
	m_SandboxCharacterState = false;
	m_SandboxAutoBattle = false;
	m_SandboxPaintAudit = false;
	m_SandboxSelectionCamera = false;
	m_SandboxEffects = false;
	m_SandboxSimState = false;
	m_SandboxOrdersOverlay = 0;
	m_DebugChannels = 0;
	m_TraceAllUnits = false;
	m_ShowFPSAndVersion = true;
	m_CrabBombThreshold = 42;
	m_ShowEnemyHUD = true;
	m_ShowUnitTags = true;
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

	int failureCode = Serializable::Create(settingsReader);

	if (GetAnyExperimentalSettingsEnabled()) {
		// Show a message box to annoy people as much as possible while they're using experimental settings, so they can't leave it on accidentally
		RTEError::ShowMessageBox("Experimental settings are enabled!\nThis may break mods, crash the game, corrupt saves or worse.\nUse at your own risk.");
	}

	return failureCode;
}

void SettingsMan::UpdateSettingsFile() const {
	Writer settingsWriter(m_SettingsPath);
	g_SettingsMan.Save(settingsWriter);
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
	StartPropertyList(return Serializable::ReadProperty(propName, reader));

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
	MatchProperty("CollapseMaxPiece", { TerrainCollapse::GetTuning().MaxPiecePixels = std::clamp(std::stoi(reader.ReadPropValue()), 500, 200000); });
	MatchProperty("CollapseMinFitting", { TerrainCollapse::GetTuning().MinFittingPixels = std::clamp(std::stoi(reader.ReadPropValue()), 0, 5000); });
	MatchProperty("CollapseBreakStrength", { TerrainCollapse::GetTuning().BreakStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.1F, 10.0F); });
	MatchProperty("CollapseBlastPush", { TerrainCollapse::GetTuning().BlastPush = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 5.0F); });
	MatchProperty("CollapseCrushPixels", { TerrainCollapse::GetTuning().CrushPixels = std::clamp(std::stoi(reader.ReadPropValue()), 0, 500); });
	MatchProperty("CollapseScuffStrength", { TerrainCollapse::GetTuning().ScuffStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 3.0F); });
	MatchProperty("CollapseRestSeconds", { TerrainCollapse::GetTuning().RestSeconds = std::clamp(std::stof(reader.ReadPropValue()), 0.1F, 30.0F); });
	MatchProperty("CollapseHitDamage", { TerrainCollapse::GetTuning().HitDamage = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 20.0F); });
	MatchProperty("CollapseHitMinSpeed", { TerrainCollapse::GetTuning().HitMinSpeed = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 30.0F); });
	MatchProperty("CollapseHitMinPixels", { TerrainCollapse::GetTuning().HitMinPixels = std::clamp(std::stoi(reader.ReadPropValue()), 0, 5000); });
	MatchProperty("CollapseHitMassCap", { TerrainCollapse::GetTuning().HitMassCap = std::clamp(std::stof(reader.ReadPropValue()), 0.1F, 50.0F); });
	MatchProperty("CollapseHitKnockback", { TerrainCollapse::GetTuning().HitKnockback = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 10.0F); });
	MatchProperty("CollapseBuildings", { TerrainCollapse::SetBuildingsFall(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("LightningStrikes", { WeatherLightning::SetStrikes(static_cast<WeatherLightning::Strikes>(std::clamp(std::stoi(reader.ReadPropValue()), 0, 2))); });
	MatchProperty("AIThreatMemory", { ThreatMemory::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("WaterFreezes", { FluidSim::SetFreezingEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("BloodFlows", { FluidSim::SetBloodFlows(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("LoosePowders", { FluidSim::SetPowdersEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("TerrainCollapse", { TerrainCollapse::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("TerrainFire", { TerrainFire::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
	MatchProperty("ModernHUD", { ModernHUD::SetEnabled(std::stoi(reader.ReadPropValue()) != 0); });
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
	MatchProperty("LightingDebugView", { g_PostProcessMan.GetLightingSettings().DebugView = std::stoi(reader.ReadPropValue()); }); // Read only, for automated screenshots.
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
	MatchProperty("CustomWeather", { g_PostProcessMan.GetLightingSettings().CustomWeather = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("WeatherGlow", { g_PostProcessMan.GetLightingSettings().WeatherGlow = std::stof(reader.ReadPropValue()); });
	MatchProperty("WeatherIntensity", { g_PostProcessMan.GetLightingSettings().WeatherIntensity = std::stof(reader.ReadPropValue()); });
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
	MatchProperty("UnitOutlineTeamColor", { g_PostProcessMan.GetLightingSettings().UnitOutlineTeamColor = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("UnitOutlineColor", { g_PostProcessMan.GetLightingSettings().UnitOutlineColor = glm::clamp(ReadVec3(reader.ReadPropValue(), g_PostProcessMan.GetLightingSettings().UnitOutlineColor), glm::vec3(0.0F), glm::vec3(1.0F)); });
	MatchProperty("UnitOutlineOpacity", { g_PostProcessMan.GetLightingSettings().UnitOutlineOpacity = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("PaletteAnimation", { g_PostProcessMan.GetLightingSettings().PaletteAnimation = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("PaletteAnimationStrength", { g_PostProcessMan.GetLightingSettings().PaletteAnimationStrength = std::clamp(std::stof(reader.ReadPropValue()), 0.0F, 1.0F); });
	MatchProperty("FireFlameBrightness", { g_PostProcessMan.GetLightingSettings().FireFlameBrightness = std::stof(reader.ReadPropValue()); });
	MatchProperty("ShockwaveStrength", { g_PostProcessMan.GetLightingSettings().ShockwaveStrength = std::stof(reader.ReadPropValue()); });
	MatchProperty("SmokeScattering", { g_PostProcessMan.GetLightingSettings().SmokeScattering = std::stof(reader.ReadPropValue()); });
	MatchProperty("SmokeShading", { g_PostProcessMan.GetLightingSettings().SmokeShading = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("SmokeShadingStrength", { g_PostProcessMan.GetLightingSettings().SmokeShadingStrength = std::stof(reader.ReadPropValue()); });
	MatchProperty("EffectsParticles", { g_PostProcessMan.GetLightingSettings().EffectsParticles = std::stof(reader.ReadPropValue()); });
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
	MatchProperty("WaterMistBrightness", { g_PostProcessMan.GetLightingSettings().WaterMistBrightness = std::stof(reader.ReadPropValue()); });
	MatchProperty("WaterMistGlow", { g_PostProcessMan.GetLightingSettings().WaterMistGlow = std::stof(reader.ReadPropValue()); });
	MatchProperty("RainSplashes", { g_PostProcessMan.GetLightingSettings().RainSplashes = std::stof(reader.ReadPropValue()); });
	MatchProperty("WeatherLight", { g_PostProcessMan.GetLightingSettings().WeatherLight = std::stof(reader.ReadPropValue()); });
	MatchProperty("TracerGlow", { g_PostProcessMan.GetLightingSettings().TracerGlow = std::stof(reader.ReadPropValue()); });
	MatchProperty("TracerLightBrightness", { g_PostProcessMan.GetLightingSettings().TracerLightBrightness = std::stof(reader.ReadPropValue()); });
	MatchProperty("TracerLightRandomness", { g_PostProcessMan.GetLightingSettings().TracerLightRandomness = std::stof(reader.ReadPropValue()); });
	MatchProperty("SaberLightBrightness", { g_PostProcessMan.GetLightingSettings().SaberLightBrightness = std::max(std::stof(reader.ReadPropValue()), 0.0F); });
	MatchProperty("SaberLightReach", { g_PostProcessMan.GetLightingSettings().SaberLightReach = std::max(std::stof(reader.ReadPropValue()), 0.0F); });
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
	MatchProperty("PanelsOverlay", { g_DebugMan.m_PanelsOverlay = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("DockPanels", { g_DebugMan.m_DockPanels = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("SandboxCharacter", { Sandbox::SetCharacterSetup(reader.ReadPropValue()); });
	MatchProperty("SandboxPins", { /* Pins used to live here; they belong to the saved game now. */ reader.ReadPropValue(); });
	MatchProperty("SandboxFavourites", { /* Favourites used to live here; they have their own file now. */ reader.ReadPropValue(); });
	MatchProperty("PixelToolFont", { g_DebugMan.m_PixelFont = std::stoi(reader.ReadPropValue()) != 0; });
	MatchProperty("ToolScale", { g_DebugMan.m_ToolScale = std::clamp(std::stof(reader.ReadPropValue()), 0.4F, 1.5F); });
	MatchProperty("PanelWidth", { g_DebugMan.m_PanelWidth = std::clamp(std::stof(reader.ReadPropValue()), 240.0F, 700.0F); });
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
	MatchProperty("EndlessMetaGameMode", { reader >> m_EndlessMetaGameMode; });
	MatchProperty("EndlessMode", { reader >> m_EndlessMetaGameMode; }); // Legacy name, kept for old Settings.ini files.
	MatchProperty("EnableCrabBombs", { reader >> m_EnableCrabBombs; });
	MatchProperty("EnableMantling", { reader >> m_EnableMantling; });
	MatchProperty("UnitSpeech", { bool on = true; reader >> on; UnitSpeech::SetEnabled(on); });
	MatchProperty("UnitSpeechChance", { int percent = 40; reader >> percent; UnitSpeech::SetChance(percent); });
	MatchProperty("UnitSpeechEnemies", { bool on = true; reader >> on; UnitSpeech::SetShowsEnemies(on); });
	MatchProperty("UnitSpeechOff", { UnitSpeech::SetTriggerOn(reader.ReadPropValue(), false); });
	MatchProperty("AISuppression", {
		reader >> m_AISuppression;
		m_AISuppression = std::clamp(m_AISuppression, 0.0F, 2.0F);
	});
	MatchProperty("AIDigWillingness", { float scale = 1.0F; reader >> scale; SetAIDigWillingness(scale); });
	MatchProperty("AIRecklessness", { float recklessness = 0.5F; reader >> recklessness; SetAIRecklessness(recklessness); });
	MatchProperty("AISteadyBeforeJet", { reader >> m_AISteadyBeforeJet; });
	MatchProperty("AIWaitForFuel", { reader >> m_AIWaitForFuel; });
	MatchProperty("NavDebugOverlay", { int level = 0; reader >> level; SetNavDebugOverlay(level); });
	MatchProperty("DebugTeam", { int team = 0; reader >> team; SetDebugTeam(team); });
	MatchProperty("UnitInspector", { int which = 0; reader >> which; SetUnitInspector(which); });
	MatchProperty("ShowSquadLinks", { reader >> m_ShowSquadLinks; });
	MatchProperty("ShowOrderLabels", { reader >> m_ShowOrderLabels; });
	MatchProperty("CombatOverlay", { int which = 0; reader >> which; SetCombatOverlay(which); });
	MatchProperty("ShowLightSources", { reader >> m_ShowLightSources; });
	MatchProperty("ShowSunDirection", { reader >> m_ShowSunDirection; });
	MatchProperty("WorldSimOverlay", { int which = 0; reader >> which; SetWorldSimOverlay(which); });
	MatchProperty("SandboxStrokeLog", { reader >> m_SandboxStrokeLog; });
	MatchProperty("SandboxSpotReach", { reader >> m_SandboxSpotReach; });
	MatchProperty("SandboxGroupBadges", { reader >> m_SandboxGroupBadges; });
	MatchProperty("SandboxOrderGlyphs", { int which = 1; reader >> which; SetSandboxOrderGlyphs(which); });
	MatchProperty("SandboxAttackPings", { reader >> m_SandboxAttackPings; });
	MatchProperty("SandboxMinimap", { reader >> m_SandboxMinimap; });
	MatchProperty("LightsBySource", { reader >> m_LightsBySource; });
	MatchProperty("SandboxCharacterState", { reader >> m_SandboxCharacterState; });
	MatchProperty("SandboxAutoBattle", { reader >> m_SandboxAutoBattle; });
	MatchProperty("SandboxPaintAudit", { reader >> m_SandboxPaintAudit; });
	MatchProperty("SandboxSelectionCamera", { reader >> m_SandboxSelectionCamera; });
	MatchProperty("SandboxEffects", { reader >> m_SandboxEffects; });
	MatchProperty("SandboxSimState", { reader >> m_SandboxSimState; });
	MatchProperty("SandboxOrdersOverlay", { int which = 0; reader >> which; SetSandboxOrdersOverlay(which); });
	MatchProperty("DebugChannels", { reader >> m_DebugChannels; });
	MatchProperty("TraceAllUnits", { reader >> m_TraceAllUnits; });
	MatchProperty("ShowFPSAndVersion", { reader >> m_ShowFPSAndVersion; });
	MatchProperty("CrabBombThreshold", { reader >> m_CrabBombThreshold; });
	MatchProperty("ShowEnemyHUD", { reader >> m_ShowEnemyHUD; });
	MatchProperty("ShowUnitTags", { reader >> m_ShowUnitTags; });
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
	MatchProperty("DisableLuaJIT", { reader >> m_DisableLuaJIT; });
	MatchProperty("EnableLuaDebugging", { reader >> m_EnableLuaDebugging; });
	MatchProperty("RecommendedMOIDCount", { reader >> m_RecommendedMOIDCount; });
	MatchProperty("SceneBackgroundAutoScaleMode", { SetSceneBackgroundAutoScaleMode(std::stoi(reader.ReadPropValue())); });
	MatchProperty("DisableFactionBuyMenuThemes", { reader >> m_DisableFactionBuyMenuThemes; });
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

void SettingsMan::SaveTunables(Writer& writer, const LightingSettings& lighting) const {
	writer.NewPropertyWithValue("LightingSettingsVersion", c_LightingSettingsVersion);
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
	writer.NewPropertyWithValue("CustomWeather", lighting.CustomWeather);
	writer.NewPropertyWithValue("WeatherGlow", lighting.WeatherGlow);
	writer.NewPropertyWithValue("WeatherIntensity", lighting.WeatherIntensity);
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
	writer.NewPropertyWithValue("UnitOutlineTeamColor", lighting.UnitOutlineTeamColor);
	writer.NewPropertyWithValue("UnitOutlineColor", WriteVec3(lighting.UnitOutlineColor));
	writer.NewPropertyWithValue("UnitOutlineOpacity", lighting.UnitOutlineOpacity);
	writer.NewPropertyWithValue("PaletteAnimation", lighting.PaletteAnimation);
	writer.NewPropertyWithValue("PaletteAnimationStrength", lighting.PaletteAnimationStrength);
	writer.NewPropertyWithValue("FireFlameBrightness", lighting.FireFlameBrightness);
	writer.NewPropertyWithValue("ShockwaveStrength", lighting.ShockwaveStrength);
	writer.NewPropertyWithValue("Embers", lighting.Embers);
	writer.NewPropertyWithValue("EffectsParticles", lighting.EffectsParticles);
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
	writer.NewPropertyWithValue("NightAffectsAI", lighting.NightAffectsAI);
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
	writer.NewPropertyWithValue("AimDotsLight", lighting.AimDotsLight);
	writer.NewPropertyWithValue("ShowAIPaths", Actor::ShowAIPaths());
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
	writer.NewPropertyWithValue("TerrainFire", TerrainFire::IsEnabled());
	writer.NewPropertyWithValue("TerrainCollapse", TerrainCollapse::IsEnabled());
	writer.NewPropertyWithValue("FlowingLiquids", FluidSim::IsEnabled());
	writer.NewPropertyWithValue("LoosePowders", FluidSim::PowdersEnabled());
	writer.NewPropertyWithValue("WaterFreezes", FluidSim::FreezingEnabled());
	writer.NewPropertyWithValue("LightningStrikes", static_cast<int>(WeatherLightning::GetStrikes()));
	writer.NewPropertyWithValue("BloodFlows", FluidSim::BloodFlows());
	writer.NewPropertyWithValue("CollapseBuildings", TerrainCollapse::BuildingsFall());
	writer.NewPropertyWithValue("CollapseFloatingStays", TerrainCollapse::GetTuning().FloatingStays);
	writer.NewPropertyWithValue("CollapseNeckWidth", TerrainCollapse::GetTuning().NeckWidth);
	writer.NewPropertyWithValue("CollapseMaxPiece", TerrainCollapse::GetTuning().MaxPiecePixels);
	writer.NewPropertyWithValue("CollapseMinFitting", TerrainCollapse::GetTuning().MinFittingPixels);
	writer.NewPropertyWithValue("CollapseBreakStrength", TerrainCollapse::GetTuning().BreakStrength);
	writer.NewPropertyWithValue("CollapseScuffStrength", TerrainCollapse::GetTuning().ScuffStrength);
	writer.NewPropertyWithValue("CollapseRestSeconds", TerrainCollapse::GetTuning().RestSeconds);
	writer.NewPropertyWithValue("CollapseCrushPixels", TerrainCollapse::GetTuning().CrushPixels);
	writer.NewPropertyWithValue("CollapseBlastPush", TerrainCollapse::GetTuning().BlastPush);
	writer.NewPropertyWithValue("CollapseHitDamage", TerrainCollapse::GetTuning().HitDamage);
	writer.NewPropertyWithValue("CollapseHitMinSpeed", TerrainCollapse::GetTuning().HitMinSpeed);
	writer.NewPropertyWithValue("CollapseHitMinPixels", TerrainCollapse::GetTuning().HitMinPixels);
	writer.NewPropertyWithValue("CollapseHitMassCap", TerrainCollapse::GetTuning().HitMassCap);
	writer.NewPropertyWithValue("CollapseHitKnockback", TerrainCollapse::GetTuning().HitKnockback);
	writer.NewPropertyWithValue("SmokeBlocksSight", SmokeGrid::IsEnabled());
	writer.NewPropertyWithValue("AIThreatMemory", ThreatMemory::IsEnabled());
	writer.NewPropertyWithValue("BurningUnits", ActorFire::IsEnabled());
	writer.NewPropertyWithValue("SwimmingAndDrowning", ActorWater::IsEnabled());
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
}

std::string SettingsMan::SavePreset(const std::string& name) const {
	std::string safe = PresetFileName(name);
	if (safe.empty()) {
		return "";
	}
	std::error_code error;
	std::filesystem::create_directories(PresetFolder(), error);
	Writer writer(PresetFolder() + safe + ".ini");
	if (!writer.WriterOK()) {
		return "";
	}
	writer.ObjectStart(GetClassName());
	// As they are on screen now, not as the player's own behind a scene that sets its time and weather.
	SaveTunables(writer, g_PostProcessMan.GetLightingSettings());
	writer.ObjectEnd();
	writer.EndWrite();
	return safe;
}

bool SettingsMan::LoadPreset(const std::string& name) {
	std::string path = PresetFolder() + PresetFileName(name) + ".ini";
	if (!std::filesystem::exists(path)) {
		return false;
	}
	Reader reader(path, false, nullptr, true, true);
	if (!reader.ReaderOK()) {
		return false;
	}
	// The same reading as the settings file gets, of a file that holds only the tunable settings.
	return CreateSerializable(reader, true, false, false) >= 0;
}

bool SettingsMan::DeletePreset(const std::string& name) const {
	std::error_code error;
	return std::filesystem::remove(PresetFolder() + PresetFileName(name) + ".ini", error);
}

std::vector<std::string> SettingsMan::ListPresets() const {
	std::vector<std::string> names;
	std::error_code error;
	for (const auto& entry: std::filesystem::directory_iterator(PresetFolder(), error)) {
		if (entry.is_regular_file() && entry.path().extension() == ".ini") {
			names.push_back(entry.path().stem().string());
		}
	}
	std::sort(names.begin(), names.end());
	return names;
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
	writer.NewPropertyWithValue("SmoothHUDText", TextOverlay::IsEnabled());
	writer.NewPropertyWithValue("ModernHUD", ModernHUD::IsEnabled());

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Lighting and Post-Processing Settings (colors are linear R G B)", false);
	writer.NewLine(false);
	const LightingSettings lighting = g_PostProcessMan.GetLightingSettingsToSave();
	writer.NewPropertyWithValue("DockPanels", g_DebugMan.m_DockPanels);
	writer.NewPropertyWithValue("PanelsOverlay", g_DebugMan.m_PanelsOverlay);
	writer.NewPropertyWithValue("PanelWidth", g_DebugMan.m_PanelWidth);
	writer.NewPropertyWithValue("ToolScale", g_DebugMan.m_ToolScale);
	writer.NewPropertyWithValue("PixelToolFont", g_DebugMan.m_PixelFont);
	writer.NewPropertyWithValue("SandboxCharacter", Sandbox::GetCharacterSetup());
	if (ControlLink::s_SettingsPort > 0) {
		writer.NewPropertyWithValue("ControlLinkPort", ControlLink::s_SettingsPort);
	}
	SaveTunables(writer, lighting);

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
	writer.NewPropertyWithValue("ClassicPieWheel", m_ClassicPieWheel);
	writer.NewPropertyWithValue("EndlessMetaGameMode", m_EndlessMetaGameMode);
	writer.NewPropertyWithValue("EnableCrabBombs", m_EnableCrabBombs);
	writer.NewPropertyWithValue("EnableMantling", m_EnableMantling);
	writer.NewPropertyWithValue("UnitSpeech", UnitSpeech::IsEnabled());
	writer.NewPropertyWithValue("UnitSpeechChance", UnitSpeech::GetChance());
	writer.NewPropertyWithValue("UnitSpeechEnemies", UnitSpeech::ShowsEnemies());
	for (const std::string& off: UnitSpeech::GetTriggersOff()) {
		writer.NewPropertyWithValue("UnitSpeechOff", off);
	}
	writer.NewPropertyWithValue("AISuppression", m_AISuppression);
	writer.NewPropertyWithValue("AIDigWillingness", m_AIDigWillingness);
	writer.NewPropertyWithValue("AIRecklessness", m_AIRecklessness);
	writer.NewPropertyWithValue("AISteadyBeforeJet", m_AISteadyBeforeJet);
	writer.NewPropertyWithValue("AIWaitForFuel", m_AIWaitForFuel);
	writer.NewPropertyWithValue("NavDebugOverlay", m_NavDebugOverlay);
	writer.NewPropertyWithValue("DebugTeam", m_DebugTeam);
	writer.NewPropertyWithValue("UnitInspector", m_UnitInspector);
	writer.NewPropertyWithValue("ShowSquadLinks", m_ShowSquadLinks);
	writer.NewPropertyWithValue("ShowOrderLabels", m_ShowOrderLabels);
	writer.NewPropertyWithValue("CombatOverlay", m_CombatOverlay);
	writer.NewPropertyWithValue("ShowLightSources", m_ShowLightSources);
	writer.NewPropertyWithValue("ShowSunDirection", m_ShowSunDirection);
	writer.NewPropertyWithValue("WorldSimOverlay", m_WorldSimOverlay);
	writer.NewPropertyWithValue("SandboxStrokeLog", m_SandboxStrokeLog);
	writer.NewPropertyWithValue("SandboxSpotReach", m_SandboxSpotReach);
	writer.NewPropertyWithValue("SandboxGroupBadges", m_SandboxGroupBadges);
	writer.NewPropertyWithValue("SandboxOrderGlyphs", m_SandboxOrderGlyphs);
	writer.NewPropertyWithValue("SandboxAttackPings", m_SandboxAttackPings);
	writer.NewPropertyWithValue("SandboxMinimap", m_SandboxMinimap);
	writer.NewPropertyWithValue("LightsBySource", m_LightsBySource);
	writer.NewPropertyWithValue("SandboxCharacterState", m_SandboxCharacterState);
	writer.NewPropertyWithValue("SandboxAutoBattle", m_SandboxAutoBattle);
	writer.NewPropertyWithValue("SandboxPaintAudit", m_SandboxPaintAudit);
	writer.NewPropertyWithValue("SandboxSelectionCamera", m_SandboxSelectionCamera);
	writer.NewPropertyWithValue("SandboxEffects", m_SandboxEffects);
	writer.NewPropertyWithValue("SandboxSimState", m_SandboxSimState);
	writer.NewPropertyWithValue("SandboxOrdersOverlay", m_SandboxOrdersOverlay);
	writer.NewPropertyWithValue("DebugChannels", m_DebugChannels);
	writer.NewPropertyWithValue("TraceAllUnits", m_TraceAllUnits);
	writer.NewPropertyWithValue("ShowFPSAndVersion", m_ShowFPSAndVersion);
	writer.NewPropertyWithValue("CrabBombThreshold", m_CrabBombThreshold);
	writer.NewPropertyWithValue("ShowEnemyHUD", m_ShowEnemyHUD);
	writer.NewPropertyWithValue("ShowUnitTags", m_ShowUnitTags);
	writer.NewPropertyWithValue("SmartBuyMenuNavigation", m_EnableSmartBuyMenuNavigation);
	writer.NewPropertyWithValue("ScrapCompactingHeight", g_SceneMan.m_ScrapCompactingHeight);
	writer.NewPropertyWithValue("AutomaticGoldDeposit", m_AutomaticGoldDeposit);

	writer.NewLine(false, 2);
	writer.NewDivider(false);
	writer.NewLineString("// Screen Shake Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("ScreenShakeStrength", g_CameraMan.m_ScreenShakeStrength);
	writer.NewPropertyWithValue("HitStopStrength", g_CameraMan.m_HitStopStrength);
	writer.NewPropertyWithValue("FrameCap", g_WindowMan.GetFrameCap());
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
	writer.NewLineString("// Engine Settings", false);
	writer.NewLine(false);
	writer.NewPropertyWithValue("DisableLuaJIT", m_DisableLuaJIT);
	writer.NewPropertyWithValue("EnableLuaDebugging", m_EnableLuaDebugging);
	writer.NewPropertyWithValue("RecommendedMOIDCount", m_RecommendedMOIDCount);
	writer.NewPropertyWithValue("SceneBackgroundAutoScaleMode", m_SceneBackgroundAutoScaleMode);
	writer.NewPropertyWithValue("DisableFactionBuyMenuThemes", m_DisableFactionBuyMenuThemes);
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
