// Letter-to-sound conversion: the SAM "reciter" rule engine plus English
// number expansion. Ported to match the Python implementation exactly,
// including its boundary behaviour.
#include "sam_engine.hpp"
#include "sam_tables.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace sam {
namespace {

using namespace tables;

inline std::uint8_t char_flag(char c)
{
    return CHAR_FLAGS[static_cast<unsigned char>(c)];
}

inline bool flags(char c, std::uint8_t f)
{
    return (char_flag(c) & f) != 0;
}

// Mirrors the Python flags_at(): out-of-range positions simply have no flags.
inline bool flags_at(const std::string& text, long pos, std::uint8_t f)
{
    if (pos < 0 || pos >= static_cast<long>(text.size())) {
        return false;
    }
    return flags(text[static_cast<std::size_t>(pos)], f);
}

// Equivalent of Python's text[a:b] == lit, with slice clamping.
bool slice_equals(const std::string& text, long a, long b, const char* lit)
{
    if (a < 0) {
        a = 0;
    }
    const long n = static_cast<long>(text.size());
    if (b > n) {
        b = n;
    }
    if (b <= a) {
        return *lit == '\0';
    }
    const std::size_t len = static_cast<std::size_t>(b - a);
    return std::strlen(lit) == len &&
           text.compare(static_cast<std::size_t>(a), len, lit) == 0;
}

inline bool is_tcs(char c) { return c == 'T' || c == 'C' || c == 'S'; }
inline bool is_eiy(char c) { return c == 'E' || c == 'I' || c == 'Y'; }

// A single rule of the form "xxx(yyy)zzz=phonemes".
class Rule
{
public:
    explicit Rule(const char* text)
    {
        const std::string s(text);
        const std::size_t eq = s.rfind('=');
        std::string source;
        if (eq == std::string::npos) {
            source = s;
        } else {
            target_ = s.substr(eq + 1);
            source = s.substr(0, eq);
        }

        const std::size_t open = source.find('(');
        const std::size_t close = source.find(')');
        if (open != std::string::npos && close != std::string::npos && close > open) {
            pre_ = source.substr(0, open);
            match_ = source.substr(open + 1, close - open - 1);
            post_ = source.substr(close + 1);
        } else {
            match_ = source;
        }
        first_ = match_.empty() ? '\0' : match_[0];
    }

    char first() const { return first_; }
    const std::string& target() const { return target_; }
    std::size_t match_length() const { return match_.size(); }

    bool matches(const std::string& text, long pos) const
    {
        if (pos < 0 || static_cast<std::size_t>(pos) + match_.size() > text.size()) {
            return false;
        }
        if (text.compare(static_cast<std::size_t>(pos), match_.size(), match_) != 0) {
            return false;
        }
        if (!check_prefix(text, pos)) {
            return false;
        }
        return check_suffix(text, pos + static_cast<long>(match_.size()) - 1);
    }

private:
    bool check_prefix(const std::string& text, long pos) const;
    bool check_suffix(const std::string& text, long pos) const;

