#include "sam_settings.hpp"
#include "sam_paths.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace SamVoice {
namespace settings {
namespace {

int clamp_int(int v, int lo, int hi)
{
    return std::max(lo, std::min(hi, v));
}

// GetPrivateProfile* would work, but it is registry-adjacent, caches
// aggressively, and mangles non-ASCII. A tiny reader keeps the format ours.
struct Ini
{
    // section -> key -> value, all lower-cased for lookup.
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> data;

    static std::string lower(std::string s)
    {
        for (char& c : s) {
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    }

    static std::string trim(const std::string& s)
    {
        const std::size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) {
            return std::string();
        }
        const std::size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    void set(const std::string& section, const std::string& key, const std::string& value)
    {
        const std::string s = lower(section);
        const std::string k = lower(key);
        for (auto& sec : data) {
            if (sec.first == s) {
                for (auto& kv : sec.second) {
                    if (kv.first == k) {
                        kv.second = value;
                        return;
                    }
                }
                sec.second.emplace_back(k, value);
                return;
            }
        }
        data.push_back({s, {{k, value}}});
    }

    const std::string* get(const std::string& section, const std::string& key) const
    {
        const std::string s = lower(section);
        const std::string k = lower(key);
        for (const auto& sec : data) {
            if (sec.first != s) {
                continue;
            }
            for (const auto& kv : sec.second) {
                if (kv.first == k) {
                    return &kv.second;
                }
            }
        }
        return nullptr;
    }

    int get_int(const std::string& section, const std::string& key, int fallback) const
    {
        const std::string* v = get(section, key);
        if (!v || v->empty()) {
            return fallback;
        }
        try {
            return std::stoi(*v);
        } catch (...) {
            return fallback;
        }
    }

    bool get_bool(const std::string& section, const std::string& key, bool fallback) const
    {
        const std::string* v = get(section, key);
        if (!v || v->empty()) {
            return fallback;
        }
        const std::string s = lower(trim(*v));
        if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
        if (s == "0" || s == "false" || s == "no" || s == "off") return false;
        return fallback;
    }

    std::string get_str(const std::string& section, const std::string& key,
                        const std::string& fallback) const
    {
        const std::string* v = get(section, key);
        return v ? trim(*v) : fallback;
    }

    bool read(const std::wstring& path)
    {
        std::FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
            return false;
        }
        std::string section;
        std::vector<char> buf(1024);
        while (std::fgets(buf.data(), static_cast<int>(buf.size()), f)) {
            std::string line = trim(buf.data());
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }
            if (line.front() == '[' && line.back() == ']') {
                section = trim(line.substr(1, line.size() - 2));
                continue;
            }
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            set(section, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
        }
        std::fclose(f);
        return true;
    }
};

std::string voice_section(int index)
{
    return std::string("voice.") + sam::VOICES[index].id;
}

}  // namespace

void Settings::reset()
{
    *this = Settings();
    for (int i = 0; i < sam::VOICE_COUNT; ++i) {
        const sam::VoiceParams& p = sam::VOICES[i].params;
        voices[i] = VoiceSettings{p.pitch, p.speed, p.mouth, p.throat,
                                  p.inflection, p.singmode};
    }
}

const VoiceSettings& Settings::voice(int index) const
{
    if (index < 0 || index >= sam::VOICE_COUNT) {
        index = 0;
    }
    return voices[index];
}

VoiceSettings& Settings::voice(int index)
{
    if (index < 0 || index >= sam::VOICE_COUNT) {
        index = 0;
    }
    return voices[index];
}

sam::VoiceParams Settings::params_for(int voice_index) const
{
    const VoiceSettings& v = voice(voice_index);
    sam::VoiceParams p;
    p.pitch = v.pitch;
    p.speed = v.speed;
    p.mouth = v.mouth;
    p.throat = v.throat;
    p.inflection = v.inflection;
    p.singmode = v.singmode;
    return p;
}

void clamp(Settings& s)
{
    s.volume = clamp_int(s.volume, 0, 100);
    s.rate_min_speed = clamp_int(s.rate_min_speed, 1, 255);
    s.rate_max_speed = clamp_int(s.rate_max_speed, 1, 255);
    if (s.rate_min_speed > s.rate_max_speed) {
        std::swap(s.rate_min_speed, s.rate_max_speed);
    }
    if (sam::find_voice(s.default_voice.c_str()) < 0) {
        s.default_voice = sam::VOICES[0].id;
    }
    const int lvl = clamp_int(static_cast<int>(s.log_level), 0, 5);
    s.log_level = static_cast<log::Level>(lvl);

    for (int i = 0; i < sam::VOICE_COUNT; ++i) {
        VoiceSettings& v = s.voices[i];
        v.pitch = clamp_int(v.pitch, 0, 255);
        // Speed 0 makes the renderer divide by zero; the engine treats it as
        // the default, but keep users out of that corner entirely.
        v.speed = clamp_int(v.speed, 1, 255);
        v.mouth = clamp_int(v.mouth, 0, 255);
        v.throat = clamp_int(v.throat, 0, 255);
        v.inflection = clamp_int(v.inflection, 0, 100);
    }
}

std::wstring file_path()
{
    return paths::settings_path();
}

Settings load()
{
    Settings s;
    s.reset();

    const std::wstring path = file_path();
    if (path.empty()) {
        return s;
    }

    Ini ini;
    if (!ini.read(path)) {
        return s;  // no file yet: defaults
    }

    s.default_voice = ini.get_str("global", "voice", s.default_voice);
    s.volume = ini.get_int("global", "volume", s.volume);
    s.expand_numbers = ini.get_bool("global", "expand_numbers", s.expand_numbers);
    s.rate_min_speed = ini.get_int("global", "rate_min_speed", s.rate_min_speed);
    s.rate_max_speed = ini.get_int("global", "rate_max_speed", s.rate_max_speed);
    s.log_level = static_cast<log::Level>(
        ini.get_int("logging", "level", static_cast<int>(s.log_level)));

    for (int i = 0; i < sam::VOICE_COUNT; ++i) {
        const std::string sec = voice_section(i);
        VoiceSettings& v = s.voices[i];
        v.pitch = ini.get_int(sec, "pitch", v.pitch);
        v.speed = ini.get_int(sec, "speed", v.speed);
        v.mouth = ini.get_int(sec, "mouth", v.mouth);
        v.throat = ini.get_int(sec, "throat", v.throat);
        v.inflection = ini.get_int(sec, "inflection", v.inflection);
        v.singmode = ini.get_bool(sec, "singmode", v.singmode);
    }

    clamp(s);
    return s;
}

bool save(const Settings& input)
{
    Settings s = input;
    clamp(s);

    const std::wstring path = file_path();
    if (path.empty()) {
        return false;
    }
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        paths::ensure_directory(path.substr(0, slash));
    }

