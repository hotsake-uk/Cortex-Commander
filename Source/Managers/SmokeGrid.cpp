#include "SmokeGrid.h"
#include "Constants.h"
#include "MOSParticle.h"
#include "Material.h"
#include "Atom.h"
#include "MovableMan.h"
#include "SceneMan.h"
#include "Vector.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace RTE;

bool SmokeGrid::s_Enabled = true;

namespace {
	constexpr int c_CellSize = 16;
	constexpr float c_BlockingDensity = 2.5F; //!< Smoke that adds up to this much along a line of sight hides what's behind it.

	std::vector<float> s_Density;
	int s_Width = 0;
	int s_Height = 0;
	bool s_HasSmoke = false;

	float Sample(int cellX, int cellY) {
		if (g_SceneMan.SceneWrapsX()) {
			cellX = ((cellX % s_Width) + s_Width) % s_Width;
		}
		if (cellX < 0 || cellY < 0 || cellX >= s_Width || cellY >= s_Height) {
			return 0.0F;
		}
		return s_Density[static_cast<size_t>(cellY) * s_Width + cellX];
	}
} // namespace

void SmokeGrid::Update() {
	s_HasSmoke = false;
	if (!s_Enabled || !g_SceneMan.GetScene()) {
		return;
	}
	int width = (g_SceneMan.GetSceneWidth() + c_CellSize - 1) / c_CellSize;
	int height = (g_SceneMan.GetSceneHeight() + c_CellSize - 1) / c_CellSize;
	if (width != s_Width || height != s_Height) {
		s_Width = width;
		s_Height = height;
		s_Density.assign(static_cast<size_t>(width) * height, 0.0F);
	} else {
		std::fill(s_Density.begin(), s_Density.end(), 0.0F);
	}
	for (const MovableObject* particle: g_MovableMan.m_Particles) {
		// Smoke: weightless air particles that float up.
		if (!particle || particle->GetGlobalAccScalar() >= 0.0F || particle->ToDelete()) {
			continue;
		}
		const MOSParticle* smoke = dynamic_cast<const MOSParticle*>(particle);
		if (!smoke || !smoke->GetAtom() || smoke->GetAtom()->GetMaterial()->GetIndex() != g_MaterialAir) {
			continue;
		}
		float life = smoke->GetLifetime() > 0 ? std::clamp(1.0F - static_cast<float>(smoke->GetAge()) / static_cast<float>(smoke->GetLifetime()), 0.0F, 1.0F) : 1.0F;
		// Bigger puffs are thicker and cover more of the cells around them.
		float amount = life * std::clamp(smoke->GetRadius() / 10.0F, 0.2F, 2.0F) * 0.6F;
		int cellX = static_cast<int>(std::floor(smoke->GetPos().m_X / c_CellSize));
		int cellY = static_cast<int>(std::floor(smoke->GetPos().m_Y / c_CellSize));
		int reach = smoke->GetRadius() > 12.0F ? 1 : 0;
		for (int dy = -reach; dy <= reach; ++dy) {
			for (int dx = -reach; dx <= reach; ++dx) {
				int x = cellX + dx;
				int y = cellY + dy;
				if (g_SceneMan.SceneWrapsX()) {
					x = ((x % s_Width) + s_Width) % s_Width;
				}
				if (x >= 0 && y >= 0 && x < s_Width && y < s_Height) {
					s_Density[static_cast<size_t>(y) * s_Width + x] += (dx == 0 && dy == 0) ? amount : amount * 0.4F;
					s_HasSmoke = true;
				}
			}
		}
	}
}

float SmokeGrid::GetDensity(const Vector& position) {
	if (!s_HasSmoke) {
		return 0.0F;
	}
	return Sample(static_cast<int>(std::floor(position.m_X / c_CellSize)), static_cast<int>(std::floor(position.m_Y / c_CellSize)));
}

bool SmokeGrid::BlocksSight(const Vector& from, const Vector& to) {
	if (!s_Enabled || !s_HasSmoke) {
		return false;
	}
	Vector ray = g_SceneMan.ShortestDistance(from, to, g_SceneMan.SceneWrapsX());
	float length = ray.GetMagnitude();
	if (length < 1.0F) {
		return false;
	}
	// March in half-cell steps, adding up the smoke crossed.
	float step = c_CellSize * 0.5F;
	int steps = static_cast<int>(length / step) + 1;
	float total = 0.0F;
	for (int i = 1; i < steps; ++i) {
		Vector point = from + ray * (static_cast<float>(i) / static_cast<float>(steps));
		total += GetDensity(point) * (step / c_CellSize);
		if (total >= c_BlockingDensity) {
			return true;
		}
	}
	return false;
}
