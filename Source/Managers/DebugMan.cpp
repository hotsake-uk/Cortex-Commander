#include "DebugMan.h"
#include "WindowMan.h"
#include "PerformanceMan.h"
#include "imgui/imgui.h"
#include "tracy/Tracy.hpp"
#include "Draw.h"
#include "RenderTarget.h"
#include "RenderBatch.h"
#include "RenderMan.h"
#include "MovableMan.h"
#include "CameraMan.h"
#include "FrameMan.h"
#include "SceneMan.h"
#include "PostProcessMan.h"
#include "SettingsMan.h"
#include "SceneLighting.h"
#include "TextOverlay.h"
#include "EffectsParticles.h"
#include "TerrainFire.h"
#include "TerrainCollapse.h"
#include "FluidSim.h"
#include "SmokeGrid.h"
#include "ActorFire.h"
#include "ActorWater.h"
#include "Controller.h"
#include "Sandbox.h"
#include "ModernHUD.h"
#include "TimerMan.h"
#include "UInputMan.h"
#include "ActivityMan.h"
#include "FrameMan.h"
#include "Scene.h"
#include "tracy/TracyOpenGL.hpp"

using namespace RTE;

void Draw() {
}

void DebugMan::DrawImGui() {
	UpdateMouseOwnership();

	// Debug windows keep a readable size on big windows and handheld screens: scale with the window height (720 px = 1x).
	{
		ImGuiIO& io = ImGui::GetIO();
		float uiScale = std::clamp(io.DisplaySize.y / 720.0F, 1.0F, 2.5F);
		float fontScale = uiScale * g_WindowMan.GetImGuiFontBaseScale();
		if (std::abs(io.FontGlobalScale - fontScale) > 0.001F) {
			io.FontGlobalScale = fontScale;
			ImGuiStyle& style = ImGui::GetStyle();
			style = ImGuiStyle();
			ImGui::StyleColorsDark(&style);
			style.ScaleAllSizes(uiScale);
		}
	}

	// The modern HUD, unless photo mode is hiding the HUD.
	if (!IsPhotoModeHidingHUD()) {
		ModernHUD::Draw();
	}

	if (m_ShowWorldDebug) {
		WorldDebugGUI();
	}

	Sandbox::DrawGUI();

	if (m_ShowPhotoMode) {
		PhotoModeGUI();
	} else if (m_PhotoModeActive) {
		EndPhotoMode();
	}

	if (m_ShowDebugWindow) {
		DebugOptionsGUI();
	}

	if (m_ShowActorDebugGui) {
		ActorDrawDebugGUI();
	}

	if (m_ImGuiDemoWindow) {
		ImGui::ShowDemoWindow(&m_ImGuiDemoWindow);
	}

	if (m_ShowPerformanceMan) {
		g_PerformanceMan.ImGui();
	}

	if (m_ShowGraphicsLab) {
		GraphicsLabGUI();
	}
}

void DebugMan::UpdateMouseOwnership() {
	bool wantMouse = Sandbox::IsOpen() || m_ShowWorldDebug || m_ShowPhotoMode || m_ShowGraphicsLab || m_ShowDebugWindow || m_ShowActorDebugGui || m_ImGuiDemoWindow || m_ShowPerformanceMan;
	if (wantMouse != m_ReleasedMouseForImGui) {
		// In game the mouse is trapped in relative mode for aiming, which ImGui can't use; release it while debug windows are open.
		g_UInputMan.DisableMouseMoving(wantMouse);
		m_ReleasedMouseForImGui = wantMouse;
	}
	// The game hides the OS cursor and draws its own, so have ImGui draw one too.
	ImGui::GetIO().MouseDrawCursor = wantMouse;
}

