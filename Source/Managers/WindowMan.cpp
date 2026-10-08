#include "WindowMan.h"
#include "PerformanceMan.h"
#include "GameVersion.h"
#include "TextOverlay.h"
#include <filesystem>
#include "loadpng.h"
#include "System.h"
#include <array>
#include <ctime>
#include "RTEError.h"
#include "SDL3/SDL.h"
#include "SettingsMan.h"
#include "FrameMan.h"
#include "ActivityMan.h"
#include "UInputMan.h"
#include "ConsoleMan.h"
#include "PresetMan.h"
#include "PostProcessMan.h"
#include "RenderTarget.h"
#include "GLStateMan.h"
#include "RenderMan.h"
#include "DebugMan.h"
#include "Draw.h"

#include "GLCheck.h"
#include <SDL3/SDL.h>
#include "glad/gl.h"
#include "raylib/raylib.h"
#include "raylib/rlgl.h"
#include "Shader.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/epsilon.hpp"
#include "tracy/Tracy.hpp"
#include "tracy/TracyOpenGL.hpp"

#include "GUI/imgui/imgui.h"
#include "GUI/imgui/backends/imgui_impl_sdl3.h"
#include "GUI/imgui/backends/imgui_impl_opengl3.h"

#ifdef __linux__
#include "Resources/cccp.xpm"
#include <SDL3_image/SDL_image.h>
#endif

using namespace RTE;

void SDLWindowDeleter::operator()(SDL_Window* window) const { SDL_DestroyWindow(window); }
void SDLRendererDeleter::operator()(SDL_Renderer* renderer) const { SDL_DestroyRenderer(renderer); }
void SDLTextureDeleter::operator()(SDL_Texture* texture) const { SDL_DestroyTexture(texture); }

void SDLContextDeleter::operator()(SDL_GLContext context) const { SDL_GL_DestroyContext(context); }

void WindowMan::Clear() {
	m_EventQueue.clear();
	m_FocusEventsDispatchedByMovingBetweenWindows = false;
	m_FocusEventsDispatchedByDisplaySwitchIn = false;

	m_PrimaryWindow.reset();
	m_BackBuffer32Texture = 0;
	m_ScreenVAO = 0;
	m_ScreenVBO = 0;
	ClearMultiDisplayData();

	m_AnyWindowHasFocus = false;
	m_ResolutionChanged = false;

	m_NumDisplays = 0;
	m_MaxResX = 0;
	m_MaxResY = 0;
	m_ValidDisplayIndicesAndBoundsForMultiDisplayFullscreen.clear();
	m_CanMultiDisplayFullscreen = false;
	m_DisplayArrangmentLeftMostDisplayIndex = -1;
	m_DisplayArrangementLeftMostOffset = 0;
	m_DisplayArrangementLeftMostOffset = 0;

	m_PrimaryWindowDisplayIndex = 0;
	m_PrimaryWindowDisplayWidth = 0;
	m_PrimaryWindowDisplayHeight = 0;
	m_ResX = c_DefaultResX;
	m_ResY = c_DefaultResY;
	m_ResMultiplier = 1;
	m_Fullscreen = false;
	m_EnableVSync = true;
	m_UseMultiDisplays = false;
}

void WindowMan::ClearMultiDisplayData() {
	m_MultiDisplayTextureOffsets.clear();
	m_MultiDisplayProjections.clear();
	m_MultiDisplayWindows.clear();
}

WindowMan::WindowMan() {
	Clear();
}

WindowMan::~WindowMan() = default;

void WindowMan::Destroy() {
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
}

void WindowMan::Initialize() {
	const char* background = std::getenv("CCCP_BACKGROUND");
	m_Background = background && background[0] != '0';
	if (m_Background) {
		m_Fullscreen = false;
	}

	SDL_free(SDL_GetDisplays(&m_NumDisplays));

	m_PrimaryWindowDisplayIndex = SDL_GetPrimaryDisplay();
	if (m_PrimaryWindowDisplayIndex == 0) {
		g_ConsoleMan.PrintString("ERROR: Failed to get primary display!" + std::string(SDL_GetError()));
		int count{0};
		SDL_DisplayID* displays = SDL_GetDisplays(&count);
		if (displays) {
			m_PrimaryWindowDisplayIndex = displays[0];
		} else {
			RTEAbort("No displays detetected somehow! " + std::string(SDL_GetError()));
		}
		SDL_free(displays);
	}

	SDL_Rect currentDisplayBounds{};
	SDL_GetDisplayBounds(m_PrimaryWindowDisplayIndex, &currentDisplayBounds);

	m_PrimaryWindowDisplayWidth = currentDisplayBounds.w;
	m_PrimaryWindowDisplayHeight = currentDisplayBounds.h;

	MapDisplays(false);

	ValidateResolution(m_ResX, m_ResY, m_ResMultiplier);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	CreatePrimaryWindow();
	InitializeOpenGL();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	ImGui::StyleColorsDark();
	// A TTF font rasterized at twice its normal size, drawn at half scale (see DebugMan), so debug windows stay sharp when scaled up for big screens.
	{
		std::string fontPath = g_PresetMan.GetFullModulePath("Base.rte/GUIs/Fonts/Roboto-Medium.ttf");
		ImFontConfig fontConfig;
		fontConfig.OversampleH = 2;
		if (std::filesystem::exists(fontPath) && io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 30.0F, &fontConfig)) {
			m_ImGuiFontBaseScale = 0.5F;
		}
	}
	ImGui_ImplSDL3_InitForOpenGL(m_PrimaryWindow.get(), m_GLContext.get());
	ImGui_ImplOpenGL3_Init("#version 330 core");
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	CreateBackBufferTexture();
	m_ScreenBlitShader = std::make_unique<Shader>(g_PresetMan.GetFullModulePath("Base.rte/Shaders/ScreenBlit.vert"), g_PresetMan.GetFullModulePath("Base.rte/Shaders/ScreenBlit.frag"));
	m_ScreenUpscaleShader = std::make_unique<Shader>(g_PresetMan.GetFullModulePath("Base.rte/Shaders/ScreenBlit.vert"), g_PresetMan.GetFullModulePath("Base.rte/Shaders/ScreenUpscale.frag"));
	m_ScreenUpscaleMaskedShader = std::make_unique<Shader>(g_PresetMan.GetFullModulePath("Base.rte/Shaders/ScreenBlit.vert"), g_PresetMan.GetFullModulePath("Base.rte/Shaders/ScreenUpscaleMasked.frag"));

	// SDL is kinda dumb about the taskbar icon so we need to poll after creating the window for it to show up, otherwise there's no icon till it starts polling in the main menu loop.
	SDL_PollEvent(nullptr);

	m_PrimaryWindowDisplayIndex = SDL_GetDisplayForWindow(m_PrimaryWindow.get());

	if (FullyCoversAllDisplays()) {
		ChangeResolutionToMultiDisplayFullscreen(m_ResMultiplier);
	} else {
		SetViewportLetterboxed();
	}

	SDL_AddEventWatch((SDL_EventFilter)WindowMan::HandleWindowExposedEvent, nullptr);
}

