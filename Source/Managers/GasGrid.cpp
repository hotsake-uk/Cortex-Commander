#include "GasGrid.h"
#include "ACraft.h"
#include "ADoor.h"
#include "Actor.h"
#include "ActorFire.h"
#include "ActorWater.h"
#include "Atom.h"
#include "Constants.h"
#include "EffectsParticles.h"
#include "MOSParticle.h"
#include "Material.h"
#include "MovableMan.h"
#include "PostProcessMan.h"
#include "PresetMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "SceneMan.h"
#include "TerrainFire.h"
#include "TimerMan.h"
#include "Vector.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <climits>
#include <cmath>
#include <mutex>
#include <vector>

using namespace RTE;

bool GasGrid::s_Enabled = true;
float GasGrid::s_Shown = 1.0F;

namespace {
	constexpr int c_Cell = 8; //!< Each cell of the grid covers this many pixels a side.
	constexpr int c_Kinds = GasGrid::KindCount;
	constexpr float c_Spread = 0.15F; //!< How much of the difference between two open cells evens out each update.
	constexpr float c_Rise = 0.08F; //!< How fast buoyant gas trades places with the air above or below it, each update, at a buoyancy of 1.
	constexpr float c_Trace = 0.002F; //!< Less than this in a cell is no gas at all.
	constexpr float c_MaxThickness = 2.0F;
	constexpr int c_MaxSpan = 320; //!< The gas is worked out over at most this many cells across and down (2560 pixels).
	constexpr int c_OpenEvery = 8; //!< Which cells are open is looked at again every this many updates.
	constexpr int c_HurtEvery = 15; //!< Units feel the gas every this many updates.
	constexpr float c_MethaneBurns = 0.15F; //!< Methane this thick or more goes up where it meets fire.
	constexpr int c_MaxBlastsShown = 6; //!< The most methane blasts drawn (with their sound) each update; the rest burn without.

	/// How each gas behaves.
	struct GasProperties {
		float Buoyancy; //!< Up for light gas, down (negative) for heavy.
		float Keeps; //!< What of it is left each update (smoke settles out, steam condenses).
		float Hides; //!< How much a cell full of it hides, in the smoke map's units (0 for methane, which can't be seen).
	};
	constexpr GasProperties c_Gases[c_Kinds] = {
	    {0.5F, 0.9985F, 1.2F}, // Smoke
	    {-1.0F, 0.9997F, 0.6F}, // Toxic
	    {1.0F, 0.99995F, 0.0F}, // Methane
	    {1.5F, 0.993F, 0.8F}, // Steam
	};

	std::array<std::vector<float>, c_Kinds> s_Gas; //!< Per kind, how thick it is in each cell.
	std::array<std::vector<float>, c_Kinds> s_Change; //!< Per kind, this update's change in each cell, so the order cells are worked through doesn't matter.
	std::vector<unsigned char> s_Open; //!< Whether each cell lets gas in (air, not ground or liquid), for the cells in the active area.
	std::vector<unsigned char> s_Burning; //!< Cells where methane went up last update, which light the methane beside them.
	int s_GridWidth = 0;
	int s_GridHeight = 0;
	int s_Left = 0; //!< The active area, in cells: s_Left to s_Right (not included), s_Top to s_Bottom. Empty while there is no gas.
	int s_Right = 0;
	int s_Top = 0;
	int s_Bottom = 0;
	bool s_AnyBurning = false;
	const void* s_Scene = nullptr;
	unsigned int s_SceneGeneration = 0;
	unsigned int s_Random = 0x2F6B1C35u;

	float Random01() {
		s_Random ^= s_Random << 13;
		s_Random ^= s_Random >> 17;
		s_Random ^= s_Random << 5;
		return static_cast<float>(s_Random & 0xFFFFFFu) / static_cast<float>(0x1000000);
	}

	struct GasRequest {
		int X;
		int Y;
		int Kind;
		float Amount;
	};
	std::mutex s_QueueMutex;
	std::vector<GasRequest> s_Requests;

