#include "ActionMenu.h"

#include "SandboxInternal.h"
#include "GUISound.h"
#include "PieMenu.h"
#include "PieSlice.h"
#include "SettingsMan.h"
#include "UInputMan.h"
#include "ToolWidgets.h"
#include "AVehicle.h"
#include "Arm.h"
#include "HDFirearm.h"
#include "HeldDevice.h"
#include "TimerMan.h"

#include <cfloat>

using namespace RTE;

ActionMenu::MenuLayout::MenuLayout(float scale) :
    m_Scale(scale), m_Width(320.0F * scale), m_Pad(8.0F * scale), m_Gap(3.0F * scale), m_RowHeight(24.0F * scale), m_HeadingHeight(18.0F * scale), m_Y(8.0F * scale) {}

void ActionMenu::MenuLayout::Heading(const std::string& text, Kind kind) {
	m_Kind = kind;
	Cell cell;
	cell.Section = kind;
	cell.Min = ImVec2(m_Pad, m_Y);
	cell.Max = ImVec2(m_Width - m_Pad, m_Y + m_HeadingHeight);
	cell.Label = text;
	Cells.push_back(cell);
	m_Y += m_HeadingHeight + m_Gap;
}

void ActionMenu::MenuLayout::Choices(int action, const std::vector<std::string>& labels, int chosen, int perRow, const std::vector<bool>& enabled, const std::vector<const void*>& data) {
	int count = static_cast<int>(labels.size());
	if (count == 0) {
		return;
	}
	int columns = perRow > 0 ? std::min(perRow, count) : count;
	float cellWidth = (m_Width - m_Pad * 2.0F - m_Gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
	for (int i = 0; i < count; ++i) {
		int column = i % columns;
		if (i > 0 && column == 0) {
			m_Y += m_RowHeight + m_Gap;
		}
		Cell cell;
		float left = m_Pad + static_cast<float>(column) * (cellWidth + m_Gap);
		cell.Min = ImVec2(left, m_Y);
		cell.Max = ImVec2(left + cellWidth, m_Y + m_RowHeight);
		cell.Label = labels[i];
		cell.Action = action;
		cell.Value = i;
		cell.Chosen = i == chosen;
		cell.Enabled = i < static_cast<int>(enabled.size()) ? enabled[i] : true;
		cell.Data = i < static_cast<int>(data.size()) ? data[i] : nullptr;
		cell.Section = m_Kind;
		Cells.push_back(cell);
	}
	m_Y += m_RowHeight + m_Gap;
}

void ActionMenu::MenuLayout::PlaceAbove(const ImVec2& point) {
	float height = m_Y + m_Pad - m_Gap;
	GameViewRect view = g_WindowMan.GetGameViewRect();
	float left = std::clamp(point.x - m_Width * 0.5F, view.x + 4.0F, std::max(view.x + 4.0F, view.x + view.w - m_Width - 4.0F));
	float top = std::clamp(point.y - 14.0F * m_Scale - height, view.y + 4.0F, std::max(view.y + 4.0F, view.y + view.h - height - 4.0F));
	Min = ImVec2(left, top);
	Max = ImVec2(left + m_Width, top + height);
	for (Cell& cell: Cells) {
		cell.Min = ImVec2(cell.Min.x + left, cell.Min.y + top);
		cell.Max = ImVec2(cell.Max.x + left, cell.Max.y + top);
	}
}

int ActionMenu::CellAt(const std::vector<Cell>& cells, const ImVec2& point) {
	for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
		const Cell& cell = cells[i];
		if (cell.Action >= 0 && point.x >= cell.Min.x && point.x < cell.Max.x && point.y >= cell.Min.y && point.y < cell.Max.y) {
			return i;
		}
	}
	return -1;
}

namespace {
	/// Clips a label to a width, ending it with ".." when it is cut.
	std::string FitLabel(ImFont* font, float size, const std::string& label, float width) {
		if (font->CalcTextSizeA(size, FLT_MAX, 0.0F, label.c_str()).x <= width) {
			return label;
		}
		std::string cut = label;
		while (!cut.empty() && font->CalcTextSizeA(size, FLT_MAX, 0.0F, (cut + "..").c_str()).x > width) {
			cut.pop_back();
		}
		return cut + "..";
	}
} // namespace

