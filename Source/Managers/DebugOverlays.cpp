#include "DebugOverlays.h"

#include "DebugDraw.h"
#include "DebugMan.h"
#include "Actor.h"
#include "MovableMan.h"
#include "PostProcessMan.h"
#include "SceneLighting.h"
#include "SLTerrain.h"
#include "FluidSim.h"
#include "Material.h"
#include "SmokeGrid.h"
#include "TerrainCollapse.h"
#include "TerrainFire.h"
#include "WeatherEffects.h"
#include "PathFinder.h"
#include "Scene.h"
#include "SettingsMan.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
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
	const char* const c_KindNames[] = {"walk", "crawl", "jump", "fall", "dig", "door", "stairs", "ladder", "leap", "mantle", "crouch", "scramble", "swim", "wade", "step over"};
	const ImU32 c_KindColors[] = {IM_COL32(80, 220, 90, 230), IM_COL32(240, 210, 60, 230), IM_COL32(120, 170, 255, 230), IM_COL32(170, 170, 170, 230), IM_COL32(200, 130, 70, 230), IM_COL32(255, 120, 200, 230), IM_COL32(220, 80, 220, 230), IM_COL32(255, 150, 40, 230), IM_COL32(140, 255, 200, 230), IM_COL32(255, 230, 150, 230), IM_COL32(170, 230, 70, 230), IM_COL32(190, 140, 90, 230), IM_COL32(60, 140, 255, 230), IM_COL32(110, 200, 230, 230), IM_COL32(150, 240, 120, 230)};

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

void DebugOverlays::DrawLightSources() {
	// GetSceneLighting makes the lighting (and loads its shaders) when there is none yet, so it's only asked for while the overlay is on,
	// and once more after, to turn the recording off.
	static bool recording = false;
	bool on = g_SettingsMan.ShowLightSources();
	if (!on && !recording) {
		return;
	}
	SceneLighting* lighting = g_PostProcessMan.GetSceneLighting();
	if (lighting) {
		lighting->SetRecordDebugLights(on);
	}
	recording = on && lighting;
	if (!on || !lighting) {
		return;
	}
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	float perPixel = DebugDraw::ScenePixelsPerWindowPixel();
	// A colour as a dot: the light's own hue at full brightness, so a dim light still shows what colour it is.
	auto dotColor = [](const glm::vec3& color, int alpha) {
		float peak = std::max({color.r, color.g, color.b, 0.0001F});
		glm::vec3 shown = glm::clamp(color / peak, 0.0F, 1.0F) * 255.0F;
		return IM_COL32(static_cast<int>(shown.r), static_cast<int>(shown.g), static_cast<int>(shown.b), alpha);
	};
	for (const SceneLighting::DebugLight& light: lighting->GetDebugLights()) {
		ImVec2 at = DebugDraw::ToScreen(light.Pos);
		float reach = light.Radius / perPixel;
		if (!InView(at, reach)) {
			continue;
		}
		ImU32 edge = light.Dropped ? IM_COL32(240, 60, 50, 160) : dotColor(light.Color, light.Glow ? 90 : 150);
		if (light.ConeCos >= -1.0F) {
			// The beam: a wedge out to its reach, its half angle either side of where it points.
			float half = std::acos(std::clamp(light.ConeCos, -1.0F, 1.0F));
			float facing = std::atan2(light.Direction.y, light.Direction.x);
			drawList->PathLineTo(at);
			drawList->PathArcTo(at, reach, facing - half, facing + half, 16);
			drawList->PathStroke(edge, ImDrawFlags_Closed);
		} else if (light.Glow) {
			// (Dashed, by drawing every other segment: a glow's light is the glow's, not a light of its own.)
			const int segments = 24;
			for (int i = 0; i < segments; i += 2) {
				float a0 = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853F;
				float a1 = static_cast<float>(i + 1) / static_cast<float>(segments) * 6.2831853F;
				drawList->AddLine(ImVec2(at.x + std::cos(a0) * reach, at.y + std::sin(a0) * reach), ImVec2(at.x + std::cos(a1) * reach, at.y + std::sin(a1) * reach), edge);
			}
		} else {
			drawList->AddCircle(at, reach, edge, 32);
		}
		drawList->AddCircleFilled(at, 2.5F, dotColor(light.Color, 255));
	}
	// The scenery lamps: what each hangs on, and so whether it goes out when that is destroyed.
	if (Scene* scene = g_SceneMan.GetScene(); scene && scene->GetTerrain()) {
		for (const TerrainLight& lamp: scene->GetTerrain()->GetLights()) {
			ImVec2 at = DebugDraw::ToScreen(lamp.m_Pos);
			if (!InView(at, 20.0F)) {
				continue;
			}
			ImU32 state = lamp.m_Anchored > 0 ? IM_COL32(90, 230, 90, 255) : lamp.m_Anchored == 0 ? IM_COL32(240, 70, 60, 255) : IM_COL32(160, 160, 160, 255);
			drawList->AddRect(ImVec2(at.x - 3.0F, at.y - 3.0F), ImVec2(at.x + 3.0F, at.y + 3.0F), state);
			if (lamp.m_Anchored > 0) {
				ImVec2 anchor = DebugDraw::ToScreen(lamp.m_Pos + lamp.m_AnchorOffset);
				drawList->AddLine(at, anchor, state, 2.0F);
				drawList->AddCircleFilled(anchor, 2.0F, state);
			}
		}
	}
	// The counts, in the top left of the picture.
	const SceneLighting::DebugLightCounts& counts = lighting->GetDebugLightCounts();
	char text[160];
	std::snprintf(text, sizeof(text), "lights %d  cones %d  glows %d  merged %d  over the cap %d  reach^2 %.2f Mpx", counts.Lights, counts.Cones, counts.Glows, counts.Merged, counts.Dropped, counts.ReachSquared / 1000000.0F);
	ImVec2 origin = DebugDraw::ViewOrigin();
	ImVec2 size = ImGui::CalcTextSize(text);
	drawList->AddRectFilled(ImVec2(origin.x + 4.0F, origin.y + 4.0F), ImVec2(origin.x + 10.0F + size.x, origin.y + 8.0F + size.y), IM_COL32(10, 12, 10, 190));
	drawList->AddText(ImVec2(origin.x + 7.0F, origin.y + 6.0F), IM_COL32(235, 235, 220, 255), text);
}

