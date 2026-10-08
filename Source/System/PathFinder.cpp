#include "PathFinder.h"
#include "SettingsMan.h"
#include <algorithm>
#include "PrimitiveMan.h"
#include "Color.h"
#include <chrono>

#include "ConsoleMan.h"
#include "Material.h"
#include "Scene.h"
#include "SceneMan.h"
#include "ThreadMan.h"

#include "tracy/Tracy.hpp"

#include <array>
#include <execution>
#include <mutex>
#include <set>

using namespace RTE;

// One pathfinder per thread, lazily initialized. Shouldn't access this directly, use GetPather() instead.
struct MicroPatherWrapper {
	MicroPatherWrapper() {
		m_Instance = nullptr;
	}

	~MicroPatherWrapper() {
		delete m_Instance;
	}

	MicroPather* m_Instance;
};

thread_local MicroPatherWrapper s_Pather;

// How high the given agent can jump / jetpack vertically, in metres
thread_local float s_JumpHeight = 0.0F;
thread_local double s_LastSolveMS = 0.0; //!< Debug: how long the last solve took.
thread_local bool s_LastCutAtDoor = false; //!< Whether the last route this thread solved was cut short at a door (see CalculatePath).

// How high the given agent can jump / jetpack vertically, in nodes
thread_local int s_JumpHeightVertical = 0;
thread_local int s_JumpHeightDiagonal = 0;

// What material strength the search is capable of digging through.
// Needs to be thread-local because of how it's passed around, unfortunately it doesn't seem we can give userdata for a path agent in MicroPather.
// TODO: Enhance MicroPather to add that capability (or write our own pather)!
thread_local float s_DigStrength = 0.0F;

// What door material the search can get through: dug, or shot open. Doors used to be open to everyone, so a unit with a rifle that couldn't
// scratch a blast door was routed through it, and stood at it.
thread_local float s_BreachStrength = 0.0F;

// The searcher's size: head room to stand and to crawl, and half its width.
thread_local float s_StandHeight = 40.0F;
thread_local float s_CrawlHeight = 22.0F;
thread_local float s_HalfWidth = 6.0F;
thread_local bool s_WalksStairs = false;
thread_local bool s_ClimbsLadders = false; // Whether the searcher climbs ladders (PathAgent::ClimbsLadders).
thread_local float s_MantleHeight = 0.0F; // How high a ledge the searcher mantles onto (PathAgent::MantleHeight).
thread_local Vector s_Velocity; // The searcher's velocity when it asked, in m/s (PathAgent::Velocity).
thread_local float s_JetTimeMS = 0.0F; // The searcher's full tank, in ms (PathAgent::JetTimeMS).
thread_local float s_LeapHeight = 0.0F; // How high a leap of the searcher's legs lifts it, px (PathAgent::LeapHeight).
thread_local float s_LeapSpeed = 4.0F; // How fast a leap carries it forward, m/s (PathAgent::LeapSpeed).
thread_local float s_JetClimbMSPerPx = 6.0F; // The fuel its climbs burn per pixel of height (PathAgent::JetClimbMSPerPx).
thread_local const RTE::PathNode* s_FlyingStart = nullptr; // The search's start node when the searcher is in the air with a jetpack (see AdjacentCost).
thread_local const std::vector<std::pair<Vector, Vector>>* s_AvoidLinks = nullptr; // Flights the searcher's side has failed lately (PathAgent::AvoidLinks).
// The steps of this search whose cheapest edge was a leap (see AdjacentCost): only those are labelled Leap (StepKindBetween). Labelled by
// the geometry alone, a flight link between two floors a leap also fits was called a leap, and flown as one with no fuel, if the flight
// was ever the cheaper.
thread_local std::set<std::pair<const RTE::PathNode*, const RTE::PathNode*>> s_LeapsTaken;
thread_local const std::vector<Vector>* s_Avoid = nullptr; // Where the searcher has failed jumps lately (PathAgent::Avoid). // Whether the searcher's legs take stairs (PathAgent::WalksStairs).

RTE::PathNode::PathNode(const Vector& pos) :
    Pos(pos), Anchor(pos) {
	const Material* outOfBounds = g_SceneMan.GetMaterialFromID(MaterialColorKeys::g_MaterialOutOfBounds);
	for (int i = 0; i < c_MaxAdjacentNodeCount; i++) {
		AdjacentNodes[i] = nullptr;
		AdjacentNodeBlockingMaterials[i] = outOfBounds; // Costs are infinite unless recalculated as otherwise.
	}
}

PathFinder::PathFinder(int nodeDimension) {
	Clear();
	Create(nodeDimension);
}

PathFinder::~PathFinder() {
	Destroy();
}

void PathFinder::Clear() {
	m_NodeGrid.clear();
	m_NodeDimension = SCENEGRIDSIZE;
	m_Offset = Vector();
}

int PathFinder::Create(int nodeDimension) {
	RTEAssert(g_SceneMan.GetScene(), "Scene doesn't exist or isn't loaded when creating PathFinder!");

	m_NodeDimension = nodeDimension;
	// (Here, once, on the main thread: the nodes are measured on many threads at once, and a lookup that isn't found prints.)
	{
		const Material* ladder = g_SceneMan.GetMaterial("Ladder");
		m_LadderMaterial = ladder ? static_cast<unsigned char>(ladder->GetIndex()) : 0;
	}
	int sceneWidth = g_SceneMan.GetSceneWidth();
	int sceneHeight = g_SceneMan.GetSceneHeight();

	// Make overlapping nodes at seams if necessary, to make sure all scene pixels are covered.
	m_GridWidth = std::ceil(static_cast<float>(sceneWidth) / static_cast<float>(m_NodeDimension));
	m_GridHeight = std::ceil(static_cast<float>(sceneHeight) / static_cast<float>(m_NodeDimension));

	m_WrapsX = g_SceneMan.SceneWrapsX();
	m_WrapsY = g_SceneMan.SceneWrapsY();

	m_Offset = Vector(nodeDimension * 0.5f, 0.0F);

	// Create and assign scene coordinate positions for all nodes.
	Vector nodePos = Vector(static_cast<float>(nodeDimension) / 2.0F, static_cast<float>(nodeDimension) / 2.0F) + m_Offset;
	m_NodeGrid.reserve(m_GridWidth * m_GridHeight);
	for (int y = 0; y < m_GridHeight; ++y) {
		// Make sure no cell centers are off the scene (since they can overlap the far edge of the scene).
		if (nodePos.m_Y >= sceneHeight) {
			nodePos.m_Y = sceneHeight - 1.0F;
		}

		// Start the row over at middle of the leftmost node each new row.
		nodePos.m_X = static_cast<float>(nodeDimension) / 2.0F;

		for (int x = 0; x < m_GridWidth; ++x) {
			// Make sure no cell centers are off the scene (since they can overlap the far edge of the scene).
			if (nodePos.m_X >= sceneWidth) {
				nodePos.m_X = sceneWidth - 1.0F;
			}

			// Add the newly created node to the column.
			// Warning! Emplace back must be used to ensure this is constructed in-place, as otherwise the Up/Right/Down etc references will be incorrect.
			m_NodeGrid.emplace_back(nodePos);

			nodePos.m_X += static_cast<float>(nodeDimension);
		}

		nodePos.m_Y += static_cast<float>(nodeDimension);
	}

	// Assign all the adjacent nodes on each node. GetPathNodeAtGridCoords handles Scene wrapping.
	for (int x = 0; x < m_GridWidth; ++x) {
		for (int y = 0; y < m_GridHeight; ++y) {
			PathNode& node = *GetPathNodeAtGridCoords(x, y);

			node.Up = GetPathNodeAtGridCoords(x, y - 1);
			node.Right = GetPathNodeAtGridCoords(x + 1, y);
			node.Down = GetPathNodeAtGridCoords(x, y + 1);
			node.Left = GetPathNodeAtGridCoords(x - 1, y);
			node.UpRight = GetPathNodeAtGridCoords(x + 1, y - 1);
			node.RightDown = GetPathNodeAtGridCoords(x + 1, y + 1);
			node.DownLeft = GetPathNodeAtGridCoords(x - 1, y + 1);
			node.LeftUp = GetPathNodeAtGridCoords(x - 1, y - 1);
		}
	}

	RecalculateAllCosts();

	return 0;
}

void PathFinder::Destroy() {
	Clear();
}

MicroPather* PathFinder::GetPather() {
	// TODO: cache a collection of pathers. For async pathfinding right now we create a new pather for every thread!
	if (!s_Pather.m_Instance || s_Pather.m_Instance->GetGraph() != this) {
		// First time this thread has asked for a pather, let's initialize it
		delete s_Pather.m_Instance; // Might be reinitialized and Graph ptrs mismatch, in that case delete the old one

		// TODO: test dynamically setting this. The code below sets it based on map area and block size, with a hefty upper limit.
		// int sceneArea = m_GridWidth * m_GridHeight;
		// unsigned int numberOfBlocksToAllocate = std::min(128000, sceneArea / (m_NodeDimension * m_NodeDimension));
		unsigned int numberOfBlocksToAllocate = 4000;
		s_Pather.m_Instance = new MicroPather(this, numberOfBlocksToAllocate, PathNode::c_MaxAdjacentNodeCount, false);
	}

	return s_Pather.m_Instance;
}

int PathFinder::CalculatePath(Vector start, Vector end, std::list<Vector>& pathResult, float& totalCostResult, float jumpHeight, float digStrength, float breachStrength) {
	PathAgent agent;
	agent.JumpHeight = jumpHeight;
	agent.DigStrength = digStrength;
	agent.BreachStrength = breachStrength;
	return CalculatePath(start, end, pathResult, totalCostResult, agent, nullptr);
}