void ActionMenu::DrawMenu(const MenuLayout& menu, int hover, float scale) {
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	ImFont* font = ImGui::GetFont();
	const float fontSize = 14.0F * scale;
	const float rounding = 4.0F * scale;
	const float line = std::max(1.0F, scale);
	// The game's own theme (DebugMan's): olive panels, parchment text. Each kind of section has its own colour: blue for what is done now,
	// green for the units' state, gold for your settings. A choice in use is filled with it, with dark text and a tick; the rest are sunk
	// and dim, so the one in use stands out at a glance.
	const ImU32 panel = IM_COL32(38, 46, 32, 238);
	const ImU32 goldDim = IM_COL32(170, 128, 48, 255);
	const ImU32 text = IM_COL32(232, 224, 190, 255);
	const ImU32 textDim = IM_COL32(150, 146, 120, 255);
	const ImU32 textOnLit = IM_COL32(255, 255, 255, 255); // Light: the pixel font has a dark edge of its own, which smudges dark lettering.
	auto colourOf = [](Kind kind, int alpha) {
		switch (kind) {
			case Kind::Command:
				return IM_COL32(120, 180, 245, alpha);
			case Kind::State:
				return IM_COL32(125, 215, 120, alpha);
			default:
				return IM_COL32(242, 182, 61, alpha);
		}
	};
	auto tagOf = [](Kind kind) {
		switch (kind) {
			case Kind::Command:
				return "DO NOW";
			case Kind::State:
				return "UNITS' STATE";
			default:
				return "SETTING";
		}
	};

	// Its shadow falls away from the UI's light (ToolLight), or down and right with the UI lighting off.
	if (ToolLight::Strength > 0.0F) {
		ToolLight::Shadow(drawList, menu.Min, menu.Max, line * 2.0F);
	} else {
		drawList->AddRectFilled(ImVec2(menu.Min.x + 3.0F, menu.Min.y + 4.0F), ImVec2(menu.Max.x + 3.0F, menu.Max.y + 4.0F), IM_COL32(0, 0, 0, 90), rounding);
	}
	drawList->AddRectFilled(menu.Min, menu.Max, panel, rounding);
	ToolLight::Surface(drawList, menu.Min, menu.Max, line * 2.0F);
	drawList->AddRect(menu.Min, menu.Max, goldDim, rounding, 0, line);
	for (int i = 0; i < static_cast<int>(menu.Cells.size()); ++i) {
		const Cell& cell = menu.Cells[i];
		ImU32 colour = colourOf(cell.Section, 255);
		if (cell.Action < 0) {
			// The heading, in its section's colour, with the kind of section as a tag on the right.
			const float tagSize = fontSize * 0.78F;
			const char* tag = tagOf(cell.Section);
			ImVec2 tagExtent = font->CalcTextSizeA(tagSize, FLT_MAX, 0.0F, tag);
			float tagLeft = std::floor(cell.Max.x - tagExtent.x - 4.0F * scale);
			std::string label = FitLabel(font, fontSize * 0.85F, cell.Label, tagLeft - cell.Min.x - 8.0F * scale);
			drawList->AddText(font, fontSize * 0.85F, ImVec2(cell.Min.x + 2.0F, cell.Min.y + 2.0F * scale), colour, label.c_str());
			float tagTop = std::floor(cell.Min.y + 2.0F * scale + (fontSize * 0.85F - tagSize) * 0.5F);
			drawList->AddRectFilled(ImVec2(tagLeft - 3.0F * scale, tagTop - 1.0F * scale), ImVec2(cell.Max.x, tagTop + tagExtent.y + 1.0F * scale), colourOf(cell.Section, 60), 2.0F * scale);
			drawList->AddText(font, tagSize, ImVec2(tagLeft, tagTop), colour, tag);
			float lineY = std::floor(cell.Max.y - 2.0F * scale);
			drawList->AddLine(ImVec2(cell.Min.x, lineY), ImVec2(cell.Max.x, lineY), colourOf(cell.Section, 110), 1.0F);
			continue;
		}
		bool hovered = i == hover;
		bool lit = cell.Chosen && cell.Section != Kind::Command;
		ImU32 fill;
		if (lit) {
			// A darker shade of its colour, edged in the colour itself, so the light lettering reads on it.
			auto shade = [colour](int shift) { return static_cast<ImU32>(static_cast<float>((colour >> shift) & 0xFF) * 0.45F) << shift; };
			fill = shade(IM_COL32_R_SHIFT) | shade(IM_COL32_G_SHIFT) | shade(IM_COL32_B_SHIFT) | IM_COL32_A_MASK;
		} else if (cell.Section == Kind::Command) {
			// Buttons: raised, blue-grey.
			fill = hovered ? IM_COL32(62, 88, 118, 255) : IM_COL32(44, 62, 84, 255);
		} else {
			// Choices not in use: sunk into the panel.
			fill = hovered ? IM_COL32(64, 80, 50, 255) : IM_COL32(30, 37, 25, 255);
		}
		drawList->AddRectFilled(cell.Min, cell.Max, fill, rounding * 0.75F);
		if (cell.Section == Kind::Command && !hovered) {
			drawList->AddRect(cell.Min, cell.Max, colourOf(Kind::Command, 90), rounding * 0.75F, 0, 1.0F);
		} else if (!lit && !hovered) {
			drawList->AddRect(cell.Min, cell.Max, IM_COL32(70, 80, 58, 255), rounding * 0.75F, 0, 1.0F);
		}
		if (lit && !hovered) {
			drawList->AddRect(cell.Min, cell.Max, colour, rounding * 0.75F, 0, std::max(1.0F, 1.5F * scale));
		}
		if (hovered) {
			drawList->AddRect(cell.Min, cell.Max, lit ? IM_COL32(255, 250, 230, 255) : colour, rounding * 0.75F, 0, std::max(1.0F, 1.5F * scale));
		}
		// A tick before the label of the one in use.
		float tickRoom = lit ? 12.0F * scale : 0.0F;
		std::string label = FitLabel(font, fontSize, cell.Label, cell.Max.x - cell.Min.x - 8.0F * scale - tickRoom);
		ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0F, label.c_str());
		ImVec2 at(std::floor((cell.Min.x + cell.Max.x - size.x + tickRoom) * 0.5F), std::floor((cell.Min.y + cell.Max.y - size.y) * 0.5F));
		if (lit) {
			float midY = (cell.Min.y + cell.Max.y) * 0.5F;
			float x = at.x - tickRoom;
			ImVec2 tick[] = {ImVec2(x + 1.0F * scale, midY), ImVec2(x + 4.0F * scale, midY + 3.0F * scale), ImVec2(x + 9.0F * scale, midY - 4.0F * scale)};
			drawList->AddPolyline(tick, 3, textOnLit, ImDrawFlags_None, std::max(1.5F, 2.0F * scale));
		}
		ImU32 labelColour = lit ? textOnLit : (cell.Enabled ? text : textDim);
		drawList->AddText(font, fontSize, at, labelColour, label.c_str());
	}
}

