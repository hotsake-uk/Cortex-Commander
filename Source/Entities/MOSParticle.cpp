#include "MOSParticle.h"
#include "EffectsParticles.h"

#include "Atom.h"
#include "Actor.h"
#include "PostProcessMan.h"
#include "Draw.h"
#include "Texture.h"
#include "allegro.h"
#include "FrameMan.h"
#include <array>
#include <mutex>
#include <unordered_map>

using namespace RTE;

ConcreteClassInfo(MOSParticle, MovableObject, 1000);

namespace {
	/// The average colour of a sprite's drawn pixels, as 0xRRGGBB: what colour its smoke is. Worked out once per sprite frame.
	unsigned int SpriteColor(const BitmapTexture* sprite) {
		// Kept with the bitmap it was worked out from, so a sprite freed and another made at the same address (a mod reload) is worked out again.
		static std::unordered_map<const BitmapTexture*, std::pair<const BITMAP*, unsigned int>> cache;
		static std::mutex cacheMutex;
		if (!sprite) {
			return 0xF2E6D9;
		}
		std::scoped_lock lock(cacheMutex);
		BITMAP* bitmap = sprite->GetBitmap();
		auto found = cache.find(sprite);
		if (found != cache.end() && found->second.first == bitmap) {
			return found->second.second;
		}
		unsigned int color = 0xF2E6D9;
		if (bitmap && bitmap_color_depth(bitmap) == 8) {
			unsigned long red = 0;
			unsigned long green = 0;
			unsigned long blue = 0;
			unsigned long count = 0;
			for (int y = 0; y < bitmap->h; ++y) {
				for (int x = 0; x < bitmap->w; ++x) {
					int index = _getpixel(bitmap, x, y);
					if (index != 0) {
						unsigned int rgb = EffectsParticles::ColorToRGB(Color(index));
						red += (rgb >> 16) & 0xFF;
						green += (rgb >> 8) & 0xFF;
						blue += rgb & 0xFF;
						++count;
					}
				}
			}
			if (count > 0) {
				color = static_cast<unsigned int>(((red / count) << 16) | ((green / count) << 8) | (blue / count));
			}
		}
		cache[sprite] = {bitmap, color};
		return color;
	}
} // namespace

MOSParticle::MOSParticle() {
	Clear();
}

MOSParticle::~MOSParticle() {
	Destroy(true);
}

void MOSParticle::Clear() {
	m_Atom = nullptr;
	m_SpriteAnimMode = OVERLIFETIME;
	m_PostEffectEnabled = true; // Default to true for backwards compatibility reasons
	m_FlameSprite = false;
}

int MOSParticle::Create() {
	if (MOSprite::Create() < 0) {
		return -1;
	}
	if (!m_Atom) {
		m_Atom = new Atom();
	}
	// The game's flame particles (Flame 1, Flame 2 and every copy of them, like the sandbox fire brush's) all draw this one animation.
	m_FlameSprite = m_SpriteFile.GetDataPath().find("Effects/Pyro/Flame/Flame.png") != std::string::npos;
	return 0;
}

int MOSParticle::Create(const MOSParticle& reference) {
	MOSprite::Create(reference);

	m_Atom = new Atom(*(reference.m_Atom));
	m_Atom->SetOwner(this);
	m_FlameSprite = reference.m_FlameSprite;

	return 0;
}

int MOSParticle::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return MOSprite::ReadProperty(propName, reader));

	MatchProperty("Atom", {
		if (!m_Atom) {
			m_Atom = new Atom;
		}
		reader >> *m_Atom;
		m_Atom->SetOwner(this);
	});

	EndPropertyList;
}

int MOSParticle::Save(Writer& writer) const {
	MOSprite::Save(writer);

	// TODO: Make proper save system that knows not to save redundant data!
	/*
	writer.NewProperty("Atom");
	writer << m_Atom;
	*/

	return 0;
}

void MOSParticle::Destroy(bool notInherited) {
	delete m_Atom;

	if (!notInherited) {
		MOSprite::Destroy();
	}
	Clear();
}

int MOSParticle::GetDrawPriority() const { return m_Atom->GetMaterial()->GetPriority(); }

const Material* MOSParticle::GetMaterial() const { return m_Atom->GetMaterial(); }

void MOSParticle::SetAtom(Atom* newAtom) {
	delete m_Atom;
	m_Atom = newAtom;
	m_Atom->SetOwner(this);
}

