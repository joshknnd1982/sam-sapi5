// Phoneme string -> timed phoneme frames. Applies SAM's rewriting rules,
// stress propagation and length adjustment.
#include "sam_engine.hpp"
#include "sam_tables.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace sam {
namespace {

using namespace tables;

constexpr int pR = 23;  // R*
constexpr int pD = 57;  // D*
constexpr int pT = 69;  // T*

constexpr std::uint16_t FLAG_DIP_YX = 0x0020;  // diphthong ending in YX
constexpr std::uint16_t FLAG_0008 = 0x0008;

// Sentinel for "off the end of the phoneme list", matching the Python's None.
constexpr int NONE = -1;

bool phoneme_has_flag(int phoneme, std::uint16_t flag)
{
    if (phoneme < 0 || phoneme >= PHONEME_FLAGS_COUNT) {
        return false;
    }
    return (PHONEME_FLAGS[phoneme] & flag) != 0;
}

// Working state for the whole parse, so the rewriting rules can insert into
// the middle of the list the way the Python closures do.
struct ParseState
{
    std::vector<int> index;
    std::vector<int> length;
    std::vector<int> stress;

    int get_phoneme(long pos) const
    {
        if (pos < 0 || pos >= static_cast<long>(index.size())) {
            return NONE;
        }
        return index[static_cast<std::size_t>(pos)];
    }

    void set_phoneme(long pos, int value)
    {
        index[static_cast<std::size_t>(pos)] = value;
    }

    void insert_phoneme(long pos, int value, int stress_value, int len = 0)
    {
        const std::size_t at = static_cast<std::size_t>(pos);
        index.insert(index.begin() + at, value);
        length.insert(length.begin() + at, len);
        stress.insert(stress.begin() + at, stress_value);
    }

    int get_stress(long pos) const
    {
        if (pos < 0 || pos >= static_cast<long>(stress.size())) {
            return 0;
        }
        return stress[static_cast<std::size_t>(pos)];
    }

    void set_stress(long pos, int value)
    {
        stress[static_cast<std::size_t>(pos)] = value;
    }

    int get_length(long pos) const
    {
        if (pos < 0 || pos >= static_cast<long>(length.size())) {
            return 0;
        }
        return length[static_cast<std::size_t>(pos)];
    }

    void set_length(long pos, int value)
    {
        length[static_cast<std::size_t>(pos)] = value;
    }

    void add_phoneme(int value)
    {
        index.push_back(value);
        length.push_back(0);
        stress.push_back(0);
    }

