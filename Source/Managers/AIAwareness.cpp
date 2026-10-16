// What AI units can see and are aware of, shown (AIAwareness.h). The AI's scripts tell each unit's engine side what it is aware of as they
// scan (SharedBehaviors.ReportAwareness, Actor::ReportAwareness); its sight cone is worked out then too, in the sim, and the player's own
// concealment in the controlled unit's update (Actor::GetConcealment), as the lights it reads are only steady while a sim update runs. Here
// they are only read and drawn, with ImGui at window resolution over player 1's view, as the modern HUD is.

#include "AIAwareness.h"

#include "Activity.h"
#include "ActivityMan.h"
#include "Actor.h"
#include "CameraMan.h"
#include "DebugDraw.h"
#include "DebugMan.h"
#include "FrameMan.h"
#include "MovableMan.h"
#include "PostProcessMan.h"
#include "Sandbox.h"
#include "SceneMan.h"
#include "TimerMan.h"
#include "WeatherEffects.h"
#include "WindowMan.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using namespace RTE;

int AIAwareness::Marks = AIAwareness::EnemyUnits;
int AIAwareness::Cones = AIAwareness::EnemyUnits;
int AIAwareness::Meter = AIAwareness::MeterAlways;
bool AIAwareness::OnlyWithStealth = false;
float AIAwareness::ConeOpacity = 0.5F;
float AIAwareness::MarkScale = 1.0F;

namespace {
	constexpr long long c_FreshMS = 1000; //!< A report older than this is stale: the unit stopped scanning (dead, in a craft, turned off).
	constexpr long long c_HeardMS = 3000; //!< A unit alarmed by a noise this recently is searching.
	constexpr long long c_ConeEveryMS = 100; //!< How often a unit's cone is worked out afresh.
	constexpr int c_MaxConeRays = 64;

	/// An AI unit's sight cone, as last worked out.
	struct Cone {
		Vector Eye; //!< Where it was looked from.
		float Facing = 1.0F; //!< 1 facing right, -1 left.
		float HalfAngle = 0.0F; //!< Half the field, radians.
		float Reach = 0.0F; //!< How far it sees now (night and dust taken), pixels.
		int Rays = 0;
		std::array<float, c_MaxConeRays> Hit{}; //!< How far along each ray, across the field from top to bottom, before a wall: 0 to 1 of Reach.
		long long AtMS = -1;
	};

	std::mutex s_ConeMutex; //!< The scans report from the threaded script updates.
	std::unordered_map<long, Cone> s_Cones; //!< By unique ID.

	/// The controlled unit's concealment, for the meter.
	struct MeterReading {
		long ActorID = 0;
		ActorConcealment Hidden;
		float Noise = 0.0F;
		long long AtMS = -1;
	};
	std::mutex s_MeterMutex;
	MeterReading s_Meter;

	/// How a mark is being shown: faded in and out, so a state doesn't blink on and off between scans.
	struct MarkShown {
		int State = ActorAwareness::Unaware; //!< What is drawn (the last state worth a mark, while it fades out).
		float Alpha = 0.0F;
		double SeenAt = 0.0; //!< When last drawn, ImGui time, to forget units gone.
	};
	std::unordered_map<long, MarkShown> s_Marks;

	bool StealthOn() {
		const LightingSettings& lighting = g_PostProcessMan.GetLightingSettings();
		return lighting.Enabled && lighting.NightAffectsAI;
	}

	bool Allowed() { return !AIAwareness::OnlyWithStealth || StealthOn(); }

	/// The side player 1 looks from, or NoTeam where every side's are shown (the Sandbox game mode).
	int ViewerTeam() {
		Activity* activity = g_ActivityMan.GetActivity();
		if (!activity) {
			return Activity::NoTeam;
		}
		// (In the Sandbox game mode, the side of the unit you play, if you play one.)
		if (Sandbox::IsGodMode()) {
			const Actor* controlled = activity->GetControlledActor(Players::PlayerOne);
			return g_MovableMan.IsActor(controlled) ? controlled->GetTeam() : Activity::NoTeam;
		}
		return activity->GetTeamOfPlayer(Players::PlayerOne);
	}

