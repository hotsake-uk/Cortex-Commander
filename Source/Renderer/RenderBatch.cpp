#include "RenderBatch.h"
#include "Constants.h"
#include "GLStateMan.h"
#include "RenderMan.h"
#include "PerformanceMan.h"
#include "Shader.h"
#include "glad/gl.h"
#include "glm/glm.hpp"
#include <cstddef>
#include <algorithm>
#include "tracy/Tracy.hpp"
#include "tracy/TracyOpenGL.hpp"
#include "GLCheck.h"

using namespace RTE;

namespace {
	bool SameScissor(const std::optional<FloatRect>& a, const std::optional<FloatRect>& b) {
		if (a.has_value() != b.has_value()) {
			return false;
		}
		return !a.has_value() || (a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h);
	}
} // namespace

VertexBuffer::VertexBuffer() {
	m_Vertices.reserve(4 * c_DefaultBatchVAOElements);
	m_Indices.reserve(6 * c_DefaultBatchVAOElements);
	InitializeBuffers();
};

VertexBuffer::~VertexBuffer() {
	glDeleteBuffers(1, &m_VertexBuffer);
	glDeleteBuffers(1, &m_IndexBuffer);
	glDeleteVertexArrays(1, &m_VertexArray);
	m_VertexBuffer = 0;
	m_IndexBuffer = 0;
	m_VertexArray = 0;
};

VertexBuffer::VertexBuffer(int bufferSize) :
    m_BufferElements(bufferSize) {
	m_Vertices.reserve(4 * bufferSize);
	m_Indices.reserve(6 * bufferSize);
	InitializeBuffers();
}

void VertexBuffer::InitializeBuffers() {

	GL_CHECK(glGenBuffers(1, &m_VertexBuffer));
	GL_CHECK(glGenBuffers(1, &m_IndexBuffer));
	GL_CHECK(glGenVertexArrays(1, &m_VertexArray));

	GL_CHECK(glBindVertexArray(m_VertexArray));
	GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer));
	GL_CHECK(glBufferData(GL_ARRAY_BUFFER, m_BufferElements * 4 * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW));

	GL_CHECK(glEnableVertexAttribArray(VertexAttribLocation::VERTEX));
	GL_CHECK(glVertexAttribPointer(VertexAttribLocation::VERTEX, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0));

	GL_CHECK(glEnableVertexAttribArray(VertexAttribLocation::TEXTURECOORDINATE));
	GL_CHECK(glVertexAttribPointer(VertexAttribLocation::TEXTURECOORDINATE, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLvoid*)offsetof(Vertex, m_TextureUV)));

	GL_CHECK(glEnableVertexAttribArray(VertexAttribLocation::COLOR));
	GL_CHECK(glVertexAttribPointer(VertexAttribLocation::COLOR, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (GLvoid*)offsetof(Vertex, m_Color)));

	GL_CHECK(glEnableVertexAttribArray(VertexAttribLocation::SURFACE));
	GL_CHECK(glVertexAttribPointer(VertexAttribLocation::SURFACE, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (GLvoid*)offsetof(Vertex, m_Surface)));

	GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IndexBuffer));
	GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_BufferElements * 6 * sizeof(decltype(m_Indices)::value_type), nullptr, GL_DYNAMIC_DRAW));
	glBindVertexArray(0);
}

RenderBatch::RenderBatch() = default;

void RenderBatch::BeginFrame() {
	ZoneScoped;
	m_CurrentDepth = 0;
	m_CurrentZ = c_DefaultDrawDepth;
	m_CurrentSurface = glm::u8vec4(0);
	m_ShadowCasting = true;
	m_OpenPixelDraw = nullptr;
	m_ObjectUniforms.clear();
	m_ShaderBeforeObject = nullptr;
	m_InObjectShader = false;
	m_VertexBuffers.m_Vertices.clear();
	m_VertexBuffers.m_Indices.clear();
	RecycleDrawCalls();
}

void RenderBatch::EndFrame() {
	ZoneScoped;
	PerformanceMan::LogStages logStages(true);
	logStages.Next("Batch: gathering vertices");
	PerformanceMan::AddLogCount("# draw calls in a batch", m_DrawCalls.size());
	ApplyDrawCalls();
	logStages.Next("Batch: uploading vertices");
	m_VertexBuffers.UpdateBuffers();
}

