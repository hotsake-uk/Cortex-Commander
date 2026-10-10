#include "UnitSpeech.h"
#include "Actor.h"
#include "ConsoleMan.h"
#include "DataModule.h"
#include "FrameMan.h"
#include "PresetMan.h"
#include "TimerMan.h"
#include "TextOverlay.h"
#include "GUI.h"
#include "AllegroBitmap.h"

#include "allegro.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <random>
#include <map>
#include <set>
#include <sstream>
#include <thread>

using namespace RTE;

bool UnitSpeech::s_Enabled = true;
int UnitSpeech::s_ChancePercent = 76;
bool UnitSpeech::s_ShowEnemies = true;
bool UnitSpeech::s_ShowBubbles = true;

namespace {
	/// One set of lines: the default one, or a faction's or unit type's (an actor's SpeechSet). Lines by trigger index.
	struct LineSet {
		std::string Name;
		std::vector<std::vector<std::string>> Lines;
		std::vector<std::string> UnitNames; //!< What its units are called (UnitName), none to use the default set's.
		std::vector<std::vector<unsigned>> LineTones; //!< Each line's tones as bits (s_Tones), by trigger as Lines; 0 for a line of no tone, which fits any.
	};

	/// The tones lines come in, in the order first used; a line's tones are bits in this order. At most 32.
	std::vector<std::string> s_Tones;
	/// The tones each side's units speak in, by name (so a side can be set before Speech.ini is read); none for any.
	std::array<std::set<std::string>, 4> s_TeamTones;
	/// The same as bits, for the threaded AI to read: 0 for any.
	std::array<std::atomic<unsigned>, 4> s_TeamToneBits{};
	/// How often each tone is picked against the others, 0 to 100, by name (so it can be set before Speech.ini is read); unset is 100.
	std::map<std::string, int> s_ToneWeights;
	/// The weights Speech.ini files give tones (ToneWeight), for those the settings haven't set; unset is 100.
	std::map<std::string, int> s_ToneDefaultWeights;
	/// What each tone is like, for the settings' tooltips (ToneDescription).
	std::map<std::string, std::string> s_ToneDescriptions;

	int ToneWeightOf(const std::string& tone) {
		if (auto weight = s_ToneWeights.find(tone); weight != s_ToneWeights.end()) {
			return weight->second;
		}
		auto weight = s_ToneDefaultWeights.find(tone);
		return weight == s_ToneDefaultWeights.end() ? 100 : weight->second;
	}
	/// The same by tone bit, for the threaded AI to read.
	std::array<std::atomic<int>, 32> s_ToneWeightBits{};

	/// The bit of a tone, adding it to the list if it's new; 0 if there are already 32.
	unsigned ToneBit(const std::string& tone) {
		auto found = std::find(s_Tones.begin(), s_Tones.end(), tone);
		if (found == s_Tones.end()) {
			if (s_Tones.size() >= 32) {
				return 0;
			}
			s_Tones.push_back(tone);
			found = s_Tones.end() - 1;
		}
		return 1u << static_cast<unsigned>(found - s_Tones.begin());
	}

	void RebuildTeamToneBits() {
		for (size_t team = 0; team < s_TeamTones.size(); ++team) {
			unsigned bits = 0;
			for (const std::string& tone: s_TeamTones[team]) {
				if (auto found = std::find(s_Tones.begin(), s_Tones.end(), tone); found != s_Tones.end()) {
					bits |= 1u << static_cast<unsigned>(found - s_Tones.begin());
				}
			}
			// (Tones a side asked for that no Speech.ini has: it would never speak, so it takes any.)
			s_TeamToneBits[team].store(s_TeamTones[team].empty() || bits == 0 ? 0u : bits, std::memory_order_relaxed);
		}
		for (size_t bit = 0; bit < s_ToneWeightBits.size(); ++bit) {
			s_ToneWeightBits[bit].store(bit < s_Tones.size() ? ToneWeightOf(s_Tones[bit]) : 100, std::memory_order_relaxed);
		}
	}

