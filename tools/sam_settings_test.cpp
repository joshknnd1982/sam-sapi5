// Round-trip test for the settings store: everything written must come back
// unchanged, out-of-range values must be clamped, and a missing file must fall
// back to the defaults rather than failing.
#include <cstdio>
#include <string>

#include "../src/engine/sam_engine.hpp"
#include "../src/sapi/sam_paths.hpp"
#include "../src/sapi/sam_settings.hpp"

using namespace SamVoice;

namespace {

int g_failures = 0;

void check(bool condition, const char* what)
{
    if (!condition) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

void check_eq(int got, int want, const char* what)
{
    if (got != want) {
        std::printf("  FAIL: %s (got %d, want %d)\n", what, got, want);
        ++g_failures;
    }
}

}  // namespace

int main()
{
    paths::set_module(nullptr);

    const std::wstring path = settings::file_path();
    if (path.empty()) {
        std::printf("FAIL: no settings path available\n");
        return 1;
    }
    std::printf("settings file: %ls\n\n", path.c_str());

    // Preserve whatever the user already has.
    std::string saved;
    if (std::FILE* f = _wfopen(path.c_str(), L"rb")) {
        char buf[4096];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
            saved.append(buf, n);
        }
        std::fclose(f);
    }
    const bool had_file = !saved.empty();

    // --- defaults when no file exists ------------------------------------
    std::printf("no file -> defaults\n");
    DeleteFileW(path.c_str());
    {
        settings::Settings s = settings::load();
        check_eq(s.volume, 100, "default volume");
        check(s.expand_numbers, "default expand_numbers");
        for (int i = 0; i < sam::VOICE_COUNT; ++i) {
            check_eq(s.voice(i).pitch, sam::VOICES[i].params.pitch,
                     "default pitch matches the preset");
            check_eq(s.voice(i).speed, sam::VOICES[i].params.speed,
                     "default speed matches the preset");
        }
    }

    // --- every field survives a save/load round trip ---------------------
    std::printf("round trip\n");
    {
        settings::Settings s;
        s.reset();
        s.default_voice = "little_old_lady";
        s.volume = 73;
        s.expand_numbers = false;
        s.rate_min_speed = 25;
        s.rate_max_speed = 190;
        s.log_level = log::Level::Debug;

        // Give every voice a distinct set of values, so a mix-up shows up.
        for (int i = 0; i < sam::VOICE_COUNT; ++i) {
            settings::VoiceSettings& v = s.voice(i);
            v.pitch = 10 + i * 7;
            v.speed = 30 + i * 11;
            v.mouth = 40 + i * 13;
            v.throat = 50 + i * 17;
            v.inflection = 5 + i * 9;
            v.singmode = (i % 2) == 1;
        }

        check(settings::save(s), "save succeeded");

        const settings::Settings r = settings::load();
        check(r.default_voice == "little_old_lady", "default_voice round trip");
        check_eq(r.volume, 73, "volume round trip");
        check(!r.expand_numbers, "expand_numbers round trip");
        check_eq(r.rate_min_speed, 25, "rate_min_speed round trip");
        check_eq(r.rate_max_speed, 190, "rate_max_speed round trip");
        check_eq(static_cast<int>(r.log_level), static_cast<int>(log::Level::Debug),
                 "log_level round trip");

        for (int i = 0; i < sam::VOICE_COUNT; ++i) {
            const settings::VoiceSettings& v = r.voice(i);
            check_eq(v.pitch, 10 + i * 7, "pitch round trip");
            check_eq(v.speed, 30 + i * 11, "speed round trip");
            check_eq(v.mouth, 40 + i * 13, "mouth round trip");
            check_eq(v.throat, 50 + i * 17, "throat round trip");
            check_eq(v.inflection, 5 + i * 9, "inflection round trip");
            check(v.singmode == ((i % 2) == 1), "singmode round trip");
        }

        // And the parameters handed to the engine reflect the saved voice.
        const sam::VoiceParams p = r.params_for(2);
        check_eq(p.pitch, 10 + 2 * 7, "params_for pitch");
        check_eq(p.mouth, 40 + 2 * 13, "params_for mouth");
    }

    // --- out-of-range values are clamped, not accepted -------------------
    std::printf("clamping\n");
    {
        settings::Settings s;
        s.reset();
        s.volume = 500;
        s.voice(0).pitch = 9999;
        s.voice(0).speed = 0;          // would divide by zero in the renderer
        s.voice(0).inflection = -20;
        s.default_voice = "no_such_voice";
        check(settings::save(s), "save with out-of-range values succeeded");

        const settings::Settings r = settings::load();
        check_eq(r.volume, 100, "volume clamped to 100");
        check_eq(r.voice(0).pitch, 255, "pitch clamped to 255");
        check_eq(r.voice(0).speed, 1, "speed clamped to at least 1");
        check_eq(r.voice(0).inflection, 0, "inflection clamped to 0");
        check(r.default_voice == "sam", "unknown voice falls back to sam");
    }

    // --- a corrupt file must not crash or wipe the defaults --------------
    std::printf("corrupt file\n");
    {
        if (std::FILE* f = _wfopen(path.c_str(), L"wb")) {
            std::fprintf(f, "this is not an ini file\n[unclosed\nvolume=abc\n"
                            "[voice.sam]\npitch=notanumber\n");
            std::fclose(f);
        }
        const settings::Settings r = settings::load();
        check_eq(r.volume, 100, "garbage volume falls back to the default");
        check_eq(r.voice(0).pitch, 64, "garbage pitch falls back to the default");
    }

    // Restore what was there before.
    if (had_file) {
        if (std::FILE* f = _wfopen(path.c_str(), L"wb")) {
            std::fwrite(saved.data(), 1, saved.size(), f);
            std::fclose(f);
        }
        std::printf("\nrestored the existing settings file\n");
    } else {
        DeleteFileW(path.c_str());
        std::printf("\nremoved the temporary settings file\n");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
