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
	constexpr int c_NoTree = -1; //!< In s_Owner: no tree's.
	constexpr int c_Scrap = -2; //!< In s_Owner: a scrap of wood, seen but not a tree.

	std::vector<int> s_Owner; //!< For every pixel (y * width + x), its tree's place in s_Found, or c_NoTree or c_Scrap. Empty until a survey finds trunk material.
	std::vector<int> s_Marked; //!< The pixels s_Owner has something other than c_NoTree for, so the next survey clears just those.
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
	s_Owner.shrink_to_fit();
	s_Marked.clear();
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
	size_t key = static_cast<size_t>(y) * static_cast<size_t>(materials->w) + static_cast<size_t>(x);
	return key < s_Owner.size() && s_Owner[key] >= 0 ? &trees[s_Owner[key]] : nullptr;
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
	if (s_Owner.empty() || s_SceneWidth <= 0 || x < 0 || y < 0 || x >= s_SceneWidth) {
		return 0;
	}
	size_t key = static_cast<size_t>(y) * static_cast<size_t>(s_SceneWidth) + static_cast<size_t>(x);
	int owner = key < s_Owner.size() ? s_Owner[key] : c_NoTree;
	return (owner >= 0 && owner < static_cast<int>(s_Found.size())) ? s_Found[owner].ID : 0;
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
	for (int key: s_Marked) {
		if (static_cast<size_t>(key) < s_Owner.size()) {
			s_Owner[key] = c_NoTree;
		}
	}
	s_Marked.clear();

	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	const BITMAP* materials = terrain ? terrain->GetMaterialBitmap() : nullptr;
	if (!materials || !std::any_of(s_Trunk.begin(), s_Trunk.end(), [](bool trunk) { return trunk; })) {
		return;
	}
	const int width = materials->w;
	const int height = materials->h;
	s_SceneWidth = width;
	// One entry per pixel, made once per scene: a plain array lookup where a hash map was, which a big tree's many pixels made slow.
	if (s_Owner.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) {
		s_Owner.assign(static_cast<size_t>(width) * static_cast<size_t>(height), c_NoTree);
	}
	auto at = [materials](int x, int y) { return static_cast<int>(materials->line[y][x]); };

	// First each trunk: tree trunk pixels standing together (diagonally too).
	std::vector<int> open;
	std::vector<int> pixels;
	std::vector<char> rooted; // Per tree found: whether its trunk touches ground (anything solid but a tree). A branch drawn apart from the trunk doesn't.
	for (int y = 0; y < height; ++y) {
		const unsigned char* row = materials->line[y];
		for (int x = 0; x < width; ++x) {
			int key = y * width + x;
			if (!s_Trunk[row[x]] || s_Owner[key] != c_NoTree) {
				continue;
			}
			int index = static_cast<int>(s_Found.size());
			pixels.clear();
			open.assign(1, key);
			s_Owner[key] = index;
			s_Marked.push_back(key);
			bool touchesGround = false;
			while (!open.empty()) {
				int here = open.back();
				open.pop_back();
				pixels.push_back(here);
				int hx = here % width;
				int hy = here / width;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						int nx = hx + dx;
						int ny = hy + dy;
						if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width || ny >= height) {
							continue;
						}
						if (int material = at(nx, ny); material != 0 && !s_Tree[material]) {
							touchesGround = true;
						}
						if (!s_Trunk[at(nx, ny)]) {
							continue;
						}
						if (int neighbour = ny * width + nx; s_Owner[neighbour] == c_NoTree) {
							s_Owner[neighbour] = index;
							s_Marked.push_back(neighbour);
							open.push_back(neighbour);
						}
					}
				}
			}
			if (static_cast<int>(pixels.size()) < c_MinTrunkPixels) {
				// A scrap of wood: forgotten, though its pixels stay seen (marked c_Scrap) so they aren't looked at again.
				for (int pixel: pixels) {
					s_Owner[pixel] = c_Scrap;
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
			std::array<int, 256> materialCount{};
			for (int pixel: pixels) {
				int px = pixel % width;
				int py = pixel / width;
				++materialCount[at(px, py)];
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
			tree.TrunkMaterial = static_cast<int>(std::max_element(materialCount.begin(), materialCount.end()) - materialCount.begin());
			s_Found.push_back(tree);
			rooted.push_back(touchesGround ? 1 : 0);
		}
	}

	// Then the leaves, spreading out from every trunk at once through leaves touching leaves, so each leaf goes to the trunk it hangs nearest.
	std::vector<int> front;
	for (int key: s_Marked) {
		if (s_Owner[key] >= 0) {
			front.push_back(key);
		}
	}
	std::sort(front.begin(), front.end()); // (A fixed order, so the same terrain gives the same trees.)
	std::vector<int> owned(front); // Every pixel of a tree, trunk and leaves.
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
					if (int neighbour = ny * width + nx; s_Owner[neighbour] == c_NoTree) {
						s_Owner[neighbour] = index;
						s_Marked.push_back(neighbour);
						owned.push_back(neighbour);
						next.push_back(neighbour);
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

	// A branch drawn apart from its trunk, among the leaves (a big tree has several), is part of the tree its leaves touch, not a tree of its
	// own: otherwise a tree's leaves hanging on such a branch counted as already hanging in the air, and stayed up when the trunk was cut.
	// Each tree that doesn't stand in the ground joins one it touches, rooted ones first. One pass over the touching pixels is enough: a pair
	// passed over (both already in the ground) stays that way, as joining only ever puts more in the ground.
	if (s_Found.size() > 1) {
		std::vector<int> parent(s_Found.size());
		for (size_t i = 0; i < parent.size(); ++i) {
			parent[i] = static_cast<int>(i);
		}
		auto find = [&parent](int i) {
			while (parent[i] != i) {
				parent[i] = parent[parent[i]];
				i = parent[i];
			}
			return i;
		};
		std::sort(owned.begin(), owned.end()); // (A fixed order, so the same terrain gives the same trees.)
		for (int key: owned) {
			int a = find(s_Owner[key]);
			int hx = key % width;
			int hy = key / width;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					int nx = hx + dx;
					int ny = hy + dy;
					if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width || ny >= height) {
						continue;
					}
					int other = s_Owner[ny * width + nx];
					if (other < 0) {
						continue;
					}
					int b = find(other);
					if (a == b || (rooted[a] && rooted[b])) {
						continue;
					}
					// The one not in the ground joins the other (two loose ones: the later the earlier).
					if (rooted[a] || (!rooted[b] && a < b)) {
						parent[b] = a;
					} else {
						parent[a] = b;
						a = b;
					}
				}
			}
		}
		std::vector<int> newIndex(s_Found.size(), -1);
		std::vector<Tree> merged;
		for (size_t i = 0; i < s_Found.size(); ++i) {
			if (find(static_cast<int>(i)) == static_cast<int>(i)) {
				newIndex[i] = static_cast<int>(merged.size());
				merged.push_back(s_Found[i]);
			}
		}
		for (size_t i = 0; i < s_Found.size(); ++i) {
			int root = find(static_cast<int>(i));
			if (root == static_cast<int>(i)) {
				continue;
			}
			Tree& into = merged[newIndex[root]];
			const Tree& branch = s_Found[i];
			into.Left = std::min(into.Left, branch.Left);
			into.Top = std::min(into.Top, branch.Top);
			into.Right = std::max(into.Right, branch.Right);
			into.Bottom = std::max(into.Bottom, branch.Bottom);
			into.TrunkPixels += branch.TrunkPixels;
			into.LeafPixels += branch.LeafPixels;
			newIndex[i] = newIndex[root];
		}
		for (int key: owned) {
			s_Owner[key] = newIndex[s_Owner[key]];
		}
		s_Found.swap(merged);
	}

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
