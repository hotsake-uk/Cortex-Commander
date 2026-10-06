#pragma once

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>

namespace RTE {
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
			ImU32 light = IM_COL32(255, 255, 220, static_cast<int>((pressed ? 0.0F : 46.0F) * alpha));
			ImU32 dark = IM_COL32(0, 0, 0, static_cast<int>(120.0F * alpha));
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
