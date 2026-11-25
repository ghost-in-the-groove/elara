// SPDX-License-Identifier: AGPL-3.0-or-later
// FLAC streams built here bit by bit from RFC 9639, with the samples they
// must give written out. Files encoded by libFLAC are checked by
// elara_check_flac (audio.flac_against_libflac).
#include <elara/audio/flac_reader.hpp>
#include <elara/audio/wav_writer.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace {

using elara::audio::decodeAudioFile;
using elara::audio::decodeFlac;

class BitWriter {
public:
    void put(std::uint64_t value, unsigned bits) {
        for (unsigned i = bits; i-- > 0;) {
            current_ =
                static_cast<std::uint8_t>((std::uint64_t{current_} << 1U) | ((value >> i) & 1U));
            if (++used_ == 8) {
                bytes_.push_back(std::byte{current_});
                current_ = 0;
                used_ = 0;
            }
        }
    }
    void putSigned(std::int64_t value, unsigned bits) {
        put(static_cast<std::uint64_t>(value), bits);
    }
    void align() {
        while (used_ != 0) {
            put(0, 1);
        }
    }
    [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
    std::uint8_t current_ = 0;
    unsigned used_ = 0;
};

// The CRCs of RFC 9639, section 9.1.8 and 9.3, bit by bit.
unsigned crc(const std::vector<std::byte>& bytes, unsigned width, unsigned polynomial) {
    const unsigned top = 1U << (width - 1);
    const unsigned mask = (1U << width) - 1;
    unsigned value = 0;
    for (const std::byte byte : bytes) {
        value ^= static_cast<unsigned>(byte) << (width - 8);
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & top) != 0 ? ((value << 1U) ^ polynomial) & mask : (value << 1U) & mask;
        }
    }
    return value;
}

std::vector<std::byte> text(std::string_view chars) {
    std::vector<std::byte> bytes;
    for (const char c : chars) {
        bytes.push_back(static_cast<std::byte>(c));
    }
    return bytes;
}

// A frame of `blockSize` samples whose subframes `body` writes; the sample
// rate and bit depth come from the stream information.
std::vector<std::byte> frame(unsigned number, unsigned blockSize, unsigned channelCode,
                             const std::function<void(BitWriter&)>& body) {
    BitWriter writer;
    writer.put(0x7FFC, 15);
    writer.put(0, 1);           // fixed block size
    writer.put(7, 4);           // block size - 1 follows in 16 bits
    writer.put(0, 4);           // sample rate from the stream information
    writer.put(channelCode, 4); // channels
    writer.put(0, 3);           // bit depth from the stream information
    writer.put(0, 1);
    writer.put(number, 8); // frame number below 128: one byte
    writer.put(blockSize - 1, 16);
    writer.put(crc(writer.bytes(), 8, 0x07), 8);
    body(writer);
    writer.align();
    writer.put(crc(writer.bytes(), 16, 0x8005), 16);
    return writer.bytes();
}

std::vector<std::byte> stream(unsigned rate, unsigned channels, unsigned depth,
                              std::uint64_t totalSamples,
                              const std::vector<std::vector<std::byte>>& frames) {
    BitWriter info;
    info.put(16, 16);   // minimum block size
    info.put(4096, 16); // maximum block size
    info.put(0, 24);    // minimum frame size: unknown
    info.put(0, 24);    // maximum frame size: unknown
    info.put(rate, 20);
    info.put(channels - 1, 3);
    info.put(depth - 1, 5);
    info.put(totalSamples, 36);
    for (int i = 0; i < 16; ++i) {
        info.put(0, 8); // MD5 unknown
    }
    std::vector<std::byte> bytes = text("fLaC");
    bytes.push_back(std::byte{0x80}); // the last metadata block, STREAMINFO
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{0});
    bytes.push_back(std::byte{34});
    bytes.insert(bytes.end(), info.bytes().begin(), info.bytes().end());
    for (const auto& f : frames) {
        bytes.insert(bytes.end(), f.begin(), f.end());
    }
    return bytes;
}

void verbatim(BitWriter& writer, const std::vector<std::int64_t>& samples, unsigned depth) {
    writer.put(0, 1);
    writer.put(1, 6); // verbatim
    writer.put(0, 1); // no wasted bits
    for (const std::int64_t sample : samples) {
        writer.putSigned(sample, depth);
    }
}

std::vector<float> scaled(const std::vector<std::int64_t>& samples, unsigned depth) {
    std::vector<float> out;
    for (const std::int64_t sample : samples) {
        out.push_back(static_cast<float>(static_cast<double>(sample) /
                                         static_cast<double>(std::int64_t{1} << (depth - 1))));
    }
    return out;
}

} // namespace

TEST_CASE("FLAC: the test streams carry the CRCs of the specification", "[audio][flac]") {
    // The check values of CRC-8 (polynomial 0x07) and CRC-16/UMTS (0x8005).
    CHECK(crc(text("123456789"), 8, 0x07) == 0xF4);
    CHECK(crc(text("123456789"), 16, 0x8005) == 0xFEE8);
}

