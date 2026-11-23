// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <elara/audio/wav_writer.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace elara::audio {

/// Decoded audio: interleaved samples in [-1, 1].
struct WavAudio {
    WavFormat format{};
    std::vector<float> samples{};

    [[nodiscard]] std::size_t frames() const noexcept {
        return format.channels == 0 ? 0 : samples.size() / format.channels;
    }
    /// The channels averaged into one.
    [[nodiscard]] std::vector<float> mono() const;
};

/// Decodes a RIFF/WAVE file: PCM of 8, 16, 24 or 32 bits, or 32- or 64-bit
/// float, plain or WAVE_FORMAT_EXTENSIBLE. Chunks it does not need are
/// skipped. An error message for anything else or a truncated file.
[[nodiscard]] std::expected<WavAudio, std::string> decodeWav(std::span<const std::byte> bytes);

/// Reads and decodes `path`.
[[nodiscard]] std::expected<WavAudio, std::string> readWav(const std::filesystem::path& path);

} // namespace elara::audio
