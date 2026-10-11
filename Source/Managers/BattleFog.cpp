// Battle Command's fog of war: you see only the enemies your team's units can see. The rest aren't drawn (Actor::SetHiddenByFog), and where
// one was last seen is marked, fading, on the map and the minimap.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		constexpr long long c_FogEvery = 6; //!< Sim updates between looks.
		constexpr float c_LeastSight = 300.0F; //!< However short a unit's own sight distance, it sees this far.
		constexpr float c_AlwaysSeen = 60.0F; //!< An enemy this close to one of your units is seen, whatever is between.
		constexpr float c_SightBlocks = 10.0F; //!< Ground at least this strong blocks sight (snow, sand and earth do; air and liquids don't).

		/// One of your units looking: from where, and how far.
		struct Eye {
			Vector Pos;
			float Reach = 0.0F;
		};

		/// Whether any of your units sees a point: near enough, with nothing solid between.
		bool Sees(const std::vector<Eye>& eyes, const Vector& at) {
			for (const Eye& eye: eyes) {
				Vector toward = g_SceneMan.ShortestDistance(eye.Pos, at, g_SceneMan.SceneWrapsX());
				const float distance = toward.GetSqrMagnitude();
				if (distance > eye.Reach * eye.Reach) {
					continue;
				}
				Vector hit;
				if (distance < c_AlwaysSeen * c_AlwaysSeen || !g_SceneMan.CastStrengthRay(eye.Pos, toward, c_SightBlocks, hit, 3, g_MaterialAir)) {
					return true;
				}
			}
			return false;
		}

		/// Every unit shown again and the marks gone, once, when the fog goes off.
		bool s_WasOn = false;
	} // namespace

	bool FogOn() {
		return Sandbox::IsBattleCommand() && s_ModeRun.Running && s_ModeRun.Settings.FogOfWar && CommandedTeam() >= 0;
	}

	void UpdateFog() {
		if (!FogOn()) {
			if (s_WasOn) {
				for (Actor* actor: SandboxAccess::Actors()) {
					actor->SetHiddenByFog(false);
				}
				s_FogGhosts.clear();
				s_WasOn = false;
			}
			return;
		}
		s_WasOn = true;
		const long long now = g_TimerMan.GetSimUpdateCount();
		if (now % c_FogEvery != 0) {
			return;
		}
		const int yours = CommandedTeam();
		std::vector<Eye> eyes;
		for (const Actor* actor: SandboxAccess::Actors()) {
			if (actor->GetTeam() == yours && !actor->IsDead()) {
				eyes.push_back({actor->GetEyePos(), std::max(actor->GetSightDistance(), c_LeastSight)});
			}
		}
		for (Actor* actor: SandboxAccess::Actors()) {
			// (Doors and anything on no side are part of the scenery, always seen.)
			if (actor->GetTeam() == yours || actor->GetTeam() < 0 || actor->GetTeam() >= c_Sides || dynamic_cast<const ADoor*>(actor)) {
				actor->SetHiddenByFog(false);
				continue;
			}
			const long id = static_cast<long>(actor->GetUniqueID());
			const bool seen = actor->IsDead() || Sees(eyes, actor->GetPos());
			auto ghost = std::find_if(s_FogGhosts.begin(), s_FogGhosts.end(), [id](const FogGhost& mark) { return mark.ID == id; });
			if (seen) {
				if (ghost != s_FogGhosts.end()) {
					s_FogGhosts.erase(ghost);
				}
			} else if (!actor->IsHiddenByFog()) {
				// Just lost sight of: marked where it was.
				FogGhost mark;
				mark.ID = id;
				mark.Team = actor->GetTeam();
				mark.Pos = actor->GetPos();
				mark.Brain = actor->IsInGroup("Brains");
				mark.SeenAt = now;
				if (ghost != s_FogGhosts.end()) {
					*ghost = mark;
				} else {
					s_FogGhosts.push_back(mark);
				}
			}
			actor->SetHiddenByFog(!seen);
		}
		std::erase_if(s_FogGhosts, [](const FogGhost& mark) { return FogGhostAge(mark) >= 1.0F; });
	}

	float FogGhostAge(const FogGhost& ghost) {
		const float seconds = static_cast<float>(g_TimerMan.GetSimUpdateCount() - ghost.SeenAt) * g_TimerMan.GetDeltaTimeSecs();
		return std::clamp(seconds / c_FogGhostSeconds, 0.0F, 1.0F);
	}

	void DrawFogGhosts() {
		if (s_FogGhosts.empty() || g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		float pixel = std::max(1.0F, std::round(2.0F / std::max(scale, 0.25F)));
		for (const FogGhost& ghost: s_FogGhosts) {
			const float age = FogGhostAge(ghost);
			if (age >= 1.0F) {
				continue;
			}
			Vector onScreen = FromCamera(ghost.Pos);
			ImVec2 base(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
			const ImU32 color = (ghost.Team >= 0 && ghost.Team < c_Sides ? c_SideColors[ghost.Team] : IM_COL32(200, 200, 200, 255)) & 0x00FFFFFF;
			const ImU32 faded = color | (static_cast<ImU32>(170.0F * (1.0F - age)) << IM_COL32_A_SHIFT);
			DrawIcon(drawList, Icon::Person, ImVec2(std::floor(base.x - pixel * 6.0F), std::floor(base.y - pixel * 6.0F)), pixel, faded);
			drawList->AddText(ImVec2(base.x + pixel * 7.0F, base.y - pixel * 6.0F), faded, "?");
		}
	}
} // namespace SandboxDetail
