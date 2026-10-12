// Creature afflictions (CR-6): what a monster's attack leaves on whoever it hits. A giant spider's venom eats at the victim's health for a
// few seconds and slows its walk; its web holds the victim fast for a couple of seconds, legs and weapons both: it can still turn and look
// about, but not walk, fire, swing, throw or reload. The
// bite and the web glob put them on (Envenom, Ensnare, from their scripts); they run here. An ooze's acid (CR-9) eats at flesh and metal
// alike (Corrode).
// Status effects (SE-1) run here too: the buffs and curses spells leave on a unit (StatusEffect.h), and the passives a creature's preset
// gives it. Each Update they are expired, their regeneration and hold applied, and the shader and light they give the unit set.

#include "Actor.h"
#include "Color.h"
#include "Controller.h"
#include "EffectsParticles.h"
#include "PostProcessMan.h"
#include "PrimitiveMan.h"
#include "TimerMan.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>

using namespace RTE;

namespace {
	constexpr float c_SnaredPace = 0.05F; //!< The share of its pace a webbed unit's legs keep (not none: a path's time divides by it).
	constexpr unsigned int c_VenomRGB = 0x7CE040;
	constexpr unsigned int c_WebRGB = 0xE8E8F0;
	constexpr unsigned int c_AcidRGB = 0xB8F020;
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

void Actor::Corrode(float damagePerSecond, float seconds) {
	if (IsDead() || damagePerSecond <= 0.0F || seconds <= 0.0F) {
		return;
	}
	const long long now = g_TimerMan.GetSimTimeMS();
	if (!IsCorroding()) {
		m_AcidDamagePerSecond = 0.0F;
	}
	m_AcidUntil = std::max(m_AcidUntil, now + static_cast<long long>(seconds * 1000.0F));
	m_AcidDamagePerSecond = std::max(m_AcidDamagePerSecond, damagePerSecond);
}

bool Actor::IsCorroding() const {
	return m_AcidUntil > g_TimerMan.GetSimTimeMS();
}

bool Actor::IsEnvenomed() const {
	return m_VenomUntil > g_TimerMan.GetSimTimeMS();
}

bool Actor::IsEnsnared() const {
	return m_SnareUntil > g_TimerMan.GetSimTimeMS();
}

float Actor::GetAfflictionSpeedMultiplier() const {
	if (IsEnsnared() || m_EffectHeld) {
		return c_SnaredPace;
	}
	return (IsEnvenomed() ? m_VenomSlowTo : 1.0F) * m_EffectSpeed;
}

void Actor::AddStatusEffect(const StatusEffect& effect, float seconds) {
	if (IsDead() || effect.Name.empty()) {
		return;
	}
	const long long now = g_TimerMan.GetSimTimeMS();
	const long long until = seconds > 0.0F ? now + static_cast<long long>(seconds * 1000.0F) : 0;
	for (StatusEffect& running: m_StatusEffects) {
		if (running.Name == effect.Name) {
			const long long had = running.Until;
			running = effect;
			running.Until = (until == 0 || had == 0) ? 0 : std::max(had, until);
			return;
		}
	}
	m_StatusEffects.push_back(effect);
	m_StatusEffects.back().Until = until;
}

bool Actor::RemoveStatusEffect(const std::string& name) {
	auto found = std::find_if(m_StatusEffects.begin(), m_StatusEffects.end(), [&name](const StatusEffect& effect) { return effect.Name == name; });
	if (found == m_StatusEffects.end()) {
		return false;
	}
	m_StatusEffects.erase(found);
	return true;
}

int Actor::RemoveStatusEffects(bool harmful, bool helpful) {
	const size_t before = m_StatusEffects.size();
	std::erase_if(m_StatusEffects, [harmful, helpful](const StatusEffect& effect) { return effect.Harmful ? harmful : helpful; });
	return static_cast<int>(before - m_StatusEffects.size());
}

bool Actor::HasStatusEffect(const std::string& name) const {
	const long long now = g_TimerMan.GetSimTimeMS();
	return std::any_of(m_StatusEffects.begin(), m_StatusEffects.end(), [&](const StatusEffect& effect) { return effect.Name == name && effect.IsActiveAt(now); });
}

float Actor::GetStatusEffectSecondsLeft(const std::string& name) const {
	const long long now = g_TimerMan.GetSimTimeMS();
	for (const StatusEffect& effect: m_StatusEffects) {
		if (effect.Name == name) {
			if (effect.Until == 0) {
				return FLT_MAX;
			}
			return effect.Until > now ? static_cast<float>(effect.Until - now) / 1000.0F : 0.0F;
		}
	}
	return 0.0F;
}

void Actor::UpdateStatusEffects() {
	// The passives its preset gives it, back on it whenever they are missing (a Dispel takes them off for a moment only).
	for (const StatusEffect& passive: m_PassiveEffects) {
		if (!HasStatusEffect(passive.Name)) {
			AddStatusEffect(passive, passive.Seconds);
		}
	}
	const long long now = g_TimerMan.GetSimTimeMS();
	std::erase_if(m_StatusEffects, [now](const StatusEffect& effect) { return !effect.IsActiveAt(now); });

	// What they add up to.
	float speed = 1.0F, taken = 1.0F, dealt = 1.0F, healthPerSecond = 0.0F, energyPerSecond = 0.0F;
	bool fireProof = false, held = false, silenced = false;
	const StatusEffect* shaded = nullptr;
	for (const StatusEffect& effect: m_StatusEffects) {
		speed *= effect.SpeedMultiplier;
		taken *= effect.DamageTakenMultiplier;
		dealt *= effect.DamageDealtMultiplier;
		healthPerSecond += effect.HealthPerSecond;
		energyPerSecond += effect.EnergyPerSecond;
		fireProof = fireProof || effect.FireProof;
		held = held || effect.Held;
		silenced = silenced || effect.Silenced;
		if (!effect.Shader.empty() && (!shaded || effect.Priority >= shaded->Priority)) {
			shaded = &effect;
		}
	}
	m_EffectSpeed = std::clamp(speed, 0.05F, 10.0F);
	m_EffectDamageTaken = taken;
	m_EffectDamageDealt = dealt;
	m_EffectFireProof = fireProof;
	m_EffectHeld = held;
	m_EffectSilenced = silenced;

	const bool alive = !IsDead() && m_Status != DYING;
	const float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	const long long frame = g_TimerMan.GetSimUpdateCount();
	if (alive && healthPerSecond != 0.0F) {
		m_Health = std::min(m_Health + healthPerSecond * deltaTime, m_MaxHealth);
		// Mending closes a wound every couple of seconds as well, so the bleeding stops.
		if (healthPerSecond > 0.0F && frame % 120 == 0 && GetWoundCount() > 0) {
			RemoveWounds(1);
		}
	}
	if (alive && energyPerSecond != 0.0F && m_MaxEnergy > 0.0F) {
		SetEnergy(m_Energy + energyPerSecond * deltaTime);
	}
	if (held && alive) {
		// Held fast: nothing moves and nothing fires, however it is controlled (the controller has had its say this update).
		for (ControlState state: {MOVE_LEFT, MOVE_RIGHT, MOVE_UP, MOVE_DOWN, BODY_JUMP, BODY_JUMPSTART, BODY_LEAP, BODY_CROUCH, WEAPON_FIRE, WEAPON_RELOAD, AIM_SHARP}) {
			m_Controller.SetState(state, false);
		}
		m_Vel.m_X *= 0.5F;
		if (m_Vel.m_Y < 0.0F) {
			m_Vel.m_Y *= 0.5F;
		}
		m_AngularVel *= 0.5F;
	}
	for (const StatusEffect& effect: m_StatusEffects) {
		if (effect.LightRadius > 0.0F) {
			const Color& c = effect.LightColor;
			g_PostProcessMan.RegisterLight(m_Pos, glm::vec3(static_cast<float>(c.GetR()), static_cast<float>(c.GetG()), static_cast<float>(c.GetB())), effect.LightRadius, effect.LightIntensity, LightSource::Objects);
		}
	}

	// The shader they give it. Something else (a script, a chill) may change the unit's shader meanwhile: that becomes the one to put back.
	const std::string wanted = shaded ? shaded->Shader : std::string();
	if (m_ShaderName != m_EffectShader) {
		m_ShaderBeforeEffects = m_ShaderName;
		m_EffectShader.clear();
	}
	if (wanted != m_EffectShader) {
		if (wanted.empty()) {
			SetShaderName(m_ShaderBeforeEffects);
			m_ShaderBeforeEffects.clear();
		} else {
			SetShaderName(wanted);
		}
		m_EffectShader = wanted;
		if (m_ShaderName != wanted) {
			// (A shader that isn't defined: left as it was, and not asked for again this update.)
			m_EffectShader = m_ShaderName;
		}
	}
}

void Actor::UpdateAfflictions() {
	UpdateStatusEffects();
	if (m_VenomUntil == 0 && m_SnareUntil == 0 && m_AcidUntil == 0) {
		return;
	}
	const bool alive = !IsDead();
	const float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	const long long frame = g_TimerMan.GetSimUpdateCount();

	if (IsEnvenomed()) {
		if (alive) {
			m_Health -= m_VenomDamagePerSecond * deltaTime * GetDamageTakenMultiplier();
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

	if (IsCorroding()) {
		if (alive) {
			m_Health -= m_AcidDamagePerSecond * deltaTime;
		}
		// Yellow-green fizz: a sickly glow, and wisps of smoke off it.
		g_PostProcessMan.RegisterLight(m_Pos, glm::vec3(180.0F, 240.0F, 40.0F), m_SpriteRadius, 0.2F);
		if (frame % 8 == 0) {
			EffectsParticles::Emit("Mist", m_Pos + Vector(0.0F, -m_SpriteRadius * 0.2F), Vector(0.0F, -0.5F), 0.5F, 1, c_AcidRGB);
		}
	} else if (m_AcidUntil != 0) {
		m_AcidUntil = 0;
		m_AcidDamagePerSecond = 0.0F;
	}

	if (IsEnsnared()) {
		if (alive) {
			// Held fast: no walking, leaping, jumping or flying off, and no firing, swinging, throwing or reloading its weapons, however it is
			// controlled (the controller has had its say this update, and the legs and arms read it next, so a weapon being fired is let go).
			// Turning and aiming are left to it.
			for (ControlState state: {MOVE_LEFT, MOVE_RIGHT, MOVE_UP, BODY_JUMP, BODY_JUMPSTART, BODY_LEAP, WEAPON_FIRE, WEAPON_RELOAD, WEAPON_AUXILIARY_HOTKEY, WEAPON_PRIMARY_HOTKEY}) {
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
