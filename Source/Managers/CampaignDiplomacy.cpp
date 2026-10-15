#include "CampaignModel.h"

#include <algorithm>
#include <cmath>

// Diplomacy between the campaign's factions: stances and opinion, deals and trade pacts, peace and alliances, and the AI's side of it.
namespace RTE::CampaignModel {
	namespace {
		constexpr int c_BreakVictim = 40; //!< Opinion a broken treaty costs with the faction attacked.
		constexpr int c_BreakEveryone = 20; //!< And with everyone else.
		constexpr int c_AttackVictim = 25; //!< Opinion attacking a neutral costs with it.
		constexpr int c_AttackEveryone = 5; //!< And with everyone else.

		bool Valid(const Campaign& campaign, int faction) { return faction >= 0 && faction < static_cast<int>(campaign.Factions.size()); }

		bool Alive(const Campaign& campaign, int faction) { return Valid(campaign, faction) && campaign.Factions[faction].Alive; }

		const std::string& NameOf(const Campaign& campaign, int faction) {
			static const std::string c_Nobody = "Nobody";
			return Valid(campaign, faction) ? campaign.Factions[faction].Name : c_Nobody;
		}

		bool Human(const Campaign& campaign, int faction) { return Valid(campaign, faction) && campaign.Factions[faction].Human; }

		/// How much of a resource a faction likes to keep, as its AI trades by.
		int Want(const Campaign& campaign, int faction, Resource resource) {
			const size_t index = static_cast<size_t>(resource);
			switch (resource) {
				case Resource::Food: {
					const int used = UsageOf(campaign, faction)[index];
					return std::max(0, (used - ProductionOf(campaign, faction)[index]) * 4) + used + 2;
				}
				case Resource::Fuel: {
					int armies = 0;
					for (const Army& army: campaign.Armies) {
						armies += army.Faction == faction ? 1 : 0;
					}
					return 3 + armies * 2;
				}
				case Resource::Water:
					return 6;
				case Resource::Metal:
					return 8;
				case Resource::Crystal:
					return 3;
				default:
					return 0;
			}
		}

		/// What a unit of a resource is worth to a faction, in gold: dearer when it's short of it, cheaper when it has plenty.
		float UnitValue(const Campaign& campaign, int faction, Resource resource, bool getting) {
			const size_t index = static_cast<size_t>(resource);
			const float price = campaign.Prices[index] > 0.0F ? campaign.Prices[index] : BasePrice(resource);
			const int stock = campaign.Factions[faction].Resources[index];
			const int want = Want(campaign, faction, resource);
			if (stock < want) {
				return price * (getting ? 1.3F : 1.6F);
			}
			if (resource != Resource::Relics && stock > want * 3 + 10) {
				return price * (getting ? 0.5F : 0.7F);
			}
			return price * (getting ? 0.9F : 1.05F);
		}

		bool Empty(const Deal& deal) {
			return deal.GoldGive <= 0 && deal.GoldGet <= 0 && std::all_of(deal.Give.begin(), deal.Give.end(), [](int each) { return each <= 0; }) && std::all_of(deal.Get.begin(), deal.Get.end(), [](int each) { return each <= 0; });
		}

		bool GivesNothingBack(const Deal& deal) { return deal.GoldGet <= 0 && std::all_of(deal.Get.begin(), deal.Get.end(), [](int each) { return each <= 0; }); }

		/// Moves what a deal says between its two factions.
		void Transfer(Campaign& campaign, const Deal& deal) {
			Faction& from = campaign.Factions[deal.From];
			Faction& to = campaign.Factions[deal.To];
			from.Gold += std::max(deal.GoldGet, 0) - std::max(deal.GoldGive, 0);
			to.Gold += std::max(deal.GoldGive, 0) - std::max(deal.GoldGet, 0);
			for (int resource = 0; resource < c_ResourceCount; ++resource) {
				const int give = std::max(deal.Give[resource], 0);
				const int get = std::max(deal.Get[resource], 0);
				from.Resources[resource] += get - give;
				to.Resources[resource] += give - get;
			}
		}

		void SetStance(Campaign& campaign, int a, int b, Stance stance) {
			Relation& relation = RelationOf(campaign, a, b);
			relation.Stance = static_cast<int>(stance);
			relation.Turns = 0;
		}

