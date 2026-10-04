#include "TerrainFire.h"
#include "Constants.h"
#include "ConsoleMan.h"
#include "Material.h"
#include "MovableMan.h"
#include "MovableObject.h"
#include "MOPixel.h"
#include "PostProcessMan.h"
#include "PresetMan.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "TimerMan.h"
#include "Vector.h"
#include "EffectsParticles.h"
#include "WeatherEffects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <sstream>
#include <unordered_map>

using namespace RTE;

bool TerrainFire::s_Enabled = true;

namespace {
	enum class Fuel : unsigned char {
		None,
		Grass, //!< Grass, vegetation, leaves: flares up and burns away quickly.
		Wood, //!< Wood, cloth, rubber: burns slowly from the surface in, leaving ash.
		Oil //!< Burns fast and hot, spreads eagerly.
	};

	struct FuelProperties {
		int MinTicks, MaxTicks; //!< How long a pixel burns, in fire ticks.
		float Spread; //!< Chance per tick of setting each flammable neighbour alight.
		bool LeavesAsh;
	};

	constexpr FuelProperties c_Fuels[] = {
	    {0, 0, 0.0F, false}, // None
	    {25, 50, 0.25F, false}, // Grass
	    {40, 90, 0.07F, true}, // Wood
	    {20, 40, 0.5F, false}, // Oil
	};

	constexpr int c_TickInterval = 3; //!< Sim updates per fire tick (about 20 per second).
	constexpr size_t c_MaxBurning = 6000;

	struct BurningPixel {
		int X, Y;
		short TicksLeft;
		short TotalTicks;
		Fuel Kind;
	};

	std::array<Fuel, 256> s_FuelTable{};
	bool s_FuelTableBuilt = false;
	int s_AshMaterial = -1;
	int s_AshColor = 0;

	std::map<int, BurningPixel> s_Burning; //!< Keyed by y * width + x, so iteration order is deterministic.
	std::vector<std::pair<int, int>> s_IgniteQueue;
	std::vector<std::pair<glm::vec2, float>> s_AreaQueue;
	std::vector<glm::ivec3> s_DouseQueue; //!< x, y, radius.
	std::array<bool, 256> s_DousingTable{};
	int s_WaterColor = -1; //!< Palette index water is drawn with.
	std::mutex s_QueueMutex;
	std::unordered_map<std::string, bool> s_FireSourceCache;
	std::mutex s_FireSourceMutex;

	const void* s_Scene = nullptr;
	std::string s_PendingLoadState; //!< Saved fire to restore when the loaded scene starts.
	const void* s_MaterialBitmap = nullptr;
	long long s_LastTickUpdate = -1;
	unsigned int s_Random = 0x2545F491u; //!< The fire's own deterministic random state, reset per scene.
	unsigned int s_LightFlicker = 0x9E3779B9u; //!< Render only: flicker of the fire's lights.
	struct FireLight {
		glm::vec2 Position;
		float Radius;
		float Intensity;
	};
	std::vector<FireLight> s_Lights; //!< Lights of the fire as of its last tick, registered every sim update (scene lights are cleared every update).

