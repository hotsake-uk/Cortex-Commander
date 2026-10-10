#include "TerrainCollapse.h"
#include "TerrainTrees.h"
#include "Actor.h"
#include "Atom.h"
#include "Constants.h"
#include "Material.h"
#include "MovableMan.h"
#include "MovableObject.h"
#include "MOPixel.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "TimerMan.h"
#include "Vector.h"
#include "EffectsParticles.h"
#include "FluidSim.h"

#include "glm/glm.hpp"
#include "PostProcessMan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

using namespace RTE;

bool TerrainCollapse::s_Enabled = true;
bool TerrainCollapse::s_BuildingsFall = true;
bool TerrainCollapse::s_PassesTrees = false;
TerrainCollapse::Tuning TerrainCollapse::s_Tuning;

namespace {
	/// What was around a blast before it dug its crater, so that what it cut loose can be told from what was already like that.
	struct Before {
		std::vector<std::vector<int>> Floating; //!< Each mass that was already hanging in the air: its pixels, sorted.
		std::vector<std::vector<int>> Hanging; //!< The middle of each piece that was already held on by only a thin neck: its pixels, sorted.
	};

	struct Check {
		int X, Y;
		int Radius;
		long long DueUpdate; //!< Sim update count when the check runs.
		std::shared_ptr<Before> Was; //!< Null if nothing is known of how things were.
	};

	constexpr int c_MinBodyPixels = 6; //!< Pieces smaller than this fall as loose particles.
	constexpr int c_MinBreakPixels = 40; //!< Pieces smaller than this don't crack any further.
	constexpr int c_MaxGeneration = 4; //!< How many times a piece's pieces may crack again.
	constexpr int c_MaxOutlinePoints = 260; //!< How many points of a piece's outline are tested against the terrain.
	constexpr int c_MaxDebrisPerUpdate = 160; //!< How many loose particles breaking pieces may throw off in one update.
	constexpr float c_Gravity = 0.1F; //!< Pixels per update, per update.
	constexpr float c_MaxSpeed = 9.0F; //!< Pixels per update.
	constexpr float c_Friction = 0.55F;
	constexpr float c_Bounce = 0.2F;
	constexpr int c_MaxAge = 3600; //!< A piece still moving after this many updates (a minute) is left where it is.

	std::array<bool, 256> s_Fixed{}; //!< Materials that are never lifted out of the terrain: doors (drawn by their own objects) and the world's edge.
	std::array<bool, 256> s_Structure{}; //!< Materials of buildings: concrete, metal and the like.
	std::array<bool, 256> s_Flimsy{}; //!< Materials too weak to hold a falling piece up: grass, plants, ash. A piece goes through them and flattens them.
	std::array<bool, 256> s_Leaves{}; //!< Vegetation: the leaves of trees and the base game's plants.
	std::array<bool, 256> s_TreeTrunk{}; //!< The wood of trees. A tree's leaves hang on its trunk, not on whatever ground or tree their tips brush against.
	std::array<bool, 256> s_NoHold{}; //!< Ash and charcoal: loose powder that neither holds anything up nor joins anything into one piece.
	std::array<float, 256> s_Density{};
	std::array<float, 256> s_Toughness{};
	std::array<float, 256> s_Scuff{}; //!< How readily walking on a material knocks it loose, 0 to 1.
	/// How a material breaks when a falling piece of it lands hard (MaterialBehaviour::BreakStyle). Each has its own threshold in the tuning.
	enum BreakStyle : unsigned char { c_Shatter, c_Crack, c_Crumble, c_Splinter, c_Bend, c_StyleCount };
	std::array<unsigned char, 256> s_Style{};
	std::array<float, 256> s_ImpactStrength{}; //!< Multiplies its style's threshold.
	std::array<int, 256> s_Neck{}; //!< How thin a neck of it snaps, in pixels; -1 for the tuning's NeckWidth.

	/// A material's break style and strength from its ini, or else the stock rule by name, so old mods' materials behave sensibly.
	void StockBreaking(const Material& material, unsigned char& style, float& strength, int& neck) {
		const MaterialBehaviour& behaviour = material.GetBehaviour();
		const std::string& name = material.GetPresetName();
		auto has = [&name](const char* word) { return name.find(word) != std::string::npos; };
		style = c_Crack;
		strength = 1.0F;
		neck = -1;
		if (has("Wood") || has("Tree Trunk") || has("Timber")) {
			style = c_Splinter;
			strength = has("Tree Trunk") ? 1.0F : 0.8F;
			// Wood holds by a sliver: a burning trunk stands until it's burnt right through.
			neck = 0;
		} else if (has("Scrap") || has("Mangled") || has("Shards")) {
			style = has("Shards") ? c_Crumble : c_Crack;
		} else if (has("Metal") || has("Plate") || has("Ladder") || has("Rubber") || has("Gold") || name == "Armoured Military Stuff") {
			style = c_Bend;
		} else if (has("Concrete") || has("Glass") || has("Ice")) {
			style = c_Shatter;
			strength = has("Glass") ? 0.45F : (has("Ice") ? 0.7F : 1.0F);
		} else if (behaviour.Powder > 0 || has("Sand") || has("Snow") || has("Topsoil") || has("Rubble") || has("Gravel") || has("Ash") || has("Charcoal") || has("Vegetation") || has("Leaves") || has("Grass")) {
			style = c_Crumble;
		} else if (has("Bedrock") || has("Cave Ceiling")) {
			strength = 1.8F;
		} else if (has("Stone")) {
			strength = has("Lunar") ? 1.0F : 1.2F;
		} else if (has("Dense")) {
			strength = 1.1F;
		} else if (has("Earth")) {
			strength = 0.9F;
		}
		static const std::pair<const char*, BreakStyle> names[] = {{"Shatter", c_Shatter}, {"Crack", c_Crack}, {"Crumble", c_Crumble}, {"Splinter", c_Splinter}, {"Bend", c_Bend}};
		for (const auto& [styleName, value]: names) {
			if (behaviour.BreakStyle == styleName) {
				style = value;
			}
		}
		if (behaviour.ImpactStrength >= 0.0F) {
			strength = behaviour.ImpactStrength;
		}
		if (behaviour.NeckWidth >= 0) {
			neck = behaviour.NeckWidth;
		}
	}
	int s_IceMaterial = 0;
	int s_WaterMaterial = 0;
	int s_WaterColor = 0;
	bool s_TablesBuilt = false;

	void BuildTables() {
		s_Fixed.fill(false);
		s_Structure.fill(false);
		s_Flimsy.fill(false);
		s_Leaves.fill(false);
		s_TreeTrunk.fill(false);
		s_NoHold.fill(false);
		s_IceMaterial = 0;
		s_WaterMaterial = 0;
		s_Density.fill(1.0F);
		s_Toughness.fill(60.0F);
		s_Scuff.fill(0.0F);
		s_Style.fill(c_Crack);
		s_ImpactStrength.fill(1.0F);
		s_Neck.fill(-1);
		for (int id = 1; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (!material || material->GetIndex() != id) {
				continue;
			}
			s_Density[id] = std::clamp(material->GetPixelDensity(), 0.05F, 50.0F);
			s_Toughness[id] = std::clamp(material->GetIntegrity(), 1.0F, 600.0F);
			s_Scuff[id] = std::clamp(material->GetBehaviour().Scuffs, 0.0F, 1.0F);
			s_Flimsy[id] = material->GetIntegrity() >= 0.0F && material->GetIntegrity() < 5.0F;
			StockBreaking(*material, s_Style[id], s_ImpactStrength[id], s_Neck[id]);
			const std::string& name = material->GetPresetName();
			s_Leaves[id] = name == "Vegetation" || TerrainTrees::IsLeaves(id);
			s_TreeTrunk[id] = name == "Tree Trunk" || TerrainTrees::IsTrunk(id);
			s_NoHold[id] = name == "Ashes" || name == "Charcoal";
			if (name == "Ice") {
				s_IceMaterial = id;
			} else if (name == "Water") {
				s_WaterMaterial = id;
				Color color = material->GetColor();
				color.RecalculateIndex();
				s_WaterColor = color.GetIndex();
			}
			for (const char* word: {"Door", "Level End", "Test", "Xenocronium"}) {
				if (name.find(word) != std::string::npos) {
					s_Fixed[id] = true;
				}
			}
			// Scrap and mangled metal are debris, not structure.
			if (name.find("Scrap") != std::string::npos || name.find("Mangled") != std::string::npos) {
				continue;
			}
			for (const char* word: {"Concrete", "Metal", "Plate", "Ladder", "Military", "Civilian", "Glass"}) {
				if (name.find(word) != std::string::npos) {
					s_Structure[id] = true;
					break;
				}
			}
		}
		s_TablesBuilt = true;
	}

	std::vector<Check> s_Pending; //!< Checks queued from (possibly parallel) gib code, waiting to be scheduled.

	/// Ground being worn away bit by bit (dug, shot at) is watched in squares of this many pixels. While the wearing goes on in a square, and for a couple of seconds after,
	/// the ground around it is checked every half second against how it was when the wearing began.
	constexpr int c_WatchCell = 48;
	constexpr int c_WatchRadius = 76;
	struct Watch {
		long long LastDamage = 0; //!< Sim update when the square last lost a pixel.
		long long NextCheck = 0;
		std::shared_ptr<Before> Was;
	};
	std::map<std::pair<int, int>, Watch> s_Watches; //!< By square (row, column): ordered, so they are always gone through in the same order.
	/// Squares newly being worn, waiting for their first look (LookBefore, a scan of some 23 k pixels with flood fills): only a few are looked at
	/// an update, so a crash that chips fifty squares at once doesn't do all fifty in the update already paying for the gibs. First worn, first looked at.
	std::deque<std::pair<int, int>> s_WatchesWaiting;
	std::map<std::pair<int, int>, long long> s_WatchWaitingDamage; //!< The waiting squares, with when each last lost a pixel.
	std::vector<int> s_Damage; //!< Pixels knocked out since the last update, x and y by turns. Filled from (possibly parallel) collision code.
	std::mutex s_DamageMutex;
	std::vector<Check> s_Scheduled;
	struct ChunkRequest {
		int X, Y, Radius;
		std::string Material;
	};
	std::vector<ChunkRequest> s_ChunkRequests;
	std::mutex s_QueueMutex;
	const void* s_Scene = nullptr;
	unsigned int s_SceneGeneration = 0; //!< SceneMan's count of scene loads when this scene was taken up: the same scene restarted, or a new one at the old one's address, still counts as new (as in FluidSim).
	int s_CollapsedCount = 0;
	int s_DebrisThisUpdate = 0;
	unsigned int s_Random = 0x51ED270Bu;

