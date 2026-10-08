#pragma once
#include "Singleton.h"
#include "LightingSettings.h"
#include "Vector.h"
#include <memory>

struct ImFont;

#define g_DebugMan DebugMan::Instance()

namespace RTE {
	class RenderTarget;
	struct GameViewRect;
	class DebugMan : public Singleton<DebugMan> {
		friend class SettingsMan;
	public:
		void Draw();
		void DrawImGui();

		/// Draws the debug overlays the settings ask for, over player 1's view of the game with ImGui's foreground list, in or out of the sandbox.
		/// The one place every overlay is drawn from, so each is a settings check and a call here (see DebugDraw.h for the helpers they share).
		void DrawOverlays();

		/// Opens the settings panel at its debug part.
		void ShowDebugOptions() { m_ShowDebugWindow = true; }

		/// Opens the Graphics Lab, the live lighting and post-processing tuning window.
		void ShowGraphicsLab() { m_ShowGraphicsLab = true; }

		/// Toggles the World Debug window (time of day, weather, lighting and game speed), bound to F6.
		void ToggleWorldDebug() { m_ShowWorldDebug = !m_ShowWorldDebug; }

		/// Toggles photo mode (F8): frozen time, a free camera, look controls and window resolution screenshots.
		void TogglePhotoMode() { m_ShowPhotoMode = !m_ShowPhotoMode; }

		/// Gets whether photo mode is open and hiding the HUD and screen text.
		bool IsPhotoModeHidingHUD() const { return m_PhotoModeActive && m_PhotoHideHUD; }

		/// Gets whether photo mode is open. It decides for itself whether time is frozen.
		bool IsPhotoModeOpen() const { return m_ShowPhotoMode || m_PhotoModeActive; }

		/// Gets and clears whether a photo mode screenshot was asked for. WindowMan takes it from the finished frame, before ImGui is drawn.
		bool ConsumeScreenshotRequest() {
			bool requested = m_ScreenshotRequested;
			m_ScreenshotRequested = false;
			return requested;
		}

		/// Gets how many times the internal resolution photo mode screenshots are saved at (1 = as shown in the window).
		int GetScreenshotScale() const { return m_PhotoScale > 0 ? m_PhotoScale + 1 : 0; }

		/// Which side of the window a tool panel docks at.
		enum class PanelSide { Left, Right };

		/// Begins a tool window. With docking on (the default) it is a panel fixed at one side of the game's picture, sharing that side with any others open there;
		/// with docking off it's an ordinary floating window. Use it like ImGui::Begin, and close with ImGui::End.
		/// @param name The window's title. @param open Set to false when the player closes it; nullptr for no close button. @param side Where it docks.
		bool BeginPanel(const char* name, bool* open, PanelSide side);

		/// Ends a tool window begun with BeginPanel, whatever BeginPanel returned.
		void EndPanel();

		/// Makes the game's own pixel font available to the tool windows, the first time it can be. Call between frames (not between ImGui's NewFrame and Render).
		void PrepareFonts();

		/// Gets whether the tool windows are being drawn in the game's pixel font, which should only ever be drawn at whole sizes.
		bool UsingPixelFont() const { return m_PixelFont && m_PixelFontInUse; }

		/// Gets whether any tool window that uses the mouse is open.
		bool AnyToolWindowOpen() const;

		/// The one key for all the tool windows (Tab in a game): closes every one that is open, remembering which they were, or opens those again.
		/// In the Sandbox game mode closing them puts you in your character and opening them takes you back to the god view.
		/// @param atPointer Closing in the Sandbox game mode: put the character down where the mouse points instead of where it stands.
		void ToggleTools(bool atPointer = false);

		/// Closes every tool window, remembering which were open for the next ToggleTools.
		void CloseTools();

		/// Opens the tool windows that were open when they were last closed together.
		void OpenTools();

		/// Draws the controls for the tool windows themselves (how big their text and controls are, how wide the side panels, docked or floating), folded away under a heading.
		/// For the top of each tool window, so they can be found from any of them.
		void DrawToolWindowControls();

		bool DrawSpriteBounds() { return m_DrawSpriteBounds; }
		constexpr bool DrawNoGravBoxes() { return false; }
		bool DrawBigTextureBounds() { return false; }
		bool DrawTilingBounds() { return false; }

		bool FreeCamEnabled() { return m_EnableFreeCam; }
		float FreeCamZoom() { return m_FreeCamZoom; }
		Camera* GetFreeCam() { return m_FreeCam.get(); }

