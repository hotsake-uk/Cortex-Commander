#pragma once
#include "Shader.h"
#include "BlendMode.h"
#include "Constants.h"
#include "DrawCall.h"

#include "glad/gl.h"
#include "glm/glm.hpp"
#include <optional>
#include <vector>

namespace RTE {
	class Shader;

	struct VertexBuffer {
		std::vector<Vertex> m_Vertices{};
		std::vector<unsigned int> m_Indices{};
		GLuint m_VertexArray{0};
		GLuint m_VertexBuffer{0};
		GLuint m_IndexBuffer{0};
		int m_BufferElements{c_DefaultBatchVAOElements};
		~VertexBuffer();
		VertexBuffer();
		VertexBuffer(int bufferSize);
		VertexBuffer(VertexBuffer&& vertexBuffer) = default;

		void UpdateBuffers();

		VertexBuffer& operator=(VertexBuffer&& rhs) = default;

	private:
		void InitializeBuffers();
		VertexBuffer(const VertexBuffer&) = delete;
		VertexBuffer& operator=(const VertexBuffer&) = delete;
	};

	/// Render batch based on raysan5's raylib
	struct RenderBatch {
	public:
		constexpr static float c_DrawDepthIncrement = -(c_FarDepth - c_NearDepth) / 16777216.0f;
		VertexBuffer m_VertexBuffers{};
		std::vector<std::shared_ptr<DrawCall>> m_DrawCalls{};
		std::vector<std::shared_ptr<DrawCall>> m_FreeDrawCalls{}; //!< Finished draw calls kept for reuse, so drawing doesn't allocate every frame.
		float m_CurrentDepth{0.0f};
		float m_CurrentZ{0.0f};
		glm::u8vec4 m_CurrentSurface{0, 0, 0, 0}; //!< The surface new vertices are given (see Vertex::m_Surface). Zero for anything that isn't a solid object.
		bool m_ShadowCasting{true}; //!< Whether surfaces set from now on may be marked as casting shadows. Turned off around things like muzzle flashes.
		const DrawCall* m_OpenPixelDraw{nullptr}; //!< The last draw call, if it is a run of single pixels that the next pixel can join (see Draw::PixelBatched). Any other draw closes it.
		const Shader* m_CurrentShader{nullptr};
		std::vector<std::shared_ptr<UniformValueType>> m_CurrentUniforms{};
		glm::mat4 m_CurrentView{1.0f};
		glm::mat4 m_CurrentProjection{1.0f};
		const Camera* m_CurrentCamera{nullptr};
		BlendMode m_CurrentBlendMode{Blend::ALPHA};

		/// Constructor.
		RenderBatch();

		/// Resets DrawDepth, draw calls, vertices and indices and resets to default values.
		void BeginFrame();

		/// Collects draw calls, uploads vertices. TODO: split drawcalls by shader/alpha.
		void EndFrame();

		/// Draws current draw calls.
		void Render();

		/// Clears draw calls and vertex buffer.
		void ClearDraws();

		/// Collect draw calls, render and clear.
		void Flush() {
			EndFrame();
			Render();
			ClearDraws();
		}

		/// Moves the current draw calls to the free list for reuse.
		void RecycleDrawCalls();

	private:
		void ApplyDrawCalls();
	};
} // namespace RTE
