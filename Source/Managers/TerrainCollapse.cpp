#include "TerrainCollapse.h"
#include "Constants.h"
#include "Material.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "TimerMan.h"
#include "Vector.h"
#include "EffectsParticles.h"
#include "FluidSim.h"

#include <algorithm>
#include <array>
#include <string>
#include <mutex>
#include <unordered_set>
#include <vector>

using namespace RTE;

bool TerrainCollapse::s_Enabled = true;

namespace {
	struct Check {
		int X, Y;
		int Radius;
		long long DueUpdate; //!< Sim update count when the check runs.
	};

	constexpr int c_MaxIslandPixels = 2500; //!< Bigger detached pieces are left standing; they're too big to crumble convincingly.
	std::array<bool, 256> s_Anchors{}; //!< Materials that hold a piece up: built structures (concrete, metal) and the world's bedrock.
	bool s_AnchorsBuilt = false;

	void BuildAnchors() {
		s_Anchors.fill(false);
		for (int id = 1; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (!material || material->GetIndex() != id) {
				continue;
			}
			const std::string& name = material->GetPresetName();
			// Scrap and mangled metal are debris, not structure. ("Bedrock" is used for ordinary hill cores in some scenes, so it isn't an anchor.)
			if (name.find("Scrap") != std::string::npos || name.find("Mangled") != std::string::npos) {
				continue;
			}
			for (const char* word: {"Concrete", "Metal", "Military", "Level End", "Xenocronium", "Door", "Test"}) {
				if (name.find(word) != std::string::npos) {
					s_Anchors[id] = true;
					break;
				}
			}
		}
		s_AnchorsBuilt = true;
	}

	std::vector<Check> s_Pending; //!< Checks queued from (possibly parallel) gib code, waiting to be scheduled.
	std::vector<Check> s_Scheduled;
	std::mutex s_QueueMutex;
	const void* s_Scene = nullptr;
	int s_CollapsedCount = 0;

	/// A detached piece of terrain falling as a rigid chunk until it lands.
	struct FallingPiece {
		std::vector<int> Keys; //!< Pixels, y * width + x, sorted.
		std::vector<unsigned char> Materials;
		std::vector<int> Colors;
		float Speed = 0.0F; //!< Pixels per sim update.
		float Progress = 0.0F; //!< Fraction of a pixel moved but not applied yet.
	};
	std::vector<FallingPiece> s_Falling;
	std::unordered_set<int> s_FallingKeys; //!< All pixels of falling pieces, which checks ignore.

	void RebuildFallingKeys() {
		s_FallingKeys.clear();
		for (const FallingPiece& piece: s_Falling) {
			s_FallingKeys.insert(piece.Keys.begin(), piece.Keys.end());
		}
	}

