#pragma once

#include "Box.h"
#include "System/MicroPather/micropather.h"

#include <array>
#include <atomic>
#include <list>
#include <memory>
#include <functional>
#include <vector>

using namespace micropather;

namespace RTE {

	class Scene;
	class Material;

	/// What a step along a path is: the kind of move the pathfinder meant by it, so whoever follows the path needn't guess from the geometry.
	enum class PathStepKind {
		Walk = 0, //!< Along the ground (a slope included).
		Crawl, //!< Along the ground with too little head room to stand.
		Jump, //!< Up, by jetpack or legs: the step's height says how far.
		Fall, //!< Down, off an edge.
		Dig, //!< Through ground the searcher can dig.
		Door, //!< Through a door the searcher can open or breach.
		Stairs //!< Up or down stairs, or a slope of about sixty degrees, on the legs: two nodes of height for one of width.
	};

	/// The searcher, as far as the path grid cares: what it can jump, dig and breach, and how big it is.
	struct PathAgent {
		float JumpHeight = FLT_MAX; //!< How high it can get on a jump or a tank of jet fuel, in metres. FLT_MAX for anything that flies.
		float DigStrength = 35.0F; //!< The strongest material it can dig through (c_PathFindingDefaultDigStrength).
		float BreachStrength = -1.0F; //!< The strongest door it can get through; -1 for the dig strength.
		float StandHeight = 40.0F; //!< Head room it needs to walk upright, in pixels.
		float CrawlHeight = 22.0F; //!< Head room it needs to crawl; the same as StandHeight for something that can't.
		float HalfWidth = 6.0F; //!< Half its width, in pixels: room it needs either side to pass or to jump up through.
		std::vector<Vector> Avoid; //!< Places this unit has failed a jump at lately: routes through them cost more (see PathFinder::AvoidCost).
		bool WalksStairs = false; //!< Whether its legs take stairs and slopes of about sixty degrees (a soldier walks the base game's steep stairs unaided; nothing is known of a crab's).
	};

	/// Information required to make an async pathing request.
	struct PathRequest {
		bool complete = false;
		int status = MicroPather::NO_SOLUTION;
		std::list<Vector> path;
		std::list<PathStepKind> kinds; //!< What each step of the path is, one per point of path after the first.
		float pathLength = 0.0f;
		float totalCost = 0.0f;
		bool cutAtDoor = false; //!< Whether the route was cut short at a door the searcher can't get through (yet): a door opens, or is shot open, so this is no dead end.
		Vector startPos;
		Vector targetPos;
	};

	using PathCompleteCallback = std::function<void(std::shared_ptr<volatile PathRequest>)>;

	/// Contains everything related to a PathNode on the path grid used by PathFinder.
	struct PathNode {

		static constexpr int c_MaxAdjacentNodeCount = 8; //!< The maximum number of adjacent PathNodes to any given PathNode. Thusly, also the number of directions for PathNodes to be in.

		Vector Pos; //!< Absolute position of the center of this PathNode in the scene.

		bool m_Navigable; //!< Whether this node can be navigated through.

		float Surface = -1.0F; //!< The ground surface in this node's column within its cell, or -1 for none (the node is in the air, or buried).
		int FreeHeight = 0; //!< Air above the surface (or above the centre, for a node in the air) in the node's column, up to c_ClearanceReach.
		int ClearLeft = 0; //!< Air to the left of the centre (a little over the surface, for a node on the ground), up to a node's width.
		int ClearRight = 0; //!< Air to the right, likewise.
		bool StairsUpRight = false; //!< Whether stairs, or a slope of about sixty degrees, lead from this node's floor up to the floor of the node two up and one to the right (see UpdateNodeCosts).
		bool Grounded = false; //!< Whether a body here stands on something: any of the lines down across the cell meets ground (see UpdateNodeCosts).
		bool StairsUpLeft = false; //!< Likewise up to the left.
		static constexpr int c_ClearanceReach = 96; //!< How far up the free height is measured.

