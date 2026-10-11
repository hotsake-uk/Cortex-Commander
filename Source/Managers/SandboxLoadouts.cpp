// The Loadout Builder and the Squad Builder: units of your own design (a unit and exactly what it carries) and squads of them or of plain
// units, kept between games, offered wherever the sandbox spawns units (the Spawn tab, Drop squad, a vehicle's crew, the You tab), and
// squads that keep together once spawned.

#include "SandboxInternal.h"

namespace SandboxDetail {
	namespace {
		bool s_LoadoutsLoaded = false;

		/// A squad of yours in the world: who is in it and who leads.
		struct PlayerSquad {
			std::string Name;
			int Team = 0;
			Order Orders = Order::Hold; //!< What the leader was told: a new leader is told the same.
			std::vector<UnitRef> Members; //!< In the squad's order. (Not all out in the world yet if a craft brings them.)
			std::vector<bool> Seen; //!< Each member: has been out in the world.
			std::vector<bool> Joined; //!< Each member: has been set to follow the leader (or is the leader).
			size_t Leader = 0; //!< Index into Members.
			int Age = 0; //!< Sim updates since it was made.
			int LeaderWait = 0; //!< Sim updates others have been out while the leader never was.
		};
		std::vector<PlayerSquad> s_PlayerSquads;

		constexpr int c_SquadGiveUp = 60 * 90; //!< Sim updates a squad none of which has come out is kept before it is forgotten (a craft shot down).
		constexpr int c_LeaderWaitLimit = 120; //!< Sim updates followers wait out in the world for a leader who never comes out.

		/// What can't be in a name in the file: its separators.
		std::string CleanName(std::string name) {
			for (char& c: name) {
				if (c == '|' || c == ';' || c == '*' || c == '\n' || c == '\r') {
					c = ' ';
				}
			}
			while (!name.empty() && name.back() == ' ') {
				name.pop_back();
			}
			while (!name.empty() && name.front() == ' ') {
				name.erase(name.begin());
			}
			return name;
		}

		std::vector<std::string> Split(const std::string& text, char by) {
			std::vector<std::string> parts;
			size_t from = 0;
			while (from <= text.size()) {
				size_t to = text.find(by, from);
				if (to == std::string::npos) {
					to = text.size();
				}
				parts.push_back(text.substr(from, to - from));
				from = to + 1;
			}
			return parts;
		}

		/// A name not yet taken by another loadout (or squad), made from wanted with a number after it if need be.
		template <typename List> std::string UniqueName(const List& list, const std::string& wanted, const void* except = nullptr) {
			std::string base = CleanName(wanted);
			if (base.empty()) {
				base = "Unnamed";
			}
			std::string name = base;
			for (int number = 2;; ++number) {
				bool taken = false;
				for (const auto& entry: list) {
					if (&entry != except && entry.Name == name) {
						taken = true;
						break;
					}
				}
				if (!taken) {
					return name;
				}
				name = base + " " + std::to_string(number);
			}
		}

		const Preset* BodyPreset(const std::string& name) {
			const Preset* body = FindPreset(s_Units, name);
			return body ? body : FindPreset(s_Brains, name);
		}

		float CostOf(const Preset* preset, bool own) {
			const SceneObject* object = preset ? dynamic_cast<const SceneObject*>(g_PresetMan.GetEntityPreset(preset->ClassName, preset->PresetName, preset->ModuleID)) : nullptr;
			return object ? (own ? object->GetGoldValue(0, 1.0F, 1.0F) : object->GetTotalValue(0, 1.0F, 1.0F)) : 0.0F;
		}

		/// What a loadout costs: its unit, and what it carries (the faction's kit for a plain unit, as best as can be told: its first picks).
		float CostOf(const CustomLoadout& loadout) {
			const Preset* body = BodyPreset(loadout.Body);
			float cost = CostOf(body, true);
			std::vector<std::string> items = loadout.FactionKit && body ? DefaultKitOf(*body) : loadout.Items;
			for (const std::string& item: items) {
				cost += CostOf(FindPreset(s_Items, item), false);
			}
			return cost;
		}

		/// A preset's picture fitted into a box at the cursor, centred, by whole pixels when it can be; a dark well behind. Takes up the box.
		void PictureBox(const Preset* preset, float width, float height, bool lit = false) {
			ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(width, height));
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(at, ImVec2(at.x + width, at.y + height), lit ? ToolTheme::Panel : ToolTheme::Well);
			drawList->AddRect(at, ImVec2(at.x + width, at.y + height), lit ? ToolTheme::Gold : ToolTheme::Edge);
			if (!preset || !ImGui::IsItemVisible()) {
				return;
			}
			const PiecePicture& picture = PictureOf(*preset);
			if (picture.Width <= 0) {
				return;
			}
			float fit = std::min((width - 4.0F) / static_cast<float>(picture.Width), (height - 4.0F) / static_cast<float>(picture.Height));
			if (fit >= 1.0F) {
				fit = std::min(std::floor(fit), 4.0F);
			}
			ImVec2 size(static_cast<float>(picture.Width) * fit, static_cast<float>(picture.Height) * fit);
			ImVec2 corner(std::floor(at.x + (width - size.x) * 0.5F), std::floor(at.y + (height - size.y) * 0.5F));
			drawList->AddImage(static_cast<ImTextureID>(picture.Texture), corner, ImVec2(corner.x + size.x, corner.y + size.y));
		}