	/// Whether a unit's mark or cone is shown under a Units setting.
	bool Shows(int units, const Actor* actor, int viewerTeam) {
		if (units == AIAwareness::NoUnits || !actor || actor->IsDead() || actor->GetTeam() < 0) {
			return false;
		}
		if (viewerTeam == Activity::NoTeam || actor->GetTeam() != viewerTeam) {
			// Other sides' only where your side can see, and not while a cloak hides them (as their HUD).
			if (viewerTeam != Activity::NoTeam && (actor->IsCloaked() || g_SceneMan.IsUnseen(actor->GetPos().GetFloorIntX(), actor->GetPos().GetFloorIntY(), viewerTeam))) {
				return false;
			}
			return true;
		}
		return units == AIAwareness::AllUnits;
	}

	/// What a unit is aware of now: its report while fresh, a noise heard as searching. -1 for a unit that isn't reporting.
	int StateOf(const Actor* actor, long long now) {
		const ActorAwareness& aware = actor->GetAwareness();
		if (aware.ReportedMS < 0 || now - aware.ReportedMS > c_FreshMS || actor->IsPlayerControlled()) {
			return -1;
		}
		if (aware.StateNow == ActorAwareness::Unaware && actor->GetAlarmAgeMS() < c_HeardMS) {
			return ActorAwareness::Searching;
		}
		return aware.StateNow;
	}

	ImU32 StateColor(int state, int alpha) {
		switch (state) {
			case ActorAwareness::Noticing:
				return IM_COL32(255, 214, 72, alpha);
			case ActorAwareness::Searching:
				return IM_COL32(255, 146, 46, alpha);
			case ActorAwareness::Engaged:
				return IM_COL32(240, 62, 52, alpha);
			default:
				return IM_COL32(196, 222, 255, alpha);
		}
	}

	/// The window-pixel size the marks and meter are drawn at.
	float Unit() {
		GameViewRect view = g_WindowMan.GetGameViewRect();
		return 16.0F * std::clamp(view.h / 720.0F, 0.75F, 2.5F) * std::clamp(AIAwareness::MarkScale, 0.5F, 2.0F);
	}

	/// Text centred on a point, with a dark outline so it reads over anything.
	void OutlinedText(ImDrawList* drawList, ImVec2 centre, float size, ImU32 color, const char* text, bool centreX = true) {
		ImFont* font = ImGui::GetFont();
		ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0F, text);
		ImVec2 at(std::floor(centreX ? centre.x - extent.x * 0.5F : centre.x), std::floor(centre.y - extent.y * 0.5F));
		const int alpha = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF);
		const ImU32 shadow = IM_COL32(0, 0, 0, alpha * 3 / 4);
		for (ImVec2 offset: {ImVec2(-1, 0), ImVec2(1, 0), ImVec2(0, -1), ImVec2(0, 1), ImVec2(1, 1)}) {
			drawList->AddText(font, size, ImVec2(at.x + offset.x, at.y + offset.y), shadow, text);
		}
		drawList->AddText(font, size, at, color, text);
	}

	ImU32 WithAlpha(ImU32 color, float alpha) {
		const int a = static_cast<int>(std::clamp(alpha, 0.0F, 1.0F) * static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF));
		return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
	}

	float EaseOutBack(float t) {
		t = std::clamp(t, 0.0F, 1.0F) - 1.0F;
		return 1.0F + t * t * (2.7F * t + 1.7F);
	}