	private:
		bool m_ShowDebugWindow{false};
		bool m_ImGuiDemoWindow{false};
		bool m_ShowPerformanceMan{false};
		bool m_ShowGraphicsLab{false};
		bool m_PanelsOverlay{true}; //!< Docked panels lie over the game's picture, which keeps its full size, instead of pushing it into the space between them.
		bool m_DockPanels{true}; //!< Tool windows are panels at the sides of the game's picture, not floating over it.
		float m_PanelWidth{380.0F}; //!< Width of the docked panels, before the interface scale.
		float m_ToolScale{0.7F}; //!< How big the tool windows' text and controls are, as a share of the size that follows the window's height.

		/// Gets how much the tool windows are scaled: with the window's height (720 px = 1x), times the size the player chose.
		float GetToolScale() const;
		bool m_PixelFont{true}; //!< The tool windows use the game's own pixel font, not the smooth one.
		bool m_PixelFontInUse{false}; //!< Whether a pixel font is the one being drawn with this frame.
		int m_PixelFontTries{0}; //!< Frames waited so far for the game's font art to be loadable.
		::ImFont* m_PixelFonts[4]{}; //!< The game's small font at 1x to 4x, each baked at its own size so no pixel is ever blurred.
		int m_PanelKind{0}; //!< What the BeginPanel in progress began, for EndPanel: 0 a floating window, 1 a tab that isn't the one showing, 2 the tab showing.
		unsigned m_RememberedTools{0}; //!< The tool windows that were open when they were last closed together, as bits.
		int m_PanelsThisFrame[2]{0, 0}; //!< How many panels have been begun at each side so far this frame.
		int m_PanelsLastFrame[2]{0, 0}; //!< How many there were at each side last frame, which is how the side is shared out this frame.
		bool m_ShowWorldDebug{false};
		bool m_ShowPhotoMode{false};
		bool m_PhotoModeActive{false}; //!< Whether photo mode has taken over (time frozen, settings saved), to restore things when it closes.
		bool m_PhotoFreeze{true};
		bool m_PhotoHideHUD{true};
		bool m_PhotoKeepLook{false};
		bool m_PhotoPreviousHUDDisabled{false};
		bool m_ScreenshotRequested{false};
		int m_PhotoScale{0}; //!< Resolution choice: 0 = as shown in the window, 1..3 = 2x..4x the internal resolution.
		Vector m_PhotoCameraCenter;
		LightingSettings m_PhotoSavedSettings;
		bool m_ReleasedMouseForImGui{false}; //!< Whether the mouse was taken from the game so ImGui windows can be used.

		bool m_FreezeSim{false}; //!< "Freeze simulation" on the Debug page: the world stands still, in any game, until it's unticked or stepped.
		bool m_FrozeSim{false}; //!< Whether it's this that paused the simulation, so only this unpauses it.
		int m_FreezeStepsWanted{0}; //!< Updates to let the frozen world do, from the Step buttons.

		/// Holds the simulation paused while "Freeze simulation" is ticked, letting through the updates the Step buttons ask for. Runs after the sandbox's own pause, so it wins.
		void UpdateFreeze();

		bool m_DrawCameraBounds{false};
		bool m_DrawSpriteBounds{false};

		/// The settings panel (F6): everything that can be tuned while the game runs, in categories, searchable, with presets. In SettingsPanel.cpp.
		void SettingsGUI();
		void FreeCamGUI();
		int m_SettingsCategory{0}; //!< Which category of the settings panel is showing.

		/// Gets how wide the docked panel at a side is, in window pixels.
		float GetPanelWidth(PanelSide side) const;

	public:
		/// Gets the part of the game's picture not covered by docked panels this frame, in window pixels: where things that must stay in sight (the sandbox bar, banners) go.
		GameViewRect GetUncoveredView() const;

	private:
		void PhotoModeGUI();

		/// Restores what photo mode changed, when it closes.
		void EndPhotoMode();

		/// Gives the mouse to ImGui while any interactive debug window is open, and back to the game when they all close.
		void UpdateMouseOwnership();

		bool m_ShowActorDebugGui{false};
		std::unique_ptr<RenderTarget> m_DebugDrawTarget;
		void ActorDrawDebugGUI();


		bool m_EnableFreeCam{false};
		float m_FreeCamZoom{1.0f};
		std::unique_ptr<Camera> m_FreeCam;
	};
} // namespace RTE
