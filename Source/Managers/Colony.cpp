#include "Colony.h"

#include "Actor.h"
#include "Constants.h"
#include "MovableMan.h"
#include "Sandbox.h"
#include "SceneMan.h"
#include "TimerMan.h"

#include <algorithm>
#include <array>

using namespace RTE;

namespace {
	constexpr Colony::Type c_Types[] = {
	    {"Barracks", "Trains units for its side, one after another, and keeps a number of them alive.", 120, 72},
	    {"Extractor", "Earns supply for its side.", 48, 60},
	    {"Generator", "Powers its side's buildings in range, when buildings need power.", 56, 64},
	};
	constexpr float c_StartingSupply = 1500.0F;
	constexpr float c_BaseIncome = 4.0F; //!< Supply a second for a side with any building.
	constexpr float c_ExtractorIncome = 12.0F; //!< Supply a second from each extractor.
	constexpr float c_GeneratorPower = 100.0F; //!< Power each generator gives: enough for two barracks training at once.
	constexpr float c_TrainingPower = 50.0F; //!< Power a barracks draws while it trains.
	constexpr float c_PowerRange = 500.0F; //!< How far a generator reaches, from ground to ground.
	constexpr float c_UnpoweredPace = 0.25F; //!< With NoPower::Slows, how fast a barracks with no power trains, as a share of its full pace.
	constexpr float c_WreckedShare = 0.45F; //!< With less than this much of it left, a building is a ruin.

	std::vector<Colony::Building> s_Buildings;
	std::array<float, 4> s_Supply = {c_StartingSupply, c_StartingSupply, c_StartingSupply, c_StartingSupply};
	bool s_Free = true;
	bool s_NeedsPower = false;
	Colony::NoPower s_WithoutPower = Colony::NoPower::Stops;
	int s_NextID = 1;

	bool IsAir(int x, int y) { return g_SceneMan.GetTerrMatter(x, y) == g_MaterialAir; }

	/// How much of the plot of a building is solid, counted on every other pixel.
	int CountSolid(const Colony::Building& building) {
		const Colony::Type& type = Colony::GetType(building.What);
		int left = building.Ground.GetFloorIntX() - type.Width / 2;
		int top = building.Ground.GetFloorIntY() - type.Height;
		int solid = 0;
		for (int y = top; y < top + type.Height; y += 2) {
			for (int x = left; x < left + type.Width; x += 2) {
				if (!IsAir(x, y)) {
					++solid;
				}
			}
		}
		return solid;
	}

	/// The generators of a side that reach a building and are running.
	std::vector<Colony::Building*> GeneratorsFor(const Colony::Building& building) {
		std::vector<Colony::Building*> generators;
		for (Colony::Building& generator: s_Buildings) {
			if (generator.What == Colony::Kind::Generator && generator.Team == building.Team && !generator.Paused && (generator.Ground - building.Ground).MagnitudeIsLessThan(c_PowerRange)) {
				generators.push_back(&generator);
			}
		}
		return generators;
	}

	/// Takes up to this much power for a building from its side's generators in range. Generator::Power holds what each has given out.
	/// @return How much it got.
	float DrawPower(const Colony::Building& building, float wanted) {
		float got = 0.0F;
		for (Colony::Building* generator: GeneratorsFor(building)) {
			float take = std::min(wanted - got, c_GeneratorPower - generator->Power);
			if (take > 0.0F) {
				generator->Power += take;
				got += take;
			}
		}
		return got;
	}

	/// How much power a building could still get from its side's generators in range.
	float SparePower(const Colony::Building& building) {
		float spare = 0.0F;
		for (const Colony::Building* generator: GeneratorsFor(building)) {
			spare += std::max(c_GeneratorPower - generator->Power, 0.0F);
		}
		return spare;
	}
} // namespace

const Colony::Type& Colony::GetType(Kind kind) { return c_Types[std::clamp(static_cast<int>(kind), 0, static_cast<int>(Kind::Count) - 1)]; }

std::vector<Colony::Building>& Colony::Buildings() { return s_Buildings; }

float& Colony::Supply(int team) { return s_Supply[std::clamp(team, 0, 3)]; }

bool& Colony::Free() { return s_Free; }

