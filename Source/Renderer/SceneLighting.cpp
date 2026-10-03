#include "SceneLighting.h"

#include "PostProcessMan.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "RenderTarget.h"
#include "Shader.h"
#include "Texture.h"
#include "GLCheck.h"
#include "Constants.h"
#include "TimerMan.h"

#include "allegro.h"
#include "tracy/Tracy.hpp"
#include "tracy/TracyOpenGL.hpp"

#include <algorithm>
#include <cmath>

using namespace RTE;

namespace {
	/// Terrain material indices that light passes through.
	bool IsOpenMaterial(unsigned char material) { return material == MaterialColorKeys::g_MaterialAir || material == MaterialColorKeys::g_MaterialCavity; }

	glm::vec3 ToLinear(float r, float g, float b) { return glm::vec3(std::pow(r / 255.0F, 2.2F), std::pow(g / 255.0F, 2.2F), std::pow(b / 255.0F, 2.2F)); }
} // namespace

#pragma region GLTarget

void SceneLighting::GLTarget::Create(int width, int height, GLenum internalFormat, GLenum format, GLenum type, GLint filter, GLint wrapS, GLint wrapT, bool withFramebuffer) {
	Destroy();
	Width = width;
	Height = height;
	glGenTextures(1, &Texture);
	glBindTexture(GL_TEXTURE_2D, Texture);
	GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
	if (withFramebuffer) {
		glGenFramebuffers(1, &Framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
		GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Texture, 0));
		glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
		glClear(GL_COLOR_BUFFER_BIT);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}

void SceneLighting::GLTarget::Destroy() {
	if (Framebuffer) {
		glDeleteFramebuffers(1, &Framebuffer);
		Framebuffer = 0;
	}
	if (Texture) {
		glDeleteTextures(1, &Texture);
		Texture = 0;
	}
	Width = 0;
	Height = 0;
}

#pragma endregion

#pragma region Creation and Destruction

SceneLighting::SceneLighting(LightingSettings& settings) :
    m_Settings(settings) {
	LoadShaders();
	CreateGeometry();
}

SceneLighting::~SceneLighting() {
	DestroyWorldResources();
	DestroyScreenResources();
	if (m_FullscreenVAO) {
		glDeleteVertexArrays(1, &m_FullscreenVAO);
		glDeleteBuffers(1, &m_FullscreenVBO);
	}
	if (m_EmptyVAO) {
		glDeleteVertexArrays(1, &m_EmptyVAO);
	}
	if (m_QuadVAO) {
		glDeleteVertexArrays(1, &m_QuadVAO);
		glDeleteBuffers(1, &m_QuadVBO);
		glDeleteBuffers(1, &m_QuadIBO);
	}
}

void SceneLighting::LoadShaders() {
	const std::string fullscreenVertex = "Base.rte/Shaders/Lighting/Fullscreen.vert";
	m_PropagateShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/LightPropagate.frag");
	m_PointLightShader = std::make_unique<Shader>("Base.rte/Shaders/Lighting/PointLight.vert", "Base.rte/Shaders/Lighting/PointLight.frag");
	m_CompositeShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/LightComposite.frag");
	m_EmissiveShader = std::make_unique<Shader>("Base.rte/Shaders/Lighting/Emissive.vert", "Base.rte/Shaders/Lighting/Emissive.frag");
	m_BloomDownsampleShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/BloomDownsample.frag");
	m_BloomUpsampleShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/BloomUpsample.frag");
	m_TonemapShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/Tonemap.frag");
	m_ShockwaveShader = std::make_unique<Shader>("Base.rte/Shaders/Lighting/PointLight.vert", "Base.rte/Shaders/Lighting/Shockwave.frag");
	m_PrecipitationShader = std::make_unique<Shader>("Base.rte/Shaders/Lighting/Precipitation.vert", "Base.rte/Shaders/Lighting/Precipitation.frag");
}

