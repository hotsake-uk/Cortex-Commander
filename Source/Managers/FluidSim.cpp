#include "FluidSim.h"
#include "Constants.h"
#include "ConsoleMan.h"
#include "EffectsParticles.h"
#include "Material.h"
#include "MovableMan.h"
#include "MovableObject.h"
#include "MOPixel.h"
#include "PresetMan.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "TerrainFire.h"
#include "TimerMan.h"
#include "Vector.h"
#include "RenderMan.h"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace RTE;

bool FluidSim::s_Enabled = true;

namespace {
	enum class Liquid : unsigned char {
		None,
		Water,
		Lava,
		Acid,
		Oil
	};
	constexpr int c_LiquidKinds = 5;

	struct LiquidProperties {
		int Flow; //!< How far a pixel may run sideways per step.
		int MoveEvery; //!< Moves every this many sim updates; viscous liquids are slower.
	};
	constexpr LiquidProperties c_Liquids[] = {
	    {0, 1}, // None
	    {4, 1}, // Water
	    {1, 3}, // Lava
	    {3, 1}, // Acid
	    {2, 2}, // Oil
	};

	constexpr size_t c_MaxActive = 30000;
	constexpr int c_RestSteps = 20; //!< A pixel that hasn't moved for this many of its steps stops being simulated.

	std::array<Liquid, 256> s_Kinds{};
	std::array<int, c_LiquidKinds> s_MaterialOf{}; //!< Material ID of each liquid kind, 0 if the scene's materials don't have it.
	std::array<int, c_LiquidKinds> s_ColorOf{}; //!< Palette index each liquid is drawn with.
	int s_StoneMaterial = 0;
	int s_StoneColor = 0;
	bool s_TablesBuilt = false;

	std::map<int, int> s_Active; //!< Moving liquid pixels (y * width + x) and how many steps they've been still. Ordered, so deterministic.
	struct PourRequest {
		int X, Y, Radius;
		Liquid Kind;
	};
	std::vector<PourRequest> s_Pours;
	std::vector<std::pair<glm::ivec2, int>> s_Disturbances;
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
		s_StoneMaterial = 0;
		for (int id = 1; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (!material || material->GetIndex() != id) {
				continue;
			}
			const std::string& name = material->GetPresetName();
			Liquid kind = LiquidFromName(name, Liquid::None);
			if (kind != Liquid::None) {
				s_Kinds[id] = kind;
				s_MaterialOf[static_cast<int>(kind)] = id;
				Color color = material->GetColor();
				color.RecalculateIndex();
				s_ColorOf[static_cast<int>(kind)] = color.GetIndex();
				// Oil is drawn plain: its dark brown is shared with too many sprites to shimmer.
				if (kind != Liquid::Oil) {
					g_RenderMan.SetLiquidPaletteColor(color.GetIndex(), static_cast<int>(kind), kind == Liquid::Lava ? 230 : 0);
				}
			} else if (name == "Stone") {
				s_StoneMaterial = id;
				Color color = material->GetColor();
				color.RecalculateIndex();
				s_StoneColor = color.GetIndex();
			}
		}
		s_TablesBuilt = true;
	}

	Liquid KindAt(const SLTerrain* terrain, int x, int y) { return s_Kinds[static_cast<unsigned char>(terrain->GetMaterialPixel(x, y))]; }

	bool InWorld(int& x, int& y, int width, int height) {
		if (g_SceneMan.SceneWrapsX()) {
			x = ((x % width) + width) % width;
		}
		return x >= 0 && y >= 0 && x < width && y < height;
	}

	void Activate(int x, int y, int width, int height, const SLTerrain* terrain) {
		if (s_Active.size() < c_MaxActive && InWorld(x, y, width, height) && KindAt(terrain, x, y) != Liquid::None) {
			s_Active.emplace(y * width + x, 0);
		}
	}

	void ActivateAround(int x, int y, int width, int height, const SLTerrain* terrain) {
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				Activate(x + dx, y + dy, width, height, terrain);
			}
		}
	}

	MovableObject* CreateEffect(const char* className, const char* presetName) {
		const Entity* preset = g_PresetMan.GetEntityPreset(className, presetName, "Base.rte");
		return preset ? dynamic_cast<MovableObject*>(preset->Clone()) : nullptr;
	}
} // namespace

bool FluidSim::IsLiquid(int materialID) {
	return s_TablesBuilt && materialID > 0 && materialID < 256 && s_Kinds[materialID] != Liquid::None;
}