int PathFinder::CalculatePath(Vector start, Vector end, std::list<Vector>& pathResult, float& totalCostResult, const PathAgent& agent, std::list<PathStepKind>* kinds) {
	ZoneScoped;

	float jumpHeight = agent.JumpHeight;
	float digStrength = agent.DigStrength;
	ApplyAgent(agent);
	s_LeapsTaken.clear();

	++m_CurrentPathingRequests;

	// Make sure start and end are within scene bounds.
	g_SceneMan.ForceBounds(start);
	g_SceneMan.ForceBounds(end);

	// Convert from absolute scene pixel coordinates to path node indices: the node whose cell the point is in. (It used to be the cell half a
	// node up, which for a point under a low ceiling was the node inside the ceiling, and the only way out of that was through it.)
	int startNodeX = std::floor(start.m_X / static_cast<float>(m_NodeDimension));
	int startNodeY = std::max(0.0F, std::floor(start.m_Y / static_cast<float>(m_NodeDimension)));
	int endNodeX = std::floor(end.m_X / static_cast<float>(m_NodeDimension));
	int endNodeY = std::max(0.0F, std::floor(end.m_Y / static_cast<float>(m_NodeDimension)));

	// Clear out the results if it happens to contain anything
	pathResult.clear();

	// Due to different actors having different dig strengths, node costs aren't consistent, so reset on every path.
	GetPather()->Reset();

	// Do the actual pathfinding, fetch out the list of states that comprise the best path.
	int result = MicroPather::NO_SOLUTION;
	std::vector<void*> statePath;

	// A node whose centre is in solid ground is no place to start or finish: the one above (the surface node) is, when it's open, or else
	// the one below. A goal a little above the ground falls in the cell over the surface's: that is the node a unit stands at, so the
	// search ends there.
	// (The open one on the point's side first: a point in the lower half of a buried cell is under it, and always taking the one above, a
	// waypoint inside the thin ceiling slab of a bunker corridor was put on the roof over it, and the unit went outside and round to get there.)
	auto openNode = [this](PathNode* node, const Vector& point) -> PathNode* {
		auto buried = [this](const PathNode* n) { return n && TerrNav(static_cast<int>(n->Pos.m_X), static_cast<int>(n->Pos.m_Y)) != MaterialColorKeys::g_MaterialAir; };
		if (node && buried(node)) {
			bool below = point.m_Y > node->Pos.m_Y;
			PathNode* first = below ? node->Down : node->Up;
			PathNode* second = below ? node->Up : node->Down;
			if (first && !buried(first)) {
				return first;
			}
			if (second && !buried(second)) {
				return second;
			}
		}
		return node;
	};
	PathNode* startNode = openNode(GetPathNodeAtGridCoords(startNodeX, startNodeY), start);
	// A searcher with a jetpack asking from the air (a re-path or a route check part way through a jump) flies on from where it is; see
	// AdjacentCost.
	s_FlyingStart = (startNode && jumpHeight < FLT_MAX && !NodeIsOnSolidGround(*startNode)) ? startNode : nullptr;
	PathNode* endNode = openNode(GetPathNodeAtGridCoords(endNodeX, endNodeY), end);
	if (endNode && !NodeIsOnSolidGround(*endNode) && endNode->Down && endNode->Down->m_Navigable && NodeIsOnSolidGround(*endNode->Down)) {
		endNode = endNode->Down;
	}
	// If end node is invalid, there's no path
	if (startNode && endNode && endNode->m_Navigable) {
		auto solveStart = std::chrono::steady_clock::now();
		result = GetPather()->Solve(static_cast<void*>(startNode), static_cast<void*>(endNode), &statePath, &totalCostResult);
		s_LastSolveMS = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - solveStart).count();
		// A route that only exists through ground the searcher can't dig is usually down to the start node: a unit pressed into a bunker wall or
		// a ledge stands in a cell whose every edge samples concrete. The neighbouring cells are tried as starts before that answer is given.
		if (result == MicroPather::SOLVED && totalCostResult > 100000.0F && digStrength <= c_PathFindingDefaultDigStrength + 1.0F) {
			const PathNode* alternatives[] = {startNode->Up, startNode->Down, startNode->Left, startNode->Right, startNode->LeftUp, startNode->UpRight, startNode->DownLeft, startNode->RightDown};
			for (const PathNode* alternative: alternatives) {
				if (!alternative || !alternative->m_Navigable || TerrNav(static_cast<int>(alternative->Pos.m_X), static_cast<int>(alternative->Pos.m_Y)) != MaterialColorKeys::g_MaterialAir) {
					continue;
				}
				std::vector<void*> otherPath;
				float otherCost = 0.0F;
				GetPather()->Reset();
				int otherResult = GetPather()->Solve(const_cast<PathNode*>(alternative), static_cast<void*>(endNode), &otherPath, &otherCost);
				if (otherResult == MicroPather::SOLVED && otherCost < totalCostResult) {
					result = otherResult;
					statePath = otherPath;
					totalCostResult = otherCost;
					if (totalCostResult <= 100000.0F) {
						break;
					}
				}
			}
		}
	}

	if (result == MicroPather::NO_SOLUTION) {
		// Otherwise micropather inits it to zero :)
		totalCostResult = std::numeric_limits<float>::max();
	}

	// A route that needs what the searcher hasn't got (digging, a door shot open) is cut short at the first such edge: the unit goes as far as
	// it can and deals with the obstacle there (shooting a door, the stuck handling), or stands down there, rather than at the start. The cost
	// stays that of the whole route, so the asker knows it was cut.
	bool cut = false;
	s_LastCutAtDoor = false;
	if (result == MicroPather::SOLVED && totalCostResult > 100000.0F && statePath.size() > 2) {
		for (size_t i = 0; i + 1 < statePath.size(); ++i) {
			std::vector<micropather::StateCost> adjacent;
			AdjacentCost(statePath[i], &adjacent);
			bool expensive = false;
			for (const micropather::StateCost& adj: adjacent) {
				if (adj.state == statePath[i + 1] && adj.cost > 100000.0F) {
					expensive = true;
				}
			}
			if (expensive) {
				// (A cut at a door is told apart: the door opens for its own side, or is shot open, and then the way through is there; a unit
				// that took it for a dead end gave up on its goal, and stood at the shot-out doorway for good.)
				const PathNode* fromNode = static_cast<const PathNode*>(statePath[i]);
				const PathNode* toNode = static_cast<const PathNode*>(statePath[i + 1]);
				const Material* blocking = StrongestMaterialAlongLine(fromNode->Pos, toNode->Pos);
				s_LastCutAtDoor = blocking && blocking->GetIndex() == MaterialColorKeys::g_MaterialDoor;
				// Up to the near side of that edge. (With the edge first, the path was kept two nodes long, so its end was the node past
				// the obstacle, and the unit was sent at the far side of a wall it couldn't pass. With nothing before it, the route is the
				// start node alone, given twice for a step to stand on.)
				if (i == 0) {
					statePath.resize(1);
					statePath.push_back(statePath.front());
				} else {
					statePath.resize(i + 1);
				}
				cut = true;
				break;
			}
		}
	}

	if (g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::Path)) {
		// Debug (the Path channel): the cost of each step along the found path, so the grid's view of the terrain can be checked against the scene.
		std::string line = "PATHLOG " + std::to_string(static_cast<int>(start.m_X)) + "," + std::to_string(static_cast<int>(start.m_Y)) + " -> " + std::to_string(static_cast<int>(end.m_X)) + "," + std::to_string(static_cast<int>(end.m_Y)) + " dig " + std::to_string(static_cast<int>(digStrength)) + " result " + std::to_string(result) + " cost " + std::to_string(totalCostResult) + " in " + std::to_string(static_cast<int>(s_LastSolveMS)) + " ms:";
		for (size_t i = 0; i + 1 < statePath.size(); ++i) {
			std::vector<micropather::StateCost> adjacent;
			AdjacentCost(statePath[i], &adjacent);
			float stepCost = -1.0F;
			for (const micropather::StateCost& adj: adjacent) {
				if (adj.state == statePath[i + 1]) {
					stepCost = adj.cost;
				}
			}
			const PathNode* node = static_cast<PathNode*>(statePath[i + 1]);
			line += " " + std::to_string(static_cast<int>(node->Pos.m_X)) + "," + std::to_string(static_cast<int>(node->Pos.m_Y)) + "=" + std::to_string(static_cast<int>(stepCost));
		}
		static std::mutex logMutex;
		std::lock_guard<std::mutex> lock(logMutex);
		g_ConsoleMan.PrintString(line);
	}

	if (g_SettingsMan.ShowRecentSolves()) {
		RecordSolve(start, end, statePath, result, totalCostResult, cut);
	}

	if (!statePath.empty()) {
		// The points of the path and what each step to them is, from the nodes they go between. The approximate first point is the exact
		// start and the last the exact end.
		struct Step {
			Vector Pos;
			PathStepKind Kind;
			Vector Centre; //!< The node's centre, where the step falls back to when its anchor puts a leg through something solid.
		};
		std::vector<Step> steps;
		steps.push_back({start, PathStepKind::Walk, start});
		float nodeSize = static_cast<float>(m_NodeDimension);
		for (size_t i = 0; i + 1 < statePath.size(); ++i) {
			const PathNode* from = static_cast<const PathNode*>(statePath[i]);
			const PathNode* to = static_cast<const PathNode*>(statePath[i + 1]);
			PathStepKind kind = StepKindBetween(from, to);
			// A jump that is up a lot and over a little is up, then over: a jet column with a landing on the ledge beside it. Flown as the
			// one straight line it was, the line went into the face of the ledge under its lip, and the unit was pressed there burning.
			// So the top of the column goes in first, a node and a half over the landing (the feet clear the lip), when it's in the open.
			float dx = g_SceneMan.ShortestDistance(from->Pos, to->Pos).m_X;
			float dy = to->Pos.m_Y - from->Pos.m_Y;
			if (kind == PathStepKind::Jump && std::abs(dx) >= 1.0F && dy <= -nodeSize * 1.5F && -dy >= std::abs(dx) * 1.5F) {
				// Where the body's centre is when its feet have just cleared the landing's floor: the feet hang about 0.45 of the standing
				// height under it and the head's top reaches 0.55 over it. Twenty pixels of clearance under the feet in the open; under a
				// ceiling, as little as keeps the head two pixels clear of it (a 44 px body in a 48 px corridor gets two under the feet).
				// (It used to sit 0.75 of a standing height and 8 px under the ceiling, which in a corridor left the feet 11 px below the
				// floor it was to step onto: the step-off found the slab in the way and the unit hovered in the hatch until the tank ran out.)
				float landingFloor = to->Surface >= 0.0F ? to->Surface : to->Pos.m_Y + nodeSize * 0.5F;
				float ceiling = landingFloor - static_cast<float>(to->FreeHeight);
				float feetClearY = landingFloor - s_StandHeight * 0.45F - 20.0F;
				float headClearY = ceiling + s_StandHeight * 0.55F + 2.0F;
				float apexY = std::max(feetClearY, headClearY);
				Vector apex(from->Anchor.m_X, apexY);
				g_SceneMan.ForceBounds(apex);
				// Only when it is above where the body stands on the landing (its centre about 0.45 of a standing height over the floor):
				// lower than that it is no top at all. (Measured against the node's centre it was skipped whenever the centre sat high in
				// its cell, and the climb was flown as the one straight line again.)
				float standingY = landingFloor - s_StandHeight * 0.45F;
				if (apexY < standingY + 1.0F && TerrNav(static_cast<int>(apex.m_X), static_cast<int>(apex.m_Y)) == MaterialColorKeys::g_MaterialAir) {
					steps.push_back({apex, PathStepKind::Jump, apex});
				}
			}
			steps.push_back({to->Anchor, kind, to->Pos});
		}
		// (Not when the route was cut short at an obstacle: then the last point is the node the unit can get to, and giving it the goal's
		// coordinates told a unit at the foot of a hatch it couldn't pass to jump 771 px to the room above.)
		if (!cut) {
			steps.back().Pos = end;
		}

		// Fewer points along a straight: a walk is one waypoint per node, and along a beam forty of them were each "arrived at" in turn,
		// with the checks that go with it. Walks that keep heading the same way on much the same level are run together, up to a few
		// nodes at a time so the movement script's look at the next waypoint still looks a sensible way ahead. Nothing else is touched:
		// a crawl, a jump, a fall, a dig and a door each want their own point.
		// Every leg between the route's points open, as the search found the legs between the nodes' centres: where an anchor (see
		// PathNode::Anchor) puts a leg through something solid (a ledge's corner between two anchors moved off their walls), the point goes back
		// to its node's centre, and the one before it too if that isn't enough. (The search knows only the centres; a route drawn through a
		// ledge's corner sent units up into the ledge's underside instead of round its lip.)
		auto legOpen = [&](const Vector& a, const Vector& b) { return Open(*StrongestMaterialAlongLine(a, b)); };
		for (size_t i = 1; i < steps.size(); ++i) {
			if (legOpen(steps[i - 1].Pos, steps[i].Pos)) {
				continue;
			}
			steps[i].Pos = steps[i].Centre;
			if (!legOpen(steps[i - 1].Pos, steps[i].Pos) && i > 1) {
				steps[i - 1].Pos = steps[i - 1].Centre;
			}
		}
		// (And the leg out of each point moved back, checked again on the next pass of the loop's own order: a point put back can open or
		// close the leg after it, which the next iteration looks at.)

		std::vector<Step> fewer;
		fewer.push_back(steps.front());
		int run = 0;
		for (size_t i = 1; i < steps.size(); ++i) {
			bool last = i + 1 == steps.size();
			if (!last && i + 1 < steps.size() && steps[i].Kind == PathStepKind::Walk && steps[i + 1].Kind == PathStepKind::Walk && run < 7) {
				float dxHere = g_SceneMan.ShortestDistance(fewer.back().Pos, steps[i].Pos).m_X;
				float dxNext = g_SceneMan.ShortestDistance(steps[i].Pos, steps[i + 1].Pos).m_X;
				float dyNext = steps[i + 1].Pos.m_Y - steps[i].Pos.m_Y;
				float dyRun = steps[i + 1].Pos.m_Y - fewer.back().Pos.m_Y;
				if (dxHere * dxNext > 0.0F && std::abs(dyNext) <= nodeSize && std::abs(dyRun) <= nodeSize) {
					++run; // This point is passed through on the way to the next.
					continue;
				}
			}
			fewer.push_back(steps[i]);
			run = 0;
		}

		for (const Step& step: fewer) {
			pathResult.push_back(step.Pos);
		}
		if (kinds) {
			kinds->clear();
			for (size_t i = 1; i < fewer.size(); ++i) {
				kinds->push_back(fewer[i].Kind);
			}
		}
	} else {
		// Empty path, give exact start and end.
		pathResult.push_back(start);
		pathResult.push_back(end);
		if (kinds) {
			kinds->clear();
			kinds->push_back(PathStepKind::Walk);
		}
	}

	--m_CurrentPathingRequests;

	// TODO: Clean up the path, remove series of nodes in the same direction etc?
	return result;
}

void PathFinder::RecordSolve(const Vector& start, const Vector& end, const std::vector<void*>& statePath, int status, float totalCost, bool cut) {
	DebugSolve solve;
	solve.Start = start;
	solve.End = end;
	solve.Status = status;
	solve.TotalCost = totalCost;
	solve.SolveMS = s_LastSolveMS;
	solve.Cut = cut;
	std::vector<micropather::StateCost> adjacent;
	for (size_t i = 0; i < statePath.size(); ++i) {
		const PathNode* node = static_cast<const PathNode*>(statePath[i]);
		solve.Points.push_back(node->Surface >= 0.0F ? Vector(node->Anchor.m_X, node->Surface - 3.0F) : node->Anchor);
		if (i + 1 < statePath.size()) {
			// (The step's cost as the search was offered it: this thread's searcher is still the one that asked.)
			adjacent.clear();
			AdjacentCost(statePath[i], &adjacent);
			float stepCost = -1.0F;
			for (const micropather::StateCost& adj: adjacent) {
				if (adj.state == statePath[i + 1] && (stepCost < 0.0F || adj.cost < stepCost)) {
					stepCost = adj.cost;
				}
			}
			solve.StepCosts.push_back(stepCost);
			solve.Kinds.push_back(StepKindBetween(node, static_cast<const PathNode*>(statePath[i + 1])));
		}
	}
	std::lock_guard<std::mutex> lock(m_RecentSolvesMutex);
	m_RecentSolves.push_back(std::move(solve));
	while (m_RecentSolves.size() > c_RecentSolvesKept) {
		m_RecentSolves.pop_front();
	}
}

void PathFinder::GetRecentSolves(std::vector<DebugSolve>& solves) const {
	std::lock_guard<std::mutex> lock(m_RecentSolvesMutex);
	solves.assign(m_RecentSolves.begin(), m_RecentSolves.end());
}

void PathFinder::ApplyAgent(const PathAgent& agent) {
	s_StandHeight = agent.StandHeight;
	s_CrawlHeight = agent.CrawlHeight;
	s_HalfWidth = agent.HalfWidth;
	s_WalksStairs = agent.WalksStairs;
	s_ClimbsLadders = agent.ClimbsLadders;
	s_MantleHeight = agent.MantleHeight;
	s_Velocity = agent.Velocity;
	s_JetTimeMS = agent.JetTimeMS;
	s_JetClimbMSPerPx = agent.JetClimbMSPerPx;
	s_LeapHeight = agent.LeapHeight;
	s_LeapSpeed = agent.LeapSpeed;
	s_Avoid = agent.Avoid.empty() ? nullptr : &agent.Avoid;
	s_AvoidLinks = agent.AvoidLinks.empty() ? nullptr : &agent.AvoidLinks;

	// Actors capable of jumping/jetpacking can jump upwards.
	float jumpHeight = agent.JumpHeight;
	s_JumpHeight = jumpHeight;

	// How high up we can jump from this node.
	if (jumpHeight == FLT_MAX) {
		// Probably quite high.
		s_JumpHeightVertical = INT_MAX;
		s_JumpHeightDiagonal = INT_MAX;
	} else {
		// Assume at least 1 so automovers work a bit better
		s_JumpHeightVertical = std::max(1, static_cast<int>(jumpHeight / (m_NodeDimension * c_MPP)));
		s_JumpHeightDiagonal = std::max(1, static_cast<int>((jumpHeight * 0.7F) / (m_NodeDimension * c_MPP)));
	}

	// Actors capable of digging can use s_DigStrength to modify the node adjacency cost.
	s_DigStrength = agent.DigStrength;
	s_BreachStrength = agent.BreachStrength < 0.0F ? agent.DigStrength : agent.BreachStrength;
}

namespace {
	/// This thread's searcher (the s_ values a search reads), kept while a debug overlay borrows the thread to look at the grid as another
	/// searcher would, and put back afterwards: the main thread also runs searches of its own (CalculatePath from Lua).
	struct SearcherState {
		float JumpHeight = s_JumpHeight;
		int JumpHeightVertical = s_JumpHeightVertical;
		int JumpHeightDiagonal = s_JumpHeightDiagonal;
		float DigStrength = s_DigStrength;
		float BreachStrength = s_BreachStrength;
		float StandHeight = s_StandHeight;
		float CrawlHeight = s_CrawlHeight;
		float HalfWidth = s_HalfWidth;
		bool WalksStairs = s_WalksStairs;
		bool ClimbsLadders = s_ClimbsLadders;
		float MantleHeight = s_MantleHeight;
		Vector Velocity = s_Velocity;
		float JetTimeMS = s_JetTimeMS;
		float LeapHeight = s_LeapHeight;
		float LeapSpeed = s_LeapSpeed;
		float JetClimbMSPerPx = s_JetClimbMSPerPx;
		const RTE::PathNode* FlyingStart = s_FlyingStart;
		const std::vector<std::pair<Vector, Vector>>* AvoidLinks = s_AvoidLinks;
		const std::vector<Vector>* Avoid = s_Avoid;

		~SearcherState() {
			s_JumpHeight = JumpHeight;
			s_JumpHeightVertical = JumpHeightVertical;
			s_JumpHeightDiagonal = JumpHeightDiagonal;
			s_DigStrength = DigStrength;
			s_BreachStrength = BreachStrength;
			s_StandHeight = StandHeight;
			s_CrawlHeight = CrawlHeight;
			s_HalfWidth = HalfWidth;
			s_WalksStairs = WalksStairs;
			s_ClimbsLadders = ClimbsLadders;
			s_MantleHeight = MantleHeight;
			s_Velocity = Velocity;
			s_JetTimeMS = JetTimeMS;
			s_LeapHeight = LeapHeight;
			s_LeapSpeed = LeapSpeed;
			s_JetClimbMSPerPx = JetClimbMSPerPx;
			s_FlyingStart = FlyingStart;
			s_AvoidLinks = AvoidLinks;
			s_Avoid = Avoid;
		}
	};
} // namespace