    std::string pre_, match_, post_, target_;
    char first_ = '\0';
};

bool Rule::check_prefix(const std::string& text, long pos) const
{
    for (long i = static_cast<long>(pre_.size()) - 1; i >= 0; --i) {
        const char rb = pre_[static_cast<std::size_t>(i)];
        if (!flags(rb, FLAG_ALPHA_OR_QUOT)) {
            switch (rb) {
            case ' ':
                --pos;
                if (flags_at(text, pos, FLAG_ALPHA_OR_QUOT)) return false;
                break;
            case '#':
                --pos;
                if (!flags_at(text, pos, FLAG_VOWEL_OR_Y)) return false;
                break;
            case '.':
                --pos;
                if (!flags_at(text, pos, FLAG_0X08)) return false;
                break;
            case '&':
                --pos;
                if (!flags_at(text, pos, FLAG_DIPHTHONG_CH)) {
                    --pos;
                    if (!(pos >= 0 && (slice_equals(text, pos, pos + 2, "CH") ||
                                       slice_equals(text, pos, pos + 2, "SH")))) {
                        return false;
                    }
                }
                break;
            case '@':
                --pos;
                if (flags_at(text, pos, FLAG_VOICED_CH)) {
                    break;
                }
                if (pos >= 0 && text[static_cast<std::size_t>(pos)] == 'H') {
                    // Matches the Python: 'H' is never in TCS, so this always fails.
                    if (!is_tcs(text[static_cast<std::size_t>(pos)])) return false;
                } else {
                    return false;
                }
                break;
            case '^':
                --pos;
                if (!flags_at(text, pos, FLAG_CONSONANT_CH)) return false;
                break;
            case '+':
                --pos;
                if (pos < 0 || !is_eiy(text[static_cast<std::size_t>(pos)])) return false;
                break;
            case ':':
                while (pos >= 0 && flags_at(text, pos - 1, FLAG_CONSONANT_CH)) {
                    --pos;
                }
                break;
            default:
                return false;
            }
        } else {
            --pos;
            if (pos < 0 || text[static_cast<std::size_t>(pos)] != rb) return false;
        }
    }
    return true;
}

bool Rule::check_suffix(const std::string& text, long pos) const
{
    const long n = static_cast<long>(text.size());
    for (std::size_t i = 0; i < post_.size(); ++i) {
        const char rb = post_[i];
        if (!flags(rb, FLAG_ALPHA_OR_QUOT)) {
            switch (rb) {
            case ' ':
                ++pos;
                if (flags_at(text, pos, FLAG_ALPHA_OR_QUOT)) return false;
                break;
            case '#':
                ++pos;
                if (!flags_at(text, pos, FLAG_VOWEL_OR_Y)) return false;
                break;
            case '.':
                ++pos;
                if (!flags_at(text, pos, FLAG_0X08)) return false;
                break;
            case '&':
                ++pos;
                if (!flags_at(text, pos, FLAG_DIPHTHONG_CH)) {
                    ++pos;
                    if (!(pos >= 2 && (slice_equals(text, pos - 1, pos + 1, "HC") ||
                                       slice_equals(text, pos - 1, pos + 1, "HS")))) {
                        return false;
                    }
                }
                break;
            case '@':
                ++pos;
                if (flags_at(text, pos, FLAG_VOICED_CH)) {
                    break;
                }
                if (pos < n && text[static_cast<std::size_t>(pos)] == 'H') {
                    if (!is_tcs(text[static_cast<std::size_t>(pos)])) return false;
                } else {
                    return false;
                }
                break;
            case '^':
                ++pos;
                if (!flags_at(text, pos, FLAG_CONSONANT_CH)) return false;
                break;
            case '+':
                ++pos;
                if (pos >= n || !is_eiy(text[static_cast<std::size_t>(pos)])) return false;
                break;
            case ':':
                while (flags_at(text, pos + 1, FLAG_CONSONANT_CH)) {
                    ++pos;
                }
                break;
            case '%': {
                if (pos + 1 >= n) return false;
                if (text[static_cast<std::size_t>(pos + 1)] != 'E') {
                    if (slice_equals(text, pos + 1, pos + 4, "ING")) {
                        pos += 3;
                    } else {
                        return false;
                    }
                } else {
                    if (!flags_at(text, pos + 2, FLAG_ALPHA_OR_QUOT)) {
                        pos += 1;
                    } else if (pos + 2 < n && (text[static_cast<std::size_t>(pos + 2)] == 'R' ||
                                               text[static_cast<std::size_t>(pos + 2)] == 'S' ||
                                               text[static_cast<std::size_t>(pos + 2)] == 'D')) {
                        pos += 2;
                    } else if (pos + 2 < n && text[static_cast<std::size_t>(pos + 2)] == 'L') {
                        if (pos + 3 < n && text[static_cast<std::size_t>(pos + 3)] == 'Y') {
                            pos += 3;
                        } else {
                            return false;
                        }
                    } else if (slice_equals(text, pos + 2, pos + 5, "FUL")) {
                        pos += 4;
                    } else {
                        return false;
                    }
                }
                break;
            }
            default:
                return false;
            }
        } else {
            ++pos;
            if (pos >= n || text[static_cast<std::size_t>(pos)] != rb) return false;
        }
    }
    return true;
}

// Rules bucketed by leading character, mirroring the Python dict-of-lists, plus
// the flat ruleset 2 list. Built once on first use.
struct RuleSets
{
    std::vector<Rule> by_char[256];
    std::vector<Rule> set2;