	float Random01() {
		s_Random ^= s_Random << 13;
		s_Random ^= s_Random >> 17;
		s_Random ^= s_Random << 5;
		return static_cast<float>(s_Random & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	float Cross(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }

	/// A detached piece of terrain: a rigid body with its own little bitmap of materials and colours.
	/// It is drawn into the terrain where it is each update (so units collide with it and it's rendered like any ground) and lifted out again before it moves.
	struct Body {
		int W = 0;
		int H = 0;
		std::vector<unsigned char> Materials; //!< Per pixel of its bitmap; 0 where there is none.
		std::vector<unsigned char> Colors;
		glm::vec2 Center{0.0F}; //!< Centre of mass, in its bitmap.
		float Mass = 1.0F;
		float Inertia = 1.0F;
		float Radius = 1.0F; //!< Furthest pixel from the centre of mass.
		float Toughness = 60.0F; //!< Average strength of its materials.
		std::array<float, c_StyleCount> StyleStrength{}; //!< Its carrying pixels' ImpactStrength summed, by break style: with the tuning's thresholds, how hard a landing breaks it.
		int CarryingPixels = 0; //!< Its pixels that count for how hard it is to break: all but leaves, grass and ash, unless it's nothing else.
		int PixelCount = 0;
		glm::vec2 Pos{0.0F}; //!< Of the centre of mass, in the scene.
		glm::vec2 Vel{0.0F}; //!< Pixels per update.
		float Angle = 0.0F;
		float Spin = 0.0F; //!< Radians per update.
		std::vector<glm::vec2> Outline; //!< Points on its edge, relative to the centre of mass, unrotated.
		std::vector<std::pair<int, int>> Stamped; //!< Where it's drawn in the terrain: pixel key and which of its own pixels is there.
		int Still = 0;
		int Age = 0;
		int Generation = 0;
		int BreakCooldown = 0;
		bool Done = false;
		bool Wet = false; //!< Whether it was in liquid last update.
		bool Damaged = false; //!< Whether pixels have been taken off it since its mass and outline were worked out.
		std::vector<std::pair<long, long long>> Hurt; //!< Units it hurt lately: unique ID and sim update, so a piece grinding on a unit hurts it once per blow, not every update.
	};

	/// Whether a piece is a tree coming down: a good share of tree trunk, its leaves the rest. (Every 7th pixel is enough to tell.)
	bool IsTree(const Body& body) {
		int trunk = 0;
		int solid = 0;
		for (size_t i = 0; i < body.Materials.size(); i += 7) {
			if (unsigned char material = body.Materials[i]; material != 0) {
				++solid;
				trunk += s_TreeTrunk[material] ? 1 : 0;
			}
		}
		return trunk * 5 > solid && trunk > 0;
	}

	std::vector<Body> s_Bodies;
	std::vector<Body> s_NewBodies; //!< Pieces made while the bodies are being stepped; they join afterwards.

	/// A piece that came to rest not long ago and is ordinary ground again. Remembered so a blast beside it can pick it up and throw it, as debris lying about should be.
	struct Rested {
		std::vector<int> Keys; //!< Its pixels in the terrain.
		glm::vec2 Center{0.0F};
		float Radius = 0.0F;
		long long When = 0; //!< Sim update when it came to rest.
	};
	std::vector<Rested> s_Rested;
	constexpr size_t c_MaxRested = 64;
	constexpr long long c_RestedUpdates = 3600; //!< How long a rested piece is remembered: a minute.
	struct BlastRequest {
		int X, Y;
		float Reach, Push;
	};
	std::vector<BlastRequest> s_Blasts;

	/// A foot coming down, queued from the units' updates.
	struct Footfall {
		int X, Y, Direction;
		float Speed;
	};
	std::vector<Footfall> s_Footfalls;
	constexpr size_t c_MaxFootfalls = 64;

	/// What the checks know about each terrain pixel. A flat array, a byte per pixel: with hash sets a single check of a big crater took 20 to 40 ms, a visible hitch after every blast.
	enum PixelState : unsigned char {
		c_Seen = 1, //!< Reached by a fill during the current check.
		c_Supported = 2, //!< Part of a piece found to be held up during the current check.
		c_Falling = 4 //!< Part of a falling piece, which checks ignore. Kept between checks.
	};
	std::vector<unsigned char> s_State;
	std::vector<int> s_Touched; //!< Pixels marked seen or supported during the current check, to clear afterwards.
	std::vector<int> s_Stack;
	std::vector<int> s_FillSeen; //!< Every pixel the current fill has reached.

	int s_Width = 0;
	int s_Height = 0;
	bool s_WrapX = false;

	bool WrapInWorld(int& x, int y) {
		if (s_WrapX) {
			x = ((x % s_Width) + s_Width) % s_Width;
		}
		return x >= 0 && y >= 0 && x < s_Width && y < s_Height;
	}

	/// Whether a falling piece would hit something at a point: solid ground, not liquid, and not a tree while pieces go through trees.
	bool SolidAt(const BITMAP* materialBitmap, int x, int y) {
		if (!WrapInWorld(x, y)) {
			return false;
		}
		int material = materialBitmap->line[y][x];
		return material != g_MaterialAir && !s_Flimsy[material] && !FluidSim::IsLiquid(material) && !(TerrainCollapse::PassesTrees() && TerrainTrees::IsTreeMaterial(material));
	}

	/// Works out a body's mass, centre, inertia and outline from its bitmap. Returns false if nothing is left of it.
	bool FinishBody(Body& body, const glm::vec2& topLeft) {
		double mass = 0.0;
		double centerX = 0.0;
		double centerY = 0.0;
		double toughness = 0.0;
		body.PixelCount = 0;
		body.StyleStrength.fill(0.0F);
		body.CarryingPixels = 0;
		std::array<float, c_StyleCount> flimsyStrength{};
		for (int y = 0; y < body.H; ++y) {
			for (int x = 0; x < body.W; ++x) {
				int material = body.Materials[static_cast<size_t>(y) * body.W + x];
				if (material) {
					float density = s_Density[material];
					mass += density;
					centerX += (static_cast<double>(x) + 0.5) * density;
					centerY += (static_cast<double>(y) + 0.5) * density;
					toughness += s_Toughness[material];
					++body.PixelCount;
					if (s_Flimsy[material]) {
						flimsyStrength[s_Style[material]] += s_ImpactStrength[material];
					} else {
						body.StyleStrength[s_Style[material]] += s_ImpactStrength[material];
						++body.CarryingPixels;
					}
				}
			}
		}
		if (body.PixelCount == 0) {
			return false;
		}
		body.Mass = static_cast<float>(mass);
		body.Center = glm::vec2(static_cast<float>(centerX / mass), static_cast<float>(centerY / mass));
		body.Toughness = static_cast<float>(toughness / body.PixelCount);
		// Leaves on a trunk don't make it weaker; a piece of nothing but leaves goes by them.
		if (body.CarryingPixels == 0) {
			body.StyleStrength = flimsyStrength;
			body.CarryingPixels = body.PixelCount;
		}
		double inertia = 0.0;
		float radius = 1.0F;
		std::vector<glm::vec2> edge;
		for (int y = 0; y < body.H; ++y) {
			for (int x = 0; x < body.W; ++x) {
				int material = body.Materials[static_cast<size_t>(y) * body.W + x];
				if (!material) {
					continue;
				}
				glm::vec2 offset = glm::vec2(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F) - body.Center;
				float distanceSquared = glm::dot(offset, offset);
				inertia += static_cast<double>(s_Density[material]) * (distanceSquared + 0.17);
				radius = std::max(radius, std::sqrt(distanceSquared));
				auto empty = [&](int nx, int ny) { return nx < 0 || ny < 0 || nx >= body.W || ny >= body.H || !body.Materials[static_cast<size_t>(ny) * body.W + nx]; };
				if (empty(x - 1, y) || empty(x + 1, y) || empty(x, y - 1) || empty(x, y + 1)) {
					edge.push_back(offset);
				}
			}
		}
		body.Inertia = std::max(static_cast<float>(inertia), 1.0F);
		body.Radius = radius + 0.75F;
		body.Outline.clear();
		size_t stride = std::max<size_t>(1, (edge.size() + c_MaxOutlinePoints - 1) / c_MaxOutlinePoints);
		for (size_t i = 0; i < edge.size(); i += stride) {
			body.Outline.push_back(edge[i]);
		}
		body.Pos = topLeft + body.Center;
		return true;
	}

	glm::vec2 ToWorld(const Body& body, const glm::vec2& offset, const glm::vec2& pos, float angle) {
		float c = std::cos(angle);
		float s = std::sin(angle);
		return pos + glm::vec2(offset.x * c - offset.y * s, offset.x * s + offset.y * c);
	}

	/// Lifts a body's pixels out of the terrain. Pixels of it that have been destroyed since it was drawn (shot or blasted away) are taken off the body too.
	void Unstamp(SLTerrain* terrain, Body& body) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		for (const auto& [key, local]: body.Stamped) {
			int x = key % s_Width;
			int y = key / s_Width;
			s_State[key] &= static_cast<unsigned char>(~c_Falling);
			if (materialBitmap->line[y][x] == g_MaterialAir) {
				if (body.Materials[local]) {
					body.Materials[local] = 0;
					--body.PixelCount;
					body.Damaged = true;
				}
				continue;
			}
			terrain->SetMaterialPixel(x, y, g_MaterialAir);
			terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
		}
		body.Stamped.clear();
	}

	/// A pixel of liquid a body has just covered, to be put back elsewhere (see PlaceDisplaced).
	struct Displaced {
		int X, Y, Material, Color;
	};

	/// Puts the liquid a body pushed aside back into the liquid it came from: in the lowest free places at that liquid's edge (the gap the body
	/// left behind it as it sinks, then the surface), nearest the body first, so the level all round rises by what went in. The places are
	/// found by a search through the liquid only, never through the body: the old way went straight up through the body to the first air,
	/// and the water a piece fell into appeared on top of it.
	/// @return How many it couldn't place (nowhere reachable: a body filling a closed tank); those are the last ones in the list.
	size_t PlaceDisplaced(SLTerrain* terrain, const Body& body, std::vector<Displaced>& displaced) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		// The search is in a window round the body, its cells marked with a search number rather than cleared (as FluidSim's level search).
		constexpr int halfWidth = 512;
		constexpr int halfHeight = 320;
		constexpr int windowWidth = halfWidth * 2 + 1;
		// How much of the liquid one search may cross: a wide pool's surface for a big piece, less for a few pixels (a piece sinking a pixel
		// an update covers a row of them; each search was the full count, for every moving piece, every update).
		const size_t maxLiquidCells = std::min<size_t>(24000, 2000 + displaced.size() * 100);
		static std::vector<unsigned int> visited;
		static unsigned int search = 0;
		if (visited.empty() || ++search == 0) {
			visited.assign(static_cast<size_t>(windowWidth) * (halfHeight * 2 + 1), 0);
			search = 1;
		}
		int originX = static_cast<int>(std::floor(body.Pos.x));
		int originY = static_cast<int>(std::floor(body.Pos.y));
		// Where a scene cell is in the window, and how far across from the body (the shorter way round a wrapping scene); false outside it.
		auto inWindow = [&](int x, int y, size_t& slot, int& across) {
			int dx = x - originX;
			if (s_WrapX && s_Width > 0) {
				dx = ((dx % s_Width) + s_Width + s_Width / 2) % s_Width - s_Width / 2;
			}
			int dy = y - originY;
			if (dx < -halfWidth || dx > halfWidth || dy < -halfHeight || dy > halfHeight) {
				return false;
			}
			slot = static_cast<size_t>(dy + halfHeight) * windowWidth + static_cast<size_t>(dx + halfWidth);
			across = dx;
			return true;
		};
		struct Cell {
			int X, Y, Across;
		};
		// The liquid is searched upwards first, so the surface over a deep piece is reached before the cells run out; free places are taken
		// lowest first, then nearest the body, so the gap behind a sinking piece closes before the surface rises, and the surface rises
		// outwards from the piece as a swell rather than all over in specks.
		auto liquidOrder = [](const Cell& a, const Cell& b) { return a.Y != b.Y ? a.Y > b.Y : (std::abs(a.Across) != std::abs(b.Across) ? std::abs(a.Across) > std::abs(b.Across) : a.Across > b.Across); };
		auto freeOrder = [](const Cell& a, const Cell& b) { return a.Y != b.Y ? a.Y < b.Y : (std::abs(a.Across) != std::abs(b.Across) ? std::abs(a.Across) > std::abs(b.Across) : a.Across > b.Across); };
		static std::vector<Cell> liquidCells;
		static std::vector<Cell> freePlaces;
		liquidCells.clear();
		freePlaces.clear();
		size_t liquidSeen = 0;
		auto look = [&](int x, int y) {
			if (!WrapInWorld(x, y)) {
				return;
			}
			size_t slot = 0;
			int across = 0;
			if (!inWindow(x, y, slot, across) || visited[slot] == search) {
				return;
			}
			int material = materialBitmap->line[y][x];
			if (material == g_MaterialAir) {
				visited[slot] = search;
				freePlaces.push_back({x, y, across});
				std::push_heap(freePlaces.begin(), freePlaces.end(), freeOrder);
			} else if (FluidSim::IsLiquid(material) && liquidSeen < maxLiquidCells) {
				visited[slot] = search;
				++liquidSeen;
				liquidCells.push_back({x, y, across});
				std::push_heap(liquidCells.begin(), liquidCells.end(), liquidOrder);
			}
		};
		static constexpr int sides[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
		for (const Displaced& pixel: displaced) {
			for (const auto& side: sides) {
				look(pixel.X + side[0], pixel.Y + side[1]);
			}
		}
		while (!liquidCells.empty()) {
			std::pop_heap(liquidCells.begin(), liquidCells.end(), liquidOrder);
			Cell cell = liquidCells.back();
			liquidCells.pop_back();
			for (const auto& side: sides) {
				look(cell.X + side[0], cell.Y + side[1]);
			}
		}
		size_t placed = 0;
		int frothed = 0;
		while (placed < displaced.size() && !freePlaces.empty()) {
			std::pop_heap(freePlaces.begin(), freePlaces.end(), freeOrder);
			Cell cell = freePlaces.back();
			freePlaces.pop_back();
			if (materialBitmap->line[cell.Y][cell.X] != g_MaterialAir) {
				continue;
			}
			const Displaced& pixel = displaced[placed++];
			terrain->SetMaterialPixel(cell.X, cell.Y, pixel.Material);
			terrain->SetFGColorPixel(cell.X, cell.Y, pixel.Color);
			// It flows on from there if it can (over an edge, along a step).
			FluidSim::Disturb(Vector(static_cast<float>(cell.X), static_cast<float>(cell.Y)), 1.0F);
			// Where it tops the liquid, the rising surface froths (for the eye; a few puffs, not one for every pixel).
			if (placed % 4 == 1 && frothed < 24 && cell.Y > 0 && materialBitmap->line[cell.Y - 1][cell.X] == g_MaterialAir) {
				FluidSim::Froth(Vector(static_cast<float>(cell.X), static_cast<float>(cell.Y)), 4.0F, 1, pixel.Color);
				++frothed;
			}
			// Its own edge is the liquid's edge now: the free places beside and above it are next.
			for (const auto& side: sides) {
				look(cell.X + side[0], cell.Y + side[1]);
			}
		}
		return displaced.size() - placed;
	}

	/// Draws a body into the terrain where it is. Liquid in its way is pushed aside into the rest of that liquid (PlaceDisplaced) rather than lost.
	void Stamp(SLTerrain* terrain, Body& body) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		float c = std::cos(body.Angle);
		float s = std::sin(body.Angle);
		int reach = static_cast<int>(std::ceil(body.Radius)) + 1;
		int centerX = static_cast<int>(std::floor(body.Pos.x));
		int centerY = static_cast<int>(std::floor(body.Pos.y));
		static std::vector<Displaced> displaced;
		displaced.clear();
		for (int wy = std::max(0, centerY - reach); wy <= std::min(s_Height - 1, centerY + reach); ++wy) {
			for (int rawX = centerX - reach; rawX <= centerX + reach; ++rawX) {
				glm::vec2 offset = glm::vec2(static_cast<float>(rawX) + 0.5F, static_cast<float>(wy) + 0.5F) - body.Pos;
				// Turned back into the body's own bitmap.
				float localX = offset.x * c + offset.y * s + body.Center.x;
				float localY = -offset.x * s + offset.y * c + body.Center.y;
				if (localX < 0.0F || localY < 0.0F) {
					continue;
				}
				int lx = static_cast<int>(localX);
				int ly = static_cast<int>(localY);
				if (lx >= body.W || ly >= body.H) {
					continue;
				}
				int local = ly * body.W + lx;
				if (!body.Materials[local]) {
					continue;
				}
				int wx = rawX;
				if (!WrapInWorld(wx, wy)) {
					continue;
				}
				int existing = materialBitmap->line[wy][wx];
				// Liquid is pushed aside (below); grass and the like is flattened; anything else solid is left as it is. A tree a piece goes
				// through is left as it is too, leaves and all: the piece goes behind it.
				if (existing != g_MaterialAir && !FluidSim::IsLiquid(existing) && (!s_Flimsy[existing] || (TerrainCollapse::PassesTrees() && TerrainTrees::IsTreeMaterial(existing)))) {
					continue;
				}
				if (FluidSim::IsLiquid(existing)) {
					if (displaced.size() < 60000) {
						displaced.push_back({wx, wy, existing, terrain->GetFGColorPixel(wx, wy)});
					}
				}
				terrain->SetMaterialPixel(wx, wy, body.Materials[local]);
				terrain->SetFGColorPixel(wx, wy, body.Colors[local]);
				int key = wy * s_Width + wx;
				s_State[key] |= c_Falling;
				body.Stamped.emplace_back(key, local);
			}
		}
		if (displaced.empty()) {
			return;
		}
		size_t unplaced = PlaceDisplaced(terrain, body, displaced);
		// Only with nowhere in the liquid to go (a piece filling a closed tank): up through the piece and the liquid above it to the first free
		// place, or, under a ceiling, sideways along the highest row it can reach, as before. None is lost.
		for (size_t i = displaced.size() - unplaced; i < displaced.size(); ++i) {
			const Displaced& liquid = displaced[i];
			auto passable = [&](int x, int y) {
				int material = materialBitmap->line[y][x];
				return FluidSim::IsLiquid(material) || (s_State[y * s_Width + x] & c_Falling);
			};
			auto place = [&](int x, int y) {
				terrain->SetMaterialPixel(x, y, liquid.Material);
				terrain->SetFGColorPixel(x, y, liquid.Color);
			};
			int row = liquid.Y;
			bool placed = false;
			for (int y = liquid.Y - 1; y >= 0 && liquid.Y - y <= 700; --y) {
				if (materialBitmap->line[y][liquid.X] == g_MaterialAir) {
					place(liquid.X, y);
					placed = true;
					break;
				}
				if (!passable(liquid.X, y)) {
					break;
				}
				row = y;
			}
			for (int side = 0; side < 2 && !placed; ++side) {
				for (int step = 1; step <= 500; ++step) {
					int x = liquid.X + (side == 0 ? step : -step);
					int y = row;
					if (!WrapInWorld(x, y)) {
						break;
					}
					if (materialBitmap->line[y][x] == g_MaterialAir) {
						place(x, y);
						placed = true;
						break;
					}
					if (!passable(x, y)) {
						break;
					}
				}
			}
		}
		FluidSim::Disturb(Vector(body.Pos.x, body.Pos.y), body.Radius + 6.0F);
	}

	/// Whether this many more pixels can be thrown off as loose particles this update. Beyond that, a scrap stays a little body of its own and comes apart later,
	/// rather than vanishing.
	bool CanThrow(size_t count) {
		return s_DebrisThisUpdate + static_cast<int>(count) <= c_MaxDebrisPerUpdate;
	}