std::vector<PathFinder::DebugEdge> PathFinder::DescribeEdgesAt(const Vector& scenePos, const PathAgent& agent) {
	std::vector<DebugEdge> edges;
	int gridX = static_cast<int>(std::floor(scenePos.m_X / static_cast<float>(m_NodeDimension)));
	int gridY = static_cast<int>(std::floor(scenePos.m_Y / static_cast<float>(m_NodeDimension)));
	PathNode* node = GetPathNodeAtGridCoords(gridX, gridY);
	if (!node || !node->m_Navigable) {
		return edges;
	}
	SearcherState kept;
	ApplyAgent(agent);
	s_FlyingStart = nullptr;
	// The ways out as the searcher is offered them, its recent failures counted; and again without them, in the same order (the failures
	// only add to costs), so a flight link can be told from a leap to the same floor by its own cost.
	std::vector<micropather::StateCost> adjacent;
	AdjacentCost(node, &adjacent);
	std::vector<micropather::StateCost> plain;
	s_Avoid = nullptr;
	s_AvoidLinks = nullptr;
	AdjacentCost(node, &plain);
	// The flight links among them, for their fuel: the same links AdjacentCost offered (see CollectFlightLinks).
	std::vector<FlightLink> flights;
	if (s_JumpHeight < FLT_MAX && s_JetTimeMS > 0.0F && !g_SceneMan.IsPointInNoGravArea(node->Pos)) {
		CollectFlightLinks(*node, flights);
	}
	for (size_t i = 0; i < adjacent.size() && i < plain.size(); ++i) {
		const PathNode* target = static_cast<const PathNode*>(adjacent[i].state);
		if (!target || adjacent[i].cost >= 1000.0F) {
			continue;
		}
		DebugEdge edge;
		edge.From = node->Surface >= 0.0F ? Vector(node->Anchor.m_X, node->Surface - 3.0F) : node->Anchor;
		edge.To = target->Surface >= 0.0F ? Vector(target->Anchor.m_X, target->Surface - 3.0F) : target->Anchor;
		edge.Cost = adjacent[i].cost;
		edge.AvoidCost = adjacent[i].cost - plain[i].cost;
		edge.Kind = StepKindBetween(node, target);
		for (const FlightLink& flight: flights) {
			if (flight.target == target && std::abs(flight.cost - plain[i].cost) < 0.001F) {
				edge.Flight = true;
				edge.FuelMS = flight.fuel;
			}
		}
		edges.push_back(edge);
	}
	return edges;
}

std::shared_ptr<volatile PathRequest> PathFinder::CalculatePathAsync(Vector start, Vector end, float jumpHeight, float digStrength, PathCompleteCallback callback, float breachStrength) {
	PathAgent agent;
	agent.JumpHeight = jumpHeight;
	agent.DigStrength = digStrength;
	agent.BreachStrength = breachStrength;
	return CalculatePathAsync(start, end, agent, callback);
}

std::shared_ptr<volatile PathRequest> PathFinder::CalculatePathAsync(Vector start, Vector end, const PathAgent& agent, PathCompleteCallback callback) {
	std::shared_ptr<volatile PathRequest> pathRequest = std::make_shared<PathRequest>();

	const_cast<Vector&>(pathRequest->startPos) = start;
	const_cast<Vector&>(pathRequest->targetPos) = end;

	// Counted from the moment it's queued, not from when a thread picks it up: the grid's cost updates wait for the count to be zero, and
	// a request still in the queue when they ran was then solved on a grid being written under it (new requests are only queued from the
	// main thread, which is the one doing the rebuild, so with nothing queued or running the rebuild has the grid to itself).
	++m_CurrentPathingRequests;
	g_ThreadMan.GetBackgroundThreadPool().push_task(
	    [this, start, end, agent, callback](std::shared_ptr<volatile PathRequest> volRequest) {
		    // Cast away the volatile-ness - only matters outside (and complicates the API otherwise)
		    PathRequest& request = const_cast<PathRequest&>(*volRequest);

		    int status = this->CalculatePath(start, end, request.path, request.totalCost, agent, &request.kinds);

		    request.status = status;
		    request.cutAtDoor = s_LastCutAtDoor;
		    request.pathLength = request.path.size();

		    if (callback) {
			    callback(volRequest);
		    }

		    // Have to set to complete after the callback, so anything that blocks on it knows that the callback will have been called by now
		    // This has the awkward side-effect that the complete flag is actually false during the callback - but that's fine, if it's called we know it's complete anyways
		    request.complete = true;
		    --m_CurrentPathingRequests;
	    },
	    pathRequest);

	return pathRequest;
}

void PathFinder::RecalculateAllCosts() {
	RTEAssert(g_SceneMan.GetScene(), "Scene doesn't exist or isn't loaded when recalculating PathFinder!");

	// Deadlock until all path requests are complete
	while (m_CurrentPathingRequests.load() != 0) {};

	// I hate this copy, but fuck it.
	std::vector<int> pathNodesIdsVec;
	pathNodesIdsVec.reserve(m_NodeGrid.size());
	for (size_t i = 0; i < m_NodeGrid.size(); ++i) {
		pathNodesIdsVec.push_back(i);
	}

	UpdateNodeList(pathNodesIdsVec);
}

std::vector<int> PathFinder::RecalculateAreaCosts(std::deque<Box>& boxList, size_t nodeUpdateLimit) {
	ZoneScoped;

	std::unordered_set<int> nodeIDsToUpdate;

	while (!boxList.empty()) {
		std::vector<int> nodesInside = GetNodeIdsInBox(boxList.front(), true);
		for (int nodeId: nodesInside) {
			nodeIDsToUpdate.insert(nodeId);
		}

		boxList.pop_front();
		if (nodeIDsToUpdate.size() > nodeUpdateLimit) {
			break;
		}
	}

	// Note - This copy is necessary because std::for_each with parallel execution doesn't appear to work with std::unordered_set -
	// Using it will cause nodes to randomly fail to update. This should be rechecked when the codebase upgrades to C++20,
	// and then UpdateNodeList can be refactored to take a pair of iterators instead of a vector.
	std::vector<int> nodeVec(nodeIDsToUpdate.begin(), nodeIDsToUpdate.end());

	// If no PathNode costs were changed, clear the set of IDs to update, so it's empty when it's returned.
	if (!UpdateNodeList(nodeVec)) {
		nodeVec.clear();
	}

	return nodeVec;
}

float PathFinder::LeastCostEstimate(void* startState, void* endState) {
	const PathNode* startNode = static_cast<PathNode*>(startState);
	const PathNode* endNode = static_cast<PathNode*>(endState);
	// Never more than the cheapest way can cost: some edges cost a little under their length in nodes (a diagonal 1.4 for 1.414, a flight
	// straight down 5.66 for 6), and with the straight distance as it was the estimate overshot them, so the search, which never reopens a
	// node it has closed, could settle on a dearer route and report a total under the sum of its steps (which Actor::UpdateMovePath compares).
	// 0.94 is under the cheapest such ratio.
	return g_SceneMan.ShortestDistance(startNode->Pos, endNode->Pos).GetMagnitude() / m_NodeDimension * 0.94F;
}

