#include "TerrainTrees.h"

#include "Material.h"
#include "SceneMan.h"
#include "Scene.h"
#include "SLTerrain.h"
#include "TimerMan.h"
#include "Vector.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

using namespace RTE;

bool TerrainTrees::s_UnitsCollide = false;
bool TerrainTrees::s_DrawnBehindUnits = true;
std::array<float, 256> TerrainTrees::s_ShaderFlags{};
int TerrainTrees::s_StrayBulletPercent = 5;
std::array<bool, 256> TerrainTrees::s_Tree{};
std::array<bool, 256> TerrainTrees::s_Trunk{};
std::array<bool, 256> TerrainTrees::s_ActorsPass{};
bool TerrainTrees::s_Stale = true;

namespace {
	constexpr int c_MinTrunkPixels = 12; //!< Less trunk than this standing together is a scrap of wood, not a tree.
	constexpr long long c_LookAgainMS = 1000; //!< How long the trees found are taken to hold, as they burn and fall.

	std::vector<TerrainTrees::Tree> s_Found; //!< The trees found at the last look.
	std::unordered_map<int, int> s_Owner; //!< Each tree pixel found (y * width + x) to its tree's place in s_Found.
	long s_NextID = 1;
	long long s_LookedAtMS = -1; //!< Sim time of the last look; -1 for never.
	const Scene* s_Scene = nullptr; //!< The scene the trees were found in.
	unsigned int s_SceneGeneration = 0;
	int s_SceneWidth = 0; //!< The width the owner keys were made with.
} // namespace

void TerrainTrees::SetUnitsCollide(bool collide) {
	bool changed = collide != s_UnitsCollide;
	s_UnitsCollide = collide;
	for (int id = 0; id < 256; ++id) {
		s_ActorsPass[id] = !s_UnitsCollide && s_Tree[id];
	}
	// The path grids price trees as open ground or as something to go round, so they're worked out again.
	if (changed && g_SceneMan.GetScene() && g_SceneMan.GetScene()->GetTerrain()) {
		g_SceneMan.GetScene()->ResetPathFinding();
	}
}

void TerrainTrees::BuildTables() {
	s_Tree.fill(false);
	s_Trunk.fill(false);
	for (int id = 1; id < 256; ++id) {
		const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
		if (!material || material->GetIndex() != id) {
			continue;
		}
		const std::string& name = material->GetPresetName();
		if (name.find("Tree") == std::string::npos && name.find("tree") == std::string::npos) {
			continue;
		}
		s_Tree[id] = true;
		s_Trunk[id] = name.find("Leaf") == std::string::npos && name.find("Leaves") == std::string::npos && name.find("leaf") == std::string::npos && name.find("leaves") == std::string::npos;
	}
	for (int id = 0; id < 256; ++id) {
		s_ActorsPass[id] = !s_UnitsCollide && s_Tree[id];
		s_ShaderFlags[id] = s_Tree[id] ? 1.0F : 0.0F;
	}
}

void TerrainTrees::Clear() {
	s_Found.clear();
	s_Owner.clear();
	s_LookedAtMS = -1;
	s_Stale = true;
	s_Scene = g_SceneMan.GetScene();
	s_SceneGeneration = g_SceneMan.GetSceneGeneration();
}

const std::vector<TerrainTrees::Tree>& TerrainTrees::GetTrees() {
	if (g_SceneMan.GetScene() != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		Clear();
	}
	long long now = g_TimerMan.GetSimTimeMS();
	if (s_Stale || s_LookedAtMS < 0 || now - s_LookedAtMS >= c_LookAgainMS || now < s_LookedAtMS) {
		Survey();
		s_LookedAtMS = now;
		s_Stale = false;
	}
	return s_Found;
}

const TerrainTrees::Tree* TerrainTrees::FindTreeAt(int x, int y) {
	const std::vector<Tree>& trees = GetTrees();
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	const BITMAP* materials = terrain ? terrain->GetMaterialBitmap() : nullptr;
	if (!materials || x < 0 || y < 0 || x >= materials->w || y >= materials->h) {
		return nullptr;
	}
	auto owner = s_Owner.find(y * materials->w + x);
	return owner != s_Owner.end() ? &trees[owner->second] : nullptr;
}

void TerrainTrees::FindTreesNear(const Vector& centre, float radius, std::vector<const Tree*>& found) {
	found.clear();
	std::vector<std::pair<float, const Tree*>> near;
	for (const Tree& tree: GetTrees()) {
		// How far the point is from the tree's box.
		float dx = std::max({static_cast<float>(tree.Left) - centre.m_X, 0.0F, centre.m_X - static_cast<float>(tree.Right)});
		float dy = std::max({static_cast<float>(tree.Top) - centre.m_Y, 0.0F, centre.m_Y - static_cast<float>(tree.Bottom)});
		float distance = std::sqrt(dx * dx + dy * dy);
		if (distance <= radius) {
			near.emplace_back(distance, &tree);
		}
	}
	std::stable_sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	for (const auto& [distance, tree]: near) {
		found.push_back(tree);
	}
}

