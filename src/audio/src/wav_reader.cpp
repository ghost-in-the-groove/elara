// SPDX-License-Identifier: AGPL-3.0-or-later
// RIFF/WAVE decoding from the published format: a "RIFF" header, then chunks
// of a four-letter id, a little-endian 32-bit size and the data, padded to an
// even length. "fmt " says how the samples in "data" are stored.
#include <elara/audio/wav_reader.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <string_view>

namespace elara::audio {

namespace {

constexpr std::uint16_t kPcm = 1;
constexpr std::uint16_t kFloat = 3;
constexpr std::uint16_t kExtensible = 0xFFFE;

std::uint32_t readU32(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24U);
}

std::uint16_t readU16(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(bytes[at]) |
                                      (static_cast<std::uint32_t>(bytes[at + 1]) << 8U));
}

bool idIs(std::span<const std::byte> bytes, std::size_t at, std::string_view id) {
    for (std::size_t i = 0; i < 4; ++i) {
        if (static_cast<char>(bytes[at + i]) != id[i]) {
            return false;
        }
    }
    return true;
}

// One sample of `bits` bits stored in `encoding` at `at`, scaled to [-1, 1].
float sampleAt(std::span<const std::byte> bytes, std::size_t at, std::uint16_t encoding,
               std::uint16_t bits) {
    if (encoding == kFloat) {
        if (bits == 32) {
            return std::bit_cast<float>(readU32(bytes, at));
        }
        const std::uint64_t raw =
            readU32(bytes, at) | (static_cast<std::uint64_t>(readU32(bytes, at + 4)) << 32U);
        return static_cast<float>(std::bit_cast<double>(raw));
    }
    switch (bits) {
    case 8: // unsigned, centred on 128
        return static_cast<float>(std::to_integer<int>(bytes[at]) - 128) / 128.0f;
    case 16:
        return static_cast<float>(static_cast<std::int16_t>(readU16(bytes, at))) / 32768.0f;
    case 24: {
        const std::uint32_t raw = static_cast<std::uint32_t>(bytes[at]) |
                                  (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
                                  (static_cast<std::uint32_t>(bytes[at + 2]) << 16U);
        const auto value = static_cast<std::int32_t>(raw << 8U) >> 8; // sign-extend
        return static_cast<float>(value) / 8388608.0f;
    }
    default: // 32
        return static_cast<float>(
            static_cast<double>(static_cast<std::int32_t>(readU32(bytes, at))) / 2147483648.0);
    }
}

} // namespace

std::vector<float> WavAudio::mono() const {
    const std::size_t channels = format.channels;
    std::vector<float> out(frames());
    for (std::size_t f = 0; f < out.size(); ++f) {
        float sum = 0.0f;
        for (std::size_t c = 0; c < channels; ++c) {
            sum += samples[(f * channels) + c];
        }
        out[f] = sum / static_cast<float>(channels);
    }
    return out;
}

std::expected<WavAudio, std::string> decodeWav(std::span<const std::byte> bytes) {
    if (bytes.size() < 12 || !idIs(bytes, 0, "RIFF") || !idIs(bytes, 8, "WAVE")) {
        return std::unexpected(std::string("not a RIFF/WAVE file"));
    }
    std::uint16_t encoding = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bits = 0;
    bool haveFormat = false;
    std::size_t at = 12;
    while (at + 8 <= bytes.size()) {
        const std::size_t size = readU32(bytes, at + 4);
        const std::size_t body = at + 8;
        if (size > bytes.size() - body) {
            return std::unexpected(std::string("truncated chunk"));
        }
        if (idIs(bytes, at, "fmt ")) {
            if (size < 16) {
                return std::unexpected(std::string("format chunk too short"));
            }
            encoding = readU16(bytes, body);
            channels = readU16(bytes, body + 2);
            sampleRate = readU32(bytes, body + 4);
            bits = readU16(bytes, body + 14);
            if (encoding == kExtensible) {
                if (size < 40) {
                    return std::unexpected(std::string("extensible format chunk too short"));
                }
                encoding = readU16(bytes, body + 24); // the sub-format GUID starts with it
            }
            haveFormat = true;
        } else if (idIs(bytes, at, "data")) {
            if (!haveFormat) {
                return std::unexpected(std::string("data before the format chunk"));
            }
            const bool pcm =
                encoding == kPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
            const bool floating = encoding == kFloat && (bits == 32 || bits == 64);
            if (!pcm && !floating) {
                return std::unexpected(
                    std::format("unsupported sample format {} with {} bits", encoding, bits));
            }
            if (channels == 0 || sampleRate == 0) {
                return std::unexpected(std::string("no channels or no sample rate"));
            }
            const std::size_t width = bits / 8U;
            const std::size_t count = size / width / channels * channels;
            WavAudio audio{.format = {.sampleRate = sampleRate, .channels = channels}};
            audio.samples.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                audio.samples[i] = sampleAt(bytes, body + (i * width), encoding, bits);
            }
            return audio;
        }
        at = body + size + (size % 2); // chunks are padded to an even length
    }
    return std::unexpected(std::string(haveFormat ? "no data chunk" : "no format chunk"));
}

std::expected<WavAudio, std::string> readWav(const std::filesystem::path& path) {
    // One sized read straight into the bytes: no per-byte iterator, no copy.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const std::streamoff size = file ? static_cast<std::streamoff>(file.tellg()) : -1;
    if (size < 0) {
        return std::unexpected(std::format("cannot open {}", path.string()));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        return std::unexpected(std::format("cannot read {}", path.string()));
    }
    return decodeWav(bytes);
}

} // namespace elara::audio
