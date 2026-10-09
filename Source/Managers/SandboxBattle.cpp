// The Battle Director: up to four teams that keep buying waves of units and sending them in by ship, to attack, hunt brains, defend a place,
// patrol or hold, until they are stopped. It replaced the auto battle, which sent waves to attack until one side was left.

#include "SandboxInternal.h"

#include <functional>
#include <limits>

namespace SandboxDetail {
	namespace {
		bool s_ScriptAnyFaction = false; //!< The old SandboxAutoBattleRandom: the next battle a script starts buys from any faction.
		bool s_ScriptFavourites = false; //!< With it: only units marked as favourites.

		/// Sim updates in a second of game time.
		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		/// The order a team's units are made with. Defend is held here, and given its post as the units are bought (DefendPlace).
		Order OrderOf(BattleStyle style) {
			switch (style) {
				case BattleStyle::Attack:
					return Order::Attack;
				case BattleStyle::HuntBrains:
					return Order::HuntBrains;
				case BattleStyle::Patrol:
					return Order::Patrol;
				default:
					return Order::Hold;
			}
		}

		/// Whether the team defends a place: the style, and a place set for it. (Without a place, its units attack.)
		bool Defends(const BattleSettings& settings) { return settings.Style == BattleStyle::Defend && settings.HasDefendPos; }

		/// The units a team may buy: its factions' (or every faction's, with none ticked), turrets aside, and only the favourites among them when it
		/// asks for those and any are marked.
		std::vector<const Preset*> BattleUnitPool(const BattleSettings& settings) {
			std::vector<const Preset*> pool;
			if (settings.Factions.empty()) {
				pool = RandomUnitPool(false);
			} else {
				for (int moduleID: settings.Factions) {
					std::vector<const Preset*> units = FactionUnits(moduleID);
					pool.insert(pool.end(), units.begin(), units.end());
				}
			}
			if (settings.FavouritesOnly) {
				std::vector<const Preset*> favourites;
				for (const Preset* unit: pool) {
					if (FindFavourite(Tool::Unit, unit->PresetName) >= 0 || FindFavourite(Tool::Drop, unit->PresetName) >= 0) {
						favourites.push_back(unit);
					}
				}
				if (!favourites.empty()) {
					pool.swap(favourites);
				}
			}
			return pool;
		}

		/// Where across the scene one ship of a burst comes in: spread along the team's drop line, or across the whole scene, each ship in a
		/// stretch of its own so a burst doesn't land in a heap.
		float DropX(const BattleSettings& settings, int ship, int ships) {
			float along = (static_cast<float>(ship) + Random01()) / static_cast<float>(std::max(ships, 1));
			if (settings.DropOnLine && settings.HasLine) {
				// (Only the line's span across counts: ships come in from the top, or the bottom.)
				Vector span = g_SceneMan.ShortestDistance(settings.LineA, settings.LineB, g_SceneMan.SceneWrapsX());
				Vector at = settings.LineA + Vector(span.m_X * along, 0.0F);
				g_SceneMan.WrapPosition(at);
				return at.m_X;
			}
			float width = static_cast<float>(g_SceneMan.GetSceneWidth());
			float margin = g_SceneMan.SceneWrapsX() ? 0.0F : std::min(60.0F, width * 0.1F);
			return margin + (width - margin * 2.0F) * along;
		}

		/// Gives a unit just bought its post in the place its team defends: somewhere on the ground inside the radius. It walks there once its
		/// ship has let it out, and from then on UpdateBattleDefenders sends it after enemies near the place and back again.
		/// A post somewhere on the ground inside a defended place's radius.
		Vector PostIn(const BattleSettings& settings) {
			float radius = static_cast<float>(std::max(settings.DefendRadius, 1));
			Vector around = settings.DefendPos + Vector((Random01() * 2.0F - 1.0F) * radius * 0.6F, 0.0F);
			g_SceneMan.WrapPosition(around);
			std::vector<Vector> spots = StandingSpots(around, 1);
			return spots.empty() ? settings.DefendPos : spots.front();
		}

		/// The place, radius and chase distance a defender goes by: its team card's as they are now, so a change on the card reaches the
		/// units already in (they kept what the card said when they were bought: chase distance lowered, they still chased as far as before).
		void FollowCard(BattleDefender& defender) {
			if (defender.Team < 0 || defender.Team >= c_Sides || !Defends(s_BattleTeams[defender.Team].Settings)) {
				return;
			}
			const BattleSettings& settings = s_BattleTeams[defender.Team].Settings;
			defender.Radius = static_cast<float>(std::max(settings.DefendRadius, 1));
			defender.Chase = static_cast<float>(std::max(settings.ChaseDistance, 0));
			defender.Roams = defender.RoamRoll * 100.0F < static_cast<float>(settings.RoamPercent);
			const bool wraps = g_SceneMan.SceneWrapsX();
			if (!g_SceneMan.ShortestDistance(defender.Center, settings.DefendPos, wraps).MagnitudeIsLessThan(1.0F)) {
				// The place moved: a post in the new one.
				defender.Center = settings.DefendPos;
				defender.Post = PostIn(settings);
				defender.IdleSince = -1;
			} else if (!defender.Roams && !g_SceneMan.ShortestDistance(defender.Center, defender.Post, wraps).MagnitudeIsLessThan(defender.Radius + 30.0F)) {
				// Roaming no more (the share was lowered), and out in the zone: a post inside the radius again.
				defender.Post = PostIn(settings);
			}
		}

