#include "ActionMenu.h"

#include "SandboxInternal.h"
#include "GUISound.h"
#include "PieMenu.h"
#include "PieSlice.h"
#include "SettingsMan.h"
#include "UInputMan.h"
#include "ToolWidgets.h"
#include "AVehicle.h"
#include "ACraft.h"
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

namespace {
	// The game's own theme (DebugMan's): olive panels, parchment text. Each kind of section has its own colour: blue for what is done now,
	// green for the units' state, gold for your settings. A choice in use is filled with it, with dark text and a tick; the rest are sunk
	// and dim, so the one in use stands out at a glance.
	const ImU32 c_Text = IM_COL32(232, 224, 190, 255);
	const ImU32 c_TextDim = IM_COL32(150, 146, 120, 255);
	const ImU32 c_TextOnLit = IM_COL32(255, 255, 255, 255); // Light: the pixel font has a dark edge of its own, which smudges dark lettering.

	ImU32 KindColour(ActionMenu::Kind kind, int alpha) {
		switch (kind) {
			case ActionMenu::Kind::Command:
				return IM_COL32(120, 180, 245, alpha);
			case ActionMenu::Kind::State:
				return IM_COL32(125, 215, 120, alpha);
			default:
				return IM_COL32(242, 182, 61, alpha);
		}
	}

	/// Draws one cell that can be picked: its box, filled by its kind and whether it is the one in use, its picture if it has one, and its label.
	void DrawCell(ImDrawList* drawList, const ActionMenu::Cell& cell, bool hovered, float scale) {
		ImFont* font = ImGui::GetFont();
		const float fontSize = 14.0F * scale;
		const float rounding = 3.0F * scale;
		ImU32 colour = KindColour(cell.Section, 255);
		bool lit = cell.Chosen && cell.Section != ActionMenu::Kind::Command;
		ImU32 fill;
		if (lit) {
			// A darker shade of its colour, edged in the colour itself, so the light lettering reads on it.
			auto shade = [colour](int shift) { return static_cast<ImU32>(static_cast<float>((colour >> shift) & 0xFF) * 0.45F) << shift; };
			fill = shade(IM_COL32_R_SHIFT) | shade(IM_COL32_G_SHIFT) | shade(IM_COL32_B_SHIFT) | IM_COL32_A_MASK;
		} else if (cell.Section == ActionMenu::Kind::Command) {
			// Buttons: raised, blue-grey.
			fill = hovered ? IM_COL32(62, 88, 118, 255) : IM_COL32(44, 62, 84, 255);
		} else {
			// Choices not in use: sunk into the panel.
			fill = hovered ? IM_COL32(64, 80, 50, 255) : IM_COL32(30, 37, 25, 255);
		}
		drawList->AddRectFilled(cell.Min, cell.Max, fill, rounding);
		if (cell.Section == ActionMenu::Kind::Command && !hovered) {
			drawList->AddRect(cell.Min, cell.Max, KindColour(ActionMenu::Kind::Command, 90), rounding, 0, 1.0F);
		} else if (!lit && !hovered) {
			drawList->AddRect(cell.Min, cell.Max, IM_COL32(70, 80, 58, 255), rounding, 0, 1.0F);
		}
		if (lit && !hovered) {
			drawList->AddRect(cell.Min, cell.Max, colour, rounding, 0, std::max(1.0F, 1.5F * scale));
		}
		if (hovered) {
			drawList->AddRect(cell.Min, cell.Max, lit ? IM_COL32(255, 250, 230, 255) : colour, rounding, 0, std::max(1.0F, 1.5F * scale));
		}
		// Its picture at its left, fitted to the box's height (kept to whole pixels of the sprite where it fits, so it stays crisp).
		float left = cell.Min.x + 4.0F * scale;
		if (cell.Picture) {
			const PiecePicture& picture = PictureOfBitmap(static_cast<BITMAP*>(const_cast<void*>(cell.Picture)));
			if (picture.Texture != 0 && picture.Width > 0 && picture.Height > 0) {
				float room = cell.Max.y - cell.Min.y - 4.0F * scale;
				float fit = std::min(room / static_cast<float>(picture.Height), room / static_cast<float>(picture.Width));
				fit = fit >= 1.0F ? std::floor(fit) : fit;
				ImVec2 size(static_cast<float>(picture.Width) * fit, static_cast<float>(picture.Height) * fit);
				ImVec2 at(std::floor(left + (room - size.x) * 0.5F), std::floor((cell.Min.y + cell.Max.y - size.y) * 0.5F));
				drawList->AddImage(static_cast<ImTextureID>(picture.Texture), at, ImVec2(at.x + size.x, at.y + size.y), ImVec2(0, 0), ImVec2(1, 1), cell.Enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110));
				left += room + 3.0F * scale;
			}
		}
		// A tick before the label of the one in use.
		float tickRoom = lit ? 12.0F * scale : 0.0F;
		float room = cell.Max.x - left - 4.0F * scale - tickRoom;
		std::string label = FitLabel(font, fontSize, cell.Label, room);
		ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0F, label.c_str());
		// (Centred in the room left beside the picture.)
		ImVec2 at(std::floor(left + tickRoom + (room - size.x) * 0.5F), std::floor((cell.Min.y + cell.Max.y - size.y) * 0.5F));
		if (lit) {
			float midY = (cell.Min.y + cell.Max.y) * 0.5F;
			float x = at.x - tickRoom;
			ImVec2 tick[] = {ImVec2(x + 1.0F * scale, midY), ImVec2(x + 4.0F * scale, midY + 3.0F * scale), ImVec2(x + 9.0F * scale, midY - 4.0F * scale)};
			drawList->AddPolyline(tick, 3, c_TextOnLit, ImDrawFlags_None, std::max(1.5F, 2.0F * scale));
		}
		drawList->AddText(font, fontSize, at, lit ? c_TextOnLit : (cell.Enabled ? c_Text : c_TextDim), label.c_str());
	}
} // namespace

