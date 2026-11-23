// SPDX-License-Identifier: AGPL-3.0-or-later
#include "smf.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string_view>

namespace elara::io::detail {

namespace {

// ---- Writing --------------------------------------------------------------

void putByte(std::string& out, int value) {
    out.push_back(static_cast<char>(static_cast<std::uint8_t>(value)));
}

void putBigEndian(std::string& out, std::uint32_t value, int bytes) {
    for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) {
        putByte(out, static_cast<int>((value >> shift) & 0xFFU));
    }
}

// Variable-length quantity: seven bits per byte, most significant first.
void putVarLength(std::string& out, std::uint32_t value) {
    std::array<std::uint8_t, 5> groups{};
    std::size_t count = 0;
    do {
        groups[count++] = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7;
    } while (value != 0);
    while (count > 1) {
        putByte(out, groups[--count] | 0x80);
    }
    putByte(out, groups[0]);
}

std::string encodeEvents(const MidiTrack& track) {
    std::string out;
    std::int64_t tick = 0;
    std::uint8_t running = 0;
    const auto delta = [&](std::int64_t at) {
        putVarLength(out, static_cast<std::uint32_t>(std::max<std::int64_t>(at - tick, 0)));
        tick = std::max(tick, at);
    };
    for (const MidiEvent& event : track.events) {
        delta(event.tick);
        if (event.isChannelMessage()) {
            if (event.status != running) {
                putByte(out, event.status);
                running = event.status;
            }
            putByte(out, event.data[0]);
            if (dataBytes(event.status) == 2) {
                putByte(out, event.data[1]);
            }
            continue;
        }
        running = 0; // meta and system exclusive events cancel running status
        putByte(out, event.status);
        if (event.status == kMetaEvent) {
            putByte(out, event.type);
        }
        putVarLength(out, static_cast<std::uint32_t>(event.payload.size()));
        out += event.payload;
    }
    delta(track.end);
    putByte(out, kMetaEvent);
    putByte(out, kMetaEndOfTrack);
    putByte(out, 0);
    return out;
}

// ---- Reading --------------------------------------------------------------

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }

    [[nodiscard]] std::expected<std::uint8_t, FormatError> byte() {
        if (offset_ >= bytes_.size()) {
            return fail("unexpected end of data");
        }
        return bytes_[offset_++];
    }

    [[nodiscard]] std::expected<std::uint32_t, FormatError> bigEndian(int count) {
        std::uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            const auto b = byte();
            if (!b) {
                return std::unexpected(b.error());
            }
            value = (value << 8) | *b;
        }
        return value;
    }

    // At most four bytes (values below 2^28), as the format requires.
    [[nodiscard]] std::expected<std::uint32_t, FormatError> varLength() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const auto b = byte();
            if (!b) {
                return std::unexpected(b.error());
            }
            value = (value << 7) | (*b & 0x7FU);
            if ((*b & 0x80U) == 0) {
                return value;
            }
        }
        return fail("variable-length number longer than four bytes");
    }

    [[nodiscard]] std::expected<std::string, FormatError> text(std::uint32_t length) {
        if (length > remaining()) {
            return fail("event runs past the end of its track");
        }
        std::string result(reinterpret_cast<const char*>(bytes_.data() + offset_), length);
        offset_ += length;
        return result;
    }

    void skip(std::size_t count) noexcept { offset_ += std::min(count, remaining()); }

    [[nodiscard]] std::unexpected<FormatError> fail(std::string_view what) const {
        return std::unexpected(FormatError{std::format("MIDI: {} at byte {}", what, offset_)});
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

// Decodes one track chunk's events. `base` is the chunk's offset in the file,
// for error messages.
std::expected<MidiTrack, FormatError> readTrack(std::span<const std::uint8_t> chunk,
                                                std::size_t base, std::size_t number,
                                                Warnings& warnings) {
    Reader in(chunk);
    MidiTrack track;
    std::int64_t tick = 0;
    std::uint8_t running = 0;
    std::size_t eventStart = 0;
    const auto fail = [&](std::string_view what) {
        return std::unexpected(FormatError{std::format(
            "MIDI: {} in the event at byte {} (track {})", what, base + eventStart, number + 1)});
    };
    while (in.remaining() > 0) {
        eventStart = in.offset();
        const auto delta = in.varLength();
        if (!delta) {
            return fail("damaged event time");
        }
        tick += *delta;
        auto status = in.byte();
        if (!status) {
            return fail("event without a status byte");
        }
        MidiEvent event{.tick = tick};
        if (*status < 0x80) { // running status: this was the first data byte
            if (running == 0) {
                return fail("data byte without a status byte");
            }
            event.status = running;
            event.data[0] = *status;
        } else if (*status < 0xF0) {
            running = *status;
            event.status = *status;
            const auto first = in.byte();
            if (!first) {
                return fail("channel message cut short");
            }
            event.data[0] = *first;
        } else {
            running = 0;
            event.status = *status;
        }
        if (event.isChannelMessage()) {
            if (dataBytes(event.status) == 2) {
                const auto second = in.byte();
                if (!second) {
                    return fail("channel message cut short");
                }
                event.data[1] = *second;
            }
            if (event.data[0] >= 0x80 || event.data[1] >= 0x80) {
                return fail("channel message with a status byte as data");
            }
            track.events.push_back(std::move(event));
            continue;
        }
        if (event.status == kMetaEvent) {
            const auto type = in.byte();
            if (!type) {
                return fail("meta event cut short");
            }
            event.type = *type;
        } else if (event.status != kSysEx && event.status != kSysExContinuation) {
            return fail(std::format("unexpected status byte 0x{:02X}", event.status));
        }
        const auto length = in.varLength();
        if (!length) {
            return fail("damaged event length");
        }
        auto payload = in.text(*length);
        if (!payload) {
            return fail("event runs past the end of its track");
        }
        event.payload = std::move(*payload);
        if (event.status == kMetaEvent && event.type == kMetaEndOfTrack) {
            track.end = tick;
            return track;
        }
        track.events.push_back(std::move(event));
    }
    warnings.add("", std::format("track {} has no end-of-track event", number + 1));
    track.end = tick;
    return track;
}

} // namespace

