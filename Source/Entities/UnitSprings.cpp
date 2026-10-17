#include "UnitSprings.h"
#include "ACrab.h"
#include "AHuman.h"
#include "Actor.h"
#include "Constants.h"
#include "FluidSim.h"
#include "HeldDevice.h"
#include "SceneMan.h"
#include "SettingsMan.h"
#include "TerrainTrees.h"
#include "TimerMan.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace RTE;

namespace {
	constexpr float c_TwoPi = 6.2831853F;
	constexpr float c_StepAngle = 0.35F; //!< How far round a swing one sub-step may go, in radians: small enough for a stiff spring to stay steady.
} // namespace

void UnitSprings::Spring::Step(float deltaTime, float frequency, float dampingRatio, const Vector& acceleration, float limit) {
	if (deltaTime <= 0.0F) {
		return;
	}
	float omega = c_TwoPi * std::max(frequency, 0.1F);
	int steps = std::clamp(static_cast<int>(std::ceil(deltaTime * omega / c_StepAngle)), 1, 16);
	float step = deltaTime / static_cast<float>(steps);
	float stiffness = omega * omega;
	float damping = 2.0F * dampingRatio * omega;
	for (int i = 0; i < steps; ++i) {
		// The push lands all in the first sub-step (a jolt is a change of speed), the spring and its damping in every one.
		Vector push = i == 0 ? acceleration * deltaTime : Vector();
		Speed += push + (Value * -stiffness - Speed * damping) * step;
		Value += Speed * step;
	}
	// No further than the limit: what goes past it stops there, as a knee bending all the way would.
	if (limit > 0.0F) {
		if (std::abs(Value.m_X) > limit) {
			Value.m_X = std::copysign(limit, Value.m_X);
			Speed.m_X = Speed.m_X * Value.m_X > 0.0F ? 0.0F : Speed.m_X;
		}
		if (std::abs(Value.m_Y) > limit) {
			Value.m_Y = std::copysign(limit, Value.m_Y);
			Speed.m_Y = Speed.m_Y * Value.m_Y > 0.0F ? 0.0F : Speed.m_Y;
		}
	}
}

bool UnitSprings::Enabled() {
	return g_SettingsMan.UnitSprings();
}

float UnitSprings::Strength() {
	return g_SettingsMan.UnitSpringStrength();
}

bool UnitSprings::SprungLegs() {
	return g_SettingsMan.UnitSprings() && g_SettingsMan.SprungLegs();
}

bool UnitSprings::FleshChunks() {
	return g_SettingsMan.FleshChunks();
}

