#pragma once
#include "Singleton.h"
#include "RenderBatch.h"
#include "GLState.h"
#include "DrawCall.h"
#include "Texture.h"
#include <memory>
#include <array>
#include <vector>

#define g_RenderMan RenderMan::Instance()

namespace RTE {
	class DrawCall;
	class RenderMan: public Singleton<RenderMan> {
	public:
		/// Constructor
		RenderMan() = default;
		/// Destructor
		~RenderMan() { Destroy(); }

		/// Initializes this RenderMan.
		void Initialize();

		/// Destroys this RenderMan.
		void Destroy();

		/// Returns the active RenderBatch.
		/// @return The active RenderBatch.
		RenderBatch* GetActiveBatch() { return m_ActiveBatch; }
		void SetActiveBatch(RenderBatch* batch) { m_ActiveBatch = batch; }
		void ResetActiveBatch() { m_ActiveBatch = m_RenderBatch.get(); }

		/// Returns the current RenderDepth to be set on new Vertices.
		/// @return Depth value to use for the current draw call.
		float GetCurrentDepth() const { return m_ActiveBatch->m_CurrentDepth; }

		/// Returns the current depth offset to be added to the current depth.
		/// @return Depth offset to be added to the vertex or transform.
		float GetCurrentZOffset() const { return m_ActiveBatch->m_CurrentZ; }

		/// Set the current depth offset, this can be used to change draw order without sorting.
		/// @param depth The new offset.
		void SetCurrentZOffset(float depth) { m_ActiveBatch->m_CurrentZ = depth; }

		/// Sets the surface that upcoming draws are marked with, for the lighting: R how metallic, G how glossy, B whether it's a solid object that casts shadows (0 or 255).
		/// Set it back to zero after drawing an object, so terrain, particles and effects stay unmarked.
		/// @param surface The surface values.
		void SetCurrentSurface(glm::u8vec4 surface) {
			if (!m_ActiveBatch->m_ShadowCasting) {
				surface.b = 0;
			}
			m_ActiveBatch->m_CurrentSurface = surface;
		}

		/// Sets whether upcoming surfaces may be marked as casting shadows. Turn it off around parts of an object that are light, not matter (muzzle flashes), and back on afterwards.
		/// @param shadowCasting Whether shadows may be cast.
		void SetShadowCasting(bool shadowCasting) { m_ActiveBatch->m_ShadowCasting = shadowCasting; }

		/// Set the default shader for upcoming draw calls.
		/// @param shader (non owning) pointer to the new shader.
		void SetCurrentShader(const Shader* shader) { m_ActiveBatch->m_CurrentShader = shader; }

		const Shader* GetCurrentShader() { return m_ActiveBatch->m_CurrentShader ? m_ActiveBatch->m_CurrentShader : GetDefaultShader(); }

		void SetActiveBlendMode(BlendMode mode) { m_ActiveBatch->m_CurrentBlendMode = std::move(mode); }

		/// Add a uniform value to be set on upcoming draw calls.
		/// @param uniform The unform value to enable.
		void PushUniform(std::shared_ptr<UniformValueType> uniform) { m_ActiveBatch->m_CurrentUniforms.push_back(uniform); }

		/// Clears active uniforms.
		void ClearUniforms() { m_ActiveBatch->m_CurrentUniforms.clear(); }

		/// Schedules a new draw call on the current batch, initialized with the current shader, uniforms and camera.
		std::shared_ptr<DrawCall> BeginDraw();

		/// Returns the shape texture id (1x1 white pixel texture).
		/// @return GL texture object.
		GLuint GetShapeTexture() { return m_ShapesTexture->GetTextureId(); }

		/// Returns the texture id for the default palette. TODO: allow palette swapping.
		GLuint GetPaletteTexture() { return m_PaletteTexture->GetTextureId(); }

		/// Returns the texture marking which palette colors glow (256x1, R = emissive strength), so indexed art can be emissive without new assets.
		GLuint GetEmissivePaletteTexture() const { return m_EmissivePaletteTexture; }

		/// Marks a palette color as a liquid (for the terrain shader's water, lava and acid looks) and optionally as emissive.
		/// @param paletteIndex The palette color.
		/// @param liquidKind 0 none, 1 water, 2 lava, 3 acid.
		/// @param emissive How strongly the color glows, 0 to 255.
		void SetLiquidPaletteColor(int paletteIndex, int liquidKind, int emissive);

