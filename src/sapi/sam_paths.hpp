// Locating the files the engine needs, without consulting the registry.
#pragma once

#include <string>
#include <windows.h>

namespace SamVoice {
namespace paths {

// Directory containing the currently-executing module (the SAPI DLL, or the
// config utility's exe). Always ends without a trailing backslash.
std::wstring module_dir();

// Where the installed data files live (cmudict.txt and friends). This is the
// module directory; for the 32-bit DLL installed under <root>\x86 it walks one
// level up so both architectures share a single dictionary.
std::wstring data_dir();

// Full path to cmudict.txt, or an empty string if it cannot be found.
std::wstring dictionary_path();

// Per-user settings file: %APPDATA%\SoftwareAutomaticMouth\settings.ini
std::wstring settings_path();

// Per-user log directory: %LOCALAPPDATA%\SoftwareAutomaticMouth\logs
std::wstring log_dir();

// Creates `dir` and any missing parents. Returns true if it exists afterwards.
bool ensure_directory(const std::wstring& dir);

// Records the module handle so module_dir() can resolve it. Called from DllMain
// or from the utility's entry point.
void set_module(HMODULE module);

}  // namespace paths
}  // namespace SamVoice
