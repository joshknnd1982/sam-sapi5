// Command-line renderer, used to verify the C++ engine against the Python
// reference and to produce the sample WAVs.
//
//   sam_render --voice sam --text "hello" --out out.wav
//   sam_render --pitch 64 --speed 72 --mouth 128 --throat 128 --text "hi" --out o.wav
#include "../src/engine/sam_engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void usage()
{
    std::fprintf(stderr,
        "usage: sam_render [options] --text TEXT --out FILE.wav\n"
        "\n"
        "  --voice ID       one of: ");
    for (int i = 0; i < sam::VOICE_COUNT; ++i) {
        std::fprintf(stderr, "%s%s", i ? ", " : "", sam::VOICES[i].id);
    }
    std::fprintf(stderr,
        "\n"
        "  --pitch N        0-255   (overrides the preset)\n"
        "  --speed N        0-255\n"
        "  --mouth N        0-255\n"
        "  --throat N       0-255\n"
        "  --inflection N   0-100\n"
        "  --singmode       hold pitch steady\n"
        "  --phonetic       treat TEXT as a SAM phoneme string\n"
        "  --no-numbers     skip English number expansion\n"
        "  --dict PATH      cmudict.txt (default: cmudict.txt next to the exe)\n"
        "  --phonemes       print the phoneme string and exit\n");
}

bool arg_int(int argc, char** argv, int& i, int& out)
{
    if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s needs a value\n", argv[i]);
        return false;
    }
    out = std::atoi(argv[++i]);
    return true;
}

std::string exe_dir(const char* argv0)
{
    std::string p(argv0);
    const std::size_t slash = p.find_last_of("\\/");
    return (slash == std::string::npos) ? std::string(".") : p.substr(0, slash);
}

}  // namespace

int main(int argc, char** argv)
{
    sam::VoiceParams params;
    std::string text, out_path, dict_path, voice_id;
    bool phonetic = false;
    bool expand = true;
    bool print_phonemes = false;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--text") == 0 && i + 1 < argc) {
            text = argv[++i];
        } else if (std::strcmp(a, "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (std::strcmp(a, "--dict") == 0 && i + 1 < argc) {
            dict_path = argv[++i];
        } else if (std::strcmp(a, "--voice") == 0 && i + 1 < argc) {
            voice_id = argv[++i];
        } else if (std::strcmp(a, "--pitch") == 0) {
            if (!arg_int(argc, argv, i, params.pitch)) return 2;
        } else if (std::strcmp(a, "--speed") == 0) {
            if (!arg_int(argc, argv, i, params.speed)) return 2;
        } else if (std::strcmp(a, "--mouth") == 0) {
            if (!arg_int(argc, argv, i, params.mouth)) return 2;
        } else if (std::strcmp(a, "--throat") == 0) {
            if (!arg_int(argc, argv, i, params.throat)) return 2;
        } else if (std::strcmp(a, "--inflection") == 0) {
            if (!arg_int(argc, argv, i, params.inflection)) return 2;
        } else if (std::strcmp(a, "--singmode") == 0) {
            params.singmode = true;
        } else if (std::strcmp(a, "--phonetic") == 0) {
            phonetic = true;
        } else if (std::strcmp(a, "--no-numbers") == 0) {
            expand = false;
        } else if (std::strcmp(a, "--phonemes") == 0) {
            print_phonemes = true;
        } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "error: unknown option %s\n", a);
            usage();
            return 2;
        }
    }

    // A --voice sets the preset; explicit numeric options given after it win,
    // so apply the preset only where the caller left the defaults alone.
    if (!voice_id.empty()) {
        const int vi = sam::find_voice(voice_id.c_str());
        if (vi < 0) {
            std::fprintf(stderr, "error: unknown voice '%s'\n", voice_id.c_str());
            return 2;
        }
        const sam::VoiceParams preset = sam::VOICES[vi].params;
        const sam::VoiceParams def;
        if (params.pitch == def.pitch)   params.pitch = preset.pitch;
        if (params.speed == def.speed)   params.speed = preset.speed;
        if (params.mouth == def.mouth)   params.mouth = preset.mouth;
        if (params.throat == def.throat) params.throat = preset.throat;
    }

    if (text.empty()) {
        std::fprintf(stderr, "error: --text is required\n");
        usage();
        return 2;
    }

    if (dict_path.empty()) {
        dict_path = exe_dir(argv[0]) + "\\cmudict.txt";
    }
    const std::size_t words = sam::load_dictionary(dict_path);
    std::fprintf(stderr, "dictionary: %s (%zu entries)\n",
                 words ? dict_path.c_str() : "not loaded, using reciter rules only", words);

    std::string input = text;
    if (!phonetic && expand) {
        input = sam::expand_numbers(input);
    }

    if (print_phonemes) {
        const std::string ph = phonetic ? input : sam::text_to_phonemes(input);
        std::printf("%s\n", ph.c_str());
        return 0;
    }

    const std::vector<std::uint8_t> audio = sam::text_to_audio(input, params, phonetic);
    if (audio.empty()) {
        std::fprintf(stderr, "error: nothing rendered\n");
        return 1;
    }

    std::fprintf(stderr,
                 "rendered %zu samples (%.2f s) pitch=%d speed=%d mouth=%d throat=%d "
                 "inflection=%d singmode=%d\n",
                 audio.size(), static_cast<double>(audio.size()) / sam::SAMPLE_RATE,
                 params.pitch, params.speed, params.mouth, params.throat,
                 params.inflection, params.singmode ? 1 : 0);

    if (out_path.empty()) {
        return 0;
    }

    const std::vector<std::uint8_t> wav = sam::audio_to_wav(audio);
    std::FILE* f = std::fopen(out_path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "error: cannot write %s\n", out_path.c_str());
        return 1;
    }
    std::fwrite(wav.data(), 1, wav.size(), f);
    std::fclose(f);
    std::fprintf(stderr, "wrote %s\n", out_path.c_str());
    return 0;
}