void DebugOverlays::DrawSunDirection() {
	if (!g_SettingsMan.ShowSunDirection() || !g_SceneMan.GetScene()) {
		return;
	}
	SceneLighting* lighting = g_PostProcessMan.GetSceneLighting();
	if (!lighting) {
		return;
	}
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	GameViewRect view = g_WindowMan.GetGameViewRect();
	ImVec2 middle(view.x + view.w * 0.5F, view.y + view.h * 0.5F);
	glm::vec2 towards = lighting->GetSunDirection();
	float length = std::min(view.w, view.h) * 0.2F;
	ImVec2 tip(middle.x + towards.x * length, middle.y + towards.y * length);
	ImU32 color = IM_COL32(255, 220, 90, 230);
	drawList->AddLine(middle, tip, color, 3.0F);
	// The arrowhead.
	glm::vec2 side(-towards.y, towards.x);
	float head = 12.0F;
	drawList->AddTriangleFilled(tip, ImVec2(tip.x - towards.x * head + side.x * head * 0.5F, tip.y - towards.y * head + side.y * head * 0.5F), ImVec2(tip.x - towards.x * head - side.x * head * 0.5F, tip.y - towards.y * head - side.y * head * 0.5F), color);
	drawList->AddCircle(middle, 4.0F, color);
	char text[64];
	std::snprintf(text, sizeof(text), "sun shadows %.2f", lighting->GetSunShadowStrength());
	drawList->AddText(ImVec2(tip.x + 6.0F, tip.y - ImGui::GetTextLineHeight() * 0.5F), color, text);
}