	/// The order a side works through a trigger's lines in: shuffled, each said once before any is said again, so a side doesn't repeat
	/// itself while it has lines it hasn't said. By set, trigger and side.
	struct Deck {
		std::vector<int> Order;
		size_t Next = 0;
	};
	std::map<long long, Deck> s_Decks;
	std::mutex s_DeckMutex;

	std::vector<UnitSpeech::Trigger> s_Triggers;
	std::vector<LineSet> s_Sets; //!< The default set first.
	std::set<std::string> s_TriggersOff; //!< By key. Changed only from the settings (main thread, outside the sim update).
	std::vector<char> s_TriggerOn; //!< The same by trigger index, for the threaded AI to read without a string lookup.
	std::atomic<bool> s_Loaded = false;
	std::mutex s_LoadMutex;

	/// When each side last said each trigger, so a squad doesn't say the same thing in chorus. Teams 0 to 3, the first 256 triggers.
	constexpr int c_TeamTriggerSlots = 256;
	std::array<std::array<std::atomic<long long>, c_TeamTriggerSlots>, 4> s_TeamLastSaidMS{};

	/// Speech's own random numbers, one generator per thread: the threaded AI says things too, and the sim's stream must not move.
	std::mt19937& Random() {
		thread_local std::mt19937 generator(static_cast<unsigned int>(std::hash<std::thread::id>()(std::this_thread::get_id()) ^ static_cast<size_t>(std::chrono::steady_clock::now().time_since_epoch().count())));
		return generator;
	}

