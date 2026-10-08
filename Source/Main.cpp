/*          ______   ______   ______  ______  ______  __  __       ______   ______   __    __   __    __   ______   __   __   _____
           /\  ___\ /\  __ \ /\  == \/\__  _\/\  ___\/\_\_\_\     /\  ___\ /\  __ \ /\ "-./  \ /\ "-./  \ /\  __ \ /\ "-.\ \ /\  __-.
           \ \ \____\ \ \/\ \\ \  __<\/_/\ \/\ \  __\\/_/\_\/_    \ \ \____\ \ \/\ \\ \ \-./\ \\ \ \-./\ \\ \  __ \\ \ \-.  \\ \ \/\ \
            \ \_____\\ \_____\\ \_\ \_\ \ \_\ \ \_____\/\_\/\_\    \ \_____\\ \_____\\ \_\ \ \_\\ \_\ \ \_\\ \_\ \_\\ \_\\"\_\\ \____-
             \/_____/ \/_____/ \/_/ /_/  \/_/  \/_____/\/_/\/_/     \/_____/ \/_____/ \/_/  \/_/ \/_/  \/_/ \/_/\/_/ \/_/ \/_/ \/____/
   ______   ______   __    __   __    __   __  __   __   __   __   ______  __  __       ______  ______   ______      __   ______   ______   ______
  /\  ___\ /\  __ \ /\ "-./  \ /\ "-./  \ /\ \/\ \ /\ "-.\ \ /\ \ /\__  _\/\ \_\ \     /\  == \/\  == \ /\  __ \    /\ \ /\  ___\ /\  ___\ /\__  _\
  \ \ \____\ \ \/\ \\ \ \-./\ \\ \ \-./\ \\ \ \_\ \\ \ \-.  \\ \ \\/_/\ \/\ \____ \    \ \  _-/\ \  __< \ \ \/\ \  _\_\ \\ \  __\ \ \ \____\/_/\ \/
   \ \_____\\ \_____\\ \_\ \ \_\\ \_\ \ \_\\ \_____\\ \_\\"\_\\ \_\  \ \_\ \/\_____\    \ \_\   \ \_\ \_\\ \_____\/\_____\\ \_____\\ \_____\  \ \_\
    \/_____/ \/_____/ \/_/  \/_/ \/_/  \/_/ \/_____/ \/_/ \/_/ \/_/   \/_/  \/_____/     \/_/    \/_/ /_/ \/_____/\/_____/ \/_____/ \/_____/   \/_/

/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\/////\\\\\*/

/// <summary>
/// Main driver implementation of the Retro Terrain Engine.
/// Data Realms, LLC - http://www.datarealms.com
/// Cortex Command Community Project - https://github.com/cortex-command-community
/// Cortex Command Community Project Discord - https://discord.gg/TSU6StNQUG
/// </summary>

#include "ControlLink.h"
#include "SDL3/SDL_hints.h"
#include "allegro.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "GUI.h"
#include "GUIInputWrapper.h"
#include "AllegroScreen.h"
#include "AllegroBitmap.h"

#include "MainMenuGUI.h"
#include "ScenarioGUI.h"
#include "PauseMenuGUI.h"
#include "TitleScreen.h"
#include "LoadingScreen.h"

#include "MenuMan.h"
#include "ConsoleMan.h"
#include "SettingsMan.h"
#include <chrono>
#include <string>
#include <utility>
#include <vector>
#include <thread>
#include "PresetMan.h"
#include "UInputMan.h"
#include "PerformanceMan.h"
#include "FrameMan.h"
#include "DebugMan.h"
#include "TerrainFire.h"
#include "WeatherLightning.h"
#include "TerrainCollapse.h"
#include "FluidSim.h"
#include "SmokeGrid.h"
#include "Sandbox.h"
#include "ActorFire.h"
#include "ActorWater.h"
#include "PostProcessMan.h"
#include "SceneMan.h"
#include "MetaMan.h"
#include "WindowMan.h"
#include "GLStateMan.h"
#include "RenderMan.h"
#include "CameraMan.h"
#include "ActivityMan.h"
#include "PrimitiveMan.h"
#include "ThreadMan.h"
#include "LuaMan.h"
#include "MusicMan.h"
#include "System.h"

#include "RenderTarget.h"
#include "tracy/Tracy.hpp"