void MOSParticle::RestDetection() {
	MOSprite::RestDetection();

	// If we seem to be about to settle, make sure we're not still flying in the air
	if ((m_ToSettle || IsAtRest()) && g_SceneMan.OverAltitude(m_Pos, (m_aSprite[m_Frame]->h / 2) + 3, 2)) {
		m_VelOscillations = 0;
		m_RestTimer.Reset();
		m_ToSettle = false;
	}
}

void MOSParticle::Travel() {
	MOSprite::Travel();

	if (m_PinStrength) {
		return;
	}

	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	float velMag = m_Vel.GetMagnitude();

	// Set the atom to ignore a certain MO, if set and applicable.
	if (m_HitsMOs && m_pMOToNotHit && g_MovableMan.ValidMO(m_pMOToNotHit) && !m_MOIgnoreTimer.IsPastSimTimeLimit()) {
		std::vector<MOID> MOIDsNotToHit;
		m_pMOToNotHit->GetMOIDs(MOIDsNotToHit);
		for (const MOID& MOIDNotToHit: MOIDsNotToHit) {
			m_Atom->AddMOIDToIgnore(MOIDNotToHit);
		}
	}
	// Do static particle bounce calculations.
	int hitCount = 0;
	if (!IsTooFast()) {
		m_Atom->Travel(g_TimerMan.GetDeltaTimeSecs(), true);
	}

	m_Atom->ClearMOIDIgnoreList();

	if (m_SpriteAnimMode == ONCOLLIDE) {
		// Change angular velocity after collision.
		if (hitCount >= 1) {
			m_AngularVel *= 0.5F * velMag * RandomNormalNum();
			m_AngularVel = -m_AngularVel;
		}

		// TODO: Rework this so it's less incomprehensible black magic math and not driven by AngularVel.
		double newFrame = m_Rotation.GetRadAngle();

		newFrame -= std::floor(m_Rotation.GetRadAngle() / (2.0F * c_PI)) * (2.0F * c_PI);
		newFrame /= (2.0F * c_PI);
		newFrame *= m_FrameCount;
		m_Frame = std::floor(newFrame);
		m_Rotation += m_AngularVel * deltaTime;

		if (m_Frame >= m_FrameCount) {
			m_Frame = m_FrameCount - 1;
		}
	}
}

void MOSParticle::Update() {
	MOSprite::Update();
	// A shot cracking past a unit pins it down a little (see Actor::ShotPassing).
	Actor::ShotPassing(*this);
}

void MOSParticle::Draw(BITMAP* targetBitmap, const Vector& targetPos, DrawMode mode, bool onlyPhysical) const {
	RTEAssert(!m_aSprite.empty(), "No sprite bitmaps loaded to draw " + GetPresetName());
	RTEAssert(m_Frame >= 0 && m_Frame < m_FrameCount, "Frame is out of bounds for " + GetPresetName());

	if (mode == g_DrawMOID && m_MOID == g_NoMOID) {
		return;
	}

	Vector spritePos(m_Pos + m_SpriteOffset - targetPos);

	// TODO I think this is an array with 4 elements to account for Y wrapping. Y wrapping is not really handled in this game, so this can probably be knocked down to 2 elements. Also, I'm sure this code can be simplified.
	std::array<Vector, 4> drawPositions = {spritePos};
	int drawPasses = 1;

	bool needsWrap = mode != g_DrawMOID;
	if (needsWrap && g_SceneMan.SceneWrapsX()) {
		if (targetPos.IsZero() && m_WrapDoubleDraw) {
			if (spritePos.GetFloorIntX() < m_aSprite[m_Frame]->w) {
				drawPositions.at(drawPasses) = spritePos;
				drawPositions.at(drawPasses).m_X += static_cast<float>(targetBitmap->w);
				drawPasses++;
			} else if (spritePos.GetFloorIntX() > targetBitmap->w - m_aSprite[m_Frame]->w) {
				drawPositions.at(drawPasses) = spritePos;
				drawPositions.at(drawPasses).m_X -= static_cast<float>(targetBitmap->w);
				drawPasses++;
			}
		} else if (m_WrapDoubleDraw) {
			if (targetPos.m_X < 0) {
				drawPositions.at(drawPasses) = drawPositions[0];
				drawPositions.at(drawPasses).m_X -= static_cast<float>(g_SceneMan.GetSceneWidth());
				drawPasses++;
			}
			if (targetPos.GetFloorIntX() + targetBitmap->w > g_SceneMan.GetSceneWidth()) {
				drawPositions.at(drawPasses) = drawPositions[0];
				drawPositions.at(drawPasses).m_X += static_cast<float>(g_SceneMan.GetSceneWidth());
				drawPasses++;
			}
		}
	}

	for (int i = 0; i < drawPasses; ++i) {
		int spriteX = drawPositions.at(i).GetFloorIntX();
		int spriteY = drawPositions.at(i).GetFloorIntY();
		switch (mode) {
			case g_DrawMaterial:
				draw_character_ex(targetBitmap, m_aSprite[m_Frame], spriteX, spriteY, m_SettleMaterialDisabled ? GetMaterial()->GetIndex() : GetMaterial()->GetSettleMaterial(), -1);
				break;
			case g_DrawWhite:
				draw_character_ex(targetBitmap, m_aSprite[m_Frame], spriteX, spriteY, g_WhiteColor, -1);
				break;
			case g_DrawTrans:
				DrawTexture(m_aSprite[m_Frame], spriteX, spriteY, {255, 255, 255, g_FrameMan.GetCurrentAlpha()});
				break;
			case g_DrawAlpha:
				DrawTexture(m_aSprite[m_Frame], spriteX, spriteY, {255, 255, 255, 255});
				break;
			case g_DrawMOID:
				break;
			default:
				draw_sprite(targetBitmap, m_aSprite[m_Frame], spriteX, spriteY);
				break;
		}

		g_SceneMan.RegisterDrawing(targetBitmap, m_MOID, spriteX, spriteY, spriteX + m_aSprite[m_Frame]->w, spriteY + m_aSprite[m_Frame]->h);
	}
}

