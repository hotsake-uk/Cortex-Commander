// The gym: AI courses on a map, run and timed.

#include "SandboxInternal.h"

namespace SandboxDetail {
	std::string GymSceneName() {
		return g_SceneMan.GetScene() ? g_SceneMan.GetScene()->GetPresetName() : "";
	}

	std::string GymFile() {
		return "Userdata/Gyms/" + GymSceneName() + ".txt";
	}

	std::string GymSettingsFile() {
		return "Userdata/Gyms/" + GymSceneName() + ".settings.txt";
	}

	/// Puts the gym's settings into the game as the settings file would (TerrainCollapse = 0 keeps the buildings up, say). They are also
	/// read into the settings the map is opened with by Tools/RenderTest/Gym.ps1, so they hold from the start.
	void GymApplySettings() {
		std::istringstream lines(s_GymSettings);
		std::string line;
		while (std::getline(lines, line)) {
			size_t equals = line.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			auto trim = [](std::string text) {
				size_t from = text.find_first_not_of(" \t\r");
				size_t to = text.find_last_not_of(" \t\r");
				return from == std::string::npos ? std::string() : text.substr(from, to - from + 1);
			};
			std::string key = trim(line.substr(0, equals));
			std::string value = trim(line.substr(equals + 1));
			if (key.empty()) {
				continue;
			}
			Reader reader(std::make_unique<std::istringstream>(value), "gym settings");
			g_SettingsMan.ReadProperty(key, reader);
		}
	}

	void GymLoadSettings() {
		s_GymSettings[0] = 0;
		std::ifstream file(GymSettingsFile());
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		std::strncpy(s_GymSettings, text.c_str(), sizeof(s_GymSettings) - 1);
		s_GymSettings[sizeof(s_GymSettings) - 1] = 0;
		if (s_GymSettings[0]) {
			GymApplySettings();
		}
	}

	void GymSaveSettings() {
		std::filesystem::create_directories("Userdata/Gyms");
		std::ofstream file(GymSettingsFile());
		file << s_GymSettings;
	}

	void GymLoad() {
		std::string scene = GymSceneName();
		if (scene == s_GymScene) {
			return;
		}
		s_GymScene = scene;
		s_GymCourses.clear();
		s_GymRuns.clear();
		GymLoadSettings();
		std::ifstream file(GymFile());
		std::string line;
		while (std::getline(file, line)) {
			size_t first = line.find('|');
			if (first == std::string::npos) {
				continue;
			}
			GymCourse course;
			course.Name = line.substr(0, first);
			float x1 = 0.0F;
			float y1 = 0.0F;
			float x2 = 0.0F;
			float y2 = 0.0F;
			if (std::sscanf(line.c_str() + first + 1, "%f,%f|%f,%f", &x1, &y1, &x2, &y2) == 4) {
				course.From = Vector(x1, y1);
				course.To = Vector(x2, y2);
				s_GymCourses.push_back(course);
			}
		}
	}

	void GymSave() {
		std::filesystem::create_directories("Userdata/Gyms");
		std::ofstream file(GymFile());
		for (const GymCourse& course: s_GymCourses) {
			file << course.Name << '|' << static_cast<int>(course.From.m_X) << ',' << static_cast<int>(course.From.m_Y) << '|' << static_cast<int>(course.To.m_X) << ',' << static_cast<int>(course.To.m_Y) << '\n';
		}
	}

	/// A standing spot for a unit near a clicked point: down out of any wall or slab the click landed in, then down to the floor, and up
	/// off it by a fifth of the body. (A click a few pixels into a bunker's floor put the unit inside the floor.)
	Vector GymSettle(const Vector& point, float height) {
		int x = static_cast<int>(point.m_X);
		int y = static_cast<int>(point.m_Y);
		int limit = 0;
		while (g_SceneMan.GetTerrMatter(x, y) != MaterialColorKeys::g_MaterialAir && limit++ < 200) {
			++y;
		}
		limit = 0;
		while (g_SceneMan.GetTerrMatter(x, y) == MaterialColorKeys::g_MaterialAir && limit++ < 400) {
			++y;
		}
		return Vector(static_cast<float>(x), static_cast<float>(y) - height * 0.2F);
	}

