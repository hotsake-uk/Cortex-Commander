// Campaign mode's model: making the galaxy, and the rules of a turn. See CampaignModel.h.

#include "CampaignModel.h"

#include "PresetMan.h"
#include "DataModule.h"
#include "Loadout.h"
#include "Actor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <list>
#include <numeric>
#include <queue>
#include <set>

namespace RTE::CampaignModel {

	namespace {
		constexpr float c_Pi = 3.14159265F;

		constexpr std::uint32_t Rgb(int r, int g, int b) { return 0xFF000000u | (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(r); }

		/// Faction colours, in the order factions are given them.
		constexpr std::uint32_t c_FactionColors[] = {Rgb(70, 140, 255), Rgb(235, 70, 60), Rgb(80, 200, 90), Rgb(240, 200, 50), Rgb(190, 90, 230), Rgb(255, 140, 40), Rgb(60, 210, 210), Rgb(235, 110, 170), Rgb(170, 170, 170), Rgb(150, 110, 70)};

		// The economy and battle numbers, kept together to be tuned.
		constexpr int c_StartGold = 1500;
		constexpr int c_PointIncome = 20; //!< Every owned point.
		constexpr int c_SettlementIncome = 30; //!< Times its level.
		constexpr int c_MarketIncome = 40; //!< And 15 times the level on top.
		constexpr int c_ControlBonus = 50; //!< A planet you control and nobody has invaded.
		constexpr float c_UpkeepRate = 0.05F; //!< Of each soldier's cost, each turn.
		constexpr float c_InvadedIncome = 0.5F; //!< What an invaded planet's points give its controller.
		constexpr float c_WallsBonus = 1.4F;
		constexpr float c_MilitiaPerLevel = 2.0F; //!< A settlement's locals, times its level; they come back at a quarter of that a turn.

		std::uint64_t SplitMix(std::uint64_t x) {
			x += 0x9E3779B97F4A7C15ull;
			x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
			x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
			return x ^ (x >> 31);
		}

		/// A random number stream for making a galaxy: the same seed always makes the same galaxy.
		struct Dice {
			std::mt19937 Engine;
			explicit Dice(std::uint32_t seed) :
			    Engine(seed) {}
			float Unit() { return std::uniform_real_distribution<float>(0.0F, 1.0F)(Engine); }
			float Range(float from, float to) { return from + (to - from) * Unit(); }
			int Int(int from, int to) { return std::uniform_int_distribution<int>(from, std::max(from, to))(Engine); }
			bool Chance(float chance) { return Unit() < chance; }
		};

		const char* const c_Syllables[] = {"ka", "ri", "vo", "tal", "zen", "or", "ul", "mar", "es", "dra", "kin", "sa", "lo", "vex", "thu", "ae", "ny", "qor", "bel", "ix", "ra", "do", "phe", "gan", "sol", "tir", "um", "yra", "hal", "cor", "ves", "nar"};
		const char* const c_Surnames[] = {"Hale", "Okoro", "Vance", "Reyes", "Novak", "Tanaka", "Brennan", "Kovacs", "Mbeki", "Larsen", "Quill", "Sato", "Duarte", "Ivanov", "Marsh", "Keane", "Abara", "Fischer", "Holt", "Ansel", "Volkov", "Ruiz", "Calder", "Nyberg", "Osei", "Price", "Rourke", "Stroud", "Yilmaz", "Zane", "Dekker", "Faraday", "Grieve", "Harlow", "Ibsen", "Juno", "Kessler", "Lomax", "Mercer", "Nash"};
		const char* const c_ArmyWords[] = {"Legion", "Host", "Vanguard", "Company", "Battlegroup", "Expedition", "Guard", "Column"};

		std::string Capitalised(std::string text) {
			if (!text.empty()) {
				text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
			}
			return text;
		}

		std::string MakeName(Dice& dice, int minSyllables, int maxSyllables) {
			std::string name;
			const int count = dice.Int(minSyllables, maxSyllables);
			for (int i = 0; i < count; ++i) {
				name += c_Syllables[dice.Int(0, static_cast<int>(std::size(c_Syllables)) - 1)];
			}
			return Capitalised(name);
		}

		std::string Ordinal(int number) {
			const int lastTwo = number % 100;
			const char* suffix = (lastTwo >= 11 && lastTwo <= 13) ? "th" : number % 10 == 1 ? "st" : number % 10 == 2 ? "nd" : number % 10 == 3 ? "rd" : "th";
			return std::to_string(number) + suffix;
		}

		bool Landable(const Planet& planet) { return planet.Type != PlanetType::GasGiant && !planet.Points.empty(); }

		/// Whether two segments cross (not counting a shared end).
		bool Crosses(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
			auto side = [](float px, float py, float qx, float qy, float rx, float ry) { return (qx - px) * (ry - py) - (qy - py) * (rx - px); };
			const float d1 = side(cx, cy, dx, dy, ax, ay);
			const float d2 = side(cx, cy, dx, dy, bx, by);
			const float d3 = side(ax, ay, bx, by, cx, cy);
			const float d4 = side(ax, ay, bx, by, dx, dy);
			return ((d1 > 0.0F) != (d2 > 0.0F)) && ((d3 > 0.0F) != (d4 > 0.0F)) && std::abs(d1) > 1e-6F && std::abs(d2) > 1e-6F && std::abs(d3) > 1e-6F && std::abs(d4) > 1e-6F;
		}

		/// Lane hops between every system and one, -1 where there's no way.
		std::vector<int> HopsFrom(const Campaign& campaign, int from) {
			std::vector<int> hops(campaign.Systems.size(), -1);
			std::queue<int> open;
			hops[from] = 0;
			open.push(from);
			while (!open.empty()) {
				const int at = open.front();
				open.pop();
				for (int next: campaign.Systems[at].Lanes) {
					if (hops[next] < 0) {
						hops[next] = hops[at] + 1;
						open.push(next);
					}
				}
			}
			return hops;
		}

		/// Joins systems by lanes: each to its nearest few without lanes crossing, then the islands joined up.
		void MakeLanes(Campaign& campaign, float spacing) {
			std::vector<StarSystem>& systems = campaign.Systems;
			const int count = static_cast<int>(systems.size());
			std::vector<std::pair<int, int>> lanes;
			auto distance = [&](int a, int b) { return std::hypot(systems[a].X - systems[b].X, systems[a].Y - systems[b].Y); };
			auto crossesAny = [&](int a, int b) {
				for (const auto& [c, d]: lanes) {
					if (c == a || c == b || d == a || d == b) {
						continue;
					}
					if (Crosses(systems[a].X, systems[a].Y, systems[b].X, systems[b].Y, systems[c].X, systems[c].Y, systems[d].X, systems[d].Y)) {
						return true;
					}
				}
				return false;
			};
			auto linked = [&](int a, int b) { return std::find(systems[a].Lanes.begin(), systems[a].Lanes.end(), b) != systems[a].Lanes.end(); };
			auto addLane = [&](int a, int b) {
				systems[a].Lanes.push_back(b);
				systems[b].Lanes.push_back(a);
				lanes.emplace_back(a, b);
			};
			// Every pair, shortest first: a lane where it's short and doesn't cross another, up to three per system.
			std::vector<std::pair<int, int>> pairs;
			for (int a = 0; a < count; ++a) {
				for (int b = a + 1; b < count; ++b) {
					pairs.emplace_back(a, b);
				}
			}
			std::sort(pairs.begin(), pairs.end(), [&](const auto& p, const auto& q) { return distance(p.first, p.second) < distance(q.first, q.second); });
			for (const auto& [a, b]: pairs) {
				if (distance(a, b) > spacing * 2.3F) {
					break;
				}
				if (systems[a].Lanes.size() >= 3 || systems[b].Lanes.size() >= 3 || linked(a, b) || crossesAny(a, b)) {
					continue;
				}
				addLane(a, b);
			}
			// Islands joined to the rest by their nearest pair, not crossing where it can.
			for (;;) {
				std::vector<int> hops = HopsFrom(campaign, 0);
				if (std::all_of(hops.begin(), hops.end(), [](int hop) { return hop >= 0; })) {
					break;
				}
				int bestA = -1;
				int bestB = -1;
				float best = 1e9F;
				bool bestCrosses = true;
				for (int a = 0; a < count; ++a) {
					for (int b = 0; b < count; ++b) {
						if (hops[a] < 0 || hops[b] >= 0) {
							continue;
						}
						const bool crosses = crossesAny(a, b);
						const float d = distance(a, b);
						if ((bestCrosses && !crosses) || (crosses == bestCrosses && d < best)) {
							bestA = a;
							bestB = b;
							best = d;
							bestCrosses = crosses;
						}
					}
				}
				addLane(bestA, bestB);
			}
		}

		/// A planet's points spread over its surface map, joined overland: a tree of the nearest, and one more for five.
		void MakePoints(Dice& dice, Planet& planet, int count) {
			planet.Points.clear();
			for (int i = 0; i < count; ++i) {
				Point point;
				for (int attempt = 0; attempt < 60; ++attempt) {
					point.X = dice.Range(0.12F, 0.88F);
					point.Y = dice.Range(0.18F, 0.82F);
					bool clear = std::all_of(planet.Points.begin(), planet.Points.end(), [&](const Point& other) { return std::hypot(other.X - point.X, other.Y - point.Y) > 0.26F; });
					if (clear) {
						break;
					}
				}
				point.Terrain = dice.Int(0, TerrainCount(planet.Type) - 1);
				point.Name = MakeName(dice, 1, 2) + " " + TerrainName(planet.Type, point.Terrain);
				planet.Points.push_back(point);
			}
			// Prim's tree over the points.
			std::vector<bool> joined(count, false);
			joined[0] = true;
			for (int added = 1; added < count; ++added) {
				int bestA = -1;
				int bestB = -1;
				float best = 1e9F;
				for (int a = 0; a < count; ++a) {
					for (int b = 0; b < count; ++b) {
						if (joined[a] && !joined[b]) {
							const float d = std::hypot(planet.Points[a].X - planet.Points[b].X, planet.Points[a].Y - planet.Points[b].Y);
							if (d < best) {
								best = d;
								bestA = a;
								bestB = b;
							}
						}
					}
				}
				joined[bestB] = true;
				planet.Points[bestA].Links.push_back(bestB);
				planet.Points[bestB].Links.push_back(bestA);
			}
			if (count >= 5) {
				// One loop, so a five-point planet has two ways round.
				for (int tries = 0; tries < 10; ++tries) {
					const int a = dice.Int(0, count - 1);
					const int b = dice.Int(0, count - 1);
					std::vector<int>& links = planet.Points[a].Links;
					if (a != b && std::find(links.begin(), links.end(), b) == links.end()) {
						links.push_back(b);
						planet.Points[b].Links.push_back(a);
						break;
					}
				}
			}
		}

		PlanetType RockyType(Dice& dice) {
			static constexpr PlanetType c_Types[] = {PlanetType::Temperate, PlanetType::Desert, PlanetType::Ice, PlanetType::Jungle, PlanetType::Volcanic, PlanetType::Barren, PlanetType::Caves, PlanetType::Ocean};
			static constexpr float c_Weights[] = {1.0F, 1.2F, 1.0F, 0.8F, 0.7F, 1.0F, 0.6F, 0.6F};
			float total = std::accumulate(std::begin(c_Weights), std::end(c_Weights), 0.0F);
			float roll = dice.Unit() * total;
			for (size_t i = 0; i < std::size(c_Types); ++i) {
				roll -= c_Weights[i];
				if (roll <= 0.0F) {
					return c_Types[i];
				}
			}
			return PlanetType::Barren;
		}

		PlanetType MoonType(Dice& dice) {
			static constexpr PlanetType c_Types[] = {PlanetType::Barren, PlanetType::Barren, PlanetType::Ice, PlanetType::Volcanic, PlanetType::Caves};
			return c_Types[dice.Int(0, static_cast<int>(std::size(c_Types)) - 1)];
		}

		/// The squad types a faction recruits: its Loadouts (not its brain's or its default), by what one soldier costs, cheapest first,
		/// split into three tiers by cost. A faction with none recruits plain militia.
		std::vector<SquadType> SquadTypesOf(const std::string& moduleName) {
			std::vector<SquadType> types;
			const int moduleID = g_PresetMan.GetModuleID(moduleName);
			std::list<Entity*> loadouts;
			if (moduleID >= 0) {
				g_PresetMan.GetAllOfType(loadouts, "Loadout", moduleID);
			}
			for (const Entity* entity: loadouts) {
				const Loadout* loadout = dynamic_cast<const Loadout*>(entity);
				if (!loadout || loadout->GetModuleID() != moduleID) {
					continue;
				}
				const std::string& name = loadout->GetPresetName();
				if (name == "Default" || name.find("Brain") != std::string::npos) {
					continue;
				}
				float tally = 0.0F;
				Actor* actor = loadout->CreateFirstActor(moduleID, 1.0F, 1.0F, tally);
				if (!actor) {
					continue;
				}
				SquadType type;
				type.Name = name;
				type.Loadout = name;
				type.SoldierCost = std::max(actor->GetTotalValue(moduleID, 1.0F, 1.0F), 20.0F);
				delete actor;
				const bool big = name.find("Mecha") != std::string::npos || name.find("Heavy") != std::string::npos;
				type.Size = big ? 3 : 4;
				types.push_back(type);
			}
			std::sort(types.begin(), types.end(), [](const SquadType& a, const SquadType& b) { return a.SoldierCost < b.SoldierCost; });
			const int count = static_cast<int>(types.size());
			for (int i = 0; i < count; ++i) {
				types[i].Tier = count <= 1 ? 1 : 1 + (i * 3) / count;
			}
			if (types.empty()) {
				SquadType militia;
				militia.Name = "Militia";
				militia.SoldierCost = 100.0F;
				types.push_back(militia);
			}
			return types;
		}

		Soldier NewSoldier(Campaign& campaign) {
			Soldier soldier;
			soldier.Id = campaign.NextId++;
			soldier.Name = c_Surnames[static_cast<size_t>(SplitMix(static_cast<std::uint64_t>(soldier.Id) * 7919u + campaign.Seed) % std::size(c_Surnames))];
			return soldier;
		}

		Squad NewSquad(Campaign& campaign, const Faction& faction, int type) {
			Squad squad;
			squad.Type = type;
			const int size = std::clamp(faction.Squads[type].Size, 1, c_MaxSquadSize);
			for (int i = 0; i < size; ++i) {
				squad.Soldiers.push_back(NewSoldier(campaign));
			}
			return squad;
		}

		std::string NewArmyName(Campaign& campaign, int faction) {
			int number = 1;
			for (const Army& army: campaign.Armies) {
				if (army.Faction == faction) {
					++number;
				}
			}
			return Ordinal(number) + " " + c_ArmyWords[static_cast<size_t>(SplitMix(campaign.NextId + faction) % std::size(c_ArmyWords))];
		}

		int NewArmy(Campaign& campaign, int faction, int system, int planet, int point) {
			Army army;
			army.Id = campaign.NextId++;
			army.Faction = faction;
			army.Name = NewArmyName(campaign, faction);
			army.System = system;
			army.Planet = planet;
			army.Point = point;
			army.Moves = 0;
			campaign.Armies.push_back(army);
			return static_cast<int>(campaign.Armies.size()) - 1;
		}

		float SoldierStrength(const SquadType& type, const Soldier& soldier) { return (type.SoldierCost / 100.0F) * std::max(soldier.Health, 0.1F) * (1.0F + 0.15F * static_cast<float>(soldier.Rank)); }

		int RankForKills(int kills) { return kills >= 15 ? 3 : kills >= 8 ? 2 : kills >= 3 ? 1 : 0; }

		std::string FactionName(const Campaign& campaign, int faction) { return faction >= 0 && faction < static_cast<int>(campaign.Factions.size()) ? campaign.Factions[faction].Name : std::string("Locals"); }

		bool IsHuman(const Campaign& campaign, int faction) { return faction >= 0 && faction < static_cast<int>(campaign.Factions.size()) && campaign.Factions[faction].Human; }

		/// Takes casualties from armies: so many fall, picked at random; the rest are hurt a little. Returns how many fell.
		int TakeCasualties(Campaign& campaign, const std::vector<int>& armies, float fraction) {
			std::vector<std::pair<int, int>> soldiers; // (army, squad) per soldier.
			for (int army: armies) {
				for (int squad = 0; squad < static_cast<int>(campaign.Armies[army].Squads.size()); ++squad) {
					for (size_t s = 0; s < campaign.Armies[army].Squads[squad].Soldiers.size(); ++s) {
						soldiers.emplace_back(army, squad);
					}
				}
			}
			const int total = static_cast<int>(soldiers.size());
			int toFall = std::clamp(static_cast<int>(std::round(static_cast<float>(total) * fraction)), 0, total);
			int fell = 0;
			while (fell < toFall) {
				// (Picked afresh from what's left each time.)
				std::vector<std::pair<int, int>> left;
				for (int army: armies) {
					for (int squad = 0; squad < static_cast<int>(campaign.Armies[army].Squads.size()); ++squad) {
						if (!campaign.Armies[army].Squads[squad].Soldiers.empty()) {
							left.emplace_back(army, squad);
						}
					}
				}
				if (left.empty()) {
					break;
				}
				const auto [army, squad] = left[std::min(static_cast<size_t>(Roll(campaign) * static_cast<float>(left.size())), left.size() - 1)];
				std::vector<Soldier>& members = campaign.Armies[army].Squads[squad].Soldiers;
				members.erase(members.begin() + std::min(static_cast<size_t>(Roll(campaign) * static_cast<float>(members.size())), members.size() - 1));
				++fell;
			}
			for (int army: armies) {
				for (Squad& squad: campaign.Armies[army].Squads) {
					for (Soldier& soldier: squad.Soldiers) {
						soldier.Health = std::max(0.1F, soldier.Health - Roll(campaign) * fraction * 0.6F);
					}
				}
				std::erase_if(campaign.Armies[army].Squads, [](const Squad& squad) { return squad.Soldiers.empty(); });
			}
			return fell;
		}

		/// The winners' kills shared out among them, and their promotions.
		void ShareKills(Campaign& campaign, const std::vector<int>& armies, int kills, std::vector<std::string>& promotions) {
			std::vector<Soldier*> soldiers;
			for (int army: armies) {
				for (Squad& squad: campaign.Armies[army].Squads) {
					for (Soldier& soldier: squad.Soldiers) {
						soldiers.push_back(&soldier);
					}
				}
			}
			if (soldiers.empty()) {
				return;
			}
			for (int kill = 0; kill < kills; ++kill) {
				Soldier* soldier = soldiers[std::min(static_cast<size_t>(Roll(campaign) * static_cast<float>(soldiers.size())), soldiers.size() - 1)];
				++soldier->Kills;
				const int rank = RankForKills(soldier->Kills);
				if (rank > soldier->Rank) {
					soldier->Rank = rank;
					promotions.push_back(soldier->Name + " is now a " + RankName(rank));
				}
			}
		}

		/// Armies with no squads left taken away (their indices shift: the caller works from IDs).
		void RemoveEmptyArmies(Campaign& campaign) {
			std::erase_if(campaign.Armies, [](const Army& army) { return army.Squads.empty(); });
		}

		void CheckFactions(Campaign& campaign) {
			for (int faction = 0; faction < static_cast<int>(campaign.Factions.size()); ++faction) {
				Faction& each = campaign.Factions[faction];
				if (!each.Alive) {
					continue;
				}
				bool hasPoint = false;
				for (const Planet& planet: campaign.Planets) {
					for (const Point& point: planet.Points) {
						hasPoint = hasPoint || point.Owner == faction;
					}
				}
				const bool hasArmy = std::any_of(campaign.Armies.begin(), campaign.Armies.end(), [faction](const Army& army) { return army.Faction == faction; });
				if (!hasPoint && !hasArmy) {
					each.Alive = false;
					AddLog(campaign, each.Name + " has been wiped out.");
				}
			}
			std::vector<int> alive;
			for (int faction = 0; faction < static_cast<int>(campaign.Factions.size()); ++faction) {
				if (campaign.Factions[faction].Alive) {
					alive.push_back(faction);
				}
			}
			if (alive.size() == 1 && campaign.Winner < 0) {
				campaign.Winner = alive.front();
				AddLog(campaign, campaign.Factions[alive.front()].Name + " rules the galaxy!");
			}
			campaign.Lost = std::none_of(campaign.Factions.begin(), campaign.Factions.end(), [](const Faction& faction) { return faction.Human && faction.Alive; });
		}

		/// An army moving onto a point: taken if nobody defends it, else an auto-resolved battle. Returns whether the army is there now.
		/// army is an index; it may move in the list, so it's found again by ID afterwards.
		bool EnterPoint(Campaign& campaign, int armyIndex, int planetIndex, int pointIndex) {
			const long armyID = campaign.Armies[armyIndex].Id;
			const int faction = campaign.Armies[armyIndex].Faction;
			Planet& planet = campaign.Planets[planetIndex];
			Point& point = planet.Points[pointIndex];
			const int owner = point.Owner;
			std::vector<int> defenders;
			for (int other: ArmiesAt(campaign, planetIndex, pointIndex)) {
				if (campaign.Armies[other].Faction != faction) {
					defenders.push_back(other);
				}
			}
			const float militia = owner == faction ? 0.0F : PointDefence(campaign, planetIndex, pointIndex);
			auto arrive = [&]() {
				Army& army = campaign.Armies[FindArmy(campaign, armyID)];
				army.Planet = planetIndex;
				army.Point = pointIndex;
				army.Path.clear();
				if (point.Owner != faction) {
					const int previous = point.Owner;
					point.Owner = faction;
					point.Militia = 0.0F;
					AddLog(campaign, campaign.Factions[faction].Name + " took " + point.Name + " on " + planet.Name + (previous >= 0 ? " from " + FactionName(campaign, previous) : std::string()) + ".");
				}
			};
			if (defenders.empty() && militia < 0.01F) {
				arrive();
				CheckFactions(campaign);
				return true;
			}

			// The battle, auto-resolved.
			BattleOutcome outcome;
			outcome.Happened = true;
			outcome.Attacker = faction;
			outcome.Defender = !defenders.empty() ? campaign.Armies[defenders.front()].Faction : owner;
			outcome.Where = point.Name + " on " + planet.Name;
			outcome.Planet = planetIndex;
			outcome.Point = pointIndex;
			const float walls = point.Built[static_cast<size_t>(Building::Walls)] && point.Settlement > 0 ? c_WallsBonus : 1.0F;
			float defence = militia;
			for (int defender: defenders) {
				defence += Strength(campaign, campaign.Armies[defender]) * walls;
				outcome.DefenderSoldiers += SoldierCount(campaign.Armies[defender]);
			}
			const int militiaSoldiers = static_cast<int>(std::ceil(militia > 0.0F ? point.Militia : 0.0F));
			outcome.DefenderSoldiers += militiaSoldiers;
			const float attack = Strength(campaign, campaign.Armies[armyIndex]);
			outcome.AttackerSoldiers = SoldierCount(campaign.Armies[armyIndex]);
			outcome.AttackStrength = attack;
			outcome.DefendStrength = defence;
			outcome.PlayerInvolved = IsHuman(campaign, faction) || IsHuman(campaign, outcome.Defender);
			const float rolledAttack = attack * (0.85F + 0.3F * Roll(campaign));
			const float rolledDefence = defence * (0.85F + 0.3F * Roll(campaign));
			outcome.AttackerWon = rolledAttack > rolledDefence;
			const float ratio = std::clamp(outcome.AttackerWon ? rolledDefence / std::max(rolledAttack, 0.01F) : rolledAttack / std::max(rolledDefence, 0.01F), 0.0F, 1.0F);
			const float winnerLoss = 0.08F + 0.45F * std::pow(ratio, 1.5F);
			const float loserLoss = 0.5F + 0.35F * (1.0F - ratio);
			const std::vector<int> attackers = {armyIndex};
			if (outcome.AttackerWon) {
				outcome.AttackerLost = TakeCasualties(campaign, attackers, winnerLoss);
				outcome.DefenderLost = TakeCasualties(campaign, defenders, loserLoss) + militiaSoldiers;
				ShareKills(campaign, attackers, outcome.DefenderLost, outcome.Promotions);
			} else {
				outcome.AttackerLost = TakeCasualties(campaign, attackers, loserLoss);
				outcome.DefenderLost = TakeCasualties(campaign, defenders, winnerLoss);
				ShareKills(campaign, defenders, outcome.AttackerLost, outcome.Promotions);
				if (militia > 0.0F) {
					point.Militia = std::max(0.0F, point.Militia * (1.0F - winnerLoss));
				}
			}
			std::vector<long> defenderIDs;
			for (int defender: defenders) {
				defenderIDs.push_back(campaign.Armies[defender].Id);
			}
			const std::string attackerName = campaign.Factions[faction].Name;
			const std::string defenderName = FactionName(campaign, outcome.Defender);
			AddLog(campaign, "Battle at " + outcome.Where + ": " + attackerName + " attacked " + defenderName + " and " + (outcome.AttackerWon ? "won" : "lost") + " (" + std::to_string(outcome.AttackerLost) + " lost against " + std::to_string(outcome.DefenderLost) + ").");
			RemoveEmptyArmies(campaign);
			bool there = false;
			if (outcome.AttackerWon) {
				// The defenders still standing pull back into orbit.
				for (long id: defenderIDs) {
					if (int defender = FindArmy(campaign, id); defender >= 0) {
						campaign.Armies[defender].Planet = -1;
						campaign.Armies[defender].Point = -1;
						campaign.Armies[defender].Moves = 0;
						campaign.Armies[defender].Path.clear();
					}
				}
				if (FindArmy(campaign, armyID) >= 0) {
					arrive();
					there = true;
				} else if (owner >= 0 || militia > 0.0F) {
					// Won, but nobody left to hold it.
					point.Militia = 0.0F;
				}
			}
			if (int army = FindArmy(campaign, armyID); army >= 0) {
				campaign.Armies[army].Moves = 0;
				campaign.Armies[army].Path.clear();
			}
			if (outcome.PlayerInvolved) {
				campaign.Reports.push_back(outcome);
			}
			if (campaign.Battles.size() < 64) {
				campaign.Battles.push_back(outcome);
			}
			CheckFactions(campaign);
			return there;
		}

		/// Spends an army's moves on its path: a lane jump at a time, while it has them. Taking off first, if it's on a planet.
		void FollowPath(Campaign& campaign, int army) {
			Army& moving = campaign.Armies[army];
			if (moving.Path.empty()) {
				return;
			}
			if (moving.Planet >= 0) {
				if (moving.Moves < 1) {
					return;
				}
				moving.Planet = -1;
				moving.Point = -1;
				--moving.Moves;
			}
			while (!moving.Path.empty() && moving.Moves >= 2) {
				const int next = moving.Path.front();
				const std::vector<int>& lanes = campaign.Systems[moving.System].Lanes;
				if (std::find(lanes.begin(), lanes.end(), next) == lanes.end()) {
					moving.Path.clear();
					break;
				}
				moving.System = next;
				moving.Path.erase(moving.Path.begin());
				moving.Moves -= 2;
			}
		}

		/// What taking a point is worth to a faction: its income and settlement, more for a planet it would then control.
		float PointValue(const Campaign& campaign, int faction, int planet, int point) {
			const Point& target = campaign.Planets[planet].Points[point];
			float value = 1.0F + static_cast<float>(target.Settlement) * 1.5F;
			int owned = 0;
			for (const Point& other: campaign.Planets[planet].Points) {
				owned += other.Owner == faction ? 1 : 0;
			}
			const int count = static_cast<int>(campaign.Planets[planet].Points.size());
			if ((owned + 1) * 2 > count && owned * 2 <= count) {
				value += 2.0F;
			}
			for (const Faction& each: campaign.Factions) {
				if (each.HomePlanet == planet && target.Owner >= 0) {
					value += 1.0F;
				}
			}
			return value;
		}

		/// One AI faction's turn: recruiting, building, then each army moving on the best target it can take.
		void AITurn(Campaign& campaign, int faction) {
			Faction& self = campaign.Factions[faction];
			const float nerve = campaign.Difficulty == 0 ? 0.72F : campaign.Difficulty == 2 ? 0.52F : 0.6F; //!< The win chance it attacks at.

			// Its armies on the same spot put together first, so it doesn't send out a trickle of small ones.
			for (int army = 0; army < static_cast<int>(campaign.Armies.size()); ++army) {
				for (int other = army + 1; other < static_cast<int>(campaign.Armies.size()); ++other) {
					const Army& a = campaign.Armies[army];
					const Army& b = campaign.Armies[other];
					if (a.Faction == faction && b.Faction == faction && a.System == b.System && a.Planet == b.Planet && a.Point == b.Point && static_cast<int>(a.Squads.size() + b.Squads.size()) <= c_MaxSquads) {
						Merge(campaign, army, other);
						other = army;
					}
				}
			}

			// Recruiting at its settlements while its soldiers' upkeep stays under about half its income, keeping some gold back.
			const int reserve = 250;
			for (int planet = 0; planet < static_cast<int>(campaign.Planets.size()); ++planet) {
				for (int point = 0; point < static_cast<int>(campaign.Planets[planet].Points.size()); ++point) {
					const Point& here = campaign.Planets[planet].Points[point];
					if (here.Owner != faction || here.Settlement == 0) {
						continue;
					}
					for (int squads = 0; squads < 3; ++squads) {
						const int upkeep = UpkeepOf(campaign, faction);
						const int gross = IncomeOf(campaign, faction) + upkeep;
						int best = -1;
						for (int type = static_cast<int>(self.Squads.size()) - 1; type >= 0; --type) {
							const float squadCost = self.Squads[type].SoldierCost * static_cast<float>(self.Squads[type].Size);
							std::string why;
							if (self.Gold - static_cast<int>(squadCost) >= reserve && static_cast<float>(upkeep) + squadCost * c_UpkeepRate <= static_cast<float>(gross) * 0.55F && CanRecruit(campaign, faction, planet, point, type, why)) {
								best = type;
								// (Now and then a cheaper one than the best it can have, for a mix.)
								if (Roll(campaign) < 0.6F) {
									break;
								}
							}
						}
						if (best < 0 || !Recruit(campaign, faction, planet, point, best)) {
							break;
						}
					}
					for (int army: ArmiesAt(campaign, planet, point)) {
						if (campaign.Armies[army].Faction == faction && ReplenishCost(campaign, army) > 0 && self.Gold - ReplenishCost(campaign, army) >= reserve) {
							Replenish(campaign, army);
						}
					}
				}
			}

			// Building with what's left: a couple of things a turn, while gold is plenty. Barracks and markets first, then settlements
			// grown and walls.
			int builds = 2;
			for (int pass = 0; pass < 3 && builds > 0; ++pass) {
				for (int planet = 0; planet < static_cast<int>(campaign.Planets.size()) && builds > 0; ++planet) {
					for (int point = 0; point < static_cast<int>(campaign.Planets[planet].Points.size()) && builds > 0; ++point) {
						const Point& here = campaign.Planets[planet].Points[point];
						if (here.Owner != faction || self.Gold < 1000) {
							continue;
						}
						bool built = false;
						if (pass == 0 && here.Settlement > 0) {
							built = Build(campaign, faction, planet, point, Building::Barracks) || Build(campaign, faction, planet, point, Building::Market);
						} else if (pass == 1) {
							built = Settle(campaign, faction, planet, point);
						} else if (pass == 2 && here.Settlement > 0) {
							built = Build(campaign, faction, planet, point, Building::Walls);
						}
						builds -= built ? 1 : 0;
					}
				}
			}

			// Armies, by ID (battles take armies away).
			std::vector<long> ids;
			for (const Army& army: campaign.Armies) {
				if (army.Faction == faction) {
					ids.push_back(army.Id);
				}
			}
			for (long id: ids) {
				for (int step = 0; step < 4; ++step) {
					int army = FindArmy(campaign, id);
					if (army < 0) {
						break;
					}
					Army& self_army = campaign.Armies[army];
					if (!self_army.Path.empty()) {
						FollowPath(campaign, army);
						break;
					}
					if (self_army.Moves <= 0) {
						break;
					}
					// The home planet's only army stays while an enemy army is in the system.
					if (self_army.Planet >= 0 && self_army.Planet == self.HomePlanet) {
						bool threat = false;
						for (const Army& other: campaign.Armies) {
							threat = threat || (other.Faction != faction && other.System == self_army.System);
						}
						int mine = 0;
						for (int other: ArmiesAt(campaign, self_army.Planet, self_army.Point)) {
							mine += campaign.Armies[other].Faction == faction ? 1 : 0;
						}
						if (threat && mine <= 1) {
							break;
						}
					}
					const float strength = Strength(campaign, self_army);
					const std::vector<int> hops = HopsFrom(campaign, self_army.System);
					float bestScore = 0.0F;
					int bestPlanet = -1;
					int bestPoint = -1;
					for (int planet = 0; planet < static_cast<int>(campaign.Planets.size()); ++planet) {
						const Planet& target = campaign.Planets[planet];
						const int hop = hops[target.System];
						if (hop < 0 || hop > 4 || !Landable(target)) {
							continue;
						}
						for (int point = 0; point < static_cast<int>(target.Points.size()); ++point) {
							if (target.Points[point].Owner == faction) {
								continue;
							}
							float defence = 0.0F;
							WouldFight(campaign, faction, planet, point, defence);
							if (defence > 0.0F && WinChance(strength, defence) < nerve) {
								continue;
							}
							float distance = static_cast<float>(hop) * 2.0F;
							if (hop == 0) {
								const bool samePlanet = planet == self_army.Planet;
								const bool linked = samePlanet && self_army.Point >= 0 && std::find(target.Points[self_army.Point].Links.begin(), target.Points[self_army.Point].Links.end(), point) != target.Points[self_army.Point].Links.end();
								distance = linked ? 0.5F : samePlanet ? 3.0F : (self_army.Planet >= 0 ? 2.0F : 1.0F);
							}
							const float score = PointValue(campaign, faction, planet, point) / (1.0F + distance) * (defence > 0.0F ? 0.8F : 1.0F);
							if (score > bestScore) {
								bestScore = score;
								bestPlanet = planet;
								bestPoint = point;
							}
						}
					}
					if (bestPlanet < 0) {
						break;
					}
					const Planet& target = campaign.Planets[bestPlanet];
					bool acted = false;
					if (target.System != self_army.System) {
						OrderMove(campaign, army, target.System);
						break;
					} else if (self_army.Planet == bestPlanet && self_army.Point >= 0 && std::find(target.Points[self_army.Point].Links.begin(), target.Points[self_army.Point].Links.end(), bestPoint) != target.Points[self_army.Point].Links.end()) {
						acted = OrderOverland(campaign, army, bestPoint);
					} else if (self_army.Planet >= 0) {
						acted = OrderLaunch(campaign, army);
					} else {
						acted = OrderLand(campaign, army, bestPlanet, bestPoint);
					}
					if (!acted) {
						break;
					}
				}
			}
		}

		/// The start of a turn for every faction: income less upkeep, healing, locals coming back, moves back, and moves carried on.
		void NewTurn(Campaign& campaign) {
			++campaign.Turn;
			const float aiIncome = campaign.Difficulty == 0 ? 0.75F : campaign.Difficulty == 2 ? 1.35F : 1.0F;
			for (int faction = 0; faction < static_cast<int>(campaign.Factions.size()); ++faction) {
				Faction& each = campaign.Factions[faction];
				if (!each.Alive) {
					continue;
				}
				int income = IncomeOf(campaign, faction);
				if (!each.Human && income > 0) {
					income = static_cast<int>(static_cast<float>(income) * aiIncome);
				}
				each.Gold += income;
				if (each.Gold < 0) {
					// Unpaid: soldiers desert, one for every 50 short.
					int deserters = std::max(1, -each.Gold / 50);
					each.Gold = 0;
					for (Army& army: campaign.Armies) {
						for (Squad& squad: army.Squads) {
							while (army.Faction == faction && deserters > 0 && !squad.Soldiers.empty()) {
								squad.Soldiers.pop_back();
								--deserters;
							}
						}
					}
					for (Army& army: campaign.Armies) {
						std::erase_if(army.Squads, [](const Squad& squad) { return squad.Soldiers.empty(); });
					}
					RemoveEmptyArmies(campaign);
					if (each.Human) {
						AddLog(campaign, "Unpaid soldiers of " + each.Name + " deserted.");
					}
				}
			}
			for (Army& army: campaign.Armies) {
				army.Moves = c_MovesPerTurn;
				float heal = 0.05F;
				if (army.Planet >= 0) {
					const Point& point = campaign.Planets[army.Planet].Points[army.Point];
					if (point.Owner == army.Faction) {
						heal = point.Settlement > 0 ? 0.3F : 0.15F;
						if (point.Settlement > 0 && point.Built[static_cast<size_t>(Building::Hospital)]) {
							heal += 0.25F;
						}
						if (ControlOf(campaign, army.Planet).Invaded) {
							heal *= 0.5F;
						}
					}
				}
				for (Squad& squad: army.Squads) {
					for (Soldier& soldier: squad.Soldiers) {
						soldier.Health = std::min(1.0F, soldier.Health + heal);
					}
				}
			}
			for (Planet& planet: campaign.Planets) {
				for (Point& point: planet.Points) {
					if (point.Settlement > 0) {
						const float most = c_MilitiaPerLevel * static_cast<float>(point.Settlement);
						point.Militia = std::min(most, point.Militia + most * 0.25F);
					}
				}
			}
			for (int army = 0; army < static_cast<int>(campaign.Armies.size()); ++army) {
				FollowPath(campaign, army);
			}
			CheckFactions(campaign);
		}
	} // namespace

#pragma region Names and looks
	const char* PlanetTypeName(PlanetType type) {
		static constexpr const char* c_Names[] = {"Temperate world", "Desert world", "Ice world", "Jungle world", "Volcanic world", "Barren rock", "Cave world", "Ocean world", "Gas giant"};
		return c_Names[std::clamp(static_cast<int>(type), 0, static_cast<int>(PlanetType::Count) - 1)];
	}

