#pragma once

#include "Box.h"

#include <deque>
#include <vector>

namespace RTE {

	/// The debug overlays of the settings panel's AI debug and Render debug pages, drawn over the game's picture by DebugMan::DrawOverlays.
	/// Each draws only while its option is on, into ImGui's foreground list, in player 1's view (see DebugDraw).
	namespace DebugOverlays {

		/// The unit inspector: a label over each inspected unit (or every unit in view) with its AI mode, route step, mover state, timers and what its scripts are doing.
		void DrawUnitInspector();

		/// The combat AI overlay: for each inspected unit (or every unit in view), the line to its target coloured by whether it is in sight, the range it holds to, and its cover, flank and retreat spots with how long it has held them.
		void DrawCombatOverlay();

		/// The navigation overlay's node under the pointer (its top level): what the grid makes of the node (PathFinder::DescribeNodeAt), and
		/// every way out of it as the inspected unit is offered them, drawn with its kind and cost, flights with their fuel.
		void DrawNavNode();

		/// The recent path solves overlay: the debug team's last few routes found, faded by age, each step coloured by kind with its cost, and at
		/// the goal the answer, total cost and solve time.
		void DrawRecentSolves();

		/// Keeps a path grid update for the terrain update boxes overlay: the areas of changed terrain that were waiting, and the nodes re-sampled.
		/// Called by Scene::UpdatePathFinding while the overlay is on; safe from any thread.
		void NoteTerrainUpdate(const std::deque<Box>& areas, const std::vector<Vector>& nodes);

		/// The terrain update boxes overlay: the areas and nodes NoteTerrainUpdate kept, fading over a second.
		void DrawTerrainUpdates();
	} // namespace DebugOverlays
} // namespace RTE
