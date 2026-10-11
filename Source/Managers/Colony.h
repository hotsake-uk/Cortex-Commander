#pragma once

#include "Vector.h"

#include <string>
#include <utility>
#include <vector>

namespace RTE {
	class Actor;

	/// The beginnings of colony management: buildings that stand in the world, belong to a side and do something for it over time.
	/// A barracks trains units, an extractor earns supply, a generator powers its side's buildings, and a woodcutter keeps staff of its own who
	/// fell the trees round it, cut them up into logs and carry them home, where they are the side's wood.
	/// Each side has a stock of supply that training spends (unless everything is free), and a stock of wood its woodcutters bring in.
	/// With power turned on, a barracks also needs power while it trains, from its side's generators in range; short of it, it stops (or slows, as set).
	/// A building is made of real terrain, so it can be shot, dug, burnt and blown apart; with too much of it gone it stops being a building.
	/// Each kind comes in two styles: Fantasy, one of the medieval buildings (Medieval.rte/Scenes/Buildings), and Sci-fi, one of the colony
	/// buildings in Base.rte (Base.rte/Scenes/Objects/Colony, drawn by Tools/MakeColonySprites.py), stamped on a levelled footing; without that
	/// module it is a concrete block. Both styles of a kind stand on the same plot, so they work the same.
	/// Each building is run by a brain in a chamber at its back. Kill the brain and the building is disabled: it stands, but does nothing until
	/// another brain of its side is put in its plot (or one is put back in its chamber, PlaceBrain). These brains only run buildings: they are
	/// never a player's brain, so losing one does not end the game.
	/// This holds the buildings and runs them; the sandbox (Sandbox.cpp) places them and draws their controls. Woodcutters' staff are steered from
	/// here (where to go and what to cut, set on them as number values) and swing their tools from their AI (Base.rte/AI/WorkerBehaviors.lua).
	class Colony {

	public:
		enum class Kind {
			Barracks,
			Extractor,
			Generator,
			Woodcutter,
			Count
		};

		/// How a building looks.
		enum class Style {
			Fantasy, //!< A medieval building of timber, thatch and stone.
			SciFi, //!< A prefab of metal, concrete and glass.
			Count
		};

		/// What one style of a kind is built as.
		struct Look {
			const char* Building; //!< The TerrainObject it is built as.
			const char* Module; //!< The module that has it.
			const char* Worker; //!< Woodcutter: the unit its staff are.
			const char* WorkerModule;
			const char* Tool; //!< Woodcutter: what they cut with.
		};

		/// What every building of a kind has in common.
		struct Type {
			const char* Name;
			const char* Description;
			int Width; //!< The plot it stands on, in pixels: the size of its building.
			int Height;
			Look Looks[static_cast<int>(Style::Count)];
		};

		/// What a barracks does without enough power to train.
		enum class NoPower {
			Stops, //!< Short of all it needs, it waits, showing "No power".
			Slows //!< It trains at the share of its power it gets, and at a quarter pace with none.
		};

		/// What one of a woodcutter's staff is about.
		enum class Job {
			Idle, //!< Looking for something to do.
			ToTree, //!< Walking to a tree to fell.
			Chop, //!< Chopping through its trunk, at knee height.
			Settle, //!< Stood back while it comes down.
			ToWood, //!< Walking to the next bit of a felled tree.
			Buck, //!< Cutting it up into logs.
			ToLog, //!< Walking to a log lying about, to pick it up.
			Carry, //!< Taking logs home.
			Home //!< Nothing to do: going back to the hut.
		};

		/// One of a woodcutter's staff.
		struct Worker {
			Actor* Unit = nullptr;
			long UnitID = -1;
			Job Doing = Job::Idle;
			long Tree = 0; //!< The tree it is felling (TerrainTrees ID), claimed so no one else fells it too.
			Vector Spot; //!< Where it is going, or standing to work.
			Vector Aim; //!< What it cuts.
			int BandLeft = 0, BandTop = 0, BandRight = 0, BandBottom = 0; //!< Chop: the cut through the trunk, scene pixels, inclusive.
			bool HasArea = false; //!< Whether a tree it felled is still to be cut up.
			int AreaLeft = 0, AreaTop = 0, AreaRight = 0, AreaBottom = 0; //!< Where that tree came down, scene pixels, inclusive.
			std::vector<long> Spare; //!< The standing trees in that area when it fell, which are not to be cut up with it.
			int StumpX = 0, StumpY = 0; //!< Where the tree stood: the ground at its foot, for a sapling.
			int Cut = 0; //!< Trunk pixels cut since its last log.
			long long JobMS = 0; //!< When it started what it is doing, sim ms.
			long long HitMS = 0; //!< When it last cut, sim ms.
			long long MoveMS = 0; //!< When it was last sent somewhere, sim ms.
			unsigned int Serial = 0; //!< Its order serial just after it was last sent somewhere, to tell its own walk from someone else's order.
			int Tries = 0; //!< How many times it has been sent to the same place.
			std::vector<std::pair<long, long long>> Skip; //!< Trees it couldn't get to or fell, and until when (sim ms) it leaves them be.
			std::string Status; //!< What it is doing, for showing.
		};

		/// A young tree a woodcutter planted where one was felled, growing up in steps.
		struct Sapling {
			int X = 0, Y = 0; //!< Its foot, on the ground.
			int Stage = 0; //!< How many times it has grown: 0 not yet planted, 3 grown (then it is a tree like any other).
			long long DueMS = 0; //!< When it next grows, sim ms.
			float Variant = 0.0F, Piece = 0.0F; //!< Which tree it is (the same one at every size).
			bool Mirror = false;
			int Left = 0, Top = 0, Right = 0, Bottom = 0; //!< Its last picture, to take up when it grows. Right < Left for none.
		};