void FluidSim::Pour(const Vector& position, float radius, const char* liquidName) {
	std::string name(liquidName ? liquidName : "Water");
	Liquid kind = LiquidFromName(name, Liquid::Water);
	std::scoped_lock lock(s_QueueMutex);
	s_Pours.push_back({static_cast<int>(position.m_X), static_cast<int>(position.m_Y), std::max(1, static_cast<int>(radius)), kind});
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
	if (!s_Enabled || !IsLiquid(material)) {
		return;
	}
	const MOPixel* pixel = dynamic_cast<const MOPixel*>(particle);
	if (pixel && pixel->GetColor().GetIndex() == s_ColorOf[static_cast<int>(s_Kinds[material])]) {
		std::scoped_lock lock(s_QueueMutex);
		s_Disturbances.emplace_back(glm::ivec2(position.GetFloorIntX(), position.GetFloorIntY()), 1);
	}
}

void FluidSim::Disturb(const Vector& position, float radius) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	s_Disturbances.emplace_back(glm::ivec2(static_cast<int>(position.m_X), static_cast<int>(position.m_Y)), static_cast<int>(radius));
}

void FluidSim::Update() {
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
			int x = 0;
			int y = 0;
			int still = 0;
			while (loadedWidth > 0 && stream >> x >> y >> still && s_Active.size() < c_MaxActive) {
				s_Active.emplace(y * loadedWidth + x, still);
			}
			g_ConsoleMan.PrintString("SYSTEM: Restored " + std::to_string(s_Active.size()) + " moving liquid pixels from the saved game.");
		}
		s_PendingLoadState.clear();
	}
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	if (!terrain || !s_Enabled) {
		std::scoped_lock lock(s_QueueMutex);
		s_Pours.clear();
		s_Disturbances.clear();
		s_Active.clear();
		return;
	}
	if (!s_TablesBuilt) {
		BuildTables();
	}
	int width = terrain->GetBitmap()->w;
	int height = terrain->GetBitmap()->h;
	s_Width = width;

	std::vector<PourRequest> pours;
	std::vector<std::pair<glm::ivec2, int>> disturbances;
	{
		std::scoped_lock lock(s_QueueMutex);
		pours.swap(s_Pours);
		disturbances.swap(s_Disturbances);
	}
	std::sort(pours.begin(), pours.end(), [](const PourRequest& a, const PourRequest& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Radius < b.Radius); });
	for (const PourRequest& pour: pours) {
		int material = s_MaterialOf[static_cast<int>(pour.Kind)];
		if (material == 0) {
			continue;
		}
		for (int dy = -pour.Radius; dy <= pour.Radius; ++dy) {
			for (int dx = -pour.Radius; dx <= pour.Radius; ++dx) {
				int x = pour.X + dx;
				int y = pour.Y + dy;
				if (dx * dx + dy * dy <= pour.Radius * pour.Radius && InWorld(x, y, width, height) && terrain->GetMaterialPixel(x, y) == g_MaterialAir) {
					terrain->SetMaterialPixel(x, y, material);
					terrain->SetFGColorPixel(x, y, s_ColorOf[static_cast<int>(pour.Kind)]);
					Activate(x, y, width, height, terrain);
				}
			}
		}
	}
	std::sort(disturbances.begin(), disturbances.end(), [](const auto& a, const auto& b) { return a.first.y != b.first.y ? a.first.y < b.first.y : (a.first.x != b.first.x ? a.first.x < b.first.x : a.second < b.second); });
	for (const auto& [center, radius]: disturbances) {
		for (int dy = -radius; dy <= radius; ++dy) {
			for (int dx = -radius; dx <= radius; ++dx) {
				Activate(center.x + dx, center.y + dy, width, height, terrain);
			}
		}
	}
	if (s_Active.empty()) {
		return;
	}

	long long simUpdate = g_TimerMan.GetSimUpdateCount();
	// Bottom to top, so a column of liquid falls together instead of one pixel per step.
	std::vector<int> keys;
	keys.reserve(s_Active.size());
	for (auto it = s_Active.rbegin(); it != s_Active.rend(); ++it) {
		keys.push_back(it->first);
	}
	std::vector<int> settled;
	std::vector<glm::ivec2> hurtSpots;
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

		// Reactions with neighbours.
		static constexpr int neighbours[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
		bool reacted = false;
		for (const auto& offset: neighbours) {
			int nx = x + offset[0];
			int ny = y + offset[1];
			if (!InWorld(nx, ny, width, height)) {
				continue;
			}
			int neighbourMaterial = terrain->GetMaterialPixel(nx, ny);
			Liquid neighbourKind = s_Kinds[static_cast<unsigned char>(neighbourMaterial)];
			if (kind == Liquid::Lava && neighbourKind == Liquid::Water && s_StoneMaterial) {
				// Lava meeting water: the lava sets to stone and the water boils off in a puff of steam.
				terrain->SetMaterialPixel(x, y, s_StoneMaterial);
				terrain->SetFGColorPixel(x, y, s_StoneColor);
				terrain->SetMaterialPixel(nx, ny, g_MaterialAir);
				terrain->SetFGColorPixel(nx, ny, ColorKeys::g_MaskColor);
				ActivateAround(nx, ny, width, height, terrain);
				EffectsParticles::SpawnExplosion(Vector(static_cast<float>(nx), static_cast<float>(ny)), 520.0F);
				reacted = true;
				break;
			}
			if (kind == Liquid::Lava && TerrainFire::IsFlammable(neighbourMaterial) && Random01() < 0.2F) {
				TerrainFire::QueueIgnite(nx, ny);
			}
			if (kind == Liquid::Water) {
				TerrainFire::Extinguish(nx, ny);
			}
			if (kind == Liquid::Acid && neighbourKind == Liquid::None && neighbourMaterial != g_MaterialAir) {
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
		if (kind == Liquid::Water) {
			TerrainFire::Extinguish(x, y);
		}
		if (reacted) {
			settled.push_back(key);
			continue;
		}
		if (kind == Liquid::Lava && y > 0 && terrain->GetMaterialPixel(x, y - 1) == g_MaterialAir && Random01() < 0.01F) {
			hurtSpots.emplace_back(x, y - 1);
		}

		auto canMoveTo = [&](int tx, int ty) {
			return InWorld(tx, ty, width, height) && terrain->GetMaterialPixel(tx, ty) == g_MaterialAir;
		};
		int targetX = x;
		int targetY = y;
		bool moved = false;
		if (canMoveTo(x, y + 1)) {
			targetY = y + 1;
			moved = true;
		} else {
			int first = Random01() < 0.5F ? -1 : 1;
			for (int side: {first, -first}) {
				int sx = x + side;
				InWorld(sx, y, width, height);
				if (canMoveTo(sx, y + 1)) {
					targetX = sx;
					targetY = y + 1;
					moved = true;
					break;
				}
			}
			if (!moved) {
				// Run sideways to find the level.
				for (int side: {first, -first}) {
					for (int step = 1; step <= properties.Flow; ++step) {
						int sx = x + side * step;
						if (!canMoveTo(sx, y)) {
							break;
						}
						targetX = sx;
						moved = true;
						InWorld(targetX, targetY, width, height);
						// Stop as soon as there's a way down, so liquid pours off ledges.
						int belowX = targetX;
						int belowY = y + 1;
						if (canMoveTo(belowX, belowY)) {
							break;
						}
					}
					if (moved) {
						break;
					}
				}
			}
		}
		if (moved) {
			// Applied straight away: going bottom to top, the pixel above sees this one already gone and can follow it down in the same step.
			InWorld(targetX, targetY, width, height);
			int target = targetY * width + targetX;
			int material = terrain->GetMaterialPixel(x, y);
			int color = terrain->GetFGColorPixel(x, y);
			terrain->SetMaterialPixel(targetX, targetY, material);
			terrain->SetFGColorPixel(targetX, targetY, color);
			terrain->SetMaterialPixel(x, y, g_MaterialAir);
			terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
			s_Active.erase(key);
			s_Active[target] = 0;
			// Whatever was resting around it may now flow into the gap.
			ActivateAround(x, y, width, height, terrain);
		} else if (++s_Active[key] >= c_RestSteps) {
			settled.push_back(key);
		}
	}

	for (int key: settled) {
		s_Active.erase(key);
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
		for (const auto& [key, still]: s_Active) {
			stream << ' ' << key % width << ' ' << key / width << ' ' << still;
		}
	}
	return stream.str();
}

void FluidSim::SetPendingLoadState(const std::string& state) {
	s_PendingLoadState = state;
}

void FluidSim::Clear() {
	s_Active.clear();
	std::scoped_lock lock(s_QueueMutex);
	s_Pours.clear();
	s_Disturbances.clear();
}

int FluidSim::GetActiveCount() {
	return static_cast<int>(s_Active.size());
}
