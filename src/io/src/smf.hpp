// SPDX-License-Identifier: AGPL-3.0-or-later
// Standard MIDI File chunks and events (ADR-0009): the byte level shared by the MIDI
// reader and writer. Internal to elara_io.
#pragma once

#include <elara/io/result.hpp>

#include "warnings.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace elara::io::detail {

inline constexpr std::uint8_t kMetaEvent = 0xFF;
inline constexpr std::uint8_t kSysEx = 0xF0;
inline constexpr std::uint8_t kSysExContinuation = 0xF7;

inline constexpr std::uint8_t kMetaText = 0x01;
inline constexpr std::uint8_t kMetaCopyright = 0x02;
inline constexpr std::uint8_t kMetaTrackName = 0x03;
inline constexpr std::uint8_t kMetaInstrumentName = 0x04;
inline constexpr std::uint8_t kMetaLyric = 0x05;
inline constexpr std::uint8_t kMetaMarker = 0x06;
inline constexpr std::uint8_t kMetaEndOfTrack = 0x2F;
inline constexpr std::uint8_t kMetaTempo = 0x51;
inline constexpr std::uint8_t kMetaTimeSignature = 0x58;
inline constexpr std::uint8_t kMetaKeySignature = 0x59;

inline constexpr std::uint8_t kNoteOff = 0x80;
inline constexpr std::uint8_t kNoteOn = 0x90;
inline constexpr std::uint8_t kController = 0xB0;
inline constexpr std::uint8_t kProgramChange = 0xC0;
inline constexpr std::uint8_t kPitchBend = 0xE0;

inline constexpr int kPercussionChannel = 9; // channel 10 for musicians
inline constexpr int kMaxDivision = 0x7FFF;  // ticks per quarter note

/// One event at an absolute tick. Channel messages use `status` (kind | channel)
/// and `data`; meta events use status kMetaEvent, `type` and `payload`; system
/// exclusive messages use status kSysEx or kSysExContinuation and `payload`.
struct MidiEvent {
    std::int64_t tick = 0;
    std::uint8_t status = 0;
    std::uint8_t type = 0;
    std::array<std::uint8_t, 2> data{};
    std::string payload{};

    [[nodiscard]] bool isChannelMessage() const noexcept { return status >= 0x80 && status < 0xF0; }
    [[nodiscard]] int kind() const noexcept { return status & 0xF0; }
    [[nodiscard]] int channel() const noexcept { return status & 0x0F; }
};

struct MidiTrack {
    std::vector<MidiEvent> events{}; // ascending ticks, without the end-of-track event
    std::int64_t end = 0;            // tick of the end-of-track event
};

struct MidiFile {
    int format = 1;
    int division = 960; // ticks per quarter note
    std::vector<MidiTrack> tracks{};
};

/// Number of data bytes after a channel status byte.
[[nodiscard]] constexpr int dataBytes(std::uint8_t status) noexcept {
    const int kind = status & 0xF0;
    return kind == kProgramChange || kind == 0xD0 ? 1 : 2;
}

/// Encodes the file. Events must be in ascending tick order; each track gets its
/// end-of-track event at max(end, last event). Channel messages use running status.
[[nodiscard]] std::string writeSmf(const MidiFile& file);

/// The body of one track's MTrk chunk, encoded as writeSmf() does: tracks can be
/// encoded on several threads and joined with the overload below.
[[nodiscard]] std::string encodeTrack(const MidiTrack& track);

/// A file from its header fields and its tracks' encoded bodies, in order.
[[nodiscard]] std::string writeSmf(int format, int division, std::span<const std::string> bodies);

/// Decodes a file. Fails on anything that is not a Standard MIDI File with a
/// ticks-per-quarter division; tolerates (with a warning) chunk lengths that run
/// past the end of the file and tracks without an end-of-track event.
[[nodiscard]] std::expected<MidiFile, FormatError> readSmf(std::span<const std::uint8_t> bytes,
                                                           Warnings& warnings);

} // namespace elara::io::detail
