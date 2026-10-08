#include "FluidSim.h"
#include "Constants.h"
#include "ConsoleMan.h"
#include "EffectsParticles.h"
#include "PostProcessMan.h"
#include "Material.h"
#include "MovableMan.h"
#include "MovableObject.h"
#include "MOPixel.h"
#include "Atom.h"
#include "PresetMan.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "TerrainFire.h"
#include "TimerMan.h"
#include "WeatherEffects.h"
#include "Vector.h"
#include "RenderMan.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace RTE;

bool FluidSim::s_Enabled = true;
bool FluidSim::s_Powders = true;
bool FluidSim::s_Freezing = false;
bool FluidSim::s_BloodFlows = false;

namespace {
	enum class Liquid : unsigned char {
		None,
		Water,
		Lava,
		Acid,
		Oil,
		Powder, //!< Sand, snow and the like: falls and slides down slopes but doesn't flow level. Not a liquid to the rest of the game.
		Other //!< A liquid a material's own INI says flows (MaterialBehaviour::Flows) under a name the stock reactions don't know: it flows by its own numbers and reacts only as its behaviour says.
	};
	constexpr int c_LiquidKinds = 7;

	/// Speeds are kept in quarter pixels per step, in a byte per moving pixel.
	struct LiquidProperties {
		int Flow; //!< How far a pixel may run sideways per step.
		int Fall; //!< How far a pixel may fall per step.
		int MoveEvery; //!< Moves every this many sim updates; viscous liquids are slower.
		int Gravity; //!< How much falling speed it gains per step.
		int FlowGain; //!< How much sideways speed it gains per step while it has somewhere to run.
		int Weight; //!< Heavier sinks through lighter.
	};
	constexpr LiquidProperties c_Liquids[] = {
	    {0, 0, 1, 0, 0, 0}, // None
	    {12, 8, 1, 4, 16, 2}, // Water
	    {1, 2, 3, 2, 4, 4}, // Lava
	    {9, 8, 1, 4, 12, 3}, // Acid
	    {5, 5, 1, 3, 6, 1}, // Oil
	    {0, 5, 1, 2, 0, 5}, // Powder
	    {12, 8, 1, 4, 16, 2}, // Other: as water, unless its INI says otherwise.
	};
	static_assert(std::size(c_Liquids) == c_LiquidKinds, "A row of properties for each kind of liquid");
	constexpr int c_SplashSpeed = 14; //!< Liquid landing at least this fast may throw a drop.
	constexpr int c_MaxSplashesPerUpdate = 30;

	constexpr size_t c_MaxActive = 80000; //!< More than this many moving pixels wait their turn (see s_Waiting) rather than being forgotten. Measured with this many moving at once: 6 to 8 ms an update, with spikes to 11 ms.
	constexpr int c_RestSteps = 20; //!< A pixel that hasn't got any lower for this many of its steps stops being simulated.
	constexpr int c_SweepPixelsPerUpdate = 90000; //!< How much of the terrain is checked each update for liquid left hanging (see Sweep).
	constexpr int c_LevelSearchesPerUpdate = 32; //!< How many stuck pixels may look for a lower spot through the liquid each update. A search through a big body costs up to about 0.1 ms, so this bounds them to about 1 ms an update.
	constexpr int c_LevelSearchCells = 14000; //!< How many liquid pixels such a search may cross: enough for a pit, a tunnel and the pit beyond.

	std::array<Liquid, 256> s_Kinds{};
	std::array<float, 256> s_PowderSlide{}; //!< For powders: the chance per step of sliding down a slope.
	std::array<bool, 256> s_PowderSticky{}; //!< For powders: only slides off a drop two deep, so it stands steeper.
	std::array<int, 256> s_PourColor{}; //!< Palette index a poured pixel of each flowing material gets.
	// Per material, from its behaviour (MaterialBehaviour, SB-1) or the stock rule for its name: how it moves, and what it turns into.
	std::array<LiquidProperties, 256> s_Props{}; //!< How each liquid or powder moves.
	std::array<bool, 256> s_Douses{}; //!< Puts fire out, and quenches what settles in it (water).
	std::array<int, 256> s_SettlesTo{}; //!< What it sets into where it meets something that douses it (lava: stone), 0 for nothing.
	std::array<int, 256> s_BoilsTo{}; //!< What it boils into against something that settles (water: air, with steam), -1 for nothing.
	std::array<int, 256> s_MeltsTo{}; //!< What a solid melts into beside lava (ice and snow: water), 0 for nothing.
	std::array<int, 256> s_FreezesTo{}; //!< What a liquid freezes into, still under snowfall (water: ice), 0 for nothing.
	std::array<int, 256> s_DriesTo{}; //!< What a liquid dries into, still with air over it (mud: earth), 0 for nothing.
	std::array<float, 256> s_DryChance{}; //!< The chance a sweep pass of a still surface pixel of it drying.
	std::array<bool, 256> s_Chills{}; //!< Freezes what it touches that freezes (cryogenic fluid).
	std::array<float, 256> s_Evaporates{}; //!< The chance a step of a surface pixel of it boiling off into mist.
	int s_BloodMaterial = 0; //!< The Blood liquid, which settled blood becomes when it flows (FluidSim::BloodFlows), 0 if the scene has none.
	int s_WaterMaterial = 0; //!< The material blood drops are made of (water, drawn red).
	std::vector<glm::ivec2> s_BloodSettled; //!< Where blood drops settled since the last update, to become flowing blood.
	std::array<int, 256> s_ColorOfMaterial{}; //!< Palette index each material is drawn with, for pixels changed into it.
	std::array<bool, 256> s_Soft{}; //!< Soft enough for acid to eat (integrity under 100).
	bool s_TablesBuilt = false;

	/// The moving liquid pixels. A state byte per pixel says whether it's active and for how many steps it's been still; a list of keys (y * width + x) says which to visit.
	/// Pixels are visited in descending key order (bottom to top), so the result is deterministic. The state is kept in 64 by 64 tiles made when a pixel in them first
	/// moves and dropped when none in them is moving (M-5): a full plane cost three bytes a terrain pixel on every scene, 480 MB on the largest, liquid or not.
	struct ActiveSet {
		static constexpr int c_TileShift = 6;
		static constexpr int c_TileSize = 1 << c_TileShift;
		static constexpr int c_TileMask = c_TileSize - 1;
		struct Tile {
			std::array<unsigned char, c_TileSize * c_TileSize> Grid{}; //!< 0 = not active. Otherwise the low 6 bits are steps without getting lower + 1, and the top bit is the way the pixel is heading (set = right).
			std::array<signed char, c_TileSize * c_TileSize> VelX{}; //!< Sideways speed of each active pixel, in quarter pixels per step.
			std::array<signed char, c_TileSize * c_TileSize> VelY{}; //!< Falling speed.
			int Count = 0; //!< Active pixels in the tile.
		};
		std::vector<std::unique_ptr<Tile>> Tiles; //!< Row by row, null where nothing moves.
		int Width = 0;
		int Height = 0;
		int TilesX = 0;
		std::vector<int> Keys; //!< Keys of active pixels, plus stale ones (no longer active) that are dropped at the next update.
		size_t Count = 0;

