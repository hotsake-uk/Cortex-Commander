#include "DebugMan.h"
#include "DebugDraw.h"
#include "DebugOverlays.h"
#include <unordered_map>
#include "Actor.h"
#include "WindowMan.h"
#include "PerformanceMan.h"
#include "imgui/imgui.h"
#include "ToolWidgets.h"
#include "tracy/Tracy.hpp"
#include "Draw.h"
#include "RenderTarget.h"
#include "RenderBatch.h"
#include "RenderMan.h"
#include "MovableMan.h"
#include "ConsoleMan.h"
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
#include "imgui/backends/imgui_impl_opengl3.h"
#include "ContentFile.h"
#include "PresetMan.h"
#include <filesystem>
#include <vector>

using namespace RTE;

void Draw() {
}

float DebugMan::GetToolScale() const {
	return std::max(std::clamp(ImGui::GetIO().DisplaySize.y / 720.0F, 1.0F, 2.5F) * m_ToolScale, 0.5F);
}

namespace {
	/// Dresses the tool windows in the game's own menu colours (the olive panels, parchment text and gold of its skins) with square, hard-edged shapes.
	void ApplyGameTheme(ImGuiStyle& style) {
		auto rgb = [](int r, int g, int b, float a = 1.0F) { return ImVec4(static_cast<float>(r) / 255.0F, static_cast<float>(g) / 255.0F, static_cast<float>(b) / 255.0F, a); };
		const ImVec4 panel = rgb(38, 46, 32, 0.93F);
		const ImVec4 panelDark = rgb(24, 29, 21);
		const ImVec4 field = rgb(57, 75, 42);
		const ImVec4 fieldHover = rgb(85, 96, 68);
		const ImVec4 fieldActive = rgb(105, 121, 71);
		const ImVec4 gold = rgb(242, 182, 61);
		const ImVec4 goldDim = rgb(170, 128, 48);
		const ImVec4 parchment = rgb(232, 224, 190);
		ImVec4* colors = style.Colors;
		colors[ImGuiCol_Text] = parchment;
		colors[ImGuiCol_TextDisabled] = rgb(150, 150, 120);
		colors[ImGuiCol_WindowBg] = panel;
		colors[ImGuiCol_ChildBg] = rgb(0, 0, 0, 0.0F);
		colors[ImGuiCol_PopupBg] = rgb(30, 37, 26, 0.98F);
		colors[ImGuiCol_Border] = goldDim;
		colors[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0.0F);
		colors[ImGuiCol_FrameBg] = field;
		colors[ImGuiCol_FrameBgHovered] = fieldHover;
		colors[ImGuiCol_FrameBgActive] = fieldActive;
		colors[ImGuiCol_TitleBg] = panelDark;
		colors[ImGuiCol_TitleBgActive] = field;
		colors[ImGuiCol_TitleBgCollapsed] = panelDark;
		colors[ImGuiCol_MenuBarBg] = panelDark;
		colors[ImGuiCol_ScrollbarBg] = panelDark;
		colors[ImGuiCol_ScrollbarGrab] = fieldHover;
		colors[ImGuiCol_ScrollbarGrabHovered] = fieldActive;
		colors[ImGuiCol_ScrollbarGrabActive] = gold;
		colors[ImGuiCol_CheckMark] = gold;
		colors[ImGuiCol_SliderGrab] = gold;
		colors[ImGuiCol_SliderGrabActive] = rgb(255, 214, 110);
		colors[ImGuiCol_Button] = field;
		colors[ImGuiCol_ButtonHovered] = fieldHover;
		colors[ImGuiCol_ButtonActive] = goldDim;
		colors[ImGuiCol_Header] = field;
		colors[ImGuiCol_HeaderHovered] = fieldHover;
		colors[ImGuiCol_HeaderActive] = goldDim;
		colors[ImGuiCol_Separator] = goldDim;
		colors[ImGuiCol_SeparatorHovered] = gold;
		colors[ImGuiCol_SeparatorActive] = gold;
		colors[ImGuiCol_ResizeGrip] = fieldHover;
		colors[ImGuiCol_ResizeGripHovered] = gold;
		colors[ImGuiCol_ResizeGripActive] = gold;
		colors[ImGuiCol_Tab] = panelDark;
		colors[ImGuiCol_TabHovered] = fieldHover;
		colors[ImGuiCol_TabSelected] = field;
		colors[ImGuiCol_TabSelectedOverline] = gold;
		colors[ImGuiCol_TabDimmed] = panelDark;
		colors[ImGuiCol_TabDimmedSelected] = field;
		colors[ImGuiCol_PlotHistogram] = gold;
		colors[ImGuiCol_TextSelectedBg] = rgb(170, 128, 48, 0.6F);
		colors[ImGuiCol_NavHighlight] = gold;
		// Hard edges throughout: nothing in the game's own menus is rounded.
		style.WindowRounding = style.ChildRounding = style.FrameRounding = style.PopupRounding = style.ScrollbarRounding = style.GrabRounding = style.TabRounding = 0.0F;
		style.WindowBorderSize = 2.0F;
		style.ChildBorderSize = 1.0F;
		style.FrameBorderSize = 0.0F;
		style.TabBarBorderSize = 2.0F;
		style.TabBarOverlineSize = 2.0F;
		style.SeparatorTextBorderSize = 2.0F;
		style.GrabMinSize = 10.0F;
	}
} // namespace

