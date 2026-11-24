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

/// Bits from the most significant down, byte after byte. Up to eight bytes wait
/// in a buffer, so most reads take a shift and a mask.
class BitReader {
public:
    struct EndOfInput {};

    explicit BitReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    unsigned bit() { return bits(1); }

    /// `count` bits (at most 32), the first one the most significant. Throws
    /// EndOfInput when fewer are left.
    unsigned bits(unsigned count) {
        if (count > available_) {
            refill(count);
        }
        available_ -= count;
        const std::uint64_t mask = (std::uint64_t{1} << count) - 1U;
        return static_cast<unsigned>((buffer_ >> available_) & mask);
    }

    /// `count` bits, the first one the least significant.
    unsigned bitsReversed(unsigned count) {
        unsigned value = bits(count);
        unsigned reversed = 0;
        for (unsigned i = 0; i < count; ++i) {
            reversed = (reversed << 1U) | (value & 1U);
            value >>= 1U;
        }
        return reversed;
    }

private:
    void refill(unsigned count) {
        while (available_ <= 56 && byte_ < bytes_.size()) {
            buffer_ = (buffer_ << 8U) | bytes_[byte_++];
            available_ += 8;
        }
        if (count > available_) {
            throw EndOfInput{};
        }
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t byte_ = 0;
    std::uint64_t buffer_ = 0;
    unsigned available_ = 0; // bits of buffer_ not read yet, the lowest ones
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
                // The copy never reaches the bytes it adds: count <= offset.
                const std::size_t at = out.size();
                const std::size_t count = std::min(offset, length);
                out.resize(at + count);
                std::copy_n(out.begin() + static_cast<std::ptrdiff_t>(at - offset), count,
                            out.begin() + static_cast<std::ptrdiff_t>(at));
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

GpxContainer::GpxContainer(std::span<const std::uint8_t> bytes) {
    if (startsWith(bytes, "BCFZ")) {
        unpacked_ = unpack(bytes.subspan(4));
        if (!startsWith(unpacked_, "BCFS")) {
            throw damaged("the unpacked data is not a file system");
        }
    } else if (startsWith(bytes, "BCFS")) {
        plain_ = bytes;
    } else {
        throw damaged("it is not a container");
    }
}

std::optional<std::string> GpxContainer::entry(std::string_view name) const {
    const std::span<const std::uint8_t> fs = unpacked_.empty() ? plain_ : std::span(unpacked_);
    return findFile(fs.subspan(4), name);
}

} // namespace elara::io::detail
