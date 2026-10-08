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
#include <array>
#include <cfloat>
#include <string>

using namespace RTE;

bool ActorWater::s_Enabled = true;

namespace {
	constexpr const char* c_DepthTag = "LiquidDepth"; //!< Number value on units in liquid: 1 feet in, 2 body in, 3 head under.
	constexpr const char* c_AirTag = "AirLeft"; //!< Number value on units holding their breath, seconds of air left.
	constexpr const char* c_StickTag = "LiquidStick"; //!< Number value on units in a sticky liquid (tar, mud): its stickiness, 0 to 1.
	constexpr float c_AirSeconds = 12.0F; //!< How long a unit can hold its breath.

	const void* s_Scene = nullptr;
	// Per material (SB-1): whether it holds bodies (FluidSim::HoldsBodies), whether they can breathe in it, and the health a second it takes
	// for each level of depth (MaterialBehaviour::TouchDamage; stock, acid's 5).
	std::array<bool, 256> s_HoldsBodies{};
	std::array<bool, 256> s_Breathable{};
	std::array<float, 256> s_TouchDamage{};
	std::array<float, 256> s_Stickiness{}; //!< How much a liquid holds a body back (Material::GetStickiness: tar 0.9, mud 0.4).
	std::array<float, 256> s_Heaviness{}; //!< How hard a liquid pushes a body up against water's push: its density over water's, 1 to 3 (mercury the most).
	std::array<float, 256> s_CutDamage{}; //!< For what isn't a liquid (glass shards): the health a second it takes from a body walking through it.
	std::array<float, 256> s_Chill{}; //!< How cold a liquid is (MaterialBehaviour::Chills; cryogenic fluid): frosts a body over while it is in it.
	bool s_AnyLiquid = false;
	bool s_TablesBuilt = false;

	int MaterialAt(const Vector& position) { return g_SceneMan.GetTerrMatter(position.GetFloorIntX(), position.GetFloorIntY()); }

	bool InLiquid(const Vector& position) { return s_HoldsBodies[static_cast<unsigned char>(MaterialAt(position))]; }

