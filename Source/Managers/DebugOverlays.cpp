#include "DebugOverlays.h"

#include "DebugDraw.h"
#include "Actor.h"
#include "MovableMan.h"
#include "SettingsMan.h"

#include <cmath>
#include <string>
#include <vector>

using namespace RTE;

namespace {

	/// Whether a window position is on the game's picture, with a margin so labels of units just off its edge still show.
	bool InView(const ImVec2& at, float margin) {
		GameViewRect view = g_WindowMan.GetGameViewRect();
		return at.x > view.x - margin && at.x < view.x + view.w + margin && at.y > view.y - margin && at.y < view.y + view.h + margin;
	}

	/// A box of lines of text, its bottom middle at a window position, on a dark backing so it reads over any scene.
	void DrawLabel(ImDrawList* drawList, const ImVec2& bottomMiddle, const std::vector<std::string>& lines, ImU32 headColor) {
		float lineHeight = ImGui::GetTextLineHeight();
		float width = 0.0F;
		for (const std::string& line: lines) {
			width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
		}
		float pad = std::max(2.0F, lineHeight * 0.25F);
		ImVec2 topLeft(std::floor(bottomMiddle.x - width * 0.5F - pad), std::floor(bottomMiddle.y - lineHeight * static_cast<float>(lines.size()) - pad * 2.0F));
		ImVec2 bottomRight(topLeft.x + width + pad * 2.0F, std::floor(bottomMiddle.y));
		drawList->AddRectFilled(topLeft, bottomRight, IM_COL32(10, 12, 10, 190));
		drawList->AddRect(topLeft, bottomRight, IM_COL32(90, 100, 80, 220));
		for (size_t i = 0; i < lines.size(); ++i) {
			drawList->AddText(ImVec2(topLeft.x + pad, topLeft.y + pad + lineHeight * static_cast<float>(i)), i == 0 ? headColor : IM_COL32(225, 225, 210, 255), lines[i].c_str());
		}
	}
} // namespace

void DebugOverlays::DrawUnitInspector() {
	int which = g_SettingsMan.UnitInspector();
	if (which == 0) {
		return;
	}
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	std::vector<Actor::DebugStateField> fields;
	std::vector<std::string> lines;
	for (const Actor* actor: g_MovableMan.GetActorList()) {
		if (which == 1 && !actor->IsDebugInspected()) {
			continue;
		}
		// Over the head: half the unit's height above its middle.
		Vector top = actor->GetPos() - Vector(0.0F, actor->GetRadius() * 0.8F);
		ImVec2 at = DebugDraw::ToScreen(top);
		if (!InView(at, 200.0F)) {
			continue;
		}
		fields.clear();
		actor->GetDebugState(fields);
		lines.clear();
		lines.push_back(actor->GetPresetName() + " #" + std::to_string(actor->GetUniqueID()) + " team " + std::to_string(actor->GetTeam() + 1));
		// Two fields to a line where they are short, so a label stays about as wide as it is tall.
		std::string line;
		for (const Actor::DebugStateField& field: fields) {
			if (field.Name == "preset" || field.Name == "id" || field.Name == "team" || field.Name == "x" || field.Name == "y" || field.Name == "AITrace") {
				continue;
			}
			std::string part = field.Name + " " + field.Value;
			if (!line.empty() && line.size() + part.size() > 34) {
				lines.push_back(line);
				line.clear();
			}
			line += line.empty() ? part : "   " + part;
		}
		if (!line.empty()) {
			lines.push_back(line);
		}
		ImU32 head = actor->IsPlayerControlled() ? IM_COL32(120, 200, 255, 255) : actor->IsDebugInspected() ? IM_COL32(255, 210, 90, 255) : IM_COL32(170, 220, 140, 255);
		DrawLabel(drawList, ImVec2(at.x, at.y - 4.0F), lines, head);
	}
}
