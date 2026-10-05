#pragma once

#include "glm/glm.hpp"
#include <string>
#include <vector>

namespace RTE {
	class Camera;
	class Vector;
	class Color;
	class Material;

	/// Purely visual particles: sparks, dust and debris chips from explosions and impacts. They never affect the simulation:
	/// they're spawned from simulation events through a thread safe queue, move in simulation time with their own random numbers,
	/// and only ever read the terrain to bounce off it. Thousands cost next to nothing.
	class EffectsParticles {

	public:
		/// A spark to draw as emitted light, in screen space.
		struct Spark {
			glm::vec2 Position; //!< Screen pixels.
			glm::vec2 Direction; //!< Unit vector along the streak.
			float Length; //!< Streak length in pixels.
			glm::vec3 Color; //!< Linear-ish brightness, fed to the emissive buffer.
		};

		/// A translucent puff (dust) to draw lit over the scene, in screen space.
		struct Puff {
			glm::vec2 Position; //!< Screen pixels, centre.
			float Size; //!< Diameter in pixels.
			glm::vec4 Color; //!< RGB albedo 0..1 in gamma space, A opacity.
		};

		/// Gets a color as 0xRRGGBB: its RGB if set, otherwise its palette index looked up in the palette. 0 if it has neither (index 0 is the mask color).
		static unsigned int ColorToRGB(const Color& color);

		/// Queues an explosion's effects. Thread safe; call from the simulation.
		/// @param position Where, in scene coordinates.
		/// @param energy The gib energy, scaling the amount of effects.
		static void SpawnExplosion(const Vector& position, float energy);

		/// Queues effects for something hitting terrain. Thread safe and cheap to call often; most calls are skipped by a per frame budget.
		/// @param position Where, in scene coordinates.
		/// @param velocity Velocity of the hitting particle.
		/// @param materialRGB The hit material's color, 0xRRGGBB, for debris and dust.
		/// @param hardness 0 for soft materials like dirt (dust), 1 for hard ones like metal and rock (sparks).
		static void SpawnImpact(const Vector& position, const Vector& velocity, unsigned int materialRGB, float hardness);

		/// Queues visual particles of one kind, for mods (INI VisualEmission on any object, Lua EmitVisualParticles). Thread safe. Render only: they never touch the simulation.
		/// @param kind "Sparks" (glowing streaks), "Dust" (soft puffs), "Debris" (little chips that bounce), "Embers" (rise and drift), "Mist" (soft pale spray that hangs and thins) or "Smoke" (soft dark puffs that roll up and linger).
		/// @param position Where, in scene coordinates.
		/// @param velocity Which way they fly, in meters per second like an object's velocity.
		/// @param spread How much each particle's direction and speed vary, 0 (none) to 1 (every direction).
		/// @param count How many.
		/// @param colorRGB Their color as 0xRRGGBB, or 0 for the kind's own color.
		/// @return Whether the kind was recognised.
		static bool Emit(const std::string& kind, const Vector& position, const Vector& velocity, float spread, int count, unsigned int colorRGB);

		/// Queues a glowing ember that rises from a fire. Render only.
		static void SpawnEmber(const Vector& position);

		/// Moves all particles on by however much simulation time passed since the last call, and adds queued spawns. Call once per frame.
		/// @param amount Multiplier for how many particles spawn; 0 turns the system off.
		static void Update(float amount);

		/// Draws the opaque debris chips into the scene (so they're lit like everything else). Call while drawing a camera's view.
		/// Translucent dust is drawn later over the lit scene, see GetPuffs.
		static void Draw(const Camera& camera);

		/// Gets the sparks visible in a screen area, positioned relative to it.
		/// @param screenOrigin Scene position of the screen's top left pixel.
		/// @param width Screen size in pixels.
		/// @param height Screen size in pixels.
		/// @param sparks Filled with the visible sparks.
		static void GetSparks(const glm::vec2& screenOrigin, int width, int height, std::vector<Spark>& sparks);

		/// Gets the dust puffs visible in a screen area, positioned relative to it.
		static void GetPuffs(const glm::vec2& screenOrigin, int width, int height, std::vector<Puff>& puffs);

		/// Gets the fire of explosions visible in a screen area, as puffs that glow (RGB brightness, to draw into the emissive buffer with the puff texture).
		static void GetFire(const glm::vec2& screenOrigin, int width, int height, std::vector<Puff>& fire);

		/// Gets the GL texture puffs are drawn with (soft round, white with alpha).
		static unsigned int GetPuffTexture();

		/// Records a smoke particle drawn this frame, for light scattering in smoke. Duplicate calls for the same object (several cameras) are ignored.
		/// @param object Identifies the smoke particle.
		/// @param position Scene position of its centre.
		/// @param radius Its radius in pixels.
		/// @param density How thick it is, 0 to 1.
		static void RegisterSmoke(const void* object, const glm::vec2& position, float radius, float density);

		/// Forgets the smoke recorded last frame. Call at the start of each frame's drawing.
		static void BeginFrame();

		/// Gets the smoke visible in a screen area, as puffs (A = density).
		static void GetSmoke(const glm::vec2& screenOrigin, int width, int height, std::vector<Puff>& smoke);

		/// A splat of liquid to stamp into the terrain stain map.
		struct Stain {
			glm::vec2 Position; //!< Scene pixels.
			glm::vec3 Color; //!< 0..1, gamma space.
			float Radius; //!< Pixels.
		};

		/// Gets whether drops of a material stain terrain: blood (any color) and oil. Cached per material.
		static bool IsStainingMaterial(const Material* material);

		/// Queues a stain where blood or oil hit terrain. Thread safe and cheap; a per frame budget skips the excess.
		static void SpawnStain(const Vector& position, int red, int green, int blue, float speed);

		/// Takes the stains queued since the last call.
		static std::vector<Stain> TakeStains();

		/// Removes all particles, e.g. when the scene changes.
		static void Clear();

		/// Gets how many particles are alive, for statistics.
		static int GetCount();
	};
} // namespace RTE