	/// Starts a run of one course. @return Whether a unit was put down for it.
	bool GymRunCourse(int index) {
		if (index < 0 || index >= static_cast<int>(s_GymCourses.size())) {
			return false;
		}
		const Preset* preset = ChosenPreset(Tool::Unit, s_UnitChoice);
		if (!preset) {
			return false;
		}
		Actor* unit = CreateUnit(*preset, s_Team, s_Loadout, Order::Hold);
		if (!unit) {
			return false;
		}
		GymCourse& course = s_GymCourses[index];
		Vector from = GymSettle(course.From, unit->GetHeight());
		Vector to = GymSettle(course.To, unit->GetHeight());
		unit->SetPos(from);
		unit->SetNumberValue(c_GymUnitTag, 1.0);
		if (g_SettingsMan.DebugChannelOn(SettingsMan::DebugChannel::AI)) {
			unit->SetNumberValue("AITrace", 1.0);
		}
		g_MovableMan.AddActor(unit);
		unit->ClearAIWaypoints();
		unit->AddAISceneWaypoint(to);
		unit->SetAIMode(Actor::AIMODE_GOTO);
		GymRun run;
		run.Course = index;
		run.Unit = MakeRef(unit);
		run.LastPos = from;
		run.Goal = to;
		s_GymRuns.push_back(run);
		course.Result = "running";
		s_GymReported = false;
		g_ConsoleMan.PrintString("GYM " + course.Name + ": started, " + preset->PresetName + " from " + std::to_string(static_cast<int>(course.From.m_X)) + "," + std::to_string(static_cast<int>(course.From.m_Y)) + " to " + std::to_string(static_cast<int>(course.To.m_X)) + "," + std::to_string(static_cast<int>(course.To.m_Y)));
		return true;
	}

	/// Starts every course of this map at once. @return Whether there were any.
	bool GymRunAll() {
		GymLoad();
		bool any = false;
		for (int i = 0; i < static_cast<int>(s_GymCourses.size()); ++i) {
			any = GymRunCourse(i) || any;
		}
		return any;
	}

	void GymRemoveUnits() {
		for (Actor* actor: SandboxAccess::Actors()) {
			if (actor->NumberValueExists(c_GymUnitTag)) {
				actor->SetToDelete(true);
			}
		}
		s_GymRuns.clear();
	}

	/// Watches the runs: arrived within reach of the goal, dead, or given up after a minute, and how long it stood still.
	void GymUpdate() {
		bool allDone = true;
		for (GymRun& run: s_GymRuns) {
			if (run.Done) {
				continue;
			}
			GymCourse& course = s_GymCourses[run.Course];
			Actor* unit = GetRef(run.Unit);
			std::string seconds = std::to_string(static_cast<int>(run.Clock.GetElapsedSimTimeMS() / 100) / 10.0).substr(0, 4);
			if (!unit || unit->GetHealth() <= 0.0F) {
				course.Result = "died after " + seconds + " s";
				run.Done = true;
			} else {
				if (run.StillClock.IsPastSimMS(1000)) {
					run.StillClock.Reset();
					if (g_SceneMan.ShortestDistance(run.LastPos, unit->GetPos(), g_SceneMan.SceneWrapsX()).MagnitudeIsLessThan(4.0F)) {
						++run.Still;
					}
					run.LastPos = unit->GetPos();
				}
				float left = g_SceneMan.ShortestDistance(unit->GetPos(), run.Goal, g_SceneMan.SceneWrapsX()).GetMagnitude();
				if (left < c_GymArrivedWithin) {
					course.Result = "arrived in " + seconds + " s, stood still " + std::to_string(run.Still) + " s";
					run.Done = true;
				} else if (run.Clock.IsPastSimMS(c_GymGiveUpMS)) {
					course.Result = "gave up after 60 s, " + std::to_string(static_cast<int>(left)) + " px short at " + std::to_string(static_cast<int>(unit->GetPos().m_X)) + "," + std::to_string(static_cast<int>(unit->GetPos().m_Y)) + ", stood still " + std::to_string(run.Still) + " s";
					run.Done = true;
				}
			}
			if (run.Done) {
				g_ConsoleMan.PrintString("GYM " + course.Name + ": " + course.Result);
			} else {
				allDone = false;
			}
		}
		if (allDone && !s_GymReported) {
			s_GymReported = true;
			g_ConsoleMan.PrintString("GYM done");
		}
	}

