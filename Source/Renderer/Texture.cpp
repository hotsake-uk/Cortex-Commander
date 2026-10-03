#include "Texture.h"
#include "allegro.h"
#include "GLCheck.h"
#include "glad/gl.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace RTE;

Texture::Texture() : m_Dimensions() {
	glGenTextures(1, &m_TextureID);
}

Texture::~Texture() {
	if (m_OwnsTexture) {
		glDeleteTextures(1, &m_TextureID);
	}
}

Texture::Texture(GLuint textureId) : m_TextureID(textureId), m_Dimensions() {
	glBindTexture(GL_TEXTURE_2D, m_TextureID);
	glGetTexLevelParameterfv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &m_Dimensions.w);
	glGetTexLevelParameterfv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &m_Dimensions.h);
	glBindTexture(GL_TEXTURE_2D, 0);
}

Texture::Texture(FloatRect dimensions, Filter filtering, WrapType wrapType, int bitDepth) : m_BitDepth(bitDepth), m_Dimensions(std::move(dimensions)) {
	glGenTextures(1, &m_TextureID);

	Bind();

	if (bitDepth == 8) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_Dimensions.w, m_Dimensions.h, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
		GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_ONE};
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
	} else {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_Dimensions.w, m_Dimensions.h, 0, bitDepth == 32 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	}

	GLint wrap;
	switch (wrapType) {
		case WrapType::ClampToBorder:
			wrap = GL_CLAMP_TO_BORDER;
			break;
		case WrapType::ClampToEdge:
			wrap = GL_CLAMP_TO_EDGE;
			break;
		case RTE::WrapType::Repeat:
			wrap = GL_REPEAT;
			break;
	}

	GLint textureMagFilter;
	GLint textureMinFilter;
	switch (filtering) {
		case Filter::Linear:
			textureMagFilter = GL_LINEAR;
			textureMinFilter = GL_LINEAR;
			break;
		case Filter::LinearMipmap:
			textureMagFilter = GL_LINEAR;
			textureMinFilter = GL_LINEAR_MIPMAP_LINEAR;
			break;
		case Filter::Nearest:
			textureMagFilter = GL_NEAREST;
			textureMinFilter = GL_NEAREST;
			break;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, textureMagFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, textureMinFilter);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::Bind() {
	glBindTexture(GL_TEXTURE_2D, m_TextureID);
}

BitmapTexture::BitmapTexture(std::unique_ptr<BITMAP, BitmapDeleter> bitmap, Filter filtering, WrapType clamp) : Texture(), m_Pixels(std::move(bitmap)), m_HaveAlpha(false) {
	m_Dimensions = FloatRect(0.0f, 0.0f, m_Pixels->w, m_Pixels->h);
	Bind();

	m_BitDepth = bitmap_color_depth(m_Pixels.get());
	if (m_BitDepth == 8) {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_Pixels->w, m_Pixels->h, 0, GL_RED, GL_UNSIGNED_BYTE, m_Pixels->dat);
		GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_ONE};
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
		filtering = Filter::Nearest;
	} else {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_Pixels->w, m_Pixels->h, 0, m_BitDepth == 32 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, m_Pixels->dat);
	}

	GLint wrap;
	switch (clamp) {
		case WrapType::ClampToBorder:
			wrap = GL_CLAMP_TO_BORDER;
			break;
		case WrapType::ClampToEdge:
			wrap = GL_CLAMP_TO_EDGE;
			break;
		case RTE::WrapType::Repeat:
			wrap = GL_REPEAT;
			break;
	}

	GLint textureMagFilter;
	GLint textureMinFilter;
	switch (filtering) {
		case Filter::Linear:
			textureMagFilter = GL_LINEAR;
			textureMinFilter = GL_LINEAR;
			break;
		case Filter::LinearMipmap:
			textureMagFilter = GL_LINEAR;
			textureMinFilter = GL_LINEAR_MIPMAP_LINEAR;
			break;
		case Filter::Nearest:
			textureMagFilter = GL_NEAREST;
			textureMinFilter = GL_NEAREST;
			break;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, textureMagFilter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, textureMinFilter);

	if (filtering == Filter::LinearMipmap) {
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}

namespace {
	/// A sprite atlas page: an 8-bit texture filled with small sprites by simple shelf packing.
	struct AtlasPage {
		GLuint Texture = 0;
		int ShelfX = 0; //!< Where the next sprite goes on the current shelf.
		int ShelfY = 0; //!< Top of the current shelf.
		int ShelfHeight = 0; //!< Height of the tallest sprite on the current shelf.
	};
	constexpr int c_AtlasPageSize = 2048;
	constexpr int c_AtlasMaxSpriteSize = 256; //!< Larger sprites keep their own texture.
	// The sprite shader looks up to 2 texels around each pixel for edge normals, so keep sprites apart by that much transparency.
	constexpr int c_AtlasPadding = 2;
	std::vector<AtlasPage> s_AtlasPages;
	int s_AtlasTextureCount = 0;

