// SPDX-License-Identifier: AGPL-3.0-or-later
// FLAC decoding from its specification, RFC 9639: the "fLaC" marker, metadata
// blocks (of which only STREAMINFO is needed), then frames. A frame is a
// header, one subframe per channel (a constant, verbatim samples, or a fixed
// or linear predictor with a Rice-coded residual), zero bits up to a byte
// boundary and a CRC-16.
#include <elara/audio/flac_reader.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <format>
#include <fstream>
#include <string_view>
#include <vector>

namespace elara::audio {

namespace {

using Bytes = std::span<const std::byte>;

std::uint32_t byteAt(Bytes bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]);
}

bool startsWith(Bytes bytes, std::size_t at, std::string_view text) {
    if (bytes.size() < at + text.size()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (static_cast<char>(bytes[at + i]) != text[i]) {
            return false;
        }
    }
    return true;
}

// CRC-8 (polynomial x^8 + x^2 + x + 1) of frame headers and CRC-16
// (x^16 + x^15 + x^2 + 1) of whole frames, both starting from 0.
constexpr std::array<std::uint8_t, 256> kCrc8 = [] {
    std::array<std::uint8_t, 256> table{};
    for (unsigned i = 0; i < 256; ++i) {
        unsigned crc = i;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) != 0 ? ((crc << 1U) ^ 0x07U) & 0xFFU : (crc << 1U) & 0xFFU;
        }
        table[i] = static_cast<std::uint8_t>(crc);
    }
    return table;
}();

constexpr std::array<std::uint16_t, 256> kCrc16 = [] {
    std::array<std::uint16_t, 256> table{};
    for (unsigned i = 0; i < 256; ++i) {
        unsigned crc = i << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0 ? ((crc << 1U) ^ 0x8005U) & 0xFFFFU : (crc << 1U) & 0xFFFFU;
        }
        table[i] = static_cast<std::uint16_t>(crc);
    }
    return table;
}();

unsigned crc8(Bytes bytes) {
    unsigned crc = 0;
    for (const std::byte byte : bytes) {
        crc = kCrc8[(crc ^ static_cast<unsigned>(byte)) & 0xFFU];
    }
    return crc;
}

unsigned crc16(Bytes bytes) {
    unsigned crc = 0;
    for (const std::byte byte : bytes) {
        crc = ((crc << 8U) ^ kCrc16[((crc >> 8U) ^ static_cast<unsigned>(byte)) & 0xFFU]) & 0xFFFFU;
    }
    return crc;
}

// Reads bits most significant first, through a 64-bit cache whose valid bits
// are at the top. Past the end it reads zeros and remembers the overrun,
// which the caller checks once per frame.
class BitReader {
public:
    BitReader(Bytes bytes, std::size_t at) : bytes_(bytes), at_(at) {}

    // `count` bits as an unsigned number, up to 32.
    std::uint32_t bits(unsigned count) {
        if (count == 0) {
            return 0;
        }
        if (cached_ < count) {
            refill();
            if (cached_ < count) {
                overrun_ = true;
                cached_ = count; // the missing bits read as zeros
            }
        }
        const auto value = static_cast<std::uint32_t>(cache_ >> (64U - count));
        cache_ <<= count;
        cached_ -= count;
        return value;
    }

    // `count` bits as a two's complement number, up to 33.
    std::int64_t signedBits(unsigned count) {
        if (count == 0) {
            return 0;
        }
        std::uint64_t value = 0;
        if (count > 32) {
            value = static_cast<std::uint64_t>(bits(count - 32)) << 32U;
            value |= bits(32);
        } else {
            value = bits(count);
        }
        const unsigned unused = 64U - count;
        return static_cast<std::int64_t>(value << unused) >> unused;
    }

    // The zeros before the next one bit, which is read too.
    std::uint64_t unary() {
        std::uint64_t zeros = 0;
        while (true) {
            if (cached_ == 0 || cache_ == 0) {
                zeros += cached_;
                cache_ = 0;
                cached_ = 0;
                refill();
                if (cached_ == 0) {
                    overrun_ = true;
                    return zeros;
                }
                continue;
            }
            const auto leading = static_cast<unsigned>(std::countl_zero(cache_));
            zeros += leading;
            cache_ <<= leading;
            cache_ <<= 1U;
            cached_ -= leading + 1;
            return zeros;
        }
    }

    // Skips to the next byte boundary.
    void align() {
        const unsigned skip = cached_ % 8U;
        cache_ <<= skip;
        cached_ -= skip;
    }

