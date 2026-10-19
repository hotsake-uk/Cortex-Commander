#include "UnitSpeechVoice.h"
#include "UnitSpeech.h"
#include "Activity.h"
#include "ActivityMan.h"
#include "Actor.h"
#include "AudioMan.h"
#include "CameraMan.h"
#include "ConsoleMan.h"
#include "DataModule.h"
#include "FrameMan.h"
#include "MovableMan.h"
#include "PresetMan.h"
#include "SceneMan.h"
#include "TimerMan.h"

#include "System/RSynth/darray.h"
#include "System/RSynth/klatt.h"
#include "System/RSynth/tts.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <vector>

using namespace RTE;

bool UnitSpeechVoice::s_Enabled = true;
int UnitSpeechVoice::s_VolumePercent = 70;
int UnitSpeechVoice::s_RadioPercent = 75;
bool UnitSpeechVoice::s_HearsEnemies = true;
bool UnitSpeechVoice::s_SpeaksWords = true;

namespace {
	constexpr int c_Rate = 22050; //!< The voices' sample rate.
	constexpr float c_Pi = 3.14159265F;
	constexpr int c_MaxVoices = 5; //!< Voices at once: a big fight is a few voices over the radio, not a wall of them.
	constexpr long long c_FreshMS = 500; //!< A line older than this when first seen (the setting just turned on, a reload) isn't voiced.

	/// What a kind of unit sounds like, as Voices.ini defines it.
	struct Voice {
		std::string Name;
		bool Words = false; //!< Speaks the actual words (the synthesised voice), where the settings let it; else babbles.
		float Pitch = 120.0F; //!< Its pitch, in Hz.
		float PitchSpread = 0.18F; //!< How far one unit's pitch is from another's, as a share of Pitch.
		float Square = 0.3F; //!< 0: a soft voice; 1: a square-wave beep (Undertale).
		float Formant = 1.0F; //!< Its vowels' formants, times: under 1 a bigger throat, over 1 a smaller one.
		float Speed = 1.0F; //!< How fast it talks, times.
		float Bounce = 1.0F; //!< How far its pitch steps from syllable to syllable, times.
		float Radio = 1.0F; //!< How much radio, times the settings'.
		float Crush = 0.0F; //!< 0 to 1: bit-crushed and decimated, for machines.
		float Ring = 0.0F; //!< A ring modulator's frequency in Hz, for machines; 0 for none.
		float Growl = 0.0F; //!< 0 to 1: rough, with its pitch halving every other period (the undead).
		float Breath = 0.0F; //!< 0 to 1: breathy, whispered.
		float Volume = 1.0F; //!< Times.
		int Wave = RSynth::KW_SAW; //!< The synthesised voice's source wave (Words).
	};

	/// How a tone of line (Speech.ini's Tone) changes the voice.
	struct ToneStyle {
		float Radio = 1.0F; //!< Times.
		float Bounce = 1.0F; //!< Times.
		float Speed = 1.0F; //!< Times.
		bool RogerBeep = false; //!< Ends on a radio beep.
	};

	/// Which units speak with which voice: "VoiceForSet = Undead: Undead". The most particular kind that matches wins.
	enum RuleKind { ForActor, ForSet, ForGroup, ForClass, ForModule, RuleKinds };
	struct Rule {
		RuleKind Kind;
		std::string Match;
		std::string VoiceName;
	};

	std::vector<Voice> s_Voices;
	std::map<std::string, ToneStyle> s_ToneStyles;
	std::vector<Rule> s_Rules;
	std::string s_DefaultVoice;
	bool s_Loaded = false;

	/// A voice playing, to let go of its sound when it ends.
	struct Playing {
		FMOD::Sound* Sound = nullptr;
		FMOD::Channel* Channel = nullptr;
		long ActorID = 0;
	};
	std::vector<Playing> s_Playing;
	FMOD::System* s_PlayingSystem = nullptr; //!< The FMOD system the playing sounds were made with.

