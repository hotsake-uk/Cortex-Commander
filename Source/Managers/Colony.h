#pragma once

#include "Vector.h"

#include <string>
#include <utility>
#include <vector>

namespace RTE {
	class Actor;

	/// The beginnings of colony management: buildings that stand in the world, belong to a side and do something for it over time.
	/// So far a barracks trains units and an extractor earns supply. Each side has a stock of supply that training spends (unless everything is free).
	/// A building is made of real terrain, so it can be shot, dug and blown apart; with too much of it gone it stops being a building.
	/// This holds the buildings and runs them; the sandbox (Sandbox.cpp) places them and draws their controls.
	class Colony {

	public:
		enum class Kind {
			Barracks,
			Extractor,
			Count
		};

		/// What every building of a kind has in common.
		struct Type {
			const char* Name;
			const char* Description;
			int Width; //!< The plot it stands on, in pixels.
			int Height;
		};

		/// One building in the world.
		struct Building {
			int ID = 0;
			Kind What = Kind::Barracks;
			int Team = 0;
			Vector Ground; //!< The middle of its plot, at ground level.
			std::string Unit; //!< Barracks: the unit it trains.
			int Orders = 1; //!< Barracks: the orders its units get (the sandbox's: 0 hold, 1 attack, 2 hunt brains, 3 patrol, 4 rally, 5 nothing).
			int KeepAlive = 4; //!< Barracks: it trains until this many of its units are alive, and again as they fall.
			bool Paused = false;
			bool Paid = false; //!< Barracks: the unit in training has been paid for.
			float Progress = 0.0F; //!< Barracks: how far the unit in training is, 0 to 1.
			int Produced = 0; //!< Units trained so far.
			int SolidAtStart = 0; //!< How much of it there was when built, to tell when it is wrecked.
			std::vector<std::pair<Actor*, long>> Alive; //!< Barracks: its units still alive, with their unique IDs.
			std::string Status; //!< What it is doing, for showing.
		};

		/// Gets what buildings of a kind have in common.
		static const Type& GetType(Kind kind);

		/// Builds a building on the ground under a place.
		/// @param kind What to build. @param place Where: it is put on the ground below. @param team The side it belongs to.
		/// @param unit Barracks: the unit to train. @param orders Barracks: the orders its units get. @param keepAlive Barracks: how many of its units it keeps alive.
		/// @return The ID of the building, or -1 if it could not be built.
		static int Place(Kind kind, const Vector& place, int team, const std::string& unit, int orders, int keepAlive);

		/// Takes a building out of the list. What it was built of stays standing.
		static void Remove(int id);

		/// Forgets every building and puts the supplies back to what a game starts with.
		static void Clear();

		/// Gets the buildings, to show and change.
		static std::vector<Building>& Buildings();

		/// Gets the supply of a side, to show and change.
		static float& Supply(int team);

		/// Gets whether training costs nothing, to show and change. It does by default.
		static bool& Free();

		/// Gets how many seconds a unit that costs this much takes to train.
		static float TrainingSeconds(float cost);

		/// Runs the buildings. Call once per sim update while a game is going.
		static void Update();
	};
} // namespace RTE