void PathFinder::AdjacentCost(void* state, std::vector<micropather::StateCost>* adjacentList) {
	const PathNode* node = static_cast<PathNode*>(state);
	micropather::StateCost adjCost;
	size_t leapsBegin = 0;
	size_t leapsEnd = 0;

	// We do a little trick here, where we radiate out a little percentage of our average cost in all directions.
	// This encourages the AI to generally try to give hard surfaces some berth when pathing, so we don't get too close and get stuck.
	const float costRadiationMultiplier = 0.2F;
	float radiatedCost = 0.0F; // GetNodeAverageTransitionCost(*node) * costRadiationMultiplier;

	bool isInNoGrav = g_SceneMan.IsPointInNoGravArea(node->Pos);
	bool allowDiagonal = !isInNoGrav; // We don't allow diagonals in nograv to improve automover behaviour

	// From the start of a search asked in the air by a searcher with a jetpack: straight on to anything within a short flight (four nodes
	// across, three up or down) that is in plain sight. A node in the air has no way sideways or up of its own (those start from the
	// ground), so a route asked for mid-jump fell to the floor and climbed back up, and the unit turned round in the air to follow it.
	// Only from the start, so routes don't chain flights across the sky.
	if (node == s_FlyingStart) {
		int gridX = static_cast<int>(std::floor(node->Pos.m_X / static_cast<float>(m_NodeDimension)));
		int gridY = static_cast<int>(std::floor(node->Pos.m_Y / static_cast<float>(m_NodeDimension)));
		// (Six across and four up or down: at four, a roof four and a half nodes off a unit half way up its climb was out of reach, and the
		// route from there went back down to the wall and up again.)
		for (int dy = -4; dy <= 4; ++dy) {
			for (int dx = -6; dx <= 6; ++dx) {
				if ((dx == 0 && dy == 0) || (std::abs(dx) <= 1 && std::abs(dy) <= 1)) {
					continue;
				}
				PathNode* target = GetPathNodeAtGridCoords(gridX + dx, gridY + dy);
				if (!target || !target->m_Navigable || !Open(*StrongestMaterialAlongLine(node->Pos, target->Pos))) {
					continue;
				}
				// Somewhere a body fits, and if it stands there, somewhere it can stand up.
				if (!RoomToPass(*target, 2.0F) || (NodeIsOnSolidGround(*target) && static_cast<float>(target->FreeHeight) < s_StandHeight)) {
					continue;
				}
				adjCost.cost = std::sqrt(static_cast<float>(dx * dx + dy * dy)) * 1.5F + (dy < 0 ? static_cast<float>(-dy) * 0.5F : 0.0F);
				adjCost.state = static_cast<void*>(target);
				adjacentList->push_back(adjCost);
			}
		}
	}

	// (Only the steps down pay it: a step across, in the air or on the ground, is flight or a walk, not a fall. Charged on the sideways steps
	// too, a flight straight across a high room cost more than diving to its floor and climbing back up, and that is the route units took.)
	// A fall is paid for by the node: every step into a node that is still high above the ground (FallCost) costs a little more, so a
	// fall costs by its height, however it began. Falling cost nothing however far, so a route that left a bunker by an opening, dropped
	// four hundred pixels down its outside wall and came back in at the bottom beat the hatches inside; the jetpack then paid for the
	// fall in fuel, braking at the bottom, or the body did. (Charged once at the step off something standing, the search hopped up a
	// rung or two of a jump and stepped off those into the drop for nothing: a search with no memory can't tell a rung from a fall.)

	// Ladders, for a searcher that climbs them (see PathAgent::ClimbsLadders): up and down the rungs whatever its jet, off the side onto a
	// floor beside, and over the top onto a floor there; and onto the foot of one from the floor under it. Priced by the climb's pace
	// (dearer up than a walk, about a walk down), so a jet that is clearly quicker still wins for a unit that has one, and a unit with
	// too little jet for a shaft has the ladder up it. (The jump links up a shaft are for the jet; with only those, a unit whose tank
	// couldn't make the climb was told there was no way up a laddered shaft at all.)
	if (s_ClimbsLadders && s_JumpHeight < FLT_MAX) {
		auto standsAt = [&](const PathNode* n) {
			return n && n->m_Navigable && NodeIsOnSolidGround(*n) && static_cast<float>(n->FreeHeight) >= s_StandHeight;
		};
		auto link = [&](const PathNode* to, float cost) {
			adjCost.cost = cost + radiatedCost;
			adjCost.state = const_cast<PathNode*>(to);
			adjacentList->push_back(adjCost);
		};
		if (node->Ladder) {
			if (node->Up && node->Up->m_Navigable && Open(*node->UpMaterial) && (node->Up->Ladder || standsAt(node->Up))) {
				link(node->Up, 2.0F);
			}
			if (node->Down && node->Down->m_Navigable && Open(*node->DownMaterial) && (node->Down->Ladder || standsAt(node->Down))) {
				link(node->Down, 1.2F);
			}
			// (Off the side and over the top only where the way between is open: a ladder stands against a wall, and offered through it,
			// the floor on the wall's far side was a step away, and units walked into the wall behind the ladder.)
			if (standsAt(node->Left) && Open(*node->LeftMaterial)) {
				link(node->Left, 1.3F);
			}
			if (standsAt(node->Right) && Open(*node->RightMaterial)) {
				link(node->Right, 1.3F);
			}
			// Over the top: the floor beside the node above the last rung.
			if (node->Up && node->Up->m_Navigable && !node->Up->Ladder && Open(*node->UpMaterial)) {
				if (standsAt(node->Up->Left) && Open(*node->Up->LeftMaterial)) {
					link(node->Up->Left, 2.2F);
				}
				if (standsAt(node->Up->Right) && Open(*node->Up->RightMaterial)) {
					link(node->Up->Right, 2.2F);
				}
			}
		} else if (standsAt(node) && node->Up && node->Up->m_Navigable && Open(*node->UpMaterial)) {
			// Onto the foot of a ladder over this floor: the node above, or reached up to two nodes up, in this column or one either side
			// (a ladder whose foot is over the floor of the corridor under its shaft).
			const PathNode* oneUp = node->Up;
			const PathNode* twoUp = (oneUp->Up && oneUp->Up->m_Navigable && Open(*oneUp->UpMaterial)) ? oneUp->Up : nullptr;
			// (Beside, only through open air: not round the corner of a wall to a ladder on its far side.)
			const PathNode* oneLeft = Open(*oneUp->LeftMaterial) ? oneUp->Left : nullptr;
			const PathNode* oneRight = Open(*oneUp->RightMaterial) ? oneUp->Right : nullptr;
			const PathNode* twoLeft = (twoUp && Open(*twoUp->LeftMaterial)) ? twoUp->Left : nullptr;
			const PathNode* twoRight = (twoUp && Open(*twoUp->RightMaterial)) ? twoUp->Right : nullptr;
			for (const PathNode* target: std::array<const PathNode*, 6>{oneUp, oneLeft, oneRight, twoUp, twoLeft, twoRight}) {
				if (target && target->m_Navigable && target->Ladder) {
					link(target, target == oneUp ? 2.0F : (target == twoUp ? 4.5F : 3.5F));
				}
			}
		}
	}

	if (node->Down && node->Down->m_Navigable) {
		// (Down through a gap narrower than the body is no way down; down through ground is a dig, and the digger makes its own room.)
		adjCost.cost = (1.0F + GetMaterialTransitionCost(*node->DownMaterial) + radiatedCost) * ((RoomToPass(*node->Down) || !Open(*node->DownMaterial)) ? 1.0F : 1000.0F) + FallCost(*node->Down);
		adjCost.state = static_cast<void*>(node->Down);
		adjacentList->push_back(adjCost);
	}

	if (node->RightDown && node->RightDown->m_Navigable && allowDiagonal) {
		adjCost.cost = 1.4F + (GetMaterialTransitionCost(*node->RightDownMaterial) * 1.4F) + radiatedCost + FallCost(*node->RightDown);
		adjCost.state = static_cast<void*>(node->RightDown);
		adjacentList->push_back(adjCost);
	}

	if (node->DownLeft && node->DownLeft->m_Navigable && allowDiagonal) {
		adjCost.cost = 1.4F + (GetMaterialTransitionCost(*node->DownLeftMaterial) * 1.4F) + radiatedCost + FallCost(*node->DownLeft);
		adjCost.state = static_cast<void*>(node->DownLeft);
		adjacentList->push_back(adjCost);
	}

	if (isInNoGrav || NodeIsOnSolidGround(*node)) {
		// Cost to discourage us from going up. At 3 a hill was worth a long walk round, which is what units did; at half that they go over.
		const float extraUpCost = 1.5F;

		// We can only go straight left or right if we're on solid ground, otherwise we need to go downwards. The head room along the way says
		// whether it's a walk, a crawl (slower), or no way through at all for this searcher.
		// (The room only matters where the way is open: through ground, a digger makes its own.)
		// (A sideways step into the air over a drop is the start of a fall too, and pays like the rest of it.)
		if (node->Left && node->Left->m_Navigable) {
			adjCost.cost = (1.0F + GetMaterialTransitionCost(*node->LeftMaterial) + radiatedCost) * (Open(*node->LeftMaterial) ? HeadRoomFactor(*node, *node->Left) : 1.0F);
			adjCost.state = static_cast<void*>(node->Left);
			adjacentList->push_back(adjCost);
		}

		if (node->Right && node->Right->m_Navigable) {
			adjCost.cost = (1.0F + GetMaterialTransitionCost(*node->RightMaterial) + radiatedCost) * (Open(*node->RightMaterial) ? HeadRoomFactor(*node, *node->Right) : 1.0F);
			adjCost.state = static_cast<void*>(node->Right);
			adjacentList->push_back(adjCost);
		}

		// Stepping over something low on the floor, to the floor level with this one or two nodes along (see UpdateNodeCosts): stepped or vaulted
		// over when it is within 0.6 of the searcher's standing height and there is room to stand over it, crawled over when there is only room
		// to crawl. Priced as the walk plus the effort, so a route takes it over going round, as a person would.
		if (s_JumpHeight < FLT_MAX && node->Surface >= 0.0F) {
			// (Known to this end or the other: see PathNode::StepOverRiseLeft.)
			auto stepOver = [&](bool leftward, int k, const PathNode* to) {
				if (!to || !to->m_Navigable) {
					return;
				}
				float riseHere = leftward ? node->StepOverRiseLeft[k] : node->StepOverRise[k];
				float riseThere = leftward ? to->StepOverRise[k] : to->StepOverRiseLeft[k];
				if (riseHere <= 0.0F && riseThere <= 0.0F) {
					return;
				}
				float rise = std::max(riseHere, riseThere);
				float room = static_cast<float>(riseHere >= riseThere ? (leftward ? node->StepOverRoomLeft[k] : node->StepOverRoom[k]) : (leftward ? to->StepOverRoom[k] : to->StepOverRoomLeft[k]));
				// (Up to 0.6 of the standing height: a soldier gets over a 24 px block in a 48 px tunnel, and at half, 22 px, the step was refused one
				// way round, where nothing else on the grid stood in for it, and the unit went 400 px round.)
				if (rise > s_StandHeight * 0.6F || room < s_CrawlHeight) {
					return;
				}
				float walk = static_cast<float>(k + 1);
				float effort = rise / static_cast<float>(m_NodeDimension);
				float crawl = room < s_StandHeight ? walk * 2.0F : 0.0F;
				adjCost.cost = walk + 0.5F + effort + crawl + radiatedCost;
				adjCost.state = const_cast<PathNode*>(to);
				adjacentList->push_back(adjCost);
			};
			stepOver(false, 0, node->Right);
			stepOver(false, 1, node->Right ? node->Right->Right : nullptr);
			stepOver(true, 0, node->Left);
			stepOver(true, 1, node->Left ? node->Left->Left : nullptr);
		}

		// Mantles: up onto a ledge one or two nodes up and one across, when its top is within the searcher's reach (it pulls itself up and over;
		// see Actor::TryStartMantle). Priced as a short walk plus the lift, so a route takes the ledge a person would simply climb onto rather
		// than a jet hop that needs the height just right. Room is wanted to stand on top and to rise in place first.
		if (s_MantleHeight > 0.0F && s_JumpHeight < FLT_MAX && node->Surface >= 0.0F && node->Up && node->Up->m_Navigable && Open(*node->UpMaterial)) {
			const PathNode* oneUp = node->Up;
			const PathNode* twoUp = (oneUp->Up && oneUp->Up->m_Navigable && Open(*oneUp->UpMaterial)) ? oneUp->Up : nullptr;
			for (const PathNode* target: {oneUp->Left, oneUp->Right, twoUp ? twoUp->Left : nullptr, twoUp ? twoUp->Right : nullptr}) {
				if (!target || !target->m_Navigable || target->Surface < 0.0F || !NodeIsOnSolidGround(*target)) {
					continue;
				}
				float rise = node->Surface - target->Surface;
				if (rise <= 4.0F || rise > s_MantleHeight || static_cast<float>(target->FreeHeight) < s_StandHeight || static_cast<float>(node->FreeHeight) < s_StandHeight + rise * 0.5F) {
					continue;
				}
				adjCost.cost = 1.5F + rise / static_cast<float>(m_NodeDimension) + radiatedCost;
				adjCost.state = const_cast<PathNode*>(target);
				adjacentList->push_back(adjCost);
			}
		}

		// Stairs: a steep walk, two nodes up for one over, for a searcher whose legs take it (see UpdateNodeCosts for what counts). A
		// soldier walks the base game's steep stairs unaided in four seconds; routed as two jump rungs and a landing, which was all the
		// grid could offer for a 2:1 rise, the same stairs took twenty to forty seconds of hopping. Dearer than a diagonal step by the
		// extra node of height, cheaper than the rungs it replaces; and down them likewise, which is cheaper than the falls it replaces.
		if (s_WalksStairs && s_JumpHeight < FLT_MAX) {
			const PathNode* upRight = node->Up ? node->Up->UpRight : nullptr;
			if (node->StairsUpRight && upRight && upRight->m_Navigable) {
				adjCost.cost = (2.24F + extraUpCost * 2.0F + radiatedCost) * HeadRoomFactor(*node, *upRight);
				adjCost.state = const_cast<PathNode*>(upRight);
				adjacentList->push_back(adjCost);
			}
			const PathNode* upLeft = node->Up ? node->Up->LeftUp : nullptr;
			if (node->StairsUpLeft && upLeft && upLeft->m_Navigable) {
				adjCost.cost = (2.24F + extraUpCost * 2.0F + radiatedCost) * HeadRoomFactor(*node, *upLeft);
				adjCost.state = const_cast<PathNode*>(upLeft);
				adjacentList->push_back(adjCost);
			}
			// Down: the node two down and one over whose stairs lead up to this one.
			const PathNode* downRight = node->Down ? node->Down->RightDown : nullptr;
			if (downRight && downRight->m_Navigable && downRight->StairsUpLeft) {
				adjCost.cost = (2.24F + radiatedCost) * HeadRoomFactor(*node, *downRight);
				adjCost.state = const_cast<PathNode*>(downRight);
				adjacentList->push_back(adjCost);
			}
			const PathNode* downLeft = node->Down ? node->Down->DownLeft : nullptr;
			if (downLeft && downLeft->m_Navigable && downLeft->StairsUpRight) {
				adjCost.cost = (2.24F + radiatedCost) * HeadRoomFactor(*node, *downLeft);
				adjCost.state = const_cast<PathNode*>(downLeft);
				adjacentList->push_back(adjCost);
			}
		}

		// Jumping vertically
		if (s_JumpHeight < FLT_MAX) {
			// How high up we can jump from this node
			const PathNode* currentNode = node;
			float totalMaterialCost = 0.0F;
			for (int i = 0; i < s_JumpHeightVertical; ++i) {
				if (currentNode->Up == nullptr || !currentNode->Up->m_Navigable || currentNode->UpMaterial->GetIntegrity() > c_PathFindingDefaultDigStrength) {
					// solid ceiling, stop
					break;
				}
				// Too close to a wall to go up past it: a jet pressed to a cliff face burns and doesn't lift. The next column out is used instead.
				if (Open(*currentNode->UpMaterial) && !RoomToPass(*currentNode->Up, 3.0F)) {
					break;
				}

				float f = i + 2; // Exponential cost increase for jumping higher
				float extraJumpCost = f * 0.5F; // Dearer the higher, but not by the square: at that a 190 px cliff was worth a 1250 px walk round through the valley; at a quarter, units leapt over whole courses rather than walk them.

				// (And for how hard the rung is to fly: a person goes up a node or two out from a wall, where there's room to drift over the
				// lip, not pressed to its face where the jet has to go precisely straight up; see ClimbMarginCost.)
				// (And a hatch barely wider than the body is a risk: the drift has to be just so, and a miss bangs the lip and falls back.)
				float tightRisk = RoomToPass(*currentNode->Up, 4.5F) ? 0.0F : 1.2F;
				// (And for a lip or corner the body's side brushes between the nodes' centre rows, which their clearances don't see: a thin
				// platform's edge beside the column, which caught the shoulder and stopped the climb. Units hung up under such an edge on a
				// route straight up past it when a column a node out, or a route with one more turn, went clear.)
				float grazeRisk = Open(*currentNode->UpMaterial) ? ColumnGrazeCost(currentNode->Pos.m_X, currentNode->Pos.m_Y, currentNode->Up->Pos.m_Y) : 0.0F;
				totalMaterialCost += 1.0F + extraUpCost + extraJumpCost + (GetMaterialTransitionCost(*currentNode->UpMaterial) * 3.0F) + radiatedCost + ClimbMarginCost(*currentNode->Up) + tightRisk + grazeRisk;

				adjCost.cost = totalMaterialCost;
				adjCost.state = static_cast<void*>(currentNode->Up);
				adjacentList->push_back(adjCost);

				currentNode = currentNode->Up;

				// Landing on a ledge beside the jump: a node up the column is only a stop if there's ground under it, which there isn't next to a
				// ledge. So from each rung of the jump, a step sideways onto a node that does stand on ground is offered too, which is how a
				// jetpack really gets onto a platform: up its side, then on. Two steps when the first is still in the air, for the top of a
				// steep slope, whose first node in from the drop is over the face.
				if (!NodeIsOnSolidGround(*currentNode)) {
					auto landing = [&](const PathNode* step, const Material* stepMaterial, float stepCost) -> const PathNode* {
						if (!step || !step->m_Navigable || stepMaterial->GetIntegrity() > s_DigStrength) {
							return nullptr;
						}
						// A landing wants room to stand up in: a jet doesn't come down into a crawlspace, and a unit sent to land on a
						// ledge with a ceiling a few pixels over it climbed into the ceiling and was pushed about under it.
						if (static_cast<float>(step->FreeHeight) < s_StandHeight) {
							return nullptr;
						}
						if (NodeIsOnSolidGround(*step)) {
							// (A landing with the ceiling close over it is where the head meets the lip coming in: a risk.)
							float lipRisk = static_cast<float>(step->FreeHeight) < s_StandHeight * 1.4F ? 2.0F : 0.0F;
							adjCost.cost = totalMaterialCost + stepCost + GetMaterialTransitionCost(*stepMaterial) + radiatedCost + LandingWidthCost(*step) + lipRisk;
							adjCost.state = const_cast<PathNode*>(step);
							adjacentList->push_back(adjCost);
							return nullptr;
						}
						return step;
					};
					if (const PathNode* step = landing(currentNode->Left, currentNode->LeftMaterial, 1.0F)) {
						landing(step->Left, step->LeftMaterial, 2.0F);
					}
					if (const PathNode* step = landing(currentNode->Right, currentNode->RightMaterial, 1.0F)) {
						landing(step->Right, step->RightMaterial, 2.0F);
					}
					// And diagonally up onto a steep slope's face: a slope too steep for the diagonal chains, with its surface a node or more up
					// for every node across, was only climbed by going far above it and coming back down.
					landing(currentNode->LeftUp, currentNode->LeftUpMaterial, 1.4F + extraUpCost);
					landing(currentNode->UpRight, currentNode->UpRightMaterial, 1.4F + extraUpCost);
				}
			}
		} else if (node->Up && node->Up->m_Navigable) {
			adjCost.cost = 1.0F + (extraUpCost) + (GetMaterialTransitionCost(*node->UpMaterial) * 3.0F) + radiatedCost; // Three times more expensive when digging.
			adjCost.state = static_cast<void*>(node->Up);
			adjacentList->push_back(adjCost);
		}

		// Leaps of the legs across a gap or onto a low ledge (see AddLeapLinks).
		if (s_JumpHeight < FLT_MAX && s_LeapHeight > 0.0F && !isInNoGrav) {
			leapsBegin = adjacentList->size();
			AddLeapLinks(*node, adjacentList);
			leapsEnd = adjacentList->size();
		}

		// Flights to other floors (see AddFlightLinks).
		if (s_JumpHeight < FLT_MAX && s_JetTimeMS > 0.0F && !isInNoGrav) {
			AddFlightLinks(*node, adjacentList);
		}

		// Jumping diagonally
		if (s_JumpHeight < FLT_MAX && node->UpRight && !isInNoGrav) {
			const PathNode* currentNode = node->UpRight;
			float totalMaterialCost = 1.4F + (extraUpCost * 1.4F) + (GetMaterialTransitionCost(*node->UpRightMaterial) * 1.4F * 3.0F) + radiatedCost;
			for (int i = 0; i < s_JumpHeightDiagonal; ++i) {
				if (currentNode->UpRight == nullptr || !currentNode->UpRight->m_Navigable || currentNode->UpRightMaterial->GetIntegrity() > c_PathFindingDefaultDigStrength) {
					// solid ceiling, stop
					break;
				}
				// A jet comes down where there's room to stand, not into a crawlspace (see the landings of the vertical jump).
				if (Open(*currentNode->UpRightMaterial) && static_cast<float>(currentNode->UpRight->FreeHeight) < s_StandHeight) {
					break;
				}

				float f = i + 2; // Exponential cost increase for jumping higher
				float extraJumpCost = f * 0.5F; // Dearer the higher, but not by the square: at that a 190 px cliff was worth a 1250 px walk round through the valley; at a quarter, units leapt over whole courses rather than walk them.

				totalMaterialCost += 1.4F + (extraUpCost * 1.4F) + (extraJumpCost * 1.4f) + (GetMaterialTransitionCost(*currentNode->UpRightMaterial) * 1.4F * 3.0F) + radiatedCost;

				adjCost.cost = totalMaterialCost;
				adjCost.state = static_cast<void*>(currentNode->UpRight);
				adjacentList->push_back(adjCost);

				currentNode = currentNode->UpRight;
			}
		}

		if (s_JumpHeight < FLT_MAX && node->LeftUp && !isInNoGrav) {
			const PathNode* currentNode = node->LeftUp;
			float totalMaterialCost = 1.4F + (extraUpCost * 1.4F) + (GetMaterialTransitionCost(*node->LeftUpMaterial) * 1.4F * 3.0F) + radiatedCost;
			for (int i = 0; i < s_JumpHeightDiagonal; ++i) {
				if (currentNode->LeftUp == nullptr || !currentNode->LeftUp->m_Navigable || currentNode->LeftUpMaterial->GetIntegrity() > c_PathFindingDefaultDigStrength) {
					// solid ceiling, stop
					break;
				}
				if (Open(*currentNode->LeftUpMaterial) && static_cast<float>(currentNode->LeftUp->FreeHeight) < s_StandHeight) {
					break;
				}

				float f = i + 2; // Exponential cost increase for jumping higher
				float extraJumpCost = f * 0.5F; // Dearer the higher, but not by the square: at that a 190 px cliff was worth a 1250 px walk round through the valley; at a quarter, units leapt over whole courses rather than walk them.

				totalMaterialCost += 1.4F + (extraUpCost * 1.4F) + (extraJumpCost * 1.4f) + (GetMaterialTransitionCost(*currentNode->LeftUpMaterial) * 1.4F * 3.0F) + radiatedCost;

				adjCost.cost = totalMaterialCost;
				adjCost.state = static_cast<void*>(currentNode->LeftUp);
				adjacentList->push_back(adjCost);

				currentNode = currentNode->LeftUp;
			}
		}

		// Add cost for digging at 45 degrees and for digging upwards. (A step up a slope wants the head room a walk does: a crawl's worth at
		// the least, and dearer under a low ceiling.)
		if (node->UpRight && node->UpRight->m_Navigable && allowDiagonal) {
			adjCost.cost = (1.4F + (extraUpCost * 1.4F) + (GetMaterialTransitionCost(*node->UpRightMaterial) * 1.4F * 3.0F) + radiatedCost) * (Open(*node->UpRightMaterial) ? HeadRoomFactor(*node, *node->UpRight) : 1.0F); // Three times more expensive when digging.
			adjCost.state = static_cast<void*>(node->UpRight);
			adjacentList->push_back(adjCost);
		}

		if (node->LeftUp && node->LeftUp->m_Navigable && allowDiagonal) {
			adjCost.cost = (1.4F + (extraUpCost * 1.4F) + (GetMaterialTransitionCost(*node->LeftUpMaterial) * 1.4F * 3.0F) + radiatedCost) * (Open(*node->LeftUpMaterial) ? HeadRoomFactor(*node, *node->LeftUp) : 1.0F); // Three times more expensive when digging.
			adjCost.state = static_cast<void*>(node->LeftUp);
			adjacentList->push_back(adjCost);
		}
	}

	// From a start in the air, a step against the way the searcher is moving costs for the speed it has to undo: a person in mid-jump goes on
	// to somewhere ahead before turning back for somewhere behind. Free of it, a route asked mid-air at the top of a climb went back down the
	// wall and up again, and the unit, at speed, turned round in the air for it.
	if (node == s_FlyingStart && s_Velocity.MagnitudeIsGreaterThan(1.0F)) {
		for (micropather::StateCost& adjacent: *adjacentList) {
			Vector step = g_SceneMan.ShortestDistance(node->Pos, static_cast<const PathNode*>(adjacent.state)->Pos);
			if (step.MagnitudeIsGreaterThan(0.5F)) {
				float along = step.GetNormalized().Dot(s_Velocity);
				if (along < 0.0F) {
					adjacent.cost += -along * 0.8F;
				}
			}
		}
	}

	// The searcher has failed here lately (PathAgent::Avoid): every step into a node near one costs more for it, so its next route goes another way
	// if there is a reasonable one, rather than at the same jump again.
	if (s_Avoid) {
		for (micropather::StateCost& adjacent: *adjacentList) {
			adjacent.cost += AvoidCost(*static_cast<const PathNode*>(adjacent.state));
		}
	}
	// A flight failed lately (PathAgent::AvoidLinks): from near that take-off to near that landing costs more, so the next route takes off
	// somewhere else for it (a step back, the other side of the shaft) or lands somewhere else; walking past either spot costs nothing.
	// (Marking the landing's place, as it was, made every route by it dearer, walks and all, though the landing was rarely what failed.)
	if (s_AvoidLinks) {
		const float near = static_cast<float>(m_NodeDimension) * 1.5F;
		for (const std::pair<Vector, Vector>& failed: *s_AvoidLinks) {
			if (!g_SceneMan.ShortestDistance(node->Pos, failed.first).MagnitudeIsLessThan(near)) {
				continue;
			}
			for (micropather::StateCost& adjacent: *adjacentList) {
				if (g_SceneMan.ShortestDistance(static_cast<const PathNode*>(adjacent.state)->Pos, failed.second).MagnitudeIsLessThan(near)) {
					adjacent.cost += 25.0F;
				}
			}
		}
	}
	// Which edge the search takes to each leap's landing, all costs in: the cheapest, the first of equals (the search keeps the first it is
	// given at a cost). A leap that is it is noted for the label.
	for (size_t i = leapsBegin; i < leapsEnd; ++i) {
		const micropather::StateCost& leap = (*adjacentList)[i];
		bool taken = true;
		for (size_t j = 0; j < adjacentList->size() && taken; ++j) {
			const micropather::StateCost& other = (*adjacentList)[j];
			if (j != i && other.state == leap.state && (j < i ? other.cost <= leap.cost : other.cost < leap.cost)) {
				taken = false;
			}
		}
		std::pair<const PathNode*, const PathNode*> key(node, static_cast<const PathNode*>(leap.state));
		if (taken) {
			s_LeapsTaken.insert(key);
		} else {
			s_LeapsTaken.erase(key);
		}
	}
}

