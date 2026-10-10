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
			// No crabs (ACrab: crabs, and the tanks and walkers built on them) unless the team's card says so: they can't climb most of what
			// the infantry can, and stood in the way of everyone else.
			// Nor animals or civilians (NC-1): a wave is soldiers.
			std::erase_if(pool, [](const Preset* unit) { return unit->NonCombatant; });
			if (!settings.Crabs) {
				std::erase_if(pool, [](const Preset* unit) { return unit->ClassName == "ACrab"; });
			}
			if (settings.JetpackOnly) {
				DropJetless(pool);
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
		/// The running mode's own zone (a hill, an assault objective) a place is the middle of, or nullptr.
		const std::vector<Vector>* ModeZoneAt(const Vector& centre) {
			if (!s_ModeRun.Running) {
				return nullptr;
			}
			const bool wraps = g_SceneMan.SceneWrapsX();
			for (const std::vector<Vector>& zone: s_ModeRun.Settings.Zones) {
				if (zone.size() < 3) {
					continue;
				}
				Vector middle;
				for (const Vector& corner: zone) {
					middle += corner;
				}
				middle *= 1.0F / static_cast<float>(zone.size());
				g_SceneMan.WrapPosition(middle);
				if (g_SceneMan.ShortestDistance(middle, centre, wraps).MagnitudeIsLessThan(2.0F)) {
					return &zone;
				}
			}
			return nullptr;
		}

		/// Whether a place (not yet wrapped) is in the ground.
		bool GroundAt(Vector at) {
			g_SceneMan.WrapPosition(at);
			return g_SceneMan.GetTerrMatter(at.GetFloorIntX(), at.GetFloorIntY()) != g_MaterialAir;
		}

		/// Where a unit's feet go on a floor inside a zone, with headroom above them in it too (so its middle, which is what counts as in
		/// the zone, is): a place picked at random inside it, up out of the ground and down onto the floor. False with none found.
		bool FloorInZone(const std::vector<Vector>& zone, float headroom, Vector& feet) {
			float left = zone[0].m_X, right = zone[0].m_X, top = zone[0].m_Y, bottom = zone[0].m_Y;
			for (const Vector& corner: zone) {
				left = std::min(left, corner.m_X);
				right = std::max(right, corner.m_X);
				top = std::min(top, corner.m_Y);
				bottom = std::max(bottom, corner.m_Y);
			}
			const Vector step(0.0F, 2.0F);
			for (int tries = 0; tries < 60; ++tries) {
				Vector at(left + Random01() * (right - left), top + Random01() * (bottom - top));
				if (!IsInZone(zone, at)) {
					continue;
				}
				while (IsInZone(zone, at) && GroundAt(at)) {
					at -= step;
				}
				while (IsInZone(zone, at + step) && !GroundAt(at + step)) {
					at += step;
				}
				// (No floor under it inside the zone, or no room above it: the zone's bottom edge in the air, or a floor up against its top.)
				if (!IsInZone(zone, at) || !GroundAt(at + step) || !IsInZone(zone, at - Vector(0.0F, headroom))) {
					continue;
				}
				g_SceneMan.WrapPosition(at);
				feet = at;
				return true;
			}
			return false;
		}

		/// A post somewhere on the ground inside a defended place's radius. For a mode's own zone, on a floor inside the zone: from its
		/// middle straight down, an objective drawn round an upper storey (with a gap in its floor there) had its units stand on the storey
		/// below, out of it, thinking they were taking it.
		Vector PostAround(const Vector& centre, float radius) {
			Vector feet;
			if (const std::vector<Vector>* zone = ModeZoneAt(centre); zone && (FloorInZone(*zone, 24.0F, feet) || FloorInZone(*zone, 0.0F, feet))) {
				return feet;
			}
			Vector around = centre + Vector((Random01() * 2.0F - 1.0F) * radius * 0.6F, 0.0F);
			g_SceneMan.WrapPosition(around);
			std::vector<Vector> spots = StandingSpots(around, 1);
			return spots.empty() ? centre : spots.front();
		}

		Vector PostIn(const BattleSettings& settings) { return PostAround(settings.DefendPos, static_cast<float>(std::max(settings.DefendRadius, 1))); }

		/// The place, radius and chase distance a defender goes by: its team card's as they are now, so a change on the card reaches the
		/// units already in (they kept what the card said when they were bought: chase distance lowered, they still chased as far as before).
		void FollowCard(BattleDefender& defender) {
			// (One a player told to defend goes by its own order's zone: CommandDefender.)
			if (defender.Commanded || defender.Team < 0 || defender.Team >= c_Sides || !Defends(s_BattleTeams[defender.Team].Settings)) {
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

		/// Whether a place is inside a spawn zone (its corners as stored, so the place is to be on the same side of a wrap as the first).
		bool InsideZone(const std::vector<Vector>& zone, const Vector& at) {
			bool inside = false;
			for (size_t i = 0, j = zone.size() - 1; i < zone.size(); j = i++) {
				const Vector& a = zone[i];
				const Vector& b = zone[j];
				if ((a.m_Y > at.m_Y) != (b.m_Y > at.m_Y) && at.m_X < (b.m_X - a.m_X) * (at.m_Y - a.m_Y) / (b.m_Y - a.m_Y) + a.m_X) {
					inside = !inside;
				}
			}
			return inside;
		}

		/// Whether a place (not yet wrapped) is open air.
		bool AirAt(Vector at) {
			g_SceneMan.WrapPosition(at);
			return g_SceneMan.GetTerrMatter(at.GetFloorIntX(), at.GetFloorIntY()) == g_MaterialAir;
		}

		/// Where a unit (this tall) appears in a spawn zone: a place picked at random inside it, then down onto the ground if there is any
		/// below in the zone (or up out of it, if the place is in it), so units stand on whatever ground the zone takes in. A zone drawn
		/// in the open sky lets them fall from where they appear.
		Vector ZoneSpawnSpot(const std::vector<Vector>& zone, float height) {
			float left = zone[0].m_X, right = zone[0].m_X, top = zone[0].m_Y, bottom = zone[0].m_Y;
			Vector middle;
			for (const Vector& corner: zone) {
				left = std::min(left, corner.m_X);
				right = std::max(right, corner.m_X);
				top = std::min(top, corner.m_Y);
				bottom = std::max(bottom, corner.m_Y);
				middle += corner / static_cast<float>(zone.size());
			}
			const Vector down(0.0F, 1.0F);
			for (int tries = 0; tries < 60; ++tries) {
				Vector at(left + Random01() * (right - left), top + Random01() * (bottom - top));
				if (!InsideZone(zone, at)) {
					continue;
				}
				while (InsideZone(zone, at) && !AirAt(at)) {
					at -= down;
				}
				if (!InsideZone(zone, at)) {
					continue; // (Ground all the way up to the zone's edge.)
				}
				while (InsideZone(zone, at + down) && AirAt(at + down)) {
					at += down;
				}
				Vector spot = at - Vector(0.0F, height * 0.5F);
				g_SceneMan.WrapPosition(spot);
				return spot;
			}
			// A zone that's all ground (or too thin to land a pick in): its middle, lifted out of the ground.
			int up = 0;
			while (up < 400 && !AirAt(middle - Vector(0.0F, static_cast<float>(up)))) {
				++up;
			}
			Vector spot = middle - Vector(0.0F, static_cast<float>(up) + height * 0.5F);
			g_SceneMan.WrapPosition(spot);
			return spot;
		}
	} // namespace

	/// Gives a unit just bought a post in a place it defends, as a defending team's are given theirs (a mode's guards, by settings made for
	/// them).
	void MakeDefender(Actor* unit, const BattleSettings& settings) { DefendPlace(unit, settings); }

	/// Moves the place a team's defenders defend (a mode's guards going after their flag, or its units on to the next hill), each given a
	/// post in the new one: right at it with atIt (to pick up what lies there), else somewhere on the ground inside their radius (a new one,
	/// with chase distance to match, when radius is given).
	void RecentreDefenders(int team, const Vector& centre, bool atIt, float radius) {
		const bool wraps = g_SceneMan.SceneWrapsX();
		for (auto& [id, defender]: s_BattleDefenders) {
			if (defender.Commanded || defender.Team != team || g_SceneMan.ShortestDistance(defender.Center, centre, wraps).MagnitudeIsLessThan(1.0F)) {
				continue;
			}
			MoveDefender(defender, centre, atIt, radius);
		}
	}

	/// Moves one defender's place (RecentreDefenders, a team commander's split), as RecentreDefenders says.
	void MoveDefender(BattleDefender& defender, const Vector& centre, bool atIt, float radius) {
		if (radius >= 0.0F) {
			defender.Chase = std::max(defender.Chase - defender.Radius, 0.0F) + radius;
			defender.Radius = radius;
		}
		defender.Center = centre;
		defender.IdleSince = -1;
		// (Sent there on the next update, not at the defenders' next half-second turn: the hill moving or an objective falling had them
		// carry on to the old one for up to half a second.)
		s_DefendersMoved = true;
		if (atIt) {
			defender.Post = centre;
		} else {
			defender.Post = PostAround(centre, defender.Radius);
		}
	}

	/// Makes a unit a player has told to defend a place (Defend at, defend where it stands, guard something) a defender of it as a Battle
	/// Director team's are (UpdateBattleDefenders): it holds its post, goes after enemies that come within the zone's radius and chase
	/// distance, and comes back after. The zone is the command row's as it is now. The unit is already on its way to its post (SendUnit),
	/// which drops any defending it was doing before, so this comes after.
	void CommandDefender(Actor* unit, const Vector& centre, const Vector& post) {
		if (!unit || dynamic_cast<const ACraft*>(unit)) {
			return;
		}
		BattleDefender& defender = s_BattleDefenders[unit->GetUniqueID()];
		defender = BattleDefender();
		defender.Commanded = true;
		defender.Team = unit->GetTeam();
		defender.Center = centre;
		defender.Radius = static_cast<float>(std::max(s_DefendRadius, 1));
		defender.Chase = static_cast<float>(std::max(s_DefendChase, 0));
		defender.Post = post;
		defender.Made = g_TimerMan.GetSimUpdateCount();
		defender.RoamRoll = Random01();
		defender.Roams = defender.RoamRoll * 100.0F < static_cast<float>(s_DefendRoam);
	}

	/// Moves a commanded defender's zone along with what it guards (UpdateGuards): its middle and its post, the radius and chase kept.
	void MoveCommandedZone(Actor* unit, const Vector& centre, const Vector& post) {
		if (auto defender = unit ? s_BattleDefenders.find(unit->GetUniqueID()) : s_BattleDefenders.end(); defender != s_BattleDefenders.end() && defender->second.Commanded) {
			defender->second.Center = centre;
			defender->second.Post = post;
			defender->second.IdleSince = -1;
		}
	}

	/// A defend zone on the map, as the Battle Director's cards draw theirs: the radius in the colour, the chase distance past it faint.
	void DrawDefendZone(ImDrawList* drawList, const Vector& centre, float radius, float chase, ImU32 color) {
		const float scale = std::max(ScenePixelsPerWindowPixel(), 0.01F);
		const float pixel = ToolUI::Pixel();
		const ImU32 faint = (color & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT);
		const ImVec2 middle = ToScreen(centre);
		drawList->AddCircle(middle, std::max(radius, 1.0F) / scale, color, 48, pixel * 1.5F);
		if (chase > 0.0F) {
			drawList->AddCircle(middle, (radius + chase) / scale, faint, 64, pixel);
		}
	}

	/// The zones the selected units were told to defend, each drawn once however many hold it.
	void DrawCommandedZones(ImDrawList* drawList) {
		std::vector<const BattleDefender*> drawn;
		const ImU32 amber = c_CommandModeColors[static_cast<int>(CommandMode::DefendAt)];
		for (const UnitRef& ref: s_Selected) {
			const Actor* unit = GetRef(ref);
			auto found = unit ? s_BattleDefenders.find(unit->GetUniqueID()) : s_BattleDefenders.end();
			if (found == s_BattleDefenders.end() || !found->second.Commanded) {
				continue;
			}
			const BattleDefender& zone = found->second;
			if (std::any_of(drawn.begin(), drawn.end(), [&zone](const BattleDefender* other) { return other->Radius == zone.Radius && other->Chase == zone.Chase && g_SceneMan.ShortestDistance(other->Center, zone.Center, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(1.0F); })) {
				continue;
			}
			drawn.push_back(&zone);
			DrawDefendZone(drawList, zone.Center, zone.Radius, zone.Chase, amber);
		}
		// And the places they were told to suppress.
		const ImU32 violet = c_CommandModeColors[static_cast<int>(CommandMode::Suppress)];
		for (const UnitRef& ref: s_Selected) {
			Vector centre;
			float radius = 0.0F;
			if (SuppressZoneOf(GetRef(ref), centre, radius)) {
				DrawDefendZone(drawList, centre, radius, 0.0F, violet);
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
			if (IsSoldier(actor) && !dynamic_cast<const ACraft*>(actor) && !actor->IsIgnoredByAI()) {
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
					// (A player's defenders don't go after what their side can't see, in commander mode: RC-9.)
					if (defender.Commanded && HiddenFromCommander(other)) {
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
		if (now % 30 == 0 || s_DefendersMoved) {
			s_DefendersMoved = false;
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
			if (s_ModeRun.Running) {
				// (A mode may hold it to fewer: last team standing's units left to send.)
				room = ModeRoom(side, room);
			}
			if (room <= 0) {
				team.FillFirst = false;
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
			const bool playerTeam = BattlePlayerCommands(side);
			const Order order = playerTeam || Defends(settings) ? Order::Hold : OrderOf(settings.Style);
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
			// A ship's wave spends at most 180 a unit (the 900 a wave of five always had), unless money is no object; a spawn zone's may spend
			// whatever is left, so it puts down as many as it was asked for while the money lasts (held to 180 a unit, it was asked for three
			// and put down one or two, and with dear units often none at all past the cheapest).
			auto buyWave = [&](int size, bool wholeBudget) {
				std::vector<Actor*> wave;
				float left = settings.EndlessMoney ? std::numeric_limits<float>::max() : static_cast<float>(settings.Budget) - team.Spent;
				float waveBudget = wholeBudget || settings.EndlessMoney ? left : std::min(left, std::max(180.0F * static_cast<float>(size), cheapest));
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
				} else if (Defends(settings) && !playerTeam) {
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
				// (Filling the team at once: as many ships as it takes, up to ten, each as full as need be.)
				const int ships = team.FillFirst && room < std::numeric_limits<int>::max() ? std::clamp((room + waveSize - 1) / waveSize, 1, 10) : std::clamp(settings.ShipsPerBurst, 1, 10);
				const int shipSize = team.FillFirst && room < std::numeric_limits<int>::max() ? std::clamp((room + ships - 1) / ships, 1, 20) : waveSize;
				for (int ship = 0; ship < ships && room > 0 && !team.Broke; ++ship) {
					std::vector<Actor*> wave = buyWave(std::min(shipSize, room), false);
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
				std::vector<std::vector<Vector>> zones = settings.SpawnZones;
				for (size_t i = 0; i + 1 < zones.size(); ++i) {
					std::swap(zones[i], zones[i + std::min(zones.size() - i - 1, static_cast<size_t>(Random01() * static_cast<float>(zones.size() - i)))]);
				}
				// (Filling the team at once: its whole limit shared between the zones.)
				const int usable = static_cast<int>(std::count_if(zones.begin(), zones.end(), [](const std::vector<Vector>& zone) { return zone.size() >= 3; }));
				const int perZone = team.FillFirst && room < std::numeric_limits<int>::max() ? std::max((room + std::max(usable, 1) - 1) / std::max(usable, 1), 1) : std::clamp(settings.ZoneUnits, 1, 20);
				for (const std::vector<Vector>& zone: zones) {
					if (zone.size() < 3) {
						continue;
					}
					if (room <= 0 || team.Broke) {
						break;
					}
					std::vector<Actor*> wave = buyWave(std::min(perZone, room), true);
					if (wave.empty()) {
						break;
					}
					// Each somewhere inside the zone, on its ground.
					ActivateSide(side);
					for (Actor* unit: wave) {
						team.Spent += unit->GetTotalValue(unit->GetModuleID(), 1.0F);
						unit->SetPos(s_ModeRun.Running ? ModeSpawnSpot(side, zone, unit) : ZoneSpawnSpot(zone, unit->GetHeight()));
						g_MovableMan.AddActor(unit);
					}
					team.Sent += static_cast<int>(wave.size());
					room -= static_cast<int>(wave.size());
				}
			}
			team.FillFirst = false;
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
		if (stroke.Kind == Tool::BattleModePoint || IsModeZoneTool(stroke.Kind) || stroke.Kind == Tool::BattleModeFlag || (stroke.Kind == Tool::BattleTeam && (stroke.Count == BattleModeSet || stroke.Count == BattleModeStart || stroke.Count == BattleModeStop))) {
			ApplyBattleMode(stroke);
			return;
		}
		if (IsBattleTool(stroke.Kind)) {
			// From a script's SandboxDo: the window sends the whole settings instead.
			if (oneTeam) {
				BattleSettings& settings = s_BattleTeams[side].Settings;
				if (stroke.Kind == Tool::BattleSpawnZone) {
					// A corner of a zone: a script puts one down per call, and closes the zone with one on its first corner.
					AddZoneCorner(s_BattleTeams[side].ZoneDraft, settings, stroke.Position, 20.0F);
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
			team.ZoneDraft.clear();
			team.NextZoneWave = 0;
		}
		for (BattleSettings& setup: s_BattleSetup) {
			setup.HasDefendPos = false;
			setup.HasLine = false;
			setup.SpawnZones.clear();
		}
		s_ZoneDraft.clear();
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
		changed |= ToolUI::Checkbox("Spawn crabs", &setup.Crabs);
		ImGui::SetItemTooltip("Crabs among this team's units: crabs, and the tanks and walkers built on them. Off: infantry and drones only.");
		ImGui::SameLine();
		changed |= ToolUI::Checkbox("Jetpacks only", &setup.JetpackOnly);
		ImGui::SetItemTooltip("Only units whose jetpack really flies them (lifts them 5 m or more). Units without one, or with one that only gives a hop, aren't sent.");
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
			if (ToolUI::Button(zoning ? "Done##zones" : "Draw spawn zones")) {
				if (zoning) {
					PutDownBattleTool();
				} else {
					TakeBattleTool(Tool::BattleSpawnZone, side);
				}
			}
			ImGui::SetItemTooltip(zoning ? "Click the corners of a zone on the map, then click the first corner again (or press Enter) to close it. Backspace takes back the last corner. Enter with no zone part drawn, or this, when done." : "Then click out the corners of each area this team's units are to appear in, as many areas as you like.");
			ImGui::SameLine();
			if (setup.SpawnZones.empty()) {
				ImGui::TextDisabled("none");
			} else {
				ImGui::TextDisabled("%d drawn", static_cast<int>(setup.SpawnZones.size()));
				ImGui::SameLine();
				if (ToolUI::Button("Undo##zones")) {
					setup.SpawnZones.pop_back();
					changed = true;
				}
				ImGui::SetItemTooltip("Takes away the last spawn zone drawn.");
				ImGui::SameLine();
				if (ToolUI::Button("Clear##zones")) {
					setup.SpawnZones.clear();
					changed = true;
				}
				ImGui::SetItemTooltip("Takes away all of this team's spawn zones.");
			}
			if (!setup.SpawnZones.empty()) {
				changed |= ImGui::SliderInt("Units per zone", &setup.ZoneUnits, 1, 10);
				ImGui::SetItemTooltip("How many units appear in each zone each time, anywhere inside it (fewer when the budget or the unit limit runs short).");
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
		BattlePresetsPanel();
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
		s_ZoneDraft.clear();
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
		s_ZoneDraft.clear();
		int back = s_ToolBeforeBattle >= 0 ? s_ToolBeforeBattle : ToolIndex(Tool::Command);
		s_ToolBeforeBattle = -1;
		TookTool(back);
	}

	/// Puts down the next corner of a spawn zone being drawn. One on (within closeWithin of) the first corner, with at least three down,
	/// closes the zone instead (CloseSpawnZone): true then. The corners are kept next to the first, not wrapped, so a zone can cross a
	/// wrapping map's seam. At most 32 corners.
	bool AddZoneCorner(std::vector<Vector>& draft, BattleSettings& settings, const Vector& position, float closeWithin) {
		Vector at = position;
		g_SceneMan.WrapPosition(at);
		if (draft.empty()) {
			draft.push_back(at);
			return false;
		}
		Vector fromFirst = g_SceneMan.ShortestDistance(draft.front(), at, g_SceneMan.SceneWrapsX());
		if (draft.size() >= 3 && fromFirst.MagnitudeIsLessThan(closeWithin)) {
			return CloseSpawnZone(draft, settings);
		}
		if (draft.size() < 32) {
			draft.push_back(draft.front() + fromFirst);
		}
		return false;
	}

	/// The spawn zone being drawn made one of the team's zones, if it has three corners or more (at most 16 zones a team). The draft is
	/// emptied either way.
	bool CloseSpawnZone(std::vector<Vector>& draft, BattleSettings& settings) {
		bool made = draft.size() >= 3 && settings.SpawnZones.size() < 16;
		if (made) {
			settings.SpawnZones.push_back(draft);
		}
		draft.clear();
		return made;
	}

	/// How near the first corner of the spawn zone being drawn a click closes it: 12 window pixels, in the scene.
	float ZoneCloseDistance() { return 12.0F * std::max(ScenePixelsPerWindowPixel(), 0.01F); }

	/// A spawn zone's corners on the screen, each placed from the first, so a zone across a wrapping map's seam is drawn whole.
	std::vector<ImVec2> ZoneOnScreen(const std::vector<Vector>& zone, float scale) {
		std::vector<ImVec2> corners;
		ImVec2 first = ToScreen(zone.front());
		for (const Vector& corner: zone) {
			corners.emplace_back(first.x + (corner.m_X - zone.front().m_X) / scale, first.y + (corner.m_Y - zone.front().m_Y) / scale);
		}
		return corners;
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
			// Each zone: its area, shaded.
			for (const std::vector<Vector>& zone: setup.SpawnZones) {
				std::vector<ImVec2> corners = ZoneOnScreen(zone, scale);
				drawList->AddConcavePolyFilled(corners.data(), static_cast<int>(corners.size()), (color & 0x00FFFFFF) | (50u << 24));
				drawList->AddPolyline(corners.data(), static_cast<int>(corners.size()), color, ImDrawFlags_Closed, 2.0F);
			}
		}
		if (kind == Tool::BattleSpawnZone) {
			DrawZoneDraft(drawList, scale);
		}
	}

	/// The zone being drawn (a spawn zone, or a mode's base): its corners so far and on to the pointer, and its first corner ringed (filled
	/// when the pointer is close enough for a click to close it).
	void DrawZoneDraft(ImDrawList* drawList, float scale) {
		if (s_ZoneDraft.empty()) {
			return;
		}
		const ImU32 color = c_SideColors[std::clamp(s_BattleEditTeam, 0, c_Sides - 1)];
		std::vector<ImVec2> corners = ZoneOnScreen(s_ZoneDraft, scale);
		ImVec2 first = corners.front();
		if (Sandbox::CapturesWorldClicks()) {
			Vector pointer = MouseScenePosition();
			Vector fromFirst = g_SceneMan.ShortestDistance(s_ZoneDraft.front(), pointer, g_SceneMan.SceneWrapsX());
			corners.emplace_back(first.x + fromFirst.m_X / scale, first.y + fromFirst.m_Y / scale);
			if (s_ZoneDraft.size() >= 3 && fromFirst.MagnitudeIsLessThan(ZoneCloseDistance())) {
				drawList->AddCircleFilled(first, 7.0F, color, 16);
			}
		}
		drawList->AddPolyline(corners.data(), static_cast<int>(corners.size()), color, 0, 2.0F);
		drawList->AddCircle(first, 7.0F, color, 16, 2.0F);
		for (size_t i = 1; i < s_ZoneDraft.size(); ++i) {
			drawList->AddCircleFilled(corners[i], 3.0F, color, 8);
		}
	}

	bool IsInZone(const std::vector<Vector>& zone, const Vector& at) {
		if (zone.size() < 3) {
			return false;
		}
		// (On the same side of a wrap as the first corner, as the corners are.)
		return InsideZone(zone, zone.front() + g_SceneMan.ShortestDistance(zone.front(), at, g_SceneMan.SceneWrapsX()));
	}

	Vector SpotInZone(const std::vector<Vector>& zone, float height) { return ZoneSpawnSpot(zone, height); }
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