	bool Active() { return s_Right > s_Left && s_Bottom > s_Top; }

	size_t Index(int x, int y) { return static_cast<size_t>(y) * static_cast<size_t>(s_GridWidth) + static_cast<size_t>(x); }

	int CellAt(const Vector& position) {
		int x = static_cast<int>(std::floor(position.m_X / static_cast<float>(c_Cell)));
		int y = static_cast<int>(std::floor(position.m_Y / static_cast<float>(c_Cell)));
		if (g_SceneMan.SceneWrapsX() && s_GridWidth > 0) {
			x = ((x % s_GridWidth) + s_GridWidth) % s_GridWidth;
		}
		return x < 0 || y < 0 || x >= s_GridWidth || y >= s_GridHeight ? -1 : y * s_GridWidth + x;
	}

	bool InActiveArea(int cell) {
		int x = cell % s_GridWidth;
		int y = cell / s_GridWidth;
		return x >= s_Left && x < s_Right && y >= s_Top && y < s_Bottom;
	}

	void WorkOutOpenness(const BITMAP* materialBitmap, int left, int top, int right, int bottom) {
		int width = materialBitmap->w;
		int height = materialBitmap->h;
		for (int y = top; y < bottom; ++y) {
			for (int x = left; x < right; ++x) {
				int air = 0;
				for (int i = 0; i < 4; ++i) {
					int px = std::min(x * c_Cell + ((i & 1) != 0 ? 6 : 2), width - 1);
					int py = std::min(y * c_Cell + ((i & 2) != 0 ? 6 : 2), height - 1);
					air += materialBitmap->line[py][px] == g_MaterialAir ? 1 : 0;
				}
				s_Open[Index(x, y)] = air >= 2 ? 1 : 0;
			}
		}
	}

	/// Takes in more of the scene, worked out for what is open there. Never past c_MaxSpan: a side that would take it further stays where it is.
	void Grow(const BITMAP* materialBitmap, int left, int top, int right, int bottom) {
		left = std::max(left, 0);
		top = std::max(top, 0);
		right = std::min(right, s_GridWidth);
		bottom = std::min(bottom, s_GridHeight);
		int oldLeft = s_Left;
		int oldTop = s_Top;
		int oldRight = s_Right;
		int oldBottom = s_Bottom;
		if (!Active()) {
			s_Left = left;
			s_Top = top;
			s_Right = right;
			s_Bottom = bottom;
		} else {
			if (s_Right - std::min(left, s_Left) <= c_MaxSpan) {
				s_Left = std::min(left, s_Left);
			}
			if (std::max(right, s_Right) - s_Left <= c_MaxSpan) {
				s_Right = std::max(right, s_Right);
			}
			if (s_Bottom - std::min(top, s_Top) <= c_MaxSpan) {
				s_Top = std::min(top, s_Top);
			}
			if (std::max(bottom, s_Bottom) - s_Top <= c_MaxSpan) {
				s_Bottom = std::max(bottom, s_Bottom);
			}
		}
		// Only the newly taken in strips are looked at here; the rest is kept up every c_OpenEvery updates.
		bool wasActive = oldRight > oldLeft && oldBottom > oldTop;
		if (!wasActive) {
			WorkOutOpenness(materialBitmap, s_Left, s_Top, s_Right, s_Bottom);
		} else {
			WorkOutOpenness(materialBitmap, s_Left, s_Top, s_Right, oldTop);
			WorkOutOpenness(materialBitmap, s_Left, oldBottom, s_Right, s_Bottom);
			WorkOutOpenness(materialBitmap, s_Left, oldTop, oldLeft, oldBottom);
			WorkOutOpenness(materialBitmap, oldRight, oldTop, s_Right, oldBottom);
		}
	}