	std::string Trim(const std::string& text) {
		size_t first = text.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			return "";
		}
		size_t last = text.find_last_not_of(" \t\r\n");
		return text.substr(first, last - first + 1);
	}

	int WaveByName(const std::string& name) {
		static const std::map<std::string, int> waves{{"Saw", RSynth::KW_SAW}, {"Triangle", RSynth::KW_TRIANGLE}, {"Sine", RSynth::KW_SIN}, {"Square", RSynth::KW_SQUARE}, {"Pulse", RSynth::KW_PULSE}, {"Noise", RSynth::KW_NOISE}, {"Warble", RSynth::KW_WARBLE}};
		auto found = waves.find(name);
		return found == waves.end() ? RSynth::KW_SAW : found->second;
	}

	/// Reads one Voices.ini. "Voice = Name" starts a voice (defining it the first time any file names it), "Tone = Name" a tone's style; their
	/// properties follow. "VoiceFor... = What: Voice" and "DefaultVoice = Voice" can come anywhere. See Base.rte/Voices.ini.
	void ReadVoiceFile(const std::string& path) {
		std::ifstream file(path);
		if (!file) {
			return;
		}
		Voice* voice = nullptr;
		ToneStyle* tone = nullptr;
		int lineNumber = 0;
		std::string line;
		while (std::getline(file, line)) {
			++lineNumber;
			if (size_t comment = line.find("//"); comment != std::string::npos) {
				line.erase(comment);
			}
			size_t equals = line.find('=');
			if (equals == std::string::npos) {
				continue;
			}
			std::string key = Trim(line.substr(0, equals));
			std::string value = Trim(line.substr(equals + 1));
			if (key == "Voice") {
				auto found = std::find_if(s_Voices.begin(), s_Voices.end(), [&value](const Voice& existing) { return existing.Name == value; });
				if (found == s_Voices.end()) {
					s_Voices.push_back({});
					s_Voices.back().Name = value;
					found = s_Voices.end() - 1;
				}
				voice = &*found;
				tone = nullptr;
				continue;
			}
			if (key == "Tone") {
				tone = &s_ToneStyles[value];
				voice = nullptr;
				continue;
			}
			if (key == "DefaultVoice") {
				s_DefaultVoice = value;
				continue;
			}
			static const std::map<std::string, RuleKind> ruleKeys{{"VoiceForActor", ForActor}, {"VoiceForSet", ForSet}, {"VoiceForGroup", ForGroup}, {"VoiceForClass", ForClass}, {"VoiceForModule", ForModule}};
			if (auto rule = ruleKeys.find(key); rule != ruleKeys.end()) {
				// (The last colon: a group's name can have one in it.)
				size_t colon = value.rfind(':');
				if (colon == std::string::npos) {
					g_ConsoleMan.PrintString("WARNING: " + path + " line " + std::to_string(lineNumber) + ": \"" + key + "\" wants \"What: Voice\"; passed over.");
					continue;
				}
				std::string match = Trim(value.substr(0, colon));
				std::string voiceName = Trim(value.substr(colon + 1));
				auto existing = std::find_if(s_Rules.begin(), s_Rules.end(), [&](const Rule& other) { return other.Kind == rule->second && other.Match == match; });
				if (existing != s_Rules.end()) {
					existing->VoiceName = voiceName;
				} else {
					s_Rules.push_back({rule->second, match, voiceName});
				}
				continue;
			}
			if (!voice && !tone) {
				g_ConsoleMan.PrintString("WARNING: " + path + " line " + std::to_string(lineNumber) + ": \"" + key + "\" before any Voice or Tone; passed over.");
				continue;
			}
			try {
				if (tone) {
					if (key == "Radio") {
						tone->Radio = std::max(0.0F, std::stof(value));
					} else if (key == "Bounce") {
						tone->Bounce = std::max(0.0F, std::stof(value));
					} else if (key == "Speed") {
						tone->Speed = std::clamp(std::stof(value), 0.25F, 4.0F);
					} else if (key == "RogerBeep") {
						tone->RogerBeep = std::stoi(value) != 0;
					} else {
						g_ConsoleMan.PrintString("WARNING: " + path + " line " + std::to_string(lineNumber) + ": unknown tone property \"" + key + "\"; passed over.");
					}
					continue;
				}
				if (key == "Words") {
					voice->Words = std::stoi(value) != 0;
				} else if (key == "Pitch") {
					voice->Pitch = std::clamp(std::stof(value), 30.0F, 800.0F);
				} else if (key == "PitchSpread") {
					voice->PitchSpread = std::clamp(std::stof(value), 0.0F, 0.9F);
				} else if (key == "Square") {
					voice->Square = std::clamp(std::stof(value), 0.0F, 1.0F);
				} else if (key == "Formant") {
					voice->Formant = std::clamp(std::stof(value), 0.5F, 2.0F);
				} else if (key == "Speed") {
					voice->Speed = std::clamp(std::stof(value), 0.25F, 4.0F);
				} else if (key == "Bounce") {
					voice->Bounce = std::max(0.0F, std::stof(value));
				} else if (key == "Radio") {
					voice->Radio = std::max(0.0F, std::stof(value));
				} else if (key == "Crush") {
					voice->Crush = std::clamp(std::stof(value), 0.0F, 1.0F);
				} else if (key == "Ring") {
					voice->Ring = std::clamp(std::stof(value), 0.0F, 2000.0F);
				} else if (key == "Growl") {
					voice->Growl = std::clamp(std::stof(value), 0.0F, 1.0F);
				} else if (key == "Breath") {
					voice->Breath = std::clamp(std::stof(value), 0.0F, 1.0F);
				} else if (key == "Volume") {
					voice->Volume = std::clamp(std::stof(value), 0.0F, 4.0F);
				} else if (key == "Wave") {
					voice->Wave = WaveByName(value);
				} else {
					g_ConsoleMan.PrintString("WARNING: " + path + " line " + std::to_string(lineNumber) + ": unknown voice property \"" + key + "\"; passed over.");
				}
			} catch (const std::exception&) {
				g_ConsoleMan.PrintString("WARNING: " + path + " line " + std::to_string(lineNumber) + ": \"" + value + "\" isn't a number; passed over.");
			}
		}
	}

	void LoadAll() {
		s_Voices.clear();
		s_ToneStyles.clear();
		s_Rules.clear();
		s_DefaultVoice.clear();
		// Base.rte first (module 0), then every other module in load order, each adding to or changing what came before.
		for (int module = 0; module < g_PresetMan.GetTotalModuleCount(); ++module) {
			if (const DataModule* dataModule = g_PresetMan.GetDataModule(module)) {
				ReadVoiceFile(g_PresetMan.GetFullModulePath(dataModule->GetFileName() + "/Voices.ini"));
			}
		}
		s_Loaded = true;
	}

	const Voice& VoiceOf(const Actor& actor) {
		if (!s_Loaded) {
			LoadAll();
		}
		static const Voice fallback;
		const Rule* best = nullptr;
		for (const Rule& rule: s_Rules) {
			if (best && best->Kind <= rule.Kind) {
				continue;
			}
			bool matches = false;
			switch (rule.Kind) {
				case ForActor:
					matches = actor.GetPresetName() == rule.Match;
					break;
				case ForSet:
					matches = actor.GetSpeechSet() == rule.Match;
					break;
				case ForGroup:
					matches = actor.IsInGroup(rule.Match);
					break;
				case ForClass:
					matches = actor.GetClassName() == rule.Match;
					break;
				case ForModule:
					if (const DataModule* dataModule = g_PresetMan.GetDataModule(actor.GetModuleID())) {
						matches = dataModule->GetFileName() == rule.Match;
					}
					break;
				default:
					break;
			}
			if (matches) {
				best = &rule;
			}
		}
		const std::string& name = best ? best->VoiceName : s_DefaultVoice;
		for (const Voice& voice: s_Voices) {
			if (voice.Name == name) {
				return voice;
			}
		}
		return s_Voices.empty() ? fallback : s_Voices.front();
	}

	/// The style of a line's tone: its first tone that Voices.ini styles, else the plain one.
	ToneStyle StyleOf(unsigned tones) {
		if (tones == 0) {
			return {};
		}
		const std::vector<std::string> names = UnitSpeech::GetTones();
		for (size_t bit = 0; bit < names.size() && bit < 32; ++bit) {
			if (tones & (1u << bit)) {
				if (auto style = s_ToneStyles.find(names[bit]); style != s_ToneStyles.end()) {
					return style->second;
				}
			}
		}
		return {};
	}

