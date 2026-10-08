#include "WeatherLightning.h"

#include "Activity.h"
#include "ActivityMan.h"
#include "Actor.h"
#include "Constants.h"
#include "EffectsParticles.h"
#include "Material.h"
#include "MovableMan.h"
#include "PostProcessMan.h"
#include "PresetMan.h"
#include "RTETools.h"
#include "SceneLighting.h"
#include "SceneMan.h"
#include "SoundContainer.h"
#include "TerrainFire.h"
#include "TimerMan.h"
#include "Vector.h"
#include "WeatherEffects.h"

#include <algorithm>
#include <cmath>
#include <deque>

using namespace RTE;

WeatherLightning::Strikes WeatherLightning::s_Strikes = WeatherLightning::Strikes::Fires;

namespace {
	SoundContainer* s_Thunder = nullptr; //!< Never deleted: it would outlive the audio system at exit.

	const Scene* s_Scene = nullptr; //!< The scene the storm's schedule is for.
	unsigned int s_SceneGeneration = 0;
	long long s_NextStrikeUpdate = -1; //!< The sim update the storm next strikes on, or -1 for none due.

	/// A well-mixed number from another (SplitMix64's finaliser).
	unsigned long long Mix(unsigned long long value) {
		value += 0x9E3779B97F4A7C15ull;
		value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
		value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
		return value ^ (value >> 31);
	}

	/// Random numbers, 0 to 1, from a seed: the storm's own, so it draws nothing from any other random stream.
	struct StormRandom {
		unsigned long long State;

		float operator()() {
			State = Mix(State);
			return static_cast<float>(State >> 40) / static_cast<float>(1ull << 24);
		}
	};

	/// Sim updates until the storm strikes again: 8 to 32 seconds in a storm just over half intensity, about half that in a downpour.
	long long UpdatesToNextStrike(StormRandom& random, float rain) {
		float storm = std::clamp((rain - 0.5F) * 2.0F, 0.0F, 1.0F);
		float seconds = (8.0F + random() * 24.0F) * (1.2F - 0.6F * storm);
		return std::max(1LL, static_cast<long long>(seconds / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F)));
	}
} // namespace

