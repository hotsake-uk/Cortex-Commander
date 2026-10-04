#include "ActorWater.h"
#include "ACraft.h"
#include "ADoor.h"
#include "Actor.h"
#include "FluidSim.h"
#include "Material.h"
#include "MovableMan.h"
#include "SceneMan.h"
#include "TimerMan.h"
#include "PostProcessMan.h"
#include "WeatherEffects.h"

#include <algorithm>
#include <string>

using namespace RTE;

bool ActorWater::s_Enabled = true;

namespace {
	constexpr const char* c_DepthTag = "LiquidDepth"; //!< Number value on units in liquid: 1 feet in, 2 body in, 3 head under.
	constexpr const char* c_AirTag = "AirLeft"; //!< Number value on units holding their breath, seconds of air left.
	constexpr float c_AirSeconds = 12.0F; //!< How long a unit can hold its breath.

	const void* s_Scene = nullptr;
	int s_WaterMaterial = -1;
	int s_AcidMaterial = -1;

	int MaterialAt(const Vector& position) { return g_SceneMan.GetTerrMatter(position.GetFloorIntX(), position.GetFloorIntY()); }

	bool InLiquid(const Vector& position) {
		int material = MaterialAt(position);
		return material == s_WaterMaterial || material == s_AcidMaterial;
	}

	/// Whether there's nothing solid above a point for a good way up, so rain and snow reach it.
	bool UnderOpenSky(const Vector& position) {
		for (float up = 24.0F; up <= 240.0F; up += 24.0F) {
			if (position.m_Y - up < 0.0F) {
				return true;
			}
			if (MaterialAt(position - Vector(0.0F, up)) != g_MaterialAir) {
				return false;
			}
		}
		return true;
	}

	/// How units look from what the world has done to them: wet from wading and rain, snowed on when they stand still in snowfall, sooty and hot while burning. Looks only.
	void UpdateSurfaceStates() {
		if (!g_PostProcessMan.GetLightingSettings().SurfaceStates) {
			return;
		}
		float deltaTime = g_TimerMan.GetDeltaTimeSecs();
		float rain = WeatherEffects::GetRain();
		float snow = WeatherEffects::GetSnow();
		for (Actor* actor: g_MovableMan.GetActorList()) {
			float wetness = actor->GetWetness();
			float soot = actor->GetSoot();
			float snowCover = actor->GetSnowCover();
			bool inLiquid = actor->NumberValueExists(c_DepthTag);
			bool weather = (rain > 0.0F || snow > 0.0F) && UnderOpenSky(actor->GetPos());
			if (inLiquid) {
				// Soaked at once, and the water takes the soot and snow with it.
				wetness = 1.0F;
				soot = std::max(soot - deltaTime * 0.5F, 0.0F);
				snowCover = 0.0F;
			} else if (weather && rain > 0.0F) {
				wetness = std::min(wetness + deltaTime * 0.12F * rain, 0.4F + 0.5F * rain);
				soot = std::max(soot - deltaTime * 0.03F * rain, 0.0F);
			} else {
				// Dry in about twenty seconds.
				wetness = std::max(wetness - deltaTime * 0.05F, 0.0F);
			}
			if (weather && snow > 0.0F && !inLiquid && actor->GetVel().MagnitudeIsLessThan(1.5F)) {
				// Snow settles on whoever stands still: fully covered in about twenty seconds of heavy snow.
				snowCover = std::min(snowCover + deltaTime * 0.05F * snow, 1.0F);
			} else if (snowCover > 0.0F) {
				// Moving shakes it off; out of the snowfall it melts.
				snowCover = std::max(snowCover - deltaTime * (actor->GetVel().MagnitudeIsGreaterThan(3.0F) ? 0.5F : 0.04F), 0.0F);
			}
			if (actor->NumberValueExists("OnFire")) {
				// Burning: blackening as it goes, and glowing.
				soot = std::min(soot + deltaTime * 0.25F, 1.0F);
				actor->AddHeat(deltaTime * 1.2F);
				wetness = 0.0F;
				snowCover = 0.0F;
			} else {
				// Soot wears off over a couple of minutes.
				soot = std::max(soot - deltaTime * 0.008F, 0.0F);
			}
			actor->SetWetness(wetness);
			actor->SetSoot(soot);
			actor->SetSnowCover(snowCover);
		}
	}

	/// Flesh and blood units breathe; robots, drones and brains in jars don't.
	bool Breathes(const Actor* actor) {
		const Material* material = actor->GetMaterial();
		return material && material->GetPresetName().find("Flesh") != std::string::npos;
	}
} // namespace