		void DropPactsBetween(Campaign& campaign, int a, int b) {
			std::erase_if(campaign.Pacts, [a, b](const Pact& pact) { return (pact.Terms.From == a && pact.Terms.To == b) || (pact.Terms.From == b && pact.Terms.To == a); });
		}

		/// Whether two factions both hold points in one system.
		bool Neighbours(const Campaign& campaign, int a, int b) {
			for (const StarSystem& system: campaign.Systems) {
				bool hasA = false;
				bool hasB = false;
				for (int planet: system.Planets) {
					for (const Point& point: campaign.Planets[planet].Points) {
						hasA = hasA || point.Owner == a;
						hasB = hasB || point.Owner == b;
					}
				}
				if (hasA && hasB) {
					return true;
				}
			}
			return false;
		}

		bool HasOffer(const Campaign& campaign, int from, int to) {
			return std::any_of(campaign.Offers.begin(), campaign.Offers.end(), [from, to](const Offer& offer) { return offer.From == from && offer.To == to; });
		}

		/// A trade an AI faction would like from a human one: something it's short of that they have plenty of, for something of its own
		/// they're short of, or gold. False when there's nothing worth asking.
		bool TradeToOffer(const Campaign& campaign, int ai, int human, Deal& deal) {
			const Faction& self = campaign.Factions[ai];
			const Faction& them = campaign.Factions[human];
			int need = -1;
			for (int resource = 0; resource < static_cast<int>(Resource::Relics); ++resource) {
				const Resource each = static_cast<Resource>(resource);
				if (self.Resources[resource] < Want(campaign, ai, each) && them.Resources[resource] >= Want(campaign, human, each) * 2 + 4) {
					need = resource;
					break;
				}
			}
			if (need < 0) {
				return false;
			}
			const int amount = 3;
			const float worth = static_cast<float>(amount) * UnitValue(campaign, human, static_cast<Resource>(need), false) * 1.1F;
			deal = Deal();
			deal.From = ai;
			deal.To = human;
			deal.Get[need] = amount;
			for (int resource = 0; resource < static_cast<int>(Resource::Relics); ++resource) {
				const Resource each = static_cast<Resource>(resource);
				if (resource != need && them.Resources[resource] < Want(campaign, human, each) && self.Resources[resource] > Want(campaign, ai, each) * 2 + 4) {
					const int give = std::max(1, static_cast<int>(std::ceil(worth / std::max(UnitValue(campaign, human, each, true), 1.0F))));
					if (give <= self.Resources[resource] - Want(campaign, ai, each)) {
						deal.Give[resource] = give;
						return true;
					}
				}
			}
			const int gold = static_cast<int>(std::ceil(worth / 10.0F)) * 10;
			if (self.Gold - gold < 300) {
				return false;
			}
			deal.GoldGive = gold;
			return true;
		}
	} // namespace

	const char* StanceName(Stance stance) {
		static constexpr const char* c_Names[] = {"War", "Neutral", "Peace", "Alliance"};
		return c_Names[std::clamp(static_cast<int>(stance), 0, static_cast<int>(Stance::Count) - 1)];
	}

	const char* OpinionWord(int opinion) {
		if (opinion < -40) {
			return "Hostile";
		}
		if (opinion < 0) {
			return "Wary";
		}
		return opinion < 40 ? "Cordial" : "Friendly";
	}

	void ResetRelations(Campaign& campaign) {
		campaign.Relations.assign(campaign.Factions.size() * campaign.Factions.size(), Relation());
		campaign.Pacts.clear();
		campaign.Offers.clear();
	}

	Relation& RelationOf(Campaign& campaign, int a, int b) {
		static Relation s_Ignored;
		if (!Valid(campaign, a) || !Valid(campaign, b) || a == b) {
			s_Ignored = Relation();
			return s_Ignored;
		}
		const size_t count = campaign.Factions.size();
		if (campaign.Relations.size() != count * count) {
			campaign.Relations.assign(count * count, Relation());
		}
		return campaign.Relations[static_cast<size_t>(std::min(a, b)) * count + static_cast<size_t>(std::max(a, b))];
	}

