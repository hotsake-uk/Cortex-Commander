#include "Temperature.h"
#include "ACraft.h"
#include "ADoor.h"
#include "Actor.h"
#include "Constants.h"
#include "FluidSim.h"
#include "MovableMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "SceneMan.h"
#include "TerrainFire.h"
#include "TimerMan.h"
#include "Vector.h"
#include "WeatherEffects.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

using namespace RTE;

bool Temperature::s_Enabled = true;
bool Temperature::s_HurtsUnits = true;

namespace {
	constexpr int c_Cell = 8; //!< Each cell of the field covers this many pixels a side.
	constexpr int c_Stripes = 8; //!< Each update works through every this many rows of cells, so a cell's turn comes round every this many updates.
	constexpr int c_SamplesPerCell = 4; //!< How many of a cell's pixels are looked at on its turn, for what heats or cools it and what changes phase.
	constexpr int c_UnitEvery = 15; //!< Units feel the heat and cold every this many updates.

	constexpr float c_FireHeat = 110.0F; //!< What burning terrain warms its cell towards.
	constexpr float c_LavaHeat = 120.0F; //!< What lava warms its cell towards.
	constexpr float c_ChillCold = -60.0F; //!< What cryogenic fluid cools its cell towards.
	constexpr float c_Freezes = 0.0F; //!< Water freezes below this.
	constexpr float c_Melts = 20.0F; //!< Ice and snow melt above this: warmer than mild weather, so a snowy map's ice keeps unless something heats it.
	constexpr float c_Boils = 100.0F; //!< Water boils above this.
	constexpr float c_LavaCrusts = 40.0F; //!< Lava with air colder than this over it crusts over into stone.
	constexpr float c_Ignites = 115.0F; //!< What burns catches fire above this: hotter than a fire makes its own cell, so heat alone doesn't spread fire.
	constexpr float c_Burns = 60.0F; //!< Units take burns above this.
	constexpr float c_Frostbite = -40.0F; //!< Units take frostbite below this.
	constexpr float c_SlowsWalking = -10.0F; //!< Units walk slower below this.

	std::vector<float> s_Grid; //!< Degrees Celsius per cell, row by row.
	int s_GridWidth = 0;
	int s_GridHeight = 0;
	const void* s_Scene = nullptr;
	unsigned int s_SceneGeneration = 0;

	struct HeatRequest {
		int X;
		int Y;
		float Degrees;
	};
	std::mutex s_QueueMutex;
	std::vector<HeatRequest> s_HeatQueue; //!< Heat added from scripts, applied on the next update.

	unsigned int MixBits(unsigned int value) {
		value ^= value >> 16;
		value *= 0x7FEB352Du;
		value ^= value >> 15;
		value *= 0x846CA68Bu;
		value ^= value >> 16;
		return value;
	}

	/// The cell a pixel is in, wrapping sideways on a scene that wraps, or -1 off the scene.
	int CellAt(int x, int y) {
		if (s_GridWidth <= 0) {
			return -1;
		}
		int cellX = static_cast<int>(std::floor(static_cast<float>(x) / static_cast<float>(c_Cell)));
		int cellY = static_cast<int>(std::floor(static_cast<float>(y) / static_cast<float>(c_Cell)));
		if (g_SceneMan.SceneWrapsX()) {
			cellX = ((cellX % s_GridWidth) + s_GridWidth) % s_GridWidth;
		}
		if (cellX < 0 || cellY < 0 || cellX >= s_GridWidth || cellY >= s_GridHeight) {
			return -1;
		}
		return cellY * s_GridWidth + cellX;
	}

	void Warm(int cell, float degrees) {
		if (cell >= 0) {
			s_Grid[cell] = std::clamp(s_Grid[cell] + degrees, -100.0F, 400.0F);
		}
	}

	/// Units feel the temperature where they stand: burns in great heat, frostbite in great cold. Machines take half.
	void HurtUnits() {
		for (Actor* actor: g_MovableMan.GetActorList()) {
			if (!actor || actor->IsDead() || actor->GetHealth() <= 0.0F || dynamic_cast<const ADoor*>(actor) || dynamic_cast<const ACraft*>(actor)) {
				continue;
			}
			float temperature = Temperature::GetTemperature(actor->GetPos());
			float damage = 0.0F;
			if (temperature > c_Burns) {
				damage = (temperature - c_Burns) * 0.02F;
			} else if (temperature < c_Frostbite) {
				damage = (c_Frostbite - temperature) * 0.01F;
			}
			if (damage > 0.0F) {
				actor->AddHealth(-(actor->GetMetalness() >= 0.2F ? damage * 0.5F : damage));
			}
		}
	}
} // namespace

