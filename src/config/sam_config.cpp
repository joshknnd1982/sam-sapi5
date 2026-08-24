// SAM Voice Settings -- the configuration utility.
//
// Accessibility notes, since this is a screen-reader-first application:
//   * Every value is an edit control with a spin buddy rather than a trackbar.
//     MSAA reports a trackbar's position as a percentage, so a 0-255 pitch
//     would be announced as a meaningless number; an edit box announces the
//     real value and accepts typed input.
//   * Each control is immediately preceded in the dialog template by its label,
//     which is how MSAA derives an accessible name for it.
//   * Everything interactive has WS_TABSTOP and a keyboard mnemonic.
//   * Changes are written to disk as they are made, so the running SAPI voice
//     picks them up on the next utterance, and nothing is lost on close.
#include <windows.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "../engine/sam_engine.hpp"
#include "../sapi/sam_log.hpp"
#include "../sapi/sam_paths.hpp"
#include "../sapi/sam_settings.hpp"
#include "sam_config_ids.h"

using SamVoice::settings::Settings;
using SamVoice::settings::VoiceSettings;

namespace {

HINSTANCE g_instance = nullptr;
HWND g_dialog = nullptr;
Settings g_settings;
int g_voice = 0;

// Set while we are pushing values into controls, so the resulting EN_CHANGE
// notifications do not feed back as user edits.
bool g_loading = false;

// PlaySound with SND_MEMORY needs the buffer to stay alive for the whole
// playback, so it lives here rather than on the stack.
std::vector<std::uint8_t> g_playing;

struct SpinRange
{
    int edit;
    int spin;
    int lo;
    int hi;
};

const SpinRange SPINS[] = {
    {IDC_PITCH,      IDC_PITCH_SPIN,      0,   255},
    {IDC_SPEED,      IDC_SPEED_SPIN,      1,   255},
    {IDC_MOUTH,      IDC_MOUTH_SPIN,      0,   255},
    {IDC_THROAT,     IDC_THROAT_SPIN,     0,   255},
    {IDC_INFLECTION, IDC_INFLECTION_SPIN, 0,   100},
    {IDC_VOLUME,     IDC_VOLUME_SPIN,     0,   100},
    {IDC_SLOWEST,    IDC_SLOWEST_SPIN,    1,   255},
    {IDC_FASTEST,    IDC_FASTEST_SPIN,    1,   255},
};

std::wstring widen(const std::string& s)
{
    if (s.empty()) {
        return std::wstring();
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                      static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w)
{
    if (w.empty()) {
        return std::string();
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring get_text(int id)
{
    const int len = GetWindowTextLengthW(GetDlgItem(g_dialog, id));
    std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
    GetDlgItemTextW(g_dialog, id, text.data(), len + 1);
    text.resize(static_cast<std::size_t>(len));
    return text;
}

int get_value(int id, int lo, int hi, int fallback)
{
    const std::wstring text = get_text(id);
    if (text.empty()) {
        return fallback;
    }
    try {
        const int v = std::stoi(text);
        return (v < lo) ? lo : (v > hi ? hi : v);
    } catch (...) {
        return fallback;
    }
}

void set_value(int id, int value)
{
    SetDlgItemInt(g_dialog, id, static_cast<UINT>(value), TRUE);
}

// Announces a message both visually and to screen readers. Updating a static
// control's text does not raise a notification on its own, so nudge MSAA.
void set_status(const std::wstring& message)
{
    SetDlgItemTextW(g_dialog, IDC_STATUS, message.c_str());
    HWND status = GetDlgItem(g_dialog, IDC_STATUS);
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, status, OBJID_CLIENT, CHILDID_SELF);
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, status, OBJID_CLIENT, CHILDID_SELF);
}

// --- settings <-> controls -------------------------------------------------

void load_voice_into_controls()
{
    g_loading = true;
    const VoiceSettings& v = g_settings.voice(g_voice);
    set_value(IDC_PITCH, v.pitch);
    set_value(IDC_SPEED, v.speed);
    set_value(IDC_MOUTH, v.mouth);
    set_value(IDC_THROAT, v.throat);
    set_value(IDC_INFLECTION, v.inflection);
    CheckDlgButton(g_dialog, IDC_SINGMODE, v.singmode ? BST_CHECKED : BST_UNCHECKED);
    g_loading = false;
}

void load_globals_into_controls()
{
    g_loading = true;
    set_value(IDC_VOLUME, g_settings.volume);
    set_value(IDC_SLOWEST, g_settings.rate_max_speed);
    set_value(IDC_FASTEST, g_settings.rate_min_speed);
    CheckDlgButton(g_dialog, IDC_EXPAND_NUMBERS,
                   g_settings.expand_numbers ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(g_dialog, IDC_LOGLEVEL, CB_SETCURSEL,
                        static_cast<WPARAM>(g_settings.log_level), 0);
    g_loading = false;
}

// Pulls every control's current value back into g_settings.
void read_controls()
{
    VoiceSettings& v = g_settings.voice(g_voice);
    v.pitch = get_value(IDC_PITCH, 0, 255, v.pitch);
    v.speed = get_value(IDC_SPEED, 1, 255, v.speed);
    v.mouth = get_value(IDC_MOUTH, 0, 255, v.mouth);
    v.throat = get_value(IDC_THROAT, 0, 255, v.throat);
    v.inflection = get_value(IDC_INFLECTION, 0, 100, v.inflection);
    v.singmode = IsDlgButtonChecked(g_dialog, IDC_SINGMODE) == BST_CHECKED;

    g_settings.volume = get_value(IDC_VOLUME, 0, 100, g_settings.volume);
    g_settings.rate_max_speed = get_value(IDC_SLOWEST, 1, 255, g_settings.rate_max_speed);
    g_settings.rate_min_speed = get_value(IDC_FASTEST, 1, 255, g_settings.rate_min_speed);
    g_settings.expand_numbers =
        IsDlgButtonChecked(g_dialog, IDC_EXPAND_NUMBERS) == BST_CHECKED;
    g_settings.default_voice = sam::VOICES[g_voice].id;

    const LRESULT sel = SendDlgItemMessageW(g_dialog, IDC_LOGLEVEL, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR) {
        g_settings.log_level = static_cast<SamVoice::log::Level>(sel);
    }
}

// Saves immediately, so the change is live for any running SAPI host.
void apply_now(bool quiet = false)
{
    read_controls();
    if (SamVoice::settings::save(g_settings)) {
        if (!quiet) {
            set_status(L"Saved. New settings apply to the next thing spoken.");
        }
    } else {
        set_status(L"Could not save settings. Check that your user profile is writable.");
    }
}

// --- preview ---------------------------------------------------------------

void stop_playback()
{
    PlaySoundW(nullptr, nullptr, SND_PURGE);
    g_playing.clear();
}

void speak_test()
{
    read_controls();

    std::string text = narrow(get_text(IDC_TESTTEXT));
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        text = "This is the SAM voice speaking your current settings.";
    }
    if (g_settings.expand_numbers) {
        text = sam::expand_numbers(text);
    }

    set_status(L"Rendering...");
    const sam::VoiceParams params = g_settings.params_for(g_voice);
    const std::vector<std::uint8_t> audio = sam::text_to_audio(text, params);
    if (audio.empty()) {
        set_status(L"Nothing to speak for that phrase.");
        return;
    }

    stop_playback();
    g_playing = sam::audio_to_wav(audio);
    if (!PlaySoundW(reinterpret_cast<LPCWSTR>(g_playing.data()), nullptr,
                    SND_MEMORY | SND_ASYNC | SND_NODEFAULT)) {
        set_status(L"Could not play audio on the default device.");
        return;
    }

    wchar_t buffer[160];
    swprintf_s(buffer, L"Speaking %s: pitch %d, speed %d, mouth %d, throat %d.",
               widen(sam::VOICES[g_voice].display).c_str(), params.pitch,
               params.speed, params.mouth, params.throat);
    set_status(buffer);
}

// --- dialog ----------------------------------------------------------------

void populate_lists()
{
    for (int i = 0; i < sam::VOICE_COUNT; ++i) {
        SendDlgItemMessageW(g_dialog, IDC_VOICE, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(
                                widen(sam::VOICES[i].display).c_str()));
    }
    static const wchar_t* const LEVELS[] = {
        L"Off", L"Errors", L"Warnings", L"Information", L"Debug", L"Trace"};
    for (const wchar_t* level : LEVELS) {
        SendDlgItemMessageW(g_dialog, IDC_LOGLEVEL, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(level));
    }
}

void init_spins()
{
    for (const SpinRange& s : SPINS) {
        HWND spin = GetDlgItem(g_dialog, s.spin);
        SendMessageW(spin, UDM_SETBUDDY,
                     reinterpret_cast<WPARAM>(GetDlgItem(g_dialog, s.edit)), 0);
        SendMessageW(spin, UDM_SETRANGE32, static_cast<WPARAM>(s.lo),
                     static_cast<LPARAM>(s.hi));
        // The spin arrows are reachable from the edit box with the arrow keys,
        // so keep them out of the tab order.
        SetWindowLongPtrW(spin, GWL_STYLE,
                          GetWindowLongPtrW(spin, GWL_STYLE) & ~WS_TABSTOP);
    }
    // Cap typed input at three digits so a stray keypress cannot create a value
    // far outside the range before it is clamped.
    for (const SpinRange& s : SPINS) {
        SendDlgItemMessageW(g_dialog, s.edit, EM_SETLIMITTEXT, 3, 0);
    }
}

INT_PTR CALLBACK dialog_proc(HWND dlg, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG: {
        g_dialog = dlg;

        HICON icon = LoadIconW(g_instance, MAKEINTRESOURCEW(IDI_APP));
        if (icon) {
            SendMessageW(dlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            SendMessageW(dlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        }

        populate_lists();
        init_spins();

        g_settings = SamVoice::settings::load();
        const int index = sam::find_voice(g_settings.default_voice.c_str());
        g_voice = (index >= 0) ? index : 0;
        SendDlgItemMessageW(dlg, IDC_VOICE, CB_SETCURSEL, static_cast<WPARAM>(g_voice), 0);

        load_voice_into_controls();
        load_globals_into_controls();
        SetDlgItemTextW(dlg, IDC_TESTTEXT,
                        L"Hello, my name is SAM, the Software Automatic Mouth.");

        // Load the dictionary so the preview pronounces words properly.
        const std::wstring dict = SamVoice::paths::dictionary_path();
        if (!dict.empty()) {
            sam::load_dictionary(narrow(dict));
        }

        set_status(dict.empty()
            ? L"Ready. Dictionary not found, so pronunciation uses spelling rules."
            : L"Ready. Changes are saved as you make them.");
        return TRUE;
    }

    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int code = HIWORD(wparam);

        switch (id) {
        case IDC_VOICE:
            if (code == CBN_SELCHANGE) {
                // Keep the outgoing voice's edits before switching.
                read_controls();
                SamVoice::settings::save(g_settings);
                const LRESULT sel = SendDlgItemMessageW(dlg, IDC_VOICE, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) {
                    g_voice = static_cast<int>(sel);
                }
                load_voice_into_controls();
                g_settings.default_voice = sam::VOICES[g_voice].id;
                SamVoice::settings::save(g_settings);
                set_status(widen(std::string(sam::VOICES[g_voice].display) +
                                 " selected."));
            }
            return TRUE;

        case IDC_LOGLEVEL:
            if (code == CBN_SELCHANGE) {
                apply_now(true);
                set_status(L"Logging level changed.");
            }
            return TRUE;

        case IDC_PITCH: case IDC_SPEED: case IDC_MOUTH: case IDC_THROAT:
        case IDC_INFLECTION: case IDC_VOLUME: case IDC_SLOWEST: case IDC_FASTEST:
            if (code == EN_CHANGE && !g_loading) {
                apply_now(true);
            }
            return TRUE;

        case IDC_SINGMODE:
        case IDC_EXPAND_NUMBERS:
            if (code == BN_CLICKED && !g_loading) {
                apply_now(true);
                set_status(id == IDC_SINGMODE
                    ? (IsDlgButtonChecked(dlg, IDC_SINGMODE) == BST_CHECKED
                           ? L"Sing mode on." : L"Sing mode off.")
                    : (IsDlgButtonChecked(dlg, IDC_EXPAND_NUMBERS) == BST_CHECKED
                           ? L"Numbers will be read as words."
                           : L"Numbers will be read as digits."));
            }
            return TRUE;

        case IDC_TEST:
            speak_test();
            return TRUE;

        case IDC_STOP:
            stop_playback();
            set_status(L"Stopped.");
            return TRUE;

        case IDC_RESET_VOICE: {
            const sam::VoiceParams& p = sam::VOICES[g_voice].params;
            VoiceSettings& v = g_settings.voice(g_voice);
            v = VoiceSettings{p.pitch, p.speed, p.mouth, p.throat, p.inflection,
                              p.singmode};
            load_voice_into_controls();
            apply_now(true);
            set_status(widen(std::string(sam::VOICES[g_voice].display) +
                             " reset to its default settings."));
            return TRUE;
        }

        case IDC_RESET_ALL: {
            const int answer = MessageBoxW(dlg,
                L"Reset every voice and every global setting to the defaults?",
                L"SAM Voice Settings", MB_YESNO | MB_ICONQUESTION);
            if (answer == IDYES) {
                g_settings.reset();
                g_voice = 0;
                SendDlgItemMessageW(dlg, IDC_VOICE, CB_SETCURSEL, 0, 0);
                load_voice_into_controls();
                load_globals_into_controls();
                apply_now(true);
                set_status(L"All settings reset to the defaults.");
            }
            return TRUE;
        }

        case IDC_OPEN_LOGS: {
            const std::wstring dir = SamVoice::paths::log_dir();
            SamVoice::paths::ensure_directory(dir);
            ShellExecuteW(dlg, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            set_status(L"Opened the log folder.");
            return TRUE;
        }

        case IDOK:
        case IDCANCEL:
            stop_playback();
            apply_now(true);
            EndDialog(dlg, 0);
            return TRUE;

        default:
            break;
        }
        return FALSE;
    }

    case WM_CLOSE:
        stop_playback();
        apply_now(true);
        EndDialog(dlg, 0);
        return TRUE;

    default:
        break;
    }
    return FALSE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    g_instance = instance;
    SamVoice::paths::set_module(instance);
    SamVoice::log::init(L"config");
    SamVoice::log::set_level(SamVoice::settings::load().log_level);
    SAM_INFO("settings utility started");

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, dialog_proc, 0);

    SAM_INFO("settings utility exiting");
    SamVoice::log::shutdown();
    return 0;
}