		/// Makes a palette colour's glow pulse between two strengths (animated palette flags, LightingSettings::PaletteAnimation).
		/// @param paletteIndex The colour, 1 to 255. @param low The glow at the bottom of the pulse, 0 to 1. @param high The glow at the top.
		/// @param period Seconds per pulse; 0 or less removes the colour's pulse. @param phase Where in the pulse it starts, 0 to 1.
		void SetPalettePulse(int paletteIndex, float low, float high, float period, float phase) { SetPalettePulse(paletteIndex, low, high, period, phase, false); }

		/// Rotates a run of palette colours through each other, the classic palette cycle for water or energy.
		/// @param from The first colour of the run. @param to The last. @param period Seconds for a whole turn; 0 or less removes the cycle on that run.
		void SetPaletteCycle(int from, int to, float period);

		/// Removes every pulse and cycle that was asked for. Liquids' own glow pulses (lava) stay.
		void ClearPaletteAnimation();

		/// Writes this frame's pulsing glows and cycled colours into the palette textures, or puts the palette back as it was when animation stops.
		/// @param time Seconds, for the pulses and cycles. @param enabled Whether palette animation is on. @param strength 0 to 1, how far pulses swing from the colour's own glow.
		void UpdatePaletteAnimation(float time, bool enabled, float strength);

		/// Sets a texture that's bound for every batch render, for shaders that sample world space maps. Units 3 to 5.
		void SetGlobalTexture(int unit, GLuint texture) {
			if (unit >= 3 && unit < 3 + static_cast<int>(m_GlobalTextures.size())) {
				m_GlobalTextures[unit - 3] = texture;
			}
		}

		/// Gets the textures to bind to units 3 and up for every batch render.
		const std::array<GLuint, 4>& GetGlobalTextures() const { return m_GlobalTextures; }

		/// Returns the default shader.
		const Shader* GetDefaultShader() { return m_DefaultShader.get(); }

		/// Returns the camera the was set at BeginFrame.
		const Camera* GetActiveCamera() { return m_ActiveBatch->m_CurrentCamera; }

		/// Begin a set of draws with camera. Clears batched draw calls.
		void BeginFrame(const Camera* camera = nullptr);

		/// Draws current draw calls to the active target and clears the batch for new draws.
		void DrawActiveBatch();
	private:
		RenderBatch* m_ActiveBatch;
		std::unique_ptr<RenderBatch> m_RenderBatch{nullptr};
		std::unique_ptr<GLState> m_GLState{nullptr};
		std::shared_ptr<BitmapTexture> m_ShapesTexture{nullptr};
		std::shared_ptr<BitmapTexture> m_PaletteTexture{nullptr};
		GLuint m_EmissivePaletteTexture{0};
		std::array<unsigned char, 1024> m_EmissivePalette{}; //!< RGBA per palette color: R emissive, G vegetation, B liquid kind.

		/// A palette colour whose glow pulses.
		struct PalettePulse {
			int Index = 0;
			float Low = 0.0F;
			float High = 0.0F;
			float Period = 1.0F;
			float Phase = 0.0F;
			bool Automatic = false; //!< Set by SetLiquidPaletteColor for a glowing liquid, kept by ClearPaletteAnimation.
		};
		/// A run of palette colours rotated through each other.
		struct PaletteCycle {
			int From = 0;
			int To = 0;
			float Period = 1.0F;
		};
		std::vector<PalettePulse> m_PalettePulses;
		std::vector<PaletteCycle> m_PaletteCycles;
		std::array<unsigned char, 1024> m_PaletteColors{}; //!< The palette texture's own colours, RGBA, read back once, for cycling from and putting back.
		bool m_PaletteColorsRead = false;
		bool m_PaletteAnimated = false; //!< The textures hold animated values, to be put back when animation stops.

		void SetPalettePulse(int paletteIndex, float low, float high, float period, float phase, bool automatic);
		std::array<GLuint, 4> m_GlobalTextures{};
		std::shared_ptr<Shader> m_DefaultShader{nullptr};
		Camera m_DefaultCamera{{-1.0f, -1.0f}, {{0.0f, 0.0f}, {2.0f, 2.0f}}};
	};
}