	void Empty() {
		for (int y = s_Top; y < s_Bottom; ++y) {
			for (int x = s_Left; x < s_Right; ++x) {
				size_t cell = Index(x, y);
				for (int kind = 0; kind < c_Kinds; ++kind) {
					s_Gas[kind][cell] = 0.0F;
				}
				s_Burning[cell] = 0;
			}
		}
		s_Left = s_Right = s_Top = s_Bottom = 0;
		s_AnyBurning = false;
	}

	/// Gas evens out between open cells beside each other, and buoyant gas trades places with the air above or below.
	/// Gas that reaches the scene's edge (a side that doesn't wrap, the top or the bottom) goes on out of it, as into empty air, and is gone.
	/// @return The thickest any gas is anywhere, to know when it has all gone.
	float Spread() {
		int width = s_GridWidth;
		bool sidesOpen = !g_SceneMan.SceneWrapsX();
		bool topAndBottomOpen = !g_SceneMan.SceneWrapsY();
		for (int kind = 0; kind < c_Kinds; ++kind) {
			std::vector<float>& gas = s_Gas[kind];
			std::vector<float>& change = s_Change[kind];
			float buoyancy = c_Gases[kind].Buoyancy;
			for (int y = s_Top; y < s_Bottom; ++y) {
				for (int x = s_Left; x < s_Right; ++x) {
					change[Index(x, y)] = 0.0F;
				}
			}
			for (int y = s_Top; y < s_Bottom; ++y) {
				for (int x = s_Left; x < s_Right; ++x) {
					int cell = y * width + x;
					if (!s_Open[cell]) {
						continue;
					}
					float here = gas[cell];
					if (x + 1 < s_Right && s_Open[cell + 1]) {
						float there = gas[cell + 1];
						float flow = std::clamp((here - there) * c_Spread, -there * 0.25F, here * 0.25F);
						change[cell] -= flow;
						change[cell + 1] += flow;
					}
					if (y + 1 < s_Bottom && s_Open[cell + width]) {
						float below = gas[cell + width];
						// Downwards positive: evening out, then heavy gas sinking into the air below and light gas rising into the air above.
						float flow = (here - below) * c_Spread;
						flow += buoyancy < 0.0F ? -buoyancy * c_Rise * here * std::max(0.0F, 1.0F - below) : -buoyancy * c_Rise * below * std::max(0.0F, 1.0F - here);
						flow = std::clamp(flow, -below * 0.25F, here * 0.25F);
						change[cell] -= flow;
						change[cell + width] += flow;
					}
					// Out past the scene's edge, into air with no gas in it: evening out, and buoyant gas rising out of the top or sinking out of the bottom.
					// (Each edge takes the place of the neighbour that isn't there, so at most a quarter of the cell each way, as between cells.)
					if (sidesOpen && x == 0) {
						change[cell] -= here * c_Spread;
					}
					if (sidesOpen && x == width - 1) {
						change[cell] -= here * c_Spread;
					}
					if (topAndBottomOpen && y == 0) {
						change[cell] -= std::min(here * (c_Spread + std::max(0.0F, buoyancy) * c_Rise), here * 0.25F);
					}
					if (topAndBottomOpen && y == s_GridHeight - 1) {
						change[cell] -= std::min(here * (c_Spread - std::min(0.0F, buoyancy) * c_Rise), here * 0.25F);
					}
				}
			}
		}
		float thickest = 0.0F;
		for (int kind = 0; kind < c_Kinds; ++kind) {
			std::vector<float>& gas = s_Gas[kind];
			const std::vector<float>& change = s_Change[kind];
			for (int y = s_Top; y < s_Bottom; ++y) {
				float keeps = c_Gases[kind].Keeps;
				for (int x = s_Left; x < s_Right; ++x) {
					size_t cell = Index(x, y);
					// (Ground or liquid that fills a cell with gas in it pushes the gas out into the cells around next update: what was there is spread to them.)
					float value = s_Open[cell] ? std::clamp((gas[cell] + change[cell]) * keeps, 0.0F, c_MaxThickness) : 0.0F;
					if (!s_Open[cell] && gas[cell] > 0.0F) {
						float share = gas[cell] * 0.25F;
						for (int side = 0; side < 4; ++side) {
							int nx = x + (side == 0 ? 1 : side == 1 ? -1 : 0);
							int ny = y + (side == 2 ? 1 : side == 3 ? -1 : 0);
							if (nx >= s_Left && ny >= s_Top && nx < s_Right && ny < s_Bottom && s_Open[Index(nx, ny)]) {
								gas[Index(nx, ny)] += share;
							}
						}
					}
					gas[cell] = value < c_Trace * 0.1F ? 0.0F : value;
					thickest = std::max(thickest, gas[cell]);
				}
			}
		}
		return thickest;
	}

