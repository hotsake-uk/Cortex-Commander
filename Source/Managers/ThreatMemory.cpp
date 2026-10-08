#include "ThreatMemory.h"
#include "ADoor.h"
#include "Activity.h"
#include "Actor.h"
#include "MovableMan.h"
#include "SceneMan.h"
#include "TimerMan.h"

#include <array>
#include <map>

using namespace RTE;

bool ThreatMemory::s_Enabled = true;

namespace {
	constexpr float c_ShareRange = 500.0F; //!< How far from the unit that saw an enemy its teammates hear of it.
	constexpr long long c_ShareEveryUpdates = 60; //!< A team hears of the same enemy at most once a second (at 60 updates a second).
	constexpr long long c_ForgetUpdates = 3600; //!< A memory over a minute old is forgotten.

	struct Memory {
		Vector Pos; //!< Where it was last seen.
		long long SeenUpdate = 0; //!< The sim update it was last seen on.
		long long SharedUpdate = -1000000; //!< The sim update the team was last told of it.
		bool PlayerControlled = false; //!< Whether a player controlled it when it was seen.
	};
	std::array<std::map<long, Memory>, Activity::MaxTeamCount> s_Memories; //!< Per team, by the enemy's unique ID (so in a fixed order).
	const void* s_Scene = nullptr;
	unsigned int s_SceneGeneration = 0;

	float AgeMS(long long seenUpdate) {
		return static_cast<float>(g_TimerMan.GetSimUpdateCount() - seenUpdate) * g_TimerMan.GetDeltaTimeMS();
	}
} // namespace

void ThreatMemory::SetEnabled(bool enabled) {
	if (s_Enabled != enabled) {
		s_Enabled = enabled;
		Clear();
	}
}

void ThreatMemory::Report(const Actor* reporter, const Actor* enemy) {
	if (!s_Enabled || !reporter || !enemy) {
		return;
	}
	int team = reporter->GetTeam();
	// (Not doors: a door is a thing to get through, not an enemy to keep watch for.)
	if (team < 0 || team >= Activity::MaxTeamCount || enemy->GetTeam() == team || dynamic_cast<const ADoor*>(enemy)) {
		return;
	}
	long long now = g_TimerMan.GetSimUpdateCount();
	Memory& memory = s_Memories[team][enemy->GetUniqueID()];
	memory.Pos = enemy->GetPos();
	memory.SeenUpdate = now;
	memory.PlayerControlled = enemy->IsPlayerControlled();
	if (now - memory.SharedUpdate < c_ShareEveryUpdates) {
		return;
	}
	memory.SharedUpdate = now;
	// The others close by hear of it and turn to face it (the alarm the AI scripts already answer). Not units a player controls: they have
	// a player's eyes.
	for (Actor* mate: g_MovableMan.GetActorList()) {
		if (mate == reporter || mate->GetTeam() != team || mate->IsDead() || mate->IsPlayerControlled() || dynamic_cast<const ADoor*>(mate)) {
			continue;
		}
		if (g_SceneMan.ShortestDistance(reporter->GetPos(), mate->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(c_ShareRange)) {
			mate->AlarmPoint(memory.Pos);
		}
	}
}

Vector ThreatMemory::GetNearestRemembered(int team, const Vector& near, float maxAgeMS, float maxDistance) {
	if (!s_Enabled || team < 0 || team >= Activity::MaxTeamCount) {
		return Vector();
	}
	Vector best;
	float bestDistance = maxDistance;
	for (const auto& [id, memory]: s_Memories[team]) {
		if (AgeMS(memory.SeenUpdate) > maxAgeMS) {
			continue;
		}
		float distance = g_SceneMan.ShortestDistance(near, memory.Pos, g_SceneMan.SceneWrapsX()).GetMagnitude();
		if (distance < bestDistance) {
			bestDistance = distance;
			best = memory.Pos;
		}
	}
	return best;
}

Vector ThreatMemory::GetPlayerLastSeen(int team, float maxAgeMS) {
	if (!s_Enabled || team < 0 || team >= Activity::MaxTeamCount) {
		return Vector();
	}
	Vector best;
	long long latest = -1;
	for (const auto& [id, memory]: s_Memories[team]) {
		if (memory.PlayerControlled && memory.SeenUpdate > latest && AgeMS(memory.SeenUpdate) <= maxAgeMS) {
			latest = memory.SeenUpdate;
			best = memory.Pos;
		}
	}
	return best;
}

float ThreatMemory::GetAgeOf(int team, const Actor* enemy) {
	if (!s_Enabled || !enemy || team < 0 || team >= Activity::MaxTeamCount) {
		return -1.0F;
	}
	auto found = s_Memories[team].find(enemy->GetUniqueID());
	return found == s_Memories[team].end() ? -1.0F : AgeMS(found->second.SeenUpdate);
}

void ThreatMemory::Update() {
	if (g_SceneMan.GetScene() != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		Clear();
		s_Scene = g_SceneMan.GetScene();
		s_SceneGeneration = g_SceneMan.GetSceneGeneration();
	}
	long long now = g_TimerMan.GetSimUpdateCount();
	if (!s_Enabled || now % 30 != 0) {
		return;
	}
	for (std::map<long, Memory>& memories: s_Memories) {
		for (auto entry = memories.begin(); entry != memories.end();) {
			const MovableObject* enemy = g_MovableMan.FindObjectByUniqueID(entry->first);
			const Actor* actor = dynamic_cast<const Actor*>(enemy);
			if (!actor || actor->IsDead() || now - entry->second.SeenUpdate > c_ForgetUpdates) {
				entry = memories.erase(entry);
			} else {
				++entry;
			}
		}
	}
}

void ThreatMemory::Clear() {
	for (std::map<long, Memory>& memories: s_Memories) {
		memories.clear();
	}
}
