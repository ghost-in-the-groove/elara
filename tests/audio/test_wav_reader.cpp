// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/wav_reader.hpp>
#include <elara/audio/wav_writer.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

using Catch::Matchers::WithinAbs;
using elara::audio::decodeWav;
using elara::audio::encodeWavPcm16;
using elara::audio::WavAudio;
using elara::audio::WavFormat;

namespace {

void put(std::vector<std::byte>& out, std::string_view id) {
    for (const char c : id) {
        out.push_back(static_cast<std::byte>(c));
    }
}

void putLE(std::vector<std::byte>& out, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<std::byte>((value >> (8U * static_cast<unsigned>(i))) & 0xFFU));
    }
}

struct Layout {
    std::uint16_t encoding = 1;
    std::uint16_t channels = 1;
    std::uint32_t rate = 44100;
    std::uint16_t bits = 16;
    bool extensible = false;
    bool listChunk = false; // an odd-sized chunk before the data
};

// A WAV file as a recorder might write it, around raw sample bytes.
std::vector<std::byte> wav(const Layout& layout, const std::vector<std::byte>& data) {
    std::vector<std::byte> body;
    put(body, "WAVE");
    put(body, "fmt ");
    putLE(body, layout.extensible ? 40 : 16, 4);
    putLE(body, layout.extensible ? 0xFFFE : layout.encoding, 2);
    putLE(body, layout.channels, 2);
    putLE(body, layout.rate, 4);
    putLE(body, std::uint64_t{layout.rate} * layout.channels * layout.bits / 8U, 4);
    putLE(body, std::uint64_t{layout.channels} * layout.bits / 8U, 2);
    putLE(body, layout.bits, 2);
    if (layout.extensible) {
        putLE(body, 22, 2);              // extension size
        putLE(body, layout.bits, 2);     // valid bits
        putLE(body, 0, 4);               // channel mask
        putLE(body, layout.encoding, 2); // sub-format GUID: the format, then a fixed tail
        for (int i = 0; i < 14; ++i) {
            body.push_back(std::byte{0});
        }
    }
    if (layout.listChunk) {
        put(body, "LIST");
        putLE(body, 3, 4);
        put(body, "abc");
        body.push_back(std::byte{0}); // padding
    }
    put(body, "data");
    putLE(body, data.size(), 4);
    body.insert(body.end(), data.begin(), data.end());
    std::vector<std::byte> file;
    put(file, "RIFF");
    putLE(file, body.size(), 4);
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

} // namespace

TEST_CASE("A WAV file written by Elara reads back", "[audio][wav]") {
    const std::vector<float> samples{0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 0.25f};
    const auto decoded =
        decodeWav(encodeWavPcm16(samples, WavFormat{.sampleRate = 48000, .channels = 2}));
    REQUIRE(decoded);
    CHECK(decoded->format.sampleRate == 48000);
    CHECK(decoded->format.channels == 2);
    CHECK(decoded->frames() == 3);
    REQUIRE(decoded->samples.size() == samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        CHECK_THAT(decoded->samples[i], WithinAbs(static_cast<double>(samples[i]), 1.0 / 32767.0));
    }
    CHECK(decoded->mono().size() == 3);
    CHECK_THAT(decoded->mono()[2], WithinAbs(-0.375, 1e-4)); // (-1 + 0.25) / 2
}

TEST_CASE("WAV sample formats decode to the same scale", "[audio][wav]") {
    SECTION("24-bit PCM, past a chunk of another kind") {
        // -0.5 and the largest positive 24-bit value.
        const auto decoded = decodeWav(wav({.bits = 24, .listChunk = true},
                                           {std::byte{0x00}, std::byte{0x00}, std::byte{0xC0},
                                            std::byte{0xFF}, std::byte{0xFF}, std::byte{0x7F}}));
        REQUIRE(decoded);
        REQUIRE(decoded->samples.size() == 2);
        CHECK_THAT(decoded->samples[0], WithinAbs(-0.5, 1e-6));
        CHECK_THAT(decoded->samples[1], WithinAbs(1.0, 1e-6));
    }
    SECTION("8-bit PCM is unsigned") {
        const auto decoded =
            decodeWav(wav({.bits = 8}, {std::byte{128}, std::byte{0}, std::byte{192}}));
        REQUIRE(decoded);
        CHECK(decoded->samples == std::vector<float>{0.0f, -1.0f, 0.5f});
    }
    SECTION("32-bit float inside WAVE_FORMAT_EXTENSIBLE") {
        std::vector<std::byte> data;
        putLE(data, std::bit_cast<std::uint32_t>(0.75f), 4);
        putLE(data, std::bit_cast<std::uint32_t>(-0.125f), 4);
        const auto decoded = decodeWav(wav({.encoding = 3, .bits = 32, .extensible = true}, data));
        REQUIRE(decoded);
        CHECK(decoded->samples == std::vector<float>{0.75f, -0.125f});
    }
    SECTION("32-bit PCM and 64-bit float") {
        std::vector<std::byte> pcm;
        putLE(pcm, 0x40000000U, 4); // 0.5
        const auto decodedPcm = decodeWav(wav({.bits = 32}, pcm));
        REQUIRE(decodedPcm);
        CHECK(decodedPcm->samples == std::vector<float>{0.5f});
        std::vector<std::byte> wide;
        putLE(wide, std::bit_cast<std::uint64_t>(-0.25), 8);
        const auto decodedWide = decodeWav(wav({.encoding = 3, .bits = 64}, wide));
        REQUIRE(decodedWide);
        CHECK(decodedWide->samples == std::vector<float>{-0.25f});
    }
}

TEST_CASE("What is not a WAV file Elara can read is refused", "[audio][wav]") {
    CHECK_FALSE(decodeWav({}));
    std::vector<std::byte> notRiff(44, std::byte{0});
    CHECK_FALSE(decodeWav(notRiff));
    CHECK_FALSE(decodeWav(wav({.encoding = 2, .bits = 4}, {std::byte{0}}))); // ADPCM
    std::vector<std::byte> truncated = wav({}, {std::byte{0}, std::byte{0}});
    truncated.resize(truncated.size() - 1);
    CHECK_FALSE(decodeWav(truncated));
    CHECK_FALSE(elara::audio::readWav("no/such/file.wav"));
}
