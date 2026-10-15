#pragma once

// Campaign mode's model: the galaxy, its planets and their points, the factions, their armies and their gold, and the rules of a turn
// (moving, taking points, recruiting, building, income, healing, auto-resolved battles and the AI factions' turns). No drawing: the
// campaign screen (CampaignScreen.cpp) only reads this and calls its commands, and the AI factions use the same commands.
// See /plans/campaign-mode-plan.md in the project files.

#include <array>
#include <cstdint>
#include <random>
#include <string>
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
		Count
	};

	constexpr int c_MaxSquads = 12; //!< Squads an army can have.
	constexpr int c_MaxSquadSize = 8; //!< Soldiers a squad can have.
	constexpr int c_MovesPerTurn = 2; //!< An army's moves each turn: a lane jump takes 2, landing, taking off or going overland 1.
	constexpr int c_MaxSettlement = 3; //!< Outpost, colony, city.

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
	};

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
	const char* RankName(int rank);
	/// The colour planets of a type are drawn in, as an ImU32.
	std::uint32_t PlanetColor(PlanetType type);
	/// A star's colour, as an ImU32.
	std::uint32_t StarColor(int index);
#pragma endregion

#pragma region Starting a campaign
	/// Every faction module that is loaded, mods included, for the new campaign screen.
	std::vector<FactionChoice> AvailableFactions();
	/// A new galaxy and its factions placed in it. Fails (empty Systems) with fewer than two factions picked.
	Campaign Generate(const NewCampaign& setup, std::string& error);
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
#pragma endregion

#pragma region Commands (for the screen and the AI)
	/// Moves an army toward a system along the lanes: as far as its moves go now, the rest at the start of later turns.
	bool OrderMove(Campaign& campaign, int army, int system);
	/// Lands an army in orbit on a point of a planet in its system. Onto an enemy point that is defended, it's a battle.
	bool OrderLand(Campaign& campaign, int army, int planet, int point);
	/// Moves an army on a planet to a linked point (or attacks it).
	bool OrderOverland(Campaign& campaign, int army, int point);
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
