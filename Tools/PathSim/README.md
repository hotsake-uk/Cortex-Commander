# PathSim: the path grid, offline

A Python model of the game's A* path grid (`Source/System/PathFinder.cpp`), run on the sky bunker that
`Tools/RenderTest/RenderTest.rte/AIBunker.lua` builds from the base-game bunker modules, using the modules' own material bitmaps
and the Ketanot Hills terrain. It exists so a change to the grid's rules can be tried against real bunker geometry without the
game: it gives the game's routes node for node (checked against the `AITRACE nodes:` lines of the test machine's runs).

It models the grid, not the movement script: what route a unit is given, with each step's kind, and what the grid makes of any
node. Whether the unit can follow the route is the gym's business.

```
pip install numpy pillow
python3 -I Tools/PathSim/pathsim.py sky --layout new            # the seven courses of the current sky bunker
python3 -I Tools/PathSim/pathsim.py sky --layout new --course 1 --costs   # one course with its per-step costs (PATHLOG style)
python3 -I Tools/PathSim/pathsim.py dump 1788,348 --layout new  # the grid's view of the 5 x 5 nodes around a point (CCCP_BUNKER_DUMP style)
python3 -I Tools/PathSim/pathsim.py png out.png --layout new --grid       # a picture of the layout with every route
python3 -I Tools/PathSim/pathsim.py describe --layout new       # where the modules landed; a seal and connectivity audit
python3 -I Tools/PathSim/portdata.py                            # which sides of each module are open, from its bitmap
```

`--layout sky` is the first sky bunker (hubs everywhere; kept for comparing against the logs in `Tools/RenderTest/Results/7f09496`).
The searcher is the Soldier Light as the game measures it (height 100, radius 28, jump height 440 px): `--agent lua` is what
`Scene:CalculatePath` from Lua uses instead (the `PathAgent` header defaults), and `--jump`, `--half-width`, `--stand`, `--crawl`
override. `--mode fresh` builds the grid on the finished scene instead of staging it the way the game does (modules placed, then the
boxes around them re-sampled).

## Keeping it true

`grid_rules.py` mirrors `PathFinder.cpp` function by function; each method names the C++ function it follows. When the C++ changes,
change the Python the same way, and check a course the game has logged still comes out the same. `scene.py` holds what the model
needs of `SceneMan` (rays, wrapping, `MovePointToGround`, how the sandbox snaps a placed structure); `astar.py` is MicroPather's
search order; `agent.py` the `PathAgent` and the jump-height estimate; `bunker.py` the layouts and courses of `AIBunker.lua`
(`NEW_BUNKER_LAYOUT` is the one in the Lua now, and must be kept the same).

Known differences from the game: costs are doubles here and floats there, so an exact tie can break the other way; the terrain is
the raw material bitmap, without the scene's debris or any actors or doors; there is one grid, not one per team.