void DebugMan::WorldDebugGUI() {
	ImGui::SetNextWindowSize(ImVec2(340.0F, 0.0F), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(10.0F, 40.0F), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("World Debug (F6)", &m_ShowWorldDebug)) {
		LightingSettings& settings = g_PostProcessMan.GetLightingSettings();

		if (const Scene* scene = g_SceneMan.GetScene(); scene && g_ActivityMan.IsInActivity()) {
			ImGui::Text("Scene: %s", scene->GetPresetName().c_str());
		} else {
			ImGui::TextDisabled("No scene loaded (menus)");
		}
		ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
		if (ImGui::Button(Sandbox::IsOpen() ? "Close sandbox (F7)" : "Open sandbox (F7)")) {
			Sandbox::Toggle();
		}

		ImGui::SeparatorText("Time of day");
		ImGui::SliderFloat("Hour", &settings.TimeOfDay, 0.0F, 24.0F, "%.2f");
		const std::pair<const char*, float> presets[] = {{"Midnight", 0.0F}, {"Dawn", 6.0F}, {"Noon", 12.0F}, {"Dusk", 19.0F}, {"Night", 22.0F}};
		for (size_t i = 0; i < std::size(presets); ++i) {
			if (i > 0) {
				ImGui::SameLine();
			}
			if (ImGui::Button(presets[i].first)) {
				settings.TimeOfDay = presets[i].second;
			}
		}
		bool timeFlows = settings.DayLengthMinutes > 0.0F;
		if (ImGui::Checkbox("Time passes", &timeFlows)) {
			settings.DayLengthMinutes = timeFlows ? 10.0F : 0.0F;
		}
		if (timeFlows) {
			ImGui::SliderFloat("Day length (min)", &settings.DayLengthMinutes, 0.5F, 60.0F, "%.1f", ImGuiSliderFlags_Logarithmic);
		}

		ImGui::SeparatorText("Weather");
		ImGui::Combo("Precipitation", &settings.WeatherType, "Clear\0Rain\0Snow\0Ash fall\0Dust storm\0");
		ImGui::SliderFloat("Intensity", &settings.WeatherIntensity, 0.0F, 1.0F);
		ImGui::SliderFloat("Wind", &settings.Wind, -400.0F, 400.0F, "%.0f px/s");

		ImGui::SeparatorText("Lighting");
		int quality = settings.GraphicsQuality;
		if (ImGui::Combo("Quality", &quality, "Potato (classic)\0Low\0Medium\0High\0Ultra\0Custom\0")) {
			settings.ApplyQualityPreset(quality);
		}
		// Scene makers: keep what's set above as the scene's own atmosphere. It's written when the scene is saved from the scene editor.
		if (Scene* scene = g_SceneMan.GetScene()) {
			ImGui::SeparatorText("This scene's own atmosphere");
			const Scene::Atmosphere& own = scene->GetAtmosphere();
			if (own.TimeOfDay >= 0.0F || own.WeatherType >= 0) {
				static const char* weatherNames[] = {"clear", "rain", "snow", "ash fall", "dust storm"};
				ImGui::Text("Set: %.1f h, %s", own.TimeOfDay, own.WeatherType >= 0 && own.WeatherType <= 4 ? weatherNames[own.WeatherType] : "default weather");
			} else {
				ImGui::TextDisabled("Not set (uses the player's settings)");
			}
			if (ImGui::Button("Use the current time and weather")) {
				Scene::Atmosphere atmosphere;
				atmosphere.TimeOfDay = settings.TimeOfDay;
				atmosphere.DayLengthMinutes = settings.DayLengthMinutes;
				atmosphere.WeatherType = settings.WeatherType;
				atmosphere.WeatherIntensity = settings.WeatherIntensity;
				atmosphere.Wind = settings.Wind;
				scene->SetAtmosphere(atmosphere);
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear")) {
				scene->SetAtmosphere(Scene::Atmosphere());
			}
			ImGui::TextDisabled("Saved when the scene is saved in the scene editor.");
		}
		ImGui::Checkbox("Lighting", &settings.Enabled);
		ImGui::SameLine();
		ImGui::Checkbox("Bloom", &settings.BloomEnabled);
		ImGui::SameLine();
		ImGui::Checkbox("Extra effects", &settings.DistortionEnabled);
		ImGui::Checkbox("Radiance cascades GI", &settings.RadianceCascades);
		bool modernHUD = ModernHUD::IsEnabled();
		if (ImGui::Checkbox("Modern HUD", &modernHUD)) {
			ModernHUD::SetEnabled(modernHUD);
		}
		bool smoothText = TextOverlay::IsEnabled();
		if (ImGui::Checkbox("Smooth HUD text", &smoothText)) {
			TextOverlay::SetEnabled(smoothText);
		}
		// Brightness sliders scale the colors uniformly, keeping their tint.
		auto brightnessSlider = [](const char* label, glm::vec3& color, float maxValue) {
			float level = std::max({color.x, color.y, color.z});
			if (ImGui::SliderFloat(label, &level, 0.0F, maxValue, "%.2f") && level > 0.0F) {
				float current = std::max({color.x, color.y, color.z});
				color = current > 0.0F ? color * (level / current) : glm::vec3(level);
			}
		};
		brightnessSlider("Interior / cave light", settings.Ambient, 1.0F);
		brightnessSlider("Playfield light floor", settings.ForegroundAmbient, 1.0F);
		brightnessSlider("Sky light", settings.SkyColor, 2.0F);
		ImGui::SliderFloat("Exposure", &settings.Exposure, 0.1F, 4.0F);
		ImGui::SliderFloat("Auto exposure", &settings.AutoExposure, 0.0F, 1.0F);
		if (SceneLighting* lighting = g_PostProcessMan.GetSceneLighting(); lighting && settings.Enabled && settings.AutoExposure > 0.0F) {
			float averageLuminance = 0.0F;
			float autoExposure = 1.0F;
			lighting->ReadAutoExposure(averageLuminance, autoExposure);
			ImGui::Text("Scene luminance %.3f -> exposure x%.2f", averageLuminance, autoExposure);
		}
		ImGui::SliderFloat("God rays", &settings.GodRays, 0.0F, 2.0F);
		ImGui::SliderFloat("Haze", &settings.AtmosphereHaze, 0.0F, 1.0F);
		ImGui::Combo("View", &settings.DebugView, "Final image\0Lighting on grey\0Sky light only\0Dynamic light only\0Normals\0Distortion\0GI only (radiance cascades)\0Solid objects and distance to them\0Where the sun is visible\0");

		ImGui::SeparatorText("Game");
		bool terrainFire = TerrainFire::IsEnabled();
		if (ImGui::Checkbox("Spreading fire", &terrainFire)) {
			TerrainFire::SetEnabled(terrainFire);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d burning)", TerrainFire::GetCount());
		bool terrainCollapse = TerrainCollapse::IsEnabled();
		if (ImGui::Checkbox("Collapsing terrain", &terrainCollapse)) {
			TerrainCollapse::SetEnabled(terrainCollapse);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d pixels fell)", TerrainCollapse::GetCollapsedCount());
		bool liquids = FluidSim::IsEnabled();
		if (ImGui::Checkbox("Flowing liquids", &liquids)) {
			FluidSim::SetEnabled(liquids);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d moving, %.2f ms)", FluidSim::GetActiveCount(), FluidSim::GetLastUpdateMS());
		float hitStop = g_CameraMan.GetHitStopStrength();
		if (ImGui::SliderFloat("Hit-stop on big blasts", &hitStop, 0.0F, 2.0F)) {
			g_CameraMan.SetHitStopStrength(hitStop);
		}
		int frameCap = g_WindowMan.GetFrameCap();
		if (ImGui::SliderInt("Frame cap (0 = none)", &frameCap, 0, 360)) {
			g_WindowMan.SetFrameCap(frameCap > 0 && frameCap < 30 ? 30 : frameCap);
		}
		float cameraZoom = g_FrameMan.GetCameraZoom();
		if (ImGui::SliderFloat("Camera zoom", &cameraZoom, FrameMan::c_MinCameraZoom, FrameMan::c_MaxCameraZoom, "%.2fx")) {
			g_FrameMan.SetCameraZoom(cameraZoom);
		}
		ImGui::TextDisabled("(Ctrl + mouse wheel in game)");
		bool aiPaused = Controller::IsAIPaused();
		if (ImGui::Checkbox("Pause AI", &aiPaused)) {
			Controller::SetAIPaused(aiPaused);
		}
		bool swimming = ActorWater::IsEnabled();
		if (ImGui::Checkbox("Units swim, float and drown", &swimming)) {
			ActorWater::SetEnabled(swimming);
		}
		bool burningUnits = ActorFire::IsEnabled();
		if (ImGui::Checkbox("Units catch fire", &burningUnits)) {
			ActorFire::SetEnabled(burningUnits);
		}
		bool smokeBlocks = SmokeGrid::IsEnabled();
		if (ImGui::Checkbox("Smoke blocks sight", &smokeBlocks)) {
			SmokeGrid::SetEnabled(smokeBlocks);
		}
		ImGui::Checkbox("Headlamps at night", &settings.Headlamps);
		ImGui::SameLine();
		ImGui::Checkbox("Night limits AI sight", &settings.NightAffectsAI);
		float timeScale = g_TimerMan.GetTimeScale();
		if (ImGui::SliderFloat("Game speed", &timeScale, 0.1F, 4.0F, "%.2fx", ImGuiSliderFlags_Logarithmic)) {
			g_TimerMan.SetTimeScale(timeScale);
		}
		if (ImGui::Button("Normal speed")) {
			g_TimerMan.SetTimeScale(1.0F);
		}

		ImGui::Separator();
		if (ImGui::Button("Graphics Lab")) {
			m_ShowGraphicsLab = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Performance")) {
			m_ShowPerformanceMan = !m_ShowPerformanceMan;
		}
		ImGui::SameLine();
		if (ImGui::Button("Save settings")) {
			g_PostProcessMan.AdoptAtmosphereAsPlayers();
			g_SettingsMan.UpdateSettingsFile();
		}
	}
	ImGui::End();
}

