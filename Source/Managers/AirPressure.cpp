#include "AirPressure.h"
#include "ADoor.h"
#include "Actor.h"
#include "Atom.h"
#include "Constants.h"
#include "FluidSim.h"
#include "MOSParticle.h"
#include "Material.h"
#include "MovableMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "SceneMan.h"
#include "TimerMan.h"
#include "Vector.h"
#include "WeatherEffects.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

using namespace RTE;

bool AirPressure::s_On = true;
AirPressure::Tuning AirPressure::s_Tuning;
bool AirPressure::s_Enabled = true;
bool AirPressure::s_Wind = true;

namespace {
	constexpr int c_Cell = 8; //!< Each cell of the grid covers this many pixels a side.
	constexpr int c_Substeps = 2; //!< Steps of the wave per sim update: with c_WaveRate, a wave front crosses about a cell an update (480 px a second).
	constexpr float c_WaveRate = 0.5F; //!< How hard a difference in pressure pushes the air between two cells, and how much the air arriving raises the pressure. Stable up to 0.7.
	constexpr float c_FlowDamping = 0.985F; //!< What the air's movement keeps each step.
	constexpr float c_PressureDamping = 0.995F; //!< What the pressure keeps each step.
	constexpr float c_SkyRelease = 0.85F; //!< What the pressure keeps each step in the top row, open to the sky.
	constexpr float c_Quiet = 0.03F; //!< Once no cell is this far from still air, the waves are over.
	constexpr int c_MaxSpan = 256; //!< The waves are worked out over at most this many cells across and down (2048 pixels); past that they bounce as off a wall.
	constexpr float c_Push = 0.4F; //!< How hard moving air pushes a weightless thing, in metres a second per update for each unit of flow.
	constexpr float c_MaxPush = 5.0F; //!< The most an update's push may change a thing's speed.
	constexpr float c_ThrowsLiquid = 0.6F; //!< Air pushing up out of liquid faster than this throws it.
	constexpr int c_MaxThrowsPerUpdate = 8;
	constexpr float c_WindSpeed = 4.0F; //!< How fast a full wind carries smoke, in metres a second.

	enum Openness : unsigned char {
		Solid,
		Air,
		Liquid
	};

	std::vector<float> s_Pressure; //!< Above still air, per cell.
	std::vector<float> s_FlowX; //!< The air's movement across each cell's right side, rightwards positive.
	std::vector<float> s_FlowY; //!< The air's movement across each cell's bottom side, downwards positive.
	std::vector<unsigned char> s_Open; //!< Openness per cell, worked out for the cells in the active area each update.
	int s_GridWidth = 0;
	int s_GridHeight = 0;
	int s_Left = 0; //!< The active area, in cells: from s_Left to s_Right (not included), s_Top to s_Bottom. Empty while no wave is going.
	int s_Right = 0;
	int s_Top = 0;
	int s_Bottom = 0;
	const void* s_Scene = nullptr;
	unsigned int s_SceneGeneration = 0;

	struct BlastRequest {
		int X;
		int Y;
		float Energy;
	};
	std::mutex s_QueueMutex;
	std::vector<BlastRequest> s_Blasts;

	bool Active() { return s_Right > s_Left && s_Bottom > s_Top; }