#pragma region Filters
	/// A two-pole resonator (Klatt's): a peak at a frequency, unity gain at DC.
	struct Resonator {
		float A = 1.0F, B = 0.0F, C = 0.0F, Y1 = 0.0F, Y2 = 0.0F;
		void Set(float frequency, float bandwidth) {
			frequency = std::clamp(frequency, 40.0F, c_Rate * 0.45F);
			C = -std::exp(-2.0F * c_Pi * bandwidth / c_Rate);
			B = 2.0F * std::exp(-c_Pi * bandwidth / c_Rate) * std::cos(2.0F * c_Pi * frequency / c_Rate);
			A = 1.0F - B - C;
		}
		float Run(float input) {
			float output = A * input + B * Y1 + C * Y2;
			Y2 = Y1;
			Y1 = output;
			return output;
		}
	};

	/// A biquad, high- or low-pass (the RBJ cookbook's).
	struct Biquad {
		float B0 = 1.0F, B1 = 0.0F, B2 = 0.0F, A1 = 0.0F, A2 = 0.0F, X1 = 0.0F, X2 = 0.0F, Y1 = 0.0F, Y2 = 0.0F;
		void Set(bool highPass, float frequency, float q = 0.7071F) {
			float w = 2.0F * c_Pi * std::clamp(frequency, 20.0F, c_Rate * 0.45F) / c_Rate;
			float alpha = std::sin(w) / (2.0F * q);
			float cosW = std::cos(w);
			float a0 = 1.0F + alpha;
			if (highPass) {
				B0 = (1.0F + cosW) / 2.0F / a0;
				B1 = -(1.0F + cosW) / a0;
			} else {
				B0 = (1.0F - cosW) / 2.0F / a0;
				B1 = (1.0F - cosW) / a0;
			}
			B2 = B0;
			A1 = -2.0F * cosW / a0;
			A2 = (1.0F - alpha) / a0;
		}
		float Run(float input) {
			float output = B0 * input + B1 * X1 + B2 * X2 - A1 * Y1 - A2 * Y2;
			X2 = X1;
			X1 = input;
			Y2 = Y1;
			Y1 = output;
			return output;
		}
	};

	/// Brings a voice to a steady loudness, its peaks softly limited: the synthesised voices are spiky (a glottal pulse through resonators), so
	/// by their peaks alone they come out quiet against the radio's static.
	void Level(std::vector<float>& samples) {
		double sum = 0.0;
		for (float sample: samples) {
			sum += static_cast<double>(sample) * sample;
		}
		const float rms = samples.empty() ? 0.0F : static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
		if (rms < 1e-6F) {
			return;
		}
		for (float& sample: samples) {
			sample = std::tanh(sample * 0.12F / rms);
		}
	}

	void Normalize(std::vector<float>& samples, float peak) {
		float loudest = 0.0F;
		for (float sample: samples) {
			loudest = std::max(loudest, std::abs(sample));
		}
		if (loudest > 1e-6F) {
			for (float& sample: samples) {
				sample *= peak / loudest;
			}
		}
	}
#pragma endregion

