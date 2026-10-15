// Campaign mode's screen: the start page (new campaign, saved ones), the new campaign setup, and the game itself: the galaxy map, a
// system's map with its planets and moons, a planet's simplified surface map with its points, the side panel for what's selected, the
// log, and the attack and battle report windows. Armies glide between places on the maps, and captures and battles flash where they
// happen. A guide walks a new player through a first turn, and a to-do list says what needs doing each turn. Drawn in the main menu
// with ImGui, like the map browser. See Campaign.h.

#include "Campaign.h"
#include "CampaignModel.h"

#include "MenuMan.h"
#include "ToolWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <unordered_map>

using namespace RTE;
using namespace RTE::CampaignModel;

namespace {
	constexpr float c_Pi = 3.14159265F;

	enum class Page { Start, Setup, Playing };
	enum class View { Galaxy, System, Planet };

	/// An attack waiting on the player's word, in the attack window.
	struct PendingAttack {
		bool Open = false;
		long ArmyID = 0;
		int Planet = -1;
		int Point = -1;
		bool Overland = false;
	};

	/// Where an army is, as the maps place it: a point on a planet, or in orbit in a system.
	struct Place {
		int System = -1;
		int Planet = -1;
		int Point = -1;
		bool operator==(const Place& other) const { return System == other.System && Planet == other.Planet && Point == other.Point; }
		bool operator!=(const Place& other) const { return !(*this == other); }
	};

	/// An army gliding from place to place on the maps, leg by leg, or one that's gone, fading out where it was.
	struct Motion {
		long Army = 0;
		int Faction = 0;
		std::vector<Place> Legs; //!< Where it started, the places it passed, where it is. Just the one place for an army that's gone.
		double Start = 0.0;
		float LegTime = 0.4F; //!< Seconds a leg takes.
		bool Gone = false;
	};

	enum class PulseKind { Capture, Battle, Arrive };

	/// A ring or a flash on the maps: a point changing hands, a battle, a new army.
	struct Pulse {
		PulseKind Kind = PulseKind::Capture;
		Place At;
		ImU32 Color = 0;
		double Start = 0.0;
	};

	constexpr float c_GoneTime = 0.9F;
	constexpr float c_PulseTime = 1.0F;
	constexpr float c_BattleTime = 1.3F;
	constexpr float c_ViewFadeTime = 0.25F;
	constexpr float c_TurnBannerTime = 2.4F;

	/// What the maps showed last frame, to see what changed since and animate it.
	struct Tracking {
		bool Ready = false;
		std::unordered_map<long, std::pair<Place, int>> Armies; //!< Each army's place and faction.
		std::vector<std::vector<int>> Owners; //!< Each point's owner, by planet.
		std::vector<Motion> Motions;
		std::vector<Pulse> Pulses;
		int Turn = 0;
		int Gold = 0;
		double TurnStarted = -100.0; //!< When the turn banner went up.
		int TurnGold = 0; //!< Gold the new turn brought, for the banner.
		float ShownGold = 0.0F; //!< The gold counter, rolling toward the real gold.
		double ViewChanged = -100.0;
		int LastView = -1;
		int LastSystem = -1;
		int LastPlanet = -1;
	};

	/// The guide's steps, in order.
	enum class Step { Welcome, HomePlanet, ReadPoint, Resources, Recruit, TakeOff, Travel, EndTurn, Land, Battle, Control, Grow, Trade, Diplomacy, Routine, Win, Count };

	struct GuideState {
		bool On = false;
		bool Folded = false;
		Step At = Step::Welcome;
		int Base = 0; //!< What the step counts (points held, squads...), when it started.
		int BaseTurn = 0;
		int Target = -1; //!< The planet the guide suggests taking first.
		double DoneAt = -1.0; //!< When the step was done: it shows as done for a moment, then the next one starts.
		bool Fought = false; //!< The player has fought a battle since the guide started.
		bool Traded = false; //!< Bought or sold on the market since the Trade step started.
		bool Proposed = false; //!< Put something to a faction since the Diplomacy step started.
	};

	/// What the guide points at this frame, with a bouncing arrow and a pulsing ring.
	struct Highlights {
		int System = -1; //!< A star on the galaxy map.
		int Planet = -1; //!< A planet on the system map.
		std::vector<int> Points; //!< Points on the planet map shown.
		bool Galaxy = false; //!< The Galaxy button.
		bool TakeOff = false;
		bool EndTurn = false;
		bool Resources = false; //!< The resource bar.
		bool Trade = false; //!< The Trade button.
		bool Diplomacy = false; //!< The Diplomacy button.
		bool ToDo = false; //!< The To do list.
	};

	struct Screen {
		bool Open = false;
		bool Back = false;
		Page At = Page::Start;
		bool HasGame = false;
		CampaignModel::Campaign Game;
		NewCampaign Setup;
		int SetupPlayer = 0; //!< The faction picked to play, in Setup.Factions.
		std::vector<SaveInfo> Saves;
		std::string Note;
		View Showing = View::Galaxy;
		int ViewSystem = -1;
		int ViewPlanet = -1;
		long SelectedArmy = 0;
		int SelectedSystem = -1;
		int SelectedPlanet = -1;
		int SelectedPoint = -1;
		float GalaxyZoom = 1.0F;
		ImVec2 GalaxyPan = ImVec2(0.0F, 0.0F);
		PendingAttack Attack;
		bool ConfirmDisband = false;
		bool ConfirmDelete = false;
		bool MarketOpen = false; //!< The galaxy market window is up.
		bool MilitaryOpen = false; //!< The window of all the player's armies.
		bool MilitaryReadyOnly = false;
		bool DiplomacyOpen = false;
		int DealWith = -1; //!< The faction a deal is being put together for, in the diplomacy window.
		Deal DealDraft;
		std::string DiplomacyNote; //!< The last answer a faction gave.
		int ConfirmWar = -1; //!< The faction war is about to be declared on, to make sure.
		std::string DeleteFile;
		bool EndShown = false; //!< The victory or defeat window was shown for this campaign.
		bool SetupGuide = true; //!< Whether the new campaign starts with the guide.
		Tracking Track;
		GuideState Guide;
		Highlights Lights;
		long FollowArmy = 0; //!< An army just ordered somewhere: the view goes to where it ends up once it gets there.
		Place FollowFrom; //!< Where it was when ordered.
		std::vector<std::pair<std::string, Place>> Alerts; //!< Points of the player's taken by another faction without a battle, to tell them of.
		long RosterHover = 0; //!< The army the mouse is on in the army list, shown on the maps.
		long RosterHoverNext = 0;
		ImVec2 MapFrom; //!< Where the map was drawn this frame.
		ImVec2 MapTo;
	};
	Screen s_Screen;

	ImU32 Alpha(ImU32 color, float alpha) { return (color & 0x00FFFFFFu) | (static_cast<ImU32>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F) << IM_COL32_A_SHIFT); }

	ImU32 Scale(ImU32 color, float by) {
		ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
		return ImGui::ColorConvertFloat4ToU32(ImVec4(std::min(c.x * by, 1.0F), std::min(c.y * by, 1.0F), std::min(c.z * by, 1.0F), c.w));
	}

	/// A small drawn icon for a resource: a drop of water, grain for food, an ingot of metal, a flame of fuel, a cut crystal, a relic.
	void DrawResourceIcon(ImDrawList* drawList, ImVec2 c, float size, Resource resource) {
		const float r = size * 0.5F;
		const ImU32 color = ResourceColor(resource);
		const ImU32 dark = Scale(color, 0.45F);
		switch (resource) {
			case Resource::Water:
				drawList->AddCircleFilled(ImVec2(c.x, c.y + r * 0.3F), r * 0.62F, color, 16);
				drawList->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x - r * 0.57F, c.y + r * 0.08F), ImVec2(c.x + r * 0.57F, c.y + r * 0.08F), color);
				drawList->AddCircleFilled(ImVec2(c.x - r * 0.22F, c.y + r * 0.22F), r * 0.16F, IM_COL32(255, 255, 255, 170), 8);
				break;
			case Resource::Food:
				drawList->AddLine(ImVec2(c.x, c.y + r), ImVec2(c.x, c.y - r * 0.2F), dark, std::max(1.0F, r * 0.22F));
				for (int head = -1; head <= 1; ++head) {
					drawList->AddEllipseFilled(ImVec2(c.x + static_cast<float>(head) * r * 0.42F, c.y - r * (head == 0 ? 0.5F : 0.25F)), ImVec2(r * 0.24F, r * 0.5F), color, static_cast<float>(head) * 0.45F, 10);
				}
				break;
			case Resource::Metal:
				drawList->AddQuadFilled(ImVec2(c.x - r * 0.55F, c.y - r * 0.4F), ImVec2(c.x + r * 0.55F, c.y - r * 0.4F), ImVec2(c.x + r, c.y + r * 0.45F), ImVec2(c.x - r, c.y + r * 0.45F), color);
				drawList->AddLine(ImVec2(c.x - r * 0.55F, c.y - r * 0.4F), ImVec2(c.x + r * 0.55F, c.y - r * 0.4F), IM_COL32(255, 255, 255, 200), std::max(1.0F, r * 0.15F));
				drawList->AddQuad(ImVec2(c.x - r * 0.55F, c.y - r * 0.4F), ImVec2(c.x + r * 0.55F, c.y - r * 0.4F), ImVec2(c.x + r, c.y + r * 0.45F), ImVec2(c.x - r, c.y + r * 0.45F), dark, 1.0F);
				break;
			case Resource::Fuel:
				drawList->AddCircleFilled(ImVec2(c.x, c.y + r * 0.35F), r * 0.6F, color, 16);
				drawList->AddTriangleFilled(ImVec2(c.x + r * 0.1F, c.y - r), ImVec2(c.x - r * 0.6F, c.y + r * 0.3F), ImVec2(c.x + r * 0.6F, c.y + r * 0.3F), color);
				drawList->AddCircleFilled(ImVec2(c.x, c.y + r * 0.45F), r * 0.3F, IM_COL32(255, 240, 150, 255), 12);
				break;
			case Resource::Crystal:
				drawList->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.65F, c.y - r * 0.1F), ImVec2(c.x, c.y + r), ImVec2(c.x - r * 0.65F, c.y - r * 0.1F), color);
				drawList->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y - r * 0.1F), ImVec2(c.x - r * 0.65F, c.y - r * 0.1F), Scale(color, 1.5F));
				drawList->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r * 0.65F, c.y - r * 0.1F), ImVec2(c.x, c.y + r), ImVec2(c.x - r * 0.65F, c.y - r * 0.1F), dark, 1.0F);
				break;
			default:
				drawList->AddNgonFilled(c, r * 0.9F, color, 6);
				drawList->AddNgon(c, r * 0.9F, dark, 6, 1.0F);
				drawList->AddCircleFilled(c, r * 0.35F, dark, 10);
				break;
		}
	}

	/// A resource's icon on a dark disc, so it reads on any map.
	void DrawResourceBadge(ImDrawList* drawList, ImVec2 c, float size, Resource resource) {
		drawList->AddCircleFilled(c, size * 0.68F, IM_COL32(10, 10, 20, 220), 16);
		drawList->AddCircle(c, size * 0.68F, Alpha(ResourceColor(resource), 0.8F), 16, 1.0F);
		DrawResourceIcon(drawList, c, size * 0.85F, resource);
	}

	/// A resource's icon in line with text.
	void ResourceIcon(Resource resource) {
		const float size = ImGui::GetTextLineHeight();
		const ImVec2 at = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(size, size));
		DrawResourceIcon(ImGui::GetWindowDrawList(), ImVec2(at.x + size * 0.5F, at.y + size * 0.5F), size * 0.9F, resource);
	}

	ImU32 FactionColor(int faction) { return faction >= 0 && faction < static_cast<int>(s_Screen.Game.Factions.size()) ? s_Screen.Game.Factions[faction].Color : IM_COL32(150, 150, 150, 255); }

	std::string FactionLabel(int faction) { return faction >= 0 && faction < static_cast<int>(s_Screen.Game.Factions.size()) ? s_Screen.Game.Factions[faction].Name : std::string("Nobody"); }

	int Player() { return PlayerFaction(s_Screen.Game); }

	int SelectedArmyIndex() { return s_Screen.SelectedArmy ? FindArmy(s_Screen.Game, s_Screen.SelectedArmy) : -1; }

	/// The selected army, if it's the player's own.
	int OwnSelectedArmy() {
		const int army = SelectedArmyIndex();
		return army >= 0 && s_Screen.Game.Armies[army].Faction == Player() ? army : -1;
	}

	void SaveGame() {
		if (!s_Screen.HasGame) {
			return;
		}
		std::string error;
		if (!Save(s_Screen.Game, error)) {
			s_Screen.Note = "Not saved: " + error;
		}
	}

	void ShowSystem(int system) {
		s_Screen.Showing = View::System;
		s_Screen.ViewSystem = system;
		s_Screen.SelectedSystem = system;
	}

	void ShowPlanet(int planet) {
		s_Screen.Showing = View::Planet;
		s_Screen.ViewPlanet = planet;
		s_Screen.ViewSystem = s_Screen.Game.Planets[planet].System;
		s_Screen.SelectedPlanet = planet;
	}

	/// The view the game opens on: the player's home planet's system, its first army picked.
	void StartViewing() {
		s_Screen.At = Page::Playing;
		s_Screen.HasGame = true;
		s_Screen.Showing = View::Galaxy;
		s_Screen.GalaxyZoom = 1.0F;
		s_Screen.GalaxyPan = ImVec2(0.0F, 0.0F);
		s_Screen.SelectedArmy = 0;
		s_Screen.SelectedSystem = s_Screen.SelectedPlanet = s_Screen.SelectedPoint = -1;
		s_Screen.Attack = PendingAttack();
		s_Screen.EndShown = false;
		s_Screen.Game.Reports.clear();
		s_Screen.Track.Ready = false;
		s_Screen.Guide.On = false;
		s_Screen.FollowArmy = 0;
		s_Screen.Alerts.clear();
		s_Screen.MarketOpen = false;
		s_Screen.MilitaryOpen = false;
		s_Screen.DiplomacyOpen = false;
		s_Screen.DealWith = s_Screen.ConfirmWar = -1;
		s_Screen.DiplomacyNote.clear();
		const int player = Player();
		if (player >= 0) {
			const int home = s_Screen.Game.Factions[player].HomePlanet;
			if (home >= 0 && home < static_cast<int>(s_Screen.Game.Planets.size())) {
				ShowSystem(s_Screen.Game.Planets[home].System);
			}
			for (const Army& army: s_Screen.Game.Armies) {
				if (army.Faction == player) {
					s_Screen.SelectedArmy = army.Id;
					break;
				}
			}
		}
	}

#pragma region Drawing pieces
	/// A planet or moon as a lit ball: its colour, spots of its surface, a shadow on the side away from the star and a thin atmosphere.
	void DrawPlanetBall(ImDrawList* drawList, ImVec2 center, float radius, const Planet& planet, ImVec2 lightFrom) {
		const ImU32 base = PlanetColor(planet.Type);
		drawList->AddCircleFilled(center, radius, base, 48);
		std::mt19937 dice(planet.Look);
		std::uniform_real_distribution<float> unit(0.0F, 1.0F);
		if (planet.Type == PlanetType::GasGiant) {
			// Bands across it.
			for (int band = 0; band < 6; ++band) {
				const float y = -0.8F + 0.32F * static_cast<float>(band) + unit(dice) * 0.08F;
				const float half = std::sqrt(std::max(0.0F, 1.0F - y * y)) * radius;
				const float thick = radius * (0.06F + unit(dice) * 0.08F);
				drawList->AddRectFilled(ImVec2(center.x - half * 0.98F, center.y + y * radius - thick), ImVec2(center.x + half * 0.98F, center.y + y * radius + thick), Alpha(Scale(base, unit(dice) < 0.5F ? 0.75F : 1.25F), 0.7F), thick);
			}
		} else {
			// Patches: land, ice caps, craters, lava, by type.
			for (int spot = 0; spot < 9; ++spot) {
				const float angle = unit(dice) * 2.0F * c_Pi;
				const float reach = std::sqrt(unit(dice)) * 0.65F;
				const float size = radius * (0.12F + unit(dice) * 0.22F);
				const ImVec2 at(center.x + std::cos(angle) * reach * radius, center.y + std::sin(angle) * reach * radius);
				const bool light = unit(dice) < 0.5F;
				ImU32 color = Scale(base, light ? 1.3F : 0.7F);
				if (planet.Type == PlanetType::Ocean) {
					color = IM_COL32(70, 150, 80, 255);
				} else if (planet.Type == PlanetType::Volcanic && light) {
					color = IM_COL32(255, 120, 30, 255);
				}
				drawList->AddCircleFilled(at, size, Alpha(color, 0.75F), 20);
			}
			if (planet.Type == PlanetType::Ice || planet.Type == PlanetType::Temperate) {
				drawList->AddCircleFilled(ImVec2(center.x, center.y - radius * 0.82F), radius * 0.3F, IM_COL32(235, 245, 255, 200), 20);
			}
		}
		// The shadow: a crescent on the far side from the light.
		ImVec2 dark(center.x - lightFrom.x, center.y - lightFrom.y);
		const float length = std::max(std::hypot(dark.x, dark.y), 0.001F);
		dark = ImVec2(dark.x / length, dark.y / length);
		const ImVec2 across(-dark.y, dark.x);
		const float start = std::atan2(across.y, across.x);
		std::vector<ImVec2> shadow;
		constexpr int c_Steps = 24;
		for (int i = 0; i <= c_Steps; ++i) {
			const float angle = start + c_Pi * static_cast<float>(i) / c_Steps;
			shadow.emplace_back(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius);
		}
		for (int i = c_Steps - 1; i >= 1; --i) {
			const float t = c_Pi * static_cast<float>(i) / c_Steps;
			shadow.emplace_back(center.x + across.x * std::cos(t) * radius + dark.x * std::sin(t) * radius * 0.3F, center.y + across.y * std::cos(t) * radius + dark.y * std::sin(t) * radius * 0.3F);
		}
		drawList->AddConcavePolyFilled(shadow.data(), static_cast<int>(shadow.size()), IM_COL32(0, 0, 10, 150));
		drawList->AddCircle(center, radius + 1.5F, Alpha(Scale(base, 1.4F), 0.35F), 48, 2.0F);
	}

	/// A shield in a faction's colour: an army on the maps. Returns whether the mouse is on it.
	bool DrawArmyIcon(ImDrawList* drawList, ImVec2 at, float size, ImU32 color, bool selected, int count, float alpha = 1.0F) {
		const ImVec2 points[] = {ImVec2(at.x - size, at.y - size), ImVec2(at.x + size, at.y - size), ImVec2(at.x + size, at.y + size * 0.3F), ImVec2(at.x, at.y + size * 1.2F), ImVec2(at.x - size, at.y + size * 0.3F)};
		if (selected) {
			// A ring that breathes, so the army picked stands out.
			const float breathe = 1.0F + 0.08F * std::sin(static_cast<float>(ImGui::GetTime()) * 5.0F);
			drawList->AddCircle(at, size * 1.9F * breathe, Alpha(IM_COL32(255, 230, 120, 255), alpha), 24, 2.0F);
		}
		drawList->AddConvexPolyFilled(points, 5, Alpha(color, alpha));
		drawList->AddPolyline(points, 5, Alpha(IM_COL32(10, 10, 20, 255), alpha), ImDrawFlags_Closed, 1.5F);
		if (count > 1) {
			const std::string text = std::to_string(count);
			drawList->AddText(ImVec2(at.x - ImGui::CalcTextSize(text.c_str()).x * 0.5F, at.y - size * 0.9F), Alpha(IM_COL32(10, 10, 20, 255), alpha), text.c_str());
		}
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		return std::abs(mouse.x - at.x) <= size * 1.2F && std::abs(mouse.y - at.y) <= size * 1.3F;
	}

	/// A ring that pulses and an arrow that bobs above it, pointing something out on the maps.
	void Beacon(ImDrawList* drawList, ImVec2 at, float radius);

	/// The shield for one or more armies of one faction on the same spot. The player's own stand out: bigger, with a gold rim, a pip for
	/// each move the readiest of them has left (and a glow while any can still move), and their name, or how many there are, beneath.
	/// Returns whether the mouse is on it.
	bool DrawArmies(ImDrawList* drawList, ImVec2 at, float size, const std::vector<int>& armies, bool labelAbove = false) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		if (armies.empty()) {
			return false;
		}
		const Army& first = game.Armies[armies.front()];
		const bool own = first.Faction == Player();
		bool selected = false;
		bool pointedAt = false;
		bool onRoute = false;
		int moves = 0;
		for (int army: armies) {
			const Army& each = game.Armies[army];
			selected = selected || each.Id == s_Screen.SelectedArmy;
			pointedAt = pointedAt || (s_Screen.RosterHover != 0 && each.Id == s_Screen.RosterHover);
			onRoute = onRoute || !each.Path.empty();
			if (each.Path.empty()) {
				moves = std::max(moves, each.Moves);
			}
		}
		const int count = static_cast<int>(armies.size());
		if (!own) {
			const bool hovered = DrawArmyIcon(drawList, at, size, FactionColor(first.Faction), selected, count);
			if (pointedAt) {
				Beacon(drawList, at, size + 4.0F);
			}
			return hovered;
		}
		size *= 1.3F;
		const float time = static_cast<float>(ImGui::GetTime());
		if (moves > 0) {
			drawList->AddCircleFilled(at, size * (1.9F + 0.15F * std::sin(time * 3.0F)), Alpha(IM_COL32(120, 255, 140, 255), 0.12F), 24);
		}
		// The gold rim: a bigger shield behind.
		const float rim = size + 2.5F;
		const ImVec2 back[] = {ImVec2(at.x - rim, at.y - rim), ImVec2(at.x + rim, at.y - rim), ImVec2(at.x + rim, at.y + rim * 0.3F), ImVec2(at.x, at.y + rim * 1.25F), ImVec2(at.x - rim, at.y + rim * 0.3F)};
		drawList->AddConvexPolyFilled(back, 5, ToolTheme::Gold);
		const bool hovered = DrawArmyIcon(drawList, at, size, FactionColor(first.Faction), selected, count);
		// A star on it, so it reads as yours at a glance.
		if (count <= 1) {
			std::array<ImVec2, 10> star;
			for (int corner = 0; corner < 10; ++corner) {
				const float angle = -c_Pi * 0.5F + static_cast<float>(corner) * c_Pi / 5.0F;
				const float reach = (corner % 2 == 0 ? 0.55F : 0.24F) * size;
				star[corner] = ImVec2(at.x + std::cos(angle) * reach, at.y - size * 0.15F + std::sin(angle) * reach);
			}
			drawList->AddConcavePolyFilled(star.data(), 10, IM_COL32(255, 245, 200, 235));
		}
		// Pips for the moves left.
		const float pipY = at.y + size * 1.25F + 6.0F;
		for (int pip = 0; pip < c_MovesPerTurn; ++pip) {
			const ImVec2 dot(at.x + (static_cast<float>(pip) - static_cast<float>(c_MovesPerTurn - 1) * 0.5F) * 7.0F, pipY);
			drawList->AddCircleFilled(dot, 2.6F, pip < moves ? IM_COL32(120, 255, 140, 255) : IM_COL32(70, 70, 80, 255), 8);
			drawList->AddCircle(dot, 2.6F, IM_COL32(0, 0, 0, 200), 8, 1.0F);
		}
		if (onRoute) {
			// A little arrow: on its way somewhere.
			const float x = at.x + size + 6.0F;
			drawList->AddTriangleFilled(ImVec2(x, at.y - 4.0F), ImVec2(x, at.y + 4.0F), ImVec2(x + 6.0F, at.y), IM_COL32(255, 220, 90, 255));
		}
		const std::string label = count > 1 ? std::to_string(count) + " armies" : first.Name;
		const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
		const ImVec2 textAt(at.x - textSize.x * 0.5F, labelAbove ? at.y - rim - textSize.y - 3.0F : pipY + 4.0F);
		drawList->AddRectFilled(ImVec2(textAt.x - 3.0F, textAt.y - 1.0F), ImVec2(textAt.x + textSize.x + 3.0F, textAt.y + textSize.y), IM_COL32(0, 0, 0, 150), 3.0F);
		drawList->AddText(textAt, selected ? IM_COL32(255, 235, 140, 255) : IM_COL32(255, 255, 255, 235), label.c_str());
		if (pointedAt) {
			Beacon(drawList, at, size + 4.0F);
		}
		return hovered;
	}

	/// The armies on one spot in a row centred on it, a shield for each faction (DrawArmies). Returns the army the mouse is on: the
	/// first of its faction's there, or the next after the one picked, so clicking again goes through them.
	long DrawArmyRow(ImDrawList* drawList, ImVec2 center, float size, const std::vector<int>& armies, bool labelAbove) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		std::vector<std::vector<int>> groups;
		for (int army: armies) {
			auto group = std::find_if(groups.begin(), groups.end(), [&game, army](const std::vector<int>& each) { return game.Armies[each.front()].Faction == game.Armies[army].Faction; });
			if (group == groups.end()) {
				groups.push_back({army});
			} else {
				group->push_back(army);
			}
		}
		const int player = Player();
		auto widthOf = [&game, player, size](const std::vector<int>& group) { return game.Armies[group.front()].Faction == player ? size * 3.6F : size * 2.6F; };
		float total = 0.0F;
		for (const std::vector<int>& group: groups) {
			total += widthOf(group);
		}
		float x = center.x - total * 0.5F;
		long hoveredArmy = 0;
		for (const std::vector<int>& group: groups) {
			const float width = widthOf(group);
			if (DrawArmies(drawList, ImVec2(x + width * 0.5F, center.y), size, group, labelAbove)) {
				auto picked = std::find_if(group.begin(), group.end(), [&game](int army) { return game.Armies[army].Id == s_Screen.SelectedArmy; });
				hoveredArmy = game.Armies[picked == group.end() || picked + 1 == group.end() ? group.front() : *(picked + 1)].Id;
			}
			x += width;
		}
		return hoveredArmy;
	}

	void CenteredText(ImDrawList* drawList, ImVec2 at, ImU32 color, const std::string& text) {
		const ImVec2 size = ImGui::CalcTextSize(text.c_str());
		drawList->AddText(ImVec2(at.x - size.x * 0.5F + 1.0F, at.y + 1.0F), IM_COL32(0, 0, 0, 200), text.c_str());
		drawList->AddText(ImVec2(at.x - size.x * 0.5F, at.y), color, text.c_str());
	}

	/// A starfield and a few clouds of nebula, the same for the same seed, behind the galaxy and system maps.
	void DrawSpace(ImDrawList* drawList, ImVec2 from, ImVec2 to, std::uint32_t seed, ImVec2 drift) {
		drawList->AddRectFilled(from, to, IM_COL32(6, 8, 18, 255));
		std::mt19937 dice(seed * 2654435761u + 17u);
		std::uniform_real_distribution<float> unit(0.0F, 1.0F);
		const float width = to.x - from.x;
		const float height = to.y - from.y;
		for (int cloud = 0; cloud < 10; ++cloud) {
			const ImU32 tint = cloud % 3 == 0 ? IM_COL32(90, 40, 130, 255) : cloud % 3 == 1 ? IM_COL32(30, 70, 140, 255) : IM_COL32(130, 40, 70, 255);
			ImVec2 at(from.x + std::fmod(unit(dice) * width + drift.x * 0.3F + width * 4.0F, width), from.y + std::fmod(unit(dice) * height + drift.y * 0.3F + height * 4.0F, height));
			const float size = (0.15F + unit(dice) * 0.25F) * std::max(width, height);
			for (int ring = 0; ring < 4; ++ring) {
				drawList->AddCircleFilled(at, size * (1.0F - 0.2F * static_cast<float>(ring)), Alpha(tint, 0.025F), 32);
			}
		}
		for (int star = 0; star < 260; ++star) {
			const float depth = 0.2F + unit(dice) * 0.6F;
			ImVec2 at(from.x + std::fmod(unit(dice) * width + drift.x * depth + width * 4.0F, width), from.y + std::fmod(unit(dice) * height + drift.y * depth + height * 4.0F, height));
			drawList->AddRectFilled(at, ImVec2(at.x + (depth > 0.65F ? 2.0F : 1.0F), at.y + (depth > 0.65F ? 2.0F : 1.0F)), Alpha(IM_COL32(220, 225, 255, 255), 0.25F + depth));
		}
	}

	/// A canvas over the rest of the window for a map: an invisible button the size of it, taking every mouse button.
	bool BeginCanvas(const char* id, ImVec2 size, ImVec2& from, ImVec2& to) {
		from = ImGui::GetCursorScreenPos();
		size.x = std::max(size.x, 50.0F);
		size.y = std::max(size.y, 50.0F);
		to = ImVec2(from.x + size.x, from.y + size.y);
		s_Screen.MapFrom = from;
		s_Screen.MapTo = to;
		ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		ImGui::GetWindowDrawList()->PushClipRect(from, to, true);
		return ImGui::IsItemHovered();
	}

	void EndCanvas() { ImGui::GetWindowDrawList()->PopClipRect(); }
