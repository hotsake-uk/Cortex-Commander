#include "PostProcessMan.h"
#include "SceneLighting.h"

#include "CameraMan.h"
#include "WindowMan.h"
#include "FrameMan.h"
#include "Scene.h"
#include "ContentFile.h"
#include "Matrix.h"
#include "Rectangles.h"
#include "Draw.h"
#include "RenderMan.h"

#include "PresetMan.h"
#include "GLStateMan.h"
#include "RenderTarget.h"

#include "GLCheck.h"
#include "glad/gl.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "tracy/Tracy.hpp"
#include "tracy/TracyOpenGL.hpp"
#include "raylib/raylib.h"

#include <array>

using namespace RTE;

PostProcessMan::PostProcessMan() {
	Clear();
}

PostProcessMan::~PostProcessMan() {
	Destroy();
}

void PostProcessMan::Clear() {
	m_PostScreenEffects.clear();
	m_PostSceneEffects.clear();
	m_YellowGlow = nullptr;
	m_YellowGlowHash = 0;
	m_RedGlow = nullptr;
	m_RedGlowHash = 0;
	m_BlueGlow = nullptr;
	m_BlueGlowHash = 0;
	m_TempEffectBitmaps.clear();
	m_BackBuffer8 = 0;
	m_Palette8Texture = 0;
	m_PostProcessFramebuffer = 0;
	m_VertexBuffer = 0;
	m_VertexArray = 0;
	for (int i = 0; i < c_MaxScreenCount; ++i) {
		m_ScreenRelativeEffects[i].clear();
	}
}

int PostProcessMan::Initialize() {
	InitializeGLPointers();
	CreateGLBackBuffers();

	m_Blit8 = std::make_unique<Shader>(g_PresetMan.GetFullModulePath("Base.rte/Shaders/Blit8.vert"), g_PresetMan.GetFullModulePath("Base.rte/Shaders/Blit8.frag"));
	m_PostProcessShader = std::make_unique<Shader>(g_PresetMan.GetFullModulePath("Base.rte/Shaders/PostProcess.vert"), g_PresetMan.GetFullModulePath("Base.rte/Shaders/PostProcess.frag"));
	// TODO: Make more robust and load more glows!
	ContentFile glowFile("Base.rte/Effects/Glows/YellowTiny.png");
	m_YellowGlow = glowFile.GetAsTexture();
	m_YellowGlowHash = glowFile.GetHash();
	glowFile.SetDataPath("Base.rte/Effects/Glows/RedTiny.png");
	m_RedGlow = glowFile.GetAsTexture();
	m_RedGlowHash = glowFile.GetHash();
	glowFile.SetDataPath("Base.rte/Effects/Glows/BlueTiny.png");
	m_BlueGlow = glowFile.GetAsTexture();
	m_BlueGlowHash = glowFile.GetHash();

	// Create temporary bitmaps to rotate post effects in.
	m_TempEffectBitmaps = {
	    {16, create_bitmap(16, 16)},
	    {32, create_bitmap(32, 32)},
	    {64, create_bitmap(64, 64)},
	    {128, create_bitmap(128, 128)},
	    {256, create_bitmap(256, 256)},
	    {512, create_bitmap(512, 512)}};

	return 0;
}

void PostProcessMan::InitializeGLPointers() {
	GL_CHECK(glGenTextures(1, &m_BackBuffer8));
	GL_CHECK(glGenTextures(1, &m_Palette8Texture));
	GL_CHECK(glGenVertexArrays(1, &m_VertexArray));
	GL_CHECK(glGenBuffers(1, &m_VertexBuffer));
}

void PostProcessMan::DestroyGLPointers() {
	GL_CHECK(glDeleteTextures(1, &m_BackBuffer8));
	GL_CHECK(glDeleteTextures(1, &m_Palette8Texture));
	GL_CHECK(glDeleteVertexArrays(1, &m_VertexArray));
	GL_CHECK(glDeleteBuffers(1, &m_VertexBuffer));
}

