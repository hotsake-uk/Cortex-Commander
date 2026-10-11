// Battle Command: squad follows me. Take control of one of your units with others selected and they come with you as its squad: they follow
// it (the AI's squad mode, keeping a little way behind and fighting as they go), and while you fire, they go for the enemy you're aiming at.
// T has them hold where they are or follow again; X calls them back to you. Let go of the unit and they hold where they stand.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		constexpr const char* c_TargetKey = "SandboxSquadTarget"; //!< The unique ID of the enemy the squad goes for (SharedBehaviors.SquadTargetUpdate).
		constexpr float c_AimReach = 1500.0F; //!< How far along your aim an enemy can be to be the one you're shooting at.
		constexpr float c_AimWidth = 40.0F; //!< How far off the line of your aim, near you; it widens with distance.
		constexpr float c_TargetSeconds = 3.0F; //!< How long the squad keeps on your target once you stop firing.

		std::vector<UnitRef> s_LastSelection; //!< Your selection as it last was before you took control of a unit.
		std::vector<UnitRef> s_Squad;
		long s_LeaderID = 0; //!< The unit you control that the squad follows; 0 for none.
		bool s_Holding = false; //!< Told to hold where they are (T), not follow.
		bool s_ToggleAsked = false; //!< T pressed, for the next sim update.
		bool s_RegroupAsked = false; //!< X pressed.
		long s_TargetID = 0; //!< The enemy you were last shooting at; 0 for none.
		long long s_TargetUntil = 0; //!< The sim update the squad gives up on it, if you've stopped.

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		bool SquadOn() { return Sandbox::IsBattleCommand() && s_ModeRun.Running && g_SettingsMan.AISquadFollowsYou(); }

		/// The unit you're controlling, if it's one of the team you command.
		Actor* Leader() {
			if (!s_Possessed || !g_MovableMan.IsActor(s_Possessed) || static_cast<long>(s_Possessed->GetUniqueID()) != s_PossessedID) {
				return nullptr;
			}
			return s_Possessed->GetTeam() == CommandedTeam() ? s_Possessed : nullptr;
		}

		std::vector<Actor*> Members() {
			std::vector<Actor*> members;
			for (const UnitRef& ref: s_Squad) {
				if (Actor* unit = GetRef(ref); unit && IsCombatant(unit)) {
					members.push_back(unit);
				}
			}
			return members;
		}

		void Follow(Actor* unit, Actor* leader) {
			HoldUnit(unit);
			unit->SetAIMode(Actor::AIMODE_SQUAD);
			unit->AddAIMOWaypoint(leader);
			unit->SetMovePathToUpdate();
		}

		void ForgetTarget() {
			for (Actor* unit: Members()) {
				unit->RemoveNumberValue(c_TargetKey);
			}
			s_TargetID = 0;
		}

		/// The squad let go: each holds where it stands, yours to order again.
		void EndSquad() {
			ForgetTarget();
			for (Actor* unit: Members()) {
				HoldUnit(unit);
			}
			s_Squad.clear();
			s_LeaderID = 0;
			s_Holding = false;
		}

		/// The units you had selected (yours, still in the fight, and not the one you took) made the squad of the one you took control of.
		void StartSquad(Actor* leader) {
			EndSquad();
			s_LeaderID = static_cast<long>(leader->GetUniqueID());
			for (const UnitRef& ref: s_LastSelection) {
				Actor* unit = GetRef(ref);
				if (unit && unit != leader && unit->GetTeam() == leader->GetTeam() && IsSelectable(unit) && IsCombatant(unit)) {
					s_Squad.push_back(ref);
				}
			}
			for (Actor* unit: Members()) {
				Follow(unit, leader);
				AnswerOrder(unit, "SquadFollowing");
			}
			if (!s_Squad.empty()) {
				g_ConsoleMan.PrintString("BATTLE: " + std::to_string(s_Squad.size()) + (s_Squad.size() == 1 ? " unit follows you" : " units follow you") + ". T: hold or follow, X: regroup on you.");
			}
		}

		/// The enemy you're aiming at, nearest you along your aim: nullptr for none.
		Actor* AimedAt(const Actor* leader) {
			const bool wraps = g_SceneMan.SceneWrapsX();
			const float angle = leader->GetAimAngle(true);
			const Vector along(std::cos(angle), -std::sin(angle));
			const Vector eye = leader->GetEyePos();
			Actor* best = nullptr;
			float bestAlong = 0.0F;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (actor->GetTeam() == leader->GetTeam() || !IsCombatant(actor) || dynamic_cast<const ACraft*>(actor)) {
					continue;
				}
				const Vector to = g_SceneMan.ShortestDistance(eye, actor->GetPos(), wraps);
				const float ahead = to.m_X * along.m_X + to.m_Y * along.m_Y;
				if (ahead <= 0.0F || ahead > c_AimReach) {
					continue;
				}
				const Vector off = to - along * ahead;
				if (off.MagnitudeIsGreaterThan(c_AimWidth + ahead * 0.05F + actor->GetRadius() * 0.5F)) {
					continue;
				}
				if (!best || ahead < bestAlong) {
					best = actor;
					bestAlong = ahead;
				}
			}
			return best;
		}

		/// While you fire, the enemy you're aiming at is the squad's target; a few seconds after you stop (or once it's down), it's dropped.
		void UpdateTarget(Actor* leader, const std::vector<Actor*>& members, long long now) {
			if (leader->GetController() && leader->GetController()->IsState(WEAPON_FIRE)) {
				if (Actor* target = AimedAt(leader)) {
					const long id = static_cast<long>(target->GetUniqueID());
					if (id != s_TargetID) {
						s_TargetID = id;
						for (Actor* unit: members) {
							unit->SetNumberValue(c_TargetKey, static_cast<double>(id));
							AnswerOrder(unit, "SquadFocusFire");
						}
					}
					s_TargetUntil = now + static_cast<long long>(c_TargetSeconds * UpdatesPerSecond());
				}
			}
			if (s_TargetID == 0) {
				return;
			}
			const MovableObject* target = g_MovableMan.FindObjectByUniqueID(s_TargetID);
			const Actor* actor = dynamic_cast<const Actor*>(target);
			if (now > s_TargetUntil || !actor || !IsCombatant(actor)) {
				ForgetTarget();
			}
		}
	} // namespace

	void UpdateSquad() {
		const bool asked = s_ToggleAsked || s_RegroupAsked;
		const bool toggle = s_ToggleAsked;
		const bool regroup = s_RegroupAsked;
		s_ToggleAsked = false;
		s_RegroupAsked = false;
		if (!SquadOn()) {
			if (s_LeaderID != 0) {
				EndSquad();
			}
			s_LastSelection.clear();
			return;
		}
		Actor* leader = Leader();
		if (!leader) {
			if (s_LeaderID != 0) {
				EndSquad();
			}
			s_LastSelection = s_Selected;
			return;
		}
		if (static_cast<long>(leader->GetUniqueID()) != s_LeaderID) {
			StartSquad(leader);
		}
		std::erase_if(s_Squad, [](const UnitRef& ref) {
			const Actor* unit = GetRef(ref);
			return !unit || !IsCombatant(unit);
		});
		const std::vector<Actor*> members = Members();
		if (members.empty()) {
			return;
		}
		if (asked) {
			if (regroup || (toggle && s_Holding)) {
				s_Holding = false;
				for (Actor* unit: members) {
					Follow(unit, leader);
					AnswerOrder(unit, regroup ? "SquadRegroup" : "SquadFollowing");
				}
			} else {
				s_Holding = true;
				ForgetTarget();
				for (Actor* unit: members) {
					HoldUnit(unit);
					AnswerOrder(unit, "SquadHolding");
				}
			}
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		if (!s_Holding && now % 6 == 0) {
			UpdateTarget(leader, members, now);
		}
	}

	void SquadKeys() {
		if (s_LeaderID == 0 || s_Squad.empty()) {
			return;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_T, false)) {
			s_ToggleAsked = true;
		} else if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
			s_RegroupAsked = true;
		}
	}

	void DrawSquad() {
		if (s_LeaderID == 0 || s_Squad.empty() || !SquadOn() || g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const float scale = ScenePixelsPerWindowPixel();
		const ImU32 color = c_SideColors[std::clamp(CommandedTeam(), 0, c_Sides - 1)];
		int count = 0;
		for (const UnitRef& ref: s_Squad) {
			const Actor* unit = GetRef(ref);
			if (!unit || !IsCombatant(unit)) {
				continue;
			}
			++count;
			// A chevron over each of the squad, above its rank and morale marks.
			const Vector onScreen = FromCamera(OverHeadPos(unit) - Vector(0.0F, 30.0F));
			const ImVec2 at(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
			drawList->AddTriangleFilled(ImVec2(at.x - 5.0F, at.y - 3.0F), ImVec2(at.x + 5.0F, at.y - 3.0F), ImVec2(at.x, at.y + 3.0F), color);
		}
		if (count == 0) {
			return;
		}
		// What they're doing and the keys, at the top of the view.
		const std::string line = std::to_string(count) + (s_Holding ? " holding" : " following") + (s_TargetID != 0 && !s_Holding ? ", on your target" : "") + "    T: " + (s_Holding ? "follow me" : "hold here") + "    X: regroup";
		const ImVec2 size = ImGui::CalcTextSize(line.c_str());
		const GameViewRect view = g_WindowMan.GetGameViewRect();
		const ImVec2 at(ViewOrigin().x + (static_cast<float>(view.w) - size.x) * 0.5F, ViewOrigin().y + 32.0F);
		drawList->AddRectFilled(ImVec2(at.x - 8.0F, at.y - 3.0F), ImVec2(at.x + size.x + 8.0F, at.y + size.y + 3.0F), IM_COL32(0, 0, 0, 140), 4.0F);
		drawList->AddText(at, IM_COL32(230, 230, 220, 255), line.c_str());
	}
} // namespace SandboxDetail