namespace {
	/// What a cell of the unit's menu does when picked.
	enum UnitAction {
		Slice, //!< A slice of the wheel (Data), handed to the unit's PieMenu.
		Item, //!< An item the unit carries (Data): taken in hand.
		Powerup, //!< A powerup the unit carries (Data): used (Actor::UsePowerup).
		Handbrake, //!< A vehicle's handbrake off (0) or on (1).
		WeaponRule, //!< Sets the unit's weapons rule (Actor::WeaponRule).
		MovementRule, //!< Sets the unit's movement rule (Actor::MovementRule).
		TemperamentChoice //!< Sets the unit's temperament (Actor::Temperament, NC-1).
	};

	bool s_Open = false; //!< Whether the unit's menu is up.
	bool s_Spent = false; //!< A slice was picked while the button is held: the menu is gone till the button is let go and held again.
	bool s_LetGo = false; //!< The right button was let go with the menu left up (UnitMenuClickToOpen): it is put away by a click.
	bool s_Moved = false; //!< The pointer has moved since the menu opened, so letting go of the button over a row picks it.
	bool s_WaitForLetGo = false; //!< The menu was put away with the right button down: it doesn't open again till the button is let go.
	const Actor* s_Actor = nullptr; //!< The unit it was opened on. Not owned.
	ImVec2 s_Anchor; //!< Where it opened, in window pixels: the unit's aim point then. The menu sits above it.
	ImVec2 s_Pointer; //!< The menu's own pointer, in window pixels, moved by the mouse.
	int s_Hover = -1; //!< The cell under the pointer, or -1.
	float s_Scale = 1.0F; //!< Sizes are for a 720 px high picture, times this.
	float s_TimeScaleBefore = 1.0F; //!< How fast time ran before the menu slowed it.
	float s_TimeScaleSet = -1.0F; //!< The speed the menu slowed time to, or -1 if it didn't.
	ActionMenu::MenuLayout s_Menu(1.0F); //!< The menu as last laid out (by Update, for the picks; Draw draws it).

