#include "StatusEffect.h"

#include <algorithm>

using namespace RTE;

const std::string StatusEffect::c_ClassName = "StatusEffect";

int StatusEffect::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Serializable::ReadProperty(propName, reader));

	MatchProperty("Name", { reader >> Name; });
	MatchProperty("Label", { reader >> Label; });
	MatchProperty("Harmful", { reader >> Harmful; });
	MatchProperty("SpeedMultiplier", { reader >> SpeedMultiplier; SpeedMultiplier = std::clamp(SpeedMultiplier, 0.0F, 10.0F); });
	MatchProperty("DamageTakenMultiplier", { reader >> DamageTakenMultiplier; DamageTakenMultiplier = std::clamp(DamageTakenMultiplier, 0.0F, 10.0F); });
	MatchProperty("DamageDealtMultiplier", { reader >> DamageDealtMultiplier; DamageDealtMultiplier = std::clamp(DamageDealtMultiplier, 0.0F, 10.0F); });
	MatchProperty("HealthPerSecond", { reader >> HealthPerSecond; });
	MatchProperty("EnergyPerSecond", { reader >> EnergyPerSecond; });
	MatchProperty("FireProof", { reader >> FireProof; });
	MatchProperty("Held", { reader >> Held; });
	MatchProperty("Silenced", { reader >> Silenced; });
	MatchProperty("Shader", { reader >> Shader; });
	MatchProperty("Priority", { reader >> Priority; });
	MatchProperty("LightColor", { reader >> LightColor; });
	MatchProperty("LightRadius", { reader >> LightRadius; });
	MatchProperty("LightIntensity", { reader >> LightIntensity; });
	MatchProperty("Seconds", { reader >> Seconds; });

	EndPropertyList;
}

int StatusEffect::Save(Writer& writer) const {
	Serializable::Save(writer);

	writer.NewPropertyWithValue("Name", Name);
	if (!Label.empty()) {
		writer.NewPropertyWithValue("Label", Label);
	}
	if (Harmful) {
		writer.NewPropertyWithValue("Harmful", Harmful);
	}
	if (SpeedMultiplier != 1.0F) {
		writer.NewPropertyWithValue("SpeedMultiplier", SpeedMultiplier);
	}
	if (DamageTakenMultiplier != 1.0F) {
		writer.NewPropertyWithValue("DamageTakenMultiplier", DamageTakenMultiplier);
	}
	if (DamageDealtMultiplier != 1.0F) {
		writer.NewPropertyWithValue("DamageDealtMultiplier", DamageDealtMultiplier);
	}
	if (HealthPerSecond != 0.0F) {
		writer.NewPropertyWithValue("HealthPerSecond", HealthPerSecond);
	}
	if (EnergyPerSecond != 0.0F) {
		writer.NewPropertyWithValue("EnergyPerSecond", EnergyPerSecond);
	}
	if (FireProof) {
		writer.NewPropertyWithValue("FireProof", FireProof);
	}
	if (Held) {
		writer.NewPropertyWithValue("Held", Held);
	}
	if (Silenced) {
		writer.NewPropertyWithValue("Silenced", Silenced);
	}
	if (!Shader.empty()) {
		writer.NewPropertyWithValue("Shader", Shader);
	}
	if (Priority != 0) {
		writer.NewPropertyWithValue("Priority", Priority);
	}
	if (LightRadius > 0.0F) {
		writer.NewPropertyWithValue("LightColor", LightColor);
		writer.NewPropertyWithValue("LightRadius", LightRadius);
		writer.NewPropertyWithValue("LightIntensity", LightIntensity);
	}
	if (Seconds > 0.0F) {
		writer.NewPropertyWithValue("Seconds", Seconds);
	}
	return 0;
}
