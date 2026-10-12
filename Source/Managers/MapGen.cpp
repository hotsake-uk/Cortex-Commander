// The map generator: maps of a size and a style made from a seed. See MapGen.h and plans/procedural-maps-plan.md.
//
// A map is laid out first on a coarse grid of 8 by 8 pixel cells (the Layout): a density for every cell (above 0 is ground, and roughly how
// many cells in from its edge), the line of the ground's surface where the style has ground, the water lying in hollows and the bunkers.
// The preview is that grid drawn a pixel a cell. The map itself samples the same grid smoothly for every pixel, roughens its edges a little,
// and gives every pixel of ground its material by how deep it is under the surface, then writes the material picture and a scene definition
// that dresses it (grass, plants, boulders, gold) with the game's own terrain pieces and a sandbox backdrop.
//
// Only integer hashes, + * / floor and sqrt go into the shapes (no library random numbers or transcendental functions), so a recipe makes the
// same map on every machine.

#include "MapGen.h"

#include "Constants.h"
#include "Material.h"
#include "PresetMan.h"
#include "Reader.h"
#include "RTEError.h"
#include "Scene.h"
#include "SceneMan.h"
#include "SLBackground.h"
#include "System.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <execution>
#include <filesystem>
#include <fstream>
#include <future>
#include <numeric>
#include <set>
#include <sstream>

using namespace RTE;

namespace {
	constexpr int c_Cell = 8; //!< Pixels a side of a layout cell.
	constexpr const char* c_Folder = "/Generated/"; //!< Under UserScenes.rte: maps made and not saved. Not read at start-up (only the module's top folder is).
	constexpr int c_FilesKept = 8; //!< Maps made whose files are kept in the folder (the older ones are deleted as new ones are made).

#pragma region Random numbers and noise
	uint32_t Hash(uint32_t a) {
		a ^= a >> 16;
		a *= 0x7FEB352DU;
		a ^= a >> 15;
		a *= 0x846CA68BU;
		a ^= a >> 16;
		return a;
	}

	uint32_t Hash(uint32_t a, uint32_t b) { return Hash(a ^ Hash(b + 0x9E3779B9U)); }
	uint32_t Hash(uint32_t a, uint32_t b, uint32_t c) { return Hash(a, Hash(b, c)); }

	/// A 0 to 1 number for a lattice point.
	float Unit(uint32_t a, uint32_t b, uint32_t c) { return static_cast<float>(Hash(a, b, c) >> 8) / 16777216.0F; }

	/// Small, fast and the same everywhere (PCG32).
	struct Random {
		uint64_t State;

		explicit Random(uint32_t seed, uint32_t stream = 0) : State(0) {
			State = (static_cast<uint64_t>(Hash(seed, stream)) << 32) | Hash(stream, seed + 1U);
			Next();
		}

		uint32_t Next() {
			uint64_t old = State;
			State = old * 6364136223846793005ULL + 1442695040888963407ULL;
			uint32_t shifted = static_cast<uint32_t>(((old >> 18U) ^ old) >> 27U);
			uint32_t rotation = static_cast<uint32_t>(old >> 59U);
			return (shifted >> rotation) | (shifted << ((~rotation + 1U) & 31U));
		}

		/// 0 to 1.
		float Float() { return static_cast<float>(Next() >> 8) / 16777216.0F; }
		float Range(float low, float high) { return low + (high - low) * Float(); }
		/// low to high, both included.
		int Int(int low, int high) { return high <= low ? low : low + static_cast<int>(Next() % static_cast<uint32_t>(high - low + 1)); }
		bool Chance(float chance) { return Float() < chance; }
	};

	float Smooth(float t) { return t * t * (3.0F - 2.0F * t); }
	float Mix(float a, float b, float t) { return a + (b - a) * t; }
	float Clamp01(float value) { return std::clamp(value, 0.0F, 1.0F); }

	/// Value noise, -1 to 1, on a lattice a unit apart; with a period, the lattice repeats every that many units across.
	float ValueNoise(float x, float y, uint32_t seed, int period) {
		float floorX = std::floor(x);
		float floorY = std::floor(y);
		int x0 = static_cast<int>(floorX);
		int y0 = static_cast<int>(floorY);
		float fx = Smooth(x - floorX);
		float fy = Smooth(y - floorY);
		auto wrapped = [period](int i) { return period > 0 ? ((i % period) + period) % period : i; };
		uint32_t xa = static_cast<uint32_t>(wrapped(x0));
		uint32_t xb = static_cast<uint32_t>(wrapped(x0 + 1));
		uint32_t ya = static_cast<uint32_t>(y0);
		uint32_t yb = static_cast<uint32_t>(y0 + 1);
		float top = Mix(Unit(xa, ya, seed), Unit(xb, ya, seed), fx);
		float bottom = Mix(Unit(xa, yb, seed), Unit(xb, yb, seed), fx);
		return Mix(top, bottom, fy) * 2.0F - 1.0F;
	}

	/// Noise over the layout, in cells, with features about featureCells across, and finer octaves on top. -1 to 1.
	/// With wrapWidth (the map's width in cells, when it wraps) the noise joins up seamlessly left to right.
	float Fractal(float x, float y, uint32_t seed, float featureCells, int octaves, float wrapWidth) {
		float sum = 0.0F;
		float amplitude = 1.0F;
		float total = 0.0F;
		float feature = featureCells;
		for (int octave = 0; octave < octaves; ++octave) {
			int period = 0;
			float featureX = feature;
			if (wrapWidth > 0.0F) {
				period = std::max(1, static_cast<int>(wrapWidth / feature + 0.5F));
				featureX = wrapWidth / static_cast<float>(period);
			}
			sum += amplitude * ValueNoise(x / featureX, y / feature, seed + static_cast<uint32_t>(octave) * 1013U, period);
			total += amplitude;
			amplitude *= 0.5F;
			feature *= 0.5F;
		}
		return sum / total;
	}

	/// Sharp crests where the noise crosses zero: 0 to 1, 1 on a crest.
	float Ridged(float x, float y, uint32_t seed, float featureCells, int octaves, float wrapWidth) {
		float value = 1.0F - std::abs(Fractal(x, y, seed, featureCells, octaves, wrapWidth));
		return value * value;
	}
#pragma endregion

#pragma region Styles
	/// A band of material under the surface, this many pixels deep (0: down to the bottom).
	struct Band {
		const char* Material;
		int Thickness;
	};

	struct StyleInfo {
		const char* Name;
		const char* Blurb;
		const char* Backdrop; //!< The sandbox theme whose backdrop it uses ("Sandbox Small - <it>").
		bool Ground; //!< Ground under everything, with a surface line.
		std::array<Band, 5> Bands; //!< From the surface down.
		const char* Pocket; //!< A material found in blobs through the ground below the top band, or none.
		float Detail; //!< How rough the edges of the ground are, in cells.
		const char* Dressing; //!< The terrain's frostings and debris, as INI at the terrain's level.
	};

	// The dressings are taken from the game's own terrains (Base.rte/Scenes/Terrains/Terrains.ini) of the same kind.
	constexpr const char* c_GrassDressing =
	    "\t\tAddTerrainFrosting = TerrainFrosting\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Topsoil\n\t\t\tFrostingMaterial = Material\n\t\t\t\tCopyOf = Grass\n"
	    "\t\t\tMinThickness = 4\n\t\t\tMaxThickness = 7\n\t\t\tInAirOnly = 1\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Plants\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Gold\n\t\t\tDensityPerMeter = 2.5\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Boulders\n\t\t\tDensityPerMeter = 1.2\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Boulders\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Dense Earth\n\t\t\tDensityPerMeter = 0.8\n";
	constexpr const char* c_MountainDressing =
	    "\t\tAddTerrainFrosting = TerrainFrosting\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Topsoil\n\t\t\tFrostingMaterial = Material\n\t\t\t\tCopyOf = Grass\n"
	    "\t\t\tMinThickness = 3\n\t\t\tMaxThickness = 7\n\t\t\tInAirOnly = 1\n"
	    "\t\tAddTerrainFrosting = TerrainFrosting\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Snow\n\t\t\tFrostingMaterial = Material\n\t\t\t\tCopyOf = Snow\n"
	    "\t\t\tMinThickness = 4\n\t\t\tMaxThickness = 8\n\t\t\tInAirOnly = 1\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Plants\n\t\t\tDensityPerMeter = 1\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Stalagmites\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Stalactites\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Gold\n\t\t\tMinDepth = 300\n\t\t\tMaxDepth = 900\n\t\t\tDensityPerMeter = 2.5\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Boulders\n\t\t\tMinDepth = 60\n\t\t\tMaxDepth = 1200\n\t\t\tDensityPerMeter = 3\n";
	constexpr const char* c_DesertDressing =
	    "\t\tAddTerrainFrosting = TerrainFrosting\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Sand\n\t\t\tFrostingMaterial = Material\n\t\t\t\tCopyOf = Sand\n"
	    "\t\t\tMinThickness = 2\n\t\t\tMaxThickness = 2\n\t\t\tInAirOnly = 1\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Cacti\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Small Cacti\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Gold\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Gold\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Dense Red Earth\n\t\t\tMinDepth = 25\n\t\t\tMaxDepth = 4000\n\t\t\tDensityPerMeter = 3\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Fossils\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Boulders\n";
	constexpr const char* c_CaveDressing =
	    "\t\tAddTerrainFrosting = TerrainFrosting\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Topsoil\n\t\t\tFrostingMaterial = Material\n\t\t\t\tCopyOf = Grass\n"
	    "\t\t\tMinThickness = 3\n\t\t\tMaxThickness = 7\n\t\t\tInAirOnly = 1\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Plants\n\t\t\tDensityPerMeter = 1\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Gold\n\t\t\tDensityPerMeter = 6\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Gold\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Stone\n\t\t\tMinDepth = 0\n\t\t\tMaxDepth = 1000\n\t\t\tDensityPerMeter = 1\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Stalactites\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Stalagmites\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Boulders\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Stone\n\t\t\tMinDepth = 0\n\t\t\tMaxDepth = 1000\n\t\t\tDensityPerMeter = 1.2\n";
	constexpr const char* c_IslandDressing =
	    "\t\tAddTerrainFrosting = TerrainFrosting\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Topsoil\n\t\t\tFrostingMaterial = Material\n\t\t\t\tCopyOf = Grass\n"
	    "\t\t\tMinThickness = 4\n\t\t\tMaxThickness = 7\n\t\t\tInAirOnly = 1\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Plants\n\t\t\tDensityPerMeter = 2\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Gold\n\t\t\tDensityPerMeter = 2\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Stalactites\n";
	constexpr const char* c_RockDressing =
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Gold\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Boulders\n\t\t\tTargetMaterial = Material\n\t\t\t\tCopyOf = Stone\n\t\t\tMaxDepth = 1500\n\t\t\tDensityPerMeter = 2\n"
	    "\t\tAddTerrainDebris = TerrainDebris\n\t\t\tCopyOf = Stalactites\n";