		/// Pointers to all adjacent PathNodes, in clockwise order with top first. These are not owned, and may be 0 if adjacent to non-wrapping scene border.
		std::array<PathNode*, c_MaxAdjacentNodeCount> AdjacentNodes;
		PathNode*& Up = AdjacentNodes[0];
		PathNode*& UpRight = AdjacentNodes[1];
		PathNode*& Right = AdjacentNodes[2];
		PathNode*& RightDown = AdjacentNodes[3];
		PathNode*& Down = AdjacentNodes[4];
		PathNode*& DownLeft = AdjacentNodes[5];
		PathNode*& Left = AdjacentNodes[6];
		PathNode*& LeftUp = AdjacentNodes[7];

		/// The strongest material between us and our adjacent PathNodes, in clockwise order with top first.
		std::array<const Material*, c_MaxAdjacentNodeCount> AdjacentNodeBlockingMaterials;
		const Material*& UpMaterial = AdjacentNodeBlockingMaterials[0];
		const Material*& UpRightMaterial = AdjacentNodeBlockingMaterials[1];
		const Material*& RightMaterial = AdjacentNodeBlockingMaterials[2];
		const Material*& RightDownMaterial = AdjacentNodeBlockingMaterials[3];
		const Material*& DownMaterial = AdjacentNodeBlockingMaterials[4];
		const Material*& DownLeftMaterial = AdjacentNodeBlockingMaterials[5];
		const Material*& LeftMaterial = AdjacentNodeBlockingMaterials[6];
		const Material*& LeftUpMaterial = AdjacentNodeBlockingMaterials[7];

		/// Constructor method used to instantiate a PathNode object in system memory and make it ready for use.
		/// @param pos Absolute position of the center of the PathNode in the scene.
		explicit PathNode(const Vector& pos);
	};

	/// A class encapsulating and implementing the MicroPather A* pathfinding library.
	class PathFinder : public Graph {

	public:
#pragma region Creation
		/// Constructor method used to instantiate a PathFinder object.
		/// @param nodeDimension The width and height in scene pixels that of each PathNode should represent.
		/// @param allocate The block size that the PathNode cache is allocated from. Should be about a fourth of the total number of PathNodes.
		PathFinder(int nodeDimension);

		/// Makes the PathFinder object ready for use.
		/// @param nodeDimension The width and height in scene pixels that of each PathNode should represent.
		/// @return An error return value signaling success or any particular failure. Anything below 0 is an error signal.
		int Create(int nodeDimension);
#pragma endregion

#pragma region Destruction
		/// Destructor method used to clean up a PathFinder object before deletion.
		~PathFinder() override;

		/// Destroys and resets (through Clear()) this PathFinder object.
		void Destroy();

		/// Resets the entire PathFinder object to the default settings or values.
		void Reset() { Clear(); }
#pragma endregion

#pragma region PathFinding
		/// Calculates and returns the least difficult path between two points on the current scene.
		/// This is synchronous, and will block the current thread!
		/// @param start Start positions on the scene to find the path between.
		/// @param end End positions on the scene to find the path between.
		/// @param pathResult A list which will be filled out with waypoints between the start and end.
		/// @param totalCostResult The total minimum difficulty cost calculated between the two points on the scene.
		/// @param jumpHeight How high, in metres, the search can jump vertically.
		/// @param digStrength What material strength the search is capable of digging through.
		/// @return Success or failure, expressed as SOLVED, NO_SOLUTION, or START_END_SAME.
		int CalculatePath(Vector start, Vector end, std::list<Vector>& pathResult, float& totalCostResult, float jumpHeight, float digStrength, float breachStrength = -1.0F);

