// Generated from the Python SAM tables by tools/gen_tables.py -- do not edit.
#pragma once

#include <cstdint>

namespace sam {
namespace tables {

// ---- reciter -------------------------------------------------------------
// Flag byte per input character, indexed directly by (unsigned char).
// Characters absent from the original table map to 0.
extern const std::uint8_t CHAR_FLAGS[256];
extern const char* const RULES[];
extern const int RULES_COUNT;
extern const char* const RULES2[];
extern const int RULES2_COUNT;

// ---- parser --------------------------------------------------------------
extern const char STRESS_TABLE[10];              // "*12345678" + NUL
extern const char PHONEME_NAME_TABLE[81][3];     // two chars + NUL
extern const int PHONEME_NAME_COUNT;
extern const std::uint16_t PHONEME_FLAGS[81];
extern const int PHONEME_FLAGS_COUNT;
extern const std::uint16_t COMBINED_PHONEME_LENGTH_TABLE[80];
extern const int COMBINED_PHONEME_LENGTH_COUNT;

// ---- renderer ------------------------------------------------------------
extern const std::uint8_t STRESS_PITCH_TABLE[10];
extern const int STRESS_PITCH_COUNT;
extern const std::uint8_t BLEND_RANK[80];
extern const std::uint8_t OUT_BLEND_LENGTH[80];
extern const std::uint8_t IN_BLEND_LENGTH[80];
extern const std::uint8_t SAMPLED_CONSONANT_FLAGS[80];
extern const int BLEND_COUNT;
extern const std::uint32_t FREQUENCY_DATA[80];
extern const std::uint32_t AMPLITUDE_DATA[80];
extern const int FREQ_AMP_COUNT;
extern const std::uint8_t AMPLITUDE_RESCALE[16];
extern const int AMPLITUDE_RESCALE_COUNT;
extern const std::uint8_t SAMPLE_TABLE[1280];
extern const int SAMPLE_TABLE_COUNT;
extern const std::uint8_t SAMPLED_CONSONANT_VALUES0[5];
extern const int SAMPLED_CONSONANT_VALUES0_COUNT;
// Precomputed int(sin(2*pi*x/256) * 127) for x in 0..255.
extern const std::int8_t SINUS[256];

// ---- parser flag bits ----------------------------------------------------
inline constexpr std::uint16_t FLAG_VOWEL = 0x0080;
inline constexpr std::uint16_t FLAG_CONSONANT = 0x0040;
inline constexpr std::uint16_t FLAG_DIPHTHONG = 0x0010;
inline constexpr std::uint16_t FLAG_VOICED = 0x0004;
inline constexpr std::uint16_t FLAG_STOPCONS = 0x0002;
inline constexpr std::uint16_t FLAG_UNVOICED_STOPCONS = 0x0001;
inline constexpr std::uint16_t FLAG_PUNCT = 0x0100;
inline constexpr std::uint16_t FLAG_FRICATIVE = 0x2000;
inline constexpr std::uint16_t FLAG_LIQUID = 0x1000;
inline constexpr std::uint16_t FLAG_NASAL = 0x0800;
inline constexpr std::uint16_t FLAG_ALVEOLAR = 0x0400;

// ---- reciter flag bits ---------------------------------------------------
inline constexpr std::uint8_t FLAG_NUMERIC       = 0x01;
inline constexpr std::uint8_t FLAG_RULESET2      = 0x02;
inline constexpr std::uint8_t FLAG_VOICED_CH     = 0x04;
inline constexpr std::uint8_t FLAG_0X08          = 0x08;
inline constexpr std::uint8_t FLAG_DIPHTHONG_CH  = 0x10;
inline constexpr std::uint8_t FLAG_CONSONANT_CH  = 0x20;
inline constexpr std::uint8_t FLAG_VOWEL_OR_Y    = 0x40;
inline constexpr std::uint8_t FLAG_ALPHA_OR_QUOT = 0x80;

}  // namespace tables
}  // namespace sam