		/// Somewhere on the ground for a roamer to walk to next: anywhere inside the place's radius and chase distance.
		Vector RoamSpot(const BattleDefender& defender) {
			const float reach = defender.Radius + defender.Chase;
			for (int attempt = 0; attempt < 6; ++attempt) {
				Vector around = defender.Center + Vector((Random01() * 2.0F - 1.0F) * reach * 0.9F, 0.0F);
				g_SceneMan.WrapPosition(around);
				std::vector<Vector> spots = StandingSpots(around, 1);
				if (!spots.empty() && g_SceneMan.ShortestDistance(defender.Center, spots.front(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(reach * 0.95F)) {
					return spots.front();
				}
			}
			return defender.Post;
		}

		/// Gives a unit just bought its post in the place its team defends: somewhere on the ground inside the radius. It walks there once its
		/// ship has let it out, and from then on UpdateBattleDefenders sends it after enemies near the place and back again.
		void DefendPlace(Actor* unit, const BattleSettings& settings) {
			Vector post = PostIn(settings);
			unit->ClearAIWaypoints();
			unit->AddAISceneWaypoint(post);
			unit->SetAIMode(Actor::AIMODE_GOTO);
			unit->SetOrderPost(post);
			BattleDefender& defender = s_BattleDefenders[unit->GetUniqueID()];
			defender.Team = unit->GetTeam();
			defender.Center = settings.DefendPos;
			defender.Radius = static_cast<float>(std::max(settings.DefendRadius, 1));
			defender.Chase = static_cast<float>(std::max(settings.ChaseDistance, 0));
			defender.Post = post;
			defender.Made = g_TimerMan.GetSimUpdateCount();
			defender.RoamRoll = Random01();
			defender.Roams = defender.RoamRoll * 100.0F < static_cast<float>(settings.RoamPercent);
		}

		/// Whether a defender is at its post or on its way back to it, as UpdateBattleDefenders last sent it: its post kept, no attack order, and
		/// not walking anywhere else. Its own AI can put an attack back on it after (a flank or a fall-back started mid-chase puts back the
		/// order it had then), which this sees.
		bool HeadingForPost(const Actor* unit, const BattleDefender& defender) {
			const bool wraps = g_SceneMan.SceneWrapsX();
			// (Its post the one it has now: the place may have been moved on the card since.)
			if (!unit->GetOrderHasPost() || unit->GetOrderAttack() || !g_SceneMan.ShortestDistance(unit->GetOrderPost(), defender.Post, wraps).MagnitudeIsLessThan(60.0F)) {
				return false;
			}
			if (unit->GetAIMode() == Actor::AIMODE_GOTO) {
				return !unit->GetMOMoveTarget() && g_SceneMan.ShortestDistance(unit->GetLastAIWaypoint(), defender.Post, wraps).MagnitudeIsLessThan(60.0F);
			}
			return true;
		}

		/// Starts a team sending ships: afresh (nothing spent or sent yet), or carrying on where it stopped unless it had run out of money.
		/// @param delay Sim updates to its first ships, so teams started together don't all arrive at once.
		void StartTeam(int side, bool afresh, long long delay) {
			BattleTeam& team = s_BattleTeams[side];
			if (afresh || team.Broke) {
				team.Spent = 0.0F;
				team.Sent = 0;
				team.Broke = false;
			}
			team.Running = true;
			team.NextWave = g_TimerMan.GetSimUpdateCount() + delay;
			team.NextZoneWave = team.NextWave;
		}

		/// Starts every active team afresh, and stops the rest.
		void StartAllTeams() {
			if (!s_CatalogueBuilt) {
				BuildCatalogue();
			}
			long long delay = 0;
			for (int side = 0; side < c_Sides; ++side) {
				if (s_BattleTeams[side].Settings.Active) {
					StartTeam(side, true, delay);
					delay += 60;
				} else {
					s_BattleTeams[side].Running = false;
				}
			}
		}

		/// Ends a ship that can't be hurt and hasn't left: it stops flying and dies, but doesn't blow up. It falls, and once it lies still the
		/// game settles it into the terrain where it lies, as a body is (MovableMan), so it becomes part of the ground rather than a wreck that
		/// nothing can shift.
		void SettleCraft(ACraft* ship) {
			if (!g_MovableMan.IsParticleSettlingEnabled()) {
				// (With settling turned off in the settings it would lie there for good: it is taken away instead.)
				ship->SetToDelete(true);
				return;
			}
			ship->SetScuttleOnDeath(false);
			ship->SetAIMode(Actor::AIMODE_SENTRY);
			ship->SetHealth(0.0F);
			ship->SetStatus(Actor::DEAD);
			// (A ship whose own rest time is never, read from its file, would never be settled.)
			if (ship->GetRestThreshold() < 0) {
				ship->SetRestThreshold(500);
			}
		}

		/// The ships that can't be hurt are kept so, each update, until they've unloaded and gone: off the top (or bottom) of the scene, where
		/// the game takes them away itself. One still about ten seconds after it was emptied, and not climbing away, is settled into the
		/// terrain (SettleCraft), so none is left standing about, or hanging over the battle.
		void UpdateBattleCraft() {
			long long now = g_TimerMan.GetSimUpdateCount();
			long long linger = static_cast<long long>(10.0F * UpdatesPerSecond());
			for (auto craft = s_BattleCraft.begin(); craft != s_BattleCraft.end();) {
				ACraft* ship = dynamic_cast<ACraft*>(GetRef(craft->Ship));
				if (!ship || ship->IsSetToDelete()) {
					craft = s_BattleCraft.erase(craft);
					continue;
				}
				// (One past its time but still on its way up and out is leaving, and is let be.)
				if (ship->IsInventoryEmpty() && craft->Emptied >= 0 && now - craft->Emptied > linger && ship->GetVel().GetY() > -2.0F) {
					SettleCraft(ship);
					craft = s_BattleCraft.erase(craft);
					continue;
				}
				ship->SetHealth(ship->GetMaxHealth());
				if (int wounds = ship->GetWoundCount(); wounds > 0) {
					ship->RemoveWounds(wounds);
				}
				// (A craft left lying on its side is set dying by its own update, whatever its health.)
				if (ship->GetStatus() == Actor::DYING || ship->GetStatus() == Actor::DEAD) {
					ship->SetStatus(Actor::STABLE);
				}
				if (ship->IsInventoryEmpty() && craft->Emptied < 0) {
					craft->Emptied = now;
				}
				++craft;
			}
		}

		/// Takes every ship of a team off the map, at once and without a blast: the ships that can't be hurt and any other, with anyone still
		/// aboard.
		void ClearTeamCraft(int side) {
			for (Actor* actor: SandboxAccess::Actors()) {
				if (dynamic_cast<ACraft*>(actor) && actor->GetTeam() == side) {
					actor->SetToDelete(true);
				}
			}
		}

		/// The settings named by a script, as a team's in the sim and on its card alike.
		void ShowOnCard(int side) { s_BattleSetup[side] = s_BattleTeams[side].Settings; }

		/// The module IDs of factions named by a script, comma-separated, as "Coalition" or "Coalition.rte". Names not known are left out.
		std::vector<int> FactionsNamed(const std::string& names) {
			std::vector<int> factions;
			std::stringstream list(names);
			std::string name;
			while (std::getline(list, name, ',')) {
				name.erase(0, name.find_first_not_of(" \t"));
				name.erase(name.find_last_not_of(" \t") + 1);
				for (size_t i = 0; i < s_FactionNames.size() && i < s_FactionModules.size(); ++i) {
					if (!name.empty() && (s_FactionNames[i] == name || s_FactionNames[i] + ".rte" == name) && std::find(factions.begin(), factions.end(), s_FactionModules[i]) == factions.end()) {
						factions.push_back(s_FactionModules[i]);
					}
				}
			}
			return factions;
		}
	} // namespace

	/// Gives a unit just bought a post in a place it defends, as a defending team's are given theirs (a mode's guards, by settings made for
	/// them).
	void MakeDefender(Actor* unit, const BattleSettings& settings) { DefendPlace(unit, settings); }

	/// Moves the place a team's defenders defend (a mode's guards going after their flag), each given a post in the new one: right at it with
	/// atIt (to pick up what lies there), else somewhere on the ground inside their radius.
	void RecentreDefenders(int team, const Vector& centre, bool atIt) {
		const bool wraps = g_SceneMan.SceneWrapsX();
		for (auto& [id, defender]: s_BattleDefenders) {
			if (defender.Team != team || g_SceneMan.ShortestDistance(defender.Center, centre, wraps).MagnitudeIsLessThan(1.0F)) {
				continue;
			}
			defender.Center = centre;
			defender.IdleSince = -1;
			if (atIt) {
				defender.Post = centre;
			} else {
				Vector around = centre + Vector((Random01() * 2.0F - 1.0F) * defender.Radius * 0.6F, 0.0F);
				g_SceneMan.WrapPosition(around);
				std::vector<Vector> spots = StandingSpots(around, 1);
				defender.Post = spots.empty() ? centre : spots.front();
			}
		}
	}

	/// Makes a craft take no harm: no wound hurts it and nothing breaks it apart or knocks a part off it, and UpdateBattleCraft keeps it whole
	/// and takes it away once it has delivered and left.
	void KeepCraftWhole(ACraft* ship) {
		if (!ship) {
			return;
		}
		std::function<void(MOSRotating*)> harden = [&harden](MOSRotating* part) {
			part->SetDamageMultiplier(0.0F);
			part->SetGibImpulseLimit(0.0F);
			part->SetGibWoundLimit(0);
			for (Attachable* attachable: part->GetAttachableList()) {
				// (A joint strength of 0 is one that never gives.)
				attachable->SetJointStrength(0.0F);
				harden(attachable);
			}
		};
		harden(ship);
		s_BattleCraft.push_back({MakeRef(ship), -1});
	}

	/// The defenders of a place go after enemies near it, but only so far: an enemy within the defend radius and chase distance of the place
	/// is gone after, while the defender itself is within that of the place; with none, or once it has strayed past that, it goes back to its
	/// post, where it stands and fights from (ReturnDefenders sees it settled there). Every half second, by the team card as it is now.
	/// (It was sent back once, when its chase ended, and never looked at again: one whose AI took the attack up again after, or that went off
	/// after enemies of its own accord, kept going till it died. Now any defender not chasing and not heading for its post is sent back.)
	void UpdateBattleDefenders() {
		if (s_BattleDefenders.empty()) {
			return;
		}
		const bool wraps = g_SceneMan.SceneWrapsX();
		const long long now = g_TimerMan.GetSimUpdateCount();
		const long long riding = static_cast<long long>(60.0F * UpdatesPerSecond());
		std::unordered_map<long, Actor*> byID;
		std::vector<Actor*> fighters;
		for (Actor* actor: SandboxAccess::Actors()) {
			byID[actor->GetUniqueID()] = actor;
			if (IsCombatant(actor) && !dynamic_cast<const ACraft*>(actor) && !actor->IsIgnoredByAI()) {
				fighters.push_back(actor);
			}
		}
		for (auto entry = s_BattleDefenders.begin(); entry != s_BattleDefenders.end();) {
			BattleDefender& defender = entry->second;
			auto found = byID.find(entry->first);
			Actor* unit = found == byID.end() ? nullptr : found->second;
			if (!unit) {
				// Still riding in its ship (for a minute at most), or gone.
				entry = defender.Seen || now - defender.Made > riding ? s_BattleDefenders.erase(entry) : std::next(entry);
				continue;
			}
			defender.Seen = true;
			if (!IsCombatant(unit)) {
				entry = s_BattleDefenders.erase(entry);
				continue;
			}
			if (unit->IsPlayerControlled()) {
				++entry;
				continue;
			}
			FollowCard(defender);
			const float reach = defender.Radius + defender.Chase;
			Actor* enemy = nullptr;
			if (g_SceneMan.ShortestDistance(defender.Center, unit->GetPos(), wraps).MagnitudeIsLessThan(reach)) {
				float nearest = 0.0F;
				for (Actor* other: fighters) {
					if (other->GetTeam() == unit->GetTeam() || !g_SceneMan.ShortestDistance(defender.Center, other->GetPos(), wraps).MagnitudeIsLessThan(reach)) {
						continue;
					}
					float distance = g_SceneMan.ShortestDistance(unit->GetPos(), other->GetPos(), wraps).GetSqrMagnitude();
					if (!enemy || distance < nearest) {
						enemy = other;
						nearest = distance;
					}
				}
			}
			if (enemy) {
				if (defender.ChasingID != static_cast<long>(enemy->GetUniqueID())) {
					defender.ChasingID = static_cast<long>(enemy->GetUniqueID());
					SendUnit(unit, enemy->GetPos(), enemy, true, "defending: after an enemy", false, true);
				}
			} else if (defender.ChasingID != 0 || !HeadingForPost(unit, defender)) {
				defender.ChasingID = 0;
				SendUnit(unit, defender.Post, nullptr, false, "defending: back to its post", false, true);
				unit->SetOrderPost(defender.Post);
				defender.IdleSince = -1;
			} else if (defender.Roams && unit->GetAIMode() != Actor::AIMODE_GOTO) {
				// A roamer at its spot: it waits a few seconds, then walks on to another somewhere in the zone.
				if (defender.IdleSince < 0) {
					defender.IdleSince = now;
					defender.Dwell = static_cast<long long>((3.0F + Random01() * 7.0F) * UpdatesPerSecond());
				} else if (now - defender.IdleSince > defender.Dwell) {
					defender.Post = RoamSpot(defender);
					defender.IdleSince = -1;
					SendUnit(unit, defender.Post, nullptr, false, "defending: roaming", false, true);
					unit->SetOrderPost(defender.Post);
				}
			}
			++entry;
		}
	}

	/// Each running team sends a burst of ships every so often, each with a wave of units bought from what's left of its budget, until it is
	/// stopped. With the AI paused, nothing is sent and the clocks are held back, so the waves don't all come at once after.
	void UpdateBattle(bool aiPaused) {
		UpdateBattleCraft();
		UpdateBattleMode(aiPaused);
		long long now = g_TimerMan.GetSimUpdateCount();
		if (aiPaused) {
			for (BattleTeam& team: s_BattleTeams) {
				++team.NextWave;
				++team.NextZoneWave;
			}
			return;
		}
		if (now % 30 == 0) {
			UpdateBattleDefenders();
		}
		for (int side = 0; side < c_Sides; ++side) {
			BattleTeam& team = s_BattleTeams[side];
			const BattleSettings& settings = team.Settings;
			if (!team.Running || team.Broke || s_FactionModules.empty()) {
				continue;
			}
			// Ships and spawn zones each on a clock of their own.
			const bool shipsDue = settings.ShipsPerBurst > 0 && now >= team.NextWave;
			const bool zonesDue = !settings.SpawnZones.empty() && now >= team.NextZoneWave;
			if (!shipsDue && !zonesDue) {
				continue;
			}
			if (shipsDue) {
				team.NextWave = now + std::max(1LL, static_cast<long long>(static_cast<float>(std::max(settings.EverySeconds, 1)) * UpdatesPerSecond()));
			}
			if (zonesDue) {
				team.NextZoneWave = now + std::max(1LL, static_cast<long long>(static_cast<float>(std::max(settings.ZoneEverySeconds, 1)) * UpdatesPerSecond()));
			}
			// Under a unit limit, only as many as top it up (counting those still riding in): none at all when it's reached, till the next
			// burst.
			int room = settings.UnitLimit > 0 ? settings.UnitLimit - Sandbox::CountUnits(side) : std::numeric_limits<int>::max();
			if (room <= 0) {
				continue;
			}
			// A few dozen of the units it may buy, picked afresh each burst, are priced and bought from, not the whole list (each pricing
			// makes the unit and its loadout).
			std::vector<const Preset*> choices = BattleUnitPool(settings);
			for (size_t i = 0; i < choices.size() && i < 24; ++i) {
				size_t other = i + std::min(choices.size() - i - 1, static_cast<size_t>(Random01() * static_cast<float>(choices.size() - i)));
				std::swap(choices[i], choices[other]);
			}
			if (choices.size() > 24) {
				choices.resize(24);
			}
			const Order order = Defends(settings) ? Order::Hold : OrderOf(settings.Style);
			// What each costs as bought (with its loadout), and the cheapest. A wave's budget is at least the cheapest unit, and picks are made
			// only from what still fits: a faction whose cheapest unit cost more than a wave may spend (heavy mechs, some mods) never filled one
			// and was called broke before buying anything (review S7).
			std::vector<std::pair<const Preset*, float>> priced;
			float cheapest = -1.0F;
			for (const Preset* choice: choices) {
				if (Actor* unit = CreateUnit(*choice, side, 0, order)) {
					float cost = unit->GetTotalValue(unit->GetModuleID(), 1.0F);
					delete unit;
					priced.emplace_back(choice, cost);
					cheapest = cheapest < 0.0F ? cost : std::min(cheapest, cost);
				}
			}
			// Buys one ship's or one zone's units, as many as fit what's left of the budget, up to a number; none when nothing more can be
			// afforded, which leaves the team broke.
			auto buyWave = [&](int size) {
				std::vector<Actor*> wave;
				float left = settings.EndlessMoney ? std::numeric_limits<float>::max() : static_cast<float>(settings.Budget) - team.Spent;
				// (180 a unit: the 900 a wave of five always had.)
				float waveBudget = std::min(left, std::max(180.0F * static_cast<float>(size), cheapest));
				float waveCost = 0.0F;
				for (int attempt = 0; attempt < size * 3 && static_cast<int>(wave.size()) < size; ++attempt) {
					std::vector<const Preset*> affordable;
					for (const auto& [choice, cost]: priced) {
						if (waveCost + cost <= waveBudget) {
							affordable.push_back(choice);
						}
					}
					if (affordable.empty()) {
						break;
					}
					const Preset* pick = affordable[std::min(affordable.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(affordable.size())))];
					Actor* unit = CreateUnit(*pick, side, 0, order);
					float cost = unit ? unit->GetTotalValue(unit->GetModuleID(), 1.0F) : 0.0F;
					if (unit && waveCost + cost <= waveBudget) {
						wave.push_back(unit);
						waveCost += cost;
					} else {
						delete unit;
					}
				}
				if (wave.empty()) {
					// Nothing left it can afford (or nothing to buy at all).
					team.Broke = !settings.EndlessMoney || priced.empty();
				} else if (Defends(settings)) {
					for (Actor* unit: wave) {
						DefendPlace(unit, settings);
					}
				}
				if (!wave.empty() && s_ModeRun.Running) {
					// (A mode's own jobs for them: capture the flag's guards and flag runners.)
					ModeUnitsMade(side, wave);
				}
				return wave;
			};
			const int waveSize = std::clamp(settings.WaveSize, 1, 20);
			if (shipsDue) {
				const int ships = std::clamp(settings.ShipsPerBurst, 1, 10);
				for (int ship = 0; ship < ships && room > 0 && !team.Broke; ++ship) {
					std::vector<Actor*> wave = buyWave(std::min(waveSize, room));
					if (wave.empty()) {
						break;
					}
					// (Counted as sent only once a craft took them: with no craft to be had, DropUnits deletes the units and returns nothing.)
					int count = static_cast<int>(wave.size());
					float paid = DropUnits(wave, side, DropX(settings, ship, ships), settings.Craft, settings.Invincible);
					if (paid > 0.0F) {
						team.Sent += count;
						team.Spent += paid;
						room -= count;
					}
				}
			}
			if (zonesDue) {
				// The zones in a fresh order each time, so a limit or a budget that runs short doesn't always leave out the same ones.
				std::vector<Vector> zones = settings.SpawnZones;
				for (size_t i = 0; i + 1 < zones.size(); ++i) {
					std::swap(zones[i], zones[i + std::min(zones.size() - i - 1, static_cast<size_t>(Random01() * static_cast<float>(zones.size() - i)))]);
				}
				const int perZone = std::clamp(settings.ZoneUnits, 1, 20);
				for (const Vector& zone: zones) {
					if (room <= 0 || team.Broke) {
						break;
					}
					std::vector<Actor*> wave = buyWave(std::min(perZone, room));
					if (wave.empty()) {
						break;
					}
					// Each on the ground there, spread out sideways, and dropped in from just above its feet.
					std::vector<Vector> spots = StandingSpots(zone, static_cast<int>(wave.size()));
					ActivateSide(side);
					for (size_t i = 0; i < wave.size(); ++i) {
						Actor* unit = wave[i];
						Vector feet = i < spots.size() ? spots[i] : zone;
						team.Spent += unit->GetTotalValue(unit->GetModuleID(), 1.0F);
						unit->SetPos(feet - Vector(0.0F, unit->GetHeight() * 0.5F));
						g_MovableMan.AddActor(unit);
					}
					team.Sent += static_cast<int>(wave.size());
					room -= static_cast<int>(wave.size());
				}
			}
		}
	}