    RuleSets()
    {
        for (int i = 0; i < RULES_COUNT; ++i) {
            Rule r(RULES[i]);
            by_char[static_cast<unsigned char>(r.first())].push_back(r);
        }
        for (int i = 0; i < RULES2_COUNT; ++i) {
            set2.emplace_back(RULES2[i]);
        }
    }
};

const RuleSets& rulesets()
{
    static const RuleSets sets;
    return sets;
}

const char* const ONES[] = {
    "", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
    "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen",
    "seventeen", "eighteen", "nineteen"
};
const char* const TENS[] = {
    "", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"
};

void number_to_words(long long n, std::string& out)
{
    if (n == 0) {
        out += "zero";
        return;
    }
    if (n < 0) {
        out += "negative ";
        number_to_words(-n, out);
        return;
    }

    std::vector<std::string> parts;
    if (n >= 1000000) {
        std::string p;
        number_to_words(n / 1000000, p);
        parts.push_back(p + " million");
        n %= 1000000;
    }
    if (n >= 1000) {
        std::string p;
        number_to_words(n / 1000, p);
        parts.push_back(p + " thousand");
        n %= 1000;
    }
    if (n >= 100) {
        parts.push_back(std::string(ONES[n / 100]) + " hundred");
        n %= 100;
    }
    if (n >= 20) {
        if (n % 10) {
            parts.push_back(std::string(TENS[n / 10]) + " " + ONES[n % 10]);
        } else {
            parts.push_back(TENS[n / 10]);
        }
    } else if (n > 0) {
        parts.push_back(ONES[n]);
    }

    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += ' ';
        out += parts[i];
    }
}

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Parses the leading integer of `s`, saturating rather than overflowing.
bool to_number(const std::string& s, long long& value)
{
    if (s.empty()) {
        return false;
    }
    std::size_t i = 0;
    bool neg = false;
    if (s[0] == '-') {
        neg = true;
        i = 1;
    }
    if (i >= s.size()) {
        return false;
    }
    long long v = 0;
    for (; i < s.size(); ++i) {
        if (!is_digit(s[i])) return false;
        if (v > 99999999999LL) {  // far past what number_to_words covers
            v = 99999999999LL;
            continue;
        }
        v = v * 10 + (s[i] - '0');
    }
    value = neg ? -v : v;
    return true;
}

}  // namespace

