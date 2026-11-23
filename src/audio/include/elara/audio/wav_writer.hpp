// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
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

/// Writes a 16-bit PCM WAV file block by block, for audio too long to hold in
/// memory. The header announces the frame count given to create().
class WavFileWriter {
public:
    /// Creates (or truncates) `path` and writes the header for `frames` frames.
    [[nodiscard]] static std::expected<WavFileWriter, std::string>
    create(const std::filesystem::path& path, WavFormat format, std::uint64_t frames);

    /// Appends interleaved samples; an error once more than the announced
    /// frames would be written.
    [[nodiscard]] std::expected<void, std::string> write(std::span<const float> interleaved);

    /// Flushes the file; an error when fewer frames than announced were written.
    [[nodiscard]] std::expected<void, std::string> finish();

private:
    WavFileWriter(std::ofstream file, std::filesystem::path path, std::uint64_t samples);

    std::ofstream file_;
    std::filesystem::path path_;
    std::uint64_t samplesLeft_ = 0;
    std::vector<std::byte> bytes_;
};

} // namespace elara::audio