	/// A change made on the Battle tab (or by a script, with the defence point and drop line tools), in the sim: a team's settings, and a
	/// start or stop (BattleCommand).
	void ApplyBattleStroke(const Stroke& stroke) {
		if (!s_CatalogueBuilt) {
			BuildCatalogue();
		}
		const int side = stroke.Team;
		const bool oneTeam = side >= 0 && side < c_Sides;
		if (stroke.Kind == Tool::BattleModePoint || (stroke.Kind == Tool::BattleTeam && (stroke.Count == BattleModeSet || stroke.Count == BattleModeStart || stroke.Count == BattleModeStop))) {
			ApplyBattleMode(stroke);
			return;
		}
		if (IsBattleTool(stroke.Kind)) {
			// From a script's SandboxDo: the window sends the whole settings instead.
			if (oneTeam) {
				BattleSettings& settings = s_BattleTeams[side].Settings;
				if (stroke.Kind == Tool::BattleSpawnZone) {
					ToggleSpawnZone(settings, stroke.Position);
				} else if (stroke.Kind == Tool::BattleDefendPoint) {
					settings.DefendPos = stroke.Position;
					settings.HasDefendPos = true;
				} else {
					settings.LineA = stroke.Position;
					settings.LineB = stroke.Position2.IsZero() ? stroke.Position : stroke.Position2;
					settings.HasLine = true;
					settings.DropOnLine = true;
				}
				ShowOnCard(side);
			}
			return;
		}
		if (oneTeam) {
			// (While a mode's game is on, the card is only where its units come from: the rest is the mode's.)
			s_BattleTeams[side].Settings = s_ModeRun.Running ? ModeTeamSettings(side, stroke.Battle) : stroke.Battle;
		}
		switch (stroke.Count) {
			case BattleStartTeam:
				if (oneTeam) {
					StartTeam(side, false, 0);
				}
				break;
			case BattleStopTeam:
				if (oneTeam) {
					s_BattleTeams[side].Running = false;
				}
				break;
			case BattleStartAll:
				// (The cards' battle, not a mode's.)
				s_ModeRun.Running = false;
				s_ModeRun.Over = false;
				StartAllTeams();
				break;
			case BattleStopAll:
				for (BattleTeam& team: s_BattleTeams) {
					team.Running = false;
				}
				break;
			case BattleClearCraft:
				if (oneTeam) {
					ClearTeamCraft(side);
				}
				break;
			default:
				break;
		}
	}

