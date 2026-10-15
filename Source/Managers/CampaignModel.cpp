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
		constexpr float c_WallsBonusMetal = 1.6F; //!< Walls with the second Metal bonus.

		// Ships.
		constexpr float c_ShipPower[] = {0.0F, 10.0F, 25.0F}; //!< A ship's power in space at full hull, by kind.
		constexpr int c_ShipUpkeep[] = {0, 25, 60}; //!< Gold a turn a ship costs, by kind.
		constexpr float c_SunkLoss = 0.6F; //!< The share of its soldiers an army loses when its warship is destroyed.
		constexpr float c_HullRepair = 0.1F; //!< Hull a ship gets back each turn anywhere, more in orbit over its faction's Shipyard.

		// Resources.
		constexpr float c_BasePrices[c_ResourceCount] = {40.0F, 30.0F, 50.0F, 45.0F, 120.0F, 400.0F};
		constexpr Stock c_StartStock = {5, 10, 8, 6, 2, 0};
		constexpr float c_MarketCut = 0.1F; //!< The market buys this much under its price and sells this much over.
		constexpr float c_PriceStep = 0.03F; //!< How far a price moves for each unit bought or sold.
		constexpr float c_PriceDrift = 0.15F; //!< How far a price moves back toward normal each turn.
		constexpr int c_RelicGold[3] = {0, 50, 150}; //!< Gold a turn from the Relics bonus tiers.

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

		/// How much harder a point's defenders fight for its walls (none: 1).
		float WallsFactor(const Campaign& campaign, const Point& point) {
			if (!point.Built[static_cast<size_t>(Building::Walls)] || point.Settlement <= 0) {
				return 1.0F;
			}
			return point.Owner >= 0 && BonusTier(campaign, point.Owner, Resource::Metal) >= 2 ? c_WallsBonusMetal : c_WallsBonus;
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
			} else if (int winner = -1; campaign.Winner < 0 && AlliedWin(campaign, winner)) {
				campaign.Winner = winner;
				std::string names;
				for (size_t index = 0; index < alive.size(); ++index) {
					names += (index == 0 ? "" : index + 1 == alive.size() ? " and " : ", ") + campaign.Factions[alive[index]].Name;
				}
				AddLog(campaign, names + " rule the galaxy together!");
			}
			campaign.Lost = std::none_of(campaign.Factions.begin(), campaign.Factions.end(), [](const Faction& faction) { return faction.Human && faction.Alive; });
		}

		bool WantsToFlee(const Campaign& campaign, const Army& army, float holdChance);
		std::string Flee(Campaign& campaign, int index, int& lost);

		/// An army moving onto a point: taken if nobody defends it, else an auto-resolved battle. Returns whether the army is there now.
		/// army is an index; it may move in the list, so it's found again by ID afterwards.
		/// After a battle fought for real: the soldiers not among the survivors fall, the rest keep the health they ended on. Returns
		/// how many fell.
		int ApplySurvivors(Campaign& campaign, const std::vector<int>& armies, const std::unordered_map<long, float>& survivors) {
			int fell = 0;
			for (int army: armies) {
				for (Squad& squad: campaign.Armies[army].Squads) {
					fell += static_cast<int>(std::erase_if(squad.Soldiers, [&survivors](const Soldier& soldier) { return !survivors.contains(soldier.Id); }));
					for (Soldier& soldier: squad.Soldiers) {
						soldier.Health = std::clamp(survivors.at(soldier.Id), 0.1F, 1.0F);
					}
				}
				std::erase_if(campaign.Armies[army].Squads, [](const Squad& squad) { return squad.Soldiers.empty(); });
			}
			return fell;
		}

		/// An army moving onto a point: a battle when it's defended, auto-resolved, or with the result of one fought for real.
		bool EnterPoint(Campaign& campaign, int armyIndex, int planetIndex, int pointIndex, const FoughtBattle* fought) {
			const long armyID = campaign.Armies[armyIndex].Id;
			const int faction = campaign.Armies[armyIndex].Faction;
			Planet& planet = campaign.Planets[planetIndex];
			Point& point = planet.Points[pointIndex];
			const int owner = point.Owner;
			std::vector<int> defenders;
			for (int other: ArmiesAt(campaign, planetIndex, pointIndex)) {
				if (campaign.Armies[other].Faction != faction && !Allied(campaign, faction, campaign.Armies[other].Faction)) {
					defenders.push_back(other);
				}
			}
			// An ally's point is walked onto, not taken.
			const bool friendlyGround = owner == faction || Allied(campaign, faction, owner);
			const float militia = friendlyGround ? 0.0F : PointDefence(campaign, planetIndex, pointIndex);
			// Whoever this attacks is now at war with it.
			if (!friendlyGround && owner >= 0) {
				DeclareWar(campaign, faction, owner);
			}
			for (int defender: defenders) {
				DeclareWar(campaign, faction, campaign.Armies[defender].Faction);
			}
			// Defenders told to flee (with a move left) get away first, at a cost. Not from a battle already fought for real.
			std::vector<std::string> fled;
			int firstFled = -1;
			if (!fought && !defenders.empty()) {
				float holding = militia;
				for (int defender: defenders) {
					holding += Strength(campaign, campaign.Armies[defender]) * WallsFactor(campaign, point);
				}
				const float hold = 1.0F - WinChance(Strength(campaign, campaign.Armies[armyIndex]), holding);
				std::vector<int> staying;
				for (int defender: defenders) {
					if (WantsToFlee(campaign, campaign.Armies[defender], hold)) {
						int lost = 0;
						const std::string where = Flee(campaign, defender, lost);
						firstFled = firstFled < 0 ? campaign.Armies[defender].Faction : firstFled;
						fled.push_back(campaign.Armies[defender].Name + " fled " + where + ", losing " + std::to_string(lost) + " soldier" + (lost == 1 ? "" : "s") + ".");
						AddLog(campaign, campaign.Armies[defender].Name + " (" + campaign.Factions[campaign.Armies[defender].Faction].Name + ") fled from " + point.Name + " on " + planet.Name + ".");
					} else {
						staying.push_back(defender);
					}
				}
				defenders = std::move(staying);
			}
			auto arrive = [&]() {
				Army& army = campaign.Armies[FindArmy(campaign, armyID)];
				army.Planet = planetIndex;
				army.Point = pointIndex;
				army.Path.clear();
				if (point.Owner != faction && !Allied(campaign, faction, point.Owner)) {
					const int previous = point.Owner;
					point.Owner = faction;
					point.Militia = 0.0F;
					AddLog(campaign, campaign.Factions[faction].Name + " took " + point.Name + " on " + planet.Name + (previous >= 0 ? " from " + FactionName(campaign, previous) : std::string()) + ".");
				}
			};
			if (defenders.empty() && militia < 0.01F) {
				arrive();
				if (!fled.empty()) {
					// Nobody stayed to fight: a report of who fled, and no battle.
					BattleOutcome outcome;
					outcome.Happened = true;
					outcome.Fled = true;
					outcome.AttackerWon = true;
					outcome.Attacker = faction;
					outcome.Defender = firstFled;
					outcome.Where = point.Name + " on " + planet.Name;
					outcome.Planet = planetIndex;
					outcome.Point = pointIndex;
					outcome.Notes = fled;
					outcome.PlayerInvolved = IsHuman(campaign, faction) || IsHuman(campaign, firstFled);
					if (outcome.PlayerInvolved) {
						campaign.Reports.push_back(outcome);
					}
					if (campaign.Battles.size() < 64) {
						campaign.Battles.push_back(outcome);
					}
				}
				CheckFactions(campaign);
				return true;
			}

			// The battle: auto-resolved, or as it was fought.
			BattleOutcome outcome;
			outcome.Happened = true;
			outcome.Attacker = faction;
			outcome.Defender = !defenders.empty() ? campaign.Armies[defenders.front()].Faction : owner;
			outcome.Where = point.Name + " on " + planet.Name;
			outcome.Planet = planetIndex;
			outcome.Point = pointIndex;
			outcome.Notes = fled;
			const float walls = WallsFactor(campaign, point);
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
			const std::vector<int> attackers = {armyIndex};
			if (fought) {
				// Fought for real: who fell and who won is what happened on the battlefield.
				outcome.AttackerWon = fought->AttackerWon;
				outcome.Fought = true;
				outcome.AttackerLost = ApplySurvivors(campaign, attackers, fought->Survivors);
				const int localsLost = std::clamp(militiaSoldiers - fought->LocalsLeft, 0, militiaSoldiers);
				outcome.DefenderLost = ApplySurvivors(campaign, defenders, fought->Survivors) + (outcome.AttackerWon ? militiaSoldiers : localsLost);
				if (outcome.AttackerWon) {
					ShareKills(campaign, attackers, outcome.DefenderLost, outcome.Promotions);
				} else {
					ShareKills(campaign, defenders, outcome.AttackerLost, outcome.Promotions);
					if (militia > 0.0F && militiaSoldiers > 0) {
						point.Militia = std::max(0.0F, point.Militia * static_cast<float>(militiaSoldiers - localsLost) / static_cast<float>(militiaSoldiers));
					}
				}
			} else {
				const float rolledAttack = attack * (0.85F + 0.3F * Roll(campaign));
				const float rolledDefence = defence * (0.85F + 0.3F * Roll(campaign));
				outcome.AttackerWon = rolledAttack > rolledDefence;
				const float ratio = std::clamp(outcome.AttackerWon ? rolledDefence / std::max(rolledAttack, 0.01F) : rolledAttack / std::max(rolledDefence, 0.01F), 0.0F, 1.0F);
				const float winnerLoss = 0.08F + 0.45F * std::pow(ratio, 1.5F);
				const float loserLoss = 0.5F + 0.35F * (1.0F - ratio);
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

		/// A fleet that lost a space battle falls back into orbit of a system next door along the lanes: one with no warship hostile to
		/// it, its own points first. With nowhere to go, it stays.
		void FallBack(Campaign& campaign, int armyIndex, int from) {
			Army& army = campaign.Armies[armyIndex];
			int best = -1;
			int bestScore = -1;
			for (int next: campaign.Systems[army.System].Lanes) {
				bool guarded = false;
				for (int other: ArmiesInOrbit(campaign, next)) {
					guarded = guarded || (Armed(campaign.Armies[other]) && StanceOf(campaign, army.Faction, campaign.Armies[other].Faction) == Stance::War);
				}
				if (guarded) {
					continue;
				}
				int score = next == from ? 1 : 0;
				for (int planet: campaign.Systems[next].Planets) {
					for (const Point& point: campaign.Planets[planet].Points) {
						score += point.Owner == army.Faction ? 2 : 0;
					}
				}
				if (score > bestScore) {
					bestScore = score;
					best = next;
				}
			}
			army.Planet = -1;
			army.Point = -1;
			army.Moves = 0;
			army.Path.clear();
			if (best >= 0) {
				army.System = best;
			}
		}

		/// Whether an army that's attacked, with this chance of holding, flees: by its order, and only with a move left. AI factions flee
		/// when they'd likely lose badly, but never from their home planet.
		bool WantsToFlee(const Campaign& campaign, const Army& army, float holdChance) {
			if (army.Moves < 1) {
				return false;
			}
			if (!campaign.Factions[army.Faction].Human) {
				return holdChance < 0.3F && (army.Planet < 0 || army.Planet != campaign.Factions[army.Faction].HomePlanet);
			}
			return army.IfAttacked == static_cast<int>(OnAttack::Flee) || (army.IfAttacked == static_cast<int>(OnAttack::FleeIfLosing) && holdChance < 0.4F);
		}

		/// An army fleeing: it loses c_FleeLoss of its soldiers (and some hull, in space), spends a move, and goes to the nearest safe
		/// place. On a planet, that's the nearest point along the roads that it or an ally holds with no enemy on it, else up into orbit;
		/// in orbit, a system next door (as FallBack). Returns where it went ("to X"), for the report.
		std::string Flee(Campaign& campaign, int index, int& lost) {
			lost = TakeCasualties(campaign, {index}, c_FleeLoss);
			Army& army = campaign.Armies[index];
			army.Moves = std::max(0, army.Moves - 1);
			army.Path.clear();
			if (army.Planet >= 0) {
				const Planet& planet = campaign.Planets[army.Planet];
				std::vector<bool> seen(planet.Points.size(), false);
				std::queue<int> next;
				next.push(army.Point);
				seen[army.Point] = true;
				while (!next.empty()) {
					const int at = next.front();
					next.pop();
					if (at != army.Point) {
						const Point& here = planet.Points[at];
						bool safe = here.Owner == army.Faction || (here.Owner >= 0 && Allied(campaign, army.Faction, here.Owner));
						for (int other: ArmiesAt(campaign, army.Planet, at)) {
							safe = safe && Friendly(campaign, army.Faction, campaign.Armies[other].Faction);
						}
						if (safe) {
							army.Point = at;
							return "to " + here.Name;
						}
					}
					for (int link: planet.Points[at].Links) {
						if (link >= 0 && link < static_cast<int>(seen.size()) && !seen[link]) {
							seen[link] = true;
							next.push(link);
						}
					}
				}
				army.Planet = -1;
				army.Point = -1;
				return "up into orbit";
			}
			if (Armed(army)) {
				army.Hull = std::max(0.05F, army.Hull - 0.2F);
			}
			const int moves = army.Moves;
			const int from = army.System;
			FallBack(campaign, index, -1);
			campaign.Armies[index].Moves = moves;
			return campaign.Armies[index].System != from ? "to " + campaign.Systems[campaign.Armies[index].System].Name : std::string("but had nowhere to go");
		}

		/// A space battle in orbit: an attacking warship against the warships it meets (all in the same system). Ships lose hull, the
		/// winner less; a ship knocked out takes most of its army's soldiers with it, and the survivors carry on in a transport. The
		/// losing side's fleets fall back to a system next door. Returns whether the attacker won (and is still there).
		bool SpaceBattle(Campaign& campaign, int attackerIndex, std::vector<int> defenders, int cameFrom = -1) {
			const long attackerID = campaign.Armies[attackerIndex].Id;
			const int faction = campaign.Armies[attackerIndex].Faction;
			const int system = campaign.Armies[attackerIndex].System;
			BattleOutcome outcome;
			outcome.Happened = true;
			outcome.Space = true;
			outcome.System = system;
			outcome.Attacker = faction;
			outcome.Defender = campaign.Armies[defenders.front()].Faction;
			outcome.Where = "the orbit of " + campaign.Systems[system].Name;
			for (int defender: defenders) {
				if (StanceOf(campaign, faction, campaign.Armies[defender].Faction) != Stance::War) {
					DeclareWar(campaign, faction, campaign.Armies[defender].Faction);
				}
			}
			// Fleets told to flee (with a move left) get away first, at a cost.
			{
				float holding = 0.0F;
				for (int defender: defenders) {
					holding += SpacePower(campaign.Armies[defender]);
				}
				const float hold = 1.0F - WinChance(SpacePower(campaign.Armies[attackerIndex]), holding);
				std::vector<int> staying;
				for (int defender: defenders) {
					if (WantsToFlee(campaign, campaign.Armies[defender], hold)) {
						int lost = 0;
						const std::string name = campaign.Armies[defender].Name;
						const std::string where = Flee(campaign, defender, lost);
						outcome.Notes.push_back(name + " fled " + where + ", losing " + std::to_string(lost) + " soldier" + (lost == 1 ? "" : "s") + " and some hull.");
						AddLog(campaign, name + " fled a space battle over " + campaign.Systems[system].Name + ".");
					} else {
						staying.push_back(defender);
					}
				}
				defenders = std::move(staying);
			}
			if (defenders.empty()) {
				outcome.Fled = true;
				outcome.AttackerWon = true;
				if (outcome.PlayerInvolved = IsHuman(campaign, faction) || IsHuman(campaign, outcome.Defender); outcome.PlayerInvolved) {
					campaign.Reports.push_back(outcome);
				}
				if (campaign.Battles.size() < 64) {
					campaign.Battles.push_back(outcome);
				}
				return true;
			}
			const float attack = SpacePower(campaign.Armies[attackerIndex]);
			float defence = 0.0F;
			for (int defender: defenders) {
				defence += SpacePower(campaign.Armies[defender]);
				outcome.DefenderSoldiers += SoldierCount(campaign.Armies[defender]);
			}
			outcome.AttackerSoldiers = SoldierCount(campaign.Armies[attackerIndex]);
			outcome.AttackStrength = attack;
			outcome.DefendStrength = defence;
			outcome.PlayerInvolved = IsHuman(campaign, faction) || IsHuman(campaign, outcome.Defender);
			const float rolledAttack = attack * (0.85F + 0.3F * Roll(campaign));
			const float rolledDefence = defence * (0.85F + 0.3F * Roll(campaign));
			outcome.AttackerWon = rolledAttack > rolledDefence;
			const float ratio = std::clamp(outcome.AttackerWon ? rolledDefence / std::max(rolledAttack, 0.01F) : rolledAttack / std::max(rolledDefence, 0.01F), 0.0F, 1.0F);
			const float winnerDamage = 0.1F + 0.35F * std::pow(ratio, 1.5F);
			const float loserDamage = 0.45F + 0.4F * (1.0F - ratio);
			const std::vector<long> attackers = {attackerID};
			std::vector<long> defenderIDs;
			for (int defender: defenders) {
				defenderIDs.push_back(campaign.Armies[defender].Id);
			}
			// Hull lost, and ships knocked out, side by side.
			auto damage = [&](const std::vector<long>& ids, float amount, bool lost, int back, int& soldiersLost) {
				for (long id: ids) {
					const int index = FindArmy(campaign, id);
					if (index < 0) {
						continue;
					}
					Army& army = campaign.Armies[index];
					army.Hull -= amount * (0.85F + 0.3F * Roll(campaign));
					if (army.Hull <= 0.05F) {
						const std::string ship = ShipName(army.Ship);
						const int fell = TakeCasualties(campaign, {index}, c_SunkLoss);
						soldiersLost += fell;
						Army& after = campaign.Armies[index];
						after.Ship = static_cast<int>(Ship::Transport);
						after.Hull = 1.0F;
						outcome.Notes.push_back(after.Name + "'s " + ship + " was destroyed: " + std::to_string(fell) + " soldiers went down with it, the rest got away in a transport.");
					} else {
						army.Hull = std::max(army.Hull, 0.05F);
					}
					if (lost) {
						Army& loser = campaign.Armies[index];
						const int from = loser.System;
						FallBack(campaign, index, back);
						if (campaign.Armies[index].System != from) {
							outcome.Notes.push_back(loser.Name + " fell back to " + campaign.Systems[campaign.Armies[index].System].Name + ".");
						}
					}
				}
			};
			damage(attackers, outcome.AttackerWon ? winnerDamage : loserDamage, !outcome.AttackerWon, cameFrom, outcome.AttackerLost);
			damage(defenderIDs, outcome.AttackerWon ? loserDamage : winnerDamage, outcome.AttackerWon, -1, outcome.DefenderLost);
			AddLog(campaign, "Space battle over " + campaign.Systems[system].Name + ": " + campaign.Factions[faction].Name + " attacked " + FactionName(campaign, outcome.Defender) + "'s fleet and " + (outcome.AttackerWon ? "won." : "lost."));
			RemoveEmptyArmies(campaign);
			if (outcome.PlayerInvolved) {
				campaign.Reports.push_back(outcome);
			}
			if (campaign.Battles.size() < 64) {
				campaign.Battles.push_back(outcome);
			}
			CheckFactions(campaign);
			const int attacker = FindArmy(campaign, attackerID);
			return outcome.AttackerWon && attacker >= 0 && campaign.Armies[attacker].System == system;
		}

		/// The warships in orbit of a system at war with a faction.
		std::vector<int> EnemyWarships(const Campaign& campaign, int faction, int system) {
			std::vector<int> enemies;
			for (int other: ArmiesInOrbit(campaign, system)) {
				if (Armed(campaign.Armies[other]) && StanceOf(campaign, faction, campaign.Armies[other].Faction) == Stance::War) {
					enemies.push_back(other);
				}
			}
			return enemies;
		}

		/// Whether a faction has a Shipyard on a settlement in a system.
		bool ShipyardIn(const Campaign& campaign, int faction, int system) {
			for (int planet: campaign.Systems[system].Planets) {
				for (const Point& point: campaign.Planets[planet].Points) {
					if (point.Owner == faction && point.Settlement > 0 && point.Built[static_cast<size_t>(Building::Shipyard)]) {
						return true;
					}
				}
			}
			return false;
		}

		/// Spends an army's moves on its path: a lane jump at a time, while it has them. Taking off first, if it's on a planet.
		void FollowPath(Campaign& campaign, int army) {
			Army& moving = campaign.Armies[army];
			if (moving.Path.empty()) {
				return;
			}
			Faction& faction = campaign.Factions[moving.Faction];
			if (faction.Resources[static_cast<size_t>(Resource::Fuel)] < 1) {
				// Out of fuel: it waits where it is, on the planet or in orbit.
				if (faction.Human && moving.Moves == c_MovesPerTurn) {
					AddLog(campaign, moving.Name + " has no fuel to jump on. Buy some on the Market.");
				}
				return;
			}
			if (moving.Planet >= 0) {
				const int moves = TakeOffMoves(campaign, moving.Faction);
				if (moving.Moves < std::max(moves, 1)) {
					return;
				}
				moving.Planet = -1;
				moving.Point = -1;
				moving.Moves -= moves;
			}
			const int jump = JumpMoves(campaign, moving.Faction);
			while (!moving.Path.empty() && moving.Moves >= jump && faction.Resources[static_cast<size_t>(Resource::Fuel)] >= 1) {
				const int next = moving.Path.front();
				const std::vector<int>& lanes = campaign.Systems[moving.System].Lanes;
				if (std::find(lanes.begin(), lanes.end(), next) == lanes.end()) {
					moving.Path.clear();
					break;
				}
				const int from = moving.System;
				moving.System = next;
				moving.Path.erase(moving.Path.begin());
				moving.Moves -= jump;
				--faction.Resources[static_cast<size_t>(Resource::Fuel)];
				// A warship jumping in on enemy warships fights them; losing, it falls back. (Transports slip past.)
				if (Armed(moving)) {
					if (const std::vector<int> enemies = EnemyWarships(campaign, moving.Faction, next); !enemies.empty()) {
						const long id = moving.Id;
						if (!SpaceBattle(campaign, army, enemies, from)) {
							if (const int after = FindArmy(campaign, id); after >= 0) {
								campaign.Armies[after].Path.clear();
							}
						}
						return; // (The army list may have changed.)
					}
				}
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

		/// The market's turn: prices drift back toward normal (or toward where an event pulls them), events run down, and now and then
		/// a new one starts.
		void UpdateMarket(Campaign& campaign) {
			for (GalaxyEvent& event: campaign.Events) {
				--event.TurnsLeft;
			}
			std::erase_if(campaign.Events, [](const GalaxyEvent& event) { return event.TurnsLeft <= 0; });
			if (campaign.Events.size() < 2 && Roll(campaign) < 0.08F) {
				struct Kind {
					Resource What;
					float Factor;
					const char* Text;
				};
				static constexpr Kind c_Kinds[] = {
				    {Resource::Water, 2.0F, "Drought on the core worlds: water prices are up."},
				    {Resource::Water, 0.6F, "Comet ice floods the markets: water is cheap."},
				    {Resource::Food, 1.9F, "Blight in the farming belts: food prices are up."},
				    {Resource::Food, 0.6F, "Bumper harvests across the galaxy: food is cheap."},
				    {Resource::Metal, 1.8F, "A war in the far reaches is eating metal: prices are up."},
				    {Resource::Metal, 0.6F, "A new asteroid field is being mined: metal is cheap."},
				    {Resource::Fuel, 2.1F, "A fuel crisis grips the trade lanes: fuel prices are up."},
				    {Resource::Fuel, 0.55F, "A gas giant rush floods the market: fuel is cheap."},
				    {Resource::Crystal, 1.8F, "Collectors are hoarding crystal: prices are up."},
				    {Resource::Relics, 1.7F, "Museums on the core worlds are bidding for relics."},
				};
				const Kind& kind = c_Kinds[std::min(static_cast<size_t>(Roll(campaign) * static_cast<float>(std::size(c_Kinds))), std::size(c_Kinds) - 1)];
				if (std::none_of(campaign.Events.begin(), campaign.Events.end(), [&kind](const GalaxyEvent& event) { return event.Resource == static_cast<int>(kind.What); })) {
					GalaxyEvent event;
					event.Resource = static_cast<int>(kind.What);
					event.PriceFactor = kind.Factor;
					event.TurnsLeft = 4 + static_cast<int>(Roll(campaign) * 5.0F);
					event.Text = kind.Text;
					campaign.Events.push_back(event);
					AddLog(campaign, std::string("Galaxy news: ") + kind.Text);
				}
			}
			for (int resource = 0; resource < c_ResourceCount; ++resource) {
				float target = c_BasePrices[resource];
				for (const GalaxyEvent& event: campaign.Events) {
					if (event.Resource == resource) {
						target *= event.PriceFactor;
					}
				}
				float& price = campaign.Prices[resource];
				price += (target - price) * c_PriceDrift;
				price = std::clamp(price, c_BasePrices[resource] * 0.3F, c_BasePrices[resource] * 4.0F);
			}
		}

		/// An AI faction's trading: buying what it's short of (food and fuel first) and selling what it has too much of.
		void AIMarket(Campaign& campaign, int faction) {
			Faction& self = campaign.Factions[faction];
			const Stock made = ProductionOf(campaign, faction);
			const Stock used = UsageOf(campaign, faction);
			int armies = 0;
			for (const Army& army: campaign.Armies) {
				armies += army.Faction == faction ? 1 : 0;
			}
			const size_t food = static_cast<size_t>(Resource::Food);
			Stock want{};
			want[static_cast<size_t>(Resource::Water)] = 6;
			want[food] = std::max(0, (used[food] - made[food]) * 4) + used[food] + 2;
			want[static_cast<size_t>(Resource::Metal)] = 8;
			want[static_cast<size_t>(Resource::Fuel)] = 3 + armies * 2;
			want[static_cast<size_t>(Resource::Crystal)] = 3;
			static constexpr Resource c_Order[] = {Resource::Food, Resource::Fuel, Resource::Metal, Resource::Water, Resource::Crystal};
			for (Resource resource: c_Order) {
				const size_t index = static_cast<size_t>(resource);
				const int missing = want[index] - self.Resources[index];
				// Food and fuel bought even when gold is low; the rest only with gold to spare.
				const int keep = (resource == Resource::Food || resource == Resource::Fuel) ? 100 : 600;
				if (missing > 0 && self.Gold > keep) {
					const int afford = (self.Gold - keep) / std::max(BuyPrice(campaign, resource), 1);
					if (afford > 0) {
						Trade(campaign, faction, resource, std::min(missing, afford));
					}
				}
			}
			for (int resource = 0; resource < c_ResourceCount; ++resource) {
				const int surplus = resource == static_cast<int>(Resource::Relics) ? self.Resources[resource] - 1 : self.Resources[resource] - want[resource] * 3 - 10;
				if (surplus > 0) {
					Trade(campaign, faction, static_cast<Resource>(resource), -surplus);
				}
			}
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

			AIDiplomacy(campaign, faction);
			AIMarket(campaign, faction);

			// Recruiting at its settlements while its soldiers' upkeep stays under about half its income, keeping some gold back, and
			// while it can feed them.
			const int reserve = 250;
			for (int planet = 0; planet < static_cast<int>(campaign.Planets.size()); ++planet) {
				for (int point = 0; point < static_cast<int>(campaign.Planets[planet].Points.size()); ++point) {
					const Point& here = campaign.Planets[planet].Points[point];
					if (here.Owner != faction || here.Settlement == 0) {
						continue;
					}
					for (int squads = 0; squads < 3; ++squads) {
						const int foodMade = ProductionOf(campaign, faction)[static_cast<size_t>(Resource::Food)];
						const int foodUsed = UsageOf(campaign, faction)[static_cast<size_t>(Resource::Food)];
						if (foodUsed + 1 > foodMade + self.Resources[static_cast<size_t>(Resource::Food)] / 6) {
							break;
						}
						const int upkeep = UpkeepOf(campaign, faction);
						const int gross = IncomeOf(campaign, faction) + upkeep;
						int best = -1;
						for (int type = static_cast<int>(self.Squads.size()) - 1; type >= 0; --type) {
							const float squadCost = static_cast<float>(RecruitCostFor(campaign, faction, type).Gold);
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
						// A warship for armies at a Shipyard, with gold to spare.
						std::string why;
						if (campaign.Armies[army].Faction == faction && CanUpgradeShip(campaign, army, why) && self.Gold - ShipCostFor(campaign, faction, campaign.Armies[army].Ship + 1).Gold >= reserve * 3) {
							UpgradeShip(campaign, army);
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
							built = Build(campaign, faction, planet, point, Building::Walls) || Build(campaign, faction, planet, point, Building::Shipyard);
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
							threat = threat || (other.Faction != faction && !Friendly(campaign, faction, other.Faction) && other.System == self_army.System);
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
							const int owner = target.Points[point].Owner;
							// Not its own or an ally's, nobody it has a treaty with, and not a neutral it gets on with.
							if (owner == faction || Allied(campaign, faction, owner) || TreatyBroken(campaign, faction, planet, point) >= 0 || (owner >= 0 && StanceOf(campaign, faction, owner) == Stance::Neutral && RelationOf(campaign, faction, owner).Opinion >= 40)) {
								continue;
							}
							float defence = 0.0F;
							WouldFight(campaign, faction, planet, point, defence);
							if (defence > 0.0F && WinChance(strength, defence) < nerve) {
								continue;
							}
							// Landing past warships guarding the orbit takes a warship that can beat them.
							if (planet != self_army.Planet) {
								if (const std::vector<int> guards = OrbitGuards(campaign, faction, planet, point); !guards.empty() && (!Armed(self_army) || SpaceWinChance(campaign, self_army, guards) < nerve)) {
									continue;
								}
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
			// Resources: what the points produce, less the armies' food. Factions short of food go hungry.
			std::vector<bool> hungry(campaign.Factions.size(), false);
			std::vector<bool> healthy(campaign.Factions.size(), false);
			for (int faction = 0; faction < static_cast<int>(campaign.Factions.size()); ++faction) {
				Faction& each = campaign.Factions[faction];
				if (!each.Alive) {
					continue;
				}
				const Stock made = ProductionOf(campaign, faction);
				const Stock used = UsageOf(campaign, faction);
				for (int resource = 0; resource < c_ResourceCount; ++resource) {
					each.Resources[resource] += made[resource] - used[resource];
				}
				int& food = each.Resources[static_cast<size_t>(Resource::Food)];
				if (food < 0) {
					food = 0;
					hungry[faction] = true;
					if (each.Human) {
						AddLog(campaign, "Your armies are starving: no food to feed them. They weaken, and the weakest desert. Buy food on the Market, or take points with food.");
					}
				}
				healthy[faction] = BonusTier(campaign, faction, Resource::Water) >= 2;
			}
			for (Army& army: campaign.Armies) {
				if (hungry[army.Faction]) {
					for (Squad& squad: army.Squads) {
						std::erase_if(squad.Soldiers, [](const Soldier& soldier) { return soldier.Health <= 0.15F; });
						for (Soldier& soldier: squad.Soldiers) {
							soldier.Health = std::max(0.1F, soldier.Health - 0.15F);
						}
					}
					std::erase_if(army.Squads, [](const Squad& squad) { return squad.Soldiers.empty(); });
				}
			}
			RemoveEmptyArmies(campaign);
			UpdateMarket(campaign);
			UpdateDiplomacy(campaign);
			for (Army& army: campaign.Armies) {
				army.Moves = c_MovesPerTurn;
				army.Hull = std::min(1.0F, army.Hull + (ShipyardIn(campaign, army.Faction, army.System) ? 0.5F : c_HullRepair));
				if (hungry[army.Faction]) {
					continue; // No healing on an empty stomach.
				}
				float heal = healthy[army.Faction] ? 0.15F : 0.05F;
				if (army.Planet >= 0) {
					const Point& point = campaign.Planets[army.Planet].Points[army.Point];
					if (point.Owner == army.Faction) {
						heal = (point.Settlement > 0 ? 0.3F : 0.15F) + (healthy[army.Faction] ? 0.1F : 0.0F);
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
			// By ID: a space battle on the way can take armies away.
			std::vector<long> ids;
			for (const Army& army: campaign.Armies) {
				ids.push_back(army.Id);
			}
			for (long id: ids) {
				if (const int army = FindArmy(campaign, id); army >= 0) {
					FollowPath(campaign, army);
				}
			}
			// Warships of factions at war in the same orbit fight, once a system a turn: the strongest of them attacks.
			for (int system = 0; system < static_cast<int>(campaign.Systems.size()); ++system) {
				int attacker = -1;
				float best = 0.0F;
				std::vector<int> enemies;
				for (int army: ArmiesInOrbit(campaign, system)) {
					if (!Armed(campaign.Armies[army]) || SpacePower(campaign.Armies[army]) <= best) {
						continue;
					}
					if (std::vector<int> found = EnemyWarships(campaign, campaign.Armies[army].Faction, system); !found.empty()) {
						attacker = army;
						best = SpacePower(campaign.Armies[army]);
						enemies = std::move(found);
					}
				}
				if (attacker >= 0) {
					SpaceBattle(campaign, attacker, enemies);
				}
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
		static constexpr const char* c_Names[] = {"Barracks", "Market", "Walls", "Hospital", "Shipyard"};
		return c_Names[std::clamp(static_cast<int>(building), 0, static_cast<int>(Building::Count) - 1)];
	}

	const char* BuildingTip(Building building) {
		static constexpr const char* c_Tips[] = {
		    "Lets better squads be recruited here: second tier, and third in a city on a planet nobody has invaded.",
		    "More gold every turn from this point.",
		    "Armies and locals defending this point fight harder.",
		    "Wounded soldiers resting here heal faster.",
		    "Armies here can have their ship upgraded to a warship (a frigate, then a cruiser), and ships in orbit above are repaired faster."};
		return c_Tips[std::clamp(static_cast<int>(building), 0, static_cast<int>(Building::Count) - 1)];
	}

	int BuildingCost(Building building) {
		static constexpr int c_Costs[] = {500, 400, 600, 350, 600};
		return c_Costs[std::clamp(static_cast<int>(building), 0, static_cast<int>(Building::Count) - 1)];
	}

	const char* OnAttackName(int order) {
		static constexpr const char* c_Names[] = {"Fight", "Flee if losing", "Always flee"};
		return c_Names[std::clamp(order, 0, static_cast<int>(OnAttack::Count) - 1)];
	}

	const char* OnAttackTip(int order) {
		static constexpr const char* c_Tips[] = {
		    "Stands and fights whoever attacks it.",
		    "Flees when attacked by an army or fleet likely to beat it (under about 40% to hold).",
		    "Flees whenever it's attacked."};
		return c_Tips[std::clamp(order, 0, static_cast<int>(OnAttack::Count) - 1)];
	}

	const char* ShipName(int ship) {
		static constexpr const char* c_Names[] = {"transport", "frigate", "cruiser"};
		return c_Names[std::clamp(ship, 0, static_cast<int>(Ship::Count) - 1)];
	}

	const char* ShipTip(int ship) {
		static constexpr const char* c_Tips[] = {
		    "Only carries the army. It never fights in space, and can't land where enemy warships guard the orbit.",
		    "A warship: fights enemy warships in orbit, guards the planets below, and can fight its way past a guarded orbit to land.",
		    "A heavy warship: much stronger in space than a frigate."};
		return c_Tips[std::clamp(ship, 0, static_cast<int>(Ship::Count) - 1)];
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

	const char* ResourceName(Resource resource) {
		static constexpr const char* c_Names[] = {"Water", "Food", "Metal", "Fuel", "Crystal", "Relics"};
		return c_Names[std::clamp(static_cast<int>(resource), 0, c_ResourceCount - 1)];
	}

	const char* ResourceUse(Resource resource) {
		static constexpr const char* c_Uses[] = {
		    "Grows settlements past an outpost, and builds Hospitals.",
		    "Feeds armies: one a squad each turn. Starving armies weaken and desert.",
		    "Builds, and recruits second and third tier squads.",
		    "Lane jumps: one an army a jump. With none, armies can't jump.",
		    "Rare. Recruits third tier squads, and builds Walls.",
		    "Very rare. Nothing to spend, but producing them pays gold each turn."};
		return c_Uses[std::clamp(static_cast<int>(resource), 0, c_ResourceCount - 1)];
	}

	std::uint32_t ResourceColor(Resource resource) {
		static constexpr std::uint32_t c_Colors[] = {Rgb(80, 170, 255), Rgb(150, 210, 70), Rgb(175, 180, 190), Rgb(255, 150, 50), Rgb(200, 110, 255), Rgb(255, 210, 60)};
		return c_Colors[std::clamp(static_cast<int>(resource), 0, c_ResourceCount - 1)];
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
		AddResources(campaign);
		ResetRelations(campaign);
		AddLog(campaign, "The campaign begins. Take more than half of a planet's points to control it.");
		return campaign;
	}

	void AddResources(Campaign& campaign) {
		// A stream of its own, so the galaxy's layout doesn't depend on it.
		Dice dice(campaign.Seed ^ 0x5EEDC0DEu);
		// What each planet type mostly has: (resource, weight) pairs.
		static const std::vector<std::vector<std::pair<Resource, float>>> c_Deposits = {
		    {{Resource::Food, 0.5F}, {Resource::Water, 0.35F}, {Resource::Metal, 0.15F}}, // Temperate
		    {{Resource::Fuel, 0.55F}, {Resource::Metal, 0.35F}, {Resource::Crystal, 0.1F}}, // Desert
		    {{Resource::Water, 0.65F}, {Resource::Crystal, 0.2F}, {Resource::Metal, 0.15F}}, // Ice
		    {{Resource::Food, 0.7F}, {Resource::Water, 0.2F}, {Resource::Fuel, 0.1F}}, // Jungle
		    {{Resource::Fuel, 0.45F}, {Resource::Crystal, 0.3F}, {Resource::Metal, 0.25F}}, // Volcanic
		    {{Resource::Metal, 0.7F}, {Resource::Fuel, 0.2F}, {Resource::Crystal, 0.1F}}, // Barren
		    {{Resource::Metal, 0.5F}, {Resource::Crystal, 0.35F}, {Resource::Water, 0.15F}}, // Caves
		    {{Resource::Water, 0.55F}, {Resource::Food, 0.45F}}, // Ocean
		    {{Resource::Fuel, 1.0F}}, // Gas giant (no points)
		};
		std::vector<std::pair<int, int>> candidates; // (planet, point) a relic could be on.
		for (int planetIndex = 0; planetIndex < static_cast<int>(campaign.Planets.size()); ++planetIndex) {
			Planet& planet = campaign.Planets[planetIndex];
			const bool giantMoon = planet.Parent >= 0 && campaign.Planets[planet.Parent].Type == PlanetType::GasGiant;
			const std::vector<std::pair<Resource, float>>& table = c_Deposits[std::clamp(static_cast<int>(planet.Type), 0, static_cast<int>(PlanetType::Count) - 1)];
			for (int pointIndex = 0; pointIndex < static_cast<int>(planet.Points.size()); ++pointIndex) {
				Point& point = planet.Points[pointIndex];
				float roll = dice.Unit();
				Resource deposit = table.front().first;
				for (const auto& [resource, weight]: table) {
					roll -= weight;
					if (roll <= 0.0F) {
						deposit = resource;
						break;
					}
				}
				if (giantMoon && dice.Chance(0.5F)) {
					deposit = Resource::Fuel;
				}
				point.Deposit = static_cast<int>(deposit);
				const float size = dice.Unit();
				point.Yield = size < 0.55F ? 1 : size < 0.9F ? 2 : 3;
				candidates.emplace_back(planetIndex, pointIndex);
			}
		}
		// Home planets the same for everyone: enough food for the first army, a little of the rest.
		static constexpr std::pair<Resource, int> c_Home[] = {{Resource::Food, 2}, {Resource::Food, 1}, {Resource::Water, 1}, {Resource::Metal, 1}, {Resource::Fuel, 1}};
		std::set<int> homes;
		for (const Faction& faction: campaign.Factions) {
			if (faction.HomePlanet >= 0 && faction.HomePlanet < static_cast<int>(campaign.Planets.size())) {
				homes.insert(faction.HomePlanet);
				std::vector<Point>& points = campaign.Planets[faction.HomePlanet].Points;
				for (size_t point = 0; point < points.size() && point < std::size(c_Home); ++point) {
					points[point].Deposit = static_cast<int>(c_Home[point].first);
					points[point].Yield = c_Home[point].second;
				}
			}
		}
		// A few relics, scattered away from the homes.
		std::erase_if(candidates, [&homes](const std::pair<int, int>& each) { return homes.count(each.first) > 0; });
		const int relics = std::max(2, static_cast<int>(campaign.Systems.size()) / 8);
		for (int relic = 0; relic < relics && !candidates.empty(); ++relic) {
			const size_t pick = static_cast<size_t>(dice.Int(0, static_cast<int>(candidates.size()) - 1));
			Point& point = campaign.Planets[candidates[pick].first].Points[candidates[pick].second];
			point.Deposit = static_cast<int>(Resource::Relics);
			point.Yield = 1;
			candidates.erase(candidates.begin() + static_cast<long>(pick));
		}
		for (int resource = 0; resource < c_ResourceCount; ++resource) {
			campaign.Prices[resource] = c_BasePrices[resource];
		}
		campaign.Events.clear();
		for (Faction& faction: campaign.Factions) {
			faction.Resources = c_StartStock;
		}
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
			control.Invaded = std::any_of(points.begin(), points.end(), [&](const Point& point) { return point.Owner >= 0 && point.Owner != control.Controller && !Allied(campaign, point.Owner, control.Controller); });
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
		if (BonusTier(campaign, army.Faction, Resource::Food) >= 1) {
			upkeep *= 0.9F;
		}
		return static_cast<int>(upkeep) + c_ShipUpkeep[std::clamp(army.Ship, 0, static_cast<int>(Ship::Count) - 1)];
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
		income += c_RelicGold[BonusTier(campaign, faction, Resource::Relics)];
		return income - UpkeepOf(campaign, faction);
	}

	bool Armed(const Army& army) { return army.Ship > static_cast<int>(Ship::Transport); }

	float SpacePower(const Army& army) { return c_ShipPower[std::clamp(army.Ship, 0, static_cast<int>(Ship::Count) - 1)] * std::clamp(army.Hull, 0.1F, 1.0F); }

	std::vector<int> OrbitGuards(const Campaign& campaign, int faction, int planet, int point) {
		std::vector<int> guards;
		if (planet < 0 || planet >= static_cast<int>(campaign.Planets.size())) {
			return guards;
		}
		// Whoever the landing would attack.
		std::vector<int> attacked;
		const int owner = point >= 0 && point < static_cast<int>(campaign.Planets[planet].Points.size()) ? campaign.Planets[planet].Points[point].Owner : -1;
		if (owner >= 0) {
			attacked.push_back(owner);
		}
		if (point >= 0) {
			for (int army: ArmiesAt(campaign, planet, point)) {
				attacked.push_back(campaign.Armies[army].Faction);
			}
		}
		for (int army: ArmiesInOrbit(campaign, campaign.Planets[planet].System)) {
			const Army& each = campaign.Armies[army];
			if (!Armed(each) || Friendly(campaign, faction, each.Faction)) {
				continue;
			}
			if (StanceOf(campaign, faction, each.Faction) == Stance::War || std::find(attacked.begin(), attacked.end(), each.Faction) != attacked.end()) {
				guards.push_back(army);
			}
		}
		return guards;
	}

	float SpaceWinChance(const Campaign& campaign, const Army& army, const std::vector<int>& guards) {
		float defence = 0.0F;
		for (int guard: guards) {
			defence += SpacePower(campaign.Armies[guard]);
		}
		return WinChance(SpacePower(army), defence);
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
		if (strength > 0.0F && BonusTier(campaign, army.Faction, Resource::Crystal) >= 2) {
			strength *= 1.1F;
		}
		return strength;
	}

	float PointDefence(const Campaign& campaign, int planet, int point) {
		const Point& here = campaign.Planets[planet].Points[point];
		const float walls = WallsFactor(campaign, here);
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
		const Cost cost = RecruitCostFor(campaign, faction, type);
		if (!CanAfford(campaign, faction, cost)) {
			why = "Needs " + CostText(cost) + ".";
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

	int PointYield(const Campaign& campaign, int planet, int point) {
		const Point& here = campaign.Planets[planet].Points[point];
		if (here.Owner < 0) {
			return 0;
		}
		int yield = here.Yield + (here.Settlement > 0 ? 1 : 0);
		const PlanetControl control = ControlOf(campaign, planet);
		if (control.Controller == here.Owner && control.Invaded) {
			yield = (yield + 1) / 2;
		}
		return yield;
	}

	Stock ProductionOf(const Campaign& campaign, int faction) {
		Stock made{};
		for (int planet = 0; planet < static_cast<int>(campaign.Planets.size()); ++planet) {
			const std::vector<Point>& points = campaign.Planets[planet].Points;
			if (std::none_of(points.begin(), points.end(), [faction](const Point& point) { return point.Owner == faction; })) {
				continue;
			}
			// (The planet's control worked out once, not for every point.)
			const PlanetControl control = ControlOf(campaign, planet);
			for (const Point& point: points) {
				if (point.Owner == faction) {
					int yield = point.Yield + (point.Settlement > 0 ? 1 : 0);
					if (control.Controller == faction && control.Invaded) {
						yield = (yield + 1) / 2;
					}
					made[std::clamp(point.Deposit, 0, c_ResourceCount - 1)] += yield;
				}
			}
		}
		return made;
	}

	Stock UsageOf(const Campaign& campaign, int faction) {
		Stock used{};
		for (const Army& army: campaign.Armies) {
			if (army.Faction == faction) {
				used[static_cast<size_t>(Resource::Food)] += static_cast<int>(army.Squads.size());
			}
		}
		return used;
	}

	int BonusThreshold(Resource resource, int tier) {
		const int step = resource == Resource::Relics ? 1 : resource == Resource::Crystal ? 2 : 3;
		return tier <= 1 ? step : resource == Resource::Relics ? 3 : step * 2;
	}

	int BonusTier(const Campaign& campaign, int faction, Resource resource) {
		if (faction < 0 || faction >= static_cast<int>(campaign.Factions.size())) {
			return 0;
		}
		const int made = ProductionOf(campaign, faction)[static_cast<size_t>(resource)];
		return made >= BonusThreshold(resource, 2) ? 2 : made >= BonusThreshold(resource, 1) ? 1 : 0;
	}

	const char* BonusText(Resource resource, int tier) {
		static constexpr const char* c_Texts[c_ResourceCount][2] = {
		    {"Hospitals cost 30% less", "Settlements grow for 30% less, and wounded heal faster"},
		    {"Upkeep 10% lower", "New soldiers start as Corporals"},
		    {"Buildings cost 25% less", "Walls are stronger"},
		    {"Taking off takes no move", "Lane jumps take 1 move, not 2"},
		    {"Third tier squads cost 20% less", "Your armies fight 10% harder"},
		    {"50 gold a turn", "150 gold a turn"}};
		return c_Texts[std::clamp(static_cast<int>(resource), 0, c_ResourceCount - 1)][std::clamp(tier, 1, 2) - 1];
	}

	bool CanAfford(const Campaign& campaign, int faction, const Cost& cost) {
		const Faction& self = campaign.Factions[faction];
		if (self.Gold < cost.Gold) {
			return false;
		}
		for (int resource = 0; resource < c_ResourceCount; ++resource) {
			if (self.Resources[resource] < cost.Resources[resource]) {
				return false;
			}
		}
		return true;
	}

	std::string CostText(const Cost& cost) {
		std::string text = std::to_string(cost.Gold) + " gold";
		for (int resource = 0; resource < c_ResourceCount; ++resource) {
			if (cost.Resources[resource] > 0) {
				std::string name = ResourceName(static_cast<Resource>(resource));
				name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
				text += ", " + std::to_string(cost.Resources[resource]) + " " + name;
			}
		}
		return text;
	}

	namespace {
		Cost Discounted(Cost cost, float factor) {
			cost.Gold = static_cast<int>(std::round(static_cast<float>(cost.Gold) * factor));
			for (int& amount: cost.Resources) {
				if (amount > 0) {
					amount = std::max(1, static_cast<int>(std::round(static_cast<float>(amount) * factor)));
				}
			}
			return cost;
		}

		void Pay(Campaign& campaign, int faction, const Cost& cost) {
			Faction& self = campaign.Factions[faction];
			self.Gold -= cost.Gold;
			for (int resource = 0; resource < c_ResourceCount; ++resource) {
				self.Resources[resource] -= cost.Resources[resource];
			}
		}

		int& Of(Stock& stock, Resource resource) { return stock[static_cast<size_t>(resource)]; }
	} // namespace

	Cost SettleCostFor(const Campaign& campaign, int faction, int level) {
		Cost cost;
		cost.Gold = SettleCost(level);
		Of(cost.Resources, Resource::Water) = level <= 0 ? 0 : level == 1 ? 3 : 6;
		Of(cost.Resources, Resource::Metal) = level >= 2 ? 3 : 0;
		return BonusTier(campaign, faction, Resource::Water) >= 2 ? Discounted(cost, 0.7F) : cost;
	}

	Cost BuildingCostFor(const Campaign& campaign, int faction, Building building) {
		Cost cost;
		cost.Gold = BuildingCost(building);
		switch (building) {
			case Building::Barracks:
				Of(cost.Resources, Resource::Metal) = 4;
				break;
			case Building::Market:
				Of(cost.Resources, Resource::Metal) = 2;
				break;
			case Building::Walls:
				Of(cost.Resources, Resource::Metal) = 4;
				Of(cost.Resources, Resource::Crystal) = 1;
				break;
			case Building::Hospital:
				Of(cost.Resources, Resource::Metal) = 2;
				Of(cost.Resources, Resource::Water) = 3;
				break;
			case Building::Shipyard:
				Of(cost.Resources, Resource::Metal) = 6;
				Of(cost.Resources, Resource::Fuel) = 2;
				break;
			default:
				break;
		}
		if (BonusTier(campaign, faction, Resource::Metal) >= 1) {
			cost = Discounted(cost, 0.75F);
		}
		if (building == Building::Hospital && BonusTier(campaign, faction, Resource::Water) >= 1) {
			cost = Discounted(cost, 0.7F);
		}
		return cost;
	}

	Cost ShipCostFor(const Campaign& campaign, int faction, int ship) {
		Cost cost;
		if (ship == static_cast<int>(Ship::Frigate)) {
			cost.Gold = 600;
			Of(cost.Resources, Resource::Metal) = 5;
			Of(cost.Resources, Resource::Fuel) = 3;
		} else if (ship == static_cast<int>(Ship::Cruiser)) {
			cost.Gold = 1500;
			Of(cost.Resources, Resource::Metal) = 10;
			Of(cost.Resources, Resource::Crystal) = 3;
		}
		if (BonusTier(campaign, faction, Resource::Metal) >= 1) {
			cost = Discounted(cost, 0.75F);
		}
		return cost;
	}

	bool CanUpgradeShip(const Campaign& campaign, int army, std::string& why) {
		if (army < 0 || army >= static_cast<int>(campaign.Armies.size())) {
			return false;
		}
		const Army& self = campaign.Armies[army];
		if (self.Ship >= static_cast<int>(Ship::Count) - 1) {
			why = "It already has the best ship there is.";
			return false;
		}
		if (self.Planet < 0 || campaign.Planets[self.Planet].Points[self.Point].Owner != self.Faction || campaign.Planets[self.Planet].Points[self.Point].Settlement == 0 || !campaign.Planets[self.Planet].Points[self.Point].Built[static_cast<size_t>(Building::Shipyard)]) {
			why = "Only at one of your settlements with a Shipyard.";
			return false;
		}
		const Cost cost = ShipCostFor(campaign, self.Faction, self.Ship + 1);
		if (!CanAfford(campaign, self.Faction, cost)) {
			why = "Needs " + CostText(cost) + ".";
			return false;
		}
		return true;
	}

	Cost RecruitCostFor(const Campaign& campaign, int faction, int type) {
		Cost cost;
		const std::vector<SquadType>& squads = campaign.Factions[faction].Squads;
		if (type < 0 || type >= static_cast<int>(squads.size())) {
			return cost;
		}
		const SquadType& squad = squads[type];
		cost.Gold = static_cast<int>(squad.SoldierCost * static_cast<float>(squad.Size));
		if (squad.Tier == 2) {
			Of(cost.Resources, Resource::Metal) = 2;
		} else if (squad.Tier >= 3) {
			Of(cost.Resources, Resource::Metal) = 3;
			Of(cost.Resources, Resource::Crystal) = 2;
			if (BonusTier(campaign, faction, Resource::Crystal) >= 1) {
				cost.Gold = static_cast<int>(std::round(static_cast<float>(cost.Gold) * 0.8F));
			}
		}
		return cost;
	}

	int JumpMoves(const Campaign& campaign, int faction) { return BonusTier(campaign, faction, Resource::Fuel) >= 2 ? 1 : 2; }

	int TakeOffMoves(const Campaign& campaign, int faction) { return BonusTier(campaign, faction, Resource::Fuel) >= 1 ? 0 : 1; }

	float BasePrice(Resource resource) { return c_BasePrices[std::clamp(static_cast<int>(resource), 0, c_ResourceCount - 1)]; }

	int BuyPrice(const Campaign& campaign, Resource resource) { return std::max(1, static_cast<int>(std::round(campaign.Prices[static_cast<size_t>(resource)] * (1.0F + c_MarketCut)))); }

	int SellPrice(const Campaign& campaign, Resource resource) { return std::max(1, static_cast<int>(std::round(campaign.Prices[static_cast<size_t>(resource)] * (1.0F - c_MarketCut)))); }
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
		const float walls = WallsFactor(campaign, here);
		for (int army: ArmiesAt(campaign, planet, point)) {
			if (campaign.Armies[army].Faction != faction && !Allied(campaign, faction, campaign.Armies[army].Faction)) {
				defence += Strength(campaign, campaign.Armies[army]) * walls;
			}
		}
		if (here.Owner != faction && !Allied(campaign, faction, here.Owner)) {
			defence += PointDefence(campaign, planet, point);
		}
		return defence > 0.01F;
	}

	int LocalSoldiers(const Campaign& campaign, int faction, int planet, int point) {
		const Point& here = campaign.Planets[planet].Points[point];
		if (here.Owner == faction || Allied(campaign, faction, here.Owner) || PointDefence(campaign, planet, point) < 0.01F) {
			return 0;
		}
		return static_cast<int>(std::ceil(here.Militia));
	}

	bool OrderLand(Campaign& campaign, int army, int planet, int point, const FoughtBattle* fought) {
		if (army < 0 || army >= static_cast<int>(campaign.Armies.size()) || campaign.Winner >= 0) {
			return false;
		}
		Army& landing = campaign.Armies[army];
		if (landing.Planet >= 0 || landing.Moves < 1 || planet < 0 || planet >= static_cast<int>(campaign.Planets.size()) || campaign.Planets[planet].System != landing.System || point < 0 || point >= static_cast<int>(campaign.Planets[planet].Points.size())) {
			return false;
		}
		landing.Path.clear();
		if (const std::vector<int> guards = OrbitGuards(campaign, landing.Faction, planet, point); !guards.empty()) {
			// Warships guard the orbit: a transport can't get past them, a warship has to beat them first.
			if (!Armed(landing)) {
				return false;
			}
			const long id = landing.Id;
			if (!SpaceBattle(campaign, army, guards)) {
				return true;
			}
			army = FindArmy(campaign, id);
		}
		--campaign.Armies[army].Moves;
		EnterPoint(campaign, army, planet, point, fought);
		return true;
	}

	bool UpgradeShip(Campaign& campaign, int army) {
		std::string why;
		if (!CanUpgradeShip(campaign, army, why)) {
			return false;
		}
		Army& self = campaign.Armies[army];
		Pay(campaign, self.Faction, ShipCostFor(campaign, self.Faction, self.Ship + 1));
		++self.Ship;
		self.Hull = 1.0F;
		if (IsHuman(campaign, self.Faction)) {
			AddLog(campaign, self.Name + " now has a " + ShipName(self.Ship) + ".");
		}
		return true;
	}

	bool OrderOverland(Campaign& campaign, int army, int point, const FoughtBattle* fought) {
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
		EnterPoint(campaign, army, moving.Planet, point, fought);
		return true;
	}

	bool OrderLaunch(Campaign& campaign, int army) {
		if (army < 0 || army >= static_cast<int>(campaign.Armies.size())) {
			return false;
		}
		Army& launching = campaign.Armies[army];
		const int moves = TakeOffMoves(campaign, launching.Faction);
		if (launching.Planet < 0 || launching.Moves < std::max(moves, 1)) {
			return false;
		}
		launching.Planet = -1;
		launching.Point = -1;
		launching.Moves -= moves;
		return true;
	}

	bool Recruit(Campaign& campaign, int faction, int planet, int point, int type) {
		std::string why;
		if (!CanRecruit(campaign, faction, planet, point, type, why)) {
			return false;
		}
		Faction& self = campaign.Factions[faction];
		Pay(campaign, faction, RecruitCostFor(campaign, faction, type));
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
		if (BonusTier(campaign, faction, Resource::Food) >= 2) {
			for (Soldier& soldier: campaign.Armies[into].Squads.back().Soldiers) {
				soldier.Rank = std::max(soldier.Rank, 1);
			}
		}
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
		const int rank = BonusTier(campaign, self.Faction, Resource::Food) >= 2 ? 1 : 0;
		for (Squad& squad: self.Squads) {
			const SquadType& type = faction.Squads[std::clamp(squad.Type, 0, static_cast<int>(faction.Squads.size()) - 1)];
			while (static_cast<int>(squad.Soldiers.size()) < type.Size) {
				squad.Soldiers.push_back(NewSoldier(campaign));
				squad.Soldiers.back().Rank = rank;
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
		const Cost cost = SettleCostFor(campaign, faction, here.Settlement);
		if (!CanAfford(campaign, faction, cost)) {
			return false;
		}
		Pay(campaign, faction, cost);
		++here.Settlement;
		if (IsHuman(campaign, faction)) {
			AddLog(campaign, here.Name + " is now " + (here.Settlement == 1 ? "an " : "a ") + SettlementName(here.Settlement) + ".");
		}
		return true;
	}

	bool Build(Campaign& campaign, int faction, int planet, int point, Building building) {
		Point& here = campaign.Planets[planet].Points[point];
		const size_t which = static_cast<size_t>(building);
		const Cost cost = BuildingCostFor(campaign, faction, building);
		if (here.Owner != faction || here.Settlement == 0 || here.Built[which] || !CanAfford(campaign, faction, cost)) {
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
		Pay(campaign, faction, cost);
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
		// Everyone aboard: the better ship carries the army on.
		if (source.Squads.empty() && (source.Ship > target.Ship || (source.Ship == target.Ship && source.Hull > target.Hull))) {
			target.Ship = source.Ship;
			target.Hull = source.Hull;
		}
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
		campaign.Armies[made].IfAttacked = source.IfAttacked;
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

	bool Trade(Campaign& campaign, int faction, Resource resource, int amount) {
		if (faction < 0 || faction >= static_cast<int>(campaign.Factions.size()) || resource == Resource::Count || amount == 0) {
			return false;
		}
		Faction& self = campaign.Factions[faction];
		const size_t index = static_cast<size_t>(resource);
		float& price = campaign.Prices[index];
		bool traded = false;
		for (int unit = 0; unit < std::abs(amount); ++unit) {
			if (amount > 0) {
				const int cost = BuyPrice(campaign, resource);
				if (self.Gold < cost) {
					break;
				}
				self.Gold -= cost;
				++self.Resources[index];
				price *= 1.0F + c_PriceStep;
			} else {
				if (self.Resources[index] < 1) {
					break;
				}
				self.Gold += SellPrice(campaign, resource);
				--self.Resources[index];
				price *= 1.0F - c_PriceStep;
			}
			price = std::clamp(price, c_BasePrices[index] * 0.3F, c_BasePrices[index] * 4.0F);
			traded = true;
		}
		return traded;
	}

	void EndTurn(Campaign& campaign) {
		if (campaign.Winner >= 0) {
			return;
		}
		campaign.Offers.clear(); // Anything not answered by now lapses.
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
