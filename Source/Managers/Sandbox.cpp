#include "SandboxInternal.h"
#include "ActorWater.h"
#include "MenuMan.h"
#include "Notices.h"
#include "Weather.h"
#include "TerrainCandle.h"
#include "ZeroGravity.h"

bool Sandbox::s_Open = false;

std::deque<Actor*>& Sandbox::Actors() {
	return g_MovableMan.m_Actors;
}

std::deque<MovableObject*>& Sandbox::Items() {
	return g_MovableMan.m_Items;
}

bool Sandbox::Do(const std::string& toolName, const Vector& position, int team, int order, int count, const std::string& presetName) {
	if (!InGame()) {
		return false;
	}
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	if (toolName == "Remove water spawners") {
		s_WaterSpawners.clear();
		return true;
	}
	if (toolName == "Run gym") {
		return GymRunAll();
	}
	if (toolName == "Remove effects") {
		s_Effects.clear();
		return true;
	}
	if (toolName == "Finish rope" || toolName == "Remove ropes") {
		// The rope a script is putting down a point at a time ("Rope") finished, or every rope taken away.
		Stroke stroke;
		stroke.Kind = Tool::Rope;
		stroke.Choice = toolName == "Finish rope" ? 1 : 2;
		s_Queue.push_back(stroke);
		return true;
	}
	if (toolName == "Undo") {
		// As Ctrl+Z: the newest paint stroke, placing click or drop taken back.
		QueueSimChange(Tool::UndoTerrain);
		return true;
	}
	if (toolName == "Redo") {
		// As Ctrl+Y: the newest step the undo took back made again.
		QueueSimChange(Tool::RedoTerrain);
		return true;
	}
	if (toolName == "Effect") {
		// The preset name is the effect's name.
		LoadCustomEffects();
		for (size_t i = 0; i < s_CustomEffects.size(); ++i) {
			if (presetName == s_CustomEffects[i].Name) {
				Stroke placed;
				placed.Kind = Tool::Effect;
				placed.Position = position;
				placed.Choice = static_cast<int>(EffectKind::Count) + static_cast<int>(i);
				placed.Layers = s_CustomEffects[i].Layers;
				placed.Material = s_CustomEffects[i].Name;
				s_Queue.push_back(placed);
				return true;
			}
		}
		for (int i = 0; i < static_cast<int>(EffectKind::Count); ++i) {
			if (presetName == c_Effects[i].Name) {
				Stroke placed;
				placed.Kind = Tool::Effect;
				placed.Position = position;
				placed.Choice = i;
				s_Queue.push_back(placed);
				return true;
			}
		}
		return false;
	}
	Stroke stroke;
	stroke.Position = position;
	stroke.Team = std::clamp(team, 0, c_Sides - 1);
	stroke.Orders = UnitOrder(order);
	stroke.Count = std::max(count, 1);
	stroke.Radius = std::max(count, 1);
	if (toolName == "Loadout" || toolName == "Squad") {
		// One of your loadouts (count of them) or one of your squads, by name (the preset name), put down as the Unit tool does.
		CustomPick pick{toolName == "Squad" ? CustomPick::Squad : CustomPick::Loadout, presetName};
		std::vector<CustomLoadout> units = UnitsOfPick(pick);
		if (units.empty()) {
			return false;
		}
		stroke.Kind = Tool::Unit;
		if (pick.Kind == CustomPick::Squad) {
			stroke.SquadName = presetName;
			stroke.Customs = std::move(units);
		} else {
			stroke.Customs.assign(static_cast<size_t>(stroke.Count), units.front());
		}
		s_Queue.push_back(stroke);
		return true;
	}
	if (ContainsIgnoringCase(toolName, "Orders") && toolName.size() == 6) {
		stroke.Kind = Tool::OrderSide;
		s_Queue.push_back(stroke);
		return true;
	}
	if (ContainsIgnoringCase(toolName, "Select") && toolName.size() == 6) {
		// A box of half-size count around the point.
		stroke.Kind = Tool::Select;
		stroke.Position = position - Vector(static_cast<float>(count), static_cast<float>(count));
		stroke.Position2 = position + Vector(static_cast<float>(count), static_cast<float>(count));
		s_Queue.push_back(stroke);
		return true;
	}
	int toolIndex = -1;
	// (The spring tool's old name, still taken from scripts.)
	const std::string lookedFor = ContainsIgnoringCase("Water spawner", toolName.c_str()) && toolName.size() == 13 ? std::string("Spring") : toolName;
	for (int i = 0; i < c_ToolCount; ++i) {
		std::string name = c_Tools[i].Name;
		if (name.size() == lookedFor.size() && ContainsIgnoringCase(name, lookedFor.c_str())) {
			toolIndex = i;
		}
	}
	if (toolIndex < 0) {
		return false;
	}
	stroke.Kind = c_Tools[toolIndex].Kind;
	if (stroke.Kind == Tool::CollapseArea) {
		// A box of half-size count around the point, as "Select".
		stroke.Position = position - Vector(static_cast<float>(count), static_cast<float>(count));
		stroke.Position2 = position + Vector(static_cast<float>(count), static_cast<float>(count));
		s_Queue.push_back(stroke);
		return true;
	}
	if (stroke.Kind == Tool::Command && (s_CommandMode == CommandMode::Select || s_CommandMode == CommandMode::Command)) {
		// (A script's command clicks are orders, as they were before the tool started out selecting: a move, or an attack on an enemy.)
		stroke.OrderMode = static_cast<int>(CommandMode::Move);
	}
	if (stroke.Kind == Tool::None) {
		// "Look around" from a script: put the free camera on the point, and stop following anything.
		s_FreeCamera = true;
		s_FreeCameraStarted = true;
		s_FollowTarget = UnitRef();
		s_FollowAction = false;
		s_CameraCenter = position;
		g_CameraMan.SetScroll(position, 0);
		return true;
	}
	int randomFaction = -1;
	if (presetName.rfind("Random ", 0) == 0) {
		// "Random <faction>" as the window's box gives: the faction by name, as SandboxAutoBattleSide takes it.
		std::string name = presetName.substr(7);
		for (size_t i = 0; i < s_FactionNames.size(); ++i) {
			if (s_FactionNames[i] == name || s_FactionNames[i] + ".rte" == name) {
				randomFaction = static_cast<int>(i);
			}
		}
	}
	if ((stroke.Kind == Tool::Drop || stroke.Kind == Tool::Unit) && (presetName == "Random units" || presetName == "Random favourites" || randomFaction >= 0)) {
		// Random units, as the window's "Random units" box: each one picked from every faction's units, from the favourites or from one faction.
		stroke.Random = true;
		stroke.FavouritesOnly = presetName == "Random favourites";
		stroke.RandomFaction = randomFaction;
	} else if (stroke.Kind == Tool::Unit || stroke.Kind == Tool::Vehicle || stroke.Kind == Tool::Drop || stroke.Kind == Tool::Brain || stroke.Kind == Tool::Item || stroke.Kind == Tool::Structure || stroke.Kind == Tool::Barracks) {
		const std::vector<Preset>& list = ListFor(stroke.Kind);
		auto found = std::find_if(list.begin(), list.end(), [&presetName](const Preset& preset) { return preset.PresetName == presetName; });
		if (found == list.end()) {
			return false;
		}
		stroke.Choice = static_cast<int>(found - list.begin());
		if (stroke.Kind == Tool::Structure) {
			stroke.Count = 1;
		}
		if (std::getenv("CCCP_TEST_POINTER")) {
			ChoiceFor(stroke.Kind) = stroke.Choice;
		}
	}
	if (stroke.Kind == Tool::Rope) {
		// The next point of the rope being put down (or its first). The preset name is its kind ("Chain"); none, the one picked in the window.
		stroke.Choice = 0;
		stroke.Material = RopeSim::FindType(presetName) >= 0 ? presetName : std::string(RopeSim::GetType(s_RopeType).Name);
		stroke.Rate = s_RopeSlack;
		stroke.RopeStrength = s_RopeStrength;
		stroke.RopeAnchor = s_RopeAnchor;
		s_Queue.push_back(stroke);
		return true;
	}
	if (stroke.Kind == Tool::TerrainOther || stroke.Kind == Tool::Metal) {
		// The material to paint is the preset name; none, the one chosen in the window.
		stroke.Material = !presetName.empty() ? presetName : (stroke.Kind == Tool::Metal ? s_PaintMetal : s_OtherTerrain);
	}
	if (std::getenv("CCCP_TEST_POINTER")) {
		// Test runs that show what the pointer does: what a script used stays in hand, for the side it used.
		s_ToolIndex = toolIndex;
		s_Team = std::clamp(team, 0, c_Sides - 1);
		if (c_Tools[toolIndex].UsesRadius) {
			s_Radius = std::clamp(stroke.Radius, 1, c_MaxBrushRadius);
		} else if (stroke.Kind == Tool::Unit) {
			s_SquadSize = std::max(stroke.Count, 1);
		}
	}
	s_Queue.push_back(stroke);
	return true;
}

void Sandbox::SetAIPaused(bool paused) {
	Controller::SetAIPaused(paused);
}

void Sandbox::SetColonyPower(bool needsPower, bool slowWithout) {
	Colony::NeedsPower() = needsPower;
	Colony::WithoutPower() = slowWithout ? Colony::NoPower::Slows : Colony::NoPower::Stops;
}

std::string Sandbox::ColonyStatus(int id) {
	for (const Colony::Building& building: Colony::Buildings()) {
		if (building.ID == id) {
			return building.Status;
		}
	}
	return "";
}

bool Sandbox::SetBuildMode(bool build) {
	GameActivity* game = CurrentGame();
	if (!game || !InGame()) {
		return false;
	}
	if (build && s_Possessed) {
		// Building is done from the god view.
		ReleaseControl();
	}
	game->SetFreeBuildMode(build);
	if (build) {
		// The build menu needs the mouse, so every tool window goes away. Tab (or Done in its pie menu) comes back.
		g_DebugMan.CloseTools();
	}
	return game->IsFreeBuildMode();
}

int Sandbox::CountUnits(int team) {
	int count = 0;
	for (const Actor* actor: SandboxAccess::Actors()) {
		// (Not a brain: it doesn't fight, and a side down to its brain is out of the battle.)
		// (Nor a non-combatant, NC-1: an animal or a civilian isn't a unit in the fight.)
		if (!IsSoldier(actor) || actor->GetTeam() != team || actor->IsInGroup("Brains")) {
			continue;
		}
		if (!dynamic_cast<const ACraft*>(actor)) {
			++count;
			continue;
		}
		// The passengers of a craft of the side's, still on the way in: inventory, not in the world. (Left out, a side whose last wave
		// was in the air was "gone", and the old auto battle was called for the other side.)
		for (const MovableObject* item: *actor->GetInventory()) {
			if (const Actor* passenger = dynamic_cast<const Actor*>(item); passenger && !passenger->IsDead() && passenger->GetHealth() > 0.0F) {
				++count;
			}
		}
	}
	return count;
}

Actor* Sandbox::SpawnUnit(const std::string& presetName, int team, const Vector& position, int order) {
	if (!InGame()) {
		return nullptr;
	}
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	// (Vehicles are listed apart from the units, VH: a script puts one down by name all the same.)
	const Preset* preset = FindPreset(s_Units, presetName);
	if (!preset) {
		preset = FindPreset(s_Vehicles, presetName);
	}
	Actor* actor = preset ? CreateUnit(*preset, team, 0, UnitOrder(order)) : nullptr;
	if (!actor) {
		return nullptr;
	}
	ActivateSide(team);
	actor->SetPos(position);
	g_MovableMan.AddActor(actor);
	return actor;
}

Actor* Sandbox::SpawnBrain(const std::string& presetName, int team, const Vector& position) {
	if (!InGame()) {
		return nullptr;
	}
	const Entity* preset = g_PresetMan.GetEntityPreset("Actor", presetName);
	Actor* brain = preset ? dynamic_cast<Actor*>(CreateObject(preset->GetClassName(), presetName, preset->GetModuleID())) : nullptr;
	if (!brain) {
		return nullptr;
	}
	ActivateSide(team);
	brain->SetPos(position);
	brain->SetTeam(team);
	brain->SetControllerMode(Controller::CIM_AI);
	g_MovableMan.AddActor(brain);
	GiveOrder(brain, Order::Hold);
	return brain;
}

Actor* Sandbox::SpawnWorker(const std::string& presetName, const std::string& moduleName, int team, const Vector& position) {
	if (!InGame()) {
		return nullptr;
	}
	const Entity* preset = g_PresetMan.GetEntityPreset("AHuman", presetName, moduleName);
	Actor* worker = preset ? dynamic_cast<Actor*>(preset->Clone()) : nullptr;
	if (!worker) {
		return nullptr;
	}
	ActivateSide(team);
	worker->SetPos(position);
	worker->SetTeam(team);
	worker->SetControllerMode(Controller::CIM_AI);
	worker->SetAIMode(Actor::AIMODE_SENTRY);
	worker->SetOrderHold(false);
	g_MovableMan.AddActor(worker);
	return worker;
}

bool Sandbox::PlantTree(const Vector& at, float scale, float variant, float piece, bool mirror, int& left, int& top, int& right, int& bottom) {
	if (!InGame()) {
		return false;
	}
	PlantRoll roll;
	roll.Variant = variant;
	roll.Piece = piece;
	roll.Jitter = 0.5F;
	roll.Depth = 0.0F;
	roll.Mirror = mirror;
	roll.Kind = Tool::Trees;
	PlantPlacement plan;
	if (!PlanPlant(at, 0, Tool::Trees, scale, roll, plan) || !plan.Piece) {
		return false;
	}
	int width = std::max(1, static_cast<int>(static_cast<float>(plan.Piece->w) * plan.Scale));
	int height = std::max(1, static_cast<int>(static_cast<float>(plan.Piece->h) * plan.Scale));
	if (plan.LeafPiece) {
		width = std::max(width, static_cast<int>(static_cast<float>(plan.LeafPiece->w) * plan.Scale));
		height = std::max(height, static_cast<int>(static_cast<float>(plan.LeafPiece->h) * plan.Scale));
	}
	left = plan.Left;
	top = plan.Upper;
	right = plan.Left + width - 1;
	bottom = plan.Upper + height - 1;
	// (Not the player's own stroke: nothing to undo.)
	bool recording = s_RecordPaint;
	s_RecordPaint = false;
	PlacePlant(at, 0, Tool::Trees, scale, &roll);
	s_RecordPaint = recording;
	return true;
}

float Sandbox::UnitCost(const std::string& presetName) {
	// (The catalogue is built when the window is first drawn; a barracks a script placed before that trained at the 20 supply floor.)
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	const Preset* preset = FindPreset(s_Units, presetName);
	if (!preset) {
		preset = FindPreset(s_Vehicles, presetName);
	}
	const SceneObject* object = preset ? dynamic_cast<const SceneObject*>(g_PresetMan.GetEntityPreset(preset->ClassName, preset->PresetName, preset->ModuleID)) : nullptr;
	return object ? object->GetGoldValue(preset->ModuleID, 1.0F, 1.0F) : 0.0F;
}

void Sandbox::FillBox(const Vector& topLeft, int width, int height, const std::string& materialName) {
	if (!InGame() || width <= 0 || height <= 0) {
		return;
	}
	if (materialName.empty()) {
		ClearBox(topLeft, width, height);
	} else {
		PaintBox(topLeft, width, height, materialName.c_str());
	}
}

void Sandbox::OnToolsClosed(bool atPointer) {
	GameActivity* game = CurrentGame();
	if (!IsGodMode() || !game || game->IsFreeBuildMode() || s_Possessed) {
		return;
	}
	// With a tool in hand, or no character, the tools are only hidden: you stay above, the tool goes on working on the world, and P steps into the character.
	// With nothing in hand ("Look around") putting the tools away is stepping into the character, as is Shift+Tab whatever is in hand.
	if (!HasCharacter() || (CurrentTool().Kind != Tool::None && !atPointer)) {
		s_PlayHintSeconds = 8.0F;
		return;
	}
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	Stroke stroke;
	stroke.Kind = Tool::PlayCharacter;
	stroke.Count = atPointer ? 1 : 0;
	stroke.Position = MouseScenePosition();
	s_Queue.push_back(stroke);
}

void Sandbox::SetRespawnPoint(bool atPointer) {
	GameActivity* game = CurrentGame();
	if (!IsGodMode() || !game || game->IsFreeBuildMode() || !HasCharacter()) {
		return;
	}
	const Actor* playing = s_Possessed && s_Possessed == GetRef(s_PlayerUnit) ? s_Possessed : nullptr;
	s_RespawnPoint = playing && !atPointer ? playing->GetPos() : MouseScenePosition();
	s_RespawnSet = true;
	g_ConsoleMan.PrintString("SANDBOX: Respawn point set. If your character dies while you play it, it comes back here.");
}

void Sandbox::TogglePlay(bool atPointer) {
	GameActivity* game = CurrentGame();
	if (!IsGodMode() || !game || game->IsFreeBuildMode()) {
		return;
	}
	if (s_Possessed) {
		// Back above, with the tools as they were (hidden) and whatever tool was in hand still in it.
		Stroke release;
		release.Kind = Tool::Release;
		s_Queue.push_back(release);
		s_Possessed = nullptr;
		s_FreeCameraStarted = false;
		s_PlayHintSeconds = 8.0F;
		return;
	}
	if (!HasCharacter()) {
		if (IsBattleCommand()) {
			return;
		}
		g_ConsoleMan.PrintString("SANDBOX: There is no character to play. Tick \"Have a character of my own\" in the sandbox's You tab.");
		return;
	}
	if (!s_CatalogueBuilt) {
		BuildCatalogue();
	}
	Stroke stroke;
	stroke.Kind = Tool::PlayCharacter;
	stroke.Count = atPointer ? 1 : 0;
	stroke.Position = MouseScenePosition();
	s_Queue.push_back(stroke);
}

std::string Sandbox::GetCharacterSetup() {
	std::string setup = s_Player.Body + "|" + std::to_string(s_Player.Team) + "|";
	for (bool flag: {s_Player.Unkillable, s_Player.EndlessJetpack, s_Player.EndlessAmmo, s_Player.NumberKeys, s_Player.FlyKey, s_Player.EnterOnClose, s_PauseInMenus, s_Player.Neutral, s_Player.InheritKit, s_Player.RemoveOnLeave, s_Player.RespawnInstant}) {
		setup += flag ? '1' : '0';
	}
	setup += "|";
	for (size_t i = 0; i < s_Player.Kit.size(); ++i) {
		setup += (i > 0 ? ";" : "") + s_Player.Kit[i];
	}
	return setup;
}

std::string Sandbox::GetFavourites() {
	std::string favourites;
	for (const Pin& favourite: s_Favourites) {
		favourites += (favourites.empty() ? "" : ";") + std::string(c_Tools[ToolIndex(favourite.Kind)].Name) + "=" + favourite.PresetName;
	}
	return favourites;
}

void Sandbox::SetFavourites(const std::string& favourites) {
	s_Favourites.clear();
	for (size_t at = 0; at < favourites.size();) {
		size_t end = favourites.find(';', at);
		end = end == std::string::npos ? favourites.size() : end;
		std::string one = favourites.substr(at, end - at);
		size_t equals = one.find('=');
		if (equals != std::string::npos) {
			std::string toolName = one.substr(0, equals);
			for (int i = 0; i < c_ToolCount; ++i) {
				if (toolName == c_Tools[i].Name) {
					s_Favourites.push_back({c_Tools[i].Kind, one.substr(equals + 1)});
					break;
				}
			}
		}
		at = end + 1;
	}
}