	/// A side of the active area the gas has reached grows, so the gas goes on past it.
	void FollowGas(const BITMAP* materialBitmap) {
		auto any = [](int left, int top, int right, int bottom) {
			for (int y = std::max(top, s_Top); y < std::min(bottom, s_Bottom); ++y) {
				for (int x = std::max(left, s_Left); x < std::min(right, s_Right); ++x) {
					for (int kind = 0; kind < c_Kinds; ++kind) {
						if (s_Gas[kind][Index(x, y)] > c_Trace) {
							return true;
						}
					}
				}
			}
			return false;
		};
		int left = s_Left;
		int top = s_Top;
		int right = s_Right;
		int bottom = s_Bottom;
		Grow(materialBitmap, any(left, top, left + 1, bottom) ? left - 3 : left, any(left, top, right, top + 1) ? top - 3 : top, any(right - 1, top, right, bottom) ? right + 3 : right, any(left, bottom - 1, right, bottom) ? bottom + 3 : bottom);
	}

	MovableObject* CreateEffect(const char* presetName) {
		const Entity* preset = g_PresetMan.GetEntityPreset("MOSParticle", presetName, "Base.rte");
		return preset ? dynamic_cast<MovableObject*>(preset->Clone()) : nullptr;
	}

	/// What there is to see of the gas: toxic gas as short-lived green puffs, steam as steam puffs, smoke as haze in the air (the fog volume,
	/// drawn only); methane can't be seen. As much as the Gas shown setting says, and no more than a couple of dozen puffs an update.
	void Show(long long update) {
		float shown = GasGrid::GetShown();
		if (shown <= 0.0F || (update & 3) != 0) {
			return;
		}
		int puffs = static_cast<int>(24.0F * shown);
		for (int y = s_Top; y < s_Bottom; ++y) {
			for (int x = s_Left; x < s_Right; ++x) {
				size_t cell = Index(x, y);
				Vector middle(static_cast<float>(x * c_Cell + c_Cell / 2), static_cast<float>(y * c_Cell + c_Cell / 2));
				if (float smoke = s_Gas[GasGrid::Smoke][cell]; smoke > 0.05F && ((x + y + static_cast<int>(update / 4)) & 3) == 0) {
					g_PostProcessMan.RegisterFog(middle, 14.0F, std::min(smoke * 0.12F * shown, 0.5F));
				}
				for (int kind: {GasGrid::Toxic, GasGrid::Steam}) {
					float thickness = s_Gas[kind][cell];
					if (puffs > 0 && thickness > 0.08F && Random01() < thickness * 0.15F * shown) {
						if (MovableObject* puff = CreateEffect(kind == GasGrid::Toxic ? "Toxic Gas Ball" : "Steam Puff")) {
							puff->SetPos(middle + Vector((Random01() - 0.5F) * c_Cell, (Random01() - 0.5F) * c_Cell));
							puff->SetVel(Vector((Random01() - 0.5F) * 0.6F, (Random01() - 0.5F) * 0.6F));
							puff->SetLifetime(1200);
							puff->SetToHitMOs(false);
							g_MovableMan.AddParticle(puff);
							--puffs;
						}
					}
				}
			}
		}
	}
} // namespace

