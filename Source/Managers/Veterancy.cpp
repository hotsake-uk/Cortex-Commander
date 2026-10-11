// Battle Command: veterancy. Every unit in a battle gets a name and earns ranks from its kills and from staying alive; each rank aims better
// and quicker (its AI reads Actor's "VeteranRank" number value) and has steadier nerve (Actor::SetVeterancy). Ranks show as stars over units.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		constexpr const char* c_RankNames[] = {"Private", "Corporal", "Sergeant", "Veteran"};
		constexpr const char* c_RankShort[] = {"Pte.", "Cpl.", "Sgt.", "Vet."};
		constexpr float c_RankAt[] = {0.0F, 2.0F, 5.0F, 10.0F}; //!< Merit for each rank: a kill is one, a minute alive half of one.
		constexpr float c_MinuteMerit = 0.5F;

		constexpr const char* c_Surnames[] = {
		    "Hale", "Brandt", "Okafor", "Reyes", "Novak", "Lindqvist", "Moreau", "Tanaka", "Kowalski", "Dumont", "Petrov", "Achebe", "Sato", "Varga", "Quinn", "Ferreira",
		    "Holt", "Ibarra", "Kerr", "Mbeki", "Nakamura", "Olsen", "Pryce", "Rourke", "Silva", "Thorne", "Ueda", "Voss", "Walsh", "Yilmaz", "Zielinski", "Abbott",
		    "Bianchi", "Castell", "Drake", "Engel", "Fitch", "Garza", "Haas", "Ivers", "Jansen", "Kaur", "Lowe", "Mercer", "Nyberg", "Ortiz", "Park", "Rask",
		    "Stroud", "Tamsin", "Ulrich", "Vance", "Wren", "Xu", "Young", "Zane", "Acosta", "Becker", "Cole", "Dahl", "Ekwueme", "Frost", "Grieve", "Hadley"};

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		bool InBattle() { return Sandbox::IsBattleCommand() && s_ModeRun.Running; }

		/// The same name for the same unit, picked by its ID.
		std::string NameFor(long id) {
			unsigned long mixed = static_cast<unsigned long>(id) * 2654435761UL;
			return c_Surnames[(mixed >> 7) % std::size(c_Surnames)];
		}

		int RankFor(const Veteran& veteran) {
			const float minutes = static_cast<float>(g_TimerMan.GetSimUpdateCount() - veteran.Since) / UpdatesPerSecond() / 60.0F;
			const float merit = static_cast<float>(veteran.Kills) + minutes * c_MinuteMerit;
			int rank = 0;
			while (rank + 1 < static_cast<int>(std::size(c_RankAt)) && merit >= c_RankAt[rank + 1]) {
				++rank;
			}
			return rank;
		}

		void Note(const std::string& note) {
			s_ModeRun.Note = note;
			s_ModeRun.NoteAt = g_TimerMan.GetSimUpdateCount();
			g_ConsoleMan.PrintString("BATTLE: " + note);
		}

		/// Its rank in stars, drawn as small gold diamonds side by side, centred on a point.
		void DrawStars(ImDrawList* drawList, ImVec2 centre, int rank, float alpha) {
			const float size = 4.0F;
			const float gap = size * 2.4F;
			float x = centre.x - gap * static_cast<float>(rank - 1) * 0.5F;
			const ImU32 fill = IM_COL32(255, 215, 90, static_cast<int>(255.0F * alpha));
			const ImU32 edge = IM_COL32(40, 30, 10, static_cast<int>(220.0F * alpha));
			for (int i = 0; i < rank; ++i, x += gap) {
				ImVec2 points[] = {ImVec2(x, centre.y - size), ImVec2(x + size, centre.y), ImVec2(x, centre.y + size), ImVec2(x - size, centre.y)};
				drawList->AddConvexPolyFilled(points, 4, fill);
				drawList->AddPolyline(points, 4, edge, ImDrawFlags_Closed, 1.0F);
			}
		}
	} // namespace

	void ForgetVeterans() {
		s_Veterans.clear();
	}

	void NoteVeteranKill(long killerID, long victimID) {
		if (!InBattle()) {
			return;
		}
		if (auto victim = s_Veterans.find(victimID); victim != s_Veterans.end()) {
			if (victim->second.Rank > 0 && victim->second.Team == CommandedTeam()) {
				Note(std::string(c_RankNames[victim->second.Rank]) + " " + victim->second.Name + " has fallen (" + std::to_string(victim->second.Kills) + (victim->second.Kills == 1 ? " kill)" : " kills)"));
			}
			s_Veterans.erase(victim);
		}
		if (auto killer = s_Veterans.find(killerID); killerID != 0 && killer != s_Veterans.end()) {
			++killer->second.Kills;
		}
	}

	void UpdateVeterans() {
		if (!InBattle()) {
			if (!s_Veterans.empty()) {
				s_Veterans.clear();
			}
			return;
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		if (now % 30 != 0) {
			return;
		}
		const int yours = CommandedTeam();
		for (Actor* actor: SandboxAccess::Actors()) {
			if (!IsCombatant(actor) || dynamic_cast<const ACraft*>(actor) || actor->IsInGroup("Brains")) {
				continue;
			}
			const long id = static_cast<long>(actor->GetUniqueID());
			auto found = s_Veterans.find(id);
			if (found == s_Veterans.end()) {
				Veteran veteran;
				veteran.Team = actor->GetTeam();
				veteran.Since = now;
				veteran.Name = NameFor(id);
				found = s_Veterans.emplace(id, veteran).first;
			}
			Veteran& veteran = found->second;
			const int rank = RankFor(veteran);
			if (rank > veteran.Rank) {
				veteran.Rank = rank;
				actor->SetVeterancy(rank);
				actor->SetNumberValue("VeteranRank", static_cast<double>(rank));
				if (veteran.Team == yours) {
					Note(veteran.Name + " is now a " + c_RankNames[rank] + " (" + std::to_string(veteran.Kills) + (veteran.Kills == 1 ? " kill)" : " kills)"));
				}
			}
		}
		// (Units gone without a kill noted: carried off, pulled out, or gone in a ship.)
		if (now % 600 == 0) {
			std::unordered_map<long, bool> live;
			for (const Actor* actor: SandboxAccess::Actors()) {
				live[static_cast<long>(actor->GetUniqueID())] = true;
			}
			std::erase_if(s_Veterans, [&live](const auto& entry) { return !live.count(entry.first); });
		}
	}

	std::string VeteranTitle(long id) {
		auto found = s_Veterans.find(id);
		return found == s_Veterans.end() ? std::string() : std::string(c_RankShort[found->second.Rank]) + " " + found->second.Name;
	}

	void DrawVeterans() {
		if (!InBattle() || s_Veterans.empty() || g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const float scale = ScenePixelsPerWindowPixel();
		const int yours = CommandedTeam();
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		for (const Actor* actor: SandboxAccess::Actors()) {
			if (actor->IsHiddenByFog()) {
				continue;
			}
			auto found = s_Veterans.find(static_cast<long>(actor->GetUniqueID()));
			if (found == s_Veterans.end()) {
				continue;
			}
			const Veteran& veteran = found->second;
			Vector onScreen = FromCamera(actor->GetPos() - Vector(0.0F, actor->GetHeight() * 0.5F + 14.0F));
			ImVec2 at(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
			if (veteran.Rank > 0) {
				DrawStars(drawList, at, veteran.Rank, veteran.Team == yours ? 1.0F : 0.7F);
			}
			// The name of yours selected or under the pointer.
			if (veteran.Team == yours) {
				const bool selected = std::any_of(s_Selected.begin(), s_Selected.end(), [actor](const UnitRef& ref) { return RefersTo(ref, actor); });
				const bool pointed = std::abs(mouse.x - at.x) < 16.0F && mouse.y > at.y - 10.0F && mouse.y < at.y + actor->GetHeight() / scale + 20.0F;
				if (selected || pointed) {
					std::string title = std::string(c_RankShort[veteran.Rank]) + " " + veteran.Name + (veteran.Kills > 0 ? "  " + std::to_string(veteran.Kills) + (veteran.Kills == 1 ? " kill" : " kills") : std::string());
					ImVec2 size = ImGui::CalcTextSize(title.c_str());
					ImVec2 corner(std::floor(at.x - size.x * 0.5F), std::floor(at.y - 8.0F - size.y));
					drawList->AddRectFilled(ImVec2(corner.x - 3.0F, corner.y - 1.0F), ImVec2(corner.x + size.x + 3.0F, corner.y + size.y + 1.0F), IM_COL32(0, 0, 0, 140), 2.0F);
					drawList->AddText(corner, IM_COL32(235, 230, 210, 255), title.c_str());
				}
			}
		}
	}
} // namespace SandboxDetail
