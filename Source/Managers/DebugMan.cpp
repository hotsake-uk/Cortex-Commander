#include "DebugMan.h"
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
#include "tracy/TracyOpenGL.hpp"

using namespace RTE;

void Draw() {
}

void DebugMan::DrawImGui() {
	if (m_ShowDebugWindow) {
		DebugOptionsGUI();
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

	if (m_ShowGraphicsLab) {
		GraphicsLabGUI();
	}
}

void DebugMan::GraphicsLabGUI() {
	if (ImGui::Begin("Graphics Lab", &m_ShowGraphicsLab)) {
		LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
		const ImGuiColorEditFlags linearColorFlags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;

		ImGui::SeparatorText("Sky and ambient light");
		ImGui::Checkbox("Lighting enabled", &settings.Enabled);
		ImGui::ColorEdit3("Ambient (linear)", &settings.Ambient.x, linearColorFlags);
		ImGui::ColorEdit3("Sky (linear)", &settings.SkyColor.x, linearColorFlags);
		ImGui::ColorEdit3("Foreground floor (linear)", &settings.ForegroundAmbient.x, linearColorFlags);
		ImGui::SliderFloat("Air falloff", &settings.AirFalloff, 0.8F, 0.995F, "%.3f");
		ImGui::SliderFloat("Terrain falloff", &settings.SolidFalloff, 0.1F, 0.95F, "%.2f");
		ImGui::SliderInt("Propagation steps/frame", &settings.PropagationIterationsPerFrame, 1, 32);
		ImGui::SliderFloat("God rays", &settings.GodRays, 0.0F, 2.0F);
		ImGui::SliderFloat("God ray decay", &settings.GodRayDecay, 0.85F, 0.999F, "%.3f");
		ImGui::SliderFloat("Atmosphere haze", &settings.AtmosphereHaze, 0.0F, 1.0F);
		ImGui::ColorEdit3("Atmosphere (linear)", &settings.AtmosphereColor.x, linearColorFlags);
		ImGui::SliderFloat("Time of day (h)", &settings.TimeOfDay, 0.0F, 24.0F, "%.2f");
		ImGui::SliderFloat("Day length (min, 0 = fixed)", &settings.DayLengthMinutes, 0.0F, 60.0F, "%.1f");

		ImGui::SeparatorText("Weather");
		ImGui::Combo("Precipitation", &settings.WeatherType, "Clear\0Rain\0Snow\0");
		ImGui::SliderFloat("Intensity##Weather", &settings.WeatherIntensity, 0.0F, 1.0F);
		ImGui::SliderFloat("Wind (px/s)", &settings.Wind, -400.0F, 400.0F);

		ImGui::SeparatorText("Glows and dynamic lights");
		ImGui::SliderFloat("Glow light intensity", &settings.GlowLightIntensity, 0.0F, 8.0F);
		ImGui::SliderFloat("Glow light radius", &settings.GlowLightRadiusScale, 0.5F, 10.0F);
		ImGui::SliderFloat("Shadow strength", &settings.ShadowStrength, 0.0F, 1.0F);
		ImGui::SliderFloat("Emissive intensity", &settings.EmissiveIntensity, 0.0F, 4.0F);
		ImGui::SliderFloat("Edge lighting", &settings.EdgeLighting, 0.0F, 1.0F);

		ImGui::SeparatorText("Distortion");
		ImGui::Checkbox("Distortion enabled", &settings.DistortionEnabled);
		ImGui::SliderFloat("Heat haze (px)", &settings.HeatHaze, 0.0F, 6.0F);
		ImGui::SliderFloat("Shockwave strength", &settings.ShockwaveStrength, 0.0F, 3.0F);

		ImGui::SeparatorText("Terrain effects");
		ImGui::Checkbox("Scorch marks", &settings.ScorchMarks);
		ImGui::SliderFloat("Embers", &settings.Embers, 0.0F, 3.0F);
		ImGui::SliderFloat("Hot spot cooling (s)", &settings.HotSpotSeconds, 0.0F, 10.0F);

		ImGui::SeparatorText("Bloom");
		ImGui::Checkbox("Bloom enabled", &settings.BloomEnabled);
		ImGui::SliderFloat("Threshold", &settings.BloomThreshold, 0.0F, 4.0F);
		ImGui::SliderFloat("Knee", &settings.BloomKnee, 0.01F, 1.0F);
		ImGui::SliderFloat("Intensity", &settings.BloomIntensity, 0.0F, 3.0F);

		ImGui::SeparatorText("Tonemapping and grading");
		ImGui::SliderFloat("Exposure", &settings.Exposure, 0.1F, 4.0F);
		ImGui::SliderFloat("Highlight shoulder", &settings.ShoulderStart, 0.3F, 1.0F);
		ImGui::SliderFloat("Saturation", &settings.Saturation, 0.0F, 2.0F);
		ImGui::SliderFloat("Vignette", &settings.Vignette, 0.0F, 1.0F);
		ImGui::SliderFloat("Temperature", &settings.Temperature, -1.0F, 1.0F);
		ImGui::SliderFloat("Tint", &settings.Tint, -1.0F, 1.0F);
		ImGui::SliderFloat("Contrast", &settings.Contrast, 0.5F, 1.6F);
		ImGui::ColorEdit3("Shadow tint", &settings.ShadowTint.x, linearColorFlags);
		ImGui::ColorEdit3("Highlight tint", &settings.HighlightTint.x, linearColorFlags);
		ImGui::SliderFloat("Film grain", &settings.FilmGrain, 0.0F, 1.0F);
		ImGui::SliderFloat("Chromatic aberration (px)", &settings.ChromaticAberration, 0.0F, 4.0F);

		ImGui::SeparatorText("Debug");
		ImGui::Combo("View", &settings.DebugView, "Final image\0Lighting on grey\0Sky light only\0Dynamic light only\0Normals\0Distortion\0");

		ImGui::SeparatorText("Stats");
		if (SceneLighting* lighting = g_PostProcessMan.GetSceneLighting()) {
			ImGui::Text("Light grid: %d x %d cells of %dpx", lighting->GetGridWidth(), lighting->GetGridHeight(), lighting->GetGridCellSize());
			ImGui::Text("Dynamic lights last screen: %d", lighting->GetLastLightCount());
		}

		ImGui::Separator();
		if (ImGui::Button("Reset to defaults")) {
			settings = LightingSettings();
		}
		ImGui::SameLine();
		if (ImGui::Button("Save to Settings.ini")) {
			g_PostProcessMan.AdoptAtmosphereAsPlayers();
			g_SettingsMan.UpdateSettingsFile();
		}
	}
	ImGui::End();
}

void DebugMan::DebugOptionsGUI() {
	if (ImGui::Begin("Debug Options", &m_ShowDebugWindow)) {
		ImGui::Checkbox("Show Performance Stats", &m_ShowPerformanceMan);
		ImGui::Checkbox("Show Graphics Lab", &m_ShowGraphicsLab);
		ImGui::Checkbox("Show ImGui Demo Window", &m_ShowDebugWindow);
		ImGui::Checkbox("Show Actor debug", &m_ShowActorDebugGui);
		if (ImGui::TreeNode("Debug Draw")) {
			ImGui::Checkbox("Draw Camera bounds", &m_DrawCameraBounds);

			if (ImGui::TreeNode("Sprite Draw")) {
				ImGui::Checkbox("Draw frustum tests", &m_DrawSpriteBounds);
				ImGui::TreePop();
			}

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

			ImGui::TreePop();
		}
	}
	ImGui::End();
}


void DebugMan::ActorDrawDebugGUI() {
	ZoneScoped;
	static std::shared_ptr<RenderBatch> batch = std::make_unique<RenderBatch>();
	if (ImGui::Begin("Actor Draw Debug", &m_ShowActorDebugGui)) {
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
	ImGui::End();
}
