#pragma once
#include "Singleton.h"
#include "LightingSettings.h"
#include "Vector.h"
#include <memory>

#define g_DebugMan DebugMan::Instance()

namespace RTE {
	class RenderTarget;
	class DebugMan : public Singleton<DebugMan> {
		friend class SettingsMan;
	public:
		void Draw();
		void DrawImGui();

		void ShowDebugOptions() { m_ShowDebugWindow = true; }

		/// Opens the Graphics Lab, the live lighting and post-processing tuning window.
		void ShowGraphicsLab() { m_ShowGraphicsLab = true; }

		/// Toggles the World Debug window (time of day, weather, lighting and game speed), bound to F6.
		void ToggleWorldDebug() { m_ShowWorldDebug = !m_ShowWorldDebug; }

		/// Toggles photo mode (F8): frozen time, a free camera, look controls and window resolution screenshots.
		void TogglePhotoMode() { m_ShowPhotoMode = !m_ShowPhotoMode; }

		/// Gets whether photo mode is open and hiding the HUD and screen text.
		bool IsPhotoModeHidingHUD() const { return m_PhotoModeActive && m_PhotoHideHUD; }

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
		bool m_DockPanels{true}; //!< Tool windows are panels at the sides of the game's picture, not floating over it.
		float m_PanelWidth{380.0F}; //!< Width of the docked panels, before the interface scale.
		float m_ToolScale{0.7F}; //!< How big the tool windows' text and controls are, as a share of the size that follows the window's height.

		/// Gets how much the tool windows are scaled: with the window's height (720 px = 1x), times the size the player chose.
		float GetToolScale() const;
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

		bool m_DrawCameraBounds{false};
		bool m_DrawSpriteBounds{false};

		void DebugOptionsGUI();
		void GraphicsLabGUI();
		void WorldDebugGUI();
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
