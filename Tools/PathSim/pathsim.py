#!/usr/bin/env python3
"""pathsim -- an offline simulator of the game's A* path grid (Source/System/PathFinder.cpp) on the sky bunker of
Tools/RenderTest/RenderTest.rte/AIBunker.lua, built from the game's own bunker module material bitmaps.

    python3 -I pathsim.py sky [--course N] [--costs] [--dump X,Y] [--png OUT.png] [agent/world options]
    python3 -I pathsim.py png OUT.png [--course N] [--grid] [--dots]
    python3 -I pathsim.py dump X,Y
    python3 -I pathsim.py check            (staged vs fresh grid build: counts differing nodes)
    python3 -I pathsim.py describe         (where the modules landed, the storeys' floors, the shaft's width)

Agent options: --agent soldier|lua, --jump METRES|auto, --mass KG, --half-width PX, --stand PX, --crawl PX, --dig S, --breach S.
World options: --repo PATH, --mode staged|fresh, --terrain real|air, --no-snap, --settle, --raw-end.
"""

import argparse
import math
import os
import sys

# `python3 -I` does not put the script's directory on sys.path; these modules are our own.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import agent as agent_mod  # noqa: E402
import astar  # noqa: E402
import bunker  # noqa: E402
import grid_rules  # noqa: E402
from grid_rules import PathStepKind  # noqa: E402
from materials import MATERIAL_AIR, load_materials  # noqa: E402
from scene import Scene  # noqa: E402

# The repository root: two directories up from this file (Tools/PathSim/pathsim.py).
DEFAULT_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUNKER_CROP = (1400, 150, 2200, 520)


def build_world(args):
    """The scene and the path grid, built the way the game gets to them."""
    repo = args.repo
    materials = load_materials(repo)
    layout, courses, bounds = bunker.LAYOUTS[args.layout]
    bitmaps = bunker.load_module_bitmaps_for(repo, layout)
    if args.terrain == "real":
        terrain = bunker.load_ketanot_terrain(repo)
        scene = Scene.from_array(terrain, materials, wraps_x=True, wraps_y=False)
    else:
        terrain = bunker.load_ketanot_terrain(repo)  # for the size only (3600 x 1570, WrapX)
        scene = Scene(terrain.shape[1], terrain.shape[0], materials, wraps_x=True, wraps_y=False)
    snap = not args.no_snap
    if args.mode == "staged":
        # As in the game: the grid is built on the scene as loaded; the modules are placed 2.5 s in by the script, each placement
        # registering its box; Scene::UpdatePathFinding then re-samples the nodes in and around those boxes (RecalculateAreaCosts).
        finder = grid_rules.PathFinder(scene, 24)
        placements = bunker.build_sky_bunker(scene, bitmaps, snap=snap, layout=layout)
        boxes = list(scene.updated_material_areas)
        scene.updated_material_areas = []
        updated = finder.RecalculateAreaCosts(boxes, 10 ** 9)
        finder.last_area_update_count = len(updated)
    else:
        placements = bunker.build_sky_bunker(scene, bitmaps, snap=snap, layout=layout)
        scene.updated_material_areas = []
        finder = grid_rules.PathFinder(scene, 24)
        finder.last_area_update_count = -1
    return scene, finder, placements


def make_agent(args):
    jump = None if args.jump is None else ("auto" if args.jump == "auto" else float(args.jump))
    if args.agent == "lua":
        the_agent = agent_mod.lua_default_agent(jump_height=jump, dig_strength=args.dig, mass=args.mass)
    else:
        the_agent = agent_mod.soldier_light_agent(jump_height=jump, half_width=args.half_width, dig_strength=args.dig, breach_strength=args.breach, mass=args.mass)
        if args.stand is not None:
            the_agent.StandHeight = args.stand
        if args.crawl is not None:
            the_agent.CrawlHeight = args.crawl
    return the_agent


