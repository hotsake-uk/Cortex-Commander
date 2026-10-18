// Spawn points (Spawn > Spawn point): points put down on the map that units come in at on a clock, on their own, with no battle running. Each
// picks its units as a battle team's card does (factions, favourites, crabs, jetpacks), puts them down as a battle's spawn zones do (on the
// ground round the point), or brings them in by ship over it, and gives each its spawn order once it's on its feet, as a commanded team's
// spawn zones do (SpawnOrders.cpp). A click puts one down; a click on one picks it to change; a right click takes it away. Kept in a saved
// game, with the units each has made.

#include "SandboxInternal.h"
#include "DetMath.h"
#include "Notices.h"

namespace SandboxDetail {
	namespace {
		constexpr float c_ReadySeconds = 0.75F; //!< How long a new unit is in before it is given its spawn order: on its feet, out of its ship.
		constexpr float c_GiveUpSeconds = 120.0F; //!< How long one still in its ship is waited for (and counted as alive).
		constexpr float c_PickPixels = 14.0F; //!< How near a point, in window pixels, a click is to pick it.
		constexpr float c_DragPixels = 6.0F; //!< How far the pointer goes with the right button down before it is a drag (the view), not a click.
		constexpr int c_MostAliveCeiling = 100; //!< Most of a point's units alive at once with Most alive at "no cap": it would otherwise make units on its clock for good.

		/// A unit a point made: counted against its cap while alive, and given its spawn order once in.
		struct Made {
			UnitRef Unit;
			long long MadeAt = 0;
			long long InAt = -1; //!< When it was first seen in the world (out of its ship), -1 not yet.
			bool Ordered = false; //!< Given its spawn order (or had none to give).
		};

		struct SpawnPoint {
			int ID = 0;
			Vector Pos;
			SpawnPointSettings Settings;
			bool Paused = false;
			int Count = 0; //!< Units it has made in all, against its total limit.
			long long NextAt = 0; //!< The sim update its next lot comes.
			std::vector<Made> Units;
		};
		std::vector<SpawnPoint> s_Points;
		int s_LastID = 0;

		// The window's side.
		SpawnPointSettings s_NewSetup; //!< The settings the next point put down is given.
		bool s_NewSetupTeamSet = false; //!< s_NewSetup.Team taken from the side picked elsewhere in the sandbox, once.
		int s_Picked = 0; //!< The point picked to change, by ID; 0 none.
		SpawnPointSettings s_PickedCopy; //!< The window's copy of its settings, sent whole when changed.
		int s_PickedCopyOf = 0; //!< Which point s_PickedCopy was taken from.
		bool s_SettingTarget = false; //!< The next clicks place the spawn order's point (or a patrol's route) rather than a point.
		std::vector<Vector> s_RouteDraft; //!< A patrol route being clicked out.
		bool s_RightDown = false;
		ImVec2 s_RightAt;

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		/// The order choices the panel lists: what the units are made to do, with the spawn order given them once in.
		struct ArrivalChoice {
			const char* Name;
			Order Behaviour;
			SpawnOrderKind Kind;
		};
		constexpr ArrivalChoice c_Arrivals[] = {
		    {"Sentry where they land", Order::Sentry, SpawnOrderKind::Hold},
		    {"Hold where they land", Order::Hold, SpawnOrderKind::Hold},
		    {"Attack nearest enemy", Order::Attack, SpawnOrderKind::Hold},
		    {"Hunt brains", Order::HuntBrains, SpawnOrderKind::Hold},
		    {"Move", Order::Sentry, SpawnOrderKind::Move},
		    {"Attack-move", Order::Sentry, SpawnOrderKind::AttackMove},
		    {"Defend at", Order::Sentry, SpawnOrderKind::DefendAt},
		    {"Patrol", Order::Sentry, SpawnOrderKind::Patrol},
		    {"Jetpack here", Order::Sentry, SpawnOrderKind::JetpackHere},
		    {"Battle objective", Order::Sentry, SpawnOrderKind::Objective},
		};

		/// Which of c_Arrivals settings are, the first for none that fits.
		int ArrivalOf(const SpawnPointSettings& settings) {
			for (int i = 0; i < static_cast<int>(std::size(c_Arrivals)); ++i) {
				if (c_Arrivals[i].Kind == settings.Arrival.Kind && (settings.Arrival.Kind != SpawnOrderKind::Hold || c_Arrivals[i].Behaviour == settings.Behaviour)) {
					return i;
				}
			}
			return 0;
		}

		/// The name of what a point's units do, as the panel lists it.
		const char* ArrivalName(int choice) { return c_Arrivals[std::clamp(choice, 0, static_cast<int>(std::size(c_Arrivals)) - 1)].Name; }

		/// Whether an order needs a place on the map, and how many points: a patrol a route of two or more, the rest one.
		bool NeedsPlace(SpawnOrderKind kind) { return kind == SpawnOrderKind::Move || kind == SpawnOrderKind::AttackMove || kind == SpawnOrderKind::DefendAt || kind == SpawnOrderKind::Patrol || kind == SpawnOrderKind::JetpackHere; }
		bool PlaceSet(const SpawnOrder& order) { return order.Kind == SpawnOrderKind::Patrol ? order.Points.size() >= 2 : !order.Points.empty(); }

