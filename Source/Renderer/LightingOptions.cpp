#include "LightingOptions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_map>

using namespace RTE;

const std::vector<LightingOption>& LightingOptions::GetAll() {
	// The keys are the ones Cortex Commander's settings presets use, so a preset's lines work in Settings.ini as they are.
	static const std::vector<LightingOption> options = {
	    // Lights
	    {"LightingEnabled", "Lighting", 0, &LightingSettings::Enabled},
	    {"LampBrightness", "Lamp brightness", 0, &LightingSettings::LampBrightness, 0.0F, 4.0F},
	    {"LampReach", "Lamp reach", 0, &LightingSettings::LampReach, 0.25F, 3.0F},
	    {"LightingGlowIntensity", "Glow light intensity", 0, &LightingSettings::GlowLightIntensity, 0.0F, 8.0F},
	    {"LightingGlowRadiusScale", "Glow light radius", 0, &LightingSettings::GlowLightRadiusScale, 0.5F, 10.0F},
	    {"LightingEmissiveIntensity", "Emissive intensity", 0, &LightingSettings::EmissiveIntensity, 0.0F, 4.0F},
	    {"LightSaturation", "Light colour strength", 0, &LightingSettings::LightSaturation, 0.0F, 2.5F},
	    {"LightingIndirect", "Indirect light", 0, &LightingSettings::IndirectLight, 0.0F, 1.5F},
	    {"RadianceCascades", "Radiance cascades", 0, &LightingSettings::RadianceCascades},
	    // Light detail
	    {"GIStrength", "GI strength", 1, &LightingSettings::GIStrength, 0.0F, 4.0F},
	    {"GIBounce", "GI bounce", 1, &LightingSettings::GIBounce, 0.0F, 1.0F},
	    {"LightingSpecular", "Shine", 1, &LightingSettings::Specular, 0.0F, 3.0F},
	    {"LightingMetals", "Metal reflections", 1, &LightingSettings::Metals, 0.0F, 2.0F},
	    {"LightingRelief", "Surface relief", 1, &LightingSettings::Relief, 0.0F, 1.5F},
	    {"LightingEdgeLighting", "Edge lighting", 1, &LightingSettings::EdgeLighting, 0.0F, 1.0F},
	    {"LightingAirFalloff", "Light carried through air", 1, &LightingSettings::AirFalloff, 0.8F, 0.995F},
	    {"LightingSolidFalloff", "Light carried through ground", 1, &LightingSettings::SolidFalloff, 0.1F, 0.95F},
	    {"LightingPropagationSteps", "Light spreading steps", 1, &LightingSettings::PropagationIterationsPerFrame, 1.0F, 32.0F},
	    // Shadows
	    {"LightingShadowStrength", "Shadow strength", 2, &LightingSettings::ShadowStrength, 0.0F, 1.0F},
	    {"LightShadowSoftness", "Shadow softness", 2, &LightingSettings::LightShadowSoftness, 0.0F, 2.0F},
	    {"LightShadowField", "Traced terrain shadows", 2, &LightingSettings::LightShadowField},
	    {"SoftWallLight", "Soft light edges on walls", 2, &LightingSettings::SoftWallLight},
	    {"UnitShadows", "Shadows of units and objects", 2, &LightingSettings::UnitShadows, 0.0F, 1.0F},
	    {"SunShadows", "Sun and moon shadows", 2, &LightingSettings::SunShadows, 0.0F, 1.0F},
	    {"SunShadowMap", "Crisp sun shadows", 2, &LightingSettings::SunShadowMap},
	    {"BackgroundShadows", "Terrain shadow on backdrop", 2, &LightingSettings::BackgroundShadows, 0.0F, 1.0F},
	    {"ContactShading", "Contact shading", 2, &LightingSettings::ContactShading, 0.0F, 1.0F},
	    // Time and sky
	    {"TimeOfDay", "Time of day", 3, &LightingSettings::TimeOfDay, 0.0F, 24.0F},
	    {"DayLengthMinutes", "Day length (minutes)", 3, &LightingSettings::DayLengthMinutes, 0.0F, 60.0F},
	    {"DeepNightDarkness", "Darker in the dead of night", 3, &LightingSettings::DeepNightDarkness, 0.0F, 0.95F},
	    {"SkyFollowsTime", "Sky takes the hour's colours", 3, &LightingSettings::SkyFollowsTime, 0.0F, 1.0F},
	    {"SunDisc", "Sun in the sky", 3, &LightingSettings::SunDisc, 0.0F, 2.0F},
	    {"GodRays", "God rays", 3, &LightingSettings::GodRays, 0.0F, 2.0F},
	    {"AtmosphereHaze", "Haze", 3, &LightingSettings::AtmosphereHaze, 0.0F, 1.0F},
	    {"FogVolume", "Mist and dust", 3, &LightingSettings::FogVolume, 0.0F, 1.5F},
	    {"FogMorningMist", "Dawn mist", 3, &LightingSettings::FogMorningMist, 0.0F, 1.0F},
	    // Clouds and mist
	    {"CloudLayer", "Clouds in the sky", 4, &LightingSettings::CloudLayer},
	    {"CloudCover", "Cloud cover", 4, &LightingSettings::CloudCover, 0.0F, 1.0F},
	    {"CloudOpacity", "Cloud opacity", 4, &LightingSettings::CloudOpacity, 0.0F, 1.0F},
	    {"CloudSize", "Cloud size", 4, &LightingSettings::CloudSize, 0.4F, 2.5F},
	    {"CloudHeight", "Cloud height", 4, &LightingSettings::CloudHeight, 0.0F, 1.0F},
	    {"CloudShadows", "Cloud shadows", 4, &LightingSettings::CloudShadows, 0.0F, 1.0F},
	    {"FogOpacity", "Mist and dust opacity", 4, &LightingSettings::FogOpacity, 0.0F, 1.0F},
	    {"FogClearSeconds", "Mist clears after (s)", 4, &LightingSettings::FogClearSeconds, 3.0F, 120.0F},
	    {"SunShadowSoftness", "Sun shadow softness", 4, &LightingSettings::SunShadowSoftness, 0.0F, 2.0F},
	    // Weather
	    {"WeatherType", "Weather", 5, &LightingSettings::WeatherType},
	    {"OverrideMapWeather", "My weather on every map", 5, &LightingSettings::OverrideMapWeather},
	    {"WeatherIntensity", "Weather intensity", 5, &LightingSettings::WeatherIntensity, 0.0F, 1.0F},
	    {"WeatherSpeed", "Weather speed", 5, &LightingSettings::WeatherSpeed, 0.1F, 3.0F},
	    {"Wind", "Wind (px/s)", 5, &LightingSettings::Wind, -400.0F, 400.0F},
	    {"WeatherLight", "Weather's own light", 5, &LightingSettings::WeatherLight, 0.0F, 1.5F},
	    {"RainSplashes", "Rain splashes", 5, &LightingSettings::RainSplashes, 0.0F, 2.0F},
	    {"Puddles", "Puddles", 5, &LightingSettings::Puddles, 0.0F, 1.0F},
	    {"StormFlashes", "Storm flashes", 5, &LightingSettings::StormFlashes},
	    // Colour
	    {"PostExposure", "Exposure", 6, &LightingSettings::Exposure, 0.1F, 4.0F},
	    {"AutoExposure", "Auto exposure", 6, &LightingSettings::AutoExposure, 0.0F, 1.0F},
	    {"PostSaturation", "Saturation", 6, &LightingSettings::Saturation, 0.0F, 2.0F},
	    {"GradeContrast", "Contrast", 6, &LightingSettings::Contrast, 0.5F, 1.6F},
	    {"GradeTemperature", "Temperature", 6, &LightingSettings::Temperature, -1.0F, 1.0F},
	    {"GradeTint", "Tint", 6, &LightingSettings::Tint, -1.0F, 1.0F},
	    {"PostVignette", "Vignette", 6, &LightingSettings::Vignette, 0.0F, 1.0F},
	    {"FilmGrain", "Film grain", 6, &LightingSettings::FilmGrain, 0.0F, 1.0F},
	    {"ChromaticAberration", "Colour fringing (px)", 6, &LightingSettings::ChromaticAberration, 0.0F, 4.0F},
	    // Bloom and focus
	    {"BloomEnabled", "Bloom", 7, &LightingSettings::BloomEnabled},
	    {"BloomIntensity", "Bloom intensity", 7, &LightingSettings::BloomIntensity, 0.0F, 3.0F},
	    {"BloomThreshold", "Bloom threshold", 7, &LightingSettings::BloomThreshold, 0.0F, 4.0F},
	    {"BloomKnee", "Bloom knee", 7, &LightingSettings::BloomKnee, 0.01F, 1.0F},
	    {"DepthOfField", "Depth of field", 7, &LightingSettings::DepthOfField},
	    {"DepthOfFieldStrength", "Depth of field strength", 7, &LightingSettings::DepthOfFieldStrength, 0.0F, 2.0F},
	    {"BackgroundBlur", "Far background blur", 7, &LightingSettings::BackgroundBlur, 0.0F, 1.5F},
	    {"EventLooks", "Grade answers events", 7, &LightingSettings::EventLooks},
	    {"EventLookStrength", "Event grade strength", 7, &LightingSettings::EventLookStrength, 0.0F, 2.0F},
	    // Effects
	    {"DistortionEnabled", "Distortion", 8, &LightingSettings::DistortionEnabled},
	    {"HeatHaze", "Heat haze (px)", 8, &LightingSettings::HeatHaze, 0.0F, 6.0F},
	    {"ShockwaveStrength", "Shockwave strength", 8, &LightingSettings::ShockwaveStrength, 0.0F, 3.0F},
	    {"EffectsSparks", "Sparks", 8, &LightingSettings::EffectsSparks, 0.0F, 3.0F},
	    {"EffectsDust", "Dust", 8, &LightingSettings::EffectsDust, 0.0F, 3.0F},
	    {"EffectsDebris", "Debris", 8, &LightingSettings::EffectsDebris, 0.0F, 3.0F},
	    {"Embers", "Embers", 8, &LightingSettings::Embers, 0.0F, 3.0F},
	    {"SparkLights", "Spark lights", 8, &LightingSettings::SparkLights, 0.0F, 2.0F},
	    {"SmokeScattering", "Smoke scattering", 8, &LightingSettings::SmokeScattering, 0.0F, 3.0F},
	    // Marks and fire
	    {"ScorchMarks", "Scorch marks", 9, &LightingSettings::ScorchMarks},
	    {"Stains", "Blood and oil stains", 9, &LightingSettings::Stains},
	    {"DecalsFade", "Soot and stains fade", 9, &LightingSettings::DecalsFade},
	    {"DecalFadeMinutes", "Soot fades over (min)", 9, &LightingSettings::DecalFadeMinutes, 0.5F, 60.0F},
	    {"LivingWorld", "Living world", 9, &LightingSettings::LivingWorld},
	    {"WetnessMap", "Ground dries place by place", 9, &LightingSettings::WetnessMap},
	    {"WetDrySeconds", "Wet ground dries in (s)", 9, &LightingSettings::WetDrySeconds, 10.0F, 600.0F},
	    {"FireFlameSize", "Flame height", 9, &LightingSettings::FireFlameSize, 0.2F, 3.0F},
	    {"FireFlameBrightness", "Flame brightness", 9, &LightingSettings::FireFlameBrightness, 0.2F, 2.0F},
	    // Units
	    {"UnitOutline", "Outline units", 10, &LightingSettings::UnitOutline},
	    {"UnitOutlineTeamColor", "Outline in team colour", 10, &LightingSettings::UnitOutlineTeamColor},
	    {"UnitOutlineOpacity", "Outline opacity", 10, &LightingSettings::UnitOutlineOpacity, 0.0F, 1.0F},
	    {"UnitOutlineGlow", "Outline glow light", 10, &LightingSettings::UnitOutlineGlow, 0.0F, 2.0F},
	    {"TracerLights", "Tracers light the way", 10, &LightingSettings::TracerLights},
	    {"TracerLightBrightness", "Tracer light brightness", 10, &LightingSettings::TracerLightBrightness, 0.0F, 3.0F},
	    {"TracerLightReach", "Tracer light reach (px)", 10, &LightingSettings::TracerLightReach, 8.0F, 120.0F},
	    {"JetpackLightBrightness", "Jetpack light brightness", 10, &LightingSettings::JetpackLightBrightness, 0.0F, 4.0F},
	    {"JetpackLightReach", "Jetpack light reach", 10, &LightingSettings::JetpackLightReach, 0.2F, 3.0F},
	    // Headlamps
	    {"Headlamps", "Headlamps in the dark", 11, &LightingSettings::Headlamps},
	    {"HeadlampsByDay", "Headlamps by day as well", 11, &LightingSettings::HeadlampsByDay},
	    {"HeadlampsOnlyInDark", "Only where it is dark", 11, &LightingSettings::HeadlampsOnlyInDark},
	    {"HeadlampDarkThreshold", "Darkness to switch on", 11, &LightingSettings::HeadlampDarkThreshold, 0.05F, 0.95F},
	    {"HeadlampBrightness", "Headlamp brightness", 11, &LightingSettings::HeadlampBrightness, 0.0F, 5.0F},
	    {"HeadlampReach", "Headlamp reach (px)", 11, &LightingSettings::HeadlampReach, 40.0F, 600.0F},
	    {"HeadlampWidth", "Headlamp width (degrees)", 11, &LightingSettings::HeadlampWidth, 5.0F, 80.0F},
	    {"HeadlampGlow", "Headlamp glow", 11, &LightingSettings::HeadlampGlow, 0.0F, 2.0F},
	    {"HeadlampTeamTint", "Team colour in beam", 11, &LightingSettings::HeadlampTeamTint, 0.0F, 1.0F},
	    // Saved, but only set here or in Settings.ini.
	    {"ShelterMask", "Exact shelter from the weather", -1, &LightingSettings::ShelterMask},
	    {"ShelterSoftness", "Shelter edge softness", -1, &LightingSettings::ShelterSoftness, 0.0F, 2.0F},
	    {"BackgroundShadowLength", "Background shadow offset", -1, &LightingSettings::BackgroundShadowLength, 0.25F, 3.0F},
	    {"LightingAmbient", "LightingAmbient", -1, &LightingSettings::Ambient},
	    {"LightingForegroundAmbient", "LightingForegroundAmbient", -1, &LightingSettings::ForegroundAmbient},
	    {"LightingSkyColor", "LightingSkyColor", -1, &LightingSettings::SkyColor},
	    {"LightningBrightness", "Lightning brightness", -1, &LightingSettings::LightningBrightness, 0.0F, 2.0F},
	    {"AtmosphereColor", "AtmosphereColor", -1, &LightingSettings::AtmosphereColor},
	    {"CustomWeather", "More weather types", -1, &LightingSettings::CustomWeather},
	    {"WeatherGlow", "Weather glow", -1, &LightingSettings::WeatherGlow, 0.0F, 2.0F},
	    {"WeatherEverywhere", "Weather everywhere", -1, &LightingSettings::WeatherEverywhere},
	    {"WeatherOverBackground", "Weather over the background", -1, &LightingSettings::WeatherOverBackground},
	    {"LightingMaxScreenLights", "Most lights on screen", -1, &LightingSettings::MaxScreenLights, 64.0F, 4096.0F},
	    {"UnitShineLights", "Shine on units from lights", -1, &LightingSettings::UnitShineLights},
	    {"UnitShineLamps", "Shine on units from lamps", -1, &LightingSettings::UnitShineLamps},
	    {"UnitShineSun", "Shine on units from the sun", -1, &LightingSettings::UnitShineSun},
	    {"HazeFromHeat", "Haze only from heat", -1, &LightingSettings::HazeFromHeat},
	    {"FireStyle", "FireStyle", -1, &LightingSettings::FireStyle},
	    {"UnitOutlineOverEverything", "Outline over everything", -1, &LightingSettings::UnitOutlineOverEverything},
	    {"UnitOutlineWidth", "Outline width (px)", -1, &LightingSettings::UnitOutlineWidth, 1.0F, 4.0F},
	    {"UnitOutlineColor", "UnitOutlineColor", -1, &LightingSettings::UnitOutlineColor},
	    {"PaletteAnimation", "Animated palette colours", -1, &LightingSettings::PaletteAnimation},
	    {"PaletteAnimationStrength", "Palette pulse strength", -1, &LightingSettings::PaletteAnimationStrength, 0.0F, 1.0F},
	    {"SmokeShading", "Smoke shades itself", -1, &LightingSettings::SmokeShading},
	    {"SmokeShadingStrength", "Smoke shading strength", -1, &LightingSettings::SmokeShadingStrength, 0.0F, 1.0F},
	    {"StainSurface", "Stains and soot change the shine", -1, &LightingSettings::StainSurface},
	    {"StainShine", "Stain shine strength", -1, &LightingSettings::StainShine, 0.0F, 1.0F},
	    {"ShoulderStart", "Highlight shoulder", -1, &LightingSettings::ShoulderStart, 0.3F, 1.0F},
	    {"HotSpotSeconds", "Hot metal cooling (seconds)", -1, &LightingSettings::HotSpotSeconds, 0.0F, 10.0F},
	    {"AutoExposureLow", "Adapt below", -1, &LightingSettings::AutoExposureLow, 0.001F, 0.2F},
	    {"AutoExposureHigh", "Adapt above", -1, &LightingSettings::AutoExposureHigh, 0.05F, 2.0F},
	    {"AutoExposureGameTime", "Adapt in game time", -1, &LightingSettings::AutoExposureGameTime},
	    {"GradeShadowTint", "GradeShadowTint", -1, &LightingSettings::ShadowTint},
	    {"GradeHighlightTint", "GradeHighlightTint", -1, &LightingSettings::HighlightTint},
	    {"EventBlastFlash", "EventBlastFlash", -1, &LightingSettings::EventBlastFlash},
	    {"EventHurtLook", "EventHurtLook", -1, &LightingSettings::EventHurtLook},
	    {"EventFireWarmth", "EventFireWarmth", -1, &LightingSettings::EventFireWarmth},
	    {"WaterFoam", "Froth", -1, &LightingSettings::WaterFoam, 0.0F, 1.5F},
	    {"WaterFoamStray", "Froth on stray drops", -1, &LightingSettings::WaterFoamStray, 0.0F, 1.0F},
	    {"WaterFoamBrightness", "Froth brightness", -1, &LightingSettings::WaterFoamBrightness, 0.2F, 2.0F},
	    {"WaterFoamGlow", "Froth glow in the dark", -1, &LightingSettings::WaterFoamGlow, 0.0F, 1.0F},
	    {"SoftSmoke", "Soft smoke", -1, &LightingSettings::SoftSmoke, 0.0F, 3.0F},
	    {"WaterLightGlow", "Light glowing through water", -1, &LightingSettings::WaterLightGlow, 0.0F, 1.5F},
	    {"WaterReflections", "Water reflects and refracts", -1, &LightingSettings::WaterReflections},
	    {"WaterReflectionStrength", "Reflection", -1, &LightingSettings::WaterReflectionStrength, 0.0F, 1.0F},
	    {"WaterRefraction", "Refraction", -1, &LightingSettings::WaterRefraction, 0.0F, 1.5F},
	    {"WaterRipples", "Ripples", -1, &LightingSettings::WaterRipples, 0.0F, 2.0F},
	    {"WaterSoftReflection", "Soft reflections", -1, &LightingSettings::WaterSoftReflection},
	    {"WaterMirrorSurface", "Reflection ripples with the surface", -1, &LightingSettings::WaterMirrorSurface},
	    {"WaterFoamBubbles", "Froth bubbling", -1, &LightingSettings::WaterFoamBubbles, 0.0F, 2.0F},
	    {"WaterCaustics", "Wavy lines of light", -1, &LightingSettings::WaterCaustics},
	    {"DistinctLiquidLooks", "Each liquid has its own look", -1, &LightingSettings::DistinctLiquidLooks},
	    {"WaterThinFlow", "Thin streams shown", -1, &LightingSettings::WaterThinFlow, 0.0F, 2.0F},
	    {"PuffVariety", "Turn each froth and spray puff randomly", -1, &LightingSettings::PuffVariety},
	    {"TracerLightRandomness", "Tracer light randomness", -1, &LightingSettings::TracerLightRandomness, 0.0F, 1.0F},
	    {"LightTint", "LightTint", -1, &LightingSettings::LightTint},
	    {"LampTint", "LampTint", -1, &LightingSettings::LampTint},
	    {"LampCache", "Light steady lamps once (faster)", -1, &LightingSettings::LampCache},
	    {"LampCacheDetail", "LampCacheDetail", -1, &LightingSettings::LampCacheDetail},
	    {"HeadlampColor", "HeadlampColor", -1, &LightingSettings::HeadlampColor},
	    {"AimDotsLight", "Aiming dots light the scene", -1, &LightingSettings::AimDotsLight},
	    {"DepthOfFieldFocus", "Focus (battlefield to far background)", -1, &LightingSettings::DepthOfFieldFocus, 0.0F, 1.0F},
	    {"TiltShift", "Tilt-shift", -1, &LightingSettings::TiltShift},
	    {"TiltShiftLine", "Sharp band (top to bottom)", -1, &LightingSettings::TiltShiftLine, 0.0F, 1.0F},
	    {"TiltShiftStrength", "Tilt-shift strength", -1, &LightingSettings::TiltShiftStrength, 0.0F, 2.0F},
	};
	return options;
}