def on_ground(scene, place, char_height=agent_mod.SOLDIER_LIGHT_CHAR_HEIGHT):
    """Actor::UpdateMovePath's onGround (Actor.cpp 1043-1049): a point in the ground stays; one in the air drops to a fifth of a
    body over the ground under it."""
    in_scene = (place[0], max(1.0, place[1]))
    if scene.GetTerrMatter(int(in_scene[0]), int(in_scene[1])) != MATERIAL_AIR:
        return in_scene
    return scene.MovePointToGround(in_scene, char_height * 0.2, 3)


def course_points(scene, course, args):
    """The start and end the AI really asks for: onGround on both (Actor.cpp 1050-1066), with the 'start far below' rule."""
    frm, to, name = course
    frm = (float(frm[0]), float(frm[1]))
    to = (float(to[0]), float(to[1]))
    if args.settle:
        frm = bunker.settle(scene, frm, agent_mod.SOLDIER_LIGHT_CHAR_HEIGHT)
        to = bunker.settle(scene, to, agent_mod.SOLDIER_LIGHT_CHAR_HEIGHT)
    start = on_ground(scene, frm)
    if start[1] - frm[1] > agent_mod.SOLDIER_LIGHT_CHAR_HEIGHT:
        start = frm
    end = to if args.raw_end else on_ground(scene, to)
    return start, end, name


def format_nodes(result):
    """The AITRACE nodes: line (Actor.cpp 1168-1182): each point, with the kind (as its enum int) of the step that reaches it."""
    parts = []
    for i, point in enumerate(result.path):
        text = "%d,%d" % (int(point[0]), int(point[1]))
        if i > 0 and i - 1 < len(result.kinds):
            text += "(%d)" % result.kinds[i - 1]
        parts.append(text)
    return " ".join(parts)


def format_nodes_named(result):
    parts = []
    for i, point in enumerate(result.path):
        text = "%d,%d" % (int(point[0]), int(point[1]))
        if i > 0 and i - 1 < len(result.kinds):
            text += "(%s)" % PathStepKind.NAMES[result.kinds[i - 1]]
        parts.append(text)
    return " ".join(parts)


def run_course(finder, scene, the_agent, course, args, index):
    start, end, name = course_points(scene, course, args)
    result = finder.CalculatePath(start, end, the_agent)
    cost_text = "%.3f" % result.totalCost if result.totalCost < 1e30 else "FLT_MAX"
    print("course %d %s: %d,%d -> %d,%d (asked %d,%d -> %d,%d)" % (index, name, int(start[0]), int(start[1]), int(end[0]), int(end[1]), course[0][0], course[0][1], course[1][0], course[1][1]))
    print("  result %s cost %s points %d steps %d expanded %d closed-rewrites %d" % (
        result.result_name(), cost_text, len(result.path), len(result.statePath), result.solveStats.expanded, result.solveStats.closed_rewrites))
    print("  start node %s end node %s" % (result.startNode, result.endNode))
    print("  nodes: " + format_nodes(result))
    print("  kinds: " + format_nodes_named(result))
    bounds = bunker.LAYOUTS[args.layout][2]
    if bounds is not None:
        x0, y0, x1, y1 = bounds
        outside = [(int(p[0]), int(p[1])) for p in result.path if not (x0 <= p[0] < x1 and y0 <= p[1] <= y1)]
        outside_nodes = [(int(n.Pos[0]), int(n.Pos[1])) for n in result.statePath if not (x0 <= n.Pos[0] < x1 and y0 <= n.Pos[1] <= y1)]
        ok = result.result == astar.SOLVED and result.totalCost < 100000.0 and not outside and not outside_nodes
        print("  inside the bunker (x %d..%d, y %d..%d): %s%s" % (x0, x1 - 1, y0, y1, "yes" if not (outside or outside_nodes) else "NO", "" if not (outside or outside_nodes) else " points %s nodes %s" % (outside, outside_nodes)))
        print("  verdict: %s" % ("PASS" if ok else "FAIL"))
    if args.costs:
        costs = finder.StepCosts(result.statePath)
        print("  PATHLOG " + " ".join("%d,%d=%d" % (int(n.Pos[0]), int(n.Pos[1]), int(c)) for n, c in zip(result.statePath[1:], costs)))
    return result


