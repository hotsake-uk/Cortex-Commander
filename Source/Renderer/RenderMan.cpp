#include "RenderMan.h"
#include "Constants.h"
#include <array>
#include "DrawCall.h"
#include "allegro.h"
#include "glad/gl.h"
#include "SDL3/SDL.h"

#include "FrameMan.h"
#include "tracy/Tracy.hpp"

using namespace RTE;

void RenderMan::Initialize() {
	std::unique_ptr<BITMAP, BitmapDeleter> shapesBitmap = std::unique_ptr<BITMAP, BitmapDeleter>(create_bitmap_ex(32, 1, 1));
	clear_to_color(shapesBitmap.get(), makeacol32(255, 255, 255, 255));
	m_ShapesTexture = std::make_unique<BitmapTexture>(std::move(shapesBitmap));
	m_RenderBatch = std::make_unique<RenderBatch>();
	m_ActiveBatch = m_RenderBatch.get();
	m_DefaultShader = std::make_unique<Shader>("Base.rte/Shaders/Blit8.vert", "Base.rte/Shaders/Blit8.frag");
	std::unique_ptr<BITMAP, BitmapDeleter> paletteBitmap = std::unique_ptr<BITMAP, BitmapDeleter>(create_bitmap_ex(32, 256, 1));
	SDL_Palette* palette = ContentFile::DefaultPaletteToSDL(true);
	const PALETTE& pal = g_FrameMan.GetDefaultPalette();
	for (int i = 0; i < 256; ++i) {
		SDL_Color color = palette->colors[i];
		_putpixel32(paletteBitmap.get(), i, 0, makeacol32(color.r, color.g, color.b, color.a));
	}
	m_PaletteTexture = std::make_shared<BitmapTexture>(std::move(paletteBitmap), Filter::Nearest, WrapType::ClampToEdge);

	// The palette's glow yellows (the colors the original dot glows keyed on: gold sparkle, tracers, hot bits) are emissive, so they glint in the dark.
	// R = emissive strength, G = vegetation (green-dominant colors, which sway in the wind when they're part of the terrain).
	std::array<unsigned char, 512> emissivePalette{};
	emissivePalette[g_YellowGlowColor * 2] = 255;
	emissivePalette[98 * 2] = 255;
	emissivePalette[120 * 2] = 200;
	for (int i = 1; i < 256; ++i) {
		SDL_Color color = palette->colors[i];
		int green = color.g;
		int otherMax = std::max<int>(color.r, color.b);
		if (green > 50 && green > color.r * 1.1F && green > color.b * 1.2F && green - otherMax > 18) {
			emissivePalette[i * 2 + 1] = 255;
		}
	}
	glGenTextures(1, &m_EmissivePaletteTexture);
	glBindTexture(GL_TEXTURE_2D, m_EmissivePaletteTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, 256, 1, 0, GL_RG, GL_UNSIGNED_BYTE, emissivePalette.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void RenderMan::Destroy() {
	if (m_EmissivePaletteTexture) {
		glDeleteTextures(1, &m_EmissivePaletteTexture);
		m_EmissivePaletteTexture = 0;
	}
}

std::shared_ptr<DrawCall> RenderMan::BeginDraw() {
	ZoneScoped;
	std::shared_ptr<DrawCall> drawCall;
	if (!m_ActiveBatch->m_FreeDrawCalls.empty()) {
		drawCall = std::move(m_ActiveBatch->m_FreeDrawCalls.back());
		m_ActiveBatch->m_FreeDrawCalls.pop_back();
		drawCall->Reset(static_cast<int>(m_ActiveBatch->m_DrawCalls.size()));
	} else {
		drawCall.reset(new DrawCall(static_cast<int>(m_ActiveBatch->m_DrawCalls.size())));
	}
	m_ActiveBatch->m_DrawCalls.push_back(drawCall);
	drawCall->m_Shader = m_ActiveBatch->m_CurrentShader;
	drawCall->m_TextureId = m_ShapesTexture->GetTextureId();
	drawCall->m_BlendMode = m_ActiveBatch->m_CurrentBlendMode;
	m_ActiveBatch->m_CurrentDepth += RenderBatch::c_DrawDepthIncrement;
	return drawCall;
}

void RenderMan::BeginFrame(const Camera* camera) {
	ZoneScoped;
	m_ActiveBatch->m_CurrentCamera = camera ? camera : &m_DefaultCamera;
	m_ActiveBatch->m_CurrentShader = m_DefaultShader.get();
	m_ActiveBatch->m_CurrentBlendMode = Blend::ALPHA;
	m_ActiveBatch->m_CurrentZ = c_DefaultDrawDepth;
	m_ActiveBatch->BeginFrame();
}

void RenderMan::DrawActiveBatch() {
	ZoneScoped;
	m_ActiveBatch->Flush();
}