#pragma region Cones
	void DrawCone(ImDrawList* drawList, const Actor* actor, const Cone& cone, int state, float level) {
		if (cone.Rays < 2 || cone.Reach <= 1.0F) {
			return;
		}
		const float scale = DebugDraw::ScenePixelsPerWindowPixel();
		// From where its eyes are now (the walls were measured a moment ago from about here).
		const ImVec2 eye = DebugDraw::ToScreen(actor->GetEyePos());
		const float reach = cone.Reach / scale;
		const float opacity = std::clamp(AIAwareness::ConeOpacity, 0.0F, 1.0F);
		// Noticing someone, the cone brightens as it does; fighting, it is strongest.
		float strength = 0.55F;
		if (state == ActorAwareness::Noticing) {
			strength = 0.65F + 0.35F * level;
		} else if (state == ActorAwareness::Searching) {
			strength = 0.75F;
		} else if (state == ActorAwareness::Engaged) {
			strength = 0.9F;
		}
		const float nearAlpha = 105.0F * opacity * strength;
		const ImU32 color = StateColor(state, 255);
		const ImVec2 white = drawList->_Data->TexUvWhitePixel;

		std::array<ImVec2, c_MaxConeRays> points;
		std::array<float, c_MaxConeRays> along;
		for (int i = 0; i < cone.Rays; ++i) {
			const float angle = -cone.HalfAngle + 2.0F * cone.HalfAngle * static_cast<float>(i) / static_cast<float>(cone.Rays - 1);
			const float length = reach * cone.Hit[i];
			points[i] = ImVec2(eye.x + std::cos(angle) * length * cone.Facing, eye.y + std::sin(angle) * length);
			along[i] = cone.Hit[i];
		}
		// The fan: brightest at the eyes, fading out with distance, so where a wall cuts a ray short it stays brighter there.
		const int triangles = cone.Rays - 1;
		drawList->PrimReserve(triangles * 3, triangles * 3);
		for (int i = 0; i < triangles; ++i) {
			const ImDrawIdx base = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
			drawList->PrimWriteVtx(eye, white, WithAlpha(color, nearAlpha / 255.0F));
			drawList->PrimWriteVtx(points[i], white, WithAlpha(color, nearAlpha * (1.0F - 0.92F * along[i]) / 255.0F));
			drawList->PrimWriteVtx(points[i + 1], white, WithAlpha(color, nearAlpha * (1.0F - 0.92F * along[i + 1]) / 255.0F));
			drawList->PrimWriteIdx(base);
			drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
			drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
		}
		// The rim where its sight ends: a soft line along the far edge and over the walls that stop it.
		const float rimAlpha = std::min(1.0F, opacity * strength * 1.1F);
		for (int i = 0; i < triangles; ++i) {
			const float fade = 1.0F - 0.6F * std::max(along[i], along[i + 1]);
			drawList->AddLine(points[i], points[i + 1], WithAlpha(color, rimAlpha * fade * 0.8F), 1.5F);
		}
		// The two sides of the field, faint.
		drawList->AddLine(eye, points[0], WithAlpha(color, rimAlpha * 0.35F), 1.0F);
		drawList->AddLine(eye, points[cone.Rays - 1], WithAlpha(color, rimAlpha * 0.35F), 1.0F);
	}
#pragma endregion

