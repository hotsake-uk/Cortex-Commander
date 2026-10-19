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

	/// How many window pixels one pixel of the marks and meter takes: one of the game's own pixels, so they sit with the rest of its pixel-art HUD, times
	/// the size setting, rounded to a whole number so no pixel is ever blurred.
	int PixelSize() {
		const float gamePixel = 1.0F / std::max(DebugDraw::ScenePixelsPerWindowPixel(), 0.01F);
		return std::max(1, static_cast<int>(std::lround(gamePixel * std::clamp(AIAwareness::MarkScale, 0.5F, 2.0F))));
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

	/// A colour taken toward another, 0 to 1 of the way.
	ImU32 Mix(ImU32 from, ImU32 to, float amount) {
		ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
		ImVec4 b = ImGui::ColorConvertU32ToFloat4(to);
		const float t = std::clamp(amount, 0.0F, 1.0F);
		return ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
	}

#pragma region Pixel art
	constexpr ImU32 c_Outline = IM_COL32(20, 12, 16, 255); //!< The dark edge round everything, as round the game's own HUD art.

	/// Where pixel art is drawn: its top left corner on a whole window pixel, how big its pixels are and how faded it is.
	struct PixelCanvas {
		ImDrawList* List;
		ImVec2 Corner;
		int P;
		float Alpha;

		/// A canvas w by h art pixels whose bottom middle is at a point.
		PixelCanvas(ImDrawList* list, ImVec2 bottomMiddle, int w, int h, int p, float alpha) :
		    List(list), Corner(std::floor(bottomMiddle.x - static_cast<float>(w * p) * 0.5F), std::floor(bottomMiddle.y - static_cast<float>(h * p))), P(p), Alpha(alpha) {}

		void Box(int x, int y, int w, int h, ImU32 color) const {
			if (w <= 0 || h <= 0) {
				return;
			}
			const float p = static_cast<float>(P);
			List->AddRectFilled(ImVec2(Corner.x + x * p, Corner.y + y * p), ImVec2(Corner.x + (x + w) * p, Corner.y + (y + h) * p), WithAlpha(color, Alpha));
		}

		void Dot(int x, int y, ImU32 color) const { Box(x, y, 1, 1, color); }

		/// A sprite from rows of letters: '#' the colour, '+' it lighter, '-' it darker, 's' it faint, 'i' and 'p' the iris and pupil, 'o' the dark
		/// edge, 'w' white; anything else is see-through.
		void Sprite(int x, int y, std::initializer_list<const char*> rows, ImU32 color, ImU32 iris = 0) const {
			int row = 0;
			for (const char* line: rows) {
				for (int column = 0; line[column]; ++column) {
					ImU32 pixel = 0;
					switch (line[column]) {
						case '#': pixel = color; break;
						case '+': pixel = Mix(color, IM_COL32(255, 255, 255, 255), 0.45F); break;
						case '-': pixel = Mix(color, IM_COL32(0, 0, 0, 255), 0.35F); break;
						case 's': pixel = WithAlpha(Mix(color, IM_COL32(0, 0, 0, 255), 0.55F), 0.75F); break;
						case 'i': pixel = iris; break;
						case 'p': pixel = c_Outline; break;
						case 'o': pixel = c_Outline; break;
						case 'w': pixel = IM_COL32(255, 255, 255, 255); break;
						default: break;
					}
					if (pixel != 0) {
						Dot(x + column, y + row, pixel);
					}
				}
				++row;
			}
		}
	};

	/// The pixels one in from the edge of a w by h frame, clockwise from the middle of its top, for a frame that fills as something goes on.
	std::vector<std::pair<int, int>> RingPixels(int w, int h) {
		std::vector<std::pair<int, int>> ring;
		for (int x = w / 2; x <= w - 2; ++x) {
			ring.emplace_back(x, 1);
		}
		for (int y = 2; y <= h - 2; ++y) {
			ring.emplace_back(w - 2, y);
		}
		for (int x = w - 3; x >= 1; --x) {
			ring.emplace_back(x, h - 2);
		}
		for (int y = h - 3; y >= 1; --y) {
			ring.emplace_back(1, y);
		}
		for (int x = 2; x < w / 2; ++x) {
			ring.emplace_back(x, 1);
		}
		return ring;
	}

	/// A w by h frame with clipped corners and the dark edge, its ring of track pixels one in, and its middle filled.
	void Frame(const PixelCanvas& canvas, int w, int h, ImU32 inside, ImU32 track) {
		canvas.Box(1, 0, w - 2, 1, c_Outline);
		canvas.Box(1, h - 1, w - 2, 1, c_Outline);
		canvas.Box(0, 1, 1, h - 2, c_Outline);
		canvas.Box(w - 1, 1, 1, h - 2, c_Outline);
		canvas.Box(1, 1, w - 2, h - 2, track);
		canvas.Box(2, 2, w - 4, h - 4, inside);
	}

	/// Lights the ring of a w by h frame: the first `count` of its pixels clockwise from `from`.
	void LightRing(const PixelCanvas& canvas, int w, int h, int from, int count, ImU32 color) {
		const std::vector<std::pair<int, int>> ring = RingPixels(w, h);
		const int size = static_cast<int>(ring.size());
		for (int i = 0; i < std::min(count, size); ++i) {
			const auto& [x, y] = ring[(from + i) % size];
			canvas.Dot(x, y, color);
		}
	}
#pragma endregion

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
		// (At the default strength about a fifth opaque at the eyes, a third while fighting: at 105/255 times the strength it was so faint it
		// could hardly be seen at all.)
		const float nearAlpha = 190.0F * opacity * strength;
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
			drawList->PrimWriteVtx(points[i], white, WithAlpha(color, nearAlpha * (1.0F - 0.75F * along[i]) / 255.0F));
			drawList->PrimWriteVtx(points[i + 1], white, WithAlpha(color, nearAlpha * (1.0F - 0.75F * along[i + 1]) / 255.0F));
			drawList->PrimWriteIdx(base);
			drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
			drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
		}
		// The rim where its sight ends: a line a game pixel thick along the far edge and over the walls that stop it.
		const float rimAlpha = std::min(1.0F, opacity * strength * 2.4F);
		const float rimWidth = std::max(1.5F, 1.0F / std::max(scale, 0.01F));
		for (int i = 0; i < triangles; ++i) {
			const float fade = 1.0F - 0.4F * std::max(along[i], along[i + 1]);
			drawList->AddLine(points[i], points[i + 1], WithAlpha(color, rimAlpha * fade), rimWidth);
		}
		// The two sides of the field, fainter.
		drawList->AddLine(eye, points[0], WithAlpha(color, rimAlpha * 0.5F), rimWidth * 0.75F);
		drawList->AddLine(eye, points[cone.Rays - 1], WithAlpha(color, rimAlpha * 0.5F), rimWidth * 0.75F);
	}
