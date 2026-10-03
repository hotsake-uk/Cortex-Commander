#include "TextOverlay.h"
#include "Camera.h"
#include "Draw.h"
#include "PresetMan.h"
#include "RenderMan.h"
#include "Texture.h"
#include "allegro.h"
#include "glad/gl.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <vector>

// ImGui vendors stb_truetype. Compile a private copy of its implementation here (static, so it doesn't clash with ImGui's).
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

using namespace RTE;

bool TextOverlay::s_Enabled = true;
int TextOverlay::s_Suspended = 0;

namespace {
	struct CapturedText {
		std::string Text;
		float X, Y; //!< Anchor in internal screen pixels.
		TextOverlay::Align Align;
		float LineHeight; //!< Internal pixels.
		unsigned int Fill, Outline;
		bool Shadow;
		int ClipX, ClipY, ClipWidth, ClipHeight; //!< Internal pixels.
		int BoxLeft, BoxTop, BoxRight, BoxBottom; //!< Where the text would have been in the target bitmap.
		size_t BoxHash; //!< Hash of the target's pixels under the text when captured.
	};

	/// Hash of a rectangle of an 8 or 32 bit bitmap's pixels.
	size_t HashRegion(const BITMAP* bitmap, int left, int top, int right, int bottom) {
		left = std::clamp(left, 0, bitmap->w);
		right = std::clamp(right, 0, bitmap->w);
		top = std::clamp(top, 0, bitmap->h);
		bottom = std::clamp(bottom, 0, bitmap->h);
		int bytesPerPixel = bitmap_color_depth(const_cast<BITMAP*>(bitmap)) / 8;
		size_t hash = 1469598103934665603ULL;
		for (int y = top; y < bottom; ++y) {
			const unsigned char* row = bitmap->line[y] + left * bytesPerPixel;
			for (int i = 0; i < (right - left) * bytesPerPixel; ++i) {
				hash = (hash ^ row[i]) * 1099511628211ULL;
			}
		}
		return hash;
	}

	size_t s_TargetFirstItem = 0; //!< Index of the first item captured for the current target.

	/// Glyphs of the font baked at one pixel size.
	struct GlyphSet {
		std::unique_ptr<Texture> Atlas;
		int AtlasWidth = 0;
		int AtlasHeight = 0;
		stbtt_packedchar Chars[95]{}; //!< Printable ASCII, 32 to 126.
		float Ascent = 0.0F;
		float Descent = 0.0F;
	};

	const BITMAP* s_Target = nullptr;
	int s_TargetX = 0;
	int s_TargetY = 0;
	int s_TargetClipWidth = 0;
	int s_TargetClipHeight = 0;
	std::vector<CapturedText> s_Pending;
	std::vector<CapturedText> s_Presented;

	std::vector<unsigned char> s_FontData;
	stbtt_fontinfo s_FontInfo{};
	bool s_FontLoadAttempted = false;
	bool s_FontLoaded = false;
	std::unordered_map<int, std::unique_ptr<GlyphSet>> s_GlyphSets;

	bool LoadFont() {
		if (s_FontLoadAttempted) {
			return s_FontLoaded;
		}
		s_FontLoadAttempted = true;
		std::ifstream file(g_PresetMan.GetFullModulePath("Base.rte/GUIs/Fonts/Roboto-Medium.ttf"), std::ios::binary);
		if (!file) {
			return false;
		}
		s_FontData.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		s_FontLoaded = !s_FontData.empty() && stbtt_InitFont(&s_FontInfo, s_FontData.data(), stbtt_GetFontOffsetForIndex(s_FontData.data(), 0)) != 0;
		return s_FontLoaded;
	}

	const GlyphSet* GetGlyphSet(int pixelSize) {
		auto found = s_GlyphSets.find(pixelSize);
		if (found != s_GlyphSets.end()) {
			return found->second.get();
		}
		auto set = std::make_unique<GlyphSet>();
		int side = pixelSize <= 24 ? 256 : (pixelSize <= 48 ? 512 : 1024);
		std::vector<unsigned char> coverage(static_cast<size_t>(side) * side, 0);
		stbtt_pack_context pack;
		if (!stbtt_PackBegin(&pack, coverage.data(), side, side, 0, 1, nullptr)) {
			return nullptr;
		}
		stbtt_PackSetOversampling(&pack, 1, 1);
		stbtt_PackFontRange(&pack, s_FontData.data(), 0, static_cast<float>(pixelSize), 32, 95, set->Chars);
		stbtt_PackEnd(&pack);

		GLuint texture = 0;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, side, side, 0, GL_RED, GL_UNSIGNED_BYTE, coverage.data());
		// White glyphs with coverage as alpha, so the vertex color tints them.
		GLint swizzle[] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
		glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		set->Atlas = std::make_unique<Texture>(texture);
		set->AtlasWidth = side;
		set->AtlasHeight = side;