#pragma region Marks
	void DrawMark(ImDrawList* drawList, const Actor* actor, int state, float level, float alpha, long long engagedAge) {
		const float u = Unit();
		// Just over what its own HUD drew above its head (health, team), or over its head.
		const float stack = actor->GetAboveHUDPos().m_Y - actor->GetPos().m_Y - 6.0F;
		ImVec2 at = DebugDraw::ToScreen(actor->GetPos() + Vector(0.0F, std::min(stack, -actor->GetHeight() * 0.5F) - 4.0F));
		const float time = static_cast<float>(ImGui::GetTime());
		float radius = 0.62F * u;
		at.y -= radius;
		const ImU32 color = StateColor(state, 255);
		if (state == ActorAwareness::Noticing) {
			// A "?" in a ring that fills as it notices; nearly there, it throbs.
			const float throb = level > 0.7F ? 0.08F * std::sin(time * 14.0F) : 0.0F;
			radius *= 1.0F + throb;
			drawList->AddCircleFilled(at, radius, IM_COL32(12, 10, 4, static_cast<int>(170 * alpha)), 24);
			drawList->AddCircle(at, radius, IM_COL32(255, 255, 255, static_cast<int>(45 * alpha)), 24, 0.16F * u);
			if (level > 0.0F) {
				drawList->PathArcTo(at, radius, -c_PI * 0.5F, -c_PI * 0.5F + 2.0F * c_PI * std::clamp(level, 0.0F, 1.0F), 24);
				drawList->PathStroke(WithAlpha(color, alpha), 0, 0.2F * u);
			}
			OutlinedText(drawList, ImVec2(at.x + 0.5F, at.y), 1.05F * u, WithAlpha(color, alpha * (0.65F + 0.35F * level)), "?");
		} else if (state == ActorAwareness::Searching) {
			// A "?" in a ring with a gap going round: looking about. It bobs a little.
			at.y += std::sin(time * 4.0F) * 0.12F * u;
			drawList->AddCircleFilled(at, radius, IM_COL32(14, 8, 2, static_cast<int>(170 * alpha)), 24);
			const float start = time * 3.0F;
			drawList->PathArcTo(at, radius, start, start + c_PI * 1.55F, 24);
			drawList->PathStroke(WithAlpha(color, alpha), 0, 0.18F * u);
			OutlinedText(drawList, ImVec2(at.x + 0.5F, at.y), 1.05F * u, WithAlpha(color, alpha), "?");
		} else if (state == ActorAwareness::Engaged) {
			// A red "!" that pops up when it spots someone, with a ring going out, then settles smaller while it fights.
			const float seconds = static_cast<float>(std::max<long long>(engagedAge, 0)) / 1000.0F;
			float size = 1.0F;
			if (seconds < 0.45F) {
				size = 0.4F + 0.75F * EaseOutBack(seconds / 0.45F);
				const float wave = seconds / 0.45F;
				drawList->AddCircle(at, radius * (1.0F + 1.6F * wave), WithAlpha(color, alpha * (1.0F - wave)), 32, 0.14F * u);
			} else if (seconds < 2.5F) {
				size = 1.15F;
			} else {
				size = 1.15F - 0.35F * std::min(1.0F, (seconds - 2.5F) / 0.4F);
			}
			radius *= size;
			drawList->AddCircleFilled(at, radius, WithAlpha(color, alpha * 0.92F), 24);
			drawList->AddCircle(at, radius, IM_COL32(60, 0, 0, static_cast<int>(200 * alpha)), 24, 0.09F * u);
			OutlinedText(drawList, ImVec2(at.x + 0.5F, at.y), 1.1F * u * size, IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), "!");
		}
	}
#pragma endregion

