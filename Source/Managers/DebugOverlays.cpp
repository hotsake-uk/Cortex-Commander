#include "DebugOverlays.h"

#include "DebugDraw.h"
#include "Actor.h"
#include "MovableMan.h"
#include "PathFinder.h"
#include "Scene.h"
#include "SettingsMan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

using namespace RTE;

namespace {

	/// Whether a window position is on the game's picture, with a margin so labels of units just off its edge still show.
	bool InView(const ImVec2& at, float margin) {
		GameViewRect view = g_WindowMan.GetGameViewRect();
		return at.x > view.x - margin && at.x < view.x + view.w + margin && at.y > view.y - margin && at.y < view.y + view.h + margin;
	}

	/// The names and colours of the route's step kinds (PathStepKind's order), for the navigation overlays.
	const char* const c_KindNames[] = {"walk", "crawl", "jump", "fall", "dig", "door", "stairs", "ladder", "leap"};
	const ImU32 c_KindColors[] = {IM_COL32(80, 220, 90, 230), IM_COL32(240, 210, 60, 230), IM_COL32(120, 170, 255, 230), IM_COL32(170, 170, 170, 230), IM_COL32(200, 130, 70, 230), IM_COL32(255, 120, 200, 230), IM_COL32(220, 80, 220, 230), IM_COL32(255, 150, 40, 230), IM_COL32(140, 255, 200, 230)};

	int KindIndex(PathStepKind kind) { return std::clamp(static_cast<int>(kind), 0, static_cast<int>(std::size(c_KindNames)) - 1); }

	/// Path grid updates kept for the terrain update boxes overlay, with when they happened.
	struct TerrainUpdate {
		std::vector<Box> Areas;
		std::vector<Vector> Nodes;
		std::chrono::steady_clock::time_point At;
	};
	std::deque<TerrainUpdate> s_TerrainUpdates;
	std::mutex s_TerrainUpdatesMutex;
	constexpr double c_TerrainUpdateShownMS = 1000.0;