	const Relation& RelationOf(const Campaign& campaign, int a, int b) {
		static const Relation c_Neutral;
		const size_t count = campaign.Factions.size();
		if (!Valid(campaign, a) || !Valid(campaign, b) || a == b || campaign.Relations.size() != count * count) {
			return c_Neutral;
		}
		return campaign.Relations[static_cast<size_t>(std::min(a, b)) * count + static_cast<size_t>(std::max(a, b))];
	}

	Stance StanceOf(const Campaign& campaign, int a, int b) {
		if (a == b && Valid(campaign, a)) {
			return Stance::Alliance;
		}
		return static_cast<Stance>(std::clamp(RelationOf(campaign, a, b).Stance, 0, static_cast<int>(Stance::Count) - 1));
	}

	bool Allied(const Campaign& campaign, int a, int b) { return Valid(campaign, a) && Valid(campaign, b) && StanceOf(campaign, a, b) == Stance::Alliance; }

	bool Friendly(const Campaign& campaign, int a, int b) {
		if (!Valid(campaign, a) || !Valid(campaign, b)) {
			return false;
		}
		const Stance stance = StanceOf(campaign, a, b);
		return stance == Stance::Peace || stance == Stance::Alliance;
	}

	int TreatyBroken(const Campaign& campaign, int faction, int planet, int point) {
		const Point& here = campaign.Planets[planet].Points[point];
		if (Valid(campaign, here.Owner) && here.Owner != faction && StanceOf(campaign, faction, here.Owner) == Stance::Peace) {
			return here.Owner;
		}
		for (int army: ArmiesAt(campaign, planet, point)) {
			const int other = campaign.Armies[army].Faction;
			if (other != faction && StanceOf(campaign, faction, other) == Stance::Peace) {
				return other;
			}
		}
		return -1;
	}

	float MightOf(const Campaign& campaign, int faction) {
		float might = 0.0F;
		for (const Army& army: campaign.Armies) {
			if (army.Faction == faction) {
				might += Strength(campaign, army);
			}
		}
		return might;
	}

	void ChangeOpinion(Campaign& campaign, int a, int b, int by) {
		if (a == b || !Valid(campaign, a) || !Valid(campaign, b)) {
			return;
		}
		Relation& relation = RelationOf(campaign, a, b);
		relation.Opinion = std::clamp(relation.Opinion + by, -100, 100);
	}

	void DeclareWar(Campaign& campaign, int from, int to) {
		if (from == to || !Valid(campaign, from) || !Valid(campaign, to)) {
			return;
		}
		const Stance was = StanceOf(campaign, from, to);
		if (was == Stance::War) {
			return;
		}
		const bool treaty = was == Stance::Peace || was == Stance::Alliance;
		ChangeOpinion(campaign, from, to, -(treaty ? c_BreakVictim : c_AttackVictim));
		for (int other = 0; other < static_cast<int>(campaign.Factions.size()); ++other) {
			if (other != from && other != to && campaign.Factions[other].Alive) {
				ChangeOpinion(campaign, from, other, -(treaty ? c_BreakEveryone : c_AttackEveryone));
			}
		}
		SetStance(campaign, from, to, Stance::War);
		DropPactsBetween(campaign, from, to);
		if (treaty) {
			AddLog(campaign, NameOf(campaign, from) + " broke its " + (was == Stance::Alliance ? "alliance" : "peace") + " with " + NameOf(campaign, to) + ": everyone trusts them less.");
		} else {
			AddLog(campaign, NameOf(campaign, from) + " and " + NameOf(campaign, to) + " are at war.");
		}
		// The attacked side's allies may join in.
		for (int ally = 0; ally < static_cast<int>(campaign.Factions.size()); ++ally) {
			if (ally == from || ally == to || !Alive(campaign, ally) || !Allied(campaign, ally, to) || Allied(campaign, ally, from) || StanceOf(campaign, ally, from) == Stance::War) {
				continue;
			}
			if (Human(campaign, ally)) {
				if (!Human(campaign, to) && !HasOffer(campaign, to, ally)) {
					Offer offer;
					offer.What = Offer::Kind::JoinWar;
					offer.From = to;
					offer.To = ally;
					offer.Against = from;
					offer.Text = NameOf(campaign, from) + " has attacked us. Stand with us, ally, and go to war with them.";
					campaign.Offers.push_back(offer);
				}
			} else if (RelationOf(campaign, ally, to).Opinion >= 30 && (StanceOf(campaign, ally, from) != Stance::Peace || RelationOf(campaign, ally, from).Opinion < -30)) {
				SetStance(campaign, ally, from, Stance::War);
				ChangeOpinion(campaign, ally, from, -15);
				ChangeOpinion(campaign, ally, to, 10);
				DropPactsBetween(campaign, ally, from);
				AddLog(campaign, NameOf(campaign, ally) + " joins its ally " + NameOf(campaign, to) + " in the war against " + NameOf(campaign, from) + ".");
			}
		}
	}