std::string Sandbox::GetPins() {
	std::string pins;
	for (const Pin& pin: s_Pins) {
		pins += (pins.empty() ? "" : ";") + std::string(c_Tools[ToolIndex(pin.Kind)].Name) + "=" + pin.PresetName;
	}
	return pins;
}

namespace {
	void ReadPins(const std::string& pins) {
		s_Pins.clear();
		for (size_t at = 0; at < pins.size();) {
			size_t end = pins.find(';', at);
			end = end == std::string::npos ? pins.size() : end;
			std::string one = pins.substr(at, end - at);
			size_t equals = one.find('=');
			if (equals != std::string::npos) {
				std::string toolName = one.substr(0, equals);
				for (int i = 0; i < c_ToolCount; ++i) {
					if (toolName == c_Tools[i].Name && s_Pins.size() < 24) {
						s_Pins.push_back({c_Tools[i].Kind, one.substr(equals + 1)});
						break;
					}
				}
			}
			at = end + 1;
		}
	}
} // namespace

void Sandbox::SetPins(const std::string& pins) {
	ReadPins(pins);
	SavePinsFile();
}

void Sandbox::LoadPins(const std::string& fromOldSave) {
	static bool loaded = false;
	if (loaded) {
		return;
	}
	loaded = true;
	if (std::ifstream file(c_PinsFile); file) {
		std::string line;
		std::getline(file, line);
		ReadPins(line);
	} else if (!fromOldSave.empty()) {
		SetPins(fromOldSave);
	}
}

void Sandbox::SetCharacterSetup(const std::string& setup) {
	std::vector<std::string> parts;
	size_t start = 0;
	for (size_t bar = setup.find('|'); parts.size() < 3 && bar != std::string::npos; bar = setup.find('|', start)) {
		parts.push_back(setup.substr(start, bar - start));
		start = bar + 1;
	}
	parts.push_back(setup.substr(start));
	if (parts.size() != 4 || parts[0].empty()) {
		return;
	}
	s_Player.Body = parts[0];
	s_Player.Team = std::clamp(std::atoi(parts[1].c_str()), 0, c_Sides - 1);
	bool* flags[] = {&s_Player.Unkillable, &s_Player.EndlessJetpack, &s_Player.EndlessAmmo, &s_Player.NumberKeys, &s_Player.FlyKey, &s_Player.EnterOnClose, &s_PauseInMenus, &s_Player.Neutral, &s_Player.InheritKit, &s_Player.RemoveOnLeave, &s_Player.RespawnInstant};
	for (size_t i = 0; i < std::size(flags) && i < parts[2].size(); ++i) {
		*flags[i] = parts[2][i] == '1';
	}
	s_Player.Kit.clear();
	for (size_t at = 0; at < parts[3].size();) {
		size_t end = parts[3].find(';', at);
		end = end == std::string::npos ? parts[3].size() : end;
		if (end > at) {
			s_Player.Kit.push_back(parts[3].substr(at, end - at));
		}
		at = end + 1;
	}
}

std::string Sandbox::GetPanelSetup() {
	// "key:value" entries, a bar between: the tab, the brush, the filters, then each tab's and shelf group's last tool ("tabTool:tab:tool")
	// and each list's kind and mod filters ("kind:list:kind"). Tools are kept by their Tool number (tools are only ever appended, so they keep
	// their numbers). (No '=' in it: the settings file's reader takes a value from after the first one on its line.)
	std::string setup = "tab:" + s_CurrentTab + "|radius:" + std::to_string(s_Radius) + "|shape:" + std::to_string(static_cast<int>(s_BrushShape));
	setup += "|fill:" + std::string(s_ShapeFill ? "1" : "0") + "|fillShape:" + std::to_string(static_cast<int>(s_FillShape));
	setup += "|modded:" + std::string(s_ShowModded ? "1" : "0") + "|favourites:" + std::string(s_FavouritesOnly ? "1" : "0");
	auto toolNumber = [](int toolIndex) { return std::to_string(static_cast<int>(c_Tools[std::clamp(toolIndex, 0, c_ToolCount - 1)].Kind)); };
	for (const auto& [tab, toolIndex]: s_LastToolOfTab) {
		setup += "|tabTool:" + tab + ":" + toolNumber(toolIndex);
	}
	for (const auto& [group, toolIndex]: s_LastToolOfGroup) {
		setup += "|groupTool:" + group + ":" + toolNumber(toolIndex);
	}
	for (const auto& [filters, prefix]: {std::pair{&s_KindFilter, "|kind:"}, std::pair{&s_ModFilter, "|mod:"}}) {
		for (const auto& [kind, value]: *filters) {
			if (!value.empty()) {
				setup += prefix + std::to_string(static_cast<int>(kind)) + ":" + value;
			}
		}
	}
	return setup;
}

void Sandbox::SetPanelSetup(const std::string& setup) {
	// A tool kept by its number, as a c_Tools index; -1 for a number that isn't a tool (from a newer version, or not one at all).
	auto toolIndexOf = [](const std::string& number) {
		const Tool kind = static_cast<Tool>(std::atoi(number.c_str()));
		const int index = ToolIndex(kind);
		return c_Tools[index].Kind == kind ? index : -1;
	};
	for (size_t at = 0; at < setup.size();) {
		size_t end = setup.find('|', at);
		end = end == std::string::npos ? setup.size() : end;
		const std::string entry = setup.substr(at, end - at);
		at = end + 1;
		const size_t colon = entry.find(':');
		if (colon == std::string::npos) {
			continue;
		}
		const std::string key = entry.substr(0, colon);
		const std::string value = entry.substr(colon + 1);
		const int number = std::atoi(value.c_str());
		// The entries with a name before their value: the tab or group, or the list.
		const size_t nameEnd = value.find(':');
		const std::string name = nameEnd != std::string::npos ? value.substr(0, nameEnd) : std::string();
		const std::string named = nameEnd != std::string::npos ? value.substr(nameEnd + 1) : std::string();
		if (key == "tab") {
			s_CurrentTab = value;
		} else if (key == "radius") {
			s_Radius = std::clamp(number, 1, c_MaxBrushRadius);
		} else if (key == "shape") {
			s_BrushShape = static_cast<BrushShape>(std::clamp(number, 0, 2));
		} else if (key == "fill") {
			s_ShapeFill = number != 0;
		} else if (key == "fillShape") {
			s_FillShape = static_cast<FillShape>(std::clamp(number, 0, c_FillShapeCount - 1));
		} else if (key == "modded") {
			s_ShowModded = number != 0;
		} else if (key == "favourites") {
			s_FavouritesOnly = number != 0;
		} else if ((key == "tabTool" || key == "groupTool") && !name.empty()) {
			if (int toolIndex = toolIndexOf(named); toolIndex >= 0) {
				(key == "tabTool" ? s_LastToolOfTab : s_LastToolOfGroup)[name] = toolIndex;
			}
		} else if ((key == "kind" || key == "mod") && !name.empty()) {
			(key == "kind" ? s_KindFilter : s_ModFilter)[static_cast<Tool>(std::atoi(name.c_str()))] = named;
		}
	}
}

std::string Sandbox::GetVehicleSetup() {
	const VehicleTuning& tuning = s_VehicleTuning;
	std::ostringstream setup;
	setup << (tuning.Modified ? 1 : 0) << (!tuning.Crewed ? 0 : (tuning.DriverOnly ? 2 : 1)) << "|" << tuning.Power << ";" << tuning.TopSpeed << ";" << tuning.Grip << ";" << tuning.Brakes << ";" << tuning.Jump << ";" << tuning.WaterPower << ";" << tuning.WaterSpeed << ";" << tuning.Friction << "|" << tuning.Driver << "|" << tuning.Crew << "|" << tuning.TerrainCrush;
	return setup.str();
}

void Sandbox::SetVehicleSetup(const std::string& setup) {
	size_t bar = setup.find('|');
	if (bar == std::string::npos || bar < 2) {
		return;
	}
	VehicleTuning tuning;
	tuning.Modified = setup[0] == '1';
	// (Crew: 0 empty, 1 full, 2 with a driver only.)
	tuning.Crewed = setup[1] == '1' || setup[1] == '2';
	tuning.DriverOnly = setup[1] == '2';
	float* values[] = {&tuning.Power, &tuning.TopSpeed, &tuning.Grip, &tuning.Brakes, &tuning.Jump, &tuning.WaterPower, &tuning.WaterSpeed, &tuning.Friction};
	// (Friction last: not there in settings saved before it could be set, so left at x1.)
	// (Then the driver and crew picks, each after a bar: not there in settings saved before they could be picked.)
	std::istringstream fields(setup.substr(bar + 1));
	std::string field;
	std::getline(fields, field, '|');
	std::getline(fields, tuning.Driver, '|');
	std::getline(fields, tuning.Crew, '|');
	// (Then the terrain damage, -1 for each vehicle's own: not there in settings saved before it could be set.)
	if (std::string crush; std::getline(fields, crush, '|') && !crush.empty()) {
		tuning.TerrainCrush = std::clamp(static_cast<float>(std::atof(crush.c_str())), -1.0F, c_VehicleCrushMax);
		tuning.TerrainCrush = tuning.TerrainCrush < 0.0F ? -1.0F : tuning.TerrainCrush;
	}
	std::istringstream numbers(field);
	std::string number;
	for (float* value: values) {
		if (!std::getline(numbers, number, ';')) {
			break;
		}
		*value = std::clamp(static_cast<float>(std::atof(number.c_str())), c_VehicleTuneMin, c_VehicleTuneMax);
	}
	s_VehicleTuning = tuning;
}

bool Sandbox::WantsWorldPaused() {
	return IsGodMode() && s_Open && s_PauseInMenus && g_TimerMan.GetSimUpdateCount() > s_GodStartUpdate + 90;
}

bool Sandbox::IsGodMode() {
	const Activity* activity = g_ActivityMan.GetActivity();
	return activity && InGame() && (activity->GetPresetName() == "Sandbox" || activity->GetPresetName() == "Battle Command");
}

bool Sandbox::IsBattleCommand() {
	const Activity* activity = g_ActivityMan.GetActivity();
	return activity && InGame() && activity->GetPresetName() == "Battle Command";
}

void Sandbox::ToggleCommanderBar() {
	if (IsGodMode() && (c_CommanderBarSwitch || !IsBattleCommand())) {
		s_CommanderBar = !s_CommanderBar;
		// (A sandbox tool in hand, a paint brush say, isn't on the Commander Toolbar: the Command tool is taken up instead.)
		const Tool inHand = CurrentTool().Kind;
		if (s_CommanderBar && inHand != Tool::Command && inHand != Tool::Follow && inHand != Tool::Possess && !IsBattleTool(inHand)) {
			s_ToolIndex = ToolIndex(Tool::Command);
		}
		// (Units of another side picked while the sandbox's bar was up aren't yours to command under the Commander Toolbar.)
		if (int side = OnlySide(); side >= 0) {
			s_Selected.erase(std::remove_if(s_Selected.begin(), s_Selected.end(), [side](const UnitRef& ref) { const Actor* unit = GetRef(ref); return !unit || unit->GetTeam() != side; }), s_Selected.end());
		}
	}
}

namespace {
	/// The Commander Toolbar key from the settings, F11 if it names no key, or F10 or F12 (picked before those were taken out of the
	/// choices: they clear the console and take a screenshot).
	SDL_Scancode CommanderBarScancode() {
		SDL_Scancode key = SDL_GetScancodeFromName(g_SettingsMan.CommanderBarKey().c_str());
		return (key == SDL_SCANCODE_UNKNOWN || key == SDL_SCANCODE_F10 || key == SDL_SCANCODE_F12) ? SDL_SCANCODE_F11 : key;
	}
} // namespace

bool Sandbox::IsCommanderBarKey(int scancode) {
	return scancode == CommanderBarScancode();
}

std::string Sandbox::CommanderBarKeyName() {
	return SDL_GetScancodeName(CommanderBarScancode());
}

bool Sandbox::TakesObjectiveKey() {
	// As CommandHotkeys is reached in DrawGUI: the command tool in hand, units selected, not playing one, no text box.
	return InGame() && CurrentTool().Kind == Tool::Command && !s_Selected.empty() && !s_Possessed && !ImGui::GetIO().WantTextInput;
}

bool Sandbox::WantsWheelZoom() {
	return IsLookingAround() && !ImGui::GetIO().WantCaptureMouse;
}

bool Sandbox::TakeBrushWheel(bool up) {
	if (!CapturesWorldClicks() || s_Possessed || !IsPaintTool(CurrentTool().Kind)) {
		return false;
	}
	return StepBrushSize(up ? 1 : -1);
}

void Sandbox::ToggleCommander() {
	GameActivity* game = CurrentGame();
	if (!game || !InGame() || IsGodMode()) {
		s_Commander = false;
		return;
	}
	if (!s_Commander) {
		int team = game->GetTeamOfPlayer(Players::PlayerOne);
		if (team < 0 || team >= c_Sides) {
			return;
		}
		Actor* controlled = game->GetControlledActor(Players::PlayerOne);
		s_CommanderReturnTo = controlled && g_MovableMan.IsActor(controlled) ? MakeRef(controlled) : UnitRef();
		s_Commander = true;
		s_CommanderTeam = team;
		s_Team = team;
		s_Selected.erase(std::remove_if(s_Selected.begin(), s_Selected.end(), [team](const UnitRef& ref) { const Actor* unit = GetRef(ref); return !unit || unit->GetTeam() != team; }), s_Selected.end());
		if (controlled) {
			game->LoseControlOfActor(Players::PlayerOne);
		}
		game->SetViewState(Activity::ViewState::Observe, Players::PlayerOne);
		s_FreeCamera = true;
		s_FreeCameraStarted = false;
		s_FollowTarget = UnitRef();
		s_ToolIndex = ToolIndex(Tool::Command);
		s_CommandMode = CommandMode::Command;
		s_Open = true;
		return;
	}
	s_Commander = false;
	s_FreeCamera = false;
	// Back into the unit you left, or else your brain, or else whatever the game gives you next.
	Actor* back = GetRef(s_CommanderReturnTo);
	if (!back || back->GetTeam() != s_CommanderTeam) {
		back = game->GetPlayerBrain(Players::PlayerOne);
	}
	if (back && g_MovableMan.IsActor(back) && game->SwitchToActor(back, Players::PlayerOne, s_CommanderTeam)) {
		game->SetViewState(Activity::ViewState::Normal, Players::PlayerOne);
	} else {
		game->SetViewState(Activity::ViewState::ActorSelect, Players::PlayerOne);
	}
}

bool Sandbox::IsCommander() {
	return s_Commander;
}

bool Sandbox::IsLookingAround() {
	if (s_Commander) {
		return CommanderLooking();
	}
	// Automated test runs (CCCP_HIDE_PANELS) place the camera themselves and want no pointer in their pictures.
	static const bool testRun = std::getenv("CCCP_HIDE_PANELS") != nullptr;
	const GameActivity* game = CurrentGame();
	return IsGodMode() && game && !s_Possessed && s_PlayerEnterPending == 0 && !game->IsFreeBuildMode() && (!testRun || s_Open);
}

bool Sandbox::TakeEscape() {
	// Copy area: Esc puts the stamp away, or else unmarks the box (not pausing on it).
	if (IsGodMode() && !s_Possessed && (s_Open || IsLookingAround()) && CurrentTool().Kind == Tool::CopyArea && !ImGui::GetIO().WantTextInput) {
		if (s_Stamping) {
			s_Stamping = false;
			return true;
		}
		if (s_CopyBoxSet || s_ShapeDragging) {
			s_CopyBoxSet = false;
			s_ShapeDragging = false;
			return true;
		}
	}
	// (A hold zone being clicked out is dropped, not the game paused.)
	if (CurrentTool().Kind == Tool::HoldGround && !s_HoldDraft.empty() && !ImGui::GetIO().WantTextInput) {
		s_HoldDraft.clear();
		return true;
	}
	if (!IsGodMode() || s_Possessed || !(s_Open || IsLookingAround()) || CurrentTool().Kind != Tool::Command || s_CommandMode == CommandMode::Command || ImGui::GetIO().WantTextInput) {
		return false;
	}
	s_CommandMode = CommandMode::Command;
	s_PatrolDraft.clear();
	s_BackToCommandAt = -1.0;
	return true;
}

void Sandbox::Destroy() {
	s_Clipboard = Clipboard();
	s_PaintUndo.clear();
}

bool Sandbox::CapturesWorldClicks() {
	// With the tools hidden in the Sandbox game mode, the tool in hand still works on the world.
	static const bool testPointer = std::getenv("CCCP_TEST_POINTER") != nullptr;
	return (s_Open || IsLookingAround() || testPointer) && CurrentTool().Kind != Tool::None && InGame() && !ImGui::GetIO().WantCaptureMouse;
}