	/// Sends a team's card to the sim, as it now stands, with a start or stop if asked for; or with no team, only the start or stop of them all.
	void SendBattleSettings(int team, int command) {
		Stroke stroke;
		stroke.Kind = Tool::BattleTeam;
		stroke.Team = team;
		stroke.Count = command;
		if (team >= 0 && team < c_Sides) {
			stroke.Battle = s_BattleSetup[team];
		}
		s_Queue.push_back(stroke);
	}

	/// A new game: no team is running, and the places set for them (on the last game's scene) are gone.
	void ForgetBattle() {
		for (BattleTeam& team: s_BattleTeams) {
			team.Running = false;
			team.Spent = 0.0F;
			team.Sent = 0;
			team.Broke = false;
			team.NextWave = 0;
			team.Settings.HasDefendPos = false;
			team.Settings.HasLine = false;
			team.Settings.SpawnZones.clear();
			team.NextZoneWave = 0;
		}
		for (BattleSettings& setup: s_BattleSetup) {
			setup.HasDefendPos = false;
			setup.HasLine = false;
			setup.SpawnZones.clear();
		}
		s_BattleDefenders.clear();
		s_BattleCraft.clear();
		s_ScriptAnyFaction = false;
		s_ScriptFavourites = false;
		ForgetBattleMode();
	}