	/// Flood fills the solid piece containing a pixel. Returns true if it's a small floating piece that should fall, filling its pixels.
	bool FindFloatingPiece(SLTerrain* terrain, int startX, int startY, int width, int height, bool wrapX, std::unordered_set<int>& supported, std::unordered_set<int>& visited, std::vector<int>& piece) {
		piece.clear();
		std::vector<int> stack{startY * width + startX};
		std::unordered_set<int> seen{stack.back()};
		bool floating = true;
		while (!stack.empty()) {
			int key = stack.back();
			stack.pop_back();
			piece.push_back(key);
			if (static_cast<int>(piece.size()) > c_MaxIslandPixels || supported.count(key)) {
				floating = false;
				break;
			}
			int x = key % width;
			int y = key / width;
			if (s_Anchors[static_cast<unsigned char>(terrain->GetMaterialPixel(x, y))]) {
				floating = false;
				break;
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
					if (seen.count(neighbour) || s_FallingKeys.count(neighbour) || terrain->GetMaterialPixel(nx, ny) == g_MaterialAir) {
						continue;
					}
					seen.insert(neighbour);
					stack.push_back(neighbour);
				}
			}
			if (!floating) {
				break;
			}
		}
		for (int key: seen) {
			visited.insert(key);
		}
		if (!floating) {
			// Everything reached is held up; later fills that touch it stop early.
			for (int key: seen) {
				supported.insert(key);
			}
			return false;
		}
		std::sort(piece.begin(), piece.end());
		return true;
	}

	void RunCheck(SLTerrain* terrain, const Check& check) {
		int width = terrain->GetBitmap()->w;
		int height = terrain->GetBitmap()->h;
		bool wrapX = g_SceneMan.SceneWrapsX();
		std::unordered_set<int> supported;
		std::unordered_set<int> visited;
		std::vector<int> piece;
		std::vector<int> falling;
		for (int y = std::max(0, check.Y - check.Radius); y <= std::min(height - 1, check.Y + check.Radius); ++y) {
			for (int rawX = check.X - check.Radius; rawX <= check.X + check.Radius; ++rawX) {
				int x = wrapX ? (rawX % width + width) % width : rawX;
				if (x < 0 || x >= width) {
					continue;
				}
				int key = y * width + x;
				if (visited.count(key) || s_FallingKeys.count(key) || terrain->GetMaterialPixel(x, y) == g_MaterialAir) {
					continue;
				}
				if (FindFloatingPiece(terrain, x, y, width, height, wrapX, supported, visited, piece)) {
					falling.insert(falling.end(), piece.begin(), piece.end());
				}
			}
		}
		if (falling.empty()) {
			return;
		}
		std::sort(falling.begin(), falling.end());
		falling.erase(std::unique(falling.begin(), falling.end()), falling.end());
		// Lift the piece out of the terrain; it falls as a rigid chunk from the next update on.
		FallingPiece fallingPiece;
		fallingPiece.Keys = falling;
		for (int key: falling) {
			int x = key % width;
			int y = key / width;
			fallingPiece.Materials.push_back(static_cast<unsigned char>(terrain->GetMaterialPixel(x, y)));
			fallingPiece.Colors.push_back(terrain->GetFGColorPixel(x, y));
		}
		s_CollapsedCount += static_cast<int>(falling.size());
		s_Falling.push_back(std::move(fallingPiece));
		RebuildFallingKeys();
	}
	/// Moves falling pieces down, accelerating under gravity, until they land; landed pieces are removed.
	void UpdateFalling(SLTerrain* terrain) {
		if (s_Falling.empty()) {
			return;
		}
		int width = terrain->GetBitmap()->w;
		int height = terrain->GetBitmap()->h;
		for (FallingPiece& piece: s_Falling) {
			piece.Speed = std::min(piece.Speed + 0.12F, 6.0F);
			piece.Progress += piece.Speed;
			int steps = static_cast<int>(piece.Progress);
			piece.Progress -= static_cast<float>(steps);
			std::unordered_set<int> inPiece(piece.Keys.begin(), piece.Keys.end());
			bool landed = false;
			for (int step = 0; step < steps && !landed; ++step) {
				// Blocked if any pixel would move into solid terrain that isn't part of the piece.
				for (int key: piece.Keys) {
					int below = key + width;
					if (key / width + 1 >= height) {
						continue;
					}
					if (!inPiece.count(below) && terrain->GetMaterialPixel(below % width, below / width) != g_MaterialAir) {
						landed = true;
						break;
					}
				}
				if (landed) {
					break;
				}
				for (int key: piece.Keys) {
					terrain->SetMaterialPixel(key % width, key / width, g_MaterialAir);
					terrain->SetFGColorPixel(key % width, key / width, ColorKeys::g_MaskColor);
				}
				std::vector<int> moved;
				std::vector<unsigned char> movedMaterials;
				std::vector<int> movedColors;
				for (size_t i = 0; i < piece.Keys.size(); ++i) {
					int below = piece.Keys[i] + width;
					// Pixels falling out of the bottom of the world are gone.
					if (below / width < height) {
						moved.push_back(below);
						movedMaterials.push_back(piece.Materials[i]);
						movedColors.push_back(piece.Colors[i]);
					}
				}
				piece.Keys.swap(moved);
				piece.Materials.swap(movedMaterials);
				piece.Colors.swap(movedColors);
				for (size_t i = 0; i < piece.Keys.size(); ++i) {
					terrain->SetMaterialPixel(piece.Keys[i] % width, piece.Keys[i] / width, piece.Materials[i]);
					terrain->SetFGColorPixel(piece.Keys[i] % width, piece.Keys[i] / width, piece.Colors[i]);
				}
				inPiece = std::unordered_set<int>(piece.Keys.begin(), piece.Keys.end());
			}
			if (landed || piece.Keys.empty()) {
				int minX = width;
				int minY = height;
				int maxX = -1;
				int maxY = -1;
				for (int key: piece.Keys) {
					minX = std::min(minX, key % width);
					minY = std::min(minY, key / width);
					maxX = std::max(maxX, key % width);
					maxY = std::max(maxY, key / width);
				}
				if (maxX >= 0) {
					terrain->AddUpdatedMaterialArea(Box(Vector(static_cast<float>(minX), static_cast<float>(minY - 64)), static_cast<float>(maxX - minX + 1), static_cast<float>(maxY - minY + 65)));
					FluidSim::Disturb(Vector(static_cast<float>(minX + maxX) * 0.5F, static_cast<float>(minY + maxY) * 0.5F), static_cast<float>(std::max(maxX - minX, maxY - minY)) * 0.5F + 8.0F);
					// A thud of dust where it lands (visual only).
					EffectsParticles::SpawnExplosion(Vector(static_cast<float>(minX + maxX) * 0.5F, static_cast<float>(maxY)), std::min(600.0F + static_cast<float>(piece.Keys.size()) * 6.0F, 8000.0F) * std::max(0.3F, piece.Speed / 6.0F));
				}
				piece.Keys.clear();
			}
		}
		s_Falling.erase(std::remove_if(s_Falling.begin(), s_Falling.end(), [](const FallingPiece& piece) { return piece.Keys.empty(); }), s_Falling.end());
		RebuildFallingKeys();
	}
} // namespace

