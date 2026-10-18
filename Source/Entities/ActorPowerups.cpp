// Powerups: items a unit carries in its inventory and uses (from the unit menu's Quick actions, Lua, or by itself when run by the AI) for a
// few seconds of an effect: an energy shield that bullets bounce off, a stim, an overcharge of its guns, a cloak, an instant heal, or the
// casts of a spell from a scroll. What each item gives is its PowerupInfo (HeldDevice.h), from its INI; the effects run here.

#include "Actor.h"
#include "DetMath.h"
#include "ACrab.h"
#include "AEJetpack.h"
#include "AHuman.h"
#include "Arm.h"
#include "Controller.h"
#include "EffectsParticles.h"
#include "FrameMan.h"
#include "HDFirearm.h"
#include "HeldDevice.h"
#include "Magazine.h"
#include "MovableMan.h"
#include "PostProcessMan.h"
#include "PresetMan.h"
#include "PrimitiveMan.h"
#include "SceneMan.h"
#include "TimerMan.h"

#include "GUI.h"
#include "AllegroBitmap.h"

#include <algorithm>
#include <cmath>

using namespace RTE;

namespace {
	constexpr float c_CloakOpacity = 0.2F; //!< How opaque a cloaked unit is drawn.
	constexpr float c_ShieldMinRadius = 16.0F;
	constexpr float c_ShieldMaxRadius = 90.0F;
	constexpr float c_DeflectMinSpeed = 20.0F; //!< Slower than this, m/s, a particle isn't a shot and passes through the shield.
	constexpr long long c_AIThinkMS = 500; //!< How often a unit run by the AI looks at whether to use a powerup.
	constexpr float c_AIHealBelow = 0.4F; //!< A unit run by the AI uses a heal under this share of its health.
	constexpr float c_AIFightSuppression = 0.25F; //!< And its other powerups when pinned down at least this much (or firing).
	constexpr const char* c_EffectNames[] = {"Shield", "Stim", "Overcharge", "Cloak"};
	static_assert(std::size(c_EffectNames) == static_cast<size_t>(Actor::PowerupEffect::Count), "A name for each powerup effect.");

	/// The seconds of each timed effect a powerup gives, by PowerupEffect.
	std::array<float, static_cast<int>(Actor::PowerupEffect::Count)> EffectSeconds(const PowerupInfo& info) {
		return {info.Shield, info.Stim, info.Overcharge, info.Cloak};
	}

	/// The spell a scroll puts in the hand, or null if there's no such preset.
	const HDFirearm* ScrollSpell(const PowerupInfo& info) {
		return info.Spell.empty() ? nullptr : dynamic_cast<const HDFirearm*>(g_PresetMan.GetEntityPreset("HDFirearm", info.Spell));
	}

	/// Takes an item from the inventory into the unit's hand, putting what it held in its place, as the unit menu does: a two-handed item also
	/// puts the off hand's away.
	/// @return The unique ID of what it held before, 0 for nothing.
	long TakeInHand(AHuman* human, int index) {
		Arm* arm = human->GetFGArm();
		const std::deque<MovableObject*>* inventory = human->GetInventory();
		if (!arm || index < 0 || index >= static_cast<int>(inventory->size())) {
			return 0;
		}
		const HeldDevice* incoming = dynamic_cast<const HeldDevice*>(inventory->at(index));
		if (!incoming) {
			return 0;
		}
		HeldDevice* held = arm->GetHeldDevice();
		long heldID = held ? held->GetUniqueID() : 0;
		bool twoHanded = !incoming->IsDualWieldable() && !incoming->HasObjectInGroup("Shields");
		arm->SetHeldDevice(dynamic_cast<HeldDevice*>(human->SetInventoryItemAtIndex(held ? arm->RemoveAttachable(held) : nullptr, index)));
		arm->SetHandPos(human->GetPos() + human->GetHolsterOffset().GetXFlipped(human->IsHFlipped()));
		if (Arm* offArm = human->GetBGArm(); twoHanded && offArm && offArm->GetHeldDevice()) {
			human->AddInventoryItem(offArm->RemoveAttachable(offArm->GetHeldDevice()));
			offArm->SetHandPos(human->GetPos() + human->GetHolsterOffset().GetXFlipped(human->IsHFlipped()));
		}
		if (SoundContainer* sound = human->GetDeviceSwitchSound()) {
			sound->Play(human->GetPos());
		}
		return heldID;
	}