def dump_around(finder, x, y):
    """CCCP_BUNKER_DUMP (AIBunker.lua): DescribeNodeAt for the 5x5 nodes around a point."""
    for gy in range(y - 48, y + 49, 24):
        for gx in range(x - 48, x + 49, 24):
            print("AIBUNKER grid " + finder.DescribeNodeAt((float(gx), float(gy))))


def describe_layout(scene, finder, placements):
    print("scene %dx%d wrapX=%s grid %dx%d nodes (node %d px, centres at 12+24k)" % (scene.w, scene.h, scene.wraps_x, finder.m_GridWidth, finder.m_GridHeight, finder.m_NodeDimension))
    for preset, pos, corner in placements:
        print("  %-9s m_Pos %6.0f,%4.0f  bitmap corner %6.0f,%4.0f" % (preset, pos[0], pos[1], corner[0], corner[1]))
    # Floors: the first solid pixel down a column inside each storey, at a few x.
    for label, (cx, cy) in (("top", (1792, 230)), ("mid", (1700, 326)), ("low", (1792, 422))):
        y = int(cy)
        while scene.GetTerrMatter(cx, y) == MATERIAL_AIR and y < cy + 120:
            y += 1
        print("  %s storey: floor surface at x=%d is y=%d" % (label, cx, y))
    # The shaft: the air width of the Shaft A column around x 1792-1800 at the mid storey.
    for probe_y in (300, 330, 360):
        x = 1800
        while scene.GetTerrMatter(x, probe_y) == MATERIAL_AIR and x > 1700:
            x -= 1
        left = x
        x = 1800
        while scene.GetTerrMatter(x, probe_y) == MATERIAL_AIR and x < 1900:
            x += 1
        right = x
        print("  shaft air span at y=%d: x %d..%d (%d px wide)" % (probe_y, left + 1, right - 1, right - left - 1))
    print("  updated nodes after placement: %s" % getattr(finder, "last_area_update_count", "?"))
    audit_edges(scene, placements)


