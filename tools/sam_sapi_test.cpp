// End-to-end test of the SAPI5 DLL that needs no registration and no admin
// rights: it loads the DLL, asks it for class objects directly, enumerates the
// voice tokens, and drives ISpTTSEngine::Speak through a mock ISpTTSEngineSite
// that captures the audio to a WAV file.
//
//   sam_sapi_test <path-to-SamVoiceSAPI.dll> <output-dir> [text]
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <shlobj.h>
#include <sperror.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Must match the CLSIDs declared in the DLL's headers.
// {7baac040-6018-482c-ab1b-7d7f6d391bb8}
const CLSID CLSID_SamEnumerator = {
    0x7baac040, 0x6018, 0x482c, {0xab, 0x1b, 0x7d, 0x7f, 0x6d, 0x39, 0x1b, 0xb8}};
// {326f55f7-5d3d-4276-a65e-f5445a6978b0}
const CLSID CLSID_SamEngine = {
    0x326f55f7, 0x5d3d, 0x4276, {0xa6, 0x5e, 0xf5, 0x44, 0x5a, 0x69, 0x78, 0xb0}};

int g_failures = 0;

void fail(const char* what, HRESULT hr)
{
    std::fprintf(stderr, "FAIL: %s (hr=0x%08lX)\n", what, static_cast<unsigned long>(hr));
    ++g_failures;
}

// Collects everything the engine writes, plus the events it raises.
class MockSite : public ISpTTSEngineSite
{
public:
    MockSite(long rate, USHORT volume) : rate_(rate), volume_(volume) {}

    // --- IUnknown ---
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, __uuidof(ISpTTSEngineSite)) ||
            IsEqualIID(riid, __uuidof(ISpEventSink))) {
            *ppv = static_cast<ISpTTSEngineSite*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
    STDMETHOD_(ULONG, Release)() override { return --refs_; }

    // --- ISpEventSink ---
    STDMETHOD(AddEvents)(const SPEVENT* events, ULONG count) override
    {
        for (ULONG i = 0; i < count; ++i) {
            ++event_count_;
            switch (events[i].eEventId) {
            case SPEI_WORD_BOUNDARY:     ++word_events_; break;
            case SPEI_SENTENCE_BOUNDARY: ++sentence_events_; break;
            case SPEI_TTS_BOOKMARK:      ++bookmark_events_; break;
            default: break;
            }
        }
        return S_OK;
    }
    STDMETHOD(GetEventInterest)(ULONGLONG* interest) override
    {
        if (!interest) return E_POINTER;
        *interest = SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_SENTENCE_BOUNDARY) |
                    SPFEI(SPEI_TTS_BOOKMARK);
        return S_OK;
    }

    // --- ISpTTSEngineSite ---
    STDMETHOD_(DWORD, GetActions)() override { return actions_; }

    STDMETHOD(Write)(const void* buffer, ULONG cb, ULONG* written) override
    {
        const BYTE* p = static_cast<const BYTE*>(buffer);
        audio_.insert(audio_.end(), p, p + cb);
        // Real SAPI consumes the whole buffer but does NOT reliably set
        // pcbWritten. Leaving it untouched here keeps the engine honest: an
        // engine that trusts this value will truncate against real SAPI.
        (void)written;
        return S_OK;
    }
    STDMETHOD(GetRate)(long* rate) override
    {
        if (!rate) return E_POINTER;
        *rate = rate_;
        return S_OK;
    }
    STDMETHOD(GetVolume)(USHORT* volume) override
    {
        if (!volume) return E_POINTER;
        *volume = volume_;
        return S_OK;
    }
    STDMETHOD(GetSkipInfo)(SPVSKIPTYPE* type, long* items) override
    {
        if (type) *type = SPVST_SENTENCE;
        if (items) *items = 0;
        return S_OK;
    }
    STDMETHOD(CompleteSkip)(long /*skipped*/) override { return S_OK; }

    void request_abort() { actions_ |= SPVES_ABORT; }

    const std::vector<BYTE>& audio() const { return audio_; }
    ULONG events() const { return event_count_; }
    ULONG words() const { return word_events_; }
    ULONG sentences() const { return sentence_events_; }
    ULONG bookmarks() const { return bookmark_events_; }