void PostProcessMan::CreateGLBackBuffers() {
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_BackBuffer8));
	GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_FrameMan.GetBackBuffer8()->w, g_FrameMan.GetBackBuffer8()->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_Palette8Texture));
	GL_CHECK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, c_PaletteEntriesNumber, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
	UpdatePalette();

	m_BlitFramebuffer = std::make_unique<RenderTarget>(FloatRect(0, 0 ,g_FrameMan.GetBackBuffer32()->w, g_FrameMan.GetBackBuffer32()->h), FloatRect(0, 0 ,g_FrameMan.GetBackBuffer32()->w, g_FrameMan.GetBackBuffer32()->h));
	m_PostProcessFramebuffer = std::make_unique<RenderTarget>(FloatRect(0, 0 ,g_FrameMan.GetBackBuffer32()->w, g_FrameMan.GetBackBuffer32()->h), FloatRect(0, 0 ,g_FrameMan.GetBackBuffer32()->w, g_FrameMan.GetBackBuffer32()->h));

	GL_CHECK(glActiveTexture(GL_TEXTURE0));
	m_ProjectionMatrix = std::make_unique<glm::mat4>(glm::ortho(0.0F, static_cast<float>(g_WindowMan.GetResX()), 0.0F, static_cast<float>(g_WindowMan.GetResY()), -1.0F, 1.0F));
}

void PostProcessMan::UpdatePalette() {
	GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_Palette8Texture));
	std::array<unsigned int, c_PaletteEntriesNumber> palette;
	for (int i = 0; i < c_PaletteEntriesNumber; ++i) {
		if (i == g_MaskColor) {
			palette[i] = 0;
			continue;
		}
		palette[i] = makeacol32(getr8(i), getg8(i), getb8(i), 255);
	}
	GL_CHECK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, c_PaletteEntriesNumber, 1, GL_RGBA, GL_UNSIGNED_BYTE, palette.data()));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GL_CHECK(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
}

void PostProcessMan::RegisterLight(const Vector& pos, const glm::vec3& color, float radius, float intensity) {
	if (radius <= 0.0F || intensity <= 0.0F || g_TimerMan.SimUpdatesSinceDrawn() < 0) {
		return;
	}
	glm::vec3 linearColor(std::pow(std::clamp(color.r, 0.0F, 255.0F) / 255.0F, 2.2F), std::pow(std::clamp(color.g, 0.0F, 255.0F) / 255.0F, 2.2F), std::pow(std::clamp(color.b, 0.0F, 255.0F) / 255.0F, 2.2F));
	m_SceneLights.push_back({pos, linearColor * intensity, radius});
}

void PostProcessMan::RegisterConeLight(const Vector& pos, const Vector& direction, float halfAngleDegrees, const glm::vec3& color, float radius, float intensity) {
	size_t before = m_SceneLights.size();
	RegisterLight(pos, color, radius, intensity);
	if (m_SceneLights.size() > before) {
		glm::vec2 dir(direction.m_X, direction.m_Y);
		float length = glm::length(dir);
		m_SceneLights.back().m_Direction = length > 0.0001F ? dir / length : glm::vec2(1.0F, 0.0F);
		m_SceneLights.back().m_ConeCos = std::cos(halfAngleDegrees * c_PI / 180.0F);
	}
}

void PostProcessMan::GetLightsWrapped(const Vector& boxPos, int boxWidth, int boxHeight, std::vector<SceneLight>& lights) const {
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	float sceneHeight = static_cast<float>(g_SceneMan.GetSceneHeight());
	for (const SceneLight& light: m_SceneLights) {
		// Try the light's position and its wrapped copies, keeping any that reach the box.
		for (int wrapX = -1; wrapX <= 1; ++wrapX) {
			if (wrapX != 0 && !g_SceneMan.SceneWrapsX()) {
				continue;
			}
			for (int wrapY = -1; wrapY <= 1; ++wrapY) {
				if (wrapY != 0 && !g_SceneMan.SceneWrapsY()) {
					continue;
				}
				Vector relativePos = light.m_Pos + Vector(wrapX * sceneWidth, wrapY * sceneHeight) - boxPos;
				if (relativePos.m_X + light.m_Radius >= 0 && relativePos.m_Y + light.m_Radius >= 0 && relativePos.m_X - light.m_Radius <= boxWidth && relativePos.m_Y - light.m_Radius <= boxHeight) {
					lights.push_back({relativePos, light.m_Color, light.m_Radius, light.m_Direction, light.m_ConeCos});
				}
			}
		}
	}
}

float PostProcessMan::GetSmoothSimTime() {
	return (static_cast<float>(g_TimerMan.GetSimUpdateCount()) + g_TimerMan.GetSimUpdateProportion()) * g_TimerMan.GetDeltaTimeSecs();
}