void TerrainCollapse::QueueCheck(const Vector& position, float radius) {
	if (!s_Enabled) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	s_Pending.push_back({static_cast<int>(position.m_X), static_cast<int>(position.m_Y), static_cast<int>(radius), 0});
}

void TerrainCollapse::Update() {
	if (g_SceneMan.GetScene() != s_Scene) {
		Clear();
		s_Scene = g_SceneMan.GetScene();
		s_CollapsedCount = 0;
		s_AnchorsBuilt = false;
	}
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	if (!terrain || !s_Enabled) {
		Clear();
		return;
	}
	if (!s_AnchorsBuilt) {
		BuildAnchors();
	}
	long long now = g_TimerMan.GetSimUpdateCount();
	{
		std::scoped_lock lock(s_QueueMutex);
		// Sort for a fixed order, whatever order the gib code queued them in. The crater is still being dug by the blast's particles,
		// so check after half a second and again after a second and a half.
		std::sort(s_Pending.begin(), s_Pending.end(), [](const Check& a, const Check& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Radius < b.Radius); });
		for (const Check& check: s_Pending) {
			s_Scheduled.push_back({check.X, check.Y, check.Radius, now + 30});
			s_Scheduled.push_back({check.X, check.Y, check.Radius, now + 90});
		}
		s_Pending.clear();
	}
	UpdateFalling(terrain);
	for (const Check& check: s_Scheduled) {
		if (check.DueUpdate <= now) {
			RunCheck(terrain, check);
		}
	}
	s_Scheduled.erase(std::remove_if(s_Scheduled.begin(), s_Scheduled.end(), [now](const Check& check) { return check.DueUpdate <= now; }), s_Scheduled.end());
}

void TerrainCollapse::Clear() {
	s_Falling.clear();
	s_FallingKeys.clear();
	std::scoped_lock lock(s_QueueMutex);
	s_Pending.clear();
	s_Scheduled.clear();
}

int TerrainCollapse::GetCollapsedCount() {
	return s_CollapsedCount;
}
