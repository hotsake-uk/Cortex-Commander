#include "Notices.h"
#include "SettingsMan.h"
#include "WindowMan.h"
#include "imgui/imgui.h"
#include "ToolWidgets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <vector>

using namespace RTE;

namespace {
	constexpr float c_ShownSeconds = 3.0F; //!< How long a notice is shown, the fade included.
	constexpr float c_FadeSeconds = 0.6F; //!< How long it takes to fade out, at the end of that.
	constexpr size_t c_MostShown = 4; //!< The most notices shown at once: the oldest goes when another comes.

	struct Notice {
		std::string Text;
		std::chrono::steady_clock::time_point Posted; //!< (Not ImGui's time: notices can be posted from other threads, which mustn't touch ImGui.)
	};

	std::mutex s_Mutex; //!< Guards s_Notices, which any thread may post to.
	std::deque<Notice> s_Notices;
} // namespace

void Notices::Post(const std::string& text) {
	if (text.empty() || !g_SettingsMan.ShowNotices()) {
		return;
	}
	std::lock_guard<std::mutex> lock(s_Mutex);
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	if (!s_Notices.empty() && s_Notices.back().Text == text) {
		s_Notices.back().Posted = now;
		return;
	}
	s_Notices.push_back({text, now});
	while (s_Notices.size() > c_MostShown) {
		s_Notices.pop_front();
	}
}

void Notices::Draw() {
	// What to draw, copied out so the lock isn't held while drawing: each notice's text and how faded it is (1 fully shown).
	std::vector<std::pair<std::string, float>> shown;
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		if (!g_SettingsMan.ShowNotices()) {
			s_Notices.clear();
			return;
		}
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		std::erase_if(s_Notices, [&now](const Notice& notice) { return std::chrono::duration<float>(now - notice.Posted).count() >= c_ShownSeconds; });
		for (const Notice& notice: s_Notices) {
			float age = std::chrono::duration<float>(now - notice.Posted).count();
			shown.emplace_back(notice.Text, std::clamp((c_ShownSeconds - age) / c_FadeSeconds, 0.0F, 1.0F));
		}
	}
	if (shown.empty()) {
		return;
	}
	// Over everything else, centred across the game's picture (not the whole window when tool panels are docked at the sides), a little way
	// down from its top so they clear the window's own title bars.
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	GameViewRect view = g_WindowMan.GetGameViewRect();
	float scale = std::clamp(view.h / 720.0F, 0.75F, 2.5F);
	ImVec2 padding(10.0F * scale, 4.0F * scale);
	float y = view.y + 48.0F * scale;
	for (const auto& [text, fade]: shown) {
		ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
		ImVec2 from(std::floor(view.x + (view.w - textSize.x) * 0.5F - padding.x), std::floor(y));
		ImVec2 to(from.x + textSize.x + padding.x * 2.0F, from.y + textSize.y + padding.y * 2.0F);
		int alpha = static_cast<int>(255.0F * fade);
		drawList->AddRectFilled(from, to, (ToolTheme::Well & 0x00FFFFFF) | (static_cast<ImU32>(alpha * 220 / 255) << IM_COL32_A_SHIFT), 3.0F * scale);
		drawList->AddRect(from, to, (ToolTheme::Gold & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT), 3.0F * scale, 0, std::max(1.0F, scale));
		drawList->AddText(ImVec2(from.x + padding.x, from.y + padding.y), IM_COL32(255, 255, 255, alpha), text.c_str());
		y = to.y + 4.0F * scale;
	}
}
