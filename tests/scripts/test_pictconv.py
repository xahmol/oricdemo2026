#!/usr/bin/env python3
"""tests/scripts/test_pictconv.py

Pure-Python, no-emulator unit test for tools/oric_pictconv.py (the `make
test-pictconv` target -- see Makefile). Runs the converter against small
checked-in synthetic test images and diffs the output against checked-in
expected .bin fixtures, matching this repo's existing "no test framework,
standalone argparse-driven scripts" convention (tests/scripts/test_boot.sh,
oric_screen.py).

Fixtures:
  hires_test_input.png    240x200, left half (x=0-119) white, right half
                          (x=120-239) black -- mono mode with
                          ink=white/paper=black/dither=none should produce
                          column-bytes 0-19=0x7f (all-ink), 20-39=0x40
                          (all-paper) on every row. Also used to confirm
                          colored mode degenerates to the identical mono
                          output when the image only ever uses the ULA's
                          default ink/paper colours.
  hires_test_stripes.png  240x200, three vertical stripes (red/green/blue,
                          80px each) -- exercises colored mode's attribute
                          optimizer across two colour-boundary blocks,
                          including the 2-colour "boundary block" case
                          that forces the preceding 1-colour block to
                          pre-stage the next ink/paper pair (hand-verified
                          byte-by-byte when this fixture was captured).
  (hires_test_input.png is reused for the aic-mode case too, with a
  white/black even-row pair and a cyan/red odd-row pair.)
"""

import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
CONVERTER = REPO_ROOT / "tools" / "oric_pictconv.py"
FIXTURES = REPO_ROOT / "tests" / "fixtures"

CASES = [
    {
        "label": "mono mode (b/w split)",
        "input": FIXTURES / "hires_test_input.png",
        "args": ["--mode", "mono", "--dither", "none", "--ink", "white", "--paper", "black"],
        "expected": FIXTURES / "hires_test_expected_mono.bin",
    },
    {
        "label": "colored mode degenerates to mono (b/w split, default colours)",
        "input": FIXTURES / "hires_test_input.png",
        "args": ["--mode", "colored", "--dither", "none"],
        "expected": FIXTURES / "hires_test_expected_mono.bin",
    },
    {
        "label": "colored mode attribute optimizer (red/green/blue stripes)",
        "input": FIXTURES / "hires_test_stripes.png",
        "args": ["--mode", "colored", "--dither", "none"],
        "expected": FIXTURES / "hires_test_expected_colored.bin",
    },
    {
        "label": "aic mode per-parity ink/paper (b/w split, white/black + cyan/red)",
        "input": FIXTURES / "hires_test_input.png",
        "args": ["--mode", "aic", "--dither", "none",
                 "--aic-ink0", "white", "--aic-paper0", "black",
                 "--aic-ink1", "cyan", "--aic-paper1", "red"],
        "expected": FIXTURES / "hires_test_expected_aic.bin",
    },
]


def run_case(case, tmp_dir):
    out_path = Path(tmp_dir) / "out.bin"
    result = subprocess.run(
        [sys.executable, str(CONVERTER), str(case["input"]), str(out_path), *case["args"]],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        print(f"  [FAIL] {case['label']} -- converter exited non-zero")
        print(result.stderr)
        return False

    actual = out_path.read_bytes()
    expected = case["expected"].read_bytes()
    if actual == expected:
        print(f"  [PASS] {case['label']} ({len(actual)} bytes)")
        return True

    print(f"  [FAIL] {case['label']} -- output differs from expected fixture "
          f"(actual {len(actual)} bytes, expected {len(expected)} bytes)")
    for i, (a, e) in enumerate(zip(actual, expected)):
        if a != e:
            print(f"    first difference at byte {i}: actual=0x{a:02x} expected=0x{e:02x}")
            break
    return False


def main():
    print("=" * 63)
    print("  oric_pictconv.py -- unit tests")
    print("=" * 63)
    print()

    pass_count = 0
    fail_count = 0
    with tempfile.TemporaryDirectory() as tmp:
        for case in CASES:
            if run_case(case, tmp):
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
