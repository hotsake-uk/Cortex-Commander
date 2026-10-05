#include "TerrainCollapse.h"
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

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <memory>
#include <mutex>
#include <vector>

using namespace RTE;

bool TerrainCollapse::s_Enabled = true;
bool TerrainCollapse::s_BuildingsFall = true;
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
	std::array<float, 256> s_Density{};
	std::array<float, 256> s_Toughness{};
	int s_IceMaterial = 0;
	int s_WaterMaterial = 0;
	int s_WaterColor = 0;
	bool s_TablesBuilt = false;

	void BuildTables() {
		s_Fixed.fill(false);
		s_Structure.fill(false);
		s_Flimsy.fill(false);
		s_IceMaterial = 0;
		s_WaterMaterial = 0;
		s_Density.fill(1.0F);
		s_Toughness.fill(60.0F);
		for (int id = 1; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (!material || material->GetIndex() != id) {
				continue;
			}
			s_Density[id] = std::clamp(material->GetPixelDensity(), 0.05F, 50.0F);
			s_Toughness[id] = std::clamp(material->GetIntegrity(), 1.0F, 600.0F);
			s_Flimsy[id] = material->GetIntegrity() >= 0.0F && material->GetIntegrity() < 5.0F;
			const std::string& name = material->GetPresetName();
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
			for (const char* word: {"Concrete", "Metal", "Military", "Civilian", "Glass"}) {
				if (name.find(word) != std::string::npos) {
					s_Structure[id] = true;
					break;
				}
			}
		}
		s_TablesBuilt = true;
	}

	std::vector<Check> s_Pending; //!< Checks queued from (possibly parallel) gib code, waiting to be scheduled.
	std::vector<Check> s_Scheduled;
	struct ChunkRequest {
		int X, Y, Radius;
		std::string Material;
	};
	std::vector<ChunkRequest> s_ChunkRequests;
	std::mutex s_QueueMutex;
	const void* s_Scene = nullptr;
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
	};
	std::vector<Body> s_Bodies;
	std::vector<Body> s_NewBodies; //!< Pieces made while the bodies are being stepped; they join afterwards.

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

	/// Whether a falling piece would hit something at a point: solid ground, not liquid.
	bool SolidAt(const BITMAP* materialBitmap, int x, int y) {
		if (!WrapInWorld(x, y)) {
			return false;
		}
		int material = materialBitmap->line[y][x];
		return material != g_MaterialAir && !s_Flimsy[material] && !FluidSim::IsLiquid(material);
	}

	/// Works out a body's mass, centre, inertia and outline from its bitmap. Returns false if nothing is left of it.
	bool FinishBody(Body& body, const glm::vec2& topLeft) {
		double mass = 0.0;
		double centerX = 0.0;
		double centerY = 0.0;
		double toughness = 0.0;
		body.PixelCount = 0;
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
				}
			}
		}
		if (body.PixelCount == 0) {
			return false;
		}
		body.Mass = static_cast<float>(mass);
		body.Center = glm::vec2(static_cast<float>(centerX / mass), static_cast<float>(centerY / mass));
		body.Toughness = static_cast<float>(toughness / body.PixelCount);
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

	/// Draws a body into the terrain where it is. Liquid in its way is moved up out of it rather than lost.
	void Stamp(SLTerrain* terrain, Body& body) {
		const BITMAP* materialBitmap = terrain->GetMaterialBitmap();
		float c = std::cos(body.Angle);
		float s = std::sin(body.Angle);
		int reach = static_cast<int>(std::ceil(body.Radius)) + 1;
		int centerX = static_cast<int>(std::floor(body.Pos.x));
		int centerY = static_cast<int>(std::floor(body.Pos.y));
		struct Displaced {
			int X, Y, Material, Color;
		};
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
				// Liquid is pushed aside (below); grass and the like is flattened; anything else solid is left as it is.
				if (existing != g_MaterialAir && !FluidSim::IsLiquid(existing) && !s_Flimsy[existing]) {
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
		// Each pixel of liquid the piece now covers goes to the nearest free place: up through the piece and the liquid above it to the surface,
		// or, under a ceiling, sideways along the highest row it can reach. None is lost, so the level rises around what falls in.
		for (const Displaced& liquid: displaced) {
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
		if (!displaced.empty()) {
			FluidSim::Disturb(Vector(body.Pos.x, body.Pos.y), body.Radius + 6.0F);
		}
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
		Color color;
		color.SetRGBWithIndex(colorIndex);
		// Particle speeds are in metres a second: 20 pixels to the metre, 60 updates a second.
		MOPixel* pixel = new MOPixel(color, sceneMaterial->GetPixelDensity(), Vector(position.x, position.y), Vector(velocity.x * 3.0F, velocity.y * 3.0F), new Atom(Vector(), sceneMaterial->GetIndex(), nullptr, color, 2), 0);
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

	/// Cracks a body into pieces along lines through the point where it hit. The pieces become bodies of their own, or loose particles if they're tiny.
	void Break(Body& body, const glm::vec2& hitPoint, float violence) {
		float c = std::cos(body.Angle);
		float s = std::sin(body.Angle);
		glm::vec2 offset = hitPoint - body.Pos;
		glm::vec2 hitLocal(offset.x * c + offset.y * s + body.Center.x, -offset.x * s + offset.y * c + body.Center.y);
		int cuts = 1 + (violence > 1.5F ? 1 : 0) + (body.Toughness < 60.0F ? 1 : 0);
		struct Cut {
			glm::vec2 Through, Across;
			float Wobble, Phase;
		};
		std::vector<Cut> lines;
		for (int i = 0; i < cuts; ++i) {
			float angle = Random01() * 3.14159F;
			// The first crack runs from where it hit; the others cross the body nearer its middle.
			glm::vec2 through = i == 0 ? glm::mix(hitLocal, body.Center, 0.35F) : body.Center + glm::vec2(Random01() - 0.5F, Random01() - 0.5F) * body.Radius * 0.8F;
			lines.push_back({through, glm::vec2(std::cos(angle), std::sin(angle)), 1.0F + Random01() * 2.5F, Random01() * 6.28F});
		}
		std::vector<int> region(body.Materials.size(), -1);
		for (int y = 0; y < body.H; ++y) {
			for (int x = 0; x < body.W; ++x) {
				int local = y * body.W + x;
				if (!body.Materials[local]) {
					continue;
				}
				glm::vec2 at(static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F);
				int side = 0;
				for (size_t i = 0; i < lines.size(); ++i) {
					glm::vec2 from = at - lines[i].Through;
					float along = from.x * -lines[i].Across.y + from.y * lines[i].Across.x;
					// A jagged line rather than a ruled one.
					float distance = glm::dot(from, lines[i].Across) + lines[i].Wobble * std::sin(along * 0.45F + lines[i].Phase) + 0.6F * std::sin(along * 1.7F);
					side |= (distance > 0.0F ? 1 : 0) << i;
				}
				region[local] = side;
			}
		}
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
			if (static_cast<int>(part.size()) < 20) {
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
			piece.Vel = body.Vel + body.Spin * glm::vec2(-arm.y, arm.x) + (armLength > 0.01F ? arm / armLength * 0.25F : glm::vec2(0.0F));
			piece.Spin = body.Spin + (Random01() - 0.5F) * 0.03F;
			piece.Generation = body.Generation + 1;
			piece.BreakCooldown = 12;
			s_NewBodies.push_back(std::move(piece));
		}
		body.Done = true;
		// A burst of dust where it broke (visual only).
		ThrowDust(hitPoint, std::min(6 + body.PixelCount / 60, 40), body.Materials, body.Colors);
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
			if (static_cast<int>(part.size()) < c_MinBodyPixels) {
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

	/// A body has stopped: it stays in the terrain as ordinary ground.
	void Settle(SLTerrain* terrain, Body& body) {
		for (const auto& [key, local]: body.Stamped) {
			s_State[key] &= static_cast<unsigned char>(~c_Falling);
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
		if (body.PixelCount < c_MinBodyPixels) {
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
		for (size_t i = 0; i < body.Outline.size(); i += 4) {
			glm::vec2 at = ToWorld(body, body.Outline[i], body.Pos, body.Angle);
			int x = static_cast<int>(std::floor(at.x));
			int y = static_cast<int>(std::floor(at.y));
			if (WrapInWorld(x, y) && FluidSim::IsLiquid(materialBitmap->line[y][x])) {
				++inLiquid;
			}
		}
		if (inLiquid > 0 && !body.Wet && glm::length(body.Vel) > 1.5F) {
			FluidSim::Splash(Vector(body.Pos.x, body.Pos.y + body.Radius * 0.5F), body.Radius + 4.0F, 0.35F, glm::length(body.Vel) * 1.6F);
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
					float drag = std::clamp(-glm::dot(pointVel, along) / (1.0F / body.Mass + armCrossAlong * armCrossAlong / body.Inertia), -c_Friction * push, c_Friction * push);
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
		if (glm::length(body.Vel) > 1.2F) {
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
				if (!object || std::find(struck, struck + struckCount, object) != struck + struckCount) {
					continue;
				}
				struck[struckCount++] = object;
				float objectMass = std::max(object->GetMass(), 1.0F);
				float weight = std::min(body.Mass, objectMass * 2.0F + 20.0F);
				object->AddAbsImpulseForce(Vector(body.Vel.x, body.Vel.y) * (3.0F * weight), Vector(at.x, at.y));
				body.Vel *= 1.0F - std::min(0.3F, objectMass / (objectMass + body.Mass));
			}
		}

		// Hit harder than its material can take: it cracks.
		float breakSpeed = (1.6F + body.Toughness / 45.0F) * std::max(TerrainCollapse::GetTuning().BreakStrength, 0.1F);
		// A thud of dust where it lands (visual only).
		if (hardestHit > 1.2F) {
			ThrowDust(hardestPoint, std::min(static_cast<int>((3.0F + static_cast<float>(body.PixelCount) / 120.0F) * hardestHit * 0.5F), 30), body.Materials, body.Colors);
		}
		if (hardestHit > breakSpeed && body.BreakCooldown == 0 && body.Generation < c_MaxGeneration && body.PixelCount >= c_MinBreakPixels) {
			Break(body, hardestPoint, hardestHit / breakSpeed);
			return;
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

	/// Flood fills the solid piece containing a pixel. Returns true if it's a floating piece that should fall, filling its pixels.
	bool FindFloatingPiece(const BITMAP* materialBitmap, int startKey, int width, int height, bool wrapX, std::vector<int>& piece) {
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
					if (state & c_Supported) {
						// Joined to a piece already found to be held up.
						floating = false;
						continue;
					}
					int neighbourMaterial = materialBitmap->line[ny][nx];
					// Liquid holds nothing up, and a piece that's already falling isn't support either.
					if ((state & (c_Seen | c_Falling)) || neighbourMaterial == g_MaterialAir || FluidSim::IsLiquid(neighbourMaterial)) {
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
		if (floating && structurePixels * 2 > static_cast<int>(piece.size()) && static_cast<int>(piece.size()) < TerrainCollapse::GetTuning().MinFittingPixels) {
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
		static std::vector<int> owner;
		static std::vector<int> depth;
		static std::vector<int> queue;
		size_t cells = static_cast<size_t>(side) * side;
		solid.assign(cells, 0);
		pared.assign(cells, 0);
		owner.assign(cells, -1);
		for (int wy = 0; wy < side; ++wy) {
			for (int wx = 0; wx < side; ++wx) {
				solid[static_cast<size_t>(wy) * side + wx] = solidAt(left + wx, top + wy) ? 1 : 0;
			}
		}
		// Rows of solid first, then columns of those: a pixel is kept if everything within the paring distance of it is solid. Beyond the window counts as solid.
		static std::vector<unsigned char> rows;
		rows.assign(cells, 0);
		for (int wy = 0; wy < side; ++wy) {
			for (int wx = 0; wx < side; ++wx) {
				bool all = true;
				for (int d = -pare; d <= pare && all; ++d) {
					int x = wx + d;
					all = x < 0 || x >= side || solid[static_cast<size_t>(wy) * side + x];
				}
				rows[static_cast<size_t>(wy) * side + wx] = all ? 1 : 0;
			}
		}
		for (int wy = 0; wy < side; ++wy) {
			for (int wx = 0; wx < side; ++wx) {
				bool all = true;
				for (int d = -pare; d <= pare && all; ++d) {
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
		for (int y = std::max(0, check.Y - check.Radius); y <= std::min(s_Height - 1, check.Y + check.Radius); ++y) {
			for (int rawX = check.X - check.Radius; rawX <= check.X + check.Radius; ++rawX) {
				int x = rawX;
				if (!WrapInWorld(x, y)) {
					continue;
				}
				int key = y * s_Width + x;
				int material = materialBitmap->line[y][x];
				if (material == g_MaterialAir || FluidSim::IsLiquid(material) || (s_State[key] & (c_Seen | c_Falling))) {
					continue;
				}
				if (FindFloatingPiece(materialBitmap, key, s_Width, s_Height, s_WrapX, piece)) {
					std::sort(piece.begin(), piece.end());
					was->Floating.push_back(piece);
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
		for (int y = std::max(0, check.Y - check.Radius); y <= std::min(height - 1, check.Y + check.Radius); ++y) {
			const unsigned char* materialRow = materialBitmap->line[y];
			for (int rawX = check.X - check.Radius; rawX <= check.X + check.Radius; ++rawX) {
				int x = wrapX ? (rawX % width + width) % width : rawX;
				if (x < 0 || x >= width) {
					continue;
				}
				int key = y * width + x;
				if (materialRow[x] == g_MaterialAir || FluidSim::IsLiquid(materialRow[x]) || (s_State[key] & (c_Seen | c_Falling))) {
					continue;
				}
				if (!FindFloatingPiece(materialBitmap, key, width, height, wrapX, piece)) {
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
} // namespace

void TerrainCollapse::QueueCheck(const Vector& position, float radius) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	s_Pending.push_back({static_cast<int>(position.m_X), static_cast<int>(position.m_Y), static_cast<int>(radius), 0});
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
	if (g_SceneMan.GetScene() != s_Scene) {
		Clear();
		s_Scene = g_SceneMan.GetScene();
		s_CollapsedCount = 0;
		s_TablesBuilt = false;
		s_Random = 0x51ED270Bu;
	}
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	if (!terrain || !s_Enabled) {
		Clear();
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
	std::vector<Check> pending;
	{
		std::scoped_lock lock(s_QueueMutex);
		// Sort for a fixed order, whatever order the gib code queued them in. The crater is still being dug by the blast's particles,
		// so check after half a second and again after a second and a half.
		std::sort(s_Pending.begin(), s_Pending.end(), [](const Check& a, const Check& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Radius < b.Radius); });
		pending.swap(s_Pending);
		chunks.swap(s_ChunkRequests);
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
	UpdateBodies(terrain);
	for (const Check& check: s_Scheduled) {
		if (check.DueUpdate <= now) {
			RunCheck(terrain, check);
		}
	}
	s_Scheduled.erase(std::remove_if(s_Scheduled.begin(), s_Scheduled.end(), [now](const Check& check) { return check.DueUpdate <= now; }), s_Scheduled.end());
}

void TerrainCollapse::Clear() {
	// Only falling pieces leave marks on the pixel states between checks. Where they are drawn in the terrain they stay, as ordinary ground.
	if (!s_Bodies.empty()) {
		std::fill(s_State.begin(), s_State.end(), static_cast<unsigned char>(0));
	}
	s_Bodies.clear();
	s_NewBodies.clear();
	std::scoped_lock lock(s_QueueMutex);
	s_Pending.clear();
	s_Scheduled.clear();
	s_ChunkRequests.clear();
}

int TerrainCollapse::GetCollapsedCount() {
	return s_CollapsedCount;
}

int TerrainCollapse::GetFallingCount() {
	return static_cast<int>(s_Bodies.size());
}
