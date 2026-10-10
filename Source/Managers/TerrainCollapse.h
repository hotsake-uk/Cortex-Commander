#pragma once

#include <vector>

namespace RTE {
	class Vector;

	/// Terrain left floating after explosions breaks loose: a piece no longer touching any other ground becomes a rigid body
	/// that falls, tips, rolls and slides to rest, cracks into smaller pieces if it lands hard, hits units in its way and pushes liquid aside.
	/// Pieces of buildings fall too once nothing holds them; doors and small fittings stay.
	/// While it moves a piece is drawn into the terrain each update, so everything treats it as ground.
	/// Part of the simulation and deterministic: fixed sim steps, sorted order, its own seeded random numbers.
	class TerrainCollapse {

	public:
		/// The numbers that decide what falls and how, for tuning (World Debug, F6) and saved with the settings.
		struct Tuning {
			bool FloatingStays = true; //!< A mass that was already hanging in the air before a blast stays up when it's chipped; if it's cut in two, the bigger part stays and the smaller falls. Off, anything touching nothing falls.
			int NeckWidth = 3; //!< A piece held on by a neck of ground no wider than this many pixels snaps off. 0 turns this off.
			int MaxPiecePixels = 30000; //!< A connected piece bigger than this counts as the world itself and never falls.
			int MinFittingPixels = 150; //!< Loose bits of a building smaller than this stay where they are (lamps, signs and consoles are drawn hanging in mid-air).
			float BreakStrength = 1.0F; //!< How hard a landing pieces take before cracking: 2 is twice as tough, 0.5 half.
			/// How fast in m/s a piece has to land to break, by its materials' BreakStyle (each material's ImpactStrength scales its style's).
			/// A piece of several materials goes by the ones that carry it: leaves, grass and ash don't count. Pieces move at most 27 m/s, so above that never.
			float ShatterSpeed = 7.0F; //!< Concrete, glass, ice.
			float CrackSpeed = 9.0F; //!< Earth, stone and anything not listed.
			float CrumbleSpeed = 2.0F; //!< Sand, snow, gravel, rubble, ash.
			float SplinterSpeed = 22.0F; //!< Wood and tree trunks: a tree lands whole unless it comes down very hard.
			float BendSpeed = 30.0F; //!< Metal: by default it never breaks from a landing.
			float BlastPush = 0.5F; //!< How hard explosions throw loose pieces: 0 not at all, 1 hard, 3 very hard. Lower also means fewer pieces lying at rest are picked up again.
			int CrushPixels = 24; //!< A falling piece goes through loose bits of ground of up to this many pixels (leftover scraps, nuggets, a few grains) and flattens them, instead of being held up by them. Never more than a quarter of its own size. 0 turns this off.
			float ScuffStrength = 1.0F; //!< How much loose ground (sand and the like, per its Scuffs material property) is knocked loose and shoved along by units walking or running on it: 1 a few pixels a step, 2 more, 0 none.
			float RestSeconds = 2.5F; //!< How long a piece lies still before it becomes ordinary ground again.
			float HitDamage = 1.0F; //!< How much a falling piece hurts the units it hits: at 1 a block a metre across (about 100 kg) falling 10 m/s onto a soldier takes about a quarter to a third of their health, at 2 twice that. 0 turns hit damage off (pieces still knock units about).
			float HitMinSpeed = 4.0F; //!< How fast in m/s a piece has to be moving into a unit to hurt it at all. Only the speed above this counts.
			int HitMinPixels = 12; //!< Pieces smaller than this many pixels never hurt (gravel, a few grains).
			float HitMassCap = 3.0F; //!< A piece heavier than this many times the unit it hits counts as only this heavy, so a boulder doesn't hurt endlessly more than a big rock.
			float HitKnockback = 1.0F; //!< How hard pieces shove the units and loose objects they hit: 0 not at all, 1 as before, 2 twice as hard.
		};

		/// Gets the tuning numbers, to read or change.
		static Tuning& GetTuning() { return s_Tuning; }

		/// Tells the system of an explosion, so it can throw the loose pieces near it: ones still moving, and ones that came to rest in the last minute, which are picked up again. Thread safe.
		/// @param position The middle of the blast, in scene coordinates.
		/// @param reach How far it's felt, in pixels.
		/// @param energy How big it is (a gibbing object's gib energy).
		static void Blast(const Vector& position, float reach, float energy);

		/// Tells the system a pixel of terrain has just been knocked out by something other than an explosion: a digger, bullets, anything that wears ground away bit by bit.
		/// Where that goes on, the ground around is watched, and what gets cut loose falls. Thread safe.
		/// @param x The pixel, in scene coordinates.
		/// @param y The pixel, in scene coordinates.
		static void NoteDamage(int x, int y);

		/// Tells the system a unit's foot came down at a point, so loose ground under it (a material with Scuffs) is knocked loose and shoved the way the unit travels. Thread safe.
		/// @param x The foot, in scene coordinates.
		/// @param y The foot, in scene coordinates.
		/// @param direction Which way the unit is going: -1 left, 1 right.
		/// @param speed How fast it is going, in pixels per update.
		static void NoteFootfall(int x, int y, int direction, float speed);

		/// Tells the system that terrain around a point is about to be removed by something other than an explosion (a digging tool), then checks for loose pieces afterwards.
		/// Call it before the terrain changes, from the main thread: it notes what was already hanging in the air there, so only what the change cuts loose falls.
		/// @param position Centre of the change, in scene coordinates.
		/// @param radius How far around it to look.
		static void BeginChange(const Vector& position, float radius);

		/// Gets whether collapsing terrain is on (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether collapsing terrain is on.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Gets whether pieces of buildings (concrete, metal and the like) fall once nothing holds them. Off, only natural ground falls (a gameplay setting).
		static bool BuildingsFall() { return s_BuildingsFall; }

		/// Sets whether pieces of buildings fall.
		static void SetBuildingsFall(bool fall) { s_BuildingsFall = fall; }

		/// Queues a check for floating terrain around a crater. Thread safe.
		/// @param position Centre of the crater, in scene coordinates.
		/// @param radius How far around it to look.
		static void QueueCheck(const Vector& position, float radius);

		/// Makes a boulder of a material in the air at a point, which falls like any loose piece. Thread safe; applied on the next sim step.
		/// @param position Its centre, in scene coordinates.
		/// @param radius Its rough radius in pixels (3 to 60).
		/// @param materialName The material's name, e.g. "Stone".
		static void SpawnChunk(const Vector& position, float radius, const char* materialName);

		/// Runs due checks. Call once per sim update, from the main thread.
		static void Update();

		/// Forgets pending checks, e.g. when the scene changes.
		static void Clear();

		/// Gets how many pixels have collapsed in this scene, for statistics.
		static int GetCollapsedCount();

		/// Gets how many loose pieces are moving right now, for statistics.
		static int GetFallingCount();

		/// One loose piece, for the world simulation overlay.
		struct FallingPiece {
			float X, Y; //!< Its centre of mass, in the scene.
			float Radius; //!< Its furthest pixel from that.
			float VelX, VelY; //!< Pixels per update.
		};

		/// Gets the loose pieces moving right now, for the world simulation overlay. Call from the main thread, between sim updates.
		/// @param pieces Filled with them.
		static void GetFallingPieces(std::vector<FallingPiece>& pieces);

	private:
		static bool s_Enabled; //!< Whether collapsing terrain is on.
		static bool s_BuildingsFall; //!< Whether pieces of buildings fall too.
		static Tuning s_Tuning; //!< What falls and how.
	};
} // namespace RTE
