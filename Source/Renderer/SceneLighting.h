#pragma once

#include "Vector.h"
#include "LightingSettings.h"
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
	struct SceneLight;
	struct ScreenShockwave;

	/// Lights the scene: sky light that propagates through a low resolution grid of the terrain, dynamic lights cast by glow effects, glows drawn as emitted light, bloom and tonemapping.
	/// Works on each player screen after the scene is drawn and before the HUD, so the HUD is never lit.
	class SceneLighting {

	public:
		/// Constructor.
		/// @param settings The settings to use, not owned. Read every frame, so they can be changed live.
		explicit SceneLighting(LightingSettings& settings);
		~SceneLighting();

		/// Gets the tunable settings.
		LightingSettings& GetSettings() { return m_Settings; }

		/// Updates the world light grid from the terrain and propagates sky light. Call once per frame before lighting any player screens.
		void Update();

		/// Lights a player screen in place.
		/// @param playerScreen The render target holding the unlit scene for this player screen.
		/// @param screenOrigin Scene position of the player screen's top left pixel.
		/// @param screenEffects Glow effects visible on this screen, with positions relative to the screen.
		void LightPlayerScreen(int screenIndex, RenderTarget* playerScreen, const Vector& screenOrigin, const std::list<PostEffect>& screenEffects, const std::vector<SceneLight>& screenLights, const std::vector<ScreenShockwave>& screenShockwaves);

		/// Forces the world light grid to be rebuilt from scratch, e.g. after the scene's terrain changed wholesale.
		void InvalidateWorld() { m_WorldScene = nullptr; }

		/// Gets the linear daylight tint for a time of day.
		/// @param hours Time of day in hours, 0 to 24.
		/// @return The daylight tint, white at noon.
		static glm::vec3 GetDaylightTint(float hours);

		/// Prepares the terrain shader (scorch marks, cooling hot spots) for this frame and returns it, to set as the current shader while terrain layers are drawn.
		/// @return The terrain shader, or nullptr if there's no scene.
		const Shader* PrepareTerrainShader();

		/// Gets statistics from the last frame, for the Graphics Lab.
		int GetLastLightCount() const { return m_LastLightCount; }
		int GetGridCellSize() const { return m_CellSize; }
		int GetGridWidth() const { return m_GridWidth; }
		int GetGridHeight() const { return m_GridHeight; }

		/// Gets the first player screen's average scene luminance and the exposure auto exposure applied, for the Graphics Lab. Reading them stalls the GPU, so only while it's shown.
		/// @param averageLuminance The adapted average luminance.
		/// @param autoExposure The exposure multiplier from auto exposure.
		void ReadAutoExposure(float& averageLuminance, float& autoExposure) const;

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

		LightingSettings& m_Settings;

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
		int m_LastLightCount = 0;
		long long m_LastSimUpdateCount = -1; //!< For advancing the time of day in sim time.
		glm::vec3 m_EffectiveSky{1.0F}; //!< Sky light after time of day, this frame.
		glm::vec3 m_EffectiveAmbient{1.0F}; //!< Ambient light after time of day, this frame.
		glm::vec3 m_EffectiveForegroundAmbient{1.0F}; //!< Foreground light floor after time of day, this frame.
		float m_NightSky = 0.0F; //!< How visible the stars and moon are, this frame.
		float m_MoonHours = 0.0F; //!< Where the moon is along its path across the sky, in sun-path hours (6 rising, 18 setting).
		float m_Lightning = 0.0F; //!< Current lightning flash brightness.
		float m_LightningSecondsLeft = 0.0F; //!< Time left in the current flash.
		float m_NextLightningSeconds = 8.0F; //!< Sim seconds until the next flash.
		long long m_LightningLastSimUpdate = -1;
		unsigned int m_LightningRandom = 12345u; //!< Render-only random state, so lightning never touches the sim's random numbers.

		GLTarget m_OccupancyTexture;
		GLTarget m_SkylineTexture;
		GLTarget m_SkyLight[2]; //!< Ping-ponged sky light propagation buffers.
		GLTarget m_Scorch; //!< World space soot darkness, R.
		int m_ScorchCellSize = 2; //!< Size of a scorch map texel, in scene pixels.
		int m_CurrentSkyLight = 0;

		int m_ScreenWidth = 0;
		int m_ScreenHeight = 0;
		GLTarget m_DynamicLight;
		GLTarget m_Emissive;
		GLTarget m_Distortion; //!< Screen space displacement in pixels, RG.
		GLTarget m_GodRays; //!< Half resolution light shafts.
		static constexpr int c_IndirectMipCount = 4;
		GLTarget m_IndirectMips[c_IndirectMipCount]; //!< Downsample chain of the lit scene, the smallest is the next frame's indirect light.
		static constexpr int c_MaxScreens = 4;
		GLTarget m_IndirectHistory[c_MaxScreens]; //!< Per player screen, last frame's heavily blurred lit scene.
		glm::vec2 m_IndirectHistoryOrigin[c_MaxScreens]; //!< Per player screen, the screen origin the history was made at, for reprojection.
		bool m_IndirectHistoryValid[c_MaxScreens] = {};
		GLTarget m_HDRScene;
		static constexpr int c_BloomMipCount = 5;
		GLTarget m_BloomMips[c_BloomMipCount];
		static constexpr int c_RCCascadeCount = 5;
		GLTarget m_RCScene; //!< Half resolution radiance cascades input: light and occluders.
		GLTarget m_RCCascades[2]; //!< Ping-ponged cascades, the last one written is cascade 0.
		GLTarget m_RCIrradiance; //!< Quarter resolution light from radiance cascades.
		GLTarget m_RCPreviousLit[c_MaxScreens]; //!< Per player screen, last frame's lit scene at half resolution, for bounces.
		glm::vec2 m_RCPreviousOrigin[c_MaxScreens]; //!< Per player screen, the screen origin m_RCPreviousLit was made at.
		bool m_RCPreviousValid[c_MaxScreens] = {};
		GLTarget m_Luminance; //!< Power of two log luminance of the HDR scene, with mipmaps, for auto exposure.
		int m_LuminanceMaxLod = 0;
		GLTarget m_AdaptedLuminance[c_MaxScreens][2]; //!< Per player screen, ping-ponged 1x1 adapted log luminance.
		int m_AdaptedLuminanceCurrent[c_MaxScreens] = {};
		bool m_AdaptedLuminanceValid[c_MaxScreens] = {};
		double m_LastAdaptSeconds[c_MaxScreens] = {};

		std::unique_ptr<Shader> m_PropagateShader;
		std::unique_ptr<Shader> m_PointLightShader;
		std::unique_ptr<Shader> m_CompositeShader;
		std::unique_ptr<Shader> m_EmissiveShader;
		std::unique_ptr<Shader> m_BloomDownsampleShader;
		std::unique_ptr<Shader> m_BloomUpsampleShader;
		std::unique_ptr<Shader> m_TonemapShader;
		std::unique_ptr<Shader> m_RCSceneShader;
		std::unique_ptr<Shader> m_RCCascadeShader;
		std::unique_ptr<Shader> m_RCIrradianceShader;
		std::unique_ptr<Shader> m_LuminanceShader;
		std::unique_ptr<Shader> m_ExposureAdaptShader;
		std::unique_ptr<Shader> m_ShockwaveShader;
		std::unique_ptr<Shader> m_PrecipitationShader;
		std::unique_ptr<Shader> m_GodRaysShader;
		std::unique_ptr<Shader> m_GodRaysApplyShader;
		std::unique_ptr<Shader> m_ScorchShader;
		std::unique_ptr<Shader> m_TerrainShader;
		GLuint m_EmptyVAO = 0; //!< For draws that generate their vertices from gl_VertexID.

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
		void StampScorchMarks();

		const GlowInfo& GetGlowInfo(const BitmapTexture* glowTexture);

		void DrawFullscreen() const;
		/// Uploads m_QuadVertices and draws them as quads.
		void DrawQuads(size_t firstQuad, size_t quadCount);
		void UploadQuads();
	};
} // namespace RTE