		/// Calculates a path for a given searcher, with what each step of it is.
		/// @param agent What the searcher can do and how big it is.
		/// @param kinds Where to put what each step is, one per point of the path after the first; may be nullptr.
		int CalculatePath(Vector start, Vector end, std::list<Vector>& pathResult, float& totalCostResult, const PathAgent& agent, std::list<PathStepKind>* kinds);

		/// Calculates and returns the least difficult path between two points on the current scene.
		/// This is asynchronous and thus will not block the current thread.
		/// @param start Start positions on the scene to find the path between.
		/// @param end End positions on the scene to find the path between.
		/// @param jumpHeight How high, in metres, the search can jump vertically.
		/// @param digStrength What material strength the search is capable of digging through.
		/// @param callback The callback function to be run when the path calculation is completed.
		/// @return A shared pointer to the volatile PathRequest to be used to track whether the asynchronous path calculation has been completed, and check its results.
		std::shared_ptr<volatile PathRequest> CalculatePathAsync(Vector start, Vector end, float jumpHeight, float digStrength, PathCompleteCallback callback = nullptr, float breachStrength = -1.0F);

		/// Asynchronously calculates a path for a given searcher; the request carries what each step is.
		std::shared_ptr<volatile PathRequest> CalculatePathAsync(Vector start, Vector end, const PathAgent& agent, PathCompleteCallback callback = nullptr);

		// <summary>
		/// Returns how many pathfinding requests are currently active.
		/// @return How many pathfinding requests are currently active.
		int GetCurrentPathingRequests() const { return m_CurrentPathingRequests.load(); }

		/// Debug: what the grid makes of the node at a scene point, as a line of text (its surface, ground, room and the material each way).
		std::string DescribeNodeAt(const Vector& scenePos);

		/// Recalculates all the costs between all the PathNodes by tracing lines in the material layer and summing all the material strengths for each encountered pixel. Also resets the pather itself.
		void RecalculateAllCosts();

		/// Recalculates the costs between all the PathNodes touching a deque of specific rectangular areas (which will be wrapped). Also resets the pather itself, if necessary.
		/// @param boxList The deque of Boxes representing the updated areas.
		/// @param nodeUpdateLimit The maximum number of PathNodes we'll try to update this frame. True PathNode update count can be higher if we received a big box, as we always do at least 1 box.
		/// @return The set of PathNode ids that were updated.
		std::vector<int> RecalculateAreaCosts(std::deque<Box>& boxList, size_t nodeUpdateLimit);

		/// Updates a set of PathNodes, adjusting their transitions.
		/// This does NOT update the pather, which is required if PathNode costs changed.
		/// @param nodeVec The set of PathNode IDs to update.
		/// @return Whether any PathNode costs changed.
		bool UpdateNodeList(const std::vector<int>& nodeVec);

		/// Implementation of the abstract interface of Graph.
		/// Gets the least possible cost to get from PathNode A to B, if it all was air.
		/// @param startState Pointer to PathNode to start from. OWNERSHIP IS NOT TRANSFERRED!
		/// @param endState PathNode to end up at. OWNERSHIP IS NOT TRANSFERRED!
		/// @return The cost of the absolutely fastest possible way between the two points, as if traveled through air all the way.
		float LeastCostEstimate(void* startState, void* endState) override;

		/// Implementation of the abstract interface of Graph.
		/// Gets the cost to go to any adjacent PathNode of the one passed in.
		/// @param state Pointer to PathNode to get to cost of all adjacents for. OWNERSHIP IS NOT TRANSFERRED!
		/// @param adjacentList An empty vector which will be filled out with all the valid PathNodes adjacent to the one passed in. If at non-wrapping edge of seam, those non existent PathNodes won't be added.
		void AdjacentCost(void* state, std::vector<micropather::StateCost>* adjacentList) override;

		/// Returns whether two position represent the same path nodes.
		/// @param pos1 First coordinates to compare.
		/// @param pos2 Second coordinates to compare.
		/// @return Whether both coordinates represent the same path node.
		bool PositionsAreTheSamePathNode(const Vector& pos1, const Vector& pos2) const;

