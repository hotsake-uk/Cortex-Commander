#pragma once
#include "glm/glm.hpp"

namespace RTE {
	struct Vertex {
		Vertex(glm::vec2 pos, glm::u8vec4 color = {255, 255, 255, 255});
		Vertex(glm::vec2 pos, glm::vec2 uv, glm::u8vec4 color = {255, 255, 255, 255});
		Vertex(glm::vec2 pos, glm::vec2 uv, float layerDepth, glm::u8vec4 color = {255, 255, 255, 255});
		/// Takes every field as given, without looking at the active batch. For hot paths that read the batch's depth and surface once for several vertices:
		/// pos.z is then the batch's current Z offset plus its current depth, as the constructors above work it out.
		Vertex(const glm::vec3& pos, const glm::vec2& uv, const glm::u8vec4& color, const glm::u8vec4& surface) :
		    m_Pos(pos), m_TextureUV(uv), m_Color(color), m_Surface(surface) {}

		glm::vec3 m_Pos{};
		glm::vec2 m_TextureUV{};
		glm::u8vec4 m_Color{};
		glm::u8vec4 m_Surface{}; //!< How the lighting should treat what's drawn: R how metallic, G how glossy, B whether it's a solid object that casts shadows. Taken from the active batch's current surface.
	};
}
