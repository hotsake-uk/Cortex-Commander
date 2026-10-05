#include "DebugMan.h"
#include <unordered_map>
#include "Actor.h"
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

float DebugMan::GetToolScale() const {
	return std::max(std::clamp(ImGui::GetIO().DisplaySize.y / 720.0F, 1.0F, 2.5F) * m_ToolScale, 0.5F);
}

void DebugMan::DrawToolWindowControls() {
	if (ImGui::TreeNode("Size and layout of these windows")) {
		// Typed in and applied on Enter (or with the + and - buttons): a slider would resize the window under the mouse as it was dragged.
		float toolScale = m_ToolScale;
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0F);
		if (ImGui::InputFloat("Size of text and controls", &toolScale, 0.05F, 0.1F, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue)) {
			m_ToolScale = std::clamp(toolScale, 0.4F, 1.5F);
		}
		ImGui::SetItemTooltip("How big the tool windows are drawn: type a number from 0.4 to 1.5 and press Enter. 1 is the old size; 0.7 is the usual.");
		float panelWidth = m_PanelWidth;
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0F);
		if (ImGui::InputFloat("Panel width", &panelWidth, 20.0F, 60.0F, "%.0f", ImGuiInputTextFlags_EnterReturnsTrue)) {
			m_PanelWidth = std::clamp(panelWidth, 240.0F, 700.0F);
		}
		ImGui::SetItemTooltip("How wide the side panels are, before the size above: type a number from 240 to 700 and press Enter.");
		ImGui::Checkbox("Dock tool windows at the sides", &m_DockPanels);
		ImGui::SetItemTooltip("On: tool windows are panels beside the game's picture. Off: they float over it and can be moved.");
		ImGui::TreePop();
	}
}

