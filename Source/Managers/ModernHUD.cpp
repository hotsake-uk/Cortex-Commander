#include "WindowMan.h"
#include "ModernHUD.h"
#include "AHuman.h"
#include "ACraft.h"
#include "ActorWater.h"
#include "Activity.h"
#include "ActivityMan.h"
#include "CameraMan.h"
#include "Constants.h"
#include "FrameMan.h"
#include "HDFirearm.h"
#include "MovableMan.h"
#include "SceneMan.h"
#include "SLTerrain.h"
#include "Scene.h"
#include "TimerMan.h"
#include "imgui/imgui.h"
#include "glad/gl.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

using namespace RTE;

bool ModernHUD::s_Enabled = true;

namespace {
	/// A downscaled picture of the scene's terrain, rebuilt now and then.
	struct Minimap {
		GLuint Texture = 0;
		int Width = 0;
		int Height = 0;
		const void* Scene = nullptr;
		long long BuiltAtUpdate = -1000000;
	};
	Minimap s_Minimap;

	struct FeedEntry {
		std::string Text;
		int Team;
		double Time; //!< ImGui time it was added.
	};
	std::deque<FeedEntry> s_Feed;
	std::unordered_map<const Actor*, std::pair<std::string, int>> s_KnownActors; //!< Actors seen last frame, with name and team, to notice who's gone.

	/// A number floating up from a unit that just lost health.
	struct DamageNumber {
		Vector Position;
		int Amount;
		bool Mine; //!< Whether it's the unit the player controls.
		double Time;
	};
	std::vector<DamageNumber> s_DamageNumbers;

	/// What each unit's health was when its last number was shown, by unique ID.
	struct HealthRecord {
		float Shown;
		double LastNumberTime;
		bool Seen;
	};
	std::unordered_map<long, HealthRecord> s_Health;

	/// A red arc near the middle of the screen pointing to where a hit on the player's unit came from.
	struct DamageArc {
		float Angle;
		double Time;
	};
	std::vector<DamageArc> s_DamageArcs;

	// The game's own team colours, in team order: red, green, blue, yellow (as the team icons and the sandbox's sides). Blue first, team 1's
	// units showed blue on the minimap and in the feed, team 2's red.
	const ImU32 c_TeamColors[] = {IM_COL32(249, 120, 100, 255), IM_COL32(170, 210, 100, 255), IM_COL32(110, 180, 250, 255), IM_COL32(248, 230, 100, 255)};

	ImU32 TeamColor(int team) { return (team >= 0 && team < 4) ? c_TeamColors[team] : IM_COL32(200, 200, 200, 255); }