	std::string SliceLabel(const PieSlice* slice) {
		const std::string& description = slice->GetDescription();
		return description.empty() ? slice->GetPresetName() : description;
	}

	/// Whether a slice is about what the unit carries, so it goes under Inventory rather than Actions.
	bool IsInventorySlice(const PieSlice* slice) {
		switch (slice->GetType()) {
			case PieSliceType::Pickup:
			case PieSliceType::Drop:
			case PieSliceType::NextItem:
			case PieSliceType::PreviousItem:
			case PieSliceType::Reload:
			case PieSliceType::FullInventory:
				return true;
			default:
				return false;
		}
	}

	void AddSlices(ActionMenu::MenuLayout& menu, const std::vector<PieSlice*>& slices, bool inventory) {
		std::vector<std::string> labels;
		std::vector<bool> enabled;
		std::vector<const void*> data;
		for (const PieSlice* slice: slices) {
			if (!slice->GetSubPieMenu() && IsInventorySlice(slice) == inventory) {
				labels.push_back(SliceLabel(slice));
				enabled.push_back(slice->IsEnabled());
				data.push_back(slice);
			}
		}
		menu.Choices(UnitAction::Slice, labels, -1, 2, enabled, data);
	}

	/// The unit's inventory: what it has in hand (lit) and what it carries, each taken in hand when picked. Then the wheel's inventory slices.
	void AddInventory(ActionMenu::MenuLayout& menu, const Actor* actor, const PieMenu* pieMenu) {
		const AHuman* human = dynamic_cast<const AHuman*>(actor);
		const std::deque<MovableObject*>* inventory = actor->GetInventory();
		bool hasSlices = std::any_of(pieMenu->GetPieSlices().begin(), pieMenu->GetPieSlices().end(), [](const PieSlice* slice) { return !slice->GetSubPieMenu() && IsInventorySlice(slice); });
		if (!human && inventory->empty() && !hasSlices) {
			return;
		}
		menu.Heading("Inventory", ActionMenu::Kind::State);
		std::vector<std::string> labels;
		std::vector<bool> enabled;
		std::vector<const void*> data;
		int chosen = -1;
		if (human) {
			if (const HeldDevice* inHand = human->GetEquippedItem()) {
				chosen = 0;
				labels.push_back(inHand->GetPresetName());
				enabled.push_back(true);
				data.push_back(nullptr);
			}
			if (const HeldDevice* offHand = human->GetEquippedBGItem()) {
				labels.push_back(offHand->GetPresetName() + " (off hand)");
				enabled.push_back(true);
				data.push_back(nullptr);
			}
		}
		for (const MovableObject* item: *inventory) {
			// (Powerups are under Quick actions.)
			if (const HeldDevice* device = dynamic_cast<const HeldDevice*>(item); device && device->IsPowerup()) {
				continue;
			}
			const HDFirearm* scrollSpell = dynamic_cast<const HDFirearm*>(item);
			const int casts = scrollSpell ? scrollSpell->GetScrollCasts() : -1;
			labels.push_back(casts >= 0 ? item->GetPresetName() + " (" + std::to_string(casts) + (casts == 1 ? " cast)" : " casts)") : item->GetPresetName());
			// (Only a unit with a hand to hold it takes an item in hand; others' inventories are shown, greyed.)
			enabled.push_back(human && human->GetFGArm() && dynamic_cast<const HeldDevice*>(item));
			data.push_back(item);
		}
		if (labels.empty()) {
			labels.emplace_back("Nothing carried");
			enabled.push_back(false);
			data.push_back(nullptr);
		}
		menu.Choices(UnitAction::Item, labels, chosen, 2, enabled, data);
		if (hasSlices) {
			menu.SwitchKind(ActionMenu::Kind::Command);
			AddSlices(menu, pieMenu->GetPieSlices(), true);
		}
	}

