#include "EffectsParticles.h"
#include "ActorFire.h"
#include "ADoor.h"
#include "Actor.h"
#include "Material.h"
#include "MovableMan.h"
#include "PostProcessMan.h"
#include "PresetMan.h"
#include "SceneMan.h"
#include "TerrainFire.h"
#include "TimerMan.h"
#include "WeatherEffects.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace RTE;

bool ActorFire::s_Enabled = true;

namespace {
	constexpr int c_TickInterval = 3; //!< Sim updates per fire tick, like the terrain fire.
	constexpr const char* c_OnFireTag = "OnFire"; //!< Number value on burning units, so other code (the sandbox's attack orders) leaves them to panic.

	struct Burner {
		MovableObject* Object;
		long ID; //!< Unique ID, to tell the object apart from a new one reusing its memory.
		int TicksLeft;
		int PanicTicks;
	};

	std::vector<Burner> s_Burners; //!< In the order they caught fire, so updates are deterministic.
	std::vector<MovableObject*> s_IgniteQueue;
	std::vector<MovableObject*> s_DouseQueue;
	std::mutex s_QueueMutex;
	const void* s_Scene = nullptr;
	unsigned int s_Random = 0x2F1E3D5Bu;
	int s_LavaMaterial = -1;
	int s_WaterMaterial = -1;

