# RSynth

A small formant speech synthesiser (Klatt) with the NRL letter-to-sound rules, used by unit speech voices
(`Source/Managers/UnitSpeechVoice.cpp`) for robots that speak the actual words.

Taken from SoLoud's speech module (`src/audiosource/speech`, by Jari Komppa), itself a cut-down rsynth by Nick Ing-Simmons. It is public
domain (CC0 where that isn't recognised); see `LICENSE.txt`. Changed here: everything is wrapped in `namespace RSynth`, `Elements.def` is `Elements.inl` (the repo ignores .def files), and `tts.h` declares
`xlate_string` itself. SoLoud's own interface files (zlib licence) aren't used.
