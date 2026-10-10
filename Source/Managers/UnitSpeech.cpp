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
#include <fstream>
#include <functional>
#include <mutex>
#include <random>
#include <set>
#include <thread>

using namespace RTE;

bool UnitSpeech::s_Enabled = true;
int UnitSpeech::s_ChancePercent = 76;
bool UnitSpeech::s_ShowEnemies = true;

namespace {
	/// One set of lines: the default one, or a faction's or unit type's (an actor's SpeechSet). Lines by trigger index.
	struct LineSet {
		std::string Name;
		std::vector<std::vector<std::string>> Lines;
	};

	std::vector<UnitSpeech::Trigger> s_Triggers;
	std::vector<LineSet> s_Sets; //!< The default set first.
	std::set<std::string> s_TriggersOff; //!< By key. Changed only from the settings (main thread, outside the sim update).
	std::vector<char> s_TriggerOn; //!< The same by trigger index, for the threaded AI to read without a string lookup.
	std::atomic<bool> s_Loaded = false;
	std::mutex s_LoadMutex;

	/// When each side last said each trigger, so a squad doesn't say the same thing in chorus. Teams 0 to 3, the first 64 triggers.
	constexpr int c_TeamTriggerSlots = 64;
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
				continue;
			}
			if (key == "Trigger") {
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
			if (lines.size() <= static_cast<size_t>(trigger)) {
				lines.resize(trigger + 1);
			}
			try {
				if (key == "Line") {
					if (!value.empty()) {
						lines[trigger].push_back(value);
					}
				} else if (key == "ClearLines") {
					lines[trigger].clear();
				} else if (key == "Name") {
					current.Name = value;
				} else if (key == "Description") {
					current.Description = value;
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
	s_Sets.push_back({"Default", {}});
	// Base.rte first (module 0), then every other module in load order, each adding to or changing what came before.
	for (int module = 0; module < g_PresetMan.GetTotalModuleCount(); ++module) {
		if (const DataModule* dataModule = g_PresetMan.GetDataModule(module)) {
			ReadSpeechFile(g_PresetMan.GetFullModulePath(dataModule->GetFileName() + "/Speech.ini"));
		}
	}
	RebuildTriggerOn();
}

bool UnitSpeech::SayOrder(Actor& actor, const std::string& triggerKey) {
	actor.GetSpeech().OrderAnsweredMS = std::max(g_TimerMan.GetSimTimeMS(), 1LL);
	return Say(actor, triggerKey, true);
}

bool UnitSpeech::Say(Actor& actor, const std::string& triggerKey, bool answeringOrder) {
	if (!s_Enabled || s_ChancePercent <= 0 || actor.GetStatus() >= Actor::DYING) {
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
	if (const std::string& setName = actor.GetSpeechSet(); !setName.empty()) {
		if (int set = FindSet(setName); set > 0 && static_cast<size_t>(trigger) < s_Sets[set].Lines.size() && !s_Sets[set].Lines[trigger].empty()) {
			lines = &s_Sets[set].Lines[trigger];
		}
	}
	if (!lines && static_cast<size_t>(trigger) < s_Sets[0].Lines.size() && !s_Sets[0].Lines[trigger].empty()) {
		lines = &s_Sets[0].Lines[trigger];
	}
	if (!lines) {
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

	int count = static_cast<int>(lines->size());
	int pick = std::uniform_int_distribution<int>(0, count - 1)(Random());
	// Not the same line twice running for the same thing.
	if (count > 1 && state.Trigger == trigger && pick == state.Line) {
		pick = (pick + 1 + std::uniform_int_distribution<int>(0, count - 2)(Random())) % count;
	}
	SayText(actor, (*lines)[pick], 0);
	state.Trigger = trigger;
	state.Line = pick;
	return true;
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