void WindowMan::CreatePrimaryWindow() {
	std::string windowTitle = "Cortex Command Community Project";

#ifdef DEBUG_BUILD
	windowTitle += " (Full Debug)";
#elif MIN_DEBUG_BUILD
	windowTitle += " (Min Debug)";
#elif DEBUG_RELEASE_BUILD
	windowTitle += " (Debug Release)";
#elif PROFILING_BUILD
	windowTitle += " (Profiling)";
#endif

#ifdef TARGET_MACHINE_X86
	windowTitle += " (x86)";
#endif

	int windowPosX = (m_ResX * m_ResMultiplier <= m_PrimaryWindowDisplayWidth) ? SDL_WINDOWPOS_CENTERED : (m_MaxResX - (m_ResX * m_ResMultiplier)) / 2;
	int windowPosY = SDL_WINDOWPOS_CENTERED;

	SDL_PropertiesID windowProps = SDL_CreateProperties();
	RTEAssert(windowProps, "Unable to create window properties! " + std::string(SDL_GetError()));
	SDL_SetStringProperty(windowProps, SDL_PROP_WINDOW_CREATE_TITLE_STRING, windowTitle.c_str());
	SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
	SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
	SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, m_Fullscreen);
	SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_X_NUMBER, windowPosX);
	SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_Y_NUMBER, windowPosY);
	SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, m_ResX * m_ResMultiplier);
	SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, m_ResY * m_ResMultiplier);
	// In the background (test runs): hidden and never focused, so the person at the machine keeps their mouse and keyboard.
	if (m_Background) {
		SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
		SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN, false);
		SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, false);
	}
	m_PrimaryWindow = std::shared_ptr<SDL_Window>(SDL_CreateWindowWithProperties(windowProps), SDLWindowDeleter());
	if (!m_PrimaryWindow) {
		RTEError::ShowMessageBox("Unable to create window because:\n" + std::string(SDL_GetError()) + "!\n\nTrying to revert to defaults!");

		m_ResX = c_DefaultResX;
		m_ResY = c_DefaultResY;
		m_ResMultiplier = 1;
		g_SettingsMan.SetSettingsNeedOverwrite();

		m_PrimaryWindow = std::shared_ptr<SDL_Window>(SDL_CreateWindow(windowTitle.c_str(), m_ResX * m_ResMultiplier, m_ResY * m_ResMultiplier, SDL_WINDOW_OPENGL), SDLWindowDeleter());
		if (!m_PrimaryWindow) {
			RTEAbort("Failed to create window because:\n" + std::string(SDL_GetError()));
		}
	}

	SDL_SetWindowMinimumSize(m_PrimaryWindow.get(), c_MinResX, c_MinResY);
	SDL_GL_SwapWindow(m_PrimaryWindow.get());
	SDL_SetCursor(NULL);

	if (!m_Fullscreen && IsResolutionMaximized(m_ResX, m_ResY, m_ResMultiplier)) {
		SDL_MaximizeWindow(m_PrimaryWindow.get());
	}

#ifdef __linux__
	SDL_Surface* iconSurface = IMG_ReadXPMFromArray(ccicon);
	if (iconSurface) {
		SDL_SetWindowIcon(m_PrimaryWindow.get(), iconSurface);
		SDL_DestroySurface(iconSurface);
	}
#endif
}

static void GLAPIENTRY DebugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
	// Ignore non-significant error/warning codes (NVidia drivers)
	// NOTE: Here there are the details with a sample output:
	// - #131169 - Framebuffer detailed info: The driver allocated storage for renderbuffer 2. (severity: low)
	// - #131185 - Buffer detailed info: Buffer object 1 (bound to GL_ELEMENT_ARRAY_BUFFER_ARB, usage hint is GL_ENUM_88e4)
	//             will use VIDEO memory as the source for buffer object operations. (severity: low)
	// - #131218 - Program/shader state performance warning: Vertex shader in program 7 is being recompiled based on GL state. (severity: medium)
	// - #131204 - Texture state usage warning: The texture object (0) bound to texture image unit 0 does not have
	//             a defined base level and cannot be used for texture mapping. (severity: low)
	if ((id == 131169) || (id == 131185) || (id == 131218) || (id == 131204))
		return;

	const char* msgSource = NULL;
	switch (source) {
		case GL_DEBUG_SOURCE_API:
			msgSource = "API";
			break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
			msgSource = "WINDOW_SYSTEM";
			break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER:
			msgSource = "SHADER_COMPILER";
			break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:
			msgSource = "THIRD_PARTY";
			break;
		case GL_DEBUG_SOURCE_APPLICATION:
			msgSource = "APPLICATION";
			break;
		case GL_DEBUG_SOURCE_OTHER:
			msgSource = "OTHER";
			break;
		default:
			break;
	}

	const char* msgType = NULL;
	switch (type) {
		case GL_DEBUG_TYPE_ERROR:
			msgType = "ERROR";
			break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
			msgType = "DEPRECATED_BEHAVIOR";
			break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
			msgType = "UNDEFINED_BEHAVIOR";
			break;
		case GL_DEBUG_TYPE_PORTABILITY:
			msgType = "PORTABILITY";
			break;
		case GL_DEBUG_TYPE_PERFORMANCE:
			msgType = "PERFORMANCE";
			break;
		case GL_DEBUG_TYPE_MARKER:
			msgType = "MARKER";
			break;
		case GL_DEBUG_TYPE_PUSH_GROUP:
			msgType = "PUSH_GROUP";
			break;
		case GL_DEBUG_TYPE_POP_GROUP:
			msgType = "POP_GROUP";
			break;
		case GL_DEBUG_TYPE_OTHER:
			msgType = "OTHER";
			break;
		default:
			break;
	}

	const char* msgSeverity = "DEFAULT";
	switch (severity) {
		case GL_DEBUG_SEVERITY_LOW:
			msgSeverity = "LOW";
			break;
		case GL_DEBUG_SEVERITY_MEDIUM:
			msgSeverity = "MEDIUM";
			break;
		case GL_DEBUG_SEVERITY_HIGH:
			msgSeverity = "HIGH";
			break;
		case GL_DEBUG_SEVERITY_NOTIFICATION:
			msgSeverity = "NOTIFICATION";
			break;
		default:
			break;
	}

	std::cout << "GL: OpenGL debug message: " << message << "\n";
	std::cout << "    > Type: " << msgType << "\n";
	std::cout << "    > Source = " <<  msgSource << "\n";
	std::cout << "    > Severity = " << msgSeverity << std::endl;
}