void WeatherLightning::Strike(const Vector& target, const std::function<float()>& random01, bool harmUnits) {
	Vector ground = target;
	while (g_SceneMan.GetTerrMatter(ground.GetFloorIntX(), ground.GetFloorIntY()) == g_MaterialAir && ground.m_Y < static_cast<float>(g_SceneMan.GetSceneHeight() - 1)) {
		ground.m_Y += 1.0F;
	}
	// From the open sky over the point, at most 480 px above the ground: up through the air from where it lands, not from the top of the
	// view. (From the view, the bolt's particles, their number and places, went by where the camera was, so a storm ran differently
	// in a replay; and zoomed in or underground the bolt started inside the earth.)
	float top = ground.m_Y;
	while (top > 0.0F && top > ground.m_Y - 480.0F && g_SceneMan.GetTerrMatter(ground.GetFloorIntX(), static_cast<int>(top) - 1) == g_MaterialAir) {
		top -= 1.0F;
	}
	// Drawn as a bolt of light (LightingSettings::LightningBolts), or as before as a line of particles. The particle path is still worked out either way,
	// so the simulation's random numbers are used the same whichever way it's drawn.
	bool drawnAsLight = g_PostProcessMan.GetLightingSettings().LightningBolts;
	auto boltDot = [drawnAsLight](const Vector& at) {
		if (drawnAsLight) {
			return;
		}
		if (const Entity* preset = g_PresetMan.GetEntityPreset("MOPixel", "Lightning Bolt Particle", "Base.rte")) {
			if (MovableObject* spark = dynamic_cast<MovableObject*>(preset->Clone())) {
				spark->SetPos(at);
				g_MovableMan.AddParticle(spark);
			}
		}
	};
	// The bolt: a few jagged segments, with a short side branch.
	Vector from(ground.m_X + (random01() - 0.5F) * 60.0F, top);
	if (drawnAsLight) {
		// Its shape from where and when it struck, not from the simulation's random numbers.
		unsigned int seed = static_cast<unsigned int>(ground.GetFloorIntX()) * 73856093u ^ static_cast<unsigned int>(ground.GetFloorIntY()) * 19349663u ^ static_cast<unsigned int>(g_TimerMan.GetSimUpdateCount()) * 83492791u;
		g_PostProcessMan.RegisterLightningBolt(from, ground, seed);
	}
	constexpr int segments = 9;
	for (int segment = 1; segment <= segments; ++segment) {
		float t = static_cast<float>(segment) / static_cast<float>(segments);
		Vector to = segment == segments ? ground : Vector(Lerp(0.0F, 1.0F, from.m_X, ground.m_X, t) + (random01() - 0.5F) * 26.0F, Lerp(0.0F, 1.0F, top, ground.m_Y, t));
		Vector step = to - from;
		int dots = std::max(1, static_cast<int>(step.GetMagnitude() / 2.0F));
		for (int dot = 0; dot < dots; ++dot) {
			boltDot(from + step * (static_cast<float>(dot) / static_cast<float>(dots)));
		}
		if (segment == segments / 2) {
			Vector branch = to;
			for (int dot = 0; dot < 14; ++dot) {
				branch += Vector(random01() < 0.5F ? -2.0F : 2.0F, 2.0F);
				boltDot(branch);
			}
		}
		from = to;
	}
	if (SceneLighting* lighting = g_PostProcessMan.GetSceneLighting()) {
		lighting->TriggerLightning();
	}
	EffectsParticles::SpawnExplosion(ground, 900.0F);
	TerrainFire::QueueIgniteArea(ground, 12.0F);
	TerrainFire::QueueIgniteArea(ground, 6.0F);
	if (harmUnits) {
		// Units within 30 px are struck: up to six charges driven down into the body, fewer the further off, so the blow lands as shots
		// do, through wounds, armour and the unit's own scripts, and can take a limb. (It took up to 80 health off directly, which went
		// round all of that and killed a 100-health unit outright within 6 px, never dismembering.)
		const Entity* chargePreset = g_PresetMan.GetEntityPreset("MOPixel", "Lightning Strike Charge", "Base.rte");
		for (Actor* actor: g_MovableMan.GetActorList()) {
			float distance = g_SceneMan.ShortestDistance(ground, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetMagnitude();
			if (distance >= 30.0F) {
				continue;
			}
			int charges = static_cast<int>(std::ceil(6.0F * (1.0F - distance / 30.0F)));
			float above = std::max(actor->GetRadius(), 12.0F);
			for (int charge = 0; charge < charges; ++charge) {
				if (MovableObject* strike = chargePreset ? dynamic_cast<MovableObject*>(chargePreset->Clone()) : nullptr) {
					strike->SetPos(actor->GetPos() + Vector((random01() - 0.5F) * 8.0F, -above));
					strike->SetVel(Vector((random01() - 0.5F) * 6.0F, 80.0F));
					strike->SetTeam(Activity::NoTeam);
					g_MovableMan.AddParticle(strike);
				}
			}
		}
	}
	if (!s_Thunder) {
		if (const Entity* preset = g_PresetMan.GetEntityPreset("SoundContainer", "Explosion Large", "Base.rte")) {
			s_Thunder = dynamic_cast<SoundContainer*>(preset->Clone());
		}
	}
	if (s_Thunder) {
		s_Thunder->Play(ground);
	}
}

void WeatherLightning::Update() {
	if (g_SceneMan.GetScene() != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		s_Scene = g_SceneMan.GetScene();
		s_SceneGeneration = g_SceneMan.GetSceneGeneration();
		s_NextStrikeUpdate = -1;
	}
	const Activity* activity = g_ActivityMan.GetActivity();
	float rain = WeatherEffects::GetRain();
	// Only in a game being played (not an editor), and only in a storm; the sky's own flashes go on as before either way.
	if (!s_Scene || !activity || activity->GetActivityState() != Activity::Running || activity->IsPaused() || s_Strikes == Strikes::SkyOnly || rain <= 0.5F) {
		s_NextStrikeUpdate = -1;
		return;
	}
	long long now = g_TimerMan.GetSimUpdateCount();
	// When and where from the update count and the scene, so a replay strikes in the same places at the same times.
	StormRandom random{Mix(static_cast<unsigned long long>(now) * 0x2545F4914F6CDD1Dull ^ static_cast<unsigned long long>(s_SceneGeneration))};
	if (s_NextStrikeUpdate < 0) {
		s_NextStrikeUpdate = now + UpdatesToNextStrike(random, rain);
		return;
	}
	if (now < s_NextStrikeUpdate) {
		return;
	}
	s_NextStrikeUpdate = now + UpdatesToNextStrike(random, rain);

	// Half the strikes land near someone, so storms are seen and felt where the fighting is; the rest anywhere along the scene.
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	float x = random() * sceneWidth;
	const std::deque<Actor*>& actors = g_MovableMan.GetActorList();
	if (!actors.empty() && random() < 0.5F) {
		size_t pick = std::min(static_cast<size_t>(random() * static_cast<float>(actors.size())), actors.size() - 1);
		x = actors[pick]->GetPos().m_X + (random() - 0.5F) * 800.0F;
	}
	if (g_SceneMan.SceneWrapsX()) {
		x = std::fmod(std::fmod(x, sceneWidth) + sceneWidth, sceneWidth);
	} else {
		x = std::clamp(x, 0.0F, sceneWidth - 1.0F);
	}
	Strike(Vector(x, 0.0F), random, s_Strikes == Strikes::FiresAndUnits);
}
