#pragma once

#include <functional>
#include <string>
#include <vector>

namespace RTE {
	class Vector;
	class MovableObject;

	/// Flowing liquids in the terrain: water, lava, acid and oil pixels fall, spread sideways and pool.
	/// Water puts out fire; lava sets things alight, glows, hurts and turns to stone where it meets water; acid eats through soft terrain; oil burns.
	/// Liquids at rest cost nothing: only pixels that moved recently, or whose surroundings changed, are simulated.
	/// Part of the simulation and deterministic: fixed sim steps, sorted order, its own seeded random numbers.
	class FluidSim {

	public:
		/// Gets whether flowing liquids are on (a gameplay setting).
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether flowing liquids are on.
		static void SetEnabled(bool enabled) { s_Enabled = enabled; }

		/// Gets whether loose powders (sand, snow, rubble, ash) slide and pile when disturbed (a gameplay setting).
		static bool PowdersEnabled() { return s_Powders; }

		/// Sets whether loose powders slide and pile.
		static void SetPowdersEnabled(bool enabled);

		/// Gets whether blood that settles on the ground runs and pools as a liquid of its own (Blood), rather than staying where it fell (a gameplay setting, off unless turned on).
		static bool BloodFlows() { return s_BloodFlows; }

		/// Sets whether settled blood runs and pools.
		static void SetBloodFlows(bool enabled);

		/// Gets whether still water freezes over in snowy weather (a gameplay setting, off unless turned on).
		static bool FreezingEnabled() { return s_Freezing; }

		/// Sets whether still water freezes over in snowy weather.
		static void SetFreezingEnabled(bool enabled) { s_Freezing = enabled; }

		/// Gets whether liquid that reaches the bottom of the map runs out of it and is gone, rather than pooling on the bottom row (a gameplay setting, off unless turned on).
		static bool DrainsBottom() { return s_DrainBottom; }

		/// Sets whether liquid drains out of the bottom of the map.
		static void SetDrainsBottom(bool enabled) { s_DrainBottom = enabled; }

		/// Gets whether liquid that reaches the left or right edge of the map runs out of it and is gone, rather than banking up against it (a gameplay
		/// setting, off unless turned on). A map that wraps sideways has no side edges, so this does nothing there.
		static bool DrainsSides() { return s_DrainSides; }

		/// Sets whether liquid drains out of the sides of the map.
		static void SetDrainsSides(bool enabled) { s_DrainSides = enabled; }

		/// Gets whether loose ground (sand, snow, gravel: the powders) that reaches the bottom or a side of the map falls out of it and is gone,
		/// rather than piling there (a gameplay setting, off unless turned on). As for liquids, a map that wraps sideways has no side edges.
		static bool PowdersFallOut() { return s_PowdersFallOut; }

		/// Sets whether loose ground falls out of the map.
		static void SetPowdersFallOut(bool enabled) { s_PowdersFallOut = enabled; }

		/// Gets whether a material is one of the flowing liquids. Powders aren't.
		static bool IsLiquid(int materialID);

		/// Gets whether liquids flow through a material as if it weren't there (grass, foliage: MaterialBehaviour::LiquidsPassThrough).
		static bool LetsLiquidsThrough(int materialID);

		/// Gets whether a flowing liquid holds up and drags at bodies in it (ActorWater): every flowing liquid, oil and lava included (L-5); what
		/// each does to a body is its material's (weight, stickiness, touch damage).
		static bool HoldsBodies(int materialID);

		/// Gets how many pixels of a liquid a look sees through (MaterialBehaviour::SightDepth), 0 for what isn't a liquid or can't be seen through.
		static int SightDepth(int materialID);

		/// Gets how many pixels of a liquid a shot goes on through before it is spent (MaterialBehaviour::ShotDepth), 0 for what isn't a liquid.
		static int ShotDepth(int materialID);

		/// Gets what a shot's speed is multiplied by for each pixel of a liquid it goes through: down to half over its ShotDepth.
		static float ShotDrag(int materialID);

		/// Fills air in a circle with a liquid. Thread safe; applied on the next sim step.
		/// @param position Centre, in scene coordinates.
		/// @param radius Radius in pixels.
		/// @param liquidName "Water", "Lava", "Acid" or "Oil", or a powder: "Sand", "Snow", "Earth Rubble" or "Ashes".
		static void Pour(const Vector& position, float radius, const char* liquidName);

		/// Wakes liquid around a disturbance (explosion, collapse) so it starts flowing again. Thread safe.
		static void Disturb(const Vector& position, float radius);

