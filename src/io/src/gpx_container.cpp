// SPDX-License-Identifier: AGPL-3.0-or-later
// Guitar Pro 6 container (ADR-0010). A .gpx file is "BCFZ", the size of the
// unpacked data and a bit stream of literal bytes and back references that
// unpacks to "BCFS": a file system of 4096-byte sectors whose entries name a
// file, give its size and list the sectors that hold it. The format has no
// public specification; this reader follows the structure of the files.
#include "gpx_container.hpp"

#include <elara/io/result.hpp>

#include <algorithm>
#include <format>
#include <vector>

namespace elara::io::detail {

namespace {

constexpr std::size_t kSector = 0x1000;
constexpr std::size_t kMaxUnpacked = std::size_t{64} << 20U;
constexpr std::uint32_t kFileEntry = 2;
constexpr std::size_t kNameAt = 0x04;
constexpr std::size_t kNameSize = 127;
constexpr std::size_t kSizeAt = 0x8C;
constexpr std::size_t kSectorsAt = 0x94;

FormatError damaged(std::string_view what) {
    return FormatError{std::format("Guitar Pro: damaged Guitar Pro 6 file: {}", what)};
}

bool startsWith(std::span<const std::uint8_t> bytes, std::string_view prefix) {
    return bytes.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), bytes.begin(),
                      [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; });
}

std::uint32_t le32(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 4; i > 0; --i) {
        value = (value << 8U) | bytes[at + i - 1];
    }
    return value;
}

/// Bits from the most significant down, byte after byte.
class BitReader {
public:
    struct EndOfInput {};

    explicit BitReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    unsigned bit() {
        if (byte_ >= bytes_.size()) {
            throw EndOfInput{};
        }
        const unsigned value = (static_cast<unsigned>(bytes_[byte_]) >> (7U - bit_)) & 1U;
        if (++bit_ == 8) {
            bit_ = 0;
            ++byte_;
        }
        return value;
    }

    /// `count` bits, the first one the most significant.
    unsigned bits(unsigned count) {
        unsigned value = 0;
        for (unsigned i = 0; i < count; ++i) {
            value = (value << 1U) | bit();
        }
        return value;
    }

    /// `count` bits, the first one the least significant.
    unsigned bitsReversed(unsigned count) {
        unsigned value = 0;
        for (unsigned i = 0; i < count; ++i) {
            value |= bit() << i;
        }
        return value;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t byte_ = 0;
    unsigned bit_ = 0;
};

/// After "BCFZ": the unpacked size (32 bits, little-endian), then chunks. A 0
/// bit and two bits (least significant first) count 0-3 literal bytes; a 1 bit,
/// a four-bit word size and two numbers of that many bits (least significant
/// first) copy `length` earlier bytes from `offset` bytes before the end, but
/// never more than `offset`.
std::vector<std::uint8_t> unpack(std::span<const std::uint8_t> packed) {
    if (packed.size() < 4) {
        throw damaged("it ends early");
    }
    const std::size_t expected = le32(packed, 0);
    if (expected > kMaxUnpacked) {
        throw damaged("it claims more than 64 MB");
    }
    std::vector<std::uint8_t> out;
    out.reserve(std::min(expected, packed.size() * 16));
    BitReader in(packed.subspan(4));
    try {
        while (out.size() < expected) {
            if (in.bit() == 1) {
                const unsigned wordSize = in.bits(4);
                const std::size_t offset = in.bitsReversed(wordSize);
                const std::size_t length = in.bitsReversed(wordSize);
                if (offset == 0 || offset > out.size()) {
                    throw damaged("a reference before the start");
                }
                const std::size_t from = out.size() - offset;
                const std::size_t count = std::min(offset, length);
                for (std::size_t i = 0; i < count; ++i) {
                    const std::uint8_t value = out[from + i];
                    out.push_back(value);
                }
            } else {
                const unsigned count = in.bitsReversed(2);
                for (unsigned i = 0; i < count; ++i) {
                    out.push_back(static_cast<std::uint8_t>(in.bits(8)));
                }
            }
        }
    } catch (const BitReader::EndOfInput&) {
        // A stream may stop short of the announced size; keep what it holds.
    }
    if (out.size() > expected) {
        out.resize(expected);
    }
    return out;
}

/// Looks `name` up in a "BCFS" file system (`fs` starts after the four
/// letters). Sector 0 is unused; each file entry is followed by its data
/// sectors, so the next entry comes after the last of them.
std::optional<std::string> findFile(std::span<const std::uint8_t> fs, std::string_view name) {
    for (std::size_t offset = kSector; offset + 4 <= fs.size(); offset += kSector) {
        if (le32(fs, offset) != kFileEntry) {
            continue;
        }
        if (offset + kSectorsAt > fs.size()) {
            throw damaged("a file entry is cut");
        }
        const auto nameField = fs.subspan(offset + kNameAt, kNameSize);
        const std::string entry(nameField.begin(), std::ranges::find(nameField, 0));
        const std::size_t size = le32(fs, offset + kSizeAt);
        const bool wanted = entry == name;
        std::string content;
        std::size_t last = offset;
        const std::size_t end = std::min(fs.size(), offset + kSector);
        for (std::size_t slot = offset + kSectorsAt; slot + 4 <= end; slot += 4) {
            const std::size_t sector = le32(fs, slot);
            if (sector == 0) {
                break;
            }
            if (sector > (fs.size() - 1) / kSector) {
                throw damaged(std::format("{} names a sector past the end", entry));
            }
            const std::size_t start = sector * kSector;
            if (wanted && content.size() < size) {
                const std::size_t stop = std::min(fs.size(), start + kSector);
                content.append(fs.begin() + static_cast<std::ptrdiff_t>(start),
                               fs.begin() + static_cast<std::ptrdiff_t>(stop));
            }
            last = std::max(last, start);
        }
        if (wanted) {
            if (content.size() > size) {
                content.resize(size);
            }
            return content;
        }
        offset = last;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> gpxEntry(std::span<const std::uint8_t> bytes, std::string_view name) {
    if (startsWith(bytes, "BCFZ")) {
        const std::vector<std::uint8_t> unpacked = unpack(bytes.subspan(4));
        if (!startsWith(unpacked, "BCFS")) {
            throw damaged("the unpacked data is not a file system");
        }
        return findFile(std::span(unpacked).subspan(4), name);
    }
    if (startsWith(bytes, "BCFS")) {
        return findFile(bytes.subspan(4), name);
    }
    throw damaged("it is not a container");
}

} // namespace elara::io::detail