def audit_edges(scene, placements):
    """Is the bunker sealed? Flood-fill the interior air from a point in the bottom corridor (the first module's cell centre, over its
    floor) through air pixels; if the fill reaches a pixel outside every module rectangle, the interior opens to the sky and the
    pixel where it escapes is printed. Also lists the modules' 5 px outer-corner chamfers for information (bevels on the outside of
    24 px walls, not openings)."""
    import bunker as _b
    from collections import deque
    sizes = {}
    boxes = []
    for preset, pos, corner in placements:
        if preset not in sizes:
            sizes[preset] = _b.load_module_bitmaps_for(DEFAULT_REPO, [(preset, 0, 0)])[preset].shape
        h, w = sizes[preset]
        boxes.append((int(corner[0]), int(corner[1]), int(corner[0]) + w, int(corner[1]) + h, preset))
    X0 = min(b[0] for b in boxes) - 2
    Y0 = min(b[1] for b in boxes) - 2
    X1 = max(b[2] for b in boxes) + 2
    Y1 = max(b[3] for b in boxes) + 2

    def inside_any(x, y):
        return any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1, _ in boxes)

    # Seed: the first placement's interior, 20 px over its floor (corner + 72), at its centre column.
    first = boxes[0]
    seed = (first[0] + 48, first[1] + 52)
    if scene.GetTerrMatter(*seed) != MATERIAL_AIR:
        print("  seal audit: seed %s is not air; pick another" % (seed,))
        return
    seen = set([seed])
    queue = deque([seed])
    escapes = []
    reached = 0
    while queue:
        x, y = queue.popleft()
        reached += 1
        if not inside_any(x, y):
            escapes.append((x, y))
            if len(escapes) > 20:
                break
            continue
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if X0 <= nx < X1 and Y0 <= ny < Y1 and (nx, ny) not in seen and scene.GetTerrMatter(nx, ny) == MATERIAL_AIR:
                seen.add((nx, ny))
                queue.append((nx, ny))
    if escapes:
        print("  seal audit: INTERIOR AIR REACHES THE SKY at %s (first %d escape pixels shown; %d air pixels reached)" % (escapes[:8], len(escapes[:8]), reached))
    else:
        print("  seal audit: sealed -- %d interior air pixels reached from %s, none outside the module rectangles" % (reached, seed))
    # Which module cells are reached at all (connectivity): a module none of whose interior pixels were reached is disconnected.
    unreached = []
    for x0, y0, x1, y1, preset in boxes:
        if not any((x, y) in seen for x in range(x0 + 24, x1 - 24, 4) for y in range(y0 + 24, y1 - 24, 4)):
            unreached.append("%s@%d,%d" % (preset, x0, y0))
    print("  connectivity: %s" % ("every module's interior is reached from the seed" if not unreached else "NOT reached: " + ", ".join(unreached)))
    chamfers = 0
    for x0, y0, x1, y1, preset in boxes:
        for (cx, cy) in ((x0, y0), (x1 - 1, y0), (x0, y1 - 1), (x1 - 1, y1 - 1)):
            if scene.GetTerrMatter(cx, cy) == MATERIAL_AIR and not inside_any(cx + (1 if cx == x0 else -1) * -1, cy):
                chamfers += 1
    print("  (outer-corner chamfers of the End modules facing the sky: %d; 5 px bevels on the outside of the walls)" % chamfers)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["sky", "png", "dump", "check", "describe"])
    parser.add_argument("target", nargs="?", help="png: the output file; dump: X,Y")
    parser.add_argument("--repo", default=DEFAULT_REPO)
    parser.add_argument("--course", type=int, default=None, help="course number (all when omitted)")
    parser.add_argument("--layout", choices=sorted(bunker.LAYOUTS), default="sky", help="sky: AIBunker.lua's layout; new: the redesigned closed bunker (bunker.py NEW_BUNKER_LAYOUT)")
    parser.add_argument("--costs", action="store_true", help="print the PATHLOG-style per-step costs")
    parser.add_argument("--dump", default=None, help="X,Y: DescribeNodeAt for the 5x5 nodes around the point")
    parser.add_argument("--png", default=None, help="sky: also render the course(s) to this file")
    parser.add_argument("--grid", action="store_true", help="png: draw the 24 px node grid")
    parser.add_argument("--dots", action="store_true", help="png: mark nodes on solid ground and each node's Surface pixel")
    parser.add_argument("--agent", choices=["soldier", "lua"], default="soldier", help="soldier: AHuman::GetPathAgent for the Soldier Light; lua: Scene:CalculatePath's header defaults")
    parser.add_argument("--jump", default=None, help="jump height in metres (default: the 22 m the game measures for a Soldier Light), or auto for the AHuman::EstimateJumpHeight port")
    parser.add_argument("--mass", type=float, default=agent_mod.SOLDIER_LIGHT_MASS_ESTIMATE, help="total mass for the jump estimate")
    parser.add_argument("--half-width", type=float, default=agent_mod.SOLDIER_LIGHT_HALF_WIDTH)
    parser.add_argument("--crop", default=None, help="png: X0,Y0,X1,Y1 in scene pixels (default the bunker, 1400,150,2200,520)")
    parser.add_argument("--scale", type=int, default=2, help="png: pixels per scene pixel")
    parser.add_argument("--stand", type=float, default=None)
    parser.add_argument("--crawl", type=float, default=None)
    parser.add_argument("--dig", type=float, default=agent_mod.C_PATHFINDING_DEFAULT_DIG_STRENGTH)
    parser.add_argument("--breach", type=float, default=None)
    parser.add_argument("--mode", choices=["staged", "fresh"], default="staged", help="staged: grid built on the bare scene then area-updated after the modules (as in the game); fresh: full recalc on the composed terrain")
    parser.add_argument("--terrain", choices=["real", "air"], default="real", help="real: Ketanot Hills' material bitmap under the bunker; air: an empty scene of the same size")
    parser.add_argument("--no-snap", action="store_true", help="place modules by their exact centres instead of the sandbox's 24 px snap")
    parser.add_argument("--settle", action="store_true", help="apply AIBunkerScript:Settle to the course points first (the real-map rule)")
    parser.add_argument("--raw-end", action="store_true", help="use the course's end point as given (the Lua 'path for' line) instead of onGround(end)")
    args = parser.parse_args(argv)

    if args.command == "check":
        args.mode = "staged"
        scene_a, finder_a, _ = build_world(args)
        args.mode = "fresh"
        scene_b, finder_b, _ = build_world(args)
        differing = 0
        examples = []
        for na, nb in zip(finder_a.m_NodeGrid, finder_b.m_NodeGrid):
            same = (na.Surface == nb.Surface and na.FreeHeight == nb.FreeHeight and na.ClearLeft == nb.ClearLeft and na.ClearRight == nb.ClearRight
                    and all(ma.index == mb.index for ma, mb in zip(na.AdjacentNodeBlockingMaterials, nb.AdjacentNodeBlockingMaterials)))
            if not same:
                differing += 1
                if len(examples) < 10:
                    examples.append((finder_a.DescribeNodeAt(na.Pos), finder_b.DescribeNodeAt(nb.Pos)))
        print("staged vs fresh: %d of %d nodes differ" % (differing, len(finder_a.m_NodeGrid)))
        for a, b in examples:
            print("  staged: " + a)
            print("  fresh:  " + b)
        return 0

    scene, finder, placements = build_world(args)
    the_agent = make_agent(args)
    estimated = agent_mod.estimate_jump_height(args.mass)
    print("agent: %s  (EstimateJumpHeight port with mass %.0f kg: %.2f m = %.0f px; vertical chain %d nodes, diagonal %d)" % (
        the_agent, args.mass, estimated, estimated * agent_mod.C_PPM,
        max(1, int(the_agent.JumpHeight / 1.2)) if the_agent.JumpHeight < 1e30 else -1,
        max(1, int(the_agent.JumpHeight * 0.7 / 1.2)) if the_agent.JumpHeight < 1e30 else -1))

    if args.command == "describe":
        describe_layout(scene, finder, placements)
        return 0

    if args.command == "dump":
        x, y = (int(v) for v in args.target.split(","))
        dump_around(finder, x, y)
        return 0

    courses = bunker.LAYOUTS[args.layout][1]
    selected = [(i + 1, c) for i, c in enumerate(courses) if args.course is None or args.course == i + 1]

    if args.command == "sky":
        results = []
        for index, course in selected:
            results.append((index, course, run_course(finder, scene, the_agent, course, args, index)))
        if args.dump:
            x, y = (int(v) for v in args.dump.split(","))
            dump_around(finder, x, y)
        if args.png:
            write_png(args.png, scene, finder, results, args)
        return 0

    if args.command == "png":
        results = []
        for index, course in selected:
            start, end, name = course_points(scene, course, args)
            results.append((index, course, finder.CalculatePath(start, end, the_agent)))
        write_png(args.target, scene, finder, results, args)
        return 0
    return 1


