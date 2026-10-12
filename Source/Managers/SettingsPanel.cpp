#include "DebugMan.h"
#include "Weather.h"
#include "Actor.h"
#include "ActivityMan.h"
#include "MovableMan.h"
#include "ActorFire.h"
#include "ActorWater.h"
#include "CameraMan.h"
#include "Controller.h"
#include "EffectsParticles.h"
#include "FluidSim.h"
#include "ThreatMemory.h"
#include "GasGrid.h"
#include "AirPressure.h"
#include "MagicEffects.h"
#include "FrameMan.h"
#include "ModernHUD.h"
#include "Notices.h"
#include "PathFinder.h"
#include "Material.h"
#include "MOSParticle.h"
#include "MOPixel.h"
#include "PerformanceMan.h"
#include "PostProcessMan.h"
#include "RenderTarget.h"
#include "Texture.h"
#include "Sandbox.h"
#include "Scene.h"
#include "SceneLighting.h"
#include "SceneMan.h"
#include "SettingsMan.h"
#include "SmokeGrid.h"
#include "TerrainCollapse.h"
#include "ZeroGravity.h"
#include "TerrainTrees.h"
#include "TerrainFire.h"
#include "RopeSim.h"
#include "TerrainCandle.h"
#include "WeatherLightning.h"
#include "TextOverlay.h"
#include "TimerMan.h"
#include "UnitSpeech.h"
#include "UnitSpeechVoice.h"
#include "WindowMan.h"

#include "imgui/imgui.h"
#include "SDL3/SDL_keyboard.h"
#include "ToolWidgets.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace RTE;

// The settings panel: everything that can be tuned while the game runs, in one place, sorted into categories, searchable, and kept as named presets.
// Its controls are made through the small wrappers below instead of ImGui's own, so that a search can leave out the ones that don't match.
namespace {
	char s_Search[64] = "";
	bool s_Searching = false;
	bool s_CategoryMatches = false; //!< The search matches the category's own name, so all of it shows.
	bool s_LastShown = true; //!< Whether the control just made was shown, for its tooltip.
	int s_ShownCount = 0;
	const char* s_PendingHeading = nullptr; //!< While searching: the category's name, drawn above the first of its controls that matches.
	char s_PresetName[64] = "";
	std::vector<std::string> s_Presets;
	bool s_PresetsListed = false;
	std::string s_PresetMessage;

	bool ContainsIgnoringCase(const char* text, const char* part) {
		size_t partLength = std::strlen(part);
		if (partLength == 0) {
			return true;
		}
		for (const char* at = text; *at && !(at[0] == '#' && at[1] == '#'); ++at) {
			size_t i = 0;
			while (i < partLength && at[i] && std::tolower(static_cast<unsigned char>(at[i])) == std::tolower(static_cast<unsigned char>(part[i]))) {
				++i;
			}
			if (i == partLength) {
				return true;
			}
		}
		return false;
	}

	/// Decides whether a control is shown, from its label.
	bool Shown(const char* label) {
		s_LastShown = !s_Searching || s_CategoryMatches || ContainsIgnoringCase(label, s_Search);
		if (s_LastShown) {
			++s_ShownCount;
			if (s_PendingHeading) {
				ImGui::SeparatorText(s_PendingHeading);
				s_PendingHeading = nullptr;
			}
		}
		return s_LastShown;
	}

	bool Slider(const char* label, float* value, float low, float high, const char* format = "%.2f", ImGuiSliderFlags flags = 0) { return Shown(label) && ImGui::SliderFloat(label, value, low, high, format, flags); }
	bool SliderI(const char* label, int* value, int low, int high, const char* format = "%d", ImGuiSliderFlags flags = 0) { return Shown(label) && ImGui::SliderInt(label, value, low, high, format, flags); }
	bool Check(const char* label, bool* value) { return Shown(label) && ToolUI::Checkbox(label, value); }
	bool Combo(const char* label, int* value, const char* items) { return Shown(label) && ImGui::Combo(label, value, items); }
	bool Tint(const char* label, float* value) { return Shown(label) && ImGui::ColorEdit3(label, value, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR); }

	/// A checkbox for something that is read and set through functions.
	template <typename Setter> void Toggle(const char* label, bool value, Setter set) {
		if (Check(label, &value)) {
			set(value);
		}
	}

	/// A tooltip for the control just made.
	void Tip(const char* text) {
		if (s_LastShown) {
			ImGui::SetItemTooltip("%s", text);
		}
	}

	/// Whether the things that aren't settings (headings, buttons, readouts) are shown: not in a search, unless it is for the whole category.
	bool Plain() { return !s_Searching || s_CategoryMatches; }

