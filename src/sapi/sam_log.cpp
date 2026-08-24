#include "sam_log.hpp"
#include "sam_paths.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace SamVoice {
namespace log {
namespace {

constexpr long long ROTATE_BYTES = 4LL * 1024 * 1024;

std::mutex g_mutex;
std::wstring g_path;
Level g_level = Level::Off;
bool g_initialised = false;

const char* level_name(Level l)
{
    switch (l) {
    case Level::Error: return "ERROR";
    case Level::Warn:  return "WARN ";
    case Level::Info:  return "INFO ";
    case Level::Debug: return "DEBUG";
    case Level::Trace: return "TRACE";
    default:           return "?????";
    }
}

// Rotates the log to a .1 backup once it passes ROTATE_BYTES.
void rotate_if_needed(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        return;
    }
    const long long size =
        (static_cast<long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (size < ROTATE_BYTES) {
        return;
    }
    const std::wstring backup = path + L".1";
    DeleteFileW(backup.c_str());
    MoveFileW(path.c_str(), backup.c_str());
}

}  // namespace

void init(const wchar_t* component)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    const std::wstring dir = paths::log_dir();
    if (dir.empty() || !paths::ensure_directory(dir)) {
        g_path.clear();
        g_initialised = true;
        return;
    }

#ifdef _WIN64
    const wchar_t* arch = L"x64";
#else
    const wchar_t* arch = L"x86";
#endif

    g_path = dir + L"\\" + component + L"-" + arch + L".log";
    g_initialised = true;
}

void shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path.clear();
    g_level = Level::Off;
    g_initialised = false;
}

void set_level(Level l)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_level = l;
}

Level level()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_level;
}

std::wstring current_file()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return (g_level == Level::Off) ? std::wstring() : g_path;
}

void write(Level l, const char* format, ...)
{
    std::string message;
    {
        va_list args;
        va_start(args, format);
        va_list copy;
        va_copy(copy, args);
        const int needed = std::vsnprintf(nullptr, 0, format, copy);
        va_end(copy);
        if (needed > 0) {
            message.resize(static_cast<std::size_t>(needed));
            std::vsnprintf(message.data(), message.size() + 1, format, args);
        }
        va_end(args);
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialised || g_path.empty() || l > g_level) {
        return;
    }

    rotate_if_needed(g_path);

    // Opened per write so a crash still leaves a complete log on disk.
    std::FILE* f = nullptr;
    if (_wfopen_s(&f, g_path.c_str(), L"a") != 0 || !f) {
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] %s %s\n",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 st.wMilliseconds, GetCurrentThreadId(), level_name(l),
                 message.c_str());
    std::fclose(f);
}

}  // namespace log
}  // namespace SamVoice
