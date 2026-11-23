// SPDX-License-Identifier: AGPL-3.0-or-later
// Guitar Pro import (ADR-0010).
#pragma once

#include <elara/io/result.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>

namespace elara::io {

struct GuitarProOptions {
    /// Threads that read the staves: 0 decides from the score size and the
    /// machine, 1 reads everything on the calling thread. The result never
    /// depends on it. Guitar Pro 3-5 files are always read on one thread.
    unsigned threads = 0;
};

/// Reads a Guitar Pro file: Guitar Pro 7 and 8 files (.gp: a zip archive holding
/// the score as GPIF XML), Guitar Pro 6 files (.gpx: a compressed container
/// holding GPIF XML) and Guitar Pro 3, 4 and 5 files (.gp3, .gp4, .gp5: a binary
/// stream). Tracks keep their tuning, capo, staves and fret positions (a track
/// Guitar Pro 6-8 shows without tablature keeps a tuning only on a guitar or a
/// bass: Guitar Pro gives every track one); techniques the model holds (bends, slides, hammer-ons,
/// harmonics, palm mutes, vibrato...) are kept, and what it cannot hold (repeats, grace notes,
/// lyrics, chord diagrams...) is listed in ImportResult::warnings. Fails when the bytes are not a
/// Guitar Pro file or the score is damaged.
[[nodiscard]] std::expected<ImportResult, FormatError>
importGuitarPro(std::span<const std::uint8_t> bytes, const GuitarProOptions& options = {});

/// Reads a .gp, .gpx, .gp5, .gp4 or .gp3 file.
[[nodiscard]] std::expected<ImportResult, FormatError>
importGuitarProFile(const std::filesystem::path& path, const GuitarProOptions& options = {});

/// Reads a GPIF document: the `Content/score.gpif` XML of a .gp file, or the
/// `score.gpif` of a .gpx container.
[[nodiscard]] std::expected<ImportResult, FormatError>
importGpif(std::string_view xml, const GuitarProOptions& options = {});

} // namespace elara::io