void DebugMan::DrawImGui() {
	UpdateMouseOwnership();

	// Debug windows keep a readable size on big windows and handheld screens: scale with the window height (720 px = 1x).
	{
		ImGuiIO& io = ImGui::GetIO();
		float uiScale = GetToolScale();
		float fontScale = uiScale * g_WindowMan.GetImGuiFontBaseScale();
		if (std::abs(io.FontGlobalScale - fontScale) > 0.001F) {
			io.FontGlobalScale = fontScale;
			ImGuiStyle& style = ImGui::GetStyle();
			style = ImGuiStyle();
			ImGui::StyleColorsDark(&style);
			style.ScaleAllSizes(uiScale);
		}
	}

	// Docked tool panels: the game's picture is fitted between the ones that were open last frame.
	{
		ImGuiIO& io = ImGui::GetIO();
		float uiScale = GetToolScale();
		int panelWidth = static_cast<int>(std::min(m_PanelWidth * uiScale, io.DisplaySize.x * 0.32F));
		for (int side = 0; side < 2; ++side) {
			m_PanelsLastFrame[side] = m_PanelsThisFrame[side];
			m_PanelsThisFrame[side] = 0;
		}
		g_WindowMan.SetReservedSpace(m_DockPanels && m_PanelsLastFrame[0] > 0 ? panelWidth : 0, m_DockPanels && m_PanelsLastFrame[1] > 0 ? panelWidth : 0);
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

bool DebugMan::BeginPanel(const char* name, bool* open, PanelSide side) {
	if (!m_DockPanels) {
		m_PanelKind = 0;
		return ImGui::Begin(name, open);
	}
	ImGuiIO& io = ImGui::GetIO();
	float uiScale = GetToolScale();
	float width = std::min(m_PanelWidth * uiScale, io.DisplaySize.x * 0.32F);
	int sideIndex = side == PanelSide::Left ? 0 : 1;
	++m_PanelsThisFrame[sideIndex];
	// Each side of the window is one panel the full height of it, and the tool windows docked there are its tabs.
	ImGui::SetNextWindowPos(ImVec2(side == PanelSide::Left ? 0.0F : io.DisplaySize.x - width, 0.0F), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(width, io.DisplaySize.y), ImGuiCond_Always);
	ImGui::Begin(side == PanelSide::Left ? "##ToolsLeft" : "##ToolsRight", nullptr,
	             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::BeginTabBar("##ToolTabs", ImGuiTabBarFlags_FittingPolicyScroll);
	// A tool window that has just been opened comes to the front.
	static std::unordered_map<ImGuiID, int> lastSeen;
	int frame = ImGui::GetFrameCount();
	int& seen = lastSeen[ImGui::GetID(name)];
	ImGuiTabItemFlags flags = seen != 0 && seen < frame - 1 ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
	seen = frame;
	if (ImGui::BeginTabItem(name, open, flags)) {
		m_PanelKind = 2;
		ImGui::BeginChild("##Body");
		return true;
	}
	m_PanelKind = 1;
	return false;
}

void DebugMan::EndPanel() {
	if (m_PanelKind == 0) {
		ImGui::End();
		return;
	}
	if (m_PanelKind == 2) {
		ImGui::EndChild();
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
	ImGui::End();
	m_PanelKind = 0;
}

namespace {
	enum ToolBit : unsigned { ToolSandbox = 1, ToolWorld = 2, ToolGraphics = 4, ToolOptions = 8, ToolActorDraw = 16, ToolPerformance = 32 };
}

bool DebugMan::AnyToolWindowOpen() const {
	return Sandbox::IsOpen() || m_ShowWorldDebug || m_ShowPhotoMode || m_ShowGraphicsLab || m_ShowDebugWindow || m_ShowActorDebugGui || m_ImGuiDemoWindow || m_ShowPerformanceMan;
}

void DebugMan::CloseTools() {
	unsigned open = (Sandbox::IsOpen() ? ToolSandbox : 0U) | (m_ShowWorldDebug ? ToolWorld : 0U) | (m_ShowGraphicsLab ? ToolGraphics : 0U) | (m_ShowDebugWindow ? ToolOptions : 0U) | (m_ShowActorDebugGui ? ToolActorDraw : 0U) |
	                (m_ShowPerformanceMan ? ToolPerformance : 0U);
	if (open != 0) {
		m_RememberedTools = open;
	}
	Sandbox::SetOpen(false);
	m_ShowWorldDebug = m_ShowGraphicsLab = m_ShowDebugWindow = m_ShowActorDebugGui = m_ShowPerformanceMan = m_ImGuiDemoWindow = false;
	// Photo mode puts back what it changed when its window is seen to be shut.
	m_ShowPhotoMode = false;
}

void DebugMan::OpenTools() {
	// The first time: the sandbox at one side, the world and the look of it at the other.
	unsigned open = m_RememberedTools != 0 ? m_RememberedTools : (ToolSandbox | ToolWorld | ToolGraphics);
	if (Sandbox::IsGodMode()) {
		open |= ToolSandbox;
	}
	Sandbox::SetOpen((open & ToolSandbox) != 0);
	m_ShowWorldDebug = (open & ToolWorld) != 0;
	m_ShowGraphicsLab = (open & ToolGraphics) != 0;
	m_ShowDebugWindow = (open & ToolOptions) != 0;
	m_ShowActorDebugGui = (open & ToolActorDraw) != 0;
	m_ShowPerformanceMan = (open & ToolPerformance) != 0;
}

void DebugMan::ToggleTools(bool atPointer) {
	if (AnyToolWindowOpen()) {
		CloseTools();
		Sandbox::OnToolsClosed(atPointer);
	} else {
		OpenTools();
	}
}

void DebugMan::UpdateMouseOwnership() {
	bool wantMouse = AnyToolWindowOpen();
	if (wantMouse) {
		// In game the mouse is trapped in relative mode for aiming, which ImGui can't use; release it while tool windows are open.
		// Checked every frame, not only when a window opens: coming back from another program hands the mouse to the game again, which with tool windows open
		// shut the pointer into the game's picture, out of reach of the windows.
		if (!g_UInputMan.IsMouseReleased()) {
			g_UInputMan.DisableMouseMoving(true);
		}
		m_ReleasedMouseForImGui = true;
	} else if (m_ReleasedMouseForImGui) {
		m_ReleasedMouseForImGui = false;
		// Not while the window is in the background: it gets the mouse back by itself when it comes to the front.
		if (g_WindowMan.AnyWindowHasFocus()) {
			g_UInputMan.DisableMouseMoving(false);
		}
	}
	// The game hides the OS cursor and draws its own, so have ImGui draw one too.
	ImGui::GetIO().MouseDrawCursor = wantMouse;
}

void DebugMan::WorldDebugGUI() {
	ImGui::SetNextWindowSize(ImVec2(340.0F, 0.0F), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(10.0F, 40.0F), ImGuiCond_FirstUseEver);
	if (BeginPanel("World (F6)###WorldDebug", &m_ShowWorldDebug, PanelSide::Right)) {
		LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
		DrawToolWindowControls();

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
		// The interior light takes the floor with it: the floor is the least light units and solid ground ever get, so left behind it would keep them bright in a dark room.
		float interiorBefore = std::max({settings.Ambient.x, settings.Ambient.y, settings.Ambient.z});
		brightnessSlider("Interior / cave light", settings.Ambient, 1.0F);
		float interiorNow = std::max({settings.Ambient.x, settings.Ambient.y, settings.Ambient.z});
		if (interiorNow != interiorBefore) {
			float floorLevel = std::max({settings.ForegroundAmbient.x, settings.ForegroundAmbient.y, settings.ForegroundAmbient.z});
			settings.ForegroundAmbient = floorLevel > 0.001F ? settings.ForegroundAmbient * (interiorNow * 0.83F / floorLevel) : glm::vec3(interiorNow * 0.83F);
		}
		brightnessSlider("Least light on units and ground", settings.ForegroundAmbient, 1.0F);
		ImGui::SetItemTooltip("Units and solid ground never get darker than this, indoors or out. The slider above sets it too; move this one afterwards to keep them more visible than the walls behind.");
		bool showPaths = Actor::ShowAIPaths();
		if (ImGui::Checkbox("Show the paths of units moving under AI", &showPaths)) {
			Actor::SetShowAIPaths(showPaths);
		}
		ImGui::SetItemTooltip("The dotted yellow line from a unit to where it's been told to go. Off, only the unit you're controlling shows its path.");
		ImGui::Checkbox("Aiming dots light the scene", &settings.AimDotsLight);
		ImGui::SetItemTooltip("The dots that show where a weapon points always glow. On, they also cast light on what is around them.");
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
		ImGui::TextDisabled("(%d pieces moving, %d pixels fell)", TerrainCollapse::GetFallingCount(), TerrainCollapse::GetCollapsedCount());
		bool buildingsFall = TerrainCollapse::BuildingsFall();
		if (ImGui::Checkbox("Pieces of buildings fall too", &buildingsFall)) {
			TerrainCollapse::SetBuildingsFall(buildingsFall);
		}
		if (ImGui::TreeNode("What falls, and how")) {
			TerrainCollapse::Tuning& tuning = TerrainCollapse::GetTuning();
			ImGui::Checkbox("Floating masses stay up when chipped", &tuning.FloatingStays);
			ImGui::SetItemTooltip("On: a mass that was already hanging in the air before a blast stays; cut in two, the bigger part stays and the smaller falls. Off: anything touching nothing falls.");
			ImGui::SliderInt("Thin neck that snaps (pixels)", &tuning.NeckWidth, 0, 16);
			ImGui::SetItemTooltip("A piece left joined to the rest by a neck no wider than this breaks off and falls. 0: only pieces cut right through fall.");
			ImGui::SliderInt("Biggest piece that can fall (pixels)", &tuning.MaxPiecePixels, 500, 200000, "%d", ImGuiSliderFlags_Logarithmic);
			ImGui::SetItemTooltip("Anything bigger counts as the world and never falls. 30,000 is about a 170 by 170 block.");
			ImGui::SliderInt("Smallest loose bit of a building that falls", &tuning.MinFittingPixels, 0, 2000);
			ImGui::SetItemTooltip("Smaller loose bits of building material stay put: lamps, signs and consoles are drawn hanging in mid-air.");
			ImGui::SliderFloat("How hard explosions throw loose pieces", &tuning.BlastPush, 0.0F, 3.0F, "%.2f");
			ImGui::SetItemTooltip("0: explosions don't move loose pieces at all. Higher: pieces still moving are thrown harder, and more of the pieces lying at rest near a blast are picked up and thrown.");
			ImGui::SliderInt("Loose scraps it flattens (pixels)", &tuning.CrushPixels, 0, 300);
			ImGui::SetItemTooltip("A falling piece goes through loose bits of ground up to this size (leftover scraps of wall, nuggets, grains) instead of getting stuck on them. Never more than a quarter of its own size. 0: everything holds it up.");
			ImGui::SliderFloat("How hard a landing before a piece cracks", &tuning.BreakStrength, 0.2F, 5.0F, "%.2fx");
			ImGui::SliderFloat("Seconds lying still before it's ground again", &tuning.RestSeconds, 0.2F, 15.0F, "%.1f");
			if (ImGui::Button("Back to the usual##collapse")) {
				tuning = TerrainCollapse::Tuning();
			}
			ImGui::TreePop();
		}
		bool liquids = FluidSim::IsEnabled();
		if (ImGui::Checkbox("Flowing liquids", &liquids)) {
			FluidSim::SetEnabled(liquids);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%d moving, %.2f ms)", FluidSim::GetActiveCount(), FluidSim::GetLastUpdateMS());
		bool freezing = FluidSim::FreezingEnabled();
		if (ImGui::Checkbox("Still water freezes over in snow", &freezing)) {
			FluidSim::SetFreezingEnabled(freezing);
		}
		bool powders = FluidSim::PowdersEnabled();
		if (ImGui::Checkbox("Loose sand and snow slide", &powders)) {
			FluidSim::SetPowdersEnabled(powders);
		}
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
	EndPanel();
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
		float pixelsPerScreenPixel = static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) / std::max(1.0F, g_WindowMan.GetGameViewRect().w);
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
	if (BeginPanel("Photo (F8)###PhotoMode", &m_ShowPhotoMode, PanelSide::Right)) {
		if (!inActivity) {
			ImGui::TextWrapped("Start a game to use photo mode.");
		} else {
			LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
			ImGui::Checkbox("Freeze time", &m_PhotoFreeze);
			ImGui::SameLine();
			ImGui::Checkbox("Hide HUD", &m_PhotoHideHUD);
			ImGui::TextDisabled("Camera: drag with right mouse, or arrow keys (Shift = faster)");

			ImGui::SeparatorText("Look");
			if (ImGui::Button("Natural##Look")) {
				settings.ApplyLook(LightingSettings::LookNatural);
			}
			ImGui::SameLine();
			if (ImGui::Button("Gritty##Look")) {
				settings.ApplyLook(LightingSettings::LookGritty);
			}
			ImGui::SameLine();
			if (ImGui::Button("Vivid##Look")) {
				settings.ApplyLook(LightingSettings::LookVivid);
			}
			ImGui::SameLine();
			if (ImGui::Button("Noir##Look")) {
				settings.ApplyLook(LightingSettings::LookNoir);
			}
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
	EndPanel();
}

void DebugMan::GraphicsLabGUI() {
	if (BeginPanel("Graphics###GraphicsLab", &m_ShowGraphicsLab, PanelSide::Right)) {
		LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
		DrawToolWindowControls();
		const ImGuiColorEditFlags linearColorFlags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;

		ImGui::SeparatorText("Sky and ambient light");
		ImGui::Checkbox("Lighting enabled", &settings.Enabled);
		// One slider for how bright interiors and caves are without any lamps: it scales the ambient light and the foreground floor together, keeping their tints.
		// Turned down, bunkers are lit by their lamps and go dark where those are shot out.
		float ambientLevel = std::max({settings.Ambient.x, settings.Ambient.y, settings.Ambient.z});
		if (ImGui::SliderFloat("Ambient lighting", &ambientLevel, 0.01F, 1.0F, "%.2f")) {
			float floorLevel = std::max({settings.ForegroundAmbient.x, settings.ForegroundAmbient.y, settings.ForegroundAmbient.z});
			float before = std::max({settings.Ambient.x, settings.Ambient.y, settings.Ambient.z});
			settings.Ambient = before > 0.001F ? settings.Ambient * (ambientLevel / before) : glm::vec3(ambientLevel);
			// The floor (the least light units and solid ground get) goes just under it, so units in a dark room are dark too.
			settings.ForegroundAmbient = floorLevel > 0.001F ? settings.ForegroundAmbient * (ambientLevel * 0.83F / floorLevel) : glm::vec3(ambientLevel * 0.83F);
		}
		ImGui::SetItemTooltip("How bright interiors and caves are without lamps. Lower it and bunkers are lit by their lamps.");
		ImGui::ColorEdit3("Ambient (linear)", &settings.Ambient.x, linearColorFlags);
		ImGui::ColorEdit3("Sky (linear)", &settings.SkyColor.x, linearColorFlags);
		ImGui::ColorEdit3("Foreground floor (linear)", &settings.ForegroundAmbient.x, linearColorFlags);
		ImGui::SliderFloat("Air falloff", &settings.AirFalloff, 0.8F, 0.995F, "%.3f");
		ImGui::SliderFloat("Terrain falloff", &settings.SolidFalloff, 0.1F, 0.95F, "%.2f");
		ImGui::SliderInt("Propagation steps/frame", &settings.PropagationIterationsPerFrame, 1, 32);
		ImGui::SliderFloat("Darker in the dead of night", &settings.DeepNightDarkness, 0.0F, 0.95F);
		ImGui::SetItemTooltip("How much less light there is on the scene from eleven to two than at nightfall. 0.5 is half. Lamps, fires and headlamps aren't dimmed.");
		ImGui::SliderFloat("Sky takes the hour's colours", &settings.SkyFollowsTime, 0.0F, 1.0F);
		ImGui::SetItemTooltip("Away from midday the sky art (painted as a blue day) is recoloured: dark at night, red at dawn and dusk, grey in bad weather. 0: the art is only darkened.");
		ImGui::SliderFloat("God rays", &settings.GodRays, 0.0F, 2.0F);
		ImGui::SliderFloat("Atmosphere haze", &settings.AtmosphereHaze, 0.0F, 1.0F);
		ImGui::ColorEdit3("Atmosphere (linear)", &settings.AtmosphereColor.x, linearColorFlags);
		ImGui::SliderFloat("Time of day (h)", &settings.TimeOfDay, 0.0F, 24.0F, "%.2f");
		ImGui::SliderFloat("Day length (min, 0 = fixed)", &settings.DayLengthMinutes, 0.0F, 60.0F, "%.1f");

		ImGui::SeparatorText("Weather");
		ImGui::Combo("Precipitation", &settings.WeatherType, "Clear\0Rain\0Snow\0Ash fall\0Dust storm\0");
		ImGui::SliderFloat("Intensity##Weather", &settings.WeatherIntensity, 0.0F, 1.0F);
		ImGui::SliderFloat("Wind (px/s)", &settings.Wind, -400.0F, 400.0F);
		ImGui::SliderFloat("Weather's own light", &settings.WeatherLight, 0.0F, 1.5F);
		ImGui::SetItemTooltip("The least light rain, snow, ash and dust are drawn with, so they show on a dark night.");
		ImGui::SeparatorText("Smoke");
		ImGui::SliderFloat("Soft smoke", &settings.SoftSmoke, 0.0F, 3.0F);
		ImGui::SetItemTooltip("Every puff of the game's smoke trails soft, billowing smoke as well, so it hangs and rolls. 0: only the game's own smoke sprites.");
		ImGui::SeparatorText("Water");
		ImGui::SliderFloat("Light glowing through water", &settings.WaterLightGlow, 0.0F, 1.5F);
		ImGui::SetItemTooltip("How much a lamp, fire or blast in or beside water shows as a glow in the water, in the light's own colour. 0: water is only lit like a surface.");
		ImGui::SeparatorText("Pouring water");
		ImGui::SliderFloat("Froth", &settings.WaterFoam, 0.0F, 1.5F);
		ImGui::SetItemTooltip("Thin, broken water (a stream off a ledge, the lip of a pour) is drawn as froth, and froth fills the air beside it. 0 for none.");
		ImGui::SliderFloat("Froth on stray drops", &settings.WaterFoamStray, 0.0F, 1.0F);
		ImGui::SetItemTooltip("How much of that a pixel or two of water thrown clear of the rest gets. 0: stray drops stay single pixels. 1: as much as a stream.");
		ImGui::SliderFloat("Froth bubbling", &settings.WaterFoamBubbles, 0.0F, 2.0F);
		ImGui::SetItemTooltip("How much the froth flickers lighter and darker. 0: smooth, like still water. 1: lively.");
		ImGui::SliderFloat("Froth brightness", &settings.WaterFoamBrightness, 0.2F, 2.0F);
		ImGui::SliderFloat("Froth glow in the dark", &settings.WaterFoamGlow, 0.0F, 1.0F);
		ImGui::SetItemTooltip("How much light of its own froth carries. 0: it is as dark as the scene around it at night.");
		ImGui::SliderFloat("Mist", &settings.WaterMist, 0.0F, 2.0F);
		ImGui::SetItemTooltip("Soft spray thrown off water that is falling fast or landing. 0 for none.");
		ImGui::SliderFloat("Mist puff size", &settings.WaterMistSize, 0.1F, 3.0F);
		ImGui::SetItemTooltip("How big each puff is. 1 is about 3 to 6 pixels across at first.");
		ImGui::SliderFloat("Mist puff spread", &settings.WaterMistSpread, 0.0F, 3.0F);
		ImGui::SetItemTooltip("How much each puff swells as it thins. 0: it stays the size it starts.");
		ImGui::SliderFloat("Mist puff life", &settings.WaterMistLife, 0.2F, 4.0F);
		ImGui::SetItemTooltip("How long each puff lasts. 1 is about half a second to a second.");
		ImGui::SliderFloat("Mist puff opacity", &settings.WaterMistOpacity, 0.0F, 1.0F);
		ImGui::SliderFloat("Mist brightness", &settings.WaterMistBrightness, 0.2F, 2.0F);
		ImGui::SliderFloat("Mist glow in the dark", &settings.WaterMistGlow, 0.0F, 1.0F);
		ImGui::SetItemTooltip("The least light mist is drawn with. 0: it is as dark as the scene around it at night.");
		ImGui::SeparatorText("Weather effects");
		ImGui::SliderFloat("Rain splashes", &settings.RainSplashes, 0.0F, 2.0F);
		ImGui::SetItemTooltip("Little splashes where rain lands on ground, water, roofs and units. 0 for none.");

		ImGui::SeparatorText("Glows and dynamic lights");
		ImGui::SliderFloat("Glow light intensity", &settings.GlowLightIntensity, 0.0F, 8.0F);
		ImGui::SliderFloat("Glow light radius", &settings.GlowLightRadiusScale, 0.5F, 10.0F);
		ImGui::SliderFloat("Shadow strength", &settings.ShadowStrength, 0.0F, 1.0F);
		ImGui::SliderFloat("Shadows of units and objects", &settings.UnitShadows, 0.0F, 1.0F);
		ImGui::SliderFloat("Sun and moon shadows", &settings.SunShadows, 0.0F, 1.0F);
		ImGui::SliderFloat("Contact shading", &settings.ContactShading, 0.0F, 1.0F);
		ImGui::SliderFloat("Sun in the sky", &settings.SunDisc, 0.0F, 2.0F);
		ImGui::SliderFloat("Cloud shadows", &settings.CloudShadows, 0.0F, 1.0F);
		ImGui::SliderFloat("Emissive intensity", &settings.EmissiveIntensity, 0.0F, 4.0F);
		ImGui::SliderFloat("Edge lighting", &settings.EdgeLighting, 0.0F, 1.0F);
		ImGui::SliderFloat("Shine (metal, wet ground)", &settings.Specular, 0.0F, 3.0F);
		ImGui::SliderFloat("Metal reflections", &settings.Metals, 0.0F, 2.0F);
		ImGui::Checkbox("Wet, sooty, snowy and hot surfaces", &settings.SurfaceStates);
		ImGui::Checkbox("Tracers light what they pass", &settings.TracerLights);
		ImGui::SliderFloat("Far background blur", &settings.BackgroundBlur, 0.0F, 1.5F);
		ImGui::TextUnformatted("Looks:");
		ImGui::SameLine();
		if (ImGui::Button("Natural")) {
			settings.ApplyLook(LightingSettings::LookNatural);
		}
		ImGui::SameLine();
		if (ImGui::Button("Gritty")) {
			settings.ApplyLook(LightingSettings::LookGritty);
		}
		ImGui::SameLine();
		if (ImGui::Button("Vivid")) {
			settings.ApplyLook(LightingSettings::LookVivid);
		}
		ImGui::SameLine();
		if (ImGui::Button("Noir")) {
			settings.ApplyLook(LightingSettings::LookNoir);
		}
		ImGui::SliderFloat("Surface relief", &settings.Relief, 0.0F, 1.5F);
		ImGui::SliderFloat("CRT scanlines", &settings.Scanlines, 0.0F, 1.0F);
		ImGui::SliderFloat("Indirect light", &settings.IndirectLight, 0.0F, 1.5F);
		ImGui::Checkbox("Radiance cascades GI", &settings.RadianceCascades);
		ImGui::SliderFloat("GI strength", &settings.GIStrength, 0.0F, 4.0F);
		ImGui::SliderFloat("GI bounce", &settings.GIBounce, 0.0F, 1.0F);

		ImGui::SeparatorText("Light colors");
		ImGui::SliderFloat("Light color strength", &settings.LightSaturation, 0.0F, 2.5F);
		ImGui::SetItemTooltip("How colorful the light of lamps, glows, flashes and fire is. 0 makes all light white.");
		ImGui::ColorEdit3("Tint on all lights (linear)", &settings.LightTint.x, linearColorFlags);

		ImGui::SeparatorText("Scenery lamps");
		ImGui::SliderFloat("Lamp brightness", &settings.LampBrightness, 0.0F, 4.0F);
		ImGui::SliderFloat("Lamp reach", &settings.LampReach, 0.25F, 3.0F);
		ImGui::ColorEdit3("Lamp tint (linear)", &settings.LampTint.x, linearColorFlags);

		ImGui::SeparatorText("Headlamps");
		ImGui::Checkbox("Headlamps", &settings.Headlamps);
		ImGui::Checkbox("On by day as well", &settings.HeadlampsByDay);
		ImGui::SliderFloat("Beam brightness", &settings.HeadlampBrightness, 0.0F, 5.0F);
		ImGui::SliderFloat("Beam reach (px)", &settings.HeadlampReach, 40.0F, 600.0F);
		ImGui::SliderFloat("Beam width (degrees)", &settings.HeadlampWidth, 5.0F, 80.0F);
		ImGui::ColorEdit3("Beam color (linear)", &settings.HeadlampColor.x, linearColorFlags);
		ImGui::SliderFloat("Glow around the lamp", &settings.HeadlampGlow, 0.0F, 2.0F);
		ImGui::SliderFloat("Team color in the beam", &settings.HeadlampTeamTint, 0.0F, 1.0F);

		ImGui::SeparatorText("Tracers");
		ImGui::SliderFloat("Tracer glow", &settings.TracerGlow, 0.0F, 1.0F);
		ImGui::SetItemTooltip("Tracers and their trails shine in their own color and bloom.");
		ImGui::SliderFloat("Tracer light brightness", &settings.TracerLightBrightness, 0.0F, 3.0F);
		ImGui::SliderFloat("Tracer light reach (px)", &settings.TracerLightReach, 8.0F, 120.0F);
		ImGui::SliderFloat("Tracer light randomness", &settings.TracerLightRandomness, 0.0F, 1.0F);
		ImGui::SetItemTooltip("How much tracers' lights differ from one another in size and brightness, and waver as they fly. 0: all alike and steady. A small value: each a little different.");

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
	EndPanel();
}

void DebugMan::DebugOptionsGUI() {
	if (BeginPanel("Options###DebugOptions", &m_ShowDebugWindow, PanelSide::Right)) {
		DrawToolWindowControls();
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
	EndPanel();
}


void DebugMan::ActorDrawDebugGUI() {
	ZoneScoped;
	static std::shared_ptr<RenderBatch> batch = std::make_unique<RenderBatch>();
	if (BeginPanel("Actor draw###ActorDraw", &m_ShowActorDebugGui, PanelSide::Right)) {
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
	EndPanel();
}
