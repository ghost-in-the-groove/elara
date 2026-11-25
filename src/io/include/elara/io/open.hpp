// SPDX-License-Identifier: AGPL-3.0-or-later
// Opening any file Elara reads, chosen by its extension.
#pragma once

#include <elara/io/result.hpp>

#include <expected>
#include <filesystem>
#include <span>
#include <string_view>

namespace elara::io {

/// Reads a song from a native .elara file (no warnings) or imports it from
/// MusicXML (.musicxml, .xml, .mxl), MIDI (.mid, .midi, .smf, .kar) or Guitar Pro
/// (.gp, .gpx, .gp5, .gp4, .gp3), by the file's extension in any case. An
/// unknown extension is an "unsupported file type" error.
[[nodiscard]] std::expected<ImportResult, FormatError> openFile(const std::filesystem::path& path);

/// The extensions openFile() reads, lower case with their dot.
[[nodiscard]] std::span<const std::string_view> openExtensions() noexcept;

} // namespace elara::io