void SceneLighting::CreateGeometry() {
	const float fullscreen[] = {
	    -1.0F, -1.0F, 0.0F, 0.0F, 0.0F,
	    1.0F, -1.0F, 0.0F, 1.0F, 0.0F,
	    -1.0F, 1.0F, 0.0F, 0.0F, 1.0F,
	    1.0F, 1.0F, 0.0F, 1.0F, 1.0F};
	glGenVertexArrays(1, &m_FullscreenVAO);
	glGenBuffers(1, &m_FullscreenVBO);
	glBindVertexArray(m_FullscreenVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_FullscreenVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(fullscreen), fullscreen, GL_STATIC_DRAW);
	glEnableVertexAttribArray(VertexAttribLocation::VERTEX);
	glVertexAttribPointer(VertexAttribLocation::VERTEX, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
	glEnableVertexAttribArray(VertexAttribLocation::TEXTURECOORDINATE);
	glVertexAttribPointer(VertexAttribLocation::TEXTURECOORDINATE, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

	glGenVertexArrays(1, &m_EmptyVAO);

	glGenVertexArrays(1, &m_QuadVAO);
	glGenBuffers(1, &m_QuadVBO);
	glGenBuffers(1, &m_QuadIBO);
	glBindVertexArray(m_QuadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_QuadVBO);
	glEnableVertexAttribArray(VertexAttribLocation::VERTEX);
	glVertexAttribPointer(VertexAttribLocation::VERTEX, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), reinterpret_cast<void*>(offsetof(QuadVertex, X)));
	glEnableVertexAttribArray(VertexAttribLocation::TEXTURECOORDINATE);
	glVertexAttribPointer(VertexAttribLocation::TEXTURECOORDINATE, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), reinterpret_cast<void*>(offsetof(QuadVertex, U)));
	glEnableVertexAttribArray(VertexAttribLocation::COLOR);
	glVertexAttribPointer(VertexAttribLocation::COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), reinterpret_cast<void*>(offsetof(QuadVertex, R)));
	glEnableVertexAttribArray(VertexAttribLocation::NORMAL);
	glVertexAttribPointer(VertexAttribLocation::NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), reinterpret_cast<void*>(offsetof(QuadVertex, CenterX)));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_QuadIBO);
	glBindVertexArray(0);
}

bool SceneLighting::EnsureWorldResources() {
	const Scene* scene = g_SceneMan.GetScene();
	SLTerrain* terrain = scene ? g_SceneMan.GetTerrain() : nullptr;
	BITMAP* materialBitmap = terrain ? terrain->GetMaterialBitmap() : nullptr;
	if (!materialBitmap) {
		return false;
	}
	if (scene == m_WorldScene && materialBitmap == m_WorldMaterialBitmap && materialBitmap->w == m_SceneWidth && materialBitmap->h == m_SceneHeight) {
		return true;
	}
	ZoneScopedN("Build World Light Grid");

	DestroyWorldResources();
	m_WorldScene = scene;
	m_WorldMaterialBitmap = materialBitmap;
	m_SceneWidth = materialBitmap->w;
	m_SceneHeight = materialBitmap->h;
	m_WrapX = g_SceneMan.SceneWrapsX();
	m_WrapY = g_SceneMan.SceneWrapsY();
	// Keep the grid a manageable size on huge scenes.
	m_CellSize = (static_cast<long long>(m_SceneWidth) * m_SceneHeight > 32'000'000LL) ? 8 : 4;
	m_GridWidth = (m_SceneWidth + m_CellSize - 1) / m_CellSize;
	m_GridHeight = (m_SceneHeight + m_CellSize - 1) / m_CellSize;

	m_Occupancy.assign(static_cast<size_t>(m_GridWidth) * m_GridHeight, 0);
	m_Skyline.assign(m_GridWidth, 0.0F);

	GLint wrapS = m_WrapX ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	GLint wrapT = m_WrapY ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	m_OccupancyTexture.Create(m_GridWidth, m_GridHeight, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR, wrapS, wrapT, false);
	m_SkylineTexture.Create(m_GridWidth, 1, GL_R32F, GL_RED, GL_FLOAT, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, false);
	for (GLTarget& skyLight: m_SkyLight) {
		skyLight.Create(m_GridWidth, m_GridHeight, GL_R16F, GL_RED, GL_FLOAT, GL_LINEAR, wrapS, wrapT, true);
	}
	m_CurrentSkyLight = 0;

	RefreshOccupancyRows(0, m_GridHeight);
	UploadOccupancyRows(0, m_GridHeight);
	RecomputeSkyline();
	m_NextRefreshRow = 0;

	// Start with the light already settled instead of it visibly flowing in at scene start.
	PropagateSkyLight(std::max(m_GridWidth, m_GridHeight) / 2);
	return true;
}

void SceneLighting::DestroyWorldResources() {
	m_OccupancyTexture.Destroy();
	m_SkylineTexture.Destroy();
	m_SkyLight[0].Destroy();
	m_SkyLight[1].Destroy();
	m_WorldScene = nullptr;
	m_WorldMaterialBitmap = nullptr;
	m_SceneWidth = 0;
	m_SceneHeight = 0;
}