void RenderBatch::Render() {
	ZoneScoped;
	TracyGpuZone("Render");
	glBindVertexArray(m_VertexBuffers.m_VertexArray);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_VertexBuffers.m_IndexBuffer);

	const Shader* currentShader = m_CurrentShader ? m_CurrentShader : g_RenderMan.GetDefaultShader();

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_RenderMan.GetPaletteTexture());
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, g_RenderMan.GetEmissivePaletteTexture());
	for (size_t globalUnit = 0; globalUnit < g_RenderMan.GetGlobalTextures().size(); ++globalUnit) {
		glActiveTexture(GL_TEXTURE3 + static_cast<GLenum>(globalUnit));
		glBindTexture(GL_TEXTURE_2D, g_RenderMan.GetGlobalTextures()[globalUnit]);
	}
	// Draw call textures are bound to unit 1, so leave it active.
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, g_RenderMan.GetShapeTexture());
	const Camera* currentCamera = m_CurrentCamera;
	currentShader->Enable();
	currentShader->SetInt(currentShader->GetTextureUniform(), 1);
	currentShader->SetInt(currentShader->GetPaletteUniform(), 0);
	currentShader->SetInt("rteEmissivePalette", 2);
	if(!currentCamera) {
		currentShader->SetMatrix4f(currentShader->GetProjectionUniform(), glm::mat4(1.0f));
		currentShader->SetMatrix4f(currentShader->GetTransformUniform(), glm::mat4(1.0f));
		currentShader->SetMatrix4f(currentShader->GetUVTransformUniform(), glm::mat4(1.0f));
		currentShader->SetMatrix4f(currentShader->GetViewUniform(), glm::mat4(1.0f));
	} else {
		currentShader->SetMatrix4f(currentShader->GetProjectionUniform(), currentCamera->GetProjection());
		currentShader->SetMatrix4f(currentShader->GetTransformUniform(), glm::mat4(1.0f));
		currentShader->SetMatrix4f(currentShader->GetUVTransformUniform(), glm::mat4(1.0f));
		currentShader->SetMatrix4f(currentShader->GetViewUniform(), currentCamera->GetView());
	}

	GLuint indexOffset = 0;
	GLuint activeTexture = g_RenderMan.GetShapeTexture();
	glDisable(GL_MULTISAMPLE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	// Start with blending disabled and track that, so the first draw call enables its own blend mode (default constructed BlendMode is Blend::NONE).
	BlendMode activeBlendMode{};
	glDisable(GL_BLEND);
	GLint indexedUniform = glGetUniformLocation(currentShader->GetProgramID(), "rteIndexed");
	int activeIndexed = -1;
	bool transformsDirty = false;
	bool scissorEnabled = false;

	for (size_t drawIndex = 0; drawIndex < m_DrawCalls.size();) {
		const std::shared_ptr<DrawCall>& drawCall = m_DrawCalls[drawIndex];
		if (drawCall->m_Shader && drawCall->m_Shader != currentShader) {
			currentShader = drawCall->m_Shader;
			currentShader->Enable();
			currentShader->SetInt(currentShader->GetTextureUniform(), 1);
			currentShader->SetInt(currentShader->GetPaletteUniform(), 0);
			currentShader->SetInt("rteEmissivePalette", 2);
			if (currentCamera) {
				currentShader->SetMatrix4f(currentShader->GetProjectionUniform(), currentCamera->GetProjection());
				currentShader->SetMatrix4f(currentShader->GetViewUniform(), currentCamera->GetView());
			}
			transformsDirty = true;
			indexedUniform = glGetUniformLocation(currentShader->GetProgramID(), "rteIndexed");
			activeIndexed = -1;
		}

		if (transformsDirty || !drawCall->m_UniformValues.empty()) {
			currentShader->SetMatrix4f(currentShader->GetTransformUniform(), glm::mat4(1.0f));
			currentShader->SetMatrix4f(currentShader->GetUVTransformUniform(), glm::mat4(1.0f));
			transformsDirty = false;
		}
		for (auto& uniform: drawCall->m_UniformValues) {
			uniform->Enable();
		}
		if (!drawCall->m_UniformValues.empty()) {
			// The draw may have changed transforms or other state, so reset before the next one.
			transformsDirty = true;
		}

		if (drawCall->m_Scissor != std::nullopt) {
			const FloatRect& scissor = *drawCall->m_Scissor;
			glEnable(GL_SCISSOR_TEST);
			glScissor(scissor.x, scissor.y, scissor.w, scissor.h);
			scissorEnabled = true;
		} else if (scissorEnabled) {
			glDisable(GL_SCISSOR_TEST);
			scissorEnabled = false;
		}

		if (drawCall->m_TextureId != activeTexture) {
			glBindTexture(GL_TEXTURE_2D, drawCall->m_TextureId);
			activeTexture = drawCall->m_TextureId;
		}
		if (drawCall->m_BlendMode != activeBlendMode) {
			drawCall->m_BlendMode.Enable();
			activeBlendMode = drawCall->m_BlendMode;
		}
		if (static_cast<int>(drawCall->m_Indexed) != activeIndexed) {
			glUniform1i(indexedUniform, drawCall->m_Indexed ? 1 : 0);
			activeIndexed = drawCall->m_Indexed ? 1 : 0;
		}

		// Merge following draw calls that would render with exactly the same state into one GL draw. Their indices are contiguous in the index buffer.
		size_t indexCount = drawCall->m_Indices.size();
		size_t nextIndex = drawIndex + 1;
		if (drawCall->m_UniformValues.empty() && drawCall->m_DrawMode == GL_TRIANGLES) {
			while (nextIndex < m_DrawCalls.size()) {
				const std::shared_ptr<DrawCall>& next = m_DrawCalls[nextIndex];
				const Shader* nextShader = next->m_Shader ? next->m_Shader : currentShader;
				if (nextShader != currentShader || next->m_TextureId != drawCall->m_TextureId || !(next->m_BlendMode == drawCall->m_BlendMode) || next->m_Indexed != drawCall->m_Indexed ||
				    next->m_DrawMode != GL_TRIANGLES || !next->m_UniformValues.empty() || !SameScissor(next->m_Scissor, drawCall->m_Scissor)) {
					break;
				}
				indexCount += next->m_Indices.size();
				++nextIndex;
			}
		}

		GL_CHECK(glDrawElements(drawCall->m_DrawMode, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT, (GLvoid*)(indexOffset * sizeof(GLuint))));
		indexOffset += static_cast<GLuint>(indexCount);
		drawIndex = nextIndex;
	}
	if (scissorEnabled) {
		glDisable(GL_SCISSOR_TEST);
	}
}