		void Resize(int width, int height) {
			if (width != Width || height != Height) {
				Width = std::max(width, 0);
				Height = std::max(height, 0);
				TilesX = (Width + c_TileMask) >> c_TileShift;
				Tiles.clear();
				Tiles.resize(static_cast<size_t>(TilesX) * static_cast<size_t>((Height + c_TileMask) >> c_TileShift));
				Keys.clear();
				Count = 0;
			}
		}
		/// The tile a key falls in (null if none is made, or the key is out of the terrain), and its cell in it.
		Tile* TileOf(int key, int& cell) const {
			if (key < 0 || Width <= 0 || static_cast<size_t>(key) >= static_cast<size_t>(Width) * static_cast<size_t>(Height)) {
				return nullptr;
			}
			int x = key % Width;
			int y = key / Width;
			cell = ((y & c_TileMask) << c_TileShift) | (x & c_TileMask);
			return Tiles[static_cast<size_t>(y >> c_TileShift) * static_cast<size_t>(TilesX) + static_cast<size_t>(x >> c_TileShift)].get();
		}
		bool Contains(int key) const {
			int cell = 0;
			const Tile* tile = TileOf(key, cell);
			return tile && tile->Grid[cell] != 0;
		}
		void Add(int key, int still, bool headingRight, int velX = 0, int velY = 0) {
			if (key < 0 || Width <= 0 || static_cast<size_t>(key) >= static_cast<size_t>(Width) * static_cast<size_t>(Height)) {
				return;
			}
			int x = key % Width;
			int y = key / Width;
			int cell = ((y & c_TileMask) << c_TileShift) | (x & c_TileMask);
			std::unique_ptr<Tile>& tile = Tiles[static_cast<size_t>(y >> c_TileShift) * static_cast<size_t>(TilesX) + static_cast<size_t>(x >> c_TileShift)];
			if (!tile) {
				tile = std::make_unique<Tile>();
			}
			if (tile->Grid[cell] == 0) {
				tile->Grid[cell] = static_cast<unsigned char>((std::min(still, 62) + 1) | (headingRight ? 0x80 : 0));
				tile->VelX[cell] = static_cast<signed char>(std::clamp(velX, -120, 120));
				tile->VelY[cell] = static_cast<signed char>(std::clamp(velY, 0, 120));
				++tile->Count;
				Keys.push_back(key);
				++Count;
			}
		}
		/// Adds, or takes over an entry already there (one left by a pixel that is gone).
		void Put(int key, int still, bool headingRight, int velX = 0, int velY = 0) {
			int cell = 0;
			if (Tile* tile = TileOf(key, cell); tile && tile->Grid[cell] != 0) {
				tile->Grid[cell] = static_cast<unsigned char>((std::min(still, 62) + 1) | (headingRight ? 0x80 : 0));
				tile->VelX[cell] = static_cast<signed char>(std::clamp(velX, -120, 120));
				tile->VelY[cell] = static_cast<signed char>(std::clamp(velY, 0, 120));
			} else {
				Add(key, still, headingRight, velX, velY);
			}
		}
		void Remove(int key) {
			int cell = 0;
			if (Tile* tile = TileOf(key, cell); tile && tile->Grid[cell] != 0) {
				tile->Grid[cell] = 0;
				// (So a cell taken again never starts with the old pixel's speed.)
				tile->VelX[cell] = 0;
				tile->VelY[cell] = 0;
				--tile->Count;
				--Count;
			}
		}
		/// Counts another still step, returning how many that makes.
		int StillStep(int key) {
			int cell = 0;
			Tile* tile = TileOf(key, cell);
			if (!tile || tile->Grid[cell] == 0) {
				return 0;
			}
			if ((tile->Grid[cell] & 0x3F) < 63) {
				++tile->Grid[cell];
			}
			return (tile->Grid[cell] & 0x3F) - 1;
		}
		int Still(int key) const {
			int cell = 0;
			const Tile* tile = TileOf(key, cell);
			return tile && tile->Grid[cell] != 0 ? (tile->Grid[cell] & 0x3F) - 1 : 0;
		}
		bool HeadingRight(int key) const {
			int cell = 0;
			const Tile* tile = TileOf(key, cell);
			return tile && (tile->Grid[cell] & 0x80) != 0;
		}
		int VelXOf(int key) const {
			int cell = 0;
			const Tile* tile = TileOf(key, cell);
			return tile ? tile->VelX[cell] : 0;
		}
		int VelYOf(int key) const {
			int cell = 0;
			const Tile* tile = TileOf(key, cell);
			return tile ? tile->VelY[cell] : 0;
		}
		void SetVel(int key, int velX, int velY) {
			int cell = 0;
			if (Tile* tile = TileOf(key, cell); tile && tile->Grid[cell] != 0) {
				tile->VelX[cell] = static_cast<signed char>(std::clamp(velX, -120, 120));
				tile->VelY[cell] = static_cast<signed char>(std::clamp(velY, 0, 120));
			}
		}
		/// Drops stale and repeated keys and sorts the rest, highest first; and the tiles nothing moves in any more.
		void Tidy() {
			Keys.erase(std::remove_if(Keys.begin(), Keys.end(), [this](int key) { return !Contains(key); }), Keys.end());
			SortHighestFirst();
			Keys.erase(std::unique(Keys.begin(), Keys.end()), Keys.end());
			for (std::unique_ptr<Tile>& tile: Tiles) {
				if (tile && tile->Count == 0) {
					tile.reset();
				}
			}
		}
		/// Sorts the keys, highest first. A big flood has hundreds of thousands of them to sort every update, which a radix sort does several times faster than a comparison sort.
		void SortHighestFirst() {
			size_t count = Keys.size();
			if (count < 4096) {
				std::sort(Keys.begin(), Keys.end(), std::greater<int>());
				return;
			}
			static std::vector<int> scratch;
			scratch.resize(count);
			int* from = Keys.data();
			int* to = scratch.data();
			// Three passes of 11 bits, lowest first, cover any key (they're never negative). Each pass keeps the order of the one before.
			for (int pass = 0; pass < 3; ++pass) {
				int shift = pass * 11;
				size_t starts[2048] = {};
				for (size_t i = 0; i < count; ++i) {
					++starts[(from[i] >> shift) & 2047];
				}
				size_t total = 0;
				for (size_t& start: starts) {
					size_t inBucket = start;
					start = total;
					total += inBucket;
				}
				for (size_t i = 0; i < count; ++i) {
					to[starts[(from[i] >> shift) & 2047]++] = from[i];
				}
				std::swap(from, to);
			}
			// After an odd number of passes the result, lowest first, is in the scratch buffer. Copy it back the other way round.
			for (size_t i = 0; i < count; ++i) {
				Keys[i] = scratch[count - 1 - i];
			}
		}
		void Clear() {
			for (std::unique_ptr<Tile>& tile: Tiles) {
				tile.reset();
			}
			Keys.clear();
			Count = 0;
		}
		bool Empty() const { return Count == 0; }
	};
	ActiveSet s_Active;
	std::vector<int> s_Waiting; //!< Pixels that should start moving but couldn't, because too many already were. They get their turn as room frees up.
	int s_SweepPass = 0; //!< How many times the sweep has been through the whole terrain.
	size_t s_SweepCursor = 0; //!< Where the sweep for hanging liquid has got to in the terrain.
	struct PourRequest {
		int X, Y, Radius;
		std::string Name;
	};
	std::vector<PourRequest> s_Pours;
	std::vector<std::pair<glm::ivec2, int>> s_Disturbances;
	struct SplashRequest {
		int X, Y, Radius;
		float Share, Speed;
	};
	std::vector<SplashRequest> s_Splashes;
	std::mutex s_QueueMutex;
	const void* s_Scene = nullptr;
	unsigned int s_SceneGeneration = 0; //!< SceneMan's count of scene loads when this scene was taken up: a new scene at the old one's address, or the same one restarted, still counts as new (L-1).
	std::string s_PendingLoadState; //!< Saved moving liquid to restore when the loaded scene starts.
	int s_Width = 0; //!< Width of the terrain the active pixels' keys refer to.
	unsigned int s_Random = 0x6C8E9CF5u;