void GasGrid::SetEnabled(bool enabled) {
	if (s_Enabled != enabled) {
		s_Enabled = enabled;
		Clear();
	}
}

void GasGrid::SetShown(float shown) {
	s_Shown = std::clamp(shown, 0.0F, 2.0F);
}

void GasGrid::Add(const Vector& position, Kind kind, float amount) {
	if (!s_Enabled || kind < 0 || kind >= KindCount || !std::isfinite(amount) || amount <= 0.0F) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	if (s_Requests.size() < 20000) {
		s_Requests.push_back({position.GetFloorIntX(), position.GetFloorIntY(), kind, amount});
	}
}

float GasGrid::Get(const Vector& position, Kind kind) {
	if (!Active() || kind < 0 || kind >= KindCount) {
		return 0.0F;
	}
	int cell = CellAt(position);
	return cell >= 0 && InActiveArea(cell) ? s_Gas[kind][cell] : 0.0F;
}

void GasGrid::VisitObscuring(const std::function<void(int x, int y, float amount)>& visit) {
	if (!s_Enabled || !Active()) {
		return;
	}
	for (int y = s_Top; y < s_Bottom; ++y) {
		for (int x = s_Left; x < s_Right; ++x) {
			size_t cell = Index(x, y);
			float amount = 0.0F;
			for (int kind = 0; kind < c_Kinds; ++kind) {
				amount += s_Gas[kind][cell] * c_Gases[kind].Hides;
			}
			if (amount > 0.05F) {
				visit(x * c_Cell + c_Cell / 2, y * c_Cell + c_Cell / 2, amount);
			}
		}
	}
}

int GasGrid::GetActiveCells() {
	return Active() ? (s_Right - s_Left) * (s_Bottom - s_Top) : 0;
}

void GasGrid::Clear() {
	for (int kind = 0; kind < c_Kinds; ++kind) {
		s_Gas[kind].clear();
		s_Change[kind].clear();
	}
	s_Open.clear();
	s_Burning.clear();
	s_GridWidth = s_GridHeight = 0;
	s_Left = s_Right = s_Top = s_Bottom = 0;
	s_AnyBurning = false;
	std::scoped_lock lock(s_QueueMutex);
	s_Requests.clear();
}

void GasGrid::TakeInSmoke() {
	for (const MovableObject* particle: g_MovableMan.m_Particles) {
		// Smoke: weightless air particles that float up (as SmokeGrid tells them), named smoke, so steam and the gas's own puffs aren't.
		if (!particle || particle->GetGlobalAccScalar() >= 0.0F || particle->ToDelete()) {
			continue;
		}
		const MOSParticle* smoke = dynamic_cast<const MOSParticle*>(particle);
		if (!smoke || !smoke->GetAtom() || smoke->GetAtom()->GetMaterial()->GetIndex() != g_MaterialAir || smoke->GetPresetName().find("Smoke") == std::string::npos) {
			continue;
		}
		int cell = CellAt(smoke->GetPos());
		if (cell < 0) {
			continue;
		}
		// A little each update, more for a big puff: a puff that lives its life out leaves about a cell's worth behind.
		float amount = std::clamp(smoke->GetRadius() / 10.0F, 0.2F, 2.0F) * 0.0015F;
		int x = cell % s_GridWidth;
		int y = cell / s_GridWidth;
		if (!InActiveArea(cell)) {
			continue;
		}
		s_Gas[Smoke][Index(x, y)] = std::min(s_Gas[Smoke][Index(x, y)] + amount, c_MaxThickness);
	}
}

