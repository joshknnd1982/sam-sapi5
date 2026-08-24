// DLL entry points and COM self-registration for the SAM SAPI5 voice.
//
// Registration writes two CLSID entries plus one TokenEnums entry -- that is
// the minimum SAPI requires to find an engine. The speech engine itself reads
// nothing from the registry: voices, parameters and settings all come from the
// code and from settings.ini.
#include <mutex>
#include <new>
#include <sapi.h>

#include "IEnumSpObjectTokensImpl.hpp"
#include "ISpTTSEngineImpl.hpp"
#include "com.hpp"
#include "registry.hpp"
#include "sam_log.hpp"
#include "sam_paths.hpp"
#include "sam_settings.hpp"

namespace {

HINSTANCE g_dll_handle = nullptr;
SamVoice::com::class_object_factory g_cls_obj_factory;

// SAPI only consults TokenEnums under HKLM, so registration needs elevation.
const std::wstring token_enums_path = L"Software\\Microsoft\\Speech\\Voices\\TokenEnums";
const std::wstring token_enums_name = L"SAM";

// Logging and settings both resolve known folders through the shell, which can
// load DLLs. Doing that from DllMain runs it under the loader lock and risks a
// deadlock, so initialisation is deferred to the first call that arrives on a
// normal thread. A hung screen reader is the worst possible failure here.
std::once_flag g_init_once;

void ensure_initialised()
{
    std::call_once(g_init_once, [] {
        SamVoice::log::init(L"sapi5");
        SamVoice::log::set_level(SamVoice::settings::load().log_level);
        SAM_INFO("SAM SAPI5 engine initialised (%s)",
                 sizeof(void*) == 8 ? "x64" : "x86");
    });
}

[[nodiscard]] std::wstring clsid_to_string(const GUID& clsid)
{
    wchar_t buf[64];
    StringFromGUID2(clsid, buf, 64);
    return std::wstring(buf);
}

void register_token_enumerator()
{
    using namespace SamVoice::sapi;
    using namespace SamVoice::registry;

    const std::wstring clsid_str = clsid_to_string(__uuidof(IEnumSpObjectTokensImpl));

    key enums_key(HKEY_LOCAL_MACHINE, token_enums_path,
                  KEY_CREATE_SUB_KEY | KEY_SET_VALUE, true);
    key enum_key(enums_key, token_enums_name, KEY_SET_VALUE, true);

    enum_key.set(L"SAM (Software Automatic Mouth) Voices");
    enum_key.set(L"CLSID", clsid_str);
}

void unregister_token_enumerator() noexcept
{
    using namespace SamVoice::registry;

    try {
        key enums_key(HKEY_LOCAL_MACHINE, token_enums_path, KEY_ALL_ACCESS);
        enums_key.delete_subkey(token_enums_name);
    }
    catch (...) {
    }
}

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_dll_handle = hInstance;
        DisableThreadLibraryCalls(hInstance);

        // Only cheap, loader-lock-safe work belongs here.
        SamVoice::paths::set_module(hInstance);

        try {
            g_cls_obj_factory.register_class<SamVoice::sapi::IEnumSpObjectTokensImpl>();
            g_cls_obj_factory.register_class<SamVoice::sapi::ISpTTSEngineImpl>();
        }
        catch (...) {
            return FALSE;
        }
    }
    else if (dwReason == DLL_PROCESS_DETACH) {
        // Nothing to do: the log opens and closes its file per write, so there
        // is no handle to release, and touching the shell here is unsafe.
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    ensure_initialised();
    return g_cls_obj_factory.create(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return SamVoice::com::object_counter::is_zero() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    ensure_initialised();
    try {
        SamVoice::com::class_registrar r(g_dll_handle);
        r.register_class<SamVoice::sapi::IEnumSpObjectTokensImpl>();
        r.register_class<SamVoice::sapi::ISpTTSEngineImpl>();
        register_token_enumerator();
        SAM_INFO("DllRegisterServer succeeded");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        SAM_ERROR("DllRegisterServer failed");
        return E_UNEXPECTED;
    }
}

STDAPI DllUnregisterServer()
{
    ensure_initialised();
    try {
        unregister_token_enumerator();
        SamVoice::com::class_registrar r(g_dll_handle);
        r.unregister_class<SamVoice::sapi::IEnumSpObjectTokensImpl>();
        r.unregister_class<SamVoice::sapi::ISpTTSEngineImpl>();
        SAM_INFO("DllUnregisterServer succeeded");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}