	/// The powerups the unit carries, each used when picked: how many uses it has left, or while what it gives is running, the seconds left.
	void AddQuickActions(ActionMenu::MenuLayout& menu, const Actor* actor) {
		std::vector<std::string> labels;
		std::vector<bool> enabled;
		std::vector<const void*> data;
		for (const MovableObject* item: *actor->GetInventory()) {
			const HeldDevice* device = dynamic_cast<const HeldDevice*>(item);
			if (!device || !device->IsPowerup()) {
				continue;
			}
			const PowerupInfo& info = device->GetPowerup();
			const bool usable = actor->CanUsePowerup(device);
			std::string label = device->GetPresetName();
			if (!info.Spell.empty()) {
				const int casts = std::max(info.Casts, 1);
				label += " (" + std::to_string(casts) + (casts == 1 ? " cast)" : " casts)");
			} else {
				// (What it gives that is running: the longest left.)
				float running = 0.0F;
				const float seconds[] = {info.Shield, info.Stim, info.Overcharge, info.Cloak};
				for (int effect = 0; effect < static_cast<int>(Actor::PowerupEffect::Count); ++effect) {
					if (seconds[effect] > 0.0F) {
						running = std::max(running, actor->GetPowerupSecondsLeft(static_cast<Actor::PowerupEffect>(effect)));
					}
				}
				if (!usable && running > 0.0F) {
					label += ": " + std::to_string(static_cast<int>(std::ceil(running))) + "s";
				} else {
					const int uses = device->GetPowerupUsesLeft();
					label += " (" + std::to_string(uses) + (uses == 1 ? " use)" : " uses)");
				}
			}
			labels.push_back(label);
			enabled.push_back(usable);
			data.push_back(item);
		}
		if (labels.empty()) {
			return;
		}
		menu.Heading("Quick actions", ActionMenu::Kind::Command);
		menu.Choices(UnitAction::Powerup, labels, -1, 2, enabled, data);
	}

