# Test results: 3e9b13f4 (with 7d8b18a7, ba4f53b1, 07c5feeb)

Debug build of 3e9b13f45. Every suite run 3 times (`*_1..3.log`, `results.txt`), plus traced reruns of Bywater course 3 and sky
bunker course 2. 7d8b18a and ba4f53b were superseded while I tested them; `../ba4f53b/` holds one partial outdoor pass of ba4f53b.

## Totals

| Suite | af7836b | 3e9b13f | Notes |
|---|---|---|---|
| Outdoor | 15/16 x3 | 15/16 x3 | through the room back to **14.1 s** x3; crab flat run 3/3 (16-20 s); crab over the hill 0/3 |
| Combat | all resolve | all resolve | the "move" mover died twice, short once (it has varied all day) |
| Sky bunker | 17/21 | **17/21** | run 2 was 7/7 with all times under 29 s; up the hatch 1/3, bottom corridor to the top room 2/3, gallery 2/3 |
| Bywater | 5/15 | **3/15** | top room to the bottom corridor 0/3 (was 3/3). See 1 |

ba4f53b alone (one outdoor pass): through the room 14.1 s, **crab flat run GAVE UP** (fell off the beam), crab over the hill failed.
07c5feeb fixed the flat run.

## Checklist

- **Through the room** back to 14 s: met.
- **Crab arriving**: flat run met; over the hill **not met** (0/3; GAVE UP at 1092-1427, below the hill).
- **Sky 21/21 or close**: 17/21, same total, but faster and one perfect run.
- **Bywater courses 1 and 5 improving**: **not met**, 0/3 each.
- **No one-point routes held for 45 s**: met. `climb: down again, asking for a new route` fires after a failed climb.
- **d4b81ebe (from last round, which I missed)**: Bywater "top room to the bottom corridor" still routes down the *outside* wall:
  `1548,372 ... 1524,516(1) 1524,540(3) ... 1476,780(3) 1668,804(0)`, a 400 px drop at x 1476-1548. The route print now comes from
  the unit's own agent, so it is the route the unit really has.

## Faults, most important first

### 1. A long fall keeps the sideways speed it started with (Bywater course 3: 3/3 to 0/3)

`AIBywater_trace3.log`: the unit walks off the corridor edge at 1500,372 moving left at 3.3 m/s and falls the outside drop with it. At
14 s it passes the route's column (1524/1476) 60-75 px to the left (x 1449-1465, falling at 13.8), popping each point as "passed".
At 15 s it is at 1464,786 with vel -4.1,12. The landing is 1476,796 and the next point 1668,804 is to the *right*. It sails on left
to 1362,858 and lands outside at ground level (1034,1110), with no way back in time. It was prone in the air throughout
(`step kind ... prone true` from 14 s). Before this round it made the same drop 3/3. Suggest: on a Fall step, brake sideways speed
with the jet towards the column of the landing (the same "against the speed" lean the crab now has), and step off a tall drop at
walking speed, not running. Also, the route should not use the outside drop at all (d4b81ebe's aim).

### 2. At the top of a hatch the climb still pushes away from the landing (sky "up the hatch", 1/3)

`AIBunker_trace2.log`: the climb at 1788 reaches the height (4 s), then `climb: under a ceiling at 310, keeping out from under it`
pushes it *right* to 1841. Its landing is 1764,348 and then 1700,340, both left. 310 is the upper corridor's own ceiling (the
corridor's floor is about 358). 7d8b18a capped the *lip* probe at the head's height at the waypoint. This message comes from the
other "keeping out from under it" branch, which still probes from the unit. It falls back, re-paths, climbs again, and arrives in
44.5 s on the third try.

### 3. A long wait at the foot of a small climb (Bywater course 3, 7-11 s)

The unit stands still for 4 s at 1651,342 with a full tank under its next point 1620,300 (`dx -30 dy -41`), then climbs it.
Cause unknown from the trace: there are no AITRACE lines in those 4 s.

### 4. Crab over the hill (0/3)

Still falls back below the hill (1092,1172 / 1427,893 / 1292,1059). No crab trace this round. Tell me if you want one.

## Test-side change: Bywater's "middle" moved off the hatch

The capture of the middle of Bywater (`CCCP_BUNKER_LOOK=1820,560,2`) shows (1820,580) is not a room. It is the vertical shaft at x
1761-1846 with a Doors B floor hatch across it at about y 562 (scene object at 1800,540; two rotating leaves pivoting at
1764,573 and 1836,573). Units started there stood on the leaves and were gibbed when one swung (died at full health, 3 of the last 9
runs). The start of "middle to the mid left room" and the goal of "bottom right to the middle" are now (1950,590), in the open room
east of the shaft (floor about 610). Both courses' numbers start again from the next run.
