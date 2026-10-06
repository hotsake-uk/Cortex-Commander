#include "PathFinder.h"
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

RTE::PathNode::PathNode(const Vector& pos) :
    Pos(pos) {
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
	float breachStrength = agent.BreachStrength;
	s_StandHeight = agent.StandHeight;
	s_CrawlHeight = agent.CrawlHeight;
	s_HalfWidth = agent.HalfWidth;

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

	// Actors capable of jumping/jetpacking can jump upwards.
	s_JumpHeight = jumpHeight;

	// How high up we can jump from this node.
	if(jumpHeight == FLT_MAX) {
		// Probably quite high.
		s_JumpHeightVertical = INT_MAX;
		s_JumpHeightDiagonal = INT_MAX;
	} else {
		// Assume at least 1 so automovers work a bit better
		s_JumpHeightVertical = std::max(1, static_cast<int>(jumpHeight / (m_NodeDimension * c_MPP)));
		s_JumpHeightDiagonal = std::max(1, static_cast<int>((jumpHeight * 0.7F) / (m_NodeDimension * c_MPP)));
	}

	// Actors capable of digging can use s_DigStrength to modify the node adjacency cost.
	s_DigStrength = digStrength;
	s_BreachStrength = breachStrength < 0.0F ? digStrength : breachStrength;

	// Do the actual pathfinding, fetch out the list of states that comprise the best path.
	int result = MicroPather::NO_SOLUTION;
	std::vector<void*> statePath;

	// A node whose centre is in solid ground is no place to start or finish: the one above (the surface node) is, when it's open, or else
	// the one below. A goal a little above the ground falls in the cell over the surface's: that is the node a unit stands at, so the
	// search ends there.
	auto openNode = [this](PathNode* node) -> PathNode* {
		auto buried = [](const PathNode* n) { return n && g_SceneMan.GetTerrMatter(static_cast<int>(n->Pos.m_X), static_cast<int>(n->Pos.m_Y)) != MaterialColorKeys::g_MaterialAir; };
		if (buried(node)) {
			if (node->Up && !buried(node->Up)) {
				return node->Up;
			}
			if (node->Down && !buried(node->Down)) {
				return node->Down;
			}
		}
		return node;
	};
	PathNode* startNode = openNode(GetPathNodeAtGridCoords(startNodeX, startNodeY));
	PathNode* endNode = openNode(GetPathNodeAtGridCoords(endNodeX, endNodeY));
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
				if (!alternative || !alternative->m_Navigable || g_SceneMan.GetTerrMatter(static_cast<int>(alternative->Pos.m_X), static_cast<int>(alternative->Pos.m_Y)) != MaterialColorKeys::g_MaterialAir) {
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
				statePath.resize(std::max<size_t>(2, i + 1));
				break;
			}
		}
	}

	if (std::getenv("CCCP_PATH_LOG")) {
		// Debug: the cost of each step along the found path, so the grid's view of the terrain can be checked against the scene.
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

	if (!statePath.empty()) {
		// The points of the path and what each step to them is, from the nodes they go between. The approximate first point is the exact
		// start and the last the exact end.
		struct Step {
			Vector Pos;
			PathStepKind Kind;
		};
		std::vector<Step> steps;
		steps.push_back({start, PathStepKind::Walk});
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
				Vector apex(from->Pos.m_X, apexY);
				g_SceneMan.ForceBounds(apex);
				// Only when it is above where the body stands on the landing (its centre about 0.45 of a standing height over the floor):
				// lower than that it is no top at all. (Measured against the node's centre it was skipped whenever the centre sat high in
				// its cell, and the climb was flown as the one straight line again.)
				float standingY = landingFloor - s_StandHeight * 0.45F;
				if (apexY < standingY + 1.0F && g_SceneMan.GetTerrMatter(static_cast<int>(apex.m_X), static_cast<int>(apex.m_Y)) == MaterialColorKeys::g_MaterialAir) {
					steps.push_back({apex, PathStepKind::Jump});
				}
			}
			steps.push_back({to->Pos, kind});
		}
		steps.back().Pos = end;

		// Fewer points along a straight: a walk is one waypoint per node, and along a beam forty of them were each "arrived at" in turn,
		// with the checks that go with it. Walks that keep heading the same way on much the same level are run together, up to a few
		// nodes at a time so the movement script's look at the next waypoint still looks a sensible way ahead. Nothing else is touched:
		// a crawl, a jump, a fall, a dig and a door each want their own point.
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
		std::vector<int> nodesInside = GetNodeIdsInBox(boxList.front());
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
	return g_SceneMan.ShortestDistance(startNode->Pos, endNode->Pos).GetMagnitude() / m_NodeDimension;
}