	void LayoutUnitMenu(const Actor* actor, const PieMenu* pieMenu) {
		ActionMenu::MenuLayout menu(s_Scale);
		AddQuickActions(menu, actor);
		AddInventory(menu, actor, pieMenu);

		// What it can do: the wheel's other slices, and a vehicle's handbrake.
		const AVehicle* vehicle = dynamic_cast<const AVehicle*>(actor);
		bool hasActions = std::any_of(pieMenu->GetPieSlices().begin(), pieMenu->GetPieSlices().end(), [](const PieSlice* slice) { return !slice->GetSubPieMenu() && !IsInventorySlice(slice); });
		if (vehicle && !vehicle->GetWheels().empty()) {
			menu.Heading("Handbrake", ActionMenu::Kind::State);
			menu.Choices(UnitAction::Handbrake, {"Off", "On"}, vehicle->IsHandbrakeOn() ? 1 : 0);
		}
		if (hasActions) {
			menu.Heading("Actions", ActionMenu::Kind::Command);
			AddSlices(menu, pieMenu->GetPieSlices(), false);
		}
		for (const PieSlice* slice: pieMenu->GetPieSlices()) {
			if (const PieMenu* subPieMenu = slice->GetSubPieMenu(); subPieMenu && !subPieMenu->GetPieSlices().empty()) {
				menu.Heading(SliceLabel(slice), ActionMenu::Kind::Command);
				std::vector<std::string> labels;
				std::vector<bool> enabled;
				std::vector<const void*> data;
				for (const PieSlice* subSlice: subPieMenu->GetPieSlices()) {
					if (!subSlice->GetSubPieMenu()) {
						labels.push_back(SliceLabel(subSlice));
						enabled.push_back(subSlice->IsEnabled());
						data.push_back(subSlice);
					}
				}
				menu.Choices(UnitAction::Slice, labels, -1, 2, enabled, data);
			}
		}

		menu.Heading("Weapons", ActionMenu::Kind::State);
		menu.Choices(UnitAction::WeaponRule, {std::begin(c_WeaponRuleNames), std::end(c_WeaponRuleNames)}, actor->GetWeaponRule());
		menu.Heading("Movement", ActionMenu::Kind::State);
		menu.Choices(UnitAction::MovementRule, {std::begin(c_MovementRuleNames), std::end(c_MovementRuleNames)}, actor->GetMovementRule());
		// (Not a vehicle's: it is how a unit behaves, and a vehicle's crew have their own.)
		if (!vehicle) {
			menu.Heading(actor->IsNonCombatant() ? "Temperament (non-combatant)" : "Temperament", ActionMenu::Kind::State);
			std::vector<std::string> temperaments;
			for (int i = 0; i < Actor::TEMPERAMENTCOUNT; ++i) {
				temperaments.emplace_back(Actor::TemperamentName(i));
			}
			menu.Choices(UnitAction::TemperamentChoice, temperaments, actor->GetTemperament(), 2);
		}
		menu.PlaceAbove(s_Anchor);
		s_Menu = std::move(menu);
	}

	/// Takes an item the unit carries in its hand, putting what it held in its place in the inventory, as the full inventory screen does: a
	/// two-handed item also puts the off hand's away.
	void TakeInHand(Actor* actor, const MovableObject* item) {
		AHuman* human = dynamic_cast<AHuman*>(actor);
		Arm* arm = human ? human->GetFGArm() : nullptr;
		const std::deque<MovableObject*>* inventory = actor->GetInventory();
		auto found = std::find(inventory->begin(), inventory->end(), item);
		const HeldDevice* incoming = found != inventory->end() ? dynamic_cast<const HeldDevice*>(*found) : nullptr;
		if (!arm || !incoming) {
			g_GUISound.UserErrorSound()->Play();
			return;
		}
		bool twoHanded = !incoming->IsDualWieldable() && !incoming->HasObjectInGroup("Shields");
		int index = static_cast<int>(found - inventory->begin());
		arm->SetHeldDevice(dynamic_cast<HeldDevice*>(actor->SetInventoryItemAtIndex(arm->RemoveAttachable(arm->GetHeldDevice()), index)));
		arm->SetHandPos(actor->GetPos() + actor->GetHolsterOffset().GetXFlipped(actor->IsHFlipped()));
		if (Arm* offArm = human->GetBGArm(); twoHanded && offArm && offArm->GetHeldDevice()) {
			actor->AddInventoryItem(offArm->RemoveAttachable(offArm->GetHeldDevice()));
			offArm->SetHandPos(actor->GetPos() + actor->GetHolsterOffset().GetXFlipped(actor->IsHFlipped()));
		}
		if (SoundContainer* sound = actor->GetDeviceSwitchSound()) {
			sound->Play(actor->GetPos(), Players::PlayerOne);
		}
	}

