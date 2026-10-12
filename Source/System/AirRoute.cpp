#include "AirRoute.h"

#include "ADoor.h"
#include "Actor.h"
#include "Constants.h"
#include "Material.h"
#include "MovableMan.h"
#include "SceneMan.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <queue>
#include <vector>

using namespace RTE;

bool AirRoute::LineClear(const AirBody& body, const Vector& from, const Vector& to) {
	// Its whole outline swept along the line, not just its middle: rays from its two ends, top and bottom, and its middle. (A ship's
	// underside's rays run half its ground gap under the hull, not more, Under: a ship sitting at its clearance over the ground, or sent to a
	// place there, must not find the ground in the way of every line it could fly.)
	const Vector way = g_SceneMan.ShortestDistance(from, to, g_SceneMan.SceneWrapsX());
	const float halfWidth = body.HalfWidth;
	const float top = body.Top + 4.0F;
	const float under = body.Under;
	const std::array<Vector, 7> outline{Vector(0.0F, 0.0F), Vector(-halfWidth, -top), Vector(halfWidth, -top), Vector(-halfWidth, under), Vector(halfWidth, under), Vector(0.0F, -top), Vector(0.0F, under)};
	Vector blocked;
	for (const Vector& offset: outline) {
		Vector start = from + offset;
		g_SceneMan.WrapPosition(start);
		if (g_SceneMan.CastStrengthRay(start, way, 5.0F, blocked, 4)) {
			return false;
		}
	}
	return true;
}