void SceneLighting::EnsureScreenResources(int width, int height) {
	if (width == m_ScreenWidth && height == m_ScreenHeight && m_HDRScene.Texture) {
		return;
	}
	DestroyScreenResources();
	m_ScreenWidth = width;
	m_ScreenHeight = height;
	m_DynamicLight.Create(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	m_Emissive.Create(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	m_Distortion.Create(width, height, GL_RG16F, GL_RG, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	m_HDRScene.Create(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	int mipWidth = width;
	int mipHeight = height;
	for (GLTarget& mip: m_BloomMips) {
		mipWidth = std::max(1, mipWidth / 2);
		mipHeight = std::max(1, mipHeight / 2);
		mip.Create(mipWidth, mipHeight, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	}
}

void SceneLighting::DestroyScreenResources() {
	m_DynamicLight.Destroy();
	m_Emissive.Destroy();
	m_Distortion.Destroy();
	m_HDRScene.Destroy();
	for (GLTarget& mip: m_BloomMips) {
		mip.Destroy();
	}
	m_ScreenWidth = 0;
	m_ScreenHeight = 0;
}

#pragma endregion

#pragma region World Grid

void SceneLighting::RefreshOccupancyRows(int firstRow, int endRow) {
	ZoneScoped;
	const BITMAP* materialBitmap = static_cast<const BITMAP*>(m_WorldMaterialBitmap);
	// Sample a 2x2 pattern inside each cell rather than every pixel, which is plenty to tell air, surfaces and solid apart.
	const int sampleNear = m_CellSize / 4;
	const int sampleFar = (m_CellSize * 3) / 4;
	for (int row = firstRow; row < endRow; ++row) {
		int y0 = std::min(row * m_CellSize + sampleNear, m_SceneHeight - 1);
		int y1 = std::min(row * m_CellSize + sampleFar, m_SceneHeight - 1);
		const unsigned char* line0 = materialBitmap->line[y0];
		const unsigned char* line1 = materialBitmap->line[y1];
		unsigned char* occupancyRow = &m_Occupancy[static_cast<size_t>(row) * m_GridWidth];
		for (int column = 0; column < m_GridWidth; ++column) {
			int x0 = std::min(column * m_CellSize + sampleNear, m_SceneWidth - 1);
			int x1 = std::min(column * m_CellSize + sampleFar, m_SceneWidth - 1);
			int solidSamples = !IsOpenMaterial(line0[x0]) + !IsOpenMaterial(line0[x1]) + !IsOpenMaterial(line1[x0]) + !IsOpenMaterial(line1[x1]);
			occupancyRow[column] = static_cast<unsigned char>((solidSamples * 255) / 4);
		}
	}
}

void SceneLighting::RecomputeSkyline() {
	ZoneScoped;
	for (int column = 0; column < m_GridWidth; ++column) {
		int row = 0;
		while (row < m_GridHeight && m_Occupancy[static_cast<size_t>(row) * m_GridWidth + column] < 128) {
			++row;
		}
		m_Skyline[column] = static_cast<float>(row) / static_cast<float>(m_GridHeight);
	}
	glBindTexture(GL_TEXTURE_2D, m_SkylineTexture.Texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_GridWidth, 1, GL_RED, GL_FLOAT, m_Skyline.data()));
}

void SceneLighting::UploadOccupancyRows(int firstRow, int endRow) {
	if (endRow <= firstRow) {
		return;
	}
	glBindTexture(GL_TEXTURE_2D, m_OccupancyTexture.Texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, firstRow, m_GridWidth, endRow - firstRow, GL_RED, GL_UNSIGNED_BYTE, &m_Occupancy[static_cast<size_t>(firstRow) * m_GridWidth]));
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void SceneLighting::PropagateSkyLight(int iterations) {
	ZoneScoped;
	TracyGpuZone("Sky Light Propagation");
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, m_GridWidth, m_GridHeight);
	m_PropagateShader->Enable();
	m_PropagateShader->SetInt("rteLight", 0);
	m_PropagateShader->SetInt("rteOccupancy", 1);
	m_PropagateShader->SetInt("rteSkyline", 2);
	m_PropagateShader->SetVector2f("rteGridSize", glm::vec2(m_GridWidth, m_GridHeight));
	m_PropagateShader->SetBool("rteWrapX", m_WrapX);
	m_PropagateShader->SetBool("rteWrapY", m_WrapY);
	m_PropagateShader->SetFloat("rteAirFalloff", m_Settings.AirFalloff);
	m_PropagateShader->SetFloat("rteSolidFalloff", m_Settings.SolidFalloff);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_OccupancyTexture.Texture);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, m_SkylineTexture.Texture);
	for (int i = 0; i < iterations; ++i) {
		const GLTarget& source = m_SkyLight[m_CurrentSkyLight];
		const GLTarget& destination = m_SkyLight[1 - m_CurrentSkyLight];
		glBindFramebuffer(GL_FRAMEBUFFER, destination.Framebuffer);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, source.Texture);
		DrawFullscreen();
		m_CurrentSkyLight = 1 - m_CurrentSkyLight;
	}
	glActiveTexture(GL_TEXTURE0);
}