bool PathFinder::PositionsAreTheSamePathNode(const Vector& pos1, const Vector& pos2) const {
	int startNodeX = std::floor(pos1.m_X / static_cast<float>(m_NodeDimension));
	int startNodeY = std::floor(pos1.m_Y / static_cast<float>(m_NodeDimension));
	int endNodeX = std::floor(pos2.m_X / static_cast<float>(m_NodeDimension));
	int endNodeY = std::floor(pos2.m_Y / static_cast<float>(m_NodeDimension));
	return startNodeX == endNodeX && startNodeY == endNodeY;
}

float PathFinder::SurfaceUnder(const PathNode& node) const {
	int x = static_cast<int>(node.Pos.m_X);
	int top = static_cast<int>(node.Pos.m_Y) - m_NodeDimension / 2;
	if (TerrNav(x, top) != MaterialColorKeys::g_MaterialAir) {
		return -1.0F;
	}
	for (int y = top + 1; y <= top + m_NodeDimension; ++y) {
		if (TerrNav(x, y) != MaterialColorKeys::g_MaterialAir) {
			return static_cast<float>(y);
		}
	}
	return -1.0F;
}

std::string PathFinder::DescribeNodeAt(const Vector& scenePos) {
	int gridX = static_cast<int>(std::floor(scenePos.m_X / static_cast<float>(m_NodeDimension)));
	int gridY = static_cast<int>(std::floor(scenePos.m_Y / static_cast<float>(m_NodeDimension)));
	PathNode* node = GetPathNodeAtGridCoords(gridX, gridY);
	if (!node) {
		return "no node";
	}
	auto integrity = [](const Material* material) { return material ? std::to_string(static_cast<int>(material->GetIntegrity())) : "-"; };
	std::string text = "node " + std::to_string(static_cast<int>(node->Pos.m_X)) + "," + std::to_string(static_cast<int>(node->Pos.m_Y));
	text += node->m_Navigable ? "" : " unnavigable";
	text += " surface " + std::to_string(static_cast<int>(node->Surface)) + " ground " + (NodeIsOnSolidGround(*node) ? "yes" : "no");
	text += " free " + std::to_string(node->FreeHeight) + " clear " + std::to_string(node->ClearLeft) + "/" + std::to_string(node->ClearRight);
	text += " up " + integrity(node->UpMaterial) + " upright " + integrity(node->UpRightMaterial) + " right " + integrity(node->RightMaterial) + " rightdown " + integrity(node->RightDownMaterial);
	text += " down " + integrity(node->DownMaterial) + " downleft " + integrity(node->DownLeftMaterial) + " left " + integrity(node->LeftMaterial) + " leftup " + integrity(node->LeftUpMaterial);
	for (int k = 0; k < 2; ++k) {
		if (node->StepOverRise[k] > 0.0F) {
			text += " stepright" + std::to_string(k + 1) + " " + std::to_string(static_cast<int>(node->StepOverRise[k])) + "/" + std::to_string(node->StepOverRoom[k]);
		}
		if (node->StepOverRiseLeft[k] > 0.0F) {
			text += " stepleft" + std::to_string(k + 1) + " " + std::to_string(static_cast<int>(node->StepOverRiseLeft[k])) + "/" + std::to_string(node->StepOverRoomLeft[k]);
		}
	}
	text += std::string(" grounded ") + (node->Grounded ? "yes" : "no");
	text += node->StairsUpRight ? " stairs-upright" : "";
	text += node->StairsUpLeft ? " stairs-upleft" : "";
	text += node->Ladder ? " ladder" : "";
	text += " anchor " + std::to_string(static_cast<int>(node->Anchor.m_X)) + "," + std::to_string(static_cast<int>(node->Anchor.m_Y));
	return text;
}

Vector PathFinder::StandingPoint(const PathNode& node, float lift) const {
	float surface = SurfaceUnder(node);
	if (surface < 0.0F) {
		return node.Pos;
	}
	return Vector(node.Pos.m_X, surface - lift);
}

bool PathFinder::NodeIsOnSolidGround(const PathNode& node) const {
	// Anything that isn't air is stood on: the bushes on a hillside are walked over, not through, and taking only what's too hard to dig
	// as ground left every node over a thick layer of them hanging in the air, with no way along but a jet.
	return s_JumpHeight == FLT_MAX || (node.Down && node.Grounded);
}

int PathFinder::DropNodes(const PathNode& node) const {
	// Down the column to the first node that stands on something.
	int drop = 0;
	const PathNode* current = &node;
	while (current && !NodeIsOnSolidGround(*current) && drop < c_FallCostReach) {
		current = current->Down;
		++drop;
	}
	return drop;
}

float PathFinder::FallCost(const PathNode& to) const {
	// Nothing for a flier or in no gravity, and nothing within a storey of the ground, which is what a hatch drops. Higher than that, each
	// node of the fall costs about what a rung of a jump does: the jetpack brakes a long fall with fuel at the bottom, and a body without
	// one takes the fall. (The height is measured by walking down the column to the first node that stands on something.)
	if (s_JumpHeight == FLT_MAX || g_SceneMan.IsPointInNoGravArea(to.Pos)) {
		return 0.0F;
	}
	return DropNodes(to) > c_SafeFallNodes ? c_FallCostPerNode : 0.0F;
}

bool PathFinder::Open(const Material& material) const {
	return material.GetIntegrity() <= 5.0F;
}

bool PathFinder::RoomToPass(const PathNode& node, float widths) const {
	// The run of air through the node counts the node's own column: the clearances are counted from the pixels beside the centre, so a
	// 48 px hatch measured 47 from either column in it, and the jet column up it was refused for a unit three half-widths of 16 wide by
	// that one pixel.
	return s_JumpHeight == FLT_MAX || static_cast<float>(node.ClearLeft + node.ClearRight + 1) >= s_HalfWidth * widths;
}

bool PathFinder::HasFloor(const PathNode& node) const {
	return node.Surface >= 0.0F || NodeIsOnSolidGround(node);
}

float PathFinder::HeadRoomFactor(const PathNode& from, const PathNode& to) const {
	if (s_JumpHeight == FLT_MAX) {
		return 1.0F;
	}
	// A node with no floor under it has its free height measured from its own centre, not from a floor, so it says nothing about a
	// body's head room: a step into one is a step off an edge, and only the room where the step starts counts. (Measured from the
	// centre, the node over a hatch or past the top step of a stair read as a crawlspace, and units lay down to walk off the edge.)
	int headRoom = HasFloor(to) ? std::min(from.FreeHeight, to.FreeHeight) : from.FreeHeight;
	if (static_cast<float>(headRoom) < s_CrawlHeight) {
		return 1000.0F;
	}
	if (static_cast<float>(headRoom) < s_StandHeight) {
		return 1.4F; // A crawl is slower; much dearer than this and a tunnel was worth flying over the top of.
	}
	return 1.0F;
}

PathStepKind PathFinder::StepKindBetween(const PathNode* from, const PathNode* to) const {
	if (!from || !to) {
		return PathStepKind::Walk;
	}
	// The material along the step, from whichever side has it sampled (down and right are sampled; up and left are the neighbour's).
	const Material* material = nullptr;
	for (int i = 0; i < PathNode::c_MaxAdjacentNodeCount; ++i) {
		if (from->AdjacentNodes[i] == to) {
			material = from->AdjacentNodeBlockingMaterials[i];
		}
	}
	float dx = g_SceneMan.ShortestDistance(from->Pos, to->Pos).m_X;
	float dy = to->Pos.m_Y - from->Pos.m_Y;
	float nodeSize = static_cast<float>(m_NodeDimension);
	if (material && material->GetIndex() == MaterialColorKeys::g_MaterialDoor) {
		return PathStepKind::Door;
	}
	// Up or down a ladder (either end on one, straight up or down), or off one onto a floor beside or over its top: climbed, not flown.
	// (Not a level step between two floors: the walk across a ladder's foot, from or onto it, is a walk. Labelled a ladder, the unit
	// beside a ladder took hold of it to cross the floor.)
	bool floorWalk = std::abs(dy) < 1.0F && HasFloor(*from) && HasFloor(*to);
	if (s_ClimbsLadders && (from->Ladder || to->Ladder) && !floorWalk && std::abs(dx) <= nodeSize + 1.0F && std::abs(dy) <= nodeSize + 1.0F && (std::abs(dx) < 1.0F || from->Ladder)) {
		return PathStepKind::Ladder;
	}
	// (Reached up to from a floor: a ladder's foot one or two nodes over it.)
	if (s_ClimbsLadders && to->Ladder && !from->Ladder && dy < -1.0F && dy >= -2.0F * nodeSize - 1.0F && std::abs(dx) <= nodeSize + 1.0F) {
		return PathStepKind::Ladder;
	}
	// A mantle (see the mantle edges in AdjacentCost, whose test this is): up onto a ledge one or two nodes up and one across, within the
	// searcher's pull. A kind of its own: as a non-neighbour or a line through the ledge's corner it was labelled a jump, given an apex point
	// 40 px over the landing and flown as a jet climb, with the climb's fuel and arrival checks, and a unit with no jet couldn't follow it.
	if (s_MantleHeight > 0.0F && s_JumpHeight < FLT_MAX && std::abs(std::abs(dx) - nodeSize) < 1.0F && dy < -1.0F && dy >= -2.0F * nodeSize - 1.0F && from->Surface >= 0.0F && to->Surface >= 0.0F) {
		float rise = from->Surface - to->Surface;
		if (rise > 4.0F && rise <= s_MantleHeight && static_cast<float>(to->FreeHeight) >= s_StandHeight && static_cast<float>(from->FreeHeight) >= s_StandHeight + rise * 0.5F) {
			return PathStepKind::Mantle;
		}
	}
	// Something solid on the straight line between the two: a dig if this searcher digs that, and otherwise the step wasn't along that
	// line at all but up the column and over onto a ledge (the landing edges), which is a jump. (Read as a dig, a step up onto a 24 px
	// ledge whose corner the line clipped was neither hopped nor climbed by a unit with no digger, and it stood at the step for ever.)
	if (material && material->GetIntegrity() > c_PathFindingDefaultDigStrength && std::abs(dy) <= nodeSize && std::abs(dx) <= nodeSize) {
		if (material->GetIntegrity() <= s_DigStrength) {
			return PathStepKind::Dig;
		}
		return dy < -1.0F ? PathStepKind::Jump : PathStepKind::Walk;
	}
	// Stairs, up or down (see UpdateNodeCosts): two nodes of height for one of width, with the lower node's stairs flag set towards the upper.
	if (std::abs(std::abs(dx) - nodeSize) < 1.0F && std::abs(std::abs(dy) - 2.0F * nodeSize) < 1.0F) {
		const PathNode* lower = dy < 0.0F ? from : to;
		bool rightwards = dy < 0.0F ? dx > 0.0F : dx < 0.0F; // From the lower node, which way the stairs go up.
		if (rightwards ? lower->StairsUpRight : lower->StairsUpLeft) {
			return PathStepKind::Stairs;
		}
	}
	// A leap's two floors, two or more nodes apart, where the search's edge between them was the leap (see s_LeapsTaken).
	if (s_LeapHeight > 0.0F && s_JumpHeight < FLT_MAX && std::abs(dx) > nodeSize * 1.5F && s_LeapsTaken.count({from, to}) > 0) {
		return PathStepKind::Leap;
	}
	if (dy < -1.0F) {
		return PathStepKind::Jump;
	}
	// A step to a node that isn't a neighbour, level or down: a flight link (see AddFlightLinks), flown.
	if (!material && (std::abs(dx) > nodeSize + 1.0F || std::abs(dy) > nodeSize + 1.0F)) {
		return PathStepKind::Jump;
	}
	if (dy > nodeSize + 1.0F || (dy > 1.0F && std::abs(dx) < 1.0F)) {
		return PathStepKind::Fall;
	}
	// (Only where both have floors: see HeadRoomFactor.)
	int headRoom = HasFloor(*to) ? std::min(from->FreeHeight, to->FreeHeight) : from->FreeHeight;
	if (static_cast<float>(headRoom) < s_StandHeight) {
		return PathStepKind::Crawl;
	}
	return PathStepKind::Walk;
}

