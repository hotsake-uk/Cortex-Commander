# The original AI on our gyms (2026-10-07)

The question: are we ahead of, or behind, where we started on real bunkers?

## What was run

- **Build:** commit bc93d5a5e (`../cccp-ai-baseline`, a git worktree), the last commit before the AI work. Its pathfinder is
  upstream's, unchanged. Its AI scripts differ from upstream only by about 190 lines from the earlier sandbox work (units keep after
  an assigned enemy and get unstuck). Pure upstream (20dfb3ea5) can't run the gyms: it lacks the sandbox scripting and console-log
  hooks they need.
- **Gyms:** today's AIBunker.lua, with four newer script calls patched out for that build (grid dumps, step kinds, the actor-sized
  path call, so the path printouts there use the older CalculatePath).
- **Courses:** Bywater Barracks (the BB+ mod, 5 courses, 35 wall ladders of its own) and the ladder tower (`CCCP_BUNKER_TOWER=1`,
  8 courses, two shafts with wall ladders all the way up), three runs each. Logs are in this folder.

## Results

| Gym | Original AI (bc93d5a) | Ours, before today's climber | Ours, new climber |
|---|---|---|---|
| Bywater | 2/5 and 1/5 in the two complete runs (the third ended early with 2 arrived) | 3-7/15 across the last rounds | 4/15 (one round) |
| Ladder tower | **22/24** (7, 7, 8) | 20/24 | 30/32 (8, 6, 8, 8) |

- Tower times, original: "A up shaft one to C" 5.5-10.5 s, "B up shaft two to D" 10.5-25.5 s. Ours with the new climber: 9.5-23.5 s
  and 7.5-27.5 s.
- Original failures: "A up both shafts to D" 2/3 (stuck at the foot of shaft two or 150 px short).

## What it shows

1. **We introduced the ladder block.** The original pathfinder routes straight up a laddered shaft (`1596,444 -> 1596,228`, a plain
   route). Ours, before 10aa2efb9, priced the same climb at 1,200,091, which counts as impossible. Our overhaul of the grid added head
   room and body-width checks, and those read a wall ladder's rungs as ceiling and wall. Causeless (CCCP's lead maintainer) said the
   routes ought to be fine and the block is in navigation; for ladders it was our own grid. Fixed in 10aa2efb9 (the grid samples
   several lines across each cell, so a body counts as fitting beside the rungs).
2. **On Bywater we are not clearly ahead.** The original scores about 30%; ours has been 25-45% across the last rounds, and the noise is
   as large as the difference. A month of AI work has not yet moved the real-map number in a way these runs can show.
3. **On the tower the original is already good** (22/24). Our old climb was worse (20/24); the new staged climber is about level or a
   little better (30/32), with similar times.

## What to do about it

- Keep comparing against this baseline: every change should beat the original on a real map, not only on our own gyms. The worktree
  stays at `../cccp-ai-baseline`; rerun with `Tools\RenderTest\AIBunker.ps1` there (its gym copy is patched as above).
- Treat our grid changes as suspects. They added rules the original didn't have (head room, body width, standing points); each one
  can block a real map the way the ladder rungs did. Before adding more, check what the original grid routes on Bywater, and make sure
  ours never refuses a route the original would take.
- Most of what is left is navigation (following the route): climbs, doors, snags, overshoots. That's where the work since has gone
  (the staged climber, door waits, corner correction, route checks in flight, ladder climbing).