void DebugOverlays::DrawWorldSim() {
	int which = g_SettingsMan.WorldSimOverlay();
	if (which == 0 || !g_SceneMan.GetScene()) {
		return;
	}
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	float perPixel = DebugDraw::ScenePixelsPerWindowPixel();
	Box view = DebugDraw::ViewBox();
	// A scene pixel as a little square, at least a window pixel big.
	float dot = std::max(1.0F, 1.0F / perPixel);
	char text[128];
	auto caption = [&](const char* line) {
		ImVec2 origin = DebugDraw::ViewOrigin();
		ImVec2 size = ImGui::CalcTextSize(line);
		float top = origin.y + 8.0F + ImGui::GetTextLineHeight() * 1.5F;
		drawList->AddRectFilled(ImVec2(origin.x + 4.0F, top - 2.0F), ImVec2(origin.x + 10.0F + size.x, top + size.y + 2.0F), IM_COL32(10, 12, 10, 190));
		drawList->AddText(ImVec2(origin.x + 7.0F, top), IM_COL32(235, 235, 220, 255), line);
	};
	switch (which) {
		case 1: {
			std::vector<Vector> pixels;
			const size_t limit = 40000;
			FluidSim::GetActivePixels(view.GetCorner(), view.GetWidth(), view.GetHeight(), pixels, limit);
			// Each material its own colour (its terrain colour, so water reads blue and lava orange), liquids filled and powders hollow, with
			// a count of each in view: the one window onto which liquids and powders (SB-1, SB-2) are moving.
			std::array<int, 256> counts{};
			std::array<ImU32, 256> colors{};
			for (int id = 1; id < 256; ++id) {
				const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
				// (Brightened a little, so dark liquids like tar and oil still show over the scene.)
				colors[id] = material && material->GetColor().GetIndex() > 0 ? IM_COL32(std::min(material->GetColor().GetR() + 50, 255), std::min(material->GetColor().GetG() + 50, 255), std::min(material->GetColor().GetB() + 50, 255), 200) : IM_COL32(70, 160, 255, 170);
			}
			for (const Vector& pixel: pixels) {
				int material = g_SceneMan.GetTerrMatter(pixel.GetFloorIntX(), pixel.GetFloorIntY()) & 0xFF;
				++counts[material];
				ImVec2 at = DebugDraw::ToScreen(pixel);
				if (FluidSim::IsLiquid(material) || dot < 3.0F) {
					drawList->AddRectFilled(at, ImVec2(at.x + dot, at.y + dot), colors[material]);
				} else {
					drawList->AddRect(at, ImVec2(at.x + dot, at.y + dot), colors[material]);
				}
			}
			std::snprintf(text, sizeof(text), "moving liquid: %d pixels, %d in view%s, %.2f ms an update", FluidSim::GetActiveCount(), static_cast<int>(pixels.size()), pixels.size() >= limit ? "+" : "", FluidSim::GetLastUpdateMS());
			caption(text);
			// The legend: each material moving in view, most first.
			std::vector<std::pair<int, int>> moving;
			for (int id = 1; id < 256; ++id) {
				if (counts[id] > 0) {
					moving.emplace_back(counts[id], id);
				}
			}
			std::sort(moving.begin(), moving.end(), std::greater<>());
			ImVec2 origin = DebugDraw::ViewOrigin();
			float line = ImGui::GetTextLineHeight();
			float top = origin.y + 8.0F + line * 3.0F;
			for (size_t i = 0; i < std::min<size_t>(moving.size(), 12); ++i) {
				const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(moving[i].second));
				std::snprintf(text, sizeof(text), "%s%s: %d", material ? material->GetPresetName().c_str() : "?", FluidSim::IsLiquid(moving[i].second) ? "" : " (powder)", moving[i].first);
				ImVec2 size = ImGui::CalcTextSize(text);
				float y = top + static_cast<float>(i) * (line + 2.0F);
				drawList->AddRectFilled(ImVec2(origin.x + 4.0F, y - 1.0F), ImVec2(origin.x + 24.0F + size.x, y + line + 1.0F), IM_COL32(10, 12, 10, 190));
				drawList->AddRectFilled(ImVec2(origin.x + 7.0F, y + 2.0F), ImVec2(origin.x + 7.0F + line - 4.0F, y + line - 2.0F), colors[moving[i].second]);
				drawList->AddText(ImVec2(origin.x + 10.0F + line, y), IM_COL32(235, 235, 220, 255), text);
			}
			break;
		}
		case 2: {
			std::vector<glm::vec3> burning;
			TerrainFire::GetBurning(glm::vec2(view.GetCorner().m_X, view.GetCorner().m_Y), static_cast<int>(view.GetWidth()), static_cast<int>(view.GetHeight()), burning);
			ImVec2 origin = DebugDraw::ToScreen(view.GetCorner());
			for (const glm::vec3& pixel: burning) {
				// Yellow while fresh, to red as it burns out.
				float heat = std::clamp(pixel.z, 0.0F, 1.0F);
				ImU32 color = IM_COL32(255, static_cast<int>(60.0F + 180.0F * heat), 40, 200);
				ImVec2 at(origin.x + pixel.x / perPixel, origin.y + pixel.y / perPixel);
				drawList->AddRectFilled(at, ImVec2(at.x + dot, at.y + dot), color);
			}
			std::snprintf(text, sizeof(text), "burning ground: %d pixels, %d in view", TerrainFire::GetCount(), static_cast<int>(burning.size()));
			caption(text);
			break;
		}
		case 3: {
			// The smoke grid's cells (16 px), sampled at their middles.
			const float cell = 16.0F;
			Vector corner(std::floor(view.GetCorner().m_X / cell) * cell, std::floor(view.GetCorner().m_Y / cell) * cell);
			int cells = 0;
			for (float y = corner.m_Y; y < view.GetCorner().m_Y + view.GetHeight(); y += cell) {
				for (float x = corner.m_X; x < view.GetCorner().m_X + view.GetWidth(); x += cell) {
					Vector middle(x + cell * 0.5F, y + cell * 0.5F);
					g_SceneMan.WrapPosition(middle);
					float density = SmokeGrid::GetDensity(middle);
					if (density <= 0.01F) {
						continue;
					}
					++cells;
					ImVec2 at = DebugDraw::ToScreen(Vector(x, y));
					ImVec2 to(at.x + cell / perPixel, at.y + cell / perPixel);
					drawList->AddRectFilled(at, to, IM_COL32(200, 200, 210, static_cast<int>(std::clamp(density / 2.5F, 0.05F, 1.0F) * 150.0F)));
					// (Thick enough on its own to hide what's behind a cell's width of it: SmokeGrid blocks sight at a summed 2.5.)
					if (density >= 2.5F) {
						drawList->AddRect(at, to, IM_COL32(255, 255, 255, 200));
					}
				}
			}
			std::snprintf(text, sizeof(text), "smoke: %d cells in view%s", cells, SmokeGrid::IsEnabled() ? "" : " (smoke doesn't block sight: switched off)");
			caption(text);
			break;
		}
		case 4: {
			std::vector<TerrainCollapse::FallingPiece> pieces;
			TerrainCollapse::GetFallingPieces(pieces);
			for (const TerrainCollapse::FallingPiece& piece: pieces) {
				ImVec2 at = DebugDraw::ToScreen(Vector(piece.X, piece.Y));
				float reach = piece.Radius / perPixel;
				if (!InView(at, reach)) {
					continue;
				}
				drawList->AddCircle(at, reach, IM_COL32(230, 180, 90, 220), 20);
				// Where it will be in a sixth of a second (ten updates) at this speed.
				drawList->AddLine(at, ImVec2(at.x + piece.VelX * 10.0F / perPixel, at.y + piece.VelY * 10.0F / perPixel), IM_COL32(255, 230, 120, 230), 2.0F);
			}
			std::snprintf(text, sizeof(text), "falling pieces: %d moving, %d pixels collapsed in this scene", TerrainCollapse::GetFallingCount(), TerrainCollapse::GetCollapsedCount());
			caption(text);
			break;
		}
		case 5: {
			GameViewRect rect = g_WindowMan.GetGameViewRect();
			ImVec2 middle(rect.x + rect.w * 0.5F, rect.y + rect.h * 0.25F);
			float wind = WeatherEffects::GetWind();
			float length = rect.w * 0.2F * wind;
			ImU32 color = IM_COL32(170, 220, 255, 230);
			drawList->AddLine(ImVec2(middle.x - length * 0.5F, middle.y), ImVec2(middle.x + length * 0.5F, middle.y), color, 3.0F);
			if (std::abs(length) > 1.0F) {
				float head = length > 0.0F ? 10.0F : -10.0F;
				ImVec2 tip(middle.x + length * 0.5F, middle.y);
				drawList->AddTriangleFilled(ImVec2(tip.x + head, tip.y), ImVec2(tip.x, tip.y - 6.0F), ImVec2(tip.x, tip.y + 6.0F), color);
			}
			std::snprintf(text, sizeof(text), "wind %+.2f   rain %.2f   snow %.2f   dust %.2f   sight x%.2f   walking x%.2f", wind, WeatherEffects::GetRain(), WeatherEffects::GetSnow(), WeatherEffects::GetDust(), WeatherEffects::GetSightMultiplier(), WeatherEffects::GetWalkSpeedMultiplier());
			caption(text);
			break;
		}
		default:
			break;
	}
}

