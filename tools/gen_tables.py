"""Generate C++ data tables from the Python SAM tables, bit-exact."""
import sys, os
BIN = r"C:\Users\joshk\OneDrive\dev\sapivoice\bin"
OUT = r"C:\Users\joshk\OneDrive\dev\sapivoice\src\engine"
sys.path.insert(0, BIN)
os.makedirs(OUT, exist_ok=True)

import reciter_tables as rt, parser_tables as pt, renderer_tables as rr


def c_str(s):
    """Escape a Python str into a C string literal body."""
    out = []
    for ch in s:
        if ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\t':
            out.append('\\t')
        elif ch == '?':
            out.append('\\?')   # avoid trigraph warnings
        elif 32 <= ord(ch) < 127:
            out.append(ch)
        else:
            out.append('\\x%02x' % ord(ch))
    return '"' + ''.join(out) + '"'


def emit_array(f, ctype, name, values, fmt='%d', per_line=12):
    f.write("const %s %s[%d] = {\n" % (ctype, name, len(values)))
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        f.write("    " + ", ".join(fmt % v for v in chunk) + ",\n")
    f.write("};\n\n")


def emit_strarray(f, name, items):
    f.write("const char* const %s[%d] = {\n" % (name, len(items)))
    for it in items:
        f.write("    %s,\n" % c_str(it))
    f.write("};\n")
    f.write("const int %s_COUNT = %d;\n\n" % (name, len(items)))


# ---------------------------------------------------------------- header
with open(os.path.join(OUT, "sam_tables.hpp"), "w", encoding="ascii") as f:
    f.write("""// Generated from the Python SAM tables by tools/gen_tables.py -- do not edit.
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
""")
    for nm in ("FLAG_VOWEL", "FLAG_CONSONANT", "FLAG_DIPHTHONG", "FLAG_VOICED",
               "FLAG_STOPCONS", "FLAG_UNVOICED_STOPCONS", "FLAG_PUNCT",
               "FLAG_FRICATIVE", "FLAG_LIQUID", "FLAG_NASAL", "FLAG_ALVEOLAR"):
        f.write("inline constexpr std::uint16_t %s = 0x%04X;\n" % (nm, getattr(pt, nm)))
    f.write("""
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
""")

# ---------------------------------------------------------------- source
with open(os.path.join(OUT, "sam_tables.cpp"), "w", encoding="ascii") as f:
    f.write("// Generated from the Python SAM tables by tools/gen_tables.py -- do not edit.\n")
    f.write('#include "sam_tables.hpp"\n\nnamespace sam {\nnamespace tables {\n\n')

    cf = [0] * 256
    for ch, v in rt.char_flags.items():
        cf[ord(ch)] = v
    emit_array(f, "std::uint8_t", "CHAR_FLAGS", cf, "0x%02X", 12)

    emit_strarray(f, "RULES", [r for r in rt.rules.split('|') if r])
    emit_strarray(f, "RULES2", [r for r in rt.rules2.split('|') if r])

    f.write('const char STRESS_TABLE[10] = "*12345678";\n\n')

    f.write("const char PHONEME_NAME_TABLE[81][3] = {\n")
    for n in pt.PHONEME_NAME_TABLE:
        assert len(n) == 2, n
        f.write("    %s,\n" % c_str(n))
    f.write("};\n")
    f.write("const int PHONEME_NAME_COUNT = %d;\n\n" % len(pt.PHONEME_NAME_TABLE))

    emit_array(f, "std::uint16_t", "PHONEME_FLAGS", pt.PHONEME_FLAGS, "0x%04X", 8)
    f.write("const int PHONEME_FLAGS_COUNT = %d;\n\n" % len(pt.PHONEME_FLAGS))
    emit_array(f, "std::uint16_t", "COMBINED_PHONEME_LENGTH_TABLE",
               pt.COMBINED_PHONEME_LENGTH_TABLE, "0x%04X", 8)
    f.write("const int COMBINED_PHONEME_LENGTH_COUNT = %d;\n\n"
            % len(pt.COMBINED_PHONEME_LENGTH_TABLE))

    emit_array(f, "std::uint8_t", "STRESS_PITCH_TABLE", rr.STRESS_PITCH_TABLE, "%d", 10)
    f.write("const int STRESS_PITCH_COUNT = %d;\n\n" % len(rr.STRESS_PITCH_TABLE))
    emit_array(f, "std::uint8_t", "BLEND_RANK", rr.BLEND_RANK, "%d", 16)
    emit_array(f, "std::uint8_t", "OUT_BLEND_LENGTH", rr.OUT_BLEND_LENGTH, "%d", 16)
    emit_array(f, "std::uint8_t", "IN_BLEND_LENGTH", rr.IN_BLEND_LENGTH, "%d", 16)
    emit_array(f, "std::uint8_t", "SAMPLED_CONSONANT_FLAGS", rr.SAMPLED_CONSONANT_FLAGS, "%d", 16)
    f.write("const int BLEND_COUNT = %d;\n\n" % len(rr.BLEND_RANK))

    emit_array(f, "std::uint32_t", "FREQUENCY_DATA", rr.FREQUENCY_DATA, "0x%08X", 6)
    emit_array(f, "std::uint32_t", "AMPLITUDE_DATA", rr.AMPLITUDE_DATA, "0x%08X", 6)
    f.write("const int FREQ_AMP_COUNT = %d;\n\n" % len(rr.FREQUENCY_DATA))

    emit_array(f, "std::uint8_t", "AMPLITUDE_RESCALE", rr.AMPLITUDE_RESCALE, "%d", 16)
    f.write("const int AMPLITUDE_RESCALE_COUNT = %d;\n\n" % len(rr.AMPLITUDE_RESCALE))

    emit_array(f, "std::uint8_t", "SAMPLE_TABLE", rr.SAMPLE_TABLE, "0x%02X", 16)
    f.write("const int SAMPLE_TABLE_COUNT = %d;\n\n" % len(rr.SAMPLE_TABLE))
    emit_array(f, "std::uint8_t", "SAMPLED_CONSONANT_VALUES0",
               rr.SAMPLED_CONSONANT_VALUES0, "%d", 8)
    f.write("const int SAMPLED_CONSONANT_VALUES0_COUNT = %d;\n\n"
            % len(rr.SAMPLED_CONSONANT_VALUES0))

    emit_array(f, "std::int8_t", "SINUS", [rr.sinus(x) for x in range(256)], "%d", 16)

    f.write("}  // namespace tables\n}  // namespace sam\n")

print("generated sam_tables.hpp / sam_tables.cpp in", OUT)
for n in ("sam_tables.hpp", "sam_tables.cpp"):
    print("  %-16s %d bytes" % (n, os.path.getsize(os.path.join(OUT, n))))