void PostProcessMan::RegisterShockwave(const Vector& pos, float energy) {
	// Only explosions, not every gibbing body part. Explosives release ~10k, limbs and items a few hundred.
	if (energy < 2000.0F || !m_LightingSettings.DistortionEnabled || m_LightingSettings.ShockwaveStrength <= 0.0F) {
		return;
	}
	Shockwave shockwave{pos, std::clamp(std::sqrt(energy) * 2.2F, 60.0F, 500.0F), std::clamp(energy / 2500.0F, 2.0F, 10.0F) * m_LightingSettings.ShockwaveStrength, GetSmoothSimTime()};
	std::scoped_lock lock(m_ShockwaveMutex);
	m_Shockwaves.push_back(shockwave);
}

void PostProcessMan::RegisterShimmer(const Vector& pos, float radius, float strength) {
	if (radius <= 1.0F || strength <= 0.0F || !m_LightingSettings.DistortionEnabled || g_TimerMan.SimUpdatesSinceDrawn() < 0) {
		return;
	}
	std::scoped_lock lock(m_ShockwaveMutex);
	if (m_Shimmers.size() < 64) {
		m_Shimmers.push_back({pos, radius, strength, 0.0F});
	}
}

void PostProcessMan::RegisterScorchMark(const Vector& pos, float energy) {
	if (energy < 2000.0F || !m_LightingSettings.ScorchMarks) {
		return;
	}
	ScorchMark mark{pos, std::clamp(std::sqrt(energy) * 0.45F, 14.0F, 90.0F), std::clamp(0.35F + energy / 40000.0F, 0.35F, 0.8F), GetSmoothSimTime()};
	std::scoped_lock lock(m_ShockwaveMutex);
	m_PendingScorchMarks.push_back(mark);
	m_HotScorchMarks.push_back(mark);
}

std::vector<PostProcessMan::ScorchMark> PostProcessMan::TakePendingScorchMarks() {
	std::scoped_lock lock(m_ShockwaveMutex);
	std::vector<ScorchMark> marks;
	marks.swap(m_PendingScorchMarks);
	return marks;
}

const std::vector<PostProcessMan::ScorchMark>& PostProcessMan::GetHotScorchMarks(float duration) {
	float now = GetSmoothSimTime();
	std::scoped_lock lock(m_ShockwaveMutex);
	std::erase_if(m_HotScorchMarks, [now, duration](const ScorchMark& mark) { return now - mark.m_StartTime > duration || now < mark.m_StartTime - 1.0F; });
	return m_HotScorchMarks;
}

void PostProcessMan::InvalidateSceneLighting() {
	if (m_SceneLighting) {
		m_SceneLighting->InvalidateWorld();
	}
	std::scoped_lock lock(m_ShockwaveMutex);
	m_PendingScorchMarks.clear();
	m_HotScorchMarks.clear();
}

void PostProcessMan::GetActiveShockwaves(std::vector<glm::vec4>& shockwaves) {
	const float duration = 0.55F;
	float now = GetSmoothSimTime();
	std::scoped_lock lock(m_ShockwaveMutex);
	for (const Shockwave& shockwave: m_Shockwaves) {
		float progress = std::clamp((now - shockwave.m_StartTime) / duration, 0.0F, 1.0F);
		shockwaves.emplace_back(shockwave.m_Pos.m_X, shockwave.m_Pos.m_Y, shockwave.m_Radius * progress, shockwave.m_Amplitude * (1.0F - progress));
	}
}

