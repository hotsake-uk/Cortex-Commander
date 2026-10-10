#pragma once

#include <array>
#include <vector>

namespace RTE {
	class Vector;

	/// Trees, as things in the world of their own: each one a trunk of tree trunk material standing in the ground with its leaves (Tree Leaves)
	/// hanging on it, found in the terrain and known by an ID, a place and a size, for anything that works with whole trees (felling one for
	/// wood, say). A tree stays made of terrain pixels, which is what lets it burn pixel by pixel, stand until its trunk burns through and
	/// come down as one piece (TerrainFire, TerrainCollapse); this knows which pixels are which tree.
	/// Any material whose name has "Tree" in it counts as a tree's (the base game's Tree Trunk and Tree Leaves, a mod's Palm Tree Trunk), so
	/// mods can add trees of their own.
	/// Units and vehicles go through trees as if they weren't there unless "Units and vehicles bump into trees" is on (a gameplay setting,
	/// saved): bullets, fire, liquids, falling ground and loose objects meet them as before.
	class TerrainTrees {

	public:
		/// One tree, as last found in the terrain.
		struct Tree {
			long ID = 0; //!< Its own number, kept from one look to the next while it stands where it stood (a burning tree keeps it as it burns).
			int Left = 0, Top = 0, Right = 0, Bottom = 0; //!< The box round all its pixels, in scene coordinates, inclusive.
			int BaseX = 0, BaseY = 0; //!< The middle of the bottom of its trunk: where it stands in the ground.
			int TrunkPixels = 0; //!< How much trunk it has: the wood that felling it would give.
			int LeafPixels = 0; //!< How many leaves hang on it.
			int TrunkMaterial = 0; //!< What its trunk is made of (the most of it).
		};

		/// Gets whether units and vehicles bump into trees (a gameplay setting). Off (as the game comes) they walk and drive through them.
		static bool UnitsCollide() { return s_UnitsCollide; }

		/// Sets whether units and vehicles bump into trees.
		static void SetUnitsCollide(bool collide);

		/// Gets whether a terrain material is a tree's: its trunk or its leaves. Cheap; safe from any thread.
		static bool IsTreeMaterial(int materialID) { return s_Tree[static_cast<unsigned char>(materialID)]; }

		/// Gets whether a terrain material is a tree's trunk. Cheap; safe from any thread.
		static bool IsTrunk(int materialID) { return s_Trunk[static_cast<unsigned char>(materialID)]; }

		/// Gets whether a terrain material is a tree's leaves. Cheap; safe from any thread.
		static bool IsLeaves(int materialID) { return s_Tree[static_cast<unsigned char>(materialID)] && !s_Trunk[static_cast<unsigned char>(materialID)]; }

		/// Gets whether units' and vehicles' bodies go through a terrain material as if it were air: a tree's, while they don't bump into trees.
		/// Called for every bit of a unit's body that touches the ground, so it is a table lookup. Safe from any thread.
		static bool ActorsPass(int materialID) { return s_ActorsPass[static_cast<unsigned char>(materialID)]; }

		/// Sees which materials are trees', for the current scene's materials. Called when a scene starts; call again if materials are added.
		static void BuildTables();

		/// Gets the trees standing in the scene now. Looks the terrain over again when it may have changed (a tree planted or felled, or a
		/// second gone since the last look, for trees burning and falling). Call from the main thread.
		static const std::vector<Tree>& GetTrees();

		/// Gets the tree a pixel belongs to, or none. Call from the main thread.
		/// @param x The pixel, in scene coordinates.
		/// @param y The pixel, in scene coordinates.
		static const Tree* FindTreeAt(int x, int y);

		/// Gets the trees with any part within a distance of a point, nearest first. Call from the main thread.
		/// @param centre The point, in scene coordinates.
		/// @param radius How far from it, in pixels.
		/// @param found Filled with them.
		static void FindTreesNear(const Vector& centre, float radius, std::vector<const Tree*>& found);

		/// Gets a tree by its ID, or none if it's gone. Call from the main thread.
		static const Tree* GetTree(long id);

		/// Tells it the terrain where trees are has changed (a tree planted, painted or cut), so the next ask looks again. Call from the main thread.
		static void NoteChanged() { s_Stale = true; }

		/// Forgets the trees found, e.g. when the scene changes. Call from the main thread.
		static void Clear();

	private:
		static bool s_UnitsCollide; //!< Whether units and vehicles bump into trees.
		static std::array<bool, 256> s_Tree; //!< The materials that are trees'.
		static std::array<bool, 256> s_Trunk; //!< The materials that are trees' trunks.
		static std::array<bool, 256> s_ActorsPass; //!< The materials units' and vehicles' bodies go through as air: s_Tree while they don't collide.
		static bool s_Stale; //!< Whether the terrain may have changed since the trees were last found.

		/// Finds the trees in the terrain again, keeping the IDs of those that still stand where they stood.
		static void Survey();
	};
} // namespace RTE
