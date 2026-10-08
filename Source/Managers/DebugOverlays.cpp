#include "DebugOverlays.h"

#include "DebugDraw.h"
#include "Actor.h"
#include "MovableMan.h"
#include "SettingsMan.h"

#include <cmath>
#include <cstdio>
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

void DebugOverlays::DrawCombatOverlay() {
	int which = g_SettingsMan.CombatOverlay();
	if (which == 0) {
		return;
	}
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	float perPixel = DebugDraw::ScenePixelsPerWindowPixel();
	float thick = std::max(1.0F, std::floor(1.5F / perPixel));
	auto spot = [&](const Actor* actor, const char* name) -> std::pair<bool, Vector> {
		std::string x = std::string(name) + "X";
		std::string y = std::string(name) + "Y";
		if (!actor->NumberValueExists(x) || !actor->NumberValueExists(y)) {
			return {false, Vector()};
		}
		return {true, Vector(static_cast<float>(actor->GetNumberValue(x)), static_cast<float>(actor->GetNumberValue(y)))};
	};
	auto seconds = [](const Actor* actor, const char* name) {
		char text[16] = "";
		if (actor->NumberValueExists(name)) {
			std::snprintf(text, sizeof(text), " %.1fs", actor->GetNumberValue(name) / 1000.0);
		}
		return std::string(text);
	};
	for (const Actor* actor: g_MovableMan.GetActorList()) {
		if (which == 1 && !actor->IsDebugInspected()) {
			continue;
		}
		ImVec2 at = DebugDraw::ToScreen(actor->GetPos());
		if (!InView(at, 200.0F)) {
			continue;
		}
		if (actor->NumberValueExists("AI_TargetID")) {
			if (const MovableObject* target = g_MovableMan.FindObjectByUniqueID(static_cast<long>(actor->GetNumberValue("AI_TargetID")))) {
				bool seen = actor->NumberValueExists("AI_TargetSeen") && actor->GetNumberValue("AI_TargetSeen") > 0.0;
				ImU32 color = seen ? IM_COL32(90, 230, 90, 230) : IM_COL32(150, 150, 150, 200);
				ImVec2 to = DebugDraw::ToScreen(target->GetPos());
				drawList->AddLine(at, to, color, thick);
				drawList->AddCircle(to, 6.0F, color, 12, thick);
			}
		}
		if (actor->NumberValueExists("AI_HoldRange")) {
			drawList->AddCircle(at, static_cast<float>(actor->GetNumberValue("AI_HoldRange")) / perPixel, IM_COL32(230, 230, 120, 120), 48, thick);
		}
		if (auto [has, where] = spot(actor, "AI_Cover"); has) {
			ImVec2 to = DebugDraw::ToScreen(where);
			float half = actor->GetRadius() * 0.5F / perPixel;
			drawList->AddRect(ImVec2(to.x - half, to.y - half * 2.0F), ImVec2(to.x + half, to.y), IM_COL32(80, 220, 230, 230), 0.0F, 0, thick);
			drawList->AddLine(at, to, IM_COL32(80, 220, 230, 120), thick);
			std::string why = actor->GetStringValue("AI_CoverWhy");
			drawList->AddText(ImVec2(to.x + half + 3.0F, to.y - half * 2.0F), IM_COL32(80, 220, 230, 255), ("cover" + (why.empty() ? std::string() : " (" + why + ")") + seconds(actor, "AI_CoverMs")).c_str());
		}
		if (auto [has, where] = spot(actor, "AI_Flank"); has) {
			ImVec2 to = DebugDraw::ToScreen(where);
			drawList->AddQuadFilled(ImVec2(to.x, to.y - 6.0F), ImVec2(to.x + 6.0F, to.y), ImVec2(to.x, to.y + 6.0F), ImVec2(to.x - 6.0F, to.y), IM_COL32(240, 150, 50, 220));
			drawList->AddLine(at, to, IM_COL32(240, 150, 50, 120), thick);
			drawList->AddText(ImVec2(to.x + 9.0F, to.y - ImGui::GetTextLineHeight() * 0.5F), IM_COL32(240, 150, 50, 255), ("flank" + seconds(actor, "AI_FlankMs")).c_str());
		}
		if (auto [has, where] = spot(actor, "AI_Retreat"); has) {
			ImVec2 to = DebugDraw::ToScreen(where);
			drawList->AddTriangleFilled(ImVec2(to.x, to.y + 6.0F), ImVec2(to.x - 6.0F, to.y - 5.0F), ImVec2(to.x + 6.0F, to.y - 5.0F), IM_COL32(235, 70, 60, 220));
			drawList->AddLine(at, to, IM_COL32(235, 70, 60, 120), thick);
			drawList->AddText(ImVec2(to.x + 9.0F, to.y - ImGui::GetTextLineHeight() * 0.5F), IM_COL32(235, 70, 60, 255), ("retreat" + seconds(actor, "AI_RetreatMs")).c_str());
		}
	}
}
