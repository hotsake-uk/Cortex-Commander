// The sandbox's map (RC-8): the whole scene small, with every unit, the view, pings and "no route" markers, and it takes clicks: a click
// looks there, a drag selects, a right click gives the selected units the order the command mode says, there.

#include "SandboxInternal.h"
#include "glad/gl.h"

namespace SandboxDetail {
	namespace {
		/// The terrain, small: rebuilt every few seconds, as the ground changes slowly at this scale.
		struct MapPicture {
			GLuint Texture = 0;
			int Width = 0;
			int Height = 0;
			const void* Scene = nullptr;
			double BuiltAt = -100.0; //!< ImGui time.
		};
		MapPicture s_MapPicture;
		bool s_MapDragging = false;
		ImVec2 s_MapDragStart;

		void UpdateMapPicture() {
			Scene* scene = g_SceneMan.GetScene();
			SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
			if (!terrain) {
				return;
			}
			double now = ImGui::GetTime();
			if (s_MapPicture.Scene == scene && now - s_MapPicture.BuiltAt < 3.0) {
				return;
			}
			int sceneWidth = g_SceneMan.GetSceneWidth();
			int sceneHeight = g_SceneMan.GetSceneHeight();
			int step = std::max(1, std::max(sceneWidth / 360, sceneHeight / 240));
			int width = std::max(1, sceneWidth / step);
			int height = std::max(1, sceneHeight / step);
			std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4, 0);
			for (int y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					int material = terrain->GetMaterialPixel(x * step, y * step);
					unsigned char* pixel = &pixels[(static_cast<size_t>(y) * width + x) * 4];
					if (material != g_MaterialAir) {
						// Ground in a muted earth tone, darker deeper down; liquid in blue.
						float depth = static_cast<float>(y) / static_cast<float>(height);
						bool liquid = FluidSim::IsLiquid(material);
						pixel[0] = static_cast<unsigned char>(liquid ? 50 : 150 - 60 * depth);
						pixel[1] = static_cast<unsigned char>(liquid ? 90 : 120 - 50 * depth);
						pixel[2] = static_cast<unsigned char>(liquid ? 160 : 85 - 35 * depth);
						pixel[3] = 235;
					} else {
						pixel[0] = 15;
						pixel[1] = 22;
						pixel[2] = 35;
						pixel[3] = 190;
					}
				}
			}
			if (!s_MapPicture.Texture) {
				glGenTextures(1, &s_MapPicture.Texture);
			}
			glBindTexture(GL_TEXTURE_2D, s_MapPicture.Texture);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			glBindTexture(GL_TEXTURE_2D, 0);
			s_MapPicture.Width = width;
			s_MapPicture.Height = height;
			s_MapPicture.Scene = scene;
			s_MapPicture.BuiltAt = now;
		}
	} // namespace

	void DrawMinimap() {
		if (!g_SettingsMan.ShowSandboxMinimap() || !InGame() || !g_SceneMan.GetScene()) {
			s_MapDragging = false;
			return;
		}
		UpdateMapPicture();
		if (!s_MapPicture.Texture) {
			return;
		}
		GameViewRect view = g_WindowMan.GetGameViewRect();
		ImGui::SetNextWindowPos(ImVec2(view.x + 12.0F, view.y + 48.0F), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(340.0F, 250.0F), ImGuiCond_FirstUseEver);
		bool open = true;
		if (ImGui::Begin("Map###SandboxMap", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse)) {
			ImGuiIO& io = ImGui::GetIO();
			ImVec2 room = ImGui::GetContentRegionAvail();
			float aspect = static_cast<float>(s_MapPicture.Height) / static_cast<float>(std::max(1, s_MapPicture.Width));
			float width = std::max(room.x, 40.0F);
			float height = width * aspect;
			if (height > room.y && room.y > 40.0F) {
				height = room.y;
				width = height / aspect;
			}
			ImVec2 topLeft = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##map", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
			bool hovered = ImGui::IsItemHovered();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddImage(static_cast<ImTextureID>(s_MapPicture.Texture), topLeft, ImVec2(topLeft.x + width, topLeft.y + height));
			float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
			float sceneHeight = static_cast<float>(g_SceneMan.GetSceneHeight());
			auto toMap = [&](const Vector& position) { return ImVec2(topLeft.x + position.m_X / sceneWidth * width, topLeft.y + position.m_Y / sceneHeight * height); };
			auto toScene = [&](const ImVec2& point) {
				Vector scene(std::clamp((point.x - topLeft.x) / width, 0.0F, 1.0F) * sceneWidth, std::clamp((point.y - topLeft.y) / height, 0.0F, 1.0F) * sceneHeight);
				g_SceneMan.WrapPosition(scene);
				return scene;
			};
			drawList->PushClipRect(topLeft, ImVec2(topLeft.x + width, topLeft.y + height), true);

			// The view.
			Vector viewTopLeft = g_CameraMan.GetOffset(0);
			float scale = ScenePixelsPerWindowPixel();
			Vector viewBottomRight = viewTopLeft + Vector(view.w * scale, view.h * scale);
			drawList->AddRect(toMap(viewTopLeft), toMap(viewBottomRight), IM_COL32(255, 255, 255, 150), 0.0F, 0, 1.5F);

			// The units, by side; outside the God mode, other sides' only where the player's side can see. Selected ones ringed.
			const Activity* activity = g_ActivityMan.GetActivity();
			int viewerTeam = activity ? activity->GetTeamOfPlayer(0) : -1;
			for (const Actor* actor: SandboxAccess::Actors()) {
				if (dynamic_cast<const ADoor*>(actor) || actor->IsDead()) {
					continue;
				}
				if (actor->IsHiddenByFog() || (!Sandbox::IsGodMode() && viewerTeam >= 0 && actor->GetTeam() != viewerTeam && g_SceneMan.IsUnseen(actor->GetPos().GetFloorIntX(), actor->GetPos().GetFloorIntY(), viewerTeam))) {
					continue;
				}
				ImVec2 at = toMap(actor->GetPos());
				float radius = actor->IsInGroup("Brains") ? 3.5F : 2.2F;
				ImU32 color = actor->GetTeam() >= 0 && actor->GetTeam() < c_Sides ? c_SideColors[actor->GetTeam()] : IM_COL32(200, 200, 200, 255);
				drawList->AddCircleFilled(at, radius + 1.0F, IM_COL32(0, 0, 0, 200));
				drawList->AddCircleFilled(at, radius, color);
				if (std::any_of(s_Selected.begin(), s_Selected.end(), [actor](const UnitRef& ref) { return RefersTo(ref, actor); })) {
					drawList->AddCircle(at, radius + 3.0F, IM_COL32(255, 255, 255, 220), 0, 1.0F);
				}
			}

			// Battle Command's fog of war: where enemies were last seen, hollow and fading.
			for (const FogGhost& ghost: s_FogGhosts) {
				const float age = FogGhostAge(ghost);
				if (age < 1.0F) {
					ImU32 color = ghost.Team >= 0 && ghost.Team < c_Sides ? c_SideColors[ghost.Team] : IM_COL32(200, 200, 200, 255);
					drawList->AddCircle(toMap(ghost.Pos), ghost.Brain ? 3.5F : 2.2F, (color & 0x00FFFFFF) | (static_cast<ImU32>(200.0F * (1.0F - age)) << IM_COL32_A_SHIFT), 0, 1.0F);
				}
			}

			// Pings: units of the selection's side under fire (RC-7) in red, and where the fighting is in yellow.
			double time = ImGui::GetTime();
			float pulse = static_cast<float>(std::fmod(time, 1.0));
			for (const AttackPing& ping: s_AttackPings) {
				float age = static_cast<float>(time - ping.Time);
				if (age < 6.0F) {
					int alpha = static_cast<int>(230.0F * (1.0F - age / 6.0F) * (1.0F - pulse));
					drawList->AddCircle(toMap(ping.Position), 3.0F + pulse * 9.0F, IM_COL32(255, 70, 60, alpha), 0, 2.0F);
				}
			}
			if (s_ActionSpotValid) {
				drawList->AddCircle(toMap(s_ActionSpot), 4.0F + pulse * 6.0F, IM_COL32(248, 220, 90, static_cast<int>(180.0F * (1.0F - pulse))), 0, 1.5F);
			}
			// Places units couldn't get to (RC-7).
			for (const NoRoute& marker: s_NoRoutes) {
				ImVec2 at = toMap(marker.Destination);
				drawList->AddLine(ImVec2(at.x - 3.0F, at.y - 3.0F), ImVec2(at.x + 3.0F, at.y + 3.0F), IM_COL32(239, 90, 80, 255), 1.5F);
				drawList->AddLine(ImVec2(at.x - 3.0F, at.y + 3.0F), ImVec2(at.x + 3.0F, at.y - 3.0F), IM_COL32(239, 90, 80, 255), 1.5F);
			}
			// The patrol route being clicked out (RC-4), so points can be laid on the map as well.
			for (size_t i = 0; i < s_PatrolDraft.size(); ++i) {
				ImVec2 at = toMap(s_PatrolDraft[i]);
				if (i > 0) {
					drawList->AddLine(toMap(s_PatrolDraft[i - 1]), at, c_CommandModeColors[static_cast<int>(CommandMode::Patrol)], 1.0F);
				}
				drawList->AddCircleFilled(at, 2.5F, c_CommandModeColors[static_cast<int>(CommandMode::Patrol)]);
			}

			// The clicks. Left: a click looks there, a drag selects the units in its box. Right: the selected units' order, there.
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s_MapDragging = true;
				s_MapDragStart = io.MousePos;
			}
			if (s_MapDragging) {
				ImVec2 now = io.MousePos;
				bool dragged = std::abs(now.x - s_MapDragStart.x) + std::abs(now.y - s_MapDragStart.y) > 4.0F;
				if (dragged) {
					drawList->AddRect(ImVec2(std::min(s_MapDragStart.x, now.x), std::min(s_MapDragStart.y, now.y)), ImVec2(std::max(s_MapDragStart.x, now.x), std::max(s_MapDragStart.y, now.y)), IM_COL32(255, 255, 255, 200), 0.0F, 0, 1.0F);
				}
				if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
					s_MapDragging = false;
					if (dragged) {
						Stroke stroke;
						stroke.Kind = Tool::Select;
						// (Unwrapped corners, so a box across the map is the box drawn.)
						stroke.Position = Vector(std::clamp((std::min(s_MapDragStart.x, now.x) - topLeft.x) / width, 0.0F, 1.0F) * sceneWidth, std::clamp((std::min(s_MapDragStart.y, now.y) - topLeft.y) / height, 0.0F, 1.0F) * sceneHeight);
						stroke.Position2 = Vector(std::clamp((std::max(s_MapDragStart.x, now.x) - topLeft.x) / width, 0.0F, 1.0F) * sceneWidth, std::clamp((std::max(s_MapDragStart.y, now.y) - topLeft.y) / height, 0.0F, 1.0F) * sceneHeight);
						s_Queue.push_back(stroke);
					} else {
						s_FreeCamera = true;
						s_FollowTarget = UnitRef();
						s_FollowAction = false;
						s_CameraCenter = toScene(now);
					}
				}
			}
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				Vector point = toScene(io.MousePos);
				if (s_CommandMode == CommandMode::Patrol) {
					s_PatrolDraft.push_back(point);
				} else if (!s_Selected.empty()) {
					Stroke stroke;
					stroke.Kind = Tool::Command;
					stroke.Position = point;
					stroke.Count = io.KeyShift ? 42 : 41;
					stroke.Clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Right);
					s_Queue.push_back(stroke);
				}
			}
			drawList->PopClipRect();
			if (hovered && !s_MapDragging) {
				ImGui::SetTooltip("Click: look here.  Drag: select the units in the box.\nRight click: %s here (Shift: add it to their plans).", s_CommandMode == CommandMode::Patrol ? "a point of the patrol route" : c_CommandModeNames[static_cast<int>(s_CommandMode == CommandMode::Select ? CommandMode::Move : s_CommandMode)]);
			}
		}
		ImGui::End();
		if (!open) {
			g_SettingsMan.SetShowSandboxMinimap(false);
		}
	}
} // namespace SandboxDetail