#pragma region Meter
	/// One reason the meter gives: what it says and whether it hides you or gives you away.
	struct Reason {
		const char* Text;
		bool GivesAway;
	};

	/// The eye: as open as you are visible, coloured by whether you are hidden, plain, being noticed or spotted.
	void DrawEye(ImDrawList* drawList, ImVec2 at, float u, float openness, ImU32 color, ImU32 iris, bool slashed, float alpha) {
		const float halfWidth = 0.85F * u;
		const float lid = 0.55F * u * std::clamp(openness, 0.08F, 1.0F);
		const ImVec2 left(at.x - halfWidth, at.y);
		const ImVec2 right(at.x + halfWidth, at.y);
		auto almond = [&]() {
			drawList->PathLineTo(left);
			drawList->PathBezierCubicCurveTo(ImVec2(at.x - halfWidth * 0.45F, at.y - lid * 1.3F), ImVec2(at.x + halfWidth * 0.45F, at.y - lid * 1.3F), right, 16);
			drawList->PathBezierCubicCurveTo(ImVec2(at.x + halfWidth * 0.45F, at.y + lid * 1.3F), ImVec2(at.x - halfWidth * 0.45F, at.y + lid * 1.3F), left, 16);
		};
		// A dark backing behind, so it shows on snow and sky.
		drawList->AddCircleFilled(at, halfWidth * 1.25F, IM_COL32(0, 0, 0, static_cast<int>(110 * alpha)), 28);
		almond();
		drawList->PathFillConvex(WithAlpha(color, alpha * 0.22F));
		// The iris, cut to the lids' height.
		const float irisRadius = std::min(0.32F * u, lid * 0.9F);
		if (irisRadius > 1.0F) {
			drawList->AddCircleFilled(at, irisRadius, WithAlpha(iris, alpha), 20);
			drawList->AddCircleFilled(at, irisRadius * 0.45F, IM_COL32(10, 10, 14, static_cast<int>(230 * alpha)), 16);
		}
		almond();
		drawList->PathStroke(WithAlpha(color, alpha), ImDrawFlags_Closed, std::max(1.5F, 0.11F * u));
		if (slashed) {
			drawList->AddLine(ImVec2(at.x - halfWidth * 0.9F, at.y + halfWidth * 0.6F), ImVec2(at.x + halfWidth * 0.9F, at.y - halfWidth * 0.6F), WithAlpha(color, alpha), std::max(1.5F, 0.12F * u));
		}
	}

	void DrawMeter(ImDrawList* drawList, Activity* activity, int viewerTeam, long long now) {
		Actor* controlled = activity->GetControlledActor(Players::PlayerOne);
		if (!g_MovableMan.IsActor(controlled) || controlled->IsDead()) {
			return;
		}
		MeterReading reading;
		{
			std::lock_guard<std::mutex> lock(s_MeterMutex);
			reading = s_Meter;
		}
		if (reading.ActorID != controlled->GetUniqueID() || reading.AtMS < 0 || now - reading.AtMS > c_FreshMS) {
			return;
		}
		const ActorConcealment& hidden = reading.Hidden;
		const float visibility = hidden.Visibility();
		// Who is watching you: the most any enemy AI unit has noticed you, and whether one is fighting you.
		const long myID = controlled->GetUniqueID();
		float noticed = 0.0F;
		bool spotted = false;
		for (const Actor* actor: g_MovableMan.GetActorList()) {
			if (!actor || actor->IsDead() || actor->GetTeam() == controlled->GetTeam()) {
				continue;
			}
			int state = StateOf(actor, now);
			if (state < 0 || actor->GetAwareness().TargetID != myID) {
				continue;
			}
			if (state == ActorAwareness::Engaged) {
				spotted = true;
			} else if (state == ActorAwareness::Noticing) {
				noticed = std::max(noticed, actor->GetAwareness().Level);
			}
		}
		const bool hiddenNow = visibility < 0.7F || hidden.Cloaked || hidden.Night > 0.3F || noticed > 0.0F || spotted;
		if (AIAwareness::Meter == AIAwareness::MeterWhenHidden && !hiddenNow) {
			return;
		}

		std::vector<Reason> reasons;
		if (spotted) {
			reasons.push_back({"Spotted", true});
		} else if (noticed > 0.0F) {
			reasons.push_back({"Being noticed", true});
		}
		if (hidden.Cloaked) {
			reasons.push_back({"Cloaked", false});
		} else {
			if (hidden.Firing && hidden.Night > 0.05F) {
				reasons.push_back({"Firing", true});
			} else if (hidden.Headlamp) {
				reasons.push_back({"Headlamp on", true});
			} else if (hidden.Lit) {
				reasons.push_back({"In the light", true});
			}
			if (StealthOn() && reading.Noise >= 0.3F) {
				reasons.push_back({"Loud steps", true});
			}
			if (!hidden.Lit && hidden.Night > 0.3F) {
				reasons.push_back({hidden.Roofed ? "Dark, under a roof" : "Dark", false});
			} else if (hidden.Roofed && hidden.Night > 0.05F && !hidden.Lit) {
				reasons.push_back({"Under a roof", false});
			}
			if (hidden.Profile < 0.6F) {
				reasons.push_back({"Prone", false});
			} else if (hidden.Profile < 0.95F) {
				reasons.push_back({"Crouched", false});
			}
			if (hidden.Moving < 1.0F) {
				reasons.push_back({"Still", false});
			}
			if (WeatherEffects::GetSightMultiplier() < 0.85F) {
				reasons.push_back({"Dust", false});
			}
		}

		const float u = Unit();
		const float time = static_cast<float>(ImGui::GetTime());
		// Beside the head, on the side away from where it faces (where its gun is).
		const ImVec2 head = DebugDraw::ToScreen(controlled->GetEyePos());
		const float side = controlled->IsHFlipped() ? 1.0F : -1.0F;
		const float scale = DebugDraw::ScenePixelsPerWindowPixel();
		const float out = std::max(1.9F * u, (controlled->GetRadius() * 0.55F) / scale + 1.2F * u);
		const ImVec2 at(head.x + side * out, head.y - 0.6F * u);

		ImU32 color;
		ImU32 iris;
		if (spotted) {
			color = IM_COL32(255, 80, 64, 255);
			iris = color;
		} else if (noticed > 0.0F) {
			color = IM_COL32(255, 214, 72, 255);
			iris = color;
		} else if (hidden.Cloaked || visibility < 0.35F) {
			color = IM_COL32(150, 186, 222, 255);
			iris = IM_COL32(110, 150, 200, 255);
		} else if (visibility < 0.7F) {
			color = IM_COL32(224, 230, 238, 255);
			iris = IM_COL32(170, 190, 210, 255);
		} else {
			color = IM_COL32(255, 196, 92, 255);
			iris = IM_COL32(255, 170, 60, 255);
		}
		const float alpha = 0.95F;
		if (spotted) {
			// A glow throbbing behind the eye while you are being fought.
			const float throb = 0.5F + 0.5F * std::sin(time * 9.0F);
			drawList->AddCircleFilled(at, 1.35F * u, IM_COL32(255, 60, 40, static_cast<int>(70 + 60 * throb)), 28);
		}
		DrawEye(drawList, at, u, hidden.Cloaked ? 0.5F : 0.15F + 0.85F * visibility, color, iris, hidden.Cloaked, alpha);
		if (noticed > 0.0F && !spotted) {
			// How far the most watchful enemy has got noticing you, round the eye.
			drawList->PathArcTo(at, 1.12F * u, -c_PI * 0.5F, -c_PI * 0.5F + 2.0F * c_PI * std::clamp(noticed, 0.0F, 1.0F), 28);
			drawList->PathStroke(WithAlpha(color, alpha), 0, 0.14F * u);
		}

		// How visible, as a bar under it.
		const float barWidth = 2.0F * u;
		const float barHeight = std::max(3.0F, 0.2F * u);
		const ImVec2 barTopLeft(at.x - barWidth * 0.5F, at.y + 0.85F * u);
		drawList->AddRectFilled(ImVec2(barTopLeft.x - 1.0F, barTopLeft.y - 1.0F), ImVec2(barTopLeft.x + barWidth + 1.0F, barTopLeft.y + barHeight + 1.0F), IM_COL32(0, 0, 0, 150), barHeight);
		const float filled = barWidth * std::clamp(visibility, 0.0F, 1.0F);
		if (filled > 1.0F) {
			drawList->AddRectFilledMultiColor(barTopLeft, ImVec2(barTopLeft.x + filled, barTopLeft.y + barHeight), IM_COL32(120, 160, 210, 230), WithAlpha(iris, 0.95F), WithAlpha(iris, 0.95F), IM_COL32(120, 160, 210, 230));
		}

		// Why, a line each: what gives you away warm, what hides you cool. Three at most.
		const float textSize = 0.72F * u;
		float y = barTopLeft.y + barHeight + 0.55F * u;
		int shown = 0;
		for (const Reason& reason: reasons) {
			if (shown == 3) {
				break;
			}
			ImU32 textColor = reason.GivesAway ? IM_COL32(255, 186, 110, 240) : IM_COL32(170, 205, 240, 230);
			if (reason.Text == std::string("Spotted")) {
				textColor = IM_COL32(255, 96, 80, 255);
			}
			OutlinedText(drawList, ImVec2(at.x, y), textSize, textColor, reason.Text);
			y += textSize + 2.0F;
			++shown;
		}
	}