std::string encodeTrack(const MidiTrack& track) {
    return encodeEvents(track);
}

std::string writeSmf(int format, int division, std::span<const std::string> bodies) {
    std::size_t size = 14;
    for (const std::string& body : bodies) {
        size += 8 + body.size();
    }
    std::string out;
    out.reserve(size);
    out += "MThd";
    putBigEndian(out, 6, 4);
    putBigEndian(out, static_cast<std::uint32_t>(format), 2);
    putBigEndian(out, static_cast<std::uint32_t>(bodies.size()), 2);
    putBigEndian(out, static_cast<std::uint32_t>(division), 2);
    for (const std::string& body : bodies) {
        out += "MTrk";
        putBigEndian(out, static_cast<std::uint32_t>(body.size()), 4);
        out += body;
    }
    return out;
}

std::string writeSmf(const MidiFile& file) {
    std::vector<std::string> bodies;
    bodies.reserve(file.tracks.size());
    for (const MidiTrack& track : file.tracks) {
        bodies.push_back(encodeTrack(track));
    }
    return writeSmf(file.format, file.division, bodies);
}

std::expected<MidiFile, FormatError> readSmf(std::span<const std::uint8_t> bytes,
                                             Warnings& warnings) {
    Reader in(bytes);
    const auto id = in.text(bytes.size() < 4 ? 0U : 4U);
    if (!id || *id != "MThd") {
        return std::unexpected(FormatError{"MIDI: not a Standard MIDI File (no MThd header)"});
    }
    const auto headerLength = in.bigEndian(4);
    if (!headerLength || *headerLength < 6 || *headerLength > in.remaining()) {
        return std::unexpected(FormatError{"MIDI: damaged MThd header"});
    }
    const std::uint32_t format = *in.bigEndian(2);
    const std::uint32_t declaredTracks = *in.bigEndian(2);
    const std::uint32_t division = *in.bigEndian(2);
    in.skip(*headerLength - 6);
    if (format > 2) {
        return std::unexpected(FormatError{std::format("MIDI: unknown format {}", format)});
    }
    if ((division & 0x8000U) != 0) {
        return std::unexpected(FormatError{"MIDI: SMPTE time division is not supported"});
    }
    if (division == 0) {
        return std::unexpected(FormatError{"MIDI: zero ticks per quarter note"});
    }
    MidiFile file{.format = static_cast<int>(format), .division = static_cast<int>(division)};
    while (in.remaining() >= 8) {
        const std::size_t chunkStart = in.offset();
        const std::string type = *in.text(4);
        std::uint32_t length = *in.bigEndian(4);
        if (length > in.remaining()) {
            warnings.add("",
                         std::format("chunk at byte {} runs past the end of the file", chunkStart));
            length = static_cast<std::uint32_t>(in.remaining());
        }
        if (type == "MTrk") {
            auto track = readTrack(bytes.subspan(in.offset(), length), in.offset(),
                                   file.tracks.size(), warnings);
            if (!track) {
                return std::unexpected(track.error());
            }
            file.tracks.push_back(std::move(*track));
        }
        in.skip(length);
    }
    if (file.tracks.size() != declaredTracks) {
        warnings.add("", std::format("the header announces {} tracks, the file has {}",
                                     declaredTracks, file.tracks.size()));
    }
    return file;
}

} // namespace elara::io::detail
