// Voice presets, the end-to-end text-to-audio path, and output format helpers.
#include "sam_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define SAM_STRICMP _stricmp
#else
#include <strings.h>
#define SAM_STRICMP strcasecmp
#endif

namespace sam {

// The six voices the engine ships with. Values match VOICE_PRESETS in the
// original sam.py; inflection and singmode take the engine defaults.
const VoicePreset VOICES[VOICE_COUNT] = {
    {"sam",               "Sam",               {64, 72,  128, 128, 50, false}},
    {"elf",               "Elf",               {64, 72,  110, 160, 50, false}},
    {"little_robot",      "Little Robot",      {60, 92,  190, 190, 50, false}},
    {"stuffy_guy",        "Stuffy Guy",        {72, 82,  110, 105, 50, false}},
    {"little_old_lady",   "Little Old Lady",   {32, 72,  145, 145, 50, false}},
    {"extra_terrestrial", "Extra Terrestrial", {64, 100, 150, 200, 50, false}},
};

int find_voice(const char* id)
{
    if (!id) {
        return -1;
    }
    for (int i = 0; i < VOICE_COUNT; ++i) {
        if (SAM_STRICMP(VOICES[i].id, id) == 0) {
            return i;
        }
    }
    // Also accept the display name, so a SAPI token name resolves.
    for (int i = 0; i < VOICE_COUNT; ++i) {
        if (SAM_STRICMP(VOICES[i].display, id) == 0) {
            return i;
        }
    }
    return -1;
}

std::vector<std::uint8_t> text_to_audio(const std::string& text,
                                        const VoiceParams& params,
                                        bool phonetic)
{
    std::string phoneme_string;
    if (phonetic) {
        phoneme_string.reserve(text.size());
        for (char c : text) {
            phoneme_string += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    } else {
        phoneme_string = text_to_phonemes(text);
        if (phoneme_string.empty()) {
            return {};
        }
    }

    std::vector<Phoneme> phonemes;
    if (!parse_phonemes(phoneme_string, phonemes) || phonemes.empty()) {
        return {};
    }
    return render(phonemes, params);
}

std::vector<std::uint8_t> audio_to_wav(const std::vector<std::uint8_t>& audio)
{
    if (audio.empty()) {
        return {};
    }

    const std::uint32_t data_size = static_cast<std::uint32_t>(audio.size());
    const std::uint32_t sample_rate = SAMPLE_RATE;
    const std::uint16_t channels = 1;
    const std::uint16_t bits = 8;
    const std::uint16_t block_align = static_cast<std::uint16_t>(channels * bits / 8);
    const std::uint32_t byte_rate = sample_rate * block_align;

    std::vector<std::uint8_t> wav;
    wav.reserve(44 + audio.size());

    auto put32 = [&wav](std::uint32_t v) {
        wav.push_back(static_cast<std::uint8_t>(v & 0xFF));
        wav.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        wav.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        wav.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    };
    auto put16 = [&wav](std::uint16_t v) {
        wav.push_back(static_cast<std::uint8_t>(v & 0xFF));
        wav.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    };
    auto puttag = [&wav](const char* s) {
        for (int i = 0; i < 4; ++i) {
            wav.push_back(static_cast<std::uint8_t>(s[i]));
        }
    };

    puttag("RIFF");
    put32(data_size + 36);
    puttag("WAVE");
    puttag("fmt ");
    put32(16);
    put16(1);  // PCM
    put16(channels);
    put32(sample_rate);
    put32(byte_rate);
    put16(block_align);
    put16(bits);
    puttag("data");
    put32(data_size);

    wav.insert(wav.end(), audio.begin(), audio.end());
    return wav;
}

std::vector<std::int16_t> to_pcm16(const std::vector<std::uint8_t>& audio,
                                   int volume, bool fade)
{
    std::vector<std::int16_t> out;
    if (audio.empty()) {
        return out;
    }

    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    out.resize(audio.size());
    for (std::size_t i = 0; i < audio.size(); ++i) {
        // 8-bit unsigned centred on 128 -> signed 16-bit.
        const int signed_sample = static_cast<int>(audio[i]) - 128;
        int v = signed_sample * 256 * volume / 100;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out[i] = static_cast<std::int16_t>(v);
    }

    // A 5 ms ramp at each end keeps concatenated chunks from clicking.
    if (fade && out.size() >= 20) {
        std::size_t n = static_cast<std::size_t>(SAMPLE_RATE * 5 / 1000);
        n = std::min(n, out.size() / 4);
        for (std::size_t i = 0; i < n; ++i) {
            const int scale_num = static_cast<int>(i);
            const int scale_den = static_cast<int>(n);
            out[i] = static_cast<std::int16_t>(out[i] * scale_num / scale_den);
            const std::size_t j = out.size() - 1 - i;
            out[j] = static_cast<std::int16_t>(out[j] * scale_num / scale_den);
        }
    }
    return out;
}

}  // namespace sam
