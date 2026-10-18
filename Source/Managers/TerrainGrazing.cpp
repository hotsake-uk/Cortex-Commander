#include "TerrainGrazing.h"

#include "FluidSim.h"
#include "Material.h"
#include "RTETools.h"
#include "SceneMan.h"
#include "Scene.h"
#include "SLTerrain.h"
#include "TerrainTrees.h"
#include "TimerMan.h"
#include "Vector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>

using namespace RTE;

float TerrainGrazing::s_RegrowMinutes = 3.0F;

namespace {
	std::array<unsigned char, 256> s_Food{}; //!< For each material, the Diet bits it is food for (0: none).

	/// A plant pixel eaten, waiting to grow back.
	struct Eaten {
		int X = 0;
		int Y = 0;
		int Material = 0;
		int Color = 0;
		long long DueMS = 0; //!< Sim time it grows back at, once there is something under it and nothing where it was.
	};
	std::deque<Eaten> s_Eaten; //!< In the order eaten.

	/// A bite asked for by an animal's AI (on its own thread), taken out of the terrain on the main thread at the next update.
	struct Bite {
		Vector At;
		float Radius = 0.0F;
		int Diet = 0;
		int Most = 0;
	};
	std::mutex s_BiteMutex;
	std::vector<Bite> s_Bites;
	long long s_LastCheckMS = -1;

	constexpr int c_MaxWaiting = 200000; //!< More eaten than this waiting, and the oldest are let go (they stay eaten).
	constexpr int c_MaxSaved = 50000; //!< The most eaten pixels a saved game keeps, the newest (the rest stay eaten).

	std::string s_PendingLoadState; //!< The plants waiting to grow back in the saved game being loaded, taken up when its scene starts.
	bool s_DueIsTimeLeft = false; //!< Whether the DueMS of the plants just loaded are still the time left, to put on the clock at the first update.

	SLTerrain* Terrain() {
		return g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetTerrain() : nullptr;
	}

	/// Whether a material is solid ground to stand a plant or an animal on: not air, liquid or a tree.
	bool IsGround(int material) {
		return material != MaterialColorKeys::g_MaterialAir && material != MaterialColorKeys::g_MaterialOutOfBounds && !FluidSim::IsLiquid(material) && !TerrainTrees::IsTreeMaterial(material);
	}

	/// Whether a scene pixel is open: air, or not there.
	bool IsOpen(int material) {
		return material == MaterialColorKeys::g_MaterialAir;
	}

