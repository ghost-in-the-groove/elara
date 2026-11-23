// SPDX-License-Identifier: AGPL-3.0-or-later
#include <elara/audio/wav_writer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

std::string tagAt(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::string tag;
    for (std::size_t i = 0; i < 4; ++i) {
        tag += static_cast<char>(bytes.at(offset + i));
    }
    return tag;
}

std::uint32_t u32At(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes.at(offset + i)) << (8 * i);
    }
    return value;
}

std::uint16_t u16At(const std::vector<std::byte>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(bytes.at(offset)) |
                                      (static_cast<unsigned>(bytes.at(offset + 1)) << 8u));
}

} // namespace

TEST_CASE("encodeWavPcm16 writes a canonical 44-byte header", "[audio][wav]") {
    const std::vector<float> samples(10, 0.0f);
    const auto bytes = elara::audio::encodeWavPcm16(samples, {.sampleRate = 44100, .channels = 2});

    REQUIRE(bytes.size() == 44 + 10 * 2);
    CHECK(tagAt(bytes, 0) == "RIFF");
    CHECK(u32At(bytes, 4) == bytes.size() - 8);
    CHECK(tagAt(bytes, 8) == "WAVE");
    CHECK(tagAt(bytes, 12) == "fmt ");
    CHECK(u32At(bytes, 16) == 16);
    CHECK(u16At(bytes, 20) == 1);         // PCM
    CHECK(u16At(bytes, 22) == 2);         // channels
    CHECK(u32At(bytes, 24) == 44100);     // sample rate
    CHECK(u32At(bytes, 28) == 44100 * 4); // byte rate
    CHECK(u16At(bytes, 32) == 4);         // block align
    CHECK(u16At(bytes, 34) == 16);        // bits per sample
    CHECK(tagAt(bytes, 36) == "data");
    CHECK(u32At(bytes, 40) == 10 * 2);
}

TEST_CASE("encodeWavPcm16 scales and clips samples", "[audio][wav]") {
    const std::vector<float> samples{0.0f, 1.0f, -1.0f, 1.5f, -1.5f};
    const auto bytes = elara::audio::encodeWavPcm16(samples, {.sampleRate = 48000, .channels = 1});

    const auto sampleAt = [&](std::size_t index) {
        return static_cast<std::int16_t>(u16At(bytes, 44 + index * 2));
    };
    CHECK(sampleAt(0) == 0);
    CHECK(sampleAt(1) == 32767);
    CHECK(sampleAt(2) == -32767);
    CHECK(sampleAt(3) == 32767);
    CHECK(sampleAt(4) == -32767);
}

TEST_CASE("WavFileWriter writes block by block what encodeWavPcm16 writes at once",
          "[audio][wav]") {
    std::vector<float> samples;
    for (int i = 0; i < 2000; ++i) {
        samples.push_back(static_cast<float>(i % 200) / 100.0f - 1.0f);
    }
    const elara::audio::WavFormat format{.sampleRate = 48000, .channels = 2};
    const auto path = std::filesystem::temp_directory_path() / "elara-wav-writer-test.wav";
    {
        auto writer = elara::audio::WavFileWriter::create(path, format, samples.size() / 2);
        REQUIRE(writer.has_value());
        const std::span<const float> all(samples);
        REQUIRE(writer->write(all.first(6)).has_value());
        REQUIRE(writer->write(all.subspan(6, 994)).has_value());
        REQUIRE(writer->write(all.subspan(1000)).has_value());
        REQUIRE(writer->finish().has_value());
    }
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    file.close();
    std::filesystem::remove(path);
    const auto expected = elara::audio::encodeWavPcm16(samples, format);
    REQUIRE(raw.size() == expected.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        REQUIRE(static_cast<std::byte>(raw[i]) == expected[i]);
    }
}

TEST_CASE("WavFileWriter reports a file shorter or longer than its header says", "[audio][wav]") {
    const elara::audio::WavFormat format{.sampleRate = 48000, .channels = 1};
    const auto path = std::filesystem::temp_directory_path() / "elara-wav-writer-short.wav";
    {
        auto writer = elara::audio::WavFileWriter::create(path, format, 10);
        REQUIRE(writer.has_value());
        const std::vector<float> few(4, 0.0f);
        REQUIRE(writer->write(few).has_value());
        CHECK_FALSE(writer->finish().has_value());
        const std::vector<float> many(20, 0.0f);
        CHECK_FALSE(writer->write(many).has_value());
    }
    std::filesystem::remove(path);
    CHECK_FALSE(elara::audio::WavFileWriter::create(path / "missing" / "x.wav", format, 1));
}
