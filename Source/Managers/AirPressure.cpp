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
float AirPressure::s_NaturalDrift = 0.0F;
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

	/// Smooth noise along time, -1 to 1: a fixed random value at each whole number, eased between them. The same on every machine.
	float TimeNoise(double t, unsigned int seed) {
		double cell = std::floor(t);
		auto value = [seed](long long at) {
			unsigned int h = static_cast<unsigned int>(at) * 0x9E3779B1u ^ seed * 0x85EBCA77u;
			h ^= h >> 15;
			h *= 0x2C1B3C6Du;
			h ^= h >> 12;
			return static_cast<float>(h & 0xFFFFu) / 32767.5F - 1.0F;
		};
		float f = static_cast<float>(t - cell);
		f = f * f * (3.0F - 2.0F * f);
		long long at = static_cast<long long>(cell);
		return value(at) + (value(at + 1) - value(at)) * f;
	}

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
	struct GustRequest {
		int X;
		int Y;
		float DirX; //!< Unit direction.
		float DirY;
		float Strength;
		float Length; //!< In pixels.
		float Spread; //!< Half the cone's width, in radians.
	};
	std::vector<GustRequest> s_Gusts;

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
		float readily = AirPressure::GetTuning().LiquidThrow * AirPressure::GetOverall();
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
	float strength = s_Tuning.PushStrength * GetOverall();
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
		if (particle->GetGlobalAccScalar() < 0.0F && !particle->HitsMOs()) {
			// Smoke, steam and gas puffs: anything that floats up and hits nothing, whatever it is made of (explosion and flame smoke are
			// "Air Blast" or other materials) and whether a sprite or a single pixel (smoke trails).
			share = 1.0F;
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
	return s_On && s_Wind ? WeatherEffects::GetWind() * std::max(s_Tuning.WindStrength, 0.0F) * GetOverall() : 0.0F;
}

float AirPressure::GetNaturalWind() {
	float base = g_PostProcessMan.GetLightingSettings().Wind;
	float gusts = std::max(s_Tuning.Gusts, 0.0F);
	float shifts = std::max(s_Tuning.Shifts, 0.0F);
	float breeze = std::max(s_Tuning.Breeze, 0.0F);
	if (gusts == 0.0F && shifts == 0.0F && breeze == 0.0F) {
		return base;
	}
	double seconds = static_cast<double>(g_TimerMan.GetSimUpdateCount()) * static_cast<double>(g_TimerMan.GetDeltaTimeSecs());
	// Over a minute or so the wind's strength wanders, and the breeze with it, now and then turning about.
	float wander = TimeNoise(seconds / 70.0, 11u) * 0.7F + TimeNoise(seconds / 23.0, 12u) * 0.3F;
	float breezeWay = std::clamp(0.35F + 1.3F * (TimeNoise(seconds / 95.0, 13u) * 0.8F + TimeNoise(seconds / 31.0, 14u) * 0.2F), -1.0F, 1.0F);
	float mean = base * (1.0F + 0.4F * shifts * wander) + breeze * (shifts > 0.0F ? breezeWay : 1.0F);
	// Every few seconds a gust, stronger than the wind as it is, and lulls between them.
	float gust = TimeNoise(seconds / 4.5, 21u) * 0.65F + TimeNoise(seconds / 1.7, 22u) * 0.35F;
	float rise = std::max(gust * 1.4F - 0.2F, 0.0F) * gusts;
	float lull = std::max(-gust, 0.0F) * gusts;
	float way = mean < 0.0F ? -1.0F : 1.0F;
	float wind = mean * (1.0F + 0.5F * rise - 0.3F * lull) + way * 10.0F * rise;
	return std::clamp(wind, -800.0F, 800.0F);
}

float AirPressure::GetWindSpeed() {
	return s_On && s_Wind ? GetNaturalWind() * std::max(s_Tuning.WindStrength, 0.0F) * GetOverall() : 0.0F;
}

Vector AirPressure::GetPush(const Vector& position) {
	if (!s_On || !s_Enabled || s_Tuning.PushStrength <= 0.0F || GetOverall() <= 0.0F) {
		return Vector();
	}
	Vector flow = GetFlow(position);
	if (flow.MagnitudeIsLessThan(0.05F)) {
		return Vector();
	}
	Vector change = flow * (c_Push * s_Tuning.PushStrength * GetOverall());
	change.CapMagnitude(c_MaxPush);
	return change;
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
	if (!s_On || !s_Enabled || !std::isfinite(energy) || energy <= 0.0F || s_Tuning.BlastStrength <= 0.0F || GetOverall() <= 0.0F) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	if (s_Blasts.size() < 256) {
		s_Blasts.push_back({position.GetFloorIntX(), position.GetFloorIntY(), energy});
	}
}