	GLuint CreateAtlasPage() {
		GLuint texture = 0;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		// Zero is the transparent palette index, so empty space and padding are see-through.
		std::vector<unsigned char> zeros(static_cast<size_t>(c_AtlasPageSize) * c_AtlasPageSize, 0);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, c_AtlasPageSize, c_AtlasPageSize, 0, GL_RED, GL_UNSIGNED_BYTE, zeros.data());
		GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_ONE};
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		return texture;
	}

	/// Finds room for a w x h block (padding included) and returns the page index and top left corner.
	bool AllocateAtlasSpace(int width, int height, int& pageIndex, int& x, int& y) {
		for (size_t attempt = 0; attempt < 2; ++attempt) {
			if (!s_AtlasPages.empty()) {
				AtlasPage& page = s_AtlasPages.back();
				if (page.ShelfX + width > c_AtlasPageSize) {
					// Start a new shelf below the current one.
					page.ShelfY += page.ShelfHeight;
					page.ShelfX = 0;
					page.ShelfHeight = 0;
				}
				if (page.ShelfY + height <= c_AtlasPageSize && page.ShelfX + width <= c_AtlasPageSize) {
					pageIndex = static_cast<int>(s_AtlasPages.size()) - 1;
					x = page.ShelfX;
					y = page.ShelfY;
					page.ShelfX += width;
					page.ShelfHeight = std::max(page.ShelfHeight, height);
					return true;
				}
			}
			s_AtlasPages.push_back({CreateAtlasPage()});
		}
		return false;
	}
} // namespace

bool BitmapTexture::MoveToAtlas() {
	if (!m_OwnsTexture) {
		return true;
	}
	BITMAP* bitmap = m_Pixels.get();
	if (!bitmap || bitmap_color_depth(bitmap) != 8 || bitmap->w > c_AtlasMaxSpriteSize || bitmap->h > c_AtlasMaxSpriteSize || bitmap->w < 1 || bitmap->h < 1) {
		return false;
	}
	int pageIndex = 0;
	int x = 0;
	int y = 0;
	if (!AllocateAtlasSpace(bitmap->w + c_AtlasPadding * 2, bitmap->h + c_AtlasPadding * 2, pageIndex, x, y)) {
		return false;
	}
	x += c_AtlasPadding;
	y += c_AtlasPadding;
	glBindTexture(GL_TEXTURE_2D, s_AtlasPages[pageIndex].Texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (int row = 0; row < bitmap->h; ++row) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, x, y + row, bitmap->w, 1, GL_RED, GL_UNSIGNED_BYTE, bitmap->line[row]);
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	glDeleteTextures(1, &m_TextureID);
	m_TextureID = s_AtlasPages[pageIndex].Texture;
	m_OwnsTexture = false;
	float pageSize = static_cast<float>(c_AtlasPageSize);
	m_UVRect = FloatRect(static_cast<float>(x) / pageSize, static_cast<float>(y) / pageSize, static_cast<float>(bitmap->w) / pageSize, static_cast<float>(bitmap->h) / pageSize);
	++s_AtlasTextureCount;
	return true;
}

void BitmapTexture::GetAtlasStats(int& pageCount, int& textureCount) {
	pageCount = static_cast<int>(s_AtlasPages.size());
	textureCount = s_AtlasTextureCount;
}

void BitmapTexture::Update() {
	if (!m_OwnsTexture) {
		Update(FloatRect(0.0f, 0.0f, m_Dimensions.w, m_Dimensions.h));
		return;
	}
	Bind();
	if (bitmap_color_depth(m_Pixels.get()) == 8) {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_Pixels->w, m_Pixels->h, 0, GL_RED, GL_UNSIGNED_BYTE, m_Pixels->dat);
	} else {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_Pixels->w, m_Pixels->h, 0, m_BitDepth == 32 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, m_Pixels->dat);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
}

void BitmapTexture::Update(const FloatRect& region) {
	RTEAssert((region.x >= 0) && (region.y >= 0) && (region.x + region.w) <= m_Dimensions.w && (region.y + region.h) <= m_Dimensions.h, "Update area out of BITMAP bounds!");
	Bind();
	if (!m_OwnsTexture) {
		// In an atlas: write into this texture's part of the page.
		int offsetX = static_cast<int>(std::lround(m_UVRect.x * c_AtlasPageSize));
		int offsetY = static_cast<int>(std::lround(m_UVRect.y * c_AtlasPageSize));
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		for (int row = static_cast<int>(region.y); row < static_cast<int>(region.y + region.h); ++row) {
			glTexSubImage2D(GL_TEXTURE_2D, 0, offsetX + static_cast<int>(region.x), offsetY + row, static_cast<int>(region.w), 1, GL_RED, GL_UNSIGNED_BYTE, m_Pixels->line[row] + static_cast<int>(region.x));
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		return;
	}
	int bitDepth = bitmap_color_depth(m_Pixels.get());
	glPixelStorei(GL_UNPACK_ROW_LENGTH, m_Dimensions.w);
	if (bitDepth == 8) {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, region.x, region.y, region.w, region.h, GL_RED, GL_UNSIGNED_BYTE, m_Pixels->line[static_cast<int>(region.y)] + static_cast<int>(region.x));
	} else {
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexSubImage2D(GL_TEXTURE_2D, 0, region.x, region.y, region.w, region.h, GL_RGBA, GL_UNSIGNED_BYTE, m_Pixels->line[static_cast<int>(region.y)] + (static_cast<int>(region.x) * 4));
	}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

DepthTexture::DepthTexture(FloatRect dimensions) : Texture(std::move(dimensions)) {
	Bind();

	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, m_Dimensions.w, m_Dimensions.h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