void PostProcessMan::GetShockwavesWrapped(const Vector& boxPos, int boxWidth, int boxHeight, std::vector<ScreenShockwave>& shockwaves) {
	const float duration = 0.55F;
	float now = GetSmoothSimTime();
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	float sceneHeight = static_cast<float>(g_SceneMan.GetSceneHeight());
	std::scoped_lock lock(m_ShockwaveMutex);
	std::erase_if(m_Shockwaves, [now, duration](const Shockwave& shockwave) { return now - shockwave.m_StartTime > duration || now < shockwave.m_StartTime - 1.0F; });
	for (const Shockwave& shockwave: m_Shockwaves) {
		float progress = std::clamp((now - shockwave.m_StartTime) / duration, 0.0F, 1.0F);
		for (int wrapX = -1; wrapX <= 1; ++wrapX) {
			if (wrapX != 0 && !g_SceneMan.SceneWrapsX()) {
				continue;
			}
			for (int wrapY = -1; wrapY <= 1; ++wrapY) {
				if (wrapY != 0 && !g_SceneMan.SceneWrapsY()) {
					continue;
				}
				Vector relativePos = shockwave.m_Pos + Vector(wrapX * sceneWidth, wrapY * sceneHeight) - boxPos;
				if (relativePos.m_X + shockwave.m_Radius >= 0 && relativePos.m_Y + shockwave.m_Radius >= 0 && relativePos.m_X - shockwave.m_Radius <= boxWidth && relativePos.m_Y - shockwave.m_Radius <= boxHeight) {
					shockwaves.push_back({glm::vec2(relativePos.m_X, relativePos.m_Y), shockwave.m_Radius, shockwave.m_Amplitude, progress});
				}
			}
		}
	}
	// Shimmers are the same ring held in place: its radius breathes in and out a little, each at its own pace.
	for (const Shockwave& shimmer: m_Shimmers) {
		Vector relativePos = shimmer.m_Pos - boxPos;
		if (g_SceneMan.SceneWrapsX()) {
			if (relativePos.m_X < (static_cast<float>(boxWidth) - sceneWidth) * 0.5F) {
				relativePos.m_X += sceneWidth;
			} else if (relativePos.m_X > (static_cast<float>(boxWidth) + sceneWidth) * 0.5F) {
				relativePos.m_X -= sceneWidth;
			}
		}
		if (relativePos.m_X + shimmer.m_Radius >= 0 && relativePos.m_Y + shimmer.m_Radius >= 0 && relativePos.m_X - shimmer.m_Radius <= boxWidth && relativePos.m_Y - shimmer.m_Radius <= boxHeight) {
			float wobble = 0.62F + 0.16F * std::sin(now * 3.1F + shimmer.m_Pos.m_X * 0.37F + shimmer.m_Pos.m_Y * 0.21F);
			// The ring's strength falls off with its progress in the shader; make up for that so the strength asked for is what shows.
			shockwaves.push_back({glm::vec2(relativePos.m_X, relativePos.m_Y), shimmer.m_Radius, shimmer.m_Amplitude * 3.0F / ((1.0F - wobble) * (1.0F - wobble)) * 0.15F, wobble});
		}
	}
}

void PostProcessMan::ApplySceneAtmosphere(const Scene* scene) {
	if (!m_PlayerAtmosphereCaptured) {
		m_PlayerAtmosphere = m_LightingSettings;
		m_PlayerAtmosphereCaptured = true;
	}
	m_LightingSettings.TimeOfDay = m_PlayerAtmosphere.TimeOfDay;
	m_LightingSettings.DayLengthMinutes = m_PlayerAtmosphere.DayLengthMinutes;
	m_LightingSettings.WeatherType = m_PlayerAtmosphere.WeatherType;
	m_LightingSettings.WeatherIntensity = m_PlayerAtmosphere.WeatherIntensity;
	m_LightingSettings.Wind = m_PlayerAtmosphere.Wind;
	m_LightingSettings.SkyColor = m_PlayerAtmosphere.SkyColor;
	m_LightingSettings.Ambient = m_PlayerAtmosphere.Ambient;
	m_LightingSettings.Temperature = m_PlayerAtmosphere.Temperature;
	m_LightingSettings.Tint = m_PlayerAtmosphere.Tint;
	m_LightingSettings.Contrast = m_PlayerAtmosphere.Contrast;
	m_LightingSettings.ShadowTint = m_PlayerAtmosphere.ShadowTint;
	m_LightingSettings.HighlightTint = m_PlayerAtmosphere.HighlightTint;
	if (!scene) {
		ApplyActivityAtmosphere();
		return;
	}
	const Scene::Atmosphere& atmosphere = scene->GetAtmosphere();
	if (atmosphere.TimeOfDay >= 0.0F) {
		m_LightingSettings.TimeOfDay = std::fmod(atmosphere.TimeOfDay, 24.0F);
	}
	if (atmosphere.DayLengthMinutes >= 0.0F) {
		m_LightingSettings.DayLengthMinutes = atmosphere.DayLengthMinutes;
	}
	if (atmosphere.WeatherType >= 0) {
		m_LightingSettings.WeatherType = std::clamp(atmosphere.WeatherType, 0, 4);
	}
	if (atmosphere.WeatherIntensity >= 0.0F) {
		m_LightingSettings.WeatherIntensity = std::clamp(atmosphere.WeatherIntensity, 0.0F, 1.0F);
	}
	if (atmosphere.Wind > -10000.0F) {
		m_LightingSettings.Wind = atmosphere.Wind;
	}
	ApplyActivityAtmosphere();
}