glm::vec3 SceneLighting::GetDaylightTint(float hours) {
	struct Keyframe {
		float Hours;
		glm::vec3 Tint;
	};
	static const Keyframe keyframes[] = {
	    {0.0F, {0.06F, 0.08F, 0.17F}},
	    {4.5F, {0.06F, 0.08F, 0.17F}},
	    {5.5F, {0.30F, 0.22F, 0.30F}},
	    {6.5F, {0.95F, 0.55F, 0.35F}},
	    {8.0F, {1.0F, 0.92F, 0.82F}},
	    {12.0F, {1.0F, 1.0F, 1.0F}},
	    {16.0F, {1.0F, 0.95F, 0.85F}},
	    {18.0F, {1.0F, 0.62F, 0.38F}},
	    {19.0F, {0.55F, 0.32F, 0.42F}},
	    {20.0F, {0.10F, 0.11F, 0.22F}},
	    {24.0F, {0.06F, 0.08F, 0.17F}}};
	hours = std::fmod(std::fmod(hours, 24.0F) + 24.0F, 24.0F);
	for (size_t i = 1; i < std::size(keyframes); ++i) {
		if (hours <= keyframes[i].Hours) {
			float t = (hours - keyframes[i - 1].Hours) / (keyframes[i].Hours - keyframes[i - 1].Hours);
			// Smooth the transitions between keyframes.
			t = t * t * (3.0F - 2.0F * t);
			return glm::mix(keyframes[i - 1].Tint, keyframes[i].Tint, t);
		}
	}
	return keyframes[0].Tint;
}

void SceneLighting::Update() {
	ZoneScoped;

	// Advance the time of day in sim time, so it pauses with the game.
	long long simUpdateCount = g_TimerMan.GetSimUpdateCount();
	if (m_LastSimUpdateCount >= 0 && m_Settings.DayLengthMinutes > 0.0F) {
		float elapsedSeconds = static_cast<float>(simUpdateCount - m_LastSimUpdateCount) * g_TimerMan.GetDeltaTimeSecs();
		float hoursPerSecond = 24.0F / (m_Settings.DayLengthMinutes * 60.0F);
		m_Settings.TimeOfDay = std::fmod(m_Settings.TimeOfDay + elapsedSeconds * hoursPerSecond, 24.0F);
	}
	m_LastSimUpdateCount = simUpdateCount;
	glm::vec3 daylight = GetDaylightTint(m_Settings.TimeOfDay);
	float dayFactor = std::clamp(glm::dot(daylight, glm::vec3(0.2126F, 0.7152F, 0.0722F)), 0.0F, 1.0F);
	m_EffectiveSky = m_Settings.SkyColor * daylight;
	// Caves get a little darker at night too, but not as much as the outdoors.
	m_EffectiveAmbient = m_Settings.Ambient * (0.6F + 0.4F * dayFactor);

	if (!EnsureWorldResources()) {
		return;
	}
	++m_FrameCounter;

	// Round-robin refresh of the terrain into the grid, so digging and explosions show up in the lighting within a fraction of a second.
	int rowsPerFrame = std::max(4, m_GridHeight / 30);
	int firstRow = m_NextRefreshRow;
	int endRow = std::min(firstRow + rowsPerFrame, m_GridHeight);
	RefreshOccupancyRows(firstRow, endRow);
	UploadOccupancyRows(firstRow, endRow);
	m_NextRefreshRow = (endRow >= m_GridHeight) ? 0 : endRow;
	RecomputeSkyline();

	GLint previousFramebuffer = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
	GLint previousViewport[4];
	glGetIntegerv(GL_VIEWPORT, previousViewport);
	PropagateSkyLight(m_Settings.PropagationIterationsPerFrame);
	glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
	glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
}

#pragma endregion

#pragma region Player Screen

const SceneLighting::GlowInfo& SceneLighting::GetGlowInfo(const BitmapTexture* glowTexture) {
	if (auto cached = m_GlowInfoCache.find(glowTexture); cached != m_GlowInfoCache.end()) {
		return cached->second;
	}
	GlowInfo info{glm::vec3(1.0F), std::max(glowTexture->GetDimensions().w, glowTexture->GetDimensions().h)};
	if (const BITMAP* bitmap = glowTexture->GetBitmap(); bitmap && bitmap_color_depth(const_cast<BITMAP*>(bitmap)) == 32) {
		// Average the glow's visible pixels for the color of the light it casts. Glow art is light on black, so weight by brightness.
		glm::vec3 sum(0.0F);
		float weight = 0.0F;
		for (int y = 0; y < bitmap->h; ++y) {
			const uint32_t* line = reinterpret_cast<const uint32_t*>(bitmap->line[y]);
			for (int x = 0; x < bitmap->w; ++x) {
				uint32_t pixel = line[x];
				glm::vec3 color = ToLinear(static_cast<float>(getr32(pixel)), static_cast<float>(getg32(pixel)), static_cast<float>(getb32(pixel)));
				float brightness = std::max(color.r, std::max(color.g, color.b));
				sum += color * brightness;
				weight += brightness;
			}
		}
		if (weight > 0.0F) {
			glm::vec3 average = sum / weight;
			float peak = std::max(average.r, std::max(average.g, average.b));
			info.LightColor = peak > 0.0F ? average / peak : glm::vec3(1.0F);
		}
	}
	return m_GlowInfoCache.emplace(glowTexture, info).first->second;
}