std::string rule_based_phonemes(const std::string& input_text, bool* ok)
{
    if (ok) {
        *ok = true;
    }

    std::string text = " ";
    text.reserve(input_text.size() + 1);
    for (char c : input_text) {
        text += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    const RuleSets& rs = rulesets();
    std::string output;
    long pos = 0;
    int guard = 0;

    while (pos < static_cast<long>(text.size()) && guard < 10000) {
        ++guard;
        const char c = text[static_cast<std::size_t>(pos)];

        if (c != '.' || flags_at(text, pos + 1, FLAG_NUMERIC)) {
            if (flags(c, FLAG_RULESET2)) {
                for (const Rule& r : rs.set2) {
                    if (r.matches(text, pos)) {
                        pos += static_cast<long>(r.match_length());
                        output += r.target();
                        break;
                    }
                }
                continue;
            }
            if (char_flag(c) != 0) {
                if (!flags(c, FLAG_ALPHA_OR_QUOT)) {
                    if (ok) *ok = false;
                    return std::string();
                }
                for (const Rule& r : rs.by_char[static_cast<unsigned char>(c)]) {
                    if (r.matches(text, pos)) {
                        pos += static_cast<long>(r.match_length());
                        output += r.target();
                        break;
                    }
                }
                continue;
            }
            output += ' ';
            ++pos;
            continue;
        }
        output += '.';
        ++pos;
    }
    return output;
}

std::string expand_numbers(const std::string& text)
{
    // Equivalent of re.sub(r'-?\d+\.?\d*', ...) over the input.
    std::string out;
    out.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        // A match is an optional '-', one or more digits, an optional '.', then
        // optional digits.
        std::size_t start = i;
        std::size_t j = i;
        if (text[j] == '-') {
            ++j;
        }
        if (j >= text.size() || !is_digit(text[j])) {
            out += text[i];
            ++i;
            continue;
        }
        while (j < text.size() && is_digit(text[j])) {
            ++j;
        }
        bool has_dot = false;
        if (j < text.size() && text[j] == '.') {
            has_dot = true;
            ++j;
            while (j < text.size() && is_digit(text[j])) {
                ++j;
            }
        }

        const std::string num = text.substr(start, j - start);

        if (has_dot) {
            const std::size_t dot = num.find('.');
            const std::string int_part = num.substr(0, dot);
            const std::string frac = num.substr(dot + 1);
            long long v = 0;
            if (int_part.empty() || int_part == "-") {
                v = 0;
            } else if (!to_number(int_part, v)) {
                out += num;
                i = j;
                continue;
            }
            number_to_words(v, out);
            out += " point";
            for (char d : frac) {
                out += ' ';
                number_to_words(d - '0', out);
            }
        } else {
            long long v = 0;
            if (!to_number(num, v)) {
                out += num;
                i = j;
                continue;
            }
            number_to_words(v, out);
        }
        i = j;
    }
    return out;
}

std::string text_to_phonemes(const std::string& input_text)
{
    if (input_text.empty()) {
        return std::string();
    }

    if (!dictionary_loaded()) {
        return rule_based_phonemes(input_text);
    }

    // Tokenise into runs of [A-Za-z'] and runs of everything else, matching
    // the Python re.findall(r"[A-Za-z']+|[^A-Za-z']+").
    auto is_word_char = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\'';
    };

    // Python's str.strip(). A token that reduces to nothing still contributes an
    // empty part, which the join below turns into a second separating space --
    // that spacing is load-bearing, so keep it.
    auto strip = [](const std::string& s) {
        const std::size_t a = s.find_first_not_of(" \t\n\r\f\v");
        if (a == std::string::npos) {
            return std::string();
        }
        const std::size_t b = s.find_last_not_of(" \t\n\r\f\v");
        return s.substr(a, b - a + 1);
    };

    // Appends the reciter's output for a token, mirroring the Python's
    // `if rule_result and rule_result is not False` test.
    auto append_rule_based = [&](std::vector<std::string>& parts, const std::string& token) {
        bool ok = true;
        const std::string r = rule_based_phonemes(token, &ok);
        if (ok && !r.empty()) {
            parts.push_back(strip(r));
        }
    };

    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i < input_text.size()) {
        const bool word = is_word_char(input_text[i]);
        std::size_t j = i;
        while (j < input_text.size() && is_word_char(input_text[j]) == word) {
            ++j;
        }
        const std::string token = input_text.substr(i, j - i);

        if (word) {
            const std::string phonemes = dictionary_lookup(token);
            if (!phonemes.empty()) {
                parts.push_back(phonemes);
            } else {
                append_rule_based(parts, token);
            }
        } else {
            append_rule_based(parts, token);
        }
        i = j;
    }

    std::string out;
    for (std::size_t k = 0; k < parts.size(); ++k) {
        if (k) out += ' ';
        out += parts[k];
    }
    return out;
}

}  // namespace sam