void ActorWater::Update() {
	if (g_SceneMan.GetScene() != s_Scene) {
		s_Scene = g_SceneMan.GetScene();
		s_WaterMaterial = -1;
		s_AcidMaterial = -1;
		if (s_Scene) {
			const Material* water = g_SceneMan.GetMaterial("Water");
			const Material* acid = g_SceneMan.GetMaterial("Acid");
			s_WaterMaterial = water && water->GetIndex() != g_MaterialAir ? water->GetIndex() : -1;
			s_AcidMaterial = acid && acid->GetIndex() != g_MaterialAir ? acid->GetIndex() : -1;
		}
	}
	if (s_Scene) {
		UpdateSurfaceStates();
	}
	if (!s_Enabled || !FluidSim::IsEnabled() || !s_Scene || (s_WaterMaterial < 0 && s_AcidMaterial < 0)) {
		return;
	}
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	float gravity = g_SceneMan.GetGlobalAcc().m_Y;
	for (Actor* actor: g_MovableMan.m_Actors) {
		if (actor->IsDead() || dynamic_cast<ADoor*>(actor) || dynamic_cast<ACraft*>(actor)) {
			continue;
		}
		const Vector& position = actor->GetPos();
		float reach = actor->GetRadius() * 0.55F;
		Vector feet = position + Vector(0.0F, reach);
		Vector head = position - Vector(0.0F, reach);
		int depth = InLiquid(head) ? 3 : (InLiquid(position) ? 2 : (InLiquid(feet) || InLiquid(feet + Vector(0.0F, 3.0F)) ? 1 : 0));
		if (depth == 0) {
			if (actor->NumberValueExists(c_DepthTag)) {
				actor->RemoveNumberValue(c_DepthTag);
			}
			// Catching its breath: air comes back three times as fast as it went.
			if (actor->NumberValueExists(c_AirTag)) {
				float air = static_cast<float>(actor->GetNumberValue(c_AirTag)) + deltaTime * 3.0F;
				if (air >= c_AirSeconds) {
					actor->RemoveNumberValue(c_AirTag);
				} else {
					actor->SetNumberValue(c_AirTag, air);
				}
			}
			continue;
		}
		actor->SetNumberValue(c_DepthTag, static_cast<double>(depth));

		if (depth >= 2) {
			// The liquid drags, and pushes up: light units bob to the top, heavy ones sink slowly.
			Vector velocity = actor->GetVel();
			velocity *= std::max(1.0F - 2.2F * deltaTime, 0.0F);
			// A lightly loaded soldier is about 145 kg all in and just floats; heavy armour and big guns sink.
			float buoyancy = std::clamp(1.7F - (actor->GetMass() - 110.0F) / 70.0F, 0.3F, 1.7F);
			velocity.m_Y -= gravity * buoyancy * deltaTime * (depth == 3 ? 1.0F : 0.6F);
			actor->SetVel(velocity);
		}

		bool acid = s_AcidMaterial >= 0 && (MaterialAt(feet) == s_AcidMaterial || MaterialAt(position) == s_AcidMaterial || MaterialAt(feet + Vector(0.0F, 3.0F)) == s_AcidMaterial);
		if (acid) {
			actor->SetHealth(actor->GetHealth() - 5.0F * static_cast<float>(depth) * deltaTime);
		}

		if (Breathes(actor)) {
			float air = actor->NumberValueExists(c_AirTag) ? static_cast<float>(actor->GetNumberValue(c_AirTag)) : c_AirSeconds;
			if (depth == 3) {
				air = std::max(air - deltaTime, 0.0F);
				actor->SetNumberValue(c_AirTag, air);
				if (air <= 0.0F) {
					actor->SetHealth(actor->GetHealth() - 12.0F * deltaTime);
				}
			} else if (air < c_AirSeconds) {
				air += deltaTime * 3.0F;
				if (air >= c_AirSeconds) {
					actor->RemoveNumberValue(c_AirTag);
				} else {
					actor->SetNumberValue(c_AirTag, air);
				}
			}
		}
	}
}

int ActorWater::GetDepth(const Actor* actor) {
	return actor && actor->NumberValueExists(c_DepthTag) ? static_cast<int>(actor->GetNumberValue(c_DepthTag)) : 0;
}

float ActorWater::GetWalkSpeedMultiplier(const Actor* actor) {
	static constexpr float multipliers[4] = {1.0F, 0.8F, 0.55F, 0.45F};
	return multipliers[std::clamp(GetDepth(actor), 0, 3)];
}

float ActorWater::GetAir(const Actor* actor) {
	return actor && actor->NumberValueExists(c_AirTag) ? std::clamp(static_cast<float>(actor->GetNumberValue(c_AirTag)) / c_AirSeconds, 0.0F, 1.0F) : 1.0F;
}