	namespace {
		const std::vector<std::vector<const char*>>& Terrains() {
			static const std::vector<std::vector<const char*>> c_Terrains = {
			    {"Plains", "Hills", "Highlands", "Valley", "Forest"},
			    {"Dunes", "Canyon", "Mesa", "Flats", "Badlands"},
			    {"Tundra", "Glacier", "Ice Caves", "Peaks", "Snowfield"},
			    {"Jungle", "Swamp", "Ridge", "River Delta", "Ruins"},
			    {"Ash Fields", "Lava Flats", "Caldera", "Basalt Ridge", "Vents"},
			    {"Crater", "Regolith", "Rift", "Mountains", "Plateau"},
			    {"Caverns", "Hollows", "Sinkhole", "Deep Tunnels", "Grotto"},
			    {"Archipelago", "Reef", "Atoll", "Cliffs", "Shallows"},
			    {"Clouds"},
			};
			return c_Terrains;
		}
	} // namespace

	int TerrainCount(PlanetType type) { return static_cast<int>(Terrains()[std::clamp(static_cast<int>(type), 0, static_cast<int>(PlanetType::Count) - 1)].size()); }

	const char* TerrainName(PlanetType type, int terrain) {
		const std::vector<const char*>& names = Terrains()[std::clamp(static_cast<int>(type), 0, static_cast<int>(PlanetType::Count) - 1)];
		return names[std::clamp(terrain, 0, static_cast<int>(names.size()) - 1)];
	}

