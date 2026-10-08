#pragma once

#include "Vector.h"
#include "LightingSettings.h"
#include "glad/gl.h"
#include "glm/glm.hpp"

#include <array>
#include <cstdint>
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
		/// Starts a lightning flash now, as if a bolt just struck (the sandbox's lightning).
		void TriggerLightning() { m_LightningSecondsLeft = 0.45F; }

		/// A big blast just went off in view: smear the lens for an instant (a pulse of chromatic aberration that fades in a fraction of a second).
		/// @param strength 0 to 1.
		void AddBlastPulse(float strength) { m_BlastPulse = std::max(m_BlastPulse, strength); }

		/// How badly the unit a player screen's player controls is hurt, 0 (fine) to 1 (nearly dead), for the grade to answer (LightingSettings::EventLooks). Every frame, before LightPlayerScreen.
		void SetScreenHurt(int screenIndex, float hurt) {
			if (screenIndex >= 0 && screenIndex < c_MaxScreens) {
				m_ScreenHurtTarget[screenIndex] = std::clamp(hurt, 0.0F, 1.0F);
			}
		}

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

		/// One light of the first player screen's last frame, for the light sources overlay (see SetRecordDebugLights).
		struct DebugLight {
			Vector Pos; //!< Scene coordinates.
			glm::vec3 Color{0.0F}; //!< Linear, intensity included, before the player's light colour settings.
			float Radius = 0.0F;
			glm::vec2 Direction{1.0F, 0.0F}; //!< A cone light's direction, y down.
			float ConeCos = -2.0F; //!< A cone light's half angle's cosine; below -1 for an all-round light.
			bool Glow = false; //!< Cast by a glow effect rather than registered as a light.
			bool Dropped = false; //!< Left out for the cap on lights on screen (LightingSettings::MaxScreenLights).
		};

		/// Counts of the first player screen's last frame's lights, for the light sources overlay.
		struct DebugLightCounts {
			int Glows = 0; //!< Lights cast by glow effects.
			int Lights = 0; //!< All-round lights drawn (after merging and the cap).
			int Cones = 0; //!< Cone lights drawn.
			int Merged = 0; //!< All-round lights merged into another on the same spot.
			int Dropped = 0; //!< Lights left out for the cap.
			float ReachSquared = 0.0F; //!< The drawn lights' radii squared, summed: about how many pixels the light pass fills.
		};

		/// Sets whether the first player screen's lights are kept each frame for the light sources overlay. Off, nothing is kept.
		void SetRecordDebugLights(bool record) { m_RecordDebugLights = record; }

		/// Gets the first player screen's lights from the last frame recorded (see SetRecordDebugLights).
		const std::vector<DebugLight>& GetDebugLights() const { return m_DebugLights; }

		/// Gets the counts that go with GetDebugLights.
		const DebugLightCounts& GetDebugLightCounts() const { return m_DebugLightCounts; }

		/// Gets the direction towards the sun (or the moon at night) this frame, a unit vector in scene pixels, y down.
		glm::vec2 GetSunDirection() const { return m_SunDirection; }

		/// Gets the sun's shadow strength this frame, after the time of day and the weather.
		float GetSunShadowStrength() const { return m_SunShadowStrength; }

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
		static constexpr int c_MaxScreens = 4; //!< Player screens; sizes the per-screen arrays below, so it comes first.
		static constexpr float c_ShelterMaxSlope = 6.0F; //!< Pixels across per pixel down past which weather is too level for the shelter map, and is marched instead.
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
			float ConeX = 1.0F, ConeY = 0.0F, ConeCos = -2.0F; //!< Cone lights: direction and cosine of the half angle (below -1 for none).
		};

		/// Cached properties of a glow sprite.
		struct GlowInfo {
			glm::vec3 LightColor; //!< Average color of the glow's lit pixels, linear.
			float Size; //!< The larger of the glow's dimensions.
		};

		LightingSettings& m_Settings;

		const void* m_WorldScene = nullptr; //!< The scene the world grid was built for.
		unsigned int m_WorldSceneGeneration = 0; //!< The scene load generation the world grid was built for. A new scene can be allocated where the old one was, so the pointer alone can't tell.
		const void* m_WorldMaterialBitmap = nullptr; //!< The terrain material bitmap the world grid was built from.
		int m_SceneWidth = 0;
		int m_SceneHeight = 0;
		int m_CellSize = 4; //!< Size of a world grid cell, in scene pixels.
		int m_GridWidth = 0;
		int m_GridHeight = 0;
		bool m_WrapX = false;
		bool m_WrapY = false;
		static constexpr int c_ShadowFieldCells = 16; //!< How far the terrain distance field reaches, in grid cells. Further is stored as this far.
		static constexpr unsigned char c_ShadowWallBlock = 128; //!< A cell stopping at least this much light (occupancy R) is a wall to the distance field. Lighter ones (water, glass, oil, a cell barely touched) only dim light, as density.
		GLTarget m_ShadowFieldTexture; //!< R: distance from each grid cell to the nearest wall, in c_ShadowFieldCells, for tracing lights' terrain shadows (LightingSettings::LightShadowField).
		std::vector<unsigned char> m_ShadowField; //!< One byte per grid cell, as m_ShadowFieldTexture.
		std::vector<unsigned short> m_ShadowFieldScratch; //!< Chamfer distances for RefreshShadowField, in thirds of a cell.
		int m_WallChangeMinColumn = 0, m_WallChangeMinRow = 0, m_WallChangeEndColumn = 0, m_WallChangeEndRow = 0; //!< The cells whose wall status RefreshOccupancyRows changed since the field was last refreshed; empty when the end is not past the start.
		GLTarget m_LampCache; //!< World lamp cache (LightingSettings::LampCache): RGB light from the steady scenery lamps, m_LampCacheCell pixels a texel. Its framebuffer writes m_LampDirection too.
		GLTarget m_LampDirection; //!< RG: which way the steady lamps' light comes from, as the xy of a unit vector, times its brightness, added up.
		int m_LampCacheCell = 0; //!< Pixels a lamp cache texel, 0 while there's no cache.
		uint64_t m_LampCacheSignature = 0; //!< The steady lamps and settings the cache was lit with.
		int m_LampCacheLamps = 0; //!< How many steady lamps are in it.
		bool m_LampCacheReady = false; //!< The cache holds the steady lamps, so the screens draw it instead of them.
		int m_LampDirtyMinX = 0, m_LampDirtyMinY = 0, m_LampDirtyEndX = 0, m_LampDirtyEndY = 0; //!< Scene pixels where the ground changed since the cache was last relit; empty when the end is not past the start.
		std::vector<unsigned char> m_Occupancy; //!< Four bytes per grid cell: terrain coverage (0 air .. 255 solid), then how metallic and how glossy the terrain there is (from its materials), then a spare.
		std::array<unsigned char, 256> m_MaterialMetalness{}; //!< How metallic each terrain material looks, 0 to 255.
		std::array<unsigned char, 256> m_MaterialGloss{}; //!< How glossy each terrain material looks, 0 to 255.
		std::array<unsigned char, 256> m_MaterialLightBlock{}; //!< How much each terrain material stops light, 0 (none, like air) to 255 (all of it, like rock). Water lets most through.
		std::vector<float> m_Skyline; //!< Per grid column, the row of the first mostly solid cell, normalized by grid height.
		int m_NextRefreshRow = 0; //!< Row the round-robin terrain refresh continues from.
		int m_FrameCounter = 0;
		int m_LastLightCount = 0;
		long long m_LastSimUpdateCount = -1; //!< For advancing the time of day in sim time.
		glm::vec3 m_EffectiveSky{1.0F}; //!< Sky light after time of day, this frame.
		glm::vec3 m_EffectiveAmbient{1.0F}; //!< Ambient light after time of day, this frame.
		glm::vec3 m_EffectiveForegroundAmbient{1.0F}; //!< Foreground light floor after time of day, this frame.
		float m_NightSky = 0.0F; //!< How visible the stars and moon are, this frame.
		glm::vec3 m_SkyDaylight{1.0F}; //!< The colour of daylight at this hour, white at noon.
		glm::vec3 m_SkyZenith{0.0F}; //!< The sky's colour overhead at this hour, linear.
		glm::vec3 m_SkyHorizon{0.0F}; //!< And at the horizon.
		glm::vec3 m_SkyCloud{1.0F}; //!< What the light of the hour makes of white cloud.
		float m_NightDim = 1.0F; //!< How much of the scene's light is left in the dead of night, this frame (1 by day).
		float m_SkyRecolor = 0.0F; //!< How far the sky art's colours are replaced by those, this frame.
		float m_SnowCover = 0.0F; //!< How deep snow has settled on exposed ground, 0 to 1. Builds up while it snows, melts otherwise.
		float m_Wetness = 0.0F; //!< How wet exposed ground is from rain, 0 to 1.
		float m_MoonHours = 0.0F; //!< Where the moon is along its path across the sky, in sun-path hours (6 rising, 18 setting).
		float m_BlastPulse = 0.0F; //!< Lens smear from a big blast, fading out.
		double m_BlastPulseLastTime = 0.0; //!< Real seconds when the pulse was last faded.
		float m_ScreenHurtTarget[c_MaxScreens] = {}; //!< Per player screen, how hurt its player's unit is (SetScreenHurt).
		float m_ScreenHurt[c_MaxScreens] = {}; //!< And how much the grade shows it, easing towards that.
		float m_ScreenWarmthTarget[c_MaxScreens] = {}; //!< Per player screen, how much burning terrain there is around its middle, 0 to 1.
		float m_ScreenWarmth[c_MaxScreens] = {}; //!< And how much the grade shows it, easing towards that.
		double m_EventLookLastTime[c_MaxScreens] = {}; //!< Real seconds when each screen's event looks last eased.
		float m_Lightning = 0.0F; //!< Current lightning flash brightness.
		float m_LightningSecondsLeft = 0.0F; //!< Time left in the current flash.
		float m_NextLightningSeconds = 8.0F; //!< Sim seconds until the next flash.
		long long m_LightningLastSimUpdate = -1;
		unsigned int m_LightningRandom = 12345u; //!< Render-only random state, so lightning never touches the sim's random numbers.

		GLTarget m_OccupancyTexture;
		GLTarget m_SkylineTexture;
		GLTarget m_SunMap; //!< The sun's shadow map (LightingSettings::SunShadowMap): 1 row, R32F, for each ray from the sun the scene y of the first solid point on it.
		float m_SunMapSlope = 0.0F; //!< How far a ray moves in x per pixel down, as the map was last made.
		float m_SunMapStart = 0.0F; //!< Where its first ray crosses the top of the scene.
		float m_SunMapTexel = 1.0F; //!< Scene pixels between its rays.
		bool m_SunMapReady = false; //!< It's been made for this scene and is in use.
		GLTarget m_ShelterMap; //!< The weather's shelter map (LightingSettings::ShelterMask): the same kind of strip as the sun's, its rays coming down the way rain or snow falls and stopping at anything solid.
		float m_ShelterMapSlope = 0.0F; //!< How far a ray moves in x per pixel down, as the map was last made.
		float m_ShelterMapStart = 0.0F; //!< Where its first ray crosses the top of the scene.
		float m_ShelterMapTexel = 1.0F; //!< Scene pixels between its rays.
		bool m_ShelterMapReady = false; //!< It's been made for this scene and the weather and is in use.
		GLTarget m_SkyLight[2]; //!< Ping-ponged sky light propagation buffers. R = sky light, G = how much of the sun (or moon) is visible.
		bool m_RecordDebugLights = false; //!< Whether LightPlayerScreen keeps the first screen's lights for the light sources overlay.
		std::vector<DebugLight> m_DebugLights; //!< The first screen's lights from the last frame recorded.
		DebugLightCounts m_DebugLightCounts; //!< Their counts.
		glm::vec2 m_SunDirection{0.0F, -1.0F}; //!< Unit vector towards the sun (or the moon at night) in scene pixels, y down, this frame.
		float m_SunShadowStrength = 0.0F; //!< Sun shadow strength after time of day and weather, this frame.
		float m_SunDiscStrength = 0.0F; //!< How bright the sun's disc is in the sky this frame: none at night, fading at the horizon and under weather.
		float m_SunArc = 0.0F; //!< Where the sun is along its path, -1 rising to 1 setting.
		float m_CloudDrift = 0.0F; //!< How far the clouds have drifted with the wind, in scene pixels.
		float m_CloudCover = -1.0F; //!< How much of the sky is cloud now, 0 to 1: follows the weather, gathering faster than it breaks up. Below 0 until first set.
		GLTarget m_FlowTexture; //!< The moving liquid, for the water surface (LightingSettings::WaterFlowSurface), in the light grid's cells: R sideways speed (128 none), G speed, B how lately it moved, A 255 where any moves.
		std::vector<unsigned char> m_Flow; //!< Four bytes per grid cell, as m_FlowTexture.
		static constexpr int c_FlowTileCells = 32; //!< The flow field is cleared and uploaded in square tiles of this many cells a side.
		int m_FlowTileColumns = 0;
		std::vector<unsigned char> m_FlowTileMarks; //!< Per tile: 1 to be uploaded this frame, 2 written this frame (either or both).
		std::vector<int> m_FlowTiles; //!< The tiles with moving liquid written into them last frame, to be cleared this frame.
		GLTarget m_Fog[2]; //!< Ping-ponged fog volume, in the light grid's cells: R = how thick the mist or dust is, 0 to 1.
		int m_CurrentFog = 0;
		GLTarget m_WetMap[2]; //!< Ping-ponged wetness map (LightingSettings::WetnessMap), in the light grid's cells: R = how wet, 0 to 1, and past 1 water standing in dips.
		int m_CurrentWetMap = 0;
		bool m_FogLive = false; //!< The fog targets hold fog (cleared when the fog volume is turned off).
		double m_LastFogTime = -1.0; //!< Game seconds of the last fog step.
		glm::vec3 m_DecalFadeDebt{0.0F}; //!< Fading the decal maps owes this many 1/255 steps not yet taken: x soot, y drying, z washing off.
		std::vector<glm::vec4> m_PendingFogPuffs; //!< Puffs taken from PostProcessMan and not yet put in, a step holding only so many.
		GLTarget m_Scorch; //!< World space soot darkness, R; the stains' gloss, G while wet (it dries away) and B once dry.
		GLTarget m_Stains; //!< World space liquid stains, RGB color and A coverage, same cells as m_Scorch.
		int m_ScorchCellSize = 2; //!< Size of a scorch map texel, in scene pixels.
		GLTarget m_DecalGround; //!< Which cells of the scorch and stain maps hold any ground, R: 255 where a cell has a terrain pixel that isn't air. Soot and stains are only kept next to ground (RefreshDecalGround).
		std::vector<unsigned char> m_DecalGroundRows; //!< Rows of m_DecalGround worked out on the way to being uploaded.
		int m_CurrentSkyLight = 0;

		int m_ScreenWidth = 0;
		int m_ScreenHeight = 0;
		GLTarget m_DynamicLight;
		GLTarget m_OccluderSeeds[2]; //!< Ping-ponged jump flood buffers: RG = position of the nearest pixel of a solid object, in full floats: half floats step by 1 px past 1024 and 2 px past 2048, too coarse for the sub-pixel tests that read it.
		GLTarget m_RoundedNormals; //!< The player screen's normals with metallic and glossy objects rounded off (see SurfaceRound.frag).
		GLTarget m_Emissive;
		GLTarget m_Distortion; //!< Screen space displacement in pixels, RG.
		GLTarget m_OutlineRows; //!< Unit outlines: per pixel, the distance along its row to the nearest unit pixel, its side, and whether a stroke may go there (see UnitOutlineRow.frag).
		GLTarget m_GodRays; //!< Half resolution light shafts.
		static constexpr int c_IndirectMipCount = 4;
		GLTarget m_IndirectMips[c_IndirectMipCount]; //!< Downsample chain of the lit scene, the smallest is the next frame's indirect light.
		GLTarget m_IndirectHistory[c_MaxScreens]; //!< Per player screen, last frame's heavily blurred lit scene.
		glm::vec2 m_IndirectHistoryOrigin[c_MaxScreens]; //!< Per player screen, the screen origin the history was made at, for reprojection.
		bool m_IndirectHistoryValid[c_MaxScreens] = {};
		GLTarget m_HDRScene;
		GLTarget m_ModPostScene; //!< A copy of the lit scene for a mod's post pass to read (LightingSettings::ModShaders). Made when one is first used.
		GLTarget m_FocusScene; //!< A copy of the lit scene for depth of field and tilt-shift to read. Made when they're first used.
		static constexpr int c_BloomMipCount = 5;
		GLTarget m_BloomMips[c_BloomMipCount];
		static constexpr int c_RCCascadeCount = 5;
		GLTarget m_SmokeDensity; //!< Half resolution smoke density, for light scattering in smoke.
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
		std::unique_ptr<Shader> m_FogUpdateShader;
		std::unique_ptr<Shader> m_WetnessUpdateShader;
		std::unique_ptr<Shader> m_SunShadowMapShader;
		std::unique_ptr<Shader> m_PointLightShader;
		std::unique_ptr<Shader> m_LampCacheApplyShader;
		std::unique_ptr<Shader> m_OccluderSeedShader;
		std::unique_ptr<Shader> m_OccluderJumpShader;
		std::unique_ptr<Shader> m_SurfaceRoundShader;
		std::unique_ptr<Shader> m_CompositeShader;
		std::unique_ptr<Shader> m_EmissiveShader;
		std::unique_ptr<Shader> m_FireFlameShader; //!< The flames of burning ground, into the glow buffer (see LightingSettings::FireShader).
		float m_LastFlameTime[c_MaxScreens] = {-1.0F, -1.0F, -1.0F, -1.0F}; //!< Per player screen, effect time when the flames were last drawn there, for the embers' pace.
		std::unique_ptr<Shader> m_BloomDownsampleShader;
		std::unique_ptr<Shader> m_BloomUpsampleShader;
		std::unique_ptr<Shader> m_TonemapShader;
		std::unique_ptr<Shader> m_UnitOutlineRowShader; //!< The first half of the unit outlines' search (see LightingSettings::UnitOutline).
		std::unique_ptr<Shader> m_LitParticleShader;
		std::unique_ptr<Shader> m_SmokeScatterShader;
		std::unique_ptr<Shader> m_RCSceneShader;
		std::unique_ptr<Shader> m_RCCascadeShader;
		std::unique_ptr<Shader> m_RCIrradianceShader;
		std::unique_ptr<Shader> m_LuminanceShader;
		std::unique_ptr<Shader> m_ExposureAdaptShader;
		std::unique_ptr<Shader> m_ShockwaveShader;
		std::unique_ptr<Shader> m_PrecipitationShader;
		std::unique_ptr<Shader> m_RainSplashShader;
		std::unique_ptr<Shader> m_GodRaysShader;
		std::unique_ptr<Shader> m_DepthOfFieldShader;
		std::unique_ptr<Shader> m_GodRaysApplyShader;
		std::unique_ptr<Shader> m_ScorchShader;
		std::unique_ptr<Shader> m_StainShader;
		std::unique_ptr<Shader> m_DecalFadeShader;
		std::unique_ptr<Shader> m_DecalClearShader;
		std::unique_ptr<Shader> m_TerrainShader;
		GLuint m_EmptyVAO = 0; //!< For draws that generate their vertices from gl_VertexID.

		GLuint m_FullscreenVAO = 0;
		GLuint m_FullscreenVBO = 0;
		GLuint m_QuadVAO = 0;
		GLuint m_QuadVBO = 0;
		GLuint m_QuadIBO = 0;
		size_t m_QuadIndexCapacity = 0;
		size_t m_QuadVertexCapacity = 0; //!< How many vertices the quad vertex buffer has room for.
		std::vector<QuadVertex> m_QuadVertices;

		std::unordered_map<const BitmapTexture*, GlowInfo> m_GlowInfoCache;

		void LoadShaders();
		void CreateGeometry();

		/// Rebuilds the world grid if the scene changed. Returns false if there's no scene.
		bool EnsureWorldResources();
		void DestroyWorldResources();
		void EnsureScreenResources(int width, int height);
		void DestroyScreenResources();

		/// Builds the map of distances to solid objects for a player screen, which unit shadows and contact shading are worked out from.
		/// @param playerScreen The player screen, drawn but not lit yet.
		/// @param foregroundDepth Depth below which pixels are foreground terrain and objects.
		/// @return The texture holding, for each pixel, the position of the nearest pixel of a solid object (far away when none is within reach). 0 if it couldn't be built.
		GLuint BuildOccluderField(RenderTarget* playerScreen, float foregroundDepth);

		/// Recomputes occupancy for a range of grid rows from the terrain material layer.
		void RefreshOccupancyRows(int firstRow, int endRow, int firstColumn = 0, int endColumn = -1);
		void RecomputeSkyline();
		void UploadOccupancyRows(int firstRow, int endRow);

		/// Works out the terrain distance field (m_ShadowField) again around a rectangle of grid cells whose walls changed, and uploads what changed.
		/// Every cell within c_ShadowFieldCells of the rectangle is rewritten, from the walls within twice that.
		void RefreshShadowField(int firstColumn, int firstRow, int endColumn, int endRow);

		/// Sets the point light shader's terrain distance field uniforms and binds the field to unit 4. The shader must be enabled.
		void SetShadowFieldUniforms() const;
		void PropagateSkyLight(int iterations);
		void StampScorchMarks();
		void StampStains();

		/// Works out again which cells of the scorch and stain maps hold ground (m_DecalGround), over a box of the scene, and wipes the soot and stains off
		/// the cells in it with no ground left beside them. A mark then goes with the ground it was on: when that ground is dug or blown away and something
		/// else lands there later, the new ground comes in clean instead of showing the old blood, oil or soot.
		/// @param minX, minY, endX, endY The box, in scene pixels, end not included. Anything outside the scene takes its whole width or height, for the seams of wrapping scenes.
		/// @param wipeBare Whether to wipe the marks off cells left bare, or only work out the ground (when the maps are new and hold nothing).
		void RefreshDecalGround(int minX, int minY, int endX, int endY, bool wipeBare = true);

		/// Binds m_DecalGround to unit 1 for a stamping shader, which keeps its marks off cells with no ground beside them. The shader must be enabled.
		void BindDecalGround(const Shader& shader) const;

		/// Fades soot and stains away over time and washes them off in the rain, and dries wet stains (LightingSettings::DecalsFade).
		/// @param seconds Game seconds since the last call.
		void FadeDecals(float seconds);

		/// Steps the fog volume (LightingSettings::FogVolume) on by the game time since the last step: wind, spreading, clearing and its sources.
		void UpdateFog();

		/// Keeps the lamp cache (LightingSettings::LampCache) up to date: lights it in full when it's made or the steady lamps or the settings that shape them change,
		/// and relights only around where the ground changed otherwise. Lets it go when the setting is off.
		void UpdateLampCache();
		/// Steps the wetness map (LightingSettings::WetnessMap) on by game time: rain wets the ground and fills dips, and it dries after, rock slower than earth.
		/// @param seconds Game seconds since the last call.
		void UpdateWetMap(float seconds);

		/// Keeps the sun's shadow map (LightingSettings::SunShadowMap) up to date: remakes it when the ground changed or the sun has moved enough to shift a shadow at the
		/// bottom of the scene by a couple of pixels. Lets it go when it's off or can't be used (a scene that wraps vertically has no top for the sun to come in from).
		/// @param terrainChanged Whether the light grid's terrain changed this frame. @param changedArea Where, in scene pixels: min x, min y, end x, end y.
		void UpdateSunShadowMap(bool terrainChanged, const glm::ivec4& changedArea);

		/// Keeps the weather's shelter map (LightingSettings::ShelterMask) up to date, the same way as the sun's: remade when the ground changed or the wind or the weather has
		/// turned the way it falls enough to move a shelter's edge. Made while rain, snow or ash is falling or wetness or snow is left on the ground, and only for weather falling
		/// no flatter than c_ShelterMaxSlope; otherwise what uses it marches the light grid as before.
		/// @param terrainChanged Whether the ground changed this frame. @param changedArea The scene area it changed in, x y to z w.
		void UpdateShelterMap(bool terrainChanged, const glm::ivec4& changedArea);

		/// Marches the rays of a strip like the sun's shadow map through the light grid: the whole strip when it's new or its slope has moved, or else just the rays through
		/// the area where the ground changed.
		/// @param map The strip. @param mapSlope, mapStart, mapTexel, ready Its layout as last made, and whether it's in use: updated here.
		/// @param slope How far its rays move in x per pixel down. @param channel Which channel of the light grid stops them. @param threshold From how full a cell they stop.
		/// @param terrainChanged Whether the ground changed this frame. @param changedArea The scene area it changed in, x y to z w.
		void MarchCoverStrip(GLTarget& map, float& mapSlope, float& mapStart, float& mapTexel, bool& ready, float slope, int channel, float threshold, bool terrainChanged, const glm::ivec4& changedArea);

		/// Sets the shelter map's uniforms on a shader that reads it (Precipitation.vert, RainSplash.frag, Terrain.frag).
		/// @param shader The shader, enabled. @param use Whether it should use the map. @param unit The texture unit the map is bound to.
		void SetShelterUniforms(const Shader& shader, bool use, int unit) const;

		/// The way rain, snow or ash falls on average, in pixels per second (y down), for the shelter map and the ground's weather cover.
		glm::vec2 WeatherFall() const;

		/// Brings the flow field (m_FlowTexture) up to date with the liquid moving this frame, clearing and uploading only the tiles that had or have moving liquid in them.
		void UpdateFlowField();

		const GlowInfo& GetGlowInfo(const BitmapTexture* glowTexture);

		void DrawFullscreen() const;
		/// Uploads m_QuadVertices and draws them as quads.
		void DrawQuads(size_t firstQuad, size_t quadCount);
		void UploadQuads();
	};
} // namespace RTE
