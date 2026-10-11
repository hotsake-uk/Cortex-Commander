#include "Colony.h"

#include "ActivityMan.h"
#include "Actor.h"
#include "AHuman.h"
#include "Box.h"
#include "Constants.h"
#include "FluidSim.h"
#include "HeldDevice.h"
#include "MovableMan.h"
#include "PresetMan.h"
#include "Sandbox.h"
#include "Scene.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "TerrainCollapse.h"
#include "TerrainObject.h"
#include "TerrainTrees.h"
#include "TimerMan.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace RTE;

namespace {
	// Each kind's two looks: its medieval building (Fantasy) and its prefab (Sci-fi). Both are drawn to the kind's plot.
	constexpr Colony::Type c_Types[] = {
	    {"Barracks", "Trains units for its side, one after another, and keeps a number of them alive.", 240, 132,
	     {{"Longhouse", "Medieval.rte", nullptr, nullptr, nullptr}, {"Clone Barracks", "Base.rte", nullptr, nullptr, nullptr}}},
	    {"Extractor", "Earns supply for its side.", 96, 112,
	     {{"Peasant Hut", "Medieval.rte", nullptr, nullptr, nullptr}, {"Ore Extractor", "Base.rte", nullptr, nullptr, nullptr}}},
	    {"Generator", "Powers its side's buildings in range, when buildings need power.", 92, 120,
	     {{"Round Hut", "Medieval.rte", nullptr, nullptr, nullptr}, {"Fusion Generator", "Base.rte", nullptr, nullptr, nullptr}}},
	    {"Woodcutter", "Keeps two staff, who fell the trees round it, cut them into logs and bring them home as wood. They plant a sapling where each tree stood.", 108, 108,
	     {{"Woodcutter's Hut", "Medieval.rte", "Woodcutter", "Medieval.rte", "Woodcutter's Axe"}, {"Lumber Mill", "Base.rte", "Forester", "Base.rte", "Laser Saw"}}},
	};
	static_assert(std::size(c_Types) == static_cast<size_t>(Colony::Kind::Count), "c_Types must have an entry for each Colony::Kind.");
	constexpr const char* c_StyleNames[] = {"Fantasy", "Sci-fi"};
	static_assert(std::size(c_StyleNames) == static_cast<size_t>(Colony::Style::Count), "c_StyleNames must name each Colony::Style.");

	constexpr float c_StartingSupply = 1500.0F;
	constexpr float c_BaseIncome = 4.0F; //!< Supply a second for a side with any building.
	constexpr float c_ExtractorIncome = 12.0F; //!< Supply a second from each extractor.
	constexpr float c_GeneratorPower = 100.0F; //!< Power each generator gives: enough for two barracks training at once.
	constexpr float c_TrainingPower = 50.0F; //!< Power a barracks draws while it trains.
	constexpr float c_PowerRange = 500.0F; //!< How far a generator reaches, from ground to ground.
	constexpr float c_UnpoweredPace = 0.25F; //!< With NoPower::Slows, how fast a barracks with no power trains, as a share of its full pace.
	constexpr const char* c_Brain = "Brain Case"; //!< The brain put in each building.
	constexpr int c_BrainRoomWidth = 20; //!< The chamber at the back of a building its brain sits in.
	constexpr int c_BrainRoomHeight = 20;
	constexpr float c_WreckedShare = 0.45F; //!< With less than this much of it left, a building is a ruin.

	// Woodcutters.
	constexpr int c_LogPixels = 50; //!< Trunk pixels to a log.
	constexpr int c_CarryLogs = 2; //!< Logs a worker carries home at a time.
	constexpr long long c_HireMS = 20000; //!< How long a woodcutter takes to take on someone new, after one of its staff is lost.
	constexpr float c_Reach = 46.0F; //!< How far from a worker's middle what it cuts can be.
	constexpr int c_BandHeight = 7; //!< How tall the cut through a trunk is.
	constexpr int c_BandAbove = 5; //!< How far above the ground its bottom is.
	constexpr long long c_SkipMS = 90000; //!< How long a worker leaves be a tree it couldn't get to or fell.
	constexpr long long c_SaplingStepMS = 100000; //!< How long a sapling takes to grow a step (three steps to a tree).
	constexpr float c_SaplingScales[] = {0.35F, 0.65F, 1.0F};

	/// How a tool cuts: how often, and how much each time.
	struct Cutting {
		long long EveryMS;
		int Pixels;
	};
	constexpr Cutting c_AxeCutting = {650, 14}; //!< A swing of the axe.
	constexpr Cutting c_SawCutting = {160, 4}; //!< The laser saw, as it runs.

	std::vector<Colony::Building> s_Buildings;
	std::array<float, 4> s_Supply = {c_StartingSupply, c_StartingSupply, c_StartingSupply, c_StartingSupply};
	std::array<int, 4> s_Wood = {};
	bool s_Free = true;
	bool s_NeedsPower = false;
	bool s_Regrow = true;
	Colony::NoPower s_WithoutPower = Colony::NoPower::Stops;
	int s_NextID = 1;

	bool IsAir(int x, int y) { return g_SceneMan.GetTerrMatter(x, y) == g_MaterialAir; }

	SLTerrain* Terrain() {
		Scene* scene = g_SceneMan.GetScene();
		return scene ? scene->GetTerrain() : nullptr;
	}

	/// What a scene pixel is made of, the scene wrapped round as it does: out of bounds off its edges.
	int MaterialAt(int x, int y) {
		if (!g_SceneMan.WrapPosition(x, y) && (x < 0 || y < 0 || x >= g_SceneMan.GetSceneWidth() || y >= g_SceneMan.GetSceneHeight())) {
			return g_MaterialOutOfBounds;
		}
		if (x < 0 || y < 0 || x >= g_SceneMan.GetSceneWidth() || y >= g_SceneMan.GetSceneHeight()) {
			return g_MaterialOutOfBounds;
		}
		return g_SceneMan.GetTerrMatter(x, y);
	}

	/// Whether a material is ground to stand on: solid, and not a tree's or a liquid.
	bool IsGround(int material) { return material != g_MaterialAir && !TerrainTrees::IsTreeMaterial(material) && !FluidSim::IsLiquid(material); }

	/// The first ground at or below a point in a column, out of anything it is inside first; -1 if there is none within reach.
	int GroundBelow(int x, int y, int reach = 200) {
		for (int up = 0; up < reach && IsGround(MaterialAt(x, y)); ++up) {
			--y;
		}
		for (int down = 0; down < reach; ++down, ++y) {
			int material = MaterialAt(x, y);
			if (material == g_MaterialOutOfBounds) {
				return -1;
			}
			if (IsGround(material)) {
				return y;
			}
		}
		return -1;
	}

	long long Now() { return static_cast<long long>(g_TimerMan.GetSimTimeMS()); }

