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
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace RTE;

bool FluidSim::s_Enabled = true;
bool FluidSim::s_Powders = true;
bool FluidSim::s_Freezing = false;

namespace {
	enum class Liquid : unsigned char {
		None,
		Water,
		Lava,
		Acid,
		Oil,
		Powder //!< Sand, snow and the like: falls and slides down slopes but doesn't flow level. Not a liquid to the rest of the game.
	};
	constexpr int c_LiquidKinds = 6;

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
	};
	constexpr int c_SplashSpeed = 14; //!< Liquid landing at least this fast may throw a drop.
	constexpr int c_MaxSplashesPerUpdate = 30;

	constexpr size_t c_MaxActive = 80000; //!< More than this many moving pixels wait their turn (see s_Waiting) rather than being forgotten. Measured with this many moving at once: 6 to 8 ms an update, with spikes to 11 ms.
	constexpr int c_RestSteps = 20; //!< A pixel that hasn't got any lower for this many of its steps stops being simulated.
	constexpr int c_SweepPixelsPerUpdate = 90000; //!< How much of the terrain is checked each update for liquid left hanging (see Sweep).
	constexpr int c_LevelSearchesPerUpdate = 32; //!< How many stuck pixels may look for a lower spot through the liquid each update. A search through a big body costs up to about 0.1 ms, so this bounds them to about 1 ms an update.
	constexpr int c_LevelSearchCells = 14000; //!< How many liquid pixels such a search may cross: enough for a pit, a tunnel and the pit beyond.

	std::array<Liquid, 256> s_Kinds{};
	std::array<int, c_LiquidKinds> s_MaterialOf{}; //!< Material ID of each liquid kind, 0 if the scene's materials don't have it.
	std::array<int, c_LiquidKinds> s_ColorOf{}; //!< Palette index each liquid is drawn with.
	std::array<float, 256> s_PowderSlide{}; //!< For powders: the chance per step of sliding down a slope.
	std::array<bool, 256> s_PowderSticky{}; //!< For powders: only slides off a drop two deep, so it stands steeper.
	std::array<int, 256> s_PourColor{}; //!< Palette index a poured pixel of each flowing material gets.
	int s_StoneMaterial = 0;
	int s_StoneColor = 0;
	int s_IceMaterial = 0;
	int s_IceColor = 0;
	int s_SnowMaterial = 0;
	bool s_TablesBuilt = false;

	/// The moving liquid pixels. A grid byte per terrain pixel says whether it's active and for how many steps it's been still; a list of keys (y * width + x) says which to visit.
	/// Pixels are visited in descending key order (bottom to top), so the result is deterministic. A flat grid and a list are many times faster than an ordered map for floods of tens of thousands of pixels.
	struct ActiveSet {
		std::vector<unsigned char> Grid; //!< 0 = not active. Otherwise the low 6 bits are steps without getting lower + 1, and the top bit is the way the pixel is heading (set = right).
		std::vector<int> Keys; //!< Keys of active pixels, plus stale ones (no longer active) that are dropped at the next update.
		std::vector<signed char> VelX; //!< Sideways speed of each active pixel, in quarter pixels per step.
		std::vector<signed char> VelY; //!< Falling speed.
		size_t Count = 0;

		void Resize(size_t pixels) {
			if (Grid.size() != pixels) {
				Grid.assign(pixels, 0);
				VelX.assign(pixels, 0);
				VelY.assign(pixels, 0);
				Keys.clear();
				Count = 0;
			}
		}
		bool Contains(int key) const { return key >= 0 && static_cast<size_t>(key) < Grid.size() && Grid[key] != 0; }
		void Add(int key, int still, bool headingRight, int velX = 0, int velY = 0) {
			if (key >= 0 && static_cast<size_t>(key) < Grid.size() && Grid[key] == 0) {
				Grid[key] = static_cast<unsigned char>((std::min(still, 62) + 1) | (headingRight ? 0x80 : 0));
				VelX[key] = static_cast<signed char>(std::clamp(velX, -120, 120));
				VelY[key] = static_cast<signed char>(std::clamp(velY, 0, 120));
				Keys.push_back(key);
				++Count;
			}
		}
		void Remove(int key) {
			if (Contains(key)) {
				Grid[key] = 0;
				--Count;
			}
		}
		/// Counts another still step, returning how many that makes.
		int StillStep(int key) {
			if (!Contains(key)) {
				return 0;
			}
			if ((Grid[key] & 0x3F) < 63) {
				++Grid[key];
			}
			return (Grid[key] & 0x3F) - 1;
		}
		int Still(int key) const { return Contains(key) ? (Grid[key] & 0x3F) - 1 : 0; }
		bool HeadingRight(int key) const { return Contains(key) && (Grid[key] & 0x80) != 0; }
		/// Drops stale and repeated keys and sorts the rest, highest first.
		void Tidy() {
			Keys.erase(std::remove_if(Keys.begin(), Keys.end(), [this](int key) { return Grid[key] == 0; }), Keys.end());
			SortHighestFirst();
			Keys.erase(std::unique(Keys.begin(), Keys.end()), Keys.end());
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
			for (int key: Keys) {
				Grid[key] = 0;
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

	void BuildTables() {
		s_Kinds.fill(Liquid::None);
		s_MaterialOf.fill(0);
		s_PowderSlide.fill(0.0F);
		s_PowderSticky.fill(false);
		s_PourColor.fill(0);
		s_StoneMaterial = 0;
		s_IceMaterial = 0;
		s_SnowMaterial = 0;
		for (int id = 1; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (!material || material->GetIndex() != id) {
				continue;
			}
			const std::string& name = material->GetPresetName();
			Liquid kind = LiquidFromName(name, Liquid::None);
			if (name == "Ice") {
				s_IceMaterial = id;
				Color color = material->GetColor();
				color.RecalculateIndex();
				s_IceColor = color.GetIndex();
			} else if (name == "Snow") {
				s_SnowMaterial = id;
			}
			if (kind != Liquid::None) {
				s_Kinds[id] = kind;
				s_MaterialOf[static_cast<int>(kind)] = id;
				Color color = material->GetColor();
				color.RecalculateIndex();
				s_ColorOf[static_cast<int>(kind)] = color.GetIndex();
				s_PourColor[id] = color.GetIndex();
				// Oil is drawn plain: its dark brown is shared with too many sprites to shimmer.
				if (kind != Liquid::Oil) {
					g_RenderMan.SetLiquidPaletteColor(color.GetIndex(), static_cast<int>(kind), kind == Liquid::Lava ? 230 : 0);
				}
			} else if (FluidSim::PowdersEnabled() && (name == "Sand" || name == "Snow" || name == "Earth Rubble" || name == "Ashes")) {
				s_Kinds[id] = Liquid::Powder;
				s_PowderSlide[id] = name == "Sand" ? 0.7F : (name == "Snow" ? 0.4F : 0.55F);
				s_PowderSticky[id] = name == "Snow";
				Color color = material->GetColor();
				color.RecalculateIndex();
				s_PourColor[id] = color.GetIndex();
			} else if (name == "Stone") {
				s_StoneMaterial = id;
				Color color = material->GetColor();
				color.RecalculateIndex();
				s_StoneColor = color.GetIndex();
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

	/// Looks along a liquid pixel's own row, one way, for the nearest place it could drop into: air with air below it. The look passes through air and through liquid of its own kind.
	/// @return How many pixels away the place is, or 0 if there's none within reach.
	int FindRowDrop(BITMAP* materialBitmap, int x, int y, int side, int reach, int width, int height) {
		if (y + 1 >= height) {
			return 0;
		}
		Liquid kind = s_Kinds[materialBitmap->line[y][x]];
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
			} else if (s_Kinds[material] != kind) {
				return 0;
			}
		}
		return 0;
	}

	/// Checks a stretch of the terrain for liquid that should be moving but isn't being simulated: left hanging over a gap when ground was removed without a wake-up, or loaded from a scene.
	/// A little each update, working through the whole terrain every few seconds.
	void Sweep(SLTerrain* terrain, int width, int height) {
		// In snowy weather still water slowly freezes over from the top.
		float freezing = s_IceMaterial && FluidSim::FreezingEnabled() ? WeatherEffects::GetSnow() : 0.0F;
		size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
		if (total == 0) {
			return;
		}
		BITMAP* materialBitmap = terrain->GetBitmap();
		size_t index = s_SweepCursor < total ? s_SweepCursor : 0;
		int x = static_cast<int>(index % static_cast<size_t>(width));
		int y = static_cast<int>(index / static_cast<size_t>(width));
		for (int i = 0; i < c_SweepPixelsPerUpdate; ++i) {
			// Nearly every pixel isn't liquid, so that's checked first and costs next to nothing.
			if (Liquid sweptKind = s_Kinds[materialBitmap->line[y][x]]; sweptKind != Liquid::None && sweptKind != Liquid::Powder && y + 1 < height && s_Active.Grid[index] == 0) {
				// Air right below, or below and to a side, means it has somewhere to go.
				const unsigned char* below = materialBitmap->line[y + 1];
				int left = x > 0 ? x - 1 : (s_WrapsX ? width - 1 : x);
				int right = x + 1 < width ? x + 1 : (s_WrapsX ? 0 : x);
				if (below[x] == g_MaterialAir || below[left] == g_MaterialAir || below[right] == g_MaterialAir) {
					Activate(x, y, width, height, terrain);
				} else if ((materialBitmap->line[y][left] == g_MaterialAir || materialBitmap->line[y][right] == g_MaterialAir) && (FindRowDrop(materialBitmap, x, y, -1, 300, width, height) || FindRowDrop(materialBitmap, x, y, 1, 300, width, height))) {
					// The end of a layer on the surface, with somewhere lower along its row to go to. (One with nowhere to go is left asleep, or the top of every pool would stir for ever.)
					Activate(x, y, width, height, terrain);
				} else if (y > 0 && materialBitmap->line[y - 1][x] == g_MaterialAir && ((x * 7 + y * 13 + s_SweepPass) & 15) == 0) {
					// Now and then a pixel of a resting surface is woken to look through the body it's part of for a lower place (see FindLowerSpot): this is what starts
					// two pools joined below coming to one level. If it finds one, the pixels around it wake and follow; if not, it goes back to sleep. A different one in 16 each pass.
					Activate(x, y, width, height, terrain);
				} else if (freezing > 0.05F && sweptKind == Liquid::Water && y > 0) {
					int above = materialBitmap->line[y - 1][x];
					if ((above == g_MaterialAir || (above == s_IceMaterial && Random01() < 0.25F)) && Random01() < freezing * 0.04F) {
						terrain->SetMaterialPixel(x, y, s_IceMaterial);
						terrain->SetFGColorPixel(x, y, s_IceColor);
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

void FluidSim::SetPowdersEnabled(bool enabled) {
	if (s_Powders != enabled) {
		s_Powders = enabled;
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
	if (pixel && pixel->GetColor().GetIndex() == s_ColorOf[static_cast<int>(s_Kinds[material])]) {
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
	if (g_SceneMan.GetScene() != s_Scene) {
		Clear();
		s_Scene = g_SceneMan.GetScene();
		s_Random = 0x6C8E9CF5u;
		s_TablesBuilt = false;
		if (!s_PendingLoadState.empty() && s_Scene) {
			// Restore a saved game's moving liquid: the random state, then "x y stillSteps" per pixel.
			std::istringstream stream(s_PendingLoadState);
			unsigned int random = 0;
			stream >> random;
			if (random != 0) {
				s_Random = random;
			}
			Scene* loadedScene = g_SceneMan.GetScene();
			int loadedWidth = loadedScene && loadedScene->GetTerrain() ? loadedScene->GetTerrain()->GetBitmap()->w : 0;
			if (loadedWidth > 0) {
				s_Active.Resize(static_cast<size_t>(loadedWidth) * static_cast<size_t>(loadedScene->GetTerrain()->GetBitmap()->h));
			}
			int x = 0;
			int y = 0;
			int still = 0;
			while (loadedWidth > 0 && stream >> x >> y >> still && s_Active.Count < c_MaxActive) {
				s_Active.Add(y * loadedWidth + x, still, ((x + y) & 1) != 0);
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
	s_Active.Resize(static_cast<size_t>(width) * static_cast<size_t>(height));

	std::vector<PourRequest> pours;
	std::vector<std::pair<glm::ivec2, int>> disturbances;
	std::vector<SplashRequest> splashRequests;
	{
		std::scoped_lock lock(s_QueueMutex);
		pours.swap(s_Pours);
		disturbances.swap(s_Disturbances);
		splashRequests.swap(s_Splashes);
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
				terrain->SetMaterialPixel(x, y, g_MaterialAir);
				terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
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
					terrain->SetMaterialPixel(x, y, material);
					terrain->SetFGColorPixel(x, y, s_PourColor[material]);
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
	if (s_Active.Empty()) {
		s_Active.Keys.clear();
		return;
	}

	long long simUpdate = g_TimerMan.GetSimUpdateCount();
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
	float foamSetting = g_PostProcessMan.GetLightingSettings().WaterFoam;
	int mistLeft = foamSetting > 0.0F ? static_cast<int>(400.0F * std::min(foamSetting, 1.5F)) : 0;
	// Read straight from the material bitmap's rows in the loop below; coordinates are brought into the world first.
	BITMAP* materialBitmap = terrain->GetBitmap();
	// Water only needs to put fire out when something is burning.
	bool anyFire = TerrainFire::GetCount() > 0;
	for (int key: keys) {
		int x = key % width;
		int y = key / width;
		Liquid kind = KindAt(terrain, x, y);
		if (kind == Liquid::None) {
			settled.push_back(key);
			continue;
		}
		const LiquidProperties& properties = c_Liquids[static_cast<int>(kind)];
		if (simUpdate % properties.MoveEvery != 0) {
			continue;
		}

		// Reactions with neighbours. Only lava and acid react, and water only has fire to put out when something is burning; for everything else there's nothing to check.
		static constexpr int neighbours[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
		bool reacted = false;
		bool mayReact = kind == Liquid::Lava || kind == Liquid::Acid || (kind == Liquid::Water && anyFire);
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
			if (kind == Liquid::Lava && neighbourKind == Liquid::Water && s_StoneMaterial) {
				// Lava meeting water: the lava sets to stone and the water boils off in a puff of steam.
				terrain->SetMaterialPixel(x, y, s_StoneMaterial);
				terrain->SetFGColorPixel(x, y, s_StoneColor);
				terrain->SetMaterialPixel(nx, ny, g_MaterialAir);
				terrain->SetFGColorPixel(nx, ny, ColorKeys::g_MaskColor);
				ActivateAround(nx, ny, width, height, terrain);
				EffectsParticles::SpawnExplosion(Vector(static_cast<float>(nx), static_cast<float>(ny)), 520.0F);
				if (Random01() < 0.5F) {
					TerrainFire::SpawnSteam(Vector(static_cast<float>(nx), static_cast<float>(ny)), 1);
				}
				reacted = true;
				break;
			}
			if (kind == Liquid::Lava && neighbourMaterial != g_MaterialAir && (neighbourMaterial == s_IceMaterial || neighbourMaterial == s_SnowMaterial) && s_MaterialOf[static_cast<int>(Liquid::Water)] && Random01() < 0.3F) {
				// Lava melts ice and snow to water (which then quenches it to stone).
				terrain->SetMaterialPixel(nx, ny, s_MaterialOf[static_cast<int>(Liquid::Water)]);
				terrain->SetFGColorPixel(nx, ny, s_ColorOf[static_cast<int>(Liquid::Water)]);
				Activate(nx, ny, width, height, terrain);
			}
			if (kind == Liquid::Lava && TerrainFire::IsFlammable(neighbourMaterial) && Random01() < 0.2F) {
				TerrainFire::QueueIgnite(nx, ny);
			}
			if (kind == Liquid::Water && anyFire) {
				TerrainFire::Extinguish(nx, ny);
			}
			if (kind == Liquid::Acid && (neighbourKind == Liquid::None || neighbourKind == Liquid::Powder) && neighbourMaterial != g_MaterialAir) {
				// Acid slowly eats soft terrain, and is used up doing it.
				const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(neighbourMaterial));
				if (material->GetIntegrity() < 100.0F && Random01() < 0.02F) {
					terrain->SetMaterialPixel(nx, ny, g_MaterialAir);
					terrain->SetFGColorPixel(nx, ny, ColorKeys::g_MaskColor);
					ActivateAround(nx, ny, width, height, terrain);
					if (Random01() < 0.3F) {
						terrain->SetMaterialPixel(x, y, g_MaterialAir);
						terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
						ActivateAround(x, y, width, height, terrain);
						reacted = true;
						break;
					}
				}
			}
		}
		if (kind == Liquid::Water && anyFire) {
			TerrainFire::Extinguish(x, y);
		}
		if (reacted) {
			settled.push_back(key);
			continue;
		}
		if (kind == Liquid::Lava && y > 0 && materialBitmap->line[y - 1][x] == g_MaterialAir && Random01() < 0.01F) {
			hurtSpots.emplace_back(x, y - 1);
		}

		auto canMoveTo = [&](int tx, int ty) {
			return InWorld(tx, ty, width, height) && materialBitmap->line[ty][tx] == g_MaterialAir;
		};
		// Whether this pixel sinks through what's at a spot: a lighter liquid, which rises into its place.
		auto sinksInto = [&](int tx, int ty) {
			if (!InWorld(tx, ty, width, height)) {
				return false;
			}
			Liquid other = s_Kinds[materialBitmap->line[ty][tx]];
			return other != Liquid::None && other != Liquid::Powder && c_Liquids[static_cast<int>(other)].Weight < properties.Weight;
		};
		int heading = s_Active.HeadingRight(key) ? 1 : -1;
		int still = s_Active.Still(key);
		int velX = s_Active.VelX[key];
		int velY = s_Active.VelY[key];
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
			int steps = std::clamp(velY / 4, kind == Liquid::Powder ? 1 : 2, properties.Fall);
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
			if (mistLeft > 0 && kind == Liquid::Water && velY >= 8 && (x * 3 + y * 13 + static_cast<int>(simUpdate) * 5) % 3 == 0) {
				--mistLeft;
				EffectsParticles::Emit("Mist", Vector(static_cast<float>(targetX), static_cast<float>(targetY)), Vector(static_cast<float>(velX) * 0.25F, static_cast<float>(velY) * 0.25F), 0.5F, 1, 0);
			}
		} else {
			if (mistLeft > 0 && kind == Liquid::Water && velY >= 10 && (x * 11 + y * 3 + static_cast<int>(simUpdate)) % 6 == 0) {
				// Where it lands, a burst of spray.
				--mistLeft;
				EffectsParticles::Emit("Mist", Vector(static_cast<float>(x), static_cast<float>(y - 1)), Vector(0.0F, -2.2F), 1.0F, 3, 0);
			}
			if (velY >= c_SplashSpeed && kind != Liquid::Powder) {
				// Landed hard: now and then a drop is thrown up, flies and rejoins the pool where it comes down. The rest of the speed goes sideways.
				if (splashes < c_MaxSplashesPerUpdate && canMoveTo(x, y - 1) && Random01() < 0.22F) {
					++splashes;
					int material = materialBitmap->line[y][x];
					Color color;
					color.SetRGBWithIndex(terrain->GetFGColorPixel(x, y));
					terrain->SetMaterialPixel(x, y, g_MaterialAir);
					terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
					const Material* sceneMaterial = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(material));
					// Particle speeds are in metres a second: 20 pixels to the metre, 60 updates a second.
					Vector velocity((Random01() - 0.5F) * 9.0F, -(1.5F + Random01() * static_cast<float>(velY) * 0.3F));
					MOPixel* drop = new MOPixel(color, sceneMaterial->GetPixelDensity(), Vector(static_cast<float>(x), static_cast<float>(y - 1)), velocity, new Atom(Vector(), sceneMaterial->GetIndex(), nullptr, color, 2), 0);
					drop->SetToHitMOs(false);
					g_MovableMan.AddParticle(drop);
					ActivateAround(x, y, width, height, terrain);
					settled.push_back(key);
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
				if (!gotLower && !atSurface && !atFront && (still & 1) == 0 && y > 0 && s_Kinds[materialBitmap->line[y - 1][x]] == kind) {
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
							if (s_Kinds[material] != kind) {
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
			terrain->SetMaterialPixel(targetX, targetY, material);
			terrain->SetFGColorPixel(targetX, targetY, color);
			terrain->SetMaterialPixel(x, y, leftMaterial);
			terrain->SetFGColorPixel(x, y, leftColor);
			s_Active.Remove(key);
			// Running along the level without getting any lower counts towards coming to rest, so ripples die down.
			int newStill = gotLower ? 0 : (waitingToSearch ? still : still + 1);
			if (newStill < c_RestSteps) {
				s_Active.Add(target, newStill, heading > 0, velX, velY);
			}
			// Whatever was resting around it may now flow into the gap.
			ActivateAround(x, y, width, height, terrain);
		} else {
			s_Active.VelX[key] = static_cast<signed char>(std::clamp(velX, -120, 120));
			s_Active.VelY[key] = 0;
			// Powder that can't slide goes to rest sooner: it has nowhere to level out to.
			if (!waitingToSearch && s_Active.StillStep(key) >= (kind == Liquid::Powder ? 8 : c_RestSteps)) {
				settled.push_back(key);
			}
		}
	}

	for (int key: settled) {
		s_Active.Remove(key);
	}
	for (const glm::ivec2& spot: hurtSpots) {
		if (MovableObject* flame = CreateEffect("MOPixel", "Flame Hurt Particle")) {
			flame->SetPos(Vector(static_cast<float>(spot.x), static_cast<float>(spot.y)));
			flame->SetVel(Vector(0.0F, -2.0F));
			g_MovableMan.AddParticle(flame);
		}
	}
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
		for (int key: keys) {
			stream << ' ' << key % width << ' ' << key / width << ' ' << s_Active.Still(key);
		}
	}
	return stream.str();
}

void FluidSim::SetPendingLoadState(const std::string& state) {
	s_PendingLoadState = state;
}

void FluidSim::Clear() {
	s_Active.Clear();
	s_Waiting.clear();
	s_SweepCursor = 0;
	std::scoped_lock lock(s_QueueMutex);
	s_Pours.clear();
	s_Disturbances.clear();
	s_Splashes.clear();
}

int FluidSim::GetActiveCount() {
	return static_cast<int>(s_Active.Count);
}
