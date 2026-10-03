#pragma once
#include "Singleton.h"
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

		bool m_DrawCameraBounds{false};
		bool m_DrawSpriteBounds{false};

		void DebugOptionsGUI();
		void GraphicsLabGUI();

		bool m_ShowActorDebugGui{false};
		std::unique_ptr<RenderTarget> m_DebugDrawTarget;
		void ActorDrawDebugGUI();


		bool m_EnableFreeCam{false};
		float m_FreeCamZoom{1.0f};
		std::unique_ptr<Camera> m_FreeCam;
	};
} // namespace RTE
