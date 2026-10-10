#include "TerrainCandle.h"
#include "AirPressure.h"
#include "ConsoleMan.h"
#include "Constants.h"
#include "FluidSim.h"
#include "Material.h"
#include "MovableMan.h"
#include "MovableObject.h"
#include "PostProcessMan.h"
#include "PresetMan.h"
#include "Scene.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "TerrainFire.h"
#include "TimerMan.h"
#include "Vector.h"
#include "WeatherEffects.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <sstream>

using namespace RTE;

namespace {
	constexpr size_t c_MaxLit = 512;
	constexpr size_t c_MaxDrips = 1024;
	constexpr int c_MaxWick = 24; //!< The most pixels a wick has (one drawn three times as big has nine).
	constexpr float c_TicksPerRow = 110.0F; //!< Fire ticks (about 20 a second) a candle takes to burn down a row, however wide: each pixel of the row takes this over its width.
	constexpr int c_RimLag = 2; //!< How many rows the middle burns down ahead of the rim: the dish under the wick.
	constexpr int c_DripEvery = 3; //!< Fire ticks a running drip takes to go down a pixel.
	constexpr int c_GrowTicks = 10; //!< Fire ticks a flame takes to grow to full when lit.

	struct LitCandle {
		int X, Y; //!< The tip of the wick: its topmost pixel.
		short Melt; //!< Fire ticks since the last pixel of wax melted.
		short Age; //!< Fire ticks since it was lit, up to c_GrowTicks.
		short Size; //!< How wide the wick's top is (a candle drawn bigger has a wider one).
		bool Covered; //!< Whether something is over it, so rain can't reach it (looked at now and then).
	};

	struct Drip {
		int X, Y;
		int Color;
		short Steps; //!< How much further it runs before it sets, if it still has the candle beside it.
		signed char Side; //!< Which way the candle is from it.
		signed char Wait;
	};

	std::array<bool, 256> s_Wick{};
	std::array<bool, 256> s_Wax{};
	int s_WaxMaterial = -1; //!< What drips are made of.
	std::vector<LitCandle> s_Lit;
	std::vector<Drip> s_Drips;
	unsigned int s_Random = 0x6C8E9CF5u;
	unsigned int s_LightFlicker = 0x2F6B3A1Du; //!< Render only: the flicker of the candles' lights.
	long long s_Tick = 0;
	std::string s_PendingLoadState;

	struct CandleLight {
		glm::vec2 Position;
		float Strength;
		float Size;
	};
	std::vector<CandleLight> s_Lights; //!< As of the last tick, registered every sim update (scene lights are cleared every update).