	std::string Trim(const std::string& text) {
		size_t first = text.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			return "";
		}
		size_t last = text.find_last_not_of(" \t\r\n");
		return text.substr(first, last - first + 1);
	}

	int FindSet(const std::string& name) {
		for (size_t i = 0; i < s_Sets.size(); ++i) {
			if (s_Sets[i].Name == name) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void RebuildTriggerOn() {
		s_TriggerOn.assign(s_Triggers.size(), 1);
		for (size_t i = 0; i < s_Triggers.size(); ++i) {
			s_TriggerOn[i] = s_TriggersOff.count(s_Triggers[i].Key) == 0;
		}
	}

	/// Reads one Speech.ini. Lines are "Key = Value"; "//" starts a comment. "Set = Name" starts a set (lines before any Set are the
	/// default set's), "Trigger = Key" starts a trigger (defining it the first time any file names it); the trigger's settings and its
	/// lines follow. See Base.rte/Speech.ini.
	void ReadSpeechFile(const std::string& path) {
		std::ifstream file(path);
		if (!file) {
			return;
		}
		int set = 0;
		int trigger = -1;
		unsigned tone = 0; //!< The tones of the lines that follow (Tone), 0 for none.
		int lineNumber = 0;
		std::string line;
		while (std::getline(file, line)) {
			++lineNumber;
			// (Not in a Line, whose words may have a "//" in them; a comment there goes on a line of its own.)
			bool isLine = Trim(line.substr(0, line.find('='))) == "Line";
			if (size_t comment = line.find("//"); comment != std::string::npos && !isLine) {
				line.erase(comment);
			}
			size_t equals = line.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			std::string key = Trim(line.substr(0, equals));
			std::string value = Trim(line.substr(equals + 1));
			if (key == "Set") {
				set = FindSet(value);
				if (set < 0) {
					s_Sets.push_back({value, {}});
					set = static_cast<int>(s_Sets.size()) - 1;
				}
				trigger = -1;
				tone = 0;
				continue;
			}
			if (key == "UnitName") {
				if (!value.empty() && std::find(s_Sets[set].UnitNames.begin(), s_Sets[set].UnitNames.end(), value) == s_Sets[set].UnitNames.end()) {
					s_Sets[set].UnitNames.push_back(value);
				}
				continue;
			}
			if (key == "ToneWeight" || key == "ToneDescription") {
				// "ToneWeight = Imperial: 0": how often the tone comes up in the mix until a player sets it (0: only for a side that picks it).
				// "ToneDescription = Imperial: ...": what it's like, for the settings. Either also adds the tone to the settings' list.
				if (size_t colon = value.find(':'); colon != std::string::npos) {
					if (std::string name = Trim(value.substr(0, colon)); !name.empty() && name != "Any") {
						ToneBit(name);
						std::string rest = Trim(value.substr(colon + 1));
						if (key == "ToneDescription") {
							s_ToneDescriptions[name] = rest;
						} else {
							s_ToneDefaultWeights[name] = std::clamp(std::atoi(rest.c_str()), 0, 100);
						}
					}
				}
				continue;
			}
			if (key == "ClearUnitNames") {
				s_Sets[set].UnitNames.clear();
				continue;
			}
			if (key == "Trigger") {
				tone = 0;
				trigger = UnitSpeech::FindTrigger(value);
				if (trigger < 0 && !value.empty()) {
					UnitSpeech::Trigger added;
					added.Key = value;
					added.Name = value;
					s_Triggers.push_back(added);
					trigger = static_cast<int>(s_Triggers.size()) - 1;
				}
				continue;
			}
			if (trigger < 0) {
				g_ConsoleMan.PrintString("WARNING: " + path + " line " + std::to_string(lineNumber) + ": \"" + key + "\" before any Trigger; passed over.");
				continue;
			}
			UnitSpeech::Trigger& current = s_Triggers[trigger];
			std::vector<std::vector<std::string>>& lines = s_Sets[set].Lines;
			std::vector<std::vector<unsigned>>& lineTones = s_Sets[set].LineTones;
			if (lines.size() <= static_cast<size_t>(trigger)) {
				lines.resize(trigger + 1);
			}
			if (lineTones.size() <= static_cast<size_t>(trigger)) {
				lineTones.resize(trigger + 1);
			}
			try {
				if (key == "Line") {
					if (!value.empty()) {
						lines[trigger].push_back(value);
						lineTones[trigger].resize(lines[trigger].size() - 1);
						lineTones[trigger].push_back(tone);
					}
				} else if (key == "Tone") {
					// "Tone = Serious, Casual": the lines after it, until the next Tone, are of those tones. "Any" or nothing: of none.
					tone = 0;
					std::stringstream names(value);
					for (std::string name; std::getline(names, name, ',');) {
						if (name = Trim(name); !name.empty() && name != "Any") {
							tone |= ToneBit(name);
						}
					}
				} else if (key == "ClearLines") {
					lines[trigger].clear();
					lineTones[trigger].clear();
				} else if (key == "Name") {
					current.Name = value;
				} else if (key == "Description") {
					current.Description = value;
				} else if (key == "Group") {
					current.Group = value;
				} else if (key == "Chance") {
					current.Chance = std::max(0.0F, std::stof(value));
				} else if (key == "Cooldown") {
					current.CooldownMS = std::max(0, std::stoi(value));
				} else if (key == "TeamCooldown") {
					current.TeamCooldownMS = std::max(0, std::stoi(value));
				} else if (key == "Urgent") {
					current.Urgent = std::stoi(value) != 0;
				} else if (key == "Order") {
					current.Order = std::stoi(value) != 0;
				} else {
					g_ConsoleMan.PrintString("WARNING: " + path + " line " + std::to_string(lineNumber) + ": unknown property \"" + key + "\"; passed over.");
				}
			} catch (const std::exception&) {
				g_ConsoleMan.PrintString("WARNING: " + path + " line " + std::to_string(lineNumber) + ": \"" + value + "\" isn't a number; passed over.");
			}
		}
	}

	/// The edge colour of a side's bubbles: the game's team colours (as the team icons and the sandbox's sides), in the HUD's palette.
	int TeamEdgeColor(int team) {
		static std::array<int, 5> colors{-1, -1, -1, -1, -1};
		static const std::array<std::array<int, 3>, 5> rgb{{{249, 120, 100}, {170, 210, 100}, {110, 180, 250}, {248, 230, 100}, {109, 117, 170}}};
		int which = (team >= 0 && team < 4) ? team : 4;
		if (colors[which] < 0) {
			colors[which] = makecol8(rgb[which][0], rgb[which][1], rgb[which][2]);
		}
		return colors[which];
	}
} // namespace

void UnitSpeech::SetChance(int percent) {
	s_ChancePercent = std::clamp(percent, 0, 100);
}

bool UnitSpeech::IsTriggerOn(const std::string& key) {
	return s_TriggersOff.count(key) == 0;
}

void UnitSpeech::SetTriggerOn(const std::string& key, bool on) {
	if (on) {
		s_TriggersOff.erase(key);
	} else if (!key.empty()) {
		s_TriggersOff.insert(key);
	}
	RebuildTriggerOn();
}

std::vector<std::string> UnitSpeech::GetTriggersOff() {
	return std::vector<std::string>(s_TriggersOff.begin(), s_TriggersOff.end());
}

std::vector<std::string> UnitSpeech::GetTones() {
	EnsureLoaded();
	return s_Tones;
}

std::string UnitSpeech::GetTeamTonesText(int team) {
	if (team < 0 || team >= 4 || s_TeamTones[team].empty()) {
		return "Any";
	}
	std::string text;
	for (const std::string& tone: s_TeamTones[team]) {
		text += (text.empty() ? "" : ", ") + tone;
	}
	return text;
}

void UnitSpeech::SetTeamTonesText(int team, const std::string& text) {
	if (team < 0 || team >= 4) {
		return;
	}
	s_TeamTones[team].clear();
	std::stringstream names(text);
	for (std::string name; std::getline(names, name, ',');) {
		if (name = Trim(name); !name.empty() && name != "Any") {
			s_TeamTones[team].insert(name);
		}
	}
	RebuildTeamToneBits();
}

bool UnitSpeech::TeamUsesTone(int team, const std::string& tone) {
	return team < 0 || team >= 4 || s_TeamTones[team].empty() || s_TeamTones[team].count(tone) > 0;
}

bool UnitSpeech::TeamUsesAnyTone(int team) {
	return team < 0 || team >= 4 || s_TeamTones[team].empty();
}

void UnitSpeech::SetTeamTone(int team, const std::string& tone, bool on) {
	if (team < 0 || team >= 4) {
		return;
	}
	if (on) {
		s_TeamTones[team].insert(tone);
	} else {
		s_TeamTones[team].erase(tone);
	}
	RebuildTeamToneBits();
}

int UnitSpeech::GetToneWeight(const std::string& tone) {
	return ToneWeightOf(tone);
}

std::string UnitSpeech::GetToneDescription(const std::string& tone) {
	auto description = s_ToneDescriptions.find(tone);
	return description == s_ToneDescriptions.end() ? "" : description->second;
}

void UnitSpeech::SetToneWeight(const std::string& tone, int weight) {
	s_ToneWeights[tone] = std::clamp(weight, 0, 100);
	RebuildTeamToneBits();
}

std::string UnitSpeech::GetToneWeightsText() {
	std::string text;
	for (const auto& [tone, weight]: s_ToneWeights) {
		text += (text.empty() ? "" : ", ") + tone + ":" + std::to_string(weight);
	}
	return text;
}

void UnitSpeech::SetToneWeightsText(const std::string& text) {
	s_ToneWeights.clear();
	std::stringstream entries(text);
	for (std::string entry; std::getline(entries, entry, ',');) {
		if (size_t colon = entry.find(':'); colon != std::string::npos) {
			if (std::string tone = Trim(entry.substr(0, colon)); !tone.empty()) {
				s_ToneWeights[tone] = std::clamp(std::atoi(Trim(entry.substr(colon + 1)).c_str()), 0, 100);
			}
		}
	}
	RebuildTeamToneBits();
}

void UnitSpeech::SetTeamAnyTone(int team) {
	if (team >= 0 && team < 4) {
		s_TeamTones[team].clear();
		RebuildTeamToneBits();
	}
}

const std::vector<UnitSpeech::Trigger>& UnitSpeech::GetTriggers() {
	EnsureLoaded();
	return s_Triggers;
}

int UnitSpeech::FindTrigger(const std::string& key) {
	for (size_t i = 0; i < s_Triggers.size(); ++i) {
		if (s_Triggers[i].Key == key) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

std::string UnitSpeech::GetExampleLine(int trigger) {
	EnsureLoaded();
	if (trigger < 0 || s_Sets.empty() || static_cast<size_t>(trigger) >= s_Sets[0].Lines.size() || s_Sets[0].Lines[trigger].empty()) {
		return "";
	}
	return s_Sets[0].Lines[trigger].front();
}

void UnitSpeech::EnsureLoaded() {
	if (s_Loaded.load(std::memory_order_acquire)) {
		return;
	}
	std::scoped_lock lock(s_LoadMutex);
	if (!s_Loaded.load(std::memory_order_relaxed)) {
		LoadAll();
		s_Loaded.store(true, std::memory_order_release);
	}
}

void UnitSpeech::Reload() {
	std::scoped_lock lock(s_LoadMutex);
	LoadAll();
	s_Loaded.store(true, std::memory_order_release);
}

void UnitSpeech::LoadAll() {
	s_Triggers.clear();
	s_Sets.clear();
	s_Tones.clear();
	s_ToneDefaultWeights.clear();
	s_ToneDescriptions.clear();
	{
		std::scoped_lock lock(s_DeckMutex);
		s_Decks.clear();
	}
	s_Sets.push_back({"Default", {}});
	// Base.rte first (module 0), then every other module in load order, each adding to or changing what came before.
	for (int module = 0; module < g_PresetMan.GetTotalModuleCount(); ++module) {
		if (const DataModule* dataModule = g_PresetMan.GetDataModule(module)) {
			ReadSpeechFile(g_PresetMan.GetFullModulePath(dataModule->GetFileName() + "/Speech.ini"));
			// And any files in its Speech folder, by name: a mod can keep each tone or set of lines in a file of its own.
			std::error_code error;
			const std::string folder = g_PresetMan.GetFullModulePath(dataModule->GetFileName() + "/Speech");
			if (std::filesystem::is_directory(folder, error)) {
				std::vector<std::filesystem::path> files;
				for (const std::filesystem::directory_entry& entry: std::filesystem::directory_iterator(folder, error)) {
					if (entry.is_regular_file(error) && entry.path().extension() == ".ini") {
						files.push_back(entry.path());
					}
				}
				std::sort(files.begin(), files.end());
				for (const std::filesystem::path& file: files) {
					ReadSpeechFile(file.string());
				}
			}
		}
	}
	RebuildTriggerOn();
	RebuildTeamToneBits();
}

bool UnitSpeech::SayOrder(Actor& actor, const std::string& triggerKey) {
	actor.GetSpeech().OrderAnsweredMS = std::max(g_TimerMan.GetSimTimeMS(), 1LL);
	return Say(actor, triggerKey, true, nullptr);
}

bool UnitSpeech::Say(Actor& actor, const std::string& triggerKey, bool answeringOrder, const Actor* subject) {
	// (Animals don't talk.)
	if (!s_Enabled || s_ChancePercent <= 0 || actor.GetStatus() >= Actor::DYING || actor.IsAnimal()) {
		return false;
	}
	EnsureLoaded();
	int trigger = FindTrigger(triggerKey);
	if (trigger < 0 || !s_TriggerOn[trigger]) {
		return false;
	}
	const Trigger& definition = s_Triggers[trigger];
	State& state = actor.GetSpeech();
	const long long now = g_TimerMan.GetSimTimeMS();
	// (A time ahead of now is from before the sim clock was last reset: taken as long ago.)
	auto recently = [now](long long then, int windowMS) { return then > 0 && then <= now && now - then < windowMS; };

	// An order a command has just answered (or chosen not to) isn't answered again by the AI's guess at it.
	if (definition.Order && !answeringOrder && recently(state.OrderAnsweredMS, 1500)) {
		return false;
	}
	// Still saying something (and a moment's pause after it), unless this can't wait.
	if (!state.Text.empty() && recently(state.StartMS, state.DurationMS + 400) && !definition.Urgent) {
		return false;
	}
	if (state.LastSaidMS.size() < s_Triggers.size()) {
		state.LastSaidMS.resize(s_Triggers.size(), 0);
	}
	if (recently(state.LastSaidMS[trigger], definition.CooldownMS)) {
		return false;
	}
	// The chance is rolled once per cooldown: a trigger that fires every hit is considered again only after the cooldown, said or not.
	state.LastSaidMS[trigger] = std::max(now, 1LL);
	float chance = static_cast<float>(s_ChancePercent) / 100.0F * definition.Chance;
	if (std::uniform_real_distribution<float>(0.0F, 1.0F)(Random()) >= chance) {
		return false;
	}
	// The unit's own set's lines for this, else the default set's.
	const std::vector<std::string>* lines = nullptr;
	int lineSet = 0;
	if (const std::string& setName = actor.GetSpeechSet(); !setName.empty()) {
		if (int set = FindSet(setName); set > 0 && static_cast<size_t>(trigger) < s_Sets[set].Lines.size() && !s_Sets[set].Lines[trigger].empty()) {
			lines = &s_Sets[set].Lines[trigger];
			lineSet = set;
		}
	}
	if (!lines && static_cast<size_t>(trigger) < s_Sets[0].Lines.size() && !s_Sets[0].Lines[trigger].empty()) {
		lines = &s_Sets[0].Lines[trigger];
	}
	if (!lines) {
		return false;
	}
	// A line that names someone, only with someone to name; and only in the tones the side speaks in (a line of no tone fits any).
	const std::vector<unsigned>* tones = static_cast<size_t>(trigger) < s_Sets[lineSet].LineTones.size() ? &s_Sets[lineSet].LineTones[trigger] : nullptr;
	unsigned teamTones = actor.GetTeam() >= 0 && actor.GetTeam() < 4 ? s_TeamToneBits[actor.GetTeam()].load(std::memory_order_relaxed) : 0u;
	auto fits = [&](int index) {
		const unsigned lineTones = tones && static_cast<size_t>(index) < tones->size() ? (*tones)[index] : 0u;
		return (subject || (*lines)[index].find("{name}") == std::string::npos) && (teamTones == 0 || lineTones == 0 || (lineTones & teamTones) != 0);
	};
	// One of the side's tones, by the settings' mix (Serious 10, Funny 90), out of those it has a line of here: so a side with only some
	// tones ticked shares the whole mix between them. The line is then one of that tone, or of none.
	unsigned toneOfLine = 0;
	if (tones) {
		unsigned available = 0;
		for (int index = 0; index < static_cast<int>(lines->size()); ++index) {
			if (fits(index) && static_cast<size_t>(index) < tones->size()) {
				available |= (*tones)[index] & (teamTones == 0 ? ~0u : teamTones);
			}
		}
		int total = 0;
		int toneCount = 0;
		for (unsigned bit = 0; bit < 32; ++bit) {
			if (available & (1u << bit)) {
				total += s_ToneWeightBits[bit].load(std::memory_order_relaxed);
				++toneCount;
			}
		}
		// (All of them at 0: any of them alike.)
		int roll = total > 0 || toneCount > 0 ? std::uniform_int_distribution<int>(0, (total > 0 ? total : toneCount) - 1)(Random()) : -1;
		for (unsigned bit = 0; bit < 32 && roll >= 0 && toneOfLine == 0; ++bit) {
			if (available & (1u << bit)) {
				roll -= total > 0 ? s_ToneWeightBits[bit].load(std::memory_order_relaxed) : 1;
				if (roll < 0) {
					toneOfLine = 1u << bit;
				}
			}
		}
	}
	auto fitsTone = [&](int index) {
		const unsigned lineTones = tones && static_cast<size_t>(index) < tones->size() ? (*tones)[index] : 0u;
		return fits(index) && (toneOfLine == 0 || lineTones == 0 || (lineTones & toneOfLine) != 0);
	};
	bool anyFits = false;
	for (int index = 0; index < static_cast<int>(lines->size()) && !anyFits; ++index) {
		anyFits = fits(index);
	}
	// None in the side's tones (a mod's tone with no lines for this): any tone rather than nothing.
	if (!anyFits && teamTones != 0) {
		teamTones = 0;
		for (int index = 0; index < static_cast<int>(lines->size()) && !anyFits; ++index) {
			anyFits = fits(index);
		}
	}
	if (!anyFits) {
		return false;
	}
	// A friend just said it: left to them. The side's slot is taken only by one who does say it.
	int team = actor.GetTeam();
	if (team >= 0 && team < 4 && trigger < c_TeamTriggerSlots) {
		std::atomic<long long>& teamSlot = s_TeamLastSaidMS[team][trigger];
		long long last = teamSlot.load(std::memory_order_relaxed);
		do {
			if (recently(last, definition.TeamCooldownMS)) {
				return false;
			}
		} while (!teamSlot.compare_exchange_weak(last, std::max(now, 1LL), std::memory_order_relaxed));
	}

	// The next line in the side's shuffled deck for this trigger, passing over those that don't fit.
	const int count = static_cast<int>(lines->size());
	int pick = -1;
	{
		std::scoped_lock lock(s_DeckMutex);
		Deck& deck = s_Decks[(static_cast<long long>(lineSet) << 32) | (static_cast<long long>(trigger) << 8) | static_cast<long long>(team + 1)];
		for (int tries = 0; tries <= count * 2 && pick < 0; ++tries) {
			if (deck.Next >= deck.Order.size() || static_cast<int>(deck.Order.size()) != count) {
				const int last = deck.Order.empty() || deck.Next == 0 ? -1 : deck.Order[deck.Next - 1];
				deck.Order.resize(count);
				for (int i = 0; i < count; ++i) {
					deck.Order[i] = i;
				}
				std::shuffle(deck.Order.begin(), deck.Order.end(), Random());
				// (Not the line just said, first again after the shuffle.)
				if (count > 1 && deck.Order[0] == last) {
					std::swap(deck.Order[0], deck.Order[count - 1]);
				}
				deck.Next = 0;
			}
			int candidate = deck.Order[deck.Next++];
			if (fitsTone(candidate)) {
				pick = candidate;
			}
		}
	}
	if (pick < 0) {
		return false;
	}
	std::string text = (*lines)[pick];
	auto replaceAll = [&text](const std::string& from, const std::string& to) {
		for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
			text.replace(at, from.size(), to);
		}
	};
	if (subject) {
		replaceAll("{name}", GetName(*subject));
	}
	replaceAll("{self}", GetName(actor));
	SayText(actor, text, 0);
	state.Trigger = trigger;
	state.Line = pick;
	return true;
}

std::string UnitSpeech::GetName(const Actor& actor) {
	EnsureLoaded();
	const std::vector<std::string>* names = nullptr;
	if (const std::string& setName = actor.GetSpeechSet(); !setName.empty()) {
		if (int set = FindSet(setName); set > 0 && !s_Sets[set].UnitNames.empty()) {
			names = &s_Sets[set].UnitNames;
		}
	}
	if (!names && !s_Sets.empty() && !s_Sets[0].UnitNames.empty()) {
		names = &s_Sets[0].UnitNames;
	}
	if (!names) {
		return "buddy";
	}
	// (Mixed, so units made one after another don't get neighbouring names off the list.)
	unsigned long long id = static_cast<unsigned long long>(actor.GetUniqueID()) * 0x9E3779B97F4A7C15ULL;
	id ^= id >> 29;
	return (*names)[static_cast<size_t>(id % names->size())];
}

void UnitSpeech::SayText(Actor& actor, const std::string& text, int durationMS) {
	if (!s_Enabled || text.empty() || actor.GetStatus() >= Actor::DYING) {
		return;
	}
	State& state = actor.GetSpeech();
	state.Text = text;
	state.StartMS = std::max(g_TimerMan.GetSimTimeMS(), 1LL);
	// About as long as it takes to read: 1.8 s for a word or two, up to 4.5 s.
	state.DurationMS = durationMS > 0 ? durationMS : std::clamp(1500 + 55 * static_cast<int>(text.size()), 1800, 4500);
	state.Trigger = -1;
	state.Line = -1;
}

void UnitSpeech::DrawBubble(BITMAP* targetBitmap, int x, int y, const State& state, int team) {
	GUIFont* font = g_FrameMan.GetSmallFont();
	if (!targetBitmap || !font || state.Text.empty()) {
		return;
	}
	const long long age = g_TimerMan.GetSimTimeMS() - state.StartMS;
	// Zoomed far out the text comes out bigger than its spot in the view (TextOverlay keeps it readable), so the bubble grows to hold it.
	const float growth = TextOverlay::GetTextGrowth(targetBitmap);
	const int textWidth = static_cast<int>(std::ceil(static_cast<float>(font->CalculateWidth(state.Text)) * growth));
	const int textHeight = static_cast<int>(std::ceil(static_cast<float>(font->GetFontHeight()) * growth));
	const int padX = 4;
	const int padY = 2;
	const int tail = 3;
	// It pops up a few pixels as it's said.
	const int rise = age < 120 ? static_cast<int>((120 - age) / 30) : 0;
	const int bottom = y - tail - 1 + rise;
	const int top = bottom - textHeight - padY * 2;
	int left = x - textWidth / 2 - padX;
	int right = x + (textWidth - textWidth / 2) + padX;
	// Kept on the screen for a unit at its edge (the tail still points at the unit).
	int shift = 0;
	if (left < 1) {
		shift = 1 - left;
	} else if (right > targetBitmap->w - 2) {
		shift = targetBitmap->w - 2 - right;
	}
	left += shift;
	right += shift;

	if (!s_ShowBubbles) {
		// Bare text, where the bubble's text would be (its dark edge keeps it readable on any ground).
		AllegroBitmap bitmapInt(targetBitmap);
		font->DrawAligned(&bitmapInt, x + shift, top + padY, state.Text, GUIFont::Centre);
		return;
	}

	// The game's menu panels: dark blue, with a light edge (here the side's colour), the corners cut.
	static const int fill = makecol8(12, 20, 39);
	const int edge = TeamEdgeColor(team);
	rectfill(targetBitmap, left + 1, top + 1, right - 1, bottom - 1, fill);
	hline(targetBitmap, left + 1, top, right - 1, edge);
	hline(targetBitmap, left + 1, bottom, right - 1, edge);
	vline(targetBitmap, left, top + 1, bottom - 1, edge);
	vline(targetBitmap, right, top + 1, bottom - 1, edge);
	// The tail, pointing down at the unit.
	for (int row = 0; row < tail; ++row) {
		int half = tail - 1 - row;
		hline(targetBitmap, x - half, bottom + row, x + half, fill);
		putpixel(targetBitmap, x - half - 1, bottom + row, edge);
		putpixel(targetBitmap, x + half + 1, bottom + row, edge);
	}
	putpixel(targetBitmap, x, bottom + tail, edge);

	AllegroBitmap bitmapInt(targetBitmap);
	font->DrawAligned(&bitmapInt, x + shift, top + padY, state.Text, GUIFont::Centre);
}
