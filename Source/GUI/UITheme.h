#pragma once

#include <string>
#include <vector>

namespace RTE {

	/// The UI themes (the UI theme setting): sets of colours for the menus, the build menu, the inventory, the console, the loading
	/// screen, the tool windows and the HUD.
	///
	/// Each theme is a folder in a module's GUIs/Themes (Base.rte/GUIs/Themes/Green, or a mod's own) holding a Theme.ini and the
	/// images it swaps in. Theme.ini lines (// starts a comment):
	///   Name = Green                     What the setting calls it.
	///   Order = 1                        Where it comes in the list (lowest first, then by name).
	///   Image = <image> => <image>       Any skin loading the first image (a module path, such as Base.rte/GUIs/Skins/Menus/SkinBlue.png) gets the second.
	///   Color = <color> => <color>       Any GUI colour of the first value (a palette index, or #RRGGBB) is drawn in the second.
	///   HUD.<role> = #RRGGBB             A tool window and HUD colour (see ToolTheme in ToolWidgets.h): Panel, Well, WellHover, WellPressed, Edge, EdgeLight or EdgeDark.
	/// A theme only lists what it changes; everything else keeps the Classic look.
	namespace UITheme {

		/// The name of the theme that changes nothing.
		inline const std::string ClassicName = "Classic";

		/// The names of the themes found, in their order, Classic first.
		std::vector<std::string> Names();

		/// The name of the theme in use.
		const std::string& Current();

		/// Uses the named theme (Classic if there's no theme of that name). Menus and windows already open change over on their next draw.
		void Select(const std::string& name);

		/// A number that changes every time the theme in use changes, for things that build their look once to tell when to build it again.
		int Generation();

		/// The image to load in place of one a skin asks for.
		/// @param path The path the skin would load the image from, as GUISkin makes it.
		/// @return The path to load instead, or path itself if the theme leaves the image be.
		std::string Image(const std::string& path);

		/// The colour to draw in place of one a GUI asks for.
		/// @param color A palette index (0 to 255) or a packed RGB colour.
		/// @return The colour to draw instead, or color itself if the theme leaves the colour be.
		unsigned long Color(unsigned long color);
	} // namespace UITheme
} // namespace RTE