void WindowMan::InitializeOpenGL() {
	m_GLContext = std::unique_ptr<SDL_GLContextState, SDLContextDeleter>(SDL_GL_CreateContext(m_PrimaryWindow.get()));

	if (!m_GLContext) {
		RTEAbort("Failed to create OpenGL context because:\n" + std::string(SDL_GetError()));
	}

	if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
		m_GLContext = nullptr;
		RTEAbort("Failed to load GL functions!");
	}

	gladUninstallGLDebug();

	if (GLAD_GL_KHR_debug || GLAD_GL_ARB_debug_output) {
		std::cout << "Enable Debug output!" << std::endl;
		glDebugMessageCallback(DebugMessageCallback, nullptr);
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	}

#ifndef _WIN32
	SDL_GL_SetSwapInterval(m_EnableVSync ? 1 : 0);
#else
	SDL_GL_SetSwapInterval(m_Fullscreen && m_EnableVSync ? 1 : 0);
#endif

	rlLoadExtensions((void*)SDL_GL_GetProcAddress);
	rlglInit(m_ResX, m_ResY);
	TracyGpuContext;
}

void WindowMan::CreateBackBufferTexture() {
	m_ScreenBuffer = std::make_unique<RenderTarget>(FloatRect(0, 0, m_ResX, m_ResY), FloatRect(0, 0, m_ResX, m_ResY));
	m_BackBuffer32Texture = std::make_unique<Texture>(FloatRect(0, 0, m_ResX, m_ResY));
}

int WindowMan::GetWindowResX() {
	int w, h;
	SDL_GetWindowSizeInPixels(m_PrimaryWindow.get(), &w, &h);
	return w;
}

int WindowMan::GetWindowResY() {
	int w, h;
	SDL_GetWindowSizeInPixels(m_PrimaryWindow.get(), &w, &h);
	return h;
}

void WindowMan::SetVSyncEnabled(bool enable) {
	m_EnableVSync = enable;

	// Workaround for DWM frame stutter
	// See https://github.com/libsdl-org/SDL/issues/5797
#ifndef _WIN32
	int sdlEnableVSync = m_EnableVSync ? 1 : 0;
#else
	int sdlEnableVSync = m_Fullscreen && m_EnableVSync ? 1 : 0;
#endif

	SDL_GL_SetSwapInterval(sdlEnableVSync);
}

void WindowMan::RefocusWindow() const {
	if (!m_Background) SDL_RaiseWindow(m_PrimaryWindow.get());
}

void WindowMan::UpdatePrimaryDisplayInfo() {
	m_PrimaryWindowDisplayIndex = SDL_GetDisplayForWindow(m_PrimaryWindow.get());

	SDL_Rect currentDisplayBounds;
	SDL_GetDisplayBounds(m_PrimaryWindowDisplayIndex, &currentDisplayBounds);

	m_PrimaryWindowDisplayWidth = currentDisplayBounds.w;
	m_PrimaryWindowDisplayHeight = currentDisplayBounds.h;
}

SDL_Rect WindowMan::GetUsableBoundsWithDecorations(int display) {
	if (m_Fullscreen) {
		SDL_Rect displayBounds;
		SDL_GetDisplayBounds(display, &displayBounds);
		return displayBounds;
	}
	SDL_Rect displayBounds;
	SDL_GetDisplayUsableBounds(display, &displayBounds);

	int top, left, bottom, right;
	SDL_GetWindowBordersSize(m_PrimaryWindow.get(), &top, &left, &bottom, &right);
	displayBounds.x += left;
	displayBounds.y += top;
	displayBounds.w -= left + right;
	displayBounds.h -= top + bottom;
	return displayBounds;
}

bool WindowMan::IsResolutionMaximized(int resX, int resY, float resMultiplier) {
	SDL_Rect displayBounds = GetUsableBoundsWithDecorations(m_PrimaryWindowDisplayIndex);
	if (resMultiplier == 1) {
		return (resX == displayBounds.w) && (resY == displayBounds.h);
	} else {
		return glm::epsilonEqual<float>(resX * resMultiplier, displayBounds.w, resMultiplier) && glm::epsilonEqual<float>(resY * resMultiplier, displayBounds.h, resMultiplier);
	}
}

