// Campaign mode's screen: the start page (new campaign, saved ones), the new campaign setup, and the game itself: the galaxy map, a
// system's map with its planets and moons, a planet's simplified surface map with its points, the side panel for what's selected, the
// log, and the attack and battle report windows. Drawn in the main menu with ImGui, like the map browser. See Campaign.h.

#include "Campaign.h"
#include "CampaignModel.h"

#include "MenuMan.h"
#include "ToolWidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <random>

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
		std::string DeleteFile;
		bool EndShown = false; //!< The victory or defeat window was shown for this campaign.
	};
	Screen s_Screen;

	ImU32 Alpha(ImU32 color, float alpha) { return (color & 0x00FFFFFFu) | (static_cast<ImU32>(std::clamp(alpha, 0.0F, 1.0F) * 255.0F) << IM_COL32_A_SHIFT); }

	ImU32 Scale(ImU32 color, float by) {
		ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
		return ImGui::ColorConvertFloat4ToU32(ImVec4(std::min(c.x * by, 1.0F), std::min(c.y * by, 1.0F), std::min(c.z * by, 1.0F), c.w));
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
	bool DrawArmyIcon(ImDrawList* drawList, ImVec2 at, float size, ImU32 color, bool selected, int count) {
		const ImVec2 points[] = {ImVec2(at.x - size, at.y - size), ImVec2(at.x + size, at.y - size), ImVec2(at.x + size, at.y + size * 0.3F), ImVec2(at.x, at.y + size * 1.2F), ImVec2(at.x - size, at.y + size * 0.3F)};
		if (selected) {
			drawList->AddCircle(at, size * 1.9F, IM_COL32(255, 230, 120, 255), 24, 2.0F);
		}
		drawList->AddConvexPolyFilled(points, 5, color);
		drawList->AddPolyline(points, 5, IM_COL32(10, 10, 20, 255), ImDrawFlags_Closed, 1.5F);
		if (count > 1) {
			const std::string text = std::to_string(count);
			drawList->AddText(ImVec2(at.x - ImGui::CalcTextSize(text.c_str()).x * 0.5F, at.y - size * 0.9F), IM_COL32(10, 10, 20, 255), text.c_str());
		}
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		return std::abs(mouse.x - at.x) <= size * 1.2F && std::abs(mouse.y - at.y) <= size * 1.3F;
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
		ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		ImGui::GetWindowDrawList()->PushClipRect(from, to, true);
		return ImGui::IsItemHovered();
	}

	void EndCanvas() { ImGui::GetWindowDrawList()->PopClipRect(); }
#pragma endregion

#pragma region Orders from the maps
	/// Opens the attack window for a move that's a battle, or makes the move at once when it isn't.
	void TryEnter(int army, int planet, int point, bool overland) {
		float defence = 0.0F;
		if (WouldFight(s_Screen.Game, s_Screen.Game.Armies[army].Faction, planet, point, defence)) {
			s_Screen.Attack = PendingAttack{true, s_Screen.Game.Armies[army].Id, planet, point, overland};
			return;
		}
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
		if (!OrderMove(s_Screen.Game, army, system)) {
			s_Screen.Note = "No lane leads there.";
		} else if (!s_Screen.Game.Armies[army].Path.empty()) {
			s_Screen.Note = s_Screen.Game.Armies[army].Name + " will carry on next turn (" + std::to_string(s_Screen.Game.Armies[army].Path.size()) + " jumps to go).";
		}
	}
#pragma endregion

#pragma region The galaxy map
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
			for (int next: game.Armies[army].Path) {
				drawList->AddLine(toScreen(game.Systems[at].X, game.Systems[at].Y), toScreen(game.Systems[next].X, game.Systems[next].Y), IM_COL32(255, 220, 90, 220), 3.0F);
				at = next;
			}
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
				CenteredText(drawList, ImVec2(at.x, at.y + starSize + 9.0F), onIt ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 205, 230, 220), system.Name);
			}
			// Armies here, one shield a faction, beside the star.
			std::vector<std::vector<int>> byFaction(game.Factions.size());
			for (int army = 0; army < static_cast<int>(game.Armies.size()); ++army) {
				if (game.Armies[army].System == index) {
					byFaction[game.Armies[army].Faction].push_back(army);
				}
			}
			float offset = starSize + 14.0F;
			for (int faction = 0; faction < static_cast<int>(byFaction.size()); ++faction) {
				if (byFaction[faction].empty()) {
					continue;
				}
				const bool selected = std::any_of(byFaction[faction].begin(), byFaction[faction].end(), [](int army) { return s_Screen.Game.Armies[army].Id == s_Screen.SelectedArmy; });
				if (DrawArmyIcon(drawList, ImVec2(at.x + offset, at.y - starSize), 7.0F, FactionColor(faction), selected, static_cast<int>(byFaction[faction].size())) && hovered) {
					// The first of them, or the next after the one picked.
					const std::vector<int>& armies = byFaction[faction];
					auto picked = std::find_if(armies.begin(), armies.end(), [](int army) { return s_Screen.Game.Armies[army].Id == s_Screen.SelectedArmy; });
					hoveredArmy = game.Armies[picked == armies.end() || picked + 1 == armies.end() ? armies.front() : *(picked + 1)].Id;
				}
				offset += 17.0F;
			}
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
			ImGui::TextDisabled("Double-click to see it. Right-click to send the army picked.");
			ImGui::EndTooltip();
		} else if (hoveredArmy) {
			const Army& army = game.Armies[FindArmy(game, hoveredArmy)];
			ImGui::SetTooltip("%s (%s): %d soldiers, strength %.0f", army.Name.c_str(), FactionLabel(army.Faction).c_str(), SoldierCount(army), Strength(game, army));
		}
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
		for (int planetIndex: system.Planets) {
			const Planet& planet = game.Planets[planetIndex];
			const ImVec2 at = where[planetIndex];
			const float radius = step * 0.12F * planet.Size;
			DrawPlanetBall(drawList, at, radius, planet, star);
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
			const bool onIt = hovered && std::hypot(io.MousePos.x - at.x, io.MousePos.y - at.y) < radius + 8.0F;
			if (onIt) {
				hoveredPlanet = planetIndex;
			}
			// Armies standing on it.
			std::vector<int> armies;
			for (int army = 0; army < static_cast<int>(game.Armies.size()); ++army) {
				if (game.Armies[army].Planet == planetIndex) {
					armies.push_back(army);
				}
			}
			float offset = -static_cast<float>(armies.size() - 1) * 9.0F;
			for (int army: armies) {
				if (DrawArmyIcon(drawList, ImVec2(at.x + offset, at.y - radius - 16.0F), 6.0F, FactionColor(game.Armies[army].Faction), game.Armies[army].Id == s_Screen.SelectedArmy, 1) && hovered) {
					hoveredArmy = game.Armies[army].Id;
				}
				offset += 18.0F;
			}
		}
		// Armies in orbit, in a row under the star.
		const std::vector<int> orbiting = ArmiesInOrbit(game, systemIndex);
		float offset = -static_cast<float>(static_cast<int>(orbiting.size()) - 1) * 10.0F;
		for (int army: orbiting) {
			if (DrawArmyIcon(drawList, ImVec2(star.x + offset, star.y + step * 0.25F + 18.0F), 7.0F, FactionColor(game.Armies[army].Faction), game.Armies[army].Id == s_Screen.SelectedArmy, 1) && hovered) {
				hoveredArmy = game.Armies[army].Id;
			}
			offset += 20.0F;
		}
		if (!orbiting.empty()) {
			CenteredText(drawList, ImVec2(star.x, star.y + step * 0.25F + 30.0F), IM_COL32(180, 190, 220, 200), "In orbit");
		}
		CenteredText(drawList, ImVec2(star.x, from.y + 8.0F), IM_COL32(255, 255, 255, 255), system.Name + " system");

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
			ImGui::SetTooltip("%s, %s%s", planet.Name.c_str(), PlanetTypeName(planet.Type), planet.Points.empty() ? "" : "\nDouble-click for its map. Right-click to land the army picked.");
		}
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
			std::string label = point.Name;
			if (point.Owner < 0 && point.Militia > 0.01F) {
				label += " (locals)";
			}
			CenteredText(drawList, ImVec2(at.x, at.y + radius + 4.0F), IM_COL32(255, 255, 255, 240), label);
			if (hovered && std::hypot(io.MousePos.x - at.x, io.MousePos.y - at.y) < radius + 4.0F) {
				hoveredPoint = index;
			}
			const std::vector<int> armies = ArmiesAt(game, planetIndex, index);
			float offset = -static_cast<float>(static_cast<int>(armies.size()) - 1) * 10.0F;
			for (int army: armies) {
				if (DrawArmyIcon(drawList, ImVec2(at.x + offset, at.y - radius - 14.0F), 7.0F, FactionColor(game.Armies[army].Faction), game.Armies[army].Id == s_Screen.SelectedArmy, 1) && hovered) {
					hoveredArmy = game.Armies[army].Id;
				}
				offset += 20.0F;
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
				OrderLaunch(game, army);
			}
			ImGui::EndDisabled();
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
			const bool open = ImGui::TreeNodeEx(header.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
			if (own && self.Squads.size() > 1) {
				ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Split").x - ImGui::GetStyle().FramePadding.x * 2.0F);
				if (ToolUI::SmallButton("Split")) {
					const long keep = self.Id;
					SplitSquad(game, army, squad);
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
			const int cost = SettleCost(here.Settlement);
			const std::string label = (here.Settlement == 0 ? std::string("Found an outpost") : std::string("Grow into a ") + SettlementName(here.Settlement + 1)) + " (" + std::to_string(cost) + ")";
			ImGui::BeginDisabled(self.Gold < cost);
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
				const std::string label = std::string(BuildingName(building)) + " (" + std::to_string(BuildingCost(building)) + ")";
				ImGui::BeginDisabled(self.Gold < BuildingCost(building) || builtCount >= here.Settlement + 1);
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
				const int cost = static_cast<int>(squad.SoldierCost * static_cast<float>(squad.Size));
				ImGui::PushID(type);
				ImGui::BeginDisabled(!can);
				const std::string label = squad.Name + " x" + std::to_string(squad.Size) + "  (" + std::to_string(cost) + ")";
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

	void SidePanel(float width, float height) {
		CampaignModel::Campaign& game = s_Screen.Game;
		ImGui::BeginChild("##side", ImVec2(width, height), ImGuiChildFlags_Borders);
		const int player = Player();
		if (player >= 0) {
			const Faction& self = game.Factions[player];
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(self.Color), "%s", self.Name.c_str());
			const int income = IncomeOf(game, player);
			ImGui::Text("Gold %d", self.Gold);
			ImGui::SameLine();
			ImGui::TextColored(income >= 0 ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) : ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "(%+d a turn)", income);
			ImGui::SetItemTooltip("Income from your points and settlements, and %s, less upkeep of %d for your soldiers.", "60 for each planet you control and nobody has invaded", UpkeepOf(game, player));
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
			if (game.Armies[other].Faction != game.Armies[army].Faction) {
				defenders += SoldierCount(game.Armies[other]);
			}
		}
		ImGui::Text("Defenders (%s): strength %.0f (%d soldiers%s)", FactionLabel(point.Owner).c_str(), defence, defenders, point.Militia > 0.01F && point.Owner != game.Armies[army].Faction ? " and locals" : "");
		if (point.Settlement > 0 && point.Built[static_cast<size_t>(Building::Walls)]) {
			ImGui::TextDisabled("Walls: the defenders fight harder.");
		}
		const ImVec4 color = chance >= 0.7F ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) : chance >= 0.4F ? ImVec4(1.0F, 0.85F, 0.35F, 1.0F) : ImVec4(1.0F, 0.5F, 0.4F, 1.0F);
		ImGui::TextColored(color, "Chance of winning: about %d%%", static_cast<int>(std::round(chance * 100.0F)));
		ImGui::Spacing();
		ImGui::BeginDisabled(true);
		ToolUI::Button("Fight it yourself", ImVec2(ImGui::GetFontSize() * 10.0F, 0.0F));
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Coming later: the battle played in Battle Command on a map of this point.");
		ImGui::SameLine();
		if (ToolUI::Button("Auto-resolve", ImVec2(ImGui::GetFontSize() * 8.0F, 0.0F))) {
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
		if (game.Reports.empty() || s_Screen.Attack.Open) {
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
		if (ToolUI::Button(label.c_str(), ImVec2(-1.0F, 0.0F)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			game.Reports.erase(game.Reports.begin());
			ImGui::CloseCurrentPopup();
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
		if ((game.Winner < 0 && !game.Lost) || s_Screen.EndShown || !game.Reports.empty()) {
			return;
		}
		ImGui::OpenPopup("Campaign over###CampaignEnd");
		if (!ImGui::BeginPopupModal("Campaign over###CampaignEnd", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		const bool won = game.Winner >= 0 && game.Factions[game.Winner].Human;
		ImGui::SetWindowFontScale(1.5F);
		ImGui::TextColored(won ? ImVec4(0.5F, 1.0F, 0.5F, 1.0F) : ImVec4(1.0F, 0.5F, 0.4F, 1.0F), "%s", won ? "You rule the galaxy!" : "Your faction has fallen.");
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
				s_Screen.SelectedArmy = each.Id;
				if (each.Planet >= 0) {
					ShowPlanet(each.Planet);
					s_Screen.SelectedPoint = each.Point;
				} else {
					ShowSystem(each.System);
				}
				return;
			}
		}
		s_Screen.Note = "Every army has moved.";
	}

	void PlayingPage() {
		CampaignModel::Campaign& game = s_Screen.Game;
		const ImGuiStyle& style = ImGui::GetStyle();
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
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		if (ToolUI::Button("Galaxy")) {
			s_Screen.Showing = View::Galaxy;
		}
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
