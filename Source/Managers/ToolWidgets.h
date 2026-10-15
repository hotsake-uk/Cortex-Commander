#pragma once

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>

namespace RTE {
	/// The colours of the tool windows, taken from the game's own menu skin (Base.rte/GUIs/Skins/Menus/SkinBlue.png and its lettering): navy wells with a
	/// periwinkle edge, slate panels and the gold of the menu text. Everything the Sandbox and settings panel draw by hand takes them from here.
	/// The UI theme (see UITheme.h) sets the panel, well and edge colours; ClassicTheme holds them as the game has them.
	namespace ToolTheme {
		namespace ClassicTheme {
			constexpr ImU32 Panel = IM_COL32(59, 65, 83, 255);
			constexpr ImU32 Well = IM_COL32(12, 20, 39, 255);
			constexpr ImU32 WellHover = IM_COL32(27, 37, 58, 255);
			constexpr ImU32 WellPressed = IM_COL32(12, 12, 28, 255);
			constexpr ImU32 Edge = IM_COL32(108, 118, 168, 255);
			constexpr ImU32 EdgeLight = IM_COL32(193, 172, 198, 255);
			constexpr ImU32 EdgeDark = IM_COL32(24, 28, 55, 255);
		} // namespace ClassicTheme
		inline ImU32 Panel = ClassicTheme::Panel;
		inline ImU32 Well = ClassicTheme::Well;
		inline ImU32 WellHover = ClassicTheme::WellHover;
		inline ImU32 WellPressed = ClassicTheme::WellPressed;
		inline ImU32 Edge = ClassicTheme::Edge;
		inline ImU32 EdgeLight = ClassicTheme::EdgeLight;
		inline ImU32 EdgeDark = ClassicTheme::EdgeDark;
		constexpr ImU32 Gold = IM_COL32(252, 209, 19, 255);
		constexpr ImU32 GoldShadow = IM_COL32(72, 40, 10, 255);
		constexpr ImU32 Text = IM_COL32(255, 255, 255, 255);
		inline ImVec4 Vec(ImU32 color, float alpha = 1.0F) {
			ImVec4 out = ImGui::ColorConvertU32ToFloat4(color);
			out.w = alpha;
			return out;
		}
	} // namespace ToolTheme

	/// The tool windows and the HUD lit as if they were in the world: a light (the game's sun or moon, or the UI's own) lights the side of each
	/// panel facing it and shades the far side, catches the edges towards it, and casts the panel's shadow away from it. Only light and shadow:
	/// nothing glows, and nothing is ever dimmed past reading.
	namespace ToolLight {
		/// How strong the light and shadows are (the UI lighting setting): 0 turns them off, 1 is the usual, 2 twice as strong.
		inline float Strength = 1.0F;

		/// Where the light comes from.
		enum class Source { FollowGame, OwnLight };
		inline Source Mode = Source::FollowGame;

		/// The UI's own light, used instead of the game's when Mode is OwnLight. The game's time of day and weather don't touch it.
		inline float OwnAngle = -35.0F; //!< Degrees from straight above; negative from the left, positive from the right.
		inline float OwnHeight = 0.6F; //!< How high it stands, 0 to 1: low gives long shadows, high short ones.
		inline float OwnColor[3] = {1.0F, 0.96F, 0.88F}; //!< Its colour.
		inline float OwnBrightness = 0.8F; //!< How strongly it shines, 0 to 1.

		/// Whether lamps, fires and blasts near a panel light it too. Off by default: it reads the lights the AI's sight uses while they may be changing.
		inline bool NearbyLights = false;

		/// The light this frame (DebugMan sets it before anything is drawn).
		struct Light {
			ImVec2 Direction = {-0.5F, -0.85F}; //!< Towards the light, unit length, y down.
			float Shine = 0.6F; //!< How strongly it shines, 0 to 1: what the shadows and the lit side follow.
			float Length = 1.0F; //!< How long the shadows are, 1 the usual.
			ImVec4 Color = {1.0F, 0.97F, 0.92F, 1.0F}; //!< The light's colour, brightest channel 1.
			float Flash = 0.0F; //!< A lightning flash, 0 to 1.
		};
		inline Light Now;

		/// How lit a point of the window is by nearby lamps and fires, 0 to 1 (set by DebugMan; null out of a game).
		inline float (*NearbyLightAt)(ImVec2 point) = nullptr;