	// clang-format off
	const StyleInfo c_Styles[] = {
		{"Grasslands", "Rolling green hills over earth and stone, with a few caves and ponds.", "Rolling Hills", true,
		 {{{"Topsoil", 24}, {"Earth", 110}, {"Dense Earth", 180}, {"Stone", 0}, {nullptr, 0}}}, "Dense Earth", 0.7F, c_GrassDressing},
		{"Mountains", "Steep peaks capped with snow, cliffs and overhangs, rock all through.", "Mountains", true,
		 {{{"Topsoil", 10}, {"Earth", 50}, {"Stone", 0}, {nullptr, 0}, {nullptr, 0}}}, "Dense Earth", 1.0F, c_MountainDressing},
		{"Desert canyon", "Red rock mesas cut by a deep stepped canyon, sand drifted over the top.", "Desert", true,
		 {{{"Sand", 30}, {"Red Earth", 90}, {"Dense Red Earth", 160}, {"Red Stone", 0}, {nullptr, 0}}}, "Red Stone", 0.6F, c_DesertDressing},
		{"Caves", "A thin crust of ground over a maze of caves and tunnels that runs end to end.", "Pine Forest", true,
		 {{{"Topsoil", 14}, {"Earth", 80}, {"Dense Earth", 120}, {"Stone", 0}, {nullptr, 0}}}, "Stone", 0.8F, c_CaveDressing},
		{"Floating islands", "No ground at all: islands hanging in the sky over a bottomless drop, a jetpack hop apart.", "Clear Sky", false,
		 {{{"Topsoil", 14}, {"Earth", 46}, {"Stone", 0}, {nullptr, 0}, {nullptr, 0}}}, nullptr, 0.8F, c_IslandDressing},
		{"Sky platforms", "Small concrete platforms in a loose grid over the city, long falls between.", "City Skyline", false,
		 {{{"Concrete", 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}}}, nullptr, 0.0F, ""},
		{"Pillars", "Tall columns of moon rock rising from a low floor, fought over at the top.", "Alien World", true,
		 {{{"Lunar Earth", 18}, {"Lunar Stone", 0}, {nullptr, 0}, {nullptr, 0}, {nullptr, 0}}}, nullptr, 0.6F, c_RockDressing},
		{"Honeycomb", "Solid ground riddled with round chambers joined by short tunnels.", "Night", true,
		 {{{"Earth", 30}, {"Dense Earth", 100}, {"Stone", 0}, {nullptr, 0}, {nullptr, 0}}}, "Stone", 0.5F, c_CaveDressing},
		{"Shattered", "Ground broken into tilted slabs with crevasses between, and chunks thrown up into the air.", "Sunset", true,
		 {{{"Topsoil", 16}, {"Earth", 60}, {"Dense Earth", 100}, {"Stone", 0}, {nullptr, 0}}}, "Stone", 0.7F, c_GrassDressing},
	};
	// clang-format on
	static_assert(std::size(c_Styles) == static_cast<size_t>(MapGen::Style::Count), "c_Styles must describe each style.");

	const StyleInfo& StyleOf(MapGen::Style style) { return c_Styles[std::clamp(static_cast<int>(style), 0, static_cast<int>(MapGen::Style::Count) - 1)]; }

	constexpr const char* c_SizeNames[] = {"Small", "Medium", "Large", "Extra large"};
	/// Each size's width and height ranges, in pixels: the sizes of the game's own maps.
	constexpr int c_SizeRanges[][4] = {{2400, 3200, 1104, 1400}, {3800, 5000, 1504, 2096}, {5200, 6400, 2000, 2600}, {7600, 9000, 2304, 2896}};
#pragma endregion

#pragma region Layout
	/// A simple concrete bunker: its box, outside walls included, in pixels.
	struct Bunker {
		int Left = 0;
		int Top = 0;
		int Width = 0;
		int Height = 0;
	};

	constexpr int c_BunkerWidth = 224;
	constexpr int c_BunkerHeight = 96;
	constexpr int c_BunkerWall = 12;
	constexpr int c_DoorHeight = 64;

	/// A map planned out a cell at a time.
	struct Layout {
		int Width = 0; //!< In pixels.
		int Height = 0;
		int CellsX = 0;
		int CellsY = 0;
		bool WrapX = false;
		const StyleInfo* Info = nullptr;
		MapGen::Style Kind = MapGen::Style::Grasslands;
		std::vector<float> Density; //!< Above 0 is ground: roughly how many cells in from its edge.
		std::vector<float> Surface; //!< Where the style has ground: the surface's height in each column of cells, in pixels from the top.
		std::vector<int> WaterTop; //!< Each column of cells: the top of the water lying in it, in pixels, or -1 for none.
		std::vector<Bunker> Bunkers;
		uint32_t Seed = 0;

		float& At(int x, int y) { return Density[static_cast<size_t>(y) * CellsX + x]; }
		float At(int x, int y) const { return Density[static_cast<size_t>(y) * CellsX + x]; }
		float WrapWidth() const { return WrapX ? static_cast<float>(CellsX) : 0.0F; }

		/// The density at a point given in cells, smoothly between the cells' middles.
		float Sample(float x, float y) const {
			x -= 0.5F;
			y -= 0.5F;
			float floorX = std::floor(x);
			float floorY = std::floor(y);
			int x0 = static_cast<int>(floorX);
			int y0 = static_cast<int>(floorY);
			float fx = x - floorX;
			float fy = y - floorY;
			auto column = [this](int i) { return WrapX ? ((i % CellsX) + CellsX) % CellsX : std::clamp(i, 0, CellsX - 1); };
			int xa = column(x0);
			int xb = column(x0 + 1);
			int ya = std::clamp(y0, 0, CellsY - 1);
			int yb = std::clamp(y0 + 1, 0, CellsY - 1);
			return Mix(Mix(At(xa, ya), At(xb, ya), fx), Mix(At(xa, yb), At(xb, yb), fx), fy);
		}

		/// The surface's height at a pixel column, in pixels, smoothly between columns of cells.
		float SurfaceAt(float pixelX) const {
			if (Surface.empty()) {
				return static_cast<float>(Height);
			}
			float x = pixelX / static_cast<float>(c_Cell) - 0.5F;
			float floorX = std::floor(x);
			int x0 = static_cast<int>(floorX);
			auto column = [this](int i) { return WrapX ? ((i % CellsX) + CellsX) % CellsX : std::clamp(i, 0, CellsX - 1); };
			return Mix(Surface[column(x0)], Surface[column(x0 + 1)], x - floorX);
		}

		/// Carves a round hole: everything within radius cells of the point is air.
		void Carve(float centreX, float centreY, float radius) {
			int reach = static_cast<int>(radius) + 2;
			int middleX = static_cast<int>(centreX);
			int middleY = static_cast<int>(centreY);
			for (int y = std::max(0, middleY - reach); y <= std::min(CellsY - 1, middleY + reach); ++y) {
				for (int dx = -reach; dx <= reach; ++dx) {
					int x = middleX + dx;
					if (WrapX) {
						x = ((x % CellsX) + CellsX) % CellsX;
					} else if (x < 0 || x >= CellsX) {
						continue;
					}
					float offsetX = static_cast<float>(middleX + dx) + 0.5F - centreX;
					float offsetY = static_cast<float>(y) + 0.5F - centreY;
					float distance = std::sqrt(offsetX * offsetX + offsetY * offsetY);
					At(x, y) = std::min(At(x, y), distance - radius);
				}
			}
		}