	float Random01(unsigned int& state) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return static_cast<float>(state & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	void RegisterLights() {
		for (const FireLight& light: s_Lights) {
			float flicker = 0.75F + 0.25F * Random01(s_LightFlicker);
			g_PostProcessMan.AddLight(Vector(light.Position.x, light.Position.y), light.Radius, 255.0F, 130.0F, 45.0F, light.Intensity * flicker);
		}
	}

	bool Contains(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }

	void BuildFuelTable() {
		s_FuelTable.fill(Fuel::None);
		s_DousingTable.fill(false);
		s_AshMaterial = -1;
		for (int id = 1; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (!material || material->GetIndex() != id) {
				continue;
			}
			const std::string& name = material->GetPresetName();
			s_DousingTable[id] = name == "Water";
			if (name == "Water") {
				Color waterColor = material->GetColor();
				waterColor.RecalculateIndex();
				s_WaterColor = waterColor.GetIndex();
			}
			if (Contains(name, "Ash")) {
				s_AshMaterial = id;
				Color ashColor = material->GetColor();
				ashColor.RecalculateIndex();
				s_AshColor = ashColor.GetIndex();
			} else if (Contains(name, "Oil")) {
				s_FuelTable[id] = Fuel::Oil;
			} else if (Contains(name, "Grass") || Contains(name, "Vegetation") || Contains(name, "Leaf") || Contains(name, "Leaves") || Contains(name, "Hay") || Contains(name, "Straw") || Contains(name, "Plant")) {
				s_FuelTable[id] = Fuel::Grass;
			} else if (Contains(name, "Wood") || Contains(name, "Cloth") || Contains(name, "Rubber") || Contains(name, "Timber")) {
				s_FuelTable[id] = Fuel::Wood;
			}
		}
		s_FuelTableBuilt = true;
	}

	SLTerrain* CurrentTerrain() {
		Scene* scene = g_SceneMan.GetScene();
		return scene ? scene->GetTerrain() : nullptr;
	}

	bool WrapPixel(int& x, int& y, int width, int height) {
		if (g_SceneMan.SceneWrapsX()) {
			x = ((x % width) + width) % width;
		}
		if (g_SceneMan.SceneWrapsY()) {
			y = ((y % height) + height) % height;
		}
		return x >= 0 && y >= 0 && x < width && y < height;
	}

	/// Fire needs air: a pixel only catches if it touches air (or ash, which lets air through).
	bool IsExposed(const SLTerrain* terrain, int x, int y, int width, int height) {
		static constexpr int offsets[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
		for (const auto& offset: offsets) {
			int nx = x + offset[0];
			int ny = y + offset[1];
			if (!WrapPixel(nx, ny, width, height)) {
				return true;
			}
			int material = terrain->GetMaterialPixel(nx, ny);
			if (material == g_MaterialAir || material == s_AshMaterial) {
				return true;
			}
		}
		return false;
	}

	void TryIgnite(SLTerrain* terrain, int x, int y, int width, int height) {
		if (s_Burning.size() >= c_MaxBurning || !WrapPixel(x, y, width, height)) {
			return;
		}
		int key = y * width + x;
		if (s_Burning.count(key)) {
			return;
		}
		Fuel kind = s_FuelTable[static_cast<unsigned char>(terrain->GetMaterialPixel(x, y))];
		if (kind == Fuel::None || !IsExposed(terrain, x, y, width, height)) {
			return;
		}
		const FuelProperties& fuel = c_Fuels[static_cast<int>(kind)];
		short ticks = static_cast<short>(fuel.MinTicks + static_cast<int>(Random01(s_Random) * static_cast<float>(fuel.MaxTicks - fuel.MinTicks + 1)));
		s_Burning.emplace(key, BurningPixel{x, y, ticks, ticks, kind});
	}

	MovableObject* CreateEffect(const char* className, const char* presetName) {
		const Entity* preset = g_PresetMan.GetEntityPreset(className, presetName, "Base.rte");
		return preset ? dynamic_cast<MovableObject*>(preset->Clone()) : nullptr;
	}
} // namespace

bool TerrainFire::IsFlammable(int materialID) {
	return s_FuelTableBuilt && materialID > 0 && materialID < 256 && s_FuelTable[materialID] != Fuel::None;
}

bool TerrainFire::IsFireSource(const MovableObject* object) {
	if (!object) {
		return false;
	}
	const std::string& name = object->GetPresetName();
	std::scoped_lock lock(s_FireSourceMutex);
	auto found = s_FireSourceCache.find(name);
	if (found != s_FireSourceCache.end()) {
		return found->second;
	}
	bool source = Contains(name, "Fire") || Contains(name, "Flame") || Contains(name, "Napalm") || Contains(name, "Burn") || Contains(name, "Incendi") || Contains(name, "Ember");
	s_FireSourceCache.emplace(name, source);
	return source;
}

bool TerrainFire::IsBurningNear(const Vector& position, int radius) {
	SLTerrain* terrain = CurrentTerrain();
	if (s_Burning.empty() || !terrain) {
		return false;
	}
	int width = terrain->GetBitmap()->w;
	int height = terrain->GetBitmap()->h;
	int centerX = position.GetFloorIntX();
	int centerY = position.GetFloorIntY();
	for (int dy = -radius; dy <= radius; ++dy) {
		for (int dx = -radius; dx <= radius; ++dx) {
			int x = centerX + dx;
			int y = centerY + dy;
			if (WrapPixel(x, y, width, height) && s_Burning.count(y * width + x)) {
				return true;
			}
		}
	}
	return false;
}

void TerrainFire::SpawnSteam(const Vector& position, int count) {
	for (int i = 0; i < count; ++i) {
		if (MovableObject* steam = CreateEffect("MOSParticle", "Steam Puff")) {
			steam->SetPos(position + Vector((Random01(s_Random) - 0.5F) * 6.0F, -2.0F - Random01(s_Random) * 4.0F));
			steam->SetVel(Vector((Random01(s_Random) - 0.5F) * 1.5F, -1.0F - Random01(s_Random) * 1.5F));
			g_MovableMan.AddParticle(steam);
		}
	}
}

bool TerrainFire::IsDousing(int materialID) {
	return s_FuelTableBuilt && materialID > 0 && materialID < 256 && s_DousingTable[materialID];
}

bool TerrainFire::IsDousingParticle(const MovableObject* particle, const Material* material) {
	if (!material || !IsDousing(material->GetIndex())) {
		return false;
	}
	const MOPixel* pixel = dynamic_cast<const MOPixel*>(particle);
	// Only drops of water: blood sprays are water-material sprites too.
	return pixel && pixel->GetColor().GetIndex() == s_WaterColor;
}

void TerrainFire::QueueDouse(int x, int y, int radius) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	if (s_DouseQueue.size() < 4096) {
		s_DouseQueue.emplace_back(x, y, radius);
	}
}

void TerrainFire::QueueIgnite(int x, int y) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	if (s_IgniteQueue.size() < 4096) {
		s_IgniteQueue.emplace_back(x, y);
	}
}

