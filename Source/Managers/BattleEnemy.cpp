// Battle Command: the enemy's commanders. A team you don't command that is led by an AI commander (BattleHasCommander) earns points as yours
// does and spends them on the same reinforcement cards at the same prices, and uses the same commander powers on the same cooldowns: strikes
// on your units where they're bunched up, supplies and rallies for its own. How well it picks follows the commanders' difficulty. Its strikes
// are marked where they'll land, and said, a few seconds before they come in, so you can scatter.

#include "SandboxInternal.h"
#include "DetMath.h"

namespace SandboxDetail {
	namespace {
		constexpr std::array<float, 3> c_BuyLookSeconds = {20.0F, 10.0F, 5.0F}; //!< Seconds between looks at what to buy, by difficulty.
		constexpr std::array<float, 3> c_PowerLookSeconds = {12.0F, 6.0F, 3.0F}; //!< And at which power to use.
		constexpr std::array<float, 3> c_WarnSeconds = {5.0F, 4.0F, 3.0F}; //!< How long a strike is marked before it comes in.
		constexpr std::array<int, 3> c_BunchedUp = {3, 2, 2}; //!< Your units together under one point that are worth a strike.
		constexpr float c_StrikeClear = 120.0F; //!< None of its own this near a point it strikes.
		constexpr float c_ArtillerySpread = 45.0F; //!< How far either side of the point the artillery's shells land (SandboxWorld's Artillery).
		constexpr float c_HurtHealth = 0.6F; //!< Units under this share of their health are worth supplies.

		/// A strike called by an enemy commander, coming in.
		struct Strike {
			int Side = 0;
			int Power = 0;
			Vector At;
			long long Due = 0; //!< The sim update it comes in on.
		};
		std::vector<Strike> s_Strikes;
		std::array<long long, c_Sides> s_NextBuy{};
		std::array<long long, c_Sides> s_NextPower{};

		float UpdatesPerSecond() { return 1.0F / std::max(g_TimerMan.GetDeltaTimeSecs(), 0.001F); }

		long long Updates(float seconds) { return static_cast<long long>(seconds * UpdatesPerSecond()); }

		bool InBattle() { return Sandbox::IsBattleCommand() && s_ModeRun.Running && !s_ModeRun.Over; }

		/// A team the enemy commander leads: not yours, and with a commander.
		bool Leads(int side) { return !BattlePlayerCommands(side) && BattleHasCommander(side); }

		float Distance(const Vector& a, const Vector& b) { return g_SceneMan.ShortestDistance(a, b, g_SceneMan.SceneWrapsX()).GetMagnitude(); }

		void Note(const std::string& note) {
			s_ModeRun.Note = note;
			s_ModeRun.NoteAt = g_TimerMan.GetSimUpdateCount();
			g_ConsoleMan.PrintString("BATTLE: " + note);
		}

		/// The fighting units in the battle: a side's own, and everyone else's.
		void Sides(int side, std::vector<Actor*>& ours, std::vector<Actor*>& theirs) {
			for (Actor* actor: SandboxAccess::Actors()) {
				if (!IsCombatant(actor) || dynamic_cast<const ACraft*>(actor) || actor->IsInGroup("Brains") || actor->GetTeam() < 0 || actor->GetTeam() >= c_Sides) {
					continue;
				}
				(actor->GetTeam() == side ? ours : theirs).push_back(actor);
			}
		}

		/// How many of some units are within reach of a point.
		int CountNear(const std::vector<Actor*>& units, const Vector& at, float reach) {
			return static_cast<int>(std::count_if(units.begin(), units.end(), [&](const Actor* unit) { return Distance(unit->GetPos(), at) < reach; }));
		}

		// ---- Reinforcements ----

