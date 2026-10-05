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

float DebugMan::GetPanelWidth(PanelSide side) const {
	// The right side holds the settings panel, which has its list of categories beside its controls, so it is the wider.
	float displayWidth = ImGui::GetIO().DisplaySize.x;
	return side == PanelSide::Left ? std::min(m_PanelWidth * GetToolScale(), displayWidth * 0.32F) : std::min(m_PanelWidth * 1.4F * GetToolScale(), displayWidth * 0.4F);
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
		for (int side = 0; side < 2; ++side) {
			m_PanelsLastFrame[side] = m_PanelsThisFrame[side];
			m_PanelsThisFrame[side] = 0;
		}
		g_WindowMan.SetReservedSpace(m_DockPanels && m_PanelsLastFrame[0] > 0 ? static_cast<int>(GetPanelWidth(PanelSide::Left)) : 0, m_DockPanels && m_PanelsLastFrame[1] > 0 ? static_cast<int>(GetPanelWidth(PanelSide::Right)) : 0);
	}

	// The modern HUD, unless photo mode is hiding the HUD.
	if (!IsPhotoModeHidingHUD()) {
		ModernHUD::Draw();
	}

	// The old separate windows are now parts of the one settings panel: asking for one opens it at that part.
	if (m_ShowGraphicsLab) {
		m_ShowGraphicsLab = false;
		m_ShowWorldDebug = true;
		m_SettingsCategory = 1;
	}
	if (m_ShowDebugWindow) {
		m_ShowDebugWindow = false;
		m_ShowWorldDebug = true;
		m_SettingsCategory = 10;
	}
	if (m_ShowWorldDebug) {
		SettingsGUI();
	}

	Sandbox::DrawGUI();

	if (m_ShowPhotoMode) {
		PhotoModeGUI();
	} else if (m_PhotoModeActive) {
		EndPhotoMode();
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

}

bool DebugMan::BeginPanel(const char* name, bool* open, PanelSide side) {
	if (!m_DockPanels) {
		m_PanelKind = 0;
		return ImGui::Begin(name, open);
	}
	ImGuiIO& io = ImGui::GetIO();
	float width = GetPanelWidth(side);
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
	// Looking around the Sandbox game mode from above uses the pointer too, even with every tool window hidden.
	bool wantMouse = AnyToolWindowOpen() || Sandbox::IsLookingAround();
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

void DebugMan::FreeCamGUI() {
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