void GasGrid::HurtUnits() {
	for (Actor* actor: g_MovableMan.m_Actors) {
		if (!actor || actor->IsDead() || actor->GetHealth() <= 0.0F || dynamic_cast<const ADoor*>(actor) || dynamic_cast<const ACraft*>(actor)) {
			continue;
		}
		int cell = CellAt(actor->GetPos());
		if (cell < 0 || !InActiveArea(cell)) {
			continue;
		}
		float damage = 0.0F;
		// Only what breathes chokes (ActorWater's breath: machines and the like never run out).
		if (float toxic = s_Gas[Toxic][cell]; toxic > 0.1F && ActorWater::GetBreathSeconds(actor) != FLT_MAX) {
			damage += toxic * 2.0F;
		}
		if (float steam = s_Gas[Steam][cell]; steam > 0.15F && actor->GetMetalness() < 0.2F) {
			damage += steam * 1.5F;
		}
		if (damage > 0.0F) {
			actor->AddHealth(-damage);
		}
	}
}

void GasGrid::BurnMethane() {
	// What lights it: burning ground near, a burning unit in the cell, or methane beside that went up last update.
	std::vector<int> lit;
	bool anyFire = TerrainFire::GetCount() > 0;
	for (int y = s_Top; y < s_Bottom; ++y) {
		for (int x = s_Left; x < s_Right; ++x) {
			int cell = y * s_GridWidth + x;
			if (s_Gas[Methane][cell] < c_MethaneBurns) {
				continue;
			}
			bool fire = anyFire && TerrainFire::IsBurningNear(Vector(static_cast<float>(x * c_Cell + c_Cell / 2), static_cast<float>(y * c_Cell + c_Cell / 2)), c_Cell);
			if (!fire && s_AnyBurning) {
				for (int side = 0; side < 4 && !fire; ++side) {
					int nx = x + (side == 0 ? 1 : side == 1 ? -1 : 0);
					int ny = y + (side == 2 ? 1 : side == 3 ? -1 : 0);
					fire = nx >= s_Left && ny >= s_Top && nx < s_Right && ny < s_Bottom && s_Burning[Index(nx, ny)] != 0;
				}
			}
			if (fire) {
				lit.push_back(cell);
			}
		}
	}
	for (Actor* actor: g_MovableMan.m_Actors) {
		if (actor && ActorFire::IsBurning(actor)) {
			if (int cell = CellAt(actor->GetPos()); cell >= 0 && InActiveArea(cell) && s_Gas[Methane][cell] >= c_MethaneBurns) {
				lit.push_back(cell);
			}
		}
	}
	std::sort(lit.begin(), lit.end());
	lit.erase(std::unique(lit.begin(), lit.end()), lit.end());
	if (s_AnyBurning) {
		for (int y = s_Top; y < s_Bottom; ++y) {
			for (int x = s_Left; x < s_Right; ++x) {
				s_Burning[Index(x, y)] = 0;
			}
		}
	}
	s_AnyBurning = !lit.empty();
	int shown = 0;
	for (int cell: lit) {
		float methane = s_Gas[Methane][cell];
		s_Gas[Methane][cell] = 0.0F;
		s_Burning[cell] = 1;
		Vector middle(static_cast<float>((cell % s_GridWidth) * c_Cell + c_Cell / 2), static_cast<float>((cell / s_GridWidth) * c_Cell + c_Cell / 2));
		if (shown < c_MaxBlastsShown && (cell & 1) == 0) {
			EffectsParticles::SpawnExplosion(middle, 400.0F + 1500.0F * methane);
			++shown;
		}
		TerrainFire::QueueIgniteArea(middle, 10.0F);
		// The flash burns whoever is in it.
		for (Actor* actor: g_MovableMan.m_Actors) {
			if (actor && !actor->IsDead() && CellAt(actor->GetPos()) == cell && !dynamic_cast<const ADoor*>(actor)) {
				actor->AddHealth(-10.0F * methane);
			}
		}
	}
}

