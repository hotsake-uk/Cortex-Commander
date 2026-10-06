# Test results: 1f2f3d60 (squads keep a place in line)

Debug build of 97a4c4193 (no C++ change since). Every suite run 3 times. No traces this round.

## Totals

| Suite | 97a4c41 | 1f2f3d6 | Notes |
|---|---|---|---|
| Outdoor | 15, 15, 16 | 15, 15, 15 | crab over the hill 0/3; crab flat run once 31.9 s (stood still 13 s) |
| Combat | all resolve | all resolve | firefight both dead 2/3; new squad course, see 1 |
| Sky bunker | 18/21 | 18/21 | Doors B shaft still 0/3 (stood still 47-50 s at 1691-1720,343: the cut-short route, as in 97a4c41) |
| Bywater | 6/15 | **4/15** | course 2 0/3 (stood still 14-31 s at 2012-2063,848); one more instant death under a door |

Not much in this push touches indoor movement apart from the climb fuel trim, so I read the Bywater drop from 6 to 4 as mostly noise.
Course 2 failing 3/3 in the same spot is worth a trace next round.

## 1. The squad course (new)

`AICombat_*.log`, `AICOMBAT squad` lines.
- **On the move the line works.** Followers keep 50-60 px apart behind the leader (2 s: spread 236, minpair 61; 8 s: 228 / 50; 14 s:
  189 / 52).
- **When the leader stops they bunch, and pass it.** At 26 s a follower is at 3097, ahead of the leader at 3058 (minpair 17). At 32 s,
  when the leader turns back, minpair is 2. At the end (56-62 s) minpair is 6, then 1. The result lines say "closest two while the leader
  stood" 3, 19 and 0 px. The places in line don't hold when the leader is still: the followers walk onto and past it.
- **The leader stops well short of both its goals, every run.** Out: it stops at about 3086-3091 (14-20 s) with its goal at
  east+900 = 3345 (the beam runs to about 3485). Back: it stops at about 2860-2940 with the way-back goal at east+100 = 2545. The
  followers were behind it both times, so they weren't in its way. Something in the leader's own GOTO ends ~250-320 px early. Perhaps
  the squad changes give the leader an arrival radius or a wait for its followers?
- Harness note: the result line's "mover N px from goal" measured the leader against the *first* goal even after it was sent back.
  I changed it to "leader N px from the way-back goal" from the next run on (AICombat.lua).

## 2. Instant deaths under doors: now a pattern (Bywater)

Three in the last two rounds, all at full health with no `hurt` line, each right under a Bywater door:
- 97a4c41 run 1: 2079,872, under Door A at (2076,792).
- 1f2f3d6 run 3, "middle to the mid left room": `died after 32.8 s, last seen at 1569,693 vel 0.4,-0.9 health 100`, under Door A at
  (1560,624).
- (Earlier: the Doors B hatch deaths that moved the "middle".)
The gym hands every door to team 0, the units' team. The door manners in 97a4c4 should hold units 80 px short of a closed leaf, but
no `door:` line has appeared in any trace yet. These doors close on units of their own team. Does an ADoor of the unit's team
sense it and stay open, and is the gym's `actor.Team = 0` reaching the door as its team (see 97a4c41 fault 1, the 600048 route)?

## 3. Still open from 97a4c41

The Doors B shaft (route cut short, door still in the grid), and tall climbs on part of a tank. Both unchanged here.

## Addendum: Bywater course 2 traced (`AIBywater_trace2.log`, same build). The door manners deadlock with the door

The first `door:` lines yet:
```
AITRACE door: waiting for Door Slide Long (state 0) 70 px off
AITRACE door: waiting for Door Slide Long (state 0) 69 px off
AITRACE door: waiting for Door Slide Long (state 0) 72 px off
```
From 33 s the unit stands at 2018,835, 70 px short of Bywater's Door A (2076,792, a `Door Slide Long`), and the door stays at state 0
(closed) for the rest of the minute. **The door never opens because the unit is waiting outside its sensor.** From
Base.rte/Scenes/Objects/Bunkers/BunkerSystems/Doors/Doors.ini (line 1095), `Door Slide Long`:
- has one sensor: `StartOffset (-106,-52)`, `SensorRay (0,104)`, `SkipPixels 8`, rotated with the door (`Rotation 90`). That's a
  single 104 px ray across the doorway itself, so only a body in the doorway is seen;
- has `SensorInterval 100`, `DoorMoveTime 1500`, `ResetDefaultDelay 1500`, `ClosedByDefault 1`: it opens over 1.5 s once it sees
  someone, and starts closing 1.5 s after it last saw someone.

So a unit holding 80 px short of a closed leaf waits for ever: the door is waiting for the unit. Suggest: walk *up to* a closed door of
our team, into its sensor ray (the doorway), and wait there. Don't climb or hover through it while it's moving. Pass through promptly
once it's open, because it closes 1.5 s after the last sighting, which would explain the instant full-health deaths under doors: a
unit lingering or hovering in a doorway as the door shuts on it. Other door presets may have other sensor shapes, so reading the
door's own sensors (offset and ray) would be safer than a fixed distance.

The rest of course 2's minute (20-33 s): it gets up the 45 px hatch at 1956 to 1956,830 and the corridor at 1980,833, walks right
towards 2100,833, and stalls under the door.
