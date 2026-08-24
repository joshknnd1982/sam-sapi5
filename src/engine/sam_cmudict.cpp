// CMU Pronouncing Dictionary lookup, mapped onto SAM's phoneme alphabet.
// The dictionary is optional: without it the engine falls back to the reciter.
#include "sam_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sam {
namespace {

struct Entry
{
    // Pre-converted SAM phoneme string, so lookup is a pure hash hit.
    std::string phonemes;
};

std::mutex g_mutex;
std::unordered_map<std::string, Entry> g_dict;
std::string g_loaded_path;
bool g_loaded = false;

// ARPABET -> SAM. Anything unmapped passes through unchanged, as in the Python.
const char* arpabet_to_sam(const char* p)
{
    struct Map { const char* from; const char* to; };
    static const Map table[] = {
        {"AA", "AA"}, {"AE", "AE"}, {"AH", "AH"}, {"AO", "AO"}, {"AW", "AW"},
        {"AY", "AY"}, {"EH", "EH"}, {"ER", "ER"}, {"EY", "EY"}, {"IH", "IH"},
        {"IY", "IY"}, {"OW", "OW"}, {"OY", "OY"}, {"UH", "UH"}, {"UW", "UW"},
        {"B", "B"},   {"CH", "CH"}, {"D", "D"},   {"DH", "DH"}, {"F", "F"},
        {"G", "G"},   {"HH", "/H"}, {"JH", "J"},  {"K", "K"},   {"L", "L"},
        {"M", "M"},   {"N", "N"},   {"NG", "NX"}, {"P", "P"},   {"R", "R"},
        {"S", "S"},   {"SH", "SH"}, {"T", "T"},   {"TH", "TH"}, {"V", "V"},
        {"W", "W"},   {"Y", "Y"},   {"Z", "Z"},   {"ZH", "ZH"},
    };
    for (const Map& m : table) {
        if (std::strcmp(m.from, p) == 0) {
            return m.to;
        }
    }
    return nullptr;
}

// Converts a whitespace-separated ARPABET pronunciation to a SAM phoneme
// string, translating CMU stress digits to SAM's stress markers.
std::string convert_pronunciation(const char* line)
{
    std::string out;
    const char* p = line;

    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            ++p;
        }
        if (!*p) {
            break;
        }
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            ++p;
        }

        char token[16];
        std::size_t len = static_cast<std::size_t>(p - start);
        if (len >= sizeof(token)) {
            len = sizeof(token) - 1;
        }
        std::memcpy(token, start, len);
        token[len] = '\0';

        // Split a trailing stress digit off the phoneme.
        char stress = '\0';
        if (len > 0 && (token[len - 1] == '0' || token[len - 1] == '1' || token[len - 1] == '2')) {
            stress = token[len - 1];
            token[len - 1] = '\0';
        }

        const char* mapped = arpabet_to_sam(token);
        out += mapped ? mapped : token;

        // CMU 0 (unstressed) adds no marker; 1 -> '4', 2 -> '2'.
        if (stress == '1') {
            out += '4';
        } else if (stress == '2') {
            out += '2';
        }
    }
    return out;
}

}  // namespace

std::size_t load_dictionary(const std::string& path)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_loaded && g_loaded_path == path) {
        return g_dict.size();
    }

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return 0;
    }

    g_dict.clear();
    g_dict.reserve(140000);

    // Lines look like:  HELLO  HH AH0 L OW1
    // Variants carry a "(2)" suffix on the word; the first spelling wins.
    std::vector<char> buf(4096);
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), f)) {
        char* line = buf.data();
        if (line[0] == '\0' || std::strncmp(line, ";;;", 3) == 0) {
            continue;
        }

        char* p = line;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        char* word_start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            ++p;
        }
        if (p == word_start || !*p) {
            continue;
        }
        *p = '\0';
        char* rest = p + 1;

        std::string word(word_start);
        const std::size_t paren = word.find('(');
        if (paren != std::string::npos) {
            word.resize(paren);
        }
        if (word.empty()) {
            continue;
        }
        for (char& c : word) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        if (g_dict.find(word) != g_dict.end()) {
            continue;  // keep the first pronunciation
        }
        std::string phonemes = convert_pronunciation(rest);
        if (phonemes.empty()) {
            continue;
        }
        g_dict.emplace(std::move(word), Entry{std::move(phonemes)});
    }

    std::fclose(f);
    g_loaded = true;
    g_loaded_path = path;
    return g_dict.size();
}

bool dictionary_loaded()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_loaded && !g_dict.empty();
}

std::size_t dictionary_size()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_dict.size();
}

std::string dictionary_lookup(const std::string& word)
{
    // Python does word.upper().strip(): trim the ends only.
    const std::size_t a = word.find_first_not_of(" \t\r\n\f\v");
    if (a == std::string::npos) {
        return std::string();
    }
    const std::size_t b = word.find_last_not_of(" \t\r\n\f\v");

    std::string key = word.substr(a, b - a + 1);
    for (char& c : key) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_dict.find(key);
    return it == g_dict.end() ? std::string() : it->second.phonemes;
}

}  // namespace sam