void TerrainFire::QueueIgniteArea(const Vector& position, float radius) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	s_AreaQueue.emplace_back(glm::vec2(position.m_X, position.m_Y), radius);
}

void TerrainFire::Update() {
	SLTerrain* terrain = CurrentTerrain();
	const void* materialBitmap = terrain ? terrain->GetMaterialBitmap() : nullptr;
	if (g_SceneMan.GetScene() != s_Scene || materialBitmap != s_MaterialBitmap) {
		// New scene: start over, deterministically.
		Clear();
		s_Scene = g_SceneMan.GetScene();
		s_MaterialBitmap = materialBitmap;
		s_Random = 0x2545F491u;
		s_FuelTableBuilt = false;
		if (!s_PendingLoadState.empty() && s_Scene) {
			// Restore a saved game's fire: "x y ticksLeft totalTicks kind" per burning pixel, then the random state.
			std::istringstream stream(s_PendingLoadState);
			unsigned int random = 0;
			stream >> random;
			if (random != 0) {
				s_Random = random;
			}
			SLTerrain* loadedTerrain = CurrentTerrain();
			int loadedWidth = loadedTerrain ? loadedTerrain->GetBitmap()->w : 0;
			int x = 0;
			int y = 0;
			int ticksLeft = 0;
			int totalTicks = 0;
			int kind = 0;
			while (loadedWidth > 0 && stream >> x >> y >> ticksLeft >> totalTicks >> kind) {
				if (kind > 0 && kind <= static_cast<int>(Fuel::Oil) && s_Burning.size() < c_MaxBurning) {
					s_Burning.emplace(y * loadedWidth + x, BurningPixel{x, y, static_cast<short>(ticksLeft), static_cast<short>(totalTicks), static_cast<Fuel>(kind)});
				}
			}
			g_ConsoleMan.PrintString("SYSTEM: Restored " + std::to_string(s_Burning.size()) + " burning terrain pixels from the saved game.");
		}
		s_PendingLoadState.clear();
	}
	if (!terrain || !s_Enabled) {
		std::scoped_lock lock(s_QueueMutex);
		s_IgniteQueue.clear();
		s_AreaQueue.clear();
		s_DouseQueue.clear();
		s_Burning.clear();
		return;
	}
	if (!s_FuelTableBuilt) {
		BuildFuelTable();
	}
	long long simUpdate = g_TimerMan.GetSimUpdateCount();
	if (s_LastTickUpdate >= 0 && simUpdate - s_LastTickUpdate < c_TickInterval) {
		RegisterLights();
		return;
	}
	s_LastTickUpdate = simUpdate;

	int width = terrain->GetBitmap()->w;
	int height = terrain->GetBitmap()->h;

	// Apply queued ignitions in a fixed order, whatever order the (possibly parallel) collision code queued them in.
	std::vector<std::pair<int, int>> ignitions;
	std::vector<std::pair<glm::vec2, float>> areas;
	std::vector<glm::ivec3> douses;
	{
		std::scoped_lock lock(s_QueueMutex);
		ignitions.swap(s_IgniteQueue);
		areas.swap(s_AreaQueue);
		douses.swap(s_DouseQueue);
	}
	std::sort(ignitions.begin(), ignitions.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
	ignitions.erase(std::unique(ignitions.begin(), ignitions.end()), ignitions.end());
	for (const auto& [x, y]: ignitions) {
		TryIgnite(terrain, x, y, width, height);
	}
	std::sort(areas.begin(), areas.end(), [](const auto& a, const auto& b) { return a.first.y != b.first.y ? a.first.y < b.first.y : (a.first.x != b.first.x ? a.first.x < b.first.x : a.second < b.second); });
	for (const auto& [center, radius]: areas) {
		int reach = static_cast<int>(radius);
		int centerX = static_cast<int>(std::floor(center.x));
		int centerY = static_cast<int>(std::floor(center.y));
		for (int dy = -reach; dy <= reach; dy += 2) {
			for (int dx = -reach; dx <= reach; dx += 2) {
				if (dx * dx + dy * dy <= reach * reach && Random01(s_Random) < 0.35F) {
					TryIgnite(terrain, centerX + dx, centerY + dy, width, height);
				}
			}
		}
	}
	// Water last, so it wins over anything set alight at the same time.
	std::sort(douses.begin(), douses.end(), [](const glm::ivec3& a, const glm::ivec3& b) { return a.y != b.y ? a.y < b.y : (a.x != b.x ? a.x < b.x : a.z < b.z); });
	for (const glm::ivec3& douse: douses) {
		if (s_Burning.empty()) {
			break;
		}
		size_t putOut = 0;
		for (int dy = -douse.z; dy <= douse.z; ++dy) {
			for (int dx = -douse.z; dx <= douse.z; ++dx) {
				int x = douse.x + dx;
				int y = douse.y + dy;
				if (dx * dx + dy * dy <= douse.z * douse.z && WrapPixel(x, y, width, height)) {
					putOut += s_Burning.erase(y * width + x);
				}
			}
		}
		// Water on fire hisses into steam.
		if (putOut > 0 && Random01(s_Random) < 0.5F) {
			SpawnSteam(Vector(static_cast<float>(douse.x), static_cast<float>(douse.y)), 1);
		}
	}
	if (s_Burning.empty()) {
		s_Lights.clear();
		return;
	}

	// Spread to neighbours (fire climbs: up is likelier than sideways, down least, and downwind likelier than upwind), then burn down.
	// Rain and snow damp it: it spreads less and burns out sooner. In dry wind, embers carry it a little way downwind.
	static constexpr int neighbours[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
	float rain = WeatherEffects::GetRain();
	float snow = WeatherEffects::GetSnow();
	float wind = WeatherEffects::GetWind();
	float damping = std::max(1.0F - 0.75F * rain - 0.35F * snow, 0.1F);
	const float directionScale[4] = {2.2F, 1.0F - 0.7F * std::max(wind, 0.0F) + 0.8F * std::max(-wind, 0.0F), 1.0F + 0.8F * std::max(wind, 0.0F) - 0.7F * std::max(-wind, 0.0F), 0.4F};
	float emberChance = (rain == 0.0F && snow == 0.0F) ? 0.006F * std::abs(wind) : 0.0F;
	float quenchChance = 0.3F * rain + 0.1F * snow;
	std::vector<std::pair<int, int>> spreadTo;
	std::vector<int> burntOut;
	std::vector<int> goneOut;
	for (auto& [key, pixel]: s_Burning) {
		if (s_FuelTable[static_cast<unsigned char>(terrain->GetMaterialPixel(pixel.X, pixel.Y))] == Fuel::None) {
			// The fuel flowed or was blown away.
			goneOut.push_back(key);
			continue;
		}
		const FuelProperties& fuel = c_Fuels[static_cast<int>(pixel.Kind)];
		for (int i = 0; i < 4; ++i) {
			if (Random01(s_Random) < fuel.Spread * directionScale[i] * damping) {
				spreadTo.emplace_back(pixel.X + neighbours[i][0], pixel.Y + neighbours[i][1]);
			}
		}
		if (emberChance > 0.0F && Random01(s_Random) < emberChance) {
			int distance = 3 + static_cast<int>(Random01(s_Random) * 9.0F);
			spreadTo.emplace_back(pixel.X + (wind > 0.0F ? distance : -distance), pixel.Y - static_cast<int>(Random01(s_Random) * 5.0F));
		}
		if (quenchChance > 0.0F && Random01(s_Random) < quenchChance) {
			--pixel.TicksLeft;
		}
		if (--pixel.TicksLeft <= 0) {
			burntOut.push_back(key);
		}
	}

	for (int key: goneOut) {
		s_Burning.erase(key);
	}

	// Burnt out pixels become air or ash.
	int minX = width;
	int minY = height;
	int maxX = -1;
	int maxY = -1;
	for (int key: burntOut) {
		const BurningPixel& pixel = s_Burning[key];
		bool ash = c_Fuels[static_cast<int>(pixel.Kind)].LeavesAsh && s_AshMaterial > 0;
		terrain->SetMaterialPixel(pixel.X, pixel.Y, ash ? s_AshMaterial : g_MaterialAir);
		terrain->SetFGColorPixel(pixel.X, pixel.Y, ash ? s_AshColor : ColorKeys::g_MaskColor);
		minX = std::min(minX, pixel.X);
		minY = std::min(minY, pixel.Y);
		maxX = std::max(maxX, pixel.X);
		maxY = std::max(maxY, pixel.Y);
		s_Burning.erase(key);
	}
	if (maxX >= 0) {
		// Let the pathfinder know the terrain changed.
		terrain->AddUpdatedMaterialArea(Box(Vector(static_cast<float>(minX), static_cast<float>(minY)), static_cast<float>(maxX - minX + 1), static_cast<float>(maxY - minY + 1)));
	}
	for (const auto& [x, y]: spreadTo) {
		TryIgnite(terrain, x, y, width, height);
	}

	// Group burning pixels into cells for flames, smoke and light. std::map keeps this ordered and deterministic.
	constexpr int cellSize = 24;
	std::map<int, std::pair<glm::ivec2, int>> cells;
	for (const auto& [key, pixel]: s_Burning) {
		int cellKey = (pixel.Y / cellSize) * ((width + cellSize - 1) / cellSize) + pixel.X / cellSize;
		auto& cell = cells[cellKey];
		if (cell.second == 0) {
			cell.first = glm::ivec2(pixel.X, pixel.Y);
		}
		cell.second++;
	}
	s_Lights.clear();
	for (const auto& [cellKey, cell]: cells) {
		const glm::ivec2& position = cell.first;
		int count = cell.second;
		// Flames that hurt whatever stands in them.
		if (count >= 2 && Random01(s_Random) < 0.25F) {
			if (MovableObject* flame = CreateEffect("MOPixel", "Flame Hurt Particle")) {
				flame->SetPos(Vector(static_cast<float>(position.x), static_cast<float>(position.y - 2)));
				flame->SetVel(Vector((Random01(s_Random) - 0.5F) * 2.0F, -2.0F - Random01(s_Random) * 3.0F));
				g_MovableMan.AddParticle(flame);
			}
		}
		// Smoke rising from bigger fires.
		if (count >= 4 && Random01(s_Random) < 0.2F) {
			if (MovableObject* smoke = CreateEffect("MOSParticle", "Tiny Smoke Ball 1")) {
				smoke->SetPos(Vector(static_cast<float>(position.x), static_cast<float>(position.y - 3)));
				smoke->SetVel(Vector((Random01(s_Random) - 0.5F) * 1.0F, -1.0F - Random01(s_Random)));
				g_MovableMan.AddParticle(smoke);
			}
		}
		// Light (render only, so it may flicker with its own random numbers).
		if (s_Lights.size() < 48 && count >= 2) {
			s_Lights.push_back({glm::vec2(static_cast<float>(position.x), static_cast<float>(position.y - 4)), std::min(40.0F + static_cast<float>(count) * 2.0F, 130.0F), std::min(0.6F + static_cast<float>(count) * 0.05F, 1.6F)});
		}
	}
	RegisterLights();
}

void TerrainFire::Extinguish(int x, int y) {
	if (s_Burning.empty()) {
		return;
	}
	SLTerrain* terrain = CurrentTerrain();
	if (!terrain) {
		return;
	}
	int width = terrain->GetBitmap()->w;
	int height = terrain->GetBitmap()->h;
	if (WrapPixel(x, y, width, height) && s_Burning.erase(y * width + x) > 0 && Random01(s_Random) < 0.08F) {
		SpawnSteam(Vector(static_cast<float>(x), static_cast<float>(y)), 1);
	}
}

void TerrainFire::GetBurning(const glm::vec2& screenOrigin, int width, int height, std::vector<glm::vec3>& burning) {
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	bool wraps = g_SceneMan.SceneWrapsX();
	for (const auto& [key, pixel]: s_Burning) {
		glm::vec2 position = glm::vec2(static_cast<float>(pixel.X), static_cast<float>(pixel.Y)) - screenOrigin;
		if (wraps) {
			if (position.x < -sceneWidth * 0.5F) {
				position.x += sceneWidth;
			} else if (position.x > sceneWidth * 0.5F) {
				position.x -= sceneWidth;
			}
		}
		if (position.x < -2.0F || position.y < -2.0F || position.x > static_cast<float>(width) + 2.0F || position.y > static_cast<float>(height) + 2.0F) {
			continue;
		}
		float heat = static_cast<float>(pixel.TicksLeft) / static_cast<float>(std::max<short>(pixel.TotalTicks, 1));
		burning.emplace_back(position, heat);
	}
}

std::string TerrainFire::GetSaveState() {
	std::ostringstream stream;
	stream << s_Random;
	for (const auto& [key, pixel]: s_Burning) {
		stream << ' ' << pixel.X << ' ' << pixel.Y << ' ' << pixel.TicksLeft << ' ' << pixel.TotalTicks << ' ' << static_cast<int>(pixel.Kind);
	}
	return stream.str();
}

void TerrainFire::SetPendingLoadState(const std::string& state) {
	s_PendingLoadState = state;
}

void TerrainFire::Clear() {
	s_Burning.clear();
	s_Lights.clear();
	s_LastTickUpdate = -1;
	std::scoped_lock lock(s_QueueMutex);
	s_IgniteQueue.clear();
	s_AreaQueue.clear();
	s_DouseQueue.clear();
}

int TerrainFire::GetCount() {
	return static_cast<int>(s_Burning.size());
}