		/// Marks a box as being navigable or not.
		/// @param box The Box of which all PathNodes that should have their navigable status changed.
		/// @param navigable Whether or not the nodes in this box should be navigable.
		void MarkBoxNavigable(Box box, bool navigable);

		/// Marks a box as being navigable or not.
		/// @param box The Box of which all PathNodes that should have their navigable status changed.
		/// @param navigable Whether or not the nodes in this box should be navigable.
		void MarkAllNodesNavigable(bool navigable);
#pragma endregion

#pragma region Misc
		/// Implementation of the abstract interface of Graph. This function is only used in DEBUG mode - it dumps output to stdout.
		/// Since void* aren't really human readable, this will print out some concise info without an ending newline.
		/// @param state The state to print out info about.
		void PrintStateInfo(void* state) override {}

		/// Draws a debug rendering for this pathfinder to a BITMAP of choice.
		/// @param targetBitmap A pointer to a BITMAP to draw on.
		/// @param targetPos The offset into the scene where the target bitmap's upper left corner is located.
		void DebugRender(BITMAP* targetBitmap, const Vector& targetPos = Vector()) const;
#pragma endregion

	private:
		static constexpr int c_SafeFallNodes = 4; //!< A drop of this many nodes (a storey, 96 px) costs nothing extra: it is what a hatch is.
		static constexpr int c_FallCostReach = 24; //!< How far down a fall is measured for its cost; past this it is as dear as it gets.
		static constexpr float c_FallCostPerNode = 2.5F; //!< The extra cost of each node of a fall that is still more than the safe drop above the ground: about what a rung of a jump costs, since the jetpack brakes the fall with fuel at the bottom.
		static constexpr float c_NodeCostChangeEpsilon = 5.0F; //!< The minimum change in a PathNodes's cost for the pathfinder to recognize a change and reset itself. This is so minor changes (e.g. blood particles) don't force constant pathfinder resets.

		MicroPather* m_Pather; //!< The actual pathing object that does the pathfinding work. Owned.
		std::vector<PathNode> m_NodeGrid; //!< The array of PathNodes representing the grid on the scene.
		unsigned int m_NodeDimension; //!< The width and height of each PathNode, in pixels on the scene.
		Vector m_Offset;
		int m_GridWidth; //!< The width of the pathing grid, in PathNodes.
		int m_GridHeight; //!< The height of the pathing grid, in PathNodes.
		bool m_WrapsX; //!< Whether the pathing grid wraps on the X axis.
		bool m_WrapsY; //!< Whether the pathing grid wraps on the Y axis.
		std::atomic<int> m_CurrentPathingRequests; //!< The number of active async pathing requests.

		/// Gets the pather for this thread. Lazily-initialized for each new thread that needs a pather.
		/// @return The pather for this thread.
		MicroPather* GetPather();

#pragma region Path Cost Updates
		/// Helper function for getting the strongest material we need to path though between PathNodes.
		/// @param start Origin point.
		/// @param end Destination point.
		/// @return The strongest material.
		const Material* StrongestMaterialAlongLine(const Vector& start, const Vector& end) const;

		/// Helper function for updating all the values of cost edges going out from a specific PathNodes.
		/// This does NOT update the pather, which is required before solving more paths after calling this.
		/// @param node The PathNode to update all costs of. It's safe to pass nullptr here. OWNERSHIP IS NOT TRANSFERRED!
		/// @return Whether the PathNodes costs changed.
		bool UpdateNodeCosts(PathNode* node) const;

		/// Helper function for getting the PathNode ids in a Box.
		/// @param box The Box of which all PathNodes it touches should be returned.
		/// @return A list of the PathNode ids inside the box.
		std::vector<int> GetNodeIdsInBox(Box box);

