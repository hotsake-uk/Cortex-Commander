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
		constexpr int c_Version = 1;

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