void AirPressure::Gust(const Vector& position, const Vector& direction, float strength, float length, float spread) {
	if (!s_On || !s_Enabled || !std::isfinite(strength) || strength <= 0.0F || length <= 0.0F || direction.IsZero() || GetOverall() <= 0.0F) {
		return;
	}
	Vector unit = direction.GetNormalized();
	std::scoped_lock lock(s_QueueMutex);
	if (s_Gusts.size() < 64) {
		s_Gusts.push_back({position.GetFloorIntX(), position.GetFloorIntY(), unit.m_X, unit.m_Y, std::min(strength, 40.0F), std::min(length, static_cast<float>(c_Cell * 64)), std::clamp(spread, 0.0F, 360.0F) * 0.5F * c_PI / 180.0F});
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
	s_Gusts.clear();
}

void AirPressure::Update() {
	Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
	if (!terrain) {
		Clear();
		s_NaturalDrift = 0.0F;
		return;
	}
	// How far gusts and shifts have carried things beyond the steady wind (wrapped far out so it stays precise).
	s_NaturalDrift = std::fmod(s_NaturalDrift + (GetNaturalWind() - g_PostProcessMan.GetLightingSettings().Wind) * g_TimerMan.GetDeltaTimeSecs(), 65536.0F);
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
	std::vector<GustRequest> gusts;
	{
		std::scoped_lock lock(s_QueueMutex);
		blasts.swap(s_Blasts);
		gusts.swap(s_Gusts);
	}
	std::sort(gusts.begin(), gusts.end(), [](const GustRequest& a, const GustRequest& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Strength < b.Strength); });
	// (Blasts made from parallel code are sorted so they land in the same order every time.)
	std::sort(blasts.begin(), blasts.end(), [](const BlastRequest& a, const BlastRequest& b) { return a.Y != b.Y ? a.Y < b.Y : (a.X != b.X ? a.X < b.X : a.Energy < b.Energy); });
	// The area is grown for every blast first, so what is open is worked out once for them all.
	for (const BlastRequest& blast: blasts) {
		if (int cell = CellAt(Vector(static_cast<float>(blast.X), static_cast<float>(blast.Y))); cell >= 0) {
			Grow(cell % s_GridWidth - 3, cell / s_GridWidth - 3, cell % s_GridWidth + 4, cell / s_GridWidth + 4);
		}
	}
	for (const GustRequest& gust: gusts) {
		if (int cell = CellAt(Vector(static_cast<float>(gust.X), static_cast<float>(gust.Y))); cell >= 0) {
			int reach = static_cast<int>(gust.Length) / c_Cell + 2;
			Grow(cell % s_GridWidth - reach, cell / s_GridWidth - reach, cell % s_GridWidth + reach + 1, cell / s_GridWidth + reach + 1);
		}
	}
	if ((!blasts.empty() || !gusts.empty()) && Active()) {
		WorkOutOpenness(materialBitmap);
	}
	// A gust sets the air moving its way across every open cell in its cone, most at its start and fading to nothing at its far end; the
	// waves then carry it on from there.
	for (const GustRequest& gust: gusts) {
		int cell = CellAt(Vector(static_cast<float>(gust.X), static_cast<float>(gust.Y)));
		if (cell < 0) {
			continue;
		}
		int originX = cell % s_GridWidth;
		int originY = cell / s_GridWidth;
		int reach = static_cast<int>(gust.Length) / c_Cell + 1;
		float strength = gust.Strength * GetOverall();
		float cosSpread = std::cos(gust.Spread);
		for (int dy = -reach; dy <= reach; ++dy) {
			for (int dx = -reach; dx <= reach; ++dx) {
				int x = originX + dx;
				int y = originY + dy;
				if (x < 0 || y < 0 || x >= s_GridWidth || y >= s_GridHeight) {
					continue;
				}
				int at = y * s_GridWidth + x;
				if (!InActiveArea(at) || s_Open[at] == Solid) {
					continue;
				}
				float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy)) * static_cast<float>(c_Cell);
				if (distance > gust.Length) {
					continue;
				}
				// The cell the gust starts in is always in it; any other only within the cone.
				Vector way = distance > 0.0F ? Vector(static_cast<float>(dx), static_cast<float>(dy)) / (distance / static_cast<float>(c_Cell)) : Vector(gust.DirX, gust.DirY);
				if (distance > 0.0F && gust.Spread < c_PI && way.m_X * gust.DirX + way.m_Y * gust.DirY < cosSpread) {
					continue;
				}
				// A cone blows along itself; all the way round (a ring), each cell blows straight out.
				Vector blow = gust.Spread >= c_PI ? way : Vector(gust.DirX, gust.DirY);
				float share = strength * (1.0F - 0.75F * distance / gust.Length);
				if (x + 1 < s_Right && s_Open[at + 1] != Solid) {
					s_FlowX[at] += blow.m_X * share;
				}
				if (y + 1 < s_Bottom && s_Open[at + s_GridWidth] != Solid) {
					s_FlowY[at] += blow.m_Y * share;
				}
			}
		}
	}
	for (const BlastRequest& blast: blasts) {
		int cell = CellAt(Vector(static_cast<float>(blast.X), static_cast<float>(blast.Y)));
		if (cell < 0) {
			continue;
		}
		int x = cell % s_GridWidth;
		int y = cell / s_GridWidth;
		float pressure = std::clamp(std::sqrt(blast.Energy) * 0.15F, 2.0F, 40.0F) * s_Tuning.BlastStrength * GetOverall();
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
	// Overall also sets how fast the waves travel: more steps of the wave each update (never fewer than one, nor so many it costs too much).
	int substeps = std::clamp(static_cast<int>(std::lround(static_cast<float>(c_Substeps) * GetOverall())), 1, 8);
	for (int i = 0; i < substeps; ++i) {
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
