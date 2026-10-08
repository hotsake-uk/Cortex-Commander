#pragma once

#include <mutex>

#include "Box.h"
#include "System/MicroPather/micropather.h"

#include <array>
#include <atomic>
#include <limits>
#include <deque>
#include <list>
#include <memory>
#include <functional>
#include <vector>
#include <unordered_map>

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
		Stairs, //!< Up or down stairs, or a slope of about sixty degrees, on the legs: two nodes of height for one of width.
		Ladder, //!< Up or down a ladder, hand over hand (see AHuman::UpdateLadder), or off its top or side onto a floor.
		Leap, //!< Across a gap or up onto a low ledge on a leap of the legs (see AHuman::UpdateLeap): no jet.
		Mantle, //!< Up onto a ledge one or two nodes up and one across, pulled up onto by pressing into it (see Actor::TryStartMantle): no jet.
		Crouch, //!< Along the ground with room to walk crouched but not upright (PathAgent::CrouchHeight): walked ducking, not crawled.
		Scramble, //!< Up a rough slope of about seventy degrees on legs and arms, crouched: three nodes of height for one of width (see UpdateNodeCosts).
		Swim, //!< Along the surface of liquid too deep to wade, swum by a searcher that floats (PathAgent::Floats).
		Wade //!< Through liquid, walked: shallow enough to wade, or along the bottom of deep water for a searcher that sinks.
	};

	/// What liquid fills a node's column under its surface (see PathNode::Liquid). Told by the material's name, the four FluidSim pours.
	enum class PathLiquid : unsigned char {
		None = 0,
		Water, //!< Swum or waded; drowns what breathes with its head under for long.
		Oil, //!< Waded: nothing swims or drowns in it (ActorWater leaves it alone).
		Acid, //!< Eats anything in it (ActorWater): never routed through but as a last resort.
		Lava //!< Sets flesh alight (ActorFire): routed through only by what doesn't burn (PathAgent::CrossesLava).
	};

	/// The searcher, as far as the path grid cares: what it can jump, dig and breach, and how big it is.
	struct PathAgent {
		float JumpHeight = FLT_MAX; //!< How high it can get on a jump or a tank of jet fuel, in metres. FLT_MAX for anything that flies.
		float DigStrength = 35.0F; //!< The strongest material it can dig through (c_PathFindingDefaultDigStrength).
		float BreachStrength = -1.0F; //!< The strongest door it can get through; -1 for the dig strength.
		float StandHeight = 40.0F; //!< Head room it needs to walk upright, in pixels.
		float CrawlHeight = 22.0F; //!< Head room it needs to crawl; the same as StandHeight for something that can't.
		float CrouchHeight = 0.0F; //!< Head room it needs to walk crouched (see AHuman::GetCrouchHeight); 0 for something that can't, which crawls under anything lower than it stands.
		float HalfWidth = 6.0F; //!< Half its width, in pixels: room it needs either side to pass or to jump up through.
		std::vector<Vector> Avoid; //!< Places this unit has failed a jump at lately: routes through them cost more (see PathFinder::AvoidCost).
		std::vector<std::pair<Vector, Vector>> AvoidLinks; //!< Flights (take-off, landing) this unit or its team has failed lately: that take-off for that landing costs more, nothing else does.
		float MantleHeight = 0.0F; //!< How high a ledge it pulls itself up onto from the ground, in pixels (0 for none; see Actor::TryStartMantle).
		float JetTimeMS = 0.0F; //!< Its jetpack's full tank, in ms, for the flight links (see PathFinder::AddFlightLinks); 0 for none.
		float JetClimbMSPerPx = 6.0F; //!< The fuel its climbs burn per pixel of height, in ms, from its own jet's push (see AHuman::ClimbFuelPerPixel).
		Vector Velocity; //!< Its velocity when it asks, in m/s: a search started in the air charges for going against it (see AdjacentCost).
		bool WalksStairs = false; //!< Whether its legs take stairs and slopes of about sixty degrees (a soldier walks the base game's steep stairs unaided; nothing is known of a crab's).
		bool ClimbsLadders = false; //!< Whether it climbs ladders hand over hand (a humanoid with an arm): ladders are a way up and down for it, jet or none.
		float LeapHeight = 0.0F; //!< How high a leap of its legs lifts it, in pixels (see AHuman::GetLegJumpHeight); 0 for none.
		float LeapSpeed = 4.0F; //!< How fast a leap carries it forward, in m/s.
		float MaxSafeFall = FLT_MAX; //!< For a searcher with no jet to brake a fall, the highest drop it lands from unhurt, in pixels (see Actor::GetMaxSafeFallHeight): falls higher are not routed. FLT_MAX for no limit.
		bool Scrambles = false; //!< Whether it scrambles up rough slopes too steep for stairs on its legs and arms (a humanoid with an arm).
		bool Floats = false; //!< Whether it floats in deep water (ActorWater::IsFloater) and swims along the surface, rather than walking the bottom.
		float BreathSeconds = FLT_MAX; //!< How long it holds its breath with its head under, in seconds (ActorWater::GetBreathSeconds); FLT_MAX for what doesn't breathe.
		bool CrossesLava = false; //!< Whether it may be routed through lava: what doesn't burn (machines).
	};

	/// Whether an async path request is done: set by the worker that solved it once the results are written, read by the thread that asked.
	/// Release on the write and acquire on the read, so the results are all there to be read once it says so (a plain bool worked on x86's
	/// strong ordering, by luck; elsewhere the asker could copy a half-built path). Copies take the value, so a request can still be copied.
	struct PathRequestDoneFlag {
		std::atomic<bool> Value{false};
		PathRequestDoneFlag() = default;
		PathRequestDoneFlag(const PathRequestDoneFlag& other) : Value(other.Value.load(std::memory_order_acquire)) {}
		PathRequestDoneFlag& operator=(const PathRequestDoneFlag& other) {
			Value.store(other.Value.load(std::memory_order_acquire), std::memory_order_release);
			return *this;
		}
		operator bool() const volatile { return Value.load(std::memory_order_acquire); }
		void operator=(bool done) volatile { Value.store(done, std::memory_order_release); }
	};

	/// Information required to make an async pathing request.
	struct PathRequest {
		PathRequestDoneFlag complete;
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
		/// Where a body is in this node's cell, and what a route through it is given: the centre, moved off the walls the 24 px cells don't line
		/// up with. In the air between walls (a shaft, a hatch, a gap), the middle of the channel at its height; beside one wall, or on a floor
		/// against one, out from it. (Routes were the cells' centres, and up a shaft or a climb beside a ledge they ran against one wall: units
		/// jetted into the wall, or strafed to line up.) The search, the grid and the costs keep to Pos.
		Vector Anchor;
		bool Ladder = false; //!< Rungs of the Ladder material in or beside this cell: a way up and down for a climber (the anchor is where its body climbs).

		bool m_Navigable = true; //!< Whether this node can be navigated through.

		float Surface = -1.0F; //!< The ground surface in this node's column within its cell, or -1 for none (the node is in the air, or buried).
		int FreeHeight = 0; //!< Air above the surface (or above the centre, for a node in the air) in the node's column, up to c_ClearanceReach.
		int ClearLeft = 0; //!< Air to the left of the centre (a little over the surface, for a node on the ground), up to a node's width.
		int ClearRight = 0; //!< Air to the right, likewise.
		bool StairsUpRight = false; //!< Whether stairs, or a slope of about sixty degrees, lead from this node's floor up to the floor of the node two up and one to the right (see UpdateNodeCosts).
		bool Grounded = false; //!< Whether a body here stands on something: any of the lines down across the cell meets ground (see UpdateNodeCosts).
		bool StairsUpLeft = false; //!< Likewise up to the left.
		bool ScrambleUpRight = false; //!< Whether a rough slope of loose or diggable ground, too steep for stairs, leads from this node's floor up to the floor of the node three up and one to the right (see UpdateNodeCosts).
		bool ScrambleUpLeft = false; //!< Likewise up to the left.
		/// Liquid under this node's surface (LM-4): the surface is then the liquid's, and LiquidDepth is how far down its centre column the
		/// liquid goes before the bottom, up to c_ClearanceReach. None and 0 for a dry node.
		PathLiquid Liquid = PathLiquid::None;
		int LiquidDepth = 0;
		/// Stepping over something low between this node's floor and a floor level with it one node (index 0) or two nodes (index 1) to the right:
		/// how high the thing is over the floor, or -1 when there is nothing to step over (or no such floor, or it is too high), and the air over it.
		std::array<float, 2> StepOverRise = {-1.0F, -1.0F};
		std::array<int, 2> StepOverRoom = {0, 0};
		/// Likewise to the left, so that either end of a step has it: a lump put down after the grid was built is re-sampled only by the nodes
		/// whose cells it is in, and the floor two nodes to one side of it never heard of it.
		std::array<float, 2> StepOverRiseLeft = {-1.0F, -1.0F};
		std::array<int, 2> StepOverRoomLeft = {0, 0};
		static constexpr int c_ClearanceReach = 96; //!< How far up the free height is measured.
		static constexpr int c_SamplingReachUpNodes = 6; //!< How many rows up a node's measures look: a step-over's room, up to 48 px over its floor and c_ClearanceReach more.
		static constexpr int c_SamplingReachSideNodes = 2; //!< How many columns either side a node's measures look: the clearances and the step-overs.

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

		/// Waits for this grid's path searches in flight to finish, yielding the thread meanwhile, but not for ever: after the time given it
		/// says so in the console and gives up, so a search that never ends can't freeze the game.
		/// @param timeoutMS How long to wait at most, in real milliseconds.
		/// @return Whether no searches were in flight when it returned.
		bool WaitForPathingRequests(int timeoutMS = 10000);

		/// Draws the grid in an area for the navigation debug overlay (SettingsMan::NavDebugOverlay): a dot over each node a body can stand on,
		/// green where the searcher stands upright, yellow where it can only crawl, red where it doesn't fit; cyan lines for the step-overs,
		/// magenta for the stairs, pale green arcs for the searcher's leaps.
		/// @param area The part of the scene to draw, in scene coordinates.
		/// @param agent The searcher whose sizes and leaps to show (the inspected unit's, or a soldier's).
		void DrawDebug(const Box& area, const PathAgent& agent);

		/// Debug: what the grid makes of the node at a scene point, as a line of text (its surface, ground, room and the material each way,
		/// its step-overs, stairs and ladder, and its anchor).
		std::string DescribeNodeAt(const Vector& scenePos);

		/// A search this grid answered, kept for the recent path solves overlay (SettingsMan::ShowRecentSolves).
		struct DebugSolve {
			Vector Start; //!< Where the search was asked from.
			Vector End; //!< Where it was asked to.
			std::vector<Vector> Points; //!< The nodes of the answer, as the search took them (before the route's points are thinned and moved).
			std::vector<float> StepCosts; //!< What each step between them cost, one fewer than Points.
			std::vector<PathStepKind> Kinds; //!< What each step is, one fewer than Points.
			int Status = 0; //!< MicroPather's answer: SOLVED, NO_SOLUTION or START_END_SAME.
			float TotalCost = 0.0F;
			double SolveMS = 0.0; //!< How long the search took.
			bool Cut = false; //!< Whether the route was cut short at something the searcher can't get through.
		};

		/// Gets the centre of the node with an id (as RecalculateAreaCosts returns them), for the terrain update boxes overlay.
		/// @param id The node's id.
		/// @return Its centre, or a zero vector for an id that isn't one.
		Vector GetNodePos(int id) const { return id >= 0 && id < static_cast<int>(m_NodeGrid.size()) ? m_NodeGrid[id].Pos : Vector(); }

		/// The last few searches this grid answered, oldest first, while the recent path solves overlay is on (none are kept while it's off).
		/// @param solves Filled with them.
		void GetRecentSolves(std::vector<DebugSolve>& solves) const;

		/// One way out of a node, for the navigation overlay's node under the pointer (see DescribeEdgesAt).
		struct DebugEdge {
			Vector From; //!< Where the step starts: the node's standing point (or its anchor, in the air).
			Vector To; //!< Where the step goes: the target node's standing point (or its anchor, in the air).
			float Cost = 0.0F; //!< What the search pays for the step, the searcher's recent failures included.
			float AvoidCost = 0.0F; //!< How much of Cost is for those failures (PathAgent::Avoid and AvoidLinks).
			PathStepKind Kind = PathStepKind::Walk; //!< What the step is, as the route would label it (StepKindBetween).
			bool Flight = false; //!< Whether it is a flight link (see AddFlightLinks).
			float FuelMS = 0.0F; //!< A flight link's fuel, as the route-follower reckons it, in ms.
		};

		/// Debug: every way out of the node at a scene point, as a searcher is offered them (AdjacentCost with its sizes, jet, legs and recent
		/// failures), with what each step is and costs. Borrows this thread's searcher for the call and puts it back.
		/// @param scenePos The point.
		/// @param agent The searcher.
		/// @return The ways out; none for no node or one that can't be navigated.
		std::vector<DebugEdge> DescribeEdgesAt(const Vector& scenePos, const PathAgent& agent);

		/// Recalculates all the costs between all the PathNodes by tracing lines in the material layer and summing all the material strengths for each encountered pixel. Also resets the pather itself.
		void RecalculateAllCosts();

		/// Recalculates the costs between all the PathNodes touching a deque of specific rectangular areas (which will be wrapped). Also resets the pather itself, if necessary.
		/// @param boxList The deque of Boxes representing the updated areas.
		/// Every box is taken off the deque and its nodes marked as waiting (a node waits once however many boxes touch it); then the oldest waiting
		/// nodes, up to the limit, are sampled again. The rest wait for the next call.
		/// @param nodeUpdateLimit The maximum number of PathNodes we'll update this call.
		/// @return The ids of the PathNodes that were sampled again, or none if no cost changed.
		std::vector<int> RecalculateAreaCosts(std::deque<Box>& boxList, size_t nodeUpdateLimit);

		/// How many nodes are marked as changed and not yet sampled again (see RecalculateAreaCosts).
		size_t GetWaitingNodeCount() const { return m_WaitingNodes.size(); }

		/// Helper function for getting the PathNode ids in a Box.
		/// @param box The Box of which all PathNodes it touches should be returned.
		/// @param samplingReach Whether to take in too every node whose measures look into the box, for re-sampling after a terrain change.
		/// @return A list of the PathNode ids inside the box.
		std::vector<int> GetNodeIdsInBox(Box box, bool samplingReach = false);

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
		static constexpr float c_OpenIntegrity = 5.0F; //!< The strongest material a body passes as open air (see Open).

		MicroPather* m_Pather; //!< The actual pathing object that does the pathfinding work. Owned.
		std::vector<PathNode> m_NodeGrid; //!< The array of PathNodes representing the grid on the scene.
		std::deque<int> m_WaitingNodes; //!< Ids of nodes in changed areas not sampled again yet, oldest first (see RecalculateAreaCosts).
		std::vector<bool> m_NodeWaiting; //!< Per node id, whether it is in m_WaitingNodes.
		unsigned int m_NodeDimension; //!< The width and height of each PathNode, in pixels on the scene.
		unsigned char m_LadderMaterial = 0; //!< The Ladder material's index (the bunkers' rungs), 0 when there is none; looked up once at creation.
		std::array<unsigned char, 5> m_LiquidMaterials = {}; //!< The liquids' material indices, by PathLiquid (0 when the scene has none); looked up once at creation.

		/// Which liquid a material index is, or None.
		PathLiquid LiquidOf(unsigned char id) const;

		/// The extra cost of a step into a node with liquid under it, for this searcher (see AdjacentCost); 0 for a dry node.
		float LiquidCost(const PathNode& to) const;

		/// The terrain at a point as a body meets it: air for the ladders' rungs, which a soldier passes (see AHuman::LearnFlight).
		unsigned char TerrNav(int x, int y) const;

		std::vector<std::pair<Vector, double>> m_TeamAvoid; //!< Places this team's units have failed at lately (a long stuck), and until when (sim ms).
		struct AvoidLink {
			Vector from;
			Vector to;
			double until;
		};
		std::vector<AvoidLink> m_TeamAvoidLinks; //!< Flights this team's units have failed lately: from where, for where, until when.
		mutable std::mutex m_TeamAvoidMutex;
		static constexpr size_t c_TeamAvoidKept = 256; //!< How many team avoid places, and how many team avoid flights, are kept at most.
		std::deque<DebugSolve> m_RecentSolves; //!< The last few searches, for the recent path solves overlay (see GetRecentSolves).
		/// The nav overlay's leap links, per floor node, kept for half a second (DrawDebug): worked out afresh every sim update for every node in
		/// view they were 100 k+ ray casts an update on the main thread. Forgotten when the searcher's leap changes, or when the view has moved on far enough to fill it.
		struct DebugLeaps {
			double TimeMS = 0.0;
			std::vector<micropather::StateCost> Links;
		};
		std::unordered_map<const PathNode*, DebugLeaps> m_DebugLeaps;
		std::array<float, 4> m_DebugLeapsAgent{}; //!< The leap height, leap speed, standing height and mantle height the kept links were found for.
		mutable std::mutex m_RecentSolvesMutex;
		static constexpr size_t c_RecentSolvesKept = 8;

		/// Keeps a search's answer for the recent path solves overlay, with each step's cost and kind as this thread's searcher sees them.
		void RecordSolve(const Vector& start, const Vector& end, const std::vector<void*>& statePath, int status, float totalCost, bool cut);

	public:
		/// Remembers a place a unit of this grid's team failed at, for every unit of the team to route around for a while.
		/// Drops the ones expired by nowMS, and the oldest past c_TeamAvoidKept.
		void AddTeamAvoid(const Vector& place, double untilMS, double nowMS);
		/// The team's remembered failures still in force, added to a list.
		void GetTeamAvoid(std::vector<Vector>& places, double nowMS) const;
		/// Remembers a flight a unit of this grid's team failed: from that take-off for that landing, dearer for the whole team for a while.
		/// Drops the ones expired by nowMS, and the oldest past c_TeamAvoidKept.
		void AddTeamAvoidLink(const Vector& from, const Vector& to, double untilMS, double nowMS);
		/// The team's failed flights still in force, added to a list.
		void GetTeamAvoidLinks(std::vector<std::pair<Vector, Vector>>& links, double nowMS) const;

	private:
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
		/// @param stopAbove Stop at the first material stronger than this (see SceneMan::CastMaxStrengthRayMaterial).
		/// @return The strongest material.
		const Material* StrongestMaterialAlongLine(const Vector& start, const Vector& end, float stopAbove = std::numeric_limits<float>::max()) const;

		/// Whether the line between two points is open (see Open), stopping at the first pixel that isn't.
		bool LineOpen(const Vector& start, const Vector& end) const { return Open(*StrongestMaterialAlongLine(start, end, c_OpenIntegrity)); }

		/// Helper function for updating all the values of cost edges going out from a specific PathNodes.
		/// This does NOT update the pather, which is required before solving more paths after calling this.
		/// @param node The PathNode to update all costs of. It's safe to pass nullptr here. OWNERSHIP IS NOT TRANSFERRED!
		/// @return Whether the PathNodes costs changed.
		bool UpdateNodeCosts(PathNode* node) const;

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

		/// What a walk's step pays for the material along it: nothing for what a walking body goes through as it comes (Open: grass,
		/// foliage, ash), except a liquid, which LiquidCost prices; otherwise the transition cost (GetMaterialTransitionCost).
		/// @param material The strongest material along the step.
		float WalkMaterialCost(const Material& material) const;

		/// What a rung of a jetpack climb up a column costs over its height, for how hard it is to fly: hugging a wall with open air on the
		/// other side, or a gap barely wider than the body. A shaft, tight both sides, costs nothing extra.
		/// @param node The node the rung rises into.
		float ClimbMarginCost(const PathNode& node) const;

		/// Whether a leap of the searcher's legs (PathAgent::LeapHeight and LeapSpeed) takes it from one floor node to another: within its
		/// reach under gravity, landing no higher than most of the leap's height and no more than two nodes lower, with the body's head,
		/// middle and feet clear all along the arc.
		/// @param from The floor it leaps from.
		/// @param to The floor it lands on.
		/// @return Whether the leap fits.
		bool LeapFits(const PathNode& from, const PathNode& to) const;

		/// Whether the ground between two floors a leap fits has a gap in it that the walk can't step across: a stretch at least half a
		/// node wide with no floor within a node under the lower of the two (a liquid there is no floor). Bumps, plants and lumps are
		/// floor: a walk goes over or through them.
		/// @param from The floor it leaps from.
		/// @param to The floor it lands on.
		bool GapBetween(const PathNode& from, const PathNode& to) const;

		/// Whether a floor is the top of a face (a lip) seen from the way a leap or a mantle comes: within a node back from its middle, the
		/// ground's top drops half a node or more between two 2 px columns. A slope up to it is walked.
		/// @param to The floor.
		/// @param direction -1 for a leap leftward, 1 rightward.
		bool LipAt(const PathNode& to, float direction) const;

		/// Whether the legs walk from one floor node to its diagonal neighbour up or down an incline no steeper than 40 degrees: the ground's top between them rises or
		/// falls by less than half a node from one 2 px column to the next (a face that size is a lip), with no gap, and the body's middle
		/// clear over the way. However the straight line between the node centres meets the ground.
		/// @param from The floor walked from.
		/// @param to The floor walked to, a node across and a node up or down.
		bool SurfaceWalkable(const PathNode& from, const PathNode& to) const;

		/// Whether door material at a place is a door this grid sees through: one of the grid's side, erased while its nodes were sampled
		/// (Scene::UpdatePathFinding, OverrideMaterialDoors), so no edge of the node there, or of its neighbours into it, sampled a door.
		bool DoorSeenThrough(const Vector& at) const;

		/// Adds the leaps from a floor node (see LeapFits) to its adjacent list: only over a gap the walk can't cross (GapBetween), or onto a lip
		/// higher than the searcher mantles, and priced over the walk of the same distance.
		/// @param node The node.
		/// @param adjacentList The list.
		void AddLeapLinks(const PathNode& node, std::vector<micropather::StateCost>* adjacentList);

		/// What it costs to fly straight up (or down) a column, from one height to another, for how close its sides pass to something solid:
		/// the body's edges (a half-width and a little each side of the column) traced the whole way, not only at the nodes' centre rows.
		/// One edge touching with the other in the open is a column grazing a lip or a corner, which a column further out clears: dear.
		/// Both edges touching is a shaft, which the shaft-centring flight handles: nothing here (RoomToPass and the tight-gap risk price it).
		/// @param x The column.
		/// @param fromY Where the climb starts.
		/// @param toY Where it ends.
		/// @return The extra cost.
		float ColumnGrazeCost(float x, float fromY, float toY) const;

		/// The flight links from a node at the edge of its floor: to floors within reach of a flight as the pilot flies one (up to just over the
		/// landing, across, down onto it), where the rise, the crossing and the descent are open air and a tank's fuel covers it, priced by the
		/// flight's time and fuel. These are the routes across gaps and up to ledges the column-and-rung jumps never offered.
		/// @param node The node the flights leave from.
		/// @param adjacentList The list to add the links to.
		void AddFlightLinks(const PathNode& node, std::vector<micropather::StateCost>* adjacentList);

		/// A flight link from a node (see CollectFlightLinks).
		struct FlightLink {
			const PathNode* target; //!< The landing.
			float cost; //!< What the search pays for it.
			float fuel; //!< The fuel it takes, in ms.
		};

		/// All the flight links from a node at the edge of its floor, cheapest first: AddFlightLinks offers the first few, and the navigation
		/// overlay shows their fuel.
		/// @param node The node the flights leave from.
		/// @param links Where to put them.
		void CollectFlightLinks(const PathNode& node, std::vector<FlightLink>& links);

		/// Sets this thread's searcher (its sizes, jet, legs, dig and breach strength and recent failures) from an agent, as a search does
		/// before it starts.
		void ApplyAgent(const PathAgent& agent);

		/// Whether a node stands at the edge of its floor: on ground, with a neighbour to one side that isn't.
		bool IsFloorEdge(const PathNode& node) const;

		/// What a jump's landing costs over its own for how little floor there is round it: a pinpoint ledge has to be hit just so, and a wide
		/// floor forgives a jump that comes down a little long or short.
		/// @param node The landing.
		float LandingWidthCost(const PathNode& node) const;

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