bool& Colony::NeedsPower() { return s_NeedsPower; }

Colony::NoPower& Colony::WithoutPower() { return s_WithoutPower; }

float Colony::PowerRange() { return c_PowerRange; }

float Colony::GeneratorPower() { return c_GeneratorPower; }

float Colony::TrainingPower() { return c_TrainingPower; }

float Colony::TrainingSeconds(float cost) { return std::clamp(cost / 40.0F, 4.0F, 30.0F); }

void Colony::Clear() {
	s_Buildings.clear();
	s_Supply.fill(c_StartingSupply);
	s_NextID = 1;
}

void Colony::Remove(int id) {
	s_Buildings.erase(std::remove_if(s_Buildings.begin(), s_Buildings.end(), [id](const Building& building) { return building.ID == id; }), s_Buildings.end());
}

int Colony::Place(Kind kind, const Vector& place, int team, const std::string& unit, int orders, int keepAlive) {
	if (!g_SceneMan.GetScene() || kind == Kind::Count) {
		return -1;
	}
	// Onto the ground: up out of it if the place is inside it, then down to it.
	int x = place.GetFloorIntX();
	int y = place.GetFloorIntY();
	int sceneHeight = g_SceneMan.GetSceneHeight();
	for (int tries = 0; tries < 600 && !IsAir(x, y); ++tries) {
		--y;
	}
	while (y < sceneHeight - 2 && IsAir(x, y + 1)) {
		++y;
	}
	if (y >= sceneHeight - 2) {
		return -1;
	}
	const Type& type = GetType(kind);
	Building building;
	building.ID = s_NextID++;
	building.What = kind;
	building.Team = std::clamp(team, 0, 3);
	building.Ground = Vector(static_cast<float>(x), static_cast<float>(y + 1));
	building.Unit = unit;
	building.Orders = orders;
	building.KeepAlive = std::clamp(keepAlive, 1, 30);

	float left = building.Ground.m_X - static_cast<float>(type.Width / 2);
	float top = building.Ground.m_Y - static_cast<float>(type.Height);
	float width = static_cast<float>(type.Width);
	// The plot is cleared, and what is under it made level, so it stands the same on a slope as on the flat.
	Sandbox::FillBox(Vector(left, top), type.Width, type.Height, "");
	Sandbox::FillBox(Vector(left, building.Ground.m_Y), type.Width, 14, "Concrete");
	if (kind == Kind::Barracks) {
		// A floor, a roof, a solid wall at the back and a doorway at the front.
		Sandbox::FillBox(Vector(left, building.Ground.m_Y - 8.0F), type.Width, 8, "Concrete");
		Sandbox::FillBox(Vector(left, top), type.Width, 8, "Concrete");
		Sandbox::FillBox(Vector(left, top), 8, type.Height, "Concrete");
		Sandbox::FillBox(Vector(left + width - 8.0F, top), 8, 14, "Concrete");
	} else if (kind == Kind::Generator) {
		// A low hall with two stacks.
		Sandbox::FillBox(Vector(left, building.Ground.m_Y - 30.0F), type.Width, 30, "Concrete");
		Sandbox::FillBox(Vector(left + 8.0F, top), 8, type.Height - 30, "Concrete");
		Sandbox::FillBox(Vector(left + width - 16.0F, top + 12.0F), 8, type.Height - 42, "Concrete");
	} else {
		// A squat block with a mast.
		Sandbox::FillBox(Vector(left, building.Ground.m_Y - 26.0F), type.Width, 26, "Concrete");
		Sandbox::FillBox(Vector(building.Ground.m_X - 3.0F, top), 6, type.Height - 26, "Concrete");
		Sandbox::FillBox(Vector(building.Ground.m_X - 10.0F, top), 20, 5, "Concrete");
	}
	building.SolidAtStart = std::max(CountSolid(building), 1);
	s_Buildings.push_back(building);
	return building.ID;
}

