#include "Weather.h"

#include "PresetMan.h"
#include "ConsoleMan.h"

#include <algorithm>
#include <array>
#include <list>
#include <mutex>
#include <sstream>

using namespace RTE;

ConcreteClassInfo(Weather, Entity, 0);

namespace {
	/// The names of the built-in slots 1 to 4.
	const std::array<std::string, Weather::c_BuiltInCount> c_BuiltInNames = {"Rain", "Snow", "Ash Fall", "Dust Storm"};

	/// Reads three numbers, separated by spaces or commas, as a colour.
	glm::vec3 ReadVec3(Reader& reader) {
		std::string text = reader.ReadPropValue();
		for (char& character: text) {
			if (character == ',') {
				character = ' ';
			}
		}
		std::istringstream stream(text);
		glm::vec3 value(0.0F);
		stream >> value.x >> value.y >> value.z;
		return value;
	}

	bool ReadBool(Reader& reader) {
		std::string text = reader.ReadPropValue();
		return !(text == "0" || text == "false" || text == "False" || text.empty());
	}

	int ReadShape(Reader& reader) {
		std::string text = reader.ReadPropValue();
		if (text == "Streak" || text == "0") {
			return Weather::Streak;
		} else if (text == "Flake" || text == "1") {
			return Weather::Flake;
		} else if (text == "Spark" || text == "2") {
			return Weather::Spark;
		} else if (text == "Orb" || text == "3") {
			return Weather::Orb;
		}
		reader.ReportError("Unknown DropShape \"" + text + "\": use Streak, Flake, Spark or Orb.");
		return Weather::Streak;
	}

	std::string Vec3Text(const glm::vec3& value) {
		std::ostringstream stream;
		stream << value.x << " " << value.y << " " << value.z;
		return stream.str();
	}

	/// The slots, rebuilt when modules are added (and until the built-in ones are found).
	struct SlotTable {
		std::mutex Mutex;
		std::vector<const Weather*> Slots{nullptr};
		int ModuleCount = -1;
		bool Complete = false;
		bool WarnedMissing = false;

		void Refresh() {
			int moduleCount = g_PresetMan.GetTotalModuleCount();
			if (moduleCount == ModuleCount && Complete) {
				return;
			}
			std::list<Entity*> presets;
			g_PresetMan.GetAllOfType(presets, "Weather");
			std::vector<const Weather*> slots(1 + Weather::c_BuiltInCount, nullptr);
			std::vector<const Weather*> custom;
			for (const Entity* entity: presets) {
				const Weather* weather = dynamic_cast<const Weather*>(entity);
				if (!weather) {
					continue;
				}
				// Presets come module by module, each module's in the order its files define them (DataModule keeps its type lists in read order).
				// A later module's preset of the same name replaces an earlier one, built-in or not.
				auto builtIn = std::find(c_BuiltInNames.begin(), c_BuiltInNames.end(), weather->GetPresetName());
				if (builtIn != c_BuiltInNames.end()) {
					slots[1 + (builtIn - c_BuiltInNames.begin())] = weather;
					continue;
				}
				auto same = std::find_if(custom.begin(), custom.end(), [weather](const Weather* other) { return other->GetPresetName() == weather->GetPresetName(); });
				if (same != custom.end()) {
					*same = weather;
				} else {
					custom.push_back(weather);
				}
			}
			slots.insert(slots.end(), custom.begin(), custom.end());
			Complete = std::all_of(slots.begin() + 1, slots.begin() + 1 + Weather::c_BuiltInCount, [](const Weather* weather) { return weather != nullptr; });
			if (!Complete && moduleCount > 0 && moduleCount == ModuleCount && !WarnedMissing && !presets.empty()) {
				WarnedMissing = true;
				g_ConsoleMan.PrintString("ERROR: the built-in weather (Base.rte/Weather/Weather.ini: Rain, Snow, Ash Fall, Dust Storm) isn't all defined; the missing ones fall clear.");
			}
			ModuleCount = moduleCount;
			Slots = std::move(slots);
		}
	};

	SlotTable& Table() {
		static SlotTable table;
		return table;
	}
} // namespace

int Weather::Create(const Weather& reference) {
	Entity::Create(reference);
	m_Params = reference.m_Params;
	return 0;
}