		/// Adds a solid shape: each cell is the more solid of what it was and the shape.
		void Add(int x, int y, float density) {
			if (WrapX) {
				x = ((x % CellsX) + CellsX) % CellsX;
			} else if (x < 0 || x >= CellsX) {
				return;
			}
			if (y >= 0 && y < CellsY) {
				At(x, y) = std::max(At(x, y), density);
			}
		}

		/// A tunnel wandering from a point the way it's heading, carved as it goes.
		void Worm(Random& random, float x, float y, float heading, int steps, float radius, float minY, float maxY) {
			for (int step = 0; step < steps; ++step) {
				Carve(x, y, radius);
				heading += random.Range(-0.35F, 0.35F);
				// Keep it mostly level: units walk tunnels, they don't climb shafts.
				heading = std::clamp(heading, -0.7F, 0.7F);
				float directionX = 1.0F - heading * heading * 0.5F;
				x += directionX * radius * 0.6F;
				y += heading * radius * 0.6F;
				if (y < minY) {
					y = minY;
					heading = std::abs(heading);
				} else if (y > maxY) {
					y = maxY;
					heading = -std::abs(heading);
				}
				if (!WrapX && (x < 0.0F || x >= static_cast<float>(CellsX))) {
					break;
				}
			}
		}

		/// Carves a straight tunnel, radius cells wide, between two points.
		void Tunnel(float fromX, float fromY, float toX, float toY, float radius) {
			float lengthX = toX - fromX;
			float lengthY = toY - fromY;
			int steps = std::max(1, static_cast<int>(std::sqrt(lengthX * lengthX + lengthY * lengthY) / (radius * 0.5F)));
			for (int step = 0; step <= steps; ++step) {
				float t = static_cast<float>(step) / static_cast<float>(steps);
				Carve(fromX + lengthX * t, fromY + lengthY * t, radius);
			}
		}

		/// The top of the ground in a column of cells, from the top down, or -1 if there's none.
		int TopOfGround(int x) const {
			for (int y = 0; y < CellsY; ++y) {
				if (At(x, y) > 0.0F) {
					return y;
				}
			}
			return -1;
		}
	};

	/// Ground below a surface line, its density by how far under the line.
	void FillBelowSurface(Layout& layout) {
		for (int x = 0; x < layout.CellsX; ++x) {
			float surface = layout.Surface[x] / static_cast<float>(c_Cell);
			for (int y = 0; y < layout.CellsY; ++y) {
				layout.At(x, y) = std::clamp(static_cast<float>(y) + 0.5F - surface, -8.0F, 8.0F);
			}
		}
	}

	/// Caves through the ground, by how many are wanted: a few wandering tunnels, and open pockets where noise is high.
	void AddCaves(Layout& layout, Random& random, float amount, float minDepthCells) {
		const float cellsX = static_cast<float>(layout.CellsX);
		const float cellsY = static_cast<float>(layout.CellsY);
		int worms = static_cast<int>(amount * cellsX / 70.0F + 0.5F);
		for (int i = 0; i < worms; ++i) {
			float x = random.Range(0.0F, cellsX);
			int column = std::clamp(static_cast<int>(x), 0, layout.CellsX - 1);
			float top = layout.Surface.empty() ? 0.0F : layout.Surface[column] / static_cast<float>(c_Cell);
			float minY = top + minDepthCells;
			float maxY = cellsY - 8.0F;
			if (minY >= maxY) {
				continue;
			}
			float y = random.Range(minY, maxY);
			layout.Worm(random, x, y, random.Range(-0.4F, 0.4F), random.Int(30, 90), random.Range(3.2F, 4.6F), minY, maxY);
		}
		if (amount > 0.05F) {
			float threshold = 0.62F - 0.22F * amount;
			for (int y = 0; y < layout.CellsY - 6; ++y) {
				for (int x = 0; x < layout.CellsX; ++x) {
					float top = layout.Surface.empty() ? 0.0F : layout.Surface[x] / static_cast<float>(c_Cell);
					if (static_cast<float>(y) < top + minDepthCells + 4.0F) {
						continue;
					}
					float noise = Fractal(static_cast<float>(x), static_cast<float>(y) * 1.6F, layout.Seed + 77U, 16.0F, 3, layout.WrapWidth());
					if (noise > threshold) {
						layout.At(x, y) = std::min(layout.At(x, y), (threshold - noise) * 12.0F);
					}
				}
			}
		}
	}

	void MakeGrasslands(Layout& layout, Random& random, float rough, float caves) {
		const float height = static_cast<float>(layout.Height);
		for (int x = 0; x < layout.CellsX; ++x) {
			float fx = static_cast<float>(x);
			float hills = Fractal(fx, 0.5F, layout.Seed + 1U, static_cast<float>(layout.CellsX) / 3.0F, 4, layout.WrapWidth());
			float bumps = Fractal(fx, 3.5F, layout.Seed + 2U, 12.0F, 2, layout.WrapWidth());
			layout.Surface[x] = height * (0.52F + (0.08F + 0.12F * rough) * hills + 0.015F * bumps);
		}
		FillBelowSurface(layout);
		AddCaves(layout, random, caves * 0.6F, 10.0F);
	}

	void MakeMountains(Layout& layout, Random& random, float rough, float caves) {
		const float height = static_cast<float>(layout.Height);
		for (int x = 0; x < layout.CellsX; ++x) {
			float fx = static_cast<float>(x);
			float peaks = Ridged(fx, 0.5F, layout.Seed + 3U, static_cast<float>(layout.CellsX) / 4.0F, 4, layout.WrapWidth());
			float rolling = Fractal(fx, 7.5F, layout.Seed + 4U, static_cast<float>(layout.CellsX) / 2.0F, 2, layout.WrapWidth());
			layout.Surface[x] = height * (0.70F - (0.28F + 0.18F * rough) * peaks - 0.08F * rolling);
		}
		FillBelowSurface(layout);
		// Cliffs and overhangs: the ground pushed in and out sideways near the surface.
		for (int y = 0; y < layout.CellsY; ++y) {
			for (int x = 0; x < layout.CellsX; ++x) {
				float warp = Fractal(static_cast<float>(x), static_cast<float>(y), layout.Seed + 5U, 9.0F, 3, layout.WrapWidth());
				layout.At(x, y) += warp * (2.0F + 4.0F * rough);
			}
		}
		AddCaves(layout, random, caves * 0.5F, 8.0F);
	}

	void MakeDesertCanyon(Layout& layout, Random& random, float rough, float caves) {
		const float height = static_cast<float>(layout.Height);
		const float cellsX = static_cast<float>(layout.CellsX);
		int canyons = layout.CellsX > 700 ? 2 : 1;
		std::vector<float> middles;
		std::vector<float> widths;
		for (int i = 0; i < canyons; ++i) {
			float share = (static_cast<float>(i) + 0.5F) / static_cast<float>(canyons);
			middles.push_back(cellsX * (share + random.Range(-0.08F, 0.08F)));
			widths.push_back(cellsX * random.Range(0.07F, 0.12F) / static_cast<float>(canyons));
		}
		const float step = height * 0.05F;
		for (int x = 0; x < layout.CellsX; ++x) {
			float fx = static_cast<float>(x);
			float mesas = Fractal(fx, 0.5F, layout.Seed + 6U, cellsX / 5.0F, 3, layout.WrapWidth());
			// Terraced: the mesa heights come in steps, with short slopes between.
			float raw = mesas * height * (0.06F + 0.08F * rough) / step;
			float floorRaw = std::floor(raw);
			float terraced = (floorRaw + Smooth(Clamp01((raw - floorRaw - 0.75F) * 4.0F))) * step;
			float surface = height * 0.42F + terraced;
			for (size_t i = 0; i < middles.size(); ++i) {
				float distance = std::abs(fx - middles[i]);
				if (layout.WrapX) {
					distance = std::min(distance, cellsX - distance);
				}
				float t = Clamp01(1.0F - distance / widths[i]);
				if (t > 0.0F) {
					// A stepped cut, steep walls and a flat bottom.
					float depth = height * (0.24F + 0.10F * rough);
					float steps = 4.0F;
					float shape = std::sqrt(t);
					surface += depth * std::floor(shape * steps + 0.5F) / steps;
				}
			}
			float dunes = Fractal(fx, 9.5F, layout.Seed + 7U, 10.0F, 2, layout.WrapWidth());
			layout.Surface[x] = surface + height * 0.01F * dunes;
		}
		FillBelowSurface(layout);
		AddCaves(layout, random, caves * 0.5F, 12.0F);
	}

