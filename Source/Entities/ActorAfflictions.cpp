// Creature afflictions (CR-6): what a monster's attack leaves on whoever it hits. A giant spider's venom eats at the victim's health for a
// few seconds and slows its walk; its web holds the victim's legs fast for a couple of seconds, though it can still turn and shoot. The
// bite and the web glob put them on (Envenom, Ensnare, from their scripts); they run here.

#include "Actor.h"
#include "Color.h"
#include "Controller.h"
#include "EffectsParticles.h"
#include "PostProcessMan.h"
#include "PrimitiveMan.h"
#include "TimerMan.h"

#include <algorithm>
#include <cmath>

using namespace RTE;

namespace {
	constexpr float c_SnaredPace = 0.05F; //!< The share of its pace a webbed unit's legs keep (not none: a path's time divides by it).
	constexpr unsigned int c_VenomRGB = 0x7CE040;
	constexpr unsigned int c_WebRGB = 0xE8E8F0;
} // namespace

void Actor::Envenom(float damagePerSecond, float seconds, float slowTo) {
	if (!m_Organic || IsDead() || damagePerSecond <= 0.0F || seconds <= 0.0F) {
		return;
	}
	const long long now = g_TimerMan.GetSimTimeMS();
	if (!IsEnvenomed()) {
		m_VenomDamagePerSecond = 0.0F;
		m_VenomSlowTo = 1.0F;
	}
	m_VenomUntil = std::max(m_VenomUntil, now + static_cast<long long>(seconds * 1000.0F));
	m_VenomDamagePerSecond = std::max(m_VenomDamagePerSecond, damagePerSecond);
	m_VenomSlowTo = std::clamp(std::min(m_VenomSlowTo, slowTo), 0.1F, 1.0F);
}

void Actor::Ensnare(float seconds) {
	if (IsDead() || seconds <= 0.0F) {
		return;
	}
	m_SnareUntil = std::max(m_SnareUntil, g_TimerMan.GetSimTimeMS() + static_cast<long long>(seconds * 1000.0F));
}

bool Actor::IsEnvenomed() const {
	return m_VenomUntil > g_TimerMan.GetSimTimeMS();
}

bool Actor::IsEnsnared() const {
	return m_SnareUntil > g_TimerMan.GetSimTimeMS();
}

float Actor::GetAfflictionSpeedMultiplier() const {
	if (IsEnsnared()) {
		return c_SnaredPace;
	}
	return IsEnvenomed() ? m_VenomSlowTo : 1.0F;
}

void Actor::UpdateAfflictions() {
	if (m_VenomUntil == 0 && m_SnareUntil == 0) {
		return;
	}
	const bool alive = !IsDead();
	const float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	const long long frame = g_TimerMan.GetSimUpdateCount();

	if (IsEnvenomed()) {
		if (alive) {
			m_Health -= m_VenomDamagePerSecond * deltaTime;
		}
		// Sickly green: a faint glow, and now and then a drip of it.
		g_PostProcessMan.RegisterLight(m_Pos, glm::vec3(120.0F, 220.0F, 70.0F), m_SpriteRadius * 1.2F, 0.25F);
		if (frame % 10 == 0) {
			EffectsParticles::Emit("Droplets", m_Pos + Vector(0.0F, -m_SpriteRadius * 0.3F), Vector(0.0F, 1.0F), 0.6F, 1, c_VenomRGB);
		}
	} else if (m_VenomUntil != 0) {
		m_VenomUntil = 0;
		m_VenomDamagePerSecond = 0.0F;
		m_VenomSlowTo = 1.0F;
	}

	if (IsEnsnared()) {
		if (alive) {
			// The legs held: no walking, leaping, jumping or flying off, however it is controlled (the controller has had its say this update,
			// and the legs read it next). Turning, aiming and shooting are left to it.
			for (ControlState state: {MOVE_LEFT, MOVE_RIGHT, MOVE_UP, BODY_JUMP, BODY_JUMPSTART, BODY_LEAP}) {
				m_Controller.SetState(state, false);
			}
			m_Vel.m_X *= 0.6F;
			if (m_Vel.m_Y < 0.0F) {
				m_Vel.m_Y *= 0.6F;
			}
		}
		// The web: pale strands across the legs, pinned to the ground either side.
		const float reach = std::max(m_SpriteRadius * 0.7F, 8.0F);
		const Vector low = m_Pos + Vector(0.0F, reach * 0.6F);
		static const unsigned char s_WebColor = static_cast<unsigned char>(Color(225, 225, 235).GetIndex());
		for (int strand = 0; strand < 4; ++strand) {
			const float across = (static_cast<float>(strand) - 1.5F) * reach * 0.5F;
			g_PrimitiveMan.DrawLinePrimitive(low + Vector(-reach, across * 0.4F + reach * 0.3F), low + Vector(reach, -across * 0.4F - reach * 0.2F), s_WebColor);
			g_PrimitiveMan.DrawLinePrimitive(low + Vector(across, -reach * 0.8F), low + Vector(across * 1.4F, reach * 0.5F), s_WebColor);
		}
		if (frame % 20 == 0) {
			EffectsParticles::Emit("Mist", low, Vector(0.0F, -0.3F), 0.5F, 1, c_WebRGB);
		}
	} else if (m_SnareUntil != 0) {
		m_SnareUntil = 0;
	}
}
