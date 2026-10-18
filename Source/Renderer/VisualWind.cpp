#include "VisualWind.h"

#include "SceneMan.h"
#include "Vector.h"

#include <algorithm>
#include <cmath>

using namespace RTE;

float VisualWind::s_Wind = 0.0F;
float VisualWind::s_Drift = 0.0F;
double VisualWind::s_LastSeconds = -1.0;

namespace {
	/// Smooth value noise over time: a hashed value at each whole step, eased between them, in -1 to 1.
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
} // namespace

void VisualWind::Update(double smoothSimSeconds, float baseWind) {
	// Over a minute or so the wind's strength wanders, and the breeze with it, now and then turning about.
	float wander = TimeNoise(smoothSimSeconds / 70.0, 11u) * 0.7F + TimeNoise(smoothSimSeconds / 23.0, 12u) * 0.3F;
	float breezeWay = std::clamp(0.35F + 1.3F * (TimeNoise(smoothSimSeconds / 95.0, 13u) * 0.8F + TimeNoise(smoothSimSeconds / 31.0, 14u) * 0.2F), -1.0F, 1.0F);
	float mean = baseWind * (1.0F + 0.4F * wander) + c_Breeze * breezeWay;
	// Every few seconds a gust, stronger than the wind as it is, and lulls between them.
	float gust = TimeNoise(smoothSimSeconds / 4.5, 21u) * 0.65F + TimeNoise(smoothSimSeconds / 1.7, 22u) * 0.35F;
	float rise = std::max(gust * 1.4F - 0.2F, 0.0F);
	float lull = std::max(-gust, 0.0F);
	float way = mean < 0.0F ? -1.0F : 1.0F;
	s_Wind = std::clamp(mean * (1.0F + 0.5F * rise - 0.3F * lull) + way * 10.0F * rise, -800.0F, 800.0F);

	if (s_LastSeconds >= 0.0 && smoothSimSeconds > s_LastSeconds) {
		float seconds = static_cast<float>(std::min(smoothSimSeconds - s_LastSeconds, 0.25));
		s_Drift = std::fmod(s_Drift + (s_Wind - baseWind) * seconds, 65536.0F);
	}
	s_LastSeconds = smoothSimSeconds;
}

bool VisualWind::IsSheltered(const Vector& position, float wind) {
	float side = wind > 0.0F ? 1.0F : -1.0F;
	for (int reach = 12; reach <= 36; reach += 12) {
		if (g_SceneMan.GetTerrMatter(static_cast<int>(position.m_X - side * static_cast<float>(reach)), position.GetFloorIntY()) != g_MaterialAir) {
			return true;
		}
	}
	return false;
}
