// SPDX-License-Identifier: AGPL-3.0-or-later
// Standard MIDI File import and export (ADR-0009).
#pragma once

#include <elara/core/song.hpp>
#include <elara/io/result.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace elara::io {

struct MidiOptions {
    /// Threads that read or write the tracks: 0 decides from the number of notes
    /// and the machine, 1 does everything on the calling thread. The result never
    /// depends on it.
    unsigned threads = 0;
};

/// Reads a Standard MIDI File (format 0 or 1; format 2 is read as format 1).
/// Notes are placed on a rhythm grid chosen per beat, the simplest one that
/// holds them exactly or, for played (unquantized) music, the closest one; then
/// written as notes, rests, ties and tuplets in up to four voices per staff.
/// Pitches are spelled in the key in force. Each channel of each track becomes a
/// track whose instrument comes from its program (channel 10: drums). Fails only
/// when the bytes are not a Standard MIDI File; what the model cannot hold is
/// listed in ImportResult::warnings.
[[nodiscard]] std::expected<ImportResult, FormatError>
importMidi(std::span<const std::uint8_t> bytes, const MidiOptions& options = {});

/// Reads a .mid, .midi or .smf file.
[[nodiscard]] std::expected<ImportResult, FormatError>
importMidiFile(const std::filesystem::path& path, const MidiOptions& options = {});

/// Writes the song as a format 1 Standard MIDI File: a conductor track with the
/// title, copyright, meters, keys and tempos, then one track per track with its
/// name, instrument name, program and notes (ties joined, velocity from the
/// dynamic; drums on channel 10). What MIDI cannot hold (fret positions, guitar
/// techniques, articulations...) is listed in the warnings.
[[nodiscard]] ExportResult exportMidi(const core::Song& song, const MidiOptions& options = {});

/// Writes a .mid file atomically and returns the warnings. Throws
/// std::runtime_error (with the path) on I/O failure.
std::vector<std::string> exportMidiFile(const core::Song& song, const std::filesystem::path& path);

} // namespace elara::io