	int DealValue(const Campaign& campaign, const Deal& deal) {
		if (!Valid(campaign, deal.From) || !Valid(campaign, deal.To)) {
			return 0;
		}
		float value = static_cast<float>(std::max(deal.GoldGive, 0) - std::max(deal.GoldGet, 0));
		for (int resource = 0; resource < c_ResourceCount; ++resource) {
			const Resource each = static_cast<Resource>(resource);
			value += static_cast<float>(std::max(deal.Give[resource], 0)) * UnitValue(campaign, deal.To, each, true);
			value -= static_cast<float>(std::max(deal.Get[resource], 0)) * UnitValue(campaign, deal.To, each, false);
		}
		if (deal.Pact) {
			value *= static_cast<float>(c_PactTurns) * 0.7F; // Ten turns of it, less for the risk it falls through.
		}
		return static_cast<int>(std::round(value));
	}

	bool CanPay(const Campaign& campaign, const Deal& deal, std::string& why) {
		const Faction& from = campaign.Factions[deal.From];
		const Faction& to = campaign.Factions[deal.To];
		if (from.Gold < deal.GoldGive) {
			why = from.Name + " hasn't the gold.";
			return false;
		}
		if (to.Gold < deal.GoldGet) {
			why = to.Name + " hasn't the gold.";
			return false;
		}
		for (int resource = 0; resource < c_ResourceCount; ++resource) {
			if (from.Resources[resource] < deal.Give[resource]) {
				why = from.Name + " hasn't the " + ResourceName(static_cast<Resource>(resource)) + ".";
				return false;
			}
			if (to.Resources[resource] < deal.Get[resource]) {
				why = to.Name + " hasn't the " + ResourceName(static_cast<Resource>(resource)) + ".";
				return false;
			}
		}
		return true;
	}

	std::string ProposeDeal(Campaign& campaign, const Deal& deal) {
		if (!Alive(campaign, deal.From) || !Alive(campaign, deal.To) || deal.From == deal.To) {
			return "There's nobody to deal with.";
		}
		if (Human(campaign, deal.To)) {
			return "They're played by a person: ask them.";
		}
		if (Empty(deal)) {
			return "There's nothing in it.";
		}
		const Relation& relation = RelationOf(campaign, deal.From, deal.To);
		if (StanceOf(campaign, deal.From, deal.To) == Stance::War) {
			return "We're at war. Make peace first.";
		}
		if (relation.Opinion < -50) {
			return "We won't deal with you.";
		}
		std::string why;
		if (!CanPay(campaign, deal, why)) {
			return why;
		}
		const int value = DealValue(campaign, deal);
		const int leaning = relation.Opinion * 2;
		if (value + leaning < 0) {
			const int more = -(value + leaning);
			return "That's not worth it to us. Offer more, or ask for less (about " + std::to_string(std::max(10, (more + 9) / 10 * 10)) + " gold's worth" + (deal.Pact ? " over the pact" : "") + ").";
		}
		if (deal.Pact) {
			campaign.Pacts.push_back(Pact{deal, c_PactTurns});
			ChangeOpinion(campaign, deal.From, deal.To, 5);
			AddLog(campaign, NameOf(campaign, deal.From) + " and " + NameOf(campaign, deal.To) + " agreed a trade pact for " + std::to_string(c_PactTurns) + " turns.");
		} else {
			Transfer(campaign, deal);
			ChangeOpinion(campaign, deal.From, deal.To, GivesNothingBack(deal) ? std::clamp(value / 25, 2, 25) : 3);
			AddLog(campaign, NameOf(campaign, deal.From) + (GivesNothingBack(deal) ? " sent a gift to " : " traded with ") + NameOf(campaign, deal.To) + ".");
		}
		return "";
	}

