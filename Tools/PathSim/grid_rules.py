"""grid_rules.py -- the path grid's rules, mirroring Source/System/PathFinder.cpp (and PathFinder.h) function by function.

RE-SYNC NOTE: every method below names the C++ function and the line range it mirrors (line numbers as of the PathFinder.cpp this
was written against, 1076 lines). After editing the C++, re-read each named range and update the Python next to it. Nothing in
here is "simplified": the rays, the offsets, the bands, the epsilon rule, the thread_local search parameters, the jump chains and
the path post-processing are the C++ as written. What this file does NOT contain: scene services (GetTerrMatter, the ray casts,
ShortestDistance, ForceBounds) live in scene.py, the A* (MicroPather) in astar.py, and the PathAgent in agent.py.
"""

import math

import numpy as np

import astar
from agent import C_MPP, C_PATHFINDING_DEFAULT_DIG_STRENGTH, FLT_MAX
from materials import MATERIAL_AIR, MATERIAL_DOOR
from scene import Box

INT_MAX = 2147483647

# PathNode::AdjacentNodes order (PathFinder.h 64-72): clockwise with top first.
UP, UPRIGHT, RIGHT, RIGHTDOWN, DOWN, DOWNLEFT, LEFT, LEFTUP = range(8)
DIRECTION_NAMES = ["Up", "UpRight", "Right", "RightDown", "Down", "DownLeft", "Left", "LeftUp"]


class PathStepKind:
    """PathFinder.h 20-28."""
    Walk = 0
    Crawl = 1
    Jump = 2
    Fall = 3
    Dig = 4
    Door = 5
    NAMES = ["Walk", "Crawl", "Jump", "Fall", "Dig", "Door"]


class PathNode:
    """PathFinder.h 52-95 (struct PathNode) and PathNode::PathNode (PathFinder.cpp 55-62)."""

    c_MaxAdjacentNodeCount = 8
    c_ClearanceReach = 96

    __slots__ = ("Pos", "m_Navigable", "Surface", "FreeHeight", "ClearLeft", "ClearRight", "AdjacentNodes", "AdjacentNodeBlockingMaterials", "id", "gx", "gy")

    def __init__(self, pos, out_of_bounds_material):
        self.Pos = pos
        self.m_Navigable = True  # Not initialised in C++; Scene::Activate marks every node navigable when the scene has no navigable areas (Scene.cpp 2543).
        self.Surface = -1.0
        self.FreeHeight = 0
        self.ClearLeft = 0
        self.ClearRight = 0
        self.AdjacentNodes = [None] * 8
        self.AdjacentNodeBlockingMaterials = [out_of_bounds_material] * 8  # Costs are infinite unless recalculated as otherwise.
        self.id = -1
        self.gx = -1
        self.gy = -1

    # The named references of PathFinder.h, as properties.
    Up = property(lambda self: self.AdjacentNodes[UP])
    UpRight = property(lambda self: self.AdjacentNodes[UPRIGHT])
    Right = property(lambda self: self.AdjacentNodes[RIGHT])
    RightDown = property(lambda self: self.AdjacentNodes[RIGHTDOWN])
    Down = property(lambda self: self.AdjacentNodes[DOWN])
    DownLeft = property(lambda self: self.AdjacentNodes[DOWNLEFT])
    Left = property(lambda self: self.AdjacentNodes[LEFT])
    LeftUp = property(lambda self: self.AdjacentNodes[LEFTUP])

    UpMaterial = property(lambda self: self.AdjacentNodeBlockingMaterials[UP])
    UpRightMaterial = property(lambda self: self.AdjacentNodeBlockingMaterials[UPRIGHT])
    RightMaterial = property(lambda self: self.AdjacentNodeBlockingMaterials[RIGHT])
    RightDownMaterial = property(lambda self: self.AdjacentNodeBlockingMaterials[RIGHTDOWN])
    DownMaterial = property(lambda self: self.AdjacentNodeBlockingMaterials[DOWN])
    DownLeftMaterial = property(lambda self: self.AdjacentNodeBlockingMaterials[DOWNLEFT])
    LeftMaterial = property(lambda self: self.AdjacentNodeBlockingMaterials[LEFT])
    LeftUpMaterial = property(lambda self: self.AdjacentNodeBlockingMaterials[LEFTUP])

    def __repr__(self):
        return "PathNode(%d,%d)" % (int(self.Pos[0]), int(self.Pos[1]))


class SearchParams:
    """The thread_local search state of PathFinder.cpp 33-53: s_JumpHeight, s_JumpHeightVertical/Diagonal, s_DigStrength,
    s_BreachStrength, s_StandHeight, s_CrawlHeight, s_HalfWidth."""

    def __init__(self):
        self.JumpHeight = 0.0
        self.JumpHeightVertical = 0
        self.JumpHeightDiagonal = 0
        self.DigStrength = 0.0
        self.BreachStrength = 0.0
        self.StandHeight = 40.0
        self.CrawlHeight = 22.0
        self.HalfWidth = 6.0


class PathResult:
    def __init__(self):
        self.result = astar.NO_SOLUTION
        self.path = []  # [(x, y)] -- the game's pathResult
        self.kinds = []  # [PathStepKind] -- one per point after the first
        self.totalCost = 0.0
        self.statePath = []  # the raw node states MicroPather returned (after the cut-short rule)
        self.start = None
        self.end = None
        self.startNode = None
        self.endNode = None
        self.solveStats = None

    def result_name(self):
        return {astar.SOLVED: "SOLVED", astar.NO_SOLUTION: "NO_SOLUTION", astar.START_END_SAME: "START_END_SAME"}.get(self.result, str(self.result))