		/// Helper function to determine if a node is on solid fround.
		/// @param node The node we're checking.
		/// @return Whether the node is on solid ground.
		bool NodeIsOnSolidGround(const PathNode& node) const;

		/// Whether a material along an edge is as good as air (nothing to dig), so the room at the nodes is what matters.
		bool Open(const Material& material) const;

		/// How many nodes a body falls from a node before it stands on something, up to c_FallCostReach.
		int DropNodes(const PathNode& node) const;

		/// What a step into a node costs for the fall it is part of: nothing within a storey of the ground, a rung's worth higher up.
		float FallCost(const PathNode& to) const;

		/// Whether the searcher's body fits through a node, by the room to either side of it.
		/// Whether a body of the searcher's width fits through this node sideways: the air either side of it over its floor adds up to
		/// the width wanted. @param widths How many body widths of room: two for a fall or a walk, three for a jet column (a jet in a
		/// shaft a body wide only bounces off the walls).
		bool RoomToPass(const PathNode& node, float widths = 2.0F) const;

		/// Whether a node has a floor within reach under it (its own surface, or ground just below): its head room is then a body's.
		bool HasFloor(const PathNode& node) const;

		/// The cost factor for walking between two nodes by the head room: 1 standing, 2 crawling, 1000 for no way through.
		float HeadRoomFactor(const PathNode& from, const PathNode& to) const;

		/// What a step from one node to the next is.
		PathStepKind StepKindBetween(const PathNode* from, const PathNode* to) const;

		/// The ground surface within a node's cell: the first solid pixel from the top of the cell down the node's column.
		/// @param node The node.
		/// @return The y of the surface, or -1 when the cell's top is already solid (the node is buried) or there is no ground in the cell.
		float SurfaceUnder(const PathNode& node) const;

		/// Where a body at this node stands: a little over the ground in its cell, when there is ground in it, or else the node's centre.
		/// Steps to and from a node are sampled between standing points, not centres: a centre is anywhere from above the surface to buried
		/// in it, and from a centre two pixels under a slope every way up but back down the slope read as through ground.
		Vector StandingPoint(const PathNode& node, float lift) const;


		/// Gets the cost for transitioning through this Material.
		/// @param material The Material to get the transition cost for.
		/// @return The transition cost for the Material.
		float GetMaterialTransitionCost(const Material& material) const;

		/// What a rung of a jetpack climb up a column costs over its height, for how hard it is to fly: hugging a wall with open air on the
		/// other side, or a gap barely wider than the body. A shaft, tight both sides, costs nothing extra.
		/// @param node The node the rung rises into.
		float ClimbMarginCost(const PathNode& node) const;

		/// What a step into a node costs over its own for the searcher's recent failures there (PathAgent::Avoid).
		float AvoidCost(const PathNode& node) const;

		/// Gets the average cost for all transitions out of this PathNode, ignoring infinities/unpathable transitions.
		/// @param node The PathNode to get the average transition cost for.
		/// @return The average transition cost.
		float GetNodeAverageTransitionCost(const PathNode& node) const;
#pragma endregion

		/// Gets the PathNode at the given coordinates.
		/// @param x The X coordinate, in PathNodes.
		/// @param y The Y coordinate, in PathNodes.
		/// @return The PathNode at the given coordinates.
		PathNode* GetPathNodeAtGridCoords(int x, int y);

		/// Gets the PathNode id at the given coordinates.
		/// @param x The X coordinate, in PathNodes.
		/// @param y The Y coordinate, in PathNodes.
		/// @return The PathNode id at the given coordinates.
		int ConvertCoordsToNodeId(int x, int y) const;

		/// Clears all the member variables of this PathFinder, effectively resetting the members of this abstraction level only.
		void Clear();

		// Disallow the use of some implicit methods.
		PathFinder(const PathFinder& reference) = delete;
		PathFinder& operator=(const PathFinder& rhs) = delete;
	};
} // namespace RTE