	/// Throws a pixel of a body off as a loose particle.
	void ThrowDebris(int material, int colorIndex, const glm::vec2& position, const glm::vec2& velocity) {
		if (s_DebrisThisUpdate >= c_MaxDebrisPerUpdate || colorIndex == ColorKeys::g_MaskColor) {
			return;
		}
		const Material* sceneMaterial = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(material));
		if (!sceneMaterial) {
			return;
		}
		++s_DebrisThisUpdate;
		// A pixel of a piece pressed against the ground can sit a hair inside it, and a particle that starts inside the ground is lost: lift it clear first.
		glm::vec2 at = position;
		if (SLTerrain* terrain = g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetTerrain() : nullptr) {
			const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
			for (int lift = 0; lift < 8; ++lift) {
				int x = static_cast<int>(std::floor(at.x));
				int y = static_cast<int>(std::floor(at.y)) - lift;
				if (!WrapInWorld(x, y) || materialBitmap->line[y][x] == g_MaterialAir || FluidSim::IsLiquid(materialBitmap->line[y][x])) {
					at.y -= static_cast<float>(lift);
					break;
				}
			}
		}
		// Loose bits fly off at the piece's own speed: fast ones of hard material strike sparks where they hit.
		Color color;
		color.SetRGBWithIndex(colorIndex);
		// Particle speeds are in metres a second: 20 pixels to the metre, 60 updates a second.
		MOPixel* pixel = new MOPixel(color, sceneMaterial->GetPixelDensity(), Vector(at.x, at.y), Vector(velocity.x * 3.0F, velocity.y * 3.0F), new Atom(Vector(), sceneMaterial->GetIndex(), nullptr, color, 2), 0);
		pixel->SetToHitMOs(false);
		g_MovableMan.AddParticle(pixel);
	}

	/// A puff of dust and a few chips where a piece lands or breaks, in the colour of what it's made of. Visual only: no fire, no light, nothing that touches the simulation.
	void ThrowDust(const glm::vec2& point, int amount, const std::vector<unsigned char>& materials, const std::vector<unsigned char>& colors) {
		unsigned int rgb = 0;
		for (size_t i = 0; i < materials.size(); i += 7) {
			if (materials[i]) {
				Color color;
				color.SetRGBWithIndex(colors[i]);
				rgb = EffectsParticles::ColorToRGB(color);
				break;
			}
		}
		if (amount > 0) {
			// And a cloud of it that hangs in the air and drifts off (the fog volume; render only).
			g_PostProcessMan.RegisterFog(Vector(point.x, point.y - 4.0F), 12.0F + static_cast<float>(amount), std::min(0.04F * static_cast<float>(amount), 0.6F));
			EffectsParticles::Emit("Dust", Vector(point.x, point.y), Vector(0.0F, -1.5F), 1.0F, amount, rgb);
			EffectsParticles::Emit("Debris", Vector(point.x, point.y), Vector(0.0F, -3.0F), 1.0F, std::max(amount / 3, 1), rgb);
		}
	}

	/// Turns everything left of a body into loose particles.
	void Crumble(Body& body) {
		for (int y = 0; y < body.H; ++y) {
			for (int x = 0; x < body.W; ++x) {
				int local = y * body.W + x;
				if (body.Materials[local]) {
					glm::vec2 offset = glm::vec2(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F) - body.Center;
					glm::vec2 at = ToWorld(body, offset, body.Pos, body.Angle);
					ThrowDebris(body.Materials[local], body.Colors[local], at, body.Vel + glm::vec2(Random01() - 0.5F, Random01() - 0.5F));
				}
			}
		}
		body.Done = true;
	}

	/// Which way is out of the ground at a point: away from the solid pixels around it.
	glm::vec2 SurfaceNormal(const BITMAP* materialBitmap, const glm::vec2& point, const glm::vec2& fallback) {
		int px = static_cast<int>(std::floor(point.x));
		int py = static_cast<int>(std::floor(point.y));
		glm::vec2 sum(0.0F);
		for (int dy = -3; dy <= 3; ++dy) {
			for (int dx = -3; dx <= 3; ++dx) {
				if (SolidAt(materialBitmap, px + dx, py + dy)) {
					sum -= glm::vec2(static_cast<float>(dx), static_cast<float>(dy));
				}
			}
		}
		float length = glm::length(sum);
		if (length < 0.5F) {
			float fallbackLength = glm::length(fallback);
			return fallbackLength > 0.001F ? fallback / fallbackLength : glm::vec2(0.0F, -1.0F);
		}
		return sum / length;
	}

	/// Puts a pixel of loose ground into the terrain at the nearest free place at or above a point (a little to either side as it goes up).
	/// Returns false if there's no room near.
	bool PlaceGrain(SLTerrain* terrain, int material, int colorIndex, int x, int y) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		for (int up = 0; up < 12; ++up) {
			for (int side: {0, -1, 1, -2, 2}) {
				int px = x + side;
				int py = y - up;
				if (!WrapInWorld(px, py) || materialBitmap->line[py][px] != g_MaterialAir) {
					continue;
				}
				terrain->SetMaterialPixel(px, py, material);
				terrain->SetFGColorPixel(px, py, colorIndex);
				return true;
			}
		}
		return false;
	}

	/// How hard a landing breaks a body, in pixels per update: its carrying materials' style thresholds, each scaled by the material's ImpactStrength, averaged by pixel count.
	float BreakSpeed(const Body& body) {
		const TerrainCollapse::Tuning& tuning = TerrainCollapse::GetTuning();
		const float thresholds[c_StyleCount] = {tuning.ShatterSpeed, tuning.CrackSpeed, tuning.CrumbleSpeed, tuning.SplinterSpeed, tuning.BendSpeed};
		float sum = 0.0F;
		for (int style = 0; style < c_StyleCount; ++style) {
			sum += body.StyleStrength[style] * std::max(thresholds[style], 0.0F);
		}
		// Particle speeds are in metres a second: 20 pixels to the metre, 60 updates a second.
		float metresPerSecond = sum / static_cast<float>(std::max(body.CarryingPixels, 1));
		return metresPerSecond / 3.0F * std::max(tuning.BreakStrength, 0.1F);
	}

	/// Breaks a body where it hit, each of its materials by its own style (MaterialBehaviour::BreakStyle): Shatter splits it into many jagged shards around the hit
	/// with a spray of chips, Crack cuts it along a few lines, Crumble turns what's near the hit into loose grains, Splinter snaps it across its length into a few
	/// long pieces, and Bend (metal) holds together. Leaves and grass go with the style of what carries them. The pieces become bodies of their own, or loose
	/// particles if they're tiny.
	void Break(Body& body, const glm::vec2& hitPoint, float violence) {
		float c = std::cos(body.Angle);
		float s = std::sin(body.Angle);
		glm::vec2 offset = hitPoint - body.Pos;
		glm::vec2 hitLocal(offset.x * c + offset.y * s + body.Center.x, -offset.x * s + offset.y * c + body.Center.y);
		// Leaves go with the style that carries the most of the piece.
		int mainStyle = c_Crack;
		for (int style = 0; style < c_StyleCount; ++style) {
			if (body.StyleStrength[style] > body.StyleStrength[mainStyle]) {
				mainStyle = style;
			}
		}
		struct Cut {
			glm::vec2 Through, Across;
			float Wobble, Phase;
		};
		auto sideOf = [](const std::vector<Cut>& lines, const glm::vec2& at) {
			int side = 0;
			for (size_t i = 0; i < lines.size(); ++i) {
				glm::vec2 from = at - lines[i].Through;
				float along = from.x * -lines[i].Across.y + from.y * lines[i].Across.x;
				// A jagged line rather than a ruled one.
				float distance = glm::dot(from, lines[i].Across) + lines[i].Wobble * std::sin(along * 0.45F + lines[i].Phase) + 0.6F * std::sin(along * 1.7F);
				side |= (distance > 0.0F ? 1 : 0) << i;
			}
			return side;
		};
		// Crack: a few lines, the first from where it hit, the others nearer the middle.
		std::vector<Cut> cracks;
		int cuts = 1 + (violence > 1.5F ? 1 : 0) + (body.Toughness < 60.0F ? 1 : 0);
		for (int i = 0; i < cuts; ++i) {
			float angle = Random01() * 3.14159F;
			glm::vec2 through = i == 0 ? glm::mix(hitLocal, body.Center, 0.35F) : body.Center + glm::vec2(Random01() - 0.5F, Random01() - 0.5F) * body.Radius * 0.8F;
			cracks.push_back({through, glm::vec2(std::cos(angle), std::sin(angle)), 1.0F + Random01() * 2.5F, Random01() * 6.28F});
		}
		// Splinter: across its long axis (the grain), through the middle nudged toward the hit; a second snap only for a huge hit.
		std::vector<Cut> snaps;
		{
			double xx = 0.0;
			double yy = 0.0;
			double xy = 0.0;
			for (int y = 0; y < body.H; ++y) {
				for (int x = 0; x < body.W; ++x) {
					if (body.Materials[static_cast<size_t>(y) * body.W + x]) {
						double dx = static_cast<double>(x) + 0.5 - body.Center.x;
						double dy = static_cast<double>(y) + 0.5 - body.Center.y;
						xx += dx * dx;
						yy += dy * dy;
						xy += dx * dy;
					}
				}
			}
			float grain = 0.5F * static_cast<float>(std::atan2(2.0 * xy, xx - yy));
			glm::vec2 along(std::cos(grain), std::sin(grain));
			float hitAlong = glm::dot(hitLocal - body.Center, along);
			int count = violence > 2.5F ? 2 : 1;
			for (int i = 0; i < count; ++i) {
				float at = i == 0 ? hitAlong * 0.35F : -hitAlong * 0.5F + (Random01() - 0.5F) * body.Radius * 0.4F;
				// The cut runs across the grain, a little askew, with a ragged, splintery edge.
				float skew = (Random01() - 0.5F) * 0.5F;
				glm::vec2 across(std::cos(grain + skew), std::sin(grain + skew));
				snaps.push_back({body.Center + along * at, across, 1.5F + Random01() * 1.5F, Random01() * 6.28F});
			}
		}
		// Shatter: shards around seed points, most of them near the hit.
		std::vector<glm::vec2> seeds;
		std::vector<float> seedPhase;
		int shards = std::clamp(static_cast<int>(3.0F + 2.0F * violence + static_cast<float>(body.PixelCount) / 400.0F), 4, 12);
		for (int i = 0; i < shards; ++i) {
			glm::vec2 seed = body.Center;
			for (int attempt = 0; attempt < 30; ++attempt) {
				glm::vec2 candidate(Random01() * static_cast<float>(body.W), Random01() * static_cast<float>(body.H));
				int cx = std::clamp(static_cast<int>(candidate.x), 0, body.W - 1);
				int cy = std::clamp(static_cast<int>(candidate.y), 0, body.H - 1);
				if (!body.Materials[static_cast<size_t>(cy) * body.W + cx]) {
					continue;
				}
				seed = candidate;
				if (Random01() < std::exp(-glm::length(candidate - hitLocal) / std::max(body.Radius * 0.5F, 1.0F))) {
					break;
				}
			}
			seeds.push_back(seed);
			seedPhase.push_back(Random01() * 6.28F);
		}
		// How far from the hit brittle material is powdered and loose ground falls apart into grains.
		float chipRadius = 1.5F + std::min(violence, 4.0F);
		float crumbleRadius = body.Radius * std::min(1.0F, 0.35F + 0.25F * violence) + 2.0F;
		int debrisLeft = c_MaxDebrisPerUpdate - s_DebrisThisUpdate;
		constexpr int c_Loose = -3; //!< A pixel thrown off as a loose particle.
		constexpr int c_Grain = -4; //!< A pixel of loose ground let go where it is, as loose grains in the terrain (the powders then slide them down: FluidSim).
		std::vector<int> region(body.Materials.size(), -1);
		for (int y = 0; y < body.H; ++y) {
			for (int x = 0; x < body.W; ++x) {
				int local = y * body.W + x;
				int material = body.Materials[local];
				if (!material) {
					continue;
				}
				glm::vec2 at(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F);
				float fromHit = glm::length(at - hitLocal);
				int style = s_Flimsy[material] ? mainStyle : s_Style[material];
				int id = 0;
				bool loose = false;
				switch (style) {
					case c_Shatter: {
						size_t nearest = 0;
						float best = 1.0e9F;
						for (size_t i = 0; i < seeds.size(); ++i) {
							// Jittered, so the shards have jagged edges.
							float distance = glm::length(at - seeds[i]) + 1.5F * std::sin(at.x * 0.9F + seedPhase[i]) + 1.2F * std::sin(at.y * 1.3F - seedPhase[i]);
							if (distance < best) {
								best = distance;
								nearest = i;
							}
						}
						id = 0x100 + static_cast<int>(nearest);
						loose = fromHit < chipRadius && Random01() < 0.4F;
						break;
					}
					case c_Crumble:
						id = 0x200;
						loose = fromHit < crumbleRadius;
						break;
					case c_Splinter:
						id = 0x300 + sideOf(snaps, at);
						break;
					case c_Bend:
						id = 0x400;
						break;
					default:
						id = sideOf(cracks, at);
						break;
				}
				if (loose && style == c_Crumble) {
					region[local] = c_Grain;
				} else if (loose && debrisLeft > 0) {
					--debrisLeft;
					region[local] = c_Loose;
				} else {
					region[local] = id;
				}
			}
		}
		// Splinters: a few chips of wood off the edges of each snap.
		for (int local = 0; local < static_cast<int>(region.size()) && debrisLeft > 0; ++local) {
			if (region[local] < 0x300 || region[local] >= 0x400) {
				continue;
			}
			int x = local % body.W;
			int y = local / body.W;
			bool onBreak = (x > 0 && region[local - 1] >= 0 && region[local - 1] != region[local]) || (x + 1 < body.W && region[local + 1] >= 0 && region[local + 1] != region[local]) ||
			               (y > 0 && region[local - body.W] >= 0 && region[local - body.W] != region[local]) || (y + 1 < body.H && region[local + body.W] >= 0 && region[local + body.W] != region[local]);
			if (onBreak && Random01() < 0.35F) {
				--debrisLeft;
				region[local] = c_Loose;
			}
		}
		// Grains go straight into the terrain where they are, rather than as particles: a heap of particles landing together settles on top of each other and
		// most are lost.
		SLTerrain* terrain = g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetTerrain() : nullptr;
		int grains = 0;
		for (int local = 0; local < static_cast<int>(region.size()) && terrain; ++local) {
			if (region[local] != c_Grain) {
				continue;
			}
			glm::vec2 partOffset = glm::vec2(static_cast<float>(local % body.W) + 0.5F, static_cast<float>(local / body.W) + 0.5F) - body.Center;
			glm::vec2 at = ToWorld(body, partOffset, body.Pos, body.Angle);
			if (PlaceGrain(terrain, body.Materials[local], body.Colors[local], static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)))) {
				region[local] = -2;
				++grains;
			} else {
				region[local] = 0x200;
			}
		}
		if (grains > 0) {
			float reach = body.Radius + 4.0F;
			terrain->AddUpdatedMaterialArea(Box(Vector(body.Pos.x - reach, body.Pos.y - reach), reach * 2.0F, reach * 2.0F));
			FluidSim::Disturb(Vector(body.Pos.x, body.Pos.y), reach + 4.0F);
		}
		for (int local = 0; local < static_cast<int>(region.size()); ++local) {
			if (region[local] == c_Loose) {
				glm::vec2 partOffset = glm::vec2(static_cast<float>(local % body.W) + 0.5F, static_cast<float>(local / body.W) + 0.5F) - body.Center;
				glm::vec2 at = ToWorld(body, partOffset, body.Pos, body.Angle);
				glm::vec2 arm = at - body.Pos;
				// Grains spill at the piece's speed; chips fly up and out.
				float spray = s_Style[body.Materials[local]] == c_Crumble ? 0.6F : 1.5F + 0.4F * std::min(violence, 3.0F);
				ThrowDebris(body.Materials[local], body.Colors[local], at, body.Vel + body.Spin * glm::vec2(-arm.y, arm.x) + glm::vec2(Random01() - 0.5F, -Random01()) * spray);
				region[local] = -2;
			}
		}
		float burst = mainStyle == c_Shatter ? 0.25F + 0.2F * std::min(violence, 3.0F) : 0.25F;
		// Each connected part of each side is a piece.
		std::vector<int> stack;
		std::vector<int> part;
		for (int start = 0; start < static_cast<int>(region.size()); ++start) {
			if (region[start] < 0) {
				continue;
			}
			int side = region[start];
			part.clear();
			stack.clear();
			stack.push_back(start);
			region[start] = -2;
			int minX = body.W;
			int minY = body.H;
			int maxX = 0;
			int maxY = 0;
			while (!stack.empty()) {
				int local = stack.back();
				stack.pop_back();
				part.push_back(local);
				int x = local % body.W;
				int y = local / body.W;
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
				static constexpr int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
				for (const auto& step: steps) {
					int nx = x + step[0];
					int ny = y + step[1];
					if (nx < 0 || ny < 0 || nx >= body.W || ny >= body.H) {
						continue;
					}
					int neighbour = ny * body.W + nx;
					if (region[neighbour] == side) {
						region[neighbour] = -2;
						stack.push_back(neighbour);
					}
				}
			}
			if (static_cast<int>(part.size()) < (mainStyle == c_Shatter ? c_MinBodyPixels : 20) && CanThrow(part.size())) {
				for (int local: part) {
					glm::vec2 partOffset = glm::vec2(static_cast<float>(local % body.W) + 0.5F, static_cast<float>(local / body.W) + 0.5F) - body.Center;
					glm::vec2 at = ToWorld(body, partOffset, body.Pos, body.Angle);
					glm::vec2 arm = at - body.Pos;
					ThrowDebris(body.Materials[local], body.Colors[local], at, body.Vel + body.Spin * glm::vec2(-arm.y, arm.x) + glm::vec2(Random01() - 0.5F, -Random01()) * 1.5F);
				}
				continue;
			}
			Body piece;
			piece.W = maxX - minX + 1;
			piece.H = maxY - minY + 1;
			piece.Materials.assign(static_cast<size_t>(piece.W) * piece.H, 0);
			piece.Colors.assign(static_cast<size_t>(piece.W) * piece.H, 0);
			for (int local: part) {
				int index = (local / body.W - minY) * piece.W + (local % body.W - minX);
				piece.Materials[index] = body.Materials[local];
				piece.Colors[index] = body.Colors[local];
			}
			if (!FinishBody(piece, glm::vec2(0.0F))) {
				continue;
			}
			// Where its centre is in the scene, given where the old body's was.
			glm::vec2 centerOffset = glm::vec2(static_cast<float>(minX), static_cast<float>(minY)) + piece.Center - body.Center;
			piece.Pos = ToWorld(body, centerOffset, body.Pos, body.Angle);
			piece.Angle = body.Angle;
			glm::vec2 arm = piece.Pos - body.Pos;
			float armLength = glm::length(arm);
			piece.Vel = body.Vel + body.Spin * glm::vec2(-arm.y, arm.x) + (armLength > 0.01F ? arm / armLength * burst : glm::vec2(0.0F));
			piece.Spin = body.Spin + (Random01() - 0.5F) * 0.03F;
			piece.Generation = body.Generation + 1;
			piece.BreakCooldown = 12;
			s_NewBodies.push_back(std::move(piece));
		}
		body.Done = true;
		// A burst of dust where it broke (visual only): a cloud where something brittle shatters or loose ground falls apart.
		int dust = 6 + body.PixelCount / 60;
		ThrowDust(hitPoint, std::min(mainStyle == c_Shatter || mainStyle == c_Crumble ? dust * 2 : dust, mainStyle == c_Shatter || mainStyle == c_Crumble ? 60 : 40), body.Materials, body.Colors);
	}

	/// Brings a body up to date after pixels have been shot, dug or blasted off it: its outline (what it collides with), mass and centre are worked out again
	/// from what's left, and if it has come apart, each part becomes a body of its own. Call while the body is lifted out of the terrain.
	void Rebuild(Body& body) {
		body.Damaged = false;
		body.Still = 0;
		// The connected parts of what's left (joined at corners counts, as it does for terrain).
		std::vector<int> label(body.Materials.size(), -1);
		std::vector<std::vector<int>> parts;
		std::vector<int> stack;
		for (int start = 0; start < static_cast<int>(label.size()); ++start) {
			if (!body.Materials[start] || label[start] >= 0) {
				continue;
			}
			int id = static_cast<int>(parts.size());
			parts.emplace_back();
			stack.clear();
			stack.push_back(start);
			label[start] = id;
			while (!stack.empty()) {
				int local = stack.back();
				stack.pop_back();
				parts[id].push_back(local);
				int x = local % body.W;
				int y = local / body.W;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						int nx = x + dx;
						int ny = y + dy;
						if (nx < 0 || ny < 0 || nx >= body.W || ny >= body.H) {
							continue;
						}
						int neighbour = ny * body.W + nx;
						if (body.Materials[neighbour] && label[neighbour] < 0) {
							label[neighbour] = id;
							stack.push_back(neighbour);
						}
					}
				}
			}
		}
		if (parts.size() <= 1) {
			// Still one piece: the same body, around its new centre of mass.
			glm::vec2 oldCenter = body.Center;
			glm::vec2 oldPos = body.Pos;
			if (!FinishBody(body, glm::vec2(0.0F))) {
				body.Done = true;
				return;
			}
			body.Pos = ToWorld(body, body.Center - oldCenter, oldPos, body.Angle);
			return;
		}
		for (const std::vector<int>& part: parts) {
			if (static_cast<int>(part.size()) < c_MinBodyPixels && CanThrow(part.size())) {
				for (int local: part) {
					glm::vec2 offset = glm::vec2(static_cast<float>(local % body.W) + 0.5F, static_cast<float>(local / body.W) + 0.5F) - body.Center;
					ThrowDebris(body.Materials[local], body.Colors[local], ToWorld(body, offset, body.Pos, body.Angle), body.Vel);
				}
				continue;
			}
			int minX = body.W;
			int minY = body.H;
			int maxX = 0;
			int maxY = 0;
			for (int local: part) {
				minX = std::min(minX, local % body.W);
				maxX = std::max(maxX, local % body.W);
				minY = std::min(minY, local / body.W);
				maxY = std::max(maxY, local / body.W);
			}
			Body piece;
			piece.W = maxX - minX + 1;
			piece.H = maxY - minY + 1;
			piece.Materials.assign(static_cast<size_t>(piece.W) * piece.H, 0);
			piece.Colors.assign(static_cast<size_t>(piece.W) * piece.H, 0);
			for (int local: part) {
				int index = (local / body.W - minY) * piece.W + (local % body.W - minX);
				piece.Materials[index] = body.Materials[local];
				piece.Colors[index] = body.Colors[local];
			}
			if (!FinishBody(piece, glm::vec2(0.0F))) {
				continue;
			}
			glm::vec2 centerOffset = glm::vec2(static_cast<float>(minX), static_cast<float>(minY)) + piece.Center - body.Center;
			piece.Pos = ToWorld(body, centerOffset, body.Pos, body.Angle);
			piece.Angle = body.Angle;
			glm::vec2 arm = piece.Pos - body.Pos;
			piece.Vel = body.Vel + body.Spin * glm::vec2(-arm.y, arm.x);
			piece.Spin = body.Spin;
			piece.Generation = body.Generation;
			piece.BreakCooldown = body.BreakCooldown;
			piece.Age = body.Age;
			s_NewBodies.push_back(std::move(piece));
		}
		body.Done = true;
	}

	/// Turns a point in the scene into a body's own bitmap.
	glm::vec2 ToLocal(const Body& body, const glm::vec2& point) {
		float c = std::cos(body.Angle);
		float s = std::sin(body.Angle);
		glm::vec2 offset = point - body.Pos;
		return glm::vec2(offset.x * c + offset.y * s + body.Center.x, -offset.x * s + offset.y * c + body.Center.y);
	}

	/// A long piece of metal (a beam, a girder, a plate) hit hard off its middle bends instead of breaking: it folds at a crease between the middle and where it hit,
	/// the part beyond the crease turned on with the way it was moving, by more the harder the hit and the thinner it is. The bent shape is drawn into a fresh bitmap
	/// and the piece is a rigid body again. Returns false, changing nothing, if it isn't long and thin enough to bend (it dents instead).
	/// Call while the body is lifted out of the terrain.
	bool BendAtCrease(Body& body, const glm::vec2& hitPoint, float violence) {
		// Its long axis and how long and thick it is, from the spread of its pixels.
		double xx = 0.0;
		double yy = 0.0;
		double xy = 0.0;
		for (int y = 0; y < body.H; ++y) {
			for (int x = 0; x < body.W; ++x) {
				if (body.Materials[static_cast<size_t>(y) * body.W + x]) {
					double dx = static_cast<double>(x) + 0.5 - body.Center.x;
					double dy = static_cast<double>(y) + 0.5 - body.Center.y;
					xx += dx * dx;
					yy += dy * dy;
					xy += dx * dy;
				}
			}
		}
		float grain = 0.5F * static_cast<float>(std::atan2(2.0 * xy, xx - yy));
		glm::vec2 along(std::cos(grain), std::sin(grain));
		glm::vec2 across(-along.y, along.x);
		double count = static_cast<double>(std::max(body.PixelCount, 1));
		float varAlong = static_cast<float>((xx * along.x * along.x + 2.0 * xy * along.x * along.y + yy * along.y * along.y) / count);
		float varAcross = static_cast<float>((xx * across.x * across.x + 2.0 * xy * across.x * across.y + yy * across.y * across.y) / count);
		float length = std::sqrt(12.0F * std::max(varAlong, 0.0F));
		float thickness = std::max(std::sqrt(12.0F * std::max(varAcross, 0.0F)), 1.0F);
		if (length < 20.0F || length < thickness * 3.0F) {
			return false;
		}
		// How far it folds: more for a harder hit and a thinner piece (stiffness grows with the square of the thickness), in steps of 5 degrees so the crease stays tidy.
		constexpr float c_Step = 0.0872665F;
		float angle = std::min(0.12F * (violence - 1.0F) + 0.1F, 0.6F) * std::clamp(36.0F / (thickness * thickness), 0.2F, 2.0F);
		angle = std::floor(std::min(angle, 0.6F) / c_Step) * c_Step;
		if (angle < c_Step) {
			return false;
		}
		glm::vec2 hitLocal = ToLocal(body, hitPoint);
		float hitAlong = glm::dot(hitLocal - body.Center, along);
		// The crease: between the middle and where it hit. What lies beyond it, away from the hit, is what turns.
		glm::vec2 crease = body.Center + along * (hitAlong * 0.3F);
		glm::vec2 far = hitAlong >= 0.0F ? -along : along;
		// Turned the way the piece was moving, as the end that hit stops and the rest carries on.
		float c = std::cos(body.Angle);
		float s = std::sin(body.Angle);
		glm::vec2 velLocal(body.Vel.x * c + body.Vel.y * s, -body.Vel.x * s + body.Vel.y * c);
		glm::vec2 arm = far * (length * 0.5F);
		if (glm::dot(glm::vec2(-arm.y, arm.x), velLocal) < 0.0F) {
			angle = -angle;
		}
		auto turn = [&](const glm::vec2& point, float cosine, float sine) {
			glm::vec2 from = point - crease;
			return crease + glm::vec2(from.x * cosine - from.y * sine, from.x * sine + from.y * cosine);
		};
		auto pixelAt = [&body](const glm::vec2& point) -> int {
			int x = static_cast<int>(std::floor(point.x));
			int y = static_cast<int>(std::floor(point.y));
			if (x < 0 || y < 0 || x >= body.W || y >= body.H) {
				return -1;
			}
			int local = y * body.W + x;
			return body.Materials[local] ? local : -1;
		};
		Body bent;
		glm::vec2 shift(0.0F);
		const BITMAP* materialBitmap = g_SceneMan.GetScene()->GetTerrain()->GetMaterialBitmap();
		auto tryFold = [&](float fold) {
			float ca = std::cos(fold);
			float sa = std::sin(fold);
			// The new bitmap's bounds: the near part where it was, the far part turned.
			float minX = 0.0F;
			float minY = 0.0F;
			float maxX = static_cast<float>(body.W);
			float maxY = static_cast<float>(body.H);
			for (int y = 0; y < body.H; ++y) {
				for (int x = 0; x < body.W; ++x) {
					glm::vec2 at(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F);
					if (body.Materials[static_cast<size_t>(y) * body.W + x] && glm::dot(at - crease, far) > 0.0F) {
						glm::vec2 moved = turn(at, ca, sa);
						minX = std::min(minX, moved.x - 1.0F);
						minY = std::min(minY, moved.y - 1.0F);
						maxX = std::max(maxX, moved.x + 1.0F);
						maxY = std::max(maxY, moved.y + 1.0F);
					}
				}
			}
			shift = glm::vec2(-std::floor(minX), -std::floor(minY));
			bent = Body();
			bent.W = static_cast<int>(std::ceil(maxX)) + static_cast<int>(shift.x);
			bent.H = static_cast<int>(std::ceil(maxY)) + static_cast<int>(shift.y);
			bent.Materials.assign(static_cast<size_t>(bent.W) * bent.H, 0);
			bent.Colors.assign(static_cast<size_t>(bent.W) * bent.H, 0);
			// The far part's side of the crease, turned with it.
			glm::vec2 farTurned = glm::vec2(far.x * ca - far.y * sa, far.x * sa + far.y * ca);
			for (int y = 0; y < bent.H; ++y) {
				for (int x = 0; x < bent.W; ++x) {
					glm::vec2 at = glm::vec2(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F) - shift;
					int source = -1;
					if (glm::dot(at - crease, far) <= 0.0F && glm::dot(at - crease, farTurned) <= 0.0F) {
						// The near part, where it was.
						source = pixelAt(at);
					} else if (glm::dot(at - crease, farTurned) > 0.0F) {
						// The far part, turned back to where it came from.
						glm::vec2 from = turn(at, ca, -sa);
						if (glm::dot(from - crease, far) > 0.0F) {
							source = pixelAt(from);
						}
					} else {
						// The wedge opened on the outside of the bend: filled from the crease itself, so the metal stretches rather than tears.
						glm::vec2 onCrease = at - far * glm::dot(at - crease, far);
						source = pixelAt(onCrease - far * 0.5F);
					}
					if (source >= 0) {
						size_t index = static_cast<size_t>(y) * bent.W + x;
						bent.Materials[index] = body.Materials[source];
						bent.Colors[index] = body.Colors[source];
					}
				}
			}
			if (!FinishBody(bent, glm::vec2(0.0F))) {
				return false;
			}
			// Not into the ground: a fold that would push it into solid ground is too much (a beam lying flat can't fold down through the floor).
			glm::vec2 pos = ToWorld(body, bent.Center - shift - body.Center, body.Pos, body.Angle);
			int buried = 0;
			for (int y = 0; y < bent.H; ++y) {
				for (int x = 0; x < bent.W; ++x) {
					if (bent.Materials[static_cast<size_t>(y) * bent.W + x]) {
						glm::vec2 world = ToWorld(body, glm::vec2(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F) - bent.Center, pos, body.Angle);
						buried += SolidAt(materialBitmap, static_cast<int>(std::floor(world.x)), static_cast<int>(std::floor(world.y))) ? 1 : 0;
					}
				}
			}
			return buried <= std::max(2, bent.PixelCount / 100);
		};
		// The fold the hit asks for, or less if that would go into the ground.
		bool folded = false;
		for (float fold = angle; std::abs(fold) >= c_Step * 0.99F && !folded; fold -= (angle > 0.0F ? c_Step : -c_Step)) {
			folded = tryFold(fold);
		}
		if (!folded) {
			return false;
		}
		// Where it is in the scene: its bitmap moved by the shift, the same angle.
		bent.Pos = ToWorld(body, bent.Center - shift - body.Center, body.Pos, body.Angle);
		bent.Angle = body.Angle;
		// Bending takes up some of the blow.
		bent.Vel = body.Vel * 0.75F;
		bent.Spin = body.Spin * 0.75F;
		bent.Generation = body.Generation + 1;
		bent.BreakCooldown = 20;
		bent.Age = body.Age;
		bent.Wet = body.Wet;
		bent.Hurt = std::move(body.Hurt);
		body = std::move(bent);
		// A clank of dust and sparks of its colour where it hit (visual only).
		ThrowDust(hitPoint, 6, body.Materials, body.Colors);
		return true;
	}

	/// Metal (Bend) hit harder than it takes doesn't break: it dents where it hit, a shallow round bite pushed in from its surface, deeper the harder the hit.
	/// Call while the body is lifted out of the terrain. It may come apart if the dent goes right through a thin part.
	void Dent(Body& body, const glm::vec2& hitPoint, float violence) {
		glm::vec2 hitLocal = ToLocal(body, hitPoint);
		glm::vec2 inward = body.Center - hitLocal;
		float length = glm::length(inward);
		inward = length > 0.01F ? inward / length : glm::vec2(0.0F, 1.0F);
		float depth = std::clamp(violence, 1.0F, 4.0F);
		float radius = 3.0F + depth * 1.5F;
		glm::vec2 bite = hitLocal - inward * (radius - depth);
		int removed = 0;
		for (int y = std::max(0, static_cast<int>(bite.y - radius)); y <= std::min(body.H - 1, static_cast<int>(bite.y + radius)); ++y) {
			for (int x = std::max(0, static_cast<int>(bite.x - radius)); x <= std::min(body.W - 1, static_cast<int>(bite.x + radius)); ++x) {
				int local = y * body.W + x;
				if (body.Materials[local] && glm::length(glm::vec2(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F) - bite) < radius) {
					body.Materials[local] = 0;
					++removed;
				}
			}
		}
		++body.Generation;
		body.BreakCooldown = 20;
		if (removed > 0) {
			// A clank of dust and sparks of its colour (visual only).
			ThrowDust(hitPoint, std::min(3 + removed / 4, 12), body.Materials, body.Colors);
			Rebuild(body);
		}
	}

	/// A piece with leaves (or grass) on it that lands hard sheds some: the harder the landing, the more, nearest where it hit first. The piece itself holds together.
	/// Call while the body is lifted out of the terrain. Returns true if any came off.
	bool ShedLeaves(Body& body, const glm::vec2& hitPoint, float hit, float breakSpeed) {
		if (body.CarryingPixels >= body.PixelCount) {
			return false;
		}
		// From a third of the way to breaking, up to most of them at the point of breaking.
		float share = std::clamp((hit / std::max(breakSpeed, 0.1F) - 0.33F) * 0.9F, 0.0F, 0.6F);
		if (share <= 0.0F) {
			return false;
		}
		glm::vec2 hitLocal = ToLocal(body, hitPoint);
		float reach = std::max(body.Radius, 1.0F);
		int shed = 0;
		for (int local = 0; local < static_cast<int>(body.Materials.size()) && s_DebrisThisUpdate < c_MaxDebrisPerUpdate; ++local) {
			int material = body.Materials[local];
			if (!material || !s_Flimsy[material]) {
				continue;
			}
			glm::vec2 at(static_cast<float>(local % body.W) + 0.5F, static_cast<float>(local / body.W) + 0.5F);
			float closeness = 1.0F - std::min(glm::length(at - hitLocal) / reach, 1.0F);
			if (Random01() < share * (0.3F + closeness)) {
				glm::vec2 world = ToWorld(body, at - body.Center, body.Pos, body.Angle);
				ThrowDebris(material, body.Colors[local], world, body.Vel * 0.5F + glm::vec2(Random01() - 0.5F, -Random01()) * 1.2F);
				body.Materials[local] = 0;
				++shed;
			}
		}
		if (shed == 0) {
			return false;
		}
		// Leaves left hanging on nothing but other loose leaves go too (shredded, or thrown while there's room), so the tree stays one piece rather than
		// dropping a scatter of leafy scraps.
		std::vector<unsigned char> held(body.Materials.size(), 0);
		std::vector<int> stack;
		for (int local = 0; local < static_cast<int>(body.Materials.size()); ++local) {
			if (body.Materials[local] && !s_Flimsy[body.Materials[local]]) {
				held[local] = 1;
				stack.push_back(local);
			}
		}
		while (!stack.empty()) {
			int local = stack.back();
			stack.pop_back();
			int x = local % body.W;
			int y = local / body.W;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					int nx = x + dx;
					int ny = y + dy;
					if (nx < 0 || ny < 0 || nx >= body.W || ny >= body.H) {
						continue;
					}
					int neighbour = ny * body.W + nx;
					if (body.Materials[neighbour] && !held[neighbour]) {
						held[neighbour] = 1;
						stack.push_back(neighbour);
					}
				}
			}
		}
		for (int local = 0; local < static_cast<int>(body.Materials.size()); ++local) {
			if (body.Materials[local] && !held[local]) {
				if (CanThrow(1)) {
					glm::vec2 at(static_cast<float>(local % body.W) + 0.5F, static_cast<float>(local / body.W) + 0.5F);
					ThrowDebris(body.Materials[local], body.Colors[local], ToWorld(body, at - body.Center, body.Pos, body.Angle), body.Vel * 0.5F + glm::vec2(Random01() - 0.5F, -Random01()) * 1.2F);
				}
				body.Materials[local] = 0;
			}
		}
		Rebuild(body);
		return true;
	}

	/// A body has stopped: it stays in the terrain as ordinary ground.
	void Settle(SLTerrain* terrain, Body& body) {
		for (const auto& [key, local]: body.Stamped) {
			s_State[key] &= static_cast<unsigned char>(~c_Falling);
		}
		if (body.Stamped.size() >= 10 && body.Stamped.size() <= 8000) {
			Rested rested;
			rested.Keys.reserve(body.Stamped.size());
			for (const auto& [key, local]: body.Stamped) {
				rested.Keys.push_back(key);
			}
			rested.Center = body.Pos;
			rested.Radius = body.Radius;
			rested.When = g_TimerMan.GetSimUpdateCount();
			if (s_Rested.size() >= c_MaxRested) {
				s_Rested.erase(s_Rested.begin());
			}
			s_Rested.push_back(std::move(rested));
		}
		float reach = body.Radius + 2.0F;
		terrain->AddUpdatedMaterialArea(Box(Vector(body.Pos.x - reach, body.Pos.y - reach - 64.0F), reach * 2.0F, reach * 2.0F + 64.0F));
		FluidSim::Disturb(Vector(body.Pos.x, body.Pos.y), reach + 6.0F);
		body.Stamped.clear();
		body.Done = true;
	}

	/// Moves a body for one update.
	void StepBody(SLTerrain* terrain, Body& body) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		Unstamp(terrain, body);
		++body.Age;
		if (body.BreakCooldown > 0) {
			--body.BreakCooldown;
		}
		if (body.PixelCount < c_MinBodyPixels && CanThrow(static_cast<size_t>(body.PixelCount))) {
			Crumble(body);
			return;
		}
		if (body.Damaged) {
			Rebuild(body);
			if (body.Done) {
				return;
			}
		}
		if (body.Pos.y - body.Radius > static_cast<float>(s_Height)) {
			// Fell out of the bottom of the world.
			body.Done = true;
			return;
		}
		body.Vel.y += c_Gravity;
		// In liquid it sinks slowly instead of dropping.
		int inLiquid = 0;
		int touchedX = -1;
		int touchedY = -1;
		for (size_t i = 0; i < body.Outline.size(); i += 4) {
			glm::vec2 at = ToWorld(body, body.Outline[i], body.Pos, body.Angle);
			int x = static_cast<int>(std::floor(at.x));
			int y = static_cast<int>(std::floor(at.y));
			if (WrapInWorld(x, y) && FluidSim::IsLiquid(materialBitmap->line[y][x])) {
				if (inLiquid == 0) {
					touchedX = x;
					touchedY = y;
				}
				++inLiquid;
			}
		}
		if (inLiquid > 0 && !body.Wet && glm::length(body.Vel) > 1.0F) {
			// Going in: a splash for the eye only, from the surface where it went in. (It threw real liquid before, which came down on the piece
			// and stayed there; what the piece pushes aside goes into the level instead, see Stamp.)
			int surfaceX = static_cast<int>(std::floor(body.Pos.x));
			int surfaceY = -1;
			for (int y = std::max(0, static_cast<int>(body.Pos.y - body.Radius) - 4); y <= std::min(s_Height - 1, static_cast<int>(body.Pos.y + body.Radius) + 4); ++y) {
				int x = surfaceX;
				if (WrapInWorld(x, y) && FluidSim::IsLiquid(materialBitmap->line[y][x])) {
					surfaceX = x;
					surfaceY = y;
					break;
				}
			}
			if (surfaceY < 0) {
				// No liquid straight under the middle (the piece only clipped some at its edge): splash from the liquid it touched. The colour is
				// always a liquid pixel's, never air's: air's is the mask colour, which showed as a spray of magenta drops.
				surfaceX = touchedX;
				surfaceY = touchedY;
			}
			int colorIndex = terrain->GetFGColorPixel(surfaceX, surfaceY);
			// (Pixels an update into metres a second: 60 updates a second, 20 pixels to the metre.)
			FluidSim::VisualSplash(Vector(static_cast<float>(surfaceX), static_cast<float>(surfaceY)), body.Radius * 2.0F, glm::length(body.Vel) * 3.0F, colorIndex);
		}
		body.Wet = inLiquid > 0;
		if (inLiquid > 0) {
			float fraction = std::min(1.0F, static_cast<float>(inLiquid) * 4.0F / static_cast<float>(body.Outline.size()));
			// Heavy pieces plunge; light ones (wood, soil) are slowed more. Stone is about 2.5 a pixel, concrete and metal more.
			float drag = std::clamp(0.1F / std::max(body.Mass / static_cast<float>(std::max(body.PixelCount, 1)), 0.5F), 0.02F, 0.09F);
			body.Vel *= 1.0F - drag * fraction;
			body.Spin *= 1.0F - 0.05F * fraction;
		}
		float speed = glm::length(body.Vel);
		if (speed > c_MaxSpeed) {
			body.Vel *= c_MaxSpeed / speed;
		}
		body.Spin = std::clamp(body.Spin, -0.25F, 0.25F);

		glm::vec2 startPos = body.Pos;
		float startAngle = body.Angle;
		int subSteps = std::clamp(static_cast<int>(std::ceil(std::max(glm::length(body.Vel), std::abs(body.Spin) * body.Radius))), 1, 14);
		float hardestHit = 0.0F;
		glm::vec2 hardestPoint(0.0F);
		int responses = 0;
		static std::vector<glm::vec2> contacts;
		for (int subStep = 0; subStep < subSteps; ++subStep) {
			glm::vec2 tryPos = body.Pos + body.Vel / static_cast<float>(subSteps);
			float tryAngle = body.Angle + body.Spin / static_cast<float>(subSteps);
			contacts.clear();
			for (const glm::vec2& point: body.Outline) {
				glm::vec2 at = ToWorld(body, point, tryPos, tryAngle);
				if (SolidAt(materialBitmap, static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)))) {
					contacts.push_back(at);
				}
			}
			if (contacts.empty()) {
				body.Pos = tryPos;
				body.Angle = tryAngle;
				continue;
			}
			// Ice over water doesn't hold a piece that comes down on it at any speed: it breaks back into water where it's hit, and the piece goes on in.
			if (s_IceMaterial && s_WaterMaterial && glm::length(body.Vel) > 1.0F) {
				int broken = 0;
				for (const glm::vec2& contact: contacts) {
					int cx = static_cast<int>(std::floor(contact.x));
					int cy = static_cast<int>(std::floor(contact.y));
					for (int dy = -2; dy <= 2; ++dy) {
						for (int dx = -2; dx <= 2; ++dx) {
							int x = cx + dx;
							int y = cy + dy;
							if (WrapInWorld(x, y) && materialBitmap->line[y][x] == s_IceMaterial) {
								terrain->SetMaterialPixel(x, y, s_WaterMaterial);
								terrain->SetFGColorPixel(x, y, s_WaterColor);
								++broken;
							}
						}
					}
				}
				if (broken > 0) {
					FluidSim::Disturb(Vector(body.Pos.x, body.Pos.y), body.Radius + 6.0F);
					body.Vel *= 0.92F;
					continue;
				}
			}
			// Loose scraps in the way (a few leftover pixels of wall, a nugget, some grains) don't hold a big piece up: it flattens them and carries on.
			if (int limit = std::min(TerrainCollapse::GetTuning().CrushPixels, body.PixelCount / 4); limit > 0) {
				static std::vector<int> scrap;
				int crushed = 0;
				for (const glm::vec2& contact: contacts) {
					int startX = static_cast<int>(std::floor(contact.x));
					int startY = static_cast<int>(std::floor(contact.y));
					if (!WrapInWorld(startX, startY) || !SolidAt(materialBitmap, startX, startY)) {
						continue;
					}
					// The connected bit of ground this point is part of, as far as the limit: any more than that and it's no scrap.
					scrap.clear();
					scrap.push_back(startY * s_Width + startX);
					bool small = true;
					bool fixed = false;
					for (size_t next = 0; next < scrap.size() && small; ++next) {
						int x = scrap[next] % s_Width;
						int y = scrap[next] / s_Width;
						fixed = fixed || s_Fixed[materialBitmap->line[y][x]];
						for (int dy = -1; dy <= 1 && small; ++dy) {
							for (int dx = -1; dx <= 1; ++dx) {
								int nx = x + dx;
								int ny = y + dy;
								if ((dx == 0 && dy == 0) || !SolidAt(materialBitmap, nx, ny)) {
									continue;
								}
								if (!WrapInWorld(nx, ny)) {
									// Joined to the bottom of the world.
									small = false;
									break;
								}
								int key = ny * s_Width + nx;
								if (std::find(scrap.begin(), scrap.end(), key) == scrap.end()) {
									scrap.push_back(key);
									if (static_cast<int>(scrap.size()) > limit) {
										small = false;
										break;
									}
								}
							}
						}
					}
					if (!small && !fixed && limit >= 9) {
						// Joined to something bigger, but only a sliver of it here (a stub of wall a pixel or two thick, the tip of a spike): that goes too, just where it's touched.
						int around = 0;
						for (int dy = -4; dy <= 4; ++dy) {
							for (int dx = -4; dx <= 4; ++dx) {
								around += SolidAt(materialBitmap, startX + dx, startY + dy) ? 1 : 0;
							}
						}
						if (around <= 14) {
							scrap.clear();
							for (int dy = -1; dy <= 1; ++dy) {
								for (int dx = -1; dx <= 1; ++dx) {
									int x = startX + dx;
									int y = startY + dy;
									if (SolidAt(materialBitmap, x, y) && WrapInWorld(x, y) && !s_Fixed[materialBitmap->line[y][x]]) {
										scrap.push_back(y * s_Width + x);
									}
								}
							}
							small = !scrap.empty();
						}
					}
					if (!small || fixed) {
						continue;
					}
					for (int key: scrap) {
						int x = key % s_Width;
						int y = key / s_Width;
						ThrowDebris(materialBitmap->line[y][x], terrain->GetFGColorPixel(x, y), glm::vec2(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F), body.Vel * 0.5F + glm::vec2(Random01() - 0.5F, -Random01()));
						terrain->SetMaterialPixel(x, y, g_MaterialAir);
						terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
						s_State[key] &= static_cast<unsigned char>(~c_Falling);
					}
					crushed += static_cast<int>(scrap.size());
				}
				if (crushed > 0) {
					// It costs the piece a little of its speed, by how much it crushed for its size.
					body.Vel *= 1.0F - std::min(0.3F, static_cast<float>(crushed) / static_cast<float>(body.PixelCount));
					continue;
				}
			}
			if (++responses > 6) {
				break;
			}
			// Impulses at the points of contact, a few rounds so several points share the load (as rigid body engines do).
			size_t stride = std::max<size_t>(1, contacts.size() / 12);
			bool pushed = false;
			glm::vec2 normalSum(0.0F);
			for (int round = 0; round < 4; ++round) {
				for (size_t i = 0; i < contacts.size(); i += stride) {
					glm::vec2 arm = contacts[i] - body.Pos;
					glm::vec2 pointVel = body.Vel + body.Spin * glm::vec2(-arm.y, arm.x);
					glm::vec2 normal = SurfaceNormal(materialBitmap, contacts[i], -pointVel);
					if (round == 0) {
						normalSum += normal;
					}
					float approach = glm::dot(pointVel, normal);
					if (approach >= 0.0F) {
						continue;
					}
					if (-approach > hardestHit) {
						hardestHit = -approach;
						hardestPoint = contacts[i];
					}
					float armCrossNormal = Cross(arm, normal);
					float push = -(1.0F + (approach < -1.5F ? c_Bounce : 0.0F)) * approach / (1.0F / body.Mass + armCrossNormal * armCrossNormal / body.Inertia);
					body.Vel += normal * (push / body.Mass);
					body.Spin += armCrossNormal * push / body.Inertia;
					// Friction, along the surface, no stronger than the push allows.
					glm::vec2 along(-normal.y, normal.x);
					pointVel = body.Vel + body.Spin * glm::vec2(-arm.y, arm.x);
					float armCrossAlong = Cross(arm, along);
					float drag = std::clamp(-glm::dot(pointVel, along) / (1.0F / body.Mass + armCrossAlong * armCrossAlong / body.Inertia), -c_Friction * std::abs(push), c_Friction * std::abs(push));
					body.Vel += along * (drag / body.Mass);
					body.Spin += armCrossAlong * drag / body.Inertia;
					pushed = true;
				}
			}
			// Rolling and sliding lose a little each touch.
			body.Spin *= 0.985F;
			body.Vel *= 0.995F;
			// The impulses set its speed right, but where it was going still has a corner in the ground: a body turns about its middle, so a piece
			// rolling on a corner dips that corner in. Take the move anyway and lift it out along the surface (what rigid body engines call position
			// correction). Without this a piece stopped dead at its first touch. A piece that is all but still is left alone, so it doesn't creep.
			bool lively = glm::length(body.Vel) > 0.12F || std::abs(body.Spin) * body.Radius > 0.12F;
			float length = glm::length(normalSum);
			if ((lively || !pushed) && length > 0.01F) {
				glm::vec2 out = normalSum / length;
				for (float lift: {0.5F, 1.0F, 1.5F, 2.0F, 3.0F}) {
					glm::vec2 lifted = tryPos + out * lift;
					bool free = true;
					for (const glm::vec2& point: body.Outline) {
						glm::vec2 at = ToWorld(body, point, lifted, tryAngle);
						if (SolidAt(materialBitmap, static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)))) {
							free = false;
							break;
						}
					}
					if (free) {
						body.Pos = lifted;
						body.Angle = tryAngle;
						break;
					}
				}
			}
		}

		// Units in the way are hit, and slow it a little.
		const TerrainCollapse::Tuning& tuning = TerrainCollapse::GetTuning();
		float secondsPerUpdate = std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F);
		float metersPerSecond = c_MPP / secondsPerUpdate; //!< Pixels per update to m/s.
		// (Slower than this nothing is hit; lower when hits are set to hurt at lower speeds, so those hits are found.)
		float hitSpeed = std::clamp(tuning.HitDamage > 0.0F ? tuning.HitMinSpeed / metersPerSecond : 1.2F, 0.4F, 1.2F);
		if (glm::length(body.Vel) > hitSpeed) {
			long long now = g_TimerMan.GetSimUpdateCount();
			constexpr long long c_HurtCooldown = 20; //!< Updates before the same piece can hurt the same unit again.
			body.Hurt.erase(std::remove_if(body.Hurt.begin(), body.Hurt.end(), [now](const std::pair<long, long long>& hurt) { return now - hurt.second >= c_HurtCooldown; }), body.Hurt.end());
			MovableObject* struck[4] = {};
			int struckCount = 0;
			for (size_t i = 0; i < body.Outline.size() && struckCount < 4; i += 3) {
				glm::vec2 at = ToWorld(body, body.Outline[i], body.Pos, body.Angle);
				MOID id = g_SceneMan.GetMOIDPixel(static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)));
				if (id == g_NoMOID) {
					continue;
				}
				MovableObject* object = g_MovableMan.GetMOFromID(id);
				object = object ? object->GetRootParent() : nullptr;
				// Everything under it is hit: units, and loose objects too (dropped weapons, wreckage, unexploded bombs), which burst apart when hit this hard.
				if (!object || std::find(struck, struck + struckCount, object) != struck + struckCount) {
					continue;
				}
				struck[struckCount++] = object;
				float objectMass = std::max(object->GetMass(), 1.0F);
				// Hurt by how fast the piece closes on the unit above the slowest speed that hurts, and by how heavy it is for the unit (up to the cap),
				// as a share of the unit's full health so big and small units alike take the same share from the same blow.
				// A tree coming down goes through units and vehicles as a standing one does, while they don't bump into trees.
				if (object->IsActor() && !TerrainTrees::UnitsCollide() && IsTree(body)) {
					continue;
				}
				if (Actor* actor = dynamic_cast<Actor*>(object); actor && tuning.HitDamage > 0.0F && body.PixelCount >= tuning.HitMinPixels && !actor->IsDead()) {
					long id = actor->GetUniqueID();
					bool hurtLately = std::any_of(body.Hurt.begin(), body.Hurt.end(), [id](const std::pair<long, long long>& hurt) { return hurt.first == id; });
					float speed = glm::length(body.Vel);
					glm::vec2 actorVel(actor->GetVel().GetX() / metersPerSecond, actor->GetVel().GetY() / metersPerSecond);
					float closing = speed > 0.0F ? glm::dot(body.Vel - actorVel, body.Vel / speed) * metersPerSecond : 0.0F;
					if (!hurtLately && closing > tuning.HitMinSpeed) {
						float heft = std::min(body.Mass / objectMass, std::max(tuning.HitMassCap, 0.0F));
						float damage = (closing - tuning.HitMinSpeed) * heft * 0.06F * tuning.HitDamage * actor->GetMaxHealth();
						if (damage > 0.0F) {
							actor->AddHealth(-damage);
							body.Hurt.emplace_back(id, now);
						}
					}
				}
				float weight = std::min(body.Mass, objectMass * 2.0F + 20.0F);
				if (tuning.HitKnockback > 0.0F) {
					object->AddAbsImpulseForce(Vector(body.Vel.x, body.Vel.y) * (3.0F * weight * tuning.HitKnockback), Vector(at.x, at.y));
				}
				body.Vel *= 1.0F - std::min(0.3F, objectMass / (objectMass + body.Mass));
			}
		}

		// Hit harder than its material can take: it cracks.
		float breakSpeed = BreakSpeed(body);
		// A thud of dust where it lands (visual only).
		if (hardestHit > 1.2F) {
			ThrowDust(hardestPoint, std::min(static_cast<int>((3.0F + static_cast<float>(body.PixelCount) / 120.0F) * hardestHit * 0.5F), 30), body.Materials, body.Colors);
		}
		// A tree that lands hard loses leaves, whether or not its trunk breaks.
		if (body.BreakCooldown == 0 && ShedLeaves(body, hardestPoint, hardestHit, breakSpeed)) {
			body.BreakCooldown = 6;
			if (body.Done) {
				return;
			}
		}
		if (hardestHit > breakSpeed && body.BreakCooldown == 0 && body.Generation < c_MaxGeneration && body.PixelCount >= c_MinBreakPixels) {
			// Metal through and through dents; anything else breaks, each material its own way.
			if (body.StyleStrength[c_Bend] > 0.0F && body.StyleStrength[c_Bend] >= 0.999F * (body.StyleStrength[c_Shatter] + body.StyleStrength[c_Crack] + body.StyleStrength[c_Crumble] + body.StyleStrength[c_Splinter] + body.StyleStrength[c_Bend])) {
				// A chunky piece only dents when hit well over the threshold.
				if (!BendAtCrease(body, hardestPoint, hardestHit / breakSpeed) && hardestHit > breakSpeed * 1.5F) {
					Dent(body, hardestPoint, hardestHit / breakSpeed);
				}
				if (body.Done) {
					return;
				}
			} else {
				Break(body, hardestPoint, hardestHit / breakSpeed);
				return;
			}
		}

		float movedBy = glm::length(body.Pos - startPos) + std::abs(body.Angle - startAngle) * body.Radius;
		// Lying on something and all but stopped. (How far it moved doesn't count: a piece rocking on the pixel grid is lifted clear and drops back for ever.)
		bool calm = responses > 0 && glm::length(body.Vel) < 0.35F && std::abs(body.Spin) * body.Radius < 0.35F;
		// A wobble sets the count back a little rather than to nothing, or a piece rocking on a point never comes to rest.
		body.Still = calm ? body.Still + 1 : (movedBy > 2.0F ? 0 : std::max(body.Still - 4, 0));
		Stamp(terrain, body);
		// A piece that has lain still long enough becomes ordinary ground again. Until then it can still tip, roll or be knocked.
		if (body.Still >= std::max(static_cast<int>(TerrainCollapse::GetTuning().RestSeconds * 60.0F), 5) || body.Age > c_MaxAge) {
			Settle(terrain, body);
		}
	}

	void UpdateBodies(SLTerrain* terrain) {
		s_DebrisThisUpdate = 0;
		for (Body& body: s_Bodies) {
			if (!body.Done) {
				StepBody(terrain, body);
			}
		}
		// Pieces that broke off this update take their place in the terrain and move from the next.
		for (Body& piece: s_NewBodies) {
			Stamp(terrain, piece);
			s_Bodies.push_back(std::move(piece));
		}
		s_NewBodies.clear();
		s_Bodies.erase(std::remove_if(s_Bodies.begin(), s_Bodies.end(), [](const Body& body) { return body.Done; }), s_Bodies.end());
	}

	/// Whether two touching pixels' materials make one piece. With tree rules, leaves join only leaves and tree trunk: a tree hangs on its own trunk,
	/// so one whose trunk is cut falls with its leaves, even where they brush the ground or a wall.
	bool Joins(int material, int other, bool treeRules) {
		if (!treeRules || s_Leaves[material] == s_Leaves[other]) {
			return true;
		}
		return s_TreeTrunk[s_Leaves[material] ? other : material];
	}

	/// Whether a pixel can start a search for loose pieces. The search is done twice: first from everything but leaves, with tree rules, which finds
	/// trees as pieces (trunk and leaves); then from the leaves not yet reached, without, so plants and leaves on no trunk hold on by anything they touch.
	bool StartsSearch(int material, bool treeRules) {
		return material != g_MaterialAir && !FluidSim::IsLiquid(material) && !s_NoHold[material] && !(treeRules && s_Leaves[material]);
	}

	/// Flood fills the solid piece containing a pixel. Returns true if it's a floating piece that should fall, filling its pixels.
	bool FindFloatingPiece(const BITMAP* materialBitmap, int startKey, int width, int height, bool wrapX, std::vector<int>& piece, bool keepFittings = true, bool treeRules = false) {
		piece.clear();
		s_Stack.clear();
		s_FillSeen.clear();
		s_Stack.push_back(startKey);
		s_FillSeen.push_back(startKey);
		s_State[startKey] |= c_Seen;
		bool floating = true;
		int structurePixels = 0;
		while (!s_Stack.empty() && floating) {
			int key = s_Stack.back();
			s_Stack.pop_back();
			piece.push_back(key);
			int x = key % width;
			int y = key / width;
			int material = materialBitmap->line[y][x];
			if (static_cast<int>(piece.size()) > TerrainCollapse::GetTuning().MaxPiecePixels || s_Fixed[material]) {
				floating = false;
				break;
			}
			if (s_Structure[material]) {
				++structurePixels;
			}
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					if (dx == 0 && dy == 0) {
						continue;
					}
					int nx = x + dx;
					int ny = y + dy;
					if (wrapX) {
						nx = (nx + width) % width;
					}
					if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
						// Touching the edge of the world counts as being held up by it.
						floating = false;
						continue;
					}
					int neighbour = ny * width + nx;
					unsigned char state = s_State[neighbour];
					int neighbourMaterial = materialBitmap->line[ny][nx];
					// Liquid, ash and charcoal hold nothing up.
					if (neighbourMaterial == g_MaterialAir || FluidSim::IsLiquid(neighbourMaterial) || s_NoHold[neighbourMaterial] || !Joins(material, neighbourMaterial, treeRules)) {
						continue;
					}
					if (state & c_Supported) {
						// Joined to a piece already found to be held up.
						floating = false;
						continue;
					}
					// A piece that's already falling isn't support either.
					if (state & (c_Seen | c_Falling)) {
						continue;
					}
					s_State[neighbour] |= c_Seen;
					s_FillSeen.push_back(neighbour);
					s_Stack.push_back(neighbour);
				}
			}
		}
		s_Touched.insert(s_Touched.end(), s_FillSeen.begin(), s_FillSeen.end());
		// A small loose bit that's mostly building material is a fitting drawn in mid-air, not rubble.
		// (Only when it isn't known what was hanging before the blast. When it is, fittings are among the things that were, and stay for that reason, while scraps the blast made fall.)
		if (keepFittings && floating && structurePixels * 2 > static_cast<int>(piece.size()) && static_cast<int>(piece.size()) < TerrainCollapse::GetTuning().MinFittingPixels) {
			floating = false;
		}
		// With buildings set to stay up, anything with building material in it holds, and holds up the ground joined to it.
		if (floating && structurePixels > 0 && !TerrainCollapse::BuildingsFall()) {
			floating = false;
		}
		if (!floating) {
			// Everything reached is held up; later fills that touch it stop at once.
			for (int key: s_FillSeen) {
				s_State[key] |= c_Supported;
			}
			return false;
		}
		return true;
	}

	/// What share of some pixels are in a sorted list of pixels. Every third pixel is tried, which is plenty to tell most from few.
	float ShareIn(const std::vector<int>& pixels, const std::vector<int>& sortedList) {
		int tried = 0;
		int found = 0;
		for (size_t i = 0; i < pixels.size(); i += 3) {
			++tried;
			found += std::binary_search(sortedList.begin(), sortedList.end(), pixels[i]) ? 1 : 0;
		}
		return tried > 0 ? static_cast<float>(found) / static_cast<float>(tried) : 0.0F;
	}

	/// A piece of ground held on by only a thin neck.
	struct HangingPiece {
		std::vector<int> Core; //!< The pixels of its middle (what's left of it when its edges are pared away), sorted.
		std::vector<int> Cut; //!< The pixels across the neck that have to go for it to come free.
	};

	/// Finds pieces of ground around a point that are joined to the rest by a neck no wider than the tuning allows.
	/// How: pare the edges off all the ground in a window around the point, by half the neck width. Thin necks vanish; what's left in separate lumps that don't reach
	/// the window's edge are the middles of hanging pieces. Each is grown back out to its own edge, and the ground touching it beyond that is its neck.
	void FindHangingPieces(const BITMAP* materialBitmap, const Check& check, std::vector<HangingPiece>& found) {
		found.clear();
		int neckWidth = TerrainCollapse::GetTuning().NeckWidth;
		if (neckWidth <= 0) {
			return;
		}
		int pare = (neckWidth + 1) / 2;
		// A window well beyond the blast, since the piece hanging by the neck has to fit inside it to be recognised.
		int reach = std::min(check.Radius + 60, 150);
		int side = reach * 2 + 1;
		int left = check.X - reach;
		int top = check.Y - reach;
		auto solidAt = [&](int x, int y) {
			if (y >= s_Height) {
				return true;
			}
			if (!WrapInWorld(x, y)) {
				return false;
			}
			int material = materialBitmap->line[y][x];
			return material != g_MaterialAir && !s_Flimsy[material] && !FluidSim::IsLiquid(material) && !(s_State[y * s_Width + x] & c_Falling);
		};
		static std::vector<unsigned char> solid;
		static std::vector<unsigned char> pared;
		static std::vector<unsigned char> pareBy; //!< How far each pixel is pared, by its material's own neck width (MaterialBehaviour::NeckWidth): 0 for wood, which holds by a sliver.
		static std::vector<int> owner;
		static std::vector<int> depth;
		static std::vector<int> queue;
		size_t cells = static_cast<size_t>(side) * side;
		solid.assign(cells, 0);
		pared.assign(cells, 0);
		pareBy.assign(cells, static_cast<unsigned char>(pare));
		owner.assign(cells, -1);
		for (int wy = 0; wy < side; ++wy) {
			for (int wx = 0; wx < side; ++wx) {
				size_t cell = static_cast<size_t>(wy) * side + wx;
				solid[cell] = solidAt(left + wx, top + wy) ? 1 : 0;
				if (int x = left + wx, y = top + wy; solid[cell] && WrapInWorld(x, y) && y < s_Height) {
					if (int neck = s_Neck[materialBitmap->line[y][x]]; neck >= 0 && neck < neckWidth) {
						pareBy[cell] = static_cast<unsigned char>((neck + 1) / 2);
					}
				}
			}
		}
		// Rows of solid first, then columns of those: a pixel is kept if everything within the paring distance of it is solid. Beyond the window counts as solid.
		static std::vector<unsigned char> rows;
		rows.assign(cells, 0);
		for (int wy = 0; wy < side; ++wy) {
			for (int wx = 0; wx < side; ++wx) {
				bool all = true;
				int by = pareBy[static_cast<size_t>(wy) * side + wx];
				for (int d = -by; d <= by && all; ++d) {
					int x = wx + d;
					all = x < 0 || x >= side || solid[static_cast<size_t>(wy) * side + x];
				}
				rows[static_cast<size_t>(wy) * side + wx] = all ? 1 : 0;
			}
		}
		for (int wy = 0; wy < side; ++wy) {
			for (int wx = 0; wx < side; ++wx) {
				bool all = true;
				int by = pareBy[static_cast<size_t>(wy) * side + wx];
				for (int d = -by; d <= by && all; ++d) {
					int y = wy + d;
					all = y < 0 || y >= side || rows[static_cast<size_t>(y) * side + wx];
				}
				pared[static_cast<size_t>(wy) * side + wx] = all ? 1 : 0;
			}
		}
		static constexpr int steps4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		int lumps = 0;
		for (int start = 0; start < static_cast<int>(cells); ++start) {
			if (!pared[start] || owner[start] >= 0) {
				continue;
			}
			int lump = lumps++;
			queue.clear();
			queue.push_back(start);
			owner[start] = lump;
			bool reachesEdge = false;
			for (size_t next = 0; next < queue.size(); ++next) {
				int cell = queue[next];
				int wx = cell % side;
				int wy = cell / side;
				if (wx == 0 || wy == 0 || wx == side - 1 || wy == side - 1) {
					reachesEdge = true;
				}
				for (const auto& step: steps4) {
					int nx = wx + step[0];
					int ny = wy + step[1];
					if (nx < 0 || ny < 0 || nx >= side || ny >= side) {
						continue;
					}
					int neighbour = ny * side + nx;
					if (pared[neighbour] && owner[neighbour] < 0) {
						owner[neighbour] = lump;
						queue.push_back(neighbour);
					}
				}
			}
			if (reachesEdge || queue.size() < 6) {
				continue;
			}
			// Grow the lump back out through the ground around it, as far as was pared off and one more, to get the whole piece and the root of its neck.
			size_t coreSize = queue.size();
			depth.assign(queue.size(), 0);
			bool spillsOut = false;
			for (size_t next = 0; next < queue.size(); ++next) {
				int cell = queue[next];
				int wx = cell % side;
				int wy = cell / side;
				if (depth[next] > pare) {
					continue;
				}
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						int nx = wx + dx;
						int ny = wy + dy;
						if (nx < 0 || ny < 0 || nx >= side || ny >= side) {
							spillsOut = spillsOut || solidAt(left + nx, top + ny);
							continue;
						}
						int neighbour = ny * side + nx;
						if (solid[neighbour] && owner[neighbour] != lump) {
							if (pared[neighbour]) {
								// Ran into another lump's middle without a neck between: not a hanging piece.
								spillsOut = true;
								continue;
							}
							owner[neighbour] = lump;
							queue.push_back(neighbour);
							depth.push_back(depth[next] + 1);
						}
					}
				}
			}
			if (spillsOut) {
				continue;
			}
			HangingPiece piece;
			bool fixed = false;
			bool building = false;
			for (size_t i = 0; i < queue.size(); ++i) {
				int wx = queue[i] % side;
				int wy = queue[i] / side;
				int x = left + wx;
				int y = top + wy;
				WrapInWorld(x, y);
				int material = materialBitmap->line[y][x];
				fixed = fixed || s_Fixed[material];
				building = building || s_Structure[material];
				if (i < coreSize) {
					piece.Core.push_back(y * s_Width + x);
				}
				// Ground touching the piece that isn't part of it is its neck.
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						int nx = wx + dx;
						int ny = wy + dy;
						if (nx < 0 || ny < 0 || nx >= side || ny >= side) {
							continue;
						}
						int neighbour = ny * side + nx;
						if (solid[neighbour] && owner[neighbour] != lump) {
							int cutX = left + nx;
							int cutY = top + ny;
							WrapInWorld(cutX, cutY);
							piece.Cut.push_back(cutY * s_Width + cutX);
						}
					}
				}
			}
			std::sort(piece.Cut.begin(), piece.Cut.end());
			piece.Cut.erase(std::unique(piece.Cut.begin(), piece.Cut.end()), piece.Cut.end());
			// Nothing to cut means it's loose already; a lot to cut means it isn't held by a neck at all.
			if (piece.Cut.empty() || static_cast<int>(piece.Cut.size()) > neckWidth * 4 + 6 || fixed || (building && !TerrainCollapse::BuildingsFall())) {
				continue;
			}
			for (int key: piece.Cut) {
				int material = materialBitmap->line[key / s_Width][key % s_Width];
				fixed = fixed || s_Fixed[material];
			}
			if (fixed) {
				continue;
			}
			// What's beyond the neck has to be a real body of ground, with a middle of its own: otherwise the "neck" is only the tip of a spike on the piece.
			bool anchored = false;
			{
				static std::vector<int> beyond;
				static std::vector<unsigned char> seen;
				seen.assign(cells, 0);
				beyond.clear();
				for (int key: piece.Cut) {
					int wx = ((key % s_Width) - left) % s_Width;
					wx = wx < 0 ? wx + s_Width : wx;
					int wy = key / s_Width - top;
					if (wx >= 0 && wy >= 0 && wx < side && wy < side) {
						beyond.push_back(wy * side + wx);
						seen[static_cast<size_t>(wy) * side + wx] = 1;
					}
				}
				for (size_t next = 0; next < beyond.size() && !anchored && next < 4000; ++next) {
					int wx = beyond[next] % side;
					int wy = beyond[next] / side;
					for (const auto& step: steps4) {
						int nx = wx + step[0];
						int ny = wy + step[1];
						if (nx < 0 || ny < 0 || nx >= side || ny >= side) {
							anchored = anchored || solidAt(left + nx, top + ny);
							continue;
						}
						int neighbour = ny * side + nx;
						if (!solid[neighbour] || seen[neighbour] || owner[neighbour] == lump) {
							continue;
						}
						if (pared[neighbour]) {
							anchored = true;
							break;
						}
						seen[neighbour] = 1;
						beyond.push_back(neighbour);
					}
				}
			}
			if (!anchored) {
				continue;
			}
			std::sort(piece.Core.begin(), piece.Core.end());
			found.push_back(std::move(piece));
		}
	}

	/// Notes how things are around a point before a blast digs its crater: which masses already hang in the air, and which pieces already hang by a thin neck.
	std::shared_ptr<Before> LookBefore(SLTerrain* terrain, const Check& check) {
		auto was = std::make_shared<Before>();
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		std::vector<int> piece;
		s_Touched.clear();
		for (bool treeRules: {true, false}) {
			for (int y = std::max(0, check.Y - check.Radius); y <= std::min(s_Height - 1, check.Y + check.Radius); ++y) {
				for (int rawX = check.X - check.Radius; rawX <= check.X + check.Radius; ++rawX) {
					int x = rawX;
					if (!WrapInWorld(x, y)) {
						continue;
					}
					int key = y * s_Width + x;
					if (!StartsSearch(materialBitmap->line[y][x], treeRules) || (s_State[key] & (c_Seen | c_Falling))) {
						continue;
					}
					if (FindFloatingPiece(materialBitmap, key, s_Width, s_Height, s_WrapX, piece, false, treeRules)) {
						std::sort(piece.begin(), piece.end());
						was->Floating.push_back(piece);
					}
				}
			}
		}
		for (int key: s_Touched) {
			s_State[key] &= static_cast<unsigned char>(~(c_Seen | c_Supported));
		}
		s_Touched.clear();
		std::vector<HangingPiece> hanging;
		FindHangingPieces(materialBitmap, check, hanging);
		for (HangingPiece& found: hanging) {
			was->Hanging.push_back(std::move(found.Core));
		}
		return was;
	}

	/// Lifts a loose piece out of the terrain as a body; it's drawn back in where it moves to.
	void LiftPiece(SLTerrain* terrain, const std::vector<int>& piece) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		int width = s_Width;
		int minX = width;
		int minY = s_Height;
		int maxX = -1;
		int maxY = -1;
		for (int pieceKey: piece) {
			minX = std::min(minX, pieceKey % width);
			maxX = std::max(maxX, pieceKey % width);
			minY = std::min(minY, pieceKey / width);
			maxY = std::max(maxY, pieceKey / width);
		}
		// A piece lying across the seam of a wrapping scene is left alone.
		if (maxX - minX > width / 2) {
			return;
		}
		Body body;
		body.W = maxX - minX + 1;
		body.H = maxY - minY + 1;
		body.Materials.assign(static_cast<size_t>(body.W) * body.H, 0);
		body.Colors.assign(static_cast<size_t>(body.W) * body.H, 0);
		for (int pieceKey: piece) {
			int px = pieceKey % width;
			int py = pieceKey / width;
			int local = (py - minY) * body.W + (px - minX);
			body.Materials[local] = materialBitmap->line[py][px];
			body.Colors[local] = static_cast<unsigned char>(terrain->GetFGColorPixel(px, py));
			body.Stamped.emplace_back(pieceKey, local);
			s_State[pieceKey] |= c_Falling;
		}
		if (!FinishBody(body, glm::vec2(static_cast<float>(minX), static_cast<float>(minY)))) {
			return;
		}
		body.Spin = (Random01() - 0.5F) * 0.006F;
		s_CollapsedCount += body.PixelCount;
		s_Bodies.push_back(std::move(body));
	}

	void RunCheck(SLTerrain* terrain, const Check& check) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		int width = materialBitmap->w;
		int height = materialBitmap->h;
		bool wrapX = g_SceneMan.SceneWrapsX();
		const TerrainCollapse::Tuning& tuning = TerrainCollapse::GetTuning();

		// Pieces left hanging by a thin neck snap off: the neck goes, and the piece is then loose like any other. Ones that hung like that before the blast are left.
		std::vector<HangingPiece> hanging;
		FindHangingPieces(materialBitmap, check, hanging);
		for (const HangingPiece& found: hanging) {
			bool wasLikeThat = false;
			for (size_t i = 0; check.Was && i < check.Was->Hanging.size() && !wasLikeThat; ++i) {
				// The same piece, not merely inside a bigger one that hung before.
				const std::vector<int>& before = check.Was->Hanging[i];
				wasLikeThat = before.size() < found.Core.size() * 2 && found.Core.size() < before.size() * 2 && ShareIn(found.Core, before) > 0.5F;
			}
			if (wasLikeThat) {
				continue;
			}
			for (int key: found.Cut) {
				int x = key % width;
				int y = key / width;
				ThrowDebris(materialBitmap->line[y][x], terrain->GetFGColorPixel(x, y), glm::vec2(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F), glm::vec2(Random01() - 0.5F, Random01() - 0.5F));
				terrain->SetMaterialPixel(x, y, g_MaterialAir);
				terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
			}
		}

		// Everything loose around the point, and for each, which mass that was already hanging in the air it came from (-1 for none: it was part of the world).
		std::vector<std::vector<int>> loose;
		std::vector<int> cameFrom;
		std::vector<int> piece;
		s_Touched.clear();
		for (bool treeRules: {true, false}) {
			for (int y = std::max(0, check.Y - check.Radius); y <= std::min(height - 1, check.Y + check.Radius); ++y) {
				const unsigned char* materialRow = materialBitmap->line[y];
				for (int rawX = check.X - check.Radius; rawX <= check.X + check.Radius; ++rawX) {
					int x = wrapX ? (rawX % width + width) % width : rawX;
					if (x < 0 || x >= width) {
						continue;
					}
					int key = y * width + x;
					if (!StartsSearch(materialRow[x], treeRules) || (s_State[key] & (c_Seen | c_Falling))) {
						continue;
					}
					if (!FindFloatingPiece(materialBitmap, key, width, height, wrapX, piece, !(tuning.FloatingStays && check.Was), treeRules)) {
						continue;
					}
					int origin = -1;
					for (size_t i = 0; tuning.FloatingStays && check.Was && i < check.Was->Floating.size() && origin < 0; ++i) {
						if (ShareIn(piece, check.Was->Floating[i]) > 0.5F) {
							origin = static_cast<int>(i);
						}
					}
					loose.push_back(piece);
					cameFrom.push_back(origin);
				}
			}
		}
		for (int key: s_Touched) {
			s_State[key] &= static_cast<unsigned char>(~(c_Seen | c_Supported));
		}
		s_Touched.clear();
		// What was cut from the world falls. Of the parts of a mass that was already in the air, the biggest stays where it was and the rest fall:
		// chip the corner off a floating island and only the chip falls; cut it in two and the smaller half does.
		for (size_t i = 0; i < loose.size(); ++i) {
			bool falls = cameFrom[i] < 0;
			for (size_t other = 0; !falls && other < loose.size(); ++other) {
				falls = other != i && cameFrom[other] == cameFrom[i] && (loose[other].size() > loose[i].size() || (loose[other].size() == loose[i].size() && other < i));
			}
			if (falls) {
				LiftPiece(terrain, loose[i]);
			}
		}
	}

	/// Makes a lumpy boulder of a material in the air at a point.
	void MakeChunk(SLTerrain* terrain, const ChunkRequest& request) {
		int material = 0;
		for (int id = 1; id < 256 && !material; ++id) {
			const Material* candidate = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (candidate && candidate->GetIndex() == id && candidate->GetPresetName() == request.Material) {
				material = id;
			}
		}
		if (!material || s_Fixed[material]) {
			return;
		}
		Color base = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(material))->GetColor();
		base.RecalculateIndex();
		int shades[3];
		static constexpr int levels[3] = {100, 82, 118};
		for (int i = 0; i < 3; ++i) {
			Color shade = base;
			shade.SetRGB(std::min(255, base.GetR() * levels[i] / 100), std::min(255, base.GetG() * levels[i] / 100), std::min(255, base.GetB() * levels[i] / 100));
			shade.RecalculateIndex();
			shades[i] = shade.GetIndex() > 1 ? shade.GetIndex() : base.GetIndex();
		}
		const BITMAP* texture = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(material))->GetFGTexture();
		if (texture && bitmap_color_depth(const_cast<BITMAP*>(texture)) != 8) {
			texture = nullptr;
		}
		int radius = std::clamp(request.Radius, 3, 60);
		Body body;
		body.W = radius * 2 + 1;
		body.H = radius * 2 + 1;
		body.Materials.assign(static_cast<size_t>(body.W) * body.H, 0);
		body.Colors.assign(static_cast<size_t>(body.W) * body.H, 0);
		float phase[3] = {Random01() * 6.28F, Random01() * 6.28F, Random01() * 6.28F};
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		for (int y = 0; y < body.H; ++y) {
			for (int x = 0; x < body.W; ++x) {
				float dx = static_cast<float>(x - radius);
				float dy = static_cast<float>(y - radius);
				float angle = std::atan2(dy, dx);
				float edge = static_cast<float>(radius) * (0.78F + 0.12F * std::sin(angle * 3.0F + phase[0]) + 0.07F * std::sin(angle * 5.0F + phase[1]) + 0.03F * std::sin(angle * 9.0F + phase[2]));
				int wx = request.X - radius + x;
				int wy = request.Y - radius + y;
				if (dx * dx + dy * dy > edge * edge || !WrapInWorld(wx, wy) || materialBitmap->line[wy][wx] != g_MaterialAir) {
					continue;
				}
				int local = y * body.W + x;
				body.Materials[local] = static_cast<unsigned char>(material);
				if (texture && texture->w > 0 && texture->h > 0) {
					// The material's own terrain texture, as ground made of it has.
					int color = texture->line[wy % texture->h][wx % texture->w];
					body.Colors[local] = static_cast<unsigned char>(color != ColorKeys::g_MaskColor ? color : shades[0]);
				} else {
					// Lighter on top, darker beneath, with a few flecks.
					float light = -dy / static_cast<float>(radius) + (Random01() - 0.5F) * 0.35F;
					body.Colors[local] = static_cast<unsigned char>(shades[light > 0.45F ? 2 : (light < -0.4F ? 1 : 0)]);
				}
			}
		}
		if (!FinishBody(body, glm::vec2(static_cast<float>(request.X - radius), static_cast<float>(request.Y - radius))) || body.PixelCount < c_MinBodyPixels) {
			return;
		}
		body.Spin = (Random01() - 0.5F) * 0.05F;
		Stamp(terrain, body);
		s_Bodies.push_back(std::move(body));
	}

	/// A step on loose ground: the few surface pixels just ahead of the foot (in the way the unit is going) come loose and are pushed along, so a run down a sand slope slumps it a little.
	void Scuff(SLTerrain* terrain, const Footfall& step) {
		const TerrainCollapse::Tuning& tuning = TerrainCollapse::GetTuning();
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		int depth = tuning.ScuffStrength >= 1.5F ? 3 : 2;
		std::vector<int> piece;
		float scuffiness = 0.0F;
		for (int column = 0; column < 3; ++column) {
			int x = step.X + step.Direction * (column + 1);
			if (!WrapInWorld(x, step.Y)) {
				continue;
			}
			// The surface of this column near the foot: the first ground with air above it.
			for (int y = std::max(step.Y - 3, 1); y < std::min(step.Y + 5, s_Height); ++y) {
				int material = materialBitmap->line[y][x];
				if (material == g_MaterialAir || materialBitmap->line[y - 1][x] != g_MaterialAir) {
					continue;
				}
				for (int below = 0; below < depth && y + below < s_Height; ++below) {
					int key = (y + below) * s_Width + x;
					int belowMaterial = materialBitmap->line[y + below][x];
					if (s_Scuff[belowMaterial] <= 0.0F || s_Fixed[belowMaterial] || (s_State[key] & c_Falling)) {
						break;
					}
					scuffiness = std::max(scuffiness, s_Scuff[belowMaterial]);
					piece.push_back(key);
				}
				break;
			}
		}
		if (static_cast<int>(piece.size()) < c_MinBodyPixels || Random01() > std::min(1.0F, tuning.ScuffStrength * scuffiness * 0.6F)) {
			return;
		}
		size_t before = s_Bodies.size();
		LiftPiece(terrain, piece);
		if (s_Bodies.size() > before) {
			float push = std::clamp(tuning.ScuffStrength, 0.0F, 2.0F) * std::clamp(step.Speed, 0.4F, 1.5F);
			s_Bodies.back().Vel += glm::vec2(static_cast<float>(step.Direction) * (0.35F + 0.25F * Random01()) * push, -0.25F * push);
			s_Bodies.back().Still = 0;
		}
	}
} // namespace