	/// Carries out a pick on the unit's menu.
	/// @return Whether it was a slice or a powerup used, which ends the menu.
	bool PickOnUnitMenu(const ActionMenu::Cell& cell, Actor* actor, PieMenu* pieMenu) {
		if (!cell.Enabled && cell.Action != UnitAction::Slice) {
			g_GUISound.HoverDisabledSound()->Play();
			return false;
		}
		switch (cell.Action) {
			case UnitAction::Slice:
				pieMenu->QueueSliceActivation(static_cast<const PieSlice*>(cell.Data)); // (Its sound, picked or greyed out, is the PieMenu's.)
				return true;
			case UnitAction::Item:
				// (What is in hand already is only shown.)
				if (cell.Data) {
					TakeInHand(actor, static_cast<const MovableObject*>(cell.Data));
				}
				return false;
			case UnitAction::Powerup:
				// (Used, the menu goes, as after a slice: back to the fight.)
				if (cell.Data && actor->UsePowerup(const_cast<MovableObject*>(static_cast<const MovableObject*>(cell.Data)))) {
					g_GUISound.ItemChangeSound()->Play();
					return true;
				}
				g_GUISound.UserErrorSound()->Play();
				return false;
			case UnitAction::Handbrake:
				if (AVehicle* vehicle = dynamic_cast<AVehicle*>(actor)) {
					vehicle->SetHandbrake(cell.Value != 0);
				}
				break;
			case UnitAction::WeaponRule:
				actor->SetWeaponRule(cell.Value);
				break;
			case UnitAction::MovementRule:
				actor->SetMovementRule(cell.Value);
				break;
			case UnitAction::TemperamentChoice:
				// (As when spawned with one: made skittish or a pacifist, it's a non-combatant; made a fighter or defensive, it isn't.)
				actor->SetTemperament(cell.Value);
				actor->SetNonCombatant(cell.Value >= Actor::TEMPERAMENT_SKITTISH);
				break;
			default:
				return false;
		}
		if (!cell.Chosen) {
			g_GUISound.SelectionChangeSound()->Play();
		}
		return false;
	}

	/// Puts the menu away, and time back to the speed it had (unless something else has changed it since).
	void CloseUnitMenu() {
		if (s_TimeScaleSet > 0.0F && std::abs(g_TimerMan.GetTimeScale() - s_TimeScaleSet) < 0.001F) {
			g_TimerMan.SetTimeScale(s_TimeScaleBefore);
		}
		s_TimeScaleSet = -1.0F;
		s_Open = false;
		s_LetGo = false;
		Controller::SetUnitMenuHeldOpen(false);
	}

	/// The pointer as drawn: where the last update left it, plus the mouse moved since (time slowed, updates come less often than frames).
	ImVec2 DrawnPointer() {
		Vector motion = g_UInputMan.GetMouseMovement(Players::PlayerOne);
		return ImVec2(std::clamp(s_Pointer.x + motion.m_X, s_Menu.Min.x, s_Menu.Max.x), std::clamp(s_Pointer.y + motion.m_Y, s_Menu.Min.y, std::max(s_Menu.Max.y, s_Anchor.y)));
	}
} // namespace