void Sandbox::DrawGUI() {
	// A new Sandbox game opens the god view: the window and the free camera.
	if (IsGodMode()) {
		if (s_GodViewPending || !s_GodViewSetUp) {
			s_GodViewPending = false;
			s_GodViewSetUp = true;
			// Automated test runs set CCCP_HIDE_PANELS, so the window (wherever the player last left it) doesn't cover what they capture.
			// Battle Command opens on its own battle panel instead, with the Commander Toolbar: the sandbox's window is only for development there.
			s_Open = std::getenv("CCCP_HIDE_PANELS") == nullptr && !IsBattleCommand();
			s_BattlePanelOpen = IsBattleCommand() && std::getenv("CCCP_HIDE_PANELS") == nullptr;
			s_CommanderBar = IsBattleCommand();
			s_FreeCamera = true;
			s_FreeCameraStarted = false;
			s_CameraWarmupFrames = 30;
			s_Possessed = nullptr;
			s_PlayerUnit = UnitRef();
			s_PlayerMade.clear();
			s_PlayerEnterPending = 0;
			s_RespawnSet = false;
			ClearRespawn();
			s_Flying = false;
			s_StepsWanted = 0;
			s_GodStartUpdate = g_TimerMan.GetSimUpdateCount();
			s_RallySet.fill(false);
			s_Selected.clear();
			s_FollowTarget = UnitRef();
			s_ToolIndex = ToolIndex(IsBattleCommand() ? Tool::Command : Tool::Unit);
			s_CommandMode = CommandMode::Command;
		}
	} else {
		if (s_GodViewSetUp) {
			// Left the sandbox: its pictures aren't needed until it's next opened.
			ForgetPictures();
		}
		s_GodViewSetUp = false;
	}
	// Nothing of the sandbox over the menus: the game's still loaded behind them, so the bar and the window went on being drawn over the
	// main menu (the menu loop draws the debug GUI too) after leaving a Sandbox game, until another game was started.
	if (g_MenuMan.GetIsInMenuScreen()) {
		UnmarkSelection();
		for (int screenId = 0; screenId < c_MaxScreenCount; ++screenId) {
			g_CameraMan.SetBottomOverscroll(0.0F, screenId);
			g_CameraMan.SetSideOverscroll(0.0F, screenId);
		}
		return;
	}
	if (GameActivity* game = CurrentGame(); s_Open && game && game->IsFreeBuildMode()) {
		game->SetFreeBuildMode(false);
	}
	if (s_Open && s_Possessed) {
		// Back to the god view.
		Stroke release;
		release.Kind = Tool::Release;
		s_Queue.push_back(release);
		s_Possessed = nullptr;
		s_FreeCameraStarted = false;
	}
	// In the Sandbox game mode the world stands still while the tools are open, so things can be set up and tuned. What is done with a tool still happens:
	// the world is let through one update for it, a sixtieth of a second.
	{
		bool wantPause = WantsWorldPaused() && !g_DebugMan.IsPhotoModeOpen();
		if (wantPause) {
			g_TimerMan.PauseSim(true, TimerMan::SimPauseSandbox);
			s_PausedByMenus = true;
			// Painting (the brushes held down: terrain, liquids, fire, smoke) is put in the world here, without a step: it is terrain and
			// liquid written in place, which needs no update to show. Let through an update each, as every stroke was, a held brush ran
			// the world at the frame rate under the "paused" banner, water flowing and fire spreading while it was held.
			if (InGame()) {
				auto paint = std::stable_partition(s_Queue.begin(), s_Queue.end(), [](const Stroke& stroke) { return c_Tools[ToolIndex(stroke.Kind)].Interval <= 0.0F; });
				std::vector<Stroke> painted(std::make_move_iterator(paint), std::make_move_iterator(s_Queue.end()));
				s_Queue.erase(paint, s_Queue.end());
				for (const Stroke& stroke: painted) {
					Apply(stroke);
				}
			}
			// Lights, glows and the like are put together by the sim update from the settings as they stand (the lamps' brightness and tint, fire and
			// effect looks...), so a setting changed while the world stands still showed only once it ran again. A widget in use (a slider dragged, a
			// box ticked, a button let go) lets an update through, a slow few a second while it is held, and one as it is let go.
			static bool widgetWasActive = false;
			const bool widgetActive = ImGui::GetActiveID() != 0;
			const bool settingsTouched = (widgetActive && ImGui::GetFrameCount() % 6 == 0) || (widgetWasActive && !widgetActive);
			widgetWasActive = widgetActive;
			if (s_StepsWanted > 0 || !s_Queue.empty() || s_PlayerEnterPending > 0 || settingsTouched) {
				g_TimerMan.StepSim(1);
				s_StepsWanted = std::max(s_StepsWanted - 1, 0);
			}
		} else {
			// (Every frame, not only as the tools close: photo mode closing can set this pause for a frame; see DebugMan.)
			g_TimerMan.PauseSim(false, TimerMan::SimPauseSandbox);
			if (s_PausedByMenus) {
				s_PausedByMenus = false;
				s_StepsWanted = 0;
			}
		}
	}
	auto banner = [](const char* text, float fromTop, ImU32 color, float alpha) {
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		ImVec2 size = ImGui::CalcTextSize(text);
		float scale = g_DebugMan.UsingPixelFont() ? 1.0F : 1.3F;
		GameViewRect view = g_DebugMan.GetUncoveredView();
		ImVec2 at(view.x + (view.w - size.x * scale) * 0.5F, view.y + fromTop);
		drawList->AddRectFilled(ImVec2(at.x - 10.0F, at.y - 4.0F), ImVec2(at.x + size.x * scale + 10.0F, at.y + size.y * scale + 4.0F), IM_COL32(0, 0, 0, static_cast<int>(150.0F * alpha)), 4.0F);
		drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, at, (color & 0x00FFFFFF) | (static_cast<ImU32>(255.0F * alpha) << 24), text);
	};
	if (s_PausedByMenus && InGame()) {
		banner(IsBattleCommand() ? "WORLD PAUSED  -  F7: carry on" : "WORLD PAUSED  -  Tab: play", 8.0F, IM_COL32(150, 210, 255, 255), 1.0F);
	}
	if (IsGodMode() && (s_Possessed || !s_Open) && s_PlayHintSeconds > 0.0F && !g_DebugMan.IsPhotoModeHidingHUD()) {
		// A reminder of the keys, for a few seconds after stepping in.
		s_PlayHintSeconds -= ImGui::GetIO().DeltaTime;
		const std::string barKey = "    " + CommanderBarKeyName() + (s_CommanderBar ? ": sandbox bar" : ": Commander Toolbar");
		std::string hint = IsBattleCommand() ? (c_CommanderBarSwitch ? "Tab: back above    F7: sandbox window" + barKey : "Tab: back above") : "Tab: sandbox tools";
		if (!s_Possessed) {
			if (IsBattleCommand()) {
				hint = c_CommanderBarSwitch ? "F7: sandbox window" + barKey : "";
			} else {
				hint += barKey;
			}
			if (HasCharacter()) {
				hint += "    P: play";
			}
			if (CurrentTool().Kind != Tool::None) {
				hint += std::string("    In hand: ") + CurrentTool().Name;
			}
			hint += IsPaintTool(CurrentTool().Kind) ? "    Right button: dig    Middle drag / WASD: move    Wheel: zoom    [ ]: size    Alt+click: pick" : "    Right drag / WASD: move    Wheel: zoom";
			hint += s_BarShown ? "    U: hide the bar" : "    U: show the bar";
		} else {
			hint += "    P: back above";
		}
		if (s_Possessed && s_Possessed == GetRef(s_PlayerUnit)) {
			if (s_Player.FlyKey) {
				hint += s_Flying ? "    N: stop flying" : "    N: fly";
			}
			if (s_Player.NumberKeys && !s_Player.Kit.empty()) {
				hint += "    1-" + std::to_string(std::min<size_t>(s_Player.Kit.size(), 9)) + ": kit";
			}
		}
		banner(hint.c_str(), 8.0F, IM_COL32(255, 255, 255, 255), std::clamp(s_PlayHintSeconds, 0.0F, 1.0F));
	}
	{
		// The swim keys (LM-4), the first time the unit you play is in liquid over its waist.
		static long swimHintedFor = 0;
		static float swimHintSeconds = 0.0F;
		if (s_Possessed && s_PossessedID != swimHintedFor && g_MovableMan.IsActor(s_Possessed) && static_cast<long>(s_Possessed->GetUniqueID()) == s_PossessedID && ActorWater::IsEnabled() && ActorWater::GetDepth(s_Possessed) >= 2) {
			swimHintedFor = s_PossessedID;
			swimHintSeconds = 7.0F;
		}
		if (swimHintSeconds > 0.0F && !g_DebugMan.IsPhotoModeHidingHUD()) {
			swimHintSeconds -= ImGui::GetIO().DeltaTime;
			banner("Swimming: Up or Jump strokes up, Down or Crouch dives    Air runs out with the head under: watch the Air gauge", 34.0F, IM_COL32(150, 210, 255, 255), std::clamp(swimHintSeconds, 0.0F, 1.0F));
		}
	}
	if (Controller::IsAIPaused() && InGame()) {
		// A reminder that nobody will move until it's resumed, with the key that resumes it.
		const std::string& key = g_SettingsMan.GetPauseAIKey();
		std::string bannerText = key.empty() ? std::string("AI PAUSED") : "AI PAUSED (" + key + ")";
		const char* banner = bannerText.c_str();
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		ImVec2 size = ImGui::CalcTextSize(banner);
		float scale = g_DebugMan.UsingPixelFont() ? 2.0F : 1.6F;
		ImVec2 at(g_WindowMan.GetGameViewRect().x + (g_WindowMan.GetGameViewRect().w - size.x * scale) * 0.5F, g_WindowMan.GetGameViewRect().y + 36.0F);
		drawList->AddRectFilled(ImVec2(at.x - 10.0F, at.y - 4.0F), ImVec2(at.x + size.x * scale + 10.0F, at.y + size.y * scale + 4.0F), IM_COL32(0, 0, 0, 150), 4.0F);
		drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, at, IM_COL32(255, 210, 80, 255), banner);
	}
	if (InGame() && !Colony::Buildings().empty() && !g_DebugMan.IsPhotoModeHidingHUD()) {
		DrawColony();
	}
	if (InGame() && !g_DebugMan.IsPhotoModeHidingHUD()) {
		DrawBuildingPanel();
	}
	if (InGame() && !g_DebugMan.IsPhotoModeHidingHUD()) {
		// A Battle Director mode's game: its flags and score, with the window open or not.
		DrawBattleMode();
		DrawBattleEdit();
		DrawBattleEditNote();
	}
	// With the tools hidden in the Sandbox game mode you're still above it all: the view goes on moving with the mouse and keys, and the tool in hand goes on
	// working. Only the window itself is left out.
	bool hiddenButAbove = !s_Open && IsLookingAround();
	// The bar is there whenever you're above the world and not playing a unit, whatever else is open or hidden, unless put away with U.
	if (s_BarShown && IsGodMode() && InGame() && !s_Possessed && s_PlayerEnterPending == 0 && !g_DebugMan.IsPhotoModeHidingHUD()) {
		if (!s_CatalogueBuilt) {
			BuildCatalogue();
		}
		if (s_CommanderBar) {
			DrawCommanderBar();
		} else {
			DrawBar();
		}
	} else {
		s_BarHeight = 0.0F;
	}
	{
		// Whichever bar is up along the foot of the picture covers that much of the bottom screens, so they may scroll that far past the bottom of the map:
		// otherwise the bottom of a map that doesn't wrap stays under the bar for good.
		GameViewRect view = g_WindowMan.GetGameViewRect();
		bool hSplit = g_FrameMan.GetHSplit();
		float scenePerWindowPixel = static_cast<float>(g_FrameMan.GetPlayerScreenHeight() * (hSplit ? 2 : 1)) / std::max(1.0F, view.h);
		float overscroll = std::ceil(s_BarHeight * scenePerWindowPixel);
		int screenCount = g_FrameMan.GetScreenCount();
		for (int screenId = 0; screenId < c_MaxScreenCount; ++screenId) {
			// Split above and below, only the lower screens reach down to the bar.
			bool alongBottom = screenId < screenCount && (!hSplit || screenId >= screenCount / 2);
			g_CameraMan.SetBottomOverscroll(alongBottom ? overscroll : 0.0F, screenId);
			// The same amount past the left and right edges, so the corners and edges of the map can be brought out from under the panels too.
			g_CameraMan.SetSideOverscroll(screenId < screenCount ? overscroll : 0.0F, screenId);
		}
	}
	if (IsBattleCommand() && InGame()) {
		KeepCommandedTeam();
		// Till the battle is started the panel stays open: it's the only way to start one, and with it shut there was nothing to do.
		static const bool hidePanelsForTests = std::getenv("CCCP_HIDE_PANELS") != nullptr;
		if (!s_ModeRun.Running && !hidePanelsForTests) {
			s_BattlePanelOpen = true;
		}
		if (s_BattlePanelOpen && !s_Possessed && !g_DebugMan.IsPhotoModeHidingHUD()) {
			DrawCommanderPanel();
		}
		if (!g_DebugMan.IsPhotoModeHidingHUD()) {
			DrawBattleReport();
		}
		// (Also while you play a unit: the enemy's strikes coming in, and the squad following you with its keys.)
		DrawEnemyStrikes();
		DrawSquad();
		DrawAISquads();
		DrawAlerts();
		DrawHotSeat();
		if (!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt) {
			if (s_Possessed) {
				SquadKeys();
			} else if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
				// Space: the latest alert. Shift+Space: the one before, each press.
				JumpToAlert(ImGui::GetIO().KeyShift);
			}
		}
	}
	if (!s_Open && !hiddenButAbove) {
		// The selection's arrows come off while the window is away (they stayed on the units of a game with the window shut, till it was
		// opened again); the selection itself is kept for when it is.
		UnmarkSelection();
		static const bool hidePanels = std::getenv("CCCP_HIDE_PANELS") != nullptr;
		if (s_FreeCameraStarted && IsGodMode() && hidePanels) {
			// Automated test runs keep the window shut, but the camera they've placed has to stay where they put it.
			UpdateFreeCamera();
			return;
		}
		s_FreeCameraStarted = false;
		return;
	}
	if (InGame() && !s_CatalogueBuilt) {
		BuildCatalogue();
	}
	UpdateFreeCamera();
	if (InGame()) {
		DrawRallyPoints();
		DrawRespawnPoint();
		DrawSpawnOrders();
		DrawBattleMarks();
		DrawHoldZones();
		if (IsBattleCommand()) {
			DrawLandingSpot();
			DrawFogGhosts();
			DrawPowerTarget();
			DrawVeterans();
			DrawMorale();
		}
	}

	ImGuiIO& io = ImGui::GetIO();
	// Test runs can't move the real pointer (someone may be using the computer), so they say where it is to be taken to be: CCCP_TEST_POINTER=x,y in window pixels.
	static const char* testPointer = std::getenv("CCCP_TEST_POINTER");
	if (testPointer) {
		float x = 0.0F;
		float y = 0.0F;
		if (std::sscanf(testPointer, "%f,%f", &x, &y) == 2) {
			io.MousePos = ImVec2(x, y);
			io.WantCaptureMouse = false;
			static const bool testRing = std::getenv("CCCP_TEST_RING") != nullptr;
			if (testRing && !s_RingOpen) {
				s_RingOpen = true;
				s_RingCenter = ImVec2(x - 40.0F, y - 10.0F);
			}
		}
	}
	// U: the bar along the bottom hidden or shown again, in the god view, so long as no text box has the keys.
	if (IsGodMode() && InGame() && !s_Possessed && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_U, false)) {
		s_BarShown = !s_BarShown;
	}
	// E: the plant brush in hand's next picture (Shift: the one before), held down to run through them; F: the next one flipped. In the god
	// view, so long as no text box has the keys.
	if (InGame() && !s_Possessed && IsPlantBrush(CurrentTool().Kind) && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt) {
		if (ImGui::IsKeyPressed(ImGuiKey_E)) {
			StepNextPlant(CurrentTool().Kind, io.KeyShift ? -1 : 1);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			FlipNextPlant(CurrentTool().Kind);
		}
	}
	// [ and ]: the brush in hand smaller or bigger, a tenth at a time, held down to keep going (Ctrl+wheel over the world does the same: see
	// TakeBrushWheel). So long as no text box has the keys.
	if (InGame() && !s_Possessed && IsPaintTool(CurrentTool().Kind) && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt) {
		if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
			StepBrushSize(-1);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
			StepBrushSize(1);
		}
	}
	// Enter: done with the Battle tab's defence point, drop line or spawn zone tool, which goes back to the one in hand before (PutDownBattleTool). Not
	// part way through a drag, so the line being drawn isn't lost.
	// With a spawn zone part drawn, Enter closes it instead (three corners or more; fewer are dropped), and Backspace takes back its last
	// corner.
	if (InGame() && !io.WantTextInput && !s_Dragging && IsBattleTool(CurrentTool().Kind) && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
		if (CurrentTool().Kind == Tool::BattleSpawnZone && !s_ZoneDraft.empty()) {
			if (CloseSpawnZone(s_ZoneDraft, s_BattleSetup[std::clamp(s_BattleEditTeam, 0, c_Sides - 1)])) {
				SendBattleSettings(s_BattleEditTeam);
			}
		} else if (IsModeZoneTool(CurrentTool().Kind) && !s_ZoneDraft.empty()) {
			CloseModeBase();
		} else {
			PutDownBattleTool();
		}
	}
	if (InGame() && !io.WantTextInput && (CurrentTool().Kind == Tool::BattleSpawnZone || IsModeZoneTool(CurrentTool().Kind)) && !s_ZoneDraft.empty() && ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
		s_ZoneDraft.pop_back();
	}
	// Copy area (S-4): Ctrl+C copies the marked box, Ctrl+V takes up the stamp of what was copied (the Copy area tool with it), and F
	// flips the stamp. In the god view, so long as no text box has the keys. (Escape: see TakeEscape.)
	if (InGame() && !s_Possessed && !io.WantTextInput && !io.KeyAlt) {
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && CurrentTool().Kind == Tool::CopyArea && s_CopyBoxSet) {
			CopyMarkedBox();
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !s_Clipboard.Empty()) {
			TakeUpStamp();
		}
		if (!io.KeyCtrl && s_Stamping && CurrentTool().Kind == Tool::CopyArea && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			s_StampFlipped = !s_StampFlipped;
		}
	}
	// (Another tool taken up puts the stamp away.)
	if (s_Stamping && (CurrentTool().Kind != Tool::CopyArea || !InGame())) {
		s_Stamping = false;
	}
	// Ctrl+Z: the newest paint stroke or placing click undone (see UndoPaint), whichever tool is in hand, so long as no text box has the keys.
	// Not while you play a unit: in the WASD layouts Ctrl is crouch, so crouching with Z down took back the last stroke.
	if (InGame() && io.KeyCtrl && !io.KeyShift && !io.WantTextInput && !s_Possessed && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
		if (s_PaintUndo.empty()) {
			Notices::Post("Nothing to undo");
		} else {
			QueueSimChange(Tool::UndoTerrain);
			// (A rope part put down is taken away whole, so the next click starts a new one.)
			s_RopeDraft.clear();
		}
	}
	// Ctrl+Y, or Ctrl+Shift+Z: what Ctrl+Z took back made again (see RedoPaint), the same way.
	if (InGame() && io.KeyCtrl && !io.WantTextInput && !s_Possessed && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))) && !s_PaintRedo.empty()) {
		QueueSimChange(Tool::RedoTerrain);
	}
	// The rope being put down is finished with Enter or Escape, a right click (not a drag, which moves the view), or another tool taken up.
	if (!s_RopeDraft.empty()) {
		bool finish = !InGame() || CurrentTool().Kind != Tool::Rope;
		if (!io.WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
			finish = true;
		}
		if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			s_RopeRightDown = true;
			s_RopeRightStart = io.MousePos;
		}
		if (s_RopeRightDown && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			s_RopeRightDown = false;
			finish = finish || std::abs(io.MousePos.x - s_RopeRightStart.x) + std::abs(io.MousePos.y - s_RopeRightStart.y) < 6.0F;
		}
		if (finish) {
			Stroke stroke;
			stroke.Kind = Tool::Rope;
			stroke.Choice = 1;
			s_Queue.push_back(stroke);
			s_RopeDraft.clear();
			s_RopeRightDown = false;
		}
	}
	// - and =: the game a step slower or faster through the speed tile's speeds, Shift with either back to 1x (B-5). Above the world, so long
	// as no text box has the keys; not while you play a unit.
	if ((IsGodMode() || s_Commander) && InGame() && !s_Possessed && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt) {
		const bool slower = ImGui::IsKeyPressed(ImGuiKey_Minus, false);
		const bool faster = ImGui::IsKeyPressed(ImGuiKey_Equal, false);
		if (slower || faster) {
			StepGameSpeed(io.KeyShift ? 0 : (faster ? 1 : -1));
		}
	}
	// Ctrl+Shift and 1 to 4 keeps the view, Ctrl+Alt and the number goes back to it (B-10). Whichever tool is in hand.
	if ((IsGodMode() || s_Commander) && InGame()) {
		CameraBookmarkKeys();
	}
	// Control groups: Ctrl and a number keeps the selection under it, Shift and a number adds it to the group (B-3), the number alone brings
	// it back, and the number again straight after looks at them (RC-6); Ctrl+A takes the whole side. With the command tool in hand, wherever
	// the pointer is, so long as no text box has the keys. (Only while the pointer was over the world, as these were, they did nothing with it
	// resting on the window.) Not while you play a unit: those keys are its own then (its weapons, and Ctrl+A as A). Alt and a number is the
	// Commander Toolbar's (B-1), and Ctrl+Shift or Ctrl+Alt and a number the camera bookmarks' (B-10), so none of those touch the groups.
	if (InGame() && CurrentTool().Kind == Tool::Command && !io.WantTextInput && !s_Possessed) {
		static int lastNumber = -1;
		static double lastNumberTime = -10.0;
		for (int number = 0; number < 10; ++number) {
			if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + number), false) && !io.KeyAlt && !(io.KeyCtrl && io.KeyShift)) {
				if (io.KeyCtrl) {
					s_Groups[number] = s_Selected;
				} else if (io.KeyShift) {
					// Added to the group, each unit once.
					for (const UnitRef& ref: s_Selected) {
						if (std::none_of(s_Groups[number].begin(), s_Groups[number].end(), [&ref](const UnitRef& member) { return member.ID == ref.ID; })) {
							s_Groups[number].push_back(ref);
						}
					}
				} else if (!s_Groups[number].empty()) {
					s_Selected = s_Groups[number];
					if (number == lastNumber && ImGui::GetTime() - lastNumberTime < 0.4) {
						LookAtUnits(s_Selected);
					}
					lastNumber = number;
					lastNumberTime = ImGui::GetTime();
				}
			}
		}
		if (!io.KeyCtrl && !io.KeyAlt) {
			CommandHotkeys();
		}
		// Delete: the selected units taken away, as the command strip's Delete (S-8), with the sandbox's bar up. Decided on the frame after the
		// key, so the battle editor and the effects overlay, which take it for what is under the pointer, have had it first (s_DeleteKeyTakenFrame).
		{
			static int deletePressed = -1;
			const int frame = ImGui::GetFrameCount();
			if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
				deletePressed = frame;
			} else if (deletePressed >= 0 && frame > deletePressed) {
				if (frame == deletePressed + 1 && s_DeleteKeyTakenFrame != deletePressed && !s_CommanderBar && IsGodMode() && !s_Selected.empty()) {
					QueueSelectionAction(SelectionDo::Remove);
				}
				deletePressed = -1;
			}
		}
		if (io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Period, false)) {
			// Every idle unit of the side at once (B-2); with Shift, added to the selection.
			SelectAllIdle(io.KeyShift);
		}
		if (io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
			// Every unit of the kinds selected on the whole map, not only in view (B-4).
			SelectSameKinds(true);
		}
		if (io.KeyCtrl && !io.KeyAlt && !SelectionOnlyVehicles() && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
			// Breach: the clicks to come dig through to the point, then attack.
			s_CommandMode = CommandMode::Breach;
		}
		if (io.KeyCtrl && !io.KeyAlt && !SelectionOnlyVehicles() && ImGui::IsKeyPressed(ImGuiKey_B, false)) {
			// Build: the clicks to come put up the piece picked.
			s_CommandMode = CommandMode::Build;
		}
		if (io.KeyCtrl && !io.KeyAlt && !SelectionOnlyVehicles() && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
			// Airlift: the click to come is where they're flown.
			s_CommandMode = CommandMode::Airlift;
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
			// Everyone on the selection's side.
			int team = SelectionTeam();
			s_Selected.clear();
			for (Actor* actor: SandboxAccess::Actors()) {
				if (IsSelectable(actor) && actor->GetTeam() == team) {
					s_Selected.push_back(MakeRef(actor));
				}
			}
		}
	}
	// Paint or spawn with the left mouse button on the world.
	if (CapturesWorldClicks()) {
		const ToolInfo& tool = CurrentTool();
		Vector position = MouseScenePosition();
		// Alt+click with a Paint or Spawn tool in hand: the eyedropper, what is under the pointer taken up (Eyedrop). The button held
		// after it paints and places nothing until let go. (Alt+drag is the Command tool's facing, which isn't one of these.)
		if (io.KeyAlt && !s_Possessed && CanEyedrop(tool.Kind) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			Eyedrop(position);
			s_EyedropHeld = true;
		}
		if (s_EyedropHeld && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			s_EyedropHeld = false;
		}
		// A tool its simulation is off for does nothing (it's greyed in the window): a click with it says why, rather than nothing happening.
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (const char* unavailable = ToolUnavailableReason(tool.Kind)) {
				Notices::Post(unavailable);
			}
		}
		// With the battle panel showing, a click on one of the battle's zones or points selects and moves it (BattleEditMouse) rather than
		// giving an order.
		const bool battleEdit = !s_EyedropHeld && tool.Kind == Tool::Command && BattleEditMouse();
		if (battleEdit || s_EyedropHeld) {
			// (Taken: no order, box or stroke from this click.)
		} else if (tool.Kind == Tool::Command) {
			// Drag a box to select units; click the ground to send them there, or an enemy to attack it. A click on a "no route" marker
			// (RC-7) sends its units there again instead.
			if (const NoRoute* marker = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ? NoRouteAt(io.MousePos) : nullptr) {
				Stroke stroke;
				stroke.Kind = Tool::Command;
				stroke.Position = marker->Destination;
				stroke.Count = 40;
				s_Queue.push_back(stroke);
			} else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s_Dragging = true;
				s_DragStart = io.MousePos;
				s_DoubleClick = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				s_ClickCount = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
			}
		} else if (tool.Kind == Tool::PowerTarget) {
			// Battle Command: where a commander power strikes.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				PowerTargetClicked(position);
			}
		} else if (tool.Kind == Tool::LandingSpot) {
			// Battle Command: where reinforcements come down.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				LandingSpotClicked(position);
			}
		} else if (tool.Kind == Tool::BattleRally) {
			// Battle Command: where your new units make for.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				RallySpotClicked(position);
			}
		} else if (tool.Kind == Tool::BattleDropLine) {
			// Drag along where the team's ships are to come in.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s_Dragging = true;
				s_DragStart = io.MousePos;
				s_DoubleClick = false;
				s_ClickCount = 0;
			}
		} else if (DragsLine(tool.Kind)) {
			// A lift or moving platform: a drag from where its deck is first stood on to where it runs to lays its track (below); a click
			// puts the piece down as it comes. A tunnel, wall, floor or ladder: a drag lays a run of it from the piece a click there puts down.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s_LineDragging = true;
				s_LineStart = DragsRun(tool.Kind) ? position : SnapLinePoint(position, s_SnapToGrid);
			}
		} else if (tool.Kind == Tool::CopyArea && s_Stamping) {
			// Copy area's stamp: a click puts the clipboard down just where it is shown (DrawCopyArea), and the stamp stays for more.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !s_Clipboard.Empty()) {
				Stroke stroke;
				stroke.Kind = Tool::PasteArea;
				stroke.Position = StampCorner();
				stroke.Choice = s_StampFlipped ? 1 : 0;
				stroke.Count = s_PasteAir ? 1 : 0;
				s_Queue.push_back(stroke);
			}
		} else if (tool.Kind == Tool::HoldGround) {
			// (Its clicks and drags: HoldGroundInput, below.)
		} else if (DragsShape(tool.Kind)) {
			// Brush type Shape: a drag marks out the shape, filled when the button is let go (below). "Make it fall" and Copy area drag their box
			// the same way.
			if (!s_ShapeDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				s_ShapeDragging = true;
				s_ShapeDigging = false;
				s_ShapeStart = position;
			}
		} else if (tool.Interval <= 0.0F) {
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				QueueStroke(tool.Kind, position);
			}
		} else if (IsPlantBrush(tool.Kind)) {
			// A plant where clicked, then another each Plant spacing the pointer goes across while held: a row along the ground.
			// (Trees at least about a tree's width apart, so a drag doesn't pile them into one.)
			float spacing = tool.Kind == Tool::Trees ? std::max(static_cast<float>(s_PlantSpacing), 40.0F * s_PlantScale) : (tool.Kind == Tool::Candles ? std::max(static_cast<float>(s_PlantSpacing), 12.0F * std::max(std::round(s_PlantScale), 1.0F)) : static_cast<float>(s_PlantSpacing));
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || (ImGui::IsMouseDown(ImGuiMouseButton_Left) && std::abs(g_SceneMan.ShortestDistance(Vector(s_LastPlantX, position.m_Y), position, g_SceneMan.SceneWrapsX()).m_X) >= spacing)) {
				s_LastPlantX = position.m_X;
				QueueStroke(tool.Kind, position);
			}
		} else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			s_StrokeTimer -= io.DeltaTime;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || s_StrokeTimer <= 0.0F) {
				// (The Flow slider: the pouring brushes pour less often.)
				s_StrokeTimer = PoursLiquid(tool.Kind) ? tool.Interval / std::clamp(s_Flow, 0.1F, 1.0F) : tool.Interval;
				QueueStroke(tool.Kind, position);
			}
		}
		// With a Paint tool in hand the right button always digs, whatever material or brush is picked (the middle button and WASD move
		// the view). Not while you play a unit: the right button is its own then. With a terrain brush and Brush type Shape, a right drag
		// marks out the shape to dig, dug out when the button is let go (below).
		if (IsPaintTool(tool.Kind) && !s_Possessed && IsTerrainBrush(tool.Kind) && DragsShape(tool.Kind)) {
			if (!s_ShapeDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				s_ShapeDragging = true;
				s_ShapeDigging = true;
				s_ShapeStart = position;
			}
		} else if (IsPaintTool(tool.Kind) && !s_Possessed && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			s_DigTimer -= io.DeltaTime;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || s_DigTimer <= 0.0F) {
				s_DigTimer = c_Tools[ToolIndex(Tool::Dig)].Interval;
				QueueStroke(Tool::Dig, position);
			}
		}
		if (tool.Kind == Tool::Rope && !s_RopeDraft.empty()) {
			// The rope's next stretch, from its last point to the pointer, in its colour, with how long it is.
			const RopeSim::TypeInfo& type = RopeSim::GetType(s_RopeType);
			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			ImVec2 from = ToScreen(s_RopeDraft.back());
			ImU32 color = IM_COL32(type.R, type.G, type.B, 230);
			drawList->AddLine(from, io.MousePos, color, 2.0F);
			drawList->AddCircleFilled(from, 3.0F, color);
			float length = g_SceneMan.ShortestDistance(s_RopeDraft.back(), position, g_SceneMan.SceneWrapsX()).GetMagnitude() * (1.0F + s_RopeSlack);
			std::string label = std::to_string(static_cast<int>(length)) + " px    right click or Enter: done";
			drawList->AddText(ImVec2(io.MousePos.x + 14.0F, io.MousePos.y + 10.0F), color, label.c_str());
		}
		if (!battleEdit) {
			DrawSideRing();
		}
		if (!s_RingOpen) {
			DrawCursor();
		}
		// A word by the pointer for a moment: the brush's new size, or what the eyedropper picked. It fades over its last quarter second.
		if (const double age = ImGui::GetTime() - s_PointerNoteAt; age < 1.2 && !s_PointerNote.empty()) {
			const int alpha = static_cast<int>(255.0 * std::clamp((1.2 - age) / 0.25, 0.0, 1.0));
			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			ImVec2 at(io.MousePos.x + 16.0F, io.MousePos.y - ImGui::GetTextLineHeight() - 14.0F);
			ImVec2 size = ImGui::CalcTextSize(s_PointerNote.c_str());
			drawList->AddRectFilled(ImVec2(at.x - 4.0F, at.y - 2.0F), ImVec2(at.x + size.x + 4.0F, at.y + size.y + 2.0F), IM_COL32(0, 0, 0, alpha * 150 / 255), 3.0F);
			drawList->AddText(at, IM_COL32(255, 255, 255, alpha), s_PointerNote.c_str());
		}
	} else {
		s_RingOpen = false;
	}
	if (s_LineDragging) {
		// A lift's or moving platform's track being dragged out: any way (Shift: the nearest of the 8 ways), drawn as its deck at each
		// stop and the line between; Escape drops it. Let go, it is put down; a drag too short to be a track is a click.
		const Preset* preset = ChosenPreset(Tool::Structure, s_StructureChoice);
		if (!InGame() || !DragsLine(CurrentTool().Kind) || !preset || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			s_LineDragging = false;
		} else if (DragsRun(CurrentTool().Kind)) {
			// A bunker run being dragged out: any way, slanted to it (Shift: the nearest of the 8 ways, never straight across for a piece that
			// only runs up and down, or the other way round). Its pieces are drawn see-through where they will go, each slanted as it will be;
			// let go, it is put down.
			Vector end = LineDragEnd(s_LineStart, MouseScenePosition(), io.KeyShift, false, RunLock(*preset));
			bool run = (end - s_LineStart).MagnitudeIsGreaterThan(c_MinLineDrag);
			if (run) {
				RunLayout layout = LayOutRun(*preset, s_LineStart, end, s_SnapToGrid);
				ImDrawList* drawList = ImGui::GetForegroundDrawList();
				GameViewRect view = g_WindowMan.GetGameViewRect();
				drawList->PushClipRect(ImVec2(view.x, view.y), ImVec2(view.x + view.w, view.y + view.h));
				const ImU32 outline = IM_COL32(255, 220, 120, 200);
				for (const RunTile& tile: layout.Tiles) {
					const PiecePicture& picture = PictureOf(*tile.Piece);
					// Its corners, slanted: along and across its own length and thickness.
					const float thickness = static_cast<float>(layout.UpDown ? tile.Piece->Width : tile.Piece->Height);
					ImVec2 corners[4] = {ToScreen(layout.At(tile.Start, 0.0F)), ToScreen(layout.At(tile.Start + tile.Length, 0.0F)), ToScreen(layout.At(tile.Start + tile.Length, thickness)), ToScreen(layout.At(tile.Start, thickness))};
					if (layout.UpDown) {
						// Along is down the picture: top left, bottom left, bottom right, top right.
						std::swap(corners[1], corners[3]);
					}
					if (picture.Texture) {
						drawList->AddImageQuad(static_cast<ImTextureID>(picture.Texture), corners[0], corners[1], corners[2], corners[3], ImVec2(0.0F, 0.0F), ImVec2(1.0F, 0.0F), ImVec2(1.0F, 1.0F), ImVec2(0.0F, 1.0F), IM_COL32(255, 255, 255, 170));
					}
					drawList->AddPolyline(corners, 4, outline, ImDrawFlags_Closed, 1.0F);
				}
				drawList->PopClipRect();
				const int pieces = static_cast<int>(layout.Tiles.size());
				std::string label = std::to_string(pieces) + (pieces == 1 ? " piece" : " pieces") + (io.KeyShift ? "" : "    Shift: 8 ways");
				drawList->AddText(ImVec2(io.MousePos.x + 14.0F, io.MousePos.y + 10.0F), outline, label.c_str());
			}
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				s_LineDragging = false;
				QueueStroke(Tool::Structure, s_LineStart);
				if (run && !s_Queue.empty()) {
					s_Queue.back().Position2 = end;
				}
			}
		} else {
			Vector end = LineDragEnd(s_LineStart, MouseScenePosition(), io.KeyShift, s_SnapToGrid);
			Vector along = end - s_LineStart;
			bool track = along.MagnitudeIsGreaterThan(c_MinLineDrag);
			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			ImU32 color = IM_COL32(255, 220, 120, 230);
			if (track) {
				drawList->AddLine(ToScreen(s_LineStart), ToScreen(end), color, 1.5F);
				const int stops = TrackStopCount(*preset);
				const float half = TrackDeckWidth(*preset) * 0.5F;
				for (int i = 0; i < stops; ++i) {
					Vector stand = s_LineStart + along * (static_cast<float>(i) / static_cast<float>(stops - 1));
					drawList->AddRect(ToScreen(stand - Vector(half, 0.0F)), ToScreen(stand + Vector(half, 8.0F)), color, 0.0F, 0, 1.5F);
				}
				std::string label = std::to_string(static_cast<int>(along.GetMagnitude())) + " px" + (io.KeyShift ? "" : "    Shift: 8 ways");
				drawList->AddText(ImVec2(io.MousePos.x + 14.0F, io.MousePos.y + 10.0F), color, label.c_str());
			}
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				s_LineDragging = false;
				QueueStroke(Tool::Structure, s_LineStart);
				if (track && !s_Queue.empty()) {
					s_Queue.back().Position2 = end;
				}
			}
		}
	}
	if (s_ShapeDragging) {
		// The shape being dragged out with a terrain brush (Brush type Shape), drawn as it will be filled (dug, with the right button: in
		// orange); Shift keeps it as wide as it is tall (a line: to the 8 ways), Escape drops it.
		if (!InGame() || !DragsShape(CurrentTool().Kind) || (s_ShapeDigging && (s_Possessed || !IsTerrainBrush(CurrentTool().Kind))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			s_ShapeDragging = false;
		} else {
			bool dropBox = CurrentTool().Kind == Tool::CollapseArea;
			bool copyBox = CurrentTool().Kind == Tool::CopyArea;
			bool line = !dropBox && !copyBox && s_FillShape == FillShape::Line;
			Vector end = s_ShapeStart + g_SceneMan.ShortestDistance(s_ShapeStart, MouseScenePosition(), g_SceneMan.SceneWrapsX());
			if (line) {
				end = LineDragEnd(s_ShapeStart, MouseScenePosition(), io.KeyShift, false);
			} else if (io.KeyShift) {
				float side = std::max(std::abs(end.m_X - s_ShapeStart.m_X), std::abs(end.m_Y - s_ShapeStart.m_Y));
				end = s_ShapeStart + Vector(end.m_X >= s_ShapeStart.m_X ? side : -side, end.m_Y >= s_ShapeStart.m_Y ? side : -side);
			}
			if (dropBox || copyBox) {
				// (No bigger than c_MaxDropSide, or c_MaxCopySide, either way.)
				const float c_Most = static_cast<float>((copyBox ? c_MaxCopySide : c_MaxDropSide) - 1);
				end = s_ShapeStart + Vector(std::clamp(end.m_X - s_ShapeStart.m_X, -c_Most, c_Most), std::clamp(end.m_Y - s_ShapeStart.m_Y, -c_Most, c_Most));
			}
			ImDrawList* drawList = ImGui::GetForegroundDrawList();
			ImU32 outline = s_ShapeDigging ? IM_COL32(242, 150, 60, 235) : IM_COL32(255, 255, 255, 220);
			float left = std::min(s_ShapeStart.m_X, end.m_X);
			float right = std::max(s_ShapeStart.m_X, end.m_X) + 1.0F;
			float top = std::min(s_ShapeStart.m_Y, end.m_Y);
			float bottom = std::max(s_ShapeStart.m_Y, end.m_Y) + 1.0F;
			float middleX = (left + right) * 0.5F;
			if (copyBox) {
				// The box to copy: shaded, edged in blue, with its size.
				ImVec2 topLeft = ToScreen(Vector(left, top));
				ImVec2 bottomRight = ToScreen(Vector(right, bottom));
				drawList->AddRectFilled(topLeft, bottomRight, IM_COL32(110, 180, 250, 40));
				drawList->AddRect(topLeft, bottomRight, IM_COL32(110, 180, 250, 235), 0.0F, 0, 2.0F);
				std::string size = std::to_string(static_cast<int>(right - left)) + " x " + std::to_string(static_cast<int>(bottom - top));
				drawList->AddText(ImVec2(topLeft.x + 4.0F, topLeft.y - ImGui::GetTextLineHeight() - 2.0F), IM_COL32(110, 180, 250, 235), size.c_str());
			} else if (dropBox) {
				// The box whose ground will fall: shaded, edged in warning orange, with its size.
				ImVec2 topLeft = ToScreen(Vector(left, top));
				ImVec2 bottomRight = ToScreen(Vector(right, bottom));
				drawList->AddRectFilled(topLeft, bottomRight, IM_COL32(242, 150, 60, 50));
				drawList->AddRect(topLeft, bottomRight, IM_COL32(242, 150, 60, 235), 0.0F, 0, 2.0F);
				std::string size = std::to_string(static_cast<int>(right - left)) + " x " + std::to_string(static_cast<int>(bottom - top));
				drawList->AddText(ImVec2(topLeft.x + 4.0F, topLeft.y - ImGui::GetTextLineHeight() - 2.0F), IM_COL32(242, 150, 60, 235), size.c_str());
			} else if (line) {
				// The line as wide as it will be, its ends rounded, with how long it is and at what angle.
				Vector along = end - s_ShapeStart;
				float length = along.GetMagnitude();
				float half = std::max(static_cast<float>(s_LineWidth) * 0.5F, 0.75F);
				float heading = length > 0.0F ? std::atan2(along.m_Y, along.m_X) : 0.0F;
				constexpr int c_CapPoints = 16;
				ImVec2 points[c_CapPoints * 2];
				for (int i = 0; i < c_CapPoints; ++i) {
					// Round the far end from one side to the other, then back round the near end.
					float turn = heading - c_HalfPI + c_PI * static_cast<float>(i) / static_cast<float>(c_CapPoints - 1);
					points[i] = ToScreen(end + Vector(std::cos(turn), std::sin(turn)) * half);
					points[c_CapPoints + i] = ToScreen(s_ShapeStart - Vector(std::cos(turn), std::sin(turn)) * half);
				}
				drawList->AddPolyline(points, c_CapPoints * 2, outline, ImDrawFlags_Closed, 1.5F);
				int degrees = (static_cast<int>(std::lround(-heading * 180.0F / c_PI)) + 360) % 360;
				std::string label = std::to_string(static_cast<int>(length)) + " px  " + std::to_string(degrees) + " deg  " + std::to_string(s_LineWidth) + " wide" + (io.KeyShift ? "" : "    Shift: 8 ways");
				drawList->AddText(ImVec2(io.MousePos.x + 14.0F, io.MousePos.y + 10.0F), outline, label.c_str());
			} else if (s_FillShape == FillShape::Circle) {
				constexpr int c_Points = 48;
				ImVec2 points[c_Points];
				for (int i = 0; i < c_Points; ++i) {
					float turn = static_cast<float>(i) / static_cast<float>(c_Points) * 6.2831853F;
					points[i] = ToScreen(Vector(middleX + std::cos(turn) * (right - left) * 0.5F, (top + bottom) * 0.5F + std::sin(turn) * (bottom - top) * 0.5F));
				}
				drawList->AddPolyline(points, c_Points, outline, ImDrawFlags_Closed, 1.5F);
			} else if (s_FillShape == FillShape::Triangle) {
				bool pointUp = end.m_Y >= s_ShapeStart.m_Y;
				ImVec2 point = ToScreen(Vector(middleX, pointUp ? top : bottom));
				ImVec2 baseLeft = ToScreen(Vector(left, pointUp ? bottom : top));
				ImVec2 baseRight = ToScreen(Vector(right, pointUp ? bottom : top));
				drawList->AddTriangle(point, baseLeft, baseRight, outline, 1.5F);
			} else {
				drawList->AddRect(ToScreen(Vector(left, top)), ToScreen(Vector(right, bottom)), outline, 0.0F, 0, 1.5F);
			}
			if (copyBox && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				// Copy area: the box is only marked, for Ctrl+C (or the Copy button) to copy. A click without a drag unmarks it.
				s_ShapeDragging = false;
				s_CopyBoxSet = right - left >= 2.0F && bottom - top >= 2.0F;
				s_CopyBoxA = s_ShapeStart;
				s_CopyBoxB = end;
			} else if (!ImGui::IsMouseDown(s_ShapeDigging ? ImGuiMouseButton_Right : ImGuiMouseButton_Left)) {
				s_ShapeDragging = false;
				Stroke stroke;
				stroke.Kind = s_ShapeDigging ? Tool::Dig : CurrentTool().Kind;
				stroke.Position = s_ShapeStart;
				stroke.Position2 = end;
				stroke.Fill = static_cast<int>(s_FillShape);
				stroke.Radius = s_LineWidth;
				stroke.Over = CurrentPaintOver();
				if (stroke.Kind == Tool::TerrainOther) {
					stroke.Material = s_OtherTerrain;
				} else if (stroke.Kind == Tool::Metal) {
					stroke.Material = s_PaintMetal;
				}
				NoteRecent(stroke.Kind, stroke.Material);
				s_Queue.push_back(stroke);
			}
		}
	}
	HoldGroundInput();
	// A one-off order's mode back to Command once it's placed, when no second click of a double click can follow.
	if (s_BackToCommandAt >= 0.0 && !s_Dragging && ImGui::GetTime() >= s_BackToCommandAt) {
		if (s_CommandMode == s_BackToCommandFrom) {
			s_CommandMode = CommandMode::Command;
		}
		s_BackToCommandAt = -1.0;
	}
	if (s_Dragging) {
		ImVec2 now = io.MousePos;
		// Defend at (RC-4), and a move or attack-move with Alt held (RC-5), drag the way to face, drawn as an arrow; anything else drags a box
		// to select.
		bool defendAt = s_CommandMode == CommandMode::DefendAt && CurrentTool().Kind == Tool::Command;
		// (Command mode is as Move where it would move, at the place the drag began; as Select with nothing selected.)
		const bool commandTool = CurrentTool().Kind == Tool::Command;
		CommandTarget::Kind commandAt = CommandTarget::Select;
		if (commandTool && s_CommandMode == CommandMode::Command) {
			Vector start = g_CameraMan.GetOffset(0) + Vector(s_DragStart.x - ViewOrigin().x, s_DragStart.y - ViewOrigin().y) * ScenePixelsPerWindowPixel();
			g_SceneMan.WrapPosition(start);
			commandAt = CommandTargetAt(start).What;
			// (Setting a spawn order, with none selected: as a move, so a double click drags its risk.)
			if (s_SpawnOrderArmed && commandAt == CommandTarget::Select) {
				commandAt = CommandTarget::Move;
			}
		}
		const bool commandMove = s_CommandMode == CommandMode::Command && commandAt == CommandTarget::Move;
		bool facingMove = (s_CommandMode == CommandMode::Move || s_CommandMode == CommandMode::AttackMove || commandMove) && io.KeyAlt && commandTool;
		bool dropLine = CurrentTool().Kind == Tool::BattleDropLine;
		// A double click held and dragged sets how much risk the order takes, from normal at the click to full at the risk distance, and the
		// way dragged is the way they face when they get there. It takes over from the box, the facing arrow and the patrol clicks.
		bool riskDrag = commandTool && s_ClickCount >= 2 && s_CommandMode != CommandMode::Select && s_CommandMode != CommandMode::Patrol && s_CommandMode != CommandMode::Pen && !(s_CommandMode == CommandMode::Command && commandAt == CommandTarget::Select);
		// Pen animals: a box dragged out is the pen (a click is a corner of one).
		bool penBox = commandTool && s_CommandMode == CommandMode::Pen;
		float riskPixels = std::max(1.0F, g_SettingsMan.DirectOrders().RiskDragPixels);
		float dragLength = std::sqrt((now.x - s_DragStart.x) * (now.x - s_DragStart.x) + (now.y - s_DragStart.y) * (now.y - s_DragStart.y));
		// (On a curve: slow at first and steep at the end, so a long drag is about 70% and full risk, a suicide mission, takes the whole of it.)
		float risk = std::pow(std::clamp(dragLength / riskPixels, 0.0F, 1.0F), 1.6F);
		const bool suicide = risk >= g_SettingsMan.DirectOrders().StraightFlightRisk;
		bool riskDragging = riskDrag && std::abs(now.x - s_DragStart.x) + std::abs(now.y - s_DragStart.y) > 8.0F;
		if (riskDragging) {
			ImDrawList* draw = ImGui::GetForegroundDrawList();
			ImU32 color = suicide ? IM_COL32(255, 40, 40, 255) : IM_COL32(static_cast<int>(90.0F + 165.0F * risk), static_cast<int>(200.0F - 150.0F * risk), static_cast<int>(255.0F - 205.0F * risk), 230);
			draw->AddCircle(s_DragStart, riskPixels, IM_COL32(255, 255, 255, 60), 64, 1.0F);
			draw->AddLine(s_DragStart, now, color, 2.0F + 3.0F * risk);
			if (dragLength > 1.0F) {
				ImVec2 dir((now.x - s_DragStart.x) / dragLength, (now.y - s_DragStart.y) / dragLength);
				ImVec2 normal(-dir.y, dir.x);
				draw->AddTriangleFilled(ImVec2(now.x + dir.x * 10.0F, now.y + dir.y * 10.0F), ImVec2(now.x + normal.x * 6.0F, now.y + normal.y * 6.0F), ImVec2(now.x - normal.x * 6.0F, now.y - normal.y * 6.0F), color);
			}
			char label[32];
			std::snprintf(label, sizeof(label), suicide ? "Suicide %d%%" : "Risk %d%%", static_cast<int>(std::round(risk * 100.0F)));
			draw->AddText(ImVec2(now.x + 14.0F, now.y - 22.0F), color, label);
		} else if (dropLine) {
			ImGui::GetForegroundDrawList()->AddLine(s_DragStart, now, c_SideColors[std::clamp(s_BattleEditTeam, 0, c_Sides - 1)], 3.0F);
		} else if (penBox) {
			ImGui::GetForegroundDrawList()->AddRect(ImVec2(std::min(s_DragStart.x, now.x), std::min(s_DragStart.y, now.y)), ImVec2(std::max(s_DragStart.x, now.x), std::max(s_DragStart.y, now.y)), c_CommandModeColors[static_cast<int>(CommandMode::Pen)], 0.0F, 0, 2.0F);
		} else if (defendAt || facingMove) {
			ImU32 color = c_CommandModeColors[static_cast<int>(s_CommandMode)];
			ImGui::GetForegroundDrawList()->AddLine(s_DragStart, now, color, 2.0F);
			float side = now.x >= s_DragStart.x ? 1.0F : -1.0F;
			if (std::abs(now.x - s_DragStart.x) > 12.0F) {
				ImGui::GetForegroundDrawList()->AddTriangleFilled(ImVec2(now.x + side * 8.0F, now.y), ImVec2(now.x, now.y - 6.0F), ImVec2(now.x, now.y + 6.0F), color);
			}
		} else {
			ImGui::GetForegroundDrawList()->AddRect(ImVec2(std::min(s_DragStart.x, now.x), std::min(s_DragStart.y, now.y)), ImVec2(std::max(s_DragStart.x, now.x), std::max(s_DragStart.y, now.y)), IM_COL32(255, 255, 255, 200), 0.0F, 0, 1.5F);
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			s_Dragging = false;
			Stroke stroke;
			float scale = ScenePixelsPerWindowPixel();
			Vector start = g_CameraMan.GetOffset(0) + Vector(s_DragStart.x - ViewOrigin().x, s_DragStart.y - ViewOrigin().y) * scale;
			Vector end = g_CameraMan.GetOffset(0) + Vector(now.x - ViewOrigin().x, now.y - ViewOrigin().y) * scale;
			bool dragged = std::abs(now.x - s_DragStart.x) + std::abs(now.y - s_DragStart.y) > 8.0F;
			bool give = true;
			if (dropLine) {
				// The team being set up on the Battle tab has its ships come in over this line from now on.
				BattleSettings& setup = s_BattleSetup[std::clamp(s_BattleEditTeam, 0, c_Sides - 1)];
				setup.LineA = start;
				setup.LineB = dragged ? end : start;
				setup.HasLine = true;
				setup.DropOnLine = true;
				SendBattleSettings(s_BattleEditTeam);
				give = false;
			} else if (riskDrag && dragged) {
				stroke.Kind = Tool::Command;
				stroke.Position = start;
				g_SceneMan.WrapPosition(stroke.Position);
				stroke.Position2 = end;
				stroke.HasRisk = true;
				stroke.Risk = risk;
				if (s_CommandMode == CommandMode::DefendAt) {
					stroke.Count = io.KeyShift ? 11 : 10;
				} else if (s_CommandMode == CommandMode::Move || s_CommandMode == CommandMode::AttackMove || commandMove) {
					stroke.Count = io.KeyShift ? 31 : 30;
				} else {
					stroke.Count = io.KeyShift ? 1 : 0;
				}
			} else if (defendAt) {
				stroke.Kind = Tool::Command;
				stroke.Position = start;
				g_SceneMan.WrapPosition(stroke.Position);
				stroke.Position2 = end;
				stroke.Count = io.KeyShift ? 11 : 10;
			} else if (facingMove) {
				stroke.Kind = Tool::Command;
				stroke.Position = start;
				g_SceneMan.WrapPosition(stroke.Position);
				stroke.Position2 = end;
				stroke.Count = io.KeyShift ? 31 : 30;
			} else if (s_CommandMode == CommandMode::Patrol && CurrentTool().Kind == Tool::Command && !dragged) {
				// A point of the patrol route being clicked out; the command row starts it.
				g_SceneMan.WrapPosition(end);
				s_PatrolDraft.push_back(end);
				give = false;
			} else if (penBox && !dragged) {
				// A corner of the pen being clicked out; the command row pens them.
				g_SceneMan.WrapPosition(end);
				s_PenDraft.push_back(end);
				give = false;
			} else if (penBox) {
				// A box dragged out: the pen, at once.
				Vector topLeft(std::min(start.m_X, end.m_X), std::min(start.m_Y, end.m_Y));
				Vector bottomRight(std::max(start.m_X, end.m_X), std::max(start.m_Y, end.m_Y));
				stroke.Kind = Tool::Command;
				stroke.Count = 50;
				stroke.Points = {topLeft, Vector(bottomRight.m_X, topLeft.m_Y), bottomRight, Vector(topLeft.m_X, bottomRight.m_Y)};
				for (Vector& corner: stroke.Points) {
					g_SceneMan.WrapPosition(corner);
				}
				stroke.Position = stroke.Points.front();
				s_PenDraft.clear();
			} else if (dragged) {
				stroke.Kind = Tool::Select;
				stroke.Position = start;
				stroke.Position2 = end;
			} else {
				stroke.Kind = Tool::Command;
				stroke.Position = end;
				g_SceneMan.WrapPosition(stroke.Position);
				// (Ctrl and a double click: 3, every unit of that kind on the whole map rather than in view, B-4.)
				stroke.Count = io.KeyShift ? 1 : (s_DoubleClick ? (io.KeyCtrl ? 3 : 2) : 0);
			}
			if (give) {
				// (One click places the order cautiously and two normally; a double click dragged carries its own risk.)
				stroke.Clicks = stroke.Kind == Tool::Command ? s_ClickCount : 0;
				if (stroke.Kind == Tool::Command) {
					// Command mode gives a move facing the way dragged as Move does (FacingMoveSelected).
					stroke.OrderMode = static_cast<int>(commandMove && stroke.Count >= 30 ? CommandMode::Move : s_CommandMode);
					BackToCommandAfterOrder(io.KeyShift);
				}
				// (Setting a spawn order, the order placed is that, not one for the selection.)
				if (!TakeSpawnOrder(stroke)) {
					s_Queue.push_back(stroke);
				}
			}
		}
	}
	if (InGame()) {
		DrawSelection();
		DrawPlayerSquads();
		DrawMinimap();
		if (!s_Possessed) {
			LoadoutWindow();
			DrawArmyOverview();
		}
	}

	if (hiddenButAbove) {
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(430.0F, 0.0F), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 445.0F, 40.0F), ImGuiCond_FirstUseEver);
	if (g_DebugMan.BeginPanel(IsGodMode() ? "Sandbox (F7)###Sandbox" : "Sandbox tools (F7)###Sandbox", &s_Open, DebugMan::PanelSide::Left, g_DebugMan.GetSandboxPlacement(), true)) {
		if (!InGame()) {
			g_DebugMan.DrawToolWindowControls();
			ImGui::TextWrapped("Start a game to use the sandbox. Pick \"Sandbox\" on the main menu for the full god mode.");
			g_DebugMan.EndPanel();
			return;
		}
		if (!IsGodMode()) {
			// In other games there is no bar, so the window carries the main tools itself.
			g_DebugMan.DrawToolWindowControls();
			SideStatus();
			TimeControls();
			ImGui::TextDisabled("Left click: use tool.  Right drag / WASD: move camera.  Wheel: zoom.");
			ToolButtons({Tool::None, Tool::Command, Tool::Follow, Tool::Possess});
			ToolButtons({Tool::Remove, Tool::RallyPoint});
			CommanderPanel();
		}
		// How this window is shown, at the top of it where it can be found: docked at the side, floating, or most of the screen.
		{
			int placement = static_cast<int>(g_DebugMan.GetSandboxPlacement());
			static const char* const placementNames[] = {"Side panel", "Floating", "Large"};
			static const char* const placementTips[] = {"Docked at the left of the screen; drag its edge to make it wider or narrower.", "A window to move and resize as you like.", "Nine tenths of the screen, in the middle of it."};
			ImGui::TextDisabled("View");
			for (int choice = 0; choice < 3; ++choice) {
				ImGui::SameLine();
				if (ToolUI::RadioButton(placementNames[choice], &placement, choice)) {
					g_DebugMan.SetSandboxPlacement(static_cast<DebugMan::PanelPlacement>(placement));
				}
				ImGui::SetItemTooltip("%s", placementTips[choice]);
			}
		}
		if (DrawTabRows()) {
			if (IsGodMode() && SandboxTab("You")) {
				s_CurrentTab = "You";
				bool exists = GetRef(s_PlayerUnit) || PlayerHolder();
				if (ToolUI::Checkbox("Have a character of my own", &s_Player.EnterOnClose) && !s_Player.EnterOnClose) {
					Stroke stroke;
					stroke.Kind = Tool::PlayerRemove;
					s_Queue.push_back(stroke);
				}
				ImGui::SetItemTooltip("On: Tab puts the tools away and puts you in your character.\nOff: there is no character. Tab only hides and shows the tools, and you go on looking around from above.");
				if (!s_Player.EnterOnClose) {
					ImGui::TextWrapped("No character. Tab hides and shows these tools; with them hidden the right mouse button and WASD still move the view and the wheel zooms.");
					EndSandboxTab();
				} else {
				ImGui::TextWrapped("For walking about in what you've made. P puts you in it, and P again brings you back above; Shift+Tab puts it down where the mouse points and puts you in it. Tab hides and shows these tools: with a tool in hand you stay above and go on using it, with nothing in hand (Look around) Tab puts you in your character.");
				if (ToolUI::Button(exists ? "Play (Tab)" : "Make it and play (Tab)", ImVec2(-1.0F, 0.0F))) {
					Stroke stroke;
					stroke.Kind = Tool::PlayCharacter;
					stroke.Count = 0;
					s_Queue.push_back(stroke);
				}
				ToolButtons({Tool::PlayCharacter});
				ImGui::SameLine();
				ImGui::TextDisabled("then click where to start");
				ImGui::SeparatorText("What it can do");
				ToolUI::Checkbox("Can't be hurt", &s_Player.Unkillable);
				ImGui::SameLine();
				ToolUI::Checkbox("Endless jetpack", &s_Player.EndlessJetpack);
				ToolUI::Checkbox("Endless ammunition", &s_Player.EndlessAmmo);
				ToolUI::Checkbox("1 to 9 take out that item of the kit", &s_Player.NumberKeys);
				ToolUI::Checkbox("Enemies take no notice of it", &s_Player.Neutral);
				ImGui::SetItemTooltip("On: units run by the AI don't see your character as an enemy and don't pick it as a target, whatever side it is on. Stray shots and blasts still reach it.");
				ToolUI::Checkbox("Remove it when I leave play", &s_Player.RemoveOnLeave);
				ImGui::SetItemTooltip("On: going back above takes your character away, and P makes a new one. If it is in a vehicle, it is taken out and the vehicle stays.\nOff: it stays where you left it, and P puts you back in it.");
				ImGui::SeparatorText("Respawn point");
				ImGui::TextWrapped(s_RespawnSet ? "Set. When your character dies while you play it, it is made again there." : "Not set. When your character dies, you go back to the god view.");
				if (ToolUI::Button("Set it here (Alt+P)")) {
					Sandbox::SetRespawnPoint(false);
				}
				ImGui::SetItemTooltip("Where your character stands while you play, or where the mouse points from above. It is cleared when a new scene starts.");
				ImGui::SameLine();
				ImGui::BeginDisabled(!s_RespawnSet);
				if (ToolUI::Button("Clear it")) {
					s_RespawnSet = false;
					ClearRespawn();
				}
				ImGui::EndDisabled();
				ToolUI::Checkbox("Respawn at once", &s_Player.RespawnInstant);
				ImGui::SetItemTooltip("On: back at the respawn point the moment it dies.\nOff: the view stays where it died for a few seconds first, so you can see what happens, then it respawns.");
				ToolUI::Checkbox("N flies through anything", &s_Player.FlyKey);
				ImGui::SetItemTooltip("While playing, N lifts the character out of the physics: the movement keys fly it in any direction, through the ground and walls. N again drops it back in.");
				ImGui::SeparatorText("What it is");
				for (int side = 0; side < c_Sides; ++side) {
					if (side > 0) {
						ImGui::SameLine();
					}
					ImGui::PushStyleColor(ImGuiCol_Text, c_SideColors[side]);
					ToolUI::RadioButton((std::string(c_SideNames[side]) + "##you").c_str(), &s_Player.Team, side);
					ImGui::PopStyleColor();
				}
				static char kitFilter[48] = "";
				// The base class: any unit there is, picked from the same browser as the Spawn tab's, folded away until wanted.
				bool knownBody = FindPreset(s_Units, s_Player.Body) != nullptr;
				std::string baseHeader = "Base class: " + s_Player.Body + (knownBody ? "" : "  (not in this game)") + "###baseClass";
				bool baseOpen = ImGui::CollapsingHeader(baseHeader.c_str());
				ImGui::SetItemTooltip("The unit your character is made as. Open to pick any unit, as on the Spawn tab.");
				if (baseOpen) {
					ImGui::PushID("baseClass");
					PictureGrid(Tool::Unit, nullptr, &s_Player.Body);
					ImGui::PopID();
				}
				ToolUI::Checkbox("Inherit its equipment", &s_Player.InheritKit);
				ImGui::SetItemTooltip("On: the character also carries what a unit of this kind is spawned with (its own items and its faction's guns), besides the kit below.\nOff: it carries only the kit below.");
				ImGui::SeparatorText("What it carries");
				int removeItem = -1;
				for (size_t i = 0; i < s_Player.Kit.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));
					if (ToolUI::SmallButton("x")) {
						removeItem = static_cast<int>(i);
					}
					ImGui::SameLine();
					if (ToolUI::SmallButton("^") && i > 0) {
						std::swap(s_Player.Kit[i], s_Player.Kit[i - 1]);
					}
					ImGui::SameLine();
					bool known = FindPreset(s_Items, s_Player.Kit[i]) != nullptr;
					ImGui::TextColored(known ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%d  %s%s", static_cast<int>(i) + 1, s_Player.Kit[i].c_str(), known ? "" : "  (not in this game)");
					ImGui::PopID();
				}
				if (removeItem >= 0) {
					s_Player.Kit.erase(s_Player.Kit.begin() + removeItem);
				}
				if (ImGui::BeginCombo("##addKit", "Add an item...", ImGuiComboFlags_HeightLarge)) {
					ImGui::InputTextWithHint("##kitFilter", "Search...", kitFilter, sizeof(kitFilter));
					for (const Preset& item: s_Items) {
						if (ContainsIgnoringCase(item.Label, kitFilter) && ImGui::Selectable(item.Label.c_str())) {
							s_Player.Kit.push_back(item.PresetName);
						}
					}
					ImGui::EndCombo();
				}
				if (ToolUI::Button("Usual kit")) {
					s_Player.Kit = PlayerSetup().Kit;
				}
				// Your loadouts (the Loadouts window): one taken as the character, or the character kept as one.
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55F);
				if (ImGui::BeginCombo("##fromLoadout", "From my loadouts...", ImGuiComboFlags_HeightLarge)) {
					CustomPick pick;
					if (CustomPickItems(pick, false) && pick.Kind == CustomPick::Loadout) {
						if (const CustomLoadout* loadout = FindCustomLoadout(pick.Name)) {
							s_Player.Body = loadout->Body;
							s_Player.Kit = loadout->Items;
							s_Player.InheritKit = false;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("Makes your character one of your loadouts: its unit, carrying its kit.");
				ImGui::SameLine();
				if (ToolUI::Button("Save as a loadout")) {
					LoadLoadoutsFile();
					CustomLoadout loadout;
					loadout.Body = s_Player.Body;
					loadout.Items = s_Player.Kit;
					std::string name = "My character";
					for (int number = 2; FindCustomLoadout(name); ++number) {
						name = "My character " + std::to_string(number);
					}
					loadout.Name = name;
					s_CustomLoadouts.push_back(loadout);
					SaveLoadoutsFile();
					g_ConsoleMan.PrintString("SANDBOX: Saved your character as the loadout \"" + name + "\" (the Loadouts window has it).");
				}
				ImGui::SetItemTooltip("Keeps this base class and kit as one of your loadouts, to spawn or put in squads from the Loadouts window.");
				ImGui::TextDisabled("A new base class or kit is used the next time the character is made.");
				ImGui::BeginDisabled(!exists);
				if (ToolUI::Button("Make it again now")) {
					Stroke stroke;
					stroke.Kind = Tool::PlayerRemake;
					s_Queue.push_back(stroke);
				}
				ImGui::SameLine();
				if (ToolUI::Button("Remove it")) {
					Stroke stroke;
					stroke.Kind = Tool::PlayerRemove;
					s_Queue.push_back(stroke);
				}
				ImGui::EndDisabled();
				EndSandboxTab();
				}
			}
			if (SandboxTab("Spawn")) {
				s_CurrentTab = "Spawn";
				ToolButtons({Tool::Unit, Tool::Vehicle, Tool::Drop, Tool::Brain, Tool::Item});
				if (CurrentTool().Kind == Tool::Structure) {
					s_ToolIndex = ToolIndex(Tool::Unit);
				}
				Tool kind = CurrentTool().Kind;
				if (kind == Tool::Unit || kind == Tool::Vehicle || kind == Tool::Drop || kind == Tool::Brain || kind == Tool::Item) {
					PictureGrid(kind, nullptr);
				}
				if (kind == Tool::Unit || kind == Tool::Vehicle || kind == Tool::Drop || kind == Tool::Brain || kind == Tool::Structure || kind == Tool::RallyPoint) {
					SideChooser();
				}
				if (kind == Tool::Drop) {
					ImGui::Combo("Craft", &s_Craft, "Dropship\0Rocket\0");
				}
				if (kind == Tool::Unit || kind == Tool::Drop) {
					ToolUI::Checkbox("Random units", &s_RandomUnits);
					ImGui::SetItemTooltip(kind == Tool::Drop ? "Each unit in the craft is picked at random, not the one chosen above." : "Each unit in the squad is picked at random, not the one chosen above. Squad size is how many.");
					if (s_RandomUnits) {
						ImGui::SameLine();
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6F);
						RandomSourceCombo("From##random", s_RandomFavourites, s_RandomFaction);
					}
				}
				if (kind == Tool::Unit || kind == Tool::Drop) {
					ToolUI::Checkbox("Jetpacks only", &s_JetpackOnly);
					ImGui::SetItemTooltip("Only units whose jetpack really flies them (lifts them 5 m or more). Units without one, or with one that only gives a hop, aren't listed or picked at random. Hover a unit to see its lift.");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					ImGui::Combo("##shown", &s_UnitsShown, "All units\0Fighters only\0Non-combatants only\0");
					ImGui::SetItemTooltip("Which units are listed: all of them, only fighters, or only non-combatants (animals, civilians: units that never start a fight).");
					TemperamentCombo("Temperament");
					const bool squadPicked = !s_RandomUnits && s_SpawnPick.Kind == CustomPick::Squad;
					ImGui::BeginDisabled(squadPicked);
					ImGui::SliderInt("Squad size", &s_SquadSize, 1, 10);
					ImGui::EndDisabled();
					if (squadPicked) {
						ImGui::SetItemTooltip("Your squad says how many.");
					}
					// An animal or a civilian has no kit to choose and takes no orders to fight (it is only one if the chosen unit is: random picks are soldiers unless asked for otherwise).
					const Preset* chosenUnit = kind == Tool::Unit && !s_RandomUnits && s_SpawnPick.Kind == CustomPick::None ? ChosenPreset(kind, ChoiceFor(kind)) : nullptr;
					ImGui::BeginDisabled(chosenUnit && chosenUnit->NonCombatant);
					LoadoutChooser("Loadout", true);
					UnitOrderCombo("Orders");
					ImGui::EndDisabled();
				} else if (kind == Tool::Vehicle) {
					VehicleOptions();
				} else if (kind == Tool::Item) {
					ToolUI::Checkbox("Pull the pin (grenades)", &s_LitGrenade);
				} else if (kind == Tool::Structure) {
					ImGui::SeparatorText("Lights and fires: shine from the background");
				{
					int column = 0;
					for (int i = 0; i < static_cast<int>(DecorKind::Count); ++i) {
						if (column++ % 3 != 0) {
							ImGui::SameLine();
						}
						if (ToolUI::RadioButton(DecorName(i), c_Tools[s_ToolIndex].Kind == Tool::Decor && s_DecorChoice == i)) {
							s_DecorChoice = i;
							TookTool(ToolIndex(Tool::Decor));
						}
						ImGui::SetItemTooltip("%s", DecorTip(i));
					}
				}
				ImGui::Separator();
				ToolUI::Checkbox("Snap to the bunker grid", &s_SnapToGrid);
				}
				UndoButton();
				EndSandboxTab();
			}
			// The colony buildings work (scripts can still place them with SandboxDo) but their tab is hidden until they are taken further.
			if (c_ShowColonyTab && SandboxTab("Colony")) {
				s_CurrentTab = "Colony";
				ColonyTab();
				EndSandboxTab();
			}
			if (SandboxTab("Build")) {
				s_CurrentTab = "Build";
				// Coming to this tab picks up the building tool.
				static int shownLast = -10;
				if (ImGui::GetFrameCount() > shownLast + 1 || CurrentTool().Kind != Tool::Structure) {
					if (ImGui::GetFrameCount() > shownLast + 1) {
						s_ToolIndex = ToolIndex(Tool::Structure);
					}
				}
				shownLast = ImGui::GetFrameCount();
				ImGui::TextWrapped("Bunker pieces, placed straight into the world. Pick one: it follows the pointer as it will stand, and a click puts it there. Keep clicking to place more.");
				ToolButtons({Tool::Structure, Tool::Remove, Tool::None});
				const int groupCount = static_cast<int>(std::size(c_StructureGroups));
				std::string groupNames;
				for (const char* group: c_StructureGroups) {
					groupNames += std::string(group) + '\0';
				}
				groupNames += std::string("Everything") + '\0';
				ImGui::Combo("Kind", &s_StructureGroup, groupNames.c_str());
				PictureGrid(Tool::Structure, s_StructureGroup < groupCount ? c_StructureGroups[s_StructureGroup] : nullptr);
				ToolUI::Checkbox("Snap to the bunker grid", &s_SnapToGrid);
				ImGui::SetItemTooltip("On: pieces line up with each other on the 24 pixel grid bunkers are built on. Off: they go exactly where the pointer is.");
				if (DragsTrack(Tool::Structure)) {
					ImGui::TextWrapped("A lift or moving platform: drag from where its deck starts to where it runs to (any way; Shift keeps it to the 8 ways, straight or at 45 degrees). A click puts it down as it comes.");
				}
				if (DragsRun(Tool::Structure)) {
					ImGui::TextWrapped("Drag to lay a run of this piece end to end, slanted to it (Shift keeps it to the 8 ways, straight or at 45 degrees). A click puts down one.");
				}
				ImGui::TextDisabled("Doors and turrets belong to:");
				SideChooser();
				UndoButton();
				ImGui::Separator();
				if (ToolUI::Button("The game's own build menu", ImVec2(-1.0F, 0.0F))) {
					// Placing through the game's build menu instead. Choose Done in its pie menu (or press Tab) to come back.
					Sandbox::SetBuildMode(true);
				}
				ImGui::SetItemTooltip("Puts these tools away and opens the build menu the game uses before a battle: everything it offers, moved and placed with the game's own cursor.\nDone in its menu, or Tab, comes back here.");
				EndSandboxTab();
			}
			if (SandboxTab("Orders")) {
				s_CurrentTab = "Orders";
				ImGui::TextWrapped("Give every unit on a side new orders. Units told to attack find a new target when theirs dies.");
				SideChooser();
				s_Order = std::clamp(s_Order, 0, c_OrderCount - 1);
				ImGui::Combo("Orders", &s_Order, OrderName, nullptr, c_OrderCount);
				if (static_cast<Order>(s_Order) == Order::MoveTo) {
					// Given by clicking the place: the pointer shows where each unit will stand first.
					ToolButtons({Tool::OrderMove});
					ImGui::TextWrapped("Click where the side should go. Each unit is shown a place of its own on the ground there, as close to the point as the ground allows, and the nearest unit takes the nearest place.");
				} else if (ToolUI::Button("Give orders", ImVec2(-1.0F, 0.0F))) {
					Stroke stroke;
					stroke.Kind = Tool::OrderSide;
					stroke.Team = s_Team;
					stroke.Orders = static_cast<Order>(s_Order);
					s_Queue.push_back(stroke);
				}
				if (ToolUI::Button("Everyone attack!", ImVec2(-1.0F, 0.0F))) {
					for (int side = 0; side < c_Sides; ++side) {
						Stroke stroke;
						stroke.Kind = Tool::OrderSide;
						stroke.Team = side;
						stroke.Orders = Order::Attack;
						s_Queue.push_back(stroke);
					}
				}
				const bool removeSide = ToolUI::Button("Remove this side's units", ImVec2(-1.0F, 0.0F));
				ImGui::SetItemTooltip("Takes away every unit of the side picked above (not doors, nor your character). Ctrl+click: no question asked.");
				const std::string removeSideQuestion = std::string("Every ") + c_SideNames[std::clamp(s_Team, 0, c_Sides - 1)] + " unit on the map is taken away, and it can't be undone.";
				if (Confirmed(removeSide, "Remove this side's units?", removeSideQuestion.c_str(), "Yes, remove them")) {
					Stroke stroke;
					stroke.Kind = Tool::RemoveSide;
					stroke.Team = s_Team;
					s_Queue.push_back(stroke);
				}
				ImGui::TextDisabled("Rally point: pick the tool above and click to place this side's flag.");
				EndSandboxTab();
			}
			if (SandboxTab("Battle")) {
				s_CurrentTab = "Battle";
				BattleTab();
				EndSandboxTab();
			}
			if (IsGodMode() && SandboxTab("Gym")) {
				s_CurrentTab = "Gym";
				GymTab();
				EndSandboxTab();
			}
			if (SandboxTab("Paint")) {
				s_CurrentTab = "Paint";
				RecentRow(Tool::PourOther);
				ImGui::SeparatorText("Paint over");
				ToolUI::Checkbox("Liquids", &s_PaintOverLiquids);
				ImGui::SetItemTooltip("The liquid, loose-ground and terrain brushes replace water, lava, oil, sand, snow and the like, instead of only filling air. Dig is as it was.");
				ImGui::SameLine();
				ToolUI::Checkbox("Terrain", &s_PaintOverTerrain);
				ImGui::SetItemTooltip("The liquid, loose-ground and terrain brushes replace solid terrain (earth, rock, concrete, metal, ...) instead of only filling air. The edge of the world stays. Dig is as it was.");
				ImGui::SeparatorText("Elements");
				ToolButtons({Tool::Fire, Tool::Water, Tool::Lava, Tool::Acid, Tool::Oil, Tool::Smoke, Tool::ToxicGas});
				ToolButtons({Tool::Mud, Tool::Tar, Tool::Mercury, Tool::Fuel, Tool::Cryo, Tool::Blood});
				{
					// Every other pourable (rubble, ash, mods' liquids), a button each, for the "Other" tool. Those with buttons of their own above
					// (and in Loose things) aren't repeated.
					static const char* const c_HaveButtons[] = {"Water", "Lava", "Acid", "Oil", "Mud", "Tar", "Mercury", "Fuel", "Cryogenic Fluid", "Blood", "Sand", "Snow", "Gravel", "Glass Shards"};
					std::vector<std::string> pourables = PourableNames();
					pourables.erase(std::remove_if(pourables.begin(), pourables.end(), [](const std::string& name) { return std::find(std::begin(c_HaveButtons), std::end(c_HaveButtons), name) != std::end(c_HaveButtons); }), pourables.end());
					MaterialButtons(Tool::PourOther, s_OtherPourable, pourables);
				}
				ImGui::SliderFloat("Flow", &s_Flow, 0.1F, 1.0F, "%.2f");
				ImGui::SetItemTooltip("How fast the liquid and loose-ground brushes pour while held. 1: as fast as they go.");
				ToolButtons({Tool::Methane, Tool::Steam});
				ImGui::SetItemTooltip("Gases fill the air they are let into and stay in a closed room. Toxic gas sinks and hurts whoever breathes it; methane rises, can't be seen, and goes up in a chain of blasts where it meets fire; steam rises, scalds and condenses away. Turn on the Gas overlay (Settings, sandbox overlays) to see where it is.");
				ImGui::SeparatorText("Springs");
				ToolButtons({Tool::WaterSpawner});
				ImGui::SameLine();
				ImGui::BeginDisabled(s_WaterSpawners.empty());
				const bool removeSprings = ToolUI::Button("Remove all springs");
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Takes away every spring on the map (what they have poured stays). Ctrl+click: no question asked.");
				if (Confirmed(removeSprings, "Remove all springs?", "Every spring on the map is taken away, and it can't be undone.", "Yes, remove them")) {
					QueueSimChange(Tool::ClearWaterSpawners);
				}
				{
					// Remove all of one kind: the kinds placed, each with how many.
					static std::string removeKind;
					std::vector<std::pair<std::string, int>> kinds = SpringCounts();
					auto chosen = std::find_if(kinds.begin(), kinds.end(), [](const auto& kind) { return kind.first == removeKind; });
					if (chosen == kinds.end() && !kinds.empty()) {
						removeKind = kinds.front().first;
						chosen = kinds.begin();
					}
					ImGui::BeginDisabled(kinds.empty());
					std::string shown = chosen != kinds.end() ? chosen->first + " " + std::to_string(chosen->second) : "(none placed)";
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5F);
					if (ImGui::BeginCombo("##removeKind", shown.c_str())) {
						for (const auto& [name, count]: kinds) {
							if (ImGui::Selectable((name + " " + std::to_string(count)).c_str(), name == removeKind)) {
								removeKind = name;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					if (ToolUI::Button(("Remove all " + (chosen != kinds.end() ? removeKind : std::string("of one kind"))).c_str())) {
						Stroke stroke;
						stroke.Kind = Tool::ClearWaterSpawners;
						stroke.Material = removeKind;
						s_Queue.push_back(stroke);
					}
					ImGui::EndDisabled();
				}
				if (ImGui::BeginCombo("Springs pour", s_SpringLiquid.c_str(), ImGuiComboFlags_HeightLarge)) {
					// A search over them, ready to type into as it opens: with mods' there can be dozens.
					static char filter[64] = {};
					if (ImGui::IsWindowAppearing()) {
						ImGui::SetKeyboardFocusHere();
					}
					ImGui::SetNextItemWidth(-1.0F);
					ImGui::InputTextWithHint("##springFilter", "Search...", filter, 64);
					ImGui::SetItemTooltip("Only the liquids and loose ground whose names have this in them.");
					for (const std::string& name: PourableNames()) {
						if (!ContainsIgnoringCase(name, filter)) {
							continue;
						}
						if (ImGui::Selectable(name.c_str(), name == s_SpringLiquid)) {
							s_SpringLiquid = name;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("What new springs pour, and what the tank is filled with.");
				ImGui::SliderFloat("Spring rate", &s_SpringRate, 0.05F, 1.0F, "%.2f");
				ImGui::SetItemTooltip("How much of the time new springs pour. 1: they keep the air around them full.");
				ImGui::SliderFloat("Spring lifetime", &s_SpringLife, 0.0F, 120.0F, s_SpringLife <= 0.0F ? "for ever" : "%.0f s");
				ImGui::SetItemTooltip("How long what a new spring pours lasts. Each bit of liquid is gone that many seconds after it was poured, wherever it has flowed to by then, so a spring can make a stream or a waterfall that never fills up what it runs into. For ever: it stays, and the pool builds up.");
				// Each spring: what it pours, on or off, removed (as the overlay's Delete does, from the ImGui frame).
				int removeSpring = -1;
				for (size_t i = 0; i < s_WaterSpawners.size(); ++i) {
					WaterSpawner& spring = s_WaterSpawners[i];
					ImGui::PushID(static_cast<int>(i));
					ToolUI::Checkbox("##on", &spring.On);
					ImGui::SetItemTooltip("Pouring. Off, it stays put and pours nothing.");
					ImGui::SameLine();
					{
						// (A swatch in the colour of what it pours, as on the map.)
						float side = ImGui::GetTextLineHeight() * 0.7F;
						ImVec2 at = ImGui::GetCursorScreenPos();
						ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + side * 0.2F), ImVec2(at.x + side, at.y + side * 1.2F), MaterialMarkColor(spring.Liquid, spring.On ? 255 : 110));
						ImGui::Dummy(ImVec2(side, 0.0F));
						ImGui::SameLine();
					}
					ImGui::Text("%s, %d px, at %d,%d", spring.Liquid.c_str(), spring.Radius, spring.Position.GetFloorIntX(), spring.Position.GetFloorIntY());
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4F);
					ImGui::SliderFloat("##rate", &spring.Rate, 0.05F, 1.0F, "rate %.2f");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6F);
					ImGui::SliderFloat("##life", &spring.Life, 0.0F, 120.0F, spring.Life <= 0.0F ? "life: for ever" : "life %.0f s");
					ImGui::SetItemTooltip("How many seconds what it pours lasts: gone that long after it was poured, wherever it has flowed to. For ever: it stays.");
					ImGui::SameLine();
					if (ToolUI::Button("x")) {
						removeSpring = static_cast<int>(i);
					}
					ImGui::PopID();
				}
				if (removeSpring >= 0) {
					s_WaterSpawners.erase(s_WaterSpawners.begin() + removeSpring);
				}
				ImGui::SeparatorText("Loose things");
				ToolButtons({Tool::LooseSand, Tool::LooseSnow, Tool::Gravel, Tool::GlassShards, Tool::Boulder, Tool::Slab});
				ImGui::SeparatorText("Plants");
				ToolButtons({Tool::Plants, Tool::Cacti, Tool::Mushrooms, Tool::Trees, Tool::GrowGrass, Tool::Candles});
				ImGui::SliderFloat("Plant size", &s_PlantScale, 0.5F, 3.0F, "x%.1f");
				ImGui::SetItemTooltip("How big the plants, cacti, mushrooms, trees and candles are drawn. x1 is the game's own art; bigger keeps it blocky, as the pixel art is (candles go up in whole steps, x1, x2, x3).");
				ImGui::SliderInt("Plant spacing", &s_PlantSpacing, 2, 60, "%d px");
				ImGui::SetItemTooltip("How far apart the plants go along a stroke. Each is one of the game's own plant pictures, set into the ground under the pointer.");
				if (IsPlantBrush(CurrentTool().Kind)) {
					PlantPickPanel(CurrentTool().Kind);
				}
				{
					// How long candles burn (the same setting as Settings > Fire and smoke).
					bool forever = TerrainCandle::GetBurnMinutes() <= 0.0F;
					if (ToolUI::Checkbox("Candles burn forever", &forever)) {
						TerrainCandle::SetBurnMinutes(forever ? 0.0F : 2.0F);
					}
					ImGui::SetItemTooltip("Lit candles keep burning and never melt down. Off, they burn down in the Candle burn time.");
					if (float minutes = TerrainCandle::GetBurnMinutes(); minutes > 0.0F) {
						ImGui::SameLine();
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5F);
						if (ImGui::SliderFloat("Candle burn time", &minutes, 0.5F, 60.0F, "%.1f min", ImGuiSliderFlags_Logarithmic)) {
							TerrainCandle::SetBurnMinutes(minutes);
						}
						ImGui::SetItemTooltip("How long a lit candle 20 pixels tall takes to burn down, whatever its width; a taller one takes longer.");
					}
				}
				ImGui::SeparatorText("Ropes");
				ToolButtons({Tool::Rope, Tool::RopeCut});
				ImGui::SameLine();
				ImGui::BeginDisabled(RopeSim::GetCount() == 0);
				const bool removeRopes = ToolUI::Button("Remove all ropes");
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Takes away every rope on the map. Ctrl+click: no question asked.");
				if (Confirmed(removeRopes, "Remove all ropes?", "Every rope on the map is taken away, and Ctrl+Z can't bring them back.", "Yes, remove them")) {
					Stroke stroke;
					stroke.Kind = Tool::Rope;
					stroke.Choice = 2;
					s_Queue.push_back(stroke);
					s_RopeDraft.clear();
				}
				for (int type = 0; type < RopeSim::GetTypeCount(); ++type) {
					if (type % 3 != 0) {
						ImGui::SameLine();
					}
					const RopeSim::TypeInfo& info = RopeSim::GetType(type);
					if (ToolUI::RadioButton(info.Name, s_RopeType == type)) {
						s_RopeType = type;
						TookTool(ToolIndex(Tool::Rope));
					}
					ImGui::SetItemTooltip("%s", info.About);
				}
				{
					int slack = static_cast<int>(std::round(s_RopeSlack * 100.0F));
					if (ImGui::SliderInt("Rope slack", &slack, 0, 100, "%d%%")) {
						s_RopeSlack = static_cast<float>(slack) / 100.0F;
					}
					ImGui::SetItemTooltip("How much longer than the straight line between its points the rope is: 0 strung tight, 10%% hangs a little, 50%% droops well down.");
					ImGui::SliderFloat("Rope strength", &s_RopeStrength, 0.1F, 100.0F, "x%.1f", ImGuiSliderFlags_Logarithmic);
					ImGui::SetItemTooltip("How much more (or less) than the kind's own it holds before it snaps. Everything else about it stays: a rope can be made to hold a dropship and still burn and be cut by a bullet.");
					ImGui::SliderFloat("Tie strength", &s_RopeAnchor, 0.0F, 5000.0F, s_RopeAnchor <= 0.0F ? "unbreakable" : "%.0f kg", ImGuiSliderFlags_Logarithmic);
					ImGui::SetItemTooltip("How hard a tie can be pulled, in kg, before it lets go of the rope (pulled out of the ground or off the unit). Far left: the ties never fail, only the rope can.");
					ImGui::BeginDisabled(!RopeSettingsChanged());
					if (ImGui::Button("Reset rope to default")) {
						ResetRopeSettings();
					}
					ImGui::EndDisabled();
					ImGui::SetItemTooltip("Puts slack, strength and tie strength back to the kind's own (slack 10%%, strength x1, ties unbreakable).");
				}
				ImGui::SeparatorText("Terrain");
				ToolButtons({Tool::Dig, Tool::Earth, Tool::Sand, Tool::Ice, Tool::Grass, Tool::Wood, Tool::TreeTrunk, Tool::Concrete});
				ToolButtons({Tool::Stone, Tool::DenseEarth, Tool::GoldEarth, Tool::CollapseArea, Tool::HoldGround});
				if (CurrentTool().Kind == Tool::HoldGround) {
					bool show = TerrainCollapse::ShowsHoldZones();
					if (ToolUI::Checkbox("Show hold zones without the tool", &show)) {
						TerrainCollapse::SetShowHoldZones(show);
					}
					ImGui::SetItemTooltip("On: the hold zones are outlined on the map whatever is in hand (also under Settings > Falling ground). Off: only while Hold ground is.");
					ImGui::SameLine();
					ImGui::BeginDisabled(TerrainCollapse::GetHoldZones().empty());
					if (ImGui::SmallButton("Clear all hold zones")) {
						QueueSimChange(Tool::HoldGround, 0);
						s_Queue.back().Choice = 2;
					}
					ImGui::EndDisabled();
					ImGui::SetItemTooltip("Takes every hold zone on this map away (Ctrl+Z puts them back).");
				}
				{
					// The rest of the base game's ground, a button each, for the "Other terrain" tool. Those with buttons of their own (above, and
					// under Metals) aren't repeated.
					static const char* const c_HaveButtons[] = {"Earth", "Dense Earth", "Stone", "Ice", "Sand", "Grass", "Wood", "Tree Trunk", "Concrete"};
					std::vector<std::string> terrain;
					for (const char* name: c_TerrainMaterials) {
						bool own = std::find_if(std::begin(c_HaveButtons), std::end(c_HaveButtons), [name](const char* other) { return std::string(other) == name; }) != std::end(c_HaveButtons);
						own = own || std::any_of(std::begin(c_PaintMetals), std::end(c_PaintMetals), [name](const PaintMetal& metal) { return std::string(metal.Material) == name; });
						if (!own) {
							terrain.emplace_back(name);
						}
					}
					MaterialButtons(Tool::TerrainOther, s_OtherTerrain, terrain);
				}
				ImGui::SeparatorText("Metals");
				MetalButtons();
				ImGui::TextUnformatted("Brush type");
				ImGui::SameLine();
				if (ImGui::RadioButton("Brush", !s_ShapeFill)) {
					s_ShapeFill = false;
				}
				ImGui::SameLine();
				if (ImGui::RadioButton("Shape", s_ShapeFill)) {
					s_ShapeFill = true;
				}
				ImGui::SetItemTooltip("Shape: click and drag out a circle, triangle, square or line on the world, and the terrain brush in hand fills it (Dig digs it out; with any brush, a right drag digs the shape out). Shift keeps it as wide as tall (a line: to the 8 ways, straight or at 45 degrees); Escape drops it.");
				if (s_ShapeFill) {
					int fill = static_cast<int>(s_FillShape);
					ImGui::TextUnformatted("Shape");
					ImGui::SameLine();
					ImGui::RadioButton("Circle##fill", &fill, 0);
					ImGui::SameLine();
					ImGui::RadioButton("Triangle##fill", &fill, 1);
					ImGui::SameLine();
					ImGui::RadioButton("Square##fill", &fill, 2);
					ImGui::SameLine();
					ImGui::RadioButton("Line##fill", &fill, 3);
					s_FillShape = static_cast<FillShape>(fill);
					if (s_FillShape == FillShape::Line) {
						ImGui::SliderInt("Line width", &s_LineWidth, 1, c_MaxLineWidth, "%d px", ImGuiSliderFlags_Logarithmic);
					}
				}
				ImGui::SliderInt("Brush size", &s_Radius, 1, c_MaxBrushRadius, "%d", ImGuiSliderFlags_Logarithmic);
				int shape = static_cast<int>(s_BrushShape);
				ImGui::TextUnformatted("Brush shape");
				ImGui::SameLine();
				ImGui::RadioButton("Circle", &shape, 0);
				ImGui::SameLine();
				ImGui::RadioButton("Square", &shape, 1);
				ImGui::SameLine();
				ImGui::RadioButton("Spray", &shape, 2);
				s_BrushShape = static_cast<BrushShape>(shape);
				ImGui::SetItemTooltip("What the terrain brushes (Dig and the materials) paint and dig: a circle, a square as wide as the brush, or a soft spray that scatters it over the circle, thickest in the middle, building up while held.");
				ImGui::SeparatorText("Copy and paste");
				ToolButtons({Tool::CopyArea});
				ImGui::SameLine();
				ImGui::BeginDisabled(!s_CopyBoxSet || s_Stamping);
				if (ImGui::Button("Copy")) {
					CopyMarkedBox();
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Ctrl+C: copies the box marked on the world with Copy area, its ground and (with \"Include units\") the units wholly inside it.");
				ImGui::SameLine();
				ImGui::BeginDisabled(s_Clipboard.Empty());
				if (ImGui::Button(s_Stamping ? "Stop pasting" : "Paste")) {
					if (s_Stamping) {
						s_Stamping = false;
					} else {
						TakeUpStamp();
					}
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Ctrl+V: a stamp of the copy on the pointer. Click to put it down (as often as you like), F flips it, Escape puts it away. Each one put down is a step Ctrl+Z takes back.");
				ImGui::Checkbox("Include units", &s_CopyUnits);
				ImGui::SetItemTooltip("A copy takes the units wholly inside the box too (not your character, nor the unit you are in), with their kit and side; each paste puts copies of them down.");
				ImGui::SameLine();
				ImGui::Checkbox("Paste the air too", &s_PasteAir);
				ImGui::SetItemTooltip("The air in the copy clears what is under the stamp, so it is put down exactly as it was. Off, only its ground is put down, over what is there.");
				ImGui::SameLine();
				ImGui::Checkbox("Flipped", &s_StampFlipped);
				ImGui::SetItemTooltip("F while pasting: the stamp flipped left to right, its units turned round with it.");
				if (s_Clipboard.Empty()) {
					ImGui::TextDisabled("Nothing copied yet: drag a box with Copy area, then Ctrl+C.");
				} else {
					ImGui::TextDisabled("Copied: %d x %d px, %d unit%s", s_Clipboard.Width, s_Clipboard.Height, static_cast<int>(s_Clipboard.Units.size()), s_Clipboard.Units.size() == 1 ? "" : "s");
				}
				ImGui::SetItemTooltip("Kept until the game is closed, so ground can be taken from one map to another.");
				UndoButton();
				EndSandboxTab();
			}
			if (SandboxTab("Boom")) {
				s_CurrentTab = "Boom";
				ToolButtons({Tool::Grenade, Tool::BigBomb, Tool::Napalm, Tool::Lightning});
				ImGui::SeparatorText("Craters");
				ToolButtons({Tool::Demolition, Tool::BunkerBuster, Tool::Meteor});
				ImGui::SeparatorText("Force: shoves and scatters, burns and harms nothing");
				ToolButtons({Tool::ForceBlast, Tool::HugeForceBlast, Tool::Implosion, Tool::Updraft, Tool::GustRight, Tool::GustLeft});
				ImGui::SeparatorText("For show");
				ToolButtons({Tool::SmokeBomb, Tool::Fireworks});
				ImGui::SeparatorText("From the sky: click where it should land");
				ToolButtons({Tool::RocketStrike, Tool::RocketBarrage, Tool::CarpetBomb, Tool::Artillery, Tool::NapalmRain, Tool::OrbitalBeam, Tool::BoulderRain});
				ImGui::SeparatorText("Craft that don't make it: click where it comes down");
				ToolButtons({Tool::CrashRocket, Tool::CrashDropship});
				if (!s_Incoming.empty()) {
					ImGui::TextDisabled("%d on the way", static_cast<int>(s_Incoming.size()));
				}
				EndSandboxTab();
			}
			if (SandboxTab("Effects")) {
				s_CurrentTab = "Effects";
				ImGui::TextWrapped("Pick one, then click in the world to put it down. They keep running until removed.");
				RecentRow(Tool::Effect);
				// A search over every effect, these and the ones you made.
				char* filter = FilterFor(Tool::Effect, false);
				ImGui::SetNextItemWidth(-1.0F);
				ImGui::InputTextWithHint("##effectFilter", "Search...", filter, 64);
				ImGui::SetItemTooltip("Only the effects whose names have this in them, the ones you made too.");
				// Each heading with its effects, left out when the search lets none of them through.
				auto effectButtons = [filter](const char* heading, std::initializer_list<EffectKind> kinds) {
					if (std::none_of(kinds.begin(), kinds.end(), [filter](EffectKind kind) { return ContainsIgnoringCase(c_Effects[static_cast<int>(kind)].Name, filter); })) {
						return;
					}
					ImGui::SeparatorText(heading);
					int column = 0;
					for (EffectKind kind: kinds) {
						if (!ContainsIgnoringCase(c_Effects[static_cast<int>(kind)].Name, filter)) {
							continue;
						}
						if (column++ % 3 != 0) {
							ImGui::SameLine();
						}
						int index = static_cast<int>(kind);
						bool chosen = c_Tools[s_ToolIndex].Kind == Tool::Effect && s_EffectChoice == index;
						if (ToolUI::RadioButton(c_Effects[index].Name, chosen)) {
							s_EffectChoice = index;
							TookTool(ToolIndex(Tool::Effect));
						}
						ImGui::SetItemTooltip("%s", c_Effects[index].Tip);
					}
				};
				effectButtons("Lights", {EffectKind::NuclearGlow, EffectKind::StormCell, EffectKind::RedAlarm, EffectKind::PoliceLights, EffectKind::BlueBeacon, EffectKind::Floodlight, EffectKind::Searchlight, EffectKind::Strobe, EffectKind::Disco,
				               EffectKind::Candle, EffectKind::LavaGlow, EffectKind::Fireflies});
				effectButtons("Lights with particles", {EffectKind::Campfire, EffectKind::WeldingArc, EffectKind::Portal, EffectKind::SparkFountain, EffectKind::FireJet, EffectKind::ToxicVent});
				effectButtons("Particles and air", {EffectKind::EmberVent, EffectKind::SmokeStack, EffectKind::SmokePlume, EffectKind::MistVent, EffectKind::DustDevil, EffectKind::HeatShimmer, EffectKind::ShockwavePulse});
				CustomEffectsUI();
				ImGui::Separator();
				ImGui::BeginDisabled(s_Effects.empty());
				const bool removeEffects = ToolUI::Button("Remove all effects");
				ImGui::SetItemTooltip("Takes away every effect put down. Ctrl+click: no question asked.");
				ImGui::SameLine();
				if (ToolUI::Button("Remove the last one")) {
					QueueSimChange(Tool::ClearEffects, 1);
				}
				ImGui::EndDisabled();
				if (Confirmed(removeEffects, "Remove all effects?", "Every effect put down is taken away, and it can't be undone.", "Yes, remove them")) {
					QueueSimChange(Tool::ClearEffects);
				}
				ImGui::SameLine();
				ImGui::TextDisabled("%d running", static_cast<int>(s_Effects.size()));
				EndSandboxTab();
			}
			if (SandboxTab("World")) {
				s_CurrentTab = "World";
				if (IsGodMode()) {
					SideStatus();
					TimeControls();
					ImGui::Separator();
				}
				LightingSettings& settings = g_PostProcessMan.GetLightingSettings();
				ImGui::Combo("Weather", &settings.WeatherType, Weather::GetComboItems(settings.CustomWeather).c_str());
				ImGui::SliderFloat("Intensity", &settings.WeatherIntensity, 0.0F, 1.0F);
				ImGui::Checkbox("Weather everywhere", &settings.WeatherEverywhere);
				ImGui::SetItemTooltip("The weather shows everywhere but over the terrain and its back walls, under roofs and overhangs too. The same as in the settings.");
				ImGui::SliderFloat("Wind", &settings.Wind, -200.0F, 200.0F, "%.0f px/s");
				ImGui::SliderFloat("Hour", &settings.TimeOfDay, 0.0F, 24.0F, "%.1f");
				for (auto [name, hour]: {std::pair{"Dawn", 6.5F}, std::pair{"Day", 12.0F}, std::pair{"Dusk", 19.0F}, std::pair{"Night", 23.0F}}) {
					if (hour != 6.5F) {
						ImGui::SameLine();
					}
					if (ToolUI::Button(name)) {
						settings.TimeOfDay = hour;
					}
				}
				ImGui::Separator();
				float zoom = g_FrameMan.GetCameraZoom();
				if (ImGui::SliderFloat("Zoom", &zoom, FrameMan::c_MinCameraZoom, FrameMan::c_MaxCameraZoom, "%.2fx")) {
					g_FrameMan.SetCameraZoom(zoom);
				}
				ImGui::SameLine();
				if (ToolUI::SmallButton("1x")) {
					g_FrameMan.SetCameraZoom(1.0F);
				}
				ToolUI::Checkbox("Free camera", &s_FreeCamera);
				ImGui::SameLine();
				ToolUI::Checkbox("Follow the action", &s_FollowAction);
				ImGui::SameLine();
				// (Ticked whenever time runs slow, however it was set: set from the speed slider, the box read unticked at 0.25x.)
				s_SlowMotion = g_TimerMan.GetTimeScale() < 0.99F;
				if (ToolUI::Checkbox("Slow motion", &s_SlowMotion)) {
					g_TimerMan.SetTimeScale(s_SlowMotion ? 0.25F : 1.0F);
				}
				// The map's gravity (Scene GlobalAcceleration, which scripts can set too), its pull down as a share of Earth's. Everything reads it as it
				// goes, so under a twentieth of Earth's the zero gravity ways (ZeroGravity: units flying on their jets, liquids hanging, nothing
				// falling) take over at once, and go again as it comes back. Its pull sideways, if the map has one, is left as it is.
				ImGui::SeparatorText("Gravity");
				if (Scene* scene = g_SceneMan.GetScene()) {
					Vector gravity = scene->GetGlobalAcc();
					float earths = gravity.m_Y / c_EarthGravity;
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55F);
					if (ImGui::SliderFloat("##gravity", &earths, 0.0F, 3.0F, "x%.2f Earth")) {
						scene->SetGlobalAcc(Vector(gravity.m_X, earths * c_EarthGravity));
					}
					ImGui::SetItemTooltip("How hard everything is pulled down: units, things, shots, liquids, sand and falling ground. x1 is the game's own (20 m/s/s, as most maps have).\nUnder x0.05 there is no gravity to speak of: units fly on their jetpacks or push off what they touch, and liquids float (Settings > Zero G says how).\nCtrl+click to type a value outside the slider's.");
					ImGui::SameLine();
					ImGui::TextDisabled("%.1f m/s/s", gravity.m_Y);
					ImGui::SetItemTooltip("The pull down in the game's own units, metres per second per second.");
					for (const auto& [name, share, tip]: {std::tuple<const char*, float, const char*>{"Earth", 1.0F, "The game's own gravity, x1 (20 m/s/s)."}, {"Moon", c_MoonGravity, "About a sixth of Earth's: units jump high and fall slowly."}, {"Zero", 0.0F, "No gravity at all: as in space. Units fly on their jetpacks, liquids float, nothing falls."}}) {
						if (share != 1.0F) {
							ImGui::SameLine();
						}
						if (ToolUI::Button(name)) {
							scene->SetGlobalAcc(Vector(gravity.m_X, share * c_EarthGravity));
						}
						ImGui::SetItemTooltip("%s", tip);
					}
					ImGui::SameLine();
					ImGui::BeginDisabled(!s_MapGravityKnown || gravity == s_MapGravity);
					if (ToolUI::Button("Map's own")) {
						scene->SetGlobalAcc(s_MapGravity);
					}
					ImGui::EndDisabled();
					ImGui::SetItemTooltip("Back to the gravity this map had when the game started (x%.2f Earth).", s_MapGravity.m_Y / c_EarthGravity);
					if (ZeroGravity::UnitsWeightless()) {
						ImGui::TextColored(ImVec4(0.6F, 0.82F, 1.0F, 1.0F), "Zero gravity: units fly, liquids float.");
					}
				}
				// The world simulations the Paint tools need, here as well as in F6, so a brush that does nothing says why and can be fixed on the spot.
				ImGui::SeparatorText("Simulations");
				{
					bool liquids = FluidSim::IsEnabled();
					if (ToolUI::Checkbox("Flowing liquids", &liquids)) {
						FluidSim::SetEnabled(liquids);
					}
					ImGui::SetItemTooltip("Off: liquids stay where they are and nothing more can be poured.");
					ImGui::SameLine();
					bool powders = FluidSim::PowdersEnabled();
					if (ToolUI::Checkbox("Loose ground", &powders)) {
						FluidSim::SetPowdersEnabled(powders);
					}
					ImGui::SetItemTooltip("Sand, snow, gravel and glass slide and pile. Off: they can't be poured.");
					ImGui::SameLine();
					bool blood = FluidSim::BloodFlows();
					if (ToolUI::Checkbox("Blood flows", &blood)) {
						FluidSim::SetBloodFlows(blood);
					}
					ImGui::SetItemTooltip("Spilt blood runs and pools, then soaks away. Off: it stays where it fell.");
					ImGui::SameLine();
					bool freezing = FluidSim::FreezingEnabled();
					if (ToolUI::Checkbox("Freezing", &freezing)) {
						FluidSim::SetFreezingEnabled(freezing);
					}
					ImGui::SetItemTooltip("Still water freezes over in snowy weather.");
				}
				ImGui::Text("%d burning, %d liquid pixels flowing", TerrainFire::GetCount(), FluidSim::GetActiveCount());
				const bool putOut = ToolUI::Button("Put out all fire");
				ImGui::SetItemTooltip("Puts out every fire burning on the map at once. Ctrl+click: no question asked.");
				if (Confirmed(putOut, "Put out all fire?", "Every fire on the map goes out at once, and it can't be undone.", "Yes, put it out")) {
					TerrainFire::Clear();
				}
				EndSandboxTab();
			}
			if (SandboxTab("Keys")) {
				s_CurrentTab = "Keys";
				KeysPage();
				EndSandboxTab();
			}
		}
	}
	g_DebugMan.EndPanel();
}