void WindowMan::MapDisplays(bool updatePrimaryDisplayInfo) {
	auto setSingleDisplayMode = [this](const std::string& errorMsg = "") {
		m_MaxResX = m_PrimaryWindowDisplayWidth;
		m_MaxResY = m_PrimaryWindowDisplayHeight;
		m_MaxResMultiplier = std::min<float>(m_MaxResX / static_cast<float>(c_MinResX), m_MaxResY / static_cast<float>(c_MinResY));
		m_NumDisplays = 1;
		m_DisplayArrangementLeftMostOffset = -1;
		m_DisplayArrangementTopMostOffset = -1;
		m_ValidDisplayIndicesAndBoundsForMultiDisplayFullscreen.clear();
		m_CanMultiDisplayFullscreen = false;
		m_UseMultiDisplays = false;
		if (!errorMsg.empty()) {
			RTEError::ShowMessageBox("Failed to map displays for multi-display fullscreen because:\n\n" + errorMsg + "!\n\nFullscreen will be limited to the display the window is positioned at!");
		}
	};

	if (updatePrimaryDisplayInfo) {
		UpdatePrimaryDisplayInfo();
	}

	SDL_DisplayID* displays = SDL_GetDisplays(&m_NumDisplays);

	if (!m_UseMultiDisplays || m_NumDisplays == 1) {
		setSingleDisplayMode();
		return;
	}

	m_ValidDisplayIndicesAndBoundsForMultiDisplayFullscreen.clear();

	int leftMostOffset = 0;
	int topMostOffset = std::numeric_limits<int>::max();
	int maxHeight = std::numeric_limits<int>::min();
	int totalWidth = 0;

	for (int i = 0; i < m_NumDisplays; ++i) {
		SDL_Rect displayBounds;
		if (SDL_GetDisplayBounds(displays[i], &displayBounds)) {
			m_ValidDisplayIndicesAndBoundsForMultiDisplayFullscreen.emplace_back(displays[i], displayBounds);

			leftMostOffset = std::min(leftMostOffset, displayBounds.x);
			topMostOffset = std::min(topMostOffset, displayBounds.y);
			maxHeight = std::max(maxHeight, displayBounds.h);

			totalWidth += displayBounds.w;
		} else {
			setSingleDisplayMode("Failed to get resolution of display " + std::to_string(displays[i]) + "!");
			return;
		}
	}

	if (m_ValidDisplayIndicesAndBoundsForMultiDisplayFullscreen.size() == 1) {
		setSingleDisplayMode("Somehow ended up with only one valid display even though " + std::to_string(m_NumDisplays) + " are available!");
		return;
	}

	std::stable_sort(m_ValidDisplayIndicesAndBoundsForMultiDisplayFullscreen.begin(), m_ValidDisplayIndicesAndBoundsForMultiDisplayFullscreen.end(),
	                 [](auto left, auto right) {
		                 return left.second.x < right.second.x;
	                 });

	for (const auto& [displayIndex, displayBounds]: m_ValidDisplayIndicesAndBoundsForMultiDisplayFullscreen) {
		// Translate display offsets to backbuffer offsets, where the top left corner is (0,0) to figure out if the display arrangement is unreasonable garbage, i.e not top or bottom edge aligned.
		// If any of the translated offsets ends up negative, or over-positive for the Y offset, disallow going into multi-display fullscreen
		// because we'll just end up with an access violation when trying to read from the backbuffer pixel array during rendering.
		// In odd display size arrangements this is valid as long as the misaligned display is somewhere between the top and bottom edge of the tallest display, as the translated offset will remain in bounds.
		int translatedOffsetX = (displayBounds.x - leftMostOffset);
		int translatedOffsetY = (displayBounds.y - topMostOffset);
		if (translatedOffsetX < 0 || translatedOffsetY < 0 || translatedOffsetY + displayBounds.h > maxHeight) {
			setSingleDisplayMode("Bad display alignment detected!\nMulti-display fullscreen currently supports only horizontal arrangements where all displays are either top or bottom edge aligned!");
			return;
		}
	}

	for (const auto& [displayIndex, displayBounds]: m_ValidDisplayIndicesAndBoundsForMultiDisplayFullscreen) {
#if SDL_VERSION_ATLEAST(2, 24, 0)
		m_DisplayArrangmentLeftMostDisplayIndex = SDL_GetDisplayForRect(&displayBounds);
		if (m_DisplayArrangmentLeftMostDisplayIndex >= 0) {
#else
		// This doesn't return the nearest display index to the point but should still be reliable enough for reasonable display arrangements.
		SDL_Point testPoint = {leftMostOffset + 1, topMostOffset + 1};
		if (SDL_PointInRect(&testPoint, &displayBounds) == true) {
#endif
			m_DisplayArrangmentLeftMostDisplayIndex = displayIndex;
			break;
		}
	}

	if (m_DisplayArrangmentLeftMostDisplayIndex >= 0) {
		m_MaxResX = totalWidth;
		m_MaxResY = maxHeight;
		m_MaxResMultiplier = std::min<float>(m_MaxResX / static_cast<float>(c_MinResX), m_MaxResY / static_cast<float>(c_MinResY));
		m_DisplayArrangementLeftMostOffset = leftMostOffset;
		m_DisplayArrangementTopMostOffset = topMostOffset;
		m_CanMultiDisplayFullscreen = true;
	} else {
		setSingleDisplayMode("Unable to determine left-most display index!");
	}
}

void WindowMan::ValidateResolution(int& resX, int& resY, float& resMultiplier) const {
	if (resX < c_MinResX || resY < c_MinResY) {
		resX = c_MinResX;
		resY = c_MinResY;
		resMultiplier = 1.0f;
		RTEError::ShowMessageBox("Resolution too low, overriding to fit!");
		g_SettingsMan.SetSettingsNeedOverwrite();
	} else if (resMultiplier > m_MaxResMultiplier) {
		resMultiplier = 1.0f;
		RTEError::ShowMessageBox("Resolution multiplier too high, overriding to fit!");
		g_SettingsMan.SetSettingsNeedOverwrite();
	}
}

void WindowMan::SetViewportLetterboxed() {
	int windowW, windowH;
	SDL_GetWindowSizeInPixels(m_PrimaryWindow.get(), &windowW, &windowH);
	double aspectRatio = m_ResX / static_cast<double>(m_ResY);
	// The picture goes in the part of the window that docked tool panels leave free.
	int reservedLeft = std::clamp(m_ReservedLeft, 0, std::max(0, windowW / 2 - 40));
	int reservedRight = std::clamp(m_ReservedRight, 0, std::max(0, windowW / 2 - 40));
	int freeW = std::max(windowW - reservedLeft - reservedRight, 80);
	int width = freeW;
	int height = (freeW / aspectRatio) + 0.5F;

	if (height > windowH) {
		height = windowH;
		width = (height * aspectRatio) + 0.5F;
	}
	// Whole-number scaling: the largest that fits, and the rest left as bars. A window too small for even one to one keeps the fitted picture.
	if (m_IntegerScaling && m_ResX > 0 && m_ResY > 0) {
		int scale = std::min(freeW / m_ResX, windowH / m_ResY);
		if (scale >= 1) {
			width = m_ResX * scale;
			height = m_ResY * scale;
		}
	}

	m_ResMultiplier = width / static_cast<float>(m_ResX);

	int offsetX = reservedLeft + (freeW / 2) - (width / 2);
	int offsetY = (windowH / 2) - (height / 2);
	m_GameViewTop = offsetY;
	m_PrimaryWindowViewport = std::make_unique<SDL_Rect>(offsetX, windowH - offsetY - height, width, height);
}

void WindowMan::SetIntegerScaling(bool integerScaling) {
	if (integerScaling != m_IntegerScaling) {
		m_IntegerScaling = integerScaling;
		if (m_PrimaryWindow) {
			SetViewportLetterboxed();
		}
	}
}

void WindowMan::SetUpscaleUniforms() const {
	const LightingSettings& lighting = g_PostProcessMan.GetLightingSettings();
	m_ScreenUpscaleShader->SetFloat("rteScanlines", g_ActivityMan.IsInActivity() ? lighting.Scanlines : 0.0F);
	m_ScreenUpscaleShader->SetInt("rteCRTStyle", std::clamp(lighting.CRTStyle, 0, 3));
	m_ScreenUpscaleShader->SetFloat("rteSharpness", std::clamp(lighting.UpscaleSharpness, 0.0F, 1.0F));
}

void WindowMan::SetReservedSpace(int left, int right) {
	if (left != m_ReservedLeft || right != m_ReservedRight) {
		m_ReservedLeft = left;
		m_ReservedRight = right;
		if (m_PrimaryWindow) {
			SetViewportLetterboxed();
		}
	}
}

GameViewRect WindowMan::GetGameViewRect() const {
	if (!m_PrimaryWindowViewport) {
		return GameViewRect{0.0F, 0.0F, 1.0F, 1.0F};
	}
	return GameViewRect{static_cast<float>(m_PrimaryWindowViewport->x), static_cast<float>(m_GameViewTop), static_cast<float>(m_PrimaryWindowViewport->w), static_cast<float>(m_PrimaryWindowViewport->h)};
}

void WindowMan::AttemptToRevertToPreviousResolution(bool revertToDefaults) {
	auto setDefaultResSettings = [this]() {
		m_ResX = c_DefaultResX;
		m_ResY = c_DefaultResY;
		m_ResMultiplier = 1;
		g_SettingsMan.UpdateSettingsFile();
	};

	bool fullscreen = true;

	if ((m_ResX * m_ResMultiplier >= m_MaxResX) && (m_ResY * m_ResMultiplier >= m_MaxResY)) {
		setDefaultResSettings();
		fullscreen = false;
	}
	SDL_SetWindowSize(m_PrimaryWindow.get(), m_ResX * m_ResMultiplier, m_ResY * m_ResMultiplier);

	if (!m_Fullscreen) {
		fullscreen = false;
		SDL_SetWindowBordered(m_PrimaryWindow.get(), true);
		SDL_SetWindowPosition(m_PrimaryWindow.get(), SDL_WINDOWPOS_CENTERED_DISPLAY(m_PrimaryWindowDisplayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(m_PrimaryWindowDisplayIndex));
	}

	bool result = SDL_SetWindowFullscreen(m_PrimaryWindow.get(), fullscreen) == 0;
	if (!result && !revertToDefaults) {
		RTEError::ShowMessageBox("Failed to revert to previous resolution settings!\nAttempting to revert to defaults!");
		setDefaultResSettings();
		AttemptToRevertToPreviousResolution(true);
	} else if (!result) {
		RTEAbort("Failed to revert to previous resolution or defaults because: \n" + std::string(SDL_GetError()) + "!");
	}
}

void WindowMan::ChangeResolution(int newResX, int newResY, float newResMultiplier, bool fullscreen, bool displaysAlreadyMapped) {

	if (m_ResX == newResX && m_ResY == newResY && glm::epsilonEqual(m_ResMultiplier, newResMultiplier, glm::epsilon<float>()) && m_Fullscreen == fullscreen) {
		return;
	}

	bool onlyResMultiplierChange = (m_ResX == newResX) && (m_ResY == newResY);

	SDL_GL_MakeCurrent(m_PrimaryWindow.get(), m_GLContext.get());
	ClearMultiDisplayData();

	if (!displaysAlreadyMapped) {
		MapDisplays();
	}
	ValidateResolution(newResX, newResY, newResMultiplier);

	bool newResFullyCoversAllDisplays = fullscreen && m_UseMultiDisplays && m_CanMultiDisplayFullscreen && (m_NumDisplays > 1);

	bool recoveredToPreviousSettings = false;

	if ((newResFullyCoversAllDisplays && !ChangeResolutionToMultiDisplayFullscreen(newResMultiplier)) || (fullscreen && !SDL_SetWindowFullscreen(m_PrimaryWindow.get(), true))) {
		RTEError::ShowMessageBox("Failed to switch to new resolution!\nAttempting to revert to previous settings!");
		AttemptToRevertToPreviousResolution();
		recoveredToPreviousSettings = true;
	} else if (!fullscreen && !newResFullyCoversAllDisplays) {
		SDL_SetWindowFullscreen(m_PrimaryWindow.get(), 0);
		if (IsResolutionMaximized(newResX, newResY, newResMultiplier)) {
			SDL_MaximizeWindow(m_PrimaryWindow.get());
		} else {
			SDL_RestoreWindow(m_PrimaryWindow.get());
			SDL_GL_SwapWindow(m_PrimaryWindow.get());
		}
		SDL_SetWindowSize(m_PrimaryWindow.get(), newResX * newResMultiplier, newResY * newResMultiplier);
		SDL_SetWindowBordered(m_PrimaryWindow.get(), true);
		SDL_SetWindowPosition(m_PrimaryWindow.get(), SDL_WINDOWPOS_CENTERED_DISPLAY(m_PrimaryWindowDisplayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(m_PrimaryWindowDisplayIndex));
		SDL_SetWindowMinimumSize(m_PrimaryWindow.get(), c_MinResX, c_MinResY);
	}
	if (!recoveredToPreviousSettings) {
		m_ResX = newResX;
		m_ResY = newResY;
		m_ResMultiplier = newResMultiplier;
		m_Fullscreen = fullscreen;
		g_SettingsMan.UpdateSettingsFile();
	}

	if (onlyResMultiplierChange) {
		if (!newResFullyCoversAllDisplays) {
			SetViewportLetterboxed();
			CreateBackBufferTexture();
		}
		g_ConsoleMan.PrintString("SYSTEM: Switched to different resolution multiplier.");
	} else {
		m_ResolutionChanged = true;
		g_FrameMan.CreateBackBuffers();
		g_PostProcessMan.CreateGLBackBuffers();
		SetViewportLetterboxed();
		CreateBackBufferTexture();
	}
#ifdef _WIN32
	SDL_GL_SetSwapInterval(m_Fullscreen && m_EnableVSync ? 1 : 0);
#endif
	g_ConsoleMan.PrintString("SYSTEM: " + std::string(!recoveredToPreviousSettings ? "Switched to different resolution." : "Failed to switch to different resolution. Reverted to previous settings."));
}

void WindowMan::ToggleFullscreen() {
	bool fullscreen = !m_Fullscreen;

	MapDisplays();

	if (fullscreen && m_UseMultiDisplays && m_CanMultiDisplayFullscreen && (m_NumDisplays > 1)) {
		double aspectRatio = m_ResX / static_cast<double>(m_ResY);
		double maxAspectRatio = m_MaxResX / static_cast<double>(m_MaxResY);
		if (glm::epsilonNotEqual(aspectRatio, maxAspectRatio, glm::epsilon<double>())) {
			RTEError::ShowMessageBox("Switching to multi display fullscreen would result in letterboxing, please disable multiple displays in settings or switch to fullscreen manually!");
			return;
		}
		ChangeResolution(m_ResX, m_ResY, m_ResMultiplier, fullscreen, true);
	}

	if (!fullscreen) {
		SDL_SetWindowFullscreen(m_PrimaryWindow.get(), 0);
		SDL_SetWindowMinimumSize(m_PrimaryWindow.get(), c_MinResX, c_MinResY);
	} else {
		SDL_SetWindowFullscreen(m_PrimaryWindow.get(), true);
	}
	m_Fullscreen = fullscreen;

#ifdef _WIN32
	SDL_GL_SetSwapInterval(m_Fullscreen && m_EnableVSync ? 1 : 0);
#endif
	SetViewportLetterboxed();
}

bool WindowMan::ChangeResolutionToMultiDisplayFullscreen(float resMultiplier) {
	if (!m_CanMultiDisplayFullscreen) {
		return false;
	}
	int windowPrevPositionX = 0;
	int windowPrevPositionY = 0;
	SDL_GetWindowPosition(m_PrimaryWindow.get(), &windowPrevPositionX, &windowPrevPositionY);

	// Move the window to the detected leftmost display to avoid all the headaches.
	if (m_PrimaryWindowDisplayIndex != m_DisplayArrangmentLeftMostDisplayIndex) {
		SDL_SetWindowPosition(m_PrimaryWindow.get(), SDL_WINDOWPOS_CENTERED_DISPLAY(m_DisplayArrangmentLeftMostDisplayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(m_DisplayArrangmentLeftMostDisplayIndex));
		m_PrimaryWindowDisplayIndex = m_DisplayArrangmentLeftMostDisplayIndex;
	}

	bool errorSettingFullscreen = false;

	for (const auto& [displayIndex, displayBounds]: m_ValidDisplayIndicesAndBoundsForMultiDisplayFullscreen) {
		int displayOffsetX = displayBounds.x;
		int displayOffsetY = displayBounds.y;
		int displayWidth = displayBounds.w;
		int displayHeight = displayBounds.h;

		SDL_PropertiesID windowProps = SDL_CreateProperties();
		RTEAssert(windowProps, "Failed to create properties!" + std::string(SDL_GetError()));
		if (displayIndex == m_PrimaryWindowDisplayIndex) {
			m_MultiDisplayWindows.emplace_back(m_PrimaryWindow);
		} else {
			SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_X_NUMBER, displayOffsetX);
			SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_Y_NUMBER, displayOffsetY);
			SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, displayHeight);
			SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, displayWidth);
			SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
			SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
			SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_UTILITY_BOOLEAN, true);

			m_MultiDisplayWindows.emplace_back(SDL_CreateWindowWithProperties(windowProps), SDLWindowDeleter());
			if (m_MultiDisplayWindows.back()) {
			} else {
				errorSettingFullscreen = true;
			}
			if (errorSettingFullscreen) {
				break;
			}
		}

		int textureOffsetX = (displayOffsetX - m_DisplayArrangementLeftMostOffset);
		int textureOffsetY = (displayOffsetY - m_DisplayArrangementTopMostOffset);

		glm::mat4 textureOffset = glm::translate(glm::mat4(1), {m_DisplayArrangementLeftMostOffset, m_DisplayArrangementTopMostOffset, 0.0f});
		textureOffset = glm::scale(textureOffset, {m_MaxResX * 0.5f, m_MaxResY * 0.5f, 1.0f});
		textureOffset = glm::translate(textureOffset, {1.0f, 1.0f, 0.0f}); // Shift the quad so we're scaling from top left instead of center.

		m_MultiDisplayTextureOffsets.emplace_back(textureOffset);
		glm::mat4 projection = glm::ortho(static_cast<float>(textureOffsetX), static_cast<float>(textureOffsetX + displayWidth), static_cast<float>(textureOffsetY), static_cast<float>(textureOffsetY + displayHeight), -1.0f, 1.0f);
		m_MultiDisplayProjections.emplace_back(projection);
	}

	if (errorSettingFullscreen) {
		ClearMultiDisplayData();
		SDL_SetWindowPosition(m_PrimaryWindow.get(), windowPrevPositionX, windowPrevPositionY);
		return false;
	}

	SDL_SetWindowFullscreen(m_PrimaryWindow.get(), true);
	return true;
}