void MOSParticle::Draw(const Camera& camera) const {
	RTEAssert(!m_Sprites.empty(), "No sprite bitmaps loaded to draw " + GetPresetName());
	RTEAssert(m_Frame >= 0 && m_Frame < m_FrameCount, "Frame is out of bounds for " + GetPresetName());
	// Nowhere near the screen: nothing to hand to the GPU. The margin covers the trail of a fast one that has just left the view.
	if (!camera.IsVisible(m_Pos, m_SpriteRadius + 48.0F)) {
		return;
	}
	if (m_Atom && !m_Atom->GetDrawnTrail().empty()) {
		Draw::PixelsBatched(m_Atom->GetDrawnTrail(), Color(m_Atom->GetTrailColor().GetIndex()));
	}
	if (!camera.IsVisible(m_Pos, m_SpriteRadius)) {
		return;
	}
	// A flame gets the fire shader's flames, the same as burning ground, drawn over this sprite in the glow pass, and with the shader only, no sprite.
	int fireStyle = m_FlameSprite ? g_PostProcessMan.GetLightingSettings().FireStyle : LightingSettings::FirePixel;
	if (fireStyle != LightingSettings::FirePixel) {
		float age = static_cast<float>(GetAge());
		float size = std::clamp(age / 120.0F, 0.3F, 1.0F);
		float heat = m_Lifetime > 0 ? std::clamp(1.0F - age / static_cast<float>(m_Lifetime), 0.35F, 1.0F) : 1.0F;
		Vector foot = GetRenderPos();
		EffectsParticles::RegisterFlame(this, glm::vec2(foot.m_X, foot.m_Y), size, heat);
		if (fireStyle == LightingSettings::FireShaderOnly) {
			return;
		}
	}
	Vector spritePos((GetRenderPos() + m_SpriteOffset).GetFloored());
	Color tint = ApplyRenderBlendMode();
	ApplySpriteMaps();
	Draw::DrawTexture(m_Sprites[m_Frame].get(), spritePos, tint);
	RestoreRenderBlendMode();
	// Smoke (weightless air particles that float up) scatters the light passing through it.
	if (m_GlobalAccScalar < 0.0F && m_Atom && m_Atom->GetMaterial() && m_Atom->GetMaterial()->GetIndex() == g_MaterialAir) {
		float density = m_Lifetime > 0 ? std::clamp(1.0F - static_cast<float>(GetAge()) / static_cast<float>(m_Lifetime), 0.0F, 1.0F) : 1.0F;
		Vector center = GetRenderPos();
		EffectsParticles::RegisterSmoke(this, glm::vec2(center.m_X, center.m_Y), m_SpriteRadius, density, SpriteColor(m_Sprites[m_Frame].get()));
	}
}