    // The byte the next bit is in; only on a byte boundary.
    [[nodiscard]] std::size_t byte() const noexcept { return at_ - cached_ / 8U; }

    [[nodiscard]] bool overrun() const noexcept { return overrun_; }

private:
    void refill() {
        while (cached_ <= 56 && at_ < bytes_.size()) {
            cache_ |= static_cast<std::uint64_t>(bytes_[at_]) << (56U - cached_);
            cached_ += 8;
            ++at_;
        }
    }

    Bytes bytes_;
    std::size_t at_;
    std::uint64_t cache_ = 0;
    unsigned cached_ = 0;
    bool overrun_ = false;
};

struct StreamInfo {
    std::uint32_t sampleRate = 0;
    unsigned channels = 0;
    unsigned bitDepth = 0;
    std::uint64_t totalSamples = 0; // per channel; 0 when unknown
};

// How the two channels of a stereo frame are coded.
enum class Stereo : std::uint8_t { Independent, LeftSide, SideRight, MidSide };

using Unexpected = std::unexpected<std::string>;

// The residual of a subframe, into `samples` from `order` on.
std::expected<void, std::string> readResidual(BitReader& reader, std::span<std::int64_t> samples,
                                              unsigned order) {
    const std::uint32_t method = reader.bits(2);
    if (method > 1) {
        return Unexpected("a residual uses a reserved coding method");
    }
    const unsigned parameterBits = method == 0 ? 4U : 5U;
    const std::uint32_t escape = (1U << parameterBits) - 1U;
    const std::uint32_t partitionOrder = reader.bits(4);
    const std::size_t partitions = std::size_t{1} << partitionOrder;
    const std::size_t blockSize = samples.size();
    if (blockSize % partitions != 0 || (blockSize >> partitionOrder) < order) {
        return Unexpected("a residual's partitions do not fit its block");
    }
    const std::size_t perPartition = blockSize >> partitionOrder;
    std::size_t at = order;
    for (std::size_t partition = 0; partition < partitions; ++partition) {
        const std::size_t end = (partition + 1) * perPartition;
        const std::uint32_t parameter = reader.bits(parameterBits);
        if (parameter == escape) {
            const std::uint32_t width = reader.bits(5);
            for (; at < end; ++at) {
                samples[at] = reader.signedBits(width);
            }
            continue;
        }
        for (; at < end; ++at) {
            const std::uint64_t folded = (reader.unary() << parameter) | reader.bits(parameter);
            samples[at] =
                static_cast<std::int64_t>(folded >> 1U) ^ -static_cast<std::int64_t>(folded & 1U);
        }
        if (reader.overrun()) {
            return Unexpected("the file ends inside a frame");
        }
    }
    return {};
}