#include "imgui_impl_sdl3.h"

#ifdef RENDERDOC_DEBUG
#include "renderdoc_app.h"
#include <dlfcn.h>
#endif

#ifdef _WIN32
#include "windows.h"
#endif

extern "C" {
FILE __iob_func[3] = {*stdin, *stdout, *stderr};
}

using namespace RTE;

namespace {
	/// How long each step of start-up took, from main() to the first menu frame (or the first activity, when launching straight into one).
	/// Printed to the console, and so LogConsole.txt, once start-up is over, so a slow start says which step the time went to.
	struct StartupTiming {
		std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
		std::chrono::steady_clock::time_point Last = Start;
		std::vector<std::pair<std::string, long long>> Stages;
		bool Reported = false;

		/// Ends the step that ran since the last mark, under this name.
		void Mark(const std::string& stageName) {
			auto now = std::chrono::steady_clock::now();
			Stages.emplace_back(stageName, std::chrono::duration_cast<std::chrono::milliseconds>(now - Last).count());
			Last = now;
		}

		/// Ends the last step and prints them all, with the total. Only the first call prints.
		void Report(const std::string& lastStageName) {
			if (Reported) {
				return;
			}
			Reported = true;
			Mark(lastStageName);
			g_ConsoleMan.PrintString("SYSTEM: Start-up timing (ms):");
			for (const auto& [stageName, milliseconds]: Stages) {
				g_ConsoleMan.PrintString("Start-up: " + stageName + ": " + std::to_string(milliseconds) + " ms");
			}
			if (auto [files, indexMS] = System::GetCaseIndexStats(); indexMS >= 0) {
				g_ConsoleMan.PrintString("Start-up: (of which the case check's file index: " + std::to_string(files) + " files in " + std::to_string(indexMS) + " ms)");
			}
			g_ConsoleMan.PrintString("Start-up: loading screen frames drawn: " + std::to_string(LoadingScreen::GetProgressFramesDrawn()) + " (progress report " + (g_SettingsMan.GetLoadingScreenProgressReportDisabled() ? "off" : "on") +
			                         ", frame cap " + std::to_string(g_WindowMan.GetFrameCap()) + ", vsync " + (g_WindowMan.GetVSyncEnabled() ? "on" : "off") + ", fullscreen " + (g_WindowMan.IsFullscreen() ? "yes" : "no") + ")");
			// The Lua states are made one after another, each with every engine binding (one per hardware thread unless Settings.ini says otherwise),
			// so how many there were says how much of the Lua step above that is.
			g_ConsoleMan.PrintString("Start-up: Lua states made: " + std::to_string(g_LuaMan.GetThreadedScriptStates().size() + 1) + " (" + std::to_string(g_LuaMan.GetThreadedScriptStates().size()) + " threaded and the master; " + std::to_string(std::thread::hardware_concurrency()) + " hardware threads)");
			g_ConsoleMan.PrintString("Start-up: total to " + lastStageName + ": " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Last - Start).count()) + " ms");
		}
	};
	StartupTiming s_StartupTiming;
} // namespace