void DebugMan::PrepareFonts() {
	// Once, as soon as the game's art can be loaded (the palette has to be set first, which the loading screen sees to).
	if (m_PixelFonts[0] || m_PixelFontTries > 600) {
		return;
	}
	if (++m_PixelFontTries < 5) {
		return;
	}
	std::string path = "Base.rte/GUIs/Skins/FontSmall.png";
	if (!std::filesystem::exists(g_PresetMan.GetFullModulePath(path))) {
		m_PixelFontTries = 1000;
		return;
	}
	BITMAP* sheet = ContentFile(path.c_str()).GetAsBitmap(COLORCONV_NONE, false);
	if (!sheet || bitmap_color_depth(sheet) != 8) {
		if (sheet) {
			destroy_bitmap(sheet);
		}
		return;
	}
	// The sheet is rows of sixteen letters from the space on. A marker colour (the top left pixel) sits between letters on the first row of each line and
	// down the left edge at the start of each line; the top right pixel is the background.
	int marker = sheet->line[0][0];
	int background = sheet->line[0][sheet->w - 1];
	int lineHeight = 0;
	for (int y = 1; y < sheet->h; ++y) {
		if (sheet->line[y][0] == marker) {
			lineHeight = y;
			break;
		}
	}
	struct Letter {
		int Code, X, Y, Width;
	};
	std::vector<Letter> letters;
	if (lineHeight > 0) {
		int x = 1;
		int y = 0;
		int onLine = 0;
		for (int code = 32; code < 256 && y + lineHeight <= sheet->h; ++code) {
			int width = 0;
			while (x + width < sheet->w && sheet->line[y][x + width] != marker) {
				++width;
			}
			if (width > 0) {
				letters.push_back({code, x, y, width});
			}
			x += width + 1;
			if (++onLine >= 16) {
				onLine = 0;
				x = 1;
				y += lineHeight;
			}
		}
	}
	if (letters.size() < 60) {
		destroy_bitmap(sheet);
		m_PixelFontTries = 1000;
		return;
	}
	PALETTE palette;
	get_palette(palette);
	int brightest = 1;
	for (int i = 0; i < 256; ++i) {
		brightest = std::max({brightest, static_cast<int>(palette[i].r), static_cast<int>(palette[i].g), static_cast<int>(palette[i].b)});
	}
	int channelScale = brightest <= 63 ? 4 : 1;

	ImGuiIO& io = ImGui::GetIO();
	std::vector<std::vector<int>> rects(4);
	for (int size = 1; size <= 4; ++size) {
		ImFontConfig config;
		config.SizePixels = static_cast<float>(lineHeight * size);
		config.PixelSnapH = true;
		config.OversampleH = config.OversampleV = 1;
		ImFont* font = io.Fonts->AddFontDefault(&config);
		m_PixelFonts[size - 1] = font;
		for (const Letter& letter: letters) {
			rects[size - 1].push_back(io.Fonts->AddCustomRectFontGlyph(font, static_cast<ImWchar>(letter.Code), letter.Width * size, lineHeight * size, static_cast<float>((letter.Code == ' ' ? letter.Width : std::max(letter.Width - 1, 2)) * size)));
		}
	}
	unsigned char* pixels = nullptr;
	int atlasWidth = 0;
	int atlasHeight = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
	for (int size = 1; size <= 4; ++size) {
		for (size_t i = 0; i < letters.size(); ++i) {
			const ImFontAtlasCustomRect* rect = io.Fonts->GetCustomRectByIndex(rects[size - 1][i]);
			const Letter& letter = letters[i];
			if (!rect || !rect->IsPacked()) {
				continue;
			}
			for (int y = 0; y < rect->Height; ++y) {
				for (int x = 0; x < rect->Width; ++x) {
					int index = sheet->line[letter.Y + y / size][letter.X + x / size];
					unsigned char* pixel = pixels + (static_cast<size_t>(rect->Y + y) * atlasWidth + rect->X + x) * 4;
					if (index == background || index == marker) {
						pixel[0] = pixel[1] = pixel[2] = 255;
						pixel[3] = 0;
						continue;
					}
					// The letters' own shades are kept as shades of whatever colour the text is drawn in: the fill is white, the outline dark.
					int level = std::min((palette[index].r + palette[index].g + palette[index].b) * channelScale / 3, 255);
					pixel[0] = pixel[1] = pixel[2] = static_cast<unsigned char>(level);
					pixel[3] = 255;
				}
			}
		}
	}
	destroy_bitmap(sheet);
	// The picture of the fonts that the drawing code holds is made again from the new atlas.
	ImGui_ImplOpenGL3_DestroyFontsTexture();
	ImGui_ImplOpenGL3_CreateFontsTexture();
}