	void UpdateMinimap() {
		Scene* scene = g_SceneMan.GetScene();
		SLTerrain* terrain = scene ? scene->GetTerrain() : nullptr;
		if (!terrain) {
			return;
		}
		long long now = g_TimerMan.GetSimUpdateCount();
		// Rebuild every few seconds; terrain changes slowly at minimap scale.
		if (s_Minimap.Scene == scene && now - s_Minimap.BuiltAtUpdate < 180) {
			return;
		}
		int sceneWidth = g_SceneMan.GetSceneWidth();
		int sceneHeight = g_SceneMan.GetSceneHeight();
		int step = std::max(1, std::max(sceneWidth / 240, sceneHeight / 160));
		int width = std::max(1, sceneWidth / step);
		int height = std::max(1, sceneHeight / step);
		std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4, 0);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				int material = terrain->GetMaterialPixel(x * step, y * step);
				unsigned char* pixel = &pixels[(static_cast<size_t>(y) * width + x) * 4];
				if (material != g_MaterialAir) {
					// Solid ground in a muted earth tone, darker deeper down.
					float depth = static_cast<float>(y) / static_cast<float>(height);
					pixel[0] = static_cast<unsigned char>(150 - 60 * depth);
					pixel[1] = static_cast<unsigned char>(120 - 50 * depth);
					pixel[2] = static_cast<unsigned char>(85 - 35 * depth);
					pixel[3] = 230;
				} else {
					pixel[0] = 15;
					pixel[1] = 22;
					pixel[2] = 35;
					pixel[3] = 160;
				}
			}
		}
		if (!s_Minimap.Texture) {
			glGenTextures(1, &s_Minimap.Texture);
		}
		glBindTexture(GL_TEXTURE_2D, s_Minimap.Texture);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glBindTexture(GL_TEXTURE_2D, 0);
		s_Minimap.Width = width;
		s_Minimap.Height = height;
		s_Minimap.Scene = scene;
		s_Minimap.BuiltAtUpdate = now;
	}

	void DrawBar(ImDrawList* drawList, ImVec2 position, ImVec2 size, float fraction, ImU32 color, const char* label) {
		fraction = std::clamp(fraction, 0.0F, 1.0F);
		drawList->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y), IM_COL32(10, 12, 18, 190), 4.0F);
		if (fraction > 0.0F) {
			drawList->AddRectFilled(ImVec2(position.x + 2.0F, position.y + 2.0F), ImVec2(position.x + 2.0F + (size.x - 4.0F) * fraction, position.y + size.y - 2.0F), color, 3.0F);
		}
		drawList->AddRect(position, ImVec2(position.x + size.x, position.y + size.y), IM_COL32(255, 255, 255, 60), 4.0F);
		ImVec2 textSize = ImGui::CalcTextSize(label);
		ImVec2 textPosition(position.x + (size.x - textSize.x) * 0.5F, position.y + (size.y - textSize.y) * 0.5F);
		// Dark outline so the label reads over any bar color.
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (dx != 0 || dy != 0) {
					drawList->AddText(ImVec2(textPosition.x + static_cast<float>(dx), textPosition.y + static_cast<float>(dy)), IM_COL32(0, 0, 0, 200), label);
				}
			}
		}
		drawList->AddText(textPosition, IM_COL32(255, 255, 255, 240), label);
	}
} // namespace

