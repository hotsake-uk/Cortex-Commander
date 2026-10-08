// The sandbox window, bar, rings and cursor.

#include "SandboxInternal.h"

namespace SandboxDetail {
	void UpdateFreeCamera() {
		if (!s_FreeCamera || !InGame()) {
			s_FreeCameraStarted = false;
			return;
		}
		if (s_CameraWarmupFrames > 0) {
			--s_CameraWarmupFrames;
			return;
		}
		if (!s_FreeCameraStarted) {
			s_FreeCameraStarted = true;
			s_CameraCenter = g_CameraMan.GetOffset(0) + Vector(static_cast<float>(g_FrameMan.GetPlayerScreenWidth()) * 0.5F, static_cast<float>(g_FrameMan.GetPlayerScreenHeight()) * 0.5F);
		}
		ImGuiIO& io = ImGui::GetIO();
		bool movedByHand = false;
		// Dragging with the right button moves the view, unless the tool in hand makes things for a side: then the right button is for the ring of sides, and the
		// middle button (or the keys) moves the view.
		bool rightPans = !TakesSide(CurrentTool().Kind) || !Sandbox::CapturesWorldClicks();
		if (!io.WantCaptureMouse && !s_RingOpen && ((rightPans && ImGui::IsMouseDown(ImGuiMouseButton_Right)) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))) {
			s_CameraCenter -= Vector(io.MouseDelta.x, io.MouseDelta.y) * ScenePixelsPerWindowPixel();
			movedByHand = io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F;
		}
		if (!io.WantCaptureKeyboard) {
			float keySpeed = 400.0F * io.DeltaTime * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0F : 1.0F);
			bool left = ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_A);
			bool right = ImGui::IsKeyDown(ImGuiKey_RightArrow) || ImGui::IsKeyDown(ImGuiKey_D);
			bool up = ImGui::IsKeyDown(ImGuiKey_UpArrow) || ImGui::IsKeyDown(ImGuiKey_W);
			bool down = ImGui::IsKeyDown(ImGuiKey_DownArrow) || ImGui::IsKeyDown(ImGuiKey_S);
			s_CameraCenter.m_X += (right ? keySpeed : 0.0F) - (left ? keySpeed : 0.0F);
			s_CameraCenter.m_Y += (down ? keySpeed : 0.0F) - (up ? keySpeed : 0.0F);
			movedByHand = movedByHand || left || right || up || down;
		}
		if (movedByHand) {
			s_FollowTarget = UnitRef();
			s_FollowAction = false;
		}
		if (Actor* followed = GetRef(s_FollowTarget)) {
			s_CameraCenter += g_SceneMan.ShortestDistance(s_CameraCenter, followed->GetPos(), g_SceneMan.SceneWrapsX()) * std::min(1.0F, io.DeltaTime * 8.0F);
		} else if (s_FollowAction && s_ActionSpotValid) {
			s_CameraCenter += g_SceneMan.ShortestDistance(s_CameraCenter, s_ActionSpot, g_SceneMan.SceneWrapsX()) * std::min(1.0F, io.DeltaTime * 2.0F);
		}
		g_SceneMan.WrapPosition(s_CameraCenter);
		g_SceneMan.ForceBounds(s_CameraCenter);
		g_CameraMan.SetScrollTarget(s_CameraCenter, 1.0F, 0);
		// The god view's own camera follows along, so the two don't fight.
		if (GameActivity* game = CurrentGame(); game && Sandbox::IsGodMode()) {
			game->SetObservationTarget(s_CameraCenter, Players::PlayerOne);
		}
	}



	ToolLook LookOf(Tool kind) {
		switch (kind) {
			case Tool::None:
				return {Icon::Eye, IM_COL32(232, 224, 190, 255)};
			case Tool::Command:
				return {Icon::Arrows, IM_COL32(232, 224, 190, 255)};
			case Tool::Follow:
				return {Icon::Target, IM_COL32(232, 224, 190, 255)};
			case Tool::Possess:
				return {Icon::Person, IM_COL32(242, 182, 61, 255)};
			case Tool::Remove:
				return {Icon::Cross, IM_COL32(239, 106, 91, 255)};
			case Tool::RallyPoint:
				return {Icon::Flag, IM_COL32(242, 182, 61, 255)};
			case Tool::Unit:
				return {Icon::Person, IM_COL32(232, 224, 190, 255)};
			case Tool::Brain:
				return {Icon::Jar, IM_COL32(240, 150, 170, 255)};
			case Tool::Item:
				return {Icon::Gun, IM_COL32(200, 205, 215, 255)};
			case Tool::Structure:
				return {Icon::Wall, IM_COL32(170, 170, 165, 255)};
			case Tool::Drop:
				return {Icon::Down, IM_COL32(232, 224, 190, 255)};
			case Tool::PlayCharacter:
				return {Icon::Person, IM_COL32(130, 220, 120, 255)};
			case Tool::Barracks:
				return {Icon::Wall, IM_COL32(242, 182, 61, 255)};
			case Tool::Extractor:
				return {Icon::Wall, IM_COL32(120, 200, 230, 255)};
			case Tool::OrderMove:
				return {Icon::Arrows, IM_COL32(242, 182, 61, 255)};
			case Tool::GymStart:
				return {Icon::Person, IM_COL32(120, 220, 120, 255)};
			case Tool::GymGoal:
				return {Icon::Flag, IM_COL32(242, 182, 61, 255)};
			case Tool::Fire:
				return {Icon::Flame, IM_COL32(255, 140, 40, 255)};
			case Tool::Napalm:
				return {Icon::Flame, IM_COL32(255, 90, 30, 255)};
			case Tool::NapalmRain:
				return {Icon::Flame, IM_COL32(255, 60, 30, 255)};
			case Tool::Water:
				return {Icon::Drop, IM_COL32(90, 170, 240, 255)};
			case Tool::Lava:
				return {Icon::Drop, IM_COL32(255, 110, 30, 255)};
			case Tool::Acid:
				return {Icon::Drop, IM_COL32(140, 230, 60, 255)};
			case Tool::Oil:
				return {Icon::Drop, IM_COL32(120, 90, 140, 255)};
			case Tool::Mud:
				return {Icon::Drop, IM_COL32(150, 105, 60, 255)};
			case Tool::Tar:
				return {Icon::Drop, IM_COL32(70, 60, 55, 255)};
			case Tool::Mercury:
				return {Icon::Drop, IM_COL32(200, 205, 215, 255)};
			case Tool::Fuel:
				return {Icon::Drop, IM_COL32(220, 190, 60, 255)};
			case Tool::Cryo:
				return {Icon::Drop, IM_COL32(180, 235, 255, 255)};
			case Tool::WaterSpawner:
				return {Icon::Down, IM_COL32(90, 170, 240, 255)};
			case Tool::Smoke:
				return {Icon::Cloud, IM_COL32(190, 190, 190, 255)};
			case Tool::ToxicGas:
				return {Icon::Cloud, IM_COL32(150, 220, 80, 255)};
			case Tool::LooseSand:
				return {Icon::Grains, IM_COL32(222, 190, 120, 255)};
			case Tool::LooseSnow:
				return {Icon::Grains, IM_COL32(240, 245, 255, 255)};
			case Tool::Gravel:
				return {Icon::Grains, IM_COL32(150, 145, 135, 255)};
			case Tool::GlassShards:
				return {Icon::Grains, IM_COL32(190, 225, 235, 255)};
			case Tool::Sand:
				return {Icon::Grains, IM_COL32(200, 170, 100, 255)};
			case Tool::Boulder:
				return {Icon::Chunk, IM_COL32(150, 140, 130, 255)};
			case Tool::Slab:
				return {Icon::Chunk, IM_COL32(180, 180, 175, 255)};
			case Tool::Earth:
				return {Icon::Chunk, IM_COL32(150, 100, 60, 255)};
			case Tool::Ice:
				return {Icon::Chunk, IM_COL32(170, 220, 250, 255)};
			case Tool::Grass:
				return {Icon::Chunk, IM_COL32(110, 180, 70, 255)};
			case Tool::Wood:
				return {Icon::Chunk, IM_COL32(170, 120, 70, 255)};
			case Tool::Concrete:
				return {Icon::Chunk, IM_COL32(170, 170, 165, 255)};
			case Tool::BoulderRain:
				return {Icon::Chunk, IM_COL32(150, 140, 130, 255)};
			case Tool::Dig:
				return {Icon::Pick, IM_COL32(232, 224, 190, 255)};
			case Tool::Grenade:
				return {Icon::Bomb, IM_COL32(120, 150, 90, 255)};
			case Tool::BigBomb:
				return {Icon::Bomb, IM_COL32(90, 90, 100, 255)};
			case Tool::Demolition:
				return {Icon::Bomb, IM_COL32(239, 106, 91, 255)};
			case Tool::BunkerBuster:
				return {Icon::Bomb, IM_COL32(242, 182, 61, 255)};
			case Tool::CarpetBomb:
				return {Icon::Bomb, IM_COL32(150, 150, 160, 255)};
			case Tool::Artillery:
				return {Icon::Bomb, IM_COL32(200, 160, 90, 255)};
			case Tool::Meteor:
				return {Icon::Rocket, IM_COL32(255, 140, 40, 255)};
			case Tool::RocketStrike:
				return {Icon::Rocket, IM_COL32(220, 220, 225, 255)};
			case Tool::RocketBarrage:
				return {Icon::Rocket, IM_COL32(239, 106, 91, 255)};
			case Tool::CrashRocket:
				return {Icon::Rocket, IM_COL32(242, 182, 61, 255)};
			case Tool::CrashDropship:
				return {Icon::Rocket, IM_COL32(120, 200, 230, 255)};
			case Tool::Lightning:
				return {Icon::Bolt, IM_COL32(255, 240, 120, 255)};
			case Tool::OrbitalBeam:
				return {Icon::Bolt, IM_COL32(120, 220, 255, 255)};
			case Tool::Effect:
				return {Icon::Star, IM_COL32(255, 220, 120, 255)};
			case Tool::BuildBeam:
				return {Icon::Wall, IM_COL32(170, 170, 165, 255)};
			case Tool::BuildPillar:
				return {Icon::Wall, IM_COL32(170, 170, 165, 255)};
			case Tool::BuildRoom:
				return {Icon::Wall, IM_COL32(170, 170, 165, 255)};
			case Tool::BuildTower:
				return {Icon::Wall, IM_COL32(200, 200, 195, 255)};
			case Tool::BuildIsland:
				return {Icon::Chunk, IM_COL32(150, 100, 60, 255)};
			case Tool::BuildTank:
				return {Icon::Drop, IM_COL32(90, 170, 240, 255)};
			case Tool::BuildBridge:
				return {Icon::Wall, IM_COL32(170, 120, 70, 255)};
			default:
				return {Icon::Star, IM_COL32(232, 224, 190, 255)};
		}
	}

	/// Draws one of the tool pictures, each of its pixels a square of the size given.
	void DrawIcon(ImDrawList* drawList, Icon icon, ImVec2 at, float pixel, ImU32 color) {
		const char* art = c_IconArt[static_cast<int>(icon)];
		ImU32 highlight = IM_COL32(255, 255, 255, (color >> IM_COL32_A_SHIFT) & 0xFF);
		for (int y = 0; y < 12; ++y) {
			for (int x = 0; x < 12; ++x) {
				char dot = art[y * 12 + x];
				if (dot != '.') {
					drawList->AddRectFilled(ImVec2(at.x + static_cast<float>(x) * pixel, at.y + static_cast<float>(y) * pixel), ImVec2(at.x + static_cast<float>(x + 1) * pixel, at.y + static_cast<float>(y + 1) * pixel), dot == '+' ? highlight : color);
				}
			}
		}
	}


	/// Notes that a tool was picked on the tab showing, for the bar to bring back with the tab.
	void TookTool(int toolIndex) {
		s_ToolIndex = toolIndex;
		if (!s_CurrentTab.empty()) {
			s_LastToolOfTab[s_CurrentTab] = toolIndex;
		}
	}

	/// Says whether a tab of the sandbox window is to be brought to the front this frame: because the bar asked for it, or a test run did (they can't click, so
	/// they name the one the window is to open on: CCCP_TEST_TAB=Spawn).
	ImGuiTabItemFlags TestTab(const char* name) {
		static const char* testWanted = std::getenv("CCCP_TEST_TAB");
		static int testFrames = 0;
		if (testWanted && std::string(testWanted) == name && testFrames < 3) {
			++testFrames;
			return ImGuiTabItemFlags_SetSelected;
		}
		if (!s_WantedTab.empty() && s_WantedTab == name) {
			s_WantedTab.clear();
			return ImGuiTabItemFlags_SetSelected;
		}
		return ImGuiTabItemFlags_None;
	}


	int FindPin(Tool kind, const std::string& presetName) {
		for (size_t i = 0; i < s_Pins.size(); ++i) {
			if (s_Pins[i].Kind == kind && s_Pins[i].PresetName == presetName) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void TogglePin(Tool kind, const std::string& presetName) {
		if (int at = FindPin(kind, presetName); at >= 0) {
			s_Pins.erase(s_Pins.begin() + at);
		} else if (s_Pins.size() < 24) {
			s_Pins.push_back({kind, presetName});
		}
	}


	int FindFavourite(Tool kind, const std::string& presetName) {
		for (size_t i = 0; i < s_Favourites.size(); ++i) {
			if (s_Favourites[i].Kind == kind && s_Favourites[i].PresetName == presetName) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}


	void SaveFavouritesFile() {
		if (std::ofstream file(c_FavouritesFile, std::ios::trunc); file) {
			file << Sandbox::GetFavourites() << '\n';
		}
	}

	void LoadFavouritesFile() {
		static bool loaded = false;
		if (loaded) {
			return;
		}
		loaded = true;
		if (std::ifstream file(c_FavouritesFile); file) {
			std::string line;
			std::getline(file, line);
			if (!line.empty()) {
				Sandbox::SetFavourites(line);
			}
		}
	}

	void ToggleFavourite(Tool kind, const std::string& presetName) {
		if (int at = FindFavourite(kind, presetName); at >= 0) {
			s_Favourites.erase(s_Favourites.begin() + at);
		} else {
			s_Favourites.push_back({kind, presetName});
		}
		SaveFavouritesFile();
	}

	/// A small gold star in the top left corner of a tile that is a favourite.
	void DrawFavouriteMark(ImDrawList* drawList, ImVec2 from) {
		float pixel = ToolUI::Pixel();
		ImVec2 centre(from.x + pixel * 6.0F, from.y + pixel * 6.0F);
		float outer = pixel * 5.0F;
		float inner = pixel * 2.0F;
		ImVec2 points[10];
		for (int i = 0; i < 10; ++i) {
			float angle = -1.5708F + 0.6283F * static_cast<float>(i);
			float reach = (i % 2 == 0) ? outer : inner;
			points[i] = ImVec2(centre.x + std::cos(angle) * reach, centre.y + std::sin(angle) * reach);
		}
		drawList->AddConcavePolyFilled(points, 10, IM_COL32(242, 182, 61, 255));
	}

	/// A small gold corner on a tile that is pinned to the bar.
	void DrawPinMark(ImDrawList* drawList, ImVec2 from, ImVec2 to) {
		float size = ToolUI::Pixel() * 5.0F;
		drawList->AddTriangleFilled(ImVec2(to.x - size, from.y), ImVec2(to.x, from.y), ImVec2(to.x, from.y + size), IM_COL32(242, 182, 61, 255));
	}

	/// The tools to pick from, as a row of tiles: each its picture with its name under it, the one in hand lit up.
	void ToolButtons(std::initializer_list<Tool> tools) {
		const ImGuiStyle& style = ImGui::GetStyle();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		float pixel = ToolUI::Pixel() * 2.0F;
		const int perRow = 4;
		float gap = style.ItemSpacing.x * 0.5F;
		float width = std::floor((ImGui::GetContentRegionAvail().x - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow));
		float pad = pixel * 2.0F;
		float height = pad + pixel * 12.0F + ImGui::GetTextLineHeight() * 2.0F + pad;
		int column = 0;
		for (Tool kind: tools) {
			int index = ToolIndex(kind);
			if (column++ % perRow != 0) {
				ImGui::SameLine(0.0F, gap);
			}
			ImGui::PushID(index);
			ImVec2 at = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##tool", ImVec2(width, height))) {
				TookTool(index);
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && Sandbox::IsGodMode()) {
				TogglePin(kind, "");
			}
			bool hovered = ImGui::IsItemHovered();
			bool selected = s_ToolIndex == index;
			ImVec2 to(at.x + width, at.y + height);
			drawList->AddRectFilled(at, to, ImGui::GetColorU32(selected ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
			drawList->AddRect(at, to, ImGui::GetColorU32(selected ? ImGuiCol_SliderGrab : ImGuiCol_Border), 0.0F, 0, selected ? ToolUI::Pixel() * 2.0F : ToolUI::Pixel());
			ToolLook look = LookOf(kind);
			DrawIcon(drawList, look.Art, ImVec2(std::floor(at.x + (width - pixel * 12.0F) * 0.5F), at.y + pad), pixel, look.Color);
			if (FindPin(kind, "") >= 0) {
				DrawPinMark(drawList, at, to);
			}
			const char* name = c_Tools[index].Name;
			float wrap = width - pad;
			ImVec2 nameSize = ImGui::CalcTextSize(name, nullptr, false, wrap);
			ImGui::PushClipRect(at, to, true);
			drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(std::floor(at.x + std::max((width - nameSize.x) * 0.5F, pad * 0.5F)), at.y + pad + pixel * 12.0F + ToolUI::Pixel()), ImGui::GetColorU32(selected ? ImGuiCol_SliderGrab : ImGuiCol_Text), name, nullptr, wrap);
			ImGui::PopClipRect();
			ImGui::PopID();
		}
	}

	void SideChooser() {
		for (int side = 0; side < c_Sides; ++side) {
			if (side > 0) {
				ImGui::SameLine();
			}
			ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
			ToolUI::RadioButton(c_SideNames[side], &s_Team, side);
			ImGui::PopStyleColor();
		}
	}

	void PresetList(Tool kind, const char* group, float rows) {
		const std::vector<Preset>& list = ListFor(kind);
		int& choice = ChoiceFor(kind);
		char* filter = FilterFor(kind, false);
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::InputTextWithHint("##filter", "Search...", filter, 64);
		if (ImGui::BeginListBox("##presets", ImVec2(-1.0F, ImGui::GetTextLineHeightWithSpacing() * rows))) {
			for (int i = 0; i < static_cast<int>(list.size()); ++i) {
				if (!ContainsIgnoringCase(list[i].Label, filter) || (group && list[i].Group != group)) {
					continue;
				}
				if (ImGui::Selectable(list[i].Label.c_str(), i == choice)) {
					choice = i;
				}
				if (i == choice && ImGui::IsWindowAppearing()) {
					ImGui::SetScrollHereY();
				}
			}
			ImGui::EndListBox();
		}
	}

	void LoadoutChooser(const char* label) {
		const char* current = s_Loadout == 0 ? "Faction default" : (s_Loadout == 1 ? "Unarmed" : (s_Loadout - 2 < static_cast<int>(s_Weapons.size()) ? s_Weapons[s_Loadout - 2]->Label.c_str() : "?"));
		if (ImGui::BeginCombo(label, current)) {
			if (ImGui::Selectable("Faction default", s_Loadout == 0)) {
				s_Loadout = 0;
			}
			if (ImGui::Selectable("Unarmed", s_Loadout == 1)) {
				s_Loadout = 1;
			}
			for (int i = 0; i < static_cast<int>(s_Weapons.size()); ++i) {
				if (ImGui::Selectable(s_Weapons[i]->Label.c_str(), s_Loadout == i + 2)) {
					s_Loadout = i + 2;
				}
			}
			ImGui::EndCombo();
		}
	}



	/// Frees every picture's texture, for when the sandbox is left: they're made again as they're next needed.
	void ForgetPictures() {
		for (std::map<std::string, PiecePicture>* pictures: {&s_PresetPictures, &s_FilePictures}) {
			for (const auto& [key, picture]: *pictures) {
				if (picture.Texture != 0) {
					GLuint texture = picture.Texture;
					glDeleteTextures(1, &texture);
				}
			}
			pictures->clear();
		}
	}

	/// The colour the game shows for a palette index, as 8-bit RGB: the same conversion the renderer uploads the palette with (Allegro palettes are 6 bits a channel).
	void PaletteColor(int index, unsigned char* rgb) {
		rgb[0] = static_cast<unsigned char>(getr8(index));
		rgb[1] = static_cast<unsigned char>(getg8(index));
		rgb[2] = static_cast<unsigned char>(getb8(index));
	}

	/// Gets the picture of a bunker piece, making it the first time it is asked for. A piece with no art of its own gets an empty picture.
	const PiecePicture& PictureOf(const Preset& preset) {
		std::map<std::string, PiecePicture>& pictures = s_PresetPictures;
		std::string key = preset.ClassName + "/" + preset.Module + "/" + preset.PresetName;
		if (auto found = pictures.find(key); found != pictures.end()) {
			return found->second;
		}
		PiecePicture& picture = pictures[key];
		const Entity* entity = g_PresetMan.GetEntityPreset(preset.ClassName, preset.PresetName, preset.ModuleID);
		std::vector<BITMAP*> layers;
		std::unique_ptr<BITMAP, void (*)(BITMAP*)> portrait(nullptr, destroy_bitmap);
		int cropX = 0, cropY = 0, cropWidth = 0, cropHeight = 0; //!< If set, the part of the one layer that is the picture.
		if (const TerrainObject* terrainObject = dynamic_cast<const TerrainObject*>(entity)) {
			layers = {terrainObject->GetBGColorBitmap(), terrainObject->GetFGColorBitmap()};
			picture.OffsetX = terrainObject->GetBitmapOffset().m_X;
			picture.OffsetY = terrainObject->GetBitmapOffset().m_Y;
		} else if (const Actor* actorPreset = dynamic_cast<const Actor*>(entity); actorPreset && !dynamic_cast<const ADoor*>(entity)) {
			// A unit is many parts: a copy of it is drawn whole, the way the game's build menu shows the thing in hand, and the picture cut to fit.
			// The copy is never updated: an update during drawing would play its sounds, spawn particles and register lights at the picture's spot in the scene and use the sim's random numbers. Its parts are just put in place, as when a unit is added to the world.
			const int room = 160;
			portrait.reset(create_bitmap_ex(8, room, room));
			clear_to_color(portrait.get(), ColorKeys::g_MaskColor);
			if (Actor* copy = dynamic_cast<Actor*>(actorPreset->Clone())) {
				copy->SetPos(Vector(static_cast<float>(room / 2), static_cast<float>(room / 2)));
				copy->SetTeam(0);
				copy->CorrectAttachableAndWoundPositionsAndRotations();
				copy->Draw(portrait.get(), Vector(), g_DrawColor, true);
				delete copy;
			}
			int left = room, top = room, right = -1, bottom = -1;
			for (int y = 0; y < room; ++y) {
				for (int x = 0; x < room; ++x) {
					if (portrait->line[y][x] != ColorKeys::g_MaskColor) {
						left = std::min(left, x);
						right = std::max(right, x);
						top = std::min(top, y);
						bottom = std::max(bottom, y);
					}
				}
			}
			if (right >= left) {
				cropX = left;
				cropY = top;
				cropWidth = right - left + 1;
				cropHeight = bottom - top + 1;
				layers = {portrait.get()};
			}
		} else if (const MOSprite* sprite = dynamic_cast<const MOSprite*>(entity)) {
			layers = {sprite->GetGraphicalIcon()};
			picture.OffsetX = sprite->GetSpriteOffset().m_X;
			picture.OffsetY = sprite->GetSpriteOffset().m_Y;
		}
		for (const BITMAP* layer: layers) {
			if (layer && bitmap_color_depth(const_cast<BITMAP*>(layer)) == 8) {
				picture.Width = std::max(picture.Width, cropWidth > 0 ? cropWidth : layer->w);
				picture.Height = std::max(picture.Height, cropHeight > 0 ? cropHeight : layer->h);
			}
		}
		if (picture.Width <= 0 || picture.Height <= 0 || picture.Width > 4096 || picture.Height > 4096) {
			picture.Width = picture.Height = 0;
			return picture;
		}
		std::vector<unsigned char> pixels(static_cast<size_t>(picture.Width) * picture.Height * 4, 0);
		for (const BITMAP* layer: layers) {
			if (!layer || bitmap_color_depth(const_cast<BITMAP*>(layer)) != 8) {
				continue;
			}
			for (int y = 0; y < std::min(layer->h - cropY, picture.Height); ++y) {
				for (int x = 0; x < std::min(layer->w - cropX, picture.Width); ++x) {
					int index = layer->line[y + cropY][x + cropX];
					if (index == ColorKeys::g_MaskColor) {
						continue;
					}
					unsigned char* pixel = &pixels[(static_cast<size_t>(y) * picture.Width + x) * 4];
					PaletteColor(index, pixel);
					pixel[3] = 255;
				}
			}
		}
		GLint boundBefore = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundBefore);
		glGenTextures(1, &picture.Texture);
		glBindTexture(GL_TEXTURE_2D, picture.Texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, picture.Width, picture.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundBefore));
		return picture;
	}


	/// A combo of the distinct values of one field over the list, with "All" first. @return Whether the choice changed.
	bool ChoiceCombo(const char* label, std::string& chosen, const std::vector<std::string>& values) {
		std::string items = "All";
		items.push_back(0);
		int current = 0;
		for (size_t i = 0; i < values.size(); ++i) {
			items += values[i];
			items.push_back(0);
			if (values[i] == chosen) {
				current = static_cast<int>(i) + 1;
			}
		}
		items.push_back(0);
		if (ImGui::Combo(label, &current, items.c_str())) {
			chosen = current == 0 ? "" : values[current - 1];
			return true;
		}
		return false;
	}

	void PictureGrid(Tool kind, const char* group) {
		LoadFavouritesFile();
		const std::vector<Preset>& list = ListFor(kind);
		int& choice = ChoiceFor(kind);
		char* filter = FilterFor(kind, true);
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::InputTextWithHint("##filter", "Search...", filter, 64);
		// Narrowing the list: by subcategory (not for structures, whose own Kind combo does that), by mod, and whether mods are listed at all.
		{
			std::vector<std::string> kinds;
			std::vector<std::string> mods;
			for (const Preset& preset: list) {
				if (group && preset.Group != group) {
					continue;
				}
				if (!preset.Kind.empty() && std::find(kinds.begin(), kinds.end(), preset.Kind) == kinds.end()) {
					kinds.push_back(preset.Kind);
				}
				if ((s_ShowModded || !preset.Modded) && std::find(mods.begin(), mods.end(), preset.Module) == mods.end()) {
					mods.push_back(preset.Module);
				}
			}
			std::sort(kinds.begin(), kinds.end());
			std::sort(mods.begin(), mods.end());
			float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0F) / 3.0F;
			ToolUI::Checkbox("Favourites", &s_FavouritesOnly);
			ImGui::SetItemTooltip("Only the things marked as favourites (Ctrl+click on a tile marks one, and again unmarks it). Favourites are yours, kept whatever game is played.");
			if (s_FavouritesOnly && !s_Favourites.empty()) {
				ImGui::SameLine();
				if (ToolUI::SmallButton("Clear")) {
					s_Favourites.clear();
					SaveFavouritesFile();
				}
				ImGui::SetItemTooltip("Unmarks every favourite.");
			}
			ImGui::SameLine();
			ToolUI::Checkbox("Show modded", &s_ShowModded);
			ImGui::SetItemTooltip("Whether things from mods are listed, as well as the game's own.");
			if (kind != Tool::Structure) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(third);
				ChoiceCombo("##kind", s_KindFilter[kind], kinds);
				ImGui::SetItemTooltip("The kind of thing listed.");
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(third);
			ChoiceCombo("##mod", s_ModFilter[kind], mods);
			ImGui::SetItemTooltip("Only things from this module (faction or mod).");
		}
		const ImGuiStyle& style = ImGui::GetStyle();
		float cell = ImGui::GetFontSize() * 6.0F;
		float labelHeight = ImGui::GetTextLineHeight() * 2.0F;
		ImGui::BeginChild("##pictures", ImVec2(-1.0F, std::max(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 6.5F, cell * 2.5F)), ImGuiChildFlags_Borders);
		int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / (cell + style.ItemSpacing.x)));
		int shown = 0;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (int i = 0; i < static_cast<int>(list.size()); ++i) {
			const Preset& preset = list[i];
			if (!ContainsIgnoringCase(preset.Label, filter) || (group && preset.Group != group)) {
				continue;
			}
			if ((!s_ShowModded && preset.Modded) || (!s_KindFilter[kind].empty() && preset.Kind != s_KindFilter[kind]) || (!s_ModFilter[kind].empty() && preset.Module != s_ModFilter[kind])) {
				continue;
			}
			if (s_FavouritesOnly && FindFavourite(kind, preset.PresetName) < 0) {
				continue;
			}
			if (shown++ % columns != 0) {
				ImGui::SameLine();
			}
			ImGui::PushID(i);
			ImVec2 at = ImGui::GetCursorScreenPos();
			ImVec2 size(cell, cell + labelHeight);
			bool picked = ImGui::InvisibleButton("##piece", size);
			bool hovered = ImGui::IsItemHovered();
			// Only the ones on screen have their pictures made.
			if (ImGui::IsItemVisible()) {
				bool selected = i == choice;
				// In the colours of the game's own menus: olive cells, the picked one brighter with a gold edge.
				drawList->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), selected ? IM_COL32(85, 96, 68, 255) : hovered ? IM_COL32(57, 75, 42, 255) : IM_COL32(24, 29, 21, 255));
				drawList->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), selected ? IM_COL32(242, 182, 61, 255) : IM_COL32(60, 70, 48, 255), 0.0F, 0, selected ? 2.0F : 1.0F);
				const PiecePicture& picture = PictureOf(preset);
				if (picture.Width > 0) {
					// As big as fits, by whole pixels when it can be so the art stays crisp.
					float room = cell - 8.0F;
					float fit = std::min(room / static_cast<float>(picture.Width), room / static_cast<float>(picture.Height));
					if (fit >= 1.0F) {
						fit = std::floor(fit);
					}
					fit = std::min(fit, 3.0F);
					ImVec2 pictureSize(static_cast<float>(picture.Width) * fit, static_cast<float>(picture.Height) * fit);
					ImVec2 corner(std::floor(at.x + (cell - pictureSize.x) * 0.5F), std::floor(at.y + (cell - pictureSize.y) * 0.5F));
					drawList->AddImage(static_cast<ImTextureID>(picture.Texture), corner, ImVec2(corner.x + pictureSize.x, corner.y + pictureSize.y));
				}
				ImGui::PushClipRect(ImVec2(at.x + 2.0F, at.y + cell), ImVec2(at.x + size.x - 2.0F, at.y + size.y), true);
				ImVec2 nameSize = ImGui::CalcTextSize(preset.PresetName.c_str(), nullptr, false, cell - 4.0F);
				drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + std::max((cell - nameSize.x) * 0.5F, 2.0F), at.y + cell), IM_COL32(230, 232, 238, 255), preset.PresetName.c_str(), nullptr, cell - 4.0F);
				ImGui::PopClipRect();
			}
			if (hovered) {
				std::string size = preset.Width > 0 ? "\n" + std::to_string(preset.Width) + " x " + std::to_string(preset.Height) + " pixels" : "";
				ImGui::SetTooltip("%s\n%s%s%s\nCtrl+click: a favourite, or not", preset.PresetName.c_str(), preset.Module.c_str(), size.c_str(), Sandbox::IsGodMode() ? "\nRight click: keep it on the bar, or take it off" : "");
			}
			if (picked && ImGui::GetIO().KeyCtrl) {
				ToggleFavourite(kind, preset.PresetName);
			} else if (picked) {
				choice = i;
				TookTool(ToolIndex(kind));
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && Sandbox::IsGodMode()) {
				TogglePin(kind, preset.PresetName);
			}
			if (ImGui::IsItemVisible() && FindPin(kind, preset.PresetName) >= 0) {
				DrawPinMark(drawList, at, ImVec2(at.x + size.x, at.y + size.y));
			}
			if (ImGui::IsItemVisible() && FindFavourite(kind, preset.PresetName) >= 0) {
				DrawFavouriteMark(drawList, at);
			}
			ImGui::PopID();
		}
		if (shown == 0) {
			ImGui::TextDisabled("Nothing of that kind matches.");
		}
		ImGui::EndChild();
	}


	void DrawCursor() {
		ImGuiIO& io = ImGui::GetIO();
		const ToolInfo& tool = CurrentTool();
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		ImU32 white = IM_COL32(255, 255, 255, 170);
		std::string label = tool.Name;
		if (tool.Kind == Tool::Barracks || tool.Kind == Tool::Extractor) {
			// The plot it will take, on the ground under the pointer.
			const Colony::Type& type = Colony::GetType(tool.Kind == Tool::Barracks ? Colony::Kind::Barracks : Colony::Kind::Extractor);
			Vector ground = MouseScenePosition();
			int sceneHeight = g_SceneMan.GetSceneHeight();
			for (int tries = 0; tries < 600 && g_SceneMan.GetTerrMatter(ground.GetFloorIntX(), ground.GetFloorIntY()) != g_MaterialAir; ++tries) {
				ground.m_Y -= 1.0F;
			}
			while (ground.m_Y < static_cast<float>(sceneHeight - 2) && g_SceneMan.GetTerrMatter(ground.GetFloorIntX(), ground.GetFloorIntY() + 1) == g_MaterialAir) {
				ground.m_Y += 1.0F;
			}
			Vector corner = g_SceneMan.ShortestDistance(g_CameraMan.GetOffset(0), ground + Vector(-static_cast<float>(type.Width / 2), 1.0F - static_cast<float>(type.Height)), g_SceneMan.SceneWrapsX());
			ImVec2 topLeft(ViewOrigin().x + corner.m_X / scale, ViewOrigin().y + corner.m_Y / scale);
			drawList->AddRect(topLeft, ImVec2(topLeft.x + static_cast<float>(type.Width) / scale, topLeft.y + static_cast<float>(type.Height) / scale), c_SideColors[s_Team], 0.0F, 0, 1.5F);
		} else if (tool.Kind == Tool::Structure) {
			if (const Preset* preset = ChosenPreset(Tool::Structure, s_StructureChoice)) {
				// The piece itself, see-through, exactly where a click will put it, with its outline.
				const PiecePicture& picture = PictureOf(*preset);
				if (picture.Width > 0) {
					Vector corner = g_SceneMan.ShortestDistance(g_CameraMan.GetOffset(0), StructurePosition(*preset, MouseScenePosition(), s_SnapToGrid) + Vector(picture.OffsetX, picture.OffsetY), g_SceneMan.SceneWrapsX());
					ImVec2 topLeft(ViewOrigin().x + corner.m_X / scale, ViewOrigin().y + corner.m_Y / scale);
					ImVec2 bottomRight(topLeft.x + static_cast<float>(picture.Width) / scale, topLeft.y + static_cast<float>(picture.Height) / scale);
					GameViewRect view = g_WindowMan.GetGameViewRect();
					drawList->PushClipRect(ImVec2(view.x, view.y), ImVec2(view.x + view.w, view.y + view.h));
					drawList->AddImage(static_cast<ImTextureID>(picture.Texture), topLeft, bottomRight, ImVec2(0.0F, 0.0F), ImVec2(1.0F, 1.0F), IM_COL32(255, 255, 255, 190));
					drawList->AddRect(topLeft, bottomRight, IM_COL32(255, 255, 255, 110), 0.0F, 0, 1.0F);
					drawList->PopClipRect();
				}
			}
		} else {
			float outline = tool.UsesRadius ? static_cast<float>(s_Radius) / scale : 6.0F;
			drawList->AddCircle(io.MousePos, std::max(outline, 3.0F), tool.Kind == Tool::Unit || tool.Kind == Tool::Brain || tool.Kind == Tool::RallyPoint ? c_SideColors[s_Team] : white, 0, 1.5F);
		}
		if (const Preset* preset = (tool.Kind == Tool::Unit || tool.Kind == Tool::Brain || tool.Kind == Tool::Item || tool.Kind == Tool::Structure) ? ChosenPreset(tool.Kind, ChoiceFor(tool.Kind)) : nullptr) {
			label = preset->PresetName;
			if (tool.Kind == Tool::Unit && s_SquadSize > 1) {
				label += " x" + std::to_string(s_SquadSize);
			}
		}
		float pixel = ToolUI::Pixel();
		auto flag = [&](const Vector& spot, ImU32 color) {
			ImVec2 at = ToScreen(spot);
			drawList->AddTriangleFilled(ImVec2(at.x, at.y - pixel * 2.0F), ImVec2(at.x - pixel * 3.0F, at.y - pixel * 7.0F), ImVec2(at.x + pixel * 3.0F, at.y - pixel * 7.0F), color);
			drawList->AddRectFilled(ImVec2(at.x - pixel * 4.0F, at.y - pixel), ImVec2(at.x + pixel * 4.0F, at.y + pixel), color);
		};
		auto crosshair = [&](const Vector& where, ImU32 color, float reach) {
			ImVec2 at = ToScreen(where);
			drawList->AddCircle(at, reach, color, 0, pixel);
			drawList->AddLine(ImVec2(at.x - reach * 1.4F, at.y), ImVec2(at.x - reach * 0.5F, at.y), color, pixel);
			drawList->AddLine(ImVec2(at.x + reach * 0.5F, at.y), ImVec2(at.x + reach * 1.4F, at.y), color, pixel);
			drawList->AddLine(ImVec2(at.x, at.y - reach * 1.4F), ImVec2(at.x, at.y - reach * 0.5F), color, pixel);
			drawList->AddLine(ImVec2(at.x, at.y + reach * 0.5F), ImVec2(at.x, at.y + reach * 1.4F), color, pixel);
		};
		// With the reachability preview on: each spot the order will look at, ringed green where a unit goes, red where the first unit has no path,
		// grey where it wasn't needed, with the path cost.
		auto reachMarks = [&](const std::vector<Actor*>& units, const Vector& point) {
			if (!g_SettingsMan.ShowSandboxSpotReach() || units.empty()) {
				return;
			}
			for (const SpotReach& entry: SpotReachPreview(units, point)) {
				ImVec2 at = ToScreen(entry.Spot - Vector(0.0F, 4.0F));
				ImU32 color = entry.Cost == -1.0F ? IM_COL32(239, 90, 80, 255) : entry.Chosen ? IM_COL32(120, 230, 110, 255) : IM_COL32(150, 150, 140, 200);
				drawList->AddCircle(at, pixel * 7.0F, color, 0, entry.Chosen ? pixel * 1.5F : pixel);
				std::string cost = entry.Cost == -2.0F ? "not tried" : entry.Cost < 0.0F ? "no path" : std::to_string(static_cast<int>(entry.Cost + 0.5F));
				drawList->AddText(ImVec2(at.x + pixel * 9.0F, at.y - ImGui::GetTextLineHeight() * 0.5F), color, cost.c_str());
			}
		};
		if (tool.Kind == Tool::OrderMove) {
			// Where each unit will stand: a marker on the ground for every one, so the order can be seen before it is given.
			std::vector<Actor*> units = UnitsToMove(s_Team, false);
			reachMarks(units, MouseScenePosition());
			for (const Vector& spot: StandingSpots(MouseScenePosition(), static_cast<int>(units.size()))) {
				flag(spot, c_SideColors[s_Team]);
			}
			label = units.empty() ? std::string(c_SideNames[s_Team]) + " has no units to move" : std::to_string(units.size()) + (units.size() == 1 ? " unit will come here" : " units will come here");
		} else if (tool.Kind == Tool::Command) {
			// What the click will do, in the mode's own colour and marks.
			Vector point = MouseScenePosition();
			Actor* under = dynamic_cast<Actor*>(ObjectUnder(point, true));
			bool underIsUnit = under && IsCombatant(under) && !under->IsInGroup("Brains");
			bool underIsFriend = underIsUnit && (s_Selected.empty() || under->GetTeam() == SelectionTeam());
			std::vector<Actor*> units = UnitsToMove(0, true);
			std::string count = std::to_string(units.size()) + (units.size() == 1 ? " unit" : " units");
			if (units.empty() || (underIsFriend && s_CommandMode == CommandMode::Move)) {
				if (underIsUnit) {
					drawList->AddCircle(ToScreen(under->GetPos()), std::max(under->GetRadius() / scale, 8.0F) + pixel * 2.0F, IM_COL32(255, 255, 255, 200), 0, pixel);
					label = "Select " + under->GetPresetName() + "  (Shift: add, double click: all of this kind)";
				} else {
					label = units.empty() ? "Drag a box round units to select them" : "Move " + count + " here";
				}
			} else if (s_CommandMode == CommandMode::Attack || (s_CommandMode == CommandMode::Move && underIsUnit && !underIsFriend)) {
				ImU32 red = IM_COL32(239, 106, 91, 255);
				Actor* target = (underIsUnit && !underIsFriend) ? under : nullptr;
				float nearest = 400.0F * 400.0F;
				for (Actor* actor: SandboxAccess::Actors()) {
					if (target || !IsCombatant(actor) || actor->IsIgnoredByAI() || actor->GetTeam() == SelectionTeam()) {
						continue;
					}
					float distance = g_SceneMan.ShortestDistance(point, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude();
					if (distance < nearest) {
						nearest = distance;
						target = actor;
					}
				}
				if (target && !(underIsUnit && !underIsFriend)) {
					// Found near the point rather than under the pointer.
					for (Actor* actor: SandboxAccess::Actors()) {
						if (IsCombatant(actor) && !actor->IsIgnoredByAI() && actor->GetTeam() != SelectionTeam() && g_SceneMan.ShortestDistance(point, actor->GetPos(), g_SceneMan.SceneWrapsX()).GetSqrMagnitude() <= nearest) {
							target = actor;
						}
					}
				}
				if (target) {
					crosshair(target->GetPos(), red, std::max(target->GetRadius() / scale, 8.0F) + pixel * 3.0F);
					drawList->AddLine(io.MousePos, ToScreen(target->GetPos()), (red & 0x00FFFFFF) | (120u << IM_COL32_A_SHIFT), pixel);
					label = "Attack " + target->GetPresetName() + " with " + count;
				} else {
					crosshair(point, red, pixel * 6.0F);
					label = count + " attack towards here (no enemy near)";
				}
			} else if (s_CommandMode == CommandMode::Guard) {
				ImU32 green = IM_COL32(120, 220, 120, 255);
				if (underIsFriend) {
					ImVec2 at = ToScreen(under->GetPos());
					float reach = std::max(under->GetRadius() / scale, 8.0F) + pixel * 3.0F;
					drawList->AddCircle(at, reach, green, 0, pixel * 1.5F);
					drawList->AddCircle(at, reach + pixel * 3.0F, (green & 0x00FFFFFF) | (90u << IM_COL32_A_SHIFT), 0, pixel);
					label = count + " guard " + under->GetPresetName();
				} else {
					label = "Guard: point at a friendly unit for " + count + " to stay with";
				}
			} else {
				for (const Vector& spot: StandingSpots(point, static_cast<int>(units.size()))) {
					flag(spot, IM_COL32(110, 180, 250, 255));
				}
				reachMarks(units, point);
				label = "Move " + count + " here";
			}
		}
		if (TakesSide(tool.Kind) && tool.Kind != Tool::Structure) {
			// Whose it will be, in that side's colour, and how to change it.
			std::string whose = std::string(c_SideNames[s_Team]) + "  (right button: change side)";
			ImVec2 at(io.MousePos.x + 14.0F, io.MousePos.y + 10.0F);
			drawList->AddText(ImVec2(at.x + 1.0F, at.y + 1.0F), IM_COL32(0, 0, 0, 200), whose.c_str());
			drawList->AddText(at, c_SideColors[s_Team], whose.c_str());
		}
		// A bunker piece is shown as itself, so its name would only be in the way; under the ring of sides nothing is.
		if (tool.Kind != Tool::Structure && !s_RingOpen) {
			drawList->AddText(ImVec2(io.MousePos.x + 14.0F, io.MousePos.y - 8.0F), IM_COL32(255, 255, 255, 220), label.c_str());
		}
	}


	/// A picture made from one of the game's own 8-bit image files, the first time it is asked for: the pie menu's icons and cursor.
	const PiecePicture& PictureOfFile(const std::string& path) {
		std::map<std::string, PiecePicture>& pictures = s_FilePictures;
		if (auto found = pictures.find(path); found != pictures.end()) {
			return found->second;
		}
		PiecePicture& picture = pictures[path];
		BITMAP* bitmap = ContentFile(path.c_str()).GetAsBitmap();
		if (!bitmap || bitmap_color_depth(bitmap) != 8) {
			return picture;
		}
		picture.Width = bitmap->w;
		picture.Height = bitmap->h;
		std::vector<unsigned char> pixels(static_cast<size_t>(picture.Width) * picture.Height * 4, 0);
		for (int y = 0; y < picture.Height; ++y) {
			for (int x = 0; x < picture.Width; ++x) {
				int index = bitmap->line[y][x];
				if (index == ColorKeys::g_MaskColor) {
					continue;
				}
				unsigned char* pixel = &pixels[(static_cast<size_t>(y) * picture.Width + x) * 4];
				PaletteColor(index, pixel);
				pixel[3] = 255;
			}
		}
		GLint boundBefore = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundBefore);
		glGenTextures(1, &picture.Texture);
		glBindTexture(GL_TEXTURE_2D, picture.Texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, picture.Width, picture.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundBefore));
		return picture;
	}

	/// A ring of choices round where the right button went down, held open while it is held, in the look of the game's own pie menu: a dark
	/// band with the choices' icons round it, separated by lines, the cursor on the inner edge pointing at the one under the pointer, and
	/// that one's name outside the band. Letting go takes it.
	/// @param items The choices, from the top going clockwise. @param current The one in force now, named when the pointer is in the middle.
	/// @param sticky The ring stays up after the right button is let go, and a left click takes the choice under the pointer (a right click, none).
	/// @return The choice let go over, -1 for none (let go in the middle), or -2 while the ring is still up.
	int DrawRing(const std::vector<RingItem>& items, int current, bool sticky) {
		ImGuiIO& io = ImGui::GetIO();
		float pixel = ToolUI::Pixel();
		// The game's pie menu: an inner radius of 58 and a band 16 thick, in game pixels. A little thicker here, for icons at twice the size.
		float inner = pixel * 50.0F;
		float thickness = pixel * 26.0F;
		float outer = inner + thickness;
		int count = static_cast<int>(items.size());
		ImVec2 away(io.MousePos.x - s_RingCenter.x, io.MousePos.y - s_RingCenter.y);
		float distance = std::sqrt(away.x * away.x + away.y * away.y);
		// Each choice has an equal slice; the first is centred straight up.
		const float slice = 6.2832F / static_cast<float>(count);
		int under = -1;
		if (distance > inner * 0.5F) {
			float angle = std::atan2(away.y, away.x) + 1.5708F + slice * 0.5F; // 0 at the top edge of the first slice, growing clockwise.
			while (angle < 0.0F) {
				angle += 6.2832F;
			}
			under = static_cast<int>(angle / slice) % count;
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		const ImU32 band = IM_COL32(0, 0, 0, 128); // The pie menu's background: black at half.
		const ImU32 separator = IM_COL32(0, 0, 0, 220);
		// The band.
		drawList->AddCircle(s_RingCenter, (inner + outer) * 0.5F, band, 64, thickness);
		// The slice under the pointer, lit a little (an arc as thick as the band: a filled sector isn't convex, and bled into the middle).
		if (under >= 0) {
			float from = -1.5708F - slice * 0.5F + slice * static_cast<float>(under);
			drawList->PathClear();
			drawList->PathArcTo(s_RingCenter, (inner + outer) * 0.5F, from, from + slice, 16);
			drawList->PathStroke(IM_COL32(255, 255, 255, 40), 0, thickness);
		}
		// The separators, and the edges.
		for (int i = 0; i < count; ++i) {
			float edge = -1.5708F - slice * 0.5F + slice * static_cast<float>(i);
			ImVec2 a(s_RingCenter.x + std::cos(edge) * inner, s_RingCenter.y + std::sin(edge) * inner);
			ImVec2 b(s_RingCenter.x + std::cos(edge) * outer, s_RingCenter.y + std::sin(edge) * outer);
			drawList->AddLine(a, b, separator, pixel * 2.0F);
		}
		drawList->AddCircle(s_RingCenter, inner, separator, 64, pixel);
		drawList->AddCircle(s_RingCenter, outer, separator, 64, pixel);
		// The icons, each in the middle of its slice, at twice their size; a choice without one shows its colour.
		for (int i = 0; i < count; ++i) {
			float middle = -1.5708F + slice * static_cast<float>(i);
			ImVec2 at(s_RingCenter.x + std::cos(middle) * (inner + thickness * 0.5F), s_RingCenter.y + std::sin(middle) * (inner + thickness * 0.5F));
			const PiecePicture* picture = items[i].Icon ? &PictureOfFile(std::string("Base.rte/GUIs/PieMenus/PieIcons/") + items[i].Icon + "000.png") : nullptr;
			if (picture && picture->Texture) {
				float w = static_cast<float>(picture->Width) * pixel * 2.0F;
				float h = static_cast<float>(picture->Height) * pixel * 2.0F;
				ImVec2 corner(std::floor(at.x - w * 0.5F), std::floor(at.y - h * 0.5F));
				ImU32 tint = (i == under || (under < 0 && i == current)) ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 200, 255);
				drawList->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(picture->Texture)), corner, ImVec2(corner.x + w, corner.y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
			} else {
				drawList->AddCircleFilled(at, pixel * 6.0F, items[i].Color);
				drawList->AddCircle(at, pixel * 6.0F, separator, 0, pixel);
			}
		}
		// The cursor on the inner edge, pointing at the choice under the pointer (or the one in force), as the pie menu's does.
		int shown = under >= 0 ? under : current;
		if (shown >= 0 && shown < count) {
			float middle = -1.5708F + slice * static_cast<float>(shown);
			const PiecePicture& cursor = PictureOfFile("Base.rte/GUIs/PieMenus/PieCursor.png");
			if (cursor.Texture) {
				// The cursor art points right; it is turned to the slice.
				float w = static_cast<float>(cursor.Width) * pixel * 2.0F;
				float h = static_cast<float>(cursor.Height) * pixel * 2.0F;
				ImVec2 at(s_RingCenter.x + std::cos(middle) * (inner - w * 0.5F), s_RingCenter.y + std::sin(middle) * (inner - w * 0.5F));
				float c = std::cos(middle);
				float s = std::sin(middle);
				auto turned = [&](float x, float y) { return ImVec2(at.x + x * c - y * s, at.y + x * s + y * c); };
				drawList->AddImageQuad(static_cast<ImTextureID>(static_cast<intptr_t>(cursor.Texture)), turned(-w * 0.5F, -h * 0.5F), turned(w * 0.5F, -h * 0.5F), turned(w * 0.5F, h * 0.5F), turned(-w * 0.5F, h * 0.5F));
			}
			// Its name, outside the band on that side.
			const char* label = items[shown].Label;
			ImVec2 nameSize = ImGui::CalcTextSize(label);
			float textReach = outer + pixel * 4.0F;
			ImVec2 anchor(s_RingCenter.x + std::cos(middle) * textReach, s_RingCenter.y + std::sin(middle) * textReach);
			float x = std::cos(middle) > 0.3F ? anchor.x : (std::cos(middle) < -0.3F ? anchor.x - nameSize.x : anchor.x - nameSize.x * 0.5F);
			float y = std::sin(middle) > 0.3F ? anchor.y : (std::sin(middle) < -0.3F ? anchor.y - nameSize.y : anchor.y - nameSize.y * 0.5F);
			ImVec2 pos(std::floor(x), std::floor(y));
			drawList->AddText(ImVec2(pos.x + pixel, pos.y + pixel), IM_COL32(0, 0, 0, 220), label);
			drawList->AddText(pos, IM_COL32(255, 255, 255, 255), label);
		}
		static const bool testHeld = std::getenv("CCCP_TEST_RING") != nullptr;
		if (sticky) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s_RingOpen = false;
				return under;
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				s_RingOpen = false;
				return -1;
			}
			return -2;
		}
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || testHeld) {
			return -2;
		}
		s_RingOpen = false;
		return under;
	}

	/// The rings the right button opens, by the tool in hand: the sides for anything made for a side, the commands for the command tool.
	void DrawSideRing() {
		ImGuiIO& io = ImGui::GetIO();
		Tool kind = CurrentTool().Kind;
		bool hasRing = TakesSide(kind) || kind == Tool::Command;
		if (!s_RingOpen) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse && hasRing) {
				s_RingOpen = true;
				s_RingPage = 0;
				s_RingCenter = io.MousePos;
				s_RingScenePoint = MouseScenePosition();
			}
			return;
		}
		if (kind == Tool::Command && s_RingPage == 1) {
			// The game's own AI modes for the units picked, as the pie menu offers them when playing a unit. Up until a click, since the button
			// that held the first ring open has been let go.
			static const std::vector<RingItem> modes = {{"Sentry", IM_COL32(242, 182, 61, 255), "Eye"}, {"Patrol", IM_COL32(120, 200, 220, 255), "Cycle"}, {"Hunt brains", IM_COL32(239, 106, 91, 255), "Brain"}, {"Dig for gold", IM_COL32(230, 200, 80, 255), "Dig"}, {"Rally point", IM_COL32(180, 140, 240, 255), "Flag"}, {"Do nothing", IM_COL32(150, 150, 140, 255), "Blank"}, {"Back", IM_COL32(110, 180, 250, 255), "Return"}};
			static const Order orders[] = {Order::Hold, Order::Patrol, Order::HuntBrains, Order::DigGold, Order::Rally, Order::Idle};
			int picked = DrawRing(modes, -1, true);
			if (picked == -2) {
				return;
			}
			if (picked >= 0 && picked < 6) {
				Stroke stroke;
				stroke.Kind = Tool::OrderSelected;
				stroke.Position = s_RingScenePoint;
				stroke.Orders = orders[picked];
				s_Queue.push_back(stroke);
			} else if (picked == 6) {
				s_RingOpen = true;
				s_RingPage = 2;
			}
			return;
		}
		if (kind == Tool::Command && (s_RingPage == 3 || s_RingPage == 4)) {
			// The engagement rules (RC-1) for the units picked: what they may shoot at (3), and how they move when they meet an enemy (4).
			// The one they all have is lit; up until a click.
			bool weapons = s_RingPage == 3;
			static const std::vector<RingItem> weaponRules = {{c_WeaponRuleNames[0], IM_COL32(239, 106, 91, 255), "Death"}, {c_WeaponRuleNames[1], IM_COL32(242, 182, 61, 255), "Reorient"}, {c_WeaponRuleNames[2], IM_COL32(150, 150, 140, 255), "Cancel"}, {"Back", IM_COL32(110, 180, 250, 255), "Return"}};
			static const std::vector<RingItem> movementRules = {{c_MovementRuleNames[0], IM_COL32(200, 200, 200, 255), "Cycle"}, {c_MovementRuleNames[1], IM_COL32(239, 106, 91, 255), "Move"}, {c_MovementRuleNames[2], IM_COL32(110, 180, 250, 255), "GoTo"}, {c_MovementRuleNames[3], IM_COL32(242, 182, 61, 255), "Flag"}, {"Back", IM_COL32(110, 180, 250, 255), "Return"}};
			const std::vector<RingItem>& rules = weapons ? weaponRules : movementRules;
			int picked = DrawRing(rules, SelectedRule(weapons), true);
			if (picked == -2) {
				return;
			}
			int count = static_cast<int>(rules.size());
			if (picked >= 0 && picked < count - 1) {
				QueueRule(weapons, picked);
			} else if (picked == count - 1) {
				s_RingOpen = true;
				s_RingPage = 2;
			}
			return;
		}
		if (kind == Tool::Command) {
			// (The two rules show what the units picked have, or "mixed".)
			auto ruleLabel = [](bool weapons) {
				static std::string labels[2];
				int rule = SelectedRule(weapons);
				std::string& label = labels[weapons ? 0 : 1];
				label = std::string(weapons ? "Weapons: " : "Movement: ") + (rule == -1 ? "mixed" : (rule < 0 ? "..." : (weapons ? c_WeaponRuleNames[rule] : c_MovementRuleNames[rule])));
				return label.c_str();
			};
			std::vector<RingItem> commands = {{"Move", IM_COL32(110, 180, 250, 255), "GoTo"}, {"Attack", IM_COL32(239, 106, 91, 255), "Death"}, {"Guard", IM_COL32(120, 220, 120, 255), "Follow"}, {"Defend", IM_COL32(242, 182, 61, 255), "Eye"}, {"Cancel", IM_COL32(200, 160, 120, 255), "Cancel"}, {"Deselect", IM_COL32(150, 150, 140, 255), "Remove"}, {ruleLabel(true), IM_COL32(242, 182, 61, 255), "Reload"}, {ruleLabel(false), IM_COL32(120, 220, 120, 255), "Move"}, {"More...", IM_COL32(200, 200, 200, 255), "SubPieMenu1"}};
			int picked = DrawRing(commands, static_cast<int>(s_CommandMode), s_RingPage == 2);
			if (picked == -2) {
				return;
			}
			if (picked >= 0 && picked <= 2) {
				// The mode for the clicks to come.
				s_CommandMode = static_cast<CommandMode>(picked);
			} else if (picked == 3 || picked == 4) {
				// Defend where they stand (3), or cancel their orders (4).
				Stroke stroke;
				stroke.Kind = Tool::OrderSelected;
				stroke.Position = s_RingScenePoint;
				stroke.Count = 100 + (picked == 3 ? 3 : 2);
				s_Queue.push_back(stroke);
			} else if (picked == 5) {
				s_Selected.clear();
			} else if (picked == 6 || picked == 7) {
				s_RingOpen = true;
				s_RingPage = picked == 6 ? 3 : 4;
			} else if (picked == 8) {
				s_RingOpen = true;
				s_RingPage = 1;
			}
			return;
		}
		std::vector<RingItem> sides;
		static const char* teamIcons[] = {"Team1", "Team2", "Team3", "Team4"};
		for (int side = 0; side < c_Sides; ++side) {
			sides.push_back({c_SideNames[side], c_SideColors[side], side < 4 ? teamIcons[side] : nullptr});
		}
		int picked = DrawRing(sides, s_Team);
		if (picked >= 0) {
			s_Team = picked;
		}
	}

	ImVec2 ToScreen(const Vector& scenePosition) { return DebugDraw::ToScreen(scenePosition); }


	/// A label over each colony building: whose it is, what it is doing and how far along.
	void DrawColony() {
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		GameViewRect view = g_WindowMan.GetGameViewRect();
		float scale = ScenePixelsPerWindowPixel();
		// Kept to the picture of the game, so a label never lies over a tool panel.
		drawList->PushClipRect(ImVec2(view.x, view.y), ImVec2(view.x + view.w, view.y + view.h));
		for (const Colony::Building& building: Colony::Buildings()) {
			const Colony::Type& type = Colony::GetType(building.What);
			ImVec2 top = ToScreen(building.Ground + Vector(0.0F, -static_cast<float>(type.Height) - 6.0F));
			if (top.x < view.x - 100.0F || top.x > view.x + view.w + 100.0F || top.y < view.y || top.y > view.y + view.h + 40.0F) {
				continue;
			}
			std::string label = std::string(type.Name) + (building.What == Colony::Kind::Barracks ? "  " + std::to_string(building.Alive.size()) + "/" + std::to_string(building.KeepAlive) : "");
			ImVec2 size = ImGui::CalcTextSize(label.c_str());
			ImVec2 at(top.x - size.x * 0.5F, top.y - size.y - 6.0F);
			drawList->AddRectFilled(ImVec2(at.x - 4.0F, at.y - 2.0F), ImVec2(at.x + size.x + 4.0F, at.y + size.y + 2.0F), IM_COL32(0, 0, 0, 140), 3.0F);
			drawList->AddText(at, c_SideColors[building.Team], label.c_str());
			if (building.What == Colony::Kind::Barracks && building.Paid) {
				float barWidth = std::max(static_cast<float>(type.Width) / scale * 0.6F, 30.0F);
				ImVec2 barAt(top.x - barWidth * 0.5F, top.y - 3.0F);
				drawList->AddRectFilled(barAt, ImVec2(barAt.x + barWidth, barAt.y + 4.0F), IM_COL32(0, 0, 0, 160));
				drawList->AddRectFilled(barAt, ImVec2(barAt.x + barWidth * std::clamp(building.Progress, 0.0F, 1.0F), barAt.y + 4.0F), c_SideColors[building.Team]);
			}
		}
		drawList->PopClipRect();
	}

	/// The Colony tab of the sandbox window.
	void ColonyTab() {
		ImGui::TextWrapped("Buildings that work for a side. A barracks trains a unit, sends it out with its orders, and trains another whenever fewer than its number are alive. An extractor earns supply. They are built of concrete: wreck one and it stops.");
		ToolUI::Checkbox("Training is free", &Colony::Free());
		ImGui::SetItemTooltip("Off: a barracks pays for each unit from the supply of its side, which grows slowly by itself and faster with extractors.");
		if (!Colony::Free()) {
			for (int side = 0; side < c_Sides; ++side) {
				ImGui::PushID(side);
				ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4F);
				ImGui::DragFloat(c_SideNames[side], &Colony::Supply(side), 10.0F, 0.0F, 999999.0F, "%.0f supply");
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
		}
		ImGui::SeparatorText("Build");
		ToolButtons({Tool::Barracks, Tool::Extractor});
		Tool kind = CurrentTool().Kind;
		if (kind == Tool::Barracks || kind == Tool::Extractor) {
			SideChooser();
		}
		if (kind == Tool::Barracks) {
			ImGui::TextDisabled("It trains:");
			PresetList(Tool::Unit);
			UnitOrderCombo("Their orders");
			ImGui::SliderInt("Keeps this many alive", &s_ColonyKeep, 1, 20);
		}
		ImGui::SeparatorText("Standing");
		std::vector<Colony::Building>& buildings = Colony::Buildings();
		if (buildings.empty()) {
			ImGui::TextDisabled("Nothing built yet.");
		}
		int removeID = -1;
		for (Colony::Building& building: buildings) {
			ImGui::PushID(building.ID);
			const Colony::Type& type = Colony::GetType(building.What);
			ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[building.Team]);
			bool open = ImGui::TreeNode("##building", "%s %d (%s)", type.Name, building.ID, c_SideNames[building.Team]);
			ImGui::PopStyleColor();
			ImGui::SameLine();
			ImGui::TextDisabled("%s", building.Status.c_str());
			if (open) {
				if (building.What == Colony::Kind::Barracks) {
					if (building.Paid) {
						ImGui::ProgressBar(building.Progress, ImVec2(-1.0F, 0.0F));
					}
					if (ImGui::BeginCombo("Trains", building.Unit.c_str(), ImGuiComboFlags_HeightLarge)) {
						for (const Preset& unit: s_Units) {
							if (ImGui::Selectable(unit.Label.c_str(), unit.PresetName == building.Unit)) {
								building.Unit = unit.PresetName;
							}
						}
						ImGui::EndCombo();
					}
					building.Orders = static_cast<int>(UnitOrder(building.Orders));
					ImGui::Combo("Their orders", &building.Orders, OrderName, nullptr, c_UnitOrderCount);
					ImGui::SliderInt("Keeps this many alive", &building.KeepAlive, 1, 20);
					ImGui::Text("%d alive, %d trained in all. One takes %.0f s%s.", static_cast<int>(building.Alive.size()), building.Produced, Colony::TrainingSeconds(std::max(Sandbox::UnitCost(building.Unit), 20.0F)),
					            Colony::Free() ? "" : (" and " + std::to_string(static_cast<int>(std::max(Sandbox::UnitCost(building.Unit), 20.0F))) + " supply").c_str());
				} else {
					ImGui::TextDisabled("%s", type.Description);
				}
				ToolUI::Checkbox("Stopped", &building.Paused);
				ImGui::SameLine();
				if (ToolUI::SmallButton("Look at it")) {
					s_FreeCamera = true;
					s_FollowTarget = UnitRef();
					s_FollowAction = false;
					s_CameraCenter = building.Ground + Vector(0.0F, -40.0F);
				}
				ImGui::SameLine();
				if (ToolUI::SmallButton("Close it down")) {
					removeID = building.ID;
				}
				ImGui::SetItemTooltip("It stops being a building. What it was built of stays standing.");
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (removeID >= 0) {
			Colony::Remove(removeID);
		}
	}

	/// Takes the selection arrow off the units that carry it.
	void UnmarkSelection() {
		for (const UnitRef& ref: s_MarkedSelected) {
			if (Actor* unit = GetRef(ref)) {
				unit->SetSandboxSelected(false);
			}
		}
		s_MarkedSelected.clear();
	}

	void DrawSelection() {
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		UnmarkSelection();
		for (const UnitRef& ref: s_Selected) {
			if (Actor* unit = GetRef(ref)) {
				unit->SetSandboxSelected(true);
				s_MarkedSelected.push_back(ref);
			}
		}
		// The engagement rules a selected unit has that aren't the usual (RC-1), in a small tag over it: HF hold fire, RF return fire, and
		// EN engage, MO move only, HG hold ground.
		for (const UnitRef& ref: s_Selected) {
			const Actor* unit = GetRef(ref);
			if (!unit || (unit->GetWeaponRule() == Actor::WEAPONS_AT_WILL && unit->GetMovementRule() == Actor::MOVE_FOLLOW_ORDER)) {
				continue;
			}
			static const char* weaponTags[] = {"", "RF", "HF"};
			static const char* movementTags[] = {"", "EN", "MO", "HG"};
			std::string tag = weaponTags[std::clamp(unit->GetWeaponRule(), 0, 2)];
			const char* movementTag = movementTags[std::clamp(unit->GetMovementRule(), 0, 3)];
			if (*movementTag) {
				tag += tag.empty() ? movementTag : std::string(" ") + movementTag;
			}
			ImVec2 size = ImGui::CalcTextSize(tag.c_str());
			ImVec2 at = ToScreen(unit->GetPos() - Vector(0.0F, unit->GetRadius() + 4.0F));
			ImVec2 corner(std::floor(at.x - size.x * 0.5F), std::floor(at.y - size.y));
			drawList->AddRectFilled(ImVec2(corner.x - 2.0F, corner.y - 1.0F), ImVec2(corner.x + size.x + 2.0F, corner.y + size.y + 1.0F), IM_COL32(0, 0, 0, 150), 2.0F);
			drawList->AddText(corner, unit->GetWeaponRule() == Actor::WEAPONS_HOLD ? IM_COL32(170, 170, 160, 255) : IM_COL32(242, 182, 61, 255), tag.c_str());
		}
		// (No line from each unit to where it is going: the game draws the route itself, as Routes on the command row has it.)
		// The marks of orders just given, fading.
		float seconds = ImGui::GetIO().DeltaTime;
		for (OrderMark& mark: s_OrderMarks) {
			mark.Life -= seconds;
			float size = 6.0F + (1.0F - mark.Life) * 10.0F;
			ImU32 color = (mark.Color & 0x00FFFFFF) | (static_cast<ImU32>(std::clamp(mark.Life, 0.0F, 1.0F) * 220.0F) << IM_COL32_A_SHIFT);
			drawList->AddCircle(ToScreen(mark.Position), size, color, 0, 2.0F);
		}
		s_OrderMarks.erase(std::remove_if(s_OrderMarks.begin(), s_OrderMarks.end(), [](const OrderMark& mark) { return mark.Life <= 0.0F; }), s_OrderMarks.end());
		if (s_CurrentTab == "Gym") {
			DrawGym(drawList);
		}
		if (const Actor* followed = GetRef(s_FollowTarget)) {
			ImVec2 at = ToScreen(followed->GetPos() - Vector(0.0F, followed->GetRadius() + 8.0F));
			drawList->AddTriangleFilled(ImVec2(at.x - 6.0F, at.y - 8.0F), ImVec2(at.x + 6.0F, at.y - 8.0F), ImVec2(at.x, at.y), IM_COL32(255, 255, 255, 230));
		}
	}

	/// Flags marking each side's rally point.
	void DrawRallyPoints() {
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		for (int side = 0; side < c_Sides; ++side) {
			if (!s_RallySet[side]) {
				continue;
			}
			Vector onScreen = g_SceneMan.ShortestDistance(g_CameraMan.GetOffset(0), s_RallyPoints[side], g_SceneMan.SceneWrapsX());
			ImVec2 base(ViewOrigin().x + onScreen.m_X / scale, ViewOrigin().y + onScreen.m_Y / scale);
			drawList->AddLine(base, ImVec2(base.x, base.y - 26.0F), IM_COL32(230, 230, 230, 220), 2.0F);
			drawList->AddTriangleFilled(ImVec2(base.x, base.y - 26.0F), ImVec2(base.x + 16.0F, base.y - 21.0F), ImVec2(base.x, base.y - 16.0F), c_SideColors[side]);
		}
	}

	void SideStatus() {
		// The fighting units each side has, as the auto battle counts them (Sandbox::CountUnits): not brains or craft, but a craft's passengers.
		// ("Red 7" was one brain, one dropship and five soldiers.)
		std::array<int, c_Sides> counts{};
		for (int side = 0; side < c_Sides; ++side) {
			counts[side] = Sandbox::CountUnits(side);
		}
		for (int side = 0; side < c_Sides; ++side) {
			if (side > 0) {
				ImGui::SameLine();
			}
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c_SideColors[side]), "%s %d", c_SideNames[side], counts[side]);
		}
		// The order labels overlay adds how each auto battle side stands.
		if (g_SettingsMan.ShowOrderLabels() && s_AutoRunning) {
			long long now = g_TimerMan.GetSimUpdateCount();
			for (int side = 0; side < c_Sides; ++side) {
				const AutoSide& autoSide = s_AutoSides[side];
				if (!autoSide.Active) {
					continue;
				}
				std::string wave = autoSide.Broke ? std::string("broke") : "next wave " + std::to_string(std::max(0LL, autoSide.NextWave - now) / 60) + "s";
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c_SideColors[side]), "%s: budget %d, spent %.0f, sent %d, %s", c_SideNames[side], autoSide.Budget, autoSide.Spent, autoSide.Sent, wave.c_str());
			}
		}
	}
	/// How fast time runs, the AI's pause, and in the Sandbox game mode whether the world stands still while the window is open.
	void TimeControls() {
		bool aiPaused = Controller::IsAIPaused();
		float timeScale = g_TimerMan.GetTimeScale();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45F);
		if (ImGui::SliderFloat("Speed of time", &timeScale, 0.05F, 3.0F, "%.2fx")) {
			g_TimerMan.SetTimeScale(timeScale);
		}
		for (const auto& [label, scale]: {std::pair<const char*, float>{"Slow", 0.25F}, {"Normal", 1.0F}, {"Fast", 2.0F}}) {
			ImGui::SameLine();
			if (ToolUI::SmallButton(label)) {
				g_TimerMan.SetTimeScale(scale);
			}
		}
		ImGui::PushStyleColor(ImGuiCol_Text, aiPaused ? IM_COL32(255, 210, 80, 255) : ImGui::GetColorU32(ImGuiCol_Text));
		if (ToolUI::Checkbox("Pause AI (set things up, then let them loose)", &aiPaused)) {
			Controller::SetAIPaused(aiPaused);
		}
		ImGui::PopStyleColor();
		if (Sandbox::IsGodMode()) {
			ToolUI::Checkbox("The world stands still while this window is open", &s_PauseInMenus);
			ImGui::SetItemTooltip("On: time stops while this window is open and starts when it is put away (Tab) or you go and play (P). What you do with a tool still happens at once.\nOff: the world carries on while you work.");
			if (s_PausedByMenus) {
				ImGui::SameLine();
				if (ToolUI::SmallButton("Step")) {
					s_StepsWanted += 1;
				}
				ImGui::SetItemTooltip("Lets the world move one update, a sixtieth of a second. Hold Ctrl and click for a second's worth.");
				if (ImGui::IsItemDeactivated() && ImGui::GetIO().KeyCtrl) {
					s_StepsWanted += 59;
				}
			}
		}
	}


	/// A thin upright gold rule between groups of tiles on the bar.
	void BarDivider() {
		ImGui::SameLine(0.0F, ToolUI::Pixel() * 4.0F);
		ImVec2 at = ImGui::GetCursorScreenPos();
		float height = ToolUI::Pixel() * 24.0F;
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + ToolUI::Pixel()), ImVec2(at.x + ToolUI::Pixel(), at.y + height - ToolUI::Pixel()), IM_COL32(170, 128, 48, 200));
		ImGui::Dummy(ImVec2(ToolUI::Pixel(), height));
		ImGui::SameLine(0.0F, ToolUI::Pixel() * 4.0F);
	}

	/// The bar's plate: an olive slab with cut corners and a gold line round it, drawn behind the window's contents.
	void BarPlate() {
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImVec2 at = ImGui::GetWindowPos();
		ImVec2 size = ImGui::GetWindowSize();
		ImVec2 to(at.x + size.x, at.y + size.y);
		float pixel = ToolUI::Pixel();
		float cut = pixel * 5.0F;
		auto shape = [&](float grow) {
			drawList->PathClear();
			drawList->PathLineTo(ImVec2(at.x - grow + cut, at.y - grow));
			drawList->PathLineTo(ImVec2(to.x + grow - cut, at.y - grow));
			drawList->PathLineTo(ImVec2(to.x + grow, at.y - grow + cut));
			drawList->PathLineTo(ImVec2(to.x + grow, to.y + grow - cut));
			drawList->PathLineTo(ImVec2(to.x + grow - cut, to.y + grow));
			drawList->PathLineTo(ImVec2(at.x - grow + cut, to.y + grow));
			drawList->PathLineTo(ImVec2(at.x - grow, to.y + grow - cut));
			drawList->PathLineTo(ImVec2(at.x - grow, at.y - grow + cut));
		};
		shape(pixel * 2.0F);
		drawList->PathFillConvex(IM_COL32(14, 17, 12, 230));
		shape(0.0F);
		drawList->PathFillConvex(IM_COL32(44, 53, 37, 245));
		shape(pixel);
		drawList->PathStroke(IM_COL32(170, 128, 48, 255), ImDrawFlags_Closed, pixel);
		// A faint lighter band along the top, as a lip.
		drawList->AddRectFilled(ImVec2(at.x + cut, at.y + pixel), ImVec2(to.x - cut, at.y + pixel * 2.0F), IM_COL32(255, 240, 180, 30));
		// Studs in the corners.
		for (ImVec2 corner: {ImVec2(at.x + cut, at.y + cut), ImVec2(to.x - cut, at.y + cut), ImVec2(at.x + cut, to.y - cut), ImVec2(to.x - cut, to.y - cut)}) {
			drawList->AddRectFilled(ImVec2(corner.x - pixel, corner.y - pixel), ImVec2(corner.x + pixel, corner.y + pixel), IM_COL32(242, 182, 61, 160));
		}
	}

	/// The settings of the tool in hand, in a row at the top of the bar: brush size for painting, squad size and orders for units, and so on.
	/// Returns whether anything was shown.
	bool ContextRow() {
		const ToolInfo& tool = CurrentTool();
		float pixel = ToolUI::Pixel();
		float field = ImGui::GetFontSize() * 9.0F;
		bool shown = false;
		auto start = [&](const char* what) {
			ImGui::TextDisabled("%s", what);
			ImGui::SameLine(0.0F, pixel * 4.0F);
			shown = true;
		};
		if (tool.UsesRadius) {
			start(tool.Name);
			ImGui::SetNextItemWidth(field);
			ImGui::SliderInt("##brush", &s_Radius, 1, 40, "Brush %d px");
			ImGui::SameLine();
			for (const auto& [label, size]: {std::pair<const char*, int>{"S", 4}, {"M", 10}, {"L", 24}}) {
				if (ToolUI::SmallButton(label)) {
					s_Radius = size;
				}
				ImGui::SameLine();
			}
			ImGui::NewLine();
		} else if (tool.Kind == Tool::Unit || tool.Kind == Tool::Drop) {
			const Preset* preset = ChosenPreset(tool.Kind, ChoiceFor(tool.Kind));
			start(tool.Kind == Tool::Drop && s_DropRandom ? (s_DropFavourites ? "Random favourites" : "Random units") : preset ? preset->PresetName.c_str() : tool.Name);
			ImGui::SetNextItemWidth(field * 0.7F);
			ImGui::SliderInt("##squad", &s_SquadSize, 1, 10, "Squad of %d");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(field);
			UnitOrderCombo("##orders");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(field);
			LoadoutChooser("##loadout");
			if (tool.Kind == Tool::Drop) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(field * 0.7F);
				ImGui::Combo("##craft", &s_Craft, "Dropship\0Rocket\0");
			}
		} else if (tool.Kind == Tool::Structure) {
			const Preset* preset = ChosenPreset(Tool::Structure, s_StructureChoice);
			start(preset ? preset->PresetName.c_str() : tool.Name);
			ToolUI::Checkbox("Snap to the bunker grid", &s_SnapToGrid);
		} else if (tool.Kind == Tool::Item) {
			const Preset* preset = ChosenPreset(Tool::Item, s_ItemChoice);
			start(preset ? preset->PresetName.c_str() : tool.Name);
			ToolUI::Checkbox("Pull the pin (grenades)", &s_LitGrenade);
		} else if (tool.Kind == Tool::Barracks) {
			start(tool.Name);
			ImGui::SetNextItemWidth(field);
			ImGui::SliderInt("##keep", &s_ColonyKeep, 1, 20, "Keeps %d alive");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(field);
			UnitOrderCombo("##orders");
		} else if (tool.Kind == Tool::Effect) {
			start(tool.Name);
			ImGui::SetNextItemWidth(field * 1.4F);
			if (ImGui::BeginCombo("##effect", c_Effects[std::clamp(s_EffectChoice, 0, static_cast<int>(EffectKind::Count) - 1)].Name)) {
				for (int i = 0; i < static_cast<int>(EffectKind::Count); ++i) {
					if (ImGui::Selectable(c_Effects[i].Name, i == s_EffectChoice)) {
						s_EffectChoice = i;
					}
				}
				ImGui::EndCombo();
			}
		} else if (tool.Kind == Tool::OrderMove || tool.Kind == Tool::RallyPoint || tool.Kind == Tool::Brain) {
			start(tool.Name);
			for (int side = 0; side < c_Sides; ++side) {
				if (side > 0) {
					ImGui::SameLine();
				}
				ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
				ToolUI::RadioButton(c_SideNames[side], &s_Team, side);
				ImGui::PopStyleColor();
			}
		} else if (tool.Kind == Tool::Command) {
			start(tool.Name);
			// The mode of the clicks, in its colours.
			for (int mode = 0; mode < 3; ++mode) {
				if (mode > 0) {
					ImGui::SameLine();
				}
				static const ImU32 modeColors[] = {IM_COL32(110, 180, 250, 255), IM_COL32(239, 106, 91, 255), IM_COL32(120, 220, 120, 255)};
				ImGui::PushStyleColor(ImGuiCol_Text, modeColors[mode]);
				int current = static_cast<int>(s_CommandMode);
				if (ToolUI::RadioButton(c_CommandModeNames[mode], &current, mode)) {
					s_CommandMode = static_cast<CommandMode>(current);
				}
				ImGui::PopStyleColor();
			}
			ImGui::SameLine(0.0F, pixel * 6.0F);
			// What is selected, by kind.
			std::map<std::string, int> kinds;
			int alive = 0;
			for (const UnitRef& ref: s_Selected) {
				if (const Actor* unit = GetRef(ref)) {
					++kinds[unit->GetPresetName()];
					++alive;
				}
			}
			std::string what = alive == 0 ? "Nothing selected" : std::to_string(alive) + " selected:";
			for (const auto& [name, number]: kinds) {
				what += " " + std::to_string(number) + " " + name + ",";
			}
			if (!kinds.empty()) {
				what.pop_back();
			}
			ImGui::TextDisabled("%s", what.c_str());
			ImGui::SameLine();
			ImGui::BeginDisabled(alive == 0);
			if (ToolUI::SmallButton("Deselect")) {
				s_Selected.clear();
			}
			ImGui::EndDisabled();
			// The engagement rules of what is selected (RC-1): the one they share, or "mixed"; a choice gives it to them all.
			ImGui::BeginDisabled(alive == 0);
			for (bool weapons: {true, false}) {
				ImGui::SameLine(0.0F, pixel * 6.0F);
				int rule = SelectedRule(weapons);
				const char* const* names = weapons ? c_WeaponRuleNames : c_MovementRuleNames;
				int ruleCount = weapons ? static_cast<int>(std::size(c_WeaponRuleNames)) : static_cast<int>(std::size(c_MovementRuleNames));
				ImGui::SetNextItemWidth(field * 0.75F);
				if (ImGui::BeginCombo(weapons ? "##weaponRule" : "##movementRule", rule == -1 ? "Mixed" : (rule < 0 ? (weapons ? "Weapons" : "Movement") : names[rule]))) {
					for (int choice = 0; choice < ruleCount; ++choice) {
						if (ImGui::Selectable(names[choice], choice == rule)) {
							QueueRule(weapons, choice);
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("%s", weapons ? "What the selected units may shoot at.\nFire at will: any enemy they see. Return fire: only while they are being shot at. Hold fire: never; they aim, and open up the moment this changes.\nKept until changed." : "How the selected units move when they meet an enemy.\nAs ordered: a move keeps walking, an attack closes in, a post is held. Engage: stop and fight, closing in. Move only: keep going, firing on the way. Hold ground: fight from where they stand.\nEach new order goes back to As ordered.");
			}
			ImGui::EndDisabled();
			ImGui::SameLine(0.0F, pixel * 6.0F);
			// Whose routes are drawn: the game's own AI path drawing, as the settings have it.
			ImGui::TextDisabled("Routes");
			ImGui::SameLine();
			{
				int paths = Actor::ShowAIPaths();
				static const char* routeNames[] = {"Never", "Always", "Selected"};
				for (int mode = 0; mode < 3; ++mode) {
					if (mode > 0) {
						ImGui::SameLine();
					}
					int shown = (mode + 1) % 3; // Shown in the order Always, Selected, Never.
					if (ToolUI::RadioButton(routeNames[shown], &paths, shown)) {
						Actor::SetShowAIPaths(paths);
					}
				}
			}
			ImGui::BeginDisabled(alive == 0);
			ImGui::SameLine();
			if (ToolUI::SmallButton("Follow")) {
				s_FollowTarget = s_Selected.empty() ? UnitRef() : s_Selected.front();
				s_FollowAction = false;
			}
			ImGui::EndDisabled();
			ImGui::SameLine(0.0F, pixel * 6.0F);
			ImGui::SetNextItemWidth(field * 0.8F);
			ImGui::SliderFloat("##spacing", &s_Spacing, 8.0F, 60.0F, "Spacing %.0f px");
			ImGui::SetItemTooltip("How far apart units stand when sent somewhere together.\nDrag a box to select; Shift+click adds a unit, or on the ground queues another place to go on to; double click takes all of a kind in sight; Ctrl+A everyone on the side.\nCtrl+number keeps the selection, the number brings it back. Hold the right button over the world for the ring.");
		}
		return shown;
	}

	/// The sandbox's bar along the bottom of the picture, in the Sandbox game mode while you're above it all: the main tools, the parts of the sandbox window to
	/// open, and the things you've pinned. It is there whether the window is open or not.
	void DrawBar() {
		// In the middle of the picture, and it stays there: opening a panel doesn't shove it along.
		GameViewRect view = g_WindowMan.GetGameViewRect();
		const ImGuiStyle& style = ImGui::GetStyle();
		float pixel = ToolUI::Pixel();
		struct Part {
			const char* Name;
			Icon Art;
			ImU32 Color;
			const char* Tip;
		};
		static const Part parts[] = {
		    {"Spawn", Icon::Person, IM_COL32(232, 224, 190, 255), "Spawn: units, squads dropped from orbit, brains and items"},
		    {"Build", Icon::Wall, IM_COL32(170, 170, 165, 255), "Build: bunker pieces, placed straight into the world"},
		    {"Paint", Icon::Drop, IM_COL32(90, 170, 240, 255), "Paint: fire, liquids, smoke, loose and solid ground"},
		    {"Boom", Icon::Bomb, IM_COL32(239, 106, 91, 255), "Boom: blasts, strikes from the sky, and things to knock down"},
		    {"Effects", Icon::Star, IM_COL32(255, 220, 120, 255), "Effects: lights and particle effects to put down"},
		    {"Orders", Icon::Flag, IM_COL32(242, 182, 61, 255), "Orders: orders for whole sides, and auto battles"},
		    {"World", Icon::Cloud, IM_COL32(190, 190, 190, 255), "World: time, weather, the speed of the world, the camera"},
		    {"You", Icon::Person, IM_COL32(130, 220, 120, 255), "You: your own character, what it is, carries and can do"},
		};
		static const Tool mainTools[] = {Tool::None, Tool::Command, Tool::Follow, Tool::Possess, Tool::Remove, Tool::RallyPoint};

		ImGui::SetNextWindowPos(ImVec2(view.x + view.w * 0.5F, view.y + view.h - pixel * 6.0F), ImGuiCond_Always, ImVec2(0.5F, 1.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(8.0F, 8.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pixel * 8.0F, pixel * 5.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pixel * 2.0F, pixel * 3.0F));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
		if (ImGui::Begin("##SandboxBar", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
			BarPlate();
			// The settings of the tool in hand, in a row of their own at the top.
			if (ContextRow()) {
				ImVec2 at = ImGui::GetCursorScreenPos();
				float width = ImGui::GetContentRegionAvail().x;
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + pixel), ImVec2(at.x + width, at.y + pixel * 2.0F), IM_COL32(170, 128, 48, 160));
				ImGui::Dummy(ImVec2(width, pixel * 3.0F));
			}
			// What you've pinned, in a row of its own above the rest.
			int unpin = -1;
			if (!s_Pins.empty()) {
				ImGui::TextDisabled("Pinned");
				ImGui::SameLine(0.0F, pixel * 4.0F);
			}
			for (size_t i = 0; i < s_Pins.size(); ++i) {
				const Pin& pin = s_Pins[i];
				int toolIndex = ToolIndex(pin.Kind);
				const std::vector<Preset>& list = ListFor(pin.Kind);
				int presetIndex = -1;
				if (!pin.PresetName.empty()) {
					for (size_t j = 0; j < list.size(); ++j) {
						if (list[j].PresetName == pin.PresetName) {
							presetIndex = static_cast<int>(j);
							break;
						}
					}
				}
				if (i > 0) {
					ImGui::SameLine();
				}
				ImGui::PushID(static_cast<int>(i) + 1000);
				bool inHand = s_ToolIndex == toolIndex && (pin.PresetName.empty() || ChoiceFor(pin.Kind) == presetIndex);
				std::string tip = (pin.PresetName.empty() ? std::string(c_Tools[toolIndex].Name) : pin.PresetName + "  (" + c_Tools[toolIndex].Name + ")") + "\nRight click: take it off the bar";
				int clicked = BarTile("##pin", tip.c_str(), inHand, [&](ImDrawList* drawList, ImVec2 at, float room) {
					const PiecePicture* picture = presetIndex >= 0 ? &PictureOf(list[presetIndex]) : nullptr;
					if (picture && picture->Width > 0) {
						float fit = std::min(room / static_cast<float>(picture->Width), room / static_cast<float>(picture->Height));
						if (fit >= 1.0F) {
							fit = std::floor(fit);
						}
						ImVec2 size(static_cast<float>(picture->Width) * fit, static_cast<float>(picture->Height) * fit);
						ImVec2 corner(std::floor(at.x + (room - size.x) * 0.5F), std::floor(at.y + (room - size.y) * 0.5F));
						drawList->AddImage(static_cast<ImTextureID>(picture->Texture), corner, ImVec2(corner.x + size.x, corner.y + size.y));
					} else {
						ToolLook look = LookOf(pin.Kind);
						DrawIcon(drawList, look.Art, at, room / 12.0F, look.Color);
					}
				});
				if (clicked == 1) {
					s_ToolIndex = toolIndex;
					if (presetIndex >= 0) {
						ChoiceFor(pin.Kind) = presetIndex;
					}
				} else if (clicked == 2) {
					unpin = static_cast<int>(i);
				}
				ImGui::PopID();
			}
			if (unpin >= 0) {
				s_Pins.erase(s_Pins.begin() + unpin);
			}
			if (!s_Pins.empty()) {
				// A gold rule between the pins and the rest.
				ImVec2 at = ImGui::GetCursorScreenPos();
				float width = ImGui::GetContentRegionAvail().x;
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + pixel), ImVec2(at.x + width, at.y + pixel * 2.0F), IM_COL32(170, 128, 48, 160));
				ImGui::Dummy(ImVec2(width, pixel * 3.0F));
			}

			// Into your character.
			if (s_Player.EnterOnClose) {
				if (BarTile("##play", "Play: step into your own character (P). Shift+P puts it down where the mouse points first.", false, [&](ImDrawList* drawList, ImVec2 at, float room) { DrawIcon(drawList, Icon::Person, at, room / 12.0F, IM_COL32(130, 220, 120, 255)); }) == 1) {
					Sandbox::TogglePlay(false);
				}
				BarDivider();
			}
			// The main tools.
			for (size_t i = 0; i < std::size(mainTools); ++i) {
				int index = ToolIndex(mainTools[i]);
				ToolLook look = LookOf(mainTools[i]);
				ImGui::PushID(index);
				if (i > 0) {
					ImGui::SameLine();
				}
				if (BarTile("##main", c_Tools[index].Name, s_ToolIndex == index, [&](ImDrawList* drawList, ImVec2 at, float room) { DrawIcon(drawList, look.Art, at, room / 12.0F, look.Color); }) == 1) {
					s_ToolIndex = index;
				}
				ImGui::PopID();
			}
			BarDivider();
			// The side things are made for, in its colour: a click goes round the sides.
			{
				std::string tip = std::string("Side: ") + c_SideNames[s_Team] + ". Click to go to the next side; or hold the right button over the world with a unit in hand for the ring of sides.";
				if (BarTile("##side", tip.c_str(), false, [&](ImDrawList* drawList, ImVec2 at, float room) {
					    float inset = room * 0.2F;
					    drawList->AddRectFilled(ImVec2(at.x + inset, at.y + inset), ImVec2(at.x + room - inset, at.y + room - inset), c_SideColors[s_Team]);
					    drawList->AddRect(ImVec2(at.x + inset, at.y + inset), ImVec2(at.x + room - inset, at.y + room - inset), IM_COL32(20, 24, 16, 255), 0.0F, 0, pixel);
				    }) == 1) {
					s_Team = (s_Team + 1) % c_Sides;
				}
			}
			BarDivider();
			// The parts of the sandbox window: a click opens the window on that part, and a click on the one showing puts the window away.
			for (size_t i = 0; i < std::size(parts); ++i) {
				const Part& part = parts[i];
				if (std::string(part.Name) == "You" && !Sandbox::IsGodMode()) {
					continue;
				}
				bool showing = Sandbox::IsOpen() && s_CurrentTab == part.Name;
				ImGui::PushID(static_cast<int>(i) + 500);
				if (i > 0) {
					ImGui::SameLine();
				}
				if (BarTile("##part", part.Tip, showing, [&](ImDrawList* drawList, ImVec2 at, float room) { DrawIcon(drawList, part.Art, at, room / 12.0F, part.Color); }) == 1) {
					if (showing) {
						Sandbox::SetOpen(false);
					} else {
						Sandbox::SetOpen(true);
						s_WantedTab = part.Name;
						s_CurrentTab = part.Name;
						// Whatever was last picked on that part comes back to hand with it; the first time, the part's first tool.
						static const std::map<std::string, Tool> firstTools = {{"Spawn", Tool::Unit}, {"Build", Tool::Structure}, {"Paint", Tool::Fire}, {"Boom", Tool::Grenade}, {"Effects", Tool::Effect}, {"Orders", Tool::Command}, {"You", Tool::PlayCharacter}};
						if (auto remembered = s_LastToolOfTab.find(part.Name); remembered != s_LastToolOfTab.end()) {
							s_ToolIndex = remembered->second;
						} else if (auto first = firstTools.find(part.Name); first != firstTools.end()) {
							s_ToolIndex = ToolIndex(first->second);
						}
					}
				}
				ImGui::PopID();
			}
		}
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(4);
	}
} // namespace SandboxDetail
