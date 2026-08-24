"""Verify the C++ engine renders byte-identically to the Python reference.

Runs the same texts and voice settings through both and compares the raw PCM.
Any mismatch is a porting bug.
"""
import os
import subprocess
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")
DICT = os.path.join(BIN, "cmudict.txt")
sys.path.insert(0, BIN)

from sam import SAM, VOICE_PRESETS          # noqa: E402
from reciter import expand_numbers, text_to_phonemes  # noqa: E402

# Prefer the CMake build; fall back to the quick dev build.
_CANDIDATES = [
    os.path.join(ROOT, "build_x64", "bin", "Release", "sam_render.exe"),
    os.path.join(ROOT, "build_dev", "x64", "sam_render.exe"),
]
EXE = next((p for p in _CANDIDATES if os.path.exists(p)), _CANDIDATES[0])
TMP = os.path.join(os.path.dirname(EXE), "verify.wav")

TEXTS = [
    "Hello, my name is SAM, the Software Automatic Mouth.",
    "I am a speech synthesizer from 1982.",
    "The quick brown fox jumps over the lazy dog.",
    "Testing one two three. Is this a question? Yes!",
    "Xylophone zephyr quagmire; syzygy, rhythm.",
    "3.14159 and -42 and 1000000",
    "supercalifragilisticexpialidocious",
    "a",
    "I. B. M. and N. V. D. A.",
    "It's a dog's life, isn't it?",
    "Colonel Worcestershire, Leicester Gloucester.",
    "AAAAA BBBBB 99999 !!!!! ?????",
]

# voice name -> params; plus some off-preset parameter combinations
CASES = []
for name, preset in VOICE_PRESETS.items():
    CASES.append((name, dict(preset), 50, False))
CASES.append(("edge_min", {"pitch": 0, "speed": 20, "mouth": 0, "throat": 0}, 0, False))
CASES.append(("edge_max", {"pitch": 255, "speed": 255, "mouth": 255, "throat": 255}, 100, False))
CASES.append(("sing", {"pitch": 64, "speed": 72, "mouth": 128, "throat": 128}, 50, True))
CASES.append(("mono", {"pitch": 100, "speed": 60, "mouth": 200, "throat": 90}, 0, False))
CASES.append(("dramatic", {"pitch": 40, "speed": 90, "mouth": 60, "throat": 220}, 100, False))


def cpp_render(text, params, inflection, singmode):
    cmd = [
        EXE, "--dict", DICT, "--text", text, "--out", TMP,
        "--pitch", str(params["pitch"]), "--speed", str(params["speed"]),
        "--mouth", str(params["mouth"]), "--throat", str(params["throat"]),
        "--inflection", str(inflection),
    ]
    if singmode:
        cmd.append("--singmode")
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode != 0:
        return None
    with wave.open(TMP) as w:
        return w.readframes(w.getnframes())


def cpp_phonemes(text):
    r = subprocess.run([EXE, "--dict", DICT, "--text", text, "--phonemes"],
                       capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def main():
    if not os.path.exists(EXE):
        print("build first: tools\\devbuild.bat x64")
        return 1

    # ---- stage 1: phoneme strings must match exactly --------------------
    print("=== phoneme conversion ===")
    ph_fail = 0
    for text in TEXTS:
        expanded = expand_numbers(text)
        want = text_to_phonemes(expanded)
        got = cpp_phonemes(expanded)
        if want != got:
            ph_fail += 1
            print("  MISMATCH %r" % text)
            print("    python: %r" % want)
            print("    c++   : %r" % got)
    print("  %d/%d matched" % (len(TEXTS) - ph_fail, len(TEXTS)))

    # ---- stage 2: rendered audio must match byte for byte ---------------
    print("=== rendered audio ===")
    total = 0
    fail = 0
    for text in TEXTS:
        expanded = expand_numbers(text)
        for name, params, inflection, singmode in CASES:
            total += 1
            s = SAM(inflection=inflection, singmode=singmode, **params)
            want = s.speak(expanded)
            want = bytes(want) if want else b""
            got = cpp_render(expanded, params, inflection, singmode)
            got = got or b""
            if want != got:
                fail += 1
                if len(want) != len(got):
                    detail = "length %d vs %d" % (len(want), len(got))
                else:
                    diff = [i for i in range(len(want)) if want[i] != got[i]]
                    detail = "%d/%d bytes differ, first at %d" % (
                        len(diff), len(want), diff[0])
                print("  MISMATCH [%s] %r: %s" % (name, text[:40], detail))
    print("  %d/%d matched" % (total - fail, total))

    print()
    if ph_fail == 0 and fail == 0:
        print("PASS - C++ output is byte-identical to the Python reference")
        return 0
    print("FAIL - %d phoneme, %d audio mismatches" % (ph_fail, fail))
    return 1


if __name__ == "__main__":
    sys.exit(main())
