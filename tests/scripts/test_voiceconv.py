#!/usr/bin/env python3
"""tests/scripts/test_voiceconv.py

Pure-Python, no-emulator unit test for tools/oric_voiceconv.py (the `make
test-voiceconv` target -- see Makefile), matching this repo's existing
"no test framework, standalone argparse-driven scripts" convention
(tests/scripts/test_pictconv.py).

Two checks:

1. Byte-exact conversion test against a small, synthetic WAV built
   in-memory (no checked-in binary fixture needed) -- 5 samples with
   hand-picked values (-32768, -16384, 0, 16384, 32767) at a source rate
   equal to the target --rate, so neither resampling interpolation nor
   silence-trimming affects the result and every output byte can be
   hand-verified: quantize_4bit()'s round((sample+1.0)/2.0*15.0) maps
   these to [0, 4, 8, 11, 15] exactly (peak sample is -32768/32768 = -1.0
   exactly, so normalize_peak() is a no-op here too).

2. The real, load-bearing safety assertion this project's own
   docs/voice.md commits to: VOICE_SAMPLE_MAX_SIZE (include/voice.h) must
   stay <= 7731 bytes ($FA00 minus assets/boulesetbits.aky's own end
   address -- see that doc's "Memory budget and address" section for the
   full reasoning), and BOTH voice clips' real sizes -- assets/
   voice_welcome.bin ("Welcome to Oric Atmos", played from
   section_logo.c) and assets/voice_thanks.bin ("Thanks for watching",
   played from section_credits.c) -- must each individually
   stay <= VOICE_SAMPLE_MAX_SIZE. The two clips share one fixed overlay-RAM
   address (they're never resident/playing at the same time) and are no
   longer required to match VOICE_SAMPLE_MAX_SIZE exactly, unlike the
   original single-clip design -- each just needs to fit under the shared
   ceiling. This is the actual enforcement mechanism, not just a comment --
   re-run this test after ever regenerating either voice asset or changing
   which/how many music tracks this project ships.
"""

import re
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
CONVERTER = REPO_ROOT / "tools" / "oric_voiceconv.py"
VOICE_H = REPO_ROOT / "include" / "voice.h"
VOICE_CLIPS = {
    "voice_welcome.bin": REPO_ROOT / "assets" / "voice_welcome.bin",
    "voice_thanks.bin": REPO_ROOT / "assets" / "voice_thanks.bin",
}

# $FA00 minus assets/boulesetbits.aky's own end address ($C000 + 7117 =
# $DBCD) -- see docs/voice.md for the full derivation. The larger of the
# two real music tracks, not whichever one happens to be resident when a
# given clip plays -- this is what makes VOICE_SAMPLE_ADDR safe regardless
# of which track is loaded when voice_play() runs.
VOICE_SAMPLE_SIZE_CEILING = 0xFA00 - (0xC000 + 7117)

SYNTH_SAMPLES = [-32768, -16384, 0, 16384, 32767]
SYNTH_RATE = 100
EXPECTED_BYTES = bytes([0, 4, 8, 11, 15])


def write_synth_wav(path):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SYNTH_RATE)
        w.writeframes(struct.pack("<%dh" % len(SYNTH_SAMPLES), *SYNTH_SAMPLES))


