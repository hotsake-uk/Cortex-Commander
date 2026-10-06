# Test results: af7836b6 (158c93ba to c553485a and the handover)

Debug build of af7836b69. Every suite run 3 times (`*_1..3.log`, `results.txt`), then traced reruns: sky bunker courses 1 and 2,
Bywater courses 1, 2 and 5, outdoor courses 5 and 15 (`*_traceN.log`). AIBunker.lua on the test side now also has my Bywater course 5
start fix and `AIBUNKER hurt` / `died after` lines (committed with these results).

## Totals

| Suite | 2362a79 | af7836b | Notes |
|---|---|---|---|
| Outdoor | 15/16 x3 | 15/16 x3 | crab over the hill fails 3/3; **through the room** slowed from 14.1 s to 20-22 s (see 1) |
| Combat | all resolve | all resolve | |
| New sky bunker (7 courses) | - | **17/21** | up the hatch 1/3, bottom corridor to the top room 1/3, the other five 3/3 |
| Bywater | 7/15 | 5/15 | no deaths in the plain runs; course 5 and course 1 fail 3/3 |

Per-course on the new sky bunker: down the hatch 7.5 s x3; across the hub 5.5-9.5 s; up the stairs 21-46 s; down the stairs
23-46 s; bottom to the top gallery 51-59 s (close to the limit); up the hatch 44.5 s once, else ~92 px short; bottom corridor to the
top room 49.5 s once, else ~110 px short.

## Checklist against your expectations

- **c553485a, a step off an edge is not a crawl**: sky "down the hatch" 3/3 at 7.5 s and "down the stairs" 3/3. No crawl
  labels at those points in the route prints. OK.
- **d4b81ebe, a fall costs by its height**: Bywater "top room to the bottom corridor" arrived 3/3 (37-51 s). I did not check the
  route shape line by line. Outdoor "down into the cave" and "down the slope" still arrive. OK.
- **ce08008f, the step off a climb lands**: **not met.** Sky "up the hatch" still drops back (see 2). There is also a stale-route problem that
  may come from this commit's re-path change (see 3).
- **8caa4f7d, the rebuilt sky bunker**: 17/21. Every failure is a movement fault, as you predicted.
- **1cf3a694, the crab's jet is lift only**: **not met.** Crab over the hill 0/3 (was 1/4). See 5.
- **158c93ba, the climb keeps to its column**: Bywater "mid left room to the right column" 2/3 (was 3/3). Bywater course 1 still 0/3.
  It may also have caused the outdoor slowdown (see 1).
- **2362a79e**: prone crawling in corridors stays fixed.

## Faults, most important first

### 1. A prone unit at a jump point that only pops at its height (outdoor regression, "through the room")

`AIGym_trace5.log`: the route is the same as before (`612,204 -> 660,180(2) -> 852,204`). At the foot of the 24 px step into the room, with the
doorway low over it, the unit goes prone (`crawl: going prone, wpt dx 44 dy -24`). The step is a Jump point that now pops only with the
unit at its height (158c93ba), and a prone unit may not jet, so it creeps against the step from 6 s to 11 s until `jet: stuck` hops
it up. Before 158c93ba it popped the point as it passed and walked on. Suggest: a Jump step of under about a third of the body is a step
for the legs. Don't go prone for it and don't hold it to the height rule. Or let the crawl rule yield when the waypoint is a Jump.

### 2. The climb out of a hole lands on the side away from the next point (sky hatch, Bywater course 5, Bywater course 1)

This is now the main indoor fault, seen in three places:
- Sky "up the hatch" (`AIBunker_trace2.log`): the climb up the hatch at 1788 tops out at 1812,335, on the *right* lip. The next point
  is 1764,348 and then 1700,342, both to the *left*, across the mouth. It drifts left over the hole and falls back (5-9 s), twice.
- Bywater course 5 (`AIBywater_trace5.log`): the route drops into a small pit (2220,1116 / 2244,1140) and jumps out. The unit comes up on
  the *left* lip (popped 2268,1073 from 2238,1072, 30 px off), its next point 2292,1073 is to the right across the pit, and it
  falls back in. This repeats from 5 s to 28 s.
- Bywater course 1: the same at its first hatch (2362a79 trace; still 0/3 here).
Suggest: when a climb's next point is across the column, aim the top of the climb at the lip on that side (the climb's drift target
should be the side of the next waypoint, not the nearest lip). Or don't pop the jump point until the unit stands on the far side.
Also "pop: reached" from 30 px away for a jump point looks like the height rule is met by the wrong node.

### 3. After falling back down a hatch the unit keeps a one-point route for 30+ s (sky "up the hatch")

After the second fall (14-16 s) the route is `path 1`: only the goal 1700,342, straight above the corridor below. For the remaining 45 s
there is no `path for` line at all, only `climb: under a ceiling at 405, keeping out from under it` over and over. ce08008f made the
12 s re-path wait for a climb to end. If a fall out of a climb doesn't count as the end, no re-path ever fires. Suggest: a fall out of a
climb ends it, and re-paths when the route's next point is above and its kind is not a Jump.

### 4. Instant deaths at full health in the Bywater middle room (traced runs only this time)

`AIBywater_trace1.log` and `AIBywater_trace2.log`: "middle to the mid left room" `died after 5.9 s, last seen at 1825,520 vel -0.2,-2
health 100`, and `4.2 s ... 1826,524 vel 0.8,-0.4 health 100`. No `hurt` line before either: the unit went from full health to gone in
one frame while jetting slowly up its first step (1812,432). That fits being gibbed by a door closing on it, not a fall. The gym
hands the scene's doors to team 0. A door over 1812,4xx closing on a unit jetting up into it is the likely cause. I'll confirm with a capture next
round if you want it. Add the request.

### 5. The crab rises far past a small climb and drifts backwards (crab over the hill, 0/3)

`AIGym_trace15.log`: at the first wall ahead the climb asks for `wpt dx 11 dy -23`. The crab rises from 727 to 615 (and later to 551),
drifting left at about 0.9 m/s, burns the whole tank, and falls to 994, losing 52 health (10-11 s). It never gets back. With lift
only, nothing moves it forward and nothing cuts the jet when it has the height. Suggest: cut the crab's jet at the climb's height
(plus a little), and drive its legs forward while it rises.

## Open requests

1. The Bywater course 2 trace is `AIBywater_trace2.log`.
2. Unit lines: only the Soldier Light and the Dreadnought are run (`Soldier Light height 100 radius 28 jump height 440 px aim range 1.57`,
   `Dreadnought height 110 radius 42 jump height 169 px aim range 0.5`).
