#include "ThreatMemory.h"
#include "ACShip.h"
#include "ACraft.h"
#include "AVehicle.h"
#include "ADoor.h"
#include "Activity.h"
#include "Actor.h"
#include "MovableMan.h"
#include "SceneMan.h"
#include "TimerMan.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

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

	/// A sighting reported by a unit's AI, waiting for the main thread. (The AI scripts run on worker threads, one per Lua state, so a report
	/// is only queued there; changing the memories or alarming teammates from them crashed the game as soon as two threads reported at once.)
	struct PendingReport {
		long ReporterID; //!< The reporting unit's unique ID.
		long EnemyID; //!< The enemy's unique ID.
		int Team; //!< The reporting unit's team.
		Vector ReporterPos; //!< Where the reporting unit was.
		Vector EnemyPos; //!< Where the enemy was seen.
		bool PlayerControlled; //!< Whether a player controlled the enemy.
		long long Update; //!< The sim update it was seen on.
	};
	/// The queue is split over a few shards, each with its own lock, picked by the reporting thread, so AI threads reporting at once don't
	/// wait on one lock; the main thread takes them all, and sorts them, at Update.
	struct PendingShard {
		std::mutex Mutex;
		std::vector<PendingReport> Reports;
	};
	std::array<PendingShard, 16> s_PendingShards;
	PendingShard& PendingShardForThisThread() {
		static thread_local PendingShard& shard = s_PendingShards[std::hash<std::thread::id>{}(std::this_thread::get_id()) % s_PendingShards.size()];
		return shard;
	}
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
	// Called from the AI scripts, on worker threads: queued, and applied on the main thread by Update.
	PendingShard& shard = PendingShardForThisThread();
	std::scoped_lock lock(shard.Mutex);
	shard.Reports.push_back({reporter->GetUniqueID(), enemy->GetUniqueID(), team, reporter->GetPos(), enemy->GetPos(), enemy->IsPlayerControlled(), g_TimerMan.GetSimUpdateCount()});
}