#pragma endregion

#pragma region Marks
	constexpr int c_BadgeSize = 12; //!< The marks are badges 12 game pixels square.

	void DrawMark(ImDrawList* drawList, const Actor* actor, int state, float level, float alpha, long long engagedAge) {
		const int p = PixelSize();
		// Just over what its own HUD drew above its head (health, team), or over its head.
		const float stack = actor->GetAboveHUDPos().m_Y - actor->GetPos().m_Y - 6.0F;
		ImVec2 at = DebugDraw::ToScreen(actor->GetPos() + Vector(0.0F, std::min(stack, -actor->GetHeight() * 0.5F) - 4.0F));
		const float time = static_cast<float>(ImGui::GetTime());
		const ImU32 color = StateColor(state, 255);
		const ImU32 track = IM_COL32(58, 52, 60, 255);
		const int n = c_BadgeSize;
		const int ringSize = static_cast<int>(RingPixels(n, n).size());
		auto question = [](const PixelCanvas& canvas, ImU32 glyph) {
			canvas.Sprite(3, 2, {".####.", "##..##", "....##", "...##.", "..##..", "......", "..##..", "..##.."}, glyph);
		};
		if (state == ActorAwareness::Noticing) {
			// A "?" in a frame that lights pixel by pixel, clockwise, as it notices; nearly there, it hops.
			const int hop = level > 0.7F && static_cast<int>(time * 8.0F) % 2 == 0 ? 1 : 0;
			PixelCanvas canvas(drawList, ImVec2(at.x, at.y - static_cast<float>(hop * p)), n, n, p, alpha);
			Frame(canvas, n, n, IM_COL32(16, 13, 8, 220), track);
			LightRing(canvas, n, n, 0, static_cast<int>(std::ceil(std::clamp(level, 0.0F, 1.0F) * static_cast<float>(ringSize))), color);
			question(canvas, WithAlpha(color, 0.65F + 0.35F * level));
		} else if (state == ActorAwareness::Searching) {
			// A "?" with a few frame pixels chasing round it: looking about. It bobs a pixel.
			const int bob = static_cast<int>(time * 2.0F) % 2;
			PixelCanvas canvas(drawList, ImVec2(at.x, at.y - static_cast<float>(bob * p)), n, n, p, alpha);
			Frame(canvas, n, n, IM_COL32(18, 11, 4, 220), track);
			const int head = static_cast<int>(time * 22.0F) % ringSize;
			LightRing(canvas, n, n, head, 4, Mix(color, IM_COL32(0, 0, 0, 255), 0.4F));
			LightRing(canvas, n, n, head + 4, 3, color);
			question(canvas, color);
		} else if (state == ActorAwareness::Engaged) {
			// A red "!" badge that pops up in steps when it spots someone, with pixels bursting out, then sits there while it fights.
			const float seconds = static_cast<float>(std::max<long long>(engagedAge, 0)) / 1000.0F;
			if (seconds < 0.06F) {
				PixelCanvas canvas(drawList, at, 4, 4, p, alpha);
				canvas.Box(0, 0, 4, 4, c_Outline);
				canvas.Box(1, 1, 2, 2, color);
				return;
			}
			const int rise = seconds < 0.14F ? 3 : (seconds < 0.24F ? 1 : 0);
			PixelCanvas canvas(drawList, ImVec2(at.x, at.y - static_cast<float>(rise * p)), n, n, p, alpha);
			if (seconds < 0.45F) {
				// Eight pixels thrown out from its middle, a step further each few frames, fading.
				const int out = n / 2 + 1 + static_cast<int>(seconds / 0.45F * 6.0F);
				const PixelCanvas burst(drawList, ImVec2(at.x, at.y - static_cast<float>(rise * p)), n, n, p, alpha * (1.0F - seconds / 0.45F));
				const int c = n / 2;
				for (auto [dx, dy]: {std::pair(0, -1), std::pair(1, -1), std::pair(1, 0), std::pair(1, 1), std::pair(0, 1), std::pair(-1, 1), std::pair(-1, 0), std::pair(-1, -1)}) {
					const int reach = dx != 0 && dy != 0 ? out * 3 / 4 : out;
					burst.Box(c + dx * reach - (dx < 0 ? 1 : 0), c + dy * reach - (dy < 0 ? 1 : 0), 1, 1, color);
				}
			}
			Frame(canvas, n, n, color, Mix(color, IM_COL32(0, 0, 0, 255), 0.4F));
			// A lit top edge, as the game's own buttons have.
			canvas.Box(2, 1, n - 4, 1, Mix(color, IM_COL32(255, 255, 255, 255), 0.35F));
			canvas.Sprite(5, 2, {"..", "ww", "ww", "ww", "ww", "..", "ww", "ww"}, color);
		}
	}