	float Random01() {
		s_Random ^= s_Random << 13;
		s_Random ^= s_Random >> 17;
		s_Random ^= s_Random << 5;
		return static_cast<float>(s_Random & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	Liquid LiquidFromName(const std::string& name, Liquid otherwise) {
		return name == "Water" ? Liquid::Water : (name == "Lava" ? Liquid::Lava : (name == "Acid" ? Liquid::Acid : (name == "Oil" ? Liquid::Oil : otherwise)));
	}

	/// The material of a name, for a behaviour that names one (SettlesTo and the like): its index, 0 for none, or -1 for "Air" itself.
	int MaterialNamed(const std::string& name) {
		if (name.empty()) {
			return 0;
		}
		if (name == "Air") {
			return -1;
		}
		for (int id = 1; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (material && material->GetIndex() == id && material->GetPresetName() == name) {
				return id;
			}
		}
		return 0;
	}

	/// The tables, per material, from each material's behaviour (MaterialBehaviour, SB-1), and where it sets nothing, the stock rule for its name:
	/// "Water", "Lava", "Acid" and "Oil" flow, "Sand", "Snow", "Earth Rubble" and "Ashes" are powders, lava settles to "Stone" in water, which boils
	/// off, ice and snow melt to water by lava, and water freezes to "Ice". So a mod's Materials.ini can add a liquid or change one with lines of
	/// its own, and a mod that sets nothing behaves as before.
	void BuildTables() {
		s_Kinds.fill(Liquid::None);
		s_PowderSlide.fill(0.0F);
		s_PowderSticky.fill(false);
		s_PourColor.fill(0);
		s_Props.fill(c_Liquids[0]);
		s_Douses.fill(false);
		s_SettlesTo.fill(0);
		s_BoilsTo.fill(0);
		s_MeltsTo.fill(0);
		s_FreezesTo.fill(0);
		s_DriesTo.fill(0);
		s_DryChance.fill(0.0F);
		s_Chills.fill(false);
		s_Evaporates.fill(0.0F);
		s_BloodMaterial = 0;
		s_WaterMaterial = 0;
		s_ColorOfMaterial.fill(0);
		s_Soft.fill(false);
		for (int id = 1; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (!material || material->GetIndex() != id) {
				continue;
			}
			const std::string& name = material->GetPresetName();
			const MaterialBehaviour& behaviour = material->GetBehaviour();
			Color color = material->GetColor();
			color.RecalculateIndex();
			s_ColorOfMaterial[id] = color.GetIndex();
			s_Soft[id] = material->GetIntegrity() < 100.0F;
			if (name == "Blood") {
				s_BloodMaterial = id;
			} else if (name == "Water") {
				s_WaterMaterial = id;
			}
			Liquid named = LiquidFromName(name, Liquid::None);
			bool flows = behaviour.Flows >= 0 ? behaviour.Flows == 1 : named != Liquid::None;
			// (Blood runs only with the setting on: otherwise it stays where it fell, as it always did.)
			if (id == s_BloodMaterial && !s_BloodFlows) {
				flows = false;
			}
			bool powderByName = name == "Sand" || name == "Snow" || name == "Earth Rubble" || name == "Ashes";
			bool powder = !flows && (behaviour.Powder >= 0 ? behaviour.Powder == 1 : powderByName);
			// What it turns into, set or stock.
			auto turnsInto = [](const std::string& set, const char* stock) { return MaterialNamed(set.empty() ? std::string(stock ? stock : "") : set); };
			s_MeltsTo[id] = std::max(0, turnsInto(behaviour.MeltsTo, name == "Ice" || name == "Snow" ? "Water" : nullptr));
			Liquid kind = Liquid::None;
			if (flows) {
				kind = named != Liquid::None ? named : Liquid::Other;
			} else if (powder && FluidSim::PowdersEnabled()) {
				kind = Liquid::Powder;
			}
			if (kind == Liquid::None) {
				continue;
			}
			s_Kinds[id] = kind;
			s_PourColor[id] = color.GetIndex();
			LiquidProperties properties = c_Liquids[static_cast<int>(kind)];
			auto setIf = [](int& value, int set, int low) {
				if (set >= 0) {
					value = std::max(set, low);
				}
			};
			setIf(properties.Flow, behaviour.FlowSpeed, 0);
			setIf(properties.Fall, behaviour.FallSpeed, 1);
			setIf(properties.MoveEvery, behaviour.MoveEvery, 1);
			setIf(properties.Gravity, behaviour.Gravity, 1);
			setIf(properties.FlowGain, behaviour.Viscosity, 0);
			setIf(properties.Weight, behaviour.LiquidWeight, 0);
			s_Props[id] = properties;
			if (kind == Liquid::Powder) {
				s_PowderSlide[id] = behaviour.SlideChance >= 0.0F ? std::clamp(behaviour.SlideChance, 0.0F, 1.0F) : (name == "Sand" ? 0.7F : (name == "Snow" ? 0.4F : 0.55F));
				s_PowderSticky[id] = behaviour.Sticky >= 0 ? behaviour.Sticky == 1 : name == "Snow";
				continue;
			}
			s_Douses[id] = behaviour.Douses >= 0 ? behaviour.Douses == 1 : kind == Liquid::Water;
			s_SettlesTo[id] = std::max(0, turnsInto(behaviour.SettlesTo, kind == Liquid::Lava ? "Stone" : nullptr));
			s_BoilsTo[id] = turnsInto(behaviour.BoilsTo, kind == Liquid::Water ? "Air" : nullptr);
			s_FreezesTo[id] = std::max(0, turnsInto(behaviour.FreezesTo, kind == Liquid::Water ? "Ice" : nullptr));
			s_DriesTo[id] = turnsInto(behaviour.DriesTo, nullptr);
			s_Chills[id] = behaviour.Chills == 1;
			s_Evaporates[id] = behaviour.Evaporates > 0.0F ? std::min(behaviour.Evaporates, 1.0F) : 0.0F;
			s_DryChance[id] = s_DriesTo[id] != 0 ? std::clamp(behaviour.DryChance >= 0.0F ? behaviour.DryChance : 0.1F, 0.0F, 1.0F) : 0.0F;
			// How it is drawn: water, lava and acid by their own looks, oil plain (its dark brown is shared with too many sprites to shimmer), a
			// liquid of a mod's own as water; and lava glows.
			int look = behaviour.Look >= 0 ? behaviour.Look : (kind == Liquid::Oil ? 0 : (kind == Liquid::Other ? 1 : static_cast<int>(kind)));
			int glow = behaviour.Glow >= 0 ? std::clamp(behaviour.Glow, 0, 255) : (kind == Liquid::Lava ? 230 : 0);
			if (look > 0) {
				g_RenderMan.SetLiquidPaletteColor(color.GetIndex(), std::clamp(look, 1, 15), glow);
			}
		}
		s_TablesBuilt = true;
	}

	/// The liquid at a pixel. The coordinates must already be inside the world (see InWorld): this reads the material bitmap directly, without the layer's own wrapping and bounds checks, because it's called tens of thousands of times an update.
	Liquid KindAt(const SLTerrain* terrain, int x, int y) { return s_Kinds[terrain->GetBitmap()->line[y][x]]; }

	bool s_WrapsX = false; //!< Whether the scene wraps sideways. Looked up once an update: InWorld is called hundreds of thousands of times in a big flood.

	bool InWorld(int& x, int& y, int width, int height) {
		if (s_WrapsX) {
			if (x < 0) {
				x += width;
			} else if (x >= width) {
				x -= width;
			}
		}
		return x >= 0 && y >= 0 && x < width && y < height;
	}

	void Activate(int x, int y, int width, int height, const SLTerrain* terrain) {
		if (InWorld(x, y, width, height) && !s_Active.Contains(y * width + x) && KindAt(terrain, x, y) != Liquid::None) {
			if (s_Active.Count < c_MaxActive) {
				// A pixel that starts moving heads one way or the other by where it is, so a body of liquid spreads both ways.
				s_Active.Add(y * width + x, 0, ((x + y) & 1) != 0);
			} else if (s_Waiting.size() < 2000000) {
				s_Waiting.push_back(y * width + x);
			}
		}
	}

	void ActivateAround(int x, int y, int width, int height, const SLTerrain* terrain) {
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				Activate(x + dx, y + dy, width, height, terrain);
			}
		}
	}

	/// Looks through the body of liquid a pixel belongs to for a free spot that's lower than the pixel, so liquid finds its own level: across a wide pool, under a wall or through a pipe.
	/// The search spreads out from the pixel through liquid of the same material, in a fixed order, inside a window around it.
	/// @return Whether a spot was found; if so it's in foundX and foundY.
	bool FindLowerSpot(const SLTerrain* terrain, int startX, int startY, int width, int height, int& foundX, int& foundY) {
		constexpr int halfWidth = 400;
		constexpr int halfHeight = 200;
		constexpr int windowWidth = halfWidth * 2 + 1;
		constexpr int windowHeight = halfHeight * 2 + 1;
		static std::vector<unsigned char> visited;
		static std::vector<glm::ivec2> frontier;
		visited.assign(static_cast<size_t>(windowWidth) * windowHeight, 0);
		frontier.clear();
		BITMAP* materialBitmap = terrain->GetBitmap();
		bool wraps = g_SceneMan.SceneWrapsX();
		int liquidMaterial = materialBitmap->line[startY][startX];
		frontier.emplace_back(0, 0);
		visited[static_cast<size_t>(halfHeight) * windowWidth + halfWidth] = 1;
		// Down first, then sideways, then up: the lowest spots are found first.
		static constexpr int offsets[4][2] = {{0, 1}, {-1, 0}, {1, 0}, {0, -1}};
		for (size_t next = 0; next < frontier.size() && next < static_cast<size_t>(c_LevelSearchCells); ++next) {
			glm::ivec2 cell = frontier[next];
			for (const auto& offset: offsets) {
				int relativeX = cell.x + offset[0];
				int relativeY = cell.y + offset[1];
				if (relativeX < -halfWidth || relativeX > halfWidth || relativeY < -halfHeight || relativeY > halfHeight) {
					continue;
				}
				size_t index = static_cast<size_t>(relativeY + halfHeight) * windowWidth + (relativeX + halfWidth);
				if (visited[index]) {
					continue;
				}
				visited[index] = 1;
				int x = startX + relativeX;
				int y = startY + relativeY;
				if (wraps) {
					x = ((x % width) + width) % width;
				}
				if (x < 0 || y < 0 || x >= width || y >= height) {
					continue;
				}
				int material = materialBitmap->line[y][x];
				if (material == liquidMaterial) {
					frontier.emplace_back(relativeX, relativeY);
				} else if (material == g_MaterialAir && relativeY >= 1) {
					// Lower than the pixel by at least a row, so moving there brings the two levels together rather than swapping them.
					foundX = x;
					foundY = y;
					return true;
				}
			}
		}
		return false;
	}

	/// Looks along a liquid pixel's own row, one way, for the nearest place it could drop into: air with air below it. The look passes through air and through liquid of its own material.
	/// @return How many pixels away the place is, or 0 if there's none within reach.
	int FindRowDrop(BITMAP* materialBitmap, int x, int y, int side, int reach, int width, int height) {
		if (y + 1 >= height) {
			return 0;
		}
		int own = materialBitmap->line[y][x];
		for (int step = 1; step <= reach; ++step) {
			int lookX = x + side * step;
			int lookY = y;
			if (!InWorld(lookX, lookY, width, height)) {
				return 0;
			}
			int material = materialBitmap->line[y][lookX];
			if (material == g_MaterialAir) {
				if (materialBitmap->line[y + 1][lookX] == g_MaterialAir) {
					return step;
				}
			} else if (material != own) {
				return 0;
			}
		}
		return 0;
	}

	/// Checks a stretch of the terrain for liquid that should be moving but isn't being simulated: left hanging over a gap when ground was removed without a wake-up, or loaded from a scene.
	/// A little each update, working through the whole terrain every few seconds.
	/// The box the liquids changed what can be walked through this update (to or from something solid: stone set from lava, ice, a hole eaten
	/// by acid, sand sliding), told to the pathfinder at the end of the update (M-4), as TerrainFire does.
	struct ChangedBox {
		int MinX = 0, MinY = 0, MaxX = -1, MaxY = -1;
		void Add(int x, int y) {
			if (MaxX < 0) {
				MinX = MaxX = x;
				MinY = MaxY = y;
				return;
			}
			MinX = std::min(MinX, x);
			MinY = std::min(MinY, y);
			MaxX = std::max(MaxX, x);
			MaxY = std::max(MaxY, y);
		}
		bool Empty() const { return MaxX < 0; }
		void Reset() { MaxX = MaxY = -1; }
	};
	ChangedBox s_SolidChanged;
	ChangedBox s_LiquidRested; //!< Where liquid came to rest since it was last told (the path grid weighs liquid: LM-4), told once a second.