	const char* BuildingName(Building building) {
		static constexpr const char* c_Names[] = {"Barracks", "Market", "Walls", "Hospital"};
		return c_Names[std::clamp(static_cast<int>(building), 0, static_cast<int>(Building::Count) - 1)];
	}

	const char* BuildingTip(Building building) {
		static constexpr const char* c_Tips[] = {
		    "Lets better squads be recruited here: second tier, and third in a city on a planet nobody has invaded.",
		    "More gold every turn from this point.",
		    "Armies and locals defending this point fight harder.",
		    "Wounded soldiers resting here heal faster."};
		return c_Tips[std::clamp(static_cast<int>(building), 0, static_cast<int>(Building::Count) - 1)];
	}

	int BuildingCost(Building building) {
		static constexpr int c_Costs[] = {500, 400, 600, 350};
		return c_Costs[std::clamp(static_cast<int>(building), 0, static_cast<int>(Building::Count) - 1)];
	}

	const char* SettlementName(int level) {
		static constexpr const char* c_Names[] = {"None", "Outpost", "Colony", "City"};
		return c_Names[std::clamp(level, 0, c_MaxSettlement)];
	}

	const char* RankName(int rank) {
		static constexpr const char* c_Names[] = {"Private", "Corporal", "Sergeant", "Veteran"};
		return c_Names[std::clamp(rank, 0, 3)];
	}

