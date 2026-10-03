#pragma once
#include "glm/fwd.hpp"
#include "Rectangles.h"
#include "Color.h"
#include "glad/gl.h"
#include <memory>
#include "DrawCall.h"
#include "AllegroTools.h"

extern "C" {
typedef struct BITMAP BITMAP;
}

namespace RTE {
	enum class Filter {
		Linear,
		LinearMipmap,
		Nearest
	};

	enum class WrapType {
		ClampToBorder,
		ClampToEdge,
		Repeat
	};

	/// Abstraction for rectangular textures.
	class Texture {
	public:
		Texture();
		Texture(GLuint textureId);
		Texture(FloatRect dimensions, Filter filtering = Filter::Linear, WrapType wrap = WrapType::ClampToEdge, int bitDepth = 32);
		virtual ~Texture();
		void Bind();
		GLuint GetTextureId() const { return m_TextureID; }
		const FloatRect& GetDimensions() const { return m_Dimensions; }
		int GetBitDepth() { return m_BitDepth; }

		/// Gets where this texture's pixels are inside its GL texture, in normalized coordinates. The whole texture unless it lives in an atlas.
		const FloatRect& GetUVRect() const { return m_UVRect; }

		/// Maps normalized coordinates within this texture to coordinates in its GL texture.
		FloatRect MapUV(const FloatRect& uv) const { return FloatRect(m_UVRect.x + uv.x * m_UVRect.w, m_UVRect.y + uv.y * m_UVRect.h, uv.w * m_UVRect.w, uv.h * m_UVRect.h); }

	protected:
		GLuint m_TextureID{0};
		int m_BitDepth{32};
		FloatRect m_Dimensions;
		FloatRect m_UVRect{0.0f, 0.0f, 1.0f, 1.0f}; //!< Where this texture's pixels are inside m_TextureID.
		bool m_OwnsTexture{true}; //!< False when m_TextureID is a shared atlas page.
	};

	class BitmapTexture : public Texture {
	public:
		/// Constructs a texture from an allegro BITMAP.
		/// @param bitmap Unique pointer to the BITMAP representing this texture's pixels.
		/// @param filtering A Filter mode which is applied to the GL texture. Always set to Nearest for indexed images.
		/// @param clamp The texture wrapping mode.
		BitmapTexture(std::unique_ptr<BITMAP, BitmapDeleter> bitmap, Filter filtering = Filter::Linear, WrapType clamp = WrapType::ClampToEdge);

		/// Destructor.
		~BitmapTexture() = default;

		/// Returns the pixels of this BitmapTexture.
		/// If the bitmap is modified, call Update to update the texture on the GPU as well.
		/// @return (Non owning) Bitmap to the pixels.
		BITMAP* GetBitmap() const { return m_Pixels.get(); }

		/// Update the pixels on GPU.
		void Update();
		void Update(const FloatRect& region);

		/// Moves a small 8-bit texture into a shared sprite atlas page, so consecutive sprites can be drawn in one GL draw.
		/// Its pixels are copied there and its own GL texture is freed. Does nothing for textures that aren't eligible.
		/// @return Whether the texture is now in the atlas.
		bool MoveToAtlas();

		/// Gets how many atlas pages exist and how many textures they hold, for statistics.
		static void GetAtlasStats(int& pageCount, int& textureCount);

	private:
		std::unique_ptr<BITMAP, BitmapDeleter> m_Pixels;
		bool m_HaveAlpha;

	public:
		// explicit operator BITMAP*() { return m_Pixels.get(); }
	};

	class DepthTexture : public Texture {
	public:
		DepthTexture(FloatRect dimensions);
		~DepthTexture() = default;
	};
} // namespace RTE
