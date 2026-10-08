#include "PostProcessMan.h"
#include "SceneLighting.h"
#include "Weather.h"

#include "CameraMan.h"
#include "TimerMan.h"
#include "WindowMan.h"
#include "FrameMan.h"
#include "Scene.h"
#include "ContentFile.h"
#include "Matrix.h"
#include "Rectangles.h"
#include "Draw.h"
#include "RenderMan.h"

#include "PresetMan.h"
#include "ActivityMan.h"
#include "Activity.h"
#include "ConsoleMan.h"
#include "GLStateMan.h"
#include "RenderTarget.h"

#include "GLCheck.h"
#include "glad/gl.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "tracy/Tracy.hpp"
#include <fstream>
#include <cctype>
#include <algorithm>
#include <sstream>
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

void PostProcessMan::SetPalettePulse(int paletteIndex, float low, float high, float period, float phase) {
	g_RenderMan.SetPalettePulse(paletteIndex, low, high, period, phase);
}

void PostProcessMan::SetPaletteCycle(int from, int to, float period) {
	g_RenderMan.SetPaletteCycle(from, to, period);
}

void PostProcessMan::ClearPaletteAnimation() {
	g_RenderMan.ClearPaletteAnimation();
}

void PostProcessMan::SetWeatherType(int weatherType) {
	m_LightingSettings.WeatherType = std::clamp(weatherType, 0, Weather::GetSlotCount() - 1);
	m_LightingSettings.WeatherName.clear();
}

std::string PostProcessMan::GetWeatherName() const {
	const Weather* weather = Weather::GetSlot(m_LightingSettings.WeatherType);
	return weather ? weather->GetPresetName() : "Clear";
}

void PostProcessMan::SetWeatherName(const std::string& name) {
	int slot = Weather::FindSlot(name);
	if (slot < 0) {
		g_ConsoleMan.PrintString("ERROR: there's no weather called \"" + name + "\" (an AddWeather = Weather preset); the weather stays as it is.");
		return;
	}
	SetWeatherType(slot);
}

std::string PostProcessMan::GetWeatherSound() const {
	const Weather* weather = Weather::GetSlot(m_LightingSettings.WeatherType, m_LightingSettings.CustomWeather);
	return weather ? weather->GetParams().Sound : "";
}

void PostProcessMan::ResolveWeather() {
	for (LightingSettings* settings: {&m_LightingSettings, &m_PlayerAtmosphere}) {
		if (!settings->WeatherName.empty()) {
			if (int slot = Weather::FindSlot(settings->WeatherName); slot >= 0) {
				settings->WeatherType = slot;
			}
			settings->WeatherName.clear();
		}
		settings->WeatherType = std::clamp(settings->WeatherType, 0, Weather::GetSlotCount() - 1);
	}
}

void PostProcessMan::SetPostShader(const std::string& shaderName) {
	std::scoped_lock lock(m_PostShaderMutex);
	m_PostShaderName = shaderName == "None" ? "" : shaderName;
}

std::string PostProcessMan::GetPostShader() const {
	std::scoped_lock lock(m_PostShaderMutex);
	return m_PostShaderName;
}

const Shader* PostProcessMan::GetActivePostShader() {
	std::string name = GetPostShader();
	if (name.empty() && g_ActivityMan.GetActivity()) {
		name = g_ActivityMan.GetActivity()->GetPostShader();
	}
	if (name.empty() && g_SceneMan.GetScene()) {
		name = g_SceneMan.GetScene()->GetAtmosphere().PostShader;
	}
	if (name != m_ActivePostShaderName) {
		m_ActivePostShaderName = name;
		m_ActivePostShader = nullptr;
		if (!name.empty()) {
			const Shader* shader = dynamic_cast<const Shader*>(g_PresetMan.GetEntityPreset("Shader", name));
			if (!shader) {
				g_ConsoleMan.PrintString("ERROR: The post shader \"" + name + "\" isn't defined. The screen is drawn without it.");
			} else if (shader->IsValid()) {
				m_ActivePostShader = shader;
			}
		}
	}
	return m_ActivePostShader;
}

int PostProcessMan::GetSceneLook() const {
	if (const Activity* activity = g_ActivityMan.GetActivity(); activity && !activity->GetLook().empty()) {
		return LightingSettings::FindLook(activity->GetLook());
	}
	if (const Scene* scene = g_SceneMan.GetScene(); scene && !scene->GetAtmosphere().Look.empty()) {
		return LightingSettings::FindLook(scene->GetAtmosphere().Look);
	}
	return -1;
}