	std::uint32_t PlanetColor(PlanetType type) {
		static constexpr std::uint32_t c_Colors[] = {Rgb(80, 150, 90), Rgb(205, 165, 95), Rgb(190, 220, 240), Rgb(40, 130, 60), Rgb(170, 60, 35), Rgb(140, 135, 128), Rgb(100, 85, 110), Rgb(45, 95, 175), Rgb(200, 150, 110)};
		return c_Colors[std::clamp(static_cast<int>(type), 0, static_cast<int>(PlanetType::Count) - 1)];
	}

	std::uint32_t StarColor(int index) {
		static constexpr std::uint32_t c_Colors[] = {Rgb(255, 240, 200), Rgb(255, 210, 120), Rgb(255, 150, 90), Rgb(180, 210, 255), Rgb(255, 255, 255), Rgb(255, 110, 80)};
		return c_Colors[static_cast<size_t>(std::abs(index)) % std::size(c_Colors)];
	}
#pragma endregion

#pragma region Starting a campaign
	std::vector<FactionChoice> AvailableFactions() {
		std::vector<FactionChoice> factions;
		for (int module = 0; module < g_PresetMan.GetTotalModuleCount(); ++module) {
			const DataModule* data = g_PresetMan.GetDataModule(module);
			if (!data || !data->IsFaction()) {
				continue;
			}
			FactionChoice choice;
			choice.Module = data->GetFileName();
			choice.Name = data->GetFriendlyName().empty() ? choice.Module : data->GetFriendlyName();
			factions.push_back(choice);
		}
		std::sort(factions.begin(), factions.end(), [](const FactionChoice& a, const FactionChoice& b) { return a.Name < b.Name; });
		return factions;
	}