#pragma endregion

#pragma region Meter
	/// One reason the meter gives: what it says and whether it hides you or gives you away.
	struct Reason {
		const char* Text;
		bool GivesAway;
	};

	/// The eye: as open as you are visible, in whole frames, coloured by whether you are hidden, plain, being noticed or spotted. 11 by 7 art pixels.
	void DrawEye(const PixelCanvas& canvas, int x, int y, float openness, ImU32 color, ImU32 iris) {
		if (openness >= 0.7F) {
			canvas.Sprite(x, y, {"...#####...", ".##sssss##.", "#sssiiisss#", "#ssiipiiss#", "#sssiiisss#", ".##sssss##.", "...#####..."}, color, iris);
		} else if (openness >= 0.4F) {
			canvas.Sprite(x, y, {"...........", "...#####...", ".##ssiss##.", "#ssiipiiss#", ".##ssiss##.", "...#####...", "..........."}, color, iris);
		} else {
			canvas.Sprite(x, y, {"...........", "...........", "..#######..", "##ssipiss##", "..#######..", "...........", "..........."}, color, iris);
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

		const int p = PixelSize();
		const float time = static_cast<float>(ImGui::GetTime());
		// Beside the head, on the side away from where it faces (where its gun is).
		const ImVec2 head = DebugDraw::ToScreen(controlled->GetEyePos());
		const float side = controlled->IsHFlipped() ? 1.0F : -1.0F;
		const float scale = DebugDraw::ScenePixelsPerWindowPixel();
		const float out = (controlled->GetRadius() * 0.55F) / scale + static_cast<float>(10 * p);
		// A small panel, 15 by 11 art pixels, its middle there: the eye in it, the bar and the reasons under it.
		constexpr int panelWidth = 15;
		constexpr int panelHeight = 11;
		const ImVec2 at(head.x + side * out, head.y + static_cast<float>(panelHeight * p) * 0.5F - static_cast<float>(p));

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
		const PixelCanvas panel(drawList, at, panelWidth, panelHeight, p, 0.95F);
		// Being fought, its middle throbs red in two steps.
		ImU32 inside = IM_COL32(14, 14, 20, 210);
		if (spotted) {
			inside = static_cast<int>(time * 6.0F) % 2 == 0 ? IM_COL32(110, 20, 14, 230) : IM_COL32(60, 10, 8, 230);
		}
		Frame(panel, panelWidth, panelHeight, inside, IM_COL32(58, 52, 60, 255));
		if (noticed > 0.0F && !spotted) {
			// How far the most watchful enemy has got noticing you, lighting the frame round the eye.
			const int ringSize = static_cast<int>(RingPixels(panelWidth, panelHeight).size());
			LightRing(panel, panelWidth, panelHeight, 0, static_cast<int>(std::ceil(std::clamp(noticed, 0.0F, 1.0F) * static_cast<float>(ringSize))), color);
		} else if (spotted) {
			LightRing(panel, panelWidth, panelHeight, 0, 999, color);
		}
		DrawEye(panel, 2, 2, hidden.Cloaked ? 0.5F : 0.15F + 0.85F * visibility, color, iris);
		if (hidden.Cloaked) {
			// Struck through: you can't be seen at all.
			for (int i = 0; i < 11; ++i) {
				panel.Dot(2 + i, 8 - i / 2, color);
			}
		}

		// How visible, as a bar under it, filled a whole pixel at a time.
		const PixelCanvas bar(drawList, ImVec2(at.x, at.y + static_cast<float>(5 * p)), panelWidth, 4, p, 0.95F);
		bar.Box(0, 0, panelWidth, 4, c_Outline);
		bar.Box(1, 1, panelWidth - 2, 2, IM_COL32(40, 40, 48, 255));
		const int filled = static_cast<int>(std::lround(static_cast<float>(panelWidth - 2) * std::clamp(visibility, 0.0F, 1.0F)));
		bar.Box(1, 1, filled, 1, Mix(iris, IM_COL32(255, 255, 255, 255), 0.3F));
		bar.Box(1, 2, filled, 1, iris);

		// Why, a line each: what gives you away warm, what hides you cool. Three at most. In the game's own small pixel font where it has been
		// loaded (DebugMan::PrepareFonts), at the nearest whole size.
		ImFont* pixelFont = g_DebugMan.GetPixelFont(std::clamp(p, 1, 4));
		float y = at.y + static_cast<float>(7 * p);
		int shown = 0;
		for (const Reason& reason: reasons) {
			if (shown == 3) {
				break;
			}
			ImU32 textColor = reason.GivesAway ? IM_COL32(255, 186, 110, 240) : IM_COL32(170, 205, 240, 230);
			if (reason.Text == std::string("Spotted")) {
				textColor = IM_COL32(255, 96, 80, 255);
			}
			if (pixelFont) {
				const ImVec2 extent = pixelFont->CalcTextSizeA(pixelFont->FontSize, FLT_MAX, 0.0F, reason.Text);
				drawList->AddText(pixelFont, pixelFont->FontSize, ImVec2(std::floor(at.x - extent.x * 0.5F), std::floor(y)), textColor, reason.Text);
				y += pixelFont->FontSize;
			} else {
				const float textSize = static_cast<float>(8 * p);
				OutlinedText(drawList, ImVec2(at.x, y + textSize * 0.5F), textSize, textColor, reason.Text);
				y += textSize + 2.0F;
			}
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