/// <summary>
/// Initializes all the essential managers.
/// </summary>
void InitializeManagers() {
	ThreadMan::Construct();
	TimerMan::Construct();
	PresetMan::Construct();
	SettingsMan::Construct();
	WindowMan::Construct();
	GLStateMan::Construct();
	LuaMan::Construct();
	FrameMan::Construct();
	RenderMan::Construct();
	DebugMan::Construct();
	PerformanceMan::Construct();
	PostProcessMan::Construct();
	PrimitiveMan::Construct();
	AudioMan::Construct();
	GUISound::Construct();
	MusicMan::Construct();
	UInputMan::Construct();
	ConsoleMan::Construct();
	SceneMan::Construct();
	MovableMan::Construct();
	MetaMan::Construct();
	MenuMan::Construct();
	CameraMan::Construct();
	ActivityMan::Construct();
	LoadingScreen::Construct();
	s_StartupTiming.Mark("constructing the managers");

	g_ThreadMan.Initialize();
	s_StartupTiming.Mark("ThreadMan");
	g_SettingsMan.Initialize();
	s_StartupTiming.Mark("SettingsMan (Settings.ini)");
	g_WindowMan.Initialize();
	s_StartupTiming.Mark("WindowMan (window and GL context)");
	g_GLStateMan.Initialize();
	s_StartupTiming.Mark("GLStateMan");

	g_LuaMan.Initialize();
	s_StartupTiming.Mark("LuaMan (Lua states)");
	g_TimerMan.Initialize();
	g_FrameMan.Initialize();
	s_StartupTiming.Mark("FrameMan");
	g_RenderMan.Initialize();
	s_StartupTiming.Mark("RenderMan (shaders)");
	g_PostProcessMan.Initialize();
	s_StartupTiming.Mark("PostProcessMan (shaders)");
	g_PerformanceMan.Initialize();

	if (g_AudioMan.Initialize()) {
		g_GUISound.Initialize();
		g_MusicMan.Initialize();
	}
	s_StartupTiming.Mark("AudioMan, GUISound and MusicMan");

	g_UInputMan.Initialize();
	s_StartupTiming.Mark("UInputMan (input devices)");
	g_ConsoleMan.Initialize();
	s_StartupTiming.Mark("ConsoleMan");
	g_SceneMan.Initialize();
	g_MovableMan.Initialize();
	g_MetaMan.Initialize();
	s_StartupTiming.Mark("SceneMan, MovableMan and MetaMan");
	g_MenuMan.Initialize();
	s_StartupTiming.Mark("MenuMan (loading screen and menus)");

	// Overwrite Settings.ini after all the managers are created to fully populate the file. Up until this moment Settings.ini is populated only with minimal required properties to run.
	// If Settings.ini already exists and is fully populated, this will deal with overwriting it to apply any overrides performed by the managers at boot (e.g resolution validation).
	if (g_SettingsMan.SettingsNeedOverwrite()) {
		g_SettingsMan.UpdateSettingsFile();
	}
	s_StartupTiming.Mark("writing Settings.ini");
}

/// <summary>
/// Destroys all the managers and frees all loaded data before termination.
/// </summary>
void DestroyManagers() {
	ControlLink::Stop();
	g_MetaMan.Destroy();
	g_PerformanceMan.Destroy();
	g_MovableMan.Destroy();
	g_SceneMan.Destroy();
	g_ActivityMan.Destroy();
	g_GUISound.Destroy();
	g_AudioMan.Destroy();
	g_MusicMan.Destroy();
	g_PresetMan.Destroy();
	g_UInputMan.Destroy();
	g_PostProcessMan.Destroy();
	g_FrameMan.Destroy();
	g_TimerMan.Destroy();
	g_LuaMan.Destroy();
	ContentFile::FreeAllLoaded();
	g_ConsoleMan.Destroy();
	g_RenderMan.Destroy();
	g_GLStateMan.Destroy();
	g_WindowMan.Destroy();

#ifdef DEBUG_BUILD
	Entity::ClassInfo::DumpPoolMemoryInfo(Writer("MemCleanupInfo.txt"));
#endif
}

/// <summary>
/// Command-line argument handling.
/// </summary>
/// <param name="argCount">Argument count.</param>
/// <param name="argValue">Argument values.</param>
void HandleMainArgs(int argCount, char** argValue) {
	// Discard the first argument because it's always the executable path/name
	argCount--;
	argValue++;
	if (argCount == 0) {
		return;
	}
	bool launchModeSet = false;
	bool singleModuleSet = false;

	for (int i = 0; i < argCount;) {
		std::string currentArg = argValue[i];
		bool lastArg = i + 1 == argCount;

		if (currentArg == "-cout") {
			System::EnableLoggingToCLI();
		}

		if (currentArg == "-ext-validate") {
			System::EnableExternalModuleValidationMode();
		}

		if (!lastArg && !singleModuleSet && currentArg == "-module") {
			std::string moduleToLoad = argValue[++i];
			if (moduleToLoad.find(System::GetModulePackageExtension()) == moduleToLoad.length() - System::GetModulePackageExtension().length()) {
				g_PresetMan.SetSingleModuleToLoad(moduleToLoad);
				singleModuleSet = true;
			}
		}
		if (!launchModeSet) {
			if (!lastArg && currentArg == "-editor") {
				g_ActivityMan.SetEditorToLaunch(argValue[++i]);
				launchModeSet = true;
			}
		}
		++i;
	}
	if (launchModeSet) {
		g_SettingsMan.SetSkipIntro(true);
	}
}