		/// One building in the world.
		struct Building {
			int ID = 0;
			Kind What = Kind::Barracks;
			Style Looks = Style::Fantasy;
			bool Lite = false; //!< Just its core: a small box with its brain in, for a building of your own round it. It does all the full building does.
			int Team = 0;
			Vector Ground; //!< The middle of its plot, at ground level.
			std::string Unit; //!< Barracks: the unit it trains.
			int Orders = 1; //!< Barracks: the orders its units get (the sandbox's: 0 hold, 1 attack, 2 hunt brains, 3 patrol, 4 rally, 5 nothing).
			int KeepAlive = 4; //!< Barracks: it trains until this many of its units are alive, and again as they fall. Woodcutter: how many staff it keeps.
			bool Paused = false;
			bool Paid = false; //!< Barracks: the unit in training has been paid for.
			float Progress = 0.0F; //!< Barracks: how far the unit in training is, 0 to 1.
			int Produced = 0; //!< Units trained so far (woodcutter: staff taken on).
			int SolidAtStart = 0; //!< How much of it there was when built, to tell when it is wrecked.
			std::vector<std::pair<Actor*, long>> Alive; //!< Barracks: its units still alive, with their unique IDs.
			float Power = 1.0F; //!< Barracks: the share of the power it needs that it got this update, 0 to 1. Generator: how much of its power is drawn.
			bool NoPower = false; //!< Barracks: it wants to train and has not the power to.
			Actor* Brain = nullptr; //!< The brain that runs it, or nothing once that brain is dead.
			long BrainID = -1; //!< The unique ID of its brain, or -1 if it was never given one (and so runs without).
			std::vector<Worker> Staff; //!< Woodcutter: its staff alive.
			long long HireMS = 0; //!< Woodcutter: when it may next take someone on, sim ms.
			int Wood = 0; //!< Woodcutter: the logs brought in to it, in all.
			float WorkRadius = 800.0F; //!< Woodcutter: how far from it its staff go for trees, in pixels.
			std::vector<Sapling> Saplings; //!< Woodcutter: the trees its staff planted, still growing.
			std::string Status; //!< What it is doing, for showing.

			/// Whether it has lost its brain, and does nothing until it gets another.
			bool Disabled() const { return Brain == nullptr && BrainID != -1; }

			/// The plot it stands on, in pixels: its kind's, or the lite box's.
			int Width() const;
			int Height() const;
		};

		/// The group every building's brain is put in, so the game never takes it for a player's brain.
		static constexpr const char* c_BrainGroup = "Building Brains";

		/// The size of a lite building's box, in pixels: walls round its brain's chamber.
		static constexpr int c_LiteWidth = 44;
		static constexpr int c_LiteHeight = 32;

		/// The item a log is (Base.rte), cut from a tree and carried home as wood.
		static constexpr const char* c_Log = "Log";

		/// Gets what buildings of a kind have in common.
		static const Type& GetType(Kind kind);

		/// Gets what one style of a kind is built as.
		static const Look& GetLook(Kind kind, Style style);

		/// Gets the name of a style, for showing.
		static const char* StyleName(Style style);

		/// Builds a building on the ground under a place.
		/// @param kind What to build. @param place Where: it is put on the ground below. @param team The side it belongs to.
		/// @param unit Barracks: the unit to train. @param orders Barracks: the orders its units get. @param keepAlive Barracks: how many of its units it keeps alive; woodcutter: how many staff.
		/// @param style How it looks.
		/// @param lite Whether it is just its core, a small box with its brain in, put exactly at the place (in the air, or carved into the ground), not on the ground below.
		/// @return The ID of the building, or -1 if it could not be built.
		static int Place(Kind kind, const Vector& place, int team, const std::string& unit, int orders, int keepAlive, Style style = Style::Fantasy, bool lite = false);

		/// Takes a building out of the list. What it was built of stays standing, and so do its staff (who stop working).
		static void Remove(int id);

		/// Puts a new brain of its side in a disabled building's chamber, so it runs again.
		/// @return Whether a brain was put in.
		static bool PlaceBrain(int id);

		/// Forgets every building and puts the supplies and wood back to what a game starts with.
		static void Clear();

		/// Gets the buildings, to show and change.
		static std::vector<Building>& Buildings();

		/// Gets the supply of a side, to show and change.
		static float& Supply(int team);

		/// Gets the wood of a side, in logs, to show and change.
		static int& Wood(int team);

		/// Gets whether training costs nothing, to show and change. It does by default.
		static bool& Free();

		/// Gets whether buildings need power, to show and change. They do not by default.
		static bool& NeedsPower();

		/// Gets what a barracks does without power, to show and change.
		static NoPower& WithoutPower();

		/// Gets whether woodcutters plant a sapling where each tree they fell stood, to show and change. They do by default.
		static bool& Regrow();

		/// Gets how far from a generator, in pixels, a building takes power from it.
		static float PowerRange();

		/// Gets how much power a generator gives.
		static float GeneratorPower();

		/// Gets how much power a barracks draws while it trains.
		static float TrainingPower();

		/// Gets how many seconds a unit that costs this much takes to train.
		static float TrainingSeconds(float cost);

		/// Gets how much supply a side earns a minute, as it stands.
		static float SupplyPerMinute(int team);

		/// Runs the buildings. Call once per sim update while a game is going, from the main thread.
		static void Update();
	};
} // namespace RTE