#pragma endregion

#pragma region Animations
	Place PlaceOf(const Army& army) { return Place{army.System, army.Planet, army.Point}; }

	float Ease(float t) {
		t = std::clamp(t, 0.0F, 1.0F);
		return t * t * (3.0F - 2.0F * t);
	}

	/// The places an army passes going from one place to another: up into orbit, along the lanes, and down.
	std::vector<Place> Route(const Place& from, const Place& to) {
		std::vector<Place> legs = {from};
		auto add = [&legs](const Place& place) {
			if (legs.back() != place) {
				legs.push_back(place);
			}
		};
		if (from.System != to.System || from.Planet != to.Planet) {
			add(Place{from.System, -1, -1});
			if (from.System != to.System) {
				for (int system: LanePath(s_Screen.Game, from.System, to.System)) {
					add(Place{system, -1, -1});
				}
			}
		}
		add(to);
		return legs;
	}

	/// Whether an army is gliding somewhere: the maps draw it on its way, not where it stands.
	bool IsMoving(long army) {
		return std::any_of(s_Screen.Track.Motions.begin(), s_Screen.Track.Motions.end(), [army](const Motion& motion) { return !motion.Gone && motion.Army == army; });
	}

	/// Whether armies are still moving or a battle is still flashing: the battle report waits for them.
	bool Animating() {
		const Tracking& track = s_Screen.Track;
		return std::any_of(track.Motions.begin(), track.Motions.end(), [](const Motion& motion) { return !motion.Gone; }) ||
		       std::any_of(track.Pulses.begin(), track.Pulses.end(), [](const Pulse& pulse) { return pulse.Kind == PulseKind::Battle; });
	}

	/// Compares the campaign with what the maps showed last frame and starts the animations for what changed: armies moved, new or
	/// gone, points that changed hands, battles fought, a new turn.
	void TrackChanges() {
		CampaignModel::Campaign& game = s_Screen.Game;
		Tracking& track = s_Screen.Track;
		const double now = ImGui::GetTime();
		const int player = Player();
		const int gold = player >= 0 ? game.Factions[player].Gold : 0;
		std::vector<std::vector<int>> owners(game.Planets.size());
		for (size_t planet = 0; planet < game.Planets.size(); ++planet) {
			for (const Point& point: game.Planets[planet].Points) {
				owners[planet].push_back(point.Owner);
			}
		}
		std::unordered_map<long, std::pair<Place, int>> armies;
		for (const Army& army: game.Armies) {
			armies[army.Id] = {PlaceOf(army), army.Faction};
		}
		if (!track.Ready) {
			// A campaign just opened: nothing to animate yet.
			track = Tracking();
			track.Ready = true;
			track.Armies = std::move(armies);
			track.Owners = std::move(owners);
			track.Turn = game.Turn;
			track.Gold = gold;
			track.ShownGold = static_cast<float>(gold);
			game.Battles.clear();
			return;
		}
		for (const auto& [id, current]: armies) {
			const auto was = track.Armies.find(id);
			if (was == track.Armies.end()) {
				track.Pulses.push_back(Pulse{PulseKind::Arrive, current.first, FactionColor(current.second), now});
			} else if (was->second.first != current.first) {
				Motion motion;
				motion.Army = id;
				motion.Faction = current.second;
				motion.Legs = Route(was->second.first, current.first);
				motion.Start = now;
				motion.LegTime = std::min(0.45F, 1.8F / static_cast<float>(std::max<size_t>(motion.Legs.size() - 1, 1)));
				// An army already on its way starts again from where it was headed: near enough, as legs are short.
				track.Motions.erase(std::remove_if(track.Motions.begin(), track.Motions.end(), [id](const Motion& each) { return !each.Gone && each.Army == id; }), track.Motions.end());
				track.Motions.push_back(std::move(motion));
			}
		}
		for (const auto& [id, was]: track.Armies) {
			if (armies.find(id) == armies.end()) {
				Motion motion;
				motion.Army = id;
				motion.Faction = was.second;
				motion.Legs = {was.first};
				motion.Start = now;
				motion.Gone = true;
				track.Motions.erase(std::remove_if(track.Motions.begin(), track.Motions.end(), [id](const Motion& each) { return each.Army == id; }), track.Motions.end());
				track.Motions.push_back(std::move(motion));
			}
		}
		track.Armies = std::move(armies);
		for (size_t planet = 0; planet < owners.size() && planet < track.Owners.size(); ++planet) {
			for (size_t point = 0; point < owners[planet].size() && point < track.Owners[planet].size(); ++point) {
				if (owners[planet][point] != track.Owners[planet][point] && owners[planet][point] >= 0) {
					const Place where{game.Planets[planet].System, static_cast<int>(planet), static_cast<int>(point)};
					track.Pulses.push_back(Pulse{PulseKind::Capture, where, FactionColor(owners[planet][point]), now});
					// One of the player's taken with no battle (a battle has its own report): worth telling them.
					const bool fought = std::any_of(game.Battles.begin(), game.Battles.end(), [&where](const BattleOutcome& battle) { return battle.Planet == where.Planet && battle.Point == where.Point; });
					if (player >= 0 && track.Owners[planet][point] == player && !fought && s_Screen.Alerts.size() < 20) {
						const Planet& lost = game.Planets[planet];
						s_Screen.Alerts.emplace_back(FactionLabel(owners[planet][point]) + " took " + lost.Points[point].Name + " on " + lost.Name + ". Nobody was there to defend it.", where);
					}
				}
			}
		}
		track.Owners = std::move(owners);
		for (const BattleOutcome& battle: game.Battles) {
			if (battle.Planet >= 0 && battle.Planet < static_cast<int>(game.Planets.size())) {
				track.Pulses.push_back(Pulse{PulseKind::Battle, Place{game.Planets[battle.Planet].System, battle.Planet, battle.Point}, FactionColor(battle.Attacker), now});
			}
		}
		game.Battles.clear();
		if (game.Turn != track.Turn) {
			track.Turn = game.Turn;
			track.TurnStarted = now;
			track.TurnGold = gold - track.Gold;
		}
		track.Gold = gold;
		// The gold counter rolls to the new sum.
		const float step = std::min(1.0F, ImGui::GetIO().DeltaTime * 5.0F);
		track.ShownGold += (static_cast<float>(gold) - track.ShownGold) * step;
		if (std::abs(static_cast<float>(gold) - track.ShownGold) < 0.5F) {
			track.ShownGold = static_cast<float>(gold);
		}
		track.Motions.erase(std::remove_if(track.Motions.begin(), track.Motions.end(), [now](const Motion& motion) {
			const double length = motion.Gone ? c_GoneTime : motion.LegTime * static_cast<float>(motion.Legs.size() - 1);
			return now - motion.Start > length;
		}), track.Motions.end());
		track.Pulses.erase(std::remove_if(track.Pulses.begin(), track.Pulses.end(), [now](const Pulse& pulse) { return now - pulse.Start > (pulse.Kind == PulseKind::Battle ? c_BattleTime : c_PulseTime); }), track.Pulses.end());
		const int view = static_cast<int>(s_Screen.Showing);
		if (view != track.LastView || s_Screen.ViewSystem != track.LastSystem || s_Screen.ViewPlanet != track.LastPlanet) {
			track.ViewChanged = now;
			track.LastView = view;
			track.LastSystem = s_Screen.ViewSystem;
			track.LastPlanet = s_Screen.ViewPlanet;
		}
	}

	/// Where a place is on the map being drawn: for an army's shield, or the place itself. False when it's off this map (the
	/// position is then at the map's edge, toward it, for armies to glide in from or out to).
	using Placer = std::function<bool(const Place& place, bool army, ImVec2& at)>;

	/// The armies on their way, and the ones that are gone fading out.
	void DrawMotions(ImDrawList* drawList, const Placer& place, float size) {
		const double now = ImGui::GetTime();
		for (const Motion& motion: s_Screen.Track.Motions) {
			const float elapsed = static_cast<float>(now - motion.Start);
			const ImU32 color = FactionColor(motion.Faction);
			if (motion.Gone) {
				ImVec2 at;
				if (!place(motion.Legs.front(), true, at)) {
					continue;
				}
				const float t = std::clamp(elapsed / c_GoneTime, 0.0F, 1.0F);
				DrawArmyIcon(drawList, ImVec2(at.x, at.y + t * 8.0F), size * (1.0F - 0.4F * t), color, false, 1, 1.0F - t);
				continue;
			}
			const int legs = static_cast<int>(motion.Legs.size()) - 1;
			if (legs < 1) {
				continue;
			}
			const int leg = std::clamp(static_cast<int>(elapsed / motion.LegTime), 0, legs - 1);
			const float t = Ease(elapsed / motion.LegTime - static_cast<float>(leg));
			ImVec2 a;
			ImVec2 b;
			const bool onA = place(motion.Legs[leg], true, a);
			const bool onB = place(motion.Legs[leg + 1], true, b);
			if (!onA && !onB) {
				continue;
			}
			const ImVec2 at(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
			const float alpha = (onA ? 1.0F - t : 0.0F) + (onB ? t : 0.0F);
			drawList->AddLine(a, at, Alpha(color, 0.45F * alpha), 2.0F);
			float shown = size;
			if (motion.Faction == Player()) {
				// The player's own, gold-rimmed as on the maps.
				shown = size * 1.3F;
				const float rim = shown + 2.5F;
				const ImVec2 back[] = {ImVec2(at.x - rim, at.y - rim), ImVec2(at.x + rim, at.y - rim), ImVec2(at.x + rim, at.y + rim * 0.3F), ImVec2(at.x, at.y + rim * 1.25F), ImVec2(at.x - rim, at.y + rim * 0.3F)};
				drawList->AddConvexPolyFilled(back, 5, Alpha(ToolTheme::Gold, std::max(alpha, 0.15F)));
			}
			DrawArmyIcon(drawList, at, shown, color, motion.Army == s_Screen.SelectedArmy, 1, std::max(alpha, 0.15F));
		}
	}

	/// Captures, battles and new armies, flashing where they happened.
	void DrawPulses(ImDrawList* drawList, const Placer& place) {
		const double now = ImGui::GetTime();
		for (const Pulse& pulse: s_Screen.Track.Pulses) {
			ImVec2 at;
			if (!place(pulse.At, false, at)) {
				continue;
			}
			const float elapsed = static_cast<float>(now - pulse.Start);
			switch (pulse.Kind) {
				case PulseKind::Capture: {
					const float t = std::clamp(elapsed / c_PulseTime, 0.0F, 1.0F);
					drawList->AddCircle(at, 8.0F + 34.0F * Ease(t), Alpha(pulse.Color, 1.0F - t), 32, 3.0F);
					if (t > 0.2F) {
						const float later = (t - 0.2F) / 0.8F;
						drawList->AddCircle(at, 8.0F + 22.0F * Ease(later), Alpha(pulse.Color, 0.8F * (1.0F - later)), 32, 2.0F);
					}
					break;
				}
				case PulseKind::Arrive: {
					const float t = std::clamp(elapsed / c_PulseTime, 0.0F, 1.0F);
					drawList->AddCircle(at, 4.0F + 20.0F * Ease(t), Alpha(pulse.Color, 0.9F * (1.0F - t)), 24, 2.0F);
					break;
				}
				case PulseKind::Battle: {
					// A flash, sparks flying out, and a ring of smoke.
					const float t = std::clamp(elapsed / c_BattleTime, 0.0F, 1.0F);
					const float flash = std::max(0.0F, 1.0F - t * 2.5F);
					drawList->AddCircleFilled(at, 10.0F + 14.0F * flash, Alpha(IM_COL32(255, 200, 90, 255), 0.7F * flash), 24);
					for (int spark = 0; spark < 10; ++spark) {
						const float angle = static_cast<float>(spark) * 2.0F * c_Pi / 10.0F + static_cast<float>(pulse.Start);
						const float inner = 6.0F + 30.0F * Ease(t);
						const float outer = inner + 8.0F * (1.0F - t);
						drawList->AddLine(ImVec2(at.x + std::cos(angle) * inner, at.y + std::sin(angle) * inner), ImVec2(at.x + std::cos(angle) * outer, at.y + std::sin(angle) * outer), Alpha(spark % 2 == 0 ? IM_COL32(255, 230, 140, 255) : IM_COL32(255, 120, 50, 255), 1.0F - t), 2.0F);
					}
					drawList->AddCircle(at, 12.0F + 26.0F * Ease(t), Alpha(IM_COL32(200, 200, 200, 255), 0.5F * (1.0F - t)), 32, 4.0F * (1.0F - t) + 1.0F);
					// Two blades crossing.
					const float cross = 9.0F * std::min(1.0F, t * 4.0F);
					const ImU32 blade = Alpha(IM_COL32(240, 240, 250, 255), 1.0F - t);
					drawList->AddLine(ImVec2(at.x - cross, at.y - cross), ImVec2(at.x + cross, at.y + cross), blade, 2.5F);
					drawList->AddLine(ImVec2(at.x + cross, at.y - cross), ImVec2(at.x - cross, at.y + cross), blade, 2.5F);
					break;
				}
			}
		}
	}

	/// A ring that pulses and an arrow that bobs above it: what the guide is pointing at.
	void Beacon(ImDrawList* drawList, ImVec2 at, float radius) {
		const float time = static_cast<float>(ImGui::GetTime());
		const float swell = 0.5F + 0.5F * std::sin(time * 4.0F);
		const ImU32 gold = ToolTheme::Gold;
		drawList->AddCircle(at, radius + 4.0F + 4.0F * swell, Alpha(gold, 0.5F + 0.5F * swell), 32, 2.5F);
		const float bob = 5.0F * std::sin(time * 5.0F);
		const float top = at.y - radius - 26.0F + bob;
		drawList->AddTriangleFilled(ImVec2(at.x - 8.0F, top), ImVec2(at.x + 8.0F, top), ImVec2(at.x, top + 11.0F), gold);
		drawList->AddRectFilled(ImVec2(at.x - 3.0F, top - 9.0F), ImVec2(at.x + 3.0F, top), gold);
	}

	/// A pulsing frame round the widget just drawn, when the guide points at it.
	void BeaconItem(bool on) {
		if (!on) {
			return;
		}
		const float swell = 0.5F + 0.5F * std::sin(static_cast<float>(ImGui::GetTime()) * 4.0F);
		const ImVec2 from = ImGui::GetItemRectMin();
		const ImVec2 to = ImGui::GetItemRectMax();
		const float grow = 2.0F + 3.0F * swell;
		ImGui::GetWindowDrawList()->AddRect(ImVec2(from.x - grow, from.y - grow), ImVec2(to.x + grow, to.y + grow), Alpha(ToolTheme::Gold, 0.5F + 0.5F * swell), 4.0F, 0, 2.5F);
	}

	/// Over the map just drawn: a fade in after the view changes, and the new turn's banner.
	void MapOverlay() {
		const Tracking& track = s_Screen.Track;
		const double now = ImGui::GetTime();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 from = s_Screen.MapFrom;
		const ImVec2 to = s_Screen.MapTo;
		drawList->PushClipRect(from, to, true);
		if (const float fade = 1.0F - static_cast<float>(now - track.ViewChanged) / c_ViewFadeTime; fade > 0.0F) {
			drawList->AddRectFilled(from, to, Alpha(IM_COL32(6, 8, 18, 255), fade));
		}
		if (const float age = static_cast<float>(now - track.TurnStarted); age < c_TurnBannerTime) {
			const float alpha = std::min({1.0F, age / 0.25F, (c_TurnBannerTime - age) / 0.5F});
			const float slide = (1.0F - Ease(age / 0.35F)) * -20.0F;
			ImFont* font = ImGui::GetFont();
			const float big = ImGui::GetFontSize() * 2.0F;
			const std::string title = "Turn " + std::to_string(track.Turn);
			const ImVec2 titleSize = font->CalcTextSizeA(big, FLT_MAX, 0.0F, title.c_str());
			const std::string gold = (track.TurnGold >= 0 ? "+" : "") + std::to_string(track.TurnGold) + " gold";
			const ImVec2 goldSize = ImGui::CalcTextSize(gold.c_str());
			const float middle = (from.x + to.x) * 0.5F;
			const float top = from.y + (to.y - from.y) * 0.18F + slide;
			const float halfWidth = std::max(titleSize.x, goldSize.x) * 0.5F + 40.0F;
			drawList->AddRectFilledMultiColor(ImVec2(middle - halfWidth, top - 8.0F), ImVec2(middle, top + titleSize.y + goldSize.y + 12.0F), Alpha(IM_COL32(0, 0, 0, 255), 0.0F), Alpha(IM_COL32(0, 0, 0, 255), 0.6F * alpha), Alpha(IM_COL32(0, 0, 0, 255), 0.6F * alpha), Alpha(IM_COL32(0, 0, 0, 255), 0.0F));
			drawList->AddRectFilledMultiColor(ImVec2(middle, top - 8.0F), ImVec2(middle + halfWidth, top + titleSize.y + goldSize.y + 12.0F), Alpha(IM_COL32(0, 0, 0, 255), 0.6F * alpha), Alpha(IM_COL32(0, 0, 0, 255), 0.0F), Alpha(IM_COL32(0, 0, 0, 255), 0.0F), Alpha(IM_COL32(0, 0, 0, 255), 0.6F * alpha));
			drawList->AddText(font, big, ImVec2(middle - titleSize.x * 0.5F, top), Alpha(ToolTheme::Gold, alpha), title.c_str());
			drawList->AddText(ImVec2(middle - goldSize.x * 0.5F, top + titleSize.y + 2.0F), Alpha(track.TurnGold >= 0 ? IM_COL32(130, 255, 130, 255) : IM_COL32(255, 130, 110, 255), alpha), gold.c_str());
		}
		drawList->PopClipRect();
	}
#pragma endregion

#pragma region Orders from the maps
	/// Has the view go to where an army ends up once it's moved there (after its glide), so a take-off shows the system and a landing
	/// the planet. Call before giving the order.
	void FollowNext(int army) {
		s_Screen.FollowArmy = s_Screen.Game.Armies[army].Id;
		s_Screen.FollowFrom = PlaceOf(s_Screen.Game.Armies[army]);
	}
	/// Opens the attack window for a move that's a battle, or makes the move at once when it isn't.
	void TryEnter(int army, int planet, int point, bool overland) {
		float defence = 0.0F;
		// A battle, or a move that breaks a treaty, asks first.
		if (WouldFight(s_Screen.Game, s_Screen.Game.Armies[army].Faction, planet, point, defence) || TreatyBroken(s_Screen.Game, s_Screen.Game.Armies[army].Faction, planet, point) >= 0) {
			s_Screen.Attack = PendingAttack{true, s_Screen.Game.Armies[army].Id, planet, point, overland};
			return;
		}
		FollowNext(army);
		const bool done = overland ? OrderOverland(s_Screen.Game, army, point) : OrderLand(s_Screen.Game, army, planet, point);
		if (!done) {
			s_Screen.Note = "That army has no moves left this turn.";
		}
	}

	/// A right-click on a point with an army picked: overland from a linked point, or landing from orbit.
	void OrderToPoint(int planet, int point) {
		const int army = OwnSelectedArmy();
		if (army < 0) {
			s_Screen.Note = "Pick one of your armies first, then right-click where it should go.";
			return;
		}
		const Army& self = s_Screen.Game.Armies[army];
		const Planet& target = s_Screen.Game.Planets[planet];
		if (self.Planet == planet && self.Point == point) {
			return;
		}
		if (self.Planet == planet) {
			const std::vector<int>& links = target.Points[self.Point].Links;
			if (std::find(links.begin(), links.end(), point) == links.end()) {
				s_Screen.Note = "Overland, an army only reaches the points joined to where it stands. Take off and land instead.";
				return;
			}
			if (self.Moves < 1) {
				s_Screen.Note = "That army has no moves left this turn.";
				return;
			}
			TryEnter(army, planet, point, true);
		} else if (self.Planet < 0 && self.System == target.System) {
			if (self.Moves < 1) {
				s_Screen.Note = "That army has no moves left this turn.";
				return;
			}
			TryEnter(army, planet, point, false);
		} else if (self.System == target.System) {
			s_Screen.Note = "Take off into orbit first (Take off, in the army's panel), then land here.";
		} else {
			s_Screen.Note = "Move the army to this system first: right-click the star on the galaxy map.";
		}
	}

	void OrderToSystem(int system) {
		const int army = OwnSelectedArmy();
		if (army < 0) {
			s_Screen.Note = "Pick one of your armies first, then right-click where it should go.";
			return;
		}
		if (s_Screen.Game.Armies[army].System == system) {
			return;
		}
		FollowNext(army);
		if (!OrderMove(s_Screen.Game, army, system)) {
			s_Screen.Note = "No lane leads there.";
		} else if (!s_Screen.Game.Armies[army].Path.empty()) {
			s_Screen.Note = s_Screen.Game.Armies[army].Name + " will carry on next turn (" + std::to_string(s_Screen.Game.Armies[army].Path.size()) + " jumps to go).";
		}
	}
#pragma endregion


#pragma region The guide
	constexpr const char* c_GuideFile = "Userdata/Campaigns/Guide.ini";

	/// Whether the player has been through the guide (or turned it off) before: new campaigns then start without it.
	bool GuideFinished() {
		std::ifstream in(c_GuideFile);
		std::string word;
		in >> word;
		return word == "Finished";
	}

	void MarkGuideFinished() {
		std::error_code code;
		std::filesystem::create_directories("Userdata/Campaigns", code);
		std::ofstream out(c_GuideFile);
		out << "Finished\n";
	}

	int OwnedPoints(int faction) {
		int count = 0;
		for (const Planet& planet: s_Screen.Game.Planets) {
			for (const Point& point: planet.Points) {
				count += point.Owner == faction ? 1 : 0;
			}
		}
		return count;
	}

	int OwnSquads(int faction) {
		int count = 0;
		for (const Army& army: s_Screen.Game.Armies) {
			count += army.Faction == faction ? static_cast<int>(army.Squads.size()) : 0;
		}
		return count;
	}

	int ControlledPlanets(int faction) {
		int count = 0;
		for (int planet = 0; planet < static_cast<int>(s_Screen.Game.Planets.size()); ++planet) {
			count += ControlOf(s_Screen.Game, planet).Controller == faction ? 1 : 0;
		}
		return count;
	}

	/// Settlement levels and buildings on a faction's points, added up.
	int Development(int faction) {
		int count = 0;
		for (const Planet& planet: s_Screen.Game.Planets) {
			for (const Point& point: planet.Points) {
				if (point.Owner == faction) {
					count += point.Settlement + static_cast<int>(std::count(point.Built.begin(), point.Built.end(), true));
				}
			}
		}
		return count;
	}

	int HomePlanet() {
		const int player = Player();
		const int home = player >= 0 ? s_Screen.Game.Factions[player].HomePlanet : -1;
		return home >= 0 && home < static_cast<int>(s_Screen.Game.Planets.size()) ? home : -1;
	}

	/// Whether a part of the screen is shown: always without the guide, and with it once the guide has got to the step that introduces
	/// it, so a new player isn't shown everything at once.
	bool Shown(Step introducedAt) { return !s_Screen.Guide.On || s_Screen.Guide.At >= introducedAt; }

	/// Read and moved on from with a button, not done by playing. The battle step turns into one once the player has fought: it then
	/// explains the battle they just had.
	bool ManualStep(Step step) { return step == Step::Welcome || step == Step::Resources || step == Step::Control || step == Step::Routine || step == Step::Win || (step == Step::Battle && s_Screen.Guide.Fought); }

	/// A planet worth taking first: the nearest along the lanes from the player's home with points the player doesn't hold, free ones
	/// before enemy ones.
	int FindTarget() {
		const CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		const int home = HomePlanet();
		if (home < 0) {
			return -1;
		}
		std::vector<int> distance(game.Systems.size(), -1);
		std::vector<int> queue = {game.Planets[home].System};
		distance[queue.front()] = 0;
		int best = -1;
		int bestScore = 1 << 30;
		for (size_t next = 0; next < queue.size(); ++next) {
			const int system = queue[next];
			for (int planet: game.Systems[system].Planets) {
				int free = 0;
				int enemy = 0;
				for (const Point& point: game.Planets[planet].Points) {
					free += point.Owner < 0 ? 1 : 0;
					enemy += point.Owner >= 0 && point.Owner != player ? 1 : 0;
				}
				if (free + enemy == 0) {
					continue;
				}
				const int score = distance[system] * 10 + (free > 0 ? 0 : 5);
				if (score < bestScore) {
					bestScore = score;
					best = planet;
				}
			}
			for (int lane: game.Systems[system].Lanes) {
				if (distance[lane] < 0) {
					distance[lane] = distance[system] + 1;
					queue.push_back(lane);
				}
			}
		}
		return best;
	}

	/// Whether one of the player's armies is in a system, and (with moves to land) ready to land there now.
	bool ArmyIn(int system, bool readyToLand) {
		const int player = Player();
		for (const Army& army: s_Screen.Game.Armies) {
			if (army.Faction == player && army.System == system && (!readyToLand || (army.Planet < 0 && army.Moves >= 1))) {
				return true;
			}
		}
		return false;
	}

	/// Steps that need doing only sometimes: travelling when the target is in another system, ending the turn when the army can't
	/// land yet. Checked as the step starts.
	bool SkipStep(Step step) {
		const GuideState& guide = s_Screen.Guide;
		const CampaignModel::Campaign& game = s_Screen.Game;
		const int targetSystem = guide.Target >= 0 ? game.Planets[guide.Target].System : -1;
		switch (step) {
			case Step::Travel:
				return targetSystem < 0 || ArmyIn(targetSystem, false);
			case Step::EndTurn:
				return targetSystem < 0 || ArmyIn(targetSystem, true);
			default:
				return false;
		}
	}

	void StartStep(Step step) {
		GuideState& guide = s_Screen.Guide;
		const int player = Player();
		guide.At = step;
		guide.DoneAt = -1.0;
		guide.BaseTurn = s_Screen.Game.Turn;
		guide.Base = 0;
		switch (step) {
			case Step::Recruit:
				guide.Base = OwnSquads(player);
				break;
			case Step::Travel:
				guide.Target = FindTarget();
				break;
			case Step::Land:
				guide.Base = OwnedPoints(player);
				break;
			case Step::Control:
				guide.Base = ControlledPlanets(player);
				break;
			case Step::Grow:
				guide.Base = Development(player);
				break;
			case Step::Trade:
				guide.Traded = false;
				break;
			case Step::Diplomacy:
				guide.Proposed = false;
				break;
			default:
				break;
		}
		if (step != Step::Count && SkipStep(step)) {
			StartStep(static_cast<Step>(static_cast<int>(step) + 1));
		}
	}

	void StartGuide() {
		s_Screen.Guide.On = true;
		s_Screen.Guide.Folded = false;
		s_Screen.Guide.Target = -1;
		s_Screen.Guide.Fought = s_Screen.Guide.Traded = s_Screen.Guide.Proposed = false;
		StartStep(Step::Welcome);
	}

	void StopGuide() {
		s_Screen.Guide.On = false;
		MarkGuideFinished();
	}

	bool StepDone(Step step) {
		const GuideState& guide = s_Screen.Guide;
		const CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		const int targetSystem = guide.Target >= 0 ? game.Planets[guide.Target].System : -1;
		switch (step) {
			case Step::HomePlanet:
				return s_Screen.Showing == View::Planet && s_Screen.ViewPlanet == HomePlanet();
			case Step::ReadPoint:
				return s_Screen.Showing == View::Planet && s_Screen.SelectedPoint >= 0 && s_Screen.SelectedPlanet >= 0 && s_Screen.SelectedPlanet < static_cast<int>(game.Planets.size()) && s_Screen.SelectedPoint < static_cast<int>(game.Planets[s_Screen.SelectedPlanet].Points.size()) && game.Planets[s_Screen.SelectedPlanet].Points[s_Screen.SelectedPoint].Owner == player;
			case Step::Battle:
				return !game.Reports.empty();
			case Step::Trade:
				return guide.Traded;
			case Step::Diplomacy:
				return guide.Proposed;
			case Step::Recruit:
				return OwnSquads(player) > guide.Base;
			case Step::TakeOff:
				return std::any_of(game.Armies.begin(), game.Armies.end(), [player](const Army& army) { return army.Faction == player && army.Planet < 0; });
			case Step::Travel:
				return ArmyIn(targetSystem, false) || std::any_of(game.Armies.begin(), game.Armies.end(), [player, targetSystem](const Army& army) { return army.Faction == player && !army.Path.empty() && army.Path.back() == targetSystem; });
			case Step::EndTurn:
				return targetSystem >= 0 ? ArmyIn(targetSystem, true) : game.Turn > guide.BaseTurn;
			case Step::Land:
				return OwnedPoints(player) > guide.Base;
			case Step::Control:
				return ControlledPlanets(player) > guide.Base;
			case Step::Grow:
				return Development(player) > guide.Base;
			default:
				return false; // Read and moved on from with Next.
		}
	}

	/// Moves the guide on when a step is done, and sets what it points at this frame.
	void UpdateGuide() {
		Highlights& lights = s_Screen.Lights;
		lights = Highlights();
		GuideState& guide = s_Screen.Guide;
		const CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		if (!guide.On || player < 0 || game.Winner >= 0 || game.Lost) {
			return;
		}
		const double now = ImGui::GetTime();
		if (guide.At == Step::Count) {
			StopGuide();
			return;
		}
		guide.Fought = guide.Fought || !game.Reports.empty();
		if (guide.DoneAt < 0.0 && !ManualStep(guide.At) && StepDone(guide.At)) {
			guide.DoneAt = now;
		}
		if (guide.DoneAt >= 0.0) {
			if (now - guide.DoneAt > 1.2) {
				StartStep(static_cast<Step>(static_cast<int>(guide.At) + 1));
			}
			return;
		}
		const int home = HomePlanet();
		const int target = guide.Target;
		// Points the way to a planet: its star on the galaxy map, the planet in its system, or the Galaxy button from elsewhere.
		auto pointToPlanet = [&](int planet) {
			if (planet < 0) {
				return;
			}
			const int system = game.Planets[planet].System;
			if (s_Screen.Showing == View::Galaxy) {
				lights.System = system;
			} else if (s_Screen.ViewSystem == system && s_Screen.Showing == View::System) {
				lights.Planet = planet;
			} else if (s_Screen.ViewSystem == system) {
				lights.Planet = planet; // Shown once Esc goes back to the system.
			} else {
				lights.Galaxy = true;
			}
		};
		auto pointsOnShownPlanet = [&](auto&& want) {
			if (s_Screen.Showing != View::Planet || s_Screen.ViewPlanet < 0) {
				return;
			}
			const Planet& planet = game.Planets[s_Screen.ViewPlanet];
			for (int point = 0; point < static_cast<int>(planet.Points.size()); ++point) {
				if (want(planet.Points[point])) {
					lights.Points.push_back(point);
				}
			}
		};
		switch (guide.At) {
			case Step::HomePlanet:
				pointToPlanet(home);
				break;
			case Step::Recruit:
				if (s_Screen.Showing == View::Planet) {
					pointsOnShownPlanet([player](const Point& point) { return point.Owner == player && point.Settlement > 0; });
				} else {
					pointToPlanet(home);
				}
				break;
			case Step::TakeOff:
				lights.TakeOff = true;
				break;
			case Step::Travel:
				if (s_Screen.Showing == View::Galaxy) {
					lights.System = target >= 0 ? game.Planets[target].System : -1;
				} else {
					lights.Galaxy = true;
				}
				break;
			case Step::EndTurn:
				lights.EndTurn = true;
				break;
			case Step::Land:
				if (target >= 0 && s_Screen.Showing == View::Planet && s_Screen.ViewPlanet == target) {
					pointsOnShownPlanet([player](const Point& point) { return point.Owner != player; });
				} else {
					pointToPlanet(target);
				}
				break;
			case Step::ReadPoint:
				if (s_Screen.Showing == View::Planet && s_Screen.ViewPlanet == home) {
					pointsOnShownPlanet([player](const Point& point) { return point.Owner == player; });
				} else {
					pointToPlanet(home);
				}
				break;
			case Step::Resources:
				lights.Resources = true;
				break;
			case Step::Battle:
				if (!guide.Fought) {
					if (s_Screen.Showing == View::Planet && s_Screen.ViewPlanet >= 0) {
						const int shown = s_Screen.ViewPlanet;
						for (int point = 0; point < static_cast<int>(game.Planets[shown].Points.size()); ++point) {
							float defence = 0.0F;
							if (game.Planets[shown].Points[point].Owner != player && WouldFight(game, player, shown, point, defence)) {
								lights.Points.push_back(point);
							}
						}
					} else {
						pointToPlanet(target);
					}
				}
				break;
			case Step::Grow:
				pointsOnShownPlanet([player](const Point& point) { return point.Owner == player; });
				break;
			case Step::Trade:
				lights.Trade = true;
				break;
			case Step::Diplomacy:
				lights.Diplomacy = true;
				break;
			case Step::Routine:
				lights.ToDo = true;
				break;
			default:
				break;
		}
	}

	struct StepText {
		std::string Title;
		std::string Text;
	};

	StepText TextOf(Step step) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		const int home = HomePlanet();
		const std::string homeName = home >= 0 ? game.Planets[home].Name : std::string("your home planet");
		const int target = s_Screen.Guide.Target;
		const std::string targetName = target >= 0 ? game.Planets[target].Name : std::string("a planet nearby");
		const std::string targetSystem = target >= 0 ? game.Systems[game.Planets[target].System].Name : std::string("a star nearby");
		switch (step) {
			case Step::Welcome:
				return {"Welcome, commander", "You lead " + (player >= 0 ? game.Factions[player].Name : std::string("your faction")) + ". This guide plays your first turns with you, one small step at a time, and the rest of the screen appears as it gets there.\n\nYour armies are the gold-rimmed shields with a star. Each green pip is a move left this turn."};
			case Step::HomePlanet:
				return {"Look at your home planet", "Double-click " + homeName + " to open its map.\n\nA planet has 3 or 5 points. Hold more than half of them to control it. All of " + homeName + "'s are yours."};
			case Step::ReadPoint:
				return {"Read a point", "Click one of your points. The panel on the right shows who holds it, the gold it pays each turn and what it produces.\n\nThe small icon by each point is its resource, with how much a turn."};
			case Step::Resources:
				return {"Your resources", "Your stores are now under your gold: water, food, metal, fuel, crystal and relics, each with its change a turn.\n\nArmies eat food, and burn fuel to jump between stars. Building takes metal and water. Hover one to see what it's for. One blinking red is running out."};
			case Step::Recruit:
				return {"Recruit a squad", "Click a settlement (a point with houses) and pick a squad under Recruit on the right. It joins the army there.\n\nEach button shows its cost. Every soldier costs upkeep each turn."};
			case Step::TakeOff:
				return {"Take off", "Pick your army and press Take off in its panel. It lifts into orbit, ready to travel.\n\nAn army has 2 moves a turn: taking off, landing and marching take 1, a jump between stars takes 2."};
			case Step::Travel:
				return {"Travel to " + targetSystem, targetName + " in the " + targetSystem + " system has points to take. Open the Galaxy map and right-click " + targetSystem + " to send your army along the lanes."};
			case Step::EndTurn:
				return {"End the turn", "Press End turn (or Enter). The other factions move, you collect gold and resources, and your army carries on along its route.\n\nKeep ending turns until it is in orbit at " + targetSystem + " with a move left to land."};
			case Step::Land:
				return {"Land on " + targetName, "Open " + targetName + "'s map (double-click it in its system) and right-click a point that isn't yours to land there.\n\nGrey points with nobody on them are free to take."};
			case Step::Battle:
				if (s_Screen.Guide.Fought) {
					return {"Your first battle", "Before a fight, the attack window shows both sides' strength and your chance of winning. After it, the battle report shows who won and the losses, and Take me there shows where.\n\nWounded soldiers heal each turn, fastest at your settlements."};
				}
				return {"Fight a battle", "Points marked (locals), or held by another faction, fight back. Right-click one (they're pointed out on the planet map): the attack window shows your chance of winning before you commit. Go for 60% or better.\n\nAfterwards the battle report tells you how it went."};
			case Step::Control:
				return {"Control a planet", "Keep taking points: hold more than half of a planet's points to control it, which pays a bonus each turn.\n\nWhile an enemy holds any of its points, the planet is Invaded: half income and slower healing until you drive them out."};
			case Step::Grow:
				return {"Build up", "Click a point you hold and found an outpost on it, then grow it into a colony and a city for more gold and more building slots.\n\nBarracks unlock better squads, a Market pays more, Walls help hold it and a Hospital heals."};
			case Step::Trade:
				return {"Trade", "Short of something? The galaxy market sells everything and buys what you have spare. Press Trade at the top and buy one of anything (food, say).\n\nPrices rise as you buy and drift back each turn."};
			case Step::Diplomacy:
				return {"Diplomacy", "Every faction starts Neutral: either side can attack, but it sours relations. Press Diplomacy at the top to see where you stand with each one, then ask one for peace. If they won't, they say why.\n\nA gift (a deal where you only give) raises their opinion. Allies fight beside you and win with you."};
			case Step::Routine:
				return {"Each turn from here", "That's everything. Each turn: check the To do list on the right (most pressing first; click a line to go there), press Tab to pick each army with moves left, spend your gold, then End turn.\n\nMilitary (or M) lists all your armies: what each is made of and which have moved."};
			case Step::Win:
				return {"Conquer the galaxy", "Wipe out the other factions to win, or outlast them with your allies.\n\nGood luck, commander."};
			default:
				return {"", ""};
		}
	}

	/// The guide's parts: each a few steps, played as a turn or two of the game.
	struct Part {
		const char* Name;
		Step First;
		Step Last;
	};
	constexpr Part c_Parts[] = {
	    {"Your empire", Step::Welcome, Step::Recruit},
	    {"Your first move", Step::TakeOff, Step::EndTurn},
	    {"Taking ground", Step::Land, Step::Control},
	    {"Running an empire", Step::Grow, Step::Diplomacy},
	    {"Each turn from here", Step::Routine, Step::Win},
	};

	int PartOf(Step step) {
		for (int part = 0; part < static_cast<int>(std::size(c_Parts)); ++part) {
			if (step <= c_Parts[part].Last) {
				return part;
			}
		}
		return static_cast<int>(std::size(c_Parts)) - 1;
	}


	/// The guide's box, over the top left of the map.
	void GuideBox() {
		GuideState& guide = s_Screen.Guide;
		if (!guide.On || guide.At == Step::Count || s_Screen.Game.Winner >= 0 || s_Screen.Game.Lost) {
			return;
		}
		const float width = std::min(ImGui::GetFontSize() * 21.0F, s_Screen.MapTo.x - s_Screen.MapFrom.x - 20.0F);
		ImGui::SetCursorScreenPos(ImVec2(s_Screen.MapFrom.x + 10.0F, s_Screen.MapFrom.y + 10.0F));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04F, 0.05F, 0.11F, 0.93F));
		ImGui::PushStyleColor(ImGuiCol_Border, ToolTheme::Vec(ToolTheme::Gold, 0.6F));
		const StepText text = TextOf(guide.At);
		const int number = static_cast<int>(guide.At) + 1;
		const int count = static_cast<int>(Step::Count);
		if (ImGui::BeginChild("##guide", ImVec2(guide.Folded ? 0.0F : width, 0.0F), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | (guide.Folded ? ImGuiChildFlags_AutoResizeX : 0), ImGuiWindowFlags_NoScrollbar)) {
			if (guide.Folded) {
				const std::string label = "Guide, part " + std::to_string(PartOf(guide.At) + 1) + ": " + text.Title;
				if (ToolUI::SmallButton(label.c_str())) {
					guide.Folded = false;
				}
				ImGui::SetItemTooltip("Opens the guide again.");
			} else {
				const int part = PartOf(guide.At);
				ImGui::TextDisabled("Part %d of %d: %s", part + 1, static_cast<int>(std::size(c_Parts)), c_Parts[part].Name);
				ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Fold").x - ImGui::GetStyle().FramePadding.x * 2.0F);
				if (ToolUI::SmallButton("Fold")) {
					guide.Folded = true;
				}
				// Progress through the steps.
				const ImVec2 at = ImGui::GetCursorScreenPos();
				const float barWidth = ImGui::GetContentRegionAvail().x;
				ImDrawList* drawList = ImGui::GetWindowDrawList();
				drawList->AddRectFilled(at, ImVec2(at.x + barWidth, at.y + 3.0F), IM_COL32(60, 60, 80, 255), 2.0F);
				const float done = (static_cast<float>(number - 1) + (guide.DoneAt >= 0.0 ? 1.0F : 0.0F)) / static_cast<float>(count);
				drawList->AddRectFilled(at, ImVec2(at.x + barWidth * done, at.y + 3.0F), ToolTheme::Gold, 2.0F);
				ImGui::Dummy(ImVec2(barWidth, 5.0F));
				ImGui::SetWindowFontScale(1.15F);
				ImGui::TextColored(ToolTheme::Vec(ToolTheme::Gold), "%s", text.Title.c_str());
				ImGui::SetWindowFontScale(1.0F);
				ImGui::PushTextWrapPos(0.0F);
				ImGui::TextUnformatted(text.Text.c_str());
				ImGui::PopTextWrapPos();
				ImGui::Spacing();
				const bool manual = ManualStep(guide.At);
				if (guide.DoneAt >= 0.0) {
					ImGui::TextColored(ImVec4(0.5F, 1.0F, 0.5F, 1.0F), "Done!");
				} else if (manual) {
					const char* next = guide.At == Step::Win ? "Finish" : guide.At == Step::Welcome ? "Start" : "Next";
					if (ToolUI::Button(next, ImVec2(ImGui::GetFontSize() * 5.0F, 0.0F))) {
						StartStep(static_cast<Step>(number));
						if (guide.At == Step::Count) {
							StopGuide();
						}
					}
					ImGui::SameLine();
				}
				if (guide.DoneAt < 0.0 && !manual) {
					if (ToolUI::SmallButton("Skip this step")) {
						StartStep(static_cast<Step>(number));
					}
					ImGui::SameLine();
				}
				if (guide.DoneAt < 0.0 && ToolUI::SmallButton("Turn the guide off")) {
					StopGuide();
				}
				if (guide.DoneAt < 0.0) {
					ImGui::SetItemTooltip("The Guide button at the top brings it back.");
				}
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(2);
	}
#pragma endregion

#pragma region The galaxy map
	/// The faction holding most of a system's points, -1 when nobody holds any or it's a tie.
	int SystemOwner(int system) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		std::vector<int> owned(game.Factions.size(), 0);
		for (int planet: game.Systems[system].Planets) {
			for (const Point& point: game.Planets[planet].Points) {
				if (point.Owner >= 0) {
					++owned[point.Owner];
				}
			}
		}
		int best = -1;
		int most = 0;
		bool tie = false;
		for (int faction = 0; faction < static_cast<int>(owned.size()); ++faction) {
			if (owned[faction] > most) {
				best = faction;
				most = owned[faction];
				tie = false;
			} else if (owned[faction] == most && most > 0) {
				tie = true;
			}
		}
		return tie ? -1 : best;
	}

	/// A key to the factions' colours in a corner of the map: each faction still in it, its planets and points, the player's marked.
	void DrawLegend(ImDrawList* drawList, ImVec2 corner) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		struct Row {
			int Faction;
			int Planets;
			int Points;
		};
		std::vector<Row> rows;
		for (int faction = 0; faction < static_cast<int>(game.Factions.size()); ++faction) {
			if (game.Factions[faction].Alive) {
				rows.push_back({faction, ControlledPlanets(faction), OwnedPoints(faction)});
			}
		}
		std::sort(rows.begin(), rows.end(), [player](const Row& a, const Row& b) { return (a.Faction == player) != (b.Faction == player) ? a.Faction == player : a.Points > b.Points; });
		const float line = ImGui::GetTextLineHeightWithSpacing();
		float width = 0.0F;
		std::vector<std::string> texts;
		for (const Row& row: rows) {
			const Stance stance = player >= 0 ? StanceOf(game, player, row.Faction) : Stance::Neutral;
			const char* tag = row.Faction == player ? " (you)" : stance == Stance::Alliance ? " (ally)" : stance == Stance::Peace ? " (at peace)" : stance == Stance::War ? " (at war)" : "";
			texts.push_back(FactionLabel(row.Faction) + tag + ": " + std::to_string(row.Planets) + (row.Planets == 1 ? " planet, " : " planets, ") + std::to_string(row.Points) + " points");
			width = std::max(width, ImGui::CalcTextSize(texts.back().c_str()).x);
		}
		const ImVec2 from(corner.x, corner.y - line * static_cast<float>(rows.size()) - 12.0F);
		drawList->AddRectFilled(from, ImVec2(from.x + width + line + 14.0F, corner.y), IM_COL32(0, 0, 10, 170), 4.0F);
		for (size_t index = 0; index < rows.size(); ++index) {
			const ImVec2 at(from.x + 6.0F, from.y + 6.0F + line * static_cast<float>(index));
			const ImU32 color = FactionColor(rows[index].Faction);
			drawList->AddRectFilled(ImVec2(at.x, at.y + 2.0F), ImVec2(at.x + line - 6.0F, at.y + line - 4.0F), color, 2.0F);
			drawList->AddText(ImVec2(at.x + line, at.y), rows[index].Faction == player ? ToolTheme::Gold : IM_COL32(230, 232, 245, 255), texts[index].c_str());
		}
	}
	void GalaxyMap(ImVec2 size) {
		CampaignModel::Campaign& game = s_Screen.Game;
		ImVec2 from;
		ImVec2 to;
		const bool hovered = BeginCanvas("##galaxy", size, from, to);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImGuiIO& io = ImGui::GetIO();
		const float span = std::min(to.x - from.x, to.y - from.y) * 1.05F * s_Screen.GalaxyZoom;
		const ImVec2 middle((from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F);
		auto toScreen = [&](float x, float y) { return ImVec2(middle.x + (x - 0.5F - s_Screen.GalaxyPan.x) * span * 1.35F, middle.y + (y - 0.5F - s_Screen.GalaxyPan.y) * span); };
		DrawSpace(drawList, from, to, game.Seed, ImVec2(-s_Screen.GalaxyPan.x * span * 0.5F, -s_Screen.GalaxyPan.y * span * 0.5F));

		if (hovered) {
			if (io.MouseWheel != 0.0F) {
				s_Screen.GalaxyZoom = std::clamp(s_Screen.GalaxyZoom * (io.MouseWheel > 0.0F ? 1.15F : 1.0F / 1.15F), 0.6F, 4.0F);
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0F)) {
				s_Screen.GalaxyPan.x -= io.MouseDelta.x / (span * 1.35F);
				s_Screen.GalaxyPan.y -= io.MouseDelta.y / span;
			}
		}

		// Territory: a wash of colour round each star, in the colour of whoever holds most of its points.
		std::vector<int> systemOwners(game.Systems.size(), -1);
		for (int index = 0; index < static_cast<int>(game.Systems.size()); ++index) {
			systemOwners[index] = SystemOwner(index);
			if (systemOwners[index] >= 0) {
				const ImVec2 at = toScreen(game.Systems[index].X, game.Systems[index].Y);
				const float reach = span * 0.075F;
				for (int ring = 0; ring < 4; ++ring) {
					drawList->AddCircleFilled(at, reach * (1.0F - 0.2F * static_cast<float>(ring)), Alpha(FactionColor(systemOwners[index]), 0.06F), 40);
				}
			}
		}
		// Lanes, and the selected army's route.
		for (int a = 0; a < static_cast<int>(game.Systems.size()); ++a) {
			for (int b: game.Systems[a].Lanes) {
				if (b > a) {
					drawList->AddLine(toScreen(game.Systems[a].X, game.Systems[a].Y), toScreen(game.Systems[b].X, game.Systems[b].Y), IM_COL32(110, 130, 190, 90), 1.5F);
				}
			}
		}
		if (const int army = SelectedArmyIndex(); army >= 0 && !game.Armies[army].Path.empty()) {
			int at = game.Armies[army].System;
			const float time = static_cast<float>(ImGui::GetTime());
			for (int next: game.Armies[army].Path) {
				const ImVec2 a = toScreen(game.Systems[at].X, game.Systems[at].Y);
				const ImVec2 b = toScreen(game.Systems[next].X, game.Systems[next].Y);
				drawList->AddLine(a, b, IM_COL32(255, 220, 90, 120), 3.0F);
				// Dots marching along it, the way the army will go.
				const float length = std::hypot(b.x - a.x, b.y - a.y);
				const int dots = std::max(2, static_cast<int>(length / 14.0F));
				for (int dot = 0; dot < dots; ++dot) {
					const float t = std::fmod((static_cast<float>(dot) + time * 2.0F) / static_cast<float>(dots), 1.0F);
					drawList->AddCircleFilled(ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t), 2.5F, IM_COL32(255, 230, 120, 255), 8);
				}
				at = next;
			}
			const ImVec2 end = toScreen(game.Systems[at].X, game.Systems[at].Y);
			drawList->AddCircle(end, 14.0F + 2.0F * std::sin(time * 4.0F), IM_COL32(255, 220, 90, 200), 24, 2.0F);
		}

		int hoveredSystem = -1;
		long hoveredArmy = 0;
		const float starSize = std::clamp(6.0F * s_Screen.GalaxyZoom, 4.0F, 12.0F);
		for (int index = 0; index < static_cast<int>(game.Systems.size()); ++index) {
			const StarSystem& system = game.Systems[index];
			const ImVec2 at = toScreen(system.X, system.Y);
			const ImU32 color = StarColor(system.StarColor);
			for (int glow = 3; glow >= 1; --glow) {
				drawList->AddCircleFilled(at, starSize * (1.0F + static_cast<float>(glow) * 0.8F), Alpha(color, 0.08F), 24);
			}
			drawList->AddCircleFilled(at, starSize, color, 24);
			// Who owns the points here: a ring split by faction.
			std::vector<int> owned(game.Factions.size(), 0);
			int total = 0;
			for (int planet: system.Planets) {
				for (const Point& point: game.Planets[planet].Points) {
					++total;
					if (point.Owner >= 0) {
						++owned[point.Owner];
					}
				}
			}
			float angle = -c_Pi * 0.5F;
			for (int faction = 0; faction < static_cast<int>(owned.size()) && total > 0; ++faction) {
				if (owned[faction] == 0) {
					continue;
				}
				const float sweep = 2.0F * c_Pi * static_cast<float>(owned[faction]) / static_cast<float>(total);
				drawList->PathArcTo(at, starSize + 5.0F, angle, angle + sweep, 16);
				drawList->PathStroke(FactionColor(faction), ImDrawFlags_None, 3.0F);
				angle += sweep;
			}
			if (index == s_Screen.SelectedSystem) {
				drawList->AddCircle(at, starSize + 10.0F, IM_COL32(255, 255, 255, 200), 32, 1.5F);
			}
			const bool onIt = hovered && std::hypot(io.MousePos.x - at.x, io.MousePos.y - at.y) < starSize + 8.0F;
			if (onIt) {
				hoveredSystem = index;
			}
			if (s_Screen.GalaxyZoom >= 0.9F || onIt || index == s_Screen.SelectedSystem) {
				const ImU32 nameColor = systemOwners[index] >= 0 ? Scale(FactionColor(systemOwners[index]), 1.3F) : IM_COL32(200, 205, 230, 220);
				CenteredText(drawList, ImVec2(at.x, at.y + starSize + 9.0F), onIt ? IM_COL32(255, 255, 255, 255) : nameColor, system.Name);
			}
			// Armies here, one shield a faction, beside the star.
			std::vector<std::vector<int>> byFaction(game.Factions.size());
			for (int army = 0; army < static_cast<int>(game.Armies.size()); ++army) {
				if (game.Armies[army].System == index && !IsMoving(game.Armies[army].Id)) {
					byFaction[game.Armies[army].Faction].push_back(army);
				}
			}
			float offset = starSize + 14.0F;
			for (int faction = 0; faction < static_cast<int>(byFaction.size()); ++faction) {
				if (byFaction[faction].empty()) {
					continue;
				}
				if (DrawArmies(drawList, ImVec2(at.x + offset, at.y - starSize), 7.0F, byFaction[faction]) && hovered) {
					// The first of them, or the next after the one picked.
					const std::vector<int>& armies = byFaction[faction];
					auto picked = std::find_if(armies.begin(), armies.end(), [](int army) { return s_Screen.Game.Armies[army].Id == s_Screen.SelectedArmy; });
					hoveredArmy = game.Armies[picked == armies.end() || picked + 1 == armies.end() ? armies.front() : *(picked + 1)].Id;
				}
				offset += faction == Player() ? 22.0F : 17.0F;
			}
		}
		const Placer place = [&](const Place& where, bool army, ImVec2& at) {
			if (where.System < 0 || where.System >= static_cast<int>(game.Systems.size())) {
				return false;
			}
			at = toScreen(game.Systems[where.System].X, game.Systems[where.System].Y);
			if (army) {
				at.x += starSize + 14.0F;
				at.y -= starSize;
			}
			return true;
		};
		DrawPulses(drawList, place);
		DrawMotions(drawList, place, 7.0F);
		if (const int beacon = s_Screen.Lights.System; beacon >= 0 && beacon < static_cast<int>(game.Systems.size())) {
			Beacon(drawList, toScreen(game.Systems[beacon].X, game.Systems[beacon].Y), starSize + 8.0F);
		}

		if (hovered && ImGui::IsItemDeactivated() && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left, 6.0F) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (hoveredArmy) {
				s_Screen.SelectedArmy = hoveredArmy;
				s_Screen.SelectedSystem = game.Armies[FindArmy(game, hoveredArmy)].System;
			} else if (hoveredSystem >= 0) {
				s_Screen.SelectedSystem = hoveredSystem;
				s_Screen.SelectedPlanet = s_Screen.SelectedPoint = -1;
			}
		}
		if (hovered && hoveredSystem >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			ShowSystem(hoveredSystem);
		}
		if (hovered && hoveredSystem >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			OrderToSystem(hoveredSystem);
		}
		if (hoveredSystem >= 0 && hoveredArmy == 0) {
			const StarSystem& system = game.Systems[hoveredSystem];
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(system.Name.c_str());
			for (int planet: system.Planets) {
				const PlanetControl control = ControlOf(game, planet);
				ImGui::TextDisabled("  %s, %s%s", game.Planets[planet].Name.c_str(), PlanetTypeName(game.Planets[planet].Type), control.Controller >= 0 ? (std::string(": ") + FactionLabel(control.Controller) + (control.Invaded ? " (invaded)" : "")).c_str() : control.Contested ? ": contested" : "");
			}
			std::array<int, c_ResourceCount> yields{};
			for (int planet: system.Planets) {
				for (const Point& point: game.Planets[planet].Points) {
					yields[static_cast<size_t>(std::clamp(point.Deposit, 0, c_ResourceCount - 1))] += point.Yield;
				}
			}
			bool first = true;
			for (int each = 0; each < c_ResourceCount; ++each) {
				if (yields[each] > 0) {
					if (!first) {
						ImGui::SameLine();
					}
					first = false;
					ResourceIcon(static_cast<Resource>(each));
					ImGui::SameLine(0.0F, 2.0F);
					ImGui::TextDisabled("%d", yields[each]);
				}
			}
			ImGui::TextDisabled("Double-click to see it. Right-click to send the army picked.");
			ImGui::EndTooltip();
		} else if (hoveredArmy) {
			const Army& army = game.Armies[FindArmy(game, hoveredArmy)];
			ImGui::SetTooltip("%s (%s): %d soldiers, strength %.0f", army.Name.c_str(), FactionLabel(army.Faction).c_str(), SoldierCount(army), Strength(game, army));
		}
		DrawLegend(drawList, ImVec2(from.x + 8.0F, to.y - 8.0F));
		EndCanvas();
	}