void WindowMan::DisplaySwitchIn(SDL_Window* windowThatShouldTakeInputFocus) const {
	g_UInputMan.DisableMouseMoving(false);
	g_UInputMan.DisableKeys(false);

	if (!m_MultiDisplayWindows.empty()) {
		for (const auto& window: m_MultiDisplayWindows) {
			if (!m_Background) SDL_RaiseWindow(window.get());
		}
		if (!m_Background) SDL_RaiseWindow(windowThatShouldTakeInputFocus);
	} else {
		if (!m_Background) SDL_RaiseWindow(m_PrimaryWindow.get());
	}

	if (!m_Background) SDL_HideCursor();
}

void WindowMan::DisplaySwitchOut() const {
	g_UInputMan.DisableMouseMoving(true);
	g_UInputMan.DisableKeys(true);

	SDL_ShowCursor();
	// Sometimes the cursor will not be visible after disabling relative mode. Setting it to nullptr forces it to redraw, though this doesn't always work either.
	SDL_SetCursor(nullptr);
}

bool WindowMan::HandleWindowExposedEvent(void *userdata, SDL_Event *event) {
	if (event->type == SDL_EVENT_WINDOW_EXPOSED) {
		// Re-show the last finished frame (e.g. while the window is being dragged or something was moved over it).
		// Rebuilding it here instead would happen outside the frame and could show a cleared screen buffer with only the GUI on it.
		g_WindowMan.SetViewportLetterboxed();
		g_WindowMan.ClearBackbuffer(false);
		if (g_WindowMan.m_LastPresentUsedTextOverlay) {
			g_WindowMan.PresentWithTextOverlay(true);
		} else {
			g_WindowMan.BlitScreenBufferToWindows();
		}
		g_WindowMan.Present();
	}

	return true;
}