void PostProcessMan::ApplyActivityAtmosphere() {
	if (m_ActivityTimeOfDay >= 0.0F) {
		m_LightingSettings.TimeOfDay = std::fmod(m_ActivityTimeOfDay, 24.0F);
		m_LightingSettings.DayLengthMinutes = 0.0F;
	}
	if (m_ActivityWeather >= 0) {
		m_LightingSettings.WeatherType = std::clamp(m_ActivityWeather, 0, 4);
		if (m_LightingSettings.WeatherType > 0) {
			m_LightingSettings.WeatherIntensity = std::max(m_LightingSettings.WeatherIntensity, 0.6F);
		}
	}
}

LightingSettings PostProcessMan::GetLightingSettingsToSave() const {
	LightingSettings settings = m_LightingSettings;
	if (m_PlayerAtmosphereCaptured) {
		settings.TimeOfDay = m_PlayerAtmosphere.TimeOfDay;
		settings.DayLengthMinutes = m_PlayerAtmosphere.DayLengthMinutes;
		settings.WeatherType = m_PlayerAtmosphere.WeatherType;
		settings.WeatherIntensity = m_PlayerAtmosphere.WeatherIntensity;
		settings.Wind = m_PlayerAtmosphere.Wind;
		settings.SkyColor = m_PlayerAtmosphere.SkyColor;
		settings.Ambient = m_PlayerAtmosphere.Ambient;
		settings.Temperature = m_PlayerAtmosphere.Temperature;
		settings.Tint = m_PlayerAtmosphere.Tint;
		settings.Contrast = m_PlayerAtmosphere.Contrast;
		settings.ShadowTint = m_PlayerAtmosphere.ShadowTint;
		settings.HighlightTint = m_PlayerAtmosphere.HighlightTint;
	}
	return settings;
}

SceneLighting* PostProcessMan::GetSceneLighting() {
	if (!m_SceneLighting) {
		m_SceneLighting = std::make_unique<SceneLighting>(m_LightingSettings);
	}
	return m_SceneLighting.get();
}

void PostProcessMan::Destroy() {
	for (std::pair<int, BITMAP*> tempBitmapEntry: m_TempEffectBitmaps) {
		destroy_bitmap(tempBitmapEntry.second);
	}
	m_SceneLighting.reset();
	DestroyGLPointers();
	ClearScreenPostEffects();
	ClearScenePostEffects();
	Clear();
}

void PostProcessMan::AdjustEffectsPosToPlayerScreen(int playerScreen, BITMAP* targetBitmap, const Vector& targetBitmapOffset, std::list<PostEffect>& screenRelativeEffectsList, std::list<Box>& screenRelativeGlowBoxesList) {
	int screenOcclusionOffsetX = g_CameraMan.GetScreenOcclusion(playerScreen).GetFloorIntX();
	int screenOcclusionOffsetY = g_CameraMan.GetScreenOcclusion(playerScreen).GetFloorIntY();
	int occludedOffsetX = targetBitmap->w + screenOcclusionOffsetX;
	int occludedOffsetY = targetBitmap->h + screenOcclusionOffsetY;

	// Adjust for the player screen's position on the final buffer
	for (const PostEffect& postEffect: screenRelativeEffectsList) {
		// Make sure we won't be adding any effects to a part of the screen that is occluded by menus and such
		if (postEffect.m_Pos.GetFloorIntX() > screenOcclusionOffsetX && postEffect.m_Pos.GetFloorIntY() > screenOcclusionOffsetY && postEffect.m_Pos.GetFloorIntX() < occludedOffsetX && postEffect.m_Pos.GetFloorIntY() < occludedOffsetY) {
			m_PostScreenEffects.emplace_back(postEffect.m_Pos, postEffect.m_Bitmap, postEffect.m_BitmapHash, postEffect.m_Strength, postEffect.m_Angle, postEffect.m_NoLight);
		}
	}
	// Adjust glow areas for the player screen's position on the final buffer
	for (const Box& glowBox: screenRelativeGlowBoxesList) {
		m_PostScreenGlowBoxes.push_back(glowBox);
		// Adjust each added glow area for the player screen's position on the final buffer
		m_PostScreenGlowBoxes.back().m_Corner += targetBitmapOffset;
	}
}