		/// The card a commander buys next. Easy picks any it can have; normal and hard answer your armour with heavies, keep up the numbers
		/// with riflemen while the team is short, and otherwise mix (hard bringing in crabs once it can afford them).
		int PickCard(int side, int level, const std::vector<Actor*>& ours, const std::vector<Actor*>& theirs) {
			std::vector<int> cards;
			for (int card = 0; card < c_ReinforcementCardCount; ++card) {
				// (Diggers dig gold for the team you command; for the enemy, they're only a weaker rifleman.)
				if (ReinforcementCost(card, side) > 0.0F && std::string(c_ReinforcementCards[card].Name) != "Digger") {
					cards.push_back(card);
				}
			}
			if (cards.empty()) {
				return -1;
			}
			auto has = [&cards](const char* name) {
				for (int card: cards) {
					if (std::string(c_ReinforcementCards[card].Name) == name) {
						return card;
					}
				}
				return -1;
			};
			if (level == 0) {
				return cards[std::min(cards.size() - 1, static_cast<size_t>(Random01() * static_cast<float>(cards.size())))];
			}
			const int armour = static_cast<int>(std::count_if(theirs.begin(), theirs.end(), [](const Actor* actor) { return dynamic_cast<const ACrab*>(actor) != nullptr; }));
			if (armour > 0 && has("Heavy") >= 0) {
				return has("Heavy");
			}
			if (static_cast<int>(ours.size()) < static_cast<int>(theirs.size()) && has("Rifleman") >= 0) {
				return has("Rifleman");
			}
			const float roll = Random01();
			if (level == 2 && roll < 0.2F && has("Crab") >= 0 && s_CommandPoints[side].Points >= ReinforcementCost(has("Crab"), side) * 1.5F) {
				return has("Crab");
			}
			const char* pick = roll < 0.55F ? "Rifleman" : (roll < 0.8F ? "Heavy" : "Sniper");
			return has(pick) >= 0 ? has(pick) : cards.front();
		}

		/// A look at what to buy: a card (PickCard), as many at once as it can pay for (one on easy; on hard it saves up for three), landed
		/// over its base.
		void Buy(int side, int level) {
			std::vector<Actor*> ours;
			std::vector<Actor*> theirs;
			Sides(side, ours, theirs);
			const int card = PickCard(side, level, ours, theirs);
			Vector base;
			if (card < 0 || !BattleTeamBase(side, base)) {
				return;
			}
			const float cost = ReinforcementCost(card, side);
			const float points = s_CommandPoints[side].Points;
			int count = 1;
			if (level >= 1 && points >= cost * 3.0F) {
				count = level == 2 && points >= cost * 5.0F ? 5 : 3;
			} else if (level == 2 && points < cost * 3.0F && ours.size() >= theirs.size()) {
				// (Not short of units: saving for a bigger drop.)
				return;
			}
			if (points < cost * static_cast<float>(count)) {
				return;
			}
			// (A rocket for one or two; a dropship carries more.)
			const int craft = count == 1 && std::size(c_Crafts) > 1 ? 1 : 0;
			if (CallReinforcements(side, card, count, craft, base)) {
				AddAlert("reinforce", std::string("Enemy ") + c_ReinforcementCards[card].Name + (count > 1 ? "s" : "") + " inbound", base, IM_COL32(200, 200, 200, 255));
			}
		}

		// ---- Powers ----

		/// Where a strike does the most to the enemy: the point with most of them about it, none of its own near, and at least so many.
		bool StrikePoint(const std::vector<Actor*>& ours, const std::vector<Actor*>& theirs, float reach, int least, Vector& at) {
			int best = least - 1;
			for (const Actor* target: theirs) {
				const Vector point = target->GetPos();
				if (CountNear(ours, point, c_StrikeClear + reach) > 0) {
					continue;
				}
				if (const int count = CountNear(theirs, point, reach); count > best) {
					best = count;
					at = point;
				}
			}
			return best >= least;
		}

		/// A strike called: marked where it'll land, said, and your units under it told to get clear; it comes in once the warning's up.
		void CallStrike(int side, int power, const Vector& at, int level) {
			const long long now = g_TimerMan.GetSimUpdateCount();
			s_Strikes.push_back({side, power, at, now + Updates(c_WarnSeconds[level])});
			Note(std::string("Incoming ") + c_SideNames[side] + " " + (power == static_cast<int>(CommanderPower::Orbital) ? "orbital strike" : "artillery") + ": get clear of the marker!");
			AddAlert("strike", power == static_cast<int>(CommanderPower::Orbital) ? "Orbital strike incoming" : "Artillery incoming", at, IM_COL32(239, 60, 50, 255));
			const float reach = c_CommanderPowers[power].Radius + c_ArtillerySpread + 100.0F;
			for (Actor* actor: SandboxAccess::Actors()) {
				if (actor->GetTeam() != side && IsCombatant(actor) && Distance(actor->GetPos(), at) < reach) {
					actor->Say("IncomingStrike");
				}
			}
		}