void Sandbox::OnActivityStarted() {
	// (Called by ActivityMan::StartActivity for every game started, loaded or restarted, before its own start-up runs. A new game used to be
	// told by the activity's address changing, in two places, which a new game allocated where the last one was would not have changed.)
	Controller::SetAIPaused(false);
	Colony::Clear();
	// The bar is up at the start of every game, even if U put it away in the last one.
	s_BarShown = true;
	// A new game: nothing is left pouring or on its way in from the last one.
	s_WaterSpawners.clear();
	s_Incoming.clear();
	s_Effects.clear();
	s_Decor.clear();
	s_PaintUndo.clear();
	// (The new map's own gravity is taken on the first update, once its scene is loaded.)
	s_MapGravityKnown = false;
	s_PaintRedo.clear();
	// The same random stream from the start of every game, so the same inputs give the same game.
	s_Random = c_RandomSeed;
	// And none of the last game's units, orders or battle: in any game, not only a Sandbox one. (Reset only when the god view opened, an
	// auto battle started in a skirmish kept landing waves in the next game, and the selection, groups and rally points pointed into it.)
	s_Possessed = nullptr;
	s_PlayerUnit = UnitRef();
	s_PlayerMade.clear();
	s_PlayerEnterPending = 0;
	s_RespawnSet = false;
	ClearRespawn();
	s_Flying = false;
	s_StepsWanted = 0;
	s_RallySet.fill(false);
	s_Selected.clear();
	for (std::vector<UnitRef>& group: s_Groups) {
		group.clear();
	}
	s_OrderMarks.clear();
	s_FollowTarget = UnitRef();
	ForgetBattle();
	// (The setup saved to load with the map goes back in once it is loaded.)
	BattlePresetsNewGame();
	s_PendingOrders.clear();
	s_Commander = false;
	// (And clicks queued in the last game, not yet applied: they were applied to this one.)
	s_Queue.clear();
	s_GodViewPending = true;
}

