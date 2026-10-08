#include "SceneLighting.h"
#include "EffectsParticles.h"
#include "TerrainFire.h"

#include "PostProcessMan.h"
#include "PerformanceMan.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "Material.h"
#include "RenderTarget.h"
#include "Shader.h"
#include "Texture.h"
#include "GLCheck.h"
#include "Constants.h"
#include "TimerMan.h"
#include "RenderMan.h"
#include <array>

#include "allegro.h"
#include "tracy/Tracy.hpp"
#include "tracy/TracyOpenGL.hpp"

#include <algorithm>
#include <chrono>
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
	m_OccluderSeedShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/OccluderSeed.frag");
	m_OccluderJumpShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/OccluderJump.frag");
	m_SurfaceRoundShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/SurfaceRound.frag");
	m_CompositeShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/LightComposite.frag");
	m_EmissiveShader = std::make_unique<Shader>("Base.rte/Shaders/Lighting/Emissive.vert", "Base.rte/Shaders/Lighting/Emissive.frag");
	m_BloomDownsampleShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/BloomDownsample.frag");
	m_BloomUpsampleShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/BloomUpsample.frag");
	m_TonemapShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/Tonemap.frag");
	m_LuminanceShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/Luminance.frag");
	m_RCSceneShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/RCScene.frag");
	m_LitParticleShader = std::make_unique<Shader>("Base.rte/Shaders/Lighting/Emissive.vert", "Base.rte/Shaders/Lighting/LitParticle.frag");
	m_SmokeScatterShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/SmokeScatter.frag");
	m_RCCascadeShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/RCCascade.frag");
	m_RCIrradianceShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/RCIrradiance.frag");
	m_ExposureAdaptShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/ExposureAdapt.frag");
	m_ShockwaveShader = std::make_unique<Shader>("Base.rte/Shaders/Lighting/PointLight.vert", "Base.rte/Shaders/Lighting/Shockwave.frag");
	m_PrecipitationShader = std::make_unique<Shader>("Base.rte/Shaders/Lighting/Precipitation.vert", "Base.rte/Shaders/Lighting/Precipitation.frag");
	m_GodRaysShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/GodRays.frag");
	m_GodRaysApplyShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/GodRaysApply.frag");
	m_RainSplashShader = std::make_unique<Shader>(fullscreenVertex, "Base.rte/Shaders/Lighting/RainSplash.frag");
	m_ScorchShader = std::make_unique<Shader>("Base.rte/Shaders/Lighting/PointLight.vert", "Base.rte/Shaders/Lighting/Scorch.frag");
	m_StainShader = std::make_unique<Shader>("Base.rte/Shaders/Lighting/PointLight.vert", "Base.rte/Shaders/Lighting/Stain.frag");
	m_TerrainShader = std::make_unique<Shader>("Base.rte/Shaders/TerrainLayer.vert", "Base.rte/Shaders/Terrain.frag");
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
	glEnableVertexAttribArray(VertexAttribLocation::LIGHTCONE);
	glVertexAttribPointer(VertexAttribLocation::LIGHTCONE, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), reinterpret_cast<void*>(offsetof(QuadVertex, ConeX)));
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
	unsigned int sceneGeneration = g_SceneMan.GetSceneGeneration();
	if (scene == m_WorldScene && sceneGeneration == m_WorldSceneGeneration && materialBitmap == m_WorldMaterialBitmap && materialBitmap->w == m_SceneWidth && materialBitmap->h == m_SceneHeight) {
		return true;
	}
	ZoneScopedN("Build World Light Grid");

	DestroyWorldResources();
	m_WorldScene = scene;
	m_WorldSceneGeneration = sceneGeneration;
	m_WorldMaterialBitmap = materialBitmap;
	m_SceneWidth = materialBitmap->w;
	m_SceneHeight = materialBitmap->h;
	m_WrapX = g_SceneMan.SceneWrapsX();
	m_WrapY = g_SceneMan.SceneWrapsY();
	// Keep the grid a manageable size on huge scenes.
	m_CellSize = (static_cast<long long>(m_SceneWidth) * m_SceneHeight > 32'000'000LL) ? 8 : 4;
	m_GridWidth = (m_SceneWidth + m_CellSize - 1) / m_CellSize;
	m_GridHeight = (m_SceneHeight + m_CellSize - 1) / m_CellSize;

	m_Occupancy.assign(static_cast<size_t>(m_GridWidth) * m_GridHeight * 4, 0);
	// What each terrain material looks like, for the grid's material values.
	m_MaterialMetalness.fill(0);
	m_MaterialGloss.fill(0);
	m_MaterialLightBlock.fill(255);
	for (int id = 1; id < 256; ++id) {
		const Material* material = g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(id));
		if (material && material->GetIndex() == id) {
			m_MaterialMetalness[id] = static_cast<unsigned char>(std::clamp(material->GetMetalness(), 0.0F, 1.0F) * 255.0F);
			m_MaterialGloss[id] = static_cast<unsigned char>(std::clamp(material->GetGloss(), 0.0F, 1.0F) * 255.0F);
			// (Small numbers: a lamp's light is sampled at a fixed number of places on its way to each pixel, so even these take a third or so off it across a pool.)
			// Clear liquids let light through, dimming it with depth: sky light reaches down into a pool, and a lamp or a fire under water lights the water around it.
			const std::string& materialName = material->GetPresetName();
			m_MaterialLightBlock[id] = materialName == "Water" ? 8 : (materialName == "Acid" ? 14 : (materialName == "Ice" ? 30 : (materialName == "Glass" ? 10 : (materialName == "Oil" ? 120 : 255))));
		}
	}
	m_Skyline.assign(m_GridWidth, 0.0F);

	GLint wrapS = m_WrapX ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	GLint wrapT = m_WrapY ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	m_OccupancyTexture.Create(m_GridWidth, m_GridHeight, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, wrapS, wrapT, false);
	m_SkylineTexture.Create(m_GridWidth, 1, GL_R32F, GL_RED, GL_FLOAT, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, false);
	for (GLTarget& skyLight: m_SkyLight) {
		skyLight.Create(m_GridWidth, m_GridHeight, GL_RG16F, GL_RG, GL_FLOAT, GL_LINEAR, wrapS, wrapT, true);
	}
	m_CurrentSkyLight = 0;

	m_ScorchCellSize = (static_cast<long long>(m_SceneWidth) * m_SceneHeight > 32'000'000LL) ? 4 : 2;
	m_Scorch.Create((m_SceneWidth + m_ScorchCellSize - 1) / m_ScorchCellSize, (m_SceneHeight + m_ScorchCellSize - 1) / m_ScorchCellSize, GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR, wrapS, wrapT, true);
	m_Stains.Create(m_Scorch.Width, m_Scorch.Height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR, wrapS, wrapT, true);
	// Scorch marks from before the grid was built belong to the last scene.
	g_PostProcessMan.TakePendingScorchMarks();
	g_PostProcessMan.ClearHotScorchMarks();

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
	m_Scorch.Destroy();
	m_Stains.Destroy();
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
	for (GLTarget& seeds: m_OccluderSeeds) {
		seeds.Create(width, height, GL_RG16F, GL_RG, GL_FLOAT, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	}
	m_RoundedNormals.Create(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	m_Emissive.Create(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	m_Distortion.Create(width, height, GL_RG16F, GL_RG, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	m_GodRays.Create(std::max(1, width / 2), std::max(1, height / 2), GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	int indirectWidth = width;
	int indirectHeight = height;
	for (GLTarget& mip: m_IndirectMips) {
		indirectWidth = std::max(1, indirectWidth / 2);
		indirectHeight = std::max(1, indirectHeight / 2);
		mip.Create(indirectWidth, indirectHeight, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	}
	for (int screen = 0; screen < c_MaxScreens; ++screen) {
		m_IndirectHistory[screen].Create(indirectWidth, indirectHeight, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
		m_IndirectHistoryValid[screen] = false;
	}
	m_HDRScene.Create(width, height, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	int mipWidth = width;
	int mipHeight = height;
	for (GLTarget& mip: m_BloomMips) {
		mipWidth = std::max(1, mipWidth / 2);
		mipHeight = std::max(1, mipHeight / 2);
		mip.Create(mipWidth, mipHeight, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	}
	int rcWidth = std::max(4, width / 2);
	int rcHeight = std::max(4, height / 2);
	m_RCScene.Create(rcWidth, rcHeight, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	m_SmokeDensity.Create(rcWidth, rcHeight, GL_R16F, GL_RED, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	for (GLTarget& cascade: m_RCCascades) {
		cascade.Create(rcWidth, rcHeight, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	}
	m_RCIrradiance.Create(rcWidth / 2, rcHeight / 2, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	for (int screen = 0; screen < c_MaxScreens; ++screen) {
		m_RCPreviousLit[screen].Create(rcWidth, rcHeight, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
		m_RCPreviousValid[screen] = false;
	}
	// 64x32 log luminance; its mip chain down to 1x1 is the average.
	m_Luminance.Create(64, 32, GL_R16F, GL_RED, GL_FLOAT, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
	glBindTexture(GL_TEXTURE_2D, m_Luminance.Texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
	glGenerateMipmap(GL_TEXTURE_2D);
	m_LuminanceMaxLod = 6;
	for (int screen = 0; screen < c_MaxScreens; ++screen) {
		for (GLTarget& adapted: m_AdaptedLuminance[screen]) {
			adapted.Create(1, 1, GL_R32F, GL_RED, GL_FLOAT, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, true);
		}
		m_AdaptedLuminanceValid[screen] = false;
	}
}

void SceneLighting::DestroyScreenResources() {
	m_DynamicLight.Destroy();
	for (GLTarget& seeds: m_OccluderSeeds) {
		seeds.Destroy();
	}
	m_RoundedNormals.Destroy();
	m_Emissive.Destroy();
	m_Distortion.Destroy();
	m_GodRays.Destroy();
	for (GLTarget& mip: m_IndirectMips) {
		mip.Destroy();
	}
	for (int screen = 0; screen < c_MaxScreens; ++screen) {
		m_IndirectHistory[screen].Destroy();
		m_IndirectHistoryValid[screen] = false;
	}
	m_HDRScene.Destroy();
	for (GLTarget& mip: m_BloomMips) {
		mip.Destroy();
	}
	m_Luminance.Destroy();
	m_RCScene.Destroy();
	m_SmokeDensity.Destroy();
	for (GLTarget& cascade: m_RCCascades) {
		cascade.Destroy();
	}
	m_RCIrradiance.Destroy();
	for (int screen = 0; screen < c_MaxScreens; ++screen) {
		m_RCPreviousLit[screen].Destroy();
		m_RCPreviousValid[screen] = false;
	}
	for (int screen = 0; screen < c_MaxScreens; ++screen) {
		for (GLTarget& adapted: m_AdaptedLuminance[screen]) {
			adapted.Destroy();
		}
		m_AdaptedLuminanceValid[screen] = false;
	}
	m_ScreenWidth = 0;
	m_ScreenHeight = 0;
}

#pragma endregion

#pragma region World Grid

void SceneLighting::RefreshOccupancyRows(int firstRow, int endRow, int firstColumn, int endColumn) {
	if (endColumn < 0 || endColumn > m_GridWidth) {
		endColumn = m_GridWidth;
	}
	firstColumn = std::clamp(firstColumn, 0, endColumn);
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
		unsigned char* occupancyRow = &m_Occupancy[static_cast<size_t>(row) * m_GridWidth * 4];
		for (int column = firstColumn; column < endColumn; ++column) {
			int x0 = std::min(column * m_CellSize + sampleNear, m_SceneWidth - 1);
			int x1 = std::min(column * m_CellSize + sampleFar, m_SceneWidth - 1);
			const unsigned char materials[4] = {line0[x0], line0[x1], line1[x0], line1[x1]};
			int solidSamples = 0;
			int metalness = 0;
			int gloss = 0;
			int lightBlock = 0;
			for (unsigned char material: materials) {
				if (!IsOpenMaterial(material)) {
					++solidSamples;
					lightBlock += m_MaterialLightBlock[material];
					metalness += m_MaterialMetalness[material];
					gloss += m_MaterialGloss[material];
				}
			}
			unsigned char* cell = occupancyRow + column * 4;
			// R is how much the cell stops light; A is how much of it is filled with anything at all (what rain and snow can't fall through).
			cell[0] = static_cast<unsigned char>(lightBlock / 4);
			// What the solid part of the cell is made of, so a thin metal plate isn't diluted by the air beside it.
			cell[1] = static_cast<unsigned char>(solidSamples > 0 ? metalness / solidSamples : 0);
			cell[2] = static_cast<unsigned char>(solidSamples > 0 ? gloss / solidSamples : 0);
			cell[3] = static_cast<unsigned char>((solidSamples * 255) / 4);
		}
	}
}

void SceneLighting::RecomputeSkyline() {
	ZoneScoped;
	for (int column = 0; column < m_GridWidth; ++column) {
		int row = 0;
		// (By how full the cell is, so the open sky stops at the surface of water and light dims with depth below it.)
		while (row < m_GridHeight && m_Occupancy[(static_cast<size_t>(row) * m_GridWidth + column) * 4 + 3] < 128) {
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
	GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, firstRow, m_GridWidth, endRow - firstRow, GL_RGBA, GL_UNSIGNED_BYTE, &m_Occupancy[static_cast<size_t>(firstRow) * m_GridWidth * 4]));
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
	// One cell at a time towards the sun: a whole cell along the longer axis, so the sample always lands in the next row or column.
	m_PropagateShader->SetVector2f("rteSunStep", m_SunDirection / std::max(std::abs(m_SunDirection.x), std::abs(m_SunDirection.y)));
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

void SceneLighting::ReadAutoExposure(float& averageLuminance, float& autoExposure) const {
	averageLuminance = 0.0F;
	autoExposure = 1.0F;
	if (!m_AdaptedLuminanceValid[0] || !m_AdaptedLuminance[0][0].Texture) {
		return;
	}
	float logLuminance = 0.0F;
	glBindTexture(GL_TEXTURE_2D, m_AdaptedLuminance[0][m_AdaptedLuminanceCurrent[0]].Texture);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, &logLuminance);
	glBindTexture(GL_TEXTURE_2D, 0);
	averageLuminance = std::exp(logLuminance);
	// Same as Tonemap.frag.
	float target = std::clamp(averageLuminance, std::min(m_Settings.AutoExposureLow * m_NightDim, m_Settings.AutoExposureHigh), m_Settings.AutoExposureHigh); // (Low never over high: std::clamp aborts on that with libstdc++'s checks.)
	autoExposure = std::clamp(std::pow(target / std::max(averageLuminance, 0.0001F), m_Settings.AutoExposure), 0.5F, 2.0F);
}

void SceneLighting::StampScorchMarks() {
	std::vector<PostProcessMan::ScorchMark> marks = g_PostProcessMan.TakePendingScorchMarks();
	if (marks.empty() || !m_Scorch.Framebuffer) {
		return;
	}
	ZoneScoped;
	TracyGpuZone("Scorch Marks");
	m_QuadVertices.clear();
	float cell = static_cast<float>(m_ScorchCellSize);
	auto addMark = [this, cell](glm::vec2 center, float radius, float darkness) {
		const glm::vec2 corners[4] = {{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}};
		for (const glm::vec2& corner: corners) {
			glm::vec2 position = center + corner * radius;
			m_QuadVertices.push_back({position.x, position.y, 0.0F, corner.x, corner.y, darkness, 0.0F, 0.0F, 1.0F, center.x * cell, center.y * cell, radius, 1.0F, 0.0F, -2.0F});
		}
	};
	for (const PostProcessMan::ScorchMark& mark: marks) {
		glm::vec2 center(mark.m_Pos.m_X / cell, mark.m_Pos.m_Y / cell);
		float radius = mark.m_Radius / cell;
		addMark(center, radius, mark.m_Darkness);
		// Wrapped copies so marks across the seam of wrapping scenes show on both sides.
		if (m_WrapX) {
			addMark(center + glm::vec2(static_cast<float>(m_Scorch.Width), 0.0F), radius, mark.m_Darkness);
			addMark(center - glm::vec2(static_cast<float>(m_Scorch.Width), 0.0F), radius, mark.m_Darkness);
		}
	}
	UploadQuads();
	glBindFramebuffer(GL_FRAMEBUFFER, m_Scorch.Framebuffer);
	glViewport(0, 0, m_Scorch.Width, m_Scorch.Height);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	// Max, so repeated explosions don't pile up into flat black.
	glBlendEquation(GL_MAX);
	glBlendFunc(GL_ONE, GL_ONE);
	m_ScorchShader->Enable();
	m_ScorchShader->SetVector2f("rteScreenSize", glm::vec2(m_Scorch.Width, m_Scorch.Height));
	DrawQuads(0, m_QuadVertices.size() / 4);
	glBlendEquation(GL_FUNC_ADD);
	glDisable(GL_BLEND);
	glBindVertexArray(0);
}

void SceneLighting::StampStains() {
	std::vector<EffectsParticles::Stain> stains = EffectsParticles::TakeStains();
	if (stains.empty() || !m_Stains.Framebuffer || !m_Settings.Stains) {
		return;
	}
	ZoneScoped;
	TracyGpuZone("Stains");
	m_QuadVertices.clear();
	float cell = static_cast<float>(m_ScorchCellSize);
	auto addSplat = [this, cell](glm::vec2 center, float radius, const glm::vec3& color) {
		const glm::vec2 corners[4] = {{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}};
		for (const glm::vec2& corner: corners) {
			glm::vec2 position = center + corner * radius;
			m_QuadVertices.push_back({position.x, position.y, 0.0F, corner.x, corner.y, color.r, color.g, color.b, 0.55F, center.x * cell, center.y * cell, radius, 1.0F, 0.0F, -2.0F});
		}
	};
	for (const EffectsParticles::Stain& stain: stains) {
		glm::vec2 center = stain.Position / cell;
		float radius = std::max(1.0F, stain.Radius / cell);
		addSplat(center, radius, stain.Color);
		if (m_WrapX) {
			addSplat(center + glm::vec2(static_cast<float>(m_Stains.Width), 0.0F), radius, stain.Color);
			addSplat(center - glm::vec2(static_cast<float>(m_Stains.Width), 0.0F), radius, stain.Color);
		}
	}
	UploadQuads();
	glBindFramebuffer(GL_FRAMEBUFFER, m_Stains.Framebuffer);
	glViewport(0, 0, m_Stains.Width, m_Stains.Height);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	// New stains cover old ones; coverage builds up towards fully stained.
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	m_StainShader->Enable();
	m_StainShader->SetVector2f("rteScreenSize", glm::vec2(m_Stains.Width, m_Stains.Height));
	DrawQuads(0, m_QuadVertices.size() / 4);
	glDisable(GL_BLEND);
	glBindVertexArray(0);
}

const Shader* SceneLighting::PrepareTerrainShader() {
	if (!m_Scorch.Texture) {
		return nullptr;
	}
	m_TerrainShader->Enable();
	m_TerrainShader->SetInt("rteScorch", 3);
	m_TerrainShader->SetBool("rteScorchEnabled", m_Settings.ScorchMarks);
	m_TerrainShader->SetVector2f("rteScorchWorldSize", glm::vec2(static_cast<float>(m_Scorch.Width * m_ScorchCellSize), static_cast<float>(m_Scorch.Height * m_ScorchCellSize)));
	g_RenderMan.SetGlobalTexture(3, m_Scorch.Texture);
	g_RenderMan.SetGlobalTexture(4, m_Stains.Texture);
	g_RenderMan.SetGlobalTexture(5, m_SkylineTexture.Texture);
	g_RenderMan.SetGlobalTexture(6, m_OccupancyTexture.Texture);
	m_TerrainShader->SetInt("rteWorldGrid", 6);
	m_TerrainShader->SetFloat("rteRelief", m_Settings.Enabled ? m_Settings.Relief : 0.0F);
	// The sprite shader reads sprites' own shading as relief too.
	if (const Shader* spriteShader = g_RenderMan.GetDefaultShader()) {
		spriteShader->Enable();
		spriteShader->SetFloat("rteRelief", m_Settings.Enabled ? m_Settings.Relief : 0.0F);
		m_TerrainShader->Enable();
	}
	m_TerrainShader->SetBool("rteLivingWorld", m_Settings.LivingWorld);
	m_TerrainShader->SetFloat("rteTime", PostProcessMan::GetSmoothSimTime());
	m_TerrainShader->SetFloat("rteWind", m_Settings.Wind);
	m_TerrainShader->SetFloat("rteSnowCover", m_Settings.LivingWorld ? m_SnowCover : 0.0F);
	m_TerrainShader->SetFloat("rteWetness", m_Settings.LivingWorld ? m_Wetness : 0.0F);
	m_TerrainShader->SetFloat("rteWaterFoam", m_Settings.Enabled ? m_Settings.WaterFoam : 0.0F);
	m_TerrainShader->SetFloat("rteWaterFoamStray", std::clamp(m_Settings.WaterFoamStray, 0.0F, 1.0F));
	m_TerrainShader->SetFloat("rteWaterFoamBright", m_Settings.WaterFoamBrightness);
	m_TerrainShader->SetFloat("rteWaterFoamGlow", m_Settings.WaterFoamGlow);
	m_TerrainShader->SetFloat("rteWaterFoamBubbles", std::clamp(m_Settings.WaterFoamBubbles, 0.0F, 2.0F));
	// Snow drifts on the wind far more than rain does. Capped well short of level, so cover still only lies on what's under some sky.
	{
		glm::vec2 fall = m_Settings.WeatherType == 2 ? glm::vec2(m_Settings.Wind * 0.6F, 45.0F) : glm::vec2(m_Settings.Wind, 640.0F);
		fall.x = std::clamp(fall.x, -fall.y * 2.0F, fall.y * 2.0F);
		m_TerrainShader->SetVector2f("rteWeatherFall", glm::normalize(fall));
	}
	m_TerrainShader->SetInt("rteSkyline", 5);
	m_TerrainShader->SetVector2f("rteGridWorldSize", glm::vec2(static_cast<float>(m_GridWidth * m_CellSize), static_cast<float>(m_GridHeight * m_CellSize)));
	{
		std::vector<glm::vec4> blasts;
		if (m_Settings.LivingWorld) {
			g_PostProcessMan.GetActiveShockwaves(blasts);
		}
		constexpr size_t maxBlasts = 8;
		if (blasts.size() > maxBlasts) {
			blasts.resize(maxBlasts);
		}
		m_TerrainShader->SetInt("rteBlastCount", static_cast<int>(blasts.size()));
		if (!blasts.empty()) {
			glUniform4fv(m_TerrainShader->GetUniformLocation("rteBlasts"), static_cast<GLsizei>(blasts.size()), &blasts[0].x);
		}
	}
	m_TerrainShader->SetInt("rteStains", 4);
	m_TerrainShader->SetBool("rteStainsEnabled", m_Settings.Stains);

	// Hot spots: the most recent marks, cooling over HotSpotSeconds.
	const std::vector<PostProcessMan::ScorchMark>& hotMarks = g_PostProcessMan.GetHotScorchMarks(std::max(m_Settings.HotSpotSeconds, 0.01F));
	float now = PostProcessMan::GetSmoothSimTime();
	std::array<glm::vec4, 16> hotSpots{};
	int hotSpotCount = 0;
	for (auto mark = hotMarks.rbegin(); mark != hotMarks.rend() && hotSpotCount < static_cast<int>(hotSpots.size()); ++mark) {
		float age = (now - mark->m_StartTime) / std::max(m_Settings.HotSpotSeconds, 0.01F);
		float heat = std::clamp(1.0F - age, 0.0F, 1.0F);
		heat *= heat;
		if (heat > 0.0F) {
			hotSpots[hotSpotCount++] = glm::vec4(mark->m_Pos.m_X, mark->m_Pos.m_Y, mark->m_Radius * 1.1F, heat);
		}
	}
	m_TerrainShader->SetInt("rteHotSpotCount", hotSpotCount);
	if (hotSpotCount > 0) {
		glUniform4fv(m_TerrainShader->GetUniformLocation("rteHotSpots"), hotSpotCount, &hotSpots[0].x);
	}
	return m_TerrainShader.get();
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
	// Deep night is darker than early night by a chosen amount: from nightfall the light on the scene falls away to its least at eleven, and comes back from two until first light.
	float hourNow = m_Settings.TimeOfDay;
	float deepNight = hourNow >= 19.6F ? glm::smoothstep(19.6F, 23.0F, hourNow) : (hourNow <= 4.6F ? 1.0F - glm::smoothstep(2.0F, 4.6F, hourNow) : 0.0F);
	// The slider is how much darker it looks, so the light itself falls by more than that (eyes and screens don't see light in proportion).
	float nightDim = std::pow(1.0F - std::clamp(m_Settings.DeepNightDarkness, 0.0F, 0.97F) * deepNight, 2.2F);
	m_NightDim = nightDim;
	m_EffectiveSky = m_Settings.SkyColor * daylight * nightDim;
	m_NightSky = std::clamp(1.0F - dayFactor * 3.0F, 0.0F, 1.0F);
	m_SkyDaylight = daylight;
	{
		// The sky of the hour, for recoloring the sky art (which is painted as a blue day): overhead, at the horizon, and on cloud. Linear colours.
		struct SkyOfHour {
			float Hour;
			glm::vec3 Zenith, Horizon, Cloud;
		};
		// Early night and the hour before dawn are a deep navy; from eleven to two the sky is all but black, as it is with no sun anywhere near.
		static const glm::vec3 navy[3] = {{0.004F, 0.007F, 0.028F}, {0.016F, 0.028F, 0.075F}, {0.03F, 0.04F, 0.075F}};
		static const glm::vec3 black[3] = {{0.0003F, 0.0005F, 0.0022F}, {0.0014F, 0.0024F, 0.007F}, {0.004F, 0.005F, 0.01F}};
		static const SkyOfHour hours[] = {
		    {0.0F, black[0], black[1], black[2]},
		    {2.0F, black[0], black[1], black[2]},
		    {4.6F, navy[0], navy[1], navy[2]},
		    {5.6F, {0.03F, 0.045F, 0.16F}, {0.5F, 0.2F, 0.16F}, {0.45F, 0.22F, 0.24F}},
		    {6.4F, {0.1F, 0.19F, 0.5F}, {1.0F, 0.52F, 0.25F}, {1.0F, 0.62F, 0.48F}},
		    {8.0F, {0.16F, 0.38F, 0.82F}, {0.5F, 0.7F, 0.95F}, {1.0F, 0.98F, 0.95F}},
		    {16.0F, {0.16F, 0.38F, 0.82F}, {0.5F, 0.7F, 0.95F}, {1.0F, 0.98F, 0.95F}},
		    {17.6F, {0.1F, 0.14F, 0.45F}, {1.0F, 0.42F, 0.16F}, {1.0F, 0.55F, 0.35F}},
		    {18.5F, {0.03F, 0.04F, 0.16F}, {0.45F, 0.14F, 0.14F}, {0.4F, 0.18F, 0.22F}},
		    {19.6F, navy[0], navy[1], navy[2]},
		    {23.0F, black[0], black[1], black[2]},
		    {24.0F, black[0], black[1], black[2]},
		};
		float hour = std::clamp(m_Settings.TimeOfDay, 0.0F, 24.0F);
		size_t next = 1;
		while (next + 1 < std::size(hours) && hours[next].Hour < hour) {
			++next;
		}
		float between = std::clamp((hour - hours[next - 1].Hour) / std::max(hours[next].Hour - hours[next - 1].Hour, 0.001F), 0.0F, 1.0F);
		m_SkyZenith = glm::mix(hours[next - 1].Zenith, hours[next].Zenith, between);
		m_SkyHorizon = glm::mix(hours[next - 1].Horizon, hours[next].Horizon, between);
		m_SkyCloud = glm::mix(hours[next - 1].Cloud, hours[next].Cloud, between);
		// Around midday the art is right as it is and is left alone; the replacement comes in through the afternoon and goes out through the morning.
		float away = hour < 12.0F ? 1.0F - glm::smoothstep(7.5F, 9.5F, hour) : glm::smoothstep(14.5F, 16.5F, hour);
		// Bad weather greys the sky at any hour.
		float overcast = m_Settings.WeatherType > 0 ? std::clamp(m_Settings.WeatherIntensity, 0.0F, 1.0F) : 0.0F;
		if (overcast > 0.0F) {
			auto grey = [overcast](const glm::vec3& color) {
				float brightness = glm::dot(color, glm::vec3(0.2126F, 0.7152F, 0.0722F));
				return glm::mix(color, glm::vec3(0.75F, 0.8F, 0.88F) * brightness * 0.8F, overcast * 0.75F);
			};
			m_SkyZenith = grey(m_SkyZenith);
			m_SkyHorizon = grey(m_SkyHorizon);
			m_SkyCloud = grey(m_SkyCloud);
			away = std::max(away, overcast * 0.7F);
		}
		m_SkyRecolor = std::clamp(m_Settings.SkyFollowsTime, 0.0F, 1.0F) * away;
	}
	m_MoonHours = std::fmod(m_Settings.TimeOfDay + 12.0F, 24.0F);

	// The sun crosses the sky with the time of day, and at night the moon takes over (as for the god rays). Shadows fall away from it.
	// They fade out as the two swap at the horizon, are fainter by moonlight, and fainter under an overcast sky.
	bool sunIsUp = m_Settings.TimeOfDay >= 6.0F && m_Settings.TimeOfDay <= 18.0F;
	float sunArc = ((sunIsUp ? m_Settings.TimeOfDay : std::fmod(m_Settings.TimeOfDay + 12.0F, 24.0F)) - 12.0F) / 6.0F;
	m_SunDirection = glm::normalize(glm::vec2(sunArc * 1.05F, -1.0F));
	float overcast = m_Settings.WeatherType > 0 ? std::clamp(m_Settings.WeatherIntensity, 0.0F, 1.0F) : 0.0F;
	m_SunShadowStrength = m_Settings.SunShadows * (1.0F - glm::smoothstep(0.8F, 1.0F, std::abs(sunArc))) * (sunIsUp ? 1.0F : 0.6F) * (1.0F - 0.8F * overcast);
	m_SunArc = sunArc;
	// The sun's disc sinks into the horizon haze at dawn and dusk, and weather hides it.
	m_SunDiscStrength = sunIsUp ? m_Settings.SunDisc * (1.0F - glm::smoothstep(0.9F, 1.0F, std::abs(sunArc))) * (1.0F - overcast) : 0.0F;

	// Lightning in heavy rain: a bright double flicker every so often that briefly lights the whole sky.
	long long lightningUpdates = m_LightningLastSimUpdate >= 0 ? simUpdateCount - m_LightningLastSimUpdate : 0;
	m_LightningLastSimUpdate = simUpdateCount;
	// Sim time, so storms pause with the game.
	float frameSeconds = std::min(static_cast<float>(lightningUpdates) * g_TimerMan.GetDeltaTimeSecs(), 0.1F);
	auto nextRandom = [this]() {
		m_LightningRandom = m_LightningRandom * 1664525u + 1013904223u;
		return static_cast<float>(m_LightningRandom >> 8) / static_cast<float>(1u << 24);
	};
	if (m_Settings.WeatherType == 1 && m_Settings.WeatherIntensity > 0.5F) {
		m_NextLightningSeconds -= frameSeconds;
		if (m_NextLightningSeconds <= 0.0F) {
			m_LightningSecondsLeft = 0.45F;
			// Heavier storms flash more often.
			float storm = (m_Settings.WeatherIntensity - 0.5F) * 2.0F;
			m_NextLightningSeconds = (6.0F + nextRandom() * 16.0F) * (1.2F - 0.6F * storm);
		}
	}
	if (m_LightningSecondsLeft > 0.0F) {
		m_LightningSecondsLeft = std::max(m_LightningSecondsLeft - frameSeconds, 0.0F);
		float t = 0.45F - m_LightningSecondsLeft;
		// Two strokes: a sharp first flash and a weaker echo.
		m_Lightning = 1.6F * std::exp(-t * 18.0F) + 0.9F * std::exp(-std::abs(t - 0.2F) * 25.0F);
	} else {
		m_Lightning = 0.0F;
	}
	m_EffectiveSky += glm::vec3(0.75F, 0.8F, 1.0F) * m_Lightning;

	// Snow settles over about a minute of heavy snowfall and melts slower than that; rain wets the ground quickly and dries slowly.
	float snowTarget = m_Settings.WeatherType == 2 ? m_Settings.WeatherIntensity : 0.0F;
	float wetTarget = m_Settings.WeatherType == 1 ? std::min(1.0F, m_Settings.WeatherIntensity * 1.3F) : 0.0F;
	m_SnowCover += std::clamp(snowTarget - m_SnowCover, -frameSeconds / 90.0F, frameSeconds / 60.0F);
	m_Wetness += std::clamp(wetTarget - m_Wetness, -frameSeconds / 60.0F, frameSeconds / 8.0F);
	// Interiors and caves get a little darker at night too, but much less than the outdoors: bunkers are artificially lit and should stay playable.
	m_EffectiveAmbient = m_Settings.Ambient * (0.85F + 0.15F * dayFactor) * nightDim;
	// The readability floor drops more at night than the interior ambient does, so night battles outdoors stay dark and moody.
	m_EffectiveForegroundAmbient = m_Settings.ForegroundAmbient * (0.4F + 0.6F * dayFactor) * nightDim;

	if (!EnsureWorldResources()) {
		return;
	}
	++m_FrameCounter;

	// Round-robin refresh of the terrain into the grid, so digging and explosions show up in the lighting within a fraction of a second.
	PerformanceMan::LogStages logStages(true);
	logStages.Next("Light grid: terrain refresh and skyline");
	int rowsPerFrame = std::max(4, m_GridHeight / 30);
	int firstRow = m_NextRefreshRow;
	int endRow = std::min(firstRow + rowsPerFrame, m_GridHeight);
	RefreshOccupancyRows(firstRow, endRow);
	UploadOccupancyRows(firstRow, endRow);
	m_NextRefreshRow = (endRow >= m_GridHeight) ? 0 : endRow;
	// Where terrain actually changed since the last frame (a piece falling, liquid moving, digging, a crater) is brought up to date at once, so shade and light
	// follow it as it moves instead of catching up when the round-robin gets there.
	bool terrainChanged = false;
	if (int minX, minY, maxX, maxY; SLTerrain::TakeChangedArea(minX, minY, maxX, maxY)) {
		int changedFirstRow = std::clamp(minY / m_CellSize - 1, 0, m_GridHeight);
		int changedEndRow = std::clamp(maxY / m_CellSize + 2, 0, m_GridHeight);
		// Changes either side of the seam of a wrapping scene, or outside it, take the whole width.
		bool wholeWidth = minX < 0 || maxX >= m_SceneWidth;
		int changedFirstColumn = wholeWidth ? 0 : std::clamp(minX / m_CellSize - 1, 0, m_GridWidth);
		int changedEndColumn = wholeWidth ? m_GridWidth : std::clamp(maxX / m_CellSize + 2, 0, m_GridWidth);
		if (changedEndRow > changedFirstRow) {
			RefreshOccupancyRows(changedFirstRow, changedEndRow, changedFirstColumn, changedEndColumn);
			UploadOccupancyRows(changedFirstRow, changedEndRow);
			terrainChanged = true;
		}
	}
	RecomputeSkyline();

	GLint previousFramebuffer = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
	GLint previousViewport[4];
	glGetIntegerv(GL_VIEWPORT, previousViewport);
	logStages.Next("Light grid: sky light spreading");
	// Sky light spreads a cell a step, so it gets more steps while the ground is changing, to keep up with it.
	PropagateSkyLight(m_Settings.PropagationIterationsPerFrame * (terrainChanged ? 3 : 1));
	logStages.Next("Light grid: scorch marks and stains");
	StampScorchMarks();
	StampStains();
	logStages.Next(nullptr);
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

GLuint SceneLighting::BuildOccluderField(RenderTarget* playerScreen, float foregroundDepth) {
	std::shared_ptr<Texture> surface = playerScreen->GetSurfaceTexture().lock();
	std::shared_ptr<DepthTexture> depth = playerScreen->GetDepthTexture().lock();
	if (!surface || !depth || !m_OccluderSeeds[0].Texture) {
		return 0;
	}
	TracyGpuZone("Occluder Field");
	glDisable(GL_BLEND);
	glViewport(0, 0, m_OccluderSeeds[0].Width, m_OccluderSeeds[0].Height);

	// Seeds: every pixel of a solid object holds its own position.
	glBindFramebuffer(GL_FRAMEBUFFER, m_OccluderSeeds[0].Framebuffer);
	m_OccluderSeedShader->Enable();
	m_OccluderSeedShader->SetInt("rteSurface", 0);
	m_OccluderSeedShader->SetInt("rteSceneDepth", 1);
	m_OccluderSeedShader->SetFloat("rteForegroundDepth", foregroundDepth);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, depth->GetTextureId());
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, surface->GetTextureId());
	DrawFullscreen();

	// Jump flooding: each pass, every pixel takes the nearest seed among itself and eight pixels a step away. A final extra pass at one pixel tidies up the few it gets wrong.
	m_OccluderJumpShader->Enable();
	m_OccluderJumpShader->SetInt("rteSeeds", 0);
	int current = 0;
	for (int step: {32, 16, 8, 4, 2, 1, 1}) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_OccluderSeeds[1 - current].Framebuffer);
		m_OccluderJumpShader->SetInt("rteStep", step);
		glBindTexture(GL_TEXTURE_2D, m_OccluderSeeds[current].Texture);
		DrawFullscreen();
		current = 1 - current;
	}
	return m_OccluderSeeds[current].Texture;
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

void SceneLighting::LightPlayerScreen(int screenIndex, RenderTarget* playerScreen, const Vector& screenOrigin, const std::list<PostEffect>& screenEffects, const std::vector<SceneLight>& screenLights, const std::vector<ScreenShockwave>& screenShockwaves) {
	screenIndex = std::clamp(screenIndex, 0, c_MaxScreens - 1);
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

	PerformanceMan::LogStages logStages(true);
	logStages.Next("Lighting: building quads (CPU)");
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
			m_QuadVertices.push_back({position.x, position.y, 0.0F, (corner.x + 1.0F) * 0.5F, (corner.y + 1.0F) * 0.5F, color.r, color.g, color.b, 1.0F, center.x, center.y, radius, 1.0F, 0.0F, -2.0F});
		}
	};
	size_t lightCount = 0;
	if (m_Settings.Enabled) {
		// Every lamp, glow, flash and fire light goes through the player's light color settings: how colorful light is, and a tint on all of it.
		auto styled = [this](const glm::vec3& color) {
			float grey = glm::dot(color, glm::vec3(0.2126F, 0.7152F, 0.0722F));
			return glm::max(glm::mix(glm::vec3(grey), color, m_Settings.LightSaturation), glm::vec3(0.0F)) * m_Settings.LightTint;
		};
		for (const PostEffect& effect: screenEffects) {
			if (!effect.m_Bitmap || effect.m_NoLight) {
				continue;
			}
			const GlowInfo& glow = GetGlowInfo(effect.m_Bitmap.get());
			float radius = std::max(24.0F, glow.Size * 0.5F * m_Settings.GlowLightRadiusScale);
			glm::vec3 color = styled(glm::mix(glow.LightColor, glm::vec3(1.0F), 0.25F) * (static_cast<float>(effect.m_Strength) / 255.0F) * m_Settings.GlowLightIntensity);
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
			addQuad(center, glm::vec2(light.m_Radius), 0.0F, styled(light.m_Color), light.m_Radius);
			for (size_t vertex = firstVertex; vertex < m_QuadVertices.size(); ++vertex) {
				m_QuadVertices[vertex].U = m_QuadVertices[vertex].U * 2.0F - 1.0F;
				m_QuadVertices[vertex].V = m_QuadVertices[vertex].V * 2.0F - 1.0F;
				m_QuadVertices[vertex].ConeX = light.m_Direction.x;
				m_QuadVertices[vertex].ConeY = light.m_Direction.y;
				m_QuadVertices[vertex].ConeCos = light.m_ConeCos;
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
		const FloatRect& uvRect = effect.m_Bitmap->GetUVRect();
		for (size_t vertex = m_QuadVertices.size() - 4; vertex < m_QuadVertices.size(); ++vertex) {
			m_QuadVertices[vertex].U = uvRect.x + m_QuadVertices[vertex].U * uvRect.w;
			m_QuadVertices[vertex].V = uvRect.y + m_QuadVertices[vertex].V * uvRect.h;
		}
		emissiveTextures.push_back(effect.m_Bitmap->GetTextureId());
	}

	// Visual sparks from explosions and impacts: short glowing streaks along their motion.
	{
		std::vector<EffectsParticles::Spark> sparks;
		EffectsParticles::GetSparks(origin, width, height, sparks);
		GLuint whiteTexture = g_RenderMan.GetShapeTexture();
		for (const EffectsParticles::Spark& spark: sparks) {
			float angle = std::atan2(spark.Direction.y, spark.Direction.x);
			addQuad(spark.Position - spark.Direction * (spark.Length * 0.5F), glm::vec2(spark.Length * 0.5F + 0.5F, 0.6F), angle, glm::min(spark.Color, glm::vec3(1.0F)), 0.0F);
			emissiveTextures.push_back(whiteTexture);
		}
	}

	// The fire of explosions: soft glowing balls, drawn into the glow buffer with the puff's round shape.
	{
		std::vector<EffectsParticles::Puff> fire;
		EffectsParticles::GetFire(origin, width, height, fire);
		GLuint puffTexture = EffectsParticles::GetPuffTexture();
		for (const EffectsParticles::Puff& ball: fire) {
			addQuad(ball.Position, glm::vec2(ball.Size * 0.5F), 0.0F, glm::min(glm::vec3(ball.Color), glm::vec3(1.0F)), 0.0F);
			emissiveTextures.push_back(puffTexture);
		}
	}

	// Burning terrain: each pixel flickers between yellow and deep orange as it burns down, and sometimes throws an ember.
	{
		std::vector<glm::vec3> burning;
		TerrainFire::GetBurning(origin, width, height, burning);
		GLuint whiteTexture = g_RenderMan.GetShapeTexture();
		float time = PostProcessMan::GetSmoothSimTime();
		for (const glm::vec3& pixel: burning) {
			glm::vec2 position(pixel.x, pixel.y);
			float noise = glm::fract(std::sin(glm::dot(position + origin, glm::vec2(12.9898F, 78.233F)) + std::floor(time * 14.0F) * 3.1F) * 43758.5453F);
			float heat = pixel.z;
			glm::vec3 color = glm::mix(glm::vec3(0.9F, 0.25F, 0.03F), glm::vec3(1.0F, 0.85F, 0.35F), std::clamp(heat * 0.7F + noise * 0.5F, 0.0F, 1.0F)) * (0.7F + 0.6F * noise);
			// A flame tongue above the pixel, taller where it's hotter.
			float flameHeight = 1.0F + std::floor(noise * 3.0F * (0.4F + heat));
			addQuad(position + glm::vec2(0.5F, 0.5F - flameHeight * 0.5F), glm::vec2(0.5F, flameHeight * 0.5F + 0.5F), 0.0F, glm::min(color, glm::vec3(1.0F)), 0.0F);
			emissiveTextures.push_back(whiteTexture);
			if (noise > 0.995F) {
				EffectsParticles::SpawnEmber(Vector(position.x + origin.x, position.y + origin.y - 2.0F));
			}
		}
	}

	// Embers rising from fire and other warm glows. Procedural from a seed tied to the glow's world position (quantized, so flickering flames keep the same embers), no simulation needed.
	if (m_Settings.Embers > 0.0F) {
		auto hash = [](float n) { return glm::fract(std::sin(n) * 43758.5453F); };
		float time = PostProcessMan::GetSmoothSimTime();
		GLuint whiteTexture = g_RenderMan.GetShapeTexture();
		for (const PostEffect& effect: screenEffects) {
			if (!effect.m_Bitmap) {
				continue;
			}
			const GlowInfo& glow = GetGlowInfo(effect.m_Bitmap.get());
			float strength = static_cast<float>(effect.m_Strength) / 255.0F;
			if (glow.LightColor.r < glow.LightColor.b * 1.4F || strength < 0.25F || glow.Size < 6.0F) {
				continue;
			}
			glm::vec2 world = origin + glm::vec2(effect.m_Pos.m_X, effect.m_Pos.m_Y);
			glm::vec2 cell = glm::floor(world / 10.0F);
			float seedBase = hash(cell.x * 12.9898F + cell.y * 78.233F) * 1000.0F;
			int count = std::clamp(static_cast<int>(glow.Size / 14.0F * m_Settings.Embers), 1, 5);
			for (int i = 0; i < count; ++i) {
				float seed = hash(seedBase + static_cast<float>(i) * 17.31F);
				float seed2 = hash(seedBase + static_cast<float>(i) * 41.17F + 3.0F);
				float rate = 0.45F + seed * 0.7F;
				float phase = glm::fract(time * rate + seed * 7.0F);
				float rise = 25.0F + seed2 * 45.0F;
				glm::vec2 emberWorld(cell.x * 10.0F + 5.0F + (seed2 - 0.5F) * glow.Size * 0.5F + std::sin(time * 2.3F + seed * 20.0F) * 3.0F * phase, cell.y * 10.0F - phase * rise);
				float fade = std::pow(1.0F - phase, 1.5F) * strength;
				glm::vec3 emberColor = glm::mix(glm::vec3(1.0F, 0.85F, 0.45F), glm::vec3(1.0F, 0.35F, 0.08F), phase) * fade;
				glm::vec2 screen = glm::floor(emberWorld - origin) + glm::vec2(0.5F);
				addQuad(screen, glm::vec2(0.5F), 0.0F, emberColor, 0.0F);
				emissiveTextures.push_back(whiteTexture);
			}
		}
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
	// Dust puffs, lit and drawn over the scene after the composite.
	size_t puffStart = m_QuadVertices.size() / 4;
	{
		std::vector<EffectsParticles::Puff> puffs;
		EffectsParticles::GetPuffs(origin, width, height, puffs);
		for (const EffectsParticles::Puff& puff: puffs) {
			size_t firstVertex = m_QuadVertices.size();
			addQuad(puff.Position, glm::vec2(puff.Size * 0.5F), 0.0F, glm::vec3(puff.Color), 0.0F);
			for (size_t vertex = firstVertex; vertex < m_QuadVertices.size(); ++vertex) {
				m_QuadVertices[vertex].A = puff.Color.a;
			}
		}
	}
	size_t puffCount = m_QuadVertices.size() / 4 - puffStart;
	// Smoke density splats, in half resolution pixels.
	size_t smokeStart = m_QuadVertices.size() / 4;
	if (m_Settings.Enabled && m_Settings.SmokeScattering > 0.0F) {
		std::vector<EffectsParticles::Puff> smoke;
		EffectsParticles::GetSmoke(origin, width, height, smoke);
		for (const EffectsParticles::Puff& puff: smoke) {
			addQuad(puff.Position * 0.5F, glm::vec2(puff.Size * 0.25F), 0.0F, glm::vec3(puff.Color.a), 0.0F);
		}
	}
	size_t smokeCount = m_QuadVertices.size() / 4 - smokeStart;
	UploadQuads();
	PerformanceMan::AddLogCount("# lights on screen", lightCount);
	PerformanceMan::AddLogCount("# quads for lighting (lights, glows, sparks, fire, dust, smoke)", m_QuadVertices.size() / 4);

	// The map of distances to solid objects, for their shadows and for contact shading.
	logStages.Next("Lighting: map of solid objects");
	float foregroundDepth = ((2.0F * (c_TerrainBGDepth * 0.5F) - (c_FarDepth + c_NearDepth)) / (c_FarDepth - c_NearDepth)) * 0.5F + 0.5F;
	std::shared_ptr<Texture> surface = playerScreen->GetSurfaceTexture().lock();
	GLuint occluders = (m_Settings.Enabled && surface && (m_Settings.UnitShadows > 0.0F || m_Settings.ContactShading > 0.0F)) ? BuildOccluderField(playerScreen, foregroundDepth) : 0;
	float unitShadows = occluders ? m_Settings.UnitShadows : 0.0F;

	logStages.Next("Lighting: shockwaves");
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
	GLuint normalTexture = normals ? normals->GetTextureId() : 0;
	// Metallic and glossy objects are rounded off, so they turn from the sky above to the ground below instead of being flat with a thin rim. Everything after this reads the rounded normals.
	if (std::shared_ptr<DepthTexture> roundingDepth = playerScreen->GetDepthTexture().lock(); m_Settings.Enabled && m_Settings.Metals > 0.0F && normals && surface && roundingDepth && m_RoundedNormals.Texture) {
		logStages.Next("Lighting: rounding metal");
		glDisable(GL_BLEND);
		glBindFramebuffer(GL_FRAMEBUFFER, m_RoundedNormals.Framebuffer);
		m_SurfaceRoundShader->Enable();
		m_SurfaceRoundShader->SetInt("rteNormals", 0);
		m_SurfaceRoundShader->SetInt("rteSurface", 1);
		m_SurfaceRoundShader->SetInt("rteSceneDepth", 2);
		m_SurfaceRoundShader->SetFloat("rteRounding", m_Settings.Metals);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, roundingDepth->GetTextureId());
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, surface->GetTextureId());
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, normalTexture);
		DrawFullscreen();
		normalTexture = m_RoundedNormals.Texture;
	}

	logStages.Next("Lighting: lights and their shadows");
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
		m_PointLightShader->SetFloat("rteSpecular", m_Settings.Specular);
		m_PointLightShader->SetBool("rteBeamMode", false);
		m_PointLightShader->SetInt("rteOccluders", 2);
		m_PointLightShader->SetInt("rteSurface", 3);
		m_PointLightShader->SetFloat("rteUnitShadows", unitShadows);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_OccupancyTexture.Texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, normalTexture);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, occluders);
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, surface ? surface->GetTextureId() : 0);
		glActiveTexture(GL_TEXTURE0);
		DrawQuads(0, lightCount);
		glDisable(GL_BLEND);
	}

	logStages.Next("Lighting: glows");
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
				// Puffs (the fire of explosions) are a round shape in the texture's alpha.
				m_EmissiveShader->SetBool("rteUseAlpha", emissiveTextures[runStart] == EffectsParticles::GetPuffTexture());
				DrawQuads(emissiveStart + runStart, i - runStart);
				runStart = i;
			}
		}
		m_EmissiveShader->SetBool("rteUseAlpha", false);
		glDisable(GL_BLEND);
	}

	logStages.Next("Lighting: radiance cascades");
	// Global illumination by radiance cascades, from the glows drawn above and last frame's lit surfaces.
	std::shared_ptr<DepthTexture> cascadeDepth = playerScreen->GetDepthTexture().lock();
	bool useRadianceCascades = m_Settings.Enabled && m_Settings.RadianceCascades && cascadeDepth;
	float foregroundCascadeThresholdZ = c_TerrainBGDepth * 0.5F;
	float foregroundCascadeDepth = ((2.0F * foregroundCascadeThresholdZ - (c_FarDepth + c_NearDepth)) / (c_FarDepth - c_NearDepth)) * 0.5F + 0.5F;
	if (useRadianceCascades) {
		TracyGpuZone("Radiance Cascades");
		glDisable(GL_BLEND);
		glm::vec2 rcSize(static_cast<float>(m_RCScene.Width), static_cast<float>(m_RCScene.Height));
		glBindFramebuffer(GL_FRAMEBUFFER, m_RCScene.Framebuffer);
		glViewport(0, 0, m_RCScene.Width, m_RCScene.Height);
		m_RCSceneShader->Enable();
		m_RCSceneShader->SetInt("rteEmissive", 0);
		m_RCSceneShader->SetInt("rteSceneDepth", 1);
		m_RCSceneShader->SetInt("rtePreviousLit", 2);
		m_RCSceneShader->SetVector2f("rtePreviousOffset", m_RCPreviousValid[screenIndex] ? (origin - m_RCPreviousOrigin[screenIndex]) : glm::vec2(0.0F));
		m_RCSceneShader->SetVector2f("rteTargetSize", rcSize);
		m_RCSceneShader->SetVector2f("rteScreenSize", screenSize);
		m_RCSceneShader->SetFloat("rteForegroundDepth", foregroundCascadeDepth);
		m_RCSceneShader->SetFloat("rteEmissiveIntensity", m_Settings.EmissiveIntensity);
		m_RCSceneShader->SetFloat("rteBounce", m_Settings.GIBounce);
		m_RCSceneShader->SetBool("rteHavePrevious", m_RCPreviousValid[screenIndex]);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_Emissive.Texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, cascadeDepth->GetTextureId());
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, m_RCPreviousLit[screenIndex].Texture);
		DrawFullscreen();

		m_RCCascadeShader->Enable();
		m_RCCascadeShader->SetInt("rteScene", 0);
		m_RCCascadeShader->SetInt("rteUpper", 1);
		m_RCCascadeShader->SetInt("rteCascadeCount", c_RCCascadeCount);
		m_RCCascadeShader->SetVector2f("rteTargetSize", rcSize);
		m_RCCascadeShader->SetFloat("rteBaseInterval", 1.5F);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_RCScene.Texture);
		int written = 0;
		for (int cascade = c_RCCascadeCount - 1; cascade >= 0; --cascade) {
			int target = (c_RCCascadeCount - 1 - cascade) % 2;
			glBindFramebuffer(GL_FRAMEBUFFER, m_RCCascades[target].Framebuffer);
			m_RCCascadeShader->SetInt("rteCascade", cascade);
			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, m_RCCascades[1 - target].Texture);
			DrawFullscreen();
			written = target;
		}

		glBindFramebuffer(GL_FRAMEBUFFER, m_RCIrradiance.Framebuffer);
		glViewport(0, 0, m_RCIrradiance.Width, m_RCIrradiance.Height);
		m_RCIrradianceShader->Enable();
		m_RCIrradianceShader->SetInt("rteCascade0", 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_RCCascades[written].Texture);
		DrawFullscreen();
		glViewport(0, 0, width, height);
	}

	logStages.Next("Lighting: composite");
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
	m_CompositeShader->SetInt("rteIndirect", 6);
	// Radiance cascades include bounced light, so the simpler indirect light is left out with them.
	bool useIndirect = m_Settings.Enabled && m_Settings.IndirectLight > 0.0F && m_IndirectHistoryValid[screenIndex] && !useRadianceCascades;
	m_CompositeShader->SetInt("rteGI", 7);
	m_CompositeShader->SetFloat("rteGIStrength", useRadianceCascades ? m_Settings.GIStrength : 0.0F);
	m_CompositeShader->SetFloat("rteIndirectStrength", useIndirect ? m_Settings.IndirectLight : 0.0F);
	m_CompositeShader->SetVector2f("rteIndirectOffset", useIndirect ? (origin - m_IndirectHistoryOrigin[screenIndex]) : glm::vec2(0.0F));
	m_CompositeShader->SetFloat("rteEdgeLighting", normals ? m_Settings.EdgeLighting : 0.0F);
	float foregroundCompositeThresholdZ = c_TerrainBGDepth * 0.5F;
	m_CompositeShader->SetFloat("rteForegroundDepth", ((2.0F * foregroundCompositeThresholdZ - (c_FarDepth + c_NearDepth)) / (c_FarDepth - c_NearDepth)) * 0.5F + 0.5F);
	m_CompositeShader->SetVector3f("rteForegroundAmbient", m_Settings.Enabled ? m_EffectiveForegroundAmbient : glm::vec3(1.0F));
	m_CompositeShader->SetFloat("rteEmissiveIntensity", m_Settings.EmissiveIntensity);
	m_CompositeShader->SetFloat("rteMaxDynamicLight", 2.0F);
	// Layers are drawn at depth z mapped linearly through the cameras' ortho projection. Background layers sit at c_BackgroundDepth, terrain background at c_TerrainBGDepth.
	float backgroundThresholdZ = (c_BackgroundDepth + c_TerrainBGDepth) * 0.5F;
	float backgroundThresholdNDC = (2.0F * backgroundThresholdZ - (c_FarDepth + c_NearDepth)) / (c_FarDepth - c_NearDepth);
	m_CompositeShader->SetFloat("rteBackgroundDepth", backgroundThresholdNDC * 0.5F + 0.5F);
	auto depthForZ = [](float z) { return ((2.0F * z - (c_FarDepth + c_NearDepth)) / (c_FarDepth - c_NearDepth)) * 0.5F + 0.5F; };
	m_CompositeShader->SetFloat("rteBackgroundNearDepth", depthForZ(c_BackgroundDepth));
	m_CompositeShader->SetFloat("rteBackgroundFarDepth", depthForZ(c_BackgroundDepth + c_BackgroundDepthRange));
	// A dust storm hangs a tan haze over the distance; ash fall a grey one.
	glm::vec3 atmosphereColor = m_Settings.AtmosphereColor;
	float atmosphereHaze = m_Settings.AtmosphereHaze;
	if (m_Settings.WeatherType == 4) {
		atmosphereColor = glm::mix(atmosphereColor, glm::vec3(0.8F, 0.64F, 0.42F), std::min(m_Settings.WeatherIntensity, 1.0F));
		atmosphereHaze = std::min(atmosphereHaze + 0.55F * m_Settings.WeatherIntensity, 1.0F);
	} else if (m_Settings.WeatherType == 3) {
		atmosphereColor = glm::mix(atmosphereColor, glm::vec3(0.42F, 0.4F, 0.4F), std::min(m_Settings.WeatherIntensity, 1.0F) * 0.8F);
		atmosphereHaze = std::min(atmosphereHaze + 0.3F * m_Settings.WeatherIntensity, 1.0F);
	}
	m_CompositeShader->SetVector3f("rteAtmosphereColor", atmosphereColor * GetDaylightTint(m_Settings.TimeOfDay));
	m_CompositeShader->SetFloat("rteAtmosphereHaze", m_Settings.Enabled ? atmosphereHaze : 0.0F);
	m_CompositeShader->SetVector3f("rteBackgroundLight", m_Settings.Enabled ? m_EffectiveSky : glm::vec3(1.0F));
	m_CompositeShader->SetFloat("rteSkyRecolor", m_Settings.Enabled ? m_SkyRecolor : 0.0F);
	m_CompositeShader->SetFloat("rteWaterGlow", m_Settings.Enabled ? m_Settings.WaterLightGlow : 0.0F);
	m_CompositeShader->SetVector3f("rteSkyDaylight", m_SkyDaylight);
	m_CompositeShader->SetFloat("rteSkyOwnLight", m_Settings.Enabled ? std::clamp(m_Settings.SkyFollowsTime, 0.0F, 1.0F) : 0.0F);
	m_CompositeShader->SetVector3f("rteSkyZenith", m_SkyZenith);
	m_CompositeShader->SetVector3f("rteSkyHorizon", m_SkyHorizon);
	m_CompositeShader->SetVector3f("rteSkyCloud", m_SkyCloud);
	m_CompositeShader->SetFloat("rteNightSky", m_Settings.Enabled ? m_NightSky * (1.0F - std::clamp(m_Settings.WeatherType > 0 ? m_Settings.WeatherIntensity * 1.5F : 0.0F, 0.0F, 1.0F)) : 0.0F);
	m_CompositeShader->SetFloat("rteTime", PostProcessMan::GetSmoothSimTime());
	{
		// The moon follows the same arc as the sun, high in the sky behind everything. Player screens are drawn top down, so y 0 is the top.
		float arc = (m_MoonHours - 12.0F) / 6.0F;
		glm::vec2 moonPosition((0.5F + arc * 0.38F) * screenSize.x, (0.34F - 0.14F * (1.0F - arc * arc)) * screenSize.y);
		m_CompositeShader->SetVector2f("rteMoonPosition", moonPosition);
	}
	m_CompositeShader->SetVector2f("rteScreenSize", screenSize);
	m_CompositeShader->SetVector2f("rteScreenOrigin", origin);
	m_CompositeShader->SetVector2f("rteGridWorldSize", gridWorldSize);
	m_CompositeShader->SetVector3f("rteAmbient", m_Settings.Enabled ? m_EffectiveAmbient : glm::vec3(1.0F));
	m_CompositeShader->SetVector3f("rteSkyColor", m_Settings.Enabled ? m_EffectiveSky : glm::vec3(1.0F));
	m_CompositeShader->SetInt("rteOccupancy", 8);
	m_CompositeShader->SetInt("rteOccluders", 9);
	m_CompositeShader->SetInt("rteSurface", 10);
	m_CompositeShader->SetVector2f("rteSunDirection", m_SunDirection);
	m_CompositeShader->SetFloat("rteSunShadows", m_Settings.Enabled ? m_SunShadowStrength : 0.0F);
	m_CompositeShader->SetVector3f("rteShadeTint", glm::vec3(0.5F, 0.56F, 0.72F));
	m_CompositeShader->SetFloat("rteUnitShadows", unitShadows);
	m_CompositeShader->SetFloat("rteContactShading", occluders ? m_Settings.ContactShading : 0.0F);
	m_CompositeShader->SetFloat("rteMetals", (m_Settings.Enabled && surface) ? m_Settings.Metals : 0.0F);
	m_CompositeShader->SetFloat("rteBackgroundBlur", m_Settings.Enabled ? m_Settings.BackgroundBlur : 0.0F);
	// The sun follows the same arc across the sky as the moon does at night: low at the sides, high in the middle. Warm near the horizon.
	m_CompositeShader->SetVector2f("rteSunPosition", glm::vec2((0.5F + m_SunArc * 0.38F) * screenSize.x, (0.34F - 0.14F * (1.0F - m_SunArc * m_SunArc)) * screenSize.y));
	m_CompositeShader->SetVector3f("rteSunDisc", m_Settings.Enabled ? glm::mix(glm::vec3(1.0F, 0.97F, 0.88F), glm::vec3(1.0F, 0.6F, 0.3F), glm::smoothstep(0.55F, 1.0F, std::abs(m_SunArc))) * m_SunDiscStrength : glm::vec3(0.0F));
	// Clouds only shade while there's direct sun to block, and they drift with the wind (slowly even in still air), in sim time.
	m_CloudDrift = PostProcessMan::GetSmoothSimTime() * (m_Settings.Wind * 0.35F + 6.0F);
	m_CompositeShader->SetFloat("rteCloudShadows", m_Settings.Enabled ? m_Settings.CloudShadows * std::min(m_SunShadowStrength * 2.0F, 1.0F) : 0.0F);
	m_CompositeShader->SetFloat("rteCloudDrift", m_CloudDrift);
	m_CompositeShader->SetFloat("rteSpecular", m_Settings.Enabled ? m_Settings.Specular : 0.0F);
	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_2D, m_OccupancyTexture.Texture);
	glActiveTexture(GL_TEXTURE9);
	glBindTexture(GL_TEXTURE_2D, occluders);
	glActiveTexture(GL_TEXTURE10);
	glBindTexture(GL_TEXTURE_2D, surface ? surface->GetTextureId() : 0);
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
	glBindTexture(GL_TEXTURE_2D, normalTexture);
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, m_IndirectHistory[screenIndex].Texture);
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, m_RCIrradiance.Texture);
	glActiveTexture(GL_TEXTURE0);
	DrawFullscreen();

	// Keep this frame's lit scene for next frame's bounces.
	if (useRadianceCascades) {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_HDRScene.Framebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_RCPreviousLit[screenIndex].Framebuffer);
		glBlitFramebuffer(0, 0, width, height, 0, 0, m_RCPreviousLit[screenIndex].Width, m_RCPreviousLit[screenIndex].Height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		glBindFramebuffer(GL_FRAMEBUFFER, m_HDRScene.Framebuffer);
		m_RCPreviousOrigin[screenIndex] = origin;
		m_RCPreviousValid[screenIndex] = true;
	} else {
		m_RCPreviousValid[screenIndex] = false;
	}

	logStages.Next("Lighting: indirect light blur");
	// Blur the lit scene down for next frame's indirect light. Taken before glows, rain and god rays so only lit surfaces bounce.
	if (m_Settings.Enabled && m_Settings.IndirectLight > 0.0F) {
		TracyGpuZone("Indirect Light");
		m_BloomDownsampleShader->Enable();
		m_BloomDownsampleShader->SetInt("rteSource", 0);
		m_BloomDownsampleShader->SetBool("rteFirstPass", false);
		glActiveTexture(GL_TEXTURE0);
		for (int mip = 0; mip < c_IndirectMipCount; ++mip) {
			const GLTarget& source = mip == 0 ? m_HDRScene : m_IndirectMips[mip - 1];
			const GLTarget& destination = mip == c_IndirectMipCount - 1 ? m_IndirectHistory[screenIndex] : m_IndirectMips[mip];
			glBindFramebuffer(GL_FRAMEBUFFER, destination.Framebuffer);
			glViewport(0, 0, destination.Width, destination.Height);
			m_BloomDownsampleShader->SetVector2f("rteSourceTexelSize", glm::vec2(1.0F / source.Width, 1.0F / source.Height));
			glBindTexture(GL_TEXTURE_2D, source.Texture);
			DrawFullscreen();
		}
		m_IndirectHistoryOrigin[screenIndex] = origin;
		m_IndirectHistoryValid[screenIndex] = true;
		glViewport(0, 0, width, height);
		glBindFramebuffer(GL_FRAMEBUFFER, m_HDRScene.Framebuffer);
	}

	logStages.Next("Lighting: dust, beams and smoke");
	// Translucent particles (dust) lit like the scene behind them.
	if (puffCount > 0) {
		TracyGpuZone("Lit Particles");
		glBindFramebuffer(GL_FRAMEBUFFER, m_HDRScene.Framebuffer);
		glViewport(0, 0, width, height);
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		m_LitParticleShader->Enable();
		m_LitParticleShader->SetVector2f("rteScreenSize", screenSize);
		m_LitParticleShader->SetVector2f("rteScreenOrigin", origin);
		m_LitParticleShader->SetVector2f("rteGridWorldSize", gridWorldSize);
		m_LitParticleShader->SetVector3f("rteAmbient", m_Settings.Enabled ? m_EffectiveAmbient : glm::vec3(1.0F));
		m_LitParticleShader->SetVector3f("rteSkyColor", m_Settings.Enabled ? m_EffectiveSky : glm::vec3(1.0F));
		m_LitParticleShader->SetFloat("rteMistBright", m_Settings.WaterMistBrightness);
		m_LitParticleShader->SetFloat("rteMistGlow", m_Settings.Enabled ? m_Settings.WaterMistGlow : 0.0F);
		m_LitParticleShader->SetInt("rteTexture", 0);
		m_LitParticleShader->SetInt("rteSkyLight", 1);
		m_LitParticleShader->SetInt("rteDynamicLight", 2);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, EffectsParticles::GetPuffTexture());
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m_SkyLight[m_CurrentSkyLight].Texture);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, m_DynamicLight.Texture);
		glActiveTexture(GL_TEXTURE0);
		DrawQuads(puffStart, puffCount);
		glDisable(GL_BLEND);
	}

	// Flashlight beams visible in the air, as light catching dust. Only cone lights draw anything here.
	if (lightCount > 0 && m_Settings.Enabled) {
		TracyGpuZone("Light Beams");
		glBindFramebuffer(GL_FRAMEBUFFER, m_HDRScene.Framebuffer);
		glViewport(0, 0, width, height);
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE);
		m_PointLightShader->Enable();
		m_PointLightShader->SetInt("rteOccupancy", 0);
		m_PointLightShader->SetVector2f("rteScreenSize", screenSize);
		m_PointLightShader->SetVector2f("rteScreenOrigin", origin);
		m_PointLightShader->SetVector2f("rteGridWorldSize", gridWorldSize);
		m_PointLightShader->SetFloat("rteShadowStrength", m_Settings.ShadowStrength);
		m_PointLightShader->SetBool("rteBeamMode", true);
		m_PointLightShader->SetInt("rteOccluders", 2);
		m_PointLightShader->SetInt("rteSurface", 3);
		m_PointLightShader->SetFloat("rteUnitShadows", unitShadows);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, occluders);
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, surface ? surface->GetTextureId() : 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_OccupancyTexture.Texture);
		DrawQuads(0, lightCount);
		m_PointLightShader->SetBool("rteBeamMode", false);
		glDisable(GL_BLEND);
	}

	// Light scattered in smoke: splat the smoke's density, then add the light passing through it.
	if (smokeCount > 0) {
		TracyGpuZone("Smoke Scattering");
		glBindFramebuffer(GL_FRAMEBUFFER, m_SmokeDensity.Framebuffer);
		glViewport(0, 0, m_SmokeDensity.Width, m_SmokeDensity.Height);
		glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
		glClear(GL_COLOR_BUFFER_BIT);
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE);
		m_EmissiveShader->Enable();
		m_EmissiveShader->SetInt("rteTexture", 0);
		m_EmissiveShader->SetBool("rteUseAlpha", true);
		m_EmissiveShader->SetVector2f("rteScreenSize", glm::vec2(static_cast<float>(m_SmokeDensity.Width), static_cast<float>(m_SmokeDensity.Height)));
		glActiveTexture(GL_TEXTURE0);
		// The puff texture's alpha is its shape; its color is white, so the emissive shader outputs the density from the vertex color.
		glBindTexture(GL_TEXTURE_2D, EffectsParticles::GetPuffTexture());
		DrawQuads(smokeStart, smokeCount);
		m_EmissiveShader->SetBool("rteUseAlpha", false);

		glBindFramebuffer(GL_FRAMEBUFFER, m_HDRScene.Framebuffer);
		glViewport(0, 0, width, height);
		m_SmokeScatterShader->Enable();
		m_SmokeScatterShader->SetInt("rteDensity", 0);
		m_SmokeScatterShader->SetInt("rteDynamicLight", 1);
		m_SmokeScatterShader->SetInt("rteGI", 2);
		m_SmokeScatterShader->SetFloat("rteGIStrength", useRadianceCascades ? m_Settings.GIStrength : 0.0F);
		m_SmokeScatterShader->SetVector2f("rteScreenSize", screenSize);
		m_SmokeScatterShader->SetFloat("rteStrength", m_Settings.SmokeScattering * 0.35F);
		m_SmokeScatterShader->SetVector3f("rteSmokeColor", glm::vec3(0.95F, 0.9F, 0.85F));
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_SmokeDensity.Texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m_DynamicLight.Texture);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, m_RCIrradiance.Texture);
		glActiveTexture(GL_TEXTURE0);
		DrawFullscreen();
		glDisable(GL_BLEND);
	}

	logStages.Next("Lighting: precipitation");
	// Rain or snow, lit by the sky, over the lit scene.
	if (m_Settings.WeatherType > 0 && m_Settings.WeatherIntensity > 0.0F) {
		TracyGpuZone("Precipitation");
		static constexpr float dropsPerScreen[5] = {0.0F, 2500.0F, 1500.0F, 1800.0F, 2200.0F};
		int dropCount = static_cast<int>(m_Settings.WeatherIntensity * dropsPerScreen[std::clamp(m_Settings.WeatherType, 0, 4)] * (static_cast<float>(width * height) / (960.0F * 540.0F)));
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		m_PrecipitationShader->Enable();
		m_PrecipitationShader->SetVector2f("rteScreenSize", screenSize);
		m_PrecipitationShader->SetVector2f("rteScreenOrigin", origin);
		m_PrecipitationShader->SetFloat("rteTime", PostProcessMan::GetSmoothSimTime());
		m_PrecipitationShader->SetInt("rteType", m_Settings.WeatherType);
		m_PrecipitationShader->SetFloat("rteWind", m_Settings.Wind);
		m_PrecipitationShader->SetInt("rteOccupancy", 0);
		m_PrecipitationShader->SetFloat("rteCellSize", static_cast<float>(m_CellSize));
		m_PrecipitationShader->SetVector2f("rteGridWorldSize", gridWorldSize);
		m_PrecipitationShader->SetVector3f("rteSkyLight", m_Settings.Enabled ? m_EffectiveSky : glm::vec3(1.0F));
		m_PrecipitationShader->SetFloat("rteIntensity", std::clamp(0.6F + 0.4F * m_Settings.WeatherIntensity, 0.0F, 1.0F));
		m_PrecipitationShader->SetInt("rteDynamicLight", 1);
		m_PrecipitationShader->SetFloat("rteOwnLight", m_Settings.Enabled ? m_Settings.WeatherLight : 0.0F);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_OccupancyTexture.Texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m_DynamicLight.Texture);
		glActiveTexture(GL_TEXTURE0);
		glBindVertexArray(m_EmptyVAO);
		glDrawArrays(GL_TRIANGLES, 0, dropCount * 6);
		// Raindrops splashing on whatever they land on.
		std::shared_ptr<DepthTexture> splashDepth = playerScreen->GetDepthTexture().lock();
		if (m_Settings.WeatherType == 1 && m_Settings.RainSplashes > 0.0F && splashDepth) {
			m_RainSplashShader->Enable();
			m_RainSplashShader->SetInt("rteSceneDepth", 2);
			m_RainSplashShader->SetInt("rteOccupancy", 0);
			m_RainSplashShader->SetInt("rteDynamicLight", 1);
			// Behind the foreground terrain and objects (z 0), in front of the terrain background (z c_TerrainBGDepth).
			float splashThresholdZ = c_TerrainBGDepth * 0.5F;
			m_RainSplashShader->SetFloat("rteForegroundDepth", ((2.0F * splashThresholdZ - (c_FarDepth + c_NearDepth)) / (c_FarDepth - c_NearDepth)) * 0.5F + 0.5F);
			m_RainSplashShader->SetVector2f("rteGridWorldSize", gridWorldSize);
			m_RainSplashShader->SetFloat("rteCellSize", static_cast<float>(m_CellSize));
			m_RainSplashShader->SetVector2f("rteScreenSize", screenSize);
			m_RainSplashShader->SetVector2f("rteScreenOrigin", origin);
			m_RainSplashShader->SetFloat("rteTime", PostProcessMan::GetSmoothSimTime());
			m_RainSplashShader->SetVector2f("rteFall", glm::normalize(glm::vec2(m_Settings.Wind, 640.0F)));
			m_RainSplashShader->SetFloat("rteAmount", std::clamp(m_Settings.WeatherIntensity * m_Settings.RainSplashes * 0.45F, 0.0F, 1.0F));
			m_RainSplashShader->SetVector3f("rteSkyLight", m_Settings.Enabled ? m_EffectiveSky : glm::vec3(1.0F));
			m_RainSplashShader->SetFloat("rteOwnLight", m_Settings.Enabled ? m_Settings.WeatherLight : 0.0F);
			glActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_2D, splashDepth->GetTextureId());
			glActiveTexture(GL_TEXTURE0);
			DrawFullscreen();
		}
		glDisable(GL_BLEND);
	}

	logStages.Next("Lighting: god rays");
	// God rays: shafts of light in the air of caves and bunkers, wherever the sun (or the moon, dimly) gets in. They fade at the horizon and under an overcast sky, like the shadows.
	if (m_Settings.Enabled && m_Settings.GodRays > 0.0F && sceneDepth) {
		TracyGpuZone("God Rays");
		float sunArc = std::abs(m_SunDirection.x / std::max(-m_SunDirection.y, 0.001F)) / 1.05F;
		float overcast = m_Settings.WeatherType > 0 ? std::clamp(m_Settings.WeatherIntensity, 0.0F, 1.0F) : 0.0F;
		float shaftStrength = m_Settings.GodRays * 0.5F * (1.0F - glm::smoothstep(0.8F, 1.0F, sunArc)) * (1.0F - 0.8F * overcast);
		glViewport(0, 0, m_GodRays.Width, m_GodRays.Height);
		glBindFramebuffer(GL_FRAMEBUFFER, m_GodRays.Framebuffer);
		m_GodRaysShader->Enable();
		m_GodRaysShader->SetInt("rteSkyLight", 0);
		m_GodRaysShader->SetVector2f("rteScreenOrigin", origin);
		m_GodRaysShader->SetVector2f("rteScreenSize", screenSize);
		m_GodRaysShader->SetVector2f("rteGridWorldSize", gridWorldSize);
		m_GodRaysShader->SetFloat("rteTime", PostProcessMan::GetSmoothSimTime());
		m_GodRaysShader->SetVector2f("rteSunDirection", m_SunDirection);
		m_GodRaysShader->SetVector3f("rteSunColor", m_EffectiveSky * shaftStrength);
		m_GodRaysShader->SetVector2f("rteTargetSize", glm::vec2(m_GodRays.Width, m_GodRays.Height));
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_SkyLight[m_CurrentSkyLight].Texture);
		DrawFullscreen();

		glViewport(0, 0, width, height);
		glBindFramebuffer(GL_FRAMEBUFFER, m_HDRScene.Framebuffer);
		glEnable(GL_BLEND);
		glBlendEquation(GL_FUNC_ADD);
		glBlendFunc(GL_ONE, GL_ONE);
		m_GodRaysApplyShader->Enable();
		m_GodRaysApplyShader->SetInt("rteGodRays", 0);
		m_GodRaysApplyShader->SetInt("rteSceneDepth", 1);
		// Behind the foreground terrain and objects (z 0), in front of the terrain background (z c_TerrainBGDepth).
		float foregroundThresholdZ = c_TerrainBGDepth * 0.5F;
		m_GodRaysApplyShader->SetFloat("rteForegroundDepth", ((2.0F * foregroundThresholdZ - (c_FarDepth + c_NearDepth)) / (c_FarDepth - c_NearDepth)) * 0.5F + 0.5F);
		m_GodRaysApplyShader->SetVector2f("rteScreenSize", screenSize);
		m_GodRaysApplyShader->SetFloat("rteBackgroundDepth", backgroundThresholdNDC * 0.5F + 0.5F);
		m_GodRaysApplyShader->SetVector2f("rteScreenOrigin", origin);
		m_GodRaysApplyShader->SetFloat("rteTime", PostProcessMan::GetSmoothSimTime());
		m_GodRaysApplyShader->SetFloat("rteDustMotes", 1.0F);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_GodRays.Texture);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, sceneDepth->GetTextureId());
		glActiveTexture(GL_TEXTURE0);
		DrawFullscreen();
		glDisable(GL_BLEND);
	}

	logStages.Next("Lighting: auto exposure");
	// Auto exposure: average the scene's log luminance, then let the adapted value drift towards it.
	bool useAutoExposure = m_Settings.Enabled && m_Settings.AutoExposure > 0.0F;
	if (useAutoExposure) {
		TracyGpuZone("Auto Exposure");
		glDisable(GL_BLEND);
		glBindFramebuffer(GL_FRAMEBUFFER, m_Luminance.Framebuffer);
		glViewport(0, 0, m_Luminance.Width, m_Luminance.Height);
		m_LuminanceShader->Enable();
		m_LuminanceShader->SetInt("rteScene", 0);
		m_LuminanceShader->SetVector2f("rteSourceTexelSize", glm::vec2(1.0F / static_cast<float>(width), 1.0F / static_cast<float>(height)) * (static_cast<float>(width) / static_cast<float>(m_Luminance.Width * 4)));
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_HDRScene.Texture);
		DrawFullscreen();
		glBindTexture(GL_TEXTURE_2D, m_Luminance.Texture);
		glGenerateMipmap(GL_TEXTURE_2D);

		double nowSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		float deltaSeconds = m_AdaptedLuminanceValid[screenIndex] ? static_cast<float>(std::clamp(nowSeconds - m_LastAdaptSeconds[screenIndex], 0.0, 0.25)) : 0.0F;
		m_LastAdaptSeconds[screenIndex] = nowSeconds;
		int previous = m_AdaptedLuminanceCurrent[screenIndex];
		int next = 1 - previous;
		glBindFramebuffer(GL_FRAMEBUFFER, m_AdaptedLuminance[screenIndex][next].Framebuffer);
		glViewport(0, 0, 1, 1);
		m_ExposureAdaptShader->Enable();
		m_ExposureAdaptShader->SetInt("rteLuminance", 0);
		m_ExposureAdaptShader->SetInt("rtePrevious", 1);
		m_ExposureAdaptShader->SetFloat("rteLuminanceLod", static_cast<float>(m_LuminanceMaxLod));
		m_ExposureAdaptShader->SetFloat("rteDeltaSeconds", deltaSeconds);
		m_ExposureAdaptShader->SetFloat("rteBrightenSpeed", 3.0F);
		m_ExposureAdaptShader->SetFloat("rteDarkenSpeed", 1.2F);
		m_ExposureAdaptShader->SetBool("rteReset", !m_AdaptedLuminanceValid[screenIndex]);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m_AdaptedLuminance[screenIndex][previous].Texture);
		glActiveTexture(GL_TEXTURE0);
		DrawFullscreen();
		m_AdaptedLuminanceCurrent[screenIndex] = next;
		m_AdaptedLuminanceValid[screenIndex] = true;
		glViewport(0, 0, width, height);
	} else {
		m_AdaptedLuminanceValid[screenIndex] = false;
	}

	logStages.Next("Lighting: bloom");
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

	logStages.Next("Lighting: tonemap");
	// Tonemap back into the player screen.
	playerScreen->Bind();
	glViewport(0, 0, width, height);
	m_TonemapShader->Enable();
	m_TonemapShader->SetInt("rteScene", 0);
	m_TonemapShader->SetInt("rteBloom", 1);
	m_TonemapShader->SetVector2f("rteScreenSize", screenSize);
	m_TonemapShader->SetFloat("rteBloomIntensity", m_Settings.BloomEnabled ? m_Settings.BloomIntensity : 0.0F);
	// With lighting off, the image should be the classic one: no exposure, highlight compression, saturation or vignette (the player's own grade still applies).
	m_TonemapShader->SetFloat("rteExposure", m_Settings.Enabled ? m_Settings.Exposure : 1.0F);
	m_TonemapShader->SetFloat("rteShoulderStart", m_Settings.Enabled ? m_Settings.ShoulderStart : 1.0F);
	m_TonemapShader->SetFloat("rteVignette", m_Settings.Enabled ? m_Settings.Vignette : 0.0F);
	m_TonemapShader->SetFloat("rteSaturation", m_Settings.Enabled ? m_Settings.Saturation : 1.0F);
	m_TonemapShader->SetInt("rteDistortion", 2);
	m_TonemapShader->SetInt("rteEmissive", 3);
	m_TonemapShader->SetBool("rteDistortionEnabled", m_Settings.DistortionEnabled);
	m_TonemapShader->SetFloat("rteHeatHaze", m_Settings.HeatHaze);
	m_TonemapShader->SetFloat("rteTime", PostProcessMan::GetSmoothSimTime());
	m_TonemapShader->SetInt("rteDebugView", m_Settings.DebugView);
	m_TonemapShader->SetFloat("rteTemperature", m_Settings.Temperature);
	m_TonemapShader->SetFloat("rteTint", m_Settings.Tint);
	m_TonemapShader->SetFloat("rteContrast", m_Settings.Contrast);
	m_TonemapShader->SetVector3f("rteShadowTint", m_Settings.ShadowTint);
	m_TonemapShader->SetVector3f("rteHighlightTint", m_Settings.HighlightTint);
	m_TonemapShader->SetFloat("rteFilmGrain", m_Settings.FilmGrain);
	// The blast pulse fades in real time, so it also fades during the hit-stop that comes with it.
	double pulseNow = static_cast<double>(g_TimerMan.GetRealTickCount()) / static_cast<double>(g_TimerMan.GetTicksPerSecond());
	m_BlastPulse *= std::exp(-static_cast<float>(std::clamp(pulseNow - m_BlastPulseLastTime, 0.0, 0.1)) * 9.0F);
	m_BlastPulseLastTime = pulseNow;
	m_TonemapShader->SetFloat("rteChromaticAberration", m_Settings.ChromaticAberration + (m_Settings.DistortionEnabled ? m_BlastPulse * 2.2F : 0.0F));
	m_TonemapShader->SetInt("rteAdaptedLuminance", 4);
	m_TonemapShader->SetFloat("rteAutoExposure", useAutoExposure ? m_Settings.AutoExposure : 0.0F);
	// The dead of night is meant to be dark: auto exposure mustn't brighten it back up, so the level it lifts dark scenes towards drops with the light.
	m_TonemapShader->SetFloat("rteAutoExposureLow", m_Settings.AutoExposureLow * m_NightDim);
	m_TonemapShader->SetFloat("rteAutoExposureHigh", m_Settings.AutoExposureHigh);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_2D, m_AdaptedLuminance[screenIndex][m_AdaptedLuminanceCurrent[screenIndex]].Texture);
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