void ActionMenu::Update() {
	Activity* activity = g_ActivityMan.GetActivity();
	Actor* actor = activity && g_ActivityMan.IsInActivity() ? activity->GetControlledActor(Players::PlayerOne) : nullptr;
	if (actor && (!g_MovableMan.IsActor(actor) || actor->IsDead())) {
		actor = nullptr;
	}
	PieMenu* pieMenu = actor ? actor->GetPieMenu() : nullptr;
	bool held = g_UInputMan.ElementHeld(Players::PlayerOne, InputElements::INPUT_PIEMENU_ANALOG);
	bool pressed = g_UInputMan.ElementPressed(Players::PlayerOne, InputElements::INPUT_PIEMENU_ANALOG);
	bool released = g_UInputMan.ElementReleased(Players::PlayerOne, InputElements::INPUT_PIEMENU_ANALOG);
	bool usable = pieMenu && pieMenu->IsReplacedByActionMenu() && pieMenu->IsEnabled();
	if (!held && !pressed) {
		s_WaitForLetGo = false;
	}

	if (!s_Open) {
		if (!held || !usable || s_WaitForLetGo) {
			return;
		}
		// Opened: over the unit's aim point, with the pointer on it, and time slowed as the setting says.
		s_Open = true;
		s_Spent = false;
		s_LetGo = false;
		s_Moved = false;
		s_Actor = actor;
		s_Anchor = DebugDraw::ToScreen(actor->GetViewPoint());
		s_Pointer = s_Anchor;
		s_Hover = -1;
		s_TimeScaleBefore = g_TimerMan.GetTimeScale();
		if (float slowed = g_SettingsMan.UnitMenuTimeScale(); slowed < 0.999F && slowed < s_TimeScaleBefore) {
			s_TimeScaleSet = slowed;
			g_TimerMan.SetTimeScale(slowed);
		}
	} else if (actor != s_Actor || !pieMenu || !pieMenu->IsReplacedByActionMenu() || (!usable && !released)) {
		// Another unit, the unit gone, or the wheel shut by something else (a view of the activity's own, the setting changed).
		CloseUnitMenu();
		return;
	}

	s_Scale = std::clamp(g_WindowMan.GetGameViewRect().h / 720.0F, 0.9F, 2.2F);
	LayoutUnitMenu(actor, pieMenu);

	// The pointer, moved by the mouse (which the game keeps for aiming), kept to the panel and the spot it opened on.
	Vector motion = g_UInputMan.GetMouseMovement(Players::PlayerOne);
	s_Moved = s_Moved || motion.m_X != 0.0F || motion.m_Y != 0.0F;
	s_Pointer.x = std::clamp(s_Pointer.x + motion.m_X, s_Menu.Min.x, s_Menu.Max.x);
	s_Pointer.y = std::clamp(s_Pointer.y + motion.m_Y, s_Menu.Min.y, std::max(s_Menu.Max.y, s_Anchor.y));
	int hover = CellAt(s_Menu.Cells, s_Pointer);
	if (hover != s_Hover && hover >= 0 && !s_Spent) {
		(s_Menu.Cells[hover].Enabled ? g_GUISound.HoverChangeSound() : g_GUISound.HoverDisabledSound())->Play();
	}
	s_Hover = hover;

	if (s_LetGo) {
		// Left up after a click (UnitMenuClickToOpen): a left click on a row picks it (a slice ends it), anywhere else puts it away, and so does
		// the right button again.
		if (pressed) {
			s_WaitForLetGo = true;
			CloseUnitMenu();
		} else if (g_UInputMan.MouseButtonPressed(MouseButtons::MOUSE_LEFT, Players::PlayerOne)) {
			bool onPanel = s_Pointer.x >= s_Menu.Min.x && s_Pointer.x < s_Menu.Max.x && s_Pointer.y >= s_Menu.Min.y && s_Pointer.y < s_Menu.Max.y;
			if (s_Hover >= 0 ? PickOnUnitMenu(s_Menu.Cells[s_Hover], actor, pieMenu) : !onPanel) {
				CloseUnitMenu();
			}
		}
		return;
	}

	// Held: a left click picks and leaves the menu up for more (a slice ends it); letting go of the right button over a row the pointer was
	// moved to picks it. (Choosing a setting is the same whether clicked or let go on, so a click then a let-go on it does no harm.)
	if (!s_Spent && s_Hover >= 0 && ((released && s_Moved) || g_UInputMan.MouseButtonPressed(MouseButtons::MOUSE_LEFT, Players::PlayerOne))) {
		if (PickOnUnitMenu(s_Menu.Cells[s_Hover], actor, pieMenu)) {
			s_Spent = true;
		}
	}
	if (released) {
		// Let go: it goes, unless it opens on a click and nothing was picked by letting go (then it stays up, the wheel kept open under it).
		if (g_SettingsMan.UnitMenuClickToOpen() && !s_Spent && !(s_Moved && s_Hover >= 0)) {
			s_LetGo = true;
			Controller::SetUnitMenuHeldOpen(true);
		} else {
			CloseUnitMenu();
		}
	}
}

void ActionMenu::Draw() {
	if (!s_Open || s_Spent || s_Menu.Cells.empty() || !g_MovableMan.IsActor(const_cast<Actor*>(s_Actor))) {
		return;
	}
	ImVec2 tip = DrawnPointer();
	DrawMenu(s_Menu, CellAt(s_Menu.Cells, tip), s_Scale);
	// The pointer: a small gold arrow with a dark rim.
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	float size = 12.0F * s_Scale;
	ImVec2 left(tip.x, tip.y + size);
	ImVec2 right(tip.x + size * 0.7F, tip.y + size * 0.7F);
	drawList->AddTriangleFilled(tip, left, right, IM_COL32(242, 182, 61, 255));
	drawList->AddTriangle(tip, left, right, IM_COL32(20, 18, 10, 255), std::max(1.0F, s_Scale));
}