/// <summary>
/// Polls the SDL event queue and passes events to be handled by the relevant managers.
/// </summary>
void PollSDLEvents() {
	SDL_Event sdlEvent;
	// Commands from a companion program on this computer (the Workbench), if the link was asked for.
	ControlLink::Update();
	while (SDL_PollEvent(&sdlEvent)) {
		// Clicks, scrolls and typing aimed at a debug window shouldn't also reach the game (releases always do, so nothing gets stuck down).
		const ImGuiIO& imGuiIO = ImGui::GetIO();
		// Function keys (debug window toggles, quicksave and so on) always reach the game, so a focused debug window can still be closed with its key.
		bool functionKey = (sdlEvent.type == SDL_EVENT_KEY_DOWN || sdlEvent.type == SDL_EVENT_KEY_UP) && sdlEvent.key.scancode >= SDL_SCANCODE_F1 && sdlEvent.key.scancode <= SDL_SCANCODE_F12;
		// While a sandbox tool is picked, left clicks on the world paint instead of firing.
		bool sandboxTakesClick = Sandbox::CapturesWorldClicks() && sdlEvent.type == SDL_EVENT_MOUSE_BUTTON_DOWN && sdlEvent.button.button == SDL_BUTTON_LEFT;
		bool imGuiTakesEvent = !functionKey && (sandboxTakesClick || (imGuiIO.WantCaptureMouse && (sdlEvent.type == SDL_EVENT_MOUSE_BUTTON_DOWN || sdlEvent.type == SDL_EVENT_MOUSE_WHEEL)) ||
		                                        (imGuiIO.WantCaptureKeyboard && (sdlEvent.type == SDL_EVENT_KEY_DOWN || sdlEvent.type == SDL_EVENT_TEXT_INPUT)));
		if (imGuiTakesEvent) {
			ImGui_ImplSDL3_ProcessEvent(&sdlEvent);
			continue;
		}
		// Camera zoom: Ctrl + mouse wheel in any game, or the wheel alone in the sandbox's god view (where it isn't needed for switching weapons).
		if (sdlEvent.type == SDL_EVENT_MOUSE_WHEEL && sdlEvent.wheel.y != 0.0F && g_ActivityMan.IsInActivity() && ((SDL_GetModState() & SDL_KMOD_CTRL) || Sandbox::WantsWheelZoom())) {
			g_FrameMan.StepCameraZoom(sdlEvent.wheel.y > 0.0F);
			continue;
		}
		switch (sdlEvent.type) {
			case SDL_EVENT_QUIT :
				System::SetQuit(true);
				return;
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				System::SetQuit(true);
				return;
			case SDL_EVENT_KEY_UP :
			case SDL_EVENT_KEY_DOWN :
			case SDL_EVENT_TEXT_INPUT :
			case SDL_EVENT_MOUSE_MOTION :
			case SDL_EVENT_MOUSE_BUTTON_UP :
			case SDL_EVENT_MOUSE_BUTTON_DOWN :
			case SDL_EVENT_MOUSE_WHEEL :
			case SDL_EVENT_GAMEPAD_AXIS_MOTION :
			case SDL_EVENT_GAMEPAD_BUTTON_DOWN :
			case SDL_EVENT_GAMEPAD_BUTTON_UP :
			case SDL_EVENT_JOYSTICK_AXIS_MOTION :
			case SDL_EVENT_JOYSTICK_BUTTON_DOWN :
			case SDL_EVENT_JOYSTICK_BUTTON_UP :
			case SDL_EVENT_JOYSTICK_ADDED :
			case SDL_EVENT_JOYSTICK_REMOVED :
				g_UInputMan.HandleInputEvent(sdlEvent);
				break;
			default:
				break;
		}
		ImGui_ImplSDL3_ProcessEvent(&sdlEvent);
		// Debug window toggles work every frame, even while the simulation is frozen (photo mode) and input otherwise isn't processed.
		// Tab in a game is the one key for all of them: every tool window away (in the Sandbox game mode, into your character: Shift+Tab puts it down where the
		// mouse points), or all of them back. Alt+Tab and Ctrl+Tab are left alone.
		if (sdlEvent.type == SDL_EVENT_KEY_DOWN && !sdlEvent.key.repeat && sdlEvent.key.scancode == SDL_SCANCODE_TAB && !(sdlEvent.key.mod & (SDL_KMOD_ALT | SDL_KMOD_CTRL | SDL_KMOD_GUI)) &&
		    g_ActivityMan.IsInActivity() && !g_MenuMan.GetIsInMenuScreen() && !g_ConsoleMan.IsEnabled()) {
			g_DebugMan.ToggleTools((sdlEvent.key.mod & SDL_KMOD_SHIFT) != 0);
		}
		// P in the Sandbox game mode: into your character, or back above. Shift+P puts the character down where the mouse points first.
		if (sdlEvent.type == SDL_EVENT_KEY_DOWN && !sdlEvent.key.repeat && sdlEvent.key.scancode == SDL_SCANCODE_P && !(sdlEvent.key.mod & (SDL_KMOD_ALT | SDL_KMOD_CTRL | SDL_KMOD_GUI)) &&
		    Sandbox::IsGodMode() && !g_MenuMan.GetIsInMenuScreen() && !g_ConsoleMan.IsEnabled()) {
			Sandbox::TogglePlay((sdlEvent.key.mod & SDL_KMOD_SHIFT) != 0);
		}
		if (sdlEvent.type == SDL_EVENT_KEY_DOWN && !sdlEvent.key.repeat && !(sdlEvent.key.mod & (SDL_KMOD_ALT | SDL_KMOD_CTRL | SDL_KMOD_SHIFT))) {
			if (sdlEvent.key.scancode == SDL_SCANCODE_F6) {
				g_DebugMan.ToggleWorldDebug();
			} else if (sdlEvent.key.scancode == SDL_SCANCODE_F7) {
				// In the Sandbox game mode F7 is the same switch as Tab: the god view with its tools, or your character.
				if (Sandbox::IsGodMode()) {
					g_DebugMan.ToggleTools();
				} else {
					Sandbox::Toggle();
				}
			} else if (sdlEvent.key.scancode == SDL_SCANCODE_F8) {
				g_DebugMan.TogglePhotoMode();
			}
		}
		if (sdlEvent.type >= SDL_EVENT_WINDOW_FIRST && sdlEvent.type <= SDL_EVENT_WINDOW_LAST) {
			g_WindowMan.QueueWindowEvent(sdlEvent);
		}
	}
}

