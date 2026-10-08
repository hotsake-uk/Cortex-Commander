#pragma once

namespace RTE {

	/// The debug overlays of the settings panel's AI debug and Render debug pages, drawn over the game's picture by DebugMan::DrawOverlays.
	/// Each draws only while its option is on, into ImGui's foreground list, in player 1's view (see DebugDraw).
	namespace DebugOverlays {

		/// The unit inspector: a label over each inspected unit (or every unit in view) with its AI mode, route step, mover state, timers and what its scripts are doing.
		void DrawUnitInspector();
	} // namespace DebugOverlays
} // namespace RTE