void TerrainCollapse::NoteFootfall(int x, int y, int direction, float speed) {
	if (!s_Enabled || s_Tuning.ScuffStrength <= 0.0F || x < 0 || y < 0 || direction == 0) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	if (s_Footfalls.size() < c_MaxFootfalls) {
		s_Footfalls.push_back({x, y, direction < 0 ? -1 : 1, speed});
	}
}

void TerrainCollapse::QueueCheck(const Vector& position, float radius) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	s_Pending.push_back({static_cast<int>(position.m_X), static_cast<int>(position.m_Y), static_cast<int>(radius), 0});
}

void TerrainCollapse::Blast(const Vector& position, float reach, float energy) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	if (s_Blasts.size() < 256) {
		s_Blasts.push_back({static_cast<int>(position.m_X), static_cast<int>(position.m_Y), std::max(reach, 8.0F), std::clamp(std::sqrt(std::max(energy, 0.0F)) * 0.055F, 1.2F, 9.0F)});
	}
}

void TerrainCollapse::NoteDamage(int x, int y) {
	if (!s_Enabled || x < 0 || y < 0) {
		return;
	}
	std::scoped_lock lock(s_DamageMutex);
	if (s_Damage.size() < 40000) {
		s_Damage.push_back(x);
		s_Damage.push_back(y);
	}
}