		SpawnPoint* Find(int id) {
			auto found = std::find_if(s_Points.begin(), s_Points.end(), [id](const SpawnPoint& point) { return point.ID == id; });
			return found != s_Points.end() ? &*found : nullptr;
		}

		/// The area round a point its units appear in: a ring of corners, as a battle's spawn zone is drawn.
		std::vector<Vector> AreaOf(const SpawnPoint& point) {
			std::vector<Vector> zone;
			const float radius = static_cast<float>(std::clamp(point.Settings.Radius, 10, 400));
			for (int i = 0; i < 16; ++i) {
				const float angle = static_cast<float>(i) * c_TwoPI / 16.0F;
				zone.emplace_back(point.Pos.m_X + DetMath::Cos(angle) * radius, point.Pos.m_Y + DetMath::Sin(angle) * radius);
			}
			return zone;
		}

		/// Whether a unit that was out in the world is riding in a craft now (a ship ride, a vehicle): out of the world's actors, but not gone.
		/// (Only the pointer is compared, never followed: the object with its unique ID is still the one made.)
		bool Riding(const Made& made) { return made.Unit.Unit && g_MovableMan.FindObjectByUniqueID(made.Unit.ID) == made.Unit.Unit; }

		/// Its units alive now, or still on their way in a ship, or riding in one.
		int Alive(const SpawnPoint& point) {
			int alive = 0;
			for (const Made& made: point.Units) {
				const Actor* unit = GetRef(made.Unit);
				alive += unit ? (unit->IsDead() ? 0 : 1) : (made.InAt < 0 || Riding(made) ? 1 : 0);
			}
			return alive;
		}

		bool LimitReached(const SpawnPoint& point) { return point.Settings.TotalLimit > 0 && point.Count >= point.Settings.TotalLimit; }

		/// Keeps track of a point's units: drops those gone, and gives those just in their spawn order, together, as a selection is given one.
		void UpdateUnits(SpawnPoint& point, long long now, float perSecond) {
			std::vector<Actor*> ready;
			for (auto made = point.Units.begin(); made != point.Units.end();) {
				Actor* unit = GetRef(made->Unit);
				if (!unit) {
					// Gone, or not in yet (still in its ship): waited for a while, else dropped. One riding in a craft since it came in is kept.
					const bool gone = made->InAt >= 0 ? !Riding(*made) : static_cast<float>(now - made->MadeAt) > c_GiveUpSeconds * perSecond;
					made = gone ? point.Units.erase(made) : std::next(made);
					continue;
				}
				if (unit->IsDead()) {
					made = point.Units.erase(made);
					continue;
				}
				if (made->InAt < 0) {
					made->InAt = now;
				}
				if (!made->Ordered && static_cast<float>(now - made->InAt) >= c_ReadySeconds * perSecond) {
					made->Ordered = true;
					// (Not one you're playing, one that's changed side, nor one you have ordered already, selected as it came in: it keeps your order.)
					const Actor::StandingOrder& standing = unit->GetStandingOrder();
					const long id = unit->GetUniqueID();
					// (The Sentry it was made with is the point's own, not an order of yours.)
					if (point.Settings.Arrival.Kind != SpawnOrderKind::Hold && unit->GetTeam() == point.Settings.Team && !unit->IsPlayerControlled() && (standing.Kind == Actor::ORDER_NONE || standing.Kind == Actor::ORDER_SENTRY) && !standing.HasPost && standing.TargetID == 0 && !s_Plans.count(id) &&
					    std::none_of(s_PendingOrders.begin(), s_PendingOrders.end(), [unit](const PendingOrder& order) { return RefersTo(order.Unit, unit); })) {
						ready.push_back(unit);
					}
				}
				++made;
			}
			if (!ready.empty() && (!NeedsPlace(point.Settings.Arrival.Kind) || PlaceSet(point.Settings.Arrival))) {
				GiveSpawnOrderTo(point.Settings.Team, point.Settings.Arrival, std::move(ready));
			}
		}

