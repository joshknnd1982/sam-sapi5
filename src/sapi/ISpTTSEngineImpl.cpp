#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#include "ISpTTSEngineImpl.hpp"
#include "sam_log.hpp"
#include "sam_paths.hpp"
#include "utils.hpp"

namespace SamVoice {
namespace sapi {
namespace {

// SAM renders at 22050 Hz; 16-bit is what SAPI hosts expect to be handed.
constexpr WORD AUDIO_CHANNELS = 1;
constexpr DWORD AUDIO_SAMPLE_RATE = sam::SAMPLE_RATE;
constexpr WORD AUDIO_BITS_PER_SAMPLE = 16;

constexpr int SAPI_RATE_MIN = -10;
constexpr int SAPI_RATE_MAX = 10;

std::once_flag g_dictionary_once;
std::size_t g_dictionary_words = 0;

// --- text preparation ------------------------------------------------------

const char* letter_name(wchar_t c)
{
    switch (towlower(c)) {
    case L'a': return "ay";    case L'b': return "bee";   case L'c': return "see";
    case L'd': return "dee";   case L'e': return "ee";    case L'f': return "ef";
    case L'g': return "jee";   case L'h': return "aitch"; case L'i': return "eye";
    case L'j': return "jay";   case L'k': return "kay";   case L'l': return "el";
    case L'm': return "em";    case L'n': return "en";    case L'o': return "oh";
    case L'p': return "pee";   case L'q': return "cue";   case L'r': return "ar";
    case L's': return "ess";   case L't': return "tee";   case L'u': return "you";
    case L'v': return "vee";   case L'w': return "double you";
    case L'x': return "ex";    case L'y': return "why";   case L'z': return "zee";
    case L'0': return "zero";  case L'1': return "one";   case L'2': return "two";
    case L'3': return "three"; case L'4': return "four";  case L'5': return "five";
    case L'6': return "six";   case L'7': return "seven"; case L'8': return "eight";
    case L'9': return "nine";
    default: return nullptr;
    }
}

const char* punctuation_name(wchar_t c)
{
    switch (c) {
    case L' ':  return "space";        case L'.': return "dot";
    case L',':  return "comma";        case L'?': return "question";
    case L'!':  return "exclamation";  case L'-': return "dash";
    case L'_':  return "underscore";   case L'/': return "slash";
    case L'\\': return "backslash";    case L'@': return "at";
    case L'#':  return "number";       case L'$': return "dollar";
    case L'%':  return "percent";      case L'&': return "and";
    case L'*':  return "star";         case L'+': return "plus";
    case L'=':  return "equals";       case L':': return "colon";
    case L';':  return "semicolon";    case L'\'': return "apostrophe";
    case L'"':  return "quote";        case L'(': return "left paren";
    case L')':  return "right paren";  case L'<': return "less than";
    case L'>':  return "greater than"; case L'[': return "left bracket";
    case L']':  return "right bracket";
    default: return nullptr;
    }
}

// Renders text one character at a time, for <spell> / SPVA_SpellOut.
std::string spell_out(const wchar_t* text, ULONG len)
{
    std::string out;
    for (ULONG i = 0; i < len; ++i) {
        const wchar_t c = text[i];
        const char* name = letter_name(c);
        if (!name) {
            name = punctuation_name(c);
        }
        if (name) {
            out += name;
        } else {
            out += utils::wstring_to_string(&c, 1);
        }
        // A comma between characters makes SAM pause instead of running them
        // together into one word.
        out += ", ";
    }
    return out;
}

// Splits text into sentence-sized chunks. Rendering a whole sentence at once
// keeps SAM's punctuation-driven inflection intact, while still yielding
// often enough to stay responsive to stop requests.
std::vector<std::string> split_sentences(const std::string& text)
{
    std::vector<std::string> out;
    std::size_t start = 0;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '.' && c != '!' && c != '?') {
            continue;
        }
        // Only break when whitespace or end-of-text follows, so "3.14" and
        // "N.V.D.A." are not chopped apart.
        std::size_t j = i + 1;
        while (j < text.size() && (text[j] == '.' || text[j] == '!' || text[j] == '?' ||
                                   text[j] == '"' || text[j] == '\'' || text[j] == ')')) {
            ++j;
        }
        if (j < text.size() && !std::isspace(static_cast<unsigned char>(text[j]))) {
            continue;
        }
        out.push_back(text.substr(start, j - start));
        while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) {
            ++j;
        }
        start = j;
        i = j > 0 ? j - 1 : j;
    }

