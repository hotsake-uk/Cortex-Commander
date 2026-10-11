#pragma once

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <cmath>

namespace RTE {
	/// The colours of the tool windows, taken from the game's own menu skin (Base.rte/GUIs/Skins/Menus/SkinBlue.png and its lettering): navy wells with a
	/// periwinkle edge, slate panels and the gold of the menu text. Everything the Sandbox and settings panel draw by hand takes them from here.
	namespace ToolTheme {
		constexpr ImU32 Panel = IM_COL32(59, 65, 83, 255);
		constexpr ImU32 Well = IM_COL32(12, 20, 39, 255);
		constexpr ImU32 WellHover = IM_COL32(27, 37, 58, 255);
		constexpr ImU32 WellPressed = IM_COL32(12, 12, 28, 255);
		constexpr ImU32 Edge = IM_COL32(108, 118, 168, 255);
		constexpr ImU32 EdgeLight = IM_COL32(193, 172, 198, 255);
		constexpr ImU32 EdgeDark = IM_COL32(24, 28, 55, 255);
		constexpr ImU32 Gold = IM_COL32(252, 209, 19, 255);
		constexpr ImU32 GoldShadow = IM_COL32(72, 40, 10, 255);
		constexpr ImU32 Text = IM_COL32(255, 255, 255, 255);
		inline ImVec4 Vec(ImU32 color, float alpha = 1.0F) {
			ImVec4 out = ImGui::ColorConvertU32ToFloat4(color);
			out.w = alpha;
			return out;
		}
	} // namespace ToolTheme

	/// Soft light round the tool windows and their controls, in the manner of the game's own glows: a halo fading out from an edge, or from behind lettering.
	namespace ToolGlow {
		/// How strong every glow is (the UI glow setting): 0 turns them off, 1 is the usual, 2 twice as bright.
		inline float Strength = 0.5F;