void DebugMan::EndPhotoMode() {
	if (!m_PhotoKeepLook) {
		// Keep the time of day and weather the player had; photo mode's look changes were for the photo.
		g_PostProcessMan.GetLightingSettings() = m_PhotoSavedSettings;
	}
	g_TimerMan.PauseSim(false);
	g_FrameMan.SetHudDisabled(m_PhotoPreviousHUDDisabled, 0);
	m_PhotoModeActive = false;
}

void DebugMan::PhotoModeGUI() {
	bool inActivity = g_ActivityMan.IsInActivity() && g_SceneMan.GetScene();
	if (inActivity && !m_PhotoModeActive) {
		m_PhotoModeActive = true;
		m_PhotoSavedSettings = g_PostProcessMan.GetLightingSettings();
		m_PhotoPreviousHUDDisabled = g_FrameMan.IsHudDisabled(0);
		m_PhotoCameraCenter = g_CameraMan.GetOffset(0) + Vector(static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F, static_cast<float>(g_FrameMan.GetPlayerScreenHeight()) * 0.5F);
	}
	if (m_PhotoModeActive) {
		g_TimerMan.PauseSim(m_PhotoFreeze);
		g_FrameMan.SetHudDisabled(m_PhotoHideHUD, 0);

		// Free camera: drag with the right mouse button anywhere outside the window, or the arrow keys.
		ImGuiIO& io = ImGui::GetIO();
		float pixelsPerScreenPixel = static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) / std::max(1.0F, io.DisplaySize.x);
		if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			m_PhotoCameraCenter -= Vector(io.MouseDelta.x, io.MouseDelta.y) * pixelsPerScreenPixel;
		}
		float keySpeed = 400.0F * io.DeltaTime * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0F : 1.0F);
		if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
			m_PhotoCameraCenter.m_X -= keySpeed;
		}
		if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
			m_PhotoCameraCenter.m_X += keySpeed;
		}
		if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
			m_PhotoCameraCenter.m_Y -= keySpeed;
		}
		if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
			m_PhotoCameraCenter.m_Y += keySpeed;
		}
		g_SceneMan.WrapPosition(m_PhotoCameraCenter);
		g_CameraMan.SetScrollTarget(m_PhotoCameraCenter, 1.0F, 0);
	}

	ImGui::SetNextWindowSize(ImVec2(330.0F, 0.0F), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 345.0F, 40.0F), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Photo Mode (F8)", &m_ShowPhotoMode)) {
		if (!inActivity) {
			ImGui::TextWrapped("Start a game to use photo mode.");
		} else {
			LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
			ImGui::Checkbox("Freeze time", &m_PhotoFreeze);
			ImGui::SameLine();
			ImGui::Checkbox("Hide HUD", &m_PhotoHideHUD);
			ImGui::TextDisabled("Camera: drag with right mouse, or arrow keys (Shift = faster)");

			ImGui::SeparatorText("Look");
			ImGui::SliderFloat("Hour", &settings.TimeOfDay, 0.0F, 24.0F, "%.2f");
			ImGui::Combo("Weather", &settings.WeatherType, "Clear\0Rain\0Snow\0Ash fall\0Dust storm\0");
			ImGui::SliderFloat("Exposure", &settings.Exposure, 0.2F, 3.0F);
			ImGui::SliderFloat("Saturation", &settings.Saturation, 0.0F, 2.0F);
			ImGui::SliderFloat("Contrast", &settings.Contrast, 0.5F, 1.6F);
			ImGui::SliderFloat("Temperature", &settings.Temperature, -1.0F, 1.0F);
			ImGui::SliderFloat("Tint", &settings.Tint, -1.0F, 1.0F);
			ImGui::SliderFloat("Vignette", &settings.Vignette, 0.0F, 1.0F);
			ImGui::SliderFloat("Bloom", &settings.BloomIntensity, 0.0F, 3.0F);
			ImGui::SliderFloat("Haze", &settings.AtmosphereHaze, 0.0F, 1.0F);
			ImGui::SliderFloat("God rays", &settings.GodRays, 0.0F, 2.0F);
			ImGui::SliderFloat("Film grain", &settings.FilmGrain, 0.0F, 1.0F);
			ImGui::SliderFloat("Chromatic aberration", &settings.ChromaticAberration, 0.0F, 4.0F);
			if (ImGui::Button("Reset look")) {
				settings = m_PhotoSavedSettings;
			}
			ImGui::SameLine();
			ImGui::Checkbox("Keep look changes", &m_PhotoKeepLook);

			ImGui::Separator();
			ImGui::Combo("Resolution", &m_PhotoScale, "As shown in the window\0"
			                                          "2x (1920x1080)\0"
			                                          "3x\0"
			                                          "4x (3840x2160)\0");
			if (ImGui::Button("Take screenshot", ImVec2(-1.0F, 0.0F))) {
				m_ScreenshotRequested = true;
			}
			ImGui::TextDisabled("Saved to the ScreenShots folder.");
		}
	}
	ImGui::End();
}