		int ascent = 0;
		int descent = 0;
		int lineGap = 0;
		stbtt_GetFontVMetrics(&s_FontInfo, &ascent, &descent, &lineGap);
		float scale = stbtt_ScaleForPixelHeight(&s_FontInfo, static_cast<float>(pixelSize));
		set->Ascent = static_cast<float>(ascent) * scale;
		set->Descent = static_cast<float>(descent) * scale;
		const GlyphSet* result = set.get();
		s_GlyphSets[pixelSize] = std::move(set);
		return result;
	}

	float MeasureWidth(const GlyphSet& glyphs, const std::string& text) {
		float width = 0.0F;
		for (char character: text) {
			width += glyphs.Chars[static_cast<unsigned char>(character) - 32].xadvance;
		}
		return width;
	}

	/// Positions are window pixels from the top left; the window camera's y axis points up, so quads are flipped into it.
	void DrawString(const GlyphSet& glyphs, const std::string& text, float x, float baseline, Color color, const std::optional<FloatRect>& scissor, float windowHeight) {
		float penX = x;
		float penY = baseline;
		for (char character: text) {
			stbtt_aligned_quad quad;
			stbtt_GetPackedQuad(glyphs.Chars, glyphs.AtlasWidth, glyphs.AtlasHeight, static_cast<unsigned char>(character) - 32, &penX, &penY, &quad, 1);
			if (quad.x1 <= quad.x0 || quad.y1 <= quad.y0) {
				continue;
			}
			FloatRect source(quad.s0 * glyphs.AtlasWidth, quad.t0 * glyphs.AtlasHeight, (quad.s1 - quad.s0) * glyphs.AtlasWidth, (quad.t1 - quad.t0) * glyphs.AtlasHeight);
			std::shared_ptr<DrawCall> draw = Draw::DrawTexture(glyphs.Atlas.get(), source, FloatRect(quad.x0, windowHeight - quad.y0, quad.x1 - quad.x0, -(quad.y1 - quad.y0)), color);
			draw->m_Scissor = scissor;
		}
	}

	Color FromRGB(unsigned int rgb, int alpha = 255) {
		return Color(static_cast<int>((rgb >> 16) & 0xFF), static_cast<int>((rgb >> 8) & 0xFF), static_cast<int>(rgb & 0xFF), alpha);
	}
} // namespace

void TextOverlay::SetTarget(const BITMAP* bitmap, int offsetX, int offsetY, int clipWidth, int clipHeight) {
	s_Target = bitmap;
	s_TargetX = offsetX;
	s_TargetY = offsetY;
	s_TargetClipWidth = clipWidth;
	s_TargetClipHeight = clipHeight;
	s_TargetFirstItem = s_Pending.size();
}

void TextOverlay::ClearTarget() {
	if (s_Target) {
		const BITMAP* target = s_Target;
		auto firstItem = s_Pending.begin() + static_cast<std::ptrdiff_t>(std::min(s_TargetFirstItem, s_Pending.size()));
		s_Pending.erase(std::remove_if(firstItem, s_Pending.end(), [target](const CapturedText& item) { return HashRegion(target, item.BoxLeft, item.BoxTop, item.BoxRight, item.BoxBottom) != item.BoxHash; }), s_Pending.end());
	}
	s_Target = nullptr;
}

bool TextOverlay::Capture(const BITMAP* target, int x, int y, const std::string& text, Align align, int lineHeight, unsigned int fillRGB, unsigned int outlineRGB, bool shadow, int bitmapWidth) {
	if (!s_Enabled || s_Suspended > 0 || !target || target != s_Target || text.empty() || !LoadFont()) {
		return false;
	}
	// Special characters (icons in the game's bitmap fonts) aren't in the TTF; keep those lines as they were.
	for (char character: text) {
		unsigned char code = static_cast<unsigned char>(character);
		if (code < 32 || code > 126) {
			return false;
		}
	}
	int boxLeft = align == Centre ? x - bitmapWidth / 2 : (align == Right ? x - bitmapWidth : x);
	int boxRight = boxLeft + bitmapWidth;
	size_t boxHash = HashRegion(target, boxLeft, y, boxRight, y + lineHeight);
	s_Pending.push_back({text, static_cast<float>(x + s_TargetX), static_cast<float>(y + s_TargetY), align, static_cast<float>(lineHeight), fillRGB, outlineRGB, shadow, s_TargetX, s_TargetY, s_TargetClipWidth, s_TargetClipHeight, boxLeft, y, boxRight, y + lineHeight, boxHash});
	return true;
}