	/// Tells the pathfinder what changed (M-4): what can be walked through at once, where liquid settled once a second.
	void TellPathfinder(SLTerrain* terrain, long long simUpdate) {
		auto tell = [terrain](ChangedBox& box) {
			terrain->AddUpdatedMaterialArea(Box(Vector(static_cast<float>(box.MinX), static_cast<float>(box.MinY)), static_cast<float>(box.MaxX - box.MinX + 1), static_cast<float>(box.MaxY - box.MinY + 1)));
			box.Reset();
		};
		if (!s_SolidChanged.Empty()) {
			tell(s_SolidChanged);
		}
		if (!s_LiquidRested.Empty() && simUpdate % 60 == 0) {
			tell(s_LiquidRested);
		}
	}

	/// Whether a material is something to stand on or bump into, not air or a liquid (powder is: a sand pile is ground).
	bool BlocksPassage(int material) { return material != g_MaterialAir && (material <= 0 || material >= 256 || s_Kinds[material] == Liquid::None || s_Kinds[material] == Liquid::Powder); }

	/// Sets a terrain pixel's material and colour, noting it for the pathfinder when that changes whether it can be passed.
	void ChangePixel(SLTerrain* terrain, int x, int y, int material, int color) {
		if (BlocksPassage(terrain->GetMaterialPixel(x, y)) != BlocksPassage(material)) {
			s_SolidChanged.Add(x, y);
		}
		terrain->SetMaterialPixel(x, y, material);
		terrain->SetFGColorPixel(x, y, color);
	}

	/// Whether a resting liquid pixel has something beside it to react with (M-2): acid by soft ground, lava (or what settles like it) by
	/// what burns, melts or quenches it, cryogenic fluid by what freezes, and what douses by fire. The step only reacts while a pixel is
	/// awake, so the sweep wakes one that has.
	bool HasReactionPartner(BITMAP* materialBitmap, int x, int y, int width, int height, bool anyFire) {
		int own = materialBitmap->line[y][x];
		Liquid kind = s_Kinds[own];
		bool acid = kind == Liquid::Acid;
		bool lava = kind == Liquid::Lava || s_SettlesTo[own] != 0;
		bool chills = s_Chills[own];
		bool douses = s_Douses[own] && anyFire;
		if (!acid && !lava && !chills && !douses) {
			return false;
		}
		static constexpr int neighbours[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
		for (const auto& offset: neighbours) {
			int nx = x + offset[0];
			int ny = y + offset[1];
			if (!InWorld(nx, ny, width, height)) {
				continue;
			}
			int neighbour = materialBitmap->line[ny][nx];
			if (neighbour == g_MaterialAir) {
				continue;
			}
			Liquid neighbourKind = s_Kinds[neighbour];
			if (acid && (neighbourKind == Liquid::None || neighbourKind == Liquid::Powder) && s_Soft[neighbour]) {
				return true;
			}
			if (lava && (TerrainFire::IsFlammable(neighbour) || s_MeltsTo[neighbour] != 0 || s_Douses[neighbour])) {
				return true;
			}
			if (chills && s_FreezesTo[neighbour] != 0) {
				return true;
			}
		}
		return douses && TerrainFire::IsBurningNear(Vector(static_cast<float>(x), static_cast<float>(y)), 1);
	}

	void Sweep(SLTerrain* terrain, int width, int height) {
		// In snowy weather still water slowly freezes over from the top.
		float freezing = FluidSim::FreezingEnabled() ? WeatherEffects::GetSnow() : 0.0F;
		size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
		if (total == 0) {
			return;
		}
		BITMAP* materialBitmap = terrain->GetBitmap();
		bool anyFire = TerrainFire::GetCount() > 0;
		size_t index = s_SweepCursor < total ? s_SweepCursor : 0;
		int x = static_cast<int>(index % static_cast<size_t>(width));
		int y = static_cast<int>(index / static_cast<size_t>(width));
		for (int i = 0; i < c_SweepPixelsPerUpdate; ++i) {
			// Nearly every pixel isn't liquid, so that's checked first and costs next to nothing.
			if (Liquid sweptKind = s_Kinds[materialBitmap->line[y][x]]; sweptKind != Liquid::None && sweptKind != Liquid::Powder && y + 1 < height && !s_Active.Contains(static_cast<int>(index))) {
				// Air right below, or below and to a side, means it has somewhere to go.
				const unsigned char* below = materialBitmap->line[y + 1];
				int left = x > 0 ? x - 1 : (s_WrapsX ? width - 1 : x);
				int right = x + 1 < width ? x + 1 : (s_WrapsX ? 0 : x);
				if (below[x] == g_MaterialAir || below[left] == g_MaterialAir || below[right] == g_MaterialAir) {
					Activate(x, y, width, height, terrain);
				} else if ((materialBitmap->line[y][left] == g_MaterialAir || materialBitmap->line[y][right] == g_MaterialAir) && (FindRowDrop(materialBitmap, x, y, -1, 300, width, height) || FindRowDrop(materialBitmap, x, y, 1, 300, width, height))) {
					// The end of a layer on the surface, with somewhere lower along its row to go to. (One with nowhere to go is left asleep, or the top of every pool would stir for ever.)
					Activate(x, y, width, height, terrain);
				} else if (HasReactionPartner(materialBitmap, x, y, width, height, anyFire)) {
					// Something beside it to react with (acid by soft ground, lava by wood or snow, a pool by a fire): woken, it reacts in the step, so an acid
					// puddle on dirt eats at the sweep's pace (slowly) instead of stopping once it settles.
					Activate(x, y, width, height, terrain);
				} else if (y > 0 && materialBitmap->line[y - 1][x] == g_MaterialAir && ((x * 7 + y * 13 + s_SweepPass) & 15) == 0) {
					// Now and then a pixel of a resting surface is woken to look through the body it's part of for a lower place (see FindLowerSpot): this is what starts
					// two pools joined below coming to one level. If it finds one, the pixels around it wake and follow; if not, it goes back to sleep. A different one in 16 each pass.
					Activate(x, y, width, height, terrain);
				} else if (int driesTo = s_DriesTo[materialBitmap->line[y][x]]; driesTo != 0 && y > 0) {
					// A liquid that dries (mud to earth, blood away to nothing): from the top down, where it lies still with air, or what it dried
					// into, over it.
					int above = materialBitmap->line[y - 1][x];
					if ((above == g_MaterialAir || above == driesTo) && Random01() < s_DryChance[materialBitmap->line[y][x]]) {
						ChangePixel(terrain, x, y, driesTo > 0 ? driesTo : static_cast<int>(g_MaterialAir), driesTo > 0 ? s_ColorOfMaterial[driesTo] : static_cast<int>(ColorKeys::g_MaskColor));
						if (driesTo < 0) {
							ActivateAround(x, y, width, height, terrain);
						}
					}
				} else if (int freezesTo = s_FreezesTo[materialBitmap->line[y][x]]; freezing > 0.05F && freezesTo != 0 && y > 0) {
					int above = materialBitmap->line[y - 1][x];
					if ((above == g_MaterialAir || (above == freezesTo && Random01() < 0.25F)) && Random01() < freezing * 0.04F) {
						ChangePixel(terrain, x, y, freezesTo, s_ColorOfMaterial[freezesTo]);
					}
				}
			}
			++index;
			if (++x == width) {
				x = 0;
				if (++y == height) {
					y = 0;
					index = 0;
					++s_SweepPass;
				}
			}
		}
		s_SweepCursor = index;
	}

	MovableObject* CreateEffect(const char* className, const char* presetName) {
		const Entity* preset = g_PresetMan.GetEntityPreset(className, presetName, "Base.rte");
		return preset ? dynamic_cast<MovableObject*>(preset->Clone()) : nullptr;
	}
} // namespace

bool FluidSim::IsLiquid(int materialID) {
	return s_TablesBuilt && materialID > 0 && materialID < 256 && s_Kinds[materialID] != Liquid::None && s_Kinds[materialID] != Liquid::Powder;
}

bool FluidSim::HoldsBodies(int materialID) {
	if (!IsLiquid(materialID)) {
		return false;
	}
	Liquid kind = s_Kinds[materialID];
	return kind == Liquid::Water || kind == Liquid::Acid || kind == Liquid::Other;
}

void FluidSim::SetPowdersEnabled(bool enabled) {
	if (s_Powders != enabled) {
		s_Powders = enabled;
		s_TablesBuilt = false;
	}
}

void FluidSim::SetBloodFlows(bool enabled) {
	if (s_BloodFlows != enabled) {
		s_BloodFlows = enabled;
		s_TablesBuilt = false;
	}
}

void FluidSim::Pour(const Vector& position, float radius, const char* liquidName) {
	std::scoped_lock lock(s_QueueMutex);
	s_Pours.push_back({static_cast<int>(position.m_X), static_cast<int>(position.m_Y), std::max(1, static_cast<int>(radius)), liquidName ? liquidName : "Water"});
}

void FluidSim::OnParticleSettled(const MovableObject* particle) {
	if (!particle || !particle->GetMaterial()) {
		return;
	}
	int material = particle->GetMaterial()->GetIndex();
	Vector position = particle->GetPos();
	if (TerrainFire::IsFlammable(material) && TerrainFire::IsFireSource(particle)) {
		TerrainFire::QueueIgnite(position.GetFloorIntX(), position.GetFloorIntY());
	}
	if (!s_Enabled || !s_TablesBuilt || material <= 0 || material >= 256 || s_Kinds[material] == Liquid::None) {
		return;
	}
	if (s_Kinds[material] == Liquid::Powder) {
		// A grain of sand that lands may roll further down the pile.
		std::scoped_lock lock(s_QueueMutex);
		s_Disturbances.emplace_back(glm::ivec2(position.GetFloorIntX(), position.GetFloorIntY()), 1);
		return;
	}
	const MOPixel* pixel = dynamic_cast<const MOPixel*>(particle);
	// Blood (water drawn red) that settles runs as blood, when that's on: turned into the Blood liquid at the next update.
	if (pixel && s_BloodFlows && s_BloodMaterial != 0 && material == s_WaterMaterial && pixel->GetColor().GetIndex() != s_PourColor[material]) {
		const Color& color = pixel->GetColor();
		if (color.GetR() > color.GetG() * 2 && color.GetR() > color.GetB() * 2) {
			std::scoped_lock lock(s_QueueMutex);
			if (s_BloodSettled.size() < 4096) {
				s_BloodSettled.emplace_back(position.GetFloorIntX(), position.GetFloorIntY());
			}
			return;
		}
	}
	if (pixel && pixel->GetColor().GetIndex() == s_PourColor[material]) {
		std::scoped_lock lock(s_QueueMutex);
		s_Disturbances.emplace_back(glm::ivec2(position.GetFloorIntX(), position.GetFloorIntY()), 1);
	}
}

void FluidSim::Splash(const Vector& position, float radius, float share, float speed) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	if (s_Splashes.size() < 64) {
		s_Splashes.push_back({static_cast<int>(position.m_X), static_cast<int>(position.m_Y), std::clamp(static_cast<int>(radius), 2, 80), std::clamp(share, 0.0F, 1.0F), std::clamp(speed, 1.0F, 30.0F)});
	}
}

