#pragma once
#include "Rectangles.h"
#include <memory>
#include "glad/gl.h"
#include "raylib/raylib.h"
namespace RTE {
	class Shader;
	class Texture;
	class BitmapTexture;
	class DepthTexture;
	class RenderTarget {
	public:

		/// @brief RenderTarget constructor
		/// @param size Size of render target in pixels
		/// @param defaultViewport The default viewport to set up at Begin().
		/// @param bitDepth The color depth of this target, either 8 or 32 (default 32)
		/// @param colorTexture Optional color texture, if specified, bitDepth will be ignored.
		/// @param defaultFB0 Make this target for the window backbuffer.
		RenderTarget(const FloatRect& size, const FloatRect& defaultViewport, int bitDepth = 32, std::shared_ptr<BitmapTexture> colorTexture = nullptr, bool defaultFB0 = false);


		RenderTarget(bool defaultFB0);
		/// Destructor.
		virtual ~RenderTarget();

		/// Enables this RenderTarget for drawing and sets up projection matrix. Draws current batch, resets draw depth and model matrix.
		/// @param clear Whether to clear the target.
		void Begin(bool clear = true, bool drawBatch = true);

		/// Disables this RenderTarget and flushes the active batch.
		void End(bool drawBatch = true);

		void Bind();

		/// @brief Getter for the FBO
		/// @return The FBO.
		GLuint GetFramebuffer() { return m_FBO; }
		/// Getter for the color buffer
		std::weak_ptr<Texture> GetColorTexture() { return m_Texture; }
		/// Getter for the depth buffer.
		std::weak_ptr<DepthTexture> GetDepthTexture() { return m_Depth; }

		/// Adds the color attachments the lighting reads: screen space normals (location 1) and surface values (location 2).
		void EnableNormalAttachment();

		/// Getter for the normal buffer, if enabled. RG = normal xy * 0.5 + 0.5, B = 1 - shininess, A = 0 where nothing was drawn, else 0.5..1 with the emissive strength.
		std::weak_ptr<Texture> GetNormalTexture() { return m_Normal; }

		/// Getter for the surface buffer, if enabled. R = how metallic, G = how glossy, B = 1 for solid objects that cast shadows.
		std::weak_ptr<Texture> GetSurfaceTexture() { return m_Surface; }

		/// Getter for the size of this target.
		const FloatRect& GetSize() { return m_Size; }

	protected:
		FloatRect m_Viewport{};
		FloatRect m_Size{};
		GLuint m_FBO{0};

	private:
		std::shared_ptr<Texture> m_Texture{nullptr};
		std::shared_ptr<DepthTexture> m_Depth{nullptr};
		std::shared_ptr<Texture> m_Normal{nullptr};
		std::shared_ptr<Texture> m_Surface{nullptr};
	};
} // namespace RTE