	/// Keeps a place among the ones found if no kept one is within spacing of it, until there are most.
	void KeepSpaced(std::vector<std::pair<float, Vector>>& found, std::vector<Vector>& kept, float spacing, int most) {
		std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (const auto& [distance, place]: found) {
			if (static_cast<int>(kept.size()) >= most) {
				break;
			}
			bool near = std::any_of(kept.begin(), kept.end(), [&](const Vector& other) { return g_SceneMan.ShortestDistance(other, place, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(spacing); });
			if (!near) {
				kept.push_back(place);
			}
		}
	}
} // namespace

void TerrainGrazing::BuildTables() {
	s_Food.fill(0);
	for (int id = 1; id < 256; ++id) {
		const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
		if (!material || material->GetIndex() != id || TerrainTrees::IsTreeMaterial(id)) {
			continue;
		}
		const std::string& name = material->GetPresetName();
		if (name.find("Grass") != std::string::npos || name.find("grass") != std::string::npos) {
			s_Food[id] |= Diet::Grass;
		} else if (name == "ToughVegetation") {
			s_Food[id] |= Diet::Tough;
		} else if (name == "Vegetation") {
			s_Food[id] |= Diet::Bushes;
		}
	}
}

void TerrainGrazing::Clear() {
	s_Eaten.clear();
	s_DueIsTimeLeft = false;
	{
		std::scoped_lock lock(s_BiteMutex);
		s_Bites.clear();
	}
	s_LastCheckMS = -1;
	if (!s_PendingLoadState.empty()) {
		// "1 x y material color msLeft" per plant, oldest first. The time left goes on the clock at the first update, once the activity's clock is running.
		std::istringstream stream(s_PendingLoadState);
		int version = 0;
		stream >> version;
		Eaten eaten;
		while (version == 1 && stream >> eaten.X >> eaten.Y >> eaten.Material >> eaten.Color >> eaten.DueMS && static_cast<int>(s_Eaten.size()) < c_MaxSaved) {
			eaten.DueMS = std::max(eaten.DueMS, 0LL);
			s_Eaten.push_back(eaten);
		}
		s_DueIsTimeLeft = !s_Eaten.empty();
	}
	s_PendingLoadState.clear();
}

std::string TerrainGrazing::GetSaveState() {
	if (s_Eaten.empty()) {
		return std::string();
	}
	long long now = g_TimerMan.GetSimTimeMS();
	std::ostringstream stream;
	stream << 1;
	size_t first = s_Eaten.size() > static_cast<size_t>(c_MaxSaved) ? s_Eaten.size() - c_MaxSaved : 0;
	for (size_t i = first; i < s_Eaten.size(); ++i) {
		const Eaten& eaten = s_Eaten[i];
		long long left = s_DueIsTimeLeft ? eaten.DueMS : std::max(eaten.DueMS - now, 0LL);
		stream << ' ' << eaten.X << ' ' << eaten.Y << ' ' << eaten.Material << ' ' << eaten.Color << ' ' << left;
	}
	return stream.str();
}

void TerrainGrazing::SetPendingLoadState(const std::string& state) {
	s_PendingLoadState = state;
}

bool TerrainGrazing::IsFood(int materialID, int diet) {
	return (s_Food[static_cast<unsigned char>(materialID)] & diet) != 0;
}

void TerrainGrazing::Update() {
	SLTerrain* terrain = Terrain();
	std::vector<Bite> bites;
	{
		std::scoped_lock lock(s_BiteMutex);
		bites.swap(s_Bites);
	}
	if (!terrain) {
		return;
	}
	if (s_DueIsTimeLeft) {
		long long start = g_TimerMan.GetSimTimeMS();
		for (Eaten& eaten: s_Eaten) {
			eaten.DueMS += start;
		}
		s_DueIsTimeLeft = false;
	}
	for (const Bite& bite: bites) {
		TakeBite(terrain, bite.At, bite.Radius, bite.Diet, bite.Most);
	}
	if (s_Eaten.empty() || s_RegrowMinutes <= 0.0F) {
		return;
	}
	// Twice a second is plenty for plants.
	long long now = g_TimerMan.GetSimTimeMS();
	if (s_LastCheckMS >= 0 && now - s_LastCheckMS < 500) {
		return;
	}
	s_LastCheckMS = now;
	// Each due pixel grows back if it can, else waits a little longer (the pixel under it may still be to come back, or something stands there).
	std::deque<Eaten> waiting;
	for (Eaten& eaten: s_Eaten) {
		if (eaten.DueMS > now) {
			waiting.push_back(eaten);
			continue;
		}
		int there = terrain->GetMaterialPixel(eaten.X, eaten.Y);
		if (!IsOpen(there)) {
			// Something else took its place (dirt, a building): it is gone for good.
			continue;
		}
		int under = terrain->GetMaterialPixel(eaten.X, eaten.Y + 1);
		if (!IsGround(under)) {
			eaten.DueMS = now + 10000;
			waiting.push_back(eaten);
			continue;
		}
		terrain->SetMaterialPixel(eaten.X, eaten.Y, eaten.Material);
		terrain->SetFGColorPixel(eaten.X, eaten.Y, eaten.Color);
	}
	s_Eaten.swap(waiting);
}

std::vector<Vector>* TerrainGrazing::FindFood(const Vector& from, float radius, int diet, int most) {
	std::vector<Vector>* places = new std::vector<Vector>();
	SLTerrain* terrain = Terrain();
	if (!terrain || diet == 0 || most <= 0) {
		return places;
	}
	int reach = std::clamp(static_cast<int>(radius), 1, 1000);
	int fromX = static_cast<int>(std::floor(from.m_X));
	int fromY = static_cast<int>(std::floor(from.m_Y));
	std::vector<std::pair<float, Vector>> found;
	// Every other column is enough to find a patch, and halves the looking.
	for (int dx = -reach; dx <= reach; dx += 2) {
		int x = fromX + dx;
		int above = terrain->GetMaterialPixel(x, fromY - reach / 2 - 1);
		for (int y = fromY - reach / 2; y <= fromY + reach / 2; ++y) {
			int here = terrain->GetMaterialPixel(x, y);
			if (IsOpen(above) && IsFood(here, diet)) {
				found.emplace_back(static_cast<float>(std::abs(dx)) + static_cast<float>(std::abs(y - fromY)) * 2.0F, Vector(static_cast<float>(x), static_cast<float>(y)));
			}
			above = here;
		}
	}
	std::vector<Vector> kept;
	KeepSpaced(found, kept, 12.0F, most);
	for (Vector& place: kept) {
		g_SceneMan.WrapPosition(place);
		places->push_back(place);
	}
	return places;
}

std::vector<Vector>* TerrainGrazing::FindWater(const Vector& from, float radius, int most) {
	std::vector<Vector>* places = new std::vector<Vector>();
	SLTerrain* terrain = Terrain();
	if (!terrain || most <= 0) {
		return places;
	}
	int reach = std::clamp(static_cast<int>(radius), 1, 1000);
	int fromX = static_cast<int>(std::floor(from.m_X));
	int fromY = static_cast<int>(std::floor(from.m_Y));
	// The top of each column in the band (the first thing under air), and whether that is liquid.
	int width = reach * 2 + 1;
	std::vector<int> surfaceY(width, -1);
	std::vector<bool> wet(width, false);
	for (int i = 0; i < width; ++i) {
		int x = fromX - reach + i;
		int above = terrain->GetMaterialPixel(x, fromY - reach / 2 - 1);
		for (int y = fromY - reach / 2; y <= fromY + reach / 2; ++y) {
			int here = terrain->GetMaterialPixel(x, y);
			if (IsOpen(above) && !IsOpen(here)) {
				surfaceY[i] = y;
				wet[i] = FluidSim::IsLiquid(here);
				break;
			}
			above = here;
		}
	}
	// The dry ground at the edge of each stretch of water: the last dry column before it on either side, if it stands no more than a body above it.
	std::vector<std::pair<float, Vector>> found;
	std::vector<Vector> waterBeside; // For each found place, the water's surface beside it (by the place's x).
	for (int i = 1; i < width; ++i) {
		for (int side: {-1, 1}) {
			int dry = side < 0 ? i - 1 : i;
			int water = side < 0 ? i : i - 1;
			if (!wet[water] || wet[dry] || surfaceY[dry] < 0 || surfaceY[water] < 0 || surfaceY[water] - surfaceY[dry] > 24 || surfaceY[dry] - surfaceY[water] > 4) {
				continue;
			}
			int x = fromX - reach + dry;
			found.emplace_back(static_cast<float>(std::abs(x - fromX)) + static_cast<float>(std::abs(surfaceY[dry] - fromY)) * 2.0F, Vector(static_cast<float>(x), static_cast<float>(surfaceY[dry] - 1)));
			waterBeside.emplace_back(static_cast<float>(fromX - reach + water), static_cast<float>(surfaceY[water]));
		}
	}
	std::vector<std::pair<float, Vector>> indexed;
	for (size_t i = 0; i < found.size(); ++i) {
		indexed.emplace_back(found[i].first, found[i].second);
	}
	std::vector<Vector> kept;
	KeepSpaced(indexed, kept, 24.0F, most);
	for (Vector& place: kept) {
		// (Its water: the one found with the same place.)
		Vector water = place;
		for (size_t i = 0; i < found.size(); ++i) {
			if (found[i].second == place) {
				water = waterBeside[i];
				break;
			}
		}
		g_SceneMan.WrapPosition(place);
		g_SceneMan.WrapPosition(water);
		places->push_back(place);
		places->push_back(water);
	}
	return places;
}

void TerrainGrazing::TakeBite(SLTerrain* terrain, const Vector& at, float radius, int diet, int most) {
	int reach = std::clamp(static_cast<int>(radius), 1, 40);
	int atX = static_cast<int>(std::floor(at.m_X));
	int atY = static_cast<int>(std::floor(at.m_Y));
	// The food in the circle, from the top down (the tips first, as an animal crops a lawn), then the nearest.
	std::vector<std::pair<int, int>> food;
	for (int dy = -reach; dy <= reach; ++dy) {
		for (int dx = -reach; dx <= reach; ++dx) {
			if (dx * dx + dy * dy <= reach * reach && IsFood(terrain->GetMaterialPixel(atX + dx, atY + dy), diet)) {
				food.emplace_back(dx, dy);
			}
		}
	}
	std::stable_sort(food.begin(), food.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : std::abs(a.first) < std::abs(b.first); });
	int eaten = 0;
	long long now = g_TimerMan.GetSimTimeMS();
	for (const auto& [dx, dy]: food) {
		if (eaten >= most) {
			break;
		}
		int x = atX + dx;
		int y = atY + dy;
		g_SceneMan.WrapPosition(x, y);
		Eaten bite;
		bite.X = x;
		bite.Y = y;
		bite.Material = terrain->GetMaterialPixel(x, y);
		bite.Color = terrain->GetFGColorPixel(x, y);
		bite.DueMS = now + static_cast<long long>(s_RegrowMinutes * 60000.0F * RandomNum(0.6F, 1.6F));
		terrain->SetMaterialPixel(x, y, MaterialColorKeys::g_MaterialAir);
		terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
		if (s_RegrowMinutes > 0.0F) {
			s_Eaten.push_back(bite);
		}
		++eaten;
	}
	while (s_Eaten.size() > c_MaxWaiting) {
		s_Eaten.pop_front();
	}
}

int TerrainGrazing::Eat(const Vector& at, float radius, int diet, int most) {
	SLTerrain* terrain = Terrain();
	if (!terrain || diet == 0 || most <= 0) {
		return 0;
	}
	// What there is to eat now (a look, safe from the AI's threads); the bite itself waits for the main thread.
	int reach = std::clamp(static_cast<int>(radius), 1, 40);
	int atX = static_cast<int>(std::floor(at.m_X));
	int atY = static_cast<int>(std::floor(at.m_Y));
	int food = 0;
	for (int dy = -reach; dy <= reach && food < most; ++dy) {
		for (int dx = -reach; dx <= reach && food < most; ++dx) {
			if (dx * dx + dy * dy <= reach * reach && IsFood(terrain->GetMaterialPixel(atX + dx, atY + dy), diet)) {
				++food;
			}
		}
	}
	if (food > 0) {
		std::scoped_lock lock(s_BiteMutex);
		s_Bites.push_back({at, radius, diet, most});
	}
	return food;
}

int TerrainGrazing::GetWaitingCount() {
	return static_cast<int>(s_Eaten.size());
}
