#pragma once

// Campaign mode's model: the galaxy, its planets and their points, the factions, their armies and their gold, and the rules of a turn
// (moving, taking points, recruiting, building, income, healing, auto-resolved battles and the AI factions' turns). No drawing: the
// campaign screen (CampaignScreen.cpp) only reads this and calls its commands, and the AI factions use the same commands.
// See /plans/campaign-mode-plan.md in the project files.

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace RTE::CampaignModel {

	/// What a planet or moon is like: its look on the maps, and the names of its points' ground.
	enum class PlanetType {
		Temperate,
		Desert,
		Ice,
		Jungle,
		Volcanic,
		Barren,
		Caves,
		Ocean,
		GasGiant, //!< Can't be landed on: only its moons can.
		Count
	};

	/// What a settlement on a point has built.
	enum class Building {
		Barracks, //!< Squads past the first tier can be recruited here.
		Market, //!< More income.
		Walls, //!< Defenders fight harder.
		Hospital, //!< Wounded soldiers here heal faster.
		Shipyard, //!< Armies here can have their ship upgraded, and ships are repaired here.
		Count
	};

	/// What points produce besides gold, stockpiled by each faction, spent on growing, building, recruiting, feeding armies and lane
	/// jumps, and traded on the galaxy's market.
	enum class Resource {
		Water, //!< Growing settlements past an outpost, and Hospitals.
		Food, //!< Feeding armies: one a squad a turn.
		Metal, //!< Buildings, and second and third tier squads.
		Fuel, //!< Lane jumps: one an army a jump.
		Crystal, //!< Rare: third tier squads and Walls.
		Relics, //!< Very rare: nothing to spend, but holding them pays.
		Count
	};
	constexpr int c_ResourceCount = static_cast<int>(Resource::Count);
	using Stock = std::array<int, c_ResourceCount>;

	/// What something costs: gold, and resources.
	struct Cost {
		int Gold = 0;
		Stock Resources{};
	};

	/// Something happening in the wider galaxy that moves a resource's price for a while.
	struct GalaxyEvent {
		int Resource = 0;
		float PriceFactor = 1.0F; //!< Its price is pulled toward its normal price times this while the event lasts.
		int TurnsLeft = 0;
		std::string Text;
	};

	constexpr int c_MaxSquads = 12; //!< Squads an army can have.
	constexpr int c_MaxSquadSize = 8; //!< Soldiers a squad can have.
	constexpr int c_MovesPerTurn = 2; //!< An army's moves each turn: a lane jump takes 2, landing, taking off or going overland 1.
	constexpr int c_MaxSettlement = 3; //!< Outpost, colony, city.

	/// The ship an army travels in. A transport only carries it: it never fights in space and never stops anyone landing. Warships
	/// (frigates and cruisers) fight other factions' warships in orbit, and guard the planets below.
	enum class Ship {
		Transport,
		Frigate,
		Cruiser,
		Count
	};

	/// A place on a planet's surface that can be owned, settled and fought over.
	struct Point {
		std::string Name;
		int Owner = -1; //!< Faction index, -1 for nobody.
		float X = 0.5F; //!< Where it is on the planet's surface map, 0 to 1 across.
		float Y = 0.5F; //!< And 0 to 1 down.
		int Terrain = 0; //!< Which of its planet type's grounds (TerrainName).
		int Settlement = 0; //!< 0 none, 1 outpost, 2 colony, 3 city.
		std::array<bool, static_cast<size_t>(Building::Count)> Built{};
		float Militia = 0.0F; //!< Strength of the locals who defend it with no army there (neutral points, and every settlement).
		std::vector<int> Links; //!< Points on the same planet reached overland in one move.
		int Deposit = 0; //!< The Resource it produces.
		int Yield = 1; //!< How much of it a turn (1 to 3), one more with a settlement.
	};

	/// A planet or a moon.
	struct Planet {
		std::string Name;
		PlanetType Type = PlanetType::Temperate;
		int System = -1;
		int Parent = -1; //!< The planet this one is a moon of, -1 for a planet.
		float Orbit = 1.0F; //!< How far out it orbits (its star, or its planet for a moon), on the system map.
		float Angle = 0.0F; //!< Where on its orbit, radians.
		float Size = 1.0F; //!< How big it is drawn.
		std::uint32_t Look = 0; //!< Seed for its surface's look.
		std::vector<Point> Points; //!< 3 or 5; none for a gas giant.
	};

	/// A star and what orbits it.
	struct StarSystem {
		std::string Name;
		float X = 0.0F; //!< Where it is in the galaxy, 0 to 1.
		float Y = 0.0F;
		int StarColor = 0; //!< Index into the star colours.
		std::vector<int> Planets; //!< Planets and moons, by index.
		std::vector<int> Lanes; //!< Systems joined to this one by a space lane.
	};

	/// One soldier, kept from battle to battle.
	struct Soldier {
		long Id = 0;
		std::string Name;
		int Rank = 0; //!< 0 Private, 1 Corporal, 2 Sergeant, 3 Veteran.
		int Kills = 0;
		float Health = 1.0F; //!< 0 to 1.
	};

	/// A kind of squad a faction can recruit: one of its Loadouts, so many of them.
	struct SquadType {
		std::string Name; //!< As shown ("Infantry Light").
		std::string Loadout; //!< The Loadout preset its soldiers are made from in battle.
		float SoldierCost = 100.0F; //!< Gold for one soldier, from the Loadout's value.
		int Size = 4; //!< Soldiers in a full squad.
		int Tier = 1; //!< 1 anywhere with a settlement, 2 with Barracks, 3 with Barracks in a city that isn't invaded.
	};

	struct Squad {
		int Type = 0; //!< Index into its faction's squad types.
		std::vector<Soldier> Soldiers;
	};

	/// What an army does when it's attacked: stand and fight, or flee. Fleeing takes a move (with none left it has to fight) and costs
	/// a share of its soldiers (c_FleeLoss, less than losing would), and the army goes to the nearest safe place.
	enum class OnAttack {
		Fight,
		FleeIfLosing, //!< Flees when its chance of holding is poor.
		Flee, //!< Always flees, when it can.
		Count
	};
	constexpr float c_FleeLoss = 0.25F; //!< The share of its soldiers an army loses fleeing.

	/// An army: where it is, what's in it and what it's been told to do.
	struct Army {
		long Id = 0;
		std::string Name;
		int Faction = 0;
		int System = 0; //!< The system it is in.
		int Planet = -1; //!< The planet or moon it stands on, -1 in orbit.
		int Point = -1; //!< The point on that planet it stands on.
		int Moves = c_MovesPerTurn; //!< Moves left this turn.
		std::vector<int> Path; //!< Systems still to go through, next first: carried on with at the start of each turn.
		std::vector<Squad> Squads;
		int Ship = 0; //!< Its Ship.
		float Hull = 1.0F; //!< Its ship's state, 0 to 1: what it fights with in space. Repaired a little each turn, fully at a Shipyard.
		int IfAttacked = 0; //!< Its OnAttack order.
	};

	struct Faction {
		std::string Name; //!< The module's friendly name.
		std::string Module; //!< The faction's module file name ("Coalition.rte").
		std::uint32_t Color = 0xFFFFFFFF; //!< As an ImU32.
		bool Human = false;
		bool Alive = true;
		int Gold = 0;
		int HomePlanet = -1;
		std::vector<SquadType> Squads;
		Stock Resources{}; //!< Stockpiled resources.
	};

	/// Where two factions stand with each other.
	enum class Stance {
		War, //!< Their armies fight.
		Neutral, //!< The start: either can attack, but it angers everyone a little.
		Peace, //!< No attacks while it lasts.
		Alliance, //!< No attacks, their armies can stand on each other's points and defend together, and they can win together.
		Count
	};

	/// How a pair of factions get on.
	struct Relation {
		int Stance = static_cast<int>(Stance::Neutral);
		int Opinion = 0; //!< -100 to 100.
		int Turns = 0; //!< Turns in the stance so far.
	};

	/// A trade between two factions: what From gives To, and what it gets back. Once, or every turn as a pact.
	struct Deal {
		int From = -1;
		int To = -1;
		int GoldGive = 0;
		int GoldGet = 0;
		Stock Give{};
		Stock Get{};
		bool Pact = false; //!< Every turn for c_PactTurns, not once.
	};
	constexpr int c_PactTurns = 10;

	/// A trade pact running between two factions.
	struct Pact {
		Deal Terms;
		int TurnsLeft = 0;
	};

	/// Something an AI faction puts to a human one, answered between turns.
	struct Offer {
		enum class Kind { Deal, Peace, Alliance, JoinWar } What = Kind::Deal;
		int From = -1;
		int To = -1;
		Deal Terms; //!< For a deal.
		int Against = -1; //!< For joining a war: the enemy.
		std::string Text; //!< What they say.
	};

	/// A faction offered on the new campaign screen.
	struct FactionChoice {
		std::string Name;
		std::string Module;
		bool Include = false;
		bool Human = false;
	};

	/// What starting a campaign asks for.
	struct NewCampaign {
		std::string Name = "New campaign";
		std::uint32_t Seed = 1;
		int Size = 1; //!< 0 small, 1 medium, 2 large galaxy.
		int Difficulty = 1; //!< 0 easy, 1 normal, 2 hard.
		std::vector<FactionChoice> Factions;
		bool StartFrigates = false; //!< For testing: every faction's starting army has a frigate, not a transport.
		bool GodMode = false; //!< For testing: the human factions start with c_GodGold gold and c_GodStock of every resource.
	};
	constexpr int c_GodGold = 999999;
	constexpr int c_GodStock = 9999;

	/// How a battle went, for the report and the log.
	struct BattleOutcome {
		bool Happened = false;
		int Attacker = -1;
		int Defender = -1; //!< Faction, -1 for locals.
		std::string Where;
		int Planet = -1; //!< Where it was fought, for the maps.
		int Point = -1;
		float AttackStrength = 0.0F;
		float DefendStrength = 0.0F;
		bool AttackerWon = false;
		int AttackerLost = 0; //!< Soldiers who fell.
		int DefenderLost = 0;
		int AttackerSoldiers = 0; //!< Soldiers there at the start.
		int DefenderSoldiers = 0;
		std::vector<std::string> Promotions;
		bool PlayerInvolved = false;
		bool Fought = false; //!< Played out on the battlefield, not auto-resolved.
		bool Space = false; //!< Between fleets in orbit: strengths are the ships' power, and the losses are soldiers lost with ships.
		int System = -1; //!< Where a space battle was fought.
		std::vector<std::string> Notes; //!< What happened to the ships, who fled, and who fell back where.
		bool Fled = false; //!< No battle: the defenders all fled (see Notes).
	};

	/// A ground battle the player fought for real (CampaignBattle.lua): who won, and who's still standing.
	struct FoughtBattle {
		bool AttackerWon = false;
		std::unordered_map<long, float> Survivors; //!< Each campaign soldier alive at the end, by Id, with their health (0 to 1).
		int LocalsLeft = 0; //!< Locals defending the point still standing.
	};

	/// A planet's state: who controls it (more than half its points) and whether it's invaded or contested.
	struct PlanetControl {
		int Controller = -1;
		bool Invaded = false; //!< Controlled, but another faction holds one of its points.
		bool Contested = false; //!< Owned points, but nobody holds more than half.
	};

	/// The whole campaign.
	struct Campaign {
		std::string Name;
		std::string FileName; //!< Where it's saved, under Userdata/Campaigns.
		std::uint32_t Seed = 1;
		int Size = 1;
		int Difficulty = 1;
		int Turn = 1;
		long NextId = 1;
		std::uint32_t Rolls = 0; //!< Random numbers drawn so far: kept with the save, so a reload doesn't reroll a battle.
		int Winner = -1; //!< The faction that won, once one has.
		bool Lost = false; //!< Every human faction is gone.
		std::vector<StarSystem> Systems;
		std::vector<Planet> Planets;
		std::vector<Faction> Factions;
		std::vector<Army> Armies;
		std::vector<std::string> Log; //!< Newest last.
		std::array<float, c_ResourceCount> Prices{}; //!< The galaxy market's prices, gold a unit, before its cut either way.
		std::vector<GalaxyEvent> Events; //!< What's moving prices now.
		std::vector<Relation> Relations; //!< Every pair of factions, Factions.size() squared (see RelationOf).
		std::vector<Pact> Pacts; //!< Trade pacts running.
		std::vector<Offer> Offers; //!< What AI factions have put to the human factions, to answer (not saved).
		std::vector<BattleOutcome> Reports; //!< Battles a human faction was in since the screen last showed them (not saved).
		std::vector<BattleOutcome> Battles; //!< Every battle since the screen last drew them on the maps, the AI factions' too (not saved).
	};

#pragma region Names and looks
	const char* PlanetTypeName(PlanetType type);
	/// The name of a point's ground, by its planet's type and its Terrain.
	const char* TerrainName(PlanetType type, int terrain);
	int TerrainCount(PlanetType type);
	const char* BuildingName(Building building);
	const char* BuildingTip(Building building);
	int BuildingCost(Building building);
	const char* SettlementName(int level);
	const char* OnAttackName(int order);
	/// What an OnAttack order does, one line.
	const char* OnAttackTip(int order);
	const char* ShipName(int ship);
	/// What a ship is for, one line.
	const char* ShipTip(int ship);
	const char* RankName(int rank);
	/// The colour planets of a type are drawn in, as an ImU32.
	std::uint32_t PlanetColor(PlanetType type);
	/// A star's colour, as an ImU32.
	std::uint32_t StarColor(int index);
	const char* ResourceName(Resource resource);
	/// What a resource is for, one line.
	const char* ResourceUse(Resource resource);
	/// A resource's colour on the screen, as an ImU32.
	std::uint32_t ResourceColor(Resource resource);
#pragma endregion

#pragma region Starting a campaign
	/// Every faction module that is loaded, mods included, for the new campaign screen.
	std::vector<FactionChoice> AvailableFactions();
	/// A new galaxy and its factions placed in it. Fails (empty Systems) with fewer than two factions picked.
	Campaign Generate(const NewCampaign& setup, std::string& error);
	/// Gives every point a deposit (from the campaign's seed), the market its normal prices and every faction its starting stock. For a
	/// new galaxy, and a save from before resources.
	void AddResources(Campaign& campaign);
#pragma endregion

#pragma region Reading the state
	PlanetControl ControlOf(const Campaign& campaign, int planet);
	/// Gold a faction gets each turn, after upkeep.
	int IncomeOf(const Campaign& campaign, int faction);
	int UpkeepOf(const Campaign& campaign, int faction);
	/// Gold one army costs its faction each turn.
	int ArmyUpkeep(const Campaign& campaign, const Army& army);
	/// Gold a point gives its owner each turn.
	int PointIncome(const Campaign& campaign, int planet, int point);
	int SoldierCount(const Army& army);
	/// How strong an army is in an auto-resolved battle.
	float Strength(const Campaign& campaign, const Army& army);
	/// Whether an army's ship is a warship: one that fights in space.
	bool Armed(const Army& army);
	/// How strong an army's ship is in a space battle: its kind, and its hull. 0 for a transport.
	float SpacePower(const Army& army);
	/// The warships in orbit that stop a faction's army landing on a point: those of factions at war with it, and those of the faction
	/// it would attack there (the point's owner, and the armies on it) unless they're friends.
	std::vector<int> OrbitGuards(const Campaign& campaign, int faction, int planet, int point);
	/// The chance (0 to 1) a space battle against these guards is won, roughly.
	float SpaceWinChance(const Campaign& campaign, const Army& army, const std::vector<int>& guards);
	/// How strong a point's defence is without the armies on it: its locals and walls.
	float PointDefence(const Campaign& campaign, int planet, int point);
	/// The armies standing on a point.
	std::vector<int> ArmiesAt(const Campaign& campaign, int planet, int point);
	/// The armies in orbit in a system.
	std::vector<int> ArmiesInOrbit(const Campaign& campaign, int system);
	int FindArmy(const Campaign& campaign, long id);
	/// The first human faction (whose turn the screen shows), -1 with none left.
	int PlayerFaction(const Campaign& campaign);
	/// The systems from one to another along the lanes, the next first (not the one it starts in); empty when there's no way.
	std::vector<int> LanePath(const Campaign& campaign, int from, int to);
	/// Whether a squad type can be recruited at a point, and why not.
	bool CanRecruit(const Campaign& campaign, int faction, int planet, int point, int type, std::string& why);
	/// The chance (0 to 1) an attack by these armies on a point is won, roughly, for the attack panel.
	float WinChance(float attack, float defence);

	/// What a point gives its owner of its deposit each turn (its yield, one more with a settlement, half on an invaded planet).
	int PointYield(const Campaign& campaign, int planet, int point);
	/// Resources a faction's points produce each turn.
	Stock ProductionOf(const Campaign& campaign, int faction);
	/// Resources a faction uses each turn whatever it does (food for its armies).
	Stock UsageOf(const Campaign& campaign, int faction);
	/// The production bonus a faction has from a resource: 0 none, 1 or 2 (see BonusThreshold).
	int BonusTier(const Campaign& campaign, int faction, Resource resource);
	/// The production a turn a bonus tier needs.
	int BonusThreshold(Resource resource, int tier);
	/// What a bonus tier does, one line.
	const char* BonusText(Resource resource, int tier);
	bool CanAfford(const Campaign& campaign, int faction, const Cost& cost);
	/// A cost as text: "300 gold, 3 water".
	std::string CostText(const Cost& cost);
	/// What growing a point's settlement to the next level costs a faction, its bonuses counted.
	Cost SettleCostFor(const Campaign& campaign, int faction, int level);
	Cost BuildingCostFor(const Campaign& campaign, int faction, Building building);
	Cost RecruitCostFor(const Campaign& campaign, int faction, int type);
	/// What upgrading an army's ship to this kind costs a faction, its bonuses counted.
	Cost ShipCostFor(const Campaign& campaign, int faction, int ship);
	/// Whether an army's ship can be upgraded to the next kind now, and why not.
	bool CanUpgradeShip(const Campaign& campaign, int army, std::string& why);
	/// Moves a lane jump takes the faction's armies, and whether taking off takes one.
	int JumpMoves(const Campaign& campaign, int faction);
	int TakeOffMoves(const Campaign& campaign, int faction);
	/// What the market charges for one unit, and pays for one.
	int BuyPrice(const Campaign& campaign, Resource resource);
	int SellPrice(const Campaign& campaign, Resource resource);
	/// A resource's normal price.
	float BasePrice(Resource resource);
#pragma endregion

#pragma region Diplomacy
	const char* StanceName(Stance stance);
	/// An opinion as a word: Hostile, Wary, Cordial, Friendly.
	const char* OpinionWord(int opinion);
	/// Sizes the relations to the factions, every pair Neutral (for a new galaxy, and a save from before diplomacy).
	void ResetRelations(Campaign& campaign);
	Relation& RelationOf(Campaign& campaign, int a, int b);
	const Relation& RelationOf(const Campaign& campaign, int a, int b);
	Stance StanceOf(const Campaign& campaign, int a, int b);
	/// Allied, or the same faction.
	bool Allied(const Campaign& campaign, int a, int b);
	/// At peace or allied, or the same faction: they don't fight.
	bool Friendly(const Campaign& campaign, int a, int b);
	/// The faction whose peace or alliance a move onto this point would break by attacking, or -1.
	int TreatyBroken(const Campaign& campaign, int faction, int planet, int point);
	/// A faction's armies' total strength.
	float MightOf(const Campaign& campaign, int faction);
	/// Changes opinion between two factions, both ways.
	void ChangeOpinion(Campaign& campaign, int a, int b, int by);
	/// Puts two factions at war: breaking a treaty costs opinion with everyone, attacking a neutral a little, and the attacked side's
	/// allies may join in.
	void DeclareWar(Campaign& campaign, int from, int to);
	/// What a deal is worth to its To faction, in gold (above 0 it gains).
	int DealValue(const Campaign& campaign, const Deal& deal);
	/// Whether both sides of a deal can pay it now.
	bool CanPay(const Campaign& campaign, const Deal& deal, std::string& why);
	/// Puts a deal to an AI faction: carried out if it says yes. Returns "" for yes, else why not.
	std::string ProposeDeal(Campaign& campaign, const Deal& deal);
	/// Asks an AI faction for peace or an alliance. Returns "" for yes, else why not.
	std::string ProposeStance(Campaign& campaign, int from, int to, Stance stance);
	/// Ends an alliance or peace without attacking: back to Neutral, with some opinion lost.
	void EndTreaty(Campaign& campaign, int from, int to);
	/// Cancels a running pact early, with some opinion lost.
	void CancelPact(Campaign& campaign, int pact);
	/// Answers one of the offers to a human faction.
	void AnswerOffer(Campaign& campaign, int offer, bool accept);
	/// The diplomacy part of a new turn: opinions drift, stances age, and trade pacts swap their goods.
	void UpdateDiplomacy(Campaign& campaign);
	/// An AI faction's diplomacy for its turn: asking for peace or an alliance, ending treaties, offering trades to people.
	void AIDiplomacy(Campaign& campaign, int faction);
	/// Whether every faction still alive is allied with every other (two at least), and which of them to name the winner.
	bool AlliedWin(const Campaign& campaign, int& winner);
#pragma endregion

#pragma region Commands (for the screen and the AI)
	/// Moves an army toward a system along the lanes: as far as its moves go now, the rest at the start of later turns.
	bool OrderMove(Campaign& campaign, int army, int system);
	/// Lands an army in orbit on a point of a planet in its system. Onto an enemy point that is defended, it's a battle. With warships
	/// guarding the orbit (OrbitGuards), a transport can't land, and a warship fights them first: it lands only if it wins.
	bool OrderLand(Campaign& campaign, int army, int planet, int point, const FoughtBattle* fought = nullptr);
	/// Moves an army on a planet to a linked point (or attacks it).
	bool OrderOverland(Campaign& campaign, int army, int point, const FoughtBattle* fought = nullptr);
	/// How many locals defend a point against a faction, as soldiers in a battle (none on its own or an ally's ground).
	int LocalSoldiers(const Campaign& campaign, int faction, int planet, int point);
	/// Upgrades an army's ship to the next kind (at a Shipyard of its faction's), and repairs it.
	bool UpgradeShip(Campaign& campaign, int army);
	/// Takes an army off its planet into orbit.
	bool OrderLaunch(Campaign& campaign, int army);
	/// Whether a move onto a point would be a battle (an enemy army there, or defenders), and how strong they are.
	bool WouldFight(const Campaign& campaign, int faction, int planet, int point, float& defence);
	/// Recruits a squad at a point the faction owns: into the army there, or a new army.
	bool Recruit(Campaign& campaign, int faction, int planet, int point, int type);
	/// Fills an army's squads back up to full, with new soldiers bought at the point it stands on (a settlement of its faction's).
	bool Replenish(Campaign& campaign, int army);
	/// Gold Replenish would cost, 0 with nothing to fill.
	int ReplenishCost(const Campaign& campaign, int army);
	/// Founds a settlement on a point the faction owns, or makes it a level bigger.
	bool Settle(Campaign& campaign, int faction, int planet, int point);
	int SettleCost(int level);
	bool Build(Campaign& campaign, int faction, int planet, int point, Building building);
	/// Puts two armies on the same spot together (as many squads as fit).
	bool Merge(Campaign& campaign, int into, int from);
	/// Splits a squad off an army into a new army on the same spot.
	bool SplitSquad(Campaign& campaign, int army, int squad);
	void Disband(Campaign& campaign, int army);
	/// Buys (amount above 0) or sells (below 0) a resource on the galaxy's market, a unit at a time as the price moves.
	bool Trade(Campaign& campaign, int faction, Resource resource, int amount);
	/// Ends the human factions' turn: the AI factions take theirs, then the new turn's income, healing and carried-on moves.
	void EndTurn(Campaign& campaign);
#pragma endregion

#pragma region Saving
	/// The saved campaigns, newest first, by file name.
	struct SaveInfo {
		std::string FileName;
		std::string Name;
		int Turn = 0;
		std::string Factions;
		long long Modified = 0;
	};
	std::vector<SaveInfo> ListSaves();
	bool Save(Campaign& campaign, std::string& error);
	bool Load(const std::string& fileName, Campaign& campaign, std::string& error);
	bool DeleteSave(const std::string& fileName);
#pragma endregion

	/// A random number from the campaign's own stream (kept in the save).
	float Roll(Campaign& campaign);
	void AddLog(Campaign& campaign, const std::string& line);
} // namespace RTE::CampaignModel
