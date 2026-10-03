#pragma once

#include <deque>
#include <string>

namespace RTE {

	/// An optional modern HUD drawn at window resolution over the game: a minimap of the terrain and units, health and ammo bars
	/// for the controlled unit, and a feed of unit losses. The classic HUD stays the default; this is drawn in addition when enabled.
	class ModernHUD {

	public:
		/// Gets whether the modern HUD is shown.
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether the modern HUD is shown.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Draws the HUD for the first player, with ImGui. Call once per frame while building the ImGui frame.
		static void Draw();

	private:
		static bool s_Enabled; //!< Whether the modern HUD is shown.
	};
} // namespace RTE