void SceneLighting::UploadQuads() {
	size_t quadCount = m_QuadVertices.size() / 4;
	if (quadCount * 6 > m_QuadIndexCapacity) {
		m_QuadIndexCapacity = std::max<size_t>(quadCount * 6, 6 * 256);
		std::vector<GLuint> indices(m_QuadIndexCapacity);
		for (size_t quad = 0; quad < m_QuadIndexCapacity / 6; ++quad) {
			GLuint base = static_cast<GLuint>(quad * 4);
			indices[quad * 6 + 0] = base;
			indices[quad * 6 + 1] = base + 1;
			indices[quad * 6 + 2] = base + 2;
			indices[quad * 6 + 3] = base;
			indices[quad * 6 + 4] = base + 2;
			indices[quad * 6 + 5] = base + 3;
		}
		glBindVertexArray(m_QuadVAO);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_QuadIBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);
	}
	glBindBuffer(GL_ARRAY_BUFFER, m_QuadVBO);
	glBufferData(GL_ARRAY_BUFFER, m_QuadVertices.size() * sizeof(QuadVertex), m_QuadVertices.data(), GL_STREAM_DRAW);
}

void SceneLighting::DrawQuads(size_t firstQuad, size_t quadCount) {
	if (quadCount == 0) {
		return;
	}
	glBindVertexArray(m_QuadVAO);
	GL_CHECK(glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(quadCount * 6), GL_UNSIGNED_INT, reinterpret_cast<void*>(firstQuad * 6 * sizeof(GLuint))));
}