#pragma endregion
} // namespace

bool AIAwareness::MeterWanted() {
	return Meter != MeterOff && Allowed();
}

bool AIAwareness::ReplacesAlertMark(const Actor& actor) {
	return Marks != NoUnits && Allowed() && StateOf(&actor, g_TimerMan.GetSimTimeMS()) >= 0 && Shows(Marks, &actor, ViewerTeam());
}

void AIAwareness::StoreConcealment(const Actor& actor, const ActorConcealment& concealment, float footstepNoise) {
	Activity* activity = g_ActivityMan.GetActivity();
	if (!activity || activity->GetControlledActor(Players::PlayerOne) != &actor) {
		return;
	}
	std::lock_guard<std::mutex> lock(s_MeterMutex);
	s_Meter.ActorID = actor.GetUniqueID();
	s_Meter.Hidden = concealment;
	s_Meter.Noise = footstepNoise;
	s_Meter.AtMS = g_TimerMan.GetSimTimeMS();
}

void AIAwareness::UpdateCone(Actor& actor) {
	if (Cones == NoUnits || !Allowed() || !g_ActivityMan.GetActivity() || actor.IsPlayerControlled()) {
		return;
	}
	if (!Shows(Cones, &actor, ViewerTeam())) {
		return;
	}
	const ActorAwareness& aware = actor.GetAwareness();
	const long long now = g_TimerMan.GetSimTimeMS();
	const long id = actor.GetUniqueID();
	{
		std::lock_guard<std::mutex> lock(s_ConeMutex);
		auto found = s_Cones.find(id);
		if (found != s_Cones.end() && now - found->second.AtMS < c_ConeEveryMS && now >= found->second.AtMS) {
			return;
		}
	}
	const Vector eye = actor.GetEyePos();
	const float reach = aware.Range * actor.GetNightSightScale();
	// Only those near player 1's view: no rays for a cone no one sees.
	const Box view = DebugDraw::ViewBox();
	const Vector fromCorner = g_SceneMan.ShortestDistance(view.GetCorner(), eye, g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY());
	if (fromCorner.m_X < -reach || fromCorner.m_Y < -reach || fromCorner.m_X > view.GetWidth() + reach || fromCorner.m_Y > view.GetHeight() + reach) {
		return;
	}
	Cone cone;
	cone.Eye = eye;
	cone.Facing = actor.IsHFlipped() ? -1.0F : 1.0F;
	cone.HalfAngle = std::clamp(aware.FovDegrees, 10.0F, 360.0F) * 0.5F * c_PI / 180.0F;
	cone.Reach = reach;
	cone.Rays = std::clamp(static_cast<int>(aware.FovDegrees / 4.0F), 12, c_MaxConeRays);
	cone.AtMS = now;
	for (int i = 0; i < cone.Rays; ++i) {
		const float angle = -cone.HalfAngle + 2.0F * cone.HalfAngle * static_cast<float>(i) / static_cast<float>(cone.Rays - 1);
		const Vector ray(std::cos(angle) * reach * cone.Facing, std::sin(angle) * reach);
		Vector hit;
		// (Through grass, as the scan's own looks go.)
		if (reach > 1.0F && g_SceneMan.CastStrengthRay(eye, ray, 0.0F, hit, 2, g_MaterialGrass)) {
			cone.Hit[i] = std::clamp(g_SceneMan.ShortestDistance(eye, hit, g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY()).GetMagnitude() / reach, 0.0F, 1.0F);
		} else {
			cone.Hit[i] = 1.0F;
		}
	}
	std::lock_guard<std::mutex> lock(s_ConeMutex);
	s_Cones[id] = cone;
}

