"""MicroPather::Solve (Source/System/MicroPather/micropather.cpp 839-954), as the game runs it.

Plain A*, with MicroPather's particulars kept because they decide which of several equal-cost routes comes back:
  - the open list is kept sorted by totalCost = costFromStart + estToGoal; Push inserts AFTER entries of equal total (FIFO),
    Update (a cheaper way to a node already open) moves it to the FRONT of its equal-total class (OpenQueue::Push 79-108,
    OpenQueue::Update 132-162);
  - a node already closed that is reached more cheaply gets its parent and cost rewritten but is NOT reopened (Solve 915-935);
  - the adjacency list may name the same neighbour more than once (the jump chains do); each entry is processed in order;
  - the returned path runs from the start state to the end state inclusive; the cost is the end node's costFromStart.
"""

from bisect import bisect_left, bisect_right

SOLVED = 0
NO_SOLUTION = 1
START_END_SAME = 2

FLT_MAX = 3.4028234663852886e38


class _Rec:
    __slots__ = ("state", "costFromStart", "estToGoal", "totalCost", "parent", "inOpen", "inClosed")

    def __init__(self, state):
        self.state = state
        self.costFromStart = FLT_MAX
        self.estToGoal = FLT_MAX
        self.totalCost = FLT_MAX
        self.parent = None
        self.inOpen = False
        self.inClosed = False


class _OpenQueue:
    """OpenQueue: a list sorted by totalCost, with MicroPather's insertion rules."""

    def __init__(self):
        self.items = []  # _Rec, sorted by totalCost
        self.keys = []  # their totalCosts, for bisect

    def Push(self, rec):
        # Insert before the first entry whose total is strictly greater: after all equal ones.
        index = bisect_right(self.keys, rec.totalCost)
        self.items.insert(index, rec)
        self.keys.insert(index, rec.totalCost)
        rec.inOpen = True

    def Pop(self):
        rec = self.items.pop(0)
        self.keys.pop(0)
        rec.inOpen = False
        return rec

    def Update(self, rec):
        # The record's totalCost has been lowered; find it (its stored key is stale).
        index = self.items.index(rec)
        self.items.pop(index)
        self.keys.pop(index)
        # "If the node now costs less than the one before it, move it to the front of the list" -- then, if it is too high for its
        # new neighbour, walk right to the first entry with a total >= its own (so it lands at the front of its equal class).
        if index > 0 and rec.totalCost < self.keys[index - 1]:
            position = 0
            if position < len(self.keys) and rec.totalCost > self.keys[position]:
                position = bisect_left(self.keys, rec.totalCost)
        else:
            position = index
            if position < len(self.keys) and rec.totalCost > self.keys[position]:
                position = index
                while position < len(self.keys) and rec.totalCost > self.keys[position]:
                    position += 1
        self.items.insert(position, rec)
        self.keys.insert(position, rec.totalCost)

    def Empty(self):
        return not self.items


class SolveStats:
    def __init__(self):
        self.expanded = 0
        self.closed_rewrites = 0  # cheaper ways found to already-closed nodes (MicroPather rewrites them without reopening)


def Solve(graph, start, end, stats=None):
    """graph.AdjacentCost(state) -> list of (state, cost); graph.LeastCostEstimate(a, b) -> float.
    Returns (result, path_states, cost)."""
    if stats is None:
        stats = SolveStats()
    if start is end:
        return START_END_SAME, [], 0.0
    records = {}

    def rec_for(state):
        rec = records.get(id(state))
        if rec is None:
            rec = _Rec(state)
            records[id(state)] = rec
        return rec

    open_queue = _OpenQueue()
    start_rec = rec_for(start)
    start_rec.costFromStart = 0.0
    start_rec.estToGoal = graph.LeastCostEstimate(start, end)
    start_rec.totalCost = start_rec.costFromStart + start_rec.estToGoal
    open_queue.Push(start_rec)

    while not open_queue.Empty():
        node = open_queue.Pop()
        if node.state is end:
            path = []
            seen = set()
            walker = node
            while walker is not None:
                if id(walker) in seen:
                    raise RuntimeError("parent chain cycle in the solved path (closed-node rewrite hazard)")
                seen.add(id(walker))
                path.append(walker.state)
                walker = walker.parent
            path.reverse()
            return SOLVED, path, node.costFromStart
        node.inClosed = True
        stats.expanded += 1
        for state, cost in graph.AdjacentCost(node.state):
            if cost == FLT_MAX:
                continue
            child = rec_for(state)
            new_cost = node.costFromStart + cost
            if child.inOpen or child.inClosed:
                if new_cost < child.costFromStart:
                    child.parent = node
                    child.costFromStart = new_cost
                    child.estToGoal = graph.LeastCostEstimate(child.state, end)
                    child.totalCost = child.costFromStart + child.estToGoal
                    if child.inOpen:
                        open_queue.Update(child)
                    else:
                        stats.closed_rewrites += 1
            else:
                child.parent = node
                child.costFromStart = new_cost
                child.estToGoal = graph.LeastCostEstimate(child.state, end)
                child.totalCost = child.costFromStart + child.estToGoal
                open_queue.Push(child)
    return NO_SOLUTION, [], 0.0