		inline ImU32 WithAlpha(ImU32 color, float alpha) {
			return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F + 0.5F) << IM_COL32_A_SHIFT);
		}

		inline float AlphaOf(ImU32 color) { return static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0F; }

		/// A slow breath between 0 and 1, for glows that pulse.
		inline float Pulse(float speed = 2.0F) { return 0.5F + 0.5F * std::sin(static_cast<float>(ImGui::GetTime()) * speed); }

		/// A halo spreading out from a box's edge as far as reach, brightest at the edge and gone at its end.
		inline void Rect(ImDrawList* drawList, ImVec2 from, ImVec2 to, ImU32 color, float reach, float strength = 1.0F) {
			float amount = strength * Strength * AlphaOf(color);
			if (amount <= 0.004F || reach < 1.0F) {
				return;
			}
			int steps = std::clamp(static_cast<int>(reach), 2, 10);
			float step = reach / static_cast<float>(steps);
			for (int i = 0; i < steps; ++i) {
				float along = (static_cast<float>(i) + 0.5F) / static_cast<float>(steps);
				float out = step * (static_cast<float>(i) + 0.5F);
				drawList->AddRect(ImVec2(from.x - out, from.y - out), ImVec2(to.x + out, to.y + out), WithAlpha(color, amount * 0.45F * (1.0F - along) * (1.0F - along)), out, 0, step);
			}
		}

		/// A light filling a box, strongest along its middle, as of something lit from inside (a bar, a chosen tile).
		inline void Fill(ImDrawList* drawList, ImVec2 from, ImVec2 to, ImU32 color, float strength = 1.0F) {
			float amount = strength * Strength * AlphaOf(color);
			if (amount <= 0.004F) {
				return;
			}
			float middle = (from.y + to.y) * 0.5F;
			ImU32 edge = WithAlpha(color, 0.0F);
			ImU32 core = WithAlpha(color, amount * 0.35F);
			drawList->AddRectFilledMultiColor(from, ImVec2(to.x, middle), edge, edge, core, core);
			drawList->AddRectFilledMultiColor(ImVec2(from.x, middle), to, core, core, edge, edge);
		}

		/// A halo behind lettering, reach window pixels out. Drawn before the lettering itself, so it sits under it.
		inline void Text(ImDrawList* drawList, ImVec2 at, ImU32 glow, const char* text, const char* textEnd = nullptr, float reach = 2.0F, float strength = 1.0F, ImFont* font = nullptr, float size = 0.0F) {
			float amount = strength * Strength * AlphaOf(glow);
			if (amount <= 0.004F) {
				return;
			}
			font = font ? font : ImGui::GetFont();
			size = size > 0.0F ? size : ImGui::GetFontSize();
			int rings = std::clamp(static_cast<int>(std::lround(reach)), 1, 4);
			static const ImVec2 directions[] = {{1.0F, 0.0F}, {-1.0F, 0.0F}, {0.0F, 1.0F}, {0.0F, -1.0F}, {0.7F, 0.7F}, {-0.7F, 0.7F}, {0.7F, -0.7F}, {-0.7F, -0.7F}};
			for (int ring = rings; ring >= 1; --ring) {
				ImU32 color = WithAlpha(glow, amount * 0.2F * (1.0F - static_cast<float>(ring - 1) / static_cast<float>(rings)));
				float out = static_cast<float>(ring) * reach / static_cast<float>(rings);
				for (const ImVec2& direction: directions) {
					drawList->AddText(font, size, ImVec2(std::round(at.x + direction.x * out), std::round(at.y + direction.y * out)), color, text, textEnd);
				}
			}
		}

		/// Lettering with its halo under it.
		inline void GlowingText(ImDrawList* drawList, ImVec2 at, ImU32 color, ImU32 glow, const char* text, const char* textEnd = nullptr, float reach = 2.0F, float strength = 1.0F, ImFont* font = nullptr, float size = 0.0F) {
			Text(drawList, at, glow, text, textEnd, reach, strength, font, size);
			font = font ? font : ImGui::GetFont();
			drawList->AddText(font, size > 0.0F ? size : ImGui::GetFontSize(), at, color, text, textEnd);
		}

		/// A halo round every tool window open this frame: the periwinkle of the skin's edges, and gold, breathing slowly, round the one in use.
		/// Drawn behind all of them, so it shows only past their edges. Called once a frame, after the last window is made.
		inline void Windows() {
			if (Strength <= 0.0F) {
				return;
			}
			ImGuiContext& context = *GImGui;
			ImGuiWindow* focused = context.NavWindow ? context.NavWindow->RootWindow : nullptr;
			float pixel = std::max(1.0F, std::floor(ImGui::GetFontSize() / 10.0F));
			ImDrawList* drawList = ImGui::GetBackgroundDrawList();
			for (ImGuiWindow* window: context.Windows) {
				if (!window->Active || window->Hidden || (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_Tooltip))) {
					continue;
				}
				ImVec2 to(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y);
				if (window->RootWindow == focused) {
					Rect(drawList, window->Pos, to, ToolTheme::Gold, pixel * 6.0F, 0.35F + 0.1F * Pulse(1.6F));
				} else {
					Rect(drawList, window->Pos, to, ToolTheme::Edge, pixel * 5.0F, 0.35F);
				}
			}
		}
	} // namespace ToolGlow

	/// The tool windows and the HUD lit by the world they sit over, as if they were in it: the sun (or the moon) lights the edges facing it and
	/// casts each panel's shadow away from it, the sky's colour of the hour tints them (gold at dawn, blue-black at night), lightning flashes on
	/// them, and a soft lamp shines where the pointer is. Out of a game, a steady light from the top left.
	namespace ToolLight {
		/// How strong the light is (the UI lighting setting): 0 turns it off, 1 is the usual, 2 twice as bright.
		inline float Strength = 1.0F;

		/// The world's light this frame, read from the scene's lighting (DebugMan sets it before anything is drawn).
		struct WorldLight {
			ImVec2 Sun = {-0.45F, -0.9F}; //!< Towards the sun or moon, unit length, y down.
			float SunStrength = 0.6F; //!< How strongly it shines, 0 to 1: what the shadows and the lit edges follow.
			ImVec4 Sky = {1.0F, 0.97F, 0.92F, 1.0F}; //!< The colour of the sky's light, brightest channel 1.
			float Brightness = 1.0F; //!< How bright the sky's light is, 0 (dead of night) to 1 (day).
			float Flash = 0.0F; //!< A lightning flash, 0 to 1.
		};
		inline WorldLight World;

		inline ImU32 SkyColor(float alpha) { return ImGui::ColorConvertFloat4ToU32(ImVec4(World.Sky.x, World.Sky.y, World.Sky.z, std::clamp(alpha, 0.0F, 1.0F))); }

		/// A round light fading from its middle to nothing at its reach, built up from rings so it is soft.
		inline void Lamp(ImDrawList* drawList, ImVec2 at, float reach, ImU32 color, float strength) {
			float amount = strength * Strength * ToolGlow::AlphaOf(color);
			if (amount <= 0.004F || reach < 2.0F) {
				return;
			}
			constexpr int rings = 10;
			for (int ring = rings; ring >= 1; --ring) {
				float along = static_cast<float>(ring) / static_cast<float>(rings);
				drawList->AddCircleFilled(at, reach * along, ToolGlow::WithAlpha(color, amount / static_cast<float>(rings)), 40);
			}
		}

		/// The shadow a box casts away from the sun onto what is behind it, soft-edged, longer and darker the stronger the sun.
		/// Lightning casts a hard one straight down.
		inline void Shadow(ImDrawList* drawList, ImVec2 from, ImVec2 to, float pixel) {
			float sun = std::clamp(World.SunStrength, 0.0F, 1.0F);
			float darkness = std::min(Strength, 2.0F) * (0.12F + 0.3F * sun);
			if (darkness <= 0.004F) {
				return;
			}
			float length = pixel * (3.0F + 9.0F * sun);
			ImVec2 offset(-World.Sun.x * length, -World.Sun.y * length);
			if (World.Flash > 0.0F) {
				offset = ImVec2(offset.x * (1.0F - World.Flash), offset.y * (1.0F - World.Flash) + pixel * 10.0F * World.Flash);
				darkness = std::min(1.0F, darkness + 0.35F * World.Flash);
			}
			ImVec2 shadowFrom(from.x + offset.x, from.y + offset.y);
			ImVec2 shadowTo(to.x + offset.x, to.y + offset.y);
			// A soft edge: layers growing outward, fainter as they go.
			constexpr int layers = 5;
			float blur = pixel * (2.0F + 4.0F * (1.0F - sun));
			for (int layer = layers - 1; layer >= 0; --layer) {
				float out = blur * static_cast<float>(layer) / static_cast<float>(layers - 1);
				drawList->AddRectFilled(ImVec2(shadowFrom.x - out, shadowFrom.y - out), ImVec2(shadowTo.x + out, shadowTo.y + out), IM_COL32(0, 0, 8, static_cast<int>(255.0F * darkness / static_cast<float>(layers))), out);
			}
		}

		/// Lights a panel's face as the world would: brighter on the side towards the sun and shaded on the far side, its edges facing the sun
		/// catching the light, the whole tinted by the sky and dimmed at night, and lightning flaring on it.
		inline void Surface(ImDrawList* drawList, ImVec2 from, ImVec2 to, float pixel) {
			float amount = std::min(Strength, 2.0F);
			if (amount <= 0.0F) {
				return;
			}
			float sun = std::clamp(World.SunStrength, 0.0F, 1.0F);
			// Each corner by how much it faces the sun: lit towards it, shaded away from it.
			const ImVec2 corners[4] = {{-0.7071F, -0.7071F}, {0.7071F, -0.7071F}, {0.7071F, 0.7071F}, {-0.7071F, 0.7071F}};
			ImU32 face[4];
			for (int i = 0; i < 4; ++i) {
				float facing = corners[i].x * World.Sun.x + corners[i].y * World.Sun.y;
				if (facing > 0.0F) {
					face[i] = SkyColor(amount * facing * (0.06F + 0.16F * sun));
				} else {
					face[i] = IM_COL32(0, 0, 10, static_cast<int>(255.0F * std::min(1.0F, amount * -facing * (0.08F + 0.2F * sun))));
				}
			}
			drawList->AddRectFilledMultiColor(from, to, face[0], face[1], face[2], face[3]);
			// Night: the panel dims, to a point, so it can still be read; the sky's colour tints it.
			float dim = std::clamp(1.0F - World.Brightness, 0.0F, 1.0F) * 0.4F * std::min(amount, 1.0F);
			if (dim > 0.004F) {
				drawList->AddRectFilled(from, to, IM_COL32(2, 4, 16, static_cast<int>(255.0F * dim)));
			}
			drawList->AddRectFilled(from, to, SkyColor(0.07F * std::min(amount, 1.0F)));
			// The edges towards the sun catch its light.
			ImU32 rim = SkyColor(std::min(1.0F, amount * (0.25F + 0.55F * sun)));
			if (World.Sun.y < -0.2F) {
				drawList->AddRectFilled(from, ImVec2(to.x, from.y + pixel), rim);
			} else if (World.Sun.y > 0.2F) {
				drawList->AddRectFilled(ImVec2(from.x, to.y - pixel), to, rim);
			}
			if (World.Sun.x < -0.2F) {
				drawList->AddRectFilled(from, ImVec2(from.x + pixel, to.y), rim);
			} else if (World.Sun.x > 0.2F) {
				drawList->AddRectFilled(ImVec2(to.x - pixel, from.y), to, rim);
			}
			if (World.Flash > 0.0F) {
				drawList->AddRectFilled(from, to, IM_COL32(220, 230, 255, static_cast<int>(255.0F * std::min(1.0F, 0.3F * World.Flash * amount))));
			}
		}

		/// Lights every tool window open this frame, and casts their shadows. Called once a frame, after the last window is made.
		inline void Windows() {
			if (Strength <= 0.0F) {
				return;
			}
			ImGuiContext& context = *GImGui;
			ImGuiWindow* hovered = context.HoveredWindow ? context.HoveredWindow->RootWindow : nullptr;
			ImVec2 mouse = ImGui::GetMousePos();
			ImDrawList* behind = ImGui::GetBackgroundDrawList();
			for (ImGuiWindow* window: context.Windows) {
				if (!window->Active || window->Hidden || (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_Tooltip))) {
					continue;
				}
				ImVec2 from = window->Pos;
				ImVec2 to(from.x + window->Size.x, from.y + window->Size.y);
				float pixel = std::max(1.0F, std::floor(window->FontRefSize / 10.0F));
				Shadow(behind, from, to, pixel);
				// On the window's own layer, under what its child areas hold, so the lettering and controls sit in the light rather than under it.
				ImDrawList* drawList = window->DrawList;
				drawList->PushClipRect(from, to, false);
				Surface(drawList, from, to, pixel);
				if (window == hovered && mouse.x >= from.x && mouse.x < to.x && mouse.y >= from.y && mouse.y < to.y) {
					Lamp(drawList, mouse, std::max(window->FontRefSize, 10.0F) * 12.0F, IM_COL32(255, 236, 190, 255), 0.12F);
				}
				drawList->PopClipRect();
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
			// Lit from inside while pressed, and a gold halo while pointed at.
			if (pressed) {
				ToolGlow::Fill(drawList, from, to, ToolTheme::Gold, 0.5F * alpha);
			} else if (ImGui::IsItemHovered()) {
				// Pointed at, it catches the light: its face brightens from the top and its top edge shines.
				ImU32 lit = ToolLight::SkyColor(0.2F * std::min(ToolLight::Strength, 2.0F) * alpha);
				ImU32 unlit = ToolLight::SkyColor(0.0F);
				drawList->AddRectFilledMultiColor(from, to, lit, lit, unlit, unlit);
				drawList->AddRectFilled(from, ImVec2(to.x, from.y + pixel), ToolGlow::WithAlpha(ToolTheme::EdgeLight, std::min(1.0F, 0.9F * alpha)));
				ToolGlow::Rect(drawList, from, to, ToolTheme::Gold, pixel * 3.0F, 0.35F * alpha);
			}
		}

		/// The glow of a picture tile (the Sandbox's tools, metals and materials): the chosen one lit gold from inside and haloed, the one pointed at haloed faintly.
		inline void TileGlow(ImDrawList* drawList, ImVec2 from, ImVec2 to, bool selected, bool hovered) {
			if (selected) {
				ToolGlow::Fill(drawList, from, to, ToolTheme::Gold, 0.35F);
				ToolGlow::Rect(drawList, from, to, ToolTheme::Gold, Pixel() * 5.0F, 0.5F + 0.2F * ToolGlow::Pulse(1.6F));
			} else if (hovered) {
				ToolGlow::Rect(drawList, from, to, ToolTheme::Gold, Pixel() * 4.0F, 0.45F);
			}
		}

		/// A halo round the last control while it is pointed at or held, for the controls that are ImGui's own (sliders, lists, fields).
		inline void HoverGlow() {
			if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
				ToolGlow::Rect(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ToolTheme::Gold, Pixel() * 3.0F, ImGui::IsItemActive() ? 0.7F : 0.4F);
			}
		}

		/// A section heading in the gold of the menu lettering, glowing, with the rule after it glowing faintly. Stands in for ImGui::SeparatorText.
		inline void GlowHeading(const char* text) {
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			// The glow goes on a layer under the heading ImGui draws (a splitter of its own, so it works inside tables and columns too).
			ImDrawListSplitter splitter;
			splitter.Split(drawList, 2);
			splitter.SetCurrentChannel(drawList, 1);
			ImGui::SeparatorText(text);
			splitter.SetCurrentChannel(drawList, 0);
			const ImGuiStyle& style = ImGui::GetStyle();
			ImVec2 from = ImGui::GetItemRectMin();
			ImVec2 to = ImGui::GetItemRectMax();
			ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, true);
			ImVec2 at(from.x + style.SeparatorTextPadding.x, from.y + (to.y - from.y - textSize.y) * 0.5F);
			ToolGlow::Text(drawList, at, ToolTheme::Gold, text, ImGui::FindRenderedTextEnd(text), Pixel() * 3.0F, 0.9F);
			float lineY = std::floor((from.y + to.y) * 0.5F);
			float lineFrom = at.x + textSize.x + style.ItemSpacing.x;
			if (lineFrom < to.x) {
				ToolGlow::Rect(drawList, ImVec2(lineFrom, lineY - 1.0F), ImVec2(to.x, lineY + 1.0F), ToolTheme::Gold, Pixel() * 3.0F, 0.35F);
			}
			splitter.Merge(drawList);
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
			if (hovered) {
				ToolGlow::Rect(drawList, from, to, ToolTheme::Gold, pixel * 3.0F, 0.5F + 0.25F * ToolGlow::Pulse(3.0F));
			}
			if (filled) {
				// What is ticked glows faintly, like a lit lamp.
				ToolGlow::Rect(drawList, from, to, mark, pixel * 2.0F, 0.35F);
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