	std::string ProposeStance(Campaign& campaign, int from, int to, Stance stance) {
		if (!Alive(campaign, from) || !Alive(campaign, to) || from == to) {
			return "There's nobody to ask.";
		}
		if (Human(campaign, to)) {
			return "They're played by a person: ask them.";
		}
		const Stance now = StanceOf(campaign, from, to);
		const Relation& relation = RelationOf(campaign, from, to);
		const float theirs = MightOf(campaign, to);
		const float ours = MightOf(campaign, from);
		if (stance == Stance::Peace) {
			if (now == Stance::Peace || now == Stance::Alliance) {
				return "We're already at peace.";
			}
			if (relation.Opinion < -30) {
				return "We don't trust you enough for peace.";
			}
			if (now == Stance::Neutral && relation.Opinion < 10) {
				return "We don't know you well enough yet. Trade with us first.";
			}
			if (now == Stance::War && theirs > ours * 1.3F && relation.Turns < 10) {
				return "We're winning this war.";
			}
			SetStance(campaign, from, to, Stance::Peace);
			ChangeOpinion(campaign, from, to, 10);
			AddLog(campaign, NameOf(campaign, from) + " and " + NameOf(campaign, to) + " made peace.");
			return "";
		}
		if (stance == Stance::Alliance) {
			if (now == Stance::Alliance) {
				return "We're already allies.";
			}
			if (now == Stance::War) {
				return "We're at war. Make peace first.";
			}
			if (relation.Opinion < 50) {
				return "We don't trust you enough for an alliance.";
			}
			for (int other = 0; other < static_cast<int>(campaign.Factions.size()); ++other) {
				if (other != from && other != to && Alive(campaign, other) && Allied(campaign, to, other) && StanceOf(campaign, other, from) == Stance::War) {
					return "Our ally " + NameOf(campaign, other) + " is at war with you.";
				}
			}
			SetStance(campaign, from, to, Stance::Alliance);
			ChangeOpinion(campaign, from, to, 10);
			AddLog(campaign, NameOf(campaign, from) + " and " + NameOf(campaign, to) + " are now allies.");
			return "";
		}
		return "That can't be asked for.";
	}

	void EndTreaty(Campaign& campaign, int from, int to) {
		const Stance now = StanceOf(campaign, from, to);
		if (from == to || (now != Stance::Peace && now != Stance::Alliance)) {
			return;
		}
		SetStance(campaign, from, to, Stance::Neutral);
		ChangeOpinion(campaign, from, to, -15);
		AddLog(campaign, NameOf(campaign, from) + " ended its " + (now == Stance::Alliance ? "alliance" : "peace") + " with " + NameOf(campaign, to) + ".");
	}

	void CancelPact(Campaign& campaign, int pact) {
		if (pact < 0 || pact >= static_cast<int>(campaign.Pacts.size())) {
			return;
		}
		const Deal terms = campaign.Pacts[pact].Terms;
		ChangeOpinion(campaign, terms.From, terms.To, -10);
		campaign.Pacts.erase(campaign.Pacts.begin() + pact);
		AddLog(campaign, "The trade pact between " + NameOf(campaign, terms.From) + " and " + NameOf(campaign, terms.To) + " was cancelled.");
	}