	/// The courses' starts and goals over the map, while the Gym tab is open.
	void DrawGym(ImDrawList* drawList) {
		auto mark = [&](const Vector& at, ImU32 color, bool goal) {
			ImVec2 on = ToScreen(at);
			if (goal) {
				drawList->AddTriangleFilled(ImVec2(on.x, on.y - 10.0F), ImVec2(on.x + 7.0F, on.y + 2.0F), ImVec2(on.x - 7.0F, on.y + 2.0F), color);
			} else {
				drawList->AddCircleFilled(on, 5.0F, color);
			}
		};
		for (const GymCourse& course: s_GymCourses) {
			drawList->AddLine(ToScreen(course.From), ToScreen(course.To), IM_COL32(255, 255, 255, 50), 1.0F);
			mark(course.From, IM_COL32(120, 220, 120, 220), false);
			mark(course.To, IM_COL32(242, 182, 61, 220), true);
		}
		if (s_GymFromSet) {
			mark(s_GymFrom, IM_COL32(120, 220, 120, 255), false);
		}
		if (s_GymToSet) {
			mark(s_GymTo, IM_COL32(242, 182, 61, 255), true);
		}
	}

	void GymTab() {
		GymLoad();
		ImGui::TextWrapped("Courses for the AI on this map. Click a start and a goal on the map with the two tools, name the course and add it. A run puts a unit of the kind picked under Spawn (that side, that loadout) at the start, sends it to the goal and times it. The courses are kept in Userdata/Gyms, and the test harness can run them too.");
		ToolButtons({Tool::GymStart, Tool::GymGoal});
		ImGui::Text("Start: %s", s_GymFromSet ? (std::to_string(static_cast<int>(s_GymFrom.m_X)) + "," + std::to_string(static_cast<int>(s_GymFrom.m_Y))).c_str() : "(click with the start tool)");
		ImGui::Text("Goal: %s", s_GymToSet ? (std::to_string(static_cast<int>(s_GymTo.m_X)) + "," + std::to_string(static_cast<int>(s_GymTo.m_Y))).c_str() : "(click with the goal tool)");
		ImGui::SetNextItemWidth(-1.0F);
		ImGui::InputTextWithHint("##gymname", "Course name", s_GymName, sizeof(s_GymName));
		if (ToolUI::Button("Add course", ImVec2(-1.0F, 0.0F)) && s_GymFromSet && s_GymToSet) {
			GymCourse course;
			course.Name = s_GymName[0] ? s_GymName : ("Course " + std::to_string(s_GymCourses.size() + 1));
			course.From = s_GymFrom;
			course.To = s_GymTo;
			s_GymCourses.push_back(course);
			GymSave();
			s_GymName[0] = 0;
			s_GymFromSet = false;
			s_GymToSet = false;
		}
		ImGui::Separator();
		if (ToolUI::Button("Run all", ImVec2(ImGui::GetContentRegionAvail().x * 0.5F - ImGui::GetStyle().ItemSpacing.x * 0.5F, 0.0F))) {
			QueueSimChange(Tool::GymRun, -1);
		}
		ImGui::SameLine();
		if (ToolUI::Button("Remove gym units", ImVec2(-1.0F, 0.0F))) {
			QueueSimChange(Tool::GymRemove);
		}
		int remove = -1;
		for (int i = 0; i < static_cast<int>(s_GymCourses.size()); ++i) {
			GymCourse& course = s_GymCourses[i];
			ImGui::PushID(i);
			ImGui::Separator();
			ImGui::Text("%s", course.Name.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Run")) {
				QueueSimChange(Tool::GymRun, i);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("X")) {
				remove = i;
			}
			ImGui::TextDisabled("%d,%d to %d,%d", static_cast<int>(course.From.m_X), static_cast<int>(course.From.m_Y), static_cast<int>(course.To.m_X), static_cast<int>(course.To.m_Y));
			if (!course.Result.empty()) {
				ImGui::TextWrapped("%s", course.Result.c_str());
			}
			ImGui::PopID();
		}
		if (remove >= 0) {
			s_GymCourses.erase(s_GymCourses.begin() + remove);
			s_GymRuns.clear();
			GymSave();
		}
		ImGui::Separator();
		ImGui::TextWrapped("Settings for this gym, a \"Key = Value\" a line as in Settings.ini (TerrainCollapse = 0 keeps the buildings up). They take effect here when applied, and whenever the map is opened through Tools/RenderTest/Gym.ps1.");
		ImGui::InputTextMultiline("##gymsettings", s_GymSettings, sizeof(s_GymSettings), ImVec2(-1.0F, ImGui::GetTextLineHeight() * 5.0F));
		if (ToolUI::Button("Apply and save settings", ImVec2(-1.0F, 0.0F))) {
			GymApplySettings();
			GymSaveSettings();
		}
	}
} // namespace SandboxDetail
