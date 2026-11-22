// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace elara::audio {

struct WavFormat {
    std::uint32_t sampleRate = 48000;
    std::uint16_t channels = 1;
};

/// Encodes interleaved float samples in [-1, 1] as a 16-bit PCM RIFF/WAVE file.
/// Out-of-range samples are clipped.
[[nodiscard]] std::vector<std::byte> encodeWavPcm16(std::span<const float> interleaved,
                                                    WavFormat format);

/// Writes encodeWavPcm16() to `path`. Returns an error message on failure.
[[nodiscard]] std::expected<void, std::string> writeWavPcm16(const std::filesystem::path& path,
                                                             std::span<const float> interleaved,
                                                             WavFormat format);

} // namespace elara::audio