void RenderBatch::RecycleDrawCalls() {
	m_OpenPixelDraw = nullptr;
	for (std::shared_ptr<DrawCall>& drawCall: m_DrawCalls) {
		// Only reuse draw calls nobody else is holding on to.
		if (drawCall.use_count() == 1) {
			m_FreeDrawCalls.push_back(std::move(drawCall));
		}
	}
	m_DrawCalls.clear();
}

void RenderBatch::ClearDraws() {
	ZoneScoped;
	RecycleDrawCalls();
	m_VertexBuffers.m_Vertices.clear();
	m_VertexBuffers.m_Indices.clear();
}

void RenderBatch::ApplyDrawCalls() {
	ZoneScoped;

	// TODO: Sort and batch DrawCalls by shader and transparency.
	// std::stable_sort(m_DrawCalls.begin(), m_DrawCalls.end(), [](auto r, auto l) { return r->m_TextureId < l->m_TextureId; });

	for (auto drawCall: m_DrawCalls) {
		RTEAssert(drawCall.use_count() == 2, "DrawCall still in use on EndFrame!");
		size_t vertexCount = m_VertexBuffers.m_Vertices.size();

		for (auto& index: drawCall->m_Indices) {
			index += vertexCount;
		}
		m_VertexBuffers.m_Vertices.insert(
		    m_VertexBuffers.m_Vertices.end(),
		    std::make_move_iterator(drawCall->m_Vertices.begin()),
		    std::make_move_iterator(drawCall->m_Vertices.end()));

		m_VertexBuffers.m_Indices.insert(
		    m_VertexBuffers.m_Indices.end(),
		    std::make_move_iterator(drawCall->m_Indices.begin()),
		    std::make_move_iterator(drawCall->m_Indices.end()));
	}
}

void VertexBuffer::UpdateBuffers() {
	ZoneScoped;
	TracyGpuZone("VertexBuffer::UpdateBuffers");
	glBindVertexArray(m_VertexArray);

	GLint bufferSize;
	GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer));
	GL_CHECK(glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize));
	if (m_Vertices.size() * sizeof(decltype(m_Vertices)::value_type) < bufferSize) {
		GL_CHECK(glBufferSubData(GL_ARRAY_BUFFER, 0, m_Vertices.size() * sizeof(decltype(m_Vertices)::value_type), m_Vertices.data()));
	} else {
		GL_CHECK(glBufferData(GL_ARRAY_BUFFER, m_Vertices.size() * sizeof(decltype(m_Vertices)::value_type), m_Vertices.data(), GL_DYNAMIC_DRAW));
	}

	GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IndexBuffer));
	glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
	if (m_Indices.size() * sizeof(decltype(m_Indices)::value_type) < bufferSize) {
		GL_CHECK(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, m_Indices.size() * sizeof(decltype(m_Indices)::value_type), m_Indices.data()));
	} else {
		GL_CHECK(glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_Indices.size() * sizeof(decltype(m_Indices)::value_type), m_Indices.data(), GL_DYNAMIC_DRAW));
	}
}
