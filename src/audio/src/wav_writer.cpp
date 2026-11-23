// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/wav_writer.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

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

void appendHeader(std::vector<std::byte>& out, WavFormat format, std::uint32_t dataBytes) {
    const auto blockAlign = static_cast<std::uint16_t>(format.channels * (kBitsPerSample / 8));
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
}

void appendSamples(std::vector<std::byte>& out, std::span<const float> interleaved) {
    for (const auto sample : interleaved) {
        appendU16(out, static_cast<std::uint16_t>(toPcm16(sample)));
    }
}

} // namespace

std::vector<std::byte> encodeWavPcm16(std::span<const float> interleaved, WavFormat format) {
    const auto dataBytes = static_cast<std::uint32_t>(interleaved.size() * (kBitsPerSample / 8));
    std::vector<std::byte> out;
    out.reserve(kHeaderBytes + dataBytes);
    appendHeader(out, format, dataBytes);
    appendSamples(out, interleaved);
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

std::expected<WavFileWriter, std::string>
WavFileWriter::create(const std::filesystem::path& path, WavFormat format, std::uint64_t frames) {
    const std::uint64_t samples = frames * format.channels;
    constexpr std::uint64_t kMaxDataBytes = 0xFFFFFFFFULL - kHeaderBytes;
    if (samples * (kBitsPerSample / 8) > kMaxDataBytes) {
        return std::unexpected("too long for a WAV file: " + std::to_string(frames) + " frames");
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::unexpected("cannot open " + path.string() + " for writing");
    }
    std::vector<std::byte> header;
    appendHeader(header, format, static_cast<std::uint32_t>(samples * (kBitsPerSample / 8)));
    file.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    if (!file) {
        return std::unexpected("failed to write " + path.string());
    }
    return WavFileWriter(std::move(file), path, samples);
}

WavFileWriter::WavFileWriter(std::ofstream file, std::filesystem::path path, std::uint64_t samples)
    : file_(std::move(file)), path_(std::move(path)), samplesLeft_(samples) {}

std::expected<void, std::string> WavFileWriter::write(std::span<const float> interleaved) {
    if (interleaved.size() > samplesLeft_) {
        return std::unexpected("more samples than the header of " + path_.string() + " announces");
    }
    bytes_.clear();
    appendSamples(bytes_, interleaved);
    file_.write(reinterpret_cast<const char*>(bytes_.data()),
                static_cast<std::streamsize>(bytes_.size()));
    if (!file_) {
        return std::unexpected("failed to write " + path_.string());
    }
    samplesLeft_ -= interleaved.size();
    return {};
}

std::expected<void, std::string> WavFileWriter::finish() {
    file_.flush();
    if (!file_) {
        return std::unexpected("failed to write " + path_.string());
    }
    if (samplesLeft_ > 0) {
        return std::unexpected(std::to_string(samplesLeft_) + " samples missing from " +
                               path_.string());
    }
    return {};
}

} // namespace elara::audio