int Weather::ReadProperty(const std::string_view& propName, Reader& reader) {
	StartPropertyList(return Entity::ReadProperty(propName, reader));
	MatchProperty("DropsPerScreen", { reader >> m_Params.DropsPerScreen; });
	MatchProperty("DropShape", { m_Params.Shape = ReadShape(reader); });
	MatchProperty("DropColor", {
		m_Params.DropColor = ReadVec3(reader);
		m_Params.DropColor2 = m_Params.DropColor;
	});
	MatchProperty("DropColor2", { m_Params.DropColor2 = ReadVec3(reader); });
	MatchProperty("DropGlow", { reader >> m_Params.DropGlow; });
	MatchProperty("AlphaMin", { reader >> m_Params.AlphaMin; });
	MatchProperty("AlphaMax", { reader >> m_Params.AlphaMax; });
	MatchProperty("LengthMin", { reader >> m_Params.LengthMin; });
	MatchProperty("LengthMax", { reader >> m_Params.LengthMax; });
	MatchProperty("Width", { reader >> m_Params.Width; });
	MatchProperty("FallSpeedMin", { reader >> m_Params.FallSpeedMin; });
	MatchProperty("FallSpeedMax", { reader >> m_Params.FallSpeedMax; });
	MatchProperty("WindFactor", { reader >> m_Params.WindFactor; });
	MatchProperty("Sway", { reader >> m_Params.Sway; });
	MatchProperty("SwayRateMin", { reader >> m_Params.SwayRateMin; });
	MatchProperty("SwayRateMax", { reader >> m_Params.SwayRateMax; });
	MatchProperty("Blown", { m_Params.Blown = ReadBool(reader); });
	MatchProperty("BlownWindScale", { reader >> m_Params.BlownWindScale; });
	MatchProperty("BlownMinSpeed", { reader >> m_Params.BlownMinSpeed; });
	MatchProperty("BlownSpeedMin", { reader >> m_Params.BlownSpeedMin; });
	MatchProperty("BlownSpeedMax", { reader >> m_Params.BlownSpeedMax; });
	MatchProperty("Swirl", { reader >> m_Params.Swirl; });
	MatchProperty("SwirlRate", { reader >> m_Params.SwirlRate; });
	MatchProperty("Jitter", { reader >> m_Params.Jitter; });
	MatchProperty("JitterRate", { reader >> m_Params.JitterRate; });
	MatchProperty("PulseRate", { reader >> m_Params.PulseRate; });
	MatchProperty("PulseDepth", { reader >> m_Params.PulseDepth; });
	MatchProperty("Twinkle", { reader >> m_Params.Twinkle; });
	MatchProperty("Shelter", { m_Params.Shelter = ReadBool(reader); });
	MatchProperty("DropShader", { m_Params.DropShader = reader.ReadPropValue(); });
	MatchProperty("Splashes", { reader >> m_Params.Splashes; });
	MatchProperty("SplashColor", { m_Params.SplashColor = ReadVec3(reader); });
	MatchProperty("SplashGlow", { reader >> m_Params.SplashGlow; });
	MatchProperty("Rain", { reader >> m_Params.Rain; });
	MatchProperty("SnowCover", { reader >> m_Params.SnowCover; });
	MatchProperty("Mist", { reader >> m_Params.Mist; });
	MatchProperty("Overcast", { reader >> m_Params.Overcast; });
	MatchProperty("CloudCover", { reader >> m_Params.CloudCover; });
	MatchProperty("Lightning", { reader >> m_Params.Lightning; });
	MatchProperty("HazeColor", { m_Params.HazeColor = ReadVec3(reader); });
	MatchProperty("HazeColorAmount", { reader >> m_Params.HazeColorAmount; });
	MatchProperty("Haze", { reader >> m_Params.Haze; });
	MatchProperty("SceneTint", { m_Params.SceneTint = ReadVec3(reader); });
	MatchProperty("Sound", { m_Params.Sound = reader.ReadPropValue(); });
	EndPropertyList;
}