void PostProcessMan::LoadPaletteAnimation() {
	m_PaletteAnimationLoaded = true;
	std::ifstream file(g_PresetMan.GetFullModulePath("Base.rte/PaletteAnimation.ini"));
	std::string line;
	while (std::getline(file, line)) {
		size_t comment = line.find("//");
		line = line.substr(0, comment);
		size_t equals = line.find('=');
		if (equals == std::string::npos) {
			continue;
		}
		std::string key = line.substr(0, equals);
		key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c); }), key.end());
		std::string values = line.substr(equals + 1);
		std::replace(values.begin(), values.end(), ',', ' ');
		std::istringstream numbers(values);
		if (key == "Pulse") {
			int index = 0;
			float low = 0.0F, high = 0.0F, period = 0.0F, phase = 0.0F;
			if (numbers >> index >> low >> high >> period) {
				numbers >> phase;
				g_RenderMan.SetPalettePulse(index, low, high, period, phase);
			}
		} else if (key == "Cycle") {
			int from = 0, to = 0;
			float period = 0.0F;
			if (numbers >> from >> to >> period) {
				g_RenderMan.SetPaletteCycle(from, to, period);
			}
		}
	}
}

void PostProcessMan::RegisterLight(const Vector& pos, const glm::vec3& color, float radius, float intensity, LightSource source, bool steady) {
	SceneLight light;
	if (MakeSceneLight(pos, color, radius, intensity, light)) {
		light.m_Source = source;
		light.m_Steady = steady;
		std::scoped_lock lock(m_SceneLightsMutex);
		m_SceneLights.push_back(light);
	}
}

void PostProcessMan::RegisterConeLight(const Vector& pos, const Vector& direction, float halfAngleDegrees, const glm::vec3& color, float radius, float intensity, LightSource source) {
	SceneLight light;
	if (MakeSceneLight(pos, color, radius, intensity, light)) {
		light.m_Source = source;
		glm::vec2 dir(direction.m_X, direction.m_Y);
		float length = glm::length(dir);
		light.m_Direction = length > 0.0001F ? dir / length : glm::vec2(1.0F, 0.0F);
		light.m_ConeCos = std::cos(halfAngleDegrees * c_PI / 180.0F);
		std::scoped_lock lock(m_SceneLightsMutex);
		m_SceneLights.push_back(light);
	}
}

bool PostProcessMan::MakeSceneLight(const Vector& pos, const glm::vec3& color, float radius, float intensity, SceneLight& light) const {
	if (radius <= 0.0F || intensity <= 0.0F || g_TimerMan.SimUpdatesSinceDrawn() < 0) {
		return false;
	}
	glm::vec3 linearColor(std::pow(std::clamp(color.r, 0.0F, 255.0F) / 255.0F, 2.2F), std::pow(std::clamp(color.g, 0.0F, 255.0F) / 255.0F, 2.2F), std::pow(std::clamp(color.b, 0.0F, 255.0F) / 255.0F, 2.2F));
	light.m_Pos = pos;
	light.m_Color = linearColor * intensity;
	light.m_Radius = radius;
	return true;
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
					lights.push_back({relativePos, light.m_Color, light.m_Radius, light.m_Direction, light.m_ConeCos, light.m_Source, light.m_Steady});
				}
			}
		}
	}
	// Lightning lights up where it strikes and the air along it, for as long as the bolt shows.
	std::vector<LightningBolt> bolts;
	{
		std::scoped_lock lock(m_LightningMutex);
		bolts = m_LightningBolts;
	}
	float now = GetSmoothSimTime();
	for (const LightningBolt& bolt: bolts) {
		float flash = LightningFlash(now - bolt.StartTime) * std::clamp(m_LightingSettings.LightningBrightness, 0.2F, 2.0F);
		if (flash <= 0.01F) {
			continue;
		}
		for (int wrapX = -1; wrapX <= 1; ++wrapX) {
			if (wrapX != 0 && !g_SceneMan.SceneWrapsX()) {
				continue;
			}
			const glm::vec2 places[2] = {bolt.To, (bolt.From + bolt.To) * 0.5F};
			for (int place = 0; place < 2; ++place) {
				float radius = place == 0 ? 460.0F : 300.0F;
				Vector relativePos = Vector(places[place].x + static_cast<float>(wrapX) * sceneWidth, places[place].y) - boxPos;
				if (relativePos.m_X + radius >= 0 && relativePos.m_Y + radius >= 0 && relativePos.m_X - radius <= boxWidth && relativePos.m_Y - radius <= boxHeight) {
					lights.push_back({relativePos, glm::vec3(0.62F, 0.68F, 1.0F) * flash * (place == 0 ? 5.0F : 2.5F), radius, glm::vec2(1.0F, 0.0F), -2.0F, LightSource::Other});
				}
			}
		}
	}
}