void DebugMan::GraphicsLabGUI() {
	if (ImGui::Begin("Graphics Lab", &m_ShowGraphicsLab)) {
		LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
		const ImGuiColorEditFlags linearColorFlags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;

		ImGui::SeparatorText("Sky and ambient light");
		ImGui::Checkbox("Lighting enabled", &settings.Enabled);
		ImGui::ColorEdit3("Ambient (linear)", &settings.Ambient.x, linearColorFlags);
		ImGui::ColorEdit3("Sky (linear)", &settings.SkyColor.x, linearColorFlags);
		ImGui::ColorEdit3("Foreground floor (linear)", &settings.ForegroundAmbient.x, linearColorFlags);
		ImGui::SliderFloat("Air falloff", &settings.AirFalloff, 0.8F, 0.995F, "%.3f");
		ImGui::SliderFloat("Terrain falloff", &settings.SolidFalloff, 0.1F, 0.95F, "%.2f");
		ImGui::SliderInt("Propagation steps/frame", &settings.PropagationIterationsPerFrame, 1, 32);
		ImGui::SliderFloat("God rays", &settings.GodRays, 0.0F, 2.0F);
		ImGui::SliderFloat("Atmosphere haze", &settings.AtmosphereHaze, 0.0F, 1.0F);
		ImGui::ColorEdit3("Atmosphere (linear)", &settings.AtmosphereColor.x, linearColorFlags);
		ImGui::SliderFloat("Time of day (h)", &settings.TimeOfDay, 0.0F, 24.0F, "%.2f");
		ImGui::SliderFloat("Day length (min, 0 = fixed)", &settings.DayLengthMinutes, 0.0F, 60.0F, "%.1f");

		ImGui::SeparatorText("Weather");
		ImGui::Combo("Precipitation", &settings.WeatherType, "Clear\0Rain\0Snow\0Ash fall\0Dust storm\0");
		ImGui::SliderFloat("Intensity##Weather", &settings.WeatherIntensity, 0.0F, 1.0F);
		ImGui::SliderFloat("Wind (px/s)", &settings.Wind, -400.0F, 400.0F);

		ImGui::SeparatorText("Glows and dynamic lights");
		ImGui::SliderFloat("Glow light intensity", &settings.GlowLightIntensity, 0.0F, 8.0F);
		ImGui::SliderFloat("Glow light radius", &settings.GlowLightRadiusScale, 0.5F, 10.0F);
		ImGui::SliderFloat("Shadow strength", &settings.ShadowStrength, 0.0F, 1.0F);
		ImGui::SliderFloat("Shadows of units and objects", &settings.UnitShadows, 0.0F, 1.0F);
		ImGui::SliderFloat("Sun and moon shadows", &settings.SunShadows, 0.0F, 1.0F);
		ImGui::SliderFloat("Contact shading", &settings.ContactShading, 0.0F, 1.0F);
		ImGui::SliderFloat("Emissive intensity", &settings.EmissiveIntensity, 0.0F, 4.0F);
		ImGui::SliderFloat("Edge lighting", &settings.EdgeLighting, 0.0F, 1.0F);
		ImGui::SliderFloat("Shine (metal, wet ground)", &settings.Specular, 0.0F, 3.0F);
		ImGui::SliderFloat("CRT scanlines", &settings.Scanlines, 0.0F, 1.0F);
		ImGui::SliderFloat("Indirect light", &settings.IndirectLight, 0.0F, 1.5F);
		ImGui::Checkbox("Radiance cascades GI", &settings.RadianceCascades);
		ImGui::SliderFloat("GI strength", &settings.GIStrength, 0.0F, 4.0F);
		ImGui::SliderFloat("GI bounce", &settings.GIBounce, 0.0F, 1.0F);

		ImGui::SeparatorText("Distortion");
		ImGui::Checkbox("Distortion enabled", &settings.DistortionEnabled);
		ImGui::SliderFloat("Heat haze (px)", &settings.HeatHaze, 0.0F, 6.0F);
		ImGui::SliderFloat("Shockwave strength", &settings.ShockwaveStrength, 0.0F, 3.0F);

		ImGui::SeparatorText("Terrain effects");
		ImGui::Checkbox("Scorch marks", &settings.ScorchMarks);
		ImGui::Checkbox("Blood, oil and water stains", &settings.Stains);
		ImGui::Checkbox("Living world (sway, snow, wet ground)", &settings.LivingWorld);
		ImGui::SliderFloat("Embers", &settings.Embers, 0.0F, 3.0F);
		ImGui::SliderFloat("Sparks, dust and debris", &settings.EffectsParticles, 0.0F, 3.0F);
		ImGui::SliderFloat("Smoke scattering", &settings.SmokeScattering, 0.0F, 3.0F);
		ImGui::Text("Effects particles alive: %d", EffectsParticles::GetCount());
		ImGui::SliderFloat("Hot spot cooling (s)", &settings.HotSpotSeconds, 0.0F, 10.0F);

		ImGui::SeparatorText("Bloom");
		ImGui::Checkbox("Bloom enabled", &settings.BloomEnabled);
		ImGui::SliderFloat("Threshold", &settings.BloomThreshold, 0.0F, 4.0F);
		ImGui::SliderFloat("Knee", &settings.BloomKnee, 0.01F, 1.0F);
		ImGui::SliderFloat("Intensity", &settings.BloomIntensity, 0.0F, 3.0F);

		ImGui::SeparatorText("Tonemapping and grading");
		ImGui::SliderFloat("Exposure", &settings.Exposure, 0.1F, 4.0F);
		ImGui::SliderFloat("Auto exposure", &settings.AutoExposure, 0.0F, 1.0F);
		ImGui::SliderFloat("Adapt below", &settings.AutoExposureLow, 0.001F, 0.2F, "%.3f", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Adapt above", &settings.AutoExposureHigh, 0.05F, 2.0F, "%.3f", ImGuiSliderFlags_Logarithmic);
		if (SceneLighting* lighting = g_PostProcessMan.GetSceneLighting(); lighting && settings.AutoExposure > 0.0F) {
			float averageLuminance = 0.0F;
			float autoExposure = 1.0F;
			lighting->ReadAutoExposure(averageLuminance, autoExposure);
			ImGui::Text("Scene luminance %.3f, auto exposure x%.2f", averageLuminance, autoExposure);
		}
		ImGui::SliderFloat("Highlight shoulder", &settings.ShoulderStart, 0.3F, 1.0F);
		ImGui::SliderFloat("Saturation", &settings.Saturation, 0.0F, 2.0F);
		ImGui::SliderFloat("Vignette", &settings.Vignette, 0.0F, 1.0F);
		ImGui::SliderFloat("Temperature", &settings.Temperature, -1.0F, 1.0F);
		ImGui::SliderFloat("Tint", &settings.Tint, -1.0F, 1.0F);
		ImGui::SliderFloat("Contrast", &settings.Contrast, 0.5F, 1.6F);
		ImGui::ColorEdit3("Shadow tint", &settings.ShadowTint.x, linearColorFlags);
		ImGui::ColorEdit3("Highlight tint", &settings.HighlightTint.x, linearColorFlags);
		ImGui::SliderFloat("Film grain", &settings.FilmGrain, 0.0F, 1.0F);
		ImGui::SliderFloat("Chromatic aberration (px)", &settings.ChromaticAberration, 0.0F, 4.0F);

		ImGui::SeparatorText("Debug");
		ImGui::Combo("View", &settings.DebugView, "Final image\0Lighting on grey\0Sky light only\0Dynamic light only\0Normals\0Distortion\0GI only (radiance cascades)\0Solid objects and distance to them\0Where the sun is visible\0");

		ImGui::SeparatorText("Stats");
		if (SceneLighting* lighting = g_PostProcessMan.GetSceneLighting()) {
			ImGui::Text("Light grid: %d x %d cells of %dpx", lighting->GetGridWidth(), lighting->GetGridHeight(), lighting->GetGridCellSize());
			ImGui::Text("Dynamic lights last screen: %d", lighting->GetLastLightCount());
			int atlasPages = 0;
			int atlasTextures = 0;
			BitmapTexture::GetAtlasStats(atlasPages, atlasTextures);
			ImGui::Text("Sprite atlas: %d sprites on %d pages", atlasTextures, atlasPages);
		}

		ImGui::Separator();
		if (ImGui::Button("Reset to defaults")) {
			settings = LightingSettings();
		}
		ImGui::SameLine();
		if (ImGui::Button("Save to Settings.ini")) {
			g_PostProcessMan.AdoptAtmosphereAsPlayers();
			g_SettingsMan.UpdateSettingsFile();
		}
	}
	ImGui::End();
}

void DebugMan::DebugOptionsGUI() {
	if (ImGui::Begin("Debug Options", &m_ShowDebugWindow)) {
		ImGui::Checkbox("Show Performance Stats", &m_ShowPerformanceMan);
		ImGui::Checkbox("Show Graphics Lab", &m_ShowGraphicsLab);
		ImGui::Checkbox("Show World Debug (F6)", &m_ShowWorldDebug);
		ImGui::Checkbox("Show ImGui Demo Window", &m_ImGuiDemoWindow);
		ImGui::Checkbox("Show Actor debug", &m_ShowActorDebugGui);
		if (ImGui::TreeNode("Debug Draw")) {
			ImGui::Checkbox("Draw Camera bounds", &m_DrawCameraBounds);

			if (ImGui::TreeNode("Sprite Draw")) {
				ImGui::Checkbox("Draw frustum tests", &m_DrawSpriteBounds);
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Free Cam")) {
				ImGui::Checkbox("Enable Free Cam", &m_EnableFreeCam);
				if (m_EnableFreeCam && g_SceneMan.GetScene()) {
					ImDrawList* draw_list = ImGui::GetWindowDrawList();
					ImVec2 p = ImGui::GetCursorScreenPos();
					float maxWidth = ImGui::GetContentRegionAvail().x;
					Vector sceneDim = g_SceneMan.GetSceneDim();
					float aspectRatio = sceneDim.m_Y / sceneDim.m_X;
					static ImVec2 freeCamPos{0.0f, 0.0f};

					float height = maxWidth * aspectRatio;
					Box viewport = Box(Vector(0.0f, 0.0f), g_FrameMan.GetPlayerScreenWidth(), g_FrameMan.GetPlayerScreenHeight());
					float viewToSceneScale = viewport.m_Width / sceneDim.m_X;

					float minimapToSceneScale = sceneDim.m_X / maxWidth;

					m_FreeCam = std::make_unique<Camera>(Vector(freeCamPos.x, freeCamPos.y) * minimapToSceneScale, viewport, m_FreeCamZoom);

					draw_list->AddRectFilled(p, ImVec2(p.x + maxWidth, p.y + height), ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]));
					draw_list->AddRect(p, ImVec2(p.x + maxWidth, p.y + height), ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_Border]));
					draw_list->AddRectFilled(ImVec2(p.x + freeCamPos.x, p.y + freeCamPos.y), ImVec2(p.x + freeCamPos.x + maxWidth * viewToSceneScale, p.y + freeCamPos.y + height * viewToSceneScale), ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_Button]));
					draw_list->AddRect(ImVec2(p.x + freeCamPos.x, p.y + freeCamPos.y), ImVec2(p.x + freeCamPos.x + maxWidth * viewToSceneScale, p.y + freeCamPos.y + height * viewToSceneScale), ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_Border]));

					float zoomX = (maxWidth * viewToSceneScale) / 2.0f * (1.f / m_FreeCamZoom - 1.0f);
					float zoomY = (height * viewToSceneScale) / 2.0f * (1.f / m_FreeCamZoom - 1.0f);

					draw_list->AddRect(ImVec2(p.x + freeCamPos.x - zoomX, p.y + freeCamPos.y - zoomY), ImVec2(p.x + freeCamPos.x + maxWidth * viewToSceneScale + zoomX, p.y + freeCamPos.y + height * viewToSceneScale + zoomY), ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_Separator]));

					ImGui::InvisibleButton("##FreeCamMap", ImVec2(maxWidth, height));

					ImGuiIO& io = ImGui::GetIO();
					if (ImGui::IsItemActive()) {
						freeCamPos.x = io.MousePos.x - p.x;
						freeCamPos.y = io.MousePos.y - p.y;
					}
					ImGui::Text("FreeCamPos: {%.1f; %.1f}", freeCamPos.x, freeCamPos.y);
					ImGui::Text("FreeCamPos: {%.1f; %.1f}", freeCamPos.x + maxWidth * viewToSceneScale, freeCamPos.y + height * viewToSceneScale);
					ImGui::InputFloat("Zoom", &m_FreeCamZoom, 0.1f, 0.5f);
				}
				ImGui::TreePop();
			}

			ImGui::TreePop();
		}
	}
	ImGui::End();
}


