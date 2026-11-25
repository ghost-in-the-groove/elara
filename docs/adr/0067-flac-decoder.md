# 0067. A FLAC decoder of Elara's own

- Status: Accepted
- Date: 2026-11-25

## Context

Sampled instruments, the SFZ guitar and bass libraries above all (ADR-0066),
ship their samples as FLAC as often as WAV: a library of several hundred
megabytes of WAV takes about half as much in FLAC. Elara reads WAV with a
reader of its own in `elara_audio`, which uses only the standard library.

libFLAC is already built: libsndfile links it, and FluidSynth loads libsndfile
for SF3 SoundFonts (ADR-0043). Using it from `elara_audio` would give that
module its first third-party dependency, and would put an LGPL library
(libsndfile) or a second copy of libFLAC's API between the sampler and its
samples. FLAC is fully specified by RFC 9639, and decoding it is small: a
frame header, four kinds of subframe, a Rice-coded residual and three ways of
coding stereo.

## Decision

**`elara_audio` decodes FLAC itself, from RFC 9639** (`flac_reader.hpp`):
`decodeFlac()` and `readFlac()` give the same `WavAudio` as the WAV reader,
and `decodeAudioFile()`/`readAudioFile()` read either, told apart by the
file's first bytes.

- Every part of the format is read: constant, verbatim, fixed and linear
  predictor subframes, both Rice parameter widths and escaped partitions,
  wasted bits, independent, left-side, side-right and mid-side stereo, 4 to
  32 bits per sample, up to 8 channels, any block size, and an ID3v2 tag in
  front. Only STREAMINFO is read of the metadata.
- Samples are integers of 64 bits while they are decoded, so the side
  channel of 32-bit audio (33 bits) and long predictors cannot overflow.
- Each frame's CRC-8 and CRC-16 are checked; a damaged, truncated or
  changing stream (sample rate, channels or bit depth) is refused with a
  message, as the specification allows. The MD5 signature is not checked.
- Bits are read through a 64-bit cache, and the unary part of the Rice code
  counts leading zeros a word at a time.

**Tests have an independent source of truth.** Unit tests decode streams
built bit by bit from the specification, with the samples they must give
written out. `elara_check_flac` (`audio.flac_against_libflac`) has libFLAC,
through libsndfile, encode signals at 8, 16 and 24 bits, in one, two and four
channels, at the fastest and the best compression, and checks that every
sample decodes exactly. libsndfile is used by that test only.

## Consequences

- `elara_audio` stays free of third-party code, and the sampler can read the
  FLAC samples of a library (ADR-0066), on every core like its WAV samples.
- `elara-cli tune` reads FLAC recordings too.
- A debug build decodes 16-bit stereo at about 180 times real time on one
  core, so a library loads in a fraction of its WAV reading time plus the
  decoding; an optimised build is several times faster.
- Ogg Vorbis samples, which a few libraries use, are not read.
