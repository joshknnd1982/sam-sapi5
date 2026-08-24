// SAM (Software Automatic Mouth) speech engine -- C++ port of the Python port
// of the 1982 original.
//
// The engine is self-contained: it touches no registry, no COM, and no SAPI.
// The only external file it can use is cmudict.txt, and it degrades to the
// rule-based reciter when that file is absent.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sam {

// Native output format. SAM renders 8-bit unsigned PCM at 22050 Hz mono.
inline constexpr int SAMPLE_RATE = 22050;

// ---------------------------------------------------------------------------
// Voice parameters
// ---------------------------------------------------------------------------

struct VoiceParams
{
    int pitch = 64;        // 0-255, lower is higher-pitched
    int speed = 72;        // 0-255, lower is faster
    int mouth = 128;       // 0-255, F1 formant scaling
    int throat = 128;      // 0-255, F2 formant scaling
    int inflection = 50;   // 0-100, 0 = monotone, 50 = normal, 100 = dramatic
    bool singmode = false; // hold pitch steady across vowels
};

// The six presets the engine ships with. `id` is the stable machine name used
// in settings files and on the SAPI token; `display` is what a user sees.
struct VoicePreset
{
    const char* id;
    const char* display;
    VoiceParams params;
};

inline constexpr int VOICE_COUNT = 6;
extern const VoicePreset VOICES[VOICE_COUNT];

// Returns the index of `id` (case-insensitive), or -1.
int find_voice(const char* id);

// ---------------------------------------------------------------------------
// Pipeline stages
// ---------------------------------------------------------------------------

// One parsed phoneme: index into the phoneme table, frame count, stress level.
struct Phoneme
{
    int index;
    int length;
    int stress;
};

// Expands digit runs into English words ("60" -> "sixty").
std::string expand_numbers(const std::string& text);

// Rule-based letter-to-sound only, no dictionary.
// Returns false via `ok` if the text contains something unpronounceable.
std::string rule_based_phonemes(const std::string& text, bool* ok = nullptr);

// Parses a SAM phoneme string into the renderer's frame list.
// Returns false if the string cannot be parsed.
bool parse_phonemes(const std::string& phonemes, std::vector<Phoneme>& out);

// Renders parsed phonemes to 8-bit unsigned PCM at 22050 Hz.
std::vector<std::uint8_t> render(const std::vector<Phoneme>& phonemes,
                                 const VoiceParams& params);

// ---------------------------------------------------------------------------
// Dictionary
// ---------------------------------------------------------------------------

// Loads CMUdict from `path`. Safe to call repeatedly; only the first call for a
// given path does work. Returns the number of entries loaded (0 on failure).
// Without a dictionary the engine still speaks, using the reciter rules alone.
std::size_t load_dictionary(const std::string& path);
bool dictionary_loaded();
std::size_t dictionary_size();

// Looks a word up and returns its SAM phoneme string, or "" if not present.
std::string dictionary_lookup(const std::string& word);

// ---------------------------------------------------------------------------
// Convenience
// ---------------------------------------------------------------------------

// Full text-to-phoneme conversion: dictionary first, reciter rules as fallback.
std::string text_to_phonemes(const std::string& text);

// text (or a phoneme string when `phonetic`) -> 8-bit unsigned PCM.
std::vector<std::uint8_t> text_to_audio(const std::string& text,
                                        const VoiceParams& params,
                                        bool phonetic = false);

// Wraps 8-bit PCM in a RIFF/WAVE container.
std::vector<std::uint8_t> audio_to_wav(const std::vector<std::uint8_t>& audio);

// Converts SAM's native 8-bit unsigned PCM to signed 16-bit, applying `volume`
// (0-100) and a short fade at each end to suppress boundary clicks.
std::vector<std::int16_t> to_pcm16(const std::vector<std::uint8_t>& audio,
                                   int volume = 100,
                                   bool fade = true);

}  // namespace sam
