#include "Draw.h"
#include <cmath>
#include "allegro.h"
#include "GLStateMan.h"
#include "RenderMan.h"
#include "glm/gtx/transform.hpp"
#include "tracy/Tracy.hpp"

using namespace RTE;

namespace {
	/// Bakes a 2D transform into vertex positions on the CPU, keeping their depth. Cheaper than a per draw call uniform and lets consecutive draws be merged.
	void TransformVertices(std::vector<Vertex>& vertices, const glm::mat4& transform) {
		for (Vertex& vertex: vertices) {
			glm::vec4 position = transform * glm::vec4(vertex.m_Pos.x, vertex.m_Pos.y, 0.0f, 1.0f);
			vertex.m_Pos.x = position.x;
			vertex.m_Pos.y = position.y;
		}
	}
} // namespace
namespace {
	/// Appends a textured rectangle to a draw call in place, reusing the draw call's vector capacity. Same layout as Shape::Rectangle.
	void AppendRectangle(DrawCall& draw, const FloatRect& rect, const FloatRect& uv, glm::u8vec4 color) {
		int base = static_cast<int>(draw.m_Vertices.size());
		draw.m_Vertices.emplace_back(glm::vec2(rect.x, rect.y), glm::vec2(uv.x, uv.y), color);
		draw.m_Vertices.emplace_back(glm::vec2(rect.x + rect.w, rect.y + rect.h), glm::vec2(uv.x + uv.w, uv.y + uv.h), color);
		draw.m_Vertices.emplace_back(glm::vec2(rect.x + rect.w, rect.y), glm::vec2(uv.x + uv.w, uv.y), color);
		draw.m_Vertices.emplace_back(glm::vec2(rect.x, rect.y + rect.h), glm::vec2(uv.x, uv.y + uv.h), color);
		draw.m_Indices.insert(draw.m_Indices.end(), {base, base + 1, base + 2, base, base + 3, base + 1});
	}
} // namespace

// Raylib-style wrappers kept for the remaining CPU-side draw paths (editor/placement previews, menus). They draw on the active render batch,
// in whatever space its camera uses. Rotations follow the old vendored raylib convention: radians, with positive values rotating counter-clockwise on screen.
namespace {
	RTE::Color ToColor(RLColor tint) { return RTE::Color(tint.r, tint.g, tint.b, tint.a); }
} // namespace

void RTE::DrawTexture(BITMAP* bitmap, int posX, int posY, RLColor tint) {
	DrawTexturePro(bitmap, {0.0f, 0.0f, static_cast<float>(bitmap->w), static_cast<float>(bitmap->h)}, {static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(bitmap->w), static_cast<float>(bitmap->h)}, {0.0f, 0.0f}, 0.0f, tint);
}

void RTE::DrawTextureV(BITMAP* bitmap, Vector2 pos, RLColor tint) {
	DrawTexture(bitmap, static_cast<int>(pos.x), static_cast<int>(pos.y), tint);
}

void RTE::DrawTextureEx(BITMAP* bitmap, Vector2 pos, float rotation, float scale, RLColor tint) {
	DrawTexturePro(bitmap, {0.0f, 0.0f, static_cast<float>(bitmap->w), static_cast<float>(bitmap->h)}, {pos.x, pos.y, bitmap->w * scale, bitmap->h * scale}, {0.0f, 0.0f}, rotation, tint);
}

void RTE::DrawTextureRec(BITMAP* bitmap, Rectangle source, Vector2 pos, RLColor tint) {
	DrawTexturePro(bitmap, source, {pos.x, pos.y, std::abs(source.width), std::abs(source.height)}, {0.0f, 0.0f}, 0.0f, tint);
}