void FluidSim::Disturb(const Vector& position, float radius) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	s_Disturbances.emplace_back(glm::ivec2(static_cast<int>(position.m_X), static_cast<int>(position.m_Y)), static_cast<int>(radius));
}

namespace {
	float s_LastUpdateMS = 0.0F;
}

float FluidSim::GetLastUpdateMS() {
	return s_LastUpdateMS;
}

void FluidSim::Update() {
	struct UpdateTimer {
		std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
		~UpdateTimer() { s_LastUpdateMS = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - Start).count(); }
	} updateTimer;
	if (g_SceneMan.GetScene() != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		Clear();
		s_Scene = g_SceneMan.GetScene();
		s_SceneGeneration = g_SceneMan.GetSceneGeneration();
		s_Random = 0x6C8E9CF5u;
		s_SweepPass = 0;
		s_TablesBuilt = false;
		if (!s_PendingLoadState.empty() && s_Scene) {
			// Restore a saved game's moving liquid: the random state, then "V2" and "x y stillSteps heading velX velY" per pixel, then "W", a count
			// and "x y" for each pixel waiting for room. (Saves from before L-2 have "x y stillSteps" per pixel and nothing after.)
			std::istringstream stream(s_PendingLoadState);
			unsigned int random = 0;
			stream >> random;
			if (random != 0) {
				s_Random = random;
			}
			Scene* loadedScene = g_SceneMan.GetScene();
			int loadedWidth = loadedScene && loadedScene->GetTerrain() ? loadedScene->GetTerrain()->GetBitmap()->w : 0;
			if (loadedWidth > 0) {
				s_Active.Resize(loadedWidth, loadedScene->GetTerrain()->GetBitmap()->h);
			}
			int x = 0;
			int y = 0;
			int still = 0;
			std::streampos afterRandom = stream.tellg();
			std::string token;
			if (stream >> token && token == "V2") {
				int heading = 0;
				int velX = 0;
				int velY = 0;
				while (loadedWidth > 0 && stream >> token && token != "W") {
					x = std::atoi(token.c_str());
					if (!(stream >> y >> still >> heading >> velX >> velY)) {
						break;
					}
					if (s_Active.Count < c_MaxActive) {
						s_Active.Add(y * loadedWidth + x, still, heading != 0, velX, velY);
					}
				}
				size_t waiting = 0;
				if (token == "W" && stream >> waiting) {
					for (size_t i = 0; i < waiting && stream >> x >> y; ++i) {
						if (s_Waiting.size() < 2000000) {
							s_Waiting.push_back(y * loadedWidth + x);
						}
					}
				}
			} else {
				stream.clear();
				stream.seekg(afterRandom);
				while (loadedWidth > 0 && stream >> x >> y >> still && s_Active.Count < c_MaxActive) {
					s_Active.Add(y * loadedWidth + x, still, ((x + y) & 1) != 0);
				}
			}
			g_ConsoleMan.PrintString("SYSTEM: Restored " + std::to_string(s_Active.Count) + " moving liquid pixels from the saved game.");
		}
		s_PendingLoadState.clear();
	}
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	if (!terrain || !s_Enabled) {
		std::scoped_lock lock(s_QueueMutex);
		s_Pours.clear();
		s_Disturbances.clear();
		s_BloodSettled.clear();
		s_Active.Clear();
		return;
	}
	if (!s_TablesBuilt) {
		BuildTables();
	}
	int width = terrain->GetBitmap()->w;
	int height = terrain->GetBitmap()->h;
	s_Width = width;
	s_WrapsX = g_SceneMan.SceneWrapsX();
	s_Active.Resize(width, height);

	std::vector<PourRequest> pours;
	std::vector<std::pair<glm::ivec2, int>> disturbances;
	std::vector<SplashRequest> splashRequests;
	std::vector<glm::ivec2> bloodSettled;
	{
		std::scoped_lock lock(s_QueueMutex);
		pours.swap(s_Pours);
		disturbances.swap(s_Disturbances);
		splashRequests.swap(s_Splashes);
		bloodSettled.swap(s_BloodSettled);
	}
	// Blood that settled runs as blood (FluidSim::BloodFlows): the water pixel it became, still red, turns to the Blood liquid and wakes.
	std::sort(bloodSettled.begin(), bloodSettled.end(), [](const glm::ivec2& a, const glm::ivec2& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
	for (glm::ivec2 spot: bloodSettled) {
		int x = s_WrapsX ? ((spot.x % width) + width) % width : spot.x;
		int y = spot.y;
		if (s_BloodMaterial != 0 && InWorld(x, y, width, height) && terrain->GetMaterialPixel(x, y) == s_WaterMaterial && terrain->GetFGColorPixel(x, y) != s_PourColor[s_WaterMaterial]) {
			terrain->SetMaterialPixel(x, y, s_BloodMaterial);
			Activate(x, y, width, height, terrain);
		}
	}
	// Liquid thrown into the air by blasts and by things falling in. Each pixel thrown becomes a flying drop that joins the liquid again where it lands, so none is lost.
	std::sort(splashRequests.begin(), splashRequests.end(), [](const SplashRequest& a, const SplashRequest& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Radius < b.Radius); });
	int dropsLeft = 260;
	for (const SplashRequest& splash: splashRequests) {
		BITMAP* splashBitmap = terrain->GetBitmap();
		int splashX = s_WrapsX ? ((splash.X % width) + width) % width : splash.X;
		for (int dy = -splash.Radius; dy <= splash.Radius && dropsLeft > 0; ++dy) {
			for (int dx = -splash.Radius; dx <= splash.Radius && dropsLeft > 0; ++dx) {
				int x = splashX + dx;
				int y = splash.Y + dy;
				if (dx * dx + dy * dy > splash.Radius * splash.Radius || !InWorld(x, y, width, height)) {
					continue;
				}
				int material = splashBitmap->line[y][x];
				Liquid kind = s_Kinds[material];
				if (kind == Liquid::None || kind == Liquid::Powder) {
					continue;
				}
				// Only liquid near the open surface can go anywhere.
				bool open = false;
				for (int up = 1; up <= 4 && y - up >= 0; ++up) {
					int above = splashBitmap->line[y - up][x];
					if (above == g_MaterialAir) {
						open = true;
						break;
					}
					if (s_Kinds[above] == Liquid::None) {
						break;
					}
				}
				if (!open || Random01() >= splash.Share) {
					continue;
				}
				--dropsLeft;
				Color color;
				color.SetRGBWithIndex(terrain->GetFGColorPixel(x, y));
				ChangePixel(terrain, x, y, g_MaterialAir, ColorKeys::g_MaskColor);
				s_Active.Remove(y * width + x);
				const Material* sceneMaterial = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(material));
				float outward = static_cast<float>(dx) / static_cast<float>(splash.Radius);
				Vector velocity((outward * 0.7F + (Random01() - 0.5F) * 0.6F) * splash.Speed, -(0.35F + Random01() * 0.65F) * splash.Speed);
				MOPixel* drop = new MOPixel(color, sceneMaterial->GetPixelDensity(), Vector(static_cast<float>(x), static_cast<float>(y - 1)), velocity, new Atom(Vector(), sceneMaterial->GetIndex(), nullptr, color, 2), 0);
				drop->SetToHitMOs(false);
				g_MovableMan.AddParticle(drop);
				ActivateAround(x, y, width, height, terrain);
			}
		}
	}
	std::sort(pours.begin(), pours.end(), [](const PourRequest& a, const PourRequest& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Radius < b.Radius); });
	for (const PourRequest& pour: pours) {
		int material = 0;
		for (int id = 1; id < 256 && !material; ++id) {
			if (s_Kinds[id] != Liquid::None && g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id))->GetPresetName() == pour.Name) {
				material = id;
			}
		}
		if (material == 0) {
			continue;
		}
		// Requests come from scripts and can be anywhere; bring the centre into the world here, so InWorld only ever has to wrap by one scene width.
		int pourX = s_WrapsX ? ((pour.X % width) + width) % width : pour.X;
		for (int dy = -pour.Radius; dy <= pour.Radius; ++dy) {
			for (int dx = -pour.Radius; dx <= pour.Radius; ++dx) {
				int x = pourX + dx;
				int y = pour.Y + dy;
				if (dx * dx + dy * dy <= pour.Radius * pour.Radius && InWorld(x, y, width, height) && terrain->GetMaterialPixel(x, y) == g_MaterialAir) {
					ChangePixel(terrain, x, y, material, s_PourColor[material]);
					Activate(x, y, width, height, terrain);
				}
			}
		}
	}
	std::sort(disturbances.begin(), disturbances.end(), [](const auto& a, const auto& b) { return a.first.y != b.first.y ? a.first.y < b.first.y : (a.first.x != b.first.x ? a.first.x < b.first.x : a.second < b.second); });
	for (const auto& [center, radius]: disturbances) {
		int centerX = s_WrapsX ? ((center.x % width) + width) % width : center.x;
		int reach = std::min(radius, width / 2);
		for (int dy = -reach; dy <= reach; ++dy) {
			for (int dx = -reach; dx <= reach; ++dx) {
				Activate(centerX + dx, center.y + dy, width, height, terrain);
			}
		}
	}
	// Pixels that were waiting for room get their turn, oldest first.
	if (!s_Waiting.empty() && s_Active.Count < c_MaxActive) {
		size_t taken = 0;
		while (taken < s_Waiting.size() && s_Active.Count < c_MaxActive) {
			int key = s_Waiting[taken++];
			Activate(key % width, key / width, width, height, terrain);
		}
		s_Waiting.erase(s_Waiting.begin(), s_Waiting.begin() + static_cast<std::ptrdiff_t>(taken));
	}
	Sweep(terrain, width, height);
	long long simUpdate = g_TimerMan.GetSimUpdateCount();
	if (s_Active.Empty()) {
		s_Active.Keys.clear();
		// (What the sweep froze or dried still counts.)
		TellPathfinder(terrain, simUpdate);
		return;
	}

	// Bottom to top, so a column of liquid falls together instead of one pixel per step.
	// A copy, because pixels woken during the step are added to the set and take their turn next step.
	s_Active.Tidy();
	std::vector<int> keys = s_Active.Keys;
	// Within each row, right to left on one update and left to right on the next. In a fixed order liquid runs faster one way than the other:
	// a pixel following another gets to move in the same step only if it's visited after the one in front.
	if (simUpdate & 1) {
		size_t rowStart = 0;
		for (size_t i = 1; i <= keys.size(); ++i) {
			if (i == keys.size() || keys[i] / width != keys[rowStart] / width) {
				std::reverse(keys.begin() + static_cast<std::ptrdiff_t>(rowStart), keys.begin() + static_cast<std::ptrdiff_t>(i));
				rowStart = i;
			}
		}
	}
	std::vector<int> settled;
	std::vector<glm::ivec2> hurtSpots;
	int levelSearches = 0;
	int splashes = 0;
	// (Water is gone through from the bottom of the map up, so a small allowance would all be spent low down and never reach a fall higher up: which pixels
	// throw mist is left to chance, and the allowance is only a ceiling for floods.)
	// Mist: soft puffs thrown off water that's falling fast or landing hard, so a pour looks like water and not like pixels. Visual only: they never touch the
	// simulation, and which pixels throw them is decided from position and time, not the simulation's random numbers, so turning them off changes nothing else.
	float mistSetting = std::clamp(g_PostProcessMan.GetLightingSettings().WaterMist, 0.0F, 2.0F);
	int mistLeft = static_cast<int>(60.0F * mistSetting);
	// Out of a thousand falling pixels, and out of a thousand landing ones, how many throw a puff this update.
	int mistFalling = static_cast<int>(10.0F * mistSetting);
	int mistLanding = static_cast<int>(40.0F * mistSetting);
	// Read straight from the material bitmap's rows in the loop below; coordinates are brought into the world first.
	BITMAP* materialBitmap = terrain->GetBitmap();
	// Water only needs to put fire out when something is burning.
	bool anyFire = TerrainFire::GetCount() > 0;
	// Where pixels moved to this update (M-1): a key there that is also further down the list was left by a pixel that went (erased, or
	// moved off), and the newcomer has had its step.
	std::unordered_set<int> movedInto;
	movedInto.reserve(keys.size());
	for (int key: keys) {
		if (!s_Active.Contains(key) || movedInto.count(key) != 0) {
			continue;
		}
		int x = key % width;
		int y = key / width;
		const int ownMaterial = materialBitmap->line[y][x];
		Liquid kind = s_Kinds[ownMaterial];
		if (kind == Liquid::None) {
			// (Out of the set at once, not at the end: a pixel may flow into this spot later in the update and must be able to register.)
			s_Active.Remove(key);
			continue;
		}
		const LiquidProperties& properties = s_Props[ownMaterial];
		if (simUpdate % properties.MoveEvery != 0) {
			continue;
		}

		// Reactions with neighbours. Only lava (and what settles like it), acid and what douses fire, when something is burning, react; for everything
		// else there's nothing to check.
		static constexpr int neighbours[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
		bool reacted = false;
		const bool douses = s_Douses[ownMaterial];
		const int settlesTo = s_SettlesTo[ownMaterial];
		const bool chills = s_Chills[ownMaterial];
		bool mayReact = kind == Liquid::Lava || settlesTo != 0 || kind == Liquid::Acid || chills || (douses && anyFire);
		for (const auto& offset: neighbours) {
			if (!mayReact) {
				break;
			}
			int nx = x + offset[0];
			int ny = y + offset[1];
			if (!InWorld(nx, ny, width, height)) {
				continue;
			}
			int neighbourMaterial = materialBitmap->line[ny][nx];
			Liquid neighbourKind = s_Kinds[static_cast<unsigned char>(neighbourMaterial)];
			if (chills && s_FreezesTo[neighbourMaterial] != 0 && Random01() < 0.3F) {
				// Cryogenic fluid freezes the water (or mud) it touches, and is used up doing it, half the time.
				int freezesTo = s_FreezesTo[neighbourMaterial];
				ChangePixel(terrain, nx, ny, freezesTo, s_ColorOfMaterial[freezesTo]);
				s_Active.Remove(ny * width + nx);
				if (Random01() < 0.5F) {
					ChangePixel(terrain, x, y, g_MaterialAir, ColorKeys::g_MaskColor);
					ActivateAround(x, y, width, height, terrain);
					reacted = true;
					break;
				}
			}
			if (settlesTo != 0 && s_Douses[neighbourMaterial]) {
				// Lava meeting water: the lava sets to stone and the water boils off in a puff of steam (or to what it boils to).
				ChangePixel(terrain, x, y, settlesTo, s_ColorOfMaterial[settlesTo]);
				int boilsTo = s_BoilsTo[neighbourMaterial];
				if (boilsTo != 0) {
					ChangePixel(terrain, nx, ny, boilsTo > 0 ? boilsTo : static_cast<int>(g_MaterialAir), boilsTo > 0 ? s_ColorOfMaterial[boilsTo] : static_cast<int>(ColorKeys::g_MaskColor));
					s_Active.Remove(ny * width + nx);
				}
				ActivateAround(nx, ny, width, height, terrain);
				EffectsParticles::SpawnExplosion(Vector(static_cast<float>(nx), static_cast<float>(ny)), 520.0F);
				if (Random01() < 0.5F) {
					TerrainFire::SpawnSteam(Vector(static_cast<float>(nx), static_cast<float>(ny)), 1);
				}
				reacted = true;
				break;
			}
			if (kind == Liquid::Lava && neighbourMaterial != g_MaterialAir && s_MeltsTo[neighbourMaterial] != 0 && Random01() < 0.3F) {
				// Lava melts ice and snow to water (which then quenches it to stone).
				int meltsTo = s_MeltsTo[neighbourMaterial];
				ChangePixel(terrain, nx, ny, meltsTo, s_ColorOfMaterial[meltsTo]);
				Activate(nx, ny, width, height, terrain);
			}
			if (kind == Liquid::Lava && TerrainFire::IsFlammable(neighbourMaterial) && Random01() < 0.2F) {
				TerrainFire::QueueIgnite(nx, ny);
			}
			if (douses && anyFire) {
				TerrainFire::Extinguish(nx, ny);
			}
			if (kind == Liquid::Acid && (neighbourKind == Liquid::None || neighbourKind == Liquid::Powder) && neighbourMaterial != g_MaterialAir) {
				// Acid slowly eats soft terrain, and is used up doing it.
				const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(neighbourMaterial));
				if (material->GetIntegrity() < 100.0F && Random01() < 0.02F) {
					ChangePixel(terrain, nx, ny, g_MaterialAir, ColorKeys::g_MaskColor);
					s_Active.Remove(ny * width + nx);
					ActivateAround(nx, ny, width, height, terrain);
					if (Random01() < 0.3F) {
						ChangePixel(terrain, x, y, g_MaterialAir, ColorKeys::g_MaskColor);
						ActivateAround(x, y, width, height, terrain);
						reacted = true;
						break;
					}
				}
			}
		}
		if (douses && anyFire) {
			TerrainFire::Extinguish(x, y);
		}
		if (reacted) {
			s_Active.Remove(key);
			continue;
		}
		if (kind == Liquid::Lava && y > 0 && materialBitmap->line[y - 1][x] == g_MaterialAir && Random01() < 0.01F) {
			hurtSpots.emplace_back(x, y - 1);
		}
		// Boiling off (cryogenic fluid): a pixel at the surface goes up as mist now and then, and the one under it is next.
		if (s_Evaporates[ownMaterial] > 0.0F && y > 0 && materialBitmap->line[y - 1][x] == g_MaterialAir && Random01() < s_Evaporates[ownMaterial]) {
			ChangePixel(terrain, x, y, g_MaterialAir, ColorKeys::g_MaskColor);
			ActivateAround(x, y, width, height, terrain);
			if (mistLeft > 0) {
				--mistLeft;
				EffectsParticles::Emit("Mist", Vector(static_cast<float>(x), static_cast<float>(y - 1)), Vector(0.0F, -1.5F), 0.6F, 1, 0);
			}
			s_Active.Remove(key);
			continue;
		}

		auto canMoveTo = [&](int tx, int ty) {
			return InWorld(tx, ty, width, height) && materialBitmap->line[ty][tx] == g_MaterialAir;
		};
		// Whether this pixel sinks through what's at a spot: a lighter liquid, which rises into its place.
		auto sinksInto = [&](int tx, int ty) {
			if (!InWorld(tx, ty, width, height)) {
				return false;
			}
			int otherMaterial = materialBitmap->line[ty][tx];
			Liquid other = s_Kinds[otherMaterial];
			return other != Liquid::None && other != Liquid::Powder && s_Props[otherMaterial].Weight < properties.Weight;
		};
		int heading = s_Active.HeadingRight(key) ? 1 : -1;
		int still = s_Active.Still(key);
		int velX = s_Active.VelXOf(key);
		int velY = s_Active.VelYOf(key);
		int targetX = x;
		int targetY = y;
		bool moved = false;
		bool gotLower = false;
		bool swapped = false;
		bool freeFall = false;
		if (canMoveTo(x, y + 1)) {
			freeFall = true;
			// Falling: faster the longer it falls, drifting the way it was already going, so it pours in an arc.
			velY = std::min(velY + properties.Gravity, properties.Fall * 4);
			int fewest = kind == Liquid::Powder ? 1 : 2;
			int steps = std::clamp(velY / 4, fewest, std::max(fewest, properties.Fall)); // (The high end never under the low: std::clamp aborts on that with libstdc++'s checks.)
			int drift = velX >= 4 ? 1 : (velX <= -4 ? -1 : 0);
			if (drift == 0 && velY >= 12 && kind != Liquid::Powder) {
				// A stream that has been falling a while frays at its edges: now and then a pixel of it steps sideways as it falls.
				float stray = Random01();
				drift = stray < 0.07F ? -1 : (stray > 0.93F ? 1 : 0);
			}
			// No two pixels of a pour fall quite alike: now and then one takes a step fewer, and each sideways step is left to chance. When they all moved identically
			// a pour off a ledge stood in the air as a fixed pattern of dots, repeating for as long as it ran.
			if (kind != Liquid::Powder && steps > 2 && Random01() < 0.3F) {
				--steps;
			}
			for (int fall = 0; fall < steps; ++fall) {
				if (drift != 0 && Random01() < 0.5F && canMoveTo(targetX + drift, targetY + 1)) {
					targetX += drift;
				} else if (!canMoveTo(targetX, targetY + 1)) {
					break;
				}
				++targetY;
			}
			moved = true;
			gotLower = true;
			if (mistLeft > 0 && kind == Liquid::Water && velY >= 8 && (x * 37 + y * 113 + static_cast<int>(simUpdate) * 59) % 1000 < mistFalling) {
				--mistLeft;
				EffectsParticles::Emit("Mist", Vector(static_cast<float>(targetX), static_cast<float>(targetY)), Vector(static_cast<float>(velX) * 0.25F, static_cast<float>(velY) * 0.25F), 0.5F, 1, 0);
			}
		} else {
			if (mistLeft > 0 && kind == Liquid::Water && velY >= 10 && (x * 11 + y * 53 + static_cast<int>(simUpdate) * 131) % 1000 < mistLanding) {
				// Where it lands, a burst of spray.
				--mistLeft;
				EffectsParticles::Emit("Mist", Vector(static_cast<float>(x), static_cast<float>(y - 1)), Vector(0.0F, -2.2F), 1.0F, 1, 0);
			}
			if (velY >= c_SplashSpeed && kind != Liquid::Powder) {
				// Landed hard: now and then a drop is thrown up, flies and rejoins the pool where it comes down. The rest of the speed goes sideways.
				if (splashes < c_MaxSplashesPerUpdate && canMoveTo(x, y - 1) && Random01() < 0.22F) {
					++splashes;
					int material = materialBitmap->line[y][x];
					Color color;
					color.SetRGBWithIndex(terrain->GetFGColorPixel(x, y));
					ChangePixel(terrain, x, y, g_MaterialAir, ColorKeys::g_MaskColor);
					const Material* sceneMaterial = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(material));
					// Particle speeds are in metres a second: 20 pixels to the metre, 60 updates a second.
					Vector velocity((Random01() - 0.5F) * 9.0F, -(1.5F + Random01() * static_cast<float>(velY) * 0.3F));
					MOPixel* drop = new MOPixel(color, sceneMaterial->GetPixelDensity(), Vector(static_cast<float>(x), static_cast<float>(y - 1)), velocity, new Atom(Vector(), sceneMaterial->GetIndex(), nullptr, color, 2), 0);
					drop->SetToHitMOs(false);
					g_MovableMan.AddParticle(drop);
					s_Active.Remove(key);
					ActivateAround(x, y, width, height, terrain);
					continue;
				}
				velX += heading * velY / 2;
			}
			velY = 0;
			if (sinksInto(x, y + 1)) {
				// Heavier than the liquid below: they change places.
				targetY = y + 1;
				moved = true;
				gotLower = true;
				swapped = true;
			} else if (kind == Liquid::Powder) {
				// Powder only ever slides down a slope, and not every step, so it piles instead of levelling.
				int material = materialBitmap->line[y][x];
				if (Random01() < s_PowderSlide[material]) {
					for (int side: {heading, -heading}) {
						if (canMoveTo(x + side, y) && canMoveTo(x + side, y + 1) && (!s_PowderSticky[material] || canMoveTo(x + side, y + 2))) {
							targetX = x + side;
							targetY = y + 1;
							heading = side;
							moved = true;
							gotLower = true;
							break;
						}
					}
				}
			} else {
				// Down and to a side, the way it's heading first.
				for (int side: {heading, -heading}) {
					if (canMoveTo(x + side, y + 1)) {
						targetX = x + side;
						targetY = y + 1;
						heading = side;
						velX = side * std::max(std::abs(velX), 4);
						moved = true;
						gotLower = true;
						break;
					}
				}
				if (!moved && std::abs(velX) >= 4) {
					// Still carrying speed from a fall or a slide: it coasts along the level, slowing, and at a wall turns round with half its speed, so a wave sloshes back.
					if (!canMoveTo(x + heading, y)) {
						heading = -heading;
						velX = -velX / 2;
					}
					if (velX * heading < 0) {
						velX = -velX;
					}
					int run = std::min(std::abs(velX) / 4, properties.Flow);
					for (int step = 1; step <= run; ++step) {
						int sideX = x + heading * step;
						if (!canMoveTo(sideX, y)) {
							velX = -velX / 2;
							break;
						}
						targetX = sideX;
						moved = true;
						if (canMoveTo(sideX, y + 1)) {
							targetY = y + 1;
							gotLower = true;
							break;
						}
					}
					velX = velX * 3 / 4;
				}
				bool atSurface = canMoveTo(x, y - 1);
				bool atFront = canMoveTo(x - 1, y) || canMoveTo(x + 1, y);
				if (!gotLower && !atSurface && !atFront && (still & 1) == 0 && y > 0 && materialBitmap->line[y - 1][x] == ownMaterial) {
					// Inside the liquid with more of it pressing down from above: if there's an opening along its row within a short way (a hole in a tank's wall,
					// the mouth of a pipe), it goes out through it. This is what makes liquid under a head of liquid pour out of a hole instead of seeping.
					for (int side: {heading, -heading}) {
						int found = 0;
						for (int step = 1; step <= 48; ++step) {
							int lookX = x + side * step;
							int lookY = y;
							if (!InWorld(lookX, lookY, width, height)) {
								break;
							}
							int material = materialBitmap->line[lookY][lookX];
							if (material == g_MaterialAir) {
								found = step;
								break;
							}
							if (material != ownMaterial) {
								break;
							}
						}
						if (found) {
							targetX = x + side * found;
							targetY = canMoveTo(targetX, y + 1) ? y + 1 : y;
							heading = side;
							velX = side * 8;
							moved = true;
							gotLower = true;
							break;
						}
					}
				}
				// At the surface, or at the front of liquid running under a ceiling or along a pipe.
				if (!gotLower && (atSurface || atFront)) {
					// At the surface and not getting lower: look along its own row, as far as a wide room, for somewhere lower to be, and go there.
					// The look passes through liquid as well as air, since liquid in the way would be pushed along: this is what makes a body of liquid press outwards
					// and come level in a second or two. A pixel with nowhere lower to go stays where it is; left to wander, the top of a pool never comes to rest.
					for (int side: {heading, -heading}) {
						if (int found = FindRowDrop(materialBitmap, x, y, side, properties.Flow * 60, width, height)) {
							targetX = x + side * found;
							targetY = y + 1;
							heading = side;
							moved = true;
							gotLower = true;
							break;
						}
					}
				}
			}
		}
		// Having slipped off an edge it keeps going down the face of the liquid it's part of, to the bottom if it can, in this same step.
		// One row a step is far too slow: a heap would take the better part of a minute to drain, and looks as if it has set.
		if (gotLower && !swapped && !freeFall && kind != Liquid::Powder) {
			int drops = 0;
			for (int more = 0; more < properties.Flow * 4; ++more) {
				if (canMoveTo(targetX, targetY + 1)) {
					// Open air below: from here it falls at its own pace.
					if (++drops > 2) {
						break;
					}
					++targetY;
					continue;
				}
				drops = 0;
				bool found = false;
				for (int step = 1; step <= 4; ++step) {
					int sideX = targetX + heading * step;
					if (!canMoveTo(sideX, targetY)) {
						break;
					}
					if (canMoveTo(sideX, targetY + 1)) {
						targetX = sideX;
						++targetY;
						found = true;
						break;
					}
				}
				if (!found) {
					break;
				}
			}
		}
		// Stuck, or only wandering along the top: look through the body of liquid for a lower free spot, so separate parts of it come to one level.
		// Only so many may look each update. The rest wait for their turn without counting towards coming to rest, so none miss out.
		bool waitingToSearch = false;
		if (!gotLower && still >= 3 && (still & 3) == 3 && kind != Liquid::Powder && canMoveTo(x, y - 1)) {
			if (levelSearches < c_LevelSearchesPerUpdate) {
				++levelSearches;
				int foundX = 0;
				int foundY = 0;
				if (FindLowerSpot(terrain, x, y, width, height, foundX, foundY)) {
					targetX = foundX;
					targetY = foundY;
					moved = true;
					gotLower = true;
				}
			} else {
				waitingToSearch = true;
			}
		}
		if (moved) {
			// Applied straight away: going bottom to top, the pixel above sees this one already gone and can follow it down in the same step.
			InWorld(targetX, targetY, width, height);
			int target = targetY * width + targetX;
			int material = terrain->GetMaterialPixel(x, y);
			int color = terrain->GetFGColorPixel(x, y);
			// When it sinks through a lighter liquid, that liquid takes the place it left.
			int leftMaterial = swapped ? terrain->GetMaterialPixel(targetX, targetY) : static_cast<int>(g_MaterialAir);
			int leftColor = swapped ? terrain->GetFGColorPixel(targetX, targetY) : static_cast<int>(ColorKeys::g_MaskColor);
			if (swapped) {
				s_Active.Remove(target);
			}
			ChangePixel(terrain, targetX, targetY, material, color);
			ChangePixel(terrain, x, y, leftMaterial, leftColor);
			// Burning fuel (oil) takes its fire with it, so a lit slick that flows keeps burning and a burning stream runs downhill (M-3). Only
			// while something burns: a map lookup or two a move.
			if (anyFire && (TerrainFire::IsFlammable(material) || (swapped && TerrainFire::IsFlammable(leftMaterial)))) {
				TerrainFire::MoveBurning(x, y, targetX, targetY);
			}
			s_Active.Remove(key);
			// Running along the level without getting any lower counts towards coming to rest, so ripples die down.
			int newStill = gotLower ? 0 : (waitingToSearch ? still : still + 1);
			// (What boils off stays awake at the surface until it has: see below.)
			if (newStill < c_RestSteps || s_Evaporates[material] > 0.0F) {
				// (Put, not Add: an entry still there from a pixel erased outside this step, by a bullet or a script, is taken over rather than
				// keeping the newcomer out with the old pixel's count, heading and speed.)
				s_Active.Put(target, newStill, heading > 0, velX, velY);
				movedInto.insert(target);
			}
			// Whatever was resting around it may now flow into the gap.
			ActivateAround(x, y, width, height, terrain);
		} else {
			s_Active.SetVel(key, velX, 0);
			// Powder that can't slide goes to rest sooner: it has nowhere to level out to.
			// (Never at the surface for what boils off: it would sit there for good instead of going in seconds.)
			bool boilingOff = s_Evaporates[ownMaterial] > 0.0F && canMoveTo(x, y - 1);
			if (!waitingToSearch && s_Active.StillStep(key) >= (kind == Liquid::Powder ? 8 : c_RestSteps) && !boilingOff) {
				settled.push_back(key);
			}
		}
	}

	for (int key: settled) {
		if (movedInto.count(key) == 0) {
			s_Active.Remove(key);
			s_LiquidRested.Add(key % width, key / width);
		}
	}
	for (const glm::ivec2& spot: hurtSpots) {
		if (MovableObject* flame = CreateEffect("MOPixel", "Flame Hurt Particle")) {
			flame->SetPos(Vector(static_cast<float>(spot.x), static_cast<float>(spot.y)));
			flame->SetVel(Vector(0.0F, -2.0F));
			g_MovableMan.AddParticle(flame);
		}
	}
	TellPathfinder(terrain, simUpdate);
}