void TerrainCollapse::BeginChange(const Vector& position, float radius) {
	if (!s_Enabled) {
		return;
	}
	Check check{static_cast<int>(position.m_X), static_cast<int>(position.m_Y), static_cast<int>(radius), 0, nullptr};
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	// Only once the system has seen this scene (its tables are for it); before that the check runs without knowing how things were.
	if (terrain && scene == s_Scene && s_TablesBuilt && s_State.size() == static_cast<size_t>(terrain->GetMaterialBitmap()->w) * static_cast<size_t>(terrain->GetMaterialBitmap()->h)) {
		check.Was = LookBefore(terrain, check);
	}
	std::scoped_lock lock(s_QueueMutex);
	s_Pending.push_back(check);
}

void TerrainCollapse::SpawnChunk(const Vector& position, float radius, const char* materialName) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	s_ChunkRequests.push_back({static_cast<int>(position.m_X), static_cast<int>(position.m_Y), static_cast<int>(radius), materialName ? materialName : "Stone"});
}

void TerrainCollapse::Update() {
	if (g_SceneMan.GetScene() != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		Clear();
		// (The pixel states of the last game are no guide to this one, even on a terrain of the same size.)
		s_State.clear();
		s_Scene = g_SceneMan.GetScene();
		s_SceneGeneration = g_SceneMan.GetSceneGeneration();
		s_CollapsedCount = 0;
		s_TablesBuilt = false;
		s_Random = 0x51ED270Bu;
	}
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	if (!terrain || !s_Enabled) {
		Clear();
		// The byte per pixel is given back while there is nothing to collapse (the menus, or collapse switched off); it is made again when needed.
		std::vector<unsigned char>().swap(s_State);
		return;
	}
	if (!s_TablesBuilt) {
		BuildTables();
	}
	s_Width = terrain->GetMaterialBitmap()->w;
	s_Height = terrain->GetMaterialBitmap()->h;
	s_WrapX = g_SceneMan.SceneWrapsX();
	if (size_t pixels = static_cast<size_t>(s_Width) * static_cast<size_t>(s_Height); s_State.size() != pixels) {
		// A terrain of another size: nothing known about its pixels carries over.
		s_Bodies.clear();
		s_State.assign(pixels, 0);
	}
	long long now = g_TimerMan.GetSimUpdateCount();
	std::vector<ChunkRequest> chunks;
	std::vector<Footfall> footfalls;
	std::vector<Check> pending;
	{
		std::scoped_lock lock(s_QueueMutex);
		// Sort for a fixed order, whatever order the gib code queued them in. The crater is still being dug by the blast's particles,
		// so check after half a second and again after a second and a half.
		std::sort(s_Pending.begin(), s_Pending.end(), [](const Check& a, const Check& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Radius < b.Radius); });
		pending.swap(s_Pending);
		chunks.swap(s_ChunkRequests);
		footfalls.swap(s_Footfalls);
	}
	for (const Check& check: pending) {
		// The blast was this update or the last and its crater is only now being dug, so this is how things were before it.
		std::shared_ptr<Before> was = check.Was ? check.Was : LookBefore(terrain, check);
		s_Scheduled.push_back({check.X, check.Y, check.Radius, now + 30, was});
		s_Scheduled.push_back({check.X, check.Y, check.Radius, now + 90, was});
	}
	for (const ChunkRequest& request: chunks) {
		MakeChunk(terrain, request);
	}
	// Steps on loose ground, in a fixed order, a few an update.
	std::sort(footfalls.begin(), footfalls.end(), [](const Footfall& a, const Footfall& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Direction < b.Direction); });
	for (size_t i = 0; i < footfalls.size() && i < 8; ++i) {
		Scuff(terrain, footfalls[i]);
	}
	// Explosions throw loose pieces: the ones still moving, and ones lying where they came to rest in the last minute, which are lifted out of the ground again.
	{
		std::vector<BlastRequest> blasts;
		{
			std::scoped_lock lock(s_QueueMutex);
			blasts.swap(s_Blasts);
		}
		s_Rested.erase(std::remove_if(s_Rested.begin(), s_Rested.end(), [now](const Rested& rested) { return now - rested.When > c_RestedUpdates; }), s_Rested.end());
		std::sort(blasts.begin(), blasts.end(), [](const BlastRequest& a, const BlastRequest& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Push < b.Push); });
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		for (const BlastRequest& blast: blasts) {
			glm::vec2 from(static_cast<float>(blast.X), static_cast<float>(blast.Y));
			// How hard a piece at a place is thrown, in pixels per update, before its weight is counted.
			auto pushAt = [&](const glm::vec2& center, float radius) {
				float distance = glm::length(center - from);
				float falloff = std::clamp(1.0F - distance / (blast.Reach + radius), 0.0F, 1.0F);
				return blast.Push * falloff * falloff * std::max(TerrainCollapse::GetTuning().BlastPush, 0.0F);
			};
			auto throwBody = [&](Body& body) {
				float push = pushAt(body.Pos, body.Radius) / (1.0F + body.Mass / 500.0F);
				if (push <= 0.01F) {
					return;
				}
				glm::vec2 away = body.Pos - from;
				// Up and away rather than straight along the ground.
				away.y -= 0.35F * glm::length(away) + 1.0F;
				body.Vel += glm::normalize(away) * push;
				body.Spin += (Random01() - 0.5F) * 0.12F * std::min(push, 3.0F);
				body.Still = 0;
			};
			for (Body& body: s_Bodies) {
				if (!body.Done) {
					throwBody(body);
				}
			}
			for (size_t i = 0; i < s_Rested.size();) {
				Rested& rested = s_Rested[i];
				if (pushAt(rested.Center, rested.Radius) < 0.6F) {
					++i;
					continue;
				}
				// What's left of it where it lay. If most of it is gone (blasted, dug, built over), it's forgotten.
				std::vector<int> piece;
				for (int key: rested.Keys) {
					int material = materialBitmap->line[key / s_Width][key % s_Width];
					if (material != g_MaterialAir && !FluidSim::IsLiquid(material) && !s_Fixed[material] && !(s_State[key] & c_Falling)) {
						piece.push_back(key);
					}
				}
				if (piece.size() * 10 >= rested.Keys.size() * 7 && piece.size() >= 10) {
					size_t before = s_Bodies.size();
					LiftPiece(terrain, piece);
					if (s_Bodies.size() > before) {
						throwBody(s_Bodies.back());
					}
				}
				s_Rested.erase(s_Rested.begin() + static_cast<std::ptrdiff_t>(i));
			}
		}
	}
	// Ground being dug or shot away: start watching the squares it's happening in, and check the ones being watched.
	{
		std::vector<int> damage;
		{
			std::scoped_lock lock(s_DamageMutex);
			damage.swap(s_Damage);
		}
		for (size_t i = 0; i + 1 < damage.size(); i += 2) {
			std::pair<int, int> square(damage[i + 1] / c_WatchCell, damage[i] / c_WatchCell);
			if (auto found = s_Watches.find(square); found != s_Watches.end()) {
				found->second.LastDamage = now;
			} else if (auto waiting = s_WatchWaitingDamage.find(square); waiting != s_WatchWaitingDamage.end()) {
				waiting->second = now;
			} else {
				s_WatchWaitingDamage.emplace(square, now);
				s_WatchesWaiting.push_back(square);
			}
		}
		// A few new squares an update, more when many are waiting so the wait stays short.
		constexpr int c_NewWatchesPerUpdate = 4;
		for (size_t looks = std::max<size_t>(c_NewWatchesPerUpdate, s_WatchesWaiting.size() / 4); looks > 0 && !s_WatchesWaiting.empty(); --looks) {
			std::pair<int, int> square = s_WatchesWaiting.front();
			s_WatchesWaiting.pop_front();
			Watch watch;
			watch.LastDamage = s_WatchWaitingDamage[square];
			watch.NextCheck = now + 30;
			s_WatchWaitingDamage.erase(square);
			// How things are as the wearing begins: what's already hanging in the air here isn't this digging's doing.
			watch.Was = LookBefore(terrain, {square.second * c_WatchCell + c_WatchCell / 2, square.first * c_WatchCell + c_WatchCell / 2, c_WatchRadius, now, nullptr});
			s_Watches.emplace(square, std::move(watch));
		}
		int checksLeft = 3;
		for (auto watch = s_Watches.begin(); watch != s_Watches.end();) {
			if (checksLeft > 0 && now >= watch->second.NextCheck) {
				--checksLeft;
				RunCheck(terrain, {watch->first.second * c_WatchCell + c_WatchCell / 2, watch->first.first * c_WatchCell + c_WatchCell / 2, c_WatchRadius, now, watch->second.Was});
				watch->second.NextCheck = now + 30;
				if (now - watch->second.LastDamage > 120) {
					watch = s_Watches.erase(watch);
					continue;
				}
			}
			++watch;
		}
	}
	UpdateBodies(terrain);
	// The checks come due: a few an update (more when many are due, so the wait stays short), the longest due first, the rest carried over. A place
	// and size already checked this update isn't checked again (a crater queued twice, or a check's second look come due while its first waited).
	constexpr int c_ChecksPerUpdate = 4;
	std::stable_sort(s_Scheduled.begin(), s_Scheduled.end(), [](const Check& a, const Check& b) { return a.DueUpdate < b.DueUpdate; });
	size_t due = std::count_if(s_Scheduled.begin(), s_Scheduled.end(), [now](const Check& check) { return check.DueUpdate <= now; });
	size_t checksLeft = std::max<size_t>(c_ChecksPerUpdate, due / 4);
	std::vector<Check> ran;
	std::vector<Check> kept;
	for (Check& check: s_Scheduled) {
		if (check.DueUpdate > now) {
			kept.push_back(std::move(check));
			continue;
		}
		if (std::any_of(ran.begin(), ran.end(), [&check](const Check& done) { return done.X == check.X && done.Y == check.Y && done.Radius == check.Radius; })) {
			continue;
		}
		if (checksLeft == 0) {
			kept.push_back(std::move(check));
			continue;
		}
		--checksLeft;
		RunCheck(terrain, check);
		ran.push_back(std::move(check));
	}
	s_Scheduled.swap(kept);
}