TEST_CASE("FLAC: verbatim and constant subframes give their samples", "[audio][flac]") {
    const std::vector<std::int64_t> first = {0, 1000, -1000, 32767, -32768};
    const auto bytes = stream(44100, 1, 16, 8,
                              {frame(0, 5, 0, [&](BitWriter& w) { verbatim(w, first, 16); }),
                               frame(1, 3, 0, [](BitWriter& w) {
                                   w.put(0, 1);
                                   w.put(0, 6); // constant
                                   w.put(0, 1);
                                   w.putSigned(1234, 16);
                               })});
    const auto audio = decodeFlac(bytes);
    REQUIRE(audio.has_value());
    CHECK(audio->format.sampleRate == 44100);
    CHECK(audio->format.channels == 1);
    CHECK(audio->samples == scaled({0, 1000, -1000, 32767, -32768, 1234, 1234, 1234}, 16));
}

TEST_CASE("FLAC: fixed predictors add their prediction to the Rice-coded residual",
          "[audio][flac]") {
    const auto bytes = stream(48000, 1, 16, 9,
                              {frame(0, 6, 0,
                                     [](BitWriter& w) {
                                         // Order 2 on a ramp: every residual is 0, a single one
                                         // bit.
                                         w.put(0, 1);
                                         w.put(8 + 2, 6);
                                         w.put(0, 1);
                                         w.putSigned(0, 16);
                                         w.putSigned(10, 16);
                                         w.put(0, 2); // 4-bit Rice parameters
                                         w.put(0, 4); // one partition
                                         w.put(0, 4); // parameter 0
                                         for (int i = 0; i < 4; ++i) {
                                             w.put(1, 1);
                                         }
                                     }),
                               frame(1, 3, 0, [](BitWriter& w) {
                                   // Order 1 from 100, residuals 5 and -3, folded to 10 and 5, with
                                   // parameter 2: 10 is 001 then 10, and 5 is 01 then 01.
                                   w.put(0, 1);
                                   w.put(8 + 1, 6);
                                   w.put(0, 1);
                                   w.putSigned(100, 16);
                                   w.put(0, 2);
                                   w.put(0, 4);
                                   w.put(2, 4);
                                   w.put(0b001, 3);
                                   w.put(0b10, 2);
                                   w.put(0b01, 2);
                                   w.put(0b01, 2);
                               })});
    const auto audio = decodeFlac(bytes);
    REQUIRE(audio.has_value());
    CHECK(audio->samples == scaled({0, 10, 20, 30, 40, 50, 100, 105, 102}, 16));
}

TEST_CASE("FLAC: linear predictors shift their sum, and partitions can escape the Rice code",
          "[audio][flac]") {
    const auto bytes = stream(48000, 1, 16, 7,
                              {frame(0, 4, 0,
                                     [](BitWriter& w) {
                                         // Coefficients 2 and -1, no shift; the residual 1, -1 is
                                         // stored raw in 4 bits after the escape parameter 1111.
                                         w.put(0, 1);
                                         w.put(31 + 2, 6);
                                         w.put(0, 1);
                                         w.putSigned(0, 16);
                                         w.putSigned(7, 16);
                                         w.put(4 - 1, 4); // coefficient precision 4
                                         w.putSigned(0, 5);
                                         w.putSigned(2, 4);
                                         w.putSigned(-1, 4);
                                         w.put(0, 2);
                                         w.put(0, 4);
                                         w.put(0b1111, 4);
                                         w.put(4, 5);
                                         w.putSigned(1, 4);
                                         w.putSigned(-1, 4);
                                     }),
                               frame(1, 3, 0, [](BitWriter& w) {
                                   // Coefficient 3 shifted right by 1: 10, 15, 22.
                                   w.put(0, 1);
                                   w.put(31 + 1, 6);
                                   w.put(0, 1);
                                   w.putSigned(10, 16);
                                   w.put(3 - 1, 4);
                                   w.putSigned(1, 5);
                                   w.putSigned(3, 3);
                                   w.put(1, 2); // 5-bit Rice parameters
                                   w.put(0, 4);
                                   w.put(0, 5);
                                   w.put(1, 1);
                                   w.put(1, 1);
                               })});
    const auto audio = decodeFlac(bytes);
    REQUIRE(audio.has_value());
    CHECK(audio->samples == scaled({0, 7, 15, 22, 10, 15, 22}, 16));
}