    if (start < text.size()) {
        out.push_back(text.substr(start));
    }

    // Guard against a single unpunctuated wall of text: cap chunk length.
    constexpr std::size_t MAX_CHUNK = 400;
    std::vector<std::string> capped;
    for (const std::string& s : out) {
        if (s.size() <= MAX_CHUNK) {
            capped.push_back(s);
            continue;
        }
        std::size_t pos = 0;
        while (pos < s.size()) {
            std::size_t end = (std::min)(pos + MAX_CHUNK, s.size());
            if (end < s.size()) {
                // Back off to the last space so words stay whole.
                const std::size_t space = s.rfind(' ', end);
                if (space != std::string::npos && space > pos) {
                    end = space;
                }
            }
            capped.push_back(s.substr(pos, end - pos));
            pos = end;
            while (pos < s.size() && s[pos] == ' ') {
                ++pos;
            }
        }
    }
    return capped;
}

// --- parameter mapping -----------------------------------------------------

// SAPI rate -10..+10 -> engine speed. The voice's configured speed is rate 0,
// so a user's own speed setting stays the baseline.
int map_rate(int sapi_rate, int base_speed, const settings::Settings& s)
{
    sapi_rate = std::max(SAPI_RATE_MIN, std::min(SAPI_RATE_MAX, sapi_rate));

    double speed;
    if (sapi_rate >= 0) {
        // Faster: engine speed values go down.
        const double t = sapi_rate / 10.0;
        speed = base_speed + t * (s.rate_min_speed - base_speed);
    } else {
        const double t = -sapi_rate / 10.0;
        speed = base_speed + t * (s.rate_max_speed - base_speed);
    }
    return std::max(1, std::min(255, static_cast<int>(std::lround(speed))));
}

// SAPI pitch is in semitone-ish steps. SAM's pitch value is a period: smaller
// means higher, so a rise divides it.
int map_pitch(int base_pitch, int sapi_pitch_adj)
{
    sapi_pitch_adj = std::max(-24, std::min(24, sapi_pitch_adj));
    if (sapi_pitch_adj == 0) {
        return std::max(1, std::min(255, base_pitch));
    }
    const double scaled = base_pitch / std::pow(2.0, sapi_pitch_adj / 12.0);
    return std::max(1, std::min(255, static_cast<int>(std::lround(scaled))));
}

// --- output ----------------------------------------------------------------

struct SpeakContext
{
    ISpTTSEngineSite* site = nullptr;
    ULONGLONG bytes_written = 0;
    bool aborted = false;
};

// True to keep going, false when the host asked us to stop or skip.
bool check_actions(SpeakContext& ctx)
{
    const DWORD actions = ctx.site->GetActions();
    if (actions & SPVES_ABORT) {
        SAM_DEBUG("host requested abort");
        ctx.aborted = true;
        return false;
    }
    if (actions & SPVES_SKIP) {
        SAM_DEBUG("host requested skip");
        ctx.site->CompleteSkip(0);
        ctx.aborted = true;
        return false;
    }
    return true;
}

bool write_samples(SpeakContext& ctx, const std::vector<std::int16_t>& samples)
{
    if (samples.empty()) {
        return true;
    }
    const BYTE* ptr = reinterpret_cast<const BYTE*>(samples.data());
    const ULONG total = static_cast<ULONG>(samples.size() * sizeof(std::int16_t));

    if (!check_actions(ctx)) {
        return false;
    }

    // ISpTTSEngineSite::Write consumes the whole buffer and blocks until the
    // host has room, so a success means everything went out. pcbWritten is
    // passed as null deliberately: SAPI does not reliably set it, and reading
    // it back as zero would look like a short write and truncate the speech.
    const HRESULT hr = ctx.site->Write(ptr, total, nullptr);
    if (FAILED(hr)) {
        SAM_ERROR("site->Write failed for %lu bytes: 0x%08lX", total,
                  static_cast<unsigned long>(hr));
        return false;
    }

    ctx.bytes_written += total;
    return true;
}

