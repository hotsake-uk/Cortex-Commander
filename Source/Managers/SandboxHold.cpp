// "Hold ground" (Paint > Terrain): places drawn on the map where the ground never falls (TerrainCollapse's hold zones). A drag marks out a
// box; clicks put down the corners of a polygon, closed on its first corner, with Enter or a right click. A right click on a zone takes it
// away. Each zone put down or taken away is a step of the undo. Saved with a battle setup when its "Hold zones" is ticked.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		constexpr ImU32 c_HoldColor = IM_COL32(120, 200, 255, 255); //!< The zones' colour: a cool blue, apart from "Make it fall"'s warning orange.
		constexpr float c_DragPixels = 6.0F; //!< How far in window pixels the pointer goes with the button down before it is a drag (a box), not a click (a corner).

		bool s_Pressing = false; //!< The left button went down on the world with the tool in hand.
		ImVec2 s_PressAt; //!< Where, in the window.
		Vector s_PressScene; //!< Where, in the scene.
		bool s_Box = false; //!< The press became a drag: a box.
		bool s_RightDown = false; //!< The right button went down on the world (a click on a zone takes it away; a drag moves the view).
		ImVec2 s_RightAt;

		/// Sends a polygon to the sim to be put down.
		void PutDown(const std::vector<Vector>& corners) {
			if (corners.size() < 3) {
				return;
			}
			Stroke stroke;
			stroke.Kind = Tool::HoldGround;
			stroke.Position = corners.front();
			stroke.Points = corners;
			s_Queue.push_back(stroke);
		}

		/// The box from the press to the pointer, its corners round the pixels it covers, placed from the press across a wrap. Shift keeps it square.
		std::vector<Vector> BoxCorners(bool square) {
			Vector end = s_PressScene + g_SceneMan.ShortestDistance(s_PressScene, MouseScenePosition(), g_SceneMan.SceneWrapsX());
			if (square) {
				float side = std::max(std::abs(end.m_X - s_PressScene.m_X), std::abs(end.m_Y - s_PressScene.m_Y));
				end = s_PressScene + Vector(end.m_X >= s_PressScene.m_X ? side : -side, end.m_Y >= s_PressScene.m_Y ? side : -side);
			}
			float left = std::floor(std::min(s_PressScene.m_X, end.m_X));
			float top = std::floor(std::min(s_PressScene.m_Y, end.m_Y));
			float right = std::floor(std::max(s_PressScene.m_X, end.m_X)) + 1.0F;
			float bottom = std::floor(std::max(s_PressScene.m_Y, end.m_Y)) + 1.0F;
			return {Vector(left, top), Vector(right, top), Vector(right, bottom), Vector(left, bottom)};
		}

		/// Closes the polygon being drawn: put down with three corners or more, dropped with fewer.
		void CloseDraft() {
			PutDown(s_HoldDraft);
			s_HoldDraft.clear();
		}

		void DrawPolygon(ImDrawList* drawList, const std::vector<Vector>& zone, float scale, int fill, float thickness) {
			std::vector<ImVec2> corners = ZoneOnScreen(zone, scale);
			drawList->AddConcavePolyFilled(corners.data(), static_cast<int>(corners.size()), (c_HoldColor & 0x00FFFFFF) | (static_cast<ImU32>(fill) << 24));
			drawList->AddPolyline(corners.data(), static_cast<int>(corners.size()), c_HoldColor, ImDrawFlags_Closed, thickness);
		}
	} // namespace

	void HoldGroundInput() {
		ImGuiIO& io = ImGui::GetIO();
		if (!InGame() || CurrentTool().Kind != Tool::HoldGround) {
			s_HoldDraft.clear();
			s_Pressing = false;
			s_RightDown = false;
			return;
		}
		const bool onWorld = Sandbox::CapturesWorldClicks();
		if (onWorld && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			s_Pressing = true;
			s_Box = false;
			s_PressAt = io.MousePos;
			s_PressScene = MouseScenePosition();
		}
		if (s_Pressing) {
			if (!s_Box && s_HoldDraft.empty() && std::abs(io.MousePos.x - s_PressAt.x) + std::abs(io.MousePos.y - s_PressAt.y) >= c_DragPixels) {
				s_Box = true;
			}
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				s_Pressing = false;
				if (s_Box) {
					PutDown(BoxCorners(io.KeyShift));
				} else if (!s_HoldDraft.empty()) {
					// A corner, placed from the first so a polygon across a wrapping map's seam stays whole; on the first corner, the polygon closes.
					Vector fromFirst = g_SceneMan.ShortestDistance(s_HoldDraft.front(), s_PressScene, g_SceneMan.SceneWrapsX());
					if (s_HoldDraft.size() >= 3 && fromFirst.MagnitudeIsLessThan(ZoneCloseDistance())) {
						CloseDraft();
					} else {
						s_HoldDraft.push_back(s_HoldDraft.front() + fromFirst);
					}
				} else {
					s_HoldDraft.push_back(s_PressScene);
				}
			} else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				s_Pressing = false;
			}
		}
		if (onWorld && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			s_RightDown = true;
			s_RightAt = io.MousePos;
		}
		if (s_RightDown && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			s_RightDown = false;
			if (std::abs(io.MousePos.x - s_RightAt.x) + std::abs(io.MousePos.y - s_RightAt.y) < c_DragPixels) {
				if (!s_HoldDraft.empty()) {
					CloseDraft();
				} else if (TerrainCollapse::IsInHoldZone(MouseScenePosition())) {
					Stroke stroke;
					stroke.Kind = Tool::HoldGround;
					stroke.Choice = 1;
					stroke.Position = MouseScenePosition();
					s_Queue.push_back(stroke);
				}
			}
		}
		if (!io.WantTextInput && !s_HoldDraft.empty()) {
			if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
				CloseDraft();
			} else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
				s_HoldDraft.pop_back();
			}
		}
	}

	void DrawHoldZones() {
		if (!InGame()) {
			return;
		}
		const bool inHand = CurrentTool().Kind == Tool::HoldGround;
		if (!inHand && !TerrainCollapse::ShowsHoldZones()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		float scale = std::max(ScenePixelsPerWindowPixel(), 0.01F);
		for (const std::vector<Vector>& zone: TerrainCollapse::GetHoldZones()) {
			DrawPolygon(drawList, zone, scale, inHand ? 45 : 25, inHand ? 2.0F : 1.5F);
		}
		if (!inHand) {
			return;
		}
		if (s_Pressing && s_Box) {
			std::vector<Vector> box = BoxCorners(ImGui::GetIO().KeyShift);
			DrawPolygon(ImGui::GetForegroundDrawList(), box, scale, 60, 2.0F);
			ImVec2 topLeft = ToScreen(box.front());
			std::string size = std::to_string(static_cast<int>(box[1].m_X - box[0].m_X)) + " x " + std::to_string(static_cast<int>(box[2].m_Y - box[1].m_Y));
			ImGui::GetForegroundDrawList()->AddText(ImVec2(topLeft.x + 4.0F, topLeft.y - ImGui::GetTextLineHeight() - 2.0F), c_HoldColor, size.c_str());
		}
		if (!s_HoldDraft.empty()) {
			// The polygon so far and on to the pointer, its first corner ringed (filled when a click there closes it).
			ImDrawList* front = ImGui::GetForegroundDrawList();
			std::vector<ImVec2> corners = ZoneOnScreen(s_HoldDraft, scale);
			ImVec2 first = corners.front();
			Vector fromFirst = g_SceneMan.ShortestDistance(s_HoldDraft.front(), MouseScenePosition(), g_SceneMan.SceneWrapsX());
			corners.emplace_back(first.x + fromFirst.m_X / scale, first.y + fromFirst.m_Y / scale);
			if (s_HoldDraft.size() >= 3 && fromFirst.MagnitudeIsLessThan(ZoneCloseDistance())) {
				front->AddCircleFilled(first, 7.0F, c_HoldColor, 16);
			}
			front->AddPolyline(corners.data(), static_cast<int>(corners.size()), c_HoldColor, 0, 2.0F);
			front->AddCircle(first, 7.0F, c_HoldColor, 16, 2.0F);
			for (size_t i = 1; i < s_HoldDraft.size(); ++i) {
				front->AddCircleFilled(corners[i], 3.0F, c_HoldColor, 8);
			}
		}
	}
} // namespace SandboxDetail
