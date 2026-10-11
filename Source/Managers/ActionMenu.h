#pragma once

#include "imgui/imgui.h"

#include <string>
#include <vector>

namespace RTE {

	/// The action menu (RC-12): what holding right click opens in place of a wheel. A list above the pointer, on one layer, in the sandbox and
	/// settings style: headings across a panel, and under each its choices side by side, the one in use lit in gold.
	///
	/// On a unit the player is playing it stands in for the pie wheel as the unit menu: a square ring of boxes round the unit, which sits in an
	/// empty square in the middle and is followed as it moves. The boxes nearest the unit are what it does (its quick actions, a vehicle's
	/// handbrake, the wheel's slices and its sub-menus'), then what it carries (in hand, lit, then the rest, taken in hand when picked, with
	/// the wheel's inventory slices), and furthest out its rules (weapons, movement, temperament: one box each, going on to the next choice when
	/// picked). Each box has the slice's icon or the item's picture beside its label, and the one under the pointer is named in full over
	/// the ring. With the "Unit menu opens on a click" setting a right click leaves it up till a click off the boxes or another right click;
	/// otherwise it is up while the button is held. Time runs at the "Time while the unit menu is up" speed. Moving the mouse moves the
	/// menu's own pointer (the game keeps the mouse for aiming); a left click picks, and so does letting go of the right button over a box
	/// the pointer was moved to. A slice picked is handed to the unit's PieMenu (PieMenu::QueueSliceActivation), which carries it out on its
	/// next update just as a pick on the wheel.
	/// The wheel comes back with the "Classic pie wheel" setting; a gamepad, and players after the first, always have it (see
	/// PieMenu::IsReplacedByActionMenu). The sandbox's command ring is laid out and drawn with the same pieces (MenuLayout, DrawMenu).
	class ActionMenu {

	public:
		/// What a section of a list menu is, each drawn its own way so they can't be taken for each other.
		enum class Kind {
			Command, //!< Done the moment it is picked (an order, a slice of the wheel): buttons, in blue, never lit.
			State, //!< How the units are now (their weapons and movement rules, their AI mode): the one they have lit, in green.
			Setting //!< Yours, kept for every order to come (what clicks do, the formation, the markers): the one in use lit, in gold.
		};

		/// One cell of a list menu, in window pixels.
		struct Cell {
			ImVec2 Min;
			ImVec2 Max;
			std::string Label;
			int Action = -1; //!< What picking it does, as its menu numbers them; -1 for a heading, which isn't picked.
			int Value = 0; //!< Which of its row's choices it is.
			const void* Data = nullptr; //!< Anything else its menu keeps with it (the PieSlice of a slice). Not owned.
			bool Chosen = false; //!< The choice in use, lit in gold.
			bool Enabled = true; //!< False for one shown greyed out.
			Kind Section = Kind::Command; //!< The kind of section it is in (a heading: the kind it starts).
			const void* Picture = nullptr; //!< A BITMAP (8-bit, the game's palette) drawn at its left: the unit menu's slice icons and item sprites. Not owned.
		};

		/// Lays out a list menu: headings across the panel, and rows of cells side by side under them. Fixed sizes, no text measured, so it
		/// lays out the same wherever it is called from.
		class MenuLayout {

		public:
			/// @param scale Sizes are for a 720 px high picture, times this.
			explicit MenuLayout(float scale);

			/// A heading across the panel, starting a section of a kind: the choices under it are drawn as that kind, and the heading is
			/// tagged with it (DO NOW, UNITS' STATE, SETTING).
			void Heading(const std::string& text, Kind kind);

			/// Draws the choices after this as another kind, with no heading of their own (buttons under a section of states).
			void SwitchKind(Kind kind) { m_Kind = kind; }

			/// Choices side by side, at most perRow to a row (all on one row with 0), the one at chosen lit (none with -1).
			void Choices(int action, const std::vector<std::string>& labels, int chosen, int perRow = 0, const std::vector<bool>& enabled = {}, const std::vector<const void*>& data = {});

			/// Places the panel centred above a point (a little clear of it), kept inside the game's picture, and moves the cells there.
			void PlaceAbove(const ImVec2& point);

			std::vector<Cell> Cells; //!< The cells, headings included, in window pixels once placed.
			ImVec2 Min; //!< The panel, in window pixels once placed.
			ImVec2 Max;

		private:
			float m_Scale;
			float m_Width;
			float m_Pad;
			float m_Gap;
			float m_RowHeight;
			float m_HeadingHeight;
			float m_Y;
			Kind m_Kind = Kind::Command;
		};

		/// The cell that can be picked at a point, or -1.
		static int CellAt(const std::vector<Cell>& cells, const ImVec2& point);

		/// Draws a laid out list menu on the foreground, the cell at hover outlined.
		static void DrawMenu(const MenuLayout& menu, int hover, float scale);

		/// Opens and shuts the unit's menu with the right button, moves its pointer and takes the picks, for the first player's unit. Call
		/// once each sim update, after the units' update (which opens the unit's PieMenu) and before the update's input is let go of.
		static void Update();

		/// Draws the unit's menu as Update last laid it out. Call once per frame while building the ImGui frame.
		static void Draw();
	};
} // namespace RTE
