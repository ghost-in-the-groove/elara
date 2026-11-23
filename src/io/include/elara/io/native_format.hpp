// SPDX-License-Identifier: AGPL-3.0-or-later
// The native .elara format (ADR-0006): a zip archive holding song.json.
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

inline constexpr int kNativeFormatVersion = 1;
inline constexpr std::string_view kNativeMimeType = "application/vnd.elara.song+zip";
inline constexpr std::string_view kNativeExtension = ".elara";

/// The song as the song.json text of a native file (compact JSON).
[[nodiscard]] std::string songToJson(const core::Song& song);

/// Reads song.json text. Errors start with the JSON path of the problem when
/// there is one, e.g. "tracks[0].staves[0].clef: unknown clef: x". Rejects invalid JSON, a newer
/// formatVersion and any value the model does not accept; ignores keys it does not know.
[[nodiscard]] std::expected<core::Song, FormatError> songFromJson(std::string_view json);

/// The bytes of a native file: a zip archive with "mimetype" and "song.json".
[[nodiscard]] std::vector<std::uint8_t> encodeNative(const core::Song& song);
[[nodiscard]] std::expected<core::Song, FormatError>
decodeNative(std::span<const std::uint8_t> bytes);

/// Writes a native file atomically: a temporary file next to `path`, then a
/// rename over it. Throws std::runtime_error (with the path) on I/O failure.
void saveNative(const core::Song& song, const std::filesystem::path& path);
[[nodiscard]] std::expected<core::Song, FormatError> loadNative(const std::filesystem::path& path);

} // namespace elara::io