private:
    ULONG refs_ = 1;
    DWORD actions_ = SPVES_CONTINUE;
    long rate_ = 0;
    USHORT volume_ = 100;
    std::vector<BYTE> audio_;
    ULONG event_count_ = 0, word_events_ = 0, sentence_events_ = 0, bookmark_events_ = 0;
};

bool write_wav(const std::string& path, const std::vector<BYTE>& pcm,
               const WAVEFORMATEX& fmt)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    const DWORD data_size = static_cast<DWORD>(pcm.size());
    const DWORD riff_size = data_size + 36;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff_size, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    const DWORD fmt_size = 16;
    std::fwrite(&fmt_size, 4, 1, f);
    std::fwrite(&fmt.wFormatTag, 2, 1, f);
    std::fwrite(&fmt.nChannels, 2, 1, f);
    std::fwrite(&fmt.nSamplesPerSec, 4, 1, f);
    std::fwrite(&fmt.nAvgBytesPerSec, 4, 1, f);
    std::fwrite(&fmt.nBlockAlign, 2, 1, f);
    std::fwrite(&fmt.wBitsPerSample, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data_size, 4, 1, f);
    std::fwrite(pcm.data(), 1, pcm.size(), f);
    std::fclose(f);
    return true;
}

std::string narrow(const wchar_t* w)
{
    if (!w) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) {
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    }
    return s;
}

std::string attribute(ISpObjectToken* token, const wchar_t* name)
{
    ISpDataKey* attrs = nullptr;
    if (FAILED(token->OpenKey(L"Attributes", &attrs)) || !attrs) {
        return std::string();
    }
    wchar_t* value = nullptr;
    std::string result;
    if (SUCCEEDED(attrs->GetStringValue(name, &value)) && value) {
        result = narrow(value);
        CoTaskMemFree(value);
    }
    attrs->Release();
    return result;
}

using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);