	/// How much of the plot of a building is solid, counted on every other pixel.
	int CountSolid(const Colony::Building& building) {
		int left = building.Ground.GetFloorIntX() - building.Width() / 2;
		int top = building.Ground.GetFloorIntY() - building.Height();
		int solid = 0;
		for (int y = top; y < top + building.Height(); y += 2) {
			for (int x = left; x < left + building.Width(); x += 2) {
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
			if (generator.What == Colony::Kind::Generator && generator.Team == building.Team && !generator.Paused && !generator.Disabled() && (generator.Ground - building.Ground).MagnitudeIsLessThan(c_PowerRange)) {
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
			float take = std::min(wanted - got, Colony::GeneratorPower(*generator) - generator->Power);
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
			spare += std::max(Colony::GeneratorPower(*generator) - generator->Power, 0.0F);
		}
		return spare;
	}

	/// Whether a unit kept by its unique ID is still alive.
	bool StillAlive(const Actor* unit, long id) {
		return unit && g_MovableMan.IsActor(unit) && static_cast<long>(unit->GetUniqueID()) == id && !unit->IsDead() && unit->GetHealth() > 0.0F;
	}

	/// Whether a building's brain is still alive.
	bool BrainAlive(const Colony::Building& building) { return StillAlive(building.Brain, building.BrainID); }

	/// Makes a brain the one that runs a building.
	void GiveBrain(Colony::Building& building, Actor* brain) {
		brain->AddToGroup(Colony::c_BrainGroup);
		building.Brain = brain;
		building.BrainID = static_cast<long>(brain->GetUniqueID());
	}

	constexpr int c_LiteFloor = 6; //!< How thick a lite building's walls are under and over its brain's chamber.

	/// Where in a building its brain's chamber is: its bottom left corner, on the floor. (A lite building's is in the middle of its box.)
	Vector BrainRoom(const Colony::Building& building) {
		if (building.Lite) {
			return Vector(building.Ground.m_X - static_cast<float>(c_BrainRoomWidth / 2), building.Ground.m_Y - static_cast<float>(c_LiteFloor));
		}
		return Vector(building.Ground.m_X - static_cast<float>(building.Width() / 2) + 10.0F, building.Ground.m_Y);
	}

	/// A brain of a building's side, standing in its plot, that runs no other building and is no player's brain, to take over the building. Nothing if there is none.
	Actor* BrainInPlot(const Colony::Building& building) {
		Vector middle = building.Ground - Vector(0.0F, static_cast<float>(building.Height() / 2));
		Actor* brain = g_MovableMan.GetClosestBrainActor(building.Team, middle);
		if (!brain || brain->IsDead() || (g_ActivityMan.GetActivity() && g_ActivityMan.GetActivity()->IsAssignedBrain(brain))) {
			return nullptr;
		}
		Vector offset = g_SceneMan.ShortestDistance(middle, brain->GetPos(), g_SceneMan.SceneWrapsX());
		if (std::abs(offset.m_X) > static_cast<float>(building.Width() / 2) || std::abs(offset.m_Y) > static_cast<float>(building.Height() / 2)) {
			return nullptr;
		}
		for (const Colony::Building& other: s_Buildings) {
			if (other.Brain == brain) {
				return nullptr;
			}
		}
		return brain;
	}

#pragma region Woodcutters
	constexpr int c_LiteDoorstep = 24; //!< How far out from a lite building's box, on its right, its doorstep reaches: where its units and staff come out and logs are brought.

	/// The way in, on the floor inside: where staff come out and logs are brought. (A lite building's is just outside its box, on its right.)
	Vector Door(const Colony::Building& building) {
		if (building.Lite) {
			return building.Ground + Vector(static_cast<float>(building.Width() / 2 + c_LiteDoorstep / 2), -2.0F);
		}
		return building.Ground + Vector(static_cast<float>(building.Width() / 2) - 30.0F, -2.0F);
	}

	/// Whether a point is inside a building's plot (or on a lite building's doorstep).
	bool InPlot(const Colony::Building& building, const Vector& point) {
		Vector offset = g_SceneMan.ShortestDistance(building.Ground, point, g_SceneMan.SceneWrapsX());
		float left = -static_cast<float>(building.Width() / 2);
		float right = static_cast<float>(building.Width() / 2) + (building.Lite ? static_cast<float>(c_LiteDoorstep) : 0.0F);
		return offset.m_X >= left && offset.m_X <= right && offset.m_Y <= 4.0F && offset.m_Y >= -static_cast<float>(std::max(building.Height(), 40));
	}

	/// Whether a tree is claimed by a worker other than this one, or is one of the saplings still growing.
	bool TakenOrYoung(const TerrainTrees::Tree& tree, const Colony::Worker* self) {
		for (const Colony::Building& building: s_Buildings) {
			for (const Colony::Worker& worker: building.Staff) {
				if (&worker != self && worker.Tree == tree.ID && (worker.Doing == Colony::Job::ToTree || worker.Doing == Colony::Job::Chop)) {
					return true;
				}
			}
			for (const Colony::Sapling& sapling: building.Saplings) {
				if (std::abs(sapling.X - tree.BaseX) <= 12 && std::abs(sapling.Y - tree.BaseY) <= 24) {
					return true;
				}
			}
		}
		return false;
	}

	bool Skipping(Colony::Worker& worker, long tree) {
		long long now = Now();
		worker.Skip.erase(std::remove_if(worker.Skip.begin(), worker.Skip.end(), [now](const std::pair<long, long long>& skip) { return skip.second <= now; }), worker.Skip.end());
		return std::any_of(worker.Skip.begin(), worker.Skip.end(), [tree](const std::pair<long, long long>& skip) { return skip.first == tree; });
	}

	void Skip(Colony::Worker& worker, long tree) {
		if (tree != 0) {
			worker.Skip.emplace_back(tree, Now() + c_SkipMS);
		}
	}

	/// Sends a worker somewhere on foot.
	void Send(Colony::Worker& worker, const Vector& spot) {
		Actor* unit = worker.Unit;
		bool again = (worker.Spot - spot).MagnitudeIsLessThan(8.0F);
		worker.Tries = again ? worker.Tries + 1 : 0;
		worker.Spot = spot;
		worker.MoveMS = Now();
		unit->ClearAIWaypoints();
		unit->AddAISceneWaypoint(spot);
		unit->SetAIMode(Actor::AIMODE_GOTO);
		worker.Serial = unit->GetAIOrderSerial();
	}

	/// Whether a worker stands at a spot (a place on the ground).
	bool At(const Colony::Worker& worker, const Vector& spot, float across = 14.0F) {
		Vector offset = g_SceneMan.ShortestDistance(worker.Unit->GetPos(), spot, g_SceneMan.SceneWrapsX());
		return std::abs(offset.m_X) <= across && offset.m_Y > -12.0F && offset.m_Y < worker.Unit->GetHeight() * 0.6F + 10.0F;
	}

	/// Gets a worker to a spot: sent there if it isn't on its way, again if it stopped short. False once it has tried too often to get there.
	bool GoTo(Colony::Worker& worker, const Vector& spot) {
		Actor* unit = worker.Unit;
		bool onItsWay = unit->GetAIMode() == Actor::AIMODE_GOTO && (unit->GetLastAIWaypoint() - spot).MagnitudeIsLessThan(24.0F);
		if (!onItsWay) {
			if (worker.Tries >= 3 && (worker.Spot - spot).MagnitudeIsLessThan(8.0F)) {
				return false;
			}
			Send(worker, spot);
		} else if (Now() - worker.MoveMS > 15000 + static_cast<long long>(g_SceneMan.ShortestDistance(unit->GetPos(), spot, g_SceneMan.SceneWrapsX()).GetMagnitude() * 60.0F)) {
			// Too long on the way: stuck.
			return false;
		}
		return true;
	}

	/// Stops whatever a worker is cutting with its tool (WorkerBehaviors.lua swings it while ColonyWork is 1).
	void StopCutting(Actor* unit) {
		unit->SetNumberValue("ColonyWork", 0);
	}

	void Cut(Actor* unit, const Vector& aim) {
		unit->SetNumberValue("ColonyWork", 1);
		unit->SetNumberValue("ColonyWorkX", aim.m_X);
		unit->SetNumberValue("ColonyWorkY", aim.m_Y);
	}

	/// Whether a worker is swinging its tool at what it is to cut, in reach of it (its AI says so).
	bool Swinging(const Colony::Worker& worker) {
		const Actor* unit = worker.Unit;
		return unit->NumberValueExists("ColonyWorkSwing") && unit->GetNumberValue("ColonyWorkSwing") > 0.0 &&
		       g_SceneMan.ShortestDistance(unit->GetPos(), worker.Aim, g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(c_Reach + 8.0F);
	}

	/// What a worker cuts with, as it cuts.
	/// How a building's staff cut, faster as it is upgraded.
	Cutting CuttingOf(const Colony::Building& building) {
		Cutting cutting = building.Looks == Colony::Style::SciFi ? c_SawCutting : c_AxeCutting;
		cutting.EveryMS = static_cast<long long>(static_cast<float>(cutting.EveryMS) / Colony::LevelPace(building));
		return cutting;
	}

	/// Puts something a worker has in its hands (by name), out of its inventory if need be.
	void Hold(Actor* unit, const char* presetName) {
		AHuman* human = dynamic_cast<AHuman*>(unit);
		if (!human || !presetName) {
			return;
		}
		const HeldDevice* held = human->GetEquippedItem();
		if (held && held->GetPresetName() == presetName) {
			return;
		}
		human->EquipNamedDevice(presetName, true);
	}

	/// How many logs a worker has, in its hands and its inventory.
	int LogsOf(const Actor* unit) {
		int logs = 0;
		if (const AHuman* human = dynamic_cast<const AHuman*>(unit); human && human->GetEquippedItem() && human->GetEquippedItem()->GetPresetName() == Colony::c_Log) {
			++logs;
		}
		for (const MovableObject* item: *unit->GetInventory()) {
			if (item && item->GetPresetName() == Colony::c_Log) {
				++logs;
			}
		}
		return logs;
	}

	/// Takes a worker's logs off it. @return How many there were.
	int TakeLogs(Actor* unit) {
		int logs = LogsOf(unit);
		if (AHuman* human = dynamic_cast<AHuman*>(unit); human && human->GetEquippedItem() && human->GetEquippedItem()->GetPresetName() == Colony::c_Log) {
			human->UnequipFGArm();
		}
		for (int i = 0; i < logs; ++i) {
			unit->RemoveInventoryItem(Colony::c_Log);
		}
		return logs;
	}

	/// A new log, for a worker's inventory or the ground. Nothing without Base.rte's log.
	HeldDevice* MakeLog() {
		const HeldDevice* preset = dynamic_cast<const HeldDevice*>(g_PresetMan.GetEntityPreset("HeldDevice", Colony::c_Log, "Base.rte"));
		return preset ? dynamic_cast<HeldDevice*>(preset->Clone()) : nullptr;
	}

	void GiveLog(Actor* unit) {
		if (HeldDevice* log = MakeLog()) {
			unit->AddToInventoryBack(log);
		}
	}

	/// Clears one pixel of terrain to air.
	void ClearPixel(SLTerrain* terrain, int x, int y) {
		g_SceneMan.WrapPosition(x, y);
		terrain->SetMaterialPixel(x, y, g_MaterialAir);
		terrain->SetFGColorPixel(x, y, ColorKeys::g_MaskColor);
	}

	/// Tells the rest of the world a box of terrain was cut away.
	void NoteCut(SLTerrain* terrain, int left, int top, int right, int bottom) {
		Box box(Vector(static_cast<float>(left), static_cast<float>(top)), static_cast<float>(right - left + 1), static_cast<float>(bottom - top + 1));
		terrain->AddUpdatedMaterialArea(box);
		TerrainTrees::NoteChanged();
		FluidSim::Disturb(box.GetCenter(), std::max(box.GetWidth(), box.GetHeight()) * 0.5F + 2.0F);
	}

	/// The pixels of a material kind in a box, nearest a point first.
	template <typename Which> std::vector<std::pair<int, int>> PixelsIn(int left, int top, int right, int bottom, const Vector& from, Which which) {
		std::vector<std::pair<float, std::pair<int, int>>> found;
		for (int y = top; y <= bottom; ++y) {
			for (int x = left; x <= right; ++x) {
				if (which(x, y, MaterialAt(x, y))) {
					float dx = static_cast<float>(x) - from.m_X;
					float dy = static_cast<float>(y) - from.m_Y;
					found.push_back({dx * dx + dy * dy, {x, y}});
				}
			}
		}
		std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		std::vector<std::pair<int, int>> pixels;
		pixels.reserve(found.size());
		for (const auto& [distance, pixel]: found) {
			pixels.push_back(pixel);
		}
		return pixels;
	}

	/// Picks the nearest standing tree in a woodcutter's reach for a worker to fell, and where to stand and cut it. False if there is none.
	bool PickTree(const Colony::Building& building, Colony::Worker& worker) {
		std::vector<const TerrainTrees::Tree*> near;
		TerrainTrees::FindTreesNear(building.Ground, building.WorkRadius, near);
		// Nearest the worker, not the hut, so a worker out in the woods goes on to the next tree.
		const Vector from = worker.Unit->GetPos();
		std::stable_sort(near.begin(), near.end(), [&from](const TerrainTrees::Tree* a, const TerrainTrees::Tree* b) {
			return std::abs(g_SceneMan.ShortestDistance(from, Vector(static_cast<float>(a->BaseX), static_cast<float>(a->BaseY)), g_SceneMan.SceneWrapsX()).m_X) <
			       std::abs(g_SceneMan.ShortestDistance(from, Vector(static_cast<float>(b->BaseX), static_cast<float>(b->BaseY)), g_SceneMan.SceneWrapsX()).m_X);
		});
		for (const TerrainTrees::Tree* tree: near) {
			if (!tree->Standing() || tree->TrunkPixels < 60 || TakenOrYoung(*tree, &worker) || Skipping(worker, tree->ID) || InPlot(building, Vector(static_cast<float>(tree->BaseX), static_cast<float>(tree->BaseY)))) {
				continue;
			}
			// From the side it comes from, beside the trunk, on the ground there.
			int side = g_SceneMan.ShortestDistance(Vector(static_cast<float>(tree->BaseX), 0.0F), Vector(from.m_X, 0.0F), g_SceneMan.SceneWrapsX()).m_X < 0.0F ? -1 : 1;
			int standX = side < 0 ? tree->TrunkLeft - 9 : tree->TrunkRight + 9;
			int ground = GroundBelow(standX, tree->TrunkTop);
			if (ground < 0 || ground > tree->BaseY + 40) {
				continue;
			}
			// The cut: a band through the trunk at knee height, over the ground beside it.
			int bandBottom = ground - c_BandAbove;
			int bandTop = bandBottom - c_BandHeight + 1;
			int left = tree->TrunkRight;
			int right = tree->TrunkLeft;
			for (int y = bandTop; y <= bandBottom; ++y) {
				for (int x = tree->TrunkLeft; x <= tree->TrunkRight; ++x) {
					if (TerrainTrees::IsTrunk(MaterialAt(x, y)) && TerrainTrees::OwnerIDAt(x, y) == tree->ID) {
						left = std::min(left, x);
						right = std::max(right, x);
					}
				}
			}
			if (right < left) {
				continue; // (No trunk there: a tree leaning, or standing on a ledge over its own roots.)
			}
			worker.Tree = tree->ID;
			worker.BandLeft = left;
			worker.BandRight = right;
			worker.BandTop = bandTop;
			worker.BandBottom = bandBottom;
			worker.StumpX = tree->BaseX;
			worker.StumpY = ground;
			worker.Aim = Vector(static_cast<float>(side < 0 ? left : right), static_cast<float>((bandTop + bandBottom) / 2));
			worker.Spot = Vector(static_cast<float>(side < 0 ? left - 9 : right + 9), static_cast<float>(ground - 1));
			worker.Tries = 0;
			// Where it can come down, and the other trees standing there, which stay.
			int height = tree->BaseY - tree->Top;
			worker.AreaLeft = tree->Left - height;
			worker.AreaRight = tree->Right + height;
			worker.AreaTop = tree->Top - 20;
			worker.AreaBottom = ground + 2;
			worker.Spare.clear();
			std::vector<const TerrainTrees::Tree*> around;
			TerrainTrees::FindTreesNear(Vector(static_cast<float>(tree->BaseX), static_cast<float>(tree->BaseY)), static_cast<float>(height + tree->Right - tree->Left), around);
			for (const TerrainTrees::Tree* other: around) {
				if (other->ID != tree->ID) {
					worker.Spare.push_back(other->ID);
				}
			}
			return true;
		}
		return false;
	}

	/// Chops at the band through a worker's tree: the trunk pixels in it nearest the worker. @return Whether the trunk is cut through.
	bool ChopBand(Colony::Worker& worker, int most) {
		SLTerrain* terrain = Terrain();
		if (!terrain) {
			return true;
		}
		std::vector<std::pair<int, int>> trunk = PixelsIn(worker.BandLeft - 1, worker.BandTop, worker.BandRight + 1, worker.BandBottom, worker.Unit->GetPos(), [](int, int, int material) { return TerrainTrees::IsTrunk(material); });
		if (trunk.empty()) {
			return true;
		}
		bool through = static_cast<int>(trunk.size()) <= most;
		if (through) {
			// The last of it: what it held up is looked at once it's gone, and falls.
			const TerrainTrees::Tree* tree = TerrainTrees::GetTree(worker.Tree);
			float reach = tree ? static_cast<float>(std::max(tree->Right - tree->Left, tree->Bottom - tree->Top)) + 30.0F : 120.0F;
			TerrainCollapse::BeginChange(Vector(static_cast<float>((worker.BandLeft + worker.BandRight) / 2), static_cast<float>(worker.BandTop)), reach);
		}
		int cut = 0;
		for (const auto& [x, y]: trunk) {
			if (cut >= most) {
				break;
			}
			ClearPixel(terrain, x, y);
			++cut;
		}
		worker.Cut += cut;
		NoteCut(terrain, worker.BandLeft - 1, worker.BandTop, worker.BandRight + 1, worker.BandBottom);
		if (!through) {
			TerrainCollapse::NoteDamage(trunk.front().first, trunk.front().second);
		}
		return through;
	}

	/// Whether a trunk pixel in a felled tree's area is the felled tree's to cut up: not a tree spared (standing there when it fell), nor the
	/// roots of the stump, under the ground.
	bool Felled(const Colony::Worker& worker, int x, int y, int material) {
		if (!TerrainTrees::IsTrunk(material)) {
			return false;
		}
		if (std::abs(x - worker.StumpX) <= 16 && y > worker.StumpY - 1) {
			return false;
		}
		long owner = TerrainTrees::OwnerIDAt(x, y);
		return owner == 0 || std::find(worker.Spare.begin(), worker.Spare.end(), owner) == worker.Spare.end();
	}

	/// The next bit of a felled tree for a worker to cut up, and where to stand to. False once there is none left it can reach.
	bool FindWood(Colony::Worker& worker) {
		TerrainTrees::GetTrees(); // (Seen afresh, for who owns which pixel.)
		Vector feet = worker.Unit->GetPos() + Vector(0.0F, worker.Unit->GetHeight() * 0.25F);
		// A few of the nearest, to find one it can stand by and reach.
		std::vector<std::pair<int, int>> wood = PixelsIn(worker.AreaLeft, worker.AreaTop, worker.AreaRight, worker.AreaBottom, feet, [&worker](int x, int y, int material) { return Felled(worker, x, y, material); });
		int looked = 0;
		for (size_t i = 0; i < wood.size() && looked < 12; i += std::max<size_t>(1, wood.size() / 40), ++looked) {
			const auto& [x, y] = wood[i];
			int side = g_SceneMan.ShortestDistance(Vector(static_cast<float>(x), 0.0F), Vector(feet.m_X, 0.0F), g_SceneMan.SceneWrapsX()).m_X < 0.0F ? -1 : 1;
			int standX = x + side * 9;
			int ground = GroundBelow(standX, y - 6, 120);
			if (ground < 0 || ground - y > static_cast<int>(c_Reach + worker.Unit->GetHeight() * 0.3F)) {
				continue; // Out of reach overhead.
			}
			worker.Aim = Vector(static_cast<float>(x), static_cast<float>(y));
			worker.Spot = Vector(static_cast<float>(standX), static_cast<float>(ground - 1));
			return true;
		}
		return false;
	}

	/// The wood left in a felled tree's area: none of it in reach (a tree hung up in its neighbour's branches, or caught on a ledge, or with
	/// falling terrain off). It is taken down and cut up in one go, its logs left on the ground where the worker stands.
	void TakeDownTheRest(Colony::Worker& worker) {
		SLTerrain* terrain = Terrain();
		if (!terrain) {
			return;
		}
		TerrainTrees::GetTrees();
		int pixels = 0;
		for (const auto& [x, y]: PixelsIn(worker.AreaLeft, worker.AreaTop, worker.AreaRight, worker.AreaBottom, worker.Unit->GetPos(), [&worker](int x, int y, int material) {
			     return Felled(worker, x, y, material) || (TerrainTrees::IsLeaves(material) && TerrainTrees::OwnerIDAt(x, y) == 0);
		     })) {
			if (TerrainTrees::IsTrunk(MaterialAt(x, y))) {
				++pixels;
			}
			ClearPixel(terrain, x, y);
		}
		NoteCut(terrain, worker.AreaLeft, worker.AreaTop, worker.AreaRight, worker.AreaBottom);
		for (int logs = (worker.Cut + pixels) / c_LogPixels; logs > 0; --logs) {
			if (HeldDevice* log = MakeLog()) {
				log->SetPos(worker.Unit->GetPos() + Vector(static_cast<float>(logs % 3 - 1) * 6.0F, -10.0F));
				g_MovableMan.AddItem(log);
			}
		}
		worker.Cut = 0;
	}

	/// Cuts at a worker's bit of wood: trunk pixels round where it aims (gaining it logs), and the leaves round them (gaining nothing).
	/// @return How many trunk pixels it cut.
	int Buck(Colony::Worker& worker, int most) {
		SLTerrain* terrain = Terrain();
		if (!terrain) {
			return 0;
		}
		int aimX = worker.Aim.GetFloorIntX();
		int aimY = worker.Aim.GetFloorIntY();
		int cut = 0;
		for (const auto& [x, y]: PixelsIn(aimX - 7, aimY - 7, aimX + 7, aimY + 7, worker.Aim, [&worker](int x, int y, int material) { return Felled(worker, x, y, material); })) {
			if (cut >= most) {
				break;
			}
			ClearPixel(terrain, x, y);
			++cut;
		}
		for (const auto& [x, y]: PixelsIn(aimX - 12, aimY - 12, aimX + 12, aimY + 12, worker.Aim, [](int x, int y, int material) { return TerrainTrees::IsLeaves(material) && TerrainTrees::OwnerIDAt(x, y) == 0; })) {
			ClearPixel(terrain, x, y);
		}
		NoteCut(terrain, aimX - 12, aimY - 12, aimX + 12, aimY + 12);
		if (cut > 0) {
			TerrainCollapse::NoteDamage(aimX, aimY);
		}
		worker.Cut += cut;
		return cut;
	}

	/// Whether loose pieces of terrain are still coming down in a box.
	bool StillFalling(const Colony::Worker& worker) {
		std::vector<TerrainCollapse::FallingPiece> pieces;
		TerrainCollapse::GetFallingPieces(pieces);
		return std::any_of(pieces.begin(), pieces.end(), [&worker](const TerrainCollapse::FallingPiece& piece) {
			return piece.X + piece.Radius >= static_cast<float>(worker.AreaLeft) && piece.X - piece.Radius <= static_cast<float>(worker.AreaRight) && piece.Y + piece.Radius >= static_cast<float>(worker.AreaTop - 200) && piece.Y - piece.Radius <= static_cast<float>(worker.AreaBottom);
		});
	}

	/// A log lying about within a woodcutter's reach and outside it, nearest a worker, that no other worker is going for. Nothing if there is none.
	MovableObject* LooseLog(const Colony::Building& building, const Colony::Worker& worker) {
		MovableObject* nearest = nullptr;
		float best = building.WorkRadius;
		for (MovableObject* item: g_MovableMan.GetItemList()) {
			if (!item || item->IsSetToDelete() || item->GetPresetName() != Colony::c_Log || InPlot(building, item->GetPos())) {
				continue;
			}
			if (!g_SceneMan.ShortestDistance(building.Ground, item->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(building.WorkRadius)) {
				continue;
			}
			bool taken = false;
			for (const Colony::Building& other: s_Buildings) {
				for (const Colony::Worker& each: other.Staff) {
					taken = taken || (&each != &worker && each.Doing == Colony::Job::ToLog && (each.Aim - item->GetPos()).MagnitudeIsLessThan(10.0F));
				}
			}
			float distance = g_SceneMan.ShortestDistance(worker.Unit->GetPos(), item->GetPos(), g_SceneMan.SceneWrapsX()).GetMagnitude();
			if (!taken && distance < best) {
				best = distance;
				nearest = item;
			}
		}
		return nearest;
	}

	/// Gives a worker something new to do.
	void NextJob(Colony::Building& building, Colony::Worker& worker) {
		Actor* unit = worker.Unit;
		StopCutting(unit);
		worker.JobMS = Now();
		int logs = LogsOf(unit);
		if (logs >= c_CarryLogs) {
			worker.Doing = Colony::Job::Carry;
			return;
		}
		if (worker.HasArea) {
			if (FindWood(worker)) {
				worker.Doing = Colony::Job::ToWood;
				return;
			}
			// All cut up: a sapling where it stood, and on to the next.
			worker.HasArea = false;
			if (s_Regrow) {
				Colony::Sapling sapling;
				sapling.X = worker.StumpX;
				sapling.Y = worker.StumpY;
				sapling.DueMS = Now() + 4000;
				sapling.Variant = static_cast<float>((worker.StumpX * 2654435761u) % 1000u) / 1000.0F;
				sapling.Piece = static_cast<float>((worker.StumpY * 40503u + static_cast<unsigned int>(building.ID) * 977u) % 1000u) / 1000.0F;
				sapling.Mirror = (worker.StumpX & 1) != 0;
				sapling.Right = sapling.Left - 1;
				building.Saplings.push_back(sapling);
			}
			if (logs > 0) {
				worker.Doing = Colony::Job::Carry;
				return;
			}
		}
		if (MovableObject* log = LooseLog(building, worker)) {
			worker.Doing = Colony::Job::ToLog;
			worker.Aim = log->GetPos();
			int ground = GroundBelow(log->GetPos().GetFloorIntX(), log->GetPos().GetFloorIntY() - 4, 120);
			worker.Spot = Vector(log->GetPos().m_X, static_cast<float>(ground < 0 ? log->GetPos().GetFloorIntY() : ground - 1));
			return;
		}
		if (PickTree(building, worker)) {
			worker.Doing = Colony::Job::ToTree;
			return;
		}
		worker.Doing = logs > 0 ? Colony::Job::Carry : Colony::Job::Home;
	}

	/// Runs one of a woodcutter's staff.
	void RunWorker(Colony::Building& building, Colony::Worker& worker, bool look) {
		Actor* unit = worker.Unit;
		long long now = Now();
		auto setStatus = [&worker](const char* status) { worker.Status = status; };
		// Not while someone else has it: a player in control, its own fright, or an order given it.
		if (unit->IsPlayerControlled()) {
			StopCutting(unit);
			return setStatus("Under your control");
		}
		if (unit->NumberValueExists("AIFleeing") && unit->GetNumberValue("AIFleeing") > 0.0) {
			StopCutting(unit);
			return setStatus("Running from danger");
		}
		int mode = unit->GetAIMode();
		bool ownWalk = mode == Actor::AIMODE_GOTO && (unit->GetLastAIWaypoint() - worker.Spot).MagnitudeIsLessThan(24.0F);
		if (mode != Actor::AIMODE_SENTRY && !ownWalk) {
			StopCutting(unit);
			worker.JobMS = now;
			return setStatus("Following orders");
		}
		if (unit->GetOrderAttack()) {
			StopCutting(unit);
			return setStatus("Fighting");
		}
		if (building.Paused || building.Disabled()) {
			StopCutting(unit);
			if (!At(worker, Door(building), 40.0F) && !GoTo(worker, Door(building))) {
				worker.Tries = 0;
			}
			return setStatus(building.Paused ? "Stopped" : "Waiting for a brain");
		}
		switch (worker.Doing) {
			case Colony::Job::Idle:
				if (look) {
					NextJob(building, worker);
				}
				return setStatus("Looking for work");
			case Colony::Job::ToTree: {
				const TerrainTrees::Tree* tree = look ? TerrainTrees::GetTree(worker.Tree) : nullptr;
				if (look && !tree) {
					worker.Doing = Colony::Job::Idle; // (Gone: felled by someone else, burnt or blown up.)
					return;
				}
				Hold(unit, Colony::GetLook(building.What, building.Looks).Tool);
				if (At(worker, worker.Spot)) {
					worker.Doing = Colony::Job::Chop;
					worker.JobMS = now;
					worker.Cut = 0;
					return;
				}
				if (!GoTo(worker, worker.Spot)) {
					Skip(worker, worker.Tree);
					worker.Doing = Colony::Job::Idle;
				}
				return setStatus("Going to fell a tree");
			}
			case Colony::Job::Chop: {
				Hold(unit, Colony::GetLook(building.What, building.Looks).Tool);
				Cut(unit, worker.Aim);
				if (!At(worker, worker.Spot, 24.0F) && !GoTo(worker, worker.Spot)) {
					Skip(worker, worker.Tree);
					worker.Doing = Colony::Job::Idle;
					return;
				}
				if (now - worker.JobMS > 60000) {
					Skip(worker, worker.Tree); // (Not getting anywhere with it.)
					worker.Doing = Colony::Job::Idle;
					return;
				}
				Cutting cutting = CuttingOf(building);
				if (Swinging(worker) && now - worker.HitMS >= cutting.EveryMS) {
					worker.HitMS = now;
					if (ChopBand(worker, cutting.Pixels)) {
						// Timber! It comes down: stand clear while it does.
						StopCutting(unit);
						unit->SayText("Timber!", 1800);
						worker.HasArea = true;
						worker.Tree = 0;
						worker.Doing = Colony::Job::Settle;
						worker.JobMS = now;
						int away = g_SceneMan.ShortestDistance(Vector(static_cast<float>(worker.StumpX), 0.0F), Vector(unit->GetPos().m_X, 0.0F), g_SceneMan.SceneWrapsX()).m_X < 0.0F ? -1 : 1;
						int backX = worker.StumpX + away * 40;
						if (int ground = GroundBelow(backX, worker.StumpY - 30, 80); ground >= 0) {
							Send(worker, Vector(static_cast<float>(backX), static_cast<float>(ground - 1)));
						}
					}
				}
				return setStatus("Chopping a tree down");
			}
			case Colony::Job::Settle:
				StopCutting(unit);
				if (now - worker.JobMS > 2500 && (!StillFalling(worker) || now - worker.JobMS > 15000)) {
					if (now - worker.JobMS > static_cast<long long>(TerrainCollapse::GetTuning().RestSeconds * 1000.0F) + 2500 || !TerrainCollapse::IsEnabled()) {
						NextJob(building, worker);
					}
				}
				return setStatus("Timber!");
			case Colony::Job::ToWood:
				Hold(unit, Colony::GetLook(building.What, building.Looks).Tool);
				if (At(worker, worker.Spot)) {
					worker.Doing = Colony::Job::Buck;
					worker.JobMS = now;
					return;
				}
				if (!GoTo(worker, worker.Spot)) {
					// (Can't get there: whatever is left goes in one go.)
					TakeDownTheRest(worker);
					worker.Doing = Colony::Job::Idle;
				}
				return setStatus("Going to cut up a tree");
			case Colony::Job::Buck: {
				Hold(unit, Colony::GetLook(building.What, building.Looks).Tool);
				Cut(unit, worker.Aim);
				Cutting cutting = CuttingOf(building);
				if (Swinging(worker) && now - worker.HitMS >= cutting.EveryMS) {
					worker.HitMS = now;
					int cut = Buck(worker, cutting.Pixels);
					while (worker.Cut >= c_LogPixels) {
						worker.Cut -= c_LogPixels;
						GiveLog(unit);
					}
					if (cut == 0) {
						if (LogsOf(unit) >= c_CarryLogs) {
							NextJob(building, worker);
						} else if (FindWood(worker)) {
							worker.Doing = Colony::Job::ToWood;
						} else {
							// Done, or the rest out of reach.
							TakeDownTheRest(worker);
							NextJob(building, worker);
						}
					} else if (LogsOf(unit) >= c_CarryLogs) {
						NextJob(building, worker);
					}
				} else if (now - worker.JobMS > 20000 && now - worker.HitMS > 20000) {
					// (Never got to swinging at it: something in the way.)
					TakeDownTheRest(worker);
					NextJob(building, worker);
				}
				return setStatus("Cutting up a tree");
			}
			case Colony::Job::ToLog: {
				StopCutting(unit);
				bool there = false;
				for (MovableObject* item: g_MovableMan.GetItemList()) {
					if (item && !item->IsSetToDelete() && item->GetPresetName() == Colony::c_Log && (item->GetPos() - worker.Aim).MagnitudeIsLessThan(20.0F)) {
						there = true;
						if (g_SceneMan.ShortestDistance(unit->GetPos(), item->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(30.0F)) {
							item->SetToDelete(true);
							GiveLog(unit);
							NextJob(building, worker);
							return;
						}
						worker.Aim = item->GetPos();
						break;
					}
				}
				if (!there || !GoTo(worker, worker.Spot)) {
					NextJob(building, worker);
				}
				return setStatus("Picking up a log");
			}
			case Colony::Job::Carry:
			case Colony::Job::Home: {
				StopCutting(unit);
				bool carrying = LogsOf(unit) > 0;
				if (carrying) {
					Hold(unit, Colony::c_Log);
				}
				if (InPlot(building, unit->GetPos()) && At(worker, Door(building), static_cast<float>(std::max(building.Width() / 2, c_LiteDoorstep)))) {
					if (int logs = TakeLogs(unit); logs > 0) {
						s_Wood[building.Team] += logs;
						building.Wood += logs;
					}
					Hold(unit, Colony::GetLook(building.What, building.Looks).Tool);
					if (look) {
						NextJob(building, worker);
					}
					if (worker.Doing == Colony::Job::Home) {
						return setStatus("Nothing to cut in reach");
					}
					return;
				}
				if (!GoTo(worker, Door(building))) {
					worker.Tries = 0; // (Tries again in a while: it has to get home.)
				}
				return setStatus(carrying ? "Taking logs home" : "Going home");
			}
		}
	}

	/// Grows a woodcutter's saplings a step when they are due.
	void GrowSaplings(Colony::Building& building) {
		long long now = Now();
		SLTerrain* terrain = Terrain();
		if (!terrain) {
			return;
		}
		for (Colony::Sapling& sapling: building.Saplings) {
			if (sapling.DueMS > now) {
				continue;
			}
			// What it was is taken up, so the bigger one is drawn clean.
			if (sapling.Right >= sapling.Left) {
				// (Its own pixels only: not a tree beside it whose branches reach in.)
				TerrainTrees::GetTrees();
				for (int y = sapling.Top; y <= sapling.Bottom; ++y) {
					for (int x = sapling.Left; x <= sapling.Right; ++x) {
						const TerrainTrees::Tree* tree = TerrainTrees::IsTreeMaterial(MaterialAt(x, y)) ? TerrainTrees::FindTreeAt(x, y) : nullptr;
						if (tree && std::abs(tree->BaseX - sapling.X) <= 12) {
							ClearPixel(terrain, x, y);
						}
					}
				}
				NoteCut(terrain, sapling.Left, sapling.Top, sapling.Right, sapling.Bottom);
			}
			int left = 0, top = 0, right = -1, bottom = -1;
			if (sapling.Stage >= 3 || !Sandbox::PlantTree(Vector(static_cast<float>(sapling.X), static_cast<float>(sapling.Y - 4)), c_SaplingScales[sapling.Stage], sapling.Variant, sapling.Piece, sapling.Mirror, left, top, right, bottom)) {
				sapling.Stage = 3;
				continue;
			}
			sapling.Left = left;
			sapling.Top = top;
			sapling.Right = right;
			sapling.Bottom = bottom;
			++sapling.Stage;
			sapling.DueMS = now + c_SaplingStepMS;
		}
		// Grown (and the last picture left standing, as a tree like any other), or gone.
		building.Saplings.erase(std::remove_if(building.Saplings.begin(), building.Saplings.end(), [](const Colony::Sapling& sapling) { return sapling.Stage >= 3; }), building.Saplings.end());
	}

	/// Runs a woodcutter: takes staff on, runs them, grows its saplings and counts the logs left inside it.
	void RunWoodcutter(Colony::Building& building, bool look) {
		long long now = Now();
		const Colony::Look& looks = Colony::GetLook(building.What, building.Looks);
		building.Staff.erase(std::remove_if(building.Staff.begin(), building.Staff.end(), [&building, now](const Colony::Worker& worker) {
			                     if (StillAlive(worker.Unit, worker.UnitID)) {
				                     return false;
			                     }
			                     building.HireMS = std::max(building.HireMS, now + c_HireMS);
			                     return true;
		                     }),
		                     building.Staff.end());
		bool running = !building.Paused && !building.Disabled();
		// (One more for each level it is upgraded.)
		if (running && static_cast<int>(building.Staff.size()) < building.KeepAlive + building.Level - 1 && now >= building.HireMS) {
			// A new one, at the door, a second after the last.
			if (Actor* unit = Sandbox::SpawnWorker(looks.Worker, looks.WorkerModule, building.Team, Door(building) + Vector(0.0F, -20.0F))) {
				Colony::Worker worker;
				worker.Unit = unit;
				worker.UnitID = static_cast<long>(unit->GetUniqueID());
				worker.Spot = unit->GetPos();
				unit->SetNumberValue("ColonyWorker", building.ID);
				unit->SetStringValue("ColonyTool", looks.Tool);
				StopCutting(unit);
				building.Staff.push_back(worker);
				++building.Produced;
			} else {
				building.Status = std::string("Can not make ") + looks.Worker;
				building.Paused = true;
				return;
			}
			building.HireMS = now + 1000;
		}
		for (Colony::Worker& worker: building.Staff) {
			RunWorker(building, worker, look);
		}
		if (look) {
			GrowSaplings(building);
			// Logs left lying inside are counted in, whoever brought them.
			for (MovableObject* item: g_MovableMan.GetItemList()) {
				if (item && !item->IsSetToDelete() && item->GetPresetName() == Colony::c_Log && InPlot(building, item->GetPos())) {
					item->SetToDelete(true);
					++s_Wood[building.Team];
					++building.Wood;
				}
			}
		}
		if (building.Paused) {
			building.Status = "Stopped";
		} else if (building.Disabled()) {
			building.Status = "Disabled: brain destroyed";
		} else if (building.Staff.empty()) {
			building.Status = "Taking on staff";
		} else {
			int working = static_cast<int>(std::count_if(building.Staff.begin(), building.Staff.end(), [](const Colony::Worker& worker) { return worker.Doing != Colony::Job::Home && worker.Doing != Colony::Job::Idle; }));
			building.Status = working > 0 ? "Cutting wood" : "No trees in reach";
		}
	}

	/// Lets a building's staff go: they stand where they are, as ordinary units.
	void LetStaffGo(Colony::Building& building) {
		for (Colony::Worker& worker: building.Staff) {
			if (StillAlive(worker.Unit, worker.UnitID)) {
				StopCutting(worker.Unit);
				worker.Unit->RemoveNumberValue("ColonyWorker");
			}
		}
		building.Staff.clear();
	}
#pragma endregion
} // namespace

int Colony::Building::Width() const { return Lite ? c_LiteWidth : GetType(What).Width; }

int Colony::Building::Height() const { return Lite ? c_LiteHeight : GetType(What).Height; }

const Colony::Type& Colony::GetType(Kind kind) { return c_Types[std::clamp(static_cast<int>(kind), 0, static_cast<int>(Kind::Count) - 1)]; }

const Colony::Look& Colony::GetLook(Kind kind, Style style) { return GetType(kind).Looks[std::clamp(static_cast<int>(style), 0, static_cast<int>(Style::Count) - 1)]; }

const char* Colony::StyleName(Style style) { return c_StyleNames[std::clamp(static_cast<int>(style), 0, static_cast<int>(Style::Count) - 1)]; }

std::vector<Colony::Building>& Colony::Buildings() { return s_Buildings; }

float& Colony::Supply(int team) { return s_Supply[std::clamp(team, 0, 3)]; }

int& Colony::Wood(int team) { return s_Wood[std::clamp(team, 0, 3)]; }

bool& Colony::Free() { return s_Free; }

bool& Colony::NeedsPower() { return s_NeedsPower; }

bool& Colony::Regrow() { return s_Regrow; }

Colony::NoPower& Colony::WithoutPower() { return s_WithoutPower; }

float Colony::PowerRange() { return c_PowerRange; }

float Colony::GeneratorPower() { return c_GeneratorPower; }

float Colony::GeneratorPower(const Building& generator) { return c_GeneratorPower * LevelPace(generator); }

float Colony::ExtractorIncome(const Building& extractor) { return c_ExtractorIncome * LevelPace(extractor); }

float Colony::LevelPace(const Building& building) { return 1.0F + 0.4F * static_cast<float>(std::clamp(building.Level, 1, c_MaxLevel) - 1); }

bool Colony::UpgradeCost(const Building& building, float& supply, int& wood) {
	supply = 0.0F;
	wood = 0;
	if (building.Level >= c_MaxLevel) {
		return false;
	}
	supply = s_Free ? 0.0F : (building.Level == 1 ? 100.0F : 250.0F);
	wood = building.Level == 1 ? 10 : 25;
	return true;
}

std::string Colony::Upgrade(int id) {
	for (Building& building: s_Buildings) {
		if (building.ID != id) {
			continue;
		}
		float supply = 0.0F;
		int wood = 0;
		if (!UpgradeCost(building, supply, wood)) {
			return "It is at its top level";
		}
		if (s_Wood[building.Team] < wood) {
			return "Not enough wood: it needs " + std::to_string(wood);
		}
		if (s_Supply[building.Team] < supply) {
			return "Not enough supply: it needs " + std::to_string(static_cast<int>(supply));
		}
		s_Wood[building.Team] -= wood;
		s_Supply[building.Team] -= supply;
		++building.Level;
		if (building.What == Kind::Woodcutter) {
			building.WorkRadius *= 1.25F; // (Its staff go further for trees.)
		}
		return "";
	}
	return "It is gone";
}

void Colony::TrainNow(int id, int count) {
	for (Building& building: s_Buildings) {
		if (building.ID == id && building.What == Kind::Barracks) {
			building.Queued = std::clamp(building.Queued + count, 0, 99);
		}
	}
}

float Colony::TrainingPower() { return c_TrainingPower; }

float Colony::TrainingSeconds(float cost) { return std::clamp(cost / 40.0F, 4.0F, 30.0F); }

float Colony::SupplyPerMinute(int team) {
	bool any = false;
	float income = 0.0F;
	for (const Building& building: s_Buildings) {
		if (building.Team == team) {
			any = true;
			if (building.What == Kind::Extractor && !building.Paused && !building.Disabled()) {
				income += c_ExtractorIncome * LevelPace(building);
			}
		}
	}
	return any ? (income + c_BaseIncome) * 60.0F : 0.0F;
}

void Colony::Clear() {
	s_Buildings.clear();
	s_Supply.fill(c_StartingSupply);
	s_Wood.fill(0);
	s_NextID = 1;
}

void Colony::Remove(int id) {
	for (Building& building: s_Buildings) {
		if (building.ID == id) {
			LetStaffGo(building);
		}
	}
	s_Buildings.erase(std::remove_if(s_Buildings.begin(), s_Buildings.end(), [id](const Building& building) { return building.ID == id; }), s_Buildings.end());
}

bool Colony::PlaceBrain(int id) {
	for (Building& building: s_Buildings) {
		if (building.ID != id || !building.Disabled()) {
			continue;
		}
		// The chamber cleared again (what was dug or blown into it), and a brain put on its floor.
		Vector room = BrainRoom(building);
		Sandbox::FillBox(Vector(room.m_X, room.m_Y - static_cast<float>(c_BrainRoomHeight)), c_BrainRoomWidth, c_BrainRoomHeight, "");
		if (Actor* brain = Sandbox::SpawnBrain(c_Brain, building.Team, Vector(room.m_X + static_cast<float>(c_BrainRoomWidth / 2), room.m_Y - 9.0F))) {
			GiveBrain(building, brain);
			return true;
		}
	}
	return false;
}

int Colony::Place(Kind kind, const Vector& place, int team, const std::string& unit, int orders, int keepAlive, Style style, bool lite) {
	if (!g_SceneMan.GetScene() || kind == Kind::Count) {
		return -1;
	}
	// Onto the ground: up out of it if the place is inside it, then down to it. (A lite building goes just where it is put, its box's middle on the place.)
	int x = place.GetFloorIntX();
	int y = place.GetFloorIntY() + c_LiteHeight / 2 - 1;
	int sceneHeight = g_SceneMan.GetSceneHeight();
	if (!lite) {
		y = place.GetFloorIntY();
		for (int tries = 0; tries < 600 && !IsAir(x, y); ++tries) {
			--y;
		}
		while (y < sceneHeight - 2 && IsAir(x, y + 1)) {
			++y;
		}
	}
	if (y >= sceneHeight - 2 || y - c_LiteHeight < 0) {
		return -1;
	}
	const Type& type = GetType(kind);
	const Look& look = GetLook(kind, style);
	Building building;
	building.ID = s_NextID++;
	building.What = kind;
	building.Looks = style;
	building.Lite = lite;
	building.Team = std::clamp(team, 0, 3);
	building.Ground = Vector(static_cast<float>(x), static_cast<float>(y + 1));
	building.Unit = unit;
	building.Orders = orders;
	building.KeepAlive = std::clamp(keepAlive, 1, 30);
	building.HireMS = Now() + 500;

	if (lite) {
		// Just its core: a solid box (carved out of whatever is there), its brain's chamber cut in the middle below. Placed by hand, it stays put however it hangs.
		Vector corner(building.Ground.m_X - static_cast<float>(c_LiteWidth / 2), building.Ground.m_Y - static_cast<float>(c_LiteHeight));
		Sandbox::FillBox(corner, c_LiteWidth, c_LiteHeight, "");
		// Fantasy: stone with a timber lid. Sci-fi: a metal crate.
		int lid = style == Style::Fantasy ? 3 : 0;
		Sandbox::FillBox(corner, c_LiteWidth, lid, "Wood");
		Sandbox::FillBox(corner + Vector(0.0F, static_cast<float>(lid)), c_LiteWidth, c_LiteHeight - lid, style == Style::SciFi ? "Metal" : "Stone");
	} else {
		float left = building.Ground.m_X - static_cast<float>(type.Width / 2);
		float top = building.Ground.m_Y - static_cast<float>(type.Height);
		float width = static_cast<float>(type.Width);
		// The plot is cleared, and what is under it made level, so it stands the same on a slope as on the flat.
		Sandbox::FillBox(Vector(left, top), type.Width, type.Height, "");
		const char* footing = style == Style::SciFi ? "Concrete" : "Stone";
		const TerrainObject* preset = dynamic_cast<const TerrainObject*>(g_PresetMan.GetEntityPreset("TerrainObject", look.Building, look.Module));
		if (TerrainObject* piece = preset ? dynamic_cast<TerrainObject*>(preset->Clone()) : nullptr) {
			// The building's floor is the bottom of its picture, so it sits on a footing with its middle over the place.
			Sandbox::FillBox(Vector(left, building.Ground.m_Y), type.Width, 14, footing);
			// Where the ground falls away under the plot (a cliff edge, a dip), the footing goes on down to it, so the building is not left overhanging.
			int groundY = building.Ground.GetFloorIntY() + 14;
			for (int column = 0; column < type.Width; ++column) {
				int columnX = static_cast<int>(left) + column;
				int depth = 0;
				while (depth < 120 && groundY + depth < sceneHeight - 1 && IsAir(columnX, groundY + depth)) {
					++depth;
				}
				if (depth > 0 && depth < 120) {
					Sandbox::FillBox(Vector(static_cast<float>(columnX), static_cast<float>(groundY)), 1, depth, footing);
				}
			}
			piece->SetPos(Vector(left, top) - piece->GetBitmapOffset());
			piece->SetTeam(building.Team);
			g_SceneMan.AddSceneObject(piece);
		} else {
			// Without its module: a block of concrete in its shape.
			Sandbox::FillBox(Vector(left, building.Ground.m_Y), type.Width, 14, "Concrete");
			Sandbox::FillBox(Vector(left, building.Ground.m_Y - 8.0F), type.Width, 8, "Concrete");
			Sandbox::FillBox(Vector(left, top + type.Height / 3), type.Width, 8, "Concrete");
			Sandbox::FillBox(Vector(left, top + type.Height / 3), 8, type.Height - type.Height / 3, "Concrete");
			Sandbox::FillBox(Vector(left + width - 8.0F, top + type.Height / 3), 8, 14, "Concrete");
		}
		// It stands as built, however its roof spans or its walls join: the check clearing the plot started took its picture of what hung in the
		// air before the building was there, and would otherwise take its roof and thin walls for ground just cut loose. Damage later still brings it down.
		TerrainCollapse::NoteBuilt(static_cast<int>(left), static_cast<int>(top), static_cast<int>(left) + type.Width - 1, building.Ground.GetFloorIntY() + 13);
	}
	// Its brain, in a chamber cut at the back on the floor, so it has to be broken into to be reached. (The doorway is at the front, the right.)
	Vector room = BrainRoom(building);
	Sandbox::FillBox(Vector(room.m_X, room.m_Y - static_cast<float>(c_BrainRoomHeight)), c_BrainRoomWidth, c_BrainRoomHeight, "");
	building.SolidAtStart = std::max(CountSolid(building), 1);
	if (Actor* brain = Sandbox::SpawnBrain(c_Brain, building.Team, Vector(room.m_X + static_cast<float>(c_BrainRoomWidth / 2), room.m_Y - 9.0F))) {
		GiveBrain(building, brain);
	}
	s_Buildings.push_back(building);
	return building.ID;
}

void Colony::Update() {
	float seconds = g_TimerMan.GetDeltaTimeSecs();
	long long updates = g_TimerMan.GetSimUpdateCount();
	bool checkWrecks = updates % 30 == 0;
	bool look = updates % 15 == 0; //!< Whether woodcutters look about for work this update, four times a second.
	std::array<float, 4> income{};
	std::array<bool, 4> hasBuilding{};
	for (Building& building: s_Buildings) {
		if (building.Brain && !BrainAlive(building)) {
			building.Brain = nullptr;
		}
		// A disabled building is run again by a brain of its side put in its plot.
		if (building.Disabled() && checkWrecks) {
			if (Actor* brain = BrainInPlot(building)) {
				GiveBrain(building, brain);
			}
		}
	}
	for (Building& building: s_Buildings) {
		if (building.What == Kind::Generator) {
			building.Power = 0.0F; // Given out afresh each update, to the barracks in the order they were built.
		}
		hasBuilding[building.Team] = true;
		if (building.What == Kind::Extractor && !building.Paused && !building.Disabled()) {
			income[building.Team] += c_ExtractorIncome * LevelPace(building);
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
		if (building.What == Kind::Woodcutter) {
			RunWoodcutter(building, look);
			continue;
		}
		if (building.Disabled()) {
			// (A unit part trained stays as far on as it was, and goes on once the building has a brain again.)
			building.Status = "Disabled: brain destroyed";
			if (building.What != Kind::Generator) {
				building.Power = 0.0F;
				building.NoPower = false;
			}
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
		building.Alive.erase(std::remove_if(building.Alive.begin(), building.Alive.end(), [](const std::pair<Actor*, long>& unit) { return !StillAlive(unit.first, unit.second); }), building.Alive.end());
		if (building.Paused) {
			building.Status = "Stopped";
			continue;
		}
		if (building.Unit.empty()) {
			building.Status = "No unit chosen";
			continue;
		}
		if (!building.Paid && static_cast<int>(building.Alive.size()) >= building.KeepAlive && building.Queued <= 0) {
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
			building.ForQueue = static_cast<int>(building.Alive.size()) >= building.KeepAlive;
		}
		building.Status = "Training " + building.Unit + (!building.NoPower ? "" : building.Power > 0.0F ? " on low power" : " slowly: no power");
		float pace = building.NoPower ? std::max(building.Power, c_UnpoweredPace) : 1.0F;
		building.Progress += pace * LevelPace(building) * seconds / TrainingSeconds(cost);
		if (building.Progress >= 1.0F) {
			building.Progress = 0.0F;
			building.Paid = false;
			// Inside, by the doorway.
			Vector door = Door(building) + Vector(0.0F, -32.0F);
			if (Actor* unit = Sandbox::SpawnUnit(building.Unit, building.Team, door, building.Orders)) {
				building.Alive.emplace_back(unit, static_cast<long>(unit->GetUniqueID()));
				++building.Produced;
				if (building.ForQueue) {
					building.Queued = std::max(building.Queued - 1, 0);
					building.ForQueue = false;
				}
				if (building.HasRally) {
					// Off to its rally point, to wait there.
					unit->ClearAIWaypoints();
					unit->AddAISceneWaypoint(building.Rally);
					unit->SetAIMode(Actor::AIMODE_GOTO);
				}
			} else {
				building.Status = "Can not make " + building.Unit;
				building.Paused = true;
			}
		}
	}
	for (Building& generator: s_Buildings) {
		if (generator.What == Kind::Generator && !generator.Disabled()) {
			generator.Status = generator.Paused ? "Stopped" : !s_NeedsPower ? "Idle: buildings run without power" : "Giving " + std::to_string(static_cast<int>(generator.Power + 0.5F)) + " of " + std::to_string(static_cast<int>(GeneratorPower(generator))) + " power";
		}
	}
	for (int id: wrecked) {
		Remove(id);
	}
}
