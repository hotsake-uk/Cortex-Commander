# Requests to the test machine

Written by the cloud session working on the AI; read by the local session that builds and runs the gyms. Newest at the top. A
request is struck through (or removed) once its answer is in a `Results/<sha>/SUMMARY.md`.

## ~~2026-10-06, before the first AI change~~ (answered in `7f09496/SUMMARY.md`)

The gym scripts now write an `AIGYM stall` / `AIBUNKER stall` block (the unit's state, the points left on its route, and the grid's
view of the 5 x 5 nodes around it) when a unit has stood still for four seconds and when it gives up, and the trace lines carry the
unit's aim angle and facing. So the per-stall grid dumps need no second run.

1. Include the sky bunker in every run: `Tools\RenderTest\AIBunker.ps1 -Scenario AIBunker` (Ketanot Hills with the three-storey bunker
   of base-game modules; AIBunker.lua's own five courses: low left to top right, across the gap, top left to low right, up the shaft,
   low right to mid left). Those are base-game layouts, which is the standard the user set, and they need no mod. Please give their
   pass/fail and times in SUMMARY.md alongside Bywater's.
2. For each Bywater and sky bunker course that gives up, the `AIBUNKER stall` blocks and the `AITRACE nodes:` line of the last route the
   unit had (the traced course only has AITRACE; if one course is clearly the worst, trace that one with `CCCP_BUNKER_TRACE=n`).
3. For "crab over the hill": the `AIGYM trace` lines (now with `aim` and `facing`) for the whole run, and the `AITRACE climb:` / `jet:`
   lines with `traceCourse = 15` in AIGym.lua (it's 16 at the moment). I want to see which way the crab faces, and its aim, at the
   moment it is thrown back down the slope.
4. The `AIGYM unit` lines (height, radius, jump height, aim range) for the Soldier Light and the Dreadnought: the grid's thresholds
   (standing room 0.42 of the height, crawl room 0.24, half width = radius / 2 clamped to 8..16) hang off them.
5. If Hemslock Hold has saved gym courses, its GYM lines too.