	void MakeCaves(Layout& layout, Random& random, float rough, float caves) {
		const float height = static_cast<float>(layout.Height);
		for (int x = 0; x < layout.CellsX; ++x) {
			float fx = static_cast<float>(x);
			float hills = Fractal(fx, 0.5F, layout.Seed + 8U, static_cast<float>(layout.CellsX) / 4.0F, 3, layout.WrapWidth());
			layout.Surface[x] = height * (0.17F + (0.02F + 0.03F * rough) * hills);
		}
		FillBelowSurface(layout);
		// Caves grown by a cellular automaton: rock and air in blobs from noise, then each cell becomes what most of its neighbours are, a few
		// times over, which rounds the blobs off and closes the smallest.
		const int top = static_cast<int>(height * 0.24F) / c_Cell;
		const int bottom = layout.CellsY - 7;
		std::vector<uint8_t> rock(static_cast<size_t>(layout.CellsX) * layout.CellsY, 1);
		auto rockAt = [&layout, &rock](int x, int y) -> uint8_t& { return rock[static_cast<size_t>(y) * layout.CellsX + x]; };
		const float threshold = 0.2F - 0.24F * caves;
		for (int y = top; y < bottom; ++y) {
			for (int x = 0; x < layout.CellsX; ++x) {
				float noise = Fractal(static_cast<float>(x), static_cast<float>(y) * 1.4F, layout.Seed + 9U, 9.0F + 4.0F * rough, 3, layout.WrapWidth());
				noise += (Unit(static_cast<uint32_t>(x), static_cast<uint32_t>(y), layout.Seed + 10U) - 0.5F) * 0.25F;
				rockAt(x, y) = noise > threshold ? 0 : 1;
			}
		}
		std::vector<uint8_t> next = rock;
		for (int pass = 0; pass < 4; ++pass) {
			for (int y = top; y < bottom; ++y) {
				for (int x = 0; x < layout.CellsX; ++x) {
					int walls = 0;
					for (int dy = -1; dy <= 1; ++dy) {
						for (int dx = -1; dx <= 1; ++dx) {
							int nx = x + dx;
							if (layout.WrapX) {
								nx = (nx + layout.CellsX) % layout.CellsX;
							} else if (nx < 0 || nx >= layout.CellsX) {
								++walls;
								continue;
							}
							walls += rockAt(nx, y + dy);
						}
					}
					next[static_cast<size_t>(y) * layout.CellsX + x] = walls >= 5 ? 1 : 0;
				}
			}
			rock = next;
		}
		// The rock smoothed into densities, so the caves' walls are rounded rather than square.
		for (int y = top; y < bottom; ++y) {
			for (int x = 0; x < layout.CellsX; ++x) {
				float sum = 0.0F;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						int nx = layout.WrapX ? (x + dx + layout.CellsX) % layout.CellsX : std::clamp(x + dx, 0, layout.CellsX - 1);
						sum += rockAt(nx, y + dy) ? 1.0F : -1.0F;
					}
				}
				layout.At(x, y) = std::min(layout.At(x, y), sum / 9.0F * 2.0F);
			}
		}
		// A main tunnel from one end to the other, so every part of the maze is on a way through, and a few shafts down from the surface.
		const float middle = (static_cast<float>(top) + static_cast<float>(bottom)) * 0.5F;
		layout.Worm(random, 0.0F, middle, 0.0F, static_cast<int>(static_cast<float>(layout.CellsX) / 1.8F) + 4, 4.2F, static_cast<float>(top) + 4.0F, static_cast<float>(bottom) - 4.0F);
		int shafts = std::max(2, layout.CellsX / 160);
		for (int i = 0; i < shafts; ++i) {
			float x = static_cast<float>(layout.CellsX) * (static_cast<float>(i) + random.Range(0.3F, 0.7F)) / static_cast<float>(shafts);
			int column = std::clamp(static_cast<int>(x), 0, layout.CellsX - 1);
			float surface = layout.Surface[column] / static_cast<float>(c_Cell);
			// Slanted, so it can be walked and jetpacked rather than only fallen down.
			layout.Tunnel(x, surface - 2.0F, x + random.Range(-12.0F, 12.0F), middle, 3.6F);
		}
	}

	/// An island hanging in the air: a flat-ish top and a tapering underside. Its middle and half its width in cells, its top's height.
	void AddIsland(Layout& layout, Random& random, float middleX, float halfWidth, float top, float rough) {
		float depth = halfWidth * random.Range(0.5F, 0.9F) + 3.0F;
		uint32_t seed = layout.Seed + 11U + random.Next() % 1000U;
		int from = static_cast<int>(middleX - halfWidth) - 2;
		int to = static_cast<int>(middleX + halfWidth) + 2;
		for (int x = from; x <= to; ++x) {
			float t = (static_cast<float>(x) + 0.5F - middleX) / halfWidth;
			float across = 1.0F - t * t;
			if (across <= 0.0F) {
				continue;
			}
			float shoulder = std::sqrt(across);
			float topHere = top + (1.0F - shoulder) * 2.0F + Fractal(static_cast<float>(x), 1.5F, seed, 8.0F, 2, 0.0F) * (0.5F + 1.5F * rough);
			float bottomHere = top + 2.0F + depth * across * (0.75F + 0.25F * Fractal(static_cast<float>(x), 4.5F, seed + 1U, 4.0F, 2, 0.0F));
			for (int y = static_cast<int>(topHere) - 2; y <= static_cast<int>(bottomHere) + 2; ++y) {
				float fy = static_cast<float>(y) + 0.5F;
				float inside = std::min({fy - topHere, bottomHere - fy, shoulder * halfWidth * 0.5F});
				layout.Add(x, y, std::clamp(inside, -4.0F, 8.0F));
			}
		}
	}

	void MakeFloatingIslands(Layout& layout, Random& random, float rough, float caves) {
		const float cellsX = static_cast<float>(layout.CellsX);
		const float cellsY = static_cast<float>(layout.CellsY);
		std::fill(layout.Density.begin(), layout.Density.end(), -4.0F);
		// A big island at each end for the bases (and their bunkers), then rows of islands between with gaps a jetpack can cross.
		AddIsland(layout, random, cellsX * 0.08F, std::max(22.0F, cellsX * 0.06F), cellsY * 0.5F, rough);
		AddIsland(layout, random, cellsX * 0.92F, std::max(22.0F, cellsX * 0.06F), cellsY * 0.5F, rough);
		const float rows[] = {0.32F, 0.52F, 0.72F};
		for (int row = 0; row < 3; ++row) {
			float x = cellsX * 0.16F + random.Range(0.0F, 12.0F) + static_cast<float>(row) * 6.0F;
			float end = cellsX * 0.84F;
			while (x < end) {
				float halfWidth = random.Range(8.0F, 18.0F + 16.0F * (1.0F - rough));
				float middle = x + halfWidth;
				if (middle + halfWidth > end + 6.0F) {
					break;
				}
				float top = cellsY * rows[row] + random.Range(-5.0F, 5.0F) * (0.5F + rough);
				AddIsland(layout, random, middle, halfWidth, top, rough);
				x = middle + halfWidth + random.Range(6.0F, 14.0F + 8.0F * rough);
			}
		}
		// A few little stepping stones, high up.
		int stones = static_cast<int>(cellsX / 60.0F);
		for (int i = 0; i < stones; ++i) {
			AddIsland(layout, random, random.Range(cellsX * 0.2F, cellsX * 0.8F), random.Range(3.0F, 6.0F), cellsY * random.Range(0.15F, 0.3F), rough);
		}
		(void)caves;
	}

	/// A flat box of solid, its corners in cells.
	void AddBox(Layout& layout, float left, float top, float right, float bottom) {
		for (int y = static_cast<int>(top) - 2; y <= static_cast<int>(bottom) + 2; ++y) {
			for (int x = static_cast<int>(left) - 2; x <= static_cast<int>(right) + 2; ++x) {
				float fx = static_cast<float>(x) + 0.5F;
				float fy = static_cast<float>(y) + 0.5F;
				float inside = std::min({fx - left, right - fx, fy - top, bottom - fy});
				layout.Add(x, y, std::clamp(inside, -4.0F, 8.0F));
			}
		}
	}

	void MakeSkyPlatforms(Layout& layout, Random& random, float rough, float caves) {
		const float cellsX = static_cast<float>(layout.CellsX);
		const float cellsY = static_cast<float>(layout.CellsY);
		std::fill(layout.Density.begin(), layout.Density.end(), -4.0F);
		// A wide platform at each end for the bases.
		AddBox(layout, cellsX * 0.08F - 20.0F, cellsY * 0.6F, cellsX * 0.08F + 20.0F, cellsY * 0.6F + 4.0F);
		AddBox(layout, cellsX * 0.92F - 20.0F, cellsY * 0.6F, cellsX * 0.92F + 20.0F, cellsY * 0.6F + 4.0F);
		const float spacingX = 30.0F;
		const float spacingY = 20.0F + 4.0F * rough;
		int column = 0;
		for (float x = cellsX * 0.08F + 30.0F; x < cellsX * 0.92F - 52.0F; x += spacingX, ++column) {
			for (float y = cellsY * 0.24F + (column % 2 == 0 ? 0.0F : spacingY * 0.5F); y < cellsY * 0.86F; y += spacingY) {
				if (random.Chance(0.2F + 0.15F * rough)) {
					continue;
				}
				float width = random.Range(10.0F, 24.0F);
				float thickness = random.Range(2.0F, 3.5F);
				float left = x + random.Range(-5.0F, 5.0F) * (0.4F + rough);
				float top = y + random.Range(-3.0F, 3.0F) * (0.4F + rough);
				AddBox(layout, left, top, left + width, top + thickness);
			}
		}
		(void)caves;
	}

	void MakePillars(Layout& layout, Random& random, float rough, float caves) {
		const float height = static_cast<float>(layout.Height);
		const float cellsX = static_cast<float>(layout.CellsX);
		const float cellsY = static_cast<float>(layout.CellsY);
		for (int x = 0; x < layout.CellsX; ++x) {
			layout.Surface[x] = height * (0.86F + 0.02F * Fractal(static_cast<float>(x), 0.5F, layout.Seed + 12U, 20.0F, 2, layout.WrapWidth()));
		}
		FillBelowSurface(layout);
		auto addColumn = [&layout](float middle, float halfWidth, float top, float capHalfWidth, float capDepth, uint32_t seed) {
			int from = static_cast<int>(middle - std::max(halfWidth, capHalfWidth)) - 3;
			int to = static_cast<int>(middle + std::max(halfWidth, capHalfWidth)) + 3;
			for (int x = from; x <= to; ++x) {
				for (int y = static_cast<int>(top) - 2; y < layout.CellsY; ++y) {
					float fx = static_cast<float>(x) + 0.5F;
					float fy = static_cast<float>(y) + 0.5F;
					float wobble = Fractal(fy, 0.5F, seed, 6.0F, 2, 0.0F) * 1.2F;
					float shaft = std::min(halfWidth - std::abs(fx - middle - wobble), fy - top);
					float cap = std::min({capHalfWidth - std::abs(fx - middle), fy - top, top + capDepth - fy});
					layout.Add(x, y, std::clamp(std::max(shaft, cap), -4.0F, 8.0F));
				}
			}
		};
		// A mesa at each end for the bases.
		addColumn(cellsX * 0.08F, 16.0F, cellsY * 0.5F, 19.0F, 4.0F, layout.Seed + 13U);
		addColumn(cellsX * 0.92F, 16.0F, cellsY * 0.5F, 19.0F, 4.0F, layout.Seed + 14U);
		std::vector<std::pair<float, float>> tops;
		for (float x = cellsX * 0.08F + 32.0F; x < cellsX * 0.92F - 26.0F; x += random.Range(16.0F, 34.0F)) {
			float halfWidth = random.Range(2.5F, 7.0F);
			float top = cellsY * random.Range(0.25F, 0.65F);
			bool capped = random.Chance(0.75F);
			addColumn(x, halfWidth, top, capped ? halfWidth * random.Range(1.6F, 2.6F) : 0.0F, capped ? random.Range(2.5F, 4.5F) : 0.0F, layout.Seed + 15U + static_cast<uint32_t>(x));
			tops.emplace_back(x, top);
		}
		// Natural bridges between some neighbours of about the same height.
		for (size_t i = 1; i < tops.size(); ++i) {
			auto [leftX, leftTop] = tops[i - 1];
			auto [rightX, rightTop] = tops[i];
			if (std::abs(leftTop - rightTop) < 10.0F && random.Chance(0.25F + 0.3F * rough)) {
				float y = std::max(leftTop, rightTop) + 3.0F;
				for (float x = leftX; x <= rightX; x += 1.0F) {
					float t = (x - leftX) / std::max(1.0F, rightX - leftX);
					float sag = 4.0F * t * (1.0F - t) * 2.0F;
					AddBox(layout, x, y + sag, x + 1.0F, y + sag + 2.0F);
				}
			}
		}
		AddCaves(layout, random, caves * 0.3F, 4.0F);
	}

	void MakeHoneycomb(Layout& layout, Random& random, float rough, float caves) {
		const float height = static_cast<float>(layout.Height);
		const float cellsX = static_cast<float>(layout.CellsX);
		for (int x = 0; x < layout.CellsX; ++x) {
			layout.Surface[x] = height * (0.2F + 0.03F * Fractal(static_cast<float>(x), 0.5F, layout.Seed + 16U, cellsX / 4.0F, 3, layout.WrapWidth()));
		}
		FillBelowSurface(layout);
		const float spacingX = 26.0F;
		const float spacingY = 22.0F;
		const float firstY = height * 0.2F / c_Cell + 16.0F;
		const float lastY = static_cast<float>(layout.CellsY) - 14.0F;
		struct Chamber {
			float X;
			float Y;
			bool Open;
		};
		std::vector<std::vector<Chamber>> rows;
		for (float y = firstY; y <= lastY; y += spacingY) {
			std::vector<Chamber>& row = rows.emplace_back();
			float offset = rows.size() % 2 == 0 ? spacingX * 0.5F : 0.0F;
			for (float x = 10.0F + offset; x < cellsX - 10.0F; x += spacingX) {
				Chamber chamber{x + random.Range(-4.0F, 4.0F) * (0.3F + rough), y + random.Range(-4.0F, 4.0F) * (0.3F + rough), !random.Chance(0.15F)};
				if (chamber.Open) {
					layout.Carve(chamber.X, chamber.Y, random.Range(7.0F, 10.0F + 2.0F * caves));
				}
				row.push_back(chamber);
			}
		}
		const float tunnel = 3.6F;
		for (size_t r = 0; r < rows.size(); ++r) {
			for (size_t i = 0; i < rows[r].size(); ++i) {
				const Chamber& here = rows[r][i];
				if (!here.Open) {
					continue;
				}
				// Along the row, nearly always; down to the row below, often.
				if (i + 1 < rows[r].size() && rows[r][i + 1].Open && random.Chance(0.85F)) {
					layout.Tunnel(here.X, here.Y, rows[r][i + 1].X, rows[r][i + 1].Y, tunnel);
				}
				if (r + 1 < rows.size()) {
					for (const Chamber& below: rows[r + 1]) {
						if (below.Open && std::abs(below.X - here.X) < spacingX * 0.7F && random.Chance(0.35F + 0.3F * caves)) {
							layout.Tunnel(here.X, here.Y, below.X, below.Y, tunnel);
						}
					}
				}
			}
		}
		// Ways in from the surface.
		if (!rows.empty()) {
			int ways = std::max(2, layout.CellsX / 150);
			for (int i = 0; i < ways; ++i) {
				const Chamber& chamber = rows.front()[std::min(rows.front().size() - 1, static_cast<size_t>(random.Int(0, static_cast<int>(rows.front().size()) - 1)))];
				int column = std::clamp(static_cast<int>(chamber.X), 0, layout.CellsX - 1);
				layout.Tunnel(chamber.X + random.Range(-8.0F, 8.0F), layout.Surface[column] / c_Cell - 2.0F, chamber.X, chamber.Y, tunnel);
			}
		}
	}

	void MakeShattered(Layout& layout, Random& random, float rough, float caves) {
		const float height = static_cast<float>(layout.Height);
		const float cellsX = static_cast<float>(layout.CellsX);
		// The ground cut into slabs, each lifted or dropped and tipped.
		std::vector<float> cuts{0.0F};
		while (cuts.back() < cellsX) {
			cuts.push_back(cuts.back() + random.Range(18.0F, 50.0F));
		}
		std::vector<float> lifts;
		std::vector<float> tilts;
		for (size_t i = 0; i < cuts.size(); ++i) {
			lifts.push_back(random.Range(-1.0F, 1.0F) * height * 0.07F * (0.5F + rough));
			tilts.push_back(random.Range(-0.35F, 0.35F) * (0.4F + rough));
		}
		for (int x = 0; x < layout.CellsX; ++x) {
			float fx = static_cast<float>(x) + 0.5F;
			size_t slab = static_cast<size_t>(std::upper_bound(cuts.begin(), cuts.end(), fx) - cuts.begin()) - 1;
			slab = std::min(slab, lifts.size() - 1);
			float middle = (cuts[slab] + (slab + 1 < cuts.size() ? cuts[slab + 1] : cellsX)) * 0.5F;
			float base = height * (0.55F + 0.05F * Fractal(fx, 0.5F, layout.Seed + 17U, cellsX / 3.0F, 3, layout.WrapWidth()));
			layout.Surface[x] = base + lifts[slab] + tilts[slab] * (fx - middle) * c_Cell;
		}
		FillBelowSurface(layout);
		// Crevasses where the slabs part.
		for (size_t i = 1; i + 1 < cuts.size(); ++i) {
			float x = cuts[i];
			int column = std::clamp(static_cast<int>(x), 0, layout.CellsX - 1);
			float top = std::min(layout.Surface[std::max(0, column - 2)], layout.Surface[std::min(layout.CellsX - 1, column + 2)]) / c_Cell;
			float depth = random.Range(10.0F, 30.0F);
			float lean = random.Range(-4.0F, 4.0F);
			float width = random.Range(1.4F, 2.8F);
			for (float step = 0.0F; step < depth; step += 1.0F) {
				layout.Carve(x + lean * step / depth, top + step, width * (1.0F - 0.6F * step / depth));
			}
		}
		// Chunks thrown up into the air.
		int chunks = static_cast<int>(cellsX / 80.0F * (0.5F + rough));
		for (int i = 0; i < chunks; ++i) {
			float x = random.Range(cellsX * 0.15F, cellsX * 0.85F);
			int column = std::clamp(static_cast<int>(x), 0, layout.CellsX - 1);
			float top = layout.Surface[column] / c_Cell - random.Range(10.0F, 22.0F);
			AddIsland(layout, random, x, random.Range(3.0F, 7.0F), top, rough);
		}
		AddCaves(layout, random, caves * 0.5F, 10.0F);
	}

	/// Water in the hollows of the surface: each hollow filled part way up to where it would spill over, by how much water is wanted.
	void AddWater(Layout& layout, float amount) {
		layout.WaterTop.assign(layout.CellsX, -1);
		if (layout.Surface.empty() || amount <= 0.0F) {
			return;
		}
		const int count = layout.CellsX;
		// Heights of the ground from the bottom, so "higher" reads the right way round.
		std::vector<float> ground(count);
		for (int x = 0; x < count; ++x) {
			ground[x] = static_cast<float>(layout.Height) - layout.Surface[x];
		}
		std::vector<float> fromLeft(count);
		std::vector<float> fromRight(count);
		for (int x = 0; x < count; ++x) {
			fromLeft[x] = std::max(ground[x], x > 0 ? fromLeft[x - 1] : ground[x]);
		}
		for (int x = count - 1; x >= 0; --x) {
			fromRight[x] = std::max(ground[x], x + 1 < count ? fromRight[x + 1] : ground[x]);
		}
		int x = 0;
		while (x < count) {
			float spill = std::min(fromLeft[x], fromRight[x]);
			if (spill - ground[x] < 1.0F) {
				++x;
				continue;
			}
			int start = x;
			float bottom = ground[x];
			float level = spill;
			while (x < count && std::min(fromLeft[x], fromRight[x]) - ground[x] >= 1.0F) {
				bottom = std::min(bottom, ground[x]);
				level = std::max(level, std::min(fromLeft[x], fromRight[x]));
				++x;
			}
			// Puddles too shallow to matter are left dry.
			if (level - bottom < static_cast<float>(c_Cell) * 4.0F) {
				continue;
			}
			float surface = bottom + (level - bottom) * std::clamp(amount, 0.0F, 0.95F);
			int top = static_cast<int>(static_cast<float>(layout.Height) - surface);
			for (int i = start; i < x; ++i) {
				if (ground[i] < surface) {
					layout.WaterTop[i] = top;
				}
			}
		}
	}

	/// A bunker standing on the ground at each end, where the ground there is flattest (so it needs the least filled in under it).
	void AddBunkers(Layout& layout) {
		const int footprint = c_BunkerWidth / c_Cell;
		for (int side = 0; side < 2; ++side) {
			const float target = static_cast<float>(layout.CellsX) * (side == 0 ? 0.08F : 0.92F);
			const int from = static_cast<int>(static_cast<float>(layout.CellsX) * (side == 0 ? 0.03F : 0.83F));
			const int to = static_cast<int>(static_cast<float>(layout.CellsX) * (side == 0 ? 0.17F : 0.97F)) - footprint;
			int bestLeft = -1;
			int bestTop = -1;
			float bestScore = 0.0F;
			for (int left = from; left <= to; ++left) {
				int highest = -1;
				int lowest = -1;
				int grounded = 0;
				bool wet = false;
				for (int x = left; x < left + footprint; ++x) {
					int column = std::clamp(x, 0, layout.CellsX - 1);
					int ground = layout.TopOfGround(column);
					wet = wet || (!layout.WaterTop.empty() && layout.WaterTop[column] >= 0);
					if (ground < 0) {
						continue;
					}
					++grounded;
					highest = highest < 0 ? ground : std::min(highest, ground);
					lowest = std::max(lowest, ground);
				}
				// Most of it on ground, and none of it in water.
				if (wet || grounded * 10 < footprint * 7) {
					continue;
				}
				float middle = static_cast<float>(left) + static_cast<float>(footprint) * 0.5F;
				float score = static_cast<float>(lowest - highest) + std::abs(middle - target) * 0.1F + static_cast<float>(footprint - grounded) * 2.0F;
				if (bestLeft < 0 || score < bestScore) {
					bestLeft = left;
					bestTop = highest;
					bestScore = score;
				}
			}
			if (bestLeft < 0) {
				continue;
			}
			Bunker bunker;
			bunker.Width = c_BunkerWidth;
			bunker.Height = c_BunkerHeight;
			bunker.Left = bestLeft * c_Cell;
			// The floor's top sits on the ground's highest point under it, so the floor is solid all the way along.
			bunker.Top = bestTop * c_Cell + c_Cell / 2 - c_BunkerHeight + c_BunkerWall;
			if (bunker.Top < c_Cell * 4) {
				continue;
			}
			layout.Bunkers.push_back(bunker);
		}
	}

	Layout MakeLayout(const MapGen::Recipe& recipe) {
		Layout layout;
		MapGen::Dimensions(recipe, layout.Width, layout.Height);
		layout.CellsX = layout.Width / c_Cell;
		layout.CellsY = layout.Height / c_Cell;
		layout.WrapX = recipe.WrapX;
		layout.Info = &StyleOf(recipe.MapStyle);
		layout.Kind = recipe.MapStyle;
		layout.Seed = Hash(recipe.Seed, static_cast<uint32_t>(recipe.MapStyle) * 7919U + static_cast<uint32_t>(recipe.MapSize));
		layout.Density.assign(static_cast<size_t>(layout.CellsX) * layout.CellsY, -4.0F);
		if (layout.Info->Ground) {
			layout.Surface.assign(layout.CellsX, static_cast<float>(layout.Height));
		}
		Random random(layout.Seed, 1U);
		const float rough = Clamp01(static_cast<float>(recipe.Roughness) / 100.0F);
		const float caves = Clamp01(static_cast<float>(recipe.Caves) / 100.0F);
		switch (recipe.MapStyle) {
			case MapGen::Style::Grasslands:
				MakeGrasslands(layout, random, rough, caves);
				break;
			case MapGen::Style::Mountains:
				MakeMountains(layout, random, rough, caves);
				break;
			case MapGen::Style::DesertCanyon:
				MakeDesertCanyon(layout, random, rough, caves);
				break;
			case MapGen::Style::Caves:
				MakeCaves(layout, random, rough, caves);
				break;
			case MapGen::Style::FloatingIslands:
				MakeFloatingIslands(layout, random, rough, caves);
				break;
			case MapGen::Style::SkyPlatforms:
				MakeSkyPlatforms(layout, random, rough, caves);
				break;
			case MapGen::Style::Pillars:
				MakePillars(layout, random, rough, caves);
				break;
			case MapGen::Style::Honeycomb:
				MakeHoneycomb(layout, random, rough, caves);
				break;
			case MapGen::Style::Shattered:
				MakeShattered(layout, random, rough, caves);
				break;
			default:
				break;
		}
		// Nothing above the top: the sky is open, so ships can come in.
		for (int y = 0; y < std::min(3, layout.CellsY); ++y) {
			for (int x = 0; x < layout.CellsX; ++x) {
				layout.At(x, y) = std::min(layout.At(x, y), -1.0F);
			}
		}
		bool water = recipe.MapStyle != MapGen::Style::Caves && recipe.MapStyle != MapGen::Style::Honeycomb && MapGen::HasGround(recipe.MapStyle);
		AddWater(layout, water ? static_cast<float>(recipe.Water) / 100.0F : 0.0F);
		if (recipe.Bunkers) {
			AddBunkers(layout);
		}
		return layout;
	}