#pragma region Babble
	/// One stretch of the babble: what the voice aims for over it (the renderer glides from one to the next).
	struct Segment {
		float Seconds = 0.0F;
		float Voiced = 0.0F; //!< The voice's loudness.
		float Noise = 0.0F; //!< The hiss's loudness (s, f, a plosive's burst).
		float NoiseFrequency = 3000.0F;
		float F1 = 500.0F, F2 = 1500.0F, F3 = 2500.0F; //!< The formants.
		float Semitones = 0.0F; //!< The pitch, from the voice's own.
	};

	bool IsVowel(char letter) { return letter == 'a' || letter == 'e' || letter == 'i' || letter == 'o' || letter == 'u' || letter == 'y'; }

	/// A vowel's first three formants (a man's, Peterson and Barney's averages).
	void VowelFormants(char vowel, Segment& segment) {
		switch (vowel) {
			case 'a': segment.F1 = 730.0F; segment.F2 = 1090.0F; segment.F3 = 2440.0F; break;
			case 'e': segment.F1 = 530.0F; segment.F2 = 1840.0F; segment.F3 = 2480.0F; break;
			case 'i': segment.F1 = 390.0F; segment.F2 = 1990.0F; segment.F3 = 2550.0F; break;
			case 'o': segment.F1 = 570.0F; segment.F2 = 840.0F; segment.F3 = 2410.0F; break;
			case 'u': segment.F1 = 440.0F; segment.F2 = 1020.0F; segment.F3 = 2240.0F; break;
			default: segment.F1 = 490.0F; segment.F2 = 1350.0F; segment.F3 = 2400.0F; break; // (A schwa.)
		}
	}

	/// Turns a line into segments: a syllable for each vowel group of each word, each a consonant then a vowel, at a pitch that steps about
	/// (by a hash of the syllable, so the same words always sound the same from the same unit), with pauses at spaces and punctuation.
	std::vector<Segment> Syllables(const std::string& text, const Voice& voice, const ToneStyle& style, unsigned seed, float speed) {
		std::vector<Segment> segments;
		// (The pitch steps an Undertale or Animal Crossing voice takes: mostly small, now and then a jump.)
		static const std::array<float, 10> steps{0.0F, 2.0F, -2.0F, 0.0F, 3.0F, -1.0F, 5.0F, -3.0F, 1.0F, 4.0F};
		const float bounce = voice.Bounce * style.Bounce;
		// The whole line's mood, from how it ends.
		std::string trimmed = Trim(text);
		const char last = trimmed.empty() ? '.' : trimmed.back();
		const bool exclaims = last == '!';
		const bool asks = last == '?';
		const float lift = exclaims ? 2.0F : 0.0F;
		if (exclaims) {
			speed *= 1.12F;
		}
		// The words.
		struct Word {
			std::string Letters;
			bool Shouted = false;
			float PauseAfter = 0.035F;
		};
		std::vector<Word> words;
		Word current;
		int capitals = 0;
		auto endWord = [&](float pause) {
			if (!current.Letters.empty()) {
				current.Shouted = current.Letters.size() > 1 && capitals == static_cast<int>(current.Letters.size());
				current.PauseAfter = pause;
				words.push_back(current);
			} else if (!words.empty()) {
				words.back().PauseAfter = std::max(words.back().PauseAfter, pause);
			}
			current = {};
			capitals = 0;
		};
		for (size_t i = 0; i < trimmed.size(); ++i) {
			const unsigned char character = static_cast<unsigned char>(trimmed[i]);
			if (std::isalpha(character)) {
				capitals += std::isupper(character) ? 1 : 0;
				current.Letters += static_cast<char>(std::tolower(character));
			} else if (std::isdigit(character)) {
				// (A digit is a syllable of its own: "two hundred meters" babbles about as long as "200 meters".)
				current.Letters += "o";
			} else if (character == '\'') {
				continue;
			} else if (character == ',' || character == ';' || character == ':' || character == '-') {
				endWord(0.13F);
			} else if (character == '.' || character == '!' || character == '?') {
				endWord(trimmed.compare(i, 3, "...") == 0 ? 0.32F : 0.2F);
			} else {
				endWord(0.035F);
			}
		}
		endWord(0.0F);
		// Each word's syllables.
		struct Syllable {
			char Onset = 0;
			char Vowel = 0;
			int Letters = 1;
			bool Stressed = false;
			bool Shouted = false;
			float PauseAfter = 0.0F;
		};
		std::vector<Syllable> syllables;
		for (const Word& word: words) {
			std::vector<Syllable> inWord;
			const std::string& letters = word.Letters;
			for (size_t i = 0; i < letters.size();) {
				// ("y" first in a word is a consonant.)
				if (IsVowel(letters[i]) && !(letters[i] == 'y' && i == 0)) {
					Syllable syllable;
					syllable.Vowel = letters[i];
					syllable.Onset = i > 0 && !IsVowel(letters[i - 1]) ? letters[i - 1] : 0;
					size_t end = i;
					while (end < letters.size() && IsVowel(letters[end])) {
						++end;
					}
					syllable.Letters = static_cast<int>(end - i);
					inWord.push_back(syllable);
					i = end;
				} else {
					++i;
				}
			}
			// (A silent e: "take", "move".)
			if (inWord.size() > 1 && letters.size() > 2 && letters.back() == 'e' && !IsVowel(letters[letters.size() - 2])) {
				inWord.pop_back();
			}
			// ("Hmm", "Psst".)
			if (inWord.empty()) {
				Syllable syllable;
				syllable.Vowel = 'u';
				syllable.Onset = letters[0];
				inWord.push_back(syllable);
			}
			// (A long word babbles for four syllables at most.)
			if (inWord.size() > 4) {
				inWord.resize(4);
			}
			inWord.front().Stressed = true;
			for (Syllable& syllable: inWord) {
				syllable.Shouted = word.Shouted;
			}
			inWord.back().PauseAfter = word.PauseAfter;
			syllables.insert(syllables.end(), inWord.begin(), inWord.end());
		}
		if (syllables.empty()) {
			return segments;
		}
		// (Mumbled at most 40 syllables: a long line still ends while its bubble shows.)
		if (syllables.size() > 40) {
			syllables.resize(40);
		}
		const int count = static_cast<int>(syllables.size());
		unsigned hash = seed;
		for (int index = 0; index < count; ++index) {
			const Syllable& syllable = syllables[index];
			hash = hash * 2654435761u + static_cast<unsigned>(syllable.Vowel) * 31u + static_cast<unsigned>(syllable.Onset) + static_cast<unsigned>(index);
			const float jitter = 0.85F + 0.3F * static_cast<float>((hash >> 8) % 1000) / 1000.0F;
			float semitones = steps[(hash >> 4) % steps.size()] * bounce + lift - 2.0F * static_cast<float>(index) / static_cast<float>(count);
			if (asks && index >= count - 2) {
				semitones += index == count - 1 ? 5.0F : 3.0F;
			}
			if (syllable.Shouted) {
				semitones += 2.0F;
			}
			const float loudness = (syllable.Stressed ? 1.0F : 0.8F) * (syllable.Shouted || exclaims ? 1.2F : 1.0F);
			Segment vowel;
			VowelFormants(syllable.Vowel, vowel);
			vowel.Semitones = semitones;
			vowel.Voiced = loudness;
			vowel.Seconds = (0.075F + 0.02F * static_cast<float>(std::min(syllable.Letters, 3))) * jitter / speed;
			// The consonant before it.
			Segment onset = vowel;
			onset.Voiced = 0.0F;
			switch (syllable.Onset) {
				case 's': case 'z': case 'c': case 'x': case 'j':
					onset.Seconds = 0.045F / speed;
					onset.Noise = 0.45F * loudness;
					onset.NoiseFrequency = 4800.0F;
					onset.Voiced = syllable.Onset == 'z' || syllable.Onset == 'j' ? 0.3F * loudness : 0.0F;
					break;
				case 'f': case 'v': case 'h':
					onset.Seconds = 0.035F / speed;
					onset.Noise = 0.25F * loudness;
					onset.NoiseFrequency = syllable.Onset == 'h' ? 1500.0F : 2600.0F;
					onset.Voiced = syllable.Onset == 'v' ? 0.3F * loudness : 0.0F;
					break;
				case 'p': case 'b': case 't': case 'd': case 'k': case 'g': case 'q': {
					// (A plosive: a moment's closure, then a burst.)
					Segment closure = onset;
					closure.Seconds = 0.018F / speed;
					segments.push_back(closure);
					onset.Seconds = 0.012F / speed;
					onset.Noise = 0.55F * loudness;
					onset.NoiseFrequency = (syllable.Onset == 't' || syllable.Onset == 'd') ? 4000.0F : (syllable.Onset == 'k' || syllable.Onset == 'g' || syllable.Onset == 'q') ? 2200.0F : 900.0F;
					break;
				}
				case 'm': case 'n': case 'l': case 'r': case 'w':
					onset.Seconds = 0.035F / speed;
					onset.Voiced = 0.55F * loudness;
					onset.F1 = 300.0F;
					onset.F2 = syllable.Onset == 'w' ? 700.0F : syllable.Onset == 'm' ? 1100.0F : syllable.Onset == 'n' ? 1500.0F : 1250.0F;
					onset.F3 = syllable.Onset == 'r' ? 1700.0F : 2400.0F;
					break;
				default:
					onset.Seconds = 0.0F;
					break;
			}
			if (onset.Seconds > 0.0F) {
				segments.push_back(onset);
			}
			segments.push_back(vowel);
			if (syllable.PauseAfter > 0.0F) {
				Segment pause = vowel;
				pause.Voiced = 0.0F;
				pause.Seconds = syllable.PauseAfter / speed;
				segments.push_back(pause);
			}
		}
		return segments;
	}

	/// Renders the babble: a glottal pulse (softened, or a square) through three formant resonators in cascade, plus a hiss through its own,
	/// every target glided to so syllables run into each other as speech does.
	std::vector<float> Babble(const std::string& text, const Voice& voice, const ToneStyle& style, float pitch, float formant, float speed, unsigned seed) {
		std::vector<Segment> segments = Syllables(text, voice, style, seed, speed);
		std::vector<float> samples;
		if (segments.empty()) {
			return samples;
		}
		float seconds = 0.0F;
		for (const Segment& segment: segments) {
			seconds += segment.Seconds;
		}
		samples.reserve(static_cast<size_t>((seconds + 0.1F) * c_Rate));
		std::minstd_rand random(seed | 1u);
		std::uniform_real_distribution<float> noise(-1.0F, 1.0F);
		Resonator f1, f2, f3, hiss;
		float phase = 0.0F;
		int period = 0;
		float soft = 0.0F;
		float jitter = 0.0F;
		// The gliding values, and how fast they glide (per sample).
		float voiced = 0.0F, noiseLoudness = 0.0F, noiseFrequency = 3000.0F;
		float formant1 = segments.front().F1, formant2 = segments.front().F2, formant3 = segments.front().F3;
		float semitones = segments.front().Semitones;
		const float glide = 1.0F - std::exp(-1.0F / (0.012F * c_Rate));
		const float pitchGlide = 1.0F - std::exp(-1.0F / (0.02F * c_Rate));
		const float attack = 1.0F - std::exp(-1.0F / (0.006F * c_Rate));
		for (const Segment& segment: segments) {
			const int length = static_cast<int>(segment.Seconds * c_Rate);
			for (int i = 0; i < length; ++i) {
				voiced += (segment.Voiced - voiced) * attack;
				noiseLoudness += (segment.Noise - noiseLoudness) * attack;
				noiseFrequency += (segment.NoiseFrequency - noiseFrequency) * glide;
				formant1 += (segment.F1 * formant - formant1) * glide;
				formant2 += (segment.F2 * formant - formant2) * glide;
				formant3 += (segment.F3 * formant - formant3) * glide;
				semitones += (segment.Semitones - semitones) * pitchGlide;
				if ((i & 15) == 0) {
					f1.Set(formant1, 90.0F);
					f2.Set(formant2, 110.0F);
					f3.Set(formant3, 160.0F);
					hiss.Set(noiseFrequency, noiseFrequency * 0.5F);
				}
				// The source: one period at a time, its pitch jittered a little each period (more for a growl), every other one at half
				// height for a growl's rasp.
				const float frequency = pitch * std::pow(2.0F, semitones / 12.0F) * (1.0F + jitter);
				phase += frequency / c_Rate;
				if (phase >= 1.0F) {
					phase -= 1.0F;
					++period;
					jitter = noise(random) * (0.01F + 0.05F * voice.Growl);
				}
				const float saw = 1.0F - 2.0F * phase;
				const float square = phase < 0.5F ? 1.0F : -1.0F;
				soft += (saw - soft) * 0.35F;
				float source = soft * (1.0F - voice.Square) + square * voice.Square * 0.6F;
				if (voice.Growl > 0.0F && (period & 1)) {
					source *= 1.0F - 0.5F * voice.Growl;
				}
				source = source * (1.0F - voice.Breath) + noise(random) * voice.Breath * 0.7F;
				float out = f3.Run(f2.Run(f1.Run(source * voiced)));
				out += hiss.Run(noise(random) * noiseLoudness) * 6.0F;
				samples.push_back(out);
			}
		}
		// (A moment for the last syllable to ring out.)
		for (int i = 0; i < c_Rate / 25; ++i) {
			samples.push_back(f3.Run(f2.Run(f1.Run(0.0F))));
		}
		return samples;
	}