GameViewRect DebugMan::GetUncoveredView() const {
	GameViewRect view = g_WindowMan.GetGameViewRect();
	if (m_DockPanels && m_PanelsOverlay) {
		float left = m_PanelsLastFrame[0] > 0 ? GetPanelWidth(PanelSide::Left) : 0.0F;
		float right = m_PanelsLastFrame[1] > 0 ? GetPanelWidth(PanelSide::Right) : 0.0F;
		float from = std::max(view.x, left);
		float to = std::min(view.x + view.w, ImGui::GetIO().DisplaySize.x - right);
		if (to - from > 100.0F) {
			view.x = from;
			view.w = to - from;
		}
	}
	return view;
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
		ToolUI::Checkbox("The game's own pixel lettering", &m_PixelFont);
		ImGui::SetItemTooltip("On: these windows are lettered in the game's small pixel font. Off: a smooth font, which is easier to read at length.");
		ToolUI::Checkbox("Dock tool windows at the sides", &m_DockPanels);
		ImGui::SetItemTooltip("On: tool windows are panels at the sides of the window. Off: they float and can be moved.");
		if (m_DockPanels) {
			ToolUI::Checkbox("Panels lie over the picture", &m_PanelsOverlay);
			ImGui::SetItemTooltip("On: the game's picture keeps its full size and the panels cover its edges. Off: the picture is fitted into the space between the panels.");
		}
		ImGui::TreePop();
	}
}

void DebugMan::UpdateFreeze() {
	if (m_FreezeSim && g_ActivityMan.IsInActivity()) {
		g_TimerMan.PauseSim(true);
		m_FrozeSim = true;
		if (m_FreezeStepsWanted > 0) {
			g_TimerMan.StepSim(1);
			--m_FreezeStepsWanted;
		}
	} else if (m_FrozeSim) {
		m_FrozeSim = false;
		m_FreezeStepsWanted = 0;
		// Unpaused unless photo mode or the sandbox's open window wants the world still; they set their own pause again each frame they want it.
		if (!IsPhotoModeOpen()) {
			g_TimerMan.PauseSim(false);
		}
	}
}