/// Dev: with CCCP_TEST_RESOLUTION set to "WxH", the resolution is changed to that a few seconds in, as the settings menu would, so a
/// crash in the change can be caught by the test harness with the debug build's stack trace.
void TestResolutionChange() {
	static const char* wanted = std::getenv("CCCP_TEST_RESOLUTION");
	static bool done = false;
	static Timer timer;
	if (!wanted || done || !timer.IsPastRealMS(4000)) {
		return;
	}
	done = true;
	int width = 0;
	int height = 0;
	if (std::sscanf(wanted, "%dx%d", &width, &height) == 2 && width > 0 && height > 0) {
		g_ConsoleMan.PrintString("TEST: changing resolution to " + std::to_string(width) + "x" + std::to_string(height));
		g_WindowMan.ChangeResolution(width, height, 1.0F, false);
	}
}

/// <summary>
/// Game menus loop.
/// </summary>
void RunMenuLoop() {
	g_MenuMan.SetIsInMenuScreen(true);
	g_UInputMan.DisableKeys(false);
	g_UInputMan.TrapMousePos(false);

	while (!System::IsSetToQuit()) {
		g_WindowMan.ClearBackbuffer();
		PollSDLEvents();

		g_WindowMan.Update();
		TestResolutionChange();

		g_UInputMan.Update();
		g_TimerMan.Update();
		g_TimerMan.UpdateSim();
		g_AudioMan.Update();
		g_MusicMan.Update();

		if (g_WindowMan.ResolutionChanged()) {
			g_MenuMan.Reinitialize();
			g_ConsoleMan.Destroy();
			g_ConsoleMan.Initialize();
			g_LoadingScreen.CreateLoadingSplash();
			g_WindowMan.CompleteResolutionChange();
		}

		if (g_MenuMan.Update()) {
			g_UInputMan.EndFrame();
			break;
		}

		g_ConsoleMan.Update();

		g_UInputMan.EndFrame();
		g_WindowMan.GetScreenBuffer()->Begin();
		g_MenuMan.Draw();
		g_ConsoleMan.Draw(g_FrameMan.GetBackBuffer32());
		g_WindowMan.GetScreenBuffer()->End();
		g_WindowMan.UploadFrame();
		s_StartupTiming.Report("the first menu frame");
	}

	g_MenuMan.SetIsInMenuScreen(false);
}