bool TextOverlay::HasPendingText() {
	return !s_Pending.empty();
}

void TextOverlay::Render(int windowWidth, int windowHeight, int viewportX, int viewportY, int viewportWidth, int viewportHeight, int internalWidth, int internalHeight, bool redrawLast) {
	if (!redrawLast) {
		s_Presented.swap(s_Pending);
		s_Pending.clear();
	}
	if (s_Presented.empty() || internalWidth <= 0 || internalHeight <= 0 || !LoadFont()) {
		return;
	}
	float scale = static_cast<float>(viewportWidth) / static_cast<float>(internalWidth);
	// Window pixels, top left origin.
	float originX = static_cast<float>(viewportX);
	float originY = static_cast<float>(windowHeight - viewportY - viewportHeight);

	glViewport(0, 0, windowWidth, windowHeight);
	glClear(GL_DEPTH_BUFFER_BIT);
	Camera windowCamera(Vector(0.0F, 0.0F), Box(Vector(0.0F, 0.0F), static_cast<float>(windowWidth), static_cast<float>(windowHeight)));
	g_RenderMan.BeginFrame(&windowCamera);
	g_RenderMan.SetActiveBlendMode(Blend::ALPHA);

	for (const CapturedText& item: s_Presented) {
		// Slightly smaller than the bitmap font's line, whose height includes its outline.
		int pixelSize = std::max(8, static_cast<int>(std::lround(item.LineHeight * scale * 0.95F)));
		const GlyphSet* glyphs = GetGlyphSet(pixelSize);
		if (!glyphs) {
			continue;
		}
		float width = MeasureWidth(*glyphs, item.Text);
		float x = originX + item.X * scale;
		if (item.Align == Centre) {
			x -= width * 0.5F;
		} else if (item.Align == Right) {
			x -= width;
		}
		x = std::round(x);
		float lineTop = originY + item.Y * scale;
		float textHeight = glyphs->Ascent - glyphs->Descent;
		float baseline = std::round(lineTop + (item.LineHeight * scale - textHeight) * 0.5F + glyphs->Ascent);

		// Split screens clip text to their own screen. Scissor rectangles are in GL convention, from the bottom left.
		std::optional<FloatRect> scissor;
		if (item.ClipWidth > 0 && item.ClipHeight > 0) {
			float clipLeft = originX + static_cast<float>(item.ClipX) * scale;
			float clipTop = originY + static_cast<float>(item.ClipY) * scale;
			float clipWidth = static_cast<float>(item.ClipWidth) * scale;
			float clipHeight = static_cast<float>(item.ClipHeight) * scale;
			scissor = FloatRect(clipLeft, static_cast<float>(windowHeight) - clipTop - clipHeight, clipWidth, clipHeight);
		}

		// An outline like the bitmap fonts' baked one keeps text readable over anything; a drop shadow when asked for.
		float outline = std::max(1.0F, std::round(scale * 0.6F));
		Color outlineColor = FromRGB(item.Outline, 255);
		if (item.Shadow) {
			DrawString(*glyphs, item.Text, x + outline * 2.0F, baseline + outline * 2.0F, FromRGB(item.Outline, 160), scissor, static_cast<float>(windowHeight));
		}
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (dx != 0 || dy != 0) {
					DrawString(*glyphs, item.Text, x + static_cast<float>(dx) * outline, baseline + static_cast<float>(dy) * outline, outlineColor, scissor, static_cast<float>(windowHeight));
				}
			}
		}
		DrawString(*glyphs, item.Text, x, baseline, FromRGB(item.Fill), scissor, static_cast<float>(windowHeight));
	}
	g_RenderMan.DrawActiveBatch();
	g_RenderMan.BeginFrame(nullptr);
}

unsigned int TextOverlay::ToRGB(unsigned long color, int colorDepth) {
	int red = getr_depth(colorDepth, static_cast<int>(color));
	int green = getg_depth(colorDepth, static_cast<int>(color));
	int blue = getb_depth(colorDepth, static_cast<int>(color));
	return (static_cast<unsigned int>(red) << 16) | (static_cast<unsigned int>(green) << 8) | static_cast<unsigned int>(blue);
}

void TextOverlay::Destroy() {
	s_GlyphSets.clear();
	s_Pending.clear();
	s_Presented.clear();
}
