#include "sam_paths.hpp"

#include <shlobj.h>
#include <vector>

namespace SamVoice {
namespace paths {
namespace {

HMODULE g_module = nullptr;

std::wstring known_folder(REFKNOWNFOLDERID id)
{
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &raw))) {
        return std::wstring();
    }
    std::wstring result(raw);
    CoTaskMemFree(raw);
    return result;
}

std::wstring parent_of(const std::wstring& path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(0, slash);
}

bool file_exists(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// %APPDATA%\SoftwareAutomaticMouth or %LOCALAPPDATA%\SoftwareAutomaticMouth
std::wstring app_folder(REFKNOWNFOLDERID id)
{
    const std::wstring base = known_folder(id);
    if (base.empty()) {
        return std::wstring();
    }
    return base + L"\\SoftwareAutomaticMouth";
}

}  // namespace

void set_module(HMODULE module)
{
    g_module = module;
}

std::wstring module_dir()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(g_module, buffer.data(),
                                           static_cast<DWORD>(buffer.size()));
        if (n == 0) {
            return std::wstring();
        }
        if (n < buffer.size() - 1) {
            break;
        }
        buffer.resize(buffer.size() * 2);  // path was truncated; retry larger
    }
    return parent_of(std::wstring(buffer.data()));
}

std::wstring data_dir()
{
    const std::wstring dir = module_dir();
    if (dir.empty()) {
        return dir;
    }
    // The 32-bit DLL is installed under <root>\x86; the dictionary sits in
    // <root> so both architectures share one copy.
    if (file_exists(dir + L"\\cmudict.txt")) {
        return dir;
    }
    const std::wstring up = parent_of(dir);
    if (!up.empty() && file_exists(up + L"\\cmudict.txt")) {
        return up;
    }
    return dir;
}

std::wstring dictionary_path()
{
    const std::wstring candidate = data_dir() + L"\\cmudict.txt";
    return file_exists(candidate) ? candidate : std::wstring();
}

std::wstring settings_path()
{
    const std::wstring folder = app_folder(FOLDERID_RoamingAppData);
    if (folder.empty()) {
        return std::wstring();
    }
    return folder + L"\\settings.ini";
}

std::wstring log_dir()
{
    const std::wstring folder = app_folder(FOLDERID_LocalAppData);
    if (folder.empty()) {
        return std::wstring();
    }
    return folder + L"\\logs";
}

bool ensure_directory(const std::wstring& dir)
{
    if (dir.empty()) {
        return false;
    }
    const DWORD attrs = GetFileAttributesW(dir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return true;
    }
    const std::wstring parent = parent_of(dir);
    if (parent != dir && !parent.empty()) {
        ensure_directory(parent);
    }
    return CreateDirectoryW(dir.c_str(), nullptr) != 0 ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

}  // namespace paths
}  // namespace SamVoice