		/// Keeps the liquid at a pixel that something is about to be drawn over (a chip or a grain of dirt that came to rest at the bottom of a pool, a
		/// stain): at the next step it is put back at the liquid's surface above that spot, instead of being lost. Call before the pixel is drawn over.
		/// Thread safe.
		/// @param x The pixel, in scene coordinates.
		/// @param y The pixel, in scene coordinates.
		/// @return Whether there was liquid there to keep (false with flowing liquids off: nothing to do).
		static bool KeepLiquidAt(int x, int y);

		/// Gets whether there is any liquid or loose powder near a point, from a few samples in a plus shape: for callers that would otherwise queue work on
		/// dry, solid ground (a gib's disturbance and splash). Reads the terrain only.
		static bool IsFlowingNear(const Vector& position, float radius);

		/// Throws some of the liquid near a point into the air as drops, which fly and rejoin it where they land: for explosions and things falling in. Thread safe.
		/// @param position Centre, in scene coordinates.
		/// @param radius How far around it liquid is thrown from.
		/// @param share How much of the liquid near the surface there goes flying, 0 to 1.
		/// @param speed How hard it's thrown, in metres a second.
		static void Splash(const Vector& position, float radius, float share, float speed);

		/// Throws up a splash that is only for the eye: drops in the liquid's colour and spray, which fly and are gone where they land. Nothing is taken
		/// from the liquid or added to it (what a falling body pushes aside goes into the level: TerrainCollapse). As big as the WaterSplash setting. Thread safe.
		/// @param position Where the body meets the surface, in scene coordinates.
		/// @param width How wide the body is there, in pixels.
		/// @param speed How fast it went in, in metres a second.
		/// @param colorIndex Palette index of the liquid there, for the drops' colour.
		static void VisualSplash(const Vector& position, float width, float speed, int colorIndex);

		/// Leaves froth on a liquid's surface, only for the eye: pale bubbly puffs that sit on it and fade (the SplashFroth settings). For a splash, and
		/// where the level rises from something falling in (TerrainCollapse). Thread safe.
		/// @param position The middle of the stretch of surface, in scene coordinates.
		/// @param width How wide a stretch, in pixels.
		/// @param count How many puffs at the plain setting.
		/// @param colorIndex Palette index of the liquid there; water and the mask colour give white froth, any other liquid a paler froth of its own colour.
		static void Froth(const Vector& position, float width, int count, int colorIndex);

		/// Lets a particle that just settled into the terrain join in: a drop of liquid in that liquid's own colour starts flowing (so blood, drawn in water, stays put),
		/// and a burning particle sets the flammable pixel it became alight. Call after the particle is drawn into the terrain.
		/// @param particle The settled particle.
		static void OnParticleSettled(const MovableObject* particle);

		/// Advances the liquids one simulation step. Call once per sim update, from the main thread.
		static void Update();

		/// Gets the current state as text, for saved games.
		static std::string GetSaveState();

		/// Sets state from a saved game, applied when the loaded scene starts.
		static void SetPendingLoadState(const std::string& state);

		/// Forgets all moving liquid, e.g. when the scene changes. Liquid pixels stay where they are.
		static void Clear();

		/// Gets how many liquid pixels are moving, for statistics.
		static int GetActiveCount();

		/// Gets how long the last update took, in milliseconds, for statistics.
		static float GetLastUpdateMS();

		/// Gets the moving liquid pixels in an area, for the world simulation overlay. Call from the main thread, between sim updates.
		/// @param corner The area's top left corner, in scene pixels (it may lie off a wrapping scene's edge).
		/// @param width The area's width.
		/// @param height The area's height.
		/// @param pixels Filled with the pixels, as scene positions.
		/// @param limit The most to give.
		static void GetActivePixels(const Vector& corner, float width, float height, std::vector<Vector>& pixels, size_t limit);

		/// Visits every moving liquid pixel, for the renderer's water surface (SceneLighting's flow field). Only reads; the simulation is untouched. Call from the main thread, between sim updates.
		/// @param visit Called with the pixel's scene position, its sideways and falling speed (quarter pixels per step, sideways negative to the left) and how many of its steps it hasn't got lower.
		static void VisitMovingPixels(const std::function<void(int x, int y, int velX, int velY, int still)>& visit);

	private:
		static bool s_Enabled; //!< Whether flowing liquids are on.
		static bool s_Powders; //!< Whether loose powders slide and pile.
		static bool s_Freezing; //!< Whether still water freezes over in snowy weather.
		static bool s_BloodFlows; //!< Whether settled blood runs and pools.
		static bool s_DrainBottom; //!< Whether liquid drains out of the bottom of the map.
		static bool s_DrainSides; //!< Whether liquid drains out of the sides of the map (where it doesn't wrap).
		static bool s_PowdersFallOut; //!< Whether loose ground falls out of the bottom and sides of the map.
	};
} // namespace RTE