// Emits `msecs` of digital silence, which is how SAPI expresses <silence/>.
bool write_silence(SpeakContext& ctx, long msecs)
{
    if (msecs <= 0) {
        return true;
    }
    const std::size_t count =
        static_cast<std::size_t>(AUDIO_SAMPLE_RATE) * static_cast<std::size_t>(msecs) / 1000;
    const std::vector<std::int16_t> silence(count, 0);
    return write_samples(ctx, silence);
}

void add_event(SpeakContext& ctx, SPEVENTENUM id, SPEVENTLPARAMTYPE param_type,
               WPARAM wparam, LPARAM lparam)
{
    // eEventId and elParamType are enum bitfields, so assign the enums directly.
    SPEVENT event = {};
    event.eEventId = id;
    event.elParamType = param_type;
    event.ulStreamNum = 0;
    event.ullAudioStreamOffset = ctx.bytes_written;
    event.wParam = wparam;
    event.lParam = lparam;
    ctx.site->AddEvents(&event, 1);
}

}  // namespace

ISpTTSEngineImpl::ISpTTSEngineImpl() = default;
ISpTTSEngineImpl::~ISpTTSEngineImpl() = default;

void ISpTTSEngineImpl::ensure_dictionary()
{
    std::call_once(g_dictionary_once, [] {
        const std::wstring path = paths::dictionary_path();
        if (path.empty()) {
            SAM_WARN("cmudict.txt not found next to the DLL; "
                     "falling back to the rule-based reciter");
            return;
        }
        g_dictionary_words = sam::load_dictionary(utils::wstring_to_string(path));
        SAM_INFO("dictionary loaded: %zu entries from %s", g_dictionary_words,
                 utils::wstring_to_string(path).c_str());
    });
}

void ISpTTSEngineImpl::refresh_settings()
{
    if (!settings_loaded_ || settings::changed_since(settings_stamp_)) {
        settings_ = settings::load();
        settings_loaded_ = true;
        log::set_level(settings_.log_level);
        SAM_INFO("settings loaded (volume=%d, expand_numbers=%d, log level=%d)",
                 settings_.volume, settings_.expand_numbers ? 1 : 0,
                 static_cast<int>(settings_.log_level));
    }
}

