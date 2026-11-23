// SPDX-License-Identifier: AGPL-3.0-or-later
// MusicXML import (ADR-0007): score-partwise documents, plain or compressed (.mxl).
#pragma once

#include <elara/io/result.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>

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

} // namespace elara::io