	/// The firearms a unit has in its hands, filled up: an overcharge starts with a full magazine.
	void FillHeldMagazines(Actor* actor) {
		if (const AHuman* human = dynamic_cast<const AHuman*>(actor)) {
			for (HeldDevice* device: {human->GetEquippedItem(), human->GetEquippedBGItem()}) {
				if (HDFirearm* firearm = dynamic_cast<HDFirearm*>(device); firearm && firearm->GetMagazine() && firearm->GetMagazine()->GetCapacity() > 0) {
					firearm->GetMagazine()->SetRoundCount(firearm->GetMagazine()->GetCapacity());
				}
			}
		}
	}
} // namespace

float Actor::GetPowerupSecondsLeft(PowerupEffect effect) const {
	const long long left = m_PowerupUntil[static_cast<int>(effect)] - g_TimerMan.GetSimTimeMS();
	return left > 0 ? static_cast<float>(left) / 1000.0F : 0.0F;
}

bool Actor::AddPowerupSeconds(const std::string& effectName, float seconds) {
	if (seconds <= 0.0F || m_Status == DEAD || m_Status == DYING) {
		return false;
	}
	for (int effect = 0; effect < static_cast<int>(PowerupEffect::Count); ++effect) {
		if (effectName == c_EffectNames[effect]) {
			const long long now = g_TimerMan.GetSimTimeMS();
			m_PowerupUntil[effect] = std::max(m_PowerupUntil[effect], now) + static_cast<long long>(seconds * 1000.0F);
			return true;
		}
	}
	return false;
}

bool Actor::CanUsePowerup(const HeldDevice* item) const {
	if (!item || !item->IsPowerup() || item->GetPowerupUsesLeft() <= 0 || m_Status == DEAD || m_Status == DYING) {
		return false;
	}
	const PowerupInfo& info = item->GetPowerup();
	if (!info.Spell.empty()) {
		const AHuman* human = dynamic_cast<const AHuman*>(this);
		return human && human->GetFGArm() && ScrollSpell(info);
	}
	// (Something it gives that isn't running yet, or a heal while hurt.)
	const auto seconds = EffectSeconds(info);
	for (int effect = 0; effect < static_cast<int>(PowerupEffect::Count); ++effect) {
		if (seconds[effect] > 0.0F && GetPowerupSecondsLeft(static_cast<PowerupEffect>(effect)) <= 0.0F) {
			return true;
		}
	}
	return info.Heal && (m_Health < m_MaxHealth || GetWoundCount() > 0);
}

bool Actor::UsePowerup(MovableObject* item) {
	auto found = std::find(m_Inventory.begin(), m_Inventory.end(), item);
	HeldDevice* device = found != m_Inventory.end() ? dynamic_cast<HeldDevice*>(*found) : nullptr;
	if (!device || !CanUsePowerup(device)) {
		return false;
	}
	const PowerupInfo info = device->GetPowerup();
	int index = static_cast<int>(found - m_Inventory.begin());
	const int usesLeft = device->GetPowerupUsesLeft() - 1;
	device->SetPowerupUsesLeft(usesLeft);
	bool usedUp = usesLeft <= 0;

	if (!info.Spell.empty()) {
		// A scroll: its spell in the hand, with its casts, free of energy. What was held comes back once they're cast (UpdatePowerups).
		AHuman* human = dynamic_cast<AHuman*>(this);
		HDFirearm* spell = dynamic_cast<HDFirearm*>(ScrollSpell(info)->Clone());
		spell->SetEnergyCost(0.0F);
		spell->SetScrollCasts(std::max(info.Casts, 1));
		if (usedUp) {
			MovableObject* scroll = SetInventoryItemAtIndex(spell, index);
			scroll->DestroyScriptState();
			delete scroll;
			usedUp = false;
		} else {
			AddToInventoryBack(spell);
			index = static_cast<int>(m_Inventory.size()) - 1;
		}
		spell->SetScrollReturnID(TakeInHand(human, index));
	} else {
		const long long now = g_TimerMan.GetSimTimeMS();
		const auto seconds = EffectSeconds(info);
		for (int effect = 0; effect < static_cast<int>(PowerupEffect::Count); ++effect) {
			if (seconds[effect] > 0.0F) {
				m_PowerupUntil[effect] = std::max(m_PowerupUntil[effect], now + static_cast<long long>(seconds[effect] * 1000.0F));
			}
		}
		if (info.Heal) {
			RemoveWounds(GetWoundCount());
			SetHealth(m_MaxHealth);
		}
		if (info.Overcharge > 0.0F) {
			FillHeldMagazines(this);
		}
	}
	if (usedUp) {
		MovableObject* used = RemoveInventoryItemAtIndex(index);
		used->DestroyScriptState();
		delete used;
	}
	EffectsParticles::Emit("Sparks", m_Pos, Vector(0.0F, -3.0F), 0.8F, 8, 0x80C0FF);
	g_PostProcessMan.RegisterLight(m_Pos, glm::vec3(120.0F, 190.0F, 255.0F), 40.0F, 1.2F, LightSource::Objects);
	return true;
}