void PostProcessMan::RegisterPostEffect(const Vector& effectPos, std::shared_ptr<BitmapTexture> effect, size_t hash, int strength, float angle) {
	// These effects get applied when there's a drawn frame that followed one or more sim updates.
	// They are not only registered on drawn sim updates; flashes and stuff could be missed otherwise if they occur on undrawn sim updates.

	if (effect && g_TimerMan.SimUpdatesSinceDrawn() >= 0) {
		m_PostSceneEffects.push_back(PostEffect(effectPos, effect, hash, strength, angle));
	}
}

bool PostProcessMan::GetPostScreenEffectsWrapped(const Vector& boxPos, int boxWidth, int boxHeight, std::list<PostEffect>& effectsList, int team) {
	bool found = false;

	// Do the first unwrapped rect
	found = GetPostScreenEffects(boxPos, boxWidth, boxHeight, effectsList, team);

	int left = boxPos.GetFloorIntX();
	int top = boxPos.GetFloorIntY();
	int right = left + boxWidth;
	int bottom = top + boxHeight;

	if (g_SceneMan.SceneWrapsX()) {
		int sceneWidth = g_SceneMan.GetScene()->GetWidth();
		if (left < 0) {
			found = GetPostScreenEffects(left + sceneWidth, top, right + sceneWidth, bottom, effectsList, team) || found;
		}
		if (right >= sceneWidth) {
			found = GetPostScreenEffects(left - sceneWidth, top, right - sceneWidth, bottom, effectsList, team) || found;
		}
	}
	if (g_SceneMan.SceneWrapsY()) {
		int sceneHeight = g_SceneMan.GetScene()->GetHeight();
		if (top < 0) {
			found = GetPostScreenEffects(left, top + sceneHeight, right, bottom + sceneHeight, effectsList, team) || found;
		}
		if (bottom >= sceneHeight) {
			found = GetPostScreenEffects(left, top - sceneHeight, right, bottom - sceneHeight, effectsList, team) || found;
		}
	}
	return found;
}

BITMAP* PostProcessMan::GetTempEffectBitmap(BITMAP* bitmap) const {
	// Get the largest dimension of the bitmap and convert it to a multiple of 16, i.e. 16, 32, etc
	int bitmapSizeNeeded = static_cast<int>(std::ceil(static_cast<float>(std::max(bitmap->w, bitmap->h)) / 16.0F)) * 16;
	std::unordered_map<int, BITMAP*>::const_iterator correspondingBitmapSizeEntry = m_TempEffectBitmaps.find(bitmapSizeNeeded);

	// If we didn't find a match then the bitmap size is greater than 512 but that's the biggest we've got, so return it
	if (correspondingBitmapSizeEntry == m_TempEffectBitmaps.end()) {
		correspondingBitmapSizeEntry = m_TempEffectBitmaps.find(512);
	}

	return correspondingBitmapSizeEntry->second;
}

void PostProcessMan::RegisterGlowDotEffect(const Vector& effectPos, DotGlowColor color, int strength) {
	// These effects only apply only once per drawn sim update, and only on the first frame drawn after one or more sim updates
	if (color != NoDot && g_TimerMan.DrawnSimUpdate() && g_TimerMan.SimUpdatesSinceDrawn() >= 0) {
		// Aiming dots glow, but only light the scene around them if that's turned on.
		if (std::shared_ptr<BitmapTexture> effect = GetDotGlowEffect(color); effect && g_TimerMan.SimUpdatesSinceDrawn() >= 0) {
			m_PostSceneEffects.push_back(PostEffect(effectPos, effect, GetDotGlowEffectHash(color), strength, 0.0F, !m_LightingSettings.AimDotsLight));
		}
	}
}

