#pragma once

#include "Vector.h"
#include "glad/gl.h"
#include "glm/glm.hpp"

#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace RTE {

	class RenderTarget;
	class Shader;
	class BitmapTexture;
	struct PostEffect;

	/// Tunable parameters of the scene lighting and post-processing. Colors are linear.
	struct LightingSettings {
		bool Enabled = true; //!< Whether scene lighting is applied at all. Glows and bloom still apply when disabled.
		glm::vec3 Ambient = {0.13F, 0.13F, 0.16F}; //!< Light where no sky light reaches.
		glm::vec3 SkyColor = {1.0F, 0.98F, 0.95F}; //!< Light under open sky.
		float AirFalloff = 0.96F; //!< How much sky light is kept per grid cell travelled through air.
		float SolidFalloff = 0.6F; //!< How much sky light is kept per grid cell travelled into terrain.
		int PropagationIterationsPerFrame = 6; //!< Sky light propagation iterations per frame. Light settles into new terrain over a few frames.

		float GlowLightIntensity = 2.2F; //!< Brightness of the lights cast by glow effects.
		float GlowLightRadiusScale = 4.0F; //!< Radius of glow lights relative to the glow sprite's size.
		float ShadowStrength = 0.85F; //!< How much terrain blocks dynamic lights, 0 to 1.
		float EmissiveIntensity = 1.0F; //!< Brightness of glow sprites drawn as emitted light.

		bool BloomEnabled = true;
		float BloomThreshold = 0.9F;
		float BloomKnee = 0.4F;
		float BloomIntensity = 0.6F;

		float Exposure = 1.0F;
		float ShoulderStart = 0.75F; //!< Linear brightness above which highlights are softly compressed.
		float Vignette = 0.15F;
		float Saturation = 1.05F;
	};

	/// Lights the scene: sky light that propagates through a low resolution grid of the terrain, dynamic lights cast by glow effects, glows drawn as emitted light, bloom and tonemapping.
	/// Works on each player screen after the scene is drawn and before the HUD, so the HUD is never lit.
	class SceneLighting {

	public:
		SceneLighting();
		~SceneLighting();

		/// Gets the tunable settings.
		LightingSettings& GetSettings() { return m_Settings; }

		/// Updates the world light grid from the terrain and propagates sky light. Call once per frame before lighting any player screens.
		void Update();

		/// Lights a player screen in place.
		/// @param playerScreen The render target holding the unlit scene for this player screen.
		/// @param screenOrigin Scene position of the player screen's top left pixel.
		/// @param screenEffects Glow effects visible on this screen, with positions relative to the screen.
		void LightPlayerScreen(RenderTarget* playerScreen, const Vector& screenOrigin, const std::list<PostEffect>& screenEffects);

		/// Forces the world light grid to be rebuilt from scratch, e.g. after the scene's terrain changed wholesale.
		void InvalidateWorld() { m_WorldScene = nullptr; }

	private:
		/// A GL texture with an optional framebuffer.
		struct GLTarget {
			GLuint Texture = 0;
			GLuint Framebuffer = 0;
			int Width = 0;
			int Height = 0;

			void Create(int width, int height, GLenum internalFormat, GLenum format, GLenum type, GLint filter, GLint wrapS, GLint wrapT, bool withFramebuffer);
			void Destroy();
		};

		/// A light or emissive quad vertex, positioned in screen pixels.
		struct QuadVertex {
			float X, Y, Z;
			float U, V;
			float R, G, B, A;
			float CenterX, CenterY, Radius;
		};

		/// Cached properties of a glow sprite.
		struct GlowInfo {
			glm::vec3 LightColor; //!< Average color of the glow's lit pixels, linear.
			float Size; //!< The larger of the glow's dimensions.
		};

		LightingSettings m_Settings;

		const void* m_WorldScene = nullptr; //!< The scene the world grid was built for.
		const void* m_WorldMaterialBitmap = nullptr; //!< The terrain material bitmap the world grid was built from.
		int m_SceneWidth = 0;
		int m_SceneHeight = 0;
		int m_CellSize = 4; //!< Size of a world grid cell, in scene pixels.
		int m_GridWidth = 0;
		int m_GridHeight = 0;
		bool m_WrapX = false;
		bool m_WrapY = false;
		std::vector<unsigned char> m_Occupancy; //!< Terrain coverage per grid cell, 0 air .. 255 solid.
		std::vector<float> m_Skyline; //!< Per grid column, the row of the first mostly solid cell, normalized by grid height.
		int m_NextRefreshRow = 0; //!< Row the round-robin terrain refresh continues from.
		int m_FrameCounter = 0;

		GLTarget m_OccupancyTexture;
		GLTarget m_SkylineTexture;
		GLTarget m_SkyLight[2]; //!< Ping-ponged sky light propagation buffers.
		int m_CurrentSkyLight = 0;

		int m_ScreenWidth = 0;
		int m_ScreenHeight = 0;
		GLTarget m_DynamicLight;
		GLTarget m_HDRScene;
		static constexpr int c_BloomMipCount = 5;
		GLTarget m_BloomMips[c_BloomMipCount];

		std::unique_ptr<Shader> m_PropagateShader;
		std::unique_ptr<Shader> m_PointLightShader;
		std::unique_ptr<Shader> m_CompositeShader;
		std::unique_ptr<Shader> m_EmissiveShader;
		std::unique_ptr<Shader> m_BloomDownsampleShader;
		std::unique_ptr<Shader> m_BloomUpsampleShader;
		std::unique_ptr<Shader> m_TonemapShader;

		GLuint m_FullscreenVAO = 0;
		GLuint m_FullscreenVBO = 0;
		GLuint m_QuadVAO = 0;
		GLuint m_QuadVBO = 0;
		GLuint m_QuadIBO = 0;
		size_t m_QuadIndexCapacity = 0;
		std::vector<QuadVertex> m_QuadVertices;

		std::unordered_map<const BitmapTexture*, GlowInfo> m_GlowInfoCache;

		void LoadShaders();
		void CreateGeometry();

		/// Rebuilds the world grid if the scene changed. Returns false if there's no scene.
		bool EnsureWorldResources();
		void DestroyWorldResources();
		void EnsureScreenResources(int width, int height);
		void DestroyScreenResources();

		/// Recomputes occupancy for a range of grid rows from the terrain material layer.
		void RefreshOccupancyRows(int firstRow, int endRow);
		void RecomputeSkyline();
		void UploadOccupancyRows(int firstRow, int endRow);
		void PropagateSkyLight(int iterations);

		const GlowInfo& GetGlowInfo(const BitmapTexture* glowTexture);

		void DrawFullscreen() const;
		/// Uploads m_QuadVertices and draws them as quads.
		void DrawQuads(size_t firstQuad, size_t quadCount);
		void UploadQuads();
	};
} // namespace RTE