	/// A colour with its opacity scaled.
	ImU32 Faded(ImU32 color, float share) {
		unsigned alpha = static_cast<unsigned>(static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(share, 0.0F, 1.0F));
		return (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
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

void DebugOverlays::DrawNavNode() {
	Scene* scene = g_SceneMan.GetScene();
	if (g_SettingsMan.NavDebugOverlay() < 3 || !scene) {
		return;
	}
	const ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureMouse || !InView(io.MousePos, 0.0F)) {
		return;
	}
	Vector pointer = DebugDraw::MouseScenePosition();
	// The grid as the same unit sees it that the drawn grid is for (see Scene::GetNavDebugActor), so the two agree.
	int team = g_SettingsMan.DebugTeam();
	const Actor* searcher = scene->GetNavDebugActor();
	PathAgent agent;
	agent.StandHeight = 44.0F;
	agent.CrawlHeight = 24.0F;
	if (searcher) {
		agent = searcher->GetPathAgent();
	}
	PathFinder& pathFinder = scene->GetPathFinder(static_cast<Activity::Teams>(team));
	std::vector<PathFinder::DebugEdge> edges = pathFinder.DescribeEdgesAt(pointer, agent);

	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	float thick = std::max(1.0F, std::floor(1.5F / DebugDraw::ScenePixelsPerWindowPixel()));
	for (const PathFinder::DebugEdge& edge: edges) {
		int kind = KindIndex(edge.Kind);
		ImU32 color = edge.Flight ? IM_COL32(255, 255, 255, 230) : c_KindColors[kind];
		ImVec2 from = DebugDraw::ToScreen(edge.From);
		ImVec2 to = DebugDraw::ToScreen(edge.To);
		drawList->AddLine(from, to, color, thick);
		if (edge.Flight) {
			// The landing: a white tick.
			drawList->AddLine(ImVec2(to.x - 5.0F, to.y), ImVec2(to.x + 5.0F, to.y), color, thick * 2.0F);
		}
		char text[64];
		if (edge.Flight) {
			std::snprintf(text, sizeof(text), "flight %.1f fuel %.1fs", edge.Cost, edge.FuelMS / 1000.0F);
		} else {
			std::snprintf(text, sizeof(text), "%s %.1f", c_KindNames[kind], edge.Cost);
		}
		std::string label = text;
		if (edge.AvoidCost > 0.01F) {
			std::snprintf(text, sizeof(text), " (+%.1f failed here)", edge.AvoidCost);
			label += text;
		}
		ImVec2 middle((from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F);
		ImVec2 size = ImGui::CalcTextSize(label.c_str());
		drawList->AddRectFilled(ImVec2(middle.x - 1.0F, middle.y - 1.0F), ImVec2(middle.x + size.x + 1.0F, middle.y + size.y + 1.0F), IM_COL32(10, 12, 10, 170));
		drawList->AddText(middle, color, label.c_str());
	}

	// What the grid makes of the node, in lines of a readable width, over the pointer.
	std::vector<std::string> lines;
	lines.push_back("Team " + std::to_string(team + 1) + " grid, as " + (searcher ? searcher->GetPresetName() + " #" + std::to_string(searcher->GetUniqueID()) : std::string("a soldier's size")));
	std::string description = pathFinder.DescribeNodeAt(pointer);
	std::string line;
	size_t start = 0;
	while (start < description.size()) {
		size_t end = description.find(' ', start);
		std::string word = description.substr(start, end == std::string::npos ? std::string::npos : end - start);
		start = end == std::string::npos ? description.size() : end + 1;
		if (!line.empty() && line.size() + word.size() > 44) {
			lines.push_back(line);
			line.clear();
		}
		line += line.empty() ? word : " " + word;
	}
	if (!line.empty()) {
		lines.push_back(line);
	}
	lines.push_back(std::to_string(edges.size()) + " ways out");
	DrawLabel(drawList, ImVec2(io.MousePos.x, io.MousePos.y - 24.0F), lines, IM_COL32(150, 200, 255, 255));
}

void DebugOverlays::DrawRecentSolves() {
	Scene* scene = g_SceneMan.GetScene();
	if (!g_SettingsMan.ShowRecentSolves() || !scene) {
		return;
	}
	std::vector<PathFinder::DebugSolve> solves;
	scene->GetPathFinder(static_cast<Activity::Teams>(g_SettingsMan.DebugTeam())).GetRecentSolves(solves);
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	float thick = std::max(1.0F, std::floor(1.5F / DebugDraw::ScenePixelsPerWindowPixel()));
	char text[96];
	for (size_t s = 0; s < solves.size(); ++s) {
		const PathFinder::DebugSolve& solve = solves[s];
		// Newest brightest; the oldest of eight at a quarter.
		float share = 0.25F + 0.75F * static_cast<float>(s + 1) / static_cast<float>(solves.size());
		bool newest = s + 1 == solves.size();
		for (size_t i = 0; i + 1 < solve.Points.size(); ++i) {
			ImU32 color = Faded(c_KindColors[KindIndex(solve.Kinds[i])], share);
			ImVec2 from = DebugDraw::ToScreen(solve.Points[i]);
			ImVec2 to = DebugDraw::ToScreen(solve.Points[i + 1]);
			drawList->AddLine(from, to, color, newest ? thick * 2.0F : thick);
			drawList->AddCircleFilled(to, newest ? 2.5F : 1.5F, color);
			// The step's cost beside it, where it is more than a plain walk's (most steps are 1 to 1.4, and labelled they hid the route).
			if (solve.StepCosts[i] > 1.5F || solve.StepCosts[i] < 0.0F) {
				std::snprintf(text, sizeof(text), "%.1f", solve.StepCosts[i]);
				drawList->AddText(ImVec2((from.x + to.x) * 0.5F + 3.0F, (from.y + to.y) * 0.5F - 3.0F), Faded(IM_COL32(235, 235, 220, 255), share), text);
			}
		}
		ImVec2 goal = DebugDraw::ToScreen(solve.End);
		ImU32 statusColor;
		if (solve.Status == MicroPather::SOLVED) {
			statusColor = solve.Cut ? IM_COL32(255, 170, 60, 255) : IM_COL32(120, 230, 120, 255);
			std::snprintf(text, sizeof(text), "%s %.1f in %.1f ms", solve.Cut ? "cut short" : "solved", solve.TotalCost, solve.SolveMS);
		} else if (solve.Status == MicroPather::START_END_SAME) {
			statusColor = IM_COL32(180, 180, 180, 255);
			std::snprintf(text, sizeof(text), "already there");
		} else {
			statusColor = IM_COL32(240, 80, 70, 255);
			std::snprintf(text, sizeof(text), "no route (%.1f ms)", solve.SolveMS);
			drawList->AddLine(ImVec2(goal.x - 5.0F, goal.y - 5.0F), ImVec2(goal.x + 5.0F, goal.y + 5.0F), Faded(statusColor, share), thick);
			drawList->AddLine(ImVec2(goal.x - 5.0F, goal.y + 5.0F), ImVec2(goal.x + 5.0F, goal.y - 5.0F), Faded(statusColor, share), thick);
			drawList->AddLine(DebugDraw::ToScreen(solve.Start), goal, Faded(statusColor, share * 0.4F), thick);
		}
		drawList->AddText(ImVec2(goal.x + 7.0F, goal.y - ImGui::GetTextLineHeight()), Faded(statusColor, share), text);
	}
}

void DebugOverlays::NoteTerrainUpdate(const std::deque<Box>& areas, const std::vector<Vector>& nodes) {
	auto now = std::chrono::steady_clock::now();
	std::lock_guard<std::mutex> lock(s_TerrainUpdatesMutex);
	s_TerrainUpdates.push_back({std::vector<Box>(areas.begin(), areas.end()), nodes, now});
	// (A second's worth at most, and a bound on it for a scene being torn apart every frame.)
	while (!s_TerrainUpdates.empty() && (s_TerrainUpdates.size() > 120 || std::chrono::duration<double, std::milli>(now - s_TerrainUpdates.front().At).count() > c_TerrainUpdateShownMS)) {
		s_TerrainUpdates.pop_front();
	}
}

void DebugOverlays::DrawTerrainUpdates() {
	std::lock_guard<std::mutex> lock(s_TerrainUpdatesMutex);
	if (!g_SettingsMan.ShowTerrainUpdates()) {
		s_TerrainUpdates.clear();
		return;
	}
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	float perPixel = DebugDraw::ScenePixelsPerWindowPixel();
	auto now = std::chrono::steady_clock::now();
	for (const TerrainUpdate& update: s_TerrainUpdates) {
		float age = static_cast<float>(std::chrono::duration<double, std::milli>(now - update.At).count() / c_TerrainUpdateShownMS);
		if (age >= 1.0F) {
			continue;
		}
		float share = 1.0F - age;
		for (const Box& area: update.Areas) {
			ImVec2 corner = DebugDraw::ToScreen(area.GetCorner());
			ImVec2 farCorner(corner.x + area.GetWidth() / perPixel, corner.y + area.GetHeight() / perPixel);
			drawList->AddRect(corner, farCorner, Faded(IM_COL32(255, 150, 40, 230), share));
		}
		for (const Vector& node: update.Nodes) {
			ImVec2 at = DebugDraw::ToScreen(node);
			drawList->AddRectFilled(ImVec2(at.x - 1.5F, at.y - 1.5F), ImVec2(at.x + 1.5F, at.y + 1.5F), Faded(IM_COL32(240, 60, 50, 230), share));
		}
	}
}