void AIAwareness::Draw() {
	if (!g_ActivityMan.IsInActivity() || !g_SceneMan.GetScene() || !Allowed() || g_DebugMan.IsPhotoModeHidingHUD()) {
		s_Marks.clear();
		return;
	}
	Activity* activity = g_ActivityMan.GetActivity();
	if (!activity) {
		return;
	}
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	const int viewerTeam = ViewerTeam();
	const long long now = g_TimerMan.GetSimTimeMS();
	const double time = ImGui::GetTime();
	const float step = std::min(ImGui::GetIO().DeltaTime, 0.1F);
	const Box view = DebugDraw::ViewBox();
	const bool wraps = g_SceneMan.SceneWrapsX() || g_SceneMan.SceneWrapsY();
	auto onScreen = [&](const Vector& position, float margin) {
		const Vector fromCorner = g_SceneMan.ShortestDistance(view.GetCorner(), position, wraps);
		return fromCorner.m_X > -margin && fromCorner.m_Y > -margin && fromCorner.m_X < view.GetWidth() + margin && fromCorner.m_Y < view.GetHeight() + margin;
	};

	// The cones first, under the marks.
	if (Cones != NoUnits) {
		std::unordered_map<long, Cone> cones;
		{
			std::lock_guard<std::mutex> lock(s_ConeMutex);
			std::erase_if(s_Cones, [now](const auto& entry) { return now - entry.second.AtMS > c_FreshMS || now < entry.second.AtMS - c_FreshMS; });
			cones = s_Cones;
		}
		for (const Actor* actor: g_MovableMan.GetActorList()) {
			if (!actor) {
				continue;
			}
			auto found = cones.find(actor->GetUniqueID());
			int state = found != cones.end() ? StateOf(actor, now) : -1;
			if (state < 0 || !Shows(Cones, actor, viewerTeam) || !onScreen(found->second.Eye, found->second.Reach)) {
				continue;
			}
			DrawCone(drawList, actor, found->second, state, actor->GetAwareness().Level);
		}
	}

	if (Marks != NoUnits) {
		for (const Actor* actor: g_MovableMan.GetActorList()) {
			if (!actor) {
				continue;
			}
			int state = StateOf(actor, now);
			if (state < 0 || !Shows(Marks, actor, viewerTeam) || !onScreen(actor->GetPos(), 60.0F)) {
				continue;
			}
			MarkShown& shown = s_Marks[actor->GetUniqueID()];
			shown.SeenAt = time;
			if (state != ActorAwareness::Unaware) {
				shown.State = state;
				shown.Alpha = std::min(1.0F, shown.Alpha + step / 0.15F);
			} else {
				shown.Alpha = std::max(0.0F, shown.Alpha - step / 0.4F);
			}
			if (shown.Alpha > 0.01F) {
				DrawMark(drawList, actor, shown.State, actor->GetAwareness().Level, shown.Alpha, now - actor->GetAwareness().EngagedSinceMS);
			}
		}
		std::erase_if(s_Marks, [time](const auto& entry) { return time - entry.second.SeenAt > 2.0; });
	} else {
		s_Marks.clear();
	}

	if (Meter != MeterOff) {
		DrawMeter(drawList, activity, viewerTeam, now);
	}
}