void DebugOverlays::DrawCameraBounds() {
	if (!g_DebugMan.DrawCameraBounds() || !g_SceneMan.GetScene()) {
		return;
	}
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	float perPixel = DebugDraw::ScenePixelsPerWindowPixel();
	GameViewRect rect = g_WindowMan.GetGameViewRect();
	const ImU32 screenColors[] = {IM_COL32(120, 200, 255, 230), IM_COL32(255, 170, 80, 230), IM_COL32(150, 240, 120, 230), IM_COL32(240, 120, 240, 230)};
	// The scene's edges, where the camera stops; a wrapping side has none.
	ImU32 edgeColor = IM_COL32(255, 70, 60, 230);
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth()) / perPixel;
	float sceneHeight = static_cast<float>(g_SceneMan.GetSceneHeight()) / perPixel;
	ImVec2 sceneCorner(DebugDraw::ViewOrigin().x - g_CameraMan.GetOffset(0).m_X / perPixel, DebugDraw::ViewOrigin().y - g_CameraMan.GetOffset(0).m_Y / perPixel);
	if (!g_SceneMan.SceneWrapsX()) {
		drawList->AddLine(ImVec2(sceneCorner.x, rect.y), ImVec2(sceneCorner.x, rect.y + rect.h), edgeColor, 2.0F);
		drawList->AddLine(ImVec2(sceneCorner.x + sceneWidth, rect.y), ImVec2(sceneCorner.x + sceneWidth, rect.y + rect.h), edgeColor, 2.0F);
	}
	if (!g_SceneMan.SceneWrapsY()) {
		drawList->AddLine(ImVec2(rect.x, sceneCorner.y), ImVec2(rect.x + rect.w, sceneCorner.y), edgeColor, 2.0F);
		drawList->AddLine(ImVec2(rect.x, sceneCorner.y + sceneHeight), ImVec2(rect.x + rect.w, sceneCorner.y + sceneHeight), edgeColor, 2.0F);
	}
	char text[160];
	int screens = std::clamp(g_FrameMan.GetScreenCount(), 1, static_cast<int>(std::size(screenColors)));
	for (int screen = 0; screen < screens; ++screen) {
		ImU32 color = screenColors[screen];
		Vector offset = g_CameraMan.GetOffset(screen);
		Vector size = g_CameraMan.GetFrameSize(screen);
		// Player 1's view fills the picture, so its outline is drawn just inside the edge.
		ImVec2 topLeft = screen == 0 ? ImVec2(rect.x + 2.0F, rect.y + 2.0F) : DebugDraw::ToScreen(offset);
		ImVec2 bottomRight = screen == 0 ? ImVec2(rect.x + rect.w - 2.0F, rect.y + rect.h - 2.0F) : ImVec2(topLeft.x + size.m_X / perPixel, topLeft.y + size.m_Y / perPixel);
		drawList->AddRect(topLeft, bottomRight, color, 0.0F, 0, 2.0F);
		Vector& occlusion = g_CameraMan.GetScreenOcclusion(screen);
		// Where the camera is heading, and how far behind it the view's middle is.
		Vector target = g_CameraMan.GetScrollTarget(screen);
		ImVec2 middle = DebugDraw::ToScreen(offset + size * 0.5F);
		ImVec2 heading = DebugDraw::ToScreen(target);
		drawList->AddLine(middle, heading, color, 1.5F);
		drawList->AddLine(ImVec2(heading.x - 7.0F, heading.y), ImVec2(heading.x + 7.0F, heading.y), color, 2.0F);
		drawList->AddLine(ImVec2(heading.x, heading.y - 7.0F), ImVec2(heading.x, heading.y + 7.0F), color, 2.0F);
		drawList->AddCircle(middle, 3.0F, color);
		std::snprintf(text, sizeof(text), "screen %d (team %d)  offset %.0f,%.0f  target %.0f,%.0f  HUD covers %.0f,%.0f", screen + 1, g_CameraMan.GetScreenTeam(screen) + 1, offset.m_X, offset.m_Y, target.m_X, target.m_Y, occlusion.m_X, occlusion.m_Y);
		// Player 1's at the bottom, clear of the other overlays' captions along the top.
		ImVec2 at = screen == 0 ? ImVec2(rect.x + 6.0F, rect.y + rect.h - ImGui::GetTextLineHeight() - 6.0F) : ImVec2(topLeft.x + 4.0F, topLeft.y + 4.0F);
		ImVec2 textSize = ImGui::CalcTextSize(text);
		drawList->AddRectFilled(ImVec2(at.x - 2.0F, at.y - 1.0F), ImVec2(at.x + textSize.x + 2.0F, at.y + textSize.y + 1.0F), IM_COL32(10, 12, 10, 180));
		drawList->AddText(at, color, text);
	}
}