float PathFinder::ClimbMarginCost(const PathNode& node) const {
	if (s_JumpHeight == FLT_MAX) {
		return 0.0F;
	}
	float nearSide = static_cast<float>(std::min(node.ClearLeft, node.ClearRight));
	float farSide = static_cast<float>(std::max(node.ClearLeft, node.ClearRight));
	float cost = 0.0F;
	// Hugging a wall with open air on the other side: a column a node or two out would do, and is far easier to fly.
	if (nearSide < s_HalfWidth + 6.0F && farSide >= static_cast<float>(m_NodeDimension) * 1.5F) {
		cost += 1.5F;
	}
	// A gap barely wider than the body: the lift has to be precise.
	if (static_cast<float>(node.ClearLeft + node.ClearRight + 1) < s_HalfWidth * 4.0F) {
		cost += 1.0F;
	}
	return cost;
}

bool PathFinder::LeapFits(const PathNode& from, const PathNode& to) const {
	if (s_LeapHeight <= 0.0F || from.Surface < 0.0F || to.Surface < 0.0F || !NodeIsOnSolidGround(from) || !NodeIsOnSolidGround(to) || !to.m_Navigable) {
		return false;
	}
	if (static_cast<float>(from.FreeHeight) < s_StandHeight || static_cast<float>(to.FreeHeight) < s_StandHeight) {
		return false;
	}
	const float nodeSize = static_cast<float>(m_NodeDimension);
	float dx = g_SceneMan.ShortestDistance(from.Pos, to.Pos).m_X;
	float across = std::abs(dx);
	float rise = from.Surface - to.Surface; // Up is positive.
	// (Most of the leap's height at the most: the feet have to clear the lip, and a little to spare.)
	if (across < nodeSize * 1.5F || rise > s_LeapHeight * 0.75F || rise < -nodeSize * 2.0F) {
		return false;
	}
	// The arc under gravity: up at the speed that rises the leap's height, across at the leap's speed; where it comes down to the landing's
	// height, with a margin for a take-off a little short of the edge.
	float gravity = std::max(1.0F, g_SceneMan.GetGlobalAcc().m_Y * c_PPM);
	float up = std::sqrt(2.0F * gravity * s_LeapHeight);
	float speed = s_LeapSpeed * c_PPM;
	float under = up * up - 2.0F * gravity * std::max(0.0F, rise);
	if (under < 0.0F) {
		return false;
	}
	float flightTime = (up + std::sqrt(under + 2.0F * gravity * std::max(0.0F, -rise))) / gravity;
	if (across > speed * flightTime * 0.85F) {
		return false;
	}
	// The body along that arc (its middle, head and feet), from standing at the take-off to standing at the landing, in open air. The time
	// across at the leap's speed is when it is over the landing; the arc is followed to then.
	float direction = dx < 0.0F ? -1.0F : 1.0F;
	float arcTime = across / speed;
	Vector start(from.Pos.m_X, from.Surface - s_StandHeight * 0.5F);
	const int segments = 6;
	Vector last = start;
	for (int k = 1; k <= segments; ++k) {
		float t = arcTime * static_cast<float>(k) / static_cast<float>(segments);
		Vector point(start.m_X + direction * speed * t, start.m_Y - up * t + 0.5F * gravity * t * t);
		// (Not below the landing's standing height at the end: the last piece comes down onto the floor there.)
		if (k == segments) {
			point.m_Y = std::min(point.m_Y, to.Surface - s_StandHeight * 0.5F);
		}
		for (float offset: {0.0F, -s_StandHeight * 0.45F, s_StandHeight * 0.45F - 3.0F}) {
			// (The feet only in the middle of the arc: at either end they are on the floor.)
			if (offset > 0.0F && (k == 1 || k == segments)) {
				continue;
			}
			// (A door of the grid's own side is open to the leap as it is to the walk: the arc is traced on the live terrain, where the side's
			// doors are drawn, and a leap across one's own hatch standing open was refused.)
			Vector a = last + Vector(0.0F, offset);
			Vector b = point + Vector(0.0F, offset);
			const Material* along = StrongestMaterialAlongLine(a, b);
			if (!Open(*along) && !(along->GetIndex() == MaterialColorKeys::g_MaterialDoor && DoorSeenThrough(a) && DoorSeenThrough((a + b) * 0.5F) && DoorSeenThrough(b))) {
				return false;
			}
		}
		last = point;
	}
	return true;
}

bool PathFinder::DoorSeenThrough(const Vector& at) const {
	const float nodeSize = static_cast<float>(m_NodeDimension);
	int nodeId = ConvertCoordsToNodeId(static_cast<int>(std::floor(at.m_X / nodeSize)), static_cast<int>(std::floor(at.m_Y / nodeSize)));
	const PathNode* node = nodeId != -1 ? &m_NodeGrid[nodeId] : nullptr;
	if (!node) {
		return false;
	}
	for (int i = 0; i < PathNode::c_MaxAdjacentNodeCount; ++i) {
		const Material* mine = node->AdjacentNodeBlockingMaterials[i];
		if (mine && mine->GetIndex() == MaterialColorKeys::g_MaterialDoor) {
			return false;
		}
		const PathNode* neighbour = node->AdjacentNodes[i];
		const Material* theirs = neighbour ? neighbour->AdjacentNodeBlockingMaterials[(i + 4) % PathNode::c_MaxAdjacentNodeCount] : nullptr;
		if (theirs && theirs->GetIndex() == MaterialColorKeys::g_MaterialDoor) {
			return false;
		}
	}
	return true;
}

void PathFinder::AddLeapLinks(const PathNode& node, std::vector<micropather::StateCost>* adjacentList) {
	if (node.Surface < 0.0F || !NodeIsOnSolidGround(node) || static_cast<float>(node.FreeHeight) < s_StandHeight) {
		return;
	}
	const float nodeSize = static_cast<float>(m_NodeDimension);
	const int gridX = static_cast<int>(std::floor(node.Pos.m_X / nodeSize));
	const int gridY = static_cast<int>(std::floor(node.Pos.m_Y / nodeSize));
	micropather::StateCost adjCost;
	for (int dx = -5; dx <= 5; ++dx) {
		if (std::abs(dx) < 2) {
			continue;
		}
		for (int dy = -1; dy <= 2; ++dy) {
			const PathNode* target = GetPathNodeAtGridCoords(gridX + dx, gridY + dy);
			if (!target || !LeapFits(node, *target)) {
				continue;
			}
			// A little over the walk of the same distance (a node of walk is 1), and a little more for a leap up (the landing has to be
			// right): so a walk wins where there is one, and the leap where there is a gap or a lip, well under any flight.
			float rise = node.Surface - target->Surface;
			adjCost.cost = static_cast<float>(std::abs(dx)) + 1.2F + (rise > 4.0F ? rise / nodeSize : 0.0F);
			adjCost.state = const_cast<PathNode*>(target);
			adjacentList->push_back(adjCost);
		}
	}
}

float PathFinder::ColumnGrazeCost(float x, float fromY, float toY) const {
	if (s_JumpHeight == FLT_MAX || s_HalfWidth <= 0.0F) {
		return 0.0F;
	}
	auto solidAt = [this](float px, float py) {
		unsigned char id = TerrNav(static_cast<int>(px), static_cast<int>(py));
		return id != MaterialColorKeys::g_MaterialAir && !Open(*g_SceneMan.GetMaterialFromID(id));
	};
	// (Every 4 px: a lip thinner than that is no lip to a body.)
	float top = std::min(fromY, toY);
	float bottom = std::max(fromY, toY);
	float reach = s_HalfWidth + 2.0F;
	bool leftTouches = false;
	bool rightTouches = false;
	for (float y = top; y <= bottom && !(leftTouches && rightTouches); y += 4.0F) {
		leftTouches = leftTouches || solidAt(x - reach, y);
		rightTouches = rightTouches || solidAt(x + reach, y);
	}
	return leftTouches != rightTouches ? 2.0F : 0.0F;
}

void PathFinder::DrawDebug(const Box& area, const PathAgent& agent) {
	static const unsigned char standColor = static_cast<unsigned char>(Color(80, 220, 90).GetIndex());
	static const unsigned char crawlColor = static_cast<unsigned char>(Color(240, 210, 60).GetIndex());
	static const unsigned char noRoomColor = static_cast<unsigned char>(Color(230, 60, 50).GetIndex());
	static const unsigned char stepColor = static_cast<unsigned char>(Color(70, 220, 230).GetIndex());
	static const unsigned char stairsColor = static_cast<unsigned char>(Color(220, 80, 220).GetIndex());
	static const unsigned char channelColor = static_cast<unsigned char>(Color(150, 120, 255).GetIndex());
	static const unsigned char ladderColor = static_cast<unsigned char>(Color(255, 150, 40).GetIndex());
	static const unsigned char leapColor = static_cast<unsigned char>(Color(140, 255, 200).GetIndex());
	// The grid is the same for every searcher; what fits is the searcher's: the inspected unit's sizes (see Scene::Update), or a soldier's.
	const float stand = agent.StandHeight;
	const float crawl = agent.CrawlHeight;
	// The leaps are the searcher's too (its legs' height and speed), so the overlay looks at the grid as it would.
	SearcherState kept;
	ApplyAgent(agent);
	s_FlyingStart = nullptr;
	std::vector<micropather::StateCost> leaps;
	int fromX = static_cast<int>(std::floor(area.GetCorner().m_X / static_cast<float>(m_NodeDimension)));
	int fromY = static_cast<int>(std::floor(area.GetCorner().m_Y / static_cast<float>(m_NodeDimension)));
	int toX = static_cast<int>(std::ceil((area.GetCorner().m_X + area.GetWidth()) / static_cast<float>(m_NodeDimension)));
	int toY = static_cast<int>(std::ceil((area.GetCorner().m_Y + area.GetHeight()) / static_cast<float>(m_NodeDimension)));
	for (int gy = std::max(0, fromY); gy <= toY; ++gy) {
		for (int gx = fromX; gx <= toX; ++gx) {
			const PathNode* node = GetPathNodeAtGridCoords(gx, gy);
			// (A ladder node: an orange tick where the climber's body goes.)
			if (node && node->m_Navigable && node->Ladder) {
				g_PrimitiveMan.DrawLinePrimitive(node->Anchor + Vector(-2.0F, 0.0F), node->Anchor + Vector(2.0F, 0.0F), ladderColor);
			}
			// (A node in the air whose anchor is off its centre, in a shaft or a hatch: a small dot where routes through it go.)
			if (node && node->m_Navigable && node->Surface < 0.0F && std::abs(node->Anchor.m_X - node->Pos.m_X) >= 2.0F) {
				g_PrimitiveMan.DrawCircleFillPrimitive(node->Anchor, 1, channelColor);
			}
			if (!node || !node->m_Navigable || node->Surface < 0.0F || !NodeIsOnSolidGround(*node)) {
				continue;
			}
			Vector standing(node->Anchor.m_X, node->Surface - 3.0F);
			float free = static_cast<float>(node->FreeHeight);
			unsigned char color = free >= stand ? standColor : (free >= crawl ? crawlColor : noRoomColor);
			g_PrimitiveMan.DrawCircleFillPrimitive(standing, 2, color);
			for (int k = 0; k < 2; ++k) {
				if (node->StepOverRise[k] > 0.0F) {
					const PathNode* target = k == 0 ? node->Right : (node->Right ? node->Right->Right : nullptr);
					if (target) {
						Vector over(target->Pos.m_X, standing.m_Y);
						g_PrimitiveMan.DrawLinePrimitive(standing + Vector(0.0F, -node->StepOverRise[k] - 2.0F), over + Vector(0.0F, -node->StepOverRise[k] - 2.0F), stepColor);
					}
				}
			}
			if (node->StairsUpRight && node->Up && node->Up->UpRight) {
				g_PrimitiveMan.DrawLinePrimitive(standing, node->Up->UpRight->Pos, stairsColor);
			}
			if (node->StairsUpLeft && node->Up && node->Up->LeftUp) {
				g_PrimitiveMan.DrawLinePrimitive(standing, node->Up->LeftUp->Pos, stairsColor);
			}
			// Leap links (see AddLeapLinks), as an arc of two lines over the gap or up onto the lip, from the floors they can start from: the
			// edge of a floor, or under a lip a node to either side.
			if (s_LeapHeight > 0.0F && s_JumpHeight < FLT_MAX) {
				auto lip = [node](const PathNode* side) { return side && side->Surface >= 0.0F && side->Surface < node->Surface - 4.0F; };
				if (IsFloorEdge(*node) || lip(node->Left) || lip(node->Right)) {
					leaps.clear();
					AddLeapLinks(*node, &leaps);
					for (const micropather::StateCost& leap: leaps) {
						const PathNode* target = static_cast<const PathNode*>(leap.state);
						Vector landing(target->Anchor.m_X, target->Surface - 3.0F);
						Vector apex = standing + g_SceneMan.ShortestDistance(standing, landing) * 0.5F - Vector(0.0F, s_LeapHeight * 0.6F);
						g_PrimitiveMan.DrawLinePrimitive(standing, apex, leapColor);
						g_PrimitiveMan.DrawLinePrimitive(apex, landing, leapColor);
					}
				}
			}
		}
	}
}

bool PathFinder::IsFloorEdge(const PathNode& node) const {
	if (!NodeIsOnSolidGround(node) || node.Surface < 0.0F) {
		return false;
	}
	return (node.Left && !NodeIsOnSolidGround(*node.Left)) || (node.Right && !NodeIsOnSolidGround(*node.Right));
}