// One subframe of `blockSize` samples coded with `bitDepth` bits each.
std::expected<void, std::string> readSubframe(BitReader& reader, std::span<std::int64_t> samples,
                                              unsigned bitDepth) {
    if (reader.bits(1) != 0) {
        return Unexpected("a subframe does not start with a zero bit");
    }
    const std::uint32_t type = reader.bits(6);
    unsigned wasted = 0;
    if (reader.bits(1) != 0) {
        const std::uint64_t zeros = reader.unary();
        if (zeros + 1 >= bitDepth) {
            return Unexpected("a subframe wastes every bit");
        }
        wasted = static_cast<unsigned>(zeros) + 1;
    }
    const unsigned depth = bitDepth - wasted;
    const std::size_t blockSize = samples.size();

    if (type == 0) {
        const std::int64_t value = reader.signedBits(depth);
        std::ranges::fill(samples, value);
    } else if (type == 1) {
        for (std::int64_t& sample : samples) {
            sample = reader.signedBits(depth);
        }
    } else if (type >= 8 && type <= 12) {
        const unsigned order = type - 8;
        if (order > blockSize) {
            return Unexpected("a predictor is longer than its block");
        }
        for (unsigned i = 0; i < order; ++i) {
            samples[i] = reader.signedBits(depth);
        }
        if (auto residual = readResidual(reader, samples, order); !residual) {
            return residual;
        }
        std::int64_t* s = samples.data();
        switch (order) {
        case 1:
            for (std::size_t i = 1; i < blockSize; ++i) {
                s[i] += s[i - 1];
            }
            break;
        case 2:
            for (std::size_t i = 2; i < blockSize; ++i) {
                s[i] += 2 * s[i - 1] - s[i - 2];
            }
            break;
        case 3:
            for (std::size_t i = 3; i < blockSize; ++i) {
                s[i] += 3 * s[i - 1] - 3 * s[i - 2] + s[i - 3];
            }
            break;
        case 4:
            for (std::size_t i = 4; i < blockSize; ++i) {
                s[i] += 4 * s[i - 1] - 6 * s[i - 2] + 4 * s[i - 3] - s[i - 4];
            }
            break;
        default:
            break; // order 0: the residual is the signal
        }
    } else if (type >= 32) {
        const unsigned order = type - 31;
        if (order > blockSize) {
            return Unexpected("a predictor is longer than its block");
        }
        for (unsigned i = 0; i < order; ++i) {
            samples[i] = reader.signedBits(depth);
        }
        const unsigned precision = reader.bits(4) + 1;
        if (precision == 16) {
            return Unexpected("a predictor uses a forbidden precision");
        }
        const std::int64_t shift = reader.signedBits(5);
        if (shift < 0) {
            return Unexpected("a predictor shifts by a negative amount");
        }
        std::array<std::int64_t, 32> coefficients{};
        for (unsigned i = 0; i < order; ++i) {
            coefficients[i] = reader.signedBits(precision);
        }
        if (auto residual = readResidual(reader, samples, order); !residual) {
            return residual;
        }
        std::int64_t* s = samples.data();
        for (std::size_t i = order; i < blockSize; ++i) {
            std::int64_t prediction = 0;
            for (unsigned j = 0; j < order; ++j) {
                prediction += coefficients[j] * s[i - 1 - j];
            }
            s[i] += prediction >> shift;
        }
    } else {
        return Unexpected("a subframe is of a reserved kind");
    }
    if (wasted > 0) {
        for (std::int64_t& sample : samples) {
            sample *= std::int64_t{1} << wasted;
        }
    }
    return {};
}

// Decodes the frames from `at` on into `samples` (interleaved, scaled to
// [-1, 1]).
class FrameDecoder {
public:
    FrameDecoder(Bytes bytes, const StreamInfo& info)
        : bytes_(bytes), info_(info),
          scale_(1.0 / static_cast<double>(std::uint64_t{1} << (info.bitDepth - 1))) {}