void WindowMan::QueueWindowEvent(const SDL_Event& windowEvent) {
	m_EventQueue.emplace_back(windowEvent);
}

void WindowMan::Update() {
	// Some bullshit we have to deal with to correctly focus windows in multi-display fullscreen so mouse binding/unbinding works correctly. Not relevant for single window.
	// This is SDL's fault for not having handling to raise a window so it's top-most without taking focus of it.
	// Don't process any focus events this update if either of these has been set in the previous one.
	// Having two flags is a bit redundant but better be safe than recursively raising windows and taking focus and pretty much locking up your whole shit so the only thing you can do about it is sign out to terminate this.
	// Clearing the queue here is just us not handling the events on our end. Whatever they are and do and wherever they propagate to is handled by SDL_PollEvent earlier.
	if (m_FocusEventsDispatchedByDisplaySwitchIn || m_FocusEventsDispatchedByMovingBetweenWindows) {
		m_EventQueue.clear();

		m_FocusEventsDispatchedByDisplaySwitchIn = false;
		m_FocusEventsDispatchedByMovingBetweenWindows = false;
		return;
	}

	SDL_Event windowEvent;
	for (std::vector<SDL_Event>::const_iterator eventIterator = m_EventQueue.begin(); eventIterator != m_EventQueue.end(); eventIterator++) {
		windowEvent = *eventIterator;
		int windowID = windowEvent.window.windowID;

		switch (windowEvent.type) {
			case SDL_EVENT_WINDOW_MOUSE_ENTER:
				if (SDL_GetWindowID(SDL_GetMouseFocus()) > 0 && m_AnyWindowHasFocus && FullyCoversAllDisplays()) {
					for (const auto& window: m_MultiDisplayWindows) {
						if (!m_Background) SDL_RaiseWindow(window.get());
					}
					if (!m_Background) SDL_RaiseWindow(SDL_GetWindowFromID(windowID));
					m_AnyWindowHasFocus = true;
					m_FocusEventsDispatchedByMovingBetweenWindows = true;
				}
				break;
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				DisplaySwitchIn(SDL_GetWindowFromID(windowID));
				m_AnyWindowHasFocus = true;
				m_FocusEventsDispatchedByDisplaySwitchIn = true;
				break;
			case SDL_EVENT_WINDOW_FOCUS_LOST:
				DisplaySwitchOut();
				m_AnyWindowHasFocus = false;
				m_FocusEventsDispatchedByDisplaySwitchIn = false;
				m_FocusEventsDispatchedByMovingBetweenWindows = false;
				break;
			case SDL_EVENT_WINDOW_RESIZED:
			case SDL_WINDOW_MAXIMIZED:
				SetViewportLetterboxed();
				break;
			default:
				break;
		}
	}
	m_EventQueue.clear();
}