	float Random01(unsigned int& state) {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return static_cast<float>(state & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	SLTerrain* CurrentTerrain() {
		Scene* scene = g_SceneMan.GetScene();
		return scene ? scene->GetTerrain() : nullptr;
	}

	/// The terrain, its size, and a wrapped look at its pixels.
	struct Ground {
		SLTerrain* Terrain;
		int Width, Height;
		bool WrapsX;

		bool Wrap(int& x, int y) const {
			if (WrapsX) {
				x = ((x % Width) + Width) % Width;
			}
			return x >= 0 && y >= 0 && x < Width && y < Height;
		}
		int Material(int x, int y) const { return Wrap(x, y) ? Terrain->GetMaterialPixel(x, y) : g_MaterialAir; }
		int Color(int x, int y) const { return Wrap(x, y) ? Terrain->GetFGColorPixel(x, y) : 0; }
		void Set(int x, int y, int material, int color) const {
			if (Wrap(x, y)) {
				Terrain->SetMaterialPixel(x, y, material);
				Terrain->SetFGColorPixel(x, y, color);
			}
		}
		bool Wick(int x, int y) const { return s_Wick[static_cast<unsigned char>(Material(x, y))]; }
		bool Wax(int x, int y) const { return s_Wax[static_cast<unsigned char>(Material(x, y))]; }
		bool Air(int x, int y) const { return Material(x, y) == g_MaterialAir; }
		bool Liquid(int x, int y) const { return FluidSim::IsLiquid(Material(x, y)); }
	};

	bool GetGround(Ground& ground) {
		SLTerrain* terrain = CurrentTerrain();
		if (!terrain) {
			return false;
		}
		ground = Ground{terrain, terrain->GetBitmap()->w, terrain->GetBitmap()->h, g_SceneMan.SceneWrapsX()};
		return true;
	}

	/// The changed area this tick, for the pathfinder.
	struct Changed {
		int MinX = INT32_MAX, MinY = INT32_MAX, MaxX = INT32_MIN, MaxY = INT32_MIN;
		void Add(int x, int y) {
			MinX = std::min(MinX, x);
			MinY = std::min(MinY, y);
			MaxX = std::max(MaxX, x);
			MaxY = std::max(MaxY, y);
		}
		void Report(SLTerrain* terrain) const {
			if (MaxX >= MinX) {
				terrain->AddUpdatedMaterialArea(Box(Vector(static_cast<float>(MinX), static_cast<float>(MinY)), static_cast<float>(MaxX - MinX + 1), static_cast<float>(MaxY - MinY + 1)));
			}
		}
	};

	/// The pixels of the wick whose tip is at a point (4-connected, at most c_MaxWick of them).
	void FindWick(const Ground& ground, int x, int y, std::vector<glm::ivec2>& wick) {
		wick.clear();
		if (!ground.Wick(x, y)) {
			return;
		}
		wick.emplace_back(x, y);
		for (size_t i = 0; i < wick.size() && wick.size() < c_MaxWick; ++i) {
			const glm::ivec2 at = wick[i];
			for (const glm::ivec2 step: {glm::ivec2(0, 1), glm::ivec2(-1, 0), glm::ivec2(1, 0), glm::ivec2(0, -1)}) {
				glm::ivec2 next = at + step;
				if (ground.Wick(next.x, next.y) && std::find(wick.begin(), wick.end(), next) == wick.end()) {
					wick.push_back(next);
					if (wick.size() >= c_MaxWick) {
						break;
					}
				}
			}
		}
	}

	/// The tip of the wick a pixel is part of: up its column as far as it goes, then the middle of the wick's top row.
	glm::ivec2 WickTip(const Ground& ground, int x, int y) {
		std::vector<glm::ivec2> wick;
		FindWick(ground, x, y, wick);
		if (wick.empty()) {
			return glm::ivec2(x, y);
		}
		int top = wick.front().y;
		for (const glm::ivec2& pixel: wick) {
			top = std::min(top, pixel.y);
		}
		int left = INT32_MAX;
		int right = INT32_MIN;
		for (const glm::ivec2& pixel: wick) {
			if (pixel.y == top) {
				left = std::min(left, pixel.x);
				right = std::max(right, pixel.x);
			}
		}
		return glm::ivec2((left + right) / 2, top);
	}

	MovableObject* CreateEffect(const char* className, const char* presetName) {
		const Entity* preset = g_PresetMan.GetEntityPreset(className, presetName, "Base.rte");
		return preset ? dynamic_cast<MovableObject*>(preset->Clone()) : nullptr;
	}

	/// The thin curl of smoke off a wick that's just gone out.
	void SmokeWisp(int x, int y) {
		for (int i = 0; i < 2; ++i) {
			if (MovableObject* smoke = CreateEffect("MOSParticle", "Tiny Smoke Ball 1")) {
				smoke->SetPos(Vector(static_cast<float>(x) + 0.5F, static_cast<float>(y) - 1.0F - static_cast<float>(i) * 2.0F));
				smoke->SetVel(Vector((Random01(s_Random) - 0.5F) * 0.4F, -0.6F - Random01(s_Random) * 0.5F));
				g_MovableMan.AddParticle(smoke);
			}
		}
	}

	/// Across the middle of the top of a candle's wick, in scene pixels.
	float TipMiddle(const LitCandle& candle) {
		int size = std::max<int>(candle.Size, 1);
		return static_cast<float>(candle.X - (size - 1) / 2) + static_cast<float>(size) * 0.5F;
	}

	enum class Outcome { Burning, Out, Gone };

	/// Melts a pixel of the candle's wax: from the top down, the middle first, the rim c_RimLag rows behind, so a dish forms under the wick.
	/// Some of the rim runs down the outside as a drip.
	/// @return Whether there was any wax to melt.
	bool MeltOne(const Ground& ground, const LitCandle& candle, int wickBottom, int left, int right, Changed& changed) {
		int bestX = 0;
		int bestY = 0;
		int bestScore = INT32_MAX;
		int bestAcross = INT32_MAX;
		for (int y = wickBottom - 4; y <= wickBottom + 3; ++y) {
			for (int x = left; x <= right; ++x) {
				if (!ground.Wax(x, y) || ground.Wax(x, y - 1)) {
					continue;
				}
				bool rim = x == left || x == right;
				int score = y + (rim ? c_RimLag : 0);
				int across = std::abs(x - candle.X);
				if (score < bestScore || (score == bestScore && across < bestAcross)) {
					bestScore = score;
					bestAcross = across;
					bestX = x;
					bestY = y;
				}
			}
		}
		if (bestScore == INT32_MAX) {
			return false;
		}
		int color = ground.Color(bestX, bestY);
		ground.Set(bestX, bestY, g_MaterialAir, ColorKeys::g_MaskColor);
		changed.Add(bestX, bestY);
		// Wax from the rim spills over and runs down the outside; from the middle it pools, and is burnt.
		bool rim = bestX == left || bestX == right;
		if (s_Drips.size() < c_MaxDrips && s_WaxMaterial > 0 && Random01(s_Random) < (rim ? 0.55F : 0.04F)) {
			int side = bestX == left && (bestX != right || Random01(s_Random) < 0.5F) ? -1 : 1;
			int dripX = (side < 0 ? left : right) + side;
			int dripY = bestY + 1;
			if (ground.Air(dripX, dripY)) {
				ground.Set(dripX, dripY, s_WaxMaterial, color);
				changed.Add(dripX, dripY);
				s_Drips.push_back(Drip{dripX, dripY, color, static_cast<short>(1 + static_cast<int>(Random01(s_Random) * 12.0F)), static_cast<signed char>(-side), c_DripEvery});
			}
		}
		return true;
	}

	/// Burns a lit candle for one fire tick.
	Outcome Burn(const Ground& ground, LitCandle& candle, Changed& changed, std::vector<glm::ivec2>& wick) {
		// The wick may have moved (fallen with the ground it stood on) or gone (dug out, blown up).
		if (!ground.Wick(candle.X, candle.Y)) {
			bool found = false;
			for (int dy = -2; dy <= 3 && !found; ++dy) {
				for (int dx = -2; dx <= 2 && !found; ++dx) {
					if (ground.Wick(candle.X + dx, candle.Y + dy)) {
						glm::ivec2 tip = WickTip(ground, candle.X + dx, candle.Y + dy);
						ground.Wrap(tip.x, tip.y);
						candle.X = tip.x;
						candle.Y = tip.y;
						found = true;
					}
				}
			}
			if (!found) {
				return Outcome::Gone;
			}
		}
		Vector flame(static_cast<float>(candle.X) + 0.5F, static_cast<float>(candle.Y) - 2.0F);
		// Water reaching the flame or the wick puts it out with a hiss.
		for (int dy = -3; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (ground.Liquid(candle.X + dx, candle.Y + dy)) {
					TerrainFire::SpawnSteam(flame, 1);
					return Outcome::Out;
				}
			}
		}
		// A flame needs air: something over the wick smothers it.
		if (!ground.Air(candle.X, candle.Y - 1) && !ground.Wick(candle.X, candle.Y - 1)) {
			return Outcome::Out;
		}
		// A strong wind blows it out, unless it's in the lee of something (a breeze only leans it); a blast passing does, wherever it is.
		// (The weather's own wind, not the air's carrying strength, which is turned up to blow smoke about.)
		float wind = AirPressure::IsOn() ? WeatherEffects::GetWind() : 0.0F;
		float gust = std::abs(wind) - 0.6F;
		if (gust > 0.0F && Random01(s_Random) < gust * 0.05F && !AirPressure::IsSheltered(flame, wind)) {
			return Outcome::Out;
		}
		if (AirPressure::GetFlow(flame).GetMagnitude() > 0.6F) {
			return Outcome::Out;
		}
		// Rain and snow put it out in the open, sooner or later.
		float rain = WeatherEffects::GetRain();
		float snow = WeatherEffects::GetSnow();
		if (rain > 0.0F || snow > 0.0F) {
			if ((s_Tick + candle.X) % 20 == 0) {
				candle.Covered = false;
				for (int y = candle.Y - 2; y > candle.Y - 160 && y >= 0; --y) {
					if (!ground.Air(candle.X, y)) {
						candle.Covered = true;
						break;
					}
				}
			}
			if (!candle.Covered && Random01(s_Random) < 0.006F * rain + 0.003F * snow) {
				TerrainFire::SpawnSteam(flame, 1);
				return Outcome::Out;
			}
		}
		// What the flame touches catches: grass and leaves hanging into it, another candle's wick held to it.
		if (Random01(s_Random) < 0.12F) {
			int x = candle.X + static_cast<int>(std::floor(Random01(s_Random) * 3.0F)) - 1;
			int y = candle.Y - 1 - static_cast<int>(Random01(s_Random) * 4.0F);
			if (ground.Wrap(x, y) && TerrainFire::IsFlammable(ground.Material(x, y))) {
				TerrainFire::QueueIgnite(x, y);
			}
		}
		if (candle.Age < c_GrowTicks) {
			++candle.Age;
		}

		// The wax under the wick: the row the wick stands on, as wide as the candle is there.
		FindWick(ground, candle.X, candle.Y, wick);
		int wickBottom = candle.Y;
		candle.Size = 0;
		for (const glm::ivec2& pixel: wick) {
			wickBottom = std::max(wickBottom, pixel.y);
			candle.Size += pixel.y == candle.Y ? 1 : 0;
		}
		candle.Size = std::clamp<short>(candle.Size, 1, 4);
		// With the wax gone from under it, the wick sinks into what's left; with nothing but ground under it, the candle has burnt down.
		bool anyWax = false;
		bool onGround = false;
		for (const glm::ivec2& pixel: wick) {
			if (ground.Wick(pixel.x, pixel.y + 1)) {
				continue;
			}
			if (ground.Wax(pixel.x, pixel.y + 1)) {
				anyWax = true;
			} else if (!ground.Air(pixel.x, pixel.y + 1)) {
				onGround = true;
			}
		}
		if (!anyWax && !onGround) {
			// Bottom up, so the wick doesn't overwrite itself.
			std::sort(wick.begin(), wick.end(), [](const glm::ivec2& a, const glm::ivec2& b) { return a.y != b.y ? a.y > b.y : a.x < b.x; });
			for (const glm::ivec2& pixel: wick) {
				int material = ground.Material(pixel.x, pixel.y);
				int color = ground.Color(pixel.x, pixel.y);
				ground.Set(pixel.x, pixel.y, g_MaterialAir, ColorKeys::g_MaskColor);
				ground.Set(pixel.x, pixel.y + 1, material, color);
				changed.Add(pixel.x, pixel.y);
				changed.Add(pixel.x, pixel.y + 1);
			}
			++candle.Y;
			return Outcome::Burning;
		}
		if (onGround && !anyWax) {
			// Burnt down: the last of the wick burns away in the puddle of wax left.
			for (const glm::ivec2& pixel: wick) {
				ground.Set(pixel.x, pixel.y, g_MaterialAir, ColorKeys::g_MaskColor);
				changed.Add(pixel.x, pixel.y);
			}
			SmokeWisp(candle.X, candle.Y);
			return Outcome::Gone;
		}
		int left = candle.X;
		int right = candle.X;
		while (right - left < 32 && ground.Wax(left - 1, wickBottom + 1)) {
			--left;
		}
		while (right - left < 32 && ground.Wax(right + 1, wickBottom + 1)) {
			++right;
		}
		// The rim may stand out wider than the row under the wick: take it in too.
		for (int y = wickBottom - 4; y <= wickBottom; ++y) {
			if (ground.Wax(left - 1, y)) {
				--left;
			}
			if (ground.Wax(right + 1, y)) {
				++right;
			}
		}
		int across = right - left + 1;
		int interval = std::clamp(static_cast<int>(c_TicksPerRow / static_cast<float>(across)), 4, 60);
		if (++candle.Melt >= interval) {
			candle.Melt = 0;
			MeltOne(ground, candle, wickBottom, left, right, changed);
		}
		return Outcome::Burning;
	}

	/// Runs the drips down a pixel, each in its turn; a drip sets where it stops.
	void RunDrips(const Ground& ground, Changed& changed) {
		size_t kept = 0;
		for (size_t i = 0; i < s_Drips.size(); ++i) {
			Drip drip = s_Drips[i];
			if (!ground.Wax(drip.X, drip.Y)) {
				// Dug out or melted away.
				continue;
			}
			if (--drip.Wait > 0) {
				s_Drips[kept++] = drip;
				continue;
			}
			drip.Wait = c_DripEvery;
			// It runs on while there's air under it, as far as it has left in it, as long as it clings to the candle; off the candle it falls till it lands.
			bool clinging = ground.Wax(drip.X + drip.Side, drip.Y + 1) || ground.Wax(drip.X + drip.Side, drip.Y);
			if (!ground.Air(drip.X, drip.Y + 1) || (clinging && drip.Steps <= 0)) {
				continue;
			}
			ground.Set(drip.X, drip.Y, g_MaterialAir, ColorKeys::g_MaskColor);
			changed.Add(drip.X, drip.Y);
			++drip.Y;
			ground.Set(drip.X, drip.Y, s_WaxMaterial, drip.Color);
			changed.Add(drip.X, drip.Y);
			--drip.Steps;
			s_Drips[kept++] = drip;
		}
		s_Drips.resize(kept);
	}

	void RegisterLights() {
		for (const CandleLight& light: s_Lights) {
			// A candle's flame is steady: its light breathes a little, no more.
			float flicker = 0.93F + 0.07F * Random01(s_LightFlicker);
			g_PostProcessMan.RegisterLight(Vector(light.Position.x, light.Position.y), glm::vec3(255.0F, 175.0F, 95.0F), 45.0F * std::sqrt(light.Size), 0.3F * light.Strength * flicker, LightSource::Fire);
		}
	}
} // namespace

void TerrainCandle::BuildTables() {
	s_Wick.fill(false);
	s_Wax.fill(false);
	s_WaxMaterial = -1;
	for (int id = 1; id < 256; ++id) {
		const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
		if (!material || material->GetIndex() != id) {
			continue;
		}
		const std::string& name = material->GetPresetName();
		s_Wick[id] = material->GetBehaviour().Burns == "Wick";
		s_Wax[id] = name.find("Wax") != std::string::npos;
		if (name == "Candle Wax" || (s_Wax[id] && s_WaxMaterial < 0)) {
			s_WaxMaterial = id;
		}
	}
}

bool TerrainCandle::IsWick(int materialID) {
	return materialID > 0 && materialID < 256 && s_Wick[materialID];
}

bool TerrainCandle::IsWax(int materialID) {
	return materialID > 0 && materialID < 256 && s_Wax[materialID];
}

void TerrainCandle::Light(int x, int y) {
	Ground ground;
	if (s_Lit.size() >= c_MaxLit || !GetGround(ground) || !ground.Wick(x, y)) {
		return;
	}
	glm::ivec2 tip = WickTip(ground, x, y);
	ground.Wrap(tip.x, tip.y);
	for (const LitCandle& candle: s_Lit) {
		if (candle.X == tip.x && candle.Y == tip.y) {
			return;
		}
	}
	// It needs air over the wick to burn.
	if (!ground.Air(tip.x, tip.y - 1)) {
		return;
	}
	s_Lit.push_back(LitCandle{tip.x, tip.y, 0, 0, 1, true});
}

void TerrainCandle::LightInArea(int centerX, int centerY, int radius, float chance) {
	Ground ground;
	if (!GetGround(ground) || radius <= 0) {
		return;
	}
	for (int dy = -radius; dy <= radius; ++dy) {
		for (int dx = -radius; dx <= radius; ++dx) {
			if (dx * dx + dy * dy <= radius * radius && ground.Wick(centerX + dx, centerY + dy) && !ground.Wick(centerX + dx, centerY + dy - 1) && Random01(s_Random) < chance) {
				Light(centerX + dx, centerY + dy);
			}
		}
	}
}

void TerrainCandle::Douse(int centerX, int centerY, int radius) {
	Ground ground;
	if (s_Lit.empty() || !GetGround(ground)) {
		return;
	}
	int reach = radius + 3;
	s_Lit.erase(std::remove_if(s_Lit.begin(), s_Lit.end(), [&](const LitCandle& candle) {
		int dx = candle.X - centerX;
		if (ground.WrapsX) {
			dx = ((dx % ground.Width) + ground.Width + ground.Width / 2) % ground.Width - ground.Width / 2;
		}
		int dy = candle.Y - 2 - centerY;
		if (dx * dx + dy * dy > reach * reach) {
			return false;
		}
		TerrainFire::SpawnSteam(Vector(static_cast<float>(candle.X), static_cast<float>(candle.Y) - 2.0F), 1);
		return true;
	}),
	            s_Lit.end());
}

void TerrainCandle::Update(bool tick) {
	if (!tick) {
		RegisterLights();
		return;
	}
	++s_Tick;
	Ground ground;
	if (!GetGround(ground) || (s_Lit.empty() && s_Drips.empty())) {
		s_Lights.clear();
		return;
	}
	Changed changed;
	std::vector<glm::ivec2> wick;
	size_t kept = 0;
	for (size_t i = 0; i < s_Lit.size(); ++i) {
		LitCandle candle = s_Lit[i];
		switch (Burn(ground, candle, changed, wick)) {
			case Outcome::Burning:
				s_Lit[kept++] = candle;
				break;
			case Outcome::Out:
				SmokeWisp(candle.X, candle.Y);
				break;
			case Outcome::Gone:
				break;
		}
	}
	s_Lit.resize(kept);
	RunDrips(ground, changed);
	changed.Report(ground.Terrain);

	s_Lights.clear();
	for (const LitCandle& candle: s_Lit) {
		float size = static_cast<float>(candle.Size);
		s_Lights.push_back({glm::vec2(TipMiddle(candle), static_cast<float>(candle.Y) - 3.0F * size), static_cast<float>(candle.Age) / static_cast<float>(c_GrowTicks), size});
	}
	RegisterLights();
}

void TerrainCandle::GetFlames(const glm::vec2& screenOrigin, int width, int height, std::vector<Flame>& flames) {
	if (s_Lit.empty()) {
		return;
	}
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	bool wraps = g_SceneMan.SceneWrapsX();
	float wind = AirPressure::IsOn() ? WeatherEffects::GetWind() : 0.0F;
	for (const LitCandle& candle: s_Lit) {
		glm::vec2 position = glm::vec2(TipMiddle(candle), static_cast<float>(candle.Y)) - screenOrigin;
		if (wraps) {
			if (position.x < (static_cast<float>(width) - sceneWidth) * 0.5F) {
				position.x += sceneWidth;
			} else if (position.x > (static_cast<float>(width) + sceneWidth) * 0.5F) {
				position.x -= sceneWidth;
			}
		}
		if (position.x < -8.0F || position.y < -2.0F || position.x > static_cast<float>(width) + 8.0F || position.y > static_cast<float>(height) + 12.0F) {
			continue;
		}
		// The wind leans the flame over, less in the lee of something.
		float lean = std::clamp(wind * 0.8F, -1.0F, 1.0F);
		if (lean != 0.0F && AirPressure::IsSheltered(Vector(static_cast<float>(candle.X), static_cast<float>(candle.Y) - 2.0F), wind)) {
			lean *= 0.3F;
		}
		flames.push_back(Flame{position, static_cast<float>(std::max<short>(candle.Size, 1)), static_cast<float>(candle.Age) / static_cast<float>(c_GrowTicks), lean});
	}
}

int TerrainCandle::GetCount() {
	return static_cast<int>(s_Lit.size());
}

std::string TerrainCandle::GetSaveState() {
	std::ostringstream stream;
	stream << s_Random;
	for (const LitCandle& candle: s_Lit) {
		stream << ' ' << candle.X << ' ' << candle.Y << ' ' << candle.Melt << ' ' << candle.Age;
	}
	return stream.str();
}

void TerrainCandle::SetPendingLoadState(const std::string& state) {
	s_PendingLoadState = state;
}

void TerrainCandle::StartScene() {
	Clear();
	s_Random = 0x6C8E9CF5u;
	s_Tick = 0;
	if (!s_PendingLoadState.empty() && CurrentTerrain()) {
		// "random x y melt age" per lit candle. The drips running when it was saved have set where they were.
		std::istringstream stream(s_PendingLoadState);
		unsigned int random = 0;
		stream >> random;
		if (random != 0) {
			s_Random = random;
		}
		int x = 0;
		int y = 0;
		int melt = 0;
		int age = 0;
		while (stream >> x >> y >> melt >> age && s_Lit.size() < c_MaxLit) {
			s_Lit.push_back(LitCandle{x, y, static_cast<short>(melt), static_cast<short>(std::clamp(age, 0, c_GrowTicks)), 1, true});
		}
		g_ConsoleMan.PrintString("SYSTEM: Relit " + std::to_string(s_Lit.size()) + " candles from the saved game.");
	}
	s_PendingLoadState.clear();
}

void TerrainCandle::Clear() {
	s_Lit.clear();
	s_Drips.clear();
	s_Lights.clear();
}