STDMETHODIMP ISpTTSEngineImpl::SetObjectToken(ISpObjectToken* pToken)
{
    if (!pToken) {
        return E_INVALIDARG;
    }

    try {
        ISpDataKeyPtr attr;
        if (FAILED(pToken->OpenKey(L"Attributes", &attr))) {
            SAM_ERROR("SetObjectToken: cannot open Attributes");
            return E_INVALIDARG;
        }

        // Prefer the stable id we put on the token; fall back to the display
        // name for tokens created by something else.
        int index = -1;
        utils::out_ptr<wchar_t> id(CoTaskMemFree);
        if (SUCCEEDED(attr->GetStringValue(L"SAMVoice", id.address()))) {
            index = sam::find_voice(utils::wstring_to_string(id.get()).c_str());
        }
        if (index < 0) {
            utils::out_ptr<wchar_t> name(CoTaskMemFree);
            if (SUCCEEDED(attr->GetStringValue(L"Name", name.address()))) {
                std::string n = utils::wstring_to_string(name.get());
                // Token names are prefixed "SAM "; strip it before matching.
                if (n.rfind("SAM ", 0) == 0) {
                    n = n.substr(4);
                }
                index = sam::find_voice(n.c_str());
            }
        }

        voice_index_ = (index >= 0) ? index : 0;
        token_ = pToken;
        SAM_INFO("voice selected: %s (index %d)", sam::VOICES[voice_index_].id, voice_index_);
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        SAM_ERROR("SetObjectToken: unexpected exception");
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpTTSEngineImpl::GetObjectToken(ISpObjectToken** ppToken)
{
    if (!ppToken) {
        return E_POINTER;
    }
    *ppToken = nullptr;
    if (!token_) {
        return E_UNEXPECTED;
    }
    token_.AddRef();
    *ppToken = token_.GetInterfacePtr();
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::GetOutputFormat(const GUID* /*pTargetFmtId*/,
                                               const WAVEFORMATEX* /*pTargetWaveFormatEx*/,
                                               GUID* pOutputFormatId,
                                               WAVEFORMATEX** ppCoMemOutputWaveFormatEx)
{
    if (!pOutputFormatId || !ppCoMemOutputWaveFormatEx) {
        return E_POINTER;
    }
    *pOutputFormatId = SPDFID_WaveFormatEx;
    *ppCoMemOutputWaveFormatEx = nullptr;

    auto* wfex = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!wfex) {
        return E_OUTOFMEMORY;
    }
    wfex->wFormatTag = WAVE_FORMAT_PCM;
    wfex->nChannels = AUDIO_CHANNELS;
    wfex->nSamplesPerSec = AUDIO_SAMPLE_RATE;
    wfex->wBitsPerSample = AUDIO_BITS_PER_SAMPLE;
    wfex->nBlockAlign = static_cast<WORD>(wfex->nChannels * wfex->wBitsPerSample / 8);
    wfex->nAvgBytesPerSec = wfex->nSamplesPerSec * wfex->nBlockAlign;
    wfex->cbSize = 0;

    *ppCoMemOutputWaveFormatEx = wfex;
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::Speak(DWORD dwSpeakFlags, REFGUID /*rguidFormatId*/,
                                     const WAVEFORMATEX* /*pWaveFormatEx*/,
                                     const SPVTEXTFRAG* pTextFragList,
                                     ISpTTSEngineSite* pOutputSite)
{
    if (!pTextFragList || !pOutputSite) {
        return E_INVALIDARG;
    }

    try {
        refresh_settings();
        ensure_dictionary();

        SAM_DEBUG("Speak: flags=0x%08lX voice=%s", static_cast<unsigned long>(dwSpeakFlags),
                  sam::VOICES[voice_index_].id);

        SpeakContext ctx;
        ctx.site = pOutputSite;

        ULONGLONG event_interest = 0;
        pOutputSite->GetEventInterest(&event_interest);
        const bool want_sentence = (event_interest & SPFEI(SPEI_SENTENCE_BOUNDARY)) != 0;
        const bool want_word = (event_interest & SPFEI(SPEI_WORD_BOUNDARY)) != 0;

        const settings::VoiceSettings& vs = settings_.voice(voice_index_);

        long sapi_rate = 0;
        pOutputSite->GetRate(&sapi_rate);
        USHORT sapi_volume = 100;
        pOutputSite->GetVolume(&sapi_volume);

        for (const SPVTEXTFRAG* frag = pTextFragList; frag; frag = frag->pNext) {
            if (!check_actions(ctx)) {
                break;
            }

            // Pick up live rate/volume changes between fragments.
            const DWORD actions = pOutputSite->GetActions();
            if (actions & SPVES_RATE) {
                pOutputSite->GetRate(&sapi_rate);
            }
            if (actions & SPVES_VOLUME) {
                pOutputSite->GetVolume(&sapi_volume);
            }

            if (frag->State.eAction == SPVA_Bookmark) {
                std::wstring text;
                if (frag->ulTextLen > 0 && frag->pTextStart) {
                    text.assign(frag->pTextStart, frag->ulTextLen);
                }
                long id = 0;
                try {
                    id = std::stol(text);
                } catch (...) {
                    id = 0;
                }
                // lParam must stay valid until AddEvents returns; `text` does.
                add_event(ctx, SPEI_TTS_BOOKMARK, SPET_LPARAM_IS_STRING,
                          static_cast<WPARAM>(id),
                          reinterpret_cast<LPARAM>(text.c_str()));
                SAM_DEBUG("bookmark %ld at byte %llu", id, ctx.bytes_written);
                continue;
            }

            if (frag->State.eAction == SPVA_Silence) {
                SAM_DEBUG("silence %ld ms", frag->State.SilenceMSecs);
                if (!write_silence(ctx, frag->State.SilenceMSecs)) {
                    break;
                }
                continue;
            }

            if (frag->State.eAction != SPVA_Speak &&
                frag->State.eAction != SPVA_SpellOut &&
                frag->State.eAction != SPVA_Pronounce) {
                continue;
            }
            if (frag->ulTextLen == 0 || !frag->pTextStart) {
                continue;
            }

            // Build the text this fragment contributes.
            std::string text;
            if (frag->State.eAction == SPVA_SpellOut) {
                text = spell_out(frag->pTextStart, frag->ulTextLen);
            } else {
                text = utils::wstring_to_string(frag->pTextStart, frag->ulTextLen);
            }
            if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
                continue;
            }
            if (settings_.expand_numbers) {
                text = sam::expand_numbers(text);
            }

            // Per-fragment parameter overrides ride on top of the stream values.
            sam::VoiceParams params = settings_.params_for(voice_index_);
            params.speed = map_rate(static_cast<int>(sapi_rate) + frag->State.RateAdj,
                                    vs.speed, settings_);
            params.pitch = map_pitch(vs.pitch, frag->State.PitchAdj.MiddleAdj);

            const int volume = static_cast<int>(sapi_volume) * frag->State.Volume *
                               settings_.volume / 10000;

            SAM_DEBUG("fragment: %zu chars, speed=%d pitch=%d mouth=%d throat=%d "
                      "inflection=%d sing=%d volume=%d",
                      text.size(), params.speed, params.pitch, params.mouth,
                      params.throat, params.inflection, params.singmode ? 1 : 0, volume);

            if (want_sentence) {
                add_event(ctx, SPEI_SENTENCE_BOUNDARY, SPET_LPARAM_IS_UNDEFINED,
                          static_cast<WPARAM>(frag->ulTextLen),
                          static_cast<LPARAM>(frag->ulTextSrcOffset));
            }
            if (want_word) {
                // Word offsets are reported against the start of the fragment's
                // audio; SAM does not expose per-word timing.
                const wchar_t* p = frag->pTextStart;
                bool in_word = false;
                ULONG word_start = 0;
                for (ULONG i = 0; i <= frag->ulTextLen; ++i) {
                    const bool is_word = i < frag->ulTextLen &&
                                         (iswalnum(p[i]) || p[i] == L'\'' || p[i] == L'-');
                    if (is_word && !in_word) {
                        word_start = i;
                        in_word = true;
                    } else if (!is_word && in_word) {
                        add_event(ctx, SPEI_WORD_BOUNDARY, SPET_LPARAM_IS_UNDEFINED,
                                  static_cast<WPARAM>(i - word_start),
                                  static_cast<LPARAM>(frag->ulTextSrcOffset + word_start));
                        in_word = false;
                    }
                }
            }

            // Render sentence by sentence: keeps SAM's inflection intact while
            // bounding both latency and peak memory.
            for (const std::string& chunk : split_sentences(text)) {
                if (!check_actions(ctx)) {
                    break;
                }
                const std::vector<std::uint8_t> audio = sam::text_to_audio(chunk, params);
                if (audio.empty()) {
                    SAM_WARN("chunk produced no audio: \"%.60s\"", chunk.c_str());
                    continue;
                }
                if (!write_samples(ctx, sam::to_pcm16(audio, volume))) {
                    break;
                }
            }

            if (ctx.aborted) {
                break;
            }
        }

        SAM_DEBUG("Speak: done, %llu bytes written%s", ctx.bytes_written,
                  ctx.aborted ? " (aborted)" : "");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        SAM_ERROR("Speak: out of memory");
        return E_OUTOFMEMORY;
    }
    catch (const std::exception& e) {
        SAM_ERROR("Speak: %s", e.what());
        return E_UNEXPECTED;
    }
    catch (...) {
        SAM_ERROR("Speak: unexpected exception");
        return E_UNEXPECTED;
    }
}

}  // namespace sapi
}  // namespace SamVoice