	Campaign Generate(const NewCampaign& setup, std::string& error) {
		Campaign campaign;
		std::vector<FactionChoice> picked;
		for (const FactionChoice& choice: setup.Factions) {
			if (choice.Include) {
				picked.push_back(choice);
			}
		}
		if (picked.size() < 2) {
			error = "Pick at least two factions.";
			return campaign;
		}
		if (std::none_of(picked.begin(), picked.end(), [](const FactionChoice& choice) { return choice.Human; })) {
			error = "Pick a faction to play.";
			return campaign;
		}
		campaign.Name = setup.Name.empty() ? std::string("Campaign") : setup.Name;
		campaign.Seed = setup.Seed;
		campaign.Size = std::clamp(setup.Size, 0, 2);
		campaign.Difficulty = std::clamp(setup.Difficulty, 0, 2);
		Dice dice(setup.Seed);

		// Stars, spread over a wide oval.
		static constexpr int c_SystemCounts[] = {14, 24, 40};
		const int systemCount = std::max(c_SystemCounts[campaign.Size], static_cast<int>(picked.size()) * 3);
		const float spacing = 0.62F / std::sqrt(static_cast<float>(systemCount));
		for (int tries = 0; static_cast<int>(campaign.Systems.size()) < systemCount && tries < 20000; ++tries) {
			const float angle = dice.Range(0.0F, 2.0F * c_Pi);
			const float radius = std::sqrt(dice.Unit()) * 0.46F;
			StarSystem system;
			system.X = 0.5F + std::cos(angle) * radius;
			system.Y = 0.5F + std::sin(angle) * radius * 0.82F;
			const float minimum = tries < 15000 ? spacing : spacing * 0.6F;
			if (std::any_of(campaign.Systems.begin(), campaign.Systems.end(), [&](const StarSystem& other) { return std::hypot(other.X - system.X, other.Y - system.Y) < minimum; })) {
				continue;
			}
			system.Name = MakeName(dice, 2, 3);
			system.StarColor = dice.Int(0, 5);
			campaign.Systems.push_back(system);
		}
		MakeLanes(campaign, spacing);

		// Planets and moons.
		for (int systemIndex = 0; systemIndex < static_cast<int>(campaign.Systems.size()); ++systemIndex) {
			const int planetCount = dice.Int(1, 4);
			bool landable = false;
			for (int i = 0; i < planetCount; ++i) {
				Planet planet;
				planet.System = systemIndex;
				planet.Orbit = 1.0F + static_cast<float>(i);
				planet.Angle = dice.Range(0.0F, 2.0F * c_Pi);
				planet.Look = static_cast<std::uint32_t>(dice.Engine());
				const bool giant = i > 0 && dice.Chance(0.22F);
				planet.Type = giant ? PlanetType::GasGiant : RockyType(dice);
				planet.Size = giant ? dice.Range(1.5F, 1.9F) : dice.Range(0.75F, 1.45F);
				if (!giant) {
					MakePoints(dice, planet, planet.Size >= 1.15F ? 5 : 3);
					landable = true;
				}
				const int planetIndex = static_cast<int>(campaign.Planets.size());
				campaign.Planets.push_back(planet);
				campaign.Systems[systemIndex].Planets.push_back(planetIndex);
				const int moons = giant ? dice.Int(1, 2) : (dice.Chance(0.3F) ? 1 : 0);
				for (int m = 0; m < moons; ++m) {
					Planet moon;
					moon.System = systemIndex;
					moon.Parent = planetIndex;
					moon.Orbit = 1.0F + static_cast<float>(m) * 0.6F;
					moon.Angle = dice.Range(0.0F, 2.0F * c_Pi);
					moon.Look = static_cast<std::uint32_t>(dice.Engine());
					moon.Type = MoonType(dice);
					moon.Size = dice.Range(0.45F, 0.65F);
					MakePoints(dice, moon, 3);
					landable = true;
					campaign.Systems[systemIndex].Planets.push_back(static_cast<int>(campaign.Planets.size()));
					campaign.Planets.push_back(moon);
				}
			}
			if (!landable) {
				// Every system has somewhere to land.
				Planet& first = campaign.Planets[campaign.Systems[systemIndex].Planets.front()];
				first.Type = RockyType(dice);
				first.Size = 1.0F;
				MakePoints(dice, first, 3);
			}
		}
		// Planet names by their order out from the star: I, II, III, IV (moons keep their planet's name and a letter).
		for (StarSystem& system: campaign.Systems) {
			int number = 0;
			static constexpr const char* c_Numerals[] = {"I", "II", "III", "IV", "V", "VI"};
			for (int planetIndex: system.Planets) {
				Planet& planet = campaign.Planets[planetIndex];
				if (planet.Parent < 0) {
					planet.Name = system.Name + " " + c_Numerals[std::min(number++, 5)];
				} else {
					const int moonNumber = static_cast<int>(std::round((planet.Orbit - 1.0F) / 0.6F));
					planet.Name = campaign.Planets[planet.Parent].Name + static_cast<char>('a' + moonNumber);
				}
			}
		}

		// Neutral settlements here and there, held by locals.
		for (Planet& planet: campaign.Planets) {
			for (Point& point: planet.Points) {
				if (dice.Chance(0.22F)) {
					point.Settlement = dice.Chance(0.25F) ? 2 : 1;
					point.Militia = c_MilitiaPerLevel * static_cast<float>(point.Settlement) * (0.8F + 0.3F * static_cast<float>(campaign.Difficulty));
				}
			}
		}

		// The factions, their homes as far apart along the lanes as they can be.
		std::vector<int> homes;
		homes.push_back(dice.Int(0, static_cast<int>(campaign.Systems.size()) - 1));
		while (homes.size() < picked.size()) {
			std::vector<int> nearest(campaign.Systems.size(), 1 << 20);
			for (int home: homes) {
				std::vector<int> hops = HopsFrom(campaign, home);
				for (size_t i = 0; i < hops.size(); ++i) {
					nearest[i] = std::min(nearest[i], hops[i] < 0 ? 1 << 20 : hops[i]);
				}
			}
			int best = 0;
			for (int i = 0; i < static_cast<int>(nearest.size()); ++i) {
				if (nearest[i] > nearest[best]) {
					best = i;
				}
			}
			homes.push_back(best);
		}
		for (size_t f = 0; f < picked.size(); ++f) {
			Faction faction;
			faction.Name = picked[f].Name;
			faction.Module = picked[f].Module;
			faction.Human = picked[f].Human;
			faction.Color = c_FactionColors[f % std::size(c_FactionColors)];
			faction.Gold = c_StartGold;
			faction.Squads = SquadTypesOf(picked[f].Module);
			campaign.Factions.push_back(faction);
		}
		for (size_t f = 0; f < picked.size(); ++f) {
			const int faction = static_cast<int>(f);
			StarSystem& system = campaign.Systems[homes[f]];
			// The home planet: the system's first rocky planet made a big, friendly one, every point its own.
			int homePlanet = -1;
			for (int planetIndex: system.Planets) {
				if (campaign.Planets[planetIndex].Parent < 0 && campaign.Planets[planetIndex].Type != PlanetType::GasGiant) {
					homePlanet = planetIndex;
					break;
				}
			}
			if (homePlanet < 0) {
				homePlanet = system.Planets.front();
			}
			Planet& home = campaign.Planets[homePlanet];
			home.Type = (home.Type == PlanetType::GasGiant || home.Type == PlanetType::Volcanic) ? PlanetType::Temperate : home.Type;
			home.Size = std::max(home.Size, 1.25F);
			MakePoints(dice, home, 5);
			for (Point& point: home.Points) {
				point.Owner = faction;
				point.Militia = 0.0F;
			}
			home.Points[0].Settlement = 2;
			home.Points[0].Built[static_cast<size_t>(Building::Barracks)] = true;
			home.Points[0].Militia = c_MilitiaPerLevel * 2.0F;
			campaign.Factions[f].HomePlanet = homePlanet;
			const int army = NewArmy(campaign, faction, homes[f], homePlanet, 0);
			campaign.Armies[army].Moves = c_MovesPerTurn;
			const Faction& made = campaign.Factions[f];
			for (int squad = 0; squad < 4; ++squad) {
				// Two of the cheapest, then the next ones up of the first tier.
				int type = squad < 2 ? 0 : std::min(squad - 1, static_cast<int>(made.Squads.size()) - 1);
				if (made.Squads[type].Tier > 1) {
					type = 0;
				}
				campaign.Armies[army].Squads.push_back(NewSquad(campaign, made, type));
			}
		}
		AddLog(campaign, "The campaign begins. Take more than half of a planet's points to control it.");
		return campaign;
	}
#pragma endregion

#pragma region Reading the state
	PlanetControl ControlOf(const Campaign& campaign, int planet) {
		PlanetControl control;
		const std::vector<Point>& points = campaign.Planets[planet].Points;
		if (points.empty()) {
			return control;
		}
		std::vector<int> counts(campaign.Factions.size(), 0);
		bool anyOwned = false;
		for (const Point& point: points) {
			if (point.Owner >= 0 && point.Owner < static_cast<int>(counts.size())) {
				++counts[point.Owner];
				anyOwned = true;
			}
		}
		for (int faction = 0; faction < static_cast<int>(counts.size()); ++faction) {
			if (counts[faction] * 2 > static_cast<int>(points.size())) {
				control.Controller = faction;
			}
		}
		if (control.Controller >= 0) {
			control.Invaded = std::any_of(points.begin(), points.end(), [&](const Point& point) { return point.Owner >= 0 && point.Owner != control.Controller; });
		} else {
			control.Contested = anyOwned;
		}
		return control;
	}

