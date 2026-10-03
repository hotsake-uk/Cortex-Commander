#include "BigTexture.h"
#include "AllegroTools.h"
#include "Draw.h"
#include "GLStateMan.h"
#include "DebugMan.h"

#include "glad/gl.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "tracy/Tracy.hpp"
#include "tracy/TracyOpenGL.hpp"

using namespace RTE;
int BigTexture::s_MaxGLTextureSize{0};

BigTexture::BigTexture(BITMAP* bitmap) {
	if (!s_MaxGLTextureSize) {
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &s_MaxGLTextureSize);
	}
	int bitsPerPixel = bitmap_color_depth(bitmap);
	int bytesPerPixel = bitsPerPixel / 8;
	m_Bitmap = bitmap;
	m_Width = bitmap->w;
	m_Height = bitmap->h;

	int height = bitmap->h;
	for (int y = 0; y < bitmap->h; y += s_MaxGLTextureSize) {
		int width = bitmap->w;
		for (int x = 0; x < bitmap->w; x += s_MaxGLTextureSize) {
			int regionWidth = std::min(width, s_MaxGLTextureSize);
			int regionHeight = std::min(height, s_MaxGLTextureSize);
			m_Regions.emplace_back(
				Vector(x, y),
				regionWidth,
				regionHeight);
			std::unique_ptr <BITMAP, BitmapDeleter> targetRegionPixels = std::unique_ptr<BITMAP, BitmapDeleter>(create_bitmap_ex(bitmap_color_depth(bitmap), regionWidth, regionHeight));
			m_Textures.emplace_back(std::make_shared<BitmapTexture>(std::move(targetRegionPixels), Filter::Nearest));
			GLuint uploadBuffer;
			glGenBuffers(1, &uploadBuffer);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, uploadBuffer);
			glBufferData(GL_PIXEL_UNPACK_BUFFER, regionWidth * regionHeight * bytesPerPixel + 1, NULL, GL_STREAM_DRAW);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
			m_UploadBuffers.emplace_back(uploadBuffer);
			width -= s_MaxGLTextureSize;
		}
		height -= s_MaxGLTextureSize;
	}
}


void BigTexture::UpdateChanged(const Box& updateRegion) {
	ZoneScoped;
	int bytesPerPixel = bitmap_color_depth(m_Bitmap) / 8;
	size_t rowBytes = static_cast<size_t>(m_Width) * bytesPerPixel;
	if (m_MirroredBitmap != m_Bitmap || m_Mirror.size() != rowBytes * m_Height) {
		// Start from a known state: upload everything once and remember it.
		Update(Box(Vector(), static_cast<float>(m_Width), static_cast<float>(m_Height)));
		m_Mirror.resize(rowBytes * m_Height);
		for (int y = 0; y < m_Height; ++y) {
			std::memcpy(m_Mirror.data() + y * rowBytes, m_Bitmap->line[y], rowBytes);
		}
		m_MirroredBitmap = m_Bitmap;
		return;
	}

	int left = std::clamp(updateRegion.m_Corner.GetFloorIntX(), 0, m_Width);
	int top = std::clamp(updateRegion.m_Corner.GetFloorIntY(), 0, m_Height);
	int right = std::clamp(static_cast<int>(std::ceil(updateRegion.m_Corner.m_X + updateRegion.m_Width)), 0, m_Width);
	int bottom = std::clamp(static_cast<int>(std::ceil(updateRegion.m_Corner.m_Y + updateRegion.m_Height)), 0, m_Height);
	if (left >= right || top >= bottom) {
		return;
	}
	size_t spanBytes = static_cast<size_t>(right - left) * bytesPerPixel;
	size_t spanOffset = static_cast<size_t>(left) * bytesPerPixel;

	// Bands of rows, each uploading the horizontal extent of its changes, so a few scattered edits don't re-upload the whole view.
	constexpr int bandHeight = 32;
	for (int bandTop = top; bandTop < bottom; bandTop += bandHeight) {
		int bandBottom = std::min(bandTop + bandHeight, bottom);
		size_t changedFirst = spanBytes;
		size_t changedLast = 0;
		int changedTop = -1;
		int changedBottom = -1;
		for (int y = bandTop; y < bandBottom; ++y) {
			const unsigned char* current = m_Bitmap->line[y] + spanOffset;
			const unsigned char* mirrored = m_Mirror.data() + y * rowBytes + spanOffset;
			if (std::memcmp(current, mirrored, spanBytes) == 0) {
				continue;
			}
			size_t first = 0;
			while (current[first] == mirrored[first]) {
				++first;
			}
			size_t last = spanBytes - 1;
			while (current[last] == mirrored[last]) {
				--last;
			}
			changedFirst = std::min(changedFirst, first);
			changedLast = std::max(changedLast, last);
			if (changedTop < 0) {
				changedTop = y;
			}
			changedBottom = y + 1;
		}
		if (changedTop < 0) {
			continue;
		}
		int changedLeft = left + static_cast<int>(changedFirst / bytesPerPixel);
		int changedRight = left + static_cast<int>(changedLast / bytesPerPixel) + 1;
		Update(Box(Vector(static_cast<float>(changedLeft), static_cast<float>(changedTop)), static_cast<float>(changedRight - changedLeft), static_cast<float>(changedBottom - changedTop)));
		size_t copyOffset = static_cast<size_t>(changedLeft) * bytesPerPixel;
		size_t copyBytes = static_cast<size_t>(changedRight - changedLeft) * bytesPerPixel;
		for (int y = changedTop; y < changedBottom; ++y) {
			std::memcpy(m_Mirror.data() + y * rowBytes + copyOffset, m_Bitmap->line[y] + copyOffset, copyBytes);
		}
	}
}