void SceneLighting::DrawFullscreen() const {
	glBindVertexArray(m_FullscreenVAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void SceneLighting::LightPlayerScreen(RenderTarget* playerScreen, const Vector& screenOrigin, const std::list<PostEffect>& screenEffects, const std::vector<SceneLight>& screenLights, const std::vector<ScreenShockwave>& screenShockwaves) {
	ZoneScoped;
	TracyGpuZone("Scene Lighting");
	if (!EnsureWorldResources()) {
		return;
	}
	int width = static_cast<int>(playerScreen->GetSize().w);
	int height = static_cast<int>(playerScreen->GetSize().h);
	EnsureScreenResources(width, height);

	GLint previousViewport[4];
	glGetIntegerv(GL_VIEWPORT, previousViewport);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glViewport(0, 0, width, height);

	glm::vec2 screenSize(static_cast<float>(width), static_cast<float>(height));
	glm::vec2 origin(std::floor(screenOrigin.m_X), std::floor(screenOrigin.m_Y));
	glm::vec2 gridWorldSize(static_cast<float>(m_GridWidth * m_CellSize), static_cast<float>(m_GridHeight * m_CellSize));

	// Build light and emissive quads from the glow effects. Lights first, emissives after, so each can be drawn as one range.
	m_QuadVertices.clear();
	auto addQuad = [this](glm::vec2 center, glm::vec2 halfSize, float angle, glm::vec3 color, float radius) {
		const glm::vec2 corners[4] = {{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}};
		float cosAngle = std::cos(angle);
		float sinAngle = std::sin(angle);
		for (const glm::vec2& corner: corners) {
			glm::vec2 local = corner * halfSize;
			glm::vec2 rotated(local.x * cosAngle - local.y * sinAngle, local.x * sinAngle + local.y * cosAngle);
			glm::vec2 position = center + rotated;
			m_QuadVertices.push_back({position.x, position.y, 0.0F, (corner.x + 1.0F) * 0.5F, (corner.y + 1.0F) * 0.5F, color.r, color.g, color.b, 1.0F, center.x, center.y, radius});
		}
	};
	size_t lightCount = 0;
	if (m_Settings.Enabled) {
		for (const PostEffect& effect: screenEffects) {
			if (!effect.m_Bitmap) {
				continue;
			}
			const GlowInfo& glow = GetGlowInfo(effect.m_Bitmap.get());
			float radius = std::max(24.0F, glow.Size * 0.5F * m_Settings.GlowLightRadiusScale);
			glm::vec3 color = glm::mix(glow.LightColor, glm::vec3(1.0F), 0.25F) * (static_cast<float>(effect.m_Strength) / 255.0F) * m_Settings.GlowLightIntensity;
			glm::vec2 center(effect.m_Pos.m_X, effect.m_Pos.m_Y);
			size_t firstVertex = m_QuadVertices.size();
			addQuad(center, glm::vec2(radius), 0.0F, color, radius);
			// The point light shader wants local positions in -1..1 rather than 0..1 UVs.
			for (size_t vertex = firstVertex; vertex < m_QuadVertices.size(); ++vertex) {
				m_QuadVertices[vertex].U = m_QuadVertices[vertex].U * 2.0F - 1.0F;
				m_QuadVertices[vertex].V = m_QuadVertices[vertex].V * 2.0F - 1.0F;
			}
			++lightCount;
		}
		for (const SceneLight& light: screenLights) {
			glm::vec2 center(light.m_Pos.m_X, light.m_Pos.m_Y);
			size_t firstVertex = m_QuadVertices.size();
			addQuad(center, glm::vec2(light.m_Radius), 0.0F, light.m_Color, light.m_Radius);
			for (size_t vertex = firstVertex; vertex < m_QuadVertices.size(); ++vertex) {
				m_QuadVertices[vertex].U = m_QuadVertices[vertex].U * 2.0F - 1.0F;
				m_QuadVertices[vertex].V = m_QuadVertices[vertex].V * 2.0F - 1.0F;
			}
			++lightCount;
		}
	}
	m_LastLightCount = static_cast<int>(lightCount);
	size_t emissiveStart = m_QuadVertices.size() / 4;
	std::vector<GLuint> emissiveTextures;
	for (const PostEffect& effect: screenEffects) {
		if (!effect.m_Bitmap) {
			continue;
		}
		float strength = static_cast<float>(effect.m_Strength) / 255.0F;
		glm::vec2 halfSize(effect.m_Bitmap->GetDimensions().w * 0.5F, effect.m_Bitmap->GetDimensions().h * 0.5F);
		// CC angles are counter-clockwise, screen space is Y down.
		addQuad(glm::vec2(std::floor(effect.m_Pos.m_X), std::floor(effect.m_Pos.m_Y)), halfSize, -effect.m_Angle, glm::vec3(strength), 0.0F);
		emissiveTextures.push_back(effect.m_Bitmap->GetTextureId());
	}
	size_t shockwaveStart = m_QuadVertices.size() / 4;
	if (m_Settings.DistortionEnabled) {
		for (const ScreenShockwave& shockwave: screenShockwaves) {
			// Amplitude and progress travel in the color, radius in the light parameters.
			size_t firstVertex = m_QuadVertices.size();
			addQuad(shockwave.m_Pos, glm::vec2(shockwave.m_Radius), 0.0F, glm::vec3(shockwave.m_Amplitude, shockwave.m_Progress, 0.0F), shockwave.m_Radius);
			for (size_t vertex = firstVertex; vertex < m_QuadVertices.size(); ++vertex) {
				m_QuadVertices[vertex].U = m_QuadVertices[vertex].U * 2.0F - 1.0F;
				m_QuadVertices[vertex].V = m_QuadVertices[vertex].V * 2.0F - 1.0F;
			}
		}
	}
	size_t shockwaveCount = m_QuadVertices.size() / 4 - shockwaveStart;
	UploadQuads();

	// Shockwave displacement.
	glBindFramebuffer(GL_FRAMEBUFFER, m_Distortion.Framebuffer);
	glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
	glClear(GL_COLOR_BUFFER_BIT);
	if (shockwaveCount > 0) {
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE);
		m_ShockwaveShader->Enable();
		m_ShockwaveShader->SetVector2f("rteScreenSize", screenSize);
		DrawQuads(shockwaveStart, shockwaveCount);
		glDisable(GL_BLEND);
	}

	std::shared_ptr<Texture> normals = playerScreen->GetNormalTexture().lock();

	// Dynamic lights.
	glBindFramebuffer(GL_FRAMEBUFFER, m_DynamicLight.Framebuffer);
	glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
	glClear(GL_COLOR_BUFFER_BIT);
	if (lightCount > 0) {
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE);
		m_PointLightShader->Enable();
		m_PointLightShader->SetInt("rteOccupancy", 0);
		m_PointLightShader->SetVector2f("rteScreenSize", screenSize);
		m_PointLightShader->SetVector2f("rteScreenOrigin", origin);
		m_PointLightShader->SetVector2f("rteGridWorldSize", gridWorldSize);
		m_PointLightShader->SetFloat("rteShadowStrength", m_Settings.ShadowStrength);
		m_PointLightShader->SetInt("rteNormals", 1);
		m_PointLightShader->SetFloat("rteEdgeLighting", m_Settings.EdgeLighting);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_OccupancyTexture.Texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, normals ? normals->GetTextureId() : 0);
		glActiveTexture(GL_TEXTURE0);
		DrawQuads(0, lightCount);
		glDisable(GL_BLEND);
	}

	// Glows, screen blended into their own buffer like the original glows.
	glBindFramebuffer(GL_FRAMEBUFFER, m_Emissive.Framebuffer);
	glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
	glClear(GL_COLOR_BUFFER_BIT);
	if (!emissiveTextures.empty()) {
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
		m_EmissiveShader->Enable();
		m_EmissiveShader->SetInt("rteTexture", 0);
		m_EmissiveShader->SetVector2f("rteScreenSize", screenSize);
		glActiveTexture(GL_TEXTURE0);
		size_t runStart = 0;
		for (size_t i = 1; i <= emissiveTextures.size(); ++i) {
			if (i == emissiveTextures.size() || emissiveTextures[i] != emissiveTextures[runStart]) {
				glBindTexture(GL_TEXTURE_2D, emissiveTextures[runStart]);
				DrawQuads(emissiveStart + runStart, i - runStart);
				runStart = i;
			}
		}
		glDisable(GL_BLEND);
	}

	// Composite the lit scene into HDR.
	std::shared_ptr<Texture> albedo = playerScreen->GetColorTexture().lock();
	glBindFramebuffer(GL_FRAMEBUFFER, m_HDRScene.Framebuffer);
	m_CompositeShader->Enable();
	m_CompositeShader->SetInt("rteAlbedo", 0);
	m_CompositeShader->SetInt("rteDynamicLight", 1);
	m_CompositeShader->SetInt("rteSkyLight", 2);
	m_CompositeShader->SetInt("rteSceneDepth", 3);
	m_CompositeShader->SetInt("rteDebugView", m_Settings.DebugView);
	m_CompositeShader->SetInt("rteEmissive", 4);
	m_CompositeShader->SetInt("rteNormals", 5);
	m_CompositeShader->SetFloat("rteEdgeLighting", normals ? m_Settings.EdgeLighting : 0.0F);
	m_CompositeShader->SetFloat("rteEmissiveIntensity", m_Settings.EmissiveIntensity);
	m_CompositeShader->SetFloat("rteMaxDynamicLight", 2.0F);
	// Layers are drawn at depth z mapped linearly through the cameras' ortho projection. Background layers sit at c_BackgroundDepth, terrain background at c_TerrainBGDepth.
	float backgroundThresholdZ = (c_BackgroundDepth + c_TerrainBGDepth) * 0.5F;
	float backgroundThresholdNDC = (2.0F * backgroundThresholdZ - (c_FarDepth + c_NearDepth)) / (c_FarDepth - c_NearDepth);
	m_CompositeShader->SetFloat("rteBackgroundDepth", backgroundThresholdNDC * 0.5F + 0.5F);
	auto depthForZ = [](float z) { return ((2.0F * z - (c_FarDepth + c_NearDepth)) / (c_FarDepth - c_NearDepth)) * 0.5F + 0.5F; };
	m_CompositeShader->SetFloat("rteBackgroundNearDepth", depthForZ(c_BackgroundDepth));
	m_CompositeShader->SetFloat("rteBackgroundFarDepth", depthForZ(c_BackgroundDepth + c_BackgroundDepthRange));
	m_CompositeShader->SetVector3f("rteAtmosphereColor", m_Settings.AtmosphereColor * GetDaylightTint(m_Settings.TimeOfDay));
	m_CompositeShader->SetFloat("rteAtmosphereHaze", m_Settings.Enabled ? m_Settings.AtmosphereHaze : 0.0F);
	m_CompositeShader->SetVector3f("rteBackgroundLight", m_Settings.Enabled ? m_EffectiveSky : glm::vec3(1.0F));
	m_CompositeShader->SetVector2f("rteScreenSize", screenSize);
	m_CompositeShader->SetVector2f("rteScreenOrigin", origin);
	m_CompositeShader->SetVector2f("rteGridWorldSize", gridWorldSize);
	m_CompositeShader->SetVector3f("rteAmbient", m_Settings.Enabled ? m_EffectiveAmbient : glm::vec3(1.0F));
	m_CompositeShader->SetVector3f("rteSkyColor", m_Settings.Enabled ? m_EffectiveSky : glm::vec3(1.0F));
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, albedo ? albedo->GetTextureId() : 0);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_DynamicLight.Texture);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, m_SkyLight[m_CurrentSkyLight].Texture);
	std::shared_ptr<DepthTexture> sceneDepth = playerScreen->GetDepthTexture().lock();
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, sceneDepth ? sceneDepth->GetTextureId() : 0);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, m_Emissive.Texture);
	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_2D, normals ? normals->GetTextureId() : 0);
	DrawFullscreen();

	// Rain or snow, lit by the sky, over the lit scene.
	if (m_Settings.WeatherType > 0 && m_Settings.WeatherIntensity > 0.0F) {
		TracyGpuZone("Precipitation");
		int dropCount = static_cast<int>(m_Settings.WeatherIntensity * (m_Settings.WeatherType == 2 ? 1500.0F : 2500.0F) * (static_cast<float>(width * height) / (960.0F * 540.0F)));
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		m_PrecipitationShader->Enable();
		m_PrecipitationShader->SetVector2f("rteScreenSize", screenSize);
		m_PrecipitationShader->SetVector2f("rteScreenOrigin", origin);
		m_PrecipitationShader->SetFloat("rteTime", PostProcessMan::GetSmoothSimTime());
		m_PrecipitationShader->SetInt("rteType", m_Settings.WeatherType);
		m_PrecipitationShader->SetFloat("rteWind", m_Settings.Wind);
		m_PrecipitationShader->SetInt("rteSkyline", 0);
		m_PrecipitationShader->SetVector2f("rteGridWorldSize", gridWorldSize);
		m_PrecipitationShader->SetVector3f("rteSkyLight", m_Settings.Enabled ? m_EffectiveSky : glm::vec3(1.0F));
		m_PrecipitationShader->SetFloat("rteIntensity", std::clamp(0.6F + 0.4F * m_Settings.WeatherIntensity, 0.0F, 1.0F));
		m_PrecipitationShader->SetInt("rteDynamicLight", 1);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_SkylineTexture.Texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m_DynamicLight.Texture);
		glActiveTexture(GL_TEXTURE0);
		glBindVertexArray(m_EmptyVAO);
		glDrawArrays(GL_TRIANGLES, 0, dropCount * 6);
		glDisable(GL_BLEND);
	}

	// Bloom.
	if (m_Settings.BloomEnabled) {
		TracyGpuZone("Bloom");
		m_BloomDownsampleShader->Enable();
		m_BloomDownsampleShader->SetInt("rteSource", 0);
		m_BloomDownsampleShader->SetFloat("rteThreshold", m_Settings.BloomThreshold);
		m_BloomDownsampleShader->SetFloat("rteKnee", m_Settings.BloomKnee);
		glActiveTexture(GL_TEXTURE0);
		for (int mip = 0; mip < c_BloomMipCount; ++mip) {
			const GLTarget& source = mip == 0 ? m_HDRScene : m_BloomMips[mip - 1];
			glBindFramebuffer(GL_FRAMEBUFFER, m_BloomMips[mip].Framebuffer);
			glViewport(0, 0, m_BloomMips[mip].Width, m_BloomMips[mip].Height);
			m_BloomDownsampleShader->SetBool("rteFirstPass", mip == 0);
			m_BloomDownsampleShader->SetVector2f("rteSourceTexelSize", glm::vec2(1.0F / source.Width, 1.0F / source.Height));
			glBindTexture(GL_TEXTURE_2D, source.Texture);
			DrawFullscreen();
		}
		m_BloomUpsampleShader->Enable();
		m_BloomUpsampleShader->SetInt("rteSource", 0);
		m_BloomUpsampleShader->SetFloat("rteRadius", 1.0F);
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE);
		for (int mip = c_BloomMipCount - 1; mip > 0; --mip) {
			const GLTarget& source = m_BloomMips[mip];
			const GLTarget& destination = m_BloomMips[mip - 1];
			glBindFramebuffer(GL_FRAMEBUFFER, destination.Framebuffer);
			glViewport(0, 0, destination.Width, destination.Height);
			m_BloomUpsampleShader->SetVector2f("rteSourceTexelSize", glm::vec2(1.0F / source.Width, 1.0F / source.Height));
			m_BloomUpsampleShader->SetVector2f("rteTargetTexelSize", glm::vec2(1.0F / destination.Width, 1.0F / destination.Height));
			glBindTexture(GL_TEXTURE_2D, source.Texture);
			DrawFullscreen();
		}
		glDisable(GL_BLEND);
		glViewport(0, 0, width, height);
	}

	// Tonemap back into the player screen.
	playerScreen->Bind();
	glViewport(0, 0, width, height);
	m_TonemapShader->Enable();
	m_TonemapShader->SetInt("rteScene", 0);
	m_TonemapShader->SetInt("rteBloom", 1);
	m_TonemapShader->SetVector2f("rteScreenSize", screenSize);
	m_TonemapShader->SetFloat("rteBloomIntensity", m_Settings.BloomEnabled ? m_Settings.BloomIntensity : 0.0F);
	m_TonemapShader->SetFloat("rteExposure", m_Settings.Exposure);
	m_TonemapShader->SetFloat("rteShoulderStart", m_Settings.ShoulderStart);
	m_TonemapShader->SetFloat("rteVignette", m_Settings.Vignette);
	m_TonemapShader->SetFloat("rteSaturation", m_Settings.Saturation);
	m_TonemapShader->SetInt("rteDistortion", 2);
	m_TonemapShader->SetInt("rteEmissive", 3);
	m_TonemapShader->SetBool("rteDistortionEnabled", m_Settings.DistortionEnabled);
	m_TonemapShader->SetFloat("rteHeatHaze", m_Settings.HeatHaze);
	m_TonemapShader->SetFloat("rteTime", PostProcessMan::GetSmoothSimTime());
	m_TonemapShader->SetInt("rteDebugView", m_Settings.DebugView);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_HDRScene.Texture);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_BloomMips[0].Texture);
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, m_Distortion.Texture);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, m_Emissive.Texture);
	DrawFullscreen();

	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(0);
	glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
}

#pragma endregion
