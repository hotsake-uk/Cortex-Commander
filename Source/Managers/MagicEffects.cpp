#include "MagicEffects.h"
#include "ADoor.h"
#include "Actor.h"
#include "AirPressure.h"
#include "Constants.h"
#include "MovableMan.h"
#include "MovableObject.h"
#include "SceneMan.h"
#include "Vector.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

using namespace RTE;

bool MagicEffects::s_On = true;
MagicEffects::Tuning MagicEffects::s_Tuning;

namespace {
	constexpr float c_MassResistance = 0.012F; //!< How much each kilogram resists a push: a 70 kg soldier gets about half what a pebble does.
	constexpr float c_BlockingIntegrity = 30.0F; //!< Ground this strong or stronger between the origin and a unit or item shields it (sand, earth, stone, metal; not grass or leaves).
	constexpr float c_KnocksOver = 8.0F; //!< A shove at least this fast, in metres a second, knocks a standing unit off its feet.
	constexpr float c_GustPerPower = 0.25F; //!< How hard the air blows for each metre a second of push.

	struct PushRequest {
		Vector Origin;
		Vector Direction;
		float Power;
		float Range;
		float Spread;
		long CasterID; //!< The caster's root's unique ID (MOIDs change from frame to frame), 0 for none.
	};
	std::mutex s_QueueMutex;
	std::vector<PushRequest> s_Pushes;
} // namespace

void MagicEffects::Push(const Vector& origin, const Vector& direction, float power, float range, float spread, const MovableObject* caster) {
	if (!s_On || !std::isfinite(power) || power == 0.0F || !std::isfinite(range) || range <= 0.0F || !std::isfinite(spread)) {
		return;
	}
	const MovableObject* casterRoot = caster ? caster->GetRootParent() : nullptr;
	std::scoped_lock lock(s_QueueMutex);
	if (s_Pushes.size() < 256) {
		s_Pushes.push_back({origin, direction, std::clamp(power, -200.0F, 200.0F), std::min(range, 2000.0F), spread, casterRoot ? casterRoot->GetUniqueID() : 0});
	}
}

void MagicEffects::Update() {
	std::vector<PushRequest> pushes;
	{
		std::scoped_lock lock(s_QueueMutex);
		pushes.swap(s_Pushes);
	}
	if (!s_On) {
		return;
	}
	// (Casts made from parallel scripts are sorted so they land in the same order every time.)
	std::sort(pushes.begin(), pushes.end(), [](const PushRequest& a, const PushRequest& b) {
		return a.Origin.m_Y != b.Origin.m_Y ? a.Origin.m_Y < b.Origin.m_Y : (a.Origin.m_X != b.Origin.m_X ? a.Origin.m_X < b.Origin.m_X : (a.Power != b.Power ? a.Power < b.Power : a.CasterID < b.CasterID));
	});
	for (const PushRequest& push: pushes) {
		ApplyPush(push.Origin, push.Direction, push.Power, push.Range, push.Spread, push.CasterID);
	}
}

void MagicEffects::ApplyPush(const Vector& origin, const Vector& direction, float power, float range, float spread, long casterID) {
	spread = std::clamp(spread, 0.0F, 360.0F);
	bool allRound = spread >= 360.0F || direction.IsZero();
	Vector aim = allRound ? Vector() : direction.GetNormalized();
	float cosHalfSpread = std::cos(spread * 0.5F * c_PI / 180.0F);
	float strength = power * std::max(s_Tuning.PushStrength, 0.0F);

	if (s_Tuning.AirGust > 0.0F) {
		// A pull draws the air in: the gust blows back toward the origin from the far end of the cone.
		if (power > 0.0F) {
			AirPressure::Gust(origin, allRound ? Vector(1.0F, 0.0F) : aim, std::abs(power) * c_GustPerPower * s_Tuning.AirGust, range, allRound ? 360.0F : spread);
		} else if (!allRound) {
			AirPressure::Gust(origin + aim * range, -aim, std::abs(power) * c_GustPerPower * s_Tuning.AirGust, range, std::min(spread, 90.0F));
		}
	}

	auto shove = [&](MovableObject* object, float share, bool checkGround) {
		if (!object || object->ToDelete() || object->GetPinStrength() > 0.0F) {
			return;
		}
		if (casterID != 0 && object->GetUniqueID() == casterID) {
			return;
		}
		Vector offset = g_SceneMan.ShortestDistance(origin, object->GetPos(), true);
		float distance = offset.GetMagnitude();
		if (distance > range) {
			return;
		}
		Vector away = distance > 0.5F ? offset / distance : (allRound ? Vector(0.0F, -1.0F) : aim);
		if (!allRound && distance > 0.5F && away.Dot(aim) < cosHalfSpread) {
			return;
		}
		if (checkGround && distance > 2.0F && g_SceneMan.CastMaxStrengthRay(origin, object->GetPos(), 2) >= c_BlockingIntegrity) {
			return;
		}
		// Full strength in the nearest third of the range, falling to a quarter at its edge.
		float falloff = distance <= range / 3.0F ? 1.0F : 1.0F - 0.75F * (distance - range / 3.0F) / (range * 2.0F / 3.0F);
		float speed = strength * share * falloff / (1.0F + std::max(object->GetMass(), 0.0F) * c_MassResistance);
		if (speed == 0.0F) {
			return;
		}
		// A pull never throws a thing on past the origin: no faster than gets it there in about a third of a second.
		if (speed < 0.0F) {
			speed = std::max(speed, -distance / c_PPM * 3.0F);
		}
		object->SetVel(object->GetVel() + away * speed);
		object->NotResting();
		if (Actor* actor = dynamic_cast<Actor*>(object); actor && std::abs(speed) >= c_KnocksOver && actor->GetStatus() == Actor::STABLE) {
			actor->SetStatus(Actor::UNSTABLE);
		}
	};

	if (s_Tuning.UnitPush > 0.0F) {
		for (Actor* actor: g_MovableMan.m_Actors) {
			if (!dynamic_cast<const ADoor*>(actor)) {
				shove(actor, s_Tuning.UnitPush, true);
			}
		}
	}
	for (MovableObject* item: g_MovableMan.m_Items) {
		shove(item, 1.0F, true);
	}
	for (MovableObject* particle: g_MovableMan.m_Particles) {
		shove(particle, 1.0F, false);
	}
}