#pragma endregion

	/// The synthesised voice saying the line's words (RSynth, at its own 11025 Hz), brought up to the voices' rate.
	std::vector<float> Words(const std::string& text, const Voice& voice, float pitch, float speed) {
		std::vector<float> samples;
		std::string clean;
		for (char character: text) {
			clean += static_cast<unsigned char>(character) < 128 ? character : ' ';
		}
		if (clean.size() > 200) {
			clean.resize(200);
		}
		RSynth::darray phonemes;
		RSynth::xlate_string(clean.c_str(), &phonemes);
		RSynth::darray elements;
		RSynth::klatt::phone_to_elm(phonemes.getData(), phonemes.getSize(), &elements);
		if (elements.getSize() <= 0) {
			return samples;
		}
		RSynth::klatt synth;
		// (Its pitch in tenths of a Hz; its speed in ms per frame, 10 its usual.)
		synth.init(static_cast<int>(pitch * 10.0F), std::clamp(9.0F / speed, 4.0F, 20.0F), 0.5F, voice.Wave);
		synth.initsynth(elements.getSize(), reinterpret_cast<unsigned char*>(elements.getData()));
		std::vector<short> frame(static_cast<size_t>(synth.mNspFr) * 100);
		std::vector<float> slow;
		for (int count = synth.synth(synth.mNspFr, frame.data()); count >= 0; count = synth.synth(synth.mNspFr, frame.data())) {
			for (int i = 0; i < count; ++i) {
				slow.push_back(static_cast<float>(frame[i]) / 32768.0F);
			}
			// (Five seconds at most.)
			if (slow.size() > 11025 * 5) {
				break;
			}
		}
		samples.reserve(slow.size() * 2);
		for (size_t i = 0; i < slow.size(); ++i) {
			const float next = i + 1 < slow.size() ? slow[i + 1] : 0.0F;
			samples.push_back(slow[i]);
			samples.push_back((slow[i] + next) * 0.5F);
		}
		return samples;
	}

	/// The machine in a robot's voice: a ring modulator and a bit crusher.
	void Machine(std::vector<float>& samples, const Voice& voice) {
		if (voice.Ring > 0.0F) {
			for (size_t i = 0; i < samples.size(); ++i) {
				samples[i] *= 0.45F + 0.55F * std::sin(2.0F * c_Pi * voice.Ring * static_cast<float>(i) / c_Rate);
			}
		}
		if (voice.Crush > 0.0F) {
			Normalize(samples, 0.9F);
			const int hold = 1 + static_cast<int>(voice.Crush * 3.0F);
			const float levels = std::pow(2.0F, 12.0F - 8.0F * voice.Crush);
			float held = 0.0F;
			for (size_t i = 0; i < samples.size(); ++i) {
				if (i % hold == 0) {
					held = std::round(samples[i] * levels) / levels;
				}
				samples[i] = held;
			}
		}
	}

	/// Puts a voice through a radio: a squelch's click and burst before it, static under it, a roger beep and a squelch tail after it, then all
	/// of it band-limited and driven. amount 0 leaves it as it is.
	void Radio(std::vector<float>& samples, float amount, bool rogerBeep, unsigned seed) {
		if (amount <= 0.01F || samples.empty()) {
			return;
		}
		std::minstd_rand random(seed * 7919u + 13u);
		std::uniform_real_distribution<float> noise(-1.0F, 1.0F);
		std::uniform_real_distribution<float> chance(0.0F, 1.0F);
		const float strength = std::min(amount, 1.0F);
		std::vector<float> keyed;
		keyed.reserve(samples.size() + c_Rate / 2);
		// The key-up: a click, and a burst of noise as the squelch opens.
		const int open = static_cast<int>(0.05F * c_Rate);
		for (int i = 0; i < open; ++i) {
			float t = static_cast<float>(i) / c_Rate;
			float click = i < 40 ? 0.9F * (1.0F - static_cast<float>(i) / 40.0F) * (i % 2 ? -1.0F : 1.0F) : 0.0F;
			keyed.push_back((click + noise(random) * 0.45F * std::exp(-t * 60.0F)) * strength);
		}
		keyed.insert(keyed.end(), samples.begin(), samples.end());
		// A roger beep.
		if (rogerBeep) {
			const int gap = static_cast<int>(0.03F * c_Rate);
			keyed.insert(keyed.end(), gap, 0.0F);
			const int beep = static_cast<int>(0.075F * c_Rate);
			for (int i = 0; i < beep; ++i) {
				float edge = std::min(1.0F, std::min(static_cast<float>(i), static_cast<float>(beep - i)) / 60.0F);
				keyed.push_back(0.45F * edge * std::sin(2.0F * c_Pi * 1350.0F * static_cast<float>(i) / c_Rate));
			}
		}
		// The squelch tail: "kssht".
		const int gap = static_cast<int>(0.025F * c_Rate);
		keyed.insert(keyed.end(), gap, 0.0F);
		const int tail = static_cast<int>(0.16F * c_Rate);
		for (int i = 0; i < tail; ++i) {
			float t = static_cast<float>(i) / c_Rate;
			keyed.push_back(noise(random) * 0.55F * strength * std::exp(-t * 18.0F) * std::min(1.0F, t * 400.0F));
		}
		// Static under the whole transmission, with now and then a crackle.
		for (float& sample: keyed) {
			sample += noise(random) * 0.02F * amount;
			if (chance(random) < 0.0005F * amount) {
				sample += noise(random) * 0.4F;
			}
		}
		// Driven (an overloaded little speaker), then band-limited, narrower the more radio: after the drive, so the band also takes off
		// the drive's harsh top.
		const float drive = 1.0F + 3.0F * amount;
		const float driveScale = 1.0F / std::tanh(drive);
		Biquad highA, highB, lowA, lowB;
		highA.Set(true, 90.0F + 330.0F * strength);
		highB = highA;
		lowA.Set(false, 7500.0F - 4600.0F * strength);
		lowB = lowA;
		for (float& sample: keyed) {
			sample = std::tanh(sample * drive) * driveScale;
			sample = lowB.Run(lowA.Run(highB.Run(highA.Run(sample))));
		}
		samples.swap(keyed);
	}

	/// What a unit says, as 16-bit samples at the voices' rate.
	std::vector<int16_t> Render(const Actor& actor, const std::string& text, unsigned tones) {
		const Voice& voice = VoiceOf(actor);
		const ToneStyle style = StyleOf(tones);
		// The unit's own voice: its pitch, throat and pace, from its ID, so it sounds the same all game.
		unsigned long long id = static_cast<unsigned long long>(actor.GetUniqueID()) * 0x9E3779B97F4A7C15ULL;
		id ^= id >> 31;
		auto spread = [&id](int shift) { return static_cast<float>((id >> shift) % 2001) / 1000.0F - 1.0F; };
		const float pitch = voice.Pitch * (1.0F + voice.PitchSpread * spread(3));
		const float formant = voice.Formant * (1.0F + 0.06F * spread(17));
		const float speed = voice.Speed * style.Speed * (1.0F + 0.08F * spread(29));
		const unsigned seed = static_cast<unsigned>(std::hash<std::string>()(text)) ^ static_cast<unsigned>(id);

		std::vector<float> samples = voice.Words && UnitSpeechVoice::SpeaksWords() ? Words(text, voice, pitch, speed) : Babble(text, voice, style, pitch, formant, speed, seed);
		std::vector<int16_t> pcm;
		if (samples.empty()) {
			return pcm;
		}
		Machine(samples, voice);
		Level(samples);
		Radio(samples, static_cast<float>(UnitSpeechVoice::GetRadio()) / 100.0F * voice.Radio * style.Radio, style.RogerBeep, seed);
		Normalize(samples, 0.85F);
		pcm.reserve(samples.size());
		for (float sample: samples) {
			pcm.push_back(static_cast<int16_t>(std::clamp(sample, -1.0F, 1.0F) * 32767.0F));
		}
		return pcm;
	}

	/// Lets go of a playing voice: stops it if it still plays, releases its sound.
	void Release(Playing& playing, bool stop) {
		if (stop && playing.Channel) {
			playing.Channel->stop();
		}
		if (playing.Sound) {
			playing.Sound->release();
		}
		playing = {};
	}

	/// How loud and where a unit is heard: on the screen that shows it closest to its middle, panned to its side of that screen. 0 for not
	/// heard (on no screen, or a side the screen's player doesn't hear).
	float Audibility(const Actor& actor, float& pan) {
		const Activity* activity = g_ActivityMan.GetActivity();
		float best = 0.0F;
		for (int screenIndex = 0; screenIndex < g_FrameMan.GetScreenCount(); ++screenIndex) {
			const int screen = g_FrameMan.ShownScreen(screenIndex);
			const int player = activity->PlayerOfScreen(screen);
			if (player < 0) {
				continue;
			}
			const int viewingTeam = activity->GetTeamOfPlayer(player);
			const bool enemy = viewingTeam != actor.GetTeam() && viewingTeam != Activity::NoTeam;
			if (enemy && (!UnitSpeechVoice::HearsEnemies() || !UnitSpeech::ShowsEnemies() || g_SceneMan.IsUnseen(actor.GetPos().GetFloorIntX(), actor.GetPos().GetFloorIntY(), viewingTeam))) {
				continue;
			}
			const float halfWidth = static_cast<float>(g_FrameMan.GetPlayerFrameBufferWidth(screen)) / 2.0F;
			const float halfHeight = static_cast<float>(g_FrameMan.GetPlayerFrameBufferHeight(screen)) / 2.0F;
			const Vector middle = g_CameraMan.GetOffset(screen) + Vector(halfWidth, halfHeight);
			const Vector away = g_SceneMan.ShortestDistance(middle, actor.GetPos(), true);
			if (std::abs(away.m_X) > halfWidth + 40.0F || std::abs(away.m_Y) > halfHeight + 40.0F) {
				continue;
			}
			// (Off the middle a little quieter; the other side's a little quieter still.)
			const float off = std::min(1.0F, away.GetMagnitude() / std::max(halfWidth, 1.0F));
			const float loudness = (1.0F - 0.35F * off) * (enemy ? 0.8F : 1.0F);
			if (loudness > best) {
				best = loudness;
				pan = std::clamp(away.m_X / std::max(halfWidth, 1.0F), -1.0F, 1.0F) * 0.6F * g_AudioMan.GetSoundPanningEffectStrength();
			}
		}
		return best;
	}

	/// Plays a voice for a unit, cutting off its last one if that still plays.
	void Play(const Actor& actor, const std::vector<int16_t>& pcm, float volume, float pan) {
		FMOD::System* system = g_AudioMan.GetAudioSystem();
		FMOD::ChannelGroup* group = g_AudioMan.GetUnitSpeechChannelGroup();
		if (!system || !group || pcm.empty()) {
			return;
		}
		for (Playing& playing: s_Playing) {
			if (playing.ActorID == actor.GetUniqueID()) {
				Release(playing, true);
			}
		}
		s_Playing.erase(std::remove_if(s_Playing.begin(), s_Playing.end(), [](const Playing& playing) { return !playing.Sound; }), s_Playing.end());
		if (static_cast<int>(s_Playing.size()) >= c_MaxVoices) {
			return;
		}
		FMOD_CREATESOUNDEXINFO info{};
		info.cbsize = sizeof(info);
		info.length = static_cast<unsigned int>(pcm.size() * sizeof(int16_t));
		info.numchannels = 1;
		info.defaultfrequency = c_Rate;
		info.format = FMOD_SOUND_FORMAT_PCM16;
		Playing playing;
		playing.ActorID = actor.GetUniqueID();
		FMOD_RESULT result = system->createSound(reinterpret_cast<const char*>(pcm.data()), FMOD_OPENMEMORY | FMOD_OPENRAW | FMOD_CREATESAMPLE | FMOD_2D | FMOD_LOOP_OFF, &info, &playing.Sound);
		result = (result == FMOD_OK) ? system->playSound(playing.Sound, group, true, &playing.Channel) : result;
		result = (result == FMOD_OK) ? playing.Channel->setVolume(volume) : result;
		result = (result == FMOD_OK) ? playing.Channel->setPan(pan) : result;
		// (Behind the battle's own sounds when channels run short.)
		result = (result == FMOD_OK) ? playing.Channel->setPriority(200) : result;
		result = (result == FMOD_OK) ? playing.Channel->setPaused(false) : result;
		if (result != FMOD_OK) {
			g_ConsoleMan.PrintString("ERROR: Could not play a unit's voice: " + std::string(FMOD_ErrorString(result)));
			Release(playing, true);
			return;
		}
		s_PlayingSystem = system;
		s_Playing.push_back(playing);
	}
} // namespace