		/// A preset's stats on one line (StatsShort's lines), for beside its picture.
		std::string OneLine(const std::string& lines) {
			std::string line;
			for (const std::string& part: Split(lines, '\n')) {
				if (!part.empty()) {
					line += (line.empty() ? "" : ", ") + part;
				}
			}
			return line;
		}

		std::string CostText(float cost) {
			char text[32];
			std::snprintf(text, sizeof(text), "%.0f oz", cost);
			return cost > 0.0F ? std::string(text) : std::string("free");
		}

		/// Puts the Unit tool in hand, making what pick says.
		void SpawnWith(const CustomPick& pick) {
			s_SpawnPick = pick;
			s_RandomUnits = false;
			TookTool(ToolIndex(Tool::Unit));
		}
	} // namespace

	void LoadLoadoutsFile() {
		if (s_LoadoutsLoaded) {
			return;
		}
		s_LoadoutsLoaded = true;
		std::ifstream file(c_LoadoutsFile);
		std::string line;
		while (std::getline(file, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			std::vector<std::string> parts = Split(line, '|');
			if (parts.size() >= 5 && parts[0] == "Loadout") {
				// Loadout|name|unit|temperament|item;item;...
				CustomLoadout loadout;
				loadout.Name = parts[1];
				loadout.Body = parts[2];
				loadout.Temperament = std::clamp(std::atoi(parts[3].c_str()), -1, Actor::TEMPERAMENTCOUNT - 1);
				for (const std::string& item: Split(parts[4], ';')) {
					if (!item.empty()) {
						loadout.Items.push_back(item);
					}
				}
				if (!loadout.Name.empty() && !FindCustomLoadout(loadout.Name)) {
					s_CustomLoadouts.push_back(loadout);
				}
			} else if (parts.size() >= 3 && parts[0] == "Squad") {
				// Squad|name|U:unit*count;L:loadout*count;...
				CustomSquad squad;
				squad.Name = parts[1];
				for (const std::string& slotText: Split(parts[2], ';')) {
					if (slotText.size() < 3 || slotText[1] != ':') {
						continue;
					}
					SquadSlot slot;
					slot.Custom = slotText[0] == 'L';
					std::string rest = slotText.substr(2);
					if (size_t star = rest.rfind('*'); star != std::string::npos) {
						slot.Count = std::clamp(std::atoi(rest.c_str() + star + 1), 1, 20);
						rest.resize(star);
					}
					slot.Name = rest;
					if (!slot.Name.empty()) {
						squad.Slots.push_back(slot);
					}
				}
				if (!squad.Name.empty() && !FindCustomSquad(squad.Name)) {
					s_CustomSquads.push_back(squad);
				}
			}
		}
	}

	void SaveLoadoutsFile() {
		std::error_code error;
		std::filesystem::create_directories(std::filesystem::path(c_LoadoutsFile).parent_path(), error);
		std::ofstream file(c_LoadoutsFile, std::ios::trunc);
		if (!file) {
			return;
		}
		for (const CustomLoadout& loadout: s_CustomLoadouts) {
			file << "Loadout|" << loadout.Name << '|' << loadout.Body << '|' << loadout.Temperament << '|';
			for (size_t i = 0; i < loadout.Items.size(); ++i) {
				file << (i > 0 ? ";" : "") << loadout.Items[i];
			}
			file << '\n';
		}
		for (const CustomSquad& squad: s_CustomSquads) {
			file << "Squad|" << squad.Name << '|';
			for (size_t i = 0; i < squad.Slots.size(); ++i) {
				file << (i > 0 ? ";" : "") << (squad.Slots[i].Custom ? "L:" : "U:") << squad.Slots[i].Name << '*' << squad.Slots[i].Count;
			}
			file << '\n';
		}
	}

	const CustomLoadout* FindCustomLoadout(const std::string& name) {
		LoadLoadoutsFile();
		auto found = std::find_if(s_CustomLoadouts.begin(), s_CustomLoadouts.end(), [&name](const CustomLoadout& loadout) { return loadout.Name == name; });
		return found != s_CustomLoadouts.end() ? &*found : nullptr;
	}

	const CustomSquad* FindCustomSquad(const std::string& name) {
		LoadLoadoutsFile();
		auto found = std::find_if(s_CustomSquads.begin(), s_CustomSquads.end(), [&name](const CustomSquad& squad) { return squad.Name == name; });
		return found != s_CustomSquads.end() ? &*found : nullptr;
	}

	std::vector<std::string> DefaultKitOf(const Preset& unit) {
		std::vector<std::string> kit;
		const Actor* actor = dynamic_cast<const Actor*>(g_PresetMan.GetEntityPreset(unit.ClassName, unit.PresetName, unit.ModuleID));
		if (!actor) {
			return kit;
		}
		for (const MovableObject* item: *actor->GetInventory()) {
			if (item) {
				kit.push_back(item->GetPresetName());
			}
		}
		// (A beast goes armed only with what it was born with, as GiveLoadout has it.)
		if (actor->IsBeast()) {
			return kit;
		}
		const FactionArmoury* armoury = &ArmouryOf(unit.ModuleID);
		if (armoury->Primaries.empty()) {
			armoury = &ArmouryOf(g_PresetMan.GetModuleID("Coalition.rte"));
		}
		for (const std::vector<const Preset*>* from: {&armoury->Primaries, &armoury->Secondaries, &armoury->Grenades}) {
			if (!from->empty()) {
				kit.push_back(from->front()->PresetName);
			}
		}
		return kit;
	}

	std::vector<CustomLoadout> UnitsOfPick(const CustomPick& pick) {
		std::vector<CustomLoadout> units;
		if (pick.Kind == CustomPick::Loadout) {
			if (const CustomLoadout* loadout = FindCustomLoadout(pick.Name)) {
				units.push_back(*loadout);
			}
		} else if (pick.Kind == CustomPick::Squad) {
			if (const CustomSquad* squad = FindCustomSquad(pick.Name)) {
				for (const SquadSlot& slot: squad->Slots) {
					CustomLoadout unit;
					if (slot.Custom) {
						const CustomLoadout* loadout = FindCustomLoadout(slot.Name);
						if (!loadout) {
							continue;
						}
						unit = *loadout;
					} else {
						unit.Name = slot.Name;
						unit.Body = slot.Name;
						unit.FactionKit = true;
					}
					for (int i = 0; i < slot.Count; ++i) {
						units.push_back(unit);
					}
				}
			}
		}
		return units;
	}

	Actor* MakeCustomUnit(const CustomLoadout& loadout, int team) {
		const Preset* body = BodyPreset(loadout.Body);
		Actor* actor = body ? dynamic_cast<Actor*>(CreateObject(body->ClassName, body->PresetName, body->ModuleID)) : nullptr;
		if (!actor) {
			return nullptr;
		}
		if (loadout.FactionKit) {
			GiveLoadout(actor, *body, 0);
		} else {
			// Exactly what the loadout says: what the unit comes with taken away, the loadout's items given in their order.
			while (MovableObject* item = actor->RemoveInventoryItemAtIndex(0)) {
				delete item;
			}
			for (const std::string& itemName: loadout.Items) {
				if (const Preset* item = FindPreset(s_Items, itemName)) {
					if (MovableObject* object = CreateObject(item->ClassName, item->PresetName, item->ModuleID)) {
						actor->AddInventoryItem(object);
					}
				}
			}
		}
		if (loadout.Temperament >= 0) {
			ApplyTemperament(actor, loadout.Temperament);
		}
		actor->SetTeam(team);
		actor->SetControllerMode(Controller::CIM_AI);
		return actor;
	}

#pragma region Squads in the world
	void FormSquad(const std::vector<Actor*>& members, const std::string& name, int team, Order order) {
		if (members.size() < 2) {
			return;
		}
		PlayerSquad squad;
		squad.Name = name;
		squad.Team = team;
		squad.Orders = order;
		for (Actor* member: members) {
			squad.Members.push_back(MakeRef(member));
		}
		squad.Seen.assign(members.size(), false);
		squad.Joined.assign(members.size(), false);
		squad.Joined[0] = true;
		s_PlayerSquads.push_back(std::move(squad));
	}

	std::vector<Actor*> SquadMatesOf(const Actor* unit) {
		std::vector<Actor*> mates;
		if (!unit) {
			return mates;
		}
		for (const PlayerSquad& squad: s_PlayerSquads) {
			if (std::none_of(squad.Members.begin(), squad.Members.end(), [unit](const UnitRef& ref) { return RefersTo(ref, unit); })) {
				continue;
			}
			if (Actor* leader = GetRef(squad.Members[squad.Leader])) {
				mates.push_back(leader);
			}
			for (size_t i = 0; i < squad.Members.size(); ++i) {
				if (Actor* member = i != squad.Leader ? GetRef(squad.Members[i]) : nullptr) {
					mates.push_back(member);
				}
			}
			break;
		}
		return mates;
	}

	void UpdatePlayerSquads() {
		for (PlayerSquad& squad: s_PlayerSquads) {
			++squad.Age;
			bool anyOut = false;
			for (size_t i = 0; i < squad.Members.size(); ++i) {
				if (GetRef(squad.Members[i])) {
					squad.Seen[i] = true;
					anyOut = true;
				}
			}
			Actor* leader = GetRef(squad.Members[squad.Leader]);
			if (!leader) {
				// Fallen, or never out of the craft while the others are: the next one out leads, with the squad's orders.
				squad.LeaderWait = anyOut && !squad.Seen[squad.Leader] ? squad.LeaderWait + 1 : 0;
				if (squad.Seen[squad.Leader] || squad.LeaderWait > c_LeaderWaitLimit) {
					for (size_t i = 0; i < squad.Members.size(); ++i) {
						if (Actor* next = i != squad.Leader ? GetRef(squad.Members[i]) : nullptr) {
							squad.Seen[squad.Leader] = true;
							squad.Leader = i;
							squad.Joined[i] = true;
							GiveOrder(next, squad.Orders);
							leader = next;
							// Those who were following the fallen one follow the new one.
							for (size_t j = 0; j < squad.Members.size(); ++j) {
								Actor* member = j != i ? GetRef(squad.Members[j]) : nullptr;
								if (member && squad.Joined[j] && member->GetAIMode() == Actor::AIMODE_SQUAD) {
									squad.Joined[j] = false;
								}
							}
							break;
						}
					}
				}
			}
			if (!leader) {
				continue;
			}
			// Each one out and not yet following, set to follow (once: an order given to it later is its own).
			bool wasFollowingPlan = s_FollowingPlan;
			s_FollowingPlan = true; // (So they don't each call out the order.)
			for (size_t i = 0; i < squad.Members.size(); ++i) {
				if (i == squad.Leader || squad.Joined[i]) {
					continue;
				}
				if (Actor* member = GetRef(squad.Members[i])) {
					GuardUnit(member, leader);
					squad.Joined[i] = true;
				}
			}
			s_FollowingPlan = wasFollowingPlan;
		}
		// A squad is forgotten once none of it is left, or none of it ever came out.
		std::erase_if(s_PlayerSquads, [](const PlayerSquad& squad) {
			bool waiting = false;
			for (size_t i = 0; i < squad.Members.size(); ++i) {
				if (GetRef(squad.Members[i])) {
					return false;
				}
				waiting = waiting || !squad.Seen[i];
			}
			return !waiting || squad.Age > c_SquadGiveUp;
		});
	}

	void DrawPlayerSquads() {
		if (s_PlayerSquads.empty() || g_DebugMan.IsPhotoModeHidingHUD()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		for (const PlayerSquad& squad: s_PlayerSquads) {
			const Actor* leader = GetRef(squad.Members[squad.Leader]);
			if (!leader) {
				continue;
			}
			// The lines to each member while any of them is selected; the name always, small, over the leader.
			bool selected = false;
			for (const UnitRef& ref: squad.Members) {
				const Actor* member = GetRef(ref);
				selected = selected || (member && std::any_of(s_Selected.begin(), s_Selected.end(), [member](const UnitRef& chosen) { return RefersTo(chosen, member); }));
			}
			const ImU32 color = c_SideColors[std::clamp(squad.Team, 0, c_Sides - 1)];
			if (selected) {
				for (const UnitRef& ref: squad.Members) {
					const Actor* member = GetRef(ref);
					if (member && member != leader) {
						drawList->AddLine(ToScreen(leader->GetPos()), ToScreen(member->GetPos()), (color & 0x00FFFFFF) | 0x80000000, 1.0F);
					}
				}
			}
			const ImVec2 at = ToScreen(OverHeadPos(leader) - Vector(0.0F, 34.0F));
			const ImVec2 size = ImGui::CalcTextSize(squad.Name.c_str());
			drawList->AddRectFilled(ImVec2(at.x - size.x * 0.5F - 3.0F, at.y - 1.0F), ImVec2(at.x + size.x * 0.5F + 3.0F, at.y + size.y + 1.0F), IM_COL32(0, 0, 0, selected ? 170 : 110), 3.0F);
			drawList->AddText(ImVec2(at.x - size.x * 0.5F, at.y), selected ? color : (color & 0x00FFFFFF) | 0xB0000000, squad.Name.c_str());
		}
	}
#pragma endregion

	bool CustomPickItems(CustomPick& pick, bool squads) {
		LoadLoadoutsFile();
		bool picked = false;
		if (!s_CustomLoadouts.empty()) {
			ImGui::SeparatorText("My loadouts");
			for (const CustomLoadout& loadout: s_CustomLoadouts) {
				ImGui::PushID(loadout.Name.c_str());
				if (ImGui::Selectable(loadout.Name.c_str(), pick.Kind == CustomPick::Loadout && pick.Name == loadout.Name)) {
					pick = {CustomPick::Loadout, loadout.Name};
					picked = true;
				}
				ImGui::SetItemTooltip("%s carrying %zu item%s", loadout.Body.c_str(), loadout.Items.size(), loadout.Items.size() == 1 ? "" : "s");
				ImGui::PopID();
			}
		}
		if (squads && !s_CustomSquads.empty()) {
			ImGui::SeparatorText("My squads");
			for (const CustomSquad& squad: s_CustomSquads) {
				ImGui::PushID(("squad/" + squad.Name).c_str());
				if (ImGui::Selectable(squad.Name.c_str(), pick.Kind == CustomPick::Squad && pick.Name == squad.Name)) {
					pick = {CustomPick::Squad, squad.Name};
					picked = true;
				}
				ImGui::SetItemTooltip("A squad of %d. The first leads, the rest follow it.", static_cast<int>(UnitsOfPick({CustomPick::Squad, squad.Name}).size()));
				ImGui::PopID();
			}
		}
		if (s_CustomLoadouts.empty() && (!squads || s_CustomSquads.empty())) {
			ImGui::TextDisabled("None made yet: the Loadouts button on the bar makes them.");
		}
		if (ImGui::Selectable("Open the Loadouts window...")) {
			s_LoadoutWindowOpen = true;
		}
		return picked;
	}

#pragma region The Loadouts window
	namespace {
		int s_EditLoadout = -1; //!< Index into s_CustomLoadouts being edited.
		int s_EditSquad = -1; //!< Index into s_CustomSquads being edited.
		bool s_PickingBody = false; //!< The unit browser is showing in place of the armoury, to change the loadout's unit.
		int s_SquadSource = 0; //!< What the Squad Builder's browser adds: 0 plain units, 1 your loadouts.
		char s_ListFilter[64] = "";

		/// The left-hand list of a builder: a search, then each entry (with a picture if given) to click, and New, Copy and Delete under it.
		/// @return What was done: 1 new, 2 copy, 3 delete; 0 nothing.
		template <typename List, typename PictureOfEntry> int BuilderList(const char* id, List& list, int& editing, PictureOfEntry pictureOf) {
			int action = 0;
			ImGui::PushID(id);
			ImGui::SetNextItemWidth(-1.0F);
			ImGui::InputTextWithHint("##search", "Search...", s_ListFilter, sizeof(s_ListFilter));
			float buttonsHeight = ImGui::GetFrameHeightWithSpacing();
			ImGui::BeginChild("##list", ImVec2(-1.0F, -buttonsHeight), ImGuiChildFlags_Borders);
			float rowHeight = ImGui::GetFontSize() * 2.6F;
			for (int i = 0; i < static_cast<int>(list.size()); ++i) {
				if (!ContainsIgnoringCase(list[i].Name, s_ListFilter)) {
					continue;
				}
				ImGui::PushID(i);
				ImVec2 at = ImGui::GetCursorPos();
				if (ImGui::Selectable("##entry", editing == i, 0, ImVec2(0.0F, rowHeight))) {
					editing = i;
					s_PickingBody = false;
				}
				ImGui::SetCursorPos(at);
				PictureBox(pictureOf(list[i]), rowHeight * 1.2F, rowHeight, editing == i);
				ImGui::SameLine();
				ImGui::SetCursorPosY(at.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5F);
				ImGui::TextUnformatted(list[i].Name.c_str());
				ImGui::SetCursorPos(ImVec2(at.x, at.y + rowHeight + ImGui::GetStyle().ItemSpacing.y));
				ImGui::PopID();
			}
			if (list.empty()) {
				ImGui::TextDisabled("None yet. New makes one.");
			}
			ImGui::EndChild();
			float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0F) / 3.0F;
			if (ToolUI::Button("New", ImVec2(third, 0.0F))) {
				action = 1;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(editing < 0 || editing >= static_cast<int>(list.size()));
			if (ToolUI::Button("Copy", ImVec2(third, 0.0F))) {
				action = 2;
			}
			ImGui::SameLine();
			if (ToolUI::Button("Delete", ImVec2(third, 0.0F))) {
				action = 3;
			}
			ImGui::EndDisabled();
			ImGui::PopID();
			return action;
		}

		/// The name of a loadout or squad being edited, as a text box: a change is kept once the box is left (made unique among the others).
		/// @param previous Set to the name it had, when changed. @return Whether it changed.
		template <typename List> bool NameBox(List& list, int index, std::string& previous) {
			static char buffer[64] = "";
			static bool typing = false;
			auto& entry = list[index];
			if (!typing) {
				std::snprintf(buffer, sizeof(buffer), "%s", entry.Name.c_str());
			}
			ImGui::SetNextItemWidth(-1.0F);
			ImGui::InputTextWithHint("##name", "Name", buffer, sizeof(buffer));
			typing = ImGui::IsItemActive();
			if (ImGui::IsItemDeactivatedAfterEdit() && entry.Name != buffer) {
				previous = entry.Name;
				entry.Name = UniqueName(list, buffer, &entry);
				return true;
			}
			return false;
		}

		/// Up, down and remove buttons for row i of a list. @return -1 for none pressed, 0 moved (done here), 1 remove it (left to the caller,
		/// after the rows are drawn).
		template <typename Vector> int RowButtons(Vector& rows, size_t i) {
			int pressed = -1;
			ImGui::BeginDisabled(i == 0);
			if (ToolUI::SmallButton("^")) {
				std::swap(rows[i], rows[i - 1]);
				pressed = 0;
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Move it up");
			ImGui::SameLine();
			ImGui::BeginDisabled(i + 1 >= rows.size());
			if (ToolUI::SmallButton("v")) {
				std::swap(rows[i], rows[i + 1]);
				pressed = 0;
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Move it down");
			ImGui::SameLine();
			if (ToolUI::SmallButton("x")) {
				pressed = 1;
			}
			ImGui::SetItemTooltip("Take it out");
			return pressed;
		}

		/// The Loadout Builder: your loadouts on the left; the unit, what it costs and what it carries in the middle; the armoury (or, changing
		/// the unit, every unit) on the right.
		void LoadoutTab() {
			bool changed = false;
			if (ImGui::BeginTable("##loadoutBuilder", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail())) {
				ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 13.0F);
				ImGui::TableSetupColumn("unit", ImGuiTableColumnFlags_WidthStretch, 1.0F);
				ImGui::TableSetupColumn("armoury", ImGuiTableColumnFlags_WidthStretch, 1.1F);
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				int action = BuilderList("loadouts", s_CustomLoadouts, s_EditLoadout, [](const CustomLoadout& loadout) { return BodyPreset(loadout.Body); });
				if (action == 1) {
					// A new one starts as the unit picked on the Spawn tab, with what it is spawned with.
					CustomLoadout loadout;
					const Preset* unit = ChosenPreset(Tool::Unit, ChoiceFor(Tool::Unit));
					unit = unit ? unit : (s_Units.empty() ? nullptr : &s_Units.front());
					loadout.Body = unit ? unit->PresetName : std::string();
					loadout.Items = unit ? DefaultKitOf(*unit) : std::vector<std::string>();
					loadout.Name = UniqueName(s_CustomLoadouts, unit ? "My " + unit->PresetName : std::string("My loadout"));
					s_CustomLoadouts.push_back(loadout);
					s_EditLoadout = static_cast<int>(s_CustomLoadouts.size()) - 1;
					s_PickingBody = true;
					changed = true;
				} else if (action == 2) {
					CustomLoadout copy = s_CustomLoadouts[s_EditLoadout];
					copy.Name = UniqueName(s_CustomLoadouts, copy.Name);
					s_CustomLoadouts.insert(s_CustomLoadouts.begin() + s_EditLoadout + 1, copy);
					++s_EditLoadout;
					changed = true;
				} else if (action == 3) {
					if (s_SpawnPick.Kind == CustomPick::Loadout && s_SpawnPick.Name == s_CustomLoadouts[s_EditLoadout].Name) {
						s_SpawnPick = CustomPick();
					}
					s_CustomLoadouts.erase(s_CustomLoadouts.begin() + s_EditLoadout);
					s_EditLoadout = std::min(s_EditLoadout, static_cast<int>(s_CustomLoadouts.size()) - 1);
					changed = true;
				}

				ImGui::TableNextColumn();
				CustomLoadout* loadout = s_EditLoadout >= 0 && s_EditLoadout < static_cast<int>(s_CustomLoadouts.size()) ? &s_CustomLoadouts[s_EditLoadout] : nullptr;
				ImGui::BeginChild("##unit", ImVec2(0.0F, 0.0F));
				if (!loadout) {
					ImGui::TextWrapped("Pick one of your loadouts on the left, or New to design a unit: pick what it is, then what it carries.");
				} else {
					ImGui::PushID(s_EditLoadout);
					std::string oldName;
					if (NameBox(s_CustomLoadouts, s_EditLoadout, oldName)) {
						// What used it by its old name follows it.
						for (CustomSquad& squad: s_CustomSquads) {
							for (SquadSlot& slot: squad.Slots) {
								if (slot.Custom && slot.Name == oldName) {
									slot.Name = loadout->Name;
								}
							}
						}
						if (s_SpawnPick.Kind == CustomPick::Loadout && s_SpawnPick.Name == oldName) {
							s_SpawnPick.Name = loadout->Name;
						}
						for (std::string* crew: {&s_VehicleTuning.Driver, &s_VehicleTuning.Crew}) {
							if (*crew == "@" + oldName) {
								*crew = "@" + loadout->Name;
							}
						}
						changed = true;
					}
					// The unit, big, with what its game files say about it.
					const Preset* body = BodyPreset(loadout->Body);
					float pictureSize = ImGui::GetFontSize() * 7.0F;
					PictureBox(body, pictureSize, pictureSize);
					ImGui::SameLine();
					ImGui::BeginGroup();
					ImGui::TextUnformatted(loadout->Body.c_str());
					if (!body) {
						ImGui::TextDisabled("(not in this game)");
					} else {
						ImGui::TextDisabled("%s", body->Module.c_str());
						std::string stats = body->Stats;
						while (!stats.empty() && stats.front() == '\n') {
							stats.erase(stats.begin());
						}
						ImGui::PushTextWrapPos(0.0F);
						ImGui::TextUnformatted(stats.c_str());
						ImGui::PopTextWrapPos();
					}
					if (ToolUI::Button(s_PickingBody ? "Back to the armoury" : "Change the unit...")) {
						s_PickingBody = !s_PickingBody;
					}
					ImGui::SetItemTooltip("Pick what it is from every unit there is. Picking one starts it with what that unit is spawned with.");
					ImGui::EndGroup();
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6F);
					int temperament = loadout->Temperament;
					TemperamentCombo("Temperament", &temperament);
					if (temperament != loadout->Temperament) {
						loadout->Temperament = temperament;
						changed = true;
					}
					ImGui::Text("Total cost: %s", CostText(CostOf(*loadout)).c_str());

					ImGui::SeparatorText("What it carries");
					ImGui::TextDisabled("The first is what it holds. Click items in the armoury to add them.");
					int remove = -1;
					float rowHeight = ImGui::GetFontSize() * 2.4F;
					ImGui::BeginChild("##kit", ImVec2(0.0F, -ImGui::GetFrameHeightWithSpacing() * 2.2F), ImGuiChildFlags_Borders);
					for (size_t i = 0; i < loadout->Items.size(); ++i) {
						ImGui::PushID(static_cast<int>(i));
						const Preset* item = FindPreset(s_Items, loadout->Items[i]);
						ImGui::AlignTextToFramePadding();
						ImGui::Text("%d", static_cast<int>(i) + 1);
						ImGui::SameLine();
						PictureBox(item, rowHeight * 1.8F, rowHeight);
						if (item && ImGui::IsItemHovered()) {
							ImGui::SetTooltip("%s\n%s%s", item->PresetName.c_str(), item->Module.c_str(), item->Stats.c_str());
						}
						ImGui::SameLine();
						ImGui::BeginGroup();
						if (item) {
							ImGui::TextUnformatted(loadout->Items[i].c_str());
							ImGui::TextDisabled("%s", OneLine(item->StatsShort).c_str());
						} else {
							ImGui::TextDisabled("%s  (not in this game)", loadout->Items[i].c_str());
						}
						ImGui::EndGroup();
						ImGui::SameLine(std::max(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFontSize() * 4.6F, ImGui::GetCursorPosX()));
						int pressed = RowButtons(loadout->Items, i);
						if (pressed == 1) {
							remove = static_cast<int>(i);
						}
						changed = changed || pressed >= 0;
						ImGui::PopID();
					}
					if (loadout->Items.empty()) {
						ImGui::TextDisabled("Nothing: it goes unarmed.");
					}
					ImGui::EndChild();
					if (remove >= 0) {
						loadout->Items.erase(loadout->Items.begin() + remove);
						changed = true;
					}
					ImGui::BeginDisabled(!body);
					if (ToolUI::Button("Its usual kit")) {
						loadout->Items = DefaultKitOf(*body);
						changed = true;
					}
					ImGui::SetItemTooltip("What this unit is spawned with: its own items and the first of its faction's guns and grenades.");
					ImGui::EndDisabled();
					ImGui::SameLine();
					if (ToolUI::Button("Empty")) {
						loadout->Items.clear();
						changed = true;
					}
					if (ToolUI::Button("Spawn it")) {
						SpawnWith({CustomPick::Loadout, loadout->Name});
					}
					ImGui::SetItemTooltip("Takes up the Unit tool making this loadout: click in the world to put it down (the squad size makes that many).");
					ImGui::SameLine();
					ImGui::BeginDisabled(!Sandbox::IsGodMode() || Sandbox::IsBattleCommand());
					if (ToolUI::Button("Use for my character")) {
						s_Player.Body = loadout->Body;
						s_Player.Kit = loadout->Items;
						s_Player.InheritKit = false;
					}
					ImGui::SetItemTooltip("Your own character (the You tab) is made as this unit with this kit, the next time it is made.");
					ImGui::EndDisabled();
					ImGui::PopID();
				}
				ImGui::EndChild();

				ImGui::TableNextColumn();
				if (loadout && s_PickingBody) {
					ImGui::SeparatorText("Pick the unit");
					std::string body = loadout->Body;
					PictureGrid(Tool::Unit, nullptr, &body, -1.0F, 1);
					if (body != loadout->Body) {
						loadout->Body = body;
						if (const Preset* unit = BodyPreset(body)) {
							loadout->Items = DefaultKitOf(*unit);
						}
						s_PickingBody = false;
						changed = true;
					}
				} else if (loadout) {
					ImGui::SeparatorText("Armoury: click to add");
					std::string added;
					PictureGrid(Tool::Item, nullptr, &added, -1.0F, 2);
					if (!added.empty()) {
						loadout->Items.push_back(added);
						changed = true;
					}
				}
				ImGui::EndTable();
			}
			if (changed) {
				SaveLoadoutsFile();
			}
		}

		/// The Squad Builder: your squads on the left; the squad, slot by slot, in the middle; units or your loadouts to add on the right.
		void SquadTab() {
			bool changed = false;
			if (ImGui::BeginTable("##squadBuilder", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail())) {
				ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 13.0F);
				ImGui::TableSetupColumn("squad", ImGuiTableColumnFlags_WidthStretch, 1.0F);
				ImGui::TableSetupColumn("add", ImGuiTableColumnFlags_WidthStretch, 1.1F);
				ImGui::TableNextRow();

				auto leaderPicture = [](const CustomSquad& squad) -> const Preset* {
					if (squad.Slots.empty()) {
						return nullptr;
					}
					const SquadSlot& first = squad.Slots.front();
					const CustomLoadout* loadout = first.Custom ? FindCustomLoadout(first.Name) : nullptr;
					return BodyPreset(loadout ? loadout->Body : first.Name);
				};
				ImGui::TableNextColumn();
				int action = BuilderList("squads", s_CustomSquads, s_EditSquad, leaderPicture);
				if (action == 1) {
					CustomSquad squad;
					squad.Name = UniqueName(s_CustomSquads, "Squad " + std::to_string(s_CustomSquads.size() + 1));
					s_CustomSquads.push_back(squad);
					s_EditSquad = static_cast<int>(s_CustomSquads.size()) - 1;
					changed = true;
				} else if (action == 2) {
					CustomSquad copy = s_CustomSquads[s_EditSquad];
					copy.Name = UniqueName(s_CustomSquads, copy.Name);
					s_CustomSquads.insert(s_CustomSquads.begin() + s_EditSquad + 1, copy);
					++s_EditSquad;
					changed = true;
				} else if (action == 3) {
					if (s_SpawnPick.Kind == CustomPick::Squad && s_SpawnPick.Name == s_CustomSquads[s_EditSquad].Name) {
						s_SpawnPick = CustomPick();
					}
					s_CustomSquads.erase(s_CustomSquads.begin() + s_EditSquad);
					s_EditSquad = std::min(s_EditSquad, static_cast<int>(s_CustomSquads.size()) - 1);
					changed = true;
				}

				ImGui::TableNextColumn();
				CustomSquad* squad = s_EditSquad >= 0 && s_EditSquad < static_cast<int>(s_CustomSquads.size()) ? &s_CustomSquads[s_EditSquad] : nullptr;
				ImGui::BeginChild("##squad", ImVec2(0.0F, 0.0F));
				if (!squad) {
					ImGui::TextWrapped("Pick one of your squads on the left, or New to put one together from plain units and your own loadouts. Spawned, the first unit leads and the rest follow it.");
				} else {
					ImGui::PushID(s_EditSquad);
					std::string oldName;
					if (NameBox(s_CustomSquads, s_EditSquad, oldName)) {
						if (s_SpawnPick.Kind == CustomPick::Squad && s_SpawnPick.Name == oldName) {
							s_SpawnPick.Name = squad->Name;
						}
						changed = true;
					}
					std::vector<CustomLoadout> units = UnitsOfPick({CustomPick::Squad, squad->Name});
					float cost = 0.0F;
					for (const CustomLoadout& unit: units) {
						cost += CostOf(unit);
					}
					ImGui::Text("%d unit%s, %s", static_cast<int>(units.size()), units.size() == 1 ? "" : "s", CostText(cost).c_str());
					ImGui::SeparatorText("Who is in it");
					int remove = -1;
					float rowHeight = ImGui::GetFontSize() * 2.6F;
					ImGui::BeginChild("##slots", ImVec2(0.0F, -ImGui::GetFrameHeightWithSpacing() * 1.2F), ImGuiChildFlags_Borders);
					for (size_t i = 0; i < squad->Slots.size(); ++i) {
						SquadSlot& slot = squad->Slots[i];
						ImGui::PushID(static_cast<int>(i));
						const CustomLoadout* loadout = slot.Custom ? FindCustomLoadout(slot.Name) : nullptr;
						const Preset* body = BodyPreset(loadout ? loadout->Body : slot.Name);
						PictureBox(body, rowHeight * 1.2F, rowHeight, i == 0);
						ImGui::SameLine();
						ImGui::BeginGroup();
						bool known = slot.Custom ? loadout != nullptr : body != nullptr;
						if (known) {
							ImGui::Text("%s%s", slot.Name.c_str(), i == 0 ? "  (leads)" : "");
						} else {
							ImGui::TextDisabled("%s  (not there)", slot.Name.c_str());
						}
						ImGui::TextDisabled("%s", slot.Custom ? "My loadout" : "Faction's kit");
						ImGui::EndGroup();
						ImGui::SameLine(std::max(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFontSize() * 10.0F, ImGui::GetCursorPosX()));
						ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0F);
						int count = slot.Count;
						if (ImGui::InputInt("##count", &count)) {
							slot.Count = std::clamp(count, 1, 20);
							changed = true;
						}
						ImGui::SetItemTooltip("How many of it");
						ImGui::SameLine();
						int pressed = RowButtons(squad->Slots, i);
						if (pressed == 1) {
							remove = static_cast<int>(i);
						}
						changed = changed || pressed >= 0;
						ImGui::PopID();
					}
					if (squad->Slots.empty()) {
						ImGui::TextDisabled("Nobody yet: click units or loadouts on the right to add them.");
					}
					ImGui::EndChild();
					if (remove >= 0) {
						squad->Slots.erase(squad->Slots.begin() + remove);
						changed = true;
					}
					ImGui::BeginDisabled(units.empty());
					if (ToolUI::Button("Spawn this squad")) {
						SpawnWith({CustomPick::Squad, squad->Name});
					}
					ImGui::EndDisabled();
					ImGui::SetItemTooltip("Takes up the Unit tool making this squad: click in the world to put it down. (Drop squad brings it in a craft.)");
					ImGui::PopID();
				}
				ImGui::EndChild();

				ImGui::TableNextColumn();
				if (squad) {
					ToolUI::RadioButton("Units", &s_SquadSource, 0);
					ImGui::SameLine();
					ToolUI::RadioButton("My loadouts", &s_SquadSource, 1);
					ImGui::TextDisabled("Click to add to the squad.");
					if (s_SquadSource == 0) {
						std::string added;
						PictureGrid(Tool::Unit, nullptr, &added, -1.0F, 3);
						if (!added.empty()) {
							if (!squad->Slots.empty() && !squad->Slots.back().Custom && squad->Slots.back().Name == added) {
								++squad->Slots.back().Count;
							} else {
								squad->Slots.push_back({added, false, 1});
							}
							changed = true;
						}
					} else {
						ImGui::BeginChild("##myLoadouts", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders);
						float rowHeight = ImGui::GetFontSize() * 2.6F;
						for (const CustomLoadout& loadout: s_CustomLoadouts) {
							ImGui::PushID(loadout.Name.c_str());
							ImVec2 at = ImGui::GetCursorPos();
							if (ImGui::Selectable("##add", false, 0, ImVec2(0.0F, rowHeight))) {
								if (!squad->Slots.empty() && squad->Slots.back().Custom && squad->Slots.back().Name == loadout.Name) {
									++squad->Slots.back().Count;
								} else {
									squad->Slots.push_back({loadout.Name, true, 1});
								}
								changed = true;
							}
							ImGui::SetCursorPos(at);
							PictureBox(BodyPreset(loadout.Body), rowHeight * 1.2F, rowHeight);
							ImGui::SameLine();
							ImGui::BeginGroup();
							ImGui::TextUnformatted(loadout.Name.c_str());
							ImGui::TextDisabled("%s, %s", loadout.Body.c_str(), CostText(CostOf(loadout)).c_str());
							ImGui::EndGroup();
							ImGui::SetCursorPos(ImVec2(at.x, at.y + rowHeight + ImGui::GetStyle().ItemSpacing.y));
							ImGui::PopID();
						}
						if (s_CustomLoadouts.empty()) {
							ImGui::TextDisabled("No loadouts yet: make them on the Loadouts tab.");
						}
						ImGui::EndChild();
					}
				}
				ImGui::EndTable();
			}
			if (changed) {
				SaveLoadoutsFile();
			}
		}
	} // namespace

	void LoadoutWindow() {
		if (!s_LoadoutWindowOpen) {
			return;
		}
		LoadLoadoutsFile();
		const ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowSize(ImVec2(std::min(io.DisplaySize.x * 0.8F, ImGui::GetFontSize() * 62.0F), std::min(io.DisplaySize.y * 0.75F, ImGui::GetFontSize() * 40.0F)), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5F, io.DisplaySize.y * 0.45F), ImGuiCond_FirstUseEver, ImVec2(0.5F, 0.5F));
		if (ImGui::Begin("Loadouts###SandboxLoadouts", &s_LoadoutWindowOpen, ImGuiWindowFlags_NoCollapse)) {
			if (ImGui::BeginTabBar("##loadoutTabs")) {
				if (ImGui::BeginTabItem("Loadout Builder")) {
					LoadoutTab();
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Squad Builder")) {
					SquadTab();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}
#pragma endregion
} // namespace SandboxDetail