void Actor::UpdatePowerups() {
	const long long now = g_TimerMan.GetSimTimeMS();

	if (IsShielded()) {
		// The bubble: anything fast of another team about to cross into it is turned back off it, harmlessly.
		const float radius = std::clamp(GetRadius(), c_ShieldMinRadius, c_ShieldMaxRadius);
		const float deltaPixels = c_PPM * g_TimerMan.GetDeltaTimeSecs();
		const bool wraps = g_SceneMan.SceneWrapsX();
		for (MovableObject* particle: g_MovableMan.GetParticleList()) {
			if (!particle->HitsMOs() || particle->IsSetToDelete() || particle->GetTeam() == m_Team || particle->GetVel().MagnitudeIsLessThan(c_DeflectMinSpeed)) {
				continue;
			}
			Vector offset = g_SceneMan.ShortestDistance(m_Pos, particle->GetPos(), wraps);
			Vector step = particle->GetVel() * deltaPixels;
			if (offset.MagnitudeIsGreaterThan(radius + step.GetMagnitude() * 1.5F + 2.0F) || offset.Dot(step) >= 0.0F) {
				continue;
			}
			// Where its next step and a half heads into the bubble (or where it is, if it's already in): it bounces off the bubble there.
			Vector hit = offset;
			const float outside = offset.Dot(offset) - radius * radius;
			if (outside > 0.0F) {
				const float a = step.Dot(step);
				const float b = 2.0F * offset.Dot(step);
				const float discriminant = b * b - 4.0F * a * outside;
				if (a <= 0.0F || discriminant < 0.0F) {
					continue;
				}
				const float along = (-b - std::sqrt(discriminant)) / (2.0F * a);
				if (along < 0.0F || along > 1.5F) {
					continue;
				}
				hit = offset + step * along;
			}
			Vector normal = hit.MagnitudeIsGreaterThan(0.5F) ? hit.GetNormalized() : Vector(0.0F, -1.0F);
			Vector vel = particle->GetVel();
			vel -= normal * (2.0F * vel.Dot(normal));
			particle->SetPos(m_Pos + normal * (radius + 2.0F));
			particle->SetVel(vel * 0.8F);
			particle->SetWhichMOToNotHit(this, 0.25F);
			EffectsParticles::Emit("Sparks", m_Pos + normal * radius, normal * 4.0F, 0.6F, 3, 0x80C0FF);
		}
		static const unsigned char s_ShieldColor = static_cast<unsigned char>(Color(120, 190, 255).GetIndex());
		static const unsigned char s_ShieldEdgeColor = static_cast<unsigned char>(Color(200, 230, 255).GetIndex());
		const float flicker = 0.85F + 0.15F * DetMath::Sin(static_cast<float>(now) * 0.02F);
		g_PrimitiveMan.DrawCirclePrimitive(m_Pos, static_cast<int>(radius), s_ShieldEdgeColor);
		g_PrimitiveMan.DrawCirclePrimitive(m_Pos, static_cast<int>(radius) - 1, s_ShieldColor);
		g_PostProcessMan.RegisterLight(m_Pos, glm::vec3(120.0F, 190.0F, 255.0F), radius * 1.4F, 0.6F * flicker, LightSource::Objects);
	}

	// The cloak: faint while it lasts (its parts too, and what it picks up), as it was after.
	if (IsCloaked()) {
		if (m_OpacityBeforeCloak < 0.0F) {
			m_OpacityBeforeCloak = GetRenderOpacity();
		}
		SetRenderOpacity(c_CloakOpacity);
	} else if (m_OpacityBeforeCloak >= 0.0F) {
		SetRenderOpacity(m_OpacityBeforeCloak);
		m_OpacityBeforeCloak = -1.0F;
	}

	if (IsStimmed()) {
		AEJetpack* jetpack = nullptr;
		if (const AHuman* human = dynamic_cast<const AHuman*>(this)) {
			jetpack = human->GetJetpack();
		} else if (const ACrab* crab = dynamic_cast<const ACrab*>(this)) {
			jetpack = crab->GetJetpack();
		}
		if (jetpack) {
			jetpack->SetJetTimeLeft(jetpack->GetJetTimeTotal());
		}
	}

	// A scroll's spell with its last cast done: let go (as a used-up Medikit goes), then what was held before back in hand.
	if (AHuman* human = dynamic_cast<AHuman*>(this)) {
		HeldDevice* held = human->GetEquippedItem();
		if (HDFirearm* spell = dynamic_cast<HDFirearm*>(held); spell && spell->IsScrollSpent() && !spell->IsSetToDelete()) {
			m_PowerupReturnID = spell->GetScrollReturnID();
			spell->SetToDelete(true);
		} else if (m_PowerupReturnID != 0 && !held) {
			for (int index = 0; index < static_cast<int>(m_Inventory.size()); ++index) {
				if (m_Inventory[index]->GetUniqueID() == m_PowerupReturnID) {
					TakeInHand(human, index);
					break;
				}
			}
			m_PowerupReturnID = 0;
		} else if (m_PowerupReturnID != 0 && held && !held->IsSetToDelete()) {
			// (Something else taken in hand meanwhile: that stays.)
			m_PowerupReturnID = 0;
		}
	}

	// Units run by the AI use what they carry: a heal when badly hurt, the rest in the thick of a fight.
	if (m_Controller.GetInputMode() != Controller::CIM_AI || m_Status == DEAD || m_Status == DYING || now < m_NextPowerupThought || m_Inventory.empty()) {
		return;
	}
	m_NextPowerupThought = now + c_AIThinkMS;
	const bool hurt = m_Health < m_MaxHealth * c_AIHealBelow;
	const bool fighting = m_Suppression >= c_AIFightSuppression || m_Controller.IsState(WEAPON_FIRE);
	for (MovableObject* item: m_Inventory) {
		const HeldDevice* device = dynamic_cast<const HeldDevice*>(item);
		if (!device || !CanUsePowerup(device)) {
			continue;
		}
		const PowerupInfo& info = device->GetPowerup();
		const auto seconds = EffectSeconds(info);
		const bool healOnly = info.Heal && info.Spell.empty() && std::all_of(seconds.begin(), seconds.end(), [](float s) { return s <= 0.0F; });
		if (healOnly ? hurt : fighting) {
			UsePowerup(item);
			break;
		}
	}
}