		inline ImU32 Tint(ImVec4 color, float alpha) { return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, std::clamp(alpha, 0.0F, 1.0F))); }

		/// The shadow a box casts away from the light onto what is behind it, soft-edged, longer and darker the stronger the light. Lightning casts
		/// a hard one straight down.
		inline void Shadow(ImDrawList* drawList, ImVec2 from, ImVec2 to, float pixel) {
			float shine = std::clamp(Now.Shine, 0.0F, 1.0F);
			float darkness = std::min(Strength, 2.0F) * (0.12F + 0.3F * shine);
			if (darkness <= 0.004F) {
				return;
			}
			float length = pixel * (3.0F + 9.0F * shine) * Now.Length;
			ImVec2 offset(-Now.Direction.x * length, -Now.Direction.y * length);
			if (Now.Flash > 0.0F) {
				offset = ImVec2(offset.x * (1.0F - Now.Flash), offset.y * (1.0F - Now.Flash) + pixel * 10.0F * Now.Flash);
				darkness = std::min(1.0F, darkness + 0.35F * Now.Flash);
			}
			ImVec2 shadowFrom(from.x + offset.x, from.y + offset.y);
			ImVec2 shadowTo(to.x + offset.x, to.y + offset.y);
			// A soft edge: layers growing outward, fainter as they go, softer the weaker the light.
			constexpr int layers = 5;
			float blur = pixel * (2.0F + 4.0F * (1.0F - shine));
			for (int layer = layers - 1; layer >= 0; --layer) {
				float out = blur * static_cast<float>(layer) / static_cast<float>(layers - 1);
				drawList->AddRectFilled(ImVec2(shadowFrom.x - out, shadowFrom.y - out), ImVec2(shadowTo.x + out, shadowTo.y + out), IM_COL32(0, 0, 8, static_cast<int>(255.0F * darkness / static_cast<float>(layers))), out);
			}
		}

		/// Lights a panel's face: brighter on the side towards the light and shaded on the far side, the edges facing it catching it, a touch of its
		/// colour over the whole, nearby lamps and fires warming it (when NearbyLights is on), and lightning flaring on it.
		inline void Surface(ImDrawList* drawList, ImVec2 from, ImVec2 to, float pixel) {
			float amount = std::min(Strength, 2.0F);
			if (amount <= 0.0F) {
				return;
			}
			float shine = std::clamp(Now.Shine, 0.0F, 1.0F);
			// Each corner by how much it faces the light.
			const ImVec2 corners[4] = {{-0.7071F, -0.7071F}, {0.7071F, -0.7071F}, {0.7071F, 0.7071F}, {-0.7071F, 0.7071F}};
			ImU32 face[4];
			for (int i = 0; i < 4; ++i) {
				float facing = corners[i].x * Now.Direction.x + corners[i].y * Now.Direction.y;
				if (facing > 0.0F) {
					face[i] = Tint(Now.Color, amount * facing * (0.06F + 0.16F * shine));
				} else {
					face[i] = IM_COL32(0, 0, 10, static_cast<int>(255.0F * std::min(0.35F, amount * -facing * (0.08F + 0.2F * shine))));
				}
			}
			drawList->AddRectFilledMultiColor(from, to, face[0], face[1], face[2], face[3]);
			drawList->AddRectFilled(from, to, Tint(Now.Color, 0.06F * std::min(amount, 1.0F)));
			// The edges towards the light catch it.
			ImU32 rim = Tint(Now.Color, std::min(1.0F, amount * (0.25F + 0.55F * shine)));
			if (Now.Direction.y < -0.2F) {
				drawList->AddRectFilled(from, ImVec2(to.x, from.y + pixel), rim);
			} else if (Now.Direction.y > 0.2F) {
				drawList->AddRectFilled(ImVec2(from.x, to.y - pixel), to, rim);
			}
			if (Now.Direction.x < -0.2F) {
				drawList->AddRectFilled(from, ImVec2(from.x + pixel, to.y), rim);
			} else if (Now.Direction.x > 0.2F) {
				drawList->AddRectFilled(ImVec2(to.x - pixel, from.y), to, rim);
			}
			if (NearbyLights && NearbyLightAt) {
				if (float nearby = NearbyLightAt(ImVec2((from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F)); nearby > 0.01F) {
					drawList->AddRectFilled(from, to, IM_COL32(255, 196, 130, static_cast<int>(255.0F * std::min(0.3F, 0.3F * nearby * amount))));
				}
			}
			if (Now.Flash > 0.0F) {
				drawList->AddRectFilled(from, to, IM_COL32(220, 230, 255, static_cast<int>(255.0F * std::min(1.0F, 0.3F * Now.Flash * amount))));
			}
		}

		/// Lights every tool window open this frame and casts their shadows. Called once a frame, after the last window is made.
		inline void Windows() {
			if (Strength <= 0.0F) {
				return;
			}
			ImGuiContext& context = *GImGui;
			ImDrawList* behind = ImGui::GetBackgroundDrawList();
			for (ImGuiWindow* window: context.Windows) {
				if (!window->Active || window->Hidden || (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_Tooltip))) {
					continue;
				}
				// ImGui's own "Debug##Default" window is active all frame and only dropped at its end if nothing wrote to it: unwritten, it is a 400 x 400
				// empty window at 60, 60, and lit here it showed as a dim square on every screen.
				if (window->IsFallbackWindow && !window->WriteAccessed) {
					continue;
				}
				// A window with nothing drawn (a see-through background and no contents) has nothing to light, and would cast a shadow and show a dim square.
				if (window->DrawList->VtxBuffer.Size == 0) {
					continue;
				}
				ImVec2 from = window->Pos;
				ImVec2 to(from.x + window->Size.x, from.y + window->Size.y);
				float pixel = std::max(1.0F, std::floor(window->FontRefSize / 10.0F));
				Shadow(behind, from, to, pixel);
				// On the window's own layer, under what its child areas hold, so the lettering and controls sit in the light rather than under it.
				window->DrawList->PushClipRect(from, to, false);
				Surface(window->DrawList, from, to, pixel);
				window->DrawList->PopClipRect();
			}
		}
	} // namespace ToolLight

	/// The controls of the tool windows (the Sandbox, the settings panel), drawn in the manner of the game's own menus: square, hard-edged, a pixel at a time.
	/// Each stands in for the ImGui control of the same name and is used the same way.
	namespace ToolUI {
		/// How thick one "pixel" of the controls is drawn, in window pixels: it grows with the lettering, by whole steps.
		inline float Pixel() { return std::max(1.0F, std::floor(ImGui::GetFontSize() / 10.0F)); }

		/// A raised edge round the last control: light along the top and left, dark along the bottom and right; pressed in, the other way about.
		inline void Bevel(bool pressed) {
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 from = ImGui::GetItemRectMin();
			ImVec2 to = ImGui::GetItemRectMax();
			float pixel = Pixel();
			float alpha = ImGui::GetStyle().Alpha;
			ImU32 light = (ToolTheme::EdgeLight & 0x00FFFFFF) | (static_cast<ImU32>((pressed ? 0.0F : 150.0F) * alpha) << IM_COL32_A_SHIFT);
			ImU32 dark = (ToolTheme::EdgeDark & 0x00FFFFFF) | (static_cast<ImU32>(220.0F * alpha) << IM_COL32_A_SHIFT);
			drawList->AddRectFilled(from, ImVec2(to.x, from.y + pixel), pressed ? dark : light);
			drawList->AddRectFilled(from, ImVec2(from.x + pixel, to.y), pressed ? dark : light);
			if (!pressed) {
				drawList->AddRectFilled(ImVec2(from.x, to.y - pixel), to, dark);
				drawList->AddRectFilled(ImVec2(to.x - pixel, from.y), to, dark);
			}
		}

		inline bool Button(const char* label, const ImVec2& size = ImVec2(0.0F, 0.0F)) {
			bool pressed = ImGui::Button(label, size);
			Bevel(ImGui::IsItemActive());
			return pressed;
		}

		inline bool SmallButton(const char* label) {
			bool pressed = ImGui::SmallButton(label);
			Bevel(ImGui::IsItemActive());
			return pressed;
		}

		inline bool AtNormal(float value, float normal) { return std::abs(value - normal) <= 0.0001F * std::max(1.0F, std::abs(normal)); }
		inline bool AtNormal(int value, int normal) { return value == normal; }
		inline bool AtNormal(bool value, bool normal) { return value == normal; }

		/// A small Reset beside the slider just made, putting that one value back to normal (greyed out while it is).
		/// The slider stays the last item, so a tooltip after it is still the slider's own.
		template <typename T> bool ResetButton(const char* label, T* value, T normal, const char* tip = "Puts this one setting back to the value the game comes with.") {
			const ImGuiLastItemData slider = GImGui->LastItemData;
			ImGui::SameLine();
			ImGui::PushID(label);
			ImGui::PushID("Reset");
			ImGui::BeginDisabled(AtNormal(*value, normal));
			bool reset = SmallButton("Reset");
			ImGui::SetItemTooltip("%s", tip);
			ImGui::EndDisabled();
			ImGui::PopID();
			ImGui::PopID();
			GImGui->LastItemData = slider;
			if (reset) {
				*value = normal;
			}
			return reset;
		}

		/// The shared body of the tick-box and the radio button: a square socket with the label beside it. Returns whether it was clicked.
		inline bool Socket(const char* label, bool filled, bool diamond) {
			const ImGuiStyle& style = ImGui::GetStyle();
			float side = ImGui::GetFrameHeight();
			ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
			ImVec2 at = ImGui::GetCursorScreenPos();
			ImVec2 size(side + (labelSize.x > 0.0F ? style.ItemInnerSpacing.x + labelSize.x : 0.0F), std::max(side, labelSize.y + style.FramePadding.y * 2.0F));
			bool clicked = ImGui::InvisibleButton(label, size);
			bool hovered = ImGui::IsItemHovered();
			bool held = ImGui::IsItemActive();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			float pixel = Pixel();
			ImU32 edge = ImGui::GetColorU32(hovered ? ImGuiCol_SliderGrab : ImGuiCol_Border);
			ImU32 well = ImGui::GetColorU32(held ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
			ImU32 mark = ImGui::GetColorU32(ImGuiCol_CheckMark);
			float inset = std::floor(side * 0.12F);
			ImVec2 from(at.x + inset, at.y + inset);
			ImVec2 to(at.x + side - inset, at.y + side - inset);
			if (diamond) {
				// A radio button: one of several, so a different shape from a tick-box. A square stood on its corner.
				ImVec2 middle((from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F);
				float reach = (to.x - from.x) * 0.5F;
				drawList->AddQuadFilled(ImVec2(middle.x, middle.y - reach), ImVec2(middle.x + reach, middle.y), ImVec2(middle.x, middle.y + reach), ImVec2(middle.x - reach, middle.y), well);
				drawList->AddQuad(ImVec2(middle.x, middle.y - reach), ImVec2(middle.x + reach, middle.y), ImVec2(middle.x, middle.y + reach), ImVec2(middle.x - reach, middle.y), edge, pixel);
				if (filled) {
					float inner = reach - pixel * 2.5F;
					drawList->AddQuadFilled(ImVec2(middle.x, middle.y - inner), ImVec2(middle.x + inner, middle.y), ImVec2(middle.x, middle.y + inner), ImVec2(middle.x - inner, middle.y), mark);
				}
			} else {
				drawList->AddRectFilled(from, to, well);
				drawList->AddRect(from, to, edge, 0.0F, 0, pixel);
				if (filled) {
					float gap = pixel * 2.0F + 1.0F;
					drawList->AddRectFilled(ImVec2(from.x + gap, from.y + gap), ImVec2(to.x - gap, to.y - gap), mark);
				}
			}
			if (labelSize.x > 0.0F) {
				drawList->AddText(ImVec2(at.x + side + style.ItemInnerSpacing.x, at.y + (size.y - labelSize.y) * 0.5F), ImGui::GetColorU32(ImGuiCol_Text), label, ImGui::FindRenderedTextEnd(label));
			}
			return clicked;
		}

		inline bool Checkbox(const char* label, bool* value) {
			bool clicked = Socket(label, *value, false);
			if (clicked) {
				*value = !*value;
			}
			return clicked;
		}

		inline bool RadioButton(const char* label, bool chosen) { return Socket(label, chosen, true); }

		inline bool RadioButton(const char* label, int* value, int choice) {
			bool clicked = Socket(label, *value == choice, true);
			if (clicked) {
				*value = choice;
			}
			return clicked;
		}
	} // namespace ToolUI
} // namespace RTE