void TerrainCollapse::Clear() {
	// Only falling pieces leave marks on the pixel states between checks. Where they are drawn in the terrain they stay, as ordinary ground.
	if (!s_Bodies.empty()) {
		std::fill(s_State.begin(), s_State.end(), static_cast<unsigned char>(0));
	}
	s_Bodies.clear();
	s_NewBodies.clear();
	s_Watches.clear();
	s_WatchesWaiting.clear();
	s_WatchWaitingDamage.clear();
	s_Rested.clear();
	s_Blasts.clear();
	{
		std::scoped_lock damageLock(s_DamageMutex);
		s_Damage.clear();
	}
	std::scoped_lock lock(s_QueueMutex);
	s_Pending.clear();
	s_Scheduled.clear();
	s_ChunkRequests.clear();
	s_Footfalls.clear();
}

int TerrainCollapse::GetCollapsedCount() {
	return s_CollapsedCount;
}

int TerrainCollapse::GetFallingCount() {
	return static_cast<int>(s_Bodies.size());
}

void TerrainCollapse::GetFallingPieces(std::vector<FallingPiece>& pieces) {
	for (const Body& body: s_Bodies) {
		pieces.push_back({body.Pos.x, body.Pos.y, body.Radius, body.Vel.x, body.Vel.y, IsTree(body)});
	}
}