void WindowMan::ClearBackbuffer(bool clearFrameMan) {
	GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
	if (clearFrameMan) {
		g_FrameMan.ClearBackBuffer32();
	}
	GL_CHECK(glActiveTexture(GL_TEXTURE0));
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
	GL_CHECK(glActiveTexture(GL_TEXTURE1));
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, 0));
	m_DrawPostProcessBuffer = false;
}

void WindowMan::UploadFrame() {

	m_ScreenBuffer->Begin(g_ActivityMan.IsInActivity());
	//Camera viewport(Vector(m_ResX / 2, m_ResY / 2), m_ScreenBuffer->GetSize());
	g_RenderMan.BeginFrame(nullptr);

	glActiveTexture(GL_TEXTURE2);
	m_BackBuffer32Texture->Bind();
	GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 4));
	GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_FrameMan.GetBackBuffer32()->w, g_FrameMan.GetBackBuffer32()->h, GL_RGBA, GL_UNSIGNED_BYTE, g_FrameMan.GetBackBuffer32()->line[0]));

	// The GUI is always composited on top of whatever is in the screen buffer, so don't let it depth test against earlier draws (e.g. the title screen's stars punched holes in menu text).
	glClear(GL_DEPTH_BUFFER_BIT);

	if (m_DrawPostProcessBuffer) {
		Texture* postBuffer = g_PostProcessMan.GetPostProcessColorBuffer()->GetColorTexture().lock().get();
		Draw::DrawTexture(postBuffer, {-1.0f, -1.0f, 2.0f, 2.0f});
	}
	// Otherwise the screen buffer already holds the frame, drawing it onto itself would be a feedback loop.
	m_ScreenBlitShader->Begin();
	Draw::DrawTexture(m_BackBuffer32Texture.get(), {-1.0f, -1.0f, 2.0f, 2.0f});
	m_ScreenBlitShader->End();
	m_ScreenBuffer->End();
	g_RenderMan.BeginFrame(nullptr);

	// The picture doesn't always cover the whole window (bars at the sides of an ill-fitting window, docked tool panels): clear what it leaves, so nothing stale shows there.
	{
		int windowW = 0;
		int windowH = 0;
		SDL_GetWindowSizeInPixels(m_PrimaryWindow.get(), &windowW, &windowH);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, windowW, windowH);
		glDisable(GL_SCISSOR_TEST);
		glClearColor(0.055F, 0.06F, 0.075F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	// HUD text captured for high resolution drawing goes between the scene and the GUI layer, so it needs them separately.
	m_LastPresentUsedTextOverlay = m_DrawPostProcessBuffer && m_MultiDisplayWindows.empty() && TextOverlay::HasPendingText();
	if (m_LastPresentUsedTextOverlay) {
		PresentWithTextOverlay(false);
	} else {
		BlitScreenBufferToWindows();
	}
	if (g_DebugMan.ConsumeScreenshotRequest()) {
		SaveWindowScreenshot();
	}
	g_DebugMan.DrawImGui();
	// The frame rate and version, small, in the top right (Settings > Show FPS and version).
	if (g_SettingsMan.ShowFPSAndVersion()) {
		float mspf = g_PerformanceMan.GetMSPFAverage();
		std::string text = "v" + std::string(c_VersionString) + "  " + std::to_string(mspf > 0.0F ? static_cast<int>(std::round(1000.0F / mspf)) : 0) + " fps";
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		ImVec2 size = ImGui::CalcTextSize(text.c_str());
		ImVec2 corner(ImGui::GetMainViewport()->Pos.x + ImGui::GetMainViewport()->Size.x - size.x - 6.0F, ImGui::GetMainViewport()->Pos.y + 4.0F);
		drawList->AddText(ImVec2(corner.x + 1.0F, corner.y + 1.0F), IM_COL32(0, 0, 0, 200), text.c_str());
		drawList->AddText(corner, IM_COL32(235, 235, 235, 230), text.c_str());
	}
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	Present();
	TracyGpuCollect;
	FrameMark;
	g_DebugMan.PrepareFonts();
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
}