TEST_CASE("FLAC: stereo frames undo every decorrelation, the side channel one bit wider",
          "[audio][flac]") {
    const std::vector<std::int64_t> left = {100, -50};
    const std::vector<std::int64_t> right = {60, 10};
    const std::vector<std::int64_t> side = {40, -60}; // left - right
    const std::vector<std::int64_t> mid = {80, -20};  // (left + right) >> 1
    const auto bytes = stream(48000, 2, 16, 8,
                              {frame(0, 2, 1,
                                     [&](BitWriter& w) {
                                         verbatim(w, left, 16);
                                         verbatim(w, right, 16);
                                     }),
                               frame(1, 2, 8,
                                     [&](BitWriter& w) {
                                         verbatim(w, left, 16);
                                         verbatim(w, side, 17);
                                     }),
                               frame(2, 2, 9,
                                     [&](BitWriter& w) {
                                         verbatim(w, side, 17);
                                         verbatim(w, right, 16);
                                     }),
                               frame(3, 2, 10, [&](BitWriter& w) {
                                   verbatim(w, mid, 16);
                                   verbatim(w, side, 17);
                               })});
    const auto audio = decodeFlac(bytes);
    REQUIRE(audio.has_value());
    CHECK(audio->format.channels == 2);
    const auto pair = scaled({100, 60, -50, 10}, 16);
    std::vector<float> expected;
    for (int i = 0; i < 4; ++i) {
        expected.insert(expected.end(), pair.begin(), pair.end());
    }
    CHECK(audio->samples == expected);
}

TEST_CASE("FLAC: wasted bits are put back, and 8 and 24 bits scale to full range",
          "[audio][flac]") {
    const auto wasted = stream(48000, 1, 16, 2, {frame(0, 2, 0, [](BitWriter& w) {
                                   w.put(0, 1);
                                   w.put(1, 6);
                                   w.put(1, 1);     // wasted bits follow
                                   w.put(0b001, 3); // 3 - 1 in unary
                                   w.putSigned(1, 13);
                                   w.putSigned(-2, 13);
                               })});
    const auto shifted = decodeFlac(wasted);
    REQUIRE(shifted.has_value());
    CHECK(shifted->samples == scaled({8, -16}, 16));

    const auto eight = decodeFlac(stream(
        22050, 1, 8, 2, {frame(0, 2, 0, [](BitWriter& w) { verbatim(w, {127, -128}, 8); })}));
    REQUIRE(eight.has_value());
    CHECK(eight->samples == scaled({127, -128}, 8));

    const auto twentyFour = decodeFlac(stream(96000, 1, 24, 2, {frame(0, 2, 0, [](BitWriter& w) {
                                                  verbatim(w, {8388607, -8388608}, 24);
                                              })}));
    REQUIRE(twentyFour.has_value());
    CHECK(twentyFour->samples == scaled({8388607, -8388608}, 24));
}

TEST_CASE("FLAC: damaged, truncated and foreign files are refused with a reason", "[audio][flac]") {
    using Catch::Matchers::ContainsSubstring;
    const auto good =
        stream(48000, 1, 16, 3, {frame(0, 3, 0, [](BitWriter& w) { verbatim(w, {1, 2, 3}, 16); })});
    REQUIRE(decodeFlac(good).has_value());

    auto damaged = good;
    damaged[damaged.size() - 4] ^= std::byte{0x10};
    const auto crcError = decodeFlac(damaged);
    REQUIRE_FALSE(crcError.has_value());
    CHECK_THAT(crcError.error(), ContainsSubstring("damaged"));

    const std::vector<std::byte> truncated(good.begin(), good.end() - 3);
    CHECK_FALSE(decodeFlac(truncated).has_value());

    const auto shortCount = decodeFlac(stream(
        48000, 1, 16, 5, {frame(0, 3, 0, [](BitWriter& w) { verbatim(w, {1, 2, 3}, 16); })}));
    REQUIRE_FALSE(shortCount.has_value());
    CHECK_THAT(shortCount.error(), ContainsSubstring("3 of its 5"));

    const auto foreign = decodeFlac(text("RIFF....WAVE"));
    REQUIRE_FALSE(foreign.has_value());
    CHECK(foreign.error() == "not a FLAC file");
}

TEST_CASE("FLAC: an ID3v2 tag in front is skipped, and audio files are told apart",
          "[audio][flac]") {
    const auto flac =
        stream(48000, 1, 16, 2, {frame(0, 2, 0, [](BitWriter& w) { verbatim(w, {-3, 3}, 16); })});
    std::vector<std::byte> tagged = text("ID3");
    for (const int b : {4, 0, 0, 0, 0, 0, 5}) { // version, flags, size 5 (syncsafe)
        tagged.push_back(static_cast<std::byte>(b));
    }
    tagged.insert(tagged.end(), 5, std::byte{0});
    tagged.insert(tagged.end(), flac.begin(), flac.end());
    const auto skipped = decodeAudioFile(tagged);
    REQUIRE(skipped.has_value());
    CHECK(skipped->samples == scaled({-3, 3}, 16));

    const std::vector<float> wavSamples = {0.5f, -0.5f};
    const auto wav = decodeAudioFile(
        elara::audio::encodeWavPcm16(wavSamples, {.sampleRate = 8000, .channels = 1}));
    REQUIRE(wav.has_value());
    CHECK(wav->format.sampleRate == 8000);
    CHECK(wav->frames() == 2);
}