void ActionMenu::DrawMenu(const MenuLayout& menu, int hover, float scale) {
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	ImFont* font = ImGui::GetFont();
	const float fontSize = 14.0F * scale;
	const float rounding = 4.0F * scale;
	const float line = std::max(1.0F, scale);
	const ImU32 panel = IM_COL32(38, 46, 32, 238);
	const ImU32 goldDim = IM_COL32(170, 128, 48, 255);
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
		if (cell.Action < 0) {
			// The heading, in its section's colour, with the kind of section as a tag on the right.
			ImU32 colour = KindColour(cell.Section, 255);
			const float tagSize = fontSize * 0.78F;
			const char* tag = tagOf(cell.Section);
			ImVec2 tagExtent = font->CalcTextSizeA(tagSize, FLT_MAX, 0.0F, tag);
			float tagLeft = std::floor(cell.Max.x - tagExtent.x - 4.0F * scale);
			std::string label = FitLabel(font, fontSize * 0.85F, cell.Label, tagLeft - cell.Min.x - 8.0F * scale);
			drawList->AddText(font, fontSize * 0.85F, ImVec2(cell.Min.x + 2.0F, cell.Min.y + 2.0F * scale), colour, label.c_str());
			float tagTop = std::floor(cell.Min.y + 2.0F * scale + (fontSize * 0.85F - tagSize) * 0.5F);
			drawList->AddRectFilled(ImVec2(tagLeft - 3.0F * scale, tagTop - 1.0F * scale), ImVec2(cell.Max.x, tagTop + tagExtent.y + 1.0F * scale), KindColour(cell.Section, 60), 2.0F * scale);
			drawList->AddText(font, tagSize, ImVec2(tagLeft, tagTop), colour, tag);
			float lineY = std::floor(cell.Max.y - 2.0F * scale);
			drawList->AddLine(ImVec2(cell.Min.x, lineY), ImVec2(cell.Max.x, lineY), KindColour(cell.Section, 110), 1.0F);
			continue;
		}
		DrawCell(drawList, cell, i == hover, scale);
	}
}