int Weather::Save(Writer& writer) const {
	Entity::Save(writer);
	static constexpr std::array<const char*, 4> shapeNames = {"Streak", "Flake", "Spark", "Orb"};
	writer.NewPropertyWithValue("DropsPerScreen", m_Params.DropsPerScreen);
	writer.NewPropertyWithValue("DropShape", std::string(shapeNames[std::clamp(m_Params.Shape, 0, 3)]));
	writer.NewPropertyWithValue("DropColor", Vec3Text(m_Params.DropColor));
	writer.NewPropertyWithValue("DropColor2", Vec3Text(m_Params.DropColor2));
	writer.NewPropertyWithValue("DropGlow", m_Params.DropGlow);
	writer.NewPropertyWithValue("AlphaMin", m_Params.AlphaMin);
	writer.NewPropertyWithValue("AlphaMax", m_Params.AlphaMax);
	writer.NewPropertyWithValue("LengthMin", m_Params.LengthMin);
	writer.NewPropertyWithValue("LengthMax", m_Params.LengthMax);
	writer.NewPropertyWithValue("Width", m_Params.Width);
	writer.NewPropertyWithValue("FallSpeedMin", m_Params.FallSpeedMin);
	writer.NewPropertyWithValue("FallSpeedMax", m_Params.FallSpeedMax);
	writer.NewPropertyWithValue("WindFactor", m_Params.WindFactor);
	writer.NewPropertyWithValue("Sway", m_Params.Sway);
	writer.NewPropertyWithValue("SwayRateMin", m_Params.SwayRateMin);
	writer.NewPropertyWithValue("SwayRateMax", m_Params.SwayRateMax);
	writer.NewPropertyWithValue("Blown", m_Params.Blown);
	writer.NewPropertyWithValue("BlownWindScale", m_Params.BlownWindScale);
	writer.NewPropertyWithValue("BlownMinSpeed", m_Params.BlownMinSpeed);
	writer.NewPropertyWithValue("BlownSpeedMin", m_Params.BlownSpeedMin);
	writer.NewPropertyWithValue("BlownSpeedMax", m_Params.BlownSpeedMax);
	writer.NewPropertyWithValue("Swirl", m_Params.Swirl);
	writer.NewPropertyWithValue("SwirlRate", m_Params.SwirlRate);
	writer.NewPropertyWithValue("Jitter", m_Params.Jitter);
	writer.NewPropertyWithValue("JitterRate", m_Params.JitterRate);
	writer.NewPropertyWithValue("PulseRate", m_Params.PulseRate);
	writer.NewPropertyWithValue("PulseDepth", m_Params.PulseDepth);
	writer.NewPropertyWithValue("Twinkle", m_Params.Twinkle);
	writer.NewPropertyWithValue("Shelter", m_Params.Shelter);
	if (!m_Params.DropShader.empty()) {
		writer.NewPropertyWithValue("DropShader", m_Params.DropShader);
	}
	writer.NewPropertyWithValue("Splashes", m_Params.Splashes);
	writer.NewPropertyWithValue("SplashColor", Vec3Text(m_Params.SplashColor));
	writer.NewPropertyWithValue("SplashGlow", m_Params.SplashGlow);
	writer.NewPropertyWithValue("Rain", m_Params.Rain);
	writer.NewPropertyWithValue("SnowCover", m_Params.SnowCover);
	writer.NewPropertyWithValue("Mist", m_Params.Mist);
	writer.NewPropertyWithValue("Overcast", m_Params.Overcast);
	writer.NewPropertyWithValue("CloudCover", m_Params.CloudCover);
	writer.NewPropertyWithValue("Lightning", m_Params.Lightning);
	writer.NewPropertyWithValue("HazeColor", Vec3Text(m_Params.HazeColor));
	writer.NewPropertyWithValue("HazeColorAmount", m_Params.HazeColorAmount);
	writer.NewPropertyWithValue("Haze", m_Params.Haze);
	writer.NewPropertyWithValue("SceneTint", Vec3Text(m_Params.SceneTint));
	if (!m_Params.Sound.empty()) {
		writer.NewPropertyWithValue("Sound", m_Params.Sound);
	}
	return 0;
}

glm::vec2 Weather::GetMeanFall(float wind) const {
	return glm::vec2(wind * m_Params.WindFactor, (m_Params.FallSpeedMin + m_Params.FallSpeedMax) * 0.5F);
}

const Weather* Weather::GetSlot(int slot, bool customOn) {
	if (slot <= 0 || (!customOn && slot > c_BuiltInCount)) {
		return nullptr;
	}
	SlotTable& table = Table();
	std::scoped_lock lock(table.Mutex);
	table.Refresh();
	return slot < static_cast<int>(table.Slots.size()) ? table.Slots[slot] : nullptr;
}

int Weather::GetSlotCount() {
	SlotTable& table = Table();
	std::scoped_lock lock(table.Mutex);
	table.Refresh();
	return std::max(static_cast<int>(table.Slots.size()), 1 + c_BuiltInCount);
}

std::vector<std::string> Weather::GetSlotNames() {
	SlotTable& table = Table();
	std::scoped_lock lock(table.Mutex);
	table.Refresh();
	std::vector<std::string> names{"Clear"};
	for (size_t slot = 1; slot < std::max(table.Slots.size(), static_cast<size_t>(1 + c_BuiltInCount)); ++slot) {
		const Weather* weather = slot < table.Slots.size() ? table.Slots[slot] : nullptr;
		names.push_back(weather ? weather->GetPresetName() : (slot <= c_BuiltInCount ? c_BuiltInNames[slot - 1] : std::string("?")));
	}
	return names;
}

std::string Weather::GetComboItems(bool customOn) {
	std::string items;
	std::vector<std::string> names = GetSlotNames();
	for (size_t slot = 0; slot < names.size() && (customOn || slot <= c_BuiltInCount); ++slot) {
		items += names[slot];
		items += '\0';
	}
	items += '\0';
	return items;
}

int Weather::FindSlot(const std::string& name) {
	std::vector<std::string> names = GetSlotNames();
	auto found = std::find(names.begin(), names.end(), name);
	return found != names.end() ? static_cast<int>(found - names.begin()) : -1;
}
