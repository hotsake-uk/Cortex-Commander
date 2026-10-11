#ifndef RSYNTH_TTS_H
#define RSYNTH_TTS_H

namespace RSynth {
	class darray;

	/// Turns English text into phonemes, by the NRL letter-to-sound rules.
	int xlate_string(const char* string, darray* phone);
} // namespace RSynth

#endif