	void Grow(int left, int top, int right, int bottom) {
		left = std::max(left, 0);
		top = std::max(top, 0);
		right = std::min(right, s_GridWidth);
		bottom = std::min(bottom, s_GridHeight);
		if (!Active()) {
			s_Left = left;
			s_Top = top;
			s_Right = right;
			s_Bottom = bottom;
		} else {
			// Never past c_MaxSpan: a side that would take it further stays where it is.
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
	}

	void Quieten() {
		for (int y = s_Top; y < s_Bottom; ++y) {
			for (int x = s_Left; x < s_Right; ++x) {
				size_t cell = static_cast<size_t>(y) * static_cast<size_t>(s_GridWidth) + static_cast<size_t>(x);
				s_Pressure[cell] = 0.0F;
				s_FlowX[cell] = 0.0F;
				s_FlowY[cell] = 0.0F;
			}
		}
		s_Left = s_Right = s_Top = s_Bottom = 0;
	}

	/// The cell a point is in, or -1 off the grid.
	int CellAt(const Vector& position) {
		int x = static_cast<int>(std::floor(position.m_X / static_cast<float>(c_Cell)));
		int y = static_cast<int>(std::floor(position.m_Y / static_cast<float>(c_Cell)));
		if (x < 0 || y < 0 || x >= s_GridWidth || y >= s_GridHeight) {
			return -1;
		}
		return y * s_GridWidth + x;
	}

	bool InActiveArea(int cell) {
		int x = cell % s_GridWidth;
		int y = cell / s_GridWidth;
		return x >= s_Left && x < s_Right && y >= s_Top && y < s_Bottom;
	}

	/// Air or liquid in at least half of four points of the cell lets the wave through.
	void WorkOutOpenness(const BITMAP* materialBitmap) {
		int width = materialBitmap->w;
		int height = materialBitmap->h;
		for (int y = s_Top; y < s_Bottom; ++y) {
			for (int x = s_Left; x < s_Right; ++x) {
				int air = 0;
				int liquid = 0;
				for (int i = 0; i < 4; ++i) {
					int px = std::min(x * c_Cell + ((i & 1) != 0 ? 6 : 2), width - 1);
					int py = std::min(y * c_Cell + ((i & 2) != 0 ? 6 : 2), height - 1);
					int material = materialBitmap->line[py][px];
					if (material == g_MaterialAir) {
						++air;
					} else if (FluidSim::IsLiquid(material)) {
						++liquid;
					}
				}
				s_Open[static_cast<size_t>(y) * static_cast<size_t>(s_GridWidth) + static_cast<size_t>(x)] = air + liquid < 2 ? Solid : (liquid >= 2 ? Liquid : Air);
			}
		}
	}

	/// One step of the wave: differences in pressure push the air between open cells, and the air arriving or leaving raises or lowers the pressure.
	/// @return The largest pressure or movement left anywhere, to know when the waves are over.
	float Step() {
		int width = s_GridWidth;
		// Reaching further is losing less each step: at 2, half as much.
		float reach = std::max(AirPressure::GetTuning().BlastReach, 0.1F);
		float flowDamping = 1.0F - (1.0F - c_FlowDamping) / reach;
		float pressureDamping = 1.0F - (1.0F - c_PressureDamping) / reach;
		for (int y = s_Top; y < s_Bottom; ++y) {
			for (int x = s_Left; x < s_Right; ++x) {
				int cell = y * width + x;
				s_FlowX[cell] = x + 1 < s_Right && s_Open[cell] != Solid && s_Open[cell + 1] != Solid ? (s_FlowX[cell] + c_WaveRate * (s_Pressure[cell] - s_Pressure[cell + 1])) * flowDamping : 0.0F;
				s_FlowY[cell] = y + 1 < s_Bottom && s_Open[cell] != Solid && s_Open[cell + width] != Solid ? (s_FlowY[cell] + c_WaveRate * (s_Pressure[cell] - s_Pressure[cell + width])) * flowDamping : 0.0F;
			}
		}
		float loudest = 0.0F;
		for (int y = s_Top; y < s_Bottom; ++y) {
			for (int x = s_Left; x < s_Right; ++x) {
				int cell = y * width + x;
				if (s_Open[cell] == Solid) {
					s_Pressure[cell] = 0.0F;
					continue;
				}
				float outflow = s_FlowX[cell] - (x > s_Left ? s_FlowX[cell - 1] : 0.0F) + s_FlowY[cell] - (y > s_Top ? s_FlowY[cell - width] : 0.0F);
				float pressure = (s_Pressure[cell] - c_WaveRate * outflow) * (y == 0 ? c_SkyRelease : pressureDamping);
				s_Pressure[cell] = pressure;
				loudest = std::max({loudest, std::abs(pressure), std::abs(s_FlowX[cell]), std::abs(s_FlowY[cell])});
			}
		}
		return loudest;
	}

	/// A side of the active area the wave has reached grows, so the wave goes on past it.
	void FollowWaves() {
		auto loudIn = [](int left, int top, int right, int bottom) {
			for (int y = std::max(top, s_Top); y < std::min(bottom, s_Bottom); ++y) {
				for (int x = std::max(left, s_Left); x < std::min(right, s_Right); ++x) {
					if (std::abs(s_Pressure[static_cast<size_t>(y) * static_cast<size_t>(s_GridWidth) + static_cast<size_t>(x)]) > c_Quiet * 2.0F) {
						return true;
					}
				}
			}
			return false;
		};
		int left = s_Left;
		int top = s_Top;
		int right = s_Right;
		int bottom = s_Bottom;
		Grow(loudIn(left, top, left + 2, bottom) ? left - 4 : left, loudIn(left, top, right, top + 2) ? top - 4 : top, loudIn(right - 2, top, right, bottom) ? right + 4 : right, loudIn(left, bottom - 2, right, bottom) ? bottom + 4 : bottom);
	}

	/// Where the wave runs up through liquid to its surface, it throws the liquid there into the air.
	void ThrowLiquid(long long update) {
		float readily = AirPressure::GetTuning().LiquidThrow;
		if (readily <= 0.0F) {
			return;
		}
		float throwsAbove = c_ThrowsLiquid / readily;
		int throws = 0;
		for (int y = std::max(s_Top, 1); y < s_Bottom && throws < c_MaxThrowsPerUpdate; ++y) {
			for (int x = s_Left; x < s_Right && throws < c_MaxThrowsPerUpdate; ++x) {
				int cell = y * s_GridWidth + x;
				// Upwards out of the cell is its top side's movement, negative; each surface cell throws at most every fourth update.
				float upwards = -s_FlowY[cell - s_GridWidth];
				if (s_Open[cell] == Liquid && s_Open[cell - s_GridWidth] == Air && upwards > throwsAbove && ((update + cell) & 3) == 0) {
					Vector at(static_cast<float>(x * c_Cell + c_Cell / 2), static_cast<float>(y * c_Cell));
					FluidSim::Splash(at, 6.0F, std::clamp(upwards * 0.15F, 0.1F, 0.6F), std::clamp(upwards * 2.5F, 3.0F, 18.0F));
					++throws;
				}
			}
		}
	}

} // namespace

/// The moving air pushes what is in it: a weightless thing (smoke) as fast as the air, a heavy one (a unit) hardly at all.
void AirPressure::PushObjects() {
	float strength = s_Tuning.PushStrength;
	if (strength <= 0.0F) {
		return;
	}
	auto push = [strength](MovableObject* object, float share) {
		if (!object || object->ToDelete() || object->GetPinStrength() > 0.0F) {
			return;
		}
		int cell = CellAt(object->GetPos());
		if (cell < 0 || !InActiveArea(cell) || s_Open[cell] == Solid) {
			return;
		}
		Vector flow = GetFlow(object->GetPos());
		if (flow.MagnitudeIsLessThan(0.05F)) {
			return;
		}
		Vector change = flow * (c_Push * strength * share / (1.0F + std::max(object->GetMass(), 0.0F) * 0.05F));
		change.CapMagnitude(c_MaxPush);
		object->SetVel(object->GetVel() + change);
	};
	for (Actor* actor: g_MovableMan.m_Actors) {
		if (s_Tuning.UnitPush > 0.0F && !dynamic_cast<const ADoor*>(actor)) {
			push(actor, s_Tuning.UnitPush);
		}
	}
	for (MovableObject* item: g_MovableMan.m_Items) {
		push(item, 1.0F);
	}
	for (MovableObject* particle: g_MovableMan.m_Particles) {
		push(particle, 1.0F);
	}
}

/// The weather's wind carries smoke, and fine spray more weakly, along; in the lee of ground upwind of it, it eddies instead.
void AirPressure::BlowSmoke(long long update) {
	float wind = GetWind();
	if (std::abs(wind) < 0.02F) {
		return;
	}
	float time = static_cast<float>(update % 100000) * 0.05F;
	for (MovableObject* particle: g_MovableMan.m_Particles) {
		if (!particle || particle->ToDelete() || particle->GetPinStrength() > 0.0F) {
			continue;
		}
		float share = 0.0F;
		if (particle->GetGlobalAccScalar() < 0.0F) {
			// Smoke: weightless air particles that float up (as SmokeGrid tells them).
			const MOSParticle* smoke = dynamic_cast<const MOSParticle*>(particle);
			share = smoke && smoke->GetAtom() && smoke->GetAtom()->GetMaterial()->GetIndex() == g_MaterialAir ? 1.0F : 0.0F;
		} else if (particle->GetMass() < 0.02F && particle->GetMaterial() && FluidSim::IsLiquid(particle->GetMaterial()->GetIndex())) {
			share = 0.3F;
		}
		if (share <= 0.0F) {
			continue;
		}
		const Vector& position = particle->GetPos();
		bool sheltered = IsSheltered(position, wind);
		float swirl = std::sin(position.m_X * 0.03F + position.m_Y * 0.05F + time);
		Vector velocity = particle->GetVel();
		if (sheltered) {
			// Behind a wall or a ridge: the air turns over, back against the wind and up and down.
			velocity.m_X -= wind * 0.02F * share;
			velocity.m_Y += swirl * 0.08F * std::abs(wind) * share;
		} else {
			velocity.m_X += (wind * c_WindSpeed * share - velocity.m_X) * 0.04F * share;
			velocity.m_Y += swirl * 0.04F * std::abs(wind) * share;
		}
		particle->SetVel(velocity);
	}
}

void AirPressure::SetOn(bool on) {
	if (s_On != on) {
		s_On = on;
		Clear();
	}
}

float AirPressure::GetWind() {
	return s_On && s_Wind ? WeatherEffects::GetWind() * std::max(s_Tuning.WindStrength, 0.0F) : 0.0F;
}

bool AirPressure::IsSheltered(const Vector& position, float wind) {
	float side = wind > 0.0F ? 1.0F : -1.0F;
	for (int reach = 12; reach <= 36; reach += 12) {
		int material = g_SceneMan.GetTerrMatter(static_cast<int>(position.m_X - side * static_cast<float>(reach)), position.GetFloorIntY());
		if (material != g_MaterialAir && !FluidSim::IsLiquid(material)) {
			return true;
		}
	}
	return false;
}

bool AirPressure::GetActiveArea(int& left, int& top, int& right, int& bottom) {
	if (!Active()) {
		return false;
	}
	left = s_Left * c_Cell;
	top = s_Top * c_Cell;
	right = s_Right * c_Cell;
	bottom = s_Bottom * c_Cell;
	return true;
}

int AirPressure::GetCellSize() {
	return c_Cell;
}

void AirPressure::SetEnabled(bool enabled) {
	if (s_Enabled != enabled) {
		s_Enabled = enabled;
		Clear();
	}
}

void AirPressure::Blast(const Vector& position, float energy) {
	if (!s_On || !s_Enabled || !std::isfinite(energy) || energy <= 0.0F || s_Tuning.BlastStrength <= 0.0F) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	if (s_Blasts.size() < 256) {
		s_Blasts.push_back({position.GetFloorIntX(), position.GetFloorIntY(), energy});
	}
}

Vector AirPressure::GetFlow(const Vector& position) {
	if (!Active()) {
		return Vector();
	}
	int cell = CellAt(position);
	if (cell < 0 || !InActiveArea(cell)) {
		return Vector();
	}
	int x = cell % s_GridWidth;
	int y = cell / s_GridWidth;
	float acrossLeft = x > s_Left ? s_FlowX[cell - 1] : 0.0F;
	float acrossTop = y > s_Top ? s_FlowY[cell - s_GridWidth] : 0.0F;
	return Vector((acrossLeft + s_FlowX[cell]) * 0.5F, (acrossTop + s_FlowY[cell]) * 0.5F);
}

float AirPressure::GetPressure(const Vector& position) {
	int cell = Active() ? CellAt(position) : -1;
	return cell >= 0 && InActiveArea(cell) ? s_Pressure[cell] : 0.0F;
}

int AirPressure::GetActiveCells() {
	return Active() ? (s_Right - s_Left) * (s_Bottom - s_Top) : 0;
}

void AirPressure::Clear() {
	s_Pressure.clear();
	s_FlowX.clear();
	s_FlowY.clear();
	s_Open.clear();
	s_GridWidth = s_GridHeight = 0;
	s_Left = s_Right = s_Top = s_Bottom = 0;
	std::scoped_lock lock(s_QueueMutex);
	s_Blasts.clear();
}

void AirPressure::Update() {
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	if (!terrain) {
		Clear();
		return;
	}
	if (!s_On) {
		Clear();
		return;
	}
	long long update = g_TimerMan.GetSimUpdateCount();
	if (s_Wind) {
		BlowSmoke(update);
	}
	if (!s_Enabled) {
		Clear();
		return;
	}
	if (scene != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		Clear();
		s_Scene = scene;
		s_SceneGeneration = g_SceneMan.GetSceneGeneration();
	}
	const BITMAP* materialBitmap = terrain->GetBitmap();
	if (int gridWidth = (materialBitmap->w + c_Cell - 1) / c_Cell, gridHeight = (materialBitmap->h + c_Cell - 1) / c_Cell; gridWidth != s_GridWidth || gridHeight != s_GridHeight) {
		s_GridWidth = gridWidth;
		s_GridHeight = gridHeight;
		size_t cells = static_cast<size_t>(gridWidth) * static_cast<size_t>(gridHeight);
		s_Pressure.assign(cells, 0.0F);
		s_FlowX.assign(cells, 0.0F);
		s_FlowY.assign(cells, 0.0F);
		s_Open.assign(cells, Solid);
		s_Left = s_Right = s_Top = s_Bottom = 0;
	}

	std::vector<BlastRequest> blasts;
	{
		std::scoped_lock lock(s_QueueMutex);
		blasts.swap(s_Blasts);
	}
	// (Blasts made from parallel code are sorted so they land in the same order every time.)
	std::sort(blasts.begin(), blasts.end(), [](const BlastRequest& a, const BlastRequest& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Energy < b.Energy); });
	// The area is grown for every blast first, so what is open is worked out once for them all.
	for (const BlastRequest& blast: blasts) {
		if (int cell = CellAt(Vector(static_cast<float>(blast.X), static_cast<float>(blast.Y))); cell >= 0) {
			Grow(cell % s_GridWidth - 3, cell / s_GridWidth - 3, cell % s_GridWidth + 4, cell / s_GridWidth + 4);
		}
	}
	if (!blasts.empty() && Active()) {
		WorkOutOpenness(materialBitmap);
	}
	for (const BlastRequest& blast: blasts) {
		int cell = CellAt(Vector(static_cast<float>(blast.X), static_cast<float>(blast.Y)));
		if (cell < 0) {
			continue;
		}
		int x = cell % s_GridWidth;
		int y = cell / s_GridWidth;
		float pressure = std::clamp(std::sqrt(blast.Energy) * 0.15F, 2.0F, 40.0F) * s_Tuning.BlastStrength;
		// The blast's own cell and the four around it; one buried in the ground (all solid) puts its pressure into what is open beside it.
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				int nx = x + dx;
				int ny = y + dy;
				// (Only inside the active area: a blast too far from waves already going, past c_MaxSpan, is left out rather than kept where nothing works it out.)
				if (nx >= 0 && ny >= 0 && nx < s_GridWidth && ny < s_GridHeight && InActiveArea(ny * s_GridWidth + nx) && s_Open[ny * s_GridWidth + nx] != Solid) {
					s_Pressure[ny * s_GridWidth + nx] += pressure * (dx == 0 && dy == 0 ? 1.0F : (dx == 0 || dy == 0 ? 0.5F : 0.25F));
				}
			}
		}
	}
	if (!Active()) {
		return;
	}
	WorkOutOpenness(materialBitmap);
	float loudest = 0.0F;
	for (int i = 0; i < c_Substeps; ++i) {
		loudest = Step();
	}
	if (loudest < c_Quiet) {
		Quieten();
		return;
	}
	PushObjects();
	if (FluidSim::IsEnabled()) {
		ThrowLiquid(update);
	}
	FollowWaves();
}