	void AnswerOffer(Campaign& campaign, int index, bool accept) {
		if (index < 0 || index >= static_cast<int>(campaign.Offers.size())) {
			return;
		}
		const Offer offer = campaign.Offers[index];
		campaign.Offers.erase(campaign.Offers.begin() + index);
		if (!Alive(campaign, offer.From) || !Alive(campaign, offer.To)) {
			return;
		}
		switch (offer.What) {
			case Offer::Kind::Deal: {
				std::string why;
				if (!accept) {
					ChangeOpinion(campaign, offer.From, offer.To, -2);
				} else if (!CanPay(campaign, offer.Terms, why)) {
					AddLog(campaign, "The trade with " + NameOf(campaign, offer.From) + " fell through: " + why);
				} else if (offer.Terms.Pact) {
					campaign.Pacts.push_back(Pact{offer.Terms, c_PactTurns});
					ChangeOpinion(campaign, offer.From, offer.To, 5);
					AddLog(campaign, NameOf(campaign, offer.From) + " and " + NameOf(campaign, offer.To) + " agreed a trade pact for " + std::to_string(c_PactTurns) + " turns.");
				} else {
					Transfer(campaign, offer.Terms);
					ChangeOpinion(campaign, offer.From, offer.To, 5);
					AddLog(campaign, NameOf(campaign, offer.From) + " traded with " + NameOf(campaign, offer.To) + ".");
				}
				break;
			}
			case Offer::Kind::Peace:
			case Offer::Kind::Alliance:
				if (accept) {
					const bool alliance = offer.What == Offer::Kind::Alliance;
					SetStance(campaign, offer.From, offer.To, alliance ? Stance::Alliance : Stance::Peace);
					ChangeOpinion(campaign, offer.From, offer.To, 10);
					AddLog(campaign, NameOf(campaign, offer.From) + " and " + NameOf(campaign, offer.To) + (alliance ? " are now allies." : " made peace."));
				} else {
					ChangeOpinion(campaign, offer.From, offer.To, -5);
				}
				break;
			case Offer::Kind::JoinWar:
				if (accept && Alive(campaign, offer.Against)) {
					const Stance with = StanceOf(campaign, offer.To, offer.Against);
					if (with == Stance::Peace || with == Stance::Alliance) {
						DeclareWar(campaign, offer.To, offer.Against);
					} else if (with != Stance::War) {
						SetStance(campaign, offer.To, offer.Against, Stance::War);
						ChangeOpinion(campaign, offer.To, offer.Against, -15);
						DropPactsBetween(campaign, offer.To, offer.Against);
						AddLog(campaign, NameOf(campaign, offer.To) + " joins its ally " + NameOf(campaign, offer.From) + " in the war against " + NameOf(campaign, offer.Against) + ".");
					}
					ChangeOpinion(campaign, offer.From, offer.To, 15);
				} else if (!accept) {
					ChangeOpinion(campaign, offer.From, offer.To, -10);
				}
				break;
		}
	}

	void UpdateDiplomacy(Campaign& campaign) {
		const int count = static_cast<int>(campaign.Factions.size());
		if (campaign.Relations.size() != static_cast<size_t>(count * count)) {
			campaign.Relations.assign(static_cast<size_t>(count * count), Relation());
		}
		std::vector<float> might(static_cast<size_t>(count), 0.0F);
		float total = 0.0F;
		int alive = 0;
		for (int faction = 0; faction < count; ++faction) {
			if (campaign.Factions[faction].Alive) {
				might[faction] = MightOf(campaign, faction);
				total += might[faction];
				++alive;
			}
		}
		for (int a = 0; a < count; ++a) {
			for (int b = a + 1; b < count; ++b) {
				if (!campaign.Factions[a].Alive || !campaign.Factions[b].Alive) {
					continue;
				}
				Relation& relation = RelationOf(campaign, a, b);
				++relation.Turns;
				const Stance stance = StanceOf(campaign, a, b);
				int change = stance == Stance::War ? -1 : stance == Stance::Peace ? 1 : stance == Stance::Alliance ? 2 : 0;
				for (int c = 0; c < count; ++c) {
					if (c != a && c != b && campaign.Factions[c].Alive && StanceOf(campaign, a, c) == Stance::War && StanceOf(campaign, b, c) == Stance::War) {
						change += 2; // A shared enemy.
						break;
					}
				}
				if (stance != Stance::Alliance && Neighbours(campaign, a, b)) {
					change -= 1;
				}
				// A faction much stronger than the average of the rest worries them.
				if (alive > 2) {
					const float average = (total - might[a] - might[b]) / static_cast<float>(alive - 2);
					if (stance != Stance::Alliance && (might[a] > average * 1.6F + 50.0F || might[b] > average * 1.6F + 50.0F)) {
						change -= 1;
					}
				}
				relation.Opinion -= relation.Opinion / 20; // Old feelings fade.
				relation.Opinion = std::clamp(relation.Opinion + change, -100, 100);
			}
		}
		// Trade pacts: this turn's swap, while both can pay.
		for (int pact = 0; pact < static_cast<int>(campaign.Pacts.size());) {
			Pact& each = campaign.Pacts[pact];
			std::string why;
			if (!Alive(campaign, each.Terms.From) || !Alive(campaign, each.Terms.To)) {
				campaign.Pacts.erase(campaign.Pacts.begin() + pact);
				continue;
			}
			if (!CanPay(campaign, each.Terms, why)) {
				ChangeOpinion(campaign, each.Terms.From, each.Terms.To, -10);
				AddLog(campaign, "The trade pact between " + NameOf(campaign, each.Terms.From) + " and " + NameOf(campaign, each.Terms.To) + " fell through: " + why);
				campaign.Pacts.erase(campaign.Pacts.begin() + pact);
				continue;
			}
			Transfer(campaign, each.Terms);
			ChangeOpinion(campaign, each.Terms.From, each.Terms.To, 1);
			if (--each.TurnsLeft <= 0) {
				AddLog(campaign, "The trade pact between " + NameOf(campaign, each.Terms.From) + " and " + NameOf(campaign, each.Terms.To) + " has run its course.");
				campaign.Pacts.erase(campaign.Pacts.begin() + pact);
				continue;
			}
			++pact;
		}
	}