void LoadRenderDoc() {
#ifdef RENDERDOC_DEBUG
	RENDERDOC_API_1_1_2 *rdoc_api = NULL;
	// At init, on linux/android.
	// For android replace librenderdoc.so with libVkLayer_GLES_RenderDoc.so
	if (void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD)) {
		pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
		int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&rdoc_api);
		assert(ret == 1);
	}
	if (rdoc_api) {
		RENDERDOC_InputButton pause = eRENDERDOC_Key_Pause;
		rdoc_api->SetCaptureKeys(&pause, 1);
	}
#endif
}

/// <summary>
/// Game simulation loop.
/// </summary>
void RunGameLoop() {
	if (System::IsSetToQuit()) {
		return;
	}
	g_TimerMan.PauseSim(false);

	if (g_ActivityMan.ActivitySetToRestart()) {
		g_LoadingScreen.DrawLoadingSplash();
		g_WindowMan.UploadFrame();
		bool restarted = g_ActivityMan.RestartActivity();
		s_StartupTiming.Report("the first activity");
		if (!restarted) {
			// This doesn't work.
			// Somewhat related to https://github.com/cortex-command-community/Cortex-Command-Community-Project-Source/issues/472
			// Deal with later.
			// g_MenuMan.GetTitleScreen()->SetTitleTransitionState(TitleScreen::TitleTransition::ScrollingFadeIn);
		}
	}

	long long updateStartTime = 0;
	long long updateTotalTime = 0;
	long long updateEndAndDrawStartTime = 0;
	long long drawStartTime = 0;
	long long drawTotalTime = 0;

	while (!System::IsSetToQuit()) {
		bool serverUpdated = false;
		updateStartTime = g_TimerMan.GetAbsoluteTime();

		PollSDLEvents();
		g_WindowMan.Update();
		TestResolutionChange();
		g_WindowMan.ClearBackbuffer();

		g_TimerMan.Update();

		// Simulation update, as many times as the fixed update step allows in the span since last frame draw.
		while (g_TimerMan.TimeForSimUpdate()) {
			ZoneScopedN("Simulation Update");

			serverUpdated = false;

			g_PerformanceMan.NewPerformanceSample();
			g_PerformanceMan.UpdateMSPSU();
			g_TimerMan.UpdateSim();

			// If this is the first sim update since a drawn one, clear the scene post effects and lights before anything (activities and their global scripts included) registers new ones.
			// Lights and shimmers are registered on every sim update, so they're cleared on every one, leaving the drawn frame only the last update's.
			if (g_TimerMan.SimUpdatesSinceDrawn() == 0) {
				g_PostProcessMan.ClearScenePostEffects();
			} else {
				g_PostProcessMan.ClearSceneLights();
			}

			g_PerformanceMan.StartPerformanceMeasurement(PerformanceMan::SimTotal);

			g_LuaMan.Update();

			g_UInputMan.Update();

			g_FrameMan.Update();

			{
				PerformanceMan::LogScope logScope("Sim: waiting for MOID drawing");
				g_MovableMan.CompleteQueuedMOIDDrawings();
			}

			g_ConsoleMan.Update();
			g_ActivityMan.Update();

			if (g_SceneMan.GetScene()) {
				PerformanceMan::LogScope logScope("Sim: scene update");
				g_SceneMan.GetScene()->Update();
			}
			{
				PerformanceMan::LogStages logStages;
				logStages.Next("Sim: sandbox");
				Sandbox::Update();
				logStages.Next("Sim: smoke grid");
				SmokeGrid::Update();
				logStages.Next("Sim: lightning");
				WeatherLightning::Update();
				logStages.Next("Sim: terrain fire");
				TerrainFire::Update();
				logStages.Next("Sim: burning units");
				ActorFire::Update();
				logStages.Next("Sim: terrain collapse");
				TerrainCollapse::Update();
				logStages.Next("Sim: liquids");
				FluidSim::Update();
				logStages.Next("Sim: units in water");
				ActorWater::Update();
			}

			// CCCP_TIME_SCALE: the simulation run faster than real time (3 for three times), for test runs; set once the game is going.
			{
				static bool timeScaleSet = false;
				if (!timeScaleSet) {
					timeScaleSet = true;
					if (const char* scale = std::getenv("CCCP_TIME_SCALE")) {
						float value = std::strtof(scale, nullptr);
						if (value > 0.1F && value < 20.0F) {
							g_TimerMan.SetTimeScale(value);
						}
					}
				}
			}
			g_LuaMan.ClearScriptTimings();
			{
				PerformanceMan::LogScope logScope("Sim: MovableMan total");
				// The Perf debug channel (or CCCP_PERF_LOG): the time the units' update (their scripts and AI included) takes, its average and worst over every 5 s, as
				// PERF lines in the console, for finding hitches.
				const bool perfLog = g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Perf);
				auto updateStart = std::chrono::steady_clock::now();
				g_MovableMan.Update();
				if (perfLog) {
					static double total = 0.0;
					static double worst = 0.0;
					static int count = 0;
					static auto windowStart = std::chrono::steady_clock::now();
					double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count();
					total += ms;
					worst = std::max(worst, ms);
					++count;
					if (std::chrono::steady_clock::now() - windowStart > std::chrono::seconds(5)) {
						g_ConsoleMan.PrintString("PERF units update: average " + std::to_string(total / std::max(count, 1)) + " ms, worst " + std::to_string(worst) + " ms, " + std::to_string(count) + " updates, " + std::to_string(g_MovableMan.GetActorCount()) + " actors");
						total = 0.0;
						worst = 0.0;
						count = 0;
						windowStart = std::chrono::steady_clock::now();
					}
				}
			}
			g_PerformanceMan.UpdateSortedScriptTimings(g_LuaMan.GetScriptTimings());

			{
				PerformanceMan::LogScope logScope("Sim: audio");
				g_AudioMan.Update();
				g_MusicMan.Update();
			}

			// The MOID table and grid are rebuilt on a worker from the end of MovableMan::Update; global scripts' LateUpdate looks objects up by ID
			// and in boxes, so it waits for the rebuild instead of reading a table another thread is clearing and refilling.
			{
				PerformanceMan::LogScope logScope("Sim: waiting for MOID drawing");
				g_MovableMan.CompleteQueuedMOIDDrawings();
			}
			g_ActivityMan.LateUpdateGlobalScripts();

			// This is to support hot reloading entities in SceneEditorGUI. It's a bit hacky to put it in Main like this, but PresetMan has no update in which to clear the value, and I didn't want to set up a listener for the job.
			// It's in this spot to allow it to be set by UInputMan update and ConsoleMan update, and read from ActivityMan update.
			g_PresetMan.ClearReloadEntityPresetCalledThisUpdate();

			g_PerformanceMan.StopPerformanceMeasurement(PerformanceMan::SimTotal);
			g_UInputMan.EndFrame();

			if (!g_ActivityMan.IsInActivity()) {
				g_TimerMan.PauseSim(true);

				if (!g_ActivityMan.ActivitySetToRestart()) {
					g_MenuMan.HandleTransitionIntoMenuLoop();
					RunMenuLoop();
				}
			}
			if (g_ActivityMan.ActivitySetToRestart()) {
				g_LoadingScreen.DrawLoadingSplash();
				g_WindowMan.UploadFrame();
				if (!g_ActivityMan.RestartActivity()) {
					break;
				}
			}
			if (g_ActivityMan.ActivitySetToResume()) {
				g_ActivityMan.ResumeActivity();
				g_PerformanceMan.ResetSimUpdateTimer();
				updateStartTime = g_TimerMan.GetAbsoluteTime();
			}
		}

		updateEndAndDrawStartTime = g_TimerMan.GetAbsoluteTime();
		updateTotalTime = updateEndAndDrawStartTime - updateStartTime;
		drawStartTime = updateEndAndDrawStartTime;

		{
			PerformanceMan::LogScope logScope("Draw: FrameMan::Draw total", true);
			g_FrameMan.Draw();
		}
		{
			PerformanceMan::LogScope logScope("Draw: post-process buffer", true);
			g_WindowMan.DrawPostProcessBuffer();
		}
		{
			PerformanceMan::LogScope logScope("Draw: upload and present", true);
			g_WindowMan.UploadFrame();
		}

		drawTotalTime = g_TimerMan.GetAbsoluteTime() - drawStartTime;
		g_PerformanceMan.UpdateMSPF(updateTotalTime, drawTotalTime);
		PerformanceMan::AddLogTime("Frame: all sim updates", static_cast<uint64_t>(updateTotalTime));
		PerformanceMan::AddLogTime("Frame: all drawing", static_cast<uint64_t>(drawTotalTime));
		g_PerformanceMan.UpdateLog();
	}
}