    void add_stress(int value)
    {
        if (!stress.empty()) {
            stress.back() = value;
        }
    }
};

// Two-character phoneme name lookup. '*' in the second slot means the entry is
// a single-character phoneme and must not match here.
int full_match(char s1, char s2)
{
    for (int i = 0; i < PHONEME_NAME_COUNT; ++i) {
        const char* name = PHONEME_NAME_TABLE[i];
        if (name[0] == s1 && name[1] == s2 && name[1] != '*') {
            return i;
        }
    }
    return -1;
}

int single_match(char s1)
{
    for (int i = 0; i < PHONEME_NAME_COUNT; ++i) {
        const char* name = PHONEME_NAME_TABLE[i];
        if (name[0] == s1 && name[1] == '*') {
            return i;
        }
    }
    return -1;
}

// Turns the phoneme string into indices plus stress markers.
// Returns false on an unrecognised character.
bool parser1(const std::string& input, ParseState& st)
{
    std::size_t src = 0;
    while (src < input.size()) {
        const char s1 = input[src];
        const char s2 = (src + 1 < input.size()) ? input[src + 1] : '\0';

        int match = full_match(s1, s2);
        if (match >= 0) {
            src += 2;
            st.add_phoneme(match);
            continue;
        }
        match = single_match(s1);
        if (match >= 0) {
            src += 1;
            st.add_phoneme(match);
            continue;
        }

        // Otherwise it must be a stress digit from STRESS_TABLE.
        int m = 8;  // len("*12345678") - 1
        while (m > 0 && s1 != STRESS_TABLE[m]) {
            --m;
        }
        if (m == 0) {
            return false;
        }
        st.add_stress(m);
        src += 1;
    }
    return true;
}

void handle_uw_ch_j(ParseState& st, int phoneme, long pos)
{
    if (phoneme == 53) {  // UW
        if (phoneme_has_flag(st.get_phoneme(pos - 1), FLAG_ALVEOLAR)) {
            st.set_phoneme(pos, 16);  // UX
        }
    } else if (phoneme == 42) {  // CH
        st.insert_phoneme(pos + 1, 43, st.get_stress(pos));
    } else if (phoneme == 44) {  // J*
        st.insert_phoneme(pos + 1, 45, st.get_stress(pos));
    }
}

void change_ax(ParseState& st, long position, int suffix)
{
    st.set_phoneme(position, 13);  // AX
    st.insert_phoneme(position + 1, suffix, st.get_stress(position));
}

void parser2(ParseState& st)
{
    long pos = -1;
    for (;;) {
        ++pos;
        const int phoneme = st.get_phoneme(pos);
        if (phoneme == NONE) {
            break;
        }
        if (phoneme == 0) {
            continue;
        }

        if (phoneme_has_flag(phoneme, FLAG_DIPHTHONG)) {
            const int suffix = phoneme_has_flag(phoneme, FLAG_DIP_YX) ? 21 : 20;
            st.insert_phoneme(pos + 1, suffix, st.get_stress(pos));
            handle_uw_ch_j(st, phoneme, pos);
            continue;
        }
        if (phoneme == 78) { change_ax(st, pos, 24); continue; }
        if (phoneme == 79) { change_ax(st, pos, 27); continue; }
        if (phoneme == 80) { change_ax(st, pos, 28); continue; }

        if (phoneme_has_flag(phoneme, FLAG_VOWEL) && st.get_stress(pos)) {
            if (st.get_phoneme(pos + 1) == 0) {
                const int next_phoneme = st.get_phoneme(pos + 2);
                if (next_phoneme != NONE && phoneme_has_flag(next_phoneme, FLAG_VOWEL)) {
                    if (st.get_stress(pos + 2)) {
                        st.insert_phoneme(pos + 2, 31, 0);  // Q
                    }
                }
            }
            continue;
        }

        const int prior = (pos > 0) ? st.get_phoneme(pos - 1) : NONE;

        if (phoneme == pR) {
            if (prior == pT) {
                st.set_phoneme(pos - 1, 42);  // T R -> CH R
            } else if (prior == pD) {
                st.set_phoneme(pos - 1, 44);  // D R -> J R
            } else if (phoneme_has_flag(prior, FLAG_VOWEL)) {
                st.set_phoneme(pos, 18);  // <VOWEL> R -> <VOWEL> RX
            }
            continue;
        }
        if (phoneme == 24 && phoneme_has_flag(prior, FLAG_VOWEL)) {
            st.set_phoneme(pos, 19);  // <VOWEL> L -> <VOWEL> LX
            continue;
        }
        if (prior == 60 && phoneme == 32) {
            st.set_phoneme(pos, 38);
            continue;
        }
        if (phoneme == 60) {
            const int next_phoneme = st.get_phoneme(pos + 1);
            if (!phoneme_has_flag(next_phoneme, FLAG_DIP_YX) && next_phoneme != NONE) {
                st.set_phoneme(pos, 63);  // GX
            }
            continue;
        }

        int current = phoneme;
        if (current == 72) {
            const int next_phoneme = st.get_phoneme(pos + 1);
            if (!phoneme_has_flag(next_phoneme, FLAG_DIP_YX) || next_phoneme == NONE) {
                st.set_phoneme(pos, 75);  // KX
                current = 75;
            }
        }

        if (phoneme_has_flag(current, FLAG_UNVOICED_STOPCONS) && prior == 32) {
            st.set_phoneme(pos, current - 12);
        } else if (!phoneme_has_flag(current, FLAG_UNVOICED_STOPCONS)) {
            handle_uw_ch_j(st, current, pos);
        }

        if (current == 69 || current == 57) {
            if (pos > 0 && phoneme_has_flag(st.get_phoneme(pos - 1), FLAG_VOWEL)) {
                int next_ph = st.get_phoneme(pos + 1);
                if (next_ph == 0) {
                    next_ph = st.get_phoneme(pos + 2);
                }
                if (phoneme_has_flag(next_ph, FLAG_VOWEL) && !st.get_stress(pos + 1)) {
                    st.set_phoneme(pos, 30);  // DX
                }
            }
            continue;
        }
    }
}

// Copies a vowel's stress back onto the consonant in front of it.
void copy_stress(ParseState& st)
{
    long position = 0;
    for (;;) {
        const int phoneme = st.get_phoneme(position);
        if (phoneme == NONE) {
            break;
        }
        if (phoneme_has_flag(phoneme, FLAG_CONSONANT)) {
            const int next_phoneme = st.get_phoneme(position + 1);
            if (next_phoneme != NONE && phoneme_has_flag(next_phoneme, FLAG_VOWEL)) {
                const int stress = st.get_stress(position + 1);
                if (stress != 0 && stress < 0x80) {
                    st.set_stress(position, stress + 1);
                }
            }
        }
        ++position;
    }
}

void set_phoneme_length(ParseState& st)
{
    long position = 0;
    for (;;) {
        const int phoneme = st.get_phoneme(position);
        if (phoneme == NONE) {
            break;
        }
        const int stress = st.get_stress(position);
        int length = 0;
        if (phoneme < COMBINED_PHONEME_LENGTH_COUNT) {
            length = (stress == 0 || stress > 0x7F)
                         ? (COMBINED_PHONEME_LENGTH_TABLE[phoneme] & 0xFF)
                         : (COMBINED_PHONEME_LENGTH_TABLE[phoneme] >> 8);
        }
        st.set_length(position, length);
        ++position;
    }
}

void adjust_lengths(ParseState& st)
{
    // Lengthen the run between the last vowel and a following punctuation mark.
    long position = 0;
    while (st.get_phoneme(position) != NONE) {
        if (!phoneme_has_flag(st.get_phoneme(position), FLAG_PUNCT)) {
            ++position;
            continue;
        }
        const long loop_index = position;
        --position;
        while (position > 1 && !phoneme_has_flag(st.get_phoneme(position), FLAG_VOWEL)) {
            --position;
        }
        if (position == 0) {
            break;
        }
        while (position < loop_index) {
            const int phoneme = st.get_phoneme(position);
            if (!phoneme_has_flag(phoneme, FLAG_FRICATIVE) ||
                phoneme_has_flag(phoneme, FLAG_VOICED)) {
                const int length = st.get_length(position);
                st.set_length(position, (length >> 1) + length + 1);
            }
            ++position;
        }
        position = loop_index + 1;
    }

    long loop_index = -1;
    for (;;) {
        ++loop_index;
        const int phoneme = st.get_phoneme(loop_index);
        if (phoneme == NONE) {
            break;
        }
        position = loop_index;

        if (phoneme_has_flag(phoneme, FLAG_VOWEL)) {
            ++position;
            const int next_phoneme = st.get_phoneme(position);
            if (!phoneme_has_flag(next_phoneme, FLAG_CONSONANT)) {
                if (next_phoneme == 18 || next_phoneme == 19) {  // RX, LX
                    ++position;
                    if (phoneme_has_flag(st.get_phoneme(position), FLAG_CONSONANT)) {
                        st.set_length(loop_index, st.get_length(loop_index) - 1);
                    }
                }
                continue;
            }
            // Python: an out-of-range next phoneme is treated as an unvoiced stop.
            const std::uint16_t flags =
                (next_phoneme != NONE && next_phoneme < PHONEME_FLAGS_COUNT)
                    ? PHONEME_FLAGS[next_phoneme]
                    : static_cast<std::uint16_t>(FLAG_CONSONANT | FLAG_UNVOICED_STOPCONS);

            if ((flags & FLAG_VOICED) == 0) {
                if (flags & FLAG_UNVOICED_STOPCONS) {
                    const int length = st.get_length(loop_index);
                    st.set_length(loop_index, length - (length >> 3));
                }
                continue;
            }
            const int length = st.get_length(loop_index);
            st.set_length(loop_index, (length >> 2) + length + 1);
            continue;
        }

        if (phoneme_has_flag(phoneme, FLAG_NASAL)) {
            ++position;
            const int next_phoneme = st.get_phoneme(position);
            if (next_phoneme != NONE && phoneme_has_flag(next_phoneme, FLAG_STOPCONS)) {
                st.set_length(position, 6);
                st.set_length(position - 1, 5);
            }
            continue;
        }

        if (phoneme_has_flag(phoneme, FLAG_STOPCONS)) {
            ++position;
            while (st.get_phoneme(position) == 0) {
                ++position;
            }
            const int next_phoneme = st.get_phoneme(position);
            if (next_phoneme != NONE && phoneme_has_flag(next_phoneme, FLAG_STOPCONS)) {
                st.set_length(position, (st.get_length(position) >> 1) + 1);
                st.set_length(loop_index, (st.get_length(loop_index) >> 1) + 1);
            }
            continue;
        }

        if (position > 0 && phoneme_has_flag(phoneme, FLAG_LIQUID)) {
            if (phoneme_has_flag(st.get_phoneme(position - 1), FLAG_STOPCONS)) {
                st.set_length(position, st.get_length(position) - 2);
            }
        }
    }
}

// Plosives get their release burst by inserting the two table entries that
// follow them.
void prolong_plosive_stop_consonants(ParseState& st)
{
    long pos = -1;
    for (;;) {
        ++pos;
        const int index = st.get_phoneme(pos);
        if (index == NONE) {
            break;
        }
        if (!phoneme_has_flag(index, FLAG_STOPCONS)) {
            continue;
        }

        if (phoneme_has_flag(index, FLAG_UNVOICED_STOPCONS)) {
            long x = pos;
            int next_non_empty;
            for (;;) {
                ++x;
                next_non_empty = st.get_phoneme(x);
                if (next_non_empty != 0) {
                    break;
                }
            }
            if (next_non_empty != NONE) {
                if (phoneme_has_flag(next_non_empty, FLAG_0008) ||
                    next_non_empty == 36 || next_non_empty == 37) {
                    continue;
                }
            }
        }

        const int length1 = (index + 1 < COMBINED_PHONEME_LENGTH_COUNT)
                                ? (COMBINED_PHONEME_LENGTH_TABLE[index + 1] & 0xFF)
                                : 0;
        const int length2 = (index + 2 < COMBINED_PHONEME_LENGTH_COUNT)
                                ? (COMBINED_PHONEME_LENGTH_TABLE[index + 2] & 0xFF)
                                : 0;
        st.insert_phoneme(pos + 1, index + 1, st.get_stress(pos), length1);
        st.insert_phoneme(pos + 2, index + 2, st.get_stress(pos), length2);
        pos += 2;
    }
}

}  // namespace

bool parse_phonemes(const std::string& input, std::vector<Phoneme>& out)
{
    out.clear();
    if (input.empty()) {
        return false;
    }

    ParseState st;
    if (!parser1(input, st)) {
        return false;
    }
    parser2(st);
    copy_stress(st);
    set_phoneme_length(st);
    adjust_lengths(st);
    prolong_plosive_stop_consonants(st);

    out.reserve(st.index.size());
    for (std::size_t i = 0; i < st.index.size(); ++i) {
        if (st.index[i]) {
            out.push_back(Phoneme{st.index[i], st.length[i], st.stress[i]});
        }
    }
    return true;
}

}  // namespace sam