void RTE::DrawTexturePro(BITMAP* bitmap, Rectangle source, Rectangle dest, Vector2 origin, float rotation, RLColor tint) {
	ZoneScoped;
	if (!bitmap || dest.width == 0.0f || dest.height == 0.0f) {
		return;
	}
	// Negative source width/height flips the texture, like raylib.
	float textureWidth = static_cast<float>(bitmap->w);
	float textureHeight = static_cast<float>(bitmap->h);
	float uvLeft = source.x / textureWidth;
	float uvTop = source.y / textureHeight;
	float uvWidth = std::abs(source.width) / textureWidth;
	float uvHeight = std::abs(source.height) / textureHeight;
	FloatRect uv(uvLeft, uvTop, uvWidth, uvHeight);
	if (source.width < 0.0f) {
		uv = FloatRect(uvLeft + uvWidth, uvTop, -uvWidth, uvHeight);
	}
	if (source.height < 0.0f) {
		uv = FloatRect(uv.x, uvTop + uvHeight, uv.w, -uvHeight);
	}

	std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
	draw->m_TextureId = g_GLStateMan.GetStaticTextureFromBitmap(bitmap).id;
	draw->m_Indexed = bitmap_color_depth(bitmap) == 8;
	AppendRectangle(*draw, FloatRect(0.0f, 0.0f, dest.width, dest.height), uv, ToColor(tint));
	// dest.x/y is where the origin point ends up; rotation is around it.
	glm::mat4 transform = glm::translate(glm::vec3(dest.x, dest.y, 0.0f));
	transform = glm::rotate(transform, -rotation, glm::vec3(0.0f, 0.0f, 1.0f));
	transform = glm::translate(transform, glm::vec3(-origin.x, -origin.y, 0.0f));
	TransformVertices(draw->m_Vertices, transform);
}
namespace RTE {
	namespace Draw {
		std::shared_ptr<DrawCall> DrawTexture(Texture* texture, float posX, float posY, Color tint) {
			ZoneScoped;
			return DrawTexture(texture, glm::vec2(posX, posY), tint);
		}

		std::shared_ptr<DrawCall> DrawTexture(Texture* texture, glm::vec2 pos, Color tint) {
			ZoneScoped;
			std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
			draw->m_TextureId = texture->GetTextureId();
			draw->m_Indexed = texture->GetBitDepth() == 8;
			AppendRectangle(*draw, FloatRect(pos.x, pos.y, texture->GetDimensions().w, texture->GetDimensions().h), texture->GetUVRect(), tint);
			return draw;
		}

		std::shared_ptr<DrawCall> DrawTexture(Texture* texture, FloatRect dest, Color tint) {
			ZoneScoped;
			std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
			draw->m_TextureId = texture->GetTextureId();
			draw->m_Indexed = texture->GetBitDepth() == 8;
			AppendRectangle(*draw, dest, texture->GetUVRect(), tint);
			return draw;
		}

		std::shared_ptr<DrawCall> DrawTexture(Texture* texture, glm::vec2 pos, glm::vec2 origin, float angle, glm::vec2 scale, Color tint) {
			ZoneScoped;
			std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
			draw->m_TextureId = texture->GetTextureId();
			draw->m_Indexed = texture->GetBitDepth() == 8;
			AppendRectangle(*draw, FloatRect(0, 0, texture->GetDimensions().w, texture->GetDimensions().h), texture->GetUVRect(), tint);
			glm::mat4 transform = glm::translate(glm::vec3(pos, 0.0f));
			transform = glm::rotate(transform, angle, glm::vec3(0.0f, 0.0f, 1.0f));
			transform = glm::scale(transform, glm::vec3(scale, 1.0f));
			transform = glm::translate(transform, glm::vec3(origin, 0.0f));
			TransformVertices(draw->m_Vertices, transform);

			return draw;
		}

		std::shared_ptr<DrawCall> DrawBitmap(BITMAP* bitmap, glm::vec2 pos, glm::vec2 origin, float angle, glm::vec2 scale, Color tint) {
			ZoneScoped;
			std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
			draw->m_TextureId = g_GLStateMan.GetStaticTextureFromBitmap(bitmap).id;
			draw->m_Indexed = bitmap_color_depth(bitmap) == 8;
			AppendRectangle(*draw, FloatRect(0, 0, bitmap->w, bitmap->h), FloatRect(0.0f, 0.0f, 1.0f, 1.0f), tint);
			glm::mat4 transform = glm::translate(glm::vec3(pos, 0.0f));
			transform = glm::rotate(transform, angle, glm::vec3(0.0f, 0.0f, 1.0f));
			transform = glm::scale(transform, glm::vec3(scale, 1.0f));
			transform = glm::translate(transform, glm::vec3(origin, 0.0f));
			TransformVertices(draw->m_Vertices, transform);
			return draw;
		}

		std::shared_ptr<DrawCall> DrawTexture(Texture* texture, const FloatRect& source, const FloatRect& dest, const Color& tint) {
			ZoneScoped;
			std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
			draw->m_TextureId = texture->GetTextureId();
			draw->m_Indexed = texture->GetBitDepth() == 8;
			FloatRect uv = texture->MapUV(FloatRect(
			    source.x / texture->GetDimensions().w,
			    source.y / texture->GetDimensions().h,
			    source.w / texture->GetDimensions().w,
			    source.h / texture->GetDimensions().h));
			AppendRectangle(*draw, dest, uv, tint);
			return draw;
		}

	} // namespace Draw
} // namespace RTE
