// Persistent settings, stored as an INI file under %APPDATA% -- deliberately
// not in the registry.
//
// Both the SAPI driver and the configuration utility use this. The driver polls
// the file's timestamp on every Speak() call, so a change made in the utility
// takes effect on the next utterance without restarting the host application.
#pragma once

#include <string>
#include <windows.h>

#include "../engine/sam_engine.hpp"
#include "sam_log.hpp"

namespace SamVoice {
namespace settings {

// Everything a user can adjust. Per-voice values live in their own INI section
// so tuning one voice does not disturb another.
struct VoiceSettings
{
    int pitch = 64;         // 0-255
    int speed = 72;         // 0-255 (lower is faster)
    int mouth = 128;        // 0-255
    int throat = 128;       // 0-255
    int inflection = 50;    // 0-100
    bool singmode = false;
};

struct Settings
{
    // Voice selected in the utility; the SAPI host normally overrides this via
    // the voice token, so it mainly seeds the utility's own preview.
    std::string default_voice = "sam";

    int volume = 100;               // 0-100, applied on top of the SAPI volume
    bool expand_numbers = true;     // spell digits out as words

    // How SAPI's -10..+10 rate maps onto the engine's speed range.
    int rate_min_speed = 20;        // engine speed at SAPI rate +10 (fastest)
    int rate_max_speed = 150;       // engine speed at SAPI rate -10 (slowest)

    log::Level log_level = log::Level::Off;

    VoiceSettings voices[sam::VOICE_COUNT];

    // Resets everything, including every per-voice section, to the defaults.
    void reset();

    const VoiceSettings& voice(int index) const;
    VoiceSettings& voice(int index);

    // Builds the engine parameters for `voice_index`, applying that voice's
    // saved settings.
    sam::VoiceParams params_for(int voice_index) const;
};

// Reads settings.ini. Missing file or missing keys fall back to the defaults,
// so a fresh install works with no file present.
Settings load();

// Writes settings.ini, creating the directory if needed. Returns false on
// failure (a read-only profile, for instance).
bool save(const Settings& s);

// Cheap change check: returns true when settings.ini has been written since the
// value in `last_write` and updates it. Used by the driver to pick up edits.
bool changed_since(FILETIME& last_write);

// Clamps every field into its valid range.
void clamp(Settings& s);

std::wstring file_path();

}  // namespace settings
}  // namespace SamVoice