bool UnitSprings::HoldsFoot(float x, float y) {
	int material = g_SceneMan.GetTerrMatter(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
	return material != g_MaterialAir && !FluidSim::IsLiquid(material) && !TerrainTrees::ActorsPass(material);
}

UnitSprings::GroundFeel UnitSprings::FeelGround(float left, float right, float top, float reach) {
	GroundFeel feel;
	// (Seven columns at most: every planted foot of every unit looks each update.)
	int columns = std::clamp(static_cast<int>(right - left) + 1, 3, 7);
	float spacing = (right - left) / static_cast<float>(columns - 1);
	std::array<float, 24> heights{};
	std::array<bool, 24> found{};
	float highest = top + reach;
	for (int column = 0; column < columns; ++column) {
		float x = left + spacing * static_cast<float>(column);
		if (HoldsFoot(x, top)) {
			// Inside the ground at the top already: a wall or a step higher than the foot, not what it stands on.
			continue;
		}
		for (float step = 1.0F; step <= reach; step += 1.0F) {
			if (HoldsFoot(x, top + step)) {
				// To a fraction of a pixel, so a foot sliding over rough ground moves up and down smoothly.
				float above = step - 1.0F;
				float below = step;
				for (int halving = 0; halving < 3; ++halving) {
					float middle = (above + below) * 0.5F;
					if (HoldsFoot(x, top + middle)) {
						below = middle;
					} else {
						above = middle;
					}
				}
				heights[column] = top + below;
				found[column] = true;
				highest = std::min(highest, top + below);
				break;
			}
		}
	}
	// The slope: a line through the columns that found ground near the highest (a deep hole under one edge doesn't tip the foot into it).
	float sumX = 0.0F;
	float sumY = 0.0F;
	float sumXX = 0.0F;
	float sumXY = 0.0F;
	int count = 0;
	for (int column = 0; column < columns; ++column) {
		if (!found[column] || heights[column] > highest + 3.0F) {
			continue;
		}
		float x = spacing * static_cast<float>(column);
		sumX += x;
		sumY += heights[column];
		sumXX += x * x;
		sumXY += x * heights[column];
		++count;
	}
	if (count == 0) {
		return feel;
	}
	feel.Found = true;
	feel.Y = highest;
	float spread = static_cast<float>(count) * sumXX - sumX * sumX;
	if (count >= 2 && spread > 0.01F) {
		feel.Slope = std::clamp((static_cast<float>(count) * sumXY - sumX * sumY) / spread, -1.5F, 1.5F);
	}
	return feel;
}

void UnitSprings::PassOnSupport(MOSRotating& body, BodySprings& springs, bool active) {
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	if (!active || deltaTime <= 0.0F) {
		// (Anything held back is pushed on straight away, so leaving the legs for a jump or a ladder loses none of it.)
		float owed = springs.HeldPush + springs.SupportOwed;
		if (owed < 0.0F) {
			body.AddImpulseForce(Vector(0.0F, owed));
		}
		springs.HeldPush = 0.0F;
		springs.SupportOwed = 0.0F;
		springs.SupportForce = 0.0F;
		springs.SupportForceSpeed = 0.0F;
		return;
	}
	// The push the feet gave this update, as a force, followed by the force passed on: a spring between them, critically damped so it never
	// overshoots (an overshoot would lift the body and drop it again, bobbing it while it stands), so a footfall's bump reaches the body as a
	// smooth rise and a landing is caught over a few updates rather than one. How stiff is a setting: stiffer feels firmer, softer sinks more.
	float wanted = springs.HeldPush / deltaTime;
	springs.SupportOwed += springs.HeldPush;
	springs.HeldPush = 0.0F;
	float omega = c_TwoPi * g_SettingsMan.SprungLegsStiffness();
	int steps = std::clamp(static_cast<int>(std::ceil(deltaTime * omega / c_StepAngle)), 1, 16);
	float step = deltaTime / static_cast<float>(steps);
	float passed = 0.0F;
	for (int i = 0; i < steps; ++i) {
		springs.SupportForceSpeed += (omega * omega * (wanted - springs.SupportForce) - 2.0F * omega * springs.SupportForceSpeed) * step;
		springs.SupportForce += springs.SupportForceSpeed * step;
		passed += springs.SupportForce * step;
	}
	// Only ever up, and never more than the feet gave: the legs hold the body, they never pull it down or throw it up. What is still owed is
	// kept, and all of it pushed on when the unit leaves its legs.
	passed = std::clamp(passed, std::min(springs.SupportOwed, 0.0F), 0.0F);
	if (passed < 0.0F) {
		body.AddImpulseForce(Vector(0.0F, passed));
		springs.SupportOwed -= passed;
	}
}

void UnitSprings::Jolt(BodySprings& springs, const Vector& velocityChange) {
	// The body lags behind the jolt: it is thrown the other way against the legs.
	springs.Body.Speed -= velocityChange * c_PPM;
	springs.Head.Speed -= velocityChange * (c_PPM * 0.5F);
}

void UnitSprings::UpdateActor(Actor& actor, BodySprings& springs, bool onGround, bool footfall) {
	float deltaTime = g_TimerMan.GetDeltaTimeSecs();
	AHuman* human = dynamic_cast<AHuman*>(&actor);
	ACrab* crab = human ? nullptr : dynamic_cast<ACrab*>(&actor);
	Attachable* head = human ? human->GetHead() : nullptr;
	HeldDevice* gun = human ? human->GetEquippedItem() : nullptr;
	// A body dying or dead is soft all over: it squashes where it lands and wobbles back (as a legless ooze does), its parts too
	// (Attachable::UpdateSeveredSquash), rather than its upper body riding on its legs.
	bool corpse = actor.GetStatus() == Actor::DYING || actor.GetStatus() == Actor::DEAD;
	bool legless = (crab && crab->IsBornLegless()) || corpse;

	if (!Enabled() || deltaTime <= 0.0F) {
		springs.Body.Reset();
		springs.Head.Reset();
		springs.Gun.Reset();
		springs.Started = false;
		actor.SetDrawNudge(Vector());
		actor.SetRenderSquash(Vector(), Vector(0.0F, 1.0F), 1.0F, 1.0F);
		if (head) {
			head->SetDrawNudge(Vector());
		}
		if (gun) {
			gun->SetDrawNudge(Vector());
		}
		return;
	}

	float softness = actor.GetSoftness() * Strength() * (corpse ? g_SettingsMan.SpringCorpseSoftness() : 1.0F);
	springs.Softness = softness;
	springs.Height = std::clamp(actor.GetRadius() * 1.6F, 12.0F, 240.0F);
	float mass = std::max(actor.GetMass(), 1.0F);
	// Heavier swings slower: an ogre or a mech sways where a soldier bobs.
	float heaviness = std::clamp(std::pow(mass / 80.0F, 0.18F), 0.6F, 2.2F);
	float frequency = 4.5F * g_SettingsMan.SpringSpeed() / heaviness;
	// Standing still it settles at once; moving or in the air it swings as freely as the bounce setting lets it (softer kinds a little more).
	bool still = onGround && actor.GetVel().MagnitudeIsLessThan(0.5F);
	float bounce = std::clamp(g_SettingsMan.SpringBounce() * (softness > 1.3F ? 1.25F : 1.0F), 0.0F, 1.0F);
	float damping = 1.0F - 0.8F * bounce;
	if (still) {
		damping = std::max(damping, 0.9F);
	}

	// How hard the unit was pushed this update, gravity left out: the body on its spring takes the opposite (it lags behind a push, dips when
	// the unit lands, stretches as it falls, and stays steady in the world while the body under it jolts on rough ground).
	Vector vel = actor.GetVel();
	Vector push;
	if (springs.Started) {
		// (Gravity is in it while falling, and the body stretches up a little against the legs; standing, the ground takes it out again.)
		push = (vel - springs.LastVel) * (c_PPM / deltaTime);
		// A blast or a teleport is not a push to ride out: capped at about a hard landing.
		if (push.MagnitudeIsGreaterThan(9000.0F)) {
			push.SetMagnitude(9000.0F);
		}
		// On its feet, the small knocks the walk's feet give the body every update (standing still too: they push it up a little, gravity
		// pulls it back) are not pushes to ride, or it bounces about on the spot: only what is more than about a metre a second in an update
		// gets through (a landing, a hit, a hard stop). Footfalls are given their own small dip below.
		if (onGround) {
			float floor = g_SettingsMan.SpringKnockFloor() * c_PPM / deltaTime;
			float size = push.GetMagnitude();
			push = size > floor ? push * ((size - floor) / size) : Vector();
		}
	}
	springs.LastVel = vel;
	springs.Started = true;

	if (footfall && onGround) {
		// A foot coming down: the body settles onto it, more the faster it goes and the softer it is.
		springs.Body.Speed.m_Y += std::min(6.0F + 6.0F * std::abs(vel.m_X), 30.0F) * std::min(softness, 2.5F) * g_SettingsMan.SpringStepDip();
	}

	float limit = springs.Height * g_SettingsMan.SpringReach() * softness;
	float lag = g_SettingsMan.SpringLag();
	float lagDamping = still ? 0.9F : std::min(damping + 0.05F, 1.0F);
	springs.Body.Step(deltaTime, frequency, damping, push * -std::min(softness, 2.5F), limit);
	springs.Head.Step(deltaTime, frequency * 1.6F, lagDamping, push * (-0.4F * softness * lag), std::max(limit * 0.4F * lag, 0.5F));
	springs.Gun.Step(deltaTime, frequency * 1.25F, lagDamping, push * (-0.5F * softness * lag), std::max(limit * 0.6F * lag, 0.5F));

	Vector body = springs.Body.Value;
	// Soft creatures squash and stretch with it too: down is squashed, up stretched. A legless body (an ooze) is all squash, its underside on the
	// ground.
	float squashShare = legless ? 1.0F : std::clamp(softness - 1.0F, 0.0F, 1.0F) * 0.6F;
	if (squashShare > 0.0F) {
		float spriteHeight = static_cast<float>(std::max(actor.GetSpriteHeight(), 4));
		float amount = std::clamp(body.m_Y / spriteHeight * squashShare * 1.5F, -0.25F, 0.3F);
		actor.SetRenderSquash(Vector(0.0F, spriteHeight * 0.5F), Vector(0.0F, 1.0F), 1.0F - amount, 1.0F + amount * 0.6F);
	} else {
		actor.SetRenderSquash(Vector(), Vector(0.0F, 1.0F), 1.0F, 1.0F);
	}
	if (legless) {
		body.m_Y = 0.0F;
	}
	if (corpse) {
		body.Reset();
	}
	actor.SetDrawNudge(body);
	if (head) {
		head->SetDrawNudge(springs.Head.Value);
	}
	if (gun) {
		gun->SetDrawNudge(springs.Gun.Value);
	}
}