class PathFinder:
    """class PathFinder (PathFinder.h 97-314)."""

    c_NodeCostChangeEpsilon = 5.0  # PathFinder.h 224

    def __init__(self, scene, nodeDimension=24):
        """PathFinder::PathFinder (64-67): Clear() then Create()."""
        self.scene = scene
        self.s = SearchParams()
        self.Clear()
        self.Create(nodeDimension)

    # --- PathFinder::Clear (73-77) ------------------------------------------------------------------------------------------------
    def Clear(self):
        self.m_NodeGrid = []
        self.m_NodeDimension = 24  # SCENEGRIDSIZE
        self.m_Offset = (0.0, 0.0)
        self.m_GridWidth = 0
        self.m_GridHeight = 0
        self.m_WrapsX = False
        self.m_WrapsY = False

    # --- PathFinder::Create (79-142) ----------------------------------------------------------------------------------------------
    def Create(self, nodeDimension):
        scene = self.scene
        self.m_NodeDimension = int(nodeDimension)
        sceneWidth = scene.GetSceneWidth()
        sceneHeight = scene.GetSceneHeight()
        # Make overlapping nodes at seams if necessary, to make sure all scene pixels are covered.
        self.m_GridWidth = int(math.ceil(float(sceneWidth) / float(self.m_NodeDimension)))
        self.m_GridHeight = int(math.ceil(float(sceneHeight) / float(self.m_NodeDimension)))
        self.m_WrapsX = scene.SceneWrapsX()
        self.m_WrapsY = scene.SceneWrapsY()
        self.m_Offset = (nodeDimension * 0.5, 0.0)

        # Create and assign scene coordinate positions for all nodes. (The first X is nodeDimension/2 + the offset, but every row,
        # the first included, starts over at nodeDimension/2: the offset only ever shows in DebugRender.)
        out_of_bounds = scene.materials.out_of_bounds
        nodeX = float(nodeDimension) / 2.0 + self.m_Offset[0]
        nodeY = float(nodeDimension) / 2.0 + self.m_Offset[1]
        grid = self.m_NodeGrid
        for y in range(self.m_GridHeight):
            # Make sure no cell centers are off the scene (since they can overlap the far edge of the scene).
            if nodeY >= sceneHeight:
                nodeY = sceneHeight - 1.0
            # Start the row over at middle of the leftmost node each new row.
            nodeX = float(nodeDimension) / 2.0
            for x in range(self.m_GridWidth):
                if nodeX >= sceneWidth:
                    nodeX = sceneWidth - 1.0
                node = PathNode((nodeX, nodeY), out_of_bounds)
                node.id = len(grid)
                node.gx = x
                node.gy = y
                grid.append(node)
                nodeX += float(nodeDimension)
            nodeY += float(nodeDimension)

        # Assign all the adjacent nodes on each node. GetPathNodeAtGridCoords handles Scene wrapping.
        for x in range(self.m_GridWidth):
            for y in range(self.m_GridHeight):
                node = self.GetPathNodeAtGridCoords(x, y)
                adj = node.AdjacentNodes
                adj[UP] = self.GetPathNodeAtGridCoords(x, y - 1)
                adj[RIGHT] = self.GetPathNodeAtGridCoords(x + 1, y)
                adj[DOWN] = self.GetPathNodeAtGridCoords(x, y + 1)
                adj[LEFT] = self.GetPathNodeAtGridCoords(x - 1, y)
                adj[UPRIGHT] = self.GetPathNodeAtGridCoords(x + 1, y - 1)
                adj[RIGHTDOWN] = self.GetPathNodeAtGridCoords(x + 1, y + 1)
                adj[DOWNLEFT] = self.GetPathNodeAtGridCoords(x - 1, y + 1)
                adj[LEFTUP] = self.GetPathNodeAtGridCoords(x - 1, y - 1)

        self.RecalculateAllCosts()
        return 0

    # --- PathFinder::CalculatePath(start, end, path, cost, agent, kinds) (172-398) -------------------------------------------------
    def CalculatePath(self, start, end, agent):
        scene = self.scene
        s = self.s
        out = PathResult()

        jumpHeight = agent.JumpHeight
        digStrength = agent.DigStrength
        breachStrength = agent.BreachStrength
        s.StandHeight = agent.StandHeight
        s.CrawlHeight = agent.CrawlHeight
        s.HalfWidth = agent.HalfWidth

        # Make sure start and end are within scene bounds.
        start = scene.ForceBounds(start)
        end = scene.ForceBounds(end)
        out.start = start
        out.end = end

        # Convert from absolute scene pixel coordinates to path node indices: the node whose cell the point is in.
        nd = float(self.m_NodeDimension)
        startNodeX = int(math.floor(start[0] / nd))
        startNodeY = int(max(0.0, math.floor(start[1] / nd)))
        endNodeX = int(math.floor(end[0] / nd))
        endNodeY = int(max(0.0, math.floor(end[1] / nd)))

        # Actors capable of jumping/jetpacking can jump upwards.
        s.JumpHeight = jumpHeight
        if jumpHeight == FLT_MAX:
            s.JumpHeightVertical = INT_MAX
            s.JumpHeightDiagonal = INT_MAX
        else:
            # In float, as the C++ does it: jumpHeight / (m_NodeDimension * c_MPP), with c_MPP = 1.0F / 20.0F.
            node_metres = np.float32(np.float32(self.m_NodeDimension) * np.float32(C_MPP))
            s.JumpHeightVertical = max(1, int(np.float32(jumpHeight) / node_metres))
            s.JumpHeightDiagonal = max(1, int(np.float32(np.float32(jumpHeight) * np.float32(0.7)) / node_metres))

        # Actors capable of digging can use s_DigStrength to modify the node adjacency cost.
        s.DigStrength = digStrength
        s.BreachStrength = digStrength if breachStrength < 0.0 else breachStrength

        result = astar.NO_SOLUTION
        statePath = []
        totalCostResult = 0.0

        # A node whose centre is in solid ground is no place to start or finish: the one above (the surface node) is, when it's open,
        # or else the one below.
        GetTerrMatter = scene.GetTerrMatter

        def buried(n):
            return n is not None and GetTerrMatter(int(n.Pos[0]), int(n.Pos[1])) != MATERIAL_AIR

        def openNode(node):
            if buried(node):
                if node.Up is not None and not buried(node.Up):
                    return node.Up
                if node.Down is not None and not buried(node.Down):
                    return node.Down
            return node

        startNode = openNode(self.GetPathNodeAtGridCoords(startNodeX, startNodeY))
        endNode = openNode(self.GetPathNodeAtGridCoords(endNodeX, endNodeY))
        if endNode is not None and not self.NodeIsOnSolidGround(endNode) and endNode.Down is not None and endNode.Down.m_Navigable and self.NodeIsOnSolidGround(endNode.Down):
            endNode = endNode.Down
        out.startNode = startNode
        out.endNode = endNode
        stats = astar.SolveStats()
        out.solveStats = stats
        # If end node is invalid, there's no path
        if startNode is not None and endNode is not None and endNode.m_Navigable:
            result, statePath, totalCostResult = astar.Solve(self, startNode, endNode, stats)
            # A route that only exists through ground the searcher can't dig is usually down to the start node: the neighbouring cells
            # are tried as starts before that answer is given.
            if result == astar.SOLVED and totalCostResult > 100000.0 and digStrength <= C_PATHFINDING_DEFAULT_DIG_STRENGTH + 1.0:
                alternatives = [startNode.Up, startNode.Down, startNode.Left, startNode.Right, startNode.LeftUp, startNode.UpRight, startNode.DownLeft, startNode.RightDown]
                for alternative in alternatives:
                    if alternative is None or not alternative.m_Navigable or GetTerrMatter(int(alternative.Pos[0]), int(alternative.Pos[1])) != MATERIAL_AIR:
                        continue
                    otherResult, otherPath, otherCost = astar.Solve(self, alternative, endNode, stats)
                    if otherResult == astar.SOLVED and otherCost < totalCostResult:
                        result = otherResult
                        statePath = otherPath
                        totalCostResult = otherCost
                        if totalCostResult <= 100000.0:
                            break

        if result == astar.NO_SOLUTION:
            totalCostResult = FLT_MAX  # Otherwise micropather inits it to zero :)

        # A route that needs what the searcher hasn't got (digging, a door shot open) is cut short at the first such edge.
        if result == astar.SOLVED and totalCostResult > 100000.0 and len(statePath) > 2:
            for i in range(len(statePath) - 1):
                expensive = False
                for adjState, adjCost in self.AdjacentCost(statePath[i]):
                    if adjState is statePath[i + 1] and adjCost > 100000.0:
                        expensive = True
                if expensive:
                    statePath = statePath[:max(2, i + 1)]
                    break

        out.result = result
        out.totalCost = totalCostResult
        out.statePath = statePath

        if statePath:
            # The points of the path and what each step to them is, from the nodes they go between. The approximate first point is
            # the exact start and the last the exact end.
            steps = [[start, PathStepKind.Walk]]
            nodeSize = float(self.m_NodeDimension)
            for i in range(len(statePath) - 1):
                frm = statePath[i]
                to = statePath[i + 1]
                kind = self.StepKindBetween(frm, to)
                # A jump that is up a lot and over a little is up, then over: the top of the column goes in first, a node and a half
                # over the landing, when it's in the open.
                dx = scene.ShortestDistance(frm.Pos, to.Pos)[0]
                dy = to.Pos[1] - frm.Pos[1]
                if kind == PathStepKind.Jump and abs(dx) >= 1.0 and dy <= -nodeSize * 1.5 and -dy >= abs(dx) * 1.5:
                    # No higher than the landing's ceiling allows.
                    # (PathFinder.cpp CalculatePath, the apex: where the body's centre is when the feet have just cleared the landing's
                    # floor, 20 px under the feet in the open, and under a ceiling as little as keeps the head 2 px clear; inserted when it
                    # is above where the body stands on the landing.)
                    landingFloor = to.Surface if to.Surface >= 0.0 else to.Pos[1] + nodeSize * 0.5
                    ceiling = landingFloor - float(to.FreeHeight)
                    feetClearY = landingFloor - s.StandHeight * 0.45 - 20.0
                    headClearY = ceiling + s.StandHeight * 0.55 + 2.0
                    apexY = max(feetClearY, headClearY)
                    apex = scene.ForceBounds((frm.Pos[0], apexY))
                    standingY = landingFloor - s.StandHeight * 0.45
                    if apexY < standingY + 1.0 and GetTerrMatter(int(apex[0]), int(apex[1])) == MATERIAL_AIR:
                        steps.append([apex, PathStepKind.Jump])
                steps.append([to.Pos, kind])
            steps[-1][0] = end

            # Fewer points along a straight: walks that keep heading the same way on much the same level are run together, up to a
            # few nodes at a time. Nothing else is touched.
            fewer = [steps[0]]
            run = 0
            for i in range(1, len(steps)):
                last = i + 1 == len(steps)
                if not last and i + 1 < len(steps) and steps[i][1] == PathStepKind.Walk and steps[i + 1][1] == PathStepKind.Walk and run < 7:
                    dxHere = scene.ShortestDistance(fewer[-1][0], steps[i][0])[0]
                    dxNext = scene.ShortestDistance(steps[i][0], steps[i + 1][0])[0]
                    dyNext = steps[i + 1][0][1] - steps[i][0][1]
                    dyRun = steps[i + 1][0][1] - fewer[-1][0][1]
                    if dxHere * dxNext > 0.0 and abs(dyNext) <= nodeSize and abs(dyRun) <= nodeSize:
                        run += 1  # This point is passed through on the way to the next.
                        continue
                fewer.append(steps[i])
                run = 0

            out.path = [step[0] for step in fewer]
            out.kinds = [step[1] for step in fewer[1:]]
        else:
            # Empty path, give exact start and end.
            out.path = [start, end]
            out.kinds = [PathStepKind.Walk]
        return out

    def StepCosts(self, statePath):
        """The CCCP_PATH_LOG line's per-step costs (297-315): for each consecutive pair of states, the AdjacentCost of the edge
        (the last matching entry, as the C++ loop overwrites), or -1 if the edge isn't offered."""
        costs = []
        for i in range(len(statePath) - 1):
            stepCost = -1.0
            for adjState, adjCost in self.AdjacentCost(statePath[i]):
                if adjState is statePath[i + 1]:
                    stepCost = adjCost
            costs.append(stepCost)
        return costs

    # --- PathFinder::RecalculateAllCosts (442-456) ---------------------------------------------------------------------------------
    def RecalculateAllCosts(self):
        self.UpdateNodeList(list(range(len(self.m_NodeGrid))))

    # --- PathFinder::RecalculateAreaCosts (458-486) --------------------------------------------------------------------------------
    def RecalculateAreaCosts(self, boxList, nodeUpdateLimit):
        """boxList is consumed from the front (a deque in C++); returns the ids updated (empty when no cost changed)."""
        nodeIDsToUpdate = set()
        while boxList:
            for nodeId in self.GetNodeIdsInBox(boxList[0]):
                nodeIDsToUpdate.add(nodeId)
            boxList.pop(0)
            if len(nodeIDsToUpdate) > nodeUpdateLimit:
                break
        nodeVec = sorted(nodeIDsToUpdate)
        if not self.UpdateNodeList(nodeVec):
            nodeVec = []
        return nodeVec

    # --- PathFinder::LeastCostEstimate (488-492) -------------------------------------------------------------------------------------
    def LeastCostEstimate(self, startNode, endNode):
        dx, dy = self.scene.ShortestDistance(startNode.Pos, endNode.Pos)
        return math.sqrt(dx * dx + dy * dy) / self.m_NodeDimension

    # --- PathFinder::AdjacentCost (494-676) --------------------------------------------------------------------------------------------
    def AdjacentCost(self, node):
        """Returns [(adjacent node, cost)], in the C++ push order (duplicates included)."""
        s = self.s
        adjacentList = []
        push = adjacentList.append
        GetMaterialTransitionCost = self.GetMaterialTransitionCost
        Open = self.Open
        mats = node.AdjacentNodeBlockingMaterials
        adj = node.AdjacentNodes

        # We do a little trick here, where we radiate out a little percentage of our average cost in all directions.
        costRadiationMultiplier = 0.2
        radiatedCost = 0.0  # GetNodeAverageTransitionCost(*node) * costRadiationMultiplier;

        isInNoGrav = False  # g_SceneMan.IsPointInNoGravArea(node->Pos): no such areas in this scene.
        allowDiagonal = not isInNoGrav

        down = adj[DOWN]
        if down is not None and down.m_Navigable:
            # (Down through a gap narrower than the body is no way down; down through ground is a dig, and the digger makes its own room.)
            cost = (1.0 + GetMaterialTransitionCost(mats[DOWN]) + radiatedCost) * (1.0 if (self.RoomToPass(down) or not Open(mats[DOWN])) else 1000.0)
            push((down, cost))

        rightDown = adj[RIGHTDOWN]
        if rightDown is not None and rightDown.m_Navigable and allowDiagonal:
            push((rightDown, 1.4 + (GetMaterialTransitionCost(mats[RIGHTDOWN]) * 1.4) + radiatedCost))

        downLeft = adj[DOWNLEFT]
        if downLeft is not None and downLeft.m_Navigable and allowDiagonal:
            push((downLeft, 1.4 + (GetMaterialTransitionCost(mats[DOWNLEFT]) * 1.4) + radiatedCost))

        if isInNoGrav or self.NodeIsOnSolidGround(node):
            # Cost to discourage us from going up.
            extraUpCost = 1.5

            # We can only go straight left or right if we're on solid ground, otherwise we need to go downwards. The head room along the
            # way says whether it's a walk, a crawl (slower), or no way through at all for this searcher.
            left = adj[LEFT]
            if left is not None and left.m_Navigable:
                cost = (1.0 + GetMaterialTransitionCost(mats[LEFT]) + radiatedCost) * (self.HeadRoomFactor(node, left) if Open(mats[LEFT]) else 1.0)
                push((left, cost))

            right = adj[RIGHT]
            if right is not None and right.m_Navigable:
                cost = (1.0 + GetMaterialTransitionCost(mats[RIGHT]) + radiatedCost) * (self.HeadRoomFactor(node, right) if Open(mats[RIGHT]) else 1.0)
                push((right, cost))

            # Jumping vertically
            if s.JumpHeight < FLT_MAX:
                currentNode = node
                totalMaterialCost = 0.0
                for i in range(s.JumpHeightVertical):
                    upMat = currentNode.AdjacentNodeBlockingMaterials[UP]
                    upNode = currentNode.AdjacentNodes[UP]
                    if upNode is None or not upNode.m_Navigable or upMat.integrity > C_PATHFINDING_DEFAULT_DIG_STRENGTH:
                        break  # solid ceiling, stop
                    # Too close to a wall to go up past it: a jet pressed to a cliff face burns and doesn't lift.
                    if Open(upMat) and not self.RoomToPass(upNode, 3.0):
                        break

                    f = float(i + 2)  # Exponential cost increase for jumping higher
                    extraJumpCost = f * 0.5

                    totalMaterialCost += 1.0 + extraUpCost + extraJumpCost + (GetMaterialTransitionCost(upMat) * 3.0) + radiatedCost

                    push((upNode, totalMaterialCost))

                    currentNode = upNode

                    # Landing on a ledge beside the jump: from each rung of the jump, a step sideways onto a node that does stand on
                    # ground is offered too. Two steps when the first is still in the air.
                    if not self.NodeIsOnSolidGround(currentNode):
                        def landing(step, stepMaterial, stepCost, totalMaterialCost=totalMaterialCost):
                            if step is None or not step.m_Navigable or stepMaterial.integrity > s.DigStrength:
                                return None
                            # A landing wants room to stand up in.
                            if float(step.FreeHeight) < s.StandHeight:
                                return None
                            if self.NodeIsOnSolidGround(step):
                                push((step, totalMaterialCost + stepCost + GetMaterialTransitionCost(stepMaterial) + radiatedCost))
                                return None
                            return step

                        step = landing(currentNode.AdjacentNodes[LEFT], currentNode.AdjacentNodeBlockingMaterials[LEFT], 1.0)
                        if step is not None:
                            landing(step.AdjacentNodes[LEFT], step.AdjacentNodeBlockingMaterials[LEFT], 2.0)
                        step = landing(currentNode.AdjacentNodes[RIGHT], currentNode.AdjacentNodeBlockingMaterials[RIGHT], 1.0)
                        if step is not None:
                            landing(step.AdjacentNodes[RIGHT], step.AdjacentNodeBlockingMaterials[RIGHT], 2.0)
                        # And diagonally up onto a steep slope's face.
                        landing(currentNode.AdjacentNodes[LEFTUP], currentNode.AdjacentNodeBlockingMaterials[LEFTUP], 1.4 + extraUpCost)
                        landing(currentNode.AdjacentNodes[UPRIGHT], currentNode.AdjacentNodeBlockingMaterials[UPRIGHT], 1.4 + extraUpCost)
            else:
                up = adj[UP]
                if up is not None and up.m_Navigable:
                    push((up, 1.0 + extraUpCost + (GetMaterialTransitionCost(mats[UP]) * 3.0) + radiatedCost))  # Three times more expensive when digging.

            # Jumping diagonally
            if s.JumpHeight < FLT_MAX and adj[UPRIGHT] is not None and not isInNoGrav:
                currentNode = adj[UPRIGHT]
                totalMaterialCost = 1.4 + (extraUpCost * 1.4) + (GetMaterialTransitionCost(mats[UPRIGHT]) * 1.4 * 3.0) + radiatedCost
                for i in range(s.JumpHeightDiagonal):
                    nextNode = currentNode.AdjacentNodes[UPRIGHT]
                    nextMat = currentNode.AdjacentNodeBlockingMaterials[UPRIGHT]
                    if nextNode is None or not nextNode.m_Navigable or nextMat.integrity > C_PATHFINDING_DEFAULT_DIG_STRENGTH:
                        break  # solid ceiling, stop
                    # A jet comes down where there's room to stand, not into a crawlspace.
                    if Open(nextMat) and float(nextNode.FreeHeight) < s.StandHeight:
                        break
                    f = float(i + 2)
                    extraJumpCost = f * 0.5
                    totalMaterialCost += 1.4 + (extraUpCost * 1.4) + (extraJumpCost * 1.4) + (GetMaterialTransitionCost(nextMat) * 1.4 * 3.0) + radiatedCost
                    push((nextNode, totalMaterialCost))
                    currentNode = nextNode

            if s.JumpHeight < FLT_MAX and adj[LEFTUP] is not None and not isInNoGrav:
                currentNode = adj[LEFTUP]
                totalMaterialCost = 1.4 + (extraUpCost * 1.4) + (GetMaterialTransitionCost(mats[LEFTUP]) * 1.4 * 3.0) + radiatedCost
                for i in range(s.JumpHeightDiagonal):
                    nextNode = currentNode.AdjacentNodes[LEFTUP]
                    nextMat = currentNode.AdjacentNodeBlockingMaterials[LEFTUP]
                    if nextNode is None or not nextNode.m_Navigable or nextMat.integrity > C_PATHFINDING_DEFAULT_DIG_STRENGTH:
                        break
                    if Open(nextMat) and float(nextNode.FreeHeight) < s.StandHeight:
                        break
                    f = float(i + 2)
                    extraJumpCost = f * 0.5
                    totalMaterialCost += 1.4 + (extraUpCost * 1.4) + (extraJumpCost * 1.4) + (GetMaterialTransitionCost(nextMat) * 1.4 * 3.0) + radiatedCost
                    push((nextNode, totalMaterialCost))
                    currentNode = nextNode

            # Add cost for digging at 45 degrees and for digging upwards. (A step up a slope wants the head room a walk does.)
            upRight = adj[UPRIGHT]
            if upRight is not None and upRight.m_Navigable and allowDiagonal:
                cost = (1.4 + (extraUpCost * 1.4) + (GetMaterialTransitionCost(mats[UPRIGHT]) * 1.4 * 3.0) + radiatedCost) * (self.HeadRoomFactor(node, upRight) if Open(mats[UPRIGHT]) else 1.0)
                push((upRight, cost))

            leftUp = adj[LEFTUP]
            if leftUp is not None and leftUp.m_Navigable and allowDiagonal:
                cost = (1.4 + (extraUpCost * 1.4) + (GetMaterialTransitionCost(mats[LEFTUP]) * 1.4 * 3.0) + radiatedCost) * (self.HeadRoomFactor(node, leftUp) if Open(mats[LEFTUP]) else 1.0)
                push((leftUp, cost))

        return adjacentList

    # --- PathFinder::PositionsAreTheSamePathNode (678-684) ------------------------------------------------------------------------
    def PositionsAreTheSamePathNode(self, pos1, pos2):
        nd = float(self.m_NodeDimension)
        return (math.floor(pos1[0] / nd), math.floor(pos1[1] / nd)) == (math.floor(pos2[0] / nd), math.floor(pos2[1] / nd))

    # --- PathFinder::SurfaceUnder (686-698) --------------------------------------------------------------------------------------------
    def SurfaceUnder(self, node):
        """The ground surface within a node's cell: the first solid pixel from the top of the cell down the node's column, or -1 when
        the cell's top is already solid (the node is buried) or there is no ground in the cell."""
        GetTerrMatter = self.scene.GetTerrMatter
        x = int(node.Pos[0])
        top = int(node.Pos[1]) - self.m_NodeDimension // 2
        if GetTerrMatter(x, top) != MATERIAL_AIR:
            return -1.0
        for y in range(top + 1, top + self.m_NodeDimension + 1):
            if GetTerrMatter(x, y) != MATERIAL_AIR:
                return float(y)
        return -1.0

    # --- PathFinder::DescribeNodeAt (700-715) --------------------------------------------------------------------------------------------
    def DescribeNodeAt(self, scenePos):
        gridX = int(math.floor(scenePos[0] / float(self.m_NodeDimension)))
        gridY = int(math.floor(scenePos[1] / float(self.m_NodeDimension)))
        node = self.GetPathNodeAtGridCoords(gridX, gridY)
        if node is None:
            return "no node"

        def integrity(material):
            return str(int(material.integrity)) if material is not None else "-"

        text = "node %d,%d" % (int(node.Pos[0]), int(node.Pos[1]))
        text += "" if node.m_Navigable else " unnavigable"
        text += " surface %d ground %s" % (int(node.Surface), "yes" if self.NodeIsOnSolidGround(node) else "no")
        text += " free %d clear %d/%d" % (node.FreeHeight, node.ClearLeft, node.ClearRight)
        m = node.AdjacentNodeBlockingMaterials
        text += " up %s upright %s right %s rightdown %s" % (integrity(m[UP]), integrity(m[UPRIGHT]), integrity(m[RIGHT]), integrity(m[RIGHTDOWN]))
        text += " down %s downleft %s left %s leftup %s" % (integrity(m[DOWN]), integrity(m[DOWNLEFT]), integrity(m[LEFT]), integrity(m[LEFTUP]))
        return text

    # --- PathFinder::StandingPoint (717-723) -------------------------------------------------------------------------------------------
    def StandingPoint(self, node, lift):
        surface = self.SurfaceUnder(node)
        if surface < 0.0:
            return node.Pos
        return (node.Pos[0], surface - lift)

    # --- PathFinder::NodeIsOnSolidGround (725-729) -----------------------------------------------------------------------------------
    def NodeIsOnSolidGround(self, node):
        # Anything that isn't air is stood on.
        return self.s.JumpHeight == FLT_MAX or (node.AdjacentNodes[DOWN] is not None and node.AdjacentNodeBlockingMaterials[DOWN].integrity > 0.0)

    # --- PathFinder::Open (731-733) ------------------------------------------------------------------------------------------------------
    def Open(self, material):
        return material.integrity <= 5.0

    # --- PathFinder::RoomToPass (735-737) -------------------------------------------------------------------------------------------------
    def RoomToPass(self, node, widths=2.0):
        return self.s.JumpHeight == FLT_MAX or float(node.ClearLeft + node.ClearRight + 1) >= self.s.HalfWidth * widths  # +1: the node's own column

    # --- PathFinder::HeadRoomFactor (739-751) -------------------------------------------------------------------------------------------
    def HeadRoomFactor(self, frm, to):
        s = self.s
        if s.JumpHeight == FLT_MAX:
            return 1.0
        headRoom = min(frm.FreeHeight, to.FreeHeight)
        if float(headRoom) < s.CrawlHeight:
            return 1000.0
        if float(headRoom) < s.StandHeight:
            return 1.4  # A crawl is slower.
        return 1.0

    # --- PathFinder::StepKindBetween (753-789) --------------------------------------------------------------------------------------------
    def StepKindBetween(self, frm, to):
        if frm is None or to is None:
            return PathStepKind.Walk
        # The material along the step, from whichever side has it sampled (down and right are sampled; up and left are the neighbour's).
        material = None
        for i in range(PathNode.c_MaxAdjacentNodeCount):
            if frm.AdjacentNodes[i] is to:
                material = frm.AdjacentNodeBlockingMaterials[i]
        dx = self.scene.ShortestDistance(frm.Pos, to.Pos)[0]
        dy = to.Pos[1] - frm.Pos[1]
        nodeSize = float(self.m_NodeDimension)
        if material is not None and material.index == MATERIAL_DOOR:
            return PathStepKind.Door
        # Something solid on the straight line between the two: a dig if this searcher digs that, and otherwise the step wasn't along
        # that line at all but up the column and over onto a ledge (the landing edges), which is a jump.
        if material is not None and material.integrity > C_PATHFINDING_DEFAULT_DIG_STRENGTH and abs(dy) <= nodeSize and abs(dx) <= nodeSize:
            if material.integrity <= self.s.DigStrength:
                return PathStepKind.Dig
            return PathStepKind.Jump if dy < -1.0 else PathStepKind.Walk
        if dy < -1.0:
            return PathStepKind.Jump
        if dy > nodeSize + 1.0 or (dy > 1.0 and abs(dx) < 1.0):
            return PathStepKind.Fall
        if float(min(frm.FreeHeight, to.FreeHeight)) < self.s.StandHeight:
            return PathStepKind.Crawl
        return PathStepKind.Walk

    # --- PathFinder::GetMaterialTransitionCost (791-801) -------------------------------------------------------------------------------
    def GetMaterialTransitionCost(self, material):
        strength = material.integrity
        # A door is open to whoever can dig it or shoot it open; anything else is open to whoever can dig it.
        door = material.index == MATERIAL_DOOR
        if strength > (self.s.BreachStrength if door else self.s.DigStrength):
            strength *= 1000.0
        return strength

    # --- PathFinder::StrongestMaterialAlongLine (803-805) -------------------------------------------------------------------------------
    def StrongestMaterialAlongLine(self, start, end):
        return self.scene.CastMaxStrengthRayMaterial(start, end, 0, MATERIAL_AIR)

    # --- PathFinder::UpdateNodeCosts (807-928) --------------------------------------------------------------------------------------------
    def UpdateNodeCosts(self, node):
        if node is None:
            return False
        scene = self.scene
        GetTerrMatter = scene.GetTerrMatter
        nd = self.m_NodeDimension

        oldMaterials = list(node.AdjacentNodeBlockingMaterials)
        oldFreeHeight = node.FreeHeight
        oldClearLeft = node.ClearLeft
        oldClearRight = node.ClearRight

        def getStrongerMaterial(first, second):
            return first if first.integrity > second.integrity else second

        # How much room there is at this node: air up from the surface (or the centre), and to either side a little over the surface.
        x = int(node.Pos[0])
        node.Surface = self.SurfaceUnder(node)
        # The floor a body at this node stands on: the first solid pixel from the centre down to a node below it.
        centreY = int(node.Pos[1])
        floor = -1
        if node.Surface >= 0.0:
            floor = int(node.Surface)
        elif GetTerrMatter(x, centreY) == MATERIAL_AIR:
            for y in range(centreY + 1, centreY + nd + 1):
                if GetTerrMatter(x, y) != MATERIAL_AIR:
                    floor = y
                    break
        else:
            floor = centreY  # Buried: no room at all.
        frm = floor - 1 if floor >= 0 else centreY
        free = 0
        while free < PathNode.c_ClearanceReach and GetTerrMatter(x, frm - free) == MATERIAL_AIR:
            free += 1
        node.FreeHeight = free
        sideY = floor - 10 if floor >= 0 else centreY
        # (Out to two nodes a side.)
        left = 0
        while left < nd * 2 and GetTerrMatter(x - 1 - left, sideY) == MATERIAL_AIR:
            left += 1
        right = 0
        while right < nd * 2 and GetTerrMatter(x + 1 + right, sideY) == MATERIAL_AIR:
            right += 1
        node.ClearLeft = left
        node.ClearRight = right

        # Look at each existing adjacent node and calculate the cost for each. Only down and right (and the two right diagonals) are
        # calculated here; the other side pulls its up-and-left data from the other node's down-and-right (UpdateNodeList).
        mats = node.AdjacentNodeBlockingMaterials
        adj = node.AdjacentNodes
        if adj[RIGHT] is not None:
            # Walking is sampled along the ground, when both cells have ground in them: a band just above the surface at each column.
            # Cells without ground get a band just above the centre.
            upper = (0.0, -10.0)
            lower = (0.0, -4.0)
            here = node.Pos
            there = adj[RIGHT].Pos
            groundHere = self.SurfaceUnder(node)
            groundThere = self.SurfaceUnder(adj[RIGHT])
            if groundHere >= 0.0 and groundThere >= 0.0:
                here = (here[0], groundHere)
                there = (there[0], groundThere)
            mats[RIGHT] = getStrongerMaterial(
                self.StrongestMaterialAlongLine((here[0] + upper[0], here[1] + upper[1]), (there[0] + upper[0], there[1] + upper[1])),
                self.StrongestMaterialAlongLine((here[0] + lower[0], here[1] + lower[1]), (there[0] + lower[0], there[1] + lower[1])))

        # Down, and the diagonals, go from this node's centre to where a body stands at the other node (see StandingPoint).
        if adj[DOWN] is not None:
            # One line, down the column.
            mats[DOWN] = self.StrongestMaterialAlongLine(self.StandingPoint(node, 3.0), self.StandingPoint(adj[DOWN], 3.0))

        if adj[UPRIGHT] is not None:
            offset = (2.0, 2.0)
            here = self.StandingPoint(node, 5.0)
            there = self.StandingPoint(adj[UPRIGHT], 5.0)
            mats[UPRIGHT] = getStrongerMaterial(
                self.StrongestMaterialAlongLine((here[0] - offset[0], here[1] - offset[1]), (there[0] - offset[0], there[1] - offset[1])),
                self.StrongestMaterialAlongLine((here[0] + offset[0], here[1] + offset[1]), (there[0] + offset[0], there[1] + offset[1])))

        if adj[RIGHTDOWN] is not None:
            offset = (2.0, -2.0)
            here = self.StandingPoint(node, 5.0)
            there = self.StandingPoint(adj[RIGHTDOWN], 5.0)
            mats[RIGHTDOWN] = getStrongerMaterial(
                self.StrongestMaterialAlongLine((here[0] - offset[0], here[1] - offset[1]), (there[0] - offset[0], there[1] - offset[1])),
                self.StrongestMaterialAlongLine((here[0] + offset[0], here[1] + offset[1]), (there[0] + offset[0], there[1] + offset[1])))

        for i in range(PathNode.c_MaxAdjacentNodeCount):
            oldMat = oldMaterials[i]
            newMat = mats[i]
            # Check if the material strength is more than our delta, or if a door has appeared/disappeared.
            delta = abs(oldMat.integrity - newMat.integrity)
            doorChanged = oldMat is not newMat and (oldMat.index == MATERIAL_DOOR or newMat.index == MATERIAL_DOOR)
            if delta > self.c_NodeCostChangeEpsilon or doorChanged:
                return True

        # Room that has changed enough to matter to a body counts as a change too.
        if abs(node.FreeHeight - oldFreeHeight) >= 4 or abs(node.ClearLeft - oldClearLeft) >= 3 or abs(node.ClearRight - oldClearRight) >= 3:
            return True

        # None of the updates was past our epsilon, so ignore it and pretend it never happened. (The room values stay as measured.)
        node.AdjacentNodeBlockingMaterials[:] = oldMaterials
        return False

    # --- PathFinder::GetNodeIdsInBox (930-952) ---------------------------------------------------------------------------------------------
    def GetNodeIdsInBox(self, box):
        result = []
        box = Box(box.x, box.y, box.w, box.h)
        box.Unflip()
        nd = float(self.m_NodeDimension)
        # Get the extents of the box's potential influence on PathNodes and their connecting edges.
        firstX = int(math.floor((box.x / nd) + 0.5) - 1)
        lastX = int(math.floor(((box.x + box.w) / nd) + 0.5) + 1)
        firstY = int(math.floor((box.y / nd) + 0.5) - 1)
        lastY = int(math.floor(((box.y + box.h) / nd) + 0.5) + 1)
        for nodeX in range(firstX, lastX + 1):
            for nodeY in range(firstY, lastY + 1):
                nodeId = self.ConvertCoordsToNodeId(nodeX, nodeY)
                if nodeId != -1:
                    result.append(nodeId)
        return result

    # --- PathFinder::GetNodeAverageTransitionCost (954-967) -- unused (radiatedCost is 0) -------------------------------------------
    def GetNodeAverageTransitionCost(self, node):
        total = 0.0
        count = 0
        for material in node.AdjacentNodeBlockingMaterials:
            cost = material.integrity
            if cost < FLT_MAX:
                total += cost
                count += 1
        return total / max(float(count), 1.0)

    # --- PathFinder::UpdateNodeList (969-1010) ---------------------------------------------------------------------------------------------
    def UpdateNodeList(self, nodeVec):
        grid = self.m_NodeGrid
        anyChange = False
        for nodeId in nodeVec:
            if self.UpdateNodeCosts(grid[nodeId]):
                anyChange = True
        if anyChange:
            # Each PathNode's Up and Left direction Materials are matched to the respective neighbour's opposite direction Materials.
            for nodeId in nodeVec:
                node = grid[nodeId]
                adj = node.AdjacentNodes
                mats = node.AdjacentNodeBlockingMaterials
                if adj[RIGHT] is not None:
                    adj[RIGHT].AdjacentNodeBlockingMaterials[LEFT] = mats[RIGHT]
                if adj[DOWN] is not None:
                    adj[DOWN].AdjacentNodeBlockingMaterials[UP] = mats[DOWN]
                if adj[UPRIGHT] is not None:
                    adj[UPRIGHT].AdjacentNodeBlockingMaterials[DOWNLEFT] = mats[UPRIGHT]
                if adj[RIGHTDOWN] is not None:
                    adj[RIGHTDOWN].AdjacentNodeBlockingMaterials[LEFTUP] = mats[RIGHTDOWN]
        return anyChange

    # --- PathFinder::MarkBoxNavigable (1012-1022) / MarkAllNodesNavigable (1024-1039) ------------------------------------------------
    def MarkBoxNavigable(self, box, navigable):
        for nodeId in self.GetNodeIdsInBox(box):
            self.m_NodeGrid[nodeId].m_Navigable = navigable

    def MarkAllNodesNavigable(self, navigable):
        for node in self.m_NodeGrid:
            node.m_Navigable = navigable

    # --- PathFinder::GetPathNodeAtGridCoords (1041-1044) / ConvertCoordsToNodeId (1046-1062) -------------------------------------------
    def GetPathNodeAtGridCoords(self, x, y):
        nodeId = self.ConvertCoordsToNodeId(x, y)
        return self.m_NodeGrid[nodeId] if nodeId != -1 else None

    def ConvertCoordsToNodeId(self, x, y):
        if self.m_WrapsX:
            x = x % self.m_GridWidth  # C: x % w, then + w if negative == Python's %
        if self.m_WrapsY:
            y = y % self.m_GridHeight
        if x < 0 or x >= self.m_GridWidth or y < 0 or y >= self.m_GridHeight:
            return -1
        return (y * self.m_GridWidth) + x