#pragma endregion

#pragma region Materials
	/// The materials a map is made of, by ID, looked up by name on the main thread.
	struct Materials {
		std::array<int, 5> Bands{};
		int Pocket = -1;
		int Snow = -1;
		int Bedrock = -1;
		int Water = -1;
		int Concrete = -1;
		int Cavity = MaterialColorKeys::g_MaterialCavity;
		int Air = MaterialColorKeys::g_MaterialAir;
	};

	int MaterialID(const char* name) {
		if (!name) {
			return -1;
		}
		const Material* material = dynamic_cast<const Material*>(g_PresetMan.GetEntityPreset("Material", name));
		return material ? material->GetIndex() : -1;
	}

	Materials LookUpMaterials(const StyleInfo& info) {
		Materials materials;
		int last = MaterialID("Stone");
		for (size_t i = 0; i < info.Bands.size(); ++i) {
			int id = MaterialID(info.Bands[i].Material);
			materials.Bands[i] = id >= 0 ? id : last;
			last = materials.Bands[i];
		}
		materials.Pocket = MaterialID(info.Pocket);
		materials.Snow = MaterialID("Snow");
		materials.Bedrock = MaterialID("Bedrock");
		materials.Water = MaterialID("Water");
		materials.Concrete = MaterialID("Concrete");
		return materials;
	}

	/// The material of ground so many pixels under its surface.
	int GroundMaterial(const Layout& layout, const Materials& materials, int x, int y, int depth, float surfaceY) {
		const StyleInfo& info = *layout.Info;
		// The bands' edges wander a little, so they aren't ruled lines.
		float wander = Fractal(static_cast<float>(x) / c_Cell, 2.5F, layout.Seed + 31U, 10.0F, 2, layout.WrapWidth()) * 10.0F;
		float at = static_cast<float>(depth) + wander;
		// Snow on the high tops of mountains.
		if (layout.Kind == MapGen::Style::Mountains && materials.Snow >= 0 && surfaceY < static_cast<float>(layout.Height) * 0.32F && at < 16.0F) {
			return materials.Snow;
		}
		int band = 0;
		float bottom = 0.0F;
		for (; band < static_cast<int>(info.Bands.size()); ++band) {
			if (!info.Bands[band].Material || info.Bands[band].Thickness <= 0) {
				break;
			}
			bottom += static_cast<float>(info.Bands[band].Thickness);
			if (at < bottom) {
				break;
			}
		}
		band = std::min(band, static_cast<int>(info.Bands.size()) - 1);
		if (band > 0 && materials.Pocket >= 0) {
			float pocket = Fractal(static_cast<float>(x) / c_Cell, static_cast<float>(y) / c_Cell, layout.Seed + 32U, 7.0F, 2, layout.WrapWidth());
			if (pocket > 0.45F) {
				return materials.Pocket;
			}
		}
		return materials.Bands[band];
	}

	/// The material of one pixel of the map, given whether it's solid and how deep it is in the ground.
	int PixelMaterial(const Layout& layout, const Materials& materials, int x, int y, bool solid, int runDepth) {
		const bool ground = layout.Info->Ground;
		float surface = ground ? layout.SurfaceAt(static_cast<float>(x)) : static_cast<float>(layout.Height);
		if (solid) {
			if (ground && y >= layout.Height - 16 && materials.Bedrock >= 0) {
				return materials.Bedrock;
			}
			int depth = ground && static_cast<float>(y) > surface ? static_cast<int>(static_cast<float>(y) - surface) : runDepth;
			return GroundMaterial(layout, materials, x, y, depth, surface);
		}
		int column = std::clamp(x / c_Cell, 0, layout.CellsX - 1);
		if (layout.WaterTop[column] >= 0 && y >= layout.WaterTop[column] && materials.Water >= 0 && static_cast<float>(y) <= surface + c_Cell) {
			return materials.Water;
		}
		// Air under the ground's surface is a cave: it gets the dark earth behind it rather than the sky.
		return ground && static_cast<float>(y) > surface + 6.0F ? materials.Cavity : materials.Air;
	}

	/// Draws the bunkers into a material picture (a pixel for every cell's worth of map when scale is c_Cell): concrete walls, roof and floor,
	/// a doorway each side, the room inside dark, a way in dug from each doorway and ground filled in under the floor.
	void DrawBunkers(const Layout& layout, const Materials& materials, std::vector<uint8_t>& pixels, int width, int height, int scale) {
		auto set = [&](int mapX, int mapY, int material) {
			int x = mapX / scale;
			int y = mapY / scale;
			if (layout.WrapX) {
				x = ((x % width) + width) % width;
			}
			if (x >= 0 && x < width && y >= 0 && y < height && material >= 0) {
				pixels[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(material);
			}
		};
		auto openAt = [&](int mapX, int mapY) {
			// Air dug out: a cave if it's under the ground's surface, sky if not.
			bool under = layout.Info->Ground && static_cast<float>(mapY) > layout.SurfaceAt(static_cast<float>(mapX)) + 6.0F;
			return under ? materials.Cavity : materials.Air;
		};
		for (const Bunker& bunker: layout.Bunkers) {
			const int left = bunker.Left;
			const int right = bunker.Left + bunker.Width;
			const int top = bunker.Top;
			const int floorTop = bunker.Top + bunker.Height - c_BunkerWall;
			const int bottom = bunker.Top + bunker.Height;
			for (int y = top; y < bottom; y += scale) {
				for (int x = left; x < right; x += scale) {
					bool wall = x < left + c_BunkerWall || x >= right - c_BunkerWall || y < top + c_BunkerWall || y >= floorTop;
					bool doorway = (x < left + c_BunkerWall || x >= right - c_BunkerWall) && y >= floorTop - c_DoorHeight && y < floorTop;
					set(x, y, wall && !doorway ? materials.Concrete : materials.Cavity);
				}
			}
			// A way in at each doorway, dug out of any ground in front of it.
			for (int y = floorTop - c_DoorHeight; y < floorTop; y += scale) {
				for (int x = left - 48; x < left; x += scale) {
					set(x, y, openAt(x, y));
				}
				for (int x = right; x < right + 48; x += scale) {
					set(x, y, openAt(x, y));
				}
			}
			// Ground under the floor down to the ground, so it doesn't stand on nothing where the ground dips.
			for (int x = left; x < right; x += scale) {
				for (int y = bottom; y < std::min(bottom + 160, layout.Height); y += scale) {
					int px = layout.WrapX ? (((x / scale) % width) + width) % width : std::clamp(x / scale, 0, width - 1);
					int py = std::clamp(y / scale, 0, height - 1);
					int existing = pixels[static_cast<size_t>(py) * width + px];
					if (existing != materials.Air && existing != materials.Cavity && existing != materials.Water) {
						break;
					}
					set(x, y, materials.Bands[std::min<size_t>(1, materials.Bands.size() - 1)]);
				}
			}
		}
	}
#pragma endregion

#pragma region Making the map
	/// What the worker thread needs, gathered on the main thread.
	struct Job {
		MapGen::Recipe Recipe;
		Materials Mats;
		std::array<RGB, c_PaletteEntriesNumber> Palette{};
		std::vector<std::string> Backdrop; //!< The backdrop layers' preset names, nearest first.
		std::string Name;
		std::string FileStem; //!< The files' name without the extension, in the folder.
		std::string ModulePath; //!< "UserScenes.rte/Generated/<stem>": as the game's files refer to them.
		std::string DiskPath; //!< The same, on disk.
	};

	struct Result {
		bool Ok = false;
		std::string Error;
		std::string Name;
		std::string IniModulePath;
	};

	std::future<Result> s_Work;
	std::set<std::string> s_Generated;

	std::string Hex(uint32_t value) {
		char text[9];
		std::snprintf(text, sizeof(text), "%08X", value);
		return text;
	}

	/// Deletes the oldest maps made, so the folder doesn't grow for ever.
	void TidyFolder(const std::filesystem::path& folder, const std::string& keepStem) {
		std::error_code error;
		std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> inis;
		for (const std::filesystem::directory_entry& entry: std::filesystem::directory_iterator(folder, error)) {
			if (entry.path().extension() == ".ini" && entry.path().stem().string() != keepStem) {
				inis.emplace_back(entry.last_write_time(error), entry.path());
			}
		}
		std::sort(inis.begin(), inis.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		for (size_t i = c_FilesKept; i < inis.size(); ++i) {
			std::filesystem::path stem = inis[i].second;
			std::filesystem::remove(stem, error);
			stem.replace_extension();
			std::filesystem::remove(stem.string() + " Mat.png", error);
		}
	}

	Result MakeMap(const Job& job) {
		Result result;
		result.Name = job.Name;
		Layout layout = MakeLayout(job.Recipe);
		const int width = layout.Width;
		const int height = layout.Height;
		const float detail = layout.Info->Detail;
		const Materials& materials = job.Mats;

		// Every pixel: solid or not from the layout, with its edges roughened, then its material column by column (how deep it is
		// depends on what's above it).
		std::vector<uint8_t> pixels(static_cast<size_t>(width) * height, 0);
		std::vector<int> columns(width);
		std::iota(columns.begin(), columns.end(), 0);
		std::for_each(std::execution::par_unseq, columns.begin(), columns.end(), [&](int x) {
			int runDepth = 0;
			const float cellX = (static_cast<float>(x) + 0.5F) / c_Cell;
			for (int y = 0; y < height; ++y) {
				const float cellY = (static_cast<float>(y) + 0.5F) / c_Cell;
				float density = layout.Sample(cellX, cellY);
				if (detail > 0.0F && density > -2.0F && density < 2.0F) {
					density += Fractal(cellX * 2.5F, cellY * 2.5F, layout.Seed + 41U, 2.0F, 2, layout.WrapX ? static_cast<float>(layout.CellsX) * 2.5F : 0.0F) * detail * 0.6F;
				}
				bool solid = density > 0.0F;
				runDepth = solid ? runDepth + 1 : 0;
				pixels[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(PixelMaterial(layout, materials, x, y, solid, runDepth - 1));
			}
		});
		DrawBunkers(layout, materials, pixels, width, height, 1);

		std::error_code error;
		std::filesystem::path folder = std::filesystem::path(job.DiskPath).parent_path();
		std::filesystem::create_directories(folder, error);
		TidyFolder(folder, job.FileStem);

		BITMAP* bitmap = create_bitmap_ex(8, width, height);
		if (!bitmap) {
			result.Error = "Not enough memory for the map's picture.";
			return result;
		}
		for (int y = 0; y < height; ++y) {
			std::memcpy(bitmap->line[y], &pixels[static_cast<size_t>(y) * width], width);
		}
		PALETTE palette;
		std::copy(job.Palette.begin(), job.Palette.end(), palette);
		std::string picturePath = job.DiskPath + " Mat.png";
		int saved = save_png(picturePath.c_str(), bitmap, palette);
		destroy_bitmap(bitmap);
		if (saved != 0) {
			result.Error = "Couldn't write " + picturePath;
			return result;
		}

		const StyleInfo& info = *layout.Info;
		std::ostringstream ini;
		ini << "// Made by the map generator (MapGen.cpp) from the recipe " << job.Recipe.Code() << ". Not read at start-up: it lasts until the game closes.\n\n";
		ini << "AddScene = Scene\n";
		ini << "\tPresetName = " << job.Name << "\n";
		ini << "\tDescription = A generated map: " << info.Name << ", " << MapGen::SizeName(job.Recipe.MapSize) << ", " << width << " by " << height << ". " << info.Blurb << " Recipe " << job.Recipe.Code() << ".\n";
		ini << "\tTerrain = SLTerrain\n";
		ini << "\t\tPresetName = " << job.Name << " Terrain\n";
		ini << "\t\tBitmapFile = ContentFile\n\t\t\tFilePath = " << job.ModulePath << " Mat.png\n";
		ini << "\t\tWrapX = " << (layout.WrapX ? 1 : 0) << "\n\t\tWrapY = 0\n";
		ini << "\t\tBackgroundTexture = ContentFile\n\t\t\tFilePath = Base.rte/Scenes/Textures/DirtDark.png\n";
		ini << info.Dressing;
		for (const Bunker& bunker: layout.Bunkers) {
			int lightX = bunker.Left + bunker.Width / 2;
			int lightY = bunker.Top + c_BunkerWall + 6;
			ini << "\t\tAddLight = TerrainLight\n\t\t\tPosition = Vector\n\t\t\t\tX = " << lightX << "\n\t\t\t\tY = " << lightY << "\n";
			ini << "\t\t\tColor = Color\n\t\t\t\tR = 255\n\t\t\t\tG = 214\n\t\t\t\tB = 160\n\t\t\tRadius = 130\n\t\t\tIntensity = 0.9\n";
		}
		ini << "\tGlobalAcceleration = Vector\n\t\tX = 0\n\t\tY = 20\n";
		for (const std::string& layer: job.Backdrop) {
			ini << "\tAddBackgroundLayer = SLBackground\n\t\tCopyOf = " << layer << "\n";
		}
		std::ofstream file(job.DiskPath + ".ini", std::ios::trunc);
		file << ini.str();
		file.close();
		if (!file) {
			result.Error = "Couldn't write " + job.DiskPath + ".ini";
			return result;
		}
		result.IniModulePath = job.ModulePath + ".ini";
		result.Ok = true;
		return result;
	}
#pragma endregion
} // namespace

std::string MapGen::Recipe::Code() const {
	static constexpr char c_SizeLetters[] = {'S', 'M', 'L', 'X'};
	char text[64];
	std::snprintf(text, sizeof(text), "CC1-%c%02d-%08X-%02d%02d%02d%d%d", c_SizeLetters[std::clamp(static_cast<int>(MapSize), 0, 3)], static_cast<int>(MapStyle), Seed, std::clamp(Roughness, 0, 99), std::clamp(Caves, 0, 99), std::clamp(Water, 0, 99), Bunkers ? 1 : 0, WrapX ? 1 : 0);
	return text;
}

const char* MapGen::StyleName(Style style) { return StyleOf(style).Name; }

const char* MapGen::StyleBlurb(Style style) { return StyleOf(style).Blurb; }

const char* MapGen::SizeName(Size size) { return c_SizeNames[std::clamp(static_cast<int>(size), 0, static_cast<int>(Size::Count) - 1)]; }

bool MapGen::HasGround(Style style) { return StyleOf(style).Ground; }

void MapGen::Dimensions(const Recipe& recipe, int& width, int& height) {
	const int* range = c_SizeRanges[std::clamp(static_cast<int>(recipe.MapSize), 0, static_cast<int>(Size::Count) - 1)];
	Random random(recipe.Seed, 99U);
	width = (random.Int(range[0], range[1]) / c_Cell) * c_Cell;
	height = (random.Int(range[2], range[3]) / c_Cell) * c_Cell;
}

std::string MapGen::MapName(const Recipe& recipe) {
	std::string code = recipe.Code();
	uint32_t hash = 2166136261U;
	for (char c: code) {
		hash = (hash ^ static_cast<unsigned char>(c)) * 16777619U;
	}
	return std::string("Generated ") + StyleName(recipe.MapStyle) + " " + Hex(hash);
}

MapGen::Picture MapGen::Preview(const Recipe& recipe) {
	Layout layout = MakeLayout(recipe);
	Materials materials = LookUpMaterials(*layout.Info);
	const int width = layout.CellsX;
	const int height = layout.CellsY;
	std::vector<uint8_t> cells(static_cast<size_t>(width) * height, 0);
	for (int x = 0; x < width; ++x) {
		int runDepth = 0;
		for (int y = 0; y < height; ++y) {
			bool solid = layout.At(x, y) > 0.0F;
			runDepth = solid ? runDepth + c_Cell : 0;
			cells[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(PixelMaterial(layout, materials, x * c_Cell + c_Cell / 2, y * c_Cell + c_Cell / 2, solid, runDepth - c_Cell));
		}
	}
	DrawBunkers(layout, materials, cells, width, height, c_Cell);

	Picture picture;
	picture.Width = width;
	picture.Height = height;
	picture.MapWidth = layout.Width;
	picture.MapHeight = layout.Height;
	picture.Pixels.resize(static_cast<size_t>(width) * height * 4);
	std::array<std::array<unsigned char, 3>, c_PaletteEntriesNumber> colors{};
	for (int id = 0; id < c_PaletteEntriesNumber; ++id) {
		Color color = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id))->GetColor();
		colors[id] = {static_cast<unsigned char>(color.GetR()), static_cast<unsigned char>(color.GetG()), static_cast<unsigned char>(color.GetB())};
	}
	for (int y = 0; y < height; ++y) {
		float up = static_cast<float>(y) / static_cast<float>(std::max(1, height - 1));
		for (int x = 0; x < width; ++x) {
			int id = cells[static_cast<size_t>(y) * width + x];
			unsigned char* out = &picture.Pixels[(static_cast<size_t>(y) * width + x) * 4];
			if (id == materials.Air) {
				// The sky, lighter toward the horizon.
				out[0] = static_cast<unsigned char>(Mix(70.0F, 150.0F, up));
				out[1] = static_cast<unsigned char>(Mix(110.0F, 190.0F, up));
				out[2] = static_cast<unsigned char>(Mix(170.0F, 225.0F, up));
			} else if (id == materials.Cavity) {
				out[0] = 48;
				out[1] = 38;
				out[2] = 32;
			} else {
				out[0] = colors[id][0];
				out[1] = colors[id][1];
				out[2] = colors[id][2];
			}
			out[3] = 255;
		}
	}
	return picture;
}

void MapGen::Start(const Recipe& recipe) {
	if (Busy()) {
		return;
	}
	Job job;
	job.Recipe = recipe;
	job.Mats = LookUpMaterials(StyleOf(recipe.MapStyle));
	PALETTE palette;
	get_palette(palette);
	std::copy(palette, palette + c_PaletteEntriesNumber, job.Palette.begin());
	if (const Scene* theme = dynamic_cast<const Scene*>(g_PresetMan.GetEntityPreset("Scene", std::string("Sandbox Small - ") + StyleOf(recipe.MapStyle).Backdrop))) {
		for (const SLBackground* layer: const_cast<Scene*>(theme)->GetBackLayers()) {
			job.Backdrop.push_back(layer->GetPresetName());
		}
	}
	job.Name = MapName(recipe);
	job.FileStem = "Generated " + job.Name.substr(job.Name.rfind(' ') + 1);
	job.ModulePath = c_UserScenesModuleName + c_Folder + job.FileStem;
	job.DiskPath = g_PresetMan.GetFullModulePath(job.ModulePath);
	s_Work = std::async(std::launch::async, [job]() { return MakeMap(job); });
}

bool MapGen::Busy() {
	return s_Work.valid();
}

bool MapGen::Update(std::string& name, std::string& error) {
	if (!s_Work.valid() || s_Work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
		return false;
	}
	Result result = s_Work.get();
	if (!result.Ok) {
		error = result.Error;
		return true;
	}
	// Read in as a preset of UserScenes.rte (the reader knows the module by the path), replacing one of the same name.
	Reader reader(result.IniModulePath, true);
	while (reader.NextProperty()) {
		reader.ReadPropName();
		g_PresetMan.GetEntityPreset(reader);
	}
	if (!g_PresetMan.GetEntityPreset("Scene", result.Name)) {
		error = "The map was made but couldn't be read in.";
		return true;
	}
	s_Generated.insert(result.Name);
	name = result.Name;
	return true;
}

bool MapGen::IsGenerated(const std::string& mapName) {
	return s_Generated.count(mapName) != 0;
}