void Temperature::SetEnabled(bool enabled) {
	if (s_Enabled != enabled) {
		s_Enabled = enabled;
		Clear();
	}
}

float Temperature::GetAmbient() {
	return 15.0F - 30.0F * WeatherEffects::GetSnow() - 5.0F * WeatherEffects::GetRain();
}

float Temperature::GetTemperature(const Vector& position) {
	if (!s_Enabled || s_Grid.empty()) {
		return GetAmbient();
	}
	int cell = CellAt(position.GetFloorIntX(), position.GetFloorIntY());
	return cell >= 0 ? s_Grid[cell] : GetAmbient();
}

void Temperature::AddHeat(const Vector& position, float degrees) {
	if (!s_Enabled || !std::isfinite(degrees)) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	if (s_HeatQueue.size() < 10000) {
		s_HeatQueue.push_back({position.GetFloorIntX(), position.GetFloorIntY(), degrees});
	}
}

float Temperature::GetWalkSpeedMultiplier(const Actor* actor) {
	if (!s_Enabled || !actor) {
		return 1.0F;
	}
	float temperature = GetTemperature(actor->GetPos());
	return temperature < c_SlowsWalking ? std::max(0.5F, 1.0F + (temperature - c_SlowsWalking) / 100.0F) : 1.0F;
}

void Temperature::Clear() {
	s_Grid.clear();
	s_GridWidth = 0;
	s_GridHeight = 0;
	std::scoped_lock lock(s_QueueMutex);
	s_HeatQueue.clear();
}