bool PostProcessMan::GetGlowAreasWrapped(const Vector& boxPos, int boxWidth, int boxHeight, std::list<Box>& areaList) const {
	bool foundAny = false;
	Vector intRectPosRelativeToBox;

	// Account for wrapping in any registered glow IntRects, as well as on the box we're testing against
	std::list<IntRect> wrappedGlowRects;

	for (const IntRect& glowArea: m_GlowAreas) {
		g_SceneMan.WrapRect(glowArea, wrappedGlowRects);
	}
	std::list<IntRect> wrappedTestRects;
	g_SceneMan.WrapRect(IntRect(boxPos.GetFloorIntX(), boxPos.GetFloorIntY(), boxPos.GetFloorIntX() + boxWidth, boxPos.GetFloorIntY() + boxHeight), wrappedTestRects);

	// Check for intersections. If any are found, cut down the intersecting IntRect to the bounds of the IntRect we're testing against, then make and store a Box out of it
	for (IntRect& wrappedTestRect: wrappedTestRects) {
		for (const IntRect& wrappedGlowRect: wrappedGlowRects) {
			if (wrappedTestRect.Intersects(wrappedGlowRect)) {
				IntRect cutRect(wrappedGlowRect);
				cutRect.IntersectionCut(wrappedTestRect);
				intRectPosRelativeToBox = Vector(static_cast<float>(cutRect.m_Left) - boxPos.m_X, static_cast<float>(cutRect.m_Top) - boxPos.m_Y);
				areaList.push_back(Box(intRectPosRelativeToBox, static_cast<float>(cutRect.m_Right - cutRect.m_Left), static_cast<float>(cutRect.m_Bottom - cutRect.m_Top)));
				foundAny = true;
			}
		}
	}
	return foundAny;
}

bool PostProcessMan::GetPostScreenEffects(Vector boxPos, int boxWidth, int boxHeight, std::list<PostEffect>& effectsList, int team) {
	bool found = false;
	bool unseen = false;
	Vector postEffectPosRelativeToBox;

	if (g_SceneMan.GetScene()) {
		for (PostEffect& scenePostEffect: m_PostSceneEffects) {
			if (team != Activity::NoTeam) {
				unseen = g_SceneMan.IsUnseen(scenePostEffect.m_Pos.GetFloorIntX(), scenePostEffect.m_Pos.GetFloorIntY(), team);
			}

			if (WithinBox(scenePostEffect.m_Pos, boxPos, static_cast<float>(boxWidth), static_cast<float>(boxHeight)) && !unseen) {
				found = true;
				postEffectPosRelativeToBox = scenePostEffect.m_Pos - boxPos;
				effectsList.push_back(PostEffect(postEffectPosRelativeToBox, scenePostEffect.m_Bitmap, scenePostEffect.m_BitmapHash, scenePostEffect.m_Strength, scenePostEffect.m_Angle, scenePostEffect.m_NoLight));
			}
		}
	}
	return found;
}

bool PostProcessMan::GetPostScreenEffects(int left, int top, int right, int bottom, std::list<PostEffect>& effectsList, int team) {
	bool found = false;
	bool unseen = false;
	Vector postEffectPosRelativeToBox;

	for (PostEffect& scenePostEffect: m_PostSceneEffects) {
		if (team != Activity::NoTeam) {
			unseen = g_SceneMan.IsUnseen(scenePostEffect.m_Pos.GetFloorIntX(), scenePostEffect.m_Pos.GetFloorIntY(), team);
		}

		if (WithinBox(scenePostEffect.m_Pos, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom)) && !unseen) {
			found = true;
			postEffectPosRelativeToBox = Vector(scenePostEffect.m_Pos.m_X - static_cast<float>(left), scenePostEffect.m_Pos.m_Y - static_cast<float>(top));
			effectsList.push_back(PostEffect(postEffectPosRelativeToBox, scenePostEffect.m_Bitmap, scenePostEffect.m_BitmapHash, scenePostEffect.m_Strength, scenePostEffect.m_Angle, scenePostEffect.m_NoLight));
		}
	}
	return found;
}

std::shared_ptr<BitmapTexture> PostProcessMan::GetDotGlowEffect(DotGlowColor whichColor) const {
	switch (whichColor) {
		case NoDot:
			return nullptr;
		case YellowDot:
			return m_YellowGlow;
		case RedDot:
			return m_RedGlow;
		case BlueDot:
			return m_BlueGlow;
		default:
			RTEAbort("Undefined glow dot color value passed in. See DotGlowColor enumeration for defined values.");
			return nullptr;
	}
}

size_t PostProcessMan::GetDotGlowEffectHash(DotGlowColor whichColor) const {
	switch (whichColor) {
		case NoDot:
			return 0;
		case YellowDot:
			return m_YellowGlowHash;
		case RedDot:
			return m_RedGlowHash;
		case BlueDot:
			return m_BlueGlowHash;
		default:
			RTEAbort("Undefined glow dot color value passed in. See DotGlowColor enumeration for defined values.");
			return 0;
	}
}