bool AirRoute::Plan(const AirBody& body, const Vector& from, const Vector& to, float care, std::deque<Vector>& route, bool& reached) {
	route.clear();
	reached = false;
	const bool wraps = g_SceneMan.SceneWrapsX();
	const float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	const float sceneHeight = static_cast<float>(g_SceneMan.GetSceneHeight());
	const Vector way = g_SceneMan.ShortestDistance(from, to, wraps);

	// The part of the scene it plans in: round the two places, wider than the way between them by enough to go round what is in it, from as
	// high as it may fly (past the top of the map by its room there) to a little under the lower of them. Worked in a frame from its corner,
	// so a wrapping scene's seam is nothing special.
	constexpr float c_Cell = 16.0F; // A step of the route grid.
	constexpr int c_Sample = 4; // How finely the ground is looked at, in pixels: fine enough for a bunker's walls.
	constexpr float c_Margin = 480.0F;
	const float halfWidth = body.HalfWidth;
	float left = std::min(0.0F, way.m_X) - c_Margin - halfWidth;
	float right = std::max(0.0F, way.m_X) + c_Margin + halfWidth;
	if (wraps) {
		const float extra = std::max(0.0F, (right - left) - sceneWidth) * 0.5F;
		left += extra;
		right -= extra;
	} else {
		const float grace = body.OrbitGrace;
		left = std::max(left, -grace - from.m_X);
		right = std::min(right, sceneWidth + grace - from.m_X);
	}
	const float top = std::max(body.SkyTop - c_Cell, std::min(from.m_Y, to.m_Y) - 640.0F);
	const float bottom = std::min(sceneHeight, std::max(from.m_Y, to.m_Y) + 320.0F);
	const int columns = static_cast<int>((right - left) / c_Cell);
	const int rows = static_cast<int>((bottom - top) / c_Cell);
	if (columns < 2 || rows < 2) {
		return false;
	}
	// (The world place of a point of the frame, x from its own place along it.)
	auto toWorld = [&](float x, float y) {
		Vector point(from.m_X + x, y);
		g_SceneMan.WrapPosition(point);
		return point;
	};

	// What is solid, a few pixels at a time: the terrain (its buildings in it) as a ray of its hull would find it (CastStrengthRay's
	// strength), and the doors, which are not terrain. Summed up so how much is solid in any box of it is a few look-ups.
	const int sampleColumns = static_cast<int>((right - left) / c_Sample) + 1;
	const int sampleRows = static_cast<int>((bottom - top) / c_Sample) + 1;
	std::array<bool, c_PaletteEntriesNumber> solidMaterial{};
	for (int id = 0; id < c_PaletteEntriesNumber; ++id) {
		const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
		solidMaterial[id] = id != MaterialColorKeys::g_MaterialAir && material && material->GetIntegrity() >= 5.0F;
	}
	std::vector<uint8_t> solid(static_cast<size_t>(sampleColumns) * sampleRows, 0);
	for (int sx = 0; sx < sampleColumns; ++sx) {
		const float localX = left + static_cast<float>(sx * c_Sample);
		for (int sy = 0; sy < sampleRows; ++sy) {
			const float y = top + static_cast<float>(sy * c_Sample);
			// (Under the scene is solid; over it, open air.)
			if (y >= sceneHeight) {
				solid[static_cast<size_t>(sy) * sampleColumns + sx] = 1;
				continue;
			}
			const Vector point = toWorld(localX, y);
			if (!wraps && (point.m_X < 0.0F || point.m_X >= sceneWidth)) {
				continue;
			}
			solid[static_cast<size_t>(sy) * sampleColumns + sx] = solidMaterial[g_SceneMan.GetTerrMatter(point.GetFloorIntX(), point.GetFloorIntY())] ? 1 : 0;
		}
	}
	for (const ADoor* door: g_MovableMan.GetDoorList()) {
		if (!door || door->IsSetToDelete()) {
			continue;
		}
		const Vector at = g_SceneMan.ShortestDistance(from, door->GetPos(), wraps);
		const float reach = door->GetRadius() * 0.7F;
		const int x0 = std::max(0, static_cast<int>((at.m_X - reach - left) / c_Sample));
		const int x1 = std::min(sampleColumns - 1, static_cast<int>((at.m_X + reach - left) / c_Sample));
		const int y0 = std::max(0, static_cast<int>((door->GetPos().m_Y - reach - top) / c_Sample));
		const int y1 = std::min(sampleRows - 1, static_cast<int>((door->GetPos().m_Y + reach - top) / c_Sample));
		for (int sy = y0; sy <= y1; ++sy) {
			for (int sx = x0; sx <= x1; ++sx) {
				solid[static_cast<size_t>(sy) * sampleColumns + sx] = 1;
			}
		}
	}
	std::vector<int> summed(static_cast<size_t>(sampleColumns + 1) * (sampleRows + 1), 0);
	const int summedColumns = sampleColumns + 1;
	for (int sy = 0; sy < sampleRows; ++sy) {
		int rowSum = 0;
		for (int sx = 0; sx < sampleColumns; ++sx) {
			rowSum += solid[static_cast<size_t>(sy) * sampleColumns + sx];
			summed[static_cast<size_t>(sy + 1) * summedColumns + sx + 1] = summed[static_cast<size_t>(sy) * summedColumns + sx + 1] + rowSum;
		}
	}
	auto anySolid = [&](float x0, float y0, float x1, float y1) {
		const int ax = std::clamp(static_cast<int>(std::floor((x0 - left) / c_Sample)), 0, sampleColumns);
		const int bx = std::clamp(static_cast<int>(std::ceil((x1 - left) / c_Sample)) + 1, 0, sampleColumns);
		const int ay = std::clamp(static_cast<int>(std::floor((y0 - top) / c_Sample)), 0, sampleRows);
		const int by = std::clamp(static_cast<int>(std::ceil((y1 - top) / c_Sample)) + 1, 0, sampleRows);
		if (ax >= bx || ay >= by) {
			return false;
		}
		return summed[static_cast<size_t>(by) * summedColumns + bx] - summed[static_cast<size_t>(ay) * summedColumns + bx] - summed[static_cast<size_t>(by) * summedColumns + ax] + summed[static_cast<size_t>(ay) * summedColumns + ax] > 0;
	};

	// The grid: a step is open where its whole body fits there (a ship's ends, engines and guns, and half its gap over the ground under it,
	// as LineClear has it).
	const float hullTop = body.Top + 2.0F;
	const float hullUnder = body.Under;
	auto centre = [&](int column, int row) { return Vector(left + (static_cast<float>(column) + 0.5F) * c_Cell, top + (static_cast<float>(row) + 0.5F) * c_Cell); };
	std::vector<uint8_t> open(static_cast<size_t>(columns) * rows, 0);
	for (int column = 0; column < columns; ++column) {
		for (int row = 0; row < rows; ++row) {
			const Vector at = centre(column, row);
			open[static_cast<size_t>(row) * columns + column] = anySolid(at.m_X - halfWidth, at.m_Y - hullTop, at.m_X + halfWidth, at.m_Y + hullUnder) ? 0 : 1;
		}
	}
	auto indexOf = [&](const Vector& local) {
		const int column = static_cast<int>(std::floor((local.m_X - left) / c_Cell));
		const int row = static_cast<int>(std::floor((local.m_Y - top) / c_Cell));
		return (column < 0 || column >= columns || row < 0 || row >= rows) ? -1 : row * columns + column;
	};
	// The open step nearest a place, a few steps round it at most (its middle may sit a little off the grid's steps).
	auto nearestOpen = [&](const Vector& local, int within) {
		const int column = static_cast<int>(std::floor((local.m_X - left) / c_Cell));
		const int row = static_cast<int>(std::floor((local.m_Y - top) / c_Cell));
		int best = -1;
		float bestDistance = FLT_MAX;
		for (int dy = -within; dy <= within; ++dy) {
			for (int dx = -within; dx <= within; ++dx) {
				const int c = column + dx;
				const int r = row + dy;
				if (c < 0 || c >= columns || r < 0 || r >= rows || !open[static_cast<size_t>(r) * columns + c]) {
					continue;
				}
				const float distance = (centre(c, r) - local).GetSqrMagnitude();
				if (distance < bestDistance) {
					bestDistance = distance;
					best = r * columns + c;
				}
			}
		}
		return best;
	};
	const Vector startLocal(0.0F, from.m_Y);
	const Vector goalLocal(way.m_X, to.m_Y);
	const int start = nearestOpen(startLocal, 4);
	if (start < 0) {
		return false;
	}
	const int goalIndex = indexOf(goalLocal) >= 0 ? nearestOpen(goalLocal, 6) : -1;

	// Enemies a cautious order keeps off, in the frame.
	std::vector<Vector> enemies;
	if (care > 0.0F) {
		for (const Actor* actor: g_MovableMan.GetActorList()) {
			if (actor && actor->GetTeam() != body.Team && actor->GetTeam() >= 0 && !actor->IsDead() && static_cast<const void*>(actor) != body.Self) {
				const Vector at = g_SceneMan.ShortestDistance(from, actor->GetPos(), wraps);
				if (at.m_X > left - 400.0F && at.m_X < right + 400.0F) {
					enemies.emplace_back(at.m_X, actor->GetPos().m_Y);
				}
			}
		}
	}
	// What a step costs: its length (the shortest way, however high that takes it), and on a cautious order more near enemies.
	auto stepCost = [&](int index, float length) {
		float scale = 1.0F;
		if (!enemies.empty()) {
			const Vector at = centre(index % columns, index / columns);
			for (const Vector& enemy: enemies) {
				const float distance = (enemy - at).GetMagnitude();
				if (distance < 360.0F) {
					scale += care * 3.0F * (1.0F - distance / 360.0F);
				}
			}
		}
		return length * scale;
	};

	// A* over the open steps, eight ways. With no way to the place, the nearest it got.
	const size_t count = static_cast<size_t>(columns) * rows;
	std::vector<float> cost(count, FLT_MAX);
	std::vector<int> cameFrom(count, -1);
	std::vector<uint8_t> done(count, 0);
	using Entry = std::pair<float, int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> frontier;
	auto guess = [&](int index) { return (centre(index % columns, index / columns) - goalLocal).GetMagnitude(); };
	cost[start] = 0.0F;
	frontier.emplace(guess(start), start);
	int nearest = start;
	float nearestGuess = guess(start);
	constexpr std::array<std::array<int, 2>, 8> c_Steps{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
	while (!frontier.empty()) {
		const int index = frontier.top().second;
		frontier.pop();
		if (done[index]) {
			continue;
		}
		done[index] = 1;
		if (const float remaining = guess(index); remaining < nearestGuess) {
			nearestGuess = remaining;
			nearest = index;
		}
		if (index == goalIndex) {
			break;
		}
		const int column = index % columns;
		const int row = index / columns;
		for (const std::array<int, 2>& step: c_Steps) {
			const int c = column + step[0];
			const int r = row + step[1];
			if (c < 0 || c >= columns || r < 0 || r >= rows) {
				continue;
			}
			const int next = r * columns + c;
			// (Not cutting a corner past something shut.)
			if (!open[next] || done[next] || (step[0] != 0 && step[1] != 0 && (!open[static_cast<size_t>(row) * columns + c] || !open[static_cast<size_t>(r) * columns + column]))) {
				continue;
			}
			const float length = (step[0] != 0 && step[1] != 0) ? c_Cell * 1.4142F : c_Cell;
			const float reachedFor = cost[index] + stepCost(next, length);
			if (reachedFor < cost[next]) {
				cost[next] = reachedFor;
				cameFrom[next] = index;
				frontier.emplace(reachedFor + guess(next), next);
			}
		}
	}
	reached = goalIndex >= 0 && done[goalIndex];
	const int end = reached ? goalIndex : nearest;
	std::vector<Vector> points;
	for (int index = end; index >= 0 && index != start; index = cameFrom[index]) {
		const Vector at = centre(index % columns, index / columns);
		points.push_back(toWorld(at.m_X, at.m_Y));
	}
	std::reverse(points.begin(), points.end());
	// The place itself last, where it can get straight to it from the step it ends on.
	if (reached && (points.empty() || LineClear(body, points.back(), to))) {
		points.push_back(to);
	}
	if (points.empty()) {
		return true;
	}

	// Cut to as few points as have open air between: from each, to the furthest on it can fly straight to (found by halving, the way being
	// near enough always clear up to some point and blocked past it).
	Vector at = from;
	for (size_t i = 0; i < points.size();) {
		size_t furthest = i;
		if (LineClear(body, at, points.back())) {
			furthest = points.size() - 1;
		} else {
			size_t low = i;
			size_t high = points.size() - 1;
			while (high - low > 1) {
				const size_t middle = (low + high) / 2;
				if (LineClear(body, at, points[middle])) {
					low = middle;
				} else {
					high = middle;
				}
			}
			furthest = low;
		}
		route.push_back(points[furthest]);
		at = points[furthest];
		i = furthest + 1;
	}
	return true;
}