long TerrainTrees::OwnerIDAt(int x, int y) {
	if (s_Owner.empty() || s_SceneWidth <= 0) {
		return 0;
	}
	auto owner = s_Owner.find(y * s_SceneWidth + x);
	return (owner != s_Owner.end() && owner->second >= 0 && owner->second < static_cast<int>(s_Found.size())) ? s_Found[owner->second].ID : 0;
}

const TerrainTrees::Tree* TerrainTrees::GetTree(long id) {
	for (const Tree& tree: GetTrees()) {
		if (tree.ID == id) {
			return &tree;
		}
	}
	return nullptr;
}

void TerrainTrees::Survey() {
	std::vector<Tree> before;
	before.swap(s_Found);
	s_Owner.clear();

	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	const BITMAP* materials = terrain ? terrain->GetMaterialBitmap() : nullptr;
	if (!materials || !std::any_of(s_Trunk.begin(), s_Trunk.end(), [](bool trunk) { return trunk; })) {
		return;
	}
	const int width = materials->w;
	const int height = materials->h;
	s_SceneWidth = width;
	auto at = [materials](int x, int y) { return static_cast<int>(materials->line[y][x]); };

	// First each trunk: tree trunk pixels standing together (diagonally too).
	std::vector<int> open;
	std::vector<int> pixels;
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			int key = y * width + x;
			if (!s_Trunk[at(x, y)] || s_Owner.contains(key)) {
				continue;
			}
			int index = static_cast<int>(s_Found.size());
			pixels.clear();
			open.assign(1, key);
			s_Owner.emplace(key, index);
			std::unordered_map<int, int> materialCount;
			while (!open.empty()) {
				int here = open.back();
				open.pop_back();
				pixels.push_back(here);
				int hx = here % width;
				int hy = here / width;
				++materialCount[at(hx, hy)];
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						int nx = hx + dx;
						int ny = hy + dy;
						if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width || ny >= height || !s_Trunk[at(nx, ny)]) {
							continue;
						}
						if (s_Owner.emplace(ny * width + nx, index).second) {
							open.push_back(ny * width + nx);
						}
					}
				}
			}
			if (static_cast<int>(pixels.size()) < c_MinTrunkPixels) {
				// A scrap of wood: forgotten, though its pixels stay seen (marked -1) so they aren't looked at again.
				for (int pixel: pixels) {
					s_Owner[pixel] = -1;
				}
				continue;
			}
			Tree tree;
			tree.Left = width;
			tree.Top = height;
			tree.Right = -1;
			tree.Bottom = -1;
			long long baseSum = 0;
			int baseCount = 0;
			for (int pixel: pixels) {
				int px = pixel % width;
				int py = pixel / width;
				tree.Left = std::min(tree.Left, px);
				tree.Right = std::max(tree.Right, px);
				tree.Top = std::min(tree.Top, py);
				if (py > tree.Bottom) {
					tree.Bottom = py;
					baseSum = 0;
					baseCount = 0;
				}
				if (py == tree.Bottom) {
					baseSum += px;
					++baseCount;
				}
			}
			tree.BaseX = static_cast<int>(baseSum / std::max(baseCount, 1));
			tree.BaseY = tree.Bottom;
			tree.TrunkPixels = static_cast<int>(pixels.size());
			tree.TrunkMaterial = std::max_element(materialCount.begin(), materialCount.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
			s_Found.push_back(tree);
		}
	}

	// Then the leaves, spreading out from every trunk at once through leaves touching leaves, so each leaf goes to the trunk it hangs nearest.
	std::vector<int> front;
	for (const auto& [key, index]: s_Owner) {
		if (index >= 0) {
			front.push_back(key);
		}
	}
	std::sort(front.begin(), front.end()); // (The map's order isn't fixed; this is, so the same terrain gives the same trees.)
	std::vector<int> next;
	while (!front.empty()) {
		next.clear();
		for (int here: front) {
			int index = s_Owner[here];
			int hx = here % width;
			int hy = here / width;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					int nx = hx + dx;
					int ny = hy + dy;
					if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width || ny >= height) {
						continue;
					}
					int material = at(nx, ny);
					if (!s_Tree[material] || s_Trunk[material]) {
						continue;
					}
					if (s_Owner.emplace(ny * width + nx, index).second) {
						next.push_back(ny * width + nx);
						Tree& tree = s_Found[index];
						++tree.LeafPixels;
						tree.Left = std::min(tree.Left, nx);
						tree.Right = std::max(tree.Right, nx);
						tree.Top = std::min(tree.Top, ny);
						tree.Bottom = std::max(tree.Bottom, ny);
					}
				}
			}
		}
		front.swap(next);
	}
	std::erase_if(s_Owner, [](const auto& owner) { return owner.second < 0; });

	// A tree standing where one stood keeps its ID: its base within a few pixels, of the same wood.
	for (Tree& tree: s_Found) {
		for (Tree& old: before) {
			if (old.ID != 0 && old.TrunkMaterial == tree.TrunkMaterial && std::abs(old.BaseX - tree.BaseX) <= 6 && std::abs(old.BaseY - tree.BaseY) <= 6) {
				tree.ID = old.ID;
				old.ID = 0;
				break;
			}
		}
		if (tree.ID == 0) {
			tree.ID = s_NextID++;
		}
	}
}