void Sandbox::Update() {
	std::vector<Stroke> strokes;
	strokes.swap(s_Queue);
	if (!InGame()) {
		s_Possessed = nullptr;
		s_Plans.clear();
		ForgetBreaches();
		ForgetBuilds();
		ForgetAirlifts();
		s_GuardPosts.clear();
		s_Commander = false;
		s_Paced.clear();
		s_MoveWatch.clear();
		s_NoRoutes.clear();
		s_Incoming.clear();
		s_WaterSpawners.clear();
		s_Effects.clear();
		s_Decor.clear();
		return;
	}
	if (!s_MapGravityKnown && g_SceneMan.GetScene()) {
		// The map's own gravity, for the World tab to put back (after the game's own start-up, which may set it).
		s_MapGravity = g_SceneMan.GetScene()->GetGlobalAcc();
		s_MapGravityKnown = true;
	}
	BattlePresetsUpdate();
	ApplyPendingOrders();
	s_StrokesApplied = strokes.size();
	for (const Stroke& stroke: strokes) {
		Apply(stroke);
	}
	ClosePaintUndoStep(false);
	UpdatePlans();
	UpdateDropoffs();
	UpdateBreaches();
	UpdateBuilds();
	UpdateAirlifts();
	UpdateCommander();
	UpdatePace();
	UpdateMoveWatch();
	UpdateIncoming();
	UpdateEffects();
	UpdateDecor();
	for (WaterSpawner& spawner: s_WaterSpawners) {
		if (!spawner.On) {
			continue;
		}
		spawner.Due += std::clamp(spawner.Rate, 0.05F, 1.0F);
		if (spawner.Due >= 1.0F) {
			spawner.Due -= 1.0F;
			FluidSim::Pour(spawner.Position, static_cast<float>(spawner.Radius), spawner.Liquid.c_str(), spawner.Life);
		}
	}
	// With the AI paused, the sandbox's own passes wait too: they walked defenders home once a second, and the battle kept dropping waves,
	// all on units held still. (Its wave clocks are held back as well, so the waves don't all come at once after.) Attackers pick their own
	// enemies in their AI (SharedBehaviors.AttackOrderUpdate), which the pause holds as it is.
	const bool aiPaused = Controller::IsAIPaused();
	if (!aiPaused && g_TimerMan.GetSimUpdateCount() % 60 == 0) {
		ReturnDefenders();
	}
	if (!aiPaused && g_TimerMan.GetSimUpdateCount() % 30 == 0) {
		UpdateGuards();
	}
	GymUpdate();
	// (No sandbox-side watchdog for units that have stopped: getting unstuck, waiting for fuel before a tall climb, and giving up on a route that
	// can't be had are the AI's own business now, and re-ordering a unit every three seconds only restarted whatever it was in the middle of.)
	UpdateBattle(aiPaused);
	Colony::Update();
	if (s_FollowAction && g_TimerMan.GetSimUpdateCount() % 30 == 0) {
		FindAction();
	}
	if (IsGodMode()) {
		GameActivity* game = CurrentGame();
		if (s_Possessed && (!g_MovableMan.IsActor(s_Possessed) || static_cast<long>(s_Possessed->GetUniqueID()) != s_PossessedID)) {
			s_Possessed = nullptr;
			s_Flying = false;
			if (s_PlayerWasPlaying && s_RespawnSet && HasCharacter() && !IsBattleCommand()) {
				// Your character died and has a respawn point: the view stays where it died, with the tools away, until it is back (UpdatePlayer).
				g_ConsoleMan.PrintString(s_Player.RespawnInstant ? "SANDBOX: You died; back at your respawn point." : "SANDBOX: You died; you respawn in a few seconds.");
			} else {
				// The unit you were controlling died: back to the god view.
				g_ConsoleMan.PrintString("SANDBOX: The unit you were controlling is gone; back to the god view.");
				if (!IsBattleCommand()) {
					g_DebugMan.OpenTools();
				}
				s_FreeCameraStarted = false;
			}
		} else if (s_Possessed && game && s_PlayerEnterPending == 0) {
			// The unit the game says you control is the one you control. Its own next/previous actor keys stay live while you're in a unit,
			// and switch among the activity's side (Red in the sandbox), which the sandbox never heard of: it went on watching the unit you
			// left, Tab released whichever the game had, and the character kept flying under the AI's keys.
			// A switch within the unit's own side is followed; one that crossed to another side is undone, back into the unit you were in.
			Actor* controlled = game->GetControlledActor(Players::PlayerOne);
			if (controlled != s_Possessed) {
				if (controlled && g_MovableMan.IsActor(controlled) && controlled->GetTeam() == s_Possessed->GetTeam()) {
					if (s_Possessed == GetRef(s_PlayerUnit)) {
						StopFlying();
					}
					SetPossessed(controlled);
				} else if (game->SwitchToActor(s_Possessed, Players::PlayerOne, s_Possessed->GetTeam())) {
					game->SetViewState(Activity::ViewState::Normal, Players::PlayerOne);
				} else {
					// (Not to be had back: to the god view, as when it dies.)
					StopFlying();
					s_Possessed = nullptr;
					if (!IsBattleCommand()) {
						g_DebugMan.OpenTools();
					}
					s_FreeCameraStarted = false;
				}
			}
		}
		UpdatePlayer();
		UpdatePlayerSquads();
		UpdateLoadoutUnits();
		if (game) {
			for (int team = 0; team < c_Sides; ++team) {
				if (game->GetTeamFunds(team) < 500000.0F) {
					game->SetTeamFunds(1000000.0F, team);
				}
			}
		}
		// The god doesn't get handed a unit: stay watching unless controlling one on purpose.
		if (game && !s_Possessed && game->GetViewState(Players::PlayerOne) != Activity::ViewState::Observe) {
			if (game->GetControlledActor(Players::PlayerOne)) {
				game->LoseControlOfActor(Players::PlayerOne);
			}
			game->SetViewState(Activity::ViewState::Observe, Players::PlayerOne);
		}
	}
}

