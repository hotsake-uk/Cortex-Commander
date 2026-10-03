#pragma once

#include "glm/glm.hpp"
#include <vector>

namespace RTE {
	class Camera;
	class Vector;

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

		/// Queues an explosion's effects. Thread safe; call from the simulation.
		/// @param position Where, in scene coordinates.
		/// @param energy The gib energy, scaling the amount of effects.
		static void SpawnExplosion(const Vector& position, float energy);

		/// Queues effects for something hitting terrain. Thread safe and cheap to call often; most calls are skipped by a per frame budget.
		/// @param position Where, in scene coordinates.
		/// @param velocity Velocity of the hitting particle.
		/// @param materialColor Palette index of the hit material's color, for debris and dust.
		/// @param hardness 0 for soft materials like dirt (dust), 1 for hard ones like metal and rock (sparks).
		static void SpawnImpact(const Vector& position, const Vector& velocity, int materialColor, float hardness);

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

		/// Gets the GL texture puffs are drawn with (soft round, white with alpha).
		static unsigned int GetPuffTexture();

		/// Removes all particles, e.g. when the scene changes.
		static void Clear();

		/// Gets how many particles are alive, for statistics.
		static int GetCount();
	};
} // namespace RTE
