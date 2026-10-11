#pragma once

#include <string>

namespace RTE {
	class Actor;

	/// Unit speech voices (US-3): a sound for each line a unit says (UnitSpeech), made in the engine from the line's own words, so every line has
	/// one (a mod's, a name filled in) with no recordings. Most units babble: a syllable of mumbled vowel for each one in the line, at the unit's
	/// own pitch, with its rhythm and its rise and fall from the words and the punctuation. Robots speak the actual words, in a synthesised
	/// voice (System/RSynth, public domain). Both go through a radio: band-limited, driven, with static and a squelch at each end, more so on
	/// Serious lines, which also end on a roger beep.
	/// What each kind of unit sounds like is data: Base.rte/Voices.ini, then each other module's Voices.ini in load order.
	/// Cosmetic only: made and played on the main thread, after the sim update, from what the units' speech states say; nothing here touches the
	/// simulation or its random numbers.
	class UnitSpeechVoice {

	public:
#pragma region Settings
		/// Gets whether lines are voiced at all.
		static bool IsEnabled() { return s_Enabled; }

		/// Sets whether lines are voiced at all.
		static void SetEnabled(bool enabled);

		/// Gets the voices' volume, 0 to 100 percent (under the sound effects' own).
		static int GetVolume() { return s_VolumePercent; }

		/// Sets the voices' volume, 0 to 100 percent.
		static void SetVolume(int percent);

		/// Gets how much radio there is on the voices, 0 (a clean voice) to 100 (a crackly field radio).
		static int GetRadio() { return s_RadioPercent; }

		/// Sets how much radio there is on the voices, 0 to 100.
		static void SetRadio(int percent);

		/// Gets whether the other sides' units are heard too (where their lines are shown: UnitSpeech::ShowsEnemies and seen by the viewing side).
		static bool HearsEnemies() { return s_HearsEnemies; }

		/// Sets whether the other sides' units are heard too.
		static void SetHearsEnemies(bool hear) { s_HearsEnemies = hear; }

		/// Gets whether voices with "Words = 1" (robots) speak the actual words; off, they babble like everyone else.
		static bool SpeaksWords() { return s_SpeaksWords; }

		/// Sets whether voices with "Words = 1" speak the actual words.
		static void SetSpeaksWords(bool words) { s_SpeaksWords = words; }
#pragma endregion

#pragma region Voicing
		/// Voices the lines units have just started saying, for the screens that show them, and lets go of the sounds that have finished.
		/// Main thread, once per sim update, after the units have updated.
		static void Update();

		/// Reads the Voices.ini files again (for modders trying voices out). Main thread.
		static void Reload();

		/// Gets the name of the voice an actor speaks with (Voices.ini), for the debug views.
		static std::string GetVoiceName(const Actor& actor);
#pragma endregion

	private:
		static bool s_Enabled; //!< Whether lines are voiced at all.
		static int s_VolumePercent; //!< The voices' volume, 0 to 100.
		static int s_RadioPercent; //!< How much radio, 0 to 100.
		static bool s_HearsEnemies; //!< Whether the other sides' units are heard too.
		static bool s_SpeaksWords; //!< Whether robots speak the actual words.
	};
} // namespace RTE
