// SPDX-License-Identifier: AGPL-3.0-or-later
// FLAC decoding (ADR-0067) and audio files that are either WAV or FLAC.
#pragma once

#include <elara/audio/wav_reader.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace elara::audio {

/// Decodes a FLAC stream as its specification, RFC 9639, defines it: every
/// subframe kind (constant, verbatim, fixed and linear prediction), every
/// stereo decorrelation, 4 to 32 bits per sample, any block size, wasted
/// bits, and an ID3v2 tag in front. Each frame's CRCs are checked. A stream
/// whose sample rate, channels or bit depth change between frames is refused,
/// as is a truncated or damaged one.
[[nodiscard]] std::expected<WavAudio, std::string> decodeFlac(std::span<const std::byte> bytes);

/// Reads and decodes `path`.
[[nodiscard]] std::expected<WavAudio, std::string> readFlac(const std::filesystem::path& path);

/// Decodes a WAV or a FLAC file, told apart by its first bytes.
[[nodiscard]] std::expected<WavAudio, std::string>
decodeAudioFile(std::span<const std::byte> bytes);

/// Reads and decodes a WAV or a FLAC file.
[[nodiscard]] std::expected<WavAudio, std::string> readAudioFile(const std::filesystem::path& path);

} // namespace elara::audio