		/// A point's next lot: as many as it may have, picked from its factions, put down round it or brought in by ship.
		void SpawnLot(SpawnPoint& point, long long now) {
			const SpawnPointSettings& settings = point.Settings;
			int room = std::clamp(settings.PerSpawn, 1, 20);
			room = std::min(room, (settings.MostAlive > 0 ? std::min(settings.MostAlive, c_MostAliveCeiling) : c_MostAliveCeiling) - Alive(point));
			if (settings.TotalLimit > 0) {
				room = std::min(room, settings.TotalLimit - point.Count);
			}
			if (room <= 0) {
				return;
			}
			const std::vector<const Preset*> pool = BattleUnitsFor(settings.Units);
			if (pool.empty()) {
				return;
			}
			const int team = std::clamp(settings.Team, 0, c_Sides - 1);
			std::vector<Actor*> lot;
			for (int attempt = 0; attempt < room * 3 && static_cast<int>(lot.size()) < room; ++attempt) {
				const Preset* pick = pool[std::min(pool.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(pool.size())))];
				if (Actor* unit = CreateUnit(*pick, team, 0, settings.Behaviour)) {
					lot.push_back(unit);
				}
			}
			if (lot.empty()) {
				return;
			}
			std::vector<Made> made;
			for (Actor* unit: lot) {
				made.push_back({MakeRef(unit), now, -1, false});
			}
			const int count = static_cast<int>(lot.size());
			if (settings.ByShip) {
				// (With no craft to be had, DropUnits deletes the units and pays nothing: none made.)
				if (DropUnits(lot, team, point.Pos.m_X, settings.Craft, settings.Invincible) <= 0.0F) {
					return;
				}
			} else {
				const std::vector<Vector> area = AreaOf(point);
				ActivateSide(team);
				for (Actor* unit: lot) {
					unit->SetPos(SpawnSpotInZone(area, unit->GetHeight()));
					g_MovableMan.AddActor(unit);
				}
			}
			point.Count += count;
			point.Units.insert(point.Units.end(), made.begin(), made.end());
		}

		std::string FactionNames(const std::vector<int>& factions) {
			std::string text;
			for (int id: factions) {
				text += (text.empty() ? "" : ";") + g_PresetMan.GetDataModuleName(id);
			}
			return text;
		}

		std::vector<int> FactionIDs(const std::string& text) {
			std::vector<int> ids;
			std::istringstream names(text);
			for (std::string name; std::getline(names, name, ';');) {
				if (int id = name.empty() ? -1 : g_PresetMan.GetModuleID(name); id >= 0) {
					ids.push_back(id);
				}
			}
			return ids;
		}

		/// Sends a stroke for a point (or for every one) to the sim.
		void Send(SpawnPointDo what, int id, const SpawnPointSettings* settings = nullptr, int value = 0) {
			Stroke stroke;
			stroke.Kind = Tool::SpawnPoint;
			stroke.Count = what;
			stroke.Choice = id;
			stroke.Radius = value;
			if (settings) {
				stroke.SpawnPoint = *settings;
			}
			s_Queue.push_back(stroke);
		}

		/// The settings the panel is changing: the picked point's copy, or the next point's.
		SpawnPointSettings& Editing() { return s_Picked != 0 ? s_PickedCopy : s_NewSetup; }

		/// After a change to Editing(): the picked point's sent to the sim.
		void Changed() {
			if (s_Picked != 0) {
				Send(SpawnPointEdit, s_Picked, &s_PickedCopy);
			}
		}

		/// The point under the pointer, by ID; 0 none.
		int PointAt(const ImVec2& at) {
			int best = 0;
			float bestDistance = c_PickPixels;
			for (const SpawnPoint& point: s_Points) {
				const ImVec2 onScreen = ToScreen(point.Pos);
				const float distance = DetMath::Hypot(onScreen.x - at.x, onScreen.y - at.y);
				if (distance <= bestDistance) {
					best = point.ID;
					bestDistance = distance;
				}
			}
			return best;
		}

		void StopSettingTarget() {
			s_SettingTarget = false;
			s_RouteDraft.clear();
		}

		/// The patrol route clicked out set as the spawn order's.
		void FinishRoute() {
			if (s_RouteDraft.size() >= 2) {
				Editing().Arrival.Points = s_RouteDraft;
				Changed();
			} else if (!s_RouteDraft.empty()) {
				Notices::Post("A patrol route needs two points or more");
			}
			StopSettingTarget();
		}

		/// A spawn order's place clicked on the map.
		void TargetClicked(const Vector& at) {
			SpawnOrder& order = Editing().Arrival;
			if (order.Kind == SpawnOrderKind::Patrol) {
				s_RouteDraft.push_back(at);
				return;
			}
			order.Points = {at};
			Changed();
			StopSettingTarget();
		}

		void DrawDashed(ImDrawList* drawList, ImVec2 from, ImVec2 to, ImU32 color, float thickness) {
			const float dx = to.x - from.x;
			const float dy = to.y - from.y;
			const int dashes = std::clamp(static_cast<int>(std::sqrt(dx * dx + dy * dy) / 12.0F), 1, 200);
			for (int i = 0; i < dashes; i += 2) {
				const float t0 = static_cast<float>(i) / static_cast<float>(dashes);
				const float t1 = static_cast<float>(i + 1) / static_cast<float>(dashes);
				drawList->AddLine(ImVec2(from.x + dx * t0, from.y + dy * t0), ImVec2(from.x + dx * t1, from.y + dy * t1), color, thickness);
			}
		}