void PostProcessMan::PostProcess() {
	ZoneScoped;
	TracyGpuZone("PostProcess");
	UpdatePalette();

	// First copy the current 8bpp backbuffer to the 32bpp buffer; we'll add effects to it
	m_PostProcessFramebuffer->Begin(true);
	g_RenderMan.BeginFrame(nullptr);
	Draw::DrawTexture(g_FrameMan.GetBackBuffer()->GetColorTexture().lock().get(), {-1.0f, -1.0f, 2.0f, 2.0f})->m_Indexed = false;
	m_PostProcessFramebuffer->End();

	// Set the screen blender mode for glows
	m_PostProcessFramebuffer->Begin(false, false);

	g_RenderMan.BeginFrame(nullptr);
	g_RenderMan.SetActiveBlendMode(Blend::SCREEN);

	DrawDotGlowEffects();
	DrawPostScreenEffects();

	// Clear the effects list for this frame
	m_PostScreenEffects.clear();
	m_PostProcessFramebuffer->End();
}

void PostProcessMan::DrawDotGlowEffects() {
	int startX = 0;
	int startY = 0;
	int endX = 0;
	int endY = 0;
	int testpixel = 0;

	// Randomly sample the entire backbuffer, looking for pixels to put a glow on.
	for (const Box& glowBox: m_PostScreenGlowBoxes) {
		startX = glowBox.m_Corner.GetFloorIntX();
		startY = glowBox.m_Corner.GetFloorIntY();
		endX = startX + static_cast<int>(glowBox.m_Width);
		endY = startY + static_cast<int>(glowBox.m_Height);

		// Sanity check a little at least
		if (startX < 0 || startX >= g_FrameMan.GetBackBuffer8()->w || startY < 0 || startY >= g_FrameMan.GetBackBuffer8()->h ||
		    endX < 0 || endX >= g_FrameMan.GetBackBuffer8()->w || endY < 0 || endY >= g_FrameMan.GetBackBuffer8()->h) {
			continue;
		}

#ifdef DEBUG_BUILD
		// Draw a rectangle around the glow box so we see it's position and size
		rect(g_FrameMan.GetBackBuffer32(), startX, startY, endX, endY, g_RedColor);
#endif

		for (int y = startY; y < endY; ++y) {
			for (int x = startX; x < endX; ++x) {
				testpixel = _getpixel(g_FrameMan.GetBackBuffer8(), x, y);

				// YELLOW
				if ((testpixel == g_YellowGlowColor && RandomNum() < 0.9F) || testpixel == 98 || (testpixel == 120 && RandomNum() < 0.7F)) {
					Draw::DrawTexture(m_YellowGlow.get(),  std::floor(x -m_YellowGlow->GetDimensions().w / 2.0f), std::floor(y - m_YellowGlow->GetDimensions().h / 2.0f));
				}
				// TODO: Enable and add more colors once we actually have something that needs these.
				// RED
				/*
				if (testpixel == 13) {
				    draw_trans_sprite(m_BackBuffer32, m_RedGlow, x - 2, y - 2);
				}
				// BLUE
				if (testpixel == 166) {
				    draw_trans_sprite(g_FrameMan.GetBackBuffer32(), m_BlueGlow, x - 2, y - 2);
				}
				*/
			}
		}
	}
}

void PostProcessMan::DrawPostScreenEffects() {
	int effectPosX = 0;
	int effectPosY = 0;
	unsigned char effectStrength = 0;

	for (const PostEffect& postEffect: m_PostScreenEffects) {
		if (postEffect.m_Bitmap) {
			effectStrength = postEffect.m_Strength;
			effectPosX = postEffect.m_Pos.GetFloorIntX();
			effectPosY = postEffect.m_Pos.GetFloorIntY();
			glm::vec2 effectDims(postEffect.m_Bitmap->GetDimensions().w, postEffect.m_Bitmap->GetDimensions().h);
			Draw::DrawTexture(postEffect.m_Bitmap.get(), glm::vec2(effectPosX, effectPosY), -effectDims / 2.0F, -postEffect.m_Angle, glm::vec2(1.0F), {effectStrength, effectStrength, effectStrength, 255})->m_Indexed = false;
		}
	}
}