float PostProcessMan::GetDynamicLightAt(const Vector& pos) const {
	float lit = 0.0F;
	for (const SceneLight& light: m_LastSceneLights) {
		if (light.m_Radius <= 0.0F) {
			continue;
		}
		Vector toLight = g_SceneMan.ShortestDistance(pos, light.m_Pos, g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY());
		if (!toLight.MagnitudeIsLessThan(light.m_Radius)) {
			continue;
		}
		// A cone light (a headlamp, a flashlight) lights what is in its cone only.
		if (light.m_ConeCos >= -1.0F && !toLight.IsZero()) {
			Vector fromLight = -toLight;
			fromLight.Normalize();
			if (fromLight.m_X * light.m_Direction.x + fromLight.m_Y * light.m_Direction.y < light.m_ConeCos) {
				continue;
			}
		}
		float brightness = std::clamp(glm::dot(light.m_Color, glm::vec3(0.2126F, 0.7152F, 0.0722F)), 0.0F, 1.0F);
		lit = std::max(lit, brightness * (1.0F - toLight.GetMagnitude() / light.m_Radius));
	}
	return lit;
}

float PostProcessMan::GetSmoothSimTime() {
	return static_cast<float>(GetSmoothSimTimePrecise());
}

double PostProcessMan::GetSmoothSimTimePrecise() {
	return (static_cast<double>(g_TimerMan.GetSimUpdateCount()) + static_cast<double>(g_TimerMan.GetSimUpdateProportion())) * static_cast<double>(g_TimerMan.GetDeltaTimeSecs());
}