void Sandbox::DrawOrderLabels() {
	if (!g_SettingsMan.ShowOrderLabels() || !g_ActivityMan.GetActivity()) {
		return;
	}
	// The AI modes (Actor::AIMode's order) in the words of the sandbox's orders.
	static const char* const modeOrders[] = {"do nothing", "hold", "patrol", "move", "hunt brains", "dig gold", "go back", "stay", "scuttle", "deliver", "bomb", "squad"};
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	GameViewRect view = g_WindowMan.GetGameViewRect();
	bool aiPaused = Controller::IsAIPaused();
	float lineHeight = ImGui::GetTextLineHeight();
	float pad = std::max(2.0F, lineHeight * 0.2F);
	for (const Actor* actor: SandboxAccess::Actors()) {
		if (!IsCombatant(actor)) {
			continue;
		}
		// Under the feet: a little below the unit's middle, by its size.
		ImVec2 at = ToScreen(actor->GetPos() + Vector(0.0F, actor->GetRadius() * 0.8F));
		if (at.x < view.x - 100.0F || at.x > view.x + view.w + 100.0F || at.y < view.y - 40.0F || at.y > view.y + view.h + 40.0F) {
			continue;
		}
		std::string order;
		if (actor->IsPlayerControlled()) {
			order = "player";
		} else if (actor->GetOrderTargetID() != 0) {
			order = "attack #" + std::to_string(static_cast<long long>(actor->GetOrderTargetID()));
		} else if (actor->GetOrderHasAttackPlace()) {
			order = "attack towards a place";
		} else if (actor->GetOrderAttack()) {
			order = "attack nearest";
		} else if (actor->GetOrderHasPost()) {
			order = "defend a spot";
		} else {
			int mode = actor->GetAIMode();
			order = mode >= 0 && mode < static_cast<int>(std::size(modeOrders)) ? modeOrders[mode] : "mode " + std::to_string(mode);
		}
		if (!actor->IsPlayerControlled()) {
			if (actor->NumberValueExists("AIRetreat")) {
				order += actor->GetNumberValue("AIRetreat") == 2 ? ", falling back to a medic" : ", falling back";
			} else if (actor->NumberValueExists("AIFlank")) {
				order += ", flanking";
			} else if (actor->NumberValueExists("AIInvestigate")) {
				order += ", checking where an enemy was seen";
			} else if (actor->NumberValueExists("AIMedic")) {
				order += ", seeing to a wounded friend";
			}
		}
		for (size_t group = 0; group < s_Groups.size(); ++group) {
			if (std::any_of(s_Groups[group].begin(), s_Groups[group].end(), [actor](const UnitRef& ref) { return GetRef(ref) == actor; })) {
				order += "  [" + std::to_string(group) + "]";
				break;
			}
		}
		bool paused = aiPaused && !actor->IsPlayerControlled();
		const char* badge = "AI paused";
		ImVec2 size = ImGui::CalcTextSize(order.c_str());
		float width = std::max(size.x, paused ? ImGui::CalcTextSize(badge).x : 0.0F);
		float height = lineHeight * (paused ? 2.0F : 1.0F);
		ImVec2 topLeft(std::floor(at.x - width * 0.5F - pad), std::floor(at.y));
		ImVec2 bottomRight(topLeft.x + width + pad * 2.0F, topLeft.y + height + pad * 2.0F);
		drawList->AddRectFilled(topLeft, bottomRight, IM_COL32(10, 12, 10, 170));
		drawList->AddText(ImVec2(topLeft.x + pad, topLeft.y + pad), c_SideColors[actor->GetTeam()], order.c_str());
		if (paused) {
			drawList->AddText(ImVec2(topLeft.x + pad, topLeft.y + pad + lineHeight), IM_COL32(255, 210, 80, 255), badge);
		}
	}
}
