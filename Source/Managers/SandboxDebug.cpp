// The sandbox's debug overlays (Sandbox::DrawDebug).

#include "SandboxInternal.h"

namespace {
	/// A dashed line between window positions, as the orders overlay draws an order waiting for the next update.
	void DashedLine(ImDrawList* drawList, const ImVec2& from, const ImVec2& to, ImU32 color, float thickness) {
		float dx = to.x - from.x;
		float dy = to.y - from.y;
		float length = std::sqrt(dx * dx + dy * dy);
		if (length < 1.0F || length > 6000.0F) {
			drawList->AddLine(from, to, color, thickness);
			return;
		}
		for (float t = 0.0F; t < length; t += 12.0F) {
			float end = std::min(t + 6.0F, length);
			drawList->AddLine(ImVec2(from.x + dx * t / length, from.y + dy * t / length), ImVec2(from.x + dx * end / length, from.y + dy * end / length), color, thickness);
		}
	}

	/// Whether a window position is on the game's picture, give or take a margin.
	bool OnPicture(const ImVec2& at, float margin) {
		GameViewRect view = g_WindowMan.GetGameViewRect();
		return at.x > view.x - margin && at.x < view.x + view.w + margin && at.y > view.y - margin && at.y < view.y + view.h + margin;
	}

	/// The sim state readout (SettingsMan::ShowSandboxSimState), in the bottom right of the picture: what holds the world still (the sandbox's
	/// tools, photo mode, Freeze simulation, the game's own pause), the AI pause, how many sim updates ran for this drawn frame, the tool uses
	/// queued, applied last update and steps still wanted, and the time scale against the speed the sim actually manages.
	void DrawSimState() {
		if (!g_SettingsMan.ShowSandboxSimState()) {
			return;
		}
		static long long lastCount = -1;
		long long count = g_TimerMan.GetSimUpdateCount();
		long long thisFrame = lastCount < 0 ? 0 : count - lastCount;
		lastCount = count;
		std::string pausedBy;
		auto because = [&pausedBy](const char* what) { pausedBy += pausedBy.empty() ? what : std::string(", ") + what; };
		if (s_PausedByMenus) {
			because("sandbox tools open");
		}
		if (g_DebugMan.IsPhotoModeOpen()) {
			because("photo mode");
		}
		if (g_DebugMan.IsSimFrozen()) {
			because("Freeze simulation");
		}
		if (g_ActivityMan.ActivityPaused()) {
			because("game paused");
		}
		std::vector<std::string> lines;
		lines.push_back(g_TimerMan.IsSimPaused() ? "world paused" + (pausedBy.empty() ? std::string() : " by " + pausedBy) : std::string("world running") + (pausedBy.empty() ? "" : " (asked to pause by " + pausedBy + ")"));
		if (Controller::IsAIPaused()) {
			lines.push_back("AI paused");
		}
		lines.push_back("sim updates this frame " + std::to_string(thisFrame) + ", update " + std::to_string(count));
		lines.push_back("tool uses queued " + std::to_string(s_Queue.size()) + ", applied last update " + std::to_string(s_StrokesApplied) + ", steps wanted " + std::to_string(s_StepsWanted));
		char speed[64];
		std::snprintf(speed, sizeof(speed), "time scale x%.2f, sim running at x%.2f", g_TimerMan.GetTimeScale(), g_TimerMan.GetSimSpeed());
		lines.push_back(speed);
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		GameViewRect view = g_WindowMan.GetGameViewRect();
		float lineHeight = ImGui::GetTextLineHeight();
		float width = 0.0F;
		for (const std::string& line: lines) {
			width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
		}
		ImVec2 corner(std::floor(view.x + view.w - width - 12.0F), std::floor(view.y + view.h - lineHeight * static_cast<float>(lines.size()) - 12.0F));
		drawList->AddRectFilled(ImVec2(corner.x - 4.0F, corner.y - 4.0F), ImVec2(corner.x + width + 4.0F, corner.y + lineHeight * static_cast<float>(lines.size()) + 4.0F), IM_COL32(10, 12, 10, 190));
		for (size_t i = 0; i < lines.size(); ++i) {
			ImU32 color = i == 0 && g_TimerMan.IsSimPaused() ? IM_COL32(150, 210, 255, 255) : lines[i] == "AI paused" ? IM_COL32(255, 210, 80, 255) : IM_COL32(230, 230, 220, 255);
			drawList->AddText(ImVec2(corner.x, corner.y + lineHeight * static_cast<float>(i)), color, lines[i].c_str());
		}
	}