    // Decodes the frame at `at` and returns the byte after it.
    std::expected<std::size_t, std::string> frame(std::size_t at, std::vector<float>& samples) {
        BitReader reader(bytes_, at);
        if (reader.bits(15) != 0x7FFC) {
            return Unexpected(std::format("no frame where one should start, at byte {}", at));
        }
        reader.bits(1); // the blocking strategy: frames are read in order either way
        const std::uint32_t blockCode = reader.bits(4);
        const std::uint32_t rateCode = reader.bits(4);
        const std::uint32_t channelCode = reader.bits(4);
        const std::uint32_t depthCode = reader.bits(3);
        if (reader.bits(1) != 0) {
            return Unexpected("a frame header sets a reserved bit");
        }
        if (auto number = codedNumber(reader); !number) {
            return Unexpected(number.error());
        }
        std::size_t blockSize = 0;
        if (blockCode == 0) {
            return Unexpected("a frame uses a reserved block size");
        }
        if (blockCode == 1) {
            blockSize = 192;
        } else if (blockCode <= 5) {
            blockSize = std::size_t{576} << (blockCode - 2);
        } else if (blockCode == 6) {
            blockSize = std::size_t{reader.bits(8)} + 1;
        } else if (blockCode == 7) {
            blockSize = std::size_t{reader.bits(16)} + 1;
            if (blockSize == 65536) {
                return Unexpected("a frame uses a forbidden block size");
            }
        } else {
            blockSize = std::size_t{256} << (blockCode - 8);
        }
        std::uint32_t rate = info_.sampleRate;
        if (rateCode == 15) {
            return Unexpected("a frame uses a forbidden sample rate");
        }
        if (rateCode >= 1 && rateCode <= 11) {
            static constexpr std::array<std::uint32_t, 12> kRates = {
                0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
            rate = kRates[rateCode];
        } else if (rateCode == 12) {
            rate = reader.bits(8) * 1000;
        } else if (rateCode == 13) {
            rate = reader.bits(16);
        } else if (rateCode == 14) {
            rate = reader.bits(16) * 10;
        }
        unsigned depth = info_.bitDepth;
        if (depthCode == 3) {
            return Unexpected("a frame uses a reserved bit depth");
        }
        if (depthCode != 0) {
            static constexpr std::array<unsigned, 8> kDepths = {0, 8, 12, 0, 16, 20, 24, 32};
            depth = kDepths[depthCode];
        }
        unsigned channels = 2;
        Stereo stereo = Stereo::Independent;
        if (channelCode <= 7) {
            channels = channelCode + 1;
        } else if (channelCode <= 10) {
            stereo = channelCode == 8   ? Stereo::LeftSide
                     : channelCode == 9 ? Stereo::SideRight
                                        : Stereo::MidSide;
        } else {
            return Unexpected("a frame uses a reserved channel layout");
        }
        if (rate != info_.sampleRate || depth != info_.bitDepth || channels != info_.channels) {
            return Unexpected("the sample rate, bit depth or channels change within the file");
        }
        const std::size_t headerEnd = reader.byte();
        if (reader.bits(8) != crc8(bytes_.subspan(at, headerEnd - at)) || reader.overrun()) {
            return Unexpected(std::format("the frame header at byte {} is damaged", at));
        }

        for (unsigned channel = 0; channel < channels; ++channel) {
            std::vector<std::int64_t>& buffer = channels_[channel];
            buffer.resize(blockSize);
            const bool side = (stereo == Stereo::LeftSide && channel == 1) ||
                              (stereo == Stereo::SideRight && channel == 0) ||
                              (stereo == Stereo::MidSide && channel == 1);
            if (auto read = readSubframe(reader, buffer, depth + (side ? 1U : 0U)); !read) {
                return Unexpected(read.error());
            }
        }
        reader.align();
        const std::size_t frameEnd = reader.byte();
        if (reader.bits(16) != crc16(bytes_.subspan(at, frameEnd - at)) || reader.overrun()) {
            return Unexpected(std::format("the frame at byte {} is damaged", at));
        }

        decorrelate(stereo, blockSize);
        const std::size_t first = samples.size();
        samples.resize(first + blockSize * channels);
        float* out = samples.data() + first;
        for (std::size_t i = 0; i < blockSize; ++i) {
            for (unsigned channel = 0; channel < channels; ++channel) {
                *out++ = static_cast<float>(static_cast<double>(channels_[channel][i]) * scale_);
            }
        }
        return frameEnd + 2;
    }

private:
    // The frame or sample number, coded like UTF-8 in up to 7 bytes.
    static std::expected<std::uint64_t, std::string> codedNumber(BitReader& reader) {
        const std::uint32_t first = reader.bits(8);
        if ((first & 0xC0U) == 0x80U || first == 0xFF) {
            return Unexpected("a frame number is not coded as it should be");
        }
        const auto more = static_cast<unsigned>(std::countl_one(static_cast<std::uint8_t>(first)));
        const unsigned extra = more == 0 ? 0 : more - 1;
        std::uint64_t number = first & (0xFFU >> (more + 1));
        for (unsigned i = 0; i < extra; ++i) {
            const std::uint32_t next = reader.bits(8);
            if ((next & 0xC0U) != 0x80U) {
                return Unexpected("a frame number is not coded as it should be");
            }
            number = (number << 6U) | (next & 0x3FU);
        }
        return number;
    }

    void decorrelate(Stereo stereo, std::size_t blockSize) {
        std::int64_t* a = channels_[0].data();
        std::int64_t* b = channels_[1].data();
        switch (stereo) {
        case Stereo::LeftSide: // left, side: right = left - side
            for (std::size_t i = 0; i < blockSize; ++i) {
                b[i] = a[i] - b[i];
            }
            break;
        case Stereo::SideRight: // side, right: left = side + right
            for (std::size_t i = 0; i < blockSize; ++i) {
                a[i] += b[i];
            }
            break;
        case Stereo::MidSide:
            for (std::size_t i = 0; i < blockSize; ++i) {
                const std::int64_t mid = a[i] * 2 + (b[i] & 1);
                const std::int64_t side = b[i];
                a[i] = (mid + side) >> 1;
                b[i] = (mid - side) >> 1;
            }
            break;
        case Stereo::Independent:
            break;
        }
    }

    Bytes bytes_;
    StreamInfo info_;
    double scale_;
    std::array<std::vector<std::int64_t>, 8> channels_{};
};

std::expected<std::vector<std::byte>, std::string> readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const std::streamoff size = file ? static_cast<std::streamoff>(file.tellg()) : -1;
    if (size < 0) {
        return Unexpected(std::format("cannot open {}", path.string()));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        return Unexpected(std::format("cannot read {}", path.string()));
    }
    return bytes;
}