	int PointIncome(const Campaign& campaign, int planet, int point) {
		const Point& here = campaign.Planets[planet].Points[point];
		if (here.Owner < 0) {
			return 0;
		}
		float income = static_cast<float>(c_PointIncome + c_SettlementIncome * here.Settlement);
		if (here.Settlement > 0 && here.Built[static_cast<size_t>(Building::Market)]) {
			income += static_cast<float>(c_MarketIncome + 15 * here.Settlement);
		}
		const PlanetControl control = ControlOf(campaign, planet);
		if (control.Controller == here.Owner && control.Invaded) {
			income *= c_InvadedIncome;
		}
		return static_cast<int>(income);
	}

	int ArmyUpkeep(const Campaign& campaign, const Army& army) {
		const Faction& faction = campaign.Factions[army.Faction];
		float upkeep = 0.0F;
		for (const Squad& squad: army.Squads) {
			upkeep += faction.Squads[std::clamp(squad.Type, 0, static_cast<int>(faction.Squads.size()) - 1)].SoldierCost * c_UpkeepRate * static_cast<float>(squad.Soldiers.size());
		}
		return static_cast<int>(upkeep);
	}

	int UpkeepOf(const Campaign& campaign, int faction) {
		int upkeep = 0;
		for (const Army& army: campaign.Armies) {
			if (army.Faction == faction) {
				upkeep += ArmyUpkeep(campaign, army);
			}
		}
		return upkeep;
	}