void Actor::DrawPowerupsHUD(BITMAP* targetBitmap, const Vector& drawPos) {
	std::string line;
	for (int effect = 0; effect < static_cast<int>(PowerupEffect::Count); ++effect) {
		if (float left = GetPowerupSecondsLeft(static_cast<PowerupEffect>(effect)); left > 0.0F) {
			line += (line.empty() ? "" : "  ") + std::string(c_EffectNames[effect]) + " " + std::to_string(static_cast<int>(std::ceil(left)));
		}
	}
	// The status effects on it (SE-1), with the seconds they have left; a passive one just by name.
	const long long now = g_TimerMan.GetSimTimeMS();
	for (const StatusEffect& effect: m_StatusEffects) {
		line += (line.empty() ? "" : "  ") + effect.GetLabel();
		if (effect.Until > 0) {
			line += " " + std::to_string(static_cast<int>(std::ceil(static_cast<float>(std::max(effect.Until - now, 0LL)) / 1000.0F)));
		}
	}
	GUIFont* font = g_FrameMan.GetSmallFont();
	if (line.empty() || !font) {
		return;
	}
	AllegroBitmap bitmap(targetBitmap);
	font->DrawAligned(&bitmap, drawPos.GetFloorIntX(), drawPos.GetFloorIntY() + m_HUDStack - 34, line, GUIFont::Centre);
}