std::string FluidSim::GetSaveState() {
	std::ostringstream stream;
	stream << s_Random;
	// Keys were made with the width cached at the last update; the terrain isn't touched here, since saving can happen at any time.
	int width = s_Width;
	if (width > 0) {
		// Lowest key first, as saves have always listed them.
		std::vector<int> keys;
		keys.reserve(s_Active.Count);
		for (int key: s_Active.Keys) {
			if (s_Active.Contains(key)) {
				keys.push_back(key);
			}
		}
		std::sort(keys.begin(), keys.end());
		keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
		// Everything a pixel carries (L-2): a stream saved mid-fall comes back falling, heading the way it was.
		stream << " V2";
		for (int key: keys) {
			stream << ' ' << key % width << ' ' << key / width << ' ' << s_Active.Still(key) << ' ' << (s_Active.HeadingRight(key) ? 1 : 0) << ' ' << s_Active.VelXOf(key) << ' ' << s_Active.VelYOf(key);
		}
		// And the pixels waiting for room in the set, oldest first.
		stream << " W " << s_Waiting.size();
		for (int key: s_Waiting) {
			stream << ' ' << key % width << ' ' << key / width;
		}
	}
	return stream.str();
}

void FluidSim::SetPendingLoadState(const std::string& state) {
	s_PendingLoadState = state;
}