		bool StrikeComing(int side, int power) {
			return std::any_of(s_Strikes.begin(), s_Strikes.end(), [side, power](const Strike& strike) { return strike.Side == side && strike.Power == power; });
		}

		bool Ready(int side, CommanderPower power) { return PowerSecondsLeft(side, static_cast<int>(power)) <= 0.0F && !StrikeComing(side, static_cast<int>(power)); }

		/// A look at which power to use, and where: at most one on easy and normal, two on hard. Easy only strikes and supplies. Scan (it
		/// lifts only your fog) and Evac aren't for it.
		void UsePowers(int side, int level) {
			std::vector<Actor*> ours;
			std::vector<Actor*> theirs;
			Sides(side, ours, theirs);
			if (ours.empty()) {
				return;
			}
			int left = level == 2 ? 2 : 1;
			Vector at;
			// Strikes on your units bunched up.
			if (left > 0 && level >= 1 && Ready(side, CommanderPower::Orbital) && StrikePoint(ours, theirs, c_CommanderPowers[static_cast<int>(CommanderPower::Orbital)].Radius + 40.0F, c_BunchedUp[level] + 1, at) && !StrikeOnSpawn(side, static_cast<int>(CommanderPower::Orbital), at)) {
				CallStrike(side, static_cast<int>(CommanderPower::Orbital), at, level);
				--left;
			}
			if (left > 0 && Ready(side, CommanderPower::Artillery) && StrikePoint(ours, theirs, c_CommanderPowers[static_cast<int>(CommanderPower::Artillery)].Radius + c_ArtillerySpread, c_BunchedUp[level], at) && !StrikeOnSpawn(side, static_cast<int>(CommanderPower::Artillery), at)) {
				CallStrike(side, static_cast<int>(CommanderPower::Artillery), at, level);
				--left;
			}
			// Its own: the hurt patched up, the shaken rallied, and on hard smoke over those fighting at close quarters.
			auto bestOwn = [&](float reach, auto&& counts, int least, Vector& point) {
				int best = least - 1;
				for (const Actor* unit: ours) {
					int count = 0;
					for (const Actor* other: ours) {
						if (counts(other) && Distance(other->GetPos(), unit->GetPos()) < reach) {
							++count;
						}
					}
					if (count > best) {
						best = count;
						point = unit->GetPos();
					}
				}
				return best >= least;
			};
			if (left > 0 && Ready(side, CommanderPower::Supply) && bestOwn(c_CommanderPowers[static_cast<int>(CommanderPower::Supply)].Radius, [](const Actor* unit) { return unit->GetHealth() < unit->GetMaxHealth() * c_HurtHealth; }, c_BunchedUp[level], at)) {
				left -= UsePowerAs(side, static_cast<int>(CommanderPower::Supply), at) ? 1 : 0;
			}
			if (left > 0 && level >= 1 && g_SettingsMan.AIBreaksWhenShaken() && Ready(side, CommanderPower::Rally) && bestOwn(c_CommanderPowers[static_cast<int>(CommanderPower::Rally)].Radius, [](const Actor* unit) { return unit->GetMorale() < c_ShakenMorale; }, 2, at)) {
				left -= UsePowerAs(side, static_cast<int>(CommanderPower::Rally), at) ? 1 : 0;
			}
			if (left > 0 && level == 2 && Ready(side, CommanderPower::Smoke) && bestOwn(150.0F, [&theirs](const Actor* unit) { return CountNear(theirs, unit->GetPos(), 350.0F) >= 2; }, 3, at)) {
				left -= UsePowerAs(side, static_cast<int>(CommanderPower::Smoke), at) ? 1 : 0;
			}
		}
	} // namespace

	void ForgetEnemyCommander() {
		s_Strikes.clear();
		s_NextBuy.fill(0);
		s_NextPower.fill(0);
	}

	void SaveEnemyCommander(std::ostream& out) {
		out << ' ' << s_Strikes.size();
		for (const Strike& strike: s_Strikes) {
			out << ' ' << strike.Side << ' ' << strike.Power << ' ' << strike.At.m_X << ' ' << strike.At.m_Y;
			SaveUpdate(out, strike.Due);
		}
		for (int side = 0; side < c_Sides; ++side) {
			SaveUpdate(out, s_NextBuy[side], 0);
			SaveUpdate(out, s_NextPower[side], 0);
		}
	}

