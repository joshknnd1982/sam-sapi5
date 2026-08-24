// SAPI-facing description of each SAM voice.
#pragma once

#include <string>

#include "../engine/sam_engine.hpp"
#include "utils.hpp"

namespace SamVoice {
namespace sapi {

// SAM is a US-English synthesizer; 409 is the LCID SAPI expects for en-US.
inline constexpr const wchar_t* LANGUAGE_ID = L"409";

struct voice_description
{
    const char* gender;  // "Male", "Female" or "Neutral"
    const char* age;     // "Adult", "Senior", ...
};

// Parallel to sam::VOICES. The six voices are formant settings rather than
// recorded speakers, so gender and age describe how each one comes across.
inline constexpr voice_description VOICE_DESCRIPTIONS[sam::VOICE_COUNT] = {
    {"Male",    "Adult"},   // sam
    {"Neutral", "Child"},   // elf
    {"Neutral", "Adult"},   // little_robot
    {"Male",    "Adult"},   // stuffy_guy
    {"Female",  "Senior"},  // little_old_lady
    {"Neutral", "Adult"},   // extra_terrestrial
};

class voice_attributes
{
public:
    explicit voice_attributes(int voice_index = 0) noexcept
        : index_(voice_index)
    {
        if (index_ < 0 || index_ >= sam::VOICE_COUNT) {
            index_ = 0;
        }
    }

    [[nodiscard]] int get_index() const noexcept { return index_; }

    // Stable machine name, e.g. "little_old_lady".
    [[nodiscard]] std::string get_id() const
    {
        return sam::VOICES[index_].id;
    }

    // What the user sees in a voice list, e.g. "SAM Little Old Lady".
    [[nodiscard]] std::wstring get_name() const
    {
        return L"SAM " + utils::string_to_wstring(sam::VOICES[index_].display);
    }

    [[nodiscard]] std::wstring get_age() const
    {
        return utils::string_to_wstring(VOICE_DESCRIPTIONS[index_].age);
    }

    [[nodiscard]] std::wstring get_gender() const
    {
        return utils::string_to_wstring(VOICE_DESCRIPTIONS[index_].gender);
    }

    [[nodiscard]] std::wstring get_language() const
    {
        return LANGUAGE_ID;
    }

private:
    int index_;
};

}  // namespace sapi
}  // namespace SamVoice