void WindowMan::BlitTextureToPrimaryWindow(Texture* texture, Shader* shader, bool blend) {
	GLuint textureId = texture->GetTextureId();
	GLint previousMinFilter = GL_NEAREST;
	GLint previousMagFilter = GL_NEAREST;
	glBindTexture(GL_TEXTURE_2D, textureId);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &previousMinFilter);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &previousMagFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindFramebuffer(GL_FRAMEBUFFER, m_BlitTargetFramebuffer);
	glClear(GL_DEPTH_BUFFER_BIT);
	g_RenderMan.BeginFrame(nullptr);
	shader->Begin();
	g_RenderMan.SetActiveBlendMode(blend ? Blend::ALPHA : Blend::NONE);
	GL_CHECK(glViewport(m_PrimaryWindowViewport->x, m_PrimaryWindowViewport->y, m_PrimaryWindowViewport->w, m_PrimaryWindowViewport->h));
	Draw::DrawTexture(texture, {-1.0f, 1.0f, 2.0f, -2.0f});
	g_RenderMan.DrawActiveBatch();
	shader->End();
	g_RenderMan.BeginFrame(nullptr);
	glBindTexture(GL_TEXTURE_2D, textureId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, previousMinFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, previousMagFilter);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void WindowMan::SaveWindowScreenshot() {
	int scale = g_DebugMan.GetScreenshotScale();
	int width = scale > 0 ? static_cast<int>(m_ResX) * scale : m_PrimaryWindowViewport->w;
	int height = scale > 0 ? static_cast<int>(m_ResY) * scale : m_PrimaryWindowViewport->h;
	if (width <= 0 || height <= 0) {
		return;
	}
	std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	if (scale > 0) {
		// Draw the frame again, exactly as for the window, but into an offscreen target at the photo's resolution.
		GLuint framebuffer = 0;
		GLuint color = 0;
		GLuint depth = 0;
		glGenFramebuffers(1, &framebuffer);
		glGenTextures(1, &color);
		glBindTexture(GL_TEXTURE_2D, color);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glGenRenderbuffers(1, &depth);
		glBindRenderbuffer(GL_RENDERBUFFER, depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
		glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		SDL_Rect windowViewport = *m_PrimaryWindowViewport;
		*m_PrimaryWindowViewport = SDL_Rect{0, 0, width, height};
		m_BlitTargetFramebuffer = framebuffer;
		if (m_LastPresentUsedTextOverlay) {
			PresentWithTextOverlay(true);
		} else {
			BlitScreenBufferToWindows();
		}
		m_BlitTargetFramebuffer = 0;
		*m_PrimaryWindowViewport = windowViewport;

		glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteRenderbuffers(1, &depth);
		glDeleteTextures(1, &color);
		glDeleteFramebuffers(1, &framebuffer);
		glViewport(m_PrimaryWindowViewport->x, m_PrimaryWindowViewport->y, m_PrimaryWindowViewport->w, m_PrimaryWindowViewport->h);
	} else {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glReadPixels(m_PrimaryWindowViewport->x, m_PrimaryWindowViewport->y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	}
	BITMAP* image = create_bitmap_ex(32, width, height);
	for (int y = 0; y < height; ++y) {
		// GL rows start at the bottom.
		const unsigned char* row = &pixels[static_cast<size_t>(height - 1 - y) * width * 4];
		for (int x = 0; x < width; ++x) {
			_putpixel32(image, x, y, makeacol32(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], 255));
		}
	}
	std::time_t now = std::time(nullptr);
	std::array<char, 32> stamp{};
	std::strftime(stamp.data(), stamp.size(), "%Y-%m-%d_%H-%M-%S", std::localtime(&now));
	std::string fileName = System::GetScreenshotDirectory() + "/Photo_" + stamp.data() + ".png";
	if (save_png(fileName.c_str(), image, nullptr) == 0) {
		g_ConsoleMan.PrintString("SYSTEM: Photo saved to: " + fileName);
	} else {
		g_ConsoleMan.PrintString("ERROR: Could not save photo to: " + fileName);
	}
	destroy_bitmap(image);
}

void WindowMan::PresentWithTextOverlay(bool redrawLast) {
	Texture* sceneTexture = g_PostProcessMan.GetPostProcessColorBuffer()->GetColorTexture().lock().get();
	m_ScreenUpscaleShader->Begin();
	SetUpscaleUniforms();
	m_ScreenUpscaleShader->End();
	BlitTextureToPrimaryWindow(sceneTexture, m_ScreenUpscaleShader.get(), false);
	int windowWidth = 0;
	int windowHeight = 0;
	SDL_GetWindowSizeInPixels(m_PrimaryWindow.get(), &windowWidth, &windowHeight);
	if (m_BlitTargetFramebuffer != 0) {
		// Offscreen photo: the target is exactly the viewport.
		windowWidth = m_PrimaryWindowViewport->w;
		windowHeight = m_PrimaryWindowViewport->h;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, m_BlitTargetFramebuffer);
	TextOverlay::Render(windowWidth, windowHeight, m_PrimaryWindowViewport->x, m_PrimaryWindowViewport->y, m_PrimaryWindowViewport->w, m_PrimaryWindowViewport->h, static_cast<int>(m_ResX), static_cast<int>(m_ResY), redrawLast);
	BlitTextureToPrimaryWindow(m_BackBuffer32Texture.get(), m_ScreenUpscaleMaskedShader.get(), true);
}

void WindowMan::BlitScreenBufferToWindows() {
	glBindFramebuffer(GL_FRAMEBUFFER, m_BlitTargetFramebuffer);
	glDisable(GL_BLEND);
	// The upscale shader blends only across pixel boundaries, which needs linear filtering; restore the texture's own filtering afterwards.
	GLuint screenTexture = m_ScreenBuffer->GetColorTexture().lock()->GetTextureId();
	GLint previousMinFilter = GL_NEAREST;
	GLint previousMagFilter = GL_NEAREST;
	glBindTexture(GL_TEXTURE_2D, screenTexture);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &previousMinFilter);
	glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &previousMagFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	m_ScreenUpscaleShader->Begin();
	SetUpscaleUniforms();
	if (m_MultiDisplayWindows.empty()) {
		g_RenderMan.BeginFrame(nullptr);
		// BeginFrame resets the shader.
		m_ScreenUpscaleShader->Begin();
		SetUpscaleUniforms();
		GL_CHECK(glViewport(m_PrimaryWindowViewport->x, m_PrimaryWindowViewport->y, m_PrimaryWindowViewport->w, m_PrimaryWindowViewport->h));
		Draw::DrawTexture(m_ScreenBuffer->GetColorTexture().lock().get(), {-1.0f, 1.0f, 2.0f, -2.0f});
		g_RenderMan.DrawActiveBatch();
	} else {
		for (size_t i = 0; i < m_MultiDisplayWindows.size(); ++i) {
			SDL_GL_MakeCurrent(m_MultiDisplayWindows.at(i).get(), m_GLContext.get());
			int windowW, windowH;

			SDL_GetWindowSizeInPixels(m_MultiDisplayWindows.at(i).get(), &windowW, &windowH);
			GL_CHECK(glViewport(0, 0, windowW, windowH));

			Draw::DrawTexture(m_ScreenBuffer->GetColorTexture().lock().get(), {-1.0f, 1.0f, 2.0f, -2.0f});
			g_RenderMan.DrawActiveBatch();
		}
	}
	m_ScreenUpscaleShader->End();
	glBindTexture(GL_TEXTURE_2D, screenTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, previousMinFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, previousMagFilter);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void WindowMan::Present() {
	if (m_FrameCap > 0) {
		// Wait out the rest of this frame's time: sleep for most of it, then spin for the last moment, which sleeping can't hit precisely.
		long long frameTicks = SDL_GetPerformanceFrequency() / static_cast<Uint64>(m_FrameCap);
		long long target = m_LastPresentTicks + frameTicks;
		long long now = static_cast<long long>(SDL_GetPerformanceCounter());
		if (now < target) {
			long long sleepNS = (target - now) * 1000000000LL / static_cast<long long>(SDL_GetPerformanceFrequency()) - 1500000LL;
			if (sleepNS > 0) {
				SDL_DelayNS(static_cast<Uint64>(sleepNS));
			}
			while (static_cast<long long>(SDL_GetPerformanceCounter()) < target) {
			}
			m_LastPresentTicks = target;
		} else {
			m_LastPresentTicks = now;
		}
	}
	if (m_MultiDisplayWindows.empty()) {
		SDL_GL_SwapWindow(m_PrimaryWindow.get());
	} else {
		for (size_t i = 0; i < m_MultiDisplayWindows.size(); ++i) {
			SDL_GL_MakeCurrent(m_MultiDisplayWindows.at(i).get(), m_GLContext.get());
			SDL_GL_SwapWindow(m_MultiDisplayWindows.at(i).get());
		}
	}
}
