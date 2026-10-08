# Navigation challenges

What makes getting the AI to move well hard, as found while moving it into the engine (branch `ai-overhaul`, versions 8.0 to 8.1.53).
`AI_PLAN.md` is the plan and the history. This file is the list of problems that keep coming back, why each one is hard, what has
been done about it, and what is still open. It is meant to be read before changing the pathfinder or the route-follower.

## How it fits together now

Four parts take a unit from where it is to where it is sent.

| Part | Where | What it does |
|---|---|---|
| The path grid | `Source/System/PathFinder.cpp` | Nodes 24 px apart. Each node knows its floor, its head room and the air either side of its centre. A* over the steps a unit can take. |
| The route | `PathFinder::CalculatePath` | The cheapest chain of steps, turned into points (one per step) and a kind for each step. |
| The route-follower | `Source/Entities/AHumanMovement.cpp`, `AHuman::MoveAlongRoute` | Walks, crawls, climbs ladders, leaps and plans jet flights to follow the points. Notices being stuck. |
| The pilot | `AHuman::PilotFlight` | Flies one flight from take-off to landing, pulsing the jet. |

The kinds of step the grid offers:

| Step | Notes |
|---|---|
| Walk, crawl | To a neighbour on the same floor. Priced by head room. |
| Step over, mantle | Over something low, or up onto a ledge within arm's reach. |
| Stairs | Two nodes up for one across, for legs that take them. |
| Jump rungs | Straight or diagonally up a column, one step per node, then a step sideways onto a floor beside the column. Jet units only. |
| Flight | From a floor edge to another floor edge up to 8 nodes across and 12 up, checked as up, across and down. |
| Ladder | Up, down, off the side or over the top. Only for units whose jet can't do the climb. |
| Leap | Across a gap or onto a low lip on the legs, two to five nodes. New in 8.1.46. |

Lua sets where a unit is going and how it holds itself. The engine does all the moving. Mods can still press keys themselves.

## The challenges

### 1. The grid is coarse next to the body

A soldier is about 20 px wide and 44 px tall. The nodes are 24 px apart. Each node measures the air beside it only along its centre row,
and each step is checked along one line between node centres. So the grid misses things a body hits:

- **Thin lips and edges between rows.** A platform edge that sits between two node rows is invisible to the clearance measure. Routes
  went straight up past it and the shoulder caught it. Fixed for climbs in 8.1.37 by tracing both edges of the body along each rung.