		/// A spawn order's line from the point to where it sends them, and its route.
		void DrawOrder(ImDrawList* drawList, const Vector& from, const SpawnOrder& order, const std::vector<Vector>& draft, bool bright) {
			const std::vector<Vector>& points = !draft.empty() ? draft : order.Points;
			if (order.Kind == SpawnOrderKind::Hold || points.empty()) {
				return;
			}
			const ImU32 color = SpawnOrderColor(order.Kind);
			const ImU32 line = (color & 0x00FFFFFF) | ((bright ? 220u : 110u) << IM_COL32_A_SHIFT);
			DrawDashed(drawList, ToScreen(from), ToScreen(points.front()), line, bright ? 2.0F : 1.5F);
			for (size_t i = 1; i < points.size(); ++i) {
				drawList->AddLine(ToScreen(points[i - 1]), ToScreen(points[i]), line, 1.5F);
			}
			if (order.Kind == SpawnOrderKind::Patrol && !order.BackAndForth && points.size() > 2 && draft.empty()) {
				drawList->AddLine(ToScreen(points.back()), ToScreen(points.front()), line, 1.5F);
			}
			for (const Vector& at: points) {
				drawList->AddCircleFilled(ToScreen(at), 3.0F, color);
			}
			if (order.Kind == SpawnOrderKind::DefendAt) {
				drawList->AddCircle(ToScreen(points.front()), static_cast<float>(order.Radius) / std::max(ScenePixelsPerWindowPixel(), 0.01F), line, 0, 1.5F);
			}
		}