void ModernHUD::Draw() {
	if (!s_Enabled || !g_ActivityMan.IsInActivity() || !g_SceneMan.GetScene()) {
		s_KnownActors.clear();
		s_Health.clear();
		s_DamageNumbers.clear();
		s_DamageArcs.clear();
		return;
	}
	Activity* activity = g_ActivityMan.GetActivity();
	if (!activity) {
		return;
	}
	ImGuiIO& io = ImGui::GetIO();
	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	// Everything is placed within the game's picture, which isn't the whole window when tool panels are docked at the sides.
	GameViewRect view = g_WindowMan.GetGameViewRect();
	ImVec2 viewOrigin(view.x, view.y);
	ImVec2 viewSize(view.w, view.h);
	float scale = std::clamp(viewSize.y / 720.0F, 0.75F, 2.5F);
	float margin = 14.0F * scale;

	// Minimap, top right.
	UpdateMinimap();
	if (s_Minimap.Texture) {
		float mapWidth = 220.0F * scale;
		float mapHeight = mapWidth * static_cast<float>(s_Minimap.Height) / static_cast<float>(std::max(1, s_Minimap.Width));
		mapHeight = std::min(mapHeight, 160.0F * scale);
		ImVec2 topLeft(viewOrigin.x + viewSize.x - mapWidth - margin, viewOrigin.y + margin + 24.0F * scale);
		ImVec2 bottomRight(topLeft.x + mapWidth, topLeft.y + mapHeight);
		drawList->AddRectFilled(ImVec2(topLeft.x - 3.0F, topLeft.y - 3.0F), ImVec2(bottomRight.x + 3.0F, bottomRight.y + 3.0F), IM_COL32(0, 0, 0, 150), 5.0F);
		drawList->AddImage(static_cast<ImTextureID>(s_Minimap.Texture), topLeft, bottomRight);
		float sceneWidth = static_cast<float>(g_SceneMan.GetSceneWidth());
		float sceneHeight = static_cast<float>(g_SceneMan.GetSceneHeight());
		auto toMap = [&](const Vector& position) { return ImVec2(topLeft.x + position.m_X / sceneWidth * mapWidth, topLeft.y + position.m_Y / sceneHeight * mapHeight); };
		// The player's view.
		Vector viewTopLeft = g_CameraMan.GetOffset(0);
		Vector viewBottomRight = viewTopLeft + Vector(static_cast<float>(g_FrameMan.GetPlayerScreenWidth()), static_cast<float>(g_FrameMan.GetPlayerScreenHeight()));
		drawList->AddRect(toMap(viewTopLeft), toMap(viewBottomRight), IM_COL32(255, 255, 255, 140), 2.0F);
		// Units.
		Actor* controlled = activity->GetControlledActor(0);
		int viewerTeam = activity->GetTeamOfPlayer(0);
		for (const Actor* actor: g_MovableMan.m_Actors) {
			if (!actor || actor->IsDead()) {
				continue;
			}
			// Other sides' units only where the player's side can see: under the fog of war they gave every hidden enemy away.
			if (actor->GetTeam() != viewerTeam && viewerTeam >= 0 && g_SceneMan.IsUnseen(actor->GetPos().GetFloorIntX(), actor->GetPos().GetFloorIntY(), viewerTeam)) {
				continue;
			}
			ImVec2 point = toMap(actor->GetPos());
			float radius = (actor == controlled ? 3.5F : 2.2F) * scale;
			drawList->AddCircleFilled(point, radius + 1.0F, IM_COL32(0, 0, 0, 200));
			drawList->AddCircleFilled(point, radius, actor == controlled ? IM_COL32(255, 255, 255, 255) : TeamColor(actor->GetTeam()));
		}
		drawList->AddRect(ImVec2(topLeft.x - 3.0F, topLeft.y - 3.0F), ImVec2(bottomRight.x + 3.0F, bottomRight.y + 3.0F), IM_COL32(255, 255, 255, 50), 5.0F);
	}

	// Controlled unit: name, health and ammo, bottom left.
	// The game can still be pointing at a unit that was deleted in the sim update just before this frame (it died and was removed), so make sure it's still in the game before reading it.
	if (Actor* controlled = activity->GetControlledActor(0); g_MovableMan.IsActor(controlled) && !controlled->IsDead()) {
		float barWidth = 230.0F * scale;
		float barHeight = 20.0F * scale;
		ImVec2 base(viewOrigin.x + margin, viewOrigin.y + viewSize.y - margin - barHeight * 2.0F - 30.0F * scale);
		drawList->AddText(ImVec2(base.x + 2.0F, base.y), IM_COL32(255, 255, 255, 220), controlled->GetPresetName().c_str());
		base.y += 20.0F * scale;
		float health = controlled->GetHealth();
		float maxHealth = std::max(1.0F, controlled->GetMaxHealth());
		float healthFraction = health / maxHealth;
		ImU32 healthColor = healthFraction > 0.5F ? IM_COL32(80, 205, 95, 255) : (healthFraction > 0.25F ? IM_COL32(235, 185, 50, 255) : IM_COL32(230, 60, 50, 255));
		char label[64];
		std::snprintf(label, sizeof(label), "%d / %d", static_cast<int>(health), static_cast<int>(maxHealth));
		DrawBar(drawList, base, ImVec2(barWidth, barHeight), healthFraction, healthColor, label);
		base.y += barHeight + 6.0F * scale;
		if (float air = ActorWater::GetAir(controlled); air < 1.0F) {
			DrawBar(drawList, ImVec2(base.x, base.y - barHeight * 2.0F - 12.0F * scale - 20.0F * scale - 14.0F * scale), ImVec2(barWidth, 12.0F * scale), air, air > 0.3F ? IM_COL32(110, 190, 255, 255) : IM_COL32(230, 60, 50, 255), "Air");
		}
		if (const AHuman* human = dynamic_cast<const AHuman*>(controlled)) {
			if (const HDFirearm* firearm = dynamic_cast<const HDFirearm*>(human->GetEquippedItem())) {
				int rounds = firearm->GetRoundInMagCount();
				int capacity = firearm->GetRoundInMagCapacity();
				if (capacity > 0) {
					std::snprintf(label, sizeof(label), "%s  %d / %d", firearm->GetPresetName().c_str(), std::max(rounds, 0), capacity);
					DrawBar(drawList, base, ImVec2(barWidth, barHeight), rounds < 0 ? 1.0F : static_cast<float>(rounds) / static_cast<float>(capacity), IM_COL32(230, 200, 90, 255), label);
				} else {
					std::snprintf(label, sizeof(label), "%s  infinite", firearm->GetPresetName().c_str());
					DrawBar(drawList, base, ImVec2(barWidth, barHeight), 1.0F, IM_COL32(230, 200, 90, 255), label);
				}
			}
		}
	}

	// Damage numbers over units, and arcs showing where hits on the player's unit came from. One screen only: split screens would need each view mapped.
	if (g_FrameMan.GetScreenCount() == 1) {
		double now = ImGui::GetTime();
		const Actor* mine = activity->GetControlledActor(0);
		for (auto& [id, record]: s_Health) {
			record.Seen = false;
		}
		for (const Actor* actor: g_MovableMan.m_Actors) {
			if (!actor || actor->IsDead()) {
				continue;
			}
			float health = actor->GetHealth();
			auto found = s_Health.find(actor->GetUniqueID());
			if (found == s_Health.end()) {
				s_Health.emplace(actor->GetUniqueID(), HealthRecord{health, now, true});
				continue;
			}
			HealthRecord& record = found->second;
			record.Seen = true;
			if (health > record.Shown) {
				record.Shown = health;
			} else if (record.Shown - health >= 1.0F && now - record.LastNumberTime > 0.3) {
				// Slow damage (fire, gas) is gathered into a number every so often rather than a stream of ones.
				s_DamageNumbers.push_back({actor->GetPos() - Vector(0.0F, actor->GetRadius() * 0.6F), static_cast<int>(std::round(record.Shown - health)), actor == mine, now});
				if (actor == mine && !actor->GetLastAlarmPos().IsZero()) {
					Vector from = g_SceneMan.ShortestDistance(actor->GetPos(), actor->GetLastAlarmPos(), g_SceneMan.SceneWrapsX());
					if (from.MagnitudeIsGreaterThan(1.0F)) {
						s_DamageArcs.push_back({std::atan2(from.m_Y, from.m_X), now});
					}
				}
				record.Shown = health;
				record.LastNumberTime = now;
			}
		}
		std::erase_if(s_Health, [](const auto& entry) { return !entry.second.Seen; });

		float pixelScale = viewSize.x / static_cast<float>(std::max(g_FrameMan.GetPlayerScreenWidth(), 1));
		Vector viewCorner = g_CameraMan.GetOffset(0);
		for (const DamageNumber& number: s_DamageNumbers) {
			float age = static_cast<float>(now - number.Time);
			Vector onScreen = g_SceneMan.ShortestDistance(viewCorner, number.Position, g_SceneMan.SceneWrapsX());
			ImVec2 at(viewOrigin.x + onScreen.m_X * pixelScale, viewOrigin.y + onScreen.m_Y * pixelScale - age * 34.0F * scale);
			int alpha = static_cast<int>(255.0F * std::clamp(1.0F - (age - 0.5F) / 0.4F, 0.0F, 1.0F));
			char text[16];
			std::snprintf(text, sizeof(text), "-%d", number.Amount);
			float size = ImGui::GetFontSize() * (number.Amount >= 20 ? 1.35F : 1.0F);
			drawList->AddText(ImGui::GetFont(), size, ImVec2(at.x + 1.0F, at.y + 1.0F), IM_COL32(0, 0, 0, alpha * 3 / 4), text);
			drawList->AddText(ImGui::GetFont(), size, at, number.Mine ? IM_COL32(255, 90, 80, alpha) : IM_COL32(255, 235, 200, alpha), text);
		}
		std::erase_if(s_DamageNumbers, [now](const DamageNumber& number) { return now - number.Time > 0.9; });

		ImVec2 middle(viewOrigin.x + viewSize.x * 0.5F, viewOrigin.y + viewSize.y * 0.5F);
		float arcRadius = std::min(viewSize.x, viewSize.y) * 0.3F;
		for (const DamageArc& arc: s_DamageArcs) {
			float age = static_cast<float>(now - arc.Time);
			int alpha = static_cast<int>(220.0F * std::clamp(1.0F - age / 0.9F, 0.0F, 1.0F));
			drawList->PathArcTo(middle, arcRadius, arc.Angle - 0.28F, arc.Angle + 0.28F, 16);
			drawList->PathStroke(IM_COL32(255, 60, 50, alpha), 0, 5.0F * scale);
		}
		std::erase_if(s_DamageArcs, [now](const DamageArc& arc) { return now - arc.Time > 0.9; });
	} else {
		s_DamageNumbers.clear();
		s_DamageArcs.clear();
		s_Health.clear();
	}

	// Feed, top left below the funds: units that died or vanished since last frame, and units that spawned in (appeared, or got out of
	// their ship). Not ships coming and going, nor what is there when a scene starts.
	std::unordered_map<const Actor*, std::pair<std::string, int>> currentActors;
	for (const Actor* actor: g_MovableMan.m_Actors) {
		if (actor && !actor->IsDead()) {
			currentActors.emplace(actor, std::make_pair(actor->GetPresetName(), actor->GetTeam()));
		}
	}
	for (const auto& [actor, info]: s_KnownActors) {
		if (!currentActors.count(actor)) {
			s_Feed.push_back({info.first + " lost", info.second, ImGui::GetTime()});
		}
	}
	if (!s_KnownActors.empty()) {
		// (Several of a team at once on one line, as a battle's wave, so they don't push everything else off.)
		std::unordered_map<int, std::vector<std::string>> spawned;
		for (const Actor* actor: g_MovableMan.m_Actors) {
			if (actor && !actor->IsDead() && !s_KnownActors.count(actor) && !dynamic_cast<const ACraft*>(actor)) {
				spawned[actor->GetTeam()].push_back(actor->GetPresetName());
			}
		}
		for (const auto& [team, names]: spawned) {
			s_Feed.push_back({names.size() == 1 ? names.front() + " spawned in" : std::to_string(names.size()) + " units spawned in", team, ImGui::GetTime()});
		}
	}
	s_KnownActors.swap(currentActors);
	while (!s_Feed.empty() && (ImGui::GetTime() - s_Feed.front().Time > 8.0 || s_Feed.size() > 6)) {
		s_Feed.pop_front();
	}
	ImVec2 feedPosition(margin, margin + 30.0F * scale);
	for (const FeedEntry& entry: s_Feed) {
		float age = static_cast<float>(ImGui::GetTime() - entry.Time);
		int alpha = static_cast<int>(255.0F * std::clamp(8.0F - age, 0.0F, 1.0F));
		ImVec2 textSize = ImGui::CalcTextSize(entry.Text.c_str());
		drawList->AddRectFilled(ImVec2(feedPosition.x - 4.0F, feedPosition.y - 2.0F), ImVec2(feedPosition.x + textSize.x + 14.0F, feedPosition.y + textSize.y + 2.0F), IM_COL32(0, 0, 0, alpha / 2), 3.0F);
		ImU32 teamColor = (TeamColor(entry.Team) & 0x00FFFFFF) | (static_cast<ImU32>(alpha) << 24);
		drawList->AddRectFilled(ImVec2(feedPosition.x - 4.0F, feedPosition.y - 2.0F), ImVec2(feedPosition.x, feedPosition.y + textSize.y + 2.0F), teamColor);
		drawList->AddText(ImVec2(feedPosition.x + 6.0F, feedPosition.y), IM_COL32(255, 255, 255, alpha), entry.Text.c_str());
		feedPosition.y += textSize.y + 6.0F;
	}
}