void PathFinder::CollectFlightLinks(const PathNode& node, std::vector<FlightLink>& links) {
	if (!IsFloorEdge(node)) {
		return;
	}
	const float nodeSize = static_cast<float>(m_NodeDimension);
	const float ppm = c_PPM;
	const int gridX = static_cast<int>(std::floor(node.Pos.m_X / nodeSize));
	const int gridY = static_cast<int>(std::floor(node.Pos.m_Y / nodeSize));
	const float standY = node.Surface - s_StandHeight * 0.5F;
	for (int dy = -12; dy <= 6; ++dy) {
		for (int dx = -8; dx <= 8; ++dx) {
			if (std::abs(dx) <= 1 && std::abs(dy) <= 1) {
				continue;
			}
			const PathNode* target = GetPathNodeAtGridCoords(gridX + dx, gridY + dy);
			if (!target || !target->m_Navigable || !IsFloorEdge(*target) || static_cast<float>(target->FreeHeight) < s_StandHeight) {
				continue;
			}
			float rise = node.Surface - target->Surface; // Up is positive.
			float across = std::abs(g_SceneMan.ShortestDistance(node.Pos, target->Pos).m_X);
			if (across < nodeSize * 1.5F && rise > -nodeSize) {
				continue; // (Next door and level or up: the rungs and mantles have those.)
			}
			// The fuel the flight takes, as the route-follower reckons it (AHuman::FlightFuelNeeded): the climb on this unit's own jet (its
			// fuel per pixel, from its push and tank), the burst that lights it and a reserve for the top; the crossing lit about half the time.
			float climbFuel = rise > 4.0F ? (rise + 12.0F) * s_JetClimbMSPerPx + (across >= 10.0F ? 450.0F : 300.0F) : 0.0F;
			float fuel = 200.0F + climbFuel + across / (5.0F * ppm) * 0.5F * 1000.0F * 1.2F;
			if (fuel > s_JetTimeMS * 0.95F) {
				continue;
			}
			// Open air: up at this column to the cruising height, across at it (at two heights), and down onto the landing.
			float cruiseY = std::min(standY, target->Surface - s_StandHeight * 1.1F - 12.0F);
			Vector here(node.Pos.m_X, standY);
			Vector cruiseHere(node.Pos.m_X, cruiseY);
			Vector cruiseThere(target->Pos.m_X, cruiseY);
			Vector there(target->Pos.m_X, target->Surface - 4.0F);
			if (cruiseY < standY - 2.0F && !Open(*StrongestMaterialAlongLine(here, cruiseHere))) {
				continue;
			}
			if (!Open(*StrongestMaterialAlongLine(cruiseHere, cruiseThere)) || !Open(*StrongestMaterialAlongLine(cruiseHere + Vector(0.0F, -s_StandHeight * 0.5F), cruiseThere + Vector(0.0F, -s_StandHeight * 0.5F)))) {
				continue;
			}
			if (!Open(*StrongestMaterialAlongLine(cruiseThere, there))) {
				continue;
			}
			// The cost: the flight's time against a walk's (a node of walk is about half a second), the take-off and landing, and the fuel.
			float seconds = std::max(0.0F, rise) / (4.0F * ppm) + across / (5.0F * ppm) + std::max(0.0F, -rise) / (6.0F * ppm) + 0.6F;
			// And the risk: how likely the flight is to come off first time, not only how long it takes. A tank most of the way spent, a tall
			// climb, a landing under a low ceiling (the head meets the lip on the way in), or a take-off pressed to a wall: each makes a
			// miss likelier, and a miss costs the fall and the try again. (Priced on time alone, the hard quick jump through a hatch beat
			// the two easy hops beside it, and units missed it again and again.)
			float risk = 0.0F;
			float tankShare = fuel / std::max(s_JetTimeMS, 1.0F);
			if (tankShare > 0.6F) {
				risk += (tankShare - 0.6F) * 12.0F;
			}
			float riseNodes = std::max(0.0F, rise) / nodeSize;
			if (riseNodes > 4.0F) {
				risk += (riseNodes - 4.0F) * 0.4F;
			}
			if (static_cast<float>(target->FreeHeight) < s_StandHeight * 1.4F) {
				risk += 2.0F;
			}
			if (!RoomToPass(node, 3.0F)) {
				risk += 1.5F;
			}
			// The climb up this column brushing a lip or corner on one side (see ColumnGrazeCost).
			if (cruiseY < standY - 2.0F) {
				risk += ColumnGrazeCost(node.Pos.m_X, standY, cruiseY);
			}
			float cost = seconds * 2.2F + 1.5F + fuel / 1000.0F + LandingWidthCost(*target) + risk;
			links.push_back({target, cost, fuel});
		}
	}
	// Cheapest first: a wide window offers many near-alike landings, and AddFlightLinks takes the first few.
	std::sort(links.begin(), links.end(), [](const FlightLink& a, const FlightLink& b) { return a.cost < b.cost; });
}

void PathFinder::AddFlightLinks(const PathNode& node, std::vector<micropather::StateCost>* adjacentList) {
	std::vector<FlightLink> links;
	CollectFlightLinks(node, links);
	micropather::StateCost adjCost;
	int added = 0;
	for (const FlightLink& link: links) {
		if (added++ >= 8) {
			break;
		}
		adjCost.cost = link.cost;
		adjCost.state = const_cast<PathNode*>(link.target);
		adjacentList->push_back(adjCost);
	}
}

float PathFinder::LandingWidthCost(const PathNode& node) const {
	if (node.Surface < 0.0F) {
		return 0.0F;
	}
	// Floor level with the landing's, a node either side.
	auto level = [&node](const PathNode* side) {
		return side && side->m_Navigable && side->Surface >= 0.0F && std::abs(side->Surface - node.Surface) <= 6.0F;
	};
	int sides = (level(node.Left) ? 1 : 0) + (level(node.Right) ? 1 : 0);
	return static_cast<float>(2 - sides);
}

float PathFinder::AvoidCost(const PathNode& node) const {
	if (!s_Avoid) {
		return 0.0F;
	}
	for (const Vector& place: *s_Avoid) {
		if (g_SceneMan.ShortestDistance(node.Pos, place).MagnitudeIsLessThan(static_cast<float>(m_NodeDimension) * 1.5F)) {
			// About what a detour of twenty-odd nodes costs: taken only when there's no other reasonable way.
			return 25.0F;
		}
	}
	return 0.0F;
}

float PathFinder::GetMaterialTransitionCost(const Material& material) const {
	float strength = material.GetIntegrity();

	// A door is open to whoever can dig it or shoot it open; anything else is open to whoever can dig it.
	bool door = material.GetIndex() == MaterialColorKeys::g_MaterialDoor;
	if (strength > (door ? s_BreachStrength : s_DigStrength)) {
		strength *= 1000.0F;
	}

	return strength;
}

const Material* PathFinder::StrongestMaterialAlongLine(const Vector& start, const Vector& end) const {
	return g_SceneMan.CastMaxStrengthRayMaterial(start, end, 0, MaterialColorKeys::g_MaterialAir, m_LadderMaterial);
}

unsigned char PathFinder::TerrNav(int x, int y) const {
	unsigned char id = g_SceneMan.GetTerrMatter(x, y);
	return (m_LadderMaterial != 0 && id == m_LadderMaterial) ? static_cast<unsigned char>(MaterialColorKeys::g_MaterialAir) : id;
}

void PathFinder::AddTeamAvoid(const Vector& place, double untilMS) {
	std::lock_guard<std::mutex> lock(m_TeamAvoidMutex);
	m_TeamAvoid.emplace_back(place, untilMS);
}

void PathFinder::AddTeamAvoidLink(const Vector& from, const Vector& to, double untilMS) {
	std::lock_guard<std::mutex> lock(m_TeamAvoidMutex);
	std::erase_if(m_TeamAvoidLinks, [untilMS](const AvoidLink& link) { return link.until < untilMS - 600000.0; });
	m_TeamAvoidLinks.push_back({from, to, untilMS});
}

void PathFinder::GetTeamAvoidLinks(std::vector<std::pair<Vector, Vector>>& links, double nowMS) const {
	std::lock_guard<std::mutex> lock(m_TeamAvoidMutex);
	for (const AvoidLink& link: m_TeamAvoidLinks) {
		if (link.until > nowMS) {
			links.emplace_back(link.from, link.to);
		}
	}
}

void PathFinder::GetTeamAvoid(std::vector<Vector>& places, double nowMS) const {
	std::lock_guard<std::mutex> lock(m_TeamAvoidMutex);
	for (const std::pair<Vector, double>& avoid: m_TeamAvoid) {
		if (avoid.second > nowMS) {
			places.push_back(avoid.first);
		}
	}
}

bool PathFinder::UpdateNodeCosts(PathNode* node) const {
	if (!node) {
		return false;
	}

	std::array<const Material*, PathNode::c_MaxAdjacentNodeCount> oldMaterials = node->AdjacentNodeBlockingMaterials;
	int oldFreeHeight = node->FreeHeight;
	int oldClearLeft = node->ClearLeft;
	int oldClearRight = node->ClearRight;
	bool oldLadder = node->Ladder;
	bool oldStairsUpRight = node->StairsUpRight;
	bool oldGrounded = node->Grounded;
	bool oldStairsUpLeft = node->StairsUpLeft;
	std::array<float, 2> oldStepOverRise = node->StepOverRise;
	std::array<int, 2> oldStepOverRoom = node->StepOverRoom;
	std::array<float, 2> oldStepOverRiseLeft = node->StepOverRiseLeft;
	std::array<int, 2> oldStepOverRoomLeft = node->StepOverRoomLeft;

	auto getStrongerMaterial = [](const Material* first, const Material* second) {
		return first->GetIntegrity() > second->GetIntegrity() ? first : second;
	};

	// How much room there is at this node: air up from the surface (or the centre), and to either side a little over the surface.
	{
		int x = static_cast<int>(node->Pos.m_X);
		node->Surface = SurfaceUnder(*node);
		// The floor a body at this node stands on: the first solid pixel from the centre down to a node below it (a floor just past the cell's
		// edge is still what the node stands on; measured from the centre instead, a crawl-high tunnel read as no room at all).
		// (The surface in the cell is the floor whether the centre is over it or a few pixels under it: a centre just under a slope read
		// as buried, with no room at all, and walking along the slope was out of the question.)
		int centreY = static_cast<int>(node->Pos.m_Y);
		int floor = -1;
		if (node->Surface >= 0.0F) {
			floor = static_cast<int>(node->Surface);
		} else if (TerrNav(x, centreY) == MaterialColorKeys::g_MaterialAir) {
			for (int y = centreY + 1; y <= centreY + m_NodeDimension; ++y) {
				if (TerrNav(x, y) != MaterialColorKeys::g_MaterialAir) {
					floor = y;
					break;
				}
			}
		} else {
			floor = centreY; // Buried: no room at all.
		}
		int from = floor >= 0 ? floor - 1 : centreY;
		// The most open of a few lines up, the centre's and a few pixels either side: a ladder's rungs stick out 8 px from a shaft's wall, one
		// every 8 px, and a node whose centre was over them read as having no head room at all, so the shaft beside a ladder was no way up
		// and every route past one came back impossible. A ceiling or a floor slab fills the cell, and stops all the lines alike.
		int free = 0;
		for (int dx: {0, -4, 4, -8, 8}) {
			int lineFree = 0;
			while (lineFree < PathNode::c_ClearanceReach && TerrNav(x + dx, from - lineFree) == MaterialColorKeys::g_MaterialAir) {
				++lineFree;
			}
			free = std::max(free, lineFree);
		}
		node->FreeHeight = free;
		int sideY = floor >= 0 ? floor - 10 : centreY;
		// (Out to two nodes a side: measured to one, a shaft two nodes wide read as narrower than it was from either of its columns, since
		// neither is in the middle, and a jet column was refused where a body would have fitted twice over.)
		// (At two heights 4 px apart, the more open of each side: a ladder's rungs are 4 px thick on an 8 px pitch, so one of the two passes
		// between them; measured at one, a shaft with a ladder up its side read 8 px narrower, and too narrow to jet up.)
		int left = 0;
		int right = 0;
		for (int dy: {0, -4}) {
			int lineLeft = 0;
			while (lineLeft < m_NodeDimension * 2 && TerrNav(x - 1 - lineLeft, sideY + dy) == MaterialColorKeys::g_MaterialAir) {
				++lineLeft;
			}
			int lineRight = 0;
			while (lineRight < m_NodeDimension * 2 && TerrNav(x + 1 + lineRight, sideY + dy) == MaterialColorKeys::g_MaterialAir) {
				++lineRight;
			}
			left = std::max(left, lineLeft);
			right = std::max(right, lineRight);
		}
		node->ClearLeft = left;
		node->ClearRight = right;

		// The anchor (see PathNode::Anchor), from the same look either side, two nodes out. Kept off a wall by 14 px, a soldier's half-width and
		// a little; in the air with walls both sides within the look, in the channel's middle. On a floor it is only moved where the floor goes
		// on there (never off an edge), and nowhere further than a node from the centre.
		// (In signed numbers: m_NodeDimension is unsigned, and x - m_NodeDimension for the grid's first column, x 12, came to four billion,
		// which put the clamp's low end over its high end; libstdc++'s checks abort on that (every map crashed loading on macOS), and on Windows
		// those nodes' anchors went four billion pixels off.)
		const int nodeSize = static_cast<int>(m_NodeDimension);
		float anchorX = static_cast<float>(x);
		const int reachSide = nodeSize * 2;
		const float offWall = 14.0F;
		bool wallLeft = left < reachSide;
		bool wallRight = right < reachSide;
		if (floor < 0 || floor > centreY + nodeSize / 2) {
			if (wallLeft && wallRight) {
				anchorX += static_cast<float>(right - left) * 0.5F;
			} else if (wallLeft && static_cast<float>(left) < offWall) {
				anchorX += offWall - static_cast<float>(left);
			} else if (wallRight && static_cast<float>(right) < offWall) {
				anchorX -= offWall - static_cast<float>(right);
			}
		} else {
			if (wallLeft && static_cast<float>(left) < offWall && static_cast<float>(right) > offWall * 2.0F) {
				anchorX += offWall - static_cast<float>(left);
			} else if (wallRight && static_cast<float>(right) < offWall && static_cast<float>(left) > offWall * 2.0F) {
				anchorX -= offWall - static_cast<float>(right);
			}
			if (anchorX != static_cast<float>(x)) {
				int ax = static_cast<int>(anchorX);
				bool floorThere = false;
				for (int y = floor - 3; y <= floor + 6 && !floorThere; ++y) {
					floorThere = TerrNav(ax, y) != MaterialColorKeys::g_MaterialAir;
				}
				if (!floorThere) {
					anchorX = static_cast<float>(x);
				}
			}
		}
		anchorX = std::clamp(anchorX, static_cast<float>(x - nodeSize), static_cast<float>(x + nodeSize));
		node->Anchor = Vector(anchorX + (node->Pos.m_X - static_cast<float>(x)), node->Pos.m_Y);

		// A ladder: rungs of the Ladder material at this node's height, within a node either side of its centre. The nearest strip of rungs
		// is the ladder; the climber's body hangs beside it on the open side (off the wall the rungs stand from), and that is the anchor, so a
		// route up a ladder runs where the body climbs. (Only the Ladder material: a background ladder is no material at all, and the
		// nodes are measured on many threads, where the scene's particles can't be looked at.)
		node->Ladder = false;
		if (m_LadderMaterial != 0) {
			int stripLeft = INT_MAX;
			int stripRight = INT_MIN;
			for (int dy: {-8, 0, 8}) {
				int y = centreY + dy;
				for (int d = 0; d <= nodeSize; ++d) {
					for (int sx: {x - d, x + d}) {
						if (g_SceneMan.GetTerrMatter(sx, y) == m_LadderMaterial) {
							stripLeft = std::min(stripLeft, sx);
							stripRight = std::max(stripRight, sx);
						}
					}
				}
			}
			if (stripLeft != INT_MAX && stripRight - stripLeft <= nodeSize) {
				node->Ladder = true;
				int mid = (stripLeft + stripRight) / 2;
				auto wall = [&](int wx) {
					unsigned char id = TerrNav(wx, centreY);
					return id != MaterialColorKeys::g_MaterialAir && id != m_LadderMaterial;
				};
				float bodyX = static_cast<float>(mid);
				if (wall(stripLeft - 2)) {
					bodyX = static_cast<float>(stripRight) + 7.0F;
				} else if (wall(stripRight + 2)) {
					bodyX = static_cast<float>(stripLeft) - 7.0F;
				}
				node->Anchor = Vector(bodyX + (node->Pos.m_X - static_cast<float>(x)), node->Pos.m_Y);
			}
		}
	}

	// Stepping over something low on a floor: a kerb, a sandbag, a lump of rubble, between this node's floor and a floor level with it one or
	// two nodes to the right. The walk there is blocked by it, and with too little room on top to stand, the grid had no way past at all: a
	// unit ordered a few steps along a corridor with a knee-high lump in it went round by the next storey. The lump's height over the floor
	// and the air over it are kept, and the search allows the step for what the searcher can step, vault or crawl over (see AdjacentCost).
	{
		for (int k = 0; k < 4; ++k) {
			bool leftward = k >= 2;
			int reach = k % 2;
			float& riseOut = leftward ? node->StepOverRiseLeft[reach] : node->StepOverRise[reach];
			int& roomOut = leftward ? node->StepOverRoomLeft[reach] : node->StepOverRoom[reach];
			riseOut = -1.0F;
			roomOut = 0;
			const PathNode* next = leftward ? node->Left : node->Right;
			const PathNode* target = reach == 0 ? next : (next ? (leftward ? next->Left : next->Right) : nullptr);
			if (!target || node->Surface < 0.0F) {
				continue;
			}
			float targetSurface = SurfaceUnder(*target);
			if (targetSurface < 0.0F || std::abs(targetSurface - node->Surface) > 6.0F) {
				continue;
			}
			int floorY = static_cast<int>(std::max(node->Surface, targetSurface));
			int highest = floorY; // The top of the highest thing between, as the first air over it.
			int room = PathNode::c_ClearanceReach;
			bool passable = true;
			int fromX = static_cast<int>(std::min(node->Pos.m_X, target->Pos.m_X));
			int toX = static_cast<int>(std::max(node->Pos.m_X, target->Pos.m_X));
			for (int x = fromX + 2; x < toX - 1 && passable; x += 2) {
				// The top of what stands here: the last solid pixel under a clear run of 8. (Not the first air going up: bunker blocks have
				// pockets of air in their material, and a 24 px metal block read as having 1 px of room over it, from inside itself.)
				int y = floorY - 1;
				int lastSolid = floorY;
				int clearRun = 0;
				while (y > floorY - 48 && clearRun < 8) {
					if (TerrNav(x, y) != MaterialColorKeys::g_MaterialAir) {
						lastSolid = y;
						clearRun = 0;
					} else {
						++clearRun;
					}
					--y;
				}
				if (clearRun < 8) {
					passable = false;
					break;
				}
				y = lastSolid - 1;
				int air = 0;
				while (air < PathNode::c_ClearanceReach && TerrNav(x, y - air) == MaterialColorKeys::g_MaterialAir) {
					++air;
				}
				highest = std::min(highest, y + 1);
				room = std::min(room, air);
			}
			float rise = static_cast<float>(floorY - highest);
			if (passable && rise > 4.0F) {
				riseOut = rise;
				roomOut = room;
			}
		}
	}

	// Stairs: a steep walk, two nodes up for one over, that legs can take. Flagged when both nodes have a surface, the rise is between 30
	// and 60 px over the 24 of width (about 50 to 70 degrees: the base game's steep stairs are 6 px risers on 3 px treads), and two lines a
	// little over the slope between the two surfaces are clear, which is a staircase or a slope and not a wall with a ledge on it. Gentler
	// rises are the diagonal step's; steeper ones a jump's.
	{
		auto stairsTo = [&](const PathNode* target) -> bool {
			if (!target || node->Surface < 0.0F) {
				return false;
			}
			float targetSurface = SurfaceUnder(*target);
			if (targetSurface < 0.0F) {
				return false;
			}
			float rise = node->Surface - targetSurface;
			if (rise < 30.0F || rise > 60.0F) {
				return false;
			}
			Vector here(node->Pos.m_X, node->Surface);
			Vector there(target->Pos.m_X, targetSurface);
			return Open(*StrongestMaterialAlongLine(here + Vector(0.0F, -10.0F), there + Vector(0.0F, -10.0F))) && Open(*StrongestMaterialAlongLine(here + Vector(0.0F, -18.0F), there + Vector(0.0F, -18.0F)));
		};
		node->StairsUpRight = stairsTo(node->Up ? node->Up->UpRight : nullptr);
		node->StairsUpLeft = stairsTo(node->Up ? node->Up->LeftUp : nullptr);
	}

	// Look at each existing adjacent node and calculate the cost for each. Start and end are offset to cover more terrain.
	// Note that we only calculate transitions to one side (down and right), because for the other side we can pull our up-and-left transition data from the other node's down-and-right.
	if (node->Right) {
		// Walking is sampled along the ground, when both cells have ground in them: a band just above the surface at each column, so a slope
		// or a bumpy floor is walked along the way a unit walks it. Nodes centred on a 24 px grid are anywhere from on the surface to buried
		// in it, and with the rays cast at the node centres the only way along a flat beam was to hop up a node and drop back at every
		// column, and a gentle slope was a wall. Cells without ground get a band just above the centre.
		// (The band sits a few pixels up: a pixel over the surface read every bump of a bushy hillside as something to push through.)
		// (Each end on its own: a node in the air beside a ledge goes from its centre to the ledge's surface, as the diagonals go to where a
		// body stands. Both at the centres, as they were unless both had ground, a ledge whose top was in the upper part of its cell put the
		// lower line's end inside the ledge, and a rung beside it had no step onto it: no route onto a plain ledge, depending on where the
		// ledge's top fell in its cell.)
		Vector upper(0.0F, -10.0F);
		Vector lower(0.0F, -4.0F);
		Vector here = node->Pos;
		Vector there = node->Right->Pos;
		float groundHere = SurfaceUnder(*node);
		float groundThere = SurfaceUnder(*node->Right);
		if (groundHere >= 0.0F) {
			here.m_Y = groundHere;
		}
		if (groundThere >= 0.0F) {
			there.m_Y = groundThere;
		}
		node->RightMaterial = getStrongerMaterial(StrongestMaterialAlongLine(here + upper, there + upper), StrongestMaterialAlongLine(here + lower, there + lower));
	}

	// Down, and the diagonals, go from this node's centre to where a body stands at the other node (see StandingPoint): a node whose
	// centre is a couple of pixels under the ground in its cell was a dead end upwards, with its own surface in the way of every line up
	// out of it, and the way up a steep slope was a two hundred pixel jet from a column further along it.
	if (node->Down) {
		// One line, down the column: the pair a few pixels either side read a slope's surface as a ceiling over the node under it, and
		// whether a body fits down a gap is RoomToPass's business now.
		// (The weakest of a few lines, the centre's and a few pixels either side: a body goes up or down beside a ladder's rungs, and a line
		// down the centre that clipped one costed the step at the rung's metal, a thousand times over, and the shaft was no way at all. A
		// floor fills the cell and stops every line, so a slab is still a slab; whether a body fits is RoomToPass's business, as before.)
		Vector top = StandingPoint(*node, 3.0F);
		Vector bottom = StandingPoint(*node->Down, 3.0F);
		const Material* weakest = nullptr;
		bool grounded = false;
		for (float dx: {0.0F, -4.0F, 4.0F, -8.0F, 8.0F}) {
			const Material* line = StrongestMaterialAlongLine(top + Vector(dx, 0.0F), bottom + Vector(dx, 0.0F));
			if (!weakest || line->GetIntegrity() < weakest->GetIntegrity()) {
				weakest = line;
			}
			grounded = grounded || line->GetIntegrity() > 0.0F;
		}
		node->DownMaterial = weakest;
		// Standing is another matter: a body stands on anything under its width, so the node stands on ground when any of the lines meets
		// some. (Taken from the weakest line, as the way down is, a node at a ledge's edge or on a narrow slope had a line that missed the
		// ground and was no place to stand; the sloped ledge into Bywater's west hatch cut a hundred routes the original pathfinder takes.)
		node->Grounded = grounded;
	}

	if (node->UpRight) {
		Vector offset(2.0F, 2.0F);
		Vector here = StandingPoint(*node, 5.0F);
		Vector there = StandingPoint(*node->UpRight, 5.0F);
		node->UpRightMaterial = getStrongerMaterial(StrongestMaterialAlongLine(here - offset, there - offset), StrongestMaterialAlongLine(here + offset, there + offset));
	}

	if (node->RightDown) {
		Vector offset(2.0F, -2.0F);
		Vector here = StandingPoint(*node, 5.0F);
		Vector there = StandingPoint(*node->RightDown, 5.0F);
		node->RightDownMaterial = getStrongerMaterial(StrongestMaterialAlongLine(here - offset, there - offset), StrongestMaterialAlongLine(here + offset, there + offset));
	}

	for (int i = 0; i < PathNode::c_MaxAdjacentNodeCount; ++i) {
		const Material* oldMat = oldMaterials[i];
		const Material* newMat = node->AdjacentNodeBlockingMaterials[i];

		// Check if the material strength is more than our delta, or if a door has appeared/disappeared (since we handle their costs in a special manner).
		float delta = std::abs(oldMat->GetIntegrity() - newMat->GetIntegrity());
		bool doorChanged = oldMat != newMat && (oldMat->GetIndex() == MaterialColorKeys::g_MaterialDoor || newMat->GetIndex() == MaterialColorKeys::g_MaterialDoor);
		if (delta > c_NodeCostChangeEpsilon || doorChanged) {
			return true;
		}
	}

	// Stairs appearing or going count as a change.
	if (node->StairsUpRight != oldStairsUpRight || node->StairsUpLeft != oldStairsUpLeft || node->Grounded != oldGrounded || node->StepOverRise != oldStepOverRise || node->StepOverRoom != oldStepOverRoom || node->StepOverRiseLeft != oldStepOverRiseLeft || node->StepOverRoomLeft != oldStepOverRoomLeft || node->Ladder != oldLadder) {
		return true;
	}

	// Room that has changed enough to matter to a body counts as a change too.
	if (std::abs(node->FreeHeight - oldFreeHeight) >= 4 || std::abs(node->ClearLeft - oldClearLeft) >= 3 || std::abs(node->ClearRight - oldClearRight) >= 3) {
		return true;
	}

	// None of the updates was past our epsilon, so ignore it and pretend it never happened.
	node->AdjacentNodeBlockingMaterials = oldMaterials;
	return false;
}

