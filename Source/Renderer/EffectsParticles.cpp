#include "EffectsParticles.h"
#include "Camera.h"
#include "Constants.h"
#include "PostProcessMan.h"
#include "SceneMan.h"
#include "Shapes.h"
#include "TimerMan.h"
#include "Vector.h"
#include "Material.h"
#include "Color.h"
#include "Draw.h"
#include "Texture.h"
#include "glad/gl.h"

#include <memory>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace RTE;

namespace {
	enum class Kind : unsigned char {
		Spark,
		Debris,
		Dust,
		Ember,
		Fire, //!< A ball of an explosion's fire: glows, swells and rises, and is gone in about a second.
		Smoke, //!< What the fire leaves: dark, rising, lingering for seconds. Lit like dust.
		Mist //!< Spray off falling water: a soft pale puff that drifts down a little, swells and is gone in under a second. Never quite dark, so it shows at night.
	};

	struct Particle {
		glm::vec2 Position; //!< Scene pixels.
		glm::vec2 Velocity; //!< Pixels per second.
		float Age;
		float Life;
		float Size;
		glm::u8vec3 Color;
		Kind Type;
	};

	struct SpawnRequest {
		bool Ember = false;
		glm::vec2 Position;
		glm::vec2 Velocity; //!< Pixels per second, for impacts.
		float Energy; //!< Explosion energy, or 0 for an impact.
		unsigned int MaterialColor; //!< 0xRRGGBB.
		float Hardness;
		int EmitKind = -1; //!< A Kind to emit directly (mods), or -1.
		int EmitCount = 0;
		float EmitSpread = 0.0F;
	};

	constexpr size_t c_MaxParticles = 8000;
	constexpr int c_ImpactsPerFrame = 40; //!< Impacts beyond this per frame are skipped; explosions alone make thousands.

	std::vector<Particle> s_Particles;
	std::vector<SpawnRequest> s_Queue;
	std::mutex s_QueueMutex;
	std::atomic<int> s_ImpactBudget{c_ImpactsPerFrame};
	long long s_LastSimUpdate = -1;
	struct SmokeEntry {
		glm::vec2 Position;
		float Radius;
		float Density;
	};
	std::vector<SmokeEntry> s_Smoke;
	std::unordered_set<const void*> s_SmokeSeen;
	int s_SmokeFrame = 0; //!< Frames drawn, for spacing out the soft smoke each smoke sprite trails.
	std::mutex s_SmokeMutex;
	std::vector<EffectsParticles::Stain> s_Stains;
	std::mutex s_StainMutex;
	constexpr size_t c_MaxStainsPerFrame = 400;
	unsigned int s_Random = 0x9E3779B9u; //!< Render-only random state, never the simulation's.