// Where the "fLaC" marker is: at the start, or after an ID3v2 tag.
std::size_t flacStart(Bytes bytes) {
    if (startsWith(bytes, 0, "ID3") && bytes.size() >= 10) {
        // A syncsafe size: seven bits a byte, then a footer when flagged.
        const std::size_t size = (byteAt(bytes, 6) & 0x7FU) << 21U |
                                 (byteAt(bytes, 7) & 0x7FU) << 14U |
                                 (byteAt(bytes, 8) & 0x7FU) << 7U | (byteAt(bytes, 9) & 0x7FU);
        return 10 + size + ((byteAt(bytes, 5) & 0x10U) != 0 ? 10 : 0);
    }
    return 0;
}

} // namespace

std::expected<WavAudio, std::string> decodeFlac(Bytes bytes) {
    std::size_t at = flacStart(bytes);
    if (!startsWith(bytes, at, "fLaC")) {
        return Unexpected("not a FLAC file");
    }
    at += 4;
    StreamInfo info;
    bool haveInfo = false;
    for (bool last = false; !last;) {
        if (bytes.size() < at + 4) {
            return Unexpected("the file ends in its metadata");
        }
        const std::uint32_t header = byteAt(bytes, at);
        last = (header & 0x80U) != 0;
        const std::uint32_t type = header & 0x7FU;
        const std::size_t length =
            byteAt(bytes, at + 1) << 16U | byteAt(bytes, at + 2) << 8U | byteAt(bytes, at + 3);
        const std::size_t body = at + 4;
        if (bytes.size() < body + length) {
            return Unexpected("the file ends in its metadata");
        }
        if (type == 127) {
            return Unexpected("a metadata block is of a forbidden kind");
        }
        if (type == 0) {
            if (length < 34) {
                return Unexpected("the stream information is too short");
            }
            BitReader reader(bytes, body + 10); // after the block and frame sizes
            info.sampleRate = reader.bits(20);
            info.channels = reader.bits(3) + 1;
            info.bitDepth = reader.bits(5) + 1;
            info.totalSamples = static_cast<std::uint64_t>(reader.bits(4)) << 32U;
            info.totalSamples |= reader.bits(32);
            haveInfo = true;
        } else if (!haveInfo) {
            return Unexpected("the stream information is not the first metadata block");
        }
        at = body + length;
    }
    if (info.sampleRate == 0 || info.bitDepth < 4) {
        return Unexpected("the stream information is not valid");
    }

    WavAudio audio;
    audio.format = {.sampleRate = info.sampleRate,
                    .channels = static_cast<std::uint16_t>(info.channels)};
    // Silence compresses to almost nothing: trust the count only so far.
    if (info.totalSamples > 0 && info.totalSamples * info.channels <= std::size_t{1} << 28U) {
        audio.samples.reserve(static_cast<std::size_t>(info.totalSamples) * info.channels);
    }
    FrameDecoder decoder(bytes, info);
    const std::uint64_t wanted = info.totalSamples * info.channels;
    while (at + 2 <= bytes.size()) {
        if (wanted > 0 && audio.samples.size() >= wanted) {
            break; // whatever follows the last frame, such as a tag, is not audio
        }
        auto next = decoder.frame(at, audio.samples);
        if (!next) {
            return Unexpected(next.error());
        }
        at = *next;
    }
    if (wanted > 0 && audio.samples.size() != wanted) {
        return Unexpected(std::format("the file holds {} of its {} samples",
                                      audio.samples.size() / info.channels, info.totalSamples));
    }
    return audio;
}

std::expected<WavAudio, std::string> readFlac(const std::filesystem::path& path) {
    const auto bytes = readBytes(path);
    if (!bytes) {
        return Unexpected(bytes.error());
    }
    return decodeFlac(*bytes);
}

std::expected<WavAudio, std::string> decodeAudioFile(Bytes bytes) {
    if (startsWith(bytes, flacStart(bytes), "fLaC")) {
        return decodeFlac(bytes);
    }
    return decodeWav(bytes);
}

std::expected<WavAudio, std::string> readAudioFile(const std::filesystem::path& path) {
    const auto bytes = readBytes(path);
    if (!bytes) {
        return Unexpected(bytes.error());
    }
    return decodeAudioFile(*bytes);
}

} // namespace elara::audio