float PostProcessMan::GetEffectTime() {
	return static_cast<float>(std::fmod(GetSmoothSimTimePrecise(), c_EffectTimePeriod));
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

void PostProcessMan::RegisterFog(const Vector& pos, float radius, float amount) {
	if (radius <= 0.0F || amount <= 0.0F || !m_LightingSettings.Enabled || m_LightingSettings.FogVolume <= 0.0F) {
		return;
	}
	std::scoped_lock lock(m_ShockwaveMutex);
	// A burst of a hundred steam puffs is still one thick patch; past this many in a frame the oldest are dropped.
	if (m_FogPuffs.size() >= 64) {
		m_FogPuffs.erase(m_FogPuffs.begin());
	}
	m_FogPuffs.emplace_back(pos.m_X, pos.m_Y, std::min(radius, 400.0F), std::min(amount, 1.0F));
}

std::vector<glm::vec4> PostProcessMan::TakeFogPuffs() {
	std::scoped_lock lock(m_ShockwaveMutex);
	std::vector<glm::vec4> puffs;
	puffs.swap(m_FogPuffs);
	return puffs;
}

void PostProcessMan::RegisterLightningBolt(const Vector& from, const Vector& to, unsigned int seed) {
	if (!m_LightingSettings.LightningBolts) {
		return;
	}
	float now = GetSmoothSimTime();
	{
		std::scoped_lock lock(m_LightningMutex);
		m_LightningBolts.erase(std::remove_if(m_LightningBolts.begin(), m_LightningBolts.end(), [now](const LightningBolt& bolt) { return now - bolt.StartTime > c_LightningBoltSeconds || now < bolt.StartTime; }), m_LightningBolts.end());
		if (m_LightningBolts.size() < 16) {
			m_LightningBolts.push_back({glm::vec2(from.m_X, from.m_Y), glm::vec2(to.m_X, to.m_Y), seed, now});
		}
	}
	if (m_SceneLighting) {
		m_SceneLighting->TriggerLightning();
	}
}

namespace {
	/// Builds a lightning bolt's jagged line from one point to another by splitting each piece at a displaced midpoint, five times over, with a few
	/// dimmer forks. The shape depends on the seed alone.
	void BuildLightningBolt(glm::vec2 from, glm::vec2 to, unsigned int& random, float width, float brightness, int levels, int forks, std::vector<RTE::LightningBoltSegment>& segments) {
		auto next = [&random]() {
			random = random * 1664525u + 1013904223u;
			return static_cast<float>(random >> 8) / static_cast<float>(1u << 24);
		};
		std::vector<glm::vec2> points{from, to};
		std::vector<glm::vec2> split;
		struct Fork {
			glm::vec2 From, To;
		};
		std::vector<Fork> forkList;
		for (int level = 0; level < levels; ++level) {
			split.clear();
			for (size_t i = 0; i + 1 < points.size(); ++i) {
				glm::vec2 a = points[i];
				glm::vec2 b = points[i + 1];
				glm::vec2 along = b - a;
				float length = glm::length(along);
				glm::vec2 across = length > 0.001F ? glm::vec2(-along.y, along.x) / length : glm::vec2(1.0F, 0.0F);
				glm::vec2 middle = (a + b) * 0.5F + across * (next() - 0.5F) * length * 0.4F;
				split.push_back(a);
				split.push_back(middle);
				// Forks leave from the coarser bends, angled off downwards.
				if (level >= 1 && level <= 2 && static_cast<int>(forkList.size()) < forks && next() < 0.35F) {
					float turn = (next() < 0.5F ? -1.0F : 1.0F) * (0.4F + next() * 0.5F);
					glm::vec2 direction = length > 0.001F ? along / length : glm::vec2(0.0F, 1.0F);
					glm::vec2 turned(direction.x * std::cos(turn) - direction.y * std::sin(turn), direction.x * std::sin(turn) + direction.y * std::cos(turn));
					forkList.push_back({middle, middle + turned * length * (0.7F + next() * 0.6F)});
				}
			}
			split.push_back(points.back());
			points.swap(split);
		}
		for (size_t i = 0; i + 1 < points.size(); ++i) {
			segments.push_back({points[i], points[i + 1], width, brightness});
		}
		for (const Fork& fork: forkList) {
			BuildLightningBolt(fork.From, fork.To, random, width * 0.55F, brightness * 0.55F, 3, 0, segments);
		}
	}
}

void PostProcessMan::GetLightningBolts(const Vector& boxPos, int boxWidth, int boxHeight, std::vector<LightningBoltSegment>& segments) const {
	std::vector<LightningBolt> bolts;
	{
		std::scoped_lock lock(m_LightningMutex);
		bolts = m_LightningBolts;
	}
	float now = GetSmoothSimTime();
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	glm::vec2 box(boxPos.m_X, boxPos.m_Y);
	for (const LightningBolt& bolt: bolts) {
		float brightness = LightningFlash(now - bolt.StartTime) * std::clamp(m_LightingSettings.LightningBrightness, 0.2F, 2.0F);
		if (brightness <= 0.01F) {
			continue;
		}
		// The copy of the bolt nearest the box, on a wrapping scene.
		glm::vec2 shift(0.0F);
		if (g_SceneMan.SceneWrapsX() && sceneWidth > 0.0F) {
			float centre = (bolt.From.x + bolt.To.x) * 0.5F - (box.x + static_cast<float>(boxWidth) * 0.5F);
			shift.x = -std::round(centre / sceneWidth) * sceneWidth;
		}
		float left = std::min(bolt.From.x, bolt.To.x) + shift.x - box.x;
		float right = std::max(bolt.From.x, bolt.To.x) + shift.x - box.x;
		float top = std::min(bolt.From.y, bolt.To.y) - box.y;
		float bottom = std::max(bolt.From.y, bolt.To.y) - box.y;
		float margin = glm::distance(bolt.From, bolt.To) * 0.5F;
		if (right + margin < 0.0F || left - margin > static_cast<float>(boxWidth) || bottom + margin < 0.0F || top - margin > static_cast<float>(boxHeight)) {
			continue;
		}
		unsigned int random = bolt.Seed * 2654435761u + 12345u;
		BuildLightningBolt(bolt.From + shift - box, bolt.To + shift - box, random, 1.4F, brightness, 5, 4, segments);
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

std::vector<PostProcessMan::ScorchMark> PostProcessMan::GetHotScorchMarks(float duration) {
	float now = GetSmoothSimTime();
	std::scoped_lock lock(m_ShockwaveMutex);
	std::erase_if(m_HotScorchMarks, [now, duration](const ScorchMark& mark) { return now - mark.m_StartTime > duration || now < mark.m_StartTime - 1.0F; });
	return m_HotScorchMarks;
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
	ResolveWeather();
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
	m_SceneCloudCover.Restore(m_LightingSettings.CloudCover);
	m_SceneMist.Restore(m_LightingSettings.FogMorningMist);
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
	if (!atmosphere.WeatherName.empty() && Weather::FindSlot(atmosphere.WeatherName) >= 0) {
		// A custom weather goes by name, since its slot depends on the mods loaded.
		m_LightingSettings.WeatherType = Weather::FindSlot(atmosphere.WeatherName);
	} else if (atmosphere.WeatherType >= 0) {
		m_LightingSettings.WeatherType = std::clamp(atmosphere.WeatherType, 0, Weather::GetSlotCount() - 1);
	}
	if (atmosphere.WeatherIntensity >= 0.0F) {
		m_LightingSettings.WeatherIntensity = std::clamp(atmosphere.WeatherIntensity, 0.0F, 1.0F);
	}
	if (atmosphere.Wind > -10000.0F) {
		m_LightingSettings.Wind = atmosphere.Wind;
	}
	if (atmosphere.CloudCover >= 0.0F) {
		m_SceneCloudCover.Apply(m_LightingSettings.CloudCover, std::clamp(atmosphere.CloudCover, 0.0F, 1.0F));
	}
	if (atmosphere.Mist >= 0.0F) {
		m_SceneMist.Apply(m_LightingSettings.FogMorningMist, std::clamp(atmosphere.Mist, 0.0F, 1.0F));
	}
	ApplyActivityAtmosphere();
}

void PostProcessMan::ApplyActivityAtmosphere() {
	if (m_ActivityTimeOfDay >= 0.0F) {
		m_LightingSettings.TimeOfDay = std::fmod(m_ActivityTimeOfDay, 24.0F);
		m_LightingSettings.DayLengthMinutes = 0.0F;
	}
	if (m_ActivityWeather >= 0) {
		m_LightingSettings.WeatherType = std::clamp(m_ActivityWeather, 0, Weather::GetSlotCount() - 1);
		if (m_LightingSettings.WeatherType > 0) {
			m_LightingSettings.WeatherIntensity = std::max(m_LightingSettings.WeatherIntensity, 0.6F);
		}
	}
}

void PostProcessMan::PulseGrade(int look, float strength, float attackMS, float releaseMS) {
	if (look < 0 || look >= LightingSettings::LookAllCount || strength <= 0.0F) {
		return;
	}
	double now = static_cast<double>(g_TimerMan.GetRealTickCount()) / static_cast<double>(g_TimerMan.GetTicksPerSecond());
	std::scoped_lock lock(m_EventLookMutex);
	// A handful at once is plenty; the oldest makes way.
	if (m_GradePulses.size() >= 8) {
		m_GradePulses.erase(m_GradePulses.begin());
	}
	m_GradePulses.push_back({look, std::min(strength, 2.0F), std::clamp(attackMS, 0.0F, 10000.0F) * 0.001F, std::clamp(releaseMS, 1.0F, 30000.0F) * 0.001F, now});
}

void PostProcessMan::BlendLook(int fromLook, int toLook, float t) {
	std::scoped_lock lock(m_EventLookMutex);
	m_LookBlendOn = true;
	m_LookBlendFrom = std::clamp(fromLook, 0, LightingSettings::LookAllCount - 1);
	m_LookBlendTo = std::clamp(toLook, 0, LightingSettings::LookAllCount - 1);
	m_LookBlendT = std::clamp(t, 0.0F, 1.0F);
}

void PostProcessMan::ClearLookBlend() {
	std::scoped_lock lock(m_EventLookMutex);
	m_LookBlendOn = false;
}

void PostProcessMan::ClearEventLooks() {
	std::scoped_lock lock(m_EventLookMutex);
	m_GradePulses.clear();
	m_LookBlendOn = false;
}

LightingSettings::GradeLook PostProcessMan::GetEventGrade(const LightingSettings::GradeLook& playerGrade, float strength, const std::vector<std::pair<int, float>>& extra) {
	using GradeLook = LightingSettings::GradeLook;
	auto mixGrade = [](const GradeLook& a, const GradeLook& b, float t) {
		return GradeLook{glm::mix(a.Saturation, b.Saturation, t), glm::mix(a.Contrast, b.Contrast, t), glm::mix(a.Temperature, b.Temperature, t), glm::mix(a.Tint, b.Tint, t), glm::mix(a.Vignette, b.Vignette, t), glm::mix(a.FilmGrain, b.FilmGrain, t), glm::mix(a.BloomIntensity, b.BloomIntensity, t), glm::mix(a.ShadowTint, b.ShadowTint, t), glm::mix(a.HighlightTint, b.HighlightTint, t)};
	};
	// Events push the grade by how far their look is from Natural, so they show over whatever grade the player chose.
	const GradeLook natural = LightingSettings::LookGrade(LightingSettings::LookNatural);
	auto push = [&natural](GradeLook& grade, int look, float weight) {
		GradeLook target = LightingSettings::LookGrade(look);
		grade.Saturation += (target.Saturation - natural.Saturation) * weight;
		grade.Contrast += (target.Contrast - natural.Contrast) * weight;
		grade.Temperature += (target.Temperature - natural.Temperature) * weight;
		grade.Tint += (target.Tint - natural.Tint) * weight;
		grade.Vignette += (target.Vignette - natural.Vignette) * weight;
		grade.FilmGrain += (target.FilmGrain - natural.FilmGrain) * weight;
		grade.BloomIntensity += (target.BloomIntensity - natural.BloomIntensity) * weight;
		grade.ShadowTint += (target.ShadowTint - natural.ShadowTint) * weight;
		grade.HighlightTint += (target.HighlightTint - natural.HighlightTint) * weight;
	};
	GradeLook grade = playerGrade;
	double now = static_cast<double>(g_TimerMan.GetRealTickCount()) / static_cast<double>(g_TimerMan.GetTicksPerSecond());
	{
		std::scoped_lock lock(m_EventLookMutex);
		if (m_LookBlendOn) {
			grade = mixGrade(LightingSettings::LookGrade(m_LookBlendFrom), LightingSettings::LookGrade(m_LookBlendTo), m_LookBlendT);
		}
		std::erase_if(m_GradePulses, [now](const GradePulse& pulse) { return now - pulse.StartSeconds > static_cast<double>(pulse.AttackSeconds + pulse.ReleaseSeconds); });
		for (const GradePulse& pulse: m_GradePulses) {
			float age = static_cast<float>(now - pulse.StartSeconds);
			float weight = age < pulse.AttackSeconds ? age / std::max(pulse.AttackSeconds, 0.001F) : 1.0F - (age - pulse.AttackSeconds) / pulse.ReleaseSeconds;
			weight = std::clamp(weight, 0.0F, 1.0F);
			push(grade, pulse.Look, weight * weight * pulse.Strength * strength);
		}
	}
	for (const auto& [look, weight]: extra) {
		if (weight > 0.0F) {
			push(grade, look, weight * strength);
		}
	}
	grade.Saturation = std::max(grade.Saturation, 0.0F);
	grade.Contrast = std::max(grade.Contrast, 0.1F);
	grade.Vignette = std::clamp(grade.Vignette, 0.0F, 1.0F);
	grade.FilmGrain = std::clamp(grade.FilmGrain, 0.0F, 1.0F);
	grade.BloomIntensity = std::max(grade.BloomIntensity, 0.0F);
	grade.ShadowTint = glm::max(grade.ShadowTint, glm::vec3(0.0F));
	grade.HighlightTint = glm::max(grade.HighlightTint, glm::vec3(0.0F));
	return grade;
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
	settings.CloudCover = m_SceneCloudCover.ToSave(settings.CloudCover);
	settings.FogMorningMist = m_SceneMist.ToSave(settings.FogMorningMist);
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
	// Animated palette flags: pulsing glows and cycling colours, for the next frame's sprites and terrain.
	if (!m_PaletteAnimationLoaded) {
		LoadPaletteAnimation();
	}
	g_RenderMan.UpdatePaletteAnimation(GetEffectTime(), m_LightingSettings.PaletteAnimation, m_LightingSettings.PaletteAnimationStrength);

	// Copy the current 8bpp backbuffer to the 32bpp buffer.
	m_PostProcessFramebuffer->Begin(true);
	g_RenderMan.BeginFrame(nullptr);
	Draw::DrawTexture(g_FrameMan.GetBackBuffer()->GetColorTexture().lock().get(), {-1.0f, -1.0f, 2.0f, 2.0f})->m_Indexed = false;
	m_PostProcessFramebuffer->End();
	// Glows are drawn once, as emitted light in the scene lighting (SceneLighting::LightPlayerScreen). The old second pass that screened them over the
	// finished frame, HUD and all, is gone.
	m_PostScreenEffects.clear();
}