template <typename T>
T* create(DllGetClassObjectFn get_class_object, REFCLSID clsid, const char* what)
{
    IClassFactory* factory = nullptr;
    HRESULT hr = get_class_object(clsid, IID_IClassFactory,
                                  reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        fail(what, hr);
        return nullptr;
    }
    T* object = nullptr;
    hr = factory->CreateInstance(nullptr, __uuidof(T), reinterpret_cast<void**>(&object));
    factory->Release();
    if (FAILED(hr) || !object) {
        fail(what, hr);
        return nullptr;
    }
    return object;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: sam_sapi_test <SamVoiceSAPI.dll> <output-dir> [text]\n");
        return 2;
    }
    const std::string dll_path = argv[1];
    const std::string out_dir = argv[2];
    const std::string text = (argc > 3)
        ? argv[3]
        : "Hello, my name is SAM, the Software Automatic Mouth. "
          "I am a speech synthesizer from 1982.";

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        fail("CoInitializeEx", hr);
        return 1;
    }

    HMODULE dll = LoadLibraryA(dll_path.c_str());
    if (!dll) {
        std::fprintf(stderr, "FAIL: cannot load %s (error %lu)\n",
                     dll_path.c_str(), GetLastError());
        return 1;
    }
    auto get_class_object =
        reinterpret_cast<DllGetClassObjectFn>(GetProcAddress(dll, "DllGetClassObject"));
    if (!get_class_object) {
        std::fprintf(stderr, "FAIL: DllGetClassObject not exported\n");
        return 1;
    }
    std::printf("loaded %s\n\n", dll_path.c_str());

    // --- enumerate the voices -------------------------------------------
    auto* tokens = create<IEnumSpObjectTokens>(get_class_object, CLSID_SamEnumerator,
                                               "create token enumerator");
    if (!tokens) {
        return 1;
    }

    ULONG count = 0;
    hr = tokens->GetCount(&count);
    if (FAILED(hr)) {
        fail("GetCount", hr);
        return 1;
    }
    std::printf("voices reported by the enumerator: %lu\n", count);

    const std::wstring wide_text(text.begin(), text.end());

    for (ULONG i = 0; i < count; ++i) {
        ISpObjectToken* token = nullptr;
        hr = tokens->Item(i, &token);
        if (FAILED(hr) || !token) {
            fail("Item", hr);
            continue;
        }

        const std::string name = attribute(token, L"Name");
        const std::string id = attribute(token, L"SAMVoice");
        const std::string gender = attribute(token, L"Gender");
        const std::string age = attribute(token, L"Age");
        const std::string language = attribute(token, L"Language");

        std::printf("\n[%lu] %-24s id=%-18s %s/%s lang=%s\n", i, name.c_str(),
                    id.c_str(), gender.c_str(), age.c_str(), language.c_str());

        auto* engine = create<ISpTTSEngine>(get_class_object, CLSID_SamEngine,
                                            "create TTS engine");
        if (!engine) {
            token->Release();
            continue;
        }

        ISpObjectWithToken* with_token = nullptr;
        if (SUCCEEDED(engine->QueryInterface(__uuidof(ISpObjectWithToken),
                                             reinterpret_cast<void**>(&with_token)))) {
            hr = with_token->SetObjectToken(token);
            if (FAILED(hr)) {
                fail("SetObjectToken", hr);
            }
            with_token->Release();
        } else {
            fail("QueryInterface(ISpObjectWithToken)", E_NOINTERFACE);
        }

        GUID format_id = {};
        WAVEFORMATEX* format = nullptr;
        hr = engine->GetOutputFormat(nullptr, nullptr, &format_id, &format);
        if (FAILED(hr) || !format) {
            fail("GetOutputFormat", hr);
            engine->Release();
            token->Release();
            continue;
        }
        std::printf("     format: %lu Hz, %u-bit, %u channel(s)\n",
                    format->nSamplesPerSec, format->wBitsPerSample, format->nChannels);

        SPVTEXTFRAG frag = {};
        frag.pNext = nullptr;
        frag.State.eAction = SPVA_Speak;
        frag.State.LangID = 409;
        frag.State.EmphAdj = 0;
        frag.State.RateAdj = 0;
        frag.State.Volume = 100;
        frag.State.PitchAdj.MiddleAdj = 0;
        frag.State.PitchAdj.RangeAdj = 0;
        frag.State.SilenceMSecs = 0;
        frag.pTextStart = wide_text.c_str();
        frag.ulTextLen = static_cast<ULONG>(wide_text.size());
        frag.ulTextSrcOffset = 0;

        MockSite site(0, 100);
        hr = engine->Speak(0, GUID_NULL, nullptr, &frag, &site);
        if (FAILED(hr)) {
            fail("Speak", hr);
        }

        const double seconds =
            format->nAvgBytesPerSec
                ? static_cast<double>(site.audio().size()) / format->nAvgBytesPerSec
                : 0.0;
        std::printf("     spoke %zu bytes (%.2f s), events: %lu total "
                    "(%lu word, %lu sentence, %lu bookmark)\n",
                    site.audio().size(), seconds, site.events(), site.words(),
                    site.sentences(), site.bookmarks());

        // The sample text is two sentences. The engine renders them as separate
        // chunks, so anything much shorter than the full duration means a chunk
        // was dropped -- which is exactly how a short-write bug shows up.
        if (site.audio().empty()) {
            fail("Speak produced no audio", E_FAIL);
        } else if (seconds < 8.0) {
            std::fprintf(stderr,
                         "FAIL: only %.2f s of audio; expected the whole passage "
                         "(a chunk was dropped)\n", seconds);
            ++g_failures;
        } else {
            const std::string path = out_dir + "\\sapi_" +
                                     (id.empty() ? std::to_string(i) : id) + ".wav";
            if (write_wav(path, site.audio(), *format)) {
                std::printf("     wrote %s\n", path.c_str());
            } else {
                fail("cannot write WAV", E_FAIL);
            }
        }

        CoTaskMemFree(format);
        engine->Release();
        token->Release();
    }

    // --- abort must stop quickly ----------------------------------------
    std::printf("\n--- abort handling ---\n");
    {
        ISpObjectToken* token = nullptr;
        if (SUCCEEDED(tokens->Item(0, &token)) && token) {
            auto* engine = create<ISpTTSEngine>(get_class_object, CLSID_SamEngine,
                                                "create TTS engine for abort test");
            if (engine) {
                ISpObjectWithToken* with_token = nullptr;
                if (SUCCEEDED(engine->QueryInterface(
                        __uuidof(ISpObjectWithToken),
                        reinterpret_cast<void**>(&with_token)))) {
                    with_token->SetObjectToken(token);
                    with_token->Release();
                }

                const std::wstring long_text(
                    L"This is a long passage that the host aborts immediately. "
                    L"It should stop almost at once instead of rendering to the end. "
                    L"One two three four five six seven eight nine ten.");
                SPVTEXTFRAG frag = {};
                frag.State.eAction = SPVA_Speak;
                frag.State.Volume = 100;
                frag.State.LangID = 409;
                frag.pTextStart = long_text.c_str();
                frag.ulTextLen = static_cast<ULONG>(long_text.size());

                MockSite site(0, 100);
                site.request_abort();
                const DWORD t0 = GetTickCount();
                engine->Speak(0, GUID_NULL, nullptr, &frag, &site);
                const DWORD elapsed = GetTickCount() - t0;
                std::printf("aborted Speak returned in %lu ms with %zu bytes\n",
                            elapsed, site.audio().size());
                if (site.audio().size() > 0) {
                    fail("abort still produced audio", E_FAIL);
                }
                engine->Release();
            }
            token->Release();
        }
    }

    // --- settings changes must reach an already-running engine -----------
    std::printf("\n--- live settings reload ---\n");
    {
        wchar_t* appdata = nullptr;
        std::wstring ini;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))
            && appdata) {
            ini = std::wstring(appdata) + L"\\SoftwareAutomaticMouth";
            CreateDirectoryW(ini.c_str(), nullptr);
            ini += L"\\settings.ini";
            CoTaskMemFree(appdata);
        }

        ISpObjectToken* token = nullptr;
        if (!ini.empty() && SUCCEEDED(tokens->Item(0, &token)) && token) {
            auto* engine = create<ISpTTSEngine>(get_class_object, CLSID_SamEngine,
                                                "create engine for reload test");
            if (engine) {
                ISpObjectWithToken* with_token = nullptr;
                if (SUCCEEDED(engine->QueryInterface(
                        __uuidof(ISpObjectWithToken),
                        reinterpret_cast<void**>(&with_token)))) {
                    with_token->SetObjectToken(token);
                    with_token->Release();
                }

                const std::wstring phrase = L"Testing live settings.";
                SPVTEXTFRAG frag = {};
                frag.State.eAction = SPVA_Speak;
                frag.State.Volume = 100;
                frag.State.LangID = 409;
                frag.pTextStart = phrase.c_str();
                frag.ulTextLen = static_cast<ULONG>(phrase.size());

                // Preserve whatever the user already has, and restore it after.
                std::string saved;
                if (std::FILE* f = _wfopen(ini.c_str(), L"rb")) {
                    char buf[4096];
                    std::size_t n;
                    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
                        saved.append(buf, n);
                    }
                    std::fclose(f);
                }

                auto write_ini = [&](int pitch) {
                    std::FILE* f = _wfopen(ini.c_str(), L"wb");
                    if (!f) return false;
                    std::fprintf(f, "[global]\nvoice=sam\nvolume=100\n\n"
                                    "[voice.sam]\npitch=%d\nspeed=72\nmouth=128\n"
                                    "throat=128\ninflection=50\nsingmode=0\n", pitch);
                    std::fclose(f);
                    return true;
                };

                write_ini(64);
                MockSite first(0, 100);
                engine->Speak(0, GUID_NULL, nullptr, &frag, &first);

                // The driver compares the file's timestamp; make sure the second
                // write lands on a distinguishable one.
                Sleep(30);
                write_ini(20);   // a much higher-pitched voice

                MockSite second(0, 100);
                engine->Speak(0, GUID_NULL, nullptr, &frag, &second);

                std::printf("pitch 64 -> %zu bytes, pitch 20 -> %zu bytes\n",
                            first.audio().size(), second.audio().size());
                if (first.audio().empty() || second.audio().empty()) {
                    fail("live reload produced no audio", E_FAIL);
                } else if (first.audio() == second.audio()) {
                    fail("settings change did NOT reach the running engine", E_FAIL);
                } else {
                    std::printf("the same engine instance picked up the new pitch: OK\n");
                }

                // Put the user's own settings back.
                if (!saved.empty()) {
                    if (std::FILE* f = _wfopen(ini.c_str(), L"wb")) {
                        std::fwrite(saved.data(), 1, saved.size(), f);
                        std::fclose(f);
                    }
                } else {
                    DeleteFileW(ini.c_str());
                }
                engine->Release();
            }
            token->Release();
        }
    }

    tokens->Release();
    CoUninitialize();

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
