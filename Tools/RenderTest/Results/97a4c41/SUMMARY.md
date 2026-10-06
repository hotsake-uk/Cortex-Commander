# Test results: 97a4c419 (stairs on the legs, the climb's fuel and speed, doors of our own)

Debug build of 97a4c4193. Every suite run 3 times, traces of sky course 1, the crab (outdoor 15) and Bywater course 1, the stairs
courses with the Dreadnought, and a grid dump around Bywater 1736,912.

## Totals

| Suite | d3c6d8d | 97a4c41 | Notes |
|---|---|---|---|
| Outdoor | 16, 15, 16 | 15, 15, 16 | crab over the hill 1/3 (40.1 s; else ~500 px short at 1551-1578,597-676) |
| Combat | all resolve | all resolve | **firefight: both sides dead 3/3** (it was team 2 surviving in most earlier runs) |
| Sky bunker | 21/21 | **18/21** | everything 3/3 except **bottom corridor to the top room 0/3** (the new Doors B shaft) |
| Bywater | 6/15 | **6/15** | course 4 3/3, course 3 2/3, course 2 1/3 (one of its misses an instant death); courses 1 and 5 0/3 |

Sky times: up the stairs 4.5-6.5 s, down the stairs 4.5 s (were 6.5 and 15.5), across the hub 5.5, the hatches 6.5-8.5, the gallery 7.5.

## Checklist

- **Stairs are walked**: met. Routes `2076,348(0) 2100,300(6) 2124,252(6)`, courses 4.5-6.5 s.
- **Crab on the stairs** (your request): with the Dreadnought, AI driven, **up the stairs arrived in 11.5 s and down the stairs in 5.5 s**
  (`stairs_crab_course5.log` / `course6.log`). Its route print shows the (6) steps too: `2060,339 2076,348(0) 2100,300(6) 2124,252(6)
  2160,244(0)`. So the crab was offered the walking edge (contrary to "a crab's no") and it walked it.
- **Doors of our own / door manners**: **not met**, see 1. No `door:` line in any trace.
- **The climb's tank check**: **not met** for Bywater course 1, see 2.
- **Bywater course 5 climbing at 45 s**: still 0/3; no trace this round.

## Faults

### 1. The sky bunker's Doors B shaft: the door still blocks, the route is cut short, and the unit stands at its end (0/3)

`AIBunker_trace1.log`. The unit's first route: `status 0, 5 nodes, cost 600048.375000, from 1700,439 to 1520,245`, ending
`1764,348(2) 1692,348(0)`, short of the goal. The 600000 is the door penalty, so the hatch's leaves still block team 0's grid, although the
gym sets every door to team 0 (`actor.Team = 0` for ADoors at 5 s) and ADoor::SetTeam should re-sample. Perhaps SetTeam from Lua doesn't
reach ADoor::SetTeam, or the re-sample runs before the leaves are where they will be. The second route (from 1734,439) is to
`1692,341`, the cut-short end now taken as the goal. The unit walks there (9 s) and then stands with `path 0` for 50 s: a cut-short route
that ends is never asked again. No `door: waiting` line at any time.
Two things to fix: the door in the grid, and a unit whose route was cut short should re-path (or wait) when it reaches the cut, not
stop for good.

### 2. Tall climbs still start on a part tank and run dry (Bywater course 1, 0/3)

`AIBywater_trace1.log`:
- 4-7 s: a 167 px climb (`wpt dx 3 dy -167`) from about 829 fuel, reaching the top with **55**.
- 31-38 s: the 329 px climb (`wpt dx 31 dy -329`) starts with **787** fuel. It runs dry at 1430,405 (fuel 20), drifts left to
  1194,348 and falls to 1216,754, then 1402,947: 10 s lost and back at the foot.
- 9-12 s: still at 1812,860 until `jet: stuck`.
- `climb: under a ceiling at 765/850, keeping out from under it` still fires at the foot of two climbs (25-28 s, 41-43 s).

### 3. Instant death beside a door in Bywater

Run 1, "mid left room to the right column": `died after 36.1 s, last seen at 2079,872 vel 0.1,-1.5 health 100`, with no hurt line. That is
the full-health-then-gone pattern again. Bywater has `Door A` at (2076,792), right over that spot, so most likely its door shut on the unit.

### 4. Combat firefight

Both fighters died in all three runs. Earlier rounds mostly ended with team 2 alive. No change of yours touches combat directly. Could
door or ray changes affect line of sight or cover?

## Bywater course 1's stand at 1756,924 (your request 6): the grid dump

`bywater_dump_1736_912.log`. It's a step, not a wall. A small stair down to the left:
- 1740,924: `surface 924 ground yes free 48`, the floor the unit stands on.
- 1716,924: `surface 935 ground yes free 70 clear 48/11`, 11 px lower.
- 1692,948: `surface 948 ground yes free 96`, 13 px lower again. 1692,924 is air above it (`free 96 clear 48/20`).
- 1716,900: `free 36`, 1716,876: `free 12`. So above 1716 the head room is low, a crawl band over the first step.
- 1764,924 / 1788,924-972: the hatch column (`free 96`, clear 10-36 / 35-48).
So the route's 1716,915 is the first step down, and the crawl step to 1692,928 goes under the low lintel over it.