void DebugMan::DrawOverlays() {
	if (!g_ActivityMan.IsInActivity() || !g_SceneMan.GetScene()) {
		return;
	}
	// The inspect key, Ctrl+I: pins the unit under the pointer for the overlays and AI tracing, or unpins it. (Units selected in the sandbox and the one a player controls are inspected anyway.)
	if (ImGuiIO& io = ImGui::GetIO(); !io.WantCaptureKeyboard && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) {
		Vector pointer = DebugDraw::MouseScenePosition();
		Actor* nearest = nullptr;
		float nearestDistance = 40.0F * DebugDraw::ScenePixelsPerWindowPixel();
		for (Actor* actor: g_MovableMan.GetActorList()) {
			float distance = g_SceneMan.ShortestDistance(pointer, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetMagnitude();
			if (distance < nearestDistance) {
				nearest = actor;
				nearestDistance = distance;
			}
		}
		if (nearest) {
			nearest->SetDebugInspected(!nearest->IsDebugPinned());
			g_ConsoleMan.PrintString(std::string(nearest->IsDebugPinned() ? "Inspecting " : "No longer inspecting ") + nearest->GetPresetName() + " #" + std::to_string(nearest->GetUniqueID()));
		}
	}
	// Each overlay: a check of its setting and its draw call, drawn into ImGui::GetForegroundDrawList() with DebugDraw::ToScreen.
	DebugOverlays::DrawUnitInspector();
	DebugOverlays::DrawCombatOverlay();
	DebugOverlays::DrawNavNode();
	DebugOverlays::DrawRecentSolves();
	DebugOverlays::DrawTerrainUpdates();
	DebugOverlays::DrawLightSources();
	DebugOverlays::DrawSunDirection();
}

void DebugMan::DrawImGui() {
	UpdateMouseOwnership();

	// Debug windows keep a readable size on big windows and handheld screens: scale with the window height (720 px = 1x).
	{
		ImGuiIO& io = ImGui::GetIO();
		float uiScale = GetToolScale();
		// The game's pixel font is drawn at a whole size, picked to come out about as big as the smooth font would have; the smooth font scales freely.
		int pixelSize = std::clamp(static_cast<int>(std::lround(uiScale * 1.7F)), 1, 4);
		ImFont* pixelFont = m_PixelFont ? m_PixelFonts[pixelSize - 1] : nullptr;
		ImFont* wanted = pixelFont ? pixelFont : io.Fonts->Fonts[0];
		m_PixelFontInUse = pixelFont != nullptr && io.FontDefault == pixelFont;
		float fontScale = pixelFont ? 1.0F : uiScale * g_WindowMan.GetImGuiFontBaseScale();
		static float styledFor = 0.0F;
		if (std::abs(io.FontGlobalScale - fontScale) > 0.001F || io.FontDefault != wanted || styledFor != uiScale) {
			io.FontGlobalScale = fontScale;
			io.FontDefault = wanted;
			styledFor = uiScale;
			ImGuiStyle& style = ImGui::GetStyle();
			style = ImGuiStyle();
			ImGui::StyleColorsDark(&style);
			ApplyGameTheme(style);
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
		// Panels either lie over the picture or push it into the space between them.
		bool pushes = m_DockPanels && !m_PanelsOverlay;
		g_WindowMan.SetReservedSpace(pushes && m_PanelsLastFrame[0] > 0 ? static_cast<int>(GetPanelWidth(PanelSide::Left)) : 0, pushes && m_PanelsLastFrame[1] > 0 ? static_cast<int>(GetPanelWidth(PanelSide::Right)) : 0);
	}

	// The modern HUD and the debug overlays, unless photo mode is hiding the HUD.
	if (!IsPhotoModeHidingHUD()) {
		ModernHUD::Draw();
		DrawOverlays();
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
	UpdateFreeze();

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
			g_UInputMan.GiveMouseBackNow();
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
	// (Not when the sandbox holds the world still: it would pause it again only on its next draw, and the frame between ran every sim update
	// the paused time had saved up, a jump as photo mode closed.)
	if (!Sandbox::WantsWorldPaused()) {
		g_TimerMan.PauseSim(false);
	}
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
			ToolUI::Checkbox("Freeze time", &m_PhotoFreeze);
			ImGui::SameLine();
			ToolUI::Checkbox("Hide HUD", &m_PhotoHideHUD);
			ImGui::TextDisabled("Camera: drag with right mouse, or arrow keys (Shift = faster)");

			ImGui::SeparatorText("Look");
			if (ToolUI::Button("Natural##Look")) {
				settings.ApplyLook(LightingSettings::LookNatural);
			}
			ImGui::SameLine();
			if (ToolUI::Button("Gritty##Look")) {
				settings.ApplyLook(LightingSettings::LookGritty);
			}
			ImGui::SameLine();
			if (ToolUI::Button("Vivid##Look")) {
				settings.ApplyLook(LightingSettings::LookVivid);
			}
			ImGui::SameLine();
			if (ToolUI::Button("Noir##Look")) {
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
			if (ToolUI::Button("Reset look")) {
				settings = m_PhotoSavedSettings;
			}
			ImGui::SameLine();
			ToolUI::Checkbox("Keep look changes", &m_PhotoKeepLook);

			ImGui::Separator();
			ImGui::Combo("Resolution", &m_PhotoScale, "As shown in the window\0"
			                                          "2x (1920x1080)\0"
			                                          "3x\0"
			                                          "4x (3840x2160)\0");
			if (ToolUI::Button("Take screenshot", ImVec2(-1.0F, 0.0F))) {
				m_ScreenshotRequested = true;
			}
			ImGui::TextDisabled("Saved to the ScreenShots folder.");
		}
	}
	EndPanel();
}

void DebugMan::FreeCamGUI() {
	if (ImGui::TreeNode("Free Cam")) {
		ToolUI::Checkbox("Enable Free Cam", &m_EnableFreeCam);
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
