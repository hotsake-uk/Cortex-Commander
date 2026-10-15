// Campaign mode's saves: one text file a campaign in Userdata/Campaigns, everything in it written out (the galaxy too, so a save never
// depends on the generator staying the same). Strings are quoted, numbers plain, one record a line.

#include "CampaignModel.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace RTE::CampaignModel {

	namespace {
		constexpr const char* c_Folder = "Userdata/Campaigns/";
		constexpr const char* c_Magic = "CortexCampaign";
		constexpr int c_Version = 6; //!< 2: resources (deposits, stockpiles, the market). 3: diplomacy (relations, trade pacts). 4: armies' ships. 5: what armies do when attacked. 6: AI attacks waiting for the player.

		std::string FileNameFor(const std::string& name) {
			std::string file;
			for (char c: name) {
				const unsigned char u = static_cast<unsigned char>(c);
				file += (std::isalnum(u) || c == ' ' || c == '-' || c == '_') ? c : '_';
			}
			while (!file.empty() && file.back() == ' ') {
				file.pop_back();
			}
			return (file.empty() ? std::string("Campaign") : file) + ".txt";
		}

		std::ostream& Quoted(std::ostream& out, const std::string& text) { return out << std::quoted(text); }

		void WriteHeader(std::ostream& out, const Campaign& campaign) {
			out << c_Magic << ' ' << c_Version << '\n';
			Quoted(out, campaign.Name) << ' ' << campaign.Seed << ' ' << campaign.Size << ' ' << campaign.Difficulty << ' ' << campaign.Turn << ' ' << campaign.NextId << ' ' << campaign.Rolls << ' ' << campaign.Winner << ' ' << campaign.Lost << '\n';
			std::string factions;
			for (const Faction& faction: campaign.Factions) {
				factions += (factions.empty() ? "" : ", ") + faction.Name;
			}
			Quoted(out, factions) << '\n';
		}
	} // namespace

	bool Save(Campaign& campaign, std::string& error) {
		std::error_code code;
		std::filesystem::create_directories(c_Folder, code);
		if (campaign.FileName.empty()) {
			campaign.FileName = FileNameFor(campaign.Name);
		}
		std::ostringstream out;
		out << std::setprecision(9);
		WriteHeader(out, campaign);
		out << "Systems " << campaign.Systems.size() << '\n';
		for (const StarSystem& system: campaign.Systems) {
			Quoted(out, system.Name) << ' ' << system.X << ' ' << system.Y << ' ' << system.StarColor << ' ' << system.Planets.size();
			for (int planet: system.Planets) {
				out << ' ' << planet;
			}
			out << ' ' << system.Lanes.size();
			for (int lane: system.Lanes) {
				out << ' ' << lane;
			}
			out << '\n';
		}
		out << "Planets " << campaign.Planets.size() << '\n';
		for (const Planet& planet: campaign.Planets) {
			Quoted(out, planet.Name) << ' ' << static_cast<int>(planet.Type) << ' ' << planet.System << ' ' << planet.Parent << ' ' << planet.Orbit << ' ' << planet.Angle << ' ' << planet.Size << ' ' << planet.Look << ' ' << planet.Points.size() << '\n';
			for (const Point& point: planet.Points) {
				out << "  ";
				Quoted(out, point.Name) << ' ' << point.Owner << ' ' << point.X << ' ' << point.Y << ' ' << point.Terrain << ' ' << point.Settlement << ' ' << point.Militia << ' ' << point.Built.size();
				for (bool built: point.Built) {
					out << ' ' << built;
				}
				out << ' ' << point.Links.size();
				for (int link: point.Links) {
					out << ' ' << link;
				}
				out << '\n';
			}
		}
		out << "Factions " << campaign.Factions.size() << '\n';
		for (const Faction& faction: campaign.Factions) {
			Quoted(out, faction.Name) << ' ';
			Quoted(out, faction.Module) << ' ' << faction.Color << ' ' << faction.Human << ' ' << faction.Alive << ' ' << faction.Gold << ' ' << faction.HomePlanet << ' ' << faction.Squads.size() << '\n';
			for (const SquadType& type: faction.Squads) {
				out << "  ";
				Quoted(out, type.Name) << ' ';
				Quoted(out, type.Loadout) << ' ' << type.SoldierCost << ' ' << type.Size << ' ' << type.Tier << '\n';
			}
		}
		out << "Armies " << campaign.Armies.size() << '\n';
		for (const Army& army: campaign.Armies) {
			out << army.Id << ' ';
			Quoted(out, army.Name) << ' ' << army.Faction << ' ' << army.System << ' ' << army.Planet << ' ' << army.Point << ' ' << army.Moves << ' ' << army.Path.size();
			for (int system: army.Path) {
				out << ' ' << system;
			}
			out << ' ' << army.Squads.size() << '\n';
			for (const Squad& squad: army.Squads) {
				out << "  " << squad.Type << ' ' << squad.Soldiers.size() << '\n';
				for (const Soldier& soldier: squad.Soldiers) {
					out << "    " << soldier.Id << ' ';
					Quoted(out, soldier.Name) << ' ' << soldier.Rank << ' ' << soldier.Kills << ' ' << soldier.Health << '\n';
				}
			}
		}
		out << "Log " << campaign.Log.size() << '\n';
		for (const std::string& line: campaign.Log) {
			Quoted(out, line) << '\n';
		}
		// Version 2: each point's deposit and yield, planet by planet; each faction's stock; the market.
		out << "Deposits " << campaign.Planets.size() << '\n';
		for (const Planet& planet: campaign.Planets) {
			out << planet.Points.size();
			for (const Point& point: planet.Points) {
				out << ' ' << point.Deposit << ' ' << point.Yield;
			}
			out << '\n';
		}
		out << "Stocks " << campaign.Factions.size() << '\n';
		for (const Faction& faction: campaign.Factions) {
			for (size_t resource = 0; resource < faction.Resources.size(); ++resource) {
				out << (resource > 0 ? " " : "") << faction.Resources[resource];
			}
			out << '\n';
		}
		out << "Market " << campaign.Events.size() << '\n';
		for (size_t resource = 0; resource < campaign.Prices.size(); ++resource) {
			out << (resource > 0 ? " " : "") << campaign.Prices[resource];
		}
		out << '\n';
		for (const GalaxyEvent& event: campaign.Events) {
			out << event.Resource << ' ' << event.PriceFactor << ' ' << event.TurnsLeft << ' ';
			Quoted(out, event.Text) << '\n';
		}
		// Version 3: each pair of factions' stance, opinion and turns in it (first faction below the second); the trade pacts.
		out << "Relations " << campaign.Factions.size() << '\n';
		for (int a = 0; a < static_cast<int>(campaign.Factions.size()); ++a) {
			for (int b = a + 1; b < static_cast<int>(campaign.Factions.size()); ++b) {
				const Relation& relation = RelationOf(static_cast<const Campaign&>(campaign), a, b);
				out << relation.Stance << ' ' << relation.Opinion << ' ' << relation.Turns << '\n';
			}
		}
		out << "Pacts " << campaign.Pacts.size() << '\n';
		for (const Pact& pact: campaign.Pacts) {
			const Deal& deal = pact.Terms;
			out << deal.From << ' ' << deal.To << ' ' << deal.GoldGive << ' ' << deal.GoldGet << ' ' << pact.TurnsLeft;
			for (int amount: deal.Give) {
				out << ' ' << amount;
			}
			for (int amount: deal.Get) {
				out << ' ' << amount;
			}
			out << '\n';
		}
		// Version 4: each army's ship and its hull, in the armies' order.
		out << "Ships " << campaign.Armies.size() << '\n';
		for (const Army& army: campaign.Armies) {
			out << army.Ship << ' ' << army.Hull << '\n';
		}
		// Version 5: each army's order for when it's attacked (OnAttack), in the armies' order.
		out << "Orders " << campaign.Armies.size() << '\n';
		for (const Army& army: campaign.Armies) {
			out << army.IfAttacked << '\n';
		}
		// Version 6: AI attacks on the player's armies waiting to be settled (the turn ends once they are).
		out << "Defences " << campaign.Defences.size() << '\n';
		for (const PendingDefence& defence: campaign.Defences) {
			out << defence.Attacker << ' ' << defence.Planet << ' ' << defence.Point << ' ' << (defence.Overland ? 1 : 0) << '\n';
		}
		// Written to a spare file first, then swapped in, so a crash mid-save doesn't lose the campaign.
		const std::string path = c_Folder + campaign.FileName;
		const std::string spare = path + ".new";
		{
			std::ofstream file(spare, std::ios::binary | std::ios::trunc);
			if (!file) {
				error = "Couldn't write " + spare;
				return false;
			}
			file << out.str();
			if (!file) {
				error = "Couldn't write " + spare;
				return false;
			}
		}
		std::filesystem::rename(spare, path, code);
		if (code) {
			std::filesystem::remove(path, code);
			std::filesystem::rename(spare, path, code);
			if (code) {
				error = "Couldn't replace " + path;
				return false;
			}
		}
		return true;
	}

	bool Load(const std::string& fileName, Campaign& campaign, std::string& error) {
		std::ifstream in(c_Folder + fileName, std::ios::binary);
		if (!in) {
			error = "Couldn't open " + fileName;
			return false;
		}
		Campaign loaded;
		loaded.FileName = fileName;
		std::string magic;
		int version = 0;
		in >> magic >> version;
		if (magic != c_Magic || version < 1 || version > c_Version) {
			error = fileName + " isn't a campaign save this version can read.";
			return false;
		}
		std::string factionNames;
		in >> std::quoted(loaded.Name) >> loaded.Seed >> loaded.Size >> loaded.Difficulty >> loaded.Turn >> loaded.NextId >> loaded.Rolls >> loaded.Winner >> loaded.Lost >> std::quoted(factionNames);
		std::string word;
		size_t count = 0;
		auto section = [&](const char* name) {
			in >> word >> count;
			return in && word == name && count < 100000;
		};
		auto readInts = [&](std::vector<int>& into) {
			size_t n = 0;
			in >> n;
			for (size_t i = 0; i < n && in && n < 10000; ++i) {
				int value = 0;
				in >> value;
				into.push_back(value);
			}
		};
		if (!section("Systems")) {
			error = "Systems missing";
			return false;
		}
		loaded.Systems.resize(count);
		for (StarSystem& system: loaded.Systems) {
			in >> std::quoted(system.Name) >> system.X >> system.Y >> system.StarColor;
			readInts(system.Planets);
			readInts(system.Lanes);
		}
		if (!section("Planets")) {
			error = "Planets missing";
			return false;
		}
		loaded.Planets.resize(count);
		for (Planet& planet: loaded.Planets) {
			int type = 0;
			size_t points = 0;
			in >> std::quoted(planet.Name) >> type >> planet.System >> planet.Parent >> planet.Orbit >> planet.Angle >> planet.Size >> planet.Look >> points;
			planet.Type = static_cast<PlanetType>(std::clamp(type, 0, static_cast<int>(PlanetType::Count) - 1));
			planet.Points.resize(std::min<size_t>(points, 16));
			for (Point& point: planet.Points) {
				size_t built = 0;
				in >> std::quoted(point.Name) >> point.Owner >> point.X >> point.Y >> point.Terrain >> point.Settlement >> point.Militia >> built;
				for (size_t b = 0; b < built && in && built < 64; ++b) {
					bool value = false;
					in >> value;
					if (b < point.Built.size()) {
						point.Built[b] = value;
					}
				}
				readInts(point.Links);
			}
		}
		if (!section("Factions")) {
			error = "Factions missing";
			return false;
		}
		loaded.Factions.resize(count);
		for (Faction& faction: loaded.Factions) {
			size_t types = 0;
			in >> std::quoted(faction.Name) >> std::quoted(faction.Module) >> faction.Color >> faction.Human >> faction.Alive >> faction.Gold >> faction.HomePlanet >> types;
			faction.Squads.resize(std::min<size_t>(types, 256));
			for (SquadType& type: faction.Squads) {
				in >> std::quoted(type.Name) >> std::quoted(type.Loadout) >> type.SoldierCost >> type.Size >> type.Tier;
			}
			if (faction.Squads.empty()) {
				faction.Squads.push_back(SquadType{"Militia", "", 100.0F, 4, 1});
			}
		}
		if (!section("Armies")) {
			error = "Armies missing";
			return false;
		}
		loaded.Armies.resize(count);
		for (Army& army: loaded.Armies) {
			size_t squads = 0;
			in >> army.Id >> std::quoted(army.Name) >> army.Faction >> army.System >> army.Planet >> army.Point >> army.Moves;
			readInts(army.Path);
			in >> squads;
			army.Squads.resize(std::min<size_t>(squads, c_MaxSquads * 4));
			for (Squad& squad: army.Squads) {
				size_t soldiers = 0;
				in >> squad.Type >> soldiers;
				squad.Soldiers.resize(std::min<size_t>(soldiers, c_MaxSquadSize * 4));
				for (Soldier& soldier: squad.Soldiers) {
					in >> soldier.Id >> std::quoted(soldier.Name) >> soldier.Rank >> soldier.Kills >> soldier.Health;
				}
			}
		}
		if (section("Log")) {
			loaded.Log.resize(count);
			for (std::string& line: loaded.Log) {
				in >> std::quoted(line);
			}
		}
		bool resources = false;
		if (version >= 2) {
			if (!section("Deposits") || count != loaded.Planets.size()) {
				error = "Deposits missing";
				return false;
			}
			for (Planet& planet: loaded.Planets) {
				size_t points = 0;
				in >> points;
				for (size_t point = 0; point < points && in && points < 64; ++point) {
					int deposit = 0;
					int yield = 1;
					in >> deposit >> yield;
					if (point < planet.Points.size()) {
						planet.Points[point].Deposit = std::clamp(deposit, 0, c_ResourceCount - 1);
						planet.Points[point].Yield = std::clamp(yield, 0, 9);
					}
				}
			}
			if (!section("Stocks") || count != loaded.Factions.size()) {
				error = "Stocks missing";
				return false;
			}
			for (Faction& faction: loaded.Factions) {
				for (int& amount: faction.Resources) {
					in >> amount;
				}
			}
			if (!section("Market")) {
				error = "Market missing";
				return false;
			}
			for (float& price: loaded.Prices) {
				in >> price;
			}
			loaded.Events.resize(std::min<size_t>(count, 16));
			for (GalaxyEvent& event: loaded.Events) {
				in >> event.Resource >> event.PriceFactor >> event.TurnsLeft >> std::quoted(event.Text);
				event.Resource = std::clamp(event.Resource, 0, c_ResourceCount - 1);
			}
			for (int resource = 0; resource < c_ResourceCount; ++resource) {
				const float base = BasePrice(static_cast<Resource>(resource));
				loaded.Prices[resource] = std::clamp(loaded.Prices[resource], base * 0.3F, base * 4.0F);
			}
			resources = true;
		}
		bool diplomacy = false;
		if (version >= 3) {
			if (!section("Relations") || count != loaded.Factions.size()) {
				error = "Relations missing";
				return false;
			}
			const int factions = static_cast<int>(count);
			loaded.Relations.assign(count * count, Relation());
			for (int a = 0; a < factions; ++a) {
				for (int b = a + 1; b < factions; ++b) {
					Relation& relation = loaded.Relations[static_cast<size_t>(a) * count + static_cast<size_t>(b)];
					in >> relation.Stance >> relation.Opinion >> relation.Turns;
					relation.Stance = std::clamp(relation.Stance, 0, static_cast<int>(Stance::Count) - 1);
					relation.Opinion = std::clamp(relation.Opinion, -100, 100);
					relation.Turns = std::max(relation.Turns, 0);
				}
			}
			if (!section("Pacts")) {
				error = "Pacts missing";
				return false;
			}
			loaded.Pacts.resize(std::min<size_t>(count, 64));
			for (Pact& pact: loaded.Pacts) {
				Deal& deal = pact.Terms;
				in >> deal.From >> deal.To >> deal.GoldGive >> deal.GoldGet >> pact.TurnsLeft;
				for (int& amount: deal.Give) {
					in >> amount;
				}
				for (int& amount: deal.Get) {
					in >> amount;
				}
				deal.Pact = true;
			}
			std::erase_if(loaded.Pacts, [factions](const Pact& pact) { return pact.Terms.From < 0 || pact.Terms.From >= factions || pact.Terms.To < 0 || pact.Terms.To >= factions || pact.Terms.From == pact.Terms.To || pact.TurnsLeft <= 0; });
			diplomacy = true;
		}
		if (version >= 4) {
			// (From before ships, every army has a transport.)
			if (!section("Ships") || count != loaded.Armies.size()) {
				error = "Ships missing";
				return false;
			}
			for (Army& army: loaded.Armies) {
				in >> army.Ship >> army.Hull;
				army.Ship = std::clamp(army.Ship, 0, static_cast<int>(Ship::Count) - 1);
				army.Hull = std::clamp(army.Hull, 0.05F, 1.0F);
			}
		}
		if (version >= 5) {
			// (From before, every army fights.)
			if (!section("Orders") || count != loaded.Armies.size()) {
				error = "Orders missing";
				return false;
			}
			for (Army& army: loaded.Armies) {
				in >> army.IfAttacked;
				army.IfAttacked = std::clamp(army.IfAttacked, 0, static_cast<int>(OnAttack::Count) - 1);
			}
		}
		if (version >= 6) {
			if (!section("Defences")) {
				error = "Defences missing";
				return false;
			}
			loaded.Defences.resize(std::min<size_t>(count, 64));
			for (PendingDefence& defence: loaded.Defences) {
				int overland = 0;
				in >> defence.Attacker >> defence.Planet >> defence.Point >> overland;
				defence.Overland = overland != 0;
			}
			// (Checked against the galaxy when each is settled.)
		}
		if (in.fail() && !in.eof()) {
			error = fileName + " is damaged.";
			return false;
		}
		// Everything that points at something else checked, so a hand-edited or damaged save can't index out of range.
		const int systems = static_cast<int>(loaded.Systems.size());
		const int planets = static_cast<int>(loaded.Planets.size());
		const int factions = static_cast<int>(loaded.Factions.size());
		bool sound = systems > 0 && factions >= 1;
		for (const StarSystem& system: loaded.Systems) {
			for (int planet: system.Planets) {
				sound = sound && planet >= 0 && planet < planets;
			}
			for (int lane: system.Lanes) {
				sound = sound && lane >= 0 && lane < systems;
			}
		}
		for (const Planet& planet: loaded.Planets) {
			sound = sound && planet.System >= 0 && planet.System < systems && planet.Parent < planets;
			for (const Point& point: planet.Points) {
				sound = sound && point.Owner < factions;
				for (int link: point.Links) {
					sound = sound && link >= 0 && link < static_cast<int>(planet.Points.size());
				}
			}
		}
		for (const Faction& faction: loaded.Factions) {
			sound = sound && faction.HomePlanet < planets;
		}
		for (const Army& army: loaded.Armies) {
			sound = sound && army.Faction >= 0 && army.Faction < factions && army.System >= 0 && army.System < systems && army.Planet < planets;
			if (army.Planet >= 0) {
				sound = sound && army.Point >= 0 && army.Point < static_cast<int>(loaded.Planets[army.Planet].Points.size());
			}
			for (int system: army.Path) {
				sound = sound && system >= 0 && system < systems;
			}
			for (const Squad& squad: army.Squads) {
				sound = sound && squad.Type >= 0 && squad.Type < static_cast<int>(loaded.Factions[army.Faction].Squads.size());
			}
		}
		if (!sound) {
			error = fileName + " is damaged.";
			return false;
		}
		if (!resources) {
			// A save from before resources: deposits rolled from its seed, starting stock and normal prices.
			AddResources(loaded);
		}
		if (!diplomacy) {
			// From before diplomacy: everyone Neutral.
			ResetRelations(loaded);
		}
		campaign = std::move(loaded);
		return true;
	}

	std::vector<SaveInfo> ListSaves() {
		std::vector<SaveInfo> saves;
		std::error_code code;
		if (!std::filesystem::is_directory(c_Folder, code)) {
			return saves;
		}
		for (const std::filesystem::directory_entry& entry: std::filesystem::directory_iterator(c_Folder, code)) {
			if (!entry.is_regular_file(code) || entry.path().extension() != ".txt") {
				continue;
			}
			std::ifstream in(entry.path(), std::ios::binary);
			std::string magic;
			int version = 0;
			in >> magic >> version;
			if (magic != c_Magic) {
				continue;
			}
			SaveInfo info;
			info.FileName = entry.path().filename().string();
			std::uint32_t seed = 0;
			int size = 0;
			int difficulty = 0;
			in >> std::quoted(info.Name) >> seed >> size >> difficulty >> info.Turn;
			std::string rest;
			std::getline(in, rest);
			in >> std::quoted(info.Factions);
			info.Modified = static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(entry.last_write_time(code).time_since_epoch()).count());
			saves.push_back(info);
		}
		std::sort(saves.begin(), saves.end(), [](const SaveInfo& a, const SaveInfo& b) { return a.Modified > b.Modified; });
		return saves;
	}

	bool DeleteSave(const std::string& fileName) {
		std::error_code code;
		return std::filesystem::remove(c_Folder + fileName, code);
	}
} // namespace RTE::CampaignModel