namespace {
	/// What a cell of the unit's menu does when picked.
	enum UnitAction {
		Slice, //!< A slice of the wheel (Data), handed to the unit's PieMenu.
		Item, //!< An item the unit carries (Data): taken in hand.
		Powerup, //!< A powerup the unit carries (Data): used (Actor::UsePowerup).
		Handbrake, //!< A vehicle's handbrake set to Value: off (0) or on (1).
		WeaponRule, //!< Sets the unit's weapons rule to Value (Actor::WeaponRule).
		MovementRule, //!< Sets the unit's movement rule to Value (Actor::MovementRule).
		TemperamentChoice, //!< Sets the unit's temperament to Value (Actor::Temperament, NC-1).
		Empty //!< A fixed box the unit has nothing for (UnitSlot): greyed out, does nothing.
	};

	/// The unit's menu round it: the unit in an empty square in the middle and boxes all round it, each action in a fixed place (see
	/// LayoutUnitRing).
	struct UnitRing {
		std::vector<ActionMenu::Cell> Cells; //!< The cells, in window pixels once placed.
		ImVec2 Centre; //!< The unit's middle on screen, which the ring is round (moved in when the ring would go off screen).
		ImVec2 HoleMin; //!< The empty square the unit is in.
		ImVec2 HoleMax;
		ImVec2 Min; //!< Round everything.
		ImVec2 Max;
	};

	bool s_Open = false; //!< Whether the unit's menu is up.
	bool s_Spent = false; //!< A slice was picked while the button is held: the menu is gone till the button is let go and held again.
	bool s_LetGo = false; //!< The right button was let go with the menu left up (UnitMenuClickToOpen): it is put away by a click.
	bool s_Moved = false; //!< The pointer has moved since the menu opened, so letting go of the button over a box picks it.
	bool s_WaitForLetGo = false; //!< The menu was put away with the right button down: it doesn't open again till the button is let go.
	const Actor* s_Actor = nullptr; //!< The unit it was opened on. Not owned.
	ImVec2 s_PointerOffset; //!< The menu's own pointer, moved by the mouse, from the ring's middle: it moves with the ring as the unit moves.
	int s_Hover = -1; //!< The cell under the pointer, or -1.
	float s_Scale = 1.0F; //!< Sizes are for a 720 px high picture, times this.
	float s_TimeScaleBefore = 1.0F; //!< How fast time ran before the menu slowed it.
	float s_TimeScaleSet = -1.0F; //!< The speed the menu slowed time to, or -1 if it didn't.
	UnitRing s_Ring; //!< The menu as last laid out (by Update, for the picks; Draw draws it).

	std::string SliceLabel(const PieSlice* slice) {
		const std::string& description = slice->GetDescription();
		return description.empty() ? slice->GetPresetName() : description;
	}

	ActionMenu::Cell MakeCell(int action, const std::string& label, ActionMenu::Kind kind, const void* data = nullptr, bool enabled = true, int value = 0, bool chosen = false, const void* picture = nullptr) {
		ActionMenu::Cell cell;
		cell.Action = action;
		cell.Label = label;
		cell.Section = kind;
		cell.Data = data;
		cell.Enabled = enabled;
		cell.Value = value;
		cell.Chosen = chosen;
		cell.Picture = picture;
		return cell;
	}

	/// A slice as a cell, with its icon.
	ActionMenu::Cell SliceCell(const PieSlice* slice) {
		return MakeCell(UnitAction::Slice, SliceLabel(slice), ActionMenu::Kind::Command, slice, slice->IsEnabled(), 0, false, slice->HasIcon() ? slice->GetAppropriateIcon() : nullptr);
	}

	/// The first sprite frame of an item, for its picture.
	const void* ItemPicture(const MovableObject* item) {
		const MOSprite* sprite = dynamic_cast<const MOSprite*>(item);
		return sprite ? sprite->GetSpriteFrame(0) : nullptr;
	}

