#pragma once

#include <vector>

namespace RTE {
	class Vector;
	class SLTerrain;

	/// Grazing: animals eating the plants that grow in the terrain, and the plants growing back. The AI finds food and water here (AnimalBehaviors.lua),
	/// and a bite takes the plant pixels in front of its mouth out of the terrain; each comes back as it was, in the same place and colour, a few minutes
	/// later, once there is ground (or plant) under it and air where it was.
	/// What an animal eats is its diet, a set of the Diet bits. Grass is any material named with "Grass" in it (the maps' topsoil frosting, the sandbox's
	/// Grow grass), bushes are Vegetation (the plant brush's), tough plants ToughVegetation. A tree's leaves are never food.
	class TerrainGrazing {

	public:
		/// What an animal eats, as bits.
		enum Diet {
			Grass = 1, //!< Grass on the ground.
			Bushes = 2, //!< Plants and bushes (Vegetation).
			Tough = 4 //!< Tough plants (ToughVegetation).
		};

		/// Sees which materials are food, for the current scene. Called when a scene starts.
		static void BuildTables();

		/// Forgets the plants waiting to grow back. Called when a scene starts.
		static void Clear();

		/// Grows back the plants whose time has come. Call once per sim update.
		static void Update();

		/// Gets the minutes an eaten plant takes to grow back, on average (each takes from 0.6 to 1.6 of this).
		static float GetRegrowMinutes() { return s_RegrowMinutes; }

		/// Sets the minutes an eaten plant takes to grow back, on average. 0 or less: never.
		static void SetRegrowMinutes(float minutes) { s_RegrowMinutes = minutes; }

		/// Gets whether a material is food for a diet.
		static bool IsFood(int materialID, int diet);

		/// Finds food on the ground near a point: plant pixels of the diet with air over them, the nearest first, each at least 12 px from the others.
		/// @param from Where to look from.
		/// @param radius How far to look either way; up and down, half as far.
		/// @param diet What it eats (Diet bits).
		/// @param most How many places to give back at most.
		/// @return The places, nearest first: each the top food pixel of its patch. Owned by the caller.
		static std::vector<Vector>* FindFood(const Vector& from, float radius, int diet, int most);

		/// Finds water near a point an animal could drink from: the ground beside a liquid's surface, nearest first, each at least 24 px from the others.
		/// @param from Where to look from.
		/// @param radius How far to look either way; up and down, half as far.
		/// @param most How many places to give back at most.
		/// @return The places, two to each: a ground pixel with air over it, then the surface of the water beside it. Owned by the caller.
		static std::vector<Vector>* FindWater(const Vector& from, float radius, int most);

		/// Takes a bite: the food pixels of a diet within a circle, from the top down, up to so many, taken out of the terrain to grow back later.
		/// Safe from the AI's threads: the pixels are taken at the next Update, on the main thread.
		/// @param at The middle of the bite.
		/// @param radius How far round it.
		/// @param diet What it eats (Diet bits).
		/// @param most How many pixels at most.
		/// @return How many food pixels there are to take (up to most): 0 when there is nothing left there.
		static int Eat(const Vector& at, float radius, int diet, int most);

		/// Gets how many eaten plant pixels are waiting to grow back.
		static int GetWaitingCount();

	private:
		static float s_RegrowMinutes; //!< The minutes an eaten plant takes to grow back, on average.

		/// Takes a bite out of the terrain, on the main thread (Update): the bites the AI asked for with Eat.
		static void TakeBite(SLTerrain* terrain, const Vector& at, float radius, int diet, int most);
	};
} // namespace RTE
