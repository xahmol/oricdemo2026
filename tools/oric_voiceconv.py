#!/usr/bin/env python3
"""oric_voiceconv.py - convert a mono WAV file into an Oric Atmos AY-3-8912
"digidrums"-style voice-sample .bin.

Parallels the "ChibiWave Converter" tool from ChibiAkumas's Z80 tutorial
series, Lesson P35 "Playing Digital Sound with WAV on the AY!"
(chibiakumas.com/z80/platform4.php#LessonP35) -- same idea (WAV -> packed
low-bit-depth samples for AY playback), reimplemented here in Python
(stdlib only) rather than that lesson's own tool, and fixed to the
AY-3-8912's full 4-bit range rather than its 1/2/4-bit options. See
include/voice.h's own header comment and docs/voice.md's Attribution
section for what the ON-ORIC PLAYBACK side (include/voice.c) adapted
from that same lesson.

Resamples to a target rate (default 4000Hz), trims leading/trailing
silence, and linearly quantizes each sample to a 4-bit value (0-15) --
the AY-3-8912's own volume-register resolution. Output is a raw byte
stream, one byte per sample, values 0-15 only (bit 4+ must stay 0 or the
AY switches a channel into envelope mode instead of direct amplitude
control -- see include/voice.c).

This is a crude, lossy playback technique (~16 amplitude steps, no true
per-sample timing precision beyond the target rate) -- expect
"recognizable words", not clear speech. See docs/voice.md for the
on-Oric playback side and the real memory-budget constraint this tool's
--max-bytes flag enforces.

Source audio should already be a WAV file (mono; stereo is downmixed).
Converting from another format (e.g. an MP3 TTS export) is a one-off
prep step outside this tool -- `ffmpeg -i in.mp3 -ar 22050 -ac 1 out.wav`
-- same convention as oric_pictconv.py taking an already-sourced image,
not fetching one itself.

stdlib only, no dependencies (deliberately -- see this project's own
tools/requirements.txt, which lists only Pillow for oric_pictconv.py;
audioop is deprecated/removed as of Python 3.13, so it is not used here
either).
"""

import argparse
import struct
import sys
import wave


def read_wav_mono(path):
    """Reads a WAV file and returns (samples, rate) -- samples is a list
    of floats in [-1.0, 1.0], downmixed to mono if the source is stereo+."""
    with wave.open(path, "rb") as w:
        channels = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        n = w.getnframes()
        raw = w.readframes(n)

    if width == 1:
        # WAV 8-bit PCM is UNSIGNED, centred at 128.
        ints = struct.unpack("<%dB" % (n * channels), raw)
        floats = [(v - 128) / 128.0 for v in ints]
    elif width == 2:
        ints = struct.unpack("<%dh" % (n * channels), raw)
        floats = [v / 32768.0 for v in ints]
    else:
        raise ValueError(f"unsupported WAV sample width {width * 8}-bit "
                          f"(only 8-bit and 16-bit PCM are supported -- "
                          f"re-export with, e.g., 'ffmpeg -i in.wav -acodec pcm_s16le out.wav')")

    if channels == 1:
        samples = floats
    else:
        # Downmix: average all channels per frame.
        samples = [
            sum(floats[i * channels:(i + 1) * channels]) / channels
            for i in range(n)
        ]

    return samples, rate


def trim_silence(samples, threshold, min_run):
    """Trims a leading and/or trailing run of samples whose absolute value
    stays below `threshold`, but only if that run is at least `min_run`
    samples long (a short quiet blip right at the very start/end -- e.g. a
    soft consonant -- is left alone rather than trimmed). Interior silence
    (between words) is never touched."""
    n = len(samples)

    start = 0
    while start < n and abs(samples[start]) < threshold:
        start += 1
    if start < min_run:
        start = 0

    end = n
    while end > start and abs(samples[end - 1]) < threshold:
        end -= 1
    if n - end < min_run:
        end = n

    return samples[start:end]


def resample(samples, src_rate, dst_rate):
    """Linear-interpolation resample -- plain Python loop, no numpy. A
    ~2-second clip is a few thousand samples either way; trivially fast."""
    if src_rate == dst_rate:
        return list(samples)

    src_n = len(samples)
    dst_n = max(1, round(src_n * dst_rate / src_rate))
    out = []
    for i in range(dst_n):
        src_pos = i * (src_n - 1) / max(1, dst_n - 1)
        lo = int(src_pos)
        hi = min(lo + 1, src_n - 1)
        frac = src_pos - lo
        out.append(samples[lo] * (1.0 - frac) + samples[hi] * frac)
    return out


def normalize_peak(samples):
    """Scales so the loudest sample hits the full [-1.0, 1.0] range --
    without this, a source clip recorded/exported below full scale (common
    for TTS exports) wastes some of the AY's already-scarce 4-bit
    resolution on headroom that's never used. A no-op if already at or
    above full scale."""
    peak = max((abs(s) for s in samples), default=0.0)
    if peak <= 0.0 or peak >= 1.0:
        return samples
    gain = 1.0 / peak
    return [s * gain for s in samples]


def quantize_4bit(samples):
    """Linearly maps each sample's full [-1.0, 1.0] range to 0-15 (the
    AY volume register's own 4-bit amplitude range). Not perceptual/
    logarithmic remapping -- plain linear quantization of the waveform,
    matching standard digidrums-conversion practice."""
    out = bytearray()
    for s in samples:
        clamped = max(-1.0, min(1.0, s))
        level = round((clamped + 1.0) / 2.0 * 15.0)
        out.append(level)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="source WAV file (mono or stereo, 8/16-bit PCM)")
    ap.add_argument("output", help="output .bin file (raw bytes, one per sample, values 0-15)")
    ap.add_argument("--rate", type=int, default=4000, help="target sample rate in Hz (default 4000)")
    ap.add_argument("--max-bytes", type=int, default=7731,
                     help="hard cap on output size in bytes -- fails loudly rather than "
                          "silently truncating if the converted clip would exceed it "
                          "(default 7731 -- see docs/voice.md for where this number comes from)")
    ap.add_argument("--silence-threshold", type=float, default=0.02,
                     help="amplitude (0.0-1.0) below which a sample counts as silence "
                          "for leading/trailing trim (default 0.02)")
    ap.add_argument("--silence-min-run", type=int, default=1,
                     help="minimum consecutive silent SOURCE samples for leading/trailing "
                          "trim to engage (default 1 -- trim any leading/trailing silence)")
    args = ap.parse_args()

    samples, src_rate = read_wav_mono(args.input)
    trimmed = trim_silence(samples, args.silence_threshold, args.silence_min_run)
    resampled = resample(trimmed, src_rate, args.rate)
    normalized = normalize_peak(resampled)
    data = quantize_4bit(normalized)

    if len(data) > args.max_bytes:
        duration = len(data) / args.rate
        max_duration = args.max_bytes / args.rate
        print(f"ERROR: converted clip is {len(data)} bytes ({duration:.2f}s at {args.rate}Hz), "
              f"exceeds --max-bytes={args.max_bytes} ({max_duration:.2f}s). "
              f"Trim the source clip shorter, lower --rate, or raise --max-bytes "
              f"only after re-checking the real memory budget (see docs/voice.md) "
              f"-- do not silently truncate a voice sample.", file=sys.stderr)
        return 1

    with open(args.output, "wb") as f:
        f.write(data)

    print(f"Wrote {len(data)} bytes to {args.output} "
          f"({len(data) / args.rate:.2f}s at {args.rate}Hz, "
          f"trimmed from {len(samples) / src_rate:.2f}s source at {src_rate}Hz)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
