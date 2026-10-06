#include "DebugMan.h"
#include "Actor.h"
#include "ActivityMan.h"
#include "ActorFire.h"
#include "ActorWater.h"
#include "CameraMan.h"
#include "Controller.h"
#include "EffectsParticles.h"
#include "FluidSim.h"
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
#include "TerrainFire.h"
#include "TextOverlay.h"
#include "TimerMan.h"
#include "WindowMan.h"

#include "imgui/imgui.h"
#include "ToolWidgets.h"

#include <algorithm>
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
		ImGui::SetItemTooltip("A preset holds every setting in this panel: the look, the time and weather, water, fire, and how the ground falls.\nThey are files in Userdata/Presets, so they can be copied and shared.");
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
			s_PresetName[0] = 0;
			s_PresetsListed = false;
		}
		ImGui::EndDisabled();
		if (ToolUI::Button("Usual settings")) {
			g_PostProcessMan.GetLightingSettings() = LightingSettings();
			TerrainCollapse::GetTuning() = TerrainCollapse::Tuning();
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
		Combo("Precipitation", &settings.WeatherType, "Clear\0Rain\0Snow\0Ash fall\0Dust storm\0");
		Slider("Weather intensity", &settings.WeatherIntensity, 0.0F, 1.0F);
		Slider("Wind", &settings.Wind, -400.0F, 400.0F, "%.0f px/s");
		Slider("Weather's own light", &settings.WeatherLight, 0.0F, 1.5F);
		Tip("The least light rain, snow, ash and dust are drawn with, so they show on a dark night.");
		Slider("Rain splashes", &settings.RainSplashes, 0.0F, 2.0F);
		Tip("Little splashes where rain lands on ground, water, roofs and units. 0 for none.");
		Toggle("Still water freezes over in snow", FluidSim::FreezingEnabled(), [](bool on) { FluidSim::SetFreezingEnabled(on); });
		Check("Living world (sway, snow, wet ground)", &settings.LivingWorld);
		// Scene makers: keep the time and weather as the scene's own. It's written when the scene is saved from the scene editor.
		if (Scene* scene = g_SceneMan.GetScene(); scene && Plain()) {
			ImGui::SeparatorText("This scene's own time and weather");
			const Scene::Atmosphere& own = scene->GetAtmosphere();
			if (own.TimeOfDay >= 0.0F || own.WeatherType >= 0) {
				static const char* weatherNames[] = {"clear", "rain", "snow", "ash fall", "dust storm"};
				ImGui::Text("Set: %.1f h, %s", own.TimeOfDay, own.WeatherType >= 0 && own.WeatherType <= 4 ? weatherNames[own.WeatherType] : "default weather");
			} else {
				ImGui::TextDisabled("Not set (uses the player's settings)");
			}
			if (ToolUI::Button("Use what is set now")) {
				Scene::Atmosphere atmosphere;
				atmosphere.TimeOfDay = settings.TimeOfDay;
				atmosphere.DayLengthMinutes = settings.DayLengthMinutes;
				atmosphere.WeatherType = settings.WeatherType;
				atmosphere.WeatherIntensity = settings.WeatherIntensity;
				atmosphere.Wind = settings.Wind;
				scene->SetAtmosphere(atmosphere);
			}
			ImGui::SameLine();
			if (ToolUI::Button("Clear")) {
				scene->SetAtmosphere(Scene::Atmosphere());
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
		Slider("Cloud shadows", &settings.CloudShadows, 0.0F, 1.0F);
		Slider("God rays", &settings.GodRays, 0.0F, 2.0F);
		Slider("Haze", &settings.AtmosphereHaze, 0.0F, 1.0F);
		Tint("Haze colour", &settings.AtmosphereColor.x);
		Slider("Far background blur", &settings.BackgroundBlur, 0.0F, 1.5F);
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
		Slider("Emissive intensity", &settings.EmissiveIntensity, 0.0F, 4.0F);
		Slider("Light colour strength", &settings.LightSaturation, 0.0F, 2.5F);
		Tip("How colourful the light of lamps, glows, flashes and fire is. 0 makes all light white.");
		Tint("Tint on all lights", &settings.LightTint.x);
		Heading("Scenery lamps");
		Slider("Lamp brightness", &settings.LampBrightness, 0.0F, 4.0F);
		Slider("Lamp reach", &settings.LampReach, 0.25F, 3.0F);
		Tint("Lamp tint", &settings.LampTint.x);
		Heading("Headlamps");
		Check("Headlamps at night", &settings.Headlamps);
		Check("Headlamps by day as well", &settings.HeadlampsByDay);
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
	};

	auto surfaces = [&]() {
		Slider("Edge lighting", &settings.EdgeLighting, 0.0F, 1.0F);
		Slider("Shine (metal, wet ground)", &settings.Specular, 0.0F, 3.0F);
		Slider("Metal reflections", &settings.Metals, 0.0F, 2.0F);
		Slider("Surface relief", &settings.Relief, 0.0F, 1.5F);
		Check("Wet, sooty, snowy and hot surfaces", &settings.SurfaceStates);
		Check("Scorch marks", &settings.ScorchMarks);
		Check("Blood, oil and water stains", &settings.Stains);
		Slider("Hot metal cooling (seconds)", &settings.HotSpotSeconds, 0.0F, 10.0F, "%.1f");
	};

	auto water = [&]() {
		Toggle("Flowing liquids", FluidSim::IsEnabled(), [](bool on) { FluidSim::SetEnabled(on); });
		if (Plain()) {
			ImGui::SameLine();
			ImGui::TextDisabled("(%d moving, %.2f ms)", FluidSim::GetActiveCount(), FluidSim::GetLastUpdateMS());
		}
		Toggle("Loose sand and snow slide", FluidSim::PowdersEnabled(), [](bool on) { FluidSim::SetPowdersEnabled(on); });
		Toggle("Units swim, float and drown", ActorWater::IsEnabled(), [](bool on) { ActorWater::SetEnabled(on); });
		Slider("Light glowing through water", &settings.WaterLightGlow, 0.0F, 1.5F);
		Tip("How much a lamp, fire or blast in or beside water shows as a glow in the water, in the light's own colour. 0: water is only lit like a surface.");
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
		Toggle("Units catch fire", ActorFire::IsEnabled(), [](bool on) { ActorFire::SetEnabled(on); });
		Toggle("Smoke blocks sight", SmokeGrid::IsEnabled(), [](bool on) { SmokeGrid::SetEnabled(on); });
		Slider("Soft smoke", &settings.SoftSmoke, 0.0F, 3.0F);
		Tip("Every puff of the game's smoke trails soft, billowing smoke as well, so it hangs and rolls. 0: only the game's own smoke sprites.");
		Slider("Smoke scattering", &settings.SmokeScattering, 0.0F, 3.0F);
		Slider("Embers", &settings.Embers, 0.0F, 3.0F);
		Slider("Sparks, dust and debris", &settings.EffectsParticles, 0.0F, 3.0F);
		if (Plain()) {
			ImGui::TextDisabled("%d effects particles alive", EffectsParticles::GetCount());
		}
		Heading("Heat and blast in the air");
		Check("Distortion", &settings.DistortionEnabled);
		Slider("Heat haze (px)", &settings.HeatHaze, 0.0F, 6.0F);
		Slider("Shockwave strength", &settings.ShockwaveStrength, 0.0F, 3.0F);
		float hitStop = g_CameraMan.GetHitStopStrength();
		if (Slider("Hit-stop on big blasts", &hitStop, 0.0F, 2.0F)) {
			g_CameraMan.SetHitStopStrength(hitStop);
		}
	};

	auto fallingGround = [&]() {
		TerrainCollapse::Tuning& tuning = TerrainCollapse::GetTuning();
		Toggle("Collapsing terrain", TerrainCollapse::IsEnabled(), [](bool on) { TerrainCollapse::SetEnabled(on); });
		if (Plain()) {
			ImGui::TextDisabled("%d pieces moving, %d pixels fell", TerrainCollapse::GetFallingCount(), TerrainCollapse::GetCollapsedCount());
		}
		Toggle("Pieces of buildings fall too", TerrainCollapse::BuildingsFall(), [](bool on) { TerrainCollapse::SetBuildingsFall(on); });
		Heading("What falls");
		Check("Floating masses stay up when chipped", &tuning.FloatingStays);
		Tip("On: a mass that was already hanging in the air before a blast stays; cut in two, the bigger part stays and the smaller falls. Off: anything touching nothing falls.");
		SliderI("Thin neck that snaps (pixels)", &tuning.NeckWidth, 0, 16);
		Tip("A piece left joined to the rest by a neck no wider than this breaks off and falls. 0: only pieces cut right through fall.");
		SliderI("Biggest piece that can fall (pixels)", &tuning.MaxPiecePixels, 500, 200000, "%d", ImGuiSliderFlags_Logarithmic);
		Tip("Anything bigger counts as the world and never falls. 30,000 is about a 170 by 170 block.");
		SliderI("Smallest loose bit of building that falls", &tuning.MinFittingPixels, 0, 2000);
		Tip("Smaller loose bits of building material stay put: lamps, signs and consoles are drawn hanging in mid-air.");
		Heading("How it falls");
		Slider("How hard explosions throw loose pieces", &tuning.BlastPush, 0.0F, 3.0F);
		Tip("0: explosions don't move loose pieces at all. Higher: pieces still moving are thrown harder, and more of the pieces lying at rest near a blast are picked up and thrown.");
		SliderI("Loose scraps it flattens (pixels)", &tuning.CrushPixels, 0, 300);
		Tip("A falling piece goes through loose bits of ground up to this size instead of getting stuck on them. Never more than a quarter of its own size. 0: everything holds it up.");
		Slider("How hard a landing cracks a piece", &tuning.BreakStrength, 0.2F, 5.0F, "%.2fx");
		Slider("Seconds still before it's ground again", &tuning.RestSeconds, 0.2F, 15.0F, "%.1f");
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
	};

	auto gameAndHUD = [&]() {
		float timeScale = g_TimerMan.GetTimeScale();
		if (Slider("Game speed", &timeScale, 0.1F, 4.0F, "%.2fx", ImGuiSliderFlags_Logarithmic)) {
			g_TimerMan.SetTimeScale(timeScale);
		}
		if (Plain() && ToolUI::Button("Normal speed")) {
			g_TimerMan.SetTimeScale(1.0F);
		}
		Toggle("Pause AI", Controller::IsAIPaused(), [](bool on) { Controller::SetAIPaused(on); });
		Check("Night limits AI sight", &settings.NightAffectsAI);
		Toggle("Show the paths of units moving under AI", Actor::ShowAIPaths(), [](bool on) { Actor::SetShowAIPaths(on); });
		Tip("The dotted yellow line from a unit to where it's been told to go. Off, only the unit you're controlling shows its path.");
		Heading("HUD");
		Toggle("Modern HUD", ModernHUD::IsEnabled(), [](bool on) { ModernHUD::SetEnabled(on); });
		Toggle("Smooth HUD text", TextOverlay::IsEnabled(), [](bool on) { TextOverlay::SetEnabled(on); });
		int frameCap = g_WindowMan.GetFrameCap();
		if (SliderI("Frame cap (0 = none)", &frameCap, 0, 360)) {
			g_WindowMan.SetFrameCap(frameCap > 0 && frameCap < 30 ? 30 : frameCap);
		}
	};

	auto debug = [&]() {
		Combo("View", &settings.DebugView, "Final image\0Lighting on grey\0Sky light only\0Dynamic light only\0Normals\0Distortion\0GI only (radiance cascades)\0Solid objects and distance to them\0Where the sun is visible\0");
		Check("Performance statistics", &m_ShowPerformanceMan);
		Check("Actor debug drawing", &m_ShowActorDebugGui);
		Check("Draw camera bounds", &m_DrawCameraBounds);
		Check("Draw sprite frustum tests", &m_DrawSpriteBounds);
		Check("ImGui demo window", &m_ImGuiDemoWindow);
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

	const std::pair<const char*, std::function<void()>> categories[] = {
	    {"Time & weather", timeAndWeather},
	    {"Sky & daylight", skyAndDaylight},
	    {"Interiors & shadows", interiorsAndShadows},
	    {"Lamps & lights", lampsAndLights},
	    {"Surfaces", surfaces},
	    {"Water", water},
	    {"Fire, smoke & blast", fireAndSmoke},
	    {"Falling ground", fallingGround},
	    {"Camera & image", cameraAndImage},
	    {"Game & HUD", gameAndHUD},
	    {"Debug", debug},
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