	float Random01() {
		s_Random ^= s_Random << 13;
		s_Random ^= s_Random >> 17;
		s_Random ^= s_Random << 5;
		return static_cast<float>(s_Random & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	float RandomRange(float low, float high) { return low + (high - low) * Random01(); }

	glm::vec2 RandomDirection() {
		float angle = Random01() * c_TwoPI;
		return glm::vec2(std::cos(angle), std::sin(angle));
	}

	glm::u8vec3 UnpackRGB(unsigned int rgb) {
		return glm::u8vec3((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	}

	bool IsSolid(float x, float y) {
		int pixelX = static_cast<int>(std::floor(x));
		int pixelY = static_cast<int>(std::floor(y));
		if (g_SceneMan.SceneWrapsX()) {
			int width = g_SceneMan.GetSceneWidth();
			pixelX = ((pixelX % width) + width) % width;
		}
		if (pixelX < 0 || pixelY < 0 || pixelX >= g_SceneMan.GetSceneWidth() || pixelY >= g_SceneMan.GetSceneHeight()) {
			return false;
		}
		return g_SceneMan.GetTerrMatter(pixelX, pixelY) != g_MaterialAir;
	}

	void Add(const Particle& particle) {
		if (s_Particles.size() < c_MaxParticles) {
			s_Particles.push_back(particle);
		}
	}

	void SpawnFromRequest(const SpawnRequest& request, float amount) {
		if (request.EmitKind >= 0) {
			Kind kind = static_cast<Kind>(request.EmitKind);
			float speed = glm::length(request.Velocity);
			int count = std::max(static_cast<int>(std::round(static_cast<float>(request.EmitCount) * amount)), request.EmitCount > 0 ? 1 : 0);
			for (int i = 0; i < count; ++i) {
				// Each particle's velocity strays from the given one by up to the spread, in direction and speed.
				glm::vec2 velocity = request.Velocity + RandomDirection() * std::max(speed, 30.0F) * request.EmitSpread * Random01();
				glm::u8vec3 color = UnpackRGB(request.MaterialColor);
				switch (kind) {
					case Kind::Spark:
						Add({request.Position, velocity, 0.0F, RandomRange(0.2F, 0.7F), 1.0F, request.MaterialColor ? color : glm::u8vec3(255, 225, 150), Kind::Spark});
						break;
					case Kind::Dust:
						Add({request.Position, velocity, 0.0F, RandomRange(1.0F, 2.5F), RandomRange(2.5F, 5.0F), request.MaterialColor ? color : glm::u8vec3(110, 100, 92), Kind::Dust});
						break;
					case Kind::Ember:
						Add({request.Position, velocity, 0.0F, RandomRange(0.8F, 2.0F), 1.0F, request.MaterialColor ? color : glm::u8vec3(255, 160, 60), Kind::Ember});
						break;
					case Kind::Smoke: {
						// Soft dark puffs that roll upwards, swell and hang about for a few seconds.
						unsigned char grey = static_cast<unsigned char>(RandomRange(52.0F, 88.0F));
						Add({request.Position + RandomDirection() * RandomRange(0.0F, 3.0F), velocity, 0.0F, RandomRange(2.2F, 4.5F), RandomRange(3.0F, 6.0F), request.MaterialColor ? color : glm::u8vec3(grey, grey, grey), Kind::Smoke});
						break;
					}
					case Kind::Mist: {
						const LightingSettings& mist = g_PostProcessMan.GetLightingSettings();
						Add({request.Position + RandomDirection() * RandomRange(0.0F, 2.0F) * std::min(mist.WaterMistSize, 1.0F), velocity, 0.0F, RandomRange(0.4F, 0.9F) * std::max(mist.WaterMistLife, 0.05F),
						     RandomRange(1.5F, 3.2F) * std::max(mist.WaterMistSize, 0.05F), request.MaterialColor ? color : glm::u8vec3(190, 228, 255), Kind::Mist});
						break;
					}
					default:
						Add({request.Position, velocity, 0.0F, RandomRange(1.0F, 2.5F), 1.0F, request.MaterialColor ? color : glm::u8vec3(120, 110, 100), Kind::Debris});
						break;
				}
			}
			return;
		}
		if (request.Ember) {
			Add({request.Position, glm::vec2(RandomRange(-8.0F, 8.0F), RandomRange(-40.0F, -20.0F)), 0.0F, RandomRange(0.8F, 2.0F), 1.0F, glm::u8vec3(255, 160, 60), Kind::Ember});
			return;
		}
		if (request.Energy > 0.0F) {
			// Explosion: a burst of sparks, a ring of dust, and chips of whatever it went off against.
			float scale = std::clamp(request.Energy / 6000.0F, 0.3F, 3.0F) * amount;
			int sparkCount = static_cast<int>(40.0F * scale);
			for (int i = 0; i < sparkCount; ++i) {
				glm::vec2 direction = RandomDirection();
				direction.y -= 0.35F;
				Add({request.Position, glm::normalize(direction) * RandomRange(120.0F, 520.0F) * std::sqrt(scale), 0.0F, RandomRange(0.25F, 0.9F), 1.0F, glm::u8vec3(255, 220, 140), Kind::Spark});
			}
			int dustCount = static_cast<int>(10.0F * scale);
			glm::u8vec3 dustColor(110, 100, 92);
			int groundMaterial = g_SceneMan.GetTerrMatter(static_cast<int>(request.Position.x), static_cast<int>(request.Position.y + 6.0F));
			if (groundMaterial != g_MaterialAir) {
				glm::u8vec3 materialRGB = UnpackRGB(EffectsParticles::ColorToRGB(g_SceneMan.GetMaterialFromID(static_cast<unsigned char>(groundMaterial))->GetColor()));
				dustColor = glm::u8vec3(glm::mix(glm::vec3(materialRGB), glm::vec3(dustColor), 0.5F));
				int chipCount = static_cast<int>(24.0F * scale);
				for (int i = 0; i < chipCount; ++i) {
					glm::vec2 direction = RandomDirection();
					direction.y = -std::abs(direction.y) - 0.3F;
					Add({request.Position, glm::normalize(direction) * RandomRange(60.0F, 300.0F), 0.0F, RandomRange(1.5F, 3.5F), 1.0F, materialRGB, Kind::Debris});
				}
			}
			for (int i = 0; i < dustCount; ++i) {
				glm::vec2 offset = RandomDirection() * RandomRange(2.0F, 12.0F) * std::sqrt(scale);
				Add({request.Position + offset, glm::normalize(offset + glm::vec2(0.0F, -2.0F)) * RandomRange(15.0F, 60.0F), 0.0F, RandomRange(1.5F, 3.5F), RandomRange(3.0F, 6.0F), dustColor, Kind::Dust});
			}
			// The fireball: balls of fire that swell and roll upwards, each leaving smoke that appears as it burns out and hangs in the air for a few seconds.
			float reach = std::sqrt(scale);
			int fireCount = static_cast<int>(7.0F * scale) + 2;
			for (int i = 0; i < fireCount; ++i) {
				glm::vec2 offset = RandomDirection() * RandomRange(0.0F, 9.0F) * reach;
				glm::vec2 velocity = glm::normalize(offset + glm::vec2(0.0F, -3.0F)) * RandomRange(20.0F, 70.0F) * reach;
				float fireLife = RandomRange(0.45F, 1.0F);
				float size = RandomRange(5.0F, 9.0F) * reach;
				Add({request.Position + offset, velocity, 0.0F, fireLife, size, glm::u8vec3(255, 190, 90), Kind::Fire});
				// A negative age keeps the smoke unseen until its fire is nearly out.
				unsigned char grey = static_cast<unsigned char>(RandomRange(38.0F, 62.0F));
				Add({request.Position + offset, velocity * 0.6F, -fireLife * 0.6F, RandomRange(3.0F, 6.5F), size * 1.2F, glm::u8vec3(grey, grey, grey), Kind::Smoke});
			}
			if (groundMaterial != g_MaterialAir) {
				// A ring of dust racing out along the ground either side.
				int ringCount = static_cast<int>(9.0F * scale) + 2;
				for (int i = 0; i < ringCount; ++i) {
					float side = (i % 2 == 0) ? 1.0F : -1.0F;
					glm::vec2 velocity(side * RandomRange(90.0F, 260.0F) * reach, RandomRange(-25.0F, -5.0F));
					Add({request.Position + glm::vec2(side * RandomRange(2.0F, 8.0F), RandomRange(-2.0F, 3.0F)), velocity, 0.0F, RandomRange(1.2F, 2.6F), RandomRange(3.0F, 5.5F), dustColor, Kind::Dust});
				}
			}
			return;
		}
		// Impact: sparks off hard materials, a puff of dust and a few chips off soft ones, thrown back the way the hit came from.
		float speed = glm::length(request.Velocity);
		if (speed < 1.0F) {
			return;
		}
		glm::vec2 back = -request.Velocity / speed;
		glm::u8vec3 materialRGB = UnpackRGB(request.MaterialColor);
		if (request.Hardness > 0.5F) {
			int sparkCount = static_cast<int>(RandomRange(2.0F, 5.0F) * amount);
			for (int i = 0; i < sparkCount; ++i) {
				glm::vec2 direction = glm::normalize(back + RandomDirection() * 0.9F);
				Add({request.Position, direction * RandomRange(80.0F, 260.0F), 0.0F, RandomRange(0.15F, 0.45F), 1.0F, glm::u8vec3(255, 230, 170), Kind::Spark});
			}
		} else {
			Add({request.Position + back * 2.0F, back * RandomRange(10.0F, 30.0F) + glm::vec2(0.0F, -8.0F), 0.0F, RandomRange(0.8F, 1.6F), RandomRange(2.0F, 3.5F), materialRGB, Kind::Dust});
		}
		int chipCount = static_cast<int>(RandomRange(1.0F, 4.0F) * amount);
		for (int i = 0; i < chipCount; ++i) {
			glm::vec2 direction = glm::normalize(back + RandomDirection() * 0.7F + glm::vec2(0.0F, -0.4F));
			Add({request.Position + back * 1.5F, direction * RandomRange(40.0F, 140.0F), 0.0F, RandomRange(0.8F, 2.0F), 1.0F, materialRGB, Kind::Debris});
		}
	}

	std::unique_ptr<Texture> s_PuffTexture; //!< Soft, lumpy round puff for dust, white with alpha.

	GLuint GetPuffTexture_() {
		if (!s_PuffTexture) {
			constexpr int size = 64;
			std::vector<unsigned char> pixels(size * size * 4);
			// Smooth noise from a fixed table of random values, so the puff is the same every run.
			auto lattice = [](int x, int y) {
				unsigned int n = static_cast<unsigned int>(x) * 374761393u + static_cast<unsigned int>(y) * 668265263u;
				n = (n ^ (n >> 13)) * 1274126177u;
				return static_cast<float>((n ^ (n >> 16)) & 0xFFFF) / 65535.0F;
			};
			auto noise = [&lattice](float x, float y) {
				int cellX = static_cast<int>(std::floor(x));
				int cellY = static_cast<int>(std::floor(y));
				float fx = x - static_cast<float>(cellX);
				float fy = y - static_cast<float>(cellY);
				fx = fx * fx * (3.0F - 2.0F * fx);
				fy = fy * fy * (3.0F - 2.0F * fy);
				float top = lattice(cellX, cellY) + (lattice(cellX + 1, cellY) - lattice(cellX, cellY)) * fx;
				float bottom = lattice(cellX, cellY + 1) + (lattice(cellX + 1, cellY + 1) - lattice(cellX, cellY + 1)) * fx;
				return top + (bottom - top) * fy;
			};
			for (int y = 0; y < size; ++y) {
				for (int x = 0; x < size; ++x) {
					float dx = (static_cast<float>(x) + 0.5F) / size * 2.0F - 1.0F;
					float dy = (static_cast<float>(y) + 0.5F) / size * 2.0F - 1.0F;
					float distance = std::sqrt(dx * dx + dy * dy);
					// A round billow with an uneven edge and a little unevenness inside: its reach varies gently with direction (by noise, so there is no
					// pattern to it; a regular wobble made a five-pointed star), and its thickness varies across it.
					float billow = 0.6F * noise(dx * 1.6F + 7.3F, dy * 1.6F + 2.1F) + 0.4F * noise(dx * 3.7F + 31.0F, dy * 3.7F + 17.0F);
					float reach = 0.74F + 0.24F * billow;
					float alpha = std::clamp(1.0F - distance / reach, 0.0F, 1.0F);
					alpha = alpha * alpha * (3.0F - 2.0F * alpha);
					alpha *= 0.82F + 0.18F * noise(dx * 5.5F + 3.0F, dy * 5.5F + 11.0F);
					unsigned char* pixel = &pixels[(y * size + x) * 4];
					pixel[0] = pixel[1] = pixel[2] = 255;
					pixel[3] = static_cast<unsigned char>(alpha * 255.0F);
				}
			}
			GLuint texture = 0;
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindTexture(GL_TEXTURE_2D, 0);
			s_PuffTexture = std::make_unique<Texture>(texture);
		}
		return s_PuffTexture->GetTextureId();
	}

	glm::vec3 SparkColor(const Particle& particle) {
		// White-yellow when fresh, cooling through orange to dull red.
		float heat = 1.0F - particle.Age / particle.Life;
		glm::vec3 hot(1.0F, 0.9F, 0.6F);
		glm::vec3 warm(1.0F, 0.45F, 0.1F);
		glm::vec3 cool(0.5F, 0.08F, 0.02F);
		glm::vec3 color = heat > 0.5F ? glm::mix(warm, hot, (heat - 0.5F) * 2.0F) : glm::mix(cool, warm, heat * 2.0F);
		return color * (0.4F + 1.2F * heat);
	}
} // namespace

void EffectsParticles::SpawnExplosion(const Vector& position, float energy) {
	if (energy < 500.0F) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	s_Queue.push_back({false, glm::vec2(position.m_X, position.m_Y), glm::vec2(0.0F), energy, 0, 0.0F});
}

unsigned int EffectsParticles::ColorToRGB(const Color& color) {
	if (color.GetR() != 0 || color.GetG() != 0 || color.GetB() != 0) {
		return (static_cast<unsigned int>(color.GetR()) << 16) | (static_cast<unsigned int>(color.GetG()) << 8) | static_cast<unsigned int>(color.GetB());
	}
	if (color.GetIndex() == 0) {
		return 0;
	}
	Color fromPalette(color.GetIndex());
	return (static_cast<unsigned int>(fromPalette.GetR()) << 16) | (static_cast<unsigned int>(fromPalette.GetG()) << 8) | static_cast<unsigned int>(fromPalette.GetB());
}

void EffectsParticles::SpawnEmber(const Vector& position) {
	std::scoped_lock lock(s_QueueMutex);
	SpawnRequest request{};
	request.Ember = true;
	request.Position = glm::vec2(position.m_X, position.m_Y);
	s_Queue.push_back(request);
}

bool EffectsParticles::Emit(const std::string& kind, const Vector& position, const Vector& velocity, float spread, int count, unsigned int colorRGB) {
	Kind which;
	if (kind == "Sparks" || kind == "Spark") {
		which = Kind::Spark;
	} else if (kind == "Dust") {
		which = Kind::Dust;
	} else if (kind == "Embers" || kind == "Ember") {
		which = Kind::Ember;
	} else if (kind == "Debris") {
		which = Kind::Debris;
	} else if (kind == "Mist") {
		which = Kind::Mist;
	} else if (kind == "Smoke") {
		which = Kind::Smoke;
	} else {
		return false;
	}
	if (count <= 0) {
		return true;
	}
	SpawnRequest request;
	request.EmitKind = static_cast<int>(which);
	request.EmitCount = std::min(count, 200);
	request.EmitSpread = std::clamp(spread, 0.0F, 1.0F);
	request.Position = glm::vec2(position.m_X, position.m_Y);
	request.Velocity = glm::vec2(velocity.m_X, velocity.m_Y) * c_PPM;
	request.Energy = 0.0F;
	request.MaterialColor = colorRGB & 0xFFFFFF;
	request.Hardness = 0.0F;
	std::scoped_lock lock(s_QueueMutex);
	if (s_Queue.size() < 2000) {
		s_Queue.push_back(request);
	}
	return true;
}

void EffectsParticles::SpawnImpact(const Vector& position, const Vector& velocity, unsigned int materialColor, float hardness) {
	// Only fast hits make visible chips and sparks.
	if (velocity.GetSqrMagnitude() < 15.0F * 15.0F || s_ImpactBudget.load(std::memory_order_relaxed) <= 0) {
		return;
	}
	if (s_ImpactBudget.fetch_sub(1, std::memory_order_relaxed) <= 0) {
		return;
	}
	std::scoped_lock lock(s_QueueMutex);
	s_Queue.push_back({false, glm::vec2(position.m_X, position.m_Y), glm::vec2(velocity.m_X, velocity.m_Y) * c_PPM, 0.0F, materialColor, hardness});
}

void EffectsParticles::Update(float amount) {
	long long simUpdate = g_TimerMan.GetSimUpdateCount();
	float seconds = s_LastSimUpdate >= 0 ? std::min(static_cast<float>(simUpdate - s_LastSimUpdate) * g_TimerMan.GetDeltaTimeSecs(), 0.1F) : 0.0F;
	s_LastSimUpdate = simUpdate;
	s_ImpactBudget.store(c_ImpactsPerFrame, std::memory_order_relaxed);

	std::vector<SpawnRequest> requests;
	{
		std::scoped_lock lock(s_QueueMutex);
		requests.swap(s_Queue);
	}
	if (amount <= 0.0F || !g_SceneMan.GetScene()) {
		s_Particles.clear();
		return;
	}
	for (const SpawnRequest& request: requests) {
		SpawnFromRequest(request, amount);
	}
	if (seconds <= 0.0F) {
		return;
	}

	float wind = g_PostProcessMan.GetLightingSettings().Wind;
	constexpr float gravity = 9.8F * c_PPM;
	for (Particle& particle: s_Particles) {
		particle.Age += seconds;
		if (particle.Type == Kind::Ember) {
			// Embers float up on the heat, wobble, and drift with the wind.
			particle.Velocity += (glm::vec2(wind * 0.5F + std::sin(particle.Age * 7.0F + particle.Life * 13.0F) * 15.0F, -30.0F) - particle.Velocity) * std::min(1.0F, seconds * 1.5F);
			particle.Position += particle.Velocity * seconds;
			continue;
		}
		if (particle.Type == Kind::Fire || particle.Type == Kind::Smoke) {
			// Fire and its smoke roll upwards on their own heat, swelling as they go; smoke slows, keeps rising and leans with the wind.
			if (particle.Age >= 0.0F) {
				bool fire = particle.Type == Kind::Fire;
				particle.Velocity += (glm::vec2(wind * (fire ? 0.2F : 0.6F), fire ? -38.0F : -16.0F) - particle.Velocity) * std::min(1.0F, seconds * (fire ? 2.0F : 1.2F));
				particle.Size += seconds * (fire ? 9.0F : 3.5F);
				particle.Position += particle.Velocity * seconds;
			} else {
				// Smoke waiting for its fire to burn out travels with it.
				particle.Velocity += (glm::vec2(wind * 0.2F, -38.0F) - particle.Velocity) * std::min(1.0F, seconds * 2.0F);
				particle.Position += particle.Velocity * seconds;
			}
			continue;
		}
		if (particle.Type == Kind::Mist) {
			// Spray hangs and sinks slowly, spreading as it thins, and leans with the wind.
			particle.Velocity += (glm::vec2(wind * 0.3F, 14.0F) - particle.Velocity) * std::min(1.0F, seconds * 3.0F);
			particle.Size += seconds * 4.0F * g_PostProcessMan.GetLightingSettings().WaterMistSize * g_PostProcessMan.GetLightingSettings().WaterMistSpread;
			particle.Position += particle.Velocity * seconds;
			continue;
		}
		if (particle.Type == Kind::Dust) {
			// Dust billows out, slows quickly, rises a little and drifts with the wind.
			particle.Velocity += (glm::vec2(wind * 0.4F, -6.0F) - particle.Velocity) * std::min(1.0F, seconds * 2.5F);
			particle.Size += seconds * 3.0F;
			particle.Position += particle.Velocity * seconds;
			continue;
		}
		particle.Velocity.y += gravity * seconds * (particle.Type == Kind::Spark ? 0.6F : 1.0F);
		particle.Velocity *= 1.0F - std::min(1.0F, seconds * (particle.Type == Kind::Spark ? 0.8F : 0.3F));
		glm::vec2 next = particle.Position + particle.Velocity * seconds;
		if (IsSolid(next.x, next.y)) {
			// Bounce off whichever axis hit, losing most of the speed. Chips come to rest on the ground.
			bool hitX = IsSolid(next.x, particle.Position.y);
			bool hitY = IsSolid(particle.Position.x, next.y);
			float restitution = particle.Type == Kind::Spark ? 0.45F : 0.25F;
			if (hitX || !hitY) {
				particle.Velocity.x *= -restitution;
			}
			if (hitY || !hitX) {
				particle.Velocity.y *= -restitution;
				particle.Velocity.x *= 0.6F;
			}
			if (glm::length(particle.Velocity) < 8.0F) {
				particle.Velocity = glm::vec2(0.0F);
			}
			next = particle.Position + particle.Velocity * seconds;
			if (IsSolid(next.x, next.y)) {
				next = particle.Position;
			}
		}
		particle.Position = next;
	}
	int sceneHeight = g_SceneMan.GetSceneHeight();
	s_Particles.erase(std::remove_if(s_Particles.begin(), s_Particles.end(), [sceneHeight](const Particle& particle) { return particle.Age >= particle.Life || particle.Position.y > static_cast<float>(sceneHeight) + 50.0F; }), s_Particles.end());
}

void EffectsParticles::Draw(const Camera& camera) {
	for (const Particle& particle: s_Particles) {
		if (particle.Type != Kind::Debris) {
			continue;
		}
		float remaining = 1.0F - particle.Age / particle.Life;
		{
			// Chips stay solid until the end of their life, then fade.
			int alpha = static_cast<int>(255.0F * std::clamp(remaining * 4.0F, 0.0F, 1.0F));
			RTE::Draw::Rectangle(FloatRect(std::floor(particle.Position.x), std::floor(particle.Position.y), 1.0F, 1.0F), Color(particle.Color.r, particle.Color.g, particle.Color.b, alpha));
		}
	}
}

void EffectsParticles::GetSparks(const glm::vec2& screenOrigin, int width, int height, std::vector<Spark>& sparks) {
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	bool wraps = g_SceneMan.SceneWrapsX();
	for (const Particle& particle: s_Particles) {
		if (particle.Type != Kind::Spark && particle.Type != Kind::Ember) {
			continue;
		}
		glm::vec2 position = particle.Position - screenOrigin;
		if (wraps) {
			if (position.x < (static_cast<float>(width) - sceneWidth) * 0.5F) {
				position.x += sceneWidth;
			} else if (position.x > (static_cast<float>(width) + sceneWidth) * 0.5F) {
				position.x -= sceneWidth;
			}
		}
		if (position.x < -8.0F || position.y < -8.0F || position.x > static_cast<float>(width) + 8.0F || position.y > static_cast<float>(height) + 8.0F) {
			continue;
		}
		float speed = glm::length(particle.Velocity);
		glm::vec2 direction = speed > 0.01F ? particle.Velocity / speed : glm::vec2(1.0F, 0.0F);
		// Motion blurred streak: longer when fast.
		if (particle.Type == Kind::Ember) {
			float life = 1.0F - particle.Age / particle.Life;
			sparks.push_back({position, glm::vec2(1.0F, 0.0F), 1.0F, glm::vec3(1.0F, 0.45F, 0.12F) * (0.3F + 0.7F * life)});
			continue;
		}
		sparks.push_back({position, direction, std::clamp(speed / 60.0F, 1.0F, 6.0F), SparkColor(particle)});
	}
}

void EffectsParticles::GetPuffs(const glm::vec2& screenOrigin, int width, int height, std::vector<Puff>& puffs) {
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	bool wraps = g_SceneMan.SceneWrapsX();
	for (const Particle& particle: s_Particles) {
		if ((particle.Type != Kind::Dust && particle.Type != Kind::Smoke && particle.Type != Kind::Mist) || particle.Age < 0.0F) {
			continue;
		}
		glm::vec2 position = particle.Position - screenOrigin;
		if (wraps) {
			if (position.x < (static_cast<float>(width) - sceneWidth) * 0.5F) {
				position.x += sceneWidth;
			} else if (position.x > (static_cast<float>(width) + sceneWidth) * 0.5F) {
				position.x -= sceneWidth;
			}
		}
		float size = particle.Size * 2.0F;
		if (position.x < -size || position.y < -size || position.x > static_cast<float>(width) + size || position.y > static_cast<float>(height) + size) {
			continue;
		}
		float remaining = 1.0F - particle.Age / particle.Life;
		// Smoke comes in slowly as its fire dies; dust is there at once.
		float fadeIn = std::clamp(particle.Age * (particle.Type == Kind::Smoke ? 2.5F : 6.0F), 0.0F, 1.0F);
		if (particle.Type == Kind::Mist) {
			// A colour above 1 tells the particle shader this one keeps a little light of its own (see LitParticle.frag).
			puffs.push_back({position, size, glm::vec4(glm::vec3(particle.Color) / 255.0F + 1.0F, std::clamp(g_PostProcessMan.GetLightingSettings().WaterMistOpacity, 0.0F, 1.0F) * remaining * std::clamp(particle.Age * 12.0F, 0.0F, 1.0F))});
			continue;
		}
		puffs.push_back({position, size, glm::vec4(glm::vec3(particle.Color) / 255.0F, (particle.Type == Kind::Smoke ? 0.55F : 0.4F) * remaining * fadeIn)});
	}
}

void EffectsParticles::GetFire(const glm::vec2& screenOrigin, int width, int height, std::vector<Puff>& fire) {
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	bool wraps = g_SceneMan.SceneWrapsX();
	for (const Particle& particle: s_Particles) {
		if (particle.Type != Kind::Fire) {
			continue;
		}
		glm::vec2 position = particle.Position - screenOrigin;
		if (wraps) {
			if (position.x < (static_cast<float>(width) - sceneWidth) * 0.5F) {
				position.x += sceneWidth;
			} else if (position.x > (static_cast<float>(width) + sceneWidth) * 0.5F) {
				position.x -= sceneWidth;
			}
		}
		float size = particle.Size * 2.0F;
		if (position.x < -size || position.y < -size || position.x > static_cast<float>(width) + size || position.y > static_cast<float>(height) + size) {
			continue;
		}
		// White hot at first, through yellow and orange to a dull red as it burns out.
		float burnt = std::clamp(particle.Age / particle.Life, 0.0F, 1.0F);
		glm::vec3 color = burnt < 0.35F ? glm::mix(glm::vec3(1.0F, 0.95F, 0.75F), glm::vec3(1.0F, 0.7F, 0.2F), burnt / 0.35F) : glm::mix(glm::vec3(1.0F, 0.7F, 0.2F), glm::vec3(0.6F, 0.12F, 0.02F), (burnt - 0.35F) / 0.65F);
		fire.push_back({position, size, glm::vec4(color * std::pow(1.0F - burnt, 0.8F), 1.0F)});
	}
}

unsigned int EffectsParticles::GetPuffTexture() {
	return GetPuffTexture_();
}

void EffectsParticles::RegisterSmoke(const void* object, const glm::vec2& position, float radius, float density) {
	bool puff = false;
	{
		std::scoped_lock lock(s_SmokeMutex);
		if (s_SmokeSeen.insert(object).second) {
			s_Smoke.push_back({position, radius, density});
			// Every puff of the game's smoke (a little drawn sprite) trails soft smoke of this kind as well, so smoke billows and hangs instead of being a cluster of sprites.
			// One soft puff from each sprite every so many frames, each sprite on its own beat.
			float amount = g_PostProcessMan.GetLightingSettings().SoftSmoke;
			if (amount > 0.0F) {
				int period = std::max(static_cast<int>(14.0F / amount), 1);
				puff = static_cast<int>((reinterpret_cast<uintptr_t>(object) >> 5) + static_cast<uintptr_t>(s_SmokeFrame)) % period == 0;
			}
		}
	}
	if (puff) {
		SpawnRequest request;
		request.EmitKind = static_cast<int>(Kind::Smoke);
		request.EmitCount = 1;
		request.EmitSpread = 0.6F;
		request.Position = position;
		request.Velocity = glm::vec2(0.0F, -10.0F);
		request.Energy = 0.0F;
		request.MaterialColor = 0;
		request.Hardness = 0.0F;
		std::scoped_lock lock(s_QueueMutex);
		if (s_Queue.size() < 2000) {
			s_Queue.push_back(request);
		}
	}
}

void EffectsParticles::BeginFrame() {
	std::scoped_lock lock(s_SmokeMutex);
	++s_SmokeFrame;
	s_Smoke.clear();
	s_SmokeSeen.clear();
}

void EffectsParticles::GetSmoke(const glm::vec2& screenOrigin, int width, int height, std::vector<Puff>& smoke) {
	float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
	bool wraps = g_SceneMan.SceneWrapsX();
	std::scoped_lock lock(s_SmokeMutex);
	for (const SmokeEntry& entry: s_Smoke) {
		glm::vec2 position = entry.Position - screenOrigin;
		if (wraps) {
			if (position.x < (static_cast<float>(width) - sceneWidth) * 0.5F) {
				position.x += sceneWidth;
			} else if (position.x > (static_cast<float>(width) + sceneWidth) * 0.5F) {
				position.x -= sceneWidth;
			}
		}
		float size = entry.Radius * 2.6F;
		if (position.x < -size || position.y < -size || position.x > static_cast<float>(width) + size || position.y > static_cast<float>(height) + size) {
			continue;
		}
		smoke.push_back({position, size, glm::vec4(1.0F, 1.0F, 1.0F, entry.Density)});
	}
}

bool EffectsParticles::IsStainingMaterial(const Material* material) {
	static std::unordered_map<const Material*, bool> cache;
	static std::mutex cacheMutex;
	std::scoped_lock lock(cacheMutex);
	auto found = cache.find(material);
	if (found != cache.end()) {
		return found->second;
	}
	// As the material's behaviour says (MaterialBehaviour::Stains, SB-1), else by its name.
	const std::string& name = material->GetPresetName();
	int stains = material->GetBehaviour().Stains;
	bool staining = stains >= 0 ? stains == 1 : (name.find("Blood") != std::string::npos || name.find("Oil") != std::string::npos);
	cache.emplace(material, staining);
	return staining;
}

glm::vec2 EffectsParticles::StainGloss(const Material* material) {
	// Oil is glossy and stays so; blood (any colour) shines a little wet and dries matte.
	return material && material->GetPresetName().find("Oil") != std::string::npos ? glm::vec2(0.8F, 0.75F) : glm::vec2(0.25F, 0.05F);
}

void EffectsParticles::SpawnStain(const Vector& position, int red, int green, int blue, float speed, const glm::vec2& gloss) {
	std::scoped_lock lock(s_StainMutex);
	if (s_Stains.size() >= c_MaxStainsPerFrame) {
		return;
	}
	// Faster drops splash wider.
	float radius = std::clamp(1.0F + speed * 0.12F, 1.0F, 3.5F);
	s_Stains.push_back({glm::vec2(position.m_X, position.m_Y), glm::vec3(red, green, blue) / 255.0F, radius, gloss});
}

std::vector<EffectsParticles::Stain> EffectsParticles::TakeStains() {
	std::scoped_lock lock(s_StainMutex);
	std::vector<Stain> stains;
	stains.swap(s_Stains);
	return stains;
}

void EffectsParticles::Clear() {
	s_Particles.clear();
	std::scoped_lock lock(s_QueueMutex);
	s_Queue.clear();
}

int EffectsParticles::GetCount() {
	return static_cast<int>(s_Particles.size());
}