		const char* UrgencyName(float direct) { return direct <= -0.5F ? "Cautious" : (direct < 0.25F ? "Normal" : (direct < 0.75F ? "Risky" : "Whatever it takes")); }
	} // namespace

	void ApplySpawnPointStroke(const Stroke& stroke) {
		const long long now = g_TimerMan.GetSimUpdateCount();
		SpawnPoint* point = Find(stroke.Choice);
		switch (stroke.Count) {
			case SpawnPointPlace: {
				SpawnPoint added;
				added.ID = ++s_LastID;
				added.Pos = stroke.Position;
				g_SceneMan.WrapPosition(added.Pos);
				added.Settings = stroke.SpawnPoint;
				added.NextAt = added.Settings.FirstAtOnce ? now : now + std::max(1LL, static_cast<long long>(static_cast<float>(std::max(added.Settings.EverySeconds, 1)) * UpdatesPerSecond()));
				s_Points.push_back(std::move(added));
				break;
			}
			case SpawnPointEdit:
				if (point) {
					const int every = point->Settings.EverySeconds;
					point->Settings = stroke.SpawnPoint;
					if (every != point->Settings.EverySeconds) {
						// (A shorter clock takes effect now, not after the longer wait already under way.)
						point->NextAt = std::min(point->NextAt, now + std::max(1LL, static_cast<long long>(static_cast<float>(std::max(point->Settings.EverySeconds, 1)) * UpdatesPerSecond())));
					}
				}
				break;
			case SpawnPointRemove:
				std::erase_if(s_Points, [&stroke](const SpawnPoint& each) { return each.ID == stroke.Choice; });
				break;
			case SpawnPointRemoveAll:
				s_Points.clear();
				break;
			case SpawnPointPause:
				if (point) {
					point->Paused = stroke.Radius == 0;
				}
				break;
			case SpawnPointNow:
				if (point) {
					point->NextAt = now;
				}
				break;
			case SpawnPointReset:
				if (point) {
					point->Count = 0;
				}
				break;
			default:
				break;
		}
	}

	void UpdateSpawnPoints(bool aiPaused) {
		if (s_Points.empty()) {
			return;
		}
		if (aiPaused) {
			// (The clocks held back with it, so the lots don't all come at once after.)
			for (SpawnPoint& point: s_Points) {
				++point.NextAt;
			}
			return;
		}
		if (!s_CatalogueBuilt) {
			BuildCatalogue();
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		const float perSecond = UpdatesPerSecond();
		for (SpawnPoint& point: s_Points) {
			UpdateUnits(point, now, perSecond);
			if (point.Paused || LimitReached(point) || now < point.NextAt) {
				continue;
			}
			point.NextAt = now + std::max(1LL, static_cast<long long>(static_cast<float>(std::max(point.Settings.EverySeconds, 1)) * perSecond));
			SpawnLot(point, now);
		}
	}

	void ForgetSpawnPoints() {
		s_Points.clear();
		s_LastID = 0;
		s_Picked = 0;
		s_PickedCopyOf = 0;
		StopSettingTarget();
	}

	void SaveSpawnPoints(std::ostream& out) {
		out << ' ' << s_LastID << ' ' << s_Points.size();
		for (const SpawnPoint& point: s_Points) {
			const SpawnPointSettings& settings = point.Settings;
			const SpawnOrder& order = settings.Arrival;
			out << ' ' << point.ID << ' ' << point.Pos.m_X << ' ' << point.Pos.m_Y << ' ' << point.Paused << ' ' << point.Count;
			SaveUpdate(out, point.NextAt);
			out << ' ' << settings.Team;
			SaveText(out, FactionNames(settings.Units.Factions));
			out << ' ' << settings.Units.FavouritesOnly << ' ' << settings.Units.Crabs << ' ' << settings.Units.JetpackOnly << ' ' << settings.PerSpawn << ' ' << settings.EverySeconds << ' ' << settings.FirstAtOnce << ' '
			    << settings.MostAlive << ' ' << settings.TotalLimit << ' ' << settings.Radius << ' ' << settings.ByShip << ' ' << settings.Craft << ' ' << settings.Invincible << ' ' << static_cast<int>(settings.Behaviour);
			out << ' ' << static_cast<int>(order.Kind) << ' ' << order.Facing << ' ' << order.Direct << ' ' << order.BackAndForth << ' ' << order.Radius << ' ' << order.Chase << ' ' << order.Roam << ' ' << order.Points.size();
			for (const Vector& at: order.Points) {
				out << ' ' << at.m_X << ' ' << at.m_Y;
			}
			out << ' ' << point.Units.size();
			for (const Made& made: point.Units) {
				out << ' ' << made.Unit.ID << ' ' << made.Ordered;
				SaveUpdate(out, made.MadeAt);
				SaveUpdate(out, made.InAt);
			}
		}
	}

	void LoadSpawnPoints(std::istream& in, const LoadedIDs& ids) {
		ForgetSpawnPoints();
		size_t count = 0;
		in >> s_LastID >> count;
		for (size_t i = 0; i < count && in; ++i) {
			SpawnPoint point;
			SpawnPointSettings& settings = point.Settings;
			SpawnOrder& order = settings.Arrival;
			in >> point.ID >> point.Pos.m_X >> point.Pos.m_Y >> point.Paused >> point.Count;
			point.NextAt = LoadUpdate(in);
			in >> settings.Team;
			const std::string factions = LoadText(in);
			settings.Units.Factions = FactionIDs(factions);
			if (!factions.empty() && settings.Units.Factions.empty()) {
				// Its factions' mods aren't loaded now: none of them would mean any faction, so it waits, paused, rather than bring in others.
				point.Paused = true;
				g_ConsoleMan.PrintString("WARNING: Sandbox: a saved spawn point's factions (" + factions + ") aren't loaded; it's paused.");
			}
			int behaviour = 0;
			in >> settings.Units.FavouritesOnly >> settings.Units.Crabs >> settings.Units.JetpackOnly >> settings.PerSpawn >> settings.EverySeconds >> settings.FirstAtOnce >> settings.MostAlive >> settings.TotalLimit >> settings.Radius >> settings.ByShip >> settings.Craft >>
			    settings.Invincible >> behaviour;
			settings.Behaviour = UnitOrder(behaviour);
			int kind = 0;
			size_t points = 0;
			in >> kind >> order.Facing >> order.Direct >> order.BackAndForth >> order.Radius >> order.Chase >> order.Roam >> points;
			order.Kind = static_cast<SpawnOrderKind>(std::clamp(kind, 0, static_cast<int>(SpawnOrderKind::Count) - 1));
			order.Set = true;
			for (size_t p = 0; p < points && in; ++p) {
				Vector at;
				in >> at.m_X >> at.m_Y;
				order.Points.push_back(at);
			}
			size_t units = 0;
			in >> units;
			for (size_t u = 0; u < units && in; ++u) {
				long id = 0;
				Made made;
				in >> id >> made.Ordered;
				made.MadeAt = LoadUpdate(in);
				made.InAt = LoadUpdate(in);
				made.Unit = ids.Ref(id);
				// (One still in its ship when saved is back in the ship's cargo: it stays not in yet, waited for and given its spawn order when out.)
				if (made.Unit.Unit) {
					point.Units.push_back(made);
				}
			}
			if (in) {
				s_LastID = std::max(s_LastID, point.ID);
				s_Points.push_back(std::move(point));
			}
		}
	}

	void SpawnPointInput() {
		ImGuiIO& io = ImGui::GetIO();
		if (!InGame() || CurrentTool().Kind != Tool::SpawnPoint) {
			StopSettingTarget();
			s_RightDown = false;
			return;
		}
		if (s_Picked != 0 && !Find(s_Picked)) {
			s_Picked = 0;
			StopSettingTarget();
		}
		const bool onWorld = Sandbox::CapturesWorldClicks();
		const Vector at = MouseScenePosition();
		if (onWorld && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (s_SettingTarget) {
				TargetClicked(at);
			} else if (int id = PointAt(io.MousePos); id != 0) {
				s_Picked = s_Picked == id ? 0 : id;
			} else {
				if (!s_NewSetupTeamSet) {
					s_NewSetup.Team = s_Team;
					s_NewSetupTeamSet = true;
				}
				Stroke stroke;
				stroke.Kind = Tool::SpawnPoint;
				stroke.Count = SpawnPointPlace;
				stroke.Position = at;
				stroke.SpawnPoint = s_NewSetup;
				s_Queue.push_back(stroke);
			}
		}
		if (onWorld && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			s_RightDown = true;
			s_RightAt = io.MousePos;
		}
		if (s_RightDown && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			s_RightDown = false;
			if (std::abs(io.MousePos.x - s_RightAt.x) + std::abs(io.MousePos.y - s_RightAt.y) < c_DragPixels) {
				if (s_SettingTarget) {
					FinishRoute();
				} else if (int id = PointAt(io.MousePos); id != 0) {
					Send(SpawnPointRemove, id);
					if (s_Picked == id) {
						s_Picked = 0;
					}
				}
			}
		}
		if (!io.WantTextInput) {
			if (s_SettingTarget && Editing().Arrival.Kind == SpawnOrderKind::Patrol && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
				FinishRoute();
			} else if (s_SettingTarget && ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !s_RouteDraft.empty()) {
				s_RouteDraft.pop_back();
			} else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				if (s_SettingTarget) {
					StopSettingTarget();
				} else {
					s_Picked = 0;
				}
			}
		}
	}

	void DrawSpawnPoints() {
		if (!InGame() || (s_Points.empty() && !s_SettingTarget)) {
			return;
		}
		const bool inHand = CurrentTool().Kind == Tool::SpawnPoint;
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const float scale = std::max(ScenePixelsPerWindowPixel(), 0.01F);
		const long long now = g_TimerMan.GetSimUpdateCount();
		const float perSecond = UpdatesPerSecond();
		const int hovered = inHand ? PointAt(ImGui::GetIO().MousePos) : 0;
		for (const SpawnPoint& point: s_Points) {
			const SpawnPointSettings& settings = point.ID == s_Picked ? s_PickedCopy : point.Settings;
			const ImU32 side = c_SideColors[std::clamp(settings.Team, 0, c_Sides - 1)];
			const bool picked = point.ID == s_Picked;
			const bool stopped = point.Paused || LimitReached(point);
			const ImVec2 at = ToScreen(point.Pos);
			const ImU32 faint = (side & 0x00FFFFFF) | ((inHand ? 90u : 45u) << IM_COL32_A_SHIFT);
			if (inHand) {
				drawList->AddCircle(at, static_cast<float>(std::clamp(settings.Radius, 10, 400)) / scale, picked ? side : faint, 0, picked ? 2.0F : 1.0F);
				DrawOrder(drawList, point.Pos, settings.Arrival, picked && s_SettingTarget ? s_RouteDraft : std::vector<Vector>(), picked);
			}
			// The marker: a ring with a dot, the dot hollow while it's stopped, and round it how far it is to the next lot.
			const float size = inHand ? 9.0F : 6.0F;
			drawList->AddCircleFilled(at, size + 2.0F, IM_COL32(0, 0, 0, inHand ? 160 : 90));
			drawList->AddCircle(at, size, stopped ? faint : side, 0, 2.0F);
			if (!stopped) {
				drawList->AddCircleFilled(at, size * 0.45F, side);
				const float every = static_cast<float>(std::max(settings.EverySeconds, 1)) * perSecond;
				const float done = std::clamp(1.0F - static_cast<float>(point.NextAt - now) / every, 0.0F, 1.0F);
				if (inHand && done > 0.0F) {
					drawList->PathArcTo(at, size + 4.0F, -c_HalfPI, -c_HalfPI + done * c_TwoPI, 24);
					drawList->PathStroke(side, 0, 2.0F);
				}
			}
			if (picked) {
				drawList->AddCircle(at, size + 7.0F, IM_COL32(255, 255, 255, 220), 0, 1.5F);
			}
			if (inHand || point.ID == hovered) {
				char label[96];
				const int alive = Alive(point);
				if (settings.TotalLimit > 0) {
					std::snprintf(label, sizeof(label), "%d alive, %d/%d made%s", alive, point.Count, settings.TotalLimit, point.Paused ? ", paused" : (LimitReached(point) ? ", done" : ""));
				} else {
					std::snprintf(label, sizeof(label), "%d alive, %d made%s", alive, point.Count, point.Paused ? ", paused" : "");
				}
				const ImVec2 textAt(at.x - ImGui::CalcTextSize(label).x * 0.5F, at.y + size + 6.0F);
				drawList->AddText(ImVec2(textAt.x + 1.0F, textAt.y + 1.0F), IM_COL32(0, 0, 0, 200), label);
				drawList->AddText(textAt, picked || point.ID == hovered ? IM_COL32(255, 255, 255, 240) : IM_COL32(230, 230, 230, 170), label);
			}
		}
		if (inHand && s_SettingTarget) {
			// What the next click does, by the pointer.
			const SpawnOrder& order = Editing().Arrival;
			const char* hint = order.Kind == SpawnOrderKind::Patrol ? "Click the route's points; Enter or right click to finish" : "Click where they go";
			ImDrawList* front = ImGui::GetForegroundDrawList();
			const ImVec2 mouse = ImGui::GetIO().MousePos;
			front->AddText(ImVec2(mouse.x + 16.0F, mouse.y + 4.0F), SpawnOrderColor(order.Kind), hint);
			if (s_Picked == 0) {
				DrawOrder(front, MouseScenePosition(), order, s_RouteDraft, true);
			}
		}
	}

	std::string SpawnPointSummary() {
		const SpawnPointSettings& settings = Editing();
		std::string text = s_Picked != 0 ? "Point picked: " : "Next point: ";
		text += std::string(c_SideNames[std::clamp(settings.Team, 0, c_Sides - 1)]) + ", " + std::to_string(settings.PerSpawn) + " every " + std::to_string(settings.EverySeconds) + " s, " + ArrivalName(ArrivalOf(settings));
		return text + " (Spawn panel)";
	}

	void SpawnPointPanel() {
		if (s_Picked != 0 && !Find(s_Picked)) {
			s_Picked = 0;
		}
		if (!s_NewSetupTeamSet) {
			s_NewSetup.Team = s_Team;
			s_NewSetupTeamSet = true;
		}
		if (s_Picked != 0 && s_PickedCopyOf != s_Picked) {
			s_PickedCopy = Find(s_Picked)->Settings;
			s_PickedCopyOf = s_Picked;
			StopSettingTarget();
		} else if (s_Picked == 0) {
			s_PickedCopyOf = 0;
		}
		SpawnPointSettings& setup = Editing();
		bool changed = false;
		if (s_Picked != 0) {
			const SpawnPoint& point = *Find(s_Picked);
			ImGui::TextColored(ImVec4(1.0F, 1.0F, 1.0F, 1.0F), "Changing the point picked (%d made, %d alive)", point.Count, Alive(point));
			if (ToolUI::Button(point.Paused ? "Resume" : "Pause")) {
				Send(SpawnPointPause, s_Picked, nullptr, point.Paused ? 1 : 0);
			}
			ImGui::SetItemTooltip("Stops it bringing in units, or starts it again. Its units already in stay.");
			ImGui::SameLine();
			if (ToolUI::Button("Spawn now")) {
				Send(SpawnPointNow, s_Picked);
			}
			ImGui::SetItemTooltip("Its next lot comes now (within its caps), and its clock starts again from now.");
			ImGui::SameLine();
			if (ToolUI::Button("Reset count")) {
				Send(SpawnPointReset, s_Picked);
			}
			ImGui::SetItemTooltip("The count of units it has made goes back to none, so a total limit reached starts afresh.");
			ImGui::SameLine();
			if (ToolUI::Button("Remove")) {
				Send(SpawnPointRemove, s_Picked);
				s_Picked = 0;
				return;
			}
			ImGui::SetItemTooltip("Takes the point away (right click it on the map does too). Its units already in stay.");
			if (ToolUI::Button("Done", ImVec2(-1.0F, 0.0F))) {
				s_Picked = 0;
				StopSettingTarget();
				return;
			}
			ImGui::SetItemTooltip("Back to setting up the next point put down (Escape does too).");
		} else {
			ImGui::TextWrapped("Click the map to put down a spawn point with these settings. Click a point to pick it and change it; right click one to take it away.");
		}

		int team = setup.Team;
		if (ImGui::Combo("Side", &team, c_SideNames, c_Sides)) {
			setup.Team = team;
			changed = true;
		}
		changed |= FactionPicker(setup.Units);
		changed |= ToolUI::Checkbox("Favourites only", &setup.Units.FavouritesOnly);
		ImGui::SetItemTooltip("Only the units marked as favourites (Ctrl+click on a tile in the Spawn tab). With none of them marked, any.");

		ImGui::SeparatorText("How many, how often");
		changed |= ImGui::SliderInt("Units each time", &setup.PerSpawn, 1, 20);
		changed |= ImGui::SliderInt("Every", &setup.EverySeconds, 2, 300, "%d s", ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("Seconds of game time between one lot and the next. The clock stops while the AI is paused.");
		changed |= ImGui::SliderInt("Most alive", &setup.MostAlive, 0, 100, setup.MostAlive > 0 ? "%d at once" : "100 at once (most)");
		ImGui::SetItemTooltip("Most of this point's units alive at once, those still in a ship or riding in one counted. At it, none come; below it, only enough to top it up. 0: the most, 100.");
		changed |= ImGui::SliderInt("Total limit", &setup.TotalLimit, 0, 500, setup.TotalLimit > 0 ? "%d in all" : "no limit");
		ImGui::SetItemTooltip("Units it brings in, in all. Once it has, it stops (its marker goes hollow): Reset count starts it afresh. 0: no limit.");
		if (s_Picked == 0) {
			changed |= ToolUI::Checkbox("First lot at once", &setup.FirstAtOnce);
			ImGui::SetItemTooltip("The first lot comes as soon as the point is put down. Off: after the first wait.");
		}

		ImGui::SeparatorText("How they come in");
		if (ToolUI::RadioButton("Appear at the point", !setup.ByShip)) {
			setup.ByShip = false;
			changed = true;
		}
		ImGui::SetItemTooltip("They appear on the ground round the point, as at a battle's spawn zone.");
		ImGui::SameLine();
		if (ToolUI::RadioButton("By ship", setup.ByShip)) {
			setup.ByShip = true;
			changed = true;
		}
		ImGui::SetItemTooltip("A ship brings each lot in over the point and drops them off.");
		if (setup.ByShip) {
			changed |= ImGui::Combo("Craft", &setup.Craft, "Dropship\0Rocket\0");
			changed |= ToolUI::Checkbox("Ships can't be hurt", &setup.Invincible);
		} else {
			changed |= ImGui::SliderInt("Area", &setup.Radius, 10, 400, "%d px round it");
			ImGui::SetItemTooltip("How far round the point they appear, each on the ground there.");
		}
		changed |= ToolUI::Checkbox("Crabs", &setup.Units.Crabs);
		ImGui::SetItemTooltip("Crabs among its units: crabs, and the tanks and walkers built on them. Off: infantry and drones only.");
		ImGui::SameLine();
		changed |= ToolUI::Checkbox("Jetpacks only", &setup.Units.JetpackOnly);
		ImGui::SetItemTooltip("Only units whose jetpack really flies them (lifts them 5 m or more).");

		ImGui::SeparatorText("Spawn order");
		int choice = ArrivalOf(setup);
		if (ImGui::BeginCombo("Order", ArrivalName(choice))) {
			for (int i = 0; i < static_cast<int>(std::size(c_Arrivals)); ++i) {
				if (ImGui::Selectable(ArrivalName(i), i == choice)) {
					const bool samePlace = NeedsPlace(setup.Arrival.Kind) && NeedsPlace(c_Arrivals[i].Kind) && (setup.Arrival.Kind == SpawnOrderKind::Patrol) == (c_Arrivals[i].Kind == SpawnOrderKind::Patrol);
					setup.Behaviour = c_Arrivals[i].Behaviour;
					setup.Arrival.Kind = c_Arrivals[i].Kind;
					setup.Arrival.Set = true;
					if (!samePlace) {
						setup.Arrival.Points.clear();
					}
					StopSettingTarget();
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("What each unit does once it's in and on its feet, as a battle team's spawn order. Attack nearest enemy and Hunt brains are the battle's play styles.");
		const SpawnOrderKind kind = setup.Arrival.Kind;
		if (kind == SpawnOrderKind::Objective) {
			ImGui::TextDisabled("Goes for the battle objective nearest the point, while a battle mode runs.");
		}
		if (NeedsPlace(kind)) {
			const bool patrol = kind == SpawnOrderKind::Patrol;
			if (ToolUI::Button(s_SettingTarget ? (patrol ? "Finish route (Enter)" : "Cancel (Escape)") : (patrol ? "Click out the route" : "Pick the place"))) {
				if (s_SettingTarget) {
					patrol ? FinishRoute() : StopSettingTarget();
				} else {
					s_SettingTarget = true;
					s_RouteDraft.clear();
				}
			}
			ImGui::SetItemTooltip(patrol ? "Then click the route's points on the map with this tool; Enter or a right click finishes it, Backspace takes back a point." : "Then click the map with this tool where they're to go.");
			ImGui::SameLine();
			if (PlaceSet(setup.Arrival)) {
				if (patrol) {
					ImGui::TextDisabled("%d points", static_cast<int>(setup.Arrival.Points.size()));
				} else {
					ImGui::TextDisabled("set at %d, %d", setup.Arrival.Points.front().GetFloorIntX(), setup.Arrival.Points.front().GetFloorIntY());
				}
			} else {
				ImGui::TextColored(ImVec4(1.0F, 0.6F, 0.4F, 1.0F), "not set: they hold till it is");
			}
			if (patrol) {
				changed |= ToolUI::Checkbox("Back and forth", &setup.Arrival.BackAndForth);
				ImGui::SetItemTooltip("Along the route and back again, rather than round it as a loop.");
			}
			if (kind == SpawnOrderKind::DefendAt) {
				changed |= ImGui::SliderInt("Defend radius", &setup.Arrival.Radius, 30, 600, "%d px");
				changed |= ImGui::SliderInt("Chase distance", &setup.Arrival.Chase, 0, 1500, "%d px");
				changed |= ImGui::SliderInt("Roaming", &setup.Arrival.Roam, 0, 100, "%d%% of them");
			}
		}
		if (kind != SpawnOrderKind::Hold) {
			const float urgencies[] = {-1.0F, 0.0F, 0.5F, 1.0F};
			if (ImGui::BeginCombo("Risk", UrgencyName(setup.Arrival.Direct))) {
				for (float urgency: urgencies) {
					if (ImGui::Selectable(UrgencyName(urgency), std::strcmp(UrgencyName(urgency), UrgencyName(setup.Arrival.Direct)) == 0)) {
						setup.Arrival.Direct = urgency;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SetItemTooltip("How much risk they take getting there, as an order placed with one click (cautious) or more.");
		}

		if (!s_Points.empty()) {
			ImGui::Separator();
			char all[48];
			std::snprintf(all, sizeof(all), "Remove all spawn points (%d)", static_cast<int>(s_Points.size()));
			if (ToolUI::Button(all, ImVec2(-1.0F, 0.0F))) {
				Send(SpawnPointRemoveAll, 0);
				s_Picked = 0;
				StopSettingTarget();
			}
		}
		if (changed) {
			Changed();
		}
	}
} // namespace SandboxDetail
