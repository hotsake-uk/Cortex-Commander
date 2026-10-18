// The battle's pickups: with Pickups ticked on the Battle tab, powerups (Base.rte/Devices/Powerups, and the Medieval spell scrolls) come in on
// the map now and then while a mode's game is on: anywhere on open ground away from the spawn zones, at spots placed, or in pickup zones
// drawn. The first unit of a team in the game to reach one takes it into its inventory, to use from its menu's Quick actions (and the AI's
// own units use theirs, ActorPowerups.cpp). A pickup nobody takes goes after a while. The AI's teams send the unit nearest each to fetch it.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		/// A kind of pickup the Battle tab can tick (PickupKinds' bits, in this order).
		struct PickupKind {
			const char* Name;
			const char* Preset; //!< The Base.rte powerup, or none: one of Group's, picked at random.
			const char* Group;
			char Glyph; //!< The letter on its crate.
			ImU32 Color;
		};
		constexpr PickupKind c_PickupKinds[] = {
		    {"Energy shield", "Energy Shield", nullptr, 'S', IM_COL32(90, 180, 255, 255)},
		    {"Stim", "Stim", nullptr, 'T', IM_COL32(120, 230, 90, 255)},
		    {"Overcharge", "Overcharge", nullptr, 'O', IM_COL32(255, 160, 40, 255)},
		    {"Cloak", "Cloak", nullptr, 'C', IM_COL32(190, 140, 255, 255)},
		    {"Medkit", "Medkit", nullptr, '+', IM_COL32(240, 80, 80, 255)},
		    {"Juggernaut", "Juggernaut", nullptr, 'J', IM_COL32(255, 215, 60, 255)},
		    {"Spell scroll", nullptr, "Spell Scrolls", '*', IM_COL32(235, 205, 145, 255)},
		};
		constexpr int c_PickupKindCount = static_cast<int>(std::size(c_PickupKinds));
		constexpr ImU32 c_PickupPlaceColor = IM_COL32(120, 230, 90, 255); //!< The spots and zones placed.
		constexpr float c_PickupReach = 40.0F; //!< How near a unit's middle has to come to a pickup (its foot) to take it.
		constexpr float c_ClearOfSpawns = 300.0F; //!< How far from every spawn zone a pickup coming in anywhere has to be.
		constexpr float c_ClearOfPickups = 200.0F; //!< And from the other pickups.

		/// A pickup lying on the map.
		struct Pickup {
			long Id = 0;
			int Kind = 0;
			std::string Preset;
			int ModuleID = -1;
			std::string Name; //!< What it's called on the map: the kind's name, or the scroll's.
			Vector Pos; //!< Its foot, on the ground.
			long long Born = 0; //!< The sim update it came in on.
		};

		/// A unit an AI team sent to fetch a pickup.
		struct Fetch {
			long PickupId = 0;
			long long Since = 0;
		};

		std::vector<Pickup> s_Pickups;
		std::unordered_map<long, Fetch> s_Fetchers; //!< By the unit's unique ID.
		long long s_NextPickupAt = -1; //!< The sim update the next pickup comes in on (as soon as there's room for it), or -1 when not yet set.
		long s_PickupIds = 0;
		std::vector<Vector> s_ScriptPickupDraft; //!< The corners of a pickup zone a script is putting down, one SandboxDo at a time.
		std::vector<std::pair<std::string, int>> s_Scrolls; //!< The spell scrolls loaded (preset name, module), read once.
		bool s_ScrollsRead = false;

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		const std::vector<std::pair<std::string, int>>& Scrolls() {
			if (!s_ScrollsRead) {
				s_ScrollsRead = true;
				std::list<Entity*> found;
				g_PresetMan.GetAllOfGroup(found, "Spell Scrolls", "HeldDevice");
				for (const Entity* entity: found) {
					if (const HeldDevice* device = dynamic_cast<const HeldDevice*>(entity); device && device->IsPowerup()) {
						s_Scrolls.emplace_back(device->GetPresetName(), device->GetModuleID());
					}
				}
			}
			return s_Scrolls;
		}

		/// Whether a kind can come in: its powerup (or a scroll) is loaded.
		bool KindLoaded(int kind) {
			const PickupKind& info = c_PickupKinds[kind];
			return info.Preset ? g_PresetMan.GetEntityPreset("HeldDevice", info.Preset, "Base.rte") != nullptr : !Scrolls().empty();
		}

		/// Fills in what a pickup of a kind is: its powerup, a scroll picked at random. False when it isn't loaded.
		bool MakeKind(int kind, Pickup& pickup) {
			const PickupKind& info = c_PickupKinds[kind];
			pickup.Kind = kind;
			if (info.Preset) {
				const Entity* preset = g_PresetMan.GetEntityPreset("HeldDevice", info.Preset, "Base.rte");
				if (!preset) {
					return false;
				}
				pickup.Preset = info.Preset;
				pickup.ModuleID = preset->GetModuleID();
				pickup.Name = info.Name;
				return true;
			}
			const auto& scrolls = Scrolls();
			if (scrolls.empty()) {
				return false;
			}
			const auto& [name, module] = scrolls[std::min(static_cast<size_t>(RandomNum(0.0F, 1.0F) * static_cast<float>(scrolls.size())), scrolls.size() - 1)];
			pickup.Preset = name;
			pickup.ModuleID = module;
			pickup.Name = name;
			return true;
		}

		/// The ground under a place in the open air, with room over it for a unit to stand: false when the place is in the ground, or there's
		/// nothing under it (off the map, or a bottomless drop).
		bool GroundUnder(Vector at, Vector& ground, float headroom) {
			g_SceneMan.WrapPosition(at);
			const int width = g_SceneMan.GetSceneWidth();
			const int height = g_SceneMan.GetSceneHeight();
			const int x = at.GetFloorIntX();
			int y = std::max(at.GetFloorIntY(), 0);
			if (x < 0 || x >= width || y >= height - 1 || g_SceneMan.GetTerrMatter(x, y) != g_MaterialAir) {
				return false;
			}
			while (y < height - 1 && g_SceneMan.GetTerrMatter(x, y + 1) == g_MaterialAir) {
				++y;
			}
			if (y >= height - 1) {
				return false;
			}
			for (int up = 1; up <= static_cast<int>(headroom); ++up) {
				if (y - up < 0 || g_SceneMan.GetTerrMatter(x, y - up) != g_MaterialAir) {
					return false;
				}
			}
			ground = Vector(static_cast<float>(x), static_cast<float>(y));
			return true;
		}

		bool NearAPickup(const Vector& at, float within) {
			const bool wraps = g_SceneMan.SceneWrapsX();
			return std::any_of(s_Pickups.begin(), s_Pickups.end(), [&](const Pickup& pickup) { return g_SceneMan.ShortestDistance(pickup.Pos, at, wraps).MagnitudeIsLessThan(within); });
		}

		/// Whether a place is in, near or over one of the spawn zones of the teams in the game: no pickup comes in there by chance, for a team
		/// to have it at once.
		bool NearASpawn(const Vector& at) {
			const bool wraps = g_SceneMan.SceneWrapsX();
			for (int side = 0; side < c_Sides; ++side) {
				if (!BattleTeamIn(side)) {
					continue;
				}
				for (const std::vector<Vector>& zone: BattleSpawnZonesOf(s_ModeRun.Settings, side)) {
					// (In the zone's own plane: its corners are kept next to its first, across a wrap seam.)
					const Vector local = zone.front() + g_SceneMan.ShortestDistance(zone.front(), at, wraps);
					if (BattleGapToZone(zone, local) < c_ClearOfSpawns) {
						return true;
					}
					float left = local.m_X;
					float right = local.m_X;
					float bottom = local.m_Y;
					for (const Vector& corner: zone) {
						left = std::min(left, corner.m_X);
						right = std::max(right, corner.m_X);
						bottom = std::max(bottom, corner.m_Y);
					}
					if (local.m_X >= left && local.m_X <= right && local.m_Y <= bottom) {
						// (Over it: it would fall in, or be the first thing its units see.)
						return true;
					}
				}
			}
			return false;
		}

		/// Somewhere on open ground anywhere on the map, away from the spawn zones and the other pickups. False when none is found this time.
		bool SpotAnywhere(Vector& spot) {
			const float width = static_cast<float>(g_SceneMan.GetSceneWidth());
			const float height = static_cast<float>(g_SceneMan.GetSceneHeight());
			for (int tries = 0; tries < 80; ++tries) {
				Vector ground;
				float tryX = RandomNum(0.0F, width - 1.0F);
				float tryY = RandomNum(0.0F, height - 1.0F);
				if (GroundUnder(Vector(tryX, tryY), ground, 40.0F) && !NearASpawn(ground) && !NearAPickup(ground, c_ClearOfPickups)) {
					spot = ground;
					return true;
				}
			}
			return false;
		}

		/// One of the spots placed with nothing on it, on the ground under it.
		bool SpotAtSpots(Vector& spot) {
			std::vector<Vector> free;
			for (const Vector& placed: s_ModeRun.Settings.PickupSpots) {
				Vector ground;
				if (GroundUnder(OpenAirAt(placed), ground, 0.0F) && !NearAPickup(ground, c_PickupReach)) {
					free.push_back(ground);
				}
			}
			if (free.empty()) {
				return false;
			}
			spot = free[std::min(static_cast<size_t>(RandomNum(0.0F, 1.0F) * static_cast<float>(free.size())), free.size() - 1)];
			return true;
		}

		/// Somewhere on the ground in one of the pickup zones, not on another pickup.
		bool SpotInZones(Vector& spot) {
			std::vector<const std::vector<Vector>*> zones;
			for (const std::vector<Vector>& zone: s_ModeRun.Settings.PickupZones) {
				if (zone.size() >= 3) {
					zones.push_back(&zone);
				}
			}
			for (int tries = 0; tries < 8 && !zones.empty(); ++tries) {
				const Vector at = SpotInZone(*zones[std::min(static_cast<size_t>(RandomNum(0.0F, 1.0F) * static_cast<float>(zones.size())), zones.size() - 1)], 0.0F);
				if (!NearAPickup(at, c_PickupReach * 2.0F)) {
					spot = at;
					return true;
				}
			}
			return false;
		}

		/// Where the next pickup comes in: at one of the places ticked, picked at random (anywhere, with none ticked or placed).
		bool NextSpot(Vector& spot) {
			const BattleModeSettings& settings = s_ModeRun.Settings;
			std::vector<int> places;
			if ((settings.PickupPlaces & PickupAtSpots) && !settings.PickupSpots.empty()) {
				places.push_back(PickupAtSpots);
			}
			if ((settings.PickupPlaces & PickupInZones) && std::any_of(settings.PickupZones.begin(), settings.PickupZones.end(), [](const std::vector<Vector>& zone) { return zone.size() >= 3; })) {
				places.push_back(PickupInZones);
			}
			if ((settings.PickupPlaces & PickupAnywhere) || places.empty()) {
				places.push_back(PickupAnywhere);
			}
			const int place = places[std::min(static_cast<size_t>(RandomNum(0.0F, 1.0F) * static_cast<float>(places.size())), places.size() - 1)];
			return place == PickupAtSpots ? SpotAtSpots(spot) : place == PickupInZones ? SpotInZones(spot) : SpotAnywhere(spot);
		}

		/// A new pickup of one of the kinds ticked, if there's somewhere for it.
		bool SpawnPickup(long long now) {
			std::vector<int> kinds;
			for (int kind = 0; kind < c_PickupKindCount; ++kind) {
				if ((s_ModeRun.Settings.PickupKinds & (1 << kind)) && KindLoaded(kind)) {
					kinds.push_back(kind);
				}
			}
			Pickup pickup;
			if (kinds.empty() || !MakeKind(kinds[std::min(static_cast<size_t>(RandomNum(0.0F, 1.0F) * static_cast<float>(kinds.size())), kinds.size() - 1)], pickup) || !NextSpot(pickup.Pos)) {
				return false;
			}
			pickup.Id = ++s_PickupIds;
			pickup.Born = now;
			s_Pickups.push_back(pickup);
			AddAlert("pickup", pickup.Name + " pickup on the map", pickup.Pos, c_PickupKinds[pickup.Kind].Color);
			g_ConsoleMan.PrintString("BATTLE: " + pickup.Name + " pickup on the map");
			return true;
		}

		/// Whether a unit can take a pickup: one of a team in the game, that fights and can carry it (a crab can't read a scroll).
		bool CanTake(const Actor* unit, const Pickup& pickup) {
			if (!IsCombatant(unit) || unit->IsBeast() || !BattleTeamIn(unit->GetTeam())) {
				return false;
			}
			return c_PickupKinds[pickup.Kind].Preset || dynamic_cast<const AHuman*>(unit);
		}

		/// Each pickup a unit has reached goes into its inventory.
		void TakePickups() {
			if (s_Pickups.empty()) {
				return;
			}
			const bool wraps = g_SceneMan.SceneWrapsX();
			const std::vector<Actor*> fighters = BattleFighters();
			for (auto pickup = s_Pickups.begin(); pickup != s_Pickups.end();) {
				const Vector crate = pickup->Pos - Vector(0.0F, 8.0F);
				Actor* taker = nullptr;
				for (Actor* unit: fighters) {
					if (CanTake(unit, *pickup) && g_SceneMan.ShortestDistance(unit->GetPos(), crate, wraps).MagnitudeIsLessThan(std::max(c_PickupReach, unit->GetHeight() * 0.5F))) {
						taker = unit;
						break;
					}
				}
				if (!taker) {
					++pickup;
					continue;
				}
				if (MovableObject* item = CreateObject("HeldDevice", pickup->Preset, pickup->ModuleID)) {
					if (taker->AddToInventoryBack(item)) {
						const int side = taker->GetTeam();
						const std::string text = std::string(c_SideNames[side]) + " took the " + pickup->Name;
						AddAlert("pickup", text, pickup->Pos, c_SideColors[side]);
						g_ConsoleMan.PrintString("BATTLE: " + text);
					} else {
						delete item;
					}
				}
				pickup = s_Pickups.erase(pickup);
			}
		}

		/// The AI's teams each send the unit nearest a pickup (within a reach that grows with how hard the commanders play) to fetch it, and
		/// those back to their job in the battle once it's taken, gone, or they've been at it too long.
		void UpdateFetchers(long long now) {
			const float perSecond = UpdatesPerSecond();
			const bool wraps = g_SceneMan.SceneWrapsX();
			for (auto entry = s_Fetchers.begin(); entry != s_Fetchers.end();) {
				Actor* unit = dynamic_cast<Actor*>(g_MovableMan.FindObjectByUniqueID(entry->first));
				const bool there = std::any_of(s_Pickups.begin(), s_Pickups.end(), [&entry](const Pickup& pickup) { return pickup.Id == entry->second.PickupId; });
				if (!unit || !IsCombatant(unit)) {
					entry = s_Fetchers.erase(entry);
				} else if (!there || now - entry->second.Since > static_cast<long long>(25.0F * perSecond) || unit->IsPlayerControlled() || BattlePlayerCommands(unit->GetTeam())) {
					if (!unit->IsPlayerControlled() && !BattlePlayerCommands(unit->GetTeam())) {
						JoinBattleObjective(unit);
					}
					entry = s_Fetchers.erase(entry);
				} else {
					++entry;
				}
			}
			if (s_Pickups.empty()) {
				return;
			}
			const float reach = 500.0F + 200.0F * static_cast<float>(std::clamp(BattleCommanderLevel(), 0, 2));
			const std::vector<Actor*> fighters = BattleFighters();
			for (int side = 0; side < c_Sides; ++side) {
				if (!BattleTeamIn(side) || BattlePlayerCommands(side)) {
					continue;
				}
				for (const Pickup& pickup: s_Pickups) {
					if (std::any_of(s_Fetchers.begin(), s_Fetchers.end(), [&](const auto& fetch) { return fetch.second.PickupId == pickup.Id && g_MovableMan.FindObjectByUniqueID(fetch.first) && g_MovableMan.FindObjectByUniqueID(fetch.first)->GetTeam() == side; })) {
						continue;
					}
					Actor* nearest = nullptr;
					float nearestDistance = reach * reach;
					for (Actor* unit: fighters) {
						if (unit->GetTeam() != side || unit->IsPlayerControlled() || s_Fetchers.contains(unit->GetUniqueID()) || BattleUnitBusy(unit) || !CanTake(unit, pickup)) {
							continue;
						}
						const float distance = g_SceneMan.ShortestDistance(unit->GetPos(), pickup.Pos, wraps).GetSqrMagnitude();
						if (distance < nearestDistance) {
							nearest = unit;
							nearestDistance = distance;
						}
					}
					if (nearest) {
						s_Fetchers[nearest->GetUniqueID()] = {pickup.Id, now};
						SendUnit(nearest, pickup.Pos - Vector(0.0F, 20.0F), nullptr, false, "move: fetching a pickup");
					}
				}
			}
		}

		/// A pickup's crate on screen: a dark box with its kind's colour round it and its letter on it.
		void DrawCrate(ImDrawList* drawList, ImVec2 foot, int kind, float alpha) {
			const ImU32 color = c_PickupKinds[kind].Color;
			const auto faded = [alpha](ImU32 rgb, float a) { return (rgb & 0x00FFFFFF) | (static_cast<ImU32>(std::clamp(a * alpha, 0.0F, 255.0F)) << 24); };
			const ImVec2 topLeft(foot.x - 10.0F, foot.y - 18.0F);
			const ImVec2 bottomRight(foot.x + 10.0F, foot.y);
			drawList->AddRectFilled(topLeft, bottomRight, faded(IM_COL32(18, 20, 26, 0), 225.0F), 3.0F);
			drawList->AddRectFilled(ImVec2(topLeft.x, topLeft.y), ImVec2(bottomRight.x, topLeft.y + 4.0F), faded(color, 255.0F), 3.0F, ImDrawFlags_RoundCornersTop);
			drawList->AddRect(topLeft, bottomRight, faded(color, 255.0F), 3.0F, 0, 2.0F);
			const char glyph[2] = {c_PickupKinds[kind].Glyph, '\0'};
			const ImVec2 size = ImGui::CalcTextSize(glyph);
			drawList->AddText(ImVec2(foot.x - size.x * 0.5F, foot.y - 9.0F - size.y * 0.5F + 1.0F), faded(color, 255.0F), glyph);
		}

		/// A zone outlined and shaded on screen, with its name over its middle.
		void DrawPickupZone(ImDrawList* drawList, const std::vector<Vector>& zone, const std::string& label) {
			if (zone.size() < 3) {
				return;
			}
			std::vector<ImVec2> corners = ZoneOnScreen(zone, std::max(ScenePixelsPerWindowPixel(), 0.01F));
			drawList->AddConcavePolyFilled(corners.data(), static_cast<int>(corners.size()), (c_PickupPlaceColor & 0x00FFFFFF) | (30u << 24));
			drawList->AddPolyline(corners.data(), static_cast<int>(corners.size()), (c_PickupPlaceColor & 0x00FFFFFF) | (170u << 24), ImDrawFlags_Closed, 1.5F);
			ImVec2 middle(0.0F, 0.0F);
			for (const ImVec2& corner: corners) {
				middle.x += corner.x / static_cast<float>(corners.size());
				middle.y += corner.y / static_cast<float>(corners.size());
			}
			DrawBattleTag(drawList, middle, label, c_PickupPlaceColor);
		}
	} // namespace

	void StartBattlePickups() {
		s_Pickups.clear();
		s_Fetchers.clear();
		s_NextPickupAt = -1;
	}

	void StopBattlePickups() { StartBattlePickups(); }

	void SaveBattlePickups(std::ostream& out) {
		out << ' ' << s_Pickups.size();
		for (const Pickup& pickup: s_Pickups) {
			out << ' ' << pickup.Id << ' ' << pickup.Kind << ' ' << pickup.Pos.m_X << ' ' << pickup.Pos.m_Y;
			SaveUpdate(out, pickup.Born);
			SaveText(out, pickup.Preset);
			// (The module by its name: its ID depends on the mods loaded.)
			SaveText(out, pickup.ModuleID >= 0 && pickup.ModuleID < g_PresetMan.GetTotalModuleCount() ? g_PresetMan.GetDataModuleName(pickup.ModuleID) : std::string());
			SaveText(out, pickup.Name);
		}
		out << ' ' << s_Fetchers.size();
		for (const auto& [id, fetch]: s_Fetchers) {
			out << ' ' << id << ' ' << fetch.PickupId;
			SaveUpdate(out, fetch.Since);
		}
		out << ' ' << s_PickupIds;
		SaveUpdate(out, s_NextPickupAt);
	}

	void LoadBattlePickups(std::istream& in, const LoadedIDs& ids) {
		size_t count = 0;
		in >> count;
		s_Pickups.clear();
		for (size_t i = 0; i < count && in; ++i) {
			Pickup pickup;
			in >> pickup.Id >> pickup.Kind >> pickup.Pos.m_X >> pickup.Pos.m_Y;
			pickup.Born = LoadUpdate(in);
			pickup.Preset = LoadText(in);
			const std::string module = LoadText(in);
			pickup.Name = LoadText(in);
			pickup.ModuleID = module.empty() ? -1 : g_PresetMan.GetModuleID(module);
			// (One of a kind no longer known, or from a mod not loaded now, is left out.)
			if (pickup.Kind >= 0 && pickup.Kind < c_PickupKindCount && (module.empty() || pickup.ModuleID >= 0)) {
				s_Pickups.push_back(pickup);
			}
		}
		in >> count;
		s_Fetchers.clear();
		for (size_t i = 0; i < count && in; ++i) {
			long id = 0;
			Fetch fetch;
			in >> id >> fetch.PickupId;
			fetch.Since = LoadUpdate(in);
			if (const long unit = ids(id); unit != 0) {
				s_Fetchers[unit] = fetch;
			}
		}
		in >> s_PickupIds;
		s_NextPickupAt = LoadUpdate(in);
	}

	void UpdateBattlePickups(bool aiPaused) {
		if (!s_ModeRun.Running) {
			return;
		}
		const BattleModeSettings& settings = s_ModeRun.Settings;
		if (!settings.Pickups) {
			if (!s_Pickups.empty() || !s_Fetchers.empty()) {
				StopBattlePickups();
			}
			return;
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		const float perSecond = UpdatesPerSecond();
		if (s_NextPickupAt < 0) {
			// (The first comes in a little sooner than the rest.)
			s_NextPickupAt = now + static_cast<long long>(static_cast<float>(std::max(settings.PickupEvery, 1)) * 0.5F * perSecond);
		}
		if (settings.PickupLasts > 0) {
			std::erase_if(s_Pickups, [&](const Pickup& pickup) { return now - pickup.Born > static_cast<long long>(static_cast<float>(settings.PickupLasts) * perSecond); });
		}
		if (now % 6 == 0) {
			TakePickups();
		}
		if (now >= s_NextPickupAt && !s_ModeRun.Over) {
			if (static_cast<int>(s_Pickups.size()) >= std::max(settings.PickupMost, 1)) {
				// (No room: the next comes in a while after one is taken or goes.)
				s_NextPickupAt = now + static_cast<long long>(5.0F * perSecond);
			} else if (SpawnPickup(now)) {
				s_NextPickupAt = now + static_cast<long long>(static_cast<float>(std::max(settings.PickupEvery, 1)) * RandomNum(0.75F, 1.25F) * perSecond);
			} else {
				// (Nowhere for it this time: tried again soon.)
				s_NextPickupAt = now + static_cast<long long>(3.0F * perSecond);
			}
		}
		if (!aiPaused && now % 60 == 41) {
			UpdateFetchers(now);
		}
	}

	void ApplyPickupStroke(const Stroke& stroke) {
		Vector at = stroke.Position;
		g_SceneMan.WrapPosition(at);
		if (stroke.Kind == Tool::BattlePickupSpot) {
			// From a script's SandboxDo: another spot pickups come in at.
			for (BattleModeSettings* settings: {&s_ModeRun.Settings, &s_ModeSetup}) {
				if (settings->PickupSpots.size() < c_MaxPickupSpots) {
					settings->PickupSpots.push_back(at);
				}
				settings->PickupPlaces |= PickupAtSpots;
			}
		} else {
			// From a script's SandboxDo: one corner of a pickup zone a call, closed with one on its first corner.
			BattleSettings made;
			if (AddZoneCorner(s_ScriptPickupDraft, made, at, 20.0F) && !made.SpawnZones.empty()) {
				for (BattleModeSettings* settings: {&s_ModeRun.Settings, &s_ModeSetup}) {
					if (settings->PickupZones.size() < c_MaxPickupZones) {
						settings->PickupZones.push_back(made.SpawnZones.front());
					}
					settings->PickupPlaces |= PickupInZones;
				}
			}
		}
	}

	void PickupSpotPlaced(const Vector& position) {
		if (s_ModeSetup.PickupSpots.size() < c_MaxPickupSpots) {
			Vector at = position;
			g_SceneMan.WrapPosition(at);
			s_ModeSetup.PickupSpots.push_back(at);
			s_ModeSetup.PickupPlaces |= PickupAtSpots;
			SendBattleMode();
		}
		if (s_ModeSetup.PickupSpots.size() >= c_MaxPickupSpots) {
			PutDownBattleTool();
		}
	}

	void PickupZoneDrawn(const std::vector<Vector>& zone) {
		if (s_ModeSetup.PickupZones.size() < c_MaxPickupZones) {
			s_ModeSetup.PickupZones.push_back(zone);
			s_ModeSetup.PickupPlaces |= PickupInZones;
			SendBattleMode();
		}
		if (s_ModeSetup.PickupZones.size() >= c_MaxPickupZones) {
			PutDownBattleTool();
		}
	}

	std::string BattlePickupToolLabel(Tool kind) {
		if (kind == Tool::BattlePickupSpot) {
			return s_ModeSetup.PickupSpots.size() < c_MaxPickupSpots ? "Pickup spot " + std::to_string(s_ModeSetup.PickupSpots.size() + 1) : std::string("All pickup spots placed");
		}
		return "Pickup zone " + std::to_string(std::min(s_ModeSetup.PickupZones.size() + 1, c_MaxPickupZones));
	}

	void BattlePickupsPanel(BattleModeSettings& setup, bool running, bool& changed) {
		ImGui::SeparatorText("Pickups");
		ImGui::PushID("pickups");
		const Tool held = CurrentTool().Kind;
		if (ToolUI::Checkbox("Pickups come in on the map", &setup.Pickups)) {
			changed = true;
			if (!setup.Pickups && (held == Tool::BattlePickupSpot || held == Tool::BattlePickupZone)) {
				PutDownBattleTool();
			}
		}
		ImGui::SetItemTooltip("Now and then a powerup comes in on the map, with a beacon over it: the first unit to reach it takes it into its inventory, to use from its menu's Quick actions. The AI's teams send their nearest unit after each. Can be changed during the game.");
		if (!setup.Pickups) {
			ImGui::PopID();
			return;
		}
		changed |= ImGui::SliderInt("Every", &setup.PickupEvery, 10, 180, "about %d s");
		ImGui::SetItemTooltip("Seconds, about, between one pickup coming in and the next (the first comes in after half that).");
		changed |= ImGui::SliderInt("At once", &setup.PickupMost, 1, 10, setup.PickupMost == 1 ? "1 on the map" : "%d on the map");
		ImGui::SetItemTooltip("Most pickups on the map at once: no more come in till one is taken or goes.");
		changed |= ImGui::SliderInt("Lasts", &setup.PickupLasts, 0, 300, setup.PickupLasts > 0 ? "%d s" : "till taken");
		ImGui::SetItemTooltip("Seconds a pickup nobody takes lies before it goes. 0: it stays till it's taken.");

		ImGui::TextDisabled("Kinds");
		for (int kind = 0; kind < c_PickupKindCount; ++kind) {
			if (kind % 2 == 1) {
				ImGui::SameLine(ImGui::GetFontSize() * 10.0F);
			}
			bool on = (setup.PickupKinds & (1 << kind)) != 0;
			const bool loaded = KindLoaded(kind);
			ImGui::BeginDisabled(!loaded);
			if (ToolUI::Checkbox(c_PickupKinds[kind].Name, &on)) {
				setup.PickupKinds = on ? (setup.PickupKinds | (1 << kind)) : (setup.PickupKinds & ~(1 << kind));
				changed = true;
			}
			ImGui::EndDisabled();
			if (!loaded) {
				ImGui::SetItemTooltip("Not loaded: the spell scrolls come with Medieval.rte.");
			} else if (!c_PickupKinds[kind].Preset) {
				ImGui::SetItemTooltip("A scroll of one of the Medieval spells, picked at random: a cast or two by anyone, for no energy.");
			}
		}
		if ((setup.PickupKinds & ((1 << c_PickupKindCount) - 1)) == 0) {
			ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.4F, 1.0F), "No kind ticked: none come in.");
		}

		ImGui::TextDisabled("Where they come in");
		bool anywhere = (setup.PickupPlaces & PickupAnywhere) != 0;
		if (ToolUI::Checkbox("Anywhere on the map", &anywhere)) {
			setup.PickupPlaces = anywhere ? (setup.PickupPlaces | PickupAnywhere) : (setup.PickupPlaces & ~PickupAnywhere);
			changed = true;
		}
		ImGui::SetItemTooltip("On open ground anywhere, picked at random, but well away from every team's spawn zones (and not over them), and from the other pickups.");
		bool atSpots = (setup.PickupPlaces & PickupAtSpots) != 0;
		if (ToolUI::Checkbox(("At the spots placed (" + std::to_string(setup.PickupSpots.size()) + ")").c_str(), &atSpots)) {
			setup.PickupPlaces = atSpots ? (setup.PickupPlaces | PickupAtSpots) : (setup.PickupPlaces & ~PickupAtSpots);
			changed = true;
		}
		ImGui::SetItemTooltip("At one of the spots you place, picked at random, on the ground under it.");
		const bool placing = held == Tool::BattlePickupSpot;
		ImGui::BeginDisabled(!placing && setup.PickupSpots.size() >= c_MaxPickupSpots);
		if (ToolUI::Button(placing ? "Done (Enter)##spot" : "Place spots##spot")) {
			if (placing) {
				PutDownBattleTool();
			} else {
				TakeBattleTool(Tool::BattlePickupSpot, s_BattleEditTeam);
			}
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", placing ? "Click on the map for each spot; Enter (or this) when done." : ("Then click on the map for each spot a pickup can come in at. Up to " + std::to_string(c_MaxPickupSpots) + ".").c_str());
		ImGui::SameLine();
		ImGui::BeginDisabled(setup.PickupSpots.empty());
		if (ToolUI::Button("Take back the last##spot")) {
			setup.PickupSpots.pop_back();
			changed = true;
		}
		ImGui::SameLine();
		if (ToolUI::Button("Clear##spot")) {
			setup.PickupSpots.clear();
			changed = true;
		}
		ImGui::EndDisabled();
		bool inZones = (setup.PickupPlaces & PickupInZones) != 0;
		if (ToolUI::Checkbox(("In the pickup zones (" + std::to_string(setup.PickupZones.size()) + ")").c_str(), &inZones)) {
			setup.PickupPlaces = inZones ? (setup.PickupPlaces | PickupInZones) : (setup.PickupPlaces & ~PickupInZones);
			changed = true;
		}
		ImGui::SetItemTooltip("Somewhere on the ground in one of the zones you draw, picked at random.");
		const bool drawing = held == Tool::BattlePickupZone;
		ImGui::BeginDisabled(!drawing && setup.PickupZones.size() >= c_MaxPickupZones);
		if (ToolUI::Button(drawing ? "Done##zone" : "Draw a zone##zone")) {
			if (drawing) {
				PutDownBattleTool();
			} else {
				TakeBattleTool(Tool::BattlePickupZone, s_BattleEditTeam);
			}
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("%s", drawing ? "Click the corners on the map, then click the first corner again (or press Enter) to close it. Another can be drawn after." : ("Then click out the corners of a zone on the map that pickups can come in anywhere in. Up to " + std::to_string(c_MaxPickupZones) + ".").c_str());
		ImGui::SameLine();
		ImGui::BeginDisabled(setup.PickupZones.empty());
		if (ToolUI::Button("Take back the last##zone")) {
			setup.PickupZones.pop_back();
			changed = true;
		}
		ImGui::SameLine();
		if (ToolUI::Button("Clear##zone")) {
			setup.PickupZones.clear();
			changed = true;
		}
		ImGui::EndDisabled();
		const bool spotsUsable = atSpots && !setup.PickupSpots.empty();
		const bool zonesUsable = inZones && !setup.PickupZones.empty();
		if (!anywhere && !spotsUsable && !zonesUsable) {
			ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.4F, 1.0F), "No spot or zone to use: they come in anywhere.");
		}
		if (running) {
			const float next = s_NextPickupAt < 0 ? -1.0F : static_cast<float>(s_NextPickupAt - g_TimerMan.GetSimUpdateCount()) / UpdatesPerSecond();
			const int lying = static_cast<int>(s_Pickups.size());
			std::string status = std::to_string(lying) + (lying == 1 ? " pickup" : " pickups") + " on the map";
			if (next >= 0.0F && lying < std::max(setup.PickupMost, 1)) {
				status += ", the next in " + std::to_string(static_cast<int>(std::ceil(next))) + " s";
			}
			ImGui::TextDisabled("%s", status.c_str());
		}
		ImGui::PopID();
	}

	void DrawBattlePickups() {
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const Tool held = CurrentTool().Kind;
		const bool editing = held == Tool::BattlePickupSpot || held == Tool::BattlePickupZone;
		const bool running = s_ModeRun.Running && s_ModeRun.Settings.Mode == s_ModeSetup.Mode;
		// The spots and zones placed: while one of their tools is in hand, or the Battle tab is showing with pickups ticked.
		if (editing || (s_ModeSetup.Pickups && BattleSetupShowing())) {
			for (size_t i = 0; i < s_ModeSetup.PickupZones.size(); ++i) {
				DrawPickupZone(drawList, s_ModeSetup.PickupZones[i], "PICKUP ZONE " + std::to_string(i + 1));
			}
			for (size_t i = 0; i < s_ModeSetup.PickupSpots.size(); ++i) {
				const ImVec2 foot = ToScreen(s_ModeSetup.PickupSpots[i]);
				drawList->AddCircle(ImVec2(foot.x, foot.y - 9.0F), 14.0F, c_PickupPlaceColor, 24, 2.0F);
				drawList->AddCircleFilled(ImVec2(foot.x, foot.y - 9.0F), 3.0F, c_PickupPlaceColor);
				DrawBattleTag(drawList, ImVec2(foot.x, foot.y - 28.0F), "PICKUP SPOT " + std::to_string(i + 1), c_PickupPlaceColor);
			}
		}
		if (!running) {
			return;
		}
		// The pickups lying on the map: a crate bobbing over its spot under a beacon, named, and blinking as it's about to go.
		const long long now = g_TimerMan.GetSimUpdateCount();
		const float perSecond = UpdatesPerSecond();
		const int lasts = s_ModeRun.Settings.PickupLasts;
		for (const Pickup& pickup: s_Pickups) {
			const ImU32 color = c_PickupKinds[pickup.Kind].Color;
			const ImVec2 foot = ToScreen(pickup.Pos);
			const float bob = 3.0F * BattlePulse(3.0F);
			std::string label = pickup.Name;
			float alpha = 1.0F;
			if (lasts > 0) {
				const float left = static_cast<float>(lasts) - static_cast<float>(now - pickup.Born) / perSecond;
				if (left < 15.0F) {
					label += "  " + std::to_string(std::max(static_cast<int>(std::ceil(left)), 0)) + " s";
					alpha = 0.45F + 0.55F * BattlePulse(10.0F);
				}
			}
			DrawBattleBeacon(drawList, foot, color, 0.5F * alpha);
			DrawCrate(drawList, ImVec2(foot.x, foot.y - bob), pickup.Kind, alpha);
			DrawBattleTag(drawList, ImVec2(foot.x, foot.y - 30.0F - bob), label, color);
		}
	}

	void DrawBattlePickupsOnMap(ImDrawList* drawList, const std::function<ImVec2(const Vector&)>& toMap) {
		if (!s_ModeRun.Running) {
			return;
		}
		for (const Pickup& pickup: s_Pickups) {
			const ImVec2 at = toMap(pickup.Pos);
			const ImVec2 corners[] = {ImVec2(at.x, at.y - 4.5F), ImVec2(at.x + 4.5F, at.y), ImVec2(at.x, at.y + 4.5F), ImVec2(at.x - 4.5F, at.y)};
			drawList->AddConvexPolyFilled(corners, 4, c_PickupKinds[pickup.Kind].Color);
			drawList->AddPolyline(corners, 4, IM_COL32(0, 0, 0, 220), ImDrawFlags_Closed, 1.0F);
		}
	}
} // namespace SandboxDetail