std::vector<int> PathFinder::GetNodeIdsInBox(Box box, bool samplingReach) {
	std::vector<int> result;

	box.Unflip();

	// Get the extents of the box's potential influence on PathNodes and their connecting edges.
	// With the sampling reach, also every node whose measures look into the box (see UpdateNodeCosts): the clearances and the step-overs
	// two nodes either side, and from below, the head room 96 px up and a step-over's room up to 48 px over its floor and 96 px more, six
	// rows. (Padded by one node, as it was when the grid only cast lines to its neighbours, a slab placed 60 px over a corridor left the
	// floor under it with full head room, and a wall built on a stair's landing left the stair leading into it.)
	const int padSide = samplingReach ? 1 + PathNode::c_SamplingReachSideNodes : 1;
	const int padBelow = samplingReach ? 1 + PathNode::c_SamplingReachUpNodes : 1;
	int firstX = static_cast<int>(std::floor((box.m_Corner.m_X / static_cast<float>(m_NodeDimension)) + 0.5F) - padSide);
	int lastX = static_cast<int>(std::floor(((box.m_Corner.m_X + box.m_Width) / static_cast<float>(m_NodeDimension)) + 0.5F) + padSide);
	int firstY = static_cast<int>(std::floor((box.m_Corner.m_Y / static_cast<float>(m_NodeDimension)) + 0.5F) - 1);
	int lastY = static_cast<int>(std::floor(((box.m_Corner.m_Y + box.m_Height) / static_cast<float>(m_NodeDimension)) + 0.5F) + padBelow);

	// Only iterate through the grid where the box overlaps any edges.
	for (int nodeX = firstX; nodeX <= lastX; ++nodeX) {
		for (int nodeY = firstY; nodeY <= lastY; ++nodeY) {
			int nodeId = ConvertCoordsToNodeId(nodeX, nodeY);
			if (nodeId != -1) {
				result.push_back(nodeId);
			}
		}
	}

	return result;
}

float PathFinder::GetNodeAverageTransitionCost(const PathNode& node) const {
	float totalCostOfAdjacentNodes = 0.0F;
	int count = 0;
	for (const Material* material: node.AdjacentNodeBlockingMaterials) {
		// Don't use node transition cost, because we don't care about digging.
		float cost = material->GetIntegrity();
		if (cost < std::numeric_limits<float>::max()) {
			totalCostOfAdjacentNodes += cost;
			count++;
		}
	}

	return totalCostOfAdjacentNodes / std::max(static_cast<float>(count), 1.0F);
}

bool PathFinder::UpdateNodeList(const std::vector<int>& nodeVec) {
	ZoneScoped;

	std::atomic<bool> anyChange = false;

	// Update all the costs going out from each node.
	std::for_each(
	    std::execution::par_unseq,
	    nodeVec.begin(),
	    nodeVec.end(),
	    [this, &anyChange](int nodeId) {
		    if (UpdateNodeCosts(&m_NodeGrid[nodeId])) {
			    anyChange = true;
		    }
	    });

	if (anyChange) {
		// UpdateNodeCosts only calculates Materials for Right and Down directions, so each PathNode's Up and Left direction Materials need to be matched to the respective neighbor's opposite direction Materials.
		// For example, this PathNode's Left Material is its Left neighbor's Right Material.
		std::for_each(
		    std::execution::par_unseq,
		    nodeVec.begin(),
		    nodeVec.end(),
		    [this](int nodeId) {
			    PathNode* node = &m_NodeGrid[nodeId];
			    if (node->Right) {
				    node->Right->LeftMaterial = node->RightMaterial;
			    }
			    if (node->Down) {
				    node->Down->UpMaterial = node->DownMaterial;
			    }
			    if (node->UpRight) {
				    node->UpRight->DownLeftMaterial = node->UpRightMaterial;
			    }
			    if (node->RightDown) {
				    node->RightDown->LeftUpMaterial = node->RightDownMaterial;
			    }
		    });
	}

	return anyChange;
}

void PathFinder::MarkBoxNavigable(Box box, bool navigable) {
	std::vector<int> pathNodesInBox = GetNodeIdsInBox(box);
	std::for_each(
	    std::execution::par_unseq,
	    pathNodesInBox.begin(),
	    pathNodesInBox.end(),
	    [this, navigable](int nodeId) {
		    PathNode* node = &m_NodeGrid[nodeId];
		    node->m_Navigable = navigable;
	    });
}

void PathFinder::MarkAllNodesNavigable(bool navigable) {
	std::vector<int> pathNodesIdsVec;
	pathNodesIdsVec.reserve(m_NodeGrid.size());
	for (size_t i = 0; i < m_NodeGrid.size(); ++i) {
		pathNodesIdsVec.push_back(i);
	}

	std::for_each(
	    std::execution::par_unseq,
	    pathNodesIdsVec.begin(),
	    pathNodesIdsVec.end(),
	    [this, navigable](int nodeId) {
		    PathNode* node = &m_NodeGrid[nodeId];
		    node->m_Navigable = navigable;
	    });
}

RTE::PathNode* PathFinder::GetPathNodeAtGridCoords(int x, int y) {
	int nodeId = ConvertCoordsToNodeId(x, y);
	return nodeId != -1 ? &m_NodeGrid[nodeId] : nullptr;
}

int PathFinder::ConvertCoordsToNodeId(int x, int y) const {
	if (m_WrapsX) {
		x = x % m_GridWidth;
		x = x < 0 ? x + m_GridWidth : x;
	}

	if (m_WrapsY) {
		y = y % m_GridHeight;
		y = y < 0 ? y + m_GridHeight : y;
	}

	if (x < 0 || x >= m_GridWidth || y < 0 || y >= m_GridHeight) {
		return -1;
	}

	return (y * m_GridWidth) + x;
}

void PathFinder::DebugRender(BITMAP* targetBitmap, const Vector& targetPos) const {
	for (int x = 0; x < m_GridWidth; ++x) {
		Vector startPos = (m_NodeGrid[ConvertCoordsToNodeId(x, 0)].Pos - m_Offset) - targetPos;
		Vector endPos = startPos + Vector(0.0F, m_NodeDimension * m_GridHeight);
		line(targetBitmap, startPos.GetX(), startPos.GetY(), endPos.GetX(), endPos.GetY(), g_BlackColor);
	}

	for (int y = 0; y < m_GridHeight; ++y) {
		Vector startPos = (m_NodeGrid[ConvertCoordsToNodeId(0, y)].Pos - m_Offset) - targetPos;
		Vector endPos = startPos + Vector(m_NodeDimension * m_GridWidth, 0.0F);
		line(targetBitmap, startPos.GetX(), startPos.GetY(), endPos.GetX(), endPos.GetY(), g_BlackColor);
	}
}