/// <summary>
/// Self-invoking lambda that installs exception handlers before Main is executed.
/// </summary>
static const bool RTESetExceptionHandlers = []() {
	RTEError::SetExceptionHandlers();
	return true;
}();

/// <summary>
/// Implementation of the main function.
/// </summary>
int main(int argc, char** argv) {
	install_allegro(SYSTEM_NONE, &errno, nullptr);

	// Automated test runs set CCCP_NO_GAMEPAD so a controller in use elsewhere on the machine can't steer them.
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | (std::getenv("CCCP_NO_GAMEPAD") ? 0 : SDL_INIT_GAMEPAD));

	s_StartupTiming.Mark("SDL");

	SDL_SetHint(SDL_HINT_MOUSE_AUTO_CAPTURE, "0");
	SDL_SetHint("SDL_ALLOW_TOPMOST", "0");
	// SDL_HideCursor();
	LoadRenderDoc();

	if (std::filesystem::exists("Base.rte/gamecontrollerdb.txt")) {
		SDL_AddGamepadMappingsFromFile("Base.rte/gamecontrollerdb.txt");
	}

#ifdef WIN32
	// Stops framespiking from our child threads being sat on for too long
	// TODO: use a better thread system that'll do what we want ASAP instead of letting the OS schedule all over us
	// Disabled for now because windows is great and this means when the game lags out it freezes the entire computer. Which we wouldn't expect with anything but REALTIME priority.
	// Because apparently high priority class is preferred over "processing mouse input"?!
	// SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif // WIN32

	// argv[0] actually unreliable for exe path and name, because of course, why would it be, why would anything be simple and make sense.
	// Just use it anyway until some dumb edge case pops up and it becomes a problem.
	System::Initialize(argv[0]);
	SeedRNG();
	s_StartupTiming.Mark("RenderDoc, gamepad mappings and System (working folder set-up)");

	InitializeManagers();

	HandleMainArgs(argc, argv);

	g_PresetMan.LoadAllDataModules();
	s_StartupTiming.Mark("loading the data modules (Data, Mods, Userdata)");

	ControlLink::Start();
	s_StartupTiming.Mark("control link");

	if (!System::IsInExternalModuleValidationMode()) {
		// Load the different input device icons. This can't be done during UInputMan::Create() because the icon presets don't exist so we need to do this after modules are loaded.
		g_UInputMan.LoadDeviceIcons();
		s_StartupTiming.Mark("input device icons");

		if (g_ConsoleMan.LoadWarningsExist()) {
			g_ConsoleMan.PrintString("WARNING: Encountered non-fatal errors during module loading!\nSee \"LogLoadingWarning.txt\" for information.");
			g_ConsoleMan.SaveLoadWarningLog(System::InstanceFile("LogLoadingWarning.txt"));
			// Open the console so the user is aware there are loading warnings.
			g_ConsoleMan.SetEnabled(true);
		} else {
			// Delete an existing log if there are no warnings so there's less junk in the root folder.
			if (std::filesystem::exists(System::GetWorkingDirectory() + System::InstanceFile("LogLoadingWarning.txt"))) {
				std::remove(System::InstanceFile("LogLoadingWarning.txt").c_str());
			}
		}

		s_StartupTiming.Mark("loading warnings log");

		if (!g_ActivityMan.Initialize()) {
			s_StartupTiming.Mark("ActivityMan");
			RunMenuLoop();
		}

		RunGameLoop();
	}

	g_ThreadMan.GetPriorityThreadPool().wait_for_tasks();
	g_ThreadMan.GetBackgroundThreadPool().wait_for_tasks();

	DestroyManagers();

	SDL_Quit();

	return EXIT_SUCCESS;
}

#ifdef _WIN32
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) { return main(__argc, __argv); }
#endif
