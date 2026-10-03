#include "Draw.h"
#include <cmath>
#include "allegro.h"
#include "GLStateMan.h"
#include "RenderMan.h"
#include "glm/gtx/transform.hpp"
#include "tracy/Tracy.hpp"

using namespace RTE;
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
	Shape::Shape rect = Shape::Rectangle(FloatRect(0.0f, 0.0f, dest.width, dest.height), uv, ToColor(tint));
	draw->m_Vertices = std::move(rect.m_Vertices);
	draw->m_Indices = std::move(rect.m_Indices);
	// dest.x/y is where the origin point ends up; rotation is around it.
	glm::mat4 transform = glm::translate(glm::vec3(dest.x, dest.y, 0.0f));
	transform = glm::rotate(transform, -rotation, glm::vec3(0.0f, 0.0f, 1.0f));
	transform = glm::translate(transform, glm::vec3(-origin.x, -origin.y, 0.0f));
	draw->m_UniformValues.emplace_back(std::make_unique<Matrix4fValue>(draw->m_Shader->GetTransformUniform(), std::move(transform)));
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
			Shape::Shape rect = Shape::Rectangle(FloatRect(pos.x, pos.y, texture->GetDimensions().w, texture->GetDimensions().h), tint);
			draw->m_Vertices = std::move(rect.m_Vertices);
			draw->m_Indices = std::move(rect.m_Indices);
			return draw;
		}

		std::shared_ptr<DrawCall> DrawTexture(Texture* texture, FloatRect dest, Color tint) {
			ZoneScoped;
			std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
			draw->m_TextureId = texture->GetTextureId();
			draw->m_Indexed = texture->GetBitDepth() == 8;
			Shape::Shape rect = Shape::Rectangle(dest, tint);
			draw->m_Vertices = std::move(rect.m_Vertices);
			draw->m_Indices = std::move(rect.m_Indices);
			return draw;
		}

		std::shared_ptr<DrawCall> DrawTexture(Texture* texture, glm::vec2 pos, glm::vec2 origin, float angle, glm::vec2 scale, Color tint) {
			ZoneScoped;
			std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
			draw->m_TextureId = texture->GetTextureId();
			draw->m_Indexed = texture->GetBitDepth() == 8;
			Shape::Shape rect = Shape::Rectangle(FloatRect(0, 0, texture->GetDimensions().w, texture->GetDimensions().h), tint);
			draw->m_Vertices = std::move(rect.m_Vertices);
			draw->m_Indices = std::move(rect.m_Indices);
			glm::mat4 transform = glm::translate(glm::vec3(pos, 0.0f));
			transform = glm::rotate(transform, angle, glm::vec3(0.0f, 0.0f, 1.0f));
			transform = glm::scale(transform, glm::vec3(scale, 1.0f));
			transform = glm::translate(transform, glm::vec3(origin, 0.0f));
			draw->m_UniformValues.emplace_back(std::make_unique<Matrix4fValue>(draw->m_Shader->GetTransformUniform(), std::move(transform)));

			return draw;
		}

		std::shared_ptr<DrawCall> DrawBitmap(BITMAP* bitmap, glm::vec2 pos, glm::vec2 origin, float angle, glm::vec2 scale, Color tint) {
			ZoneScoped;
			std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
			draw->m_TextureId = g_GLStateMan.GetStaticTextureFromBitmap(bitmap).id;
			draw->m_Indexed = bitmap_color_depth(bitmap) == 8;
			Shape::Shape rect = Shape::Rectangle(FloatRect(0, 0, bitmap->w, bitmap->h), tint);
			draw->m_Vertices = std::move(rect.m_Vertices);
			draw->m_Indices = std::move(rect.m_Indices);
			glm::mat4 transform = glm::translate(glm::vec3(pos, 0.0f));
			transform = glm::rotate(transform, angle, glm::vec3(0.0f, 0.0f, 1.0f));
			transform = glm::scale(transform, glm::vec3(scale, 1.0f));
			transform = glm::translate(transform, glm::vec3(origin, 0.0f));
			draw->m_UniformValues.emplace_back(std::make_unique<Matrix4fValue>(draw->m_Shader->GetTransformUniform(), std::move(transform)));
			return draw;
		}

		std::shared_ptr<DrawCall> DrawTexture(Texture* texture, const FloatRect& source, const FloatRect& dest, const Color& tint) {
			ZoneScoped;
			std::shared_ptr<DrawCall> draw = g_RenderMan.BeginDraw();
			draw->m_TextureId = texture->GetTextureId();
			draw->m_Indexed = texture->GetBitDepth() == 8;
			FloatRect uv = FloatRect(
			    source.x / texture->GetDimensions().w,
			    source.y / texture->GetDimensions().h,
			    source.w / texture->GetDimensions().w,
			    source.h / texture->GetDimensions().h);
			Shape::Shape rect = Shape::Rectangle(dest, uv, tint);
			draw->m_Vertices = std::move(rect.m_Vertices);
			draw->m_Indices = std::move(rect.m_Indices);
			return draw;
		}

	} // namespace Draw
} // namespace RTE