void ThreatMemory::ApplyReports() {
	std::vector<PendingReport> reports;
	for (PendingShard& shard: s_PendingShards) {
		std::scoped_lock lock(shard.Mutex);
		reports.insert(reports.end(), shard.Reports.begin(), shard.Reports.end());
		shard.Reports.clear();
	}
	if (!s_Enabled || reports.empty()) {
		return;
	}
	// In a fixed order, whichever thread reported first, so the game plays out the same.
	std::sort(reports.begin(), reports.end(), [](const PendingReport& a, const PendingReport& b) {
		return a.Update != b.Update ? a.Update < b.Update : a.ReporterID != b.ReporterID ? a.ReporterID < b.ReporterID : a.EnemyID < b.EnemyID;
	});
	for (const PendingReport& report: reports) {
		Memory& memory = s_Memories[report.Team][report.EnemyID];
		memory.Pos = report.EnemyPos;
		memory.SeenUpdate = report.Update;
		memory.PlayerControlled = report.PlayerControlled;
		if (report.Update - memory.SharedUpdate < c_ShareEveryUpdates) {
			continue;
		}
		memory.SharedUpdate = report.Update;
		// The others close by hear of it and turn to face it (the alarm the AI scripts already answer). Not units a player controls: they
		// have a player's eyes.
		for (Actor* mate: g_MovableMan.GetActorList()) {
			// (Fighting units only: not doors, and not craft, which have no use for an alarm.)
			if (!mate || mate->GetUniqueID() == report.ReporterID || mate->GetTeam() != report.Team || mate->IsDead() || mate->IsPlayerControlled() || dynamic_cast<const ADoor*>(mate) || dynamic_cast<const ACraft*>(mate)) {
				continue;
			}
			if (g_SceneMan.ShortestDistance(report.ReporterPos, mate->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(c_ShareRange)) {
				mate->AlarmPoint(memory.Pos);
			}
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

void ThreatMemory::NoteLeftCarrier(Actor* unit) {
	if (unit) {
		unit->SetNumberValue("AILeftCarrierMS", static_cast<double>(g_TimerMan.GetSimTimeMS()));
	}
}

float ThreatMemory::GetAgeOf(int team, const Actor* enemy) {
	return enemy ? GetAgeOfID(team, enemy->GetUniqueID()) : -1.0F;
}

float ThreatMemory::GetAgeOfID(int team, long enemyID) {
	if (!s_Enabled || team < 0 || team >= Activity::MaxTeamCount) {
		return -1.0F;
	}
	auto found = s_Memories[team].find(enemyID);
	return found == s_Memories[team].end() ? -1.0F : AgeMS(found->second.SeenUpdate);
}

void ThreatMemory::LookOutFromCarriers() {
	for (Actor* carrier: g_MovableMan.GetActorList()) {
		const ACraft* craft = dynamic_cast<const ACraft*>(carrier);
		const AVehicle* vehicle = craft ? nullptr : dynamic_cast<const AVehicle*>(carrier);
		if ((!craft && !vehicle) || carrier->IsDead() || carrier->GetTeam() < 0 || carrier->GetTeam() >= Activity::MaxTeamCount) {
			continue;
		}
		// Who is inside: those in its seats (a ship's or a vehicle's) and those in its hold. They look as far as the keenest of them sees.
		float sight = 0.0F;
		auto rider = [&sight](const Actor* unit) {
			if (unit && !unit->IsDead()) {
				sight = std::max(sight, unit->GetSightDistance());
			}
		};
		if (const ACShip* ship = dynamic_cast<const ACShip*>(carrier)) {
			for (int seat = 0; seat < ship->GetSeatCount(); ++seat) {
				rider(ship->GetSeatOccupant(seat));
			}
		} else if (vehicle) {
			for (int seat = 0; seat < vehicle->GetSeatCount(); ++seat) {
				rider(vehicle->GetSeatOccupant(seat));
			}
		}
		if (craft) {
			for (const MovableObject* item: *craft->GetInventory()) {
				rider(dynamic_cast<const Actor*>(item));
			}
		}
		if (sight <= 0.0F) {
			continue;
		}
		const Vector eye = carrier->GetPos();
		for (const Actor* enemy: g_MovableMan.GetActorList()) {
			if (enemy == carrier || enemy->GetTeam() == carrier->GetTeam() || enemy->GetTeam() == Activity::NoTeam || enemy->IsDead() || enemy->IsSetToDelete() || enemy->GetHealth() <= 0.0F) {
				continue;
			}
			Vector toEnemy = g_SceneMan.ShortestDistance(eye, enemy->GetPos(), g_SceneMan.SceneWrapsX());
			Vector blocked;
			if (toEnemy.MagnitudeIsGreaterThan(sight) || g_SceneMan.CastStrengthRay(eye, toEnemy, 5.0F, blocked, 3)) {
				continue;
			}
			Report(carrier, enemy);
		}
	}
}

void ThreatMemory::Update() {
	if (g_SceneMan.GetScene() != s_Scene || g_SceneMan.GetSceneGeneration() != s_SceneGeneration) {
		Clear();
		s_Scene = g_SceneMan.GetScene();
		s_SceneGeneration = g_SceneMan.GetSceneGeneration();
	}
	long long now = g_TimerMan.GetSimUpdateCount();
	// (On the updates between the forgetting, so the two don't fall on the same one.)
	if (s_Enabled && now % 30 == 15) {
		LookOutFromCarriers();
	}
	ApplyReports();
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
	for (PendingShard& shard: s_PendingShards) {
		std::scoped_lock lock(shard.Mutex);
		shard.Reports.clear();
	}
	for (std::map<long, Memory>& memories: s_Memories) {
		memories.clear();
	}
}