void GasGrid::Update() {
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
		s_Random = 0x2F6B1C35u;
	}
	const BITMAP* materialBitmap = terrain->GetBitmap();
	if (int gridWidth = (materialBitmap->w + c_Cell - 1) / c_Cell, gridHeight = (materialBitmap->h + c_Cell - 1) / c_Cell; gridWidth != s_GridWidth || gridHeight != s_GridHeight) {
		s_GridWidth = gridWidth;
		s_GridHeight = gridHeight;
		size_t cells = static_cast<size_t>(gridWidth) * static_cast<size_t>(gridHeight);
		for (int kind = 0; kind < c_Kinds; ++kind) {
			s_Gas[kind].assign(cells, 0.0F);
			s_Change[kind].assign(cells, 0.0F);
		}
		s_Open.assign(cells, 0);
		s_Burning.assign(cells, 0);
		s_Left = s_Right = s_Top = s_Bottom = 0;
	}
	long long update = g_TimerMan.GetSimUpdateCount();

	std::vector<GasRequest> requests;
	{
		std::scoped_lock lock(s_QueueMutex);
		requests.swap(s_Requests);
	}
	// (Gas let out from parallel code is sorted so it lands in the same order every time.)
	std::sort(requests.begin(), requests.end(), [](const GasRequest& a, const GasRequest& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : (a.Kind != b.Kind ? a.Kind < b.Kind : a.Amount < b.Amount)); });
	for (const GasRequest& request: requests) {
		int cell = CellAt(Vector(static_cast<float>(request.X), static_cast<float>(request.Y)));
		if (cell < 0) {
			continue;
		}
		int x = cell % s_GridWidth;
		int y = cell / s_GridWidth;
		Grow(materialBitmap, x - 2, y - 2, x + 3, y + 3);
		// (Into the cell if it is open, or else the first open one around it: gas let out against a wall goes into the air beside it.
		// Only inside the active area: gas let out too far from gas already about, past c_MaxSpan, is left out.)
		for (int i = 0; i < 9; ++i) {
			int nx = x + (i == 0 ? 0 : (i - 1) % 3 - 1);
			int ny = y + (i == 0 ? 0 : (i - 1) / 3 - 1);
			if (nx >= s_Left && ny >= s_Top && nx < s_Right && ny < s_Bottom && s_Open[Index(nx, ny)]) {
				float& gas = s_Gas[request.Kind][Index(nx, ny)];
				gas = std::min(gas + request.Amount, c_MaxThickness);
				break;
			}
		}
	}
	// Smoke puffs take in the part of the scene they are in, so smoke can build up there.
	int smokeLeft = INT_MAX;
	int smokeTop = INT_MAX;
	int smokeRight = INT_MIN;
	int smokeBottom = INT_MIN;
	for (const MovableObject* particle: g_MovableMan.m_Particles) {
		if (particle && particle->GetGlobalAccScalar() < 0.0F && !particle->ToDelete() && particle->GetPresetName().find("Smoke") != std::string::npos) {
			if (int cell = CellAt(particle->GetPos()); cell >= 0) {
				smokeLeft = std::min(smokeLeft, cell % s_GridWidth);
				smokeRight = std::max(smokeRight, cell % s_GridWidth);
				smokeTop = std::min(smokeTop, cell / s_GridWidth);
				smokeBottom = std::max(smokeBottom, cell / s_GridWidth);
			}
		}
	}
	if (smokeRight >= smokeLeft) {
		Grow(materialBitmap, smokeLeft - 2, smokeTop - 2, smokeRight + 3, smokeBottom + 3);
	}
	if (!Active()) {
		return;
	}
	TakeInSmoke();
	if (update % c_OpenEvery == 0) {
		WorkOutOpenness(materialBitmap, s_Left, s_Top, s_Right, s_Bottom);
	}
	// (Kept while smoke puffs are about, even with too little gas yet to count, or the area would be given up and taken in again every update.)
	if (Spread() < c_Trace && smokeRight < smokeLeft) {
		Empty();
		return;
	}
	BurnMethane();
	if (update % c_HurtEvery == 0) {
		HurtUnits();
	}
	Show(update);
	FollowGas(materialBitmap);
}
