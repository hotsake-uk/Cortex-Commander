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
#include "FrameMan.h"
#include "ModernHUD.h"
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
#include "TerrainTrees.h"
#include "TerrainFire.h"
#include "TerrainCandle.h"
#include "WeatherLightning.h"
#include "TextOverlay.h"
#include "TimerMan.h"
#include "UnitSpeech.h"
#include "WindowMan.h"

#include "imgui/imgui.h"
#include "ToolWidgets.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <functional>
#include <string>
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

	void Heading(const char* text) {
		if (Plain()) {
			ImGui::SeparatorText(text);
		}
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
					s_PresetMessage = g_SettingsMan.LoadPreset(name) ? "Loaded \"" + name + "\"." : "Could not load \"" + name + "\".";
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

	auto fallingGround = [&]() {
		TerrainCollapse::Tuning& tuning = TerrainCollapse::GetTuning();
		Toggle("Collapsing terrain", TerrainCollapse::IsEnabled(), [](bool on) { TerrainCollapse::SetEnabled(on); });
		if (Plain()) {
			ImGui::TextDisabled("%d pieces moving, %d pixels fell", TerrainCollapse::GetFallingCount(), TerrainCollapse::GetCollapsedCount());
		}
		Toggle("Pieces of buildings fall too", TerrainCollapse::BuildingsFall(), [](bool on) { TerrainCollapse::SetBuildingsFall(on); });
		Toggle("Units and vehicles bump into trees", TerrainTrees::UnitsCollide(), [](bool on) { TerrainTrees::SetUnitsCollide(on); });
		Tip("Off (as the game comes): units walk and vehicles drive through trees, and a tree coming down falls through them too. Trees still burn, stand until their trunks burn through, fall and land on the ground, and bullets, fire and liquids still meet them. On: trees are solid to units and vehicles like any ground, and a falling tree hits them.");
		Toggle("Trees only meet the ground under them", TerrainCollapse::PassesTrees(), [](bool on) { TerrainCollapse::SetPassesTrees(on); });
		Tip("On: rock and other ground falling from above goes through a standing tree (behind it, leaving the tree whole) rather than landing on it, and a falling tree goes through other trees. A tree that is cut or burnt through still falls over and lands on the ground. Off (as the game comes): trees are solid to falling pieces like any ground.");
		Toggle("Units' metal and gear settle as scraps", g_SettingsMan.BodyGearSettlesAsScraps(), [](bool on) { g_SettingsMan.SetBodyGearSettlesAsScraps(on); });
		Tip("Armour plating, robot parts and the rest of what comes off a unit keep their look when they come to rest in the ground, but become the same soft scraps as the flesh, so the remains of the fallen never leave lumps of metal nobody can dig through. Flesh and bone settle as scraps and ashes either way. Off: everything settles as its own material.");
		Heading("What falls");
		Check("Floating masses stay up when chipped", &tuning.FloatingStays);
		Tip("On: a mass that was already hanging in the air before a blast stays; cut in two, the bigger part stays and the smaller falls. Off: anything touching nothing falls.");
		SliderI("Thin neck that snaps (pixels)", &tuning.NeckWidth, 0, 16);
		Tip("A piece left joined to the rest by a neck no wider than this breaks off and falls. 0: only pieces cut right through fall. Wood always holds until it's cut or burnt right through, so a burning tree stands (a material's own NeckWidth in its ini).");
		SliderI("Biggest piece that can fall (pixels)", &tuning.MaxPiecePixels, 500, 200000, "%d", ImGuiSliderFlags_Logarithmic);
		Tip("Anything bigger counts as the world and never falls. 30,000 is about a 170 by 170 block.");
		SliderI("Smallest loose bit of building that falls", &tuning.MinFittingPixels, 0, 2000);
		Tip("Smaller loose bits of building material stay put: lamps, signs and consoles are drawn hanging in mid-air.");
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
		Slider("Metal (Bend, m/s)", &tuning.BendSpeed, 0.5F, 30.0F, "%.1f");
		Tip("Metal never breaks from a landing. Above this, a long thin piece (a beam, a plate) folds at a crease, more the harder the hit and the thinner it is, and a chunky piece dents. Falling pieces move at most 27 m/s, so above that never.");
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
		Toggle("No map wrapping", g_SettingsMan.NoSceneWrap(), [](bool on) { g_SettingsMan.SetNoSceneWrap(on); });
		Tip("Every map has hard left and right edges and one copy of the world, instead of looping round. Takes effect when the next map loads.");
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
		Toggle("Side and health beside units", g_SettingsMan.ShowUnitTags(), [](bool on) { g_SettingsMan.SetShowUnitTags(on); });
		Tip("Each unit's team icon and health number. In the Sandbox game mode every side's are shown; in other games, other sides' only where your side has seen and with \"Show enemy HUD\" (Options, Gameplay) on. Units of no side (training dummies) and those a mod hides never have them.");
		Toggle("Classic pie wheel", g_SettingsMan.ClassicPieWheel(), [](bool on) { g_SettingsMan.SetClassicPieWheel(on); });
		Tip("The old wheels on right click, for a unit you play and for the sandbox's command tool, instead of the action menu: a list above the pointer with every order and the weapons and movement rules on one layer. (A gamepad, and players after the first, always get the unit's wheel.)");
		Toggle("Smooth HUD text", TextOverlay::IsEnabled(), [](bool on) { TextOverlay::SetEnabled(on); });
		int frameCap = g_WindowMan.GetFrameCap();
		if (SliderI("Frame cap (0 = none)", &frameCap, 0, 360)) {
			g_WindowMan.SetFrameCap(frameCap > 0 && frameCap < 30 ? 30 : frameCap);
		}
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
				ImGui::SetItemTooltip("Reads every Speech.ini again, for trying out lines without restarting.");
			}
		}
	};

	// How the AI behaves: what it notices, how fire and losses get to it, and how many chances it takes getting about.
	auto aiBehaviour = [&]() {
		Toggle("Pause AI", Controller::IsAIPaused(), [](bool on) { Controller::SetAIPaused(on); });
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
			if (Combo("Navigation debug overlay", &nav, "Off\0Path grid\0Path grid and flights\0Path grid, flights and the node under the pointer\0")) {
				g_SettingsMan.SetNavDebugOverlay(nav);
			}
			Tip("The pathfinder's grid in view: a dot where a unit can stand (green), only crawl (yellow) or not fit (red); cyan lines for low obstacles it steps over, magenta for stairs, pale green arcs for leaps. Sizes and leaps are the inspected unit's (Ctrl+I) of the team below, else a soldier's. With flights: each flight's chosen landing (white) and the engine pilot's predicted path (yellow). With the node under the pointer: what the grid makes of that node, and every way out of it drawn with its kind and cost, flights with their fuel.");
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
			if (Combo("World simulation overlay", &world, "None\0Flowing liquid\0Burning ground\0Smoke that hides things\0Falling pieces\0Weather\0")) {
				g_SettingsMan.SetWorldSimOverlay(world);
			}
			Tip("What one of the world's simulations is doing in view. Flowing liquid: the liquid and loose-ground pixels on the move, each in its own material's colour (powders hollow), with a count of each in view. Burning ground: each burning pixel, yellow when fresh to red as it burns out, and a ring round each lit candle. Smoke: the smoke grid's cells, darker where thicker, outlined where thick enough to hide units. Falling pieces: each loose piece of terrain with its size and which way it's going. Weather: the wind as an arrow, and how much rain, snow and dust there is.");
		}
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
	    {"Camera & image", cameraAndImage},
	    {"Game & HUD", gameAndHUD},
	    {"AI behaviour", aiBehaviour},
	    {"Debug", debug},
	    {"AI debug", aiDebug},
	    {"Render debug", renderDebug},
	    {"Sandbox debug", sandboxDebug},
	};
	const int categoryCount = static_cast<int>(std::size(categories));
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