- **Corners cut by straight legs.** A route's leg between two points can pass through the corner of a ledge the nodes went round.
  Anchors (each node's point moved off nearby walls) and a fall-back to node centres when a leg cuts through solid help, but don't cure it.
- **One line for a whole body.** Most steps are still checked along a single line. The leap (8.1.47) is the first step checked along
  its real arc with the head, middle and feet. Everything else should be checked the same way.

### 2. The route and the follower disagree about the shape of a move

The grid checks a move one way and the follower then does it another way.

- **Flights are one point.** A flight is one step in the route, so the route shows a single straight line from take-off to landing.
  The grid checked an up, across and down shape; the pilot used to fly a straight line at the landing and cut the corner the route went
  round. 8.1.39 makes the flight go by way of the route's corner when the straight line is blocked.
- **Climbs are one step too.** A climb up a column is planned as one step from the take-off to the top rung beside the landing, plus
  a sideways step on. The follower has to rebuild where the column is, where to stop and which way to step.
- **The follower has its own tests.** Whether a take-off is possible is decided by the follower's own look at the terrain
  (`FlightWayClear`, `ShaftColumn`, the corner test), not by what the grid checked. When the two disagree, the grid sends the unit to a
  take-off the follower then refuses, and the unit stands there.

**What would cure it:** the route should carry the shape of each move (the column, the corner, the landing), and the follower should
check only what the grid couldn't know (other units, doors, fuel).

### 3. Choosing where to take off

This is the problem seen most in play: a unit stands under a ledge it wants to get onto and never gets there.

- **The take-off is wherever the column starts.** Jump rungs only go straight up from a node, and the landing step only goes one or two
  nodes sideways. So the grid can only offer a climb from right beside the ledge, which is often under its lip.
- **Flights only start from floor edges.** On flat ground near a ledge, no flight link exists, so stepping back to get a clear run up
  and over isn't something the grid can express.
- **The follower only looks half a body each way** for an open column. Under an overhang wider than that it finds nothing.
- **Going round vertically.** Units go round things sideways easily, because walking round is in the grid. Going out from under
  something, up past it and back over the top isn't. Traces from 8 October show the same take-off refused dozens of times with "way up
  blocked", for a landing 50 px straight up.

**Under way:** the follower is to search further along the floor for a spot where the way up and back across is clear, walk there and
fly up and over. The grid is to offer flights from any floor node, not only edges.

### 4. What to remember about a failure

When a unit fails, the next route has to be different, or it fails the same way again. What to mark is not obvious.

| Marked | Problem |
|---|---|
| The route's next point (the first approach) | For a climb this is the top of the climb, which every way up there passes, so every alternative paid the same and the same route came back. |
| The landing | Made every route past the landing dearer, walks included, though the landing was rarely what failed. |
| One step from take-off to landing (8.1.38) | Matched flights, but not climbs, which are planned rung by rung. |
| Any step from near the take-off to near the landing (8.1.52) | Matches both. Pushes the next climb at least two columns over. |

The marks last 20 to 30 s and are shared with the unit's team at half strength, so the next unit doesn't learn the same way.
Repeated failures at the same place make the mark stronger, so a route of smaller steps can win (onto the ledge first, then over).

### 5. Waits that never end

Several fixes added a wait that also reset the stuck timer, so a unit that never finished waiting was never treated as stuck and
nothing moved it on. Each one looked like "the unit just stands there":

- The settle before a long flight waited for an upright body with no time limit. A soldier whose stance leans never counted as upright.
  Fixed in 8.1.51 with a 1.5 s cap.
- A background ladder that continues behind a floor: the unit pressed down, took hold, found the floor and let go, hundreds of times.
  Fixed in 8.1.53.
- Standing at a take-off it couldn't use until the 6 s stuck handling. Shortened to 2.5 s in 8.1.52.

**Rule:** any wait that resets the stuck timer needs its own time limit.

### 6. The jetpack is hard to fly precisely

- **Pulses.** The pilot pulses the jet. Between pulses the unit counts as not jetting, which let ladders catch it mid-flight. 8.1.42
  counts an AI unit as jetting for the whole of a planned flight.
- **Momentum.** Taking off mid-stride put the walk's speed into the flight, often the wrong way. Take-offs now steady first, and longer
  flights steady longer (8.1.43, capped in 8.1.51).
- **Fuel.** Every unit's tank and push differ, so a climb's fuel is worked out from the unit's own jet. Tall climbs refuel in stages
  on the way down and climb on at a fuel level, not a time.
- **The hardest quick jump wins.** Priced on time alone, a hard jump through a hatch beat two easy hops beside it. Flights and jumps
  now carry a risk price (a nearly empty tank, a tall climb, a low ceiling at the landing, a take-off pressed to a wall).

### 7. There was no jump

Until 8.1.46 the only way off the ground was the jet, so every small gap and knee-high lip was a jet problem: fuel, a burst, a pilot,
steadying. A leap on the legs (its own control, Leap) is now in the engine, the grid plans leaps (8.1.47) and the follower takes them
(8.1.48). The leap's reach comes from the unit's own leap height and speed under gravity, and its arc is checked against the body.
Crabs don't leap yet, and a failed leap has no failure memory of its own.

### 8. Ladders

- **Collisions.** Rungs are a material (Ladder) that living bodies pass through, so a ladder no longer blocks a walk or a flight.
- **Two kinds of ladder.** Material ladders are climbed by the engine. Background ladders are run by a Lua script that holds or steers
  any unit standing in one, which fought the follower. The script now leaves alone a unit that is jetting or climbing.
- **Who routes over them.** Routed for every unit in 8.1.40, ladders took over Bywater's routes and units got stuck on them, so only
  units whose jet can't do the climb route over them again (8.1.41).
- **Shaking.** The climb's speed was worked out from where physics had left the body, so each frame undid the last. Fixed in 8.1.44.

### 9. Facing and aim

An AI unit in a fight keeps facing its aim while it moves. Walking backwards is fine; lying down and crawling backwards isn't, and units
under something low the other way crouched there walking backwards. 8.1.45 makes a crawling AI unit face the way it goes. The same rule
had units walking backwards to a jump and taking off facing away from it.

### 10. Lua and the engine pressing the same keys

The old behaviours in Lua (cover, healing, squads, RTS orders) pressed movement and jet keys directly and fought the engine's follower.
They now set destinations, stances and short tactical moves only. Scripts for drop ships, rockets and crabs have been ported into the
engine (8.1.34 to 8.1.36). Mods that press keys themselves still work, so the engine has to cope with keys it didn't press.

### 11. Prices that pull against each other

Every step has a price, and the route is the cheapest sum. Small changes shift whole routes:

- A walk node costs 1. A jump costs more the higher it goes, but not by the square: at the square a short cliff was worth a long walk
  round; at too little, units leapt over whole courses.
- A fall costs by its height, or routes dropped off the outside of a bunker and climbed back in.
- A failure mark of 25 is about a twenty-node detour. Too low and it changes nothing; too high and one bad try sends units the long way.
- The cheapest way isn't always the surest. Risk prices help, but they are guesses.

### 12. Testing

- **Gyms and real maps differ.** The gyms (AIGym, Sky, Tower, Flight, Bywater, Hemslock) measure progress, but most bugs found lately
  were on maps the gyms don't cover (Neverending Spaceport).
- **Benches are slow** (minutes per suite), and the game in use blocks the Final build, so changes are committed, pushed and built first
  and tested after.
- **Traces are opt-in.** Launch with `CCCP_AI_LOG=all` to trace every unit to `LogConsole.txt`, which is only written when the game
  closes. In PowerShell:

```powershell
$env:CCCP_AI_LOG = "all"; Set-Location "C:\Users\Liamn\Desktop\cortex\Cortex-Command-Community-Project"; & ".\Cortex Command.exe"
```

## Open problems, most important first

1. **Going round vertically.** Take-offs from further out, and flights from any floor node (challenge 3).
2. **The route carrying the shape of each move,** so the grid and the follower agree (challenge 2).
3. **Body-shaped checks for every step,** as the leap has (challenge 1).
4. **Leaps for crabs,** and failure memory for leaps.
5. **Ladders for every unit** once the follower handles every way ladders meet the rest of a route.
6. **Gym courses for the spaceport cases:** a ledge straight overhead, an overhang wider than a body, a background ladder behind a floor.