const std::vector<std::string>& LightingOptions::GetPageNames() {
	static const std::vector<std::string> pageNames = {"Lights", "Light detail", "Shadows", "Time and sky", "Clouds and mist", "Weather", "Colour", "Bloom and focus", "Effects", "Marks and fire", "Units", "Headlamps"};
	return pageNames;
}

const LightingOption* LightingOptions::Find(const std::string& key) {
	static const std::unordered_map<std::string, const LightingOption*> optionsByKey = [] {
		std::unordered_map<std::string, const LightingOption*> byKey;
		for (const LightingOption& option: GetAll()) {
			byKey.try_emplace(option.Key, &option);
		}
		return byKey;
	}();
	auto found = optionsByKey.find(key);
	return found != optionsByKey.end() ? found->second : nullptr;
}

float LightingOptions::GetValue(const LightingOption& option, const LightingSettings& settings) {
	if (option.BoolField) {
		return settings.*option.BoolField ? 1.0F : 0.0F;
	} else if (option.FloatField) {
		return settings.*option.FloatField;
	} else if (option.IntField) {
		return static_cast<float>(settings.*option.IntField);
	}
	return 0.0F;
}

void LightingOptions::SetValue(const LightingOption& option, LightingSettings& settings, float value) {
	if (option.HasRange()) {
		value = std::clamp(value, option.Min, option.Max);
	}
	if (option.BoolField) {
		settings.*option.BoolField = value >= 0.5F;
	} else if (option.FloatField) {
		settings.*option.FloatField = value;
	} else if (option.IntField) {
		settings.*option.IntField = static_cast<int>(std::lround(value));
	}
}