void UnitSpeechVoice::SetEnabled(bool enabled) {
	s_Enabled = enabled;
}

void UnitSpeechVoice::SetVolume(int percent) {
	s_VolumePercent = std::clamp(percent, 0, 100);
}

void UnitSpeechVoice::SetRadio(int percent) {
	s_RadioPercent = std::clamp(percent, 0, 100);
}

void UnitSpeechVoice::Reload() {
	LoadAll();
}

std::string UnitSpeechVoice::GetVoiceName(const Actor& actor) {
	return VoiceOf(actor).Name;
}

void UnitSpeechVoice::Update() {
	// The voices that ended let go of; all of them, if the audio they were made with is gone.
	if (!g_AudioMan.IsAudioEnabled() || g_AudioMan.GetAudioSystem() != s_PlayingSystem) {
		s_Playing.clear();
		s_PlayingSystem = g_AudioMan.IsAudioEnabled() ? g_AudioMan.GetAudioSystem() : nullptr;
	}
	for (Playing& playing: s_Playing) {
		bool isPlaying = false;
		if (playing.Channel->isPlaying(&isPlaying) != FMOD_OK || !isPlaying) {
			Release(playing, false);
		}
	}
	s_Playing.erase(std::remove_if(s_Playing.begin(), s_Playing.end(), [](const Playing& playing) { return !playing.Sound; }), s_Playing.end());

	if (!s_Enabled || s_VolumePercent <= 0 || !UnitSpeech::IsEnabled() || !g_AudioMan.IsAudioEnabled() || !g_ActivityMan.GetActivity() || !g_SceneMan.GetScene()) {
		return;
	}
	const long long now = g_TimerMan.GetSimTimeMS();
	for (Actor* actor: g_MovableMan.GetActorList()) {
		UnitSpeech::State& state = actor->GetSpeech();
		if (state.Text.empty() || state.StartMS == state.VoicedMS) {
			continue;
		}
		state.VoicedMS = state.StartMS;
		if (now < state.StartMS || now - state.StartMS > c_FreshMS || actor->GetStatus() >= Actor::DYING || actor->GetTeam() < 0) {
			continue;
		}
		float pan = 0.0F;
		const float loudness = Audibility(*actor, pan);
		if (loudness <= 0.0F) {
			continue;
		}
		const Voice& voice = VoiceOf(*actor);
		Play(*actor, Render(*actor, state.Text, state.Tones), std::min(1.0F, static_cast<float>(s_VolumePercent) / 100.0F * loudness * voice.Volume), pan);
	}
}