	int IncomeOf(const Campaign& campaign, int faction) {
		int income = 0;
		for (int planet = 0; planet < static_cast<int>(campaign.Planets.size()); ++planet) {
			for (int point = 0; point < static_cast<int>(campaign.Planets[planet].Points.size()); ++point) {
				if (campaign.Planets[planet].Points[point].Owner == faction) {
					income += PointIncome(campaign, planet, point);
				}
			}
			const PlanetControl control = ControlOf(campaign, planet);
			if (control.Controller == faction && !control.Invaded) {
				income += c_ControlBonus;
			}
		}
		return income - UpkeepOf(campaign, faction);
	}

	int SoldierCount(const Army& army) {
		int count = 0;
		for (const Squad& squad: army.Squads) {
			count += static_cast<int>(squad.Soldiers.size());
		}
		return count;
	}

	float Strength(const Campaign& campaign, const Army& army) {
		const Faction& faction = campaign.Factions[army.Faction];
		float strength = 0.0F;
		for (const Squad& squad: army.Squads) {
			const SquadType& type = faction.Squads[std::clamp(squad.Type, 0, static_cast<int>(faction.Squads.size()) - 1)];
			for (const Soldier& soldier: squad.Soldiers) {
				strength += SoldierStrength(type, soldier);
			}
		}
		return strength;
	}

	float PointDefence(const Campaign& campaign, int planet, int point) {
		const Point& here = campaign.Planets[planet].Points[point];
		const float walls = here.Built[static_cast<size_t>(Building::Walls)] && here.Settlement > 0 ? c_WallsBonus : 1.0F;
		return here.Militia * 1.2F * walls;
	}

	std::vector<int> ArmiesAt(const Campaign& campaign, int planet, int point) {
		std::vector<int> armies;
		for (int army = 0; army < static_cast<int>(campaign.Armies.size()); ++army) {
			if (campaign.Armies[army].Planet == planet && campaign.Armies[army].Point == point) {
				armies.push_back(army);
			}
		}
		return armies;
	}

	std::vector<int> ArmiesInOrbit(const Campaign& campaign, int system) {
		std::vector<int> armies;
		for (int army = 0; army < static_cast<int>(campaign.Armies.size()); ++army) {
			if (campaign.Armies[army].System == system && campaign.Armies[army].Planet < 0) {
				armies.push_back(army);
			}
		}
		return armies;
	}

	int FindArmy(const Campaign& campaign, long id) {
		for (int army = 0; army < static_cast<int>(campaign.Armies.size()); ++army) {
			if (campaign.Armies[army].Id == id) {
				return army;
			}
		}
		return -1;
	}

	int PlayerFaction(const Campaign& campaign) {
		for (int faction = 0; faction < static_cast<int>(campaign.Factions.size()); ++faction) {
			if (campaign.Factions[faction].Human && campaign.Factions[faction].Alive) {
				return faction;
			}
		}
		return -1;
	}

	std::vector<int> LanePath(const Campaign& campaign, int from, int to) {
		std::vector<int> path;
		if (from == to || from < 0 || to < 0) {
			return path;
		}
		std::vector<int> cameFrom(campaign.Systems.size(), -1);
		std::queue<int> open;
		cameFrom[from] = from;
		open.push(from);
		while (!open.empty() && cameFrom[to] < 0) {
			const int at = open.front();
			open.pop();
			for (int next: campaign.Systems[at].Lanes) {
				if (cameFrom[next] < 0) {
					cameFrom[next] = at;
					open.push(next);
				}
			}
		}
		if (cameFrom[to] < 0) {
			return path;
		}
		for (int at = to; at != from; at = cameFrom[at]) {
			path.push_back(at);
		}
		std::reverse(path.begin(), path.end());
		return path;
	}

	bool CanRecruit(const Campaign& campaign, int faction, int planet, int point, int type, std::string& why) {
		const Faction& self = campaign.Factions[faction];
		const Point& here = campaign.Planets[planet].Points[point];
		if (type < 0 || type >= static_cast<int>(self.Squads.size())) {
			why = "No such squad.";
			return false;
		}
		const SquadType& squad = self.Squads[type];
		if (here.Owner != faction || here.Settlement == 0) {
			why = "Squads are recruited at your settlements.";
			return false;
		}
		if (squad.Tier >= 2 && !here.Built[static_cast<size_t>(Building::Barracks)]) {
			why = "Needs Barracks here.";
			return false;
		}
		if (squad.Tier >= 3 && (here.Settlement < 3 || ControlOf(campaign, planet).Invaded)) {
			why = here.Settlement < 3 ? "Needs a city with Barracks." : "Not while this planet is invaded.";
			return false;
		}
		const std::vector<int> armies = ArmiesAt(campaign, planet, point);
		bool room = armies.empty();
		for (int army: armies) {
			room = room || (campaign.Armies[army].Faction == faction && static_cast<int>(campaign.Armies[army].Squads.size()) < c_MaxSquads);
		}
		if (!room) {
			why = "The army here is full (12 squads).";
			return false;
		}
		const int cost = static_cast<int>(squad.SoldierCost * static_cast<float>(squad.Size));
		if (self.Gold < cost) {
			why = "Not enough gold.";
			return false;
		}
		return true;
	}

	float WinChance(float attack, float defence) {
		if (defence <= 0.0F) {
			return 1.0F;
		}
		// The two rolls (0.85 to 1.15 each), tried on a grid.
		int wins = 0;
		constexpr int c_Steps = 24;
		for (int a = 0; a < c_Steps; ++a) {
			for (int d = 0; d < c_Steps; ++d) {
				const float rolledAttack = attack * (0.85F + 0.3F * (static_cast<float>(a) + 0.5F) / c_Steps);
				const float rolledDefence = defence * (0.85F + 0.3F * (static_cast<float>(d) + 0.5F) / c_Steps);
				wins += rolledAttack > rolledDefence ? 1 : 0;
			}
		}
		return static_cast<float>(wins) / static_cast<float>(c_Steps * c_Steps);
	}
#pragma endregion

#pragma region Commands
	bool OrderMove(Campaign& campaign, int army, int system) {
		if (army < 0 || army >= static_cast<int>(campaign.Armies.size()) || campaign.Winner >= 0) {
			return false;
		}
		Army& moving = campaign.Armies[army];
		std::vector<int> path = LanePath(campaign, moving.System, system);
		if (path.empty()) {
			return false;
		}
		moving.Path = path;
		FollowPath(campaign, army);
		return true;
	}

	bool WouldFight(const Campaign& campaign, int faction, int planet, int point, float& defence) {
		defence = 0.0F;
		const Point& here = campaign.Planets[planet].Points[point];
		const float walls = here.Built[static_cast<size_t>(Building::Walls)] && here.Settlement > 0 ? c_WallsBonus : 1.0F;
		for (int army: ArmiesAt(campaign, planet, point)) {
			if (campaign.Armies[army].Faction != faction) {
				defence += Strength(campaign, campaign.Armies[army]) * walls;
			}
		}
		if (here.Owner != faction) {
			defence += PointDefence(campaign, planet, point);
		}
		return defence > 0.01F;
	}