void PathFinder::AdjacentCost(void* state, std::vector<micropather::StateCost>* adjacentList) {
	const PathNode* node = static_cast<PathNode*>(state);
	micropather::StateCost adjCost;

	// We do a little trick here, where we radiate out a little percentage of our average cost in all directions.
	// This encourages the AI to generally try to give hard surfaces some berth when pathing, so we don't get too close and get stuck.
	const float costRadiationMultiplier = 0.2F;
	float radiatedCost = 0.0F; // GetNodeAverageTransitionCost(*node) * costRadiationMultiplier;

	bool isInNoGrav = g_SceneMan.IsPointInNoGravArea(node->Pos);
	bool allowDiagonal = !isInNoGrav; // We don't allow diagonals in nograv to improve automover behaviour

	// A fall is paid for by the node: every step into a node that is still high above the ground (FallCost) costs a little more, so a
	// fall costs by its height, however it began. Falling cost nothing however far, so a route that left a bunker by an opening, dropped
	// four hundred pixels down its outside wall and came back in at the bottom beat the hatches inside; the jetpack then paid for the
	// fall in fuel, braking at the bottom, or the body did. (Charged once at the step off something standing, the search hopped up a
	// rung or two of a jump and stepped off those into the drop for nothing: a search with no memory can't tell a rung from a fall.)

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
			adjCost.cost = (1.0F + GetMaterialTransitionCost(*node->LeftMaterial) + radiatedCost) * (Open(*node->LeftMaterial) ? HeadRoomFactor(*node, *node->Left) : 1.0F) + FallCost(*node->Left);
			adjCost.state = static_cast<void*>(node->Left);
			adjacentList->push_back(adjCost);
		}

		if (node->Right && node->Right->m_Navigable) {
			adjCost.cost = (1.0F + GetMaterialTransitionCost(*node->RightMaterial) + radiatedCost) * (Open(*node->RightMaterial) ? HeadRoomFactor(*node, *node->Right) : 1.0F) + FallCost(*node->Right);
			adjCost.state = static_cast<void*>(node->Right);
			adjacentList->push_back(adjCost);
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

				totalMaterialCost += 1.0F + extraUpCost + extraJumpCost + (GetMaterialTransitionCost(*currentNode->UpMaterial) * 3.0F) + radiatedCost;

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
							adjCost.cost = totalMaterialCost + stepCost + GetMaterialTransitionCost(*stepMaterial) + radiatedCost;
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
			adjCost.cost = (1.4F + (extraUpCost * 1.4F) + (GetMaterialTransitionCost(*node->UpRightMaterial) * 1.4F * 3.0F) + radiatedCost) * (Open(*node->UpRightMaterial) ? HeadRoomFactor(*node, *node->UpRight) : 1.0F) + FallCost(*node->UpRight); // Three times more expensive when digging.
			adjCost.state = static_cast<void*>(node->UpRight);
			adjacentList->push_back(adjCost);
		}

		if (node->LeftUp && node->LeftUp->m_Navigable && allowDiagonal) {
			adjCost.cost = (1.4F + (extraUpCost * 1.4F) + (GetMaterialTransitionCost(*node->LeftUpMaterial) * 1.4F * 3.0F) + radiatedCost) * (Open(*node->LeftUpMaterial) ? HeadRoomFactor(*node, *node->LeftUp) : 1.0F) + FallCost(*node->LeftUp); // Three times more expensive when digging.
			adjCost.state = static_cast<void*>(node->LeftUp);
			adjacentList->push_back(adjCost);
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
	if (g_SceneMan.GetTerrMatter(x, top) != MaterialColorKeys::g_MaterialAir) {
		return -1.0F;
	}
	for (int y = top + 1; y <= top + m_NodeDimension; ++y) {
		if (g_SceneMan.GetTerrMatter(x, y) != MaterialColorKeys::g_MaterialAir) {
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
	return s_JumpHeight == FLT_MAX || (node.Down && node.DownMaterial->GetIntegrity() > 0.0F);
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

float PathFinder::HeadRoomFactor(const PathNode& from, const PathNode& to) const {
	if (s_JumpHeight == FLT_MAX) {
		return 1.0F;
	}
	int headRoom = std::min(from.FreeHeight, to.FreeHeight);
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
	// Something solid on the straight line between the two: a dig if this searcher digs that, and otherwise the step wasn't along that
	// line at all but up the column and over onto a ledge (the landing edges), which is a jump. (Read as a dig, a step up onto a 24 px
	// ledge whose corner the line clipped was neither hopped nor climbed by a unit with no digger, and it stood at the step for ever.)
	if (material && material->GetIntegrity() > c_PathFindingDefaultDigStrength && std::abs(dy) <= nodeSize && std::abs(dx) <= nodeSize) {
		if (material->GetIntegrity() <= s_DigStrength) {
			return PathStepKind::Dig;
		}
		return dy < -1.0F ? PathStepKind::Jump : PathStepKind::Walk;
	}
	if (dy < -1.0F) {
		return PathStepKind::Jump;
	}
	if (dy > nodeSize + 1.0F || (dy > 1.0F && std::abs(dx) < 1.0F)) {
		return PathStepKind::Fall;
	}
	if (static_cast<float>(std::min(from->FreeHeight, to->FreeHeight)) < s_StandHeight) {
		return PathStepKind::Crawl;
	}
	return PathStepKind::Walk;
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
	return g_SceneMan.CastMaxStrengthRayMaterial(start, end, 0, MaterialColorKeys::g_MaterialAir);
}

bool PathFinder::UpdateNodeCosts(PathNode* node) const {
	if (!node) {
		return false;
	}

	std::array<const Material*, PathNode::c_MaxAdjacentNodeCount> oldMaterials = node->AdjacentNodeBlockingMaterials;
	int oldFreeHeight = node->FreeHeight;
	int oldClearLeft = node->ClearLeft;
	int oldClearRight = node->ClearRight;

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
		} else if (g_SceneMan.GetTerrMatter(x, centreY) == MaterialColorKeys::g_MaterialAir) {
			for (int y = centreY + 1; y <= centreY + m_NodeDimension; ++y) {
				if (g_SceneMan.GetTerrMatter(x, y) != MaterialColorKeys::g_MaterialAir) {
					floor = y;
					break;
				}
			}
		} else {
			floor = centreY; // Buried: no room at all.
		}
		int from = floor >= 0 ? floor - 1 : centreY;
		int free = 0;
		while (free < PathNode::c_ClearanceReach && g_SceneMan.GetTerrMatter(x, from - free) == MaterialColorKeys::g_MaterialAir) {
			++free;
		}
		node->FreeHeight = free;
		int sideY = floor >= 0 ? floor - 10 : centreY;
		// (Out to two nodes a side: measured to one, a shaft two nodes wide read as narrower than it was from either of its columns, since
		// neither is in the middle, and a jet column was refused where a body would have fitted twice over.)
		int left = 0;
		while (left < m_NodeDimension * 2 && g_SceneMan.GetTerrMatter(x - 1 - left, sideY) == MaterialColorKeys::g_MaterialAir) {
			++left;
		}
		int right = 0;
		while (right < m_NodeDimension * 2 && g_SceneMan.GetTerrMatter(x + 1 + right, sideY) == MaterialColorKeys::g_MaterialAir) {
			++right;
		}
		node->ClearLeft = left;
		node->ClearRight = right;
	}

	// Look at each existing adjacent node and calculate the cost for each. Start and end are offset to cover more terrain.
	// Note that we only calculate transitions to one side (down and right), because for the other side we can pull our up-and-left transition data from the other node's down-and-right.
	if (node->Right) {
		// Walking is sampled along the ground, when both cells have ground in them: a band just above the surface at each column, so a slope
		// or a bumpy floor is walked along the way a unit walks it. Nodes centred on a 24 px grid are anywhere from on the surface to buried
		// in it, and with the rays cast at the node centres the only way along a flat beam was to hop up a node and drop back at every
		// column, and a gentle slope was a wall. Cells without ground get a band just above the centre.
		// (The band sits a few pixels up: a pixel over the surface read every bump of a bushy hillside as something to push through.)
		Vector upper(0.0F, -10.0F);
		Vector lower(0.0F, -4.0F);
		Vector here = node->Pos;
		Vector there = node->Right->Pos;
		float groundHere = SurfaceUnder(*node);
		float groundThere = SurfaceUnder(*node->Right);
		if (groundHere >= 0.0F && groundThere >= 0.0F) {
			here.m_Y = groundHere;
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
		node->DownMaterial = StrongestMaterialAlongLine(StandingPoint(*node, 3.0F), StandingPoint(*node->Down, 3.0F));
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

	// Room that has changed enough to matter to a body counts as a change too.
	if (std::abs(node->FreeHeight - oldFreeHeight) >= 4 || std::abs(node->ClearLeft - oldClearLeft) >= 3 || std::abs(node->ClearRight - oldClearRight) >= 3) {
		return true;
	}

	// None of the updates was past our epsilon, so ignore it and pretend it never happened.
	node->AdjacentNodeBlockingMaterials = oldMaterials;
	return false;
}

std::vector<int> PathFinder::GetNodeIdsInBox(Box box) {
	std::vector<int> result;

	box.Unflip();

	// Get the extents of the box's potential influence on PathNodes and their connecting edges.
	int firstX = static_cast<int>(std::floor((box.m_Corner.m_X / static_cast<float>(m_NodeDimension)) + 0.5F) - 1);
	int lastX = static_cast<int>(std::floor(((box.m_Corner.m_X + box.m_Width) / static_cast<float>(m_NodeDimension)) + 0.5F) + 1);
	int firstY = static_cast<int>(std::floor((box.m_Corner.m_Y / static_cast<float>(m_NodeDimension)) + 0.5F) - 1);
	int lastY = static_cast<int>(std::floor(((box.m_Corner.m_Y + box.m_Height) / static_cast<float>(m_NodeDimension)) + 0.5F) + 1);

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
