#pragma once

#include "CameraMan.h"
#include "FrameMan.h"
#include "SceneMan.h"
#include "WindowMan.h"
#include "imgui/imgui.h"

#include <algorithm>

namespace RTE {

	/// Helpers for debug overlays drawn with ImGui over the game's picture (the foreground draw list, so text stays crisp at window resolution).
	/// They work in player 1's view, which fills the game's picture, whether or not the sandbox window is open.
	namespace DebugDraw {

		/// How many scene pixels one window pixel covers (player 1's screen fills the game's picture).
		inline float ScenePixelsPerWindowPixel() { return static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) / std::max(1.0F, g_WindowMan.GetGameViewRect().w); }

		/// The top left corner of the game's picture in the window. With tool panels docked at the sides it isn't the window's own corner.
		inline ImVec2 ViewOrigin() {
			GameViewRect view = g_WindowMan.GetGameViewRect();
			return ImVec2(view.x, view.y);
		}

		/// Where a scene position is in the window, taking the shortest way round a wrapping scene from the camera.
		inline ImVec2 ToScreen(const Vector& scenePosition) {
			Vector onScreen = g_SceneMan.ShortestDistance(g_CameraMan.GetOffset(0), scenePosition, g_SceneMan.SceneWrapsX());
			float scale = ScenePixelsPerWindowPixel();
			ImVec2 origin = ViewOrigin();
			return ImVec2(origin.x + onScreen.m_X / scale, origin.y + onScreen.m_Y / scale);
		}

		/// The scene position under the mouse pointer, wrapped into the scene.
		inline Vector MouseScenePosition() {
			const ImVec2& mouse = ImGui::GetIO().MousePos;
			ImVec2 origin = ViewOrigin();
			Vector position = g_CameraMan.GetOffset(0) + Vector(mouse.x - origin.x, mouse.y - origin.y) * ScenePixelsPerWindowPixel();
			g_SceneMan.WrapPosition(position);
			return position;
		}

		/// The box of the scene player 1 sees, in scene pixels (its corner may be outside the scene on a wrapping one).
		inline Box ViewBox() {
			return Box(g_CameraMan.GetOffset(0), static_cast<float>(g_FrameMan.GetPlayerScreenWidth()), static_cast<float>(g_FrameMan.GetPlayerScreenHeight()));
		}
	} // namespace DebugDraw
} // namespace RTE
