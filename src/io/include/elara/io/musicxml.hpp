// SPDX-License-Identifier: AGPL-3.0-or-later
// MusicXML import (ADR-0007) and export (ADR-0008): score-partwise documents, plain or
// compressed (.mxl).
#pragma once

#include <elara/core/song.hpp>
#include <elara/io/result.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace elara::io {

struct MusicXmlOptions {
    /// Threads that read the parts after the first: 0 decides from the
    /// document size and the machine, 1 reads everything on the calling thread.
    /// The result never depends on it.
    unsigned threads = 0;
};

/// Reads an uncompressed MusicXML document. Fails only when the text is not a
/// readable score-partwise document; features the model cannot hold are
/// dropped and listed in ImportResult::warnings.
[[nodiscard]] std::expected<ImportResult, FormatError>
importMusicXml(std::string_view xml, const MusicXmlOptions& options = {});

/// Reads either form: a compressed .mxl archive (detected by its zip signature;
/// META-INF/container.xml names the score) or uncompressed text.
[[nodiscard]] std::expected<ImportResult, FormatError>
importMusicXmlBytes(std::span<const std::uint8_t> bytes, const MusicXmlOptions& options = {});

/// Reads a .musicxml, .xml or .mxl file.
[[nodiscard]] std::expected<ImportResult, FormatError>
importMusicXmlFile(const std::filesystem::path& path, const MusicXmlOptions& options = {});

struct MusicXmlExportOptions {
    /// A compressed .mxl archive instead of MusicXML text.
    bool compressed = false;
    /// Threads that write the parts: 0 decides from the song size and the
    /// machine, 1 writes everything on the calling thread. The result never
    /// depends on it.
    unsigned threads = 0;
};

/// Writes the song as a MusicXML 4.0 score-partwise document. A single-staff
/// fretted track gets a TAB staff next to its standard staff. What MusicXML
/// cannot hold (let ring, whammy bar...) is left out and listed in the warnings;
/// everything else imports back to the same song.
[[nodiscard]] ExportResult exportMusicXml(const core::Song& song,
                                          const MusicXmlExportOptions& options = {});

/// Writes a .musicxml or .xml file, or a compressed .mxl file (chosen by the
/// extension), atomically. Returns the warnings. Throws std::runtime_error (with
/// the path) on I/O failure.
std::vector<std::string> exportMusicXmlFile(const core::Song& song,
                                            const std::filesystem::path& path,
                                            const MusicXmlExportOptions& options = {});

} // namespace elara::io