	void LoadEnemyCommander(std::istream& in) {
		size_t count = 0;
		in >> count;
		s_Strikes.clear();
		for (size_t i = 0; i < count && in; ++i) {
			Strike strike;
			in >> strike.Side >> strike.Power >> strike.At.m_X >> strike.At.m_Y;
			strike.Due = LoadUpdate(in);
			if (strike.Side >= 0 && strike.Side < c_Sides && strike.Power >= 0 && strike.Power < static_cast<int>(CommanderPower::Count)) {
				s_Strikes.push_back(strike);
			}
		}
		for (int side = 0; side < c_Sides; ++side) {
			s_NextBuy[side] = LoadUpdate(in, 0);
			s_NextPower[side] = LoadUpdate(in, 0);
		}
	}

	void UpdateEnemyCommander(bool aiPaused) {
		if (!InBattle()) {
			s_Strikes.clear();
			return;
		}
		const long long now = g_TimerMan.GetSimUpdateCount();
		if (aiPaused) {
			// (Held back with the AI, so nothing comes in while it's paused.)
			for (Strike& strike: s_Strikes) {
				++strike.Due;
			}
			for (int side = 0; side < c_Sides; ++side) {
				++s_NextBuy[side];
				++s_NextPower[side];
			}
			return;
		}
		for (auto strike = s_Strikes.begin(); strike != s_Strikes.end();) {
			if (now >= strike->Due) {
				UsePowerAs(strike->Side, strike->Power, strike->At);
				strike = s_Strikes.erase(strike);
			} else {
				++strike;
			}
		}
		if (now % 15 != 7) {
			return;
		}
		const int level = std::clamp(BattleCommanderLevel(), 0, 2);
		for (int side = 0; side < c_Sides; ++side) {
			if (!Leads(side)) {
				continue;
			}
			if (g_SettingsMan.AIEnemyBuysReinforcements() && now >= s_NextBuy[side]) {
				s_NextBuy[side] = now + Updates(c_BuyLookSeconds[level]);
				Buy(side, level);
			}
			if (g_SettingsMan.AIEnemyUsesPowers() && now >= s_NextPower[side]) {
				s_NextPower[side] = now + Updates(c_PowerLookSeconds[level]);
				UsePowers(side, level);
			}
		}
	}

	void DrawEnemyStrikes() {
		if (s_Strikes.empty() || g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		const float scale = ScenePixelsPerWindowPixel();
		const long long now = g_TimerMan.GetSimUpdateCount();
		const float pulse = 0.6F + 0.4F * DetMath::Sin(static_cast<float>(ImGui::GetTime()) * 8.0F);
		for (const Strike& strike: s_Strikes) {
			const Vector onScreen = FromCamera(strike.At);
			const ImVec2 at(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
			const bool orbital = strike.Power == static_cast<int>(CommanderPower::Orbital);
			const float radius = (c_CommanderPowers[strike.Power].Radius + (orbital ? 0.0F : c_ArtillerySpread)) / scale;
			const int alpha = static_cast<int>(220.0F * pulse);
			drawList->AddCircleFilled(at, radius, IM_COL32(239, 60, 50, alpha / 5), 48);
			drawList->AddCircle(at, radius, IM_COL32(239, 60, 50, alpha), 48, 2.5F);
			drawList->AddLine(ImVec2(at.x - radius - 8.0F, at.y), ImVec2(at.x + radius + 8.0F, at.y), IM_COL32(239, 60, 50, alpha), 1.5F);
			drawList->AddLine(ImVec2(at.x, at.y - radius - 8.0F), ImVec2(at.x, at.y + radius + 8.0F), IM_COL32(239, 60, 50, alpha), 1.5F);
			// What's coming, and how long till it lands.
			const float seconds = std::max(static_cast<float>(strike.Due - now) / UpdatesPerSecond(), 0.0F);
			char text[48];
			std::snprintf(text, sizeof(text), "%s %.0f", orbital ? "ORBITAL" : "ARTILLERY", std::ceil(seconds));
			const ImVec2 size = ImGui::CalcTextSize(text);
			const ImVec2 textAt(std::floor(at.x - size.x * 0.5F), std::floor(at.y - radius - size.y - 10.0F));
			drawList->AddText(ImVec2(textAt.x + 1.0F, textAt.y + 1.0F), IM_COL32(0, 0, 0, 200), text);
			drawList->AddText(textAt, IM_COL32(255, 120, 100, 255), text);
		}
	}
} // namespace SandboxDetail