void DebugMan::ActorDrawDebugGUI() {
	ZoneScoped;
	static std::shared_ptr<RenderBatch> batch = std::make_unique<RenderBatch>();
	if (ImGui::Begin("Actor Draw Debug", &m_ShowActorDebugGui)) {
		static std::map<MovableObject*, std::unique_ptr<Texture>> MOTargets;
		static int playerScreen = -1;
		ImGui::InputInt("Test Draw for Screen (-1 full world):", &playerScreen);
		ImGui::SliderInt("Screen", &playerScreen, -1, c_MaxScreenCount);
		if (g_SceneMan.GetScene()) {
			ZoneScopedN("ActorList");
			g_RenderMan.SetActiveBatch(batch.get());
			g_RenderMan.BeginFrame();
			if (playerScreen >= 0) {
				ZoneScopedN("ActorList::DrawPlayer");
				for (auto& camera: g_CameraMan.GetPlayerCameras(playerScreen)) {
					g_MovableMan.Draw(camera);
				}
			} else {
				ZoneScopedN("ActorList::DrawAll");
				Camera camera{{0.0f, 0.0f}, {{0.0f, 0.0f}, (float)g_SceneMan.GetSceneHeight(), (float)g_SceneMan.GetSceneWidth()}};
				g_MovableMan.Draw(camera);
			}
			g_RenderMan.ResetActiveBatch();
			batch->EndFrame();
			{
				ZoneScopedN("ActorList::List");
				for (auto actor: g_MovableMan.m_Actors) {
					if (ImGui::TreeNode(actor->GetPresetNameAndUniqueID().c_str())) {
						ZoneScopedN("ActorList::List::Node");
						if (!MOTargets[actor]) {
							MOTargets[actor] = std::make_unique<Texture>(FloatRect{0.0f, 0.0f, actor->GetRadius() * 2.0f, 2.f * actor->GetRadius()});
						}
						if (!m_DebugDrawTarget) {
							m_DebugDrawTarget = std::make_unique<RenderTarget>(false);
						}
						m_DebugDrawTarget->Begin(true, false);
						glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, MOTargets[actor]->GetTextureId(), 0);
						glViewport(0, 0, 2 * actor->GetRadius(), 2 * actor->GetRadius());
						Camera camera(actor->GetPos() - Vector(actor->GetRadius(), actor->GetRadius()), Box({0.0f, 0.0f}, actor->GetRadius() * 2, actor->GetRadius() * 2.0f));
						batch->m_CurrentCamera = &camera;
						batch->Render();
						glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
						ImGui::ImageWithBg(MOTargets[actor]->GetTextureId(), ImVec2(MOTargets[actor]->GetDimensions().w, MOTargets[actor]->GetDimensions().h));
						ImGui::TreePop();
					}
				}
			}
			batch->ClearDraws();
		}
	}
	ImGui::End();
}