	void AIDiplomacy(Campaign& campaign, int faction) {
		bool asked = false; // One offer to a person a turn, at most.
		for (int other = 0; other < static_cast<int>(campaign.Factions.size()); ++other) {
			if (other == faction || !Alive(campaign, other)) {
				continue;
			}
			const Stance stance = StanceOf(campaign, faction, other);
			const Relation relation = RelationOf(campaign, faction, other);
			const bool person = Human(campaign, other);
			auto propose = [&](Offer::Kind kind, Stance wanted, const std::string& text) {
				if (person) {
					if (!asked && !HasOffer(campaign, faction, other)) {
						Offer offer;
						offer.What = kind;
						offer.From = faction;
						offer.To = other;
						offer.Text = text;
						campaign.Offers.push_back(offer);
						asked = true;
					}
				} else {
					ProposeStance(campaign, faction, other, wanted);
				}
			};
			const float ours = MightOf(campaign, faction);
			const float theirs = MightOf(campaign, other);
			if (stance == Stance::War && relation.Turns >= 4 && (ours < theirs * 0.7F || (relation.Opinion > -30 && relation.Turns >= 10)) && Roll(campaign) < 0.3F) {
				propose(Offer::Kind::Peace, Stance::Peace, ours < theirs * 0.7F ? "We've had enough of this war. Will you make peace?" : "This war has gone on long enough. Peace?");
			} else if (stance == Stance::Neutral && relation.Opinion >= 20 && Roll(campaign) < 0.1F) {
				propose(Offer::Kind::Peace, Stance::Peace, "We've no quarrel with you. Shall we sign a peace?");
			} else if (stance == Stance::Peace && relation.Turns >= 5 && relation.Opinion >= 55 && Roll(campaign) < 0.2F) {
				propose(Offer::Kind::Alliance, Stance::Alliance, "We've been at peace a long while. Will you be our ally?");
			} else if (stance == Stance::Peace && relation.Opinion < -40 && ours > theirs * 1.5F && Roll(campaign) < 0.1F) {
				EndTreaty(campaign, faction, other);
			} else if (person && stance != Stance::War && relation.Opinion >= -20 && !asked && Roll(campaign) < 0.12F) {
				Offer offer;
				if (TradeToOffer(campaign, faction, other, offer.Terms)) {
					offer.What = Offer::Kind::Deal;
					offer.From = faction;
					offer.To = other;
					offer.Terms.Pact = Roll(campaign) < 0.3F;
					offer.Text = offer.Terms.Pact ? "We'd like a trade pact with you." : "We'd like to trade.";
					campaign.Offers.push_back(offer);
					asked = true;
				}
			}
		}
	}

	bool AlliedWin(const Campaign& campaign, int& winner) {
		std::vector<int> alive;
		for (int faction = 0; faction < static_cast<int>(campaign.Factions.size()); ++faction) {
			if (campaign.Factions[faction].Alive) {
				alive.push_back(faction);
			}
		}
		if (alive.size() < 2) {
			return false;
		}
		for (size_t a = 0; a < alive.size(); ++a) {
			for (size_t b = a + 1; b < alive.size(); ++b) {
				if (!Allied(campaign, alive[a], alive[b])) {
					return false;
				}
			}
		}
		winner = alive.front();
		for (int faction: alive) {
			if (campaign.Factions[faction].Human) {
				winner = faction;
			}
		}
		return true;
	}
} // namespace RTE::CampaignModel