	/// The powerups the unit carries, each used when picked: how many uses it has left, or while what it gives is running, the seconds left.
	void AddQuickActions(std::vector<ActionMenu::Cell>& cells, const Actor* actor) {
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
			cells.push_back(MakeCell(UnitAction::Powerup, label, ActionMenu::Kind::Command, item, usable, 0, false, ItemPicture(item)));
		}
	}

	/// The fixed boxes of the unit's menu. Each is always in the same place round the unit, whatever it carries or can do right now, so the
	/// hand learns where things are: one the unit can't use is there all the same, greyed out.
	enum UnitSlot {
		SlotDrop, //!< Inner ring, top row, left to right.
		SlotInventory,
		SlotPickup,
		SlotPrevious, //!< Inner ring, the sides: upper left and right.
		SlotNext,
		SlotReload, //!< Inner ring, the sides: lower left and right.
		SlotSquad,
		SlotSentry, //!< Inner ring, bottom row, left to right.
		SlotGoTo,
		SlotPatrol,
		SlotBrainHunt, //!< Outer top row, the less used orders, left to right.
		SlotGoldDig,
		SlotDigTo,
		SlotWeapons, //!< Outer bottom row, the rules, left to right.
		SlotMovement,
		SlotTemperament,
		SlotCount
	};

	/// The fixed box a slice of a type has, or -1 for one that goes with the other actions in the right column. A vehicle's and a craft's own
	/// actions share the boxes of a soldier's that they never have alongside.
	int SliceSlot(PieSliceType type) {
		switch (type) {
			case PieSliceType::Drop:
				return SlotDrop;
			case PieSliceType::FullInventory:
				return SlotInventory;
			case PieSliceType::Pickup:
				return SlotPickup;
			case PieSliceType::PreviousItem:
			case PieSliceType::GetOut:
				return SlotPrevious;
			case PieSliceType::NextItem:
			case PieSliceType::ChangeSeat:
				return SlotNext;
			case PieSliceType::Reload:
				return SlotReload;
			case PieSliceType::FormSquad:
				return SlotSquad;
			case PieSliceType::Sentry:
			case PieSliceType::Stay:
				return SlotSentry;
			case PieSliceType::GoTo:
				return SlotGoTo;
			case PieSliceType::Patrol:
			case PieSliceType::Return:
				return SlotPatrol;
			case PieSliceType::BrainHunt:
			case PieSliceType::Deliver:
				return SlotBrainHunt;
			case PieSliceType::GoldDig:
			case PieSliceType::Scuttle:
				return SlotGoldDig;
			case PieSliceType::DigTo:
				return SlotDigTo;
			default:
				return -1;
		}
	}

	/// What an empty fixed box is called, greyed out: what would be there for this kind of unit.
	const char* EmptySlotLabel(int slot, bool vehicle, bool craft) {
		switch (slot) {
			case SlotDrop:
				return "Drop";
			case SlotInventory:
				return "Full Inventory";
			case SlotPickup:
				return "Pick Up";
			case SlotPrevious:
				return vehicle ? "Get Out" : "Prev Item";
			case SlotNext:
				return vehicle ? "Change Seat" : "Next Item";
			case SlotReload:
				return vehicle ? "Handbrake" : "Reload";
			case SlotSquad:
				return "Form Squad";
			case SlotSentry:
				return craft ? "Stay" : "Sentry";
			case SlotGoTo:
				return "Go To";
			case SlotPatrol:
				return craft ? "Return" : "Patrol";
			case SlotBrainHunt:
				return craft ? "Deliver" : "Brain Hunt";
			case SlotGoldDig:
				return craft ? "Scuttle" : "Gold Dig";
			case SlotDigTo:
				return "Dig To Here";
			case SlotWeapons:
				return "Weapons";
			case SlotMovement:
				return "Movement";
			default:
				return "Temperament";
		}
	}

	/// The unit's menu sorted into its places: the fixed boxes (empty where the unit has nothing for one), what it carries for the left
	/// column and its other actions for the right.
	struct UnitCells {
		ActionMenu::Cell Fixed[SlotCount];
		bool Filled[SlotCount] = {};
		std::vector<ActionMenu::Cell> Carried; //!< In hand (lit), off hand, then the rest.
		std::vector<ActionMenu::Cell> Others; //!< Powerups, then the slices with no fixed box (a mod's, the buy menu, the map...).

		/// Puts a cell in its fixed box, or with the others if the box is taken already or it has none.
		void Put(int slot, const ActionMenu::Cell& cell) {
			if (slot >= 0 && !Filled[slot]) {
				Fixed[slot] = cell;
				Filled[slot] = true;
			} else {
				Others.push_back(cell);
			}
		}
	};

	/// What the unit does: a vehicle's handbrake, the wheel's slices and its sub-menus' slices, each in its fixed box or with the others
	/// after its powerups.
	void AddActions(UnitCells& cells, const Actor* actor, const PieMenu* pieMenu) {
		AddQuickActions(cells.Others, actor);
		if (const AVehicle* vehicle = dynamic_cast<const AVehicle*>(actor); vehicle && !vehicle->GetWheels().empty()) {
			bool on = vehicle->IsHandbrakeOn();
			cells.Put(SlotReload, MakeCell(UnitAction::Handbrake, on ? "Handbrake: On" : "Handbrake: Off", ActionMenu::Kind::State, nullptr, true, on ? 0 : 1, on));
		}
		for (const PieSlice* slice: pieMenu->GetPieSlices()) {
			if (const PieMenu* subPieMenu = slice->GetSubPieMenu()) {
				for (const PieSlice* subSlice: subPieMenu->GetPieSlices()) {
					if (!subSlice->GetSubPieMenu()) {
						cells.Put(SliceSlot(subSlice->GetType()), SliceCell(subSlice));
					}
				}
			} else {
				cells.Put(SliceSlot(slice->GetType()), SliceCell(slice));
			}
		}
	}

	/// What the unit carries: what it has in hand (lit) and the rest, each taken in hand when picked.
	void AddInventory(UnitCells& cells, const Actor* actor) {
		const AHuman* human = dynamic_cast<const AHuman*>(actor);
		if (human) {
			if (const HeldDevice* inHand = human->GetEquippedItem()) {
				cells.Carried.push_back(MakeCell(UnitAction::Item, inHand->GetPresetName(), ActionMenu::Kind::State, nullptr, true, 0, true, ItemPicture(inHand)));
			}
			if (const HeldDevice* offHand = human->GetEquippedBGItem()) {
				cells.Carried.push_back(MakeCell(UnitAction::Item, offHand->GetPresetName() + " (off hand)", ActionMenu::Kind::State, nullptr, true, 0, false, ItemPicture(offHand)));
			}
		}
		for (const MovableObject* item: *actor->GetInventory()) {
			// (Powerups are quick actions.)
			if (const HeldDevice* device = dynamic_cast<const HeldDevice*>(item); device && device->IsPowerup()) {
				continue;
			}
			const HDFirearm* scrollSpell = dynamic_cast<const HDFirearm*>(item);
			const int casts = scrollSpell ? scrollSpell->GetScrollCasts() : -1;
			std::string label = casts >= 0 ? item->GetPresetName() + " (" + std::to_string(casts) + (casts == 1 ? " cast)" : " casts)") : item->GetPresetName();
			// (Only a unit with a hand to hold it takes an item in hand; others' inventories are shown, greyed.)
			bool takeable = human && human->GetFGArm() && dynamic_cast<const HeldDevice*>(item);
			cells.Carried.push_back(MakeCell(UnitAction::Item, label, ActionMenu::Kind::State, item, takeable, 0, false, ItemPicture(item)));
		}
	}

	/// The unit's rules, one box each that goes on to the next choice when picked.
	void AddRules(UnitCells& cells, const Actor* actor) {
		constexpr int weaponRules = static_cast<int>(std::size(c_WeaponRuleNames));
		constexpr int movementRules = static_cast<int>(std::size(c_MovementRuleNames));
		int weapon = std::clamp(actor->GetWeaponRule(), 0, weaponRules - 1);
		int movement = std::clamp(actor->GetMovementRule(), 0, movementRules - 1);
		cells.Put(SlotWeapons, MakeCell(UnitAction::WeaponRule, std::string("Weapons: ") + c_WeaponRuleNames[weapon], ActionMenu::Kind::Setting, nullptr, true, (weapon + 1) % weaponRules));
		cells.Put(SlotMovement, MakeCell(UnitAction::MovementRule, std::string("Movement: ") + c_MovementRuleNames[movement], ActionMenu::Kind::Setting, nullptr, true, (movement + 1) % movementRules));
		// (Not a vehicle's: it is how a unit behaves, and a vehicle's crew have their own.)
		if (!dynamic_cast<const AVehicle*>(actor)) {
			int temperament = std::clamp(actor->GetTemperament(), 0, Actor::TEMPERAMENTCOUNT - 1);
			cells.Put(SlotTemperament, MakeCell(UnitAction::TemperamentChoice, std::string(actor->IsNonCombatant() ? "Non-combatant: " : "Temperament: ") + Actor::TemperamentName(temperament), ActionMenu::Kind::Setting, nullptr, true, (temperament + 1) % Actor::TEMPERAMENTCOUNT));
		}
	}

	/// Lays out the menu round the unit, every box in a fixed place: an empty square the unit's size in the middle (at least two boxes high
	/// and one wide), the inner ring of ten fixed boxes round it (three above, two down each side, three below), a row of the less used
	/// orders above that and a row of the rules below. What it carries is a column down the left and its other actions one down the right,
	/// both growing downwards, so however many there are they never move the fixed boxes.
	void LayoutUnitRing(const Actor* actor, const PieMenu* pieMenu) {
		UnitCells sorted;
		AddActions(sorted, actor, pieMenu);
		AddInventory(sorted, actor);
		AddRules(sorted, actor);

		const float boxWidth = 118.0F * s_Scale;
		const float boxHeight = 26.0F * s_Scale;
		const float gap = 3.0F * s_Scale;
		// The unit's square: as big as the unit on screen (its radius), at least room for the boxes round it.
		ImVec2 centre = DebugDraw::ToScreen(actor->GetPos());
		float radius = std::abs(DebugDraw::ToScreen(actor->GetPos() + Vector(actor->GetRadius(), 0.0F)).x - centre.x);
		float halfWidth = std::max(radius, boxWidth * 0.5F);
		float halfHeight = std::max(radius, boxHeight + gap * 0.5F);
		UnitRing ring;
		ring.HoleMin = ImVec2(-halfWidth, -halfHeight);
		ring.HoleMax = ImVec2(halfWidth, halfHeight);

		// The columns and rows, as offsets from the middle.
		const float leftX = -halfWidth - gap - boxWidth;
		const float middleX = -boxWidth * 0.5F;
		const float rightX = halfWidth + gap;
		const float upperY = -boxHeight - gap * 0.5F;
		const float lowerY = gap * 0.5F;
		const float topY = -halfHeight - gap - boxHeight;
		const float bottomY = halfHeight + gap;
		const float outerTopY = topY - gap - boxHeight;
		const float outerBottomY = bottomY + boxHeight + gap;
		const ImVec2 fixedPlaces[SlotCount] = {
		    {leftX, topY}, {middleX, topY}, {rightX, topY},
		    {leftX, upperY}, {rightX, upperY},
		    {leftX, lowerY}, {rightX, lowerY},
		    {leftX, bottomY}, {middleX, bottomY}, {rightX, bottomY},
		    {leftX, outerTopY}, {middleX, outerTopY}, {rightX, outerTopY},
		    {leftX, outerBottomY}, {middleX, outerBottomY}, {rightX, outerBottomY}};

		const bool vehicle = dynamic_cast<const AVehicle*>(actor) != nullptr;
		const bool craft = dynamic_cast<const ACraft*>(actor) != nullptr;
		std::vector<ActionMenu::Cell> cells;
		std::vector<ImVec2> places;
		for (int slot = 0; slot < SlotCount; ++slot) {
			cells.push_back(sorted.Filled[slot] ? sorted.Fixed[slot] : MakeCell(UnitAction::Empty, EmptySlotLabel(slot, vehicle, craft), slot >= SlotWeapons ? ActionMenu::Kind::Setting : ActionMenu::Kind::Command, nullptr, false));
			places.push_back(fixedPlaces[slot]);
		}
		for (size_t i = 0; i < sorted.Carried.size(); ++i) {
			cells.push_back(sorted.Carried[i]);
			places.emplace_back(leftX - gap - boxWidth, topY + static_cast<float>(i) * (boxHeight + gap));
		}
		for (size_t i = 0; i < sorted.Others.size(); ++i) {
			cells.push_back(sorted.Others[i]);
			places.emplace_back(rightX + boxWidth + gap, topY + static_cast<float>(i) * (boxHeight + gap));
		}

		// Placed, and the whole of it moved in from the edges of the game's picture if it would go off them.
		ImVec2 low(ring.HoleMin);
		ImVec2 high(ring.HoleMax);
		for (size_t i = 0; i < cells.size(); ++i) {
			cells[i].Min = places[i];
			cells[i].Max = ImVec2(places[i].x + boxWidth, places[i].y + boxHeight);
			low = ImVec2(std::min(low.x, cells[i].Min.x), std::min(low.y, cells[i].Min.y));
			high = ImVec2(std::max(high.x, cells[i].Max.x), std::max(high.y, cells[i].Max.y));
		}
		GameViewRect view = g_WindowMan.GetGameViewRect();
		auto keepIn = [](float middle, float low, float high, float viewLow, float viewSize) {
			float lowest = viewLow + 4.0F - low;
			float highest = viewLow + viewSize - 4.0F - high;
			return lowest > highest ? (lowest + highest) * 0.5F : std::clamp(middle, lowest, highest);
		};
		centre = ImVec2(std::floor(keepIn(centre.x, low.x, high.x, static_cast<float>(view.x), static_cast<float>(view.w))), std::floor(keepIn(centre.y, low.y, high.y, static_cast<float>(view.y), static_cast<float>(view.h))));
		for (ActionMenu::Cell& cell: cells) {
			cell.Min = ImVec2(cell.Min.x + centre.x, cell.Min.y + centre.y);
			cell.Max = ImVec2(cell.Max.x + centre.x, cell.Max.y + centre.y);
		}
		ring.Centre = centre;
		ring.HoleMin = ImVec2(ring.HoleMin.x + centre.x, ring.HoleMin.y + centre.y);
		ring.HoleMax = ImVec2(ring.HoleMax.x + centre.x, ring.HoleMax.y + centre.y);
		ring.Min = ImVec2(low.x + centre.x, low.y + centre.y);
		ring.Max = ImVec2(high.x + centre.x, high.y + centre.y);
		ring.Cells = std::move(cells);
		s_Ring = std::move(ring);
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
		g_GUISound.SelectionChangeSound()->Play();
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

	/// Keeps the pointer, as an offset from the ring's middle, to the ring.
	ImVec2 KeptToRing(const ImVec2& offset) {
		return ImVec2(std::clamp(offset.x, s_Ring.Min.x - s_Ring.Centre.x, s_Ring.Max.x - s_Ring.Centre.x), std::clamp(offset.y, s_Ring.Min.y - s_Ring.Centre.y, s_Ring.Max.y - s_Ring.Centre.y));
	}

	/// The pointer on screen: where the last update left it, plus the mouse moved since (time slowed, updates come less often than frames).
	ImVec2 DrawnPointer() {
		Vector motion = g_UInputMan.GetMouseMovement(Players::PlayerOne);
		ImVec2 offset = KeptToRing(ImVec2(s_PointerOffset.x + motion.m_X, s_PointerOffset.y + motion.m_Y));
		return ImVec2(s_Ring.Centre.x + offset.x, s_Ring.Centre.y + offset.y);
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
		// Opened: round the unit, with the pointer on it, and time slowed as the setting says.
		s_Open = true;
		s_Spent = false;
		s_LetGo = false;
		s_Moved = false;
		s_Actor = actor;
		s_PointerOffset = ImVec2(0.0F, 0.0F);
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
	// (Laid out afresh each update round where the unit is now, so the ring follows it.)
	LayoutUnitRing(actor, pieMenu);

	// The pointer, moved by the mouse (which the game keeps for aiming), kept to the ring and moving with it.
	Vector motion = g_UInputMan.GetMouseMovement(Players::PlayerOne);
	s_Moved = s_Moved || motion.m_X != 0.0F || motion.m_Y != 0.0F;
	s_PointerOffset = KeptToRing(ImVec2(s_PointerOffset.x + motion.m_X, s_PointerOffset.y + motion.m_Y));
	ImVec2 pointer(s_Ring.Centre.x + s_PointerOffset.x, s_Ring.Centre.y + s_PointerOffset.y);
	int hover = CellAt(s_Ring.Cells, pointer);
	if (hover != s_Hover && hover >= 0 && !s_Spent) {
		(s_Ring.Cells[hover].Enabled ? g_GUISound.HoverChangeSound() : g_GUISound.HoverDisabledSound())->Play();
	}
	s_Hover = hover;

	if (s_LetGo) {
		// Left up after a click (UnitMenuClickToOpen): a left click on a box picks it (a slice ends it), off the boxes puts it away, and so does
		// the right button again.
		if (pressed) {
			s_WaitForLetGo = true;
			CloseUnitMenu();
		} else if (g_UInputMan.MouseButtonPressed(MouseButtons::MOUSE_LEFT, Players::PlayerOne)) {
			if (s_Hover < 0 || PickOnUnitMenu(s_Ring.Cells[s_Hover], actor, pieMenu)) {
				CloseUnitMenu();
			}
		}
		return;
	}

	// Held: a left click picks and leaves the menu up for more (a slice ends it); letting go of the right button over a box the pointer was
	// moved to picks it.
	if (!s_Spent && s_Hover >= 0 && ((released && s_Moved) || g_UInputMan.MouseButtonPressed(MouseButtons::MOUSE_LEFT, Players::PlayerOne))) {
		if (PickOnUnitMenu(s_Ring.Cells[s_Hover], actor, pieMenu)) {
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
	if (!s_Open || s_Spent || s_Ring.Cells.empty() || !g_MovableMan.IsActor(const_cast<Actor*>(s_Actor))) {
		return;
	}
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	ImVec2 tip = DrawnPointer();
	int hover = CellAt(s_Ring.Cells, tip);
	// The unit's square: corner marks in gold, so the eye finds the middle the ring is round.
	const float corner = 7.0F * s_Scale;
	const float line = std::max(1.0F, 1.5F * s_Scale);
	const ImU32 gold = IM_COL32(242, 182, 61, 200);
	const ImVec2& a = s_Ring.HoleMin;
	const ImVec2& b = s_Ring.HoleMax;
	drawList->AddLine(a, ImVec2(a.x + corner, a.y), gold, line);
	drawList->AddLine(a, ImVec2(a.x, a.y + corner), gold, line);
	drawList->AddLine(ImVec2(b.x, a.y), ImVec2(b.x - corner, a.y), gold, line);
	drawList->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + corner), gold, line);
	drawList->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + corner, b.y), gold, line);
	drawList->AddLine(ImVec2(a.x, b.y), ImVec2(a.x, b.y - corner), gold, line);
	drawList->AddLine(b, ImVec2(b.x - corner, b.y), gold, line);
	drawList->AddLine(b, ImVec2(b.x, b.y - corner), gold, line);
	for (int i = 0; i < static_cast<int>(s_Ring.Cells.size()); ++i) {
		// (Each box on a faint dark plate a little bigger than it, so the ring reads over a busy scene.)
		const Cell& cell = s_Ring.Cells[i];
		drawList->AddRectFilled(ImVec2(cell.Min.x - 1.0F, cell.Min.y - 1.0F), ImVec2(cell.Max.x + 1.0F, cell.Max.y + 1.0F), IM_COL32(10, 12, 8, 150), 4.0F * s_Scale);
		DrawCell(drawList, cell, i == hover, s_Scale);
	}
	// The box under the pointer, named in full over the ring (its label in the box may be cut short).
	if (hover >= 0) {
		ImFont* font = ImGui::GetFont();
		const float fontSize = 14.0F * s_Scale;
		const std::string& label = s_Ring.Cells[hover].Label;
		ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0F, label.c_str());
		ImVec2 at(std::floor(s_Ring.Centre.x - size.x * 0.5F), std::floor(s_Ring.Min.y - size.y - 6.0F * s_Scale));
		drawList->AddRectFilled(ImVec2(at.x - 5.0F * s_Scale, at.y - 2.0F * s_Scale), ImVec2(at.x + size.x + 5.0F * s_Scale, at.y + size.y + 2.0F * s_Scale), IM_COL32(20, 24, 16, 230), 3.0F * s_Scale);
		drawList->AddText(font, fontSize, at, c_Text, label.c_str());
	}
	// The pointer: a small gold arrow with a dark rim.
	float size = 12.0F * s_Scale;
	ImVec2 left(tip.x, tip.y + size);
	ImVec2 right(tip.x + size * 0.7F, tip.y + size * 0.7F);
	drawList->AddTriangleFilled(tip, left, right, IM_COL32(242, 182, 61, 255));
	drawList->AddTriangle(tip, left, right, IM_COL32(20, 18, 10, 255), std::max(1.0F, s_Scale));
}
