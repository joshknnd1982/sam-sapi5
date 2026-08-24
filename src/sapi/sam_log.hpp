// Debug logging for the SAPI5 driver and the configuration utility.
//
// Logs go to %LOCALAPPDATA%\SoftwareAutomaticMouth\logs\, one file per
// component per architecture, so a 32-bit host and a 64-bit host running at the
// same time do not interleave into one file. Logging is off unless enabled in
// settings.ini, and rotates at 4 MB.
#pragma once

#include <string>

namespace SamVoice {
namespace log {

enum class Level
{
    Off = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Debug = 4,
    Trace = 5,
};

// `component` names the log file, e.g. "sapi5" or "config".
void init(const wchar_t* component);
void shutdown();

void set_level(Level level);
Level level();
inline bool enabled(Level l) { return l <= level(); }

// Full path of the active log file (empty when logging is off).
std::wstring current_file();

void write(Level level, const char* format, ...);

}  // namespace log
}  // namespace SamVoice

// Guard the argument evaluation so disabled logging costs a comparison.
#define SAM_LOG(lvl, ...)                                              \
    do {                                                               \
        if (::SamVoice::log::enabled(::SamVoice::log::Level::lvl)) {    \
            ::SamVoice::log::write(::SamVoice::log::Level::lvl, __VA_ARGS__); \
        }                                                              \
    } while (0)

#define SAM_ERROR(...) SAM_LOG(Error, __VA_ARGS__)
#define SAM_WARN(...)  SAM_LOG(Warn, __VA_ARGS__)
#define SAM_INFO(...)  SAM_LOG(Info, __VA_ARGS__)
#define SAM_DEBUG(...) SAM_LOG(Debug, __VA_ARGS__)
#define SAM_TRACE(...) SAM_LOG(Trace, __VA_ARGS__)