	/// The tables, from FluidSim's liquids (built first, in the liquids' update) and each material's behaviour.
	void BuildTables() {
		s_HoldsBodies.fill(false);
		s_Breathable.fill(false);
		s_TouchDamage.fill(0.0F);
		s_Stickiness.fill(0.0F);
		s_Heaviness.fill(1.0F);
		s_CutDamage.fill(0.0F);
		s_Chill.fill(0.0F);
		s_AnyLiquid = false;
		for (int id = 1; id < 256; ++id) {
			const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
			if (!material || material->GetIndex() != id) {
				continue;
			}
			const MaterialBehaviour& behaviour = material->GetBehaviour();
			if (!FluidSim::HoldsBodies(id)) {
				// Sharp loose stuff underfoot (SB-2's glass shards) cuts whoever walks through it.
				if (behaviour.TouchDamage > 0.0F && !FluidSim::IsLiquid(id)) {
					s_CutDamage[id] = behaviour.TouchDamage;
					s_AnyLiquid = true;
				}
				continue;
			}
			s_Stickiness[id] = std::clamp(material->GetStickiness(), 0.0F, 1.0F);
			// (Only for the really heavy ones, mud and mercury: water and acid, at 1 and 1.2, push as they always did.)
			s_Heaviness[id] = material->GetVolumeDensity() > 1.5F ? std::clamp(material->GetVolumeDensity(), 1.0F, 3.0F) : 1.0F;
			s_HoldsBodies[id] = true;
			s_Chill[id] = behaviour.Chills > 0.0F ? behaviour.Chills : 0.0F;
			s_Breathable[id] = behaviour.Breathable == 1;
			s_TouchDamage[id] = behaviour.TouchDamage >= 0.0F ? behaviour.TouchDamage : (material->GetPresetName() == "Acid" ? 5.0F : 0.0F);
			s_AnyLiquid = true;
		}
		// (Looked at again next update until there is some: the liquids' own tables may not be built yet.)
		s_TablesBuilt = s_AnyLiquid;
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
				// (Unless it is freezing cold, cryogenic fluid: that frosts a body over instead, deeper in it faster, and the frost melts off as snow does once out.)
				float chill = std::max(s_Chill[static_cast<unsigned char>(MaterialAt(actor->GetPos()))], s_Chill[static_cast<unsigned char>(MaterialAt(actor->GetPos() + Vector(0.0F, 12.0F)))]);
				snowCover = chill > 0.0F ? std::min(snowCover + chill * static_cast<float>(actor->GetNumberValue(c_DepthTag)) * 0.5F * deltaTime, 1.0F) : 0.0F;
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
			} else if (snowCover > 0.0F && !inLiquid) {
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
		s_TablesBuilt = false;
	}
	if (s_Scene) {
		UpdateSurfaceStates();
	}
	if (!s_Enabled || !FluidSim::IsEnabled() || !s_Scene) {
		return;
	}
	if (!s_TablesBuilt) {
		BuildTables();
	}
	if (!s_AnyLiquid) {
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
		// Walking through something sharp (glass shards): cut, the more the faster it goes.
		if (float cut = s_CutDamage[static_cast<unsigned char>(MaterialAt(feet + Vector(0.0F, 2.0F)))]; cut > 0.0F && actor->GetVel().MagnitudeIsGreaterThan(0.5F)) {
			actor->SetHealth(actor->GetHealth() - cut * std::min(actor->GetVel().GetMagnitude() * 0.5F, 2.0F) * deltaTime);
		}
		int depth = InLiquid(head) ? 3 : (InLiquid(position) ? 2 : (InLiquid(feet) || InLiquid(feet + Vector(0.0F, 3.0F)) ? 1 : 0));
		// How sticky what it stands or swims in is (tar, mud): it walks and swims slower for it (GetWalkSpeedMultiplier).
		float stickiness = depth == 0 ? 0.0F : std::max(s_Stickiness[static_cast<unsigned char>(MaterialAt(feet))], s_Stickiness[static_cast<unsigned char>(MaterialAt(position))]);
		if (stickiness > 0.0F) {
			actor->SetNumberValue(c_StickTag, static_cast<double>(stickiness));
		} else if (actor->NumberValueExists(c_StickTag)) {
			actor->RemoveNumberValue(c_StickTag);
		}
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
		if (!actor->NumberValueExists(c_DepthTag) && actor->GetVel().GetMagnitude() > 4.0F) {
			// Dropping or running in throws up a splash.
			FluidSim::Splash(feet, actor->GetRadius() * 0.6F + 3.0F, 0.3F, std::min(actor->GetVel().GetMagnitude() * 0.55F, 10.0F));
		}
		actor->SetNumberValue(c_DepthTag, static_cast<double>(depth));

		if (depth >= 2) {
			// The liquid drags, and pushes up: light units bob to the top, heavy ones sink slowly.
			// (A sticky liquid drags harder, and a heavy one pushes harder: a soldier floats high on mercury.)
			Vector velocity = actor->GetVel();
			velocity *= std::max(1.0F - (2.2F + 8.0F * stickiness) * deltaTime, 0.0F);
			float buoyancy = GetBuoyancy(actor) * s_Heaviness[static_cast<unsigned char>(MaterialAt(position))];
			velocity.m_Y -= gravity * buoyancy * deltaTime * (depth == 3 ? 1.0F : 0.6F);
			// Swimming (LM-4): with a move key, a stroke that way, up to the swimming speed; up (or jump) strokes up, down dives. A floater with
			// its head out holds at the surface rather than bobbing, unless it dives. (Lava is ActorFire's: nobody swims in it.)
			const Controller* controller = actor->GetController();
			bool left = controller->IsState(MOVE_LEFT);
			bool right = controller->IsState(MOVE_RIGHT);
			bool up = controller->IsState(MOVE_UP) || controller->IsState(BODY_JUMP);
			bool down = controller->IsState(MOVE_DOWN) || controller->IsState(BODY_CROUCH);
			const float stroke = 6.0F * deltaTime * (1.0F - 0.8F * stickiness); // About a third of a second to the swimming speed (much longer in tar).
			if (left != right) {
				float wanted = right ? c_SwimSpeed : -c_SwimSpeed;
				if (velocity.m_X * (right ? 1.0F : -1.0F) < c_SwimSpeed) {
					velocity.m_X = right ? std::min(velocity.m_X + stroke, wanted) : std::max(velocity.m_X - stroke, wanted);
				}
			}
			if (up && !down && velocity.m_Y > -c_SwimSpeed) {
				velocity.m_Y = std::max(velocity.m_Y - stroke, -c_SwimSpeed);
			} else if (down && !up && velocity.m_Y < c_SwimSpeed) {
				velocity.m_Y = std::min(velocity.m_Y + stroke + gravity * buoyancy * deltaTime * 0.5F, c_SwimSpeed);
			} else if (depth == 2 && buoyancy > 1.0F) {
				velocity.m_Y *= std::max(1.0F - 6.0F * deltaTime, 0.0F);
			}
			actor->SetVel(velocity);
		}

		// What eats at bodies in it (acid): the worst of the liquid at the feet, a little under them and at the middle.
		float touchDamage = std::max({s_TouchDamage[static_cast<unsigned char>(MaterialAt(feet))], s_TouchDamage[static_cast<unsigned char>(MaterialAt(position))], s_TouchDamage[static_cast<unsigned char>(MaterialAt(feet + Vector(0.0F, 3.0F)))]});
		if (touchDamage > 0.0F) {
			actor->SetHealth(actor->GetHealth() - touchDamage * static_cast<float>(depth) * deltaTime);
		}

		// (Not in a liquid a body can breathe in: none of the stock ones.)
		if (Breathes(actor) && !(depth == 3 && s_Breathable[static_cast<unsigned char>(MaterialAt(head))])) {
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
	// And slower again in something sticky: tar holds the legs to a crawl, mud to half.
	float stickiness = actor && actor->NumberValueExists(c_StickTag) ? static_cast<float>(actor->GetNumberValue(c_StickTag)) : 0.0F;
	return multipliers[std::clamp(GetDepth(actor), 0, 3)] * (1.0F - 0.75F * std::clamp(stickiness, 0.0F, 1.0F));
}

float ActorWater::GetBreathSeconds(const Actor* actor) {
	return actor && Breathes(actor) ? c_AirSeconds : FLT_MAX;
}

float ActorWater::GetBuoyancy(const Actor* actor) {
	// A lightly loaded soldier is about 145 kg all in and just floats; heavy armour and big guns sink.
	return actor ? std::clamp(1.7F - (actor->GetMass() - 110.0F) / 70.0F, 0.3F, 1.7F) : 1.0F;
}

float ActorWater::GetAir(const Actor* actor) {
	return actor && actor->NumberValueExists(c_AirTag) ? std::clamp(static_cast<float>(actor->GetNumberValue(c_AirTag)) / c_AirSeconds, 0.0F, 1.0F) : 1.0F;
}