	/// The card's factions: a list to tick, none ticked for any faction.
	bool FactionPicker(BattleSettings& setup) {
		bool changed = false;
		std::string preview = "Any faction";
		if (setup.Factions.size() == 1) {
			auto known = std::find(s_FactionModules.begin(), s_FactionModules.end(), setup.Factions.front());
			preview = known != s_FactionModules.end() && static_cast<size_t>(known - s_FactionModules.begin()) < s_FactionNames.size() ? s_FactionNames[known - s_FactionModules.begin()] : std::string("1 faction");
		} else if (setup.Factions.size() > 1) {
			preview = std::to_string(setup.Factions.size()) + " factions";
		}
		if (ImGui::BeginCombo("Factions", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
			bool any = setup.Factions.empty();
			if (ToolUI::Checkbox("Any faction", &any) && any) {
				setup.Factions.clear();
				changed = true;
			}
			for (size_t i = 0; i < s_FactionModules.size() && i < s_FactionNames.size(); ++i) {
				auto at = std::find(setup.Factions.begin(), setup.Factions.end(), s_FactionModules[i]);
				bool ticked = at != setup.Factions.end();
				ImGui::PushID(static_cast<int>(i));
				if (ToolUI::Checkbox(s_FactionNames[i].c_str(), &ticked)) {
					if (ticked) {
						setup.Factions.push_back(s_FactionModules[i]);
					} else {
						setup.Factions.erase(at);
					}
					changed = true;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("The factions this team's units come from. None ticked: any faction.");
		return changed;
	}

	namespace {
		/// One team's card on the Battle tab.
		void BattleCard(int side) {
			BattleSettings& setup = s_BattleSetup[side];
			const BattleTeam& team = s_BattleTeams[side];
			bool changed = false;
			ImGui::PushID(side);
			char header[160];
			const char* state = setup.Active ? "ready" : "not taking part";
			if (team.Running) {
				state = team.Broke ? "out of money" : "running";
			}
			std::snprintf(header, sizeof(header), "%s: %s, %d sent, %d alive###card", c_SideNames[side], state, team.Sent, Sandbox::CountUnits(side));
			ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
			bool open = ImGui::CollapsingHeader(header, side < 2 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
			ImGui::PopStyleColor();
			if (!open) {
				ImGui::PopID();
				return;
			}
			changed |= ToolUI::Checkbox("Active", &setup.Active);
			ImGui::SetItemTooltip("Takes part when the battle is started.");
			ImGui::SameLine();
			if (team.Running) {
				if (ToolUI::Button("Stop this team", ImVec2(-1.0F, 0.0F))) {
					SendBattleSettings(side, BattleStopTeam);
					changed = false;
				}
				ImGui::SetItemTooltip("No more ships for this team. Its units already in stay.");
			} else if (ToolUI::Button("Start this team", ImVec2(-1.0F, 0.0F))) {
				SendBattleSettings(side, BattleStartTeam);
				changed = false;
			}

			changed |= FactionPicker(setup);
			changed |= ToolUI::Checkbox("Favourites only", &setup.FavouritesOnly);
			ImGui::SetItemTooltip("Only the units marked as favourites (Ctrl+click on a tile in the Spawn tab). With none of them marked, any.");
			int style = static_cast<int>(setup.Style);
			if (ImGui::Combo("Play style", &style, c_BattleStyleNames, static_cast<int>(BattleStyle::Count))) {
				setup.Style = static_cast<BattleStyle>(style);
				changed = true;
			}
			if (setup.Style == BattleStyle::Defend) {
				bool placing = CurrentTool().Kind == Tool::BattleDefendPoint && s_BattleEditTeam == side;
				if (ToolUI::Button(placing ? "Done (Enter)##defend" : "Set defence point")) {
					if (placing) {
						PutDownBattleTool();
					} else {
						TakeBattleTool(Tool::BattleDefendPoint, side);
					}
				}
				ImGui::SetItemTooltip(placing ? "Click the map to move it; Enter (or this) when it's where you want it." : "Then click the map where its units are to stand.");
				ImGui::SameLine();
				if (setup.HasDefendPos) {
					ImGui::TextDisabled("set at %d, %d", setup.DefendPos.GetFloorIntX(), setup.DefendPos.GetFloorIntY());
				} else {
					ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.4F, 1.0F), "not set: they attack till it is");
				}
				changed |= ImGui::SliderInt("Defend radius", &setup.DefendRadius, 30, 600, "%d px");
				ImGui::SetItemTooltip("How far round the defence point its units stand and fight.");
				changed |= ImGui::SliderInt("Chase distance", &setup.ChaseDistance, 0, 1500, "%d px");
				ImGui::SetItemTooltip("How far past the radius they go after an enemy before giving up and going back to their posts.");
				changed |= ImGui::SliderInt("Roaming", &setup.RoamPercent, 0, 100, "%d%% of them");
				ImGui::SetItemTooltip("The share of its defenders that roam the whole zone, radius and chase distance alike, from spot to spot, rather than holding a post. They still go after enemies in it, and never past it.");
			}

			ImGui::SeparatorText("Waves");
			changed |= ToolUI::Checkbox("No money limit", &setup.EndlessMoney);
			if (!setup.EndlessMoney) {
				changed |= ImGui::SliderInt("Budget", &setup.Budget, 500, 100000, "%d oz", ImGuiSliderFlags_Logarithmic);
				ImGui::SetItemTooltip("What the team may spend in all. Once it can't afford another unit it stops sending ships; starting it again gives it its budget back.");
			}
			changed |= ImGui::SliderInt("Units per ship", &setup.WaveSize, 1, 10);
			changed |= ImGui::SliderInt("Unit limit", &setup.UnitLimit, 0, 200, setup.UnitLimit > 0 ? "%d units" : "no limit");
			ImGui::SetItemTooltip("Most units the team has in at once, counting those still in its ships. At the limit no ships come; below it, only enough units to top it up. 0: no limit.");

			ImGui::SeparatorText("Ships");
			changed |= ImGui::Combo("Craft", &setup.Craft, "Dropship\0Rocket\0");
			if (ToolUI::RadioButton("Anywhere##from", !setup.DropOnLine)) {
				setup.DropOnLine = false;
				changed = true;
			}
			ImGui::SetItemTooltip("Ships come in anywhere across the scene.");
			ImGui::SameLine();
			if (ToolUI::RadioButton("Over a drop line##from", setup.DropOnLine)) {
				setup.DropOnLine = true;
				changed = true;
			}
			ImGui::SetItemTooltip("Ships come in only over a line you draw on the map, spread along it.");
			if (setup.DropOnLine) {
				bool drawing = CurrentTool().Kind == Tool::BattleDropLine && s_BattleEditTeam == side;
				if (ToolUI::Button(drawing ? "Save line (Enter)##line" : "Draw drop line")) {
					if (drawing) {
						PutDownBattleTool();
					} else {
						TakeBattleTool(Tool::BattleDropLine, side);
					}
				}
				ImGui::SetItemTooltip(drawing ? "Drag on the map to draw it (again to redraw); Enter (or this) to save it and put the tool down." : "Then drag on the map along where the ships are to come in.");
				ImGui::SameLine();
				if (setup.HasLine) {
					ImGui::TextDisabled("drawn");
				} else {
					ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.4F, 1.0F), "not drawn: anywhere till it is");
				}
			}
			changed |= ImGui::SliderInt("Ships at a time", &setup.ShipsPerBurst, 0, 6, setup.ShipsPerBurst > 0 ? "%d" : "no ships");
			ImGui::SetItemTooltip("How many ships set off together, each with a wave of its own and its own place to come in. 0: no ships, the team's units come only from its spawn zones.");
			changed |= ImGui::SliderInt("Every", &setup.EverySeconds, 5, 300, "%d s", ImGuiSliderFlags_Logarithmic);
			ImGui::SetItemTooltip("Seconds of game time between one lot of ships and the next.");
			changed |= ToolUI::Checkbox("Ships can't be hurt", &setup.Invincible);
			ImGui::SetItemTooltip("The ships take no harm. One that hasn't left ten seconds after it unloaded falls dead, without a blast, and becomes part of the ground.");
			if (ToolUI::Button("Clear all drop ships", ImVec2(-1.0F, 0.0F))) {
				SendBattleSettings(side, BattleClearCraft);
				changed = false;
			}
			ImGui::SetItemTooltip("Every ship of this team taken off the map now, without a blast, with anyone still aboard.");

			ImGui::SeparatorText("Spawn zones");
			bool zoning = CurrentTool().Kind == Tool::BattleSpawnZone && s_BattleEditTeam == side;
			if (ToolUI::Button(zoning ? "Done (Enter)##zones" : "Place spawn zones")) {
				if (zoning) {
					PutDownBattleTool();
				} else {
					TakeBattleTool(Tool::BattleSpawnZone, side);
				}
			}
			ImGui::SetItemTooltip(zoning ? "Click the map to put a zone down, or on one to take it away; Enter (or this) when done." : "Then click the map wherever this team's units are to appear, as many places as you like.");
			ImGui::SameLine();
			if (setup.SpawnZones.empty()) {
				ImGui::TextDisabled("none");
			} else {
				ImGui::TextDisabled("%d placed", static_cast<int>(setup.SpawnZones.size()));
				ImGui::SameLine();
				if (ToolUI::Button("Clear##zones")) {
					setup.SpawnZones.clear();
					changed = true;
				}
				ImGui::SetItemTooltip("Takes away all of this team's spawn zones.");
			}
			if (!setup.SpawnZones.empty()) {
				changed |= ImGui::SliderInt("Units per zone", &setup.ZoneUnits, 1, 10);
				ImGui::SetItemTooltip("How many units appear at each zone each time (fewer when the budget or the unit limit runs short).");
				changed |= ImGui::SliderInt("Every##zones", &setup.ZoneEverySeconds, 5, 300, "%d s", ImGuiSliderFlags_Logarithmic);
				ImGui::SetItemTooltip("Seconds of game time between one lot of units at the zones and the next. The zones keep their own time, apart from the ships.");
			}
			if (changed) {
				SendBattleSettings(side);
			}
			ImGui::PopID();
		}
	} // namespace

	/// The Battle tab: a card for each team, and the battle started and stopped.
	void BattleTab() {
		if (BattleModeChooser()) {
			// A mode: its own panel instead of the cards.
			BattleModeTab();
			return;
		}
		ImGui::TextWrapped("Teams that keep sending in ships of units, until you stop them. Set each team up on its card and tick Active, then start the battle. A team can be started or stopped on its own while it runs.");
		bool anyRunning = std::any_of(s_BattleTeams.begin(), s_BattleTeams.end(), [](const BattleTeam& team) { return team.Running; });
		float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
		if (ToolUI::Button(anyRunning ? "Start again" : "Start battle", ImVec2(half, 0.0F))) {
			for (int side = 0; side < c_Sides; ++side) {
				SendBattleSettings(side);
			}
			SendBattleSettings(-1, BattleStartAll);
		}
		ImGui::SetItemTooltip("Every active team starts afresh, with nothing spent or sent yet.");
		ImGui::SameLine();
		ImGui::BeginDisabled(!anyRunning);
		if (ToolUI::Button("Stop all", ImVec2(-1.0F, 0.0F))) {
			SendBattleSettings(-1, BattleStopAll);
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("No more ships for any team. The units already in stay.");
		for (int side = 0; side < c_Sides; ++side) {
			BattleCard(side);
		}
	}

	/// Puts the card's defence point or drop line tool in hand, for a team, keeping the tool it replaces for PutDownBattleTool.
	void TakeBattleTool(Tool kind, int team) {
		Tool held = CurrentTool().Kind;
		if (!IsBattleTool(held)) {
			s_ToolBeforeBattle = s_ToolIndex;
		}
		s_BattleEditTeam = team;
		TookTool(ToolIndex(kind));
	}

	/// Done with the defence point or drop line tool (Enter, or the card's button): what it set is kept, as it was sent the moment it was
	/// clicked or drawn, and the tool in hand before is given back (the command tool, if none). The tool stayed in hand till another was
	/// picked, with nothing to say you'd finished.
	void PutDownBattleTool() {
		int back = s_ToolBeforeBattle >= 0 ? s_ToolBeforeBattle : ToolIndex(Tool::Command);
		s_ToolBeforeBattle = -1;
		TookTool(back);
	}

	/// Puts down a spawn zone at a place, or takes away the one there (within 40 px). At most 16 a team.
	void ToggleSpawnZone(BattleSettings& settings, const Vector& position) {
		Vector at = position;
		g_SceneMan.WrapPosition(at);
		auto near = std::find_if(settings.SpawnZones.begin(), settings.SpawnZones.end(), [&at](const Vector& zone) { return g_SceneMan.ShortestDistance(zone, at, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(40.0F); });
		if (near != settings.SpawnZones.end()) {
			settings.SpawnZones.erase(near);
		} else if (settings.SpawnZones.size() < 16) {
			settings.SpawnZones.push_back(at);
		}
	}

	/// On the map, while the Battle tab is showing or one of its tools is in hand: each defending team's place (its radius, and how far past
	/// it its units chase, fainter), each team's drop line, and its spawn zones.
	void DrawBattleMarks() {
		Tool kind = CurrentTool().Kind;
		if (!(Sandbox::IsOpen() && s_CurrentTab == "Battle") && !IsBattleTool(kind)) {
			return;
		}
		if (s_ModeSetup.Mode != BattleMode::Custom) {
			// (A mode draws its own: DrawBattleMode.)
			return;
		}
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		float scale = std::max(ScenePixelsPerWindowPixel(), 0.01F);
		for (int side = 0; side < c_Sides; ++side) {
			const BattleSettings& setup = s_BattleSetup[side];
			ImU32 color = c_SideColors[side];
			ImU32 faint = (color & 0x00FFFFFF) | (90u << 24);
			if (setup.Style == BattleStyle::Defend && setup.HasDefendPos) {
				ImVec2 middle = ToScreen(setup.DefendPos);
				drawList->AddCircle(middle, static_cast<float>(setup.DefendRadius) / scale, color, 48, 2.0F);
				drawList->AddCircle(middle, static_cast<float>(setup.DefendRadius + setup.ChaseDistance) / scale, faint, 64, 1.0F);
				drawList->AddLine(middle, ImVec2(middle.x, middle.y - 22.0F), IM_COL32(230, 230, 230, 220), 2.0F);
				drawList->AddRectFilled(ImVec2(middle.x, middle.y - 22.0F), ImVec2(middle.x + 12.0F, middle.y - 14.0F), color);
			}
			if (setup.DropOnLine && setup.HasLine) {
				ImVec2 from = ToScreen(setup.LineA);
				ImVec2 to = ToScreen(setup.LineB);
				drawList->AddLine(from, to, color, 3.0F);
				// Arrows down along it: where the ships come in.
				for (int i = 0; i <= 4; ++i) {
					float t = static_cast<float>(i) / 4.0F;
					ImVec2 at(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
					drawList->AddTriangleFilled(ImVec2(at.x - 5.0F, at.y - 12.0F), ImVec2(at.x + 5.0F, at.y - 12.0F), ImVec2(at.x, at.y - 4.0F), color);
				}
			}
			for (size_t i = 0; i < setup.SpawnZones.size(); ++i) {
				ImVec2 middle = ToScreen(setup.SpawnZones[i]);
				drawList->AddCircleFilled(middle, 14.0F, faint, 24);
				drawList->AddCircle(middle, 14.0F, color, 24, 2.0F);
				drawList->AddLine(ImVec2(middle.x - 7.0F, middle.y), ImVec2(middle.x + 7.0F, middle.y), color, 2.0F);
				drawList->AddLine(ImVec2(middle.x, middle.y - 7.0F), ImVec2(middle.x, middle.y + 7.0F), color, 2.0F);
			}
		}
	}
} // namespace SandboxDetail

void Sandbox::SetBattleTeam(int team, const std::string& factions, int style, int budget) {
	if (!s_CatalogueBuilt && InGame()) {
		BuildCatalogue();
	}
	if (team < 0 || team >= c_Sides) {
		return;
	}
	BattleSettings& settings = s_BattleTeams[team].Settings;
	settings.Active = budget >= 0;
	settings.EndlessMoney = budget == 0;
	if (budget > 0) {
		settings.Budget = budget;
	}
	settings.Factions = FactionsNamed(factions);
	settings.Style = static_cast<BattleStyle>(std::clamp(style, 0, static_cast<int>(BattleStyle::Count) - 1));
	ShowOnCard(team);
}

void Sandbox::SetBattleDrops(int team, int craft, int ships, int everySeconds, int waveSize, bool invincible) {
	if (team < 0 || team >= c_Sides) {
		return;
	}
	BattleSettings& settings = s_BattleTeams[team].Settings;
	settings.Craft = std::clamp(craft, 0, static_cast<int>(std::size(c_Crafts)) - 1);
	settings.ShipsPerBurst = std::clamp(ships, 0, 10);
	settings.EverySeconds = std::max(everySeconds, 1);
	settings.WaveSize = std::clamp(waveSize, 1, 20);
	settings.Invincible = invincible;
	ShowOnCard(team);
}

void Sandbox::SetBattleDefend(int team, const Vector& place, int radius, int chase) {
	if (team < 0 || team >= c_Sides) {
		return;
	}
	BattleSettings& settings = s_BattleTeams[team].Settings;
	settings.Style = BattleStyle::Defend;
	settings.DefendPos = place;
	g_SceneMan.WrapPosition(settings.DefendPos);
	settings.HasDefendPos = true;
	settings.DefendRadius = std::max(radius, 1);
	settings.ChaseDistance = std::max(chase, 0);
	ShowOnCard(team);
}

void Sandbox::StartBattle() {
	if (InGame()) {
		StartAllTeams();
	}
}

void Sandbox::StopBattle() {
	for (BattleTeam& team: s_BattleTeams) {
		team.Running = false;
	}
}

void Sandbox::SetAutoBattleSide(int team, const std::string& faction, int budget) {
	SetBattleTeam(team, faction, static_cast<int>(BattleStyle::Attack), budget > 0 ? budget : -1);
}

void Sandbox::SetAutoBattleRandom(bool random, bool favouritesOnly) {
	s_ScriptAnyFaction = random;
	s_ScriptFavourites = random && favouritesOnly;
}

void Sandbox::StartAutoBattle() {
	if (s_ScriptAnyFaction) {
		for (int side = 0; side < c_Sides; ++side) {
			s_BattleTeams[side].Settings.Factions.clear();
			s_BattleTeams[side].Settings.FavouritesOnly = s_ScriptFavourites;
			ShowOnCard(side);
		}
	}
	StartBattle();
}