void Colony::Update() {
	float seconds = g_TimerMan.GetDeltaTimeSecs();
	bool checkWrecks = g_TimerMan.GetSimUpdateCount() % 30 == 0;
	std::array<float, 4> income{};
	std::array<bool, 4> hasBuilding{};
	for (Building& building: s_Buildings) {
		if (building.What == Kind::Generator) {
			building.Power = 0.0F; // Given out afresh each update, to the barracks in the order they were built.
		}
		hasBuilding[building.Team] = true;
		if (building.What == Kind::Extractor && !building.Paused) {
			income[building.Team] += c_ExtractorIncome;
		}
	}
	for (int team = 0; team < 4; ++team) {
		if (hasBuilding[team]) {
			s_Supply[team] = std::min(s_Supply[team] + (income[team] + c_BaseIncome) * seconds, 999999.0F);
		}
	}

	std::vector<int> wrecked;
	for (Building& building: s_Buildings) {
		if (checkWrecks && static_cast<float>(CountSolid(building)) < static_cast<float>(building.SolidAtStart) * c_WreckedShare) {
			wrecked.push_back(building.ID);
			continue;
		}
		if (building.What == Kind::Extractor) {
			building.Status = building.Paused ? "Stopped" : "Earning supply";
			continue;
		}
		if (building.What == Kind::Generator) {
			continue; // Its status is set once every barracks has drawn on it, below.
		}
		building.Power = 1.0F;
		building.NoPower = false;
		// (Dead too, not just gone: a body is a valid actor until it settles, and was counted alive meanwhile, so no replacement was made.)
		building.Alive.erase(std::remove_if(building.Alive.begin(), building.Alive.end(), [](const std::pair<Actor*, long>& unit) {
			                     return !g_MovableMan.IsActor(unit.first) || static_cast<long>(unit.first->GetUniqueID()) != unit.second || unit.first->IsDead() || unit.first->GetHealth() <= 0.0F;
		                     }),
		                     building.Alive.end());
		if (building.Paused) {
			building.Status = "Stopped";
			continue;
		}
		if (building.Unit.empty()) {
			building.Status = "No unit chosen";
			continue;
		}
		if (!building.Paid && static_cast<int>(building.Alive.size()) >= building.KeepAlive) {
			building.Status = "All its units are alive";
			continue;
		}
		float cost = std::max(Sandbox::UnitCost(building.Unit), 20.0F);
		if (s_NeedsPower) {
			// Without the power, it does not start (so pays nothing), or with Slows, goes on at what it gets, or a quarter pace with none.
			if (s_WithoutPower == NoPower::Stops && SparePower(building) < c_TrainingPower) {
				building.Power = 0.0F;
				building.NoPower = true;
				building.Status = "No power";
				continue;
			}
			building.Power = DrawPower(building, c_TrainingPower) / c_TrainingPower;
			building.NoPower = building.Power < 1.0F;
		}
		if (!building.Paid) {
			if (!s_Free && s_Supply[building.Team] < cost) {
				building.Status = "Waiting for supply";
				continue;
			}
			if (!s_Free) {
				s_Supply[building.Team] -= cost;
			}
			building.Paid = true;
			building.Progress = 0.0F;
		}
		building.Status = "Training " + building.Unit + (!building.NoPower ? "" : building.Power > 0.0F ? " on low power" : " slowly: no power");
		float pace = building.NoPower ? std::max(building.Power, c_UnpoweredPace) : 1.0F;
		building.Progress += pace * seconds / TrainingSeconds(cost);
		if (building.Progress >= 1.0F) {
			building.Progress = 0.0F;
			building.Paid = false;
			// Inside, by the doorway.
			Vector door = building.Ground + Vector(static_cast<float>(GetType(building.What).Width / 2) - 30.0F, -34.0F);
			if (Actor* unit = Sandbox::SpawnUnit(building.Unit, building.Team, door, building.Orders)) {
				building.Alive.emplace_back(unit, static_cast<long>(unit->GetUniqueID()));
				++building.Produced;
			} else {
				building.Status = "Can not make " + building.Unit;
				building.Paused = true;
			}
		}
	}
	for (Building& generator: s_Buildings) {
		if (generator.What == Kind::Generator) {
			generator.Status = generator.Paused ? "Stopped" : !s_NeedsPower ? "Idle: buildings run without power" : "Giving " + std::to_string(static_cast<int>(generator.Power + 0.5F)) + " of " + std::to_string(static_cast<int>(c_GeneratorPower)) + " power";
		}
	}
	for (int id: wrecked) {
		Remove(id);
	}
}