	bool OrderLand(Campaign& campaign, int army, int planet, int point) {
		if (army < 0 || army >= static_cast<int>(campaign.Armies.size()) || campaign.Winner >= 0) {
			return false;
		}
		Army& landing = campaign.Armies[army];
		if (landing.Planet >= 0 || landing.Moves < 1 || planet < 0 || planet >= static_cast<int>(campaign.Planets.size()) || campaign.Planets[planet].System != landing.System || point < 0 || point >= static_cast<int>(campaign.Planets[planet].Points.size())) {
			return false;
		}
		landing.Path.clear();
		--landing.Moves;
		EnterPoint(campaign, army, planet, point);
		return true;
	}

	bool OrderOverland(Campaign& campaign, int army, int point) {
		if (army < 0 || army >= static_cast<int>(campaign.Armies.size()) || campaign.Winner >= 0) {
			return false;
		}
		Army& moving = campaign.Armies[army];
		if (moving.Planet < 0 || moving.Moves < 1) {
			return false;
		}
		const std::vector<int>& links = campaign.Planets[moving.Planet].Points[moving.Point].Links;
		if (std::find(links.begin(), links.end(), point) == links.end()) {
			return false;
		}
		moving.Path.clear();
		--moving.Moves;
		EnterPoint(campaign, army, moving.Planet, point);
		return true;
	}

	bool OrderLaunch(Campaign& campaign, int army) {
		if (army < 0 || army >= static_cast<int>(campaign.Armies.size())) {
			return false;
		}
		Army& launching = campaign.Armies[army];
		if (launching.Planet < 0 || launching.Moves < 1) {
			return false;
		}
		launching.Planet = -1;
		launching.Point = -1;
		--launching.Moves;
		return true;
	}

	bool Recruit(Campaign& campaign, int faction, int planet, int point, int type) {
		std::string why;
		if (!CanRecruit(campaign, faction, planet, point, type, why)) {
			return false;
		}
		Faction& self = campaign.Factions[faction];
		self.Gold -= static_cast<int>(self.Squads[type].SoldierCost * static_cast<float>(self.Squads[type].Size));
		int into = -1;
		for (int army: ArmiesAt(campaign, planet, point)) {
			if (campaign.Armies[army].Faction == faction && static_cast<int>(campaign.Armies[army].Squads.size()) < c_MaxSquads) {
				into = army;
				break;
			}
		}
		if (into < 0) {
			into = NewArmy(campaign, faction, campaign.Planets[planet].System, planet, point);
		}
		campaign.Armies[into].Squads.push_back(NewSquad(campaign, self, type));
		return true;
	}

	int ReplenishCost(const Campaign& campaign, int army) {
		if (army < 0 || army >= static_cast<int>(campaign.Armies.size())) {
			return 0;
		}
		const Army& self = campaign.Armies[army];
		const Faction& faction = campaign.Factions[self.Faction];
		float cost = 0.0F;
		for (const Squad& squad: self.Squads) {
			const SquadType& type = faction.Squads[std::clamp(squad.Type, 0, static_cast<int>(faction.Squads.size()) - 1)];
			cost += type.SoldierCost * static_cast<float>(std::max(type.Size - static_cast<int>(squad.Soldiers.size()), 0));
		}
		return static_cast<int>(cost);
	}

	bool Replenish(Campaign& campaign, int army) {
		const int cost = ReplenishCost(campaign, army);
		if (cost <= 0) {
			return false;
		}
		Army& self = campaign.Armies[army];
		Faction& faction = campaign.Factions[self.Faction];
		if (self.Planet < 0 || faction.Gold < cost) {
			return false;
		}
		const Point& here = campaign.Planets[self.Planet].Points[self.Point];
		if (here.Owner != self.Faction || here.Settlement == 0) {
			return false;
		}
		faction.Gold -= cost;
		for (Squad& squad: self.Squads) {
			const SquadType& type = faction.Squads[std::clamp(squad.Type, 0, static_cast<int>(faction.Squads.size()) - 1)];
			while (static_cast<int>(squad.Soldiers.size()) < type.Size) {
				squad.Soldiers.push_back(NewSoldier(campaign));
			}
		}
		return true;
	}

	int SettleCost(int level) {
		static constexpr int c_Costs[] = {300, 700, 1400};
		return c_Costs[std::clamp(level, 0, 2)];
	}

	bool Settle(Campaign& campaign, int faction, int planet, int point) {
		Point& here = campaign.Planets[planet].Points[point];
		if (here.Owner != faction || here.Settlement >= c_MaxSettlement) {
			return false;
		}
		const int cost = SettleCost(here.Settlement);
		if (campaign.Factions[faction].Gold < cost) {
			return false;
		}
		campaign.Factions[faction].Gold -= cost;
		++here.Settlement;
		if (IsHuman(campaign, faction)) {
			AddLog(campaign, here.Name + " is now " + (here.Settlement == 1 ? "an " : "a ") + SettlementName(here.Settlement) + ".");
		}
		return true;
	}

	bool Build(Campaign& campaign, int faction, int planet, int point, Building building) {
		Point& here = campaign.Planets[planet].Points[point];
		const size_t which = static_cast<size_t>(building);
		if (here.Owner != faction || here.Settlement == 0 || here.Built[which] || campaign.Factions[faction].Gold < BuildingCost(building)) {
			return false;
		}
		int built = 0;
		for (bool each: here.Built) {
			built += each ? 1 : 0;
		}
		// A building for each level of settlement, and one more.
		if (built >= here.Settlement + 1) {
			return false;
		}
		campaign.Factions[faction].Gold -= BuildingCost(building);
		here.Built[which] = true;
		return true;
	}

	bool Merge(Campaign& campaign, int into, int from) {
		if (into == from || into < 0 || from < 0 || into >= static_cast<int>(campaign.Armies.size()) || from >= static_cast<int>(campaign.Armies.size())) {
			return false;
		}
		Army& target = campaign.Armies[into];
		Army& source = campaign.Armies[from];
		if (target.Faction != source.Faction || target.System != source.System || target.Planet != source.Planet || target.Point != source.Point) {
			return false;
		}
		while (!source.Squads.empty() && static_cast<int>(target.Squads.size()) < c_MaxSquads) {
			target.Squads.push_back(source.Squads.front());
			source.Squads.erase(source.Squads.begin());
		}
		target.Moves = std::min(target.Moves, source.Moves);
		RemoveEmptyArmies(campaign);
		return true;
	}

	bool SplitSquad(Campaign& campaign, int army, int squad) {
		if (army < 0 || army >= static_cast<int>(campaign.Armies.size()) || squad < 0 || squad >= static_cast<int>(campaign.Armies[army].Squads.size()) || campaign.Armies[army].Squads.size() < 2) {
			return false;
		}
		const Army source = campaign.Armies[army];
		const int made = NewArmy(campaign, source.Faction, source.System, source.Planet, source.Point);
		campaign.Armies[made].Moves = source.Moves;
		campaign.Armies[made].Squads.push_back(source.Squads[squad]);
		campaign.Armies[army].Squads.erase(campaign.Armies[army].Squads.begin() + squad);
		return true;
	}

	void Disband(Campaign& campaign, int army) {
		if (army >= 0 && army < static_cast<int>(campaign.Armies.size())) {
			campaign.Armies.erase(campaign.Armies.begin() + army);
			CheckFactions(campaign);
		}
	}

	void EndTurn(Campaign& campaign) {
		if (campaign.Winner >= 0) {
			return;
		}
		for (int faction = 0; faction < static_cast<int>(campaign.Factions.size()); ++faction) {
			if (!campaign.Factions[faction].Human && campaign.Factions[faction].Alive) {
				AITurn(campaign, faction);
			}
		}
		NewTurn(campaign);
	}
#pragma endregion

	float Roll(Campaign& campaign) {
		const std::uint64_t bits = SplitMix((static_cast<std::uint64_t>(campaign.Seed) << 32) ^ campaign.Rolls++);
		return static_cast<float>(bits >> 40) / static_cast<float>(1u << 24);
	}

	void AddLog(Campaign& campaign, const std::string& line) {
		campaign.Log.push_back("Turn " + std::to_string(campaign.Turn) + ": " + line);
		if (campaign.Log.size() > 200) {
			campaign.Log.erase(campaign.Log.begin(), campaign.Log.begin() + static_cast<long>(campaign.Log.size() - 200));
		}
	}
} // namespace RTE::CampaignModel
