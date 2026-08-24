// The SAPI5 TTS engine. Runs the SAM synthesizer in-process; identical code
// serves the 32-bit and 64-bit builds.
#pragma once

#include <string>
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <comdef.h>
#include <comip.h>

#include "com.hpp"
#include "sam_settings.hpp"
#include "voice_attributes.hpp"

namespace SamVoice {
namespace sapi {

class __declspec(uuid("326f55f7-5d3d-4276-a65e-f5445a6978b0")) ISpTTSEngineImpl :
    public ISpTTSEngine, public ISpObjectWithToken
{
public:
    ISpTTSEngineImpl();
    ~ISpTTSEngineImpl();

    ISpTTSEngineImpl(const ISpTTSEngineImpl&) = delete;
    ISpTTSEngineImpl& operator=(const ISpTTSEngineImpl&) = delete;

    STDMETHOD(Speak)(DWORD dwSpeakFlags, REFGUID rguidFormatId,
                     const WAVEFORMATEX* pWaveFormatEx,
                     const SPVTEXTFRAG* pTextFragList,
                     ISpTTSEngineSite* pOutputSite) override;
    STDMETHOD(GetOutputFormat)(const GUID* pTargetFmtId,
                               const WAVEFORMATEX* pTargetWaveFormatEx,
                               GUID* pOutputFormatId,
                               WAVEFORMATEX** ppCoMemOutputWaveFormatEx) override;

    STDMETHOD(SetObjectToken)(ISpObjectToken* pToken) override;
    STDMETHOD(GetObjectToken)(ISpObjectToken** ppToken) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        void* ptr = com::try_primary_interface<ISpTTSEngine>(this, riid);
        return ptr ? ptr : com::try_interface<ISpObjectWithToken>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpDataKey, __uuidof(ISpDataKey));

    // Loads the dictionary once per process, and re-reads settings.ini whenever
    // it has changed on disk so the utility's edits apply immediately.
    void refresh_settings();
    void ensure_dictionary();

    ISpObjectTokenPtr token_;
    int voice_index_ = 0;

    settings::Settings settings_;
    FILETIME settings_stamp_{};
    bool settings_loaded_ = false;
};

}  // namespace sapi
}  // namespace SamVoice
