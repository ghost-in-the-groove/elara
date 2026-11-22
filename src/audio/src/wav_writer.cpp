// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/wav_writer.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string_view>

namespace elara::audio {

namespace {

constexpr std::uint16_t kBitsPerSample = 16;
constexpr std::uint32_t kHeaderBytes = 44;

void appendTag(std::vector<std::byte>& out, std::string_view tag) {
    for (const char c : tag) {
        out.push_back(static_cast<std::byte>(c));
    }
}

void appendU16(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
}

void appendU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
    }
}

std::int16_t toPcm16(float sample) noexcept {
    const auto clipped = std::clamp(sample, -1.0f, 1.0f);
    return static_cast<std::int16_t>(std::lround(clipped * 32767.0f));
}

} // namespace

std::vector<std::byte> encodeWavPcm16(std::span<const float> interleaved, WavFormat format) {
    const auto blockAlign = static_cast<std::uint16_t>(format.channels * (kBitsPerSample / 8));
    const auto dataBytes = static_cast<std::uint32_t>(interleaved.size() * (kBitsPerSample / 8));

    std::vector<std::byte> out;
    out.reserve(kHeaderBytes + dataBytes);
    appendTag(out, "RIFF");
    appendU32(out, kHeaderBytes - 8 + dataBytes);
    appendTag(out, "WAVE");
    appendTag(out, "fmt ");
    appendU32(out, 16); // fmt chunk size
    appendU16(out, 1);  // PCM
    appendU16(out, format.channels);
    appendU32(out, format.sampleRate);
    appendU32(out, format.sampleRate * blockAlign); // byte rate
    appendU16(out, blockAlign);
    appendU16(out, kBitsPerSample);
    appendTag(out, "data");
    appendU32(out, dataBytes);
    for (const auto sample : interleaved) {
        appendU16(out, static_cast<std::uint16_t>(toPcm16(sample)));
    }
    return out;
}

std::expected<void, std::string> writeWavPcm16(const std::filesystem::path& path,
                                               std::span<const float> interleaved,
                                               WavFormat format) {
    const auto bytes = encodeWavPcm16(interleaved, format);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::unexpected("cannot open " + path.string() + " for writing");
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        return std::unexpected("failed to write " + path.string());
    }
    return {};
}

} // namespace elara::audio