void Temperature::Update() {
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	if (!s_Enabled || !terrain) {
		Clear();
		return;
	}
	if (scene != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		Clear();
		s_Scene = scene;
		s_SceneGeneration = g_SceneMan.GetSceneGeneration();
	}
	BITMAP* materialBitmap = terrain->GetBitmap();
	int width = materialBitmap->w;
	int height = materialBitmap->h;
	float ambient = GetAmbient();
	if (int gridWidth = (width + c_Cell - 1) / c_Cell, gridHeight = (height + c_Cell - 1) / c_Cell; gridWidth != s_GridWidth || gridHeight != s_GridHeight || s_Grid.empty()) {
		s_GridWidth = gridWidth;
		s_GridHeight = gridHeight;
		s_Grid.assign(static_cast<size_t>(gridWidth) * static_cast<size_t>(gridHeight), ambient);
	}
	bool wrapsX = g_SceneMan.SceneWrapsX();

	// Heat from scripts. (Adding is the same in any order, so the order they came in from their threads doesn't matter.)
	std::vector<HeatRequest> requests;
	{
		std::scoped_lock lock(s_QueueMutex);
		requests.swap(s_HeatQueue);
	}
	for (const HeatRequest& request: requests) {
		Warm(CellAt(request.X, request.Y), request.Degrees);
		for (int side = 0; side < 4; ++side) {
			Warm(CellAt(request.X + (side == 0 ? c_Cell : side == 1 ? -c_Cell : 0), request.Y + (side == 2 ? c_Cell : side == 3 ? -c_Cell : 0)), request.Degrees * 0.5F);
		}
	}

	// Burning terrain warms its cell.
	TerrainFire::VisitBurning([](int x, int y) {
		if (int cell = CellAt(x, y); cell >= 0 && s_Grid[cell] < c_FireHeat) {
			s_Grid[cell] = std::min(c_FireHeat, s_Grid[cell] + (c_FireHeat - s_Grid[cell]) * 0.05F);
		}
	});

	long long update = g_TimerMan.GetSimUpdateCount();
	int stripe = static_cast<int>(update % c_Stripes);
	unsigned int turn = static_cast<unsigned int>(update / c_Stripes);
	bool fireOn = TerrainFire::IsEnabled();
	for (int cellY = stripe; cellY < s_GridHeight; cellY += c_Stripes) {
		for (int cellX = 0; cellX < s_GridWidth; ++cellX) {
			int cell = cellY * s_GridWidth + cellX;
			float temperature = s_Grid[cell];

			// What is in the cell: a few of its pixels, picked afresh each turn.
			int samples[c_SamplesPerCell][2];
			float chances[c_SamplesPerCell];
			int sampled = 0;
			int lava = 0;
			int chill = 0;
			int air = 0;
			for (int i = 0; i < c_SamplesPerCell; ++i) {
				unsigned int hash = MixBits(static_cast<unsigned int>(cellX) * 73856093u ^ static_cast<unsigned int>(cellY) * 19349663u ^ turn * 83492791u ^ static_cast<unsigned int>(i) * 2654435761u);
				int x = cellX * c_Cell + static_cast<int>(hash & 7u);
				int y = cellY * c_Cell + static_cast<int>((hash >> 3) & 7u);
				if (x >= width || y >= height) {
					continue;
				}
				int material = materialBitmap->line[y][x];
				lava += FluidSim::IsLava(material) ? 1 : 0;
				chill += FluidSim::Chills(material) ? 1 : 0;
				air += material == g_MaterialAir ? 1 : 0;
				samples[sampled][0] = x;
				samples[sampled][1] = y;
				chances[sampled] = static_cast<float>((hash >> 8) & 0xFFFFu) / 65536.0F;
				++sampled;
			}
			if (lava > 0) {
				temperature += (c_LavaHeat - temperature) * static_cast<float>(lava) / 16.0F;
			}
			if (chill > 0) {
				temperature += (c_ChillCold - temperature) * static_cast<float>(chill) / 8.0F;
			}

			// Heat spreads to the cells around, and everything settles towards the weather's temperature: quickest where there is air to carry it.
			int left = cellX > 0 ? cell - 1 : (wrapsX ? cell + s_GridWidth - 1 : cell);
			int right = cellX + 1 < s_GridWidth ? cell + 1 : (wrapsX ? cell - s_GridWidth + 1 : cell);
			int up = cellY > 0 ? cell - s_GridWidth : cell;
			int down = cellY + 1 < s_GridHeight ? cell + s_GridWidth : cell;
			float around = (s_Grid[left] + s_Grid[right] + s_Grid[up] + s_Grid[down]) * 0.25F;
			temperature += (around - temperature) * 0.25F;
			temperature += (ambient - temperature) * (air > 0 ? 1.0F / 64.0F : 1.0F / 256.0F);
			s_Grid[cell] = temperature;
			float airAbove = cellY > 0 ? s_Grid[cell - s_GridWidth] : ambient;

			// What the temperature does to the pixels looked at.
			for (int i = 0; i < sampled; ++i) {
				int x = samples[i][0];
				int y = samples[i][1];
				float chance = chances[i];
				int material = materialBitmap->line[y][x];
				if (material == g_MaterialAir) {
					continue;
				}
				int above = y > 0 ? materialBitmap->line[y - 1][x] : g_MaterialAir;
				if (int freezesTo = FluidSim::FreezesTo(material); freezesTo != 0 && temperature < c_Freezes) {
					// Still water freezes over from the top, and the ice thickens down from there more slowly.
					float rate = std::min(0.5F, (c_Freezes - temperature) / 20.0F);
					if ((above == g_MaterialAir && chance < rate) || (above == freezesTo && chance < rate * 0.25F)) {
						FluidSim::ChangeMaterialAt(x, y, freezesTo);
					}
				} else if (int meltsTo = FluidSim::MeltsTo(material); meltsTo != 0 && temperature > c_Melts) {
					if (chance < std::min(0.5F, (temperature - c_Melts) / 30.0F)) {
						FluidSim::ChangeMaterialAt(x, y, meltsTo);
					}
				} else if (int boilsTo = FluidSim::BoilsTo(material); boilsTo != 0 && temperature > c_Boils && FluidSim::IsLiquid(material)) {
					// (0 is "boils to nothing"; -1 is air, as FluidSim's own boil reaction reads it.)
					boilsTo = std::max(boilsTo, static_cast<int>(g_MaterialAir));
					if (chance < std::min(0.5F, (temperature - c_Boils) / 40.0F)) {
						FluidSim::ChangeMaterialAt(x, y, boilsTo);
						if (boilsTo == g_MaterialAir && chance < 0.25F) {
							TerrainFire::SpawnSteam(Vector(static_cast<float>(x), static_cast<float>(y)), 1);
						}
					}
				} else if (int settlesTo = FluidSim::SettlesTo(material); settlesTo != 0 && FluidSim::IsLava(material) && above == g_MaterialAir && airAbove < c_LavaCrusts) {
					// Lava's surface crusts over where the air over it is cool: in the cold, and in rain, quicker.
					if (chance < (c_LavaCrusts - airAbove) / 800.0F) {
						FluidSim::ChangeMaterialAt(x, y, settlesTo);
					}
				} else if (fireOn && temperature > c_Ignites && chance < 0.3F && TerrainFire::IsFlammable(material)) {
					TerrainFire::QueueIgnite(x, y);
				}
			}
		}
	}

	if (s_HurtsUnits && update % c_UnitEvery == 0) {
		HurtUnits();
	}
}