#pragma endregion

#pragma region The system map
	void SystemMap(ImVec2 size) {
		CampaignModel::Campaign& game = s_Screen.Game;
		const int systemIndex = s_Screen.ViewSystem;
		if (systemIndex < 0 || systemIndex >= static_cast<int>(game.Systems.size())) {
			s_Screen.Showing = View::Galaxy;
			return;
		}
		const StarSystem& system = game.Systems[systemIndex];
		ImVec2 from;
		ImVec2 to;
		const bool hovered = BeginCanvas("##system", size, from, to);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImGuiIO& io = ImGui::GetIO();
		DrawSpace(drawList, from, to, game.Seed + static_cast<std::uint32_t>(systemIndex) * 31u, ImVec2(0.0F, 0.0F));
		const ImVec2 star((from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F);
		float outermost = 1.0F;
		for (int planet: system.Planets) {
			if (game.Planets[planet].Parent < 0) {
				outermost = std::max(outermost, game.Planets[planet].Orbit);
			}
		}
		const float room = std::min(to.x - from.x, (to.y - from.y) * 1.6F) * 0.45F;
		const float step = room / (outermost + 0.6F);
		const ImU32 starColor = StarColor(system.StarColor);
		for (int glow = 5; glow >= 1; --glow) {
			drawList->AddCircleFilled(star, step * 0.25F * (1.0F + static_cast<float>(glow) * 0.5F), Alpha(starColor, 0.07F), 48);
		}
		drawList->AddCircleFilled(star, step * 0.25F, starColor, 48);

		std::vector<ImVec2> where(game.Planets.size());
		auto planetAt = [&](const Planet& planet) {
			const float radius = planet.Orbit * step;
			// (Squashed a little, as if seen at a slant.)
			return ImVec2(star.x + std::cos(planet.Angle) * radius, star.y + std::sin(planet.Angle) * radius * 0.62F);
		};
		int hoveredPlanet = -1;
		long hoveredArmy = 0;
		for (int planetIndex: system.Planets) {
			const Planet& planet = game.Planets[planetIndex];
			if (planet.Parent >= 0) {
				continue;
			}
			drawList->AddEllipse(star, ImVec2(planet.Orbit * step, planet.Orbit * step * 0.62F), IM_COL32(120, 140, 200, 50), 0.0F, 96, 1.0F);
			where[planetIndex] = planetAt(planet);
		}
		for (int planetIndex: system.Planets) {
			const Planet& planet = game.Planets[planetIndex];
			if (planet.Parent < 0) {
				continue;
			}
			const ImVec2 parent = where[planet.Parent];
			const float parentRadius = step * 0.12F * game.Planets[planet.Parent].Size;
			const float radius = parentRadius + step * 0.12F * planet.Orbit;
			drawList->AddEllipse(parent, ImVec2(radius, radius * 0.62F), IM_COL32(120, 140, 200, 40), 0.0F, 48, 1.0F);
			where[planetIndex] = ImVec2(parent.x + std::cos(planet.Angle) * radius, parent.y + std::sin(planet.Angle) * radius * 0.62F);
		}
		const ImVec2 orbitAt(star.x, star.y + step * 0.25F + 32.0F);
		const Placer place = [&](const Place& at, bool army, ImVec2& out) {
			if (at.System != systemIndex) {
				// Off the map, at its edge toward that system along the lanes.
				ImVec2 toward(0.0F, -1.0F);
				if (at.System >= 0 && at.System < static_cast<int>(game.Systems.size())) {
					toward = ImVec2((game.Systems[at.System].X - system.X) * 1.35F, game.Systems[at.System].Y - system.Y);
				}
				const float length = std::max(std::hypot(toward.x, toward.y), 0.0001F);
				const float reach = std::hypot(to.x - from.x, to.y - from.y) * 0.5F;
				out = ImVec2(star.x + toward.x / length * reach, star.y + toward.y / length * reach);
				return false;
			}
			if (at.Planet < 0 || at.Planet >= static_cast<int>(game.Planets.size())) {
				out = orbitAt;
				return true;
			}
			out = where[at.Planet];
			if (army) {
				out.y -= step * 0.12F * game.Planets[at.Planet].Size + 16.0F;
			}
			return true;
		};
		for (int planetIndex: system.Planets) {
			const Planet& planet = game.Planets[planetIndex];
			const ImVec2 at = where[planetIndex];
			const float radius = step * 0.12F * planet.Size;
			DrawPlanetBall(drawList, at, radius, planet, star);
			// Who controls it: a ring in their colour, broken in red while it's invaded; a dashed grey ring while it's contested.
			if (const PlanetControl held = ControlOf(game, planetIndex); held.Controller >= 0) {
				drawList->AddCircle(at, radius + 3.0F, FactionColor(held.Controller), 64, 3.5F);
				if (held.Invaded) {
					const float blink = 0.5F + 0.5F * std::sin(static_cast<float>(ImGui::GetTime()) * 4.0F);
					for (int dash = 0; dash < 8; dash += 2) {
						const float start = static_cast<float>(dash) * c_Pi / 4.0F;
						drawList->PathArcTo(at, radius + 3.0F, start, start + c_Pi / 4.0F, 12);
						drawList->PathStroke(Alpha(IM_COL32(255, 70, 50, 255), blink), ImDrawFlags_None, 3.5F);
					}
				}
			} else if (held.Contested) {
				for (int dash = 0; dash < 16; dash += 2) {
					const float start = static_cast<float>(dash) * c_Pi / 8.0F;
					drawList->PathArcTo(at, radius + 3.0F, start, start + c_Pi / 8.0F, 6);
					drawList->PathStroke(IM_COL32(170, 170, 180, 220), ImDrawFlags_None, 2.5F);
				}
			}
			if (planetIndex == s_Screen.SelectedPlanet) {
				drawList->AddCircle(at, radius + 6.0F, IM_COL32(255, 255, 255, 220), 48, 1.5F);
			}
			// Its points round it, in their owners' colours.
			const int count = static_cast<int>(planet.Points.size());
			for (int point = 0; point < count; ++point) {
				const float angle = -c_Pi * 0.5F + 2.0F * c_Pi * static_cast<float>(point) / static_cast<float>(count);
				const ImVec2 dot(at.x + std::cos(angle) * (radius + 9.0F), at.y + std::sin(angle) * (radius + 9.0F));
				const int owner = planet.Points[point].Owner;
				drawList->AddCircleFilled(dot, 4.0F, owner >= 0 ? FactionColor(owner) : IM_COL32(90, 90, 100, 255), 12);
				drawList->AddCircle(dot, 4.0F, IM_COL32(0, 0, 0, 220), 12, 1.0F);
			}
			const PlanetControl control = ControlOf(game, planetIndex);
			std::string label = planet.Name;
			if (control.Invaded) {
				label += " (invaded)";
			} else if (control.Contested) {
				label += " (contested)";
			}
			CenteredText(drawList, ImVec2(at.x, at.y + radius + 15.0F), control.Controller >= 0 ? Scale(FactionColor(control.Controller), 1.2F) : IM_COL32(200, 205, 230, 230), label);
			{
				// What its points produce, an icon for each kind, under its name.
				std::array<int, c_ResourceCount> yields{};
				for (const Point& point: planet.Points) {
					yields[static_cast<size_t>(std::clamp(point.Deposit, 0, c_ResourceCount - 1))] += point.Yield;
				}
				const int kinds = static_cast<int>(std::count_if(yields.begin(), yields.end(), [](int yield) { return yield > 0; }));
				const float icon = 13.0F;
				float x = at.x - static_cast<float>(kinds) * icon * 0.75F + icon * 0.75F;
				const float y = at.y + radius + 15.0F + ImGui::GetTextLineHeight() + icon * 0.6F;
				for (int each = 0; each < c_ResourceCount && Shown(Step::ReadPoint); ++each) {
					if (yields[each] > 0) {
						DrawResourceBadge(drawList, ImVec2(x, y), icon, static_cast<Resource>(each));
						x += icon * 1.5F;
					}
				}
			}
			const bool onIt = hovered && std::hypot(io.MousePos.x - at.x, io.MousePos.y - at.y) < radius + 8.0F;
			if (onIt) {
				hoveredPlanet = planetIndex;
			}
			// Armies standing on it.
			std::vector<int> armies;
			for (int army = 0; army < static_cast<int>(game.Armies.size()); ++army) {
				if (game.Armies[army].Planet == planetIndex && !IsMoving(game.Armies[army].Id)) {
					armies.push_back(army);
				}
			}
			if (const long onIt = DrawArmyRow(drawList, ImVec2(at.x, at.y - radius - 16.0F), 6.0F, armies, true); onIt && hovered) {
				hoveredArmy = onIt;
			}
		}
		// Armies in orbit, in a row under the star.
		std::vector<int> orbiting = ArmiesInOrbit(game, systemIndex);
		orbiting.erase(std::remove_if(orbiting.begin(), orbiting.end(), [&game](int army) { return IsMoving(game.Armies[army].Id); }), orbiting.end());
		if (const long onIt = DrawArmyRow(drawList, orbitAt, 7.0F, orbiting, false); onIt && hovered) {
			hoveredArmy = onIt;
		}
		if (!orbiting.empty()) {
			CenteredText(drawList, ImVec2(star.x, star.y + step * 0.25F + 4.0F), IM_COL32(180, 190, 220, 200), "In orbit");
		}
		CenteredText(drawList, ImVec2(star.x, from.y + 8.0F), IM_COL32(255, 255, 255, 255), system.Name + " system");
		DrawPulses(drawList, place);
		DrawMotions(drawList, place, 7.0F);
		if (const int beacon = s_Screen.Lights.Planet; beacon >= 0 && beacon < static_cast<int>(game.Planets.size()) && game.Planets[beacon].System == systemIndex) {
			Beacon(drawList, where[beacon], step * 0.12F * game.Planets[beacon].Size + 6.0F);
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (hoveredArmy) {
				s_Screen.SelectedArmy = hoveredArmy;
			} else if (hoveredPlanet >= 0) {
				s_Screen.SelectedPlanet = hoveredPlanet;
				s_Screen.SelectedPoint = -1;
			}
		}
		if (hovered && hoveredPlanet >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !game.Planets[hoveredPlanet].Points.empty()) {
			ShowPlanet(hoveredPlanet);
		}
		if (hovered && hoveredPlanet >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			if (game.Planets[hoveredPlanet].Points.empty()) {
				s_Screen.Note = "A gas giant can't be landed on: its moons can.";
			} else {
				// To pick where to land: the planet's own map.
				ShowPlanet(hoveredPlanet);
				s_Screen.Note = "Right-click a point to land or move there.";
			}
		}
		if (hoveredArmy) {
			const Army& army = game.Armies[FindArmy(game, hoveredArmy)];
			ImGui::SetTooltip("%s (%s): %d soldiers, strength %.0f", army.Name.c_str(), FactionLabel(army.Faction).c_str(), SoldierCount(army), Strength(game, army));
		} else if (hoveredPlanet >= 0) {
			const Planet& planet = game.Planets[hoveredPlanet];
			ImGui::BeginTooltip();
			ImGui::Text("%s, %s", planet.Name.c_str(), PlanetTypeName(planet.Type));
			std::array<int, c_ResourceCount> yields{};
			for (const Point& point: planet.Points) {
				yields[static_cast<size_t>(std::clamp(point.Deposit, 0, c_ResourceCount - 1))] += point.Yield;
			}
			for (int each = 0; each < c_ResourceCount; ++each) {
				if (yields[each] > 0) {
					ResourceIcon(static_cast<Resource>(each));
					ImGui::SameLine();
					ImGui::TextDisabled("%s %d", ResourceName(static_cast<Resource>(each)), yields[each]);
				}
			}
			if (!planet.Points.empty()) {
				ImGui::TextDisabled("Double-click for its map. Right-click to land the army picked.");
			}
			ImGui::EndTooltip();
		}
		DrawLegend(drawList, ImVec2(from.x + 8.0F, to.y - 8.0F));
		EndCanvas();
	}
#pragma endregion

#pragma region The planet map
	void PlanetMap(ImVec2 size) {
		CampaignModel::Campaign& game = s_Screen.Game;
		const int planetIndex = s_Screen.ViewPlanet;
		if (planetIndex < 0 || planetIndex >= static_cast<int>(game.Planets.size())) {
			s_Screen.Showing = View::System;
			return;
		}
		const Planet& planet = game.Planets[planetIndex];
		ImVec2 canvasFrom;
		ImVec2 canvasTo;
		const bool hovered = BeginCanvas("##planet", size, canvasFrom, canvasTo);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImGuiIO& io = ImGui::GetIO();
		drawList->AddRectFilled(canvasFrom, canvasTo, IM_COL32(6, 8, 18, 255));
		// The surface map: two wide for one high, as big as fits.
		const float mapWidth = std::min(canvasTo.x - canvasFrom.x - 20.0F, (canvasTo.y - canvasFrom.y - 40.0F) * 2.0F);
		const float mapHeight = mapWidth * 0.5F;
		const ImVec2 from((canvasFrom.x + canvasTo.x - mapWidth) * 0.5F, (canvasFrom.y + canvasTo.y - mapHeight) * 0.5F + 10.0F);
		const ImVec2 to(from.x + mapWidth, from.y + mapHeight);
		const ImU32 base = PlanetColor(planet.Type);
		drawList->AddRectFilled(from, to, Scale(base, 0.55F));
		drawList->PushClipRect(from, to, true);
		std::mt19937 dice(planet.Look);
		std::uniform_real_distribution<float> unit(0.0F, 1.0F);
		for (int blob = 0; blob < 40; ++blob) {
			const ImVec2 at(from.x + unit(dice) * mapWidth, from.y + unit(dice) * mapHeight);
			const float radius = (0.04F + unit(dice) * 0.12F) * mapWidth;
			ImU32 color = Scale(base, 0.6F + unit(dice) * 0.6F);
			if (planet.Type == PlanetType::Ocean) {
				color = blob % 3 == 0 ? IM_COL32(70, 140, 75, 255) : Scale(base, 0.7F + unit(dice) * 0.4F);
			} else if (planet.Type == PlanetType::Volcanic && blob % 5 == 0) {
				color = IM_COL32(230, 100, 30, 255);
			}
			drawList->AddCircleFilled(at, radius, Alpha(color, 0.35F), 32);
		}
		if (planet.Type == PlanetType::Barren || planet.Type == PlanetType::Ice) {
			// Craters.
			for (int crater = 0; crater < 14; ++crater) {
				const ImVec2 at(from.x + unit(dice) * mapWidth, from.y + unit(dice) * mapHeight);
				const float radius = (0.01F + unit(dice) * 0.035F) * mapWidth;
				drawList->AddCircle(at, radius, Alpha(Scale(base, 0.4F), 0.7F), 24, 2.0F);
			}
		}
		// Who holds which ground: a wash of each owner's colour round their points.
		for (const Point& point: planet.Points) {
			if (point.Owner >= 0) {
				const ImVec2 at(from.x + point.X * mapWidth, from.y + point.Y * mapHeight);
				for (int ring = 0; ring < 4; ++ring) {
					drawList->AddCircleFilled(at, mapWidth * (0.17F - 0.035F * static_cast<float>(ring)), Alpha(FactionColor(point.Owner), 0.09F), 40);
				}
			}
		}
		// A faint grid, as a map has.
		for (int line = 1; line < 8; ++line) {
			const float x = from.x + mapWidth * static_cast<float>(line) / 8.0F;
			drawList->AddLine(ImVec2(x, from.y), ImVec2(x, to.y), IM_COL32(255, 255, 255, 14));
			if (line < 4) {
				const float y = from.y + mapHeight * static_cast<float>(line) / 4.0F;
				drawList->AddLine(ImVec2(from.x, y), ImVec2(to.x, y), IM_COL32(255, 255, 255, 14));
			}
		}
		drawList->PopClipRect();
		drawList->AddRect(from, to, IM_COL32(150, 160, 210, 160), 0.0F, 0, 2.0F);
		const PlanetControl control = ControlOf(game, planetIndex);
		std::string title = planet.Name + ", " + PlanetTypeName(planet.Type) + ": ";
		title += control.Controller >= 0 ? "controlled by " + FactionLabel(control.Controller) + (control.Invaded ? ", invaded" : "") : control.Contested ? "contested" : "unclaimed";
		CenteredText(drawList, ImVec2((from.x + to.x) * 0.5F, from.y - 24.0F), control.Controller >= 0 ? Scale(FactionColor(control.Controller), 1.2F) : IM_COL32(230, 230, 240, 255), title);

		{
			// Control at a glance: a bar split by who holds the points, with a mark at half, and what it means.
			const int total = static_cast<int>(planet.Points.size());
			std::vector<int> owned(game.Factions.size(), 0);
			int unclaimed = 0;
			for (const Point& point: planet.Points) {
				if (point.Owner >= 0) {
					++owned[point.Owner];
				} else {
					++unclaimed;
				}
			}
			std::vector<int> order;
			if (control.Controller >= 0) {
				order.push_back(control.Controller);
			}
			for (int faction = 0; faction < static_cast<int>(owned.size()); ++faction) {
				if (owned[faction] > 0 && faction != control.Controller) {
					order.push_back(faction);
				}
			}
			const float line = ImGui::GetTextLineHeight();
			const float barWidth = std::min(mapWidth * 0.35F, ImGui::GetFontSize() * 16.0F);
			std::string caption;
			if (control.Controller >= 0) {
				caption = FactionLabel(control.Controller) + " controls it (" + std::to_string(owned[control.Controller]) + " of " + std::to_string(total) + " points)";
				if (control.Invaded) {
					caption += ", invaded";
				}
			} else {
				caption = std::string(control.Contested ? "Contested" : "Unclaimed") + ": more than " + std::to_string(total / 2) + " of " + std::to_string(total) + " points controls it";
			}
			const float boxWidth = std::max(barWidth, ImGui::CalcTextSize(caption.c_str()).x) + 16.0F;
			const ImVec2 boxFrom(from.x + 8.0F, to.y - line * 2.0F - 26.0F);
			drawList->AddRectFilled(boxFrom, ImVec2(boxFrom.x + boxWidth, to.y - 8.0F), IM_COL32(0, 0, 10, 180), 4.0F);
			const ImVec2 barFrom(boxFrom.x + 8.0F, boxFrom.y + 8.0F);
			float x = barFrom.x;
			auto segment = [&](int count, ImU32 color) {
				const float width = barWidth * static_cast<float>(count) / static_cast<float>(std::max(total, 1));
				drawList->AddRectFilled(ImVec2(x, barFrom.y), ImVec2(x + width, barFrom.y + line), color);
				x += width;
			};
			for (int faction: order) {
				segment(owned[faction], FactionColor(faction));
			}
			segment(unclaimed, IM_COL32(90, 90, 100, 255));
			for (int point = 1; point < total; ++point) {
				const float tick = barFrom.x + barWidth * static_cast<float>(point) / static_cast<float>(total);
				drawList->AddLine(ImVec2(tick, barFrom.y), ImVec2(tick, barFrom.y + line), IM_COL32(0, 0, 0, 200), 1.0F);
			}
			const float half = barFrom.x + barWidth * 0.5F;
			drawList->AddLine(ImVec2(half, barFrom.y - 4.0F), ImVec2(half, barFrom.y + line + 4.0F), IM_COL32(255, 255, 255, 255), 2.0F);
			drawList->AddRect(barFrom, ImVec2(barFrom.x + barWidth, barFrom.y + line), IM_COL32(200, 200, 220, 200));
			drawList->AddText(ImVec2(barFrom.x, barFrom.y + line + 6.0F), control.Controller >= 0 ? Scale(FactionColor(control.Controller), 1.3F) : IM_COL32(230, 230, 240, 255), caption.c_str());
		}

		auto pointAt = [&](const Point& point) { return ImVec2(from.x + point.X * mapWidth, from.y + point.Y * mapHeight); };
		// Routes overland.
		const int ownArmy = OwnSelectedArmy();
		for (int a = 0; a < static_cast<int>(planet.Points.size()); ++a) {
			for (int b: planet.Points[a].Links) {
				if (b > a) {
					const bool reachable = ownArmy >= 0 && game.Armies[ownArmy].Planet == planetIndex && (game.Armies[ownArmy].Point == a || game.Armies[ownArmy].Point == b);
					drawList->AddLine(pointAt(planet.Points[a]), pointAt(planet.Points[b]), reachable ? IM_COL32(255, 220, 90, 230) : IM_COL32(230, 220, 190, 150), reachable ? 4.0F : 3.0F);
				}
			}
		}
		int hoveredPoint = -1;
		long hoveredArmy = 0;
		const float radius = std::clamp(mapWidth * 0.018F, 9.0F, 18.0F);
		for (int index = 0; index < static_cast<int>(planet.Points.size()); ++index) {
			const Point& point = planet.Points[index];
			const ImVec2 at = pointAt(point);
			const ImU32 color = point.Owner >= 0 ? FactionColor(point.Owner) : IM_COL32(120, 120, 130, 255);
			if (index == s_Screen.SelectedPoint) {
				drawList->AddCircle(at, radius + 7.0F, IM_COL32(255, 255, 255, 230), 32, 2.0F);
			}
			if (point.Settlement > 0 && point.Built[static_cast<size_t>(Building::Walls)]) {
				drawList->AddCircle(at, radius + 3.0F, IM_COL32(200, 200, 200, 255), 32, 3.0F);
			}
			drawList->AddCircleFilled(at, radius, color, 32);
			drawList->AddCircle(at, radius, IM_COL32(10, 10, 20, 255), 32, 2.0F);
			// A house for each level of settlement.
			for (int house = 0; house < point.Settlement; ++house) {
				const float x = at.x - static_cast<float>(point.Settlement - 1) * 5.0F + static_cast<float>(house) * 10.0F;
				const float y = at.y - 3.0F;
				drawList->AddRectFilled(ImVec2(x - 3.5F, y), ImVec2(x + 3.5F, y + 6.0F), IM_COL32(245, 240, 225, 255));
				drawList->AddTriangleFilled(ImVec2(x - 4.5F, y), ImVec2(x + 4.5F, y), ImVec2(x, y - 4.5F), IM_COL32(245, 240, 225, 255));
			}
			if (Shown(Step::ReadPoint)) {
				// What it produces: an icon and how much, at its top right.
				const Resource deposit = static_cast<Resource>(std::clamp(point.Deposit, 0, c_ResourceCount - 1));
				const float badge = std::max(radius * 0.8F, 11.0F);
				const ImVec2 badgeAt(at.x + radius * 0.95F, at.y - radius * 0.95F);
				DrawResourceBadge(drawList, badgeAt, badge, deposit);
				const std::string amount = std::to_string(point.Owner >= 0 ? PointYield(game, planetIndex, index) : point.Yield);
				drawList->AddText(ImVec2(badgeAt.x + badge * 0.7F + 1.0F, badgeAt.y - ImGui::GetTextLineHeight() * 0.5F), ResourceColor(deposit), amount.c_str());
			}
			std::string label = point.Name;
			if (point.Owner < 0 && point.Militia > 0.01F) {
				label += " (locals)";
			}
			CenteredText(drawList, ImVec2(at.x, at.y + radius + 4.0F), IM_COL32(255, 255, 255, 240), label);
			if (point.Owner >= 0) {
				CenteredText(drawList, ImVec2(at.x, at.y + radius + 4.0F + ImGui::GetTextLineHeight()), Scale(FactionColor(point.Owner), 1.3F), FactionLabel(point.Owner) + (point.Owner == Player() ? " (you)" : ""));
			}
			if (hovered && std::hypot(io.MousePos.x - at.x, io.MousePos.y - at.y) < radius + 4.0F) {
				hoveredPoint = index;
			}
			std::vector<int> armies = ArmiesAt(game, planetIndex, index);
			armies.erase(std::remove_if(armies.begin(), armies.end(), [&game](int army) { return IsMoving(game.Armies[army].Id); }), armies.end());
			if (const long onIt = DrawArmyRow(drawList, ImVec2(at.x, at.y - radius - 16.0F), 7.0F, armies, true); onIt && hovered) {
				hoveredArmy = onIt;
			}
		}
		const Placer place = [&](const Place& at, bool army, ImVec2& out) {
			if (at.Planet == planetIndex && at.Point >= 0 && at.Point < static_cast<int>(planet.Points.size())) {
				out = pointAt(planet.Points[at.Point]);
				if (army) {
					out.y -= radius + 16.0F;
				}
				return true;
			}
			// In orbit or further: above the map, to drop down from or rise up to.
			out = ImVec2((from.x + to.x) * 0.5F, canvasFrom.y - 10.0F);
			return false;
		};
		DrawPulses(drawList, place);
		DrawMotions(drawList, place, 7.0F);
		for (int beacon: s_Screen.Lights.Points) {
			if (beacon >= 0 && beacon < static_cast<int>(planet.Points.size())) {
				Beacon(drawList, pointAt(planet.Points[beacon]), radius + 4.0F);
			}
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (hoveredArmy) {
				s_Screen.SelectedArmy = hoveredArmy;
				const Army& army = game.Armies[FindArmy(game, hoveredArmy)];
				s_Screen.SelectedPoint = army.Point;
			} else if (hoveredPoint >= 0) {
				s_Screen.SelectedPoint = hoveredPoint;
				s_Screen.SelectedPlanet = planetIndex;
				// The player's army there, if there's one, picked with it.
				for (int army: ArmiesAt(game, planetIndex, hoveredPoint)) {
					if (game.Armies[army].Faction == Player()) {
						s_Screen.SelectedArmy = game.Armies[army].Id;
						break;
					}
				}
			}
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			if (hoveredPoint >= 0) {
				OrderToPoint(planetIndex, hoveredPoint);
			}
		}
		if (hoveredArmy) {
			const Army& army = game.Armies[FindArmy(game, hoveredArmy)];
			ImGui::SetTooltip("%s (%s): %d soldiers, strength %.0f", army.Name.c_str(), FactionLabel(army.Faction).c_str(), SoldierCount(army), Strength(game, army));
		} else if (hoveredPoint >= 0) {
			const Point& point = planet.Points[hoveredPoint];
			float defence = 0.0F;
			const int player = Player();
			const bool fight = player >= 0 && WouldFight(game, player, planetIndex, hoveredPoint, defence);
			ImGui::BeginTooltip();
			ImGui::Text("%s (%s)", point.Name.c_str(), FactionLabel(point.Owner).c_str());
			ImGui::TextDisabled("%s%s", point.Settlement > 0 ? SettlementName(point.Settlement) : "No settlement", point.Owner >= 0 ? (", " + std::to_string(PointIncome(game, planetIndex, hoveredPoint)) + " gold a turn").c_str() : "");
			{
				const Resource deposit = static_cast<Resource>(std::clamp(point.Deposit, 0, c_ResourceCount - 1));
				ResourceIcon(deposit);
				ImGui::SameLine();
				ImGui::TextDisabled("%s, %d a turn%s", ResourceName(deposit), point.Owner >= 0 ? PointYield(game, planetIndex, hoveredPoint) : point.Yield + 1, point.Owner >= 0 ? "" : " to its owner with an outpost");
			}
			if (fight) {
				ImGui::TextDisabled("Defended: strength %.0f", defence);
			}
			ImGui::TextDisabled("Right-click to send the army picked.");
			ImGui::EndTooltip();
		}
		EndCanvas();
	}
#pragma endregion

#pragma region The side panel
	void HealthBar(float health, float width) {
		const ImVec2 at = ImGui::GetCursorScreenPos();
		const float height = ImGui::GetTextLineHeight() * 0.5F;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float top = at.y + height * 0.5F;
		drawList->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + width, top + height), IM_COL32(40, 40, 50, 255));
		const ImU32 color = health > 0.66F ? IM_COL32(90, 200, 90, 255) : health > 0.33F ? IM_COL32(230, 190, 60, 255) : IM_COL32(220, 70, 60, 255);
		drawList->AddRectFilled(ImVec2(at.x, top), ImVec2(at.x + width * std::clamp(health, 0.0F, 1.0F), top + height), color);
		ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
	}

	std::string WhereIs(const Army& army) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		if (army.Planet >= 0) {
			return game.Planets[army.Planet].Points[army.Point].Name + " on " + game.Planets[army.Planet].Name;
		}
		return "In orbit at " + game.Systems[army.System].Name;
	}

	void ArmyPanel(int army) {
		CampaignModel::Campaign& game = s_Screen.Game;
		Army& self = game.Armies[army];
		const bool own = self.Faction == Player();
		const Faction& faction = game.Factions[self.Faction];
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(faction.Color), "%s", self.Name.c_str());
		ImGui::TextDisabled("%s", faction.Name.c_str());
		ImGui::TextWrapped("%s", WhereIs(self).c_str());
		ImGui::Text("%d soldiers, %d squads, strength %.0f", SoldierCount(self), static_cast<int>(self.Squads.size()), Strength(game, self));
		if (own) {
			ImGui::Text("Moves left: %d of %d", self.Moves, c_MovesPerTurn);
			if (!self.Path.empty()) {
				ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.35F, 1.0F), "On its way to %s (%d jumps)", game.Systems[self.Path.back()].Name.c_str(), static_cast<int>(self.Path.size()));
				ImGui::SameLine();
				if (ToolUI::SmallButton("Stop")) {
					self.Path.clear();
				}
			}
			ImGui::TextDisabled("Upkeep: %d gold a turn", ArmyUpkeep(game, self));
			const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
			ImGui::BeginDisabled(self.Planet < 0 || self.Moves < 1);
			if (ToolUI::Button("Take off", ImVec2(half, 0.0F))) {
				FollowNext(army);
				OrderLaunch(game, army);
			}
			ImGui::EndDisabled();
			BeaconItem(s_Screen.Lights.TakeOff && self.Planet >= 0 && self.Moves >= 1);
			ImGui::SetItemTooltip("Into orbit, to move to another system or land somewhere else on this one. Takes one move.");
			ImGui::SameLine();
			const int refill = ReplenishCost(game, army);
			const std::string refillLabel = refill > 0 ? "Refill (" + std::to_string(refill) + ")" : std::string("Refill");
			const bool canRefill = refill > 0 && self.Planet >= 0 && game.Planets[self.Planet].Points[self.Point].Owner == self.Faction && game.Planets[self.Planet].Points[self.Point].Settlement > 0 && faction.Gold >= refill;
			ImGui::BeginDisabled(!canRefill);
			if (ToolUI::Button(refillLabel.c_str(), ImVec2(-1.0F, 0.0F))) {
				Replenish(game, army);
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Fills its squads back up with new soldiers. Only at one of your settlements.");
			// Other armies of yours on the same spot, to merge.
			for (int other = 0; other < static_cast<int>(game.Armies.size()); ++other) {
				const Army& each = game.Armies[other];
				if (other != army && each.Faction == self.Faction && each.System == self.System && each.Planet == self.Planet && each.Point == self.Point) {
					ImGui::PushID(other);
					const std::string label = "Take in " + each.Name;
					if (ToolUI::Button(label.c_str(), ImVec2(-1.0F, 0.0F))) {
						const long keep = self.Id;
						Merge(game, army, other);
						s_Screen.SelectedArmy = keep;
						ImGui::PopID();
						return;
					}
					ImGui::SetItemTooltip("Its squads join this army, as many as fit (12 at most).");
					ImGui::PopID();
				}
			}
		}
		ImGui::SeparatorText("Squads");
		for (int squad = 0; squad < static_cast<int>(self.Squads.size()); ++squad) {
			const Squad& members = self.Squads[squad];
			const SquadType& type = faction.Squads[std::clamp(members.Type, 0, static_cast<int>(faction.Squads.size()) - 1)];
			float health = 0.0F;
			for (const Soldier& soldier: members.Soldiers) {
				health += soldier.Health;
			}
			health /= std::max(1.0F, static_cast<float>(members.Soldiers.size()));
			ImGui::PushID(squad);
			const std::string header = type.Name + "  " + std::to_string(members.Soldiers.size()) + "/" + std::to_string(type.Size) + "###squad";
			// (Overlap allowed, or the row takes the Split button's clicks.)
			const bool open = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
			if (own && self.Squads.size() > 1) {
				ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Split").x - ImGui::GetStyle().FramePadding.x * 2.0F);
				if (ToolUI::SmallButton("Split")) {
					const long keep = self.Id;
					if (SplitSquad(game, army, squad)) {
						s_Screen.Note = "The squad is now " + game.Armies.back().Name + ", on the same spot.";
					}
					s_Screen.SelectedArmy = keep;
					if (open) {
						ImGui::TreePop();
					}
					ImGui::PopID();
					return;
				}
				ImGui::SetItemTooltip("This squad leaves to be an army of its own, here.");
			}
			if (open) {
				for (const Soldier& soldier: members.Soldiers) {
					ImGui::Text("%s %s", RankName(soldier.Rank), soldier.Name.c_str());
					ImGui::SameLine();
					ImGui::TextDisabled("%d kills", soldier.Kills);
					ImGui::SameLine();
					HealthBar(soldier.Health, ImGui::GetFontSize() * 4.0F);
				}
				ImGui::TreePop();
			} else {
				ImGui::SameLine();
				HealthBar(health, ImGui::GetFontSize() * 3.0F);
			}
			ImGui::PopID();
		}
		if (own) {
			ImGui::Spacing();
			if (ToolUI::SmallButton("Disband army")) {
				s_Screen.ConfirmDisband = true;
			}
		}
	}

	void PointPanel(int planet, int point) {
		CampaignModel::Campaign& game = s_Screen.Game;
		Point& here = game.Planets[planet].Points[point];
		const int player = Player();
		ImGui::SeparatorText(here.Name.c_str());
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(FactionColor(here.Owner)), "%s", FactionLabel(here.Owner).c_str());
		ImGui::TextDisabled("%s on %s", TerrainName(game.Planets[planet].Type, here.Terrain), game.Planets[planet].Name.c_str());
		ImGui::Text("%s%s", here.Settlement > 0 ? SettlementName(here.Settlement) : "No settlement", here.Owner >= 0 ? (", " + std::to_string(PointIncome(game, planet, point)) + " gold a turn").c_str() : "");
		{
			const Resource deposit = static_cast<Resource>(std::clamp(here.Deposit, 0, c_ResourceCount - 1));
			ResourceIcon(deposit);
			ImGui::SameLine();
			if (here.Owner >= 0) {
				ImGui::Text("%d %s a turn", PointYield(game, planet, point), ResourceName(deposit));
			} else {
				ImGui::Text("%s deposit, %d a turn to its owner", ResourceName(deposit), here.Yield + 1);
			}
			ImGui::SetItemTooltip("%s\nIts yield is %d, one more with a settlement, half while its planet is invaded.", ResourceUse(deposit), here.Yield);
		}
		if (here.Militia > 0.01F) {
			ImGui::TextDisabled("Locals defending: strength %.0f", PointDefence(game, planet, point));
		}
		std::string built;
		for (int b = 0; b < static_cast<int>(Building::Count); ++b) {
			if (here.Built[b]) {
				built += (built.empty() ? "" : ", ") + std::string(BuildingName(static_cast<Building>(b)));
			}
		}
		if (!built.empty()) {
			ImGui::TextDisabled("Built: %s", built.c_str());
		}
		if (here.Owner != player || player < 0) {
			return;
		}
		const Faction& self = game.Factions[player];
		if (here.Settlement < c_MaxSettlement) {
			const Cost cost = SettleCostFor(game, player, here.Settlement);
			const std::string label = (here.Settlement == 0 ? std::string("Found an outpost") : std::string("Grow into a ") + SettlementName(here.Settlement + 1)) + " (" + CostText(cost) + ")";
			ImGui::BeginDisabled(!CanAfford(game, player, cost));
			if (ToolUI::Button(label.c_str(), ImVec2(-1.0F, 0.0F))) {
				Settle(game, player, planet, point);
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("A settlement gives more gold, defends itself with locals, and is where squads are recruited. Each level allows one more building.");
		}
		if (here.Settlement > 0) {
			int builtCount = 0;
			for (bool each: here.Built) {
				builtCount += each ? 1 : 0;
			}
			ImGui::SeparatorText("Build");
			ImGui::TextDisabled("%d of %d building slots used", builtCount, here.Settlement + 1);
			for (int b = 0; b < static_cast<int>(Building::Count); ++b) {
				const Building building = static_cast<Building>(b);
				if (here.Built[b]) {
					continue;
				}
				ImGui::PushID(b);
				const Cost cost = BuildingCostFor(game, player, building);
				const std::string label = std::string(BuildingName(building)) + " (" + CostText(cost) + ")";
				ImGui::BeginDisabled(!CanAfford(game, player, cost) || builtCount >= here.Settlement + 1);
				if (ToolUI::Button(label.c_str(), ImVec2(-1.0F, 0.0F))) {
					Build(game, player, planet, point, building);
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("%s", BuildingTip(building));
				ImGui::PopID();
			}
			ImGui::SeparatorText("Recruit");
			for (int type = 0; type < static_cast<int>(self.Squads.size()); ++type) {
				const SquadType& squad = self.Squads[type];
				std::string why;
				const bool can = CanRecruit(game, player, planet, point, type, why);
				const Cost cost = RecruitCostFor(game, player, type);
				ImGui::PushID(type);
				ImGui::BeginDisabled(!can);
				const std::string label = squad.Name + " x" + std::to_string(squad.Size) + "  (" + CostText(cost) + ")";
				if (ToolUI::Button(label.c_str(), ImVec2(-1.0F, 0.0F))) {
					Recruit(game, player, planet, point, type);
					const std::vector<int> armies = ArmiesAt(game, planet, point);
					for (int army: armies) {
						if (game.Armies[army].Faction == player) {
							s_Screen.SelectedArmy = game.Armies[army].Id;
						}
					}
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Tier %d. %d soldiers at %.0f gold each.%s%s", squad.Tier, squad.Size, squad.SoldierCost, can ? "" : "\n", why.c_str());
				ImGui::PopID();
			}
		}
	}

	void PlanetPanel(int planet) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		const Planet& here = game.Planets[planet];
		const PlanetControl control = ControlOf(game, planet);
		ImGui::SeparatorText(here.Name.c_str());
		ImGui::TextDisabled("%s%s", PlanetTypeName(here.Type), here.Parent >= 0 ? (", a moon of " + game.Planets[here.Parent].Name).c_str() : "");
		if (here.Points.empty()) {
			ImGui::TextWrapped("Nothing to land on. Its moons can be.");
			return;
		}
		if (control.Controller >= 0) {
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(FactionColor(control.Controller)), "Controlled by %s", FactionLabel(control.Controller).c_str());
			if (control.Invaded) {
				ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "Invaded: half income, no top-tier recruits,\nslower healing, until the invaders are out.");
			}
		} else {
			ImGui::TextDisabled(control.Contested ? "Contested: nobody holds more than half." : "Unclaimed.");
		}
		for (int point = 0; point < static_cast<int>(here.Points.size()); ++point) {
			ImGui::PushID(point);
			ImGui::ColorButton("##owner", ImGui::ColorConvertU32ToFloat4(FactionColor(here.Points[point].Owner)), ImGuiColorEditFlags_NoTooltip, ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
			ImGui::SameLine();
			if (ImGui::Selectable(here.Points[point].Name.c_str(), s_Screen.SelectedPoint == point && s_Screen.SelectedPlanet == planet)) {
				s_Screen.SelectedPlanet = planet;
				s_Screen.SelectedPoint = point;
			}
			ImGui::PopID();
		}
		if (s_Screen.Showing != View::Planet && ToolUI::Button("Open its map", ImVec2(-1.0F, 0.0F))) {
			ShowPlanet(planet);
		}
	}

	void NextArmy();
	int TurnsLeftOf(const CampaignModel::Campaign& game, int faction, Resource resource);

	/// What needs doing this turn, most pressing first: each line takes the player to it when clicked.
	void ToDoList() {
		CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		if (player < 0 || game.Winner >= 0 || game.Lost) {
			return;
		}
		struct Item {
			std::string Text;
			ImVec4 Color;
			std::function<void()> Go;
		};
		std::vector<Item> items;
		const ImVec4 urgent(1.0F, 0.55F, 0.45F, 1.0F);
		const ImVec4 normal(0.85F, 0.88F, 1.0F, 1.0F);
		// Planets of the player's that are invaded.
		for (int planet = 0; planet < static_cast<int>(game.Planets.size()) && items.size() < 2; ++planet) {
			const PlanetControl control = ControlOf(game, planet);
			if (control.Controller == player && control.Invaded) {
				items.push_back({"Drive the invaders off " + game.Planets[planet].Name, urgent, [planet]() { ShowPlanet(planet); }});
			}
		}
		// Enemy armies where the player holds points.
		for (const Army& army: game.Armies) {
			if (army.Faction == player || Friendly(game, player, army.Faction) || items.size() >= 3) {
				continue;
			}
			bool threat = false;
			for (int planet: game.Systems[army.System].Planets) {
				for (const Point& point: game.Planets[planet].Points) {
					threat = threat || point.Owner == player;
				}
			}
			if (threat) {
				const int system = army.System;
				const long id = army.Id;
				items.push_back({FactionLabel(army.Faction) + " army at " + game.Systems[system].Name, urgent, [system, id]() {
					ShowSystem(system);
					s_Screen.SelectedArmy = id;
				}});
			}
		}
		// Food running out, or no fuel to jump with.
		if (const int turns = TurnsLeftOf(game, player, Resource::Food); turns >= 0 && turns <= 3) {
			items.push_back({turns == 0 ? std::string("Out of food: armies go hungry. Buy food (Trade)") : "Food runs out in " + std::to_string(turns) + (turns == 1 ? " turn" : " turns") + ": buy some or take farms", urgent, []() { s_Screen.MarketOpen = true; }});
		}
		{
			const size_t fuel = static_cast<size_t>(Resource::Fuel);
			int jumping = 0;
			for (const Army& army: game.Armies) {
				jumping += army.Faction == player && army.Planet < 0 ? 1 : 0;
			}
			if (jumping > 0 && game.Factions[player].Resources[fuel] < jumping && ProductionOf(game, player)[fuel] < jumping) {
				items.push_back({game.Factions[player].Resources[fuel] == 0 ? std::string("No fuel: armies can't jump lanes. Buy fuel (Trade)") : std::string("Fuel is low for your armies in space (Trade)"), urgent, []() { s_Screen.MarketOpen = true; }});
			}
		}
		if (const int income = IncomeOf(game, player); income < 0) {
			items.push_back({"Losing " + std::to_string(-income) + " gold a turn: take points, or disband", urgent, nullptr});
		}
		int ready = 0;
		for (const Army& army: game.Armies) {
			ready += army.Faction == player && army.Moves > 0 && army.Path.empty() ? 1 : 0;
		}
		if (ready > 0) {
			items.push_back({std::to_string(ready) + (ready == 1 ? " army" : " armies") + " can still move (Tab)", normal, []() { NextArmy(); }});
		}
		// Armies that can be refilled where they stand.
		for (int army = 0; army < static_cast<int>(game.Armies.size()) && items.size() < 5; ++army) {
			const Army& each = game.Armies[army];
			if (each.Faction != player || each.Planet < 0) {
				continue;
			}
			const Point& point = game.Planets[each.Planet].Points[each.Point];
			const int cost = ReplenishCost(game, army);
			if (cost > 0 && point.Owner == player && point.Settlement > 0 && game.Factions[player].Gold >= cost) {
				const long id = each.Id;
				const int planet = each.Planet;
				const int at = each.Point;
				items.push_back({"Refill " + each.Name + " (" + std::to_string(cost) + " gold)", normal, [id, planet, at]() {
					s_Screen.SelectedArmy = id;
					ShowPlanet(planet);
					s_Screen.SelectedPoint = at;
				}});
				break;
			}
		}
		if (game.Factions[player].Gold >= 600) {
			// A settlement of the player's to spend it at.
			for (int planet = 0; planet < static_cast<int>(game.Planets.size()); ++planet) {
				const std::vector<Point>& points = game.Planets[planet].Points;
				const auto found = std::find_if(points.begin(), points.end(), [player](const Point& point) { return point.Owner == player && point.Settlement > 0; });
				if (found != points.end()) {
					const int at = static_cast<int>(found - points.begin());
					items.push_back({std::to_string(game.Factions[player].Gold) + " gold to spend: recruit, settle or build", normal, [planet, at]() {
						ShowPlanet(planet);
						s_Screen.SelectedPoint = at;
					}});
					break;
				}
			}
		}
		ImGui::SeparatorText("To do");
		BeaconItem(s_Screen.Lights.ToDo);
		if (items.empty()) {
			ImGui::TextColored(ImVec4(0.5F, 1.0F, 0.5F, 1.0F), "All done: End turn (Enter)");
		}
		for (size_t index = 0; index < items.size() && index < 5; ++index) {
			const Item& item = items[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::PushStyleColor(ImGuiCol_Text, item.Color);
			const std::string label = (item.Go ? "> " : "  ") + item.Text;
			if (ImGui::Selectable(label.c_str(), false, item.Go ? 0 : ImGuiSelectableFlags_Disabled) && item.Go) {
				item.Go();
			}
			ImGui::PopStyleColor();
			ImGui::PopID();
		}
	}

	/// Picks an army and shows it on the map, at the closest view that has it.
	void GoToArmy(long id) {
		const int army = FindArmy(s_Screen.Game, id);
		if (army < 0) {
			return;
		}
		const Army& each = s_Screen.Game.Armies[army];
		s_Screen.SelectedArmy = id;
		if (each.Planet >= 0) {
			ShowPlanet(each.Planet);
			s_Screen.SelectedPoint = each.Point;
		} else {
			ShowSystem(each.System);
			s_Screen.SelectedPlanet = s_Screen.SelectedPoint = -1;
		}
	}

	/// Every army of the player's, two lines each: its moves left, name, soldiers and health, then where it is and what it's doing.
	/// Click one to pick it and go to it; the mouse on one points it out on the map.
	void Roster() {
		const CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		std::vector<int> own;
		for (int army = 0; army < static_cast<int>(game.Armies.size()); ++army) {
			if (game.Armies[army].Faction == player) {
				own.push_back(army);
			}
		}
		int soldiers = 0;
		for (int army: own) {
			soldiers += SoldierCount(game.Armies[army]);
		}
		const std::string title = "Your armies (" + std::to_string(own.size()) + ", " + std::to_string(soldiers) + " soldiers)";
		ImGui::SeparatorText(title.c_str());
		if (own.empty()) {
			ImGui::TextDisabled("None. Recruit a squad at one of your settlements.");
			return;
		}
		const float line = ImGui::GetTextLineHeight();
		const float rowHeight = line * 2.0F + 4.0F;
		const float height = std::min(static_cast<float>(own.size()), 4.5F) * (rowHeight + ImGui::GetStyle().ItemSpacing.y);
		ImGui::BeginChild("##roster", ImVec2(0.0F, height));
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (int army: own) {
			const Army& each = game.Armies[army];
			ImGui::PushID(static_cast<int>(each.Id));
			const bool selected = each.Id == s_Screen.SelectedArmy;
			if (ImGui::Selectable("##army", selected, 0, ImVec2(0.0F, rowHeight))) {
				GoToArmy(each.Id);
			}
			if (ImGui::IsItemHovered()) {
				s_Screen.RosterHoverNext = each.Id;
				ImGui::SetTooltip("%s: %d squads, strength %.0f\nClick to go to it.", each.Name.c_str(), static_cast<int>(each.Squads.size()), Strength(game, each));
			}
			const ImVec2 from = ImGui::GetItemRectMin();
			const ImVec2 to = ImGui::GetItemRectMax();
			// Pips for its moves left.
			const bool moving = !each.Path.empty();
			for (int pip = 0; pip < c_MovesPerTurn; ++pip) {
				const ImVec2 dot(from.x + 6.0F + static_cast<float>(pip) * 8.0F, from.y + 2.0F + line * 0.5F);
				drawList->AddCircleFilled(dot, 3.0F, pip < each.Moves && !moving ? IM_COL32(120, 255, 140, 255) : IM_COL32(70, 70, 80, 255), 8);
			}
			const float textX = from.x + 8.0F + static_cast<float>(c_MovesPerTurn) * 8.0F;
			drawList->AddText(ImVec2(textX, from.y + 2.0F), selected ? IM_COL32(255, 235, 140, 255) : IM_COL32(255, 255, 255, 255), each.Name.c_str());
			// Soldiers and their health, on the right.
			float health = 0.0F;
			int count = 0;
			for (const Squad& squad: each.Squads) {
				for (const Soldier& soldier: squad.Soldiers) {
					health += soldier.Health;
					++count;
				}
			}
			health = count > 0 ? health / static_cast<float>(count) : 0.0F;
			const float barWidth = line * 2.5F;
			const ImVec2 barFrom(to.x - barWidth - 4.0F, from.y + 2.0F + line * 0.3F);
			drawList->AddRectFilled(barFrom, ImVec2(barFrom.x + barWidth, barFrom.y + line * 0.4F), IM_COL32(40, 40, 50, 255));
			drawList->AddRectFilled(barFrom, ImVec2(barFrom.x + barWidth * health, barFrom.y + line * 0.4F), health > 0.66F ? IM_COL32(90, 200, 90, 255) : health > 0.33F ? IM_COL32(230, 190, 60, 255) : IM_COL32(220, 70, 60, 255));
			const std::string men = std::to_string(count);
			const ImVec2 menSize = ImGui::CalcTextSize(men.c_str());
			drawList->AddText(ImVec2(barFrom.x - menSize.x - 6.0F, from.y + 2.0F), IM_COL32(220, 225, 240, 255), men.c_str());
			// Where it is and what it's doing.
			std::string status;
			ImU32 statusColor;
			if (moving) {
				status = "on its way to " + game.Systems[each.Path.back()].Name;
				statusColor = IM_COL32(255, 220, 90, 255);
			} else if (each.Moves > 0) {
				status = "ready";
				statusColor = IM_COL32(120, 255, 140, 255);
			} else {
				status = "done this turn";
				statusColor = IM_COL32(150, 150, 165, 255);
			}
			const std::string where = WhereIs(each) + ", ";
			drawList->PushClipRect(from, to, true);
			drawList->AddText(ImVec2(textX, from.y + 2.0F + line), IM_COL32(160, 165, 185, 255), where.c_str());
			drawList->AddText(ImVec2(textX + ImGui::CalcTextSize(where.c_str()).x, from.y + 2.0F + line), statusColor, status.c_str());
			drawList->PopClipRect();
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	/// Turns until a faction runs out of a resource at its rate now, or -1 if it isn't running down.
	int TurnsLeftOf(const CampaignModel::Campaign& game, int faction, Resource resource) {
		const size_t index = static_cast<size_t>(resource);
		const int net = ProductionOf(game, faction)[index] - UsageOf(game, faction)[index];
		if (net >= 0) {
			return -1;
		}
		return game.Factions[faction].Resources[index] / -net;
	}

	/// The two tiers of a resource's production bonus, lit where the faction has them.
	void BonusLines(const CampaignModel::Campaign& game, int faction, Resource resource) {
		const int tier = faction >= 0 ? BonusTier(game, faction, resource) : 0;
		for (int each = 1; each <= 2; ++each) {
			const std::string text = std::to_string(BonusThreshold(resource, each)) + "+ a turn: " + BonusText(resource, each);
			if (tier >= each) {
				ImGui::TextColored(ImVec4(0.55F, 1.0F, 0.55F, 1.0F), "* %s", text.c_str());
			} else {
				ImGui::TextDisabled("  %s", text.c_str());
			}
		}
	}

	/// Everything about one of the player's resources, for a tooltip.
	void ResourceTooltip(int player, Resource resource) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		const size_t index = static_cast<size_t>(resource);
		const int made = ProductionOf(game, player)[index];
		const int used = UsageOf(game, player)[index];
		ImGui::BeginTooltip();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ResourceColor(resource)), "%s", ResourceName(resource));
		ImGui::TextUnformatted(ResourceUse(resource));
		ImGui::Text("%d in store. Your points make %d a turn%s.", game.Factions[player].Resources[index], made, used > 0 ? (", your armies use " + std::to_string(used)).c_str() : "");
		if (const int turns = TurnsLeftOf(game, player, resource); turns >= 0) {
			ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "Runs out in %d turn%s.", turns, turns == 1 ? "" : "s");
		}
		ImGui::SeparatorText("Production bonuses");
		BonusLines(game, player, resource);
		ImGui::TextDisabled("Buy and sell it on the galaxy market (Trade).");
		ImGui::EndTooltip();
	}

	/// The player's stock of each resource and its change a turn, in two rows under their gold.
	void ResourceBar(int player) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		const Stock made = ProductionOf(game, player);
		const Stock used = UsageOf(game, player);
		if (!ImGui::BeginTable("##resources", 3, ImGuiTableFlags_SizingStretchSame)) {
			return;
		}
		for (int each = 0; each < c_ResourceCount; ++each) {
			const Resource resource = static_cast<Resource>(each);
			const int stock = game.Factions[player].Resources[each];
			const int net = made[each] - used[each];
			const int turns = TurnsLeftOf(game, player, resource);
			const bool low = turns >= 0 && turns <= 3;
			ImGui::TableNextColumn();
			ImGui::PushID(each);
			ImGui::BeginGroup();
			ResourceIcon(resource);
			ImGui::SameLine(0.0F, 3.0F);
			if (low) {
				const float blink = 0.6F + 0.4F * std::sin(static_cast<float>(ImGui::GetTime()) * 5.0F);
				ImGui::TextColored(ImVec4(1.0F, 0.4F * blink, 0.3F * blink, 1.0F), "%d", stock);
			} else {
				ImGui::Text("%d", stock);
			}
			if (net != 0) {
				ImGui::SameLine(0.0F, 2.0F);
				ImGui::TextColored(net > 0 ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) : ImVec4(1.0F, 0.55F, 0.45F, 1.0F), "%+d", net);
			}
			if (BonusTier(game, player, resource) > 0) {
				ImGui::SameLine(0.0F, 2.0F);
				ImGui::TextColored(ImVec4(0.55F, 1.0F, 0.55F, 1.0F), "*");
			}
			ImGui::EndGroup();
			if (ImGui::IsItemHovered()) {
				ResourceTooltip(player, resource);
			}
			if (ImGui::IsItemClicked()) {
				s_Screen.MarketOpen = true;
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	/// The wider galaxy's market: buying and selling resources for gold, what's moving prices, and the production bonuses.
	/// Every army of the player's in one place: what each is made of, where it is, whether it has moved this turn, and its squads and
	/// soldiers, with splitting, refilling and going to it.
	void MilitaryWindow() {
		if (!s_Screen.MilitaryOpen) {
			return;
		}
		CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		if (player < 0) {
			s_Screen.MilitaryOpen = false;
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 52.0F, ImGui::GetFontSize() * 30.0F), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
		if (!ImGui::Begin("Military###CampaignMilitary", &s_Screen.MilitaryOpen, ImGuiWindowFlags_NoCollapse)) {
			ImGui::End();
			return;
		}
		const Faction& self = game.Factions[player];
		std::vector<int> own;
		int soldiers = 0;
		int ready = 0;
		int moving = 0;
		int done = 0;
		for (int army = 0; army < static_cast<int>(game.Armies.size()); ++army) {
			const Army& each = game.Armies[army];
			if (each.Faction != player) {
				continue;
			}
			own.push_back(army);
			soldiers += SoldierCount(each);
			if (!each.Path.empty()) {
				++moving;
			} else if (each.Moves > 0) {
				++ready;
			} else {
				++done;
			}
		}
		ImGui::Text("%d armies, %d soldiers. Upkeep %d gold and %d food a turn.", static_cast<int>(own.size()), soldiers, UpkeepOf(game, player), UsageOf(game, player)[static_cast<size_t>(Resource::Food)]);
		ImGui::TextColored(ImVec4(0.47F, 1.0F, 0.55F, 1.0F), "%d ready", ready);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0F, 0.86F, 0.35F, 1.0F), "%d on the move", moving);
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.6F, 0.6F, 0.65F, 1.0F), "%d done this turn", done);
		ImGui::SameLine();
		ImGui::Checkbox("Only ready armies", &s_Screen.MilitaryReadyOnly);
		ImGui::SameLine();
		ImGui::BeginDisabled(ready == 0);
		if (ToolUI::SmallButton("Next ready army (Tab)")) {
			NextArmy();
		}
		ImGui::EndDisabled();
		if (own.empty()) {
			ImGui::TextDisabled("You have no armies. Recruit a squad at one of your settlements.");
			ImGui::End();
			return;
		}

		// Changes wait until the table is drawn: they add and remove armies.
		enum class Action { None, Split, Refill, Disband };
		Action action = Action::None;
		long actionArmy = 0;
		int actionSquad = -1;

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
		if (ImGui::BeginTable("##military", 8, flags, ImVec2(0.0F, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 1.4F))) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Army", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("This turn");
			ImGui::TableSetupColumn("Where");
			ImGui::TableSetupColumn("Made of");
			ImGui::TableSetupColumn("Soldiers");
			ImGui::TableSetupColumn("Health");
			ImGui::TableSetupColumn("Strength");
			ImGui::TableSetupColumn("##do");
			ImGui::TableHeadersRow();
			for (int army: own) {
				const Army& each = game.Armies[army];
				const bool isMoving = !each.Path.empty();
				if (s_Screen.MilitaryReadyOnly && (isMoving || each.Moves <= 0)) {
					continue;
				}
				ImGui::PushID(static_cast<int>(each.Id));
				ImGui::TableNextRow();
				if (each.Id == s_Screen.SelectedArmy) {
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(90, 80, 30, 120));
				}
				// What it's made of, and how hurt it is.
				std::vector<std::pair<int, int>> types; // Squad type, how many.
				float health = 0.0F;
				int men = 0;
				for (const Squad& squad: each.Squads) {
					auto found = std::find_if(types.begin(), types.end(), [&squad](const std::pair<int, int>& type) { return type.first == squad.Type; });
					if (found == types.end()) {
						types.emplace_back(squad.Type, 1);
					} else {
						++found->second;
					}
					for (const Soldier& soldier: squad.Soldiers) {
						health += soldier.Health;
						++men;
					}
				}
				health = men > 0 ? health / static_cast<float>(men) : 0.0F;
				std::string makeup;
				for (const auto& [type, count]: types) {
					const std::string name = self.Squads[std::clamp(type, 0, static_cast<int>(self.Squads.size()) - 1)].Name;
					makeup += (makeup.empty() ? "" : ", ") + (count > 1 ? std::to_string(count) + "x " : std::string()) + name;
				}

				ImGui::TableNextColumn();
				const bool open = ImGui::TreeNodeEx(each.Name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
				ImGui::SetItemTooltip("Open to see its squads and soldiers.");
				ImGui::TableNextColumn();
				if (isMoving) {
					ImGui::TextColored(ImVec4(1.0F, 0.86F, 0.35F, 1.0F), "To %s", game.Systems[each.Path.back()].Name.c_str());
					ImGui::SetItemTooltip("On its way: %d jump%s to go.", static_cast<int>(each.Path.size()), each.Path.size() == 1 ? "" : "s");
				} else if (each.Moves > 0) {
					ImGui::TextColored(ImVec4(0.47F, 1.0F, 0.55F, 1.0F), "Ready (%d move%s)", each.Moves, each.Moves == 1 ? "" : "s");
				} else {
					ImGui::TextDisabled("Done");
				}
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(WhereIs(each).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(makeup.c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%d", men);
				ImGui::TableNextColumn();
				HealthBar(health, ImGui::GetFontSize() * 4.0F);
				ImGui::SetItemTooltip("%d%% health on average.", static_cast<int>(std::round(health * 100.0F)));
				ImGui::TableNextColumn();
				ImGui::Text("%.0f", Strength(game, each));
				ImGui::TableNextColumn();
				if (ToolUI::SmallButton("Go to")) {
					GoToArmy(each.Id);
				}
				ImGui::SetItemTooltip("Picks it and shows it on the map.");
				const int refill = ReplenishCost(game, army);
				const bool canRefill = refill > 0 && each.Planet >= 0 && game.Planets[each.Planet].Points[each.Point].Owner == player && game.Planets[each.Planet].Points[each.Point].Settlement > 0 && self.Gold >= refill;
				if (canRefill) {
					ImGui::SameLine();
					const std::string label = "Refill (" + std::to_string(refill) + ")";
					if (ToolUI::SmallButton(label.c_str())) {
						action = Action::Refill;
						actionArmy = each.Id;
					}
					ImGui::SetItemTooltip("Fills its squads back up with new soldiers here.");
				}

				if (open) {
					for (int squad = 0; squad < static_cast<int>(each.Squads.size()); ++squad) {
						const Squad& members = each.Squads[squad];
						const SquadType& type = self.Squads[std::clamp(members.Type, 0, static_cast<int>(self.Squads.size()) - 1)];
						float squadHealth = 0.0F;
						for (const Soldier& soldier: members.Soldiers) {
							squadHealth += soldier.Health;
						}
						squadHealth /= std::max(1.0F, static_cast<float>(members.Soldiers.size()));
						ImGui::PushID(squad);
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::Indent();
						const std::string header = type.Name + "###squad";
						const bool squadOpen = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap);
						ImGui::Unindent();
						ImGui::TableNextColumn();
						ImGui::TextDisabled("Tier %d", type.Tier);
						ImGui::TableNextColumn();
						ImGui::TableNextColumn();
						ImGui::TableNextColumn();
						ImGui::Text("%d/%d", static_cast<int>(members.Soldiers.size()), type.Size);
						ImGui::TableNextColumn();
						HealthBar(squadHealth, ImGui::GetFontSize() * 4.0F);
						ImGui::TableNextColumn();
						ImGui::TableNextColumn();
						if (each.Squads.size() > 1) {
							if (ToolUI::SmallButton("Split off")) {
								action = Action::Split;
								actionArmy = each.Id;
								actionSquad = squad;
							}
							ImGui::SetItemTooltip("This squad leaves to be an army of its own, on the same spot, with the same moves left.");
						}
						if (squadOpen) {
							for (const Soldier& soldier: members.Soldiers) {
								ImGui::TableNextRow();
								ImGui::TableNextColumn();
								ImGui::Indent(ImGui::GetFontSize() * 2.0F);
								ImGui::Text("%s %s", RankName(soldier.Rank), soldier.Name.c_str());
								ImGui::Unindent(ImGui::GetFontSize() * 2.0F);
								ImGui::TableNextColumn();
								ImGui::TextDisabled("%d kills", soldier.Kills);
								ImGui::TableNextColumn();
								ImGui::TableNextColumn();
								ImGui::TableNextColumn();
								ImGui::TableNextColumn();
								HealthBar(soldier.Health, ImGui::GetFontSize() * 4.0F);
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::TextDisabled("Click an army's name to open it. Split a squad off to send it somewhere else; to put armies together, pick one and use Take in on its panel.");
		ImGui::End();

		if (const int army = FindArmy(game, actionArmy); army >= 0) {
			if (action == Action::Split && SplitSquad(game, army, actionSquad)) {
				s_Screen.Note = "The squad is now " + game.Armies.back().Name + ", on the same spot.";
				s_Screen.SelectedArmy = game.Armies.back().Id;
			} else if (action == Action::Refill) {
				Replenish(game, army);
			}
		}
	}

	void MarketWindow() {
		if (!s_Screen.MarketOpen) {
			return;
		}
		CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		if (player < 0) {
			s_Screen.MarketOpen = false;
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 40.0F, 0.0F), ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
		if (!ImGui::Begin("Galaxy market###CampaignMarket", &s_Screen.MarketOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::End();
			return;
		}
		Faction& self = game.Factions[player];
		ImGui::Text("Gold %d", self.Gold);
		ImGui::SameLine();
		ImGui::TextDisabled("Traders take a cut both ways. Buying pushes a price up and selling pushes it down; prices drift back each turn.");
		const Stock made = ProductionOf(game, player);
		const Stock used = UsageOf(game, player);
		if (ImGui::BeginTable("##market", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableSetupColumn("Resource");
			ImGui::TableSetupColumn("Stock");
			ImGui::TableSetupColumn("A turn");
			ImGui::TableSetupColumn("Buy");
			ImGui::TableSetupColumn("Sell");
			ImGui::TableSetupColumn("##trade");
			ImGui::TableHeadersRow();
			for (int each = 0; each < c_ResourceCount; ++each) {
				const Resource resource = static_cast<Resource>(each);
				ImGui::PushID(each);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ResourceIcon(resource);
				ImGui::SameLine();
				ImGui::TextUnformatted(ResourceName(resource));
				ImGui::SetItemTooltip("%s", ResourceUse(resource));
				ImGui::TableNextColumn();
				ImGui::Text("%d", self.Resources[each]);
				ImGui::TableNextColumn();
				const int net = made[each] - used[each];
				ImGui::TextColored(net > 0 ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) : net < 0 ? ImVec4(1.0F, 0.55F, 0.45F, 1.0F) : ImVec4(0.6F, 0.6F, 0.65F, 1.0F), "%+d", net);
				const int buy = BuyPrice(game, resource);
				const int sell = SellPrice(game, resource);
				const float against = game.Prices[each] / BasePrice(resource);
				const ImVec4 priceColor = against > 1.15F ? ImVec4(1.0F, 0.6F, 0.45F, 1.0F) : against < 0.87F ? ImVec4(0.55F, 1.0F, 0.6F, 1.0F) : ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
				ImGui::TableNextColumn();
				ImGui::TextColored(priceColor, "%d", buy);
				ImGui::SetItemTooltip("Normally about %.0f gold. %s", BasePrice(resource), against > 1.15F ? "Dear now." : against < 0.87F ? "Cheap now." : "");
				ImGui::TableNextColumn();
				ImGui::TextColored(priceColor, "%d", sell);
				ImGui::TableNextColumn();
				auto tradeButton = [&](const char* label, int amount, bool can) {
					ImGui::BeginDisabled(!can);
					if (ToolUI::SmallButton(label)) {
						const int before = self.Gold;
						const int had = self.Resources[each];
						if (Trade(game, player, resource, amount)) {
							s_Screen.Guide.Traded = true;
							const int units = std::abs(self.Resources[each] - had);
							const int gold = std::abs(self.Gold - before);
							s_Screen.Note = std::string(amount > 0 ? "Bought " : "Sold ") + std::to_string(units) + " " + ResourceName(resource) + " for " + std::to_string(gold) + " gold.";
						}
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
				};
				tradeButton("Buy 1", 1, self.Gold >= buy);
				tradeButton("Buy 5", 5, self.Gold >= buy);
				tradeButton("Sell 1", -1, self.Resources[each] > 0);
				tradeButton("Sell 5", -5, self.Resources[each] > 0);
				ImGui::NewLine();
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::SeparatorText("Galaxy news");
		if (game.Events.empty()) {
			ImGui::TextDisabled("Quiet: prices are near normal.");
		}
		for (const GalaxyEvent& event: game.Events) {
			ResourceIcon(static_cast<Resource>(std::clamp(event.Resource, 0, c_ResourceCount - 1)));
			ImGui::SameLine();
			ImGui::Text("%s", event.Text.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("(%d more turn%s)", event.TurnsLeft, event.TurnsLeft == 1 ? "" : "s");
		}
		ImGui::SeparatorText("Production bonuses");
		ImGui::TextDisabled("Making enough of a resource each turn, from points you hold, unlocks these. Lit ones are yours.");
		for (int each = 0; each < c_ResourceCount; ++each) {
			const Resource resource = static_cast<Resource>(each);
			ResourceIcon(resource);
			ImGui::SameLine();
			ImGui::Text("%s (you make %d)", ResourceName(resource), made[each]);
			ImGui::Indent();
			BonusLines(game, player, resource);
			ImGui::Unindent();
		}
		ImGui::End();
	}

	ImVec4 StanceColor(Stance stance) {
		switch (stance) {
			case Stance::War:
				return ImVec4(1.0F, 0.45F, 0.4F, 1.0F);
			case Stance::Peace:
				return ImVec4(0.6F, 0.85F, 1.0F, 1.0F);
			case Stance::Alliance:
				return ImVec4(0.5F, 1.0F, 0.55F, 1.0F);
			default:
				return ImVec4(0.85F, 0.85F, 0.9F, 1.0F);
		}
	}

	ImVec4 OpinionColor(int opinion) { return opinion < -40 ? ImVec4(1.0F, 0.4F, 0.35F, 1.0F) : opinion < 0 ? ImVec4(1.0F, 0.75F, 0.4F, 1.0F) : opinion < 40 ? ImVec4(0.85F, 0.9F, 1.0F, 1.0F) : ImVec4(0.5F, 1.0F, 0.55F, 1.0F); }

	/// What a deal gives one way, as text: "100 gold, 3 water", or "nothing".
	std::string DealSide(int gold, const Stock& stock) {
		const bool any = gold > 0 || std::any_of(stock.begin(), stock.end(), [](int each) { return each > 0; });
		if (!any) {
			return "nothing";
		}
		std::string text = CostText(Cost{gold, stock});
		if (gold <= 0) {
			text.erase(0, text.find(", ") == std::string::npos ? text.size() : text.find(", ") + 2);
		}
		return text;
	}

	/// The resources a faction has plenty of (spare) or too little of (short), as a row of icons.
	void ResourceIcons(int faction, bool spare) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		const Stock made = ProductionOf(game, faction);
		const Stock used = UsageOf(game, faction);
		bool any = false;
		for (int each = 0; each < c_ResourceCount; ++each) {
			const int stock = game.Factions[faction].Resources[each];
			const int net = made[each] - used[each];
			const bool relics = each == static_cast<int>(Resource::Relics);
			const bool show = spare ? stock >= (relics ? 2 : 12) : !relics && (stock < 2 || (net < 0 && stock < 6));
			if (show) {
				if (any) {
					ImGui::SameLine(0.0F, 2.0F);
				}
				ResourceIcon(static_cast<Resource>(each));
				ImGui::SetItemTooltip("%s: %d in store, %+d a turn", ResourceName(static_cast<Resource>(each)), stock, net);
				any = true;
			}
		}
		if (!any) {
			ImGui::TextDisabled("-");
		}
	}

	/// Factions: where you stand with each, deals and trade pacts, peace, alliances and war.
	void DiplomacyWindow() {
		if (!s_Screen.DiplomacyOpen) {
			return;
		}
		CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		if (player < 0) {
			s_Screen.DiplomacyOpen = false;
			return;
		}
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
		if (!ImGui::Begin("Diplomacy###CampaignDiplomacy", &s_Screen.DiplomacyOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::End();
			return;
		}
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 44.0F);
		ImGui::TextDisabled("Opinion rises with trade, gifts, a shared enemy and long peace, and falls with attacks, broken treaties, borders that touch and your growing strength. Allies can stand on each other's points, defend together and win together.");
		ImGui::PopTextWrapPos();
		const float ours = MightOf(game, player);
		if (ImGui::BeginTable("##factions", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableSetupColumn("Faction");
			ImGui::TableSetupColumn("Stance");
			ImGui::TableSetupColumn("Opinion");
			ImGui::TableSetupColumn("Strength");
			ImGui::TableSetupColumn("Spare");
			ImGui::TableSetupColumn("Short of");
			ImGui::TableSetupColumn("##do");
			ImGui::TableHeadersRow();
			for (int faction = 0; faction < static_cast<int>(game.Factions.size()); ++faction) {
				if (faction == player || !game.Factions[faction].Alive) {
					continue;
				}
				const Stance stance = StanceOf(game, player, faction);
				const Relation& relation = RelationOf(static_cast<const CampaignModel::Campaign&>(game), player, faction);
				ImGui::PushID(faction);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(FactionColor(faction)), "%s", FactionLabel(faction).c_str());
				ImGui::TableNextColumn();
				ImGui::TextColored(StanceColor(stance), "%s", StanceName(stance));
				ImGui::SetItemTooltip("For %d turn%s.", relation.Turns, relation.Turns == 1 ? "" : "s");
				ImGui::TableNextColumn();
				ImGui::TextColored(OpinionColor(relation.Opinion), "%s (%+d)", OpinionWord(relation.Opinion), relation.Opinion);
				ImGui::TableNextColumn();
				const float theirs = MightOf(game, faction);
				ImGui::Text("%.0f", theirs);
				ImGui::SetItemTooltip("Their armies' strength. Yours: %.0f.", ours);
				ImGui::TableNextColumn();
				ResourceIcons(faction, true);
				ImGui::TableNextColumn();
				ResourceIcons(faction, false);
				ImGui::TableNextColumn();
				auto answer = [&](const std::string& reply, const std::string& yes) {
					s_Screen.DiplomacyNote = FactionLabel(faction) + ": " + (reply.empty() ? yes : reply);
					s_Screen.Guide.Proposed = true;
				};
				if (ToolUI::SmallButton("Trade")) {
					s_Screen.DealWith = faction;
					s_Screen.DealDraft = Deal();
				}
				ImGui::SetItemTooltip("Put a deal to them: gold and resources either way, once or every turn.");
				ImGui::SameLine();
				if (stance == Stance::War || stance == Stance::Neutral) {
					if (ToolUI::SmallButton("Ask for peace")) {
						answer(ProposeStance(game, player, faction, Stance::Peace), "Agreed. Peace it is.");
					}
					ImGui::SetItemTooltip("At peace, neither side may attack the other.");
					ImGui::SameLine();
				}
				if (stance == Stance::Neutral || stance == Stance::Peace) {
					if (ToolUI::SmallButton("Ask to ally")) {
						answer(ProposeStance(game, player, faction, Stance::Alliance), "Agreed. We stand together.");
					}
					ImGui::SetItemTooltip("Needs them Friendly (opinion 50 or more).");
					ImGui::SameLine();
				}
				if (stance == Stance::Peace || stance == Stance::Alliance) {
					if (ToolUI::SmallButton("End treaty")) {
						EndTreaty(game, player, faction);
						s_Screen.DiplomacyNote = "You ended your " + std::string(stance == Stance::Alliance ? "alliance" : "peace") + " with " + FactionLabel(faction) + ".";
					}
					ImGui::SetItemTooltip("Back to Neutral, without attacking. They think less of you for it.");
					ImGui::SameLine();
				}
				if (stance != Stance::War) {
					if (ToolUI::SmallButton("Declare war")) {
						s_Screen.ConfirmWar = faction;
					}
					ImGui::SetItemTooltip("%s", stance == Stance::Neutral ? "Their armies and yours will fight. Everyone thinks a little less of you." : "Breaks your treaty: they and everyone else will trust you much less.");
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (!s_Screen.DiplomacyNote.empty()) {
			ImGui::TextColored(ImVec4(1.0F, 0.88F, 0.45F, 1.0F), "%s", s_Screen.DiplomacyNote.c_str());
		}

		// The deal being put together.
		const int with = s_Screen.DealWith;
		if (with >= 0 && with < static_cast<int>(game.Factions.size()) && game.Factions[with].Alive && with != player) {
			Deal& deal = s_Screen.DealDraft;
			deal.From = player;
			deal.To = with;
			const std::string title = "A deal with " + FactionLabel(with);
			ImGui::SeparatorText(title.c_str());
			if (ImGui::BeginTable("##deal", 3, ImGuiTableFlags_SizingFixedFit)) {
				ImGui::TableSetupColumn("##what");
				ImGui::TableSetupColumn("You give", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 11.0F);
				ImGui::TableSetupColumn("You get", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 11.0F);
				ImGui::TableHeadersRow();
				auto row = [&](const char* id, int& give, int& get, int mine, int theirs, int step) {
					ImGui::PushID(id);
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0F);
					ImGui::InputInt("##give", &give, step, step * 10);
					give = std::clamp(give, 0, std::max(mine, 0));
					ImGui::SameLine();
					ImGui::TextDisabled("/%d", mine);
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0F);
					ImGui::InputInt("##get", &get, step, step * 10);
					get = std::clamp(get, 0, std::max(theirs, 0));
					ImGui::SameLine();
					ImGui::TextDisabled("/%d", theirs);
					ImGui::PopID();
				};
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted("Gold");
				row("gold", deal.GoldGive, deal.GoldGet, game.Factions[player].Gold, game.Factions[with].Gold, 10);
				for (int each = 0; each < c_ResourceCount; ++each) {
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ResourceIcon(static_cast<Resource>(each));
					ImGui::SameLine();
					ImGui::TextUnformatted(ResourceName(static_cast<Resource>(each)));
					row(ResourceName(static_cast<Resource>(each)), deal.Give[each], deal.Get[each], game.Factions[player].Resources[each], game.Factions[with].Resources[each], 1);
				}
				ImGui::EndTable();
			}
			ImGui::Checkbox("Every turn for 10 turns (a trade pact)", &deal.Pact);
			ImGui::SetItemTooltip("The swap happens at the start of each turn while both sides can pay; it falls through if one can't.");
			const bool empty = deal.GoldGive <= 0 && deal.GoldGet <= 0 && std::all_of(deal.Give.begin(), deal.Give.end(), [](int each) { return each <= 0; }) && std::all_of(deal.Get.begin(), deal.Get.end(), [](int each) { return each <= 0; });
			if (!empty) {
				// How they'd take it: the same sum they decide by.
				const int leaning = DealValue(game, deal) + RelationOf(static_cast<const CampaignModel::Campaign&>(game), player, with).Opinion * 2;
				if (StanceOf(game, player, with) == Stance::War) {
					ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "They won't trade while you're at war.");
				} else if (leaning >= 0) {
					ImGui::TextColored(ImVec4(0.5F, 1.0F, 0.55F, 1.0F), "They look willing.");
				} else {
					ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.4F, 1.0F), "They'd want about %d gold's worth more.", (-leaning + 9) / 10 * 10);
				}
			}
			ImGui::BeginDisabled(empty);
			if (ToolUI::Button("Propose", ImVec2(ImGui::GetFontSize() * 7.0F, 0.0F))) {
				const std::string reply = ProposeDeal(game, deal);
				s_Screen.Guide.Proposed = true;
				s_Screen.DiplomacyNote = FactionLabel(with) + ": " + (reply.empty() ? std::string(deal.Pact ? "Agreed. The pact starts next turn." : "Agreed.") : reply);
				if (reply.empty()) {
					s_Screen.DealDraft = Deal();
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ToolUI::Button("Clear", ImVec2(ImGui::GetFontSize() * 5.0F, 0.0F))) {
				s_Screen.DealDraft = Deal();
			}
			ImGui::SameLine();
			if (ToolUI::Button("Close deal", ImVec2(ImGui::GetFontSize() * 6.0F, 0.0F))) {
				s_Screen.DealWith = -1;
			}
		}

		// Pacts running.
		bool header = false;
		for (int pact = 0; pact < static_cast<int>(game.Pacts.size()); ++pact) {
			const Deal& terms = game.Pacts[pact].Terms;
			if (terms.From != player && terms.To != player) {
				continue;
			}
			if (!header) {
				ImGui::SeparatorText("Trade pacts");
				header = true;
			}
			const bool giving = terms.From == player;
			const int other = giving ? terms.To : terms.From;
			const std::string give = giving ? DealSide(terms.GoldGive, terms.Give) : DealSide(terms.GoldGet, terms.Get);
			const std::string get = giving ? DealSide(terms.GoldGet, terms.Get) : DealSide(terms.GoldGive, terms.Give);
			ImGui::PushID(pact);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(FactionColor(other)), "%s", FactionLabel(other).c_str());
			ImGui::SameLine();
			ImGui::Text("You give %s, you get %s each turn (%d more turns).", give.c_str(), get.c_str(), game.Pacts[pact].TurnsLeft);
			ImGui::SameLine();
			if (ToolUI::SmallButton("Cancel")) {
				CancelPact(game, pact);
				ImGui::PopID();
				break;
			}
			ImGui::SetItemTooltip("Ends it now. They think less of you for it.");
			ImGui::PopID();
		}
		ImGui::End();
	}

	/// Making sure about a declaration of war.
	void WarWindow() {
		CampaignModel::Campaign& game = s_Screen.Game;
		if (s_Screen.ConfirmWar >= 0) {
			ImGui::OpenPopup("Declare war?###CampaignWar");
		}
		if (!ImGui::BeginPopupModal("Declare war?###CampaignWar", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		const int player = Player();
		const int target = s_Screen.ConfirmWar;
		if (player < 0 || target < 0 || target >= static_cast<int>(game.Factions.size())) {
			s_Screen.ConfirmWar = -1;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}
		const Stance stance = StanceOf(game, player, target);
		ImGui::Text("Declare war on %s?", FactionLabel(target).c_str());
		if (stance == Stance::Peace || stance == Stance::Alliance) {
			ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "This breaks your %s. They and every other faction will trust you much less.", stance == Stance::Alliance ? "alliance" : "peace");
		} else {
			ImGui::TextDisabled("Every other faction will think a little less of you, and their allies may join in.");
		}
		if (ToolUI::Button("Declare war", ImVec2(ImGui::GetFontSize() * 7.0F, 0.0F))) {
			DeclareWar(game, player, target);
			s_Screen.DiplomacyNote = "You are at war with " + FactionLabel(target) + ".";
			s_Screen.ConfirmWar = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ToolUI::Button("Not now", ImVec2(ImGui::GetFontSize() * 6.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			s_Screen.ConfirmWar = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	/// What the other factions put to the player between turns, one at a time after the battle reports.
	void OfferWindow() {
		CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		int index = -1;
		for (int offer = 0; offer < static_cast<int>(game.Offers.size()); ++offer) {
			if (game.Offers[offer].To == player) {
				index = offer;
				break;
			}
		}
		if (index < 0 || !game.Reports.empty() || !s_Screen.Alerts.empty() || s_Screen.Attack.Open || Animating() || game.Winner >= 0 || game.Lost) {
			return;
		}
		ImGui::OpenPopup("A message###CampaignOffer");
		if (!ImGui::BeginPopupModal("A message###CampaignOffer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		const Offer offer = game.Offers[index];
		const Stance stance = StanceOf(game, player, offer.From);
		const int opinion = RelationOf(static_cast<const CampaignModel::Campaign&>(game), player, offer.From).Opinion;
		ImGui::SetWindowFontScale(1.3F);
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(FactionColor(offer.From)), "%s", FactionLabel(offer.From).c_str());
		ImGui::SetWindowFontScale(1.0F);
		ImGui::SameLine();
		ImGui::TextColored(StanceColor(stance), "%s", StanceName(stance));
		ImGui::SameLine();
		ImGui::TextColored(OpinionColor(opinion), "%s", OpinionWord(opinion));
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0F);
		ImGui::Text("\"%s\"", offer.Text.c_str());
		std::string accept = "Accept";
		switch (offer.What) {
			case Offer::Kind::Deal:
				ImGui::Text("They give: %s", DealSide(offer.Terms.GoldGive, offer.Terms.Give).c_str());
				ImGui::Text("You give: %s", DealSide(offer.Terms.GoldGet, offer.Terms.Get).c_str());
				if (offer.Terms.Pact) {
					ImGui::TextDisabled("Every turn for %d turns, while both sides can pay.", c_PactTurns);
				}
				break;
			case Offer::Kind::Peace:
				ImGui::TextDisabled("At peace, neither side may attack the other.");
				accept = "Make peace";
				break;
			case Offer::Kind::Alliance:
				ImGui::TextDisabled("Allies don't fight, can stand on each other's points, defend together and win together.");
				accept = "Ally with them";
				break;
			case Offer::Kind::JoinWar:
				ImGui::TextDisabled("You would go to war with %s.%s", FactionLabel(offer.Against).c_str(), Friendly(game, player, offer.Against) ? " That breaks your treaty with them." : "");
				accept = "Join the war";
				break;
		}
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		if (ToolUI::Button(accept.c_str(), ImVec2(ImGui::GetFontSize() * 8.0F, 0.0F))) {
			AnswerOffer(game, index, true);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ToolUI::Button("Decline", ImVec2(ImGui::GetFontSize() * 6.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			AnswerOffer(game, index, false);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void SidePanel(float width, float height) {
		CampaignModel::Campaign& game = s_Screen.Game;
		ImGui::BeginChild("##side", ImVec2(width, height), ImGuiChildFlags_Borders);
		const int player = Player();
		if (player >= 0) {
			const Faction& self = game.Factions[player];
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(self.Color), "%s", self.Name.c_str());
			const int income = IncomeOf(game, player);
			const int shown = static_cast<int>(std::round(s_Screen.Track.ShownGold));
			if (shown != self.Gold) {
				ImGui::TextColored(ImVec4(1.0F, 0.9F, 0.4F, 1.0F), "Gold %d", shown);
			} else {
				ImGui::Text("Gold %d", self.Gold);
			}
			ImGui::SameLine();
			ImGui::TextColored(income >= 0 ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) : ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "(%+d a turn)", income);
			ImGui::SetItemTooltip("Income from your points and settlements, and %s, less upkeep of %d for your soldiers.", "60 for each planet you control and nobody has invaded", UpkeepOf(game, player));
			if (Shown(Step::Resources)) {
				ResourceBar(player);
				BeaconItem(s_Screen.Lights.Resources);
			}
			if (Shown(Step::Routine)) {
				ToDoList();
			}
			Roster();
		}
		ImGui::Separator();
		const int army = SelectedArmyIndex();
		if (army >= 0) {
			ArmyPanel(army);
		}
		const int armyNow = SelectedArmyIndex();
		int planet = s_Screen.SelectedPlanet;
		int point = s_Screen.SelectedPoint;
		if (armyNow >= 0 && game.Armies[armyNow].Planet >= 0 && point < 0) {
			planet = game.Armies[armyNow].Planet;
			point = game.Armies[armyNow].Point;
		}
		if (planet >= 0 && planet < static_cast<int>(game.Planets.size()) && point >= 0 && point < static_cast<int>(game.Planets[planet].Points.size())) {
			PointPanel(planet, point);
		} else if (s_Screen.SelectedPlanet >= 0 && s_Screen.SelectedPlanet < static_cast<int>(game.Planets.size())) {
			PlanetPanel(s_Screen.SelectedPlanet);
		} else if (s_Screen.SelectedSystem >= 0 && army < 0) {
			const StarSystem& system = game.Systems[s_Screen.SelectedSystem];
			ImGui::SeparatorText(system.Name.c_str());
			for (int each: system.Planets) {
				ImGui::PushID(each);
				if (ImGui::Selectable(game.Planets[each].Name.c_str())) {
					s_Screen.SelectedPlanet = each;
				}
				ImGui::PopID();
			}
			if (ToolUI::Button("Open the system's map", ImVec2(-1.0F, 0.0F))) {
				ShowSystem(s_Screen.SelectedSystem);
			}
		}
		if (army < 0 && planet < 0 && s_Screen.SelectedSystem < 0) {
			ImGui::TextWrapped("Click an army or a star. Double-click a star for its planets, and a planet for its points.\n\nWith one of your armies picked, right-click where it should go: a star to travel there along the lanes, a point on a planet to land or march there. Moving onto a defended point is a battle.\n\nHold more than half of a planet's points to control it.");
		}
		ImGui::EndChild();
	}
#pragma endregion

#pragma region Windows over the game
	/// Takes the view to a point on a planet, picks it, and (for a battle) flashes the battle there again.
	void TakeMeThere(int planet, int point, bool battle) {
		const CampaignModel::Campaign& game = s_Screen.Game;
		if (planet < 0 || planet >= static_cast<int>(game.Planets.size())) {
			return;
		}
		ShowPlanet(planet);
		s_Screen.SelectedPoint = point;
		if (battle) {
			s_Screen.Track.Pulses.push_back(Pulse{PulseKind::Battle, Place{game.Planets[planet].System, planet, point}, IM_COL32(255, 200, 90, 255), ImGui::GetTime()});
		}
	}

	void AttackWindow() {
		PendingAttack& attack = s_Screen.Attack;
		if (attack.Open) {
			ImGui::OpenPopup("Attack?###CampaignAttack");
		}
		if (!ImGui::BeginPopupModal("Attack?###CampaignAttack", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		CampaignModel::Campaign& game = s_Screen.Game;
		const int army = FindArmy(game, attack.ArmyID);
		if (army < 0 || attack.Planet < 0) {
			attack.Open = false;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}
		const Point& point = game.Planets[attack.Planet].Points[attack.Point];
		float defence = 0.0F;
		WouldFight(game, game.Armies[army].Faction, attack.Planet, attack.Point, defence);
		const float strength = Strength(game, game.Armies[army]);
		const float chance = WinChance(strength, defence);
		ImGui::Text("Attack %s on %s?", point.Name.c_str(), game.Planets[attack.Planet].Name.c_str());
		ImGui::Separator();
		ImGui::Text("%s: strength %.0f (%d soldiers)", game.Armies[army].Name.c_str(), strength, SoldierCount(game.Armies[army]));
		int defenders = 0;
		for (int other: ArmiesAt(game, attack.Planet, attack.Point)) {
			if (game.Armies[other].Faction != game.Armies[army].Faction && !Allied(game, game.Armies[other].Faction, game.Armies[army].Faction)) {
				defenders += SoldierCount(game.Armies[other]);
			}
		}
		ImGui::Text("Defenders (%s): strength %.0f (%d soldiers%s)", FactionLabel(point.Owner).c_str(), defence, defenders, point.Militia > 0.01F && point.Owner != game.Armies[army].Faction ? " and locals" : "");
		if (point.Settlement > 0 && point.Built[static_cast<size_t>(Building::Walls)]) {
			ImGui::TextDisabled("Walls: the defenders fight harder.");
		}
		const ImVec4 color = chance >= 0.7F ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) : chance >= 0.4F ? ImVec4(1.0F, 0.85F, 0.35F, 1.0F) : ImVec4(1.0F, 0.5F, 0.4F, 1.0F);
		ImGui::TextColored(color, "Chance of winning: about %d%%", static_cast<int>(std::round(chance * 100.0F)));
		const int faction = game.Armies[army].Faction;
		if (const int broken = TreatyBroken(game, faction, attack.Planet, attack.Point); broken >= 0) {
			ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.4F, 1.0F), "This breaks your %s with %s.\nThey and every other faction will trust you much less.", StanceOf(game, faction, broken) == Stance::Alliance ? "alliance" : "peace", FactionLabel(broken).c_str());
		} else if (point.Owner >= 0 && point.Owner != faction && StanceOf(game, faction, point.Owner) == Stance::Neutral) {
			ImGui::TextDisabled("You're neutral with %s: this starts a war, and others think a little less of you.", FactionLabel(point.Owner).c_str());
		}
		ImGui::Spacing();
		ImGui::BeginDisabled(true);
		ToolUI::Button("Fight it yourself", ImVec2(ImGui::GetFontSize() * 10.0F, 0.0F));
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Coming later: the battle played in Battle Command on a map of this point.");
		ImGui::SameLine();
		if (ToolUI::Button("Auto-resolve", ImVec2(ImGui::GetFontSize() * 8.0F, 0.0F))) {
			FollowNext(army);
			if (attack.Overland) {
				OrderOverland(game, army, attack.Point);
			} else {
				OrderLand(game, army, attack.Planet, attack.Point);
			}
			attack.Open = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ToolUI::Button("Cancel", ImVec2(ImGui::GetFontSize() * 5.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			attack.Open = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void ReportWindow() {
		CampaignModel::Campaign& game = s_Screen.Game;
		if (game.Reports.empty() || s_Screen.Attack.Open || Animating()) {
			return;
		}
		ImGui::OpenPopup("Battle report###CampaignReport");
		if (!ImGui::BeginPopupModal("Battle report###CampaignReport", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		const BattleOutcome& outcome = game.Reports.front();
		const int player = Player();
		const bool won = (outcome.Attacker == player) == outcome.AttackerWon;
		const bool playerIn = outcome.Attacker == player || outcome.Defender == player;
		ImGui::SetWindowFontScale(1.3F);
		ImGui::TextColored(!playerIn ? ImVec4(1.0F, 1.0F, 1.0F, 1.0F) : won ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) : ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "%s", !playerIn ? "Battle" : won ? "Victory" : "Defeat");
		ImGui::SetWindowFontScale(1.0F);
		ImGui::Text("Battle at %s", outcome.Where.c_str());
		ImGui::Separator();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(FactionColor(outcome.Attacker)), "%s attacked", FactionLabel(outcome.Attacker).c_str());
		ImGui::Text("  %d soldiers, strength %.0f: %d fell", outcome.AttackerSoldiers, outcome.AttackStrength, outcome.AttackerLost);
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(FactionColor(outcome.Defender)), "%s defended", outcome.Defender >= 0 ? FactionLabel(outcome.Defender).c_str() : "Locals");
		ImGui::Text("  %d soldiers, strength %.0f: %d fell", outcome.DefenderSoldiers, outcome.DefendStrength, outcome.DefenderLost);
		ImGui::Text("%s", outcome.AttackerWon ? "The attackers took the point." : "The defenders held.");
		if (!outcome.Promotions.empty()) {
			ImGui::SeparatorText("Promotions");
			for (const std::string& line: outcome.Promotions) {
				ImGui::BulletText("%s", line.c_str());
			}
		}
		ImGui::Spacing();
		const std::string label = game.Reports.size() > 1 ? "Next report (" + std::to_string(game.Reports.size() - 1) + " more)" : std::string("Continue");
		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F;
		ImGui::BeginDisabled(outcome.Planet < 0);
		if (ToolUI::Button("Take me there", ImVec2(std::max(half, ImGui::GetFontSize() * 7.0F), 0.0F))) {
			TakeMeThere(outcome.Planet, outcome.Point, true);
			game.Reports.erase(game.Reports.begin());
			ImGui::CloseCurrentPopup();
			ImGui::EndDisabled();
			ImGui::EndPopup();
			return;
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Shows the battle's point on its planet's map.");
		ImGui::SameLine();
		if (ToolUI::Button(label.c_str(), ImVec2(-1.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			game.Reports.erase(game.Reports.begin());
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	/// Points of the player's lost without a battle, one at a time after the battle reports.
	void AlertWindow() {
		CampaignModel::Campaign& game = s_Screen.Game;
		if (s_Screen.Alerts.empty() || !game.Reports.empty() || s_Screen.Attack.Open || Animating()) {
			return;
		}
		ImGui::OpenPopup("Point lost###CampaignAlert");
		if (!ImGui::BeginPopupModal("Point lost###CampaignAlert", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		const auto [text, where] = s_Screen.Alerts.front();
		ImGui::SetWindowFontScale(1.3F);
		ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "Point lost");
		ImGui::SetWindowFontScale(1.0F);
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
		ImGui::TextUnformatted(text.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		const std::string label = s_Screen.Alerts.size() > 1 ? "Next (" + std::to_string(s_Screen.Alerts.size() - 1) + " more)" : std::string("Continue");
		if (ToolUI::Button("Take me there", ImVec2(ImGui::GetFontSize() * 8.0F, 0.0F))) {
			TakeMeThere(where.Planet, where.Point, false);
			s_Screen.Alerts.erase(s_Screen.Alerts.begin());
			ImGui::CloseCurrentPopup();
		} else {
			ImGui::SameLine();
			if (ToolUI::Button(label.c_str(), ImVec2(ImGui::GetFontSize() * 8.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				s_Screen.Alerts.erase(s_Screen.Alerts.begin());
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndPopup();
	}

	void DisbandWindow() {
		if (s_Screen.ConfirmDisband) {
			ImGui::OpenPopup("Disband?###CampaignDisband");
			s_Screen.ConfirmDisband = false;
		}
		if (!ImGui::BeginPopupModal("Disband?###CampaignDisband", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		const int army = OwnSelectedArmy();
		ImGui::Text("Disband %s? Its soldiers go home for good.", army >= 0 ? s_Screen.Game.Armies[army].Name.c_str() : "this army");
		if (ToolUI::Button("Disband", ImVec2(ImGui::GetFontSize() * 6.0F, 0.0F)) && army >= 0) {
			Disband(s_Screen.Game, army);
			s_Screen.SelectedArmy = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ToolUI::Button("Keep it", ImVec2(ImGui::GetFontSize() * 6.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void EndWindow() {
		CampaignModel::Campaign& game = s_Screen.Game;
		if ((game.Winner < 0 && !game.Lost) || s_Screen.EndShown || !game.Reports.empty() || Animating()) {
			return;
		}
		ImGui::OpenPopup("Campaign over###CampaignEnd");
		if (!ImGui::BeginPopupModal("Campaign over###CampaignEnd", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		const bool won = game.Winner >= 0 && game.Factions[game.Winner].Human;
		int alive = 0;
		for (const Faction& faction: game.Factions) {
			alive += faction.Alive ? 1 : 0;
		}
		ImGui::SetWindowFontScale(1.5F);
		ImGui::TextColored(won ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) : ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "%s", won ? (alive > 1 ? "You and your allies rule the galaxy!" : "You rule the galaxy!") : "Your faction has fallen.");
		ImGui::SetWindowFontScale(1.0F);
		ImGui::Text("Turn %d", game.Turn);
		if (ToolUI::Button("Look over the galaxy", ImVec2(-1.0F, 0.0F))) {
			s_Screen.EndShown = true;
			ImGui::CloseCurrentPopup();
		}
		if (ToolUI::Button("Back to the campaign menu", ImVec2(-1.0F, 0.0F))) {
			s_Screen.EndShown = true;
			SaveGame();
			s_Screen.HasGame = false;
			s_Screen.At = Page::Start;
			s_Screen.Saves = ListSaves();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
#pragma endregion

#pragma region Pages
	/// The next of the player's armies with moves left and nowhere to go, after the one picked.
	void NextArmy() {
		CampaignModel::Campaign& game = s_Screen.Game;
		const int player = Player();
		const int count = static_cast<int>(game.Armies.size());
		const int current = std::max(SelectedArmyIndex(), -1);
		for (int step = 1; step <= count; ++step) {
			const int army = (current + step + count) % count;
			const Army& each = game.Armies[army];
			if (each.Faction == player && each.Moves > 0 && each.Path.empty()) {
				GoToArmy(each.Id);
				return;
			}
		}
		s_Screen.Note = "Every army has moved.";
	}

	void PlayingPage() {
		CampaignModel::Campaign& game = s_Screen.Game;
		const ImGuiStyle& style = ImGui::GetStyle();
		s_Screen.RosterHover = s_Screen.RosterHoverNext;
		s_Screen.RosterHoverNext = 0;
		TrackChanges();
		if (s_Screen.FollowArmy) {
			// An army just ordered somewhere: once it's got there, show where it is. Not for one still on a route (the galaxy map shows
			// that best), nor one that didn't move.
			const int army = FindArmy(game, s_Screen.FollowArmy);
			if (army < 0 || PlaceOf(game.Armies[army]) == s_Screen.FollowFrom || !game.Armies[army].Path.empty()) {
				s_Screen.FollowArmy = 0;
			} else if (!IsMoving(s_Screen.FollowArmy)) {
				GoToArmy(s_Screen.FollowArmy);
				s_Screen.FollowArmy = 0;
			}
		}
		UpdateGuide();
		if (ToolUI::Button("Menu")) {
			SaveGame();
			s_Screen.HasGame = false;
			s_Screen.At = Page::Start;
			s_Screen.Saves = ListSaves();
			return;
		}
		ImGui::SetItemTooltip("Saves the campaign and goes back to the campaign menu.");
		ImGui::SameLine();
		if (ToolUI::Button("Save")) {
			std::string error;
			s_Screen.Note = Save(game, error) ? "Saved." : "Not saved: " + error;
		}
		ImGui::SameLine();
		if (ToolUI::Button(s_Screen.Guide.On ? "Hide guide" : "Guide")) {
			if (s_Screen.Guide.On) {
				s_Screen.Guide.On = false;
			} else if (s_Screen.Guide.At == Step::Count || s_Screen.Guide.At == Step::Welcome) {
				StartGuide();
			} else {
				s_Screen.Guide.On = true;
				s_Screen.Guide.Folded = false;
			}
		}
		ImGui::SetItemTooltip("Walks you through your first moves, step by step.");
		if (Shown(Step::Routine)) {
			ImGui::SameLine();
			if (ToolUI::Button("Military")) {
				s_Screen.MilitaryOpen = !s_Screen.MilitaryOpen;
			}
			ImGui::SetItemTooltip("All your armies: what each is made of, where it is and whether it has moved this turn (M).");
		}
		if (Shown(Step::Trade)) {
			ImGui::SameLine();
			if (ToolUI::Button("Trade")) {
				s_Screen.MarketOpen = !s_Screen.MarketOpen;
			}
			BeaconItem(s_Screen.Lights.Trade);
			ImGui::SetItemTooltip("The galaxy market: buy and sell water, food, metal, fuel, crystal and relics for gold.");
		}
		if (Shown(Step::Diplomacy)) {
			ImGui::SameLine();
			if (ToolUI::Button("Diplomacy")) {
				s_Screen.DiplomacyOpen = !s_Screen.DiplomacyOpen;
			}
			BeaconItem(s_Screen.Lights.Diplomacy);
			ImGui::SetItemTooltip("Where you stand with each faction: deals, trade pacts, peace, alliances and war.");
		}
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		if (ToolUI::Button("Galaxy")) {
			s_Screen.Showing = View::Galaxy;
		}
		BeaconItem(s_Screen.Lights.Galaxy);
		if (s_Screen.ViewSystem >= 0 && s_Screen.ViewSystem < static_cast<int>(game.Systems.size())) {
			ImGui::SameLine();
			const std::string label = game.Systems[s_Screen.ViewSystem].Name + "###systemCrumb";
			if (ToolUI::Button(label.c_str())) {
				ShowSystem(s_Screen.ViewSystem);
			}
		}
		if (s_Screen.Showing == View::Planet && s_Screen.ViewPlanet >= 0) {
			ImGui::SameLine();
			ImGui::TextDisabled("> %s", game.Planets[s_Screen.ViewPlanet].Name.c_str());
		}
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		ImGui::Text("%s, turn %d", game.Name.c_str(), game.Turn);
		const float endWidth = ImGui::CalcTextSize("End turn").x + style.FramePadding.x * 4.0F;
		const float nextWidth = ImGui::CalcTextSize("Next army").x + style.FramePadding.x * 4.0F;
		ImGui::SameLine(ImGui::GetContentRegionMax().x - endWidth - nextWidth - style.ItemSpacing.x);
		if (ToolUI::Button("Next army", ImVec2(nextWidth, 0.0F)) || (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !ImGui::GetIO().WantTextInput)) {
			NextArmy();
		}
		ImGui::SetItemTooltip("Picks your next army that still has moves and nowhere to go (Tab).");
		if (ImGui::IsKeyPressed(ImGuiKey_M, false) && !ImGui::GetIO().WantTextInput && Shown(Step::Routine)) {
			s_Screen.MilitaryOpen = !s_Screen.MilitaryOpen;
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(game.Winner >= 0 || game.Lost);
		if (ToolUI::Button("End turn", ImVec2(endWidth, 0.0F)) || (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))) {
			EndTurn(game);
			SaveGame();
			s_Screen.Note.clear();
			if (FindArmy(game, s_Screen.SelectedArmy) < 0) {
				s_Screen.SelectedArmy = 0;
			}
		}
		ImGui::EndDisabled();
		BeaconItem(s_Screen.Lights.EndTurn);
		ImGui::SetItemTooltip("The other factions take their turns, then the next turn starts: income, healing, and armies carry on their moves. Saves the campaign (Enter).");

		const ImVec2 room = ImGui::GetContentRegionAvail();
		const float sideWidth = std::floor(std::clamp(room.x * 0.27F, ImGui::GetFontSize() * 18.0F, ImGui::GetFontSize() * 28.0F));
		const float logHeight = ImGui::GetTextLineHeightWithSpacing() * 5.0F + style.WindowPadding.y * 2.0F;
		const ImVec2 mapSize(room.x - sideWidth - style.ItemSpacing.x, room.y - logHeight - style.ItemSpacing.y);
		ImGui::BeginGroup();
		switch (s_Screen.Showing) {
			case View::Galaxy:
				GalaxyMap(mapSize);
				break;
			case View::System:
				SystemMap(mapSize);
				break;
			case View::Planet:
				PlanetMap(mapSize);
				break;
		}
		MapOverlay();
		ImGui::BeginChild("##log", ImVec2(mapSize.x, logHeight), ImGuiChildFlags_Borders);
		if (!s_Screen.Note.empty()) {
			ImGui::TextColored(ImVec4(1.0F, 0.85F, 0.35F, 1.0F), "%s", s_Screen.Note.c_str());
		}
		for (auto line = game.Log.rbegin(); line != game.Log.rend(); ++line) {
			ImGui::TextDisabled("%s", line->c_str());
		}
		ImGui::EndChild();
		ImGui::EndGroup();
		ImGui::SameLine();
		SidePanel(sideWidth, room.y);
		GuideBox();

		// Esc steps back out: planet to system to galaxy.
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
			if (s_Screen.Showing == View::Planet) {
				s_Screen.Showing = View::System;
			} else if (s_Screen.Showing == View::System) {
				s_Screen.Showing = View::Galaxy;
			} else {
				s_Screen.SelectedArmy = 0;
				s_Screen.SelectedSystem = s_Screen.SelectedPlanet = s_Screen.SelectedPoint = -1;
			}
		}
		AttackWindow();
		ReportWindow();
		AlertWindow();
		MilitaryWindow();
		MarketWindow();
		DiplomacyWindow();
		WarWindow();
		OfferWindow();
		DisbandWindow();
		EndWindow();
	}

	void OpenSetup() {
		s_Screen.At = Page::Setup;
		s_Screen.Setup = NewCampaign();
		s_Screen.Setup.Factions = AvailableFactions();
		s_Screen.Setup.Seed = static_cast<std::uint32_t>(std::time(nullptr)) % 100000u;
		s_Screen.Setup.Name = "Campaign " + std::to_string(ListSaves().size() + 1);
		// Four factions in to start with, the first played.
		for (size_t i = 0; i < s_Screen.Setup.Factions.size() && i < 4; ++i) {
			s_Screen.Setup.Factions[i].Include = true;
		}
		s_Screen.SetupPlayer = 0;
		s_Screen.SetupGuide = !GuideFinished();
		s_Screen.Note.clear();
	}

	void StartPage() {
		ImGui::SetWindowFontScale(1.6F);
		ImGui::TextColored(ToolTheme::Vec(ToolTheme::Gold), "Campaign");
		ImGui::SetWindowFontScale(1.0F);
		ImGui::TextWrapped("Grand strategy across a galaxy. Raise armies, take the points on planets and moons, hold more than half of a planet's points to control it, and outlast the other factions. Battles are auto-resolved for now.");
		ImGui::Spacing();
		const float width = ImGui::GetFontSize() * 16.0F;
		if (ToolUI::Button("New campaign", ImVec2(width, 0.0F))) {
			OpenSetup();
		}
		if (ToolUI::Button("Back to the main menu", ImVec2(width, 0.0F)) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))) {
			s_Screen.Back = true;
		}
		if (!s_Screen.Note.empty()) {
			ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "%s", s_Screen.Note.c_str());
		}
		ImGui::SeparatorText("Saved campaigns");
		if (s_Screen.Saves.empty()) {
			ImGui::TextDisabled("None yet.");
		}
		if (ImGui::BeginTable("##saves", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
			for (const SaveInfo& save: s_Screen.Saves) {
				ImGui::PushID(save.FileName.c_str());
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(save.Name.c_str());
				ImGui::TableNextColumn();
				ImGui::TextDisabled("Turn %d", save.Turn);
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", save.Factions.c_str());
				ImGui::TableNextColumn();
				if (ToolUI::SmallButton("Carry on")) {
					std::string error;
					if (Load(save.FileName, s_Screen.Game, error)) {
						StartViewing();
					} else {
						s_Screen.Note = error;
					}
				}
				ImGui::SameLine();
				if (ToolUI::SmallButton("Delete")) {
					s_Screen.ConfirmDelete = true;
					s_Screen.DeleteFile = save.FileName;
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (s_Screen.ConfirmDelete) {
			ImGui::OpenPopup("Delete?###CampaignDelete");
			s_Screen.ConfirmDelete = false;
		}
		if (ImGui::BeginPopupModal("Delete?###CampaignDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Delete the saved campaign %s for good?", s_Screen.DeleteFile.c_str());
			if (ToolUI::Button("Delete", ImVec2(ImGui::GetFontSize() * 6.0F, 0.0F))) {
				DeleteSave(s_Screen.DeleteFile);
				s_Screen.Saves = ListSaves();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ToolUI::Button("Keep it", ImVec2(ImGui::GetFontSize() * 6.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void SetupPage() {
		NewCampaign& setup = s_Screen.Setup;
		ImGui::SetWindowFontScale(1.4F);
		ImGui::TextColored(ToolTheme::Vec(ToolTheme::Gold), "New campaign");
		ImGui::SetWindowFontScale(1.0F);
		const float labelWidth = ImGui::GetFontSize() * 8.0F;
		char name[64];
		std::snprintf(name, sizeof(name), "%s", setup.Name.c_str());
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0F);
		if (ImGui::InputText("Name", name, sizeof(name))) {
			setup.Name = name;
		}
		int seed = static_cast<int>(setup.Seed);
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
		if (ImGui::InputInt("Seed", &seed)) {
			setup.Seed = static_cast<std::uint32_t>(std::max(seed, 0));
		}
		ImGui::SetItemTooltip("The same seed makes the same galaxy.");
		ImGui::SameLine();
		if (ToolUI::SmallButton("Random")) {
			setup.Seed = static_cast<std::uint32_t>(std::random_device()()) % 100000u;
		}
		ImGui::TextUnformatted("Galaxy");
		ImGui::SameLine(labelWidth);
		ToolUI::RadioButton("Small (14 stars)", &setup.Size, 0);
		ImGui::SameLine();
		ToolUI::RadioButton("Medium (24)", &setup.Size, 1);
		ImGui::SameLine();
		ToolUI::RadioButton("Large (40)", &setup.Size, 2);
		ImGui::TextUnformatted("Difficulty");
		ImGui::SameLine(labelWidth);
		ToolUI::RadioButton("Easy", &setup.Difficulty, 0);
		ImGui::SameLine();
		ToolUI::RadioButton("Normal", &setup.Difficulty, 1);
		ImGui::SameLine();
		ToolUI::RadioButton("Hard", &setup.Difficulty, 2);
		ImGui::SetItemTooltip("How much gold the other factions get, how strong the locals are, and how bold the other factions are.");

		ImGui::SeparatorText("Factions");
		ImGui::TextDisabled("Tick the factions in this campaign (mods' too), and pick the one you play.");
		const float tableHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.5F;
		if (ImGui::BeginTable("##factions", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(ImGui::GetFontSize() * 30.0F, std::max(tableHeight, 100.0F)))) {
			ImGui::TableSetupColumn("In it", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Faction");
			ImGui::TableSetupColumn("You play", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();
			for (int index = 0; index < static_cast<int>(setup.Factions.size()); ++index) {
				FactionChoice& faction = setup.Factions[index];
				ImGui::PushID(index);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ToolUI::Checkbox("##in", &faction.Include);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(faction.Name.c_str());
				ImGui::SetItemTooltip("%s", faction.Module.c_str());
				ImGui::TableNextColumn();
				if (ToolUI::RadioButton("##you", s_Screen.SetupPlayer == index)) {
					s_Screen.SetupPlayer = index;
					faction.Include = true;
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (setup.Factions.empty()) {
			ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "No faction modules are loaded.");
		}
		if (!s_Screen.Note.empty()) {
			ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "%s", s_Screen.Note.c_str());
		}
		ToolUI::Checkbox("Show the guide", &s_Screen.SetupGuide);
		ImGui::SetItemTooltip("A step-by-step guide through your first moves, over the map.");
		const float width = ImGui::GetFontSize() * 10.0F;
		if (ToolUI::Button("Start", ImVec2(width, 0.0F))) {
			for (int index = 0; index < static_cast<int>(setup.Factions.size()); ++index) {
				setup.Factions[index].Human = index == s_Screen.SetupPlayer;
			}
			std::string error;
			CampaignModel::Campaign made = Generate(setup, error);
			if (made.Systems.empty()) {
				s_Screen.Note = error;
			} else {
				// A name not taken by another save.
				const std::vector<SaveInfo> saves = ListSaves();
				std::string name = made.Name;
				for (int copy = 2; std::any_of(saves.begin(), saves.end(), [&name](const SaveInfo& save) { return save.Name == name; }); ++copy) {
					name = made.Name + " (" + std::to_string(copy) + ")";
				}
				made.Name = name;
				s_Screen.Game = std::move(made);
				StartViewing();
				if (s_Screen.SetupGuide) {
					StartGuide();
				}
				SaveGame();
			}
		}
		ImGui::SameLine();
		if (ToolUI::Button("Back", ImVec2(width, 0.0F)) || (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput)) {
			s_Screen.At = Page::Start;
			s_Screen.Note.clear();
		}
	}
#pragma endregion
} // namespace

void RTE::Campaign::Open() {
	s_Screen.Open = true;
	s_Screen.Back = false;
	s_Screen.At = s_Screen.HasGame ? Page::Playing : Page::Start;
	s_Screen.Saves = ListSaves();
	s_Screen.Note.clear();
}

void RTE::Campaign::Close() {
	SaveGame();
	s_Screen.Open = false;
}

bool RTE::Campaign::IsOpen() { return s_Screen.Open; }

bool RTE::Campaign::TakeBack() {
	const bool back = s_Screen.Back;
	s_Screen.Back = false;
	return back;
}

void RTE::Campaign::DrawGUI() {
	if (!s_Screen.Open || !g_MenuMan.GetIsInMenuScreen()) {
		return;
	}
	ImGui::GetIO().MouseDrawCursor = true;
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float margin = std::floor(std::min(viewport->Size.x, viewport->Size.y) * 0.02F);
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + margin, viewport->Pos.y + margin));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - margin * 2.0F, viewport->Size.y - margin * 2.0F));
	if (ImGui::Begin("Campaign###Campaign", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		switch (s_Screen.At) {
			case Page::Start:
				StartPage();
				break;
			case Page::Setup:
				SetupPage();
				break;
			case Page::Playing:
				if (s_Screen.HasGame) {
					PlayingPage();
				} else {
					s_Screen.At = Page::Start;
				}
				break;
		}
	}
	ImGui::End();
}
