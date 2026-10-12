#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace RTE {

	/// Makes maps procedurally: the ground of a map of a size and a style, from a seed, as a material picture the game textures and dresses the
	/// way it does any map's (SLTerrain::LoadData), with a backdrop, and simple bunkers if wanted. The same recipe always makes the same map.
	/// A map made is a Scene preset like any other (in UserScenes.rte/Generated, which isn't read at start-up, so it lasts until the game
	/// closes), started by name. See plans/procedural-maps-plan.md.
	class MapGen {

	public:
		/// The kinds of map.
		enum class Style {
			Grasslands,
			Mountains,
			DesertCanyon,
			Caves,
			FloatingIslands,
			SkyPlatforms,
			Pillars,
			Honeycomb,
			Shattered,
			Count
		};

		/// How big a map is. The sizes of the game's own maps (see the plan): small like Grasslands, extra large like Decision Day.
		enum class Size {
			Small,
			Medium,
			Large,
			ExtraLarge,
			Count
		};

		/// Everything a map is made from.
		struct Recipe {
			Size MapSize = Size::Medium;
			Style MapStyle = Style::Grasslands;
			uint32_t Seed = 1;
			int Roughness = 50; //!< How hilly and broken the ground is, 0 to 100.
			int Caves = 50; //!< How many caves and tunnels, 0 to 100.
			int Water = 30; //!< How much water lies in hollows, 0 to 100 (styles with ground only).
			bool Bunkers = true; //!< A simple concrete bunker at each end.
			bool WrapX = false; //!< The map wraps round left to right.

			/// The recipe as a short line of text, the same for the same recipe.
			std::string Code() const;
		};

		/// A picture of a map, to show before it's made: RGBA, a row after another.
		struct Picture {
			int Width = 0;
			int Height = 0;
			int MapWidth = 0; //!< The size of the map it shows, in pixels.
			int MapHeight = 0;
			std::vector<unsigned char> Pixels;
		};

		/// A style's name, as shown.
		static const char* StyleName(Style style);

		/// One line about a style.
		static const char* StyleBlurb(Style style);

		/// A size's name, as shown.
		static const char* SizeName(Size size);

		/// Whether a style has ground below everything (and so water in its hollows).
		static bool HasGround(Style style);

		/// The map's width and height in pixels.
		static void Dimensions(const Recipe& recipe, int& width, int& height);

		/// A small picture of the map the recipe makes, one pixel for every 8 of the map's: its ground, caves, water and bunkers, coloured by
		/// material, over the sky. Quick enough to redraw as the recipe changes. Main thread (it reads the materials' colours).
		static Picture Preview(const Recipe& recipe);

		/// The name a recipe's map is known by (a Scene preset).
		static std::string MapName(const Recipe& recipe);

		/// Starts making the map the recipe makes, on a worker thread: its material picture and its definition, written to disk. Call Update
		/// each frame until it says it's done. Main thread.
		static void Start(const Recipe& recipe);

		/// Whether a map is being made.
		static bool Busy();

		/// Finishes off a map once it's made: reads its definition in, so the Scene preset is there to start by name. Main thread.
		/// @param name Set to the map's name when it's ready.
		/// @param error Set to what went wrong, if anything did.
		/// @return Whether the map was finished this call (ready, or failed with error set).
		static bool Update(std::string& name, std::string& error);

		/// Whether a map was made this run and not saved (the map browser doesn't list these).
		static bool IsGenerated(const std::string& mapName);
	};
} // namespace RTE