void BigTexture::Draw(const Box& source, const Box& dest) {
	ZoneScoped;
	TracyGpuZone("BigTexture::Draw");
	float scaleX = dest.m_Width / source.m_Width;
	float scaleY = dest.m_Height / source.m_Height;
	for (int i = 0; i < m_Regions.size(); ++i){
		Box sourceIntersect = source.GetIntersection(m_Regions[i]);
		if (!sourceIntersect.IsEmpty()) {
			Draw::DrawTexture(m_Textures[i].get(), sourceIntersect, Box(dest.m_Corner + Vector(sourceIntersect.m_Corner.m_X * scaleX, sourceIntersect.m_Corner.m_Y * scaleY), sourceIntersect.m_Width * scaleX, sourceIntersect.m_Height * scaleY));
		}
	}
}

void BigTexture::Update(const Box& updateRegion) {
	ZoneScoped;
	TracyGpuZone("BigTexture Upload");
	int bytesPerPixel = bitmap_color_depth(m_Bitmap) / 8;
	glPixelStorei(GL_UNPACK_ALIGNMENT, bitmap_color_depth(m_Bitmap) == 8 ? 1 : 4);
	for (int i = 0; i < m_Regions.size(); ++i) {
		Box intersect = updateRegion.GetIntersection(m_Regions[i]);
		if (!intersect.IsEmpty()) {
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_UploadBuffers[i]);
			size_t pixelsSize = std::ceil(intersect.m_Width) * std::ceil(intersect.m_Height) * bytesPerPixel;
			// The region is packed tightly from the start of the buffer, which is invalidated (orphaned) every upload anyway.
			unsigned char* pixels = (unsigned char*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, pixelsSize, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
			if (!pixels) {
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
				continue;
			}

			for (size_t y = 0; y < static_cast<int>(std::ceil(intersect.m_Height)); y++) {
				memcpy(
				    pixels + y * static_cast<int>(std::ceil(intersect.m_Width)) * bytesPerPixel,
				    m_Bitmap->line[y + intersect.m_Corner.GetFloorIntY()] + intersect.m_Corner.GetFloorIntX() * bytesPerPixel,
				    std::ceil(intersect.m_Width) * bytesPerPixel);
			}
			glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);


			glBindTexture(GL_TEXTURE_2D, m_Textures[i]->GetTextureId());
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			assert(m_Textures[i]->GetDimensions().w >= intersect.m_Width);
			assert(m_Textures[i]->GetDimensions().h >= intersect.m_Height);
			glTexSubImage2D(
			    GL_TEXTURE_2D,
			    0,
			    intersect.m_Corner.GetFloorIntX() % s_MaxGLTextureSize,
			    intersect.m_Corner.GetFloorIntY() % s_MaxGLTextureSize,
			    std::ceil(intersect.m_Width),
			    std::ceil(intersect.m_Height),
			    bytesPerPixel == 1 ? GL_RED : GL_RGBA,
			    GL_UNSIGNED_BYTE,
			    nullptr);

			glBindTexture(GL_TEXTURE_2D, 0);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}
	}
}