def write_png(out, scene, finder, results, args):
    import render
    paths = []
    if len(results) == 1:
        index, course, result = results[0]
        paths.append((result.path, result.kinds, None, "course %d %s: %s cost %.1f" % (index, course[2], result.result_name(), result.totalCost if result.totalCost < 1e30 else -1)))
    else:
        for n, (index, course, result) in enumerate(results):
            color = render.COURSE_COLORS[n % len(render.COURSE_COLORS)]
            paths.append((result.path, result.kinds, color, "course %d %s: %s cost %.1f" % (index, course[2], result.result_name(), result.totalCost if result.totalCost < 1e30 else -1)))
    crop = tuple(int(v) for v in args.crop.split(",")) if args.crop else (BUNKER_CROP if args.layout == "sky" else (1400, 150, 2300, 520))
    image = render.render(scene, crop, scale=args.scale, pathfinder=finder, paths=paths, grid=args.grid, ground_dots=args.dots,
                          title="%s bunker, %s terrain, %s, snap=%s, agent %s jump %s hw %g" % (args.layout, args.terrain, args.mode, not args.no_snap, args.agent, args.jump, args.half_width))
    image.save(out)
    print("wrote %s (%dx%d)" % (out, image.width, image.height))


if __name__ == "__main__":
    sys.exit(main())