	float Random01() {
		s_Random ^= s_Random << 13;
		s_Random ^= s_Random >> 17;
		s_Random ^= s_Random << 5;
		return static_cast<float>(s_Random & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	MovableObject* CreateEffect(const char* className, const char* presetName) {
		const Entity* preset = g_PresetMan.GetEntityPreset(className, presetName, "Base.rte");
		return preset ? dynamic_cast<MovableObject*>(preset->Clone()) : nullptr;
	}

	/// Flesh burns; machines don't. A unit burns if its body is made of flesh and nothing says it's metal: robots, droids, drones, turrets, craft and brains in jars never catch fire.
	bool MadeOfFlesh(const Actor* actor) {
		if (actor->GetMetalness() >= 0.2F) {
			return false;
		}
		const Material* material = actor->GetMaterial();
		return material && material->GetPresetName().find("Flesh") != std::string::npos;
	}

	bool CanBurn(const Actor* actor) {
		return actor && !actor->IsDead() && !dynamic_cast<const ADoor*>(actor) && actor->GetHealth() > 0.0F && MadeOfFlesh(actor);
	}

	/// Whether something hitting a unit is the kind of fire that sets people alight: burning fuel and flame-thrower flames.
	/// Not the puff of an explosion, a jetpack's flame, smoke, a muzzle flash or a laser: those are named "fire" and "flame" too, and used to set people alight at a touch.
	bool SetsUnitsAlight(const MovableObject* hitter) {
		static std::unordered_map<std::string, bool> cache;
		static std::mutex cacheMutex;
		// A fast, sharp thing is a shot (a bullet, a laser pulse), whatever it's called.
		if (hitter->GetSharpness() > 5.0F) {
			return false;
		}
		const std::string& name = hitter->GetPresetName();
		std::scoped_lock lock(cacheMutex);
		auto found = cache.find(name);
		if (found != cache.end()) {
			return found->second;
		}
		auto has = [&name](const char* part) { return name.find(part) != std::string::npos; };
		bool flame = has("Napalm") || has("Incendi") || has("Flamer") || (has("Flame") && has("Hurt")) || has("Burn Particle") || has("Ground Flame");
		bool harmless = has("Smoke") || has("Puff") || has("Laser") || has("Jet") || has("Sweetener") || has("Muzzle") || has("Body Flame");
		bool result = flame && !harmless;
		cache.emplace(name, result);
		return result;
	}

	std::vector<Burner>::iterator FindBurner(const MovableObject* object) {
		return std::find_if(s_Burners.begin(), s_Burners.end(), [object](const Burner& burner) { return burner.Object == object; });
	}

	/// Sends a burning unit running in a random direction.
	void Panic(Actor* actor) {
		if (actor->IsPlayerControlled()) {
			return;
		}
		float distance = 80.0F + Random01() * 100.0F;
		actor->ClearAIWaypoints();
		actor->AddAISceneWaypoint(actor->GetPos() + Vector(Random01() < 0.5F ? -distance : distance, 0.0F));
		actor->SetAIMode(Actor::AIMODE_GOTO);
	}

	void Ignite(MovableObject* object) {
		if (!object || !g_MovableMan.ValidMO(object)) {
			return;
		}
		Actor* actor = dynamic_cast<Actor*>(object);
		if (!actor) {
			// Not a unit: something with its own idea of burning, like a fuel barrel.
			if (object->NumberValueExists("Flammable")) {
				object->SetNumberValue("Ignited", 1.0);
			}
			return;
		}
		if (!CanBurn(actor)) {
			return;
		}
		if (auto existing = FindBurner(actor); existing != s_Burners.end()) {
			// Topped up by more fire.
			existing->TicksLeft = std::max(existing->TicksLeft, 60);
			return;
		}
		s_Burners.push_back({actor, actor->GetUniqueID(), 100 + static_cast<int>(Random01() * 80.0F), 0});
		actor->SetNumberValue(c_OnFireTag, 1.0);
	}

	void PutOut(std::vector<Burner>::iterator burner, bool withSteam) {
		if (g_MovableMan.ValidMO(burner->Object) && burner->Object->GetUniqueID() == burner->ID) {
			burner->Object->RemoveNumberValue(c_OnFireTag);
			if (Actor* actor = dynamic_cast<Actor*>(burner->Object); actor && !actor->IsPlayerControlled() && actor->GetAIMode() == Actor::AIMODE_GOTO && actor->GetNumberValue("SandboxAttack") <= 0.0) {
				// Done panicking.
				actor->ClearAIWaypoints();
				actor->SetAIMode(Actor::AIMODE_SENTRY);
			}
			if (withSteam) {
				TerrainFire::SpawnSteam(burner->Object->GetPos(), 2);
			}
		}
		s_Burners.erase(burner);
	}

	int MaterialAt(const Vector& position) { return g_SceneMan.GetTerrMatter(position.GetFloorIntX(), position.GetFloorIntY()); }
} // namespace

void ActorFire::OnHit(const MovableObject* hitter, MovableObject* hitRoot, const Material* hitterMaterial) {
	if (!s_Enabled || !hitter || !hitRoot || hitRoot == hitter) {
		return;
	}
	bool fire = SetsUnitsAlight(hitter);
	bool water = !fire && TerrainFire::IsDousingParticle(hitter, hitterMaterial);
	if (!fire && !water) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	std::vector<MovableObject*>& queue = fire ? s_IgniteQueue : s_DouseQueue;
	if (queue.size() < 512) {
		queue.push_back(hitRoot);
	}
}

void ActorFire::Update() {
	if (g_SceneMan.GetScene() != s_Scene) {
		Clear();
		s_Scene = g_SceneMan.GetScene();
		s_Random = 0x2F1E3D5Bu;
		s_LavaMaterial = -1;
		s_WaterMaterial = -1;
		if (s_Scene) {
			const Material* lava = g_SceneMan.GetMaterial("Lava");
			const Material* water = g_SceneMan.GetMaterial("Water");
			s_LavaMaterial = lava && lava->GetIndex() != g_MaterialAir ? lava->GetIndex() : -1;
			s_WaterMaterial = water && water->GetIndex() != g_MaterialAir ? water->GetIndex() : -1;
		}
	}
	std::vector<MovableObject*> ignitions;
	std::vector<MovableObject*> douses;
	{
		std::scoped_lock lock(s_QueueMutex);
		ignitions.swap(s_IgniteQueue);
		douses.swap(s_DouseQueue);
	}
	if (!s_Enabled || !s_Scene) {
		s_Burners.clear();
		return;
	}

	// Lights every update, so they don't flicker between fire ticks.
	for (const Burner& burner: s_Burners) {
		if (g_MovableMan.ValidMO(burner.Object) && burner.Object->GetUniqueID() == burner.ID) {
			g_PostProcessMan.AddLight(burner.Object->GetPos(), 70.0F, 255.0F, 140.0F, 50.0F, 0.9F);
		}
	}
	if (g_TimerMan.GetSimUpdateCount() % c_TickInterval != 0 && ignitions.empty() && douses.empty()) {
		return;
	}

	// Collision reports arrive in whatever order the collision code ran; apply them in a fixed one.
	auto byID = [](const MovableObject* a, const MovableObject* b) { return a->GetUniqueID() < b->GetUniqueID(); };
	auto keepValid = [](std::vector<MovableObject*>& list) {
		list.erase(std::remove_if(list.begin(), list.end(), [](MovableObject* object) { return !g_MovableMan.ValidMO(object); }), list.end());
	};
	keepValid(ignitions);
	keepValid(douses);
	std::sort(ignitions.begin(), ignitions.end(), byID);
	ignitions.erase(std::unique(ignitions.begin(), ignitions.end()), ignitions.end());
	std::sort(douses.begin(), douses.end(), byID);
	douses.erase(std::unique(douses.begin(), douses.end()), douses.end());
	for (MovableObject* object: ignitions) {
		// A lick of flame doesn't always take: about one update in five of being in the flames does. A second in a flame-thrower's stream still sets anyone alight.
		// Things that aren't units (fuel barrels) go up at once, as before.
		if (!dynamic_cast<Actor*>(object) || Random01() < 0.2F) {
			Ignite(object);
		}
	}
	for (MovableObject* object: douses) {
		if (auto burner = FindBurner(object); burner != s_Burners.end()) {
			PutOut(burner, true);
		}
	}
	if (g_TimerMan.GetSimUpdateCount() % c_TickInterval != 0) {
		return;
	}

	// Burning ground and lava set units standing in them alight.
	for (Actor* actor: g_MovableMan.m_Actors) {
		if (!CanBurn(actor) || FindBurner(actor) != s_Burners.end()) {
			continue;
		}
		Vector feet = actor->GetPos() + Vector(0.0F, actor->GetRadius() * 0.6F);
		bool lava = s_LavaMaterial > 0 && (MaterialAt(feet) == s_LavaMaterial || MaterialAt(feet + Vector(0.0F, 3.0F)) == s_LavaMaterial);
		// Standing in burning ground: on average about a second before it takes (it was a third of that).
		if (lava || (TerrainFire::IsBurningNear(feet, 4) && Random01() < 0.05F)) {
			Ignite(actor);
		}
	}

	float rain = WeatherEffects::GetRain();
	float snow = WeatherEffects::GetSnow();
	std::vector<Actor*> caught; //!< Set alight by a burning unit, after the loop (igniting adds to the list being walked).
	for (size_t i = 0; i < s_Burners.size();) {
		Burner& burner = s_Burners[i];
		Actor* actor = g_MovableMan.ValidMO(burner.Object) && burner.Object->GetUniqueID() == burner.ID ? dynamic_cast<Actor*>(burner.Object) : nullptr;
		if (!actor || !CanBurn(actor)) {
			s_Burners.erase(s_Burners.begin() + static_cast<long>(i));
			continue;
		}
		const Vector& position = actor->GetPos();
		// Water puts it out; rain and snow make it burn out sooner.
		if (s_WaterMaterial > 0 && (MaterialAt(position) == s_WaterMaterial || MaterialAt(position + Vector(0.0F, actor->GetRadius() * 0.5F)) == s_WaterMaterial)) {
			PutOut(s_Burners.begin() + static_cast<long>(i), true);
			continue;
		}
		if (Random01() < 0.3F * rain + 0.15F * snow) {
			--burner.TicksLeft;
		}
		if (--burner.TicksLeft <= 0) {
			PutOut(s_Burners.begin() + static_cast<long>(i), false);
			continue;
		}

		actor->SetHealth(actor->GetHealth() - 0.25F);
		float radius = actor->GetRadius();
		// Flames licking off the body.
		if (MovableObject* flame = CreateEffect("MOSParticle", "Body Flame")) {
			flame->SetPos(position + Vector((Random01() - 0.5F) * radius, (Random01() - 0.6F) * radius));
			flame->SetVel(actor->GetVel() * 0.5F + Vector((Random01() - 0.5F) * 1.0F, -1.0F - Random01()));
			g_MovableMan.AddParticle(flame);
		}
		// And soft smoke winding up off it (visual only).
		if (g_PostProcessMan.GetLightingSettings().SoftSmoke > 0.0F) {
			float wind = static_cast<float>(g_TimerMan.GetSimUpdateCount()) * 0.3F;
			EffectsParticles::Emit("Smoke", position + Vector(std::sin(wind) * 4.0F, -radius * 0.6F), Vector(std::cos(wind) * 1.5F, -1.8F), 0.35F, 1, 0);
		}
		// Running burning units set the grass alight, and anyone they bump into.
		if (Random01() < 0.3F) {
			Vector feet = position + Vector((Random01() - 0.5F) * radius, radius * 0.7F);
			TerrainFire::QueueIgnite(feet.GetFloorIntX(), feet.GetFloorIntY());
		}
		if (Random01() < 0.015F) {
			for (Actor* other: g_MovableMan.m_Actors) {
				if (other != actor && CanBurn(other) && g_SceneMan.ShortestDistance(position, other->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(14.0F)) {
					caught.push_back(other);
					break;
				}
			}
		}
		if (burner.PanicTicks-- <= 0) {
			burner.PanicTicks = 40;
			Panic(actor);
		}
		++i;
	}
	for (Actor* actor: caught) {
		Ignite(actor);
	}
}

bool ActorFire::IsBurning(const MovableObject* object) {
	return FindBurner(object) != s_Burners.end();
}

int ActorFire::GetCount() {
	return static_cast<int>(s_Burners.size());
}

void ActorFire::Clear() {
	s_Burners.clear();
	std::scoped_lock lock(s_QueueMutex);
	s_IgniteQueue.clear();
	s_DouseQueue.clear();
}