	/// The key that pauses and resumes the AI: shown, and changed by clicking and pressing the new one (Escape keeps the old, Backspace leaves none).
	void PauseAIKey() {
		static bool s_Taking = false;
		static bool s_Armed = false; //!< Every key let go since the click, so a key still held from before isn't taken.
		static int s_LastFrame = -1;
		// Waiting stops when the control wasn't there last frame (the panel closed, another page or a search).
		if (s_LastFrame != ImGui::GetFrameCount() - 1) {
			s_Taking = false;
		}
		s_LastFrame = ImGui::GetFrameCount();
		if (!Shown("Pause AI key")) {
			s_Taking = false;
			g_SettingsMan.SetTakingKey(false);
			return;
		}
		const std::string& key = g_SettingsMan.GetPauseAIKey();
		std::string label = s_Taking ? std::string("Press a key...") : (key.empty() ? std::string("None") : key);
		if (ImGui::Button((label + "##PauseAIKey").c_str(), ImVec2(ImGui::GetFontSize() * 9.0F, 0.0F))) {
			s_Taking = !s_Taking;
			s_Armed = false;
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("Pause AI key");
		Tip("Click, then press the key (with Ctrl, Alt or Shift if wanted) that pauses and resumes the AI in any game. Escape, or a second click, keeps the key there was; Backspace leaves none.");
		if (s_Taking) {
			int count = 0;
			const bool* keys = SDL_GetKeyboardState(&count);
			int pressed = SDL_SCANCODE_UNKNOWN;
			for (int i = SDL_SCANCODE_A; i < count && i < SDL_SCANCODE_COUNT; ++i) {
				if (keys[i] && !(i >= SDL_SCANCODE_LCTRL && i <= SDL_SCANCODE_RGUI)) {
					pressed = i;
					break;
				}
			}
			if (!s_Armed) {
				s_Armed = pressed == SDL_SCANCODE_UNKNOWN;
			} else if (pressed == SDL_SCANCODE_ESCAPE) {
				s_Taking = false;
			} else if (pressed == SDL_SCANCODE_BACKSPACE) {
				g_SettingsMan.SetPauseAIKey(std::string());
				s_Taking = false;
			} else if (pressed != SDL_SCANCODE_UNKNOWN) {
				g_SettingsMan.SetPauseAIKey(SettingsMan::KeyComboName(pressed, SDL_GetModState()));
				s_Taking = false;
			}
		}
		// The key press is seen by the game before this panel takes it, so this keeps that press from also pausing the AI.
		g_SettingsMan.SetTakingKey(s_Taking);
	}

	void Heading(const char* text) {
		if (Plain()) {
			ImGui::SeparatorText(text);
		}
	}

	/// A small button after the control just made (and its tooltip) that puts its value back to how the game comes; greyed out while it is.
	void ResetButton(const char* id, float* value, float defaultValue) {
		if (!s_LastShown) {
			return;
		}
		ImGui::SameLine();
		ImGui::PushID(id);
		ImGui::BeginDisabled(*value == defaultValue);
		if (ImGui::SmallButton("Reset")) {
			*value = defaultValue;
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Back to how the game comes.");
		ImGui::PopID();
	}

	/// A slider for how bright a colour is, keeping its tint.
	void Brightness(const char* label, glm::vec3& color, float maxValue) {
		float level = std::max({color.x, color.y, color.z});
		if (Slider(label, &level, 0.0F, maxValue) && level > 0.0F) {
			float current = std::max({color.x, color.y, color.z});
			color = current > 0.0F ? color * (level / current) : glm::vec3(level);
		}
	}

	/// The event looks one by one (G-11), each with a button that plays it once so it can be judged: the blast flash is the one most players
	/// bothered by flashing want off.
	void EventLookSwitches(LightingSettings& settings) {
		struct EventLook {
			const char* Label;
			bool* On;
			int Look;
			const char* Tip;
		};
		const EventLook looks[] = {
		    {"Blast flash", &settings.EventBlastFlash, LightingSettings::LookFlash, "The picture washes out white and warm for a moment after a huge blast. Off if flashing bothers you."},
		    {"Hurt look", &settings.EventHurtLook, LightingSettings::LookHurt, "When your unit is badly hurt the picture drains, darkens at the edges and beats faintly like a pulse."},
		    {"Fire warmth", &settings.EventFireWarmth, LightingSettings::LookWarm, "The picture warms a little standing by a fire."},
		};
		for (const EventLook& look: looks) {
			Check(look.Label, look.On);
			Tip(look.Tip);
			if (!s_LastShown) {
				continue;
			}
			ImGui::PushID(look.Label);
			ImGui::SameLine();
			ImGui::BeginDisabled(!*look.On);
			if (ToolUI::Button("Preview")) {
				g_PostProcessMan.PulseGrade(look.Look, 1.0F, look.Look == LightingSettings::LookFlash ? 40.0F : 300.0F, look.Look == LightingSettings::LookFlash ? 1100.0F : 1800.0F);
			}
			ImGui::SetItemTooltip("Plays this look once, at the event grade strength.");
			ImGui::EndDisabled();
			ImGui::PopID();
		}
	}

	void DrawPresets() {
		if (!s_PresetsListed) {
			s_Presets = g_SettingsMan.ListPresets();
			s_PresetsListed = true;
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55F);
		if (ImGui::BeginCombo("Preset", s_PresetName[0] ? s_PresetName : "Choose one to load...")) {
			if (s_Presets.empty()) {
				ImGui::TextDisabled("None saved yet. Type a name below and press Save.");
			}
			for (const std::string& name: s_Presets) {
				if (ImGui::Selectable(name.c_str(), name == s_PresetName)) {
					std::snprintf(s_PresetName, sizeof(s_PresetName), "%s", name.c_str());
					bool loaded = g_SettingsMan.LoadPreset(name);
					s_PresetMessage = loaded ? "Loaded \"" + name + "\"." : "Could not load \"" + name + "\".";
					Notices::Post(loaded ? "Preset \"" + name + "\" loaded." : "Could not load the preset \"" + name + "\".");
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("A preset holds every setting in this panel, from the look, the time and weather, water, fire and how the ground falls to the AI, the HUD, the overlays and the game speed, and the game's own settings from the Settings menu (items shown, map wrapping, screen shake...).\nThey are files in Userdata/Presets, so they can be copied and shared.");
		ImGui::SameLine();
		const std::string& startupPreset = g_SettingsMan.GetStartupPreset();
		bool loadsAtStart = s_PresetName[0] && startupPreset == s_PresetName;
		ImGui::BeginDisabled(s_PresetName[0] == 0 || (!loadsAtStart && std::find(s_Presets.begin(), s_Presets.end(), std::string(s_PresetName)) == s_Presets.end()));
		if (ImGui::Checkbox("Load at start", &loadsAtStart)) {
			g_SettingsMan.SetStartupPreset(loadsAtStart ? s_PresetName : "");
			g_SettingsMan.UpdateSettingsFile();
			s_PresetMessage = loadsAtStart ? std::string("\"") + s_PresetName + "\" loads every time the game starts." : "No preset loads at start.";
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Loads this preset every time the game starts, over Settings.ini, so you don't have to pick it each time.\nThe game speed, freezing, the AI pause and the debug views in it are left off at start.\nChanges made later are only kept in it if it is saved again.");
		if (!startupPreset.empty()) {
			ImGui::TextDisabled("Loads at start: %s", startupPreset.c_str());
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55F);
		ImGui::InputTextWithHint("##PresetName", "Name for a preset...", s_PresetName, sizeof(s_PresetName));
		ImGui::SameLine();
		ImGui::BeginDisabled(s_PresetName[0] == 0);
		if (ToolUI::Button("Save")) {
			std::string saved = g_SettingsMan.SavePreset(s_PresetName);
			s_PresetMessage = saved.empty() ? "Could not save it." : "Saved \"" + saved + "\".";
			Notices::Post(saved.empty() ? "Could not save the preset." : "Preset \"" + saved + "\" saved.");
			std::snprintf(s_PresetName, sizeof(s_PresetName), "%s", saved.c_str());
			s_PresetsListed = false;
		}
		ImGui::SetItemTooltip("Keeps every setting as it is now under this name. Saving under a name that exists replaces it.");
		ImGui::SameLine();
		if (ToolUI::Button("Delete")) {
			s_PresetMessage = g_SettingsMan.DeletePreset(s_PresetName) ? std::string("Deleted \"") + s_PresetName + "\"." : "There is no preset of that name.";
			if (g_SettingsMan.GetStartupPreset() == s_PresetName) {
				g_SettingsMan.SetStartupPreset("");
				g_SettingsMan.UpdateSettingsFile();
			}
			s_PresetName[0] = 0;
			s_PresetsListed = false;
		}
		ImGui::EndDisabled();
		if (ToolUI::Button("Usual settings")) {
			g_PostProcessMan.GetLightingSettings() = LightingSettings();
			TerrainCollapse::GetTuning() = TerrainCollapse::Tuning();
			AirPressure::GetTuning() = AirPressure::Tuning();
			MagicEffects::GetTuning() = MagicEffects::Tuning();
			ZeroGravity::ResetSettings();
			g_SettingsMan.GibChunks() = SettingsMan::GibChunkTuning();
			s_PresetMessage = "Everything is back to how the game comes.";
		}
		ImGui::SetItemTooltip("Puts every setting here back to how the game comes.");
		ImGui::SameLine();
		if (ToolUI::Button("Keep for next time")) {
			g_PostProcessMan.AdoptAtmosphereAsPlayers();
			g_SettingsMan.UpdateSettingsFile();
			s_PresetMessage = "The game will start like this.";
		}
		ImGui::SetItemTooltip("Writes the settings as they are now to Settings.ini, so the game starts with them.");
		if (!s_PresetMessage.empty()) {
			ImGui::TextDisabled("%s", s_PresetMessage.c_str());
		}
	}
} // namespace

void DebugMan::SettingsGUI() {
	if (!BeginPanel("Settings (F6)###Settings", &m_ShowWorldDebug, PanelSide::Right)) {
		EndPanel();
		return;
	}
	LightingSettings& settings = g_PostProcessMan.GetLightingSettings();

	auto timeAndWeather = [&]() {
		Slider("Hour", &settings.TimeOfDay, 0.0F, 24.0F);
		if (Plain()) {
			const std::pair<const char*, float> hours[] = {{"Midnight", 0.0F}, {"Dawn", 6.0F}, {"Noon", 12.0F}, {"Dusk", 19.0F}, {"Night", 22.0F}};
			for (size_t i = 0; i < std::size(hours); ++i) {
				if (i > 0) {
					ImGui::SameLine();
				}
				if (ToolUI::Button(hours[i].first)) {
					settings.TimeOfDay = hours[i].second;
				}
			}
		}
		bool timeFlows = settings.DayLengthMinutes > 0.0F;
		if (Check("Time passes", &timeFlows)) {
			settings.DayLengthMinutes = timeFlows ? 10.0F : 0.0F;
		}
		if (timeFlows) {
			Slider("Day length (minutes)", &settings.DayLengthMinutes, 0.5F, 60.0F, "%.1f", ImGuiSliderFlags_Logarithmic);
		}
		Heading("Weather");
		{
			// Every Weather preset: the built-in four, then the others (Base.rte's own and mods'), unless those are turned off.
			std::string weatherItems = Weather::GetComboItems(settings.CustomWeather);
			if (Combo("Precipitation", &settings.WeatherType, weatherItems.c_str())) {
				// A pick made here wins over a saved custom weather still waiting for its preset to load.
				settings.WeatherName.clear();
			}
		}
		Slider("Weather intensity", &settings.WeatherIntensity, 0.0F, 1.0F);
		Slider("Weather speed", &settings.WeatherSpeed, 0.1F, 3.0F, "%.2fx");
		Tip("How fast the rain, snow, ash and dust move: their fall, how the wind carries them, and their sway, swirl and flicker. They keep their angle, so shelter and splashes still line up. 1: as the weather has it.");
		Check("Weather everywhere", &settings.WeatherEverywhere);
		Tip("The rain, snow, ash and dust show everywhere but over the terrain and its back walls: under roofs and overhangs and in cave mouths too, wherever the sky or the scenery behind shows. The ground still only gets wet or snowy where the weather reaches it.");
		Check("Weather over the background", &settings.WeatherOverBackground);
		Tip("The rain, snow, ash and dust are drawn over the sky and the distant scenery. Off: they only show in front of the terrain's back walls, units and the rest of the scene, not painted over the background.");
		Slider("Wind", &settings.Wind, -400.0F, 400.0F, "%.0f px/s");
		Tip("The wind's steady strength and way: negative blows left. The natural wind below makes it gust and wander around this.");
		{
			AirPressure::Tuning& air = AirPressure::GetTuning();
			Slider("Wind gusts", &air.Gusts, 0.0F, 3.0F, "%.2fx");
			Tip("How much the wind gusts and lulls every few seconds: at 1 gusts blow about half again as hard, at 2 nearly twice. 0: a steady wind.");
			Slider("Wind shifts", &air.Shifts, 0.0F, 3.0F, "%.2fx");
			Tip("How much the wind's strength wanders over a minute or so, and the light breeze's way with it. 0: it stays as set.");
			Slider("Light breeze", &air.Breeze, 0.0F, 100.0F, "%.0f px/s");
			Tip("A breeze that blows even with the wind at 0, wandering in strength and now and then turning about. 0: still air is still.");
			if (Plain()) {
				ImGui::TextDisabled("Blowing now: %.0f px/s", AirPressure::GetNaturalWind());
			}
		}
		Slider("Weather's own light", &settings.WeatherLight, 0.0F, 1.5F);
		Tip("The least light rain, snow, ash and dust are drawn with, so they show on a dark night.");
		Check("More weather types", &settings.CustomWeather);
		Tip("Weather beyond rain, snow, ash and dust: the game's own extra kinds and any mods add (AddWeather = Weather). Off: the menu lists the four, and other weather falls clear.");
		if (settings.CustomWeather) {
			Slider("Weather glow", &settings.WeatherGlow, 0.0F, 2.0F);
			Tip("How much light glowing weather gives off (acid rain, embers, sparks). 0: it's only lit, like rain.");
		}
		Slider("Rain splashes", &settings.RainSplashes, 0.0F, 2.0F);
		Tip("Little splashes where rain lands on ground, water, roofs and units. 0 for none.");
		Check("Exact shelter from the weather", &settings.ShelterMask);
		Tip("Rain, snow and ash stay out from under overhangs, roofs and caves right to the edge, however high up the shelter is, and slant in on the windward side. Off: drops look a few hundred pixels up for shelter and miss anything higher, as before. On from the Low preset up.");
		if (settings.ShelterMask) {
			Slider("Shelter edge softness", &settings.ShelterSoftness, 0.0F, 2.0F);
		}
		Toggle("Still water freezes over in snow", FluidSim::FreezingEnabled(), [](bool on) { FluidSim::SetFreezingEnabled(on); });
		Check("Living world (sway, snow, wet ground)", &settings.LivingWorld);
		if (int strikes = static_cast<int>(WeatherLightning::GetStrikes()); Combo("Storm lightning", &strikes, "In the sky only\0Strikes the ground, starts fires\0Strikes the ground, fires and hurts units\0")) {
			WeatherLightning::SetStrikes(static_cast<WeatherLightning::Strikes>(std::clamp(strikes, 0, 2)));
		}
		Tip("In heavy rain, now and then a bolt hits a real spot, half the time near someone, and can set grass and wood alight. The same in a replay. In the sky only: flashes, as before.");
		Check("Ground dries place by place", &settings.WetnessMap);
		Tip("Rain wets the ground and it dries after the rain, hard rock and concrete slower than earth, and long rain leaves puddles in the dips that reflect lamps and the sky. Off: all exposed ground is equally wet, as before. On from the Medium preset.");
		if (settings.WetnessMap) {
			Slider("Wet earth dries in (seconds)", &settings.WetDrySeconds, 10.0F, 600.0F, "%.0f", ImGuiSliderFlags_Logarithmic);
			Slider("Puddles", &settings.Puddles, 0.0F, 1.0F);
		}
		// Scene makers: keep the time and weather as the scene's own. It's written when the scene is saved from the scene editor.
		if (Scene* scene = g_SceneMan.GetScene(); scene && Plain()) {
			ImGui::SeparatorText("This scene's own time and weather");
			const Scene::Atmosphere& own = scene->GetAtmosphere();
			if (own.TimeOfDay >= 0.0F || own.WeatherType >= 0 || !own.WeatherName.empty()) {
				std::vector<std::string> weatherNames = Weather::GetSlotNames();
				std::string weatherName = !own.WeatherName.empty() ? own.WeatherName : (own.WeatherType >= 0 && own.WeatherType < static_cast<int>(weatherNames.size()) ? weatherNames[own.WeatherType] : "default weather");
				ImGui::Text("Set: %.1f h, %s", own.TimeOfDay, weatherName.c_str());
			} else {
				ImGui::TextDisabled("Not set (uses the player's settings)");
			}
			if (ToolUI::Button("Use what is set now")) {
				Scene::Atmosphere atmosphere;
				atmosphere.TimeOfDay = settings.TimeOfDay;
				atmosphere.DayLengthMinutes = settings.DayLengthMinutes;
				atmosphere.WeatherType = settings.WeatherType;
				// A custom weather is kept by name too, since its slot depends on the mods loaded.
				const Weather* weather = settings.WeatherType > Weather::c_BuiltInCount ? Weather::GetSlot(settings.WeatherType) : nullptr;
				atmosphere.WeatherName = weather ? weather->GetPresetName() : "";
				atmosphere.WeatherIntensity = settings.WeatherIntensity;
				atmosphere.Wind = settings.Wind;
				atmosphere.PostShader = own.PostShader;
				scene->SetAtmosphere(atmosphere);
			}
			ImGui::SameLine();
			if (ToolUI::Button("Clear")) {
				// Time and weather only: the scene's mod post pass stays.
				Scene::Atmosphere atmosphere;
				atmosphere.PostShader = own.PostShader;
				scene->SetAtmosphere(atmosphere);
			}
			ImGui::TextDisabled("Saved when the scene is saved in the scene editor.");
		}
	};

	auto skyAndDaylight = [&]() {
		int quality = settings.GraphicsQuality;
		if (Combo("Quality", &quality, "Potato (classic)\0Low\0Medium\0High\0Ultra\0Custom\0")) {
			settings.ApplyQualityPreset(quality);
		}
		Check("Lighting", &settings.Enabled);
		Brightness("Sky light", settings.SkyColor, 2.0F);
		Tint("Sky light colour", &settings.SkyColor.x);
		Slider("Sky takes the hour's colours", &settings.SkyFollowsTime, 0.0F, 1.0F);
		Tip("Away from midday the sky art (painted as a blue day) is recoloured: dark at night, red at dawn and dusk, grey in bad weather. 0: the art is only darkened.");
		Slider("Darker in the dead of night", &settings.DeepNightDarkness, 0.0F, 0.95F);
		Tip("How much less light there is on the scene from eleven to two than at nightfall. 0.5 is half. Lamps, fires and headlamps aren't dimmed.");
		Slider("Sun in the sky", &settings.SunDisc, 0.0F, 2.0F);
		Slider("Sun and moon shadows", &settings.SunShadows, 0.0F, 1.0F);
		Check("Crisp sun shadows", &settings.SunShadowMap);
		Tip("Shadows of hills, bunkers and overhangs are sharp right next to them and softer further off, and they follow the sun as it moves. Off: softer, blockier shadows from the light grid that catch up with the sun over a few frames. On from the Low preset up.");
		if (settings.SunShadowMap) {
			Slider("Sun shadow softness", &settings.SunShadowSoftness, 0.0F, 2.0F);
		}
		Slider("Terrain shadows on the background", &settings.BackgroundShadows, 0.0F, 1.0F);
		Tip("The terrain casts a drop shadow on the scenery behind it, offset away from the sun (or the moon at night), so hills and floating chunks stand out from the backdrop instead of looking flat against it. Never on the sky itself. 0: none, as before. On from the Low preset up.");
		if (settings.BackgroundShadows > 0.0F) {
			Slider("Background shadow offset", &settings.BackgroundShadowLength, 0.25F, 3.0F);
			Tip("How far the shadow is offset from the terrain. The further back the scenery, the further it falls and the softer its edge.");
		}
		Slider("Cloud shadows", &settings.CloudShadows, 0.0F, 1.0F);
		Check("Clouds in the sky", &settings.CloudLayer);
		Tip("Clouds drift across the sky with the wind, the same clouds whose shadows cross the ground. They gather and darken in rain, snow and ash fall, break up again after, and catch the colours of dawn and dusk. Off: an empty sky, as before. On from the Medium preset up.");
		if (settings.CloudLayer) {
			Slider("Cloud cover", &settings.CloudCover, 0.0F, 1.0F);
			Tip("How much of the sky is cloud in clear weather. 0.5 is the spread cloud shadows always had.");
			Slider("Cloud opacity", &settings.CloudOpacity, 0.0F, 1.0F);
			Slider("Cloud size", &settings.CloudSize, 0.4F, 2.5F);
			Tip("How big the clouds are: wider patches with bigger puffs, in a deeper band, and wider shadows on the ground to match. 1 is the usual size.");
			Slider("Cloud height", &settings.CloudHeight, 0.0F, 1.0F);
			Tip("How high in the sky the clouds sit. 1 is along the top of the view, as usual; lower brings the band down towards the horizon. Clouds stay behind the terrain.");
		}
		Slider("God rays", &settings.GodRays, 0.0F, 2.0F);
		Slider("Mist and dust", &settings.FogVolume, 0.0F, 1.5F);
		Tip("Mist and dust hanging in the air: low in open valleys around dawn, steam off water meeting lava, dust after ground collapses, and mist from scripts. It drifts with the wind, is lit by the sky, lamps and fires, and clears over time. 0: none, as before. On from the Medium preset up.");
		if (settings.FogVolume > 0.0F) {
			Slider("Dawn mist", &settings.FogMorningMist, 0.0F, 1.0F);
			Tip("How much mist gathers low in open ground around dawn, a little at night and more in rain.");
			Slider("Mist and dust opacity", &settings.FogOpacity, 0.0F, 1.0F);
			Tip("How much the thickest mist and dust hides what's behind it, units included. Lower lets more of their colour through.");
			Slider("Mist clears after (seconds)", &settings.FogClearSeconds, 3.0F, 120.0F, "%.0f");
		}
		Check("Lightning bolts", &settings.LightningBolts);
		Tip("Lightning (the sandbox's tool and storm cells, and scripts) is drawn as a jagged, forked bolt of light from the sky, flickering twice, lighting up where it strikes and the air along it. Off: the sandbox draws its bolt as a line of particles, as before.");
		Check("Storm flashes", &settings.StormFlashes);
		Tip("Heavy rain, and weather with lightning in it, flashes the whole sky now and then. Turn it off if flashing light bothers you; bolts are drawn as the setting above has them.");
		if (settings.LightningBolts || settings.StormFlashes) {
			Slider("Lightning brightness", &settings.LightningBrightness, 0.0F, 2.0F);
			Tip("How bright the bolt, the light it throws on the ground and air, and a storm's sky flash are. 1: as first made. 0: no flash at all.");
		}
		Slider("Haze", &settings.AtmosphereHaze, 0.0F, 1.0F);
		Tint("Haze colour", &settings.AtmosphereColor.x);
		Slider("Far background blur", &settings.BackgroundBlur, 0.0F, 1.5F);
		Check("Depth of field", &settings.DepthOfField);
		Tip("Blurs what's nearer or further than the focus, like a camera lens: with the focus on the battlefield, the far backgrounds go soft. Off: everything is sharp.");
		if (settings.DepthOfField) {
			Slider("Focus (battlefield to far background)", &settings.DepthOfFieldFocus, 0.0F, 1.0F);
			Slider("Depth of field strength", &settings.DepthOfFieldStrength, 0.0F, 2.0F);
		}
		Check("Tilt-shift", &settings.TiltShift);
		Tip("Blurs the top and bottom of the screen and keeps a band sharp, so the battlefield looks like a model diorama.");
		if (settings.TiltShift) {
			Slider("Sharp band (top to bottom)", &settings.TiltShiftLine, 0.0F, 1.0F);
			Slider("Tilt-shift strength", &settings.TiltShiftStrength, 0.0F, 2.0F);
		}
		if (settings.DepthOfField || settings.TiltShift) {
			Check("Only in photo mode", &settings.FocusEffectsInPhotoModeOnly);
		}
	};

	auto interiorsAndShadows = [&]() {
		// The interior light takes the floor with it: the floor is the least light units and solid ground ever get, so left behind it would keep them bright in a dark room.
		float interiorBefore = std::max({settings.Ambient.x, settings.Ambient.y, settings.Ambient.z});
		Brightness("Interior and cave light", settings.Ambient, 1.0F);
		Tip("How bright interiors and caves are without lamps. Lower it and bunkers are lit by their lamps, and go dark where those are shot out.");
		float interiorNow = std::max({settings.Ambient.x, settings.Ambient.y, settings.Ambient.z});
		if (interiorNow != interiorBefore) {
			float floorLevel = std::max({settings.ForegroundAmbient.x, settings.ForegroundAmbient.y, settings.ForegroundAmbient.z});
			settings.ForegroundAmbient = floorLevel > 0.001F ? settings.ForegroundAmbient * (interiorNow * 0.83F / floorLevel) : glm::vec3(interiorNow * 0.83F);
		}
		Brightness("Least light on units and ground", settings.ForegroundAmbient, 1.0F);
		Tip("Units and solid ground never get darker than this, indoors or out. The slider above sets it too; move this one afterwards to keep them more visible than the walls behind.");
		Tint("Interior light colour", &settings.Ambient.x);
		Tint("Least light colour", &settings.ForegroundAmbient.x);
		Slider("Light carried through air", &settings.AirFalloff, 0.8F, 0.995F, "%.3f");
		Slider("Light carried through ground", &settings.SolidFalloff, 0.1F, 0.95F);
		SliderI("Light spreading steps a frame", &settings.PropagationIterationsPerFrame, 1, 32);
		Heading("Shadows");
		Slider("Shadow strength", &settings.ShadowStrength, 0.0F, 1.0F);
		Check("Traced terrain shadows", &settings.LightShadowField);
		Tip("Lamps, flashes and fires find the terrain between them and what they light through a map of distances to it, so thin walls and plating stop light instead of leaking it, and shadows are sharp near what casts them and soften further off. Off: the light is sampled at eleven evenly spaced places on its way, as before (cheaper; the Low preset). On from the Medium preset up.");
		if (settings.LightShadowField) {
			Slider("Shadow softness", &settings.LightShadowSoftness, 0.0F, 2.0F);
			Tip("How soft the edges of those shadows are. 0: sharp. Bigger lights are always softer than small ones.");
		}
		if (settings.LightShadowField) {
			Check("Soft light edge on walls", &settings.SoftWallLight);
			Tip("Where a light touches a wall, the lit patch on the wall fades out at its edge. Off (hard): the edge is sharp, as before.");
		}
		Slider("Shadows of units and objects", &settings.UnitShadows, 0.0F, 1.0F);
		Slider("Contact shading", &settings.ContactShading, 0.0F, 1.0F);
		Heading("Bounced light");
		Slider("Indirect light", &settings.IndirectLight, 0.0F, 1.5F);
		Check("Radiance cascades GI", &settings.RadianceCascades);
		Slider("GI strength", &settings.GIStrength, 0.0F, 4.0F);
		Slider("GI bounce", &settings.GIBounce, 0.0F, 1.0F);
	};

	auto lampsAndLights = [&]() {
		Slider("Glow light intensity", &settings.GlowLightIntensity, 0.0F, 8.0F);
		Slider("Glow light radius", &settings.GlowLightRadiusScale, 0.5F, 10.0F);
		SliderI("Most lights on screen", &settings.MaxScreenLights, 64, 4096);
		Tip("Past this many lights on one screen the faintest are left out. Lower it if big fights with lots of tracers and fire slow the game down.");
		Slider("Emissive intensity", &settings.EmissiveIntensity, 0.0F, 4.0F);
		Slider("Light colour strength", &settings.LightSaturation, 0.0F, 2.5F);
		Tip("How colourful the light of lamps, glows, flashes and fire is. 0 makes all light white.");
		Tint("Tint on all lights", &settings.LightTint.x);
		Heading("Scenery lamps");
		Slider("Lamp brightness", &settings.LampBrightness, 0.0F, 4.0F);
		Slider("Lamp reach", &settings.LampReach, 0.25F, 3.0F);
		Tint("Lamp tint", &settings.LampTint.x);
		Check("Light steady lamps once (faster)", &settings.LampCache);
		Tip("Scenery lamps that don't flicker are lit once into a map of the world, and only relit where the ground around them changes, so bases full of lamps cost about as much as one. Units cast no shadows from those lamps, and the lamps' shadows are softer. Off: every lamp is drawn every frame. On in the Low preset.");
		if (settings.LampCache) {
			Combo("Lamp map detail", &settings.LampCacheDetail, "Coarse (8 px)\0Medium (4 px)\0Fine (2 px)\0");
		}
		Heading("Headlamps");
		Check("Headlamps in the dark", &settings.Headlamps);
		Check("Headlamps by day as well", &settings.HeadlampsByDay);
		if (!settings.HeadlampsByDay) {
			Check("Only where it's dark around them", &settings.HeadlampsOnlyInDark);
			Tip("Each unit's headlamp follows the light where it stands: on at night, in caves and under roofs, off in daylight and next to a lit lamp or a fire. Off: every headlamp comes on at night by the clock, wherever its unit is, as before.");
		}
		if (!settings.HeadlampsByDay && settings.HeadlampsOnlyInDark) {
			Slider("How dark before they come on", &settings.HeadlampDarkThreshold, 0.05F, 0.95F);
			Tip("The light around a unit (sky, lamps, fires; 1 is open daylight) below which its headlamp comes on. It goes off again a little above it, so units at the edge of a light don't flicker.");
		}
		Slider("Beam brightness", &settings.HeadlampBrightness, 0.0F, 5.0F);
		Slider("Beam reach (px)", &settings.HeadlampReach, 40.0F, 600.0F, "%.0f");
		Slider("Beam width (degrees)", &settings.HeadlampWidth, 5.0F, 80.0F, "%.0f");
		Tint("Beam colour", &settings.HeadlampColor.x);
		Slider("Glow around the headlamp", &settings.HeadlampGlow, 0.0F, 2.0F);
		Slider("Team colour in the beam", &settings.HeadlampTeamTint, 0.0F, 1.0F);
		Heading("Tracers and aiming");
		Check("Tracers light what they pass", &settings.TracerLights);
		Slider("Tracer glow", &settings.TracerGlow, 0.0F, 1.0F);
		Tip("Tracers and their trails shine in their own colour and bloom.");
		Slider("Tracer light brightness", &settings.TracerLightBrightness, 0.0F, 3.0F);
		Slider("Tracer light reach (px)", &settings.TracerLightReach, 8.0F, 120.0F, "%.0f");
		Slider("Tracer light randomness", &settings.TracerLightRandomness, 0.0F, 1.0F);
		Tip("How much tracers' lights differ from one another in size and brightness, and waver as they fly. 0: all alike and steady.");
		Check("Aiming dots light the scene", &settings.AimDotsLight);
		Tip("The dots that show where a weapon points always glow. On, they also cast light on what is around them.");
		Heading("Lightsabers");
		Slider("Blade light brightness", &settings.SaberLightBrightness, 0.0F, 4.0F);
		Tip("How brightly lightsaber blades light up their holder, the ground and the walls around them. 0: the blade still shows but lights nothing.");
		Slider("Blade light reach", &settings.SaberLightReach, 0.2F, 3.0F);
		Slider("Blade glow in the air", &settings.SaberAirGlow, 0.0F, 4.0F);
		Tip("The soft glow of a blade's light in the air around it.");
		Heading("Jetpacks");
		Slider("Jetpack light brightness", &settings.JetpackLightBrightness, 0.0F, 4.0F);
		Tip("How much light jetpacks give off while they fire: the glow at the nozzle, and the light of their flames, flashes and glowing exhaust. 0: jets still show but light nothing.");
		Slider("Jetpack light reach", &settings.JetpackLightReach, 0.2F, 3.0F);
	};

	auto surfaces = [&]() {
		Slider("Edge lighting", &settings.EdgeLighting, 0.0F, 1.0F);
		Slider("Shine (metal, wet ground)", &settings.Specular, 0.0F, 3.0F);
		Check("Shine on units from lights", &settings.UnitShineLights);
		Tip("Headlamps, fire, muzzle flashes and other lights throw highlights on units and brighten their edges facing the light. Off: units keep their art and only take the light's colour and brightness, so a unit's own headlamp can't wash it out white.");
		Check("Shine on units from lamps", &settings.UnitShineLamps);
		Tip("The same for steady scenery lamps.");
		Check("Shine on units from the sun", &settings.UnitShineSun);
		Tip("The sun (or moon) glints on units' glossy and metal parts.");
		Slider("Metal reflections", &settings.Metals, 0.0F, 2.0F);
		Slider("Surface relief", &settings.Relief, 0.0F, 1.5F);
		Check("Wet, sooty, snowy and hot surfaces", &settings.SurfaceStates);
		Check("Scorch marks", &settings.ScorchMarks);
		Check("Blood, oil and water stains", &settings.Stains);
		Check("Stains and soot change the shine", &settings.StainSurface);
		Tip("Fresh blood shines a little and dries dark and matte, oil stays glossy and catches lamps, and soot dulls the ground. Off: stains and soot only tint the ground, as before.");
		if (settings.StainSurface) {
			Slider("Stain shine strength", &settings.StainShine, 0.0F, 1.0F);
		}
		Check("Soot and stains fade", &settings.DecalsFade);
		Tip("Scorch marks weather away and stains wash off over time, much faster in the rain, so a long battle doesn't end as one black smear. Off: they stay until the scene is rebuilt.");
		if (settings.DecalsFade) {
			Slider("Soot fades over (minutes)", &settings.DecalFadeMinutes, 0.5F, 60.0F, "%.1f", ImGuiSliderFlags_Logarithmic);
		}
		Slider("Hot metal cooling (seconds)", &settings.HotSpotSeconds, 0.0F, 10.0F, "%.1f");
		Check("Animated palette colours", &settings.PaletteAnimation);
		Tip("Glowing liquids like lava breathe, and colours listed in Base.rte/PaletteAnimation.ini or set by scripts pulse or cycle. Off, every colour stands still.");
		Slider("Palette pulse strength", &settings.PaletteAnimationStrength, 0.0F, 1.0F);
	};

	auto water = [&]() {
		Toggle("Flowing liquids", FluidSim::IsEnabled(), [](bool on) { FluidSim::SetEnabled(on); });
		if (Plain()) {
			ImGui::SameLine();
			ImGui::TextDisabled("(%d moving, %.2f ms)", FluidSim::GetActiveCount(), FluidSim::GetLastUpdateMS());
		}
		Toggle("Loose ground (sand, snow, gravel, glass) slides", FluidSim::PowdersEnabled(), [](bool on) { FluidSim::SetPowdersEnabled(on); });
		Toggle("Spilt blood runs and pools", FluidSim::BloodFlows(), [](bool on) { FluidSim::SetBloodFlows(on); });
		Tip("Off, blood stays where it falls, as it always has. On, it runs downhill, pools, and slowly dries away (with flowing liquids on).");
		Toggle("Liquids drain out of the map bottom", FluidSim::DrainsBottom(), [](bool on) { FluidSim::SetDrainsBottom(on); });
		Tip("Liquid that reaches the bottom of the map runs out of it and is gone. Off: it pools on the bottom, as before. Sand and snow are the setting below.");
		Toggle("Liquids drain out of the map sides", FluidSim::DrainsSides(), [](bool on) { FluidSim::SetDrainsSides(on); });
		Tip("Liquid that reaches the left or right edge of the map runs out of it and is gone. Off: it banks up against the edge, as before. A map that wraps round sideways has no edges there, so this only does something on maps that don't wrap (or with map wrapping turned off).");
		Toggle("Loose ground falls out of the map", FluidSim::PowdersFallOut(), [](bool on) { FluidSim::SetPowdersFallOut(on); });
		Tip("Sand, snow, gravel and other loose ground that slides down to the bottom of the map, or off a side that doesn't wrap, falls out of it and is gone. Off: it piles up there, as before.");
		Toggle("Units swim, float and drown", ActorWater::IsEnabled(), [](bool on) { ActorWater::SetEnabled(on); });
		Tip("Flesh and blood units hold their breath for 12 seconds with their heads under; an Air gauge shows over the unit you play while it lasts.\nSwimming: left and right swim, Up or Jump strokes up, Down or Crouch dives.");
		if (int waterBehaviour = g_SettingsMan.UnitWaterBehaviour(); Combo("Water in units' way", &waterBehaviour, "As terrain (as before)\0Open water\0Open water, every liquid waded\0")) {
			g_SettingsMan.SetUnitWaterBehaviour(waterBehaviour);
		}
		Tip("How units, players' and the AI's, take liquid when they look where they're going. Their bodies move through it whichever you pick.\nAs terrain: as before. Wading reads as a step to leap or jet over, a deep pool or a stream pouring down in front as a wall to go round, water pouring off a ledge as a ceiling that puts the jet out, a puddle as ground that stops a take-off or a leap, and a unit knocked down in a puddle doesn't get up.\nOpen water: none of that; liquid is seen as the open water it is.\nEvery liquid waded: that, and routes wade mud, tar, mercury, fuel and the other liquids as they do oil, rather than walking along the top and sinking in. That part takes effect when the next map loads.");
		Slider("Light glowing through water", &settings.WaterLightGlow, 0.0F, 1.5F);
		Tip("How much a lamp, fire or blast in or beside water shows as a glow in the water, in the light's own colour. 0: water is only lit like a surface.");
		Check("Each liquid has its own look", &settings.DistinctLiquidLooks);
		Tip("Oil is a dark, glossy sheet, mud a dull brown, slime a glowing bubbling green and mercury a silver mirror. Off: they are all drawn as water. Water, lava and acid look the same either way.");
		Heading("Reflections");
		Check("Water reflects and refracts", &settings.WaterReflections);
		Tip("Pools mirror what's above them, the wall behind the water shows through bent by the ripples and darker with depth, and the rippled surface catches lamps and the sun. Off: water is drawn as before, flat and tinted. On from the Medium preset up.");
		if (settings.WaterReflections) {
			Slider("Reflection", &settings.WaterReflectionStrength, 0.0F, 1.0F);
			Tip("How strongly water mirrors the scene above it, strongest just under the surface. 0 for none.");
			Slider("Refraction", &settings.WaterRefraction, 0.0F, 1.5F);
			Tip("How much the ripples bend what's seen through the water, and how much it darkens with depth. 0 for none.");
			Slider("Ripples", &settings.WaterRipples, 0.0F, 2.0F);
			Tip("How much the surface ripples tilt the reflection and the glints of light on the water. 0: a flat mirror.");
			Check("Soft reflections", &settings.WaterSoftReflection);
			Tip("The mirror image blurs and fades the deeper it goes and where the open air above the pool ends, and isn't cut off hard at the screen edge or beside other water. Off: sharp and cut off, as before.");
			Check("Reflection ripples with the surface", &settings.WaterMirrorSurface);
			Tip("The mirrored scene is moved by the surface above it, so it wobbles as one image where the water moves and goes clean where it's still. Off: each pixel's own ripple moves it, as before. How much is the Ripples slider.");
		}
		Check("Wavy lines of light", &settings.WaterCaustics);
		Tip("The thin bright wavy lines that wander and cross through water. Off: water is smooth, without them.");
		Heading("Moving water");
		Check("Surface follows the flow", &settings.WaterFlowSurface);
		Tip("Still water goes glassy, a stream's ripples run downstream, the surface rings out where a pour lands and fast water froths through. Off: the same slow waves everywhere, as before. Needs flowing liquids on.");
		if (settings.WaterFlowSurface) {
			Slider("Follows the flow", &settings.WaterFlowStrength, 0.0F, 1.0F);
			Tip("How strongly. 0 is the same as off.");
		}
		Heading("Pouring water");
		Slider("Froth", &settings.WaterFoam, 0.0F, 1.5F);
		Tip("Thin, broken water (a stream off a ledge, the lip of a pour) is drawn as froth, and froth fills the air beside it. 0 for none.");
		Slider("Froth on stray drops", &settings.WaterFoamStray, 0.0F, 1.0F);
		Tip("How much of that a pixel or two of water thrown clear of the rest gets. 0: stray drops stay single pixels. 1: as much as a stream.");
		Slider("Froth bubbling", &settings.WaterFoamBubbles, 0.0F, 2.0F);
		Tip("How much the froth flickers lighter and darker. 0: smooth, like still water. 1: lively.");
		Slider("Froth brightness", &settings.WaterFoamBrightness, 0.2F, 2.0F);
		Slider("Froth glow in the dark", &settings.WaterFoamGlow, 0.0F, 1.0F);
		Tip("How much light of its own froth carries. 0: it is as dark as the scene around it at night.");
		Heading("Splashes");
		Slider("Splash size", &settings.WaterSplash, 0.0F, 4.0F);
		Tip("How big the splash is when falling ground or a broken-off piece drops into water (or any liquid): drops and spray thrown up, by how fast and how wide it went in. Only for the eye: the water it pushes aside raises the level. 0 for none.");
		Slider("Splash drops", &settings.SplashDrops, 0.0F, 4.0F);
		Tip("How many drops a splash throws for the same size. 0: none, only spray and froth.");
		Slider("Splash height", &settings.SplashHeight, 0.2F, 3.0F);
		Tip("How high the drops are thrown. 1 as it is.");
		Slider("Splash width", &settings.SplashWidth, 0.2F, 3.0F);
		Tip("How far out to the sides the drops are thrown. Low: straight up. High: a wide, flat crown.");
		Slider("Splash drop size", &settings.SplashDropSize, 1.0F, 4.0F);
		Tip("How big each drop is drawn, in pixels across.");
		Slider("Splash spray", &settings.SplashSpray, 0.0F, 3.0F);
		Tip("How much soft spray goes up with the drops. 0 for none.");
		Check("Turn each froth and spray puff randomly", &settings.PuffVariety);
		Tip("Each puff of spray, froth mist, dust and smoke is turned and mirrored its own way when it appears, so they don't all show the same shape. Off: all the same way up.");
		Slider("Thin streams shown", &settings.WaterThinFlow, 0.0F, 2.0F);
		Tip("How much water running over the ground only a pixel or two deep is shown up: paler, with spray skipping along it, so a thin stream can be seen. Only for the eye. 0 for not at all.");
		Heading("Splash under-layer");
		Slider("Under-layer drops", &settings.SplashUnder, 0.0F, 3.0F);
		Tip("A second layer of drops thrown with every splash, drawn under the first in a colour of its own, with its own height, width and size. 1 is about as many as the first layer. 0 for none.");
		Tint("Under-layer colour", &settings.SplashUnderColor.x);
		Slider("Under-layer takes the liquid's colour", &settings.SplashUnderLiquidColor, 0.0F, 1.0F);
		Tip("0: the colour above only. 1: the liquid's own colour, like the first layer.");
		Slider("Under-layer height", &settings.SplashUnderHeight, 0.05F, 3.0F);
		Tip("How high it is thrown. 1 as high as the plain splash; below 1 it stays low, under the main crown.");
		Slider("Under-layer width", &settings.SplashUnderWidth, 0.2F, 3.0F);
		Tip("How far out to the sides it is thrown. 1 as far as the plain splash.");
		Slider("Under-layer drop size", &settings.SplashUnderDropSize, 1.0F, 4.0F);
		Tip("How big each of its drops is drawn, in pixels across.");
		Slider("Under-layer opacity", &settings.SplashUnderOpacity, 0.0F, 1.0F);
		Slider("Under-layer scatter", &settings.SplashUnderScatter, 0.0F, 1.0F);
		Tip("How much its drops stray from each other. 0: a tidy crown. 1: every which way.");
		Heading("Splash froth");
		Slider("Splash froth", &settings.SplashFroth, 0.0F, 3.0F);
		Tip("How much froth a splash leaves sitting on the surface, and how much the surface froths where the level rises because something fell in. Only for the eye. 0 for none.");
		Slider("Splash froth density", &settings.SplashFrothDensity, 0.2F, 6.0F);
		Slider("Splash froth specks", &settings.SplashFrothSpecks, 0.0F, 3.0F);
		Slider("Splash froth bubble size", &settings.SplashFrothSize, 0.2F, 3.0F);
		Slider("Splash froth life", &settings.SplashFrothLife, 0.2F, 4.0F);
		Tip("How long the froth stays on the surface before it fades: 1 is a couple of seconds.");
		Slider("Splash froth opacity", &settings.SplashFrothOpacity, 0.0F, 1.0F);
		Heading("Mist");
		Slider("Mist", &settings.WaterMist, 0.0F, 2.0F);
		Tip("Soft spray thrown off water that is falling fast or landing. 0 for none.");
		Slider("Mist puff size", &settings.WaterMistSize, 0.1F, 3.0F);
		Tip("How big each puff is. 1 is about 3 to 6 pixels across at first.");
		Slider("Mist puff spread", &settings.WaterMistSpread, 0.0F, 3.0F);
		Tip("How much each puff swells as it thins. 0: it stays the size it starts.");
		Slider("Mist puff life", &settings.WaterMistLife, 0.2F, 4.0F);
		Tip("How long each puff lasts. 1 is about half a second to a second.");
		Slider("Mist puff opacity", &settings.WaterMistOpacity, 0.0F, 1.0F);
		Slider("Mist brightness", &settings.WaterMistBrightness, 0.2F, 2.0F);
		Slider("Mist glow in the dark", &settings.WaterMistGlow, 0.0F, 1.0F);
		Tip("The least light mist is drawn with. 0: it is as dark as the scene around it at night.");
	};

	auto fireAndSmoke = [&]() {
		Toggle("Spreading fire", TerrainFire::IsEnabled(), [](bool on) { TerrainFire::SetEnabled(on); });
		if (Plain()) {
			ImGui::SameLine();
			ImGui::TextDisabled("(%d burning)", TerrainFire::GetCount());
		}
		if (float chance = TerrainFire::GetEmberIgniteChance(); Slider("Embers set things alight", &chance, 0.0F, 10.0F, "%.2f%% a second")) {
			TerrainFire::SetEmberIgniteChance(chance);
		}
		Tip("The chance each second that smouldering charcoal (what's left glowing of burnt wood) sets alight each grass, wood or oil pixel touching it. Charcoal never relights other charcoal. 0 for never.");
		Toggle("Candles burn forever", TerrainCandle::GetBurnMinutes() <= 0.0F, [](bool on) { TerrainCandle::SetBurnMinutes(on ? 0.0F : 2.0F); });
		Tip("Lit candles (Paint > Plants > Candles) keep burning and never melt down. Off, they burn down in the time below.");
		if (float minutes = TerrainCandle::GetBurnMinutes(); minutes > 0.0F && Slider("Candle burn time", &minutes, 0.5F, 60.0F, "%.1f minutes", ImGuiSliderFlags_Logarithmic)) {
			TerrainCandle::SetBurnMinutes(minutes);
		}
		Tip("How long a lit candle 20 pixels tall takes to burn down, whatever its width; a taller one takes longer. 2 minutes as it comes.");
		if (float reaction = RopeSim::GetReaction(); Slider("Ropes react to wind and blasts", &reaction, 0.0F, 3.0F, "%.2fx")) {
			RopeSim::SetReaction(reaction);
		}
		Tip("How strongly ropes, threads, chains and cables blow about in the wind, ride the pressure of a passing blast wave and are thrown out from explosions. 1 as it comes, 0 they hang still.");
		Toggle("Dropped ropes settle into the ground", RopeSim::GetSettleSeconds() > 0.0F, [](bool on) { RopeSim::SetSettleSeconds(on ? 5.0F : 0.0F); });
		if (float settle = RopeSim::GetSettleSeconds(); settle > 0.0F && Slider("Rope settle time", &settle, 0.5F, 60.0F, "%.1f seconds", ImGuiSliderFlags_Logarithmic)) {
			RopeSim::SetSettleSeconds(settle);
		}
		Tip("A rope, chain or cable that is no longer tied to anything and has stayed within a small area (about 6 pixels of room) this long, even if it is still swaying, turns into terrain where it lies (wood for rope, thread and bungee, metal for chain and cable), so it stops being simulated. Off, it stays a rope.");
		Toggle("Units catch fire", ActorFire::IsEnabled(), [](bool on) { ActorFire::SetEnabled(on); });
		Toggle("Smoke blocks sight", SmokeGrid::IsEnabled(), [](bool on) { SmokeGrid::SetEnabled(on); });
		Toggle("Gas", GasGrid::IsEnabled(), [](bool on) { GasGrid::SetEnabled(on); });
		Tip("Smoke, toxic gas, methane and steam spread through the air and stay in closed rooms: smoke builds up where it can't get out, toxic gas sinks and pools and hurts whoever breathes it, methane rises and goes up in a chain of blasts where it meets fire, steam rises, scalds and condenses away. Smoke, toxic gas and steam block sight.");
		if (GasGrid::IsEnabled()) {
			if (float shown = GasGrid::GetShown(); Slider("Gas shown", &shown, 0.0F, 2.0F)) {
				GasGrid::SetShown(shown);
			}
			Tip("How much of the gas is drawn: green puffs for toxic gas, steam puffs, and haze for built-up smoke. 0 draws none (the gas still does what it does); methane is never drawn.");
		}
		Slider("Soft smoke", &settings.SoftSmoke, 0.0F, 3.0F);
		Tip("Every puff of the game's smoke trails soft, billowing smoke as well, so it hangs and rolls. 0: only the game's own smoke sprites.");
		Slider("Smoke scattering", &settings.SmokeScattering, 0.0F, 3.0F);
		if (settings.SmokeScattering > 0.0F) {
			Check("Smoke shades itself", &settings.SmokeShading);
			Tip("Smoke takes its own colour, is lit on the side towards a fire, lamp or the sun and dark on the far side, and the sun paints its top. Off: one pale tint lit evenly through, as before.");
			if (settings.SmokeShading) {
				Slider("Smoke shading strength", &settings.SmokeShadingStrength, 0.0F, 1.0F);
			}
		}
		Slider("Embers", &settings.Embers, 0.0F, 3.0F);
		Combo("Fire style", &settings.FireStyle, "Pixel and shader\0Pixel only\0Shader only\0");
		Tip("How all fire is drawn, on burning ground and in the air. Pixel: a flickering dot and short tongue per burning pixel, and the drawn flame sprites, as before. Shader: flames with shape and motion, a darker core at the base and embers lifting off the tips, grouped along the fire front. Pixel and shader: the shader's flames over the pixel fire.");
		if (settings.FireStyle != LightingSettings::FirePixel) {
			Slider("Flame height", &settings.FireFlameSize, 0.2F, 3.0F);
			Slider("Flame brightness", &settings.FireFlameBrightness, 0.2F, 2.0F);
		}
		Slider("Sparks", &settings.EffectsSparks, 0.0F, 3.0F);
		Slider("Spark lights", &settings.SparkLights, 0.0F, 2.0F);
		Tip("How bright the glow and light of the game's own sparks are, off hits and blasts. 0: they fly without lighting anything. With Sparks at 0 they're off too.");
		Slider("Dust", &settings.EffectsDust, 0.0F, 3.0F);
		Slider("Debris", &settings.EffectsDebris, 0.0F, 3.0F);
		Tip("Sparks: glowing streaks off explosions and hard hits. Dust: soft puffs off explosions and soft ground. Debris: little chips that bounce. Embers, explosion fire and smoke, and splash spray follow the highest of the three, and go only when all three are 0.");
		if (Plain()) {
			ImGui::TextDisabled("%d effects particles alive", EffectsParticles::GetCount());
		}
		Heading("Heat and blast in the air");
		Check("Distortion", &settings.DistortionEnabled);
		Slider("Heat haze (px)", &settings.HeatHaze, 0.0F, 6.0F);
		Check("Haze only from heat", &settings.HazeFromHeat);
		Tip("Heat haze rises from fire, burning ground, blasts and warm glows only. Off: anything bright shimmers, lamps and screens included, as before.");
		Slider("Shockwave strength", &settings.ShockwaveStrength, 0.0F, 3.0F);
		float hitStop = g_CameraMan.GetHitStopStrength();
		if (Slider("Hit-stop on big blasts", &hitStop, 0.0F, 2.0F)) {
			g_CameraMan.SetHitStopStrength(hitStop);
		}
	};

	auto effectLayers = [&]() {
		Combo("All effects", &settings.EffectLayers, "Each its own layer\0All in front\0All behind\0");
		Tip("Where the visual-only effects are drawn. Behind: in the effects layer, between the battlefield and the background, so units and the ground in front hide them while they still drift over the back walls of caves and bunkers and the sky. In front: over everything, as before. Each its own layer: as set for each one below.");
		Heading("Each effect");
		static constexpr const char* c_Labels[LightingSettings::EffectLayerCount] = {"Smoke", "Soft smoke", "Spray mist", "Splash drops", "Froth", "Dust", "Debris chips", "Sparks", "Embers", "Explosion fire"};
		static constexpr const char* c_Tips[LightingSettings::EffectLayerCount] = {
		    "The game's smoke sprites (smoke grenades, engines, guns, burning), and the light smoke scatters.",
		    "The soft, billowing smoke the smoke sprites trail, and the smoke explosions leave behind.",
		    "The pale spray off falling and splashing water.",
		    "The drops a splash throws.",
		    "The froth that sits on water where something splashed in, in flat clumps.",
		    "Puffs of dust from blasts and from hits on soft ground.",
		    "The little chips blasts and hits throw.",
		    "Glowing sparks from blasts and from hits on hard ground.",
		    "Embers lifting off fires.",
		    "The balls of fire that swell and roll up from explosions."};
		ImGui::BeginDisabled(settings.EffectLayers != LightingSettings::EffectLayersEach);
		for (int layer = 0; layer < LightingSettings::EffectLayerCount; ++layer) {
			int behind = settings.EffectBehind[layer] ? 1 : 0;
			if (Combo(c_Labels[layer], &behind, "In front\0Behind\0")) {
				settings.EffectBehind[layer] = behind != 0;
			}
			Tip(c_Tips[layer]);
		}
		ImGui::EndDisabled();
	};

	auto airAndWind = [&]() {
		AirPressure::Tuning& tuning = AirPressure::GetTuning();
		Toggle("Air and wind", AirPressure::IsOn(), [](bool on) { AirPressure::SetOn(on); });
		Tip("Everything below: blast waves through the air, and the weather's wind carrying smoke, spray and gas. Off: none of it, and explosions push things only as they always have.");
		if (Plain()) {
			ImGui::SameLine();
			ImGui::TextDisabled("(%d cells of blast waves)", AirPressure::GetActiveCells());
		}
		ImGui::BeginDisabled(!AirPressure::IsOn());
		Slider("Overall strength and speed", &tuning.Overall, 0.0F, 5.0F, "%.2fx");
		Tip("Over everything below at once: how hard blasts push and throw water, how hard the wind carries smoke, effects and gas, how far the flames bend, and how fast blast waves travel. 2: twice as strong and twice as fast. 0: the air does nothing. The sliders below set each part against this.");
		Heading("Blast waves");
		Toggle("Blast waves", AirPressure::IsEnabled(), [](bool on) { AirPressure::SetEnabled(on); });
		Tip("An explosion sends a wave of air out that bounces off walls: it carries far down a corridor and fades fast in the open, pushes smoke, loose things and (a little) units, and throws up the water in a flooded room.");
		Slider("Blast strength", &tuning.BlastStrength, 0.0F, 5.0F, "%.2fx");
		Tip("How much pressure an explosion puts into the air. 0: explosions make no wave.");
		Slider("How far blasts carry", &tuning.BlastReach, 0.25F, 3.0F, "%.2fx");
		Tip("How slowly a wave dies away: at 2 it carries about twice as far down a corridor before it fades.");
		Slider("Push on smoke and loose things", &tuning.PushStrength, 0.0F, 5.0F, "%.2fx");
		Tip("How hard the moving air shoves smoke, gibs, dropped items and spray. 0: the wave pushes nothing (it still throws up water).");
		Slider("Push on units", &tuning.UnitPush, 0.0F, 5.0F, "%.2fx");
		Tip("How hard the moving air shoves units, on top of the push above. Units are heavy, so at 1 a big blast beside one moves it a little. 0: units are never pushed.");
		Slider("Water thrown up", &tuning.LiquidThrow, 0.0F, 5.0F, "%.2fx");
		Tip("How readily a wave running up through water (or any liquid) throws it into the air at the surface. Higher: weaker waves throw it too. 0: never.");
		Heading("Wind");
		Toggle("Wind carries smoke", AirPressure::WindMovesSmoke(), [](bool on) { AirPressure::SetWindMovesSmoke(on); });
		Tip("The weather's wind (Time & weather, Wind) carries smoke of every kind (grenades, explosions, flames, smoke trails, soft smoke), steam, embers, dust, fine spray and gas along, and they eddy in the lee of walls and ridges. Off: none of them lean with the wind; the rain, snow, fog and clouds still do.");
		Slider("Wind strength", &tuning.WindStrength, 0.0F, 5.0F, "%.2fx");
		Tip("How hard the wind carries smoke, spray and gas, against how hard the weather's wind blows. 0: the wind moves nothing.");
		Slider("Wind carries gas", &tuning.WindGas, 0.0F, 5.0F, "%.2fx");
		Tip("How fast the wind carries gas (smoke built up, toxic gas, methane, steam) along where it blows through; gas in the lee of ground stays. Gas blown past the edge of the map is gone. 0: the wind leaves gas be.");
		ImGui::EndDisabled();
		if (Plain() && ToolUI::Button("Usual air and wind")) {
			tuning = AirPressure::Tuning();
		}
	};

	auto magic = [&]() {
		MagicEffects::Tuning& tuning = MagicEffects::GetTuning();
		Toggle("Magic and tool effects", MagicEffects::IsOn(), [](bool on) { MagicEffects::SetOn(on); });
		Tip("What spells, tools and scripts cast: a telekinetic push or pull shoves units, items and loose bits in a cone and sets the air blowing with it. Off: a push does nothing. (What a spell sprays, like its acid, still flies and fades as its material says.)");
		ImGui::BeginDisabled(!MagicEffects::IsOn());
		Slider("Push strength", &tuning.PushStrength, 0.0F, 5.0F, "%.2fx");
		Tip("How hard a push or pull shoves everything it reaches. 0: it shoves nothing.");
		Slider("Push on units", &tuning.UnitPush, 0.0F, 5.0F, "%.2fx");
		Tip("How hard it shoves units, on top of the push above. A hard enough shove knocks a unit off its feet. 0: units are never pushed.");
		Slider("Gust of air", &tuning.AirGust, 0.0F, 5.0F, "%.2fx");
		Tip("How hard the air a push sets blowing is, carrying smoke, spray and gas with it (needs Air & wind and Blast waves on). 0: no gust.");
		Toggle("Spells use energy", tuning.SpellEnergy, [&tuning](bool on) { tuning.SpellEnergy = on; });
		Tip("Casting a spell spends the caster's energy (the blue bar under its health), or the spell's own charge if it has one; with too little the spell doesn't cast. Off: every spell is free.");
		ImGui::BeginDisabled(!tuning.SpellEnergy);
		Slider("Energy regen", &tuning.EnergyRegen, 0.0F, 5.0F, "%.2fx");
		Tip("How fast energy and a spell's own charge come back. 0: they never come back.");
		ImGui::EndDisabled();
		Toggle("AI casts spells", tuning.AICasting, [&tuning](bool on) { tuning.AICasting = on; });
		Tip("AI units use their spells of their own accord: a unit with a push spell pushes a grenade landing near it back the way it came, and a unit that ran out of energy and took out its other weapon goes back to its spell once its energy has mostly come back. Off: the AI fires a spell only like any other weapon it happens to hold.");
		ImGui::EndDisabled();
		if (Plain() && ToolUI::Button("Usual magic")) {
			tuning = MagicEffects::Tuning();
		}
	};

	auto fallingGround = [&]() {
		TerrainCollapse::Tuning& tuning = TerrainCollapse::GetTuning();
		Toggle("Collapsing terrain", TerrainCollapse::IsEnabled(), [](bool on) { TerrainCollapse::SetEnabled(on); });
		if (Plain()) {
			ImGui::TextDisabled("%d pieces moving, %d pixels fell", TerrainCollapse::GetFallingCount(), TerrainCollapse::GetCollapsedCount());
		}
		Toggle("Pieces of buildings fall too", TerrainCollapse::BuildingsFall(), [](bool on) { TerrainCollapse::SetBuildingsFall(on); });
		Toggle("Units and vehicles bump into trees", TerrainTrees::UnitsCollide(), [](bool on) { TerrainTrees::SetUnitsCollide(on); });
		Tip("Off (as the game comes): units walk and vehicles drive through trees, and a tree coming down falls through them too. Trees still burn, stand until their trunks burn through, fall and land on the ground, and bullets, fire and liquids still meet them. On: trees are solid to units and vehicles like any ground, and a falling tree hits them.");
		{
			int strayPercent = TerrainTrees::StrayBulletPercent();
			if (SliderI("Stray bullets that hit trees (%)", &strayPercent, 0, 100)) {
				TerrainTrees::SetStrayBulletPercent(strayPercent);
			}
		}
		Tip("Bullets go past trees. This is the chance that a bullet meeting a tree strikes it instead, now and then, and then it barely marks the tree. Rockets and the like clear a trunk but strike the leaves, so trees give cover from them. Fire and explosions work on trees as they always did.");
		Toggle("Trees only meet the ground under them", TerrainCollapse::PassesTrees(), [](bool on) { TerrainCollapse::SetPassesTrees(on); });
		Tip("On: rock and other ground falling from above goes through a standing tree (behind it, leaving the tree whole) rather than landing on it, and a falling tree goes through other trees. A tree that is cut or burnt through still falls over and lands on the ground. Off (as the game comes): trees are solid to falling pieces like any ground.");
		Toggle("Units in front of trees", TerrainTrees::DrawnBehindUnits(), [](bool on) { TerrainTrees::SetDrawnBehindUnits(on); });
		Tip("On (as the game comes): a tree is drawn behind units and everything else that moves, so a unit walking through a tree is seen in front of its trunk and leaves. Off: trees are drawn over them like the rest of the ground. (Needs the lighting's terrain shader; very large scenes draw trees in front.)");
		Toggle("Units' metal and gear settle as scraps", g_SettingsMan.BodyGearSettlesAsScraps(), [](bool on) { g_SettingsMan.SetBodyGearSettlesAsScraps(on); });
		Tip("Armour plating, robot parts and the rest of what comes off a unit keep their look when they come to rest in the ground, but become the same soft scraps as the flesh, so the remains of the fallen never leave lumps of metal nobody can dig through. Flesh and bone settle as scraps and ashes either way. Off: everything settles as its own material.");
		Heading("What falls");
		Toggle("Show hold zones on the map", TerrainCollapse::ShowsHoldZones(), [](bool on) { TerrainCollapse::SetShowHoldZones(on); });
		Tip("Outlines the sandbox's hold zones (Paint > Terrain > Hold ground) on the map, where the ground never falls. Off: they show only while Hold ground is in hand.");
		Check("Floating masses stay up when chipped", &tuning.FloatingStays);
		Tip("On: a mass that was already hanging in the air before a blast stays; cut in two, the bigger part stays and the smaller falls. Off: anything touching nothing falls.");
		SliderI("Thin neck that snaps (pixels)", &tuning.NeckWidth, 0, 16);
		Tip("A piece left joined to the rest by a neck no wider than this breaks off and falls. 0: only pieces cut right through fall. Wood always holds until it's cut or burnt right through, so a burning tree stands (a material's own NeckWidth in its ini).");
		SliderI("Thin neck that snaps, thin pieces (pixels)", &tuning.ThinNeckWidth, 0, 16);
		Tip("A second look for necks, for thin pieces: the look above pares every piece by half its width first, so a ledge thinner than about that never shows up there. This one finds a thin ledge held on by a neck no wider than this. 0: off.");
		SliderI("Longest overhang that holds (pixels)", &tuning.OverhangLength, 0, 400);
		Tip("Ground sticking out further than this from what holds it up (ground beneath it all the way down, bedrock, buildings) breaks off where it's that far out: a ledge dug or shot this long, or the middle of a cave roof dug twice this wide. 20 pixels is a metre. Overhangs already that long (the scenery) stay until they're cut further. 0: off.");
		SliderI("Biggest piece that can fall (pixels)", &tuning.MaxPiecePixels, 500, 200000, "%d", ImGuiSliderFlags_Logarithmic);
		Tip("Anything bigger counts as the world and never falls. 30,000 is about a 170 by 170 block. A tree's trunk and leaves don't count, so a big tree cut through still comes down whole.");
		SliderI("Smallest loose bit of building that falls", &tuning.MinFittingPixels, 0, 2000);
		Tip("Smaller loose bits of building material stay put: lamps, signs and consoles are drawn hanging in mid-air.");
		Slider("Force to trigger a collapse check", &tuning.CheckMinForce, 0.0F, 2000.0F, "%.0f", ImGuiSliderFlags_Logarithmic);
		Tip("How hard a bullet, thrown object or unit has to hit the ground (its impulse, in the same units as a material's strength: earth 70, concrete 1000) to start the ground around being checked for pieces cut loose. Weaker hits on the same patch add up over a couple of seconds, so steady fire still brings a ledge down. Weapon damage to terrain counts. Explosions, digging and fire always start a check. 0: every hit that knocks out terrain does.");
		Heading("How it falls");
		Slider("How hard explosions throw loose pieces", &tuning.BlastPush, 0.0F, 3.0F);
		Tip("0: explosions don't move loose pieces at all. Higher: pieces still moving are thrown harder, and more of the pieces lying at rest near a blast are picked up and thrown.");
		SliderI("Loose scraps it flattens (pixels)", &tuning.CrushPixels, 0, 300);
		Tip("A falling piece goes through loose bits of ground up to this size instead of getting stuck on them. Never more than a quarter of its own size. 0: everything holds it up.");
		Slider("Sand disturbed by walking", &tuning.ScuffStrength, 0.0F, 3.0F);
		Tip("Units walking or running on sand and other loose ground knock a few surface pixels loose and shove them the way they go, so a slope slumps a little. 0: off. Mod materials opt in with Scuffs in their ini.");
		Slider("Seconds still before it's ground again", &tuning.RestSeconds, 0.2F, 15.0F, "%.1f");
		Heading("Breaking when it lands");
		Slider("How hard a landing breaks a piece (all)", &tuning.BreakStrength, 0.2F, 5.0F, "%.2fx");
		Tip("Scales every threshold below: 2 takes twice as hard a landing, 0.5 half.");
		Slider("Concrete, glass, ice (Shatter, m/s)", &tuning.ShatterSpeed, 0.5F, 30.0F, "%.1f");
		Tip("How fast a piece of brittle material has to land to break. 7 m/s is a drop of about 1.4 m. Glass breaks at under half this, ice at 0.7 of it.");
		Slider("Earth, stone (Crack, m/s)", &tuning.CrackSpeed, 0.5F, 30.0F, "%.1f");
		Tip("Earth, stone and any material not listed elsewhere. 9 m/s is a drop of about 2.2 m. Earth breaks at 0.9 of this, stone at 1.2, bedrock at 1.8.");
		Slider("Sand, snow, rubble (Crumble, m/s)", &tuning.CrumbleSpeed, 0.5F, 30.0F, "%.1f");
		Tip("Loose ground that falls apart easily. Leaves and grass count only in a piece of nothing else.");
		Slider("Wood, tree trunks (Splinter, m/s)", &tuning.SplinterSpeed, 0.5F, 30.0F, "%.1f");
		Tip("22 m/s is a drop of about 13 m: a felled or burnt-through tree lands whole. A tree's leaves don't make it weaker. Planks break at 0.8 of this.");
		Slider("Leaves break apart (x easier)", &tuning.LeafBreakEase, 1.0F, 20.0F, "%.1fx");
		Tip("How much more easily a falling tree's leaves (and grass) come off and break up when it lands than the piece itself breaks: at 4 a landing at a quarter of the speed strips as many, and a hard one nearly all of them. A clump of nothing but leaves comes apart this much more easily too. 1: as easily as the rest.");
		{
			int litterPercent = static_cast<int>(std::round(tuning.LeafLitter * 100.0F));
			if (SliderI("Leaves left on the ground (%)", &litterPercent, 0, 100)) {
				tuning.LeafLitter = static_cast<float>(litterPercent) / 100.0F;
			}
		}
		Tip("Of the leaves (and grass) that come off a falling tree or break off a clump, how many are laid on the ground below as plant matter rather than thrown as loose bits, most of which vanish as they land. 100: all of them stay. 0: all are thrown, as before. Leaves thrown when too many are flying at once are laid down either way rather than lost.");
		Slider("Metal (Bend, m/s)", &tuning.BendSpeed, 0.5F, 30.0F, "%.1f");
		Tip("Metal never breaks from a landing. Above this, a long thin piece (a beam, a plate) folds at a crease, more the harder the hit and the thinner it is, and a chunky piece dents. Falling pieces move at most 27 m/s, so above that never.");
		SliderI("Most loose bits thrown per update", &tuning.MaxDebrisPerUpdate, 0, 5000, "%d", ImGuiSliderFlags_Logarithmic);
		Tip("How many loose pixels breaking, snapping and crushed ground may throw off as particles in one update. Past it, little pieces wait and come apart later, but pixels of snapped necks and overhangs and crushed scraps are lost (see the next setting). Higher: fewer lost, slower big collapses. 160: as the game comes.");
		SliderI("Loose bits kept past that (%)", &tuning.DebrisKeptPercent, 0, 100);
		Tip("Of the loose pixels past the most per update that would otherwise vanish, how many in a hundred are thrown anyway. 0: all of them vanish, as the game comes. 100: none do, at the cost of more particles in big collapses.");
		Heading("Hitting units");
		Slider("How much falling pieces hurt", &tuning.HitDamage, 0.0F, 5.0F, "%.2fx");
		Tip("Damage is a share of the unit's full health, by how fast the piece is moving into it and how heavy it is for the unit. At 1 a block a metre across falling 10 m/s onto a soldier takes about a quarter to a third of their health. 0: pieces never hurt.");
		Slider("Slowest hit that hurts (m/s)", &tuning.HitMinSpeed, 0.0F, 15.0F, "%.1f");
		Tip("Only the speed above this counts toward the damage. Lower: slow slides and short drops hurt too.");
		SliderI("Smallest piece that hurts (pixels)", &tuning.HitMinPixels, 0, 1000, "%d", ImGuiSliderFlags_Logarithmic);
		Tip("Smaller pieces only push units about. 400 pixels is a block a metre across.");
		Slider("Heaviest a piece counts (x unit's mass)", &tuning.HitMassCap, 0.1F, 20.0F, "%.1fx", ImGuiSliderFlags_Logarithmic);
		Tip("A piece heavier than this many times the unit it hits hurts only as much as one this heavy. Higher: big boulders are deadlier than big rocks.");
		Slider("How hard pieces knock units", &tuning.HitKnockback, 0.0F, 3.0F, "%.2fx");
		Tip("How hard falling pieces shove the units and loose objects they hit. Units knocked flying into the ground take the usual impact damage on top.");
		if (Plain() && ToolUI::Button("Usual falling")) {
			tuning = TerrainCollapse::Tuning();
		}
	};

	// The solid pieces a body, wreck or crate breaks into when it gibs. Each tumbles about and costs many times what a drop of blood does to move,
	// so in a big battle hundreds of them can slow the game down. As they come, everything is as the game has always been.
	auto gibs = [&]() {
		SettingsMan::GibChunkTuning& chunks = g_SettingsMan.GibChunks();
		if (Plain()) {
			int loose = 0;
			for (const MovableObject* particle: g_MovableMan.GetParticleList()) {
				loose += particle->IsGibChunk() ? 1 : 0;
			}
			ImGui::TextDisabled("%d gib chunks loose", loose);
		}
		SliderI("Gib chunks per gib (%)", &chunks.CountPercent, 0, 200, "%d%%");
		Tip("How many of the solid pieces (bits of plating, flesh, bone and metal) come out when a body, wreck or crate breaks apart, against what each says: at 50 about half. Blood, flecks and sparks are unchanged, and so are the parts that were attached to it (limbs, heads, guns). 100: all of them, as the game comes.");
		SliderI("Most loose gib chunks", &chunks.MaxLoose, 0, 2000, chunks.MaxLoose > 0 ? "%d" : "No limit");
		Tip("With more pieces than this about, the oldest that have slowed down settle into the ground where they lie, as dropped items past the most kept do. One lying on a body or a unit is removed instead (or settles there, with Gib chunks settle on bodies ticked and the body dead); one still in the air waits until it lands. No limit: they settle only when they come to rest, as the game comes.");
		SliderI("Gib chunk settle time", &chunks.SettleMS, -1, 5000, chunks.SettleMS >= 0 ? "%d ms" : "As each piece says");
		Tip("How long a piece has to lie still before it settles into the ground and stops costing anything. Lower: they settle sooner. As each piece says: its own RestThreshold (half a second for most), as the game comes. A piece made never to settle still never does.");
		SliderI("Gib chunk bounces per update", &chunks.MaxBounces, 1, 10);
		Tip("The most times a piece can hit something in one update before it stops moving for that update. Pieces wedged among bodies hit something over and over; fewer is cheaper but they may jitter or slip into each other. 10, as for everything else, as the game comes.");
		SliderI("Gib chunk collision spacing", &chunks.AtomSpacing, 0, 16, chunks.AtomSpacing > 0 ? "%d px" : "As each piece says");
		Tip("The pixels between the points round a piece's outline that it collides with, for pieces made from now on. Higher: fewer points, cheaper, but they may sink a little into the ground and each other. As each piece says: its own AtomGroup Resolution (8 for the game's own), as the game comes.");
		Check("Gib chunks settle on bodies", &chunks.SettleOnBodies);
		Tip("A piece lying still on a dead body or on other pieces, rather than on the ground, settles there too, instead of jostling on it until the body is gone. Never on a unit still alive. Off, as the game comes: only on the ground.");
		g_SettingsMan.ClampGibChunks();
		if (Plain()) {
			if (ToolUI::Button("Reset to defaults")) {
				chunks = SettingsMan::GibChunkTuning();
			}
			ImGui::SetItemTooltip("Puts every Gibs setting back to how the game comes, which is how gibs always behaved.");
		}
	};

	auto cameraAndImage = [&]() {
		if (Plain()) {
			ImGui::TextUnformatted("Looks:");
			const std::pair<const char*, int> looks[] = {{"Natural", LightingSettings::LookNatural}, {"Gritty", LightingSettings::LookGritty}, {"Vivid", LightingSettings::LookVivid}, {"Noir", LightingSettings::LookNoir}};
			for (const auto& [name, look]: looks) {
				ImGui::SameLine();
				if (ToolUI::Button(name)) {
					settings.ApplyLook(look);
				}
			}
		}
		float cameraZoom = g_FrameMan.GetCameraZoom();
		if (Slider("Camera zoom", &cameraZoom, FrameMan::c_MinCameraZoom, FrameMan::c_MaxCameraZoom, "%.2fx")) {
			g_FrameMan.SetCameraZoom(cameraZoom);
		}
		Tip("Ctrl + mouse wheel in a game, or the wheel alone in the sandbox's view from above.");
		Heading("Exposure");
		Slider("Exposure", &settings.Exposure, 0.1F, 4.0F);
		Slider("Auto exposure", &settings.AutoExposure, 0.0F, 1.0F);
		Slider("Adapt below", &settings.AutoExposureLow, 0.001F, 0.2F, "%.3f", ImGuiSliderFlags_Logarithmic);
		Slider("Adapt above", &settings.AutoExposureHigh, 0.05F, 2.0F, "%.3f", ImGuiSliderFlags_Logarithmic);
		Check("Adapt in game time", &settings.AutoExposureGameTime);
		Tip("Exposure adapts as the game runs, so it holds while paused or in photo mode, and slows with slow motion. Off: it adapts in real time, as before.");
		if (SceneLighting* lighting = g_PostProcessMan.GetSceneLighting(); lighting && settings.Enabled && settings.AutoExposure > 0.0F && Plain()) {
			float averageLuminance = 0.0F;
			float autoExposure = 1.0F;
			lighting->ReadAutoExposure(averageLuminance, autoExposure);
			ImGui::TextDisabled("Scene luminance %.3f, auto exposure x%.2f", averageLuminance, autoExposure);
		}
		Slider("Highlight shoulder", &settings.ShoulderStart, 0.3F, 1.0F);
		Heading("Colour");
		Slider("Saturation", &settings.Saturation, 0.0F, 2.0F);
		Slider("Contrast", &settings.Contrast, 0.5F, 1.6F);
		Slider("Temperature", &settings.Temperature, -1.0F, 1.0F);
		Slider("Tint", &settings.Tint, -1.0F, 1.0F);
		Tint("Shadow tint", &settings.ShadowTint.x);
		Tint("Highlight tint", &settings.HighlightTint.x);
		Heading("Bloom");
		Check("Bloom", &settings.BloomEnabled);
		Slider("Bloom threshold", &settings.BloomThreshold, 0.0F, 4.0F);
		Slider("Bloom knee", &settings.BloomKnee, 0.01F, 1.0F);
		Slider("Bloom intensity", &settings.BloomIntensity, 0.0F, 3.0F);
		Heading("Lens and screen");
		Slider("Vignette", &settings.Vignette, 0.0F, 1.0F);
		Slider("Film grain", &settings.FilmGrain, 0.0F, 1.0F);
		Slider("Chromatic aberration (px)", &settings.ChromaticAberration, 0.0F, 4.0F);
		Slider("CRT scanlines", &settings.Scanlines, 0.0F, 1.0F);
		if (settings.Scanlines > 0.0F) {
			Combo("CRT style", &settings.CRTStyle, "Scanlines\0Aperture grille\0Shadow mask\0Scanlines with bloom\0");
			Tip("What the CRT effect looks like: dark lines between rows (as before), the vertical colour stripes of a Trinitron-style tube, the dot triads of a shadow-mask tube, or scanlines that bright pixels bloom across.");
		}
		Slider("Upscale sharpness", &settings.UpscaleSharpness, 0.0F, 1.0F);
		Tip("How crisp the picture is when it's scaled up to the window. 1: every game pixel an even, sharp block (as before). 0: plainly smoothed.");
		Toggle("Whole-number scaling", g_WindowMan.GetIntegerScaling(), [](bool on) { g_WindowMan.SetIntegerScaling(on); });
		Tip("Scale the picture to the window by a whole number only, so every game pixel is exactly as many screen pixels, with bars around it. Off: fill as much of the window as fits.");
		Check("Grade answers events", &settings.EventLooks);
		Tip("The colour grade reacts to what happens: it flashes washed-out and warm with a huge blast, drains and darkens at the edges when your unit is badly hurt, and warms by a fire. Scripts can pulse it and crossfade between looks. Off: the grade stays as you set it, as before.");
		if (settings.EventLooks) {
			Slider("Event grade strength", &settings.EventLookStrength, 0.0F, 2.0F);
			EventLookSwitches(settings);
		}
		Heading("Mods");
		Check("Mod shaders", &settings.ModShaders);
		Tip("Lets mods draw their objects with their own shaders (a cloaking field, a hologram) and give a scene or activity its own screen effect (a scanner overlay, a sandstorm filter). Off: everything is drawn with the game's own shaders.");
		if (settings.ModShaders) {
			Slider("Mod shader strength", &settings.ModShaderStrength, 0.0F, 1.0F);
			Tip("How strongly mods' shaders apply. A mod's shader decides what it does with this, and may ignore it.");
			if (Plain()) {
				const std::string& postShader = g_PostProcessMan.GetActivePostShaderName();
				ImGui::TextDisabled("Screen effect now: %s", postShader.empty() ? "none" : postShader.c_str());
			}
		}
		Check("Authored sprite maps", &settings.SpriteMaps);
		Tip("Sprites that come with their own normal and glow maps (mods' art, mostly) catch the light and glow the way the artist drew them. Off: every sprite gets the automatic bevel and palette glow.");
		if (settings.SpriteMaps) {
			Slider("Sprite map strength", &settings.SpriteMapStrength, 0.0F, 1.0F);
		}
	};

	auto gameAndHUD = [&]() {
		float timeScale = g_TimerMan.GetTimeScale();
		if (Slider("Game speed", &timeScale, 0.1F, 4.0F, "%.2fx", ImGuiSliderFlags_Logarithmic)) {
			g_TimerMan.SetTimeScale(timeScale);
		}
		if (Plain() && ToolUI::Button("Normal speed")) {
			g_TimerMan.SetTimeScale(1.0F);
		}
		Toggle("Mantle ledges and vault low obstacles", g_SettingsMan.MantlingEnabled(), [](bool on) { g_SettingsMan.SetMantlingEnabled(on); });
		Tip("Units, players' included, pull themselves up onto a ledge or over a low obstacle they walk or jet into, rather than needing the jetpack to get the height exactly right.");
		Toggle("Units step over objects in their way", g_SettingsMan.UnitsStepOverObjects(), [](bool on) { g_SettingsMan.SetUnitsStepOverObjects(on); });
		Tip("An AI unit held up by a loose body it bumps into (a corpse, a gib, a limb, but not a gun lying still, which bodies pass, or a scrap light enough to shove aside) leaps over it, hops it with the jet, finds a way round or waits, as it does a wreck. Off, as before: it walks into it until it counts as stuck, which after a couple of seconds tries a leap or a hop.");
		Toggle("Units see through grass and plants", g_SettingsMan.SensesPassPlants(), [](bool on) { g_SettingsMan.SetSensesPassPlants(on); });
		Tip("Units' own checks (getting up, leaping, landing, ducking, mantling, the walk's look ahead) treat grass, bushes and hanging plants as the open ground their bodies push through. Off, as before: a unit lying on grass may not get up, leaps and landings on thick grass go wrong, units duck under hanging plants and mantle onto bushes.");
		Toggle("Soldiers see through ladder rungs", g_SettingsMan.SensesPassRungs(), [](bool on) { g_SettingsMan.SetSensesPassRungs(on); });
		Tip("A soldier's checks treat a ladder's rungs as open, as its body passes them. Off, as before: rungs read as a floor in mid air, a wall beside a walk and in the way of a flight, and a lip to mantle onto (then dropped through).");
		Toggle("Units ignore loose crumbs", g_SettingsMan.SensesPassSpecks(), [](bool on) { g_SettingsMan.SetSensesPassSpecks(on); });
		Tip("Units' checks ignore crumbs of four pixels or fewer left by blasts and digging, as their bodies brush through them. Off, as before: units duck and crawl under floating crumbs, route round them and mantle against them.");
		Toggle("Push free when stuck", g_SettingsMan.StuckPushEnabled(), [](bool on) { g_SettingsMan.SetStuckPushEnabled(on); });
		Tip("AI soldiers stuck on terrain, once crouching, backing off, leaping, hopping, crawling and mantling haven't got them moving, get a small nudge up and on. Players' own units are never pushed.");
		Toggle("No map wrapping", g_SettingsMan.NoSceneWrap(), [](bool on) { g_SettingsMan.SetNoSceneWrap(on); });
		Tip("Every map has hard left and right edges and one copy of the world, instead of looping round. Takes effect when the next map loads.");
		float bleedOut = g_SettingsMan.BleedOutChance();
		if (Slider("Wounds that keep bleeding", &bleedOut, 0.0F, 100.0F, "%.0f%%")) {
			g_SettingsMan.SetBleedOutChance(bleedOut);
		}
		Tip("Every wound bleeds, and most stop after a while. This is the chance one keeps on bleeding instead, so the unit, players' and every other, keeps losing health until it's patched up or bleeds out. 0 is as before: wounds stop when they always did. 100: every wound keeps bleeding.");
		float bleedTime = g_SettingsMan.WoundBleedTime();
		if (Slider("Wound bleed time", &bleedTime, 0.1F, 10.0F, "%.2fx", ImGuiSliderFlags_Logarithmic)) {
			g_SettingsMan.SetWoundBleedTime(bleedTime);
		}
		Tip("How long a wound bleeds before it stops, as a multiple of normal, for the unit, players' and every other. Longer bleeds out more health: 2x bleeds twice as long and takes twice the health, 0.5x half. A plain flesh wound bleeds for about half a minute at 1x. Wounds kept bleeding by the setting above never stop either way. Takes effect on new wounds. 1x is normal.");
		if (Plain() && ToolUI::Button("Normal bleed time")) {
			g_SettingsMan.SetWoundBleedTime(1.0F);
		}
		Toggle("Crew killed in a vehicle fall out", g_SettingsMan.DeadCrewFallOut(), [](bool on) { g_SettingsMan.SetDeadCrewFallOut(on); });
		Tip("Riders in an open vehicle (the cart, boats, buggy and Moonhopper) can be shot in their seats. On: someone killed there falls out of the vehicle. Off: the body slumps in its seat until the vehicle is wrecked or thrown over, or someone climbs into that seat. Your own unit falls out either way. Tank crews sit inside, out of reach.");
		Toggle("Passengers fire from vehicles", g_SettingsMan.PassengersFireFromVehicles(), [](bool on) { g_SettingsMan.SetPassengersFireFromVehicles(on); });
		Tip("Units riding in the passenger seats of the vehicles built for it (the cart, boats, buggy, Moonhopper and Gunship) shoot out with the guns they hold: the AI at the nearest enemy in sight, your own unit where you aim. Their shots go through their own vehicle and its crew. The driver never shoots, and gunners man the vehicle's gun as before.");
		float shipClearance = g_SettingsMan.ShipGroundClearance();
		if (Slider("Ship ground clearance", &shipClearance, 0.0F, 200.0F, "%.0f px")) {
			g_SettingsMan.SetShipGroundClearance(shipClearance);
		}
		Tip("How close flying ships (the Gunship and Frigate) fly to the ground when they're sent down low: the gap kept between the bottom of the hull and the ground, on the way and where they stop, for the AI's ships and yours alike. They still climb over hills in their way and rise higher for a clear shot. A cautious order flies up to a hull's height higher. Dropping passengers off, they hover a little higher so there is room to get out. 12 px is normal: just clear of the ground.");
		if (Plain() && ToolUI::Button("Normal ship clearance")) {
			g_SettingsMan.SetShipGroundClearance(12.0F);
		}
		float graceZone = g_SettingsMan.ShipGraceZone();
		if (Slider("Ship room past map edges", &graceZone, 0.0F, 700.0F, "%.0f px")) {
			g_SettingsMan.SetShipGraceZone(graceZone);
		}
		Tip("How far flying ships (the Gunship and Frigate) can go above the top of the map, and past its sides where it doesn't wrap, before they leave for orbit. With room up there they can fly over buildings at the very top of the map and drop people onto them, and you can send them there. Drop ships and rockets still leave just over the edge. 0 is the old line. 400 px is normal.");
		float overscroll = g_SettingsMan.CameraOverscroll();
		if (Slider("Camera scroll past map edges", &overscroll, 0.0F, 2000.0F, "%.0f px")) {
			g_SettingsMan.SetCameraOverscroll(overscroll);
		}
		Tip("How far the camera can scroll or pan past the top, sides and bottom of a map that doesn't wrap, so what's at the edge can be brought into the middle of the screen. 0 stops at the edge as before. 400 px is normal.");
		float terrainDamage = g_SettingsMan.WeaponTerrainDamage();
		if (Slider("Weapon damage to terrain", &terrainDamage, 0.1F, 20.0F, "%.2fx", ImGuiSliderFlags_Logarithmic)) {
			g_SettingsMan.SetWeaponTerrainDamage(terrainDamage);
		}
		Tip("How hard bullets, shrapnel and other weapon fire strike the ground and buildings, as a multiple of normal. Higher, a bullet cuts through stronger stuff (concrete, then metal) and goes further into it before it stops; lower, shots barely mark the ground. Diggers dig by the same rule, so they scale too. Damage to units is unchanged. 1x is normal.");
		if (Plain() && ToolUI::Button("Normal terrain damage")) {
			g_SettingsMan.SetWeaponTerrainDamage(1.0F);
		}
		Heading("Unit toughness");
		{
			struct ToughnessSlider {
				const char* Label;
				SettingsMan::ToughnessKind Kind;
				const char* Tip;
			};
			static constexpr const char* c_ToughnessHow = " Higher takes less damage from everything (shots, bleeding, fire, drowning, falls, crushing) and takes that many more wounds and harder blows before it is blown apart or loses a limb or head; lower the other way round. 2x takes half the damage, 0.5x double. All the way up is Invincible: no damage, and never blown apart. Healing is unchanged. 1x is normal.";
			const ToughnessSlider sliders[] = {
			    {"Unit toughness", SettingsMan::ToughnessKind::Units, "How tough units on foot are, players' and brains' included, on every side."},
			    {"Vehicle toughness", SettingsMan::ToughnessKind::Vehicles, "How tough ground vehicles are: wheeled vehicles, tanks, dreadnoughts and turrets. Crew keep their own unit toughness."},
			    {"Flying craft toughness", SettingsMan::ToughnessKind::Craft, "How tough flying craft are: drop ships, rockets and flying ships. Crew keep their own unit toughness."},
			    {"Animal toughness", SettingsMan::ToughnessKind::Animals, "How tough animals are: livestock and wildlife such as crabs."},
			    {"Creature toughness", SettingsMan::ToughnessKind::Creatures, "How tough creatures are: monsters such as hounds."},
			};
			for (const ToughnessSlider& slider: sliders) {
				float toughness = g_SettingsMan.Toughness(slider.Kind);
				const char* format = g_SettingsMan.IsInvincible(slider.Kind) ? "Invincible" : "%.2fx";
				if (Slider(slider.Label, &toughness, SettingsMan::c_ToughnessMin, SettingsMan::c_ToughnessInvincible, format, ImGuiSliderFlags_Logarithmic)) {
					g_SettingsMan.SetToughness(slider.Kind, toughness);
				}
				Tip((std::string(slider.Tip) + c_ToughnessHow).c_str());
				if (s_LastShown) {
					ImGui::PushID(slider.Label);
					ImGui::SameLine();
					ImGui::BeginDisabled(toughness == 1.0F);
					if (ToolUI::Button("Reset")) {
						g_SettingsMan.SetToughness(slider.Kind, 1.0F);
					}
					ImGui::SetItemTooltip("Back to 1x, as the game comes.");
					ImGui::EndDisabled();
					ImGui::PopID();
				}
			}
		}
		Check("Team colour on units", &settings.UnitTeamColorParts);
		Tip("Parts of units that mods mark for it (a bandanna, a helmet stripe) are drawn in their side's colour: red, green, blue or yellow. Unticked, they take the mod's own default colour, or stay as painted.");
		Heading("Unit outlines");
		Check("Outline units", &settings.UnitOutline);
		Tip("A stroke round each unit and what it holds, so they stand out. It goes over the sky, the background and other objects, never over terrain.");
		Check("Outline over everything", &settings.UnitOutlineOverEverything);
		Tip("Draw the outline over terrain and water too, above everything but post-processing, so a unit hidden behind them still shows.");
		Slider("Outline width (px)", &settings.UnitOutlineWidth, 1.0F, 4.0F, "%.1f");
		Tip("In the game's pixels. Zoomed out, the stroke is thickened to keep its size on screen.");
		Check("Outline in team colour", &settings.UnitOutlineTeamColor);
		Tip("Red, green, blue or yellow by side, white for units of no side. Unticked, every outline takes the colour below.");
		if (!settings.UnitOutlineTeamColor) {
			Tint("Outline colour", &settings.UnitOutlineColor.x);
		}
		Slider("Outline opacity", &settings.UnitOutlineOpacity, 0.0F, 1.0F);
		Slider("Outline glow light", &settings.UnitOutlineGlow, 0.0F, 2.0F);
		Tip("Each outlined unit gives off a soft light in its outline's colour (its side's, the colour above, or a flag carrier's pink), lighting the ground and units round it. Needs lighting on. 0 is off.");
		Heading("HUD");
		Toggle("Show FPS and version", g_SettingsMan.ShowFPSAndVersion(), [](bool on) { g_SettingsMan.SetShowFPSAndVersion(on); });
		Tip("The frame rate and the game's version, small, in the top right of the window.");
		Toggle("Modern HUD", ModernHUD::IsEnabled(), [](bool on) { ModernHUD::SetEnabled(on); });
		Toggle("Notices at the top of the screen", g_SettingsMan.ShowNotices(), [](bool on) { g_SettingsMan.SetShowNotices(on); });
		Tip("Short notes at the top of the screen for a few seconds when something is done that is hard to see otherwise: \"Game saved\", a preset loaded, \"Nothing to undo\", a camera spot kept, or a sandbox tool used while its simulation is off.");
		Slider("UI lighting", &ToolLight::Strength, 0.0F, 2.0F);
		Tip("The menus and HUD lit as if they were in the world: the light brightens the side of each panel facing it, shades the far side, catches the edges towards it and casts the panel's shadow away from it. They are never dimmed past reading. 0 turns it off.");
		{
			int source = static_cast<int>(ToolLight::Mode);
			if (Combo("UI light", &source, "Follow the game\0Its own light\0")) {
				ToolLight::Mode = static_cast<ToolLight::Source>(source);
			}
			Tip("Follow the game: the game's sun by day and moon by night, moving with the time of day, in the sky's colour of the hour (gold at dawn and dusk), with lightning flashing on the panels. Out of a game, a steady light from the top left. Its own light: a light just for the menus and HUD, set below, that the game's time of day and weather don't touch.");
		}
		if (ToolLight::Mode == ToolLight::Source::OwnLight) {
			Slider("UI light angle", &ToolLight::OwnAngle, -90.0F, 90.0F, "%.0f");
			Tip("Where the UI's own light comes from, in degrees from straight above: negative from the left, positive from the right.");
			Slider("UI light height", &ToolLight::OwnHeight, 0.0F, 1.0F);
			Tip("How high the UI's own light stands: low throws long shadows, high short ones.");
			Slider("UI light brightness", &ToolLight::OwnBrightness, 0.0F, 1.0F);
			Tint("UI light colour", ToolLight::OwnColor);
		}
		Check("UI lit by nearby lamps and fires (may crash)", &ToolLight::NearbyLights);
		Tip("Lamps, fires, blasts and muzzle flashes in the world under a panel light it warmly too. WARNING: this reads the lights the AI's sight uses while the game may be changing them, so it may crash the game. Off unless you want to try it.");
		Toggle("Side and health beside units", g_SettingsMan.ShowUnitTags(), [](bool on) { g_SettingsMan.SetShowUnitTags(on); });
		Tip("Each unit's team icon and health number. In the Sandbox game mode every side's are shown; in other games, other sides' only where your side has seen and with \"Show enemy HUD\" (Options, Gameplay) on. Units of no side (training dummies) and those a mod hides never have them.");
		Toggle("CPU units' aim reticles", g_SettingsMan.ShowCPUAimReticles(), [](bool on) { g_SettingsMan.SetShowCPUAimReticles(on); });
		Tip("The yellow dots along the sights of a weapon a CPU-controlled unit is aiming, and of a turret's. Off: only your own units' are drawn. Your own aim is always shown.");
		Toggle("Alert mark over units", g_SettingsMan.ShowAlarmExclamation(), [](bool on) { g_SettingsMan.SetShowAlarmExclamation(on); });
		Tip("The exclamation mark that pops up over a unit's head when it is alerted to an enemy or a noise.");
		Toggle("Classic pie wheel", g_SettingsMan.ClassicPieWheel(), [](bool on) { g_SettingsMan.SetClassicPieWheel(on); });
		Tip("The old wheels on right click, for a unit you play and for the sandbox's command tool, instead of the unit menu and the action menu: lists above the pointer with every order and the weapons and movement rules on one layer. (A gamepad, and players after the first, always get the unit's wheel.)");
		Toggle("Unit menu opens on a click", g_SettingsMan.UnitMenuClickToOpen(), [](bool on) { g_SettingsMan.SetUnitMenuClickToOpen(on); });
		Tip("On: a right click opens the menu of the unit you play (its inventory, what it can do, its rules) and it stays up till you click away from it or right click again. Off: it is up only while you hold the right button, and letting go over a row picks it. (Holding, moving onto a row and letting go picks it either way.)");
		if (float scale = g_SettingsMan.UnitMenuTimeScale(); Slider("Time while the unit menu is up", &scale, 0.05F, 1.0F, "%.2fx")) {
			g_SettingsMan.SetUnitMenuTimeScale(scale);
		}
		Tip("How fast time runs while the menu of the unit you play is up: 1x as ever, lower slows the world down so you can choose. Back to the speed it was when the menu goes.");
		Toggle("Smooth HUD text", TextOverlay::IsEnabled(), [](bool on) { TextOverlay::SetEnabled(on); });
		int frameCap = g_WindowMan.GetFrameCap();
		if (SliderI("Frame cap (0 = none)", &frameCap, 0, 360)) {
			g_WindowMan.SetFrameCap(frameCap > 0 && frameCap < 30 ? 30 : frameCap);
		}
		Heading("Battle alerts and display (Battle Command)");
		Toggle("Battle alerts", g_SettingsMan.ShowBattleAlerts(), [](bool on) { g_SettingsMan.SetShowBattleAlerts(on); });
		Tip("What needs your eye in a battle (your units under attack or falling, a strike coming in, your flag taken...) listed at the top left of the view for a while, each to click and look there. Off: not listed, but still kept in the alert log, and Space and Shift+Space still look at them.");
		if (int lines = g_SettingsMan.BattleAlertLines(); SliderI("Battle alert lines", &lines, 1, 12)) {
			g_SettingsMan.SetBattleAlertLines(lines);
		}
		Tip("The most alerts listed at once: a new one pushes the oldest off. 6 as before.");
		if (int seconds = g_SettingsMan.BattleAlertSeconds(); SliderI("Battle alert time", &seconds, 3, 60, "%d s")) {
			g_SettingsMan.SetBattleAlertSeconds(seconds);
		}
		Tip("How long an alert stays listed, the last two seconds fading. 12 s as before.");
		Toggle("Battle alert sounds", g_SettingsMan.BattleAlertSounds(), [](bool on) { g_SettingsMan.SetBattleAlertSounds(on); });
		Tip("A short sound as each alert comes, one for each kind (under attack, a unit lost, your brain or flag, a strike, reinforcements...), so you hear what needs you without looking. Several at once make one sound.");
		if (float volume = g_SettingsMan.BattleAlertVolume() * 100.0F; Slider("Battle alert volume", &volume, 0.0F, 100.0F, "%.0f%%")) {
			g_SettingsMan.SetBattleAlertVolume(volume / 100.0F);
		}
		Tip("How loud the alert sounds are, under the game's own sound volume.");
		Toggle("Kill feed", g_SettingsMan.ShowBattleKillFeed(), [](bool on) { g_SettingsMan.SetShowBattleKillFeed(on); });
		Tip("Who killed whom in a battle, listed at the top right of the view, each side in its colour.");
		if (int lines = g_SettingsMan.BattleKillLines(); SliderI("Kill feed lines", &lines, 1, 12)) {
			g_SettingsMan.SetBattleKillLines(lines);
		}
		Tip("The most kills listed at once: a new one pushes the oldest off. 6 as before.");
		if (int seconds = g_SettingsMan.BattleKillSeconds(); SliderI("Kill feed time", &seconds, 3, 60, "%d s")) {
			g_SettingsMan.SetBattleKillSeconds(seconds);
		}
		Tip("How long a kill stays listed, the last two seconds fading. 10 s as before.");
		Toggle("Ask before losing a battle setup", g_SettingsMan.BattleAskFirst(), [](bool on) { g_SettingsMan.SetBattleAskFirst(on); });
		Tip("The battle panel asks first before a saved setup is deleted or saved over, and before a battle that's on is started again or stopped. Ctrl+click skips the question either way. Off is the same as ticking \"Don't ask again\".");
		Toggle("Show battle info", g_SettingsMan.ShowBattleInfo(), [](bool on) { g_SettingsMan.SetShowBattleInfo(on); });
		Tip("While a battle is on, a panel under the score: each team's tally, units in and fallen, respawns left, and how many are waiting to come back and when the next does. The same as the box on the battle panel.");
		Toggle("Show battle objectives", g_SettingsMan.ShowBattleObjectives(), [](bool on) { g_SettingsMan.SetShowBattleObjectives(on); });
		Tip("What the game is about lit up on the map and the map window: flags, VIPs, hills, assault objectives and goals. The same as the box on the battle panel.");
		if (int look = g_SettingsMan.BattleObjectiveLook(); Combo("Battle objective look", &look, "Each mode's own\0Glowing ring\0Glowing outline\0Glowing ground line\0Glowing terrain\0")) {
			g_SettingsMan.SetBattleObjectiveLook(look);
		}
		Tip("How zone objectives (hills, assault objectives, goals) are lit up: as the mode has them, or all the same way. Flags and VIPs always get a ring.");
		Toggle("Show spawn zones on the map", g_SettingsMan.ShowBattleSpawnZones(), [](bool on) { g_SettingsMan.SetShowBattleSpawnZones(on); });
		Tip("The outline and shading of each team's spawn zones, on the map and the map window. Off, they're hidden (still shown while you draw one or place a point).");
		Toggle("Show mode zones on the map", g_SettingsMan.ShowBattleModeZones(), [](bool on) { g_SettingsMan.SetShowBattleModeZones(on); });
		Tip("The outline and shading of a mode's own zones (hills, assault objectives, goal zones), on the map and the map window. Off, they're hidden but for their names (still shown while you draw one).");
		Heading("Unit speech");
		Toggle("Unit speech", UnitSpeech::IsEnabled(), [](bool on) { UnitSpeech::SetEnabled(on); });
		Tip("Units say short lines over their heads when their AI does something: \"Take cover!\", \"Reloading!\", \"Got one!\". The lines are in Base.rte/Speech.ini, and mods can add their own.");
		if (UnitSpeech::IsEnabled()) {
			int chance = UnitSpeech::GetChance();
			if (SliderI("Speech chance", &chance, 0, 100, "%d%%")) {
				UnitSpeech::SetChance(chance);
			}
			Tip("How likely a unit is to say something when it does one of the things below. 100%: nearly every time (a unit still waits a few seconds before saying the same thing again, and a squad doesn't all say it at once).");
			Toggle("Hear other sides' units", UnitSpeech::ShowsEnemies(), [](bool on) { UnitSpeech::SetShowsEnemies(on); });
			Tip("Enemy units' lines too, where your side can see them. Off: only your own side's.");
			Toggle("Speech bubbles", UnitSpeech::ShowsBubbles(), [](bool on) { UnitSpeech::SetShowsBubbles(on); });
			Tip("Lines in a dark box edged in the side's colour, with a tail pointing at the unit. Off: just the text over the unit's head.");
			Toggle("Speech sounds", UnitSpeechVoice::IsEnabled(), [](bool on) { UnitSpeechVoice::SetEnabled(on); });
			Tip("A voice for each line a unit on your screen says, made in the game from the line's words: most units babble in a voice of their own, robots speak the words, all over a radio. What each kind of unit sounds like is in Base.rte/Voices.ini, and mods can add their own.");
			if (UnitSpeechVoice::IsEnabled()) {
				int volume = UnitSpeechVoice::GetVolume();
				if (SliderI("Speech volume", &volume, 0, 100, "%d%%")) {
					UnitSpeechVoice::SetVolume(volume);
				}
				Tip("How loud the speech voices are, against units' other sounds. They also follow the sound mixer's Voices slider (Options, Audio), with pain and death cries.");
				int radio = UnitSpeechVoice::GetRadio();
				if (SliderI("Radio effect", &radio, 0, 100, "%d%%")) {
					UnitSpeechVoice::SetRadio(radio);
				}
				Tip("How much the voices sound like they come over a field radio: band-limited and crackly, with a squelch as each line starts and ends, and a roger beep after Serious lines. 0%: clean voices.");
				Toggle("Hear other sides' voices", UnitSpeechVoice::HearsEnemies(), [](bool on) { UnitSpeechVoice::SetHearsEnemies(on); });
				Tip("Enemy units' lines make a sound too, a little quieter, where their lines show (\"Hear other sides' units\" above, and seen by your side). Off: only your own side's.");
				Toggle("Robots speak words", UnitSpeechVoice::SpeaksWords(), [](bool on) { UnitSpeechVoice::SetSpeaksWords(on); });
				Tip("Robots (Dummy, Imperatus, drones and walkers) say the actual words in a synthesised voice. Off: they babble like everyone else, in their own machine voice.");
			}
			// Each side's tones: what kind of lines its units say. None ticked is any.
			{
				static const std::array<const char*, 4> sideNames{"Red", "Green", "Blue", "Yellow"};
				const std::vector<std::string> tones = UnitSpeech::GetTones();
				// The mix of tones: how often each comes up against the others. A side shares it between the tones it speaks in.
				int toneTotal = 0;
				for (const std::string& tone: tones) {
					toneTotal += UnitSpeech::GetToneWeight(tone);
				}
				for (const std::string& tone: tones) {
					int weight = UnitSpeech::GetToneWeight(tone);
					const int share = toneTotal > 0 ? (weight * 100 + toneTotal / 2) / toneTotal : 0;
					std::string label = tone + " lines, share of the mix##SpeechToneMix" + tone;
					std::string format = "%d (" + std::to_string(share) + "%% of lines)";
					if (SliderI(label.c_str(), &weight, 0, 100, format.c_str())) {
						UnitSpeech::SetToneWeight(tone, weight);
					}
					Tip("How often a unit says a line of this tone against the others: Funny 90 and Serious 10 is nine funny lines to one serious. "
					    "A side that speaks only some tones (below) shares the whole mix between those, keeping their balance; a side of one tone always speaks it. "
					    "Speech chance above still decides how often anything is said at all.");
				}
				for (int team = 0; team < 4; ++team) {
					std::string anyLabel = std::string(sideNames[team]) + " side speaks: any tone##SpeechToneAny" + std::to_string(team);
					Toggle(anyLabel.c_str(), UnitSpeech::TeamUsesAnyTone(team), [team](bool on) {
						if (on) {
							UnitSpeech::SetTeamAnyTone(team);
						}
					});
					Tip("Its units say lines of every tone. Untick by picking one or more tones instead.");
					for (const std::string& tone: tones) {
						if (s_LastShown) {
							ImGui::SameLine();
						}
						std::string label = tone + "##SpeechTone" + std::to_string(team) + tone;
						const bool on = !UnitSpeech::TeamUsesAnyTone(team) && UnitSpeech::TeamUsesTone(team, tone);
						Toggle(label.c_str(), on, [team, tone](bool set) { UnitSpeech::SetTeamTone(team, tone, set); });
						const std::string description = UnitSpeech::GetToneDescription(tone);
						std::string tip = std::string(sideNames[team]) + " side's units say " + tone + " lines" + (description.empty() ? "." : ": " + description) +
						                  " Tick more than one to mix them.";
						Tip(tip.c_str());
					}
				}
			}
			// The triggers under their groups (Speech.ini's Group), each group folding away with buttons to turn all of it on or off; a search
			// lists the matching ones flat.
			const std::vector<UnitSpeech::Trigger>& triggers = UnitSpeech::GetTriggers();
			std::vector<std::string> groups;
			for (const UnitSpeech::Trigger& trigger: triggers) {
				const std::string group = trigger.Group.empty() ? "Other" : trigger.Group;
				if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
					groups.push_back(group);
				}
			}
			for (const std::string& group: groups) {
				auto inGroup = [&group](const UnitSpeech::Trigger& trigger) { return (trigger.Group.empty() ? "Other" : trigger.Group) == group; };
				bool open = true;
				if (Plain()) {
					int count = 0;
					int on = 0;
					for (const UnitSpeech::Trigger& trigger: triggers) {
						if (inGroup(trigger)) {
							++count;
							on += UnitSpeech::IsTriggerOn(trigger.Key) ? 1 : 0;
						}
					}
					std::string header = "Speech: " + group + " (" + std::to_string(on) + "/" + std::to_string(count) + " on)###SpeechGroup" + group;
					open = ImGui::TreeNode(header.c_str());
					if (open) {
						std::string allOn = "All on##SpeechAllOn" + group;
						std::string allOff = "All off##SpeechAllOff" + group;
						bool setAll = false;
						bool setTo = true;
						if (ToolUI::Button(allOn.c_str())) {
							setAll = true;
						}
						ImGui::SameLine();
						if (ToolUI::Button(allOff.c_str())) {
							setAll = true;
							setTo = false;
						}
						if (setAll) {
							for (const UnitSpeech::Trigger& trigger: triggers) {
								if (inGroup(trigger)) {
									UnitSpeech::SetTriggerOn(trigger.Key, setTo);
								}
							}
						}
					}
				}
				if (!open) {
					continue;
				}
				for (const UnitSpeech::Trigger& trigger: triggers) {
					if (!inGroup(trigger)) {
						continue;
					}
					std::string label = "Speech: " + trigger.Name + "##Speech" + trigger.Key;
					Toggle(label.c_str(), UnitSpeech::IsTriggerOn(trigger.Key), [&trigger](bool on) { UnitSpeech::SetTriggerOn(trigger.Key, on); });
					std::string example = UnitSpeech::GetExampleLine(UnitSpeech::FindTrigger(trigger.Key));
					std::string tip = trigger.Description.empty() ? trigger.Name : trigger.Description;
					if (!example.empty()) {
						tip += "\nFor example: \"" + example + "\"";
					}
					Tip(tip.c_str());
				}
				if (Plain()) {
					ImGui::TreePop();
				}
			}
			if (Plain() && ToolUI::Button("Reload speech lines")) {
				UnitSpeech::Reload();
			}
			if (Plain()) {
				ImGui::SetItemTooltip("Reads every Speech.ini and Voices.ini again, for trying out lines and voices without restarting.");
			}
		}
	};

	// How the AI behaves: what it notices, how fire and losses get to it, and how many chances it takes getting about.
	auto aiBehaviour = [&]() {
		Toggle("Pause AI", Controller::IsAIPaused(), [](bool on) { Controller::SetAIPaused(on); });
		Tip("The AI stops thinking and acting, while the world and the players carry on. The key below does the same in any game.");
		PauseAIKey();
		Check("Night, light and noise affect AI", &settings.NightAffectsAI);
		Tip("Stealth. At night the AI sees less far, a unit in the dark or under a roof is harder to spot, and one under a lamp or wearing a lit headlamp is easier. The AI also hears footsteps: running is loud, walking quieter and crawling quietest, and metal floors ring. Sneak past sentries by keeping to the shadows and walking.");
		Toggle("AI remembers and shares sightings", ThreatMemory::IsEnabled(), [](bool on) { ThreatMemory::SetEnabled(on); });
		Tip("A unit that spots an enemy tells its team: AI teammates close by turn to face it, and the team remembers where each enemy was last seen for a minute. Units that lost sight of an enemy look there, AI units on patrol go and check the last place they saw your units, and idle ones keep watch toward it. Off: each unit knows only what it sees.");
		{
			float suppression = g_SettingsMan.AISuppression();
			if (Slider("AI suppression and morale", &suppression, 0.0F, 2.0F, "%.2fx")) {
				g_SettingsMan.SetAISuppression(suppression);
			}
			Tip("How much fire pins AI units down: shots cracking past and blasts nearby make them duck, crawl, run for cover and shoot worse, and losses, wounds and fire shake their nerve until they pull back. 0 turns it off; machines never feel it, and Unfair AI ignores it.");
		}
		Toggle("Units break when shaken", g_SettingsMan.AIBreaksWhenShaken(), [](bool on) { g_SettingsMan.SetAIBreaksWhenShaken(on); });
		Tip("A unit whose nerve has gone (losses, wounds and fire wear it down) breaks and falls back, whatever its health, until it steadies or is rallied; in Battle Command yours show a \"!\" over them. Off: suppression still makes them duck and crawl, but nobody runs.");
		Toggle("Units pick up better weapons", g_SettingsMan.AIScavengesBetterWeapons(), [](bool on) { g_SettingsMan.SetAIScavengesBetterWeapons(on); });
		Tip("An armed unit with nothing to fight looks round every so often for a loose gun lying near that's clearly better than its best (worth a third more), or a grenade when it has nothing to throw, and goes to pick it up, then carries on. Not one told to hold its position, nor one following a squad. Units with no gun at all look for one whatever this is.");
		Toggle("Units call lifts", g_SettingsMan.AIUnitsCallLifts(), [](bool on) { g_SettingsMan.SetAIUnitsCallLifts(on); });
		Tip("AI soldiers route by lifts and moving platforms. On: a unit waiting at a stop calls the deck to it, and a rider sends it on to the stop it wants; the deck then goes back to its own round. Off: they wait for a shuttling deck to come round on its own, and leave alone lifts that only come when called.");
		{
			float dig = g_SettingsMan.AIDigWillingness();
			if (Slider("AI digging", &dig, 0.0F, 2.0F, "%.2fx")) {
				g_SettingsMan.SetAIDigWillingness(dig);
			}
			Tip("How readily units carrying a digger tunnel through ground instead of going round it: at 1 a short cut through a hill or a bank of earth beats a long walk round, the softer the ground and the stronger the digger the sooner. Units only dig what their digger's regular rounds cut, and give up and go round when a cut stops getting anywhere. 0 digs only when there is no other way.");
		}
		{
			float threats = g_SettingsMan.AIThreatAvoidance();
			if (Slider("Safe routes in game modes", &threats, 0.0F, 2.0F, "%.2fx")) {
				g_SettingsMan.SetAIThreatAvoidance(threats);
			}
			Tip("How much a unit that a game mode wants kept safe weighs the enemies along a route when picking one: a capture the flag carrier taking an enemy flag home. At 1 a way past a crowd of enemies loses to a longer one past none: twenty in the way are worth walking most of a large map round, while a lone sentry is only skirted when going round is short. Places they are sent to are reached however many enemies are there. Every other unit takes the shortest way. 0 turns it off.");
		}
		Heading("Enemy commander (Battle Command)");
		Toggle("Enemy buys reinforcements", g_SettingsMan.AIEnemyBuysReinforcements(), [](bool on) { g_SettingsMan.SetAIEnemyBuysReinforcements(on); });
		Tip("An enemy team with an AI commander earns points as you do (kills, objectives) and spends them on the same reinforcement cards at the same prices, landing them by its base. How well it picks follows the commanders' difficulty on the battle panel. Off: it sends only the mode's own waves.");
		Toggle("Enemy uses powers", g_SettingsMan.AIEnemyUsesPowers(), [](bool on) { g_SettingsMan.SetAIEnemyUsesPowers(on); });
		Tip("An enemy team with an AI commander uses the same commander powers as you, on the same cooldowns: strikes on your units bunched up, supplies and rallies for its own. Its artillery and orbital strikes are marked where they'll land a few seconds before they hit, so you can scatter. Off: it uses none.");
		Heading("Playing a unit (Battle Command)");
		Toggle("Squad follows you", g_SettingsMan.AISquadFollowsYou(), [](bool on) { g_SettingsMan.SetAISquadFollowsYou(on); });
		Tip("Take control of one of your units with others selected and they follow it as its squad, fighting as they go; while you fire, they go for the enemy you're aiming at. T: hold where they are, or follow again. X: regroup on you. Let go of the unit and they hold where they stand. Off: they stay where they were.");
		{
			float spawnDiggers = g_SettingsMan.AISpawnDiggerChance();
			if (Slider("Units spawn with a digger", &spawnDiggers, 0.0F, 100.0F, "%.0f%%")) {
				g_SettingsMan.SetAISpawnDiggerChance(spawnDiggers);
			}
			Tip("The share of units, every team's, that are handed a digger as they come into the scene, whether bought, dropped in or placed with it, if they don't carry one already. It goes in their inventory, so they keep their own guns in hand and get it out when a route calls for digging. 0 hands out none.");
			int diggerType = g_SettingsMan.AISpawnDiggerType();
			if (Combo("Digger they spawn with", &diggerType, "Light Digger\0Medium Digger\0Heavy Digger\0A random one\0")) {
				g_SettingsMan.SetAISpawnDiggerType(diggerType);
			}
			Tip("Which digger those units are handed. The heavier the digger, the harder the ground it cuts through and the sooner they choose to dig.");
		}
		Toggle("AI units use vehicles", g_SettingsMan.AIUsesVehicles(), [](bool on) { g_SettingsMan.SetAIUsesVehicles(on); });
		Tip("An AI unit sent a long way walks to a friendly vehicle near it that is going its way, gets in and rides (or drives it) there, then gets out and walks the rest. A vehicle with an AI driver drives itself to the place it is sent, and units sent to a vehicle get in. Off: they walk, and AI drivers sit still.");
		Heading("Take cover order");
		Toggle("Dig a foxhole when there's no cover", g_SettingsMan.AICoverDigs(), [](bool on) { g_SettingsMan.SetAICoverDigs(on); });
		Tip("Told to take cover with nothing to get behind within about 150 px, a unit carrying a digger on soft ground (sand, soil, earth) digs itself a foxhole where it stands and crouches in it. Off: it doesn't dig, and falls back or lies flat instead.");
		Toggle("Fall back when exposed", g_SettingsMan.AICoverFallsBack(), [](bool on) { g_SettingsMan.SetAICoverFallsBack(on); });
		Tip("Told to take cover with nothing to get behind and no foxhole dug, a unit falls back 200 px away from the enemy if one is within 250 px or it is pinned down. Off: it stays where it is.");
		Toggle("Lie flat when exposed", g_SettingsMan.AICoverLiesFlat(), [](bool on) { g_SettingsMan.SetAICoverLiesFlat(on); });
		Tip("Told to take cover with nothing to get behind, a unit that doesn't dig in lies flat where it ends up. Off: it crouches.");
		Heading("AI combat");
		{
			// Each scales what the AI scripts already work out from the team's AI skill, the unit's own settings and its veteran rank;
			// at its default the scripts leave that untouched, so Reset gives exactly the AI as it always played.
			SettingsMan::AICombatTuning& combat = g_SettingsMan.AICombat();
			const SettingsMan::AICombatTuning defaults;
			Slider("Shot accuracy", &combat.Accuracy, 0.25F, 4.0F, "%.2fx", ImGuiSliderFlags_Logarithmic);
			Tip("How well AI units aim: their aim error on the first shot, while tracking a target and when leading a moving one is divided by this. 2: half the miss, 0.5: twice it. It works on top of the team's AI skill from the game setup and each unit's rank. 1, as the game comes.");
			ResetButton("Accuracy", &combat.Accuracy, defaults.Accuracy);
			Slider("Reaction time", &combat.Reaction, 0.25F, 3.0F, "%.2fx", ImGuiSliderFlags_Logarithmic);
			Tip("How long AI units take to fire on a target they've just seen and to settle their aim on it: those delays are multiplied by this. Lower is quicker (at the default skill the first shot comes about half a second after they turn on a target). 1, as the game comes.");
			ResetButton("Reaction", &combat.Reaction, defaults.Reaction);
			Slider("Spotting speed", &combat.Spotting, 0.25F, 3.0F, "%.2fx", ImGuiSliderFlags_Logarithmic);
			Tip("How quickly an AI unit notices an enemy in its view, from a third of a second for a keen unit in the open to much longer for one in the dark or behind grass: that rate is multiplied by this. 1, as the game comes.");
			ResetButton("Spotting", &combat.Spotting, defaults.Spotting);
			Slider("Sight range", &combat.SightRange, 0.5F, 2.0F, "%.2fx");
			Tip("How far AI units look for enemies (their aim distance and half a screen): multiplied by this. Night and stealth still cut it down as before. 1, as the game comes.");
			ResetButton("SightRange", &combat.SightRange, defaults.SightRange);
			Slider("Target memory", &combat.TargetMemory, 0.25F, 4.0F, "%.2fx", ImGuiSliderFlags_Logarithmic);
			Tip("How long an AI unit keeps firing at and hunting a target after losing sight of it before it gives up on it: each of the times it gives (about a second while shooting, longer when it goes after the target) is multiplied by this. 1, as the game comes.");
			ResetButton("TargetMemory", &combat.TargetMemory, defaults.TargetMemory);
			float grenades = combat.GrenadeUse * 100.0F;
			if (Slider("Grenade use", &grenades, 0.0F, 100.0F, "%.0f%%")) {
				combat.GrenadeUse = grenades / 100.0F;
			}
			Tip("The chance an AI unit takes a throw it judges worth making: opening on a target with a grenade, or a javelin, axe or knives before closing in. 0: they keep to their guns. 100%, as the game comes (every grenade throw, and three in four of the thrown weapons).");
			ResetButton("GrenadeUse", &combat.GrenadeUse, defaults.GrenadeUse);
			Slider("Burst length", &combat.Burst, 0.5F, 2.0F, "%.2fx");
			Tip("How long an AI unit with an automatic weapon holds the trigger in each burst at range (it already shortens them further out and when pinned down; close in it holds the trigger down whatever this is): multiplied by this. 1, as the game comes.");
			ResetButton("Burst", &combat.Burst, defaults.Burst);
			g_SettingsMan.ClampAICombat();
			if (Plain()) {
				if (ToolUI::Button("Reset AI combat to defaults")) {
					combat = defaults;
				}
				ImGui::SetItemTooltip("Puts every AI combat setting back to how the game comes, which is how the AI always fought.");
			}
		}
	};

	// How units get about: the AI's moves and jetpack, and ladders.
	auto movement = [&]() {
		Heading("AI movement");
		{
			float recklessness = g_SettingsMan.AIRecklessness() * 100.0F;
			if (Slider("AI movement recklessness", &recklessness, 0.0F, 100.0F, "%.0f%%")) {
				g_SettingsMan.SetAIRecklessness(recklessness / 100.0F);
			}
			Tip("How many chances AI units take getting about. Lower: they steady themselves longer before a jetpack jump, wait for a little more fuel, and pick routes round hard jumps and long drops. Higher: quicker, riskier take-offs and routes, and more missed jumps. 50% is the designed behaviour.");
		}
		Toggle("AI steadies before jetpacking", g_SettingsMan.AISteadiesBeforeJet(), [](bool on) { g_SettingsMan.SetAISteadiesBeforeJet(on); });
		Tip("AI units come to a stand, still and upright, before a jetpack climb or jump, so the flight starts true. Off: they take off mid-stride, quicker but more often off line.");
		Toggle("AI waits for fuel before jetpacking", g_SettingsMan.AIWaitsForFuel(), [](bool on) { g_SettingsMan.SetAIWaitsForFuel(on); });
		Tip("AI units wait at a take-off until the tank holds what the flight needs. Off: they go with what's in the tank, and may come down short.");
		Toggle("AI waits for doors to open", g_SettingsMan.AIWaitsAtDoors(), [](bool on) { g_SettingsMan.SetAIWaitsAtDoors(on); });
		Tip("AI units wait for a door of their own side on its sensor until it is fully open, and only then go through. Under a hatch overhead they hover where its sensor sees them, braking short of the door, so it opens and stays open while they climb through. Off: as before, they wait wherever the door came near (often out of its sensor's sight), go as soon as it starts opening, and fly or walk into it after 2 to 3 seconds.");
		Toggle("Strict climb limits", g_SettingsMan.StrictClimbLimits(), [](bool on) { g_SettingsMan.SetStrictClimbLimits(on); });
		Tip("AI routes only climb slopes a body can get up: stairs need footholds (a smooth slope steeper than about 50 degrees isn't walked), a scramble up a steep face needs no sheer section taller than a step and a hand free, and a unit whose stairs or scramble stops gaining height gives up after 1.5 s and finds another way instead of clawing at it and jet-hopping. Off: as before, any 51 to 68 degree slope counts as stairs and any soft 68 to 73 degree face as a scramble.");
		Toggle("Size movement by the measured body", g_SettingsMan.MeasuredBodySize(), [](bool on) { g_SettingsMan.SetMeasuredBodySize(on); });
		Tip("Humanoid units are routed and walked for the body they actually have: their height from the top of the head to the feet, and their width, measured from the body itself. A mod unit whose CharHeight doesn't match its sprite then fits the corridors, crouches, leaps and mantles its body can really make. Off: as before, every size comes from CharHeight in proportions tuned on the Soldier Light. Movement only: health bars, aim and the rest are unchanged.");
		Heading("Ledges");
		Toggle("AI mantles only on purpose", g_SettingsMan.AIMantlesOnPurpose(), [](bool on) { g_SettingsMan.SetAIMantlesOnPurpose(on); });
		Tip("AI units pull themselves up onto a ledge or over an obstacle, or catch a ledge in the air, only when they mean to: where their route goes up onto it or over it, at a flight's landing, or to get unstuck. Off: they mantle whenever they press into a ledge they can pull themselves over, as a player does. Players always mantle as before.");
		Toggle("AI catches ledges mid-air", g_SettingsMan.AICatchesLedgesMidAir(), [](bool on) { g_SettingsMan.SetAICatchesLedgesMidAir(on); });
		Tip("An AI unit whose jetpack gives out just short of its landing grabs the landing's lip and pulls itself up. Off: it falls and tries again.");
		Heading("Ladders");
		Toggle("AI grabs ladders only on purpose", g_SettingsMan.AIGrabsLaddersOnPurpose(), [](bool on) { g_SettingsMan.SetAIGrabsLaddersOnPurpose(on); });
		Tip("AI units take hold of a ladder only when their route climbs it, so they don't catch on ladders they are only passing. Bunker ladders follow the same rule. Off: they grab any ladder they press up or down at, as a player does. Players always climb as before.");
		Toggle("AI grabs ladders mid-jetpack", g_SettingsMan.AIGrabsLaddersMidJet(), [](bool on) { g_SettingsMan.SetAIGrabsLaddersMidJet(on); });
		Tip("An AI unit flying, hopping or falling past the ladder its route climbs next takes hold of it, cutting the jetpack. Off: a lit jetpack stops it grabbing, and it has to land first.");
		Toggle("Risky orders fly past ladders", g_SettingsMan.RiskyOrdersFlyPastLadders(), [](bool on) { g_SettingsMan.SetRiskyOrdersFlyPastLadders(on); });
		Tip("On a risky order (a double click dragged), units with the fuel jetpack up a laddered shaft instead of climbing, and their routes count ladders as slower the riskier the order. Off: they climb ladders as a normal order does.");
		{
			float ladderGiveUp = g_SettingsMan.AILadderGiveUpSeconds();
			if (Slider("Give up on a stuck ladder after", &ladderGiveUp, 0.5F, 10.0F, "%.1f s")) {
				g_SettingsMan.SetAILadderGiveUpSeconds(ladderGiveUp);
			}
			Tip("An AI unit climbing a ladder that stops going up (rungs shot away above, the ladder ending short, the way up blocked) lets go after this long and finds another way. 1.5 s is normal.");
			int teamStalls = g_SettingsMan.AILadderTeamStalls();
			if (SliderI("Team avoids a ladder after", &teamStalls, 1, 5, "%d stuck units")) {
				g_SettingsMan.SetAILadderTeamStalls(teamStalls);
			}
			Tip("How many times units of a team must get stuck on the same ladder within a minute before the whole team avoids it for a while. Until then only the unit that got stuck avoids it. 2 is normal.");
		}
		if (Plain()) {
			ImGui::Spacing();
			if (ToolUI::Button("Reset ladders to normal")) {
				g_SettingsMan.ResetLadderSettings();
			}
			ImGui::SetItemTooltip("Puts the ladder settings back as the game comes.");
		}
		Heading("Carry weight");
		{
			float carried = g_SettingsMan.CarriedWeightMultiplier();
			if (Slider("Carried weight", &carried, 0.0F, 5.0F, "%.2fx")) {
				g_SettingsMan.SetCarriedWeightMultiplier(carried);
			}
			Tip("How much what a unit carries weighs: its inventory, the guns in its hands and its gold, as a multiple of their own mass. Heavier units get less from each step, fall harder and need more jetpack. 0x: carried things weigh nothing. 1x is normal.");
			float legs = g_SettingsMan.LegStrength();
			if (Slider("Leg strength", &legs, 0.25F, 3.0F, "%.2fx")) {
				g_SettingsMan.SetLegStrength(legs);
			}
			Tip("How hard every unit's limbs push walking, crawling and climbing, and how high a leap of the legs goes. Weaker legs struggle more with weight, slopes and steps. 1x is normal.");
			float slowdown = g_SettingsMan.OverloadSlowdown() * 100.0F;
			if (Slider("Overload slowdown", &slowdown, 0.0F, 100.0F, "%.0f%%")) {
				g_SettingsMan.SetOverloadSlowdown(slowdown / 100.0F);
			}
			Tip("A straight cut to the walk speed and leap height of a loaded unit, on top of the physics: this much once what it carries (at the Carried weight above) weighs as much as its own body, less for a lighter load. A tenth of the speed is always kept. 0% is normal.");
			int compensation = g_SettingsMan.JetpackWeightCompensation();
			if (Combo("Jetpacks make up for weight", &compensation, "As each jetpack says\0Always\0Never\0")) {
				g_SettingsMan.SetJetpackWeightCompensation(compensation);
			}
			Tip("Whether a jetpack raises its thrust for the unit's load. Always: loaded units fly as well as light ones but burn fuel faster. Never: loaded units fly lower and slower on the usual fuel. As each jetpack says is normal (most do make up for it).");
			float fuel = g_SettingsMan.JetpackFuelMultiplier();
			if (Slider("Jetpack fuel", &fuel, 0.25F, 5.0F, "%.2fx")) {
				g_SettingsMan.SetJetpackFuelMultiplier(fuel);
			}
			Tip("How much fuel every jetpack holds, as a multiple of its own. More lets units fly longer; a fuller tank also takes longer to refill. 1x is normal.");
			Toggle("Infinite jetpack fuel", g_SettingsMan.InfiniteJetpackFuel(), [](bool on) { g_SettingsMan.SetInfiniteJetpackFuel(on); });
			Tip("Every jetpack's tank stays full however long it fires, for players and the AI alike. The AI still plans its flights for one tankful, so it won't take routes longer than that. Off is normal.");
		}
		if (Plain()) {
			ImGui::Spacing();
			if (ToolUI::Button("Reset carry weight to normal")) {
				g_SettingsMan.ResetCarryWeightSettings();
			}
			ImGui::SetItemTooltip("Puts the carry weight settings back as the game comes.");
		}
	};

	// How units behave on a cautious or a direct order: an RTS order placed with one click is cautious, two normal, and a double click held
	// and dragged goes from normal to fully direct (the shortest way whatever it takes) the further it is dragged.
	auto battleBehavior = [&]() {
		SettingsMan::DirectOrderTuning& direct = g_SettingsMan.DirectOrders();
		if (Plain()) {
			ImGui::TextWrapped("How you place an RTS order (move, attack-move, defend, dig to) says how urgent it is. One click: cautious, walking round jumps and waiting for fuel. A double click: a normal order. A double click held and dragged: riskier the further you drag, up to fully direct (the shortest way there whatever it takes) at the full risk distance; the way you drag is the way they face when they get there. The Direct settings say how hard a full drag pushes it, the Cautious ones how careful one click is.");
		}
		{
			Slider("Full risk drag distance", &direct.RiskDragPixels, 200.0F, 1200.0F, "%.0f px");
			Tip("How far on screen a double click is dragged for a fully direct order, a suicide mission. Risk rises slowly and then steeply: half the distance is about 35%, four fifths about 70%. 600 px is normal.");
			float straight = direct.StraightFlightRisk * 100.0F;
			if (Slider("Straight flight from", &straight, 50.0F, 100.0F, "%.0f%% risk")) {
				direct.StraightFlightRisk = straight / 100.0F;
			}
			Tip("From this much risk, a unit with a jetpack lights it and flies at the place it was sent, straight or up and over what is in the way, a long way off on a few such flights, instead of taking the route. 90% is normal.");
			float sky = direct.SkyRouteRisk * 100.0F;
			if (Slider("Sky route from", &sky, 50.0F, 100.0F, "%.0f%% risk")) {
				direct.SkyRouteRisk = sky / 100.0F;
			}
			Tip("From this much risk, on any order, a unit with a jetpack out in the open (open sky over it) may fly up into the sky and down onto the place it was sent, as high as it needs to clear what is between, when that is quicker than the ground route. It waits for the fuel to do it. 80% is normal.");
			Check("Units dive while flying", &direct.Dives);
			Tip("Whether units on a risky order dive on flights across open ground: go prone in the air so the jetpack pushes them along, for speed rather than a controlled flight. The two settings below say from what risk and how often.");
			ImGui::BeginDisabled(!direct.Dives);
			float dive = direct.DiveRisk * 100.0F;
			if (Slider("Dive from", &dive, 50.0F, 100.0F, "%.0f%% risk")) {
				direct.DiveRisk = dive / 100.0F;
			}
			Tip("From this much risk, a unit flying across open ground may dive: go prone in the air so the jetpack pushes it along, for speed rather than a controlled flight. It rights itself before the landing. 80% is normal.");
			float diveChance = direct.DiveChance * 100.0F;
			if (Slider("Dive chance", &diveChance, 0.0F, 100.0F, "%.0f%%")) {
				direct.DiveChance = diveChance / 100.0F;
			}
			Tip("The chance a flight is dived at the \"Dive from\" risk. It rises to every flight at full risk. 25% is normal.");
			ImGui::EndDisabled();
		}
		Heading("Direct (full drag): jetpack");
		{
			float preference = 1.0F / std::max(direct.FlightCostScale, 0.01F);
			if (Slider("Jetpack preference", &preference, 1.0F, 5.0F, "%.1fx")) {
				direct.FlightCostScale = 1.0F / preference;
			}
			Tip("How much more readily they jump than walk round: a jet flight costs them this many times less than it does a normal unit when the route is picked. 1x routes as usual; 2x is normal for a direct order.");
			float fuel = direct.FuelShare * 100.0F;
			if (Slider("Fuel tolerance", &fuel, 10.0F, 120.0F, "%.0f%% of the flight")) {
				direct.FuelShare = fuel / 100.0F;
			}
			Tip("How much of the fuel a flight needs they wait for at the take-off before going. Under 100% they go short and may come down early; 60% is normal for a direct order. (With \"AI waits for fuel\" off in AI behaviour, they never wait.)");
			float risk = (1.0F - direct.Caution) * 100.0F;
			if (Slider("Risk taking", &risk, 0.0F, 100.0F, "%.0f%%")) {
				direct.Caution = 1.0F - risk / 100.0F;
			}
			Tip("How little hard jumps and long drops put them off a route: 0% weighs them as a normal unit does, 100% not at all. 90% is normal for a direct order.");
			float steadyCut = direct.RecklessSteadyCut * 100.0F;
			if (Slider("Recklessness cuts steadying", &steadyCut, 0.0F, 100.0F, "%.0f%%")) {
				direct.RecklessSteadyCut = steadyCut / 100.0F;
			}
			Tip("How much shorter the riskier the order makes their coming to a stand (still and upright) before a jet climb or jump, at full risk: 100% none at all, 50% half as long. 0% (normal): they steady as any unit does, then go straight for it.");
		}
		Heading("Direct: flying");
		{
			Slider("Flight speed", &direct.FlightSpeed, 1.0F, 3.0F, "%.2fx");
			Tip("How fast they fly, up, across and down, as a multiple of a normal unit's limits. Faster gets there sooner but overshoots and tumbles more. 1.5x is normal for a direct order.");
			Slider("Landing speed", &direct.Touchdown, 3.0F, 15.0F, "%.1f m/s");
			Tip("How fast they let themselves come down onto the floor before braking: harder landings save time and fuel but bounce and stumble more. A normal unit lands at 5 m/s; 7 is normal for a direct order.");
			Check("Safe landings", &direct.SafeLandings);
			Tip("Risky orders never land harder than the unit takes without being hurt, whatever the landing speed above says. Off: they land as hard as set, and a heavy unit may hurt itself on arrival. On is normal.");
		}
		Heading("Direct: route");
		{
			float dig = direct.DigCostScale * 100.0F;
			if (Slider("Dig cost on a dig-to", &dig, 1.0F, 100.0F, "%.0f%%", ImGuiSliderFlags_Logarithmic)) {
				direct.DigCostScale = dig / 100.0F;
			}
			Tip("What tunnelling a node costs on a direct dig-to, as a share of a normal unit's price. Lower digs through rather than walking round; 2% (normal) tunnels straight through anything its digger cuts. A normal dig-to is 25%.");
			Slider("Dig straightness", &direct.LineWeight, 0.0F, 1.0F, "%.2f");
			Tip("How hard a direct dig-to keeps to the straight line from where it starts to the point, rather than any route as short: the cost of each node off the line, in nodes walked. 0 doesn't care; 0.1 is normal.");
			Slider("Route straightness", &direct.MoveLineWeight, 0.0F, 2.0F, "%.2f");
			Tip("How hard a risky move or attack keeps to the straight line to the target, at full risk (less the lower the risk): what each step off the line costs, in steps walked. 0 routes as usual; 0.6 is normal.");
			Check("Ignore enemies on the route", &direct.IgnoreEnemies);
			Tip("Whether their routes take no notice of enemies on the way (normal), even for a unit a game mode wants kept safe.");
			Check("Groups keep together", &direct.KeepPace);
			Tip("Whether a group sent with a direct order still walks at its slowest member's pace when \"keep together\" is on. Off (normal): each goes as fast as it can.");
		}
		Heading("Cautious (one click)");
		{
			Slider("Jetpack reluctance", &direct.CautiousFlightCostScale, 1.0F, 5.0F, "%.1fx");
			Tip("How much less readily they jump than walk round: a jet flight costs them this many times more than it does a normal unit when the route is picked. 1x routes as usual; 3x is normal for a cautious order.");
			Slider("Caution", &direct.CautiousCaution, 1.0F, 4.0F, "%.1fx");
			Tip("How much hard jumps and long drops put them off a route, and how long they steady themselves before a take-off, as a multiple of a normal unit's. 2x is normal for a cautious order.");
			float fuel = direct.CautiousFuelShare * 100.0F;
			if (Slider("Fuel kept in hand", &fuel, 100.0F, 150.0F, "%.0f%% of the flight")) {
				direct.CautiousFuelShare = fuel / 100.0F;
			}
			Tip("How much of the fuel a flight needs they wait for at the take-off, never more than the tank holds. 120% is normal for a cautious order.");
			Slider("Cautious flight speed", &direct.CautiousFlightSpeed, 0.4F, 1.0F, "%.2fx");
			Tip("How fast they fly, as a multiple of a normal unit's limits. 0.75x is normal for a cautious order.");
			Slider("Cautious landing speed", &direct.CautiousTouchdown, 2.0F, 5.0F, "%.1f m/s");
			Tip("How fast they let themselves come down onto the floor. A normal unit lands at 5 m/s; 3.5 is normal for a cautious order.");
			Slider("Keep clear of enemies", &direct.CautiousEnemyAvoidance, 0.0F, 2.0F, "%.2fx");
			Tip("How much their routes keep clear of enemies seen on the way, as the Safe routes in game modes setting has it for a flag carrier (and scaled by that setting). 0 takes no notice; 1x is normal.");
		}
		if (Plain()) {
			ImGui::Spacing();
			if (ToolUI::Button("Reset to normal")) {
				direct = SettingsMan::DirectOrderTuning();
			}
			ImGui::SetItemTooltip("Puts every Battle Behavior setting back to the tuning cautious and direct orders were made with.");
		}
	};

	// The route search's and route follower's tuning: how units pick a way, and when they ask for a new one.
	auto pathfinding = [&]() {
		SettingsMan::PathTuning& tuning = g_SettingsMan.PathTunings();
		const SettingsMan::PathTuning normal;
		// A small Reset beside the control just made, putting that one setting back as the game comes (greyed out while it is).
		auto resetItem = [](const char* label, float& value, float normalValue) {
			if (!s_LastShown) {
				return;
			}
			ImGui::SameLine();
			ImGui::PushID(label);
			ImGui::BeginDisabled(value == normalValue);
			if (ImGui::SmallButton("Reset")) {
				value = normalValue;
			}
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Puts this one setting back to the value the game comes with.");
			ImGui::PopID();
		};
		if (Plain()) {
			ImGui::TextWrapped("How AI units pick a way to where they are sent, and when they ask for a new one. Costs are counted in nodes walked (a node is 24 px; walking one costs 1). Changes apply to the next route each unit asks for. Each tip says the value the game comes with; Reset to defaults puts them all back.");
		}
		Heading("Route choice");
		{
			Slider("Route straightness", &tuning.LineWeight, 0.0F, 2.0F, "%.2f");
			Tip("How hard every unit's route keeps to the straight line from where it is to where it is sent: what each node off the line costs, in nodes walked. 0 (default) takes the cheapest way however far it strays; around 0.5 cuts corners and takes jumps it would walk round. Added to a dragged order's own Route straightness (Battle Behavior).");
			resetItem("Route straightness", tuning.LineWeight, normal.LineWeight);
			Slider("Uphill cost", &tuning.UpCost, 0.0F, 5.0F, "%.2f");
			Tip("The extra cost of every node gone up, on foot or on the jet. Lower goes over hills and up through buildings; higher walks round them. 1.5 is the default.");
			resetItem("Uphill cost", tuning.UpCost, normal.UpCost);
			float preference = 1.0F / std::max(tuning.FlightCostScale, 0.01F);
			if (Slider("Jetpack preference", &preference, 1.0F, 5.0F, "%.1fx")) {
				tuning.FlightCostScale = 1.0F / preference;
			}
			Tip("How readily every unit takes a jet flight between floors over walking round: a flight costs this many times less when the route is picked. Cautious and direct orders' own jetpack settings multiply it. 1x is the default.");
			resetItem("Jetpack preference", tuning.FlightCostScale, normal.FlightCostScale);
			float jetOverClimb = tuning.JetOverClimb * 100.0F;
			if (Slider("Jetpack over climbing", &jetOverClimb, 0.0F, 100.0F, "%.0f%%")) {
				tuning.JetOverClimb = jetOverClimb / 100.0F;
			}
			Tip("How much a unit with a jetpack and fuel jets up a slope, stairs or a scramble rather than walking or climbing it. 50% makes the climb on foot cost twice as much, 90% ten times; at 100% it always jets when it can. The full effect from half a tank; less below, none on an empty one. 0% (default) prices the climb as usual.");
			resetItem("Jetpack over climbing", tuning.JetOverClimb, normal.JetOverClimb);
			Slider("Tall jump cost", &tuning.JumpHeightCost, 0.0F, 2.0F, "%.2f");
			Tip("How much dearer each node of a jet jump up a wall is than the one below it. Lower takes tall jumps; higher keeps to short hops and walks round cliffs. 0.5 is the default.");
			resetItem("Tall jump cost", tuning.JumpHeightCost, normal.JumpHeightCost);
			Slider("Ladder cost", &tuning.LadderCostScale, 0.8F, 5.0F, "%.2fx");
			Tip("What climbing a ladder costs (2 a node up, 1.2 down), as a multiple. Higher keeps units off ladders where there's another way. 1x is the default.");
			resetItem("Ladder cost", tuning.LadderCostScale, normal.LadderCostScale);
			Slider("Drop cost", &tuning.FallCostPerNode, 0.0F, 10.0F, "%.1f");
			Tip("The cost of each node of a drop past one storey (96 px). 0 jumps off anything; higher takes the stairs down. 2.5 is the default.");
			resetItem("Drop cost", tuning.FallCostPerNode, normal.FallCostPerNode);
			Slider("Route variety", &tuning.RouteVariety, 0.0F, 4.0F, "%.2fx");
			Tip("How far units' own taste in routes spreads them over different ways to the same place, where the ways are near enough alike. 0 sends everyone down the same shortest way. 1x is the default.");
			resetItem("Route variety", tuning.RouteVariety, normal.RouteVariety);
			Slider("Failed spot avoidance", &tuning.FailedSpotCost, 0.0F, 100.0F, "%.0f");
			Tip("What a place or jump a unit or its team got stuck at lately costs its next routes, in nodes walked. 0 keeps trying the same way; 25 (default) is worth a detour of about twenty nodes.");
			resetItem("Failed spot avoidance", tuning.FailedSpotCost, normal.FailedSpotCost);
		}
		Heading("Route following");
		{
			Slider("Route refresh", &tuning.RefreshSeconds, 1.0F, 30.0F, "every %.1f s");
			Tip("How often a unit on the move asks for its route afresh, to catch changes on the way. 7.5 s is the default.");
			resetItem("Route refresh", tuning.RefreshSeconds, normal.RefreshSeconds);
			Slider("Next point out of sight", &tuning.NoSightSeconds, 0.25F, 5.0F, "%.2f s");
			Tip("How long the route's next point may be out of sight from the ground before a new route is asked for. 1 s is the default.");
			resetItem("Next point out of sight", tuning.NoSightSeconds, normal.NoSightSeconds);
			Slider("Stuck: try remedies after", &tuning.StuckRemedySeconds, 0.5F, 10.0F, "%.1f s");
			Tip("How long without getting nearer the next point before a unit starts trying to get unstuck (jumping, backing off, crouching). 2.5 s is the default.");
			resetItem("Stuck: try remedies after", tuning.StuckRemedySeconds, normal.StuckRemedySeconds);
			Slider("Stuck: new route after", &tuning.StuckRepathSeconds, 1.0F, 20.0F, "%.1f s");
			Tip("How long without getting nearer before the unit gives up on the spot, marks it to avoid and asks for a new route. Never sooner than the remedies start. 6 s is the default.");
			resetItem("Stuck: new route after", tuning.StuckRepathSeconds, normal.StuckRepathSeconds);
			Slider("Mid-air route check", &tuning.AirCheckSeconds, 0.1F, 3.0F, "every %.2f s");
			Tip("How often a unit in the air checks for a better route; it switches only when the new one is better. 0.5 s is the default.");
			resetItem("Mid-air route check", tuning.AirCheckSeconds, normal.AirCheckSeconds);
			Slider("Waypoint reach distance", &tuning.ReachScale, 0.5F, 3.0F, "%.2fx");
			Tip("How near a point on the route counts as reached, as a multiple of the unit's own reach (twice it in the air). Higher cuts corners and runs smoother but can clip ledges; lower follows the route tightly. 1x is the default.");
			resetItem("Waypoint reach distance", tuning.ReachScale, normal.ReachScale);
			Slider("No route: ask again after", &tuning.NoRouteRetrySeconds, 0.5F, 15.0F, "%.1f s");
			Tip("How long a unit told there's no way there waits before asking again. 3 s is the default.");
			resetItem("No route: ask again after", tuning.NoRouteRetrySeconds, normal.NoRouteRetrySeconds);
		}
		if (Plain()) {
			ImGui::Spacing();
			if (ToolUI::Button("Reset to defaults")) {
				tuning = SettingsMan::PathTuning();
			}
			ImGui::SetItemTooltip("Puts every Pathfinding setting back to the values the game comes with.");
		}
	};

	auto debug = [&]() {
		Combo("View", &settings.DebugView, "Final image\0Lighting on grey\0Sky light only\0Dynamic light only\0Normals\0Distortion\0GI only (radiance cascades)\0Solid objects and distance to them\0Where the sun is visible\0");
		Check("Freeze simulation", &m_FreezeSim);
		Tip("The world stands still, in any game, until this is unticked. The Step buttons let it move on one update (a sixtieth of a second) or a second's worth at a time.");
		if (m_FreezeSim && Plain()) {
			if (ToolUI::Button("Step 1 update")) {
				m_FreezeStepsWanted += 1;
			}
			ImGui::SameLine();
			if (ToolUI::Button("Step 60")) {
				m_FreezeStepsWanted += 60;
			}
		}
		Check("Performance statistics", &m_ShowPerformanceMan);
		Check("Actor debug drawing", &m_ShowActorDebugGui);
		Toggle("Terrain update boxes", g_SettingsMan.ShowTerrainUpdates(), [](bool on) { g_SettingsMan.SetShowTerrainUpdates(on); });
		Tip("Where the terrain changed and the path grid has yet to catch up: the waiting areas in orange and the grid nodes re-sampled for them in red, each fading over a second. Not saved.");
		Check("Draw camera bounds", &m_DrawCameraBounds);
		Tip("Each player's view as an outline in its own colour (the inner one is yours), a cross where its camera is heading with a line from the middle of the view, its offset, target and how much of it the HUD covers, and the scene's edges in red where the scene doesn't wrap. Not saved.");
		Check("Draw sprite frustum tests", &m_DrawSpriteBounds);
		Check("ImGui demo window", &m_ImGuiDemoWindow);
		Heading("Debug text in the console");
		for (int i = 0; i < static_cast<int>(SettingsMan::DebugChannel::Count); ++i) {
			auto channel = static_cast<SettingsMan::DebugChannel>(i);
			std::string label = std::string(SettingsMan::DebugChannelName(channel)) + " lines";
			Toggle(label.c_str(), g_SettingsMan.DebugChannelTicked(channel), [channel](bool on) { g_SettingsMan.SetDebugChannel(channel, on); });
		}
		Tip("Lines written to the console and LogConsole.txt. AI, Pilot, Climb, Combat and Squad: the decisions of the units being inspected (Ctrl+I over a unit, units selected in the sandbox, the one you control). Path: each route found. Grid: what path grid updates cost. Sandbox: the sandbox's orders and tools. Perf: unit update times. The CCCP_*_LOG environment variables still switch them on for a run.");
		Toggle("Trace every unit, not just inspected ones", g_SettingsMan.TraceAllUnits(), [](bool on) { g_SettingsMan.SetTraceAllUnits(on); });
		if (Plain() && g_ActivityMan.IsInActivity() && ToolUI::Button("Copy state of inspected units")) {
			std::string state;
			for (const Actor* actor: g_MovableMan.GetActorList()) {
				if (actor->IsDebugInspected()) {
					state += actor->DescribeDebugState(false) + "\n";
				}
			}
			ImGui::SetClipboardText(state.c_str());
		}
		Tip("Each inspected unit's state (Ctrl+I over a unit, units selected in the sandbox, the one you control) as one line, to the clipboard. The control link's 'inspect' command gives it as JSON.");
		if (!Plain()) {
			return;
		}
		ImGui::SeparatorText("Numbers");
		ImGui::Text("%.0f frames a second", ImGui::GetIO().Framerate);
		if (const Scene* scene = g_SceneMan.GetScene(); scene && g_ActivityMan.IsInActivity()) {
			ImGui::Text("Scene: %s", scene->GetPresetName().c_str());
		}
		if (SceneLighting* lighting = g_PostProcessMan.GetSceneLighting()) {
			ImGui::Text("Light grid: %d x %d cells of %d px", lighting->GetGridWidth(), lighting->GetGridHeight(), lighting->GetGridCellSize());
			ImGui::Text("Dynamic lights last screen: %d", lighting->GetLastLightCount());
			int atlasPages = 0;
			int atlasTextures = 0;
			BitmapTexture::GetAtlasStats(atlasPages, atlasTextures);
			ImGui::Text("Sprite atlas: %d sprites on %d pages", atlasTextures, atlasPages);
		}
		ImGui::SeparatorText("These windows");
		DrawToolWindowControls();
		FreeCamGUI();
	};

	// What the AI is thinking, drawn over the game: each overlay keys off the units being inspected (Ctrl+I over a unit, units selected in the sandbox, the one you control).
	auto aiDebug = [&]() {
		{
			int paths = Actor::ShowAIPaths();
			if (Combo("Paths of units moving under AI", &paths, "Never\0Always\0Selected units only\0")) {
				Actor::SetShowAIPaths(paths);
			}
			Tip("The dotted yellow line from a unit to where it's been told to go, with each node marked. Never: only the unit you're controlling shows its path. Selected: the units picked with the sandbox's command tool.");
		}
		{
			int nav = g_SettingsMan.NavDebugOverlay();
			if (Combo("Navigation debug overlay", &nav, "Off\0Path grid\0Path grid and flights\0Path grid, flights and the node under the pointer\0Flights only\0")) {
				g_SettingsMan.SetNavDebugOverlay(nav);
			}
			Tip("The pathfinder's grid in view: a dot where a unit can stand (green), only crawl (yellow) or not fit (red); cyan lines for low obstacles it steps over, magenta for stairs, pale green arcs for leaps. Sizes and leaps are the inspected unit's (Ctrl+I) of the team below, else a soldier's. With flights: each flight's chosen landing (white) and the engine pilot's predicted path (yellow). With the node under the pointer: what the grid makes of that node, and every way out of it drawn with its kind and cost, flights with their fuel. Flights only: just each unit's flights (take-off, landing, the flight as planned in white, the way it went in yellow, its fuel) and the pilot's predicted path, with no grid and no route.");
		}
		{
			int team = g_SettingsMan.DebugTeam();
			if (Combo("Team the debug overlays show", &team, "Team 1\0Team 2\0Team 3\0Team 4\0")) {
				g_SettingsMan.SetDebugTeam(team);
			}
			Tip("Whose view the debug overlays draw: the navigation overlay's path grid, for one, differs by team where doors are.");
		}
		{
			int inspector = g_SettingsMan.UnitInspector();
			if (Combo("Unit inspector", &inspector, "Off\0Inspected units\0Every unit in view\0")) {
				g_SettingsMan.SetUnitInspector(inspector);
			}
			Tip("A label over each unit: its AI mode, the kind of step it's on and the next, the route's cost, the engine mover's state (walk, flight, refuel, fuel wait, settle), how long since it last made progress, its stuck level and impossible-route count, and from its scripts the behaviour, climb stage, target (and whether it's in sight), squad leader and slot, and cover, flank and retreat spots.");
		}
		{
			int combat = g_SettingsMan.CombatOverlay();
			if (Combo("Combat AI overlay", &combat, "Off\0Inspected units\0Every unit in view\0")) {
				g_SettingsMan.SetCombatOverlay(combat);
			}
			Tip("A line from each unit to its target, green while it can see it and grey while it only remembers where it was; the range it holds to as a ring; and the cover (cyan, with why it went there), flank (orange) and retreat (red) spots it's heading for, each with how long it's been at it.");
		}
		Toggle("Recent path solves", g_SettingsMan.ShowRecentSolves(), [](bool on) { g_SettingsMan.SetShowRecentSolves(on); });
		Tip("The last eight routes the pathfinder found for the team the debug overlays show (Game & HUD), newest brightest: each step coloured by its kind with its cost, and at the goal whether it was solved, its total cost and how long the search took. Not saved; costs a little time per search while it's on.");
		Toggle("Squad links and trails", g_SettingsMan.ShowSquadLinks(), [](bool on) { g_SettingsMan.SetShowSquadLinks(on); });
		Tip("For inspected units in a squad: a green line from the leader to each follower, the leader's trail (yellow) that followers measure back along, and each follower's place in line as a white ring with its slot number.");
		Toggle("Order labels", g_SettingsMan.ShowOrderLabels(), [](bool on) { g_SettingsMan.SetShowOrderLabels(on); });
		Tip("Under each unit in view, in its side's colour: the order the sandbox gave it (attack a unit, attack towards a place, attack the nearest enemy, defend a spot) or else its AI mode, whether it's falling back or flanking, its control group [number], and AI paused while the AI is paused. In the sandbox's World tab, each auto battle side's budget, what it has spent, units sent and when its next wave comes.");
	};

	// How the picture is lit, drawn over the game.
	auto renderDebug = [&]() {
		Toggle("Light sources", g_SettingsMan.ShowLightSources(), [](bool on) { g_SettingsMan.SetShowLightSources(on); });
		Tip("Every light on screen as a circle as far as it reaches with a dot of its colour (cone lights as a wedge; glows' lights dashed; lights left out for the cap on lights in red), the scenery lamps with a line to what they hang on (green), none found (red) or not looked up yet (grey), and counts by kind with the summed reach squared, about what the light pass costs.");
		Toggle("Lights by source", g_SettingsMan.ShowLightsBySource(), [](bool on) { g_SettingsMan.SetShowLightsBySource(on); });
		Tip("A readout in the bottom left: the lights registered for the frame about to be drawn, counted by what registered them (objects, hot spots, headlamps, tracers, scenery lamps, fire, sandbox effects, scripts), cone lights apart, and how many sim updates ran since the last drawn frame.");
		Toggle("Sun direction and shadow strength", g_SettingsMan.ShowSunDirection(), [](bool on) { g_SettingsMan.SetShowSunDirection(on); });
		Tip("An arrow from the middle of the screen towards the sun, or the moon at night, with how strong its shadows are right now after the time of day and the weather.");
		{
			int world = g_SettingsMan.WorldSimOverlay();
			if (Combo("World simulation overlay", &world, "None\0Flowing liquid\0Burning ground\0Smoke that hides things\0Falling pieces\0Weather\0Ropes\0")) {
				g_SettingsMan.SetWorldSimOverlay(world);
			}
			Tip("What one of the world's simulations is doing in view. Flowing liquid: the liquid and loose-ground pixels on the move, each in its own material's colour (powders hollow), with a count of each in view. Burning ground: each burning pixel, yellow when fresh to red as it burns out, and a ring round each lit candle. Smoke: the smoke grid's cells, darker where thicker, outlined where thick enough to hide units. Falling pieces: each loose piece of terrain with its size and which way it's going. Weather: the wind as an arrow, and how much rain, snow and dust there is. Ropes: each link green when slack to red when about to snap, orange where it burns, a square where it's tied to the ground and a ring where it's tied to a unit or thing.");
		}
	};

	// Zero gravity: maps with next to none (in space), and the no gravity zones maps draw (ZeroGravity).
	auto zeroG = [&]() {
		if (Plain()) {
			ImGui::TextDisabled(ZeroGravity::SceneWeightless() ? "This map has no gravity." : "This map has gravity.");
		}
		Toggle("Falling ground follows gravity", ZeroGravity::TerrainFollowsGravity(), [](bool on) { ZeroGravity::SetTerrainFollowsGravity(on); });
		Tip("Collapsing ground and sand go by the map's own gravity: with none, ground blasted or dug free stays where it is, pieces thrown loose drift, slow and hang where they stop, and sand doesn't pour; with low or sideways gravity, pieces fall that way and that hard. Off: they fall straight down at normal gravity on every map, as they used to.");
		Toggle("Liquids float", ZeroGravity::LiquidsFloat(), [](bool on) { ZeroGravity::SetLiquidsFloat(on); });
		Tip("Where there's no gravity, water, lava, acid and the rest hang where they are instead of pouring down. Drops thrown off by splashes and explosions drift until they hit something. Off: liquids flow down as on any map.");
		Toggle("No gravity zones count", ZeroGravity::CountZones(), [](bool on) { ZeroGravity::SetCountZones(on); });
		Tip("A map's no gravity zones (the NoGravityArea its lift tubes and scripts set up) count as no gravity for falling ground, sand and liquids, as well as a map with no gravity anywhere. Units and objects are moved in those zones by the lifts themselves. Off: only the map's own gravity counts.");
		Toggle("Jetpacks push any way", ZeroGravity::JetpackAnyDirection(), [](bool on) { ZeroGravity::SetJetpackAnyDirection(on); });
		Tip("With no gravity a jetpack pushes whichever way you want to go, instead of only up: the move keys or stick (crouch for down), or with only the jet held, the way you aim. AI units steer it the same way. Off: jetpacks push up as on any map.");
		Toggle("AI flies in zero gravity", ZeroGravity::AIFlies(), [](bool on) { ZeroGravity::SetAIFlies(on); });
		Tip("With no gravity AI units fly their routes: straight for each point they can see, on the jetpack steered whichever way it has to push, braking to stop where they're going, and holding still there. No walking, landing or bracing for falls, and waypoints in the air stay in the air. Off: they move as they would with gravity (and mostly get nowhere).");
		ImGui::BeginDisabled(!ZeroGravity::AIFlies());
		Toggle("Push off without a jetpack", ZeroGravity::PushOff(), [](bool on) { ZeroGravity::SetPushOff(on); });
		Tip("AI units with no jetpack, or an empty one, push off whatever they're touching towards where they're going, and grab hold when they get there. Off: they stay where they are.");
		{
			float speed = ZeroGravity::FlySpeed();
			if (Slider("Flying speed (m/s)", &speed, 1.0F, 20.0F, "%.1f")) {
				ZeroGravity::SetFlySpeed(speed);
			}
			Tip("How fast AI units fly between points in zero gravity, before braking for the last one.");
			float push = ZeroGravity::PushSpeed();
			if (Slider("Push-off speed (m/s)", &push, 1.0F, 15.0F, "%.1f")) {
				ZeroGravity::SetPushSpeed(push);
			}
			Tip("How hard a push-off sends a unit (gentler when the point is close).");
		}
		ImGui::EndDisabled();
	};

	// The sandbox's own overlays: its orders, tools and effects, drawn over the game whether or not its window is open.
	auto sandboxDebug = [&]() {
		{
			int orders = g_SettingsMan.SandboxOrdersOverlay();
			if (Combo("Sandbox orders", &orders, "Off\0Selected units\0Every unit in view\0")) {
				g_SettingsMan.SetSandboxOrdersOverlay(orders);
			}
			Tip("For the sandbox's selected (or inspected) units, or every unit in view: the order waiting for the next update as a dashed white line to where it goes; the standing order over the head (ATTACK #id, ATTACK@ a place, DEFEND, GUARD #leader, MOVE, HOLD) with a line back to the post or place when off it; why it was last sent, for two seconds; and a red flash each time the standing orders send it again on their own.");
		}
		Toggle("Sim state", g_SettingsMan.ShowSandboxSimState(), [](bool on) { g_SettingsMan.SetShowSandboxSimState(on); });
		Tip("A readout in the bottom right: whether the world is paused and by what (the sandbox's tools, photo mode, Freeze simulation, the game's pause), the AI pause, how many sim updates ran for this frame, the sandbox's tool uses queued and applied last update and the steps still wanted, and the time scale against the speed the simulation actually manages.");
		Toggle("Incoming and effects", g_SettingsMan.ShowSandboxEffects(), [](bool on) { g_SettingsMan.SetShowSandboxEffects(on); });
		Tip("Each rocket, shell, bomb or falling craft on its way in as its line, where it will hit with its crater, and the updates it has left; each effect put down, numbered, with its main light's reach as a ring and storm cells' next flash; each water spring as its pour. Point at an effect or a spring and press Delete to remove just that one.");
		Toggle("Gas", g_SettingsMan.ShowSandboxGas(), [](bool on) { g_SettingsMan.SetShowSandboxGas(on); });
		Tip("The gas in view, a cell every 8 pixels: grey for smoke, green for toxic gas, orange for methane, white for steam, stronger where it is thicker; what is under the pointer is written by it. Nothing shows while the Gas setting is off.");
		Toggle("Air and wind", g_SettingsMan.ShowSandboxAir(), [](bool on) { g_SettingsMan.SetShowSandboxAir(on); });
		Tip("The blast waves in view, a cell every 8 pixels: red where the air is pressed together, blue where it is thinned, with a line for which way it moves; a yellow box round the part of the map the waves are worked out over. With a wind: arrows for where it carries things, and orange dots where it is sheltered by ground upwind. What is under the pointer is written by it. Nothing shows while Air and wind is off.");
		Toggle("Selection and camera", g_SettingsMan.ShowSandboxSelectionCamera(), [](bool on) { g_SettingsMan.SetShowSandboxSelectionCamera(on); });
		Tip("While dragging a selection box: the box as the selection will really use it, with a ring on each unit it will take and in red any part past the scene's seam, which takes nobody. Always: the unit the game says you control (green) against the one the sandbox thinks you're in (blue), the observation target (yellow cross), the free camera's centre (cyan cross), and the view's scale.");
		Toggle("Terrain paint audit", g_SettingsMan.ShowSandboxPaintAudit(), [](bool on) { g_SettingsMan.SetShowSandboxPaintAudit(on); });
		Tip("The last two dozen discs and boxes of terrain the sandbox painted, dug, filled or cleared, fading over ten seconds: dug and cleared in orange, painted and filled in green, grey where nothing changed. The newest are labelled with the material and whether falling ground and liquid were told of the change, a missing one in red. For the areas the path grid has yet to catch up on, turn on Terrain update boxes on the Debug page.");
		Toggle("Battle and colony", g_SettingsMan.ShowSandboxAutoBattle(), [](bool on) { g_SettingsMan.SetShowSandboxAutoBattle(on); });
		Tip("A readout in the top left: for each side in the auto battle, what it has spent of its budget, its next wave and whether it is broke; its units on the ground against those still in its craft; and its cheapest unit against what a wave may spend (in red when it can't buy any). Then each colony building: what it is doing, its training, and its units alive with those dead or dying counted apart.");
		Toggle("Character state", g_SettingsMan.ShowSandboxCharacterState(), [](bool on) { g_SettingsMan.SetShowSandboxCharacterState(on); });
		Tip("One line over your sandbox character's head: whether you're in it, the updates left before you step in, flying and how hard it is pinned, its side and whether it's neutral (ignored by the AI), what it has out and that item's number key, and the AI mode it is left in while you're not in it.");
		Toggle("Standing spots reachability", g_SettingsMan.ShowSandboxSpotReach(), [](bool on) { g_SettingsMan.SetShowSandboxSpotReach(on); });
		Tip("In the move previews (the Command tool's Move and the Move order), every spot the order will look at with the first unit's path cost to it: green where a unit will be sent, red where it has no way there and the spot is passed over, grey where it wasn't needed. Each is a path search, so it is worked out again only as the pointer moves.");
		Toggle("Stroke log", g_SettingsMan.ShowSandboxStrokeLog(), [](bool on) { g_SettingsMan.SetShowSandboxStrokeLog(on); });
		Tip("The last 20 sandbox tool uses as they are applied, in the top right: the sim update, the tool, where, the side, the orders, and the choice and count. With the Sandbox lines ticked under Debug text in the console, each also goes to the console.");
		Toggle("Bottom bar's counts, AI, speed and undo", g_SettingsMan.ShowSandboxBarRight(), [](bool on) { g_SettingsMan.SetShowSandboxBarRight(on); });
		Tip("The right-hand end of the sandbox's bottom bar: each side's unit count, the AI on/paused button, the speed of time, the step (while the window is open), undo and redo. Off, the bar is shorter; undo is still Ctrl+Z, redo Ctrl+Y.");
	};

	auto performance = [&]() {
		SettingsMan::SlowdownFixTuning& fixes = g_SettingsMan.SlowdownFixes();
		Heading("Late-battle slowdown fixes");
		Check("Liquid tells paths only where it moved", &fixes.LiquidTellsOnlyMoves);
		Tip("The path grid is told only where liquid really moved, in separate boxes, and the liquid sweep wakes the end of a pool only for a drop its flow can reach.\nOff, as before: every pool's surface, woken now and then to look for a lower level, told the path grid again each time it settled, all in one box from the leftmost pool to the rightmost, so after a bloody battle most of the map was re-sampled every second, for good.");
		SliderI("Wrecks settle after (s)", &fixes.WreckSettleSeconds, 0, 600, fixes.WreckSettleSeconds == 0 ? "Never" : "%d s");
		Tip("How long a vehicle's or ship's wreck lies still before it settles into the ground, in its own material (metal, wood or military stuff). Once settled it can be dug and blown up like the rest of the terrain.\n0: never, as before: every wreck stays a live object for the whole battle, and the chunks lying on it never settle either.");
		Check("Debris settles across cracks", &fixes.DebrisRestsAcrossGaps);
		Tip("A piece lying still looks for ground under both sides as well as its middle, so one lying across a crack or a crater's lip settles.\nOff, as before: only under its middle, and a piece over a gap stays a live object for good, more of them the more the ground is blown up.");
		Check("Gas area shrinks to the gas", &fixes.GasAreaShrinks);
		Tip("The area the gas is worked out over draws in, every half second, to where gas still is.\nOff, as before: it only grows, until there is no gas left anywhere on the map, which with methane in an enclosed space can take half an hour.");
		Check("Lighting handles each change on its own", &fixes.LightingSplitsChanges);
		Tip("Where the terrain changed, the scorch and stain ground is worked out over each changed patch on its own.\nOff, as before: one box round every patch changed that frame, so two changes at the ends of the map read every material pixel between them.");
		SliderI("Most loose bits", &fixes.MaxLooseBits, 0, 20000, fixes.MaxLooseBits == 0 ? "No limit" : "%d");
		Tip("The most loose bits (blood drops, flecks, chips: pixels and small sprite particles that never expire on their own) at once. Past it, the oldest that have slowed down settle into the ground where they lie, in their own colour and material, so blood stays as a stain; one with no ground under it (lying on a body, or stuck) is removed.\n0: no limit, as before. Bits that couldn't settle where they came to rest then only ever added up over a battle. Solid gib chunks have their own limit, under Gibs.");
		for (int kind = 0; kind < SettingsMan::LooseBitKindCount; ++kind) {
			std::string label = std::string("Most loose bits: ") + SettingsMan::c_LooseBitKindNames[kind];
			SliderI(label.c_str(), &fixes.MaxLooseBitsOf[kind], 0, 20000, fixes.MaxLooseBitsOf[kind] == 0 ? "No own limit" : "%d");
			Tip("The most loose bits of this kind of material at once, within the overall most above: past it the oldest that have slowed settle or are removed in the same way. 0: no limit of its own.\nBlood & flesh: blood, flesh and bone. Liquids: water, oil, acid and the rest. Earth & stone: earth, sand, snow, stone, gravel, concrete, rubble. Metal: metal, military stuff, plating, casings. Wood & plants: wood, grass, leaves, charcoal.");
		}
		if (!Plain()) {
			return;
		}
		ImGui::SeparatorText("Slowdown readout");
		if (!g_ActivityMan.IsInActivity() || !g_SceneMan.GetScene()) {
			ImGui::TextDisabled("Shown in a game.");
			return;
		}
		int chunks = 0;
		int wrecks = 0;
		for (const MovableObject* particle: g_MovableMan.GetParticleList()) {
			if (particle && particle->GetClassName() == "MOSRotating") {
				++chunks;
				if (particle->IsInGroup("Wrecks")) {
					++wrecks;
				}
			}
		}
		ImGui::Text("%.0f frames a second", ImGui::GetIO().Framerate);
		ImGui::Text("Loose objects: %ld (%d solid pieces, %d of them wrecks)", g_MovableMan.GetParticleCount(), chunks, wrecks);
		ImGui::Text("Units: %d", static_cast<int>(g_MovableMan.GetActorList().size()));
		ImGui::Text("Moving liquid pixels: %d (%.1f ms last update)", FluidSim::GetActiveCount(), FluidSim::GetLastUpdateMS());
		ImGui::Text("Path grid nodes waiting to re-sample: %d", static_cast<int>(g_SceneMan.GetScene()->GetPathFinder(Activity::Teams::NoTeam).GetWaitingNodeCount()));
		ImGui::Text("Gas area: %d cells", GasGrid::GetActiveCells());
		Tip("What a long battle leaves behind that can cost frames. With nothing happening, the path grid's waiting count should sit at 0 and the solid pieces should go down as they settle.");

		// What the loose objects are: by preset, with how many of each are lying still and never expire on their own (the ones that pile up).
		struct Kind {
			std::string Name;
			int Count = 0;
			int Still = 0;
			int Forever = 0;
		};
		std::unordered_map<std::string, Kind> kinds;
		for (const MovableObject* particle: g_MovableMan.GetParticleList()) {
			if (!particle) {
				continue;
			}
			std::string key = particle->GetPresetName() + " (" + particle->GetClassName() + ")";
			Kind& kind = kinds[key];
			kind.Name = key;
			++kind.Count;
			kind.Still += particle->GetVel().MagnitudeIsLessThan(1.0F) ? 1 : 0;
			kind.Forever += particle->GetLifetime() == 0 ? 1 : 0;
		}
		std::vector<Kind> sorted;
		sorted.reserve(kinds.size());
		for (auto& [key, kind]: kinds) {
			sorted.push_back(std::move(kind));
		}
		std::sort(sorted.begin(), sorted.end(), [](const Kind& left, const Kind& right) { return left.Count > right.Count; });
		{
			std::array<int, SettingsMan::LooseBitKindCount> bitsOf{};
			for (const MovableObject* particle: g_MovableMan.GetParticleList()) {
				if (particle && particle->GetLifetime() == 0 && particle->GetMaterial() && (dynamic_cast<const MOPixel*>(particle) || dynamic_cast<const MOSParticle*>(particle))) {
					++bitsOf[SettingsMan::GetLooseBitKind(particle->GetMaterial()->GetIndex())];
				}
			}
			ImGui::SeparatorText("Loose bits that never expire, by material");
			for (int kind = 0; kind < SettingsMan::LooseBitKindCount; ++kind) {
				ImGui::Text("%6d  %s", bitsOf[kind], SettingsMan::c_LooseBitKindNames[kind]);
			}
		}
		ImGui::SeparatorText("Most common loose objects");
		ImGui::TextDisabled("count / lying still / never expire");
		for (size_t i = 0; i < std::min<size_t>(sorted.size(), 12); ++i) {
			ImGui::Text("%5d / %5d / %5d  %s", sorted[i].Count, sorted[i].Still, sorted[i].Forever, sorted[i].Name.c_str());
		}

		ImGui::SeparatorText("Where each frame's time goes");
		bool live = PerformanceMan::LiveTimingsOn();
		if (ToolUI::Checkbox("Time each system", &live)) {
			PerformanceMan::SetLiveTimings(live);
		}
		Tip("Times the simulation and drawing systems every frame and shows the last second's milliseconds a frame for each, most first. Timing costs a little itself, so it is off unless ticked here, and isn't saved. (Some entries are inside others: 'total' lines include the parts listed under them.)");
		if (live) {
			std::vector<std::pair<std::string, float>> timings = PerformanceMan::GetLiveTimings();
			if (timings.empty()) {
				ImGui::TextDisabled("Gathering...");
			}
			for (size_t i = 0; i < std::min<size_t>(timings.size(), 24); ++i) {
				ImGui::Text("%7.2f ms  %s", timings[i].second, timings[i].first.c_str());
			}
		}
	};

	const std::pair<const char*, std::function<void()>> categories[] = {
	    {"Time & weather", timeAndWeather},
	    {"Sky & daylight", skyAndDaylight},
	    {"Interiors & shadows", interiorsAndShadows},
	    {"Lamps & lights", lampsAndLights},
	    {"Surfaces", surfaces},
	    {"Water", water},
	    {"Fire, smoke & blast", fireAndSmoke},
	    {"Effect layers", effectLayers},
	    {"Air & wind", airAndWind},
	    {"Falling ground", fallingGround},
	    {"Zero G Settings", zeroG},
	    {"Gibs", gibs},
	    {"Magic & spells", magic},
	    {"Camera & image", cameraAndImage},
	    {"Game & HUD", gameAndHUD},
	    {"AI behaviour", aiBehaviour},
	    {"Movement", movement},
	    {"Battle Behavior", battleBehavior},
	    {"Pathfinding", pathfinding},
	    {"Performance", performance},
	    {"Debug", debug},
	    {"AI debug", aiDebug},
	    {"Render debug", renderDebug},
	    {"Sandbox debug", sandboxDebug},
	};
	const int categoryCount = static_cast<int>(std::size(categories));
	// Asked for the Debug page (c_SettingsCategoryDebug, by the old debug window's key): found by its name, so adding a page doesn't move it.
	if (m_SettingsCategory == c_SettingsCategoryDebug) {
		for (int i = 0; i < categoryCount; ++i) {
			if (std::string_view(categories[i].first) == "Debug") {
				m_SettingsCategory = i;
			}
		}
	}
	m_SettingsCategory = std::clamp(m_SettingsCategory, 0, categoryCount - 1);

	DrawPresets();
	ImGui::Separator();
	ImGui::SetNextItemWidth(-1.0F);
	ImGui::InputTextWithHint("##SettingsSearch", "Search every setting...", s_Search, sizeof(s_Search));
	s_Searching = s_Search[0] != 0;
	s_ShownCount = 0;
	s_PendingHeading = nullptr;
	s_CategoryMatches = false;

	if (s_Searching) {
		ImGui::BeginChild("##Found");
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45F);
		for (int i = 0; i < categoryCount; ++i) {
			ImGui::PushID(i);
			s_CategoryMatches = ContainsIgnoringCase(categories[i].first, s_Search);
			s_PendingHeading = categories[i].first;
			categories[i].second();
			ImGui::PopID();
		}
		s_PendingHeading = nullptr;
		s_CategoryMatches = false;
		if (s_ShownCount == 0) {
			ImGui::TextDisabled("No setting has that in its name.");
		}
		ImGui::PopItemWidth();
		ImGui::EndChild();
	} else {
		// The categories down the left, the chosen one's controls beside them.
		float listWidth = 0.0F;
		for (const auto& category: categories) {
			listWidth = std::max(listWidth, ImGui::CalcTextSize(category.first).x);
		}
		listWidth += ImGui::GetStyle().FramePadding.x * 4.0F;
		ImGui::BeginChild("##Categories", ImVec2(listWidth, 0.0F));
		for (int i = 0; i < categoryCount; ++i) {
			if (ImGui::Selectable(categories[i].first, m_SettingsCategory == i)) {
				m_SettingsCategory = i;
			}
		}
		ImGui::EndChild();
		ImGui::SameLine();
		ImGui::BeginChild("##Controls");
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45F);
		ImGui::PushID(m_SettingsCategory);
		categories[m_SettingsCategory].second();
		ImGui::PopID();
		ImGui::PopItemWidth();
		ImGui::EndChild();
	}
	s_Searching = false;
	s_LastShown = true;
	EndPanel();
}