std::string LightingOptions::GetValueText(const LightingOption& option, const LightingSettings& settings) {
	if (option.ColorField) {
		const glm::vec3& color = settings.*option.ColorField;
		return std::to_string(color.r) + " " + std::to_string(color.g) + " " + std::to_string(color.b);
	} else if (option.FloatField) {
		return std::to_string(settings.*option.FloatField);
	}
	return std::to_string(static_cast<int>(GetValue(option, settings)));
}

void LightingOptions::SetValueText(const LightingOption& option, LightingSettings& settings, const std::string& text) {
	if (option.ColorField) {
		glm::vec3 color = settings.*option.ColorField;
		std::istringstream stream(text);
		stream >> color.r >> color.g >> color.b;
		if (!stream.fail()) {
			settings.*option.ColorField = glm::clamp(color, glm::vec3(0.0F), glm::vec3(4.0F));
		}
		return;
	}
	float value = 0.0F;
	if (std::sscanf(text.c_str(), "%f", &value) == 1) {
		SetValue(option, settings, value);
	}
}

void LightingOptions::CopyValue(const LightingOption& option, LightingSettings& to, const LightingSettings& from) {
	if (option.BoolField) {
		to.*option.BoolField = from.*option.BoolField;
	} else if (option.FloatField) {
		to.*option.FloatField = from.*option.FloatField;
	} else if (option.IntField) {
		to.*option.IntField = from.*option.IntField;
	} else if (option.ColorField) {
		to.*option.ColorField = from.*option.ColorField;
	}
}
