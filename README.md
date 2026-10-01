# SAM Voice for SAPI5

A Windows SAPI5 speech engine for **SAM (Software Automatic Mouth)** — the
speech synthesizer released for the Commodore 64, Apple II and Atari in 1982.

Six voices, 32-bit and 64-bit, no SAPI4 anywhere, and no engine data in the
registry. Once installed, SAM shows up as an ordinary Windows voice in any
program that speaks: screen readers, document readers, anything using the
Windows speech interface.

![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-blue)
![Arch](https://img.shields.io/badge/arch-x86%20%2B%20x64-brightgreen)
![SAPI](https://img.shields.io/badge/SAPI-5-orange)

---

## What SAM is

SAM was written by Mark Barton and published by Don't Ask Software in 1982. It
was one of the first speech synthesizers cheap enough to run on a home computer,
and it does the whole job in software — no speech chip, no recorded samples.

It works by **formant synthesis**: instead of stitching together recordings of a
human voice, it generates the resonances of a vocal tract directly. Two sine
oscillators and one rectangular oscillator produce the first three formants, and
their phase is reset on every glottal pulse to imitate vocal-cord buzz. That is
why SAM sounds the way it does — thin, buzzy, and unmistakable.

The pipeline has four stages:

1. **Reciter** — English text to phonemes, using the CMU Pronouncing Dictionary
   with SAM's original letter-to-sound rules as fallback.
2. **Parser** — phonemes to timed frames: rewriting rules, stress propagation,
   and phoneme-length adjustment.
3. **Renderer** — the formant synthesizer.
4. **Output** — 8-bit unsigned PCM at 22050 Hz, converted to 16-bit for SAPI.

Because the voice is generated rather than recorded, the vocal tract itself is
adjustable. That is what the `mouth` and `throat` parameters do, and it is why
SAM can be made to sound like an elf or an alien by changing four numbers.

### The six voices

| Voice | Pitch | Speed | Mouth | Throat |
|---|---|---|---|---|
| SAM Sam | 64 | 72 | 128 | 128 |
| SAM Elf | 64 | 72 | 110 | 160 |
| SAM Little Robot | 60 | 92 | 190 | 190 |
| SAM Stuffy Guy | 72 | 82 | 110 | 105 |
| SAM Little Old Lady | 32 | 72 | 145 | 145 |
| SAM Extra Terrestrial | 64 | 100 | 150 | 200 |

These are the six presets SAM ships with. They are parameter combinations rather
than separate voice data, and every one of those numbers is adjustable.

### Language

**English only.** SAM shipped no other language. Neither the CMU dictionary, the
letter-to-sound rules, nor the number expansion has anything but English in it,
and the 1982 original was English-only too. Adding another language would mean
writing a new grapheme-to-phoneme front end, not enabling an existing one.

---

## Installing

Download `SamVoiceSAPI5_Setup.exe` from the
[Releases](../../releases) page and run it.

Setup needs administrator rights, because SAPI only looks for speech engines
under `HKEY_LOCAL_MACHINE`. It installs:

- the 64-bit engine, into `C:\Program Files\SAM Voice`
- the 32-bit engine, into `C:\Program Files\SAM Voice\x86`, so 32-bit programs
  see the voices too
- the pronunciation dictionary and the settings utility
- a desktop shortcut and Start menu entries

Requires 64-bit Windows 10 or 11.

Silent install:

```bash
SamVoiceSAPI5_Setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

To remove it, use Add or Remove Programs. The uninstaller unregisters both
engines before deleting anything. Your settings and logs are left in place.

---

## Using the voices

After installing, the six SAM voices appear anywhere Windows speech is offered.
In NVDA: Preferences → Settings → Speech → Synthesizer → Microsoft Speech API
version 5.

To adjust how they sound, open **SAM Voice Settings** from the desktop or the
Start menu:

| Setting | Range | What it does |
|---|---|---|
| Pitch | 0–255 | Lower sounds **higher** — the number is a pitch period, not a frequency. |
| Speed | 1–255 | Lower is **faster**. |
| Mouth | 0–255 | Scales the first formant. |
| Throat | 0–255 | Scales the second formant. |
| Inflection | 0–100 | 0 is a monotone, 50 normal, 100 dramatic. |
| Sing mode | on/off | Holds pitch steady across vowels. |
| Volume | 0–100 | Applied on top of whatever the program asks for. |
| Numbers as words | on/off | "sixty" versus "six zero". |
| Rate range | 1–255 | Engine speeds used at the slowest and fastest SAPI rates. |

Settings are saved as you type them and take effect on the **next thing spoken**
— no need to restart your screen reader. They are stored per voice, so tuning
Elf leaves Sam alone.

### Accessibility

The utility was built screen-reader-first:

- Values are **edit boxes with spin buttons, never sliders**. MSAA reports a
  slider's position as a percentage, so a pitch of 64 out of 255 would be
  announced as "25" — useless. An edit box announces the real number and lets
  you type one.
- Every control is immediately preceded in the dialog template by its label,
  which is how MSAA derives an accessible name.
- Everything interactive is in the tab order and has a keyboard mnemonic.

This is checked automatically — see [Testing](#testing).

### Logging

Logging is off by default. Turn it on with the "Log detail" list in the utility,
then use "Open log folder":

```
%LOCALAPPDATA%\SoftwareAutomaticMouth\logs\
  sapi5-x64.log     from 64-bit programs
  sapi5-x86.log     from 32-bit programs
  config-x64.log    from the settings utility
```

The installer writes its own log to `install-log.txt` in the install folder,
including the exit code of each registration step.

---

## Registry use

The engine reads **nothing** from the registry. The only entries written are the
three SAPI requires in order to find an engine at all:

```
HKLM\Software\Classes\CLSID\{326f55f7-…}              the TTS engine
HKLM\Software\Classes\CLSID\{7baac040-…}              the voice enumerator
HKLM\Software\Microsoft\Speech\Voices\TokenEnums\SAM  points SAPI at it
```

Voices are enumerated from code, and settings live in
`%APPDATA%\SoftwareAutomaticMouth\settings.ini`. The TokenEnums key has to be
under HKLM — SAPI ignores one placed in HKCU.

---

## Building

Needs Visual Studio 2022 Build Tools (both x86 and x64 toolchains), CMake 3.15+,
Inno Setup 6, and Python 3 (only for regenerating tables and running the
verification).

```bash
build_all.bat
```

That regenerates the engine tables from the Python reference, builds both
architectures, verifies the C++ engine against that reference, and produces
`output\SamVoiceSAPI5_Setup.exe`.

To build by hand:

```bash
cmake -A x64 -S . -B build_x64 && cmake --build build_x64 --config Release
cmake -A Win32 -S . -B build_x86 && cmake --build build_x86 --config Release
```

### Layout

| Path | What it is |
|---|---|
| `src/engine/` | The synthesizer. Portable C++, no Windows or COM dependency. |
| `src/sapi/` | The SAPI5 engine DLL: COM plumbing, voice tokens, settings, logging. |
| `src/config/` | The settings utility. |
| `bin/` | The Python SAM implementation, kept as the reference. |
| `tools/` | Table generator, verification scripts, test harnesses. |
| `installer/` | Inno Setup script and the end-user readme. |

The data tables in `src/engine/sam_tables.cpp` are **generated** by
`tools/gen_tables.py` from the Python tables. Don't edit them by hand.

---

## Testing

```bash
python tools\verify_engine.py
```

Renders 12 texts across 11 parameter sets in both Python and C++ and compares
the raw PCM. The C++ engine is **byte-identical** to the Python reference —
132/132 renders, and identical between x86 and x64.

```bash
build_dev\x64\sam_sapi_test.exe build_x64\bin\Release\SamVoiceSAPI.dll samples\sapi
```

Drives the real `ISpTTSEngine::Speak` through a mock `ISpTTSEngineSite`. No
registration and no admin rights needed. Checks voice enumeration, output
format, events, abort responsiveness, live settings reload, and total audio
duration.

```bash
python tools\check_config_a11y.py
```

Walks the settings dialog's MSAA tree with oleacc and checks that every
focusable control has an accessible name, is in the tab order, and is not a
percentage-reporting slider. oleacc rather than UI Automation on purpose: a UIA
client reports plain Win32 controls as generic panes and would hide exactly the
problems worth catching.

```bash
build_x64\bin\Release\sam_settings_test.exe
```

Round-trips every setting through the INI file, and checks clamping and
recovery from a corrupt file.

### Two bugs these caught

**`ISpTTSEngineSite::Write` does not reliably set `pcbWritten`.** Reading it back
as zero looks like a short write. That silently dropped every audio chunk after
the first, turning a two-sentence utterance into one sentence with no error
reported to the host. Pass `nullptr` and treat success as "all bytes went out".
A mock site that helpfully sets `pcbWritten = cb` hides this completely, so the
harness here deliberately leaves it untouched and asserts on duration.

**`text_to_phonemes` emits double spaces between words.** The Python appends an
empty string for whitespace tokens, so `' '.join(...)` produces two spaces. Those
extra spaces are phoneme 0 and affect timing — dropping them shortens the audio.
Only a byte-exact comparison catches that.

---

## Credits and licensing

- **SAM (Software Automatic Mouth)** was created by Mark Barton and published by
  Don't Ask Software in 1982.
- The Python implementation in `bin/` was taken from an NVDA add-on. It carried
  **no licence header or attribution**, and its author is not identified in the
  source. It is included here because the build regenerates the engine tables
  from it and the test suite verifies against it. If you are its author and want
  it credited differently or removed, please open an issue.
- `bin/cmudict.txt` is the **CMU Pronouncing Dictionary**, from Carnegie Mellon
  University, which is freely redistributable.
- The COM and SAPI plumbing in `src/sapi/` (the class factory, registry helpers,
  voice tokens and token enumerator) was adapted from
  [gozaltech/BstSpeech-sapi](https://github.com/gozaltech/BstSpeech-sapi); the
  files are listed in [NOTICE.md](NOTICE.md).
- Apart from that plumbing, the C++ engine, SAPI5 driver, settings utility,
  installer and tests in this repository were written for this project.

The settings utility (`src/config/`), the driver's logging, path and settings
code (`src/sapi/sam_log.*`, `src/sapi/sam_paths.*`, `src/sapi/sam_settings.*`),
the installer (`installer/`), the tools and tests (`tools/`) and the build
scripts are licensed under the MIT License (see [LICENSE](LICENSE)).

The SAM-derived material is not covered by that licence, and no licence is
claimed for it here, because the provenance of the Python reference is unknown.
That is the Python implementation in `bin/` and what is ported or generated from
it: the C++ engine in `src/engine/` (its source files describe it as a port of
the Python implementation) and the engine tables in `src/engine/sam_tables.cpp`,
generated from the Python tables. The CMU Pronouncing Dictionary in
`bin/cmudict.txt` and the SAPI5 plumbing files listed in
[NOTICE.md](NOTICE.md) are not covered either. Treat this as a preservation and
accessibility project.