def test_conversion(tmp_dir):
    wav_path = Path(tmp_dir) / "synth.wav"
    out_path = Path(tmp_dir) / "out.bin"
    write_synth_wav(wav_path)

    result = subprocess.run(
        [sys.executable, str(CONVERTER), str(wav_path), str(out_path),
         "--rate", str(SYNTH_RATE), "--silence-threshold", "0.0"],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print("  [FAIL] byte-exact conversion (synthetic 5-sample WAV) -- converter exited non-zero")
        print(result.stderr)
        return False

    actual = out_path.read_bytes()
    if actual == EXPECTED_BYTES:
        print(f"  [PASS] byte-exact conversion (synthetic 5-sample WAV) ({len(actual)} bytes)")
        return True

    print(f"  [FAIL] byte-exact conversion -- expected {list(EXPECTED_BYTES)}, got {list(actual)}")
    return False


def test_max_bytes_enforcement(tmp_dir):
    # A clip that would exceed --max-bytes must fail loudly, not truncate.
    wav_path = Path(tmp_dir) / "synth.wav"
    out_path = Path(tmp_dir) / "out_should_not_exist.bin"
    write_synth_wav(wav_path)

    result = subprocess.run(
        [sys.executable, str(CONVERTER), str(wav_path), str(out_path),
         "--rate", str(SYNTH_RATE), "--silence-threshold", "0.0", "--max-bytes", "3"],
        capture_output=True, text=True,
    )
    if result.returncode == 0:
        print("  [FAIL] --max-bytes enforcement -- converter should have exited non-zero (5 samples > cap of 3)")
        return False
    if out_path.exists():
        print("  [FAIL] --max-bytes enforcement -- converter must not write a truncated output file on failure")
        return False

    print("  [PASS] --max-bytes enforcement (fails loudly, no partial output file)")
    return True


def test_voice_sample_size_ceiling():
    if not VOICE_H.exists():
        print(f"  [FAIL] VOICE_SAMPLE_MAX_SIZE ceiling -- {VOICE_H} not found")
        return False

    text = VOICE_H.read_text()
    m = re.search(r"#define\s+VOICE_SAMPLE_MAX_SIZE\s+(\d+)U?", text)
    if not m:
        print(f"  [FAIL] VOICE_SAMPLE_MAX_SIZE ceiling -- could not find '#define VOICE_SAMPLE_MAX_SIZE' in {VOICE_H}")
        return False
    declared_max = int(m.group(1))

    if declared_max > VOICE_SAMPLE_SIZE_CEILING:
        print(f"  [FAIL] VOICE_SAMPLE_MAX_SIZE ceiling -- {VOICE_H} declares {declared_max} bytes, "
              f"exceeds the safe ceiling of {VOICE_SAMPLE_SIZE_CEILING} bytes (see docs/voice.md)")
        return False

    all_ok = True
    for name, bin_path in VOICE_CLIPS.items():
        if not bin_path.exists():
            print(f"  [FAIL] VOICE_SAMPLE_MAX_SIZE ceiling -- {bin_path} not found")
            all_ok = False
            continue
        real_size = bin_path.stat().st_size
        if real_size > declared_max:
            print(f"  [FAIL] VOICE_SAMPLE_MAX_SIZE ceiling -- {name} is {real_size} bytes, "
                  f"exceeds VOICE_SAMPLE_MAX_SIZE ({declared_max} bytes)")
            all_ok = False
        else:
            print(f"  [PASS] {name} fits under VOICE_SAMPLE_MAX_SIZE ({real_size} <= {declared_max} bytes)")

    if not all_ok:
        return False

    print(f"  [PASS] VOICE_SAMPLE_MAX_SIZE ceiling ({declared_max} bytes <= {VOICE_SAMPLE_SIZE_CEILING} byte "
          f"hardware-safe ceiling)")
    return True


def main():
    print("=" * 63)
    print("  oric_voiceconv.py -- unit tests")
    print("=" * 63)
    print()

    pass_count = 0
    fail_count = 0
    with tempfile.TemporaryDirectory() as tmp:
        for check in (lambda: test_conversion(tmp), lambda: test_max_bytes_enforcement(tmp)):
            if check():
                pass_count += 1
            else:
                fail_count += 1

    if test_voice_sample_size_ceiling():
        pass_count += 1
    else:
        fail_count += 1

    print()
    print("=" * 63)
    print(f"  Results: {pass_count} passed, {fail_count} failed")
    print("=" * 63)

    return 1 if fail_count else 0


if __name__ == "__main__":
    sys.exit(main())
