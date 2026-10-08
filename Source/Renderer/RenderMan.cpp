#include "RenderMan.h"
#include "Constants.h"
#include <array>
#include <algorithm>
#include <cmath>
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
	// R = emissive strength, G = vegetation (green-dominant colors, which sway in the wind when they're part of the terrain), A = shininess (greys: metal and concrete catch highlights from lights).
	std::array<unsigned char, 1024>& emissivePalette = m_EmissivePalette;
	emissivePalette.fill(0);
	emissivePalette[g_YellowGlowColor * 4] = 255;
	emissivePalette[98 * 4] = 255;
	emissivePalette[120 * 4] = 200;
	for (int i = 1; i < 256; ++i) {
		SDL_Color color = palette->colors[i];
		int green = color.g;
		int otherMax = std::max<int>(color.r, color.b);
		if (green > 50 && green > color.r * 1.1F && green > color.b * 1.2F && green - otherMax > 18) {
			emissivePalette[i * 4 + 1] = 255;
		}
		int brightest = std::max({static_cast<int>(color.r), static_cast<int>(color.g), static_cast<int>(color.b)});
		int darkest = std::min({static_cast<int>(color.r), static_cast<int>(color.g), static_cast<int>(color.b)});
		if (brightest > 0 && emissivePalette[i * 4] == 0) {
			float saturation = static_cast<float>(brightest - darkest) / static_cast<float>(brightest);
			float greyness = std::clamp((0.2F - saturation) / 0.2F, 0.0F, 1.0F);
			float brightness = std::clamp((static_cast<float>(brightest) / 255.0F - 0.22F) / 0.45F, 0.0F, 1.0F);
			emissivePalette[i * 4 + 3] = static_cast<unsigned char>(greyness * brightness * 0.75F * 255.0F);
		}
	}
	glGenTextures(1, &m_EmissivePaletteTexture);
	glBindTexture(GL_TEXTURE_2D, m_EmissivePaletteTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, emissivePalette.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void RenderMan::SetLiquidPaletteColor(int paletteIndex, int liquidLook, int emissive) {
	if (paletteIndex <= 0 || paletteIndex > 255 || !m_EmissivePaletteTexture) {
		return;
	}
	m_EmissivePalette[paletteIndex * 4 + 2] = static_cast<unsigned char>(std::clamp(liquidLook, 0, c_MaxLiquidLooks - 1) * 16);
	m_EmissivePalette[paletteIndex * 4] = std::max(m_EmissivePalette[paletteIndex * 4], static_cast<unsigned char>(std::clamp(emissive, 0, 255)));
	// Liquids aren't vegetation, even if they're green.
	m_EmissivePalette[paletteIndex * 4 + 1] = 0;
	// Water and acid are glossy; lava glows instead, and its glow breathes (animated palette flags), each colour a little out of step.
	m_EmissivePalette[paletteIndex * 4 + 3] = static_cast<unsigned char>(emissive > 0 ? 0 : 230);
	if (emissive > 0) {
		float glow = static_cast<float>(m_EmissivePalette[paletteIndex * 4]) / 255.0F;
		SetPalettePulse(paletteIndex, glow * 0.7F, glow, 2.6F, std::fmod(static_cast<float>(paletteIndex) * 0.37F, 1.0F), true);
	}
	glBindTexture(GL_TEXTURE_2D, m_EmissivePaletteTexture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, m_EmissivePalette.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void RenderMan::SetPalettePulse(int paletteIndex, float low, float high, float period, float phase, bool automatic) {
	if (paletteIndex <= 0 || paletteIndex > 255) {
		return;
	}
	std::scoped_lock lock(m_PaletteAnimationMutex);
	m_PalettePulses.erase(std::remove_if(m_PalettePulses.begin(), m_PalettePulses.end(), [paletteIndex](const PalettePulse& pulse) { return pulse.Index == paletteIndex; }), m_PalettePulses.end());
	if (period > 0.0F) {
		m_PalettePulses.push_back({paletteIndex, std::clamp(low, 0.0F, 1.0F), std::clamp(high, 0.0F, 1.0F), std::max(period, 0.05F), phase, automatic});
	}
}

void RenderMan::SetPaletteCycle(int from, int to, float period) {
	from = std::clamp(from, 1, 255);
	to = std::clamp(to, 1, 255);
	if (to < from) {
		std::swap(from, to);
	}
	std::scoped_lock lock(m_PaletteAnimationMutex);
	m_PaletteCycles.erase(std::remove_if(m_PaletteCycles.begin(), m_PaletteCycles.end(), [from, to](const PaletteCycle& cycle) { return cycle.From == from && cycle.To == to; }), m_PaletteCycles.end());
	if (period > 0.0F && to > from) {
		m_PaletteCycles.push_back({from, to, std::max(period, 0.05F)});
	}
}

void RenderMan::ClearPaletteAnimation() {
	std::scoped_lock lock(m_PaletteAnimationMutex);
	m_PalettePulses.erase(std::remove_if(m_PalettePulses.begin(), m_PalettePulses.end(), [](const PalettePulse& pulse) { return !pulse.Automatic; }), m_PalettePulses.end());
	m_PaletteCycles.clear();
}

void RenderMan::UpdatePaletteAnimation(float time, bool enabled, float strength) {
	std::vector<PalettePulse> pulses;
	std::vector<PaletteCycle> cycles;
	{
		std::scoped_lock lock(m_PaletteAnimationMutex);
		pulses = m_PalettePulses;
		cycles = m_PaletteCycles;
	}
	bool animate = enabled && strength > 0.0F && (!pulses.empty() || !cycles.empty());
	if ((!animate && !m_PaletteAnimated) || !m_EmissivePaletteTexture || !m_PaletteTexture) {
		return;
	}
	GLuint paletteTexture = m_PaletteTexture->GetTextureId();
	if (!m_PaletteColorsRead) {
		// The colours as the texture holds them, so cycling writes them back in the same layout.
		glBindTexture(GL_TEXTURE_2D, paletteTexture);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_PaletteColors.data());
		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		m_PaletteColorsRead = true;
	}
	std::array<unsigned char, 1024> glow = m_EmissivePalette;
	std::array<unsigned char, 1024> colors = m_PaletteColors;
	if (animate) {
		float amount = std::min(strength, 1.0F);
		for (const PalettePulse& pulse: pulses) {
			float wave = 0.5F + 0.5F * std::sin((time / pulse.Period + pulse.Phase) * 6.2831853F);
			float own = static_cast<float>(glow[pulse.Index * 4]) / 255.0F;
			float value = own + (pulse.Low + (pulse.High - pulse.Low) * wave - own) * amount;
			glow[pulse.Index * 4] = static_cast<unsigned char>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
		}
		for (const PaletteCycle& cycle: cycles) {
			int length = cycle.To - cycle.From + 1;
			int shift = static_cast<int>(std::floor(time / cycle.Period * static_cast<float>(length))) % length;
			shift = shift < 0 ? shift + length : shift;
			for (int i = 0; i < length; ++i) {
				int source = cycle.From + (i + shift) % length;
				std::copy_n(m_PaletteColors.begin() + source * 4, 4, colors.begin() + (cycle.From + i) * 4);
			}
		}
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glBindTexture(GL_TEXTURE_2D, m_EmissivePaletteTexture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, glow.data());
	glBindTexture(GL_TEXTURE_2D, paletteTexture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindTexture(GL_TEXTURE_2D, 0);
	m_PaletteAnimated = animate;
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
	// Whatever this draw is, it ends any run of pixels before it.
	m_ActiveBatch->m_OpenPixelDraw = nullptr;
	drawCall->m_Shader = m_ActiveBatch->m_CurrentShader;
	drawCall->m_TextureId = m_ShapesTexture->GetTextureId();
	drawCall->m_BlendMode = m_ActiveBatch->m_CurrentBlendMode;
	if (m_ActiveBatch->m_InObjectShader) {
		drawCall->m_UniformValues = m_ActiveBatch->m_ObjectUniforms;
	}
	if (!m_ActiveBatch->m_ObjectMapUniforms.empty()) {
		drawCall->m_UniformValues.insert(drawCall->m_UniformValues.end(), m_ActiveBatch->m_ObjectMapUniforms.begin(), m_ActiveBatch->m_ObjectMapUniforms.end());
	}
	if (!m_ActiveBatch->m_LayerUniforms.empty()) {
		drawCall->m_UniformValues.insert(drawCall->m_UniformValues.end(), m_ActiveBatch->m_LayerUniforms.begin(), m_ActiveBatch->m_LayerUniforms.end());
	}
	m_ActiveBatch->m_CurrentDepth += RenderBatch::c_DrawDepthIncrement;
	return drawCall;
}

void RenderMan::BeginObjectShader(const Shader* shader, float time, float objectSeed, float health, float strength, float relief) {
	if (!shader) {
		return;
	}
	if (!m_ActiveBatch->m_InObjectShader) {
		m_ActiveBatch->m_ShaderBeforeObject = m_ActiveBatch->m_CurrentShader;
	}
	m_ActiveBatch->m_InObjectShader = true;
	m_ActiveBatch->m_CurrentShader = shader;
	std::vector<std::shared_ptr<UniformValueType>>& uniforms = m_ActiveBatch->m_ObjectUniforms;
	uniforms.clear();
	const std::pair<int, float> values[] = {{shader->GetTimeUniform(), time}, {shader->GetObjectSeedUniform(), objectSeed}, {shader->GetHealthUniform(), health}, {shader->GetStrengthUniform(), strength}, {shader->GetReliefUniform(), relief}};
	for (const auto& [location, value]: values) {
		if (location >= 0) {
			uniforms.push_back(std::make_shared<FloatValue>(location, value));
		}
	}
}

namespace {
	/// A sprite's authored maps for one draw: binds them on their units and says they're there, and says they're gone again after the draw, since the next draws share the program.
	class SpriteMapsValue : public UniformValueType {
	public:
		SpriteMapsValue(const Shader::SpriteMapUniforms& uniforms, GLuint normalMap, GLuint emissiveMap, const glm::vec4& spriteUV, float strength) :
		    UniformValueType(uniforms.HasNormalMap), m_Uniforms(uniforms), m_NormalMap(normalMap), m_EmissiveMap(emissiveMap), m_SpriteUV(spriteUV), m_Strength(strength) {}

		void Enable() override {
			static constexpr GLint c_NormalUnit = 12;
			static constexpr GLint c_EmissiveUnit = 13;
			if (m_NormalMap && m_Uniforms.NormalMap >= 0) {
				glActiveTexture(GL_TEXTURE0 + c_NormalUnit);
				glBindTexture(GL_TEXTURE_2D, m_NormalMap);
				glUniform1i(m_Uniforms.NormalMap, c_NormalUnit);
				glUniform1i(m_Uniforms.HasNormalMap, 1);
			}
			if (m_EmissiveMap && m_Uniforms.EmissiveMap >= 0) {
				glActiveTexture(GL_TEXTURE0 + c_EmissiveUnit);
				glBindTexture(GL_TEXTURE_2D, m_EmissiveMap);
				glUniform1i(m_Uniforms.EmissiveMap, c_EmissiveUnit);
				glUniform1i(m_Uniforms.HasEmissiveMap, 1);
			}
			// Draw call textures are bound to unit 1, which the batch expects to find active.
			glActiveTexture(GL_TEXTURE1);
			glUniform4f(m_Uniforms.UVRect, m_SpriteUV.x, m_SpriteUV.y, m_SpriteUV.z, m_SpriteUV.w);
			glUniform1f(m_Uniforms.Strength, m_Strength);
		}

		void Reset() override {
			glUniform1i(m_Uniforms.HasNormalMap, 0);
			glUniform1i(m_Uniforms.HasEmissiveMap, 0);
		}

	private:
		Shader::SpriteMapUniforms m_Uniforms;
		GLuint m_NormalMap;
		GLuint m_EmissiveMap;
		glm::vec4 m_SpriteUV;
		float m_Strength;
	};
} // namespace

void RenderMan::BeginObjectMaps(GLuint normalMap, GLuint emissiveMap, const glm::vec4& spriteUV, float strength) {
	m_ActiveBatch->m_ObjectMapUniforms.clear();
	const Shader* shader = m_ActiveBatch->m_CurrentShader;
	if (!shader || (!normalMap && !emissiveMap)) {
		return;
	}
	const Shader::SpriteMapUniforms& uniforms = shader->GetSpriteMapUniforms();
	if ((normalMap && uniforms.HasNormalMap >= 0) || (emissiveMap && uniforms.HasEmissiveMap >= 0)) {
		m_ActiveBatch->m_ObjectMapUniforms.push_back(std::make_shared<SpriteMapsValue>(uniforms, normalMap, emissiveMap, spriteUV, strength));
	}
}

void RenderMan::EndObjectShader() {
	if (!m_ActiveBatch->m_InObjectShader) {
		return;
	}
	m_ActiveBatch->m_CurrentShader = m_ActiveBatch->m_ShaderBeforeObject;
	m_ActiveBatch->m_ShaderBeforeObject = nullptr;
	m_ActiveBatch->m_ObjectUniforms.clear();
	m_ActiveBatch->m_InObjectShader = false;
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