void FluidSim::Clear() {
	s_Active.Clear();
	s_SolidChanged.Reset();
	s_LiquidRested.Reset();
	s_Waiting.clear();
	s_SweepCursor = 0;
	std::scoped_lock lock(s_QueueMutex);
	s_Pours.clear();
	s_Disturbances.clear();
	s_Splashes.clear();
	s_BloodSettled.clear();
}

void FluidSim::GetActivePixels(const Vector& corner, float width, float height, std::vector<Vector>& pixels, size_t limit) {
	if (s_Width <= 0) {
		return;
	}
	for (int key: s_Active.Keys) {
		if (pixels.size() >= limit) {
			break;
		}
		// (Keys can be stale until the next update drops them.)
		if (!s_Active.Contains(key)) {
			continue;
		}
		Vector pixel(static_cast<float>(key % s_Width), static_cast<float>(key / s_Width));
		Vector fromCorner = g_SceneMan.ShortestDistance(corner, pixel, g_SceneMan.SceneWrapsX());
		if (fromCorner.m_X >= 0.0F && fromCorner.m_Y >= 0.0F && fromCorner.m_X < width && fromCorner.m_Y < height) {
			pixels.push_back(pixel);
		}
	}
}

void FluidSim::VisitMovingPixels(const std::function<void(int x, int y, int velX, int velY, int still)>& visit) {
	if (s_Width <= 0) {
		return;
	}
	for (int key: s_Active.Keys) {
		// (Keys can be stale, or repeated, until the next update drops them. A repeat only visits the same pixel twice.)
		if (s_Active.Contains(key)) {
			visit(key % s_Width, key / s_Width, s_Active.VelXOf(key), s_Active.VelYOf(key), s_Active.Still(key));
		}
	}
}

int FluidSim::GetActiveCount() {
	return static_cast<int>(s_Active.Count);
}