    // Write to a sibling temp file and replace, so a reader never sees a
    // half-written file.
    const std::wstring temp = path + L".tmp";
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, temp.c_str(), L"wb") != 0 || !f) {
        return false;
    }

    std::fprintf(f,
        "; SAM (Software Automatic Mouth) SAPI5 settings\n"
        "; Edited by the SAM Voice Settings utility. Changes take effect on the\n"
        "; next thing spoken -- no restart needed.\n"
        "\n"
        "[global]\n"
        "voice=%s\n"
        "volume=%d\n"
        "expand_numbers=%d\n"
        "rate_min_speed=%d\n"
        "rate_max_speed=%d\n"
        "\n"
        "[logging]\n"
        "; 0=off 1=error 2=warn 3=info 4=debug 5=trace\n"
        "level=%d\n",
        s.default_voice.c_str(), s.volume, s.expand_numbers ? 1 : 0,
        s.rate_min_speed, s.rate_max_speed, static_cast<int>(s.log_level));

    for (int i = 0; i < sam::VOICE_COUNT; ++i) {
        const VoiceSettings& v = s.voices[i];
        std::fprintf(f,
            "\n"
            "[%s]\n"
            "; %s\n"
            "pitch=%d\n"
            "speed=%d\n"
            "mouth=%d\n"
            "throat=%d\n"
            "inflection=%d\n"
            "singmode=%d\n",
            voice_section(i).c_str(), sam::VOICES[i].display,
            v.pitch, v.speed, v.mouth, v.throat, v.inflection, v.singmode ? 1 : 0);
    }

    std::fclose(f);

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

bool changed_since(FILETIME& last_write)
{
    const std::wstring path = file_path();
    if (path.empty()) {
        return false;
    }

    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        // No file: report a change once, so defaults get applied after a delete.
        const FILETIME zero{};
        if (CompareFileTime(&last_write, &zero) != 0) {
            last_write = zero;
            return true;
        }
        return false;
    }

    if (CompareFileTime(&info.ftLastWriteTime, &last_write) != 0) {
        last_write = info.ftLastWriteTime;
        return true;
    }
    return false;
}

}  // namespace settings
}  // namespace SamVoice