	/// The incoming and effects overlay (SettingsMan::ShowSandboxEffects): each thing on its way in from the sky as its line from where it
	/// comes to where it's aimed, the point it will hit (the first ground on its line) with its crater, its preset and the updates it has
	/// left; each effect put down with its name and its main light's reach as a ring (storm cells with their next flash); each water spring as
	/// its pour. With the pointer over an effect or a spring, Delete removes that one (the window's buttons only take the last or all).
	void DrawEffectsOverlay() {
		if (!g_SettingsMan.ShowSandboxEffects()) {
			return;
		}
		// The reach of each effect's main light, in EffectKind's order; 0 for those that make no light.
		static const float lightReach[] = {320.0F, 560.0F, 280.0F, 240.0F, 220.0F, 460.0F, 640.0F, 340.0F, 320.0F, 165.0F, 62.0F, 240.0F, 150.0F, 18.0F, 200.0F, 80.0F, 0.0F, 0.0F, 0.0F, 90.0F, 0.0F, 0.0F, 130.0F, 0.0F, 0.0F};
		static_assert(sizeof(lightReach) / sizeof(lightReach[0]) == static_cast<size_t>(EffectKind::Count), "a reach for each effect");
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		const ImVec2& mouse = ImGui::GetIO().MousePos;
		auto label = [drawList](const ImVec2& at, const std::string& text, ImU32 color) {
			ImVec2 size = ImGui::CalcTextSize(text.c_str());
			ImVec2 corner(std::floor(at.x - size.x * 0.5F), std::floor(at.y));
			drawList->AddRectFilled(ImVec2(corner.x - 2.0F, corner.y), ImVec2(corner.x + size.x + 2.0F, corner.y + size.y), IM_COL32(10, 12, 10, 170));
			drawList->AddText(corner, color, text.c_str());
		};
		auto pointedAt = [&mouse](const ImVec2& at) { return std::abs(mouse.x - at.x) < 10.0F && std::abs(mouse.y - at.y) < 10.0F; };
		bool removeKey = !ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Delete, false);
		ImU32 incomingColor = IM_COL32(255, 150, 60, 230);
		for (const Incoming& incoming: s_Incoming) {
			Vector now = incoming.Id != 0 ? incoming.LastPos : incoming.From;
			Vector line = g_SceneMan.ShortestDistance(incoming.From, incoming.Target, g_SceneMan.SceneWrapsX());
			float length = line.GetMagnitude();
			Vector direction = length > 0.01F ? line / length : Vector(0.0F, 1.0F);
			// Where it will hit: the first ground on its line from here, looked for as far as its target (and no more than 1500 px ahead).
			Vector impact = incoming.Target;
			float left = std::min(g_SceneMan.ShortestDistance(now, incoming.Target, g_SceneMan.SceneWrapsX()).GetMagnitude(), 1500.0F);
			for (float ahead = 0.0F; ahead <= left; ahead += 4.0F) {
				Vector probe = now + direction * ahead;
				if (probe.m_Y > 0.0F && g_SceneMan.GetTerrMatter(probe.GetFloorIntX(), probe.GetFloorIntY()) != g_MaterialAir) {
					impact = probe;
					break;
				}
			}
			ImVec2 from = ToScreen(incoming.From);
			ImVec2 at = ToScreen(now);
			ImVec2 hit = ToScreen(impact);
			drawList->AddLine(from, at, IM_COL32(255, 150, 60, 110), 1.0F);
			drawList->AddLine(at, hit, incomingColor, 1.5F);
			drawList->AddCircle(hit, std::max(static_cast<float>(incoming.Crater) / scale, 4.0F), IM_COL32(255, 80, 50, 230), 0, 2.0F);
			std::string text = incoming.Preset + (incoming.Delay > 0 ? "  in " + std::to_string(incoming.Delay) + " updates" : "  life " + std::to_string(incoming.Life)) + (incoming.Crater > 0 ? "  crater " + std::to_string(incoming.Crater) : "");
			label(ImVec2(at.x, at.y + 8.0F), text, incomingColor);
		}
		int removeEffect = -1;
		for (size_t i = 0; i < s_Effects.size(); ++i) {
			const PlacedEffect& effect = s_Effects[i];
			ImVec2 at = ToScreen(effect.Position);
			int kind = std::clamp(static_cast<int>(effect.Kind), 0, static_cast<int>(EffectKind::Count) - 1);
			bool hovered = pointedAt(at);
			ImU32 color = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 160, 255, 230);
			if (lightReach[kind] > 0.0F) {
				drawList->AddCircle(at, lightReach[kind] / scale, IM_COL32(200, 160, 255, 90), 48, 1.0F);
			}
			drawList->AddRect(ImVec2(at.x - 5.0F, at.y - 5.0F), ImVec2(at.x + 5.0F, at.y + 5.0F), color, 0.0F, 0, 2.0F);
			std::string text = std::to_string(i + 1) + " " + c_Effects[kind].Name;
			if (effect.Kind == EffectKind::StormCell) {
				char wait[32];
				std::snprintf(wait, sizeof(wait), "  next flash %.1fs", static_cast<float>(std::max(effect.Wait, 0)) / 60.0F);
				text += wait;
			}
			if (hovered) {
				text += "  (Delete: remove)";
				if (removeKey) {
					removeEffect = static_cast<int>(i);
				}
			}
			label(ImVec2(at.x, at.y + 7.0F), text, color);
		}
		int removeSpawner = -1;
		for (size_t i = 0; i < s_WaterSpawners.size(); ++i) {
			const WaterSpawner& spawner = s_WaterSpawners[i];
			ImVec2 at = ToScreen(spawner.Position);
			bool hovered = pointedAt(at);
			ImU32 color = hovered ? IM_COL32(255, 255, 255, 255) : MaterialMarkColor(spawner.Liquid);
			drawList->AddCircle(at, std::max(static_cast<float>(spawner.Radius) / scale, 4.0F), color, 0, 2.0F);
			// (A filled dot in what it pours, so a row of springs reads as water here, lava there.)
			drawList->AddCircleFilled(at, 3.0F, MaterialMarkColor(spawner.Liquid, spawner.On ? 255 : 110));
			std::string text = spawner.Liquid + " " + std::to_string(spawner.Radius) + " px" + (spawner.On ? "" : " (off)");
			if (hovered) {
				text += "  (Delete: remove)";
				if (removeKey && removeEffect < 0) {
					removeSpawner = static_cast<int>(i);
				}
			}
			label(ImVec2(at.x, at.y + 7.0F), text, color);
		}
		// (Removed from the ImGui frame, as the window's own Remove buttons do.)
		if (removeEffect >= 0) {
			s_Effects.erase(s_Effects.begin() + removeEffect);
		} else if (removeSpawner >= 0) {
			s_WaterSpawners.erase(s_WaterSpawners.begin() + removeSpawner);
		}
	}

	/// The selection and camera overlay (SettingsMan::ShowSandboxSelectionCamera): while a box is dragged, the box as SelectInBox will take it
	/// (scene coordinates as worked out from the view, unwrapped: any part past the scene's seam picks nobody, review S6) with a ring on each
	/// unit it would take; the unit the game controls against the one the sandbox thinks you're in (review S1); the observation target and
	/// the free camera's centre as two crosses; and the view's scale.
	void DrawSelectionCameraOverlay() {
		if (!g_SettingsMan.ShowSandboxSelectionCamera()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		const ImGuiIO& io = ImGui::GetIO();
		float scale = ScenePixelsPerWindowPixel();
		ImVec2 origin = ViewOrigin();
		Vector offset = g_CameraMan.GetOffset(0);
		// Scene to window without wrapping, so the box shows where its scene coordinates really are.
		auto unwrapped = [&](const Vector& scene) { return ImVec2(origin.x + (scene.m_X - offset.m_X) / scale, origin.y + (scene.m_Y - offset.m_Y) / scale); };
		std::vector<std::string> lines;
		if (s_Dragging) {
			Vector start = offset + Vector(s_DragStart.x - origin.x, s_DragStart.y - origin.y) * scale;
			Vector end = offset + Vector(io.MousePos.x - origin.x, io.MousePos.y - origin.y) * scale;
			float left = std::min(start.m_X, end.m_X);
			float right = std::max(start.m_X, end.m_X);
			float top = std::min(start.m_Y, end.m_Y);
			float bottom = std::max(start.m_Y, end.m_Y);
			float width = static_cast<float>(g_SceneMan.GetSceneWidth());
			drawList->AddRect(unwrapped(Vector(left, top)), unwrapped(Vector(right, bottom)), IM_COL32(120, 255, 160, 220), 0.0F, 0, 1.0F);
			if (g_SceneMan.SceneWrapsX() && (left < 0.0F || right > width)) {
				// The part of the box off the scene's x range: units there sit at the other end of the scene's coordinates, so it takes none.
				float from = left < 0.0F ? left : std::max(left, width);
				float to = left < 0.0F ? std::min(right, 0.0F) : right;
				drawList->AddRectFilled(unwrapped(Vector(from, top)), unwrapped(Vector(to, bottom)), IM_COL32(255, 60, 50, 60));
				lines.push_back("box crosses the scene's seam: the red part selects nobody");
			}
			int taken = 0;
			for (const Actor* actor: SandboxAccess::Actors()) {
				const Vector& position = actor->GetPos();
				if (IsCombatant(actor) && !actor->IsInGroup("Brains") && position.m_X >= left && position.m_X <= right && position.m_Y >= top && position.m_Y <= bottom) {
					drawList->AddCircle(ToScreen(position), std::max(actor->GetRadius() / scale, 8.0F), IM_COL32(120, 255, 160, 230), 0, 1.5F);
					++taken;
				}
			}
			lines.push_back("box " + std::to_string(static_cast<int>(left)) + "," + std::to_string(static_cast<int>(top)) + " to " + std::to_string(static_cast<int>(right)) + "," + std::to_string(static_cast<int>(bottom)) + " takes " + std::to_string(taken));
		}
		GameActivity* game = CurrentGame();
		const Actor* controlled = game ? game->GetControlledActor(Players::PlayerOne) : nullptr;
		const Actor* possessed = s_Possessed && g_MovableMan.IsActor(s_Possessed) ? s_Possessed : nullptr;
		if (controlled) {
			drawList->AddCircle(ToScreen(controlled->GetPos()), std::max(controlled->GetRadius() / scale, 10.0F) + 3.0F, IM_COL32(120, 230, 120, 230), 0, 2.0F);
		}
		if (possessed && possessed != controlled) {
			drawList->AddCircle(ToScreen(possessed->GetPos()), std::max(possessed->GetRadius() / scale, 10.0F) + 7.0F, IM_COL32(110, 180, 250, 230), 0, 2.0F);
		}
		lines.push_back("game controls: " + (controlled ? controlled->GetPresetName() + " #" + std::to_string(controlled->GetUniqueID()) : std::string("nobody")));
		lines.push_back(std::string("sandbox thinks you're in: ") + (possessed ? possessed->GetPresetName() + " #" + std::to_string(possessed->GetUniqueID()) : s_Possessed ? std::string("a unit that's gone") : std::string("nobody")) + (s_Possessed != controlled && (s_Possessed || Sandbox::IsGodMode()) ? "  (they differ)" : ""));
		auto cross = [drawList](const ImVec2& at, ImU32 color) {
			drawList->AddLine(ImVec2(at.x - 8.0F, at.y - 8.0F), ImVec2(at.x + 8.0F, at.y + 8.0F), color, 2.0F);
			drawList->AddLine(ImVec2(at.x - 8.0F, at.y + 8.0F), ImVec2(at.x + 8.0F, at.y - 8.0F), color, 2.0F);
		};
		if (game) {
			Vector observing = game->GetObservationTarget(Players::PlayerOne);
			cross(ToScreen(observing), IM_COL32(255, 220, 80, 230));
			lines.push_back("observation target (yellow) " + std::to_string(observing.GetFloorIntX()) + "," + std::to_string(observing.GetFloorIntY()));
		}
		if (Sandbox::IsGodMode()) {
			cross(ToScreen(s_CameraCenter), IM_COL32(90, 230, 255, 230));
			lines.push_back("free camera centre (cyan) " + std::to_string(s_CameraCenter.GetFloorIntX()) + "," + std::to_string(s_CameraCenter.GetFloorIntY()));
		}
		char text[64];
		std::snprintf(text, sizeof(text), "scene pixels per window pixel %.3f", scale);
		lines.push_back(text);
		GameViewRect view = g_WindowMan.GetGameViewRect();
		float lineHeight = ImGui::GetTextLineHeight();
		float width = 0.0F;
		for (const std::string& line: lines) {
			width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
		}
		ImVec2 corner(std::floor(view.x + 8.0F), std::floor(view.y + view.h * 0.35F));
		drawList->AddRectFilled(ImVec2(corner.x - 4.0F, corner.y - 4.0F), ImVec2(corner.x + width + 4.0F, corner.y + lineHeight * static_cast<float>(lines.size()) + 4.0F), IM_COL32(10, 12, 10, 190));
		for (size_t i = 0; i < lines.size(); ++i) {
			bool warn = lines[i].find("seam") != std::string::npos || lines[i].find("(they differ)") != std::string::npos;
			drawList->AddText(ImVec2(corner.x, corner.y + lineHeight * static_cast<float>(i)), warn ? IM_COL32(255, 120, 100, 255) : IM_COL32(230, 230, 220, 255), lines[i].c_str());
		}
	}

	/// The terrain paint audit (SettingsMan::ShowSandboxPaintAudit): the last two dozen changes the paint helpers made, each as its box (dug
	/// and cleared orange, painted and filled green, grey if nothing changed), fading over ten seconds, labelled with the material and whether
	/// falling ground (TerrainCollapse::BeginChange) and liquid (FluidSim::Disturb) were told: a "no" in red is review S13's inconsistency.
	void DrawPaintAudit() {
		if (!g_SettingsMan.ShowSandboxPaintAudit() || s_PaintRecords.empty()) {
			return;
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		long long now = g_TimerMan.GetSimUpdateCount();
		float lineHeight = ImGui::GetTextLineHeight();
		for (size_t i = 0; i < s_PaintRecords.size(); ++i) {
			const PaintRecord& record = s_PaintRecords[i];
			float age = static_cast<float>(std::max(0LL, now - record.At)) / 600.0F;
			if (age >= 1.0F) {
				continue;
			}
			int alpha = static_cast<int>(230.0F * (1.0F - age * 0.7F));
			bool removes = record.Material == "air";
			ImU32 color = !record.Changed ? IM_COL32(160, 160, 160, alpha) : removes ? IM_COL32(255, 160, 60, alpha) : IM_COL32(120, 230, 120, alpha);
			ImVec2 topLeft = ToScreen(record.Area.GetCorner());
			ImVec2 bottomRight(topLeft.x + record.Area.GetWidth() / scale, topLeft.y + record.Area.GetHeight() / scale);
			drawList->AddRect(topLeft, bottomRight, color, 0.0F, 0, 1.5F);
			// Only the newest few are labelled, so a long brush stroke doesn't bury the picture in text.
			if (i + 6 < s_PaintRecords.size()) {
				continue;
			}
			std::string head = std::string(record.Kind) + " " + record.Material + (record.Changed ? "" : " (nothing changed)");
			std::string collapse = std::string("falling ground told: ") + (record.ToldCollapse ? "yes" : "no");
			std::string liquid = std::string("liquid told: ") + (record.ToldLiquid ? "yes" : "no");
			ImVec2 at(bottomRight.x + 4.0F, topLeft.y);
			const std::string* lines[] = {&head, &collapse, &liquid};
			float width = 0.0F;
			for (const std::string* line: lines) {
				width = std::max(width, ImGui::CalcTextSize(line->c_str()).x);
			}
			drawList->AddRectFilled(ImVec2(at.x - 2.0F, at.y), ImVec2(at.x + width + 2.0F, at.y + lineHeight * 3.0F), IM_COL32(10, 12, 10, std::min(alpha, 180)));
			drawList->AddText(at, color, head.c_str());
			// Removing ground needs falling ground told; any change may need liquid told.
			bool collapseMissing = removes && !record.ToldCollapse;
			bool liquidMissing = record.Changed && !record.ToldLiquid;
			drawList->AddText(ImVec2(at.x, at.y + lineHeight), collapseMissing ? IM_COL32(255, 90, 70, alpha) : IM_COL32(220, 220, 210, alpha), collapse.c_str());
			drawList->AddText(ImVec2(at.x, at.y + lineHeight * 2.0F), liquidMissing ? IM_COL32(255, 90, 70, alpha) : IM_COL32(220, 220, 210, alpha), liquid.c_str());
		}
	}

	/// The battle and colony readout (SettingsMan::ShowSandboxAutoBattle), in the top left of the picture: a line for each Battle Director team
	/// that is active or running, with whether it is running, how it fights, the units it has sent, those it has alive (as CountUnits counts
	/// them, a craft's passengers too) and what it has spent. Then each colony building with what it is doing, its training progress and its
	/// units alive, those dead or dying but not yet gone counted apart (review R8).
	void DrawAutoBattleColony() {
		if (!g_SettingsMan.ShowSandboxAutoBattle()) {
			return;
		}
		const ImU32 plain = IM_COL32(230, 230, 220, 255);
		std::vector<std::pair<std::string, ImU32>> lines;
		char text[256];
		for (int side = 0; side < c_Sides; ++side) {
			const BattleTeam& team = s_BattleTeams[side];
			if (!team.Running && !team.Settings.Active) {
				continue;
			}
			const BattleSettings& settings = team.Settings;
			std::string spent = settings.EndlessMoney ? std::to_string(static_cast<int>(team.Spent)) + " (no limit)" : std::to_string(static_cast<int>(team.Spent)) + " of " + std::to_string(settings.Budget);
			std::snprintf(text, sizeof(text), "%s: %s, %s, %d sent, %d alive, spent %s", c_SideNames[side], team.Running ? (team.Broke ? "running, out of money" : "running") : "stopped", c_BattleStyleNames[static_cast<int>(settings.Style)], team.Sent, Sandbox::CountUnits(side), spent.c_str());
			lines.emplace_back(text, c_SideColors[side]);
		}
		for (const Colony::Building& building: Colony::Buildings()) {
			const Colony::Type& type = Colony::GetType(building.What);
			std::string line = std::string(type.Name) + " #" + std::to_string(building.ID) + ": " + (building.Paused ? "paused, " : "") + building.Status;
			if (building.What == Colony::Kind::Barracks) {
				int dying = 0;
				for (const auto& [unit, uniqueID]: building.Alive) {
					if (g_MovableMan.IsActor(unit) && static_cast<long>(unit->GetUniqueID()) == uniqueID && unit->GetStatus() >= Actor::DYING) {
						++dying;
					}
				}
				std::snprintf(text, sizeof(text), "; %s %.0f%%; %d of %d alive (%d dead or dying, not yet gone); %d trained", building.Paid ? "training" : "waiting to pay", building.Progress * 100.0F, static_cast<int>(building.Alive.size()), building.KeepAlive, dying, building.Produced);
				line += text;
			}
			lines.emplace_back(line, building.Team >= 0 && building.Team < c_Sides ? c_SideColors[building.Team] : plain);
		}
		if (lines.empty()) {
			lines.emplace_back("No battle teams and no colony buildings", plain);
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		GameViewRect view = g_WindowMan.GetGameViewRect();
		float lineHeight = ImGui::GetTextLineHeight();
		float width = 0.0F;
		for (const auto& [line, color]: lines) {
			width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
		}
		ImVec2 corner(std::floor(view.x + 12.0F), std::floor(view.y + view.h * 0.08F));
		drawList->AddRectFilled(ImVec2(corner.x - 4.0F, corner.y - 4.0F), ImVec2(corner.x + width + 4.0F, corner.y + lineHeight * static_cast<float>(lines.size()) + 4.0F), IM_COL32(10, 12, 10, 190));
		for (size_t i = 0; i < lines.size(); ++i) {
			drawList->AddText(ImVec2(corner.x, corner.y + lineHeight * static_cast<float>(i)), lines[i].second, lines[i].first.c_str());
		}
	}

	/// The character state line (SettingsMan::ShowSandboxCharacterState): over your character's head, what UpdatePlayer keeps track of: whether
	/// you are in it, the updates left before stepping in, flying and how hard it is pinned, its side and whether the AI ignores it (Neutral),
	/// the item it has out with its kit key, and the AI mode it is left in while you are not in it.
	void DrawCharacterState() {
		Actor* actor = g_SettingsMan.ShowSandboxCharacterState() ? GetRef(s_PlayerUnit) : nullptr;
		if (!actor) {
			return;
		}
		static const char* const modeNames[] = {"none", "sentry", "patrol", "go to", "hunt brains", "dig gold", "return", "stay", "scuttle", "deliver", "bomb", "squad"};
		std::string line = s_Possessed == actor ? "you're in it" : "AI has it";
		if (s_PlayerEnterPending > 0) {
			line += ", stepping in within " + std::to_string(s_PlayerEnterPending) + " updates";
		}
		if (s_Flying) {
			line += ", flying";
		}
		if (actor->GetPinStrength() > 0.0F) {
			char pin[48];
			std::snprintf(pin, sizeof(pin), ", pinned %.0f", actor->GetPinStrength());
			line += pin;
		}
		int team = actor->GetTeam();
		line += std::string(", ") + (team >= 0 && team < c_Sides ? c_SideNames[team] : "no side");
		if (actor->IsIgnoredByAI()) {
			line += ", neutral";
		}
		if (const AHuman* human = dynamic_cast<const AHuman*>(actor)) {
			const HeldDevice* held = human->GetEquippedItem();
			std::string heldName = held ? held->GetPresetName() : std::string("nothing");
			auto key = std::find(s_Player.Kit.begin(), s_Player.Kit.end(), heldName);
			line += ", holding " + heldName + (key != s_Player.Kit.end() ? " (key " + std::to_string(key - s_Player.Kit.begin() + 1) + ")" : std::string());
		}
		int mode = actor->GetAIMode();
		line += std::string(", AI mode ") + (mode >= 0 && mode < static_cast<int>(std::size(modeNames)) ? modeNames[mode] : "?");
		ImVec2 at = ToScreen(actor->GetPos() - Vector(0.0F, actor->GetRadius() + 14.0F));
		if (!OnPicture(at, 40.0F)) {
			return;
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		ImVec2 size = ImGui::CalcTextSize(line.c_str());
		ImVec2 corner(std::floor(at.x - size.x * 0.5F), std::floor(at.y - size.y));
		drawList->AddRectFilled(ImVec2(corner.x - 3.0F, corner.y - 2.0F), ImVec2(corner.x + size.x + 3.0F, corner.y + size.y + 2.0F), IM_COL32(10, 12, 10, 190));
		drawList->AddText(corner, team >= 0 && team < c_Sides ? c_SideColors[team] : IM_COL32(230, 230, 220, 255), line.c_str());
	}

	/// The lighting-by-source readout (SettingsMan::ShowLightsBySource), in the bottom left of the picture: the lights registered for the frame
	/// about to be drawn, counted by what registered them (cone lights apart), and how many sim updates ran since the last drawn one. The lights
	/// are cleared each sim update, so whatever that count, it reads "1 x N": only the last update's lights reach the draw (review G1).
	void DrawLightsBySource() {
		if (!g_SettingsMan.ShowLightsBySource()) {
			return;
		}
		static const char* const sourceNames[] = {"other", "objects", "hot spots", "headlamps", "tracers", "scenery lamps", "fire", "sandbox effects", "scripts"};
		static_assert(std::size(sourceNames) == static_cast<size_t>(LightSource::Count));
		std::array<int, static_cast<size_t>(LightSource::Count)> counts{};
		std::array<int, static_cast<size_t>(LightSource::Count)> cones{};
		const std::vector<SceneLight>& lights = g_PostProcessMan.GetSceneLights();
		for (const SceneLight& light: lights) {
			size_t source = std::min(static_cast<size_t>(light.m_Source), counts.size() - 1);
			++counts[source];
			cones[source] += light.m_ConeCos >= -1.0F ? 1 : 0;
		}
		std::vector<std::string> lines;
		lines.push_back("Lights for this frame: 1 x " + std::to_string(lights.size()) + ", from the last sim update only (sim updates since drawn: " + std::to_string(g_TimerMan.SimUpdatesSinceDrawn()) + ")");
		for (size_t source = 0; source < counts.size(); ++source) {
			if (counts[source] > 0) {
				lines.push_back("    " + std::string(sourceNames[source]) + " " + std::to_string(counts[source]) + (cones[source] > 0 ? " (" + std::to_string(cones[source]) + " cones)" : std::string()));
			}
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		GameViewRect view = g_WindowMan.GetGameViewRect();
		float lineHeight = ImGui::GetTextLineHeight();
		float width = 0.0F;
		for (const std::string& line: lines) {
			width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
		}
		ImVec2 corner(std::floor(view.x + 12.0F), std::floor(view.y + view.h - lineHeight * static_cast<float>(lines.size()) - 12.0F));
		drawList->AddRectFilled(ImVec2(corner.x - 4.0F, corner.y - 4.0F), ImVec2(corner.x + width + 4.0F, corner.y + lineHeight * static_cast<float>(lines.size()) + 4.0F), IM_COL32(10, 12, 10, 190));
		for (size_t i = 0; i < lines.size(); ++i) {
			drawList->AddText(ImVec2(corner.x, corner.y + lineHeight * static_cast<float>(i)), IM_COL32(230, 230, 220, 255), lines[i].c_str());
		}
	}

	/// The stroke log (SettingsMan::ShowSandboxStrokeLog), in the top right of the picture: the last 20 tool uses applied, newest at the bottom.
	void DrawStrokeLog() {
		if (!g_SettingsMan.ShowSandboxStrokeLog()) {
			return;
		}
		std::vector<std::string> lines;
		lines.emplace_back(s_StrokeLog.empty() ? "Stroke log: no tool used yet" : "Stroke log (update, tool, where, side, orders, choice x count)");
		lines.insert(lines.end(), s_StrokeLog.begin(), s_StrokeLog.end());
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		GameViewRect view = g_WindowMan.GetGameViewRect();
		float lineHeight = ImGui::GetTextLineHeight();
		float width = 0.0F;
		for (const std::string& line: lines) {
			width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
		}
		ImVec2 corner(std::floor(view.x + view.w - width - 12.0F), std::floor(view.y + view.h * 0.08F));
		drawList->AddRectFilled(ImVec2(corner.x - 4.0F, corner.y - 4.0F), ImVec2(corner.x + width + 4.0F, corner.y + lineHeight * static_cast<float>(lines.size()) + 4.0F), IM_COL32(10, 12, 10, 190));
		for (size_t i = 0; i < lines.size(); ++i) {
			drawList->AddText(ImVec2(corner.x, corner.y + lineHeight * static_cast<float>(i)), i == 0 ? IM_COL32(180, 180, 170, 255) : IM_COL32(230, 230, 220, 255), lines[i].c_str());
		}
	}

	/// The sandbox orders overlay (SettingsMan::SandboxOrdersOverlay): for each unit, the order waiting for the next update as a dashed line to
	/// where it goes, its standing order as a tag over its head (with a line back to its post or place when it's off it), why it was last sent
	/// for two seconds after, and a red flash each time the standing orders send it again.
	void DrawOrdersOverlay() {
		int which = g_SettingsMan.SandboxOrdersOverlay();
		if (which == 0) {
			return;
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		float scale = ScenePixelsPerWindowPixel();
		float lineHeight = ImGui::GetTextLineHeight();
		long long now = g_TimerMan.GetSimUpdateCount();
		for (const Actor* actor: SandboxAccess::Actors()) {
			if (!IsCombatant(actor)) {
				continue;
			}
			if (which == 1 && !actor->IsDebugInspected() && std::none_of(s_Selected.begin(), s_Selected.end(), [actor](const UnitRef& ref) { return GetRef(ref) == actor; })) {
				continue;
			}
			ImVec2 at = ToScreen(actor->GetPos());
			if (!OnPicture(at, 150.0F)) {
				continue;
			}
			ImU32 side = c_SideColors[actor->GetTeam()];
			for (const PendingOrder& order: s_PendingOrders) {
				if (order.Unit.Unit == actor) {
					DashedLine(drawList, at, ToScreen(order.Waypoint), IM_COL32(255, 255, 255, 220), 1.5F);
				}
			}
			std::string tag;
			bool hasPost = false;
			Vector post;
			if (actor->GetOrderTargetID() != 0) {
				tag = "ATTACK #" + std::to_string(static_cast<long long>(actor->GetOrderTargetID()));
			} else if (actor->GetOrderHasAttackPlace()) {
				post = actor->GetOrderAttackPlace();
				hasPost = true;
				tag = "ATTACK@ " + std::to_string(post.GetFloorIntX()) + "," + std::to_string(post.GetFloorIntY());
			} else if (actor->GetOrderAttack()) {
				tag = "ATTACK";
			} else if (actor->GetOrderHasPost()) {
				post = actor->GetOrderPost();
				hasPost = true;
				tag = "DEFEND";
			} else if (actor->GetAIMode() == Actor::AIMODE_GOTO) {
				const MovableObject* target = actor->GetMOMoveTarget();
				const Actor* leader = target && g_MovableMan.ValidMO(target) ? dynamic_cast<const Actor*>(target) : nullptr;
				tag = leader && leader->GetTeam() == actor->GetTeam() ? "GUARD #" + std::to_string(leader->GetUniqueID()) : std::string("MOVE");
			} else if (actor->GetAIMode() == Actor::AIMODE_SENTRY) {
				tag = "HOLD";
			}
			if (hasPost && g_SceneMan.ShortestDistance(actor->GetPos(), post, g_SceneMan.SceneWrapsX()).GetMagnitude() > 30.0F) {
				ImVec2 postAt = ToScreen(post);
				drawList->AddLine(at, postAt, side, 1.5F);
				drawList->AddCircle(postAt, 5.0F, side, 0, 2.0F);
			}
			std::string why;
			if (auto note = s_SendNotes.find(actor->GetUniqueID()); note != s_SendNotes.end()) {
				long long ago = now - note->second.At;
				if (ago >= 0 && ago < 120) {
					why = std::string(note->second.Resend ? "sent again: " : "sent: ") + note->second.Reason;
				}
				if (note->second.Resend && ago >= 0 && ago < 20) {
					int alpha = static_cast<int>(230.0F * (1.0F - static_cast<float>(ago) / 20.0F));
					drawList->AddCircle(at, std::max(actor->GetRadius() / scale, 10.0F) + 4.0F, IM_COL32(255, 60, 50, alpha), 0, 3.0F);
				}
			}
			float top = at.y - std::max(actor->GetRadius() / scale, 10.0F) - 6.0F;
			for (const std::string* line: {&why, &tag}) {
				if (line->empty()) {
					continue;
				}
				top -= lineHeight;
				ImVec2 size = ImGui::CalcTextSize(line->c_str());
				ImVec2 corner(std::floor(at.x - size.x * 0.5F), std::floor(top));
				drawList->AddRectFilled(ImVec2(corner.x - 2.0F, corner.y), ImVec2(corner.x + size.x + 2.0F, corner.y + size.y), IM_COL32(10, 12, 10, 170));
				drawList->AddText(corner, line == &tag ? side : IM_COL32(230, 230, 220, 255), line->c_str());
			}
		}
	}
} // namespace

void Sandbox::DrawDebug() {
	if (!g_ActivityMan.GetActivity() || !g_SceneMan.GetScene()) {
		return;
	}
	DrawOrdersOverlay();
	DrawSimState();
	DrawEffectsOverlay();
	DrawSelectionCameraOverlay();
	DrawPaintAudit();
	DrawAutoBattleColony();
	DrawCharacterState();
	DrawLightsBySource();
	DrawStrokeLog();
}
